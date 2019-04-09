/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ForkServer.h"

#include "base/eintr_wrapper.h"
#include "mozilla/ipc/FileDescriptorShuffle.h"
#include "mozilla/SandboxLaunch.h"
#include "nsTArray.h"

#include <unistd.h>

namespace mozilla {

ForkServer::ForkServer(const std::string& aExePath)
    : mExePath(aExePath) {}

ForkServer::~ForkServer = default;

static void SerializeLaunchInfo(const std::vector<std::string>& aArgv,
                                const base::LaunchOptions& aOptions,
                                base::Pickle* aPickle) {
  aPickle->WriteUInt32(static_cast<uint32_t>(aArgv.size()));
  for (const auto& arg : aArgv) {
    aPickle->WriteString(arg);
  }

  aPickle->WriteUInt32(static_cast<uint32_t>(aOptions.env_map.size()));
  for (const auto& kv : aOptions.env_map) {
    aPickle->WriteString(kv.first);
    aPickle->WriteString(kv.second);
  }

  aPickle->WriteUInt32(static_cast<uint32_t>(aOptions.fds_to_remap.size()));
  for (const auto& fds : aOptions.fds_to_remap) {
    // The `first` is attached elsewhere.
    aPickle->WriteInt(fds.second);
  }

  // fork_delegate is handled elsewhere.
}

static bool DeserializeLaunchInfo(const base::Pickle& aPickle,
                                  Span<const int> aSrcFds,
                                  std::vector<std::string>* aArgv,
                                  base::LaunchOptions* aOptions)
{
  base::PickleIterator iter(aPickle);

  uint32_t argc;
  if (!aPickle.ReadUInt32(&iter, &argc)) {
    return false;
  }
  for (uint32_t i = 0; i < argc; ++i) {
    std::string arg;
    if (!aPickle.ReadString(&iter, &arg)) {
      return false;
    }
    aArgv->push_back(arg);
  }

  uint32_t envc;
  if (!aPickle.ReadUInt32(&iter, &envc)) {
    return false;
  }
  for (uint32_t i = 0; i < envc; ++i) {
    std::string k, v;
    if (!aPickle.ReadString(&iter, &k) || !aPickle.ReadString(&iter, &v)) {
      return false;
    }
    aOptions->env_map[k] = v;
  }

  uint32_t fdc;
  if (!aPickle.ReadUInt32(&iter, &fdc)) {
    return false;
  }
  for (uint32_t i = 0; i < fdc; ++i) {
    int dstFd;
    if (!aPickle.ReadInt(&iter, &dstFd)) {
      return false;
    }
    aOptions.fds_to_remap.push_back({ aSrcFds[i], dstFd });
  }

  return true;
}

static bool WritePickleToFd(int fd, const Pickle& aPickle) {
  auto iter = aPickle.Buffers().Iter();
  while (!iter.Done()) {
    ssize_t rv = HANDLE_EINTR(write(fd, iter.Data(), iter.RemainingInSegment()));
    if (rv <= 0) {
      MOZ_ASSERT(rv < 0);
      return false;
    }
    iter.Advance(aPickle.Buffers(), static_cast<size_t>(rv));
  }
  return true;
}



} // namespace mozilla
