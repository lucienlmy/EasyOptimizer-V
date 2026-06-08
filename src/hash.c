#include "hash.h"
#include <ctype.h>

uint32_t jenk_hash(const char *str) {
    uint32_t hash = 0;
    while (*str) {
        hash += (uint8_t)tolower((unsigned char)*str);
        hash += (hash << 10);
        hash ^= (hash >> 6);
        str++;
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

uint32_t jenk_hash_data(const uint8_t *data, size_t len) {
    uint32_t hash = 0;
    for (size_t i = 0; i < len; i++) {
        hash += data[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}
