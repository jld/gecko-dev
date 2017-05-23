/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Attributes.h"
#include "mozilla/ArrayUtils.h"

#include <stdint.h>
#include <string.h>
#include <link.h>

static const char *kBlockList[] = {
  "libesets_pac.so",
};

MOZ_EXPORT unsigned
la_version(unsigned version)
{
  return 1;
}

MOZ_EXPORT char *
la_objsearch(const char *name, uintptr_t *cookie, unsigned flag)
{
  const char *last_slash, *basename;
  size_t i;

  last_slash = strrchr(name, '/');
  if (last_slash == NULL) {
    basename = name;
  } else {
    basename = last_slash + 1;
  }

  for (i = 0; i < MOZ_ARRAY_LENGTH(kBlockList); ++i) {
    if (strcmp(basename, kBlockList[i]) == 0) {
      return NULL;
    }
  }

  return (char*)name;
}
