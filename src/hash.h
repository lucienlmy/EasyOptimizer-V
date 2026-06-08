#ifndef EO_HASH_H
#define EO_HASH_H

#include <stdint.h>

uint32_t jenk_hash(const char *str);
uint32_t jenk_hash_data(const uint8_t *data, size_t len);

#endif
