/*author: zitian wang*/
#include <metainfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bencode.h>

static char *extract_str_from_map(const struct bencode_value *map, const char *key) {
    const struct bencode_pair *pair = bencode_map_lookup(map, key);
    if (!pair || pair->value.type != BENCODE_STR)
        return NULL;
    size_t len = pair->value.as.str.len;
    char *dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, pair->value.as.str.str, len);
        dup[len] = '\0';
    }
    return dup;
}

static long long extract_int_from_map(const struct bencode_value *map, const char *key) {
    const struct bencode_pair *pair = bencode_map_lookup(map, key);
    if (!pair || pair->value.type != BENCODE_INT)
        return 0;
    return pair->value.as.int_val;
}

int metainfo_file_read(struct metainfo_file *file, const char *path) {
    FILE *f = fopen(path, "rb");
    
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(fsize);
    if (!data) {
        fclose(f);
        return 0;
    }
    if (fread(data, 1, fsize, f) != (size_t)fsize) {
        fclose(f);
        free(data);
        return 0;
    }
    fclose(f);

    struct bencode_value *root = malloc(sizeof(struct bencode_value));
    if (!root) {
        free(data);
        return 0;
    }
    size_t consumed = bencode_value_decode(root, data, fsize);
    free(data);
    if (consumed == 0 || root->type != BENCODE_MAP) {
        bencode_value_free(root);
        free(root);
        return 0;
    }

    /* 提取字段 */
    const struct bencode_pair *announce_pair = bencode_map_lookup(root, "announce");
    if (!announce_pair || announce_pair->value.type != BENCODE_STR) {
        bencode_value_free(root);
        free(root);
        return 0;
    }
    file->announce = malloc(announce_pair->value.as.str.len + 1);
    if (!file->announce) {
        bencode_value_free(root);
        free(root);
        return 0;
    }
    memcpy(file->announce, announce_pair->value.as.str.str, announce_pair->value.as.str.len);
    file->announce[announce_pair->value.as.str.len] = '\0';

    /* 提取字典 */
    const struct bencode_pair *info_pair = bencode_map_lookup(root, "info");
    if (!info_pair || info_pair->value.type != BENCODE_MAP) {
        free(file->announce);
        bencode_value_free(root);
        free(root);
        return 0;
    }
    const struct bencode_value *info = &info_pair->value;

    /* 从 info 字典中提取各个字段 */
    file->info.name = extract_str_from_map(info, "name");
    if (!file->info.name) {
        free(file->announce);
        bencode_value_free(root);
        free(root);
        return 0;
    }
    {
        long long tmp_piece_length = extract_int_from_map(info, "piece length");
        if (tmp_piece_length <= 0) {  
            free(file->announce);
            free(file->info.name);
            bencode_value_free(root);
            free(root);
            return 0;
        }
        file->info.piece_length = (size_t) tmp_piece_length;
    }
    {
        long long tmp_length = extract_int_from_map(info, "length");
        if (tmp_length <= 0) {  
            free(file->announce);
            free(file->info.name);
            bencode_value_free(root);
            free(root);
            return 0;
        }
        file->info.length = (size_t) tmp_length;
    }
    
    const struct bencode_pair *pieces_pair = bencode_map_lookup(info, "pieces");
    if (!pieces_pair || pieces_pair->value.type != BENCODE_STR) {
        free(file->announce);
        free(file->info.name);
        bencode_value_free(root);
        free(root);
        return 0;
    }
    /* 检查 pieces 字段长度是否为 20 的倍数 */
    if (pieces_pair->value.as.str.len % 20 != 0) {
        free(file->announce);
        free(file->info.name);
        bencode_value_free(root);
        free(root);
        return 0;
    }
    file->info.pieces = malloc(pieces_pair->value.as.str.len);
    if (!file->info.pieces) {
        free(file->announce);
        free(file->info.name);
        bencode_value_free(root);
        free(root);
        return 0;
    }
    memcpy(file->info.pieces, pieces_pair->value.as.str.str, pieces_pair->value.as.str.len);

    bencode_value_free(root);
    free(root);

    return 1;
}

void metainfo_file_free(struct metainfo_file *file)
{
    if (file->announce) {
        free(file->announce);
        file->announce = NULL;
    }
    if (file->info.name) {
        free(file->info.name);
        file->info.name = NULL;
    }
    if (file->info.pieces) {
        free(file->info.pieces);
        file->info.pieces = NULL;
    }
}

const char *metainfo_file_piece_hash(struct metainfo_file *file, size_t i)
{
    size_t piece_count = metainfo_file_pieces_count(file);
    if (i >= piece_count) return NULL;
    return file->info.pieces + (i * 20);
}

size_t metainfo_file_pieces_count(struct metainfo_file *file)
{
    size_t piece_count = (file->info.length + file->info.piece_length - 1) / file->info.piece_length;
    return piece_count;
}
