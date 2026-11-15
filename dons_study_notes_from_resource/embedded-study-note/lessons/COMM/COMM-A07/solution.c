/* COMM-A07 ISO-TP (ISO 15765-2): CAN 위 분할·재조립과 흐름 제어
 *
 * 모델 (이 레슨의 정의 — ISO 15765-2:2016 세부 규칙은 스펙 확인 권장):
 *   - Classic CAN, 표준 11비트 ID, normal addressing (연결 = CAN ID 쌍). pad=true 면 모든 프레임 DLC 8, 빈칸 0xCC.
 *   - PCI: SF 0x0L (L 1..7) / FF 0x1L LL (FF_DL 8..4095; FF_DL=0 의 32비트 escape 는 미지원 → 무시)
 *          CF 0x2N (SN 1,2,..,15,0,1,..) / FC 0x3S BS STmin (S: 0 CTS, 1 WAIT, 2 OVFLW, 그 외 = invalid).
 *   - 1 tick = cfg.tick_us µs. 버스 = 프레임 단위 공유 버스 (COMM-M05 모델 축약판: 기다리는 프레임 중 가장 낮은 ID 가 이김).
 *     한 tick: ① 전송이 끝난 프레임 배달 + 송신 확인 ② 링크마다 poll (확인 처리·다음 CF·타이머) ③ 버스가 비면 중재 시작.
 *   - 타이머: N_As (송신 확인), N_Bs (FC 대기), N_Cr (CF 대기). N_Ar/N_Br/N_Cs 는 따로 두지 않음 (FC 는 즉시 보냄).
 *   - STmin 은 같은 블록 안 CF 끝 → 다음 CF 시작 간격. FC 뒤 첫 CF 는 바로 보낸다 (모델 정의, Linux can-isotp 관찰과 같음).
 * Part A: PCI 해석·STmin 변환, 송신 상태 기계 (SF/FF/CF, BS·STmin), 수신 재조립 (OVFLW 판단, 새 FF/SF 가 오면 재시작)
 * Part B: 타이머 (N_As/N_Bs/N_Cr), FC.WAIT 결정 (N_WFTmax), 결함 주입 (손실·중복·SN·OVFLW·invalid FS·버스 잼)
 * Part C: 프레임 비트 수(COMM-M04)와 전송 시간 공식, 게이트웨이 (store-and-forward, 버퍼 잠금 → FC.WAIT)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================== CAN 프레임 · ISO-TP 상수 ===================== */
typedef struct { uint32_t id; uint8_t dlc; uint8_t d[8]; } can_msg;

enum { PCI_BAD = -1, PCI_SF = 0, PCI_FF = 1, PCI_CF = 2, PCI_FC = 3 };
enum { FS_ABORT = -1, FS_CTS = 0, FS_WAIT = 1, FS_OVFLW = 2 };
enum { ISOTP_MAX = 4095, PAD = 0xCC };

/* N_Result (ISO 15765-2 의 이름) */
typedef enum { N_OK, N_TIMEOUT_A, N_TIMEOUT_BS, N_TIMEOUT_CR, N_WRONG_SN, N_INVALID_FS,
               N_UNEXP_PDU, N_WFT_OVRN, N_BUFFER_OVFLW, N_NRES } n_result;
static const char *const N_NAME[N_NRES + 1] = { "N_OK", "N_TIMEOUT_A", "N_TIMEOUT_Bs", "N_TIMEOUT_Cr",
    "N_WRONG_SN", "N_INVALID_FS", "N_UNEXP_PDU", "N_WFT_OVRN", "N_BUFFER_OVFLW", "(none)" };

typedef struct {
    int      type;            /* PCI_SF/FF/CF/FC, 형식이 틀리면 PCI_BAD */
    uint16_t dl;              /* SF_DL 또는 FF_DL */
    uint8_t  sn;              /* CF 순번 0..15 */
    uint8_t  fs, bs, stmin;   /* FC */
    const uint8_t *data;      /* 이 프레임의 데이터 시작 */
    uint8_t  n;               /* 이 프레임에 실린 데이터 바이트 수 (CF 는 dlc-1, 패딩 포함일 수 있음) */
} pci_t;

/* ===================== 링크 (한 연결의 한쪽 끝: 송신 반쪽 + 수신 반쪽) ===================== */
typedef struct isotp_link isotp_link;
typedef void (*isotp_cb)(isotp_link *l, n_result r, uint32_t now, void *ctx);

typedef struct {
    uint32_t tx_id, rx_id;    /* 내가 보내는 ID (데이터·FC 공통), 내가 듣는 ID */
    bool     pad;             /* true: DLC 8 + 0xCC 패딩 */
    uint8_t  bs, stmin;       /* 내가 수신자일 때 FC 에 실어 보내는 값 */
    uint8_t  wftmax;          /* 연속 FC.WAIT 최대 횟수 (N_WFTmax) */
    uint32_t tick_us;         /* 1 tick 의 µs (STmin 변환용) */
    uint32_t n_as, n_bs, n_cr, wait_gap;   /* tick */
} isotp_cfg;

enum { TX_IDLE, TX_SF, TX_FF, TX_CF, TX_WAIT_FC };
enum { RX_IDLE, RX_CF, RX_WAIT };
enum { TXQ = 4, ROLE_TX = 0, ROLE_FC = 1, ROLE_RAW = 2 };

struct isotp_link {
    isotp_cfg cfg;
    /* 송신 메일박스 (작은 FIFO). role: 0 = 송신 반쪽(SF/FF/CF), 1 = 수신 반쪽(FC), 2 = 테스트용 raw 프레임 */
    can_msg  q[TXQ]; uint8_t qrole[TXQ]; int qh, qn; uint32_t q_full;
    /* 송신 반쪽 */
    int      tx_state; const uint8_t *tx_buf; uint16_t tx_len, tx_off;
    uint8_t  tx_sn, tx_bs, tx_bs_left; uint32_t tx_stmin_t;
    bool     tx_inflight, tx_conf;
    uint32_t tx_conf_deadline, tx_next_at, tx_fc_deadline;
    uint32_t tx_fc_wait, tx_fc_ignored;
    /* 수신 반쪽 */
    int      rx_state; uint8_t *rx_buf; uint16_t rx_cap, rx_len, rx_off;
    uint8_t  rx_ff[6], rx_sn, rx_bs_left, rx_wft;
    uint32_t rx_cr_deadline, rx_wait_next, rx_busy_until; bool rx_lock;
    uint16_t rx_last_len; uint32_t rx_ignored, rx_ovflw_sent, rx_wait_sent;
    /* 결과 (indication / confirm) */
    uint32_t tx_ind[N_NRES], rx_ind[N_NRES]; n_result tx_last, rx_last; uint32_t tx_last_at, rx_last_at;
    isotp_cb on_tx, on_rx; void *cb_ctx;
};

/* ---------- 주어진 도우미 (HELPER = 시작 코드에서는 TODO 안에서만 쓰여 "안 쓰임" 경고가 나지 않게) ---------- */
#define HELPER __attribute__((unused)) static
HELPER uint32_t us_to_ticks(const isotp_link *l, uint32_t us) { return (us + l->cfg.tick_us - 1) / l->cfg.tick_us; }

static bool q_push(isotp_link *l, const can_msg *m, int role)
{
    if (l->qn == TXQ) { l->q_full++; return false; }
    int i = (l->qh + l->qn) % TXQ;
    l->q[i] = *m; l->qrole[i] = (uint8_t)role; l->qn++;
    return true;
}

HELPER void q_flush_role(isotp_link *l, int role)    /* 메일박스 abort: 그 role 의 대기 프레임만 지운다 */
{
    can_msg keep[TXQ]; uint8_t kr[TXQ]; int k = 0;
    for (int j = 0; j < l->qn; j++) {
        int i = (l->qh + j) % TXQ;
        if (l->qrole[i] != role) { keep[k] = l->q[i]; kr[k++] = l->qrole[i]; }
    }
    for (int j = 0; j < k; j++) { l->q[j] = keep[j]; l->qrole[j] = kr[j]; }
    l->qh = 0; l->qn = k;
}

static void frame_put(isotp_link *l, int role, const uint8_t *pci, int npci, const uint8_t *data, int n)
{
    can_msg m = { .id = l->cfg.tx_id };
    memcpy(m.d, pci, (size_t)npci);
    if (n > 0) memcpy(m.d + npci, data, (size_t)n);
    m.dlc = (uint8_t)(npci + n);
    if (l->cfg.pad) { memset(m.d + m.dlc, PAD, (size_t)(8 - m.dlc)); m.dlc = 8; }
    q_push(l, &m, role);
}

/* 송신 반쪽이 프레임 하나를 메일박스에 넣는다 → 송신 확인(N_As) 기다림 시작 */
HELPER void tx_send(isotp_link *l, const uint8_t *pci, int npci, const uint8_t *data, int n, uint32_t now)
{
    frame_put(l, ROLE_TX, pci, npci, data, n);
    l->tx_inflight = true; l->tx_conf = false;
    l->tx_conf_deadline = now + l->cfg.n_as;
}

