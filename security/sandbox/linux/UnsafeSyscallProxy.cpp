/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "UnsafeSyscallProxy.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>

#include "SandboxLogging.h"
#include "linux_syscalls.h"
#include "mozilla/Assertions.h"
#include "mozilla/NullPtr.h"
#include "sandbox/linux/seccomp-bpf/syscall.h"

namespace mozilla {

class UnsafeSyscallProxyImpl MOZ_FINAL {
  pthread_t mThread;
  pthread_mutex_t mMutex;
  pthread_cond_t mReady, mAsked, mAnswered;
  enum State {
    READY,
    ASKED,
    ANSWERED,
    STOPPED
  };
  State mState;
  unsigned long mSyscall, mArgs[6];
  long mResult;

  static bool IsProxiable(unsigned long aSyscall);

  class AutoLockNoSignals MOZ_FINAL {
    pthread_mutex_t* const mMutexPtr;
    sigset_t mSigSet;
    int mError;
  public:
    explicit AutoLockNoSignals(pthread_mutex_t* aMutexPtr)
    : mMutexPtr(aMutexPtr) {
      sigset_t allSigs;
      sigfillset(&allSigs);
      // If we block SIGSYS, a seccomp trap will unblock it and also
      // reset its disposition; i.e., the process will be immediately
      // killed instead of giving us a chance to report the error that
      // resulted in the unexpected reentrancy.
      sigdelset(&allSigs, SIGSYS);
      sigprocmask(SIG_SETMASK, &allSigs, &mSigSet);
      mError = pthread_mutex_lock(mMutexPtr);
    }
    ~AutoLockNoSignals() {
      pthread_mutex_unlock(mMutexPtr);
      sigprocmask(SIG_SETMASK, &mSigSet, nullptr);
    }
    operator bool() {
      return mError == 0;
    }
  };

  void Main() {
    pthread_mutex_lock(&mMutex);
    while (true) {
      while (mState != ASKED) {
	pthread_cond_wait(&mAsked, &mMutex);
      }
      if (mSyscall == __NR_exit) {
	mState = STOPPED;
	pthread_cond_broadcast(&mReady);
	break;
      }
      static_assert(sizeof(long) == sizeof(intptr_t), "long != intptr_t");
      long result = sandbox::SandboxSyscall(mSyscall,
					    mArgs[0],
					    mArgs[1],
					    mArgs[2],
					    mArgs[3],
					    mArgs[4],
					    mArgs[5]);
      mResult = static_cast<long>(result);
      mState = ANSWERED;
      pthread_cond_signal(&mAnswered);
    }
    pthread_mutex_unlock(&mMutex);
  }

  static void* ThreadStart(void *aPtr) {
    static_cast<UnsafeSyscallProxyImpl*>(aPtr)->Main();
    return nullptr;
  }

public:
  UnsafeSyscallProxyImpl() {
    mState = STOPPED;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&mMutex, &attr);
    pthread_mutexattr_destroy(&attr);
    pthread_cond_init(&mReady, nullptr);
    pthread_cond_init(&mAsked, nullptr);
    pthread_cond_init(&mAnswered, nullptr);
  }

  ~UnsafeSyscallProxyImpl() {
    MOZ_ASSERT(mState == STOPPED);
    pthread_cond_destroy(&mReady);
    pthread_cond_destroy(&mAsked);
    pthread_cond_destroy(&mAnswered);
    pthread_mutex_destroy(&mMutex);
  }

  bool Start(void) {
    MOZ_ASSERT(mState == STOPPED);
    mState = READY;
    bool ok = pthread_create(&mThread, nullptr,
                             ThreadStart, static_cast<void*>(this)) == 0;
    if (!ok) {
      mState = STOPPED;
    }
    return ok;
  }

  bool Call(unsigned long aSyscall, const unsigned long aArgs[6],
	    long *aResult) {
    if (!IsProxiable(aSyscall)) {
      // NOTE: returning an error code is also possible, if there's a
      // case that can't be dealt with any other way.
      return false;
    }

    AutoLockNoSignals lock(&mMutex);
    if (!lock) {
      // FIXME: shouldn't reentrancy be impossible now?
      return false;
    }
    while (mState != READY) {
      if (mState == STOPPED) {
	return false;
      }
      pthread_cond_wait(&mReady, &mMutex);
    }
    mSyscall = aSyscall;
    memcpy(mArgs, aArgs, sizeof(mArgs));
    mState = ASKED;
    pthread_cond_signal(&mAsked);
    while (mState == ASKED) {
      pthread_cond_wait(&mAnswered, &mMutex);
    }
    MOZ_ASSERT(mState == ANSWERED);
    *aResult = mResult;
    mState = READY;
    pthread_cond_broadcast(&mReady);
    return true;
  }

  void Stop() {
    {
      AutoLockNoSignals lock(&mMutex);
      MOZ_ASSERT(lock);
      while (mState != READY) {
        MOZ_ASSERT(mState != STOPPED);
        pthread_cond_wait(&mReady, &mMutex);
      }
      mSyscall = __NR_exit;
      mState = ASKED;
      pthread_cond_signal(&mAsked);
      while (mState != STOPPED) {
        pthread_cond_wait(&mReady, &mMutex);
      }
    }
    pthread_join(mThread, nullptr);
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
  if (!mImpl) {
    return false;
  }
  mImpl->Stop();
  // Not freed, because Call() can still happen.
  return true;
}

bool
UnsafeSyscallProxy::Call(unsigned long aSyscall, const unsigned long aArgs[6],
                         long *aResult)
{
  return mImpl->Call(aSyscall, aArgs, aResult);
}


} // namespace mozilla
