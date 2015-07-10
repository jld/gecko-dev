/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SandboxBroker_h
#define mozilla_SandboxBroker_h

#include "mozilla/SandboxBrokerCommon.h"

#include <pthread.h>

#include "mozilla/Attributes.h"
#include "mozilla/UniquePtr.h"
#include "nsDataHashtable.h"
#include "nsHashKeys.h"
#include "nsString.h"

namespace mozilla {

namespace ipc {
class FileDescriptor;
}

class SandboxBroker final : private SandboxBrokerCommon {
 public:
  enum Perms {
    MAY_ACCESS = 1 << 0,
    MAY_READ = 1 << 1,
    MAY_WRITE = 1 << 2,
    MAY_CREATE = 1 << 3,
  };
  // C++ requires casts to use an enum as a bitmask, so just use int:
  typedef nsDataHashtable<nsCStringHashKey, int> PathMap;

  class Policy {
    PathMap mMap;
  public:
    Policy();
    Policy(const Policy& aOther);
    ~Policy();
    void AddPath(int aPerms, const char* aPath, bool aMightNotExist);
    void AddTree(int aPerms, const char* aPath);
    void AddPrefix(int aPerms, const char* aDir, const char* aPrefix);
    void AddPath(int aPerms, const char* aPath) {
      AddPath(aPerms, aPath, aPerms & MAY_CREATE);
    }
    int Lookup(const nsACString& aPath) const;
    int Lookup(const char* aPath) const {
      return Lookup(nsDependentCString(aPath));
    }
  };

  SandboxBroker(UniquePtr<const Policy> aPolicy, int aPid,
                ipc::FileDescriptor& aClientFd);
  ~SandboxBroker();
  ipc::FileDescriptor Start();
 private:
  pthread_t mThread;
  int mFileDesc;
  const int mPid;
  const UniquePtr<const Policy> mPolicy;

  void Main(void);
  static void* ThreadMain(void*);
};

} // namespace mozilla

#endif // mozilla_SandboxBroker_h
