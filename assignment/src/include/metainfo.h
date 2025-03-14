#ifndef METAINFO_H_INCLUDED
#define METAINFO_H_INCLUDED

#include <stddef.h>

struct metainfo_info {
    char *name;
    size_t piece_length;
    size_t length;
    char *pieces;
};

struct metainfo_file {
    char *announce;
    struct metainfo_info info;
};

int metainfo_file_read(struct metainfo_file *file, const char *path);
void metainfo_file_free(struct metainfo_file *file);

const char *metainfo_file_piece_hash(struct metainfo_file *file, size_t i);
size_t metainfo_file_pieces_count(struct metainfo_file *file);

#endif
