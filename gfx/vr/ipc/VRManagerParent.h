/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_VR_VRMANAGERPARENT_H
#define MOZILLA_GFX_VR_VRMANAGERPARENT_H

#include "mozilla/layers/CompositableTransactionParent.h"  // need?
#include "mozilla/gfx/PVRManagerParent.h" // for PVRManagerParent
#include "mozilla/gfx/PVRLayerParent.h"   // for PVRLayerParent
#include "mozilla/ipc/ProtocolUtils.h"    // for IToplevelProtocol
#include "mozilla/TimeStamp.h"            // for TimeStamp
#include "gfxVR.h"                        // for VRFieldOfView
#include "VRThread.h"                     // for VRListenerThreadHolder

namespace mozilla {
using namespace layers;
namespace gfx {

class VRManager;

namespace impl {
class VRDisplayPuppet;
class VRControllerPuppet;
} // namespace impl

class VRManagerParent final : public PVRManagerParent
{
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(VRManagerParent);
public:
  explicit VRManagerParent(ProcessId aChildProcessId, bool aIsContentChild);

  static VRManagerParent* CreateSameProcess();
  static bool CreateForGPUProcess(Endpoint<PVRManagerParent>&& aEndpoint);
  static bool CreateForContent(Endpoint<PVRManagerParent>&& aEndpoint);

  bool IsSameProcess() const;
  bool HaveEventListener();
  bool HaveControllerListener();
  bool SendGamepadUpdate(const GamepadChangeEvent& aGamepadEvent);
  bool SendReplyGamepadVibrateHaptic(const uint32_t& aPromiseID);

protected:
  ~VRManagerParent();

  virtual PVRLayerParent* AllocPVRLayerParent(const uint32_t& aDisplayID,
                                              const uint32_t& aGroup) override;
  virtual bool DeallocPVRLayerParent(PVRLayerParent* actor) override;

  virtual void ActorDestroy(ActorDestroyReason why) override;
  void OnChannelConnected(int32_t pid) override;

  virtual mozilla::ipc::IPCResult RecvRefreshDisplays() override;
  virtual mozilla::ipc::IPCResult RecvResetSensor(uint32_t&& aDisplayID) override;
  virtual mozilla::ipc::IPCResult RecvSetGroupMask(uint32_t&& aDisplayID, uint32_t&& aGroupMask) override;
  virtual mozilla::ipc::IPCResult RecvSetHaveEventListener(bool&& aHaveEventListener) override;
  virtual mozilla::ipc::IPCResult RecvControllerListenerAdded() override;
  virtual mozilla::ipc::IPCResult RecvControllerListenerRemoved() override;
  virtual mozilla::ipc::IPCResult RecvVibrateHaptic(uint32_t&& aControllerIdx, uint32_t&& aHapticIndex,
                                                    double&& aIntensity, double&& aDuration, uint32_t&& aPromiseID) override;
  virtual mozilla::ipc::IPCResult RecvStopVibrateHaptic(uint32_t&& aControllerIdx) override;
  virtual mozilla::ipc::IPCResult RecvCreateVRTestSystem() override;
  virtual mozilla::ipc::IPCResult RecvCreateVRServiceTestDisplay(nsCString&& aID, uint32_t&& aPromiseID) override;
  virtual mozilla::ipc::IPCResult RecvCreateVRServiceTestController(nsCString&& aID, uint32_t&& aPromiseID) override;
  virtual mozilla::ipc::IPCResult RecvSetDisplayInfoToMockDisplay(uint32_t&& aDeviceID,
                                                                  VRDisplayInfo&& aDisplayInfo) override;
  virtual mozilla::ipc::IPCResult RecvSetSensorStateToMockDisplay(uint32_t&& aDeviceID,
                                                                  VRHMDSensorState&& aSensorState) override;
  virtual mozilla::ipc::IPCResult RecvNewButtonEventToMockController(uint32_t&& aDeviceID, long&& aButton,
                                                                     bool&& aPressed) override;
  virtual mozilla::ipc::IPCResult RecvNewAxisMoveEventToMockController(uint32_t&& aDeviceID, long&& aAxis,
                                                                       double&& aValue) override;
  virtual mozilla::ipc::IPCResult RecvNewPoseMoveToMockController(uint32_t&& aDeviceID, GamepadPoseState&& pose) override;

private:
  void RegisterWithManager();
  void UnregisterFromManager();

  void Bind(Endpoint<PVRManagerParent>&& aEndpoint);

  static void RegisterVRManagerInVRListenerThread(VRManagerParent* aVRManager);

  void DeferredDestroy();
  already_AddRefed<impl::VRControllerPuppet> GetControllerPuppet(uint32_t aDeviceID);

  // This keeps us alive until ActorDestroy(), at which point we do a
  // deferred destruction of ourselves.
  RefPtr<VRManagerParent> mSelfRef;
  RefPtr<VRListenerThreadHolder> mVRListenerThreadHolder;

  // Keep the VRManager alive, until we have destroyed ourselves.
  RefPtr<VRManager> mVRManagerHolder;
  nsRefPtrHashtable<nsUint32HashKey, impl::VRControllerPuppet> mVRControllerTests;
  uint32_t mControllerTestID;
  bool mHaveEventListener;
  bool mHaveControllerListener;
  bool mIsContentChild;
};

class VRManagerPromise final
{
  friend class VRManager;

public:
  explicit VRManagerPromise(RefPtr<VRManagerParent> aParent, uint32_t aPromiseID)
  : mParent(aParent), mPromiseID(aPromiseID)
  {}
  ~VRManagerPromise() {
    mParent = nullptr;
  }

private:
  RefPtr<VRManagerParent> mParent;
  uint32_t mPromiseID;
};

} // namespace mozilla
} // namespace gfx

#endif // MOZILLA_GFX_VR_VRMANAGERPARENT_H
