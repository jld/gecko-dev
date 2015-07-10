/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SandboxBrokerTypes_h
#define mozilla_SandboxBrokerTypes_h

#include <sys/types.h>

struct iovec;

namespace mozilla {

class SandboxBrokerCommon {
public:
  enum Operation {
    SANDBOX_FILE_OPEN,
    SANDBOX_FILE_ACCESS,
    SANDBOX_FILE_STAT,
  };

  struct Request {
    Operation mOp;
    // For open, flags; for access, "mode"; for stat, O_NOFOLLOW for lstat.
    int mFlags;
    // The rest of the packet is the pathname.
    // SCM_RIGHTS for response socket attached.
  };

  struct Response {
    int mError; // errno, or 0 for no error
    // Followed by struct stat for stat/lstat.
    // SCM_RIGHTS attached for successful open.
  };

  // FIXME: what's the right number?
  static const size_t kMaxPathLen = 4096;

  static ssize_t RecvWithFd(int aFd, const iovec* aIO, size_t aNumIO,
                            int* aPassedFdPtr);
  static ssize_t SendWithFd(int aFd, const iovec* aIO, size_t aNumIO,
                            int aPassedFd);
};

} // namespace mozilla

#endif // mozilla_SandboxBrokerTypes_h
