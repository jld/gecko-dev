/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistDocumentReadChild.h"

#include "nsWebBrowserPersistDocumentChild.h"
#include "mozilla/dom/PBrowserChild.h"


NS_IMPL_ISUPPORTS(nsWebBrowserPersistDocumentReadChild,
                  nsIWebBrowserPersistResourceVisitor)

nsWebBrowserPersistDocumentReadChild::nsWebBrowserPersistDocumentReadChild()
{
}

nsWebBrowserPersistDocumentReadChild::~nsWebBrowserPersistDocumentReadChild()
{
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentReadChild::VisitURI(nsIWebBrowserPersistDocument *aDocument,
                                               const nsACString& aURI)
{
    nsCString copiedURI(aURI); // Yay, XPIDL/IPDL mismatch.
    SendVisitURI(copiedURI);
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentReadChild::VisitDocument(nsIWebBrowserPersistDocument* aDocument,
                                                    nsIWebBrowserPersistDocument* aSubDocument)
{
    auto* subActor = new nsWebBrowserPersistDocumentChild();
    mozilla::dom::PBrowserChild* grandManager = Manager()->Manager();
    if (!grandManager->SendPWebBrowserPersistDocumentConstructor(subActor)) {
        // NOTE: subActor is freed at this point.
        return NS_ERROR_FAILURE;
    }
    // ...but here, IPC won't free subActor until after this returns
    // to the event loop.
    
    // The order of these two messages will be preserved, because
    // they're the same toplevel protocol and priority.  This order
    // makes things a little cleaner for the parent side.
    SendVisitDocument(subActor);
    subActor->Start(aSubDocument);
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentReadChild::EndVisit(nsIWebBrowserPersistDocument *aDocument,
                                               nsresult aStatus)
{
    Send__delete__(this, aStatus);
    return NS_OK;
}
