/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
// Copyright (c) 2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/rand_util.h"

#include <fcntl.h>
#include <unistd.h>

#include "base/logging.h"

namespace base {

static bool ReadFromFD(int fd, char* buffer, size_t bytes) {
  size_t total_read = 0;
  while (total_read < bytes) {
    ssize_t bytes_read =
        HANDLE_EINTR(read(fd, buffer + total_read, bytes - total_read));
    if (bytes_read <= 0)
      break;
    total_read += bytes_read;
  }
  return total_read == bytes;
}

uint64_t RandUint64() {
  uint64_t number;

  int urandom_fd = open("/dev/urandom", O_RDONLY);
  CHECK(urandom_fd >= 0);
  bool success = ReadFromFD(urandom_fd,
                            reinterpret_cast<char*>(&number),
                            sizeof(number));
  CHECK(success);
  close(urandom_fd);

  return number;
}

}  // namespace base