static void fc_send(isotp_link *l, uint8_t fs)
{
    uint8_t pci[3] = { (uint8_t)(0x30 | fs), l->cfg.bs, l->cfg.stmin };
    frame_put(l, ROLE_FC, pci, 3, NULL, 0);
}

HELPER void tx_finish(isotp_link *l, n_result r, uint32_t now)
{
    l->tx_state = TX_IDLE; l->tx_inflight = false; l->tx_conf = false;
    l->tx_ind[r]++; l->tx_last = r; l->tx_last_at = now;
    if (l->on_tx) l->on_tx(l, r, now, l->cb_ctx);
}

static void rx_finish(isotp_link *l, n_result r, uint32_t now)
{
    l->rx_state = RX_IDLE;
    if (r == N_OK) l->rx_last_len = l->rx_len;
    l->rx_ind[r]++; l->rx_last = r; l->rx_last_at = now;
    if (l->on_rx) l->on_rx(l, r, now, l->cb_ctx);
}

HELPER bool rx_busy(const isotp_link *l, uint32_t now) { return l->rx_lock || now < l->rx_busy_until; }

/* ===================== Part A — PCI 와 STmin ===================== */

/* TODO A1: STmin 바이트 → µs. 0x00–0x7F = 0–127 ms, 0xF1–0xF9 = 100–900 µs,
 *          나머지(0x80–0xF0, 0xFA–0xFF)는 예약값 → 가장 긴 값 0x7F(127 ms)로 취급. */
static uint32_t stmin_to_us(uint8_t raw)
{
    if (raw <= 0x7F) return raw * 1000u;
    if (raw >= 0xF1 && raw <= 0xF9) return (uint32_t)(raw - 0xF0) * 100u;
    return 127000u;
}

/* TODO A2: CAN 프레임의 PCI 를 해석해 *p 를 채우고 type 을 돌려준다. 형식이 틀리면 PCI_BAD.
 *   SF: SF_DL 1..7 이고 dlc-1 이상 / FF: dlc==8, FF_DL >= 8 (0 = escape 미지원, 1..7 = SF 였어야 함)
 *   CF: dlc >= 2 / FC: dlc >= 3 / 상위 니블 4..F: BAD / dlc 0 또는 > 8: BAD */
static int isotp_parse(const can_msg *m, pci_t *p)
{
    memset(p, 0, sizeof *p);
    p->type = PCI_BAD;
    if (m->dlc < 1 || m->dlc > 8) return PCI_BAD;
    int t = m->d[0] >> 4, lo = m->d[0] & 0x0F;
    switch (t) {
    case PCI_SF:
        if (lo == 0 || lo > 7 || lo > m->dlc - 1) return PCI_BAD;   /* SF_DL 0 = CAN FD escape (미지원) */
        p->dl = (uint16_t)lo; p->data = m->d + 1; p->n = (uint8_t)lo;
        break;
    case PCI_FF:
        if (m->dlc != 8) return PCI_BAD;
        p->dl = (uint16_t)(lo << 8 | m->d[1]);
        if (p->dl < 8) return PCI_BAD;          /* 0 = 32비트 escape (2016판, 미지원), 1..7 = SF 로 보냈어야 함 */
        p->data = m->d + 2; p->n = 6;
        break;
    case PCI_CF:
        if (m->dlc < 2) return PCI_BAD;
        p->sn = (uint8_t)lo; p->data = m->d + 1; p->n = (uint8_t)(m->dlc - 1);
        break;
    case PCI_FC:
        if (m->dlc < 3) return PCI_BAD;
        p->fs = (uint8_t)lo; p->bs = m->d[1]; p->stmin = m->d[2];
        break;
    default:
        return PCI_BAD;
    }
    return p->type = t;
}

/* ===================== Part B — FC.WAIT 결정 · 타이머 (Part A 의 상태 기계가 부른다) ===================== */

/* TODO B1: 수신자가 지금 보낼 FC 의 FlowStatus. 버퍼가 비어 있으면 CTS,
 *          바쁘면(rx_busy) 연속 WAIT 가 wftmax 미만일 때 WAIT, 아니면 FS_ABORT (→ N_WFT_OVRN). */
static int rx_fc_status(isotp_link *l, uint32_t now)
{
    if (!rx_busy(l, now)) return FS_CTS;
    if (l->rx_wft < l->cfg.wftmax) return FS_WAIT;
    return FS_ABORT;
}

/* 주어짐: FC 를 하나 보내고 수신 상태를 바꾼다 (FF 직후, 블록 끝, WAIT 재시도 때 부른다) */
HELPER void rx_flow(isotp_link *l, uint32_t now)
{
    int fs = rx_fc_status(l, now);
    if (fs == FS_CTS) {
        if (l->rx_off == 0) { memcpy(l->rx_buf, l->rx_ff, 6); l->rx_off = 6; }   /* FF 데이터는 CTS 를 줄 때 버퍼로 */
        fc_send(l, FS_CTS);
        l->rx_state = RX_CF; l->rx_bs_left = l->cfg.bs; l->rx_wft = 0;
        l->rx_cr_deadline = now + l->cfg.n_cr;
    } else if (fs == FS_WAIT) {
        fc_send(l, FS_WAIT);
        l->rx_wait_sent++; l->rx_wft++;
        l->rx_state = RX_WAIT; l->rx_wait_next = now + l->cfg.wait_gap;
    } else {
        rx_finish(l, N_WFT_OVRN, now);
    }
}

/* TODO B2: 송신 타이머. 보낸 프레임의 확인이 N_As 안에 안 오면 메일박스의 송신 프레임을 지우고
 *          N_TIMEOUT_A, FC 를 기다리다 N_Bs 가 지나면 N_TIMEOUT_Bs. (마감 = now >= deadline) */
static void tx_timers(isotp_link *l, uint32_t now)
{
    if (l->tx_state == TX_IDLE) return;
    if (l->tx_inflight && now >= l->tx_conf_deadline) {
        q_flush_role(l, ROLE_TX);                    /* 컨트롤러 메일박스 abort */
        tx_finish(l, N_TIMEOUT_A, now);
    } else if (l->tx_state == TX_WAIT_FC && now >= l->tx_fc_deadline) {
        tx_finish(l, N_TIMEOUT_BS, now);
    }
}

/* TODO B3: 수신 타이머. CF 를 기다리다 N_Cr 이 지나면 N_TIMEOUT_Cr,
 *          WAIT 를 보낸 상태에서 wait_gap 이 지나면 rx_flow 로 다시 판단 (CTS / 또 WAIT / 포기). */
static void rx_timers(isotp_link *l, uint32_t now)
{
    if (l->rx_state == RX_CF && now >= l->rx_cr_deadline) rx_finish(l, N_TIMEOUT_CR, now);
    else if (l->rx_state == RX_WAIT && now >= l->rx_wait_next) rx_flow(l, now);
}

/* ===================== Part A — 송신 상태 기계 ===================== */

/* TODO A3: 메시지 송신 시작. 바쁘면 -1, len 0 또는 > 4095 이면 -2.
 *   len <= 7 → SF (PCI 0x0L), 그 외 → FF (0x1L LL + 데이터 6바이트), tx_sn = 1. tx_send 로 넣는다. */
static int tx_start(isotp_link *l, const uint8_t *buf, uint16_t len, uint32_t now)
{
    if (l->tx_state != TX_IDLE) return -1;
    if (len == 0 || len > ISOTP_MAX) return -2;
    l->tx_buf = buf; l->tx_len = len;
    if (len <= 7) {
        uint8_t pci = (uint8_t)len;
        tx_send(l, &pci, 1, buf, len, now);
        l->tx_off = len; l->tx_state = TX_SF;
    } else {
        uint8_t pci[2] = { (uint8_t)(0x10 | len >> 8), (uint8_t)(len & 0xFF) };
        tx_send(l, pci, 2, buf, 6, now);
        l->tx_off = 6; l->tx_sn = 1; l->tx_state = TX_FF;
    }
    return 0;
}

/* TODO A4: 매 tick. (1) tx_conf(송신 확인)가 왔으면 처리:
 *     SF 또는 마지막 CF → N_OK / FF 또는 블록의 마지막 CF(BS>0) → TX_WAIT_FC, N_Bs 시작 / 그 외 CF → 다음 CF 는 now + STmin.
 *     (상태가 SF/FF/CF 가 아니면 늦게 온 확인이므로 무시)
 *   (2) TX_CF 이고 보낸 프레임이 없고 now >= tx_next_at 이면 다음 CF (0x20|SN, 최대 7바이트), SN 은 0..15 로 돈다. */
