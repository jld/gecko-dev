/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxFilter.h"
#include "SandboxInternal.h"

#include "linux_syscalls.h"

#include "mozilla/NullPtr.h"

#include <errno.h>
#include <linux/ipc.h>
#include <linux/net.h>
#include <linux/prctl.h>
#include <linux/sched.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "sandbox/linux/bpf_dsl/bpf_dsl.h"

using namespace sandbox::bpf_dsl;
#define CASES SANDBOX_BPF_DSL_CASES

// "Machine independent" pseudo-syscall numbers, to deal with arch
// dependencies.  (Most 32-bit archs started with 32-bit off_t; older
// archs started with 16-bit uid_t/gid_t; 32-bit registers can't hold
// a 64-bit offset for mmap; and so on.)
//
// For some of these, the "old" syscalls are also in use in some
// cases; see, e.g., the handling of RT vs. non-RT signal syscalls.

#ifdef __NR_mmap2
#define MI_NR_mmap __NR_mmap2
#else
#define MI_NR_mmap __NR_mmap
#endif

#ifdef __NR_getuid32
#define MI_NR_getuid __NR_getuid32
#define MI_NR_getgid __NR_getgid32
#define MI_NR_geteuid __NR_geteuid32
#define MI_NR_getegid __NR_getegid32
#define MI_NR_getresuid __NR_getresuid32
#define MI_NR_getresgid __NR_getresgid32
// The set*id syscalls are omitted; we'll probably never need to allow them.
#else
#define MI_NR_getuid __NR_getuid
#define MI_NR_getgid __NR_getgid
#define MI_NR_geteuid __NR_geteuid
#define MI_NR_getegid __NR_getegid
#define MI_NR_getresuid __NR_getresuid
#define MI_NR_getresgid __NR_getresgid
#endif

#ifdef __NR_stat64
#define MI_NR_stat __NR_stat64
#define MI_NR_fstat __NR_fstat64
#define MI_NR_lstat __NR_lstat64
#define MI_NR_fcntl __NR_fcntl64
#define MI_NR_getdents __NR_getdents64
#else
#define MI_NR_stat __NR_stat
#define MI_NR_fstat __NR_fstat
#define MI_NR_lstat __NR_lstat
#define MI_NR_fcntl __NR_fcntl
#define MI_NR_getdents __NR_getdents
#endif

namespace mozilla {

class PolicyBase : public SandboxBPFDSLPolicy {
public:
  ResultExpr Block() const {
    return Trap(SandboxHandler, nullptr);
  }
  virtual ResultExpr InvalidSyscall() const OVERRIDE {
    return Block();
  }
  // TODO: move useful common stuff here
};

#ifdef MOZ_CONTENT_SANDBOX

// Some helper macros to make the code that builds the filter more
// readable, and to help deal with differences among architectures.

#define SYSCALL_EXISTS(name) (defined(__NR_##name))

#define SYSCALL(name) (Condition(__NR_##name))
#if defined(__arm__) && (defined(__thumb__) || defined(__ARM_EABI__))
#define ARM_SYSCALL(name) (Condition(__ARM_NR_##name))
#endif

#define SYSCALL_WITH_ARG(name, arg, values...) ({ \
  uint32_t argValues[] = { values };              \
  Condition(__NR_##name, arg, argValues);         \
})

// Some architectures went through a transition from 32-bit to
// 64-bit off_t and had to version all the syscalls that referenced
// it; others (newer and/or 64-bit ones) didn't.  Adjust the
// conditional as needed.
#if SYSCALL_EXISTS(stat64)
#define SYSCALL_LARGEFILE(plain, versioned) SYSCALL(versioned)
#else
#define SYSCALL_LARGEFILE(plain, versioned) SYSCALL(plain)
#endif

// i386 multiplexes all the socket-related interfaces into a single
// syscall.
#if SYSCALL_EXISTS(socketcall)
#define SOCKETCALL(name, NAME) SYSCALL_WITH_ARG(socketcall, 0, SYS_##NAME)
#else
#define SOCKETCALL(name, NAME) SYSCALL(name)
#endif

