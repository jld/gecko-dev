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

#include "mozilla/Assertions.h"
#include "mozilla/NullPtr.h"
#include "sandbox/linux/seccomp-bpf/syscall.h"

namespace mozilla {

class SandboxUnsafeProxyImpl {
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
    static_cast<SandboxUnsafeProxyImpl*>(aPtr)->Main();
    return nullptr;
  }

public:
  SandboxUnsafeProxyImpl() {
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

  ~SandboxUnsafeProxyImpl() {
    MOZ_ASSERT(mState == STOPPED);
    pthread_cond_destroy(&mReady);
    pthread_cond_destroy(&mAsked);
    pthread_cond_destroy(&mAnswered);
    pthread_mutex_destroy(&mMutex);
  }

  void Start(void) {
    MOZ_ASSERT(mState == STOPPED);
    mState = READY;
    pthread_create(&mThread, nullptr, ThreadStart, static_cast<void*>(this));
    // FIXME errors
  }

  bool Call(unsigned long aSyscall, const unsigned long aArgs[6],
	    long *aResult) {
    if (aSyscall == __NR_exit) {
      return false;
    }
    if (pthread_mutex_lock(&mMutex) != 0) {
      return false;
    }
    while (mState != READY) {
      if (mState == STOPPED) {
	pthread_mutex_unlock(&mMutex);
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
    pthread_mutex_unlock(&mMutex);
    return true;
  }

  void Stop() {
    pthread_mutex_lock(&mMutex);
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
    pthread_mutex_unlock(&mMutex);
    pthread_join(mThread, nullptr);
  }
};

static SandboxUnsafeProxyImpl* sProxy;

void
SandboxStartUnsafeProxy()
{
  MOZ_ASSERT(!sProxy);
  sProxy = new SandboxUnsafeProxyImpl();
  sProxy->Start();
}

void
SandboxStopUnsafeProxy()
{
  sProxy->Stop();
}

bool SandboxUnsafeProxy(unsigned long aSyscall, const unsigned long aArgs[6],
                        long *aResult)
{
  return sProxy->Call(aSyscall, aArgs, aResult);
}

} // namespace mozilla
