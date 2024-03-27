/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=4 et sw=4 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef __MINITRANSCEIVER_H_
#define __MINITRANSCEIVER_H_

#include "chrome/common/ipc_message.h"
#include "mozilla/Assertions.h"

#include <sys/socket.h>

namespace mozilla {
namespace ipc {

/**
 * This simple implementation handles the transmissions of IPC
 * messages.
 *
 * It works according to a strict request-response paradigm, no
 * concurrent messaging is allowed. Sending a message from A to B must
 * be followed by another one from B to A. Because of this we don't
 * need to handle data crossing the boundaries of a
 * message. Transmission is done via blocking I/O to avoid the
 * complexity of asynchronous I/O.
 */
class MiniTransceiver {
 public:
  /**
   * \param aFd should be a blocking, no O_NONBLOCK, fd.
   */
  explicit MiniTransceiver(int aFd, int aSockType = SOCK_STREAM);

  bool Send(IPC::Message& aMsg);
  inline bool SendInfallible(IPC::Message& aMsg, const char* aCrashMessage) {
    bool Ok = Send(aMsg);
    if (!Ok) {
      MOZ_CRASH_UNSAFE(aCrashMessage);
    }
    return Ok;
  }

  /**
   * \param aMsg will hold the content of the received message.
   * \return false if the fd is closed or with an error.
   */
  bool Recv(UniquePtr<IPC::Message>& aMsg);
  inline bool RecvInfallible(UniquePtr<IPC::Message>& aMsg,
                             const char* aCrashMessage) {
    bool Ok = Recv(aMsg);
    if (!Ok) {
      MOZ_CRASH_UNSAFE(aCrashMessage);
    }
    return Ok;
  }

  int GetFD() { return mFd; }

 private:
  /**
   * Set control buffer to make file descriptors ready to be sent
   * through a socket.
   */
  void PrepareFDs(msghdr* aHdr, IPC::Message& aMsg);
  /**
   * Collect buffers of the message and make them ready to be sent.
   *
   * \param aHdr is the structure going to be passed to sendmsg().
   * \param aMsg is the Message to send.
   */
  size_t PrepareBuffers(msghdr* aHdr, IPC::Message& aMsg);
  /**
   * Collect file descriptors received.
   *
   * \param aAllFds is where to store file descriptors.
   * \param aMaxFds is how many file descriptors can be stored in aAllFds.
   * \return the number of received file descriptors.
   */
  unsigned RecvFDs(msghdr* aHdr, int* aAllFds, unsigned aMaxFds);

  struct RecvReq {
    /** Where to store the data from the socket. */
    char* mDataBuf = nullptr;
    /**
     * The size of the buffer.
     *
     * In stream mode, RecvData will loop until this much has been
     * read or an error occurs.  In non-stream mode, it will attempt
     * to read up to this amount, but recvmsg will be called only once.
     */
    size_t mBufSize = 0;
    /** Return an error unless at least this many bytes are read. */
    size_t mExpectSize = 0;
    /** The number of bytes read from the socket. */
    uint32_t mMsgSizeOut = 0;

    /** The buffer to return file descriptors received. */
    int* mFdsBuf = nullptr;
    /** The size of the file descriptor buffer. */
    unsigned mMaxFds = 0;
    /** The number of file descriptors that were received. */
    unsigned mNumFdsOut = 0;
  };

  /**
   * Receive data from the socket.
   */
  bool RecvData(RecvReq* aReq);

  int mFd;  // The file descriptor of the socket for IPC.
  bool mIsStream;
};

}  // namespace ipc
}  // namespace mozilla

#endif  // __MINITRANSCEIVER_H_
