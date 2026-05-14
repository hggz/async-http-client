//===----------------------------------------------------------------------===//
//
// This source file is part of the AsyncHTTPClient open source project
//
// Copyright (c) 2018-2021 Apple Inc. and the AsyncHTTPClient project authors
// Licensed under Apache License v2.0
//
// See LICENSE.txt for license information
// See CONTRIBUTORS.txt for the list of AsyncHTTPClient project authors
//
// SPDX-License-Identifier: Apache-2.0
//
//===----------------------------------------------------------------------===//

#if __APPLE__
    #include <xlocale.h>
#elif __linux__
    #include <locale.h>
#endif

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
// Windows UCRT exposes neither `strptime` nor `strptime_l`. AHC only uses
// these to parse the three fixed HTTP cookie date formats (all POSIX
// C-locale, US English month/day names), so we provide a tiny hand-rolled
// parser instead of pulling a full strptime implementation.
//
// Supported formats (all that AHC's HTTPClient+HTTPCookie.swift passes in):
//   "%a, %d %b %Y %H:%M:%S"   e.g. "Sun, 06 Nov 1994 08:49:37"
//   "%a, %d-%b-%y %H:%M:%S"   e.g. "Sunday, 06-Nov-94 08:49:37"
//   "%a %b %d %H:%M:%S %Y"    e.g. "Sun Nov  6 08:49:37 1994"
//
// The `_l` variant ignores its locale argument on Windows — AHC always
// passes a POSIX-locale handle, which is exactly what our parser implements.

#include <ctype.h>

static const char * const _ahc_win_months[12] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

// Skip whitespace and ',' / '-' separators.
static const char * _ahc_win_skip_sep(const char * p) {
    while (*p == ' ' || *p == '\t' || *p == ',' || *p == '-') p++;
    return p;
}

// Read decimal integer of at most `max_digits` digits.
// Advances *pp past digits. Returns -1 if no digits.
static int _ahc_win_read_int(const char ** pp, int max_digits) {
    const char * p = *pp;
    int n = 0, consumed = 0;
    while (consumed < max_digits && *p >= '0' && *p <= '9') {
        n = n * 10 + (*p - '0');
        p++; consumed++;
    }
    if (consumed == 0) return -1;
    *pp = p;
    return n;
}

// Skip a weekday token (e.g. "Mon", "Monday", "Sun"). AHC does not need
// the weekday value, just to advance past it.
static const char * _ahc_win_skip_word(const char * p) {
    while (isalpha((unsigned char)*p)) p++;
    return p;
}

// Try to match a 3-letter month abbreviation case-insensitively.
// Returns 0..11 on match, -1 otherwise. Advances *pp past the match.
static int _ahc_win_read_month(const char ** pp) {
    const char * p = *pp;
    if (!isalpha((unsigned char)p[0]) || !isalpha((unsigned char)p[1]) || !isalpha((unsigned char)p[2])) {
        return -1;
    }
    for (int i = 0; i < 12; i++) {
        if ((p[0] | 0x20) == (_ahc_win_months[i][0] | 0x20) &&
            (p[1] | 0x20) == (_ahc_win_months[i][1] | 0x20) &&
            (p[2] | 0x20) == (_ahc_win_months[i][2] | 0x20)) {
            *pp = p + 3;
            // Skip remainder of month name if present (e.g. "January").
            while (isalpha((unsigned char)**pp)) (*pp)++;
            return i;
        }
    }
    return -1;
}

// Parse "HH:MM:SS" — advances *pp.
static bool _ahc_win_read_hms(const char ** pp, int *h, int *m, int *s) {
    const char * p = *pp;
    int hh = _ahc_win_read_int(&p, 2);  if (hh < 0 || *p != ':') return false; p++;
    int mm = _ahc_win_read_int(&p, 2);  if (mm < 0 || *p != ':') return false; p++;
    int ss = _ahc_win_read_int(&p, 2);  if (ss < 0) return false;
    *h = hh; *m = mm; *s = ss;
    *pp = p;
    return true;
}

