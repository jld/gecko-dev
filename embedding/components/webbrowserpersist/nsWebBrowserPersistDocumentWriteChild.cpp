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
    *aNum = static_cast<uint32_t>(mMap.mapURIs().Length());
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistDocumentWriteChild::GetURIMapping(uint32_t aIndex,
                                                     nsACString& aMapFrom,
                                                     nsACString& aMapTo)
{
    if (aIndex >= mMap.mapURIs().Length()) {
        return NS_ERROR_INVALID_ARG;
    }
    aMapFrom = mMap.mapURIs()[aIndex].mapFrom();
    aMapTo = mMap.mapURIs()[aIndex].mapTo();
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
    // Normally an nsIOutputStream would have to be thread-safe, but
    // nsDocumentEncoder currently doesn't call this off the main
    // thread (which also means it's difficult to test the
    // thread-safety code this class doesn't yet have).
    //
    // This is *not* an NS_ERROR_NOT_IMPLEMENTED, because at this
    // point we've probably already misused the non-thread-safe
    // refcounting.
    MOZ_RELEASE_ASSERT(NS_IsMainThread(), "Fix this class to be thread-safe.");

    // Limit the size of an individual IPC message.
    static const uint32_t kMaxWrite = 4096;
    
    // Work around bug 1181433 by sending multiple messages if
    // necessary to write the entire aCount bytes, even though
    // nsIOutputStream.idl says we're allowed to do a short write.
    *aWritten = 0;
    while (aCount > 0) {
        uint32_t toWrite = std::min(kMaxWrite, aCount);
        nsTArray<uint8_t> buf;
        // It would be nice if this extra copy could be avoided.
        buf.AppendElements(aBuf, toWrite);
        SendWriteData(mozilla::Move(buf));
        *aWritten += toWrite;
        aBuf += toWrite;
        aCount -= toWrite;
    }
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
    return NS_OK;
}

