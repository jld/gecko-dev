/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/Atomics.h"
#include "mozilla/Maybe.h"
#include "mozilla/PodOperations.h"
#include "UnsafeSyscallProxy.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace mozilla {

// Name begins with Sandbox for easier test globbing.
class SandboxUnsafeProxyTest : public ::testing::Test
{
  UnsafeSyscallProxy mProxy;
protected:
  virtual void SetUp() MOZ_OVERRIDE {
    ASSERT_TRUE(mProxy.Start());
  }
  virtual void TearDown() MOZ_OVERRIDE {
    ASSERT_TRUE(mProxy.Stop());
  }

  template<class C, void (C::* Main)()>
  static void* ThreadMain(void* arg) {
    (static_cast<C*>(arg)->*Main)();
    return nullptr;
  }

  template<typename A0 = unsigned long,
	   typename A1 = unsigned long,
	   typename A2 = unsigned long,
	   typename A3 = unsigned long,
	   typename A4 = unsigned long,
	   typename A5 = unsigned long>
  Maybe<long> Syscall(unsigned long aSyscall,
		      A0 aArg0 = 0,
		      A1 aArg1 = 0,
		      A2 aArg2 = 0,
		      A3 aArg3 = 0,
		      A4 aArg4 = 0,
		      A5 aArg5 = 0) MOZ_FINAL {
    const unsigned long args[6] = {
      (unsigned long)aArg0,
      (unsigned long)aArg1,
      (unsigned long)aArg2,
      (unsigned long)aArg3,
      (unsigned long)aArg4,
      (unsigned long)aArg5,
    };
    long retval;
    if (mProxy.Call(aSyscall, args, &retval)) {
      return Some(retval);
    } else {
      return Nothing();
    }
  }

  template<class C, void (C::* Main)()>
  void StartThread(pthread_t *aThread) {
    ASSERT_EQ(0, pthread_create(aThread, nullptr, ThreadMain<C, Main>,
                                static_cast<C*>(this)));
  }
  void WaitForThread(pthread_t aThread) {
    void* retval;
    ASSERT_EQ(pthread_join(aThread, &retval), 0);
    ASSERT_EQ(retval, static_cast<void*>(nullptr));
  }

  template<class C, void (C::* Main)()>
  void RunOnManyThreads() {
    static const int kNumThreads = 5;  
    pthread_t threads[kNumThreads];
    for (int i = 0; i < kNumThreads; ++i) {
      StartThread<C, Main>(&threads[i]);
    }
    for (int i = 0; i < kNumThreads; ++i) {
      WaitForThread(threads[i]);
    }
  }

  void CloseFd(int aFd) {
    int rv = close(aFd);
    if (rv < 0 && errno == EINTR) {
      rv = 0;
    }
    ASSERT_EQ(0, rv) << "errno = " << errno;
  }
  
  void SimpleOpenTest();
public:
  void MultiThreadOpenWorker();
};

TEST_F(SandboxUnsafeProxyTest, OpenDevNull)
{
  auto fd = Syscall(__NR_openat, AT_FDCWD, "/dev/null", O_RDONLY);
  ASSERT_TRUE(fd);
  ASSERT_GE(*fd, 0);
  char c;
  EXPECT_EQ(0, read(*fd, &c, 1));
  CloseFd(*fd);
}

TEST_F(SandboxUnsafeProxyTest, Rejections)
{
  ASSERT_FALSE(Syscall(__NR_exit, 0));
  ASSERT_FALSE(Syscall(__NR_gettid));
  ASSERT_FALSE(Syscall(__NR_sigaltstack, nullptr, nullptr));
}

// Sadly, /proc/self is a link to the caller's thread group leader,
// not the caller itself, so it can't be misused for an "is proxy on
// other thread" test.

TEST_F(SandboxUnsafeProxyTest, IsInSameProcess)
{
  auto proxyPid = Syscall(__NR_getpid);
  ASSERT_TRUE(proxyPid);
  EXPECT_EQ(getpid(), *proxyPid);
  // Alternate approach: proxied faccessat on /proc/self/task/N where
  // N is the test's tid.
}

