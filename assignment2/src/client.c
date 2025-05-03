#include <client.h>
#include <bencode.h>
#include <metainfo.h>
#include <peer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <curl/curl.h>
#include <ctype.h>
#include <fcntl.h>

/* 内部定义 client 结构体（隐藏实现细节） */
struct client {
    unsigned char peer_id[SHA_DIGEST_LENGTH]; // 20字节随机生成的 peer id
    uint16_t port;            // 监听端口
    size_t uploaded;          // 已上传字节数
    size_t downloaded;        // 已下载字节数（经过验证或实际文件大小）
    size_t left;              // 剩余需要下载的字节数
    struct metainfo_file *torrent; // 指向 torrent 文件结构
    int *peers;               // 动态数组，保存已连接 peer 的 socket fd
    size_t num_peers;         // 已连接 peer 数量
    pthread_t listener_thread; // 监听线程句柄
    int listener_running;     // 标志是否正在运行监听线程
    int listener_sockfd;      // 监听 socket
};

/*
 * 辅助函数 validate_piece：
 * 从文件中读取第 piece_index 个片段，读取长度为 piece_len，
 * full_piece_length 用于计算该片段在文件中的起始偏移（始终以 torrent->info.piece_length 为单位），
 * 计算 SHA1 后与 expected_hash 比较，相符则返回 piece_len，否则返回 0。
 */
static size_t validate_piece(FILE *fp, size_t piece_index, size_t piece_len, size_t full_piece_length, const char *expected_hash) {
    size_t offset = piece_index * full_piece_length;
    if (fseek(fp, offset, SEEK_SET) != 0)
        return 0;
    char *buffer = malloc(piece_len);
    if (!buffer)
        return 0;
    size_t read_bytes = fread(buffer, 1, piece_len, fp);
    if (read_bytes != piece_len) {
        free(buffer);
        return 0;
    }
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)buffer, piece_len, hash);
    free(buffer);
    if (memcmp(hash, expected_hash, SHA_DIGEST_LENGTH) == 0)
        return piece_len;
    else
        return 0;
}

/*
 * client_new: 创建并初始化一个 client 对象
 *
 * 1. 分配内存并生成随机的 20字节 peer id。
 * 2. 打开 torrent->info.name 指定的文件：
 *    - 如果文件存在，则读取实际大小 actual_size，并验证文件内容：
 *         若实际大小达到 torrent->info.length，则遍历所有片段验证；
 *         否则仅验证文件中完整存在的片段。
 *    - 如果文件不存在，则创建一个空文件，downloaded = 0。
 * 3. 设置 left = torrent->info.length - valid_downloaded。
 */
struct client *client_new(struct metainfo_file *torrent, uint16_t port) {
    struct client *c = malloc(sizeof(struct client));
    if (!c)
        return NULL;
    memset(c, 0, sizeof(*c));

    c->torrent = torrent;
    c->port = port;
    c->uploaded = 0;
    c->downloaded = 0;
    c->left = torrent->info.length;
    c->peers = NULL;
    c->num_peers = 0;
    c->listener_running = 0;
    c->listener_sockfd = -1;

    if (RAND_bytes(c->peer_id, SHA_DIGEST_LENGTH) != 1) {
        free(c);
        return NULL;
    }

    FILE *fp = fopen(torrent->info.name, "rb");
    if (fp) {
        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            free(c);
            return NULL;
        }
        long actual_size = ftell(fp);
        rewind(fp);
        if (actual_size < 0)
            actual_size = 0;
        size_t valid_downloaded = 0;
        size_t pieces = metainfo_file_pieces_count(torrent);
        if ((size_t)actual_size >= torrent->info.length) {
            /* 文件完整：验证所有片段 */
            for (size_t i = 0; i < pieces; i++) {
                size_t expected_size;
                if (i < pieces - 1)
                    expected_size = torrent->info.piece_length;
                else
                    expected_size = torrent->info.length - (pieces - 1) * torrent->info.piece_length;
                const char *expected_hash = metainfo_file_piece_hash(torrent, i);
                valid_downloaded += validate_piece(fp, i, expected_size, torrent->info.piece_length, expected_hash);
            }
        } else {
            /* 文件不完整：仅验证文件中完整存在的片段 */
            size_t pieces_full = (size_t)actual_size / torrent->info.piece_length;
            for (size_t i = 0; i < pieces_full; i++) {
                const char *expected_hash = metainfo_file_piece_hash(torrent, i);
                valid_downloaded += validate_piece(fp, i, torrent->info.piece_length, torrent->info.piece_length, expected_hash);
            }
        }
        fclose(fp);
        if (valid_downloaded > torrent->info.length)
            valid_downloaded = torrent->info.length;
        c->downloaded = valid_downloaded;
        c->left = torrent->info.length - valid_downloaded;
    } else {
        /* 文件不存在，则创建空文件 */
        fp = fopen(torrent->info.name, "wb");
        if (!fp) {
            free(c);
            return NULL;
        }
        fclose(fp);
        c->downloaded = 0;
        c->left = torrent->info.length;
    }
    return c;
}

