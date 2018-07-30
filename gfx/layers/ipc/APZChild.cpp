/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/APZChild.h"
#include "mozilla/layers/GeckoContentController.h"

#include "mozilla/dom/TabChild.h"
#include "mozilla/layers/APZCCallbackHelper.h"

#include "InputData.h" // for InputData

namespace mozilla {
namespace layers {

APZChild::APZChild(RefPtr<GeckoContentController> aController)
  : mController(aController)
{
  MOZ_ASSERT(mController);
}

APZChild::~APZChild()
{
  if (mController) {
    mController->Destroy();
    mController = nullptr;
  }
}

mozilla::ipc::IPCResult
APZChild::RecvRequestContentRepaint(FrameMetrics&& aFrameMetrics)
{
  MOZ_ASSERT(mController->IsRepaintThread());

  mController->RequestContentRepaint(aFrameMetrics);
  return IPC_OK();
}

mozilla::ipc::IPCResult
APZChild::RecvUpdateOverscrollVelocity(float&& aX, float&& aY, bool&& aIsRootContent)
{
  mController->UpdateOverscrollVelocity(aX, aY, aIsRootContent);
  return IPC_OK();
}

mozilla::ipc::IPCResult
APZChild::RecvUpdateOverscrollOffset(float&& aX, float&& aY, bool&& aIsRootContent)
{
  mController->UpdateOverscrollOffset(aX, aY, aIsRootContent);
  return IPC_OK();
}

mozilla::ipc::IPCResult
APZChild::RecvNotifyMozMouseScrollEvent(ViewID&& aScrollId,
                                        nsString&& aEvent)
{
  mController->NotifyMozMouseScrollEvent(aScrollId, aEvent);
  return IPC_OK();
}

mozilla::ipc::IPCResult
APZChild::RecvNotifyAPZStateChange(ScrollableLayerGuid&& aGuid,
                                   APZStateChange&& aChange,
                                   int&& aArg)
{
  mController->NotifyAPZStateChange(aGuid, aChange, aArg);
  return IPC_OK();
}

mozilla::ipc::IPCResult
APZChild::RecvNotifyFlushComplete()
{
  MOZ_ASSERT(mController->IsRepaintThread());

  mController->NotifyFlushComplete();
  return IPC_OK();
}

mozilla::ipc::IPCResult
APZChild::RecvNotifyAsyncScrollbarDragRejected(ViewID&& aScrollId)
{
  mController->NotifyAsyncScrollbarDragRejected(aScrollId);
  return IPC_OK();
}

mozilla::ipc::IPCResult
APZChild::RecvNotifyAsyncAutoscrollRejected(ViewID&& aScrollId)
{
  mController->NotifyAsyncAutoscrollRejected(aScrollId);
  return IPC_OK();
}

mozilla::ipc::IPCResult
APZChild::RecvDestroy()
{
  // mController->Destroy will be called in the destructor
  PAPZChild::Send__delete__(this);
  return IPC_OK();
}


} // namespace layers
} // namespace mozilla
