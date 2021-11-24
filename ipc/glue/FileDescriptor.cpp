/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileDescriptor.h"

#include "mozilla/Assertions.h"
#include "mozilla/ipc/IPDLParamTraits.h"
#include "mozilla/ipc/ProtocolMessageUtils.h"
#include "nsDebug.h"
#include "private/pprio.h"

#ifdef XP_WIN
#  include <windows.h>
#  include "ProtocolUtils.h"
#else  // XP_WIN
#  include <unistd.h>
#endif  // XP_WIN

namespace mozilla {
namespace ipc {

FileDescriptor::FileDescriptor(const FileDescriptor& aOther)
    : mHandle(CloneFileHandle(aOther.mHandle)) {}

FileDescriptor::FileDescriptor(PlatformHandleType aHandle)
    : mHandle(CloneFileHandle(aHandle)) {}

FileDescriptor::FileDescriptor(UniquePlatformHandle&& aHandle)
    : mHandle(std::move(aHandle)) {}

FileDescriptor& FileDescriptor::operator=(const FileDescriptor& aOther) {
  if (this != &aOther) {
    mHandle = CloneFileHandle(aOther.mHandle);
  }
  return *this;
}

/* static */
FileDescriptor FileDescriptor::CloneFrom(PRFileDesc* aFd) {
  if (!aFd) {
    return FileDescriptor();
  }

  return FileDescriptor::CloneFrom(PlatformHandleType(
      PR_FileDesc2NativeHandle(aFd)));
}

FileDescriptor::UniquePlatformHandle FileDescriptor::ClonePlatformHandle()
    const {
  return CloneFileHandle(mHandle);
}

FileDescriptor::UniquePlatformHandle FileDescriptor::TakePlatformHandle() {
  return UniquePlatformHandle(mHandle.release());
}

bool FileDescriptor::operator==(const FileDescriptor& aOther) const {
  return mHandle == aOther.mHandle;
}

void IPDLParamTraits<FileDescriptor>::Write(IPC::Message* aMsg,
                                            IProtocol* aActor,
                                            const FileDescriptor& aParam) {
  WriteIPDLParam(aMsg, aActor, aParam.ClonePlatformHandle());
}

bool IPDLParamTraits<FileDescriptor>::Read(const IPC::Message* aMsg,
                                           PickleIterator* aIter,
                                           IProtocol* aActor,
                                           FileDescriptor* aResult) {
  UniqueFileHandle handle;
  if (!ReadIPDLParam(aMsg, aIter, aActor, &handle)) {
    return false;
  }

  *aResult = FileDescriptor(std::move(handle));
  if (!aResult->IsValid()) {
    printf_stderr("IPDL protocol Error: Received an invalid file descriptor\n");
  }
  return true;
}

}  // namespace ipc
}  // namespace mozilla
