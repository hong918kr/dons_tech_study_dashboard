/* COMM-M07 추가 연습 — 한 번에 검증
 * 연습 1: 주기 패턴 (에러 E 번, 성공 S 번) 전수 → "결국 bus-off ⇔ 8E > S" (길이 1..12 전부, 8190 패턴)
 * 연습 2: STM32 bxCAN CAN_ESR 디코더 (TEC/REC/LEC/BOFF/EPVF/EWGF — 비트 위치는 RM 확인 권장) + 일관성 검사
 * 연습 3: 버스 부하와 bus-off 복구 시간: 프레임 사이 빈 비트 g → 필요한 프레임 수 = ceil(128 / (1 + g/11)),
 *         "센 뒤 0부터" 대신 슬라이딩 윈도로 세는 버그 재현
 * 연습 4: 에러 플래그 중첩 길이 6..12 비트, 에러 프레임 14..20 비트, rule 2 를 받는 노드 = 플래그가 먼저 끝난 수신자
 * (본문 solution.c 의 Part A/B 코드를 그대로 포함 — 레슨 간 #include 없음)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
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
    if (tec >= BUSOFF_LIMIT) return ST_BUSOFF;
    if (tec >= PASSIVE_LIMIT || rec >= PASSIVE_LIMIT) return ST_PASSIVE;
    return ST_ACTIVE;
}

/* 카운터가 바뀐 뒤 상태 갱신. bus-off 는 카운터가 아니라 복구 절차만이 끝낸다. */
static void fc_update(FcNode *n) {
    if (n->state == ST_BUSOFF) return;
    CanState s = fc_state_of(n->tec, n->rec);
    if (s == n->state) return;
    if (s == ST_PASSIVE && n->state == ST_ACTIVE) n->n_passive++;
    if (s == ST_BUSOFF) {
        n->n_busoff++;
        n->recov_run = 0;
        n->recov_occ = 0;
        n->recov_armed = n->auto_recover;
    }
    n->state = s;
}

/* 송신자가 에러 감지 (rule 3: TEC += 8) — 예외 2개 (Part B):
 *  exc1: error-passive 송신자의 ACK 에러이고, passive error flag 동안 dominant 를 못 봤으면 그대로
 *  exc2: 중재 구간의 recessive stuff bit 를 dominant 로 읽은 stuff error 면 그대로 */
static void fc_tx_error(FcNode *n, ErrType e, bool dom_in_pflag, bool arb_rec_stuff) {
    if (n->state == ST_BUSOFF) return;
    bool exc1 = e == ERR_ACK && n->state == ST_PASSIVE && !dom_in_pflag;
    bool exc2 = e == ERR_STUFF && arb_rec_stuff;
    if (!exc1 && !exc2) n->tec = (uint16_t)(n->tec + 8);
    fc_update(n);
}

/* 수신자가 에러 감지 (rule 1: REC += 1) */
static void fc_rx_error(FcNode *n) {
    if (n->state == ST_BUSOFF) return;
    n->rec = (uint16_t)(n->rec + 1 > REC_MAX ? REC_MAX : n->rec + 1);
    fc_update(n);
}

/* +8 벌점: rule 2 (수신자: 자기 에러 플래그 뒤 첫 비트가 dominant), rule 4/5 (active 플래그 송신 중 bit error),
 * rule 6 (플래그 뒤 dominant 8개마다). is_tx 면 TEC, 아니면 REC. */
static void fc_penalty8(FcNode *n, bool is_tx) {
    if (n->state == ST_BUSOFF) return;
    if (is_tx) n->tec = (uint16_t)(n->tec + 8);
    else n->rec = (uint16_t)(n->rec + 8 > REC_MAX ? REC_MAX : n->rec + 8);
    fc_update(n);
}

/* 성공 송신 (ACK 받고 EOF 까지 에러 없음): TEC -= 1 (0 이면 그대로) */
static void fc_tx_ok(FcNode *n) {
    if (n->state == ST_BUSOFF) return;
    if (n->tec > 0) n->tec--;
    fc_update(n);
}

