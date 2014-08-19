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

/* static */ already_AddRefed<nsIFile>
RemoteAnonymousTemporaryFile::CreateIfChild()
{
    ContentChild* child = ContentChild::GetSingleton();
    if (!child) {
        return nullptr;
    }
    nsRefPtr<RemoteAnonymousTemporaryFile> file =
        new RemoteAnonymousTemporaryFile();
    if (NS_WARN_IF(NS_FAILED(file->Init(child)))) {
        return nullptr;
    }
    return file.forget();
}

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
#else
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

    // Copied from FileDescriptor.cpp:
#ifdef XP_WIN
    success = DuplicateHandle(GetCurrentProcess(), mHandle, GetCurrentProcess(),
                              &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS);
#else
    duplicate = dup(mHandle);
    success = duplicate >= 0;
    if (success) {
        // The previous user of the file may have changed the offset.
        lseek(duplicate, 0, SEEK_SET);
    }
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
    // This class is intended to be thread-safe.
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

#define UNIMPLEMENT(decl) \
NS_IMETHODIMP \
RemoteAnonymousTemporaryFile::decl \
{ \
    MOZ_ASSERT(false); \
    return NS_ERROR_NOT_IMPLEMENTED; \
}

UNIMPLEMENT(Append(const nsAString &node))
UNIMPLEMENT(AppendNative(const nsACString& fragment))
UNIMPLEMENT(Normalize())
UNIMPLEMENT(Create(uint32_t type, uint32_t permissions))
UNIMPLEMENT(SetLeafName(const nsAString& aLeafName))
UNIMPLEMENT(SetNativeLeafName(const nsACString& aLeafName))
UNIMPLEMENT(InitWithPath(const nsAString& filePath))
UNIMPLEMENT(InitWithNativePath(const nsACString& filePath))
UNIMPLEMENT(InitWithFile(nsIFile* aFile))
UNIMPLEMENT(SetFollowLinks(bool aFollowLinks))
UNIMPLEMENT(AppendRelativePath(const nsAString& node))
UNIMPLEMENT(AppendRelativeNativePath(const nsACString& fragment))
UNIMPLEMENT(GetPersistentDescriptor(nsACString& aPersistentDescriptor))
UNIMPLEMENT(SetPersistentDescriptor(const nsACString& aPersistentDescriptor))
UNIMPLEMENT(GetRelativeDescriptor(nsIFile* fromFile, nsACString& _retval))
UNIMPLEMENT(SetRelativeDescriptor(nsIFile* fromFile, const nsACString& relativeDesc))
UNIMPLEMENT(CopyTo(nsIFile* newParentDir, const nsAString& newName))
UNIMPLEMENT(CopyToNative(nsIFile* newParent, const nsACString& newName))
UNIMPLEMENT(CopyToFollowingLinks(nsIFile* newParentDir, const nsAString& newName))
UNIMPLEMENT(CopyToFollowingLinksNative(nsIFile* newParent, const nsACString& newName))
UNIMPLEMENT(MoveTo(nsIFile* newParentDir, const nsAString& newName))
UNIMPLEMENT(MoveToNative(nsIFile* newParent, const nsACString& newName))
UNIMPLEMENT(RenameTo(nsIFile* newParentDir, const nsAString& newName))
UNIMPLEMENT(Remove(bool recursive))
UNIMPLEMENT(GetPermissions(uint32_t* aPermissions))
UNIMPLEMENT(SetPermissions(uint32_t aPermissions))
UNIMPLEMENT(GetPermissionsOfLink(uint32_t* aPermissionsOfLink))
UNIMPLEMENT(SetPermissionsOfLink(uint32_t aPermissions))
UNIMPLEMENT(GetLastModifiedTime(PRTime* aLastModTime))
UNIMPLEMENT(SetLastModifiedTime(PRTime aLastModTime))
UNIMPLEMENT(GetLastModifiedTimeOfLink(PRTime* aLastModTimeOfLink))
UNIMPLEMENT(SetLastModifiedTimeOfLink(PRTime aLastModTimeOfLink))
UNIMPLEMENT(GetFileSize(int64_t* aFileSize))
UNIMPLEMENT(SetFileSize(int64_t aFileSize))
UNIMPLEMENT(GetFileSizeOfLink(int64_t* aFileSize))
UNIMPLEMENT(Exists(bool* _retval))
UNIMPLEMENT(IsWritable(bool* _retval))
UNIMPLEMENT(IsReadable(bool* _retval))
UNIMPLEMENT(IsExecutable(bool* _retval))
UNIMPLEMENT(IsHidden(bool* _retval))
UNIMPLEMENT(IsDirectory(bool* _retval))
UNIMPLEMENT(IsFile(bool* _retval))
UNIMPLEMENT(IsSymlink(bool* _retval))
UNIMPLEMENT(IsSpecial(bool* _retval))
UNIMPLEMENT(CreateUnique(uint32_t type, uint32_t attributes))
UNIMPLEMENT(GetDirectoryEntries(nsISimpleEnumerator** entries))
UNIMPLEMENT(OpenANSIFileDesc(const char* mode, FILE** _retval))
UNIMPLEMENT(Load(PRLibrary** _retval))
UNIMPLEMENT(GetDiskSpaceAvailable(int64_t* aDiskSpaceAvailable))
UNIMPLEMENT(Reveal())
UNIMPLEMENT(Launch())
UNIMPLEMENT(GetLeafName(nsAString& aLeafName))
UNIMPLEMENT(GetNativeLeafName(nsACString& aLeafName))
UNIMPLEMENT(GetTarget(nsAString& _retval))
UNIMPLEMENT(GetNativeTarget(nsACString& _retval))
UNIMPLEMENT(GetPath(nsAString& _retval))
UNIMPLEMENT(Contains(nsIFile* inFile, bool* _retval))
UNIMPLEMENT(GetParent(nsIFile** aParent))
UNIMPLEMENT(GetFollowLinks(bool* aFollowLinks))

#undef UNIMPLEMENT

} // namespace dom
} // namespace mozilla
