/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=4 et sw=4 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "mozilla/ipc/MiniTransceiver.h"
#include "chrome/common/ipc_message.h"
#include "chrome/common/ipc_message_utils.h"
#include "base/eintr_wrapper.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Sprintf.h"
#include "mozilla/ScopeExit.h"
#include "nsDebug.h"

#include <sys/types.h>
#include <string.h>
#include <errno.h>

namespace mozilla::ipc {

static const size_t kMaxIOVecSize = 64;
static const size_t kMaxDataSize = 8 * 1024;
static const size_t kMaxNumFds = 16;

MiniTransceiver::MiniTransceiver(int aFd, int aSockType)
    : mFd(aFd), mIsStream(aSockType == SOCK_STREAM) {
#ifdef DEBUG
  int optval;
  socklen_t optlen = sizeof(optval);
  MOZ_ALWAYS_TRUE(getsockopt(aFd, SOL_SOCKET, SO_TYPE, &optval, &optlen) == 0);
  MOZ_ASSERT(optlen == sizeof(optval));
  MOZ_ASSERT(optval == aSockType);
#endif
}

namespace {

/**
 * Initialize the IO vector for sending data and the control buffer for sending
 * FDs.
 */
static void InitMsgHdr(msghdr* aHdr, int aIOVSize, size_t aMaxNumFds) {
  aHdr->msg_name = nullptr;
  aHdr->msg_namelen = 0;
  aHdr->msg_flags = 0;

  // Prepare the IO vector to receive the content of message.
  auto* iov = new iovec[aIOVSize];
  aHdr->msg_iov = iov;
  aHdr->msg_iovlen = aIOVSize;

  // Prepare the control buffer to receive file descriptors.
  const size_t cbufSize = CMSG_SPACE(sizeof(int) * aMaxNumFds);
  auto* cbuf = new char[cbufSize];
  // Avoid valgrind complaints about uninitialized padding (but also,
  // fill with a value that isn't a valid fd, just in case).
  memset(cbuf, 255, cbufSize);
  aHdr->msg_control = cbuf;
  aHdr->msg_controllen = cbufSize;
}

/**
 * Delete resources allocated by InitMsgHdr().
 */
static void DeinitMsgHdr(msghdr* aHdr) {
  delete aHdr->msg_iov;
  delete static_cast<char*>(aHdr->msg_control);
}

static void IOVecDrop(struct iovec* iov, int iovcnt, size_t toDrop) {
  while (toDrop > 0 && iovcnt > 0) {
    size_t toDropHere = std::min(toDrop, iov->iov_len);
    iov->iov_base = static_cast<char*>(iov->iov_base) + toDropHere;
    iov->iov_len -= toDropHere;
    toDrop -= toDropHere;
    ++iov;
    --iovcnt;
  }
}

}  // namespace

void MiniTransceiver::PrepareFDs(msghdr* aHdr, IPC::Message& aMsg) {
  // Set control buffer to send file descriptors of the Message.
  size_t num_fds = aMsg.attached_handles_.Length();

  cmsghdr* cmsg = CMSG_FIRSTHDR(aHdr);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * num_fds);
  for (size_t i = 0; i < num_fds; ++i) {
    reinterpret_cast<int*>(CMSG_DATA(cmsg))[i] =
        aMsg.attached_handles_[i].get();
  }

  // This number will be sent in the header of the message. So, we
  // can check it at the other side.
  aMsg.header()->num_handles = num_fds;
}

size_t MiniTransceiver::PrepareBuffers(msghdr* aHdr, IPC::Message& aMsg) {
  // Set iovec to send for all buffers of the Message.
  iovec* iov = aHdr->msg_iov;
  size_t iovlen = 0;
  size_t bytes_to_send = 0;
  for (Pickle::BufferList::IterImpl iter(aMsg.Buffers()); !iter.Done();
       iter.Advance(aMsg.Buffers(), iter.RemainingInSegment())) {
    char* data = iter.Data();
    size_t size = iter.RemainingInSegment();
    iov[iovlen].iov_base = data;
    iov[iovlen].iov_len = size;
    iovlen++;
    MOZ_ASSERT(iovlen <= kMaxIOVecSize);
    bytes_to_send += size;
  }
  MOZ_ASSERT(bytes_to_send <= kMaxDataSize);
  aHdr->msg_iovlen = iovlen;

  return bytes_to_send;
}

bool MiniTransceiver::Send(IPC::Message& aMsg) {
  auto clean_fdset = MakeScopeExit([&] { aMsg.attached_handles_.Clear(); });

  size_t num_fds = aMsg.attached_handles_.Length();
  msghdr hdr{};
  InitMsgHdr(&hdr, kMaxIOVecSize, num_fds);

  // FIXME why this instead of another ScopeExit
  UniquePtr<msghdr, decltype(&DeinitMsgHdr)> uniq(&hdr, &DeinitMsgHdr);

  PrepareFDs(&hdr, aMsg);
  size_t bytes_to_send = PrepareBuffers(&hdr, aMsg);

  MOZ_ASSERT(bytes_to_send > 0);
  while (bytes_to_send > 0) {
    ssize_t bytes_written = HANDLE_EINTR(sendmsg(mFd, &hdr, 0));

    if (bytes_written < 0) {
      char error[128];
      SprintfLiteral(error, "sendmsg: %s", strerror(errno));
      NS_WARNING(error);
      return false;
    }

    bytes_to_send -= bytes_written;
    if (bytes_to_send > 0 && !mIsStream) {
      MOZ_ASSERT(false, "message too long for non-stream socket");
      return false;
    }

    IOVecDrop(hdr.msg_iov, hdr.msg_iovlen, bytes_written);
    // FIXME also drop leading zero elements, to avoid quadraticness
    // Maybe have Prepare* take UniquePtr<>* for storage,
    // so that msg_iov can be bumped.
  }

  return true;
}

