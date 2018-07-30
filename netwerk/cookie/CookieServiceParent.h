/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_CookieServiceParent_h
#define mozilla_net_CookieServiceParent_h

#include "mozilla/net/PCookieServiceParent.h"

class nsCookie;
class nsICookie;
class nsCookieService;
namespace mozilla { class OriginAttributes; }

namespace mozilla {
namespace net {

class CookieServiceParent : public PCookieServiceParent
{
public:
  CookieServiceParent();
  virtual ~CookieServiceParent() = default;

  void TrackCookieLoad(nsIChannel *aChannel);

  void RemoveBatchDeletedCookies(nsIArray *aCookieList);

  void RemoveAll();

  void RemoveCookie(nsICookie *aCookie);

  void AddCookie(nsICookie *aCookie);

  // This will return true if the CookieServiceParent is currently processing
  // an update from the content process. This is used in ContentParent to make
  // sure that we are only forwarding those cookie updates to other content
  // processes, not the one they originated from.
  bool ProcessingCookie() { return mProcessingCookie; }
protected:
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  virtual mozilla::ipc::IPCResult RecvGetCookieString(URIParams&& aHost,
                                                      bool&& aIsForeign,
                                                      bool&& aIsTrackingResource,
                                                      bool&& aFirstPartyStorageAccessGranted,
                                                      bool&& aIsSafeTopLevelNav,
                                                      bool&& aIsSameSiteForeign,
                                                      OriginAttributes&& aAttrs,
                                                      nsCString* aResult) override;

  virtual mozilla::ipc::IPCResult RecvSetCookieString(URIParams&& aHost,
                                                      URIParams&& aChannelURI,
                                                      bool&& aIsForeign,
                                                      bool&& aIsTrackingResource,
                                                      bool&& aFirstPartyStorageAccessGranted,
                                                      nsCString&& aCookieString,
                                                      nsCString&& aServerTime,
                                                      OriginAttributes&& aAttrs,
                                                      bool&& aFromHttp) override;
  virtual
  mozilla::ipc::IPCResult RecvPrepareCookieList(URIParams&& aHost,
                                                bool&& aIsForeign,
                                                bool&& aIsTackingResource,
                                                bool&& aFirstPartyStorageAccessGranted,
                                                bool&& aIsSafeTopLevelNav,
                                                bool&& aIsSameSiteForeign,
                                                OriginAttributes&& aAttrs) override;

  void
  SerialializeCookieList(const nsTArray<nsCookie*> &aFoundCookieList,
                         nsTArray<CookieStruct> &aCookiesList,
                         nsIURI *aHostURI);

  RefPtr<nsCookieService> mCookieService;
  bool mProcessingCookie;
};

} // namespace net
} // namespace mozilla

#endif // mozilla_net_CookieServiceParent_h

