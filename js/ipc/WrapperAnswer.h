/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=80:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_jsipc_WrapperAnswer_h_
#define mozilla_jsipc_WrapperAnswer_h_

#include "JavaScriptShared.h"

namespace mozilla {

namespace dom {
class AutoJSAPI;
} // namespace dom

namespace jsipc {

class WrapperAnswer : public virtual JavaScriptShared
{
  public:
    bool RecvPreventExtensions(ObjectId&& objId, ReturnStatus* rs);
    bool RecvGetPropertyDescriptor(ObjectId&& objId, JSIDVariant&& id,
                                   ReturnStatus* rs,
                                   PPropertyDescriptor* out);
    bool RecvGetOwnPropertyDescriptor(ObjectId&& objId,
                                      JSIDVariant&& id,
                                      ReturnStatus* rs,
                                      PPropertyDescriptor* out);
    bool RecvDefineProperty(ObjectId&& objId, JSIDVariant&& id,
                            PPropertyDescriptor&& flags, ReturnStatus* rs);
    bool RecvDelete(ObjectId&& objId, JSIDVariant&& id, ReturnStatus* rs);

    bool RecvHas(ObjectId&& objId, JSIDVariant&& id,
                 ReturnStatus* rs, bool* foundp);
    bool RecvHasOwn(ObjectId&& objId, JSIDVariant&& id,
                    ReturnStatus* rs, bool* foundp);
    bool RecvGet(ObjectId&& objId, JSVariant&& receiverVar,
                 JSIDVariant&& id,
                 ReturnStatus* rs, JSVariant* result);
    bool RecvSet(ObjectId&& objId, JSIDVariant&& id, JSVariant&& value,
                 JSVariant&& receiverVar, ReturnStatus* rs);

    bool RecvIsExtensible(ObjectId&& objId, ReturnStatus* rs,
                          bool* result);
    bool RecvCallOrConstruct(ObjectId&& objId, InfallibleTArray<JSParam>&& argv,
                             bool&& construct, ReturnStatus* rs, JSVariant* result,
                             nsTArray<JSParam>* outparams);
    bool RecvHasInstance(ObjectId&& objId, JSVariant&& v, ReturnStatus* rs, bool* bp);
    bool RecvGetBuiltinClass(ObjectId&& objId, ReturnStatus* rs,
                             uint32_t* classValue);
    bool RecvIsArray(ObjectId&& objId, ReturnStatus* rs, uint32_t* ans);
    bool RecvClassName(ObjectId&& objId, nsCString* result);
    bool RecvGetPrototype(ObjectId&& objId, ReturnStatus* rs, ObjectOrNullVariant* result);
    bool RecvGetPrototypeIfOrdinary(ObjectId&& objId, ReturnStatus* rs, bool* isOrdinary,
                                    ObjectOrNullVariant* result);
    bool RecvRegExpToShared(ObjectId&& objId, ReturnStatus* rs, nsString* source, uint32_t* flags);

    bool RecvGetPropertyKeys(ObjectId&& objId, uint32_t&& flags,
                             ReturnStatus* rs, nsTArray<JSIDVariant>* ids);
    bool RecvInstanceOf(ObjectId&& objId, JSIID&& iid,
                        ReturnStatus* rs, bool* instanceof);
    bool RecvDOMInstanceOf(ObjectId&& objId, int&& prototypeID, int&& depth,
                           ReturnStatus* rs, bool* instanceof);

    bool RecvDropObject(ObjectId&& objId);

  private:
    bool fail(dom::AutoJSAPI& jsapi, ReturnStatus* rs);
    bool ok(ReturnStatus* rs);
    bool ok(ReturnStatus* rs, const JS::ObjectOpResult& result);
    bool deadCPOW(dom::AutoJSAPI& jsapi, ReturnStatus* rs);
};

} // namespace jsipc
} // namespace mozilla

#endif
