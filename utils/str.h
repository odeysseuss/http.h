/*
* SDS style binary safe string library
* REFERENCE: This library is designed similarly as [antirez/sds](https://github.com/antirez/sds)
*
* USAGE:
*   #define STR_IMPLEMENTATION
*   #include "str.h"
*/

#ifndef STR_H
#define STR_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define malloc_ malloc
#define calloc_ calloc
#define realloc_ realloc
#define free_ free

typedef char *String;

/// Constructors
/// MUST free this strings explicitly using strFree
String strNewLen(const void *s, size_t len);
String strNew(const char *cstr);
String strEmpty(void);
String strDup(const String s);
String strSlice(const String s, size_t start, size_t end);
/// Get string length
size_t strLen(const String s);
/// Comparison of strings
int strCmpLen(const String s1, const void *s2, size_t s2_len);
int strCmpCStr(const String s1, const char *s2);
int strCmp(const String s1, const String s2);
/// Grow or trim strings
/// MUST reasign back to original string (whatever parameter s was previously)
String strGrow(const String s, size_t addlen);
String strTrim(String s);
/// Concatinating strings
String strCatLen(String dest, const void *src, size_t len);
String strCatCStr(String dest, const char *src);
String strCat(String dest, const String src);
String strCatFmt(String dest, const char *fmt, ...);
/// Finding substrings
size_t strFindLen(const String s, const void *t, size_t len);
size_t strFind(const String s, const char *cstr);
size_t strFindLastLen(const String s, const void *t, size_t len);
size_t strFindLast(const String s, const char *cstr);
/// Spitting strings
String *
strSplitLen(const String s, const void *sep, size_t seplen, size_t *count);
String *strSplit(const String s, const char *sep, size_t *count);
void strSplitResFree(String *toks, size_t count);
/// Joining strings
/// Creates a new string
/// Free with strFree
String strJoinLen(String *toks, size_t count, const void *sep, size_t seplen);
String strJoin(String *toks, size_t count, const char *sep);
/// Replace substrings
String strReplaceLen(String s,
                     const void *from,
                     size_t from_len,
                     const void *to,
                     size_t to_len);
String strReplace(const String s, const char *from, const char *to);
/// String casing
String strToLower(String s);
String strToUpper(String s);
/// Clear without freeing strings
String strClear(String s);
/// Destructors
void strFree(String s);

#ifdef STR_IMPLEMENTATION

#    define STR_MAX_PREALLOC 1024

// header for String type
// used as a binary perfix that's stored before the actual String type
typedef struct {
    size_t len_;
    size_t alloc_;
} StrHdr_;

// get pointer to header from String
static inline StrHdr_ *getStrHdr_(const String s) {
    return ((StrHdr_ *)s - 1);
}

// get available memory space from String
static inline size_t getStrAvail_(const String s) {
    return (getStrHdr_(s)->alloc_ - getStrHdr_(s)->len_);
}

static inline size_t getStrLen_(const String s) {
    return (getStrHdr_(s)->len_);
}

static inline size_t getStrAlloc_(const String s) {
    return (getStrHdr_(s)->alloc_);
}

String strNewLen(const void *s, const size_t len) {
    StrHdr_ *hdr = (StrHdr_ *)malloc_(sizeof(StrHdr_) + len + 1);
    hdr->len_ = len;
    hdr->alloc_ = len;

    String str = (String)(hdr + 1);
    if (s && len > 0) {
        memcpy(str, s, len);
    }
    str[len] = '\0';

    return str;
}

String strNew(const char *cstr) {
    return strNewLen(cstr, strlen(cstr));
}

String strEmpty(void) {
    return strNewLen("", 0);
}

size_t strLen(const String s) {
    if (!s) {
        return -1;
    }

    return getStrLen_(s);
}

int strCmpLen(const String s1, const void *s2, size_t s2_len) {
    if (!s1 || !s2) {
        return -1;
    }

    size_t s1_len = getStrLen_(s1);
    size_t min_len = s1_len < s2_len ? s1_len : s2_len;

    int cmp = memcmp(s1, s2, min_len);
    if (cmp != 0) {
        return cmp;
    }

    return (s1_len > s2_len) - (s1_len < s2_len);
}

int strCmp(const String s1, const String s2) {
    return strCmpLen(s1, s2, strLen(s2));
}

int strCmpCStr(const String s1, const char *s2) {
    return strCmpLen(s1, s2, strlen(s2));
}

