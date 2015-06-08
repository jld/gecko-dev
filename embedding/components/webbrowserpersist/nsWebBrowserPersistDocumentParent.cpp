/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistDocumentParent.h"

#include "nsThreadUtils.h"
#include "nsWebBrowserPersistDocumentReadParent.h"
#include "nsWebBrowserPersistDocumentWriteParent.h"

NS_IMPL_ISUPPORTS(nsWebBrowserPersistDocumentParent,
		  nsIWebBrowserPersistDocument)

nsWebBrowserPersistDocumentParent::nsWebBrowserPersistDocumentParent()
: mFailure(NS_OK)
, mHoldingExtraRef(true)
, mShouldSendDelete(true)
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
    MOZ_ASSERT(aOnReady);
    MOZ_ASSERT(!mOnReady);
    MOZ_ASSERT(WaitingForAttrs());
    MOZ_ASSERT(mHoldingExtraRef);
    mOnReady = aOnReady;
}

bool
nsWebBrowserPersistDocumentParent::FireOnReady()
{
    MOZ_ASSERT(!WaitingForAttrs());
    MOZ_ASSERT(mHoldingExtraRef);
    MOZ_ASSERT(mOnReady);
    if (!mOnReady) {
        return false;
    }
    mOnReady->OnDocumentReady(this);
    mOnReady = nullptr;
    DropExtraRef();
    return true;
}

void
nsWebBrowserPersistDocumentParent::DropExtraRef()
{
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mHoldingExtraRef);
    if (!mHoldingExtraRef) {
        return;
    }
    mHoldingExtraRef = false;
    NS_DispatchToCurrentThread(NS_NewNonOwningRunnableMethod(this,
        &nsWebBrowserPersistDocumentParent::ReallyDropExtraRef));
}

void
nsWebBrowserPersistDocumentParent::ReallyDropExtraRef()
{
    MOZ_ASSERT(!mHoldingExtraRef);
    NS_RELEASE_THIS(); // This can call dtor -> IPC -> all the things.
}

void
nsWebBrowserPersistDocumentParent::ActorDestroy(ActorDestroyReason aWhy)
{
    mShouldSendDelete = false;
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
        DropExtraRef();
    }
}

nsWebBrowserPersistDocumentParent::~nsWebBrowserPersistDocumentParent()
{
    MOZ_ASSERT(!mHoldingExtraRef);
    if (mShouldSendDelete) {
        NS_WARN_IF(!Send__delete__(this));
    }
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
    return FireOnReady();
}

bool
nsWebBrowserPersistDocumentParent::RecvInitFailure(const nsresult& aFailure)
{
    MOZ_ASSERT(WaitingForAttrs());
    MOZ_ASSERT(mHoldingExtraRef);
    if (!WaitingForAttrs() || NS_SUCCEEDED(aFailure)) {
        return false;
    }
    mFailure = aFailure;
    return FireOnReady();
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
nsWebBrowserPersistDocumentParent::ForceBaseElement()
{
    return SendForceBaseElement() ? NS_OK : NS_ERROR_FAILURE;
}

mozilla::PWebBrowserPersistDocumentReadParent*
nsWebBrowserPersistDocumentParent::AllocPWebBrowserPersistDocumentReadParent()
{
    MOZ_CRASH("Don't use this; construct the actor directly and AddRef.");
    return nullptr;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentParent::ReadResources(nsIWebBrowserPersistResourceVisitor* aVisitor)
{
    auto* subActor = new nsWebBrowserPersistDocumentReadParent(this, aVisitor);
    NS_ADDREF(subActor);
    return SendPWebBrowserPersistDocumentReadConstructor(subActor)
        ? NS_OK : NS_ERROR_FAILURE;
}

bool
nsWebBrowserPersistDocumentParent::DeallocPWebBrowserPersistDocumentReadParent(PWebBrowserPersistDocumentReadParent* aActor)
{
    auto* castActor =
        static_cast<nsWebBrowserPersistDocumentReadParent*>(aActor);
    NS_RELEASE(castActor);
    return true;
}

mozilla::PWebBrowserPersistDocumentWriteParent*
nsWebBrowserPersistDocumentParent::AllocPWebBrowserPersistDocumentWriteParent(
        const WebBrowserPersistMap& aMap,
        const nsCString& aRequestedContentType,
        const uint32_t& aWrapColumn)
{
    MOZ_CRASH("Don't use this; construct the actor directly.");
    return nullptr;
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

    auto subActor = new nsWebBrowserPersistDocumentWriteParent(this,
                                                               aStream,
                                                               aCompletion);
    nsCString requestedContentType(aRequestedContentType); // Sigh.
    return SendPWebBrowserPersistDocumentWriteConstructor(subActor,
                                                          map,
                                                          requestedContentType,
                                                          aWrapColumn)
        ? NS_OK : NS_ERROR_FAILURE;
}

bool
nsWebBrowserPersistDocumentParent::DeallocPWebBrowserPersistDocumentWriteParent(PWebBrowserPersistDocumentWriteParent* aActor)
{
    delete aActor;
    return true;
}
