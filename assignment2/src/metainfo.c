#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include "metainfo.h"
#include "bencode.h"

int metainfo_file_read(struct metainfo_file *file, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("fopen");
        return 0;
    }
    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    rewind(fp);

    char *buffer = malloc(filesize);
    if (!buffer) {
        fclose(fp);
        return 0;
    }
    if (fread(buffer, 1, filesize, fp) != (size_t)filesize) {
        fclose(fp);
        free(buffer);
        return 0;
    }
    fclose(fp);

    // 解析 bencode 数据
    struct bencode_value root;
    size_t decoded = bencode_value_decode(&root, buffer, filesize);
    free(buffer);
    if (decoded == 0) {
        fprintf(stderr, "Failed to decode bencode data.\n");
        return 0;
    }
    if (root.type != BENCODE_MAP) {
        fprintf(stderr, "Torrent file root is not a dictionary.\n");
        bencode_value_free(&root);
        return 0;
    }

    // 提取 "announce" 字段（必需）
    const struct bencode_pair *announce_pair = bencode_map_lookup(&root, "announce");
    if (!announce_pair || announce_pair->value.type != BENCODE_STR) {
        fprintf(stderr, "Missing or invalid 'announce' field.\n");
        bencode_value_free(&root);
        return 0;
    }
    file->announce = malloc(announce_pair->value.as.str_value.len + 1);
    if (!file->announce) {
        bencode_value_free(&root);
        return 0;
    }
    memcpy(file->announce, announce_pair->value.as.str_value.str, announce_pair->value.as.str_value.len);
    file->announce[announce_pair->value.as.str_value.len] = '\0';

    // 提取 "info" 字典（必需）
    const struct bencode_pair *info_pair = bencode_map_lookup(&root, "info");
    if (!info_pair || info_pair->value.type != BENCODE_MAP) {
        fprintf(stderr, "Missing or invalid 'info' field.\n");
        bencode_value_free(&root);
        return 0;
    }

    // 对 info 字典中的必需字段进行校验

    // 1. "name" 字段
    const struct bencode_pair *name_pair = bencode_map_lookup(&info_pair->value, "name");
    if (!name_pair || name_pair->value.type != BENCODE_STR) {
        fprintf(stderr, "Missing or invalid 'name' field in info.\n");
        bencode_value_free(&root);
        return 0;
    }
    file->info.name = malloc(name_pair->value.as.str_value.len + 1);
    if (!file->info.name) {
        bencode_value_free(&root);
        return 0;
    }
    memcpy(file->info.name, name_pair->value.as.str_value.str, name_pair->value.as.str_value.len);
    file->info.name[name_pair->value.as.str_value.len] = '\0';

    // 2. "piece length" 字段
    const struct bencode_pair *piece_length_pair = bencode_map_lookup(&info_pair->value, "piece length");
    if (!piece_length_pair || piece_length_pair->value.type != BENCODE_INT ||
        piece_length_pair->value.as.int_value < 0) {
        fprintf(stderr, "Missing or invalid 'piece length' field in info.\n");
        bencode_value_free(&root);
        return 0;
    }
    file->info.piece_length = (size_t) piece_length_pair->value.as.int_value;

    // 3. "length" 字段
    const struct bencode_pair *length_pair = bencode_map_lookup(&info_pair->value, "length");
    if (!length_pair || length_pair->value.type != BENCODE_INT ||
        length_pair->value.as.int_value < 0) {
        fprintf(stderr, "Missing or invalid 'length' field in info.\n");
        bencode_value_free(&root);
        return 0;
    }
    file->info.length = (size_t) length_pair->value.as.int_value;

    // 4. "pieces" 字段
    const struct bencode_pair *pieces_pair = bencode_map_lookup(&info_pair->value, "pieces");
    if (!pieces_pair || pieces_pair->value.type != BENCODE_STR) {
        fprintf(stderr, "Missing or invalid 'pieces' field in info.\n");
        bencode_value_free(&root);
        return 0;
    }
    size_t p_len = pieces_pair->value.as.str_value.len;
    if (p_len % SHA_DIGEST_LENGTH != 0) {
        fprintf(stderr, "The 'pieces' field length is not a multiple of %d.\n", SHA_DIGEST_LENGTH);
        bencode_value_free(&root);
        return 0;
    }
    {
        char *p_buf = malloc(sizeof(size_t) + p_len);
        if (!p_buf) {
            bencode_value_free(&root);
            return 0;
        }
        memcpy(p_buf, &p_len, sizeof(size_t));  // 保存 pieces 长度
        memcpy(p_buf + sizeof(size_t), pieces_pair->value.as.str_value.str, p_len);
        file->info.pieces = p_buf + sizeof(size_t);
    }

    // 重新编码 info 字典，用于计算 info_hash
    {
        size_t needed = bencode_value_encode(&info_pair->value, NULL, 0);
        if (needed == 0) {
             fprintf(stderr, "Failed to compute needed size for encoding info dictionary.\n");
             bencode_value_free(&root);
             return 0;
        }
        char *temp_buf = malloc(needed);
        if (!temp_buf) {
             bencode_value_free(&root);
             return 0;
        }
        size_t encoded_len = bencode_value_encode(&info_pair->value, temp_buf, needed);
        if (encoded_len == 0) {
             fprintf(stderr, "Failed to encode info dictionary.\n");
             free(temp_buf);
             bencode_value_free(&root);
             return 0;
        }
        SHA1((unsigned char *)temp_buf, encoded_len, file->info_hash);
        free(temp_buf);
    }
    

    bencode_value_free(&root);
    return 1;
}

void metainfo_file_free(struct metainfo_file *file) {
    if (file->announce)
        free(file->announce);
    if (file->info.name)
        free(file->info.name);
    if (file->info.pieces)
        // 释放时将指针还原到 malloc 返回的位置
        free(file->info.pieces - sizeof(size_t));
}

const char *metainfo_file_piece_hash(struct metainfo_file *file, size_t i) {
    if (!file->info.pieces)
        return NULL;
    size_t p_len;
    memcpy(&p_len, file->info.pieces - sizeof(size_t), sizeof(size_t));
    size_t count = p_len / SHA_DIGEST_LENGTH;
    if (i >= count)
        return NULL;
    return file->info.pieces + i * SHA_DIGEST_LENGTH;
}

size_t metainfo_file_pieces_count(struct metainfo_file *file) {
    if (!file->info.pieces)
        return 0;
    size_t p_len;
    memcpy(&p_len, file->info.pieces - sizeof(size_t), sizeof(size_t));
    return p_len / SHA_DIGEST_LENGTH;
}