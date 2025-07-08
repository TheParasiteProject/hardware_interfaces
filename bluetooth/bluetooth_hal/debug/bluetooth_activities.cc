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

#define LOG_TAG "bluetooth_hal.bt_activities"

#include "bluetooth_hal/debug/bluetooth_activities.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <cstdint>

#include "bluetooth_hal/hal_packet.h"
#include "bluetooth_hal/hal_types.h"

namespace bluetooth_hal {
namespace debug {
namespace {

using ::bluetooth_hal::hci::EventCode;
using ::bluetooth_hal::hci::HalPacket;
using ::bluetooth_hal::hci::HciEventMonitor;
using ::bluetooth_hal::hci::MonitorMode;

constexpr uint16_t kBtMaxConnectHistoryRecord = 1024;

}  // namespace

BluetoothActivities::BluetoothActivities()
    : ble_connection_complete_event_monitor_(
          HciEventMonitor(static_cast<uint8_t>(EventCode::kBleMeta))),
      connection_complete_event_monitor_(HciEventMonitor(
          static_cast<uint8_t>(EventCode::kConnectionComplete))),
      disconnection_complete_event_monitor_(HciEventMonitor(
          static_cast<uint8_t>(EventCode::kDisconnectionComplete))) {
  RegisterMonitor(ble_connection_complete_event_monitor_,
                  MonitorMode::kMonitor);
  RegisterMonitor(connection_complete_event_monitor_, MonitorMode::kMonitor);
  RegisterMonitor(disconnection_complete_event_monitor_, MonitorMode::kMonitor);
}

bool BluetoothActivities::HasConnectedDevice() {
  return connected_device_address_.size() > 0;
}

void BluetoothActivities::OnMonitorPacketCallback(
    [[maybe_unused]] MonitorMode mode, const HalPacket& packet) {
  switch (packet.GetEventCode()) {
    case static_cast<uint8_t>(EventCode::kBleMeta):
      HandleBleMetaEvent(packet);
      break;
    case static_cast<uint8_t>(EventCode::kConnectionComplete):
      HandleConnectCompleteEvent(packet);
      break;
    case static_cast<uint8_t>(EventCode::kDisconnectionComplete):
      HandleDisconnectCompleteEvent(packet);
      break;
  }
}

void BluetoothActivities::HandleBleMetaEvent(const HalPacket& event) {
  (void)event;
}

void BluetoothActivities::HandleConnectCompleteEvent(const HalPacket& event) {
  (void)event;
}

void BluetoothActivities::HandleDisconnectCompleteEvent(
    const HalPacket& event) {
  (void)event;
}

void BluetoothActivities::UpdateConnectionHistory(
    const ConnectionActivity& device) {
  if (connection_history_.size() >= kBtMaxConnectHistoryRecord) {
    connection_history_.pop_front();
  }
  connection_history_.emplace_back(device);
}

}  // namespace debug
}  // namespace bluetooth_hal
