#include "hex.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

int hex_encode(const void *const b, char *hex, const size_t buflen,
    size_t hexlen)
{
    const char *buf = b;

    for (size_t i = 0; i < buflen; i++)
    {
        const int r = snprintf(hex, hexlen, "%02hhx", *(const char *)buf++);

        if (r < 0 || r >= hexlen)
            return -1;

        hexlen -= r;
        hex += 2;
    }

    return 0;
}

int hex_decode(const char *const hex, void *const b, size_t n)
{
    unsigned char *buf = b;

    for (const char *s = hex; *s; s += 2)
    {
        const char nibble[sizeof "00"] = {*s, *(s + 1)};

        if (!n)
            return -1;

        char *end;

        errno = 0;
        *buf++ = strtoul(nibble, &end, 16);

        if (errno || *end)
            return -1;

        n--;
    }

    return n ? -1 : 0;
}
