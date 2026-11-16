/* PCIE-B09 추가 연습 — 한 번에 검증
 *
 * 본문 solution.c 의 모의 하드웨어 + 열거기 + 트리/검증기를 그대로 담고 (자기완결), 그 위에:
 *   연습 1  only_one_child: root port / DSP 아래 링크는 dev 0 만 훑기 → 같은 결과, config 읽기 수 감소
 *   연습 2  hotplug 슬롯(PCIe cap flags 의 Slot Implemented)마다 버스 N 개 예약
 *   연습 3  빈 슬롯에 스위치 hot-add → 그 브리지 아래만 rescan (예약 없음: 버스 고갈 / 예약 4: 성공)
 *   연습 4  재귀 없는 DFS (명시적 스택) — 재귀판과 결과·쓰기 값이 같음
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- pci_regs.h 와 같은 이름·값 ---- */
#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_STATUS              0x06
#define  PCI_STATUS_CAP_LIST    0x10
#define PCI_CLASS_REVISION      0x08    /* 상위 24비트 = class */
#define PCI_HEADER_TYPE         0x0e
#define  PCI_HEADER_TYPE_MASK   0x7f
#define  PCI_HEADER_TYPE_NORMAL 0
#define  PCI_HEADER_TYPE_BRIDGE 1
#define  PCI_HEADER_TYPE_MFD    0x80
#define PCI_PRIMARY_BUS         0x18
#define PCI_SECONDARY_BUS       0x19
#define PCI_SUBORDINATE_BUS     0x1a
#define PCI_CAPABILITY_LIST     0x34
#define PCI_CAP_ID_EXP          0x10
#define PCI_EXP_FLAGS           0x02
#define  PCI_EXP_FLAGS_TYPE     0x00f0
#define  PCI_EXP_FLAGS_SLOT     0x0100
#define  PCI_EXP_TYPE_ENDPOINT  0x0
#define  PCI_EXP_TYPE_ROOT_PORT 0x4
#define  PCI_EXP_TYPE_UPSTREAM  0x5
#define  PCI_EXP_TYPE_DOWNSTREAM 0x6

#define ECAM_OFF(b, d, f, r) (((uint32_t)(b) << 20) | ((uint32_t)(d) << 15) | ((uint32_t)(f) << 12) | (uint32_t)(r))

/* ======================= 모의 하드웨어 (PCIE-B08 모델 축약판) ======================= */

#define CFG_SIZE   256
#define MAX_NODES  48
#define MAX_LINKS  24
#define NO_EXP     (-1)          /* PCIe capability 없음 (conventional PCI 함수, 예: host bridge) */

typedef struct {
    uint8_t     cfg[CFG_SIZE];
    int8_t      sec_link;        /* 브리지: 아래쪽 링크 번호, 없으면 -1 */
    bool        fn_alias;        /* 고장난 장치: 어떤 fn 번호로 물어도 fn0 config 로 대답 */
} mnode;

typedef struct {
    int8_t at[32][8];            /* [dev][fn] → 노드 번호, -1 = 빈 자리 */
    bool   p2p;                  /* root port / DSP 아래 점대점 링크: device 0 만 */
} mlink;

typedef struct {
    mnode    node[MAX_NODES];
    int      n_nodes;
    mlink    link[MAX_LINKS];
    int      n_links;
    uint32_t cfg_reads, cfg_writes, ur_reads;
} mock;

static void mock_init(mock *m) {
    memset(m, 0, sizeof *m);
    memset(m->link[0].at, -1, sizeof m->link[0].at);
    m->n_links = 1;              /* link 0 = 루트 버스 */
}

static int mk_fn(mock *m, int link, uint8_t dev, uint8_t fn, uint16_t vid, uint16_t did,
                 uint32_t class24, uint8_t hdr, int exp_type, bool slot) {
    if (m->n_nodes >= MAX_NODES || link < 0 || link >= m->n_links || dev > 31 || fn > 7) return -1;
    int i = m->n_nodes++;
    mnode *n = &m->node[i];
    memset(n, 0, sizeof *n);
    n->sec_link = -1;
    n->cfg[0] = (uint8_t)vid; n->cfg[1] = (uint8_t)(vid >> 8);
    n->cfg[2] = (uint8_t)did; n->cfg[3] = (uint8_t)(did >> 8);
    n->cfg[0x09] = (uint8_t)class24; n->cfg[0x0a] = (uint8_t)(class24 >> 8); n->cfg[0x0b] = (uint8_t)(class24 >> 16);
    n->cfg[PCI_HEADER_TYPE] = hdr;
    if (exp_type != NO_EXP) {   /* PCIe capability 하나를 0x40 에: ID 0x10, next 0, flags = 버전 2 | 포트 타입 | slot */
        n->cfg[PCI_STATUS] |= PCI_STATUS_CAP_LIST;
        n->cfg[PCI_CAPABILITY_LIST] = 0x40;
        uint16_t flags = (uint16_t)(0x2 | ((unsigned)exp_type << 4) | (slot ? PCI_EXP_FLAGS_SLOT : 0));
        n->cfg[0x40] = PCI_CAP_ID_EXP; n->cfg[0x41] = 0;
        n->cfg[0x40 + PCI_EXP_FLAGS] = (uint8_t)flags; n->cfg[0x41 + PCI_EXP_FLAGS] = (uint8_t)(flags >> 8);
    }
    m->link[link].at[dev][fn] = (int8_t)i;
    return i;
}

/* Type 1 브리지 + 그 아래 새 링크. 반환: 노드 번호 (아래 링크 = m->node[i].sec_link). bus 레지스터는 리셋값 0. */
static int mk_bridge(mock *m, int link, uint8_t dev, uint8_t fn, uint16_t vid, uint16_t did, int exp_type, bool slot) {
    if (m->n_links >= MAX_LINKS) return -1;
    int i = mk_fn(m, link, dev, fn, vid, did, 0x060400, PCI_HEADER_TYPE_BRIDGE, exp_type, slot);
    if (i < 0) return -1;
    int l = m->n_links++;
    memset(m->link[l].at, -1, sizeof m->link[l].at);
    m->link[l].p2p = (exp_type == PCI_EXP_TYPE_ROOT_PORT || exp_type == PCI_EXP_TYPE_DOWNSTREAM);
    m->node[i].sec_link = (int8_t)l;
    return i;
}
static int below(const mock *m, int bridge) { return bridge < 0 ? -1 : m->node[bridge].sec_link; }

