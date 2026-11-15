/* COMM-M08 demo: Classic CAN vs CAN FD 프레임 시간 · 처리량 · 효율 표 + 로봇 관절 버스 부하
 *
 * 계산기 코드는 solution.c 의 Part A~C 함수를 그대로 복사했다 (자기 완결 파일).
 *   표 1: 필드별 비트 수 (nominal / data 위상)
 *   표 2: Classic 8 B vs FD 8/16/64 B, 여러 (중재/데이터) 비트레이트 — 스터핑 없음 / 랜덤 데이터 실측 평균 / 최악 상한
 *   표 3: 1 kHz × N 모터 × 16 B (명령 + 상태) 로봇 관절 버스 — 설정별 최대 모터 수 (최악 스터핑)
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_fd_table.c -o demo_fd_table && ./demo_fd_table
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    CAN_OK = 0, CAN_EARG = -1, CAN_ELEN = -2, CAN_EFLAGS = -3, CAN_ERTR_FD = -4, CAN_EID = -5,
    CAN_ECAP = -6, CAN_ERATE = -7, CAN_ERES = -8, CAN_ESHORT = -9, CAN_EFORM = -10
};

typedef struct {
    uint32_t id;        /* 표준 11비트 또는 확장 29비트 */
    uint8_t  ide;       /* 1 = 확장 ID */
    uint8_t  rtr;       /* 1 = remote frame (Classic 전용) */
    uint8_t  fdf;       /* 1 = CAN FD 프레임 */
    uint8_t  brs;       /* 1 = 데이터 위상에서 비트레이트 전환 (FD 전용) */
    uint8_t  esi;       /* 1 = 송신 노드가 error-passive (FD 전용) */
    uint8_t  len;       /* 데이터 바이트 수. remote 면 요청 길이 (선 위에 데이터 없음) */
    uint8_t  len8_dlc;  /* Classic 에서 len == 8 일 때 선 위 DLC 9~15 를 쓰고 싶으면 (Linux 와 같은 뜻), 아니면 0 */
    uint8_t  data[64];
} can_msg;

/* ================= Part A: DLC ↔ 길이, 패딩, 헤더 비트 ================= */

/* FD: DLC 0~15 → 0~8, 12, 16, 20, 24, 32, 48, 64. dlc 의 하위 4비트만 본다. */
static unsigned canfd_dlc2len(uint8_t dlc) {
    static const uint8_t t[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };
    return t[dlc & 0x0F];
}

/* Classic: DLC 9~15 도 8바이트. */
static unsigned can_cc_dlc2len(uint8_t dlc) {
    dlc &= 0x0F;
    return dlc > 8 ? 8u : dlc;
}

/* FD: 길이 len 을 담는 가장 작은 DLC. len > 64 → -1. */
static int canfd_len2dlc(unsigned len) {
    if (len > 64) return -1;
    if (len <= 8) return (int)len;
    int d = 9;
    while (canfd_dlc2len((uint8_t)d) < len) d++;
    return d;
}

/* len 바이트를 FD 길이로 패딩해 out 에 쓴다 (out_cap 바이트 공간). 반환: 패딩된 길이 또는 오류.
 * len > 64 → CAN_ELEN, out_cap < 패딩된 길이 → CAN_ECAP (out 은 건드리지 않는다). */
static int canfd_pad(const uint8_t *data, unsigned len, uint8_t pad, uint8_t *out, size_t out_cap, uint8_t *dlc_out) {
    if ((!data && len) || !out || !dlc_out) return CAN_EARG;
    int dlc = canfd_len2dlc(len);
    if (dlc < 0) return CAN_ELEN;
    unsigned plen = canfd_dlc2len((uint8_t)dlc);
    if (out_cap < plen) return CAN_ECAP;
    if (len) memmove(out, data, len);
    memset(out + len, pad, plen - len);
    *dlc_out = (uint8_t)dlc;
    return (int)plen;
}

