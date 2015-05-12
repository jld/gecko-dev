/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxChroot.h"

#include "SandboxLogging.h"
#include "LinuxCapabilities.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#include "mozilla/Assertions.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/NullPtr.h"
#include "sandbox/linux/services/linux_syscalls.h"

#define MOZ_ALWAYS_ZERO(e) MOZ_ALWAYS_TRUE((e) == 0)

namespace mozilla {

SandboxChroot::SandboxChroot()
{
  pthread_mutexattr_t attr;
  MOZ_ALWAYS_ZERO(pthread_mutexattr_init(&attr));
  MOZ_ALWAYS_ZERO(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
  MOZ_ALWAYS_ZERO(pthread_mutex_init(&mMutex, &attr));
  MOZ_ALWAYS_ZERO(pthread_cond_init(&mWakeup, nullptr));
  mCommand = NO_THREAD;
}

SandboxChroot::~SandboxChroot()
{
  SendCommand(JUST_EXIT);
  MOZ_ALWAYS_ZERO(pthread_mutex_destroy(&mMutex));
  MOZ_ALWAYS_ZERO(pthread_cond_destroy(&mWakeup));
}

bool
SandboxChroot::SendCommand(Command aComm)
{
  MOZ_ALWAYS_ZERO(pthread_mutex_lock(&mMutex));
  if (mCommand == NO_THREAD) {
    MOZ_ALWAYS_ZERO(pthread_mutex_unlock(&mMutex));
    return false;
  } else {
    MOZ_ASSERT(mCommand == NO_COMMAND);
    mCommand = aComm;
    MOZ_ALWAYS_ZERO(pthread_mutex_unlock(&mMutex));
    MOZ_ALWAYS_ZERO(pthread_cond_signal(&mWakeup));
    void *retval;
    if (pthread_join(mThread, &retval) != 0 || retval != nullptr) {
      MOZ_CRASH("Failed to stop privileged chroot thread");
    }
    MOZ_ASSERT(mCommand == NO_THREAD);
  }
  return true;
}

bool
SandboxChroot::Prepare()
{
  LinuxCapabilities caps;
  if (!caps.GetCurrent() || !caps.Effective(CAP_SYS_CHROOT)) {
    SANDBOX_LOG_ERROR("don't have permission to chroot");
    return false;
  }
  // Start the thread...
  MOZ_ALWAYS_ZERO(pthread_mutex_lock(&mMutex));
  MOZ_ASSERT(mCommand == NO_THREAD);
  if (pthread_create(&mThread, nullptr, StaticThreadMain, this) != 0) {
    MOZ_ALWAYS_ZERO(pthread_mutex_unlock(&mMutex));
    SANDBOX_LOG_ERROR("pthread_create: %s", strerror(errno));
    return false;
  }
  // ... and wait for it to be ready.
  while (mCommand != NO_COMMAND) {
    MOZ_ASSERT(mCommand == NO_THREAD);
    MOZ_ALWAYS_ZERO(pthread_cond_wait(&mWakeup, &mMutex));
  }
  MOZ_ALWAYS_ZERO(pthread_mutex_unlock(&mMutex));
  return true;
}

void
SandboxChroot::Invoke()
{
  MOZ_ALWAYS_TRUE(SendCommand(DO_CHROOT));
  // Double-check that it worked:
  MOZ_RELEASE_ASSERT(access("/proc", F_OK) == -1);
  // Wait until the directory is empty -- in addition to the
  // pthread_join quality-of-implementation issues mentioned in
  // SandboxChroot.h, the thread is still reachable from the procfs
  // inode for a short time after the kernel indicates that the thread
  // exited (see set_tid_address(2)).
  while (access("/0", F_OK) == 0) {
    sched_yield();
  }
}

// This chroot()s and chdir()s to the /proc/<pid>/fdinfo directory of
// the calling thread.  When the thread exits the directory will
// become empty, none of the usual methods of creating new entries are
// allowed either before or after that point, and the inode is not
// reused even if another process/thread is later assigned the same
// pid.  This technique is due to Chromium (which uses a child process
// instead).
static bool
ChrootToThreadFdInfoDir()
{
  int tid = syscall(__NR_gettid);
  char path[32]; // "/proc/2147483647/fdinfo" is 24 including '\0'.
  DebugOnly<size_t> len;

  len = size_t(snprintf(path, sizeof(path), "/proc/%d/fdinfo", tid));
  MOZ_ASSERT(len < sizeof(path));

  if (chroot(path) != 0) {
    SANDBOX_LOG_ERROR("chroot: %s", strerror(errno));
    return false;
  }
  if (chdir("/") != 0) {
    SANDBOX_LOG_ERROR("chdir(\"/\"): %s", strerror(errno));
    // This shouldn't ever happen.
    MOZ_CRASH("chdir to / failed");
  }

  return true;
}

/* static */ void*
SandboxChroot::StaticThreadMain(void* aVoidPtr)
{
  static_cast<SandboxChroot*>(aVoidPtr)->ThreadMain();
  return nullptr;
}

void
SandboxChroot::ThreadMain()
{
  // First, drop everything that isn't CAP_SYS_CHROOT.  (This code
  // assumes that this thread already has effective CAP_SYS_CHROOT,
  // because Prepare() checked for it before creating this thread.)
  LinuxCapabilities caps;
  caps.Effective(CAP_SYS_CHROOT) = true;
  if (!caps.SetCurrent()) {
    SANDBOX_LOG_ERROR("capset: %s", strerror(errno));
    MOZ_CRASH("Can't limit chroot thread's capabilities");
  }

  MOZ_ALWAYS_ZERO(pthread_mutex_lock(&mMutex));
  MOZ_ASSERT(mCommand == NO_THREAD);
  mCommand = NO_COMMAND;
  MOZ_ALWAYS_ZERO(pthread_cond_signal(&mWakeup));
  while (mCommand == NO_COMMAND) {
    MOZ_ALWAYS_ZERO(pthread_cond_wait(&mWakeup, &mMutex));
  }
  if (mCommand == DO_CHROOT) {
    if (!ChrootToThreadFdInfoDir()) {
      MOZ_CRASH("Failed to chroot");
    }
  } else {
    MOZ_ASSERT(mCommand == JUST_EXIT);
  }
  mCommand = NO_THREAD;
  MOZ_ALWAYS_ZERO(pthread_mutex_unlock(&mMutex));
  // Drop the remaining capabilities; see note in SandboxChroot.h
  // about the potential unreliability of pthread_join.
  if (!LinuxCapabilities().SetCurrent()) {
    MOZ_CRASH("can't drop capabilities");
  }
}

} // namespace mozilla

#undef MOZ_ALWAYS_ZERO
