/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_XLIBDISPLAY_H
#define GFX_XLIBDISPLAY_H

#include <X11/Xlib.h>
#include "X11UndefineNone.h"

#include <memory>

namespace mozilla::gfx {

// Represents an X11 display connection which may be either borrowed
// (e.g., from GTK) or owned; in the latter case it will be closed
// with this object becomes unreferenced.  See also the `EglDisplay`
// class.
//
// FIXME more comments.
class XlibDisplay final {
 public:
  ~XlibDisplay();

  // Explicit `->get()` may be needed with some `Xlib.h` macros that
  // expand to C-style pointer casts.
  Display* get() const { return mDisplay; }
  operator Display*() const { return mDisplay; }

  using Ref = std::shared_ptr<XlibDisplay>;
  using WeakRef = std::weak_ptr<XlibDisplay>;

  static Ref Borrow(Display* aDisplay);
  static Ref Open(const char* aDisplayName);

 private:
  Display* mDisplay;
  bool mOwned;

  XlibDisplay(Display*, bool);
};

}  // namespace mozilla::gfx

#endif  // GFX_XLIBDISPLAY_H
