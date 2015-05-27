/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWebBrowserPersistLocalDocument.h"

#include "nsIDocument.h"
#include "nsILoadContext.h"

NS_IMPL_ISUPPORTS(nsWebBrowserPersistLocalDocument, nsIWebBrowserPersistDocument)

nsWebBrowserPersistLocalDocument::nsWebBrowserPersistLocalDocument(nsIDocument* aDocument)
: mDocument(aDocument)
{
    MOZ_ASSERT(mDocument);
}

nsWebBrowserPersistLocalDocument::~nsWebBrowserPersistLocalDocument()
{
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::GetIsPrivate(bool* aIsPrivate)
{
    nsCOMPtr<nsILoadContext> privacyContext = mDocument->GetLoadContext();
    *aIsPrivate = privacyContext && privacyContext->UsePrivateBrowsing();
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::GetDocumentURI(nsACString& aURISpec)
{
    nsCOMPtr<nsIURI> uri = mDocument->GetDocumentURI();
    if (!uri) {
        return NS_ERROR_UNEXPECTED;
    }
    return uri->GetSpec(aURISpec);
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::GetBaseURI(nsACString& aURISpec)
{
    nsCOMPtr<nsIURI> uri = mDocument->GetBaseURI();
    if (!uri) {
        return NS_ERROR_UNEXPECTED;
    }
    return uri->GetSpec(aURISpec);
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::GetContentType(nsACString& aContentType)
{
    nsAutoString utf16Type;
    nsresult rv;

    rv = mDocument->GetContentType(utf16Type);
    NS_ENSURE_SUCCESS(rv, rv);
    aContentType = NS_ConvertUTF16toUTF8(utf16Type);
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::GetCharSet(nsACString& aCharSet)
{
    aCharSet = mDocument->GetDocumentCharacterSet();
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::ReadResources(nsIWebBrowserPersistResourceVisitor* aVisitor)
{
    MOZ_CRASH("FINISH WRITING ME");
    return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersistLocalDocument::WriteContent(nsIOutputStream* aStream,
                                               nsIWebBrowserPersistMap *aMap)
{
    MOZ_CRASH("FINISH WRITING ME");
    return NS_OK;
}
