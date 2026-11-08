/* C-M02 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* 본문과 같은 비트 의미 모델 (+ 연습 1 의 W1T) */
typedef struct { uint32_t rw, w1c, w0c, w1s, w1t, rc; } reg_spec;
static uint32_t apply_write(const reg_spec *s, uint32_t cur, uint32_t w) {
    uint32_t v = cur;
    v = (v & ~s->rw) | (w & s->rw);
    v &= ~(w & s->w1c);
    v &= ~(~w & s->w0c);
    v |= (w & s->w1s);
    v ^= (w & s->w1t);                        /* 연습 1: W1T — 1 쓴 자리만 뒤집힘 */
    return v;
}

/* ---- 연습 1: W1T 와 RP2040 식 원자 alias (+0x1000 XOR, +0x2000 SET, +0x3000 CLR) ---- */
static uint32_t gpio_out;                     /* 출력 레지스터 저장소 하나에 alias 4개 */
static const reg_spec ALIAS[4] = {
    { .rw  = 0xFFFFFFFFu },                   /* +0x0000 일반 쓰기 */
    { .w1t = 0xFFFFFFFFu },                   /* +0x1000 XOR       */
    { .w1s = 0xFFFFFFFFu },                   /* +0x2000 SET       */
    { .w1c = 0xFFFFFFFFu },                   /* +0x3000 CLR       */
};
static int g_reads;                           /* 읽기 횟수 — 원자 연산은 0 이어야 */
static void alias_write(unsigned offset, uint32_t v) {
    gpio_out = apply_write(&ALIAS[(offset >> 12) & 3u], gpio_out, v);
}
static void led_toggle(unsigned pin) { alias_write(0x1000u, 1u << pin); }   /* 쓰기 1번, 읽기 0번 */
static void led_on(unsigned pin)     { alias_write(0x2000u, 1u << pin); }
static void led_off(unsigned pin)    { alias_write(0x3000u, 1u << pin); }

/* ---- 연습 2: 쓰기 전용(WO) 레지스터 — 읽으면 0 (RAZ) → shadow 로 관리 ---- */
static uint32_t wo_hw;                        /* 하드웨어 내부 값 (CPU 는 못 읽음) */
static uint32_t wo_read(void) { g_reads++; return 0; }
static void     wo_write(uint32_t v) { wo_hw = v; }
static void wo_set_buggy(uint32_t bit) { wo_write(wo_read() | bit); }       /* 다른 비트 전부 0 됨 */
static uint32_t wo_shadow;                    /* 마지막으로 쓴 값의 소프트웨어 사본 */
static void wo_set(uint32_t bit)   { wo_shadow |= bit;  wo_write(wo_shadow); }
static void wo_clear(uint32_t bit) { wo_shadow &= ~bit; wo_write(wo_shadow); }

/* ---- 연습 3: NXP Kinetis PORTx_PCRn 식 혼합 레지스터 (단순화) ---- */
#define PCR_MUX_Pos  8u
#define PCR_MUX_Msk  (7u << PCR_MUX_Pos)      /* RW  [10:8]  */
#define PCR_IRQC_Msk (0xFu << 16)             /* RW  [19:16] */
#define PCR_ISF      (1u << 24)               /* W1C 핀 인터럽트 상태 플래그 */
static const reg_spec PCR_SPEC = { .rw = PCR_MUX_Msk | PCR_IRQC_Msk | 0x7u, .w1c = PCR_ISF };
static uint32_t pcr;
static void pcr_set_mux_buggy(uint32_t mux) {
    pcr = apply_write(&PCR_SPEC, pcr, (pcr & ~PCR_MUX_Msk) | (mux << PCR_MUX_Pos));
}
static void pcr_set_mux(uint32_t mux) {       /* 읽은 값에서 W1C 비트를 빼고(=0, 무해) 쓴다 */
    uint32_t v = pcr & ~(PCR_MUX_Msk | PCR_ISF);
    pcr = apply_write(&PCR_SPEC, pcr, v | ((mux << PCR_MUX_Pos) & PCR_MUX_Msk));
}

/* ---- 연습 4: SR 하나를 두 주인이 나눠 쓴다 (ISR: RX/ERR, main 폴링: TX) ---- */
#define SR_RX  (1u << 0)
#define SR_TX  (1u << 1)
#define SR_ERR (1u << 2)
static const reg_spec SR_SPEC = { .w1c = SR_RX | SR_TX | SR_ERR };
static uint32_t sr;
static uint32_t isr_ack_owned(uint32_t owned) {
    uint32_t s = sr & owned;                  /* 읽기 1번, 내 몫만 */
    if (s) sr = apply_write(&SR_SPEC, sr, s); /* 내 몫만 ack */
    return s;
}

