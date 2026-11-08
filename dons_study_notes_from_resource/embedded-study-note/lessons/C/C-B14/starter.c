/* C-B14 연산자 우선순위와 평가 순서 함정
 * Part A: 12개 퀴즈 — 값 예측 + 분류(정의됨/미지정/UB/컴파일 에러)
 * Part B: 우선순위·평가 순서 버그가 있는 레지스터 체크 함수 6개 고치기
 * Part C: UB 가 섞인 프레임 파서를 시퀀스가 분명한 코드로 다시 쓰기
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ================= Part A: 실행하기 전에 예측하라 ================= */
#define MASK 0x20u
#define VAL  0x20u

enum { Q01, Q02, Q03A, Q03B, Q03C, Q04, Q05, Q10A, Q10B, Q11A, Q11B, Q12, NPRED };

/* 퀴즈 스니펫을 "쓰인 그대로" 평가한다. 경고는 일부러 끔 (경고 보기는 quiz_demo.c) */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wparentheses"
#pragma clang diagnostic ignored "-Wlogical-not-parentheses"
#pragma clang diagnostic ignored "-Wunused-value"
#pragma clang diagnostic ignored "-Wunevaluated-expression"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wlogical-not-parentheses"
#pragma GCC diagnostic ignored "-Wunused-value"
#endif
static long actual(int q) {
    uint32_t reg = 0x20;  unsigned a = 1;
    int arr[] = {10, 20, 30};  int *p = arr;  int v;
    int x, y = 7, z = 7;  unsigned flag = 0x02, mask = 0x04;
    int cnt = 0, pos = 1;  int w;  int nn = 5;  size_t sz;
    switch (q) {
    case Q01:  return reg & MASK == VAL;
    case Q02:  return a << 2 + 1;
    case Q03A: v = *p++; return v;
    case Q03B: v = *p++; (void)v; return *p;
    case Q03C: (*p)++; return arr[0];
    case Q04:  x = y == z; return x;
    case Q05:  return !flag & mask;
    case Q10A: if ((pos > 0) || (++cnt > 0)) { } return cnt;
    case Q10B: if ((pos > 0) && (++cnt > 0)) { } return cnt;
    case Q11A: w = (1, 2, 3); return w;
    case Q11B: w = 1, 2, 3; return w;
    case Q12:  sz = sizeof nn++; (void)sz; return nn;
    }
    return -1;
}
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* A-1. 각 스니펫의 값을 예측해 숫자로 적어라 (먼저 적고, 그 다음 실행!) */
#define TODO_PREDICT (-999L)
static const long predict[NPRED] = {
    [Q01]  = TODO_PREDICT, /* reg=0x20:              reg & MASK == VAL         */
    [Q02]  = TODO_PREDICT, /* a=1:                   a << 2 + 1                */
    [Q03A] = TODO_PREDICT, /* arr={10,20,30}, p=arr: v = *p++;  → v            */
    [Q03B] = TODO_PREDICT, /*                        그 직후의 *p              */
    [Q03C] = TODO_PREDICT, /* p=arr:                 (*p)++;    → arr[0]       */
    [Q04]  = TODO_PREDICT, /* y=7, z=7:              x = y == z; → x           */
    [Q05]  = TODO_PREDICT, /* flag=0x02, mask=0x04:  !flag & mask              */
    [Q10A] = TODO_PREDICT, /* cnt=0, pos=1:          (pos > 0) || (++cnt > 0) 후 cnt */
    [Q10B] = TODO_PREDICT, /* cnt=0, pos=1:          (pos > 0) && (++cnt > 0) 후 cnt */
    [Q11A] = TODO_PREDICT, /*                        w = (1, 2, 3); → w        */
    [Q11B] = TODO_PREDICT, /*                        w = 1, 2, 3;   → w        */
    [Q12]  = TODO_PREDICT, /* nn=5:                  sz = sizeof nn++; → nn    */
};

/* A-2. 12개 스니펫을 C11 기준으로 분류하라 */
enum kind { K_DEFINED = 0, K_UNSPEC = 1, K_UB = 2, K_ERROR = 3, K_TODO = 7 };
/* K_DEFINED: 결과가 하나로 정해짐   K_UNSPEC: 허용된 결과 여러 개 중 하나(미지정)
 * K_UB:      정의되지 않은 동작     K_ERROR:  C 컴파일러가 진단해야 하는 오류 */
