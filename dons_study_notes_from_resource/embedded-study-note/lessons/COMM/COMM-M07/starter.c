/* COMM-M07 CAN 에러 처리: TEC/REC와 error-passive·bus-off
 *
 * 모델 (ISO 11898-1 / Bosch CAN 2.0B "Fault Confinement" 요지 — 코너 규칙은 스펙 확인 권장):
 *   dominant = 0, recessive = 1, 버스 = 모든 드라이버의 AND.
 *   TEC/REC 규칙, 상태 = error-active (TEC<=127 && REC<=127) / error-passive / bus-off (TEC>255).
 *   모델 정의: REC 는 255 에서 포화, REC>127 에서 성공 수신 → 127 (스펙은 119..127 중 하나), WARNING = 96.
 * Part A: 카운터 규칙 + 상태 전이 (fc_tx_error / fc_rx_error / fc_tx_ok / fc_rx_ok / fc_penalty8)
 * Part B: 예외 2개, 에러 플래그 중첩 비트 시뮬레이션 (rule 2: 먼저 본 수신자 +8, rule 6: 연속 dominant),
 *         bus-off 복구 = 11 연속 recessive 128회
 * Part C: 프레임 수준 버스 시나리오 (고장 송신기·고장 수신기·혼자 남은 노드·babbling idiot)
 *         + 드라이버 상태 로그·재시작 정책 + Linux `ip -details -statistics` 형식 출력
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Part A: 카운터와 상태 ---------------- */
enum {
    WARN_LIMIT = 96, PASSIVE_LIMIT = 128, BUSOFF_LIMIT = 256,
    REC_MAX = 255,            /* 모델 정의: 8비트 레지스터처럼 255 에서 포화 */
    REC_PASSIVE_RESET = 127,  /* 모델 정의: REC>127 일 때 성공 수신 → 127 */
    RECOV_RUN = 11, RECOV_OCC = 128
};
typedef enum { ST_ACTIVE, ST_PASSIVE, ST_BUSOFF } CanState;
typedef enum { ERR_BIT, ERR_STUFF, ERR_CRC, ERR_FORM, ERR_ACK } ErrType;

typedef struct {
    uint16_t tec, rec;
    CanState state;
    bool     auto_recover;  /* true: bus-off 진입 즉시 128×11 카운트 시작 (STM32 bxCAN ABOM=1 류) */
    bool     recov_armed;   /* 복구 카운트 중인가 (auto 가 아니면 host 요청 뒤에만) */
    uint8_t  recov_run;     /* 지금까지 연속 recessive 수 (0..10) */
    uint16_t recov_occ;     /* "11 연속 recessive" 발생 수 (0..128) */
    uint32_t n_passive, n_busoff, n_recovered;  /* 상태 전이 횟수 */
} FcNode;

static const char *state_name(CanState s) {
    return s == ST_ACTIVE ? "error-active" : s == ST_PASSIVE ? "error-passive" : "bus-off";
}

/* TEC/REC → 상태 (bus-off 래치는 fc_update 가 처리) */
static CanState fc_state_of(unsigned tec, unsigned rec) {
    (void)tec; (void)rec;
    return ST_ACTIVE;   /* TODO A1: tec >= 256 → BUSOFF, tec 또는 rec >= 128 → PASSIVE, 아니면 ACTIVE */
}

/* 카운터가 바뀐 뒤 상태 갱신. bus-off 는 카운터가 아니라 복구 절차만이 끝낸다. */
static void fc_update(FcNode *n) {
    (void)n;   /* TODO A2: bus-off 면 그대로(래치). 아니면 fc_state_of 로 새 상태, 전이 횟수, bus-off 진입 시 복구 카운터 초기화 */
}

/* 송신자가 에러 감지 (rule 3: TEC += 8) — 예외 2개 (Part B):
 *  exc1: error-passive 송신자의 ACK 에러이고, passive error flag 동안 dominant 를 못 봤으면 그대로
 *  exc2: 중재 구간의 recessive stuff bit 를 dominant 로 읽은 stuff error 면 그대로 */
static void fc_tx_error(FcNode *n, ErrType e, bool dom_in_pflag, bool arb_rec_stuff) {
    (void)n; (void)e; (void)dom_in_pflag; (void)arb_rec_stuff;   /* TODO A3: TEC += 8 (예외 2개 제외) → fc_update */
}

/* 수신자가 에러 감지 (rule 1: REC += 1) */
static void fc_rx_error(FcNode *n) {
    (void)n;   /* TODO A4: REC += 1 (REC_MAX 포화) → fc_update */
}

/* +8 벌점: rule 2 (수신자: 자기 에러 플래그 뒤 첫 비트가 dominant), rule 4/5 (active 플래그 송신 중 bit error),
 * rule 6 (플래그 뒤 dominant 8개마다). is_tx 면 TEC, 아니면 REC. */
static void fc_penalty8(FcNode *n, bool is_tx) {
    (void)n; (void)is_tx;   /* TODO A5: 역할에 맞는 카운터 += 8 → fc_update */
}

/* 성공 송신 (ACK 받고 EOF 까지 에러 없음): TEC -= 1 (0 이면 그대로) */
static void fc_tx_ok(FcNode *n) {
    (void)n;   /* TODO A6: TEC -= 1 (0 이면 그대로) → fc_update */
}

/* 성공 수신 (ACK slot 까지 에러 없고 ACK 를 보냄): REC 1..127 → -1, >127 → REC_PASSIVE_RESET */
static void fc_rx_ok(FcNode *n) {
    (void)n;   /* TODO A7: REC 1..127 → -1, > 127 → REC_PASSIVE_RESET → fc_update */
}

/* ---------------- Part B: bus-off 복구 + 에러 플래그 중첩 ---------------- */

/* host 가 복구를 요청 (auto_recover 가 아닐 때). 요청 시점부터 센다. */
static void fc_request_recovery(FcNode *n) {
    (void)n;   /* TODO B1: bus-off 이고 아직 안 세는 중이면 recov_armed = true, run/occ = 0 */
}

/* bus-off 노드가 버스 비트 하나를 본다. 11 연속 recessive = 1회 (센 뒤 0 부터 다시), 128회면 복구.
 * 반환 true = 이 비트에서 error-active 로 복귀 (TEC = REC = 0). */
static bool fc_recovery_bit(FcNode *n, int bit) {
    (void)n; (void)bit;
    return false;   /* TODO B2: recessive 11 연속 = 1회 (센 뒤 0부터), dominant 는 run = 0, 128회면 active + TEC=REC=0 */
}

