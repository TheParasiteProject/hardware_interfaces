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

#define LOG_TAG "bluetooth_hal.transport.hci_packet_rescuer"

#include "bluetooth_hal/transport/hci_packet_rescuer.h"

#include <cstddef>
#include <cstdint>
#include <span>

#include "bluetooth_hal/debug/bluetooth_activities.h"
#include "bluetooth_hal/hal_types.h"

namespace bluetooth_hal {
namespace transport {
namespace {

using ::bluetooth_hal::debug::BluetoothActivities;
using ::bluetooth_hal::hci::EventCode;
using ::bluetooth_hal::hci::HciConstants;
using ::bluetooth_hal::hci::HciPacketType;

constexpr size_t kAclPacketRequiredLength = 3;
constexpr size_t kThreadPacketRequiredLength = 6;

bool IsProbablyValidAclPacket(size_t offset, std::span<const uint8_t> data) {
  // ACL Packet Rule: Check if handle connected.
  // byte 0   : ACL Packet Type (0x02).
  // byte 1, 2: Connection Handle.
  if (offset + kAclPacketRequiredLength > data.size()) {
    return false;
  }
  uint16_t connect_handle =
      data[offset + 1] + ((data[offset + 2] << 8u) & 0x0F00);
  return BluetoothActivities::Get().IsConnected(connect_handle);
}

bool IsProbablyValidThreadPacket(size_t offset, std::span<const uint8_t> data) {
  // Thread Packet Rule: Check values in the below bytes.
  // byte 1, 2: Fixed value (0x00)
  // byte 5   : Value in range [0x80, 0x8f]
  if (offset + kThreadPacketRequiredLength > data.size()) {
    return false;
  }
  return (data[offset + 1] == 0x00) && (data[offset + 2] == 0x00) &&
         ((data[offset + 5] & 0xF0) == 0x80);
}

/**
 * @brief Checks if a given offset in a byte stream marks the beginning of a
 * valid HCI packet.
 *
 * This function validates the potential packet start by examining the packet
 * type indicator and performing type-specific checks. For some types (e.g.,
 * ACL), it performs semantic validation against the current system state, while
 * for others it performs syntactic checks on the packet's preamble.
 *
 * @param offset The starting byte offset within `data` to check.
 * @param data A span representing the raw byte stream.
 * @return true if the offset points to a valid and recognized packet start,
 * otherwise false.
 */
bool IsValidPacketStart(size_t offset, std::span<const uint8_t> data) {
  const size_t length = data.size() - offset;
  if (length <= 0) {
    return false;
  }

  const HciPacketType hci_packet_type =
      static_cast<HciPacketType>(data[offset]);
  switch (hci_packet_type) {
    case HciPacketType::kAclData: {
      return IsProbablyValidAclPacket(offset, data);
    }
    case HciPacketType::kThreadData: {
      return IsProbablyValidThreadPacket(offset, data);
    }
    case HciPacketType::kEvent: {
      // Event Packet Rule:
      // byte 0: Event Packet Type (0x04)
      // byte 1: Event Code
      if (length <= HciConstants::kHciEventCodeOffset) {
        return false;
      }
      const EventCode event_code = static_cast<EventCode>(
          data[offset + HciConstants::kHciEventCodeOffset]);
      switch (event_code) {
        case EventCode::kCommandComplete:
        case EventCode::kCommandStatus:
        case EventCode::kBleMeta:
        case EventCode::kNumberOfCompletedPackets:
        case EventCode::kVendorSpecific:
          return true;
        default:
          return false;
      }
    }
    default:
      return false;
  }
}

}  // namespace

size_t FindValidPacketOffset(std::span<const uint8_t> data) {
  const size_t length = data.size();
  for (size_t offset = 0; offset < length; ++offset) {
    if (IsValidPacketStart(offset, data)) {
      return offset;
    }
  }
  return length;
}

}  // namespace transport
}  // namespace bluetooth_hal
