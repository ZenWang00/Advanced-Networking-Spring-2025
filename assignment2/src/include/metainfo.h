#ifndef METAINFO_H_INCLUDED
#define METAINFO_H_INCLUDED

#include <stddef.h>
#include <openssl/sha.h>

struct metainfo_info {
    char *name;
    size_t piece_length;
    size_t length;
    char *pieces;
};

struct metainfo_file {
    char *announce;
    struct metainfo_info info;
    unsigned char info_hash[SHA_DIGEST_LENGTH];
};

/**
 * Initializes a structure representing a torrent file. In practice,
 * it initializes the structure fields and computes the info_hash
 * value.
 *
 * @param file An output parameter that will contain the initialized
 * torrent file structure.
 * @param path The path to the .torrent file.
 * @return Returns 0 on failure; otherwise return a non-zero value.
 */
int metainfo_file_read(struct metainfo_file *file, const char *path);

/**
 * Release all the memory internally used by the file. Note that value
 * should not be deallocated.
 *
 * @param file A pointer to a torrent file structure.
 */
void metainfo_file_free(struct metainfo_file *file);

/**
 * Obtain the hash value of the i-th piece for the file structure.
 *
 * @param file The torrent file structure.
 * @param i The index of the piece.
 * @return The hash value of the i-th piece.
 */
const char *metainfo_file_piece_hash(struct metainfo_file *file, size_t i);

/**
 * Returns the number of pieces.
 *
 * @param file The torrent file structure.
 * @return The number of pieces.
 */
size_t metainfo_file_pieces_count(struct metainfo_file *file);

#endif
