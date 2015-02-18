/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxUtil.h"
#include "SandboxLogging.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mozilla/Assertions.h"

namespace mozilla {

bool
IsSingleThreaded()
{
  struct stat sb;
  if (stat("/proc/self/task", &sb) < 0) {
    MOZ_DIAGNOSTIC_ASSERT(false, "Couldn't access /proc/self/task!");
    return false;
  }
  MOZ_DIAGNOSTIC_ASSERT(sb.st_nlink >= 3);
  return sb.st_nlink == 3;
}

} // namespace mozilla
