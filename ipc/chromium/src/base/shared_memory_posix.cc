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
#include <sys/syscall.h>
#include <unistd.h>

#ifdef ANDROID
#  include <linux/ashmem.h>
#endif

#ifdef __FreeBSD__
#  include <sys/capsicum.h>
#endif

#include "base/eintr_wrapper.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "mozilla/Atomics.h"
#include "mozilla/UniquePtrExtensions.h"
#include "prenv.h"

namespace base {

SharedMemory::SharedMemory()
    : memory_(nullptr),
      max_size_(0),
      mapped_file_(-1),
      frozen_file_(-1),
      mapped_size_(0),
      is_memfd_(false),
      read_only_(false),
      freeze_cap_(FreezeCap::NONE) {}

SharedMemory::SharedMemory(SharedMemory&& other) {
  memory_ = other.memory_;
  max_size_ = other.max_size_;
  mapped_file_ = other.mapped_file_;
  frozen_file_ = other.frozen_file_;
  mapped_size_ = other.mapped_size_;
  is_memfd_ = other.is_memfd_;
  read_only_ = other.read_only_;
  freeze_cap_ = other.freeze_cap_;

  other.mapped_file_ = -1;
  other.frozen_file_ = -1;
  other.mapped_size_ = 0;
  other.memory_ = nullptr;
}

SharedMemory::~SharedMemory() { Close(); }

bool SharedMemory::SetHandle(SharedMemoryHandle handle, bool read_only) {
  DCHECK(mapped_file_ == -1);
  DCHECK(frozen_file_ == -1);

  freeze_cap_ = FreezeCap::NONE;
  mapped_file_ = handle.fd;
  read_only_ = read_only;
  // is_memfd_ only matters for freezing, which isn't possible
  return true;
}

// static
bool SharedMemory::IsHandleValid(const SharedMemoryHandle& handle) {
  return handle.fd >= 0;
}

bool SharedMemory::IsValid() const { return mapped_file_ >= 0; }

// static
SharedMemoryHandle SharedMemory::NULLHandle() { return SharedMemoryHandle(); }

// Workaround for CVE-2018-4435 (https://crbug.com/project-zero/1671);
// can be removed when minimum OS version is at least 10.12.
#ifdef OS_MACOSX
static const char* GetTmpDir() {
  static const char* const kTmpDir = [] {
    const char* tmpdir = PR_GetEnv("TMPDIR");
    if (tmpdir) {
      return tmpdir;
    }
    return "/tmp";
  }();
  return kTmpDir;
}

static int FakeShmOpen(const char* name, int oflag, int mode) {
  CHECK(name[0] == '/');
  std::string path(GetTmpDir());
  path += name;
  return open(path.c_str(), oflag | O_CLOEXEC | O_NOCTTY, mode);
}

static int FakeShmUnlink(const char* name) {
  CHECK(name[0] == '/');
  std::string path(GetTmpDir());
  path += name;
  return unlink(path.c_str());
}

static bool IsShmOpenSecure() {
  static const bool kIsSecure = [] {
    mozilla::UniqueFileHandle rwfd, rofd;
    std::string name;
    CHECK(SharedMemory::AppendPosixShmPrefix(&name, getpid()));
    name += "sectest";
    // The prefix includes the pid and this will be called at most
    // once per process, so no need for a counter.
    rwfd.reset(
        HANDLE_EINTR(shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600)));
    // An adversary could steal the name.  Handle this semi-gracefully.
    DCHECK(rwfd);
    if (!rwfd) {
      return false;
    }
    rofd.reset(shm_open(name.c_str(), O_RDONLY, 0400));
    CHECK(rofd);
    CHECK(shm_unlink(name.c_str()) == 0);
    CHECK(ftruncate(rwfd.get(), 1) == 0);
    rwfd = nullptr;
    void* map = mmap(nullptr, 1, PROT_READ, MAP_SHARED, rofd.get(), 0);
    CHECK(map != MAP_FAILED);
    bool ok = mprotect(map, 1, PROT_READ | PROT_WRITE) != 0;
    munmap(map, 1);
    return ok;
  }();
  return kIsSecure;
}

static int SafeShmOpen(bool freezeable, const char* name, int oflag, int mode) {
  if (!freezeable || IsShmOpenSecure()) {
    return shm_open(name, oflag, mode);
  } else {
    return FakeShmOpen(name, oflag, mode);
  }
}

static int SafeShmUnlink(bool freezeable, const char* name) {
  if (!freezeable || IsShmOpenSecure()) {
    return shm_unlink(name);
  } else {
    return FakeShmUnlink(name);
  }
}

