/* C-A06 추가 연습 — 한 번에 검증
 * 연습 1: sizeof_field / offsetofend 와 "구멍 없는 헤더" 검사 (Linux 매크로 이름 그대로)
 * 연습 2: 소프트 타이머 — 서로 다른 두 주인 타입(LED, 워치독)에 같은 soft_timer 를 내장
 * 연습 3: 드라이버 "상속" — struct device 를 품은 i2c_sensor / spi_flash, to_xxx() + ops 테이블
 * 연습 4: offset 을 런타임 값으로 들고 다니는 범용 침입형 큐
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- 본문(solution.c)의 매크로 그대로 ---- */
#define MY_OFFSETOF_ARENA_SIZE  1024u
#define MY_OFFSETOF_ARENA_ALIGN 64
static _Alignas(MY_OFFSETOF_ARENA_ALIGN) unsigned char my_offsetof_arena_[MY_OFFSETOF_ARENA_SIZE];
#define MY_OFFSETOF(type, member)                                                              \
    (sizeof(char[(sizeof(type) <= MY_OFFSETOF_ARENA_SIZE &&                                    \
                  _Alignof(type) <= MY_OFFSETOF_ARENA_ALIGN) ? 1 : -1]) - 1u +                 \
     (size_t)((unsigned char *)&((type *)(void *)my_offsetof_arena_)->member - my_offsetof_arena_))
#define container_of(ptr, type, member)                                                        \
    ((void)sizeof((ptr) == &((type *)0)->member),                                              \
     (type *)(void *)((char *)(ptr) - MY_OFFSETOF(type, member)))
#define QUAL_VOID_PTR_(p) (1 ? (p) : (void *)(p))
#define container_of_const(ptr, type, member)                                                  \
    _Generic(QUAL_VOID_PTR_(ptr),                                                              \
        const void *: ((void)sizeof((ptr) == &((type *)0)->member),                            \
                       (const type *)(const void *)((const char *)(ptr) - MY_OFFSETOF(type, member))), \
        default: container_of(ptr, type, member))

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ================= 연습 1: sizeof_field / offsetofend ================= */
#define sizeof_field(T, m)   sizeof(((T *)0)->m)                    /* 평가 안 됨 → 널이어도 안전, ICE */
#define offsetofend(T, m)    (offsetof(T, m) + sizeof_field(T, m))  /* ICE: _Static_assert 에 쓸 수 있다 */
#define MY_OFFSETOFEND(T, m) (MY_OFFSETOF(T, m) + sizeof_field(T, m))  /* 런타임용 (ICE 아님) */

struct wire_hdr { uint8_t ver; uint8_t type; uint16_t len; uint32_t seq; uint32_t crc; };
struct bad_hdr  { uint8_t ver; uint32_t seq; uint16_t len; };

/* 컴파일 타임 계약: 필드가 빈틈없이 이어진다 (표준 offsetof 라서 가능) */
_Static_assert(offsetofend(struct wire_hdr, ver)  == offsetof(struct wire_hdr, type), "gap after ver");
_Static_assert(offsetofend(struct wire_hdr, type) == offsetof(struct wire_hdr, len),  "gap after type");
_Static_assert(offsetofend(struct wire_hdr, len)  == offsetof(struct wire_hdr, seq),  "gap after len");
_Static_assert(offsetofend(struct wire_hdr, seq)  == offsetof(struct wire_hdr, crc),  "gap after seq");
_Static_assert(offsetofend(struct wire_hdr, crc)  == sizeof(struct wire_hdr),         "tail padding");

static size_t bad_hdr_padding(void) {               /* 패딩 바이트 = sizeof - 필드 크기 합 */
    return sizeof(struct bad_hdr) - (sizeof_field(struct bad_hdr, ver) + sizeof_field(struct bad_hdr, seq) +
                                     sizeof_field(struct bad_hdr, len));
}

