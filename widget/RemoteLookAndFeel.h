/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_RemoteLookAndFeel_h__
#define mozilla_widget_RemoteLookAndFeel_h__

#include "mozilla/widget/nsXPLookAndFeel.h"
#include "mozilla/widget/LookAndFeelTypes.h"

namespace mozilla::widget {

class RemoteLookAndFeel final : public nsXPLookAndFeel {
 public:
  static RemoteLookAndFeel* Get();
  virtual ~RemoteLookAndFeel();

  void NativeInit() override { }

  nsresult NativeGetColor(ColorID aID, nscolor& aResult) override;
  nsresult GetIntImpl(IntID aID, int32_t& aResult) override;
  nsresult GetFloatImpl(FloatID aID, float& aResult) override;
  bool GetFontImpl(FontID aID, nsString& aFontName,
                   gfxFontStyle& aFontStyle) override;

  void RefreshImpl() override { }
  char16_t GetPasswordCharacterImpl() override;
  bool GetEchoPasswordImpl() override;

  static void SetData(FullLookAndFeel&& aTables);
  static FullLookAndFeel ExtractData(nsXPLookAndFeel* aImpl);
  static FullLookAndFeel ExtractData();

 private:
  FullLookAndFeel mTables;

  static RemoteLookAndFeel* sSingleton;

  explicit RemoteLookAndFeel(FullLookAndFeel&& aTables);
};

} // namespace mozilla::widget


#endif // mozilla_widget_RemoteLookAndFeel_h__