static const enum kind kind_of[13] = {
    [1]  = K_TODO, /* Q1  if (reg & MASK == VAL)            */
    [2]  = K_TODO, /* Q2  a << 2 + 1                        */
    [3]  = K_TODO, /* Q3  *p++  vs  (*p)++                  */
    [4]  = K_TODO, /* Q4  x = y == z                        */
    [5]  = K_TODO, /* Q5  !flag & mask                      */
    [6]  = K_TODO, /* Q6  sel ? b : c = d;                  */
    [7]  = K_TODO, /* Q7  i = i++;                          */
    [8]  = K_TODO, /* Q8  s = f() + g();  (f, g 가 로그를 남김) */
    [9]  = K_TODO, /* Q9  a[i] = i++;                       */
    [10] = K_TODO, /* Q10 (pos > 0) || (++cnt > 0)          */
    [11] = K_TODO, /* Q11 w = (1, 2, 3);                    */
    [12] = K_TODO, /* Q12 sz = sizeof nn++;                 */
};
#define KIND_KEY 0xA7C9A5u   /* 정답을 숨겨 둔 값 — 디코딩하지 말고 풀 것 */
static enum kind key_kind(int q) { return (enum kind)(((KIND_KEY ^ 0xA5A5A5u) >> (2 * (q - 1))) & 3u); }

/* ================= Part B: 레지스터 체크 함수 고치기 ================= */
#define SR_TXE  (1u << 7)
#define SR_RXNE (1u << 5)
#define SR_ORE  (1u << 3)

/* B1. 상태 레지스터의 RXNE 비트가 1인가?
 *     원본(버그): return sr & SR_RXNE == SR_RXNE; */
static bool rxne_set(uint32_t sr) {
    (void)sr;
    return false; /* TODO */
}

/* B2. SPI 로 받은 두 바이트(hi 먼저) → 16비트
 *     원본(버그): return hi << 8 + lo; */
static uint16_t make_u16(uint8_t hi, uint8_t lo) {
    (void)hi; (void)lo;
    return 0; /* TODO */
}

/* B3. mask 의 비트가 sr 에 하나도 서 있지 않으면 true
 *     원본(버그): return !sr & mask; */
static bool flags_clear(uint32_t sr, uint32_t mask) {
    (void)sr; (void)mask;
    return false; /* TODO */
}

/* B4. 에러 카운터를 1 올린다
 *     원본(버그): *cnt++; */
static void err_bump(uint32_t *cnt) {
    (void)cnt; /* TODO */
}

/* 가짜 UART RX FIFO: uart_pop 은 "읽으면 FIFO 에서 빠지는" 부작용(side effect)이 있다 */
typedef struct { uint8_t fifo[8]; unsigned head, count; } fake_uart;
static bool uart_pop(fake_uart *u, uint8_t *out) {
    if (u->count == 0) return false;
    *out = u->fifo[u->head];
    u->head = (u->head + 1u) % 8u;
    u->count--;
    return true;
}

/* B5. FIFO 에서 최대 max 바이트를 out 으로 꺼내고 꺼낸 개수를 돌려준다.
 *     max 를 넘는 바이트는 FIFO 에 남아 있어야 하고, out[max] 이후는 건드리면 안 된다.
 *     원본(버그): while (uart_pop(u, &out[n]) && n < max) n++; */
static size_t uart_drain(fake_uart *u, uint8_t *out, size_t max) {
    (void)u; (void)out; (void)max; (void)uart_pop;
    return 0; /* TODO */
}

/* 가짜 SPI: spi_xfer 를 부를 때마다 다음 수신 바이트가 나온다 */
typedef struct { const uint8_t *rx; unsigned idx; } fake_spi;
static uint8_t spi_xfer(fake_spi *s) { return s->rx[s->idx++]; }

/* B6. SPI 에서 빅 엔디언 16비트 읽기 (먼저 온 바이트 = 상위)
 *     원본(버그): return spi_xfer(s) << 8 | spi_xfer(s); */
static uint16_t spi_read_be16(fake_spi *s) {
    (void)s; (void)spi_xfer;
    return 0; /* TODO */
}

/* ================= Part C: 프레임 파서 다시 쓰기 ================= */
/* 프레임: [0xA5][LEN_HI][LEN_LO][CMD][payload × LEN][CHK]
 * CHK = LEN_HI ^ LEN_LO ^ CMD ^ payload[0] ^ ... ^ payload[LEN-1]
 * 반환: 0 = OK (f 채움), -1 = 짧음/SYNC 틀림/LEN > FRAME_MAX/길이 불일치/CHK 틀림 */
#define FRAME_SYNC 0xA5u
#define FRAME_MAX  16u
typedef struct { uint16_t len; uint8_t cmd; uint8_t payload[FRAME_MAX]; } frame_t;

