/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "UnsafeSyscallProxy.h"

#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

#include "SandboxLogging.h"
#include "linux_syscalls.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/NullPtr.h"

// See UnsafeSyscallProxy.h for why this exists.

// The client side of this module needs to be async signal safe,
// because the syscall it's proxying could validly be taking place in
// async signal context (e.g., open).  Since there's no reliable way
// to check for async signal context, we have to handle it.
//
// In particular, this means that the pthread mutex/condvar facilities
// cannot be used.  They can use global or thread-local state (and at
// least glibc appears to so), which could be inconsistent if *any*
// synchronization operation, even on an unrelated mutex, is
// interrupted.
//
// Therefore, atomic integers are used, with the help of futex(2) to
// allow blocking rather than just spin-waiting, and sigprocmask(2) to
// manage reentrancy (so that this code doesn't have to be "NMI-safe").

namespace mozilla {

// Atomically test that aInt contains aVal and, if so, block.
static void AtomicWait(Atomic<int>& aInt, int aVal)
{
  static_assert(sizeof(Atomic<int>) == sizeof(int), "Atomic<int> != int");
  int *uaddr = reinterpret_cast<int*>(&aInt);
  const struct timespec* timeout = nullptr;
  syscall(__NR_futex, uaddr, FUTEX_WAIT, aVal, timeout);
}

// Unblock any threads calling AtomicWait on aInt.  The optional
// argument aNumThreads allows limiting the number of threads woken up
// to avoid "thundering herds"; this is probably not important here.
static void AtomicWake(Atomic<int>& aInt, int aNumThreads = INT_MAX)
{
  int *uaddr = reinterpret_cast<int*>(&aInt);
  syscall(__NR_futex, uaddr, FUTEX_WAKE, aNumThreads);
}

class UnsafeSyscallProxyImpl MOZ_FINAL {
  pthread_t mThread;
  // mCurrentClient contains the ID of the thread currently
  // communicating with the proxy, or 0 if none.  This acts as a
  // simple mutex which allows detecting reentrancy.
  Atomic<int> mCurrentClient;
  // mStatus holds a value from enum Status; see below.  Only the
  // proxy thread and the current client should modify it.  (Any
  // client may test if it is STOPPED.)
  Atomic<int> mStatus;
  // The state for the syscall currently being proxied.  If the status
  // is READY, then the current client may access these fields; if the
  // status is WORKING, then the proxy thread may access them.  As a
  // special case, if mSyscall is __NR_exit (terminate current thread,
  // which can't be proxied) then this is a request to the proxy
  // thread to enter the STOPPED state and exit.
  unsigned long mSyscall, mArgs[6];
  long mResult;

  enum Status {
    // READY: The proxy is ready to receive a syscall request; if a
    // client is holding mCurrentClient, it may access the
    // argument/result state.
    READY,
    // WORKING: The proxy is performing a syscall request; it may
    // access the argument/result state, and mCurrentClient must be
    // nonzero.
    WORKING,
    // STOPPED: The proxy thread is not running (or is in the process
    // of exiting).
    STOPPED,
  };

  // A RAII class to temporarily block most signals.  This does not
  // block SIGSYS, because if we somehow caused a seccomp trap to
  // occur in that state then the kernel would both unblock the signal
  // and remove our handler before posting the signal, thus
  // immediately killing the process.  It's better to leave it
  // unblocked and detect the reentrant invocation of the syscall
  // proxy client, so we have a chance to report the error.
  class BlockSignals {
    sigset_t mOldMask;
  public:
    BlockSignals() {
      sigset_t newMask;
      sigfillset(&newMask);
      sigdelset(&newMask, SIGSYS);
      sigprocmask(SIG_SETMASK, &newMask, &mOldMask);
    }
    ~BlockSignals() {
      sigprocmask(SIG_SETMASK, &mOldMask, nullptr);
    }
  };

