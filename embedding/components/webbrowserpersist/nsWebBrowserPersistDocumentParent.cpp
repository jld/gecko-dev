/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistDocumentParent.h"

#include "mozilla/PWebBrowserPersistDocumentReadParent.h"
#include "mozilla/PWebBrowserPersistDocumentWriteParent.h"
#include "nsThreadUtils.h"

NS_IMPL_ISUPPORTS(nsWebBrowserPersistDocumentParent,
		  nsIWebBrowserPersistDocument)

nsWebBrowserPersistDocumentParent::nsWebBrowserPersistDocumentParent()
: mFailure(NS_OK)
, mHoldingExtraRef(true)
{
    NS_ADDREF_THIS();
}

bool
nsWebBrowserPersistDocumentParent::WaitingForAttrs()
{
    return NS_SUCCEEDED(mFailure) && mAttrs.isNothing();
}

void
nsWebBrowserPersistDocumentParent::SetOnReady(nsIWebBrowserPersistDocumentReceiver* aOnReady)
{
    MOZ_ASSERT(!mOnReady);
    MOZ_ASSERT(WaitingForAttrs());
    // We'll probably just hang if those assertions would have failed.
    mOnReady = aOnReady;
}

void
nsWebBrowserPersistDocumentParent::FireOnReady()
{
    MOZ_ASSERT(!WaitingForAttrs());
    MOZ_ASSERT(mHoldingExtraRef);
    if (!mOnReady) {
        // Something else (i.e. the parent document's actor) will handle this.
        return;
    }
    mOnReady->OnDocumentReady(this);
    mOnReady = nullptr;
    DropExtraRef();
}

void
nsWebBrowserPersistDocumentParent::ReallyDropExtraRef()
{
    MOZ_ASSERT(!WaitingForAttrs());
    MOZ_ASSERT(mHoldingExtraRef);
    // Test even if no assertions; better to leak than use-after-free.
    if (!mHoldingExtraRef) {
        return;
    }
    mHoldingExtraRef = false;
    NS_RELEASE_THIS(); // This can call dtor -> IPC -> all the things.
}

void
nsWebBrowserPersistDocumentParent::DropExtraRef()
{
    MOZ_ASSERT(!WaitingForAttrs());
    MOZ_ASSERT(mHoldingExtraRef);
    NS_DispatchToCurrentThread(NS_NewNonOwningRunnableMethod(this,
        &nsWebBrowserPersistDocumentParent::ReallyDropExtraRef));
}

void
nsWebBrowserPersistDocumentParent::ActorDestroy(ActorDestroyReason aWhy)
{
    if (aWhy == Deletion) {
        MOZ_ASSERT(!WaitingForAttrs());
        MOZ_ASSERT(!mOnReady);
        MOZ_ASSERT(!mHoldingExtraRef);
    }
    if (mOnReady) {
        // Try to make things blow up instead of just hang.
        // FIXME: is this actually a horrible idea because reentrancy?
        mFailure = NS_ERROR_FAILURE;
        FireOnReady();
    }
    if (mHoldingExtraRef) {
        // That reference can't be claimed normally now, so drop it.
        // But not *right* now, because that can `delete this` while
        // IPDL stuff for `this` is on the stack.
        DropExtraRef();
    }
}

nsWebBrowserPersistDocumentParent::~nsWebBrowserPersistDocumentParent()
{
    MOZ_ASSERT(!mHoldingExtraRef);
    NS_WARN_IF(!Send__delete__(this));
}

bool
nsWebBrowserPersistDocumentParent::RecvAttributes(const Attrs& aAttrs)
{
    MOZ_ASSERT(mAttrs.isNothing());
    MOZ_ASSERT(NS_SUCCEEDED(mFailure));
    MOZ_ASSERT(mHoldingExtraRef);
    if (!WaitingForAttrs()) {
        return false;
    }
    mAttrs.emplace(aAttrs);
    FireOnReady();
    return true;
}

bool
nsWebBrowserPersistDocumentParent::RecvInitFailure(const nsresult& aFailure)
{
    MOZ_ASSERT(WaitingForAttrs());
    MOZ_ASSERT(mHoldingExtraRef);
    if (!WaitingForAttrs() || NS_SUCCEEDED(aFailure)) {
        return false;
    }
    FireOnReady();
    return true;
}

