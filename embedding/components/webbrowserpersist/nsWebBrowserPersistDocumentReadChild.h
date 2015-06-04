/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWebBrowserPersistDocumentReadChild_h__
#define nsWebBrowserPersistDocumentReadChild_h__

#include "mozilla/PWebBrowserPersistDocumentReadChild.h"

#include "nsIWebBrowserPersistDocument.h"

class nsWebBrowserPersistDocumentReadChild final
    : public mozilla::PWebBrowserPersistDocumentReadChild
    , public nsIWebBrowserPersistResourceVisitor
{
public:
    nsWebBrowserPersistDocumentReadChild();

    NS_DECL_NSIWEBBROWSERPERSISTRESOURCEVISITOR
    NS_DECL_ISUPPORTS
private:
    virtual ~nsWebBrowserPersistDocumentReadChild();
};

#endif // nsWebBrowserPersistDocumentChild_h__