// Returns true if entire `string` was consumed by parser for `format`.
static bool _ahc_win_parse(const char * string, const char * format, struct tm * result) {
    memset(result, 0, sizeof(*result));
    const char * p = string;

    if (strcmp(format, "%a, %d %b %Y %H:%M:%S") == 0) {
        p = _ahc_win_skip_word(p);              // %a
        p = _ahc_win_skip_sep(p);               // ", "
        int d = _ahc_win_read_int(&p, 2);       if (d < 1) return false;
        p = _ahc_win_skip_sep(p);
        int mon = _ahc_win_read_month(&p);      if (mon < 0) return false;
        p = _ahc_win_skip_sep(p);
        int y = _ahc_win_read_int(&p, 4);       if (y < 0) return false;
        p = _ahc_win_skip_sep(p);
        int hh, mm, ss; if (!_ahc_win_read_hms(&p, &hh, &mm, &ss)) return false;
        result->tm_mday = d; result->tm_mon = mon; result->tm_year = y - 1900;
        result->tm_hour = hh; result->tm_min = mm; result->tm_sec = ss;
    } else if (strcmp(format, "%a, %d-%b-%y %H:%M:%S") == 0) {
        p = _ahc_win_skip_word(p);
        p = _ahc_win_skip_sep(p);
        int d = _ahc_win_read_int(&p, 2);       if (d < 1) return false;
        p = _ahc_win_skip_sep(p);
        int mon = _ahc_win_read_month(&p);      if (mon < 0) return false;
        p = _ahc_win_skip_sep(p);
        int y = _ahc_win_read_int(&p, 2);       if (y < 0) return false;
        // Per RFC 6265, two-digit years 0-69 mean 2000-2069, 70-99 mean 1970-1999.
        int year = (y < 70) ? (2000 + y) : (1900 + y);
        p = _ahc_win_skip_sep(p);
        int hh, mm, ss; if (!_ahc_win_read_hms(&p, &hh, &mm, &ss)) return false;
        result->tm_mday = d; result->tm_mon = mon; result->tm_year = year - 1900;
        result->tm_hour = hh; result->tm_min = mm; result->tm_sec = ss;
    } else if (strcmp(format, "%a %b %d %H:%M:%S %Y") == 0) {
        p = _ahc_win_skip_word(p);
        p = _ahc_win_skip_sep(p);
        int mon = _ahc_win_read_month(&p);      if (mon < 0) return false;
        p = _ahc_win_skip_sep(p);
        int d = _ahc_win_read_int(&p, 2);       if (d < 1) return false;
        p = _ahc_win_skip_sep(p);
        int hh, mm, ss; if (!_ahc_win_read_hms(&p, &hh, &mm, &ss)) return false;
        p = _ahc_win_skip_sep(p);
        int y = _ahc_win_read_int(&p, 4);       if (y < 0) return false;
        result->tm_mday = d; result->tm_mon = mon; result->tm_year = y - 1900;
        result->tm_hour = hh; result->tm_min = mm; result->tm_sec = ss;
    } else if (strcmp(format, "%a %b") == 0) {
        // Used only by HTTPClientCookieTests as a locale sanity check.
        p = _ahc_win_skip_word(p);
        p = _ahc_win_skip_sep(p);
        int mon = _ahc_win_read_month(&p);      if (mon < 0) return false;
        result->tm_mon = mon;
    } else {
        return false;
    }

    return *p == 0;
}
#endif // _WIN32

bool swiftahc_cshims_strptime(const char * string, const char * format, struct tm * result) {
#if defined(_WIN32)
    return _ahc_win_parse(string, format, result);
#else
    const char * firstNonProcessed = strptime(string, format, result);
    if (firstNonProcessed) {
        return *firstNonProcessed == 0;
    }
    return false;
#endif
}

bool swiftahc_cshims_strptime_l(const char * string, const char * format, struct tm * result, void * locale) {
    // The pointer cast is fine as long we make sure it really points to a locale_t.
#if defined(_WIN32)
    // No `locale_t` on Windows UCRT. AHC always passes a POSIX C-locale
    // handle, which our hand-rolled parser implements exactly.
    (void)locale;
    return _ahc_win_parse(string, format, result);
#elif defined(__musl__) || defined(__ANDROID__)
    (void)locale;
    const char * firstNonProcessed = strptime(string, format, result);
    if (firstNonProcessed) {
        return *firstNonProcessed == 0;
    }
    return false;
#else
    const char * firstNonProcessed = strptime_l(string, format, result, (locale_t)locale);
    if (firstNonProcessed) {
        return *firstNonProcessed == 0;
    }
    return false;
#endif
}
