#include <peer.h>
#include <netdb.h>
#include <errno.h>
#include <client.h>  // 包含 accessor 接口
#include <metainfo.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/time.h>
#include <fcntl.h>  // 用于 fcntl
#include <sys/types.h>

// peer_init：对已有连接的 socket 完成 handshake
int peer_init(struct peer *peer, struct client *client, int sockfd) {
    // 构造 handshake 消息，格式如下：
    // [pstrlen][pstr]["reserved" (8 bytes)][info_hash (20 bytes)][peer_id (20 bytes)]
    unsigned char handshake[68];
    handshake[0] = 19;  // pstrlen
    memcpy(handshake + 1, "BitTorrent protocol", 19);  // pstr
    memset(handshake + 20, 0, 8);  // reserved（8字节 0）
    // 使用 accessor 函数获取 torrent 和 peer_id
    const struct metainfo_file *torrent = client_torrent(client);
    memcpy(handshake + 28, torrent->info_hash, 20);
    memcpy(handshake + 48, client_peer_id(client), 20);

    // 发送 handshake 消息
    ssize_t sent = send(sockfd, handshake, sizeof(handshake), 0);
    if (sent != sizeof(handshake)) {
        perror("send handshake");
        return 0;
    }

    // 设置接收超时
    struct timeval timeout;
    timeout.tv_sec = 5; // 超时时间 5 秒
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
        return 0;
    }

    // 循环读取，确保接收到完整的 68 字节 handshake 响应
    unsigned char response[68];
    size_t total_recv = 0;
    while (total_recv < sizeof(response)) {
        ssize_t recvd = recv(sockfd, response + total_recv, sizeof(response) - total_recv, 0);
        if (recvd <= 0) {
            perror("recv handshake");
            return 0;
        }
        total_recv += recvd;
    }

    // 检查 handshake 响应：
    // 1. 第一字节必须是 19
    if (response[0] != 19) {
        fprintf(stderr, "Invalid handshake pstrlen: %d\n", response[0]);
        return 0;
    }
    // 2. pstr 必须为 "BitTorrent protocol"
    if (memcmp(response + 1, "BitTorrent protocol", 19) != 0) {
        fprintf(stderr, "Invalid handshake pstr\n");
        return 0;
    }
    // 3. 保留字段（bytes 20-27）必须全部为 0
    for (int i = 0; i < 8; i++) {
        if (response[20 + i] != 0) {
            fprintf(stderr, "Invalid reserved bytes in handshake\n");
            return 0;
        }
    }
    // 4. 检查 info_hash（bytes 28-47）
    if (memcmp(response + 28, client_torrent(client)->info_hash, 20) != 0) {
        fprintf(stderr, "Info hash mismatch in handshake\n");
        return 0;
    }
    // 将对方的 peer_id（bytes 48-67）保存到 peer->peer_id
    memcpy(peer->peer_id, response + 48, 20);
    peer->sockfd = sockfd;
    return 1;
}

// peer_connect：连接到指定的 peer，并完成 handshake
int peer_connect(struct peer *peer, struct client *client, const char *ip, uint16_t port) {
    struct addrinfo hints, *res, *rp;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    // 如果 ip 是纯数字（IPv4 或 IPv6），设置 AI_NUMERICHOST；否则进行 DNS 解析
    int is_numeric = 1;
    for (const char *p = ip; *p; p++) {
         if (!isdigit((unsigned char)*p) && *p != '.' && *p != ':') {
             is_numeric = 0;
             break;
         }
    }
    if (is_numeric)
         hints.ai_flags = AI_NUMERICHOST;
    else
         hints.ai_flags = 0;

    int gai_err = getaddrinfo(ip, port_str, &hints, &res);
    if (gai_err != 0) {
         fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_err));
         return 0;
    }

    int sockfd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
         sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
         if (sockfd == -1)
             continue;
         
         // 设置 socket 为非阻塞模式
         int flags = fcntl(sockfd, F_GETFL, 0);
         if (flags < 0) {
             close(sockfd);
             sockfd = -1;
             continue;
         }
         if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
             close(sockfd);
             sockfd = -1;
             continue;
         }
         
         int ret = connect(sockfd, rp->ai_addr, rp->ai_addrlen);
         if (ret == 0) {
             // 连接立即成功
             break;
         } else if (ret < 0 && errno == EINPROGRESS) {
             // 正在连接中，使用 select 等待连接完成
             fd_set writefds;
             FD_ZERO(&writefds);
             FD_SET(sockfd, &writefds);
             struct timeval tv;
             tv.tv_sec = 5;  // 超时时间5秒
             tv.tv_usec = 0;
             ret = select(sockfd + 1, NULL, &writefds, NULL, &tv);
             if (ret > 0 && FD_ISSET(sockfd, &writefds)) {
                 int err;
                 socklen_t len = sizeof(err);
                 if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
                     close(sockfd);
                     sockfd = -1;
                     continue;
                 }
                 // 连接成功
                 break;
             } else {
                 // select 超时或出错
                 close(sockfd);
                 sockfd = -1;
                 continue;
             }
         } else {
             // 连接失败
             close(sockfd);
             sockfd = -1;
             continue;
         }
    }
    freeaddrinfo(res);
    if (sockfd == -1) {
         perror("connect");
         return 0;
    }
    // 恢复阻塞模式
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
    }
    if (!peer_init(peer, client, sockfd)) {
         close(sockfd);
         return 0;
    }
    return 1;
}

// peer_free：释放 peer 连接资源
void peer_free(struct peer *peer) {
    if (peer->sockfd > 0) {
        close(peer->sockfd);
        peer->sockfd = -1;
    }
}