/* 메시지 검사 + 선 위 DLC. 플래그 규칙:
 *   Classic (fdf=0): brs/esi 는 0 이어야 (CAN_EFLAGS), len ≤ 8 (CAN_ELEN), len8_dlc 는 0 또는 (len==8 && 9..15)
 *   FD (fdf=1): rtr=0 (CAN_ERTR_FD), len 은 DLC 표에 있는 값 (패딩 안 된 13 등 → CAN_ELEN)
 *   ID: 표준 ≤ 0x7FF, 확장 ≤ 0x1FFFFFFF (CAN_EID) */
static int can_msg_check(const can_msg *m, uint8_t *dlc) {
    if (!m) return CAN_EARG;
    if (m->id > (m->ide ? 0x1FFFFFFFu : 0x7FFu)) return CAN_EID;
    if (!m->fdf) {
        if (m->brs || m->esi) return CAN_EFLAGS;
        if (m->len > 8) return CAN_ELEN;
        if (m->len8_dlc) {
            if (m->len != 8 || m->len8_dlc < 9 || m->len8_dlc > 15) return CAN_ELEN;
            *dlc = m->len8_dlc;
        } else {
            *dlc = m->len;
        }
        return CAN_OK;
    }
    if (m->rtr) return CAN_ERTR_FD;
    int d = canfd_len2dlc(m->len);
    if (d < 0 || canfd_dlc2len((uint8_t)d) != m->len) return CAN_ELEN;
    *dlc = (uint8_t)d;
    return CAN_OK;
}

static size_t put_bits(uint8_t *b, size_t n, uint32_t v, int w) {   /* MSB first */
    for (int i = w - 1; i >= 0; i--) b[n++] = (uint8_t)((v >> i) & 1u);
    return n;
}

static uint32_t get_bits(const uint8_t *b, size_t *pos, int w) {
    uint32_t v = 0;
    for (int i = 0; i < w; i++) v = (v << 1) | (b[(*pos)++] & 1u);
    return v;
}

/* 헤더 = SOF 부터 DLC 끝까지 (데이터 직전). 스터핑 전 비트. 반환: 비트 수 (19/39/22/41) 또는 오류.
 *   Classic 표준: SOF ID[10:0] RTR IDE(0) r0(0) DLC
 *   Classic 확장: SOF ID[28:18] SRR(1) IDE(1) ID[17:0] RTR r1(0) r0(0) DLC
 *   FD 표준:      SOF ID[10:0] RRS(0) IDE(0) FDF(1) res(0) BRS ESI DLC
 *   FD 확장:      SOF ID[28:18] SRR(1) IDE(1) ID[17:0] RRS(0) FDF(1) res(0) BRS ESI DLC */
static int can_hdr_encode(const can_msg *m, uint8_t *bits, size_t cap) {
    uint8_t dlc = 0;
    if (!bits) return CAN_EARG;
    int rc = can_msg_check(m, &dlc);
    if (rc) return rc;
    size_t need = m->ide ? (m->fdf ? 41u : 39u) : (m->fdf ? 22u : 19u);
    if (cap < need) return CAN_ECAP;
    size_t n = 0;
    bits[n++] = 0;                                              /* SOF */
    if (m->ide) {
        n = put_bits(bits, n, m->id >> 18, 11);                 /* ID-A */
        bits[n++] = 1; bits[n++] = 1;                           /* SRR, IDE */
        n = put_bits(bits, n, m->id & 0x3FFFFu, 18);            /* ID-B */
        if (m->fdf) { bits[n++] = 0; bits[n++] = 1; bits[n++] = 0; }        /* RRS FDF res */
        else        { bits[n++] = m->rtr ? 1 : 0; bits[n++] = 0; bits[n++] = 0; } /* RTR r1 r0 */
    } else {
        n = put_bits(bits, n, m->id, 11);
        if (m->fdf) { bits[n++] = 0; bits[n++] = 0; bits[n++] = 1; bits[n++] = 0; } /* RRS IDE FDF res */
        else        { bits[n++] = m->rtr ? 1 : 0; bits[n++] = 0; bits[n++] = 0; }  /* RTR IDE r0 */
    }
    if (m->fdf) { bits[n++] = m->brs ? 1 : 0; bits[n++] = m->esi ? 1 : 0; }
    n = put_bits(bits, n, dlc, 4);
    return (int)n;
}

