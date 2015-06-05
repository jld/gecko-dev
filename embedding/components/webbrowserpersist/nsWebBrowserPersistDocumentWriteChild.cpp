/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistDocumentWriteChild.h"

#include <algorithm>

#include "nsThreadUtils.h"

NS_IMPL_ISUPPORTS(nsWebBrowserPersistDocumentWriteChild,
                  nsIWebBrowserPersistWriteCompletion,
                  nsIWebBrowserPersistMap,
                  nsIOutputStream)

nsWebBrowserPersistDocumentWriteChild::nsWebBrowserPersistDocumentWriteChild(const WebBrowserPersistMap& aMap)
: mMap(aMap)
{
    MOZ_RELEASE_ASSERT(mMap.mapURIsFrom().Length() == mMap.mapURIsTo().Length());
}

nsWebBrowserPersistDocumentWriteChild::~nsWebBrowserPersistDocumentWriteChild()
{
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentWriteChild::OnFinish(nsIWebBrowserPersistDocument* aDocument,
                                                nsIOutputStream* aStream,
                                                const nsACString& aContentType,
                                                nsresult aStatus)
{
    MOZ_ASSERT(aStream == this);
    nsCString contentType(aContentType);
    Send__delete__(this, contentType, aStatus);
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentWriteChild::GetNumMappedURIs(uint32_t* aNum)
{
    *aNum = static_cast<uint32_t>(mMap.mapURIsTo().Length());
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentWriteChild::GetURIMapping(uint32_t aIndex,
                                                     nsACString& aMapFrom,
                                                     nsACString& aMapTo)
{
    if (aIndex >= mMap.mapURIsTo().Length()) {
        return NS_ERROR_INVALID_ARG;
    }
    aMapFrom = mMap.mapURIsFrom()[aIndex];
    aMapTo = mMap.mapURIsTo()[aIndex];
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentWriteChild::GetTargetBaseURI(nsACString& aURI)
{
    aURI = mMap.targetBaseURI();
    return NS_OK;
}


NS_IMETHODIMP
nsWebBrowserPersistDocumentWriteChild::Close()
{
    NS_WARNING("nsWebBrowserPersistDocumentWriteChild::Close()");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentWriteChild::Flush()
{
    NS_WARNING("nsWebBrowserPersistDocumentWriteChild::Flush()");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentWriteChild::Write(const char* aBuf, uint32_t aCount,
                                             uint32_t* aWritten)
{
    static const uint32_t kMaxWrite = 4096;
    
    MOZ_ASSERT(NS_IsMainThread());
    uint32_t toWrite = std::min(kMaxWrite, aCount);
    nsTArray<uint8_t> buf;
    buf.AppendElements(aBuf, toWrite);
    SendWriteData(mozilla::Move(buf));
    *aWritten = toWrite;
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentWriteChild::WriteFrom(nsIInputStream* aFrom,
                                                 uint32_t aCount,
                                                 uint32_t* aWritten)
{
    NS_WARNING("nsWebBrowserPersistDocumentWriteChild::WriteFrom()");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentWriteChild::WriteSegments(nsReadSegmentFun aFun,
                                                     void* aCtx,
                                                     uint32_t aCount,
                                                     uint32_t* aWritten)
{
    NS_WARNING("nsWebBrowserPersistDocumentWriteChild::WriteSegments()");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentWriteChild::IsNonBlocking(bool* aNonBlocking)
{
    // Writes will never fail with NS_BASE_STREAM_WOULD_BLOCK, so:
    *aNonBlocking = false;
    // (They'll probably OOM instead of blocking, though, which is bad.)
    return NS_OK;
}

