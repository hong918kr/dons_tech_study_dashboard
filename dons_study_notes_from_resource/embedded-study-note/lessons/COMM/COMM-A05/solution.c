/* COMM-A05 I3C 기초: 동적 주소 할당과 In-Band Interrupt
 *
 * 모델 (이 레슨의 정의 — MIPI I3C Basic 의 단순화. 스펙 확인 권장 항목은 md 에 표시):
 *   - 트랜잭션 수준 버스 + "중재가 일어나는 곳만" 비트 수준. 비트 수준 = 오픈드레인 wired-AND
 *     (0 이 우세, 1 = 놓음). 각 참가자는 MSB 부터 비트를 내고, 1 을 냈는데 0 을 읽으면 빠진다
 *     (COMM-M02 I2C 중재와 같은 규칙).
 *   - 주소 헤더 = (7비트 주소 << 1) | RnW. START 뒤 헤더는 중재 가능: 컨트롤러는 7E/W 를,
 *     IBI 를 원하는 타깃은 (DA<<1)|1 을, Hot-Join 타깃은 (0x02<<1)|0 을 동시에 낸다.
 *     Sr(repeated START) 뒤 헤더는 중재 없음 (타깃은 START 뒤에만 요청한다).
 *   - ENTDAA: START 7E/W + CCC 0x07, 그 다음 라운드마다 Sr 7E/R → 주소 없는 타깃이 ACK →
 *     48비트 PID + BCR + DCR = 64비트를 오픈드레인으로 중재 → 컨트롤러가 7비트 DA + 홀수 패리티
 *     비트를 보냄 → 이긴 타깃이 패리티 확인 후 ACK. ACK 하는 타깃이 없으면 STOP.
 *   - 레거시 I2C 디바이스는 자기 정적 주소 헤더에만 ACK 한다 (그 외 I3C 트래픽은 무시 — 실제로는
 *     50 ns 스파이크 필터 덕분, md 참고).
 *   - 클럭 수: 오픈드레인(od_clk) / 푸시풀(pp_clk) 따로 센다. 헤더·ACK = OD 9, CCC·데이터 바이트
 *     = PP 9 (8비트 + T 비트), ENTDAA 의 64비트와 DA 바이트 = OD.
 *   - 버스 관찰자(collisions, contention): 펌웨어가 볼 수 없는 것 (LA·스코프로만 보임) — 테스트용.
 * Part A: od_arbitrate, 주소 슬롯 표 + 빈 주소 찾기, DA 패리티, ENTDAA
 * Part B: START 헤더 중재 (IBI / Hot-Join / 컨트롤러), DAA 검증 (GETPID/BCR/DCR), SETDASA
 * Part C: Linux i3c_master_bus_init 흐름의 버스 초기화 + 브링업 로그 + 혼합 버스 소크 테스트
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 상수 (주어짐) ---------------- */
enum { MAX_T = 128, MAX_L = 8, MAX_DEV = 128, LOG_CAP = 160, LOG_W = 100, IBIQ_CAP = 64 };

#define I3C_BCAST   0x7E
#define I3C_HJ_ADDR 0x02
#define HDR(a, rnw) ((uint8_t)(((unsigned)(a) << 1) | (unsigned)(rnw)))
#define HDR_NONE    0xFF            /* 컨트롤러가 헤더를 내지 않음 = SDA 를 놓은 채 클럭만 */

/* CCC 번호: Linux include/linux/i3c/ccc.h 와 같은 값 (broadcast 0x00-0x7F, direct 0x80-0xFE) */
enum { CCC_ENEC_B = 0x00, CCC_DISEC_B = 0x01, CCC_RSTDAA_B = 0x06, CCC_ENTDAA = 0x07,
       CCC_ENEC_D = 0x80, CCC_DISEC_D = 0x81, CCC_SETDASA = 0x87, CCC_SETNEWDA = 0x88,
       CCC_GETPID = 0x8D, CCC_GETBCR = 0x8E, CCC_GETDCR = 0x8F };
enum { EV_SIR = 0x01, EV_HJ = 0x08 };                    /* ENEC/DISEC 이벤트 비트 */
enum { BCR_IBI_REQ = 0x02, BCR_IBI_PAYLOAD = 0x04 };     /* BCR[1], BCR[2] */

enum { I3C_OK = 0, I3C_ERR_ARG = -1, I3C_ERR_NACK = -2, I3C_ERR_NOADDR = -3, I3C_ERR_DAA_NACK = -4,
       I3C_ERR_PID_MISMATCH = -5, I3C_ERR_MISSING = -6, I3C_ERR_PREEMPT = -7 };
enum { HDR_IDLE = 0, HDR_WON = 1, HDR_SERVED = 2 };      /* ctrl_arb_header 결과 */
enum { SLOT_FREE, SLOT_RSVD, SLOT_I2C, SLOT_I3C };

/* ---------------- 버스 위 노드 (주어짐) ---------------- */
typedef enum { TF_NONE, TF_NO_RELEASE } TFault;          /* NO_RELEASE: 중재에 져도 계속 비트를 냄 */
typedef struct {
    const char *name;
    uint64_t pid;            /* 48비트 Provisioned ID */
    uint8_t  bcr, dcr;
    uint8_t  sa, da;         /* 정적 주소 (0 = 없음), 동적 주소 (0 = 아직 없음) */
    bool     online;
    uint8_t  ev_en;          /* 타깃 쪽 이벤트 허용. 전원 인가 시 SIR|HJ (이 모델의 정의) */
    bool     ibi_req, hj_req;
    uint8_t  mdb;            /* IBI 때 보낼 Mandatory Data Byte */
    TFault   fault;
    uint8_t  status;         /* private read 로 읽히는 값 */
    int      n_ibi_ok, n_ibi_nack;
} Target;

typedef struct { const char *name; uint8_t sa, val; int n_acked; } Legacy;

typedef struct {
    Target  *t[MAX_T]; int nt;
    Legacy  *l[MAX_L]; int nl;
    uint32_t od_clk, pp_clk;
    int      collisions;     /* 관찰자: 한 주소 헤더에 2개 이상 ACK (broadcast 제외) */
    int      contention;     /* 관찰자: 푸시풀 데이터에서 두 드라이버가 다른 값 */
    int      dup_da;         /* 관찰자: ENTDAA 한 라운드에서 2개 이상이 DA 를 받음 */
    uint8_t  corrupt_da; int corrupt_n;   /* 고장 주입: 다음 corrupt_n 개 DA 바이트에 XOR */
    bool     log_on; char log[LOG_CAP][LOG_W]; int nlog;
} Bus;

__attribute__((format(printf, 2, 3)))
static void blog(Bus *b, const char *fmt, ...) {
    if (!b->log_on || b->nlog >= LOG_CAP) return;
    int k = snprintf(b->log[b->nlog], LOG_W, "[%5u] ", (unsigned)(b->od_clk + b->pp_clk));
    va_list ap; va_start(ap, fmt);
    vsnprintf(b->log[b->nlog] + k, (size_t)(LOG_W - k), fmt, ap);
    va_end(ap);
    b->nlog++;
}

/* ---------------- 컨트롤러 (주어짐) ---------------- */
typedef struct { uint64_t pid; uint8_t bcr, dcr, da; bool ibi_on; } Dev;
typedef struct { uint8_t da, mdb; bool has_mdb; } IbiEv;
typedef struct {
    Bus    *b;
    uint8_t slot[128];
    Dev     dev[MAX_DEV]; int ndev;
    bool    hj_enabled, hj_daa_pending;
    IbiEv   q[IBIQ_CAP]; int nq, q_drop;
    int     n_ibi, n_ibi_nack, n_hj, n_preempt;
    uint8_t last_ccc;        /* 마지막으로 보낸 CCC (디버그용) */
    bool    naive_alloc;     /* 실험용 잘못된 할당기: 레거시 I2C 주소를 빈 주소로 취급 */
} Ctrl;

#define GIVEN __attribute__((unused))   /* 주어진 도우미: 스텁 상태(starter)에서 안 쓰여도 경고 없게 */

