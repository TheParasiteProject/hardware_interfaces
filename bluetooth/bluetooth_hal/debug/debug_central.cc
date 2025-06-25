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

#define LOG_TAG "bthal.debug_central"

#include "bluetooth_hal/debug/debug_central.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
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
const std::string kDumpReasonForceCollectCoredump = "Force Collect Coredump";
const std::string kDumpReasonControllerHwError = "ControllerHwError";
const std::string kDumpReasonControllerRootInflammed =
    "ControllerRootInflammed";
const std::string kDumpReasonControllerDebugDumpWithoutRootInflammed =
    "ControllerDebugInfoDataDumpWithoutRootInflammed";
const std::string kDumpReasonControllerDebugInfo = "Debug Info Event";

const std::string kCrashInfoFilePath = "/data/vendor/ssrdump/";
const std::string kSsrdumpFilePath = "/data/vendor/ssrdump/coredump/";
const std::string kCrashInfoFilePrefix = "crashinfo_bt_";
const std::string kSsrdumpFilePrefix = "coredump_bt_";
const std::string kSsrdumpSocFilePrefix = "coredump_bt_socdump_";
const std::string kSsrdumpChreFilePrefix = "coredump_bt_chredump_";
const std::string kSocdumpFilePath =
    "/data/vendor/ssrdump/coredump/coredump_bt_socdump_";
const std::string kChredumpFilePath =
    "/data/vendor/ssrdump/coredump/coredump_bt_chredump_";

const std::string kDebugNodeBtLpm = "dev/logbuffer_btlpm";
constexpr char kDebugNodeBtUartPrefix[] = "/dev/logbuffer_tty";
constexpr char kHwStage[] = "ro.boot.hardware.revision";
constexpr uint8_t kReservedCoredumpFileCount = 2;

std::string GetTimestampString() {
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
  return ss.str();
}

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

void read_as_hex(std::ifstream& file, std::stringstream& content) {
  std::array<char, 64> memblock{};
  while (!file.eof()) {
    file.read(memblock.data(), memblock.size());
    size_t len = file.gcount();
    if (len == 0) continue;

    for (size_t i = 0; i < len; i++) {
      content << std::setbase(16) << std::setw(2) << std::setfill('0')
              << (memblock[i] & 0xff);
    }
    content << "\n";
  }
}

void GetStringLogFromStorage(int fd, const std::string& prefix,
                             const std::string& dir) {
  std::unique_ptr<DIR, decltype(&closedir)> dir_dump(opendir(dir.c_str()),
                                                     closedir);

  if (!dir_dump) {
    LOG(WARNING) << __func__ << ": Failed to open directory, skip " << prefix
                 << ".";
    return;
  }

  std::stringstream content;
  struct dirent* dp;
  while ((dp = readdir(dir_dump.get()))) {
    std::ifstream file;
    std::string file_name;
    std::stringstream path;

    if (dp->d_type != DT_REG) {
      continue;
    }
    file_name = dp->d_name;
    size_t pos = file_name.find(prefix);
    if (pos != 0) {
      continue;
    }

    path << dir << file_name;
    LOG(DEBUG) << __func__ << ": Dumping " << path.str() << ".";

    // for coredump_bt_socdump_[timestamp].bin
    std::size_t last_dot = file_name.rfind('.');
    bool is_bin = (last_dot != std::string::npos &&
                   file_name.substr(last_dot + 1) == "bin" &&
                   (file_name.find(kSsrdumpSocFilePrefix) == 0 ||
                    file_name.find(kSsrdumpChreFilePrefix) == 0));
    if (is_bin)
      file.open(path.str(), std::ifstream::binary);
    else
      file.open(path.str());
    content << "*********************************************\n\n";
    content << "BEGIN of LogFile: " << file_name << "\n\n";
    content << "*********************************************\n";
    if (file_name.length() != 0 && file.is_open()) {
      if (is_bin) {
        read_as_hex(file, content);
      } else {
        content << file.rdbuf() << std::endl;
      }
    } else {
      content << "File open failed: " << prefix << std::endl;
    }
    content << "*********************************************\n\n";
    content << "END of LogFile: " << file_name << "\n\n";
    content << "*********************************************\n\n";
  }
  write(fd, content.str().c_str(), content.str().length());
}

