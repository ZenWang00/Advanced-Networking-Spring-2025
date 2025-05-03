#include <tracker_connection.h>
#include <client.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

/* 定义 tracker_connection 结构体，隐藏实现细节 */
struct tracker_connection {
    struct client *client;    // 关联的 client
    pthread_t thread;         // 轮询线程句柄
    int running;              // 运行标志
};

/* 内部线程函数：循环向 tracker 发送请求 */
static void *tracker_connection_thread(void *arg) {
    struct tracker_connection *tc = (struct tracker_connection *)arg;
    while (tc->running) {
        int interval = client_tracker_connect(tc->client);
        if (interval == 0) {
            fprintf(stderr, "Tracker connection failed.\n");
            interval = 3;
        } else {
            printf("Tracker polled successfully. Interval: %d seconds\n", interval);
        }
        sleep(interval);
    }
    return NULL;
}

/**
 * tracker_connection_new - 分配 tracker_connection 结构体并启动轮询线程
 */
struct tracker_connection *tracker_connection_new(struct client *client) {
    if (!client)
        return NULL;
    struct tracker_connection *tc = malloc(sizeof(struct tracker_connection));
    if (!tc)
        return NULL;
    tc->client = client;
    tc->running = 1;
    if (pthread_create(&tc->thread, NULL, tracker_connection_thread, tc) != 0) {
        perror("pthread_create");
        free(tc);
        return NULL;
    }
    return tc;
}

/**
 * tracker_connection_free - 停止轮询线程并释放资源
 */
void tracker_connection_free(struct tracker_connection *connection) {
    if (!connection)
        return;
    connection->running = 0;
    pthread_join(connection->thread, NULL);
    free(connection);
}
