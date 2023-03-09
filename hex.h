#ifndef HEX_H
#define HEX_H

#include <stddef.h>

int hex_encode(const void *buf, char *hex, size_t buflen, size_t hexlen);
int hex_decode(const char *hex, void *buf, size_t n);

#endif /* HEX_H */
