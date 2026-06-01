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
