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

class nsWebBrowserPersistDocumentParent final
    : public mozilla::PWebBrowserPersistDocumentParent
    , public nsIWebBrowserPersistDocument
{
public:
    nsWebBrowserPersistDocumentParent();
    void SetOnReady(nsIWebBrowserPersistDocumentReceiver* aOnReady);

    using Attrs = WebBrowserPersistDocumentAttrs;

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
            const uint32_t& aEncoderFlags,
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
    bool mShouldSendDelete;

    void DropExtraRef();
    void ReallyDropExtraRef();
    bool WaitingForAttrs();
    nsresult AccessAttrs();
    bool FireOnReady();

    virtual ~nsWebBrowserPersistDocumentParent();
};

#endif // nsWebBrowserPersistDocumentParent_h__
