/* COMM-M08 CAN FD: BRS, DLC→64바이트 매핑, 데이터 위상 비트레이트
 *
 * Part A: DLC ↔ 길이 (Classic 0~8, DLC 9~15 = 8 / FD 0~8,12,16,20,24,32,48,64), 패딩,
 *         FD 플래그(FDF/BRS/ESI, RRS, res)가 들어간 헤더 비트 인코더 / 디코더 (Classic·FD × 표준·확장)
 * Part B: 필드별 비트 수 + 두 비트레이트(중재 위상 / 데이터 위상) 전송 시간 계산기
 *         (스터핑 없음 하한 / 최악 스터핑 상한) + 실제 온-와이어 비트열 인코더(동적 스터핑, stuff count,
 *         고정 스터프 비트, CRC-15/17/21)로 정확한 시간. 잘못된 플래그·길이·비트레이트·용량은 오류 코드.
 * Part C: 주기 메시지 집합의 버스 부하(%) + 로봇 관절 버스 (1 kHz × N 모터 × 16 B) 설정별 최대 모터 수
 *
 * 규약: dominant = 0, recessive = 1. 시간 계산의 위상 경계 — BRS 비트는 중재(nominal) 쪽, CRC delimiter 는
 *       데이터 쪽으로 센다. 실제로는 두 비트 모두 샘플 포인트에서 속도가 바뀌지만 두 비트의 합은 정확히
 *       T_nom + T_data 라 총 시간은 같다 (풀이 참고).
 * ISO 11898-1:2015 세부(고정 스터프 비트 위치, stuff count 의 Gray 코드 + 짝수 패리티, FD CRC 의 초기값과
 * 범위)는 이 모델의 정의다 — ISO 11898-1 확인 권장. 프레임 길이·시간은 CRC 값과 무관하다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
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

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int near(double a, double b, double tol) { return a - b <= tol && b - a <= tol; }

static uint32_t g_rng = 0x2545F491u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static void bits_str(const uint8_t *b, int n, char *s) { for (int i = 0; i < n; i++) s[i] = (char)('0' + b[i]); s[n] = 0; }

static void rand_msg(can_msg *m) {
    memset(m, 0, sizeof *m);
    m->ide = xs32() & 1u;
    m->id = xs32() & (m->ide ? 0x1FFFFFFFu : 0x7FFu);
    m->fdf = xs32() & 1u;
    if (m->fdf) {
        m->brs = xs32() & 1u; m->esi = xs32() & 1u;
        m->len = (uint8_t)canfd_dlc2len((uint8_t)(xs32() & 15u));
    } else {
        m->rtr = (xs32() % 4u) == 0;
        m->len = (uint8_t)(xs32() % 9u);
        if (m->len == 8 && (xs32() & 1u)) m->len8_dlc = (uint8_t)(9 + xs32() % 7u);
    }
    uint32_t mode = xs32() % 4u;   /* 0 랜덤, 1 전부 0x00, 2 전부 0xFF, 3 0x78/0x3C 교대 (스터핑 많은 패턴) */
    for (int i = 0; i < 64; i++)
        m->data[i] = mode == 0 ? (uint8_t)xs32() : mode == 1 ? 0x00 : mode == 2 ? 0xFF : (i & 1 ? 0x3C : 0x78);
}

/* 스터프 비트 제거: 시작 n0 비트 중 앞의 dyn 개 원래 비트를 꺼낸다 (FD 동적 구간 / Classic SOF..CRC). */
static int destuff(const uint8_t *w, int n, int dyn, uint8_t *out, int *bad6) {
    int k = 0, run = 0, i = 0;
    uint8_t last = 2;
    *bad6 = 0;
    while (k < dyn && i < n) {
        uint8_t b = w[i++];
        if (run == 5) {                         /* 이 비트는 스터프 비트여야 */
            if (b == last) { *bad6 = 1; return k; }
            last = b; run = 1; continue;
        }
        out[k++] = b;
        if (b == last) run++; else { last = b; run = 1; }
    }
    return k;
}