  // A RAII class to take the lock implemented by mCurrentClient; also
  // blocks signals other than SIGSYS, so reentrancy is always an
  // error.  Can fail if acquired recursively (see above re why SIGSYS
  // is unblocked); this is indicated via operator bool.
  friend class CurrentClient;
  class CurrentClient {
    // Note: ctor/dtor order matters.  If signals aren't blocked for
    // the entire time mCurrentClient is held, then an async signal
    // handler could try to use the proxy and fail due to reentrancy
    // when it should have succeeded (due to not being handled until
    // the outer invocation was finished).
    BlockSignals mBlock;
    UnsafeSyscallProxyImpl* const mProxy;
    bool mLocked;
  public:
    explicit CurrentClient(UnsafeSyscallProxyImpl *aProxy) : mProxy(aProxy) {
      static_assert(sizeof(pid_t) <= sizeof(int),
                    "pid_t must be storable as int");
      int self = syscall(__NR_gettid);
      while (true) {
        int other = mProxy->mCurrentClient;
        if (other == self) {
          // reentrancy; fail
          mLocked = false;
          return;
        }
        if (other == 0 && mProxy->mCurrentClient.compareExchange(0, self)) {
          mLocked = true;
          break;
        }
        AtomicWait(mProxy->mCurrentClient, other);
      }
    }
    ~CurrentClient() {
      if (mLocked) {
        mProxy->mCurrentClient = 0;
        AtomicWake(mProxy->mCurrentClient, 1);
      }
    }
    operator bool() const { return mLocked; }
  };

  void Perform() {
    static_assert(sizeof(long) == sizeof(intptr_t), "long != intptr_t");
    long result = syscall(mSyscall, mArgs[0], mArgs[1], mArgs[2], mArgs[3],
                          mArgs[4], mArgs[5]);
    mResult = static_cast<long>(result);
  }

  // The proxy thread.
  void Main() {
    while (true) {
      // Wait until WORKING.
      while (true) {
        int status = mStatus;
        MOZ_ASSERT(status != STOPPED);
        if (status == WORKING) {
          break;
        }
        AtomicWait(mStatus, status);
      }
      // Handle request or exit, as appropriate.
      if (mSyscall == __NR_exit) {
	mStatus = STOPPED;
	AtomicWake(mStatus);
	break;
      }
      Perform();
      mStatus = READY;
      AtomicWake(mStatus);
    }
  }

  static void* ThreadStart(void *aPtr) {
    static_cast<UnsafeSyscallProxyImpl*>(aPtr)->Main();
    return nullptr;
  }

  // Shared between Call() and Stop(); see above re __NR_exit.
  bool CallInternal(unsigned long aSyscall, const unsigned long aArgs[6],
                    long *aResult, DebugOnly<int> aExpected) {
    if (mStatus == STOPPED) {
      return false;
    }
    CurrentClient lock(this);
    if (!lock) {
      return false;
    }
    if (mStatus == STOPPED) {
      return false;
    }
    MOZ_ASSERT(mStatus == READY);

    mSyscall = aSyscall;
    memcpy(mArgs, aArgs, sizeof(mArgs));
    mStatus = WORKING;
    AtomicWake(mStatus);
    while (true) {
      int status = mStatus;
      if (status != WORKING) {
        MOZ_ASSERT(status == aExpected);
        break;
      }
      AtomicWait(mStatus, status);
    }
    *aResult = mResult;

    return true;
  }

  static bool IsProxiable(unsigned long aSyscall);

public:
  UnsafeSyscallProxyImpl() : mCurrentClient(0), mStatus(STOPPED) { }

  ~UnsafeSyscallProxyImpl() {
    MOZ_ASSERT(mStatus == STOPPED);
  }

  bool Start(void) {
    MOZ_ASSERT(mStatus == STOPPED);
    mStatus = READY;
    bool ok = pthread_create(&mThread, nullptr,
                             ThreadStart, static_cast<void*>(this)) == 0;
    if (!ok) {
      mStatus = STOPPED;
    }
    return ok;
  }

