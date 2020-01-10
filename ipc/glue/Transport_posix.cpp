/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <unistd.h>

#include <string>

#include "base/eintr_wrapper.h"

#include "mozilla/ipc/Transport.h"
#include "mozilla/ipc/FileDescriptor.h"
#include "ProtocolUtils.h"

using base::ProcessHandle;

namespace mozilla {
namespace ipc {

nsresult CreateTransport(base::ProcessId aProcIdOne, TransportDescriptor* aOne,
                         TransportDescriptor* aTwo) {
  std::wstring id; // Unused on Unix
  // Use MODE_SERVER to force creation of the socketpair
  Transport t(id, Transport::MODE_SERVER, nullptr);
  mozilla::UniqueFileHandle fd1 = t.TakeFileDescriptor();
  mozilla::UniqueFileHandle fd2 = mozilla::Get<0>(t.TakeClientFileDescriptorMapping());
  if (!fd1 || !fd2) {
    return NS_ERROR_TRANSPORT_INIT;
  }

  aOne->mFd = base::FileDescriptor(fd1.release(), true /*close after sending*/);
  aTwo->mFd = base::FileDescriptor(fd2.release(), true /*close after sending*/);
  return NS_OK;
}

UniquePtr<Transport> OpenDescriptor(const TransportDescriptor& aTd,
                                    Transport::Mode aMode) {
  return MakeUnique<Transport>(aTd.mFd.fd, aMode, nullptr);
}

UniquePtr<Transport> OpenDescriptor(const FileDescriptor& aFd,
                                    Transport::Mode aMode) {
  auto rawFD = aFd.ClonePlatformHandle();
  return MakeUnique<Transport>(rawFD.release(), aMode, nullptr);
}

TransportDescriptor DuplicateDescriptor(const TransportDescriptor& aTd) {
  TransportDescriptor result = aTd;
  result.mFd.fd = dup(aTd.mFd.fd);
  if (result.mFd.fd == -1) {
    AnnotateSystemError();
  }
  MOZ_RELEASE_ASSERT(result.mFd.fd != -1, "DuplicateDescriptor failed");
  return result;
}

void CloseDescriptor(const TransportDescriptor& aTd) { close(aTd.mFd.fd); }

}  // namespace ipc
}  // namespace mozilla