/* ================= 연습 2: 소프트 타이머 ================= */
struct soft_timer {
    struct soft_timer *next;
    uint32_t expiry, period;
    void (*cb)(struct soft_timer *t);
};
static struct soft_timer *g_timers;
static void timer_add(struct soft_timer *t, uint32_t now, uint32_t period, void (*cb)(struct soft_timer *)) {
    t->period = period;
    t->expiry = now + period;
    t->cb = cb;
    t->next = g_timers;
    g_timers = t;
}
static void timers_tick(uint32_t now) {             /* SysTick 마다 (여기선 직접 호출) */
    for (struct soft_timer *t = g_timers; t; t = t->next)
        if ((int32_t)(now - t->expiry) >= 0) {      /* 랩어라운드 안전 비교 */
            t->expiry += t->period;
            t->cb(t);
        }
}
struct led_blinker { uint8_t pin; uint8_t level; uint32_t toggles; struct soft_timer tmr; };
struct wdg_kicker  { uint32_t kicks; uint32_t fake_kr; struct soft_timer tmr; };
static void led_cb(struct soft_timer *t) {
    struct led_blinker *b = container_of(t, struct led_blinker, tmr);
    b->level ^= 1u;
    b->toggles++;
}
static void wdg_cb(struct soft_timer *t) {
    struct wdg_kicker *w = container_of(t, struct wdg_kicker, tmr);
    w->fake_kr = 0xAAAAu;                           /* STM32 IWDG_KR 의 리로드 키 */
    w->kicks++;
}

/* ================= 연습 3: 드라이버 "상속" ================= */
struct device;
struct dev_ops {
    int (*read)(struct device *d, uint8_t *buf, int n);
    uint32_t (*id)(const struct device *d);
};
struct device { const char *name; const struct dev_ops *ops; uint32_t reads; };
struct i2c_sensor { uint8_t addr; uint16_t last_raw; struct device dev; };
struct spi_flash  { uint32_t jedec_id; uint8_t cs_pin; struct device dev; uint8_t page[4]; };
#define to_i2c_sensor(d)       container_of(d, struct i2c_sensor, dev)
#define to_i2c_sensor_const(d) container_of_const(d, struct i2c_sensor, dev)
#define to_spi_flash(d)        container_of(d, struct spi_flash, dev)
#define to_spi_flash_const(d)  container_of_const(d, struct spi_flash, dev)

static int i2c_read(struct device *d, uint8_t *buf, int n) {
    struct i2c_sensor *s = to_i2c_sensor(d);
    s->last_raw = (uint16_t)(0x1200u | s->addr);    /* 가짜 측정값 */
    if (n >= 2) { buf[0] = (uint8_t)(s->last_raw >> 8); buf[1] = (uint8_t)s->last_raw; }
    d->reads++;
    return n >= 2 ? 2 : 0;
}
static uint32_t i2c_id(const struct device *d) { return to_i2c_sensor_const(d)->addr; }
static int spi_read(struct device *d, uint8_t *buf, int n) {
    struct spi_flash *f = to_spi_flash(d);
    int k = n < 4 ? n : 4;
    memcpy(buf, f->page, (size_t)k);
    d->reads++;
    return k;
}
static uint32_t spi_id(const struct device *d) { return to_spi_flash_const(d)->jedec_id; }
static const struct dev_ops i2c_ops = { i2c_read, i2c_id };
static const struct dev_ops spi_ops = { spi_read, spi_id };
static int dev_read(struct device *d, uint8_t *buf, int n) { return d->ops->read(d, buf, n); }   /* 범용 API */

