/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Sandbox.h"
#include "SandboxInternal.h"
#include "SandboxLogging.h"
#include "UnsafeSyscallProxy.h"

#include <unistd.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>

#include "mozilla/Atomics.h"
#include "mozilla/NullPtr.h"
#include "mozilla/unused.h"

#if defined(ANDROID)
#include "android_ucontext.h"
#endif

#include "linux_seccomp.h"
#include "SandboxFilter.h"

// See definition of SandboxDie, below.
#include "sandbox/linux/seccomp-bpf/die.h"

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

SandboxCrashFunc gSandboxCrashFunc;

#ifdef MOZ_GMP_SANDBOX
// For media plugins, we can start the sandbox before we dlopen the
// module, so we have to pre-open the file and simulate the sandboxed
// open().
static int gMediaPluginFileDesc = -1;
static const char *gMediaPluginFilePath;
#endif

struct SandboxFlags {
  bool isSupported;
#ifdef MOZ_CONTENT_SANDBOX
  bool isDisabledForContent;
#endif
#ifdef MOZ_GMP_SANDBOX
  bool isDisabledForGMP;
#endif

  SandboxFlags() {
    // Allow simulating the absence of seccomp-bpf support, for testing.
    if (getenv("MOZ_FAKE_NO_SANDBOX")) {
      isSupported = false;
    } else {
      // Determine whether seccomp-bpf is supported by trying to
      // enable it with an invalid pointer for the filter.  This will
      // fail with EFAULT if supported and EINVAL if not, without
      // changing the process's state.
      if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, nullptr) != -1) {
        MOZ_CRASH("prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, nullptr)"
                  " didn't fail");
      }
      isSupported = errno == EFAULT;
    }
#ifdef MOZ_CONTENT_SANDBOX
    isDisabledForContent = getenv("MOZ_DISABLE_CONTENT_SANDBOX");
#endif
#ifdef MOZ_GMP_SANDBOX
    isDisabledForGMP = getenv("MOZ_DISABLE_GMP_SANDBOX");
#endif
  }
};

static const SandboxFlags gSandboxFlags;

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

  long proxyResult;
  if (SandboxUnsafeProxy(syscall_nr, args, &proxyResult)) {
    SECCOMP_RESULT(ctx) = proxyResult;
    return;
  }

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

  if (InstallSyscallReporter()) {
    SANDBOX_LOG_ERROR("install_syscall_reporter() failed\n");
  }

  const sock_fprog* prog = nullptr;
  SandboxFilter filter(&prog, aType, getenv("MOZ_SANDBOX_VERBOSE"));
  InstallSyscallFilter(prog);
}

void
SandboxEarlyInit(GeckoProcessType aProcType)
{
  SandboxType aBoxType;
  switch(aProcType) {
#ifdef MOZ_CONTENT_SANDBOX
  case GeckoProcessType_Content:
    if (!CanSandboxContentProcess()) {
      return;
    }
    aBoxType = kSandboxContentProcess;
    break;
#endif
#ifdef MOZ_GMP_SANDBOX
  case GeckoProcessType_GMPlugin:
    if (!CanSandboxMediaPlugin()) {
      return;
    }
    aBoxType = kSandboxMediaPlugin;
    break;
#endif
  default:
    return;
  }
  SandboxStartUnsafeProxy();
  SetCurrentProcessSandbox(aBoxType);
  // FIXME: crash better if we Set*Sandbox without doing this.
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

  SandboxStopUnsafeProxy();
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
  if (gSandboxFlags.isDisabledForContent) {
    return;
  }

  SandboxLogicalStart();
}

bool
CanSandboxContentProcess()
{
  return gSandboxFlags.isSupported || gSandboxFlags.isDisabledForContent;
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
  if (gSandboxFlags.isDisabledForGMP) {
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

bool
CanSandboxMediaPlugin()
{
  return gSandboxFlags.isSupported || gSandboxFlags.isDisabledForGMP;
}
#endif // MOZ_GMP_SANDBOX

} // namespace mozilla
