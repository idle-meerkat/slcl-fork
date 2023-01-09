#ifndef JWT_H
#define JWT_H

#include <stddef.h>

char *jwt_encode(const char *name, const void *key, size_t n);
int jwt_check(const char *jwt, const void *key, size_t n);

#endif /* JWT_H */
