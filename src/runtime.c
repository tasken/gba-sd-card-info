/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t count)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (count--)
        *d++ = *s++;
    return dst;
}

void *memset(void *dst, int value, size_t count)
{
    unsigned char *d = dst;
    while (count--)
        *d++ = (unsigned char)value;
    return dst;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *a = left, *b = right;
    while (count--) {
        if (*a != *b)
            return (int)*a - (int)*b;
        ++a;
        ++b;
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *end = s;
    while (*end)
        ++end;
    return (size_t)(end - s);
}

char *strchr(const char *s, int c)
{
    for (;;) {
        if ((unsigned char)*s == (unsigned char)c)
            return (char *)s;
        if (!*s)
            return NULL;
        ++s;
    }
}