/* 에러 플래그 중첩 비트 시뮬레이션. 비트 0 부근에서 에러가 났고, 그 전 프레임 비트는 recessive 로 끝났다고 둔다.
 * 노드: active 플래그 = dominant 6, passive 플래그 = "같은 레벨 6 연속을 볼 때까지" recessive.
 * 플래그 뒤: recessive 를 내며 버스가 recessive 가 될 때까지 기다림 (그동안 dominant 수 d 를 셈)
 *   - 수신자이고 d == 1 (플래그 뒤 첫 비트가 dominant) → REC += 8          (rule 2)
 *   - d 가 8, 16, 24, … 가 될 때마다 송신자 TEC / 수신자 REC += 8           (rule 6: 14번째 = 플래그 6 + 8)
 *   첫 recessive 부터 error delimiter 8 비트.
 * 플래그 시작 시 기본 증가: 송신자 TEC += 8 (rule 3), 수신자 REC += 1 (rule 1). */
enum { FS_MAXN = 8, FS_MAXT = 160 };
typedef struct {
    bool is_tx;
    bool passive;   /* error-passive → passive 플래그 */
    int  detect;    /* 플래그를 시작하는 비트 (>= 0). -1 = 스스로 못 보고 버스의 6 연속 dominant(stuff error)로 감지 */
} FsNode;
typedef struct {
    int flag_start, flag_end;  /* 자기 플래그 [start, end] (-1 = 플래그 안 냄) */
    int delim_end;             /* error delimiter 마지막 비트 (-1 = 미완) */
    int tec_d, rec_d;          /* 카운터 증가량 */
} FsOut;
typedef enum { FS_WAIT, FS_FLAG, FS_AFTER, FS_DELIM, FS_DONE } FsPhase;

/* stuck_len: 외부(고장 트랜시버 등)가 비트 [0, stuck_len) 동안 dominant 를 민다.
 * wave: 버스 레벨 '0'/'1' (wcap 안에서 NUL 종료). 반환 = 시뮬레이션한 비트 수. */
static int flag_superpose(const FsNode *nd, int k, int stuck_len, FsOut *out, char *wave, size_t wcap) {
    (void)nd; (void)stuck_len;
    for (int i = 0; i < k && i < FS_MAXN; i++) out[i] = (FsOut){ -1, -1, -1, 0, 0 };
    if (wcap) wave[0] = '\0';
    return 0;   /* TODO B3: 비트마다 버스 = AND(active 플래그, stuck) → 노드별 WAIT/FLAG/AFTER/DELIM 진행 */
}

/* ---------------- Part C: 프레임 수준 버스 + 드라이버 ---------------- */
enum { MAXN = 6, QCAP = 8, IDLE_SLOT_BITS = 16, BODY_BITS = 97, ERR_AT_BITS = 40,
       LOG_LINES = 24, LOG_W = 96 };
typedef enum { F_NONE, F_TX_CORRUPT, F_RX_LOCAL } Fault;
typedef enum { OUT_IDLE, OUT_OK, OUT_TX_CORRUPT, OUT_RX_PRIMARY, OUT_GLOBAL, OUT_ACK_ERR } Outcome;

typedef struct {
    FcNode   fc;
    const char *name;
    uint16_t id;           /* 11비트 ID, 낮을수록 우선 */
    bool     present;      /* 버스에 붙어 있나 */
    Fault    fault;        /* F_TX_CORRUPT: 자기 프레임을 깨뜨림, F_RX_LOCAL: 모든 프레임에서 혼자 에러를 봄 */
    bool     babble;       /* 늘 보낼 프레임이 있다 */
    uint32_t period;       /* period 슬롯마다 프레임 1개 (0 = 없음) */
    uint32_t q[QCAP], qh, qn, dropped;  /* 대기 프레임의 생성 슬롯 (FIFO) */
    uint32_t tx_ok, tx_att, tx_err, rx_ok, rx_err, arb_lost, max_wait;
    bool     off_at_start; /* 이번 슬롯 시작 때 bus-off → 이 슬롯 비트로 복구 카운트 */
    uint64_t busoff_at, recovered_at;   /* 비트 시각 */
} Node;

typedef struct {
    Node     nd[MAXN];
    int      n;
    uint64_t bits;        /* 버스 시간 (비트) */
    uint32_t slot;
    bool     global_err;  /* 다음 시도에 전역 교란 (EMI) 1회 주입 */
    int      last_tx;     /* 지난 시도의 송신 노드 (-1 = idle) */
    Outcome  last_out;
} Bus;

/* 한 번의 송신 시도에 대한 카운터 갱신 (표는 md 5. 풀이 Part C).
 * t = 중재에서 이긴 송신 노드. 참여자 = present 이고 bus-off 아닌 노드.
 *  우선순위: 송신자 고장 > active 고장 수신자(먼저 보고 active 플래그) > 전역 교란 > ACK 할 노드 없음 > 성공
 *  수신자: 프레임이 깨졌으면 모두 REC+1, 먼저 본 active 고장 수신자는 rule 2 로 +8 더.
 *          프레임이 성공(또는 ACK 에러 — 수신자에겐 끝까지 정상)이면 정상 수신자 rx_ok, passive 고장 수신자는 혼자 +1. */
static Outcome bus_attempt(Bus *b, int t) {
    (void)b; (void)t;
    return OUT_OK;   /* TODO C1: 결과 판정 (우선순위 표) + 송신자·수신자 카운터 갱신 + rx_ok/rx_err */
}

/* ---- 드라이버 (Linux can 상태 이름과 can_device_stats 를 흉내) ---- */
typedef enum { L_ACTIVE, L_WARNING, L_PASSIVE, L_BUSOFF, L_STOPPED } LState;
static const char *const LNAME[] = { "ERROR-ACTIVE", "ERROR-WARNING", "ERROR-PASSIVE", "BUS-OFF", "STOPPED" };

typedef struct {
    uint32_t bitrate;
    uint32_t restart_ms;    /* 0 = 자동 재시작 안 함 (Linux restart-ms 0) */
    uint32_t max_restarts;  /* 펌웨어 정책: bus-off 가 이보다 많으면 STOPPED */
    LState   last;
    uint64_t restart_at;    /* 예약된 복구 요청 시각 (비트, 0 = 없음) */
    uint32_t bus_error, error_warning, error_passive, bus_off, arbitration_lost, restarts;
    uint32_t seen_err;
    char     log[LOG_LINES][LOG_W];
    int      nlog;
    uint32_t log_dropped;
} Drv;

