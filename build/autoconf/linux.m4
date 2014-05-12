dnl This Source Code Form is subject to the terms of the Mozilla Public
dnl License, v. 2.0. If a copy of the MPL was not distributed with this
dnl file, You can obtain one at http://mozilla.org/MPL/2.0/.

AC_DEFUN([MOZ_LINUX_PERF_EVENT],
[

MOZ_ARG_WITH_STRING(linux-headers,
[  --with-linux-headers=DIR
                          location where the Linux kernel headers can be found],
    linux_headers=$withval)

PERF_EVENT_CFLAGS=

if test "$linux_headers"; then
    PERF_EVENT_CFLAGS="-I$linux_headers"
fi

_SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $PERF_EVENT_CFLAGS"

dnl Performance measurement headers.
MOZ_CHECK_HEADER(linux/perf_event.h,
    [AC_CACHE_CHECK(for perf_event_open system call,ac_cv_perf_event_open,
        [AC_TRY_COMPILE([#include <asm/unistd.h>],[return sizeof(__NR_perf_event_open);],
            ac_cv_perf_event_open=yes,
            ac_cv_perf_event_open=no)])])

if test "$ac_cv_perf_event_open" = "yes"; then
    HAVE_LINUX_PERF_EVENT_H=1
else
    NR_perf_event_open=
    case "$target_cpu" in
    (arm)
        NR_perf_event_open=__NR_SYSCALL_BASE+364
        ;;
    (i?86)
        NR_perf_event_open=336
        ;;
    (x86_64)
        NR_perf_event_open=298
        ;;
    esac
    if test -n "$NR_perf_event_open"; then
        HAVE_LINUX_PERF_EVENT_H=1
        PERF_EVENT_CFLAGS="$PERF_EVENT_CFLAGS -D__NR_perf_event_open=$NR_perf_event_open"
    else
        HAVE_LINUX_PERF_EVENT_H=
        PERF_EVENT_CFLAGS=
    fi
    unset NR_perf_event_open
fi
AC_SUBST(HAVE_LINUX_PERF_EVENT_H)
AC_SUBST(PERF_EVENT_CFLAGS)

CFLAGS="$_SAVE_CFLAGS"

])
