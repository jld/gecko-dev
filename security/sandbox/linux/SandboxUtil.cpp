/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxUtil.h"

#include "LinuxCapabilities.h"
#include "LinuxSched.h"
#include "SandboxInternal.h"
#include "SandboxLogging.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "base/strings/safe_sprintf.h"
#include "mozilla/Assertions.h"
#include "mozilla/unused.h"
#include "sandbox/linux/services/linux_syscalls.h"

namespace mozilla {

using base::strings::SafeSPrintf;

bool
IsSingleThreaded()
{
  // This detects the thread count indirectly.  /proc/<pid>/task has a
  // subdirectory for each thread in <pid>'s thread group, and the
  // link count on the "task" directory follows Unix expectations: the
  // link from its parent, the "." link from itself, and the ".." link
  // from each subdirectory; thus, 2+N links for N threads.
  struct stat sb;
  if (stat("/proc/self/task", &sb) < 0) {
    MOZ_DIAGNOSTIC_ASSERT(false, "Couldn't access /proc/self/task!");
    return false;
  }
  MOZ_DIAGNOSTIC_ASSERT(sb.st_nlink >= 3);
  return sb.st_nlink == 3;
}

static bool
WriteStringToFile(const char* aPath, const char* aStr, const size_t aLen)
{
  int fd = open(aPath, O_WRONLY);
  if (fd < 0) {
    return false;
  }
  ssize_t written = write(fd, aStr, aLen);
  if (close(fd) != 0 || written != ssize_t(aLen)) {
    return false;
  }
  return true;
}

bool
UnshareUserNamespace()
{
  // The uid and gid need to be retrieved before the unshare; see
  // below.
  uid_t uid = getuid();
  gid_t gid = getgid();

  if (syscall(__NR_unshare, CLONE_NEWUSER) != 0) {
    return false;
  }

  SetUpUserNamespace(uid, gid);
  return true;
}


void
SetUpUserNamespace(uid_t aUid, gid_t aGid)
{
  // As mentioned in the header, this function sets up uid/gid
  // mappings that preserve the process's previous ids.  Mapping the
  // uid/gid to something is necessary in order to nest user
  // namespaces (needed for pid namespace support), and leaving the
  // ids unchanged is the least confusing option.
  //
  // In recent kernels (3.19, 3.18.2, 3.17.8), for security reasons,
  // establishing gid mappings will fail unless the process first
  // revokes its ability to call setgroups() by using a /proc node
  // added in the same set of patches.
  //
  // Note that /proc/self points to the thread group leader, not the
  // current thread.  However, CLONE_NEWUSER can be unshared only in a
  // single-threaded process, so those are equivalent if we reach this
  // point.
  char buf[80];
  size_t len;

  len = static_cast<size_t>(SafeSPrintf(buf, "%d %d 1\n", aUid, aUid));
  MOZ_ASSERT(len < sizeof(buf));
  if (!WriteStringToFile("/proc/self/uid_map", buf, len)) {
    MOZ_CRASH("Failed to write /proc/self/uid_map");
  }

  unused << WriteStringToFile("/proc/self/setgroups", "deny", 4);

  len = static_cast<size_t>(SafeSPrintf(buf, "%d %d 1\n", aGid, aGid));
  MOZ_ASSERT(len < sizeof(buf));
  if (!WriteStringToFile("/proc/self/gid_map", buf, len)) {
    MOZ_CRASH("Failed to write /proc/self/gid_map");
  }
}

} // namespace mozilla
