/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MachEndpoint.h"

#include "mozilla/Assertions.h"
#include "mozilla/ScopeExit.h"

#include <mach/mach_port.h>
#include <mach/message.h>

#define MACH_TRY(expr)                         \
  do {                                         \
    kern_return_t machTryTempResult_ = (expr); \
    if (machTryTempResult_ != KERN_SUCCESS) {  \
      return machTryTempResult_;               \
    }                                          \
  } while(0)

namespace mozilla {
namespace ipc {

MachEndpoint::MachEndpoint()
: MachEndpoint(pid_t(0))
{ }

MachEndpoint::MachEndpoint(pid_t aRecipient)
: mRecipient(aRecipient)
, mMachOwner(MACH_PORT_NULL)
, mRecvPort(MACH_PORT_NULL)
{ }

MachEndpoint::MachEndpoint(MachEndpoint&& aOther)
{
  if (this == &aOther) {
    return;
  }

  mRecipient = aOther.mRecipient;
  mMachOwner = aOther.mMachOwner;
  mRecvPort = aOther.mRecvPort;

  aOther.mRecipient = 0;
  aOther.mMachOwner = MACH_PORT_NULL;
  aOther.mRecvPort = MACH_PORT_NULL;
}

MachEndpoint::~MachEndpoint()
{
  if (IsCreated()) {
    mach_port_deallocate(mMachOwner, mRecipient);
  }
}

bool
MachEndpoint::IsCreated() const
{
  return mRecvPort != MACH_PORT_NULL;
}

static kern_return_t
ExtractRecvToSend(mach_port_t aTask, mach_port_t aRecv, mach_port_t* aSendOut)
{
  mach_msg_type_name_t type;

  MACH_TRY(mach_port_extract_right(aTask, aRecv,
                                   MACH_MSG_TYPE_MAKE_SEND,
                                   aSendOut, &type));
  if (type != MACH_MSG_TYPE_PORT_SEND) {
    return KERN_FAILURE;
  }
  return KERN_SUCCESS;
}

/* static */ kern_return_t
MachEndpoint::CreateEndpoints(mach_port_t task0,
                              mach_port_t task1,
                              MachEndpoint& end0,
                              MachEndpoint& end1)
{
  MOZ_RELEASE_ASSERT(!end0.IsCreated());
  MOZ_RELEASE_ASSERT(!end1.IsCreated());

  end0.mMachOwner = task0;
  end1.mMachOwner = task1;

  MACH_TRY(mach_port_allocate(task0, MACH_PORT_RIGHT_RECEIVE,
                              &end0.mRecvPort));
  MACH_TRY(mach_port_allocate(task1, MACH_PORT_RIGHT_RECEIVE,
                              &end1.mRecvPort));

  mach_port_t send0, send1;

  MACH_TRY(ExtractRecvToSend(task0, end0.mRecvPort, &send0));
  auto guard0 = mozilla::MakeScopeExit([&]() {
    mach_port_deallocate(mach_task_self(), send0);
  });

  MACH_TRY(ExtractRecvToSend(task1, end1.mRecvPort, &send1));
  auto guard1 = mozilla::MakeScopeExit([&]() {
    mach_port_deallocate(mach_task_self(), send0);
  });

  // Note: MachPortSender doesn't free the port on destruction.
  MachPortSender sender0(send0);
  MachPortSender sender1(send1);

  MachMsgPortDescriptor desc0(send0, MACH_MSG_TYPE_COPY_SEND);
  MachMsgPortDescriptor desc1(send1, MACH_MSG_TYPE_COPY_SEND);

  MachSendMessage msgFor0(/* id: */ 0);
  MachSendMessage msgFor1(/* id: */ 0);

  if (!msgFor0.AddDescriptor(desc1) ||
      !msgFor1.AddDescriptor(desc0)) {
    MOZ_DIAGNOSTIC_ASSERT(false, "adding just one translated port should"
                           " always succeed");
    return KERN_INSUFFICIENT_BUFFER_SIZE;
  }
  
  MACH_TRY(sender0.SendMessage(msgFor0, 0 /* FIXME? */));
  MACH_TRY(sender1.SendMessage(msgFor1, 0 /* FIXME? */));
  
  return KERN_SUCCESS;
}


kern_return_t
MachBridge::Init(MachEndpoint&& aEnd)
{
  MOZ_RELEASE_ASSERT(aEnd.mRecipient == getpid());
  mReceiver.emplace(aEnd.mRecvPort);
  aEnd.mRecvPort = MACH_PORT_NULL;
  
  MachReceiveMessage msg;
  MACH_TRY(mReceiver->WaitForMessage(&msg, 0 /* FIXME? */));
  MOZ_RELEASE_ASSERT(msg.GetDescriptorCount() == 1);

  mSender.emplace(msg.GetTranslatedPort(0));
  return KERN_SUCCESS;
}

MachBridge::~MachBridge()
{
  if (mSender) {
    mach_port_deallocate(mach_task_self(), mSender->GetSendPort());
  }
}

kern_return_t
MachBridge::SendMessage(MachSendMessage& message,
                        mach_msg_timeout_t timeout)
{
  MOZ_RELEASE_ASSERT(mSender);
  return mSender->SendMessage(message, timeout);
}

kern_return_t
MachBridge::WaitForMessage(MachReceiveMessage* out_message,
                           mach_msg_timeout_t timeout)
{
  MOZ_RELEASE_ASSERT(mReceiver);
  return mReceiver->WaitForMessage(out_message, timeout);
}

kern_return_t
MachBridge::SendMessageToSelf(MachSendMessage& message,
                              mach_msg_timeout_t timeout)
{
  MOZ_RELEASE_ASSERT(mReceiver);
  return mReceiver->SendMessageToSelf(message, timeout);
}

/* static */ void
IPDLParamTraits<MachEndpoint>::Write(IPC::Message* aMsg,
                                     IProtocol* aActor,
                                     paramType& aParam)
{
  MOZ_RELEASE_ASSERT(aActor->OtherPid() == aParam.mRecipient);
  MOZ_RELEASE_ASSERT(aParam.IsCreated());
  
  WriteIPDLParam(aMsg, aActor, aParam.mRecvPort);
  aParam.mRecvPort = MACH_PORT_NULL;
}

/* static */ bool
IPDLParamTraits<MachEndpoint>::Read(const IPC::Message* aMsg,
                                    PickleIterator* aIter,
                                    IProtocol* aActor,
                                    paramType* aResult)
{
  // Should be called only on a default-constructed object.
  MOZ_RELEASE_ASSERT(!aResult->IsCreated());

  aResult->mRecipient = getpid();
  aResult->mMachOwner = mach_task_self();
  
  return ReadIPDLParam(aMsg, aIter, aActor, &aResult->mRecvPort);
}

} // namespace ipc
} // namespace mozilla

#undef MACH_TRY
