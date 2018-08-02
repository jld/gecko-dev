/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileDescriptor.h"

#include "mozilla/Assertions.h"
#include "mozilla/TypeTraits.h"
#include "nsDebug.h"

#ifdef XP_WIN
#include <windows.h>
#include "ProtocolUtils.h"
#else // XP_WIN
#include <unistd.h>
#endif // XP_WIN

namespace mozilla {
namespace ipc {

FileDescriptor::FileDescriptor() = default;

FileDescriptor::FileDescriptor(const FileDescriptor& aOther)
  : mHandle(Clone(aOther.mHandle.get()))
{ }

FileDescriptor::FileDescriptor(FileDescriptor&& aOther)
  : mHandle(std::move(aOther.mHandle))
{ }

FileDescriptor::FileDescriptor(PlatformHandleType aHandle)
  : mHandle(Clone(aHandle))
{ }

FileDescriptor::FileDescriptor(UniquePlatformHandle&& aHandle)
  : mHandle(std::move(aHandle))
{ }

FileDescriptor::~FileDescriptor() = default;

FileDescriptor&
FileDescriptor::operator=(const FileDescriptor& aOther)
{
  if (this != &aOther) {
    mHandle = Clone(aOther.mHandle.get());
  }
  return *this;
}

FileDescriptor&
FileDescriptor::operator=(FileDescriptor&& aOther)
{
  if (this != &aOther) {
    mHandle = std::move(aOther.mHandle);
  }
  return *this;
}

FileDescriptor::PlatformHandleType
FileDescriptor::ShareTo(FileDescriptor::ProcessId aTargetPid) const
{
  PlatformHandleType newHandle;
#ifdef XP_WIN
  if (IsValid()) {
    if (mozilla::ipc::DuplicateHandle(mHandle.get(), aTargetPid, &newHandle, 0,
                                      DUPLICATE_SAME_ACCESS)) {
      return newHandle;
    }
    NS_WARNING("Failed to duplicate file handle for other process!");
  }
  return INVALID_HANDLE_VALUE;
#else // XP_WIN
  if (IsValid()) {
    newHandle = dup(mHandle.get());
    if (newHandle >= 0) {
      return newHandle;
    }
    NS_WARNING("Failed to duplicate file handle for other process!");
  }
  return -1;
#endif

  MOZ_CRASH("Must not get here!");
}

bool
FileDescriptor::IsValid() const
{
  return mHandle != nullptr;
}

FileDescriptor::UniquePlatformHandle
FileDescriptor::ClonePlatformHandle() const
{
  return Clone(mHandle.get());
}

FileDescriptor::UniquePlatformHandle
FileDescriptor::TakePlatformHandle()
{
  return UniquePlatformHandle(mHandle.release());
}

bool
FileDescriptor::operator==(const FileDescriptor& aOther) const
{
  return mHandle == aOther.mHandle;
}

// static
FileDescriptor::UniquePlatformHandle
FileDescriptor::Clone(PlatformHandleType aHandle)
{
  FileDescriptor::PlatformHandleType newHandle;

#ifdef XP_WIN
  if (aHandle == INVALID_HANDLE_VALUE) {
    return UniqueFileHandle();
  }
  if (::DuplicateHandle(GetCurrentProcess(), aHandle, GetCurrentProcess(),
                        &newHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
    return UniqueFileHandle(newHandle);
  }
#else // XP_WIN
  if (aHandle < 0) {
    return UniqueFileHandle();
  }
  newHandle = dup(aHandle);
  if (newHandle >= 0) {
    return UniqueFileHandle(newHandle);
  }
#endif
  NS_WARNING("Failed to duplicate file handle for current process!");
  return UniqueFileHandle();
}

static void
WriteFileDesc(IPC::Message* aMsg,
              IProtocol* aActor,
              UniqueFileHandle&& aDesc)
{
#ifdef XP_WIN
  WriteIPDLParam(aMsg, aActor, aDesc.release());
#else // !XP_WIN
  bool sendable = aDesc != nullptr;
  WriteIPDLParam(aMsg, aActor, sendable);
  if (sendable) {
    aMsg->WriteFileDescriptor(base::FileDescriptor(aDesc.release(), true));
  }
#endif // !XP_WIN
}

void
IPDLParamTraits<FileDescriptor>::Write(IPC::Message* aMsg,
                                       IProtocol* aActor,
                                       const FileDescriptor& aParam)
{
  UniqueFileHandle ufd(aParam.ShareTo(aActor->OtherPid()));
  WriteFileDesc(aMsg, aActor, std::move(ufd));
}

#ifndef XP_WIN
void
IPDLParamTraits<FileDescriptor>::Write(IPC::Message* aMsg,
                                       IProtocol* aActor,
                                       FileDescriptor&& aParam)
{
  WriteFileDesc(aMsg, aActor, aParam.TakePlatformHandle());
}
#endif

bool
IPDLParamTraits<FileDescriptor>::Read(const IPC::Message* aMsg,
                                      PickleIterator* aIter,
                                      IProtocol* aActor,
                                      FileDescriptor* aResult)
{
  UniqueFileHandle ufd;
#ifdef XP_WIN
  FileDescriptor::PlatformHandleType pfd;
  if (!ReadIPDLParam(aMsg, aIter, aActor, &pfd)) {
    return false;
  }
  ufd.reset(pfd);
#else
  bool was_sent;
  base::FileDescriptor pfd;
  if (!ReadIPDLParam(aMsg, aIter, aActor, &was_sent)) {
    return false;
  }
  if (was_sent) {
    if (!aMsg->ReadFileDescriptor(aIter, &pfd)) {
      return false;
    }
    ufd.reset(pfd.fd);
    MOZ_ASSERT(ufd != nullptr);
  }
#endif

  *aResult = FileDescriptor(std::move(ufd));
  if (!aResult->IsValid()) {
    printf_stderr("IPDL protocol Error: Received an invalid file descriptor\n");
  }
  return true;
}

} // namespace ipc
} // namespace mozilla
