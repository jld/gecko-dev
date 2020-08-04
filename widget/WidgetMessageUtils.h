/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_WidgetMessageUtils_h
#define mozilla_WidgetMessageUtils_h

#include "ipc/IPCMessageUtils.h"
#include "mozilla/LookAndFeel.h"
#include "nsIWidget.h"

namespace IPC {

template <>
struct ParamTraits<mozilla::LookAndFeel::IntID>
    : ContiguousEnumSerializer<mozilla::LookAndFeel::IntID,
                               mozilla::LookAndFeel::IntID::CaretBlinkTime,
                               mozilla::LookAndFeel::IntID::End>
{
  static_assert(static_cast<int32_t>(mozilla::LookAndFeel::IntID::CaretBlinkTime) == 0);
};

template <>
struct ParamTraits<nsTransparencyMode>
    : ContiguousEnumSerializerInclusive<nsTransparencyMode,
                                        eTransparencyOpaque,
                                        eTransparencyBorderlessGlass> {};

template <>
struct ParamTraits<nsCursor>
    : ContiguousEnumSerializer<nsCursor, eCursor_standard,
                               eCursorCount> {};

}  // namespace IPC

#endif  // WidgetMessageUtils_h
