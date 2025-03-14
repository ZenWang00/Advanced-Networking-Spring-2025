/*author: zitian wang*/
#ifndef BENCODE_H_INCLUDED
#define BENCODE_H_INCLUDED

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

enum bencode_t {
    BENCODE_STR,
    BENCODE_INT,
    BENCODE_LIST,
    BENCODE_MAP,
};

struct bencode_list;
struct bencode_map;
struct bencode_str;

struct bencode_list {
    struct bencode_value **values;
    size_t count;
};

struct bencode_map {
    struct bencode_pair *pairs;
    size_t count;
};

struct bencode_str {
    char *str;
    size_t len;
};

struct bencode_value {
    // TODO fill this structure
    enum bencode_t type;
    union {
        long long int_val;
        struct bencode_str str;
        struct bencode_list list;
        struct bencode_map map;
    } as;
};

struct bencode_pair {
    struct bencode_value key;
    struct bencode_value value;
};

void bencode_value_free(struct bencode_value *value);
size_t bencode_value_decode(struct bencode_value *value, const char *enc_val, size_t n);

enum bencode_t bencode_value_type(const struct bencode_value *value);
size_t bencode_value_len(const struct bencode_value *value);

long long bencode_value_int(const struct bencode_value *value);
const char *bencode_value_str(const struct bencode_value *value);
const struct bencode_value *bencode_list_get(const struct bencode_value *value, size_t i);
const struct bencode_pair *bencode_map_lookup(const struct bencode_value *value, const char *key);

#endif
