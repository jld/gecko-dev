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

class nsWebBrowserPersistRemoteDocument;

class nsWebBrowserPersistDocumentParent final
    : public mozilla::PWebBrowserPersistDocumentParent
{
public:
    nsWebBrowserPersistDocumentParent();
    virtual ~nsWebBrowserPersistDocumentParent();

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
private:
    // This is reset to nullptr when the callback is invoked.
    nsCOMPtr<nsIWebBrowserPersistDocumentReceiver> mOnReady;
    nsWebBrowserPersistRemoteDocument* mReflection;
};

#endif // nsWebBrowserPersistDocumentParent_h__
