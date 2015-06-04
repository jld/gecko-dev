/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistDocumentReadParent.h"

NS_IMPL_ISUPPORTS(nsWebBrowserPersistDocumentReadParent,
                  nsIWebBrowserPersistDocumentReceiver)

nsWebBrowserPersistDocumentReadParent::nsWebBrowserPersistDocumentReadParent(
        nsIWebBrowserPersistDocument* aDocument,
        nsIWebBrowserPersistResourceVisitor* aVisitor)
: mDocument(aDocument)
, mVisitor(aVisitor)
{
    MOZ_ASSERT(aDocument);
    MOZ_ASSERT(aVisitor);
}

nsWebBrowserPersistDocumentReadParent::~nsWebBrowserPersistDocumentReadParent()
{
}

void
nsWebBrowserPersistDocumentReadParent::ActorDestroy(ActorDestroyReason aWhy)
{
    if (aWhy != Deletion && mVisitor) {
        mVisitor->EndVisit(mDocument, NS_ERROR_FAILURE);
    }
    mVisitor = nullptr;
}

bool
nsWebBrowserPersistDocumentReadParent::Recv__delete__(const nsresult& aStatus)
{
    mVisitor->EndVisit(mDocument, aStatus);
    mVisitor = nullptr;
    return true;
}

bool
nsWebBrowserPersistDocumentReadParent::RecvVisitURI(const nsCString& aURI)
{
    mVisitor->VisitURI(mDocument, aURI);
    return true;
}

bool
nsWebBrowserPersistDocumentReadParent::RecvVisitDocument(PWebBrowserPersistDocumentParent* aSubDocument)
{
    static_cast<nsWebBrowserPersistDocumentParent*>(aSubDocument)
        ->SetOnReady(this);
    return true;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentReadParent::OnDocumentReady(nsIWebBrowserPersistDocument* aSubDocument)
{
    if (!mVisitor) {
        return NS_ERROR_FAILURE;
    }
    mVisitor->VisitDocument(mDocument, aSubDocument);
    return NS_OK;
}