int OpenFileWithTimestamp(const std::string& crash_timestamp) {
  std::stringstream fname;
  fname << kSocdumpFilePath << crash_timestamp << ".bin";
  int socdump_fd;
  if ((socdump_fd =
           open(fname.str().c_str(), O_APPEND | O_CREAT | O_SYNC | O_WRONLY,
                S_IRUSR | S_IWUSR | S_IRGRP)) < 0) {
    LOG(ERROR) << __func__ << ": Failed to open socdump file: " << fname.str()
               << ", failed: " << strerror(errno) << " (" << errno << ")";
  }
  // Change the file's permissions to OWNER Read/Write, GROUP Read, OTHER Read
  if (chmod(fname.str().c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0) {
    LOG(ERROR) << __func__ << ": Unable to change file permissions "
               << fname.str() << ".";
  }
  return socdump_fd;
}

bool IsCoredumpFile(const std::string& filename) {
  return filename.find("coredump_bt_") == 0 &&
         filename.find(".bin") == filename.size() - 4;
}

struct CoredumpFile {
  std::string filename;
  time_t timestamp;
};

bool CompareFileCreatedTime(const CoredumpFile& a, const CoredumpFile& b) {
  return a.timestamp < b.timestamp;
}

void DeleteCoredumpFiles(const std::string& dir) {
  std::vector<CoredumpFile> coredumpfiles;
  DIR* dir_ptr = opendir(dir.c_str());
  if (dir_ptr != nullptr) {
    struct dirent* entry;
    while ((entry = readdir(dir_ptr)) != nullptr) {
      if (IsCoredumpFile(entry->d_name)) {
        struct stat statbuf;
        stat((dir + "/" + entry->d_name).c_str(), &statbuf);
        coredumpfiles.push_back({entry->d_name, statbuf.st_mtime});
      }
    }
    closedir(dir_ptr);
  }

  sort(coredumpfiles.begin(), coredumpfiles.end(), CompareFileCreatedTime);
  LOG(INFO) << __func__ << ": Coredump files count: " << coredumpfiles.size()
            << ".";
  if (coredumpfiles.size() > kReservedCoredumpFileCount) {
    for (int i = 0; i < coredumpfiles.size() - kReservedCoredumpFileCount;
         i++) {
      remove((dir + "/" + coredumpfiles[i].filename).c_str());
      LOG(INFO) << __func__
                << ": Delete file: " << (dir + "/" + coredumpfiles[i].filename)
                << ".";
    }
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

void DebugCentral::Dump(int fd) {
  // Dump BtHal debug log
  DumpBluetoothHalLog(fd);
  if (TransportInterface::GetTransportType() == TransportType::kUartH4) {
    // Dump Kernel driver debugfs log
    DumpDebugfs(fd, serial_debug_port_);
    DumpDebugfs(fd, kDebugNodeBtLpm);
  }
  // Dump all crashinfo_bt files in ssrdump folder
  GetStringLogFromStorage(fd, kCrashInfoFilePrefix, kCrashInfoFilePath);
  // Dump all coredump_bt files in coredump folder
  LOG(INFO) << __func__
            << ": Write bt coredump files to `IBluetoothHci_default.txt`.";
  GetStringLogFromStorage(fd, kSsrdumpFilePrefix, kSsrdumpFilePath);
  // Dump Controller BT Activities Statistics
  BtActivitiesLogger::GetInstacne()->ForceUpdating();
  BtActivitiesLogger::GetInstacne()->DumpBtActivitiesStatistics(fd);
  DeleteCoredumpFiles(kSsrdumpFilePath);
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
  // collect debug dump and popup ssrdump notification UI
  HAL_LOG(ERROR) << extra_info;
  LOG(ERROR) << __func__ << ": Root inflamed event with error_code: ("
             << static_cast<uint8_t>(error) << "), error_info: " << extra_info
             << ".";
  // report bqr root inflamed event to Stack
  HciRouter::GetRouter().SendPacketToStack(bqr_event);

  if (OkToGenerateCrashDump(static_cast<uint8_t>(error))) {
    GenerateCrashDump(
        false, kDumpReasonControllerRootInflammed + " (" +
                   StringPrintf("error_code: 0x%02hhX",
                                static_cast<unsigned char>(error)) +
                   ")" + " - " + std::string(BqrErrorToStringView(error)));
    LogFatal(error, extra_info);
  } else {
    LOG(ERROR) << __func__ << ": Silent recover!";
    ThreadHandler::Cleanup();
    kill(getpid(), SIGKILL);
  }
}

void DebugCentral::HandleDebugInfoCommand() {
  // It is supported to generate coredump and record crash_timestamp_ when bthal
  // received root-inflamed event or any fw dump packet, if the controller
  // did not send any response packets, we force to trigger coredump here
  debug_info_command_timer_.Schedule(
      [this]() {
        if (crash_timestamp_.empty()) {
          LOG(ERROR) << __func__
                     << ": Force a coredump to be generated if it has not been "
                        "generated for 1 second.";
          GenerateCrashDump(true, kDumpReasonForceCollectCoredump + " (BtFw)");
        }
      },
      std::chrono::milliseconds(kHandleDebugInfoCommandMs));
}

void DebugCentral::SetControllerFirmwareInformation(const std::string& info) {
  controller_firmware_info_ = info;
}

bool DebugCentral::IsHardwareStageSupported() {
  std::string cur_hw_stage = ::android::base::GetProperty(kHwStage, "default");
  std::vector<std::string> not_supported_hw_stages =
      HalConfigLoader::GetLoader().GetFwUnsupportedHwStages();
  return std::find_if(not_supported_hw_stages.begin(),
                      not_supported_hw_stages.end(),
                      [&](std::string& not_supported_hw_stage) {
                        return cur_hw_stage == not_supported_hw_stage;
                      }) == not_supported_hw_stages.end();
}

bool DebugCentral::OkToGenerateCrashDump(uint8_t error_code) {
  // Report scenario:
  // 1) report ssr crash when bt is on
  // 2) report ssr crash when bt is off, thread is enabled, and supports
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
    GenerateCrashDump(
        false, kDumpReasonControllerRootInflammed + " (" +
                   StringPrintf("vendor_error: 0x%02hhX", vendor_error_code) +
                   ")" + " - " +
                   std::string(BqrErrorToStringView(
                       static_cast<BqrErrorCode>(vendor_error_code))));
  }
}

void DebugCentral::HandleDebugInfoEvent(const HalPacket& packet) {
  bool last_soc_dump_packet = false;
  if (packet.size() <= kDebugInfoPayloadOffset) {
    LOG(INFO) << __func__ << ": Invalid length of debug info event!";
    return;
  }

  if (crash_timestamp_.empty()) {
    GenerateCrashDump(IsHardwareStageSupported() ? false : true,
                      kDumpReasonControllerDebugInfo + " (BtFw)");
  }

  // the Last soc dump debug info packet has been received
  if (packet[kDebugInfoLastBlockOffset]) {
    LOG(INFO) << __func__ << ": Last soc dump fragment has been received.";
    last_soc_dump_packet = true;
  }

  int socdump_fd;
  if ((socdump_fd = OpenFileWithTimestamp(crash_timestamp_)) < 0) {
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

void DebugCentral::GenerateCrashDump(bool silent_report,
                                     const std::string& reason) {
  if (!crash_timestamp_.empty()) {
    // coredump has already been generated, avoid duplicated dump in one crash
    // cycle
    return;
  }

  LOG(ERROR) << __func__ << ": Reason: " << reason.c_str()
             << ", silent_report:" << silent_report << ".";
  crash_timestamp_ = GetTimestampString();
  std::stringstream coredump_fname;
  coredump_fname << kSsrdumpFilePath << kSsrdumpFilePrefix << crash_timestamp_
                 << ".bin";
  LOG(WARNING) << __func__ << ": Starting to generate Bluetooth ssrdump files: "
               << coredump_fname.str().c_str() << ".";

  int coredump_fd;
  if ((coredump_fd =
           open(coredump_fname.str().c_str(), O_CREAT | O_SYNC | O_RDWR,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
    LOG(ERROR) << __func__ << ": Failed to open coredump file: "
               << coredump_fname.str().c_str()
               << ", failed: " << strerror(errno) << " (" << errno << ").";
    return;
  }
  fchmod(coredump_fd, S_IRUSR | S_IRGRP | S_IROTH);

  std::stringstream ss;
  ss << "DUMP REASON: " << std::string(reason) << " - occurred at "
     << crash_timestamp_ << std::endl;
  write(coredump_fd, ss.str().c_str(), ss.str().length());

  DumpBluetoothHalLog(coredump_fd);
  LOG(INFO) << __func__ << ": Request to get Transport Layer Debug Dump.";
  // TODO: b/373786258 - Need to dump debug info.
  close(coredump_fd);

  if (silent_report) {
    return;
  }
  // generate crashinfo file
  std::stringstream crashinfo_fname;
  crashinfo_fname << kCrashInfoFilePath << kCrashInfoFilePrefix
                  << crash_timestamp_ << ".txt";

  int crashinfo_fd;
  if ((crashinfo_fd =
           open(crashinfo_fname.str().c_str(), O_CREAT | O_SYNC | O_RDWR,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
    LOG(ERROR) << __func__
               << ": Failed to open crashinfo file: " << crashinfo_fname.str()
               << ", failed: " << strerror(errno) << " (" << errno << ").";
    return;
  }
  fchmod(crashinfo_fd, S_IRUSR | S_IRGRP | S_IROTH);

  std::stringstream crashinfo_ss;
  static int crash_count = 0;
  crashinfo_ss << "crash_reason: " << std::string(reason) << std::endl;
  crash_count++;
  crashinfo_ss << "crash_count: " << std::to_string(crash_count) << std::endl;
  crashinfo_ss << "timestamp: " << crash_timestamp_ << std::endl;
  write(crashinfo_fd, crashinfo_ss.str().c_str(), crashinfo_ss.str().length());
  close(crashinfo_fd);
}

}  // namespace debug
}  // namespace bluetooth_hal
