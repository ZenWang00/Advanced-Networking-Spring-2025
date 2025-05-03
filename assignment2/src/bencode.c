#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "bencode.h"

/* Helper: 计算正数的位数 */
static int num_digits(size_t num) {
    int digits = 1;
    while (num >= 10) {
        num /= 10;
        digits++;
    }
    return digits;
}

/* 内部递归解析 bencode 值 */
static size_t decode_bencode_value(struct bencode_value *value, const char *s, size_t n) {
    if (n == 0) return 0;
    if (isdigit((unsigned char)s[0])) {
        /* 解析字符串: <len>:<data> */
        char *endptr;
        long len = strtol(s, &endptr, 10);
        if (endptr == s || *endptr != ':') return 0;
        if (len < 0) return 0;
        size_t header_len = (endptr - s) + 1; // 包括冒号
        if ((size_t)len + header_len > n) return 0;
        value->type = BENCODE_STR;
        value->as.str_value.len = (size_t)len;
        value->as.str_value.str = malloc(value->as.str_value.len + 1);
        if (!value->as.str_value.str) return 0;
        memcpy(value->as.str_value.str, s + header_len, value->as.str_value.len);
        value->as.str_value.str[value->as.str_value.len] = '\0';
        return header_len + value->as.str_value.len;
    } else if (s[0] == 'i') {
        /* 解析整数: i<number>e */
        if (n < 3) return 0; // 至少 "i0e"
        const char *e_ptr = memchr(s, 'e', n);
        if (!e_ptr) return 0;
        size_t len = e_ptr - s; // 不包括 'e'
        if (len < 2) return 0;
        size_t index = 1;
        if (s[1] == '-') {
            index++;
            if (index >= len) return 0; // 无数字
        }
        /* 检查前导零：如果数字部分长度大于 1 且首字符为 '0' 则非法 */
        if (s[index] == '0' && (len - index) > 1) return 0;
        /* 检查剩余字符是否都是数字 */
        for (size_t i = index; i < len; i++) {
            if (!isdigit((unsigned char)s[i])) return 0;
        }
        char *temp = strndup(s + 1, len - 1);
        if (!temp) return 0;
        char *endptr;
        long long num = strtoll(temp, &endptr, 10);
        free(temp);
        if (endptr == temp) return 0;
        value->type = BENCODE_INT;
        value->as.int_value = num;
        return (e_ptr - s) + 1;
    } else if (s[0] == 'l') {
        /* 解析列表: l<元素...>e */
        value->type = BENCODE_LIST;
        size_t capacity = 8;
        value->as.list_value.values = malloc(capacity * sizeof(struct bencode_value));
        if (!value->as.list_value.values) return 0;
        size_t count = 0;
        size_t offset = 1; // 跳过 'l'
        while (offset < n && s[offset] != 'e') {
            if (count >= capacity) {
                capacity *= 2;
                struct bencode_value *new_values = realloc(value->as.list_value.values, capacity * sizeof(struct bencode_value));
                if (!new_values) {
                    for (size_t j = 0; j < count; j++) {
                        bencode_value_free(&value->as.list_value.values[j]);
                    }
                    free(value->as.list_value.values);
                    return 0;
                }
                value->as.list_value.values = new_values;
            }
            size_t consumed = decode_bencode_value(&value->as.list_value.values[count], s + offset, n - offset);
            if (consumed == 0) {
                for (size_t j = 0; j < count; j++) {
                    bencode_value_free(&value->as.list_value.values[j]);
                }
                free(value->as.list_value.values);
                return 0;
            }
            offset += consumed;
            count++;
        }
        if (offset >= n || s[offset] != 'e') {
            for (size_t j = 0; j < count; j++) {
                bencode_value_free(&value->as.list_value.values[j]);
            }
            free(value->as.list_value.values);
            return 0;
        }
        offset++; // 跳过 'e'
        value->as.list_value.count = count;
        return offset;
    } else if (s[0] == 'd') {
        /* 解析字典: d<key><value>...e */
        value->type = BENCODE_MAP;
        size_t capacity = 8;
        value->as.map_value.pairs = malloc(capacity * sizeof(struct bencode_pair));
        if (!value->as.map_value.pairs) return 0;
        size_t count = 0;
        size_t offset = 1; // 跳过 'd'
        while (offset < n && s[offset] != 'e') {
            struct bencode_value key;
            size_t consumed = decode_bencode_value(&key, s + offset, n - offset);
            if (consumed == 0 || key.type != BENCODE_STR) {
                bencode_value_free(&key);
                for (size_t j = 0; j < count; j++) {
                    bencode_value_free(&value->as.map_value.pairs[j].key);
                    bencode_value_free(&value->as.map_value.pairs[j].value);
                }
                free(value->as.map_value.pairs);
                return 0;
            }
            offset += consumed;
            struct bencode_value val;
            consumed = decode_bencode_value(&val, s + offset, n - offset);
            if (consumed == 0) {
                bencode_value_free(&key);
                for (size_t j = 0; j < count; j++) {
                    bencode_value_free(&value->as.map_value.pairs[j].key);
                    bencode_value_free(&value->as.map_value.pairs[j].value);
                }
                free(value->as.map_value.pairs);
                return 0;
            }
            offset += consumed;
            if (count >= capacity) {
                capacity *= 2;
                struct bencode_pair *new_pairs = realloc(value->as.map_value.pairs, capacity * sizeof(struct bencode_pair));
                if (!new_pairs) {
                    bencode_value_free(&key);
                    bencode_value_free(&val);
                    for (size_t j = 0; j < count; j++) {
                        bencode_value_free(&value->as.map_value.pairs[j].key);
                        bencode_value_free(&value->as.map_value.pairs[j].value);
                    }
                    free(value->as.map_value.pairs);
                    return 0;
                }
                value->as.map_value.pairs = new_pairs;
            }
            value->as.map_value.pairs[count].key = key;
            value->as.map_value.pairs[count].value = val;
            count++;
        }
        if (offset >= n || s[offset] != 'e') {
            for (size_t j = 0; j < count; j++) {
                bencode_value_free(&value->as.map_value.pairs[j].key);
                bencode_value_free(&value->as.map_value.pairs[j].value);
            }
            free(value->as.map_value.pairs);
            return 0;
        }
        offset++; // 跳过 'e'
        value->as.map_value.count = count;
        return offset;
    }
    return 0;
}

