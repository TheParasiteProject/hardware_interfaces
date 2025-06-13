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

#include "bluetooth_hal/config/firmware_config_loader.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bluetooth_hal/config/config_constants.h"
#include "bluetooth_hal/hal_packet.h"
#include "bluetooth_hal/test/mock/mock_android_base_wrapper.h"
#include "bluetooth_hal/test/mock/mock_hal_config_loader.h"
#include "bluetooth_hal/test/mock/mock_system_call_wrapper.h"
#include "gtest/gtest.h"

namespace bluetooth_hal {
namespace config {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrictMock;
using ::testing::Test;
using ::testing::Values;
using ::testing::WithParamInterface;

using ::bluetooth_hal::config::MockHalConfigLoader;
using ::bluetooth_hal::hci::HalPacket;
using ::bluetooth_hal::transport::TransportType;
using ::bluetooth_hal::util::MockAndroidBaseWrapper;
using ::bluetooth_hal::util::MockSystemCallWrapper;

namespace cfg_consts = ::bluetooth_hal::config::constants;

constexpr int kLoadMiniDrvDelayMs = 100;
constexpr int kLaunchRamDelayMs = 100;

const std::string_view kMultiTransportValidContent = R"({
  "firmware_configs": [
    {
      "transport_type": 1,
      "firmware_folder_name": "/uart/fw/",
      "firmware_file_name": "uart_fw.bin",
      "chip_id": 101,
      "load_mini_drv_delay_ms": 51,
      "launch_ram_delay_ms": 251,
      "firmware_data_loading_type": "PACKET_BY_PACKET",
      "setup_commands": {
        "hci_reset": [1,3,12,0],
        "hci_read_chip_id": [1,121,252,0]
      }
    },
    {
      "transport_type": 100,
      "firmware_folder_name": "/vendor/fw/",
      "firmware_file_name": "vendor_fw.bin",
      "chip_id": 102,
      "load_mini_drv_delay_ms": 71,
      "launch_ram_delay_ms": 301,
      "firmware_data_loading_type": "ACCUMULATED_BUFFER",
      "setup_commands": {
        "hci_update_chip_baud_rate": [1,24,252,6,0,0,0,9,61,0]
      }
    }
  ]
})";

TEST(FirmwareConfigLoaderTest, IsSameSingleton) {
  MockSystemCallWrapper mock_system_call_wrapper;
  MockSystemCallWrapper::SetMockWrapper(&mock_system_call_wrapper);

  MockAndroidBaseWrapper mock_android_base_wrapper;
  MockAndroidBaseWrapper::SetMockWrapper(&mock_android_base_wrapper);

  EXPECT_EQ(&FirmwareConfigLoader::GetLoader(),
            &FirmwareConfigLoader::GetLoader());
}

class FirmwareConfigLoaderTestBase : public Test {
 protected:
  void SetUp() override {
    MockSystemCallWrapper::SetMockWrapper(&mock_system_call_wrapper_);
    MockAndroidBaseWrapper::SetMockWrapper(&mock_android_base_wrapper_);
    MockHalConfigLoader::SetMockLoader(&mock_hal_config_loader_);

    FirmwareConfigLoader::ResetLoader();
  }

  void TearDown() override { MockHalConfigLoader::SetMockLoader(nullptr); }

  MockSystemCallWrapper mock_system_call_wrapper_;
  MockAndroidBaseWrapper mock_android_base_wrapper_;
  StrictMock<MockHalConfigLoader> mock_hal_config_loader_;
};

TEST_F(FirmwareConfigLoaderTestBase, GetNextFirmwareDataOnInit) {
  EXPECT_FALSE(
      FirmwareConfigLoader::GetLoader().GetNextFirmwareData().has_value());
}

TEST_F(FirmwareConfigLoaderTestBase, GetLoadMiniDrvDelayMsOnInit) {
  EXPECT_EQ(FirmwareConfigLoader::GetLoader().GetLoadMiniDrvDelayMs(),
            cfg_consts::kDefaultLoadMiniDrvDelayMs);
}