static void tx_poll(isotp_link *l, uint32_t now)
{
    if (l->tx_conf) {
        l->tx_conf = false;
        if (l->tx_state == TX_SF || l->tx_state == TX_FF || l->tx_state == TX_CF) {
            l->tx_inflight = false;
            if (l->tx_state == TX_SF || (l->tx_state == TX_CF && l->tx_off == l->tx_len)) {
                tx_finish(l, N_OK, now);
                return;
            }
            if (l->tx_state == TX_FF || (l->tx_bs != 0 && --l->tx_bs_left == 0)) {
                l->tx_state = TX_WAIT_FC; l->tx_fc_deadline = now + l->cfg.n_bs;
                return;
            }
            l->tx_next_at = now + l->tx_stmin_t;
        }
    }
    if (l->tx_state == TX_CF && !l->tx_inflight && now >= l->tx_next_at) {
        uint16_t left = (uint16_t)(l->tx_len - l->tx_off);
        int n = left < 7 ? left : 7;
        uint8_t pci = (uint8_t)(0x20 | l->tx_sn);
        tx_send(l, &pci, 1, l->tx_buf + l->tx_off, n, now);
        l->tx_off = (uint16_t)(l->tx_off + n);
        l->tx_sn = (uint8_t)((l->tx_sn + 1) & 0x0F);
    }
}

/* TODO A5: FC 수신. FC 를 기다리는 중(TX_WAIT_FC)이 아니면 무시(tx_fc_ignored++).
 *   CTS → BS·STmin 저장, TX_CF, 첫 CF 는 바로 / WAIT → tx_fc_wait++, N_Bs 다시 시작
 *   OVFLW → N_BUFFER_OVFLW / 그 외 FS → N_INVALID_FS */
static void tx_on_fc(isotp_link *l, const pci_t *p, uint32_t now)
{
    if (l->tx_state != TX_WAIT_FC) { l->tx_fc_ignored++; return; }
    switch (p->fs) {
    case FS_CTS:
        l->tx_bs = l->tx_bs_left = p->bs;
        l->tx_stmin_t = us_to_ticks(l, stmin_to_us(p->stmin));
        l->tx_state = TX_CF; l->tx_next_at = now;
        break;
    case FS_WAIT:
        l->tx_fc_wait++; l->tx_fc_deadline = now + l->cfg.n_bs;
        break;
    case FS_OVFLW:
        tx_finish(l, N_BUFFER_OVFLW, now);
        break;
    default:
        tx_finish(l, N_INVALID_FS, now);
        break;
    }
}

/* ===================== Part A — 수신 상태 기계 ===================== */

/* TODO A6: SF 또는 FF 수신. 수신 중(RX_IDLE 아님)이었으면 그 수신을 N_UNEXP_PDU 로 끝내고 새 것으로 시작.
 *   SF: 버퍼가 바쁘거나 SF_DL > cap 이면 무시(rx_ignored++), 아니면 복사 후 N_OK.
 *   FF: FF_DL > cap 이면 FC OVFLW (rx_ovflw_sent++) 하고 IDLE 유지.
 *       아니면 첫 6바이트는 rx_ff 에 보관, rx_len/off(0)/sn(1)/wft(0) 설정 후 rx_flow (CTS 일 때 버퍼로 복사됨). */
static void rx_first(isotp_link *l, const pci_t *p, uint32_t now)
{
    if (l->rx_state != RX_IDLE) rx_finish(l, N_UNEXP_PDU, now);
    if (p->type == PCI_SF) {
        if (rx_busy(l, now) || p->dl > l->rx_cap) { l->rx_ignored++; return; }
        memcpy(l->rx_buf, p->data, p->dl);
        l->rx_len = p->dl;
        rx_finish(l, N_OK, now);
        return;
    }
    if (p->dl > l->rx_cap) { fc_send(l, FS_OVFLW); l->rx_ovflw_sent++; return; }
    memcpy(l->rx_ff, p->data, 6);
    l->rx_len = p->dl; l->rx_off = 0; l->rx_sn = 1; l->rx_wft = 0;
    rx_flow(l, now);
}

/* TODO A7: CF 수신. RX_CF 가 아니면 무시. SN 이 기대값과 다르면 N_WRONG_SN.
 *   남은 길이 need 에 대해 n = min(7, need), 프레임 데이터가 n 보다 짧으면 무시.
 *   복사 → off += n, SN 증가(0..15). 다 받았으면 N_OK. 아니면 N_Cr 다시 시작, BS>0 이면 블록 끝에서 rx_flow. */
static void rx_cf(isotp_link *l, const pci_t *p, uint32_t now)
{
    if (l->rx_state != RX_CF) { l->rx_ignored++; return; }
    if (p->sn != l->rx_sn) { rx_finish(l, N_WRONG_SN, now); return; }
    uint16_t need = (uint16_t)(l->rx_len - l->rx_off);
    int n = need < 7 ? need : 7;
    if (p->n < n) { l->rx_ignored++; return; }
    memcpy(l->rx_buf + l->rx_off, p->data, (size_t)n);
    l->rx_off = (uint16_t)(l->rx_off + n);
    l->rx_sn = (uint8_t)((l->rx_sn + 1) & 0x0F);
    if (l->rx_off == l->rx_len) { rx_finish(l, N_OK, now); return; }
    l->rx_cr_deadline = now + l->cfg.n_cr;
    if (l->cfg.bs != 0 && --l->rx_bs_left == 0) rx_flow(l, now);
}

/* 주어짐: 링크가 받은 CAN 프레임 디스패치, 매 tick poll */
static void link_rx(isotp_link *l, const can_msg *m, uint32_t now)
{
    pci_t p;
    switch (isotp_parse(m, &p)) {
    case PCI_SF: case PCI_FF: rx_first(l, &p, now); break;
    case PCI_CF: rx_cf(l, &p, now); break;
    case PCI_FC: tx_on_fc(l, &p, now); break;
    default: l->rx_ignored++; break;
    }
}

static void link_poll(isotp_link *l, uint32_t now)
{
    tx_poll(l, now);
    tx_timers(l, now);
    rx_timers(l, now);
}

/* ===================== Part C — 프레임 비트 수 · 전송 시간 ===================== */

/* TODO C1: 표준 ID 데이터 프레임 + IFS 3 의 비트 수 (COMM-M04): 47 + 8n (n = min(dlc, 8)).
 *          worst 이면 Davis/Burns 최악 스터프 ⌊(34 + 8n − 1)/4⌋ 를 더한다. */
static int can_frame_bits(int dlc, bool worst)
{
    int n = dlc > 8 ? 8 : dlc;
    int bits = 47 + 8 * n;
    if (worst) bits += (34 + 8 * n - 1) / 4;
    return bits;
}

/* TODO C2: len 바이트 메시지의 FF 시작 → 마지막 CF 끝 시간(µs). 프레임 시간 = bits / bitrate.
 *   SF: 프레임 1개 (pad 면 DLC 8, 아니면 1+len). FF: DLC 8.
 *   ncf = ⌈(len−6)/7⌉, 마지막 CF 데이터 = (len−6) − 7(ncf−1), 블록 수 = nfc = BS ? ⌈ncf/BS⌉ : 1,
 *   FC 는 블록마다 1개 (pad 면 DLC 8, 아니면 3), STmin 간격은 블록 안에서만: (ncf − nfc) 번. */
static double isotp_transfer_us(uint16_t len, uint8_t bs, uint8_t stmin, uint32_t bitrate, bool pad, bool worst)
{
    double bit_us = 1e6 / bitrate;
    if (len <= 7) return can_frame_bits(pad ? 8 : 1 + len, worst) * bit_us;
    int ncf = (len - 6 + 6) / 7;
    int last = (len - 6) - 7 * (ncf - 1);
    int nfc = bs ? (ncf + bs - 1) / bs : 1;
    double bits = can_frame_bits(8, worst)                           /* FF */
                + (double)nfc * can_frame_bits(pad ? 8 : 3, worst)   /* FC */
                + (double)(ncf - 1) * can_frame_bits(8, worst)       /* 가득 찬 CF */
                + can_frame_bits(pad ? 8 : 1 + last, worst);         /* 마지막 CF */
    return bits * bit_us + (double)(ncf - nfc) * stmin_to_us(stmin);
}

/* ===================== 공유 CAN 버스 (COMM-M05 모델 축약판, 프레임 단위) ===================== */
enum { MAXLINK = 8, LOGN = 64, F_DELIVER = 0, F_DROP = 1, F_DUP = 2 };
typedef struct { uint32_t start, end; can_msg m; } trace_rec;
typedef int  (*fault_fn)(void *ctx, can_msg *m);            /* 수신자가 볼 프레임을 바꾸거나 버림/중복 */
typedef void (*trace_fn)(void *ctx, const trace_rec *r);

typedef struct {
    isotp_link *link[MAXLINK]; int nlink;
    uint32_t now, tick_us, bitrate, frame_ticks; bool worst_stuff, jam;
    bool busy; int cur_owner, cur_role; trace_rec cur;
    uint32_t nframes;
    fault_fn fault; void *fault_ctx;
    trace_fn on_trace; void *trace_ctx;
    trace_rec log[LOGN]; int nlog;
} can_bus;

