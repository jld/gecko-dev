/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_gfx_ipc_GPUParent_h__
#define _include_gfx_ipc_GPUParent_h__

#include "mozilla/RefPtr.h"
#include "mozilla/gfx/PGPUParent.h"

namespace mozilla {

class TimeStamp;
class ChildProfilerController;

namespace gfx {

class VsyncBridgeParent;

class GPUParent final : public PGPUParent
{
public:
  GPUParent();
  ~GPUParent();

  static GPUParent* GetSingleton();

  bool Init(base::ProcessId aParentPid,
            const char* aParentBuildID,
            MessageLoop* aIOLoop,
            IPC::Channel* aChannel);
  void NotifyDeviceReset();

  PAPZInputBridgeParent* AllocPAPZInputBridgeParent(const LayersId& aLayersId) override;
  bool DeallocPAPZInputBridgeParent(PAPZInputBridgeParent* aActor) override;

  mozilla::ipc::IPCResult RecvInit(nsTArray<GfxPrefSetting>&& prefs,
                                   nsTArray<GfxVarUpdate>&& vars,
                                   DevicePrefs&& devicePrefs,
                                   nsTArray<LayerTreeIdMapping>&& mappings) override;
  mozilla::ipc::IPCResult RecvInitCompositorManager(Endpoint<PCompositorManagerParent>&& aEndpoint) override;
  mozilla::ipc::IPCResult RecvInitVsyncBridge(Endpoint<PVsyncBridgeParent>&& aVsyncEndpoint) override;
  mozilla::ipc::IPCResult RecvInitImageBridge(Endpoint<PImageBridgeParent>&& aEndpoint) override;
  mozilla::ipc::IPCResult RecvInitVRManager(Endpoint<PVRManagerParent>&& aEndpoint) override;
  mozilla::ipc::IPCResult RecvInitUiCompositorController(LayersId&& aRootLayerTreeId, Endpoint<PUiCompositorControllerParent>&& aEndpoint) override;
  mozilla::ipc::IPCResult RecvInitProfiler(Endpoint<PProfilerChild>&& aEndpoint) override;
  mozilla::ipc::IPCResult RecvUpdatePref(GfxPrefSetting&& pref) override;
  mozilla::ipc::IPCResult RecvUpdateVar(GfxVarUpdate&& pref) override;
  mozilla::ipc::IPCResult RecvNewContentCompositorManager(Endpoint<PCompositorManagerParent>&& aEndpoint) override;
  mozilla::ipc::IPCResult RecvNewContentImageBridge(Endpoint<PImageBridgeParent>&& aEndpoint) override;
  mozilla::ipc::IPCResult RecvNewContentVRManager(Endpoint<PVRManagerParent>&& aEndpoint) override;
  mozilla::ipc::IPCResult RecvNewContentVideoDecoderManager(Endpoint<PVideoDecoderManagerParent>&& aEndpoint) override;
  mozilla::ipc::IPCResult RecvGetDeviceStatus(GPUDeviceData* aOutStatus) override;
  mozilla::ipc::IPCResult RecvSimulateDeviceReset(GPUDeviceData* aOutStatus) override;
  mozilla::ipc::IPCResult RecvAddLayerTreeIdMapping(LayerTreeIdMapping&& aMapping) override;
  mozilla::ipc::IPCResult RecvRemoveLayerTreeIdMapping(LayerTreeIdMapping&& aMapping) override;
  mozilla::ipc::IPCResult RecvNotifyGpuObservers(nsCString&& aTopic) override;
  mozilla::ipc::IPCResult RecvRequestMemoryReport(
    uint32_t&& generation,
    bool&& anonymize,
    bool&& minimizeMemoryUsage,
    MaybeFileDesc&& DMDFile) override;

  void ActorDestroy(ActorDestroyReason aWhy) override;

private:
  const TimeStamp mLaunchTime;
  RefPtr<VsyncBridgeParent> mVsyncBridge;
#ifdef MOZ_GECKO_PROFILER
  RefPtr<ChildProfilerController> mProfilerController;
#endif
};

} // namespace gfx
} // namespace mozilla

#endif // _include_gfx_ipc_GPUParent_h__