TEST_F(FirmwareConfigLoaderTestBase, GetLaunchRamDelayMsOnInit) {
  EXPECT_EQ(FirmwareConfigLoader::GetLoader().GetLaunchRamDelayMs(),
            cfg_consts::kDefaultLaunchRamDelayMs);
}

struct SetupCommandValueTestParam {
  SetupCommandType command_type;
  std::vector<uint8_t> expected_command;
};

class LoadConfigSetupCommandTypeTest
    : public FirmwareConfigLoaderTestBase,
      public WithParamInterface<SetupCommandValueTestParam> {
 protected:
  void SetUp() override {
    FirmwareConfigLoaderTestBase::SetUp();

    EXPECT_CALL(mock_hal_config_loader_, GetTransportTypePriority())
        .WillRepeatedly(ReturnRef(empty_transport_priority_list_));
    EXPECT_TRUE(FirmwareConfigLoader::GetLoader().LoadConfigFromString(
        kMultiTransportValidContent));
  }
  const std::vector<TransportType> empty_transport_priority_list_{};
};

TEST_P(LoadConfigSetupCommandTypeTest, ParsesSetupCommandOnInit) {
  const auto& [command_type, expected_command] = GetParam();
  const auto command_packet =
      FirmwareConfigLoader::GetLoader().GetSetupCommandPacket(command_type);

  EXPECT_TRUE(!command_packet.has_value());
}

INSTANTIATE_TEST_SUITE_P(
    TestAllSetupCommandTypeConfig, LoadConfigSetupCommandTypeTest,
    Values(SetupCommandValueTestParam{.command_type = SetupCommandType::kReset,
                                      .expected_command = {}},
           SetupCommandValueTestParam{
               .command_type = SetupCommandType::kReadChipId,
               .expected_command = {}},
           SetupCommandValueTestParam{
               .command_type = SetupCommandType::kUpdateChipBaudRate,
               .expected_command = {}},
           SetupCommandValueTestParam{
               .command_type = SetupCommandType::kSetFastDownload,
               .expected_command = {}},
           SetupCommandValueTestParam{
               .command_type = SetupCommandType::kDownloadMinidrv,
               .expected_command = {}},
           SetupCommandValueTestParam{
               .command_type = SetupCommandType::kLaunchRam,
               .expected_command = {}},
           SetupCommandValueTestParam{
               .command_type = SetupCommandType::kReadFwVersion,
               .expected_command = {}},
           SetupCommandValueTestParam{
               .command_type = SetupCommandType::kSetupLowPowerMode,
               .expected_command = {}},
           SetupCommandValueTestParam{
               .command_type = SetupCommandType::kWriteBdAddress,
               .expected_command = {}}));

class LoadConfigSetupCommandTypeTestVendor
    : public FirmwareConfigLoaderTestBase,
      public WithParamInterface<SetupCommandValueTestParam> {
 protected:
  void SetUp() override {
    FirmwareConfigLoaderTestBase::SetUp();
    std::vector<TransportType> priority_list = {
        static_cast<TransportType>(TransportType::kVendorStart)};
    EXPECT_CALL(mock_hal_config_loader_, GetTransportTypePriority())
        .WillRepeatedly(ReturnRef(priority_list));

    EXPECT_TRUE(FirmwareConfigLoader::GetLoader().LoadConfigFromString(
        kMultiTransportValidContent));
    EXPECT_TRUE(FirmwareConfigLoader::GetLoader().SelectFirmwareConfiguration(
        static_cast<TransportType>(TransportType::kVendorStart)));
  }
};

TEST_P(LoadConfigSetupCommandTypeTestVendor, ParsesSetupCommandForVendor) {
  const auto& [command_type, expected_command] = GetParam();
  const auto command_packet =
      FirmwareConfigLoader::GetLoader().GetSetupCommandPacket(command_type);

  EXPECT_EQ(command_packet.has_value(), !expected_command.empty());
  if (!expected_command.empty()) {
    EXPECT_EQ(command_packet.value().get().GetPayload(), expected_command);
  }
}