/* 헤더 비트 → 메시지 필드 (데이터는 채우지 않는다). 반환: 소비한 비트 수 또는 오류.
 * SOF ≠ 0 → CAN_EFORM, 비트 부족 → CAN_ESHORT, FD 의 res = 1 → CAN_ERES (ISO 의 protocol exception 대상,
 * 이 모델은 오류로 돌려준다). RRS / SRR / r1 / r0 값은 수신 측이 받아들인다 (검사 안 함 — ISO 확인 권장).
 * Classic: len = min(dlc, 8), dlc 9~15 면 len8_dlc 에 원래 값. FD: len = 표. */
static int can_hdr_decode(const uint8_t *bits, size_t n, can_msg *m) {
    if (!bits || !m) return CAN_EARG;
    if (n < 19) return CAN_ESHORT;
    if (bits[0] != 0) return CAN_EFORM;
    memset(m, 0, sizeof *m);
    size_t p = 1;
    uint32_t ida = get_bits(bits, &p, 11);
    uint8_t b12 = bits[p++];                       /* RTR/RRS (표준) 또는 SRR (확장) */
    uint8_t ide = bits[p++];
    uint8_t rtr_rrs;
    if (ide) {
        if (n < 39) return CAN_ESHORT;
        uint32_t idb = get_bits(bits, &p, 18);
        m->id = (ida << 18) | idb;
        m->ide = 1;
        rtr_rrs = bits[p++];
    } else {
        m->id = ida;
        rtr_rrs = b12;
    }
    uint8_t fdf = bits[p++];                      /* Classic 에서는 r0(표준) / r1(확장) 자리 */
    if (fdf) {
        if (n < (size_t)(ide ? 41 : 22)) return CAN_ESHORT;
        if (bits[p++] != 0) return CAN_ERES;
        m->fdf = 1;
        m->brs = bits[p++];
        m->esi = bits[p++];
        uint8_t dlc = (uint8_t)get_bits(bits, &p, 4);
        m->len = (uint8_t)canfd_dlc2len(dlc);
        (void)rtr_rrs;
    } else {
        if (ide) p++;                             /* 확장 Classic: r1 다음 r0 */
        m->rtr = rtr_rrs;
        uint8_t dlc = (uint8_t)get_bits(bits, &p, 4);
        m->len = (uint8_t)can_cc_dlc2len(dlc);
        if (dlc > 8) m->len8_dlc = dlc;
    }
    return (int)p;
}

/* ================= Part B: 필드별 비트 수, 시간 계산기, 온-와이어 인코더 ================= */
typedef struct {
    int hdr;        /* SOF..DLC */
    int data;       /* 선 위 데이터 비트 (remote = 0) */
    int sbc;        /* stuff count (FD 4, Classic 0) */
    int fsb;        /* 고정 스터프 비트 (FD 6/7, Classic 0) */
    int crc;        /* CRC 시퀀스 15/17/21 */
    int tail;       /* CRC delim 1 + ACK slot 1 + ACK delim 1 + EOF 7 = 10 */
    int ifs;        /* intermission 3 */
    int dyn;        /* 동적 스터핑 구간 길이 (스터핑 전): Classic = hdr+data+crc, FD = hdr+data */
    int dyn_nom;    /* 그중 중재 속도로 나가는 비트 앞에 붙는 스터프 비트의 "앞 비트" 구간 = BRS 앞까지 */
    int nom;        /* 스터프 제외 nominal 속도 비트 수 */
    int dat;        /* 스터프 제외 data 속도 비트 수 (BRS=0 이거나 Classic 이면 0) */
} can_layout;