int main(void) {
    uint8_t buf[64], hb[64] = { 0 };
    char s[700];
    can_msg m, d;

    /* ---- Part A: DLC 표 ---- */
    static const unsigned fd_tab[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };
    int ok = 1;
    for (int i = 0; i < 16; i++) ok &= canfd_dlc2len((uint8_t)i) == fd_tab[i];
    CHECK(ok);
    ok = 1;
    for (int i = 0; i < 16; i++) ok &= can_cc_dlc2len((uint8_t)i) == (i > 8 ? 8u : (unsigned)i);
    CHECK(ok);
    CHECK(canfd_dlc2len(0x1F) == 64);                 /* 하위 4비트만 */
    ok = 1;
    for (unsigned l = 0; l <= 64; l++) {             /* 가장 작은 DLC: 담고, 하나 작은 DLC 는 못 담는다 */
        int dl = canfd_len2dlc(l);
        ok &= dl >= 0 && dl <= 15 && canfd_dlc2len((uint8_t)dl) >= l && (dl == 0 || canfd_dlc2len((uint8_t)(dl - 1)) < l);
        if (l > 0) ok &= dl >= canfd_len2dlc(l - 1);  /* 단조 */
    }
    CHECK(ok);
    ok = 1;
    for (int dl = 0; dl < 16; dl++) ok &= canfd_len2dlc(canfd_dlc2len((uint8_t)dl)) == dl;
    CHECK(ok);
    CHECK(canfd_len2dlc(9) == 9 && canfd_len2dlc(13) == 10 && canfd_len2dlc(33) == 14 && canfd_len2dlc(49) == 15);
    CHECK(canfd_len2dlc(65) == -1 && canfd_len2dlc(1000) == -1);

    /* 패딩 */
    uint8_t src[64], dlc = 0xEE;
    for (int i = 0; i < 64; i++) src[i] = (uint8_t)(i + 1);
    memset(buf, 0x55, sizeof buf);
    CHECK(canfd_pad(src, 13, 0xCC, buf, sizeof buf, &dlc) == 16 && dlc == 10 &&
          memcmp(buf, src, 13) == 0 && buf[13] == 0xCC && buf[15] == 0xCC);
    CHECK(canfd_pad(src, 0, 0xCC, buf, sizeof buf, &dlc) == 0 && dlc == 0);
    CHECK(canfd_pad(src, 64, 0, buf, sizeof buf, &dlc) == 64 && dlc == 15 && memcmp(buf, src, 64) == 0);
    memset(buf, 0x55, sizeof buf); dlc = 0xEE;
    CHECK(canfd_pad(src, 21, 0, buf, 23, &dlc) == CAN_ECAP && buf[0] == 0x55 && dlc == 0xEE);   /* 24 필요 */
    CHECK(canfd_pad(src, 65, 0, buf, sizeof buf, &dlc) == CAN_ELEN);
    CHECK(canfd_pad(NULL, 3, 0, buf, sizeof buf, &dlc) == CAN_EARG);
    ok = 1;
    for (unsigned l = 0; l <= 64; l++) {
        int p = canfd_pad(src, l, 0xAA, buf, sizeof buf, &dlc);
        ok &= p == (int)canfd_dlc2len(dlc) && p >= (int)l && memcmp(buf, src, l) == 0;
        for (int i = (int)l; i < p; i++) ok &= buf[i] == 0xAA;
    }
    CHECK(ok);

    /* 헤더 비트: 손으로 쓴 기대값 */
    memset(&m, 0, sizeof m);
    m.id = 0x123; m.fdf = 1; m.brs = 1; m.len = 64;
    int nh = can_hdr_encode(&m, hb, sizeof hb);
    bits_str(hb, nh > 0 ? nh : 0, s);
    /*                 SOF ID=0x123      RRS IDE FDF res BRS ESI DLC */
    CHECK(nh == 22 && strcmp(s, "0" "00100100011" "0" "0" "1" "0" "1" "0" "1111") == 0);
    memset(&m, 0, sizeof m);
    m.id = 0x123; m.len = 2;
    nh = can_hdr_encode(&m, hb, sizeof hb);
    bits_str(hb, nh > 0 ? nh : 0, s);
    /*                 SOF ID          RTR IDE r0  DLC */
    CHECK(nh == 19 && strcmp(s, "0" "00100100011" "0" "0" "0" "0010") == 0);
    memset(&m, 0, sizeof m);
    m.id = 0x12345678 & 0x1FFFFFFF; m.ide = 1; m.fdf = 1; m.esi = 1; m.len = 12;
    nh = can_hdr_encode(&m, hb, sizeof hb);
    bits_str(hb, nh > 0 ? nh : 0, s);
    /* ID 0x12345678 → ID-A = id>>18 = 0x48D (10010001101), ID-B = 0x05678 (000101011001111000) */
    CHECK(nh == 41 && strcmp(s, "0" "10010001101" "1" "1" "000101011001111000" "0" "1" "0" "0" "1" "1001") == 0);
    memset(&m, 0, sizeof m);
    m.id = 0x1FFFFFFF; m.ide = 1; m.rtr = 1; m.len = 8;
    CHECK(can_hdr_encode(&m, hb, sizeof hb) == 39);

    /* 헤더 랜덤 왕복 */
    int rt_ok = 1;
    for (int it = 0; it < 20000; it++) {
        rand_msg(&m);
        int k = can_hdr_encode(&m, hb, sizeof hb);
        int k2 = can_hdr_decode(hb, (size_t)(k > 0 ? k : 0), &d);
        rt_ok &= k > 0 && k2 == k && d.id == m.id && d.ide == m.ide && d.fdf == m.fdf && d.brs == m.brs &&
                 d.esi == m.esi && d.rtr == m.rtr && d.len == m.len && d.len8_dlc == m.len8_dlc;
    }
    CHECK(rt_ok);

    /* ---- Part B: 잘못된 입력 ---- */
    memset(&m, 0, sizeof m); m.brs = 1;               CHECK(can_hdr_encode(&m, hb, sizeof hb) == CAN_EFLAGS);
    memset(&m, 0, sizeof m); m.esi = 1;               CHECK(can_hdr_encode(&m, hb, sizeof hb) == CAN_EFLAGS);
    memset(&m, 0, sizeof m); m.fdf = 1; m.rtr = 1;    CHECK(can_hdr_encode(&m, hb, sizeof hb) == CAN_ERTR_FD);
    memset(&m, 0, sizeof m); m.fdf = 1; m.len = 13;   CHECK(can_hdr_encode(&m, hb, sizeof hb) == CAN_ELEN);
    memset(&m, 0, sizeof m); m.fdf = 1; m.len = 65;   CHECK(can_hdr_encode(&m, hb, sizeof hb) == CAN_ELEN);
    memset(&m, 0, sizeof m); m.len = 9;               CHECK(can_hdr_encode(&m, hb, sizeof hb) == CAN_ELEN);
    memset(&m, 0, sizeof m); m.len = 7; m.len8_dlc = 12; CHECK(can_hdr_encode(&m, hb, sizeof hb) == CAN_ELEN);
    memset(&m, 0, sizeof m); m.id = 0x800;            CHECK(can_hdr_encode(&m, hb, sizeof hb) == CAN_EID);
    memset(&m, 0, sizeof m); m.id = 0x20000000; m.ide = 1; CHECK(can_hdr_encode(&m, hb, sizeof hb) == CAN_EID);
    memset(&m, 0, sizeof m); m.fdf = 1;               CHECK(can_hdr_encode(&m, hb, 21) == CAN_ECAP);
    CHECK(can_hdr_encode(&m, hb, 22) == 22);
    CHECK(can_hdr_encode(NULL, hb, sizeof hb) == CAN_EARG);

    /* 디코더 고장 */
    memset(&m, 0, sizeof m); m.id = 0x2AA; m.fdf = 1; m.len = 8;
    nh = can_hdr_encode(&m, hb, sizeof hb);
    hb[15] = 1;                                         /* res 를 recessive 로 (CAN XL 의 XLF 자리) */
    CHECK(nh == 22 && can_hdr_decode(hb, 22, &d) == CAN_ERES);
    hb[15] = 0;
    CHECK(can_hdr_decode(hb, 21, &d) == CAN_ESHORT && can_hdr_decode(hb, 10, &d) == CAN_ESHORT);
    hb[0] = 1;
    CHECK(can_hdr_decode(hb, 22, &d) == CAN_EFORM);
    /* Classic DLC 12 → len 8, 원래 DLC 보존 */
    memset(&m, 0, sizeof m); m.id = 0x7FF; m.len = 8; m.len8_dlc = 12;
    nh = can_hdr_encode(&m, hb, sizeof hb);
    CHECK(nh == 19 && hb[15] == 1 && hb[16] == 1 && hb[17] == 0 && hb[18] == 0);
    CHECK(can_hdr_decode(hb, 19, &d) == 19 && d.len == 8 && d.len8_dlc == 12 && !d.fdf);
    /* 같은 헤더 위치: Classic 의 r0(dominant) 자리가 FD 의 FDF(recessive) */
    memset(&m, 0, sizeof m); m.id = 0x55; m.len = 8;
    can_hdr_encode(&m, hb, sizeof hb);
    uint8_t cc_bit14 = hb[14];
    m.fdf = 1;
    can_hdr_encode(&m, hb, sizeof hb);
    CHECK(cc_bit14 == 0 && hb[14] == 1);

    /* ---- Part B: 필드별 비트 수 (Linux include/linux/can/length.h 와 같은 값) ---- */
    can_layout L;
    ok = 1;
    for (int n = 0; n <= 8; n++) {
        memset(&m, 0, sizeof m); m.len = (uint8_t)n;
        ok &= can_layout_of(&m, &L) == 0 && L.nom == 47 + 8 * n && L.dat == 0 && L.dyn == 34 + 8 * n;
        m.ide = 1;
        ok &= can_layout_of(&m, &L) == 0 && L.nom == 67 + 8 * n && L.dyn == 54 + 8 * n;
    }
    CHECK(ok);
    memset(&m, 0, sizeof m); m.rtr = 1; m.len = 8;
    CHECK(can_layout_of(&m, &L) == 0 && L.nom == 47 && L.data == 0);
    ok = 1;
    for (int dl = 0; dl < 16; dl++) {
        int n = (int)canfd_dlc2len((uint8_t)dl);
        for (int ide = 0; ide < 2; ide++) {
            memset(&m, 0, sizeof m); m.fdf = 1; m.brs = 1; m.ide = (uint8_t)ide; m.len = (uint8_t)n;
            int base = ide ? (n <= 16 ? 81 : 86) : (n <= 16 ? 62 : 67);
            ok &= can_layout_of(&m, &L) == 0 && L.nom + L.dat == base + 8 * n;
            ok &= L.nom == (ide ? 48 : 29) && L.fsb == (n <= 16 ? 6 : 7) && L.crc == (n <= 16 ? 17 : 21);
            ok &= L.hdr + L.sbc + L.fsb + L.crc + 1 == (ide ? 41 : 22) + (n <= 16 ? 28 : 33);  /* Linux CRC 필드 = 28/33 */
            m.brs = 0;
            ok &= can_layout_of(&m, &L) == 0 && L.nom == base + 8 * n && L.dat == 0;
        }
    }
    CHECK(ok);

    /* ---- Part B: 시간 계산기 (손 계산 값) ---- */
    can_rates r500 = { 500000, 500000 }, r500_2m = { 500000, 2000000 }, r1m_5m = { 1000000, 5000000 };
    can_time t;
    memset(&m, 0, sizeof m); m.len = 8;
    CHECK(can_frame_time(&m, &r500, T_NOSTUFF, &t) == 0 && t.bits_nom == 111 && near(t.us, 222.0, 1e-9));
    CHECK(can_frame_time(&m, &r500, T_WORST, &t) == 0 && t.bits_nom == 135 && near(t.us, 270.0, 1e-9));
    memset(&m, 0, sizeof m); m.fdf = 1; m.brs = 1; m.len = 64;
    CHECK(can_frame_time(&m, &r500_2m, T_NOSTUFF, &t) == 0 && t.bits_nom == 29 && t.bits_dat == 550 && near(t.us, 333.0, 1e-9));
    CHECK(can_frame_time(&m, &r500_2m, T_WORST, &t) == 0 && t.bits_nom == 32 && t.bits_dat == 680 && near(t.us, 404.0, 1e-9));
    memset(&m, 0, sizeof m); m.fdf = 1; m.brs = 1; m.len = 16;
    CHECK(can_frame_time(&m, &r1m_5m, T_WORST, &t) == 0 && t.bits_nom == 32 && t.bits_dat == 195 && near(t.us, 71.0, 1e-9));
    m.brs = 0;                                          /* BRS=0: 전부 nominal, data_bps 무시 */
    can_rates r1m_0 = { 1000000, 0 };
    CHECK(can_frame_time(&m, &r1m_0, T_NOSTUFF, &t) == 0 && t.bits_dat == 0 && t.bits_nom == 190 && near(t.us, 190.0, 1e-9));
    /* 비트레이트 오류 */
    m.brs = 1;
    can_rates bad1 = { 0, 2000000 }, bad2 = { 1000000, 500000 };
    CHECK(can_frame_time(&m, &bad1, T_NOSTUFF, &t) == CAN_ERATE);
    CHECK(can_frame_time(&m, &bad2, T_NOSTUFF, &t) == CAN_ERATE);
    CHECK(can_frame_time(&m, &r1m_0, T_NOSTUFF, &t) == CAN_ERATE);
    m.len = 13;
    CHECK(can_frame_time(&m, &r1m_5m, T_NOSTUFF, &t) == CAN_ELEN);
    /* 단조성: 길이↑ → 시간↑ (같은 설정), 데이터 속도↑ → 시간↓ */
    ok = 1;
    const can_rates rs[] = { { 500000, 1000000 }, { 500000, 2000000 }, { 500000, 4000000 }, { 1000000, 5000000 }, { 1000000, 8000000 } };
    for (int mode = 0; mode < 2; mode++)
        for (int ide = 0; ide < 2; ide++) {
            double prev_r[16] = { 0 };
            for (size_t ri = 0; ri < sizeof rs / sizeof rs[0]; ri++) {
                double prev = -1;
                for (int dl = 0; dl < 16; dl++) {
                    memset(&m, 0, sizeof m); m.fdf = 1; m.brs = 1; m.ide = (uint8_t)ide; m.len = (uint8_t)canfd_dlc2len((uint8_t)dl);
                    ok &= can_frame_time(&m, &rs[ri], mode, &t) == 0 && t.us > prev;
                    prev = t.us;
                    if (ri == 2 || ri == 4) ok &= t.us < prev_r[dl];   /* 같은 nominal 에서 data 속도↑ */
                    prev_r[dl] = t.us;
                }
            }
        }
    CHECK(ok);

    /* ---- Part B: CRC 확인값 (RevEng "123456789", init 0) ---- */
    const uint8_t chk[] = "123456789";
    CHECK(crc_bytes(chk, 9, 15, 0x4599u, 0) == 0x059Eu);
    CHECK(crc_bytes(chk, 9, 17, 0x1685Bu, 0) == 0x04F03u);
    CHECK(crc_bytes(chk, 9, 21, 0x102899u, 0) == 0x0ED841u);

    /* ---- Part B: 온-와이어 인코더 — 정확한 길이 ∈ [하한, 상한], 스터핑 규칙, 위상 ---- */
    static uint8_t w[1000], ph[1000], ub[700], ds[700];
    int enc_ok = 1, bound_ok = 1, rule_ok = 1, phase_ok = 1, sbc_ok = 1, crc_ok = 1;
    for (int it = 0; it < 6000; it++) {
        rand_msg(&m);
        int ns = -1;
        int n = can_wire_encode(&m, w, ph, sizeof w, &ns);
        if (n < 0 || can_layout_of(&m, &L) != 0) { enc_ok = 0; continue; }
        enc_ok &= n == L.nom + L.dat + ns;
        enc_ok &= ns <= stuff_max(L.dyn);
        /* 스터핑 제거 → 원래 헤더 + 데이터 (+ Classic CRC) */
        int nu = can_hdr_encode(&m, ub, sizeof ub);
        for (int i = 0; i < L.data / 8; i++) nu = (int)put_bits(ub, (size_t)nu, m.data[i], 8);
        int bad6 = 0;
        int k = destuff(w, n, L.dyn, ds, &bad6);
        rule_ok &= !bad6 && k == L.dyn && memcmp(ds, ub, (size_t)nu) == 0;
        if (!m.fdf) {                                   /* Classic: 스터핑 제거한 SOF..CRC 의 CRC 나머지 = 0 */
            uint32_t c = 0;
            for (int i = 0; i < L.dyn; i++) c = crc_bit(c, ds[i], 15, 0x4599u);
            crc_ok &= c == 0;
        } else {                                        /* FD: 동적 구간 다음이 FSB, SBC 4, FSB ... */
            int p = L.dyn + ns;                         /* CRC 필드 시작 */
            unsigned sb = 0;
            int fsb_ok = 1;
            int nf = L.sbc + L.crc;
            uint8_t f[25];
            for (int i = 0, q = p; i < nf; i++) {
                if (i % 4 == 0) { fsb_ok &= w[q] != w[q - 1]; q++; }
                f[i] = w[q++];
            }
            for (int i = 0; i < 4; i++) sb = (sb << 1) | f[i];
            unsigned g = gray3((unsigned)ns);
            sbc_ok &= fsb_ok && sb == ((g << 1) | (((g >> 2) ^ (g >> 1) ^ g) & 1u));
            /* CRC: 스터핑된 SOF..데이터 + SBC + 받은 CRC → 나머지 0 */
            uint32_t c = 1u << (L.crc - 1);
            for (int i = 0; i < p; i++) c = crc_bit(c, w[i], L.crc, crc_poly(L.crc));
            for (int i = 0; i < nf; i++) c = crc_bit(c, f[i], L.crc, crc_poly(L.crc));
            crc_ok &= c == 0;
        }
        /* 위상 / 시간 */
        can_time lo, hi;
        can_rates rr = { 500000, 2000000 };
        if (can_frame_time(&m, &rr, T_NOSTUFF, &lo) || can_frame_time(&m, &rr, T_WORST, &hi)) { bound_ok = 0; continue; }
        double ex = wire_time_us(ph, n, &rr);
        bound_ok &= ex >= lo.us - 1e-9 && ex <= hi.us + 1e-9;
        int cnt_dat = 0;
        for (int i = 0; i < n; i++) cnt_dat += ph[i];
        phase_ok &= cnt_dat >= L.dat && cnt_dat <= L.dat + ns;
        phase_ok &= ph[0] == 0 && ph[n - 12] == 0 && ph[n - 13] == (m.fdf && m.brs ? 1 : 0);   /* SOF, ACK = nominal, CRC delim */
    }
    CHECK(enc_ok);
    CHECK(rule_ok);
    CHECK(crc_ok);
    CHECK(sbc_ok);
    CHECK(bound_ok);
    CHECK(phase_ok);
    /* 용량 부족 */
    memset(&m, 0, sizeof m); m.fdf = 1; m.brs = 1; m.len = 64;
    int ns = 0;
    CHECK(can_wire_encode(&m, w, ph, 500, &ns) == CAN_ECAP);
    m.len = 63;
    CHECK(can_wire_encode(&m, w, ph, sizeof w, &ns) == CAN_ELEN);
    /* 전부 0 데이터: 동적 스터프가 데이터 구간에서 4비트마다 */
    memset(&m, 0, sizeof m); m.id = 0x555; m.fdf = 1; m.brs = 1; m.len = 8;
    int n0 = can_wire_encode(&m, w, ph, sizeof w, &ns);
    can_layout_of(&m, &L);
    CHECK(n0 == L.nom + L.dat + ns && ns >= 12);

    /* ---- Part C: 버스 부하 ---- */
    can_periodic set[3];
    memset(set, 0, sizeof set);
    set[0].msg.len = 8; set[0].period_us = 1000;                           /* 222 us / 1 ms */
    double load = -1;
    CHECK(can_bus_load(set, 1, &r500, T_NOSTUFF, &load) == 0 && near(load, 22.2, 1e-9));
    set[1].msg.fdf = 1; set[1].msg.brs = 1; set[1].msg.len = 64; set[1].period_us = 2000;   /* 333 us / 2 ms */
    CHECK(can_bus_load(set, 2, &r500_2m, T_NOSTUFF, &load) == 0 && near(load, 22.2 + 16.65, 1e-9));
    set[2].period_us = 0;
    CHECK(can_bus_load(set, 3, &r500_2m, T_NOSTUFF, &load) == CAN_EARG);
    set[2].period_us = 100; set[2].msg.len = 9;
    CHECK(can_bus_load(set, 3, &r500_2m, T_NOSTUFF, &load) == CAN_ELEN);

    /* 로봇 관절 버스: 1 kHz, 16 B 명령 + 16 B 상태 / 모터, 최악 스터핑 */
    double la = -1;
    can_rates c1m = { 1000000, 1000000 };
    CHECK(robot_max_motors(&c1m, 0, 0, 16, 1000, 70.0, 64, &la) == 1 && near(la, 54.0, 1e-9));   /* 4 × 135 us */
    CHECK(robot_max_motors(&r1m_5m, 1, 1, 16, 1000, 70.0, 64, &la) == 4 && near(la, 56.8, 1e-9)); /* 2 × 71 us */
    CHECK(robot_max_motors(&r1m_5m, 1, 1, 16, 1000, 100.0, 64, &la) == 7 && near(la, 99.4, 1e-9));
    CHECK(robot_max_motors(&r1m_5m, 1, 1, 65, 1000, 70.0, 64, &la) == CAN_ELEN);
    CHECK(robot_max_motors(&bad2, 1, 1, 16, 1000, 70.0, 64, &la) == CAN_ERATE);
    ok = 1;                                                    /* 모터 수 경계: N 은 들어가고 N+1 은 안 들어감 */
    const can_rates rc[] = { { 500000, 500000 }, { 1000000, 1000000 }, { 500000, 2000000 }, { 1000000, 2000000 }, { 1000000, 5000000 }, { 1000000, 8000000 } };
    for (size_t i = 0; i < sizeof rc / sizeof rc[0]; i++)
        for (int fd = 0; fd < 2; fd++) {
            double l1 = 0, l2 = 0;
            int k1 = robot_max_motors(&rc[i], fd, 1, 16, 1000, 70.0, 64, &l1);
            int k2 = robot_max_motors(&rc[i], fd, 1, 16, 1000, 1e9, 64, &l2);      /* 한계 없음 → 64 */
            ok &= k1 >= 0 && k1 < 64 && k2 == 64 && l1 <= 70.0 + 1e-9 && (l1 + l2 / 64) > 70.0;
        }
    CHECK(ok);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
