/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/Maybe.h"
#include "UnsafeSyscallProxy.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace mozilla {

// Name begins with Sandbox for easier test globbing.
class SandboxUnsafeSyscallProxyTest : public ::testing::Test
{
  UnsafeSyscallProxy mProxy;
protected:
  virtual void SetUp() MOZ_OVERRIDE {
    ASSERT_TRUE(mProxy.Start());
  }
  virtual void TearDown() MOZ_OVERRIDE {
    ASSERT_TRUE(mProxy.Stop());
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
};

TEST_F(SandboxUnsafeSyscallProxyTest, OpenDevNull)
{
  auto fd = Syscall(__NR_openat, AT_FDCWD, "/dev/null", O_RDONLY);
  ASSERT_TRUE(fd);
  ASSERT_GE(*fd, 0);
  char c;
  EXPECT_EQ(0, read(*fd, &c, 1));
  close(*fd);
}

TEST_F(SandboxUnsafeSyscallProxyTest, Rejections)
{
  ASSERT_FALSE(Syscall(__NR_exit, 0));
  ASSERT_FALSE(Syscall(__NR_gettid));
  ASSERT_FALSE(Syscall(__NR_sigaltstack, nullptr, nullptr));
}

} // namespace mozilla