/* 监听线程函数：循环调用 accept，将新连接加入 client->peers */
static void *client_listener_thread(void *arg) {
    struct client *c = (struct client *)arg;
    while (c->listener_running) {
        int newfd = accept(c->listener_sockfd, NULL, NULL);
        if (newfd < 0) {
            if (c->listener_running)
                perror("accept");
            break;
        }
        client_add_connected_peer(c, newfd);
    }
    return NULL;
}

/*
 * client_peer_listener_start: 创建监听 socket 并启动监听线程
 */
int client_peer_listener_start(struct client *client) {
    if (!client)
        return 0;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 0;
    }
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return 0;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(client_port(client));
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 0;
    }
    if (listen(sockfd, 5) < 0) {
        perror("listen");
        close(sockfd);
        return 0;
    }
    client->listener_sockfd = sockfd;
    client->listener_running = 1;
    if (pthread_create(&client->listener_thread, NULL, client_listener_thread, client) != 0) {
        perror("pthread_create");
        close(sockfd);
        client->listener_running = 0;
        return 0;
    }
    return 1;
}

/*
 * client_free: 释放 client 对象所有资源，包括关闭所有 peer 连接和监听服务
 */
void client_free(struct client *client) {
    if (!client)
        return;
    if (client->peers) {
        for (size_t i = 0; i < client->num_peers; i++) {
            close(client->peers[i]);
        }
        free(client->peers);
    }
    if (client->listener_running) {
        client->listener_running = 0;
        shutdown(client->listener_sockfd, SHUT_RDWR);
        close(client->listener_sockfd);
        pthread_join(client->listener_thread, NULL);
    }
    free(client);
}

const unsigned char *client_peer_id(struct client *client) {
    return client ? client->peer_id : NULL;
}

uint16_t client_port(struct client *client) {
    return client ? client->port : 0;
}

size_t client_uploaded(struct client *client) {
    return client ? client->uploaded : 0;
}

size_t client_downloaded(struct client *client) {
    return client ? client->downloaded : 0;
}

size_t client_left(struct client *client) {
    return client ? client->left : 0;
}

const struct metainfo_file *client_torrent(struct client *client) {
    return client ? client->torrent : NULL;
}

/* 
 * URL编码函数：对二进制数据进行 URL 编码，只保留安全字符（A-Z, a-z, 0-9, '-', '.', '_', '~'）。
 */
static char *url_encode(const unsigned char *data, size_t len) {
    const char *safe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    char *enc = malloc(len * 3 + 1);
    if (!enc)
        return NULL;
    char *p = enc;
    for (size_t i = 0; i < len; i++) {
        if (strchr(safe, data[i]) != NULL) {
            *p++ = data[i];
        } else {
            sprintf(p, "%%%02X", data[i]);
            p += 3;
        }
    }
    *p = '\0';
    return enc;
}

/* 全局保存最后构造的 tracker URL，供测试使用 */
static char last_tracker_url[1024] = {0};

const char *client_tracker_url(void) {
    return last_tracker_url;
}

/* 用于存储 HTTP 响应数据 */
struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr)
        return 0;
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = '\0';
    return realsize;
}

/*
 * client_tracker_connect: 使用 libcurl 实现 tracker 连接
 *
 * 构造 URL 使用 torrent->announce，并附加查询参数：
 * - 如果文件不完整（client_left(client) > 0），构造 URL 时使用 event="stopped"，
 *   URL 包含 info_hash、peer_id、port、uploaded、downloaded、left 和 event="stopped"，
 *   并直接返回 0，表示 cleanup 状态。
 * - 如果文件已完整（client_left(client) == 0），构造 URL 时使用 event="started"，包含完整参数，
 *   发起 HTTP GET 请求（超时 10 秒），解析 tracker 返回的 bencoded 数据，
 *   如果解析成功且无 "failure reason"，则返回 tracker 响应中的 interval（若不存在则默认 30 秒），
 *   否则返回 0 表示失败。
 *
 * 构造的 URL 保存到全局变量 last_tracker_url 供测试检查，通过 client_tracker_url() 访问。
 */
