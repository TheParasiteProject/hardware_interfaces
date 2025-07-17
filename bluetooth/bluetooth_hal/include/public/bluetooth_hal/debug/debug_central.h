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

#pragma once

#include <signal.h>

#include <cstdint>
#include <list>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "android-base/logging.h"
#include "bluetooth_hal/bqr/bqr_handler.h"
#include "bluetooth_hal/bqr/bqr_root_inflammation_event.h"
#include "bluetooth_hal/bqr/bqr_types.h"
#include "bluetooth_hal/debug/bluetooth_activities.h"
#include "bluetooth_hal/debug/debug_client.h"
#include "bluetooth_hal/debug/debug_monitor.h"
#include "bluetooth_hal/debug/debug_types.h"
#include "bluetooth_hal/hal_packet.h"
#include "bluetooth_hal/util/timer_manager.h"

/*
 * SCOPED_ANCHOR is used to log the Enter and Exit of a HAL function and
 * send to DebugCentral.
 */
#ifdef UNIT_TEST
#define SCOPED_ANCHOR(type, log)
#else
#define SCOPED_ANCHOR(type, log)                                  \
  ::bluetooth_hal::debug::DurationTracker duration_##__COUNTER__( \
      ::bluetooth_hal::debug::type, log);
#endif

/*
 * ANCHOR_LOG* is used to log a message with a specific severity level
 * and send it to DebugCentral. It takes an anchor type as input.
 */
#define ANCHOR_LOG(type)                                              \
  ([](auto&& logger) -> auto&& { return logger; })(                   \
      ::bluetooth_hal::debug::LogHelper(::bluetooth_hal::debug::type, \
                                        ::android::base::VERBOSE, LOG_TAG))
#define ANCHOR_LOG_DEBUG(type)                                        \
  ([](auto&& logger) -> auto&& { return logger; })(                   \
      ::bluetooth_hal::debug::LogHelper(::bluetooth_hal::debug::type, \
                                        ::android::base::DEBUG, LOG_TAG))
#define ANCHOR_LOG_INFO(type)                                         \
  ([](auto&& logger) -> auto&& { return logger; })(                   \
      ::bluetooth_hal::debug::LogHelper(::bluetooth_hal::debug::type, \
                                        ::android::base::INFO, LOG_TAG))
#define ANCHOR_LOG_WARNING(type)                                      \
  ([](auto&& logger) -> auto&& { return logger; })(                   \
      ::bluetooth_hal::debug::LogHelper(::bluetooth_hal::debug::type, \
                                        ::android::base::WARNING, LOG_TAG))
#define ANCHOR_LOG_ERROR(type)                                        \
  ([](auto&& logger) -> auto&& { return logger; })(                   \
      ::bluetooth_hal::debug::LogHelper(::bluetooth_hal::debug::type, \
                                        ::android::base::ERROR, LOG_TAG))

/*
 * HAL_LOG pinrts system log, as well as stores it in the DebugCentral for
 * Dump()
 */
#define HAL_LOG(severity)                           \
  ([](auto&& logger) -> auto&& { return logger; })( \
      ::bluetooth_hal::debug::LogHelper(::android::base::severity, LOG_TAG))

/*
 * Helper mecro for LogHelper to print system log with a specific tag.
 */
#define LOG_WITH_TAG(severity, tag)                                          \
  ::android::base::LogMessage(__FILE__, __LINE__, SEVERITY_LAMBDA(severity), \
                              tag, -1)                                       \
      .stream()

namespace bluetooth_hal {
namespace debug {

class DurationTracker {
 public:
  DurationTracker(AnchorType type, const std::string& log);

  // Manually release the auto debug anchor.
  ~DurationTracker();

 private:
  std::string log_;
  AnchorType type_;
};

class DebugCentral {
 public:
  /*
   * Get a singleton static instance of the debug central.
   */
  static DebugCentral& Get();

  /**
   * @brief Register a debug client to receive debug callbacks from the
   * DebugCentral.
   *
   * @param debug_client The client resigers for debug information.
   * @return return true if success, otherwise false.
   */
  bool RegisterDebugClient(DebugClient* debug_client);

  /**
   * @brief Unregister the debug client that was registered.
   *
   * @param callback The debug client to be unregistered.
   * @return return true if success, otherwise false.
   */
  bool UnregisterDebugClient(DebugClient* debug_client);

  /*
   * Invokes when bugreport is triggered, dump all information to the debug fd.
   */
  void Dump(int fd);

  /*
   * set bluetooth serial port information.
   */
  void SetBtUartDebugPort(const std::string& uart_port);

  /*
   * Write debug message to logger.
   */
  void UpdateRecord(AnchorType type, const std::string& anchor);

  /*
   * Notify BtHal have detected error, we will collect debug log first then and
   * report eror code to stack via BQR root inflammation event
   */
  void ReportBqrError(::bluetooth_hal::bqr::BqrErrorCode error,
                      std::string extra_info);

  /**
   * @brief Inform DebugCentral to handle Root Inflammation Event reported from
   * the Bluetooth chip. It also generates a Bluetooth HAL coredump.
   *
   * @param packet The root inflammation event.
   */
  void HandleRootInflammationEvent(
      const ::bluetooth_hal::bqr::BqrRootInflammationEvent& event);

