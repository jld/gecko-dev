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
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>

#include "SandboxLogging.h"
#include "linux_syscalls.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/NullPtr.h"
#include "sandbox/linux/seccomp-bpf/syscall.h"

namespace mozilla {

static void AtomicWait(Atomic<int>& aInt, int aVal)
{
  static_assert(sizeof(Atomic<int>) == sizeof(int), "Atomic<int> != int");
  int *uaddr = reinterpret_cast<int*>(&aInt);
  sandbox::SandboxSyscall(__NR_futex, uaddr, FUTEX_WAIT, aVal);
}
static void AtomicWake(Atomic<int>& aInt, int aThreads = INT_MAX)
{
  int *uaddr = reinterpret_cast<int*>(&aInt);
  sandbox::SandboxSyscall(__NR_futex, uaddr, FUTEX_WAKE, aThreads);
}

class UnsafeSyscallProxyImpl MOZ_FINAL {
  class Request {
    mozilla::Atomic<int> mStatus;
    enum Status { WAITING, DONE };
  public:
    unsigned long mSyscall, mArgs[6];
    long mResult;

    Request() : mStatus(WAITING) { }

    void Wait() {
      while (mStatus == WAITING) {
        AtomicWait(mStatus, WAITING);
      }
    }
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
      Done();
    }
    void Done() {
      mStatus = DONE;
      AtomicWake(mStatus);
    }
  };

  pthread_t mThread;
  // FIXME: could separate the client->server and server->client wakeups.
  mozilla::Atomic<int> mStatus;
  mozilla::Atomic<Request*> mRequest;

  enum State {
    READY,
    ASKING,
    WORKING,
    STOPPED,
  };

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
      Request *request = mRequest.exchange(nullptr);
      MOZ_ASSERT(request);
      if (request->mSyscall == __NR_exit) {
	mStatus = STOPPED;
	AtomicWake(mStatus);
        request->Done();
	break;
      }
      mStatus = READY;
      AtomicWake(mStatus);
      request->Perform();
    }
  }

  static void* ThreadStart(void *aPtr) {
    static_cast<UnsafeSyscallProxyImpl*>(aPtr)->Main();
    return nullptr;
  }

  bool CallInternal(unsigned long aSyscall, const unsigned long aArgs[6],
                    long *aResult) {
    while (true) {
      int status = mStatus;
      if (status == STOPPED) {
        return false;
      }
      if (status == READY) {
        if (mStatus.compareExchange(READY, ASKING)) {
          break;
        }
        continue;
      }
      AtomicWait(mStatus, status);
    }

    Request request;
    request.mSyscall = aSyscall;
    memcpy(request.mArgs, aArgs, sizeof(request.mArgs));
    MOZ_ASSERT(!mRequest);
    mRequest = &request;
    mStatus = WORKING;
    AtomicWake(mStatus);
    request.Wait();
    *aResult = request.mResult;
    return true;
  }

public:
  UnsafeSyscallProxyImpl() : mStatus(STOPPED), mRequest(nullptr) { }

  ~UnsafeSyscallProxyImpl() {
    MOZ_ASSERT(mStatus == STOPPED);
    MOZ_ASSERT(mRequest == nullptr);
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
    return IsProxiable(aSyscall) && CallInternal(aSyscall, aArgs, aResult);
  }

  void Stop() {
    unsigned long args[6] = { 0, };
    long result;
    CallInternal(__NR_exit, args, &result);
    MOZ_ASSERT(mStatus == STOPPED);
    MOZ_ASSERT(mRequest == nullptr);
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