static uint32_t bus_frame_ticks(const can_bus *b, uint8_t dlc)
{
    if (b->bitrate == 0) return b->frame_ticks ? b->frame_ticks : 1;
    uint64_t ns = (uint64_t)can_frame_bits(dlc, b->worst_stuff) * 1000000000ull / b->bitrate;
    uint64_t tick_ns = (uint64_t)b->tick_us * 1000u;
    uint64_t t = (ns + tick_ns - 1) / tick_ns;
    return t ? (uint32_t)t : 1;
}

static void sim_step(can_bus *b)
{
    uint32_t now = b->now;
    if (b->busy && now >= b->cur.end) {                         /* ① 전송 끝: 배달 + 송신 확인 */
        b->busy = false;
        trace_rec r = b->cur;
        if (b->nlog < LOGN) b->log[b->nlog++] = r;
        if (b->on_trace) b->on_trace(b->trace_ctx, &r);
        can_msg m = r.m;
        int act = b->fault ? b->fault(b->fault_ctx, &m) : F_DELIVER;
        int reps = act == F_DROP ? 0 : act == F_DUP ? 2 : 1;
        b->nframes++;
        for (int k = 0; k < reps; k++)
            for (int i = 0; i < b->nlink; i++)
                if (i != b->cur_owner && b->link[i]->cfg.rx_id == m.id) link_rx(b->link[i], &m, now);
        if (b->cur_role == ROLE_TX) b->link[b->cur_owner]->tx_conf = true;
    }
    for (int i = 0; i < b->nlink; i++) link_poll(b->link[i], now);   /* ② */
    if (!b->busy && !b->jam) {                                   /* ③ 중재: 가장 낮은 ID */
        int w = -1;
        for (int i = 0; i < b->nlink; i++) {
            isotp_link *l = b->link[i];
            if (l->qn && (w < 0 || l->q[l->qh].id < b->link[w]->q[b->link[w]->qh].id)) w = i;
        }
        if (w >= 0) {
            isotp_link *l = b->link[w];
            b->cur.m = l->q[l->qh]; b->cur_role = l->qrole[l->qh];
            l->qh = (l->qh + 1) % TXQ; l->qn--;
            b->cur_owner = w; b->cur.start = now; b->cur.end = now + bus_frame_ticks(b, b->cur.m.dlc);
            b->busy = true;
        }
    }
    b->now++;
}

/* ===================== Part C — 게이트웨이 (store-and-forward) ===================== */
typedef struct { isotp_link *in, *out; uint32_t fwd_ok, fwd_fail; } gateway;

/* TODO C3: in 쪽 수신 완료 콜백. N_OK 면 in 버퍼를 잠그고(rx_lock) 그 버퍼 그대로 out 으로 송신 시작 (복사 없음).
 *          tx_start 가 실패하면 잠금을 풀고 fwd_fail++. */
static void gw_on_rx(isotp_link *in, n_result r, uint32_t now, void *ctx)
{
    gateway *g = ctx;
    if (r != N_OK) return;
    in->rx_lock = true;
    if (tx_start(g->out, in->rx_buf, in->rx_last_len, now) != 0) { in->rx_lock = false; g->fwd_fail++; }
}

/* TODO C4: out 쪽 송신 완료 콜백. in 버퍼 잠금을 풀고 결과에 따라 fwd_ok / fwd_fail. */
static void gw_on_tx(isotp_link *out, n_result r, uint32_t now, void *ctx)
{
    gateway *g = ctx;
    (void)out; (void)now;
    g->in->rx_lock = false;
    if (r == N_OK) g->fwd_ok++; else g->fwd_fail++;
}

/* ===================== 테스트 하네스 ===================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs_state = 0xA07150u;
static uint32_t xorshift32(void)
{
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}

static isotp_cfg cfg_make(uint32_t tx, uint32_t rx, uint32_t tick_us, bool pad, uint8_t bs, uint8_t stmin)
{
    isotp_cfg c = { .tx_id = tx, .rx_id = rx, .pad = pad, .bs = bs, .stmin = stmin, .wftmax = 4,
                    .tick_us = tick_us, .n_as = 1000000u / tick_us, .n_bs = 1000000u / tick_us,
                    .n_cr = 1000000u / tick_us, .wait_gap = 300000u / tick_us };   /* 1000 ms / 300 ms (모델 값) */
    return c;
}

static void link_init(isotp_link *l, isotp_cfg c, uint8_t *buf, uint16_t cap)
{
    memset(l, 0, sizeof *l);
    l->cfg = c; l->rx_buf = buf; l->rx_cap = cap;
    l->tx_last = l->rx_last = N_NRES;                  /* 아직 결과 없음 */
}

static void bus_init(can_bus *b, uint32_t tick_us, uint32_t bitrate)
{
    memset(b, 0, sizeof *b);
    b->tick_us = tick_us; b->bitrate = bitrate; b->frame_ticks = 1;
}

static void bus_add(can_bus *b, isotp_link *l) { if (b->nlink < MAXLINK) b->link[b->nlink++] = l; }

static bool all_idle(const can_bus *b)
{
    if (b->busy) return false;
    for (int i = 0; i < b->nlink; i++) {
        const isotp_link *l = b->link[i];
        if (l->tx_state != TX_IDLE || l->rx_state != RX_IDLE || l->qn) return false;
    }
    return true;
}

static uint32_t run_idle(can_bus *b, uint32_t cap)
{
    uint32_t t0 = b->now;
    do sim_step(b); while (!all_idle(b) && b->now - t0 < cap);
    return b->now - t0;
}

static void run_ticks(can_bus *b, uint32_t n) { while (n--) sim_step(b); }

static void fmt_frame(const can_msg *m, char *s, size_t cap)
{
    int k = snprintf(s, cap, "%03X#", (unsigned)m->id);
    for (int i = 0; i < m->dlc && k > 0 && (size_t)k + 3 <= cap; i++) k += snprintf(s + k, cap - (size_t)k, "%02X", m->d[i]);
}

static can_msg parse_frame(const char *s)                /* candump -L 형식 "7E0#1014000102030405" */
{
    can_msg m = { 0 };
    char *end;
    m.id = (uint32_t)strtoul(s, &end, 16);
    if (*end == '#') end++;
    while (m.dlc < 8 && end[0] && end[1]) {
        char h[3] = { end[0], end[1], 0 };
        m.d[m.dlc++] = (uint8_t)strtoul(h, NULL, 16);
        end += 2;
    }
    return m;
}

static void raw_send(isotp_link *raw, const char *s) { can_msg m = parse_frame(s); q_push(raw, &m, ROLE_RAW); }

/* 결함 주입: id·PCI 종류가 맞는 nth 번째 프레임에 action (set_b0 >= 0 이면 첫 바이트를 바꿔 배달) */
typedef struct { uint32_t id; int type, nth, action, set_b0, seen, fired; } fault_t;
static int fault_hook(void *ctx, can_msg *m)
{
    fault_t *f = ctx;
    if (m->id != f->id || (m->d[0] >> 4) != f->type) return F_DELIVER;
    if (f->seen++ != f->nth) return F_DELIVER;
    f->fired++;
    if (f->set_b0 >= 0) { m->d[0] = (uint8_t)f->set_b0; return F_DELIVER; }
    return f->action;
}

/* 트레이스 검사기: 버스에 실제로 나간 프레임만 보고 SN 순서, BS, STmin 간격을 독립적으로 확인 */
typedef struct {
    uint32_t data_id, fc_id, tick_us;
    long last_cf_end, min_slack; int cf_in_block, bs; uint32_t stmin_t; uint8_t next_sn;
    uint32_t gaps, viol, cfs, fcs;
} flow_chk;
static void chk_hook(void *ctx, const trace_rec *r)
{
    flow_chk *c = ctx;
    const can_msg *m = &r->m;
    int t = m->d[0] >> 4;
    if (m->id == c->fc_id && t == 3 && (m->d[0] & 15) == 0) {
        uint8_t s = m->d[2];
        uint32_t us = s <= 0x7F ? s * 1000u : (s >= 0xF1 && s <= 0xF9) ? (s & 15u) * 100u : 127000u;
        c->fcs++; c->bs = m->d[1]; c->stmin_t = (us + c->tick_us - 1) / c->tick_us;
        c->cf_in_block = 0; c->last_cf_end = -1;
    } else if (m->id == c->data_id && t == 1) {
        c->next_sn = 1; c->last_cf_end = -1;
    } else if (m->id == c->data_id && t == 2) {
        c->cfs++;
        if ((m->d[0] & 15) != c->next_sn) c->viol++;
        c->next_sn = (uint8_t)((c->next_sn + 1) & 15);
        if (c->last_cf_end >= 0) {
            long slack = (long)r->start - c->last_cf_end - (long)c->stmin_t;
            c->gaps++;
            if (slack < 0) c->viol++;
            if (slack < c->min_slack) c->min_slack = slack;
        }
        if (++c->cf_in_block > c->bs && c->bs) c->viol++;
        c->last_cf_end = (long)r->end;
    }
}