/* 성공 수신 (ACK slot 까지 에러 없고 ACK 를 보냄): REC 1..127 → -1, >127 → REC_PASSIVE_RESET */
static void fc_rx_ok(FcNode *n) {
    if (n->state == ST_BUSOFF) return;
    if (n->rec > 127) n->rec = REC_PASSIVE_RESET;
    else if (n->rec > 0) n->rec--;
    fc_update(n);
}

/* ---------------- Part B: bus-off 복구 + 에러 플래그 중첩 ---------------- */

/* host 가 복구를 요청 (auto_recover 가 아닐 때). 요청 시점부터 센다. */
static void fc_request_recovery(FcNode *n) {
    if (n->state != ST_BUSOFF || n->recov_armed) return;
    n->recov_armed = true;
    n->recov_run = 0;
    n->recov_occ = 0;
}

/* bus-off 노드가 버스 비트 하나를 본다. 11 연속 recessive = 1회 (센 뒤 0 부터 다시), 128회면 복구.
 * 반환 true = 이 비트에서 error-active 로 복귀 (TEC = REC = 0). */
static bool fc_recovery_bit(FcNode *n, int bit) {
    if (n->state != ST_BUSOFF || !n->recov_armed) return false;
    if (!bit) { n->recov_run = 0; return false; }
    if (++n->recov_run < RECOV_RUN) return false;
    n->recov_run = 0;
    if (++n->recov_occ < RECOV_OCC) return false;
    n->state = ST_ACTIVE;
    n->tec = n->rec = 0;
    n->recov_armed = false;
    n->recov_occ = 0;
    n->n_recovered++;
    return true;
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
    FsPhase ph[FS_MAXN];
    int cnt[FS_MAXN];
    int domrun = 0, prev_bus = 1, t;
    if (k > FS_MAXN) k = FS_MAXN;
    if (wcap) wave[0] = '\0';
    for (int i = 0; i < k; i++) {
        out[i] = (FsOut){ -1, -1, -1, 0, 0 };
        ph[i] = FS_WAIT; cnt[i] = 0;
        if (nd[i].detect == 0) {
            ph[i] = FS_FLAG; out[i].flag_start = 0;
            if (nd[i].is_tx) out[i].tec_d += 8; else out[i].rec_d += 1;
        }
    }
    for (t = 0; t < FS_MAXT; t++) {
        int bus = t < stuck_len ? 0 : 1;
        for (int i = 0; i < k; i++)
            if (ph[i] == FS_FLAG && !nd[i].passive) bus = 0;
        if ((size_t)t + 1 < wcap) { wave[t] = (char)('0' + bus); wave[t + 1] = '\0'; }
        domrun = bus ? 0 : domrun + 1;
        bool busy = t + 1 < stuck_len;
        for (int i = 0; i < k; i++) {
            switch (ph[i]) {
            case FS_WAIT:
                if ((nd[i].detect > 0 && t + 1 == nd[i].detect) || (nd[i].detect < 0 && domrun == 6)) {
                    ph[i] = FS_FLAG; cnt[i] = 0; out[i].flag_start = t + 1;
                    if (nd[i].is_tx) out[i].tec_d += 8; else out[i].rec_d += 1;
                }
                break;
            case FS_FLAG:
                if (!nd[i].passive) cnt[i]++;
                else cnt[i] = (t == out[i].flag_start || bus != prev_bus) ? 1 : cnt[i] + 1;
                if (cnt[i] == 6) { ph[i] = FS_AFTER; out[i].flag_end = t; cnt[i] = 0; }
                break;
            case FS_AFTER:
                if (bus) { ph[i] = FS_DELIM; cnt[i] = 1; break; }
                cnt[i]++;
                if (cnt[i] == 1 && !nd[i].is_tx) out[i].rec_d += 8;           /* rule 2 */
                if (cnt[i] % 8 == 0) { if (nd[i].is_tx) out[i].tec_d += 8; else out[i].rec_d += 8; } /* rule 6 */
                break;
            case FS_DELIM:
                if (bus && ++cnt[i] == 8) { ph[i] = FS_DONE; out[i].delim_end = t; }
                break;
            case FS_DONE:
                break;
            }
            if (ph[i] == FS_FLAG || ph[i] == FS_AFTER || ph[i] == FS_DELIM ||
                (ph[i] == FS_WAIT && nd[i].detect > t + 1)) busy = true;
        }
        prev_bus = bus;
        if (!busy) { t++; break; }
    }
    return t;
}