static int can_layout_of(const can_msg *m, can_layout *L) {
    uint8_t dlc = 0;
    if (!L) return CAN_EARG;
    int rc = can_msg_check(m, &dlc);
    if (rc) return rc;
    memset(L, 0, sizeof *L);
    L->hdr  = m->ide ? (m->fdf ? 41 : 39) : (m->fdf ? 22 : 19);
    L->data = m->rtr ? 0 : 8 * m->len;
    L->tail = 10;
    L->ifs  = 3;
    if (!m->fdf) {
        L->crc = 15;
        L->dyn = L->hdr + L->data + L->crc;
        L->dyn_nom = L->dyn;
        L->nom = L->hdr + L->data + L->crc + L->tail + L->ifs;
        return CAN_OK;
    }
    L->sbc = 4;
    L->crc = m->len <= 16 ? 17 : 21;
    L->fsb = 1 + (L->sbc + L->crc) / 4;
    L->dyn = L->hdr + L->data;
    int total = L->hdr + L->data + L->sbc + L->fsb + L->crc + L->tail + L->ifs;
    if (!m->brs) { L->dyn_nom = L->dyn; L->nom = total; L->dat = 0; return CAN_OK; }
    int pre = m->ide ? 36 : 17;                 /* SOF..BRS */
    L->dyn_nom = pre - 1;                         /* 스터프 비트가 BRS 앞에 끼려면 앞 비트가 BRS 이전 */
    L->nom = pre + (L->tail - 1) + L->ifs;        /* + ACK slot, ACK delim, EOF, IFS */
    L->dat = total - L->nom;                      /* ESI..CRC delim */
    return CAN_OK;
}

typedef struct { uint32_t nom_bps, data_bps; } can_rates;
typedef struct { int bits_nom, bits_dat; double us; } can_time;

enum { T_NOSTUFF = 0, T_WORST = 1 };

/* 길이 k 인 구간에서 동적 스터프 비트의 상한 ⌊(k−1)/4⌋ (k ≤ 0 → 0). */
static int stuff_max(int k) { return k > 1 ? (k - 1) / 4 : 0; }

/* 전송 시간. mode = T_NOSTUFF (스터프 0, 하한) / T_WORST (상한):
 *   nominal 비트 = nom + ⌊(dyn_nom − 1)/4⌋,  data 비트 = dat + ⌊(dyn − 1)/4⌋ − ⌊(dyn_nom − 1)/4⌋
 *   (BRS=0 / Classic: dyn_nom = dyn → 전부 nominal)
 * 오류: nom_bps = 0, FD+BRS 인데 data_bps = 0 또는 < nom_bps → CAN_ERATE. 메시지 오류는 그대로. */
static int can_frame_time(const can_msg *m, const can_rates *r, int mode, can_time *t) {
    can_layout L;
    if (!r || !t) return CAN_EARG;
    int rc = can_layout_of(m, &L);
    if (rc) return rc;
    if (r->nom_bps == 0) return CAN_ERATE;
    if (L.dat && (r->data_bps == 0 || r->data_bps < r->nom_bps)) return CAN_ERATE;
    int s_nom = 0, s_dat = 0;
    if (mode == T_WORST) {
        s_nom = stuff_max(L.dyn_nom);
        s_dat = stuff_max(L.dyn) - s_nom;
    }
    t->bits_nom = L.nom + s_nom;
    t->bits_dat = L.dat + s_dat;
    t->us = t->bits_nom * (1e6 / r->nom_bps) + (t->bits_dat ? t->bits_dat * (1e6 / r->data_bps) : 0.0);
    return CAN_OK;
}

/* --- CRC (MSB first, 비트 단위). RevEng: CRC-15/CAN 0x4599, CRC-17/CAN-FD 0x1685B, CRC-21/CAN-FD 0x102899 --- */
static uint32_t crc_bit(uint32_t crc, unsigned b, int w, uint32_t poly) {
    uint32_t msb = (crc >> (w - 1)) & 1u;
    crc = (crc << 1) & ((1u << w) - 1u);
    if (msb ^ (b & 1u)) crc ^= poly;
    return crc;
}