/* 송신 S(0x7E0 → / ← 0x7E8) 와 수신 R(0x7E8 → / ← 0x7E0) 두 노드 */
typedef struct { can_bus b; isotp_link s, r; uint8_t rbuf[ISOTP_MAX]; } duo;
static duo D;
static void duo_init(uint32_t tick_us, uint32_t bitrate, bool pad, uint8_t bs, uint8_t stmin, uint16_t cap)
{
    bus_init(&D.b, tick_us, bitrate);
    link_init(&D.s, cfg_make(0x7E0, 0x7E8, tick_us, pad, 0, 0), NULL, 0);
    link_init(&D.r, cfg_make(0x7E8, 0x7E0, tick_us, pad, bs, stmin), D.rbuf, cap);
    bus_add(&D.b, &D.s); bus_add(&D.b, &D.r);
}

static int count_type(const can_bus *b, uint32_t id, int type)
{
    int n = 0;
    for (int i = 0; i < b->nlog; i++) if (b->log[i].m.id == id && (b->log[i].m.d[0] >> 4) == type) n++;
    return n;
}

static uint8_t SRC[ISOTP_MAX];

/* ---------- Part A ---------- */
static const char *const LINUX_K1[] = { "7E0#1014000102030405", "7E8#30020ACCCCCCCCCC",
    "7E0#21060708090A0B0C", "7E0#220D0E0F10111213" };
static const char *const LINUX_K2[] = { "7E0#102800070E151C23", "7E8#3002F5CCCCCCCCCC", "7E0#212A31383F464D54",
    "7E0#225B626970777E85", "7E8#3002F5CCCCCCCCCC", "7E0#238C939AA1A8AFB6", "7E0#24BDC4CBD2D9E0E7",
    "7E8#3002F5CCCCCCCCCC", "7E0#25EEF5FC030A11CC" };
static const char *const LINUX_K3[] = { "7E0#05DEADBEEF01CCCC" };
static const char *const LINUX_K4[] = { "7E0#100AA0A1A2A3A4A5", "7E8#300000", "7E0#21A6A7A8A9" };

static bool match_linux(const char *name, const uint8_t *msg, uint16_t len, bool pad, uint8_t bs, uint8_t stmin,
                        const char *const *exp, int nexp)
{
    duo_init(100, 0, pad, bs, stmin, ISOTP_MAX);
    if (tx_start(&D.s, msg, len, D.b.now) != 0) return false;
    run_idle(&D.b, 100000);
    bool ok = D.b.nlog == nexp && D.s.tx_last == N_OK && D.r.rx_ind[N_OK] == 1 &&
              D.r.rx_last_len == len && memcmp(D.rbuf, msg, len) == 0;
    printf("%s (%d frames, Linux can-isotp capture):", name, D.b.nlog);
    for (int i = 0; i < D.b.nlog; i++) {
        char s[32]; fmt_frame(&D.b.log[i].m, s, sizeof s);
        printf(" %s", s);
        if (i >= nexp || strcmp(s, exp[i]) != 0) ok = false;
    }
    printf(" -> %s\n", ok ? "match" : "MISMATCH");
    return ok;
}

static void test_part_a(void)
{
    /* A1 STmin: 256 값 전부 */
    int bad = 0;
    for (int v = 0; v < 256; v++) {
        uint32_t e = v <= 0x7F ? (uint32_t)v * 1000u : (v >= 0xF1 && v <= 0xF9) ? (uint32_t)(v - 0xF0) * 100u : 127000u;
        if (stmin_to_us((uint8_t)v) != e) bad++;
    }
    CHECK(bad == 0);
    CHECK(stmin_to_us(0xF1) == 100 && stmin_to_us(0xF9) == 900 && stmin_to_us(0x7F) == 127000);
    CHECK(stmin_to_us(0x80) == 127000 && stmin_to_us(0xF0) == 127000 && stmin_to_us(0xFA) == 127000);

    /* A2 PCI 해석 */
    pci_t p; can_msg m;
    m = parse_frame("7E0#05DEADBEEF01CCCC"); CHECK(isotp_parse(&m, &p) == PCI_SF && p.dl == 5 && p.data[0] == 0xDE);
    m = parse_frame("7E0#00DEADBEEF01CCCC"); CHECK(isotp_parse(&m, &p) == PCI_BAD);   /* SF_DL 0 (FD escape) */
    m = parse_frame("7E0#08DEADBEEF01CCCC"); CHECK(isotp_parse(&m, &p) == PCI_BAD);   /* SF_DL 8 */
    m = parse_frame("7E0#05DEADBE");         CHECK(isotp_parse(&m, &p) == PCI_BAD);   /* dlc 4 < 1+5 */
    m = parse_frame("7E0#1014000102030405"); CHECK(isotp_parse(&m, &p) == PCI_FF && p.dl == 20 && p.n == 6);
    m = parse_frame("7E0#1FFF000102030405"); CHECK(isotp_parse(&m, &p) == PCI_FF && p.dl == 4095);
    m = parse_frame("7E0#1000000010000102"); CHECK(isotp_parse(&m, &p) == PCI_BAD);   /* 32비트 escape: 미지원 */
    m = parse_frame("7E0#1007000102030405"); CHECK(isotp_parse(&m, &p) == PCI_BAD);   /* FF_DL 7 */
    m = parse_frame("7E0#10140001020304");   CHECK(isotp_parse(&m, &p) == PCI_BAD);   /* FF dlc 7 */
    m = parse_frame("7E0#2FA6A7");           CHECK(isotp_parse(&m, &p) == PCI_CF && p.sn == 15 && p.n == 2);
    m = parse_frame("7E8#3002F5");           CHECK(isotp_parse(&m, &p) == PCI_FC && p.fs == 0 && p.bs == 2 && p.stmin == 0xF5);
    m = parse_frame("7E8#3002");             CHECK(isotp_parse(&m, &p) == PCI_BAD);
    m = parse_frame("7E8#4000000000000000"); CHECK(isotp_parse(&m, &p) == PCI_BAD);
    m = parse_frame("7E8#");                 CHECK(isotp_parse(&m, &p) == PCI_BAD);

    /* A3 시작 인자 */
    duo_init(100, 0, true, 0, 0, ISOTP_MAX);
    CHECK(tx_start(&D.s, SRC, 0, 0) == -2);
    CHECK(tx_start(&D.s, SRC, 4096, 0) == -2);
    CHECK(tx_start(&D.s, SRC, 100, 0) == 0 && tx_start(&D.s, SRC, 10, 0) == -1);

    /* Linux 커널 can-isotp (kernel 7.0, vcan0) 캡처와 바이트 단위 비교 */
    uint8_t k1[20], k2[40], k3[5] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01 }, k4[10];
    for (int i = 0; i < 20; i++) k1[i] = (uint8_t)i;
    for (int i = 0; i < 40; i++) k2[i] = (uint8_t)(i * 7);
    for (int i = 0; i < 10; i++) k4[i] = (uint8_t)(0xA0 + i);
    CHECK(match_linux("K1 20B BS=2 STmin=0x0A pad", k1, 20, true, 2, 0x0A, LINUX_K1, 4));
    CHECK(match_linux("K2 40B BS=2 STmin=0xF5 pad", k2, 40, true, 2, 0xF5, LINUX_K2, 9));
    CHECK(match_linux("K3 5B SF pad", k3, 5, true, 0, 0, LINUX_K3, 1));
    CHECK(match_linux("K4 10B no pad", k4, 10, false, 0, 0, LINUX_K4, 3));

    /* SN 랩: 120 B = FF + CF 17개 → SN 1..F,0,1 */
    duo_init(100, 0, true, 0, 0, ISOTP_MAX);
    for (int i = 0; i < 120; i++) SRC[i] = (uint8_t)(i ^ 0x5A);
    tx_start(&D.s, SRC, 120, 0);
    run_idle(&D.b, 100000);
    char sn[32] = { 0 }; int k = 0;
    for (int i = 0; i < D.b.nlog && k < 31; i++)
        if ((D.b.log[i].m.d[0] >> 4) == 2) sn[k++] = "0123456789ABCDEF"[D.b.log[i].m.d[0] & 15];
    printf("SN wrap 120 B: CF SN sequence %s\n", sn);
    CHECK(strcmp(sn, "123456789ABCDEF01") == 0 && D.r.rx_ind[N_OK] == 1 && memcmp(D.rbuf, SRC, 120) == 0);

    /* 길이 1..4095 전수 왕복: BS 0..20, STmin 6종 (ms·µs 범위), 트레이스 검사기로 SN·BS·STmin 확인 */
    static const uint8_t ST[6] = { 0x00, 0x01, 0x02, 0xF1, 0xF3, 0xF9 };
    flow_chk c = { .data_id = 0x7E0, .fc_id = 0x7E8, .tick_us = 100, .min_slack = 1L << 30 };
    duo_init(100, 0, true, 0, 0, ISOTP_MAX);
    D.b.on_trace = chk_hook; D.b.trace_ctx = &c;
    int fails = 0, cnt_bad = 0; uint32_t frames = 0, t0 = D.b.now;
    for (int len = 1; len <= ISOTP_MAX; len++) {
        uint8_t bs = (uint8_t)(xorshift32() % 21), st = ST[xorshift32() % 6];
        D.r.cfg.bs = bs; D.r.cfg.stmin = st;
        for (int i = 0; i < len; i++) SRC[i] = (uint8_t)xorshift32();
        uint32_t f0 = D.b.nframes, ok0 = D.r.rx_ind[N_OK];
        if (tx_start(&D.s, SRC, (uint16_t)len, D.b.now) != 0) { fails++; continue; }
        run_idle(&D.b, 2000000);
        int ncf = len <= 7 ? 0 : (len - 6 + 6) / 7;
        uint32_t exp_frames = len <= 7 ? 1u : (uint32_t)(1 + ncf + (bs ? (ncf + bs - 1) / bs : 1));
        if (D.s.tx_last != N_OK || D.r.rx_ind[N_OK] != ok0 + 1 || D.r.rx_last_len != len ||
            memcmp(D.rbuf, SRC, (size_t)len) != 0) fails++;
        if (D.b.nframes - f0 != exp_frames) cnt_bad++;
        frames += D.b.nframes - f0;
    }
    printf("round trip len 1..4095: fails=%d frame-count mismatches=%d, %u frames, %u ticks; "
           "trace: %u CF, %u FC, %u STmin gaps checked, min slack %ld ticks, violations %u\n",
           fails, cnt_bad, frames, D.b.now - t0, c.cfs, c.fcs, c.gaps, c.gaps ? c.min_slack : 0L, c.viol);
    CHECK(fails == 0);
    CHECK(cnt_bad == 0 && frames > 1000000);
    CHECK(c.viol == 0 && c.gaps > 100000 && c.min_slack == 0);
}

