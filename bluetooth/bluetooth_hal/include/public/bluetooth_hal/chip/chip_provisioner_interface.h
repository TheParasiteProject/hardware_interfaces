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

  /**
   * @brief Registers a vendor-specific factory for creating
   * ChipProvisionerInterface instances.
   *
   * If a vendor factory is registered, ChipProvisionerInterface::Create() will
   * use it. Otherwise, a default implementation will be created.
   *
   * @param factory The factory function to register.
   */
  static void RegisterVendorChipProvisioner(FactoryFn factory);

  virtual ~ChipProvisionerInterface() = default;

  /**
   * @brief Initializes the chip provisioner.
   *
   * @param on_hal_state_update A callback function invoked when the HAL state
   * changes.
   */
  virtual void Initialize(const std::function<void(::bluetooth_hal::HalState)>
                              on_hal_state_update) = 0;

  /**
   * @brief Downloads firmware to the Bluetooth chip.
   *
   * @return True if firmware download is successful, false otherwise.
   */
  virtual bool DownloadFirmware() = 0;

  /**
   * @brief Resets the firmware on the Bluetooth chip.
   *
   * @return True if firmware reset is successful, false otherwise.
   */
  virtual bool ResetFirmware() = 0;

  /**
   * @brief Creates an instance of ChipProvisionerInterface.
   *
   * This factory method will use a registered vendor factory if available,
   * otherwise it will create a default implementation.
   *
   * @return A unique_ptr to a ChipProvisionerInterface instance.
   */
  static std::unique_ptr<ChipProvisionerInterface> Create();

 private:
  static FactoryFn vendor_factory_;
};

}  // namespace chip
}  // namespace bluetooth_hal
