/* SIV-B11 추가 연습 — 한 번에 검증
 *  연습 1: enable 배선 결함(비트 swap / 이웃 leak)은 매트릭스의 어떤 enable 모드가 잡나
 *  연습 2: read-to-clear(RC) 클리어 레지스터 IP 의 trigger-check-clear (DesignWare IC_CLR_* 식)
 *  연습 3: level 소스 — 원인이 남아 있으면 W1C 직후 다시 서야 한다
 *  연습 4: ISR end-to-end — posted write 때문에 생기는 spurious 재진입과 read-back
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RAW    0x00u   /* W1C raw status */
#define EN     0x04u   /* RW  enable     */
#define STAT   0x08u   /* RO  RAW & EN   */
#define FORCE  0x0Cu   /* WO  set RAW    */
#define CLR0   0x40u   /* RC  : CLR0 + 4*s 를 읽으면 소스 s 의 RAW 가 지워진다 (연습 2) */
#define VALID  0x37u

typedef struct {
    bool en_swap01;     /* EN bit0 ↔ bit1 배선이 바뀜              */
    bool en_leak;       /* EN bit k 가 소스 k+1 도 enable           */
    bool stat_rc;       /* STAT 를 읽으면 RAW 가 지워짐 (부작용)    */
    bool level_as_edge; /* level 소스가 push 순간에만 섬            */
    bool posted;        /* RAW 쓰기가 다음 버스 접근 때 반영됨      */
} pfault_t;

static struct { uint32_t raw, en, pend_w1c; int fifo; bool has_pend; pfault_t f; } D;

static void dev_init(const pfault_t *f) { memset(&D, 0, sizeof D); if (f) D.f = *f; }
static uint32_t en_eff(void) {
    uint32_t e = D.en;
    if (D.f.en_swap01) e = (e & ~3u) | ((e & 1u) << 1) | ((e >> 1) & 1u);
    if (D.f.en_leak)   e |= e << 1;
    return e & VALID;
}
static void relevel(void) { if (!D.f.level_as_edge && D.fifo > 0) D.raw |= 1u; } /* 소스 0 = RXNE(level) */
static void flush_posted(void) {
    if (D.has_pend) { D.raw &= ~D.pend_w1c; D.has_pend = false; relevel(); }
}
static bool irq_line(void) { return (D.raw & en_eff()) != 0; }   /* 배선 — 버스 접근 아님 */

