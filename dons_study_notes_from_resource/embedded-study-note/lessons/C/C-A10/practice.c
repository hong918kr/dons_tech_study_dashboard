/* C-A10 추가 연습 — legacy_driver.c 의 나머지 발견 사항을 같은 방식(동등성 + 의도된 차이)으로 고친다
 *
 *   연습 1  8진수 ODR 테이블과 8비트 분주기 잘림 (MISRA-style 7.1 / 10.3)
 *   연습 2  "u 붙이기" 기계적 수정의 함정: n - 1u (10.4)
 *   연습 3  한 식 안의 두 레지스터 읽기: 평가 순서 (13.2) — 래치형 TEMP_H/TEMP_L
 *   연습 4  재귀 gcd → 반복 (17.2)
 *   연습 5  malloc/strcpy → 정적 풀 + 잘리는 이름 복사 (21.3, CERT STR31-C)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 연습 1: ODR 테이블 ---------------- */
#define BASE_CLK_HZ 3200                     /* 레거시 매크로 그대로 (int) */
#define ODR_BASE_HZ 3200u                    /* 새 코드용 (unsigned) */
static const int k_odr_hz_legacy[] = { 001, 002, 005, 010, 020, 050, 100, 200 };   /* 레거시 그대로 */
static uint8_t g_reg_odr_div;                                                     /* 8비트 레지스터 */

static int old_gcd(int a, int b) { return b ? old_gcd(b, a % b) : a; }            /* 레거시 그대로 */
static int old_set_odr(int idx)                                                   /* 레거시 핵심부 */
{
    int hz, div, g;
    if (idx < 0 || idx > 7)
        return -1;
    hz = k_odr_hz_legacy[idx];
    g = old_gcd(BASE_CLK_HZ, hz);
    div = BASE_CLK_HZ / g;
    g_reg_odr_div = div;            /* 레거시: bus_write(REG_ODR_DIV, div) — uint8_t 로 조용히 잘림 */
    return div;
}

/* 답: 8진수 없이 "지금 동작하는 값" 을 그대로 십진수로. 의도(10/20/50 Hz)로 고치는 건 별도 결정. */
#define ODR_COUNT 8u
static const uint16_t k_odr_hz[ODR_COUNT] = { 1u, 2u, 5u, 8u, 16u, 40u, 100u, 200u };

/* 답: 분주기가 레지스터(8비트)에 들어가지 않거나 나누어떨어지지 않으면 거부 */
static int odr_div_checked(uint32_t idx, uint8_t *div_out) {
    int ret = -1;
    if (idx < ODR_COUNT) {
        const uint32_t hz = k_odr_hz[idx];
        const uint32_t div = ODR_BASE_HZ / hz;
        if (((ODR_BASE_HZ % hz) == 0u) && (div <= UINT8_MAX)) {
            *div_out = (uint8_t)div;
            ret = 0;
        }
    }
    return ret;
}

/* ---------------- 연습 2: n - 1u ---------------- */
static unsigned payload_len_mechanical(uint8_t n) { return n - 1u; }   /* 경고만 없앤 "수정" */
static uint8_t payload_len(uint8_t n) {                                 /* 답 */
    return (n > 0u) ? (uint8_t)(n - 1u) : 0u;
}

/* ---------------- 연습 3: 래치형 16비트 레지스터 ---------------- */
/* 센서 값은 읽을 때마다 +0x0101 씩 변한다. TEMP_H 를 읽는 순간 하위 바이트가 래치에 복사된다.
 * TEMP_L 은 래치를 돌려준다 → H 를 먼저 읽어야 같은 순간의 H:L 쌍이 된다.
 * (STM32 RTC 는 TR→DR 순서, AVR TCNT1 은 L→H 순서 — 규칙은 데이터시트가 정한다.) */
