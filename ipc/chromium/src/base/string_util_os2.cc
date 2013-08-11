// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OS_OS2
#error This file is intended to be compiled only on OS/2!
#endif

#define STANDALONE
#define WIDECHAR

//
// The current version of kLIBC (0.6.5) lacks vswprintf() and friends.
//
// A patch to implement it was submitted at http://svn.netlabs.org/libc/ticket/272
// but it's not known when a new version will be reieased. Until then,
// this patch is contained here:
//

/* _output.c (emx+gcc) -- Copyright (c) 1990-2000 by Eberhard Mattes */
/* wide char changes by Dmitriy Kuminov */

#ifdef WIDECHAR
#define CHAR_T wchar_t
#define CHAR_L(str) L##str
#define NAME(id) id##_wchar
#define CMEMSET wmemset
#define CMEMMOVE wmemmove
#define CSTRLEN wcslen
#define CTOUPPER towupper
#define ULTOSTR uint32_to_wstr
#define ULLTOSTR uint64_to_wstr
#define REMOVE_ZEROES wremove_zeros
#define DTOSTR legacy_dtowstr
#else
#define CHAR_T char
#define CHAR_L(str) str
#define NAME(id) id
#define CMEMSET memset
#define CMEMMOVE memmove
#define CSTRLEN strlen
#define CTOUPPER toupper
#define ULTOSTR _ultoa
#define ULLTOSTR _ulltoa
#define REMOVE_ZEROES __remove_zeros
#define DTOSTR __legacy_dtoa
#endif

#define SIZEOF_ARRAY(a) (sizeof (a)/ sizeof ((a)[0]))

#ifndef STANDALONE
#include "libc-alias.h"
#else
#include <errno.h>
#include <sys/fmutex.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <emx/io.h>
#include <emx/float.h>
#include <InnoTekLIBC/locale.h>
#include <wchar.h>
#ifdef WIDECHAR
#include <wctype.h>
#else
#include <ctype.h>
#endif
#ifndef STANDALONE
#include "getputc.h"
#else
static inline int _putc_inline (int _c, FILE *_s)
{
  return (--_s->_wcount >= 0 && (_c != '\n' || !(_s->_flags & _IOLBF))
          ? (unsigned char)(*_s->_ptr++ = (char)_c)
          : _flush (_c, _s));
}
#endif

#ifdef WIDECHAR
static inline int NAME(_putc_inline) (CHAR_T _c, FILE *_s)
{
    return ((_s->_wcount -= sizeof(CHAR_T)) >= 0 && (_c != '\n' || !(_s->_flags & _IOLBF))
          ? (*((CHAR_T *)(_s->_ptr)) = (CHAR_T)_c, _s->_ptr += sizeof(CHAR_T), _c)
          // Note: the above will not work when sizeof(CHAR_T) > 2 but currenlty we don't care
          : (_flush (_c, _s), _flush (_c >> 8, _s), _c));
}
#endif

#define FALSE           0
#define TRUE            1

#define SIZE_HH         (('h' << 8) | 'h')
#define SIZE_LL         (('l' << 8) | 'l')

/* ASSUMES that there are no odd integer sizes (like char being 7 bit or similar weirdnesses). */

#define IS_SIZE_64BIT(size) (   (sizeof(long long) == sizeof(uint64_t) && ((size) == 'L' || (size) == SIZE_LL || (size) == 'q')) \
                             || (sizeof(size_t)    == sizeof(uint64_t) && ((size) == 'z' || (size) == 'z')) \
                             || (sizeof(uintmax_t) == sizeof(uint64_t) && (size) == 'j') \
                             || (sizeof(ptrdiff_t) == sizeof(uint64_t) && (size) == 't') \
                             || (sizeof(int)       == sizeof(uint64_t) && (size) == 0) \
                             || (sizeof(short)     == sizeof(uint64_t) && (size) == 'h') \
                             || (sizeof(char)      == sizeof(uint64_t) && (size) == SIZE_HH) \
                            )

#define IS_SIZE_32BIT(size) (   (sizeof(long long) == sizeof(uint32_t) && ((size) == 'L' || (size) == SIZE_LL || (size) == 'q')) \
                             || (sizeof(size_t)    == sizeof(uint32_t) && ((size) == 'z' || (size) == 'z')) \
                             || (sizeof(uintmax_t) == sizeof(uint32_t) && (size) == 'j') \
                             || (sizeof(ptrdiff_t) == sizeof(uint32_t) && (size) == 't') \
                             || (sizeof(int)       == sizeof(uint32_t) && (size) == 0) \
                             || (sizeof(short)     == sizeof(uint32_t) && (size) == 'h') \
                             || (sizeof(char)      == sizeof(uint32_t) && (size) == SIZE_HH) \
                            )

#define IS_SIZE_LE_32BIT(size) (   (sizeof(long long) <= sizeof(uint32_t) && ((size) == 'L' || (size) == SIZE_LL || (size) == 'q')) \
                                || (sizeof(size_t)    <= sizeof(uint32_t) && ((size) == 'z' || (size) == 'z')) \
                                || (sizeof(uintmax_t) <= sizeof(uint32_t) && (size) == 'j') \
                                || (sizeof(ptrdiff_t) <= sizeof(uint32_t) && (size) == 't') \
                                || (sizeof(int)       <= sizeof(uint32_t) && (size) == 0) \
                                || (sizeof(short)     <= sizeof(uint32_t) && (size) == 'h') \
                                || (sizeof(char)      <= sizeof(uint32_t) && (size) == SIZE_HH) \
                               )