static void drv_log(Drv *d, uint64_t now, const char *fmt, ...) {   /* (주어짐) */
    if (d->nlog >= LOG_LINES) { d->log_dropped++; return; }
    char *p = d->log[d->nlog++];
    int w = snprintf(p, LOG_W, "%9.1f us  ", (double)now * 1e6 / d->bitrate);
    va_list ap;
    va_start(ap, fmt);
    if (w > 0 && w < LOG_W) vsnprintf(p + w, (size_t)(LOG_W - w), fmt, ap);
    va_end(ap);
}

/* 컨트롤러 상태 → Linux 레벨 (WARNING = TEC 또는 REC >= 96, 모델 정의 = Linux can_state 주석) */
static LState lstate_of(const FcNode *f) {
    (void)f;
    return L_ACTIVE;   /* TODO C2: BUSOFF → L_BUSOFF, max(TEC,REC) >= 128 → PASSIVE, >= 96 → WARNING */
}

/* 매 슬롯 뒤 호출 (ISR 의 status-change 처리 + 주기 타이머 역할).
 *  - 에러 수(bus_error) / arbitration lost 집계
 *  - 레벨이 바뀌면 로그 "OLD -> NEW (tx T rx R)", 올라갈 때 새 레벨의 통계 +1
 *  - BUS-OFF: restart_ms > 0 이고 bus_off <= max_restarts 면 restart_ms << (bus_off-1) 뒤 복구 요청 예약 (백오프),
 *             넘으면 STOPPED (더는 복구 안 함)
 *  - 예약 시각이 되면 fc_request_recovery + restarts++ */
static void drv_poll(Drv *d, Node *x, uint64_t now) {
    (void)d; (void)x; (void)now;   /* TODO C3: 통계, 레벨 변화 로그, bus-off 재시작 예약(백오프)/STOPPED, 예약 시각에 복구 요청 */
}

/* `ip -details -statistics link show can0` 의 CAN 부분 형식. 반환 = 길이, 잘리면 -1. */
static int drv_format_ip(const Drv *d, const FcNode *f, char *buf, size_t cap) {
    (void)d; (void)f;
    if (buf && cap) buf[0] = '\0';
    return 0;   /* TODO C4: "can state ... (berr-counter tx T rx R) restart-ms M" + 통계 두 줄, 잘리면 -1 */
}

/* ---------------- 버스 슬롯 엔진 (주어짐) ---------------- */
static void bus_emit(Bus *b, int bit, int count) {
    for (int k = 0; k < count; k++) {
        b->bits++;
        for (int i = 0; i < b->n; i++) {
            Node *x = &b->nd[i];
            if (x->present && x->off_at_start && fc_recovery_bit(&x->fc, bit)) x->recovered_at = b->bits;
        }
    }
}
/* SOF + nbits 본문 (의사난수 비트, 내용은 무의미) + 5 연속 뒤 반대 비트 삽입 (COMM-M04 스터핑 축약판) */
static void emit_stuffed(Bus *b, uint32_t seed, int nbits) {
    int last = 0, run = 1;
    uint32_t x = seed * 2654435761u | 1u;
    bus_emit(b, 0, 1);
    for (int i = 0; i < nbits; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int bit = (int)(x >> 31);
        bus_emit(b, bit, 1);
        run = bit == last ? run + 1 : 1; last = bit;
        if (run == 5) { bus_emit(b, !bit, 1); last = !bit; run = 1; }
    }
}
/* 시도 하나의 비트열. 성공: 프레임 + CRC delim, ACK(dom), ACK delim, EOF 7, IFS 3.
 * 에러: 감지 지점까지 + dominant 플래그(중첩) 또는 passive 플래그 6 rec + delimiter 8 + IFS 3. */
static void emit_attempt(Bus *b, int t, Outcome o, const CanState *pre) {
    bool tx_act = pre[t] == ST_ACTIVE, rx_act = false;
    for (int i = 0; i < b->n; i++)
        if (i != t && b->nd[i].present && pre[i] == ST_ACTIVE) rx_act = true;
    uint32_t seed = (uint32_t)b->nd[t].id << 16 ^ b->nd[t].tx_att;
    if (o == OUT_OK) {
        emit_stuffed(b, seed, BODY_BITS);
        bus_emit(b, 1, 1); bus_emit(b, 0, 1); bus_emit(b, 1, 1 + 7 + 3);
        return;
    }
    int dom;
    if (o == OUT_ACK_ERR) {
        emit_stuffed(b, seed, BODY_BITS);
        bus_emit(b, 1, 2);                                   /* CRC delim + 아무도 안 당긴 ACK slot */
        dom = tx_act ? 6 : 0;
    } else {
        emit_stuffed(b, seed, ERR_AT_BITS);
        if (o == OUT_TX_CORRUPT)      dom = (tx_act ? 6 : 0) + (rx_act ? 6 : 0);
        else if (o == OUT_RX_PRIMARY) dom = 12;              /* 고장 수신자 6 + 나머지가 stuff error 로 뒤따라 6 */
        else                          dom = (tx_act || rx_act) ? 6 : 0;  /* 전역: 모두 같은 비트에서 감지 */
    }
    bus_emit(b, 0, dom);
    if (dom == 0) bus_emit(b, 1, 6);                         /* passive 플래그만 */
    bus_emit(b, 1, 8 + 3);                                   /* error delimiter + IFS */
}

static void q_push(Node *x, uint32_t slot) {
    if (x->qn == QCAP) { x->dropped++; return; }
    x->q[(x->qh + x->qn++) % QCAP] = slot;
}

