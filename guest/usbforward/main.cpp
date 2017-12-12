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
#define LOG_TAG "UsbForward"

#include <cutils/log.h>
#include <stdio.h>

#include "common/libs/fs/shared_fd.h"
#include "guest/usbforward/usb_server.h"

int main(int argc, char* argv[]) {
  if (argc == 1) {
    printf("Usage: %s <virtio_channel>\n", argv[0]);
    return 1;
  }

  avd::SharedFD fd =
      avd::SharedFD::Open(argv[1], O_RDWR | O_NOCTTY);
  if (!fd->IsOpen()) {
    ALOGE("Could not open %s: %s", argv[1], fd->StrError());
    return 1;
  }

  USBServer server(fd);
  if (server.Init()) {
    server.Serve();
  }
  ALOGE("Terminated.");
  return 1;
}