#define IS_SIZE_16BIT(size) (   (sizeof(long long) == sizeof(uint16_t) && ((size) == 'L' || (size) == SIZE_LL || (size) == 'q')) \
                             || (sizeof(size_t)    == sizeof(uint16_t) && ((size) == 'z' || (size) == 'z')) \
                             || (sizeof(uintmax_t) == sizeof(uint16_t) && (size) == 'j') \
                             || (sizeof(ptrdiff_t) == sizeof(uint16_t) && (size) == 't') \
                             || (sizeof(int)       == sizeof(uint16_t) && (size) == 0) \
                             || (sizeof(short)     == sizeof(uint16_t) && (size) == 'h') \
                             || (sizeof(char)      == sizeof(uint16_t) && (size) == SIZE_HH) \
                            )

#define IS_SIZE_8BIT(size)  (   (sizeof(long long) == sizeof(uint8_t) && ((size) == 'L' || (size) == SIZE_LL || (size) == 'q')) \
                             || (sizeof(size_t)    == sizeof(uint8_t) && ((size) == 'z' || (size) == 'z')) \
                             || (sizeof(uintmax_t) == sizeof(uint8_t) && (size) == 'j') \
                             || (sizeof(ptrdiff_t) == sizeof(uint8_t) && (size) == 't') \
                             || (sizeof(int)       == sizeof(uint8_t) && (size) == 0) \
                             || (sizeof(short)     == sizeof(uint8_t) && (size) == 'h') \
                             || (sizeof(char)      == sizeof(uint8_t) && (size) == SIZE_HH) \
                            )


#define DEFAULT_PREC    6

#define BEGIN do {
#define END   } while (0)

/* All functions in this module return -1 if an error ocurred while
   writing the target file.  Otherwise, _output() returns the number
   of characters written, all other functions return 0.

   The CHECK macro is used to return -1 if the expression X evaluates
   to a non-zero value.  This is used to pass up errors. */

#define CHECK(X) BEGIN \
                   if ((X) != 0) \
                     return -1; \
                 END

/* Write the character C.  Note the return statement! */

#define PUTC(V,C) BEGIN \
                    if (NAME(_putc_inline) (C, (V)->stream) == EOF) \
                      return -1; \
                    ++(V)->count; \
                  END

#define SAFE_STRLEN(S) (((S) == NULL) ? 0 : CSTRLEN (S))

/* This structure holds the local variables of _output() which are
   passed to the various functions called by _output(). */

typedef struct
{
  FILE *stream;                 /* Where output should go */
  char minus;                   /* Non-zero if `-' flag present */
  char plus;                    /* Non-zero if `+' flag present */
  char blank;                   /* Non-zero if ` ' flag present */
  char hash;                    /* Non-zero if `#' flag present */
  char pad;                     /* Pad character (' ' or '0')  */
  int width;                    /* Field width (or 0 if none specified) */
  int prec;                     /* Precision (or -1 if none specified) */
  int count;                    /* Number of characters printed */
  int dig;                      /* DBL_DIG / LDBL_DIG for __small_dtoa() */
} olocal;

static CHAR_T zdot[] = CHAR_L ("0.");      /* "0." for the current locale */


static CHAR_T *str_upper (CHAR_T *s)
{
  CHAR_T *p = s;
  while ((*p = CTOUPPER (*p))) ++p;
  return s;
}


#ifdef WIDECHAR
static void uint32_to_wstr(uint32_t n, CHAR_T *buf, int radix)
{
    // assume buf only cotains ASCII numbers
    char *ptr = (char *) buf;
    _ultoa (n, ptr, radix);
    int len = strlen (ptr);
    CHAR_T *wptr = buf + len;
    ptr += len;
    while (ptr >= (char *) buf)
        *wptr-- = (CHAR_T) *ptr--;
}

static void uint64_to_wstr(uint64_t n, CHAR_T *buf, int radix)
{
    // assume buf only cotains ASCII numbers
    char *ptr = (char *) buf;
    _ulltoa (n, ptr, radix);
    int len = strlen (ptr);
    CHAR_T *wptr = buf + len;
    ptr += len;
    while (ptr >= (char *) buf)
        *wptr-- = (CHAR_T) *ptr--;
}


static void wremove_zeros (CHAR_T *digits, int keep)
{
    int i;
    i = CSTRLEN (digits) - 1;
    while (i >= keep && digits[i] == '0')
        --i;
    digits[i+1] = 0;
}


static CHAR_T *legacy_dtowstr (CHAR_T *buffer, int *p_exp, long double x, int ndigits,
                               int fmt, int dig)
{
    // assume buf only cotains ASCII numbers
    char *ptr = (char *) buffer;
    char *result = __legacy_dtoa (ptr, p_exp, x, ndigits, fmt, dig);
    int len = strlen (ptr);
    CHAR_T *wptr = buffer + len;
    ptr += len;
    while (ptr >= result)
        *wptr-- = (CHAR_T) *ptr--;
    return ++wptr;
}
#endif /* WIDECHAR */