#define REG_TEMP_H 0x10u
#define REG_TEMP_L 0x11u
static uint16_t g_live = 0x12FFu;
static uint8_t g_latch_l = 0xAAu;             /* 이전 읽기에서 남은 래치 값 */
static uint8_t bus_read(uint8_t reg) {
    uint8_t v;
    if (reg == REG_TEMP_H) { v = (uint8_t)(g_live >> 8); g_latch_l = (uint8_t)(g_live & 0xFFu); }
    else { v = g_latch_l; }
    g_live = (uint16_t)(g_live + 0x0101u);
    return v;
}
static uint16_t read_temp_raw(void) {                                   /* 답 */
    const uint8_t hi = bus_read(REG_TEMP_H);      /* 문장을 나눠 순서를 고정한다 (; 는 시퀀스 포인트) */
    const uint8_t lo = bus_read(REG_TEMP_L);
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}
static uint16_t read_temp_l_first(void) {                               /* 순서가 뒤집혔을 때 */
    const uint8_t lo = bus_read(REG_TEMP_L);
    const uint8_t hi = bus_read(REG_TEMP_H);
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

/* ---------------- 연습 4: gcd ---------------- */
static uint32_t gcd_iter(uint32_t a, uint32_t b) {                      /* 답 */
    while (b != 0u) {
        const uint32_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

/* ---------------- 연습 5: 정적 풀 ---------------- */
#define LS_MAX_DEV  4u
#define LS_NAME_LEN 8u
typedef struct { char name[LS_NAME_LEN]; uint8_t addr; bool used; } ls_dev_t;
static ls_dev_t g_pool[LS_MAX_DEV];

static ls_dev_t *pool_open(const char *name, uint8_t addr) {            /* 답 */
    ls_dev_t *d = NULL;
    for (uint32_t i = 0u; (i < LS_MAX_DEV) && (d == NULL); i++) {
        if (!g_pool[i].used) {
            d = &g_pool[i];
        }
    }
    if (d != NULL) {
        size_t k = 0u;
        (void)memset(d, 0, sizeof *d);
        while ((k < (LS_NAME_LEN - 1u)) && (name[k] != '\0')) {     /* 잘라서 복사, 항상 '\0' 종료 */
            d->name[k] = name[k];
            k++;
        }
        d->addr = addr;
        d->used = true;
    }
    return d;
}
static int pool_close(ls_dev_t *d) {                                    /* 답 */
    int ret = -1;
    const uintptr_t base = (uintptr_t)&g_pool[0];
    const uintptr_t p = (uintptr_t)d;
    if ((d != NULL) && (p >= base) && (p < (base + sizeof g_pool)) &&
        (((p - base) % sizeof g_pool[0]) == 0u) && d->used) {
        d->used = false;
        ret = 0;
    }
    return ret;
}

int main(void) {
    /* 연습 1 */
    bool same = true;
    for (uint32_t i = 0u; i < ODR_COUNT; i++) same = same && (k_odr_hz[i] == (uint16_t)k_odr_hz_legacy[i]);
    CHECK(same);                                                     /* 8진수 제거해도 값은 그대로 */
    CHECK(k_odr_hz_legacy[3] == 8 && k_odr_hz_legacy[4] == 16 && k_odr_hz_legacy[5] == 40);   /* "10/20/50" 아님 */
    uint8_t div = 0u;
    for (int idx = 4; idx < 8; idx++) {                              /* 들어가는 구간: 레거시와 같은 레지스터 값 */
        (void)old_set_odr(idx);
        CHECK(odr_div_checked((uint32_t)idx, &div) == 0 && div == g_reg_odr_div);
    }
    static const uint8_t legacy_reg[4] = { 128u, 64u, 128u, 144u };  /* 3200,1600,640,400 의 하위 8비트 */
    bool trunc_pinned = true, rejected = true;
    for (int idx = 0; idx < 4; idx++) {
        int full = old_set_odr(idx);
        trunc_pinned = trunc_pinned && (g_reg_odr_div == legacy_reg[idx]) && (full > 255);
        rejected = rejected && (odr_div_checked((uint32_t)idx, &div) == -1);
    }
    CHECK(trunc_pinned);                                             /* 레거시: 1 Hz 요청이 실제로는 25 Hz */
    CHECK(rejected);                                                 /* 의도된 차이: 거부 */
    CHECK(odr_div_checked(8u, &div) == -1);

    /* 연습 2 */
    CHECK(payload_len_mechanical(0u) == 4294967295u);               /* 0 - 1u → UINT_MAX (32비트 unsigned) */
    CHECK(payload_len(0u) == 0u && payload_len(1u) == 0u && payload_len(255u) == 254u);
    bool eq2 = true;
    for (uint32_t n = 1u; n <= 255u; n++) eq2 = eq2 && (payload_len((uint8_t)n) == payload_len_mechanical((uint8_t)n));
    CHECK(eq2);                                                      /* n >= 1 에서는 같다 — 그래서 리뷰에서 놓친다 */

    /* 연습 3 */
    g_live = 0x12FFu; g_latch_l = 0xAAu;
    CHECK(read_temp_raw() == 0x12FFu);                               /* 같은 순간의 H:L */
    g_live = 0x12FFu; g_latch_l = 0xAAu;
    CHECK(read_temp_l_first() == 0x14AAu);                           /* 지난 래치 L + 나중 H → 존재한 적 없는 값 */

    /* 연습 4 */
    bool eq4 = true;
    for (int a = 1; a <= 400; a++)
        for (int b = 0; b <= 400; b++)
            eq4 = eq4 && ((int)gcd_iter((uint32_t)a, (uint32_t)b) == old_gcd(a, b));
    CHECK(eq4);
    CHECK(gcd_iter(3200u, 40u) == 40u && gcd_iter(0u, 7u) == 7u);

    /* 연습 5 */
    ls_dev_t *d0 = pool_open("ls0", 0x48u);
    ls_dev_t *d1 = pool_open("accelerometer", 0x19u);               /* 13자: 레거시 strcpy 면 name[8] 오버런 */
    CHECK(d0 != NULL && d1 != NULL && d0 != d1);
    CHECK(strcmp(d1->name, "acceler") == 0 && d1->addr == 0x19u);
    ls_dev_t *d2 = pool_open("a", 1u), *d3 = pool_open("b", 2u);
    CHECK(d2 != NULL && d3 != NULL && pool_open("c", 3u) == NULL);  /* 풀 4개 소진 → NULL (malloc 실패와 같은 계약) */
    CHECK(pool_close(d1) == 0 && pool_close(d1) == -1);              /* 이중 close 감지 */
    ls_dev_t *d4 = pool_open("again", 5u);
    CHECK(d4 == d1 && strcmp(d4->name, "again") == 0);               /* 빈 슬롯 재사용 */
    ls_dev_t outside;
    CHECK(pool_close(&outside) == -1 && pool_close(NULL) == -1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
