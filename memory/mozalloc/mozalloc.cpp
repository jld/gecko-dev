/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <errno.h>
#include <new>                  // for std::bad_alloc
#include <string.h>

#include <sys/types.h>
#ifdef XP_LINUX
#include <sys/prctl.h>
#define PR_GET_VMA 0x47564d41
# define PR_GET_VMA_MAP_COUNTS 0
#include <stdio.h>
#endif

#if defined(MALLOC_H)
#  include MALLOC_H             // for memalign, valloc, malloc_size, malloc_usable_size
#endif // if defined(MALLOC_H)
#include <stddef.h>             // for size_t
#include <stdlib.h>             // for malloc, free
#if defined(XP_UNIX)
#  include <unistd.h>           // for valloc on *BSD
#endif //if defined(XP_UNIX)

#if defined(XP_WIN)
#  define MOZALLOC_EXPORT __declspec(dllexport)
#endif

#include "mozilla/mozalloc.h"
#include "mozilla/mozalloc_oom.h"  // for mozalloc_handle_oom

/* Windows doesn't have malloc_usable_size, but jemalloc has */
#if defined(MOZ_MEMORY_WINDOWS)
extern "C" size_t malloc_usable_size(const void *ptr);
#endif

#ifdef __GNUC__
#define LIKELY(x)    (__builtin_expect(!!(x), 1))
#define UNLIKELY(x)  (__builtin_expect(!!(x), 0))
#else
#define LIKELY(x)    (x)
#define UNLIKELY(x)  (x)
#endif

void
moz_free(void* ptr)
{
    free(ptr);
}

void*
moz_xmalloc(size_t size)
{
    void* ptr = malloc(size);
    if (UNLIKELY(!ptr && size)) {
        mozalloc_handle_oom(size);
        return moz_xmalloc(size);
    }
    return ptr;
}
void*
moz_malloc(size_t size)
{
    return malloc(size);
}

void*
moz_xcalloc(size_t nmemb, size_t size)
{
    void* ptr = calloc(nmemb, size);
    if (UNLIKELY(!ptr && nmemb && size)) {
        mozalloc_handle_oom(size);
        return moz_xcalloc(nmemb, size);
    }
    return ptr;
}
void*
moz_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void*
moz_xrealloc(void* ptr, size_t size)
{
    void* newptr = realloc(ptr, size);
    if (UNLIKELY(!newptr && size)) {
        mozalloc_handle_oom(size);
        return moz_xrealloc(ptr, size);
    }
    return newptr;
}
void*
moz_realloc(void* ptr, size_t size)
{
    return realloc(ptr, size);
}

char*
moz_xstrdup(const char* str)
{
    char* dup = strdup(str);
    if (UNLIKELY(!dup)) {
        mozalloc_handle_oom(0);
        return moz_xstrdup(str);
    }
    return dup;
}
char*
moz_strdup(const char* str)
{
    return strdup(str);
}

#if defined(HAVE_STRNDUP)
char*
moz_xstrndup(const char* str, size_t strsize)
{
    char* dup = strndup(str, strsize);
    if (UNLIKELY(!dup)) {
        mozalloc_handle_oom(strsize);
        return moz_xstrndup(str, strsize);
    }
    return dup;
}
char*
moz_strndup(const char* str, size_t strsize)
{
    return strndup(str, strsize);
}
#endif  // if defined(HAVE_STRNDUP)