size_t bencode_value_decode(struct bencode_value *value, const char *enc_val, size_t n) {
    return decode_bencode_value(value, enc_val, n);
}

/* 编码函数：支持两种模式
   1. 当 buf 为 NULL 时，仅计算并返回所需总长度
   2. 当 buf 不为 NULL 时，若 n 不足返回 0，否则写入编码数据（不包含终止符） */
size_t bencode_value_encode(const struct bencode_value *value, char *buf, size_t n) {
    size_t total = 0;
    int len;
    switch (value->type) {
        case BENCODE_STR: {
            int digits = num_digits(value->as.str_value.len);
            total = digits + 1 + value->as.str_value.len;  // 数字字符 + ':' + 数据
            if (buf == NULL)
                return total;
            if (total > n)
                return 0;
            char header[32];
            len = snprintf(header, sizeof(header), "%zu:", value->as.str_value.len);
            if (len != digits + 1)
                return 0;
            memcpy(buf, header, len);
            memcpy(buf + len, value->as.str_value.str, value->as.str_value.len);
            return total;
        }
        case BENCODE_INT: {
            char temp[32];
            /* 生成 "i%llde" 格式，不包含字符串终止符 */
            len = snprintf(temp, sizeof(temp), "i%llde", value->as.int_value);
            if (len <= 0)
                return 0;
            total = (size_t)len;
            if (buf == NULL)
                return total;
            if (total > n)
                return 0;
            memcpy(buf, temp, total);
            return total;
        }
        case BENCODE_LIST: {
            total = 1; // 'l'
            size_t i;
            for (i = 0; i < value->as.list_value.count; i++) {
                size_t item_size = bencode_value_encode(&value->as.list_value.values[i], NULL, 0);
                total += item_size;
            }
            total += 1; // 'e'
            if (buf == NULL)
                return total;
            if (total > n)
                return 0;
            buf[0] = 'l';
            size_t pos = 1;
            for (i = 0; i < value->as.list_value.count; i++) {
                size_t item_size = bencode_value_encode(&value->as.list_value.values[i], buf + pos, n - pos);
                if (item_size == 0)
                    return 0;
                pos += item_size;
            }
            buf[pos] = 'e';
            pos++;
            return pos;
        }
        case BENCODE_MAP: {
            total = 1; // 'd'
            size_t i;
            for (i = 0; i < value->as.map_value.count; i++) {
                size_t key_size = bencode_value_encode(&value->as.map_value.pairs[i].key, NULL, 0);
                size_t val_size = bencode_value_encode(&value->as.map_value.pairs[i].value, NULL, 0);
                total += key_size + val_size;
            }
            total += 1; // 'e'
            if (buf == NULL)
                return total;
            if (total > n)
                return 0;
            buf[0] = 'd';
            size_t pos = 1;
            for (i = 0; i < value->as.map_value.count; i++) {
                size_t key_size = bencode_value_encode(&value->as.map_value.pairs[i].key, buf + pos, n - pos);
                if (key_size == 0)
                    return 0;
                pos += key_size;
                size_t val_size = bencode_value_encode(&value->as.map_value.pairs[i].value, buf + pos, n - pos);
                if (val_size == 0)
                    return 0;
                pos += val_size;
            }
            buf[pos] = 'e';
            pos++;
            return pos;
        }
    }
    return 0;
}

