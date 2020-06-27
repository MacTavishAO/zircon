#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "internal.h"

int wctob(wint_t c) {
  if (c < 128U)
    return c;
  if (MB_CUR_MAX == 1 && IS_CODEUNIT(c))
    return (unsigned char)c;
  return EOF;
}
