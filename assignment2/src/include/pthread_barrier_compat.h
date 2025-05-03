#ifndef PTHREAD_BARRIER_COMPAT_H
#define PTHREAD_BARRIER_COMPAT_H

#include <pthread.h>
#include <errno.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned count;
    unsigned tripCount;
} pthread_barrier_t;

static inline int pthread_barrier_init(pthread_barrier_t *barrier, const void *attr, unsigned count) {
    if (count == 0)
        return EINVAL;
    barrier->tripCount = count;
    barrier->count = 0;
    int ret;
    if ((ret = pthread_mutex_init(&barrier->mutex, NULL)) != 0)
        return ret;
    if ((ret = pthread_cond_init(&barrier->cond, NULL)) != 0)
        return ret;
    return 0;
}

static inline int pthread_barrier_wait(pthread_barrier_t *barrier) {
    pthread_mutex_lock(&barrier->mutex);
    barrier->count++;
    if (barrier->count >= barrier->tripCount) {
        barrier->count = 0;
        pthread_cond_broadcast(&barrier->cond);
        pthread_mutex_unlock(&barrier->mutex);
        return 1; // 通常最后一个线程返回一个特殊值
    } else {
        pthread_cond_wait(&barrier->cond, &barrier->mutex);
        pthread_mutex_unlock(&barrier->mutex);
        return 0;
    }
}

static inline int pthread_barrier_destroy(pthread_barrier_t *barrier) {
    int ret1 = pthread_mutex_destroy(&barrier->mutex);
    int ret2 = pthread_cond_destroy(&barrier->cond);
    return ret1 ? ret1 : ret2;
}

#endif /* PTHREAD_BARRIER_COMPAT_H */
