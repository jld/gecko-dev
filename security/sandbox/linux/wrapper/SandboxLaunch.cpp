/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxLaunch.h"
#include "SandboxWrapperDefs.h"

#include "base/file_path.h"
#include "mozilla/Assertions.h"
#include "mozilla/Move.h"
#include "nsNativeCharsetUtils.h"
#include "nsString.h"
#include "nsXPCOMPrivate.h"
#include "prenv.h"

#include <stdio.h>

namespace mozilla {

void
SandboxLaunchAdjust(std::vector<std::string>* aArgv,
		    base::environment_map* aEnv,
		    GeckoProcessType aType)
{
  // Borrowed from GeckoChildProcessHost::GetPathToBinary:
  nsCString path;
  NS_CopyUnicodeToNative(nsDependentString(gGREBinPath), path);
  const FilePath gre_dir(path.get());
  const FilePath wrapper = gre_dir.AppendASCII(MOZ_NAMESPACE_SANDBOX_NAME);

  bool wrapper_exists = access(wrapper.value().c_str(), X_OK) == 0;
  if (!wrapper_exists) {
    // FIXME: complain better
    perror(wrapper.value().c_str());
    MOZ_ASSERT(wrapper_exists);
    return;
  }

  aArgv->insert(aArgv->begin(), wrapper.value());

  // TODO: env vars, etc.
}

} // namespace mozilla
