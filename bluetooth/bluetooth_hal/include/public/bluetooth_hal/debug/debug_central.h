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
#include <sstream>

#include "android-base/logging.h"
#include "bluetooth_hal/bqr/bqr_handler.h"
#include "bluetooth_hal/bqr/bqr_root_inflammation_event.h"
#include "bluetooth_hal/bqr/bqr_types.h"
#include "bluetooth_hal/debug/debug_monitor.h"
#include "bluetooth_hal/hal_packet.h"
#include "bluetooth_hal/util/timer_manager.h"

enum class AnchorType : uint8_t {
  kNone = 0,

  // BluetoothHci
  kInitialize,
  kClose,
  kServiceDied,
  kSendHciCommand,
  kSendAclData,
  kSendScoData,
  kSendIsoData,
  kCallbackHciEvent,
  kCallbackAclData,
  kCallbackScoData,
  kCallbackIsoData,

  // HciRouter
  kRouterInitialize,

  // Thread
  kThreadAcceptClient,
  kThreadDaemonClosed,
  kThreadSocketFileDeleted,
  kThreadClientError,
  kThreadClientConnect,
  kThreadHardwareReset,

  // H4 UART
  kUserialOpen,
  kUserialClose,
  kUserialTtyOpen,

  // PowerManager
  kPowerControl,
  kLowPowerMode,

  // WakelockWatchdog
  kWatchdog,
};

/*
 * DURATION_TRACKER is used to log the Enter and Exit of a HAL function and
 * send to DebugCentral.
 */
#ifdef UNIT_TEST
#define DURATION_TRACKER(type, log)
#else
#define DURATION_TRACKER(type, log) \
  ::bluetooth_hal::debug::DurationTracker duration_##__COUNTER__(type, log);
#endif

/*
 * ANCHOR_LOG* is used to log a message with a specific severity level
 * and send it to DebugCentral. It takes an anchor type as input.
 */
#define ANCHOR_LOG(type)                                                \
  ([](auto&& logger) -> auto&& { return logger; })(                     \
      ::bluetooth_hal::debug::LogHelper(type, ::android::base::VERBOSE, \
                                        LOG_TAG))
#define ANCHOR_LOG_DEBUG(type)   \
  ([](auto&& logger) -> auto&& { \
    return logger;               \
  })(::bluetooth_hal::debug::LogHelper(type, ::android::base::DEBUG, LOG_TAG))
#define ANCHOR_LOG_INFO(type)                       \
  ([](auto&& logger) -> auto&& { return logger; })( \
      ::bluetooth_hal::debug::LogHelper(type, ::android::base::INFO, LOG_TAG))
#define ANCHOR_LOG_WARNING(type)                                        \
  ([](auto&& logger) -> auto&& { return logger; })(                     \
      ::bluetooth_hal::debug::LogHelper(type, ::android::base::WARNING, \
                                        LOG_TAG))
#define ANCHOR_LOG_ERROR(type)   \
  ([](auto&& logger) -> auto&& { \
    return logger;               \
  })(::bluetooth_hal::debug::LogHelper(type, ::android::base::ERROR, LOG_TAG))

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

 private:
  static constexpr int kMaxHistory = 400;
  // Determine if we should hijack the vendor debug event or not
  std::string serial_debug_port_;
  std::string crash_timestamp_;
  std::recursive_mutex mutex_;
  // std::vector<std::unique_ptr<hci::HciEventWatcher>> event_watchers_;
  std::queue<std::vector<uint8_t>> socdump_;
  std::queue<std::vector<uint8_t>> chredump_;
  // BtHal Logger
  std::list<std::pair<std::string, std::string>> history_record_;
  std::map<AnchorType, std::pair<std::string, std::string>> lasttime_record_;
  ::bluetooth_hal::util::Timer debug_info_command_timer_;
  DebugMonitor debug_monitor_;
  ::bluetooth_hal::bqr::BqrHandler bqr_handler_;

  void DumpBluetoothHalLog(int fd);
  void GenerateCrashDump(bool slient_report, const std::string& reason);
  bool OkToGenerateCrashDump(uint8_t error_code);
  bool IsHardwareStageSupported();
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