/* (bus, dev, fn) 의 config 요청이 도달하는 노드. 루트 버스(0)에서 출발해 브리지 창을 따라 내려간다. */
static int mock_route(const mock *m, uint8_t bus, uint8_t dev, uint8_t fn) {
    int lk = 0;
    uint8_t lk_bus = 0;
    for (int hops = 0; bus != lk_bus; hops++) {
        if (hops >= MAX_LINKS) return -1;
        int next = -1;
        uint8_t next_bus = 0;
        for (int d = 0; d < 32 && next < 0; d++)
            for (int f = 0; f < 8 && next < 0; f++) {
                int ni = m->link[lk].at[d][f];
                if (ni < 0) continue;
                const uint8_t *c = m->node[ni].cfg;
                if ((c[PCI_HEADER_TYPE] & PCI_HEADER_TYPE_MASK) != PCI_HEADER_TYPE_BRIDGE) continue;
                uint8_t sec = c[PCI_SECONDARY_BUS], sub = c[PCI_SUBORDINATE_BUS];
                if (sec > lk_bus && sec <= bus && bus <= sub) {   /* 이 브리지가 받는다 (먼저 받은 쪽이 이김) */
                    next = m->node[ni].sec_link;
                    next_bus = sec;
                    if (next < 0) return -1;                      /* 창은 있는데 아래가 빈 슬롯 → UR */
                }
            }
        if (next < 0) return -1;                                  /* 아무도 안 받음 → UR → all-1s */
        lk = next; lk_bus = next_bus;
    }
    if (m->link[lk].p2p && dev != 0) return -1;                   /* 점대점 링크엔 device 0 만 */
    int ni = m->link[lk].at[dev][fn];
    if (ni < 0 && fn != 0) {
        int n0 = m->link[lk].at[dev][0];
        if (n0 >= 0 && m->node[n0].fn_alias) ni = n0;             /* fn 번호를 무시하는 고장 장치 */
    }
    return ni;
}

