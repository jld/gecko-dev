/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxLogging.h"

#ifdef ANDROID
#include <android/log.h>
#else
#include <stdio.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

#include "base/posix/eintr_wrapper.h"

namespace mozilla {

void
SandboxLogError(const char* message)
{
#ifdef ANDROID
  __android_log_write(ANDROID_LOG_ERROR, "Sandbox", message);
#else
  static const char logPrefix[] = "Sandbox: ", logSuffix[] = "\n";
  struct iovec iovs[3] = {
    { const_cast<char*>(logPrefix), sizeof(logPrefix) - 1 },
    { const_cast<char*>(message), strlen(message) },
    { const_cast<char*>(logSuffix), sizeof(logSuffix) - 1 },
  };
  HANDLE_EINTR(writev(STDERR_FILENO, iovs, 3));
#endif
}

}
