#include "base64.h"
#include <openssl/evp.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void remove_lf(char *b64)
{
    while ((b64 = strchr(b64, '\n')))
        memcpy(b64, b64 + 1, strlen(b64));
}

static size_t base64len(const size_t n)
{
    /* Read EVP_EncodeInit(3) for further reference. */
    return ((n / 48) * 65) + (n % 48 ? 1 + ((n / 3) + 1) * 4 : 0);
}

static size_t decodedlen(const size_t n)
{
    return ((n / 64) * 48) + (n % 64 ? (n / 4) * 3 : 0);
}

char *base64_encode(const void *const buf, const size_t n)
{
    EVP_ENCODE_CTX *const ctx = EVP_ENCODE_CTX_new();
    char *ret = NULL;
    unsigned char *b64 = NULL;

    if (!ctx)
    {
        fprintf(stderr, "%s: EVP_ENCODE_CTX_new failed\n", __func__);
        goto end;
    }

    const size_t b64len = base64len(n);

    if (!(b64 = malloc(b64len + 1)))
    {
        fprintf(stderr, "%s: malloc(3): %s\n", __func__, strerror(errno));
        goto end;
    }

    EVP_EncodeInit(ctx);

    size_t rem = n, done = 0;
    int outl = b64len;

    while (rem)
    {
        const size_t i = n - rem, inl = rem > 48 ? 48 : rem;
        const unsigned char *const in = buf;

        if (!EVP_EncodeUpdate(ctx, &b64[done], &outl, &in[i], inl))
        {
            fprintf(stderr, "%s: EVP_EncodeUpdate failed\n", __func__);
            goto end;
        }

        done += outl;
        rem -= inl;
    }

    EVP_EncodeFinal(ctx, b64, &outl);
    ret = (char *)b64;
    remove_lf(ret);

end:
    if (!ret)
        free(b64);

    EVP_ENCODE_CTX_free(ctx);
    return ret;
}

void *base64_decode(const char *const b64, size_t *const n)
{
    void *ret = NULL;
    const size_t len = strlen(b64), dlen = decodedlen(len);
    EVP_ENCODE_CTX *const ctx = EVP_ENCODE_CTX_new();
    unsigned char *const buf = malloc(dlen);

    if (!buf)
    {
        fprintf(stderr, "%s: malloc(3): %s\n", __func__, strerror(errno));
        goto end;
    }
    else if (!ctx)
    {
        fprintf(stderr, "%s: EVP_ENCODE_CTX_new failed\n", __func__);
        goto end;
    }

    EVP_DecodeInit(ctx);

    size_t rem = len, done = 0;
    int outl = dlen;

    while (rem)
    {
        const size_t i = len - rem, inl = rem > 64 ? 64 : rem;
        const unsigned char *const in = (const unsigned char *)b64;

        if (EVP_DecodeUpdate(ctx, &buf[done], &outl, &in[i], inl) < 0)
        {
            fprintf(stderr, "%s: EVP_EncodeUpdate failed\n", __func__);
            goto end;
        }

        done += outl;
        rem -= inl;
    }

    EVP_DecodeFinal(ctx, buf, &outl);
    *n = done;
    ret = buf;

end:
    if (!ret)
        free(buf);

    EVP_ENCODE_CTX_free(ctx);
    return ret;
}
