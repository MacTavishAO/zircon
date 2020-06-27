// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <ktl/algorithm.h>
#include <ktl/string_view.h>

#define LONGFLAG 0x00000001
#define LONGLONGFLAG 0x00000002
#define HALFFLAG 0x00000004
#define HALFHALFFLAG 0x00000008
#define SIZETFLAG 0x00000010
#define INTMAXFLAG 0x00000020
#define PTRDIFFFLAG 0x00000040
#define ALTFLAG 0x00000080
#define CAPSFLAG 0x00000100
#define SHOWSIGNFLAG 0x00000200
#define SIGNEDFLAG 0x00000400
#define LEFTFORMATFLAG 0x00000800
#define LEADZEROFLAG 0x00001000
#define BLANKPOSFLAG 0x00002000
#define FIELDWIDTHFLAG 0x00004000

namespace {

__NO_INLINE char *longlong_to_string(char *buf, unsigned long long n, size_t len, unsigned int flag,
                                     char *signchar) {
  size_t pos = len;
  int negative = 0;

  if ((flag & SIGNEDFLAG) && (long long)n < 0) {
    negative = 1;
    n = -n;
  }

  buf[--pos] = 0;

  /* only do the math if the number is >= 10 */
  while (n >= 10) {
    int digit = (int)(n % 10);

    n /= 10;

    buf[--pos] = (char)(digit + '0');
  }
  buf[--pos] = (char)(n + '0');

  if (negative)
    *signchar = '-';
  else if ((flag & SHOWSIGNFLAG))
    *signchar = '+';
  else if ((flag & BLANKPOSFLAG))
    *signchar = ' ';
  else
    *signchar = '\0';

  return &buf[pos];
}

constexpr char hextable[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
constexpr char hextable_caps[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                  '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

__NO_INLINE const char *longlong_to_hexstring(char *buf, unsigned long long u, size_t len,
                                              unsigned int flag) {
  size_t pos = len;
  const char *table = (flag & CAPSFLAG) ? hextable_caps : hextable;

  // Special case because ALTFLAG does not prepend 0x to 0.
  if (u == 0)
    return "0";

  buf[--pos] = 0;

  do {
    unsigned int digit = u % 16;
    u /= 16;

    buf[--pos] = table[digit];
  } while (u != 0);

  if (flag & ALTFLAG) {
    buf[--pos] = (flag & CAPSFLAG) ? 'X' : 'x';
    buf[--pos] = '0';
  }

  return &buf[pos];
}

}  // namespace

int vfprintf(FILE *out, const char *fmt, va_list ap) {
  int err = 0;
  char c;
  unsigned char uc;
  const char *s;
  size_t string_len;
  unsigned long long n;
  void *ptr;
  int flags;
  unsigned int format_num;
  char signchar;
  size_t chars_written = 0;
  char num_buffer[32];

#define OUTPUT_STRING(str, len)   \
  do {                            \
    err = out->Write({str, len}); \
    if (err < 0) {                \
      goto exit;                  \
    } else {                      \
      chars_written += err;       \
    }                             \
  } while (0)
#define OUTPUT_CHAR(c)                       \
  do {                                       \
    char __temp[1] = {static_cast<char>(c)}; \
    OUTPUT_STRING(__temp, 1);                \
  } while (0)

  for (;;) {
    /* reset the format state */
    flags = 0;
    format_num = 0;
    signchar = '\0';

    /* handle regular chars that aren't format related */
    s = fmt;
    string_len = 0;
    while ((c = *fmt++) != 0) {
      if (c == '%')
        break; /* we saw a '%', break and start parsing format */
      string_len++;
    }

    /* output the string we've accumulated */
    OUTPUT_STRING(s, string_len);

    /* make sure we haven't just hit the end of the string */
    if (c == 0)
      break;

  next_format:
    /* grab the next format character */
    c = *fmt++;
    if (c == 0)
      break;

    switch (c) {
      case '0' ... '9':
        if (c == '0' && format_num == 0)
          flags |= LEADZEROFLAG;
        format_num *= 10;
        format_num += c - '0';
        goto next_format;
      case '*': {
        int width = va_arg(ap, int);
        if (width < 0) {
          flags |= LEFTFORMATFLAG;
          width = -width;
        }
        format_num = width;
        goto next_format;
      }
      case '.':
        // Check the next character. It either should be * (if valid)
        // or something else (if invalid) that we consume as invalid.
        c = *fmt;
        if (c == '*') {
          fmt++;
          flags |= FIELDWIDTHFLAG;
          format_num = va_arg(ap, int);
        } else if (c == 's') {
          // %.s is invalid, and testing glibc printf it
          // results in no output so force skipping the 's'
          fmt++;
        }
        goto next_format;
      case '%':
        OUTPUT_CHAR('%');
        break;
      case 'c':
        uc = (unsigned char)va_arg(ap, unsigned int);
        OUTPUT_CHAR(uc);
        break;
      case 's':
        s = va_arg(ap, const char *);
        if (s == 0)
          s = "<null>";
        flags &= ~LEADZEROFLAG; /* doesn't make sense for strings */
        goto _output_string;
      case 'V': {
        // string_view is "POD enough".
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-pod-varargs"
#endif
        ktl::string_view sv = va_arg(ap, ktl::string_view);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
        s = sv.data();
        string_len = sv.size();
        if (flags & FIELDWIDTHFLAG) {
          string_len = ktl::min(string_len, static_cast<size_t>(format_num));
        }
        flags &= ~LEADZEROFLAG;  // Doesn't make sense for strings.
        goto _output_string_with_len;
      }
      case '-':
        flags |= LEFTFORMATFLAG;
        goto next_format;
      case '+':
        flags |= SHOWSIGNFLAG;
        goto next_format;
      case ' ':
        flags |= BLANKPOSFLAG;
        goto next_format;
      case '#':
        flags |= ALTFLAG;
        goto next_format;
      case 'l':
        if (flags & LONGFLAG)
          flags |= LONGLONGFLAG;
        flags |= LONGFLAG;
        goto next_format;
      case 'h':
        if (flags & HALFFLAG)
          flags |= HALFHALFFLAG;
        flags |= HALFFLAG;
        goto next_format;
      case 'z':
        flags |= SIZETFLAG;
        goto next_format;
      case 'j':
        flags |= INTMAXFLAG;
        goto next_format;
      case 't':
        flags |= PTRDIFFFLAG;
        goto next_format;
      case 'i':
      case 'd':
        // clang-format off
          n = (flags & LONGLONGFLAG) ? va_arg(ap, long long) :
              (flags & LONGFLAG) ? va_arg(ap, long) :
              (flags & HALFHALFFLAG) ? (signed char)va_arg(ap, int) :
              (flags & HALFFLAG) ? (short)va_arg(ap, int) :
              (flags & SIZETFLAG) ? va_arg(ap, intptr_t) :
              (flags & INTMAXFLAG) ? va_arg(ap, intmax_t) :
              (flags & PTRDIFFFLAG) ? va_arg(ap, ptrdiff_t) :
              va_arg(ap, int);
          flags |= SIGNEDFLAG;
          s = longlong_to_string(num_buffer, n, sizeof(num_buffer), flags, &signchar);
          goto _output_string;
        // clang-format on
      case 'u':
        // clang-format off
        n = (flags & LONGLONGFLAG) ? va_arg(ap, unsigned long long) :
            (flags & LONGFLAG) ? va_arg(ap, unsigned long) :
            (flags & HALFHALFFLAG) ? (unsigned char)va_arg(ap, unsigned int) :
            (flags & HALFFLAG) ? (unsigned short)va_arg(ap, unsigned int) :
            (flags & SIZETFLAG) ? va_arg(ap, size_t) :
            (flags & INTMAXFLAG) ? va_arg(ap, uintmax_t) :
            (flags & PTRDIFFFLAG) ? (uintptr_t)va_arg(ap, ptrdiff_t) :
            va_arg(ap, unsigned int);
        s = longlong_to_string(num_buffer, n, sizeof(num_buffer), flags, &signchar);
        goto _output_string;
        // clang-format on
      case 'p':
        flags |= LONGFLAG | ALTFLAG;
        goto hex;
      case 'X':
        flags |= CAPSFLAG;
        /* fallthrough */
      hex:
      case 'x':
        // clang-format off
        n = (flags & LONGLONGFLAG) ? va_arg(ap, unsigned long long) :
            (flags & LONGFLAG) ? va_arg(ap, unsigned long) :
            (flags & HALFHALFFLAG) ? (unsigned char)va_arg(ap, unsigned int) :
            (flags & HALFFLAG) ? (unsigned short)va_arg(ap, unsigned int) :
            (flags & SIZETFLAG) ? va_arg(ap, size_t) :
            (flags & INTMAXFLAG) ? va_arg(ap, uintmax_t) :
            (flags & PTRDIFFFLAG) ? (uintptr_t)va_arg(ap, ptrdiff_t) :
            va_arg(ap, unsigned int);
        s = longlong_to_hexstring(num_buffer, n, sizeof(num_buffer), flags);
        goto _output_string;
        // clang-format on
      case 'n':
        ptr = va_arg(ap, void *);
        if (flags & LONGLONGFLAG)
          *(long long *)ptr = chars_written;
        else if (flags & LONGFLAG)
          *(long *)ptr = chars_written;
        else if (flags & HALFHALFFLAG)
          *(signed char *)ptr = (signed char)chars_written;
        else if (flags & HALFFLAG)
          *(short *)ptr = (short)chars_written;
        else if (flags & SIZETFLAG)
          *(size_t *)ptr = chars_written;
        else
          *(int *)ptr = (int)chars_written;
        break;
      default:
        OUTPUT_CHAR('%');
        OUTPUT_CHAR(c);
        break;
    }

    /* move on to the next field */
    continue;

    /* shared output code */
  _output_string:
    if (flags & FIELDWIDTHFLAG) {
      // Don't look past the specified length; the string can be unterminated.
      string_len = strnlen(s, static_cast<size_t>(format_num));
    } else {
      string_len = strlen(s);
    }
  _output_string_with_len:

    if (flags & LEFTFORMATFLAG) {
      /* left justify the text */
      OUTPUT_STRING(s, string_len);
      unsigned int written = err;

      /* pad to the right (if necessary) */
      for (; format_num > written; format_num--)
        OUTPUT_CHAR(' ');
    } else {
      /* right justify the text (digits) */

      /* if we're going to print a sign digit,
         it'll chew up one byte of the format size */
      if (signchar != '\0' && format_num > 0)
        format_num--;

      /* output the sign char before the leading zeros */
      if (flags & LEADZEROFLAG && signchar != '\0')
        OUTPUT_CHAR(signchar);

      /* pad according to the format string */
      for (; format_num > string_len; format_num--)
        OUTPUT_CHAR(flags & LEADZEROFLAG ? '0' : ' ');

      /* if not leading zeros, output the sign char just before the number */
      if (!(flags & LEADZEROFLAG) && signchar != '\0')
        OUTPUT_CHAR(signchar);

      /* output the string */
      OUTPUT_STRING(s, string_len);
    }
    continue;
  }

#undef OUTPUT_STRING
#undef OUTPUT_CHAR

exit:
  return (err < 0) ? err : (int)chars_written;
}

int fprintf(FILE *f, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = vfprintf(f, fmt, args);
  va_end(args);
  return result;
}
