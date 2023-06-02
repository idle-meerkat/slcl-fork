#define _POSIX_C_SOURCE 200809L

#include "page.h"
#include "http.h"
#include "html.h"
#include <dynstr.h>
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define PROJECT_NAME "slcl"
#define PROJECT_TITLE PROJECT_NAME ", a suckless cloud"
#define PROJECT_TAG "<title>" PROJECT_TITLE "</title>"
#define DOCTYPE_TAG "<!DOCTYPE html>\n"
#define PROJECT_URL "https://gitea.privatedns.org/Xavi92/" PROJECT_NAME
#define COMMON_HEAD \
    "   <meta charset=\"UTF-8\">\n" \
    "   <meta name=\"viewport\"\n" \
    "       content=\"width=device-width, initial-scale=1,\n" \
    "           maximum-scale=1\">\n" \
        PROJECT_TAG "\n"
#define STYLE_A "<link href=\"/style.css\" rel=\"stylesheet\">"
#define LOGIN_BODY \
    "<header>\n" \
    "   <a href=\"" PROJECT_URL "\">" PROJECT_NAME "</a>, a suckless cloud\n" \
    "</header>\n" \
    "   <form class=\"loginform\" action=\"/login\" method=\"post\">\n" \
    "       <label for=\"username\">Username:</label>\n" \
    "       <input type=\"text\" class=\"form-control\"\n" \
    "           id=\"username\" name=\"username\" autofocus><br>\n" \
    "       <label for=\"username\">Password:</label>\n" \
    "       <input type=\"password\" class=\"form-control\" \n" \
    "           id=\"password\" name=\"password\"><br>\n" \
    "       <input type=\"submit\" value=\"Submit\">\n" \
    "   </form>\n"
#define MAXSIZEFMT "18446744073709551615.0 XiB"

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

