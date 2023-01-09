#ifndef AUTH_H
#define AUTH_H

#include "http.h"

struct auth *auth_alloc(const char *dir);
void auth_free(struct auth *a);
int auth_cookie(const struct auth *a, const struct http_cookie *c);
int auth_login(const struct auth *a, const char *user, const char *password,
    char **cookie);
const char *auth_dir(const struct auth *a);

#endif /* AUTH_H */