static void bus_step(Bus *b) {
    CanState pre[MAXN];
    b->slot++;
    for (int i = 0; i < b->n; i++) {
        Node *x = &b->nd[i];
        if (!x->present) continue;
        if (x->babble && x->qn == 0) q_push(x, b->slot);
        if (x->period && b->slot % x->period == 0) q_push(x, b->slot);
    }
    int ncand = 0;
    for (int i = 0; i < b->n; i++) {
        Node *x = &b->nd[i];
        pre[i] = x->fc.state;
        x->off_at_start = x->present && x->fc.state == ST_BUSOFF;
        if (x->present && x->fc.state != ST_BUSOFF && x->qn) ncand++;
    }
    int t = -1;
    for (int i = 0; i < b->n; i++) {
        Node *x = &b->nd[i];
        if (!x->present || x->fc.state == ST_BUSOFF || !x->qn) continue;
        /* suspend transmission: 방금 보낸 passive 노드는 8비트 더 기다린다 → 다른 노드가 있으면 양보 */
        if (b->last_tx == i && x->fc.state == ST_PASSIVE && ncand > 1) continue;
        if (t < 0 || x->id < b->nd[t].id) t = i;
    }
    if (t < 0) { bus_emit(b, 1, IDLE_SLOT_BITS); b->last_tx = -1; b->last_out = OUT_IDLE; return; }
    for (int i = 0; i < b->n; i++) {
        Node *x = &b->nd[i];
        if (i != t && x->present && x->fc.state != ST_BUSOFF && x->qn &&
            !(b->last_tx == i && x->fc.state == ST_PASSIVE)) x->arb_lost++;
    }
    Node *T = &b->nd[t];
    if (b->last_tx == t && T->fc.state == ST_PASSIVE) bus_emit(b, 1, 8);   /* suspend transmission */
    Outcome o = bus_attempt(b, t);
    T->tx_att++;
    if (o == OUT_OK) {
        uint32_t w = b->slot - T->q[T->qh];
        if (w > T->max_wait) T->max_wait = w;
        T->qh = (T->qh + 1) % QCAP; T->qn--; T->tx_ok++;
    } else {
        T->tx_err++;
    }
    emit_attempt(b, t, o, pre);
    for (int i = 0; i < b->n; i++)
        if (pre[i] != ST_BUSOFF && b->nd[i].fc.state == ST_BUSOFF) b->nd[i].busoff_at = b->bits;
    b->last_tx = t;
    b->last_out = o;
}

static int bus_add(Bus *b, const char *name, uint16_t id) {
    int i = b->n++;
    memset(&b->nd[i], 0, sizeof b->nd[i]);
    b->nd[i].name = name; b->nd[i].id = id; b->nd[i].present = true;
    return i;
}
static void bus_init(Bus *b) { memset(b, 0, sizeof *b); b->last_tx = -1; }
static void drv_init(Drv *d, uint32_t bitrate, uint32_t restart_ms, uint32_t max_restarts) {
    memset(d, 0, sizeof *d);
    d->bitrate = bitrate; d->restart_ms = restart_ms; d->max_restarts = max_restarts;
}
static void drv_print(const Drv *d, const char *name) {
    for (int i = 0; i < d->nlog; i++) printf("  [%s] %s\n", name, d->log[i]);
    printf("  [%s] driver now reports %s\n", name, LNAME[d->last]);
}

/* ---------------- 테스트 하네스 (주어짐) ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs_state = 0xC0FFEE07u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}
static int log_has(const Drv *d, const char *needle) {   /* needle 이 든 첫 로그 줄 번호, 없으면 -1 */
    for (int i = 0; i < d->nlog; i++) if (strstr(d->log[i], needle)) return i;
    return -1;
}
static double us_at(uint64_t bits, uint32_t bitrate) { return (double)bits * 1e6 / bitrate; }

