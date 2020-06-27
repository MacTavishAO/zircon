// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <libc/ctype.h>
#include <libc/string.h>

void *memscan(void *addr, int c, size_t size) {
  unsigned char *p = (unsigned char *)addr;

  while (size) {
    if (*p == c)
      return (void *)p;
    p++;
    size--;
  }
  return (void *)p;
}