static uint32_t crc_bytes(const uint8_t *p, size_t n, int w, uint32_t poly, uint32_t init) {
    uint32_t c = init;
    for (size_t i = 0; i < n; i++)
        for (int k = 7; k >= 0; k--) c = crc_bit(c, (p[i] >> k) & 1u, w, poly);
    return c;
}

static uint32_t crc_poly(int w) { return w == 15 ? 0x4599u : w == 17 ? 0x1685Bu : 0x102899u; }

static unsigned gray3(unsigned s) { s &= 7u; return s ^ (s >> 1); }

/* 온-와이어 비트열: bits[i] = 선 레벨 (ACK slot 은 수신자가 덮어쓴 dominant 로), ph[i] = 0 nominal / 1 data.
 * Classic: CRC-15 (init 0) 를 스터핑 전 SOF..데이터 위에서 계산, SOF..CRC 끝까지 동적 스터핑
 *          (마지막 CRC 비트 뒤에 와야 할 스터프 비트도 넣는다).
 * FD: SOF..마지막 데이터 비트까지 동적 스터핑 (마지막 데이터 비트 뒤 스터프 비트는 넣지 않는다 — 바로 뒤의 고정
 *     스터프 비트가 그 역할), 이어서 CRC 필드 = [SBC 4비트: Gray(동적 스터프 수 mod 8) 3 + 짝수 패리티 1] +
 *     CRC 17/21. CRC 는 스터핑된 SOF..데이터 + SBC 위에서, 초기값 1 << (w−1). 고정 스터프 비트 = CRC 필드
 *     맨 앞과 매 4비트 뒤, 값은 직전 비트의 보수. 이후 CRC delim, ACK, ACK delim, EOF 7, IFS 3.
 * 위상: BRS=1 이면 BRS 다음 비트(스터프 비트 포함)부터 CRC delim 까지 data. *n_stuff = 동적 스터프 비트 수.
 * 반환: 비트 수 또는 오류 (cap 부족 → CAN_ECAP, 버퍼 내용은 정의 안 됨). */
static int can_wire_encode(const can_msg *m, uint8_t *bits, uint8_t *ph, size_t cap, int *n_stuff) {
    uint8_t u[41 + 512 + 15];
    can_layout L;
    if (!bits || !ph || !n_stuff) return CAN_EARG;
    int rc = can_layout_of(m, &L);
    if (rc) return rc;
    int nu = can_hdr_encode(m, u, sizeof u);
    if (nu < 0) return nu;
    for (int i = 0; i < L.data / 8; i++) nu = (int)put_bits(u, (size_t)nu, m->data[i], 8);
    if (!m->fdf) {
        uint32_t c = 0;
        for (int i = 0; i < nu; i++) c = crc_bit(c, u[i], 15, 0x4599u);
        nu = (int)put_bits(u, (size_t)nu, c, 15);
    }
    int pre = (m->fdf && m->brs) ? (m->ide ? 36 : 17) : nu + 1;   /* u[pre..] 은 data 위상 */
    size_t n = 0;
    int stuff = 0, run = 0;
    uint8_t last = 2;
#define EMIT(v, p) do { if (n >= cap) return CAN_ECAP; bits[n] = (uint8_t)(v); ph[n] = (uint8_t)(p); n++; } while (0)
    for (int i = 0; i < nu; i++) {
        if (run == 5) {                                   /* 5개 같으면 다음 비트 앞에 보수 */
            EMIT(!last, i >= pre); stuff++;
            last = (uint8_t)!last; run = 1;
        }
        EMIT(u[i], i >= pre);
        if (u[i] == last) run++; else { last = u[i]; run = 1; }
    }
    if (!m->fdf) {
        if (run == 5) { EMIT(!last, 0); stuff++; }
        EMIT(1, 0);                                       /* CRC delim */
    } else {
        int w = L.crc, dp = m->brs ? 1 : 0;
        uint32_t c = 1u << (w - 1);
        for (size_t i = 0; i < n; i++) c = crc_bit(c, bits[i], w, crc_poly(w));
        unsigned g = gray3((unsigned)stuff);
        unsigned par = ((g >> 2) ^ (g >> 1) ^ g) & 1u;
        unsigned sbc = (g << 1) | par;
        for (int k = 3; k >= 0; k--) c = crc_bit(c, (sbc >> k) & 1u, w, crc_poly(w));
        uint8_t f[4 + 21];
        size_t nf = put_bits(f, 0, sbc, 4);
        nf = put_bits(f, nf, c, w);
        for (size_t i = 0; i < nf; i++) {
            if (i % 4 == 0) EMIT(!bits[n - 1], dp);        /* 고정 스터프 비트 */
            EMIT(f[i], dp);
        }
        EMIT(1, dp);                                      /* CRC delim (data 쪽으로 센다) */
    }
    EMIT(0, 0);                                           /* ACK slot (수신자 dominant) */
    for (int k = 0; k < 1 + 7 + 3; k++) EMIT(1, 0);       /* ACK delim, EOF, IFS */
#undef EMIT
    *n_stuff = stuff;
    return (int)n;
}

