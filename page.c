#include "page.h"
#include "http.h"
#include "html.h"
#include <dynstr.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PROJECT_TITLE "<title>slcl, a suckless cloud</title>"
#define DOCTYPE_TAG "<!DOCTYPE html>\n"
#define PROJECT_NAME "slcl"
#define PROJECT_URL "https://gitea.privatedns.org/Xavi92/" PROJECT_NAME
#define LOGIN_HEAD \
    "   <meta charset=\"UTF-8\">\n" \
    "   <meta name=\"viewport\"\n" \
    "       content=\"width=device-width, initial-scale=1,\n" \
    "           maximum-scale=1\">\n" \
        PROJECT_TITLE "\n"
#define STYLE_A "<link href=\"/style.css\" rel=\"stylesheet\">"
#define LOGIN_BODY \
    "<header>\n" \
    "   <a href=\"" PROJECT_URL "\">" PROJECT_NAME "</a>, a suckless cloud\n" \
    "</header>\n" \
    "   <form action=\"/login\" method=\"post\">\n" \
    "       <label for=\"username\">Username:</label>\n" \
    "       <input type=\"text\" class=\"form-control\"\n" \
    "           id=\"username\" name=\"username\" autofocus><br>\n" \
    "       <label for=\"username\">Password:</label>\n" \
    "       <input type=\"password\" class=\"form-control\" \n" \
    "           id=\"password\" name=\"password\"><br>\n" \
    "       <input type=\"submit\" value=\"Submit\">\n" \
    "   </form>\n"

static void free_response(void *const arg)
{
    free(arg);
}

