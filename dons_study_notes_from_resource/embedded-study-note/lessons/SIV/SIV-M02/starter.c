/* SIV-M02 DMA 검증: 디스크립터 체인과 데이터 무결성
 *
 * 가상의 scatter-gather DMA 엔진(SGDMA0)과 256 KiB 시뮬레이션 DRAM 위에서
 *   (A) 체인을 시작하고 poll-with-timeout 으로 끝을 기다린 뒤, 디스크립터마다 CRC-32 로 src/dst 를 비교하고
 *       dst arena 의 "써서는 안 되는 곳" 을 전부 스캔한다 (stray write).
 *   (B) 정렬/비정렬, 4 KiB 경계 교차, 길이 0/1/max, 체인 중간 에러 디스크립터를 경계 + 랜덤(xorshift32,
 *       고정 시드)으로 생성한다.
 *   (C) 완료 인터럽트 · status 레지스터 · 디스크립터 write-back 이 서로 일관된지 확인하고, 8가지 결함을
 *       주입한 야간 회귀에서 각 결함이 정확한 시그니처로 잡히는지 본다.
 * 엔진 mock 은 레지스터 접근 1회 = 1 tick 으로 진행한다 (SIV-B09 의 가짜 시계와 같은 발상: 타임아웃이 결정적).
 * SIV-B02 의 mock MMIO, SIV-B11 의 W1C IRQ 블록을 이 IP 에 필요한 만큼 축약해 넣었다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= 메모리 맵 (버스 주소) ================= */
#define IP_NAME   "SGDMA0"
#define DRAM_BASE 0x80000000u
#define DRAM_SIZE 0x40000u                 /* 256 KiB 시뮬레이션 DRAM                 */
#define DESC_AREA (DRAM_BASE + 0x00000u)   /* 4 KiB   디스크립터                      */
#define SRC_AREA  (DRAM_BASE + 0x01000u)   /* 124 KiB src 버퍼                        */
#define SRC_END   (DRAM_BASE + 0x20000u)
#define DST_AREA  (DRAM_BASE + 0x20000u)   /* 128 KiB dst 버퍼 (FILL 로 채워 둔다)    */
#define DST_END   (DRAM_BASE + 0x40000u)
#define UNMAPPED  0x40000000u              /* 디코드 안 되는 주소 → DECERR            */
#define FILL      0xEEu
#define PAGE      0x1000u

/* 디스크립터: 32 B, 32 B 정렬, little-endian */
#define D_NEXT 0x00u      /* 다음 디스크립터 주소 (CTRL.LAST 면 무시)       */
#define D_SRC  0x04u
#define D_DST  0x08u
#define D_CTRL 0x0Cu      /* [15:0] LEN (0..65535), [16] LAST               */
#define D_STAT 0x10u      /* write-back: [0] DONE [1] ERR [7:4] ERR_CODE     */
#define D_XFER 0x14u      /* write-back: 전송한 바이트 수                    */
#define DESC_SIZE 32u
#define LEN_MASK  0xFFFFu
#define MAX_LEN   0xFFFFu
#define CTRL_LAST (1u << 16)
#define WB_DONE 0x1u
#define WB_ERR  0x2u
#define ERR_SRC_DEC 1u    /* src 범위가 디코드 안 됨 */
#define ERR_DST_DEC 2u    /* dst 범위가 디코드 안 됨 */
#define ERR_DESC    3u    /* 디스크립터 자체를 못 읽음 (정렬/주소) */

/* DMA 채널 레지스터 */
#define DMA_CTRL      0x000u  /* RW : [0] START, [1] SRST (둘 다 self-clear), [8] IE_DONE, [9] IE_ERR */
#define DMA_STATUS    0x004u  /* RO : [0] BUSY, [7:4] ERR_CODE                                        */
#define DMA_IRQ_STAT  0x008u  /* W1C: [0] DONE (체인 완료), [1] ERR                                    */
#define DMA_DESC_HEAD 0x00Cu  /* RW : 체인 첫 디스크립터 주소                                          */
#define DMA_CUR_DESC  0x010u  /* RO : 마지막으로 fetch 한 디스크립터 (에러 시 문제의 디스크립터)      */
#define DMA_DESC_CNT  0x014u  /* RO : 정상 완료한 디스크립터 수                                        */
#define DMA_BYTE_CNT  0x018u  /* RO : 정상 완료한 총 바이트                                            */
#define INTC_RAW      0x800u  /* RO : 인터럽트 컨트롤러 입력 라인 레벨, SGDMA0 = 입력 20               */
#define C_START   (1u << 0)
#define C_SRST    (1u << 1)
#define C_IE_DONE (1u << 8)
#define C_IE_ERR  (1u << 9)
#define I_DONE    (1u << 0)
#define I_ERR     (1u << 1)
#define LINE_BIT  (1u << 20)
#define BEAT      256u        /* tick 하나에 옮기는 최대 바이트 */
#define MAX_POLLS 4096u