/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs_state = 0x7E57C0DEu;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}

/* ---------------- 연습 1: 주기 패턴 → 결국 bus-off 인가 ---------------- */
/* pat 의 비트 0..len-1 을 차례로 (1 = 송신 에러, 0 = 성공 송신) 무한 반복. bus-off 면 그때까지의 이벤트 수, 아니면 0 */
static long periodic_busoff(uint32_t pat, int len, int max_periods) {
    FcNode f = {0};
    long ev = 0;
    for (int p = 0; p < max_periods; p++)
        for (int i = 0; i < len; i++) {
            ev++;
            if (pat >> i & 1) fc_tx_error(&f, ERR_BIT, false, false); else fc_tx_ok(&f);
            if (f.state == ST_BUSOFF) return ev;
        }
    return 0;
}

/* 수신자 버전: 1 = 혼자 먼저 본 로컬 에러 (REC +1 +8, rule 2), 0 = 성공 수신. passive 에 닿으면 이벤트 수, 아니면 0 */
static long periodic_rx_passive(uint32_t pat, int len, int max_periods) {
    FcNode f = {0};
    long ev = 0;
    for (int p = 0; p < max_periods; p++)
        for (int i = 0; i < len; i++) {
            ev++;
            if (pat >> i & 1) { fc_rx_error(&f); fc_penalty8(&f, false); } else fc_rx_ok(&f);
            if (f.state == ST_PASSIVE) return ev;
        }
    return 0;
}

