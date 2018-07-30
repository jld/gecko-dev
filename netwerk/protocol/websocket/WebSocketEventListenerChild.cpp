/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebSocketEventListenerChild.h"

#include "WebSocketEventService.h"
#include "WebSocketFrame.h"

namespace mozilla {
namespace net {

WebSocketEventListenerChild::WebSocketEventListenerChild(uint64_t aInnerWindowID,
                                                         nsIEventTarget* aTarget)
  : NeckoTargetHolder(aTarget)
  , mService(WebSocketEventService::GetOrCreate())
  , mInnerWindowID(aInnerWindowID)
{}

WebSocketEventListenerChild::~WebSocketEventListenerChild()
{
  MOZ_ASSERT(!mService);
}

mozilla::ipc::IPCResult
WebSocketEventListenerChild::RecvWebSocketCreated(uint32_t&& aWebSocketSerialID,
                                                  nsString&& aURI,
                                                  nsCString&& aProtocols)
{
  if (mService) {
    nsCOMPtr<nsIEventTarget> target = GetNeckoTarget();
    mService->WebSocketCreated(aWebSocketSerialID, mInnerWindowID, aURI,
                               aProtocols, target);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult
WebSocketEventListenerChild::RecvWebSocketOpened(uint32_t&& aWebSocketSerialID,
                                                 nsString&& aEffectiveURI,
                                                 nsCString&& aProtocols,
                                                 nsCString&& aExtensions)
{
  if (mService) {
    nsCOMPtr<nsIEventTarget> target = GetNeckoTarget();
    mService->WebSocketOpened(aWebSocketSerialID, mInnerWindowID,
                              aEffectiveURI, aProtocols, aExtensions, target);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult
WebSocketEventListenerChild::RecvWebSocketMessageAvailable(uint32_t&& aWebSocketSerialID,
                                                           nsCString&& aData,
                                                           uint16_t&& aMessageType)
{
  if (mService) {
    nsCOMPtr<nsIEventTarget> target = GetNeckoTarget();
    mService->WebSocketMessageAvailable(aWebSocketSerialID, mInnerWindowID,
                                        aData, aMessageType, target);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult
WebSocketEventListenerChild::RecvWebSocketClosed(uint32_t&& aWebSocketSerialID,
                                                 bool&& aWasClean,
                                                 uint16_t&& aCode,
                                                 nsString&& aReason)
{
  if (mService) {
    nsCOMPtr<nsIEventTarget> target = GetNeckoTarget();
    mService->WebSocketClosed(aWebSocketSerialID, mInnerWindowID,
                              aWasClean, aCode, aReason, target);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult
WebSocketEventListenerChild::RecvFrameReceived(uint32_t&& aWebSocketSerialID,
                                               WebSocketFrameData&& aFrameData)
{
  if (mService) {
    nsCOMPtr<nsIEventTarget> target = GetNeckoTarget();
    RefPtr<WebSocketFrame> frame = new WebSocketFrame(aFrameData);
    mService->FrameReceived(aWebSocketSerialID, mInnerWindowID,
                            frame.forget(), target);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult
WebSocketEventListenerChild::RecvFrameSent(uint32_t&& aWebSocketSerialID,
                                           WebSocketFrameData&& aFrameData)
{
  if (mService) {
    nsCOMPtr<nsIEventTarget> target = GetNeckoTarget();
    RefPtr<WebSocketFrame> frame = new WebSocketFrame(aFrameData);
    mService->FrameSent(aWebSocketSerialID, mInnerWindowID,
                        frame.forget(), target);
  }

  return IPC_OK();
}

void
WebSocketEventListenerChild::Close()
{
  mService = nullptr;
  SendClose();
}

void
WebSocketEventListenerChild::ActorDestroy(ActorDestroyReason aWhy)
{
  mService = nullptr;
}

} // namespace net
} // namespace mozilla