/* ================= 시뮬레이션 DRAM (주어진 코드) ================= */
static uint8_t MEM[DRAM_SIZE];
static bool in_dram(uint32_t a, uint32_t len) {
    return a >= DRAM_BASE && (uint64_t)(a - DRAM_BASE) + len <= DRAM_SIZE;
}
static uint8_t *mp(uint32_t a) { return &MEM[a - DRAM_BASE]; }     /* 호출 전에 in_dram 확인 */
static uint32_t mem_rd32(uint32_t a) {
    const uint8_t *p = mp(a);
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void mem_wr32(uint32_t a, uint32_t v) {
    uint8_t *p = mp(a);
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* ================= DMA 엔진 mock + 결함 주입 (주어진 코드 — 고치지 않는다) ================= */
typedef struct {
    bool wrap_4k;      /* 4 KiB 경계를 넘는 dst 쓰기가 같은 페이지 처음으로 감긴다                 */
    bool tail_drop;    /* dst+len 이 4B 정렬이 아니면 마지막 부분 워드((dst+len)&3 바이트)를 안 씀 */
    bool len_15bit;    /* LEN 필드를 15비트로 디코드 (bit15 무시)                                  */
    bool len0_hang;    /* LEN=0 디스크립터에서 BUSY 로 멈춤                                        */
    bool err_continue; /* 에러 디스크립터 뒤에도 체인을 계속 진행                                  */
    bool err_as_done;  /* 에러로 멈추지만 IRQ 는 DONE, ERR_CODE 는 0                               */
    bool irq_early;    /* 마지막 디스크립터 write-back 보다 DONE IRQ 가 먼저 보임                  */
    bool bytecnt_last; /* BYTE_CNT 에 마지막 디스크립터 길이가 안 더해짐                           */
} dma_fault_t;

enum { PH_IDLE, PH_FETCH, PH_MOVE, PH_WB, PH_LATE_WB };
static struct {
    uint32_t ctrl, irq, head, cur, desc_cnt, byte_cnt, err_code;
    bool busy, last;
    int phase;
    uint32_t src, dst, len, moved, next;
    dma_fault_t f;
    unsigned ticks, nacc;
} D;

static void dma_init(const dma_fault_t *f) {
    memset(&D, 0, sizeof D);
    if (f) D.f = *f;
}
static void dma_finish(uint32_t irq_bits) { D.busy = false; D.phase = PH_IDLE; D.irq |= irq_bits; }
static void wb(uint32_t desc, uint32_t st, uint32_t xfer) { mem_wr32(desc + D_STAT, st); mem_wr32(desc + D_XFER, xfer); }

static void dma_tick(void) {
    D.ticks++;
    switch (D.phase) {
    case PH_FETCH: {
        uint32_t a = D.cur;
        if ((a & (DESC_SIZE - 1u)) || !in_dram(a, DESC_SIZE)) { D.err_code = ERR_DESC; dma_finish(I_ERR); return; }
        uint32_t ctl = mem_rd32(a + D_CTRL);
        D.next = mem_rd32(a + D_NEXT);
        D.src = mem_rd32(a + D_SRC);
        D.dst = mem_rd32(a + D_DST);
        D.len = ctl & (D.f.len_15bit ? 0x7FFFu : LEN_MASK);
        D.last = (ctl & CTRL_LAST) != 0;
        uint32_t code = !in_dram(D.src, D.len) ? ERR_SRC_DEC : !in_dram(D.dst, D.len) ? ERR_DST_DEC : 0u;
        if (code) {                                     /* 데이터를 옮기기 전에 검사 → 0 바이트 */
            wb(a, WB_ERR | code << 4, 0);
            if (D.f.err_as_done) { dma_finish(I_DONE); return; }
            D.err_code = code;
            if (D.f.err_continue && !D.last) { D.irq |= I_ERR; D.cur = D.next; return; }
            dma_finish(I_ERR);
            return;
        }
        D.moved = 0;
        D.phase = PH_MOVE;
        return;
    }
    case PH_MOVE: {
        uint32_t eff = D.len, t = (D.dst + D.len) & 3u;
        if (D.f.tail_drop && t) eff = D.len > t ? D.len - t : 0;
        uint32_t n = eff - D.moved < BEAT ? eff - D.moved : BEAT;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t o = D.moved + i, da = D.dst + o;
            if (D.f.wrap_4k) da = (D.dst & ~(PAGE - 1u)) | (da & (PAGE - 1u));
            *mp(da) = *mp(D.src + o);
        }
        D.moved += n;
        if (D.moved == eff && !(D.f.len0_hang && D.len == 0)) D.phase = PH_WB;
        return;
    }
    case PH_WB:
        D.desc_cnt++;
        if (!(D.f.bytecnt_last && D.last)) D.byte_cnt += D.len;
        if (D.last && D.f.irq_early) { dma_finish(I_DONE); D.phase = PH_LATE_WB; return; }
        wb(D.cur, WB_DONE, D.len);
        if (D.last) dma_finish(I_DONE);
        else { D.cur = D.next; D.phase = PH_FETCH; }
        return;
    case PH_LATE_WB:
        wb(D.cur, WB_DONE, D.len);
        D.phase = PH_IDLE;
        return;
    default:
        return;
    }
}
static uint32_t irq_line(void) {
    bool on = ((D.irq & I_DONE) && (D.ctrl & C_IE_DONE)) || ((D.irq & I_ERR) && (D.ctrl & C_IE_ERR));
    return on ? LINE_BIT : 0;
}
/* 레지스터 접근 1회 = 엔진 1 tick (시간이 흐른다). 테스트가 DRAM 을 직접 읽는 mem_rd32/mp 는 tick 없음. */
static uint32_t reg_read32(uint32_t off) {
    D.nacc++;
    dma_tick();
    switch (off) {
    case DMA_CTRL:      return D.ctrl;
    case DMA_STATUS:    return (D.busy ? 1u : 0u) | D.err_code << 4;
    case DMA_IRQ_STAT:  return D.irq;
    case DMA_DESC_HEAD: return D.head;
    case DMA_CUR_DESC:  return D.cur;
    case DMA_DESC_CNT:  return D.desc_cnt;
    case DMA_BYTE_CNT:  return D.byte_cnt;
    case INTC_RAW:      return irq_line();
    default:            return 0xDEADBEEFu;
    }
}
static void reg_write32(uint32_t off, uint32_t v) {
    D.nacc++;
    dma_tick();
    switch (off) {
    case DMA_CTRL:
        if (v & C_SRST) {                                /* 채널 리셋: 레지스터만, DRAM 은 그대로 */
            dma_fault_t f = D.f;
            unsigned t = D.ticks, n = D.nacc;
            memset(&D, 0, sizeof D);
            D.f = f; D.ticks = t; D.nacc = n;
            return;
        }
        D.ctrl = v & (C_IE_DONE | C_IE_ERR);
        if ((v & C_START) && !D.busy) {
            D.busy = true; D.phase = PH_FETCH; D.cur = D.head;
            D.desc_cnt = 0; D.byte_cnt = 0; D.err_code = 0;
        }
        break;
    case DMA_IRQ_STAT:  D.irq &= ~(v & (I_DONE | I_ERR)); break;
    case DMA_DESC_HEAD: if (!D.busy) D.head = v; break;
    default:            break;
    }
}

/* ================= 테스트 공통 (주어진 코드) ================= */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static uint32_t rnd(uint32_t *s, uint32_t n) { return xs32(s) % n; }      /* 0..n-1 */
static uint32_t round_up(uint32_t a, uint32_t al) { return (a + al - 1u) & ~(al - 1u); }
static bool crosses_4k(uint32_t a, uint32_t len) { return len && a / PAGE != (a + len - 1u) / PAGE; }

#define MAXD 8
#define GAP  64u
typedef struct { uint32_t src, dst, len; } seg_t;
typedef struct {
    int kind; uint32_t seed; int n;
    seg_t s[MAXD];
    uint32_t desc[MAXD];      /* 디스크립터 주소 (job_load 가 채움)                      */
    uint32_t crc[MAXD];       /* 생성 시점의 src CRC-32 = 골든 시그니처 (job_load 가 채움) */
    int err_idx;              /* -1 = 에러 디스크립터 없음                                */
    uint32_t err_code;
} job_t;
enum { K_ALIGNED, K_UNALIGNED, K_CROSS4K, K_LEN0, K_LEN1, K_LENMAX, K_ERRMID, K_RANDOM, K_COUNT };
static const char *const KIND_NAME[] = {"ALIGNED", "UNALIGNED", "CROSS_4K", "LEN0", "LEN1", "LEN_MAX", "ERR_MID", "RANDOM"};

/* 다음 버퍼 위치: 앞 버퍼와 GAP 띄우고 64 B 정렬 + off. no_cross 면 4 KiB 경계를 넘지 않게 다음 페이지로 */
static uint32_t place(uint32_t *next, uint32_t len, uint32_t off, bool no_cross) {
    uint32_t a = round_up(*next + GAP, 64u) + off;
    if (no_cross && crosses_4k(a, len)) a = round_up(a, PAGE) + off;
    *next = a + len;
    return a;
}
/* 시작에서 k 바이트 뒤에 4 KiB 경계가 오도록 놓는다 (0 < k < len) */
static uint32_t place_cross(uint32_t *next, uint32_t len, uint32_t k) {
    uint32_t b = round_up(*next + GAP + k, PAGE);
    *next = b - k + len;
    return b - k;
}

enum { CK_TIMEOUT, CK_WB_STATUS, CK_WB_XFER, CK_IRQ, CK_STATUS, CK_DESC_CNT, CK_BYTE_CNT, CK_CUR_DESC,
       CK_LINE, CK_CLEAR, CK_SRC, CK_DATA, CK_STRAY };
static const char *const CHECK_NAME[] = {"TIMEOUT", "WB_STATUS", "WB_XFER", "IRQ_STAT", "STATUS", "DESC_CNT",
                                         "BYTE_CNT", "CUR_DESC", "IRQ_LINE", "IRQ_CLEAR", "SRC_INTACT", "DATA_CRC",
                                         "STRAY_WRITE"};
typedef struct { int check, desc; uint32_t addr, exp, act, aux; } fail_t;   /* desc=-1: 체인 전체 */
#define MAXF 16
typedef struct { fail_t f[MAXF]; int n, dropped; } fails_t;
static void add_fail(fails_t *F, int check, int desc, uint32_t addr, uint32_t exp, uint32_t act, uint32_t aux) {
    if (F->n < MAXF) F->f[F->n++] = (fail_t){check, desc, addr, exp, act, aux};
    else F->dropped++;
}
static void expect_reg(fails_t *F, int check, uint32_t off, uint32_t exp) {
    uint32_t act = reg_read32(off);
    if (act != exp) add_fail(F, check, -1, off, exp, act, 0);
}
static int desc_index(const job_t *J, uint32_t a) {
    for (int i = 0; i < J->n; i++) if (J->desc[i] == a) return i;
    return -1;
}

/* ================= Part A: CRC-32 · 실행 · 데이터 검사 ================= */
/* CRC-32 (IEEE 802.3 / zlib): reflected, poly 0xEDB88320, init/xorout 0xFFFFFFFF.
 * zlib crc32() 처럼 이어서 부를 수 있어야 한다: crc32_update(crc32_update(0,a,na), b, nb) == crc(a||b) */
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    (void)p; (void)n;
    return crc; /* TODO: ~crc 로 시작, 바이트마다 xor 후 8번 (crc>>1) ^ (0xEDB88320 & -(crc&1)), 끝에 ~ */
}