String strDup(const String s) {
    return strNewLen(s, getStrLen_(s));
}

String strSlice(const String s, size_t start, size_t end) {
    size_t len = getStrLen_(s);
    if (end > len) {
        end = len;
    }

    size_t new_len = end - start;
    return strNewLen(s + start, new_len);
}

String strGrow(const String s, size_t addlen) {
    if (!s || addlen == 0) {
        return NULL;
    }

    StrHdr_ *hdr = getStrHdr_(s);
    size_t curr_len = hdr->len_;
    size_t new_alloc = hdr->alloc_ + addlen;
    StrHdr_ *new_hdr = realloc_(hdr, sizeof(StrHdr_) + new_alloc + 1);
    new_hdr->alloc_ = new_alloc;
    new_hdr->len_ = curr_len;

    return (String)(new_hdr + 1);
}

String strTrim(String s) {
    if (!s) {
        return NULL;
    }

    StrHdr_ *hdr = getStrHdr_(s);
    if (getStrAvail_(s) == 0) {
        return s;
    }

    size_t new_alloc = hdr->len_;
    StrHdr_ *new_hdr = realloc_(hdr, sizeof(StrHdr_) + new_alloc + 1);
    new_hdr->alloc_ = new_alloc;
    new_hdr->len_ = new_alloc;

    return (String)(new_hdr + 1);
}

static String strMakeRoom_(String s, size_t addlen) {
    size_t new_len = getStrLen_(s) + addlen;
    if (new_len < STR_MAX_PREALLOC) {
        new_len *= 2;
    } else {
        new_len *= 1.5;
    }

    return strGrow(s, new_len);
}

String strCatLen(String dest, const void *src, size_t len) {
    if (!dest || !src || len == 0) {
        return dest;
    }

    size_t curr_len = getStrLen_(dest);
    if (getStrAvail_(dest) < len) {
        dest = strMakeRoom_(dest, len);
    }

    memcpy(dest + curr_len, src, len);
    StrHdr_ *hdr = getStrHdr_(dest);
    hdr->len_ = curr_len + len;
    dest[hdr->len_] = '\0';

    return dest;
}

String strCatCStr(String dest, const char *src) {
    return strCatLen(dest, src, strlen(src));
}

String strCat(String dest, const String src) {
    return strCatLen(dest, src, getStrLen_(src));
}

String strCatFmt(String dest, const char *fmt, ...) {
    if (!dest || !fmt) {
        return NULL;
    }

    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    if (needed < 0) {
        return NULL;
    }
    va_end(ap);

    if (getStrAvail_(dest) < (size_t)needed) {
        dest = strMakeRoom_(dest, needed);
    }

    StrHdr_ *hdr = getStrHdr_(dest);
    size_t len = getStrLen_(dest);
    va_start(ap, fmt);
    int written = vsnprintf(dest + len, needed + 1, fmt, ap);
    if (written < 0) {
        return NULL;
    }
    va_end(ap);

    hdr->len_ = len + written;
    return dest;
}

size_t strFindLen(const String s, const void *t, size_t len) {
    if (!s || !t || len == 0) {
        return -1;
    }

    size_t s_len = getStrLen_(s);
    if (len == 1) {
        for (size_t i = 0; i < s_len; i++) {
            if (s[i] == *(const unsigned char *)t) {
                return i;
            }
        }
        return -1;
    }

    for (size_t i = 0; i < s_len; i++) {
        if (memcmp(s + i, t, len) == 0) {
            return i;
        }
    }

    return -1;
}

size_t strFind(const String s, const char *cstr) {
    return strFindLen(s, cstr, strlen(cstr));
}

size_t strFindLastLen(const String s, const void *t, size_t len) {
    if (!s || !t || len == 0) {
        return -1;
    }

    size_t s_len = getStrLen_(s);
    if (len == 1) {
        for (size_t i = s_len; i > 0; i--) {
            if (s[i] == *(const unsigned char *)t) {
                return i;
            }
        }
        return -1;
    }

    for (size_t i = s_len - len; i < s_len; i--) {
        if (memcmp(s + i, t, len) == 0) {
            return i;
        }
    }

    return -1;
}

size_t strFindLast(const String s, const char *cstr) {
    return strFindLastLen(s, cstr, strlen(cstr));
}

String strClear(String s) {
    if (!s) {
        return NULL;
    }

    StrHdr_ *hdr = getStrHdr_(s);
    hdr->len_ = 0;
    s[hdr->len_] = '\0';

    return s;
}

