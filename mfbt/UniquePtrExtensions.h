/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Useful extensions to UniquePtr. */

#ifndef mozilla_UniquePtrExtensions_h
#define mozilla_UniquePtrExtensions_h

#include "mozilla/fallible.h"
#include "mozilla/UniquePtr.h"

#ifdef XP_WIN
// Need the HANDLE typedef for UniqueFileHandle.
#include <winnt.h>
#include <cstdint>
#endif

namespace mozilla {

/**
 * MakeUniqueFallible works exactly like MakeUnique, except that the memory
 * allocation performed is done fallibly, i.e. it can return nullptr.
 */
template<typename T, typename... Args>
typename detail::UniqueSelector<T>::SingleObject
MakeUniqueFallible(Args&&... aArgs)
{
  return UniquePtr<T>(new (fallible) T(std::forward<Args>(aArgs)...));
}

template<typename T>
typename detail::UniqueSelector<T>::UnknownBound
MakeUniqueFallible(decltype(sizeof(int)) aN)
{
  typedef typename RemoveExtent<T>::Type ArrayType;
  return UniquePtr<T>(new (fallible) ArrayType[aN]());
}

template<typename T, typename... Args>
typename detail::UniqueSelector<T>::KnownBound
MakeUniqueFallible(Args&&... aArgs) = delete;

namespace detail {

template<typename T>
struct FreePolicy
{
  void operator()(const void* ptr) {
    free(const_cast<void*>(ptr));
  }
};

#if defined(XP_WIN)
typedef HANDLE FileHandleType;
#elif defined(XP_UNIX)
typedef int FileHandleType;
#else
#error "Unsupported OS?"
#endif

struct FileHandleHelper
{
  MOZ_IMPLICIT FileHandleHelper(FileHandleType aHandle)
  : mHandle(aHandle) { }

  MOZ_IMPLICIT constexpr FileHandleHelper(std::nullptr_t)
  : mHandle(kInvalidHandle) { }

  bool operator!=(std::nullptr_t) const {
    return mHandle != kInvalidHandle;
  }

  operator FileHandleType () const {
    return mHandle;
  }

#ifdef XP_WIN
  operator std::intptr_t () const {
    return reinterpret_cast<std::intptr_t>(mHandle);
  }
#endif

private:
  FileHandleType mHandle;

#ifdef XP_WIN
  static constexpr FileHandleType kInvalidHandle = INVALID_HANDLE_VALUE;
#else
  static constexpr FileHandleType kInvalidHandle = -1;
#endif
};

struct FileHandleDeleter
{
  typedef FileHandleHelper pointer;
  MFBT_API void operator () (FileHandleHelper aHelper);
};

} // namespace detail

template<typename T>
using UniqueFreePtr = UniquePtr<T, detail::FreePolicy<T>>;

using UniqueFileHandle =
  UniquePtr<detail::FileHandleType, detail::FileHandleDeleter>;

} // namespace mozilla

#endif // mozilla_UniquePtrExtensions_h
