#ifndef PEER_H_INCLUDED
#define PEER_H_INCLUDED

#include <pthread.h>
#include <bencode.h>
#include <stdint.h>

struct client;

struct peer {
    unsigned char peer_id[20];
    int sockfd;
};

/**
 * Initializes a peer connection structure. In practice, it completes
 * the BitTorrent peer handshake and initialize the peer structure
 * with the connection socket and the peer ID.
 *
 * @param peer A pointer to the peer connection structure.
 * @param client A pointer to the client.
 * @param sockfd The socket file descriptor for the peer connection.
 * @return Returns 0 on failure; otherwise return a non-zero value.
 */
int peer_init(struct peer *peer, struct client *client, int sockfd);

/**
 * It connects to a peer at the given address, and intializes a peer
 * connection structure. In practice, it creates a socket connected to
 * the peer, completes the BitTorrent peer handshake and initialize
 * the peer structure with the connection socket and the peer ID.
 *
 * @param peer A pointer to the peer connection structure.
 * @param client A pointer to the client.
 * @param ip The IPv4 or IPv6 address of the peer.
 * @param port The port the peer is listening on for peer connections.
 * @return Returns 0 on failure; otherwise return a non-zero value.
 */
int peer_connect(struct peer *peer, struct client *client,
		 const char *ip, uint16_t port);

/**
 * Release all the memory internally used by the peer connection, and
 * closes the associated socket. Note that the peer pointer should not
 * be deallocated.
 *
 * @param peer A pointer the peer connection.
 */
void peer_free(struct peer *peer);

#endif
