/*
 * Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bluetooth_hal/bqr/bqr_link_quality_event.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

#include "android-base/stringprintf.h"
#include "bluetooth_hal/bqr/bqr_event.h"
#include "bluetooth_hal/bqr/bqr_types.h"
#include "bluetooth_hal/hal_packet.h"

namespace bluetooth_hal {
namespace bqr {
namespace {

using ::bluetooth_hal::hci::BluetoothAddress;
using ::bluetooth_hal::hci::HalPacket;

// Offsets relative to the start of the BQR event payload
// (after the common BQR event header: H4 type, event code, length, sub-event,
// report id)
enum class LinkQualityOffset : uint8_t {
  // After H4 type(1) + event code(1) + length(1) + sub event(1) + report id(1)
  kPacketTypes = 5,                    // 1 byte
  kConnectionHandle = 6,               // 2 bytes
  kConnectionRole = 8,                 // 1 byte
  kTxPowerLevel = 9,                   // 1 byte
  kRssi = 10,                          // 1 byte
  kSnr = 11,                           // 1 byte
  kUnusedAfhChannelCount = 12,         // 1 byte
  kAfhSelectUnidealChannelCount = 13,  // 1 byte
  kLsto = 14,                          // 2 byte
  kConnectionPiconetClock = 16,        // 4 bytes
  kRetransmissionCount = 20,           // 4 bytes
  kNoRxCount = 24,                     // 4 bytes
  kNakCount = 28,                      // 4 bytes
  kLastTxAckTimestamp = 32,            // 4 bytes
  kFlowOffCount = 36,                  // 4 bytes
  kLastFlowOnTimestamp = 40,           // 4 bytes
  kBufferOverflowBytes = 44,           // 4 bytes
  kBufferUnderflowBytes = 48,          // 4 bytes
  kEnd = 52,
};

// V4 Specific Offsets, starting immediately after V3AndBackward fields
enum class LinkQualityOffsetV4 : uint8_t {
  kTxTotalPackets = static_cast<uint8_t>(LinkQualityOffset::kEnd),
  kTxUnackedPackets = 56,       // 4 bytes
  kTxFlushedPackets = 60,       // 4 bytes
  kTxLastSubeventPackets = 64,  // 4 bytes
  kCrcErrorPackets = 68,        // 4 bytes
  kRxDuplicatePackets = 72,     // 4 bytes
  kEnd = 76,
};

// V5 Specific Offsets, starting immediately after V3AndBackward fields
enum class LinkQualityOffsetV5 : uint8_t {
  kRemoteAddr = static_cast<uint8_t>(LinkQualityOffset::kEnd),  // 6 bytes
  kCallFailedItemCount = 58,                                    // 1 bytes
  // V4-like parameters, but at new offsets unique to V5 structure
  kTxTotalPackets = 59,         // 4 byte
  kTxUnackedPackets = 63,       // 4 bytes
  kTxFlushedPackets = 67,       // 4 bytes
  kTxLastSubeventPackets = 71,  // 4 bytes
  kCrcErrorPackets = 75,        // 4 bytes
  kRxDuplicatePackets = 79,     // 4 bytes
  kEnd = 83,
};

// V6 Specific Offsets, starting immediately after V5 fields
enum class LinkQualityOffsetV6 : uint8_t {
  kRxUnreceivedPackets =
      static_cast<uint8_t>(LinkQualityOffsetV5::kEnd),  // 4 bytes
  kCoexInfoMask = 87,                                   // 2 bytes
  kEnd = 89,
};

// Minimum expected sizes for each version, including the common BQR event
// header
constexpr size_t kLinkQualityEventV3AndBackwardMinSize =
    static_cast<size_t>(LinkQualityOffset::kEnd);
constexpr size_t kLinkQualityEventV4MinSize =
    static_cast<size_t>(LinkQualityOffsetV4::kEnd);
constexpr size_t kLinkQualityEventV5MinSize =
    static_cast<size_t>(LinkQualityOffsetV5::kEnd);
constexpr size_t kLinkQualityEventV6MinSize =
    static_cast<size_t>(LinkQualityOffsetV6::kEnd);

}  // namespace

// BqrLinkQualityEventBase
BqrLinkQualityEventBase::BqrLinkQualityEventBase(const HalPacket& packet)
    : BqrEvent(packet),
      is_valid_(BqrEvent::IsValid() &&
                GetBqrEventType() == BqrEventType::kLinkQuality &&
                size() >= kLinkQualityEventV3AndBackwardMinSize),
      packet_types_(0),
      connection_handle_(0),
      connection_role_(0),
      tx_power_level_(0),
      rssi_(0),
      snr_(0),
      unused_afh_channel_count_(0),
      afh_select_unideal_channel_count_(0),
      lsto_(0),
      connection_piconet_clock_(0),
      retransmission_count_(0),
      no_rx_count_(0),
      nak_count_(0),
      last_tx_ack_timestamp_(0),
      flow_off_count_(0),
      last_flow_on_timestamp_(0),
      buffer_overflow_bytes_(0),
      buffer_underflow_bytes_(0) {
  ParseData();
}

void BqrLinkQualityEventBase::ParseData() {
  if (is_valid_) {
    packet_types_ = At(LinkQualityOffset::kPacketTypes);
    connection_handle_ =
        AtUint16LittleEndian(LinkQualityOffset::kConnectionHandle);
    connection_role_ = At(LinkQualityOffset::kConnectionRole);
    tx_power_level_ = static_cast<int8_t>(At(LinkQualityOffset::kTxPowerLevel));
    rssi_ = static_cast<int8_t>(At(LinkQualityOffset::kRssi));
    snr_ = At(LinkQualityOffset::kSnr);
    unused_afh_channel_count_ = At(LinkQualityOffset::kUnusedAfhChannelCount);
    afh_select_unideal_channel_count_ =
        At(LinkQualityOffset::kAfhSelectUnidealChannelCount);
    lsto_ = AtUint16LittleEndian(LinkQualityOffset::kLsto);
    connection_piconet_clock_ =
        AtUint32LittleEndian(LinkQualityOffset::kConnectionPiconetClock);
    retransmission_count_ =
        AtUint32LittleEndian(LinkQualityOffset::kRetransmissionCount);
    no_rx_count_ = AtUint32LittleEndian(LinkQualityOffset::kNoRxCount);
    nak_count_ = AtUint32LittleEndian(LinkQualityOffset::kNakCount);
    last_tx_ack_timestamp_ =
        AtUint32LittleEndian(LinkQualityOffset::kLastTxAckTimestamp);
    flow_off_count_ = AtUint32LittleEndian(LinkQualityOffset::kFlowOffCount);
    last_flow_on_timestamp_ =
        AtUint32LittleEndian(LinkQualityOffset::kLastFlowOnTimestamp);
    buffer_overflow_bytes_ =
        AtUint32LittleEndian(LinkQualityOffset::kBufferOverflowBytes);
    buffer_underflow_bytes_ =
        AtUint32LittleEndian(LinkQualityOffset::kBufferUnderflowBytes);
  }
}
bool BqrLinkQualityEventBase::IsValid() const { return is_valid_; }

uint8_t BqrLinkQualityEventBase::GetPacketTypes() const {
  return packet_types_;
}

uint16_t BqrLinkQualityEventBase::GetConnectionHandle() const {
  return connection_handle_;
}

uint8_t BqrLinkQualityEventBase::GetConnectionRole() const {
  return connection_role_;
}

int8_t BqrLinkQualityEventBase::GetTxPowerLevel() const {
  return tx_power_level_;
}

int8_t BqrLinkQualityEventBase::GetRssi() const { return rssi_; }

uint8_t BqrLinkQualityEventBase::GetSnr() const { return snr_; }

uint8_t BqrLinkQualityEventBase::GetUnusedAfhChannelCount() const {
  return unused_afh_channel_count_;
}

uint8_t BqrLinkQualityEventBase::GetAfhSelectUnidealChannelCount() const {
  return afh_select_unideal_channel_count_;
}

uint16_t BqrLinkQualityEventBase::GetLsto() const { return lsto_; }

uint32_t BqrLinkQualityEventBase::GetConnectionPiconetClock() const {
  return connection_piconet_clock_;
}

uint32_t BqrLinkQualityEventBase::GetRetransmissionCount() const {
  return retransmission_count_;
}

uint32_t BqrLinkQualityEventBase::GetNoRxCount() const { return no_rx_count_; }

uint32_t BqrLinkQualityEventBase::GetNakCount() const { return nak_count_; }

uint32_t BqrLinkQualityEventBase::GetLastTxAckTimestamp() const {
  return last_tx_ack_timestamp_;
}

uint32_t BqrLinkQualityEventBase::GetFlowOffCount() const {
  return flow_off_count_;
}

uint32_t BqrLinkQualityEventBase::GetLastFlowOnTimestamp() const {
  return last_flow_on_timestamp_;
}

uint32_t BqrLinkQualityEventBase::GetBufferOverflowBytes() const {
  return buffer_overflow_bytes_;
}

uint32_t BqrLinkQualityEventBase::GetBufferUnderflowBytes() const {
  return buffer_underflow_bytes_;
}

BqrVersion BqrLinkQualityEventBase::GetVersion() const { return version_; }

std::string BqrLinkQualityEventBase::ToString() const {
  if (!is_valid_) {
    return "BqrLinkQualityEvent(Invalid)";
  }
  return "BqrLinkQualityEvent: " + ToBqrString();
}

std::string BqrLinkQualityEventBase::ToBqrString() const {
  std::stringstream ss;
  ss << BqrEvent::ToBqrString() << ", Handle: 0x" << std::hex << std::setw(4)
     << std::setfill('0') << connection_handle_ << ", "
     << BqrPacketTypeToString(packet_types_)
     << (connection_role_ ? ", Peripheral" : ", Central")
     << ", PwLv: " << std::dec << static_cast<int>(tx_power_level_)
     << ", RSSI: " << std::dec << static_cast<int>(rssi_)
     << ", SNR: " << std::dec << static_cast<int>(snr_)
     << ", UnusedCh: " << std::dec
     << static_cast<int>(unused_afh_channel_count_)
     << ", UnidealCh: " << std::dec
     << static_cast<int>(afh_select_unideal_channel_count_)
     << ", LSTO: " << std::dec << lsto_
     << ", Connection Piconet Clock: " << std::dec << connection_piconet_clock_
     << ", ReTx: " << std::dec << retransmission_count_
     << ", NoRx: " << std::dec << no_rx_count_ << ", NAK: " << std::dec
     << nak_count_ << ", LastTX: " << std::dec << last_tx_ack_timestamp_
     << ", FlowOff: " << std::dec << flow_off_count_
     << ", LastFlowOn: " << std::dec << last_flow_on_timestamp_
     << ", Overflow: " << std::dec << buffer_overflow_bytes_
     << ", Underflow: " << std::dec << buffer_underflow_bytes_;
  return ss.str();
}

// BqrLinkQualityEventV3AndBackward
BqrLinkQualityEventV3AndBackward::BqrLinkQualityEventV3AndBackward(
    const HalPacket& packet)
    : BqrLinkQualityEventBase(packet) {
  if (is_valid_) {
    version_ = BqrVersion::kV1ToV3;
  }
}

bool BqrLinkQualityEventV3AndBackward::IsValid() const { return is_valid_; }

std::string BqrLinkQualityEventV3AndBackward::ToString() const {
  if (!is_valid_) {
    return "BqrLinkQualityEventV3AndBackward(Invalid)";
  }
  return "BqrLinkQualityEventV3AndBackward: " + ToBqrString();
}

std::string BqrLinkQualityEventV3AndBackward::ToBqrString() const {
  return BqrLinkQualityEventBase::ToBqrString();
}

// BqrLinkQualityEventV4
BqrLinkQualityEventV4::BqrLinkQualityEventV4(const HalPacket& packet)
    : BqrLinkQualityEventV3AndBackward(packet),
      tx_total_packets_(0),
      tx_unacked_packets_(0),
      tx_flushed_packets_(0),
      tx_last_subevent_packets_(0),
      crc_error_packets_(0),
      rx_duplicate_packets_(0) {
  is_valid_ = BqrLinkQualityEventV3AndBackward::IsValid() &&
              size() >= kLinkQualityEventV4MinSize;
  ParseData();
}

void BqrLinkQualityEventV4::ParseData() {
  if (is_valid_) {
    version_ = BqrVersion::kV4;
    tx_total_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV4::kTxTotalPackets);
    tx_unacked_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV4::kTxUnackedPackets);
    tx_flushed_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV4::kTxFlushedPackets);
    tx_last_subevent_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV4::kTxLastSubeventPackets);
    crc_error_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV4::kCrcErrorPackets);
    rx_duplicate_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV4::kRxDuplicatePackets);
  }
}

bool BqrLinkQualityEventV4::IsValid() const { return is_valid_; }

uint32_t BqrLinkQualityEventV4::GetTxTotalPackets() const {
  return tx_total_packets_;
}

uint32_t BqrLinkQualityEventV4::GetTxUnackedPackets() const {
  return tx_unacked_packets_;
}

uint32_t BqrLinkQualityEventV4::GetTxFlushedPackets() const {
  return tx_flushed_packets_;
}

uint32_t BqrLinkQualityEventV4::GetTxLastSubeventPackets() const {
  return tx_last_subevent_packets_;
}

uint32_t BqrLinkQualityEventV4::GetCrcErrorPackets() const {
  return crc_error_packets_;
}

uint32_t BqrLinkQualityEventV4::GetRxDuplicatePackets() const {
  return rx_duplicate_packets_;
}

std::string BqrLinkQualityEventV4::ToString() const {
  if (!is_valid_) {
    return "BqrLinkQualityEventV4(Invalid)";
  }
  return "BqrLinkQualityEventV4: " + ToBqrString();
}

std::string BqrLinkQualityEventV4::ToBqrString() const {
  std::stringstream ss;
  ss << BqrLinkQualityEventV3AndBackward::ToBqrString()
     << ", TxTotal: " << std::dec << tx_total_packets_
     << ", TxUnAcked: " << std::dec << tx_unacked_packets_
     << ", TxFlushed: " << std::dec << tx_flushed_packets_
     << ", TxLastSubEvent: " << std::dec << tx_last_subevent_packets_
     << ", CRCError: " << std::dec << crc_error_packets_
     << ", RxDuplicate: " << std::dec << rx_duplicate_packets_;
  return ss.str();
}

// BqrLinkQualityEventV5
BqrLinkQualityEventV5::BqrLinkQualityEventV5(const HalPacket& packet)
    : BqrLinkQualityEventV3AndBackward(packet),
      remote_addr_(),
      call_failed_item_count_(0),
      v5_tx_total_packets_(0),
      v5_tx_unacked_packets_(0),
      v5_tx_flushed_packets_(0),
      v5_tx_last_subevent_packets_(0),
      v5_crc_error_packets_(0),
      v5_rx_duplicate_packets_(0) {
  is_valid_ = BqrLinkQualityEventV3AndBackward::IsValid() &&
              size() >= kLinkQualityEventV5MinSize;
  ParseData();
}

void BqrLinkQualityEventV5::ParseData() {
  if (is_valid_) {
    version_ = BqrVersion::kV5;
    // Read each of the 6 bytes directly from the packet, little-endian.
    std::reverse_copy(
        begin() + static_cast<uint8_t>(LinkQualityOffsetV5::kRemoteAddr),
        begin() + static_cast<uint8_t>(LinkQualityOffsetV5::kRemoteAddr) +
            remote_addr_.size(),
        remote_addr_.begin());
    call_failed_item_count_ = At(LinkQualityOffsetV5::kCallFailedItemCount);
    v5_tx_total_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV5::kTxTotalPackets);
    v5_tx_unacked_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV5::kTxUnackedPackets);
    v5_tx_flushed_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV5::kTxFlushedPackets);
    v5_tx_last_subevent_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV5::kTxLastSubeventPackets);
    v5_crc_error_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV5::kCrcErrorPackets);
    v5_rx_duplicate_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV5::kRxDuplicatePackets);
  }
}

bool BqrLinkQualityEventV5::IsValid() const { return is_valid_; }

BluetoothAddress BqrLinkQualityEventV5::GetRemoteAddress() const {
  return remote_addr_;
}

uint8_t BqrLinkQualityEventV5::GetCallFailedItemCount() const {
  return call_failed_item_count_;
}

// Implement V4-like getters for V5, using V5-specific offsets
uint32_t BqrLinkQualityEventV5::GetTxTotalPackets() const {
  return v5_tx_total_packets_;
}

uint32_t BqrLinkQualityEventV5::GetTxUnackedPackets() const {
  return v5_tx_unacked_packets_;
}

uint32_t BqrLinkQualityEventV5::GetTxFlushedPackets() const {
  return v5_tx_flushed_packets_;
}

uint32_t BqrLinkQualityEventV5::GetTxLastSubeventPackets() const {
  return v5_tx_last_subevent_packets_;
}

uint32_t BqrLinkQualityEventV5::GetCrcErrorPackets() const {
  return v5_crc_error_packets_;
}

uint32_t BqrLinkQualityEventV5::GetRxDuplicatePackets() const {
  return v5_rx_duplicate_packets_;
}

std::string BqrLinkQualityEventV5::ToString() const {
  if (!is_valid_) {
    return "BqrLinkQualityEventV5(Invalid)";
  }
  return "BqrLinkQualityEventV5: " + ToBqrString();
}

std::string BqrLinkQualityEventV5::ToBqrString() const {
  std::stringstream ss;
  ss << BqrLinkQualityEventV3AndBackward::ToBqrString()
     << ", Address: " << remote_addr_.ToString()
     << ", FailedCount: " << std::dec
     << static_cast<int>(call_failed_item_count_) << ", TxTotal: " << std::dec
     << v5_tx_total_packets_ << ", TxUnAcked: " << std::dec
     << v5_tx_unacked_packets_ << ", TxFlushed: " << std::dec
     << v5_tx_flushed_packets_ << ", TxLastSubEvent: " << std::dec
     << v5_tx_last_subevent_packets_ << ", CRCError: " << std::dec
     << v5_crc_error_packets_ << ", RxDuplicate: " << std::dec
     << v5_rx_duplicate_packets_;
  return ss.str();
}

// BqrLinkQualityEventV6
BqrLinkQualityEventV6::BqrLinkQualityEventV6(const HalPacket& packet)
    : BqrLinkQualityEventV5(packet),
      rx_unreceived_packets_(0),
      coex_info_mask_(0) {
  is_valid_ =
      BqrLinkQualityEventV5::IsValid() && size() >= kLinkQualityEventV6MinSize;
  ParseData();
}

void BqrLinkQualityEventV6::ParseData() {
  if (is_valid_) {
    version_ = BqrVersion::kV6;
    rx_unreceived_packets_ =
        AtUint32LittleEndian(LinkQualityOffsetV6::kRxUnreceivedPackets);
    coex_info_mask_ = AtUint16LittleEndian(LinkQualityOffsetV6::kCoexInfoMask);
  }
}

bool BqrLinkQualityEventV6::IsValid() const { return is_valid_; }

uint32_t BqrLinkQualityEventV6::GetRxUnreceivedPackets() const {
  return rx_unreceived_packets_;
}

uint16_t BqrLinkQualityEventV6::GetCoexInfoMask() const {
  return coex_info_mask_;
}

std::string BqrLinkQualityEventV6::ToString() const {
  if (!is_valid_) {
    return "BqrLinkQualityEventV6(Invalid)";
  }
  return "BqrLinkQualityEventV6: " + ToBqrString();
}

std::string BqrLinkQualityEventV6::ToBqrString() const {
  std::stringstream ss;
  ss << BqrLinkQualityEventV5::ToBqrString() << ", RxUnreceived: " << std::dec
     << rx_unreceived_packets_ << ", CoexInfoMask: 0x" << std::hex
     << std::setw(4) << std::setfill('0') << coex_info_mask_;
  return ss.str();
}
}  // namespace bqr
}  // namespace bluetooth_hal
