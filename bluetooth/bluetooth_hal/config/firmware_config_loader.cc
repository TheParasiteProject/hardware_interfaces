/*
 * Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "bthal.fw_config"

#include "bluetooth_hal/config/firmware_config_loader.h"

#include <asm-generic/fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "android-base/logging.h"
#include "bluetooth_hal/config/config_constants.h"
#include "bluetooth_hal/config/hal_config_loader.h"
#include "bluetooth_hal/hal_types.h"
#include "bluetooth_hal/util/system_call_wrapper.h"
#include "firmware_config.pb.h"
#include "google/protobuf/util/json_util.h"

namespace bluetooth_hal {
namespace config {
namespace {

using ::bluetooth_hal::config::proto::FirmwareConfigForTransport;
using ::bluetooth_hal::config::proto::FirmwareConfigsContainer;
using ::bluetooth_hal::hci::HciPacketType;
using ::bluetooth_hal::transport::TransportType;
using ::bluetooth_hal::util::SystemCallWrapper;

using ::google::protobuf::RepeatedField;
using ::google::protobuf::util::JsonParseOptions;
using ::google::protobuf::util::JsonStringToMessage;
using ::google::protobuf::util::Status;

namespace cfg_consts = ::bluetooth_hal::config::constants;

// Used for downloading firmware data.
constexpr uint16_t kHciVscLaunchRamOpcode = 0xfc4e;

enum class DataLoadingType : int {
  kByPacket = 0,
  kByAccumulation,
};

constexpr uint16_t GetOpcode(std::span<const uint8_t> packet) {
  if (packet.size() < 2) {
    // Invalid packet.
    return 0;
  }
  return (packet[1] << 8) | packet[0];
}

}  // namespace

class FirmwareConfigLoaderImpl : public FirmwareConfigLoader {
 public:
  FirmwareConfigLoaderImpl();
  ~FirmwareConfigLoaderImpl() override = default;

  bool LoadConfig() override;
  bool LoadConfigFromFile(std::string_view path) override;
  bool LoadConfigFromString(std::string_view content) override;

  bool SelectFirmwareConfiguration(TransportType transport_type) override;

  bool ResetFirmwareDataLoadingState() override;

  std::optional<DataPacket> GetNextFirmwareData() override;

  std::optional<std::reference_wrapper<const SetupCommandPacket>>
  GetSetupCommandPacket(SetupCommandType command_type) const override;

  int GetLoadMiniDrvDelayMs() const override;
  int GetLaunchRamDelayMs() const override;

  std::string DumpConfigToString() const override;

 private:
  void LoadSetupCommandsFromConfig(
      const FirmwareConfigForTransport& config,
      std::unordered_map<SetupCommandType, std::unique_ptr<SetupCommandPacket>>&
          target_map);

  bool OpenNextFirmwareFile();

  std::optional<DataPacket> GetNextFirmwareDataByPacket();
  std::optional<DataPacket> GetNextFirmwareDataByAccumulation();

  std::unordered_map<TransportType, FirmwareConfigForTransport>
      transport_specific_configs_;
  std::optional<std::reference_wrapper<const FirmwareConfigForTransport>>
      active_config_;
  std::unordered_map<SetupCommandType, std::unique_ptr<SetupCommandPacket>>
      active_setup_commands_;

  std::mutex firmware_data_mutex_;
  std::vector<std::string> current_firmware_filenames_;
  int current_firmware_file_index_;

  std::optional<std::vector<uint8_t>> previous_header_;
  int firmware_file_fd_{-1};
};

bool FirmwareConfigLoaderImpl::ResetFirmwareDataLoadingState() {
  if (!active_config_ || !active_config_->get().has_firmware_folder_name() ||
      current_firmware_filenames_.empty()) {
    LOG(ERROR) << __func__
               << ": No active config, firmware folder not set, or firmware "
                  "file list is empty.";
    return false;
  }

  std::scoped_lock lock(firmware_data_mutex_);
  if (firmware_file_fd_ != -1) {
    SystemCallWrapper::GetWrapper().Close(firmware_file_fd_);
    firmware_file_fd_ = -1;
  }

  current_firmware_file_index_ = -1;
  return OpenNextFirmwareFile();  // Attempt to open the first file.
}

bool FirmwareConfigLoaderImpl::OpenNextFirmwareFile() {
  if (firmware_file_fd_ != -1) {
    SystemCallWrapper::GetWrapper().Close(firmware_file_fd_);
    firmware_file_fd_ = -1;
  }
  previous_header_.reset();  // Reset for accumulation mode for the new file.

  current_firmware_file_index_++;
  if (static_cast<size_t>(current_firmware_file_index_) >=
      current_firmware_filenames_.size()) {
    LOG(INFO) << __func__ << ": All firmware files processed.";
    return false;  // No more files
  }

  const std::string& current_file_name =
      current_firmware_filenames_[current_firmware_file_index_];
  const std::string firmware_path =
      active_config_->get().firmware_folder_name() + current_file_name;

  LOG(INFO) << __func__
            << ": Attempting to open firmware file: " << firmware_path;
  firmware_file_fd_ =
      SystemCallWrapper::GetWrapper().Open(firmware_path.c_str(), O_RDONLY);
  if (firmware_file_fd_ < 0) {
    LOG(ERROR) << __func__ << ": Cannot open firmware file: " << firmware_path
               << ". Error: " << strerror(errno);
    return false;
  }
  LOG(INFO) << __func__
            << ": Successfully opened firmware file: " << firmware_path;
  return true;
}

std::optional<DataPacket> FirmwareConfigLoaderImpl::GetNextFirmwareData() {
  std::scoped_lock lock(firmware_data_mutex_);
  // If no file is currently open (or previous one ended), try opening the
  // next one.
  if (firmware_file_fd_ == -1 && !OpenNextFirmwareFile()) {
    return std::nullopt;
  }

  if (!active_config_ ||
      !active_config_->get().has_firmware_data_loading_type()) {
    LOG(WARNING)
        << __func__
        << ": Data loading type not set, defaulting to PACKET_BY_PACKET.";
    return GetNextFirmwareDataByPacket();
  }

  DataLoadingType data_loading_type = static_cast<DataLoadingType>(
      active_config_->get().firmware_data_loading_type());

  switch (data_loading_type) {
    case DataLoadingType::kByAccumulation:
      return GetNextFirmwareDataByAccumulation();
    case DataLoadingType::kByPacket:
    default:
      return GetNextFirmwareDataByPacket();
  }
}

std::optional<std::reference_wrapper<const SetupCommandPacket>>
FirmwareConfigLoaderImpl::GetSetupCommandPacket(
    SetupCommandType command_type) const {
  if (!active_config_) {
    LOG(ERROR) << __func__ << ": No active firmware configuration selected.";
    return std::nullopt;
  }

  const auto iter = active_setup_commands_.find(command_type);
  if (iter == active_setup_commands_.end()) {
    return std::nullopt;
  }

  return std::cref(*iter->second);
}

int FirmwareConfigLoaderImpl::GetLoadMiniDrvDelayMs() const {
  if (!active_config_) {
    LOG(ERROR) << __func__ << ": No active firmware configuration selected.";
    return cfg_consts::kDefaultLoadMiniDrvDelayMs;
  }
  const auto& config = active_config_->get();
  return config.has_load_mini_drv_delay_ms()
             ? config.load_mini_drv_delay_ms()
             : cfg_consts::kDefaultLoadMiniDrvDelayMs;
}

int FirmwareConfigLoaderImpl::GetLaunchRamDelayMs() const {
  if (!active_config_) {
    LOG(ERROR) << __func__ << ": No active firmware configuration selected.";
    return cfg_consts::kDefaultLaunchRamDelayMs;
  }
  const auto& config = active_config_->get();
  return config.has_launch_ram_delay_ms()
             ? config.launch_ram_delay_ms()
             : cfg_consts::kDefaultLaunchRamDelayMs;
}

std::string FirmwareConfigLoaderImpl::DumpConfigToString() const {
  std::stringstream ss;
  ss << "--- FirmwareConfigLoaderImpl State ---\n";
  ss << "Loaded Transport Specific Configurations: "
     << transport_specific_configs_.size() << "\n";

  for (const auto& [transport_type, config] : transport_specific_configs_) {
    ss << "  Transport Type: " << static_cast<int>(transport_type) << "\n";
    ss << "    Firmware Folder: \"" << config.firmware_folder_name() << "\"\n";
    ss << "    Firmware Files:\n";
    if (config.firmware_file_name_size() > 0) {
      for (const std::string& fname : config.firmware_file_name()) {
        ss << "      - \"" << fname << "\"\n";
      }
    } else {
      ss << "      (None)\n";
    }
    ss << "    Chip ID: " << config.chip_id() << "\n";
    ss << "    Load MiniDrv Delay (ms): " << config.load_mini_drv_delay_ms()
       << "\n";
    ss << "    Launch RAM Delay (ms): " << config.launch_ram_delay_ms() << "\n";
    ss << "    Data Loading Type: "
       << FirmwareDataLoadingType_Name(config.firmware_data_loading_type())
       << "\n";
    ss << "    Setup Commands Loaded:\n";
    if (active_setup_commands_.empty()) {
      ss << "      (None)\n";
    } else {
      for (const auto& [command_type, packet_ptr] : active_setup_commands_) {
        ss << "      - " << SetupCommandTypeToString(command_type) << ": "
           << (packet_ptr ? "Present" : "Absent") << "\n";
      }
    }
  }

  if (active_config_) {
    ss << "Active Configuration for Transport Type: "
       << active_config_->get().transport_type() << "\n";
  } else {
    ss << "No Active Firmware Configuration Selected.\n";
  }
  ss << "-------------------------------------\n";

  return ss.str();
}

FirmwareConfigLoaderImpl::FirmwareConfigLoaderImpl() {
#ifndef UNIT_TEST
  LoadConfig();
#endif
}

bool FirmwareConfigLoaderImpl::LoadConfig() {
  return LoadConfigFromFile(cfg_consts::kFirmwareConfigFile);
}

bool FirmwareConfigLoaderImpl::LoadConfigFromFile(std::string_view path) {
  std::ifstream json_file(path.data());
  if (!json_file.is_open()) {
    LOG(ERROR) << __func__ << ": Failed to open json file " << path.data();
    return false;
  }

  std::string json_str((std::istreambuf_iterator<char>(json_file)),
                       std::istreambuf_iterator<char>());

  return LoadConfigFromString(json_str);
}

bool FirmwareConfigLoaderImpl::LoadConfigFromString(std::string_view content) {
  FirmwareConfigsContainer container;
  JsonParseOptions options;
  options.ignore_unknown_fields = true;

  Status status = JsonStringToMessage(content, &container, options);
  if (!status.ok()) {
    LOG(ERROR) << __func__
               << ": Failed to parse json file, error: " << status.message();
    return false;
  }

  transport_specific_configs_.clear();
  for (const auto& config_entry : container.firmware_configs()) {
    auto type = static_cast<TransportType>(config_entry.transport_type());
    transport_specific_configs_[type] = config_entry;
  }

  const auto& transport_type_priorities =
      HalConfigLoader::GetLoader().GetTransportTypePriority();
  for (auto& type : transport_type_priorities) {
    if (transport_specific_configs_.find(type) !=
        transport_specific_configs_.end()) {
      SelectFirmwareConfiguration(type);
      break;
    }
  }

  LOG(INFO) << DumpConfigToString();

  return true;
}

bool FirmwareConfigLoaderImpl::SelectFirmwareConfiguration(
    TransportType transport_type) {
  auto it = transport_specific_configs_.find(transport_type);
  if (it == transport_specific_configs_.end()) {
    LOG(ERROR) << __func__
               << ": No firmware configuration found for transport type "
               << static_cast<int>(transport_type);
    active_config_ = std::nullopt;
    active_setup_commands_.clear();
    return false;
  }

  active_config_ = std::cref(it->second);
  LOG(INFO) << __func__
            << ": Selected firmware configuration for transport type "
            << static_cast<int>(transport_type);

  current_firmware_filenames_.clear();
  const auto& config = active_config_->get();
  for (const std::string& fname : config.firmware_file_name()) {
    current_firmware_filenames_.push_back(fname);
  }
  current_firmware_file_index_ = -1;

  active_setup_commands_.clear();
  if (active_config_->get().has_setup_commands()) {
    LoadSetupCommandsFromConfig(active_config_->get(), active_setup_commands_);
  }

  return true;
}

void FirmwareConfigLoaderImpl::LoadSetupCommandsFromConfig(
    const FirmwareConfigForTransport& config,
    std::unordered_map<SetupCommandType, std::unique_ptr<SetupCommandPacket>>&
        target_map) {
  const auto& commands = config.setup_commands();

  auto to_vector = [](const RepeatedField<uint32_t>& field) {
    return std::vector<uint8_t>(field.begin(), field.end());
  };

  auto add = [&](SetupCommandType type, const RepeatedField<uint32_t>& data) {
    target_map.emplace(
        type, std::make_unique<SetupCommandPacket>(type, to_vector(data)));
  };

  if (commands.hci_reset_size()) {
    add(SetupCommandType::kReset, commands.hci_reset());
  }
  if (commands.hci_read_chip_id_size()) {
    add(SetupCommandType::kReadChipId, commands.hci_read_chip_id());
  }
  if (commands.hci_update_chip_baud_rate_size()) {
    add(SetupCommandType::kUpdateChipBaudRate,
        commands.hci_update_chip_baud_rate());
  }
  if (commands.hci_set_fast_download_size()) {
    add(SetupCommandType::kSetFastDownload, commands.hci_set_fast_download());
  }
  if (commands.hci_download_minidrv_size()) {
    add(SetupCommandType::kDownloadMinidrv, commands.hci_download_minidrv());
  }
  if (commands.hci_vsc_launch_ram_size()) {
    add(SetupCommandType::kLaunchRam, commands.hci_vsc_launch_ram());
  }
  if (commands.hci_read_fw_version_size()) {
    add(SetupCommandType::kReadFwVersion, commands.hci_read_fw_version());
  }
  if (commands.hci_setup_low_power_mode_size()) {
    add(SetupCommandType::kSetupLowPowerMode,
        commands.hci_setup_low_power_mode());
  }
  if (commands.hci_write_bd_address_size()) {
    add(SetupCommandType::kWriteBdAddress, commands.hci_write_bd_address());
  }
}

std::optional<DataPacket>
FirmwareConfigLoaderImpl::GetNextFirmwareDataByPacket() {
  while (true) {
    if (firmware_file_fd_ == -1 && !OpenNextFirmwareFile()) {
      return std::nullopt;  // No more files or error opening.
    }

    // Read packet header (opcode and length).
    std::vector<uint8_t> header(3);
    ssize_t bytes_read =
        TEMP_FAILURE_RETRY(SystemCallWrapper::GetWrapper().Read(
            firmware_file_fd_, header.data(), header.size()));

    if (bytes_read <= 0) {
      // End of current file or error.
      LOG(ERROR) << __func__ << ": Failed to read full header for packet in "
                 << current_firmware_filenames_[current_firmware_file_index_];
      SystemCallWrapper::GetWrapper().Close(firmware_file_fd_);
      firmware_file_fd_ = -1;
      // Attempt to open the next file.
      continue;
    }

    // Read remaining packet data.
    const size_t payload_size = header[2];
    std::vector<uint8_t> packet_payload(1 + header.size() + payload_size);
    packet_payload[0] = static_cast<uint8_t>(HciPacketType::kCommand);
    std::copy(header.begin(), header.end(), packet_payload.begin() + 1);

    ssize_t payload_bytes_read =
        TEMP_FAILURE_RETRY(SystemCallWrapper::GetWrapper().Read(
            firmware_file_fd_, packet_payload.data() + 1 + header.size(),
            payload_size));

    if (payload_bytes_read != static_cast<ssize_t>(payload_size)) {
      // Incomplete packet or error.
      LOG(ERROR) << __func__ << ": Failed to read full payload for packet in "
                 << current_firmware_filenames_[current_firmware_file_index_];
      SystemCallWrapper::GetWrapper().Close(firmware_file_fd_);
      firmware_file_fd_ = -1;
      // Attempt to open the next file.
      continue;
    }

    bool is_launch_ram =
        (GetOpcode(std::span(header)) == kHciVscLaunchRamOpcode);
    bool is_last_file = (static_cast<size_t>(current_firmware_file_index_) ==
                         current_firmware_filenames_.size() - 1);

    DataType packet_data_type = DataType::kDataFragment;
    if (is_launch_ram) {
      LOG(INFO) << __func__ << ": Launch RAM command found in file "
                << current_firmware_filenames_[current_firmware_file_index_];
      // This launch_ram packet is the end of the current file.
      // Close fd now, so next call to GetNextFirmwareData will try
      // OpenNextFirmwareFile.
      SystemCallWrapper::GetWrapper().Close(firmware_file_fd_);
      firmware_file_fd_ = -1;
      if (is_last_file) {
        LOG(INFO) << __func__ << " This is the last firmware file.";
        packet_data_type = DataType::kDataEnd;
      }
    }
    return DataPacket(packet_data_type, packet_payload);
  }
}

std::optional<DataPacket>
FirmwareConfigLoaderImpl::GetNextFirmwareDataByAccumulation() {
  std::vector<uint8_t> buffer;
  constexpr int kBufferSize = 32 * 1024;
  buffer.reserve(kBufferSize);
  bool is_final_packet_of_all_files = false;

  while (true) {
    std::vector<uint8_t> header;

    if (previous_header_.has_value()) {
      header = std::move(previous_header_.value());
      previous_header_.reset();
    } else {
      if (firmware_file_fd_ == -1 && !OpenNextFirmwareFile()) {
        // No more files or error opening. If buffer has data, return it.
        break;
      }
    }
    // Read packet header (opcode and length).
    header.resize(3);
    ssize_t bytes_read =
        TEMP_FAILURE_RETRY(SystemCallWrapper::GetWrapper().Read(
            firmware_file_fd_, header.data(), header.size()));
    if (bytes_read <= 0) {
      SystemCallWrapper::GetWrapper().Close(firmware_file_fd_);
      firmware_file_fd_ = -1;
      if (!buffer.empty()) {
        // Return current buffer if it has content.
        break;
      }
      continue;  // Try opening next file.
    }
  }

  // Calculate total packet size.
  const size_t payload_size = header[2];
  const size_t packet_size = 1 + header.size() + payload_size;

  bool is_launch_ram = (GetOpcode(header) == kHciVscLaunchRamOpcode);
  bool is_last_file = (static_cast<size_t>(current_firmware_file_index_) ==
                       current_firmware_filenames_.size() - 1);

  // Check if the current packet fits in the buffer.
  if ((is_launch_ram && !buffer.empty()) ||
      (!is_launch_ram && buffer.size() + packet_size > kBufferSize)) {
    previous_header_ = std::move(header);
    return DataPacket(DataType::kDataFragment, std::move(buffer));
  }

  // At this point, the packet (even if it's launch_ram) will be added to
  // the buffer.

  // Read remaining packet data and append to buffer.
  buffer.push_back(static_cast<uint8_t>(HciPacketType::kCommand));
  buffer.insert(buffer.end(), header.begin(), header.end());

  std::vector<uint8_t> payload(payload_size);
  ssize_t bytes_read = TEMP_FAILURE_RETRY(SystemCallWrapper::GetWrapper().Read(
      firmware_file_fd_, payload.data(), payload_size));
  if (bytes_read != static_cast<ssize_t>(payload_size)) {
    // Incomplete packet or error.
    LOG(ERROR) << __func__ << ": Failed to read full payload for packet in "
               << current_firmware_filenames_[current_firmware_file_index_];
    firmware_file_fd_ = -1;
    buffer.clear();
    continue;  // Try next file or fail.
  }
  buffer.insert(buffer.end(), payload.begin(), payload.end());

  // Check for target opcode after reading the whole packet.
  if (is_launch_ram) {
    LOG(INFO) << __func__ << " Launch RAM command found in file "
              << current_firmware_filenames_[current_firmware_file_index_];
    SystemCallWrapper::GetWrapper().Close(firmware_file_fd_);
    firmware_file_fd_ = -1;
    if (is_last_file) {
      is_final_packet_of_all_files = true;
    }
    break;
  }
}

if (!buffer.empty()) {
  return DataPacket(is_final_packet_of_all_files ? DataType::kDataEnd
                                                 : DataType::kDataFragment,
                    std::move(buffer));
}
return std::nullopt;
}

std::mutex FirmwareConfigLoader::loader_mutex_;
FirmwareConfigLoader* FirmwareConfigLoader::loader_ = nullptr;

FirmwareConfigLoader& FirmwareConfigLoader::GetLoader() {
  std::lock_guard<std::mutex> lock(loader_mutex_);
  if (loader_ == nullptr) {
    loader_ = new FirmwareConfigLoaderImpl();
  }
  return *loader_;
}

void FirmwareConfigLoader::ResetLoader() {
  std::lock_guard<std::mutex> lock(loader_mutex_);
  if (loader_ != nullptr) {
    delete loader_;
    loader_ = nullptr;
  }
}

}  // namespace config
}  // namespace bluetooth_hal
