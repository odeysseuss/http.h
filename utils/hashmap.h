/*
* Hashmap implementation with xxhash and double hashing for collision handling
*
* USAGE:
*   #define HASHMAP_IMPLEMENTATION
*   #include "hashmap.h"
*
* REFERENCE: This library is heavily inspired by [tidwall/hashmap.c](https://github.com/tidwall/hashmap.c)
*/
#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The lifetime of key and value is owned by the caller
typedef struct {
    void *key;
    void *val;
} Node;

typedef struct {
    size_t (*keySize)(const void *key);
    void (*keyFree)(void *key);
    void (*valFree)(void *val);
    int (*keyCmp)(const void *a, const void *b); // returns 0 if equal
    void (*print)(const void *key, const void *val);
    size_t size;
    size_t capacity;
    float load_factor;
    size_t threshold;
    Node tombstone;
    Node **items;
} HashMap;

/// Helper for hashmapNew
typedef struct {
    size_t (*keySize)(const void *key);
    void (*keyFree)(void *key);
    void (*valFree)(void *val);
    int (*keyCmp)(const void *a, const void *b); // returns 0 if equal
    void (*print)(const void *key, const void *val);
    size_t capacity;
    float load_factor;
} HashMapArgs;

/// Has to provide HashMapArgs with capacity, keySize and keyCmp initializes
/// If keyFree and keyVal is provided than the lifetime of key and value will be
/// maintained by the hashmap
/// If load_factor is not provided than it falls back to 7.5
/// Free it with hashmapFree
HashMap *hashmapNew(HashMapArgs *args);
/// Only assigns the pointer. If there is an heap allocated key or value caller
/// is responsible for the lifetime of that memory by freeing it explicitly or by
/// providing keyFree and valFree
void hashmapSet(HashMap *map, const void *key, const void *val);
void *hashmapGet(const HashMap *map, const void *key);
/// If you freed the key or value of any heap allocated object be sure to call
/// hashmapDel as the the lifetime of key, val and node can be completely different.
/// Or, call hashmapSet with the same key with a different val before using it.
/// Otherwise, it will hold dangling pointers.
void hashmapDel(HashMap *map, const void *key);
void hashmapFree(HashMap *map);
/// Has to provide a nodeDisplay function when initializing HashMap via hashmapNew
void hashmapPrint(HashMap *map);

#define malloc_ malloc
#define calloc_ calloc
#define realloc_ realloc
#define free_ free

#ifdef HASHMAP_IMPLEMENTATION

/// === xx3hash function ===
#    define XXH_PRIME_1 11400714785074694791ULL
#    define XXH_PRIME_2 14029467366897019727ULL
#    define XXH_PRIME_3 1609587929392839161ULL
#    define XXH_PRIME_4 9650029242287828579ULL
#    define XXH_PRIME_5 2870177450012600261ULL

static inline uint64_t read64_(const void *memptr) {
    uint64_t val;
    memcpy(&val, memptr, sizeof(val));
    return val;
}

static inline uint32_t read32_(const void *memptr) {
    uint32_t val;
    memcpy(&val, memptr, sizeof(val));
    return val;
}

