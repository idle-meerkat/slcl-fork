#include "auth.h"
#include "cftw.h"
#include "handler.h"
#include "http.h"
#include "page.h"
#include <dynstr.h>
#include <libgen.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct form
{
    char *key, *value;
};

static int redirect(struct http_response *const r)
{
    *r = (const struct http_response)
    {
        .status = HTTP_STATUS_SEE_OTHER
    };

    if (http_response_add_header(r, "Location", "/user/"))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        return -1;
    }

    return 0;
}

static int serve_index(const struct http_payload *const p,
    struct http_response *const r, void *const user)
{
    struct auth *const a = user;

    if (auth_cookie(a, &p->cookie))
        return page_login(r);

    return redirect(r);
}

static int serve_style(const struct http_payload *const p,
    struct http_response *const r, void *const user)
{
    return page_style(r);
}

static char *alloc_form_data(const char *const s, const char **const end)
{
    const char *const next = strchr(s, '&');
    const size_t len = next ? next - s : strlen(s);
    char *const data = malloc(len + 1);

    if (!data)
    {
        fprintf(stderr, "%s: malloc(3): %s\n", __func__, strerror(errno));
        return NULL;
    }

    memcpy(data, s, len);
    data[len] = '\0';
    *end = s + len;

    if (next)
        *end += 1;

    return data;
}

static void form_free(struct form *const f)
{
    if (f)
    {
        free(f->key);
        free(f->value);
    }
}

static struct form *append_form(struct form *forms, const char **const s,
    size_t *const n)
{
    struct form *ret = NULL;
    const char *end;
    char *const data = alloc_form_data(*s, &end), *key = NULL, *value = NULL;
    struct form *f = NULL;

    if (!data)
    {
        fprintf(stderr, "%s: alloc_form_data failed\n", __func__);
        goto end;
    }
    else if (!(forms = realloc(forms, (*n + 1) * sizeof *forms)))
    {
        fprintf(stderr, "%s: realloc(3): %s\n", __func__, strerror(errno));
        goto end;
    }

    const char *const sep = strchr(data, '=');

    if (!sep)
    {
        fprintf(stderr, "%s: strchr(3) returned NULL\n", __func__);
        goto end;
    }
    else if (!data || !*(sep + 1))
    {
        fprintf(stderr, "%s: expected key=value (%s)\n", __func__, data);
        goto end;
    }

    f = &forms[(*n)++];

    const size_t keylen = sep - data;

    if (!(key = strndup(data, keylen)))
    {
        fprintf(stderr, "%s: strndup(3) key: %s\n", __func__, strerror(errno));
        goto end;
    }
    else if (!(value = strdup(sep + 1)))
    {
        fprintf(stderr, "%s: strdup(3) value: %s\n", __func__, strerror(errno));
        goto end;
    }

    *f = (const struct form)
    {
        .key = http_decode_url(key),
        .value = http_decode_url(value)
    };

    if (!f->key || !f->value)
    {
        fprintf(stderr, "%s: http_decode_url key/value failed\n", __func__);
        goto end;
    }

    *s = end;
    ret = forms;

end:
    free(key);
    free(value);
    free(data);

    if (!ret)
        form_free(f);

    return ret;
}

static struct form *get_forms(const struct http_payload *const pl,
    size_t *const outn)
{
    const struct http_post *const p = &pl->u.post;
    const char *const ref = p->data;
    char *dup = NULL;
    struct form *forms = NULL;

    if (!ref)
    {
        fprintf(stderr, "%s: expected non-NULL buffer\n", __func__);
        goto failure;
    }
    else if (!(dup = strndup(ref, p->n)))
    {
        fprintf(stderr, "%s: strndup(3): %s\n", __func__, strerror(errno));
        goto failure;
    }

    const char *s = dup;

    *outn = 0;

    while (*s)
    {
        struct form *const f = append_form(forms, &s, outn);

        if (!f)
        {
            fprintf(stderr, "%s: append_form failed\n", __func__);
            goto failure;
        }

        forms = f;
    }

    free(dup);
    return forms;

failure:
    free(dup);
    free(forms);
    return NULL;
}

static int check_credentials(struct auth *const a,
    const struct form *const forms, const size_t n, char **const cookie)
{
    const char *username = NULL, *pwd = NULL;

    *cookie = NULL;

    for (size_t i = 0; i < n; i++)
    {
        const struct form *const f = &forms[i];

        if (!strcmp(f->key, "username"))
            username = f->value;
        else if (!strcmp(f->key, "password"))
            pwd = f->value;
    }

    if (!username || !pwd)
    {
        fprintf(stderr, "%s: missing credentials\n", __func__);
        return 1;
    }

    const int ret = auth_login(a, username, pwd, cookie);

    if (ret < 0)
        fprintf(stderr, "%s: auth_login failed\n", __func__);

    return ret;
}