String *
strSplitLen(const String s, const void *sep, size_t seplen, size_t *count) {
    if (!s || !sep || seplen == 0 || !count) {
        return NULL;
    }

    size_t slots = 8;
    String *tokens = calloc_(slots, sizeof(String));
    size_t len = getStrLen_(s);
    size_t elems = 0;
    size_t start = 0;

    for (size_t i = 0; i < (len - (seplen - 1)); i++) {
        if (slots < elems + 2) {
            slots *= 2;
            String *new_toks = realloc_(tokens, sizeof(String) * slots);
            if (!new_toks) {
                goto cleanup;
            }
            tokens = new_toks;
        }

        char sepch = ((const char *)sep)[0];
        if ((seplen == 1 && *(s + i) == sepch) ||
            (memcmp(s + i, sep, seplen) == 0)) {
            tokens[elems] = strNewLen(s + start, i - start);
            if (!tokens[elems]) {
                goto cleanup;
            }
            elems++;
            start = i + seplen;
            i = i + seplen - 1;
        }
    }

    tokens[elems] = strNewLen(s + start, len - start);
    if (!tokens[elems]) {
        goto cleanup;
    }
    elems++;
    *count = elems;
    return tokens;

cleanup:
    for (size_t i = 0; i < elems; i++) {
        strFree(tokens[i]);
    }
    free_(tokens);
    *count = 0;
    return NULL;
}

String *strSplit(const String s, const char *sep, size_t *count) {
    return strSplitLen(s, sep, strlen(sep), count);
}

void strSplitResFree(String *toks, size_t count) {
    if (!toks || count == 0) {
        return;
    }

    while (count--) {
        strFree(toks[count]);
    }
    free_(toks);
}

String strJoinLen(String *toks, size_t count, const void *sep, size_t seplen) {
    if (!toks || !sep || count == 0 || seplen == 0) {
        return NULL;
    }

    size_t total_len = 0;
    for (size_t i = 0; i < count; i++) {
        total_len += getStrLen_(toks[i]);
    }

    if (count > 1) {
        total_len += (count - 1) * seplen;
    }

    String str = strNewLen(NULL, total_len);

    size_t curr_pos = 0;
    for (size_t i = 0; i < count; i++) {
        size_t tok_len = getStrLen_(toks[i]);
        if (tok_len > 0) {
            memcpy(str + curr_pos, toks[i], tok_len);
            curr_pos += tok_len;
        }

        if (i < count - 1) {
            memcpy(str + curr_pos, sep, seplen);
            curr_pos += seplen;
        }
    }

    str[total_len] = '\0';
    StrHdr_ *hdr = getStrHdr_(str);
    hdr->len_ = total_len;

    return str;
}

String strJoin(String *toks, size_t count, const char *sep) {
    return strJoinLen(toks, count, sep, strlen(sep));
}

String strReplaceLen(String s,
                     const void *from,
                     size_t from_len,
                     const void *to,
                     size_t to_len) {
    if (!s || !from || !to) {
        return NULL;
    }

    size_t curr_len = getStrLen_(s);
    size_t min_len = from_len < to_len ? from_len : to_len;
    for (size_t i = 0; i < curr_len; i++) {
        for (size_t j = 0; j < min_len; j++) {
            if (((unsigned char *)s)[i] == ((const unsigned char *)from)[j]) {
                ((unsigned char *)s)[i] = ((const unsigned char *)to)[j];
            }
        }
    }

    return s;
}

String strReplace(const String s, const char *from, const char *to) {
    return strReplaceLen(s, from, strlen(from), to, strlen(to));
}

String strToLower(String s) {
    if (!s) {
        return NULL;
    }

    size_t len = getStrLen_(s);
    for (size_t i = 0; i < len; i++) {
        s[i] += 32 * (s[i] >= 'A' && s[i] <= 'Z');
    }

    return s;
}

String strToUpper(String s) {
    if (!s) {
        return NULL;
    }

    size_t len = getStrLen_(s);
    for (size_t i = 0; i < len; i++) {
        s[i] -= 32 * (s[i] >= 'a' && s[i] <= 'z');
    }

    return s;
}

void strFree(String s) {
    if (!s) {
        return;
    }

    StrHdr_ *hdr = getStrHdr_(s);
    free(hdr);
}

#endif // STRING_IMPLEMENTATION

#undef malloc_
#undef calloc_
#undef realloc_
#undef free_

#ifdef __cplusplus
}
#endif

#endif
