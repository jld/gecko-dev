/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWebBrowserPersistSerializeChild_h__
#define nsWebBrowserPersistSerializeChild_h__

#include "mozilla/PWebBrowserPersistSerializeChild.h"

#include "mozilla/PWebBrowserPersistDocument.h"
#include "nsIWebBrowserPersistDocument.h"
#include "nsIOutputStream.h"

class nsWebBrowserPersistSerializeChild final
    : public mozilla::PWebBrowserPersistSerializeChild
    , public nsIWebBrowserPersistWriteCompletion
    , public nsIWebBrowserPersistMap
    , public nsIOutputStream
{
    using WebBrowserPersistMap = mozilla::WebBrowserPersistMap;
public:
    explicit nsWebBrowserPersistSerializeChild(const WebBrowserPersistMap& aMap);

    NS_DECL_NSIWEBBROWSERPERSISTWRITECOMPLETION
    NS_DECL_NSIWEBBROWSERPERSISTMAP
    NS_DECL_NSIOUTPUTSTREAM
    NS_DECL_ISUPPORTS
private:
    WebBrowserPersistMap mMap;

    virtual ~nsWebBrowserPersistSerializeChild();
};

#endif // nsWebBrowserPersistSerializeChild_h__
