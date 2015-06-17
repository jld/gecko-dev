/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistDocumentChild.h"

#include "mozilla/ipc/InputStreamUtils.h"
#include "nsIDocument.h"
#include "nsIInputStream.h"
#include "nsWebBrowserPersistDocument.h"
#include "nsWebBrowserPersistDocumentReadChild.h"
#include "nsWebBrowserPersistDocumentWriteChild.h"

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
    nsCOMPtr<nsIInputStream> postData;
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
    ENSURE(aDocument->GetCharacterSet(attrs.characterSet()));
    ENSURE(aDocument->GetTitle(attrs.title()));
    ENSURE(aDocument->GetReferrer(attrs.referrer()));
    ENSURE(aDocument->GetContentDisposition(attrs.contentDisposition()));
    ENSURE(aDocument->GetCacheKey(&(attrs.cacheKey())));
    ENSURE(aDocument->GetPersistFlags(&(attrs.persistFlags())));
    ENSURE(aDocument->GetPostData(getter_AddRefs(postData)));
    mozilla::ipc::SerializeInputStream(postData,
                                       attrs.postData(),
                                       attrs.postFiles());
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

bool
nsWebBrowserPersistDocumentChild::RecvForceBaseElement()
{
    mDocument->ForceBaseElement();
    return true;
}

mozilla::PWebBrowserPersistDocumentReadChild*
nsWebBrowserPersistDocumentChild::AllocPWebBrowserPersistDocumentReadChild()
{
    auto* actor = new nsWebBrowserPersistDocumentReadChild();
    NS_ADDREF(actor);
    return actor;
}

bool
nsWebBrowserPersistDocumentChild::RecvPWebBrowserPersistDocumentReadConstructor(PWebBrowserPersistDocumentReadChild* aActor)
{
    nsRefPtr<nsWebBrowserPersistDocumentReadChild> visitor = 
        static_cast<nsWebBrowserPersistDocumentReadChild*>(aActor);
    nsresult rv = mDocument->ReadResources(visitor);
    if (NS_FAILED(rv)) {
        visitor->EndVisit(mDocument, rv);
    }
    return true;
}

bool
nsWebBrowserPersistDocumentChild::DeallocPWebBrowserPersistDocumentReadChild(PWebBrowserPersistDocumentReadChild* aActor)
{
    auto* castActor =
        static_cast<nsWebBrowserPersistDocumentReadChild*>(aActor);
    NS_RELEASE(castActor);
    return true;
}

mozilla::PWebBrowserPersistDocumentWriteChild*
nsWebBrowserPersistDocumentChild::AllocPWebBrowserPersistDocumentWriteChild(
            const WebBrowserPersistMap& aMap,
            const nsCString& aRequestedContentType,
            const uint32_t& aEncoderFlags,
            const uint32_t& aWrapColumn)
{
    auto* actor = new nsWebBrowserPersistDocumentWriteChild(aMap);
    NS_ADDREF(actor);
    return actor;
}

bool
nsWebBrowserPersistDocumentChild::RecvPWebBrowserPersistDocumentWriteConstructor(
            PWebBrowserPersistDocumentWriteChild* aActor,
            const WebBrowserPersistMap& aMap,
            const nsCString& aRequestedContentType,
            const uint32_t& aEncoderFlags,
            const uint32_t& aWrapColumn)
{
    auto* castActor =
        static_cast<nsWebBrowserPersistDocumentWriteChild*>(aActor);
    nsresult rv = mDocument->WriteContent(castActor,
                                          aMap.targetBaseURI().IsEmpty() &&
                                          aMap.mapURIsFrom().Length() == 0
                                          ? nullptr
                                          : castActor,
                                          aRequestedContentType,
                                          aEncoderFlags,
                                          aWrapColumn,
                                          castActor);
    if (NS_FAILED(rv)) {
        castActor->OnFinish(mDocument, castActor, aRequestedContentType, rv);
    }
    return true;
}

bool
nsWebBrowserPersistDocumentChild::DeallocPWebBrowserPersistDocumentWriteChild(PWebBrowserPersistDocumentWriteChild* aActor)
{
    auto* castActor =
        static_cast<nsWebBrowserPersistDocumentWriteChild*>(aActor);
    NS_RELEASE(castActor);
    return true;
}
