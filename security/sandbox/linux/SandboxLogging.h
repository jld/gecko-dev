/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SandboxLogging_h
#define mozilla_SandboxLogging_h

#ifdef MOZ_SANDBOX_LOG_UNSAFE

// This is async signal unsafe, but we can't get to Chromium's
// SafeSPrintf everywhere.  See after the #else for the default
// implementation.

#if defined(ANDROID)
#include <android/log.h>
#else
#include <stdio.h>
#endif

#if defined(ANDROID)
#define SANDBOX_LOG_ERROR(args...) __android_log_print(ANDROID_LOG_ERROR, "Sandbox", ## args)
#else
#define SANDBOX_LOG_ERROR(fmt, args...) fprintf(stderr, "Sandbox: " fmt "\n", ## args)
#endif

#else // MOZ_SANDBOX_LOG_UNSAFE

#include "base/strings/safe_sprintf.h"

namespace mozilla {
void SandboxLogError(const char* aMessage);
}

#define SANDBOX_LOG_ERROR(fmt, args...) do {                          \
  char _sandboxLogBuf[256];                                           \
  ::base::strings::SafeSPrintf(_sandboxLogBuf, fmt, ## args);         \
  ::mozilla::SandboxLogError(_sandboxLogBuf);                         \
} while(0)

#endif // MOZ_SANDBOX_LOG_UNSAFE

#endif // mozilla_SandboxLogging_h