static int prepare_name(struct html_node *const n, struct stat *const sb,
    const char *const dir, const char *const name)
{
    int ret = -1;
    struct html_node *a;
    struct dynstr d, dname;
    const char *const sep = S_ISDIR(sb->st_mode) ? "/" : "";

    dynstr_init(&d);
    dynstr_init(&dname);

    if (dynstr_append(&d, "%s%s%s", dir, name, sep))
    {
        fprintf(stderr, "%s: dynstr_append [1] failed\n", __func__);
        goto end;
    }
    else if (!(a = html_node_add_child(n, "a")))
    {
        fprintf(stderr, "%s: html_node_add_child failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(a, "href", d.str))
    {
        fprintf(stderr, "%s: html_node_add_attr href failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&dname, "%s%s", name, sep))
    {
        fprintf(stderr, "%s: dynstr_append [2] failed\n", __func__);
        goto end;
    }
    else if (html_node_set_value(a, dname.str))
    {
        fprintf(stderr, "%s: html_node_set_value failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    dynstr_free(&d);
    dynstr_free(&dname);
    return ret;
}

static int prepare_size(struct html_node *const n, const struct stat *const sb)
{
    if (!S_ISREG(sb->st_mode))
        return 0;

    float sz;
    size_t suffix_i;

    for (sz = sb->st_size, suffix_i = 0; (off_t)sz / 1024;
        suffix_i++, sz /= 1024.0f)
        ;

    static const char *const suffixes[] = {"B", "KiB", "MiB", "GiB", "TiB"};

    char buf[sizeof "18446744073709551615.0 XiB"];
    const int r = suffix_i ?
        snprintf(buf, sizeof buf, "%.1f %s", sz, suffixes[suffix_i])
        : snprintf(buf, sizeof buf, "%ju %s",
            (uintmax_t)sb->st_size, suffixes[suffix_i]);

    if (r >= sizeof buf || r < 0)
    {
        fprintf(stderr, "%s: snprintf(3) failed with %d\n", __func__, r);
        return -1;
    }
    else if (html_node_set_value(n, buf))
    {
        fprintf(stderr, "%s: html_node_set_value failed\n", __func__);
        return -1;
    }

    return 0;
}

static int prepare_date(struct html_node *const n, const struct stat *const sb)
{
    struct tm tm;

    if (!localtime_r(&sb->st_mtime, &tm))
    {
        fprintf(stderr, "%s: localtime_r(3): %s\n", __func__, strerror(errno));
        return -1;
    }

    char date[sizeof "0000-00-00T00:00:00+0000"];

    if (!strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S%z", &tm))
    {
        fprintf(stderr, "%s: strftime(3) failed\n", __func__);
        return -1;
    }
    else if (html_node_set_value(n, date))
    {
        fprintf(stderr, "%s: html_node_set_value failed\n", __func__);
        return -1;
    }

    return 0;
}

static int add_element(struct html_node *const n, const char *const dir,
    const char *const res, const char *const name)
{
    int ret = -1;
    enum {NAME, SIZE, DATE, COLUMNS};
    struct html_node *tr, *td[COLUMNS];
    struct dynstr path;
    const char *const sep = res[strlen(res) - 1] != '/' ? "/" : "";

    dynstr_init(&path);

    if (dynstr_append(&path, "%s%s%s", res, sep, name))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto end;
    }
    else if (!(tr = html_node_add_child(n, "tr")))
    {
        fprintf(stderr, "%s: html_node_add_child tr failed\n", __func__);
        goto end;
    }

    for (size_t i = 0; i < sizeof td / sizeof *td; i++)
        if (!(td[i] = html_node_add_child(tr, "td")))
        {
            fprintf(stderr, "%s: html_node_add_child td[%zu] failed\n",
                __func__, i);
            goto end;
        }

    struct stat sb;

    if (stat(path.str, &sb))
    {
        fprintf(stderr, "%s: stat(2) %s: %s\n",
            __func__, path.str, strerror(errno));
        goto end;
    }
    else if (prepare_name(td[NAME], &sb, dir, name))
    {
        fprintf(stderr, "%s: prepare_name failed\n", __func__);
        goto end;
    }
    else if (prepare_size(td[SIZE], &sb))
    {
        fprintf(stderr, "%s: prepare_size failed\n", __func__);
        goto end;
    }
    else if (prepare_date(td[DATE], &sb))
    {
        fprintf(stderr, "%s: prepare_date failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    dynstr_free(&path);
    return ret;
}

static int prepare_upload_form(struct html_node *const n, const char *const dir)
{
    struct html_node *div, *hidden, *form, *submit, *input;

    if (!(div = html_node_add_child(n, "div")))
    {
        fprintf(stderr, "%s: html_node_add_child div failed\n", __func__);
        return -1;
    }
    else if (!(form = html_node_add_child(div, "form")))
    {
        fprintf(stderr, "%s: html_node_add_child form failed\n", __func__);
        return -1;
    }
    else if (!(hidden = html_node_add_child(form, "input")))
    {
        fprintf(stderr, "%s: html_node_add_child hidden failed\n", __func__);
        return -1;
    }
    else if (!(input = html_node_add_child(form, "input")))
    {
        fprintf(stderr, "%s: html_node_add_child input failed\n", __func__);
        return -1;
    }
    else if (!(submit = html_node_add_child(form, "input")))
    {
        fprintf(stderr, "%s: html_node_add_child submit failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(form, "method", "post"))
    {
        fprintf(stderr, "%s: html_node_add_attr method failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(form, "action", "/upload"))
    {
        fprintf(stderr, "%s: html_node_add_attr method failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(form, "enctype", "multipart/form-data"))
    {
        fprintf(stderr, "%s: html_node_add_attr enctype failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(hidden, "type", "hidden"))
    {
        fprintf(stderr, "%s: html_node_add_attr hidden failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(hidden, "name", "dir"))
    {
        fprintf(stderr, "%s: html_node_add_attr dir failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(hidden, "value", dir))
    {
        fprintf(stderr, "%s: html_node_add_attr hidden value failed\n",
            __func__);
        return -1;
    }
    else if (html_node_add_attr(submit, "type", "submit"))
    {
        fprintf(stderr, "%s: html_node_add_attr submit failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(submit, "value", "Upload file"))
    {
        fprintf(stderr, "%s: html_node_add_attr value failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(input, "type", "file"))
    {
        fprintf(stderr, "%s: html_node_add_attr file failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(input, "name", "file"))
    {
        fprintf(stderr, "%s: html_node_add_attr name failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(input, "multiple", NULL))
    {
        fprintf(stderr, "%s: html_node_add_attr multiple failed\n", __func__);
        return -1;
    }

    return 0;
}

static int prepare_mkdir_form(struct html_node *const n, const char *const dir)
{
    struct html_node *div, *form, *hidden, *submit, *input;

    if (!(div = html_node_add_child(n, "div")))
    {
        fprintf(stderr, "%s: html_node_add_child div failed\n", __func__);
        return -1;
    }
    else if (!(form = html_node_add_child(div, "form")))
    {
        fprintf(stderr, "%s: html_node_add_child form failed\n", __func__);
        return -1;
    }
    else if (!(input = html_node_add_child(form, "input")))
    {
        fprintf(stderr, "%s: html_node_add_child input failed\n", __func__);
        return -1;
    }
    else if (!(hidden = html_node_add_child(form, "input")))
    {
        fprintf(stderr, "%s: html_node_add_child hidden failed\n", __func__);
        return -1;
    }
    else if (!(submit = html_node_add_child(form, "input")))
    {
        fprintf(stderr, "%s: html_node_add_child submit failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(form, "method", "post"))
    {
        fprintf(stderr, "%s: html_node_add_attr method failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(form, "action", "/mkdir"))
    {
        fprintf(stderr, "%s: html_node_add_attr method failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(hidden, "type", "hidden"))
    {
        fprintf(stderr, "%s: html_node_add_attr hidden failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(hidden, "name", "dir"))
    {
        fprintf(stderr, "%s: html_node_add_attr dir failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(hidden, "value", dir))
    {
        fprintf(stderr, "%s: html_node_add_attr hidden value failed\n",
            __func__);
        return -1;
    }
    else if (html_node_add_attr(submit, "type", "submit"))
    {
        fprintf(stderr, "%s: html_node_add_attr submit failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(submit, "value", "Create directory"))
    {
        fprintf(stderr, "%s: html_node_add_attr submit value failed\n",
            __func__);
        return -1;
    }
    else if (html_node_add_attr(input, "type", "text"))
    {
        fprintf(stderr, "%s: html_node_add_attr text failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(input, "name", "name"))
    {
        fprintf(stderr, "%s: html_node_add_attr name failed\n", __func__);
        return -1;
    }

    return 0;
}

static int prepare_logout_form(struct html_node *const n)
{
    struct html_node *div, *form, *input;

    if (!(div = html_node_add_child(n, "div")))
    {
        fprintf(stderr, "%s: html_node_add_child div failed\n", __func__);
        return -1;
    }
    else if (!(form = html_node_add_child(div, "form")))
    {
        fprintf(stderr, "%s: html_node_add_child form failed\n", __func__);
        return -1;
    }
    else if (!(input = html_node_add_child(form, "input")))
    {
        fprintf(stderr, "%s: html_node_add_child input failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(form, "method", "post"))
    {
        fprintf(stderr, "%s: html_node_add_attr method failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(form, "action", "/logout"))
    {
        fprintf(stderr, "%s: html_node_add_attr action failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(input, "type", "submit"))
    {
        fprintf(stderr, "%s: html_node_add_attr type failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(input, "value", "Logout"))
    {
        fprintf(stderr, "%s: html_node_add_attr value failed\n", __func__);
        return -1;
    }

    return 0;
}

static int prepare_footer(struct html_node *const n)
{
    int ret = -1;
    struct html_node *footer, *const a = html_node_alloc("a");
    struct dynstr d;

    dynstr_init(&d);

    if (!a)
    {
        fprintf(stderr, "%s: html_node_alloc failed\n", __func__);
        goto end;
    }
    else if (!(footer = html_node_add_child(n, "footer")))
    {
        fprintf(stderr, "%s: html_node_add_child footer failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(a, "href", PROJECT_URL))
    {
        fprintf(stderr, "%s: html_node_add_attr failed\n", __func__);
        goto end;
    }
    else if (html_node_set_value(a, PROJECT_NAME))
    {
        fprintf(stderr, "%s: html_node_set_value a failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&d, "Powered by "))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto end;
    }
    else if (html_serialize(a, &d))
    {
        fprintf(stderr, "%s: html_serialize failed\n", __func__);
        goto end;
    }
    else if (html_node_set_value_unescaped(footer, d.str))
    {
        fprintf(stderr, "%s: html_node_add_child a failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    dynstr_free(&d);
    html_node_free(a);
    return ret;
}

static int list_dir(struct http_response *const r, const char *const dir,
    const char *const root, const char *const res)
{
    int ret = -1;
    DIR *const d = opendir(res);
    struct html_node *const html = html_node_alloc("html"),
        *head, *title, *body, *charset, *viewport, *table;
    struct dynstr out, t;
    const char *const fdir = dir + strlen("/user");

    dynstr_init(&out);
    dynstr_init(&t);

    if (!d)
    {
        fprintf(stderr, "%s: opendir(2): %s\n", __func__, strerror(errno));
        goto end;
    }
    else if (!html)
    {
        fprintf(stderr, "%s: html_node_alloc_failed\n", __func__);
        goto end;
    }
    else if (!(head = html_node_add_child(html, "head")))
    {
        fprintf(stderr, "%s: html_node_add_child head failed\n", __func__);
        goto end;
    }
    else if (!(body = html_node_add_child(html, "body")))
    {
        fprintf(stderr, "%s: html_node_add_child body failed\n", __func__);
        goto end;
    }
    else if (!(table = html_node_add_child(body, "table")))
    {
        fprintf(stderr, "%s: html_node_add_child table failed\n", __func__);
        goto end;
    }
    else if (!(title = html_node_add_child(head, "title")))
    {
        fprintf(stderr, "%s: html_node_add_child title failed\n", __func__);
        goto end;
    }
    else if (!(charset = html_node_add_child(head, "meta")))
    {
        fprintf(stderr, "%s: html_node_add_child charset failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(charset, "charset", "UTF-8"))
    {
        fprintf(stderr, "%s: html_node_add_attr charset failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&t, PROJECT_NAME " - %s", fdir))
    {
        fprintf(stderr, "%s: dynstr_append title failed\n", __func__);
        goto end;
    }
    else if (html_node_set_value(title, t.str))
    {
        fprintf(stderr, "%s: html_node_set_value title failed\n", __func__);
        goto end;
    }
    else if (!(viewport = html_node_add_child(head, "meta")))
    {
        fprintf(stderr, "%s: html_node_add_child viewport failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(viewport, "name", "viewport"))
    {
        fprintf(stderr, "%s: html_node_add_attr name viewport failed\n",
            __func__);
        goto end;
    }
    else if (html_node_add_attr(viewport, "content",
        "width=device-width, initial-scale=1, maximum-scale=1"))
    {
        fprintf(stderr, "%s: html_node_add_attr name viewport failed\n",
            __func__);
        goto end;
    }
    else if (prepare_upload_form(body, fdir))
    {
        fprintf(stderr, "%s: prepare_upload_form failed\n", __func__);
        goto end;
    }
    else if (prepare_mkdir_form(body, fdir))
    {
        fprintf(stderr, "%s: prepare_upload_form failed\n", __func__);
        goto end;
    }
    else if (prepare_logout_form(body))
    {
        fprintf(stderr, "%s: prepare_logout_form failed\n", __func__);
        goto end;
    }
    else if (prepare_footer(body))
    {
        fprintf(stderr, "%s: prepare_footer failed\n", __func__);
        goto end;
    }

    struct dirent *de;

    while ((de = readdir(d)))
    {
        const char *const name = de->d_name;

        if (!strcmp(name, ".")
            || (!strcmp(name, "..") && !strcmp(root, res)))
            continue;
        else if (add_element(table, dir, res, name))
        {
            fprintf(stderr, "%s: add_element failed\n", __func__);
            goto end;
        }
    }

    if (dynstr_append(&out, DOCTYPE_TAG))
    {
        fprintf(stderr, "%s: dynstr_prepend failed\n", __func__);
        goto end;
    }
    else if (html_serialize(html, &out))
    {
        fprintf(stderr, "%s: html_serialize failed\n", __func__);
        goto end;
    }

    *r = (const struct http_response)
    {
        .status = HTTP_STATUS_OK,
        .buf.rw = out.str,
        .n = out.len,
        .free = free_response
    };

    if (http_response_add_header(r, "Content-Type", "text/html"))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    html_node_free(html);
    dynstr_free(&t);

    if (ret)
        dynstr_free(&out);

    if (closedir(d))
    {
        fprintf(stderr, "%s: closedir(2): %s\n", __func__, strerror(errno));
        return -1;
    }

    return ret;
}

static int serve_file(struct http_response *const r,
    const struct stat *const sb, const char *const res)
{
    FILE *const f = fopen(res, "rb");

    if (!f)
    {
        fprintf(stderr, "%s: fopen(3): %s\n", __func__, strerror(errno));
        return -1;
    }

    *r = (const struct http_response)
    {
        .status = HTTP_STATUS_OK,
        .f = f,
        .n = sb->st_size
    };

    return 0;
}

int page_resource(struct http_response *const r, const char *const dir,
    const char *const root, const char *const res)
{
    struct stat sb;

    if (stat(res, &sb))
    {
        fprintf(stderr, "%s: stat(2) %s: %s\n",
            __func__, res, strerror(errno));

        if (errno == ENOENT)
        {
            *r = (const struct http_response)
            {
                .status = HTTP_STATUS_NOT_FOUND
            };

            return 0;
        }
        else
            return -1;
    }

    const mode_t m = sb.st_mode;

    if (S_ISDIR(m))
        return list_dir(r, dir, root, res);
    else if (S_ISREG(m))
        return serve_file(r, &sb, res);

    fprintf(stderr, "%s: unexpected st_mode %jd\n", __func__, (intmax_t)m);
    return -1;
}

int page_failed_login(struct http_response *const r)
{
    static const char index[] =
        DOCTYPE_TAG
        "<html>\n"
        "   <head>\n"
        "       " LOGIN_HEAD "\n"
        "       " STYLE_A "\n"
        "   </head>\n"
        "   <p>Invalid username or password.</p>\n"
            LOGIN_BODY
        "</html>\n";

    *r = (const struct http_response)
    {
        .status = HTTP_STATUS_UNAUTHORIZED,
        .buf.ro = index,
        .n = sizeof index - 1
    };

    if (http_response_add_header(r, "Content-Type", "text/html"))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        return -1;
    }

    return 0;
}

int page_login(struct http_response *const r)
{
    static const char index[] =
        DOCTYPE_TAG
        "<html>\n"
        "   <head>\n"
        "       " STYLE_A "\n"
        "       " LOGIN_HEAD "\n"
        "   </head>\n"
        "   " LOGIN_BODY "\n"
        "</html>\n";

    *r = (const struct http_response)
    {
        .status = HTTP_STATUS_OK,
        .buf.ro = index,
        .n = sizeof index - 1
    };

    if (http_response_add_header(r, "Content-Type", "text/html"))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        return -1;
    }

    return 0;
}

int page_forbidden(struct http_response *const r)
{
    static const char body[] =
        DOCTYPE_TAG
        "<html>\n"
            "Forbidden\n"
        "</html>";

    *r = (const struct http_response)
    {
        .status = HTTP_STATUS_FORBIDDEN,
        .buf.ro = body,
        .n = sizeof body - 1
    };

    if (http_response_add_header(r, "Content-Type", "text/html"))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        return -1;
    }

    return 0;
}

int page_bad_request(struct http_response *const r)
{
    static const char body[] =
        DOCTYPE_TAG
        "<html>\n"
        "   <head>"
        "   " PROJECT_TITLE "\n"
        "   </head>"
            "Invalid request\n"
        "</html>";

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

    return 0;
}

int page_style(struct http_response *const r)
{
    static const char body[] =
    "body"
    "{\n"
    "   display: flex;\n"
    "   flex-direction: column;\n"
    "}\n"
    ".form-control\n"
    "{\n"
    "    display: block;\n"
    "    border: 1px solid;\n"
    "    width: 60%;\n"
    "    border-radius: 8px;\n"
    "    align-content: center;\n"
    "}\n";

    *r = (const struct http_response)
    {
        .status = HTTP_STATUS_OK,
        .buf.ro = body,
        .n = sizeof body - 1
    };

    if (http_response_add_header(r, "Content-Type", "text/css"))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        return -1;
    }

    return 0;
}