/* Print the first N characters of the string S. */

static int out_str (olocal *v, const CHAR_T *s, int n)
{
  if (n >= 16)
    {
      if (fwrite_unlocked (s, sizeof(CHAR_T), n, v->stream) != n * sizeof(CHAR_T))
        return -1;
      v->count += n;
    }
  else
    while (n > 0)
      {
        PUTC (v, *s);
        ++s; --n;
      }
  return 0;
}


/* Note the return statement! */

#define OUT_STR(V,S,N) BEGIN if (out_str (V, S, N) != 0) return -1; END


/* Print the character C N times. */

static int out_pad (olocal *v, CHAR_T c, int n)
{
  CHAR_T buf[256];

  if (n > SIZEOF_ARRAY (buf))
    {
      int i;

      /* Very big padding -- do 256 characters at a time. */
      CMEMSET (buf, c, SIZEOF_ARRAY (buf));
      while (n > 0)
        {
          i = SIZEOF_ARRAY (buf);
          if (i > n)
            i = n;
          OUT_STR (v, buf, i);
          n -= i;
        }
    }
  else if (n >= 16)
    {
      CMEMSET (buf, c, n);
      OUT_STR (v, buf, n);
    }
  else
    while (n > 0)
      {
        PUTC (v, c);
        --n;
      }
  return 0;
}


/* Note the return statement! */

#define OUT_PAD(V,C,N) BEGIN if (out_pad (V, C, N) != 0) return -1; END


/* Perform formatting for the "%c" and "%s" formats.  The meaning of
   the '0' flag is not defined by ANSI for "%c" and "%s"; we always
   use blanks for padding. */

static int cvt_str (olocal *v, const char *str, int str_len)
{
  if (str == NULL)
    {
      /* Print "(null)" for the NULL pointer if the precision is big
         enough or not specified. */

      if (v->prec < 0 || v->prec >= 6)
        str = "(null)";
      else
        str = "";
    }

  /* The precision, if specified, limits the number of characters to
     be printed.  If the precision is specified, the array pointed to
     by STR does not need to be null-terminated. */

  if (str_len == -1)
    {
      if (v->prec >= 0)
        {
          const char *end = (const char *)memchr (str, 0, v->prec);
          if (end != NULL)
            str_len =  end - str;
          else
            str_len = v->prec;
        }
      else
        str_len = strlen (str);
    }
  else if (v->prec >= 0 && v->prec < str_len)
    str_len = v->prec;

  /* Print the string, using blanks for padding. */

  if (str_len < v->width && !v->minus)
    OUT_PAD (v, ' ', v->width - str_len);
#ifdef WIDECHAR
  mbstate_t state = {{0}};
  int left = str_len;
  while (left)
  {
      wchar_t wc;
      size_t cb = mbrtowc(&wc, str, left, &state);
      switch (cb)
      {
          /*
           * Quit the loop.
           */
          case -2: /* incomplete character - we ASSUME this cannot happen. */
          case -1: /* encoding error */
          case 0:  /* end of string */
              left = 0;
              break;

          default:
              str += cb;
              left -= cb;
              OUT_STR(v, &wc, 1);
              break;
      }
  }
#else
  OUT_STR (v, str, str_len);
#endif
  if (str_len < v->width && v->minus)
    OUT_PAD (v, ' ', v->width - str_len);
  return 0;
}


/* Perform formatting for the "%C" and "%S" formats.  The meaning of
   the '0' flag is not defined by ANSI for "%C" and "%S"; we always
   use blanks for padding. */

static int cvt_wstr (olocal *v, const wchar_t *str, int str_len)
{
  if (str == NULL)
    {
      /* Print "(null)" for the NULL pointer if the precision is big
         enough or not specified. */

      if (v->prec < 0 || v->prec >= 6)
        str = L"(null)";
      else
        str = L"";
    }

  /* The precision, if specified, limits the number of characters to
     be printed.  If the precision is specified, the array pointed to
     by STR does not need to be null-terminated. */

  if (str_len == -1)
    {
      if (v->prec >= 0)
        {
          const wchar_t *end = (const wchar_t *)wmemchr (str, 0, v->prec);
          if (end != NULL)
            str_len =  end - str;
          else
            str_len = v->prec;
        }
      else
        str_len = wcslen (str);
    }
  else if (v->prec >= 0 && v->prec < str_len)
    str_len = v->prec;

  /* Print the string, using blanks for padding. */

  if (str_len < v->width && !v->minus)
    OUT_PAD (v, ' ', v->width - str_len);
#ifdef WIDECHAR
  OUT_STR (v, str, str_len);
#else
  mbstate_t state = {{0}};
  for (int i = 0; i < str_len; i++)
  {
      char mb[MB_LEN_MAX + 1];
      size_t cb = wcrtomb(mb, *str, &state);
      switch (cb)
      {
          /*
           * Quit the loop.
           */
          case -2: /* incomplete character - we ASSUME this cannot happen. */
          case -1: /* encoding error */
          case 0:  /* end of string */
              i = str_len;
              break;

          default:
              str++;
              OUT_STR(v, mb, cb);
              break;
      }
  }
#endif
  if (str_len < v->width && v->minus)
    OUT_PAD (v, ' ', v->width - str_len);
  return 0;
}


