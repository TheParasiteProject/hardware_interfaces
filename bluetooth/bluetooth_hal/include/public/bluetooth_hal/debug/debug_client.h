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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bluetooth_hal/debug/debug_types.h"

namespace bluetooth_hal {
namespace debug {

enum class CoredumpPosition : uint8_t {
  kBegin,
  kEnd,
};

struct Coredump {
  std::string title;
  std::string coredump;
  CoredumpPosition position;
};

/**
 * @brief A child class extends DebugCentral will automatically receive
 * OnGenerateCoredump and Dump callbacks for debugging. The child class can
 * choose to not implement any of those functions if they are not needed.
 *
 * The coredump is generated with the format below:
 *
 * ╔══════════════════════════════════════════════════════════
 * ║ BEGIN of Bluetooth HAL DUMP
 * ╠══════════════════════════════════════════════════════════
 * ║
 * ║    =============================================
 * ║    TITLE FOR CoredumpPosition::kBegin 1
 * ║    =============================================
 * ║        COREDUMP for CoredumpPosition::kBegin 1
 * ║
 * ║    =============================================
 * ║    TITLE FOR CoredumpPosition::kBegin 2
 * ║    =============================================
 * ║        COREDUMP for CoredumpPosition::kBegin 2
 * ║    ...
 * ║
 * ║    =============================================
 * ║    Default Bluetooth HAL dump
 * ║    =============================================
 * ║        dump
 * ║
 * ║    =============================================
 * ║    TITLE FOR CoredumpPosition::kEnd 1
 * ║    =============================================
 * ║        COREDUMP for CoredumpPosition::kEnd 1
 * ║    ...
 * ║
 * ╠══════════════════════════════════════════════════════════
 * ║ END of Bluetooth HAL DUMP
 * ╚══════════════════════════════════════════════════════════
 *
 */
class DebugClient {
 public:
  DebugClient();
  virtual ~DebugClient();

  /**
   * @brief OnGenerateCoredump is automatically called by the DebugCentral if
   * any error was detected and the HAL decided to generate a coredump for the
   * following crash.
   *
   * A child class can decide to collect logs or generate their own dump files
   * if required.
   *
   * Dump() will be called soon after OnGenerateCoredump() is invoked.
   *
   * @param error_code The main coredump error code of the coredump.
   * @param sub_error_code The sub error code of the coredump.
   */
  virtual void OnGenerateCoredump([[maybe_unused]] CoredumpErrorCode error_code,
                                  [[maybe_unused]] uint8_t sub_error_code) {}

  /**
   * @brief Dump() can be called for two scenarios:
   *    1. When the Android dumpsys or bugreport is triggered.
   *    2. When the DebugCentral detects an error, called after
   *    OnGenerateCoredump().
   *
   * @return A vector of Coredump is returned to the DebugCentral. the Coredumps
   * will be transformed into text logs based on the parameters set in it.
   */
  virtual std::vector<Coredump> Dump() { return std::vector<Coredump>(); }
};

}  // namespace debug
}  // namespace bluetooth_hal
