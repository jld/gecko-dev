/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWebBrowserPersistDocumentParent_h__
#define nsWebBrowserPersistDocumentParent_h__

#include "mozilla/Maybe.h"
#include "mozilla/PWebBrowserPersistDocumentParent.h"
#include "nsCOMPtr.h"
#include "nsIWebBrowserPersistDocument.h"

class nsWebBrowserPersistDocumentParent
    : public mozilla::PWebBrowserPersistDocumentParent
    , public nsIWebBrowserPersistDocument
{
public:
    nsWebBrowserPersistDocumentParent();

    using Attrs = WebBrowserPersistDocumentAttrs;

    void SetOnReady(nsIWebBrowserPersistDocumentReceiver* aOnReady);
    void DropExtraRef(); // FIXME: explain this especially

    virtual bool
    RecvAttributes(const Attrs& aAttrs) override;
    virtual bool
    RecvInitFailure(const nsresult& aFailure) override;

    virtual PWebBrowserPersistDocumentReadParent*
    AllocPWebBrowserPersistDocumentReadParent() override;
    virtual bool
    DeallocPWebBrowserPersistDocumentReadParent(PWebBrowserPersistDocumentReadParent* aActor) override;

    virtual PWebBrowserPersistDocumentWriteParent*
    AllocPWebBrowserPersistDocumentWriteParent(
            const WebBrowserPersistMap& aMap,
            const nsCString& aRequestedContentType,
            const uint32_t& aWrapColumn) override;
    virtual bool
    DeallocPWebBrowserPersistDocumentWriteParent(PWebBrowserPersistDocumentWriteParent* aActor) override;

    virtual void
    ActorDestroy(ActorDestroyReason aWhy) override;

    NS_DECL_ISUPPORTS
    NS_DECL_NSIWEBBROWSERPERSISTDOCUMENT

private:
    nsresult mFailure;
    mozilla::Maybe<Attrs> mAttrs;
    nsCOMPtr<nsIWebBrowserPersistDocumentReceiver> mOnReady;
    bool mHoldingExtraRef;

    void ReallyDropExtraRef();
    bool WaitingForAttrs();
    nsresult AccessAttrs();
    void FireOnReady();


    virtual ~nsWebBrowserPersistDocumentParent();
};

#endif // nsWebBrowserPersistDocumentParent_h__
