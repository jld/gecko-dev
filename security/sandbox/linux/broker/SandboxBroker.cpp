/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxBroker.h"
#include "../SandboxLogging.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef XP_LINUX
#include <sys/prctl.h>
#endif

#ifdef MOZ_WIDGET_GONK
#include <private/android_filesystem_config.h>
#include <sys/syscall.h>
#endif

#include "mozilla/Assertions.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/NullPtr.h"
#include "mozilla/ipc/FileDescriptor.h"

namespace mozilla {

/* static */ void*
SandboxBroker::ThreadMain(void *arg)
{
  static_cast<SandboxBroker*>(arg)->Main();
  return nullptr;
}

SandboxBroker::SandboxBroker(UniquePtr<const Policy> aPolicy, int aPid,
                             ipc::FileDescriptor& aClientFd)
  : mPid(aPid), mPolicy(Move(aPolicy))
{
  int fds[2];
  if (0 != socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds)) {
    // ???
    aClientFd = ipc::FileDescriptor();
  }
  mFileDesc = fds[0];
  aClientFd = ipc::FileDescriptor(fds[1]);
  close(fds[1]);

  // FIXME: can this use an existing thread class?
  pthread_create(&mThread, nullptr, ThreadMain, this);
}

SandboxBroker::~SandboxBroker() {
  shutdown(mFileDesc, SHUT_RD);
  // The thread will now get EOF even if the client hasn't exited.
  pthread_join(mThread, nullptr);
  // The fd will no longer be accessed.
  close(mFileDesc);
  // Nor will this object.
}

SandboxBroker::Policy::Policy() { }
SandboxBroker::Policy::~Policy() { }

static PLDHashOperator HashCopyCallback(nsCStringHashKey::KeyType aKey,
                                        int aData,
                                        void *aContext)
{
  static_cast<SandboxBroker::PathMap*>(aContext)->Put(aKey, aData);
  return PL_DHASH_NEXT;
}

SandboxBroker::Policy::Policy(const Policy& aOther) {
  aOther.mMap.EnumerateRead(HashCopyCallback, &mMap);
}

void
SandboxBroker::Policy::AddPath(int aPerms, const char* aPath,
                               bool aMightNotExist)
{
  nsDependentCString path(aPath);
  MOZ_ASSERT(path.Length() <= kMaxPathLen);
  int perms;
  if (!aMightNotExist) {
    struct stat statBuf;
    if (lstat(aPath, &statBuf) != 0) {
      return;
    }
  }
  if (!mMap.Get(path, &perms)) {
    perms = MAY_ACCESS;
  } else {
    MOZ_ASSERT(perms & MAY_ACCESS);
  }
  // SANDBOX_LOG_ERROR("policy for %s: %d -> %d", aPath, perms, perms | aPerms);
  perms |= aPerms;
  mMap.Put(path, perms);
}

void
SandboxBroker::Policy::AddTree(int aPerms, const char* aPath)
{
  struct stat statBuf;

  if (stat(aPath, &statBuf) != 0) {
    return;
  }
  if (!S_ISDIR(statBuf.st_mode)) {
    AddPath(aPerms, aPath, /* skip redundant lstat */ true);
  } else {
    DIR* dirp = opendir(aPath);
    struct dirent* de;
    if (!dirp) {
      return;
    }
    while ((de = readdir(dirp))) {
      if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
        continue;
      }
      // Note: could optimize the string handling.
      nsAutoCString subPath;
      subPath.Assign(aPath);
      subPath.Append('/');
      subPath.Append(de->d_name);
      AddTree(aPerms, subPath.get());
    }
    closedir(dirp);
  }
}

void
SandboxBroker::Policy::AddPrefix(int aPerms, const char* aDir,
                                 const char* aPrefix)
{
  size_t prefixLen = strlen(aPrefix);
  DIR* dirp = opendir(aDir);
  struct dirent* de;
  if (!dirp) {
    return;
  }
  while ((de = readdir(dirp))) {
    if (strncmp(de->d_name, aPrefix, prefixLen) == 0) {
      nsAutoCString subPath;
      subPath.Assign(aDir);
      subPath.Append('/');
      subPath.Append(de->d_name);
      AddPath(aPerms, subPath.get(), /* skip redundant lstat */ true);
    }
  }
  closedir(dirp);
}

int
SandboxBroker::Policy::Lookup(const nsACString& aPath) const
{
  return mMap.Get(aPath);
}

static bool
AllowAccess(int aReqFlags, int aPerms)
{
  if (aReqFlags & ~(R_OK|W_OK|F_OK)) {
    return false;
  }
  int needed = 0;
  if (aReqFlags & R_OK) {
    needed |= SandboxBroker::MAY_READ;
  }
  if (aReqFlags & W_OK) {
    needed |= SandboxBroker::MAY_WRITE;
  }
  return (aPerms & needed) == needed;
}

// FIXME: should these move to Common?
static const int kRequiredOpenFlags = O_CLOEXEC | O_NOCTTY;

// FIXME: explain more
#define O_SYNC_NEW 04010000
static const int kAllowedOpenFlags =
  O_APPEND | O_ASYNC | O_DIRECT | O_DIRECTORY | O_EXCL | O_LARGEFILE
  | O_NOATIME | O_NOCTTY | O_NOFOLLOW | O_NONBLOCK | O_NDELAY | O_SYNC_NEW
  | O_TRUNC | O_CLOEXEC | O_CREAT;
#undef O_SYNC_NEW

