/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPVideoEncoderChild_h_
#define GMPVideoEncoderChild_h_

#include "nsString.h"
#include "mozilla/gmp/PGMPVideoEncoderChild.h"
#include "gmp-video-encode.h"
#include "GMPSharedMemManager.h"
#include "GMPVideoHost.h"

namespace mozilla {
namespace gmp {

class GMPContentChild;

class GMPVideoEncoderChild : public PGMPVideoEncoderChild,
                             public GMPVideoEncoderCallback,
                             public GMPSharedMemManager
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GMPVideoEncoderChild);

  explicit GMPVideoEncoderChild(GMPContentChild* aPlugin);

  void Init(GMPVideoEncoder* aEncoder);
  GMPVideoHostImpl& Host();

  // GMPVideoEncoderCallback
  void Encoded(GMPVideoEncodedFrame* aEncodedFrame,
               const uint8_t* aCodecSpecificInfo,
               uint32_t aCodecSpecificInfoLength) override;
  void Error(GMPErr aError) override;

  // GMPSharedMemManager
  bool Alloc(size_t aSize, Shmem::SharedMemory::SharedMemoryType aType,
    Shmem* aMem) override;
  void Dealloc(Shmem& aMem) override;

private:
  virtual ~GMPVideoEncoderChild();

  // PGMPVideoEncoderChild
  mozilla::ipc::IPCResult RecvInitEncode(GMPVideoCodec&& aCodecSettings,
                                         InfallibleTArray<uint8_t>&& aCodecSpecific,
                                         int32_t&& aNumberOfCores,
                                         uint32_t&& aMaxPayloadSize) override;
  mozilla::ipc::IPCResult RecvEncode(GMPVideoi420FrameData&& aInputFrame,
                                     InfallibleTArray<uint8_t>&& aCodecSpecificInfo,
                                     InfallibleTArray<GMPVideoFrameType>&& aFrameTypes) override;
  mozilla::ipc::IPCResult RecvChildShmemForPool(Shmem&& aEncodedBuffer) override;
  mozilla::ipc::IPCResult RecvSetChannelParameters(uint32_t&& aPacketLoss,
                                                   uint32_t&& aRTT) override;
  mozilla::ipc::IPCResult RecvSetRates(uint32_t&& aNewBitRate,
                                       uint32_t&& aFrameRate) override;
  mozilla::ipc::IPCResult RecvSetPeriodicKeyFrames(bool&& aEnable) override;
  mozilla::ipc::IPCResult RecvEncodingComplete() override;

  GMPContentChild* mPlugin;
  GMPVideoEncoder* mVideoEncoder;
  GMPVideoHostImpl mVideoHost;

  // Non-zero when a GMP is blocked spinning the IPC message loop while
  // waiting on an NeedShmem to complete.
  int mNeedShmemIntrCount;
  bool mPendingEncodeComplete;
};

} // namespace gmp
} // namespace mozilla

#endif // GMPVideoEncoderChild_h_
