/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxParent.h"

#include "SandboxLogging.h"
#include "SandboxInfo.h"
#include "SandboxUtil.h"

#include <linux/sched.h>
#include <sched.h>
#include <setjmp.h>

namespace mozilla {

namespace {

#ifdef MOZ_VALGRIND
bool IsRunningOnValgrind() {
#ifdef RUNNING_ON_VALGRIND
  return RUNNING_ON_VALGRIND;
#else
  return false;
#endif // RUNNING_ON_VALGRIND
}
#endif // MOZ_VALGRIND

// This function runs on the stack specified on the clone call. It uses longjmp
// to switch back to the original stack so the child can return from sys_clone.
int
CloneHelper(void* arg) {
  jmp_buf* env_ptr = reinterpret_cast<jmp_buf*>(arg);
  longjmp(*env_ptr, 1);

  // Should not be reached.
  MOZ_CRASH("unreachable");
  return 1;
}

// This function is noinline to ensure that stack_buf is below the stack pointer
// that is saved when setjmp is called below. This is needed because when
// compiled with FORTIFY_SOURCE, glibc's longjmp checks that the stack is moved
// upwards. See https://crbug.com/442912 for more details.
//
// Disable AddressSanitizer instrumentation for this function to make sure
// |stack_buf| is allocated on thread stack instead of ASan's fake stack.
// Under ASan longjmp() will attempt to clean up the area between the old and
// new stack pointers and print a warning that may confuse the user.
MOZ_NEVER_INLINE MOZ_ASAN_BLACKLIST pid_t
CloneAndLongjmpInChild(unsigned long flags,
                       pid_t* ptid,
                       pid_t* ctid,
                       jmp_buf* env) {
  // We use the libc clone wrapper instead of making the syscall
  // directly because making the syscall may fail to update the libc's
  // internal pid cache. The libc interface unfortunately requires
  // specifying a new stack, so we use setjmp/longjmp to emulate
  // fork-like behavior.
  char stack_buf[PTHREAD_STACK_MIN];
#if defined(ARCH_CPU_X86_FAMILY) || defined(ARCH_CPU_ARM_FAMILY) || \
    defined(ARCH_CPU_MIPS64_FAMILY) || defined(ARCH_CPU_MIPS_FAMILY)
  // The stack grows downward.
  void* stack = stack_buf + sizeof(stack_buf);
#else
#error "Unsupported architecture"
#endif // ARCH_*
  return clone(&CloneHelper, stack, flags, env, ptid, nullptr, ctid);
}

}  // anonymous namespace

pid_t ForkWithFlags(unsigned long flags,
                    pid_t* ptid = nullptr,
                    pid_t* ctid = nullptr) {
  const bool clone_tls_used = flags & CLONE_SETTLS;
  const bool invalid_ctid =
      (flags & (CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)) && !ctid;
  const bool invalid_ptid = (flags & CLONE_PARENT_SETTID) && !ptid;

  // We do not support CLONE_VM.
  const bool clone_vm_used = flags & CLONE_VM;

  if (clone_tls_used || invalid_ctid || invalid_ptid || clone_vm_used) {
    MOZ_CRASH("Invalid usage of ForkWithFlags");
  }

#ifdef MOZ_VALGRIND
  // Valgrind's clone implementation does not support specifiying a child_stack
  // without CLONE_VM, so we cannot use libc's clone wrapper when running under
  // Valgrind. As a result, the libc pid cache may be incorrect under Valgrind.
  // See https://crbug.com/442817 for more details.
  if (IsRunningOnValgrind()) {
    // See kernel/fork.c in Linux. There is different ordering of sys_clone
    // parameters depending on CONFIG_CLONE_BACKWARDS* configuration options.
#if defined(ARCH_CPU_X86_64)
    return syscall(__NR_clone, flags, nullptr, ptid, ctid, nullptr);
#elif defined(ARCH_CPU_X86) || defined(ARCH_CPU_ARM_FAMILY) || \
    defined(ARCH_CPU_MIPS_FAMILY) || defined(ARCH_CPU_MIPS64_FAMILY)
    // CONFIG_CLONE_BACKWARDS defined.
    return syscall(__NR_clone, flags, nullptr, ptid, nullptr, ctid);
#else
#error "Unsupported architecture"
#endif // ARCH_*
  }
#endif // MOZ_VALGRIND

  jmp_buf env;
  if (setjmp(env) == 0) {
    return CloneAndLongjmpInChild(flags, ptid, ctid, &env);
  }

  return 0;
}

pid_t SandboxFork(base::ChildPrivileges aPrivs)
{
  pid_t pid = -1;
  if (aPrivs == base::PRIVILEGES_UNPRIVILEGED &&
      SandboxInfo::Get().Test(SandboxInfo::kHasSeccompTSync |
			      SandboxInfo::kHasUserNamespaces)) {
    uid_t uid = getuid();
    gid_t gid = getgid();
    pid = ForkWithFlags(CLONE_NEWUSER | CLONE_NEWPID | SIGCHLD);
    if (pid == 0) {
      SetUpUserNamespace(uid, gid);
    } else if (pid < 0) {
      SANDBOX_LOG_ERROR("clone(CLONE_NEWUSER|CLONE_NEWPID): %s",
			strerror(errno));
      MOZ_DIAGNOSTIC_ASSERT(false, "CONFIG_USER_NS=y but CONFIG_PID_NS=n?");
    }
  }
  if (pid < 0) {
    pid = fork();
  }
  return pid;
}

} // namespace mozilla
