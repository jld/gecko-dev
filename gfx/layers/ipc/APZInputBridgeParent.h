/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_APZInputBridgeParent_h
#define mozilla_layers_APZInputBridgeParent_h

#include "mozilla/layers/PAPZInputBridgeParent.h"

namespace mozilla {
namespace layers {

class IAPZCTreeManager;

class APZInputBridgeParent : public PAPZInputBridgeParent
{
  NS_INLINE_DECL_REFCOUNTING(APZInputBridgeParent)

public:
  explicit APZInputBridgeParent(const LayersId& aLayersId);

  mozilla::ipc::IPCResult
  RecvReceiveMultiTouchInputEvent(
          MultiTouchInput&& aEvent,
          nsEventStatus* aOutStatus,
          MultiTouchInput* aOutEvent,
          ScrollableLayerGuid* aOutTargetGuid,
          uint64_t* aOutInputBlockId) override;

  mozilla::ipc::IPCResult
  RecvReceiveMouseInputEvent(
          MouseInput&& aEvent,
          nsEventStatus* aOutStatus,
          MouseInput* aOutEvent,
          ScrollableLayerGuid* aOutTargetGuid,
          uint64_t* aOutInputBlockId) override;

  mozilla::ipc::IPCResult
  RecvReceivePanGestureInputEvent(
          PanGestureInput&& aEvent,
          nsEventStatus* aOutStatus,
          PanGestureInput* aOutEvent,
          ScrollableLayerGuid* aOutTargetGuid,
          uint64_t* aOutInputBlockId) override;

  mozilla::ipc::IPCResult
  RecvReceivePinchGestureInputEvent(
          PinchGestureInput&& aEvent,
          nsEventStatus* aOutStatus,
          PinchGestureInput* aOutEvent,
          ScrollableLayerGuid* aOutTargetGuid,
          uint64_t* aOutInputBlockId) override;

  mozilla::ipc::IPCResult
  RecvReceiveTapGestureInputEvent(
          TapGestureInput&& aEvent,
          nsEventStatus* aOutStatus,
          TapGestureInput* aOutEvent,
          ScrollableLayerGuid* aOutTargetGuid,
          uint64_t* aOutInputBlockId) override;

  mozilla::ipc::IPCResult
  RecvReceiveScrollWheelInputEvent(
          ScrollWheelInput&& aEvent,
          nsEventStatus* aOutStatus,
          ScrollWheelInput* aOutEvent,
          ScrollableLayerGuid* aOutTargetGuid,
          uint64_t* aOutInputBlockId) override;

  mozilla::ipc::IPCResult
  RecvReceiveKeyboardInputEvent(
          KeyboardInput&& aEvent,
          nsEventStatus* aOutStatus,
          KeyboardInput* aOutEvent,
          ScrollableLayerGuid* aOutTargetGuid,
          uint64_t* aOutInputBlockId) override;

  mozilla::ipc::IPCResult
  RecvUpdateWheelTransaction(
          LayoutDeviceIntPoint&& aRefPoint,
          EventMessage&& aEventMessage) override;

  mozilla::ipc::IPCResult
  RecvProcessUnhandledEvent(
          LayoutDeviceIntPoint&& aRefPoint,
          LayoutDeviceIntPoint* aOutRefPoint,
          ScrollableLayerGuid*  aOutTargetGuid,
          uint64_t*             aOutFocusSequenceNumber) override;

  void
  ActorDestroy(ActorDestroyReason aWhy) override;

protected:
  virtual ~APZInputBridgeParent();

private:
  RefPtr<IAPZCTreeManager> mTreeManager;
};

} // namespace layers
} // namespace mozilla

#endif // mozilla_layers_APZInputBridgeParent_h
