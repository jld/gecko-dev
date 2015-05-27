/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWebBrowserPersistLocalDocument_h__
#define nsWebBrowserPersistLocalDocument_h__

#include "nsCOMPtr.h"
#include "nsIDocument.h"
#include "nsIURI.h"
#include "nsIWebBrowserPersistDocument.h"

class nsWebBrowserPersistLocalDocument final
    : public nsIWebBrowserPersistDocument
{
private:
    virtual ~nsWebBrowserPersistLocalDocument();
    nsCOMPtr<nsIDocument> mDocument; // Reference cycles?
public:
    nsWebBrowserPersistLocalDocument(nsIDocument* aDocument);

    NS_DECL_ISUPPORTS
    NS_DECL_NSIWEBBROWSERPERSISTDOCUMENT
};

#endif // nsWebBrowserPersistLocalDocument_h__
