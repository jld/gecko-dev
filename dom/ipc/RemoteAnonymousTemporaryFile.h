/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_RemoteAnonymousTemporaryFile_h
#define mozilla_dom_RemoteAnonymousTemporaryFile_h

#include "nsID.h"
#include "mozilla/ipc/FileDescriptor.h"
#include "nsIUUIDGenerator.h"

namespace mozilla {
namespace dom {

class ContentChild;

class RemoteAnonymousTemporaryFile MOZ_FINAL
    : public nsIFile
{
    bool mInited;
    nsID mUUID;
    mozilla::ipc::FileDescriptor::PlatformHandleType mHandle;
    virtual ~RemoteAnonymousTemporaryFile();
public:
    RemoteAnonymousTemporaryFile() : mInited(false) { }
    nsresult Init(ContentChild*);

    NS_DECL_THREADSAFE_ISUPPORTS
    NS_DECL_NSIFILE
};

}
}

#endif // mozilla_dom_RemoteAnonymousTemporaryFile_h
