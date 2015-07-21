/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxBrokerPolicy.h"

#include "mozilla/ClearOnShutdown.h"
#include "nsThreadUtils.h"

namespace mozilla {

SandboxBrokerPolicyFactory::SandboxBrokerPolicyFactory()
{
#if defined(MOZ_CONTENT_SANDBOX) && defined(MOZ_WIDGET_GONK)
  static const int rdonly = SandboxBroker::MAY_READ;
  static const int wronly = SandboxBroker::MAY_WRITE;
  static const int rdwr = rdonly | wronly;
  static const int wrlog = wronly | SandboxBroker::MAY_CREATE;

  SandboxBroker::Policy* policy = new SandboxBroker::Policy;

  policy->AddPath(rdwr, "/dev/genlock");  // bug 980924
  policy->AddPath(rdwr, "/dev/ashmem");   // bug 980947 (dangerous!)
  policy->AddPrefix(rdwr, "/dev", "kgsl");  // bug 995072
  policy->AddPath(rdwr, "/dev/qemu_pipe"); // NO BUG YET; goldfish gralloc?
  policy->AddTree(wronly, "/dev/log"); // NO BUG YET (also, why?)
  policy->AddPath(rdonly, "/dev/urandom");  // bug 964500, bug 995069
  policy->AddPath(rdonly, "/dev/ion");      // bug 980937
  policy->AddPath(rdonly, "/proc/cpuinfo"); // bug 995067
  policy->AddPath(rdonly, "/proc/meminfo"); // bug 1025333
  policy->AddPath(rdonly, "/proc/stat"); // Unsure if bug; sysconf
  policy->AddPath(rdonly, "/sys/devices/system/cpu"); // NO BUG; sysconf
  policy->AddPath(rdonly, "/sys/devices/system/cpu/present"); // bug 1025329
  policy->AddPath(rdonly, "/sys/devices/system/soc/soc0/id"); // bug 1025339
  policy->AddPath(rdonly, "/etc/media_profiles.xml"); // NO BUG YET; camera.
  policy->AddPath(rdonly, "/etc/media_codecs.xml"); // NO BUG YET; video decode
  policy->AddTree(rdonly, "/system/fonts"); // bug 1026063

  // Things known to be in /system/b2g and used in content:
  // * NSS libraries
  // * Possibly web apps, depending on build type
  // * Reftest data
  // * Speech recognition models
  // Given that people are probably going to keep throwing stuff
  // into this directory, the whole thing gets whitelisted for now.
  // (For future reference: crossplatformly, this is NS_GRE_DIR.)
  policy->AddTree(rdonly, "/system/b2g");

  // Dynamic library loading from assorted frameworks we don't control
  // (media codecs, maybe others).
  policy->AddTree(rdonly, "/system/lib");
  policy->AddTree(rdonly, "/vendor/lib");

  policy->AddTree(rdonly, "/system/usr/share/zoneinfo"); // Timezones???

  // FIXME: conditionalize this on actually running mochitests.
  policy->AddPath(wrlog, "/data/local/tests/log/mochitest.log");

  mCommonContentPolicy.reset(policy);
#endif
}

#ifdef MOZ_CONTENT_SANDBOX
UniquePtr<SandboxBroker::Policy>
SandboxBrokerPolicyFactory::GetContentPolicy(int aPid)
{
#if defined(MOZ_WIDGET_GONK)
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mCommonContentPolicy);
  UniquePtr<SandboxBroker::Policy>
    policy(new SandboxBroker::Policy(*mCommonContentPolicy));

  // FIXME: profile thing goes here
  return policy;
#else // MOZ_WIDGET_GONK
  // Not implemented for desktop yet.
  return nullptr;
#endif
}

#endif // MOZ_CONTENT_SANDBOX
} // namespace mozilla
