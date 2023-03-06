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
    DIR *const d = opendir(dirpath);
    struct dirent *de;

    if (!d)
    {
        fprintf(stderr, "%s: opendir(2): %s\n", __func__, strerror(errno));
        return -1;
    }

    while ((de = readdir(d)))
    {
        const char *const path = de->d_name;

        if (!strcmp(path, ".") || !strcmp(path, ".."))
            continue;

        struct stat sb;
        struct dynstr d;

        dynstr_init(&d);

        if (dynstr_append(&d, "%s/%s", dirpath, path))
        {
            fprintf(stderr, "%s: dynstr_append failed\n", __func__);
            return -1;
        }

        const int r = stat(d.str, &sb);
        int ret = -1;

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
            return ret;
    }

    return 0;
}