INSTANTIATE_TEST_SUITE_P(
    TestVendorSetupCommandTypeConfig, LoadConfigSetupCommandTypeTestVendor,
    Values(SetupCommandValueTestParam{.command_type = SetupCommandType::kReset,
                                      .expected_command = {}},
           SetupCommandValueTestParam{
               .command_type = SetupCommandType::kUpdateChipBaudRate,
               .expected_command = {0x01, 0x18, 0xfc, 0x06, 0x00, 0x00, 0x00,
                                    0x09, 0x3d, 0x00}}));

TEST_F(FirmwareConfigLoaderTestBase, DumpConfigToString) {
  std::vector<TransportType> priority_list = {TransportType::kUartH4};
  EXPECT_CALL(mock_hal_config_loader_, GetTransportTypePriority())
      .WillRepeatedly(ReturnRef(priority_list));
  EXPECT_TRUE(FirmwareConfigLoader::GetLoader().LoadConfigFromString(
      kMultiTransportValidContent));

  std::string dump = FirmwareConfigLoader::GetLoader().DumpConfigToString();
  EXPECT_NE(dump.find("Transport Type: 1"), std::string::npos);
  EXPECT_NE(dump.find("Firmware Folder: \"/uart/fw/\""), std::string::npos);
  EXPECT_NE(dump.find("Active Configuration for Transport Type: 1"),
            std::string::npos);

  EXPECT_NE(dump.find("Transport Type: 100"), std::string::npos);
  EXPECT_NE(dump.find("Firmware Folder: \"/vendor/fw/\""), std::string::npos);

  FirmwareConfigLoader::GetLoader().SelectFirmwareConfiguration(
      static_cast<TransportType>(TransportType::kVendorStart));
  dump = FirmwareConfigLoader::GetLoader().DumpConfigToString();
  EXPECT_NE(dump.find("Active Configuration for Transport Type: 100"),
            std::string::npos);
}

TEST(FirmwarePacketTest, HandleDifferentFirmwarePacketType) {
  std::vector<uint8_t> payload = {0x01};

  for (int i = 0; i <= static_cast<int>(FirmwarePacketType::kData); ++i) {
    FirmwarePacketType packet_type = static_cast<FirmwarePacketType>(i);
    FirmwarePacket packet(packet_type, payload);
    EXPECT_EQ(packet.GetPacketType(), packet_type);
  }
}

TEST(FirmwarePacketTest, HandlePayload) {
  std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
  FirmwarePacket packet(FirmwarePacketType::kData, payload);
  EXPECT_EQ(packet.GetPayload(), payload);
}

TEST(FirmwarePacketTest, HandleEmptyPayload) {
  FirmwarePacket packet(FirmwarePacketType::kData, {});
  EXPECT_TRUE(packet.GetPayload().empty());
}

TEST(SetupCommandPacketTest, HandleDifferentSetupCommandPacketTypes) {
  std::vector<uint8_t> payload = {0x01};

  for (int i = 0; i <= static_cast<int>(SetupCommandType::kWriteBdAddress);
       ++i) {
    SetupCommandType command_type = static_cast<SetupCommandType>(i);
    SetupCommandPacket packet(command_type, payload);
    EXPECT_EQ(packet.GetCommandType(), command_type);
  }
}

TEST(DataPacketTest, HandleDifferentDataPacketTypes) {
  std::vector<uint8_t> payload = {0x01};

  for (int i = 0; i <= static_cast<int>(DataType::kDataEnd); ++i) {
    DataType data_type = static_cast<DataType>(i);
    DataPacket packet(data_type, payload);
    EXPECT_EQ(packet.GetDataType(), data_type);
  }
}

}  // namespace
}  // namespace config
}  // namespace bluetooth_hal