/* job 메모리 준비: 디스크립터 영역·src 0, dst arena 는 FILL, src 는 랜덤 데이터, 골든 CRC, 디스크립터 기록 */
static void job_load(job_t *J) {
    memset(MEM, 0, DST_AREA - DRAM_BASE);
    memset(mp(DST_AREA), FILL, DST_END - DST_AREA);
    uint32_t r = (J->seed * 0x9E3779B9u) ^ ((uint32_t)J->kind << 24) ^ 0xA5A5A5A5u;
    if (!r) r = 1;
    for (int i = 0; i < J->n; i++) J->desc[i] = DESC_AREA + 0x100u * (uint32_t)((i * 3) % MAXD); /* 비연속 */
    for (int i = 0; i < J->n; i++) {
        const seg_t *s = &J->s[i];
        J->crc[i] = 0;
        if (in_dram(s->src, s->len)) {
            for (uint32_t b = 0; b < s->len; b++) *mp(s->src + b) = (uint8_t)xs32(&r);
            J->crc[i] = crc32_update(0, mp(s->src), s->len);
        }
        uint32_t d = J->desc[i];
        mem_wr32(d + D_NEXT, i + 1 < J->n ? J->desc[i + 1] : 0);
        mem_wr32(d + D_SRC, s->src);
        mem_wr32(d + D_DST, s->dst);
        mem_wr32(d + D_CTRL, (s->len & LEN_MASK) | (i == J->n - 1 ? CTRL_LAST : 0));
        wb(d, 0, 0);
    }
}