/* ECAM 32비트 읽기 / 8비트 쓰기 (reg 는 256B 안, 32비트 읽기는 4바이트 정렬만 — PCIE-B08 참고) */
static uint32_t ecam_rd32(mock *m, uint32_t off) {
    m->cfg_reads++;
    uint16_t reg = (uint16_t)(off & 0xFFF);
    int ni = ((reg & 3) == 0 && reg < CFG_SIZE)
           ? mock_route(m, (uint8_t)(off >> 20), (uint8_t)((off >> 15) & 31), (uint8_t)((off >> 12) & 7)) : -1;
    if (ni < 0) { m->ur_reads++; return 0xFFFFFFFFu; }
    const uint8_t *c = m->node[ni].cfg;
    return (uint32_t)c[reg] | (uint32_t)c[reg + 1] << 8 | (uint32_t)c[reg + 2] << 16 | (uint32_t)c[reg + 3] << 24;
}
static void ecam_wr8(mock *m, uint32_t off, uint8_t v) {
    m->cfg_writes++;
    uint16_t reg = (uint16_t)(off & 0xFFF);
    int ni = mock_route(m, (uint8_t)(off >> 20), (uint8_t)((off >> 15) & 31), (uint8_t)((off >> 12) & 7));
    if (ni < 0 || reg >= CFG_SIZE) return;                       /* 없는 장치로 쓰기 → UR, 아무 일 없음 */
    uint8_t *c = m->node[ni].cfg;
    bool bridge = (c[PCI_HEADER_TYPE] & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE;
    if (bridge && reg >= PCI_PRIMARY_BUS && reg <= PCI_SUBORDINATE_BUS) c[reg] = v;   /* 모의 장치에서 쓰기 가능한 곳은 여기뿐 */
}

/* 드라이버 쪽 helper — config 는 little-endian, 바이트로 조립 (정렬 안 된 포인터 캐스트 금지) */
static uint8_t  cfg_rd8 (mock *m, uint8_t b, uint8_t d, uint8_t f, uint16_t r) { return (uint8_t)(ecam_rd32(m, ECAM_OFF(b, d, f, r & ~3u)) >> (8 * (r & 3))); }
static uint16_t cfg_rd16(mock *m, uint8_t b, uint8_t d, uint8_t f, uint16_t r) { return (uint16_t)(ecam_rd32(m, ECAM_OFF(b, d, f, r & ~3u)) >> (8 * (r & 2))); }
static uint32_t cfg_rd32(mock *m, uint8_t b, uint8_t d, uint8_t f, uint16_t r) { return ecam_rd32(m, ECAM_OFF(b, d, f, r)); }
static void     cfg_wr8 (mock *m, uint8_t b, uint8_t d, uint8_t f, uint16_t r, uint8_t v) { ecam_wr8(m, ECAM_OFF(b, d, f, r), v); }

/* ======================= 열거기 ======================= */

#define MAX_FOUND 64
enum { ENUM_OK = 0, ENUM_ERR_FULL = -1, ENUM_ERR_BUSES = -2 };

typedef struct {
    uint8_t  bus, dev, fn;
    uint8_t  hdr;                /* Header Type 바이트 그대로 (bit7 = MFD) */
    uint16_t vid, did;
    uint32_t class24;
    uint8_t  pri, sec, sub;      /* 브리지만: 열거기가 쓴 값 (실패해 못 쓴 브리지는 0/0/0) */
    int16_t  parent;             /* 위쪽 브리지의 결과 인덱스, 루트 버스면 -1 */
} pci_fn;

typedef struct {
    pci_fn f[MAX_FOUND];
    int    n;
    int    err;                  /* 처음 난 오류 (ENUM_OK 면 없음) */
} enum_result;

/* 재귀 전에 subordinate 에 무엇을 써 두나 — 정답은 SUB_TMP_LAST. 나머지 둘은 Part B 버그 재현용. */
typedef enum { SUB_TMP_LAST, SUB_TMP_NONE, SUB_TMP_SEC } sub_policy;
typedef struct {
    sub_policy tmp_sub;
    bool       probe_all_fn;     /* 버그 재현: MFD 비트를 무시하고 fn 0..7 을 전부 물어봄 */
    uint8_t    last_bus;         /* 이 루트가 쓸 수 있는 마지막 버스 (DT bus-range / ECAM 크기). 보통 0xFF */
} enum_opts;
static const enum_opts ENUM_DEFAULT = { SUB_TMP_LAST, false, 0xFF };

/* 재귀 전에 쓸 임시 subordinate 값. -1 = 쓰지 않음 (리셋값 0 그대로). */
static int tmp_sub_value(const enum_opts *o, uint8_t sec) {
    switch (o->tmp_sub) {
    case SUB_TMP_LAST: return o->last_bus;
    case SUB_TMP_SEC:  return sec;
    default:           return -1;
    }
}

/* ---------------- Part A ---------------- */

/* A1: (bus,dev,fn) 에 함수가 있나? 있으면 *vid_did = (DID<<16)|VID.
 * 없는 함수는 all-1s (VID 0xFFFF). Linux 처럼 VID 0x0000 도 "없음" 으로 본다. */
static bool fn_present(mock *m, uint8_t bus, uint8_t dev, uint8_t fn, uint32_t *vid_did) {
    uint32_t v = cfg_rd32(m, bus, dev, fn, PCI_VENDOR_ID);
    uint16_t vid = (uint16_t)v;
    if (vid == 0xFFFF || vid == 0x0000) return false;
    *vid_did = v;
    return true;
}

/* 결과 표에 하나 추가. 꽉 차면 -1 */
static int record(mock *m, enum_result *r, uint8_t bus, uint8_t dev, uint8_t fn, uint32_t vid_did, int parent) {
    if (r->n >= MAX_FOUND) { if (r->err == ENUM_OK) r->err = ENUM_ERR_FULL; return -1; }
    pci_fn *p = &r->f[r->n];
    memset(p, 0, sizeof *p);
    p->bus = bus; p->dev = dev; p->fn = fn;
    p->vid = (uint16_t)vid_did; p->did = (uint16_t)(vid_did >> 16);
    p->hdr = cfg_rd8(m, bus, dev, fn, PCI_HEADER_TYPE);
    p->class24 = cfg_rd32(m, bus, dev, fn, PCI_CLASS_REVISION) >> 8;
    p->parent = (int16_t)parent;
    return r->n++;
}

/* A2 + B1 + B2: 버스 하나를 훑는다 (DFS). 반환 = 이 버스 아래에서 쓴 가장 큰 버스 번호 (= 위 브리지의 subordinate).
 *  - dev 0..31: fn0 이 없으면 그 dev 는 통째로 없음.
 *  - fn0 Header Type bit7(MFD) 이면 fn 1..7 도 (빈칸이 있어도 끝까지), 아니면 fn0 만. (opts->probe_all_fn 이면 무조건 8개)
 *  - Type 1 이면: sec = max+1 (> last_bus 면 ENUM_ERR_BUSES, 그 브리지는 건드리지 않고 넘어감)
 *                 pri = bus, sec = sec, sub = tmp_sub_value() → 재귀 → sub = 재귀가 돌려준 최대, max 갱신. */
static int scan_bus(mock *m, enum_result *r, const enum_opts *o, uint8_t bus, int parent) {
    int max = bus;
    for (uint8_t dev = 0; dev < 32; dev++) {
        uint32_t id;
        if (!fn_present(m, bus, dev, 0, &id)) continue;
        uint8_t hdr0 = cfg_rd8(m, bus, dev, 0, PCI_HEADER_TYPE);
        uint8_t nfn = ((hdr0 & PCI_HEADER_TYPE_MFD) || o->probe_all_fn) ? 8 : 1;
        for (uint8_t fn = 0; fn < nfn; fn++) {
            if (fn > 0 && !fn_present(m, bus, dev, fn, &id)) continue;   /* 빈 fn 은 건너뛰고 계속 (break 아님) */
            int idx = record(m, r, bus, dev, fn, id, parent);
            if (idx < 0) return max;
            if ((r->f[idx].hdr & PCI_HEADER_TYPE_MASK) != PCI_HEADER_TYPE_BRIDGE) continue;
            if (max + 1 > o->last_bus) {                                 /* 버스 번호 고갈 */
                if (r->err == ENUM_OK) r->err = ENUM_ERR_BUSES;
                continue;
            }
            uint8_t sec = (uint8_t)(max + 1);
            cfg_wr8(m, bus, dev, fn, PCI_PRIMARY_BUS, bus);
            cfg_wr8(m, bus, dev, fn, PCI_SECONDARY_BUS, sec);
            int t = tmp_sub_value(o, sec);
            if (t >= 0) cfg_wr8(m, bus, dev, fn, PCI_SUBORDINATE_BUS, (uint8_t)t);
            int child_max = scan_bus(m, r, o, sec, idx);
            cfg_wr8(m, bus, dev, fn, PCI_SUBORDINATE_BUS, (uint8_t)child_max);
            r->f[idx].pri = bus; r->f[idx].sec = sec; r->f[idx].sub = (uint8_t)child_max;
            max = child_max;
        }
    }
    return max;
}

/* A3: 루트 버스 0 부터 전체 열거. 반환 = 마지막으로 쓴 버스 번호 */
static int enum_all(mock *m, enum_result *r, const enum_opts *o) {
    memset(r, 0, sizeof *r);
    r->err = ENUM_OK;
    return scan_bus(m, r, o, 0, -1);
}

/* ---------------- Part C: lspci -tv 형식 트리 ----------------
 * pciutils ls-tree.c 의 모양을 따른다: 버스에 자식 1개면 "--", 여럿이면 "+-" … 마지막 "\-",
 * 브리지는 "dd.f-[ss-uu]-" (sec == sub 면 "[ss]"), 끝단 장치는 "dd.f  이름".
 * 한 줄을 찍고 나면 그 줄의 '+','|' 는 '|' 로, 나머지는 ' ' 로 바꿔 다음 줄의 들여쓰기로 쓴다 (print_it).
 * 트리는 lspci 처럼 "버스 번호" 로 만든다: 브리지 아래 = bus == 그 브리지의 secondary 인 함수들. */

#define TREE_LINE_MAX 256
#define TREE_DEPTH_MAX 32
typedef const char *(*name_fn)(uint16_t vid, uint16_t did);
typedef struct {
    const enum_result *r;
    name_fn  name;
    char    *out;
    size_t   cap, len;
    bool     overflow;
    char     line[TREE_LINE_MAX];
} tree_ctx;

static size_t tput(tree_ctx *t, size_t p, const char *s) {
    size_t n = strlen(s);
    if (p + n >= TREE_LINE_MAX) { t->overflow = true; return p; }
    memcpy(t->line + p, s, n);
    return p + n;
}
static void tree_emit(tree_ctx *t, size_t p) {
    if (t->len + p + 2 > t->cap) t->overflow = true;
    else { memcpy(t->out + t->len, t->line, p); t->len += p; t->out[t->len++] = '\n'; t->out[t->len] = 0; }
    for (size_t i = 0; i < p; i++) t->line[i] = (t->line[i] == '+' || t->line[i] == '|') ? '|' : ' ';
}

static void tree_bus(tree_ctx *t, uint8_t bus, size_t p, int depth);

/* C1a: 함수 하나. 브리지면 버스 범위를 붙이고 secondary 버스로 내려간다 (sec 가 0 이거나 자기 버스 이하면
 *      내려가지 않고 "-[--]-" 로 끝 — 무한 재귀 방지). 끝단이면 "  이름" (이름 모르면 "Device vvvv:dddd"). */
static void tree_dev(tree_ctx *t, int i, size_t p, int depth) {
    const pci_fn *f = &t->r->f[i];
    char s[64];
    snprintf(s, sizeof s, "%02x.%x", f->dev, f->fn);
    p = tput(t, p, s);
    if ((f->hdr & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE) {
        if (f->sec == 0 || f->sec <= f->bus || depth >= TREE_DEPTH_MAX) { p = tput(t, p, "-[--]-"); tree_emit(t, p); return; }
        if (f->sec == f->sub) snprintf(s, sizeof s, "-[%02x]-", f->sec);
        else                  snprintf(s, sizeof s, "-[%02x-%02x]-", f->sec, f->sub);
        p = tput(t, p, s);
        p = tput(t, p, "-");
        tree_bus(t, f->sec, p, depth + 1);
        return;
    }
    const char *nm = t->name ? t->name(f->vid, f->did) : NULL;
    if (nm) snprintf(s, sizeof s, "  %s", nm);
    else    snprintf(s, sizeof s, "  Device %04x:%04x", f->vid, f->did);
    p = tput(t, p, s);
    tree_emit(t, p);
}

/* C1b: 버스 하나의 자식들 (결과 표에서 bus 가 같은 함수, 표 순서 그대로). 0개면 줄만 찍는다. */
static void tree_bus(tree_ctx *t, uint8_t bus, size_t p, int depth) {
    int k = 0, last = -1;
    for (int i = 0; i < t->r->n; i++) if (t->r->f[i].bus == bus) { k++; last = i; }
    if (k == 0) { tree_emit(t, p); return; }
    if (k == 1) { tree_dev(t, last, tput(t, p, "--"), depth); return; }
    for (int i = 0; i < t->r->n; i++) {
        if (t->r->f[i].bus != bus) continue;
        tree_dev(t, i, tput(t, p, i == last ? "\\-" : "+-"), depth);
    }
}

/* 트리 전체를 out 에. 반환 = 글자 수, 넘치면 -1 */
static int tree_print(const enum_result *r, name_fn nm, char *out, size_t cap) {
    tree_ctx t;
    memset(&t, 0, sizeof t);
    t.r = r; t.name = nm; t.out = out; t.cap = cap;
    if (cap) out[0] = 0;
    size_t p = tput(&t, 0, "-[0000:00]-");
    tree_bus(&t, 0, p, 0);
    return t.overflow ? -1 : (int)t.len;
}

/* ---------------- Part C: 검증기 ----------------
 * 열거가 끝난 "하드웨어" 를 다시 읽어 버스 번호 불변식을 검사한다 (bring-up 체크리스트 / 펌웨어가 넘겨준 상태 점검).
 * 브리지마다 (결과 표 순서, 그 BDF 에서 실제 config 를 읽어서; sec==0 인 미설정 브리지는 건너뜀):
 *   V_PRI     primary != 자기가 앉은 버스
 *   V_SEC     secondary <= 자기 버스
 *   V_SUB     subordinate < secondary
 *   V_NEST    부모 브리지 범위 [sec..sub] 밖으로 나감  (자기 버스 < 내 sec, 내 sub <= 부모 sub)
 *   V_OVERLAP 같은 버스의 형제 브리지와 [sec..sub] 가 겹침
 * 그 다음 모든 함수: 기록된 BDF 에서 VID/DID 가 그대로 읽혀야 함, 아니면 V_UNREACH.
 * 반환 = 처음 걸린 코드, *bad = 그 결과 인덱스. */
typedef enum { V_OK = 0, V_PRI, V_SEC, V_SUB, V_NEST, V_OVERLAP, V_UNREACH } vcode;

typedef struct { uint8_t pri, sec, sub; } busregs;
static busregs read_busregs(mock *m, const pci_fn *f) {
    busregs b;
    b.pri = cfg_rd8(m, f->bus, f->dev, f->fn, PCI_PRIMARY_BUS);
    b.sec = cfg_rd8(m, f->bus, f->dev, f->fn, PCI_SECONDARY_BUS);
    b.sub = cfg_rd8(m, f->bus, f->dev, f->fn, PCI_SUBORDINATE_BUS);
    return b;
}
static bool is_bridge(const pci_fn *f) { return (f->hdr & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE; }

static vcode enum_verify(mock *m, const enum_result *r, int *bad) {
    *bad = -1;
    for (int i = 0; i < r->n; i++) {
        const pci_fn *f = &r->f[i];
        if (!is_bridge(f)) continue;
        busregs b = read_busregs(m, f);
        if (b.sec == 0) continue;                                  /* 미설정(고갈 등) — 별도 r->err 로 보고됨 */
        *bad = i;
        if (b.pri != f->bus) return V_PRI;
        if (b.sec <= f->bus) return V_SEC;
        if (b.sub < b.sec)   return V_SUB;
        if (f->parent >= 0) {
            busregs pb = read_busregs(m, &r->f[f->parent]);
            if (b.sub > pb.sub) return V_NEST;
        }
        for (int j = i + 1; j < r->n; j++) {
            const pci_fn *g = &r->f[j];
            if (!is_bridge(g) || g->bus != f->bus) continue;
            busregs c = read_busregs(m, g);
            if (c.sec == 0) continue;
            if (!(c.sub < b.sec || c.sec > b.sub)) return V_OVERLAP;
        }
    }
    for (int i = 0; i < r->n; i++) {
        const pci_fn *f = &r->f[i];
        uint32_t v = cfg_rd32(m, f->bus, f->dev, f->fn, PCI_VENDOR_ID);
        if (v != ((uint32_t)f->did << 16 | f->vid)) { *bad = i; return V_UNREACH; }
    }
    *bad = -1;
    return V_OK;
}

/* C2: 루트 버스에서 결과 idx 까지 거치는 브리지들 (루트 쪽부터). 반환 = 개수, cap 부족이면 -1 */
static int enum_path(const enum_result *r, int idx, int *path, int cap) {
    int n = 0;
    for (int p = (idx >= 0 && idx < r->n) ? r->f[idx].parent : -1, hops = 0; p >= 0; p = r->f[p].parent, hops++) {
        if (n >= cap || hops > MAX_FOUND) return -1;
        path[n++] = p;
    }
    for (int i = 0; i < n / 2; i++) { int t = path[i]; path[i] = path[n - 1 - i]; path[n - 1 - i] = t; }
    return n;
}

/* ======================= 테스트 하네스 ======================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---- 토폴로지 A: QEMU 실측과 같은 구성 (capture_qemu_enum.sh) ----
 *  00:00.0 host bridge [1b36:0008] (PCIe cap 없음)
 *  00:01.0 root port [1b36:000c] ── USP [104c:8232] ── DSP 0 ── NVMe [1b36:0010]
 *                                                    ├─ DSP 1 ── NIC [8086:10d3] fn0(MFD) fn1 fn3 (fn2 없음)
 *                                                    └─ DSP 2 ── (빈 슬롯)
 *  00:02.0 root port [1b36:000c] ── edu [1234:11e8] */
typedef struct { int rp1, usp, dsp0, dsp1, dsp2, rp2; } topo_a_ids;
static topo_a_ids build_topo_a(mock *m) {
    topo_a_ids t;
    mock_init(m);
    mk_fn(m, 0, 0, 0, 0x1b36, 0x0008, 0x060000, 0, NO_EXP, false);
    t.rp1  = mk_bridge(m, 0, 1, 0, 0x1b36, 0x000c, PCI_EXP_TYPE_ROOT_PORT, true);
    t.usp  = mk_bridge(m, below(m, t.rp1), 0, 0, 0x104c, 0x8232, PCI_EXP_TYPE_UPSTREAM, false);
    t.dsp0 = mk_bridge(m, below(m, t.usp), 0, 0, 0x104c, 0x8233, PCI_EXP_TYPE_DOWNSTREAM, true);
    t.dsp1 = mk_bridge(m, below(m, t.usp), 1, 0, 0x104c, 0x8233, PCI_EXP_TYPE_DOWNSTREAM, true);
    t.dsp2 = mk_bridge(m, below(m, t.usp), 2, 0, 0x104c, 0x8233, PCI_EXP_TYPE_DOWNSTREAM, true);
    mk_fn(m, below(m, t.dsp0), 0, 0, 0x1b36, 0x0010, 0x010802, 0, PCI_EXP_TYPE_ENDPOINT, false);
    mk_fn(m, below(m, t.dsp1), 0, 0, 0x8086, 0x10d3, 0x020000, PCI_HEADER_TYPE_MFD, PCI_EXP_TYPE_ENDPOINT, false);
    mk_fn(m, below(m, t.dsp1), 0, 1, 0x8086, 0x10d3, 0x020000, 0, PCI_EXP_TYPE_ENDPOINT, false);
    mk_fn(m, below(m, t.dsp1), 0, 3, 0x8086, 0x10d3, 0x020000, 0, PCI_EXP_TYPE_ENDPOINT, false);
    t.rp2  = mk_bridge(m, 0, 2, 0, 0x1b36, 0x000c, PCI_EXP_TYPE_ROOT_PORT, true);
    mk_fn(m, below(m, t.rp2), 0, 0, 0x1234, 0x11e8, 0x00ff00, 0, NO_EXP, false);
    return t;
}
static const char *names_a(uint16_t vid, uint16_t did) {   /* lspci(pci.ids) 가 찍은 이름 그대로 */
    if (vid == 0x1b36 && did == 0x0008) return "Red Hat, Inc. QEMU PCIe Host bridge";
    if (vid == 0x1b36 && did == 0x0010) return "Red Hat, Inc. QEMU NVM Express Controller";
    if (vid == 0x8086 && did == 0x10d3) return "Intel Corporation 82574L Gigabit Network Connection";
    return NULL;
}

/* ---- 토폴로지 B (가상, 손으로 만든 보드): 스위치 2단 + 별칭 장치 + 루트 버스 MFD + fn0 없는 장치 ----
 *  00:00.0 host bridge
 *  00:01.0 RP ── USP1 ── 00.0 DSP ── USP2 ── 00.0 DSP ── SSD-a
 *                     │                   └─ 01.0 DSP ── SSD-b
 *                     └─ 01.0 DSP ── NIC
 *  00:02.0 RP ── FPGA (Header Type 0x00, fn 번호를 무시하고 fn0 로 대답하는 별칭 장치)
 *  00:03.0 RP ── (빈 슬롯)
 *  00:05.x fn0 없음, fn2 만 있음 (스펙상 무효 — 열거되면 안 됨)
 *  00:1f.0 / .3 / .4 루트 버스 위 multi-function (PCH 스타일: ISA bridge, SMBus, …) */
typedef struct { int rpa, usp1, dspa, usp2, dsp_a0, dsp_a1, dspb, rpb, rpc; } topo_b_ids;
static topo_b_ids build_topo_b(mock *m) {
    topo_b_ids t;
    mock_init(m);
    mk_fn(m, 0, 0, 0, 0xAB00, 0x0001, 0x060000, 0, NO_EXP, false);
    t.rpa    = mk_bridge(m, 0, 1, 0, 0xAB00, 0x0010, PCI_EXP_TYPE_ROOT_PORT, true);
    t.usp1   = mk_bridge(m, below(m, t.rpa), 0, 0, 0xAB10, 0x0100, PCI_EXP_TYPE_UPSTREAM, false);
    t.dspa   = mk_bridge(m, below(m, t.usp1), 0, 0, 0xAB10, 0x0101, PCI_EXP_TYPE_DOWNSTREAM, true);
    t.usp2   = mk_bridge(m, below(m, t.dspa), 0, 0, 0xAB20, 0x0200, PCI_EXP_TYPE_UPSTREAM, false);
    t.dsp_a0 = mk_bridge(m, below(m, t.usp2), 0, 0, 0xAB20, 0x0201, PCI_EXP_TYPE_DOWNSTREAM, true);
    t.dsp_a1 = mk_bridge(m, below(m, t.usp2), 1, 0, 0xAB20, 0x0201, PCI_EXP_TYPE_DOWNSTREAM, true);
    mk_fn(m, below(m, t.dsp_a0), 0, 0, 0xAB30, 0x5001, 0x010802, 0, PCI_EXP_TYPE_ENDPOINT, false);
    mk_fn(m, below(m, t.dsp_a1), 0, 0, 0xAB30, 0x5002, 0x010802, 0, PCI_EXP_TYPE_ENDPOINT, false);
    t.dspb   = mk_bridge(m, below(m, t.usp1), 1, 0, 0xAB10, 0x0101, PCI_EXP_TYPE_DOWNSTREAM, true);
    mk_fn(m, below(m, t.dspb), 0, 0, 0xAB40, 0x0e00, 0x020000, 0, PCI_EXP_TYPE_ENDPOINT, false);
    t.rpb    = mk_bridge(m, 0, 2, 0, 0xAB00, 0x0010, PCI_EXP_TYPE_ROOT_PORT, true);
    int fpga = mk_fn(m, below(m, t.rpb), 0, 0, 0xAB50, 0xF0A0, 0x120000, 0, PCI_EXP_TYPE_ENDPOINT, false);
    m->node[fpga].fn_alias = true;
    t.rpc    = mk_bridge(m, 0, 3, 0, 0xAB00, 0x0010, PCI_EXP_TYPE_ROOT_PORT, true);
    mk_fn(m, 0, 5, 2, 0xAB60, 0x0002, 0x088000, 0, NO_EXP, false);
    mk_fn(m, 0, 0x1f, 0, 0xAB00, 0x1f00, 0x060100, PCI_HEADER_TYPE_MFD, NO_EXP, false);
    mk_fn(m, 0, 0x1f, 3, 0xAB00, 0x1f03, 0x0c0500, 0, NO_EXP, false);
    mk_fn(m, 0, 0x1f, 4, 0xAB00, 0x1f04, 0x040300, 0, NO_EXP, false);
    return t;
}

/* 도우미 */
static int find_bdf(const enum_result *r, uint8_t b, uint8_t d, uint8_t f) {
    for (int i = 0; i < r->n; i++) if (r->f[i].bus == b && r->f[i].dev == d && r->f[i].fn == f) return i;
    return -1;
}
static bool bridge_is(mock *m, const enum_result *r, uint8_t b, uint8_t d, uint8_t f, uint8_t pri, uint8_t sec, uint8_t sub) {
    int i = find_bdf(r, b, d, f);
    if (i < 0) return false;
    busregs x = read_busregs(m, &r->f[i]);   /* 기록뿐 아니라 "하드웨어" 에 실제로 써졌는지 */
    return x.pri == pri && x.sec == sec && x.sub == sub &&
           r->f[i].pri == pri && r->f[i].sec == sec && r->f[i].sub == sub;
}
static int count_vid(const enum_result *r, uint16_t vid) { int k = 0; for (int i = 0; i < r->n; i++) k += r->f[i].vid == vid; return k; }
static char g_tree[4096];

/* ======================= 추가 연습 ======================= */

/* 연습 1·2 공용: 브리지의 PCIe capability 에서 포트 타입 / slot 비트 읽기 (legacy 리스트, 홉 제한 48 — PCIE-B04) */
static int exp_flags(mock *m, uint8_t b, uint8_t d, uint8_t f) {
    if (!(cfg_rd8(m, b, d, f, PCI_STATUS) & PCI_STATUS_CAP_LIST)) return -1;
    uint8_t p = cfg_rd8(m, b, d, f, PCI_CAPABILITY_LIST) & 0xFC;
    for (int hops = 0; p >= 0x40 && hops < 48; hops++) {
        if (cfg_rd8(m, b, d, f, p) == PCI_CAP_ID_EXP) return cfg_rd16(m, b, d, f, (uint16_t)(p + PCI_EXP_FLAGS));
        p = cfg_rd8(m, b, d, f, (uint16_t)(p + 1)) & 0xFC;
    }
    return -1;
}

/* 확장 열거기: one_child = 아래가 root port / DSP 링크면 dev 0 만 (Linux only_one_child),
 *              hp_reserve = slot 이 있는(hotplug 가능) 브리지는 최소 hp_reserve 개 버스를 차지 (Linux pci=hpbussize= 비슷) */
typedef struct { bool one_child; uint8_t hp_reserve; } enum_x;

static int scan_bus_x(mock *m, enum_result *r, const enum_opts *o, const enum_x *x, uint8_t bus, int parent, bool only_dev0) {
    int max = bus;
    uint8_t ndev = only_dev0 ? 1 : 32;
    for (uint8_t dev = 0; dev < ndev; dev++) {
        uint32_t id;
        if (!fn_present(m, bus, dev, 0, &id)) continue;
        uint8_t hdr0 = cfg_rd8(m, bus, dev, 0, PCI_HEADER_TYPE);
        uint8_t nfn = ((hdr0 & PCI_HEADER_TYPE_MFD) || o->probe_all_fn) ? 8 : 1;
        for (uint8_t fn = 0; fn < nfn; fn++) {
            if (fn > 0 && !fn_present(m, bus, dev, fn, &id)) continue;
            int idx = record(m, r, bus, dev, fn, id, parent);
            if (idx < 0) return max;
            if ((r->f[idx].hdr & PCI_HEADER_TYPE_MASK) != PCI_HEADER_TYPE_BRIDGE) continue;
            if (max + 1 > o->last_bus) { if (r->err == ENUM_OK) r->err = ENUM_ERR_BUSES; continue; }
            uint8_t sec = (uint8_t)(max + 1);
            int fl = exp_flags(m, bus, dev, fn);
            int type = fl < 0 ? -1 : (fl & PCI_EXP_FLAGS_TYPE) >> 4;
            bool child_p2p = x->one_child && (type == PCI_EXP_TYPE_ROOT_PORT || type == PCI_EXP_TYPE_DOWNSTREAM);
            bool hp = fl >= 0 && (fl & PCI_EXP_FLAGS_SLOT);
            cfg_wr8(m, bus, dev, fn, PCI_PRIMARY_BUS, bus);
            cfg_wr8(m, bus, dev, fn, PCI_SECONDARY_BUS, sec);
            cfg_wr8(m, bus, dev, fn, PCI_SUBORDINATE_BUS, o->last_bus);
            int child_max = scan_bus_x(m, r, o, x, sec, idx, child_p2p);
            if (hp && x->hp_reserve > 0 && child_max < sec + x->hp_reserve - 1)
                child_max = (sec + x->hp_reserve - 1 > o->last_bus) ? o->last_bus : sec + x->hp_reserve - 1;
            cfg_wr8(m, bus, dev, fn, PCI_SUBORDINATE_BUS, (uint8_t)child_max);
            r->f[idx].pri = bus; r->f[idx].sec = sec; r->f[idx].sub = (uint8_t)child_max;
            max = child_max;
        }
    }
    return max;
}
static int enum_all_x(mock *m, enum_result *r, const enum_opts *o, const enum_x *x) {
    memset(r, 0, sizeof *r);
    return scan_bus_x(m, r, o, x, 0, -1, false);
}

/* 연습 3: 빈 슬롯(브리지 idx 아래)에 장치가 꽂힌 뒤 그 아래만 다시 훑기.
 * 다른 브리지 번호는 건드리지 않는다: 쓸 수 있는 버스 = 그 브리지의 [sec..sub] 뿐. 결과는 r 뒤에 이어 붙인다. */
static int rescan_bridge(mock *m, enum_result *r, int idx) {
    const pci_fn *b = &r->f[idx];
    enum_opts o = ENUM_DEFAULT;
    o.last_bus = b->sub;
    int before = r->err;
    r->err = ENUM_OK;
    scan_bus(m, r, &o, b->sec, idx);
    int e = r->err;
    if (before != ENUM_OK) r->err = before;
    return e;
}
/* DSP2 아래에 스위치(USP + DSP 2개) + DSP 첫째 아래 SSD 를 꽂는다 */
static void hot_add_switch(mock *m, int dsp2) {
    int usp = mk_bridge(m, below(m, dsp2), 0, 0, 0xAB70, 0x0300, PCI_EXP_TYPE_UPSTREAM, false);
    int d0 = mk_bridge(m, below(m, usp), 0, 0, 0xAB70, 0x0301, PCI_EXP_TYPE_DOWNSTREAM, true);
    mk_bridge(m, below(m, usp), 1, 0, 0xAB70, 0x0301, PCI_EXP_TYPE_DOWNSTREAM, true);
    mk_fn(m, below(m, d0), 0, 0, 0xAB30, 0x5003, 0x010802, 0, PCI_EXP_TYPE_ENDPOINT, false);
}

/* 연습 4: 재귀 없는 DFS (명시적 스택). 펌웨어 스택이 작을 때. 결과·쓰기는 scan_bus 와 같아야 한다. */
typedef struct { uint8_t bus, dev, fn, nfn; int max, parent, br; } frame;
#define STACK_MAX 256
static int enum_iter(mock *m, enum_result *r, const enum_opts *o) {
    static frame st[STACK_MAX];
    int sp = 0;
    memset(r, 0, sizeof *r);
    st[0] = (frame){ 0, 0, 0, 0, 0, -1, -1 };
    for (;;) {
        frame *f = &st[sp];
        if (f->dev >= 32) {                                  /* 이 버스 끝 → 부모 브리지의 subordinate 확정 */
            int cm = f->max;
            if (sp == 0) return cm;
            f = &st[--sp];
            cfg_wr8(m, f->bus, f->dev, f->fn, PCI_SUBORDINATE_BUS, (uint8_t)cm);
            r->f[f->br].sub = (uint8_t)cm;
            f->max = cm;
            goto next_fn;
        }
        uint32_t id;
        if (f->fn == 0) {
            if (!fn_present(m, f->bus, f->dev, 0, &id)) { f->dev++; continue; }
            uint8_t hdr0 = cfg_rd8(m, f->bus, f->dev, 0, PCI_HEADER_TYPE);
            f->nfn = ((hdr0 & PCI_HEADER_TYPE_MFD) || o->probe_all_fn) ? 8 : 1;
        } else if (!fn_present(m, f->bus, f->dev, f->fn, &id)) {
            goto next_fn;
        }
        {
            int idx = record(m, r, f->bus, f->dev, f->fn, id, f->parent);
            if (idx < 0) return f->max;
            if ((r->f[idx].hdr & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE) {
                if (f->max + 1 > o->last_bus || sp + 1 >= STACK_MAX) {
                    if (r->err == ENUM_OK) r->err = ENUM_ERR_BUSES;
                } else {
                    uint8_t sec = (uint8_t)(f->max + 1);
                    cfg_wr8(m, f->bus, f->dev, f->fn, PCI_PRIMARY_BUS, f->bus);
                    cfg_wr8(m, f->bus, f->dev, f->fn, PCI_SECONDARY_BUS, sec);
                    int t = tmp_sub_value(o, sec);
                    if (t >= 0) cfg_wr8(m, f->bus, f->dev, f->fn, PCI_SUBORDINATE_BUS, (uint8_t)t);
                    r->f[idx].pri = f->bus; r->f[idx].sec = sec;
                    f->br = idx;
                    st[++sp] = (frame){ sec, 0, 0, 0, sec, idx, -1 };   /* push: 자식 버스부터 */
                    continue;
                }
            }
        }
    next_fn:
        f = &st[sp];
        if (++f->fn >= f->nfn) { f->fn = 0; f->dev++; }
    }
}
static bool same_result(const enum_result *a, const enum_result *b) {
    if (a->n != b->n || a->err != b->err) return false;
    for (int i = 0; i < a->n; i++) {
        const pci_fn *x = &a->f[i], *y = &b->f[i];
        if (x->bus != y->bus || x->dev != y->dev || x->fn != y->fn || x->hdr != y->hdr || x->vid != y->vid ||
            x->did != y->did || x->class24 != y->class24 || x->pri != y->pri || x->sec != y->sec ||
            x->sub != y->sub || x->parent != y->parent) return false;
    }
    return true;
}

int main(void) {
    static mock m, m2;
    static enum_result r, r2;
    enum_opts o;
    enum_x x;
    int bad;

    /* 연습 1: only_one_child — 결과는 같고 config 읽기 수만 준다 */
    build_topo_a(&m);
    x = (enum_x){ false, 0 };
    enum_all_x(&m, &r, &ENUM_DEFAULT, &x);
    uint32_t full = m.cfg_reads, full_ur = m.ur_reads;
    build_topo_a(&m2);
    x = (enum_x){ true, 0 };
    enum_all_x(&m2, &r2, &ENUM_DEFAULT, &x);
    CHECK(same_result(&r, &r2));
    CHECK(m2.cfg_reads < full && m2.ur_reads < full_ur);
    printf("P1 topology A: full scan %u config reads (%u all-1s), one_child %u reads (%u all-1s)\n",
           full, full_ur, m2.cfg_reads, m2.ur_reads);
    CHECK(exp_flags(&m, 0, 1, 0) >= 0 && ((exp_flags(&m, 0, 1, 0) & PCI_EXP_FLAGS_TYPE) >> 4) == PCI_EXP_TYPE_ROOT_PORT);
    CHECK(exp_flags(&m, 0, 0, 0) == -1);                                /* host bridge: PCIe cap 없음 */

    /* 연습 2: hotplug 슬롯마다 버스 4개 예약 */
    build_topo_a(&m);
    x = (enum_x){ true, 4 };
    int last = enum_all_x(&m, &r, &ENUM_DEFAULT, &x);
    CHECK(last == 18);
    CHECK(bridge_is(&m, &r, 0, 1, 0, 0, 1, 14) && bridge_is(&m, &r, 1, 0, 0, 1, 2, 14));
    CHECK(bridge_is(&m, &r, 2, 0, 0, 2, 3, 6) && bridge_is(&m, &r, 2, 1, 0, 2, 7, 10) && bridge_is(&m, &r, 2, 2, 0, 2, 11, 14));
    CHECK(bridge_is(&m, &r, 0, 2, 0, 0, 15, 18) && find_bdf(&r, 3, 0, 0) >= 0 && find_bdf(&r, 7, 0, 3) >= 0);
    CHECK(enum_verify(&m, &r, &bad) == V_OK);
    tree_print(&r, names_a, g_tree, sizeof g_tree);
    printf("P2 hp_reserve=4 tree:\n%s", g_tree);

    /* 연습 3: 빈 슬롯(DSP2)에 스위치 hot-add → 그 아래만 rescan */
    topo_a_ids ta = build_topo_a(&m);                                   /* (a) 예약 없이 열거한 시스템 */
    enum_all(&m, &r, &ENUM_DEFAULT);
    hot_add_switch(&m, ta.dsp2);
    int i_dsp2 = find_bdf(&r, 2, 2, 0);
    int n_before = r.n;
    CHECK(rescan_bridge(&m, &r, i_dsp2) == ENUM_ERR_BUSES);             /* [05-05] 안에 USP 의 secondary 6 을 못 줌 */
    CHECK(r.n == n_before + 1 && r.f[n_before].vid == 0xAB70 && r.f[n_before].sec == 0);   /* USP 만 보이고 아래는 못 봄 */
    CHECK(count_vid(&r, 0xAB70) == 1 && count_vid(&r, 0xAB30) == 0);    /* 새 DSP 2개와 SSD 는 안 보임 */
    CHECK(bridge_is(&m, &r, 0, 2, 0, 0, 6, 6));                         /* 이웃 RP2 번호는 그대로 */
    ta = build_topo_a(&m);                                              /* (b) 연습 2 처럼 예약해 둔 시스템 */
    x = (enum_x){ true, 4 };
    enum_all_x(&m, &r, &ENUM_DEFAULT, &x);
    hot_add_switch(&m, ta.dsp2);
    i_dsp2 = find_bdf(&r, 2, 2, 0);
    CHECK(rescan_bridge(&m, &r, i_dsp2) == ENUM_OK);
    CHECK(bridge_is(&m, &r, 11, 0, 0, 11, 12, 14) && bridge_is(&m, &r, 12, 0, 0, 12, 13, 13) && bridge_is(&m, &r, 12, 1, 0, 12, 14, 14));
    int i_new = find_bdf(&r, 13, 0, 0);
    CHECK(i_new >= 0 && r.f[i_new].did == 0x5003);
    CHECK(enum_verify(&m, &r, &bad) == V_OK && bridge_is(&m, &r, 0, 2, 0, 0, 15, 18));
    int path[8];
    CHECK(enum_path(&r, i_new, path, 8) == 5);                           /* RP1 → USP → DSP2 → 새 USP → 새 DSP */
    printf("P3 hot-add under DSP2 (02:02.0): no reserve -> ENUM_ERR_BUSES; reserve 4 -> new SSD at %02x:%02x.%x, path depth %d\n",
           r.f[i_new].bus, r.f[i_new].dev, r.f[i_new].fn, enum_path(&r, i_new, path, 8));

    /* 연습 4: 반복(스택) DFS == 재귀 DFS (결과 표 + 하드웨어에 쓴 값), 정상 / 고갈 / 버그 옵션 모두 */
    const enum_opts cases[3] = { { SUB_TMP_LAST, false, 0xFF }, { SUB_TMP_LAST, false, 4 }, { SUB_TMP_SEC, false, 0xFF } };
    int same = 0;
    for (int c = 0; c < 3; c++) {
        o = cases[c];
        build_topo_a(&m);  enum_all(&m, &r, &o);
        build_topo_a(&m2); enum_iter(&m2, &r2, &o);
        same += same_result(&r, &r2) && memcmp(&m, &m2, offsetof(mock, cfg_reads)) == 0;
        build_topo_b(&m);  enum_all(&m, &r, &o);
        build_topo_b(&m2); enum_iter(&m2, &r2, &o);
        same += same_result(&r, &r2) && memcmp(&m, &m2, offsetof(mock, cfg_reads)) == 0;
    }
    CHECK(same == 6);
    build_topo_b(&m2);
    CHECK(enum_iter(&m2, &r2, &ENUM_DEFAULT) == 9 && r2.n == 17);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
