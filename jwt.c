#include "jwt.h"
#include "base64.h"
#include <dynstr.h>
#include <cjson/cJSON.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char jwt_header[] = "{\"alg\": \"HS256\", \"typ\": \"JWT\"}";

static char *get_payload(const char *const name)
{
    char *ret = NULL;
    struct dynstr d;

    dynstr_init(&d);

    if (dynstr_append(&d, "{\"name\": \"%s\"}", name))
    {
        fprintf(stderr, "%s: dynstr_append name failed\n", __func__);
        goto end;
    }
    else if (!(ret = base64_encode(d.str, d.len)))
    {
        fprintf(stderr, "%s: base64 failed\n", __func__);
        goto end;
    }

end:
    dynstr_free(&d);
    return ret;
}

static char *get_hmac(const void *const buf, const size_t n,
    const void *const key, const size_t keyn)
{
    unsigned char hmac[SHA256_DIGEST_LENGTH];
    const EVP_MD *const md = EVP_sha256();
    char *ret = NULL;

    if (!md)
    {
        fprintf(stderr, "%s: EVP_sha256 failed\n", __func__);
        return NULL;
    }
    else if (!HMAC(md, key, keyn, buf, n, hmac, NULL))
    {
        fprintf(stderr, "%s: HMAC failed\n", __func__);
        return NULL;
    }
    else if (!(ret = base64_encode(hmac, sizeof hmac)))
    {
        fprintf(stderr, "%s: base64 failed\n", __func__);
        return NULL;
    }

    return ret;
}

char *jwt_encode(const char *const name, const void *const key, const size_t n)
{
    char *ret = NULL;
    char *const header = base64_encode(jwt_header, strlen(jwt_header)),
        *const payload = get_payload(name),
        *hmac = NULL;
    struct dynstr jwt;

    dynstr_init(&jwt);

    if (!header)
    {
        fprintf(stderr, "%s: base64_encode header failed\n", __func__);
        goto end;
    }
    else if (!payload)
    {
        fprintf(stderr, "%s: get_payload failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&jwt, "%s.%s", header, payload))
    {
        fprintf(stderr, "%s: dynstr_append header+payload failed", __func__);
        goto end;
    }
    else if (!(hmac = get_hmac(jwt.str, jwt.len, key, n)))
    {
        fprintf(stderr, "%s: get_hmac failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&jwt, ".%s", hmac))
    {
        fprintf(stderr, "%s: dynstr_append hmac failed\n", __func__);
        goto end;
    }

    ret = jwt.str;

end:
    free(header);
    free(payload);
    free(hmac);

    if (!ret)
        dynstr_free(&jwt);

    return ret;
}

int jwt_check(const char *const jwt, const void *const key, const size_t n)
{
    const char *const p = strrchr(jwt, '.');
    const EVP_MD *const md = EVP_sha256();
    unsigned char hmac[SHA256_DIGEST_LENGTH];
    char *dhmac = NULL;
    size_t hmaclen;

    if (!md)
    {
        fprintf(stderr, "%s: EVP_sha256 failed\n", __func__);
        return -1;
    }
    else if (!p)
    {
        fprintf(stderr, "%s: expected '.'\n", __func__);
        return 1;
    }
    else if (!HMAC(md, key, n, (const unsigned char *)jwt, p - jwt, hmac, NULL))
    {
        fprintf(stderr, "%s: HMAC failed\n", __func__);
        return -1;
    }
    else if (!(dhmac = base64_decode(p + 1, &hmaclen)))
    {
        fprintf(stderr, "%s: base64_decode failed\n", __func__);
        return -1;
    }

    const int r = memcmp(dhmac, hmac, hmaclen);

    free(dhmac);
    return !!r;
}
