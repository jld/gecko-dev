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
#include <sys/socket.h>
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
#define CASES_FOR_mmap   case __NR_mmap2
#else
#define CASES_FOR_mmap   case __NR_mmap
#endif

#ifdef __NR_getuid32
#define CASES_FOR_getuid   case __NR_getuid32
#define CASES_FOR_getgid   case __NR_getgid32
#define CASES_FOR_geteuid   case __NR_geteuid32
#define CASES_FOR_getegid   case __NR_getegid32
#define CASES_FOR_getresuid   case __NR_getresuid32
#define CASES_FOR_getresgid   case __NR_getresgid32
// The set*id syscalls are omitted; we'll probably never need to allow them.
#else
#define CASES_FOR_getuid   case __NR_getuid
#define CASES_FOR_getgid   case __NR_getgid
#define CASES_FOR_geteuid   case __NR_geteuid
#define CASES_FOR_getegid   case __NR_getegid
#define CASES_FOR_getresuid   case __NR_getresuid
#define CASES_FOR_getresgid   case __NR_getresgid
#endif

#ifdef __NR_stat64
#define CASES_FOR_stat   case __NR_stat64
#define CASES_FOR_fstat   case __NR_fstat64
#define CASES_FOR_lstat   case __NR_lstat64
#define CASES_FOR_fcntl   case __NR_fcntl64
#define CASES_FOR_getdents   case __NR_getdents64
// FIXME: we might not need the compat cases for these on non-Android:
#define CASES_FOR_lseek   case __NR_lseek: case __NR__llseek
#define CASES_FOR_ftruncate   case __NR_ftruncate: case __NR_ftruncate64
#else
#define CASES_FOR_stat   case __NR_stat
#define CASES_FOR_fstat   case __NR_fstat
#define CASES_FOR_lstat   case __NR_lstat
#define CASES_FOR_fcntl   case __NR_fcntl
#define CASES_FOR_getdents   case __NR_getdents
#define CASES_FOR_lseek   case __NR_lseek
#define CASES_FOR_ftruncate   case __NR_ftruncate
#endif

#ifdef __NR_sigprocmask
#define CASES_FOR_sigprocmask   case __NR_sigprocmask: case __NR_rt_sigprocmask
#define CASES_FOR_sigaction   case __NR_sigaction: case __NR_rt_sigaction
#define CASES_FOR_sigreturn   case __NR_sigreturn: case __NR_rt_sigreturn
#else
#define CASES_FOR_sigprocmask   case __NR_rt_sigprocmask
#define CASES_FOR_sigaction   case __NR_rt_sigaction
#define CASES_FOR_sigreturn   case __NR_rt_sigreturn
#endif

#ifdef __NR__newselect
#define CASES_FOR_select   case __NR__newselect
#else
#define CASES_FOR_select   case __NR_select
#endif

#ifdef __NR_ugetrlimit
#define CASES_FOR_getrlimit   case __NR_ugetrlimit
#else
#define CASES_FOR_getrlimit   case __NR_getrlimit
#endif


namespace mozilla {

// This class whitelists everything used by the sandbox itself, by the
// core IPC code, or by the crash reporter.  (This includes thread
// creation, which might not be strictly necessary in all cases; if we
// ever have a process type that doesn't
class PolicyBase : public SandboxBPFDSLPolicy {
  static ResultExpr sBlock;
public:
  ResultExpr Block() const {
    if (sBlock == nullptr) {
      sBlock = Trap(SandboxHandler, nullptr);
    }
    return sBlock;
  }
  virtual ResultExpr InvalidSyscall() const OVERRIDE {
    return Block();
  }

  virtual ResultExpr ClonePolicy() const {
    // Allow use for simple thread creation (pthread_create) only.

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

  virtual ResultExpr PrctlPolicy() const {
    // FIXME: PR_SET_VMA
    Arg<int> op(0);
    return Switch(op)
      .CASES((PR_GET_SECCOMP, // BroadcastSetThreadSandbox, etc.
              PR_SET_NAME,    // Thread creation
              PR_SET_DUMPABLE), // Crash reporting
             Allow())
      .Default(Block());
  }

  virtual ResultExpr SocketCallPolicy(int aCall, int aOffset) const {
    switch (aCall) {
    case SYS_RECVMSG:
    case SYS_SENDMSG:
      return Allow();
    case SYS_SOCKETPAIR: {
      // See bug 1066750.
      Arg<int> domain(aOffset + 0), type(aOffset + 1);
      return If(domain == AF_UNIX &&
                (type == SOCK_STREAM || type == SOCK_SEQPACKET), Allow())
        .Else(Block());
    }
    default:
      return Block();
    }
  }

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

      // Metadata of opened files
    CASES_FOR_fstat:
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
      auto acc = Switch(call);

      for (int i = SYS_SOCKET; i <= SYS_SENDMMSG; ++i) {
        auto thisCase = SocketCallPolicy(i, 1);
        // Optimize out cases that are equal to the default.
        if (thisCase != Block()) {
          acc = acc.Case(i, thisCase);
        }
      }
      return acc.Default(Block());
    }
#else
#define DISPATCH_SOCKETCALL(sysnum, socketnum)         \
    case sysnum:                                       \
      return SocketCallPolicy(socketnum, 0)

