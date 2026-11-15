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
 * practice.c = solution.c 의 모델 전체 + 9장 추가 연습 (PID 필드, SETNEWDA 맞바꾸기, ENTDAA 클럭 수, 주소 용량)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
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
GIVEN static int ctrl_priv_read(Ctrl *c, uint8_t addr, uint8_t *out) {
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
GIVEN static int i3c_bus_init(Ctrl *c, const Board *bd) {
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
GIVEN static uint64_t rnd64(void) { return ((uint64_t)xs32() << 32) | xs32(); }

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

GIVEN static int ix_by_da(uint8_t da) {
    for (int i = 0; i < g_b.nt; i++) if (g_t[i].da == da) return i;
    return -1;
}

/* ================= 추가 연습 (9장) ================= */

/* 연습 1 — PID 필드 (Linux include/linux/i3c/device.h 의 I3C_PID_* 매크로와 같은 비트 배치) */
typedef struct { uint16_t manuf; bool rnd; uint32_t rndval; uint16_t part; uint8_t inst; uint16_t extra; } PidF;
static PidF pid_decode(uint64_t pid) {
    PidF f;
    f.manuf  = (uint16_t)((pid >> 33) & 0x7FFF);     /* [47:33] MIPI 제조사 ID */
    f.rnd    = (pid >> 32) & 1;                      /* [32] 1 = 하위 32비트가 랜덤 값 */
    f.rndval = (uint32_t)pid;                        /* [31:0] (rnd 일 때) */
    f.part   = (uint16_t)(pid >> 16);                /* [31:16] part ID      (rnd 아닐 때) */
    f.inst   = (uint8_t)((pid >> 12) & 0xF);         /* [15:12] instance ID */
    f.extra  = (uint16_t)(pid & 0xFFF);              /* [11:0]  추가 정보 */
    return f;
}
static uint64_t pid_make_fixed(uint16_t manuf, uint16_t part, uint8_t inst, uint16_t extra) {
    return ((uint64_t)(manuf & 0x7FFF) << 33) | ((uint64_t)part << 16) | ((uint64_t)(inst & 0xF) << 12) | (extra & 0xFFFu);
}

/* 연습 2 — SETNEWDA (direct CCC 0x88) 와 DA 맞바꾸기 */
static int ctrl_setnewda(Ctrl *c, uint8_t old, uint8_t nw) {
    if (nw >= 128 || c->slot[nw] != SLOT_FREE) return I3C_ERR_ARG;
    Dev *d = dev_find(c, old);
    if (!d) return I3C_ERR_ARG;
    int r = ctrl_ccc_begin(c, CCC_SETNEWDA);
    if (r) return r;
    Bus *b = c->b; Target *rs[MAX_T]; int nr = 0;
    for (int i = 0; i < b->nt; i++) if (b->t[i]->online && b->t[i]->da == old) rs[nr++] = b->t[i];
    b->od_clk += 9;
    if (nr == 0) return I3C_ERR_NACK;
    b->pp_clk += 9;
    for (int k = 0; k < nr; k++) rs[k]->da = nw;
    c->slot[old] = SLOT_FREE; c->slot[nw] = SLOT_I3C; d->da = nw;
    blog(b, "START 7E/W CCC SETNEWDA(0x88) Sr %02X/W 0x%02X(=0x%02X<<1) STOP", old, nw << 1, nw);
    return I3C_OK;
}
static int ctrl_swap_da(Ctrl *c, uint8_t x, uint8_t y) {
    int tmp = get_free_addr(c, 8), r;
    if (tmp < 0) return I3C_ERR_NOADDR;
    if ((r = ctrl_setnewda(c, x, (uint8_t)tmp))) return r;
    if ((r = ctrl_setnewda(c, y, x))) return r;
    return ctrl_setnewda(c, (uint8_t)tmp, y);
}

/* 연습 4 — 빈 주소 수 공식 */
static int free_formula(const uint8_t *leg, int nl) {
    bool seen[128] = { false }; int extra = 0;
    for (int i = 0; i < nl; i++) {
        uint8_t a = leg[i];
        if (a >= 128 || seen[a]) continue;
        seen[a] = true;
        bool rsvd = a < 8 || a == I3C_BCAST || pop8((uint8_t)(a ^ I3C_BCAST)) == 1;
        if (!rsvd) extra++;
    }
    return 112 - extra;
}

int main(void) {
    { /* 연습 1 */
        PidF f = pid_decode(0x04A2100A5000ull);
        printf("ex1: PID 04a2100a5000 -> manuf 0x%04X rnd %d part 0x%04X inst %u extra 0x%03X\n",
               f.manuf, f.rnd, f.part, f.inst, f.extra);
        CHECK(f.manuf == 0x0251 && !f.rnd && f.part == 0x100A && f.inst == 5 && f.extra == 0);
        int bad = 0;
        for (int i = 0; i < 5000; i++) {
            uint16_t m = (uint16_t)(xs32() & 0x7FFF), p = (uint16_t)xs32(), e = (uint16_t)(xs32() & 0xFFF);
            uint8_t in = (uint8_t)(xs32() & 0xF);
            uint64_t pid = pid_make_fixed(m, p, in, e);
            PidF g = pid_decode(pid);
            if (g.manuf != m || g.rnd || g.part != p || g.inst != in || g.extra != e || pid >> 48) bad++;
        }
        CHECK(bad == 0);
        /* 같은 부품 4개, 인스턴스 핀만 다름 → ENTDAA 순서 = 인스턴스 순서 */
        bus_reset(4, 0, false);
        for (int i = 0; i < 4; i++) t_set(&g_t[i], "imu", pid_make_fixed(0x0251, 0x100A, (uint8_t)(3 - i), 0), 0x06, 0, 0);
        addr_slots_init(&g_c, NULL, 0);
        CHECK(ctrl_entdaa(&g_c) == 4 && g_t[3].da == 0x08 && g_t[2].da == 0x09 && g_t[1].da == 0x0A && g_t[0].da == 0x0B);
        /* 랜덤 PID 모드: bit32 = 1 이면 하위 32비트가 랜덤 */
        PidF r = pid_decode(((uint64_t)0x0251 << 33) | (1ull << 32) | 0xDEADBEEFu);
        CHECK(r.rnd && r.rndval == 0xDEADBEEFu && r.manuf == 0x0251);
    }
    { /* 연습 2 */
        bus_reset(5, 0, true);
        for (int i = 0; i < 4; i++) t_set(&g_t[i], "s", 0x000100000000ull * (uint64_t)(i + 1), 0x06, 0, 0);
        t_set(&g_t[4], "imu", 0x7F0000000000ull, 0x06, 0, 0);      /* PID 가 커서 DA 0x0C */
        addr_slots_init(&g_c, NULL, 0);
        CHECK(ctrl_entdaa(&g_c) == 5 && g_t[4].da == 0x0C && g_t[0].da == 0x08);
        for (int k = 0; k < g_c.ndev; k++) g_c.dev[k].ibi_on = true;
        int from = g_b.nlog;
        CHECK(ctrl_swap_da(&g_c, 0x0C, 0x08) == I3C_OK);
        printf("ex2: swap imu 0x0C <-> 0x08:\n"); log_print(&g_b, from);
        CHECK(g_t[4].da == 0x08 && g_t[0].da == 0x0C && g_c.slot[0x0D] == SLOT_FREE && da_invariants(&g_c));
        for (int i = 0; i < 5; i++) { g_t[i].ibi_req = true; g_t[i].mdb = (uint8_t)i; }
        CHECK(ctrl_service(&g_c, 20) == 5 && g_c.q[0].da == 0x08 && g_c.q[0].mdb == 4);
        CHECK(ctrl_setnewda(&g_c, 0x08, 0x09) == I3C_ERR_ARG && ctrl_setnewda(&g_c, 0x30, 0x31) == I3C_ERR_ARG);
    }
    { /* 연습 3 — ENTDAA 클럭 수 닫힌 식 */
        int bad = 0;
        printf("ex3: N  OD clk  PP clk  time @OD 1 MHz/PP 12.5 MHz (가정)\n");
        for (int n = 0; n <= 32; n++) {
            bus_reset(n, 0, false);
            for (int i = 0; i < n; i++) t_set(&g_t[i], "t", 0x000100000000ull + (uint64_t)i * 0x1000u, 0x06, 0, 0);
            addr_slots_init(&g_c, NULL, 0);
            int r = ctrl_entdaa(&g_c);
            uint32_t od = n ? 9u + 82u * (uint32_t)n + 9u : 9u, pp = n ? 9u : 0u;
            if (r != n || g_b.od_clk != od || g_b.pp_clk != pp) bad++;
            if (n == 1 || n == 4 || n == 8 || n == 16 || n == 32)
                printf("     %2d  %6u  %6u  %7.2f us\n", n, (unsigned)g_b.od_clk, (unsigned)g_b.pp_clk,
                       g_b.od_clk / 1.0 + g_b.pp_clk / 12.5);
        }
        CHECK(bad == 0);
        /* NACK 재시도 1번 = 라운드 1개(82 클럭) 추가 */
        bus_reset(3, 0, false);
        for (int i = 0; i < 3; i++) t_set(&g_t[i], "t", 0x000100000000ull + (uint64_t)i, 0x06, 0, 0);
        addr_slots_init(&g_c, NULL, 0);
        g_b.corrupt_da = 0x01; g_b.corrupt_n = 1;
        CHECK(ctrl_entdaa(&g_c) == 3 && g_b.od_clk == 9 + 82 * 4 + 9);
    }
    { /* 연습 4 — 빈 주소 수 = 112 − (예약이 아닌 서로 다른 레거시 주소 수), 고갈 시 정확히 그만큼 */
        int bad = 0;
        for (int it = 0; it < 300; it++) {
            int nl = (int)(xs32() % 9); uint8_t leg[MAX_L + 1];
            for (int i = 0; i < nl; i++) leg[i] = (uint8_t)(xs32() % 128);
            bus_reset(0, 0, false);
            if (addr_slots_init(&g_c, leg, nl) != free_formula(leg, nl)) bad++;
        }
        CHECK(bad == 0);
        int bad2 = 0;
        for (int it = 0; it < 20; it++) {
            int nl = (int)(xs32() % 9); uint8_t leg[MAX_L + 1];
            for (int i = 0; i < nl; i++) leg[i] = (uint8_t)(xs32() % 128);
            int cap = free_formula(leg, nl), nt = cap + 3;
            bus_reset(nt, 0, false);
            for (int i = 0; i < nt; i++) t_set(&g_t[i], "t", 0x000100000000ull + (uint64_t)xs32() * 16u + (uint64_t)i, 0x06, 0, 0);
            addr_slots_init(&g_c, leg, nl);
            int r = ctrl_entdaa(&g_c), left = 0;
            for (int i = 0; i < nt; i++) left += g_t[i].da == 0;
            if (r != I3C_ERR_NOADDR || g_c.ndev != cap || left != 3 || !da_invariants(&g_c)) bad2++;
        }
        printf("ex4: free-slot formula 300 random legacy sets, exhaustion 20 runs, bad=%d/%d\n", bad, bad2);
        CHECK(bad2 == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