static inline uint64_t rotl64_(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

static uint64_t xxh3(const void *data, size_t len, uint64_t seed) {
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *const end = p + len;
    uint64_t h64;

    if (len >= 32) {
        const uint8_t *const limit = end - 32;
        uint64_t v1 = seed + XXH_PRIME_1 + XXH_PRIME_2;
        uint64_t v2 = seed + XXH_PRIME_2;
        uint64_t v3 = seed + 0;
        uint64_t v4 = seed - XXH_PRIME_1;

        do {
            v1 += read64_(p) * XXH_PRIME_2;
            v1 = rotl64_(v1, 31);
            v1 *= XXH_PRIME_1;

            v2 += read64_(p + 8) * XXH_PRIME_2;
            v2 = rotl64_(v2, 31);
            v2 *= XXH_PRIME_1;

            v3 += read64_(p + 16) * XXH_PRIME_2;
            v3 = rotl64_(v3, 31);
            v3 *= XXH_PRIME_1;

            v4 += read64_(p + 24) * XXH_PRIME_2;
            v4 = rotl64_(v4, 31);
            v4 *= XXH_PRIME_1;

            p += 32;
        } while (p <= limit);

        h64 =
            rotl64_(v1, 1) + rotl64_(v2, 7) + rotl64_(v3, 12) + rotl64_(v4, 18);

        v1 *= XXH_PRIME_2;
        v1 = rotl64_(v1, 31);
        v1 *= XXH_PRIME_1;
        h64 ^= v1;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;

        v2 *= XXH_PRIME_2;
        v2 = rotl64_(v2, 31);
        v2 *= XXH_PRIME_1;
        h64 ^= v2;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;

        v3 *= XXH_PRIME_2;
        v3 = rotl64_(v3, 31);
        v3 *= XXH_PRIME_1;
        h64 ^= v3;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;

        v4 *= XXH_PRIME_2;
        v4 = rotl64_(v4, 31);
        v4 *= XXH_PRIME_1;
        h64 ^= v4;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;
    } else {
        h64 = seed + XXH_PRIME_5;
    }

    h64 += (uint64_t)len;

    while (p + 8 <= end) {
        uint64_t k1 = read64_(p);
        k1 *= XXH_PRIME_2;
        k1 = rotl64_(k1, 31);
        k1 *= XXH_PRIME_1;
        h64 ^= k1;
        h64 = rotl64_(h64, 27) * XXH_PRIME_1 + XXH_PRIME_4;
        p += 8;
    }

    if (p + 4 <= end) {
        h64 ^= (uint64_t)(read32_(p)) * XXH_PRIME_1;
        h64 = rotl64_(h64, 23) * XXH_PRIME_2 + XXH_PRIME_3;
        p += 4;
    }

    while (p < end) {
        h64 ^= (*p) * XXH_PRIME_5;
        h64 = rotl64_(h64, 11) * XXH_PRIME_1;
        p++;
    }

    h64 ^= h64 >> 33;
    h64 *= XXH_PRIME_2;
    h64 ^= h64 >> 29;
    h64 *= XXH_PRIME_3;
    h64 ^= h64 >> 32;

    return h64;
}

/// === Double hashing ===
static inline uint64_t getHash_(HashMap *map,
                                const void *key,
                                const size_t capacity,
                                const size_t attempt) {
    size_t size = map->keySize(key);
    uint64_t hash_a = xxh3(key, size, 0);
    const uint64_t hash_b = xxh3(key, size, 1);

    return (hash_a + attempt * (hash_b | 1)) % capacity;
}

/// === HashMap implementation ===
static inline Node *nodeNew_(const void *key, const void *val) {
    Node *node = malloc_(sizeof(Node));
    node->key = (void *)key;
    node->val = (void *)val;

    return node;
}

static inline void nodeFree_(HashMap *map, Node *node) {
    if (map->keyFree) {
        map->keyFree(node->key);
    }
    if (map->valFree) {
        map->valFree(node->val);
    }
    free_(node);
}

static void hashmapResize_(HashMap *map) {
    size_t old_cap = map->capacity;
    Node **old_items = map->items;

    map->size = 0;
    map->capacity *= 2;
    map->threshold = (size_t)(map->capacity * map->load_factor);
    map->items = calloc_(map->capacity, sizeof(Node *));
    if (!map->items) {
        return;
    }

    for (size_t i = 0; i < old_cap; i++) {
        Node *node = old_items[i];
        if (node && node != &map->tombstone) {
            uint64_t index = getHash_(map, node->key, map->capacity, 0);

            for (size_t j = 1;
                 map->items[index] && map->items[index] != &map->tombstone;
                 j++) {
                index = getHash_(map, node->key, map->capacity, j);
            }

            map->items[index] = node;
            map->size++;
        }
    }

    free_(old_items);
}

HashMap *hashmapNew(HashMapArgs *args) {
    if (!args || !args->capacity || !args->keySize || !args->keyCmp) {
        return NULL;
    }

    HashMap *map = malloc_(sizeof(HashMap));
    map->size = 0;
    map->capacity = args->capacity;
    if (args->load_factor) {
        map->load_factor = args->load_factor;
    } else {
        map->load_factor = 0.75;
    }
    map->threshold = (size_t)(map->capacity * map->load_factor);
    map->items = calloc_(args->capacity, sizeof(Node *));
    map->keySize = args->keySize;
    map->keyFree = args->keyFree;
    map->valFree = args->valFree;
    map->keyCmp = args->keyCmp;
    map->print = args->print;
    map->tombstone.key = NULL;
    map->tombstone.val = NULL;

    return map;
}

void hashmapSet(HashMap *map, const void *key, const void *val) {
    if (!map || !key || !val) {
        return;
    }

    if (map->threshold <= map->size) {
        hashmapResize_(map);
    }

    uint64_t index = getHash_(map, key, map->capacity, 0);
    uint64_t first_deleted = map->capacity;

    for (size_t i = 0; map->items[index]; i++) {
        if (map->items[index] == &map->tombstone &&
            first_deleted == map->capacity) {
            first_deleted = index;
        }

        if (map->items[index] != &map->tombstone &&
            map->keyCmp(map->items[index]->key, key) == 0) {
            map->items[index]->val = (void *)val;
            return;
        }

        index = getHash_(map, key, map->capacity, i + 1);
    }

    if (first_deleted != map->capacity) {
        index = first_deleted;
    }

    Node *node = nodeNew_(key, val);
    map->items[index] = node;
    map->size++;
}

void *hashmapGet(const HashMap *map, const void *key) {
    if (!map || !key) {
        return NULL;
    }

    uint64_t index = getHash_((void *)map, key, map->capacity, 0);

    for (size_t i = 0; i < map->capacity; i++) {
        Node *node = map->items[index];
        if (!node) {
            return NULL;
        }

        if (node != &map->tombstone && map->keyCmp(node->key, key) == 0) {
            return node->val;
        }

        index = getHash_((void *)map, key, map->capacity, i + 1);
    }

    return NULL;
}

void hashmapDel(HashMap *map, const void *key) {
    if (!map || !key) {
        return;
    }

    uint64_t index = getHash_(map, key, map->capacity, 0);

    for (size_t i = 0; i < map->capacity; i++) {
        Node *node = map->items[index];
        if (!node) {
            return;
        }

        if (node != &map->tombstone && map->keyCmp(node->key, key) == 0) {
            nodeFree_(map, node);
            map->items[index] = &map->tombstone;
            map->size--;
            return;
        }

        index = getHash_(map, key, map->capacity, i + 1);
    }
}

void hashmapFree(HashMap *map) {
    if (!map) {
        return;
    }

    for (size_t i = 0; i < map->capacity; i++) {
        Node *node = map->items[i];
        if (node && node != &map->tombstone) {
            nodeFree_(map, node);
        }
    }

    free_(map->items);
    free_(map);
}

void hashmapPrint(HashMap *map) {
    if (!map || !map->print) {
        return;
    }

    printf("HashMap (size: %zu, capacity: %zu, load_factor: %.2f)\n",
           map->size,
           map->capacity,
           map->load_factor);
    for (size_t i = 0; i < map->capacity; i++) {
        Node *node = map->items[i];
        printf("[%zu] ", i);
        if (!node) {
            puts("empty");
        } else if (node == &map->tombstone) {
            puts("tomb");
        } else {
            map->print(node->key, node->val);
        }
    }
}

#endif // HASHMAP_IMPLEMENTATION

#undef malloc_
#undef calloc_
#undef realloc_
#undef free_

#ifdef __cplusplus
}
#endif

#endif