      DISPATCH_SOCKETCALL(__NR_socket,      SYS_SOCKET);
      DISPATCH_SOCKETCALL(__NR_bind,        SYS_BIND);
      DISPATCH_SOCKETCALL(__NR_connect,     SYS_CONNECT);
      DISPATCH_SOCKETCALL(__NR_listen,      SYS_LISTEN);
      DISPATCH_SOCKETCALL(__NR_accept,      SYS_ACCEPT);
      DISPATCH_SOCKETCALL(__NR_getsockname, SYS_GETSOCKNAME);
      DISPATCH_SOCKETCALL(__NR_getpeername, SYS_GETPEERNAME);
      DISPATCH_SOCKETCALL(__NR_socketpair,  SYS_SOCKETPAIR);
#ifdef __NR_send
      DISPATCH_SOCKETCALL(__NR_send,        SYS_SEND);
      DISPATCH_SOCKETCALL(__NR_recv,        SYS_RECV);
#endif
      DISPATCH_SOCKETCALL(__NR_sendto,      SYS_SENDTO);
      DISPATCH_SOCKETCALL(__NR_recvfrom,    SYS_RECVFROM);
      DISPATCH_SOCKETCALL(__NR_shutdown,    SYS_SHUTDOWN);
      DISPATCH_SOCKETCALL(__NR_setsockopt,  SYS_SETSOCKOPT);
      DISPATCH_SOCKETCALL(__NR_getsockopt,  SYS_GETSOCKOPT);
      DISPATCH_SOCKETCALL(__NR_sendmsg,     SYS_SENDMSG);
      DISPATCH_SOCKETCALL(__NR_recvmsg,     SYS_RECVMSG);
      DISPATCH_SOCKETCALL(__NR_accept4,     SYS_ACCEPT4);
      DISPATCH_SOCKETCALL(__NR_recvmmsg,    SYS_RECVMMSG);
      DISPATCH_SOCKETCALL(__NR_sendmmsg,    SYS_SENDMMSG);

#undef DISPATCH_SOCKETCALL
#endif

      // Memory mapping
    CASES_FOR_mmap:
    case __NR_munmap:
      return Allow();

      // Signal handling
#if defined(MOZ_ASAN) || defined(ANDROID)
    case __NR_sigaltstack:
#endif
    CASES_FOR_sigreturn:
    CASES_FOR_sigprocmask:
    CASES_FOR_sigaction:
      return Allow();

      // Send signals within the process (raise(), profiling, etc.)
    case __NR_tgkill: {
      Arg<pid_t> tgid(0);
      return If(tgid == getpid(), Allow())
        .Else(Block());
    }

      // Thread creation.  TODO: share across sandboxes.
    case __NR_clone:
      return ClonePolicy();

      // More thread creation.
#ifdef __NR_set_robust_list
    case __NR_set_robust_list:
      return Allow();
#endif

      // prctl
    case __NR_prctl:
      return PrctlPolicy();

      // NSPR can call this when creating a thread, but it will accept a
      // polite "no".
    case __NR_getpriority:
      return Error(EACCES);

      // Stack bounds are obtained via pthread_getattr_np, which calls
      // this but doesn't actually need it:
    case __NR_sched_getaffinity:
      return Error(ENOSYS);

      // Read own pid/tid.
    case __NR_getpid:
    case __NR_gettid:
      return Allow();

      // Discard capabilities
    case __NR_close:
      return Allow();

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

ResultExpr PolicyBase::sBlock(nullptr);

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
  ContentSandboxPolicy() { }
  virtual ~ContentSandboxPolicy() { }
  virtual ResultExpr PrctlPolicy() const OVERRIDE {
    return Allow(); // FIXME
  }
  virtual ResultExpr SocketCallPolicy(int aCall, int aOffset) const OVERRIDE {
    switch(aCall) {
    case SYS_RECVFROM:
    case SYS_SENDTO:
      return Allow();

#ifdef ANDROID
    case SYS_SOCKET:
      return Error(EACCES);
#else
      // FIXME!
    case SYS_SOCKET:
    case SYS_CONNECT:
    case SYS_SETSOCKOPT:
    case SYS_GETSOCKNAME:
    case SYS_GETPEERNAME:
    case SYS_SHUTDOWN:
      return Allow();
#endif
    default:
      return PolicyBase::SocketCallPolicy(aCall, aOffset);
    }
  }