#elif !defined(ANDROID)
static int SafeShmOpen(bool freezeable, const char* name, int oflag, int mode) {
  return shm_open(name, oflag, mode);
}

static int SafeShmUnlink(bool freezeable, const char* name) {
  return shm_unlink(name);
}
#endif

// memfd_create is an interface for creating anonymous shared memory
// accessible as a file descriptor but not tied to any filesystem,
// introduced in Linux 3.17 and also implemented by FreeBSD as of the
// upcoming 13.0 release.
//
// FIXME write about seals and freezing but make it not false

#if defined(OS_LINUX)
#  if !defined(__GLIBC__) || __GLIBC__ < 2 || \
      (__GLIBC__ == 2 && __GLIBC_MINOR__ < 27)

// glibc before 2.27 didn't have a memfd_create wrapper, and if the
// build system is old enough then it won't have the syscall number
// and various related constants either.

#    if defined(__x86_64__)
#      define MEMFD_CREATE_NR 319
#    elif defined(__i386__)
#      define MEMFD_CREATE_NR 356
#    elif defined(__aarch64__)
#      define MEMFD_CREATE_NR 385
#    elif defined(__arm__)
#      define MEMFD_CREATE_NR 385
#    elif defined(__powerpc__)
#      define MEMFD_CREATE_NR 360
#    elif defined(__s390__)
#      define MEMFD_CREATE_NR 350
#    elif defined(__mips__)
#      include <sgidefs.h>
#      if _MIPS_SIM == _MIPS_SIM_ABI32
#        define MEMFD_CREATE_NR 4354
#      elif _MIPS_SIM == _MIPS_SIM_ABI64
#        define MEMFD_CREATE_NR 5314
#      elif _MIPS_SIM == _MIPS_SIM_NABI32
#        define MEMFD_CREATE_NR 6318
#      endif  // mips subarch
#    endif    // arch

#    ifdef MEMFD_CREATE_NR
#      ifdef SYS_memfd_create
static_assert(MEMFD_CREATE_NR == SYS_memfd_create,
              "MEMFD_CREATE_NR should match the actual SYS_memfd_create value");
#      else  // defined here but not in system headers
#        define SYS_memfd_create MEMFD_CREATE_NR
#      endif
#    endif

#    ifdef SYS_memfd_create
#      define HAVE_MEMFD_CREATE

static int memfd_create(const char* name, unsigned int flags) {
  return syscall(SYS_memfd_create, name, flags);
}

#      ifndef MFD_CLOEXEC
#        define MFD_CLOEXEC 0x0001U
#        define MFD_ALLOW_SEALING 0x0002U
#      endif

#      ifndef F_ADD_SEALS
#        define F_ADD_SEALS (F_LINUX_SPECIFIC_BASE + 9)
#        define F_GET_SEALS (F_LINUX_SPECIFIC_BASE + 10)
#        define F_SEAL_SEAL 0x0001   /* prevent further seals from being set */
#        define F_SEAL_SHRINK 0x0002 /* prevent file from shrinking */
#        define F_SEAL_GROW 0x0004   /* prevent file from growing */
#        define F_SEAL_WRITE 0x0008  /* prevent writes */
#      endif

#    endif  // have SYS_memfd_create

#  else  // Linux, new glibc; has memfd_create stub
#    define HAVE_MEMFD_CREATE
#  endif  // glibc version

static int DupReadOnly(int fd) {
  std::string path = StringPrintf("/proc/self/fd/%d", fd);
  return open(path.c_str(), O_RDONLY | O_CLOEXEC);
}

#elif defined(__FreeBSD__) && __FreeBSD_version >= 1300048

#  define HAVE_MEMFD_CREATE

static int DupReadOnly(int fd) {
  int rofd = dup(fd);
  if (rofd < 0) {
    return -1;
  }

  cap_rights_t rights;
  cap_rights_init(&rights, CAP_FSTAT, CAP_MMAP_R);
  if (cap_rights_limit(rofd, &rights) < 0) {
    close(rofd);
    return -1;
  }

  return ro;
}

#endif  // OS stuff

static bool HaveMemfd() {
#ifdef HAVE_MEMFD_CREATE
  static const bool kHave = [] {
    // FIXME cite the Tor Browser thing
    if (access("/proc/self/fd", R_OK | X_OK) < 0) {
      return false;
    }
    int fd = memfd_create("mozilla-ipc-test", MFD_CLOEXEC);
    if (fd < 0) {
      DCHECK(errno == ENOSYS);
      return false;
    }
    close(fd);
    return true;
  }();
  return kHave;
#else
  return false;
#endif  // HAVE_MEMFD_CREATE
}

