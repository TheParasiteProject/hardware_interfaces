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

#define LOG_TAG "bthal.chip_provisioner"

#include "bluetooth_hal/chip/chip_provisioner.h"

#include <chrono>
#include <fstream>
#include <future>
#include <sstream>
#include <thread>

#include "android-base/logging.h"
#include "android-base/properties.h"
#include "android-base/stringprintf.h"
#include "bluetooth_hal/config/firmware_config_loader.h"
#include "bluetooth_hal/config/hal_config_loader.h"
#include "bluetooth_hal/hal_packet.h"
#include "bluetooth_hal/hal_types.h"
#include "bluetooth_hal/hci_router.h"

namespace bluetooth_hal {
namespace chip {
namespace {

using ::android::base::GetProperty;
using ::android::base::StringPrintf;
using ::bluetooth_hal::HalState;
using ::bluetooth_hal::config::DataPacket;
using ::bluetooth_hal::config::DataType;
using ::bluetooth_hal::config::HalConfigLoader;
using ::bluetooth_hal::config::SetupCommandType;
using ::bluetooth_hal::config::SetupCommandTypeToString;
using ::bluetooth_hal::hci::EventResultCode;
using ::bluetooth_hal::hci::HalPacket;
using ::bluetooth_hal::hci::HciPacketType;
using ::bluetooth_hal::hci::HciRouter;

constexpr char kDevinfoNodePath[] = "/proc/device-tree/chosen/config/bt_addr";
constexpr char kRandGenBdaddrPath[] =
    "/mnt/vendor/persist/bluetooth/bdaddr.txt";
constexpr char kEvbDefaultBdaddrProp[] = "ro.vendor.bluetooth.evb_bdaddr";
constexpr uint16_t kHciVscWriteBdAddress = 0xfc01;
constexpr uint16_t kHciVscWriteBdAddressLength = 0x0A;

}  // namespace

void ChipProvisioner::Initialize(
    const std::function<void(HalState)> on_hal_state_update) {
  on_hal_state_update_ = std::move(on_hal_state_update);
}

bool ChipProvisioner::DownloadFirmware() {
  LOG(INFO) << __func__;

  state_ = ProvisioningState::kInitialReset;
  RunProvisioningSequence();

  if (state_ != ProvisioningState::kDone) {
    // TODO: b/372148907 - Need to report error (kill self if needed).
    LOG(FATAL) << __func__
               << ": Failed to complete download firmware. Final state: "
               << static_cast<int>(state_);
    return false;
  }
  LOG(INFO) << __func__ << ": Firmware download completed successfully.";
  return true;
}

bool ChipProvisioner::ResetFirmware() {
  LOG(INFO) << __func__;
  if (!ExecuteCurrentSetupStep(SetupCommandType::kReset)) {
    LOG(ERROR) << __func__ << ": Failed to reset firmware.";
    // TODO: b/372148907 - Need to report error (kill self if needed).
    return false;
  }

  switch (HciRouter::GetRouter().GetHalState()) {
    case HalState::kBtChipReady:
      UpdateHalState(HalState::kRunning);
      break;
    case HalState::kRunning:
      UpdateHalState(HalState::kBtChipReady);
      break;
    default:
      // TODO: b/372148907 - Need to report error (kill self if needed).
      return false;
  }

  return true;
}

bool ChipProvisioner::ExecuteCurrentSetupStep(SetupCommandType command_type) {
  auto next_setup_command = config_loader_.GetSetupCommandPacket(command_type);
  if (!next_setup_command.has_value()) {
    LOG(INFO) << __func__ << ": No command for type "
              << SetupCommandTypeToString(command_type);
    return true;
  }

  return SendCommandAndWait(next_setup_command->get().GetPayload());
}

bool ChipProvisioner::SendCommandAndWait(const HalPacket& packet) {
  if (!SendCommand(packet)) {
    LOG(ERROR) << __func__ << ": Failed to send next setup command.";
    return false;
  }

  std::future_status status =
      command_promise_.get_future().wait_for(std::chrono::milliseconds(1000));
  if (status != std::future_status::ready) {
    LOG(ERROR) << __func__ << ": Command timeout during download firmware.";
    return false;
  }
  command_promise_ = std::promise<void>();
  return firmware_command_success_;
}

void ChipProvisioner::OnCommandCallback(const HalPacket& callback_event) {
  bool success = (callback_event.GetCommandCompleteEventResult() ==
                  static_cast<uint8_t>(EventResultCode::kSuccess));
  LOG(success ? INFO : WARNING)
      << __func__ << ": Recv VSE <" << callback_event.ToString() << "> "
      << (success ? "[Success]" : "[Failed]");
  firmware_command_success_ = success;
  command_promise_.set_value();
}

bool ChipProvisioner::ProvisionBluetoothAddress() {
  LOG(INFO) << __func__;
  std::string bdaddr_str;
  std::fstream devinfo(kDevinfoNodePath, std::ios::in);
  std::fstream randgen(kRandGenBdaddrPath, std::ios::in);
  if (devinfo.is_open()) {
    std::getline(devinfo, bdaddr_str);
  } else if (randgen.is_open()) {
    std::getline(randgen, bdaddr_str);
  } else {
    bdaddr_str = GetProperty(kEvbDefaultBdaddrProp, "");
  }

  if (bdaddr_str.empty()) {
    LOG(ERROR) << __func__ << "Can't fetch the provisioning BDA (empty string)";
    return false;
  }

  unsigned char trailing_char = '\0';
  auto try_parse = [&](const char* fmt) {
    return std::sscanf(bdaddr_str.data(), fmt, &bdaddr_[5], &bdaddr_[4],
                       &bdaddr_[3], &bdaddr_[2], &bdaddr_[1], &bdaddr_[0],
                       &trailing_char) == kBluetoothAddressLength;
  };

  bool success = try_parse("%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx%c") ||
                 try_parse("%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%c");

  if (!success) {
    LOG(ERROR) << __func__
               << "Can't fetch the provisioning BDA (invalid format)";
    return false;
  }

  LOG(INFO) << __func__ << ": BDADDR <"
            << StringPrintf("xx:xx:xx:xx:%02x:%02x", bdaddr_[1], bdaddr_[0])
            << ">";

  std::optional<HalPacket> write_bda_packet = PrepareWriteBdAddressPacket();
  if (write_bda_packet.has_value()) {
    if (!SendCommandAndWait(write_bda_packet.value())) {
      LOG(ERROR) << __func__
                 << "Failed to send write Bluetooth address command.";
      return false;
    }
    return true;
  } else {
    LOG(ERROR) << __func__
               << "Failed to prepare write Bluetooth address packet.";
    return false;
  }
}

std::optional<HalPacket> ChipProvisioner::PrepareWriteBdAddressPacket() {
  if (bdaddr_.size() != kBluetoothAddressLength) {
    LOG(ERROR) << __func__ << ": Invalid Bluetooth address length";
    return std::nullopt;
  }

  HalPacket write_bda_vsc;
  write_bda_vsc.resize(kHciVscWriteBdAddressLength);

  // Prepare the HalPacket elements for the WriteBdAddress command.
  write_bda_vsc[0] = static_cast<uint8_t>(HciPacketType::kCommand);
  write_bda_vsc[1] = kHciVscWriteBdAddress & 0xff;
  write_bda_vsc[2] = (kHciVscWriteBdAddress >> 8u) & 0xff;
  write_bda_vsc[3] = kBluetoothAddressLength;

  memcpy(write_bda_vsc.data() + 4, bdaddr_.data(), kBluetoothAddressLength);

  std::stringstream ss;
  for (uint8_t byte : write_bda_vsc) {
    ss << StringPrintf("%02x", byte);
  }
  LOG(INFO) << __func__ << ": Prepared VSC <" << ss.str() << ">";

  return write_bda_vsc;
}

void ChipProvisioner::UpdateHalState(HalState status) {
  // Check if a callback is present.
  if (on_hal_state_update_.has_value()) {
    on_hal_state_update_.value()(status);
  } else {
    LOG(WARNING) << "No download callback registered.";
  }
}

bool ChipProvisioner::SendCommandNoAck(const HalPacket& packet) {
  if (packet.GetType() != HciPacketType::kCommand) {
    LOG(WARNING) << __func__ << ": Invalid Packet Type.";
    return false;
  }
  return HciRouter::GetRouter().SendCommandNoAck(packet);
}

void ChipProvisioner::RunProvisioningSequence() {
  bool running = true;
  while (running) {
    LOG(INFO) << "Executing provisioning state: " << static_cast<int>(state_);
    switch (state_) {
      case ProvisioningState::kInitialReset:
        if (ExecuteCurrentSetupStep(SetupCommandType::kReset)) {
          state_ = ProvisioningState::kReadChipId;
        } else {
          state_ = ProvisioningState::kError;
        }
        break;

      case ProvisioningState::kReadChipId:
        if (ExecuteCurrentSetupStep(SetupCommandType::kReadChipId)) {
          state_ = ProvisioningState::kSetRuntimeBaudRate;
        } else {
          state_ = ProvisioningState::kError;
        }
        break;

      case ProvisioningState::kSetRuntimeBaudRate:
        if (ExecuteCurrentSetupStep(SetupCommandType::kUpdateChipBaudRate)) {
          state_ = ProvisioningState::kCheckFirmwareStatus;
        } else {
          state_ = ProvisioningState::kError;
        }
        break;

      case ProvisioningState::kCheckFirmwareStatus:
        if (HciRouter::GetRouter().GetHalState() ==
            HalState::kFirmwareDownloadCompleted) {
          LOG(INFO) << __func__ << ": [ FirmwareReady ]";
          UpdateHalState(HalState::kFirmwareReady);
          state_ = ProvisioningState::kReadFwVersion;
        } else {
          LOG(INFO) << __func__ << ": [ FirmwareDownloading ]";
          UpdateHalState(HalState::kFirmwareDownloading);
          state_ = ProvisioningState::kSetFastDownload;
        }
        break;

      case ProvisioningState::kSetFastDownload:
        if (ExecuteCurrentSetupStep(SetupCommandType::kSetFastDownload)) {
          state_ = ProvisioningState::kDownloadMinidrv;
        } else {
          state_ = ProvisioningState::kError;
        }
        break;

      case ProvisioningState::kDownloadMinidrv:
        if (ExecuteCurrentSetupStep(SetupCommandType::kDownloadMinidrv)) {
          state_ = ProvisioningState::kWriteFirmware;
        } else {
          state_ = ProvisioningState::kError;
        }
        break;

      case ProvisioningState::kWriteFirmware: {
        // Add delay time for placing firmware in download mode.
        int mini_drv_delay_ms = config_loader_.GetLoadMiniDrvDelayMs();
        std::this_thread::sleep_for(
            std::chrono::milliseconds(mini_drv_delay_ms));
        LOG(INFO) << __func__ << ":Writing firmware patchram";
        // Write firmware patchram packets.
        if (WriteFwPatchramPacket()) {
          LOG(INFO) << "[ FirmwareDownloadCompleted ]";
          UpdateHalState(HalState::kFirmwareDownloadCompleted);
          state_ = ProvisioningState::kFinalReset;
        } else {
          LOG(ERROR) << "Failed to write Firmware PatchRam Packets.";
          state_ = ProvisioningState::kError;
        }
        break;
      }

      case ProvisioningState::kFinalReset:
        if (ExecuteCurrentSetupStep(SetupCommandType::kReset)) {
          // Re-enter the flow to check status and proceed to the final steps.
          state_ = ProvisioningState::kSetRuntimeBaudRate;
        } else {
          state_ = ProvisioningState::kError;
        }
        break;

      case ProvisioningState::kReadFwVersion:
        if (ExecuteCurrentSetupStep(SetupCommandType::kReadFwVersion)) {
          LOG(INFO) << "ReadFwVersion successful.";
          state_ = ProvisioningState::kWriteBdAddress;
        } else {
          state_ = ProvisioningState::kError;
        }
        break;

      case ProvisioningState::kWriteBdAddress:
        LOG(INFO) << "writing BDA to controller";
        if (!ProvisionBluetoothAddress()) {
          LOG(ERROR) << __func__
                     << ": Failed to provision and write Bluetooth address.";
          // TODO: b/409658769 - Force to abort hal service and report issue.
        }
        state_ = ProvisioningState::kSetupLowPowerMode;
        break;

      case ProvisioningState::kSetupLowPowerMode:
        if (HalConfigLoader::GetLoader().IsLowPowerModeSupported()) {
          if (!ExecuteCurrentSetupStep(SetupCommandType::kSetupLowPowerMode)) {
            LOG(ERROR) << __func__
                       << ": Failed to send low power mode command.";
            state_ = ProvisioningState::kError;
            break;
          }
        } else {
          LOG(WARNING) << __func__ << ": Low power mode is disabled!";
        }
        state_ = ProvisioningState::kDone;
        break;

      case ProvisioningState::kDone:
        LOG(INFO) << __func__ << ": [ BtChipReady ]";
        UpdateHalState(HalState::kBtChipReady);
        [[fallthrough]];
      case ProvisioningState::kError:
      case ProvisioningState::kIdle:
      default:
        running = false;
        break;
    }
  }
}

bool ChipProvisioner::WriteFwPatchramPacket() {
  if (!config_loader_.ResetFirmwareDataLoadingState()) {
    LOG(ERROR) << __func__ << ": Failed to load firmware data";
    return false;
  }

  std::optional<DataPacket> data_packet;
  while ((data_packet = config_loader_.GetNextFirmwareData()).has_value()) {
    if (data_packet->GetDataType() == config::DataType::kDataFragment) {
      if (!SendCommandNoAck(data_packet->GetPayload())) {
        LOG(ERROR) << __func__ << ": Failed to send firmware data fragment.";
        return false;
      }
    } else {
      if (!SendCommandAndWait(data_packet->GetPayload())) {
        LOG(ERROR) << __func__
                   << ": Failed to send final firmware data packet.";
        return false;
      }
    }
  }

  int launch_ram_delay_ms = config_loader_.GetLaunchRamDelayMs();
  std::this_thread::sleep_for(std::chrono::milliseconds(launch_ram_delay_ms));

  return true;
}

}  // namespace chip
}  // namespace bluetooth_hal