/* 이전 job 의 잔여 IRQ 를 W1C 로 지우고, head 로 체인을 시작(IE_DONE|IE_ERR|START)한 뒤
 * IRQ_STAT 에 DONE 이나 ERR 가 보일 때까지 최대 max_polls 번 읽는다.
 * 돌려주는 값 = 마지막으로 읽은 IRQ_STAT (timeout 이면 DONE/ERR 둘 다 0), *polls = 읽은 횟수 */
static uint32_t dma_run(uint32_t head, unsigned max_polls, unsigned *polls) {
    (void)head; (void)max_polls;
    *polls = 0;
    return 0; /* TODO: 잔여 IRQ W1C → DESC_HEAD=head → CTRL=IE_DONE|IE_ERR|START → IRQ_STAT 를 DONE|ERR 가 보일 때까지 poll */
}

/* 데이터 무결성:
 *  - 모든 디스크립터: src 가 매핑돼 있으면 src CRC == 골든 (DMA 가 src 를 건드리지 않았나)        → CK_SRC
 *  - 완료돼야 할 디스크립터(i < stop): dst CRC == 골든. 다르면 src/dst 를 비교해 첫 불일치 오프셋 → CK_DATA
 *    (addr = dst + 오프셋, exp = 골든 CRC, act = dst CRC, aux = 오프셋)
 *  - dst arena 에서 완료된 dst 구간 밖은 전부 FILL 이어야 한다. 첫 위반 주소·값과 위반 바이트 수 → CK_STRAY
 *  stop = err_idx (에러 job) 또는 n.  */
static uint8_t COV[DST_END - DST_AREA];
static void check_data(const job_t *J, fails_t *F) {
    (void)J; (void)F; (void)COV;
    /* TODO: (1) 디스크립터마다 src CRC == J->crc[i] (아니면 CK_SRC)
     *       (2) i < stop 이면 dst CRC == J->crc[i], 다르면 src/dst 를 바이트 비교해 첫 불일치 → CK_DATA
     *           완료된 dst 구간은 COV 에 표시
     *       (3) dst arena 전체에서 COV 밖인데 FILL 이 아닌 바이트 → 첫 주소/값/개수로 CK_STRAY 하나 */
}

/* ================= Part B: 경계 + 랜덤 job 생성 ================= */
/* kind 와 seed 만으로 결정되는 job (같은 입력 → 같은 job). r = 시드에서 만든 xorshift32 상태.
 *  ALIGNED   n=4, len=64*(1..16), src/dst 64B 정렬, 4K 안 넘음
 *  UNALIGNED n=4, len=1..300, src off 1..3, dst off 1..3 (mod 4), 4K 안 넘음
 *  CROSS_4K  n=3, len=64..575, desc1 의 dst 가 시작 k(1..len-1) 바이트 뒤 4K 경계를 넘음, 나머지는 안 넘음
 *  LEN0      n=3, len = {64, 0, 64}
 *  LEN1      n=3, len=1, src/dst off 0..3
 *  LEN_MAX   n=2, len = {MAX_LEN, 64}
 *  ERR_MID   n=5, len=64*(1..8), e=1..3 의 src 또는 dst 를 UNMAPPED 로 (err_idx=e, err_code=SRC/DST_DEC)
 *  RANDOM    n=1..8, len: 1/8 확률로 0..1, 아니면 0..4096, src/dst off 0..63, 4K 교차 허용   */
static void gen_job(int kind, uint32_t seed, job_t *J) {
    memset(J, 0, sizeof *J);
    J->kind = kind; J->seed = seed; J->err_idx = -1;
    uint32_t r = seed * 0x9E3779B9u + (uint32_t)kind * 0x85EBCA6Bu + 1u;
    if (!r) r = 1;
    uint32_t sn = SRC_AREA, dn = DST_AREA;
    (void)sn; (void)dn; (void)place; (void)place_cross;
    /* TODO: kind 별로 J->n 과 J->s[i] 를 위 표대로 채운다. place()/place_cross()/rnd(&r, n) 사용.
     *       ERR_MID 는 err_idx/err_code 도. */
}

/* ================= Part C: write-back · 레지스터 · IRQ 일관성 ================= */
/* irq = dma_run 이 마지막으로 읽은 IRQ_STAT. 순서가 중요하다:
 *  1) 디스크립터 write-back (DRAM 직접 읽기 — 버스 tick 없음):
 *       i < stop  : STAT == WB_DONE, XFER == len
 *       i == stop (에러 job): STAT == WB_ERR | code<<4, XFER == 0
 *       i > stop  : STAT == 0, XFER == 0 (건드리지 않음)
 *  2) irq == (에러 ? I_ERR : I_DONE) 정확히 (CK_IRQ, addr=DMA_IRQ_STAT, aux=0)
 *     STATUS == code<<4 (BUSY=0), DESC_CNT == stop, BYTE_CNT == Σ len(i<stop), CUR_DESC == desc[에러 ? stop : n-1],
 *     INTC_RAW == LINE_BIT
 *  3) IRQ_STAT 에 irq 를 W1C → IRQ_STAT == 0, INTC_RAW == 0 (둘 다 CK_CLEAR) */
static void check_status(const job_t *J, uint32_t irq, fails_t *F) {
    (void)J; (void)irq; (void)F; (void)expect_reg;
    /* TODO: 1) write-back (mem_rd32) → 2) irq 정확히 · STATUS · DESC_CNT · BYTE_CNT · CUR_DESC · INTC_RAW
     *       (expect_reg) → 3) W1C 후 IRQ_STAT==0, INTC_RAW==0 */
}

