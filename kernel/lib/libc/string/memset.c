// Copyright 2016 The Fuchsia Authors
// Copyright 2001, Travis Geiselbrecht
// Copyright 2005, Michael Noisternig
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdint.h>
#include <string.h>

__attribute__((no_sanitize_address)) void *__unsanitized_memset(void *s, int c, size_t count) {
  char *xs = (char *)s;
  size_t len = (-(size_t)s) & (sizeof(size_t) - 1);
  size_t cc = c & 0xff;

  if (count > len) {
    count -= len;
    cc |= cc << 8;
    cc |= cc << 16;
    if (sizeof(size_t) == 8)
      cc |= (uint64_t)cc << 32;  // should be optimized out on 32 bit machines

    // write to non-aligned memory byte-wise
    for (; len > 0; len--)
      *xs++ = c;

    // write to aligned memory dword-wise
    for (len = count / sizeof(size_t); len > 0; len--) {
      *((size_t *)xs) = (size_t)cc;
      xs += sizeof(size_t);
    }

    count &= sizeof(size_t) - 1;
  }

  // write remaining bytes
  for (; count > 0; count--)
    *xs++ = c;

  return s;
}

// Make the function a weak symbol so asan can override it.
__typeof(__unsanitized_memset) memset __attribute__((weak, alias("__unsanitized_memset")));