// static
bool SharedMemory::AppendPosixShmPrefix(std::string* str, pid_t pid) {
#if defined(ANDROID)
  return false;
#else
  if (HaveMemfd()) {
    return false;
  }
  *str += '/';
#  ifdef OS_LINUX
  // The Snap package environment doesn't provide a private /dev/shm
  // (it's used for communication with services like PulseAudio);
  // instead AppArmor is used to restrict access to it.  Anything with
  // this prefix is allowed:
  static const char* const kSnap = [] {
    auto instanceName = PR_GetEnv("SNAP_INSTANCE_NAME");
    if (instanceName != nullptr) {
      return instanceName;
    }
    // Compatibility for snapd <= 2.35:
    return PR_GetEnv("SNAP_NAME");
  }();

  if (kSnap) {
    StringAppendF(str, "snap.%s.", kSnap);
  }
#  endif  // OS_LINUX
  // Hopefully the "implementation defined" name length limit is long
  // enough for this.
  StringAppendF(str, "org.mozilla.ipc.%d.", static_cast<int>(pid));
  return true;
#endif    // !ANDROID
}

bool SharedMemory::CreateInternal(size_t size, FreezeCap freeze_cap) {
  bool freezeable = freeze_cap != FreezeCap::NONE;
  read_only_ = false;

  DCHECK(size > 0);
  DCHECK(mapped_file_ == -1);
  DCHECK(frozen_file_ == -1);

  mozilla::UniqueFileHandle fd;
  mozilla::UniqueFileHandle frozen_fd;
  bool needs_truncate = true;
  bool is_memfd = false;

#ifdef HAVE_MEMFD_CREATE
  if (HaveMemfd()) {
    fd.reset(memfd_create("mozilla-ipc", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    if (!fd) {
      // In general it's too late to fall back here -- in a sandboxed
      // child process, shm_open is already blocked.  And it shouldn't
      // be necessary.
      CHROMIUM_LOG(WARNING) << "failed to create memfd: " << strerror(errno);
      return false;
    }
    is_memfd = true;
    if (freezeable) {
      frozen_fd.reset(DupReadOnly(fd.get()));
      if (!frozen_fd) {
        CHROMIUM_LOG(WARNING)
            << "failed to create read-only memfd: " << strerror(errno);
        return false;
      }
    }
  }
#endif

  if (!fd) {
#ifdef ANDROID
    // ashmem doesn't support this
    if (freeze_cap == FreezeCap::RO_COPY) {
      errno = ENOSYS;
      return false;
    }
    // Android has its own shared memory facility:
    fd.reset(open("/" ASHMEM_NAME_DEF, O_RDWR, 0600));
    if (!fd) {
      CHROMIUM_LOG(WARNING) << "failed to open shm: " << strerror(errno);
      return false;
    }
    if (ioctl(fd.get(), ASHMEM_SET_SIZE, size) != 0) {
      CHROMIUM_LOG(WARNING) << "failed to set shm size: " << strerror(errno);
      return false;
    }
    needs_truncate = false;
#else
    // Generic Unix: shm_open + shm_unlink
    do {
      // The names don't need to be unique, but it saves time if they
      // usually are.
      static mozilla::Atomic<size_t> sNameCounter;
      std::string name;
      CHECK(AppendPosixShmPrefix(&name, getpid()));
      StringAppendF(&name, "%zu", sNameCounter++);
      // O_EXCL means the names being predictable shouldn't be a problem.
      fd.reset(HANDLE_EINTR(SafeShmOpen(freezeable, name.c_str(),
                                        O_RDWR | O_CREAT | O_EXCL, 0600)));
      if (fd) {
        if (freezeable) {
          frozen_fd.reset(HANDLE_EINTR(
              SafeShmOpen(freezeable, name.c_str(), O_RDONLY, 0400)));
          if (!frozen_fd) {
            int open_err = errno;
            SafeShmUnlink(freezeable, name.c_str());
            DLOG(FATAL) << "failed to re-open freezeable shm: "
                        << strerror(open_err);
            return false;
          }
        }
        if (SafeShmUnlink(freezeable, name.c_str()) != 0) {
          // This shouldn't happen, but if it does: assume the file is
          // in fact leaked, and bail out now while it's still 0-length.
          DLOG(FATAL) << "failed to unlink shm: " << strerror(errno);
          return false;
        }
      }
    } while (!fd && errno == EEXIST);
#endif
  }

  if (!fd) {
    CHROMIUM_LOG(WARNING) << "failed to open shm: " << strerror(errno);
    return false;
  }

  if (needs_truncate) {
    if (HANDLE_EINTR(ftruncate(fd.get(), static_cast<off_t>(size))) != 0) {
      CHROMIUM_LOG(WARNING) << "failed to set shm size: " << strerror(errno);
      return false;
    }
  }

  mapped_file_ = fd.release();
  frozen_file_ = frozen_fd.release();
  max_size_ = size;
  freeze_cap_ = freeze_cap;
  is_memfd_ = is_memfd;
  return true;
}

bool SharedMemory::Freeze() {
  DCHECK(mapped_file_ >= 0);
  DCHECK(!read_only_);
  CHECK(freeze_cap_ != FreezeCap::NONE);
  Unmap();

  bool is_ashmem = false;

#ifdef ANDROID
  if (!is_memfd_) {
    is_ashmem = true;
    DCHECK(frozen_file_ < 0);
    if (ioctl(mapped_file_, ASHMEM_SET_PROT_MASK, PROT_READ) != 0) {
      CHROMIUM_LOG(WARNING) << "failed to freeze shm: " << strerror(errno);
      return false;
    }
  }
#endif

#ifdef HAVE_MEMFD_CREATE
  if (is_memfd_) {
    if (fcntl(mapped_file_, F_ADD_SEALS,
              F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) != 0) {
      CHROMIUM_LOG(WARNING) << "failed to seal memfd: " << strerror(errno);
      return false;
    }
  }
#else
  DCHECK(!is_memfd_);
#endif

  if (!is_ashmem) {
    DCHECK(frozen_file_ >= 0);
    close(mapped_file_);
    mapped_file_ = frozen_file_;
    frozen_file_ = -1;
  }

  read_only_ = true;
  freeze_cap_ = FreezeCap::NONE;
  return true;
}

bool SharedMemory::ReadOnlyCopy(SharedMemory* frozen_out) {
  DCHECK(!read_only_);
  CHECK(freeze_cap_ == FreezeCap::RO_COPY);

  DCHECK(frozen_file_ >= 0);
  FileDescriptor frozen(dup(frozen_file_), true);
  if (frozen.fd < 0) {
    return false;
  }

  return frozen_out->SetHandle(frozen, /* read_only = */ true);
}

bool SharedMemory::Map(size_t bytes, void* fixed_address) {
  if (mapped_file_ == -1) return false;
  DCHECK(!memory_);

  // Don't use MAP_FIXED when a fixed_address was specified, since that can
  // replace pages that are alread mapped at that address.
  void* mem =
      mmap(fixed_address, bytes, PROT_READ | (read_only_ ? 0 : PROT_WRITE),
           MAP_SHARED, mapped_file_, 0);

  if (mem == MAP_FAILED) {
    CHROMIUM_LOG(WARNING) << "Call to mmap failed: " << strerror(errno);
    return false;
  }

  if (fixed_address && mem != fixed_address) {
    bool munmap_succeeded = munmap(mem, bytes) == 0;
    DCHECK(munmap_succeeded) << "Call to munmap failed, errno=" << errno;
    return false;
  }

  memory_ = mem;
  mapped_size_ = bytes;
  return true;
}

bool SharedMemory::Unmap() {
  if (memory_ == NULL) return false;

  munmap(memory_, mapped_size_);
  memory_ = NULL;
  mapped_size_ = 0;
  return true;
}

void* SharedMemory::FindFreeAddressSpace(size_t size) {
  void* memory =
      mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  munmap(memory, size);
  return memory != MAP_FAILED ? memory : NULL;
}

bool SharedMemory::ShareToProcessCommon(ProcessId processId,
                                        SharedMemoryHandle* new_handle,
                                        bool close_self) {
  freeze_cap_ = FreezeCap::NONE;
  const int new_fd = dup(mapped_file_);
  DCHECK(new_fd >= -1);
  new_handle->fd = new_fd;
  new_handle->auto_close = true;

  if (close_self) Close();

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
  if (frozen_file_ >= 0) {
    if (freeze_cap_ != FreezeCap::RO_COPY) {
      CHROMIUM_LOG(WARNING) << "freezeable shared memory was never frozen";
    }
    close(frozen_file_);
    frozen_file_ = -1;
  }
}

mozilla::UniqueFileHandle SharedMemory::TakeHandle() {
  mozilla::UniqueFileHandle fh(mapped_file_);
  mapped_file_ = -1;
  // Now that the main fd is removed, reset everything else: close the
  // frozen fd if present and unmap the memory if mapped.
  Close();
  return fh;
}

}  // namespace base