/* ---------- Part B ---------- */
static void test_part_b(void)
{
    for (int i = 0; i < ISOTP_MAX; i++) SRC[i] = (uint8_t)(i * 13 + 1);
    fault_t f;

    /* B1 가운데 CF 손실, BS=0 → 수신자 N_WRONG_SN, 송신자는 N_OK (끝-끝 ACK 없음) */
    duo_init(100, 0, true, 0, 0, ISOTP_MAX);
    f = (fault_t){ .id = 0x7E0, .type = 2, .nth = 2, .action = F_DROP, .set_b0 = -1 };
    D.b.fault = fault_hook; D.b.fault_ctx = &f;
    tx_start(&D.s, SRC, 50, 0); run_idle(&D.b, 100000);
    printf("B1 lost CF#3 (BS=0): rx %s, tx %s, rx ignored %u\n", N_NAME[D.r.rx_last], N_NAME[D.s.tx_last], D.r.rx_ignored);
    CHECK(f.fired == 1 && D.r.rx_ind[N_WRONG_SN] == 1 && D.r.rx_ind[N_OK] == 0);
    CHECK(D.s.tx_last == N_OK && D.r.rx_ignored == 3);

    /* B2 CF 손실, BS=4 → 수신자 N_WRONG_SN, 송신자는 블록 끝에서 FC 를 못 받아 N_TIMEOUT_Bs (= 4번째 CF 끝 + N_Bs) */
    duo_init(100, 0, true, 4, 0, ISOTP_MAX);
    f = (fault_t){ .id = 0x7E0, .type = 2, .nth = 1, .action = F_DROP, .set_b0 = -1 };
    D.b.fault = fault_hook; D.b.fault_ctx = &f;
    tx_start(&D.s, SRC, 50, 0); run_idle(&D.b, 100000);
    printf("B2 lost CF#2 (BS=4): rx %s, tx %s at tick %u (4th CF end %u + N_Bs %u)\n", N_NAME[D.r.rx_last],
           N_NAME[D.s.tx_last], D.s.tx_last_at, D.b.log[5].end, D.s.cfg.n_bs);
    CHECK(D.r.rx_ind[N_WRONG_SN] == 1 && D.s.tx_last == N_TIMEOUT_BS);
    CHECK(D.b.nlog == 6 && D.s.tx_last_at == D.b.log[5].end + D.s.cfg.n_bs);

    /* B3 마지막 CF 손실 → 수신자 N_TIMEOUT_Cr (= CF1 도착 + N_Cr), 송신자 N_OK */
    duo_init(100, 0, true, 0, 0, ISOTP_MAX);
    f = (fault_t){ .id = 0x7E0, .type = 2, .nth = 1, .action = F_DROP, .set_b0 = -1 };
    D.b.fault = fault_hook; D.b.fault_ctx = &f;
    tx_start(&D.s, SRC, 20, 0); run_idle(&D.b, 100000);
    printf("B3 lost last CF: rx %s at tick %u (CF1 at %u + N_Cr %u), tx %s\n", N_NAME[D.r.rx_last], D.r.rx_last_at,
           D.b.log[2].end, D.r.cfg.n_cr, N_NAME[D.s.tx_last]);
    CHECK(D.r.rx_last == N_TIMEOUT_CR && D.r.rx_last_at == D.b.log[2].end + D.r.cfg.n_cr && D.s.tx_last == N_OK);

    /* B4 CF 중복 (CAN 이중 수신) → N_WRONG_SN */
    duo_init(100, 0, true, 0, 0, ISOTP_MAX);
    f = (fault_t){ .id = 0x7E0, .type = 2, .nth = 0, .action = F_DUP, .set_b0 = -1 };
    D.b.fault = fault_hook; D.b.fault_ctx = &f;
    tx_start(&D.s, SRC, 50, 0); run_idle(&D.b, 100000);
    CHECK(D.r.rx_ind[N_WRONG_SN] == 1 && D.r.rx_ind[N_OK] == 0);

    /* B5 수신 버퍼가 바쁨 → FC.WAIT 3번 뒤 CTS (wftmax 4) */
    duo_init(100, 0, true, 8, 0, ISOTP_MAX);
    D.r.rx_busy_until = 7000;
    tx_start(&D.s, SRC, 300, 0); run_idle(&D.b, 200000);
    printf("B5 busy until 7000: WAIT sent %u, sender saw %u, rx %s at %u, tx %s\n", D.r.rx_wait_sent, D.s.tx_fc_wait,
           N_NAME[D.r.rx_last], D.r.rx_last_at, N_NAME[D.s.tx_last]);
    CHECK(D.r.rx_wait_sent == 3 && D.s.tx_fc_wait == 3 && count_type(&D.b, 0x7E8, 3) >= 4);
    CHECK(D.r.rx_last == N_OK && D.s.tx_last == N_OK && memcmp(D.rbuf, SRC, 300) == 0);

    /* B6 바쁨이 N_WFTmax(2) 를 넘김 → 수신자 N_WFT_OVRN, 송신자 마지막 WAIT + N_Bs 에 N_TIMEOUT_Bs */
    duo_init(100, 0, true, 8, 0, ISOTP_MAX);
    D.r.cfg.wftmax = 2; D.r.rx_busy_until = 1000000;
    tx_start(&D.s, SRC, 300, 0); run_idle(&D.b, 200000);
    printf("B6 wftmax 2: rx %s at %u, tx %s at %u (last WAIT end %u)\n", N_NAME[D.r.rx_last], D.r.rx_last_at,
           N_NAME[D.s.tx_last], D.s.tx_last_at, D.b.log[2].end);
    CHECK(D.r.rx_last == N_WFT_OVRN && D.r.rx_last_at == D.b.log[0].end + 2 * D.r.cfg.wait_gap);
    CHECK(D.s.tx_last == N_TIMEOUT_BS && D.b.nlog == 3 && D.s.tx_last_at == D.b.log[2].end + D.s.cfg.n_bs);

    /* B7 FF_DL > 수신 버퍼 → FC OVFLW → 송신자 N_BUFFER_OVFLW, CF 0개 */
    duo_init(100, 0, true, 0, 0, 100);
    tx_start(&D.s, SRC, 200, 0); run_idle(&D.b, 100000);
    char s1[32]; fmt_frame(&D.b.log[1].m, s1, sizeof s1);
    printf("B7 FF_DL 200 > cap 100: FC %s, tx %s, frames %u\n", s1, N_NAME[D.s.tx_last], D.b.nframes);
    CHECK(D.s.tx_last == N_BUFFER_OVFLW && D.b.nframes == 2 && strcmp(s1, "7E8#320000CCCCCCCCCC") == 0);
    CHECK(D.r.rx_ovflw_sent == 1 && D.r.rx_ind[N_OK] == 0 && D.r.rx_state == RX_IDLE);

    /* B8 FC 의 FS 가 깨짐(0x33) → 송신자 N_INVALID_FS, 수신자는 CF 를 기다리다 N_TIMEOUT_Cr */
    duo_init(100, 0, true, 0, 0, ISOTP_MAX);
    f = (fault_t){ .id = 0x7E8, .type = 3, .nth = 0, .set_b0 = 0x33 };
    D.b.fault = fault_hook; D.b.fault_ctx = &f;
    tx_start(&D.s, SRC, 20, 0); run_idle(&D.b, 100000);
    CHECK(D.s.tx_last == N_INVALID_FS && D.b.nframes == 2 && D.r.rx_last == N_TIMEOUT_CR);

    /* B9 버스 잼 (송신 확인이 안 옴) → N_TIMEOUT_A, 메일박스 비움 */
    duo_init(100, 0, true, 0, 0, ISOTP_MAX);
    D.b.jam = true;
    tx_start(&D.s, SRC, 20, 0); run_idle(&D.b, 100000);
    printf("B9 bus jam: tx %s at tick %u, mailbox %d\n", N_NAME[D.s.tx_last], D.s.tx_last_at, D.s.qn);
    CHECK(D.s.tx_last == N_TIMEOUT_A && D.s.tx_last_at == D.s.cfg.n_as && D.s.qn == 0);

    /* B10 reserved STmin → 127 ms, 0xF9 → 900 µs: CF1 끝 → CF2 시작 간격 (tick 100 µs) */
    static const uint8_t RS[4] = { 0x80, 0xF0, 0xFA, 0xF9 };
    static const uint32_t RG[4] = { 1270, 1270, 1270, 9 };
    for (int i = 0; i < 4; i++) {
        duo_init(100, 0, true, 0, RS[i], ISOTP_MAX);
        tx_start(&D.s, SRC, 20, 0); run_idle(&D.b, 100000);
        CHECK(D.b.nlog == 4 && D.b.log[3].start - D.b.log[2].end == RG[i] && D.r.rx_last == N_OK);
    }

    /* B11 수신자 쪽 Linux 실험 재현: raw 노드(0x7E0)가 손으로 만든 프레임을 넣는다 */
    static isotp_link raw;
    static const struct { const char *name; const char *f[5]; int nf; int res_ok, res_err, err; uint16_t len; } LX[] = {
        { "wrong SN", { "7E0#1014000102030405", "7E0#22060708090A0B0C", "7E0#210D0E0F10111213" }, 3, 0, 1, N_WRONG_SN, 0 },
        { "new FF mid", { "7E0#1014000102030405", "7E0#21060708090A0B0C", "7E0#100AA0A1A2A3A4A5",
                          "7E0#21A6A7A8A9CCCCCC" }, 4, 1, 1, N_UNEXP_PDU, 10 },
        { "SF mid", { "7E0#1014000102030405", "7E0#0311223344CCCCCC" }, 2, 1, 1, N_UNEXP_PDU, 3 },
        { "dup CF", { "7E0#1014000102030405", "7E0#21060708090A0B0C", "7E0#21060708090A0B0C",
                      "7E0#220D0E0F10111213" }, 4, 0, 1, N_WRONG_SN, 0 },
        { "ignored", { "7E0#2106070809", "7E0#1005000102030405", "7E0#00AABB", "7E0#1000000010000102",
                       "7E0#4000000000000000" }, 5, 0, 0, N_OK, 0 },
    };
    for (size_t t = 0; t < sizeof LX / sizeof LX[0]; t++) {
        duo_init(100, 0, true, 0, 0, ISOTP_MAX);
        D.b.nlink = 1; D.b.link[0] = &D.r;
        link_init(&raw, cfg_make(0x7E0, 0x7E8, 100, true, 0, 0), NULL, 0);
        raw.cfg.rx_id = 0x7FF;
        bus_add(&D.b, &raw);
        for (int i = 0; i < LX[t].nf; i++) { raw_send(&raw, LX[t].f[i]); run_ticks(&D.b, 5); }
        run_idle(&D.b, 100000);
        uint32_t errs = 0;
        for (int r = 1; r < N_NRES; r++) errs += D.r.rx_ind[r];
        bool ok = D.r.rx_ind[N_OK] == (uint32_t)LX[t].res_ok && errs == (uint32_t)LX[t].res_err &&
                  (LX[t].res_err == 0 || D.r.rx_ind[LX[t].err] == 1);
        if (LX[t].len) ok = ok && D.r.rx_last_len == LX[t].len;
        printf("B11 %-10s: rx ok %u, errors %u (%s), ignored %u\n", LX[t].name, D.r.rx_ind[N_OK], errs,
               LX[t].res_err ? N_NAME[LX[t].err] : "-", D.r.rx_ignored);
        CHECK(ok);
        if (t == 1) CHECK(D.rbuf[0] == 0xA0 && D.rbuf[9] == 0xA9);
        if (t == 2) CHECK(D.rbuf[0] == 0x11 && D.rbuf[2] == 0x33);
        if (t == 4) CHECK(D.r.rx_ignored == 5);
    }

    /* B12 송신자 쪽 Linux 실험 재현: raw 응답자(0x7E8)가 FC 를 손으로 보낸다 */
    for (int sc = 0; sc < 4; sc++) {
        duo_init(100, 0, true, 0, 0, ISOTP_MAX);
        D.b.nlink = 1; D.b.link[0] = &D.s;
        link_init(&raw, cfg_make(0x7E8, 0x7E0, 100, true, 0, 0), NULL, 0);
        raw.cfg.rx_id = 0x7FF;             /* raw 노드는 아무것도 듣지 않는다 (FF 에 OVFLW 로 답하지 않게) */
        bus_add(&D.b, &raw);
        tx_start(&D.s, SRC, 20, 0);
        if (sc == 0) { run_ticks(&D.b, 1000); raw_send(&raw, "7E8#32000000"); }
        if (sc == 1) { run_ticks(&D.b, 1000); raw_send(&raw, "7E8#33000000"); }
        if (sc == 2) {                     /* WAIT 3번 (400 ms 간격 < N_Bs) 뒤 CTS */
            for (int w = 0; w < 3; w++) { run_ticks(&D.b, w ? 4000 : 1000); raw_send(&raw, "7E8#31000000"); }
            run_ticks(&D.b, 4000); raw_send(&raw, "7E8#30000000");
        }
        if (sc == 3) {                     /* WAIT 1번 뒤 1.2 s 침묵 → N_Bs */
            run_ticks(&D.b, 1000); raw_send(&raw, "7E8#31000000");
            run_ticks(&D.b, 12000); raw_send(&raw, "7E8#30000000");
        }
        run_idle(&D.b, 100000);
        static const n_result EXP[4] = { N_BUFFER_OVFLW, N_INVALID_FS, N_OK, N_TIMEOUT_BS };
        printf("B12 sender vs raw FC #%d: tx %s, CF sent %d, WAIT seen %u\n", sc, N_NAME[D.s.tx_last],
               count_type(&D.b, 0x7E0, 2), D.s.tx_fc_wait);
        CHECK(D.s.tx_last == EXP[sc]);
        if (sc == 2) CHECK(D.s.tx_fc_wait == 3 && count_type(&D.b, 0x7E0, 2) == 2);
        if (sc == 3) CHECK(D.s.tx_last_at == D.b.log[1].end + D.s.cfg.n_bs && count_type(&D.b, 0x7E0, 2) == 0);
    }

    /* B13 CF 를 보내는 중에 온 엉뚱한 FC → 무시, 전송은 정상 */
    duo_init(100, 0, true, 0, 0x01, ISOTP_MAX);
    link_init(&raw, cfg_make(0x7E8, 0x7E0, 100, true, 0, 0), NULL, 0);
    raw.cfg.rx_id = 0x7FF;
    bus_add(&D.b, &raw);
    tx_start(&D.s, SRC, 100, 0);
    run_ticks(&D.b, 40); raw_send(&raw, "7E8#30000000");
    run_idle(&D.b, 100000);
    CHECK(D.s.tx_fc_ignored == 1 && D.s.tx_last == N_OK && D.r.rx_last == N_OK && memcmp(D.rbuf, SRC, 100) == 0);
}

