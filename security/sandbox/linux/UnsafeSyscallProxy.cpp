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
#include "sandbox/linux/seccomp-bpf/syscall.h"

namespace mozilla {

static void AtomicWait(Atomic<int>& aInt, int aVal)
{
  static_assert(sizeof(Atomic<int>) == sizeof(int), "Atomic<int> != int");
  int *uaddr = reinterpret_cast<int*>(&aInt);
  syscall(__NR_futex, uaddr, FUTEX_WAIT, aVal);
}
static void AtomicWake(Atomic<int>& aInt, int aThreads = INT_MAX)
{
  int *uaddr = reinterpret_cast<int*>(&aInt);
  syscall(__NR_futex, uaddr, FUTEX_WAKE, aThreads);
}

class UnsafeSyscallProxyImpl MOZ_FINAL {
  pthread_t mThread;
  mozilla::Atomic<int> mCurrentClient;
  mozilla::Atomic<int> mStatus;
  unsigned long mSyscall, mArgs[6];
  long mResult;

  enum Status {
    READY,
    WORKING,
    STOPPED,
  };

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

  friend class CurrentClient;
  class CurrentClient {
    BlockSignals mBlock;
    UnsafeSyscallProxyImpl* const mProxy;
    bool locked;
  public:
    CurrentClient(UnsafeSyscallProxyImpl *aProxy) : mProxy(aProxy) {
      static_assert(sizeof(pid_t) <= sizeof(int),
                    "pid_t must be storable as int");
      int self = syscall(__NR_gettid);
      while (true) {
        int other = mProxy->mCurrentClient;
        if (other == self) {
          // reentrancy; fail
          locked = false;
          return;
        }
        if (other == 0 && mProxy->mCurrentClient.compareExchange(0, self)) {
          locked = true;
          break;
        }
        AtomicWait(mProxy->mCurrentClient, other);
      }
    }
    ~CurrentClient() {
      if (locked) {
        mProxy->mCurrentClient = 0;
        AtomicWake(mProxy->mCurrentClient);
      }
    }
    operator bool() const { return locked; }
  };

  void Perform() {
    static_assert(sizeof(long) == sizeof(intptr_t), "long != intptr_t");
    long result = sandbox::SandboxSyscall(mSyscall,
                                          mArgs[0],
                                          mArgs[1],
                                          mArgs[2],
                                          mArgs[3],
                                          mArgs[4],
                                          mArgs[5]);
    mResult = static_cast<long>(result);
  }

  static bool IsProxiable(unsigned long aSyscall);

  void Main() {
    while (true) {
      while (true) {
        int status = mStatus;
        MOZ_ASSERT(status != STOPPED);
        if (status == WORKING) {
          break;
        }
        AtomicWait(mStatus, status);
      }
      if (mSyscall == __NR_exit) {
	mStatus = STOPPED;
	AtomicWake(mStatus, 1);
	break;
      }
      Perform();
      mStatus = READY;
      AtomicWake(mStatus, 1);
    }
  }

  static void* ThreadStart(void *aPtr) {
    static_cast<UnsafeSyscallProxyImpl*>(aPtr)->Main();
    return nullptr;
  }

  bool CallInternal(unsigned long aSyscall, const unsigned long aArgs[6],
                    long *aResult, mozilla::DebugOnly<int> aExpected) {
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
    AtomicWake(mStatus, 1);
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
    return CallInternal(__NR_exit, args, &result, STOPPED);
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
  case __NR_capset:
  case __NR_gettid:
  case __NR_futex:
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
