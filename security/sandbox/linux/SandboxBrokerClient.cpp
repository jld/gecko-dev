/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxBrokerClient.h"
#include "SandboxLogging.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mozilla/Assertions.h"
#include "mozilla/NullPtr.h"

namespace mozilla {

SandboxBrokerClient::SandboxBrokerClient(int aFd)
: mFileDesc(aFd)
{
}

SandboxBrokerClient::~SandboxBrokerClient()
{
  close(mFileDesc);
}

int
SandboxBrokerClient::DoCall(const Request *aReq, const char *aPath,
				struct stat *aStat, int *aOpenedFd)
{
  struct iovec ios[2];
  int respFds[2];

  ios[0].iov_base = const_cast<Request*>(aReq);
  ios[0].iov_len = sizeof(*aReq);
  ios[1].iov_base = const_cast<char*>(aPath);
  ios[1].iov_len = strlen(aPath);
  if (ios[1].iov_len > kMaxPathLen) {
    errno = ENAMETOOLONG;
    return -1;
  }

  socketpair(AF_UNIX, SOCK_SEQPACKET, 0, respFds);
  const ssize_t sent = SendWithFd(mFileDesc, ios, 2, respFds[1]);
  MOZ_ASSERT(sent < 0 ||
	     static_cast<size_t>(sent) == ios[0].iov_len + ios[1].iov_len);
  close(respFds[1]);
  if (sent < 0) {
    close(respFds[0]);
    return -1;
  }

  Response resp;
  ios[0].iov_base = &resp;
  ios[0].iov_len = sizeof(resp);
  if (aStat) {
    ios[1].iov_base = aStat;
    ios[1].iov_len = sizeof(*aStat);
  } else {
    ios[1].iov_base = nullptr;
    ios[1].iov_len = 0;
  }

  const ssize_t recvd = RecvWithFd(respFds[0], ios, aStat ? 2 : 1, aOpenedFd);
  const int recvErrno = errno;
  close(respFds[0]);
  if (recvd < 0) {
    errno = recvErrno;
    return -1;
  }
  if (recvd == 0) {
    SANDBOX_LOG_ERROR("Unexpected EOF, op %d flags 0%o path %s",
                      aReq->mOp, aReq->mFlags, aPath);
    errno = EIO;
    return -1;
  }
  if (resp.mError != 0) {
    // If the operation fails, the return payload will be empty;
    // adjust the iov_len for the following assertion.
    ios[1].iov_len = 0;
  }
  MOZ_ASSERT(static_cast<size_t>(recvd) == ios[0].iov_len + ios[1].iov_len);
  if (resp.mError == 0) {
    return 0;
  }
  // FIXME: verbose?
  SANDBOX_LOG_ERROR("Rejected errno %d op %d flags 0%o path %s",
                    resp.mError, aReq->mOp, aReq->mFlags, aPath);
  if (aOpenedFd && *aOpenedFd >= 0) {
    close(*aOpenedFd);
    *aOpenedFd = -1;
  }
  errno = resp.mError;
  return -1;
}

int
SandboxBrokerClient::Open(const char* aPath, int aFlags)
{
  Request req = { SANDBOX_FILE_OPEN, aFlags };
  int openedFd;

  if (DoCall(&req, aPath, nullptr, &openedFd) < 0) {
    MOZ_ASSERT(openedFd < 0);
    return -1;
  }
  return openedFd;
}

int
SandboxBrokerClient::Access(const char* aPath, int aMode)
{
  Request req = { SANDBOX_FILE_ACCESS, aMode };
  return DoCall(&req, aPath, nullptr, nullptr);
}

int
SandboxBrokerClient::Stat(const char* aPath, struct stat* aStat)
{
  Request req = { SANDBOX_FILE_STAT, 0 };
  return DoCall(&req, aPath, aStat, nullptr);
}

int
SandboxBrokerClient::LStat(const char* aPath, struct stat* aStat)
{
  Request req = { SANDBOX_FILE_STAT, O_NOFOLLOW };
  return DoCall(&req, aPath, aStat, nullptr);
}

} // namespace mozilla

