/* BM-B06 추가 연습 — 한 번에 검증 (본문 solution.c 의 파서·섹터 맵을 복사해 자급자족)
 *   연습 1  erase_plan()       : 쓰기 범위 → 지울 섹터 범위, 영역 밖을 지우면 거부
 *   연습 2  images_overlap()   : 따로 링크된 boot/app 의 LOAD 범위가 플래시에서 겹치나 + 합친 .bin 크기
 *   연습 3  pick_boot_slot()   : 헤더의 load_addr 로 "이 슬롯에서 실행 가능한가" + 버전 비교 (direct-XIP)
 *   연습 4  vtor_align()       : 벡터 수 → VTOR 정렬 → 헤더 예약 0x200 이 충분한가
 * 레슨 디렉터리에서:  cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-B06_pr && /tmp/BM-B06_pr [dir]
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 데이터 모델 ---------------- */
#define SF_CONTENTS 0x01u
#define SF_ALLOC    0x02u
#define SF_LOAD     0x04u
#define SF_READONLY 0x08u
#define SF_CODE     0x10u
typedef struct { char name[24]; uint32_t size, vma, lma; unsigned flags; } sect_t;
typedef struct { char name[16]; uint32_t origin, length; } region_t;
#define MAX_REGIONS 16
typedef struct { region_t r[MAX_REGIONS]; int n; } layout_t;
#define MAX_SECT 32
typedef struct { sect_t s[MAX_SECT]; int n; uint32_t hdr_addr; int hdr_len; uint8_t hdr[64]; } image_t;

/* STM32F407 류 1 MB 플래시 섹터 맵 (RM0090 기준 대표값 — RM 확인 권장) */
typedef struct { uint32_t base; int nsect; uint32_t sect_size[16]; } flash_geom_t;
#define KB 1024u
static const flash_geom_t F4_1M = { 0x08000000u, 12,
    { 16*KB, 16*KB, 16*KB, 16*KB, 64*KB, 128*KB, 128*KB, 128*KB, 128*KB, 128*KB, 128*KB, 128*KB } };

/* 이미지 헤더 계약 (shared.h 와 같은 값) */
#define IMG_MAGIC    0x31474D49u
#define IMG_HDR_SIZE 0x200u

/* ---------------- 작은 도구들 (주어짐) ---------------- */
static const char *next_line(const char *p, char *line, size_t cap)   /* 한 줄 복사, 다음 줄 시작 반환 */
{
    if (!p || !*p) return NULL;
    size_t i = 0;
    while (p[i] && p[i] != '\n') { if (i + 1 < cap) line[i] = p[i]; i++; }
    line[i < cap ? i : cap - 1] = '\0';
    return p[i] ? p + i + 1 : p + i;
}
static const sect_t *find_sect(const image_t *img, const char *name)
{
    for (int i = 0; i < img->n; i++) if (strcmp(img->s[i].name, name) == 0) return &img->s[i];
    return NULL;
}
static const region_t *find_region(const layout_t *L, const char *name)
{
    for (int i = 0; i < L->n; i++) if (strcmp(L->r[i].name, name) == 0) return &L->r[i];
    return NULL;
}
static bool range_in(const region_t *r, uint32_t a, uint32_t size)        /* [a, a+size) ⊂ r ? */
{
    return r && (uint64_t)a >= r->origin && (uint64_t)a + size <= (uint64_t)r->origin + r->length;
}
static uint32_t rd32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

