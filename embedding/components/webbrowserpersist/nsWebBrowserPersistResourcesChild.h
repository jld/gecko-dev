/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWebBrowserPersistResourcesChild_h__
#define nsWebBrowserPersistResourcesChild_h__

#include "mozilla/PWebBrowserPersistResourcesChild.h"

#include "nsIWebBrowserPersistDocument.h"

class nsWebBrowserPersistResourcesChild final
    : public mozilla::PWebBrowserPersistResourcesChild
    , public nsIWebBrowserPersistResourceVisitor
{
public:
    nsWebBrowserPersistResourcesChild();

    NS_DECL_NSIWEBBROWSERPERSISTRESOURCEVISITOR
    NS_DECL_ISUPPORTS
private:
    virtual ~nsWebBrowserPersistResourcesChild();
};

#endif // nsWebBrowserPersistDocumentChild_h__
