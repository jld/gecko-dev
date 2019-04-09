/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_ForkServer_h
#define mozilla_ipc_ForkServer_h

#include "base/process_util.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/Maybe.h"

#include <string>

namespace mozilla {

class ForkServer final {
 public:
  ForkServer(const std::string& aExePath);
  ~ForkServer();

  // FIXME should use something better than bool.
  bool Start();

  // FIXME shutdown?

  // This definitely needs an error type: can reject unsatisfiable
  // launches instead of making the caller pre-check?
  Maybe<base::ProcessHandle> LaunchApp(const std::vector<std::string>& aArgv,
                                       const base::LaunchOptions& aOptions);

 private:
  UniqueFileHandle mSocket;
  std::string mExePath;
};

} // namespace mozilla

#endif // mozilla_ipc_ForkServer_h
	