#if defined(HAVE_POSIX_MEMALIGN)
int
moz_xposix_memalign(void **ptr, size_t alignment, size_t size)
{
    int err = posix_memalign(ptr, alignment, size);
    if (UNLIKELY(err && ENOMEM == err)) {
        mozalloc_handle_oom(size);
        return moz_xposix_memalign(ptr, alignment, size);
    }
    // else: (0 == err) or (EINVAL == err)
    return err;
}
int
moz_posix_memalign(void **ptr, size_t alignment, size_t size)
{
    int code = posix_memalign(ptr, alignment, size);
    if (code)
        return code;

#if defined(XP_MACOSX)
    // Workaround faulty OSX posix_memalign, which provides memory with the
    // incorrect alignment sometimes, but returns 0 as if nothing was wrong.
    size_t mask = alignment - 1;
    if (((size_t)(*ptr) & mask) != 0) {
        void* old = *ptr;
        code = moz_posix_memalign(ptr, alignment, size);
        free(old);
    }
#endif

    return code;

}
#endif // if defined(HAVE_POSIX_MEMALIGN)

#if defined(HAVE_MEMALIGN)
void*
moz_xmemalign(size_t boundary, size_t size)
{
    void* ptr = memalign(boundary, size);
    if (UNLIKELY(!ptr && EINVAL != errno)) {
        mozalloc_handle_oom(size);
        return moz_xmemalign(boundary, size);
    }
    // non-NULL ptr or errno == EINVAL
    return ptr;
}
void*
moz_memalign(size_t boundary, size_t size)
{
    return memalign(boundary, size);
}
#endif // if defined(HAVE_MEMALIGN)

#if defined(HAVE_VALLOC)
void*
moz_xvalloc(size_t size)
{
    void* ptr = valloc(size);
    if (UNLIKELY(!ptr)) {
        mozalloc_handle_oom(size);
        return moz_xvalloc(size);
    }
    return ptr;
}
void*
moz_valloc(size_t size)
{
    return valloc(size);
}
#endif // if defined(HAVE_VALLOC)

size_t
moz_malloc_usable_size(void *ptr)
{
    if (!ptr)
        return 0;

#if defined(XP_MACOSX)
    return malloc_size(ptr);
#elif defined(HAVE_MALLOC_USABLE_SIZE) || defined(MOZ_MEMORY)
    return malloc_usable_size(ptr);
#elif defined(XP_WIN)
    return _msize(ptr);
#else
    return 0;
#endif
}

namespace mozilla {

bool moz_malloc_measure_uss;
size_t moz_malloc_total_actual;
size_t moz_malloc_total_uss;

} // namepsace mozilla

size_t moz_malloc_size_of(const void *ptr)
{
    size_t actual_size = moz_malloc_usable_size((void *)ptr);
    if (!mozilla::moz_malloc_measure_uss) {
        return actual_size;
    }
#ifndef XP_LINUX
    return actual_size;
#else
    static uintptr_t page_size, page_mask;
    uintptr_t addr, end, addr_rounded, pages;

    if (page_size == 0) {
        page_size = sysconf(_SC_PAGE_SIZE);
        page_mask = ~(page_size - 1);
    }

    addr = reinterpret_cast<uintptr_t>(ptr);
    end = addr + actual_size;
    addr_rounded = addr & page_mask;
    pages = (end - addr_rounded + page_size - 1) / page_size;

    int32_t mapcount[pages]; // FIXME: don't do this if pages is large
    if (0 != prctl(PR_GET_VMA, PR_GET_VMA_MAP_COUNTS, 
                   addr_rounded, end - addr_rounded,
                   (unsigned long)&mapcount)) {
        perror("PR_GET_VMA_MAP_COUNTS");
        return actual_size;
    }
    size_t uss = 0;
    for (size_t i = 0; i < pages; ++i) {
        uintptr_t page_start, page_end;
        page_start = addr_rounded + i * page_size;
        page_end = addr_rounded + (i + 1) * page_size;
        if (page_start < addr) {
            page_start = addr;
        }
        if (page_end > end) {
            page_end = end;
        }
        if (mapcount[i] == 1) {
            uss += page_end - page_start;
        }
    }
    mozilla::moz_malloc_total_actual += actual_size;
    mozilla::moz_malloc_total_uss += uss;
    return uss;
#endif
}

namespace mozilla {

const fallible_t fallible = fallible_t();

} // namespace mozilla