/* Print and pad a number (which has already been converted into
   strings).  PFX is "0x" or "0X" for hexadecimal numbers, or NULL.
   DOT is "0." for floating point numbers in [0.0,1.0) unless using
   exponential format, the significant digits for large floating point
   numbers in the "%f" format, "0", or NULL.  Insert LPAD0 zeros
   between DOT and STR.  STR is the number.  Insert RPAD0 zeros
   between STR and XPS.  XPS is the exponent (unless it is NULL).
   IS_SIGNED is non-zero when printing a signed number. IS_NEG is
   non-zero for negative numbers (the strings don't contain a
   sign). */

static int cvt_number (olocal *v, const CHAR_T *pfx, const CHAR_T *dot,
                       const CHAR_T *str, const CHAR_T *xps,
                       int lpad0, int rpad0, int is_signed, int is_neg)
{
  int sign, pfx_len, dot_len, str_len, xps_len, min_len, lpad1;

  if (!is_signed)               /* No sign for %u, %o and %x */
    sign = EOF;
  else if (is_neg)
    sign = '-';
  else if (v->plus)             /* '+' overrides ' ' */
    sign = '+';
  else if (v->blank)
    sign = ' ';
  else
    sign = EOF;

  pfx_len = SAFE_STRLEN (pfx);
  dot_len = SAFE_STRLEN (dot);
  str_len = CSTRLEN (str);
  xps_len = SAFE_STRLEN (xps);

  if (lpad0 < 0)
    lpad0 = 0;
  if (rpad0 < 0)
    rpad0 = 0;
  lpad1 = 0;

  /* Compute the minimum length required for printing the number. */

  min_len = lpad0 + pfx_len + dot_len + str_len + rpad0 + xps_len;
  if (sign != EOF)
    ++min_len;

  /* If padding with zeros is requested, increase LPAD1 to pad the
     number on the left with zeros.  Note that the `-' flag
     (left-justify) turns off padding with zeros. */

  if (v->pad == '0' && min_len < v->width)
    {
      lpad1 += v->width - min_len;
      min_len = v->width;
    }

  /* If DOT is empty, we can combine LPAD0 into LPAD1. */

  if (dot_len == 0)
    {
      lpad1 += lpad0;
      lpad0 = 0;
    }

  /* Pad on the left with blanks. */

  if (min_len < v->width && !v->minus)
    OUT_PAD (v, ' ', v->width - min_len);

  /* Print the number. */

  if (sign != EOF)
    PUTC (v, sign);
  OUT_STR (v, pfx, pfx_len);
  OUT_PAD (v, '0', lpad1);
  OUT_STR (v, dot, dot_len);
  OUT_PAD (v, '0', lpad0);
  OUT_STR (v, str, str_len);
  OUT_PAD (v, '0', rpad0);
  OUT_STR (v, xps, xps_len);

  /* Pad on the right with blanks. */

  if (min_len < v->width && v->minus)
    OUT_PAD (v, ' ', v->width - min_len);
  return 0;
}


/* Print and pad an integer (which has already been converted into the
   string STR).  PFX is "0x" or "0X" for hexadecimal numbers, or NULL.
   ZERO is non-zero if the number is zero.  IS_NEG is non-zero if the
   number is negative (the string STR doesn't contain a sign). */

static int cvt_integer (olocal *v, const CHAR_T *pfx, const CHAR_T *str,
                        int zero, int is_signed, int is_neg)
{
  int lpad0;

  if (zero && v->prec == 0)     /* ANSI */
    return 0;

  if (v->prec >= 0)             /* Ignore `0' if `-' is given for an integer */
    v->pad = ' ';

  lpad0 = v->prec - CSTRLEN (str);
  return cvt_number (v, pfx, NULL, str, NULL, lpad0, 0, is_signed, is_neg);
}


static int cvt_hex (olocal *v, CHAR_T *str, CHAR_T x, int zero)
{
  if (x == 'X')
    str_upper (str);
  return cvt_integer (v, ((!zero && v->hash) ? (x == 'X' ? CHAR_L ("0X") : CHAR_L ("0x")) : NULL),
                      str, zero, FALSE, FALSE);
}


static int cvt_hex_32 (olocal *v, uint32_t n, CHAR_T x)
{
  CHAR_T buf[9];

  ULTOSTR (n, buf, 16);
  return cvt_hex (v, buf, x, n == 0);
}


static int cvt_hex_64 (olocal *v, uint64_t n, CHAR_T x)
{
  CHAR_T buf[17];

  ULLTOSTR (n, buf, 16);
  return cvt_hex (v, buf, x, n == 0);
}


static int cvt_oct (olocal *v, const CHAR_T *str, int zero)
{
  size_t len;

  if (v->hash && str[0] != '0')
    {
      len = CSTRLEN (str);
      if (v->prec <= (int)len)
        v->prec = len + 1;
    }
  return cvt_integer (v, NULL, str, zero && !v->hash, FALSE, FALSE);
}


static int cvt_oct_32 (olocal *v, uint32_t n)
{
  CHAR_T buf[12];

  ULTOSTR (n, buf, 8);
  return cvt_oct (v, buf, n == 0);
}


