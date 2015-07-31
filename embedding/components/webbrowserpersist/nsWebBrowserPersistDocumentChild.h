/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWebBrowserPersistDocumentChild_h__
#define nsWebBrowserPersistDocumentChild_h__

#include "mozilla/PWebBrowserPersistDocumentChild.h"
#include "nsCOMPtr.h"
#include "nsIWebBrowserPersistDocument.h"

class nsIDocument;

class nsWebBrowserPersistDocumentChild final
    : public mozilla::PWebBrowserPersistDocumentChild
{
public:
    nsWebBrowserPersistDocumentChild();
    ~nsWebBrowserPersistDocumentChild();

    // This sends either Attributes or InitFailure and thereby causes
    // the actor to leave the START state.
    void Start(nsIWebBrowserPersistDocument* aDocument);
    void Start(nsIDocument* aDocument);

    virtual bool
    RecvSetPersistFlags(const uint32_t& aNewFlags) override;

    virtual PWebBrowserPersistResourcesChild*
    AllocPWebBrowserPersistResourcesChild() override;
    virtual bool
    RecvPWebBrowserPersistResourcesConstructor(PWebBrowserPersistResourcesChild* aActor) override;
    virtual bool
    DeallocPWebBrowserPersistResourcesChild(PWebBrowserPersistResourcesChild* aActor) override;

    virtual PWebBrowserPersistSerializeChild*
    AllocPWebBrowserPersistSerializeChild(
            const WebBrowserPersistMap& aMap,
            const nsCString& aRequestedContentType,
            const uint32_t& aEncoderFlags,
            const uint32_t& aWrapColumn) override;
    virtual bool
    RecvPWebBrowserPersistSerializeConstructor(
            PWebBrowserPersistSerializeChild* aActor,
            const WebBrowserPersistMap& aMap,
            const nsCString& aRequestedContentType,
            const uint32_t& aEncoderFlags,
            const uint32_t& aWrapColumn) override;
    virtual bool
    DeallocPWebBrowserPersistSerializeChild(PWebBrowserPersistSerializeChild* aActor) override;

private:
    nsCOMPtr<nsIWebBrowserPersistDocument> mDocument;
};

#endif // nsWebBrowserPersistDocumentChild_h__
