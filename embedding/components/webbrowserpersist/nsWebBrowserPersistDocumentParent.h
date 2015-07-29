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

// This class implements nsIWebBrowserPersistDocument for a remote document.
// See also mozilla::dom::TabParent::StartPersistence.

class nsWebBrowserPersistDocumentParent final
    : public mozilla::PWebBrowserPersistDocumentParent
    , public nsIWebBrowserPersistDocument
{
public:
    nsWebBrowserPersistDocumentParent();

    // Set a callback to be invoked when the actor leaves the START
    // state.  It must be called exactly once while the actor is still
    // in the START state (or is unconstructed).
    void SetOnReady(nsIWebBrowserPersistDocumentReceiver* aOnReady);

    using Attrs = WebBrowserPersistDocumentAttrs;

    // IPDL methods:
    virtual bool
    RecvAttributes(const Attrs& aAttrs,
                   const OptionalInputStreamParams& aPostData,
                   nsTArray<FileDescriptor>&& aPostFiles) override;
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

    // XPIDL methods:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIWEBBROWSERPERSISTDOCUMENT

private:
    // These two instance variables represent the state.
    //  START: mFailure == NS_OK, mAttrs == Nothing()
    //   MAIN: mFailure == NS_OK, mAttrs == Some(...)
    // FAILED: NS_FAILED(mFailure), mAttrs == Nothing()
    nsresult mFailure;
    mozilla::Maybe<Attrs> mAttrs;
    nsCOMPtr<nsIInputStream> mPostData;
    // This is reset to nullptr when the callback is invoked.
    nsCOMPtr<nsIWebBrowserPersistDocumentReceiver> mOnReady;
    // This object holds a reference to itself so that it's not
    // destroyed before it's passed to mOnReady.  This gets special
    // handling in ActorDestroy to not leak the object on abnormal
    // destruction (normal destruction via __delete__ isn't allowed
    // until the actor has left the START state).
    bool mHoldingExtraRef;
    // Normally the destructor will Send__delete__, but not if the
    // actor was abormally destroyed.
    bool mShouldSendDelete;

    // Releasing the self-reference can `delete this`, which is bad if
    // there are IPC methods of this object on the stack; this pair of
    // methods exists to defer the actual destruction until it's safe.
    void DropExtraRef();
    void ReallyDropExtraRef();

    bool WaitingForAttrs();
    nsresult AccessAttrs();
    bool FireOnReady();

    virtual ~nsWebBrowserPersistDocumentParent();
};

#endif // nsWebBrowserPersistDocumentParent_h__
