/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxFork.h"

#include "LinuxCapabilities.h"
#include "LinuxSched.h"
#include "SandboxInfo.h"
#include "SandboxLogging.h"
#include "mozilla/Attributes.h"

#include <fcntl.h>
#include <sched.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mozilla {

SandboxForker::SandboxForker(base::ChildPrivileges aPrivs) {
  const auto& info = SandboxInfo::Get();
  bool canChroot = false;
  mFlags = 0;

  if (!info.Test(SandboxInfo::kHasUserNamespaces)) {
    return;
  }

  switch (aPrivs) {
  case base::PRIVILEGES_MEDIA:
    canChroot = info.Test(SandboxInfo::kHasSeccompBPF);
    mFlags |= CLONE_NEWNET | CLONE_NEWIPC;
    break;
  }

  if (canChroot || mFlags != 0) {
    mFlags |= CLONE_NEWUSER;
  }

  if (canChroot) {
    int fds[2];
    int rv = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    if (rv != 0) {
      SANDBOX_LOG_ERROR("socketpair: %s", strerror(errno)); 
      MOZ_CRASH("socketpair failed");
    }
    mChrootClient = fds[0];
    mChrootServer = fds[1];
  } else {
    int fds[2];
    int rv = pipe2(fds, O_CLOEXEC);
    if (rv != 0) {
      SANDBOX_LOG_ERROR("pipe2: %s", strerror(errno)); 
      MOZ_CRASH("pipe2 failed");
    }
    close(fds[0]);
    mChrootClient = fds[1];
    mChrootServer = -1;
  }
}

SandboxForker::~SandboxForker() {
  if (mChrootClient >= 0) {
    close(mChrootClient);
  }
  if (mChrootServer >= 0) {
    close(mChrootServer);
  }
}

pid_t
SandboxForker::Fork() {
  if (mFlags == 0) {
    return fork();
  }

  MOZ_CRASH("FIXME");
}

} // namespace mozilla
