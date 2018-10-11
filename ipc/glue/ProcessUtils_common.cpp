/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProcessUtils.h"

#include "mozilla/Preferences.h"
#include "mozilla/UniquePtrExtensions.h"

namespace mozilla {
namespace ipc {

SharedPreferenceSerializer::SharedPreferenceSerializer() : mPrefMapSize(0) {
  MOZ_COUNT_CTOR(SharedPreferenceSerializer);
}

SharedPreferenceSerializer::~SharedPreferenceSerializer() {
  MOZ_COUNT_DTOR(SharedPreferenceSerializer);
}

SharedPreferenceSerializer::SharedPreferenceSerializer(
    SharedPreferenceSerializer&& aOther)
    : mPrefMapSize(aOther.mPrefMapSize),
      mPrefsLength(aOther.mPrefsLength),
      mPrefMapHandle(std::move(aOther.mPrefMapHandle)),
      mPrefsHandle(std::move(aOther.mPrefsHandle)) {
  MOZ_COUNT_CTOR(SharedPreferenceSerializer);
}

bool SharedPreferenceSerializer::SerializeToSharedMemory() {
  mPrefMapHandle =
      Preferences::EnsureSnapshot(&mPrefMapSize).TakePlatformHandle();

  // Serialize the early prefs.
  nsAutoCStringN<1024> prefs;
  Preferences::SerializePreferences(prefs);
  mPrefsLength = prefs.Length();

  base::SharedMemory shm;
  // Set up the shared memory.
  if (!shm.Create(prefs.Length())) {
    NS_ERROR("failed to create shared memory in the parent");
    return false;
  }
  if (!shm.Map(prefs.Length())) {
    NS_ERROR("failed to map shared memory in the parent");
    return false;
  }

  // Copy the serialized prefs into the shared memory.
  memcpy(static_cast<char*>(shm.memory()), prefs.get(), mPrefsLength);

  mPrefsHandle = shm.TakeHandle();
  return true;
}

#ifdef ANDROID
static int gPrefsFd = -1;
static int gPrefMapFd = -1;

void SetPrefsFd(int aFd) { gPrefsFd = aFd; }

void SetPrefMapFd(int aFd) { gPrefMapFd = aFd; }
#endif

SharedPreferenceDeserializer::SharedPreferenceDeserializer() {
  MOZ_COUNT_CTOR(SharedPreferenceDeserializer);
}

SharedPreferenceDeserializer::~SharedPreferenceDeserializer() {
  MOZ_COUNT_DTOR(SharedPreferenceDeserializer);
}

bool SharedPreferenceDeserializer::DeserializeFromSharedMemory(
    char* aPrefsHandleStr, char* aPrefMapHandleStr, char* aPrefsLenStr,
    char* aPrefMapSizeStr) {
  // Parses an arg containing a pointer-sized-integer.
  auto parseUIntPtrArg = [](char*& aArg) {
    // ContentParent uses %zu to print a word-sized unsigned integer. So
    // even though strtoull() returns a long long int, it will fit in a
    // uintptr_t.
    return uintptr_t(strtoull(aArg, &aArg, 10));
  };

#ifdef XP_WIN
  auto parseHandleArg = [&](char*& aArg) {
    return HANDLE(parseUIntPtrArg(aArg));
  };

  mPrefsHandle = Some(parseHandleArg(aPrefsHandleStr));
  if (aPrefsHandleStr[0] != '\0') {
    return false;
  }

  FileDescriptor::UniquePlatformHandle handle(
      parseHandleArg(aPrefMapHandleStr));
  if (aPrefMapHandleStr[0] != '\0') {
    return false;
  }

  mPrefMapHandle.emplace(std::move(handle));
#endif

  mPrefsLen = Some(parseUIntPtrArg(aPrefsLenStr));
  if (aPrefsLenStr[0] != '\0') {
    return false;
  }

  mPrefMapSize = Some(parseUIntPtrArg(aPrefMapSizeStr));
  if (aPrefMapSizeStr[0] != '\0') {
    return false;
  }

#ifdef ANDROID
  // Android is different; get the FD via gPrefsFd instead of a fixed fd.
  MOZ_RELEASE_ASSERT(gPrefsFd != -1);
  mPrefsHandle = Some(base::FileDescriptor(gPrefsFd, /* auto_close */ true));

  mPrefMapHandle.emplace(UniqueFileHandle(gPrefMapFd));
#elif XP_UNIX
  mPrefsHandle = Some(base::FileDescriptor(kPrefsFileDescriptor,
                                           /* auto_close */ true));

  mPrefMapHandle.emplace(UniqueFileHandle(kPrefMapFileDescriptor));
#endif

  if (mPrefsHandle.isNothing() || mPrefsLen.isNothing() ||
      mPrefMapHandle.isNothing() || mPrefMapSize.isNothing()) {
    return false;
  }

  // Init the shared-memory base preference mapping first, so that only changed
  // preferences wind up in heap memory.
  Preferences::InitSnapshot(mPrefMapHandle.ref(), *mPrefMapSize);

  // Set up early prefs from the shared memory.
  if (!mShmem.SetHandle(*mPrefsHandle, /* read_only */ true)) {
    NS_ERROR("failed to open shared memory in the child");
    return false;
  }
  if (!mShmem.Map(*mPrefsLen)) {
    NS_ERROR("failed to map shared memory in the child");
    return false;
  }
  Preferences::DeserializePreferences(static_cast<char*>(mShmem.memory()),
                                      *mPrefsLen);

  return true;
}

const base::SharedMemoryHandle& SharedPreferenceDeserializer::GetPrefsHandle()
    const {
  MOZ_ASSERT(mPrefsHandle.isSome());

  return mPrefsHandle.ref();
}

const FileDescriptor& SharedPreferenceDeserializer::GetPrefMapHandle() const {
  MOZ_ASSERT(mPrefMapHandle.isSome());

  return mPrefMapHandle.ref();
}

}  // namespace ipc
}  // namespace mozilla
