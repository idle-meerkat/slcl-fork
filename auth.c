#include "auth.h"
#include "http.h"
#include "jwt.h"
#include <cjson/cJSON.h>
#include <dynstr.h>
#include <openssl/sha.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

struct auth
{
    struct dynstr dir, db;
};

enum {KEYLEN = 32};

static char *dump_db(const char *const path)
{
    char *ret = NULL;
    FILE *f = NULL;
    struct stat sb;

    if (stat(path, &sb))
    {
        fprintf(stderr, "%s: stat(2): %s\n", __func__, strerror(errno));
        goto end;
    }
    else if (sb.st_size > SIZE_MAX)
    {
        fprintf(stderr, "%s: %s too big (%llu bytes, %zu max)\n",
            __func__, path, (unsigned long long)sb.st_size, (size_t)SIZE_MAX);
        goto end;
    }
    else if (!(f = fopen(path, "rb")))
    {
        fprintf(stderr, "%s: fopen(3): %s\n", __func__, strerror(errno));
        goto end;
    }
    else if (!(ret = malloc(sb.st_size + 1)))
    {
        fprintf(stderr, "%s: malloc(3): %s\n", __func__, strerror(errno));
        goto end;
    }
    else if (!fread(ret, sb.st_size, 1, f))
    {
        fprintf(stderr, "%s: failed to dump %zu bytes, ferror=%d\n",
            __func__, (size_t)sb.st_size, ferror(f));
        goto end;
    }

    ret[sb.st_size] = '\0';

end:

    if (f && fclose(f))
    {
        fprintf(stderr, "%s: fclose(3): %s\n", __func__, strerror(errno));
        free(ret);
        return NULL;
    }

    return ret;
}

static int decode_hex(const char *const hex, unsigned char *buf, size_t n)
{
    for (const char *s = hex; *s; s += 2)
    {
        const char nibble[sizeof "00"] = {*s, *(s + 1)};

        if (!n)
            return -1;

        *buf++ = strtoul(nibble, NULL, 16);
        n--;
    }

    return n ? -1 : 0;
}

static int find_cookie(const cJSON *const users, const char *const cookie)
{
    const cJSON *u;

    cJSON_ArrayForEach(u, users)
    {
        const cJSON *const n = cJSON_GetObjectItem(u, "name"),
            *const k = cJSON_GetObjectItem(u, "key");
        const char *name, *key;
        unsigned char dkey[KEYLEN];

        if (!n || !(name = cJSON_GetStringValue(n)))
        {
            fprintf(stderr, "%s: missing username\n", __func__);
            return -1;
        }
        else if (!k || !(key = cJSON_GetStringValue(k)))
        {
            fprintf(stderr, "%s: missing key\n", __func__);
            return -1;
        }
        else if (decode_hex(key, dkey, sizeof dkey))
        {
            fprintf(stderr, "%s: decode_hex failed\n", __func__);
            return -1;
        }

        const int res = jwt_check(cookie, dkey, sizeof dkey);

        if (!res)
            return 0;
        if (res < 0)
        {
            fprintf(stderr, "%s: jwt_decode failed\n", __func__);
            return -1;
        }
    }

    return 1;
}

int auth_cookie(const struct auth *const a, const struct http_cookie *const c)
{
    int ret = -1;
    char *db = NULL;
    cJSON *json = NULL;

    if (!c->field || !c->value)
        return 1;
    else if (!(db = dump_db(a->db.str)))
    {
        fprintf(stderr, "%s: dump_db failed\n", __func__);
        goto end;
    }
    else if (!(json = cJSON_Parse(db)))
    {
        fprintf(stderr, "%s: cJSON_Parse failed\n", __func__);
        goto end;
    }

    const cJSON *const users = cJSON_GetObjectItem(json, "users");

    if (!users)
    {
        fprintf(stderr, "%s: could not find users\n", __func__);
        goto end;
    }
    else if (!cJSON_IsArray(users))
    {
        fprintf(stderr, "%s: expected JSON array for users\n", __func__);
        goto end;
    }
    else if ((ret = find_cookie(users, c->value)) < 0)
    {
        fprintf(stderr, "%s: find_cookie failed\n", __func__);
        goto end;
    }

end:
    free(db);
    cJSON_Delete(json);
    return ret;
}

