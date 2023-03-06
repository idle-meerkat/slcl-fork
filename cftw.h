#ifndef CFTW_H
#define CFTW_H

#include <sys/stat.h>

/* Thread-safe variant of ftw(3) and nftw(3) that allows passing an
 * opaque pointer and removes some unneeded parameters. */
int cftw(const char *dirpath, int (*fn)(const char *fpath,
    const struct stat *sb, void *user), void *user);

#endif /* CFTW_H */
