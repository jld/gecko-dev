/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_MachEndpoint_h
#define mozilla_ipc_MachEndpoint_h

#include <mach/port.h>
#include <unistd.h>

#include "mozilla/ipc/IPDLParamTraits.h"
#include "mozilla/Maybe.h"
#include "chrome/common/mach_ipc_mac.h"

namespace mozilla {
namespace ipc {

class IProtocol;

#ifdef XP_DARWIN

class MachEndpoint {
public:
  MachEndpoint(); // For IPDL; sigh.
  explicit MachEndpoint(pid_t aRecipient);
  MachEndpoint(MachEndpoint&&);
  ~MachEndpoint();

  static kern_return_t CreateEndpoints(mach_port_t task0,
                                       mach_port_t task1,
                                       MachEndpoint& end0,
                                       MachEndpoint& end1);
  bool IsCreated() const;

private:
  pid_t mRecipient;
  mach_port_t mMachOwner; // Weak reference; must outlive object.
  mach_port_t mRecvPort; // Strong reference.

  friend class MachBridge;
  friend struct IPDLParamTraits<MachEndpoint>;

  MachEndpoint(const MachEndpoint&) = delete;
  MachEndpoint& operator=(const MachEndpoint&) = delete;
};

template<>
struct IPDLParamTraits<MachEndpoint> {
  typedef MachEndpoint paramType;

  static void Write(IPC::Message* aMsg, IProtocol* aActor, paramType& aParam);
  static bool Read(const IPC::Message* aMsg, PickleIterator* aIter, IProtocol* aActor, paramType* aResult);
};

class MachBridge {
public:
  MachBridge() = default;
  kern_return_t Init(MachEndpoint&&);

  MachBridge(MachBridge&&) = default;
  ~MachBridge();

  kern_return_t SendMessage(MachSendMessage &message,
                            mach_msg_timeout_t timeout);

  kern_return_t WaitForMessage(MachReceiveMessage *out_message,
                               mach_msg_timeout_t timeout);

private:
  Maybe<ReceivePort> mReceiver;
  Maybe<MachPortSender> mSender;
};

#else
// Dummy MachEndpoint for non-Mac, to avoid ifdefs in IPDL

class MachEndpoint {
public:
  MachEndpoint() = default;
  MachEndpoint(MachEndpoint&&) = default;
  ~MachEndpoint = default;

private:
  MachEndpoint(const MachEndpoint&) = delete;
  MachEndpoint& operator=(const MachEndpoint&) = delete;
};

template<>
struct IPDLParamTraits<MachEndpoint> {
  typedef MachEndpoint paramType;

  static void Write(IPC::Message* aMsg, IProtocol* aActor, paramType& aParam) {
    /* nothing */
  }

  static bool Read(const IPC::Message* aMsg, PickleIterator* aIter, IProtocol* aActor, paramType* aResult) {
    return true;
  }
};

#endif // XP_DARWIN

} // namespace ipc
} // namespace mozilla

#endif // mozilla_ipc_MachEndpoint_h
