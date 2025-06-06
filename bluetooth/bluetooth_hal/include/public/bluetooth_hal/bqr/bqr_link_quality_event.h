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

#pragma once

#include <cstdint>

#include "bluetooth_hal/bluetooth_address.h"
#include "bluetooth_hal/bqr/bqr_event.h"
#include "bluetooth_hal/bqr/bqr_types.h"
#include "bluetooth_hal/hal_packet.h"

namespace bluetooth_hal {
namespace bqr {

// Forward declarations
class BqrLinkQualityEventV3AndBackward;
class BqrLinkQualityEventV4;
class BqrLinkQualityEventV5;
class BqrLinkQualityEventV6;

// Base class for all BQR Link Quality events.
// It provides methods to access common parameters across different versions.
class BqrLinkQualityEventBase : public BqrEvent {
 public:
  explicit BqrLinkQualityEventBase(
      const ::bluetooth_hal::hci::HalPacket& packet);
  ~BqrLinkQualityEventBase() = default;

  // Checks if the BQR Link Quality Event is valid.
  // Overrides the base BqrEvent::IsValid to include Link Quality specific
  // checks.
  bool IsValid() const override;

  // Retrieves the Packet Type of the connection.
  uint8_t GetPacketTypes() const;
  // Retrieves the Connection Handle of the connection.
  uint16_t GetConnectionHandle() const;
  // Retrieves the Performing Role for the connection.
  uint8_t GetConnectionRole() const;
  // Retrieves the Current Transmit Power Level for the connection.
  int8_t GetTxPowerLevel() const;
  // Retrieves the Received Signal Strength Indication (RSSI) value for the
  // connection.
  int8_t GetRssi() const;
  // Retrieves the Signal-to-Noise Ratio (SNR) value for the connection.
  uint8_t GetSnr() const;
  // Retrieves the number of unused channels in AFH_channel_map.
  uint8_t GetUnusedAfhChannelCount() const;
  // Retrieves the number of channels which are interfered and quality is bad
  // but are still selected for AFH.
  uint8_t GetAfhSelectUnidealChannelCount() const;
  // Retrieves the Current Link Supervision Timeout Setting in 0.3125 ms units.
  uint16_t GetLsto() const;
  // Retrieves the Piconet Clock for the specified Connection_Handle in 0.3125
  // ms units.
  uint32_t GetConnectionPiconetClock() const;
  // Retrieves the count of retransmissions.
  uint32_t GetRetransmissionCount() const;
  // Retrieves the count of no RX.
  uint32_t GetNoRxCount() const;
  // Retrieves the count of NAK (Negative Acknowledge).
  uint32_t GetNakCount() const;
  // Retrieves the timestamp of the last TX ACK in 0.3125 ms units.
  uint32_t GetLastTxAckTimestamp() const;
  // Retrieves the count of Flow-off (STOP).
  uint32_t GetFlowOffCount() const;
  // Retrieves the timestamp of the last Flow-on (GO) in 0.3125 ms units.
  uint32_t GetLastFlowOnTimestamp() const;
  // Retrieves the buffer overflow count (how many bytes of TX data are dropped)
  // since the last event.
  uint32_t GetBufferOverflowBytes() const;
  // Retrieves the buffer underflow count (in bytes).
  uint32_t GetBufferUnderflowBytes() const;

 protected:
  void ParseData();

  bool is_valid_;
  uint8_t packet_types_;
  uint16_t connection_handle_;
  uint8_t connection_role_;
  int8_t tx_power_level_;
  int8_t rssi_;
  uint8_t snr_;
  uint8_t unused_afh_channel_count_;
  uint8_t afh_select_unideal_channel_count_;
  uint16_t lsto_;
  uint32_t connection_piconet_clock_;
  uint32_t retransmission_count_;
  uint32_t no_rx_count_;
  uint32_t nak_count_;
  uint32_t last_tx_ack_timestamp_;
  uint32_t flow_off_count_;
  uint32_t last_flow_on_timestamp_;
  uint32_t buffer_overflow_bytes_;
  uint32_t buffer_underflow_bytes_;
};

// Represents BQR Link Quality event for versions V1 to V3.
// It inherits all common parameters from BqrLinkQualityEventBase.
class BqrLinkQualityEventV3AndBackward : public BqrLinkQualityEventBase {
 public:
  explicit BqrLinkQualityEventV3AndBackward(
      const ::bluetooth_hal::hci::HalPacket& packet);
  ~BqrLinkQualityEventV3AndBackward() = default;