/* 실패 기록 하나 → 디버그 엔지니어용 원인 힌트 */
enum { H_NONE, H_LEN0_HANG, H_HANG, H_TAIL_BYTES, H_4K_CROSS, H_DATA, H_SRC_CORRUPT, H_STRAY, H_WB_AFTER_IRQ,
       H_NOT_PROCESSED, H_RAN_PAST_ERR, H_LEN_BIT, H_WB, H_ERR_AS_DONE, H_IRQ, H_BUSY_AFTER_IRQ, H_ERR_CODE,
       H_COUNTER, H_LINE, H_CLEAR };
static const char *const HINT_NAME[] = {"NONE", "LEN0_HANG", "HANG", "TAIL_BYTES", "4K_CROSS", "DATA_CORRUPT",
                                        "SRC_CORRUPT", "STRAY_WRITE", "WB_AFTER_IRQ", "NOT_PROCESSED",
                                        "RAN_PAST_ERROR", "LEN_FIELD_BIT", "WB_MISMATCH", "ERR_AS_DONE",
                                        "IRQ_MISMATCH", "BUSY_AFTER_IRQ", "ERR_CODE", "COUNTER", "IRQ_LINE",
                                        "IRQ_CLEAR"};
/* TIMEOUT : 그 디스크립터 len==0 → LEN0_HANG, 아니면 HANG
 * DATA    : 비정렬 끝((src|dst|len)&3)이고 len-오프셋 ≤ 3 → TAIL_BYTES;
 *           오프셋>0 이고 첫 불일치 주소가 4K 정렬 → 4K_CROSS; 그 밖 DATA_CORRUPT
 * WB_STATUS/WB_XFER: exp≠0, act==0 → 마지막 디스크립터면 WB_AFTER_IRQ, 아니면 NOT_PROCESSED;
 *           exp==0, act≠0 → RAN_PAST_ERROR; XFER 가 0 아닌 값으로 한 비트만 모자람 → LEN_FIELD_BIT; 그 밖 WB_MISMATCH
 * IRQ     : exp ERR, act DONE → ERR_AS_DONE, 그 밖 IRQ_MISMATCH
 * STATUS  : act 의 BUSY → BUSY_AFTER_IRQ, 아니면 ERR_CODE
 * 그 밖   : SRC→SRC_CORRUPT, STRAY→STRAY_WRITE, 카운터/CUR_DESC→COUNTER, LINE→IRQ_LINE, CLEAR→IRQ_CLEAR */
static int hint(const job_t *J, const fail_t *f) {
    (void)J; (void)f;
    return H_NONE; /* TODO: 위 결정표대로 */
}

/* ================= job 실행 · 캠페인 · 리포트 (주어진 코드) ================= */
static void run_job(job_t *J, fails_t *F) {
    memset(F, 0, sizeof *F);
    job_load(J);
    reg_write32(DMA_CTRL, C_SRST);
    unsigned polls = 0;
    uint32_t irq = dma_run(J->desc[0], MAX_POLLS, &polls);
    if (!(irq & (I_DONE | I_ERR))) {
        uint32_t cur = reg_read32(DMA_CUR_DESC);
        add_fail(F, CK_TIMEOUT, desc_index(J, cur), cur, 0, reg_read32(DMA_STATUS), polls);
    } else {
        check_status(J, irq, F);
        check_data(J, F);
    }
    reg_write32(DMA_CTRL, C_SRST);
}

static const char *reg_name(uint32_t off) {
    switch (off) {
    case DMA_CTRL: return "CTRL"; case DMA_STATUS: return "STATUS"; case DMA_IRQ_STAT: return "IRQ_STAT";
    case DMA_DESC_HEAD: return "DESC_HEAD"; case DMA_CUR_DESC: return "CUR_DESC"; case DMA_DESC_CNT: return "DESC_CNT";
    case DMA_BYTE_CNT: return "BYTE_CNT"; case INTC_RAW: return "INTC_RAW"; default: return "?";
    }
}
static void print_fail(const job_t *J, const fail_t *f) {
    printf("FAIL %s job=%s#%u desc=", IP_NAME, KIND_NAME[J->kind], (unsigned)J->seed);
    if (f->desc >= 0) printf("%d/%d", f->desc, J->n); else printf("-/%d", J->n);
    printf(" check=%s ", CHECK_NAME[f->check]);
    if (f->addr < 0x1000u) printf("reg=%s(0x%03X)", reg_name(f->addr), (unsigned)f->addr);
    else printf("addr=0x%08X", (unsigned)f->addr);
    printf(" exp=0x%08X act=0x%08X diff=0x%08X", (unsigned)f->exp, (unsigned)f->act, (unsigned)(f->exp ^ f->act));
    if (f->check == CK_DATA) printf(" first_bad=+0x%X/len=0x%X", (unsigned)f->aux, (unsigned)J->s[f->desc].len);
    if (f->check == CK_STRAY) printf(" stray_bytes=%u", (unsigned)f->aux);
    if (f->check == CK_TIMEOUT) printf(" polls=%u", (unsigned)f->aux);
    printf(" hint=%s\n", HINT_NAME[hint(J, f)]);
}