static int hexval(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
/* objdump -s 의 hex dump 에서 섹션 바이트를 꺼낸다 (주어짐). 반환 = 바이트 수, 없으면 0 */
static int parse_hexdump(const char *text, const char *sect, uint32_t *addr, uint8_t *buf, int cap)
{
    char want[48], line[160];
    snprintf(want, sizeof want, "Contents of section %s:", sect);
    const char *p = text;
    while ((p = next_line(p, line, sizeof line)) != NULL && strcmp(line, want) != 0) { }
    if (!p) return 0;
    int n = 0;
    bool first = true;
    while ((p = next_line(p, line, sizeof line)) != NULL) {
        unsigned a; int used;
        if (line[0] != ' ' || sscanf(line, " %x%n", &a, &used) != 1) break;
        if (first) { *addr = a; first = false; }
        const char *q = line + used;
        for (int g = 0; g < 4 && q[0] == ' ' && q[1] != ' ' && q[1] != '\0'; g++) {   /* 두 칸 공백 = ASCII 열 */
            q++;
            while (hexval(q[0]) >= 0 && hexval(q[1]) >= 0) {                  /* 2 글자 = 1 바이트, 메모리 순서 */
                if (n < cap) buf[n] = (uint8_t)(hexval(q[0]) << 4 | hexval(q[1]));
                n++; q += 2;
            }
        }
    }
    return n <= cap ? n : cap;
}


/* objdump -h: "  4 .data   00000004  20000100  08020388  00020100  2**2" + 다음 줄 플래그.
 * 반환 = 섹션 수, cap 초과면 -1. 섹션 줄이 아닌 줄은 건너뛴다. */
static int parse_sections(const char *text, sect_t *out, int cap)
{
    char line[160], flags[160];
    int n = 0;
    const char *p = text;
    while ((p = next_line(p, line, sizeof line)) != NULL) {
        int idx, align; unsigned size, vma, lma, off; char name[24];
        if (sscanf(line, " %d %23s %x %x %x %x 2**%d", &idx, name, &size, &vma, &lma, &off, &align) != 7) continue;
        if (n >= cap) return -1;
        const char *q = next_line(p, flags, sizeof flags);
        if (q) p = q; else flags[0] = '\0';
        sect_t *s = &out[n++];
        snprintf(s->name, sizeof s->name, "%s", name);
        s->size = size; s->vma = vma; s->lma = lma;
        s->flags = (strstr(flags, "CONTENTS") ? SF_CONTENTS : 0u) | (strstr(flags, "ALLOC") ? SF_ALLOC : 0u) |
                   (strstr(flags, "LOAD") ? SF_LOAD : 0u) | (strstr(flags, "READONLY") ? SF_READONLY : 0u) |
                   (strstr(flags, "CODE") ? SF_CODE : 0u);
    }
    return n;
}

/* map 파일 "Memory Configuration": "SLOT_A  0x08020000  0x00060000  xr". *default* 는 뺀다.
 * 반환 = 영역 수, MAX_REGIONS 초과면 -1. */
static int parse_memcfg(const char *text, layout_t *L)
{
    char line[160];
    const char *p = text;
    L->n = 0;
    while ((p = next_line(p, line, sizeof line)) != NULL && strncmp(line, "Memory Configuration", 20) != 0) { }
    while (p && (p = next_line(p, line, sizeof line)) != NULL) {
        char name[16]; unsigned org, len;
        if (sscanf(line, "%15s 0x%x 0x%x", name, &org, &len) != 3) continue;
        if (strcmp(name, "*default*") == 0) continue;
        if (L->n >= MAX_REGIONS) return -1;
        region_t *r = &L->r[L->n++];
        snprintf(r->name, sizeof r->name, "%s", name);
        r->origin = org; r->length = len;
    }
    return L->n;
}


/* addr 가 속한 섹터 번호 (0..nsect-1), 플래시 밖이면 -1. start 가 NULL 이 아니면 섹터 시작 주소 */
static int sector_index(const flash_geom_t *g, uint32_t addr, uint32_t *start)
{
    uint32_t s = g->base;
    for (int i = 0; i < g->nsect; i++) {
        if (addr >= s && addr - s < g->sect_size[i]) { if (start) *start = s; return i; }
        s += g->sect_size[i];
    }
    return -1;
}

static uint32_t flash_end(const flash_geom_t *g)
{
    uint32_t e = g->base;
    for (int i = 0; i < g->nsect; i++) e += g->sect_size[i];
    return e;
}


/* ======================= 연습 1 — erase 계획 ======================= */
/* [addr, addr+len) 를 쓰려면 지워야 하는 섹터 first..last. len 0 / 플래시 밖 → -1, 아니면 섹터 수.
 * inside 가 NULL 이 아니면 지울 범위 전체가 그 영역 안이어야 한다 (남의 영역을 지우는 사고 방지) → 아니면 -2 */
static int erase_plan(const flash_geom_t *g, uint32_t addr, uint32_t len, const region_t *inside, int *first, int *last)
{
    uint32_t s0 = 0, s1 = 0;
    if (len == 0) return -1;
    int a = sector_index(g, addr, &s0), b = sector_index(g, addr + len - 1u, &s1);
    if (a < 0 || b < 0 || (uint64_t)addr + len > flash_end(g)) return -1;
    if (inside && !range_in(inside, s0, s1 + g->sect_size[b] - s0)) return -2;   /* 지울 섹터 전체가 영역 안? */
    *first = a; *last = b;
    return b - a + 1;
}

/* ======================= 연습 2 — 두 이미지 합치기 ======================= */
static void load_extent(const image_t *img, uint32_t *lo, uint32_t *hi)   /* LOAD 섹션 LMA 의 [lo, hi) */
{
    *lo = 0xFFFFFFFFu; *hi = 0;
    for (int i = 0; i < img->n; i++) {
        const sect_t *s = &img->s[i];
        if ((s->flags & (SF_ALLOC | SF_LOAD)) != (SF_ALLOC | SF_LOAD) || s->size == 0) continue;
        if (s->lma < *lo) *lo = s->lma;
        if (s->lma + s->size > *hi) *hi = s->lma + s->size;
    }
}
/* 섹션 단위로 비교 (범위 [lo,hi) 끼리 비교하면 boot 가 app 사이 빈틈에 끼어도 못 잡거나 헛경보) */
static bool images_overlap(const image_t *a, const image_t *b)
{
    for (int i = 0; i < a->n; i++) {
        const sect_t *x = &a->s[i];
        if ((x->flags & (SF_ALLOC | SF_LOAD)) != (SF_ALLOC | SF_LOAD) || x->size == 0) continue;
        for (int j = 0; j < b->n; j++) {
            const sect_t *y = &b->s[j];
            if ((y->flags & (SF_ALLOC | SF_LOAD)) != (SF_ALLOC | SF_LOAD) || y->size == 0) continue;
            if ((uint64_t)x->lma < (uint64_t)y->lma + y->size && (uint64_t)y->lma < (uint64_t)x->lma + x->size) return true;
        }
    }
    return false;
}
/* objcopy -O binary 로 합친 파일 크기 = 가장 낮은 LMA ~ 가장 높은 LMA 끝 (빈틈은 0 으로 채워짐) */
static uint32_t merged_bin_size(const image_t *a, const image_t *b)
{
    uint32_t alo, ahi, blo, bhi;
    load_extent(a, &alo, &ahi); load_extent(b, &blo, &bhi);
    return (ahi > bhi ? ahi : bhi) - (alo < blo ? alo : blo);
}

/* ======================= 연습 3 — 어느 슬롯으로 부팅? ======================= */
typedef struct { uint32_t magic, load_addr, vector_addr, img_size, fw_version; uint16_t hdr_size; } hdr_view_t;
static hdr_view_t hdr_decode(const uint8_t *b)
{
    hdr_view_t h = { rd32(b), rd32(b + 8), rd32(b + 12), rd32(b + 16), rd32(b + 20), (uint16_t)(b[4] | b[5] << 8) };
    return h;
}
/* 슬롯에 놓인 헤더가 "그 자리에서 실행 가능" 한가: magic, 링크 주소 == 놓인 주소, 벡터 = +hdr_size, 크기 */
static bool runnable_at(const hdr_view_t *h, uint32_t slot_origin, uint32_t slot_len)
{
    return h->magic == IMG_MAGIC && h->hdr_size == IMG_HDR_SIZE && h->load_addr == slot_origin &&
           h->vector_addr == slot_origin + h->hdr_size && h->img_size > h->hdr_size && h->img_size <= slot_len;
}
/* direct-XIP: 둘 다 실행 가능하면 버전 높은 쪽, 하나만이면 그쪽, 없으면 -1 (복구 모드) */
static int pick_boot_slot(const hdr_view_t *a, const hdr_view_t *b, uint32_t a_org, uint32_t b_org, uint32_t len)
{
    bool ra = runnable_at(a, a_org, len), rb = runnable_at(b, b_org, len);
    if (ra && rb) return b->fw_version > a->fw_version ? 1 : 0;
    return ra ? 0 : rb ? 1 : -1;
}

/* ======================= 연습 4 — VTOR 정렬 ======================= */
/* 벡터 수(16 시스템 + n_irq) x 4 B 를 2 의 거듭제곱으로 올림, 최소 128 B (ARMv7-M VTOR 규칙 — ARM ARM 확인 권장) */
static uint32_t vtor_align(unsigned n_irq)
{
    uint32_t bytes = (16u + n_irq) * 4u, a = 128u;
    while (a < bytes) a <<= 1;
    return a;
}
/* 헤더 예약이 벡터 테이블 정렬을 만족하나: 슬롯 시작은 섹터 경계(충분히 정렬)라고 가정 → hdr_size % align == 0 */
static bool hdr_reservation_ok(uint32_t hdr_size, unsigned n_irq) { return hdr_size % vtor_align(n_irq) == 0; }

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static char g_buf[4][8192];
static const char *load(const char *dir, const char *name, int slot)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (!f) { printf("cannot open %s — 레슨 디렉터리에서 실행하거나 경로를 인자로 주세요\n", path); g_buf[slot][0] = '\0'; return g_buf[slot]; }
    size_t n = fread(g_buf[slot], 1, sizeof g_buf[slot] - 1, f);
    fclose(f);
    g_buf[slot][n] = '\0';
    return g_buf[slot];
}
static void load_image(const char *text, image_t *img)
{
    memset(img, 0, sizeof *img);
    img->n = parse_sections(text, img->s, MAX_SECT);
    if (img->n < 0) img->n = 0;
    img->hdr_len = parse_hexdump(text, ".img_hdr", &img->hdr_addr, img->hdr, (int)sizeof img->hdr);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    layout_t L;
    image_t boot, app;
    CHECK(parse_memcfg(load(dir, "cap_memcfg.txt", 0), &L) == 8);
    load_image(load(dir, "cap_boot.txt", 1), &boot);
    load_image(load(dir, "cap_app.txt", 2), &app);
    const region_t *A = find_region(&L, "SLOT_A"), *B = find_region(&L, "SLOT_B"), *BOOT = find_region(&L, "BOOT");
    const region_t *META = find_region(&L, "META");
    CHECK(A && B && BOOT && META);
    if (!A || !B || !BOOT || !META) { printf("%d/%d passed\n", g_pass, g_total); return 1; }

    /* 연습 1 */
    int f = -1, l = -1;
    CHECK(erase_plan(&F4_1M, B->origin, B->length, B, &f, &l) == 3 && f == 8 && l == 10);       /* 슬롯 B 통째 */
    CHECK(erase_plan(&F4_1M, B->origin, 100u * KB, B, &f, &l) == 1 && f == 8 && l == 8);        /* 100 KB 이미지 */
    CHECK(erase_plan(&F4_1M, B->origin, 128u * KB + 1u, B, &f, &l) == 2 && l == 9);             /* 1 바이트 넘침 → 섹터 하나 더 */
    CHECK(erase_plan(&F4_1M, META->origin + 16u * KB, 64u, META, &f, &l) == 1 && f == 3);       /* 메타 사본 2 */
    CHECK(erase_plan(&F4_1M, 0x0800FF00u, 0x200u, META, &f, &l) == -2);                          /* 섹터 4(NVS)까지 지움 */
    CHECK(erase_plan(&F4_1M, 0x0800FF00u, 0x200u, NULL, &f, &l) == 2 && f == 3 && l == 4);
    CHECK(erase_plan(&F4_1M, BOOT->origin, 40u * KB, BOOT, &f, &l) == -2);                       /* 부트로더 40 KB → 섹터 2 침범 */
    CHECK(erase_plan(&F4_1M, 0x080FFFF0u, 0x20u, NULL, &f, &l) == -1 && erase_plan(&F4_1M, A->origin, 0u, NULL, &f, &l) == -1);

    /* 연습 2 */
    uint32_t lo, hi;
    load_extent(&boot, &lo, &hi);
    CHECK(lo == 0x08000000u && hi == 0x08000128u);                                               /* __boot_end 와 같다 */
    load_extent(&app, &lo, &hi);
    CHECK(lo == 0x08020000u && hi == 0x0802038Cu);                                               /* __app_image_end */
    CHECK(!images_overlap(&boot, &app) && find_sect(&app, ".img_hdr") && find_sect(&app, ".img_hdr")->lma == A->origin);
    CHECK(merged_bin_size(&boot, &app) == 0x2038Cu);                                             /* 131,980 B, 대부분 빈틈 */
    image_t bad = app;
    for (int i = 0; i < bad.n; i++) if (strcmp(bad.s[i].name, ".rodata") == 0) bad.s[i].lma = 0x08000100u;
    CHECK(images_overlap(&boot, &bad));                                                          /* app 조각이 BOOT 로 */
    bad = boot;
    for (int i = 0; i < bad.n; i++) if (strcmp(bad.s[i].name, ".text") == 0) bad.s[i].lma = 0x08020210u;
    CHECK(images_overlap(&bad, &app));                                                           /* app 의 빈틈(0x20~0x200)이 아니라 벡터와 겹침 */
    bad = boot;
    for (int i = 0; i < bad.n; i++) if (strcmp(bad.s[i].name, ".text") == 0) bad.s[i].lma = 0x08020040u;
    CHECK(!images_overlap(&bad, &app));                                                          /* 헤더 뒤 예약 빈틈에 쏙 — 겹침 아님 */

    /* 연습 3 — 실제 캡처 헤더 + 바꾼 사본 */
    CHECK(app.hdr_len == 32);
    hdr_view_t ha = hdr_decode(app.hdr), hb = ha;
    CHECK(ha.fw_version == 0x01040002u && ha.img_size == 0x38Cu && ha.load_addr == A->origin);
    CHECK(runnable_at(&ha, A->origin, A->length) && !runnable_at(&ha, B->origin, B->length));    /* A 용 이미지를 B 에 복사하면 못 돈다 */
    hb.load_addr = B->origin; hb.vector_addr = B->origin + 0x200u; hb.fw_version = 0x01050000u; /* B 용으로 링크한 1.5.0 */
    CHECK(pick_boot_slot(&ha, &hb, A->origin, B->origin, A->length) == 1);
    hb.fw_version = 0x01030000u;
    CHECK(pick_boot_slot(&ha, &hb, A->origin, B->origin, A->length) == 0);                       /* 롤백 방지는 BM-A04 */
    hb.load_addr = A->origin;                                                                     /* B 에 A 용 이미지 */
    CHECK(pick_boot_slot(&ha, &hb, A->origin, B->origin, A->length) == 0);
    hdr_view_t blank = hdr_decode((const uint8_t *)"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff");
    CHECK(pick_boot_slot(&blank, &blank, A->origin, B->origin, A->length) == -1);                /* 지워진 플래시 = 0xFF */
    hb = ha; hb.img_size = A->length + 4u;
    CHECK(!runnable_at(&hb, A->origin, A->length));

    /* 연습 4 */
    CHECK(vtor_align(0) == 128u && vtor_align(16) == 128u && vtor_align(17) == 256u);
    CHECK(vtor_align(82) == 512u && hdr_reservation_ok(0x200u, 82));                            /* F405/407 (RM0090, 확인 권장) */
    CHECK(vtor_align(150) == 1024u && !hdr_reservation_ok(0x200u, 150) && hdr_reservation_ok(0x400u, 150)); /* H7 급 */
    CHECK(vtor_align(240) == 1024u && vtor_align(241) == 2048u);

    printf("merged boot+app .bin = %u B (0x%X), boot LOAD 0x%08X..0x%08X\n", merged_bin_size(&boot, &app),
           merged_bin_size(&boot, &app), 0x08000000u, 0x08000128u);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