  // Checks if the BQR Link Quality Event V3 and backward is valid.
  bool IsValid() const override;
};

// Represents BQR Link Quality event for version V4.
// It extends BqrLinkQualityEventV3AndBackward with V4-specific parameters.
class BqrLinkQualityEventV4 : public BqrLinkQualityEventV3AndBackward {
 public:
  explicit BqrLinkQualityEventV4(const ::bluetooth_hal::hci::HalPacket& packet);
  ~BqrLinkQualityEventV4() = default;

  // Checks if the BQR Link Quality Event V4 is valid.
  bool IsValid() const override;

  // Retrieves the number of packets that are sent out.
  uint32_t GetTxTotalPackets() const;
  // Retrieves the number of packets that don't receive an acknowledgment.
  uint32_t GetTxUnackedPackets() const;
  // Retrieves the number of packets that are not sent out by its flush point.
  uint32_t GetTxFlushedPackets() const;
  // Retrieves the number of packets that Link Layer transmits a CIS Data PDU in
  // the last subevent of a CIS event.
  uint32_t GetTxLastSubeventPackets() const;
  // Retrieves the number of received packages with CRC error since the last
  // event.
  uint32_t GetCrcErrorPackets() const;
  // Retrieves the number of duplicate (retransmission) packages that are
  // received since the last event.
  uint32_t GetRxDuplicatePackets() const;

 protected:
  void ParseData();

  uint32_t tx_total_packets_;
  uint32_t tx_unacked_packets_;
  uint32_t tx_flushed_packets_;
  uint32_t tx_last_subevent_packets_;
  uint32_t crc_error_packets_;
  uint32_t rx_duplicate_packets_;
};

// Represents BQR Link Quality event for version V5.
// It extends BqrLinkQualityEventV3AndBackward with V5-specific parameters,
// and redefines the V4-like parameters at new offsets.
class BqrLinkQualityEventV5 : public BqrLinkQualityEventV3AndBackward {
 public:
  explicit BqrLinkQualityEventV5(const ::bluetooth_hal::hci::HalPacket& packet);
  ~BqrLinkQualityEventV5() = default;

  // Checks if the BQR Link Quality Event V5 is valid.
  bool IsValid() const override;

  // Retrieves the remote Bluetooth address.
  ::bluetooth_hal::hci::BluetoothAddress GetRemoteAddress() const;
  // Retrieves the count of calibration failed items.
  uint8_t GetCallFailedItemCount() const;

  // V4-like parameters, but at offsets specific to V5
  uint32_t GetTxTotalPackets() const;
  uint32_t GetTxUnackedPackets() const;
  uint32_t GetTxFlushedPackets() const;
  uint32_t GetTxLastSubeventPackets() const;
  uint32_t GetCrcErrorPackets() const;
  uint32_t GetRxDuplicatePackets() const;

 protected:
  void ParseData();

  ::bluetooth_hal::hci::BluetoothAddress remote_addr_;
  uint8_t call_failed_item_count_;
  uint32_t v5_tx_total_packets_;
  uint32_t v5_tx_unacked_packets_;
  uint32_t v5_tx_flushed_packets_;
  uint32_t v5_tx_last_subevent_packets_;
  uint32_t v5_crc_error_packets_;
  uint32_t v5_rx_duplicate_packets_;
};

// Represents BQR Link Quality event for version V6.
// It extends BqrLinkQualityEventV5 with V6-specific parameters.
class BqrLinkQualityEventV6 : public BqrLinkQualityEventV5 {
 public:
  explicit BqrLinkQualityEventV6(const ::bluetooth_hal::hci::HalPacket& packet);
  ~BqrLinkQualityEventV6() = default;

  // Checks if the BQR Link Quality Event V6 is valid.
  bool IsValid() const override;

  // Retrieves the number of unreceived packets, same as LE Read ISO Link
  // Quality command.
  uint32_t GetRxUnreceivedPackets() const;
  // Retrieves the coex activities information mask.
  uint16_t GetCoexInfoMask() const;

 protected:
  void ParseData();

  uint32_t rx_unreceived_packets_;
  uint16_t coex_info_mask_;
};

}  // namespace bqr
}  // namespace bluetooth_hal