// i386 multiplexes all the SysV-IPC-related interfaces into a single
// syscall.
#if SYSCALL_EXISTS(ipc)
#define SYSVIPCCALL(name, NAME) SYSCALL_WITH_ARG(ipc, 0, NAME)
#else
#define SYSVIPCCALL(name, NAME) SYSCALL(name)
#endif

class ContentSandboxPolicy : public PolicyBase {
public:
  ContentSandboxPolicy();
  virtual ~ContentSandboxPolicy();
  virtual ResultExpr EvaluateSyscall(int sysno) const OVERRIDE {


  Allow(SYSCALL(futex));
  Allow(SOCKETCALL(recvmsg, RECVMSG));
  Allow(SOCKETCALL(sendmsg, SENDMSG));

  // mmap2 is a little different from most off_t users, because it's
  // passed in a register (so it's a problem for even a "new" 32-bit
  // arch) -- and the workaround, mmap2, passes a page offset instead.
#if SYSCALL_EXISTS(mmap2)
  Allow(SYSCALL(mmap2));
#else
  Allow(SYSCALL(mmap));
#endif

  Allow(SYSCALL(clock_gettime));
  Allow(SYSCALL(epoll_wait));
  Allow(SYSCALL(gettimeofday));
  Allow(SYSCALL(read));
  Allow(SYSCALL(write));
  // 32-bit lseek is used, at least on Android, to implement ANSI fseek.
#if SYSCALL_EXISTS(_llseek)
  Allow(SYSCALL(_llseek));
#endif
  Allow(SYSCALL(lseek));
  // Android also uses 32-bit ftruncate.
  Allow(SYSCALL(ftruncate));
#if SYSCALL_EXISTS(ftruncate64)
  Allow(SYSCALL(ftruncate64));
#endif

  /* ioctl() is for GL. Remove when GL proxy is implemented.
   * Additionally ioctl() might be a place where we want to have
   * argument filtering */
  Allow(SYSCALL(ioctl));
  Allow(SYSCALL(close));
  Allow(SYSCALL(munmap));
  Allow(SYSCALL(mprotect));
  Allow(SYSCALL(writev));
  Allow(SYSCALL(clone));
  Allow(SYSCALL(brk));
#if SYSCALL_EXISTS(set_thread_area)
  Allow(SYSCALL(set_thread_area));
#endif

  Allow(SYSCALL(getpid));
  Allow(SYSCALL(gettid));
  Allow(SYSCALL(getrusage));
  Allow(SYSCALL(times));
  Allow(SYSCALL(madvise));
  Allow(SYSCALL(dup));
  Allow(SYSCALL(nanosleep));
  Allow(SYSCALL(poll));
  // select()'s arguments used to be passed by pointer as a struct.
#if SYSCALL_EXISTS(_newselect)
  Allow(SYSCALL(_newselect));
#else
  Allow(SYSCALL(select));
#endif
  // Some archs used to have 16-bit uid/gid instead of 32-bit.
#if SYSCALL_EXISTS(getuid32)
  Allow(SYSCALL(getuid32));
  Allow(SYSCALL(geteuid32));
  Allow(SYSCALL(getgid32));
  Allow(SYSCALL(getegid32));
#else
  Allow(SYSCALL(getuid));
  Allow(SYSCALL(geteuid));
  Allow(SYSCALL(getgid));
  Allow(SYSCALL(getegid));
#endif
  // Some newer archs (e.g., x64 and x32) have only rt_sigreturn, but
  // ARM has and uses both syscalls -- rt_sigreturn for SA_SIGINFO
  // handlers and classic sigreturn otherwise.
#if SYSCALL_EXISTS(sigreturn)
  Allow(SYSCALL(sigreturn));
#endif
  Allow(SYSCALL(rt_sigreturn));
  Allow(SYSCALL_LARGEFILE(fcntl, fcntl64));

  /* Must remove all of the following in the future, when no longer used */
  /* open() is for some legacy APIs such as font loading. */
  /* See bug 906996 for removing unlink(). */
  Allow(SYSCALL_LARGEFILE(fstat, fstat64));
  Allow(SYSCALL_LARGEFILE(stat, stat64));
  Allow(SYSCALL_LARGEFILE(lstat, lstat64));
  Allow(SOCKETCALL(socketpair, SOCKETPAIR));
  Deny(EACCES, SOCKETCALL(socket, SOCKET));
  Allow(SYSCALL(open));
  Allow(SYSCALL(readlink)); /* Workaround for bug 964455 */
  Allow(SYSCALL(prctl));
  Allow(SYSCALL(access));
  Allow(SYSCALL(unlink));
  Allow(SYSCALL(fsync));
  Allow(SYSCALL(msync));

#if defined(ANDROID) && !defined(MOZ_MEMORY)
  // Android's libc's realloc uses mremap.
  Allow(SYSCALL(mremap));
#endif

  /* Should remove all of the following in the future, if possible */
  Allow(SYSCALL(getpriority));
  Allow(SYSCALL(sched_get_priority_min));
  Allow(SYSCALL(sched_get_priority_max));
  Allow(SYSCALL(setpriority));
  // rt_sigprocmask is passed the sigset_t size.  On older archs,
  // sigprocmask is a compatibility shim that assumes the pre-RT size.
#if SYSCALL_EXISTS(sigprocmask)
  Allow(SYSCALL(sigprocmask));
#endif
  Allow(SYSCALL(rt_sigprocmask));

  // Used by profiler.  Also used for raise(), which causes problems
  // with Android KitKat abort(); see bug 1004832.
  Allow(SYSCALL_WITH_ARG(tgkill, 0, uint32_t(getpid())));

  Allow(SOCKETCALL(sendto, SENDTO));
  Allow(SOCKETCALL(recvfrom, RECVFROM));
  Allow(SYSCALL_LARGEFILE(getdents, getdents64));
  Allow(SYSCALL(epoll_ctl));
  Allow(SYSCALL(sched_yield));
  Allow(SYSCALL(sched_getscheduler));
  Allow(SYSCALL(sched_setscheduler));
  Allow(SYSCALL(sched_getparam));
  Allow(SYSCALL(sched_setparam));
  Allow(SYSCALL(sigaltstack));

  /* Always last and always OK calls */
  /* Architecture-specific very infrequently used syscalls */
#if SYSCALL_EXISTS(sigaction)
  Allow(SYSCALL(sigaction));
#endif
  Allow(SYSCALL(rt_sigaction));
#ifdef ARM_SYSCALL
  Allow(ARM_SYSCALL(breakpoint));
  Allow(ARM_SYSCALL(cacheflush));
  Allow(ARM_SYSCALL(usr26));
  Allow(ARM_SYSCALL(usr32));
  Allow(ARM_SYSCALL(set_tls));
#endif

  /* restart_syscall is called internally, generally when debugging */
  Allow(SYSCALL(restart_syscall));

  /* linux desktop is not as performance critical as mobile */
  /* we can place desktop syscalls at the end */
#ifndef ANDROID
  Allow(SYSCALL(stat));
  Allow(SYSCALL(getdents));
  Allow(SYSCALL(lstat));
#if SYSCALL_EXISTS(mmap2)
  Allow(SYSCALL(mmap2));
#else
  Allow(SYSCALL(mmap));
#endif
  Allow(SYSCALL(openat));
  Allow(SYSCALL(fcntl));
  Allow(SYSCALL(fstat));
  Allow(SYSCALL(readlink));
  Allow(SOCKETCALL(getsockname, GETSOCKNAME));
  Allow(SYSCALL(getuid));
  Allow(SYSCALL(geteuid));
  Allow(SYSCALL(mkdir));
  Allow(SYSCALL(getcwd));
  Allow(SYSCALL(readahead));
  Allow(SYSCALL(pread64));
  Allow(SYSCALL(statfs));
  Allow(SYSCALL(pipe));
#if SYSCALL_EXISTS(ugetrlimit)
  Allow(SYSCALL(ugetrlimit));
#else
  Allow(SYSCALL(getrlimit));
#endif
  Allow(SOCKETCALL(shutdown, SHUTDOWN));
  Allow(SOCKETCALL(getpeername, GETPEERNAME));
  Allow(SYSCALL(eventfd2));
  Allow(SYSCALL(clock_getres));
  Allow(SYSCALL(sysinfo));
  Allow(SYSCALL(getresuid));
  Allow(SYSCALL(umask));
  Allow(SYSCALL(getresgid));
  Allow(SYSCALL(poll));
  Allow(SYSCALL(inotify_init1));
  Allow(SYSCALL(wait4));
  Allow(SYSVIPCCALL(shmctl, SHMCTL));
  Allow(SYSCALL(set_robust_list));
  Allow(SYSCALL(rmdir));
  Allow(SOCKETCALL(recvfrom, RECVFROM));
  Allow(SYSVIPCCALL(shmdt, SHMDT));
  Allow(SYSCALL(pipe2));
  Allow(SOCKETCALL(setsockopt, SETSOCKOPT));
  Allow(SYSVIPCCALL(shmat, SHMAT));
  Allow(SYSCALL(set_tid_address));
  Allow(SYSCALL(inotify_add_watch));
  Allow(SYSCALL(rt_sigprocmask));
  Allow(SYSVIPCCALL(shmget, SHMGET));
#if SYSCALL_EXISTS(utimes)
  Allow(SYSCALL(utimes));
#else
  Allow(SYSCALL(utime));
#endif
#if SYSCALL_EXISTS(arch_prctl)
  Allow(SYSCALL(arch_prctl));
#endif
  Allow(SYSCALL(sched_getaffinity));
  /* We should remove all of the following in the future (possibly even more) */
  Allow(SOCKETCALL(socket, SOCKET));
  Allow(SYSCALL(chmod));
  Allow(SYSCALL(execve));
  Allow(SYSCALL(rename));
  Allow(SYSCALL(symlink));
  Allow(SOCKETCALL(connect, CONNECT));
  Allow(SYSCALL(quotactl));
  Allow(SYSCALL(kill));
  Allow(SOCKETCALL(sendto, SENDTO));
#endif

  /* nsSystemInfo uses uname (and we cache an instance, so */
  /* the info remains present even if we block the syscall) */
  Allow(SYSCALL(uname));
  Allow(SYSCALL(exit_group));
  Allow(SYSCALL(exit));
  }
};
#endif // MOZ_CONTENT_SANDBOX


