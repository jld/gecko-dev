/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistDocumentParent.h"

#include "mozilla/ipc/InputStreamUtils.h"
#include "nsIInputStream.h"
#include "nsThreadUtils.h"
#include "nsWebBrowserPersistResourcesParent.h"
#include "nsWebBrowserPersistSerializeParent.h"
#include "nsWebBrowserPersistRemoteDocument.h"

nsWebBrowserPersistDocumentParent::nsWebBrowserPersistDocumentParent()
: mReflection(nullptr)
{
}

void
nsWebBrowserPersistDocumentParent::SetOnReady(nsIWebBrowserPersistDocumentReceiver* aOnReady)
{
    MOZ_ASSERT(aOnReady);
    MOZ_ASSERT(!mOnReady);
    MOZ_ASSERT(!mReflection);
    mOnReady = aOnReady;
}

void
nsWebBrowserPersistDocumentParent::ActorDestroy(ActorDestroyReason aWhy)
{
    if (mReflection) {
        mReflection->ActorDestroy();
        mReflection = nullptr;
    }
    if (mOnReady) {
        mOnReady->OnError(NS_ERROR_FAILURE);
        mOnReady = nullptr;
    }
}

nsWebBrowserPersistDocumentParent::~nsWebBrowserPersistDocumentParent()
{
    MOZ_RELEASE_ASSERT(!mReflection);
    MOZ_ASSERT(!mOnReady);
}

bool
nsWebBrowserPersistDocumentParent::RecvAttributes(const Attrs& aAttrs,
                                                  const OptionalInputStreamParams& aPostData,
                                                  nsTArray<FileDescriptor>&& aPostFiles)
{
    // Deserialize the postData unconditionally so that fds aren't leaked.
    nsCOMPtr<nsIInputStream> postData =
        mozilla::ipc::DeserializeInputStream(aPostData, aPostFiles);
    if (!mOnReady || mReflection) {
        return false;
    }
    mReflection = new nsWebBrowserPersistRemoteDocument(this, aAttrs, postData);
    nsRefPtr<nsWebBrowserPersistRemoteDocument> reflection = mReflection;
    mOnReady->OnDocumentReady(reflection);
    mOnReady = nullptr;
    return true;
}

bool
nsWebBrowserPersistDocumentParent::RecvInitFailure(const nsresult& aFailure)
{
    if (!mOnReady || mReflection) {
        return false;
    }
    mOnReady->OnError(aFailure);
    mOnReady = nullptr;
    // Warning: Send__delete__ deallocates this object.
    return Send__delete__(this);
}

mozilla::PWebBrowserPersistResourcesParent*
nsWebBrowserPersistDocumentParent::AllocPWebBrowserPersistResourcesParent()
{
    MOZ_CRASH("Don't use this; construct the actor directly and AddRef.");
    return nullptr;
}

bool
nsWebBrowserPersistDocumentParent::DeallocPWebBrowserPersistResourcesParent(PWebBrowserPersistResourcesParent* aActor)
{
    // Turn the ref held by IPC back into an nsRefPtr.
    nsRefPtr<nsWebBrowserPersistResourcesParent> actor =
        already_AddRefed<nsWebBrowserPersistResourcesParent>(
            static_cast<nsWebBrowserPersistResourcesParent*>(aActor));
    return true;
}

mozilla::PWebBrowserPersistSerializeParent*
nsWebBrowserPersistDocumentParent::AllocPWebBrowserPersistSerializeParent(
        const WebBrowserPersistURIMap& aMap,
        const nsCString& aRequestedContentType,
        const uint32_t& aEncoderFlags,
        const uint32_t& aWrapColumn)
{
    MOZ_CRASH("Don't use this; construct the actor directly.");
    return nullptr;
}

bool
nsWebBrowserPersistDocumentParent::DeallocPWebBrowserPersistSerializeParent(PWebBrowserPersistSerializeParent* aActor)
{
    delete aActor;
    return true;
}