/* ---------------- 연습 2: bxCAN CAN_ESR ---------------- */
/* 대표 레이아웃 (RM0090 등, RM 확인 권장): REC[31:24] TEC[23:16] LEC[6:4] BOFF[2] EPVF[1] EWGF[0] */
typedef struct { uint8_t tec, rec, lec; bool boff, epvf, ewgf; } Esr;
static const char *const LEC_NAME[8] = { "none", "stuff", "form", "ack", "bit-recessive", "bit-dominant", "crc", "sw" };
static Esr esr_decode(uint32_t v) {
    Esr e;
    e.rec = (uint8_t)(v >> 24); e.tec = (uint8_t)(v >> 16); e.lec = (uint8_t)(v >> 4 & 7u);
    e.boff = v >> 2 & 1u; e.epvf = v >> 1 & 1u; e.ewgf = v & 1u;
    return e;
}
static uint32_t esr_encode(const Esr *e) {
    return (uint32_t)e->rec << 24 | (uint32_t)e->tec << 16 | (uint32_t)(e->lec & 7u) << 4 |
           (uint32_t)e->boff << 2 | (uint32_t)e->epvf << 1 | (uint32_t)e->ewgf;
}
/* 플래그 우선: BOFF > EPVF > EWGF. (TEC 는 8비트라 bus-off 의 "256" 을 못 담는다 → BOFF 비트로만 안다) */
static const char *esr_state(const Esr *e) {
    return e->boff ? "BUS-OFF" : e->epvf ? "ERROR-PASSIVE" : e->ewgf ? "ERROR-WARNING" : "ERROR-ACTIVE";
}
/* bus-off 가 아니면 EWGF == (max >= 96), EPVF == (max >= 128) 이어야 한다 */
static bool esr_consistent(const Esr *e) {
    if (e->boff) return true;
    unsigned m = e->tec > e->rec ? e->tec : e->rec;
    return e->ewgf == (m >= 96) && e->epvf == (m >= 128);
}
static int esr_format(uint32_t v, char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
    Esr e = esr_decode(v);
    int n = snprintf(buf, cap, "%s TEC %u REC %u LEC=%s%s", esr_state(&e), e.tec, e.rec, LEC_NAME[e.lec],
                     esr_consistent(&e) ? "" : " (INCONSISTENT)");
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

/* ---------------- 연습 3: 복구에 필요한 프레임 수 ---------------- */
/* 정상 프레임 하나 (SOF + 스터핑된 본문 + CRC delim + ACK + ACK delim/EOF/IFS 11 rec) + 빈 비트 g 개를 비트 단위로 먹인다.
 * 반환 = 복구까지 필요했던 프레임 수 (최대 max_frames, 못 하면 -1) */
static int frames_to_recover(FcNode *f, const int *gaps, int max_frames, long *bits_out) {
    long bits = 0;
    uint32_t x = 0x1234567u;
    for (int fr = 1; fr <= max_frames; fr++) {
        int last = 0, run = 1;
        bits++; if (fc_recovery_bit(f, 0)) goto done;                  /* SOF */
        for (int i = 0; i < 97; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            int bit = (int)(x >> 31);
            bits++; if (fc_recovery_bit(f, bit)) goto done;
            run = bit == last ? run + 1 : 1; last = bit;
            if (run == 5) { bits++; if (fc_recovery_bit(f, !bit)) goto done; last = !bit; run = 1; }
        }
        bits++; if (fc_recovery_bit(f, 1)) goto done;                  /* CRC delim */
        bits++; if (fc_recovery_bit(f, 0)) goto done;                  /* ACK */
        for (int i = 0; i < 11 + gaps[fr - 1]; i++) { bits++; if (fc_recovery_bit(f, 1)) goto done; }
        continue;
    done:
        *bits_out = bits;
        return fr;
    }
    *bits_out = bits;
    return -1;
}
/* 버그 버전: run 을 0 으로 안 돌리고 "run >= 11 이면 매 비트 +1" (슬라이딩 윈도) */
static int buggy_bits_on_idle(void) {
    int run = 0, occ = 0;
    for (int b = 1; b < 5000; b++) {
        run++;
        if (run >= 11 && ++occ == 128) return b;
    }
    return -1;
}

/* ---------------- 연습 4: 중첩 길이 전수 ---------------- */

int main(void) {
    /* 연습 1 */
    long mism = 0, n_off = 0, n_bounded = 0, max_ev = 0;
    for (int len = 1; len <= 12; len++)
        for (uint32_t pat = 1; pat < (1u << len); pat++) {
            int E = __builtin_popcount(pat), S = len - E;
            long ev = periodic_busoff(pat, len, 300);
            bool pred = 8 * E > S;
            mism += (ev != 0) != pred;
            if (ev) { n_off++; if (ev > max_ev) max_ev = ev; } else n_bounded++;
        }
    printf("ex1: 8190 patterns, bus-off %ld, bounded %ld, mismatches vs 8E>S = %ld, slowest bus-off after %ld events\n",
           n_off, n_bounded, mism, max_ev);
    CHECK(mism == 0);
    CHECK(periodic_busoff(0x1u, 9, 1000) == 0 && periodic_busoff(0x1u, 8, 1000) != 0);   /* 1/9 은 버티고 1/8 은 못 버팀 */
    CHECK(periodic_busoff(0x1u, 8, 1000) == 8L * 249 - 7);                             /* E + 7S: 249번째 에러에서 */
    long mism_rx = 0;
    for (int len = 1; len <= 12; len++)
        for (uint32_t pat = 1; pat < (1u << len); pat++) {
            int E = __builtin_popcount(pat), S = len - E;
            mism_rx += (periodic_rx_passive(pat, len, 300) != 0) != (9 * E > S);
        }
    printf("ex1: receiver (primary +9 / ok -1): mismatches vs 9E>S = %ld\n", mism_rx);
    CHECK(mism_rx == 0);

    /* 연습 2 */
    char s[96];
    Esr e = { 128, 3, 3, false, true, true };
    uint32_t v = esr_encode(&e);
    printf("ex2: ESR 0x%08X -> ", (unsigned)v);
    esr_format(v, s, sizeof s); printf("%s\n", s);
    CHECK(v == 0x03800033u && strcmp(s, "ERROR-PASSIVE TEC 128 REC 3 LEC=ack") == 0);
    CHECK(esr_format(0x00000000u, s, sizeof s) > 0 && strcmp(s, "ERROR-ACTIVE TEC 0 REC 0 LEC=none") == 0);
    CHECK(esr_format(0x00F80065u, s, sizeof s) > 0 && strcmp(s, "BUS-OFF TEC 248 REC 0 LEC=crc") == 0);
    CHECK(esr_format(0x00800002u, s, sizeof s) > 0 && strstr(s, "(INCONSISTENT)") != NULL);  /* EPVF 인데 EWGF 없음 */
    CHECK(esr_format(0x00800033u, s, 8) == -1 && strlen(s) == 7);
    long bad2 = 0;
    for (unsigned tec = 0; tec < 256; tec++)
        for (unsigned rec = 0; rec < 256; rec++) {
            unsigned m = tec > rec ? tec : rec;
            Esr a = { (uint8_t)tec, (uint8_t)rec, (uint8_t)((tec ^ rec) & 7u), false, m >= 128, m >= 96 };
            Esr b = esr_decode(esr_encode(&a));
            const char *want = fc_state_of(tec, rec) == ST_PASSIVE ? "ERROR-PASSIVE" : m >= 96 ? "ERROR-WARNING" : "ERROR-ACTIVE";
            bad2 += memcmp(&a, &b, sizeof a) != 0 || !esr_consistent(&b) || strcmp(esr_state(&b), want) != 0;
        }
    CHECK(bad2 == 0);

    /* 연습 3 */
    int gaps[200];
    long bits;
    printf("ex3: gap g -> frames to recover (formula)\n    ");
    long bad3 = 0;
    for (int g = 0; g <= 40; g++) {
        for (int i = 0; i < 200; i++) gaps[i] = g;
        FcNode f = {0}; f.tec = 300; f.state = ST_BUSOFF; fc_request_recovery(&f);
        int fr = frames_to_recover(&f, gaps, 200, &bits);
        int q = 1 + g / 11, want = (128 + q - 1) / q;
        if (g % 5 == 0 || g == 11 || g == 22) printf("g=%d:%d(%d) ", g, fr, want);
        bad3 += fr != want || f.state != ST_ACTIVE;
    }
    printf("\n");
    CHECK(bad3 == 0);
    for (int trial = 0; trial < 2000; trial++) {          /* 랜덤 간격: 예측 = 누적 floor((11+g)/11) 가 128 에 닿는 프레임 */
        int sum = 0, want = -1;
        for (int i = 0; i < 200; i++) {
            gaps[i] = (int)(xorshift32() % 50);
            sum += (11 + gaps[i]) / 11;
            if (want < 0 && sum >= 128) want = i + 1;
        }
        FcNode f = {0}; f.tec = 300; f.state = ST_BUSOFF; fc_request_recovery(&f);
        bad3 += frames_to_recover(&f, gaps, 200, &bits) != want;
    }
    CHECK(bad3 == 0);
    int bug = buggy_bits_on_idle();
    printf("ex3: idle bus: correct 1408 bits, sliding-window bug recovers after %d bits\n", bug);
    CHECK(bug == 138);

    /* 연습 4 */
    int lo = 99, hi = 0, flo = 99, fhi = 0, n4 = 0;
    long bad4 = 0;
    FsNode nd[3]; FsOut out[3]; char wave[FS_MAXT + 1];
    for (int a = -1; a <= 5; a++)
        for (int b = -1; b <= 5; b++)
            for (int c = -1; c <= 5; c++) {
                if (a != 0 && b != 0 && c != 0) continue;     /* 누군가는 비트 0 에서 처음 본다 */
                nd[0] = (FsNode){ true, false, a }; nd[1] = (FsNode){ false, false, b }; nd[2] = (FsNode){ false, false, c };
                int nb = flag_superpose(nd, 3, 0, out, wave, sizeof wave);
                int dom = (int)strspn(wave, "0"), last_end = 0;
                for (int i = 0; i < 3; i++) if (out[i].flag_end > last_end) last_end = out[i].flag_end;
                if (dom < lo) lo = dom;
                if (dom > hi) hi = dom;
                if (nb < flo) flo = nb;
                if (nb > fhi) fhi = nb;
                bad4 += out[0].tec_d != 8 || nb != dom + 8 || dom != last_end + 1;
                for (int i = 1; i < 3; i++) bad4 += out[i].rec_d != 1 + 8 * (out[i].flag_end < last_end);
                n4++;
            }
    printf("ex4: %d combos, dominant run %d..%d bits, error frame (flags+delimiter) %d..%d bits, state %s\n",
           n4, lo, hi, flo, fhi, state_name(ST_ACTIVE));
    CHECK(bad4 == 0 && lo == 6 && hi == 12 && flo == 14 && fhi == 20);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
