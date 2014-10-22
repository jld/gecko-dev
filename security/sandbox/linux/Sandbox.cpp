/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Sandbox.h"
#include "SandboxFilter.h"
#include "SandboxInternal.h"
#include "SandboxLogging.h"
#include "UnsafeSyscallProxy.h"

#include <unistd.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>

#include "mozilla/Atomics.h"
#include "mozilla/SandboxInfo.h"
#include "mozilla/unused.h"
#include "sandbox/linux/seccomp-bpf/linux_seccomp.h"
#if defined(ANDROID)
#include "sandbox/linux/services/android_ucontext.h"
#endif

#ifdef MOZ_ASAN
// Copy libsanitizer declarations to avoid depending on ASAN headers.
// See also bug 1081242 comment #4.
extern "C" {
namespace __sanitizer {
// Win64 uses long long, but this is Linux.
typedef signed long sptr;
} // namespace __sanitizer

typedef struct {
  int coverage_sandboxed;
  __sanitizer::sptr coverage_fd;
  unsigned int coverage_max_block_size;
} __sanitizer_sandbox_arguments;

MOZ_IMPORT_API void
__sanitizer_sandbox_on_notify(__sanitizer_sandbox_arguments *args);
} // extern "C"
#endif // MOZ_ASAN

namespace mozilla {

#ifdef ANDROID
SandboxCrashFunc gSandboxCrashFunc;
#endif

#ifdef MOZ_GMP_SANDBOX
// For media plugins, we can start the sandbox before we dlopen the
// module, so we have to pre-open the file and simulate the sandboxed
// open().
static int gMediaPluginFileDesc = -1;
static const char *gMediaPluginFilePath;
#endif

UnsafeSyscallProxy gEarlySandboxProxy;

/**
 * This is the SIGSYS handler function. It is used to report to the user
 * which system call has been denied by Seccomp.
 * This function also makes the process exit as denying the system call
 * will otherwise generally lead to unexpected behavior from the process,
 * since we don't know if all functions will handle such denials gracefully.
 *
 * @see InstallSyscallReporter() function.
 */
static void
Reporter(int nr, siginfo_t *info, void *void_context)
{
  ucontext_t *ctx = static_cast<ucontext_t*>(void_context);
  unsigned long syscall_nr, args[6];
  pid_t pid = getpid();

  if (nr != SIGSYS) {
    return;
  }
  if (info->si_code != SYS_SECCOMP) {
    return;
  }
  if (!ctx) {
    return;
  }

  syscall_nr = SECCOMP_SYSCALL(ctx);
  args[0] = SECCOMP_PARM1(ctx);
  args[1] = SECCOMP_PARM2(ctx);
  args[2] = SECCOMP_PARM3(ctx);
  args[3] = SECCOMP_PARM4(ctx);
  args[4] = SECCOMP_PARM5(ctx);
  args[5] = SECCOMP_PARM6(ctx);

#if defined(ANDROID) && ANDROID_VERSION < 16
  // Bug 1093893: Translate tkill to tgkill for pthread_kill; fixed in
  // bionic commit 10c8ce59a (in JB and up; API level 16 = Android 4.1).
  if (syscall_nr == __NR_tkill) {
    intptr_t ret = syscall(__NR_tgkill, getpid(), args[0], args[1]);
    if (ret < 0) {
      ret = -errno;
    }
    SECCOMP_RESULT(ctx) = ret;
    return;
  }
#endif

  // FIXME: comment
  long proxyResult;
  if (gEarlySandboxProxy.Call(syscall_nr, args, &proxyResult)) {
    SECCOMP_RESULT(ctx) = proxyResult;
    return;
  }

#ifdef MOZ_ASAN
  // These have to be in the signal handler and not Deny() entries in
  // the SandboxFilter.cpp so that the syscall proxy (bug 1088387) can
  // intercept them between the real and logical sandbox start points.
#ifdef __NR_stat64
  const unsigned long nr_actual_stat = __NR_stat64;
#else
  const unsigned long nr_actual_stat = __NR_stat;
#endif

  // ASAN's error reporter, before compiler-rt r209773, will call
  // readlink and use the cached value only if that fails; and if it
  // found an external symbolizer, it will try to run it.  (See also
  // bug 1081242 comment #7.)
  if (syscall_nr == __NR_readlink || syscall_nr == nr_actual_stat) {
    SECCOMP_RESULT(ctx) = -ENOENT;
    return;
  }
#endif

#ifdef MOZ_GMP_SANDBOX
  if (syscall_nr == __NR_open && gMediaPluginFilePath) {
    const char *path = reinterpret_cast<const char*>(args[0]);
    int flags = int(args[1]);

    if ((flags & O_ACCMODE) != O_RDONLY) {
      SANDBOX_LOG_ERROR("non-read-only open of file %s attempted (flags=0%o)",
                        path, flags);
    } else if (strcmp(path, gMediaPluginFilePath) != 0) {
      SANDBOX_LOG_ERROR("attempt to open file %s which is not the media plugin"
                        " %s", path, gMediaPluginFilePath);
    } else if (gMediaPluginFileDesc == -1) {
      SANDBOX_LOG_ERROR("multiple opens of media plugin file unimplemented");
    } else {
      SECCOMP_RESULT(ctx) = gMediaPluginFileDesc;
      gMediaPluginFileDesc = -1;
      return;
    }
  }
#endif

  SANDBOX_LOG_ERROR("seccomp sandbox violation: pid %d, syscall %lu,"
                    " args %lu %lu %lu %lu %lu %lu.  Killing process.",
                    pid, syscall_nr,
                    args[0], args[1], args[2], args[3], args[4], args[5]);

  // Bug 1017393: record syscall number somewhere useful.
  info->si_addr = reinterpret_cast<void*>(syscall_nr);

  gSandboxCrashFunc(nr, info, void_context);
  _exit(127);
}

/**
 * The reporter is called when the process receives a SIGSYS signal.
 * The signal is sent by the kernel when Seccomp encounter a system call
 * that has not been allowed.
 * We register an action for that signal (calling the Reporter function).
 *
 * This function should not be used in production and thus generally be
 * called from debug code. In production, the process is directly killed.
 * For this reason, the function is ifdef'd, as there is no reason to
 * compile it while unused.
 *
 * @return 0 on success, -1 on failure.
 * @see Reporter() function.
 */
static int
InstallSyscallReporter(void)
{
  struct sigaction act;
  sigset_t mask;
  memset(&act, 0, sizeof(act));
  sigemptyset(&mask);
  sigaddset(&mask, SIGSYS);

  act.sa_sigaction = &Reporter;
  act.sa_flags = SA_SIGINFO | SA_NODEFER;
  if (sigaction(SIGSYS, &act, nullptr) < 0) {
    return -1;
  }
  if (sigemptyset(&mask) ||
    sigaddset(&mask, SIGSYS) ||
    sigprocmask(SIG_UNBLOCK, &mask, nullptr)) {
      return -1;
  }
  return 0;
}

/**
 * This function installs the syscall filter, a.k.a. seccomp.
 * PR_SET_NO_NEW_PRIVS ensures that it is impossible to grant more
 * syscalls to the process beyond this point (even after fork()).
 * SECCOMP_MODE_FILTER is the "bpf" mode of seccomp which allows
 * to pass a bpf program (in our case, it contains a syscall
 * whitelist).
 *
 * Reports failure by crashing.
 *
 * @see sock_fprog (the seccomp_prog).
 */
static void
InstallSyscallFilter(const sock_fprog *prog)
{
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
    SANDBOX_LOG_ERROR("prctl(PR_SET_NO_NEW_PRIVS) failed: %s", strerror(errno));
    MOZ_CRASH("prctl(PR_SET_NO_NEW_PRIVS)");
  }

  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, (unsigned long)prog, 0, 0)) {
    SANDBOX_LOG_ERROR("prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER) failed: %s",
                      strerror(errno));
    MOZ_CRASH("prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER)");
  }
}


