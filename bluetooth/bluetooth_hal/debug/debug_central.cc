/*
 * Copyright 2020 The Android Open Source Project
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

#define LOG_TAG "bluetooth_hal.debug_central"

#include "bluetooth_hal/debug/debug_central.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>

#include "android-base/logging.h"
#include "android-base/properties.h"
#include "android-base/stringprintf.h"
#include "bluetooth_hal/bqr/bqr_root_inflammation_event.h"
#include "bluetooth_hal/bqr/bqr_types.h"
#include "bluetooth_hal/config/hal_config_loader.h"
#include "bluetooth_hal/debug/bluetooth_activity.h"
#include "bluetooth_hal/extensions/thread/thread_handler.h"
#include "bluetooth_hal/hal_packet.h"
#include "bluetooth_hal/hci_router.h"
#include "bluetooth_hal/transport/transport_interface.h"
#include "bluetooth_hal/util/logging.h"
#include "bluetooth_hal/util/power/wakelock_watchdog.h"

namespace bluetooth_hal {
namespace debug {
namespace {

using ::android::base::StringPrintf;
using ::bluetooth_hal::bqr::BqrErrorCode;
using ::bluetooth_hal::bqr::BqrErrorToStringView;
using ::bluetooth_hal::bqr::BqrRootInflammationEvent;
using ::bluetooth_hal::config::HalConfigLoader;
using ::bluetooth_hal::hci::HalPacket;
using ::bluetooth_hal::hci::HciRouter;
using ::bluetooth_hal::thread::ThreadHandler;
using ::bluetooth_hal::transport::TransportInterface;
using ::bluetooth_hal::transport::TransportType;
using ::bluetooth_hal::util::Logger;
using ::bluetooth_hal::util::power::WakelockWatchdog;

constexpr int kVseSubEventCodeOffset = 2;
constexpr int kBqrReportIdOffset = 3;
constexpr int kBqrInflamedErrorCode = 4;
constexpr int kBqrInflamedVendorErrCode = 5;
constexpr int kDebugInfoPayloadOffset = 8;
constexpr int kChreDebugDumpLastBlockOffsetFisrtByte = 4;
constexpr int kChreDebugDumpLastBlockOffsetSecondByte = 5;
constexpr int kDebugInfoLastBlockOffset = 5;
constexpr int kHwCodeOffset = 2;

constexpr int kHandleDebugInfoCommandMs = 1000;
constexpr int kMaxCoredumpFiles = 3;
const std::string kCoredumpFilePath = "/data/vendor/ssrdump/coredump/";
const std::string kCoredumpPrefix = "coredump_bt_";
const std::string kCoredumpFilePrefix = kCoredumpFilePath + kCoredumpPrefix;
const std::string kSocdumpFilePrefix =
    kCoredumpFilePath + "coredump_bt_socdump_";
const std::regex kTimestampPattern(R"(\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2})");

const std::string kDebugNodeBtLpm = "dev/logbuffer_btlpm";
constexpr char kDebugNodeBtUartPrefix[] = "/dev/logbuffer_tty";
constexpr char kHwStage[] = "ro.boot.hardware.revision";
constexpr uint8_t kReservedCoredumpFileCount = 2;

void DumpDebugfs(int fd, const std::string& debugfs) {
  std::stringstream ss;
  std::ifstream file;

  ss << "=============================================" << std::endl;
  ss << "Debugfs:" << debugfs << std::endl;
  ss << "=============================================" << std::endl;
  file.open(debugfs);
  if (file.is_open()) {
    ss << file.rdbuf() << std::endl;
  } else {
    ss << "Fail to read debugfs: " << debugfs << std::endl;
  }
  ss << std::endl;
  write(fd, ss.str().c_str(), ss.str().length());
}

bool IsBinFilePatternMatch(const std::string& filename,
                           const std::string& base_prefix) {
  if (!filename.starts_with(base_prefix)) {
    return false;
  }
  std::string remaining_part = filename.substr(base_prefix.length());

  if (!remaining_part.ends_with(".bin")) {
    return false;
  }

  std::string timestamp_str = remaining_part.substr(
      0, remaining_part.length() - std::string(".bin").length());
  return std::regex_match(timestamp_str, kTimestampPattern);
}

void DeleteOldestBinFiles(const std::string& directory,
                          const std::string& base_file_prefix,
                          size_t files_to_keep) {
  std::vector<std::filesystem::directory_entry> filtered_files;

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string filename = entry.path().filename().string();

    if (IsBinFilePatternMatch(filename, base_file_prefix)) {
      filtered_files.emplace_back(entry);
    }
  }

  // Sort files by their last write time
  std::sort(filtered_files.begin(), filtered_files.end(),
            [](const auto& a, const auto& b) {
              return std::filesystem::last_write_time(a) >
                     std::filesystem::last_write_time(b);
            });

  // Delete files, starting at files_to_keep
  for (size_t i = files_to_keep; i < filtered_files.size(); ++i) {
    std::filesystem::remove(filtered_files[i]);
    LOG(INFO) << "Deleted: " << filtered_files[i].path().c_str();
  }
}

void FlushCoredumpToFd(int fd) {
  std::unique_ptr<DIR, decltype(&closedir)> dir(
      opendir(kCoredumpFilePath.c_str()), closedir);
  if (!dir) {
    LOG(WARNING) << __func__
                 << ": Failed to open directory: " << kCoredumpFilePath;
    return;
  }

  std::stringstream ss;
  struct dirent* entry;

  while ((entry = readdir(dir.get())) != nullptr) {
    std::string file_name = entry->d_name;

    if (file_name == "." || file_name == "..") {
      continue;
    }

    std::string full_path = kCoredumpFilePath + file_name;

    if (!IsBinFilePatternMatch(file_name, kCoredumpPrefix)) {
      continue;
    }

    struct stat file_stat;
    if (stat(full_path.c_str(), &file_stat) == -1 ||
        !S_ISREG(file_stat.st_mode)) {
      continue;
    }

    LOG(INFO) << __func__ << ": Dumping " << full_path;

    std::ifstream input_file(full_path, std::ios::binary);
    if (!input_file.is_open()) {
      ss << "*********************************************\n";
      ss << "ERROR: Failed to open file: " << full_path << "\n";
      ss << "*********************************************\n\n";
      LOG(ERROR) << __func__ << ": Failed to open file: " << full_path;
      continue;
    }

    ss << "*********************************************\n";
    ss << "BEGIN of LogFile: " << file_name << "\n";
    ss << "*********************************************\n\n";
    ss << input_file.rdbuf();
    ss << "\n*********************************************\n";
    ss << "END of LogFile: " << file_name << "\n";
    ss << "*********************************************\n\n";
    input_file.close();
  }

  std::string final_output = ss.str();
  if (!final_output.empty()) {
    ssize_t bytes_written =
        write(fd, final_output.c_str(), final_output.length());
    if (bytes_written == -1) {
      LOG(ERROR) << __func__ << ": Failed to write to file descriptor " << fd
                 << ". Error: " << strerror(errno);
    } else if (static_cast<size_t>(bytes_written) != final_output.length()) {
      LOG(WARNING) << __func__ << ": Incomplete write to file descriptor " << fd
                   << ". Wrote " << bytes_written << " of "
                   << final_output.length() << " bytes.";
    }
  } else {
    LOG(INFO) << __func__ << ": No coredump files found to dump.";
  }
}

}  // namespace

void LogFatal(BqrErrorCode error, std::string extra_info);

DurationTracker::DurationTracker(AnchorType type, const std::string& log)
    : log_(log), type_(type) {
  std::stringstream ss;
  ss << "[ IN] " << log_;
  DebugCentral::Get().UpdateRecord(type_, ss.str());
}

DurationTracker::~DurationTracker() {
  if (log_.empty()) {
    return;
  }
  std::stringstream ss;
  ss << "[OUT] " << log_;
  DebugCentral::Get().UpdateRecord(type_, ss.str());
}

DebugCentral& DebugCentral::Get() {
  static DebugCentral debug_central;
  return debug_central;
}

bool DebugCentral::RegisterCoredumpCallback(
    const std::shared_ptr<CoredumpCallback> callback) {
  if (!callback) {
    return false;
  }
  std::lock_guard<std::mutex> lock(coredump_mutex_);

  auto it = std::find(coredump_callbacks_.begin(), coredump_callbacks_.end(),
                      callback);
  if (it != coredump_callbacks_.end()) {
    return false;
  }

  coredump_callbacks_.emplace(callback);
  return true;
}

bool DebugCentral::UnregisterCoredumpCallback(
    const std::shared_ptr<CoredumpCallback> callback) {
  if (!callback) {
    return false;
  }
  std::lock_guard<std::mutex> lock(coredump_mutex_);

  auto it = std::find(coredump_callbacks_.begin(), coredump_callbacks_.end(),
                      callback);
  if (it == coredump_callbacks_.end()) {
    return false;
  }

  coredump_callbacks_.erase(it);
  return true;
}

void DebugCentral::Dump(int fd) {
  // Dump BtHal debug log
  DumpBluetoothHalLog(fd);
  if (TransportInterface::GetTransportType() == TransportType::kUartH4) {
    // Dump Kernel driver debugfs log
    DumpDebugfs(fd, serial_debug_port_);
    DumpDebugfs(fd, kDebugNodeBtLpm);
  }
  // Dump all coredump_bt files in coredump folder
  LOG(INFO) << __func__
            << ": Write bt coredump files to `IBluetoothHci_default.txt`.";
  FlushCoredumpToFd(fd);
  // Dump Controller BT Activities Statistics
  BtActivitiesLogger::GetInstacne()->ForceUpdating();
  BtActivitiesLogger::GetInstacne()->DumpBtActivitiesStatistics(fd);
}

void DebugCentral::SetBtUartDebugPort(const std::string& uart_port) {
  if (uart_port.empty()) {
    LOG(ERROR) << __func__ << ": UART port is empty!";
    return;
  }

  std::size_t const found = uart_port.find_first_of("0123456789");
  if (found != std::string::npos) {
    serial_debug_port_ = kDebugNodeBtUartPrefix + uart_port.substr(found);
    LOG(INFO) << __func__ << ": Serial debug port: " << serial_debug_port_
              << ".";
    return;
  }
  LOG(ERROR) << __func__ << ": Cannot found uart port!";
}

void DebugCentral::UpdateRecord(AnchorType type, const std::string& anchor) {
  std::unique_lock<std::recursive_mutex> lock(mutex_);
  std::string anchor_timestamp = Logger::GetLogFormatTimestamp();
  std::pair log_entry =
      std::pair<std::string, std::string>(anchor, anchor_timestamp);
  if (history_record_.size() >= kMaxHistory) {
    history_record_.pop_front();
  }
  history_record_.push_back(log_entry);
  if (type != AnchorType::kNone) {
    lasttime_record_[type] = log_entry;
  }
}

void DebugCentral::ReportBqrError(BqrErrorCode error, std::string extra_info) {
  HalPacket bqr_event({0xff, 0x04, 0x58, 0x05, 0x00, (uint8_t)error});

  HAL_LOG(ERROR) << extra_info;
  LOG(ERROR) << __func__ << ": Root inflamed event with error_code: ("
             << static_cast<uint8_t>(error) << "), error_info: " << extra_info
             << ".";
  // report bqr root inflamed event to Stack
  HciRouter::GetRouter().SendPacketToStack(bqr_event);

  if (OkToGenerateCrashDump(static_cast<uint8_t>(error))) {
    GenerateCoredump(CoredumpErrorCode::kControllerRootInflammed,
                     static_cast<uint8_t>(error));
    LogFatal(error, extra_info);
  } else {
    LOG(ERROR) << __func__ << ": Silent recover!";
    ThreadHandler::Cleanup();
    kill(getpid(), SIGKILL);
  }
}

void DebugCentral::HandleDebugInfoCommand() {
  // It is supported to generate coredump and record the timestamp when bthal
  // received root-inflamed event or any fw dump packet, if the controller
  // did not send any response packets, we force to trigger coredump here
  debug_info_command_timer_.Schedule(
      [this]() {
        LOG(ERROR) << __func__
                   << ": Force a coredump to be generated if it has not been "
                      "generated for 1 second.";
        GenerateCoredump(CoredumpErrorCode::kForceCollectCoredump);
      },
      std::chrono::milliseconds(kHandleDebugInfoCommandMs));
}

void DebugCentral::SetControllerFirmwareInformation(const std::string& info) {
  controller_firmware_info_ = info;
}

void DebugCentral::GenerateVendorDumpFile(const std::string& file_path,
                                          const std::vector<uint8_t>& data,
                                          uint8_t vendor_error_code) {
  if (file_path.empty()) {
    LOG(ERROR) << "File name is empty!";
    return;
  }
  GenerateCoredump(CoredumpErrorCode::kVendor, vendor_error_code);

  int fd = OpenOrCreateCoredumpBin(file_path);
  if (fd < 0) {
    LOG(ERROR) << "Failed to open vendor dump file: " << file_path;
    return;
  }

  ssize_t ret = 0;
  if ((ret = TEMP_FAILURE_RETRY(write(fd, data.data(), data.size()))) < 0) {
    LOG(ERROR) << "Error writing to dest file: " << ret << " ("
               << strerror(errno) << ")";
  }
  close(fd);
}

bool DebugCentral::IsHardwareStageSupported() {
  std::string cur_hw_stage = ::android::base::GetProperty(kHwStage, "default");
  std::vector<std::string> not_supported_hw_stages =
      HalConfigLoader::GetLoader().GetUnsupportedHwStages();
  return std::find_if(not_supported_hw_stages.begin(),
                      not_supported_hw_stages.end(),
                      [&](std::string& not_supported_hw_stage) {
                        return cur_hw_stage == not_supported_hw_stage;
                      }) == not_supported_hw_stages.end();
}

bool DebugCentral::OkToGenerateCrashDump(uint8_t error_code) {
  // 1) generate coredump when bt is on
  // 2) generate coredump when bt is off, thread is enabled, and supports
  // accelerated bt on

  bool is_major_fault = (static_cast<BqrErrorCode>(error_code) ==
                         BqrErrorCode::kFirmwareMiscellaneousMajorFault);

  if (is_major_fault || !IsHardwareStageSupported()) {
    return false;
  }

  bool is_thread_dispatcher_working =
      ThreadHandler::IsHandlerRunning() &&
      ThreadHandler::GetHandler().IsDaemonRunning();

  return is_thread_dispatcher_working || debug_monitor_.IsBluetoothEnabled();
}

void DebugCentral::DumpBluetoothHalLog(int fd) {
  std::stringstream ss;

  ss << "=============================================" << std::endl;
  ss << "Controller Firmware Information" << std::endl;
  ss << "=============================================" << std::endl;
  ss << controller_firmware_info_ << std::endl;

  ss << std::endl;
  ss << "=============================================" << std::endl;
  ss << "Anchors' Last Appear" << std::endl;
  ss << "=============================================" << std::endl;
  for (auto it = lasttime_record_.begin(); it != lasttime_record_.end(); ++it) {
    std::string anchor = it->second.first;
    std::string anchor_timestamp = it->second.second;
    ss << "Timestamp of " << anchor << ": " << anchor_timestamp << std::endl;
  }

  ss << std::endl;
  ss << "=============================================" << std::endl;
  ss << "Anchors' History" << std::endl;
  ss << "=============================================" << std::endl;
  for (auto it = history_record_.begin(); it != history_record_.end(); ++it) {
    std::string anchor = it->first;
    std::string anchor_timestamp = it->second;
    ss << anchor_timestamp << ": " << anchor << std::endl;
  }
  write(fd, ss.str().c_str(), ss.str().length());
}

void DebugCentral::HandleRootInflammationEvent(
    const BqrRootInflammationEvent& event) {
  if (!event.IsValid()) {
    LOG(ERROR) << __func__ << ": Invalid root inflammation event! "
               << event.ToString();
    return;
  }

  uint8_t error_code = event.GetErrorCode();
  uint8_t vendor_error_code = event.GetVendorErrorCode();
  LOG(ERROR) << __func__ << ": Received Root Inflammation event! (0x"
             << std::hex << std::setw(2) << std::setfill('0')
             << static_cast<int>(error_code) << std::setw(2)
             << std::setfill('0') << static_cast<int>(vendor_error_code)
             << ").";
  // For some vendor error codes that we do not generate a crash dump.
  if (OkToGenerateCrashDump(vendor_error_code)) {
    GenerateCoredump(CoredumpErrorCode::kControllerRootInflammed,
                     vendor_error_code);
  }
}

void DebugCentral::HandleDebugInfoEvent(const HalPacket& packet) {
  bool last_soc_dump_packet = false;
  if (packet.size() <= kDebugInfoPayloadOffset) {
    LOG(INFO) << __func__ << ": Invalid length of debug info event!";
    return;
  }

  GenerateCoredump(CoredumpErrorCode::kControllerDebugInfo);

  // the Last soc dump debug info packet has been received
  if (packet[kDebugInfoLastBlockOffset]) {
    LOG(INFO) << __func__ << ": Last soc dump fragment has been received.";
    last_soc_dump_packet = true;
  }

  int socdump_fd;
  if ((socdump_fd = OpenOrCreateCoredumpBin(kSocdumpFilePrefix)) < 0) {
    return;
  }

  size_t ret = 0;
  if ((ret = TEMP_FAILURE_RETRY(
           write(socdump_fd, packet.data(), packet.size()))) < 0) {
    LOG(ERROR) << __func__ << ": Error writing to dest file: " << ret << " ("
               << strerror(errno) << ").";
  }

  close(socdump_fd);
  if (last_soc_dump_packet) {
    LOG(ERROR) << __func__ << ": Restart bthal service for recovery!";
    last_soc_dump_packet = false;
    ThreadHandler::Cleanup();
    kill(getpid(), SIGKILL);
  }
}

void DebugCentral::GenerateCoredump(CoredumpErrorCode error_code,
                                    uint8_t sub_error_code) {
  std::lock_guard<std::mutex> lock(coredump_mutex_);
  if (is_coredump_generated_) {
    // coredump has already been generated, avoid duplicated dump in one crash
    // cycle
    return;
  }

  // Pause the watchdog to prevent it from biting before coredump is completed.
  // The HAL will be restarted when the router state exits from Running state.
  WakelockWatchdog::GetWatchdog().Pause();
  is_coredump_generated_ = true;

  HAL_LOG(ERROR) << __func__ << ": Reason: "
                 << CoredumpErrorCodeToString(error_code, sub_error_code);
  int coredump_fd = OpenOrCreateCoredumpBin(kCoredumpFilePrefix);

  if (coredump_fd < 0) {
    LOG(ERROR) << __func__ << ": Failed to open coredump file!";
    return;
  }

  std::stringstream ss;
  ss << "DUMP REASON: " << CoredumpErrorCodeToString(error_code, sub_error_code)
     << " - occurred at " << GetCoredumpTimestampString() << std::endl;
  write(coredump_fd, ss.str().c_str(), ss.str().length());

  DumpBluetoothHalLog(coredump_fd);
  close(coredump_fd);

  // Inform vendor implementations that the dump has started.
  for (auto& callback_ptr : coredump_callbacks_) {
    (*callback_ptr)(error_code, sub_error_code);
  }
}

int DebugCentral::OpenOrCreateCoredumpBin(const std::string& file_name_prefix) {
  std::string file_name =
      file_name_prefix + GetOrCreateCoredumpTimestampString() + ".bin";

  if (access(file_name.c_str(), F_OK) != 0) {
    // File does not exist, require to create a new one.
    HAL_LOG(WARNING) << "Creating coredump file: " << file_name;
  }

  int fd = open(file_name.c_str(), O_APPEND | O_CREAT | O_SYNC | O_WRONLY,
                S_IRUSR | S_IWUSR | S_IRGRP);

  if (fd < 0) {
    LOG(ERROR) << __func__
               << ": Failed to open or create coredump file: " << file_name
               << ", error: " << strerror(errno) << " (" << errno << ")";
    return -1;
  }

  if (chmod(file_name.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0) {
    LOG(ERROR) << __func__ << ": Unable to change file permissions for "
               << file_name << ", error: " << strerror(errno) << " (" << errno
               << ")";
  }

  // Delete old files and keep the latest ones.
  size_t last_slash_pos = file_name_prefix.find_last_of('/');
  if (last_slash_pos != std::string::npos) {
    auto file_path = file_name_prefix.substr(0, last_slash_pos + 1);
    auto prefix = file_name_prefix.substr(last_slash_pos + 1);
    DeleteOldestBinFiles(file_path, prefix, kMaxCoredumpFiles);
  }
  return fd;
}

std::string DebugCentral::GetOrCreateCoredumpTimestampString() {
  if (crash_timestamp_.empty()) {
    time_t rawtime;
    time(&rawtime);
    struct tm* timeinfo = localtime(&rawtime);

    std::stringstream ss;
    ss << std::to_string(timeinfo->tm_year + 1900) << "-" << std::setw(2)
       << std::setfill('0') << std::to_string(timeinfo->tm_mon + 1) << "-"
       << std::setw(2) << std::setfill('0') << std::to_string(timeinfo->tm_mday)
       << "_" << std::setw(2) << std::setfill('0')
       << std::to_string(timeinfo->tm_hour) << "-" << std::setw(2)
       << std::setfill('0') << std::to_string(timeinfo->tm_min) << "-"
       << std::setw(2) << std::setfill('0') << std::to_string(timeinfo->tm_sec);
    crash_timestamp_ = ss.str();
  }
  return crash_timestamp_;
}

bool DebugCentral::IsCoredumpGenerated() {
  std::lock_guard<std::mutex> lock(coredump_mutex_);
  return is_coredump_generated_;
}

void DebugCentral::ResetCoredumpGenerator() {
  std::lock_guard<std::mutex> lock(coredump_mutex_);
  crash_timestamp_.clear();
  if (is_coredump_generated_) {
    HAL_LOG(ERROR) << "Reset Bluetooth HAL after generating coredump!";
    kill(getpid(), SIGKILL);
  }
}

std::string& DebugCentral::GetCoredumpTimestampString() {
  return crash_timestamp_;
}

std::string DebugCentral::CoredumpErrorCodeToString(
    CoredumpErrorCode error_code, uint8_t sub_error_code) {
  switch (error_code) {
    case CoredumpErrorCode::kForceCollectCoredump:
      return "Force Collect Coredump (BtFw)";
    case CoredumpErrorCode::kControllerHwError:
      return "Controller Hw Error (BtFw)";
    case CoredumpErrorCode::kControllerRootInflammed: {
      std::stringstream ss;
      ss << "Controller Root Inflammed (vendor_error: 0x" << std::hex
         << std::setw(2) << std::setfill('0')
         << static_cast<int>(sub_error_code) << ") - "
         << BqrErrorToStringView(static_cast<BqrErrorCode>(sub_error_code));
      return ss.str();
    }
    case CoredumpErrorCode::kControllerDebugDumpWithoutRootInflammed:
      return "Controller Debug Info Data Dump Without Root Inflammed (BtFw)";
    case CoredumpErrorCode::kControllerDebugInfo:
      return "Debug Info Event (BtFw)";
    case CoredumpErrorCode::kVendor:
      return "Vendor Error";
    default: {
      std::stringstream ss;
      ss << "Unknown Error Code <" << static_cast<int>(error_code) << ">";
      return ss.str();
    }
  }
}

}  // namespace debug
}  // namespace bluetooth_hal
