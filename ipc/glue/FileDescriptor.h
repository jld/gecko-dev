/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_FileDescriptor_h
#define mozilla_ipc_FileDescriptor_h

#include "base/basictypes.h"
#include "base/process.h"
#include "ipc/IPCMessageUtils.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/ipc/IPDLParamTraits.h"

#ifdef XP_UNIX
#include "base/file_descriptor_posix.h"
#endif

namespace mozilla {
namespace ipc {

// This class is used by IPDL to share file descriptors across processes. When
// sending a FileDescriptor IPDL will first duplicate a platform-specific file
// handle type ('PlatformHandleType') into a handle that is valid in the other
// process. Then IPDL will convert the duplicated handle into a type suitable
// for pickling ('PickleType') and then send that through the IPC pipe. In the
// receiving process the pickled data is converted into a platform-specific file
// handle and then returned to the receiver.
//
// To use this class add 'FileDescriptor' as an argument in the IPDL protocol
// and then pass a file descriptor from C++ to the Call/Send method. The
// Answer/Recv method will receive a FileDescriptor& on which PlatformHandle()
// can be called to return the platform file handle.
class FileDescriptor
{
public:
  typedef base::ProcessId ProcessId;

  using UniquePlatformHandle = mozilla::UniqueFileHandle;
  using PlatformHandleType = UniquePlatformHandle::ElementType;

  // Represents an invalid handle.
  FileDescriptor();

  // Copy constructor will duplicate a new handle.
  FileDescriptor(const FileDescriptor& aOther);

  FileDescriptor(FileDescriptor&& aOther);

  // This constructor will duplicate a new handle.
  // The caller still have to close aHandle.
  explicit FileDescriptor(PlatformHandleType aHandle);

  // This constructor takes ownership of the handle.
  explicit FileDescriptor(UniquePlatformHandle&& aHandle);

  ~FileDescriptor();

  FileDescriptor&
  operator=(const FileDescriptor& aOther);

  FileDescriptor&
  operator=(FileDescriptor&& aOther);


  // Tests mHandle against a well-known invalid platform-specific file handle
  // (e.g. -1 on POSIX, INVALID_HANDLE_VALUE on Windows).
  bool
  IsValid() const;

  // Returns a duplicated handle, it is caller's responsibility to close the
  // handle.
  UniquePlatformHandle
  ClonePlatformHandle() const;

  // Extracts the underlying handle and makes this object an invalid handle.
  // (Compare UniquePtr::release.)
  UniquePlatformHandle
  TakePlatformHandle();

  // Only used in nsTArray.
  bool
  operator==(const FileDescriptor& aOther) const;

private:
  static UniqueFileHandle
  Clone(PlatformHandleType aHandle);

  UniquePlatformHandle mHandle;

  // Performs platform-specific actions to duplicate mHandle in the other
  // process (e.g. dup() on POSIX, DuplicateHandle() on Windows). Returns a
  // pickled value that can be passed to the other process via IPC.
  PlatformHandleType ShareTo(ProcessId aTargetPid) const;

  friend struct IPDLParamTraits<FileDescriptor>;
};

template<>
struct IPDLParamTraits<FileDescriptor> {
  typedef FileDescriptor paramType;

#ifndef XP_WIN
  // The move overload isn't usable yet (bug 1441651), but it could be....
  // (It's also not helpful on Windows because it doesn't save any work.)
  static void Write(IPC::Message* aMsg, IProtocol* aActor, paramType&& aParam);
#endif
  static void Write(IPC::Message* aMsg, IProtocol* aActor, const paramType& aParam);
  static bool Read(const IPC::Message* aMsg, PickleIterator* aIter, IProtocol* aActor, paramType* aResult);
};

} // namespace ipc
} // namespace mozilla

#endif // mozilla_ipc_FileDescriptor_h