// Common code for sandbox startup.
static void
SetCurrentProcessSandbox(SandboxType aType)
{
  MOZ_ASSERT(gSandboxCrashFunc);
  MOZ_ASSERT(SandboxInfo::Get().Test(SandboxInfo::kHasSeccompBPF));

  if (InstallSyscallReporter()) {
    SANDBOX_LOG_ERROR("install_syscall_reporter() failed\n");
  }

  const sock_fprog* prog = nullptr;
  SandboxFilter filter(&prog, aType, getenv("MOZ_SANDBOX_VERBOSE"));
  InstallSyscallFilter(prog);
}

static void
AssertSingleThreaded()
{
  struct stat sb;
  if (stat("/proc/self/task", &sb) < 0) {
    MOZ_CRASH("Couldn't access /proc/self/task");
  }
  if (sb.st_nlink != 3) {
    SANDBOX_LOG_ERROR("process must be single-threaded at this point.  "
                      "(%u threads)", static_cast<unsigned>(sb.st_nlink - 2));
    MOZ_CRASH();
  }
}

void
SandboxEarlyInit(GeckoProcessType aProcType)
{
  SandboxType aBoxType;
  switch(aProcType) {
#ifdef MOZ_CONTENT_SANDBOX
  case GeckoProcessType_Content:
    if (!SandboxInfo::Get().CanSandboxContent()) {
      return;
    }
    aBoxType = kSandboxContentProcess;
    break;
#endif
#ifdef MOZ_GMP_SANDBOX
  case GeckoProcessType_GMPlugin:
    if (!SandboxInfo::Get().CanSandboxMedia()) {
      return;
    }
    aBoxType = kSandboxMediaPlugin;
    break;
#endif
  default:
    return;
  }
  AssertSingleThreaded();
  if (!gEarlySandboxProxy.Start()) {
    SANDBOX_LOG_ERROR("Failed to start syscall proxy thread");
    MOZ_CRASH();
  }
  SetCurrentProcessSandbox(aBoxType);
}

