/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteLookAndFeel.h"

#include "gfxFont.h"
#include "MainThreadUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Result.h"
#include "mozilla/ResultExtensions.h"
#include "nsXULAppAPI.h"

#include <limits>
#include <type_traits>
#include <utility>

namespace mozilla::widget {

RemoteLookAndFeel::RemoteLookAndFeel(FullLookAndFeel&& aTables)
    : mTables(std::move(aTables))
{
  MOZ_DIAGNOSTIC_ASSERT(!sSingleton);
  sSingleton = this;
}

RemoteLookAndFeel::~RemoteLookAndFeel() {
  MOZ_DIAGNOSTIC_ASSERT(sSingleton == this);
  sSingleton = nullptr;
}

// static
RemoteLookAndFeel* RemoteLookAndFeel::Get() {
  return sSingleton;
}

// static
void RemoteLookAndFeel::SetData(FullLookAndFeel&& aTables) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!sSingleton) {
    sSingleton = new RemoteLookAndFeel(std::move(aTables));
  } else {
    sSingleton->mTables = std::move(aTables);
  }
}

namespace {

template<typename Item, typename UInt, typename Index>
Result<const Item*, nsresult> MapLookup(const nsTArray<Item>& aItems,
                                        const nsTArray<UInt>& aMap,
                                        Index aIndex)
{
  UInt mapped = aMap[static_cast<size_t>(aIndex)];

  if (mapped == std::numeric_limits<UInt>::max()) {
    return Err(NS_ERROR_NOT_IMPLEMENTED);
  }

  return &aItems[static_cast<size_t>(mapped)];
}

template<typename Item, typename UInt>
void AddToMap(nsTArray<Item>* aItems,
              nsTArray<UInt>* aMap,
              Maybe<Item>&& aNewItem)
{
  if (aNewItem.isNothing()) {
    aMap->AppendElement(std::numeric_limits<UInt>::max());
    return;
  }

  size_t newIndex = aItems->Length();
  MOZ_ASSERT(newIndex < std::numeric_limits<UInt>::max());

  // The arrays should be small enough that sequential search is reasonable.
  for (size_t i = 0; i < newIndex; ++i) {
    if ((*aItems)[i] == aNewItem.ref()) {
      aMap->AppendElement(static_cast<UInt>(i));
      return;
    }
  }

  aItems->AppendElement(aNewItem.extract());
  aMap->AppendElement(static_cast<UInt>(newIndex));
}

} // namespace

nsresult RemoteLookAndFeel::NativeGetColor(ColorID aID, nscolor& aResult) {
  const nscolor* result;
  MOZ_TRY_VAR(result, MapLookup(mTables.colors(), mTables.colorMap(), aID));
  aResult = *result;
  return NS_OK;
}

nsresult RemoteLookAndFeel::GetIntImpl(IntID aID, int32_t& aResult) {
  const int32_t* result;
  MOZ_TRY_VAR(result, MapLookup(mTables.ints(), mTables.intMap(), aID));
  aResult = *result;
  return NS_OK;
}

nsresult RemoteLookAndFeel::GetFloatImpl(FloatID aID, float& aResult) {
  const float* result;
  MOZ_TRY_VAR(result, MapLookup(mTables.floats(), mTables.floatMap(), aID));
  aResult = *result;
  return NS_OK;
}

bool RemoteLookAndFeel::GetFontImpl(FontID aID, nsString& aFontName,
                                    gfxFontStyle& aFontStyle) {
  size_t index = static_cast<size_t>(aID) - static_cast<size_t>(FontID::MINIMUM);
  auto result = MapLookup(mTables.fonts(), mTables.fontMap(), index);
  if (result.isErr()) {
    return false;
  }

  const LookAndFeelFont& font = *result.unwrap();
  MOZ_ASSERT(font.haveFont());
  aFontName = font.name();
  aFontStyle = gfxFontStyle();
  aFontStyle.size = font.size();
  aFontStyle.weight = FontWeight(font.weight());
  aFontStyle.style = font.italic()
      ? FontSlantStyle::Italic()
      : FontSlantStyle::Normal();

  return true;
}

char16_t RemoteLookAndFeel::GetPasswordCharacterImpl() {
  return static_cast<char16_t>(mTables.passwordChar());
}

bool RemoteLookAndFeel::GetEchoPasswordImpl() {
  return mTables.passwordEcho();
}

// static
FullLookAndFeel RemoteLookAndFeel::ExtractData() {
  MOZ_ASSERT(!sSingleton, "nesting this is probably wrong?");
  return ExtractData(nsXPLookAndFeel::GetInstance());
}

// static
FullLookAndFeel RemoteLookAndFeel::ExtractData(nsXPLookAndFeel* aImpl) {
  FullLookAndFeel lf{};

  for (size_t i = 0; i < static_cast<size_t>(IntID::End); ++i) {
    int32_t theInt;
    nsresult rv = aImpl->GetIntImpl(static_cast<IntID>(i), theInt);
    AddToMap(&lf.ints(), &lf.intMap(),
             NS_SUCCEEDED(rv) ? Some(theInt) : Nothing{});
  }

  for (size_t i = 0; i < static_cast<size_t>(FloatID::End); ++i) {
    float theFloat;
    nsresult rv = aImpl->GetFloatImpl(static_cast<FloatID>(i), theFloat);
    AddToMap(&lf.floats(), &lf.floatMap(),
             NS_SUCCEEDED(rv) ? Some(theFloat) : Nothing{});
  }

  for (size_t i = 0; i < static_cast<size_t>(ColorID::End); ++i) {
    nscolor theColor;
    nsresult rv = aImpl->NativeGetColor(static_cast<ColorID>(i), theColor);
    AddToMap(&lf.colors(), &lf.colorMap(),
             NS_SUCCEEDED(rv) ? Some(theColor) : Nothing{});
  }

  for (size_t i = static_cast<size_t>(FontID::MINIMUM);
       i < static_cast<size_t>(FontID::MAXIMUM); ++i) {
    LookAndFeelFont font{};
    gfxFontStyle fontStyle{};

    bool rv = aImpl->GetFontImpl(static_cast<FontID>(i),
                                 font.name(), fontStyle);
    Maybe<LookAndFeelFont> maybeFont;
    if (rv) {
      font.haveFont() = true;
      font.size() = fontStyle.size;
      font.weight() = fontStyle.weight.ToFloat();
      if (fontStyle.style.IsItalic()) {
        font.italic() = true;
      } else {
        MOZ_ASSERT(fontStyle.style.IsNormal());
        font.italic() = false;
      }
      maybeFont = Some(std::move(font));
    }
    AddToMap(&lf.fonts(), &lf.fontMap(), std::move(maybeFont));
  }

  lf.passwordChar() = aImpl->GetPasswordCharacterImpl();
  lf.passwordEcho() = aImpl->GetEchoPasswordImpl();

  return lf;
}

RemoteLookAndFeel* RemoteLookAndFeel::sSingleton = nullptr;

} // namespace mozilla::widget
