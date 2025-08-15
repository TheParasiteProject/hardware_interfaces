/*
 * Copyright 2024 The Android Open Source Project
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

#include "bluetooth_hal/test/mock/mock_transport_interface.h"

#include "bluetooth_hal/hal_types.h"
#include "bluetooth_hal/transport/transport_interface.h"

namespace bluetooth_hal {
namespace transport {

using ::bluetooth_hal::HalState;

TransportInterface& TransportInterface::GetTransport() {
  return *MockTransportInterface::mock_transport_interface_;
}

bool TransportInterface::UpdateTransportType(TransportType requested_type) {
  return MockTransportInterface::mock_transport_interface_->UpdateTransportType(
      requested_type);
}

TransportType TransportInterface::GetTransportType() {
  return MockTransportInterface::mock_transport_interface_->GetTransportType();
}

void TransportInterface::CleanupTransport() {
  MockTransportInterface::mock_transport_interface_->CleanupTransport();
}

bool TransportInterface::RegisterVendorTransport(TransportType type,
                                                 FactoryFn factory) {
  return MockTransportInterface::mock_transport_interface_
      ->RegisterVendorTransport(type, std::move(factory));
}

bool TransportInterface::UnregisterVendorTransport(TransportType type) {
  return MockTransportInterface::mock_transport_interface_
      ->UnregisterVendorTransport(type);
}

void TransportInterface::SetHciRouterBusy(bool is_busy) {
  MockTransportInterface::mock_transport_interface_->SetHciRouterBusy(is_busy);
}

void TransportInterface::NotifyHalStateChange(HalState hal_state) {
  MockTransportInterface::mock_transport_interface_->NotifyHalStateChange(
      hal_state);
}

void TransportInterface::Subscribe(Subscriber& subscriber) {
  MockTransportInterface::mock_transport_interface_->Subscribe(subscriber);
}

void TransportInterface::Unsubscribe(Subscriber& subscriber) {
  MockTransportInterface::mock_transport_interface_->Unsubscribe(subscriber);
}

void MockTransportInterface::SetMockTransport(
    MockTransportInterface* transport) {
  MockTransportInterface::mock_transport_interface_ = transport;
}

}  // namespace transport
}  // namespace bluetooth_hal
