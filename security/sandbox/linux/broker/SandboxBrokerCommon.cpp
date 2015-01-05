/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxBrokerCommon.h"

#include "mozilla/Assertions.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace mozilla {

/* static */ ssize_t
SandboxBrokerCommon::RecvWithFd(int aFd, const iovec* aIO, size_t aNumIO,
                                    int* aPassedFdPtr)
{
  struct msghdr msg = {};
  msg.msg_iov = const_cast<iovec*>(aIO);
  msg.msg_iovlen = aNumIO;

  char cmsg_buf[CMSG_SPACE(sizeof(int))];
  if (aPassedFdPtr) {
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);
    *aPassedFdPtr = -1;
  }

  ssize_t rv;
  do {
    rv = recvmsg(aFd, &msg, 0);
  } while (rv < 0 && errno == EINTR);

  if (rv <= 0) {
    return rv;
  }
  if (msg.msg_controllen > 0) {
    MOZ_ASSERT(aPassedFdPtr);
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg->cmsg_level == SOL_SOCKET &&
        cmsg->cmsg_type == SCM_RIGHTS) {
      MOZ_ASSERT(cmsg->cmsg_len == CMSG_LEN(sizeof(int)));
      *aPassedFdPtr = *reinterpret_cast<int*>(CMSG_DATA(cmsg));
    }
  }
  if (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
    if (aPassedFdPtr) {
      close(*aPassedFdPtr);
      *aPassedFdPtr = -1;
    }
    errno = EMSGSIZE;
    return -1;
  }

  return rv;
}

/* static */ ssize_t
SandboxBrokerCommon::SendWithFd(int aFd, const iovec* aIO, size_t aNumIO,
                                    int aPassedFd)
{
  struct msghdr msg = {};
  msg.msg_iov = const_cast<iovec*>(aIO);
  msg.msg_iovlen = aNumIO;

  char cmsg_buf[CMSG_SPACE(sizeof(int))];
  if (aPassedFd != -1) {
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    *reinterpret_cast<int*>(CMSG_DATA(cmsg)) = aPassedFd;
  }

  ssize_t rv;
  do {
    rv = sendmsg(aFd, &msg, MSG_NOSIGNAL);
  } while (rv < 0 && errno == EINTR);

  return rv;
}

} // namespace mozilla