/* ---------- Part C ---------- */
typedef struct { const uint8_t *msg[8]; uint16_t len[8]; int n, next, got, bad; isotp_link *tx; } stream;
static void stream_next(isotp_link *l, n_result r, uint32_t now, void *ctx)   /* 송신 완료 → 다음 메시지 */
{
    stream *s = ctx; (void)l; (void)r;
    if (s->next < s->n) { tx_start(s->tx, s->msg[s->next], s->len[s->next], now); s->next++; }
}
static void stream_check(isotp_link *l, n_result r, uint32_t now, void *ctx)  /* 수신 완료 → 순서대로 비교 */
{
    stream *s = ctx; (void)now;
    if (r != N_OK) { s->bad++; return; }
    if (s->got >= s->n || l->rx_last_len != s->len[s->got] || memcmp(l->rx_buf, s->msg[s->got], l->rx_last_len)) s->bad++;
    s->got++;
}

typedef struct { uint32_t prev, switches, n; uint32_t t_first[3], t_last[3]; } mix_t;
static int conn_of(uint32_t id) { return id >= 0x7E0 ? 0 : id >= 0x700 ? 2 : 1; }
static void mix_hook(void *ctx, const trace_rec *r)
{
    mix_t *x = ctx; int c = conn_of(r->m.id);
    if (x->n && (uint32_t)c != x->prev) x->switches++;
    if (!x->t_first[c]) x->t_first[c] = r->start + 1;
    x->t_last[c] = r->end; x->prev = (uint32_t)c; x->n++;
}