enum bencode_t bencode_value_type(const struct bencode_value *value) {
    return value->type;
}

size_t bencode_value_len(const struct bencode_value *value) {
    switch (value->type) {
        case BENCODE_STR:
            return value->as.str_value.len;
        case BENCODE_INT:
            return sizeof(value->as.int_value);
        case BENCODE_LIST:
            return value->as.list_value.count;
        case BENCODE_MAP:
            return value->as.map_value.count;
    }
    return 0;
}

long long bencode_value_int(const struct bencode_value *value) {
    if (value->type == BENCODE_INT)
        return value->as.int_value;
    return 0;
}

const char *bencode_value_str(const struct bencode_value *value) {
    if (value->type == BENCODE_STR)
        return value->as.str_value.str;
    return NULL;
}

const struct bencode_value *bencode_list_get(const struct bencode_value *value, size_t i) {
    if (value->type == BENCODE_LIST && i < value->as.list_value.count)
        return &value->as.list_value.values[i];
    return NULL;
}

const struct bencode_pair *bencode_map_lookup(const struct bencode_value *value, const char *key) {
    if (value->type == BENCODE_MAP) {
        for (size_t i = 0; i < value->as.map_value.count; i++) {
            const struct bencode_value *k = &value->as.map_value.pairs[i].key;
            if (k->type == BENCODE_STR && strcmp(k->as.str_value.str, key) == 0)
                return &value->as.map_value.pairs[i];
        }
    }
    return NULL;
}

void bencode_value_free(struct bencode_value *value) {
    size_t i;
    switch (value->type) {
        case BENCODE_STR:
            free(value->as.str_value.str);
            break;
        case BENCODE_LIST:
            for (i = 0; i < value->as.list_value.count; i++) {
                bencode_value_free(&value->as.list_value.values[i]);
            }
            free(value->as.list_value.values);
            break;
        case BENCODE_MAP:
            for (i = 0; i < value->as.map_value.count; i++) {
                bencode_value_free(&value->as.map_value.pairs[i].key);
                bencode_value_free(&value->as.map_value.pairs[i].value);
            }
            free(value->as.map_value.pairs);
            break;
        case BENCODE_INT:
        default:
            break;
    }
}
