/*
 * osal_test.c — OSAL 队列 host 自测（architecture.md 阶段2验证）
 *
 * 编译运行（host gcc）：
 *   cmake -B build-host && cmake --build build-host && ctest --test-dir build-host --output-on-failure
 * 或手动：
 *   gcc -Wall -Wextra -Werror -O2 -pthread \
 *       osal/osal_posix.c osal/osal_test.c -o /tmp/osal_test && /tmp/osal_test
 *
 * 注意：用自定义 CHECK 宏而非 assert()——Release 下 NDEBUG 会把 assert
 * 变空操作，导致"变量只被 assert 使用"误报 unused / 检查被静默跳过。
 *
 * 场景：
 *   1. 单线程收发顺序正确 + 空队列非阻塞/超时路径
 *   2. 多生产者并发灌 3000 事件（player_event_t 真实大小），消费到的事件完好、不卡死
 *   3. 满队列丢最旧：灌 depth*2 个，留存的是最新 depth 个且序号连续
 */
#include "../core/player_types.h"
#include "osal.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define DEPTH 16

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static int test_order(void)
{
    osal_queue_t q = osal_queue_create(DEPTH, sizeof(int));
    CHECK(q != NULL);
    for (int i = 0; i < 5; i++) CHECK(osal_queue_send(q, &i));
    for (int i = 0; i < 5; i++) {
        int v = -1;
        CHECK(osal_queue_recv(q, &v, 0));
        CHECK(v == i);
    }
    int v = -1;
    CHECK(!osal_queue_recv(q, &v, 0));   /* 空：非阻塞立即 false */
    CHECK(!osal_queue_recv(q, &v, 30));  /* 空：30ms 超时 false */
    CHECK(v == -1);                      /* 未取到不改写 out */
    osal_queue_destroy(q);
    printf("PASS test_order\n");
    return 0;
}

/* ---- 并发压测 ----
 * 丢最旧队列下消费数 ≤ 生产数（多生产者会在满时互相挤掉），
 * 正确性标准：①消费到的都是完好 player_event_t；②消费者最终能抽干队列
 * （生产者都结束后 recv 到空即守恒）；③不会卡死。 */
#define N_PROD 4
#define PER_PROD 750
static osal_queue_t g_q;

static void *producer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < PER_PROD; i++) {
        player_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = PLAYER_EVT_POS;
        ev.pos_ms = id * 10000 + i;
        osal_queue_send(g_q, &ev);
    }
    return NULL;
}

static int test_concurrent(void)
{
    g_q = osal_queue_create(DEPTH, sizeof(player_event_t));
    CHECK(g_q != NULL);
    pthread_t th[N_PROD];
    for (long i = 0; i < N_PROD; i++) pthread_create(&th[i], NULL, producer, (void *)i);

    player_event_t ev;
    int consumed = 0;
    int idle_rounds = 0;
    while (idle_rounds < 100) {          /* 生产者未全结束时空转100轮即视为卡死 */
        if (osal_queue_recv(g_q, &ev, 5)) {
            CHECK(ev.type == PLAYER_EVT_POS);
            CHECK(ev.pos_ms >= 0 && ev.pos_ms < N_PROD * 10000 + PER_PROD);
            consumed++;
            idle_rounds = 0;
        } else {
            idle_rounds++;
        }
    }
    for (int i = 0; i < N_PROD; i++) pthread_join(th[i], NULL);
    /* 生产者已全部结束：队列应已抽干（收到的 + 丢最旧的 = 发出的） */
    CHECK(!osal_queue_recv(g_q, &ev, 0));
    osal_queue_destroy(g_q);
    printf("PASS test_concurrent (consumed %d of %d, rest dropped-oldest)\n",
           consumed, N_PROD * PER_PROD);
    return 0;
}

static int test_drop_oldest(void)
{
    osal_queue_t q = osal_queue_create(DEPTH, sizeof(int));
    CHECK(q != NULL);
    for (int i = 0; i < DEPTH * 2; i++) osal_queue_send(q, &i);
    /* 深度 16 灌 32 个：留存的是 16..31 */
    for (int i = 0; i < DEPTH; i++) {
        int v = -1;
        CHECK(osal_queue_recv(q, &v, 0));
        CHECK(v == DEPTH + i);
    }
    int tail = -1;
    CHECK(!osal_queue_recv(q, &tail, 0));   /* 抽干 */
    CHECK(tail == -1);
    osal_queue_destroy(q);
    printf("PASS test_drop_oldest\n");
    return 0;
}

int main(void)
{
    printf("player_event_t size = %zu bytes\n", sizeof(player_event_t));
    if (test_order() || test_concurrent() || test_drop_oldest())
        return 1;
    printf("ALL OSAL TESTS PASSED\n");
    return 0;
}
