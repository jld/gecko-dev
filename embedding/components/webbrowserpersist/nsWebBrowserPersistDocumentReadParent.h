/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWebBrowserPersistDocumentReadParent_h__
#define nsWebBrowserPersistDocumentReadParent_h__

#include "mozilla/PWebBrowserPersistDocumentReadParent.h"

#include "nsWebBrowserPersistDocumentParent.h"
#include "nsCOMPtr.h"
#include "nsIWebBrowserPersistDocument.h"

class nsWebBrowserPersistDocumentReadParent final
    : public mozilla::PWebBrowserPersistDocumentReadParent
    , public nsIWebBrowserPersistDocumentReceiver
{
public:
    nsWebBrowserPersistDocumentReadParent(nsIWebBrowserPersistDocument* aDocument,
                                          nsIWebBrowserPersistResourceVisitor* aVisitor);

    virtual bool
    RecvVisitURI(const nsCString& aURI) override;

    virtual bool
    RecvVisitDocument(PWebBrowserPersistDocumentParent* aSubDocument) override;

    virtual bool
    Recv__delete__(const nsresult& aStatus) override;

    virtual void
    ActorDestroy(ActorDestroyReason aWhy) override;

    NS_DECL_NSIWEBBROWSERPERSISTDOCUMENTRECEIVER
    NS_DECL_ISUPPORTS

private:
    // Note: even if the XPIDL didn't need mDocument for visitor
    // callbacks, this object still needs to hold a strong reference
    // to it to defer actor subtree deletion until after the
    // visitation is finished.
    nsCOMPtr<nsIWebBrowserPersistDocument> mDocument;
    nsCOMPtr<nsIWebBrowserPersistResourceVisitor> mVisitor;

    virtual ~nsWebBrowserPersistDocumentReadParent();
};

#endif // nsWebBrowserPersistDocumentReadParent_h__