int client_tracker_connect(struct client *client) {
    const char *base_url = client_torrent(client)->announce;
    char *encoded_info_hash = url_encode(client_torrent(client)->info_hash, 20);
    char *encoded_peer_id = url_encode(client_peer_id(client), 20);
    if (!encoded_info_hash || !encoded_peer_id) {
        free(encoded_info_hash);
        free(encoded_peer_id);
        return 0;
    }
    char url[1024];
    if (client_left(client) > 0) {
        /* 文件不完整：发送 cleanup 请求，使用 event="stopped" */
        snprintf(url, sizeof(url),
                 "%s?info_hash=%s&peer_id=%s&port=%u&uploaded=%zu&downloaded=%zu&left=%zu&event=stopped",
                 base_url,
                 encoded_info_hash,
                 encoded_peer_id,
                 client_port(client),
                 client_uploaded(client),
                 client_downloaded(client),
                 client_left(client));
        // 保存 URL 供测试检查
        strncpy(last_tracker_url, url, sizeof(last_tracker_url) - 1);
        last_tracker_url[sizeof(last_tracker_url) - 1] = '\0';
        free(encoded_info_hash);
        free(encoded_peer_id);
        return 0;
    } else {
        /* 文件已完整：发送正常 tracker 请求，使用 event="started" */
        snprintf(url, sizeof(url),
                 "%s?info_hash=%s&peer_id=%s&port=%u&uploaded=%zu&downloaded=%zu&left=%zu&event=started",
                 base_url,
                 encoded_info_hash,
                 encoded_peer_id,
                 client_port(client),
                 client_uploaded(client),
                 client_downloaded(client),
                 client_left(client));
        strncpy(last_tracker_url, url, sizeof(last_tracker_url) - 1);
        last_tracker_url[sizeof(last_tracker_url) - 1] = '\0';
        free(encoded_info_hash);
        free(encoded_peer_id);
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl_easy_init failed\n");
        return 0;
    }
    struct MemoryStruct {
        char *memory;
        size_t size;
    } chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // 超时 10 秒

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return 0;
    }
    // 解析 tracker 返回的 bencoded 数据
    struct bencode_value tracker_response;
    size_t decoded = bencode_value_decode(&tracker_response, chunk.memory, chunk.size);
    free(chunk.memory);
    curl_easy_cleanup(curl);
    if (decoded == 0) {
        fprintf(stderr, "Failed to decode tracker response.\n");
        return 0;
    }
    const struct bencode_pair *failure = bencode_map_lookup(&tracker_response, "failure reason");
    if (failure) {
        fprintf(stderr, "Tracker failure: %s\n", bencode_value_str(&failure->value));
        bencode_value_free(&tracker_response);
        return 0;
    }
    int interval = 30;
    const struct bencode_pair *interval_pair = bencode_map_lookup(&tracker_response, "interval");
    if (interval_pair && interval_pair->value.type == BENCODE_INT) {
        interval = (int) interval_pair->value.as.int_value;
        if (interval <= 0)
            interval = 30;
    }
    bencode_value_free(&tracker_response);
    return interval;
}




/* 将新连接的 peer 添加到 client->peers 数组 */
void client_add_connected_peer(struct client *client, int sockfd) {
    if (!client)
        return;
    int *new_peers = realloc(client->peers, (client->num_peers + 1) * sizeof(int));
    if (new_peers) {
        client->peers = new_peers;
        client->peers[client->num_peers] = sockfd;
        client->num_peers++;
    } else {
        close(sockfd);
    }
}

/*
 * client_add_bencoded_peer_list:
 * 遍历 tracker 返回的 bencoded peer 列表，
 * 对于每个 peer 条目提取 "ip"、"port" 和 "peer id" 字段，
 * 然后调用 peer_connect 建立连接；
 * 如果连接成功，将该连接加入 client->peers 数组。
 */
void client_add_bencoded_peer_list(struct client *client, const struct bencode_value *peers) {
    if (!client || !peers || peers->type != BENCODE_LIST)
         return;
    size_t count = peers->as.list_value.count;
    for (size_t i = 0; i < count; i++) {
         const struct bencode_value *peer_val = &peers->as.list_value.values[i];
         if (peer_val->type != BENCODE_MAP)
             continue;
         const struct bencode_pair *id_pair = bencode_map_lookup(peer_val, "peer id");
         const struct bencode_pair *ip_pair = bencode_map_lookup(peer_val, "ip");
         const struct bencode_pair *port_pair = bencode_map_lookup(peer_val, "port");
         if (!id_pair || !ip_pair || !port_pair)
             continue;
         
         char ip[128] = {0};
         size_t ip_len = ip_pair->value.as.str_value.len;
         if (ip_len >= sizeof(ip))
             ip_len = sizeof(ip) - 1;
         memcpy(ip, ip_pair->value.as.str_value.str, ip_len);
         ip[ip_len] = '\0';
         
         int port_val = (int) port_pair->value.as.int_value;
         if (port_val < 0 || port_val > 65535)
             continue;
         
         printf("Peer found: id=%.*s, ip=%s, port=%d\n",
                (int) id_pair->value.as.str_value.len, id_pair->value.as.str_value.str,
                ip, port_val);
         
         struct peer p;
         if (peer_connect(&p, client, ip, (uint16_t) port_val)) {
              client_add_connected_peer(client, p.sockfd);
         } else {
              printf("Failed to connect to peer: %s:%d\n", ip, port_val);
         }
    }
}