unsigned MiniTransceiver::RecvFDs(msghdr* aHdr, int* aAllFds,
                                  unsigned aMaxFds) {
  if (aHdr->msg_controllen == 0) {
    return 0;
  }
  MOZ_ASSERT(aAllFds, "Got unexpected fds!");
  if (!aAllFds) {
    return 0;
  }

  // FIXME these debug asserts should be stronger.
  unsigned num_all_fds = 0;
  for (cmsghdr* cmsg = CMSG_FIRSTHDR(aHdr); cmsg;
       cmsg = CMSG_NXTHDR(aHdr, cmsg)) {
    MOZ_ASSERT(cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS,
               "Accept only SCM_RIGHTS to receive file descriptors");

    unsigned payload_sz = cmsg->cmsg_len - CMSG_LEN(0);
    MOZ_ASSERT(payload_sz % sizeof(int) == 0);

    // Add fds to |aAllFds|
    unsigned num_part_fds = payload_sz / sizeof(int);
    int* part_fds = reinterpret_cast<int*>(CMSG_DATA(cmsg));
    MOZ_ASSERT(num_all_fds + num_part_fds <= aMaxFds);

    memcpy(aAllFds + num_all_fds, part_fds, num_part_fds * sizeof(int));
    num_all_fds += num_part_fds;
  }
  return num_all_fds;
}

bool MiniTransceiver::RecvData(RecvReq* aReq) {
  msghdr hdr;
  InitMsgHdr(&hdr, 1, aReq->mMaxFds);
  auto guardHdr = MakeScopeExit([&] { DeinitMsgHdr(&hdr); });

  // TODO gtests: large messages, multiple messages sent before recv, etc.

  aReq->mMsgSizeOut = 0;
  aReq->mNumFdsOut = 0;

  size_t readUntil = mIsStream ? aReq->mBufSize : 0;

  do {
    hdr.msg_iov[0].iov_base = aReq->mDataBuf + aReq->mMsgSizeOut;
    hdr.msg_iov[0].iov_len = aReq->mBufSize - aReq->mMsgSizeOut;

    ssize_t bytes_read = HANDLE_EINTR(recvmsg(mFd, &hdr, 0));
    // FIXME also check for TRUNC/CTRUNC.
    if (bytes_read < 0) {
      return false;
    }
    if (bytes_read == 0) {
      break;
    }

    aReq->mMsgSizeOut += static_cast<size_t>(bytes_read);
    MOZ_ASSERT(aReq->mMsgSizeOut <= aReq->mBufSize);

    aReq->mNumFdsOut += RecvFDs(&hdr, aReq->mFdsBuf + aReq->mNumFdsOut,
                                aReq->mMaxFds - aReq->mNumFdsOut);
  } while (aReq->mMsgSizeOut < readUntil);

  MOZ_ASSERT(aReq->mMsgSizeOut <= aReq->mBufSize);
  if (aReq->mMsgSizeOut < aReq->mExpectSize) {
    errno = EPROTO;
    return false;
  }
  return true;
}

bool MiniTransceiver::Recv(UniquePtr<IPC::Message>& aMsg) {
  static constexpr size_t kHeaderSize =
      static_cast<size_t>(IPC::Message::HeaderSize());
  UniquePtr<char[]> databuf = MakeUnique<char[]>(kMaxDataSize);
  int all_fds[kMaxNumFds];

  RecvReq req;
  req.mDataBuf = databuf.get();
  // In stream mode, we can only safely read the header; if we read
  // more, we could read part of another message.  In non-stream mode,
  // we can't get more than one message but we *must* read the whole
  // thing in one call; if it's truncated, the remainder will be lost.
  req.mBufSize = mIsStream ? kHeaderSize : kMaxDataSize;
  req.mExpectSize = kHeaderSize;
  req.mFdsBuf = all_fds;
  req.mMaxFds = kMaxNumFds;

  if (!RecvData(&req)) {
    return false;
  }
  uint32_t expMsgSize =
      IPC::Message::MessageSize(databuf.get(), databuf.get() + kHeaderSize);

  if (mIsStream) {
    MOZ_ASSERT(req.mMsgSizeOut == kHeaderSize);
    // FIXME expand the buffer instead
    MOZ_RELEASE_ASSERT(expMsgSize <= kMaxDataSize);

    RecvReq req2;
    req2.mDataBuf = databuf.get() + kHeaderSize;
    req2.mBufSize = expMsgSize - kHeaderSize;
    req2.mExpectSize = req2.mBufSize;
    if (!RecvData(&req2)) {
      return false;
    }
  } else {
    // Make sure the header matches the data.
    MOZ_RELEASE_ASSERT(expMsgSize == req.mMsgSizeOut);
  }

  // Create Message from databuf & all_fds.
  UniquePtr<IPC::Message> msg =
      MakeUnique<IPC::Message>(databuf.get(), expMsgSize);
  unsigned num_all_fds = req.mNumFdsOut;
  nsTArray<UniqueFileHandle> handles(num_all_fds);
  for (unsigned i = 0; i < num_all_fds; ++i) {
    handles.AppendElement(UniqueFileHandle(all_fds[i]));
  }
  msg->SetAttachedFileHandles(std::move(handles));

  MOZ_ASSERT(msg->header()->num_handles == msg->attached_handles_.Length(),
             "The number of file descriptors in the header is different from"
             " the number actually received");

  aMsg = std::move(msg);
  return true;
}

}  // namespace mozilla::ipc