static int size_units(const unsigned long long b, char *const buf,
    const size_t n)
{
    static const char *const suffixes[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    float sz;
    size_t suffix_i;

    for (sz = b, suffix_i = 0; (unsigned long long)sz / 1024;
        suffix_i++, sz /= 1024.0f)
        ;

    if (suffix_i >= sizeof suffixes / sizeof *suffixes)
    {
        fprintf(stderr, "%s: maximum suffix exceeded\n", __func__);
        return -1;
    }

    const int r = suffix_i ?
        snprintf(buf, n, "%.1f %s", sz, suffixes[suffix_i])
        : snprintf(buf, n, "%llu %s", b, suffixes[suffix_i]);

    if (r >= n || r < 0)
    {
        fprintf(stderr, "%s: snprintf(3) failed with %d\n", __func__, r);
        return -1;
    }

    return 0;
}

static int prepare_size(struct html_node *const n, const struct stat *const sb)
{
    char buf[sizeof MAXSIZEFMT] = "";

    if (S_ISREG(sb->st_mode)
        && size_units(sb->st_size, buf, sizeof buf))
    {
        fprintf(stderr, "%s: size_units failed\n", __func__);
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

static int prepare_share(struct html_node *const n,
    const struct stat *const sb, const char *const dir, const char *const name)
{
    int ret = -1;
    const char *const fdir = dir + strlen("/user");
    struct html_node *form, *file, *submit;
    struct dynstr d;

    dynstr_init(&d);

    if (!S_ISREG(sb->st_mode))
        return 0;

    if (!(form = html_node_add_child(n, "form")))
    {
        fprintf(stderr, "%s: html_node_add_child form failed\n", __func__);
        goto end;
    }
    else if (!(file = html_node_add_child(form, "input")))
    {
        fprintf(stderr, "%s: html_node_add_child file failed\n", __func__);
        goto end;
    }
    else if (!(submit = html_node_add_child(form, "input")))
    {
        fprintf(stderr, "%s: html_node_add_child file failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(form, "method", "post"))
    {
        fprintf(stderr, "%s: html_node_add_attr method failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(form, "action", "/share"))
    {
        fprintf(stderr, "%s: html_node_add_attr action failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(file, "type", "hidden"))
    {
        fprintf(stderr, "%s: html_node_add_attr file type failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(file, "name", "name"))
    {
        fprintf(stderr, "%s: html_node_add_attr file name failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&d, "%s%s", fdir, name))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(file, "value", d.str))
    {
        fprintf(stderr, "%s: html_node_add_attr file value failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(submit, "type", "submit"))
    {
        fprintf(stderr, "%s: html_node_add_attr submit type failed\n",
            __func__);
        goto end;
    }
    else if (html_node_add_attr(submit, "value", "Share"))
    {
        fprintf(stderr, "%s: html_node_add_attr submit value failed\n",
            __func__);
        goto end;
    }

    ret = 0;

end:
    dynstr_free(&d);
    return ret;
}

static int prepare_preview(struct html_node *const n,
    const struct stat *const sb, const char *const dir, const char *const name)
{
    int ret = -1;
    struct html_node *a;
    struct dynstr d;

    dynstr_init(&d);

    if (!S_ISREG(sb->st_mode))
        return 0;
    else if (!(a = html_node_add_child(n, "a")))
    {
        fprintf(stderr, "%s: html_node_add_child form failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&d, "%s%s?preview=1", dir, name))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(a, "href", d.str))
    {
        fprintf(stderr, "%s: html_node_add_attr failed\n", __func__);
        goto end;
    }
    else if (html_node_set_value(a, "Preview"))
    {
        fprintf(stderr, "%s: html_node_set_value value failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    dynstr_free(&d);
    return ret;
}

static int add_element(struct html_node *const n, const char *const dir,
    const char *const res, const char *const name)
{
    int ret = -1;
    enum {NAME, SIZE, DATE, SHARE, PREVIEW, COLUMNS};
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
    else if (prepare_share(td[SHARE], &sb, dir, name))
    {
        fprintf(stderr, "%s: prepare_date failed\n", __func__);
        goto end;
    }
    else if (prepare_preview(td[PREVIEW], &sb, dir, name))
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
    else if (html_node_add_attr(div, "class", "userform"))
    {
        fprintf(stderr, "%s: html_node_add_attr div failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(form, "method", "post"))
    {
        fprintf(stderr, "%s: html_node_add_attr method failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(form, "action", "/upload"))
    {
        fprintf(stderr, "%s: html_node_add_attr action failed\n", __func__);
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
    else if (html_node_add_attr(div, "class", "userform"))
    {
        fprintf(stderr, "%s: html_node_add_attr div failed\n", __func__);
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

static int prepare_quota_form(struct html_node *const n,
    const struct page_quota *const q)
{
    int ret = -1;
    struct html_node *progress = NULL, *div, *label;
    char cur[sizeof MAXSIZEFMT], max[sizeof cur],
        cur_nu[sizeof "18446744073709551615"], max_nu[sizeof cur_nu];
    int res = snprintf(cur_nu, sizeof cur_nu, "%llu", q->cur);

    struct dynstr d, pd;

    dynstr_init(&d);
    dynstr_init(&pd);

    if (res < 0 || res >= sizeof cur_nu)
    {
        fprintf(stderr, "%s: snprintf(3) cur_nu failed with %d\n",
            __func__, res);
        goto end;
    }

    res = snprintf(max_nu, sizeof max_nu, "%llu", q->max);

    if (res < 0 || res >= sizeof max_nu)
    {
        fprintf(stderr, "%s: snprintf(3) max_nu failed with %d\n",
            __func__, res);
        goto end;
    }
    else if (size_units(q->cur, cur, sizeof cur))
    {
        fprintf(stderr, "%s: size_units cur failed\n", __func__);
        goto end;
    }
    else if (size_units(q->max, max, sizeof max))
    {
        fprintf(stderr, "%s: size_units max failed\n", __func__);
        goto end;
    }
    else if (!(div = html_node_add_child(n, "div")))
    {
        fprintf(stderr, "%s: html_node_add_child div failed\n", __func__);
        goto end;
    }
    else if (!(label = html_node_add_child(div, "label")))
    {
        fprintf(stderr, "%s: html_node_add_child label failed\n", __func__);
        goto end;
    }
    else if (!(progress = html_node_alloc("progress")))
    {
        fprintf(stderr, "%s: html_node_alloc progress failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(div, "class", "userform"))
    {
        fprintf(stderr, "%s: html_node_add_attr div failed\n", __func__);
        return -1;
    }
    else if (html_node_add_attr(progress, "value", cur_nu))
    {
        fprintf(stderr, "%s: html_node_add_attr value failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(progress, "max", max_nu))
    {
        fprintf(stderr, "%s: html_node_add_attr max failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&pd, "%s/%s", cur, max))
    {
        fprintf(stderr, "%s: dynstr_append pd failed\n", __func__);
        goto end;
    }
    else if (html_node_set_value(progress, pd.str))
    {
        fprintf(stderr, "%s: html_node_set_value progress failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&d, "User quota: "))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto end;
    }
    else if (html_serialize(progress, &d))
    {
        fprintf(stderr, "%s: html_serialize failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&d, "%s", pd.str))
    {
        fprintf(stderr, "%s: dynstr_append failed\n", __func__);
        goto end;
    }
    else if (html_node_set_value_unescaped(label, d.str))
    {
        fprintf(stderr, "%s: html_node_set_value_unescaped label failed\n",
            __func__);
        goto end;
    }

    ret = 0;

end:
    html_node_free(progress);
    dynstr_free(&d);
    dynstr_free(&pd);
    return ret;
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
    else if (html_node_add_attr(div, "class", "userform"))
    {
        fprintf(stderr, "%s: html_node_add_attr div failed\n", __func__);
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

static int common_head(struct html_node *const head, const char *const tl)
{
    int ret = -1;
    struct html_node *charset, *title, *viewport, *link;
    struct dynstr t;

    dynstr_init(&t);

    if (!(title = html_node_add_child(head, "title")))
    {
        fprintf(stderr, "%s: html_node_add_child title failed\n", __func__);
        goto end;
    }
    else if (!(charset = html_node_add_child(head, "meta")))
    {
        fprintf(stderr, "%s: html_node_add_child charset failed\n", __func__);
        goto end;
    }
    else if (!(link = html_node_add_child(head, "link")))
    {
        fprintf(stderr, "%s: html_node_add_child link failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(link, "href", "/style.css"))
    {
        fprintf(stderr, "%s: html_node_add_attr href failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(link, "rel", "stylesheet"))
    {
        fprintf(stderr, "%s: html_node_add_attr rel failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(charset, "charset", "UTF-8"))
    {
        fprintf(stderr, "%s: html_node_add_attr charset failed\n", __func__);
        goto end;
    }
    else if (tl && dynstr_append(&t, PROJECT_NAME " - %s", tl))
    {
        fprintf(stderr, "%s: dynstr_append title failed\n", __func__);
        goto end;
    }

    const char *const value = tl ? t.str : PROJECT_TITLE;

    if (html_node_set_value(title, value))
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

    ret = 0;

end:
    dynstr_free(&t);
    return ret;
}

static struct html_node *resource_layout(const char *const dir,
    const struct page_quota *const q, struct html_node **const table)
{
    const char *const fdir = dir + strlen("/user");
    struct html_node *const html = html_node_alloc("html"),
        *ret = NULL, *head, *body, *div;

    if (!html)
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
    else if (!(div = html_node_add_child(body, "div")))
    {
        fprintf(stderr, "%s: html_node_add_child div failed\n", __func__);
        goto end;
    }
    else if (!(*table = html_node_add_child(div, "table")))
    {
        fprintf(stderr, "%s: html_node_add_child table failed\n", __func__);
        goto end;
    }
    else if (common_head(head, fdir))
    {
        fprintf(stderr, "%s: common_head failed\n", __func__);
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
    else if (q && prepare_quota_form(body, q))
    {
        fprintf(stderr, "%s: prepare_quota_form failed\n", __func__);
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

    ret = html;

end:

    if (!ret)
        html_node_free(html);

    return ret;
}

static int add_elements(const char *const root, const char *const res,
    const char *const dir, struct html_node *const table)
{
    int ret = -1;
    struct dirent **pde = NULL;
    const int n = scandir(res, &pde, NULL, alphasort);

    if (n < 0)
    {
        fprintf(stderr, "%s: scandir(3): %s\n", __func__, strerror(errno));
        goto end;
    }

    for (int i = 0; i < n; i++)
    {
        const struct dirent *const de = pde[i];
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

    ret = 0;

end:

    for (int i = 0; i < n; i++)
        free(pde[i]);

    free(pde);
    return ret;
}

static int list_dir(const struct page_resource *const pr)
{
    int ret = -1;
    struct dynstr out;
    struct html_node *table,
        *const html = resource_layout(pr->dir, pr->q, &table);

    dynstr_init(&out);

    if (!html)
    {
        fprintf(stderr, "%s: resource_layout failed\n", __func__);
        goto end;
    }
    else if (add_elements(pr->root, pr->res, pr->dir, table))
    {
        fprintf(stderr, "%s: read_elements failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&out, DOCTYPE_TAG))
    {
        fprintf(stderr, "%s: dynstr_prepend failed\n", __func__);
        goto end;
    }
    else if (html_serialize(html, &out))
    {
        fprintf(stderr, "%s: html_serialize failed\n", __func__);
        goto end;
    }

    *pr->r = (const struct http_response)
    {
        .status = HTTP_STATUS_OK,
        .buf.rw = out.str,
        .n = out.len,
        .free = free
    };

    if (http_response_add_header(pr->r, "Content-Type", "text/html"))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    html_node_free(html);

    if (ret)
        dynstr_free(&out);

    return ret;
}

static int serve_file(struct http_response *const r,
    const struct stat *const sb, const char *const res, const bool preview)
{
    int ret = -1;
    FILE *const f = fopen(res, "rb");
    struct dynstr b, d;
    char *bn;

    dynstr_init(&b);
    dynstr_init(&d);

    if (!f)
    {
        fprintf(stderr, "%s: fopen(3): %s\n", __func__, strerror(errno));
        goto end;
    }
    else if (dynstr_append(&b, "%s", res))
    {
        fprintf(stderr, "%s: dynstr_append res failed\n", __func__);
        goto end;
    }
    else if (!(bn = basename(b.str)))
    {
        fprintf(stderr, "%s: basename(3) failed\n", __func__);
        goto end;
    }
    else if (preview)
    {
        if (dynstr_append(&d, "inline"))
        {
            fprintf(stderr, "%s: dynstr_append inline failed\n", __func__);
            goto end;
        }
    }
    else if (dynstr_append(&d, "attachment; filename=\"%s\"", bn))
    {
        fprintf(stderr, "%s: dynstr_append attachment failed\n", __func__);
        goto end;
    }

    *r = (const struct http_response)
    {
        .status = HTTP_STATUS_OK,
        .f = f,
        .n = sb->st_size
    };

    if (http_response_add_header(r, "Content-Disposition", d.str))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    dynstr_free(&b);
    dynstr_free(&d);

    if (ret && f && fclose(f))
        fprintf(stderr, "%s: fclose(3): %s\n", __func__, strerror(errno));

    return ret;
}

static int page_not_found(struct http_response *const r)
{
    static const char body[] =
        DOCTYPE_TAG
        "<html>\n"
        "   <head>"
        "   " PROJECT_TAG "\n"
        "   </head>"
            "File or directory not found\n"
        "</html>";

    *r = (const struct http_response)
    {
        .status = HTTP_STATUS_NOT_FOUND,
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

static bool preview(const struct page_resource *const pr)
{
    for (size_t i = 0; i < pr->n_args; i++)
    {
        const struct http_arg *const a = &pr->args[i];

        if (!strcmp(a->key, "preview")
            && (!strcmp(a->value, "1")
                || !strcasecmp(a->value, "true")))
            return true;
    }

    return false;
}

int page_resource(const struct page_resource *const pr)
{
    struct stat sb;

    if (stat(pr->res, &sb))
    {
        fprintf(stderr, "%s: stat(2) %s: %s\n",
            __func__, pr->res, strerror(errno));

        if (errno == ENOENT)
            return page_not_found(pr->r);
        else
            return -1;
    }

    const mode_t m = sb.st_mode;

    if (S_ISDIR(m))
        return list_dir(pr);
    else if (S_ISREG(m))
        return serve_file(pr->r, &sb, pr->res, preview(pr));

    fprintf(stderr, "%s: unexpected st_mode %jd\n", __func__, (intmax_t)m);
    return -1;
}

static char *resolve_link(const char *const res)
{
    size_t len = 1;
    char *p = NULL;

    for (;;)
    {
        char *const pp = realloc(p, len);

        if (!pp)
        {
            fprintf(stderr, "%s: realloc(3): %s\n", __func__, strerror(errno));
            break;
        }

        p = pp;

        const ssize_t n = readlink(res, p, len);

        if (n < 0)
        {
            fprintf(stderr, "%s: readlink(2): %s\n", __func__, strerror(errno));
            break;
        }
        else if (n < len)
        {
            p[len - 1] = '\0';
            return p;
        }

        len++;
    }

    free(p);
    return NULL;
}

int page_public(struct http_response *const r, const char *const res)
{
    int ret = -1;
    struct stat sb;
    char *path = NULL;

    if (stat(res, &sb))
    {
        fprintf(stderr, "%s: stat(2) %s: %s\n",
            __func__, res, strerror(errno));

        if (errno == ENOENT)
            return page_not_found(r);
        else
            goto end;
    }

    const mode_t m = sb.st_mode;

    if (!S_ISREG(m))
    {
        fprintf(stderr, "%s: only regular files are supported\n", __func__);
        goto end;
    }
    else if (!(path = resolve_link(res)))
    {
        fprintf(stderr, "%s: resolve_link failed\n", __func__);
        goto end;
    }
    else if (serve_file(r, &sb, path, false))
    {
        fprintf(stderr, "%s: serve_file failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    free(path);
    return ret;
}

int page_failed_login(struct http_response *const r)
{
    static const char index[] =
        DOCTYPE_TAG
        "<html>\n"
        "   <head>\n"
        "       " COMMON_HEAD "\n"
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
        "       " COMMON_HEAD "\n"
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
        "   <head>\n"
        "       " PROJECT_TAG "\n"
        "       " COMMON_HEAD "\n"
        "   </head>\n"
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
        "   " PROJECT_TAG "\n"
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
    "body, input\n"
    "{\n"
    "    font-family: 'Courier New', Courier, monospace;\n"
    "}\n"
    "td\n"
    "{\n"
    "    font-size: 14px;\n"
    "}\n"
    "a\n"
    "{\n"
    "    text-decoration: none;\n"
    "}\n"
    ".userform\n"
    "{\n"
    "    padding: 4px;\n"
    "}\n"
    ".loginform\n"
    "{\n"
    "    display: grid;\n"
    "}\n"
    "form, label, table, input\n"
    "{\n"
    "    margin: auto;\n"
    "}\n"
    "div\n"
    "{\n"
    "    align-items: center;\n"
    "    display: grid;\n"
    "}\n"
    "input\n"
    "{\n"
    "    margin:auto;\n"
    "    border: 1px solid;\n"
    "    border-radius: 8px;\n"
    "}\n"
    "header, footer\n"
    "{\n"
    "    display: flex;\n"
    "    justify-content: center;\n"
    "    text-decoration: auto;\n"
    "}\n"
    "table\n"
    "{\n"
    "    max-width: 50%;\n"
    "}\n"
    "tr:nth-child(even)\n"
    "{\n"
    "    background-color: lightgray;\n"
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

int page_share(struct http_response *const r, const char *const path)
{
    int ret = -1;
    struct dynstr out;
    struct html_node *const html = html_node_alloc("html"),
        *head, *a;

    dynstr_init(&out);

    if (!html)
    {
        fprintf(stderr, "%s: html_node_alloc failed\n", __func__);
        goto end;
    }
    else if (!(head = html_node_add_child(html, "head")))
    {
        fprintf(stderr, "%s: html_node_add_child head failed\n", __func__);
        goto end;
    }
    else if (common_head(head, NULL))
    {
        fprintf(stderr, "%s: common_head failed\n", __func__);
        goto end;
    }
    else if (!(a = html_node_add_child(html, "a")))
    {
        fprintf(stderr, "%s: html_node_add_child failed\n", __func__);
        goto end;
    }
    else if (html_node_add_attr(a, "href", path))
    {
        fprintf(stderr, "%s: html_node_add_attr href failed\n", __func__);
        goto end;
    }
    else if (html_node_set_value(a, "Copy this link"))
    {
        fprintf(stderr, "%s: html_node_set_value href failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&out, DOCTYPE_TAG))
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
        .free = free
    };

    if (http_response_add_header(r, "Content-Type", "text/html"))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    html_node_free(html);

    if (ret)
        dynstr_free(&out);

    return ret;
}

int page_quota_exceeded(struct http_response *const r,
    const unsigned long long len, const unsigned long long quota)
{
    int ret = -1;
    struct dynstr msg, out;
    char q[sizeof MAXSIZEFMT], l[sizeof q];
    struct html_node *const html = html_node_alloc("html"),
        *head, *body;

    dynstr_init(&msg);
    dynstr_init(&out);

    if (!html)
    {
        fprintf(stderr, "%s: html_node_alloc failed\n", __func__);
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
    else if (common_head(head, NULL))
    {
        fprintf(stderr, "%s: common_head failed\n", __func__);
        goto end;
    }
    else if (size_units(quota, q, sizeof q))
    {
        fprintf(stderr, "%s: size_units quota failed\n", __func__);
        goto end;
    }
    else if (size_units(len, l, sizeof l))
    {
        fprintf(stderr, "%s: size_units len failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&msg, "Maximum quota exceeded: %s "
        "(requested size: %s)", q, l))
    {
        fprintf(stderr, "%s: dynstr_append msg failed\n", __func__);
        goto end;
    }
    else if (html_node_set_value(body, msg.str))
    {
        fprintf(stderr, "%s: html_node_set_value msg failed\n", __func__);
        goto end;
    }
    else if (dynstr_append(&out, DOCTYPE_TAG))
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
        .status = HTTP_STATUS_PAYLOAD_TOO_LARGE,
        .buf.rw = out.str,
        .n = out.len,
        .free = free
    };

    if (http_response_add_header(r, "Content-Type", "text/html"))
    {
        fprintf(stderr, "%s: http_response_add_header failed\n", __func__);
        goto end;
    }

    ret = 0;

end:
    html_node_free(html);
    dynstr_free(&msg);

    if (ret)
        dynstr_free(&out);

    return ret;
}
