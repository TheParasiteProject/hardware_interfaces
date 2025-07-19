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
#include <list>
#include <string>
#include <unordered_map>

#include "bluetooth_hal/bluetooth_address.h"
#include "bluetooth_hal/hal_packet.h"
#include "bluetooth_hal/hci_monitor.h"
#include "bluetooth_hal/hci_router_client.h"

namespace bluetooth_hal {
namespace debug {

class BluetoothActivities : public ::bluetooth_hal::hci::HciRouterClient {
 public:
  BluetoothActivities();

  /**
   * @brief Checks if there are any connected Bluetooth devices.
   *
   * @return true if there is at least one connected device, false otherwise.
   */
  bool HasConnectedDevice();

 protected:
  void OnCommandCallback(
      [[maybe_unused]] const ::bluetooth_hal::hci::HalPacket& packet) override {
  };
  void OnMonitorPacketCallback(
      ::bluetooth_hal::hci::MonitorMode mode,
      const ::bluetooth_hal::hci::HalPacket& packet) override;
  void OnBluetoothChipReady() override {};
  void OnBluetoothChipClosed() override {};
  void OnBluetoothEnabled() override {};
  void OnBluetoothDisabled() override {};

 private:
  struct ConnectionActivity {
    uint16_t connection_handle;
    ::bluetooth_hal::hci::BluetoothAddress bd_address;
    std::string event;
    std::string status;
    std::string timestamp;
  };

  void UpdateConnectionHistory(const ConnectionActivity& device);
  void HandleBleMetaEvent(const ::bluetooth_hal::hci::HalPacket& event);
  void HandleConnectCompleteEvent(const ::bluetooth_hal::hci::HalPacket& event);
  void HandleDisconnectCompleteEvent(
      const ::bluetooth_hal::hci::HalPacket& event);

  ::bluetooth_hal::hci::HciBleMetaEventMonitor
      ble_connection_complete_event_monitor_;
  ::bluetooth_hal::hci::HciBleMetaEventMonitor
      ble_enhanced_connection_complete_v1_event_monitor_;
  ::bluetooth_hal::hci::HciBleMetaEventMonitor
      ble_enhanced_connection_complete_v2_event_monitor_;
  ::bluetooth_hal::hci::HciEventMonitor connection_complete_event_monitor_;
  ::bluetooth_hal::hci::HciEventMonitor disconnection_complete_event_monitor_;
  std::list<ConnectionActivity> connection_history_;
  std::unordered_map<uint16_t, ::bluetooth_hal::hci::BluetoothAddress>
      connected_device_address_;
};

}  // namespace debug
}  // namespace bluetooth_hal
