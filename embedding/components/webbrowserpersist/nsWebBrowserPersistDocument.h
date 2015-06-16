/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWebBrowserPersistDocument_h__
#define nsWebBrowserPersistDocument_h__

#include "nsCOMPtr.h"
#include "nsIDocument.h"
#include "nsIURI.h"
#include "nsIWebBrowserPersistDocument.h"

class nsIDocumentEncoder;

class nsWebBrowserPersistDocument final
    : public nsIWebBrowserPersistDocument
{
public:
    explicit nsWebBrowserPersistDocument(nsIDocument* aDocument);
    
    const nsCString& GetCharacterSet() const;
    uint32_t GetPersistFlags() const;
    already_AddRefed<nsIURI> GetBaseURI() const;

    NS_DECL_ISUPPORTS
    NS_DECL_NSIWEBBROWSERPERSISTDOCUMENT

private:
    nsCOMPtr<nsIDocument> mDocument; // Reference cycles?
    uint32_t mPersistFlags;

    void DecideContentType(nsACString& aContentType);
    nsresult GetDocEncoder(const nsACString& aContentType,
                           uint32_t aEncoderFlags,
                           nsIDocumentEncoder** aEncoder);

    virtual ~nsWebBrowserPersistDocument();
};

#endif // nsWebBrowserPersistDocument_h__
