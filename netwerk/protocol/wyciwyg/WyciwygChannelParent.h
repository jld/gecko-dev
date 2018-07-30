/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_WyciwygChannelParent_h
#define mozilla_net_WyciwygChannelParent_h

#include "mozilla/net/PWyciwygChannelParent.h"
#include "mozilla/net/NeckoCommon.h"
#include "nsIStreamListener.h"

#include "nsIWyciwygChannel.h"
#include "nsIInterfaceRequestor.h"
#include "nsILoadContext.h"

namespace mozilla {
namespace dom {
  class PBrowserParent;
} // namespace dom

namespace net {

class WyciwygChannelParent : public PWyciwygChannelParent
                           , public nsIStreamListener
                           , public nsIInterfaceRequestor
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIINTERFACEREQUESTOR

  WyciwygChannelParent();

protected:
  virtual ~WyciwygChannelParent() = default;

  virtual mozilla::ipc::IPCResult RecvInit(URIParams&& uri,
                                           ipc::PrincipalInfo&& aRequestingPrincipalInfo,
                                           ipc::PrincipalInfo&& aTriggeringPrincipalInfo,
                                           ipc::PrincipalInfo&& aPrincipalToInheritInfo,
                                           uint32_t&& aSecurityFlags,
                                           uint32_t&& aContentPolicyType) override;
  virtual mozilla::ipc::IPCResult RecvAsyncOpen(URIParams&& original,
                                                uint32_t&& loadFlags,
                                                IPC::SerializedLoadContext&& loadContext,
                                                PBrowserOrId&& parent) override;
  virtual mozilla::ipc::IPCResult RecvWriteToCacheEntry(nsDependentSubstring&& data) override;
  virtual mozilla::ipc::IPCResult RecvCloseCacheEntry(nsresult&& reason) override;
  virtual mozilla::ipc::IPCResult RecvSetCharsetAndSource(int32_t&& source,
                                                          nsCString&& charset) override;
  virtual mozilla::ipc::IPCResult RecvSetSecurityInfo(nsCString&& securityInfo) override;
  virtual mozilla::ipc::IPCResult RecvCancel(nsresult&& statusCode) override;
  virtual mozilla::ipc::IPCResult RecvAppData(IPC::SerializedLoadContext&& loadContext,
                                              PBrowserOrId&& parent) override;

  virtual void ActorDestroy(ActorDestroyReason why) override;

  bool SetupAppData(const IPC::SerializedLoadContext& loadContext,
                    const PBrowserOrId &aParent);

  nsCOMPtr<nsIWyciwygChannel> mChannel;
  bool mIPCClosed;
  bool mReceivedAppData;
  nsCOMPtr<nsILoadContext> mLoadContext;
};

} // namespace net
} // namespace mozilla

#endif // mozilla_net_WyciwygChannelParent_h