static int cvt_oct_64 (olocal *v, uint64_t n)
{
  CHAR_T buf[23];

  ULLTOSTR (n, buf, 8);
  return cvt_oct (v, buf, n == 0);
}


static int cvt_dec_32 (olocal *v, uint32_t n, int is_signed, int is_neg)
{
  CHAR_T buf[11];

  ULTOSTR (n, buf, 10);
  return cvt_integer (v, NULL, buf, n == 0, is_signed, is_neg);
}


static int cvt_dec_64 (olocal *v, uint64_t n, int is_signed, int is_neg)
{
  CHAR_T buf[21];

  ULLTOSTR (n, buf, 10);
  return cvt_integer (v, NULL, buf, n == 0, is_signed, is_neg);
}


/* Print a floating point number (which has been turned into the digit
   string DIGITS and the exponent XP) for the "%f" format.  If IS_NEG
   is non-zero, the number is negative. */

static int cvt_fixed_digits (olocal *v, CHAR_T *digits, int xp, int is_neg,
                             int is_auto)
{
  int lpad0, rpad0, len, frac;

  /* We have to handle 9 cases (the examples are for DECIMAL_DIG=4):

         | Fmt | Number    || digits | xp | pfx  |lpad0|rpad0| Output
     ----+-----+-----------++--------+----+------+-----+-----+---------
     (1) | .3f | 0.0001234 || "1234" | -4 | "0." | 0   | 3   | 0.000
     (2) | .7f | 0.001234  || "1234" | -3 | "0." | 2   | 1   | 0.0012340
     (3) | .3f | 0.001234  || "1234" | -3 | "0." | 2   | 0   | 0.001
     (4) | .3f | 0.1234    || "1234" | -1 | "0." | 0   | 0   | 0.123
     (5) | .3f | 1.234     || "1234" |  0 | N/A  | N/A | 0   | 1.234
     (6) | .1f | 12.34     || "1234" |  1 | N/A  | N/A | 0   | 12.3
     (7) | .3f | 123.4     || "1234" |  2 | N/A  | N/A | 2   | 123.400
     (8) | .3f | 1234.0    || "1234" |  3 | N/A  | N/A | 3   | 1234.000
     (9) | .3f | 123456.0  || "1235" |  5 |"1235"| 2   | 3   | 123500.000

   */


  if (xp < 0)
    {
      /* Cases (1) through (4).  We print "0." followed by

           MIN (-xp - 1, v->prec)

         zeros and the string of digit. */

      lpad0 = -xp - 1;          /* This is for cases (2) through (3) */
      if (v->prec < lpad0)
        lpad0 = v->prec;        /* This is for case (1) */

      /* Compute the number of zeros to append. */

      rpad0 = v->prec - (lpad0 + DECIMAL_DIG); /* This is for case (2) */
      if (rpad0 < 0)
        rpad0 = 0;              /* This is for cases (1), (3) and (4) */

      /* Truncate the string of digits according to the precision for
         cases (1), (3) and (4). */

      len = v->prec - lpad0;
      if (len >= 0 && len < DECIMAL_DIG)
        digits[len] = 0;

      if (is_auto)
        {
          rpad0 = 0;
          REMOVE_ZEROES (digits, 0);
          if (digits[0] == 0)
            lpad0 = 0;
        }

      return cvt_number (v, NULL, ((v->hash || digits[0] != 0 || lpad0 != 0)
                                   ? zdot : CHAR_L ("0")),
                         digits, NULL, lpad0, rpad0, TRUE, is_neg);
    }
  else if (xp < DECIMAL_DIG)
    {
      /* Cases (5) through (8). */

      /* Compute the number of zeros to append and truncate the string
         of digits. */

      frac = DECIMAL_DIG - xp - 1; /* Number of decimals (frac >= 0) */
      rpad0 = v->prec - frac;   /* For cases (5), (7) and (8) */
      if (rpad0 < 0)
        {
          /* Case (6) */
          digits[DECIMAL_DIG + rpad0] = 0;
          rpad0 = 0;
        }

      if (is_auto)
        {
          REMOVE_ZEROES (digits, xp + 1);
          rpad0 = 0;
        }

      /* Insert the decimal point. */
      if (v->hash || digits[xp + 1] != 0 || rpad0 != 0)
        {
          CMEMMOVE (digits + xp + 2, digits + xp + 1, DECIMAL_DIG - xp);
          digits[xp + 1] = zdot[1];
        }
      return cvt_number (v, NULL, NULL, digits, NULL, 0, rpad0, TRUE, is_neg);
    }
  else
    {
      /* Case (9). */

      lpad0 = xp - DECIMAL_DIG + 1;
      rpad0 = (is_auto ? 0 : v->prec);

      return cvt_number (v, NULL, digits,
                         ((v->hash || rpad0 != 0) ? zdot+1 : CHAR_L ("")),
                         NULL, lpad0, rpad0, TRUE, is_neg);
    }
}


/* Print a floating point number (which has been turned into the digit
   string DIGITS and the exponent XP) for the "%e" and "%g" formats.
   If IS_NEG is non-zero, the number is negative.  XP_CHAR is 'e' or
   'E'.  If IS_AUTO is non-zero, we should omit trailing zeros for the
   "%g" format. */