int main(void) {
    /* ===== Part A: 카운터 규칙과 상태 ===== */
    CHECK(fc_state_of(0, 0) == ST_ACTIVE && fc_state_of(127, 127) == ST_ACTIVE);
    CHECK(fc_state_of(128, 0) == ST_PASSIVE && fc_state_of(0, 128) == ST_PASSIVE);
    CHECK(fc_state_of(255, 0) == ST_PASSIVE && fc_state_of(256, 0) == ST_BUSOFF);
    CHECK(fc_state_of(0, 255) == ST_PASSIVE);                     /* REC 만으로는 bus-off 불가 */

    FcNode f = {0};
    int k;
    for (k = 1; k <= 40 && f.state == ST_ACTIVE; k++) fc_tx_error(&f, ERR_BIT, false, false);
    printf("broken TX: passive after %d errors (TEC %u)\n", k - 1, (unsigned)f.tec);
    CHECK(k - 1 == 16 && f.tec == 128 && f.n_passive == 1);
    for (; k <= 40 && f.state != ST_BUSOFF; k++) fc_tx_error(&f, ERR_BIT, false, false);
    printf("broken TX: bus-off after %d errors (TEC %u)\n", k - 1, (unsigned)f.tec);
    CHECK(k - 1 == 32 && f.tec == 256 && f.n_busoff == 1);
    fc_tx_ok(&f); fc_rx_ok(&f); fc_tx_error(&f, ERR_BIT, false, false); fc_rx_error(&f);
    CHECK(f.tec == 256 && f.rec == 0 && f.state == ST_BUSOFF);    /* bus-off 는 래치: 카운터 이벤트 무시 */

    f = (FcNode){0};
    f.rec = 0;   fc_rx_ok(&f); CHECK(f.rec == 0);
    f.rec = 50;  fc_rx_ok(&f); CHECK(f.rec == 49);
    f.rec = 127; fc_rx_ok(&f); CHECK(f.rec == 126);
    f = (FcNode){0};
    for (int i = 0; i < 128; i++) fc_rx_error(&f);
    CHECK(f.rec == 128 && f.state == ST_PASSIVE);
    fc_rx_ok(&f);
    CHECK(f.rec == REC_PASSIVE_RESET && f.state == ST_ACTIVE);    /* 128 → 127: 한 번에 active 복귀 */
    for (int i = 0; i < 400; i++) fc_rx_error(&f);
    CHECK(f.rec == REC_MAX && f.state == ST_PASSIVE);             /* 포화, bus-off 아님 */
    fc_rx_ok(&f);
    CHECK(f.rec == 127 && f.state == ST_ACTIVE);
    fc_penalty8(&f, false); CHECK(f.rec == 135 && f.state == ST_PASSIVE);
    f = (FcNode){0};
    fc_penalty8(&f, true); CHECK(f.tec == 8 && f.rec == 0);
    f.tec = 128; fc_update(&f); CHECK(f.state == ST_PASSIVE);
    fc_tx_ok(&f); CHECK(f.tec == 127 && f.state == ST_ACTIVE);    /* passive → active 는 둘 다 <= 127 */
    f.tec = 0; fc_tx_ok(&f); CHECK(f.tec == 0);

    /* 섞인 성공: (에러 1, 성공 m) 반복 → passive/bus-off 까지 에러 수 = ceil((L - m) / (8 - m)) */
    int bad = 0;
    for (int m = 0; m <= 9; m++) {
        FcNode g = {0};
        int errs = 0, to_pas = 0, to_off = 0;
        for (int cyc = 0; cyc < 3000 && g.state != ST_BUSOFF; cyc++) {
            fc_tx_error(&g, ERR_BIT, false, false); errs++;
            if (!to_pas && g.state == ST_PASSIVE) to_pas = errs;
            if (g.state == ST_BUSOFF) { to_off = errs; break; }
            for (int s = 0; s < m; s++) fc_tx_ok(&g);
        }
        int ep = m < 8 ? (128 - m + (8 - m) - 1) / (8 - m) : 0;
        int eo = m < 8 ? (256 - m + (8 - m) - 1) / (8 - m) : 0;
        printf("  1 error + %d ok: passive at error %3d (formula %3d), bus-off at error %3d (formula %3d)\n",
               m, to_pas, ep, to_off, eo);
        bad += to_pas != ep || to_off != eo;
    }
    CHECK(bad == 0);

    /* 랜덤 이벤트: 각 이벤트의 카운터 변화·상태를 규칙표와 대조 (3000 시퀀스 × 200 이벤트) */
    bad = 0;
    int rx_only_busoff = 0;
    for (int seq = 0; seq < 3000; seq++) {
        FcNode g = {0};
        bool rx_only = (xorshift32() & 3) == 0;
        for (int ev = 0; ev < 200; ev++) {
            FcNode p = g;
            uint32_t r = xorshift32();
            int kind = rx_only ? 3 + (int)(r % 3) : (int)(r % 7);
            unsigned et = 0, er = 0;    /* 기대 TEC / REC */
            if (kind == 0 || kind == 1) {               /* 송신 에러 (ACK 또는 stuff, 예외 플래그 랜덤) */
                ErrType e = kind == 0 ? ERR_ACK : ERR_STUFF;
                bool dom = r >> 8 & 1, arb = r >> 9 & 1;
                fc_tx_error(&g, e, dom, arb);
                bool skip = (e == ERR_ACK && p.state == ST_PASSIVE && !dom) || (e == ERR_STUFF && arb);
                et = p.tec + (skip ? 0u : 8u); er = p.rec;
            } else if (kind == 2) { fc_tx_ok(&g); et = p.tec ? p.tec - 1u : 0u; er = p.rec; }
            else if (kind == 3) { fc_rx_error(&g); et = p.tec; er = p.rec + 1u > 255u ? 255u : p.rec + 1u; }
            else if (kind == 4) { fc_rx_ok(&g); et = p.tec; er = p.rec > 127 ? 127u : p.rec ? p.rec - 1u : 0u; }
            else if (kind == 5) { fc_penalty8(&g, false); et = p.tec; er = p.rec + 8u > 255u ? 255u : p.rec + 8u; }
            else { fc_penalty8(&g, true); et = p.tec + 8u; er = p.rec; }
            if (p.state == ST_BUSOFF) { et = p.tec; er = p.rec; }
            CanState es = p.state == ST_BUSOFF ? ST_BUSOFF
                        : et > 255 ? ST_BUSOFF : (et > 127 || er > 127) ? ST_PASSIVE : ST_ACTIVE;
            bad += g.tec != et || g.rec != er || g.state != es;
        }
        if (rx_only && g.state == ST_BUSOFF) rx_only_busoff++;
    }
    printf("random: 600000 events, mismatches=%d, receiver-only bus-off=%d\n", bad, rx_only_busoff);
    CHECK(bad == 0);
    CHECK(rx_only_busoff == 0);

    /* ===== Part B: 예외 규칙 ===== */
    f = (FcNode){0}; f.tec = 128; fc_update(&f);
    fc_tx_error(&f, ERR_ACK, false, false); CHECK(f.tec == 128);  /* exc1: passive + ACK + flag 중 dominant 없음 */
    fc_tx_error(&f, ERR_ACK, true, false);  CHECK(f.tec == 136);  /* flag 중 dominant 봤으면 +8 */
    f = (FcNode){0};
    fc_tx_error(&f, ERR_ACK, false, false); CHECK(f.tec == 8);    /* active 면 ACK 에러도 +8 */
    fc_tx_error(&f, ERR_STUFF, false, true); CHECK(f.tec == 8);   /* exc2: 중재 중 recessive stuff bit 를 dominant 로 */
    fc_tx_error(&f, ERR_STUFF, false, false); CHECK(f.tec == 16);
    f.tec = 120; f.state = ST_ACTIVE;
    for (int i = 0; i < 100; i++) fc_tx_error(&f, ERR_ACK, false, false);
    CHECK(f.tec == 128 && f.state == ST_PASSIVE);                 /* 혼자 남은 노드: 128 에서 멈춤 */

    /* ===== Part B: 에러 플래그 중첩 ===== */
    FsNode fs[4];
    FsOut fo[4];
    char wave[FS_MAXT + 1];
    int nb;
    /* B1. 모두 같은 비트에서 감지 (전역 에러) → 플래그 6 + delimiter 8, 아무도 +8 없음 */
    fs[0] = (FsNode){ true, false, 0 }; fs[1] = (FsNode){ false, false, 0 }; fs[2] = (FsNode){ false, false, 0 };
    nb = flag_superpose(fs, 3, 0, fo, wave, sizeof wave);
    printf("aligned   : %s (%d bits)\n", wave, nb);
    CHECK(strcmp(wave, "00000011111111") == 0 && nb == 14);
    CHECK(fo[0].tec_d == 8 && fo[1].rec_d == 1 && fo[2].rec_d == 1 && fo[1].delim_end == 13);
    /* B2. 수신자 1 이 혼자 먼저 봄 (로컬 고장) → 나머지는 6 dominant 를 stuff error 로 보고 뒤따름 */
    fs[0] = (FsNode){ true, false, -1 }; fs[1] = (FsNode){ false, false, 0 }; fs[2] = (FsNode){ false, false, -1 };
    nb = flag_superpose(fs, 3, 0, fo, wave, sizeof wave);
    printf("primary RX: %s (%d bits)  primary REC+%d, other RX REC+%d, TX TEC+%d\n",
           wave, nb, fo[1].rec_d, fo[2].rec_d, fo[0].tec_d);
    CHECK(strcmp(wave, "00000000000011111111") == 0);
    CHECK(fo[1].rec_d == 9 && fo[2].rec_d == 1 && fo[0].tec_d == 8);
    CHECK(fo[1].flag_end == 5 && fo[2].flag_start == 6 && fo[2].flag_end == 11 && fo[1].delim_end == 19);
    /* B3. 송신자는 비트 2 에서 bit error 로 감지 → 중첩 길이는 그대로 12 */
    fs[0].detect = 2;
    flag_superpose(fs, 3, 0, fo, wave, sizeof wave);
    CHECK(strcmp(wave, "00000000000011111111") == 0 && fo[0].flag_end == 7 && fo[0].tec_d == 8 && fo[1].rec_d == 9);
    /* B4. 고장 노드가 dominant 를 계속 민다: 플래그 뒤 7개까지는 봐준다, 8번째(= 연속 14번째)부터 8개마다 +8 */
    int stucks[] = { 13, 14, 21, 22, 30 };
    int exp_rx[] = { 9, 17, 17, 25, 33 }, exp_tx[] = { 8, 16, 16, 24, 32 };
    for (int i = 0; i < 5; i++) {
        fs[0] = (FsNode){ true, false, 0 }; fs[1] = (FsNode){ false, false, 0 };
        flag_superpose(fs, 2, stucks[i], fo, wave, sizeof wave);
        printf("stuck dominant %2d bits: TX TEC+%d RX REC+%d\n", stucks[i], fo[0].tec_d, fo[1].rec_d);
        CHECK(fo[0].tec_d == exp_tx[i] && fo[1].rec_d == exp_rx[i]);
    }
    /* B5. passive 수신자가 혼자 봄 → passive 플래그는 버스에 안 보인다, 다른 노드는 모름 */
    fs[0] = (FsNode){ true, false, -1 }; fs[1] = (FsNode){ false, true, 0 }; fs[2] = (FsNode){ false, false, -1 };
    nb = flag_superpose(fs, 3, 0, fo, wave, sizeof wave);
    CHECK(strcmp(wave, "11111111111111") == 0 && nb == 14);
    CHECK(fo[1].rec_d == 1 && fo[0].tec_d == 0 && fo[2].rec_d == 0 && fo[0].flag_start == -1);
    /* B6. passive 송신자 (비트 0) + active 수신자들 (비트 3): passive 플래그는 "같은 레벨 6 연속" 까지 = 9비트 */
    fs[0] = (FsNode){ true, true, 0 }; fs[1] = (FsNode){ false, false, 3 }; fs[2] = (FsNode){ false, false, 3 };
    nb = flag_superpose(fs, 3, 0, fo, wave, sizeof wave);
    printf("passive TX: %s  TX flag [%d..%d]\n", wave, fo[0].flag_start, fo[0].flag_end);
    CHECK(strcmp(wave, "11100000011111111") == 0 && fo[0].flag_end == 8 && fo[0].tec_d == 8);
    CHECK(fo[1].rec_d == 1 && fo[2].rec_d == 1 && fo[0].delim_end == 16);

    /* ===== Part B: bus-off 복구 128 × 11 ===== */
    f = (FcNode){0}; f.auto_recover = true;
    for (int i = 0; i < 32; i++) fc_tx_error(&f, ERR_BIT, false, false);
    CHECK(f.state == ST_BUSOFF && f.recov_armed);
    int bits = 0;
    while (bits < 5000 && !fc_recovery_bit(&f, 1)) bits++;
    bits++;
    printf("recovery on idle bus: %d recessive bits\n", bits);
    CHECK(bits == RECOV_RUN * RECOV_OCC && f.state == ST_ACTIVE && f.tec == 0 && f.rec == 0 && f.n_recovered == 1);
    f = (FcNode){0}; f.auto_recover = true; f.tec = 256; fc_update(&f); f.tec = 256;
    bool rec_ok = false;
    for (int i = 0; i < 20000 && !rec_ok; i++) rec_ok = fc_recovery_bit(&f, i % 11 != 10);   /* 10 rec + 1 dom */
    CHECK(!rec_ok && f.state == ST_BUSOFF && f.recov_occ == 0);
    f = (FcNode){0}; f.tec = 250; fc_tx_error(&f, ERR_BIT, false, false);  /* auto 아님 */
    for (int i = 0; i < 3000; i++) fc_recovery_bit(&f, 1);
    CHECK(f.state == ST_BUSOFF && !f.recov_armed);                /* host 요청 전 비트는 안 센다 */
    fc_request_recovery(&f);
    for (bits = 1; bits < 5000 && !fc_recovery_bit(&f, 1); bits++) {}
    CHECK(bits == 1408 && f.state == ST_ACTIVE);
    f = (FcNode){0}; f.auto_recover = true; f.tec = 250; fc_tx_error(&f, ERR_BIT, false, false);
    for (int i = 0; i < 21; i++) fc_recovery_bit(&f, 1);
    CHECK(f.recov_occ == 1 && f.recov_run == 10);                 /* 21 = 11 + 10: 센 뒤 0부터 다시 */
    fc_recovery_bit(&f, 1);
    CHECK(f.recov_occ == 2 && f.recov_run == 0);

    /* ===== Part C: 프레임 수준 시나리오 ===== */
    const uint32_t BR = 500000;
    Bus b; Drv d[MAXN];

    /* C1. 혼자 남은 노드 (케이블 빠짐): ACK 에러만 → passive 에서 멈추고 bus-off 는 안 간다 */
    bus_init(&b);
    int A = bus_add(&b, "A", 0x100);
    b.nd[A].babble = true;
    drv_init(&d[A], BR, 0, 0);
    uint64_t t_pas = 0;
    uint32_t att_pas = 0;
    while (b.nd[A].tx_att < 1000) {
        bus_step(&b); drv_poll(&d[A], &b.nd[A], b.bits);
        if (!t_pas && b.nd[A].fc.state == ST_PASSIVE) {
            t_pas = b.bits; att_pas = b.nd[A].tx_att;
            printf("C1 lone node: passive after %u attempts, %llu bits = %.1f us @500k\n",
                   (unsigned)att_pas, (unsigned long long)b.bits, us_at(b.bits, BR));
        }
    }
    CHECK(att_pas == 16);
    printf("C1 after 1000 attempts: %s TEC %u\n", state_name(b.nd[A].fc.state), (unsigned)b.nd[A].fc.tec);
    drv_print(&d[A], "A");
    CHECK(b.nd[A].fc.tec == 128 && b.nd[A].fc.state == ST_PASSIVE && b.nd[A].fc.n_busoff == 0);
    CHECK(b.nd[A].tx_ok == 0 && b.last_out == OUT_ACK_ERR);
    CHECK(d[A].nlog == 2 && strstr(d[A].log[0], "ERROR-ACTIVE -> ERROR-WARNING (tx 96 rx 0)") &&
          strstr(d[A].log[1], "ERROR-WARNING -> ERROR-PASSIVE (tx 128 rx 0)"));
    CHECK(d[A].bus_error == 1000 && d[A].error_warning == 1 && d[A].error_passive == 1 && d[A].bus_off == 0);

    /* C2. 송신부 고장 노드 X + 정상 B, C (트래픽 없음): 16 에러에 passive, 32 에 bus-off. B, C 는 REC 32 */
    bus_init(&b);
    int X = bus_add(&b, "X", 0x010), B = bus_add(&b, "B", 0x100), C = bus_add(&b, "C", 0x200);
    b.nd[X].fault = F_TX_CORRUPT; b.nd[X].babble = true;
    for (int i = 0; i < 3; i++) drv_init(&d[i], BR, 0, 0);
    int pas_at = 0;
    for (int s = 0; s < 200 && b.nd[X].fc.state != ST_BUSOFF; s++) {
        bus_step(&b);
        for (int i = 0; i < 3; i++) drv_poll(&d[i], &b.nd[i], b.bits);
        if (!pas_at && b.nd[X].fc.state == ST_PASSIVE) pas_at = (int)b.nd[X].tx_att;
    }
    printf("C2 broken TX: passive at attempt %d, bus-off at attempt %u (%.1f us), B REC %u, C REC %u\n",
           pas_at, (unsigned)b.nd[X].tx_att, us_at(b.nd[X].busoff_at, BR),
           (unsigned)b.nd[B].fc.rec, (unsigned)b.nd[C].fc.rec);
    drv_print(&d[X], "X");
    CHECK(pas_at == 16 && b.nd[X].tx_att == 32 && b.nd[X].fc.state == ST_BUSOFF);
    CHECK(b.nd[B].fc.rec == 32 && b.nd[C].fc.rec == 32 && b.nd[B].fc.state == ST_ACTIVE);
    CHECK(d[X].bus_off == 1 && d[X].error_passive == 1 && d[X].error_warning == 1 && d[X].bus_error == 32);
    CHECK(log_has(&d[X], "ERROR-PASSIVE -> BUS-OFF (tx 256 rx 0)") == 2 && d[B].nlog == 0);
    for (int s = 0; s < 500; s++) bus_step(&b);                 /* auto 아님 + restart 없음 → 영원히 bus-off */
    CHECK(b.nd[X].fc.state == ST_BUSOFF && b.nd[X].fc.n_recovered == 0);

    /* C3. 고장 수신자 R (active 동안 모든 프레임을 먼저 깨뜨림) + 정상 송신자 T + 정상 수신자 C.
     *     rule 2 덕분에 R 이 15 프레임 만에 passive (REC 135) → T 는 TEC 120 에서 멈추고 passive 안 감 */
    bus_init(&b);
    int T = bus_add(&b, "T", 0x100); int R = bus_add(&b, "R", 0x300); C = bus_add(&b, "C", 0x200);
    b.nd[T].babble = true; b.nd[R].fault = F_RX_LOCAL;
    unsigned tmax = 0; int r_pas = 0;
    for (int s = 0; s < 40; s++) {
        bus_step(&b);
        if (b.nd[T].fc.tec > tmax) tmax = b.nd[T].fc.tec;
        if (!r_pas && b.nd[R].fc.state == ST_PASSIVE) r_pas = (int)b.nd[T].tx_att;
    }
    printf("C3 faulty RX: R passive at frame %d (REC after 40 slots %u), T peak TEC %u, T first frame waited %u slots, C REC %u\n",
           r_pas, (unsigned)b.nd[R].fc.rec, tmax, (unsigned)b.nd[T].max_wait, (unsigned)b.nd[C].fc.rec);
    CHECK(r_pas == 15 && tmax == 120 && b.nd[T].fc.n_passive == 0);
    CHECK(b.nd[T].fc.tec == 120 - 25 && b.nd[C].fc.rec == 0 && b.nd[R].fc.rec == 135 + 25);
    CHECK(b.nd[T].tx_ok == 25 && b.nd[T].max_wait == 15);

    /* C4. 수신만 하는 고장 노드는 bus-off 에 못 간다 (5000 프레임) */
    for (int s = 0; s < 5000; s++) bus_step(&b);
    CHECK(b.nd[R].fc.state == ST_PASSIVE && b.nd[R].fc.n_busoff == 0 && b.nd[R].fc.rec == REC_MAX);
    CHECK(b.nd[T].fc.tec == 0 && b.nd[T].fc.state == ST_ACTIVE);

    /* C5. 전역 교란 1회: 송신자 +8, 수신자 +1 → 다음 성공에서 -1 */
    bus_init(&b);
    T = bus_add(&b, "T", 0x100); B = bus_add(&b, "B", 0x200); C = bus_add(&b, "C", 0x300);
    b.nd[T].babble = true;
    b.global_err = true; bus_step(&b);
    CHECK(b.last_out == OUT_GLOBAL && b.nd[T].fc.tec == 8 && b.nd[B].fc.rec == 1 && b.nd[C].fc.rec == 1);
    bus_step(&b);
    CHECK(b.last_out == OUT_OK && b.nd[T].fc.tec == 7 && b.nd[B].fc.rec == 0 && b.nd[C].fc.rec == 0);

    /* C6. 복구: 빈 버스 → 정확히 1408 비트, 꽉 찬 버스 → 정확히 128 프레임 */
    bus_init(&b);
    X = bus_add(&b, "X", 0x010); B = bus_add(&b, "B", 0x100); C = bus_add(&b, "C", 0x200);  /* C = ACK 해 줄 수신자 */
    b.nd[X].fault = F_TX_CORRUPT; b.nd[X].babble = true; b.nd[X].fc.auto_recover = true;
    while (b.nd[X].fc.state != ST_BUSOFF && b.slot < 200) bus_step(&b);
    b.nd[X].babble = false; b.nd[X].qn = 0;                     /* 대기 프레임 폐기, B 는 아직 조용 */
    while (b.nd[X].fc.n_recovered == 0 && b.slot < 1000) bus_step(&b);
    printf("C6 recovery, idle bus: %llu bits after bus-off (%.1f us @500k)\n",
           (unsigned long long)(b.nd[X].recovered_at - b.nd[X].busoff_at),
           us_at(b.nd[X].recovered_at - b.nd[X].busoff_at, BR));
    CHECK(b.nd[X].recovered_at - b.nd[X].busoff_at == 1408);
    b.nd[X].babble = true; b.nd[B].babble = true;               /* X 는 여전히 고장, B 는 연속 송신 */
    while (b.nd[X].fc.n_busoff < 2 && b.slot < 2000) bus_step(&b);
    uint32_t b_ok0 = b.nd[B].tx_ok;
    uint64_t off2 = b.nd[X].busoff_at;
    while (b.nd[X].fc.n_recovered < 2 && b.slot < 5000) bus_step(&b);
    printf("C6 recovery, busy bus: %u B frames, %llu bits (%.1f us @500k)\n",
           (unsigned)(b.nd[B].tx_ok - b_ok0), (unsigned long long)(b.nd[X].recovered_at - off2),
           us_at(b.nd[X].recovered_at - off2, BR));
    CHECK(b.nd[B].tx_ok - b_ok0 == 128 && b.nd[X].fc.n_busoff == 2);
    CHECK(b.nd[X].tx_att == 64 && b.nd[B].arb_lost == 32);       /* 두 번째 에피소드 동안 B 는 32번 중재 패배 */

    /* C6b. 노드 2개짜리 버스: X 가 bus-off 되면 B 는 ACK 해 줄 상대가 없다 → ACK 에러로 passive, 128 에서 멈춤 */
    bus_init(&b);
    X = bus_add(&b, "X", 0x010); B = bus_add(&b, "B", 0x100);
    b.nd[X].fault = F_TX_CORRUPT; b.nd[X].babble = true; b.nd[B].babble = true;
    for (int s = 0; s < 500; s++) bus_step(&b);
    printf("C6b two-node bus: X %s, B %s TEC %u (B ok %u, last outcome ACK error=%d)\n",
           state_name(b.nd[X].fc.state), state_name(b.nd[B].fc.state), (unsigned)b.nd[B].fc.tec,
           (unsigned)b.nd[B].tx_ok, b.last_out == OUT_ACK_ERR);
    CHECK(b.nd[X].fc.state == ST_BUSOFF && b.nd[B].fc.state == ST_PASSIVE && b.nd[B].fc.tec == 128);
    CHECK(b.nd[B].fc.n_busoff == 0 && b.last_out == OUT_ACK_ERR);

    /* C7. babbling idiot (고장 + 최고 우선순위 + 자동 복구) vs 드라이버 정책 (백오프 재시작, 최대 2번) */
    for (int pol = 0; pol < 2; pol++) {
        bus_init(&b);
        X = bus_add(&b, "X", 0x010); B = bus_add(&b, "B", 0x100); C = bus_add(&b, "C", 0x200);
        b.nd[X].fault = F_TX_CORRUPT; b.nd[X].babble = true; b.nd[X].fc.auto_recover = pol == 0;
        b.nd[B].period = 5; b.nd[C].period = 5;
        drv_init(&d[X], BR, pol ? 2 : 0, pol ? 2 : 0);
        drv_init(&d[B], BR, 0, 0); drv_init(&d[C], BR, 0, 0);
        if (pol) drv_log(&d[X], 0, "policy: restart-ms %u (x2 backoff), max restarts %u",
                         (unsigned)d[X].restart_ms, (unsigned)d[X].max_restarts);
        for (int s = 0; s < 20000; s++) {
            bus_step(&b);
            for (int i = 0; i < 3; i++) drv_poll(&d[i], &b.nd[i], b.bits);
        }
        printf("C7 %s: X bus-off %u, recovered %u, attempts %u; B ok %u wait max %u dropped %u; C ok %u wait max %u\n",
               pol ? "driver policy (2 ms backoff, 2 restarts)" : "auto-recover (ABOM-like)",
               (unsigned)b.nd[X].fc.n_busoff, (unsigned)b.nd[X].fc.n_recovered, (unsigned)b.nd[X].tx_att,
               (unsigned)b.nd[B].tx_ok, (unsigned)b.nd[B].max_wait, (unsigned)b.nd[B].dropped,
               (unsigned)b.nd[C].tx_ok, (unsigned)b.nd[C].max_wait);
        CHECK(b.nd[X].tx_ok == 0 && b.nd[X].tx_att - 32 * b.nd[X].fc.n_busoff < 32);
        CHECK(b.nd[B].dropped == 0 && b.nd[C].dropped == 0 && b.nd[B].tx_ok + b.nd[B].qn == 20000 / 5);
        if (pol == 0) CHECK(b.nd[X].fc.n_busoff > 10 && b.nd[X].fc.n_recovered + 1 >= b.nd[X].fc.n_busoff);
        else {
            drv_print(&d[X], "X");
            CHECK(b.nd[X].fc.n_busoff == 3 && b.nd[X].fc.n_recovered == 2 && d[X].restarts == 2);
            CHECK(d[X].last == L_STOPPED && b.nd[X].fc.state == ST_BUSOFF && d[X].bus_off == 3);
            CHECK(log_has(&d[X], "restart in 2 ms") >= 0 && log_has(&d[X], "restart in 4 ms") >= 0 &&
                  log_has(&d[X], "giving up") >= 0);
        }
    }

    /* C8. 멀쩡한 프레임으로 떠드는 노드: 에러가 없으니 fault confinement 는 아무것도 못 한다 */
    bus_init(&b);
    X = bus_add(&b, "X", 0x010); B = bus_add(&b, "B", 0x100);
    b.nd[X].babble = true; b.nd[B].period = 3;
    for (int s = 0; s < 999; s++) bus_step(&b);
    printf("C8 valid babbler: X ok %u TEC %u; B ok %u arb lost %u dropped %u\n",
           (unsigned)b.nd[X].tx_ok, (unsigned)b.nd[X].fc.tec, (unsigned)b.nd[B].tx_ok,
           (unsigned)b.nd[B].arb_lost, (unsigned)b.nd[B].dropped);
    CHECK(b.nd[X].tx_ok == 999 && b.nd[X].fc.tec == 0 && b.nd[B].tx_ok == 0 && b.nd[B].dropped == 333 - QCAP);

    /* C9. ip 형식 출력 + 버퍼 부족 */
    Drv dd; drv_init(&dd, BR, 100, 0);
    FcNode ff = {0}; ff.tec = 128; ff.rec = 3; ff.state = ST_PASSIVE;
    dd.last = L_PASSIVE; dd.bus_error = 16; dd.error_warning = 1; dd.error_passive = 1; dd.arbitration_lost = 2;
    char ip[256];
    int len = drv_format_ip(&dd, &ff, ip, sizeof ip);
    printf("%s", ip);
    const char *ip1 = "can state ERROR-PASSIVE (berr-counter tx 128 rx 3) restart-ms 100\n";
    CHECK(len > 0 && strncmp(ip, ip1, strlen(ip1)) == 0);
    CHECK(strstr(ip, "  0          16         2          1          1          0\n") != NULL);
    char small[20];
    CHECK(drv_format_ip(&dd, &ff, small, sizeof small) == -1 && strlen(small) == sizeof small - 1);
    CHECK(drv_format_ip(&dd, &ff, NULL, 10) == -1);
    ff.tec = 95; CHECK(lstate_of(&ff) == L_ACTIVE);
    ff.tec = 96; CHECK(lstate_of(&ff) == L_WARNING);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
