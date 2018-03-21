/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/shared_memory.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/logging.h"
#include "base/string_util.h"
#include "mozilla/Atomics.h"
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
#include "mozilla/Sandbox.h"
#endif

namespace base {

SharedMemory::SharedMemory()
    : mapped_file_(-1),
      memory_(NULL),
      read_only_(false),
      max_size_(0) {
}

SharedMemory::~SharedMemory() {
  Close();
}

bool SharedMemory::SetHandle(SharedMemoryHandle handle, bool read_only) {
  DCHECK(mapped_file_ == -1);

  mapped_file_ = handle.fd;
  read_only_ = read_only;
  return true;
}

// static
bool SharedMemory::IsHandleValid(const SharedMemoryHandle& handle) {
  return handle.fd >= 0;
}

// static
SharedMemoryHandle SharedMemory::NULLHandle() {
  return SharedMemoryHandle();
}

namespace {

// A class to handle auto-closing of FILE*'s.
class ScopedFILEClose {
 public:
  inline void operator()(FILE* x) const {
    if (x) {
      fclose(x);
    }
  }
};

static int CreateViaSandbox(size_t size) {
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
  int rv = mozilla::SandboxSharedMemoryCreate(size);
  if (rv < 0) {
    DCHECK_EQ(-rv, ENOTCONN);
    return -1;
  }
  return rv;
#else
  return -1;
#endif
}

#ifndef SHM_ANON
static std::string GenerateShmName() {
  static mozilla::Atomic<size_t> sCounter;
  // This is no longer Chromium code, but keep the "org.chromium"
  // prefix to not break anyone's AppArmor policies.  (FIXME: lightdm
  // doesn't do that; snap does but they'd probably take PRs.  Change?)
  // (Also mention that this doesn't have to be exactly unique.)
  return StringPrintf("/org.chromium.%d.%zu", (int)getpid(), sCounter++);
}
#endif // !SHM_ANON

} // anonymous namespace

bool SharedMemory::Create(size_t size) {
  read_only_ = false;

  DCHECK(size > 0);
  DCHECK(mapped_file_ == -1);

  // Check for sandboxing and attempt brokered file creation.
  mapped_file_ = CreateViaSandbox(size);
  if (mapped_file_ >= 0) {
    max_size_ = size;
    return true;
  }

#ifdef SHM_ANON
  const char* const name = SHM_ANON;
#else
  const auto nameString = GenerateShmName();
  const char* const name = nameString.c_str();
#endif

  do {
    mapped_file_ = shm_open(name, O_RDWR | O_CREAT | O_TRUNC | O_EXCL, 0600);
  } while (mapped_file_ < 0 && (errno == EEXIST || errno == EINTR));

  if (mapped_file_ >= 0) {
#ifndef SHM_ANON
    // Deleting the file prevents anyone else from mapping it in
    // (making it private), and prevents the need for cleanup (once
    // the last fd is closed, it is truly freed).
    if (shm_unlink(name) != 0) {
      DCHECK(false) << "shm_unlink failed: " << strerror(errno);
      Close();
      return false;
    }
#endif
  } else {
    // Handle possible race condition: CreateViaSandbox fails because
    // sandbox isn't started, then sandbox is started, then filesystem
    // access is attemped and fails.
    mapped_file_ = CreateViaSandbox(size);
    if (mapped_file_ >= 0) {
      max_size_ = size;
      return true;
    }
    return false;
  }

  // Set the file size.
  //
  // According to POSIX, (f)truncate can be used to extend files;
  // previously this required the XSI option but as of the 2008
  // edition it's required for everything.  (Linux documents that this
  // may fail on non-"native" filesystems like FAT, but /dev/shm
  // should always be tmpfs.)
  if (ftruncate(mapped_file_, size) != 0) {
    Close();
    return false;
  }

  max_size_ = size;
  return true;
}

bool SharedMemory::Map(size_t bytes) {
  if (mapped_file_ == -1)
    return false;

  memory_ = mmap(NULL, bytes, PROT_READ | (read_only_ ? 0 : PROT_WRITE),
                 MAP_SHARED, mapped_file_, 0);

  if (memory_)
    max_size_ = bytes;

  bool mmap_succeeded = (memory_ != (void*)-1);
  DCHECK(mmap_succeeded) << "Call to mmap failed, errno=" << errno;
  return mmap_succeeded;
}

bool SharedMemory::Unmap() {
  if (memory_ == NULL)
    return false;

  munmap(memory_, max_size_);
  memory_ = NULL;
  max_size_ = 0;
  return true;
}

bool SharedMemory::ShareToProcessCommon(ProcessId processId,
                                        SharedMemoryHandle *new_handle,
                                        bool close_self) {
  DCHECK(mapped_file_ >= 0);

  int new_fd;
  if (close_self) {
    new_fd = mapped_file_;
    mapped_file_ = -1;
    Close();
  } else {
    new_fd = dup(mapped_file_);
  }
  if (new_fd < 0) {
    return false;
  }

  new_handle->fd = new_fd;
  new_handle->auto_close = true;
  return true;
}

void SharedMemory::Close(bool unmap_view) {
  if (unmap_view) {
    Unmap();
  }

  if (mapped_file_ >= 0) {
    close(mapped_file_);
    mapped_file_ = -1;
  }
}

SharedMemoryHandle SharedMemory::handle() const {
  return FileDescriptor(mapped_file_, false);
}

}  // namespace base