static double wire_time_us(const uint8_t *ph, int n, const can_rates *r) {
    int a = 0, b = 0;
    for (int i = 0; i < n; i++) { if (ph[i]) b++; else a++; }
    return a * (1e6 / r->nom_bps) + (b ? b * (1e6 / r->data_bps) : 0.0);
}

/* ================= Part C: 버스 부하와 로봇 관절 버스 ================= */
typedef struct { can_msg msg; uint32_t period_us; } can_periodic;

/* 부하(%) = Σ 프레임 시간 / 주기 × 100. 하나라도 잘못되면 그 오류 코드, period 0 → CAN_EARG. */
static int can_bus_load(const can_periodic *set, int n, const can_rates *r, int mode, double *load_pct) {
    if (!set || n < 0 || !load_pct) return CAN_EARG;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        can_time t;
        if (set[i].period_us == 0) return CAN_EARG;
        int rc = can_frame_time(&set[i].msg, r, mode, &t);
        if (rc) return rc;
        sum += t.us / set[i].period_us;
    }
    *load_pct = sum * 100.0;
    return CAN_OK;
}

/* 로봇 관절 버스: 매 주기(rate_hz)마다 모터 하나당 명령 1개 + 상태 1개, 각각 payload 바이트, 표준 ID.
 * FD: 한 프레임 (payload 를 DLC 길이로 패딩), BRS = brs. Classic: 8바이트씩 나눠 ⌈payload/8⌉ 프레임.
 * 최악 스터핑으로 부하가 limit_pct 이하인 최대 모터 수 (0..max_n) 와 그때 부하를 돌려준다.
 * payload > 64 (FD) 또는 0 → CAN_ELEN, rate_hz 0 → CAN_EARG, 비트레이트 오류는 그대로. */
static int robot_max_motors(const can_rates *r, int fd, int brs, unsigned payload, unsigned rate_hz,
                            double limit_pct, int max_n, double *load_at) {
    if (!r || !load_at || rate_hz == 0 || max_n < 0) return CAN_EARG;
    if (payload == 0 || (fd && payload > 64)) return CAN_ELEN;
    can_msg m;
    memset(&m, 0, sizeof m);
    m.id = 0x100;
    double per_motor_us = 0;
    can_time t;
    if (fd) {
        int d = canfd_len2dlc(payload);
        m.fdf = 1; m.brs = (uint8_t)(brs ? 1 : 0); m.len = (uint8_t)canfd_dlc2len((uint8_t)d);
        int rc = can_frame_time(&m, r, T_WORST, &t);
        if (rc) return rc;
        per_motor_us = 2 * t.us;
    } else {
        for (unsigned left = payload; left > 0; left -= (left > 8 ? 8 : left)) {
            m.len = (uint8_t)(left > 8 ? 8 : left);
            int rc = can_frame_time(&m, r, T_WORST, &t);
            if (rc) return rc;
            per_motor_us += 2 * t.us;
        }
    }
    double per_motor_pct = per_motor_us * rate_hz / 1e4;   /* us × Hz / 1e6 × 100 */
    int best = 0;
    for (int k = 1; k <= max_n; k++)
        if (k * per_motor_pct <= limit_pct + 1e-9) best = k;
    *load_at = best * per_motor_pct;
    return best;
}

