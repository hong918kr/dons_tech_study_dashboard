/* DS-A10 플래시 로그 구조 KV 저장소
 *
 * NOR 플래시 모델(섹터 소거 → 0xFF, 프로그램은 비트 1→0 만) 위의 append-only KV.
 *   Part A  NOR 모델의 물리 규칙, 레코드(CRC) 인코딩, 페이지 분할 쓰기, set/get/del, RAM 인덱스
 *   Part B  섹터 분류, 마운트 복구, 링 컴팩션(COPYING → commit → victim 소거) + 전수 전원 차단 주입
 *           (모든 program/erase 지점 × 찢어진 쓰기·부분 소거 모드, 복구 중 2차 차단까지)
 *   Part C  원자적 배치(한 레코드 = 여러 키), 부트 카운터 + 캘리브레이션 장치 시나리오, 마모 분포
 *
 * 레이아웃 (전부 4바이트 정렬, 워드는 소거 후 딱 한 번만 프로그램한다):
 *   섹터   +0 magic  +4 seq  +8 src_seq  +12 hdr_crc  +16 commit  +20 레코드...
 *   레코드 +0 key(16) +2 raw(16: bit15 tomb, bit14 batch, 나머지 len) +4 crc32 +8 payload + 0xFF 패딩
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UNUSED __attribute__((unused))

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

/* ---------- CRC-32 (IEEE 802.3, init ~0, 반사, final ~) ---------- */
static uint32_t g_crc_tab[256];
static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int b = 0; b < 8; b++) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crc_tab[i] = c;
    }
}
static uint32_t crc_feed(uint32_t c, const void *p, size_t n) {
    const uint8_t *s = p;
    while (n--) c = g_crc_tab[(c ^ *s++) & 0xFFu] ^ (c >> 8);
    return c;
}
static UNUSED uint32_t crc32_buf(const void *p, size_t n) { return ~crc_feed(~0u, p, n); }

/* ---------- NOR 플래시 모델 ---------- */
#define NOR_MAX_SEC 16u
#define NOR_MAX_BYTES (16u * 4096u)
enum { NOR_OK = 0, NOR_EPOWER = -1, NOR_EPARAM = -2 };

/* 전원 차단 모드. PREFIX k: 앞 k 바이트만 프로그램 / PARTIAL k: 앞 k 바이트 + k 번째 바이트의 일부 비트
 * RANDOM: 각 비트가 독립적으로 프로그램되거나 안 됨.
 * E_PREFIX k: [0,k) 만 소거 / E_SUFFIX_FF k: [k,끝) 만 소거 / E_SUFFIX_ZERO k: [k,끝) 이 0x00 (소거 전 pre-program 단계)
 * E_RANDOM: 0 비트가 무작위로 1 이 됨 / E_DONE: 소거가 끝난 직후 차단 */
enum { F_NONE, F_P_PREFIX, F_P_PARTIAL, F_P_RANDOM,
       F_E_PREFIX, F_E_SUFFIX_FF, F_E_SUFFIX_ZERO, F_E_RANDOM, F_E_DONE };

typedef struct {
    uint8_t *mem, *wmap;              /* wmap: 워드별 "소거 후 프로그램됨" 표시 */
    uint32_t sec_size, nsec, page, align;
    uint64_t op;                      /* program+erase 호출 번호 (전원을 껐다 켜도 계속 증가) */
    uint64_t n_prog, n_erase, prog_bytes;
    uint32_t erase_cnt[NOR_MAX_SEC];
    uint32_t viol_0to1, viol_reprog, viol_param;
    int powered;
    int64_t cut_at;                   /* 이 번호의 op 에서 전원이 끊긴다 (-1 = 해제) */
    int mode;
    uint32_t k, seed;
} nor_t;

/* 트레이스: 골든 런에서 각 op 의 종류/길이를 기록해 "어느 op 에 어떤 찢어짐이 가능한가"를 열거한다 */
#define TR_MAX 8192
static uint8_t g_tr_kind[TR_MAX];  /* 1 = program, 2 = erase */
static uint16_t g_tr_len[TR_MAX];
static uint32_t g_tr_n;
static bool g_tr_on;

