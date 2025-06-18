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

#include "bluetooth_hal/bluetooth_address.h"
#include "bluetooth_hal/hal_packet.h"

namespace bluetooth_hal {
namespace bqr {

enum class BqrVersion : uint8_t {
  kNone = 0,
  kV1ToV3 = 3,
  kV4,
  kV5,
  kV6,
  kV7,
};

enum class BqrReportId : uint8_t {
  kNone = 0x00,

  // BqrEventType::kLinkQuality
  kMonitorMode,
  kApproachLsto,
  kA2dpAudioChoppy,
  kScoVoiceChoppy,

  // BqrEventType::kRootInflammation
  kRootInflammation,

  // BqrEventType::kEnergyMonitoring
  kEnergyMonitoring,

  // BqrEventType::kLinkQuality
  kLeAudioChoppy,
  kConnectFail,

  // BqrEventType::kAdvancedRfStat
  kAdvanceRfStats,
  kAdvanceRfStatsPeriodic,

  // BqrEventType::kControllerHealthMonitor
  kControllerHealthMonitor,
  kControllerHealthMonitorPeriodic,

  // BqrEventType::kNone
  kGoogleReservedLowerBound = 0x10,
  kGoogleReservedUpperBound = 0x1F,
};

enum class BqrEventType : uint8_t {
  kNone,
  kLinkQuality,
  kRootInflammation,
  kEnergyMonitoring,
  kAdvancedRfStat,
  kControllerHealthMonitor,
};

inline BqrEventType GetBqrEventTypeFromReportId(BqrReportId id) {
  switch (id) {
    case BqrReportId::kMonitorMode:
    case BqrReportId::kApproachLsto:
    case BqrReportId::kA2dpAudioChoppy:
    case BqrReportId::kScoVoiceChoppy:
    case BqrReportId::kLeAudioChoppy:
    case BqrReportId::kConnectFail:
      return BqrEventType::kLinkQuality;
    case BqrReportId::kRootInflammation:
      return BqrEventType::kRootInflammation;
    case BqrReportId::kEnergyMonitoring:
      return BqrEventType::kEnergyMonitoring;
    case BqrReportId::kAdvanceRfStats:
    case BqrReportId::kAdvanceRfStatsPeriodic:
      return BqrEventType::kAdvancedRfStat;
    case BqrReportId::kControllerHealthMonitor:
    case BqrReportId::kControllerHealthMonitorPeriodic:
      return BqrEventType::kControllerHealthMonitor;
    default:
      return BqrEventType::kNone;
  }
}

inline std::string BqrReportIdToString(BqrReportId id) {
  switch (id) {
    case BqrReportId::kMonitorMode:
      return "Monitoring";
    case BqrReportId::kApproachLsto:
      return "Appro LSTO";
    case BqrReportId::kA2dpAudioChoppy:
      return "A2DP Choppy";
    case BqrReportId::kScoVoiceChoppy:
      return "SCO Choppy";
    case BqrReportId::kRootInflammation:
      return "Root Inflammation";
    case BqrReportId::kEnergyMonitoring:
      return "Energy Monitoring";
    case BqrReportId::kLeAudioChoppy:
      return "LE Audio Choppy";
    case BqrReportId::kConnectFail:
      return "Connect Fail";
    case BqrReportId::kAdvanceRfStats:
      return "Advance RF Stats";
    case BqrReportId::kAdvanceRfStatsPeriodic:
      return "Advance RF Stats Periodic";
    case BqrReportId::kControllerHealthMonitor:
      return "Controller Health Monitor";
    case BqrReportId::kControllerHealthMonitorPeriodic:
      return "Controller Health Monitor Periodic";
    default:
      return "Unknown BQR Report ID";
  }
}

enum BqrPacketType : uint8_t {
  PACKET_TYPE_ID = 0x01,
  PACKET_TYPE_NULL,
  PACKET_TYPE_POLL,
  PACKET_TYPE_FHS,
  PACKET_TYPE_HV1,
  PACKET_TYPE_HV2,
  PACKET_TYPE_HV3,
  PACKET_TYPE_DV,
  PACKET_TYPE_EV3,
  PACKET_TYPE_EV4,
  PACKET_TYPE_EV5,
  PACKET_TYPE_2EV3,
  PACKET_TYPE_2EV5,
  PACKET_TYPE_3EV3,
  PACKET_TYPE_3EV5,
  PACKET_TYPE_DM1,
  PACKET_TYPE_DH1,
  PACKET_TYPE_DM3,
  PACKET_TYPE_DH3,
  PACKET_TYPE_DM5,
  PACKET_TYPE_DH5,
  PACKET_TYPE_AUX1,
  PACKET_TYPE_2DH1,
  PACKET_TYPE_2DH3,
  PACKET_TYPE_2DH5,
  PACKET_TYPE_3DH1,
  PACKET_TYPE_3DH3,
  PACKET_TYPE_3DH5,
  PACKET_TYPE_4DH1 = 0x20,
  PACKET_TYPE_4DH3,
  PACKET_TYPE_4DH5,
  PACKET_TYPE_8DH1,
  PACKET_TYPE_8DH3,
  PACKET_TYPE_8DH5,
  PACKET_TYPE_4EV3,
  PACKET_TYPE_4EV5,
  PACKET_TYPE_8EV3,
  PACKET_TYPE_8EV5,
  PACKET_TYPE_ISO = 0x51,
  PACKET_TYPE_1M_PHY,
  PACKET_TYPE_2M_PHY,
  PACKET_TYPE_CODEC_PHY_S2,
  PACKET_TYPE_CODEC_PHY_S8
};

inline std::string BqrPacketTypeToString(uint8_t packet_type) {
  switch (packet_type) {
    case PACKET_TYPE_ID:
      return "ID";
    case PACKET_TYPE_NULL:
      return "NULL";
    case PACKET_TYPE_POLL:
      return "POLL";
    case PACKET_TYPE_FHS:
      return "FHS";
    case PACKET_TYPE_HV1:
      return "HV1";
    case PACKET_TYPE_HV2:
      return "HV2";
    case PACKET_TYPE_HV3:
      return "HV3";
    case PACKET_TYPE_DV:
      return "DV";
    case PACKET_TYPE_EV3:
      return "EV3";
    case PACKET_TYPE_EV4:
      return "EV4";
    case PACKET_TYPE_EV5:
      return "EV5";
    case PACKET_TYPE_2EV3:
      return "2EV3";
    case PACKET_TYPE_2EV5:
      return "2EV5";
    case PACKET_TYPE_3EV3:
      return "3EV3";
    case PACKET_TYPE_3EV5:
      return "3EV5";
    case PACKET_TYPE_DM1:
      return "DM1";
    case PACKET_TYPE_DH1:
      return "DH1";
    case PACKET_TYPE_DM3:
      return "DM3";
    case PACKET_TYPE_DH3:
      return "DH3";
    case PACKET_TYPE_DM5:
      return "DM5";
    case PACKET_TYPE_DH5:
      return "DH5";
    case PACKET_TYPE_AUX1:
      return "AUX1";
    case PACKET_TYPE_2DH1:
      return "2DH1";
    case PACKET_TYPE_2DH3:
      return "2DH3";
    case PACKET_TYPE_2DH5:
      return "2DH5";
    default:
      return "UnKnown ";
  }
}

}  // namespace bqr
}  // namespace bluetooth_hal
