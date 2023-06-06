#define _POSIX_C_SOURCE 200809L

#include "cftw.h"
#include <dynstr.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int cftw(const char *const dirpath, int (*const fn)(const char *,
    const struct stat *, void *), void *const user)
{
    int ret = -1;
    DIR *const d = opendir(dirpath);
    struct dirent *de;

    if (!d)
    {
        fprintf(stderr, "%s: opendir(2): %s\n", __func__, strerror(errno));
        goto end;
    }

    while ((de = readdir(d)))
    {
        const char *const path = de->d_name;

        if (!strcmp(path, ".") || !strcmp(path, ".."))
            continue;

        const char *const sep = dirpath[strlen(dirpath) - 1] == '/' ? "" : "/";
        struct stat sb;
        struct dynstr d;

        dynstr_init(&d);

        if (dynstr_append(&d, "%s%s%s", dirpath, sep, path))
        {
            fprintf(stderr, "%s: dynstr_append failed\n", __func__);
            return -1;
        }

        const int r = stat(d.str, &sb);

        if (r)
            fprintf(stderr, "%s: stat(2) %s: %s\n",
                __func__, path, strerror(errno));
        else if (S_ISDIR(sb.st_mode))
            ret = cftw(d.str, fn, user);
        else if (S_ISREG(sb.st_mode))
            ret = fn(d.str, &sb, user);
        else
            fprintf(stderr, "%s: unexpected st_mode %ju\n",
                __func__, (uintmax_t)sb.st_mode);

        dynstr_free(&d);

        if (ret)
            goto end;
    }

    ret = 0;

end:

    if (d && closedir(d))
    {
        fprintf(stderr, "%s: closedir(2): %s\n", __func__, strerror(errno));
        ret = -1;
    }

    return ret;
}
