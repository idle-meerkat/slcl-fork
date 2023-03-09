#ifndef PAGE_H
#define PAGE_H

#include "http.h"

struct page_quota
{
    unsigned long long cur, max;
};

int page_login(struct http_response *r);
int page_style(struct http_response *r);
int page_failed_login(struct http_response *r);
int page_forbidden(struct http_response *r);
int page_bad_request(struct http_response *r);
int page_resource(struct http_response *r, const char *dir, const char *root,
    const char *res, const struct page_quota *q);
int page_public(struct http_response *r, const char *res);
int page_share(struct http_response *r, const char *path);

#endif /* PAGE_H */
