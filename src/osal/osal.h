/*
 * osal.h — OS 抽象层（core/services/ui 依赖本接口，不直接依赖 OS API）
 *
 * Linux 实现 osal_posix.c（pthread）；裸机/RTOS 实现后续补
 * osal_none.c / osal_freertos.c，接口同构，上层零改动
 * （见 docs/architecture.md §4.4/§10）。
 */
#ifndef OSAL_H
#define OSAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 队列（定长元素，多生产者单消费者） ----------------
 * 满时丢最旧（进度/音量类事件"新值覆盖旧值"是正确语义）。
 * 裸机实现 = 关中断环形缓冲，接口不变。 */
typedef struct osal_queue *osal_queue_t;

osal_queue_t osal_queue_create(uint16_t depth, uint16_t item_size);
/* 非阻塞发送；返回 false = 队列不可用（创建失败/句柄空），元素定长拷贝 */
bool osal_queue_send(osal_queue_t q, const void *item);
/* ms=0 非阻塞取；返回 false = 空/超时 */
bool osal_queue_recv(osal_queue_t q, void *out, uint32_t ms);
void osal_queue_destroy(osal_queue_t q);

/* ---------------- 互斥锁 ---------------- */
typedef struct osal_mutex *osal_mutex_t;

osal_mutex_t osal_mutex_create(void);
void osal_mutex_lock(osal_mutex_t m);
void osal_mutex_unlock(osal_mutex_t m);
void osal_mutex_destroy(osal_mutex_t m);

/* ---------------- 时间 ---------------- */
void     osal_sleep_ms(uint32_t ms);
uint32_t osal_now_ms(void);   /* 单调毫秒（裸机上= tick 计数） */

/* ---------------- 日志 ---------------- */
typedef enum { OSAL_LOG_ERROR = 0, OSAL_LOG_WARN, OSAL_LOG_INFO, OSAL_LOG_DEBUG } osal_log_level_t;

void osal_log(osal_log_level_t lv, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#ifdef __cplusplus
}
#endif

#endif /* OSAL_H */