  virtual ResultExpr EvaluateSyscall(int sysno) const OVERRIDE {
    switch(sysno) {
      // Filesystem operations we need to get rid of.
    case __NR_open:
    case __NR_readlink:
    case __NR_access:
    case __NR_unlink:
    CASES_FOR_stat:
    CASES_FOR_lstat:
#ifndef ANDROID
      // FIXME!
    case __NR_openat:
    case __NR_mkdir:
    case __NR_rmdir:
    case __NR_getcwd:
    case __NR_statfs:
    case __NR_chmod:
    case __NR_rename:
    case __NR_symlink:
    case __NR_quotactl:  
    case __NR_utimes:
#endif
      return Allow();

    CASES_FOR_select:
      return Allow();

    CASES_FOR_getdents:
    CASES_FOR_lseek:
    CASES_FOR_ftruncate:
    case __NR_writev:
#ifndef ANDROID
    case __NR_readahead:
    case __NR_pread64:
      
#endif
      return Allow();

  /* ioctl() is for GL. Remove when GL proxy is implemented.
   * Additionally ioctl() might be a place where we want to have
   * argument filtering */
    case __NR_ioctl:
      return Allow();

      // FIXME: some of these are dangerous.
    CASES_FOR_fcntl:
      return Allow();

    case __NR_mprotect:
    case __NR_brk:
    case __NR_madvise:
#if defined(ANDROID) && !defined(MOZ_MEMORY)
  // Android's libc's realloc uses mremap.
    case __NR_mremap:
#endif
      return Allow();

#if SYSCALL_EXISTS(set_thread_area)
    case __NR_set_thread_area:
      return Allow();
#endif

    case __NR_getrusage:
      return Allow();
    case __NR_times:
      return Allow();

    case __NR_dup:
      return Allow();

    CASES_FOR_getuid:
    CASES_FOR_getgid:
    CASES_FOR_geteuid:
    CASES_FOR_getegid:
      return Allow();

    case __NR_fsync:
    case __NR_msync:
      return Allow();

    case __NR_getpriority:
    case __NR_setpriority:
    case __NR_sched_get_priority_min:
    case __NR_sched_get_priority_max:
    case __NR_sched_getscheduler:
    case __NR_sched_setscheduler:
    case __NR_sched_getparam:
    case __NR_sched_setparam:
    case __NR_sched_yield:
#ifndef ANDROID
    case __NR_sched_getaffinity:
#endif
      return Allow();

#ifndef ANDROID
    case __NR_pipe:
    case __NR_pipe2:
      return Allow();

    CASES_FOR_getrlimit:
    case __NR_clock_getres:
    case __NR_getresuid:
    case __NR_getresgid:
      return Allow();

    case __NR_umask:
    case __NR_kill:
    case __NR_wait4:
#if SYSCALL_EXISTS(arch_prctl)
    case __NR_arch_prctl:
#endif
      return Allow();
      
    case __NR_eventfd2:
    case __NR_inotify_init1:
    case __NR_inotify_add_watch:
      return Allow();

    case __NR_set_robust_list:
    case __NR_set_tid_address:
      return Allow();
#endif

#if 0
  Allow(SYSVIPCCALL(shmctl, SHMCTL));
  Allow(SYSVIPCCALL(shmdt, SHMDT));
  Allow(SYSVIPCCALL(shmat, SHMAT));
  Allow(SYSVIPCCALL(shmget, SHMGET));
#endif

  /* nsSystemInfo uses uname (and we cache an instance, so */
  /* the info remains present even if we block the syscall) */
    case __NR_uname:
#ifndef ANDROID
    case __NR_sysinfo:
#endif
      return Allow();
      
    default:
      return PolicyBase::EvaluateSyscall(sysno);
    }
  }
};

sandbox::SandboxBPFPolicy*
GetContentSandboxPolicy()
{
  return new ContentSandboxPolicy();
}
#endif // MOZ_CONTENT_SANDBOX


#ifdef MOZ_GMP_SANDBOX
class GMPSandboxPolicy : public PolicyBase {
public:
  GMPSandboxPolicy() { }
  virtual ~GMPSandboxPolicy() { }

  virtual ResultExpr EvaluateSyscall(int sysno) const OVERRIDE {
    switch (sysno) {
      // ipc::Shmem
    case __NR_mprotect:
      return Allow();
    case __NR_madvise: {
      Arg<int> advice(2);
      return If(advice == MADV_DONTNEED, Allow())
        .Else(Block());
    }

    default:
      return PolicyBase::EvaluateSyscall(sysno);
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