static int generate_cookie(const cJSON *const json, const char *const path,
    const char *const name, const char *const key, char **const cookie)
{
    unsigned char dkey[KEYLEN];
    int ret = -1;
    char *jwt = NULL;

    if (decode_hex(key, dkey, sizeof dkey))
    {
        fprintf(stderr, "%s: decode_hex failed\n", __func__);
        goto end;
    }
    else if (!(jwt = jwt_encode(name, dkey, sizeof dkey)))
    {
        fprintf(stderr, "%s: jwt_encode failed\n", __func__);
        goto end;
    }
    else if (!(*cookie = http_cookie_create(name, jwt)))
    {
        fprintf(stderr, "%s: http_cookie_create failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    free(jwt);
    return ret;
}

static int compare_pwd(const char *const salt, const char *const password,
    const char *const exp_password)
{
    int ret = -1;
    enum {SALT_LEN = SHA256_DIGEST_LENGTH};
    unsigned char dec_salt[SALT_LEN], sha256[SHA256_DIGEST_LENGTH];
    const size_t slen = strlen(salt),
        len = strlen(password), n = sizeof dec_salt + len;
    unsigned char *const salted = malloc(n);

    if (slen != SALT_LEN * 2)
    {
        fprintf(stderr, "%s: unexpected salt length: %zu\n", __func__, slen);
        goto end;
    }
    else if (!salted)
    {
        fprintf(stderr, "%s: malloc(3): %s\n", __func__, strerror(errno));
        goto end;
    }
    else if (decode_hex(salt, dec_salt, sizeof dec_salt))
    {
        fprintf(stderr, "%s: decode_hex failed\n", __func__);
        goto end;
    }

    memcpy(salted, dec_salt, sizeof dec_salt);
    memcpy(salted + sizeof dec_salt, password, len);

    if (!SHA256(salted, n, sha256))
    {
        fprintf(stderr, "%s: SHA256 (first round) failed\n", __func__);
        goto end;
    }

    enum {ROUNDS = 1000 - 1};

    for (int i = 0; i < ROUNDS; i++)
        if (!SHA256(sha256, sizeof sha256, sha256))
        {
            fprintf(stderr, "%s: SHA256 failed\n", __func__);
            goto end;
        }

    char sha256_str[sizeof
        "00000000000000000000000000000000"
        "00000000000000000000000000000000"];

    for (struct {char *p; size_t i;} a = {.p = sha256_str};
        a.i < sizeof sha256 / sizeof *sha256; a.i++, a.p += 2)
        sprintf(a.p, "%02x", sha256[a.i]);

    if (!strcmp(sha256_str, exp_password))
        ret = 0;
    else
        /* Positive error code for password mismatch. */
        ret = 1;

end:
    free(salted);
    return ret;
}

int auth_login(const struct auth *const a, const char *const user,
    const char *const password, char **const cookie)
{
    int ret = -1;
    const char *const path = a->db.str;
    char *const db = dump_db(path);
    cJSON *json = NULL;

    if (!db)
    {
        fprintf(stderr, "%s: dump_db failed\n", __func__);
        goto end;
    }
    else if (!(json = cJSON_Parse(db)))
    {
        fprintf(stderr, "%s: cJSON_Parse failed\n", __func__);
        goto end;
    }

    const cJSON *const users = cJSON_GetObjectItem(json, "users");

    if (!users)
    {
        fprintf(stderr, "%s: could not find users\n", __func__);
        goto end;
    }
    else if (!cJSON_IsArray(users))
    {
        fprintf(stderr, "%s: expected JSON array for users\n", __func__);
        goto end;
    }

    cJSON *u;

    cJSON_ArrayForEach(u, users)
    {
        const cJSON *const n = cJSON_GetObjectItem(u, "name"),
            *const s = cJSON_GetObjectItem(u, "salt"),
            *const p = cJSON_GetObjectItem(u, "password"),
            *const k = cJSON_GetObjectItem(u, "key");
        const char *name, *salt, *pwd, *key;

        if (!n || !(name = cJSON_GetStringValue(n)))
        {
            fprintf(stderr, "%s: missing username\n", __func__);
            goto end;
        }
        else if (!s || !(salt = cJSON_GetStringValue(s)))
        {
            fprintf(stderr, "%s: missing salt\n", __func__);
            goto end;
        }
        else if (!p || !(pwd = cJSON_GetStringValue(p)))
        {
            fprintf(stderr, "%s: missing password\n", __func__);
            goto end;
        }
        else if (!k || !(key = cJSON_GetStringValue(k)))
        {
            fprintf(stderr, "%s: missing key\n", __func__);
            goto end;
        }
        else if (!strcmp(name, user))
        {
            const int res = compare_pwd(salt, password, pwd);

            if (res < 0)
            {
                fprintf(stderr, "%s: generate_cookie failed\n", __func__);
                goto end;
            }
            else if (!res)
            {
                if (generate_cookie(json, path, name, key, cookie))
                {
                    fprintf(stderr, "%s: generate_cookie failed\n", __func__);
                    goto end;
                }

                ret = 0;
                goto end;
            }
        }
    }

    ret = 1;

end:
    free(db);
    cJSON_Delete(json);
    return ret;
}

void auth_free(struct auth *const a)
{
    if (a)
    {
        dynstr_free(&a->dir);
        dynstr_free(&a->db);
    }

    free(a);
}

const char *auth_dir(const struct auth *const a)
{
    return a->dir.str;
}

int auth_quota(const struct auth *const a, const char *const user,
    bool *const available, unsigned long long *const quota)
{
    int ret = -1;
    const char *const path = a->db.str;
    char *const db = dump_db(path);
    cJSON *json = NULL;

    if (!db)
    {
        fprintf(stderr, "%s: dump_db failed\n", __func__);
        goto end;
    }
    else if (!(json = cJSON_Parse(db)))
    {
        fprintf(stderr, "%s: cJSON_Parse failed\n", __func__);
        goto end;
    }

    const cJSON *const users = cJSON_GetObjectItem(json, "users");

    if (!users)
    {
        fprintf(stderr, "%s: could not find users\n", __func__);
        goto end;
    }
    else if (!cJSON_IsArray(users))
    {
        fprintf(stderr, "%s: expected JSON array for users\n", __func__);
        goto end;
    }

    *available = false;

    const cJSON *u;

    cJSON_ArrayForEach(u, users)
    {
        const cJSON *const n = cJSON_GetObjectItem(u, "name"),
            *const q = cJSON_GetObjectItem(u, "quota");
        const char *name;

        if (!n || !(name = cJSON_GetStringValue(n)))
        {
            fprintf(stderr, "%s: missing username\n", __func__);
            goto end;
        }
        else if (!strcmp(name, user))
        {
            const char *qs;

            if (!q || !(qs = cJSON_GetStringValue(q)) || !*qs)
            {
                /* Unlimited quota. */
                ret = 0;
                goto end;
            }

            char *end;

            errno = 0;
            *available = true;
            *quota = strtoull(qs, &end, 10);

            const unsigned long long mul = 1024 * 1024;

            if (errno || *end != '\0')
            {
                fprintf(stderr, "%s: invalid quota %s: %s\n",
                    __func__, qs, strerror(errno));
                goto end;
            }
            else if (*quota >= ULLONG_MAX / mul)
            {
                fprintf(stderr, "%s: quota %s too large\n", __func__, qs);
                goto end;
            }

            *quota *= 1024 * 1024;
            break;
        }
    }

    ret = 0;

end:
    free(db);
    cJSON_Delete(json);
    return ret;
}

static int create_db(const char *const path)
{
    int ret = -1;
    const int fd = open(path, O_WRONLY | O_CREAT, 0600);

    if (fd < 0)
    {
        fprintf(stderr, "%s: open(2): %s\n", __func__, strerror(errno));
        goto end;
    }

    static const char db[] = "{\"users\": []}\n";

    for (struct {size_t n; const void *p;}
        a = {.n = strlen(db), .p = db}; a.n;)
    {
        const ssize_t n = write(fd, a.p, a.n);

        if (n < 0)
        {
            fprintf(stderr, "%s: write(2): %s\n", __func__, strerror(errno));
            goto end;
        }

        a.n -= n;
        a.p = ((const char *)a.p) + n;
    }

    printf("Created login database at %s\n", path);
    ret = 0;

end:

    if (fd >= 0 && close(fd))
    {
        fprintf(stderr, "%s: close(2): %s\n", __func__, strerror(errno));
        return -1;
    }

    return ret;
}

static int init_db(struct auth *const a)
{
    struct stat sb;
    const char *const path = a->db.str;

    if (stat(path, &sb))
    {
        if (errno == ENOENT)
            return create_db(path);
        else
        {
            fprintf(stderr, "%s: stat(2): %s\n", __func__, strerror(errno));
            return -1;
        }
    }

    return 0;
}

struct auth *auth_alloc(const char *const dir)
{
    struct auth *const a = malloc(sizeof *a);

    if (!a)
    {
        fprintf(stderr, "%s: malloc(3) auth: %s\n", __func__, strerror(errno));
        goto failure;
    }

    *a = (const struct auth){0};

    dynstr_init(&a->db);
    dynstr_init(&a->dir);

    if (dynstr_append(&a->dir, "%s", dir))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto failure;
    }
    else if (dynstr_append(&a->db, "%s/db.json", dir))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto failure;
    }
    else if (init_db(a))
    {
        fprintf(stderr, "%s: init_db failed\n", __func__);
        goto failure;
    }

    return a;

failure:
    auth_free(a);
    return NULL;
}
