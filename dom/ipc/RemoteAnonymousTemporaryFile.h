/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_RemoteAnonymousTemporaryFile_h
#define mozilla_dom_RemoteAnonymousTemporaryFile_h

#include "nsAutoPtr.h"
#include "nsID.h"
#include "nsIFile.h"
#include "mozilla/ipc/FileDescriptor.h"
#include "nsIRunnable.h"
#include "nsIUUIDGenerator.h"

// This class is an nsIFile wrapper for using IPC to ask the parent to
// open an anonymous temporary file.  It supports a very small subset
// of the nsIFile interface -- currently, just enough to use with
// nsDownloader and libjar.  In particular, multiple opens will return
// descriptors that share the file offset at the system level (see
// Unix dup() and Windows DuplicateHandle()), and OpenNSPRFileDesc
// will reset that offset to the beginning of the file.  Concurrent
// opens should therefore be avoided, unless access is limited to
// operations that are given an explicit file offset.

namespace mozilla {
namespace dom {

class ContentChild;

class RemoteAnonymousTemporaryFile MOZ_FINAL
    : public nsIFile
{
public:
    RemoteAnonymousTemporaryFile() : mInited(false) { }

    // Main thread only.  On success, the runnable will eventually be
    // run, after which this object is threadsafe.
    nsresult AsyncOpen(ContentChild* aActor, nsIRunnable* aOnReady);

    NS_DECL_THREADSAFE_ISUPPORTS
    NS_DECL_NSIFILE
private:
    bool mInited;
    nsID mUUID;
    mozilla::ipc::FileDescriptor mFileDesc;
    mozilla::ipc::FileDescriptor::PlatformHandleType mHandle;
    nsCOMPtr<nsIRunnable> mOnReady;
    virtual ~RemoteAnonymousTemporaryFile();
    void Ready();
};

}
}

#endif // mozilla_dom_RemoteAnonymousTemporaryFile_h
