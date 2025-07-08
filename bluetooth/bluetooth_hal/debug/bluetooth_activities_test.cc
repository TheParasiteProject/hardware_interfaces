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

#include "bluetooth_hal/debug/bluetooth_activities.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "bluetooth_hal/hal_packet.h"
#include "bluetooth_hal/hal_types.h"
#include "bluetooth_hal/test/mock/mock_hci_router.h"
#include "bluetooth_hal/test/mock/mock_hci_router_client_agent.h"

namespace bluetooth_hal {
namespace debug {
namespace {

using ::bluetooth_hal::HalState;
using ::bluetooth_hal::hci::HalPacket;
using ::bluetooth_hal::hci::MockHciRouter;
using ::bluetooth_hal::hci::MockHciRouterClientAgent;
using ::bluetooth_hal::hci::MonitorMode;
using ::testing::_;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::Test;

class BluetoothActivitiesForTest : public BluetoothActivities {
 public:
  void OnMonitorPacketCallback(MonitorMode mode,
                               const HalPacket& packet) override {
    BluetoothActivities::OnMonitorPacketCallback(mode, packet);
  }

  void EnableBluetooth() {
    OnBluetoothChipReady();
    OnBluetoothEnabled();
    OnHalStateChanged(HalState::kRunning, HalState::kBtChipReady);
    OnPacketCallback(HalPacket({0x04, 0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00}));
  }

  void DisableBluetooth() {
    OnBluetoothDisabled();
    OnBluetoothChipClosed();
  }
};

class BluetoothActivitiesTest : public Test {
 protected:
  void SetUp() override {
    MockHciRouterClientAgent::SetMockAgent(&mock_hci_router_client_agent_);
    EXPECT_CALL(mock_hci_router_client_agent_, RegisterRouterClient(NotNull()))
        .WillOnce(Return(true));

    MockHciRouter::SetMockRouter(&mock_hci_router_);
    ON_CALL(mock_hci_router_, Send(_)).WillByDefault(Return(true));
    ON_CALL(mock_hci_router_, SendCommand(_, _)).WillByDefault(Return(true));

    bluetooth_activities = std::make_unique<BluetoothActivitiesForTest>();
  }

  void TearDown() override {
    EXPECT_CALL(mock_hci_router_client_agent_,
                UnregisterRouterClient(bluetooth_activities.get()))
        .WillOnce(Return(true));
    bluetooth_activities.reset();
  }

  void EnableBluetooth() {
    ON_CALL(mock_hci_router_, GetHalState())
        .WillByDefault(Return(HalState::kRunning));
    ON_CALL(mock_hci_router_client_agent_, IsBluetoothChipReady())
        .WillByDefault(Return(true));
    ON_CALL(mock_hci_router_client_agent_, IsBluetoothEnabled())
        .WillByDefault(Return(true));
    bluetooth_activities->EnableBluetooth();
  }

  void DisableBluetooth() {
    ON_CALL(mock_hci_router_, GetHalState())
        .WillByDefault(Return(HalState::kBtChipReady));
    ON_CALL(mock_hci_router_client_agent_, IsBluetoothChipReady())
        .WillByDefault(Return(false));
    ON_CALL(mock_hci_router_client_agent_, IsBluetoothEnabled())
        .WillByDefault(Return(false));
    bluetooth_activities->DisableBluetooth();
  }

  MockHciRouter mock_hci_router_;
  MockHciRouterClientAgent mock_hci_router_client_agent_;
  std::unique_ptr<BluetoothActivitiesForTest> bluetooth_activities;
};

TEST_F(BluetoothActivitiesTest, InitialState) {
  EnableBluetooth();
  EXPECT_FALSE(bluetooth_activities->HasConnectedDevice());
}

}  // namespace
}  // namespace debug
}  // namespace bluetooth_hal
