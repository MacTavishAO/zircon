// Copyright 2016 The Fuchsia Authors
// Copyright 2001, Manuel J. Petit.
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>

void *memchr(void const *buf, int c, size_t len) {
  size_t i;
  unsigned char const *b = buf;
  unsigned char x = (c & 0xff);

  for (i = 0; i < len; i++) {
    if (b[i] == x) {
      return (void *)(b + i);
    }
  }

  return NULL;
}