nsresult
nsWebBrowserPersistDocumentParent::AccessAttrs()
{
    MOZ_ASSERT(!WaitingForAttrs());
    if (mAttrs.isNothing()) {
        return NS_FAILED(mFailure) ? mFailure : NS_ERROR_FAILURE;
    }
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentParent::GetIsPrivate(bool* aIsPrivate)
{
    nsresult rv = AccessAttrs();
    if (NS_SUCCEEDED(rv)) {
        *aIsPrivate = mAttrs->isPrivate();
    }
    return rv;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentParent::GetDocumentURI(nsACString& aURISpec)
{
    nsresult rv = AccessAttrs();
    if (NS_SUCCEEDED(rv)) {
        aURISpec = mAttrs->documentURI();
    }
    return rv;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentParent::GetBaseURI(nsACString& aURISpec)
{
    nsresult rv = AccessAttrs();
    if (NS_SUCCEEDED(rv)) {
        aURISpec = mAttrs->baseURI();
    }
    return rv;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentParent::GetContentType(nsACString& aContentType)
{
    nsresult rv = AccessAttrs();
    if (NS_SUCCEEDED(rv)) {
        aContentType = mAttrs->contentType();
    }
    return rv;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentParent::GetCharSet(nsACString& aCharSet)
{
    nsresult rv = AccessAttrs();
    if (NS_SUCCEEDED(rv)) {
        aCharSet = mAttrs->charSet();
    }
    return rv;
}


NS_IMETHODIMP
nsWebBrowserPersistDocumentParent::GetPersistFlags(uint32_t* aFlags)
{
    nsresult rv = AccessAttrs();
    if (NS_SUCCEEDED(rv)) {
        *aFlags = mAttrs->persistFlags();
    }
    return rv;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentParent::SetPersistFlags(uint32_t aFlags)
{
    nsresult rv = AccessAttrs();
    if (NS_SUCCEEDED(rv)) {
        if (!SendSetPersistFlags(aFlags)) {
            return NS_ERROR_FAILURE;
        }
        mAttrs->persistFlags() = aFlags;
    }
    return rv;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentParent::ReadResources(nsIWebBrowserPersistResourceVisitor* aVisitor)
{
    MOZ_CRASH("not implemented yet");
    return NS_ERROR_NOT_IMPLEMENTED;
#if 0
    auto subActor = new nsWebBrowserPersistDocumentReadParent(aVisitor);
    return SendPWebBrowserPersistDocumentReadConstructor(subActor)
        ? NS_OK : NS_ERROR_FAILURE;
#endif
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentParent::WriteContent(
    nsIOutputStream* aStream,
    nsIWebBrowserPersistMap* aMap,
    const nsACString& aRequestedContentType,
    uint32_t aWrapColumn,
    nsIWebBrowserPersistWriteCompletion* aCompletion)
{
    nsresult rv;
    WebBrowserPersistMap map;
    uint32_t numMappedURIs;
    rv = aMap->GetTargetBaseURI(map.targetBaseURI());
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aMap->GetNumMappedURIs(&numMappedURIs);
    NS_ENSURE_SUCCESS(rv, rv);
    for (uint32_t i = 0; i < numMappedURIs; ++i) {
        rv = aMap->GetURIMapping(i,
                                 *(map.mapURIsFrom().AppendElement()),
                                 *(map.mapURIsTo().AppendElement()));
        NS_ENSURE_SUCCESS(rv, rv);
    }

    MOZ_CRASH("not implemented yet");
    return NS_ERROR_NOT_IMPLEMENTED;
#if 0
    auto subActor = new nsWebBrowserPersistDocumentWriteParent(aStream,
                                                               aCompletion);
    return SendPWebBrowserPersistDocumentWriteConstructor(subActor,
                                                          map,
                                                          aRequestedContentType,
                                                          aWrapColumn)
        ? NS_OK : NS_ERROR_FAILURE;
#endif
}

mozilla::PWebBrowserPersistDocumentReadParent*
nsWebBrowserPersistDocumentParent::AllocPWebBrowserPersistDocumentReadParent()
{
    MOZ_CRASH("Don't use this; construct the actor directly.");
    return nullptr;
}

bool
nsWebBrowserPersistDocumentParent::DeallocPWebBrowserPersistDocumentReadParent(PWebBrowserPersistDocumentReadParent* aActor)
{
    delete aActor;
}

mozilla::PWebBrowserPersistDocumentWriteParent*
nsWebBrowserPersistDocumentParent::AllocPWebBrowserPersistDocumentWriteParent(
// What am I even supposed to do with this indentation.
        const WebBrowserPersistMap& aMap,
        const nsCString& aRequestedContentType,
        const uint32_t& aWrapColumn)
{
    MOZ_CRASH("Don't use this; construct the actor directly.");
    return nullptr;
}

bool
nsWebBrowserPersistDocumentParent::DeallocPWebBrowserPersistDocumentWriteParent(PWebBrowserPersistDocumentWriteParent* aActor)
{
    delete aActor;
}