static int cvt_exp_digits (olocal *v, CHAR_T *digits, int xp, int is_neg,
                           CHAR_T xp_char, int is_auto)
{
  int i, rpad0;
  CHAR_T xps_buf[10];

  xps_buf[0] = xp_char;
  xps_buf[1] = '+';
  if (xp < 0)
    {
      xps_buf[1] = '-';
      xp = -xp;
    }
  i = 2;
  if (xp >= 1000)
    xps_buf[i++] = (CHAR_T)((xp / 1000) % 10) + '0';
  if (xp >= 100)
    xps_buf[i++] = (CHAR_T)((xp / 100) % 10) + '0';
  xps_buf[i++] = (CHAR_T)((xp / 10) % 10) + '0';
  xps_buf[i++] = (CHAR_T)(xp % 10) + '0';
  xps_buf[i] = 0;

  /* Insert decimal point. */

  if (v->prec == 0 && !v->hash)
    {
      digits[1] = 0;
      rpad0 = 0;
    }
  else
    {
      CMEMMOVE (digits + 2, digits + 1, DECIMAL_DIG);
      digits[1] = zdot[1];
      if (v->prec >= DECIMAL_DIG)
        rpad0 = 1 + v->prec - DECIMAL_DIG;
      else
        {
          rpad0 = 0;
          digits[2 + v->prec] = 0;
        }
      if (is_auto)
        {
          REMOVE_ZEROES (digits, 2);
          if (digits[2] == 0)
            digits[1] = 0;
          rpad0 = 0;
        }
    }
  return cvt_number (v, NULL, NULL, digits, xps_buf, 0, rpad0, TRUE, is_neg);
}


/* Perform formatting for the "%f" format. */

static int cvt_fixed (olocal *v, long double x, int is_neg)
{

  if (x == 0.0)
    return cvt_number (v, NULL, NULL, ((v->hash || v->prec > 0) ? zdot : CHAR_L ("0")),
                       NULL, 0, v->prec, TRUE, is_neg);
  else
    {
      CHAR_T digits[DECIMAL_DIG+2], *p;
      int xp;

      p = DTOSTR (digits, &xp, x, v->prec, DTOA_PRINTF_F, v->dig);
      return cvt_fixed_digits (v, p, xp, is_neg, FALSE);
    }
}


/* Perform formatting for the "%e" format.  XP_CHAR is 'e' or 'E'. */

static int cvt_exp (olocal *v, long double x, int is_neg, CHAR_T xp_char)
{
  if (x == 0.0)
    {
      static CHAR_T xps_buf[] = CHAR_L ("e+00");

      xps_buf[0] = xp_char;
      return cvt_number (v, NULL, NULL,
                         ((v->hash || v->prec > 0) ? zdot : CHAR_L ("0")),
                         xps_buf, 0, v->prec, TRUE, is_neg);
    }
  else
    {
      CHAR_T digits[DECIMAL_DIG+2], *p;
      int xp;

      p = DTOSTR (digits, &xp, x, v->prec, DTOA_PRINTF_E, v->dig);
      return cvt_exp_digits (v, p, xp, is_neg, xp_char, FALSE);
    }
}


/* Perform formatting for the "%g" format.  XP_CHAR is 'e' or 'E'. */

static int cvt_auto (olocal *v, long double x, int is_neg, CHAR_T xp_char)
{
  /* A precision of zero is treated as a precision of 1.  Note that
     the precision defines the number of significant digits, not the
     number of decimals! */

  if (v->prec == 0)
    v->prec = 1;

  /* 0.0 is treated specially as __legacy_dtoa() etc. cannot handle that
     case. */

  if (x == 0.0)
    return cvt_number (v, NULL, NULL, (v->hash ? zdot : CHAR_L ("0")), NULL,
                       0, (v->hash ? v->prec - 1 : 0), TRUE, is_neg);
  else
    {
      CHAR_T digits[DECIMAL_DIG+2], *p;
      int xp;

      p = DTOSTR (digits, &xp, x, v->prec, DTOA_PRINTF_G, v->dig);

      /* If the exponent (of "%e" format) is less than -4 or greater
         than or equal to the precision, use "%e" format.  Otherwise,
         use "%f" format. */

      if (xp < -4 || xp >= v->prec)
        {
          /* Adjust the precision to indicate the number of
             decimals. */

          v->prec -= 1;

          /* Treat "%#g" like "%e" (except for the precision).  "%g"
             (without `#') removes trailing zeros. */

          return cvt_exp_digits (v, p, xp, is_neg, xp_char, !v->hash);
        }
      else
        {
          /* Compute the number of decimals from the exponent and the
             precision.  We must not remove trailing zeros from the
             integer part of the number! */

          v->prec -= xp + 1;
          if (v->prec < 0)
            v->prec = 0;

          return cvt_fixed_digits (v, p, xp, is_neg, !v->hash);
        }
    }
}


/* Print the floating point number X.  LIM is the number of
   significant digits of X (depending on the type of X), FMT is the
   format character from the format string. */