  bool Call(unsigned long aSyscall, const unsigned long aArgs[6],
	    long *aResult) {
    // Note: the IsProxiable test could be expanded to allow returning
    // an error, if there's a case where that's the best fix.
    return IsProxiable(aSyscall) &&
      CallInternal(aSyscall, aArgs, aResult, READY);
  }

  bool Stop() {
    unsigned long args[6] = { 0, };
    long result; // deliberately unused
    // FIXME: NS_WARN_IF or otherwise indicate what broke.
    // Or just crash, if the failure is unexpected?
    if (!CallInternal(__NR_exit, args, &result, STOPPED)) {
      return false;
    }
    if (pthread_join(mThread, nullptr) != 0) {
      return false;
    }
    // FIXME: pthread_join isn't enough on buggy systems like
    // Android (before L).
    return true;
  }
};

bool
UnsafeSyscallProxyImpl::IsProxiable(unsigned long aSyscall)
{
  // Notes for future reference on syscalls that can be proxied with
  // some fixups, if we need them:
  //
  // * The scheduler parameter calls -- if args[0] == 0, replace it
  //   with the requesting thread's tid.
  //
  // * fork/vfork/(clone without CLONE_VM), by setjmp'ing in the
  //   requesting thread and longjmp'ing back in the child (result == 0).

  switch (aSyscall) {
  case __NR_exit:
  case __NR_prctl:
#ifdef __NR_arch_prctl
  case __NR_arch_prctl:
#endif
  case __NR_rt_sigprocmask:
  case __NR_rt_sigreturn:
#ifdef __NR_sigaction
  case __NR_sigprocmask:
  case __NR_sigreturn:
#endif
  case __NR_sigaltstack:
  case __NR_personality:
  case __NR_getpriority:
  case __NR_setpriority:
  case __NR_sched_setparam:
  case __NR_sched_getparam:
  case __NR_sched_setscheduler:
  case __NR_sched_getscheduler:
  case __NR_sched_get_priority_max:
  case __NR_sched_get_priority_min:
  case __NR_sched_rr_get_interval:
  case __NR_sched_setaffinity:
  case __NR_sched_getaffinity:
#ifdef __NR_sched_setattr
  case __NR_sched_setattr:
  case __NR_sched_getattr:
#endif
  case __NR_clone:
  case __NR_fork:
  case __NR_vfork:
  case __NR_setgroups:
#ifdef __NR_setuid32
  case __NR_setgroups32:
#define CASES_SETXID(stem) \
  case __NR_set##stem##uid: \
  case __NR_set##stem##gid: \
  case __NR_set##stem##uid32: \
  case __NR_set##stem##gid32:
#else
#define CASES_SETXID(stem) \
  case __NR_set##stem##uid: \
  case __NR_set##stem##gid:
#endif
  CASES_SETXID()
  CASES_SETXID(re)
  CASES_SETXID(res)
  CASES_SETXID(fs)
#undef CASES_SETXID
  case __NR_capget:
  case __NR_capset:
  case __NR_gettid:
  case __NR_futex:
#ifdef __NR_set_thread_area
  case __NR_set_thread_area:
  case __NR_get_thread_area:
#endif
  case __NR_set_tid_address:
  case __NR_get_robust_list:
  case __NR_set_robust_list:
  case __NR_unshare:
  case __NR_setns:
    return false;

  default:
    return true;
  }
}

bool
UnsafeSyscallProxy::Start()
{
  if (mImpl) {
    return false;
  }
  mImpl = new UnsafeSyscallProxyImpl();
  return mImpl->Start();
}

bool
UnsafeSyscallProxy::Stop()
{
  // mImpl isn't freed here; Call() can happen later, or concurrently.
  return mImpl && mImpl->Stop();
}

bool
UnsafeSyscallProxy::Call(unsigned long aSyscall, const unsigned long aArgs[6],
                         long *aResult)
{
  return mImpl->Call(aSyscall, aArgs, aResult);
}


} // namespace mozilla
