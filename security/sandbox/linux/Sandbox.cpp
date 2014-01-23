/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <unistd.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <signal.h>
#include <string.h>

#include "mozilla/ArrayUtils.h"
#include "mozilla/NullPtr.h"
#if defined(ANDROID)
#include "android_ucontext.h"
#include <android/log.h>
#endif
#include "seccomp_filter.h"

#include "nsIException.h"
#include "nsIXPConnect.h"
#include "nsServiceManagerUtils.h"

#include "linux_seccomp.h"
#ifdef MOZ_LOGGING
#define FORCE_PR_LOG 1
#endif
#include "prlog.h"

namespace mozilla {
#if defined(ANDROID)
#define LOG_ERROR(args...) __android_log_print(ANDROID_LOG_ERROR, "Sandbox", ## args)
#elif defined(PR_LOGGING)
static PRLogModuleInfo* gSeccompSandboxLog;
#define LOG_ERROR(args...) PR_LOG(gSeccompSandboxLog, PR_LOG_ERROR, (args))
#else
#define LOG_ERROR(args...)
#endif

struct sock_filter seccomp_filter[] = {
  VALIDATE_ARCHITECTURE,
  EXAMINE_SYSCALL,
  SECCOMP_WHITELIST,
#ifdef MOZ_CONTENT_SANDBOX_REPORTER
  TRAP_PROCESS,
#else
  KILL_PROCESS,
#endif
};

struct sock_fprog seccomp_prog = {
  (unsigned short)MOZ_ARRAY_LENGTH(seccomp_filter),
  seccomp_filter,
};

/**
 * Log JS stack info in the same place as the sandbox violation
 * message.  Useful in case the responsible code is JS and all we have
 * are logs and a minidump with the C++ stacks (e.g., on TBPL).
 */
static void
SandboxLogJSStack(void)
{
  using namespace js;

  nsresult rv;
  nsCOMPtr<nsIXPConnect> xpc(do_GetService(nsIXPConnect::GetCID(), &rv));
  NS_ENSURE_SUCCESS_VOID(rv);
  NS_ENSURE_TRUE_VOID(xpc);
  nsCOMPtr<nsIStackFrame> frame;
  rv = xpc->GetCurrentJSStack(getter_AddRefs(frame));
  NS_ENSURE_SUCCESS_VOID(rv);
  for (int i = 0; frame; ++i) {
    char *fileName, *funName;
    int32_t lineNumber;

    rv = frame->GetFilename(&fileName);
    NS_ENSURE_SUCCESS_VOID(rv);
    rv = frame->GetLineNumber(&lineNumber);
    NS_ENSURE_SUCCESS_VOID(rv);
    rv = frame->GetName(&funName);
    NS_ENSURE_SUCCESS_VOID(rv);

    LOG_ERROR("JS frame %d: %s %s line %d", i,
              funName ? funName : "(anonymous)",
              fileName ? fileName : "(no file)",
              lineNumber);

    rv = frame->GetCaller(getter_AddRefs(frame));
    NS_ENSURE_SUCCESS_VOID(rv);
  }
}

/**
 * This is the SIGSYS handler function. It is used to report to the user
 * which system call has been denied by Seccomp.
 * This function also makes the process exit as denying the system call
 * will otherwise generally lead to unexpected behavior from the process,
 * since we don't know if all functions will handle such denials gracefully.
 *
 * @see InstallSyscallReporter() function.
 */
#ifdef MOZ_CONTENT_SANDBOX_REPORTER
static struct sigaction sSandboxNextHandler;

extern "C" {
  // To avoid including <sys/syscall.h>, so that we're using only the
  // syscall numbers from our tree.  GNU has this in unistd.h, but
  // Android seems to do otherwise.
  int syscall(int, ...);
}

static void
Reporter(int nr, siginfo_t *info, void *void_context)
{
  ucontext_t *ctx = static_cast<ucontext_t*>(void_context);
  MOZ_ASSERT(nr == SIGSYS);

  if (info->si_code == SYS_SECCOMP && ctx != nullptr) {
    unsigned long syscall_nr, args[6];

    syscall_nr = SECCOMP_SYSCALL(ctx);
    args[0] = SECCOMP_PARM1(ctx);
    args[1] = SECCOMP_PARM2(ctx);
    args[2] = SECCOMP_PARM3(ctx);
    args[3] = SECCOMP_PARM4(ctx);
    args[4] = SECCOMP_PARM5(ctx);
    args[5] = SECCOMP_PARM6(ctx);

    LOG_ERROR("seccomp sandbox violation: pid %u, syscall %lu, args %lu %lu %lu"
              " %lu %lu %lu.  Killing process.", getpid(), syscall,
              args[0], args[1], args[2], args[3], args[4], args[5]);
    SandboxLogJSStack();
  }

  // Try to invoke the next handler.  (This is typically the crash reporter.)
  // FIXME: should we also reset the signal mask based on sa_mask and sa_flags?
  //   What do we want to happen if the next handler isn't SA_NODEFER and
  //   invokes a forbidden syscall?
  if (sSandboxNextHandler.sa_flags & SA_SIGINFO) {
    // Can sa_sigaction be one of the SIG_* constants?
    sSandboxNextHandler.sa_sigaction(nr, info, void_context);
  } else {
    if (sSandboxNextHandler.sa_handler != SIG_DFL &&
        sSandboxNextHandler.sa_handler != SIG_IGN) {
      sSandboxNextHandler.sa_handler(nr);
    }
  }

  // Try to reraise, so the parent sees that this process crashed.
  // (If tgkill is forbidden, then that works too.)
  signal(SIGSYS, SIG_DFL);
  syscall(__NR_tgkill, getpid(), syscall(__NR_gettid), nr);
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

  if (sigaction(SIGSYS, nullptr, &act) < 0) {
    return -1;
  }
  sSandboxNextHandler = act;

  act.sa_sigaction = &Reporter;
  act.sa_flags |= SA_SIGINFO | SA_NODEFER;
  act.sa_flags &= ~SA_RESETHAND;
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
#endif

/**
 * This function installs the syscall filter, a.k.a. seccomp.
 * PR_SET_NO_NEW_PRIVS ensures that it is impossible to grant more
 * syscalls to the process beyond this point (even after fork()).
 * SECCOMP_MODE_FILTER is the "bpf" mode of seccomp which allows
 * to pass a bpf program (in our case, it contains a syscall
 * whitelist).
 *
 * @return 0 on success, 1 on failure.
 * @see sock_fprog (the seccomp_prog).
 */
static int
InstallSyscallFilter(void)
{
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
    return 1;
  }

  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &seccomp_prog, 0, 0)) {
    return 1;
  }
  return 0;
}

