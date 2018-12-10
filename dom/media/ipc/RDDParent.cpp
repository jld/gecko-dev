/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "RDDParent.h"

#include "mozilla/dom/MemoryReportRequest.h"
#include "mozilla/ipc/CrashReporterHost.h"

#ifdef MOZ_GECKO_PROFILER
#include "ProfilerParent.h"
#endif
#include "RDDProcessHost.h"

namespace mozilla {

using namespace layers;

RDDParent::RDDParent(RDDProcessHost* aHost) : mHost(aHost), mRDDReady(false) {
  MOZ_COUNT_CTOR(RDDParent);
}

RDDParent::~RDDParent() { MOZ_COUNT_DTOR(RDDParent); }

void RDDParent::Init() {
  SendInit();

#ifdef MOZ_GECKO_PROFILER
  Unused << SendInitProfiler(ProfilerParent::CreateForProcess(OtherPid()));
#endif
}

bool RDDParent::EnsureRDDReady() {
  if (mRDDReady) {
    return true;
  }

  mRDDReady = true;
  return true;
}

mozilla::ipc::IPCResult RDDParent::RecvInitComplete() {
  // We synchronously requested RDD parameters before this arrived.
  if (mRDDReady) {
    return IPC_OK();
  }

  mRDDReady = true;
  return IPC_OK();
}

mozilla::ipc::IPCResult RDDParent::RecvInitCrashReporter(
    Shmem&& aShmem, const NativeThreadId& aThreadId) {
  mCrashReporter = MakeUnique<ipc::CrashReporterHost>(GeckoProcessType_RDD,
                                                      aShmem, aThreadId);

  return IPC_OK();
}

bool RDDParent::SendRequestMemoryReport(const uint32_t& aGeneration,
                                       const bool& aAnonymize,
                                       const bool& aMinimizeMemoryUsage,
                                       const MaybeFileDesc& aDMDFile) {
  mMemoryReportRequest = MakeUnique<MemoryReportRequestHost>(aGeneration);
  Unused << PRDDParent::SendRequestMemoryReport(aGeneration, aAnonymize,
                                               aMinimizeMemoryUsage, aDMDFile);
  return true;
}

mozilla::ipc::IPCResult RDDParent::RecvAddMemoryReport(
    const MemoryReport& aReport) {
  if (mMemoryReportRequest) {
    mMemoryReportRequest->RecvReport(aReport);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult RDDParent::RecvFinishMemoryReport(
    const uint32_t& aGeneration) {
  if (mMemoryReportRequest) {
    mMemoryReportRequest->Finish(aGeneration);
    mMemoryReportRequest = nullptr;
  }
  return IPC_OK();
}

void RDDParent::ActorDestroy(ActorDestroyReason aWhy) {
  if (aWhy == AbnormalShutdown) {
    if (mCrashReporter) {
      mCrashReporter->GenerateCrashReport(OtherPid());
      mCrashReporter = nullptr;
    }
  }

  mHost->OnChannelClosed();
}

class DeferredDeleteRDDParent : public Runnable {
 public:
  explicit DeferredDeleteRDDParent(UniquePtr<RDDParent>&& aChild)
      : Runnable("gfx::DeferredDeleteRDDParent"), mChild(std::move(aChild)) {}

  NS_IMETHODIMP Run() override { return NS_OK; }

 private:
  UniquePtr<RDDParent> mChild;
};

/* static */ void RDDParent::Destroy(UniquePtr<RDDParent>&& aChild) {
  NS_DispatchToMainThread(new DeferredDeleteRDDParent(std::move(aChild)));
}

}  // namespace mozilla