static int login(const struct http_payload *const pl,
    struct http_response *const r, void *const user)
{
    int res = -1;
    size_t n;
    struct form *const forms = get_forms(pl, &n);
    struct auth *const a = user;
    char *cookie = NULL;

    if (!forms)
    {
        fprintf(stderr, "%s: get_forms failed\n", __func__);
        goto end;
    }
    else if ((res = check_credentials(a, forms, n, &cookie)))
    {
        if (res < 0)
            fprintf(stderr, "%s: check_credentials failed\n", __func__);

        goto end;
    }
    else if (redirect(r))
    {
        fprintf(stderr, "%s: redirect failed\n", __func__);
        goto end;
    }
    else if (cookie && http_response_add_header(r, "Set-Cookie", cookie))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        goto end;
    }

    res = 0;

end:
    if (forms)
        for (size_t i = 0; i < n; i++)
        {
            free(forms[i].key);
            free(forms[i].value);
        }

    free(cookie);
    free(forms);

    if (res && page_failed_login(r))
    {
        fprintf(stderr, "%s: page_failed_login failed\n", __func__);
        return -1;
    }

    return 0;
}

static int logout(const struct http_payload *const p,
    struct http_response *const r, void *const user)
{
    struct auth *const a = user;
    const struct http_cookie *const c = &p->cookie;
    const int res = auth_cookie(a, c);

    if (res < 0)
    {
        fprintf(stderr, "%s: auth_cookie failed\n", __func__);
        return -1;
    }
    else if (res)
        return page_forbidden(r);

    int ret = -1;
    struct dynstr d;
    static const char date[] = "Thu, 1 Jan 1970 00:00:00 GMT";

    dynstr_init(&d);

    *r = (const struct http_response)
    {
        .status = HTTP_STATUS_SEE_OTHER
    };

    if (http_response_add_header(r, "Location", "/"))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        goto end;
    }
    else if (http_response_add_header(r, "Content-Type", "text/html"))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        goto end;
    }
    /* Force expired cookie so they are removed by the browser, too. */
    else if (dynstr_append(&d, "%s=%s; Expires=%s", c->field, c->value, date))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto end;
    }
    else if (http_response_add_header(r, "Set-Cookie", d.str))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    dynstr_free(&d);
    return ret;
}

static int search(const struct http_payload *const p,
    struct http_response *const r, void *const user)
{
    struct auth *const a = user;

    if (auth_cookie(a, &p->cookie))
    {
        fprintf(stderr, "%s: auth_cookie failed\n", __func__);
        return page_forbidden(r);
    }

    fprintf(stderr, "%s: TODO\n", __func__);
    return -1;
}


static int add_length(const char *const fpath, const struct stat *const sb,
    void *const user)
{
    unsigned long long *const l = user;

    *l += sb->st_size;
    return 0;
}

