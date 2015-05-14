/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxChroot.h"

#include "SandboxLogging.h"
#include "LinuxCapabilities.h"
#include "LinuxSched.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#include "mozilla/Assertions.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Maybe.h"
#include "mozilla/NullPtr.h"
#include "mozilla/unused.h"
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

static void
AlwaysClose(int fd)
{
  if (IGNORE_EINTR(close(fd)) != 0) {
    SANDBOX_LOG_ERROR("close: %s", strerror(errno));
    MOZ_CRASH("failed to close()");
  }
}

// Helper for CallInNewTask; see below.
template<class F>
static int
CallInNewTaskMain(void *aVoidPtr)
{
  (*static_cast<F*>(aVoidPtr))();
  // Terminate this task while assuming as little as possible about
  // what works normally in it:
  syscall(__NR_exit, 0);
  MOZ_CRASH("exit(2) failed?");
  return 0;
}

// Call a function-like object in a new task created with the given
// clone flags and wait for it to terminate.  The flags cannot include
// CLONE_PARENT or CLONE_THREAD and must not specify a termination
// signal.  When/if the function returns, the task exits with status 0.
//
// Returns the exit status (as for waitpid) on success; if the clone
// failed, returns Nothing() and sets errno.
template<class F>
static Maybe<int>
CallInNewTask(int aFlags, F&& aCallable)
{
  // Signal number 0 = don't signal the parent when the child exits, and
  // omit child from calls to waitpid without __WALL/__WCLONE.
  MOZ_RELEASE_ASSERT((aFlags & CSIGNAL) == 0);
  // Unsupported flags: CLONE_PARENT makes the task a sibling, and
  // CLONE_THREAD creates a task that can't be wait()ed for
  // (CLONE_CHILD_CLEARTID has to be used instead).
  MOZ_RELEASE_ASSERT((aFlags & (CLONE_PARENT | CLONE_THREAD)) == 0);
  // The flags that need extra arguments will do nothing, but that
  // should be obvious from the function signature.

  char stackBuf[PTHREAD_STACK_MIN];
  char *sp = stackBuf;
#ifndef __hppa
  // Stack grows down.  See also js/src/jscpucfg.h
  sp += sizeof(stackBuf);
#endif
  pid_t pid = clone(CallInNewTaskMain<F>, sp, aFlags,
                    static_cast<void*>(&aCallable),
                    nullptr, nullptr, nullptr);
  if (pid < 0) {
    return Nothing();
  }
  int status;
  pid_t wpid = HANDLE_EINTR(waitpid(pid, &status, __WCLONE));
  MOZ_RELEASE_ASSERT(wpid == pid);
  return Some(status);
}

// OpenPermanentlyEmptyDirectory opens a directory that is empty and
// cannot have new entries created in it.  In order to avoid
// dependencies on the host's filesystem choices, this uses the
// /proc/<pid>/fdinfo directory of a task that has exited (a technique
// used by Chromium's sandbox since 2011).
static int
OpenPermanentlyEmptyDirectory()
{
  int fd;
  int openErrno;
  // In order for this to work even if we're in a different pid
  // namespace from /proc, the task has to be a thread group leader so
  // that /proc/self works, but it shares the address space and file
  // table to avoid the unnecessary complication of regular IPC.
  auto status = CallInNewTask(CLONE_VM | CLONE_FILES, [&] {
      // Do as little as possible here; thread-local storage is
      // probably broken.
      fd = HANDLE_EINTR(open("/proc/self/fdinfo", O_RDONLY | O_DIRECTORY));
      if (fd < 0) {
        openErrno = errno;
      }
    });
  // Error checking, sigh.
  if (!status) {
    int cloneErrno = errno;
    SANDBOX_LOG_ERROR("OpenPermanentlyEmptyDirectory: clone: %s",
                      strerror(cloneErrno));
    errno = cloneErrno;
    return -1;
  }
  if (!WIFEXITED(*status) || WEXITSTATUS(*status)) {
    SANDBOX_LOG_ERROR("OpenPermanentlyEmptyDirectory: exit status %d", *status);
    errno = EIO;
    return -1;
  }
  if (fd < 0) {
    errno = openErrno;
  } else {
    MOZ_DIAGNOSTIC_ASSERT(faccessat(fd, "0", F_OK, AT_SYMLINK_NOFOLLOW) == -1,
      "directory should be empty at this point");
  }
  return fd;
}

bool
SandboxChroot::Prepare()
{
  LinuxCapabilities caps;
  if (!caps.GetCurrent() || !caps.Effective(CAP_SYS_CHROOT)) {
    SANDBOX_LOG_ERROR("don't have permission to chroot");
    return false;
  }
  mFd = OpenPermanentlyEmptyDirectory();
  if (mFd < 0) {
    SANDBOX_LOG_ERROR("failed to create empty directory for chroot");
    return false;
  }
  MOZ_ALWAYS_ZERO(pthread_mutex_lock(&mMutex));
  MOZ_ASSERT(mCommand == NO_THREAD);
  if (pthread_create(&mThread, nullptr, StaticThreadMain, this) != 0) {
    MOZ_ALWAYS_ZERO(pthread_mutex_unlock(&mMutex));
    SANDBOX_LOG_ERROR("pthread_create: %s", strerror(errno));
    return false;
  }
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
}

static bool
ChrootToFileDesc(int fd)
{
  if (fchdir(fd) != 0) {
    SANDBOX_LOG_ERROR("fchdir: %s", strerror(errno));
    return false;
  }
  if (chroot(".") != 0) {
    SANDBOX_LOG_ERROR("chroot: %s", strerror(errno));
    return false;
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
    MOZ_ASSERT(mFd >= 0);
    if (!ChrootToFileDesc(mFd)) {
      MOZ_CRASH("Failed to chroot");
    }
  } else {
    MOZ_ASSERT(mCommand == JUST_EXIT);
  }
  if (mFd >= 0) {
    AlwaysClose(mFd);
    mFd = -1;
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
