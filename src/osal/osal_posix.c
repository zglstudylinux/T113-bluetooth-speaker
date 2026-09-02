/*
 * osal_posix.c — OSAL 的 Linux/pthread 实现
 *
 * 队列：互斥锁保护的环形缓冲，满时覆盖最旧（丢最旧策略）。
 * 事件元素为定长值拷贝，无堆分配（player_event_t ~216B × 16 槽 ≈ 3.5KB）。
 */
#include "osal.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---------------- 队列 ---------------- */

struct osal_queue {
    uint16_t depth;         /* 槽数 */
    uint16_t item_size;     /* 单元素字节数 */
    uint16_t head;          /* 下一个可读槽 */
    uint16_t count;         /* 当前元素数 */
    uint8_t *buf;           /* depth * item_size */
    pthread_mutex_t lock;
};

osal_queue_t osal_queue_create(uint16_t depth, uint16_t item_size)
{
    struct osal_queue *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->buf = calloc(depth, item_size);
    if (!q->buf) { free(q); return NULL; }
    q->depth = depth;
    q->item_size = item_size;
    pthread_mutex_init(&q->lock, NULL);
    return q;
}

bool osal_queue_send(osal_queue_t q, const void *item)
{
    if (!q || !item) return false;
    pthread_mutex_lock(&q->lock);
    uint16_t tail = (q->head + q->count) % q->depth;
    memcpy(q->buf + (size_t)tail * q->item_size, item, q->item_size);
    if (q->count < q->depth) {
        q->count++;
    } else {
        q->head = (q->head + 1) % q->depth;   /* 满：丢最旧 */
    }
    pthread_mutex_unlock(&q->lock);
    return true;
}

bool osal_queue_recv(osal_queue_t q, void *out, uint32_t ms)
{
    if (!q || !out) return false;
    uint32_t waited = 0;
    do {
        pthread_mutex_lock(&q->lock);
        if (q->count > 0) {
            memcpy(out, q->buf + (size_t)q->head * q->item_size, q->item_size);
            q->head = (q->head + 1) % q->depth;
            q->count--;
            pthread_mutex_unlock(&q->lock);
            return true;
        }
        pthread_mutex_unlock(&q->lock);
        if (ms == 0) return false;
        usleep(1000);
        waited++;
    } while (waited < ms);
    return false;
}

void osal_queue_destroy(osal_queue_t q)
{
    if (!q) return;
    pthread_mutex_destroy(&q->lock);
    free(q->buf);
    free(q);
}

/* ---------------- 互斥锁 ---------------- */

struct osal_mutex {
    pthread_mutex_t lock;
};

osal_mutex_t osal_mutex_create(void)
{
    struct osal_mutex *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    pthread_mutex_init(&m->lock, NULL);
    return m;
}

void osal_mutex_lock(osal_mutex_t m)   { if (m) pthread_mutex_lock(&m->lock); }
void osal_mutex_unlock(osal_mutex_t m) { if (m) pthread_mutex_unlock(&m->lock); }

void osal_mutex_destroy(osal_mutex_t m)
{
    if (!m) return;
    pthread_mutex_destroy(&m->lock);
    free(m);
}

/* ---------------- 时间 ---------------- */

void osal_sleep_ms(uint32_t ms) { usleep((useconds_t)ms * 1000); }

uint32_t osal_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000);
}

/* ---------------- 日志 ---------------- */

static const char *level_str(osal_log_level_t lv)
{
    switch (lv) {
    case OSAL_LOG_ERROR: return "E";
    case OSAL_LOG_WARN:  return "W";
    case OSAL_LOG_INFO:  return "I";
    default:             return "D";
    }
}

void osal_log(osal_log_level_t lv, const char *tag, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "[%s][osal][%s] ", level_str(lv), tag ? tag : "-");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
