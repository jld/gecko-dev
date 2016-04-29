/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include <signal.h>

// This file tests symbol interpositions peformed by ../SandboxHooks.cpp

TEST(SandboxInterceptions, UnblockableSIGSYS)
{
  sigset_t set;

  // Add SIGSYS to blocked signals:
  ASSERT_EQ(0, sigemptyset(&set));
  ASSERT_EQ(0, sigaddset(&set, SIGSYS));
  ASSERT_EQ(0, sigprocmask(SIG_BLOCK, &set, nullptr));

  // Read back the mask and assert no SIGSYS:
  ASSERT_EQ(0, sigprocmask(SIG_BLOCK, nullptr, &set));
  EXPECT_EQ(0, sigismember(&set, SIGSYS))
    << "Attempt to block SIGSYS with SIG_BLOCK succeeded!";

  // Try again with SIG_SETMASK, since we already have the current set.
  ASSERT_EQ(0, sigaddset(&set, SIGSYS));
  ASSERT_EQ(0, sigprocmask(SIG_SETMASK, &set, nullptr));
  ASSERT_EQ(0, sigprocmask(SIG_SETMASK, nullptr, &set));
  EXPECT_EQ(0, sigismember(&set, SIGSYS))
    << "Attempt to block SIGSYS with SIG_SETMASK succeeded!";
}
