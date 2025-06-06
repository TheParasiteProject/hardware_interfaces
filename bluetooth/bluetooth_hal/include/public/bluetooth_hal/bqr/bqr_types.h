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

#include "bluetooth_hal/bluetooth_address.h"
#include "bluetooth_hal/hal_packet.h"

namespace bluetooth_hal {
namespace bqr {

enum class BqrVersion : uint8_t {
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

}  // namespace bqr
}  // namespace bluetooth_hal
