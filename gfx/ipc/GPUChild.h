/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_mozilla_gfx_ipc_GPUChild_h_
#define _include_mozilla_gfx_ipc_GPUChild_h_

#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/PGPUChild.h"
#include "mozilla/gfx/gfxVarReceiver.h"

namespace mozilla {

namespace ipc {
class CrashReporterHost;
} // namespace ipc
namespace dom {
class MemoryReportRequestHost;
} // namespace dom
namespace gfx {

class GPUProcessHost;

class GPUChild final
  : public PGPUChild
  , public gfxVarReceiver
{
  typedef mozilla::dom::MemoryReportRequestHost MemoryReportRequestHost;

public:
  explicit GPUChild(GPUProcessHost* aHost);
  ~GPUChild();

  void Init();

  bool EnsureGPUReady();

  PAPZInputBridgeChild* AllocPAPZInputBridgeChild(const LayersId& aLayersId) override;
  bool DeallocPAPZInputBridgeChild(PAPZInputBridgeChild* aActor) override;

  // gfxVarReceiver overrides.
  void OnVarChanged(const GfxVarUpdate& aVar) override;

  // PGPUChild overrides.
  mozilla::ipc::IPCResult RecvInitComplete(GPUDeviceData&& aData) override;
  mozilla::ipc::IPCResult RecvReportCheckerboard(uint32_t&& aSeverity, nsCString&& aLog) override;
  mozilla::ipc::IPCResult RecvInitCrashReporter(Shmem&& shmem, NativeThreadId&& aThreadId) override;

  mozilla::ipc::IPCResult RecvAccumulateChildHistograms(InfallibleTArray<HistogramAccumulation>&& aAccumulations) override;
  mozilla::ipc::IPCResult RecvAccumulateChildKeyedHistograms(InfallibleTArray<KeyedHistogramAccumulation>&& aAccumulations) override;
  mozilla::ipc::IPCResult RecvUpdateChildScalars(InfallibleTArray<ScalarAction>&& aScalarActions) override;
  mozilla::ipc::IPCResult RecvUpdateChildKeyedScalars(InfallibleTArray<KeyedScalarAction>&& aScalarActions) override;
  mozilla::ipc::IPCResult RecvRecordChildEvents(nsTArray<ChildEventData>&& events) override;
  mozilla::ipc::IPCResult RecvRecordDiscardedData(DiscardedData&& aDiscardedData) override;

  void ActorDestroy(ActorDestroyReason aWhy) override;
  mozilla::ipc::IPCResult RecvGraphicsError(nsCString&& aError) override;
  mozilla::ipc::IPCResult RecvNotifyUiObservers(nsCString&& aTopic) override;
  mozilla::ipc::IPCResult RecvNotifyDeviceReset(GPUDeviceData&& aData) override;
  mozilla::ipc::IPCResult RecvAddMemoryReport(MemoryReport&& aReport) override;
  mozilla::ipc::IPCResult RecvFinishMemoryReport(uint32_t&& aGeneration) override;
  mozilla::ipc::IPCResult RecvUpdateFeature(Feature&& aFeature, FeatureFailure&& aChange) override;
  mozilla::ipc::IPCResult RecvUsedFallback(Fallback&& aFallback, nsCString&& aMessage) override;
  mozilla::ipc::IPCResult RecvBHRThreadHang(HangDetails&& aDetails) override;

  bool SendRequestMemoryReport(const uint32_t& aGeneration,
                               const bool& aAnonymize,
                               const bool& aMinimizeMemoryUsage,
                               const MaybeFileDesc& aDMDFile);

  static void Destroy(UniquePtr<GPUChild>&& aChild);

private:
  GPUProcessHost* mHost;
  UniquePtr<ipc::CrashReporterHost> mCrashReporter;
  UniquePtr<MemoryReportRequestHost> mMemoryReportRequest;
  bool mGPUReady;
};

} // namespace gfx
} // namespace mozilla

#endif // _include_mozilla_gfx_ipc_GPUChild_h_