static bool
AllowOpen(int aReqFlags, int aPerms)
{
  if (aReqFlags & ~O_ACCMODE & ~kAllowedOpenFlags) {
    return false;
  }
  int needed;
  switch(aReqFlags & O_ACCMODE) {
  case O_RDONLY:
    needed = SandboxBroker::MAY_READ;
    break;
  case O_WRONLY:
    needed = SandboxBroker::MAY_WRITE;
    break;
  case O_RDWR:
    needed = SandboxBroker::MAY_READ | SandboxBroker::MAY_WRITE;
    break;
  default:
    return false;
  }
  if (aReqFlags & O_CREAT) {
    needed |= SandboxBroker::MAY_CREATE;
  }
  return (aPerms & needed) == needed;
}

static int
DoStat(const char* aPath, struct stat* aStat, int aFlags)
{
  if (aFlags & O_NOFOLLOW) {
    return lstat(aPath, aStat);
  }
  return stat(aPath, aStat);
}

void
SandboxBroker::Main(void)
{
#ifdef XP_LINUX
  char threadName[16];
  snprintf(threadName, sizeof(threadName), "FileProxy %d", mPid);
  prctl(PR_SET_NAME, threadName);
#endif

#ifdef MOZ_WIDGET_GONK
#ifdef __NR_setreuid32
  static const long nr_setreuid = __NR_setreuid32;
  static const long nr_setregid = __NR_setregid32;
#else
  static const long nr_setreuid = __NR_setreuid;
  static const long nr_setregid = __NR_setregid;
#endif
  if (syscall(nr_setregid, getgid(), AID_APP + mPid) != 0 ||
      syscall(nr_setreuid, getuid(), AID_APP + mPid) != 0) {
    MOZ_CRASH("SandboxBroker: failed to drop privileges");
  }
#endif

  while (true) {
    struct iovec ios[2];
    char pathBuf[kMaxPathLen + 1];
    size_t pathLen;
    struct stat statBuf;
    Request req;
    Response resp;
    int respfd;

    ios[0].iov_base = &req;
    ios[0].iov_len = sizeof(req);
    ios[1].iov_base = pathBuf;
    ios[1].iov_len = kMaxPathLen;

    const ssize_t recvd = RecvWithFd(mFileDesc, ios, 2, &respfd);
    if (recvd == 0) {
      // SANDBOX_LOG_ERROR("EOF from pid %d", mPid);
      break;
    }
    if (recvd < static_cast<ssize_t>(sizeof(req))) {
      SANDBOX_LOG_ERROR("bad read from pid %d (%zd < %zu)",
                        mPid, recvd, sizeof(req));
      // Error or short read.
      // (FIXME: distinguish expected errors to prevent infinite loop.)
      continue;
    }
    if (respfd == -1) { 
      SANDBOX_LOG_ERROR("no response fd from pid %d", mPid);
      // FIXME: warn?
      continue;
    }

    memset(&resp, 0, sizeof(resp));
    memset(&statBuf, 0, sizeof(statBuf));
    resp.mError = EACCES;
    ios[0].iov_base = &resp;
    ios[0].iov_len = sizeof(resp);
    ios[1].iov_base = nullptr;
    ios[1].iov_len = 0;
    int openedFd = -1;

    pathLen = recvd - sizeof(req);
    MOZ_ASSERT(pathLen <= kMaxPathLen);
    pathBuf[pathLen] = '\0';
    int perms = 0;
    if (!memchr(pathBuf, '\0', pathLen)) {
      perms = mPolicy->Lookup(nsDependentCString(pathBuf, pathLen));
    }
    if (perms & MAY_ACCESS) {
      switch(req.mOp) {
      case SANDBOX_FILE_OPEN:
        if (AllowOpen(req.mFlags, perms)) {
          // O_CREAT is forbidden, but pass 0 as mode just in case.
          openedFd = open(pathBuf, req.mFlags | kRequiredOpenFlags, 0);
          if (openedFd >= 0) {
            resp.mError = 0;
          } else {
            resp.mError = errno;
          }
        }
        break;

      case SANDBOX_FILE_ACCESS:
        if (AllowAccess(req.mFlags, perms)) {
          // This can't use access() itself because that uses the ruid
          // and not the euid.  In theory faccessat() with AT_EACCESS
          // would work, but Linux doesn't actually implement the
          // flags != 0 case; glibc has a hack which doesn't even work
          // in this case so it'll ignore the flag, and Bionic just
          // passes through the syscall and always ignores the flags.
          //
          // Instead, because we've already checked the requested
          // r/w/x bits against the policy, just return success if the
          // file exists and hope that's close enough.
          if (stat(pathBuf, &statBuf) == 0) {
            resp.mError = 0;
          } else {
            resp.mError = errno;
          }
        }
        break;

      case SANDBOX_FILE_STAT:
        if (DoStat(pathBuf, &statBuf, req.mFlags) == 0) {
          resp.mError = 0;
          ios[1].iov_base = &statBuf;
          ios[1].iov_len = sizeof(statBuf);
        } else {
          resp.mError = errno;
        }
        break;
      }
    } else {
      MOZ_ASSERT(perms == 0);
    }

    const size_t numIO = ios[1].iov_len > 0 ? 2 : 1;
    DebugOnly<const ssize_t> sent = SendWithFd(respfd, ios, numIO, openedFd);
    close(respfd);
    MOZ_ASSERT(sent < 0 ||
               static_cast<size_t>(sent) == ios[0].iov_len + ios[1].iov_len);

    if (openedFd >= 0) {
      close(openedFd);
    }
  }
}

} // namespace mozilla
