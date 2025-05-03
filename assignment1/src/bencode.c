/*author: zitian wang*/
#include <bencode.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static size_t decode_value(struct bencode_value *value, const char *s, size_t n) {
    if (n == 0) return 0;
    if (s[0] == 'i') {
        
        size_t pos = 1;
        while (pos < n && s[pos] != 'e') pos++;
        if (pos == n) return 0; /* 格式错误 */
        size_t numlen = pos - 1;
        if (numlen == 0) return 0; /* 没有数字 */
        char numbuf[64];
        if (numlen >= sizeof(numbuf))
            numlen = sizeof(numbuf) - 1;
        memcpy(numbuf, s + 1, numlen);
        numbuf[numlen] = '\0';
        
        int is_negative = (numbuf[0] == '-');
        if (is_negative) {
            if (numlen == 1) return 0; 
            if (numbuf[1] == '0') return 0;
        } else {
            if (numbuf[0] == '0' && numlen != 1) return 0;
        }
        /* 验证除符号外所有字符必须为数字 */
        size_t start = is_negative ? 1 : 0;
        for (size_t i = start; i < numlen; i++) {
            if (!isdigit(numbuf[i])) return 0;
        }
        value->type = BENCODE_INT;
        value->as.int_val = atoll(numbuf);
        return pos + 1;
    } else if (isdigit(s[0])) {
        
        size_t pos = 0;
        long len = 0;
        while (pos < n && isdigit(s[pos])) {
            len = len * 10 + (s[pos] - '0');
            pos++;
        }
        if (pos >= n || s[pos] != ':') return 0;
        pos++; 
        if (pos + len > n) return 0;
        value->type = BENCODE_STR;
        value->as.str.len = len;
        value->as.str.str = malloc(len + 1);
        memcpy(value->as.str.str, s + pos, len);
        value->as.str.str[len] = '\0';
        return pos + len;
    } else if (s[0] == 'l') {
        
        value->type = BENCODE_LIST;
        size_t pos = 1;
        size_t capacity = 4;
        value->as.list.values = malloc(capacity * sizeof(struct bencode_value *));
        value->as.list.count = 0;
        while (pos < n && s[pos] != 'e') {
            if (value->as.list.count == capacity) {
                capacity *= 2;
                value->as.list.values = realloc(value->as.list.values, capacity * sizeof(struct bencode_value *));
            }
            struct bencode_value *elem = malloc(sizeof(struct bencode_value));
            size_t consumed = decode_value(elem, s + pos, n - pos);
            if (consumed == 0) {
                free(elem);
                return 0;
            }
            value->as.list.values[value->as.list.count++] = elem;
            pos += consumed;
        }
        if (pos >= n || s[pos] != 'e') return 0;
        return pos + 1;
    } else if (s[0] == 'd') {
       
        value->type = BENCODE_MAP;
        size_t pos = 1;
        size_t capacity = 4;
        value->as.map.pairs = malloc(capacity * sizeof(struct bencode_pair));
        value->as.map.count = 0;
        while (pos < n && s[pos] != 'e') {
            if (value->as.map.count == capacity) {
                capacity *= 2;
                value->as.map.pairs = realloc(value->as.map.pairs, capacity * sizeof(struct bencode_pair));
            }
            /* 必须为字符串 */
            struct bencode_value key;
            size_t consumed = decode_value(&key, s + pos, n - pos);
            if (consumed == 0) return 0;
            
            if (key.type != BENCODE_STR) {
                bencode_value_free(&key);
                return 0;
            }
            pos += consumed;
            
            struct bencode_value val;
            consumed = decode_value(&val, s + pos, n - pos);
            if (consumed == 0) return 0;
            pos += consumed;
            value->as.map.pairs[value->as.map.count].key = key;
            value->as.map.pairs[value->as.map.count].value = val;
            value->as.map.count++;
        }
        if (pos >= n || s[pos] != 'e') return 0;
        return pos + 1;
    }
    return 0;
}

void bencode_value_free(struct bencode_value *value) {
    if (!value) return;
    switch (value->type) {
        case BENCODE_INT:
            break;
        case BENCODE_STR:
            free(value->as.str.str);
            break;
        case BENCODE_LIST:
            for (size_t i = 0; i < value->as.list.count; i++) {
                bencode_value_free(value->as.list.values[i]);
                free(value->as.list.values[i]);
            }
            free(value->as.list.values);
            break;
        case BENCODE_MAP:
            for (size_t i = 0; i < value->as.map.count; i++) {
                bencode_value_free(&value->as.map.pairs[i].key);
                bencode_value_free(&value->as.map.pairs[i].value);
            }
            free(value->as.map.pairs);
            break;
    }
}

size_t bencode_value_decode(struct bencode_value *value, const char *enc_val, size_t n) {
    return decode_value(value, enc_val, n);
}

enum bencode_t bencode_value_type(const struct bencode_value *value) {
    return value->type;
}

size_t bencode_value_len(const struct bencode_value *value)
{
    switch (value->type) {
        case BENCODE_INT:
            return sizeof(long long);  
        case BENCODE_STR:
            return value->as.str.len;
        case BENCODE_LIST:
            return value->as.list.count;
        case BENCODE_MAP:
            return value->as.map.count;
        default:
            return 0;
    }
}


long long bencode_value_int(const struct bencode_value *value)
{
    if (value->type == BENCODE_INT)
        return value->as.int_val;
    return 0;
}

const char *bencode_value_str(const struct bencode_value *value)
{
    if (value->type == BENCODE_STR)
        return value->as.str.str;
    return NULL;
}

const struct bencode_value *bencode_list_get(const struct bencode_value *value, size_t i)
{
    if (value->type == BENCODE_LIST && i < value->as.list.count)
        return value->as.list.values[i];
    return NULL;
}

const struct bencode_pair *bencode_map_lookup(const struct bencode_value *value, const char *key)
{
    if (value->type == BENCODE_MAP) {
        for (size_t i = 0; i < value->as.map.count; i++) {
            if (value->as.map.pairs[i].key.type == BENCODE_STR &&
                strcmp(value->as.map.pairs[i].key.as.str.str, key) == 0)
                return &value->as.map.pairs[i];
        }
    }
    return NULL;
}