static void test_part_c(void)
{
    /* C1 프레임 비트 수 (COMM-M04 와 같은 수) */
    CHECK(can_frame_bits(8, false) == 111 && can_frame_bits(8, true) == 135);
    CHECK(can_frame_bits(3, false) == 71 && can_frame_bits(3, true) == 85);
    CHECK(can_frame_bits(0, false) == 47 && can_frame_bits(0, true) == 55 && can_frame_bits(15, false) == 111);

    /* C2 공식 == 시뮬레이션 (tick 1 µs, 실제 프레임 비트 시간) */
    static const struct { uint16_t len; uint8_t bs, st; bool pad, worst; uint32_t br; } TC[] = {
        { 4095, 0, 0x00, true, false, 500000 }, { 4095, 8, 0xF5, true, false, 500000 },
        { 4095, 16, 0x01, true, true, 500000 }, { 4095, 0, 0x00, false, false, 1000000 },
        { 100, 3, 0x02, false, true, 250000 }, { 7, 0, 0, false, false, 500000 }, { 8, 1, 0x05, true, false, 125000 },
    };
    for (size_t i = 0; i < sizeof TC / sizeof TC[0]; i++) {
        duo_init(1, TC[i].br, TC[i].pad, TC[i].bs, TC[i].st, ISOTP_MAX);
        D.b.worst_stuff = TC[i].worst;
        uint32_t t0 = D.b.now;
        tx_start(&D.s, SRC, TC[i].len, t0);
        run_idle(&D.b, 200000000u);
        double f = isotp_transfer_us(TC[i].len, TC[i].bs, TC[i].st, TC[i].br, TC[i].pad, TC[i].worst);
        uint32_t sim = D.r.rx_last_at - t0;
        printf("C2 %4u B BS=%-2u STmin=0x%02X %s%s @%4u k: formula %.0f us, sim %u us\n", TC[i].len, TC[i].bs, TC[i].st,
               TC[i].pad ? "pad" : "nopad", TC[i].worst ? " worst-stuff" : "", TC[i].br / 1000, f, sim);
        CHECK(D.r.rx_ind[N_OK] == 1 && f > 0 && (double)sim == f);
    }

    /* C3 게이트웨이: T(0x7E0) → G.in … G.out(0x6F0) → E, 동시에 X(0x700) → Y 가 같은 버스를 쓴다 */
    static isotp_link T, Gin, Gout, E, X, Y;
    static uint8_t gbuf[ISOTP_MAX], ebuf[ISOTP_MAX], ybuf[ISOTP_MAX], M[9][ISOTP_MAX];
    static can_bus B;
    bus_init(&B, 100, 500000);                       /* 500 kbit/s: DLC 8 프레임 222 µs → 3 tick */
    link_init(&T,    cfg_make(0x7E0, 0x7E8, 100, true, 0, 0), NULL, 0);
    link_init(&Gin,  cfg_make(0x7E8, 0x7E0, 100, true, 0, 0xF2), gbuf, ISOTP_MAX);
    link_init(&Gout, cfg_make(0x6F0, 0x6F8, 100, true, 0, 0), NULL, 0);
    link_init(&E,    cfg_make(0x6F8, 0x6F0, 100, true, 8, 0x01), ebuf, ISOTP_MAX);   /* STmin 0 이면 G.out 이 버스를 독차지 */
    link_init(&X,    cfg_make(0x700, 0x708, 100, true, 0, 0), NULL, 0);
    link_init(&Y,    cfg_make(0x708, 0x700, 100, true, 16, 0x01), ybuf, ISOTP_MAX);
    isotp_link *all[6] = { &T, &Gin, &Gout, &E, &X, &Y };
    for (int i = 0; i < 6; i++) bus_add(&B, all[i]);
    gateway gw = { .in = &Gin, .out = &Gout };
    Gin.on_rx = gw_on_rx; Gin.cb_ctx = &gw; Gout.on_tx = gw_on_tx; Gout.cb_ctx = &gw;
    static const uint16_t TL[3] = { 4095, 300, 1000 };
    stream st_t = { .n = 3, .tx = &T }, st_e = { .n = 3 }, st_x = { .n = 6, .tx = &X }, st_y = { .n = 6 };
    for (int k = 0; k < 9; k++) for (int i = 0; i < ISOTP_MAX; i++) M[k][i] = (uint8_t)xorshift32();
    for (int k = 0; k < 3; k++) { st_t.msg[k] = st_e.msg[k] = M[k]; st_t.len[k] = st_e.len[k] = TL[k]; }
    for (int k = 0; k < 6; k++) { st_x.msg[k] = st_y.msg[k] = M[3 + k]; st_x.len[k] = st_y.len[k] = 700; }
    T.on_tx = stream_next; T.cb_ctx = &st_t; E.on_rx = stream_check; E.cb_ctx = &st_e;
    X.on_tx = stream_next; X.cb_ctx = &st_x; Y.on_rx = stream_check; Y.cb_ctx = &st_y;
    mix_t mx = { 0 };
    B.on_trace = mix_hook; B.trace_ctx = &mx;
    stream_next(&T, N_OK, 0, &st_t); stream_next(&X, N_OK, 0, &st_x);
    run_idle(&B, 3000000);
    uint32_t errs = 0;
    for (int i = 0; i < 6; i++) for (int r = 1; r < N_NRES; r++) errs += all[i]->tx_ind[r] + all[i]->rx_ind[r];
    printf("C3 bring-up log (first 16 of %u frames, 500 kbit/s, 1 tick = 100 us):\n", B.nframes);
    for (int i = 0; i < 16 && i < B.nlog; i++) {
        char s[32]; fmt_frame(&B.log[i].m, s, sizeof s);
        printf("   t=%7.1f ms  %s\n", B.log[i].start / 10.0, s);
    }
    printf("C3 gateway: E got %d/3 (bad %d), Y got %d/6 (bad %d), fwd ok %u fail %u, G.in WAIT sent %u, "
           "errors %u, connection switches %u, done at %.1f ms (T->G %.1f ms, G->E %.1f ms, X->Y %.1f ms)\n",
           st_e.got, st_e.bad, st_y.got, st_y.bad, gw.fwd_ok, gw.fwd_fail, Gin.rx_wait_sent, errs, mx.switches,
           B.now / 10.0, mx.t_last[0] / 10.0, mx.t_last[1] / 10.0, mx.t_last[2] / 10.0);
    CHECK(st_e.got == 3 && st_e.bad == 0 && st_y.got == 6 && st_y.bad == 0);
    CHECK(gw.fwd_ok == 3 && gw.fwd_fail == 0 && errs == 0);
    CHECK(Gin.rx_wait_sent >= 1 && T.tx_fc_wait == Gin.rx_wait_sent && mx.switches > 50);

    /* C4 게이트웨이가 바쁠 때 온 SF 는 WAIT 할 방법이 없다 → 조용히 사라짐 (rx_ignored 로만 보임) */
    bus_init(&B, 100, 500000);
    link_init(&T,    cfg_make(0x7E0, 0x7E8, 100, true, 0, 0), NULL, 0);
    link_init(&Gin,  cfg_make(0x7E8, 0x7E0, 100, true, 0, 0), gbuf, ISOTP_MAX);
    link_init(&Gout, cfg_make(0x6F0, 0x6F8, 100, true, 0, 0), NULL, 0);
    link_init(&E,    cfg_make(0x6F8, 0x6F0, 100, true, 0, 0x01), ebuf, ISOTP_MAX);
    isotp_link *four[4] = { &T, &Gin, &Gout, &E };
    for (int i = 0; i < 4; i++) bus_add(&B, four[i]);
    gw = (gateway){ .in = &Gin, .out = &Gout };
    Gin.on_rx = gw_on_rx; Gin.cb_ctx = &gw; Gout.on_tx = gw_on_tx; Gout.cb_ctx = &gw;
    static const uint16_t TL2[2] = { 1000, 5 };
    stream s2t = { .n = 2, .tx = &T }, s2e = { .n = 2 };
    for (int k = 0; k < 2; k++) { s2t.msg[k] = s2e.msg[k] = M[k]; s2t.len[k] = s2e.len[k] = TL2[k]; }
    T.on_tx = stream_next; T.cb_ctx = &s2t; E.on_rx = stream_check; E.cb_ctx = &s2e;
    stream_next(&T, N_OK, 0, &s2t);
    run_idle(&B, 3000000);
    printf("C4 SF while gateway busy: T sent %d (tx ok %u), E got %d, G.in ignored %u\n", s2t.next, T.tx_ind[N_OK],
           s2e.got, Gin.rx_ignored);
    CHECK(T.tx_ind[N_OK] == 2 && s2e.got == 1 && Gin.rx_ignored == 1 && gw.fwd_ok == 1);
}

int main(void)
{
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
