/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <map>
#include <memory>
#include <string>
#include <libusb/libusb.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/threads/pthread.h"
#include "guest/usbforward/transport_request.h"

namespace usb_forward {

// USBServer exposes access to USB devices over pipe (virtio channel etc).
// Usage:
//
//     avd::SharedFD pipe = avd::SharedFD::Open(pipe_path, O_RDWR);
//     USBServer server(pipe);
//     CHECK(server.Init());
//     server.Serve();
class USBServer final {
 public:
  USBServer(const avd::SharedFD& fd);
  ~USBServer() = default;

  // Serve incoming USB requests.
  void Serve();

 private:
  // HandleDeviceEvent opens and closes Android Gadget device, whenever it
  // appears / disappears.
  static int HandleDeviceEvent(libusb_context*, libusb_device*,
                                libusb_hotplug_event event, void* self_raw);

  // Handle CmdDeviceList request.
  void HandleDeviceList(uint32_t tag);

  // Handle CmdAttach request.
  void HandleAttach(uint32_t tag);

  // Handle CmdControlTransfer request.
  void HandleControlTransfer(uint32_t tag);

  // Handle CmdDataTransfer request.
  void HandleDataTransfer(uint32_t tag);

  // Handle CmdHeartbeat request.
  void HandleHeartbeat(uint32_t tag);

  // OnAsyncDataTransferComplete handles end of asynchronous data transfer cycle
  // and sends response back to caller.
  void OnTransferComplete(uint32_t tag, bool is_data_in, bool is_success,
                          const uint8_t* buffer, int32_t actual_length);

  std::shared_ptr<libusb_device_handle> handle_;

  avd::ScopedThread libusb_thread_;
  avd::Mutex write_mutex_;
  avd::SharedFD fd_;

  avd::Mutex requests_mutex_;
  std::map<uint32_t, std::unique_ptr<TransportRequest>> requests_in_flight_;

  USBServer(const USBServer& other) = delete;
  USBServer& operator=(const USBServer& other) = delete;
};

}  // namespace usb_forward
