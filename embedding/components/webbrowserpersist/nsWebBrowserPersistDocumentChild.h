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

    void Start(nsIWebBrowserPersistDocument* aDocument);
    void Start(nsIDocument* aDocument);

    virtual bool
    RecvSetPersistFlags(const uint32_t& aNewFlags) override;

    virtual PWebBrowserPersistDocumentReadChild*
    AllocPWebBrowserPersistDocumentReadChild() override;
    virtual bool
    RecvPWebBrowserPersistDocumentReadConstructor(PWebBrowserPersistDocumentReadChild* aActor) override;
    virtual bool
    DeallocPWebBrowserPersistDocumentReadChild(PWebBrowserPersistDocumentReadChild* aActor) override;

    virtual PWebBrowserPersistDocumentWriteChild*
    AllocPWebBrowserPersistDocumentWriteChild(
            const WebBrowserPersistMap& aMap,
            const nsCString& aRequestedContentType,
            const uint32_t& aWrapColumn) override;
    virtual bool
    RecvPWebBrowserPersistDocumentWriteConstructor(
            PWebBrowserPersistDocumentWriteChild* aActor,
            const WebBrowserPersistMap& aMap,
            const nsCString& aRequestedContentType,
            const uint32_t& aWrapColumn) override;
    virtual bool
    DeallocPWebBrowserPersistDocumentWriteChild(PWebBrowserPersistDocumentWriteChild* aActor) override;

private:
    nsCOMPtr<nsIWebBrowserPersistDocument> mDocument;
};

#endif // nsWebBrowserPersistDocumentChild_h__