/**
 * Starts the seccomp sandbox for this process.
 * Generally called just after SetCurrentProcessPrivileges.
 * Should be called only once, and before any potentially harmful content is loaded.
 *
 * Should normally make the process exit on failure.
*/
void
SetCurrentProcessSandbox(void)
{
#if !defined(ANDROID) && defined(PR_LOGGING)
  if (!gSeccompSandboxLog) {
    gSeccompSandboxLog = PR_NewLogModule("SeccompSandbox");
  }
  PR_ASSERT(gSeccompSandboxLog);
#endif

#ifdef MOZ_CONTENT_SANDBOX_REPORTER
  if (InstallSyscallReporter()) {
    LOG_ERROR("install_syscall_reporter() failed\n");
    /* This is disabled so that we do not exit if seccomp-bpf is not available
     * This will be re-enabled when all B2G devices are required to support seccomp-bpf
     * See bug 880797 for reversal
     */

    /* _exit(127); */
  }

#endif

  if (InstallSyscallFilter()) {
    LOG_ERROR("install_syscall_filter() failed\n");
    /* This is disabled so that we do not exit if seccomp-bpf is not available
     * This will be re-enabled when all B2G devices are required to support seccomp-bpf
     * See bug 880797 for reversal
     */

    /* _exit(127); */
  }

}
} // namespace mozilla

