/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_sandbox_UnsafeSyscallProxy_h
#define mozilla_sandbox_UnsafeSyscallProxy_h

namespace mozilla {

class UnsafeSyscallProxyImpl;

// This class creates a thread that executes system calls on behalf of
// other threads in the process.  This is used in order to enable
// sandboxing functionality when the process is single-threaded
// (seccomp and most other security attributes are per-thread) but
// still be able to use otherwise dangerous syscalls normally until
// the process is ready to be sandboxed.
//
// Obviously, this can't work with syscalls that affect the calling
// thread or otherwise care what thread they're run on, but typically
// these are either allowed by the sandbox policy (and so don't need
// to be proxied) or aren't used by the code being sandboxed.
//
// Also, because there's only one of these and multiple client
// threads, beware of deadlocks from proxying synchronization
// primitives.

class UnsafeSyscallProxy {
  UnsafeSyscallProxyImpl *mImpl;
public:
  bool Start();
  bool Stop();
  bool Call(unsigned long aSyscall, const unsigned long aArgs[6],
            long *aResult);
};

} // namespace mozilla

#endif // mozilla_sandbox_UnsafeSyscallProxy_h