static void
SandboxLogicalStart()
{
#ifdef MOZ_ASAN
  __sanitizer_sandbox_arguments asanArgs;
  asanArgs.coverage_sandboxed = 1;
  asanArgs.coverage_fd = -1;
  asanArgs.coverage_max_block_size = 0;
  __sanitizer_sandbox_on_notify(&asanArgs);
#endif

  if (!gEarlySandboxProxy.Stop()) {
    MOZ_CRASH("SandboxEarlyInit() wasn't called!");
  }
}

#ifdef MOZ_CONTENT_SANDBOX
/**
 * Starts the seccomp sandbox for a content process.  Should be called
 * only once, and before any potentially harmful content is loaded.
 *
 * Will normally make the process exit on failure.
*/
void
SetContentProcessSandbox()
{
  if (!SandboxInfo::Get().Test(SandboxInfo::kEnabledForContent)) {
    return;
  }

  SandboxLogicalStart();
}
#endif // MOZ_CONTENT_SANDBOX

#ifdef MOZ_GMP_SANDBOX
/**
 * Starts the seccomp sandbox for a media plugin process.  Should be
 * called only once, and before any potentially harmful content is
 * loaded -- including the plugin itself, if it's considered untrusted.
 *
 * The file indicated by aFilePath, if non-null, can be open()ed once
 * read-only after the sandbox starts; it should be the .so file
 * implementing the not-yet-loaded plugin.
 *
 * Will normally make the process exit on failure.
*/
void
SetMediaPluginSandbox(const char *aFilePath)
{
  if (!SandboxInfo::Get().Test(SandboxInfo::kEnabledForMedia)) {
    return;
  }

  if (aFilePath) {
    gMediaPluginFilePath = strdup(aFilePath);
    gMediaPluginFileDesc = open(aFilePath, O_RDONLY | O_CLOEXEC);
    if (gMediaPluginFileDesc == -1) {
      SANDBOX_LOG_ERROR("failed to open plugin file %s: %s",
                        aFilePath, strerror(errno));
      MOZ_CRASH();
    }
  }

  SandboxLogicalStart();
}
#endif // MOZ_GMP_SANDBOX

} // namespace mozilla