static int quota_current(const struct auth *const a,
    const char *const username, unsigned long long *const cur)
{
    int ret = -1;
    const char *const adir = auth_dir(a);
    struct dynstr d;

    dynstr_init(&d);

    if (!adir)
    {
        fprintf(stderr, "%s: auth_dir failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&d, "%s/user/%s", adir, username))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto end;
    }

    *cur = 0;

    if (cftw(d.str, add_length, cur))
    {
        fprintf(stderr, "%s: cftw: %s\n", __func__, strerror(errno));
        goto end;
    }

    ret = 0;

end:
    dynstr_free(&d);
    return ret;
}

static int check_quota(const struct auth *const a, const char *const username,
    const unsigned long long len, const unsigned long long quota)
{
    unsigned long long total;

    if (quota_current(a, username, &total))
    {
        fprintf(stderr, "%s: quota_current failed\n", __func__);
        return -1;
    }

    return total + len > quota ? 1 : 0;
}

static int check_length(const unsigned long long len,
    const struct http_cookie *const c, void *const user)
{
    struct auth *const a = user;
    const char *const username = c->field;
    bool has_quota;
    unsigned long long quota;

    if (auth_quota(a, username, &has_quota, &quota))
    {
        fprintf(stderr, "%s: auth_quota failed\n", __func__);
        return -1;
    }
    else if (has_quota)
        return check_quota(a, username, len, quota);

    return 0;
}

static bool path_isrel(const char *const path)
{
    if (!strcmp(path, "..") || !strcmp(path, ".") || strstr(path, "/../"))
        return true;

    static const char suffix[] = "/..";
    const size_t n = strlen(path), sn = strlen(suffix);

    if (n >= sn && !strcmp(path + n - sn, suffix))
        return true;

    return false;
}

static char *cust_dirname(char *const d)
{
    /*
        path       dirname   cust_dirname
        /usr/lib   /usr      /usr/
        /usr/      /         /usr/
        usr        .         .
        /          /         /
        .          .         .
        ..         .         .
    */

    if (!strcmp(d, "."))
        return d;

    char *const s = strrchr(d, '/');

    if (!s)
        return ".";

    *(s + 1) = '\0';
    return d;
}

static int getnode(const struct http_payload *const p,
    struct http_response *const r, void *const user)
{
    struct auth *const a = user;

    if (auth_cookie(a, &p->cookie))
    {
        fprintf(stderr, "%s: auth_cookie failed\n", __func__);
        return page_forbidden(r);
    }

    const char *const username = p->cookie.field,
        *const resource = p->resource + strlen("/user/");

    if (path_isrel(resource))
    {
        fprintf(stderr, "%s: illegal relative path %s\n", __func__, resource);
        return page_forbidden(r);
    }

    int ret = -1;
    struct dynstr root, d;
    char *const dird = strdup(p->resource), *dir = NULL;
    const char *const adir = auth_dir(a);

    dynstr_init(&d);
    dynstr_init(&root);

    if (!adir)
    {
        fprintf(stderr, "%s: auth_dir failed\n", __func__);
        goto end;
    }
    else if (!dird)
    {
        fprintf(stderr, "%s: strdup(3) failed: %s\n",
            __func__, strerror(errno));
        goto end;
    }
    else if (!(dir = cust_dirname(dird)))
    {
        fprintf(stderr, "%s: dirname(3) failed: %s\n",
            __func__, strerror(errno));
        goto end;
    }
    else if (dynstr_append(&root, "%s/user/%s/", adir, username)
        || dynstr_append(&d, "%s%s", root.str, resource))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto end;
    }

    bool available;
    unsigned long long cur, max;

    if (auth_quota(a, username, &available, &max))
    {
        fprintf(stderr, "%s: quota_available failed\n", __func__);
        goto end;
    }
    else if (available && quota_current(a, username, &cur))
    {
        fprintf(stderr, "%s: quota_current failed\n", __func__);
        goto end;
    }

    const struct page_quota pq =
    {
        .cur = cur,
        .max = max
    }, *const ppq = available ? &pq : NULL;

    ret = page_resource(r, dir, root.str, d.str, ppq);

end:
    dynstr_free(&d);
    dynstr_free(&root);
    free(dird);
    return ret;
}

static int move_file(const char *const old, const char *const new)
{
    int ret = -1;
    FILE *const f = fopen(old, "rb");
    const int fd = open(new, O_WRONLY | O_CREAT, 0600);
    struct stat sb;

    if (!f)
    {
        fprintf(stderr, "%s: fopen(3): %s\n", __func__, strerror(errno));
        goto end;
    }
    else if (fd < 0)
    {
        fprintf(stderr, "%s: open(2): %s\n", __func__, strerror(errno));
        goto end;
    }
    else if (stat(old, &sb))
    {
        fprintf(stderr, "%s: stat(2): %s\n", __func__, strerror(errno));
        goto end;
    }

    for (off_t i = 0; i < sb.st_size;)
    {
        char buf[1024];
        const off_t left = sb.st_size - i;
        const size_t rem = left > sizeof buf ? sizeof buf : left;
        ssize_t w;

        if (!fread(buf, rem, 1, f))
        {
            fprintf(stderr, "%s: fread(3) failed, feof=%d, ferror=%d\n",
                __func__, feof(f), ferror(f));
            goto end;
        }
        else if ((w = write(fd, buf, rem)) < 0)
        {
            fprintf(stderr, "%s: write(2): %s\n", __func__, strerror(errno));
            goto end;
        }
        else if (w != rem)
        {
            fprintf(stderr, "%s: write(2): expected to write %zu bytes, "
                "only %ju written\n", __func__, rem, (intmax_t)w);
            goto end;
        }

        i += rem;
    }

    ret = 0;

end:
    if (fd >= 0 && close(fd))
    {
        fprintf(stderr, "%s: close(2): %s\n", __func__, strerror(errno));
        ret = -1;
    }

    if (f && fclose(f))
    {
        fprintf(stderr, "%s: fclose(3): %s\n", __func__, strerror(errno));
        ret = -1;
    }

    return ret;
}

