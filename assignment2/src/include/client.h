#ifndef CLIENT_H_INCLUDED
#define CLIENT_H_INCLUDED

#include <bencode.h>
#include <metainfo.h>
#include <stdint.h>
#include <peer.h>

struct client;


/**
 * Initializes a structure representing the internal state needed to
 * download a file. It should also randomly generate the ID of the
 * peer.
 *
 * @param torrent A pointer to the torrent file structure representing
 * the client to download.
 * @param port The port a peer listened should listen on in case it is
 * started.
 * @return Returns a pointer to the allocated client structure.
 */
struct client *client_new(struct metainfo_file *torrent, uint16_t port);

/**
 * Release all the memory internally used by the client.
 *
 * @param client A pointer to the client structure.
 */
void client_free(struct client *client);


/**
 * Returns the ID of the client.
 *
 * @param client A pointer to the client structure.
 * @return A 20 bytes long buffer containing the ID of the peer.
 */
const unsigned char *client_peer_id(struct client *client);

/**
 * Returns the port the client should listen on when it listens on
 * peer connections.
 *
 * @param client A pointer to the client structure.
 * @return A port the client should listen on when it listens on peer
 * connections.
 */
uint16_t client_port(struct client *client);

/**
 * Returns the number of bytes uploaded by the client.
 *
 * @param client A pointer to the client structure.
 * @return The number of bytes uploaded by the client.
 */
size_t client_uploaded(struct client *client);

/**
 * Returns the number of bytes downloaded by the client.
 *
 * @param client A pointer to the client structure.
 * @return The number of bytes downloaded by the client.
 */
size_t client_downloaded(struct client *client);

/**
 * Returns the number of bytes left to download to complete
 * the download the file associated with the .torrent.
 *
 * @param client A pointer to the client structure.
 * @return The number of bytes left to download to complete
 * the download the file associated with the .torrent.
 */
size_t client_left(struct client *client);

/**
 * Obtain the torrent file structure the client is
 * torrenting.
 *
 * @param client A pointer to the client structure.
 * @return The torrent file structure the client is
 * torrenting.
 */
const struct metainfo_file *client_torrent(struct client *client);

/**
 * Start the tracker connection thread.
 *
 * @param client A pointer to the client structure.
 * @return Returns 0 on failure; otherwise returns a non-zero value
 */
int client_tracker_connect(struct client *client);

/**
 * Start the thread listening for connecting peers.
 *
 * @param client A pointer to the client structure.
 * @return Returns 0 on failure; otherwise returns a non-zero value
 */
int client_peer_listener_start(struct client *client);

/**
 * Register a peer connection.
 *
 * @param client A pointer to the client structure.
 * @param sockfd The socket of the connected peer.
 */
void client_add_connected_peer(struct client *client, int sockfd);

/**
 * Connect to each peer in a bencoded list of peers.
 *
 * @param client A pointer to the client structure.
 * @param peers A bencoded list of peers.
 */
void client_add_bencoded_peer_list(struct client *client, const struct bencode_value *peers);

#endif