/* ---- 연습 5: bit-bash — 블랙박스 레지스터의 비트 의미 판별 ---- */
typedef enum { K_RO, K_RW, K_W1C, K_W0C, K_W1S, K_RC } kind;
typedef struct { reg_spec spec; uint32_t store; } bb_reg;
static void     bb_reset(bb_reg *r)               { r->store = 0; }            /* 파워온 리셋 */
static void     bb_raise(bb_reg *r, uint32_t m)   { r->store |= m; }           /* HW 이벤트   */
static uint32_t bb_read(bb_reg *r)  { uint32_t v = r->store; r->store &= ~r->spec.rc; return v; }
static void     bb_write(bb_reg *r, uint32_t w)   { r->store = apply_write(&r->spec, r->store, w); }
static kind classify_bit(bb_reg *r, unsigned bit) {
    uint32_t m = 1u << bit, r1, r2, a, b, c;
    bb_reset(r); bb_raise(r, m);
    r1 = bb_read(r) & m; r2 = bb_read(r) & m;
    if (r1 && !r2) return K_RC;                         /* 읽기만으로 사라짐 */
    bb_reset(r); bb_raise(r, m); bb_write(r, 0); a = bb_read(r) & m;   /* 1 에서 0 쓰기 */
    bb_reset(r); bb_raise(r, m); bb_write(r, m); b = bb_read(r) & m;   /* 1 에서 1 쓰기 */
    bb_reset(r);                 bb_write(r, m); c = bb_read(r) & m;   /* 0 에서 1 쓰기 */
    if (!a &&  b &&  c) return K_RW;
    if ( a && !b && !c) return K_W1C;
    if (!a &&  b && !c) return K_W0C;
    if ( a &&  b &&  c) return K_W1S;
    return K_RO;                                        /* a=1 b=1 c=0 */
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    gpio_out = 0x0000000Fu;
    led_toggle(0); led_toggle(4);
    CHECK(gpio_out == 0x0000001Eu);
    led_on(31); led_off(1);
    CHECK(gpio_out == 0x8000001Cu);
    alias_write(0x0000u, 0x5u);                         /* 일반 쓰기는 전부 덮어쓴다 */
    CHECK(gpio_out == 0x5u && g_reads == 0);

    /* 연습 2 */
    wo_hw = 0; wo_write(0x3u);                          /* 누군가 0x3 을 써 둔 상태 */
    wo_set_buggy(0x10u);
    CHECK(wo_hw == 0x10u);                              /* 0x3 이 사라짐 (재현) */
    wo_shadow = 0; wo_set(0x3u); wo_set(0x10u); wo_clear(0x1u);
    CHECK(wo_hw == 0x12u && wo_shadow == 0x12u);

    /* 연습 3 */
    pcr = PCR_ISF | (1u << PCR_MUX_Pos) | (0x9u << 16) | 0x3u;   /* 인터럽트 펜딩 중 */
    pcr_set_mux_buggy(3);
    CHECK((pcr & PCR_ISF) == 0);                        /* 펜딩 인터럽트 유실 (재현) */
    pcr = PCR_ISF | (1u << PCR_MUX_Pos) | (0x9u << 16) | 0x3u;
    pcr_set_mux(3);
    CHECK(pcr == (PCR_ISF | (3u << PCR_MUX_Pos) | (0x9u << 16) | 0x3u));

    /* 연습 4 */
    sr = SR_RX | SR_TX | SR_ERR;
    CHECK(isr_ack_owned(SR_RX | SR_ERR) == (SR_RX | SR_ERR));
    CHECK(sr == SR_TX);                                 /* main 의 TX 는 살아 있다 */
    CHECK(isr_ack_owned(SR_TX) == SR_TX && sr == 0);

    /* 연습 5: MIX 와 같은 배치 */
    bb_reg mix = { { .rw = 0xFFu, .w1c = 0xFF00u, .w0c = 0xFF0000u, .w1s = 0x0F000000u,
                     .rc = 0x30000000u }, 0 };
    CHECK(classify_bit(&mix, 0)  == K_RW);
    CHECK(classify_bit(&mix, 8)  == K_W1C);
    CHECK(classify_bit(&mix, 16) == K_W0C);
    CHECK(classify_bit(&mix, 24) == K_W1S);
    CHECK(classify_bit(&mix, 28) == K_RC);
    CHECK(classify_bit(&mix, 31) == K_RO);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
