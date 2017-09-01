/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SandboxFork_h
#define mozilla_SandboxFork_h

#include "base/process_util.h"

namespace mozilla {

class SandboxForker {
public:
  explicit SandboxForker(base::ChildPrivileges aPrivs);
  ~SandboxForker();

  void RegisterFileDescriptors(base::file_handle_mapping_vector* aMap);
  pid_t Fork();

private:
  int mFlags;
  int mChrootServer;
  int mChrootClient;
  // For CloseSuperfluousFds in the chroot helper process:
  base::InjectiveMultimap mChrootMap;

  void StartChrootServer();
};

} // namespace mozilla

#endif // mozilla_SandboxFork_h