static uint32_t rnd_next(uint32_t *s) {
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* TODO(A1): AND 프로그램 물리 규칙.
 *   mem[addr+i] &= src[i]. src 비트가 1 인데 mem 비트가 0 이면 viol_0to1++ (1 로는 못 돌아간다, 값은 그대로 AND).
 *   워드(align 바이트) 단위로: 그 워드에 쓰는 src 에 0 비트가 하나라도 있으면 wmap 을 보고,
 *   이미 프로그램된 워드면 viol_reprog++, 아니면 wmap[word] = 1. */
static void phys_prog(nor_t *n, uint32_t addr, const uint8_t *src, uint32_t len) {
    (void)n; (void)addr; (void)src; (void)len;
}

/* TODO(A2): 섹터 전체를 0xFF 로, wmap 도 지우고, erase_cnt[sec]++. */
static void phys_erase(nor_t *n, uint32_t sec) {
    (void)n; (void)sec;
}

static void nor_read(const nor_t *n, uint32_t addr, void *buf, uint32_t len) {
    memcpy(buf, n->mem + addr, len);
}

/* 프로그램: 정렬·페이지 경계 위반은 거부 (SPI NOR 는 페이지 안에서 wrap 하므로 드라이버가 나눠 써야 한다) */
static int nor_prog(nor_t *n, uint32_t addr, const void *srcv, uint32_t len) {
    const uint8_t *src = srcv;
    if (!n->powered) return NOR_EPOWER;
    if (len == 0 || addr % n->align || len % n->align || len > n->page
        || addr / n->page != (addr + len - 1u) / n->page || addr + len > n->sec_size * n->nsec) {
        n->viol_param++;
        return NOR_EPARAM;
    }
    uint64_t me = n->op++;
    if (g_tr_on && g_tr_n < TR_MAX) { g_tr_kind[g_tr_n] = 1; g_tr_len[g_tr_n++] = (uint16_t)len; }
    if (n->cut_at >= 0 && me == (uint64_t)n->cut_at) {
        uint32_t k = n->k > len ? len : n->k;
        uint32_t s = n->seed ^ (uint32_t)(me * 2654435761u) ^ 0x5bd1e995u;
        if (n->mode == F_P_PREFIX) {
            if (k) phys_prog(n, addr, src, k);
        } else if (n->mode == F_P_PARTIAL) {
            uint8_t eff[256];
            memcpy(eff, src, k);
            if (k < len) { eff[k] = src[k] | (uint8_t)rnd_next(&s); k++; }
            if (k) phys_prog(n, addr, eff, k);
        } else if (n->mode == F_P_RANDOM) {
            uint8_t eff[256];
            for (uint32_t i = 0; i < len; i++) eff[i] = src[i] | (uint8_t)rnd_next(&s);
            phys_prog(n, addr, eff, len);
        }
        n->powered = 0; n->cut_at = -1;
        return NOR_EPOWER;
    }
    phys_prog(n, addr, src, len);
    n->n_prog++; n->prog_bytes += len;
    return NOR_OK;
}

static int nor_erase(nor_t *n, uint32_t sec) {
    if (!n->powered) return NOR_EPOWER;
    if (sec >= n->nsec) { n->viol_param++; return NOR_EPARAM; }
    uint64_t me = n->op++;
    if (g_tr_on && g_tr_n < TR_MAX) { g_tr_kind[g_tr_n] = 2; g_tr_len[g_tr_n++] = 0; }
    if (n->cut_at >= 0 && me == (uint64_t)n->cut_at) {
        uint8_t *base = n->mem + sec * n->sec_size;
        uint8_t *wm = n->wmap + sec * (n->sec_size / n->align);
        uint32_t ws = n->sec_size / n->align, k = n->k > n->sec_size ? n->sec_size : n->k;
        uint32_t s = n->seed ^ (uint32_t)(me * 2654435761u) ^ 0x1b873593u;
        switch (n->mode) {
        case F_E_PREFIX:      memset(base, 0xFF, k); memset(wm, 0, k / n->align); break;
        case F_E_SUFFIX_FF:   memset(base + k, 0xFF, n->sec_size - k); memset(wm + k / n->align, 0, ws - k / n->align); break;
        case F_E_SUFFIX_ZERO: memset(base + k, 0x00, n->sec_size - k); memset(wm + k / n->align, 1, ws - k / n->align); break;
        case F_E_RANDOM:      for (uint32_t i = 0; i < n->sec_size; i++) base[i] |= (uint8_t)rnd_next(&s); break;
        case F_E_DONE:        phys_erase(n, sec); break;
        default: break;
        }
        n->powered = 0; n->cut_at = -1;
        return NOR_EPOWER;
    }
    phys_erase(n, sec);
    n->n_erase++;
    return NOR_OK;
}

static uint8_t g_mem[NOR_MAX_BYTES], g_wmap[NOR_MAX_BYTES];
static uint8_t g_snap_mem[NOR_MAX_BYTES], g_snap_wmap[NOR_MAX_BYTES];

static void nor_fresh(nor_t *n, uint32_t sec, uint32_t nsec, uint32_t page) {
    memset(n, 0, sizeof *n);
    n->mem = g_mem; n->wmap = g_wmap;
    n->sec_size = sec; n->nsec = nsec; n->page = page; n->align = 4u;
    memset(g_mem, 0xFF, sec * nsec);
    memset(g_wmap, 0, sec * nsec / 4u);
    n->powered = 1; n->cut_at = -1;
}
static void nor_power_on(nor_t *n) { n->powered = 1; }
static void nor_arm(nor_t *n, uint64_t at, int mode, uint32_t k, uint32_t seed) {
    n->cut_at = (int64_t)at; n->mode = mode; n->k = k; n->seed = seed;
}

/* ---------- KV 온플래시 형식 ---------- */
#define KV_MAGIC     0x4C4B5631u   /* "LKV1" */
#define SRC_NONE     0xFFFFFFFFu
#define SH_BODY      16u           /* magic, seq, src_seq, hdr_crc: 한 번의 program */
#define SH_COMMIT    16u           /* 오프셋 16: commit 워드 (컴팩션 커밋 지점) */
#define SH_SIZE      20u           /* 레코드 시작 오프셋 */
#define REC_HDR      8u
#define KV_MAX_VAL   64u
#define KV_MAX_BATCH 128u
#define KV_MAX_KEYS  32u
#define KV_MAX_REC   (REC_HDR + KV_MAX_BATCH)
#define KV_KEY_RSVD  0xFFFFu
#define RAW_TOMB     0x8000u
#define RAW_BATCH    0x4000u
#define RAW_LEN(r)   ((uint32_t)(r) & 0x3FFFu)

enum { KV_OK = 0, KV_ENOENT = -1, KV_EPOWER = -2, KV_EINVAL = -3, KV_ENOSPC = -4, KV_ECORRUPT = -5 };

typedef struct { uint32_t magic, seq, src_seq, crc; } shdr_t;
_Static_assert(sizeof(shdr_t) == SH_BODY, "header is one 16-byte program");

/* 섹터 종류. BLANK: 전체 0xFF / ACTIVE: 유효 헤더, 로그 재생 대상
 * COPYING: 컴팩션 목적지인데 commit 전 → 폐기 / DEAD: seq <= (커밋된 src_seq 최댓값) → 재소거
 * GARBAGE: 헤더 무효 + 0xFF 아님 (찢어진 헤더 또는 부분 소거) → 재소거 */
enum { S_BLANK, S_ACTIVE, S_COPYING, S_DEAD, S_GARBAGE };

typedef struct {
    uint8_t kind;
    bool dirty;           /* 로그 끝 뒤에 0xFF 아닌 바이트가 있다 (찢어진 레코드) */
    uint32_t seq, src;
    uint32_t end;         /* 다음 레코드 오프셋 (재생 결과) */
} smeta_t;

typedef struct { uint16_t key, len, sec, off; } idx_t;   /* off: 값이 시작하는 섹터 내 오프셋 */

typedef struct {
    nor_t *f;
    idx_t idx[KV_MAX_KEYS];
    uint32_t n_idx, live_bytes, cap;
    smeta_t sm[NOR_MAX_SEC];
    uint32_t head, head_off, next_seq;
    bool head_dirty;
    uint8_t rbuf[KV_MAX_REC], wbuf[KV_MAX_REC];   /* rec_read 용 / 인코딩 용 — 컴팩션이 둘을 동시에 쓴다 */
    uint8_t bbuf[KV_MAX_BATCH];                   /* 배치 스테이징 */
    uint32_t b_len, b_n;
    bool b_open;
    /* 마운트 통계 (테스트의 경로 커버리지 증명용) */
    uint32_t st_copying, st_dead, st_garbage, st_dirty;
} kv_t;

typedef struct { uint16_t key, raw; uint32_t len, size, off, sec; } rec_t;
typedef struct { uint16_t key; bool tomb; uint32_t len, val_off; const uint8_t *ptr; } ent_t;

static UNUSED uint32_t align4(uint32_t x) { return (x + 3u) & ~3u; }
static UNUSED uint32_t rec_size(uint32_t len) { return REC_HDR + align4(len); }

/* commit 워드: 정확히 이 값일 때만 커밋이다. 0x00000000 (pre-program 잔해) 과 0xFFFFFFFF 는 절대 값이 될 수 없다 */
static UNUSED uint32_t commit_value(uint32_t hdr_crc) { return ((hdr_crc ^ 0xC0117EEDu) | 1u) & ~2u; }

static UNUSED bool range_is_ff(const kv_t *kv, uint32_t sec, uint32_t off, uint32_t len) {
    const uint8_t *p = kv->f->mem + sec * kv->f->sec_size + off;
    for (uint32_t i = 0; i < len; i++) if (p[i] != 0xFFu) return false;
    return true;
}

static UNUSED idx_t *idx_find(kv_t *kv, uint16_t key) {
    for (uint32_t i = 0; i < kv->n_idx; i++) if (kv->idx[i].key == key) return &kv->idx[i];
    return NULL;
}
static UNUSED int idx_put(kv_t *kv, uint16_t key, uint32_t sec, uint32_t off, uint32_t len) {
    idx_t *e = idx_find(kv, key);
    if (!e) {
        if (kv->n_idx == KV_MAX_KEYS) return KV_ENOSPC;
        e = &kv->idx[kv->n_idx++];
        e->key = key;
    } else {
        kv->live_bytes -= rec_size(e->len);
    }
    e->sec = (uint16_t)sec; e->off = (uint16_t)off; e->len = (uint16_t)len;
    kv->live_bytes += rec_size(len);
    return KV_OK;
}
static UNUSED void idx_del(kv_t *kv, uint16_t key) {
    idx_t *e = idx_find(kv, key);
    if (!e) return;
    kv->live_bytes -= rec_size(e->len);
    *e = kv->idx[--kv->n_idx];
}

/* TODO(A3): 레코드를 out 에 인코딩하고 전체 크기(패딩 포함)를 돌려준다.
 *   out[0..1]=key, out[2..3]=raw, out[4..7]=crc32(key,raw 4바이트 + payload len 바이트, 패딩 제외),
 *   out[8..]=payload, 그 뒤를 4바이트 경계까지 0xFF 로 채운다. len = RAW_LEN(raw). */
static UNUSED uint32_t rec_encode(uint8_t *out, uint16_t key, uint16_t raw, const void *val) {
    (void)out; (void)key; (void)raw; (void)val;
    return 0;
}

/* TODO(A4): (sec, off) 의 레코드를 검증해 r 을 채우고 1, 아니면 0.
 *   - 헤더가 섹터를 넘으면 0, key == KV_KEY_RSVD 이면 0 (0xFF.. = 로그 끝)
 *   - len > KV_MAX_BATCH 또는 레코드가 섹터를 넘으면 0
 *   - payload 는 kv->rbuf 에 읽어 CRC 로 검증한다 */
static UNUSED int rec_read(kv_t *kv, uint32_t sec, uint32_t off, rec_t *r) {
    (void)kv; (void)sec; (void)off; (void)r;
    return 0;
}

/* TODO(C1): 레코드의 (키, 값) 항목을 차례로 꺼낸다. *pos 는 호출자 커서(0 부터), 항목이 있으면 1, 끝이면 0.
 *   일반 레코드: 항목 1개 (아래 스텁이 이미 처리한다).
 *   배치 레코드(raw & RAW_BATCH): payload = {key16, raw16, rsvd32(0xFFFFFFFF), value + 패딩} 의 나열.
 *   val_off 는 "섹터 안에서 그 값이 있는 오프셋", ptr 은 kv->rbuf 안의 값. 범위를 벗어나는 항목이면 0. */
static UNUSED int rec_entry_next(const kv_t *kv, const rec_t *r, uint32_t *pos, ent_t *e) {
    if (r->raw & RAW_BATCH) {
        (void)kv; (void)pos; (void)e;
        return 0;
    }
    if (*pos) return 0;
    e->key = r->key; e->tomb = (r->raw & RAW_TOMB) != 0; e->len = r->len;
    e->val_off = r->off + REC_HDR; e->ptr = kv->rbuf + REC_HDR;
    *pos = 1;
    return 1;
}

/* TODO(A5): addr 부터 len 바이트를 page 경계에서 잘라 여러 번의 nor_prog 로 쓴다 (addr, len 은 4 정렬).
 *   nor_prog 가 실패하면 그 코드를 돌려준다 (NOR_EPOWER → KV_EPOWER). */
static UNUSED int flash_write(kv_t *kv, uint32_t addr, const void *p, uint32_t len) {
    (void)kv; (void)addr; (void)p; (void)len;
    return KV_EPOWER;
}

static UNUSED int kv_erase(kv_t *kv, uint32_t s) {
    int r = nor_erase(kv->f, s);
    return r == NOR_OK ? KV_OK : (r == NOR_EPOWER ? KV_EPOWER : KV_ECORRUPT);
}

/* head 에 레코드(wbuf, size) 를 추가한다. 인덱스는 건드리지 않는다. */
static UNUSED int kv_append(kv_t *kv, uint32_t size) {
    int r = flash_write(kv, kv->head * kv->f->sec_size + kv->head_off, kv->wbuf, size);
    if (r == KV_OK) kv->head_off += size;
    return r;
}

/* TODO(B1): 비어 있는 섹터 s 의 헤더(magic,seq,src,crc 16바이트)를 한 번에 쓰고 sm[s] 를 갱신한다.
 *   src == SRC_NONE 이면 S_ACTIVE, 아니면 S_COPYING. commit 워드(+16)는 건드리지 않는다. */
static UNUSED int open_sector(kv_t *kv, uint32_t s, uint32_t seq, uint32_t src) {
    (void)kv; (void)s; (void)seq; (void)src;
    return KV_ECORRUPT;
}

/* TODO(B2): 컴팩션 + 롤오버. head 를 새 섹터로 옮긴다.
 *   D = head 다음부터 찾은 첫 BLANK 섹터. BLANK 가 없으면 KV_ECORRUPT (불변식 위반).
 *   BLANK >= 2: open_sector(D, 새 seq, SRC_NONE) 만 한다.
 *   BLANK == 1: victim V = seq 가장 작은 ACTIVE.
 *     1) open_sector(D, 새 seq, V.seq) → COPYING
 *     2) V 의 레코드를 순서대로 걸으며, 인덱스가 (V, 그 val_off) 를 가리키는 항목만 단독 레코드로 D 에 복사
 *        (인덱스도 D 로 옮긴다). tombstone 은 복사하지 않는다.
 *     3) commit 워드에 commit_value(hdr crc) 를 프로그램 ← 커밋 지점
 *     4) V 소거 → sm[V] = BLANK */
static UNUSED int roll_head(kv_t *kv) {
    (void)kv;
    return KV_ENOSPC;
}

/* TODO(A6): head 에 need 바이트 자리를 만든다. head 가 dirty 거나 공간이 모자라면 roll_head, 최대 nsec 번.
 *   끝까지 안 되면 KV_ENOSPC. */
static UNUSED int ensure_room(kv_t *kv, uint32_t need) {
    (void)kv; (void)need;
    return KV_ENOSPC;
}

/* TODO(A7): key 에 val 을 쓴다.
 *   key == KV_KEY_RSVD 또는 len > KV_MAX_VAL → KV_EINVAL.
 *   새로 들어갈 live 바이트(rec_size)가 kv->cap 을 넘거나 인덱스가 꽉 차면 KV_ENOSPC (플래시를 건드리기 전에).
 *   레코드를 wbuf 에 인코딩 → ensure_room → kv_append → 성공한 뒤에만 인덱스 갱신. */
static int kv_set(kv_t *kv, uint16_t key, const void *val, uint16_t len) {
    (void)kv; (void)key; (void)val; (void)len;
    return KV_ENOSPC;
}

/* TODO(A8): tombstone 레코드(raw = RAW_TOMB, len 0)를 추가한다. 없는 키면 KV_ENOENT. */
static int kv_del(kv_t *kv, uint16_t key) {
    (void)kv; (void)key;
    return KV_ENOENT;
}

/* TODO(A9): 값 읽기. 없으면 KV_ENOENT. cap < len 이면 KV_EINVAL. *out_len 에 길이를 돌려준다. */
static int kv_get(const kv_t *kv, uint16_t key, void *buf, uint16_t cap, uint16_t *out_len) {
    (void)kv; (void)key; (void)buf; (void)cap; (void)out_len;
    return KV_ENOENT;
}

/* TODO(B3): 재생: ACTIVE 섹터를 seq 오름차순으로 돌며 레코드를 재생해 인덱스를 다시 만든다.
 *   첫 무효 레코드에서 멈추고, sm[s].end = 그 오프셋, sm[s].dirty = (그 뒤에 0xFF 아닌 바이트가 있음).
 *   dirty 면 st_dirty++. 끝에 head = seq 최대 ACTIVE, head_off/head_dirty 를 맞춘다. */
static UNUSED void kv_replay(kv_t *kv) {
    (void)kv;
}

/* TODO(B4): 마운트 — 섹터 분류 → 죽은 섹터 재소거 → 재생.
 *   헤더(CRC OK)가 유효하면 일단 ACTIVE; src != NONE 이고 commit 이 정확히 commit_value(crc) 가 아니면 COPYING.
 *   헤더 무효면 전체 0xFF 인지 보고 BLANK / GARBAGE.
 *   committed_src = (commit 이 정확한 섹터들의 src 중 최댓값). seq <= committed_src 인 섹터는 DEAD (ACTIVE 로 재생하면 안 된다: 부분 소거로 tombstone 만 지워지면 옛값이 되살아난다).
 *   COPYING / DEAD / GARBAGE 는 st_* 를 세고 지운 뒤 BLANK (KV_EPOWER 는 그대로 전파).
 *   next_seq = 유효 헤더 seq 최댓값 + 1 (하나도 없으면 1). 재생 후 ACTIVE 가 하나도 없으면 sector 0 을 새로 연다. */
static int kv_mount(kv_t *kv, nor_t *f) {
    memset(kv, 0, sizeof *kv);
    kv->f = f;
    uint32_t usable = f->sec_size - SH_SIZE;
    kv->cap = (f->nsec >= 3u ? f->nsec - 2u : 1u) * (usable - KV_MAX_REC);
    return KV_ECORRUPT;
}

/* TODO(C2): 배치 커밋 (kv_batch_begin/set/del 은 RAM 스테이징만 한다 — 아래에 있다).
 *   스테이징된 bbuf 전체를 레코드 하나(raw = RAW_BATCH | b_len)로 쓴다. CRC 하나 = 모든 키가 함께 보이거나 함께 안 보임.
 *   live 바이트 재계산으로 cap 초과면 KV_ENOSPC, 쓰고 나서 인덱스 갱신. 끝나면 b_open = false (실패해도). */
static int kv_batch_commit(kv_t *kv) {
    if (!kv->b_open) return KV_EINVAL;
    kv->b_open = false;
    (void)kv;
    return KV_ENOSPC;
}

/* 배치 스테이징 (RAM 만): 항목 = key16, raw16, rsvd32(0xFFFFFFFF), value + 0xFF 패딩. 같은 키 두 번은 거절 */
static UNUSED int kv_batch_begin(kv_t *kv) { kv->b_open = true; kv->b_len = 0; kv->b_n = 0; return KV_OK; }
static int batch_add(kv_t *kv, uint16_t key, uint16_t raw, const void *val) {
    uint32_t len = RAW_LEN(raw);
    if (!kv->b_open || key == KV_KEY_RSVD || len > KV_MAX_VAL) return KV_EINVAL;
    if (kv->b_len + rec_size(len) > KV_MAX_BATCH) return KV_EINVAL;
    for (uint32_t p = 0; p < kv->b_len;) {
        uint16_t k, r;
        memcpy(&k, kv->bbuf + p, 2); memcpy(&r, kv->bbuf + p + 2, 2);
        if (k == key) return KV_EINVAL;
        p += rec_size(RAW_LEN(r));
    }
    uint8_t *p = kv->bbuf + kv->b_len;
    memcpy(p, &key, 2); memcpy(p + 2, &raw, 2); memset(p + 4, 0xFF, 4);
    if (len) memcpy(p + REC_HDR, val, len);
    memset(p + REC_HDR + len, 0xFF, rec_size(len) - REC_HDR - len);
    kv->b_len += rec_size(len);
    kv->b_n++;
    return KV_OK;
}
static UNUSED int kv_batch_set(kv_t *kv, uint16_t key, const void *val, uint16_t len) { return batch_add(kv, key, len, val); }
static UNUSED int kv_batch_del(kv_t *kv, uint16_t key) { return batch_add(kv, key, RAW_TOMB, NULL); }

/* ---------- 불변식 검사 (TODO 와 독립) ---------- */
static bool kv_check(const kv_t *kv) {
    const nor_t *f = kv->f;
    uint32_t sum = 0, blanks = 0;
    if (kv->n_idx > KV_MAX_KEYS) return false;
    for (uint32_t i = 0; i < kv->n_idx; i++) {
        const idx_t *e = &kv->idx[i];
        if (e->sec >= f->nsec || e->off + (uint32_t)e->len > f->sec_size || e->len > KV_MAX_VAL) return false;
        if (kv->sm[e->sec].kind != S_ACTIVE) return false;
        for (uint32_t j = 0; j < i; j++) if (kv->idx[j].key == e->key) return false;
        sum += REC_HDR + ((e->len + 3u) & ~3u);
    }
    if (sum != kv->live_bytes) return false;
    for (uint32_t s = 0; s < f->nsec; s++) {
        if (kv->sm[s].kind == S_BLANK) {
            blanks++;
            const uint8_t *p = f->mem + s * f->sec_size;
            for (uint32_t i = 0; i < f->sec_size; i++) if (p[i] != 0xFFu) return false;
        } else if (kv->sm[s].kind != S_ACTIVE) return false;
    }
    if (blanks < 1u || kv->sm[kv->head].kind != S_ACTIVE || kv->head_off % 4u || kv->head_off > f->sec_size) return false;
    if (!kv->head_dirty) {
        const uint8_t *p = f->mem + kv->head * f->sec_size;
        for (uint32_t i = kv->head_off; i < f->sec_size; i++) if (p[i] != 0xFFu) return false;
    }
    return f->viol_0to1 == 0 && f->viol_reprog == 0 && f->viol_param == 0;
}

/* ---------- 참조 모델과 스크립트 ---------- */
#define KEYS 32
#define KV_BATCH_ENT 4
typedef struct { bool present; uint16_t len; uint8_t val[KV_MAX_VAL]; } refv_t;
typedef struct { refv_t v[KEYS]; } ref_t;
enum { OP_SET, OP_DEL, OP_BATCH };
typedef struct { uint16_t key, len; bool tomb; } opent_t;
typedef struct { uint8_t kind, n; opent_t e[KV_BATCH_ENT]; } op_t;

/* 값은 (op 번호, 항목, 바이트 위치) 의 결정적 함수 — 같은 값이 다른 op 에서 나오지 않게, 0xFF/0x00 도 섞는다 */
static uint8_t val_byte(int op_i, int ent, uint32_t i) {
    if (op_i % 5 == 0) return 0xFFu;
    if (i % 7u == 3u) return 0xFFu;
    if (i % 11u == 2u) return 0x00u;
    uint32_t h = ((uint32_t)op_i * 2654435761u) ^ ((uint32_t)ent * 2246822519u) ^ (i * 3266489917u);
    h ^= h >> 13;
    return (uint8_t)h;
}
static void val_gen(int op_i, int ent, uint16_t len, uint8_t *out) {
    for (uint16_t i = 0; i < len; i++) out[i] = val_byte(op_i, ent, i);
}
static void ref_apply(ref_t *r, const op_t *o, int op_i) {
    for (int j = 0; j < o->n; j++) {
        const opent_t *e = &o->e[j];
        refv_t *v = &r->v[e->key - 1];
        if (e->tomb) { v->present = false; v->len = 0; continue; }
        v->present = true; v->len = e->len;
        val_gen(op_i, j, e->len, v->val);
    }
}
static int exec_op(kv_t *kv, const op_t *o, int op_i) {
    uint8_t b[KV_MAX_VAL];
    if (o->kind == OP_SET) { val_gen(op_i, 0, o->e[0].len, b); return kv_set(kv, o->e[0].key, b, o->e[0].len); }
    if (o->kind == OP_DEL) return kv_del(kv, o->e[0].key);
    kv_batch_begin(kv);
    for (int j = 0; j < o->n; j++) {
        int r;
        if (o->e[j].tomb) r = kv_batch_del(kv, o->e[j].key);
        else { val_gen(op_i, j, o->e[j].len, b); r = kv_batch_set(kv, o->e[j].key, b, o->e[j].len); }
        if (r) return r;
    }
    return kv_batch_commit(kv);
}
static bool state_eq(const kv_t *kv, const ref_t *r) {
    uint8_t b[KV_MAX_VAL]; uint16_t l = 0;
    for (int k = 0; k < KEYS; k++) {
        int rc = kv_get(kv, (uint16_t)(k + 1), b, KV_MAX_VAL, &l);
        if (rc == KV_ENOENT) { if (r->v[k].present) return false; continue; }
        if (rc != KV_OK || !r->v[k].present || l != r->v[k].len) return false;
        if (l && memcmp(b, r->v[k].val, l)) return false;
    }
    return true;
}

static op_t g_ops[2000];
static int g_nops;
static uint32_t g_rng;
static uint16_t pick_len(void) {
    static const uint16_t tab[] = {0, 4, 7, 12, 16, 24, 30, 40, 52, 64};
    return tab[rnd_next(&g_rng) % 10u];
}
static void add_set(uint16_t k, uint16_t l) { g_ops[g_nops].kind = OP_SET; g_ops[g_nops].n = 1; g_ops[g_nops].e[0] = (opent_t){k, l, false}; g_nops++; }
static void add_del(uint16_t k) { g_ops[g_nops].kind = OP_DEL; g_ops[g_nops].n = 1; g_ops[g_nops].e[0] = (opent_t){k, 0, true}; g_nops++; }

/* cold 키 ncold 개를 쓰고, hot 키들을 덮어쓰기·삭제하는 스크립트 (반쯤은 cold 재기록/재생성 포함) */
static void build_script(int nops, uint16_t ncold, uint16_t nhot, uint32_t seed, bool batches) {
    g_nops = 0; g_rng = seed;
    bool present[KEYS + 1];
    memset(present, 0, sizeof present);
    for (uint16_t k = 1; k <= ncold; k++) { add_set(k, (uint16_t)(12u + 2u * k % 32u)); present[k] = true; }
    while (g_nops < nops) {
        uint32_t r = rnd_next(&g_rng) % 100u;
        uint16_t hot = (uint16_t)(ncold + 1u + rnd_next(&g_rng) % nhot);
        if (r < 10u && present[hot]) { add_del(hot); present[hot] = false; }
        else if (batches && r < 30u) {
            op_t *o = &g_ops[g_nops++];
            o->kind = OP_BATCH; o->n = 3;
            for (int j = 0; j < 3; j++) {
                uint16_t k = (uint16_t)(ncold + 1u + (uint32_t)j);
                bool tomb = (j == 2) && (rnd_next(&g_rng) % 3u == 0u);
                o->e[j] = (opent_t){k, tomb ? (uint16_t)0 : (uint16_t)(8u + 4u * (uint32_t)j), tomb};
                present[k] = !tomb;
            }
        } else if (r < 36u) {
            uint16_t c = (uint16_t)(1u + rnd_next(&g_rng) % ncold);
            add_set(c, pick_len()); present[c] = true;
        } else { add_set(hot, pick_len()); present[hot] = true; }
    }
}

/* ---------- 전수 주입 엔진 ---------- */
typedef struct { int mode; uint32_t k, seed; } fault_t;
typedef struct { uint32_t sec, nsec, page; int cont; bool level2; } xcfg_t;
typedef struct { uint64_t armed, cuts1, cuts2, bad; uint64_t copying, dead, garbage, dirty, infl_old, infl_new; } xstat_t;

/* level 1: program = 모든 prefix(k=0..len) + 모든 partial(k) + random×3 / erase = k 위치 × 세 모드 + random×3 + done */
static int faults_for(int kind, uint32_t len, const xcfg_t *c, int level, fault_t *out) {
    int n = 0;
    if (kind == 1) {
        if (level == 1) {
            for (uint32_t k = 0; k <= len; k++) out[n++] = (fault_t){F_P_PREFIX, k, 0};
            for (uint32_t k = 0; k < len; k++) out[n++] = (fault_t){F_P_PARTIAL, k, k + 11u};
            for (uint32_t s = 1; s <= 3; s++) out[n++] = (fault_t){F_P_RANDOM, 0, s};
        } else {
            out[n++] = (fault_t){F_P_PREFIX, 0, 0};
            out[n++] = (fault_t){F_P_PREFIX, len / 2u, 0};
            out[n++] = (fault_t){F_P_RANDOM, 0, 7};
        }
    } else {
        if (level == 1) {
            for (int m = F_E_PREFIX; m <= F_E_SUFFIX_ZERO; m++) {
                for (uint32_t k = 0; k <= 20u; k += 4u) out[n++] = (fault_t){m, k, 0};
                for (uint32_t k = c->page; k < c->sec; k += c->page) out[n++] = (fault_t){m, k, 0};
            }
            for (uint32_t s = 1; s <= 3; s++) out[n++] = (fault_t){F_E_RANDOM, 0, s};
            out[n++] = (fault_t){F_E_DONE, 0, 0};
        } else {
            out[n++] = (fault_t){F_E_PREFIX, 0, 0};
            out[n++] = (fault_t){F_E_SUFFIX_ZERO, 16, 0};
            out[n++] = (fault_t){F_E_RANDOM, 0, 9};
            out[n++] = (fault_t){F_E_DONE, 0, 0};
        }
    }
    return n;
}

static int g_lvl = 1;   /* 1차 차단 / 2차 차단 (복구 중) */
static void note_bad(xstat_t *x, const char *why, uint64_t cut, int mode, uint32_t k) {
    x->bad++;
    if (x->bad <= 4) printf("  [bad L%d] %s (1st cut=%llu mode=%d k=%u)\n", g_lvl, why, (unsigned long long)cut, mode, k);
}

/* 차단 후 복구: 마운트 → 전 키 = ref (in-flight 면 ref 또는 ref+op) → 불변식. ref 를 결정된 쪽으로 갱신 */
static bool recover(kv_t *kv, nor_t *f, const op_t *ops, int infl, ref_t *ref, xstat_t *x, uint64_t cut, int mode, uint32_t k) {
    nor_power_on(f);
    if (kv_mount(kv, f) != KV_OK) { note_bad(x, "mount failed", cut, mode, k); return false; }
    x->copying += kv->st_copying; x->dead += kv->st_dead; x->garbage += kv->st_garbage; x->dirty += kv->st_dirty;
    ref_t r2 = *ref;
    if (infl >= 0) ref_apply(&r2, &ops[infl], infl);
    if (state_eq(kv, ref)) { if (infl >= 0) x->infl_old++; }
    else if (infl >= 0 && state_eq(kv, &r2)) { *ref = r2; x->infl_new++; }
    else { note_bad(x, "state mismatch after remount", cut, mode, k); return false; }
    if (!kv_check(kv)) { note_bad(x, "invariant", cut, mode, k); return false; }
    return true;
}

/* 스크립트 [from,to) 실행. 전원이 끊기면 그 op 번호를 돌려준다(-1 = 끝까지) */
static int run_ops(kv_t *kv, const op_t *ops, int from, int to, ref_t *ref, xstat_t *x, bool *err) {
    for (int i = from; i < to; i++) {
        int r = exec_op(kv, &ops[i], i);
        if (r == KV_EPOWER) return i;
        if (r == KV_ENOENT && ops[i].kind == OP_DEL && !ref->v[ops[i].e[0].key - 1].present) continue;
        if (r != KV_OK) { note_bad(x, "op failed", (uint64_t)i, r, 0); *err = true; return -1; }
        ref_apply(ref, &ops[i], i);
    }
    return -1;
}

static kv_t g_kv;

static void snap_restore(nor_t *f, const nor_t *snap, uint32_t total) {
    uint8_t *m = f->mem, *w = f->wmap;
    *f = *snap;
    f->mem = m; f->wmap = w;
    memcpy(m, g_snap_mem, total); memcpy(w, g_snap_wmap, total / 4u);
}

static fault_t g_fl1[1024], g_fl2[32];

/* 1차 차단 하나 처리: 차단 → 스냅샷 → 복구+계속+재마운트 검증; level2 면 복구 창의 모든 op 에 2차 차단 */
static void one_cut(const xcfg_t *c, const op_t *ops, int nops, uint64_t cut, const fault_t *flt, bool canon, xstat_t *x) {
    static ref_t ref0, ref, ref_w;
    nor_t f;
    kv_t *kv = &g_kv;
    nor_fresh(&f, c->sec, c->nsec, c->page);
    memset(&ref, 0, sizeof ref);
    nor_arm(&f, cut, flt->mode, flt->k, flt->seed);
    x->armed++;
    int infl = -1, j0 = 0;
    bool err = false;
    int rc = kv_mount(kv, &f);
    if (rc == KV_OK) {
        infl = run_ops(kv, ops, 0, nops, &ref, x, &err);
        if (err) return;
        j0 = infl >= 0 ? infl + 1 : nops;
    } else if (rc != KV_EPOWER) { note_bad(x, "fresh mount", cut, flt->mode, flt->k); return; }
    if (f.powered) return;   /* 이 cut 은 이번 실행에서 발화하지 않음 */
    x->cuts1++;
    nor_t snap = f;
    uint32_t total = c->sec * c->nsec;
    memcpy(g_snap_mem, g_mem, total); memcpy(g_snap_wmap, g_wmap, total / 4u);
    memcpy(&ref0, &ref, sizeof ref);

    if (!recover(kv, &f, ops, infl, &ref, x, cut, flt->mode, flt->k)) return;
    int end = j0 + c->cont > nops ? nops : j0 + c->cont;
    int after = run_ops(kv, ops, j0, end, &ref, x, &err);
    if (err || after != -1) { note_bad(x, "post-recovery ops", cut, flt->mode, flt->k); return; }
    if (!recover(kv, &f, ops, -1, &ref, x, cut, flt->mode, flt->k)) return;

    if (!c->level2 || !canon) return;
    /* 골든 복구 창 길이 */
    snap_restore(&f, &snap, total);
    nor_power_on(&f);
    g_tr_on = true; g_tr_n = 0;
    memcpy(&ref_w, &ref0, sizeof ref);
    (void)kv_mount(kv, &f);
    if (state_eq(kv, &ref_w) == false && infl >= 0) ref_apply(&ref_w, &ops[infl], infl);
    (void)run_ops(kv, ops, j0, end, &ref_w, x, &err);
    g_tr_on = false;
    uint32_t win = g_tr_n > 40u ? 40u : g_tr_n;
    static uint8_t lk[40]; static uint16_t ll[40];
    memcpy(lk, g_tr_kind, win); memcpy(ll, g_tr_len, win * sizeof ll[0]);
    for (uint32_t j2 = 0; j2 < win; j2++) {
        int n = faults_for(lk[j2], ll[j2], c, 2, g_fl2);
        for (int mi = 0; mi < n; mi++) {
            snap_restore(&f, &snap, total);
            nor_power_on(&f);
            memcpy(&ref, &ref0, sizeof ref);
            nor_arm(&f, snap.op + j2, g_fl2[mi].mode, g_fl2[mi].k, g_fl2[mi].seed);
            x->cuts2++;
            g_lvl = 2;
            int r = kv_mount(kv, &f);
            int infl2 = infl;
            if (r == KV_OK) {
                ref_t r2 = ref;
                if (infl >= 0) ref_apply(&r2, &ops[infl], infl);
                if (state_eq(kv, &ref)) {}
                else if (infl >= 0 && state_eq(kv, &r2)) memcpy(&ref, &r2, sizeof ref);
                else { note_bad(x, "state after clean recovery", cut, g_fl2[mi].mode, g_fl2[mi].k); continue; }
                infl2 = run_ops(kv, ops, j0, end, &ref, x, &err);
                if (err) continue;
                if (infl2 < 0) { f.cut_at = -1; infl2 = -1; }
            } else if (r != KV_EPOWER) { note_bad(x, "mount rc", cut, g_fl2[mi].mode, g_fl2[mi].k); continue; }
            f.cut_at = -1;
            if (!recover(kv, &f, ops, infl2, &ref, x, cut, g_fl2[mi].mode, g_fl2[mi].k)) continue;
        }
    }
    g_lvl = 1;
}

static void exhaustive(const xcfg_t *c, const op_t *ops, int nops, xstat_t *x) {
    memset(x, 0, sizeof *x);
    static uint8_t tk[TR_MAX]; static uint16_t tl[TR_MAX];
    static ref_t ref;
    nor_t f;
    kv_t *kv = &g_kv;
    bool err = false;
    nor_fresh(&f, c->sec, c->nsec, c->page);
    memset(&ref, 0, sizeof ref);
    g_tr_on = true; g_tr_n = 0;
    if (kv_mount(kv, &f) != KV_OK) { note_bad(x, "golden mount", 0, 0, 0); g_tr_on = false; return; }
    (void)run_ops(kv, ops, 0, nops, &ref, x, &err);
    g_tr_on = false;
    if (err || !state_eq(kv, &ref) || !kv_check(kv)) { note_bad(x, "golden run", 0, 0, 0); return; }
    uint32_t T = g_tr_n;
    memcpy(tk, g_tr_kind, T); memcpy(tl, g_tr_len, T * sizeof tl[0]);
    for (uint32_t i = 0; i < T; i++) {
        int n = faults_for(tk[i], tl[i], c, 1, g_fl1);
        for (int m = 0; m < n; m++) {
            bool canon = (g_fl1[m].mode == F_P_PREFIX && g_fl1[m].k == tl[i] / 2u)
                      || (g_fl1[m].mode == F_E_SUFFIX_ZERO && g_fl1[m].k == 16u);
            one_cut(c, ops, nops, i, &g_fl1[m], canon, x);
        }
    }
}

static void print_stat(const char *name, int nops, const xstat_t *x) {
    printf("  %s: ops=%d flash-cuts(1st)=%llu (armed %llu) cuts-in-recovery(2nd)=%llu bad=%llu\n"
           "    copying-discard=%llu dead=%llu garbage=%llu dirty-tail=%llu | in-flight old=%llu new=%llu\n",
           name, nops, (unsigned long long)x->cuts1, (unsigned long long)x->armed, (unsigned long long)x->cuts2,
           (unsigned long long)x->bad, (unsigned long long)x->copying, (unsigned long long)x->dead,
           (unsigned long long)x->garbage, (unsigned long long)x->dirty,
           (unsigned long long)x->infl_old, (unsigned long long)x->infl_new);
}

/* ---------- Part A ---------- */
static bool g_a_ok;
static void part_a(void) {
    int p0 = g_pass, t0 = g_total;
    puts("== Part A: NOR 규칙 + 레코드 + set/get/del");
    static nor_t f;
    nor_fresh(&f, 512, 4, 64);
    uint8_t buf[8];
    /* 물리 규칙 */
    uint8_t w4[4] = {0xF0, 0x0F, 0xAA, 0x55};
    CHECK(nor_prog(&f, 0, w4, 4) == NOR_OK && f.mem[0] == 0xF0 && f.mem[3] == 0x55);
    uint8_t w4b[4] = {0xFF, 0xFF, 0x0F, 0xFF};
    (void)nor_prog(&f, 8, w4b, 4);
    (void)nor_prog(&f, 8, w4, 4);   /* 같은 워드 재프로그램: AND + 위반 카운트 */
    CHECK(f.mem[8] == (0xFFu & 0xF0u) && f.mem[10] == (0x0Fu & 0xAAu) && f.viol_reprog == 1u);
    CHECK(f.viol_0to1 >= 1u);
    uint8_t w4c[4] = {0, 0, 0, 0};
    uint32_t v0 = f.viol_param;
    CHECK(nor_prog(&f, 2, w4c, 4) == NOR_EPARAM && nor_prog(&f, 62, w4c, 4) == NOR_EPARAM && f.viol_param == v0 + 2u);
    CHECK(nor_erase(&f, 0) == NOR_OK && f.mem[0] == 0xFFu && f.erase_cnt[0] == 1u && f.wmap[0] == 0u);
    nor_read(&f, 8, buf, 4);
    CHECK(buf[0] == 0xFFu);

    /* 레코드 + KV */
    static kv_t kv;
    nor_fresh(&f, 512, 4, 64);
    CHECK(kv_mount(&kv, &f) == KV_OK);
    shdr_t h; nor_read(&f, 0, &h, SH_BODY);
    CHECK(h.magic == KV_MAGIC && h.seq == 1u && h.src_seq == SRC_NONE);
    uint8_t rd[KV_MAX_VAL]; uint16_t len = 0;
    CHECK(kv_get(&kv, 7, rd, sizeof rd, &len) == KV_ENOENT);
    CHECK(kv_set(&kv, KV_KEY_RSVD, "x", 1) == KV_EINVAL);
    uint8_t big[KV_MAX_VAL + 4];
    memset(big, 0x5A, sizeof big);
    CHECK(kv_set(&kv, 1, big, KV_MAX_VAL + 1u) == KV_EINVAL);
    CHECK(kv_set(&kv, 1, "hello", 5) == KV_OK);
    CHECK(kv_get(&kv, 1, rd, sizeof rd, &len) == KV_OK && len == 5u && memcmp(rd, "hello", 5) == 0);
    /* 덮어쓰기: 옛 레코드 바이트는 그대로 (append-only) */
    uint8_t oldb[8];
    nor_read(&f, SH_SIZE, oldb, 8);
    CHECK(kv_set(&kv, 1, "world!!", 7) == KV_OK);
    uint8_t oldc[8];
    nor_read(&f, SH_SIZE, oldc, 8);
    CHECK(memcmp(oldb, oldc, 8) == 0);
    CHECK(kv_get(&kv, 1, rd, sizeof rd, &len) == KV_OK && len == 7u && memcmp(rd, "world!!", 7) == 0);
    /* 0xFF 값, 빈 값 */
    uint8_t ffs[16]; memset(ffs, 0xFF, sizeof ffs);
    CHECK(kv_set(&kv, 2, ffs, 16) == KV_OK && kv_set(&kv, 3, "", 0) == KV_OK);
    CHECK(kv_get(&kv, 2, rd, sizeof rd, &len) == KV_OK && len == 16u && memcmp(rd, ffs, 16) == 0);
    CHECK(kv_get(&kv, 3, rd, sizeof rd, &len) == KV_OK && len == 0u);
    /* 삭제 */
    CHECK(kv_del(&kv, 3) == KV_OK && kv_get(&kv, 3, rd, sizeof rd, &len) == KV_ENOENT);
    CHECK(kv_del(&kv, 42) == KV_ENOENT);
    /* 페이지 경계: 긴 레코드는 program 이 2번 이상 */
    uint64_t np0 = f.n_prog;
    memset(big, 0xC3, sizeof big);
    CHECK(kv_set(&kv, 4, big, KV_MAX_VAL) == KV_OK && f.n_prog >= np0 + 2u && f.viol_param == 0u);
    CHECK(kv_get(&kv, 4, rd, sizeof rd, &len) == KV_OK && len == KV_MAX_VAL && memcmp(rd, big, KV_MAX_VAL) == 0);
    CHECK(kv_check(&kv));
    /* 재마운트 */
    static kv_t kv2;
    CHECK(kv_mount(&kv2, &f) == KV_OK);
    CHECK(kv_get(&kv2, 1, rd, sizeof rd, &len) == KV_OK && len == 7u && memcmp(rd, "world!!", 7) == 0);
    CHECK(kv_get(&kv2, 3, rd, sizeof rd, &len) == KV_ENOENT && kv_get(&kv2, 2, rd, sizeof rd, &len) == KV_OK);
    /* CRC: 마지막 레코드의 payload 비트를 뒤집으면 그 레코드는 없는 것이 된다 (키 4 는 사라짐, 3 까지 유지) */
    bool can_flip = kv2.head_off >= SH_SIZE + rec_size(KV_MAX_VAL);
    if (can_flip) f.mem[kv2.head * f.sec_size + kv2.head_off - rec_size(KV_MAX_VAL) + REC_HDR + 5] &= (uint8_t)~1u;
    CHECK(can_flip && kv_mount(&kv2, &f) == KV_OK && kv_get(&kv2, 4, rd, sizeof rd, &len) == KV_ENOENT && kv2.head_dirty);
    CHECK(kv_get(&kv2, 2, rd, sizeof rd, &len) == KV_OK);
    /* CRC 의 함정: 빈(0xFF) 레코드 헤더는 유효할 수 없다, 0 으로 채운 헤더도 */
    static kv_t k3;
    nor_fresh(&f, 512, 4, 64);
    k3.f = &f;
    rec_t rc;
    CHECK(rec_read(&k3, 0, SH_SIZE, &rc) == 0);
    memset(f.mem + SH_SIZE, 0, REC_HDR);
    CHECK(rec_read(&k3, 0, SH_SIZE, &rc) == 0);
    g_a_ok = (g_pass - p0 == g_total - t0);
}

/* 고정 전수 주입 설정 */
static void part_b(void) {
    if (!g_a_ok) { puts("SKIP Part B: Part A failed"); g_total += 25; return; }
    puts("== Part B: 마운트 복구 + 링 컴팩션 + 전수 전원 차단");
    static nor_t f;
    static kv_t kv, kv2;
    uint8_t rd[KV_MAX_VAL]; uint16_t len = 0;
    uint8_t val[KV_MAX_VAL];

    /* 손으로 만든 사례 1: 찢어진 레코드 뒤에 append 하지 않는다 */
    nor_fresh(&f, 512, 4, 64);
    CHECK(kv_mount(&kv, &f) == KV_OK);
    CHECK(kv_set(&kv, 1, "aaaaaaaa", 8) == KV_OK);
    uint64_t pc = f.op;
    nor_arm(&f, pc, F_P_PREFIX, 6, 0);       /* 다음 program 은 앞 6바이트만 */
    memset(val, 0x33, 12);
    CHECK(kv_set(&kv, 2, val, 12) == KV_EPOWER);
    nor_power_on(&f);
    CHECK(kv_mount(&kv, &f) == KV_OK && kv.head_dirty && kv.st_dirty == 1u);
    CHECK(kv_get(&kv, 2, rd, sizeof rd, &len) == KV_ENOENT && kv_get(&kv, 1, rd, sizeof rd, &len) == KV_OK);
    uint32_t h_before = kv.head;
    CHECK(kv_set(&kv, 2, val, 12) == KV_OK && kv.head != h_before && f.viol_0to1 == 0u);
    CHECK(kv_mount(&kv2, &f) == KV_OK && kv_get(&kv2, 2, rd, sizeof rd, &len) == KV_OK && len == 12u);

    /* 손으로 만든 사례 2: 컴팩션 뒤 tombstone 된 키가 부활하지 않고, cold 키는 살아남는다 */
    nor_fresh(&f, 512, 4, 64);
    CHECK(kv_mount(&kv, &f) == KV_OK);
    memset(val, 0x11, 40);
    CHECK(kv_set(&kv, 1, val, 40) == KV_OK && kv_set(&kv, 2, val, 40) == KV_OK);
    CHECK(kv_del(&kv, 2) == KV_OK);
    bool ok = true;
    for (int i = 0; i < 80 && ok; i++) { val[0] = (uint8_t)i; ok = kv_set(&kv, 3, val, 60) == KV_OK && kv_check(&kv); }
    CHECK(ok);
    CHECK(f.n_erase >= 2u);
    CHECK(kv_mount(&kv2, &f) == KV_OK);
    CHECK(kv_get(&kv2, 2, rd, sizeof rd, &len) == KV_ENOENT);
    CHECK(kv_get(&kv2, 1, rd, sizeof rd, &len) == KV_OK && len == 40u && rd[1] == 0x11u);

    /* 용량: cap 까지는 들어가고, 넘으면 ENOSPC (플래시 변화 없음), 지우면 다시 된다 */
    nor_fresh(&f, 512, 4, 64);
    CHECK(kv_mount(&kv, &f) == KV_OK);
    memset(val, 0x22, 64);
    uint32_t stored = 0;
    int last = KV_OK;
    uint16_t cnt = 0;
    while (cnt < 32u && (last = kv_set(&kv, (uint16_t)(cnt + 1u), val, 64)) == KV_OK) { cnt++; stored += rec_size(64); }
    CHECK(last == KV_ENOSPC && stored <= kv.cap && stored + rec_size(64) > kv.cap && kv_check(&kv));
    uint64_t ops_before = f.op;
    CHECK(kv_set(&kv, (uint16_t)(cnt + 1u), val, 64) == KV_ENOSPC && f.op == ops_before);
    CHECK(kv_del(&kv, 1) == KV_OK && kv_set(&kv, 1, val, 64) == KV_OK && kv_check(&kv));
    CHECK(kv_mount(&kv2, &f) == KV_OK && kv_check(&kv2));

    /* 임의 연산 스트레스 vs 참조 모델, 장애 없음 */
    nor_fresh(&f, 1024, 8, 64);
    CHECK(kv_mount(&kv, &f) == KV_OK);
    static ref_t ref;
    memset(&ref, 0, sizeof ref);
    g_rng = 0xCAFEBABEu;
    bool ok2 = true;
    uint32_t stress_ops = 200000u;
    for (uint32_t i = 0; i < stress_ops && ok2; i++) {
        uint16_t k = (uint16_t)(1u + rnd_next(&g_rng) % 24u);
        uint32_t r = rnd_next(&g_rng) % 100u;
        if (r < 15u) {
            int rc = kv_del(&kv, k);
            if (ref.v[k - 1].present) { ok2 = rc == KV_OK; ref.v[k - 1].present = false; }
            else ok2 = rc == KV_ENOENT;
        } else {
            uint16_t l = pick_len();
            for (uint16_t j = 0; j < l; j++) val[j] = (uint8_t)(i + j * 37u);
            int rc = kv_set(&kv, k, val, l);
            if (rc == KV_OK) { ref.v[k - 1].present = true; ref.v[k - 1].len = l; memcpy(ref.v[k - 1].val, val, l); }
            else ok2 = rc == KV_ENOSPC;
        }
        if ((i % 16u) == 0u) ok2 = ok2 && state_eq(&kv, &ref);
        if ((i % 4096u) == 0u) ok2 = ok2 && kv_check(&kv);
    }
    ok2 = ok2 && state_eq(&kv, &ref) && kv_check(&kv) && kv_mount(&kv2, &f) == KV_OK && state_eq(&kv2, &ref);
    CHECK(ok2);
    printf("  stress: %u ops, 8x1024 NOR, erases=%llu, prog_bytes=%llu\n", stress_ops,
           (unsigned long long)f.n_erase, (unsigned long long)f.prog_bytes);

    /* 전수 주입 (4 섹터) */
    build_script(180, 8, 6, 0x1234ABCDu, false);
    xcfg_t c = {512, 4, 64, 24, true};
    static xstat_t x;
    exhaustive(&c, g_ops, g_nops, &x);
    print_stat("4x512", g_nops, &x);
    CHECK(x.bad == 0 && x.cuts1 == x.armed && x.cuts1 > 5000u && x.cuts2 > 5000u);
    CHECK(x.copying > 0 && x.dead > 0 && x.garbage > 0 && x.dirty > 0 && x.infl_old > 0 && x.infl_new > 0);

    /* 전수 주입 (2 섹터 = ST 식 2 페이지 스왑 구성) */
    build_script(120, 3, 3, 0xBEEF0001u, false);
    xcfg_t c2 = {512, 2, 64, 16, true};
    exhaustive(&c2, g_ops, g_nops, &x);
    print_stat("2x512", g_nops, &x);
    CHECK(x.bad == 0 && x.cuts1 == x.armed && x.cuts1 > 2000u);
}

/* ---------- Part C ---------- */
static void part_c(void) {
    if (!g_a_ok) { puts("SKIP Part C: Part A failed"); g_total += 18; return; }
    puts("== Part C: 원자적 배치 + 부트 카운터/캘리브레이션 + 마모");
    static nor_t f;
    static kv_t kv, kv2;
    uint8_t rd[KV_MAX_VAL]; uint16_t l = 0;
    nor_fresh(&f, 512, 4, 64);
    CHECK(kv_mount(&kv, &f) == KV_OK);
    CHECK(kv_batch_begin(&kv) == KV_OK && kv_batch_set(&kv, 1, "gain", 4) == KV_OK && kv_batch_set(&kv, 2, "offs", 4) == KV_OK);
    CHECK(kv_batch_set(&kv, 1, "dup!", 4) == KV_EINVAL);
    CHECK(kv_batch_commit(&kv) == KV_OK);
    CHECK(kv_get(&kv, 1, rd, sizeof rd, &l) == KV_OK && l == 4u && memcmp(rd, "gain", 4) == 0);
    CHECK(kv_get(&kv, 2, rd, sizeof rd, &l) == KV_OK && l == 4u && memcmp(rd, "offs", 4) == 0);
    CHECK(kv_mount(&kv2, &f) == KV_OK && kv_get(&kv2, 2, rd, sizeof rd, &l) == KV_OK && memcmp(rd, "offs", 4) == 0 && kv_check(&kv2));
    /* 배치 안의 삭제 */
    CHECK(kv_batch_begin(&kv2) == KV_OK && kv_batch_del(&kv2, 1) == KV_OK && kv_batch_set(&kv2, 3, "new1", 4) == KV_OK && kv_batch_commit(&kv2) == KV_OK);
    CHECK(kv_get(&kv2, 1, rd, sizeof rd, &l) == KV_ENOENT && kv_get(&kv2, 3, rd, sizeof rd, &l) == KV_OK);
    /* 찢어진 배치: 어느 키도 보이지 않는다 */
    nor_fresh(&f, 512, 4, 64);
    CHECK(kv_mount(&kv, &f) == KV_OK && kv_set(&kv, 1, "old1", 4) == KV_OK && kv_set(&kv, 2, "old2", 4) == KV_OK);
    (void)kv_batch_begin(&kv); (void)kv_batch_set(&kv, 1, "new1", 4); (void)kv_batch_set(&kv, 2, "new2", 4);
    nor_arm(&f, f.op, F_P_PREFIX, 12, 0);
    CHECK(kv_batch_commit(&kv) == KV_EPOWER);
    nor_power_on(&f);
    CHECK(kv_mount(&kv, &f) == KV_OK && kv_get(&kv, 1, rd, sizeof rd, &l) == KV_OK && memcmp(rd, "old1", 4) == 0
          && kv_get(&kv, 2, rd, sizeof rd, &l) == KV_OK && memcmp(rd, "old2", 4) == 0);

    /* 배치 전수 주입 */
    build_script(160, 5, 4, 0x77777777u, true);
    xcfg_t c = {512, 4, 64, 24, true};
    static xstat_t x;
    exhaustive(&c, g_ops, g_nops, &x);
    print_stat("batch 4x512", g_nops, &x);
    CHECK(x.bad == 0 && x.cuts1 == x.armed && x.cuts1 > 5000u);

    /* 장치 시나리오: 무작위 전원 차단 부트 N 회. 부트 카운터(+1 씩), gain+offs = 1000 불변, cold 설정 유지 */
    enum { K_BOOT = 1, K_GAIN = 2, K_OFS = 3, K_NAME = 4, K_LOG = 5 };
    uint32_t nboots = 8000u;
    nor_fresh(&f, 1024, 6, 256);
    uint32_t rng = 0xD00DFEEDu, model_boot = 0, cuts = 0;
    bool pending_boot = false, name_set = false, sim_ok = true;
    uint32_t gain = 500, pending_gain = 500;
    bool pending_cal = false, have_cal = false;
    for (uint32_t b = 0; b < nboots && sim_ok; b++) {
        nor_power_on(&f);
        if (rnd_next(&rng) % 8u) nor_arm(&f, f.op + rnd_next(&rng) % 12u,
                                         (rnd_next(&rng) & 1u) ? F_P_RANDOM : F_P_PREFIX, rnd_next(&rng) % 32u, rnd_next(&rng));
        int rc = kv_mount(&kv, &f);
        if (rc == KV_EPOWER) { cuts++; continue; }
        if (rc != KV_OK || !kv_check(&kv)) { sim_ok = false; break; }
        uint32_t c32 = 0, g32 = 0, o32 = 0;
        uint16_t got;
        rc = kv_get(&kv, K_BOOT, &c32, 4, &got);
        if (rc == KV_ENOENT) c32 = 0;
        else if (rc != KV_OK || got != 4u) { sim_ok = false; break; }
        if (!(c32 == model_boot || (pending_boot && c32 == model_boot + 1u))) { sim_ok = false; break; }
        model_boot = c32; pending_boot = false;
        int rg = kv_get(&kv, K_GAIN, &g32, 4, &got), ro = kv_get(&kv, K_OFS, &o32, 4, &got);
        if (have_cal) {
            if (rg != KV_OK || ro != KV_OK || g32 + o32 != 1000u) { sim_ok = false; break; }
            if (!(g32 == gain || (pending_cal && g32 == pending_gain))) { sim_ok = false; break; }
            gain = g32;
        } else if (rg == KV_OK || ro == KV_OK) {
            if (rg != KV_OK || ro != KV_OK || g32 + o32 != 1000u || !(pending_cal && g32 == pending_gain)) { sim_ok = false; break; }
            gain = g32; have_cal = true;
        }
        pending_cal = false;
        if (name_set && kv_get(&kv, K_NAME, rd, sizeof rd, &got) != KV_OK) { sim_ok = false; break; }
        /* 이번 부트의 쓰기: 카운터, (가끔) 캘리브레이션 배치, 로그 키 */
        uint32_t nc = model_boot + 1u;
        pending_boot = true;
        if ((rc = kv_set(&kv, K_BOOT, &nc, 4)) == KV_EPOWER) { cuts++; continue; }
        if (rc != KV_OK) { sim_ok = false; break; }
        model_boot = nc; pending_boot = false;
        if (!name_set) { if ((rc = kv_set(&kv, K_NAME, "dev-001", 7)) == KV_EPOWER) { cuts++; continue; } name_set = (rc == KV_OK); }
        if (rnd_next(&rng) % 4u == 0u) {
            uint32_t ng = 1u + rnd_next(&rng) % 999u, no = 1000u - ng;
            kv_batch_begin(&kv); kv_batch_set(&kv, K_GAIN, &ng, 4); kv_batch_set(&kv, K_OFS, &no, 4);
            pending_cal = true; pending_gain = ng;
            rc = kv_batch_commit(&kv);
            if (rc == KV_EPOWER) { cuts++; continue; }
            if (rc != KV_OK) { sim_ok = false; break; }
            gain = ng; have_cal = true; pending_cal = false;
        }
        uint8_t lg[48]; memset(lg, (uint8_t)b, sizeof lg);
        rc = kv_set(&kv, K_LOG, lg, (uint16_t)(1u + rnd_next(&rng) % 48u));
        if (rc == KV_EPOWER) { cuts++; continue; }
        if (rc != KV_OK) { sim_ok = false; break; }
        f.cut_at = -1;
    }
    printf("  device: boots=%u cuts=%u final boot counter=%u erases=[", nboots, cuts, model_boot);
    for (uint32_t s = 0; s < f.nsec; s++) printf("%u%s", f.erase_cnt[s], s + 1 < f.nsec ? " " : "]\n");
    CHECK(sim_ok);
    CHECK(model_boot > 100u);

    /* 마모: 뜨거운 키 1 개 + cold 키들 → 링 회전이 cold 데이터도 옮겨서 소거 횟수 차이 ≤ 1 */
    nor_fresh(&f, 1024, 8, 256);
    CHECK(kv_mount(&kv, &f) == KV_OK);
    uint8_t cv[32]; memset(cv, 0x7C, sizeof cv);
    for (uint16_t k = 10; k < 20; k++) (void)kv_set(&kv, k, cv, 32);
    idx_t *c0 = idx_find(&kv, 10);
    uint32_t cold_sec_before = c0 ? c0->sec : 99u;
    uint8_t hv[24];
    for (uint32_t i = 0; i < 60000u; i++) { hv[0] = (uint8_t)i; hv[1] = (uint8_t)(i >> 8); (void)kv_set(&kv, 1, hv, 24); }
    uint32_t emin = UINT32_MAX, emax = 0;
    for (uint32_t s = 0; s < f.nsec; s++) { if (f.erase_cnt[s] < emin) emin = f.erase_cnt[s]; if (f.erase_cnt[s] > emax) emax = f.erase_cnt[s]; }
    printf("  wear: 60000 hot sets, erases min=%u max=%u, prog_bytes=%llu (hot+cold user bytes=%u)\n", emin, emax,
           (unsigned long long)f.prog_bytes, 60000u * 24u + 10u * 32u);
    c0 = idx_find(&kv, 10);
    CHECK(emax - emin <= 1u && emin > 100u);
    CHECK(c0 && c0->sec != cold_sec_before && kv_check(&kv));
}

int main(void) {
    crc_init();
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