/* ---------------- demo ---------------- */
static uint32_t g_rng = 0x1234567u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static void mk(can_msg *m, int fd, int brs, int len) {
    memset(m, 0, sizeof *m);
    m->id = 0x123; m->fdf = (uint8_t)fd; m->brs = (uint8_t)brs; m->len = (uint8_t)len;
}

/* 랜덤 ID(표준) + 랜덤 데이터 프레임 trials 개의 정확한 평균 시간 */
static double avg_exact_us(int fd, int brs, int len, const can_rates *r, int trials, double *max_us) {
    static uint8_t w[1200], ph[1200];
    double sum = 0; *max_us = 0;
    for (int t = 0; t < trials; t++) {
        can_msg m; int ns;
        mk(&m, fd, brs, len);
        m.id = xs32() & 0x7FFu;
        for (int i = 0; i < len; i++) m.data[i] = (uint8_t)xs32();
        int n = can_wire_encode(&m, w, ph, sizeof w, &ns);
        if (n < 0) return -1;
        double us = wire_time_us(ph, n, r);
        sum += us; if (us > *max_us) *max_us = us;
    }
    return sum / trials;
}

int main(void) {
    can_msg m; can_layout L; can_time t0, t1;
    (void)canfd_pad; (void)can_hdr_decode; (void)crc_bytes; (void)can_bus_load;   /* solution.c 에서 복사한 함수 중 demo 가 안 쓰는 것 */
    printf("표 1. 필드별 비트 수 (표준 ID, 스터프 제외, IFS 3 포함)\n");
    printf("  프레임           | hdr | data | SBC | FSB | CRC | tail | IFS | 동적구간 | nominal | data\n");
    struct { const char *name; int fd, brs, len; } f1[] = {
        { "Classic 8 B     ", 0, 0, 8 }, { "FD 8 B  BRS=1   ", 1, 1, 8 }, { "FD 16 B BRS=1   ", 1, 1, 16 },
        { "FD 20 B BRS=1   ", 1, 1, 20 }, { "FD 64 B BRS=1   ", 1, 1, 64 }, { "FD 64 B BRS=0   ", 1, 0, 64 } };
    for (size_t i = 0; i < sizeof f1 / sizeof f1[0]; i++) {
        mk(&m, f1[i].fd, f1[i].brs, f1[i].len);
        can_layout_of(&m, &L);
        printf("  %s | %3d | %4d | %3d | %3d | %3d | %4d | %3d | %6d   | %5d   | %4d\n", f1[i].name, L.hdr, L.data,
               L.sbc, L.fsb, L.crc, L.tail, L.ifs, L.dyn, L.nom, L.dat);
    }

    printf("\n표 2. 프레임 시간과 100%% 부하 처리량 (표준 ID). 하한 = 스터프 0, 평균/최대 = 랜덤 데이터 20000개 실측,\n"
           "      상한 = 최악 스터프 상한식. 효율 = 페이로드 비트 시간 / 프레임 시간 (하한 기준)\n");
    printf("  설정 (nom/data)        | len | 하한 us | 평균 us | 최대 us | 상한 us | 처리량 Mbit/s (하한/상한) | 효율\n");
    struct { const char *name; int fd, brs, len; can_rates r; } f2[] = {
        { "Classic   500k        ", 0, 0, 8,  { 500000, 500000 } },
        { "Classic   1M          ", 0, 0, 8,  { 1000000, 1000000 } },
        { "FD 500k/500k (BRS=0)  ", 1, 0, 8,  { 500000, 500000 } },
        { "FD 500k/500k (BRS=0)  ", 1, 0, 64, { 500000, 500000 } },
        { "FD 500k/2M            ", 1, 1, 8,  { 500000, 2000000 } },
        { "FD 500k/2M            ", 1, 1, 16, { 500000, 2000000 } },
        { "FD 500k/2M            ", 1, 1, 64, { 500000, 2000000 } },
        { "FD 1M/5M              ", 1, 1, 8,  { 1000000, 5000000 } },
        { "FD 1M/5M              ", 1, 1, 16, { 1000000, 5000000 } },
        { "FD 1M/5M              ", 1, 1, 64, { 1000000, 5000000 } },
        { "FD 1M/8M              ", 1, 1, 64, { 1000000, 8000000 } },
    };
    for (size_t i = 0; i < sizeof f2 / sizeof f2[0]; i++) {
        mk(&m, f2[i].fd, f2[i].brs, f2[i].len);
        can_frame_time(&m, &f2[i].r, T_NOSTUFF, &t0);
        can_frame_time(&m, &f2[i].r, T_WORST, &t1);
        double mx, avg = avg_exact_us(f2[i].fd, f2[i].brs, f2[i].len, &f2[i].r, 20000, &mx);
        double bits = 8.0 * f2[i].len;
        double tpay = bits * 1e6 / (f2[i].brs ? f2[i].r.data_bps : f2[i].r.nom_bps);
        printf("  %s | %3d | %7.1f | %7.1f | %7.1f | %7.1f | %6.3f / %6.3f          | %4.1f%%\n", f2[i].name,
               f2[i].len, t0.us, avg, mx, t1.us, bits / t0.us, bits / t1.us, 100.0 * tpay / t0.us);
    }

    printf("\n표 3. 로봇 관절 버스: 1 kHz, 모터당 16 B 명령 + 16 B 상태 (Classic 은 8 B × 2 프레임씩), 최악 스터핑\n");
    printf("  설정 (nom/data)        | 모터당 부하 | N@50%% | N@70%% | N@100%% | 6모터 부하 | 12모터 부하\n");
    struct { const char *name; int fd, brs; can_rates r; } f3[] = {
        { "Classic   500k        ", 0, 0, { 500000, 500000 } },
        { "Classic   1M          ", 0, 0, { 1000000, 1000000 } },
        { "FD 1M/1M (BRS=0)      ", 1, 0, { 1000000, 1000000 } },
        { "FD 500k/2M            ", 1, 1, { 500000, 2000000 } },
        { "FD 1M/2M              ", 1, 1, { 1000000, 2000000 } },
        { "FD 1M/4M              ", 1, 1, { 1000000, 4000000 } },
        { "FD 1M/5M              ", 1, 1, { 1000000, 5000000 } },
        { "FD 1M/8M              ", 1, 1, { 1000000, 8000000 } },
    };
    for (size_t i = 0; i < sizeof f3 / sizeof f3[0]; i++) {
        double l50, l70, l100, l1;
        int n50 = robot_max_motors(&f3[i].r, f3[i].fd, f3[i].brs, 16, 1000, 50.0, 64, &l50);
        int n70 = robot_max_motors(&f3[i].r, f3[i].fd, f3[i].brs, 16, 1000, 70.0, 64, &l70);
        int n100 = robot_max_motors(&f3[i].r, f3[i].fd, f3[i].brs, 16, 1000, 100.0, 64, &l100);
        robot_max_motors(&f3[i].r, f3[i].fd, f3[i].brs, 16, 1000, 1e9, 1, &l1);
        printf("  %s | %8.1f%%   | %4d  | %4d  | %5d  | %8.1f%%  | %8.1f%%\n", f3[i].name, l1, n50, n70, n100,
               6 * l1, 12 * l1);
    }
    return 0;
}