/* ================= 연습 4: 런타임 offset 범용 큐 ================= */
struct iq_node { struct iq_node *next; };
struct iqueue { struct iq_node *head, *tail; size_t off; };   /* off = 객체 안 노드 위치 */
static void iq_init(struct iqueue *q, size_t off) { q->head = q->tail = NULL; q->off = off; }
static void iq_push(struct iqueue *q, void *obj) {
    struct iq_node *n = (struct iq_node *)(void *)((char *)obj + q->off);
    n->next = NULL;
    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
}
static void *iq_pop(struct iqueue *q) {
    struct iq_node *n = q->head;
    if (!n) return NULL;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    return (char *)n - q->off;                      /* container_of 의 "런타임 offset" 판 */
}
struct pkt   { uint16_t len; uint8_t data[6]; struct iq_node qn; };
struct event { uint32_t code; uint64_t ts; uint8_t prio; struct iq_node qn; };

int main(void) {
    /* 연습 1 */
    CHECK(sizeof_field(struct wire_hdr, len) == 2u && offsetofend(struct wire_hdr, seq) == 8u);
    CHECK(MY_OFFSETOFEND(struct wire_hdr, crc) == sizeof(struct wire_hdr));
    CHECK(MY_OFFSETOFEND(struct bad_hdr, ver) != MY_OFFSETOF(struct bad_hdr, seq));   /* 구멍 발견 */
    CHECK(bad_hdr_padding() == sizeof(struct bad_hdr) - 7u && bad_hdr_padding() > 0u);

    /* 연습 2: LED 3 tick 주기, 워치독 5 tick 주기, 15 tick 진행 */
    static struct led_blinker led = { .pin = 13 };
    static struct wdg_kicker wdg;
    timer_add(&led.tmr, 0, 3, led_cb);
    timer_add(&wdg.tmr, 0, 5, wdg_cb);
    for (uint32_t now = 1; now <= 15; now++) timers_tick(now);
    CHECK(led.toggles == 5u && led.level == 1u && led.pin == 13u);
    CHECK(wdg.kicks == 3u && wdg.fake_kr == 0xAAAAu);

    /* 연습 3 */
    static struct i2c_sensor tmp = { .addr = 0x48, .dev = { "tmp117", &i2c_ops, 0 } };
    static struct spi_flash fl = { .jedec_id = 0xEF4018u, .cs_pin = 4, .dev = { "w25q", &spi_ops, 0 },
                                   .page = { 0xDE, 0xAD, 0xBE, 0xEF } };
    struct device *devs[2] = { &tmp.dev, &fl.dev };
    uint8_t buf[4] = {0};
    CHECK(dev_read(devs[0], buf, 4) == 2 && buf[0] == 0x12u && buf[1] == 0x48u && tmp.last_raw == 0x1248u);
    CHECK(dev_read(devs[1], buf, 4) == 4 && buf[3] == 0xEFu && fl.dev.reads == 1u);
    const struct device *cd = devs[1];
    CHECK(devs[0]->ops->id(devs[0]) == 0x48u && cd->ops->id(cd) == 0xEF4018u);
    CHECK(to_spi_flash_const(cd) == &fl && to_i2c_sensor(devs[0]) == &tmp);

    /* 연습 4: 두 타입, 두 큐, 같은 큐 코드 */
    struct iqueue pq, eq;
    iq_init(&pq, offsetof(struct pkt, qn));
    iq_init(&eq, MY_OFFSETOF(struct event, qn));
    static struct pkt p[3] = { { .len = 10 }, { .len = 20 }, { .len = 30 } };
    static struct event e[2] = { { .code = 7 }, { .code = 9 } };
    iq_push(&pq, &p[1]); iq_push(&eq, &e[1]); iq_push(&pq, &p[0]); iq_push(&pq, &p[2]); iq_push(&eq, &e[0]);
    struct pkt *a = iq_pop(&pq), *b = iq_pop(&pq), *c = iq_pop(&pq);
    CHECK(a == &p[1] && b == &p[0] && c == &p[2] && iq_pop(&pq) == NULL);
    struct event *x = iq_pop(&eq), *y = iq_pop(&eq);
    CHECK(x == &e[1] && x->code == 9u && y == &e[0] && iq_pop(&eq) == NULL);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
