#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include "peer_listener.h"
#include "client.h"  // 注意：只能通过 accessor 访问 client 内部数据

// 定义 peer_listener 结构体，隐藏实现细节
struct peer_listener {
    struct client *client;  // 与该监听器关联的 client
    int sockfd;             // 监听 socket
    pthread_t thread;       // 监听线程句柄
    int running;            // 标志是否继续运行
};

/*
 * handle_incoming_handshake - 处理入站连接的 handshake 流程
 *
 * 1. 循环读取 68 字节 handshake 消息
 * 2. 验证 handshake 各字段：pstrlen、pstr、reserved 字段、info_hash
 * 3. 如果验证失败，调用 shutdown() 关闭写端，让对方的读返回 EOF，然后返回 0
 * 4. 如果验证通过，构造 handshake 响应并发送给对端，返回 1
 */
static int handle_incoming_handshake(int sockfd, struct client *client) {
    unsigned char request[68];
    size_t total_recv = 0;
    while (total_recv < sizeof(request)) {
        ssize_t n = recv(sockfd, request + total_recv, sizeof(request) - total_recv, 0);
        if (n <= 0) {
            perror("recv handshake from incoming peer");
            return 0;
        }
        total_recv += n;
    }

    // 验证 handshake 消息格式
    if (request[0] != 19) {
        fprintf(stderr, "Incoming handshake invalid pstrlen: %d\n", request[0]);
        shutdown(sockfd, SHUT_WR);
        return 0;
    }
    if (memcmp(request + 1, "BitTorrent protocol", 19) != 0) {
        fprintf(stderr, "Incoming handshake invalid pstr\n");
        shutdown(sockfd, SHUT_WR);
        return 0;
    }
    for (int i = 0; i < 8; i++) {
        if (request[20 + i] != 0) {
            fprintf(stderr, "Incoming handshake invalid reserved bytes\n");
            shutdown(sockfd, SHUT_WR);
            return 0;
        }
    }
    if (memcmp(request + 28, client_torrent(client)->info_hash, 20) != 0) {
        fprintf(stderr, "Incoming handshake info_hash mismatch\n");
        shutdown(sockfd, SHUT_WR);
        return 0;
    }

    // 构造 handshake 响应
    unsigned char response[68];
    response[0] = 19;
    memcpy(response + 1, "BitTorrent protocol", 19);
    memset(response + 20, 0, 8);
    memcpy(response + 28, client_torrent(client)->info_hash, 20);
    memcpy(response + 48, client_peer_id(client), 20);
    ssize_t sent = send(sockfd, response, sizeof(response), 0);
    if (sent != sizeof(response)) {
        perror("send handshake response");
        return 0;
    }
    return 1;
}

/*
 * peer_listener_thread - 内部线程函数
 * 循环等待新连接，通过 select() 设置超时，
 * 接收到新连接后，调用 handle_incoming_handshake() 自动处理 handshake，
 * 若 handshake 成功则将连接加入 client 的 peer 列表，否则关闭该连接。
 */
static void *peer_listener_thread(void *arg) {
    struct peer_listener *listener = (struct peer_listener *)arg;
    while (listener->running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listener->sockfd, &readfds);

        // 设置超时时间为 5 秒
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        int sel = select(listener->sockfd + 1, &readfds, NULL, NULL, &tv);
        if (sel < 0) {
            perror("select");
            break;
        } else if (sel == 0) {
            // 超时，无新连接，继续等待
            continue;
        }

        int newfd = accept(listener->sockfd, NULL, NULL);
        if (newfd < 0) {
            if (listener->running)
                perror("accept");
            break;
        }

        // 自动处理 handshake：处理入站 handshake，若失败则关闭连接
        if (!handle_incoming_handshake(newfd, listener->client)) {
            close(newfd);
            continue;
        }
        // handshake 通过后，将该连接加入 client 的 peer 列表
        client_add_connected_peer(listener->client, newfd);
    }
    return NULL;
}

/*
 * peer_listener_new - 创建并启动一个监听器上下文
 * 1. 分配 peer_listener 内存，并保存关联的 client
 * 2. 创建监听 socket，绑定到 INADDR_ANY 和 client 指定的端口
 * 3. 调用 listen() 并启动监听线程 (peer_listener_thread)
 * 4. 返回创建成功的 peer_listener 指针，出错时释放资源并返回 NULL
 */
struct peer_listener *peer_listener_new(struct client *client) {
    if (!client)
        return NULL;

    struct peer_listener *listener = malloc(sizeof(struct peer_listener));
    if (!listener)
        return NULL;
    listener->client = client;
    listener->running = 1;

    // 创建监听 socket
    listener->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener->sockfd < 0) {
        perror("socket");
        free(listener);
        return NULL;
    }
    int opt = 1;
    if (setsockopt(listener->sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listener->sockfd);
        free(listener);
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    // 通过 accessor 获取 client 的监听端口
    addr.sin_port = htons(client_port(client));

    if (bind(listener->sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listener->sockfd);
        free(listener);
        return NULL;
    }

    if (listen(listener->sockfd, 5) < 0) {
        perror("listen");
        close(listener->sockfd);
        free(listener);
        return NULL;
    }

    // 启动监听线程
    if (pthread_create(&listener->thread, NULL, peer_listener_thread, listener) != 0) {
        perror("pthread_create");
        close(listener->sockfd);
        free(listener);
        return NULL;
    }

    // 可选：增加短暂延时，确保监听线程已启动
    usleep(100000);  // 100 毫秒

    return listener;
}

/*
 * peer_listener_free - 停止监听并释放所有资源
 * 1. 将 running 标志置为 0，并调用 shutdown() 使阻塞的 accept() 返回
 * 2. 关闭监听 socket，等待线程结束，然后释放结构体
 */
void peer_listener_free(struct peer_listener *server) {
    if (!server)
        return;
    server->running = 0;
    shutdown(server->sockfd, SHUT_RDWR);
    close(server->sockfd);
    pthread_join(server->thread, NULL);
    free(server);
}
