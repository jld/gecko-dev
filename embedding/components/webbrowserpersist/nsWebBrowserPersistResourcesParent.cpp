/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistResourcesParent.h"

NS_IMPL_ISUPPORTS(nsWebBrowserPersistResourcesParent,
                  nsIWebBrowserPersistDocumentReceiver)

nsWebBrowserPersistResourcesParent::nsWebBrowserPersistResourcesParent(
        nsIWebBrowserPersistDocument* aDocument,
        nsIWebBrowserPersistResourceVisitor* aVisitor)
: mDocument(aDocument)
, mVisitor(aVisitor)
{
    MOZ_ASSERT(aDocument);
    MOZ_ASSERT(aVisitor);
}

nsWebBrowserPersistResourcesParent::~nsWebBrowserPersistResourcesParent()
{
}

void
nsWebBrowserPersistResourcesParent::ActorDestroy(ActorDestroyReason aWhy)
{
    if (aWhy != Deletion && mVisitor) {
        mVisitor->EndVisit(mDocument, NS_ERROR_FAILURE);
    }
    mVisitor = nullptr;
}

bool
nsWebBrowserPersistResourcesParent::Recv__delete__(const nsresult& aStatus)
{
    mVisitor->EndVisit(mDocument, aStatus);
    mVisitor = nullptr;
    return true;
}

bool
nsWebBrowserPersistResourcesParent::RecvVisitResource(const nsCString& aURI)
{
    mVisitor->VisitResource(mDocument, aURI);
    return true;
}

bool
nsWebBrowserPersistResourcesParent::RecvVisitDocument(PWebBrowserPersistDocumentParent* aSubDocument)
{
    // Don't expose the subdocument to the visitor until it's ready
    // (until the actor isn't in START state).
    static_cast<nsWebBrowserPersistDocumentParent*>(aSubDocument)
        ->SetOnReady(this);
    return true;
}

NS_IMETHODIMP
nsWebBrowserPersistResourcesParent::OnDocumentReady(nsIWebBrowserPersistDocument* aSubDocument)
{
    if (!mVisitor) {
        return NS_ERROR_FAILURE;
    }
    mVisitor->VisitDocument(mDocument, aSubDocument);
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistResourcesParent::OnError(nsresult aFailure)
{
    // Nothing useful to do but ignore the failed document.
    return NS_OK;
}
