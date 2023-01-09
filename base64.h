#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>

char *base64_encode(const void *buf, size_t n);
void *base64_decode(const char *b64, size_t *n);

#endif /* BASE64_H */