#define SEEDS 4
typedef struct {
    int fail_jobs[K_COUNT], total;
    int sig_hits;                       /* 지정 kind 의 job 중 기대 시그니처(check+hint)가 나온 수 */
    job_t first_job; fail_t first; bool has_first;
} camp_t;
static void run_campaign(const dma_fault_t *flt, int want_kind, int want_check, int want_hint, camp_t *C) {
    static job_t J;
    static fails_t F;
    memset(C, 0, sizeof *C);
    dma_init(flt);
    for (int k = 0; k < K_COUNT; k++)
        for (uint32_t sd = 1; sd <= SEEDS; sd++) {
            gen_job(k, sd, &J);
            run_job(&J, &F);
            if (F.n == 0) continue;
            C->fail_jobs[k]++; C->total++;
            if (!C->has_first) { C->first_job = J; C->first = F.f[0]; C->has_first = true; }
            bool hit = false;
            for (int i = 0; i < F.n; i++)
                hit |= F.f[i].check == want_check && hint(&J, &F.f[i]) == want_hint;
            C->sig_hits += k == want_kind && hit;
        }
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 생성기 속성 검사 (주어진 코드): 범위 · 겹침 없음 · kind 별 경계 조건 */
static bool in_area(uint32_t a, uint32_t len, uint32_t lo, uint32_t hi) { return a >= lo && (uint64_t)a + len <= hi; }
static bool job_props_ok(const job_t *J) {
    if (J->n < 1 || J->n > MAXD) return false;
    for (int i = 0; i < J->n; i++) {
        const seg_t *s = &J->s[i];
        bool es = i == J->err_idx && s->src == UNMAPPED, ed = i == J->err_idx && s->dst == UNMAPPED;
        if (!es && !in_area(s->src, s->len, SRC_AREA, SRC_END)) return false;
        if (!ed && !in_area(s->dst, s->len, DST_AREA, DST_END)) return false;
        for (int j = 0; j < i; j++) {
            const seg_t *t = &J->s[j];
            if (s->len && t->len && s->dst < t->dst + t->len && t->dst < s->dst + s->len) return false;
            if (s->len && t->len && s->src < t->src + t->len && t->src < s->src + s->len) return false;
        }
        bool x = crosses_4k(s->dst, s->len) || crosses_4k(s->src, s->len);
        switch (J->kind) {
        case K_ALIGNED:   if (((s->src | s->dst | s->len) & 63u) || x) return false; break;
        case K_UNALIGNED: if (!(s->src & 3u) || !(s->dst & 3u) || x) return false; break;
        case K_CROSS4K:   if (crosses_4k(s->dst, s->len) != (i == 1) || crosses_4k(s->src, s->len)) return false; break;
        case K_LEN0:      if ((s->len == 0) != (i == 1)) return false; break;
        case K_LEN1:      if (s->len != 1) return false; break;
        case K_LENMAX:    if (i == 0 && s->len != MAX_LEN) return false; break;
        case K_ERRMID:    if (i == J->err_idx && es == ed) return false; break;
        default: break;
        }
    }
    if (J->kind == K_ERRMID && (J->err_idx < 1 || J->err_idx > 3)) return false;
    if (J->kind != K_ERRMID && J->err_idx != -1) return false;
    return true;
}

int main(void) {
    static job_t J;
    static fails_t F;

    /* ---- mock sanity (주어진 코드가 스펙대로인지) ---- */
    dma_init(NULL);
    CHECK(reg_read32(DMA_STATUS) == 0 && reg_read32(DMA_IRQ_STAT) == 0 && reg_read32(INTC_RAW) == 0);
    CHECK(reg_read32(0x7FC) == 0xDEADBEEFu);
    CHECK(in_dram(DST_END - 4, 4) && !in_dram(DST_END - 4, 5) && !in_dram(UNMAPPED, 1) && in_dram(DST_END, 0));
    CHECK(crosses_4k(0x80020FFFu, 2) && !crosses_4k(0x80020FFFu, 1) && !crosses_4k(0x80021000u, 0));

    /* ---- Part A: CRC-32 ---- */
    const uint8_t *chk = (const uint8_t *)"123456789";
    CHECK(crc32_update(0, chk, 9) == 0xCBF43926u);              /* CRC-32 표준 check 값 */
    CHECK(crc32_update(0, (const uint8_t *)"a", 1) == 0xE8B7BE43u);
    CHECK(crc32_update(0, chk, 0) == 0);
    CHECK(crc32_update(crc32_update(0, chk, 4), chk + 4, 5) == 0xCBF43926u);
    {   /* 임의 분할 이어붙이기 == 한 번에 (SG 세그먼트를 이어서 CRC 하는 근거) */
        static uint8_t buf[10000];
        uint32_t r = 12345u;
        for (size_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)xs32(&r);
        uint32_t whole = crc32_update(0, buf, sizeof buf);
        int ok = 0;
        for (int t = 0; t < 50; t++) {
            size_t cut = rnd(&r, sizeof buf + 1);
            ok += crc32_update(crc32_update(0, buf, cut), buf + cut, sizeof buf - cut) == whole;
        }
        CHECK(ok == 50);
        uint8_t save = buf[777];                                  /* 1비트만 바꿔도 CRC 가 바뀐다 */
        buf[777] ^= 0x10;
        CHECK(crc32_update(0, buf, sizeof buf) != whole);
        buf[777] = save;
    }

    /* ---- Part A: check_data — host memcpy 를 "완벽한 DMA" 로 써서 checker 자체를 검증 ---- */
    gen_job(K_ALIGNED, 1, &J);
    job_load(&J);
    for (int i = 0; i < J.n; i++) memcpy(mp(J.s[i].dst), mp(J.s[i].src), J.s[i].len);
    memset(&F, 0, sizeof F);
    check_data(&J, &F);
    CHECK(J.n == 4 && F.n == 0);
    if (J.n == 4) {                                               /* (starter 에서 n==0 일 때 보호) */
        *mp(J.s[2].dst + 17) ^= 0x01;                             /* 세그먼트 2 의 +17 한 비트 */
        *mp(J.s[3].dst + J.s[3].len) = 0x00;                      /* 세그먼트 3 바로 뒤 1바이트 */
        *mp(J.s[0].src) ^= 0x80;                                  /* src 0 의 첫 바이트 */
    }
    memset(&F, 0, sizeof F);
    check_data(&J, &F);
    CHECK(F.n == 3);                                              /* dst 0 은 골든과 같다 → DATA 는 통과 */
    CHECK(F.f[0].check == CK_SRC && F.f[0].desc == 0 && F.f[0].addr == J.s[0].src);
    CHECK(F.f[1].check == CK_DATA && F.f[1].desc == 2 && F.f[1].aux == 17 && F.f[1].addr == J.s[2].dst + 17);
    CHECK(F.f[2].check == CK_STRAY && F.f[2].addr == J.s[3].dst + J.s[3].len && F.f[2].aux == 1 &&
          F.f[2].act == 0x00);

    /* ---- Part A: dma_run + 좋은 엔진에서 한 체인 끝까지 ---- */
    dma_init(NULL);
    gen_job(K_ALIGNED, 1, &J);
    job_load(&J);
    unsigned polls = 0;
    uint32_t irq = dma_run(J.desc[0], MAX_POLLS, &polls);
    CHECK(irq == I_DONE && polls > 4 && polls < 100);
    CHECK(reg_read32(INTC_RAW) == LINE_BIT && reg_read32(DMA_STATUS) == 0);
    memset(&F, 0, sizeof F);
    check_data(&J, &F);
    CHECK(F.n == 0);
    gen_job(K_LENMAX, 1, &J);                                    /* 잔여 DONE 을 안 지웠다면 1번 만에 "끝" */
    job_load(&J);
    irq = dma_run(J.desc[0], MAX_POLLS, &polls);
    CHECK(irq == I_DONE && polls > 256);                         /* 65535 B / 256 B per tick */
    gen_job(K_ALIGNED, 1, &J);
    fails_t F2 = {0};
    run_job(&J, &F2);
    CHECK(F2.n == 0 && D.irq == 0 && !D.busy);                   /* 끝나면 깨끗 */
    dma_fault_t fh = {.len0_hang = true};                        /* timeout 이 반드시 끝나는지 */
    dma_init(&fh);
    gen_job(K_LEN0, 1, &J);
    job_load(&J);
    irq = dma_run(J.desc[0], 500, &polls);
    CHECK(!(irq & (I_DONE | I_ERR)) && polls == 500);

    /* ---- Part B: 생성기 — 결정성 · 범위 · 겹침 · kind 별 경계 ---- */
    int props = 0, determ = 0, cases = 0;
    for (int k = 0; k < K_COUNT; k++)
        for (uint32_t sd = 1; sd <= 16; sd++) {
            static job_t J2;
            gen_job(k, sd, &J);
            gen_job(k, sd, &J2);
            cases++;
            props += job_props_ok(&J);
            determ += memcmp(&J, &J2, sizeof J) == 0;
        }
    CHECK(cases == 128 && props == 128 && determ == 128);
    {   /* RANDOM 이 경계도 실제로 밟는가 (시드 1..64) */
        int len0 = 0, len1 = 0, cross = 0, unal = 0, big = 0;
        for (uint32_t sd = 1; sd <= 64; sd++) {
            gen_job(K_RANDOM, sd, &J);
            for (int i = 0; i < J.n; i++) {
                len0 += J.s[i].len == 0; len1 += J.s[i].len == 1; big += J.s[i].len > 4000;
                cross += crosses_4k(J.s[i].dst, J.s[i].len); unal += ((J.s[i].src | J.s[i].dst) & 3u) != 0;
            }
        }
        CHECK(len0 > 0 && len1 > 0 && cross > 0 && unal > 0 && big > 0);
    }
    gen_job(K_ERRMID, 3, &J);
    CHECK(J.err_idx >= 1 && J.err_idx <= 3 && (J.err_code == ERR_SRC_DEC || J.err_code == ERR_DST_DEC));

    /* ---- Part B: 좋은 엔진은 모든 kind × 시드를 통과 (에러 job 은 '정확히 거기서 멈춤' 이 통과 조건) ---- */
    dma_init(NULL);
    int good = 0, err_jobs = 0;
    for (int k = 0; k < K_COUNT; k++)
        for (uint32_t sd = 1; sd <= 8; sd++) {
            gen_job(k, sd, &J);
            run_job(&J, &F);
            good += F.n == 0;
            if (J.err_idx >= 0 && F.n == 0) {           /* 에러 뒤 디스크립터의 dst 는 FILL 그대로 */
                const seg_t *s = &J.s[J.err_idx + 1];
                err_jobs += *mp(s->dst) == FILL && *mp(s->dst + s->len - 1) == FILL;
            }
        }
    CHECK(good == 64 && err_jobs == 8);

    /* ---- Part C: hint 단위 테스트 (손으로 만든 실패 기록) ---- */
    gen_job(K_CROSS4K, 2, &J);
    job_load(&J);
    {
        uint32_t k1 = round_up(J.s[1].dst, PAGE) - J.s[1].dst;     /* desc1 이 경계를 넘는 오프셋 */
        struct { fail_t f; int h; } ht[] = {
            {{CK_DATA, 1, J.s[1].dst + k1, 1, 2, k1}, H_4K_CROSS},
            {{CK_DATA, 0, J.s[0].dst + 5, 1, 2, 5}, H_DATA},
            {{CK_WB_STATUS, 2, J.desc[2] + D_STAT, WB_DONE, 0, 0}, H_WB_AFTER_IRQ},
            {{CK_WB_STATUS, 1, J.desc[1] + D_STAT, WB_DONE, 0, 0}, H_NOT_PROCESSED},
            {{CK_WB_STATUS, 2, J.desc[2] + D_STAT, 0, WB_DONE, 0}, H_RAN_PAST_ERR},
            {{CK_WB_XFER, 0, J.desc[0] + D_XFER, 0xFFFF, 0x7FFF, 0}, H_LEN_BIT},
            {{CK_WB_XFER, 0, J.desc[0] + D_XFER, 0x100, 0x0FF, 0}, H_WB},
            {{CK_IRQ, -1, DMA_IRQ_STAT, I_ERR, I_DONE, 0}, H_ERR_AS_DONE},
            {{CK_IRQ, -1, DMA_IRQ_STAT, I_DONE, I_DONE | I_ERR, 0}, H_IRQ},
            {{CK_STATUS, -1, DMA_STATUS, 0x10, 0x11, 0}, H_BUSY_AFTER_IRQ},
            {{CK_STATUS, -1, DMA_STATUS, 0x10, 0x00, 0}, H_ERR_CODE},
            {{CK_BYTE_CNT, -1, DMA_BYTE_CNT, 300, 200, 0}, H_COUNTER},
            {{CK_STRAY, -1, DST_AREA, FILL, 0, 3}, H_STRAY},
            {{CK_CLEAR, -1, INTC_RAW, 0, LINE_BIT, 0}, H_CLEAR},
        };
        int hok = 0;
        for (size_t i = 0; i < sizeof ht / sizeof ht[0]; i++) hok += hint(&J, &ht[i].f) == ht[i].h;
        CHECK(hok == (int)(sizeof ht / sizeof ht[0]));
        gen_job(K_LEN0, 1, &J);
        fail_t t0 = {CK_TIMEOUT, 1, 0, 0, 1, 4096}, t1 = {CK_TIMEOUT, 0, 0, 0, 1, 4096};
        fail_t tb = {CK_DATA, 0, 0, 1, 2, 0};
        CHECK(hint(&J, &t0) == H_LEN0_HANG && hint(&J, &t1) == H_HANG);
        gen_job(K_UNALIGNED, 1, &J);
        tb.addr = J.s[0].dst + J.s[0].len - ((J.s[0].dst + J.s[0].len) & 3u);
        tb.aux = tb.addr - J.s[0].dst;
        CHECK(((J.s[0].dst + J.s[0].len) & 3u) == 0 || hint(&J, &tb) == H_TAIL_BYTES);
    }

    /* ---- Part C: 결함 주입 야간 회귀 (8 kind × 4 seed = 32 job / 결함) ---- */
    static camp_t C;
    run_campaign(NULL, -1, -1, -1, &C);
    CHECK(C.total == 0);
    printf("[good] jobs=%d failing=%d bus_accesses=%u ticks=%u\n", K_COUNT * SEEDS, C.total, D.nacc, D.ticks);

    struct { const char *name; dma_fault_t f; int kind, check, hint; } camp[] = {
        {"wrap_4k",      {.wrap_4k = true},      K_CROSS4K,   CK_DATA,      H_4K_CROSS},
        {"tail_drop",    {.tail_drop = true},    K_UNALIGNED, CK_DATA,      H_TAIL_BYTES},
        {"len_15bit",    {.len_15bit = true},    K_LENMAX,    CK_WB_XFER,   H_LEN_BIT},
        {"len0_hang",    {.len0_hang = true},    K_LEN0,      CK_TIMEOUT,   H_LEN0_HANG},
        {"err_continue", {.err_continue = true}, K_ERRMID,    CK_STATUS,    H_BUSY_AFTER_IRQ},
        {"err_as_done",  {.err_as_done = true},  K_ERRMID,    CK_IRQ,       H_ERR_AS_DONE},
        {"irq_early",    {.irq_early = true},    K_ALIGNED,   CK_WB_STATUS, H_WB_AFTER_IRQ},
        {"bytecnt_last", {.bytecnt_last = true}, K_ALIGNED,   CK_BYTE_CNT,  H_COUNTER},
    };
    enum { NF = sizeof camp / sizeof camp[0] };
    static camp_t R[NF];
    printf("%-13s", "fault");
    for (int k = 0; k < K_COUNT; k++) printf(" %-9s", KIND_NAME[k]);
    printf(" total\n");
    for (int i = 0; i < NF; i++) {
        run_campaign(&camp[i].f, camp[i].kind, camp[i].check, camp[i].hint, &R[i]);
        printf("%-13s", camp[i].name);
        for (int k = 0; k < K_COUNT; k++) printf(" %d/%-7d", R[i].fail_jobs[k], SEEDS);
        printf(" %d/%d\n", R[i].total, K_COUNT * SEEDS);
        CHECK(R[i].fail_jobs[camp[i].kind] == SEEDS && R[i].sig_hits == SEEDS);   /* 지정 kind 가 매번, 맞는 시그니처로 */
    }
    for (int i = 0; i < NF; i++) {
        printf("[%s] first: ", camp[i].name);
        if (R[i].has_first) print_fail(&R[i].first_job, &R[i].first); else printf("(none)\n");
    }
    /* 경계 케이스를 명시적으로 만들어야 하는 이유: 담당 kind 밖에서는 안 잡히는 결함들 */
    CHECK(R[0].fail_jobs[K_ALIGNED] == 0 && R[0].fail_jobs[K_UNALIGNED] == 0 && R[0].fail_jobs[K_ERRMID] == 0);
    CHECK(R[1].fail_jobs[K_ALIGNED] == 0 && R[1].fail_jobs[K_LEN0] == 0 && R[1].fail_jobs[K_ERRMID] == 0);
    CHECK(R[2].total == SEEDS && R[2].fail_jobs[K_LENMAX] == SEEDS);           /* LEN_MAX 만 잡는다 */
    CHECK(R[3].fail_jobs[K_ALIGNED] == 0 && R[3].fail_jobs[K_LENMAX] == 0);
    CHECK(R[4].total == SEEDS && R[5].total == SEEDS);                        /* 에러 경로는 ERR_MID 만 */
    CHECK(R[6].fail_jobs[K_ERRMID] == 0);                                     /* 에러로 끝나는 체인은 영향 없음 */

    /* ---- Part C: 리포트 — 4K wrap 결함의 한 job 전체 ---- */
    dma_init(&camp[0].f);
    gen_job(K_CROSS4K, 2, &J);
    run_job(&J, &F);
    printf("---- report: %s fault=%s job=%s#%u ----\n", IP_NAME, camp[0].name, KIND_NAME[J.kind], (unsigned)J.seed);
    for (int i = 0; i < J.n; i++)
        printf("  desc%d @0x%08X src=0x%08X dst=0x%08X len=%u%s\n", i, (unsigned)J.desc[i], (unsigned)J.s[i].src,
               (unsigned)J.s[i].dst, (unsigned)J.s[i].len, crosses_4k(J.s[i].dst, J.s[i].len) ? "  (dst crosses 4K)" : "");
    for (int i = 0; i < F.n; i++) print_fail(&J, &F.f[i]);
    int h4k = 0, hstray = 0;
    for (int i = 0; i < F.n; i++) { h4k += hint(&J, &F.f[i]) == H_4K_CROSS; hstray += F.f[i].check == CK_STRAY; }
    CHECK(h4k == 1 && hstray == 1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
