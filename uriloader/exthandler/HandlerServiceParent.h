#ifndef handler_service_parent_h
#define handler_service_parent_h

#include "mozilla/dom/PHandlerServiceParent.h"
#include "nsIMIMEInfo.h"

class nsIHandlerApp;

class HandlerServiceParent final : public mozilla::dom::PHandlerServiceParent
{
 public:
  HandlerServiceParent();
  NS_INLINE_DECL_REFCOUNTING(HandlerServiceParent)

 private:
  virtual ~HandlerServiceParent();
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;


  virtual mozilla::ipc::IPCResult RecvFillHandlerInfo(HandlerInfo&& aHandlerInfoData,
                                                      nsCString&& aOverrideType,
                                                      HandlerInfo* handlerInfoData) override;
  virtual mozilla::ipc::IPCResult RecvExists(HandlerInfo&& aHandlerInfo,
                                             bool* exits) override;

  virtual mozilla::ipc::IPCResult RecvGetTypeFromExtension(nsCString&& aFileExtension,
                                                           nsCString* type) override;

  virtual mozilla::ipc::IPCResult RecvExistsForProtocol(nsCString&& aProtocolScheme,
                                                        bool* aHandlerExists) override;

};

#endif
