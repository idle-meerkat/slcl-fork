#ifndef PAGE_H
#define PAGE_H

#include "http.h"
#include <stddef.h>

struct page_quota
{
    unsigned long long cur, max;
};

struct page_resource
{
    struct http_response *r;
    const char *dir, *root, *res;
    const struct page_quota *q;
    const struct http_arg *args;
    size_t n_args;
};

struct page_search
{
    struct page_search_result
    {
        char *name;
    } *results;

    const char *root;
    size_t n;
};

int page_login(struct http_response *r);
int page_style(struct http_response *r);
int page_failed_login(struct http_response *r);
int page_forbidden(struct http_response *r);
int page_bad_request(struct http_response *r);
int page_resource(const struct page_resource *r);
int page_public(struct http_response *r, const char *res);
int page_share(struct http_response *r, const char *path);
int page_quota_exceeded(struct http_response *r, unsigned long long len,
    unsigned long long quota);
int page_search(struct http_response *r, const struct page_search *s);

#endif /* PAGE_H */