static uint32_t rd(uint32_t off) {
    flush_posted();
    switch (off) {
    case RAW:  return D.raw;
    case EN:   return D.en;
    case STAT: { uint32_t v = D.raw & en_eff(); if (D.f.stat_rc) D.raw = 0; return v; }
    default:
        if (off >= CLR0 && off < CLR0 + 4 * 32) {
            uint32_t bit = 1u << ((off - CLR0) / 4);
            uint32_t v = (D.raw & bit) ? 1u : 0u;
            D.raw &= ~bit; relevel();
            return v;
        }
        return 0xDEADBEEFu;
    }
}
static void wr(uint32_t off, uint32_t v) {
    flush_posted();
    switch (off) {
    case RAW:
        if (D.f.posted) { D.pend_w1c = v & VALID; D.has_pend = true; }
        else { D.raw &= ~(v & VALID); relevel(); }
        break;
    case EN:    D.en = v & VALID; break;
    case FORCE: D.raw |= v & VALID; break;
    default:    break;
    }
}
static void fifo_push(void) { D.fifo++; D.raw |= 1u; }
static void fifo_pop_all(void) { D.fifo = 0; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---- 연습 1: 어떤 모드가 결함을 잡나 (bit0=NONE, bit1=SELF, bit2=OTHERS) ---- */
static unsigned detect_modes(const pfault_t *f, unsigned src) {
    unsigned hit = 0;
    uint32_t bit = 1u << src;
    uint32_t ens[3] = {0, bit, VALID & ~bit};
    for (int m = 0; m < 3; m++) {
        dev_init(f);
        wr(EN, ens[m]);
        wr(FORCE, bit);
        if (rd(STAT) != (ens[m] & bit)) hit |= 1u << m;
    }
    return hit;
}

/* ---- 연습 2: RC 클리어 레지스터 IP ---- */
static bool tcc_rc(unsigned src) {
    uint32_t bit = 1u << src;
    wr(EN, bit);
    wr(FORCE, bit);
    if (rd(STAT) != bit) return false;
    if (rd(STAT) != bit) return false;               /* 상태 읽기는 부작용이 없어야 한다 */
    unsigned other = src == 0 ? 1 : 0;
    (void)rd(CLR0 + 4 * other);                      /* 남의 CLR 를 읽어도 내 비트는 남아야 */
    if (rd(RAW) != bit) return false;
    (void)rd(CLR0 + 4 * src);                        /* clear = 읽기 */
    return rd(RAW) == 0 && rd(STAT) == 0;
}

/* ---- 연습 3: level 소스 ---- */
static bool level_tcc(void) {
    wr(EN, 1u);
    fifo_push();
    if (rd(RAW) != 1u) return false;
    wr(RAW, 1u);                                     /* 원인(데이터)이 남아 있는 상태에서 clear */
    if (rd(RAW) != 1u) return false;                 /* → 즉시 다시 서야 정상 (안 서면 데이터 방치) */
    fifo_pop_all();                                  /* 원인 제거 */
    wr(RAW, 1u);
    return rd(RAW) == 0 && !irq_line();
}

/* ---- 연습 4: ISR end-to-end ---- */
static int g_cnt[32], g_spurious, g_entries;
static void isr(bool readback) {
    g_entries++;
    uint32_t s = rd(STAT);                           /* 한 번 읽어 스냅샷 */
    if (!s) { g_spurious++; return; }
    for (unsigned b = 0; b < 32; b++) if (s & (1u << b)) g_cnt[b]++;
    wr(RAW, s);                                      /* 본 것만 W1C */
    if (readback) (void)rd(RAW);                     /* clear 가 IP 에 도달했음을 보장 */
}
static void cpu_run(bool readback) {                 /* 라인이 high 인 동안 ISR 진입 (최대 4번) */
    memset(g_cnt, 0, sizeof g_cnt); g_spurious = g_entries = 0;
    for (int k = 0; k < 4 && irq_line(); k++) isr(readback);
}

int main(void) {
    /* 연습 1 */
    pfault_t sw = {.en_swap01 = true}, lk = {.en_leak = true};
    CHECK(detect_modes(NULL, 0) == 0 && detect_modes(NULL, 1) == 0);
    CHECK(detect_modes(&sw, 0) == 0x6);              /* NONE 은 못 잡고 SELF, OTHERS 가 잡음 */
    CHECK(detect_modes(&lk, 1) == 0x4);              /* OTHERS 만 잡는다 */
    CHECK(detect_modes(&lk, 0) == 0x0);              /* 소스 0 은 leak 받을 이웃(-1)이 없음 */

    /* 연습 2 */
    dev_init(NULL);
    CHECK(tcc_rc(2) && tcc_rc(0));
    pfault_t rc = {.stat_rc = true};
    dev_init(&rc);
    CHECK(!tcc_rc(2));

    /* 연습 3 */
    dev_init(NULL);
    CHECK(level_tcc());
    pfault_t le = {.level_as_edge = true};
    dev_init(&le);
    CHECK(!level_tcc());

    /* 연습 4 */
    pfault_t po = {.posted = true};
    dev_init(&po); wr(EN, VALID); wr(FORCE, (1u << 0) | (1u << 2));
    cpu_run(false);
    CHECK(g_cnt[0] == 1 && g_cnt[2] == 1 && g_entries == 2 && g_spurious == 1);
    dev_init(&po); wr(EN, VALID); wr(FORCE, (1u << 0) | (1u << 2));
    cpu_run(true);
    CHECK(g_cnt[0] == 1 && g_cnt[2] == 1 && g_entries == 1 && g_spurious == 0);
    dev_init(NULL); wr(EN, VALID); wr(FORCE, (1u << 0) | (1u << 2));
    cpu_run(false);                                  /* posted 아니면 read-back 없어도 한 번 */
    CHECK(g_entries == 1 && g_spurious == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
