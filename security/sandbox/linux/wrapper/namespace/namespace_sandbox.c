/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// TODO: roll my own assert() that doesn't use abort(), because that
// will misfire after clone().
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "SandboxWrapperDefs.h"

static volatile pid_t gChildPid = 0;

static void
WaitAndExit(pid_t pid)
{
  int status;
  pid_t wpid;

  do {
    wpid = waitpid(pid, &status, 0);
  } while (wpid == -1 && errno == EINTR);

  if (wpid == -1) {
    perror("waitpid");
    exit(-1);
  }
  assert(wpid == pid);

  gChildPid = 0;
  if (WIFEXITED(status)) {
    exit(WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    raise(WTERMSIG(status));
  }
  exit(-1);
}

static void
ForwardSignal(int signum)
{
  pid_t pid = gChildPid;
  if (pid > 0) {
    kill(pid, signum);
    return;
  }
  signal(signum, SIG_DFL);
  raise(signum);
}

static void
InstallSignalForwarders(pid_t pid)
{
  // There's a race window before these signal handlers are set up;
  // this could be fixed by blocking the signals, but that wouldn't
  // help with SIGKILL, so I don't know if it's worth it.
  gChildPid = pid;

  signal(SIGTERM, ForwardSignal);
  // SIGTERM is the important one, but cover all the usual signals.
  signal(SIGINT, ForwardSignal);
  signal(SIGQUIT, ForwardSignal);
  signal(SIGHUP, ForwardSignal);
  signal(SIGABRT, ForwardSignal);
}

// Fork a child process, wait for it while forwarding terminal
// signals, and exit; returns in the child process.  (When this is
// finished it will be clone() instead.)
static void
DoFork()
{
  pid_t pid;

  pid = fork();
  if (pid < 0) {
    perror("fork");
    exit(-1);
  }
  if (pid > 0) {
    InstallSignalForwarders(pid);
    WaitAndExit(pid);
  }

  // Forward SIGKILL (and SIGKILL for any unexpected death of the
  // wrapper process).  Unfortunately, this doesn't work if the parent
  // has already died, so there's a race window.
  prctl(PR_SET_PDEATHSIG, SIGKILL);

  // TODO: close extra file descriptors.
}

int
main(int argc, char** argv)
{
  DoFork();
  execvp(argv[1], argv + 1);
  perror(argv[1]);
  _exit(-1);
}
