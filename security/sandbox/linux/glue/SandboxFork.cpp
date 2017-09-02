/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxFork.h"

#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "LinuxCapabilities.h"
#include "LinuxSched.h"
#include "SandboxChrootProto.h"
#include "SandboxInfo.h"
#include "SandboxLogging.h"
#include "mozilla/Attributes.h"
#include "mozilla/Unused.h"
#include "base/eintr_wrapper.h"
#include "base/strings/safe_sprintf.h"
#include "sandbox/linux/system_headers/linux_syscalls.h"

namespace mozilla {

SandboxForker::SandboxForker(base::ChildPrivileges aPrivs) {
  const auto& info = SandboxInfo::Get();
  bool canChroot = false;
  mFlags = 0;

  if (!info.Test(SandboxInfo::kHasUserNamespaces)) {
    return;
  }

  switch (aPrivs) {
  case base::PRIVILEGES_MEDIA:
    canChroot = info.Test(SandboxInfo::kHasSeccompBPF);
    mFlags |= CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWIPC;
    break;
    // Unsure of breakage; let's find out?
  case base::PRIVILEGES_CONTENT:
  case base::PRIVILEGES_FILEREAD:
    mFlags |= CLONE_NEWPID;
    break;
  default:
    /* Nothing yet. */
    break;
  }

  if (canChroot || mFlags != 0) {
    mFlags |= CLONE_NEWUSER;
  }

  if (canChroot) {
    int fds[2];
    int rv = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    if (rv != 0) {
      SANDBOX_LOG_ERROR("socketpair: %s", strerror(errno));
      MOZ_CRASH("socketpair failed");
    }
    mChrootClient = fds[0];
    mChrootServer = fds[1];
    // Do this here because the child process won't be able to malloc.
    mChrootMap.push_back(base::InjectionArc(mChrootServer,
                                            mChrootServer,
                                            false));
  } else {
    int fds[2];
    int rv = pipe2(fds, O_CLOEXEC);
    if (rv != 0) {
      SANDBOX_LOG_ERROR("pipe2: %s", strerror(errno));
      MOZ_CRASH("pipe2 failed");
    }
    close(fds[0]);
    mChrootClient = fds[1];
    mChrootServer = -1;
  }
}

SandboxForker::~SandboxForker() {
  if (mChrootClient >= 0) {
    close(mChrootClient);
  }
  if (mChrootServer >= 0) {
    close(mChrootServer);
  }
}

void
SandboxForker::RegisterFileDescriptors(base::file_handle_mapping_vector* aMap)
{
  aMap->push_back(std::pair<int, int>(mChrootClient, kSandboxChrootClientFd));
}

// FIXME: better error messages throughout.

static void
BlockAllSignals(sigset_t* aOldSigs)
{
  sigset_t allSigs;
  int rv = sigfillset(&allSigs);
  MOZ_RELEASE_ASSERT(rv == 0);
  // This will probably mask off a few libc-internal signals (for
  // glibc, SIGCANCEL and SIGSETXID).  In theory that should be fine.
  rv = pthread_sigmask(SIG_BLOCK, &allSigs, aOldSigs);
  if (rv != 0) {
    SANDBOX_LOG_ERROR("pthread_sigmask (block all): %s", strerror(rv));
    MOZ_CRASH("pthread_sigmask");
  }
}

static void
RestoreSignals(const sigset_t* aOldSigs)
{
  // Assuming that pthread_sigmask is a thin layer over rt_sigprocmask
  // and doesn't try to touch TLS, which may be in an "interesting"
  // state right now:
  int rv = pthread_sigmask(SIG_SETMASK, aOldSigs, nullptr);
  if (rv != 0) {
    SANDBOX_LOG_ERROR("pthread_sigmask (restore): %s", strerror(-rv));
    MOZ_CRASH("pthread_sigmask");
  }
}

static void
ResetSignalHandlers()
{
  for (int signum = 1; signum <= SIGRTMAX; ++signum) {
    if (signal(signum, SIG_DFL) == SIG_ERR) {
      MOZ_DIAGNOSTIC_ASSERT(errno == EINVAL);
    }
  }
}

static pid_t
DoClone(int aFlags)
{
  // FIXME: s390
  return syscall(__NR_clone, aFlags | SIGCHLD,
                      nullptr, nullptr, nullptr, nullptr);
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

// This function sets up uid/gid mappings that preserve the
// process's previous ids.  Mapping the uid/gid to something is
// necessary in order to nest user namespaces (not currently being
// used, but could be useful), and leaving the ids unchanged is
// likely to minimize unexpected side-effects.
static void
ConfigureUserNamespace(uid_t uid, gid_t gid)
{
  using base::strings::SafeSPrintf;
  char buf[sizeof("18446744073709551615 18446744073709551615 1")];
  size_t len;

  len = static_cast<size_t>(SafeSPrintf(buf, "%d %d 1", uid, uid));
  MOZ_ASSERT(len < sizeof(buf));
  if (!WriteStringToFile("/proc/self/uid_map", buf, len)) {
    MOZ_CRASH("Failed to write /proc/self/uid_map");
  }

  // In recent kernels (3.19, 3.18.2, 3.17.8), for security reasons,
  // establishing gid mappings will fail unless the process first
  // revokes its ability to call setgroups() by using a /proc node
  // added in the same set of patches.
  Unused << WriteStringToFile("/proc/self/setgroups", "deny", 4);

  len = static_cast<size_t>(SafeSPrintf(buf, "%d %d 1", gid, gid));
  MOZ_ASSERT(len < sizeof(buf));
  if (!WriteStringToFile("/proc/self/gid_map", buf, len)) {
    MOZ_CRASH("Failed to write /proc/self/gid_map");
  }
}

static void
DropAllCaps()
{
  if (!LinuxCapabilities().SetCurrent()) {
    SANDBOX_LOG_ERROR("capset (drop all): %s", strerror(errno));
  }

}

pid_t
SandboxForker::Fork() {
  if (mFlags == 0) {
    return fork();
  }

  uid_t uid = getuid();
  gid_t gid = getgid();

  sigset_t oldSigs;
  BlockAllSignals(&oldSigs);
  pid_t pid = DoClone(mFlags);
  if (pid < 0) {
    RestoreSignals(&oldSigs);
    errno = -pid;
    return -1;
  }
  if (pid > 0) {
    RestoreSignals(&oldSigs);
    return pid;
  }

  ResetSignalHandlers();
  RestoreSignals(&oldSigs);
  ConfigureUserNamespace(uid, gid);

  if (mChrootServer >= 0) {
    StartChrootServer();
  }

  DropAllCaps();
  return 0;
}

void
SandboxForker::StartChrootServer()
{
  pid_t pid = DoClone(CLONE_FS);
  MOZ_RELEASE_ASSERT(pid >= 0);
  if (pid > 0) {
    return;
  }

  LinuxCapabilities caps;
  caps.Effective(CAP_SYS_CHROOT) = true;
  if (!caps.SetCurrent()) {
    SANDBOX_LOG_ERROR("capset (chroot helper): %s", strerror(errno));
    MOZ_DIAGNOSTIC_ASSERT(false);
  }

  CloseSuperfluousFds(mChrootMap);

  char msg;
  ssize_t msgLen = HANDLE_EINTR(read(mChrootServer, &msg, 1));
  if (msgLen == 0) {
    // Process exited before chrooting (or chose not to chroot?).
    _exit(0);
  }
  MOZ_RELEASE_ASSERT(msgLen == 1);
  MOZ_RELEASE_ASSERT(msg == kSandboxChrootRequest);

  int rv = chroot("/proc/self/fdinfo");
  MOZ_RELEASE_ASSERT(rv == 0);

  // Drop CAP_SYS_CHROOT ASAP.  This *must* happen before responding;
  // the main child won't be able to waitpid(), so it could start
  // handling hostile content before this process finishes exiting.
  DropAllCaps();

  rv = chdir("/");
  MOZ_RELEASE_ASSERT(rv == 0);

  msg = kSandboxChrootResponse;
  msgLen = HANDLE_EINTR(write(mChrootServer, &msg, 1));
  MOZ_RELEASE_ASSERT(msgLen == 1);
  _exit(0);
}

} // namespace mozilla