TEST_F(SandboxUnsafeProxyTest, MultiThreadOpen)
{
  RunOnManyThreads<SandboxUnsafeProxyTest,
		   &SandboxUnsafeProxyTest::MultiThreadOpenWorker>();
}
void SandboxUnsafeProxyTest::MultiThreadOpenWorker() {
  static const int kNumLoops = 10000;

  for (int i = 1; i <= kNumLoops; ++i) {
    SimpleOpenTest();
  }
}
void SandboxUnsafeProxyTest::SimpleOpenTest() {
  auto nullfd = Syscall(__NR_openat, AT_FDCWD, "/dev/null", O_RDONLY);
  auto zerofd = Syscall(__NR_openat, AT_FDCWD, "/dev/zero", O_RDONLY);
  ASSERT_TRUE(nullfd);
  ASSERT_TRUE(zerofd);
  ASSERT_GE(*nullfd, 0);
  ASSERT_GE(*zerofd, 0);
  char c;
  ASSERT_EQ(0, read(*nullfd, &c, 1));
  ASSERT_EQ(1, read(*zerofd, &c, 1));
  ASSERT_EQ('\0', c);
  CloseFd(*nullfd);
  CloseFd(*zerofd);
}

class SandboxUnsafeProxyTestWithSignals
  : public SandboxUnsafeProxyTest
{
  static SandboxUnsafeProxyTestWithSignals* sSingleton;
  int mSigNum;
  struct sigaction mOldHandler;

  static void TestSignalHandler(int nr) {
    auto pid = sSingleton->Syscall(__NR_getpid, 0xDEADBEEF, 0xDEADBEEF);
    ASSERT_TRUE(pid);
    ASSERT_EQ(getpid(), *pid);
  }

  static int GetSyscallNum()
  {
    for (int nr = SIGRTMIN; nr <= SIGRTMAX; ++nr) {
      struct sigaction old;
      if (0 != sigaction(nr, nullptr, &old)) {
	MOZ_ASSERT(false);
	continue;
      }
      if ((old.sa_flags & SA_SIGINFO) == 0 &&
	  old.sa_handler == SIG_DFL) {
	return nr;
      }
    }
    return 0;
  }
protected:
  Atomic<int> mDone;

  virtual void SetUp() MOZ_OVERRIDE {
    SandboxUnsafeProxyTest::SetUp();

    ASSERT_FALSE(sSingleton);
    sSingleton = this;

    mSigNum = GetSyscallNum();
    ASSERT_NE(0, mSigNum);
    struct sigaction sa;
    PodZero(&sa);
    sa.sa_handler = TestSignalHandler;
    ASSERT_EQ(0, sigaction(mSigNum, &sa, &mOldHandler));
  }
  virtual void TearDown() MOZ_OVERRIDE {
    ASSERT_EQ(0, sigaction(mSigNum, &mOldHandler, nullptr));

    ASSERT_TRUE(sSingleton);
    sSingleton = nullptr;
    mSigNum = 0;

    SandboxUnsafeProxyTest::TearDown();
  }

  void SendSignalTo(pthread_t aThread) {
    ASSERT_EQ(0, pthread_kill(aThread, mSigNum));
  }
public:
  void OpenUntilDoneWorker();
};

SandboxUnsafeProxyTestWithSignals* SandboxUnsafeProxyTestWithSignals::sSingleton(nullptr);

TEST_F(SandboxUnsafeProxyTestWithSignals, TheTest)
{
  static const int kNumThreads = 5;
  static const int kNumLoops = 60000;
  pthread_t threads[kNumThreads];
  mDone = 0;
  for (int i = 0; i < kNumThreads; ++i) {
    StartThread<SandboxUnsafeProxyTestWithSignals, &SandboxUnsafeProxyTestWithSignals::OpenUntilDoneWorker>(&threads[i]);
  }
  for (int i = 0; i < kNumLoops; ++i) {
    if (i % (kNumThreads + 1) == 0) {
      int fds[2];
      auto rv = Syscall(__NR_pipe, fds);
      ASSERT_TRUE(rv);
      ASSERT_EQ(0, *rv);
      CloseFd(fds[0]);
      CloseFd(fds[1]);
    }
    SendSignalTo(threads[i % kNumThreads]);
  }
  mDone = 1;
  for (int i = 0; i < kNumThreads; ++i) {
    WaitForThread(threads[i]);
  }
}
void SandboxUnsafeProxyTestWithSignals::OpenUntilDoneWorker()
{
  while (mDone == 0) {
    SimpleOpenTest();
  }
}

} // namespace mozilla
