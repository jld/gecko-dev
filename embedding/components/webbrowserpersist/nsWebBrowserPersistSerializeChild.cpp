/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistSerializeChild.h"

#include <algorithm>

#include "nsThreadUtils.h"

NS_IMPL_ISUPPORTS(nsWebBrowserPersistSerializeChild,
                  nsIWebBrowserPersistWriteCompletion,
                  nsIWebBrowserPersistMap,
                  nsIOutputStream)

nsWebBrowserPersistSerializeChild::nsWebBrowserPersistSerializeChild(const WebBrowserPersistMap& aMap)
: mMap(aMap)
{
}

nsWebBrowserPersistSerializeChild::~nsWebBrowserPersistSerializeChild()
{
}

NS_IMETHODIMP
nsWebBrowserPersistSerializeChild::OnFinish(nsIWebBrowserPersistDocument* aDocument,
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
nsWebBrowserPersistSerializeChild::GetNumMappedURIs(uint32_t* aNum)
{
    *aNum = static_cast<uint32_t>(mMap.mapURIs().Length());
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistSerializeChild::GetURIMapping(uint32_t aIndex,
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
nsWebBrowserPersistSerializeChild::GetTargetBaseURI(nsACString& aURI)
{
    aURI = mMap.targetBaseURI();
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistSerializeChild::Close()
{
    NS_WARNING("nsWebBrowserPersistSerializeChild::Close()");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowserPersistSerializeChild::Flush()
{
    NS_WARNING("nsWebBrowserPersistSerializeChild::Flush()");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowserPersistSerializeChild::Write(const char* aBuf, uint32_t aCount,
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
    static const uint32_t kMaxWrite = 65536;

    // Work around bug 1181433 by sending multiple messages if
    // necessary to write the entire aCount bytes, even though
    // nsIOutputStream.idl says we're allowed to do a short write.
    const char* buf = aBuf;
    uint32_t count = aCount;
    *aWritten = 0;
    while (count > 0) {
        uint32_t toWrite = std::min(kMaxWrite, count);
        nsTArray<uint8_t> arrayBuf;
        // It would be nice if this extra copy could be avoided.
        arrayBuf.AppendElements(buf, toWrite);
        SendWriteData(mozilla::Move(arrayBuf));
        *aWritten += toWrite;
        buf += toWrite;
        count -= toWrite;
    }
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistSerializeChild::WriteFrom(nsIInputStream* aFrom,
                                             uint32_t aCount,
                                             uint32_t* aWritten)
{
    NS_WARNING("nsWebBrowserPersistSerializeChild::WriteFrom()");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowserPersistSerializeChild::WriteSegments(nsReadSegmentFun aFun,
                                                 void* aCtx,
                                                 uint32_t aCount,
                                                 uint32_t* aWritten)
{
    NS_WARNING("nsWebBrowserPersistSerializeChild::WriteSegments()");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowserPersistSerializeChild::IsNonBlocking(bool* aNonBlocking)
{
    // Writes will never fail with NS_BASE_STREAM_WOULD_BLOCK, so:
    *aNonBlocking = false;
    return NS_OK;
}