static uint64_t t_id64(const Target *t) { return (t->pid << 16) | ((uint64_t)t->bcr << 8) | t->dcr; }
static int pop8(uint8_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

GIVEN static Dev *dev_find(Ctrl *c, uint8_t da) {
    for (int i = 0; i < c->ndev; i++) if (c->dev[i].da == da) return &c->dev[i];
    return NULL;
}
GIVEN static Dev *dev_add(Ctrl *c, uint64_t pid, uint8_t bcr, uint8_t dcr, uint8_t da) {
    if (c->ndev >= MAX_DEV) return NULL;
    Dev *d = &c->dev[c->ndev++];
    d->pid = pid; d->bcr = bcr; d->dcr = dcr; d->da = da; d->ibi_on = false;
    return d;
}

/* 푸시풀 바이트 1개를 n 개 드라이버가 낼 때: 버스 = AND (모델의 선택), 값이 다르면 contention */
static uint8_t bus_pp_read(Bus *b, int n, const uint8_t *v) {
    uint8_t r = 0xFF;
    for (int k = 0; k < n; k++) { r &= v[k]; if (v[k] != v[0]) b->contention++; }
    b->pp_clk += 9;
    return r;
}
static bool bcast_ack(Bus *b) {
    for (int i = 0; i < b->nt; i++) if (b->t[i]->online) return true;
    return false;
}

/* ================= Part A ================= */

/* od_arbitrate: n 명이 각자 w[k] 의 하위 nbits 비트를 MSB 부터 오픈드레인으로 낸다.
 * in[k] (입력) = 참가 여부, (출력) = 끝까지 남았는지. 버스 비트 = 참가 중인 드라이버의 AND.
 * 1 을 냈는데 0 을 읽은 참가자는 그 비트에서 빠지고 lost_at[k] = 그 비트의 MSB 기준 위치(0..nbits-1).
 * 끝까지 남은 참가자는 lost_at = -1. sticky[k] (NULL 가능) = 고장: 져도 빠지지 않음.
 * 반환: 버스에 실제로 보인 nbits 비트 값. */
static uint64_t od_arbitrate(int n, const uint64_t *w, const bool *sticky, int nbits,
                             bool *in, int *lost_at) {
    /* TODO(A1): 비트 단위 wired-AND 중재 */
    uint64_t bus = 0;
    for (int k = 0; k < n; k++) lost_at[k] = -1;
    for (int bit = nbits - 1; bit >= 0; bit--) {
        unsigned level = 1;
        for (int k = 0; k < n; k++)
            if (in[k]) level &= (unsigned)(w[k] >> bit) & 1u;
        bus = (bus << 1) | level;
        if (level) continue;
        for (int k = 0; k < n; k++)
            if (in[k] && ((w[k] >> bit) & 1u) && !(sticky && sticky[k])) {
                in[k] = false;
                lost_at[k] = nbits - 1 - bit;
            }
    }
    return bus;
}

/* addr_slots_init: 128칸 주소 표. 예약 = 0x00-0x07, 0x7E, 그리고 0x7E 에서 1비트만 다른 7개
 * (Linux i3c_bus_init_addrslots 와 같은 집합). 레거시 I2C 정적 주소 = SLOT_I2C (예약 칸이면 그대로 RSVD).
 * 반환: SLOT_FREE 칸 수. */
static int addr_slots_init(Ctrl *c, const uint8_t *i2c_sa, int ni2c) {
    /* TODO(A2): 예약 주소 + 레거시 주소 표시 */
    memset(c->slot, SLOT_FREE, sizeof c->slot);
    for (int a = 0; a < 8; a++) c->slot[a] = SLOT_RSVD;
    c->slot[I3C_BCAST] = SLOT_RSVD;
    for (int i = 0; i < 7; i++) c->slot[I3C_BCAST ^ (1 << i)] = SLOT_RSVD;
    for (int i = 0; i < ni2c; i++)
        if (i2c_sa[i] < 128 && c->slot[i2c_sa[i]] == SLOT_FREE) c->slot[i2c_sa[i]] = SLOT_I2C;
    int nfree = 0;
    for (int a = 0; a < 128; a++) nfree += c->slot[a] == SLOT_FREE;
    return nfree;
}

/* get_free_addr: start 이상에서 가장 작은 빈 주소, 없으면 -1.
 * naive_alloc 이면 SLOT_I2C 도 빈 칸으로 본다 (실험용 버그). */
static int get_free_addr(const Ctrl *c, int start) {
    /* TODO(A3): 위로 올라가며 첫 빈 칸 */
    for (int a = start < 0 ? 0 : start; a < 128; a++)
        if (c->slot[a] == SLOT_FREE || (c->naive_alloc && c->slot[a] == SLOT_I2C)) return a;
    return -1;
}

/* da_with_parity: ENTDAA 에서 컨트롤러가 보내는 바이트 = DA[6:0] 다음에 패리티 비트.
 * 패리티 = 8비트 전체의 1 의 개수가 홀수가 되게 (odd parity). */
static uint8_t da_with_parity(uint8_t da) {
    /* TODO(A4): (da << 1) | PAR */
    da &= 0x7F;
    return (uint8_t)((da << 1) | (pop8(da) % 2 == 0 ? 1u : 0u));
}

/* ================= Part B (선언 — 본문은 아래) ================= */
static int ctrl_arb_header(Ctrl *c, uint8_t own_hdr);

/* ctrl_start: 컨트롤러가 START + 7E/W 헤더를 "이길 때까지" (IBI·Hot-Join 이 이기면 처리 후 다시).
 * 상한 8번. 스텁 대비: ctrl_arb_header 가 IDLE 을 돌려주면 PREEMPT 로 끝난다. (주어짐) */
static int ctrl_start(Ctrl *c) {
    for (int k = 0; k < 8; k++) {
        int r = ctrl_arb_header(c, HDR(I3C_BCAST, 0));
        if (r == HDR_WON) return I3C_OK;
        if (r == HDR_IDLE) break;
        c->n_preempt++;
    }
    return I3C_ERR_PREEMPT;
}
/* ctrl_ccc_begin: START 7E/W (ACK 확인) + CCC 바이트(PP). (주어짐) */
static int ctrl_ccc_begin(Ctrl *c, uint8_t ccc) {
    int r = ctrl_start(c);
    if (r) return r;
    if (!bcast_ack(c->b)) { blog(c->b, "START 7E/W NACK -> STOP"); return I3C_ERR_NACK; }
    c->b->pp_clk += 9;
    c->last_ccc = ccc;
    return I3C_OK;
}

/* 이벤트 CCC 본문 (CCC 바이트 이후): da == 0 이면 broadcast, 아니면 Sr DA/W 뒤 이벤트 바이트. (주어짐) */
static void events_body(Ctrl *c, bool en, uint8_t da, uint8_t ev, const char *how) {
    Bus *b = c->b;
    for (int i = 0; i < b->nt; i++) {
        Target *t = b->t[i];
        if (!t->online || (da && t->da != da)) continue;
        if (en) t->ev_en |= ev; else t->ev_en &= (uint8_t)~ev;
    }
    if (da) b->od_clk += 9;
    b->pp_clk += 9;
    if (da) blog(b, "%s 7E/W CCC %s(0x%02X) Sr %02X/W ev=0x%02X STOP", how, en ? "ENEC" : "DISEC",
                 (en ? CCC_ENEC_B : CCC_DISEC_B) | 0x80, da, ev);
    else    blog(b, "%s 7E/W CCC %s(0x%02X) ev=0x%02X STOP", how, en ? "ENEC" : "DISEC",
                 en ? CCC_ENEC_B : CCC_DISEC_B, ev);
}
GIVEN static int ctrl_events(Ctrl *c, bool en, uint8_t da, uint8_t ev) {
    uint8_t ccc = (uint8_t)((en ? CCC_ENEC_B : CCC_DISEC_B) | (da ? 0x80 : 0));
    int r = ctrl_ccc_begin(c, ccc);
    if (r) return r;
    events_body(c, en, da, ev, "START");
    return I3C_OK;
}
/* Sr 뒤에 바로 보내는 이벤트 CCC (중재 없음 — IBI/HJ 를 NACK 한 직후 쓰는 경로). (주어짐) */
GIVEN static void ctrl_events_sr(Ctrl *c, bool en, uint8_t da, uint8_t ev) {
    c->b->od_clk += 9; c->b->pp_clk += 9;            /* Sr 7E/W + CCC 바이트 */
    events_body(c, en, da, ev, "Sr");
}

GIVEN static int ctrl_rstdaa(Ctrl *c) {                    /* (주어짐) */
    int r = ctrl_ccc_begin(c, CCC_RSTDAA_B);
    if (r) return r;
    for (int i = 0; i < c->b->nt; i++) if (c->b->t[i]->online) c->b->t[i]->da = 0;
    for (int a = 0; a < 128; a++) if (c->slot[a] == SLOT_I3C) c->slot[a] = SLOT_FREE;
    c->ndev = 0;
    blog(c->b, "RSTDAA bcast STOP");
    return I3C_OK;
}

/* 타깃이 GET CCC 에 돌려주는 j 번째 바이트 (주어짐) */
static uint8_t t_get_byte(const Target *t, uint8_t ccc, int j) {
    if (ccc == CCC_GETPID) return (uint8_t)(t->pid >> (8 * (5 - j)));
    if (ccc == CCC_GETBCR) return t->bcr;
    if (ccc == CCC_GETDCR) return t->dcr;
    return 0xFF;
}
/* ctrl_get: direct GET CCC = START 7E/W, CCC, Sr DA/R, n 바이트(PP), STOP.
 * 반환: ACK 한 타깃 수(관찰자 값, 펌웨어는 ACK 유무만 안다) 또는 음수 오류. (주어짐) */
static int ctrl_get(Ctrl *c, uint8_t ccc, uint8_t da, uint8_t *buf, int n) {
    int r = ctrl_ccc_begin(c, ccc);
    if (r) return r;
    Bus *b = c->b; const Target *rs[MAX_T]; int nr = 0;
    for (int i = 0; i < b->nt; i++)
        if (b->t[i]->online && b->t[i]->da == da) rs[nr++] = b->t[i];
    b->od_clk += 9;
    if (nr > 1) b->collisions++;
    if (nr == 0) { blog(b, "GET 0x%02x Sr %02X/R NACK -> STOP", ccc, da); return I3C_ERR_NACK; }
    for (int j = 0; j < n; j++) {
        uint8_t v[MAX_T];
        for (int k = 0; k < nr; k++) v[k] = t_get_byte(rs[k], ccc, j);
        buf[j] = bus_pp_read(b, nr, v);
    }
    return nr;
}

/* ctrl_priv_read: START 7E/W (중재), Sr ADDR/R, 1바이트, STOP. 응답자 = DA 가 같은 I3C 타깃
 * + 정적 주소가 같은 레거시 I2C. 반환: 0 (ACK) / I3C_ERR_NACK / 오류. (주어짐) */
static int ctrl_priv_read(Ctrl *c, uint8_t addr, uint8_t *out) {
    int r = ctrl_start(c);
    if (r) return r;
    Bus *b = c->b; uint8_t v[MAX_T + MAX_L]; int nr = 0;
    for (int i = 0; i < b->nt; i++)
        if (b->t[i]->online && b->t[i]->da && b->t[i]->da == addr) v[nr++] = b->t[i]->status;
    for (int i = 0; i < b->nl; i++)
        if (b->l[i]->sa == addr) { v[nr++] = b->l[i]->val; b->l[i]->n_acked++; }
    b->od_clk += 9;
    if (nr > 1) { b->collisions++; blog(b, "!! %d devices ACKed %02X/R (address collision)", nr, addr); }
    if (nr == 0) { blog(b, "START 7E/W Sr %02X/R NACK STOP", addr); return I3C_ERR_NACK; }
    *out = bus_pp_read(b, nr, v);
    blog(b, "START 7E/W Sr %02X/R ACK data 0x%02X STOP", addr, *out);
    return I3C_OK;
}

/* ctrl_entdaa: ENTDAA 전체 흐름. 반환: 이번에 할당한 수 (>= 0) 또는 음수 오류.
 *   - START 7E/W + CCC 0x07 (ctrl_ccc_begin). broadcast NACK(타깃 없음) → 0.
 *   - 라운드: Sr 7E/R (OD 9) → 주소 없는 온라인 타깃이 없으면 NACK → STOP, 끝.
 *     → 64비트 ID 중재 (OD 64) → 주소 고르기: get_free_addr(c, 8) (가장 낮은 빈 주소),
 *       없으면 STOP + I3C_ERR_NOADDR.
 *     → DA 바이트 (OD 9, corrupt 주입 반영) → 남은 타깃은 패리티가 맞으면 da 를 받고 ACK.
 *     → ACK: 표에 추가(dev_add, 컨트롤러가 *보낸* 주소), slot = I3C.
 *       NACK: 같은 ID 가 두 번 연속 NACK 이면 STOP + I3C_ERR_DAA_NACK, 아니면 다음 라운드.
 *   - 라운드 상한 2*MAX_T+2. */
static int ctrl_entdaa(Ctrl *c) {
    /* TODO(A5): ENTDAA 라운드 루프 */
    Bus *b = c->b;
    int r = ctrl_ccc_begin(c, CCC_ENTDAA);
    if (r == I3C_ERR_NACK) return 0;
    if (r) return r;
    blog(b, "START 7E/W CCC ENTDAA(0x07)");
    int assigned = 0; bool have_nacked = false; uint64_t nacked = 0;
    for (int round = 0; round < 2 * MAX_T + 2; round++) {
        int idx[MAX_T], lost[MAX_T]; uint64_t w[MAX_T]; bool st[MAX_T], in[MAX_T]; int n = 0;
        for (int i = 0; i < b->nt; i++) {
            const Target *t = b->t[i];
            if (!t->online || t->da) continue;
            idx[n] = i; w[n] = t_id64(t); st[n] = t->fault == TF_NO_RELEASE; in[n] = true; n++;
        }
        b->od_clk += 9;                                         /* Sr 7E/R + ACK */
        if (n == 0) { blog(b, "  Sr 7E/R NACK -> STOP (%d assigned)", assigned); return assigned; }
        uint64_t id = od_arbitrate(n, w, st, 64, in, lost);
        b->od_clk += 64;
        int a = get_free_addr(c, 8);
        if (a < 0) { blog(b, "  no free address -> STOP"); return I3C_ERR_NOADDR; }
        uint8_t sent = da_with_parity((uint8_t)a), wire = sent;
        if (b->corrupt_n > 0) { wire ^= b->corrupt_da; b->corrupt_n--; }
        b->od_clk += 9;                                         /* DA + PAR + ACK */
        int acks = 0;
        for (int k = 0; k < n; k++) {
            if (!in[k] || pop8(wire) % 2 == 0) continue;        /* 패리티 오류면 NACK */
            b->t[idx[k]]->da = wire >> 1; acks++;
        }
        if (acks > 1) b->dup_da++;
        if (acks == 0) {
            blog(b, "  Sr 7E/R ACK ID %012llx -> DA 0x%02X NACK", (unsigned long long)(id >> 16), a);
            if (have_nacked && nacked == id) { blog(b, "  same ID NACKed twice -> STOP"); return I3C_ERR_DAA_NACK; }
            have_nacked = true; nacked = id;
            continue;
        }
        if (!dev_add(c, id >> 16, (uint8_t)(id >> 8), (uint8_t)id, (uint8_t)a)) return I3C_ERR_NOADDR;
        c->slot[a] = SLOT_I3C; assigned++;
        blog(b, "  Sr 7E/R ACK ID %012llx BCR %02x DCR %02x -> DA 0x%02X (0x%02X) ACK",
             (unsigned long long)(id >> 16), (unsigned)(uint8_t)(id >> 8), (unsigned)(uint8_t)id, a, sent);
    }
    return I3C_ERR_DAA_NACK;
}

/* ================= Part B ================= */

/* ctrl_arb_header: START + 중재 가능한 주소 헤더 1개 (OD 9).
 *   own_hdr = 컨트롤러가 내는 헤더 (보통 7E/W), HDR_NONE = 타깃이 시작한 START (컨트롤러는 클럭만).
 *   참가자: 온라인 타깃 중 (da && ibi_req && ev_en&SIR) → (da<<1)|1,
 *                         (!da && hj_req && ev_en&HJ) → (0x02<<1)|0.  고장 TF_NO_RELEASE 반영.
 *   - 참가자 0명: HDR_IDLE (own_hdr 가 HDR_NONE 일 때만 가능).
 *   - 컨트롤러 헤더가 이김: HDR_WON (진 타깃의 요청은 그대로 남는다).
 *   - Hot-Join(0x02/W) 이김: n_hj++. hj_enabled 면 ACK, 이긴 타깃들 hj_req=false, hj_daa_pending=true,
 *     STOP. 아니면 NACK + Sr DISEC(bcast, HJ) — ctrl_events_sr.  → HDR_SERVED
 *   - IBI(DA/R) 이김: 표에 있고 ibi_on 이면 ACK, BCR 에 IBI_PAYLOAD 면 MDB 1바이트 (bus_pp_read,
 *     이긴 타깃 전부가 냄), 큐에 {da, mdb, has_mdb} (꽉 차면 q_drop++), 이긴 타깃 ibi_req=false,
 *     n_ibi_ok++, c->n_ibi++, STOP.  아니면 NACK, 이긴 타깃 n_ibi_nack++, c->n_ibi_nack++,
 *     Sr DISEC(direct DA, SIR).  → HDR_SERVED */
static int ctrl_arb_header(Ctrl *c, uint8_t own_hdr) {
    /* TODO(B1): START 헤더 중재 + IBI / Hot-Join 처리 */
    Bus *b = c->b;
    uint64_t w[MAX_T + 1]; bool st[MAX_T + 1], in[MAX_T + 1]; int lost[MAX_T + 1], idx[MAX_T + 1];
    int n = 0;
    if (own_hdr != HDR_NONE) { w[n] = own_hdr; st[n] = false; in[n] = true; idx[n] = -1; n++; }
    for (int i = 0; i < b->nt; i++) {
        const Target *t = b->t[i];
        if (!t->online) continue;
        if (t->da && t->ibi_req && (t->ev_en & EV_SIR)) w[n] = HDR(t->da, 1);
        else if (!t->da && t->hj_req && (t->ev_en & EV_HJ)) w[n] = HDR(I3C_HJ_ADDR, 0);
        else continue;
        st[n] = t->fault == TF_NO_RELEASE; in[n] = true; idx[n] = i; n++;
    }
    if (n == 0) return HDR_IDLE;
    uint8_t hdr = (uint8_t)od_arbitrate(n, w, st, 8, in, lost);
    b->od_clk += 9;                                             /* 8 헤더 비트 + ACK */
    if (own_hdr != HDR_NONE && in[0] && hdr == own_hdr) return HDR_WON;

    uint8_t a = hdr >> 1; bool rnw = hdr & 1;
    const char *who = own_hdr == HDR_NONE ? "target START" : "START (ctrl 7E/W lost)";
    if (a == I3C_HJ_ADDR && !rnw) {
        c->n_hj++;
        if (c->hj_enabled) {
            for (int k = 0; k < n; k++) if (idx[k] >= 0 && in[k]) b->t[idx[k]]->hj_req = false;
            c->hj_daa_pending = true;
            blog(b, "%s 02/W Hot-Join -> ACK STOP (ENTDAA pending)", who);
        } else {
            blog(b, "%s 02/W Hot-Join -> NACK, Sr", who);
            ctrl_events_sr(c, false, 0, EV_HJ);
        }
        return HDR_SERVED;
    }
    Dev *d = rnw ? dev_find(c, a) : NULL;
    if (d && d->ibi_on) {
        IbiEv ev = { a, 0, false };
        if (d->bcr & BCR_IBI_PAYLOAD) {
            uint8_t v[MAX_T]; int nv = 0;
            for (int k = 0; k < n; k++) if (idx[k] >= 0 && in[k]) v[nv++] = b->t[idx[k]]->mdb;
            ev.mdb = bus_pp_read(b, nv, v); ev.has_mdb = true;
        }
        if (c->nq < IBIQ_CAP) c->q[c->nq++] = ev; else c->q_drop++;
        for (int k = 0; k < n; k++)
            if (idx[k] >= 0 && in[k]) { b->t[idx[k]]->ibi_req = false; b->t[idx[k]]->n_ibi_ok++; }
        c->n_ibi++;
        if (ev.has_mdb) blog(b, "%s %02X/R IBI -> ACK MDB 0x%02X STOP", who, a, ev.mdb);
        else            blog(b, "%s %02X/R IBI -> ACK (no MDB) STOP", who, a);
    } else {
        for (int k = 0; k < n; k++) if (idx[k] >= 0 && in[k]) b->t[idx[k]]->n_ibi_nack++;
        c->n_ibi_nack++;
        blog(b, "%s %02X/%c IBI -> NACK, Sr", who, a, rnw ? 'R' : 'W');
        ctrl_events_sr(c, false, a, EV_SIR);
    }
    return HDR_SERVED;
}

/* ctrl_verify: DAA 뒤 확인. 표의 각 장치에 GETPID(6)/GETBCR(1)/GETDCR(1) 을 보내
 *   - NACK → I3C_ERR_NACK, 읽은 값 != DAA 때 받은 값 → I3C_ERR_PID_MISMATCH (첫 오류를 반환, 로그는 전부)
 *   - 오류가 없고 ndev < n_expected (보드 표의 I3C 장치 수) → I3C_ERR_MISSING
 *   - 아니면 I3C_OK */
static int ctrl_verify(Ctrl *c, int n_expected) {
    /* TODO(B2): GETPID/GETBCR/GETDCR 읽어 비교 + 개수 확인 */
    int rc = I3C_OK;
    for (int i = 0; i < c->ndev; i++) {
        const Dev *d = &c->dev[i];
        uint8_t p[6], bcr = 0, dcr = 0;
        int r1 = ctrl_get(c, CCC_GETPID, d->da, p, 6);
        int r2 = r1 < 0 ? r1 : ctrl_get(c, CCC_GETBCR, d->da, &bcr, 1);
        int r3 = r2 < 0 ? r2 : ctrl_get(c, CCC_GETDCR, d->da, &dcr, 1);
        if (r3 < 0) {
            blog(c->b, "verify DA 0x%02X: no answer", d->da);
            if (!rc) rc = r3 == I3C_ERR_NACK ? I3C_ERR_NACK : r3;
            continue;
        }
        uint64_t pid = 0;
        for (int j = 0; j < 6; j++) pid = (pid << 8) | p[j];
        if (pid != d->pid || bcr != d->bcr || dcr != d->dcr) {
            blog(c->b, "verify DA 0x%02X: GETPID %012llx != DAA %012llx", d->da,
                 (unsigned long long)pid, (unsigned long long)d->pid);
            if (!rc) rc = I3C_ERR_PID_MISMATCH;
        } else {
            blog(c->b, "verify DA 0x%02X: PID %012llx BCR %02x DCR %02x OK", d->da,
                 (unsigned long long)pid, bcr, dcr);
        }
    }
    if (rc == I3C_OK && c->ndev < n_expected) {
        blog(c->b, "verify: found %d I3C devices, board says %d", c->ndev, n_expected);
        rc = I3C_ERR_MISSING;
    }
    return rc;
}

/* ctrl_setdasa: 정적 주소 sa 인 타깃에 동적 주소 da 를 직접 준다.
 *   da 가 SLOT_FREE 가 아니면 I3C_ERR_ARG. START 7E/W + CCC 0x87, Sr sa/W (OD 9):
 *   응답자 = 온라인·da==0·정적 주소 sa 인 타깃 + 정적 주소 sa 인 레거시 (2개 이상이면 collisions++).
 *   I3C 응답자 없으면 STOP + I3C_ERR_NACK. 데이터 바이트 da<<1 (PP 9) → 타깃 da 설정, STOP.
 *   그 다음 GETPID/GETBCR/GETDCR 로 PID 를 읽어 표에 추가, slot = I3C. 반환 I3C_OK. */
static int ctrl_setdasa(Ctrl *c, uint8_t sa, uint8_t da) {
    /* TODO(B3): SETDASA + 정보 읽기 */
    if (da >= 128 || c->slot[da] != SLOT_FREE) return I3C_ERR_ARG;
    int r = ctrl_ccc_begin(c, CCC_SETDASA);
    if (r) return r;
    Bus *b = c->b; Target *rs[MAX_T]; int nr = 0, nl = 0;
    for (int i = 0; i < b->nt; i++) {
        Target *t = b->t[i];
        if (t->online && !t->da && t->sa == sa) rs[nr++] = t;
    }
    for (int i = 0; i < b->nl; i++) if (b->l[i]->sa == sa) { b->l[i]->n_acked++; nl++; }
    b->od_clk += 9;
    if (nr + nl > 1) b->collisions++;
    if (nr == 0) { blog(b, "SETDASA Sr %02X/W NACK -> STOP", sa); return I3C_ERR_NACK; }
    b->pp_clk += 9;
    for (int k = 0; k < nr; k++) rs[k]->da = da;
    blog(b, "START 7E/W CCC SETDASA(0x87) Sr %02X/W ACK 0x%02X(=0x%02X<<1) STOP", sa, da << 1, da);
    uint8_t p[6], bcr, dcr;
    if (ctrl_get(c, CCC_GETPID, da, p, 6) < 0 || ctrl_get(c, CCC_GETBCR, da, &bcr, 1) < 0 ||
        ctrl_get(c, CCC_GETDCR, da, &dcr, 1) < 0) return I3C_ERR_NACK;
    uint64_t pid = 0;
    for (int j = 0; j < 6; j++) pid = (pid << 8) | p[j];
    if (!dev_add(c, pid, bcr, dcr, da)) return I3C_ERR_NOADDR;
    c->slot[da] = SLOT_I3C;
    return I3C_OK;
}

/* ================= Part C ================= */
typedef struct { uint64_t pid; uint8_t sa, init_da; bool want_ibi; } BoardI3c;
typedef struct { const BoardI3c *i3c; int ni3c; const uint8_t *i2c_sa; int ni2c; bool hot_join; } Board;

/* i3c_bus_init: Linux i3c_master_bus_init() 흐름의 축약 (스펙·소스 확인 권장).
 *   1) addr_slots_init(레거시 주소)   2) RSTDAA   3) DISEC bcast (SIR|HJ)
 *   4) 보드 표에서 sa && init_da 인 장치마다 SETDASA (실패해도 계속 — 그 장치는 ENTDAA 로 간다)
 *   5) ENTDAA (음수면 그대로 반환)    6) ctrl_verify(ni3c) — 오류면 반환
 *   7) 보드 표에서 want_ibi 인 장치: PID 로 표를 찾아 ibi_on = true, ENEC direct SIR
 *   8) hot_join 이면 hj_enabled = true, ENEC bcast HJ.   반환 I3C_OK */
static int i3c_bus_init(Ctrl *c, const Board *bd) {
    /* TODO(C1): 버스 초기화 순서 */
    addr_slots_init(c, bd->i2c_sa, bd->ni2c);
    int r = ctrl_rstdaa(c);
    if (r) return r;
    if ((r = ctrl_events(c, false, 0, EV_SIR | EV_HJ))) return r;
    for (int i = 0; i < bd->ni3c; i++)
        if (bd->i3c[i].sa && bd->i3c[i].init_da) (void)ctrl_setdasa(c, bd->i3c[i].sa, bd->i3c[i].init_da);
    r = ctrl_entdaa(c);
    if (r < 0) return r;
    if ((r = ctrl_verify(c, bd->ni3c))) return r;
    for (int i = 0; i < bd->ni3c; i++) {
        if (!bd->i3c[i].want_ibi) continue;
        for (int k = 0; k < c->ndev; k++) {
            if (c->dev[k].pid != bd->i3c[i].pid) continue;
            c->dev[k].ibi_on = true;
            if ((r = ctrl_events(c, true, c->dev[k].da, EV_SIR))) return r;
        }
    }
    if (bd->hot_join) {
        c->hj_enabled = true;
        if ((r = ctrl_events(c, true, 0, EV_HJ))) return r;
    }
    return I3C_OK;
}

/* ctrl_service: 펌웨어 메인 루프의 한 조각. Hot-Join 뒤 ENTDAA, 그 다음 타깃 요청이 없을 때까지
 * 타깃 START 헤더 처리. 반환: 처리한 요청 수. (주어짐) */
static int ctrl_service(Ctrl *c, int max_iter) {
    int k = 0;
    for (; k < max_iter; k++) {
        if (c->hj_daa_pending) { c->hj_daa_pending = false; (void)ctrl_entdaa(c); continue; }
        if (ctrl_arb_header(c, HDR_NONE) == HDR_IDLE) break;
    }
    return k;
}

/* ---------------- 테스트 하네스 (주어짐) ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0xC0FFEE11u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static uint64_t rnd64(void) { return ((uint64_t)xs32() << 32) | xs32(); }

static Target g_t[MAX_T];
static Legacy g_l[MAX_L];
static Bus    g_b;
static Ctrl   g_c;

static void t_set(Target *t, const char *name, uint64_t pid, uint8_t bcr, uint8_t dcr, uint8_t sa) {
    memset(t, 0, sizeof *t);
    t->name = name; t->pid = pid & 0xFFFFFFFFFFFFull; t->bcr = bcr; t->dcr = dcr; t->sa = sa;
    t->online = true; t->ev_en = EV_SIR | EV_HJ;
}
static void bus_reset(int nt, int nl, bool log) {
    memset(&g_b, 0, sizeof g_b); memset(&g_c, 0, sizeof g_c);
    for (int i = 0; i < nt; i++) g_b.t[i] = &g_t[i];
    for (int i = 0; i < nl; i++) g_b.l[i] = &g_l[i];
    g_b.nt = nt; g_b.nl = nl; g_b.log_on = log; g_c.b = &g_b;
}
static void log_print(const Bus *b, int from) {
    for (int i = from; i < b->nlog; i++) printf("  %s\n", b->log[i]);
}
/* 할당 불변식: 온라인 타깃 DA 가 서로 다르고, 예약·레거시 칸이 아니며, 표와 일치 */
static bool da_invariants(const Ctrl *c) {
    const Bus *b = c->b; bool used[128] = { false };
    for (int i = 0; i < b->nt; i++) {
        const Target *t = b->t[i];
        if (!t->online || !t->da) continue;
        if (t->da >= 128 || used[t->da]) return false;
        used[t->da] = true;
        if (c->slot[t->da] != SLOT_I3C) return false;
        const Dev *d = NULL;
        for (int k = 0; k < c->ndev; k++) if (c->dev[k].da == t->da) d = &c->dev[k];
        if (!d || d->pid != t->pid) return false;
    }
    for (int a = 0; a < 128; a++) {
        if (a < 8 || a == I3C_BCAST || pop8((uint8_t)(a ^ I3C_BCAST)) == 1) { if (used[a]) return false; }
        for (int i = 0; i < b->nl; i++) if (b->l[i]->sa == a && used[a]) return false;
    }
    return true;
}

static int ix_by_da(uint8_t da) {
    for (int i = 0; i < g_b.nt; i++) if (g_t[i].da == da) return i;
    return -1;
}

int main(void) {
    /* ================= Part A ================= */
    { /* A1. 8비트 헤더 쌍 전수: 이긴 값 = min, 진 쪽 lost_at = 처음 다른 비트 */
        int bad = 0;
        for (unsigned x = 0; x < 256; x++)
            for (unsigned y = 0; y < 256; y++) {
                uint64_t w[2] = { x, y }; bool in[2] = { true, true }; int lost[2];
                uint64_t bus = od_arbitrate(2, w, NULL, 8, in, lost);
                if (bus != (x < y ? x : y)) { bad++; continue; }
                if (x == y) { if (!in[0] || !in[1] || lost[0] != -1 || lost[1] != -1) bad++; continue; }
                int first = 0; while (!(((x ^ y) << first) & 0x80)) first++;
                int L = x > y ? 0 : 1, W = 1 - L;
                if (in[L] || !in[W] || lost[L] != first || lost[W] != -1) bad++;
            }
        printf("A1 exhaustive 8-bit pairs: bad=%d\n", bad);
        CHECK(bad == 0);
    }
    { /* A1b. 64비트 다자 중재 (랜덤, 공통 접두사 많이) + 비참가자 무시 */
        int bad = 0;
        for (int it = 0; it < 4000; it++) {
            int n = 2 + (int)(xs32() % 7); uint64_t w[8], base = rnd64(), mn = UINT64_MAX;
            bool in[8]; int lost[8];
            for (int k = 0; k < n; k++) {
                int keep = (int)(xs32() % 64);
                uint64_t m = keep ? (~0ull << (64 - keep)) : 0;
                w[k] = (base & m) | (rnd64() & ~m);
                if (xs32() % 8 == 0 && k) w[k] = w[k - 1];
                in[k] = xs32() % 6 != 0;
                if (in[k] && w[k] < mn) mn = w[k];
            }
            bool any = false; for (int k = 0; k < n; k++) any |= in[k];
            if (!any) continue;
            bool was[8]; memcpy(was, in, sizeof was);
            uint64_t bus = od_arbitrate(n, w, NULL, 64, in, lost);
            if (bus != mn) bad++;
            for (int k = 0; k < n; k++) if (in[k] != (was[k] && w[k] == mn)) bad++;
        }
        printf("A1b random 64-bit multi-party: bad=%d\n", bad);
        CHECK(bad == 0);
    }
    { /* A1c. 고장: 진 뒤에도 놓지 않는 참가자 → 버스 = AND, 정상 승자도 빠질 수 있다 */
        uint64_t w[2] = { 0x5A, 0xA5 }; bool st[2] = { false, true }, in[2] = { true, true }; int lost[2];
        uint64_t bus = od_arbitrate(2, w, st, 8, in, lost);
        CHECK(bus == 0x25);                          /* 0x5A 도 0xA5 도, AND(0x00) 도 아님 */
        CHECK(!in[0] && lost[0] == 1 && in[1]);      /* 정상(0x5A)은 bit6 에서 1 을 냈는데 0 */
    }
    { /* A2/A3. 주소 표 */
        bus_reset(0, 0, false);
        const uint8_t leg[] = { 0x0B, 0x50, 0x7C, 0x03 };     /* 0x7C, 0x03 은 원래 예약 */
        int nfree = addr_slots_init(&g_c, leg, 4);
        int nrsvd = 0; for (int a = 0; a < 128; a++) nrsvd += g_c.slot[a] == SLOT_RSVD;
        printf("A2 slots: reserved=%d free=%d (0x3E 0x5E 0x6E 0x76 0x7A 0x7C 0x7F 0x7E + 0x00-0x07)\n", nrsvd, nfree);
        CHECK(nrsvd == 16 && nfree == 110);
        CHECK(g_c.slot[0x3E] == SLOT_RSVD && g_c.slot[0x5E] == SLOT_RSVD && g_c.slot[0x6E] == SLOT_RSVD &&
              g_c.slot[0x76] == SLOT_RSVD && g_c.slot[0x7A] == SLOT_RSVD && g_c.slot[0x7F] == SLOT_RSVD);
        CHECK(g_c.slot[0x0B] == SLOT_I2C && g_c.slot[0x50] == SLOT_I2C && g_c.slot[0x7C] == SLOT_RSVD);
        CHECK(get_free_addr(&g_c, 0) == 0x08 && get_free_addr(&g_c, 0x0B) == 0x0C);
        CHECK(get_free_addr(&g_c, 0x3E) == 0x3F && get_free_addr(&g_c, 0x7C) == 0x7D);
        CHECK(get_free_addr(&g_c, 0x7E) == -1 && get_free_addr(&g_c, 0x50) == 0x51);
        g_c.naive_alloc = true;
        CHECK(get_free_addr(&g_c, 0x0B) == 0x0B);
    }
    { /* A4. DA + 패리티 전수 */
        int bad = 0;
        for (int a = 0; a < 128; a++) {
            uint8_t v = da_with_parity((uint8_t)a);
            if ((v >> 1) != a || pop8(v) % 2 != 1) bad++;
        }
        printf("A4 DA parity: 0x08->0x%02X 0x09->0x%02X 0x0A->0x%02X 0x0B->0x%02X, bad=%d\n",
               da_with_parity(0x08), da_with_parity(0x09), da_with_parity(0x0A), da_with_parity(0x0B), bad);
        CHECK(bad == 0 && da_with_parity(0x08) == 0x10 && da_with_parity(0x09) == 0x13);
    }
    { /* A5. ENTDAA 기본: 타깃 5 + 레거시 2 (0x0B 스마트배터리, 0x50 EEPROM) */
        bus_reset(5, 2, true);
        t_set(&g_t[0], "imu",   0x04A2100A5000ull, 0x06, 0x00, 0);     /* 같은 제조사, 인스턴스만 다름 */
        t_set(&g_t[1], "imu2",  0x04A2100A1000ull, 0x06, 0x00, 0);
        t_set(&g_t[2], "baro",  0x0BEE30040000ull, 0x02, 0x00, 0);
        t_set(&g_t[3], "mag",   0x02083F120000ull, 0x26, 0x00, 0);
        t_set(&g_t[4], "temp",  0x02083F120000ull, 0x06, 0x00, 0);     /* PID 같고 BCR 다름 */
        g_l[0] = (Legacy){ "batt", 0x0B, 0x5B, 0 }; g_l[1] = (Legacy){ "eeprom", 0x50, 0xEE, 0 };
        const uint8_t leg[] = { 0x0B, 0x50 };
        addr_slots_init(&g_c, leg, 2);
        int r = ctrl_entdaa(&g_c);
        printf("A5 ENTDAA log (5 targets, legacy 0x0B/0x50):\n"); log_print(&g_b, 0);
        CHECK(r == 5 && g_c.ndev == 5);
        CHECK(g_t[4].da == 0x08 && g_t[3].da == 0x09 && g_t[1].da == 0x0A && g_t[0].da == 0x0C && g_t[2].da == 0x0D);
        CHECK(g_b.od_clk == 9 + 5 * 82 + 9 && g_b.pp_clk == 9);
        CHECK(da_invariants(&g_c) && g_b.dup_da == 0);
        uint8_t v = 0; int n0 = g_l[0].n_acked;
        CHECK(ctrl_priv_read(&g_c, 0x0B, &v) == I3C_OK && v == 0x5B && g_l[0].n_acked == n0 + 1);
        CHECK(ctrl_priv_read(&g_c, 0x0C, &v) == I3C_OK && g_b.collisions == 0);
        CHECK(g_l[0].n_acked == 1 && g_l[1].n_acked == 0);           /* 레거시는 자기 주소에서만 */
        CHECK(ctrl_entdaa(&g_c) == 0);                                /* 다시 돌리면 아무도 ACK 안 함 */
    }
    { /* A6. 랜덤: 할당 유일, 예약·레거시 회피, 주소 순서 == 64비트 ID 순서 */
        int bad = 0, total = 0;
        for (int it = 0; it < 1500; it++) {
            int nt = 1 + (int)(xs32() % 20), nl = (int)(xs32() % 5);
            bus_reset(nt, nl, false);
            uint8_t leg[MAX_L];
            for (int i = 0; i < nl; i++) {
                bool dup;
                do { leg[i] = (uint8_t)(0x08 + xs32() % 0x70); dup = false; for (int k = 0; k < i; k++) dup |= leg[k] == leg[i]; } while (dup);
                g_l[i] = (Legacy){ "l", leg[i], (uint8_t)i, 0 };
            }
            uint64_t manuf[3] = { 0x04A2, 0x0208, 0x0BEE };
            for (int i = 0; i < nt; i++) {
                uint64_t pid;
                bool dup;
                do {
                    pid = (manuf[xs32() % 3] << 33) | ((uint64_t)(xs32() & 1) << 32) | (xs32() & 0xFFFF000Fu);
                    dup = false;
                    for (int k = 0; k < i; k++) dup |= g_t[k].pid == pid;
                } while (dup);
                t_set(&g_t[i], "t", pid, (uint8_t)(xs32() & 0x26), (uint8_t)(xs32() % 3), 0);
            }
            addr_slots_init(&g_c, leg, nl);
            int r = ctrl_entdaa(&g_c);
            total += r > 0 ? r : 0;
            if (r != nt || !da_invariants(&g_c)) { bad++; continue; }
            for (int i = 0; i < nt; i++) for (int k = 0; k < nt; k++)
                if (t_id64(&g_t[i]) < t_id64(&g_t[k]) && g_t[i].da > g_t[k].da) bad++;
            for (int i = 0; i < nt; i++) { uint8_t v; if (ctrl_priv_read(&g_c, g_t[i].da, &v) || v != g_t[i].status) bad++; }
            for (int i = 0; i < nl; i++) { uint8_t v; if (ctrl_priv_read(&g_c, leg[i], &v) || v != g_l[i].val) bad++; }
            if (g_b.collisions || g_b.contention) bad++;
        }
        printf("A6 random DAA: 1500 buses, %d addresses assigned, bad=%d\n", total, bad);
        CHECK(bad == 0);
    }

    /* ================= Part B ================= */
    { /* B1. 동시 IBI 4개 → DA 오름차순으로 처리, MDB 기록 */
        bus_reset(4, 0, true);
        for (int i = 0; i < 4; i++) t_set(&g_t[i], "s", 0x01000000000ull * (uint64_t)(4 - i), 0x06, 0, 0);
        addr_slots_init(&g_c, NULL, 0);
        CHECK(ctrl_entdaa(&g_c) == 4);
        for (int k = 0; k < g_c.ndev; k++) g_c.dev[k].ibi_on = true;
        for (int i = 0; i < 4; i++) { g_t[i].ibi_req = true; g_t[i].mdb = (uint8_t)(0xA0 + i); }
        int from = g_b.nlog;
        int n = ctrl_service(&g_c, 20);
        printf("B1 four simultaneous IBIs:\n"); log_print(&g_b, from);
        CHECK(n == 4 && g_c.nq == 4 && g_c.n_ibi == 4);
        bool ok = true;
        for (int k = 0; k < 4; k++) {
            ok &= g_c.q[k].da == 0x08 + k && g_c.q[k].has_mdb;
            int ti = ix_by_da(g_c.q[k].da); ok &= ti >= 0 && g_c.q[k].mdb == g_t[ti].mdb && g_t[ti].n_ibi_ok == 1;
        }
        CHECK(ok);
    }
    { /* B2. 컨트롤러가 CCC 를 시작하는 START 에 IBI 가 겹침 → IBI 먼저, 그 다음 CCC */
        bus_reset(2, 0, true);
        t_set(&g_t[0], "a", 0x111111111111ull, 0x02, 0, 0);    /* BCR: IBI 가능, payload 없음 */
        t_set(&g_t[1], "b", 0x222222222222ull, 0x06, 0, 0);
        addr_slots_init(&g_c, NULL, 0);
        CHECK(ctrl_entdaa(&g_c) == 2);
        g_c.dev[0].ibi_on = g_c.dev[1].ibi_on = true;
        g_t[0].ibi_req = true;
        int from = g_b.nlog; uint8_t p[6] = { 0 };
        int r = ctrl_get(&g_c, CCC_GETPID, g_t[1].da, p, 6);
        printf("B2 IBI wins over controller's 7E/W:\n"); log_print(&g_b, from);
        CHECK(r == 1 && g_c.n_preempt == 1 && g_c.n_ibi == 1 && !g_c.q[0].has_mdb && g_c.q[0].da == g_t[0].da);
        CHECK(p[0] == 0x22 && p[5] == 0x22);
    }
    { /* B3. 컨트롤러가 IBI 를 원하지 않는 장치 → NACK + Sr DISEC → 더 이상 요청 안 함 */
        bus_reset(1, 0, true);
        t_set(&g_t[0], "noisy", 0x0ABCDEF01234ull, 0x06, 0, 0);
        addr_slots_init(&g_c, NULL, 0);
        CHECK(ctrl_entdaa(&g_c) == 1);
        g_t[0].ibi_req = true;
        int from = g_b.nlog;
        int n = ctrl_service(&g_c, 10);
        printf("B3 unwanted IBI:\n"); log_print(&g_b, from);
        CHECK(n == 1 && g_c.n_ibi_nack == 1 && g_t[0].n_ibi_nack == 1 && !(g_t[0].ev_en & EV_SIR) && g_t[0].ibi_req);
        CHECK(ctrl_service(&g_c, 10) == 0);
    }
    { /* B4. Hot-Join: 늦게 붙은 타깃 + 동시에 IBI → HJ(0x02) 먼저, 그 다음 ENTDAA, 그 다음 IBI */
        bus_reset(3, 1, true);
        t_set(&g_t[0], "a", 0x0208000A0000ull, 0x06, 0, 0);
        t_set(&g_t[1], "b", 0x0208000B0000ull, 0x06, 0, 0);
        t_set(&g_t[2], "late", 0x000100000000ull, 0x06, 0, 0);   /* 가장 작은 PID 지만 늦게 옴 */
        g_t[2].online = false;
        g_l[0] = (Legacy){ "eeprom", 0x0A, 0xEE, 0 };
        const uint8_t leg[] = { 0x0A };
        addr_slots_init(&g_c, leg, 1);
        CHECK(ctrl_entdaa(&g_c) == 2 && g_t[0].da == 0x08 && g_t[1].da == 0x09);
        g_c.dev[0].ibi_on = g_c.dev[1].ibi_on = true; g_c.hj_enabled = true;
        g_t[2].online = true; g_t[2].hj_req = true;
        g_t[1].ibi_req = true; g_t[1].mdb = 0x42;
        int from = g_b.nlog;
        int n = ctrl_service(&g_c, 10);
        printf("B4 Hot-Join + IBI at the same time:\n"); log_print(&g_b, from);
        CHECK(n == 2 && g_c.n_hj == 1 && g_c.n_preempt == 1 && g_t[2].da == 0x0B && g_c.n_ibi == 1 && g_c.q[0].da == 0x09);
        CHECK(da_invariants(&g_c) && g_c.ndev == 3);
        /* HJ 를 허용하지 않는 컨트롤러 */
        bus_reset(2, 0, true);
        t_set(&g_t[0], "a", 0x0208000A0000ull, 0x06, 0, 0);
        t_set(&g_t[1], "late", 0x000100000000ull, 0x06, 0, 0); g_t[1].online = false;
        addr_slots_init(&g_c, NULL, 0);
        CHECK(ctrl_entdaa(&g_c) == 1);
        g_t[1].online = true; g_t[1].hj_req = true;
        n = ctrl_service(&g_c, 10);
        CHECK(n == 1 && g_c.n_hj == 1 && g_t[1].da == 0 && !(g_t[1].ev_en & EV_HJ) && !(g_t[0].ev_en & EV_HJ));
        CHECK(ctrl_service(&g_c, 10) == 0);
    }
    { /* B5. SETDASA: 정적 주소 0x30 → DA 0x20, ENTDAA 는 나머지만, 0x20 을 피한다 */
        bus_reset(3, 0, true);
        t_set(&g_t[0], "imu", 0x7FFF00000000ull, 0x06, 0x00, 0x30);
        t_set(&g_t[1], "x",   0x000200000000ull, 0x06, 0x00, 0);
        t_set(&g_t[2], "y",   0x000300000000ull, 0x06, 0x00, 0);
        addr_slots_init(&g_c, NULL, 0);
        int from = g_b.nlog;
        CHECK(ctrl_setdasa(&g_c, 0x30, 0x20) == I3C_OK && g_t[0].da == 0x20 && g_c.ndev == 1 && g_c.dev[0].pid == g_t[0].pid);
        CHECK(ctrl_setdasa(&g_c, 0x31, 0x21) == I3C_ERR_NACK && ctrl_setdasa(&g_c, 0x30, 0x7E) == I3C_ERR_ARG);
        CHECK(ctrl_setdasa(&g_c, 0x30, 0x20) == I3C_ERR_ARG);
        CHECK(ctrl_entdaa(&g_c) == 2 && g_t[1].da == 0x08 && g_t[2].da == 0x09 && da_invariants(&g_c));
        printf("B5 SETDASA then ENTDAA:\n"); log_print(&g_b, from);
    }
    { /* B6. 같은 64비트 ID 두 개 → 둘 다 "이김", 같은 DA. GETPID 로는 못 잡고, 개수로 잡는다 */
        bus_reset(3, 0, true);
        t_set(&g_t[0], "twinA", 0x04A2100A0000ull, 0x06, 0x00, 0); g_t[0].status = 0x11;
        t_set(&g_t[1], "twinB", 0x04A2100A0000ull, 0x06, 0x00, 0); g_t[1].status = 0x22;
        t_set(&g_t[2], "c",     0x0BEE00000000ull, 0x06, 0x00, 0);
        addr_slots_init(&g_c, NULL, 0);
        int r = ctrl_entdaa(&g_c);
        CHECK(r == 2 && g_c.ndev == 2 && g_t[0].da == g_t[1].da && g_t[0].da == 0x08 && g_b.dup_da == 1);
        int from = g_b.nlog;
        int v = ctrl_verify(&g_c, 3);
        printf("B6 duplicate PID: verify=%d, log:\n", v); log_print(&g_b, from);
        CHECK(v == I3C_ERR_MISSING && g_b.contention == 0);      /* GETPID 는 둘이 같은 값을 냄 */
        uint8_t s = 0;
        CHECK(ctrl_priv_read(&g_c, 0x08, &s) == I3C_OK && s == (0x11 & 0x22) && g_b.collisions > 0 && g_b.contention > 0);
    }
    { /* B7. DA 바이트 1비트 오류 → 패리티 NACK → 같은 타깃이 다시 이겨 성공. 계속 오류면 DAA_NACK */
        bus_reset(2, 0, true);
        t_set(&g_t[0], "a", 0x000A00000000ull, 0x06, 0, 0);
        t_set(&g_t[1], "b", 0x000B00000000ull, 0x06, 0, 0);
        addr_slots_init(&g_c, NULL, 0);
        g_b.corrupt_da = 0x04; g_b.corrupt_n = 1;
        int from = g_b.nlog;
        int r = ctrl_entdaa(&g_c);
        printf("B7 DA byte bit error:\n"); log_print(&g_b, from);
        CHECK(r == 2 && g_t[0].da == 0x08 && g_t[1].da == 0x09 && g_b.od_clk == 9 + 3 * 82 + 9);
        bus_reset(2, 0, false);
        addr_slots_init(&g_c, NULL, 0); g_t[0].da = g_t[1].da = 0;
        g_b.corrupt_da = 0x80; g_b.corrupt_n = 2;
        CHECK(ctrl_entdaa(&g_c) == I3C_ERR_DAA_NACK && g_t[0].da == 0 && g_c.ndev == 0);
        bus_reset(1, 0, false);                                     /* 2비트 오류: 패리티 통과 → 엉뚱한 DA */
        addr_slots_init(&g_c, NULL, 0); g_t[0].da = 0;
        g_b.corrupt_da = 0x06; g_b.corrupt_n = 1;
        CHECK(ctrl_entdaa(&g_c) == 1 && g_t[0].da != g_c.dev[0].da);
        CHECK(ctrl_verify(&g_c, 1) == I3C_ERR_NACK);
    }
    { /* B8. 진 뒤에도 놓지 않는 고장 타깃 → 유령 ID 가 할당됨 → GETPID 대조로 검출 */
        bus_reset(2, 0, true);
        t_set(&g_t[0], "good", 0x000500000000ull, 0x06, 0x00, 0);
        t_set(&g_t[1], "stuck", 0x000A00000000ull, 0x06, 0x00, 0); g_t[1].fault = TF_NO_RELEASE;
        addr_slots_init(&g_c, NULL, 0);
        int r = ctrl_entdaa(&g_c);
        int from = g_b.nlog;
        int v = ctrl_verify(&g_c, 2);
        printf("B8 target that never releases: entdaa=%d verify=%d\n", r, v); log_print(&g_b, from);
        CHECK(r == 2 && g_c.dev[0].pid == 0x000200000000ull && g_t[1].da == 0x08);   /* 유령 ID */
        CHECK(g_t[0].da == 0x09 && v == I3C_ERR_PID_MISMATCH);
    }
    { /* B9. 레거시 표를 무시한 할당기 → 0x0B 충돌, 올바른 할당기 → 충돌 0 */
        for (int naive = 1; naive >= 0; naive--) {
            bus_reset(5, 1, false);
            for (int i = 0; i < 5; i++) t_set(&g_t[i], "t", 0x000100000000ull * (uint64_t)(i + 1), 0x06, 0, 0);
            g_l[0] = (Legacy){ "batt", 0x0B, 0x5B, 0 };
            const uint8_t leg[] = { 0x0B };
            addr_slots_init(&g_c, leg, 1); g_c.naive_alloc = naive;
            CHECK(ctrl_entdaa(&g_c) == 5);
            uint8_t v = 0;
            int rr = ctrl_priv_read(&g_c, 0x0B, &v);
            printf("B9 %s allocator: battery read 0x%02X, collisions=%d\n", naive ? "naive" : "correct", v, g_b.collisions);
            if (naive) CHECK(rr == I3C_OK && g_b.collisions == 1 && g_t[3].da == 0x0B && v != 0x5B);
            else       CHECK(rr == I3C_OK && g_b.collisions == 0 && v == 0x5B && g_t[3].da == 0x0C);
        }
    }
    { /* B10. 주소 고갈: 레거시 2 → 빈 칸 110, 타깃 115 → 110 할당 후 NOADDR */
        bus_reset(115, 2, false);
        for (int i = 0; i < 115; i++) t_set(&g_t[i], "t", 0x000100000000ull + (uint64_t)i * 0x10000u, 0x06, 0, 0);
        g_l[0] = (Legacy){ "a", 0x0B, 0, 0 }; g_l[1] = (Legacy){ "b", 0x50, 0, 0 };
        const uint8_t leg[] = { 0x0B, 0x50 };
        CHECK(addr_slots_init(&g_c, leg, 2) == 110);
        int r = ctrl_entdaa(&g_c), left = 0;
        for (int i = 0; i < 115; i++) left += g_t[i].da == 0;
        printf("B10 exhaustion: entdaa=%d ndev=%d unaddressed=%d\n", r, g_c.ndev, left);
        CHECK(r == I3C_ERR_NOADDR && g_c.ndev == 110 && left == 5 && da_invariants(&g_c));
    }

    /* ================= Part C ================= */
    { /* C1. 보드 초기화 + 운용 + Hot-Join, 브링업 로그 */
        bus_reset(4, 2, true);
        t_set(&g_t[0], "imu",  0x04A2100A5000ull, 0x07, 0xC4, 0x68);   /* SETDASA 대상 */
        t_set(&g_t[1], "mag",  0x02083F120000ull, 0x06, 0x00, 0);
        t_set(&g_t[2], "baro", 0x0BEE30040000ull, 0x02, 0x00, 0);
        t_set(&g_t[3], "tof",  0x02083F230000ull, 0x06, 0x00, 0); g_t[3].online = false;  /* 늦게 전원 */
        g_l[0] = (Legacy){ "batt", 0x0B, 0x5B, 0 }; g_l[1] = (Legacy){ "eeprom", 0x50, 0xEE, 0 };
        static const BoardI3c bi[] = { { 0x04A2100A5000ull, 0x68, 0x08, true },
                                       { 0x02083F120000ull, 0, 0, true },
                                       { 0x0BEE30040000ull, 0, 0, false } };
        static const uint8_t bl[] = { 0x0B, 0x50 };
        Board bd = { bi, 3, bl, 2, true };
        int r = i3c_bus_init(&g_c, &bd);
        printf("C1 bring-up log (i3c_bus_init, init=%d):\n", r); log_print(&g_b, 0);
        CHECK(r == I3C_OK && g_c.ndev == 3 && g_t[0].da == 0x08 && g_t[1].da == 0x09 && g_t[2].da == 0x0A);
        CHECK(g_c.dev[0].ibi_on && (g_t[0].ev_en & EV_SIR) && (g_t[1].ev_en & EV_SIR) && !(g_t[2].ev_en & EV_SIR));
        CHECK(g_c.hj_enabled && (g_t[2].ev_en & EV_HJ));
        int from = g_b.nlog;
        g_t[3].online = true; g_t[3].hj_req = true;                  /* tof 전원 인가 */
        g_t[0].ibi_req = true; g_t[0].mdb = 0x01;                   /* imu data ready */
        g_t[1].ibi_req = true; g_t[1].mdb = 0x02;
        g_t[2].ibi_req = true;                                      /* baro: SIR 꺼짐 → 요청 못 함 */
        int n = ctrl_service(&g_c, 20);
        uint8_t v = 0;
        CHECK(ctrl_priv_read(&g_c, 0x50, &v) == I3C_OK && v == 0xEE);
        CHECK(ctrl_priv_read(&g_c, 0x0B, &v) == I3C_OK && v == 0x5B);
        printf("C1 run: hot-join + IBIs + legacy reads (%d requests):\n", n); log_print(&g_b, from);
        CHECK(n == 2 && g_c.n_hj == 1 && g_c.n_preempt == 2 && g_t[3].da == 0x0C && g_c.n_ibi == 2 && g_c.q[0].da == 0x08 && g_c.q[1].da == 0x09);
        CHECK(g_t[2].ibi_req && g_t[2].n_ibi_ok == 0 && g_b.collisions == 0 && g_b.contention == 0 && da_invariants(&g_c));
        CHECK(g_l[0].n_acked == 1 && g_l[1].n_acked == 1);
        printf("C1 clocks: OD %u, PP %u\n", (unsigned)g_b.od_clk, (unsigned)g_b.pp_clk);
    }
    { /* C2. 소크: 랜덤 보드 × 랜덤 이벤트 (IBI, 레거시/I3C 읽기, 늦은 합류) */
        int bad = 0, ibis = 0, hjs = 0, reads = 0;
        for (int seed = 0; seed < 40; seed++) {
            int nt = 3 + (int)(xs32() % 12), nl = (int)(xs32() % 4);
            bus_reset(nt, nl, false);
            uint8_t leg[MAX_L]; BoardI3c bi[MAX_T]; int nb = 0;
            for (int i = 0; i < nl; i++) {
                bool dup;
                do { leg[i] = (uint8_t)(0x08 + xs32() % 0x70); dup = false; for (int k = 0; k < i; k++) dup |= leg[k] == leg[i]; } while (dup);
                g_l[i] = (Legacy){ "l", leg[i], (uint8_t)(0x80 + i), 0 };
            }
            for (int i = 0; i < nt; i++) {
                uint64_t pid = 0x000100000000ull | ((uint64_t)xs32() << 4);
                t_set(&g_t[i], "t", pid + (uint64_t)i, (uint8_t)(0x02 | (xs32() & 4)), 0, 0);
                g_t[i].status = (uint8_t)xs32();
                if (xs32() % 4 == 0) g_t[i].online = false;          /* 나중에 Hot-Join */
                else bi[nb++] = (BoardI3c){ g_t[i].pid, 0, 0, xs32() % 3 != 0 };
            }
            Board bd = { bi, nb, leg, nl, true };
            if (i3c_bus_init(&g_c, &bd) != I3C_OK) { bad++; continue; }
            int raised = 0, legacy_reads = 0;
            for (int step = 0; step < 400; step++) {
                int i = (int)(xs32() % (uint32_t)nt); Target *t = &g_t[i];
                switch (xs32() % 4) {
                case 0: if (t->online && t->da && (t->ev_en & EV_SIR) && !t->ibi_req) { t->ibi_req = true; t->mdb = (uint8_t)step; raised++; } break;
                case 1: if (!t->online) { t->online = true; t->hj_req = true; t->ev_en = EV_SIR | EV_HJ; hjs++; } break;
                case 2: if (nl) { int k = (int)(xs32() % (uint32_t)nl); uint8_t v; legacy_reads++;
                                  if (ctrl_priv_read(&g_c, leg[k], &v) || v != g_l[k].val) bad++; } break;
                default: if (t->online && t->da) { uint8_t v; reads++; if (ctrl_priv_read(&g_c, t->da, &v) || v != t->status) bad++; } break;
                }
                if (xs32() % 3 == 0) { ctrl_service(&g_c, 8); g_c.nq = 0; }   /* 태스크가 IBI 큐를 비움 */
                if (!da_invariants(&g_c)) { bad++; break; }
            }
            ctrl_service(&g_c, 1000); g_c.nq = 0;
            int ok = 0, nack = 0, pend = 0, ack_legacy = 0;
            for (int i = 0; i < nt; i++) {
                ok += g_t[i].n_ibi_ok; nack += g_t[i].n_ibi_nack;
                pend += g_t[i].ibi_req && (g_t[i].ev_en & EV_SIR) && g_t[i].da;
                if (g_t[i].online && !g_t[i].da) bad++;
            }
            for (int k = 0; k < nl; k++) ack_legacy += g_l[k].n_acked;
            if (ok + nack + pend != raised || pend || ok != g_c.n_ibi) bad++;
            if (g_b.collisions || g_b.contention || ack_legacy != legacy_reads || g_c.q_drop) bad++;
            ibis += ok;
        }
        printf("C2 soak: 40 boards x 400 steps, %d IBIs served, %d hot-joins, %d I3C reads, bad=%d\n", ibis, hjs, reads, bad);
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