static int cvt_float (olocal *v, long double x, CHAR_T fmt)
{
  const CHAR_T *s;
  int is_neg, fpclass;

  fpclass = fpclassify (x);
  is_neg = signbit (x);
  switch (fpclass)
    {
    case FP_NAN:
      s = (fmt & 0x20) ? CHAR_L ("nan") : CHAR_L ("NAN");
      is_neg = 0;               /* Don't print -NAN */
      break;
    case FP_INFINITE:
      s = (fmt & 0x20) ? CHAR_L ("inf") : CHAR_L ("INF");
      break;
    default:
      s = NULL;
      break;
    }

  if (s != NULL)
    {
      v->pad = ' ';
      return cvt_number (v, NULL, NULL, s, NULL, 0, 0, TRUE, is_neg);
    }

  if (v->prec < 0)
    v->prec = DEFAULT_PREC;

  if (is_neg)
    x = -x;

  switch (fmt)
    {
    case 'f':
    case 'F':
      return cvt_fixed (v, x, is_neg);

    case 'g':
      return cvt_auto (v, x, is_neg, 'e');

    case 'G':
      return cvt_auto (v, x, is_neg, 'E');

    case 'e':
      return cvt_exp (v, x, is_neg, 'e');

    case 'E':
      return cvt_exp (v, x, is_neg, 'E');

    default:
      abort ();
    }
}


/* This is the working horse for printf() and friends. */

