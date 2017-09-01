/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxLaunch.h"

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
#include "base/eintr_wrapper.h"
#include "base/strings/safe_sprintf.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/Move.h"
#include "mozilla/Preferences.h"
#include "mozilla/SandboxReporter.h"
#include "mozilla/SandboxSettings.h"
#include "mozilla/Unused.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "prenv.h"
#include "sandbox/linux/system_headers/linux_syscalls.h"

namespace mozilla {

static void
PreloadSandboxLib(base::environment_map* aEnv)
{
  // Preload libmozsandbox.so so that sandbox-related interpositions
  // can be defined there instead of in the executable.
  // (This could be made conditional on intent to use sandboxing, but
  // it's harmless for non-sandboxed processes.)
  nsAutoCString preload;
  // Prepend this, because people can and do preload libpthread.
  // (See bug 1222500.)
  preload.AssignLiteral("libmozsandbox.so");
  if (const char* oldPreload = PR_GetEnv("LD_PRELOAD")) {
    // Doesn't matter if oldPreload is ""; extra separators are ignored.
    preload.Append(' ');
    preload.Append(oldPreload);
  }
  MOZ_ASSERT(aEnv->count("LD_PRELOAD") == 0);
  (*aEnv)["LD_PRELOAD"] = preload.get();
}

static void
AttachSandboxReporter(base::file_handle_mapping_vector* aFdMap)
{
  int srcFd, dstFd;
  SandboxReporter::Singleton()
    ->GetClientFileDescriptorMapping(&srcFd, &dstFd);
  aFdMap->push_back({srcFd, dstFd});
}

class SandboxFork : public base::LaunchOptions::ForkDelegate {
public:
  explicit SandboxFork(int aFlags, bool aChroot);
  virtual ~SandboxFork();

  void PrepareMapping(base::file_handle_mapping_vector* aMap);
  pid_t Fork() override;

private:
  int mFlags;
  int mChrootServer;
  int mChrootClient;
  // For CloseSuperfluousFds in the chroot helper process:
  base::InjectiveMultimap mChrootMap;

  void StartChrootServer();
  SandboxFork(const SandboxFork&) = delete;
  SandboxFork& operator=(const SandboxFork&) = delete;
};

static int
GetEffectiveSandboxLevel(GeckoProcessType aType)
{
  const auto& info = SandboxInfo::Get();
  switch (aType) {
#ifdef MOZ_GMP_SANDBOX
  case GeckoProcessType_GMPlugin:
    if (info.Test(SandboxInfo::kEnabledForMedia)) {
      return 1;
    }
    return 0;
#endif
#ifdef MOZ_CONTENT_SANDBOX
  case GeckoProcessType_Content:
    // GetEffectiveContentSandboxLevel is main-thread-only due to prefs.
    MOZ_ASSERT(NS_IsMainThread());
    if (info.Test(SandboxInfo::kEnabledForContent)) {
      return GetEffectiveContentSandboxLevel();
    }
    return 0;
#endif
  default:
    return 0;
  }
}

void
SandboxLaunchPrepare(GeckoProcessType aType,
		     base::LaunchOptions* aOptions)
{
  PreloadSandboxLib(&aOptions->environ);
  AttachSandboxReporter(&aOptions->fds_to_remap);

  const auto& info = SandboxInfo::Get();

  // We won't try any kind of sandboxing without seccomp-bpf.
  if (!info.Test(SandboxInfo::kHasSeccompBPF)) {
    return;
  }

  // Check prefs (and env vars) controlling sandbox use.
  int level = GetEffectiveSandboxLevel(aType);
  if (level == 0) {
    return;
  }

  // At this point, we know we'll be using sandboxing; other
  // sandbox-related things from GeckoChildProcessHost can move here.

  // Anything below this requires unprivileged user namespaces.
  if (!info.Test(SandboxInfo::kHasUserNamespaces)) {
    return;
  }

  bool canChroot = false;
  int flags = 0;

  switch (aType) {
#ifdef MOZ_GMP_SANDBOX
  case GeckoProcessType_GMPlugin:
    if (level >= 1) {
      canChroot = true;
      flags |= CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWIPC;
    }
    break;
#endif
#ifdef MOZ_CONTENT_SANDBOX
  case GeckoProcessType_Content:
    // TODO: CLONE_NEWIPC (bug 1376910) if not fglrx and level >= 1,
    // once the XShm detection shim is fixed.
    //
    // "Future" levels we can't ship yet:
    // 4: socket/fs isolation (breaks PulseAudio)
    // 5: pid isolation (breaks PulseAudio for all clients & requires
    //    manually restarting PulseAudio daemon)
    if (level >= 4) {
      canChroot = true;
      flags |= CLONE_NEWNET;
    }
    if (level >= 5) {
      flags |= CLONE_NEWPID;
    }
    // Hidden pref to allow testing user namespaces separately, even
    // if there's nothing that would require them.
    if (Preferences::GetBool("security.sandbox.content.force-namespace")) {
      flags |= CLONE_NEWUSER;
    }
    break;
#endif
  default:
    // Nothing yet.
    break;
  }

  if (const auto envVar = PR_GetEnv("MOZ_NO_PID_SANDBOX")) {
    if (envVar[0] != '\0' && envVar[0] != '0') {
      flags &= ~CLONE_NEWPID;
    }
  }

  if (canChroot || flags != 0) {
    auto forker = MakeUnique<SandboxFork>(flags | CLONE_NEWUSER, canChroot);
    forker->PrepareMapping(&aOptions->fds_to_remap);
    aOptions->fork_delegate = Move(forker);
    if (canChroot) {
      aOptions->environ[kSandboxChrootEnvFlag] = "1";
    }
  }
}

SandboxFork::SandboxFork(int aFlags, bool aChroot)
: mFlags(aFlags)
, mChrootServer(-1)
, mChrootClient(-1)
{
  if (aChroot) {
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
  }
}

void
SandboxFork::PrepareMapping(base::file_handle_mapping_vector* aMap)
{
  if (mChrootClient >= 0) {
    aMap->push_back({ mChrootClient, kSandboxChrootClientFd });
  }
}

SandboxFork::~SandboxFork()
{
  if (mChrootClient >= 0) {
    close(mChrootClient);
  }
  if (mChrootServer >= 0) {
    close(mChrootServer);
  }
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
SandboxFork::Fork() {
  if (mFlags == 0) {
    MOZ_ASSERT(mChrootServer < 0);
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
SandboxFork::StartChrootServer()
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
