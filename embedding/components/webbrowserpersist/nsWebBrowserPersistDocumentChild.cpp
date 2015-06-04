/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistDocumentChild.h"
#include "nsWebBrowserPersistDocument.h"

#include "mozilla/PWebBrowserPersistDocumentReadChild.h"
#include "mozilla/PWebBrowserPersistDocumentWriteChild.h"
#include "nsIDocument.h"

nsWebBrowserPersistDocumentChild::nsWebBrowserPersistDocumentChild()
{
}

nsWebBrowserPersistDocumentChild::~nsWebBrowserPersistDocumentChild()
{
}

void
nsWebBrowserPersistDocumentChild::Start(nsIDocument* aDocument)
{
    Start(aDocument ? new nsWebBrowserPersistDocument(aDocument) : nullptr);
}

void
nsWebBrowserPersistDocumentChild::Start(nsIWebBrowserPersistDocument* aDocument)
{
    MOZ_ASSERT(!mDocument);
    if (!aDocument) {
        SendInitFailure(NS_ERROR_FAILURE);
        return;
    }

    WebBrowserPersistDocumentAttrs attrs;
#define ENSURE(e) do {           \
        nsresult rv = (e);       \
        if (NS_FAILED(rv)) {     \
            SendInitFailure(rv); \
            return;              \
        }                        \
    } while(0)
    ENSURE(aDocument->GetIsPrivate(&(attrs.isPrivate())));
    ENSURE(aDocument->GetDocumentURI(attrs.documentURI()));
    ENSURE(aDocument->GetBaseURI(attrs.baseURI()));
    ENSURE(aDocument->GetContentType(attrs.contentType()));
    ENSURE(aDocument->GetCharSet(attrs.charSet()));
    ENSURE(aDocument->GetPersistFlags(&(attrs.persistFlags())));
#undef ENSURE
    mDocument = aDocument;
    SendAttributes(attrs);
}

bool
nsWebBrowserPersistDocumentChild::RecvSetPersistFlags(const uint32_t& aNewFlags)
{
    mDocument->SetPersistFlags(aNewFlags);
    return true;
}

mozilla::PWebBrowserPersistDocumentReadChild*
nsWebBrowserPersistDocumentChild::AllocPWebBrowserPersistDocumentReadChild()
{
    MOZ_CRASH("not implemented yet");
    return nullptr;
}

bool
nsWebBrowserPersistDocumentChild::RecvPWebBrowserPersistDocumentReadConstructor(PWebBrowserPersistDocumentReadChild* aActor)
{
    MOZ_CRASH("not implemented yet");
    return false;
}

bool
nsWebBrowserPersistDocumentChild::DeallocPWebBrowserPersistDocumentReadChild(PWebBrowserPersistDocumentReadChild* aActor)
{
    MOZ_CRASH("not implemented yet");
    return false;
}

mozilla::PWebBrowserPersistDocumentWriteChild*
nsWebBrowserPersistDocumentChild::AllocPWebBrowserPersistDocumentWriteChild(
            const WebBrowserPersistMap& aMap,
            const nsCString& aRequestedContentType,
            const uint32_t& aWrapColumn)
{
    MOZ_CRASH("not implemented yet");
    return nullptr;
}

bool
nsWebBrowserPersistDocumentChild::RecvPWebBrowserPersistDocumentWriteConstructor(
            PWebBrowserPersistDocumentWriteChild* aActor,
            const WebBrowserPersistMap& aMap,
            const nsCString& aRequestedContentType,
            const uint32_t& aWrapColumn)
{
    MOZ_CRASH("not implemented yet");
    return false;
}

bool
nsWebBrowserPersistDocumentChild::DeallocPWebBrowserPersistDocumentWriteChild(PWebBrowserPersistDocumentWriteChild* aActor)
{
    MOZ_CRASH("not implemented yet");
    return false;
}
