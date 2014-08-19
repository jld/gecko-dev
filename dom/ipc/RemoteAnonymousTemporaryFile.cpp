/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteAnonymousTemporaryFile.h"

#include "mozilla/dom/ContentChild.h"
#include "nsIUUIDGenerator.h"
#include "private/pprio.h"

#ifdef XP_WIN
#include <windows.h>
#else
#include <unistd.h>
#include "base/eintr_wrapper.h"
#endif

namespace mozilla {
namespace dom {

NS_IMPL_ISUPPORTS(RemoteAnonymousTemporaryFile,
		  nsIFile)

nsresult
RemoteAnonymousTemporaryFile::Init(ContentChild* aChild)
{
    mozilla::ipc::FileDescriptor receivedFD;

    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(!mInited);

    nsresult rv = NS_ERROR_FAILURE;
    nsCOMPtr<nsIUUIDGenerator> uuidgen =
        do_GetService("@mozilla.org/uuid-generator;1", &rv);
    if (NS_WARN_IF(!uuidgen)) {
        return rv;
    }
    rv = uuidgen->GenerateUUIDInPlace(&mUUID);
    NS_ENSURE_SUCCESS(rv, rv);

    bool opened = aChild->SendOpenAnonymousTemporaryFile(&receivedFD);
    if (NS_WARN_IF(!opened)) {
        return NS_ERROR_FAILURE;
    }
    mHandle = receivedFD.PlatformHandle();
    mInited = true;
    return NS_OK;
}

RemoteAnonymousTemporaryFile::~RemoteAnonymousTemporaryFile()
{
    if (mInited) {
        // Copied from FileDescriptor.cpp:
#ifdef XP_WIN
        if (!CloseHandle(mHandle)) {
            NS_WARNING("Failed to close file handle!");
        }
#else // XP_WIN
        HANDLE_EINTR(close(mHandle));
#endif
    }
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::OpenNSPRFileDesc(int32_t aFlags, int32_t aMode,
                                               PRFileDesc** aRetval)
{
    mozilla::ipc::FileDescriptor::PlatformHandleType duplicate;
    bool success;
    MOZ_ASSERT(mInited);

    // Copied from FileDescriptor.cpp
#ifdef XP_WIN
    success = DuplicateHandle(GetCurrentProcess(), mHandle, GetCurrentProcess(),
                              &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS);
#else
    duplicate = dup(mHandle);
    success = duplicate >= 0;
#endif

    if (NS_WARN_IF(!success)) {
        return NS_ERROR_FAILURE;
    }
    *aRetval = PR_ImportFile(PROsfd(duplicate));
    return NS_OK;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetNativePath(nsACString& aRetval)
{
    char buf[NSID_LENGTH];

    MOZ_ASSERT(mInited);
    mUUID.ToProvidedString(buf);
    aRetval = buf;
    return NS_OK;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::Clone(nsIFile** file)
{
    MOZ_ASSERT(mInited);
    // ...this class is threadsafe, right?
    NS_ADDREF((*file = this));
    return NS_OK;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::Equals(nsIFile* aOther , bool* aRetval)
{
    *aRetval = aOther == this;
    return NS_OK;
}

// nsIFile methods that either aren't meaningful or aren't currently needed.

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::Append(const nsAString &node)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::AppendNative(const nsACString& fragment)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::Normalize()
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::Create(uint32_t type, uint32_t permissions)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
RemoteAnonymousTemporaryFile::SetLeafName(const nsAString& aLeafName)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::SetNativeLeafName(const nsACString& aLeafName)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
RemoteAnonymousTemporaryFile::InitWithPath(const nsAString& filePath)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::InitWithNativePath(const nsACString& filePath)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::InitWithFile(nsIFile* aFile)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::SetFollowLinks(bool aFollowLinks)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult  
RemoteAnonymousTemporaryFile::AppendRelativePath(const nsAString& node)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::AppendRelativeNativePath(const nsACString& fragment)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetPersistentDescriptor(nsACString& aPersistentDescriptor)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::SetPersistentDescriptor(const nsACString& aPersistentDescriptor)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetRelativeDescriptor(nsIFile* fromFile, nsACString& _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::SetRelativeDescriptor(nsIFile* fromFile,
                                                    const nsACString& relativeDesc)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
RemoteAnonymousTemporaryFile::CopyTo(nsIFile* newParentDir, const nsAString& newName)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::CopyToNative(nsIFile* newParent, const nsACString& newName)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
RemoteAnonymousTemporaryFile::CopyToFollowingLinks(nsIFile* newParentDir,
                                                   const nsAString& newName)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::CopyToFollowingLinksNative(nsIFile* newParent,
                                                         const nsACString& newName)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
RemoteAnonymousTemporaryFile::MoveTo(nsIFile* newParentDir, const nsAString& newName)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::MoveToNative(nsIFile* newParent, const nsACString& newName)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::RenameTo(nsIFile* newParentDir, const nsAString& newName)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::Remove(bool recursive)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetPermissions(uint32_t* aPermissions)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::SetPermissions(uint32_t aPermissions)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetPermissionsOfLink(uint32_t* aPermissionsOfLink)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::SetPermissionsOfLink(uint32_t aPermissions)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetLastModifiedTime(PRTime* aLastModTime)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::SetLastModifiedTime(PRTime aLastModTime)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetLastModifiedTimeOfLink(PRTime* aLastModTimeOfLink)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::SetLastModifiedTimeOfLink(PRTime aLastModTimeOfLink)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetFileSize(int64_t* aFileSize)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::SetFileSize(int64_t aFileSize)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetFileSizeOfLink(int64_t* aFileSize)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::Exists(bool* _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::IsWritable(bool* _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::IsReadable(bool* _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::IsExecutable(bool* _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::IsHidden(bool* _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::IsDirectory(bool* _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::IsFile(bool* _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::IsSymlink(bool* _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::IsSpecial(bool* _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::CreateUnique(uint32_t type, uint32_t attributes)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetDirectoryEntries(nsISimpleEnumerator** entries)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::OpenANSIFileDesc(const char* mode, FILE** _retval)
{
    // TODO: can implement using fdopen()?
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::Load(PRLibrary** _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetDiskSpaceAvailable(int64_t* aDiskSpaceAvailable)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::Reveal()
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::Launch()
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
RemoteAnonymousTemporaryFile::GetLeafName(nsAString& aLeafName)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetNativeLeafName(nsACString& aLeafName)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
RemoteAnonymousTemporaryFile::GetTarget(nsAString& _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetNativeTarget(nsACString& _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
RemoteAnonymousTemporaryFile::GetPath(nsAString& _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::Contains(nsIFile* inFile, bool* _retval)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetParent(nsIFile** aParent)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteAnonymousTemporaryFile::GetFollowLinks(bool* aFollowLinks)
{
    MOZ_ASSERT(false); return NS_ERROR_NOT_IMPLEMENTED;
}



} // namespace dom
} // namespace mozilla
