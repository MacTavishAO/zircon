// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>

char *strcat(char *dest, char const *src) {
  char *tmp = dest;

  while (*dest)
    dest++;
  while ((*dest++ = *src++) != '\0')
    ;

  return tmp;
}