int NAME(_output) (FILE *stream, const CHAR_T *format, char *arg_ptr)
{
  olocal v;
  int size;
  int cont;
  CHAR_T c;
  int mbn, shift;

  /* Initialize variables. */

  v.stream = stream;
  v.count = 0;
#ifdef STANDALONE
  zdot[1] = localeconv()->decimal_point[0];
#else
  zdot[1] = __libc_gLocaleLconv.s.decimal_point[0];
#endif

  /* ANSI X3.159-1989, 4.9.6.1: "The format shall be a multibyte
     character sequence, beginning and ending in its initial shift
     state." */

  shift = 0;

  /* Interpret the string. */

  while ((c = *format) != 0)
    if (c != '%')
      {
#ifdef WIDECHAR
        PUTC (&v, c);
        ++format;
#else
        /* ANSI X3.159-1989, 4.9.6.1: "... ordinary multibyte
           charcters (not %), which are copied unchanged to the output
           stream..."

           Avoid the overhead of calling mblen(). */

        if (!CHK_MBCS_PREFIX (&__libc_GLocaleCtype, c, mbn))
          mbn = 1;
        while (mbn > 0)
          {
            PUTC (&v, *format);
            ++format; --mbn;
          }
#endif
      }
    else if (format[1] == '%')
      {
        /* ANSI X3.159-1989, 4.9.6.1: "The complete conversion
           specification shall be %%." */

        PUTC (&v, '%');
        format += 2;
      }
    else
      {
        v.minus = v.plus = v.blank = v.hash = FALSE;
        v.width = 0; v.prec = -1; size = 0; v.pad = ' ';
        cont = TRUE;
        do
          {
            ++format;
            switch (*format)
              {
              case '-':
                v.minus = TRUE;
                break;
              case '+':
                v.plus = TRUE;
                break;
              case '0':
                v.pad = '0';
                break;
              case ' ':
                v.blank = TRUE;
                break;
              case '#':
                v.hash = TRUE;
                break;
              default:
                cont = FALSE;
                break;
              }
          } while (cont);

        /* `-' overrides `0' */

        if (v.minus)
          v.pad = ' ';

        /* Field width */

        if (*format == '*')
          {
            ++format;
            v.width = va_arg (arg_ptr, int);
            if (v.width < 0)
              {
                v.width = -v.width;
                v.minus = TRUE;
              }
          }
        else
          while (*format >= '0' && *format <= '9')
            {
              v.width = v.width * 10 + (*format - '0');
              ++format;
            }

        /* Precision */

        if (*format == '.')
          {
            ++format;
            if (*format == '*')
              {
                ++format;
                v.prec = va_arg (arg_ptr, int);
                if (v.prec < 0)
                  v.prec = -1;  /* We don't need this */
              }
            else
              {
                v.prec = 0;
                while (*format >= '0' && *format <= '9')
                  {
                    v.prec = v.prec * 10 + (*format - '0');
                    ++format;
                  }
              }
          }

        /* Size */

        if (   *format == 'h' || *format == 'l' || *format == 'L'
            || *format == 'j' || *format == 'z' || *format == 't'
            || *format == 'q' || *format == 'Z' )
          {
            size = *format++;
            if (size == 'l' && *format == 'l')
              {
                size = SIZE_LL; ++format;
              }
            else if (size == 'h' && *format == 'h')
              {
                size = SIZE_HH; ++format;
              }
          }

        /* Format */

        switch (*format)
          {
          case 0:
            return v.count;

          case 'n':
            if (IS_SIZE_64BIT(size))
              {
                int64_t *ptr = va_arg (arg_ptr, int64_t *);
                *ptr = v.count;
              }
            else if (IS_SIZE_16BIT(size))
              {
                int16_t *ptr = va_arg (arg_ptr, int16_t *);
                *ptr = v.count;
              }
            else if (IS_SIZE_8BIT(size))
              {
                int8_t *ptr = va_arg (arg_ptr, int8_t *);
                *ptr = v.count;
              }
            else /* 32-bit */
              {
                int32_t *ptr = va_arg (arg_ptr, int32_t *);
                *ptr = v.count;
              }
            break;

          case 'c':
            if (size != 'l' && size != 'L')
              {
                char c;

                c = (char)va_arg (arg_ptr, int);
                v.prec = 1;
                CHECK (cvt_str (&v, &c, 1));
                break;
              }
            /* fall thru */
          case 'C':
            {
              wchar_t wc;

              wc = (wchar_t)va_arg (arg_ptr, int);
              v.prec = 1;
              CHECK (cvt_wstr (&v, &wc, 1));
            }
            break;

          case 's':
            if (size != 'l' && size != 'L')
              {
                CHECK (cvt_str (&v, va_arg (arg_ptr, const char *), -1));
                break;
              }
            /* fall thru */
          case 'S':
            CHECK (cvt_wstr (&v, va_arg (arg_ptr, const wchar_t *), -1));
            break;

          case 'd':
          case 'i':
            if (IS_SIZE_64BIT(size))
              {
                int64_t n = va_arg (arg_ptr, int64_t);
                if (n < 0)
                  CHECK (cvt_dec_64 (&v, -n, TRUE, TRUE));
                else
                  CHECK (cvt_dec_64 (&v, n, TRUE, FALSE));
              }
            else
              {
                int32_t n = va_arg (arg_ptr, int32_t);
                if (size == 'h')
                  n = (short)n;
                else if (size == SIZE_HH)
                  n = (char)n;
                if (n < 0)
                  CHECK (cvt_dec_32 (&v, -n, TRUE, TRUE));
                else
                  CHECK (cvt_dec_32 (&v, n, TRUE, FALSE));
              }
            break;

          case 'u':
            if (IS_SIZE_64BIT (size))
              CHECK (cvt_dec_64 (&v, va_arg (arg_ptr, uint64_t), FALSE, FALSE));
            else
              {
                uint32_t n = va_arg (arg_ptr, uint32_t);
                if (size == 'h')
                  n = (unsigned short)n;
                else if (size == SIZE_HH)
                  n = (unsigned char)n;
                CHECK (cvt_dec_32 (&v, n, FALSE, FALSE));
              }
            break;

          case 'p':
            v.hash = TRUE;
            if (sizeof(uintptr_t) == sizeof(uint64_t))
                CHECK (cvt_hex_64 (&v, va_arg (arg_ptr, uintptr_t), 'x'));
            else
                CHECK (cvt_hex_32 (&v, va_arg (arg_ptr, uintptr_t), 'x'));
            break;

          case 'x':
          case 'X':
            if (IS_SIZE_64BIT (size))
              CHECK (cvt_hex_64 (&v, va_arg (arg_ptr, uint64_t),
                                 *format));
            else
              {
                uint32_t n = va_arg (arg_ptr, uint32_t);
                if (size == 'h')
                  n = (unsigned short)n;
                else if (size == SIZE_HH)
                  n = (unsigned char)n;
                CHECK (cvt_hex_32 (&v, n, *format));
              }
            break;

          case 'o':
            if (IS_SIZE_64BIT (size))
              CHECK (cvt_oct_64 (&v, va_arg (arg_ptr, uint64_t)));
            else
              {
                uint32_t n = va_arg (arg_ptr, uint32_t);
                if (size == 'h')
                  n = (unsigned short)n;
                else if (size == SIZE_HH)
                  n = (unsigned char)n;
                CHECK (cvt_oct_32 (&v, n));
              }
            break;

          case 'g':
          case 'G':
          case 'e':
          case 'E':
          case 'f':
          case 'F':
            if (size == 'L')
              {
                long double x = va_arg (arg_ptr, long double);
                v.dig = LDBL_DIG;
                CHECK (cvt_float (&v, x, *format));
              }
            else
              {
                double x = va_arg (arg_ptr, double);
                v.dig = DBL_DIG;
                CHECK (cvt_float (&v, x, *format));
              }
            break;

          case 'm': /* GLIBC extension which equals '%s' with strerror(errno). */
            CHECK (cvt_str (&v, strerror(errno), -1));
            break;

          default:

            /* ANSI X3.159-1989, 4.9.6.1: "If a conversion
               specification is invalid, the behavior is undefined."

               We print the last letter only. */

            OUT_STR (&v, format, 1);
            break;
          }
        ++format;
      }

  /* Return the number of characters printed. */

  return v.count;
}

#ifdef STANDALONE

int vswprintf (wchar_t *buffer, size_t n, const wchar_t *format, va_list arg_ptr)
{
  FILE trick;
  int result;

  if (n > INT_MAX)
    return EOF;
  trick.__uVersion = _FILE_STDIO_VERSION;
  trick._buffer = (char *) buffer;
  trick._ptr = (char *) buffer;
  trick._rcount = 0;
  trick._wcount = n != 0 ? ((int)n - 1) * sizeof(wchar_t) : 0;
  trick._handle = -1;
  trick._flags = _IOOPEN|_IOSPECIAL|_IOBUFUSER|_IOWRT;
  trick._buf_size = ((int)n) * sizeof(CHAR_T);
  trick._flush = NULL;
  trick._ungetc_count = 0;
  trick._mbstate = 0;
  _fmutex_dummy(&trick.__u.__fsem);
  result = _output_wchar (&trick, format, arg_ptr);
  if (n != 0)
    *trick._ptr = 0;
  return result;
}

#endif
