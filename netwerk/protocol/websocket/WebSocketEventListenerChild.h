/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_WebSocketEventListenerChild_h
#define mozilla_net_WebSocketEventListenerChild_h

#include "mozilla/net/NeckoTargetHolder.h"
#include "mozilla/net/PWebSocketEventListenerChild.h"

namespace mozilla {
namespace net {

class WebSocketEventService;

class WebSocketEventListenerChild final : public PWebSocketEventListenerChild
                                        , public NeckoTargetHolder
{
public:
  NS_INLINE_DECL_REFCOUNTING(WebSocketEventListenerChild)

  explicit WebSocketEventListenerChild(uint64_t aInnerWindowID,
                                       nsIEventTarget* aTarget);

  mozilla::ipc::IPCResult RecvWebSocketCreated(uint32_t&& aWebSocketSerialID,
                                               nsString&& aURI,
                                               nsCString&& aProtocols) override;

  mozilla::ipc::IPCResult RecvWebSocketOpened(uint32_t&& aWebSocketSerialID,
                                              nsString&& aEffectiveURI,
                                              nsCString&& aProtocols,
                                              nsCString&& aExtensions) override;

  mozilla::ipc::IPCResult RecvWebSocketMessageAvailable(uint32_t&& aWebSocketSerialID,
                                                        nsCString&& aData,
                                                        uint16_t&& aMessageType) override;

  mozilla::ipc::IPCResult RecvWebSocketClosed(uint32_t&& aWebSocketSerialID,
                                              bool&& aWasClean,
                                              uint16_t&& aCode,
                                              nsString&& aReason) override;

  mozilla::ipc::IPCResult RecvFrameReceived(uint32_t&& aWebSocketSerialID,
                                            WebSocketFrameData&& aFrameData) override;

  mozilla::ipc::IPCResult RecvFrameSent(uint32_t&& aWebSocketSerialID,
                                        WebSocketFrameData&& aFrameData) override;

  void Close();

private:
  ~WebSocketEventListenerChild();

  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  RefPtr<WebSocketEventService> mService;
  uint64_t mInnerWindowID;
};

} // namespace net
} // namespace mozilla

#endif // mozilla_net_WebSocketEventListenerChild_h