/* 원본(버그) — buggy_originals.c 에 그대로 있다:
 *   if (n < 5 || *p++ != 0xA5) return -1;
 *   f->len = *p++ << 8 | *p++;                  ①
 *   f->cmd = *p++;
 *   ...
 *   for (size_t i = 0; i < f->len; ) { chk ^= p[i]; f->payload[i] = p[i++]; }   ②
 *   if (chk ^ p[f->len] != 0) return -1;        ③
 */
static int parse_frame(const uint8_t *p, size_t n, frame_t *f) {
    (void)p; (void)n; (void)f;
    return -1; /* TODO */
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* Part A-1: 예측 */
    CHECK(predict[Q01]  == actual(Q01));
    CHECK(predict[Q02]  == actual(Q02));
    CHECK(predict[Q03A] == actual(Q03A));
    CHECK(predict[Q03B] == actual(Q03B));
    CHECK(predict[Q03C] == actual(Q03C));
    CHECK(predict[Q04]  == actual(Q04));
    CHECK(predict[Q05]  == actual(Q05));
    CHECK(predict[Q10A] == actual(Q10A));
    CHECK(predict[Q10B] == actual(Q10B));
    CHECK(predict[Q11A] == actual(Q11A));
    CHECK(predict[Q11B] == actual(Q11B));
    CHECK(predict[Q12]  == actual(Q12));

    /* Part A-2: 분류 */
    for (int q = 1; q <= 12; q++) {
        g_total++;
        if (kind_of[q] == key_kind(q)) g_pass++;
        else printf("FAIL kind_of[%d] (Q%d 분류가 틀림)\n", q, q);
    }

    /* Part B */
    CHECK(rxne_set(SR_RXNE) == true);
    CHECK(rxne_set(0x01u) == false);                 /* 원본은 bit 0 을 검사한다 */
    CHECK(rxne_set(SR_TXE | SR_RXNE | SR_ORE) == true);
    CHECK(make_u16(0x12, 0x34) == 0x1234);
    CHECK(make_u16(0x00, 0xFF) == 0x00FF);
    CHECK(flags_clear(0x02u, SR_ORE | 0x04u) == true);
    CHECK(flags_clear(0x06u, 0x04u) == false);
    CHECK(flags_clear(0x00u, 0x04u) == true);        /* 원본은 여기서 false */

    uint32_t errs[2] = {7, 100};
    err_bump(&errs[0]);
    err_bump(&errs[0]);
    CHECK(errs[0] == 9 && errs[1] == 100);

    fake_uart u = { {1, 2, 3, 4, 5}, 0, 5 };
    uint8_t out[4] = {0, 0, 0, 0xEE};                /* out[3] = 카나리 */
    CHECK(uart_drain(&u, out, 3) == 3);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 0xEE);
    CHECK(u.count == 2);                             /* 4, 5 는 남아 있어야 한다 */
    CHECK(uart_drain(&u, out, 3) == 2 && out[0] == 4 && out[1] == 5);

    const uint8_t rx[] = {0x12, 0x34, 0xAB, 0xCD};
    fake_spi s = { rx, 0 };
    CHECK(spi_read_be16(&s) == 0x1234);
    CHECK(spi_read_be16(&s) == 0xABCD);

    /* Part C */
    frame_t f = {0};
    const uint8_t ok[]    = {0xA5, 0x00, 0x03, 0x10, 0x01, 0x02, 0x03, 0x13};
    const uint8_t empty[] = {0xA5, 0x00, 0x00, 0x20, 0x20};
    const uint8_t badchk[]= {0xA5, 0x00, 0x03, 0x10, 0x01, 0x02, 0x03, 0x14};
    const uint8_t badsync[]={0x5A, 0x00, 0x00, 0x20, 0x20};
    const uint8_t big[]   = {0xA5, 0x01, 0x00, 0x20, 0x21};   /* LEN = 0x0100 → 너무 큼 */
    CHECK(parse_frame(ok, sizeof ok, &f) == 0);
    CHECK(f.len == 3 && f.cmd == 0x10 && f.payload[0] == 1 && f.payload[1] == 2 && f.payload[2] == 3);
    CHECK(parse_frame(empty, sizeof empty, &f) == 0 && f.len == 0 && f.cmd == 0x20);
    CHECK(parse_frame(badchk, sizeof badchk, &f) == -1);
    CHECK(parse_frame(badsync, sizeof badsync, &f) == -1);
    CHECK(parse_frame(big, sizeof big, &f) == -1);
    CHECK(parse_frame(ok, sizeof ok - 1, &f) == -1);          /* 길이 불일치 */
    CHECK(parse_frame(ok, 3, &f) == -1);                       /* 너무 짧음 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
