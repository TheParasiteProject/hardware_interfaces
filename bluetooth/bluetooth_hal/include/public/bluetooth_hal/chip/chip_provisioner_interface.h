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

#include <functional>
#include <memory>

#include "bluetooth_hal/hal_types.h"

namespace bluetooth_hal {
namespace chip {

class ChipProvisionerInterface {
 public:
  using FactoryFn = std::function<std::unique_ptr<ChipProvisionerInterface>()>;
  static void RegisterVendorChipProvisioner(FactoryFn factory);

  virtual ~ChipProvisionerInterface() = default;

  virtual void Initialize(const std::function<void(::bluetooth_hal::HalState)>
                              on_hal_state_update) = 0;
  virtual bool DownloadFirmware() = 0;
  virtual bool ResetFirmware() = 0;

  static std::unique_ptr<ChipProvisionerInterface> Create();

 private:
  static FactoryFn vendor_factory_;
};

}  // namespace chip
}  // namespace bluetooth_hal