static int rename_or_move(const char *const old, const char *const new)
{
    const int res = rename(old, new);

    if (res && errno == EXDEV)
        return move_file(old, new);
    else if (res)
        fprintf(stderr, "%s: rename(3): %s\n", __func__, strerror(errno));

    return res;
}

static int upload_file(const struct http_post_file *const f,
    const char *const user, const char *const root, const char *const dir)
{
    int ret = -1;
    struct dynstr d;

    dynstr_init(&d);

    if (!root)
    {
        fprintf(stderr, "%s: auth_dir failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&d, "%s/user/%s/%s%s", root, user, dir, f->filename))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto end;
    }
    else if (rename_or_move(f->tmpname, d.str))
    {
        fprintf(stderr, "%s: rename_or_move failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    dynstr_free(&d);
    return ret;
}

static int redirect_to_dir(const char *const dir,
    struct http_response *const r)
{
    int ret = -1;
    struct dynstr d;
    char *const encdir = http_encode_url(dir);

    dynstr_init(&d);

    *r = (const struct http_response)
    {
        .status = HTTP_STATUS_SEE_OTHER
    };

    if (!encdir)
    {
        fprintf(stderr, "%s: http_encode_url failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&d, "/user%s", encdir))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto end;
    }
    else if (http_response_add_header(r, "Location", d.str))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    free(encdir);
    dynstr_free(&d);
    return ret;
}

static int upload_files(const struct http_payload *const p,
    struct http_response *const r, const struct auth *const a)
{
    const struct http_post *const po = &p->u.post;
    const char *const root = auth_dir(a), *const user = p->cookie.field,
        *const dir = po->dir;

    if (!po->files)
    {
        fprintf(stderr, "%s: expected file list\n", __func__);
        return 1;
    }
    else if (!root)
    {
        fprintf(stderr, "%s: auth_dir failed\n", __func__);
        return -1;
    }
    else if (!dir)
    {
        static const char body[] = "<html>No target directory set</html>";

        *r = (const struct http_response)
        {
            .status = HTTP_STATUS_BAD_REQUEST,
            .buf.ro = body,
            .n = strlen(body)
        };

        if (http_response_add_header(r, "Content-Type", "text/html"))
        {
            fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
            return -1;
        }

        return 0;
    }

    for (size_t i = 0; i < po->n; i++)
    {
        if (upload_file(&po->files[i], user, root, po->dir))
        {
            fprintf(stderr, "%s: upload_file failed\n", __func__);
            return -1;
        }
    }

    return redirect_to_dir(dir, r);
}

static int upload(const struct http_payload *const p,
    struct http_response *const r, void *const user)
{
    const struct auth *const a = user;

    if (auth_cookie(a, &p->cookie))
    {
        fprintf(stderr, "%s: auth_cookie failed\n", __func__);
        return page_forbidden(r);
    }
    else if (p->u.post.expect_continue)
    {
        *r = (const struct http_response)
        {
            .status = HTTP_STATUS_CONTINUE
        };

        return 0;
    }

    return upload_files(p, r, a);
}

static int createdir(const struct http_payload *const p,
    struct http_response *const r, void *const user)
{
    int ret = -1;
    struct auth *const a = user;
    struct dynstr d, userd;
    struct form *forms = NULL;

    dynstr_init(&d);
    dynstr_init(&userd);

    if (auth_cookie(a, &p->cookie))
    {
        fprintf(stderr, "%s: auth_cookie failed\n", __func__);
        ret = page_forbidden(r);
        goto end;
    }

    size_t n;

    if (!(forms = get_forms(p, &n)))
    {
        fprintf(stderr, "%s: get_forms failed\n", __func__);
        ret = page_bad_request(r);
        goto end;
    }
    else if (n != 2)
    {
        fprintf(stderr, "%s: expected 2 forms, got %zu\n", __func__, n);
        ret = page_bad_request(r);
        goto end;
    }

    char *name = NULL, *dir = NULL;

    for (size_t i = 0; i < n; i++)
    {
        const struct form *const f = &forms[i];

        if (!strcmp(f->key, "name"))
            name = f->value;
        else if (!strcmp(f->key, "dir"))
            dir = f->value;
        else
        {
            fprintf(stderr, "%s: unexpected key %s\n", __func__, f->key);
            ret = page_bad_request(r);
            goto end;
        }
    }

    if (!name || !dir)
    {
        fprintf(stderr, "%s: missing name or directory\n", __func__);
        ret = page_bad_request(r);
        goto end;
    }
    else if (path_isrel(name) || strpbrk(name, "/*"))
    {
        fprintf(stderr, "%s: invalid directory name %s\n", __func__, dir);
        ret = page_bad_request(r);
        goto end;
    }
    else if (path_isrel(dir) || strchr(dir, '*'))
    {
        fprintf(stderr, "%s: invalid name %s\n", __func__, name);
        ret = page_bad_request(r);
        goto end;
    }

    /* HTML input forms use '+' for whitespace, rather than %20. */
    {
        char *c = name, *d = dir;

        while ((c = strchr(c, '+')))
            *c = ' ';

        while ((d = strchr(d, '+')))
            *d = ' ';
    }

    const char *const root = auth_dir(a);

    if (!root)
    {
        fprintf(stderr, "%s: auth_dir failed\n", __func__);
        return -1;
    }
    else if (dynstr_append(&d, "%s/user/%s/%s%s",
        root, p->cookie.field, dir, name))
    {
        fprintf(stderr, "%s: dynstr_append d failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&userd, "/user%s%s/", dir, name))
    {
        fprintf(stderr, "%s: dynstr_append userd failed\n", __func__);
        goto end;
    }
    else if (mkdir(d.str, 0700))
    {
        if (errno != EEXIST)
        {
            fprintf(stderr, "%s: mkdir(2): %s\n", __func__, strerror(errno));
            goto end;
        }
        else
        {
            static const char body[] = "<html>Directory already exists</html>";

            *r = (const struct http_response)
            {
                .status = HTTP_STATUS_BAD_REQUEST,
                .buf.ro = body,
                .n = sizeof body - 1
            };

            if (http_response_add_header(r, "Content-Type", "text/html"))
            {
                fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
                return -1;
            }
        }
    }
    else
    {
        *r = (const struct http_response)
        {
            .status = HTTP_STATUS_SEE_OTHER
        };

        if (http_response_add_header(r, "Location", userd.str))
        {
            fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
            goto end;
        }
    }

    ret = 0;

end:
    if (forms)
        for (size_t i = 0; i < n; i++)
            form_free(&forms[i]);

    free(forms);
    dynstr_free(&userd);
    dynstr_free(&d);
    return ret;
}

static void usage(char *const argv[])
{
    fprintf(stderr, "%s [-t tmpdir] [-p port] dir\n", *argv);
}

static int parse_args(const int argc, char *const argv[],
    const char **const dir, unsigned short *const port,
    const char **const tmpdir)
{
    const char *const envtmp = getenv("TMPDIR");
    int opt;

    /* Default values. */
    *port = 0;
    *tmpdir = envtmp ? envtmp : "/tmp";

    while ((opt = getopt(argc, argv, "t:p:")) != -1)
    {
        switch (opt)
        {
            case 't':
                *tmpdir = optarg;
                break;

            case 'p':
            {
                const unsigned long portul = strtoul(optarg, NULL, 10);

                if (portul > UINT16_MAX)
                {
                    fprintf(stderr, "%s: invalid port %lu\n", __func__, portul);
                    return -1;
                }

                *port = portul;
            }
                break;

            default:
                usage(argv);
                return -1;
        }
    }

    if (optind >= argc)
    {
        usage(argv);
        return -1;
    }

    *dir = argv[optind];
    return 0;
}

int main(const int argc, char *const argv[])
{
    int ret = EXIT_FAILURE;
    struct handler *h = NULL;
    struct auth *a = NULL;
    const char *dir, *tmpdir;
    unsigned short port;

    if (parse_args(argc, argv, &dir, &port, &tmpdir)
        || !(a = auth_alloc(dir)))
        goto end;

    const struct handler_cfg cfg =
    {
        .length = check_length,
        .tmpdir = tmpdir,
        .user = a
    };

    if (!(h = handler_alloc(&cfg))
        || handler_add(h, "/", HTTP_OP_GET, serve_index, a)
        || handler_add(h, "/index.html", HTTP_OP_GET, serve_index, a)
        || handler_add(h, "/style.css", HTTP_OP_GET, serve_style, NULL)
        || handler_add(h, "/user/*", HTTP_OP_GET, getnode, a)
        || handler_add(h, "/login", HTTP_OP_POST, login, a)
        || handler_add(h, "/logout", HTTP_OP_POST, logout, a)
        || handler_add(h, "/search", HTTP_OP_POST, search, a)
        || handler_add(h, "/upload", HTTP_OP_POST, upload, a)
        || handler_add(h, "/mkdir", HTTP_OP_POST, createdir, a)
        || handler_listen(h, port))
        goto end;

    ret = EXIT_SUCCESS;

end:
    auth_free(a);
    handler_free(h);
    return ret;
}