  /**
   * @brief Inform DebugCentral to handle Debug Info Event reported from the
   * Bluetooth chip. It also generates a Bluetooth HAL coredump.
   *
   * @param packet The debug info event.
   */
  void HandleDebugInfoEvent(const ::bluetooth_hal::hci::HalPacket& packet);

  /**
   * @brief Inform DebugCentral to handle Debug Info Command sent from the
   * stack. It generates a Bluetooth HAL coredump if the Bluetooth chip did
   * not report Debug Info events in time.
   */
  void HandleDebugInfoCommand();

  /**
   * @brief Sets controller firmware information for debugging.
   * This optional API allows OEM vendors to provide additional firmware
   * details, which will be included in bugreports to aid debugging.
   *
   * @param info A string containing the firmware information to be printed in
   * the bugreport.
   */
  void SetControllerFirmwareInformation(const std::string& info);

  /**
   * @brief Request the Bluetooth HAL to generate a vendor dump file. This also
   * triggers the Bluetoth HAL core dump and prepare for a crash. It can trigger
   * a CoredumpCallback to the caller if the coredump procedure was initiated by
   * the vendor implementation.
   *
   * The generated file name contains the timestamp of the first dump request in
   * this Bluetooth cycle.
   *
   * @param file_path The path and the prefix of the file, for example
   * "/path/file-" generates a dump file of "/path/file-YYYY-MM-DD-SS.bin".
   * @param data The data to write into the file.
   * @param vendor_error_code The vendor specific error code to record in the
   * coredump file. If the coredump was initiated by the vendor implementation,
   * this vendor erroc code is also sent back to the caller as sub_error_code.
   */
  void GenerateVendorDumpFile(const std::string& file_path,
                              const std::vector<uint8_t>& data,
                              uint8_t vendor_error_code = 0);

  /**
   * @brief The debug central only keeps one coredump per Bluetooth cycle.
   * Invoking this function forces a reset to the coredump generator in case
   * more coredumps are needed.
   */
  void ResetCoredumpGenerator();

  /**
   * @brief Check if the DebugCentral is generating a coredump.
   *
   * @return true if the DebugCentral is generating a coredump, otherwise false.
   */
  bool IsCoredumpGenerated();

  /**
   * @brief Get the timestamp of the coredump generated recently. The timestamp
   * string is used as the suffix of the coredump files.
   *
   * @return The coredump timestamp in std::string, with the format of
   * YYYY-MM-DD-MM-SS. Returns an empty string if no coredump was generated
   * recently.
   */
  std::string& GetCoredumpTimestampString();

  /**
   * @brief A helper function that returns the std::string format of
   * CoredumpErrorCode.
   *
   * @param error_code The CoredumpErrorCode to transform to string.
   * @param sub_error_code An optional sub error code that is used by some of
   * the CoredumpErrorCodes.
   * @return The CoredumpErrorCode in std::string.
   */
  static std::string CoredumpErrorCodeToString(CoredumpErrorCode error_code,
                                               uint8_t sub_error_code);

 private:
  static constexpr int kMaxHistory = 400;
  std::string serial_debug_port_;
  std::string crash_timestamp_;
  std::recursive_mutex mutex_;
  std::string controller_firmware_info_;
  std::list<std::pair<std::string, std::string>> history_record_;
  std::map<AnchorType, std::pair<std::string, std::string>> lasttime_record_;
  ::bluetooth_hal::util::Timer debug_info_command_timer_;
  DebugMonitor debug_monitor_;
  BluetoothActivities bluetooth_activities_;
  ::bluetooth_hal::bqr::BqrHandler bqr_handler_;
  std::unordered_set<DebugClient*> debug_clients_;
  bool is_coredump_generated_;

  std::string DumpBluetoothHalLog();
  void GenerateCoredump(CoredumpErrorCode error_code,
                        uint8_t sub_error_code = 0);
  bool OkToGenerateCrashDump(uint8_t error_code);
  bool IsHardwareStageSupported();
  std::string GetOrCreateCoredumpTimestampString();
  int OpenOrCreateCoredumpBin(const std::string& file_prefix);
  std::vector<Coredump> GetCoredumpFromDebugClients();
};

class LogHelper {
 public:
  LogHelper(AnchorType type, ::android::base::LogSeverity severity,
            const char* tag)
      : type_(type), severity_(severity), tag_(tag) {}

  LogHelper(::android::base::LogSeverity severity, const char* tag)
      : type_(AnchorType::kNone), severity_(severity), tag_(tag) {}

  template <typename T>
  LogHelper& operator<<(const T& value) {
    oss_ << value;
    return *this;
  }

  ~LogHelper() {
    std::string log_message = oss_.str();
    if (!log_message.empty()) {
#ifdef UNIT_TEST
      (void)type_;
#else
      DebugCentral::Get().UpdateRecord(type_, log_message);
#endif
      LOG_WITH_TAG(severity_, tag_) << log_message;
    }
  }

 private:
  AnchorType type_;
  ::android::base::LogSeverity severity_;
  std::ostringstream oss_;
  const char* tag_;
};

}  // namespace debug
}  // namespace bluetooth_hal
