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

#define LOG_TAG "bthal.bqr"

#include "bluetooth_hal/bqr/bqr_handler.h"

#include <cstdint>

#include "android-base/logging.h"
#include "bluetooth_hal/bqr/bqr_event.h"
#include "bluetooth_hal/bqr/bqr_root_inflammation_event.h"
#include "bluetooth_hal/debug/debug_central.h"
#include "bluetooth_hal/hal_packet.h"
#include "bluetooth_hal/hal_types.h"
#include "bluetooth_hal/hci_monitor.h"
#include "bluetooth_hal/hci_router_client.h"

namespace bluetooth_hal {
namespace bqr {

using ::bluetooth_hal::debug::DebugCentral;
using ::bluetooth_hal::hci::HalPacket;
using ::bluetooth_hal::hci::MonitorMode;

BqrHandler::BqrHandler() {
  RegisterMonitor(bqr_event_monitor, MonitorMode::kMonitor);
}

BqrHandler& BqrHandler::GetHandler() {
  static BqrHandler handler;
  return handler;
}

void BqrHandler::OnMonitorPacketCallback([[maybe_unused]] MonitorMode mode,
                                         const HalPacket& packet) {
  BqrEvent bqr_event(packet);
  if (bqr_event.IsValid()) {
    auto bqr_event_type = bqr_event.GetBqrEventType();
    switch (bqr_event_type) {
      case BqrEventType::kRootInflammation:
        HandleRootInflammationEvent(bqr_event);
        break;
      default:
        break;
    }
  }
}

void BqrHandler::HandleRootInflammationEvent(const BqrEvent& bqr_event) {
  BqrRootInflammationEvent root_inflammation(bqr_event);
  if (!root_inflammation.IsValid()) {
    return;
  }
  LOG(ERROR) << "Received a root inflammation event! " << bqr_event.ToString();
  DebugCentral::Get().HandleRootInflammationEvent(root_inflammation);
}

}  //  namespace bqr
}  //  namespace bluetooth_hal