#ifdef MOZ_GMP_SANDBOX
class GMPSandboxPolicy : public PolicyBase {
public:
  GMPSandboxPolicy() { }
  virtual ~GMPSandboxPolicy() { }

  virtual ResultExpr EvaluateSyscall(int sysno) const OVERRIDE {
    switch (sysno) {
      // Timekeeping
    case __NR_clock_gettime: {
      Arg<clockid_t> clk_id(0);
      return If(clk_id == CLOCK_MONOTONIC, Allow())
        .ElseIf(clk_id == CLOCK_REALTIME, Allow())
        .Else(Block());
    }
    case __NR_gettimeofday:
    case __NR_time:
    case __NR_nanosleep:
      return Allow();

      // Thread synchronization
    case __NR_futex:
      // FIXME: lock down ops; disallow PI futexes.
      return Allow();

      // Asynchronous I/O
    case __NR_epoll_wait:
    case __NR_epoll_ctl:
    case __NR_poll:
      return Allow();

      // Discard capabilities
    case __NR_close:
        return Allow();

      // Metadata of opened files
    case MI_NR_fstat:
      return Allow();

      // Simple I/O
    case __NR_write:
    case __NR_read:
      return Allow();

      // Fancy I/O; and the crash reporter needs socketpair()
      //
      // WARNING: if the process obtains a UDP socket, it can use
      // sendmsg() to send packets to any reachable address, and we
      // can't block that with seccomp while still allowing fd passing.
#ifdef __NR_socketcall
    case __NR_socketcall: {
      Arg<int> call(0);
      return Switch(call)
        .CASES((SYS_recvmsg,
                SYS_sendmsg,
                SYS_socketpair), Allow())
        .Default(Block());
    }
#else
    case __NR_recvmsg:
    case __NR_sendmsg:
    case __NR_socketpair:
      return Allow();
#endif

      // Memory mapping
    case MI_NR_mmap:
    case __NR_mprotect:
    case __NR_munmap:
      return Allow();
    case __NR_madvise: {
      Arg<int> advice(2);
      return If(advice == MADV_DONTNEED, Allow())
        .Else(Block());
    }

      // Signal handling
#ifdef MOZ_ASAN
    case __NR_sigaltstack:
#endif
#ifdef __NR_sigreturn
    case __NR_sigreturn:
#endif
    case __NR_rt_sigreturn:
#ifdef __NR_sigprocmask
    case __NR_sigprocmask:
#endif
    case __NR_rt_sigprocmask:
#if __NR_sigaction
    case __NR_sigaction:
#endif
    case __NR_rt_sigaction:
      return Allow();

      // Send signals within the process (raise(), profiling, etc.)
    case __NR_tgkill: {
      Arg<pid_t> tgid(0);
      return If(tgid == getpid(), Allow())
        .Else(Block());
    }

      // Read own pid/tid.
    case __NR_getpid:
    case __NR_gettid:
      return Allow();

      // Thread creation.  TODO: share across sandboxes.
    case __NR_clone: {
      // WARNING: s390 and cris pass the flags in the second arg -- see
      // CLONE_BACKWARDS2 in arch/Kconfig in the kernel source -- but we
      // don't support seccomp-bpf on those archs yet.
      Arg<int> flags(0);

#ifdef __GLIBC__
      // The glibc source hasn't changed the thread creation clone flags
      // since 2004, so this *should* be safe to hard-code.
      static const int new_thread_flags = CLONE_VM | CLONE_FS | CLONE_FILES |
        CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS |
        CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;
      return If(flags == new_thread_flags, Allow())
        .Else(Block());
#else
      // TODO: nail this down more for Bionic.
      //
      // At minimum we should require CLONE_THREAD, so that a single
      // SIGKILL from the parent will destroy all descendant tasks.  In
      // general, pinning down as much of the flags word as possible is a
      // good idea, because it exposes a lot of subtle (and probably not
      // well tested in all cases) kernel functionality.
      return If((flags & CLONE_THREAD) == CLONE_THREAD, Allow())
        .Else(Block());
#endif
    }

      // More thread creation.
#ifdef __NR_set_robust_list
    case __NR_set_robust_list:
      Allow();
#endif

      // prctl
    case __NR_prctl: {
      // FIXME: PR_SET_VMA
      Arg<int> op(0);
      return Switch(op)
        .CASES((PR_GET_SECCOMP, // BroadcastSetThreadSandbox, etc.
                PR_SET_NAME,    // Thread creation
                PR_SET_DUMPABLE), // Crash reporting
               Allow())
        .Default(Block());
    }

      // NSPR can call this when creating a thread, but it will accept a
      // polite "no".
    case __NR_getpriority:
      return Error(EACCES);

      // Stack bounds are obtained via pthread_getattr_np, which calls
      // this but doesn't actually need it:
    case __NR_sched_getaffinity:
      return Error(ENOSYS);

      // Machine-dependent stuff
#ifdef __arm__
    case __ARM_NR_breakpoint:
    case __ARM_NR_cacheflush:
    case __ARM_NR_usr26: // FIXME: Do we actually need this?
    case __ARM_NR_usr32:
    case __ARM_NR_set_tls:
      return Allow();
#endif

      // Needed when being debugged:
    case __NR_restart_syscall:
      return Allow();

      // Terminate threads or the process 
    case __NR_exit:
    case __NR_exit_group:
      return Allow();

    default:
      return Block();
    }
  }
};

sandbox::SandboxBPFPolicy*
GetMediaSandboxPolicy()
{
  return new GMPSandboxPolicy();
}

#endif // MOZ_GMP_SANDBOX

}
