/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_APZChild_h
#define mozilla_layers_APZChild_h

#include "mozilla/layers/PAPZChild.h"

namespace mozilla {

namespace layers {

class GeckoContentController;

/**
 * APZChild implements PAPZChild and is used to remote a GeckoContentController
 * that lives in a different process than where APZ lives.
 */
class APZChild final : public PAPZChild
{
public:
  explicit APZChild(RefPtr<GeckoContentController> aController);
  ~APZChild();

  mozilla::ipc::IPCResult RecvRequestContentRepaint(FrameMetrics&& frame) override;

  mozilla::ipc::IPCResult RecvUpdateOverscrollVelocity(float&& aX, float&& aY, bool&& aIsRootContent) override;

  mozilla::ipc::IPCResult RecvUpdateOverscrollOffset(float&& aX, float&& aY, bool&& aIsRootContent) override;

  mozilla::ipc::IPCResult RecvNotifyMozMouseScrollEvent(ViewID&& aScrollId,
                                                        nsString&& aEvent) override;

  mozilla::ipc::IPCResult RecvNotifyAPZStateChange(ScrollableLayerGuid&& aGuid,
                                                   APZStateChange&& aChange,
                                                   int&& aArg) override;

  mozilla::ipc::IPCResult RecvNotifyFlushComplete() override;

  mozilla::ipc::IPCResult RecvNotifyAsyncScrollbarDragRejected(ViewID&& aScrollId) override;

  mozilla::ipc::IPCResult RecvNotifyAsyncAutoscrollRejected(ViewID&& aScrollId) override;

  mozilla::ipc::IPCResult RecvDestroy() override;

private:
  RefPtr<GeckoContentController> mController;
};

} // namespace layers

} // namespace mozilla

#endif // mozilla_layers_APZChild_h
