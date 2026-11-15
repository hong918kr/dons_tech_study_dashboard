/* BM-B06 부트로더+앱 플래시 분할 레이아웃 설계 — 빌드 후(post-link) 레이아웃 검사기 (Mac 호스트)
 *
 * 입력 = Lima VM 에서 run_vm.sh capture 로 뜬 실제 출력 (이 디렉터리의 cap_*.txt):
 *   cap_memcfg.txt   app.map 의 "Memory Configuration" (= layout.ld 의 MEMORY)
 *   cap_boot.txt     arm-none-eabi-objdump -h boot.elf
 *   cap_app.txt      objdump -h app.elf + objdump -s -j .img_hdr app.elf
 *   cap_bad_*.txt    ASSERT 를 뺀 채 일부러 망가뜨린 빌드 (겹친 슬롯 / orphan .noinit / 헤더가 맨 앞이 아님)
 * 빌드·실행은 레슨 디렉터리에서:  cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-B06_so && /tmp/BM-B06_so [dir]
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
#define VTOR_ALIGN   512u          /* F4 벡터 98 개 x 4 B → 2 의 거듭제곱으로 올림 (확인 권장) */

/* layout_check() 결과 비트 */
#define L_OVERLAP   0x01u   /* 두 영역이 겹침 */
#define L_ALIGN     0x02u   /* 플래시 영역의 시작/끝이 섹터 경계가 아님 */
#define L_OUTSIDE   0x04u   /* 플래시에서 시작했는데 플래시 끝을 넘음 */
#define L_SLOT_SIZE 0x08u   /* SLOT_A != SLOT_B 크기 */
#define L_EMPTY     0x10u   /* 길이 0 영역 */
#define L_MISSING   0x20u   /* SLOT_A / SLOT_B / SHARED / RAM 중 없음 */
/* image_check() 결과 비트 */
#define I_REGION        0x001u  /* ALLOC 섹션이 허용 영역 밖 */
#define I_HDR_SECT      0x002u  /* .img_hdr 섹션이 슬롯 시작에 없음 */
#define I_HDR_MAGIC     0x004u  /* 슬롯 시작 4 바이트가 magic 이 아님 */
#define I_HDR_FIELDS    0x008u  /* hdr_size / load_addr / vector_addr 불일치 */
#define I_VECTOR        0x010u  /* .isr_vector 위치·정렬 */
#define I_IMG_SIZE      0x020u  /* 헤더 img_size != 실제 마지막 LOAD 바이트 */
#define I_NOINIT_MISSING 0x040u /* .noinit 섹션이 없음 (orphan 등) */
#define I_NOINIT_LOAD   0x080u  /* .noinit 이 LOAD (startup 이 덮어씀 + 플래시 낭비) */
#define I_NOINIT_ADDR   0x100u  /* .noinit 이 SHARED 시작에 없음 */

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

/* ======================= Part A — 캡처 파싱 ======================= */

/* objdump -h: "  4 .data   00000004  20000100  08020388  00020100  2**2" + 다음 줄 플래그.
 * 반환 = 섹션 수, cap 초과면 -1. 섹션 줄이 아닌 줄은 건너뛴다. */
static int parse_sections(const char *text, sect_t *out, int cap)
{
    /* TODO: 줄마다 sscanf(" %d %23s %x %x %x %x 2**%d") 가 7 개를 읽으면 섹션 줄.
     *       바로 다음 줄의 CONTENTS/ALLOC/LOAD/READONLY/CODE 를 SF_* 로. cap 을 넘으면 -1. */
    (void)text; (void)out; (void)cap;
    return 0;
}

/* map 파일 "Memory Configuration": "SLOT_A  0x08020000  0x00060000  xr". *default* 는 뺀다.
 * 반환 = 영역 수, MAX_REGIONS 초과면 -1. */
static int parse_memcfg(const char *text, layout_t *L)
{
    /* TODO: "Memory Configuration" 줄 이후에서 "%15s 0x%x 0x%x" 3 개를 읽는 줄 = 영역. *default* 는 제외. */
    (void)text;
    L->n = 0;
    return 0;
}

/* ======================= Part B — 레이아웃 검사 ======================= */

/* addr 가 속한 섹터 번호 (0..nsect-1), 플래시 밖이면 -1. start 가 NULL 이 아니면 섹터 시작 주소 */
static int sector_index(const flash_geom_t *g, uint32_t addr, uint32_t *start)
{
    /* TODO: base 부터 sect_size[] 를 누적하며 addr 가 들어가는 섹터를 찾는다 (밖이면 -1) */
    (void)g; (void)addr;
    if (start) *start = 0;
    return -1;
}

static uint32_t flash_end(const flash_geom_t *g)
{
    uint32_t e = g->base;
    for (int i = 0; i < g->nsect; i++) e += g->sect_size[i];
    return e;
}

static bool on_boundary(const flash_geom_t *g, uint32_t addr)          /* 섹터 시작이거나 플래시 끝 */
{
    uint32_t st;
    if (addr == flash_end(g)) return true;
    return sector_index(g, addr, &st) >= 0 && st == addr;
}

static unsigned layout_check(const layout_t *L, const flash_geom_t *g)
{
    /* TODO: 길이 0 → L_EMPTY, 모든 쌍 겹침 → L_OVERLAP (64 비트로 끝 주소 계산),
     *       플래시에서 시작한 영역: 끝이 플래시 밖 → L_OUTSIDE, 시작/끝이 섹터 경계 아님 → L_ALIGN (on_boundary),
     *       SLOT_A/SLOT_B/SHARED/RAM 없음 → L_MISSING, 슬롯 크기 다름 → L_SLOT_SIZE */
    (void)L; (void)g;
    return 0;
}

/* ======================= Part C — 이미지 검사 ======================= */

/* home = 이 이미지가 링크된 플래시 영역 ("BOOT" 또는 "SLOT_A"/"SLOT_B"). 슬롯이면 헤더가 있어야 한다. */
static unsigned image_check(const image_t *img, const layout_t *L, const char *home)
{
    /* TODO: (1) ALLOC·크기>0 섹션 (.noinit 제외): LOAD 면 LMA ⊂ home, VMA ⊂ home 또는 RAM; NOBITS 면 VMA ⊂ RAM
     *       (2) .noinit: 없음/0 → I_NOINIT_MISSING, LOAD → I_NOINIT_LOAD, SHARED 시작 아님 → I_NOINIT_ADDR
     *       (3) .isr_vector == home 시작 (+0x200 if slot), 512 정렬 → 아니면 I_VECTOR
     *       (4) slot 이면 헤더: 섹션 위치 → magic → hdr_size/load_addr/vector_addr → img_size (마지막 LOAD LMA 끝 - 시작) */
    (void)img; (void)L; (void)home; (void)range_in;
    return 0;
}

/* 두 이미지의 공유 .noinit 이 같은 주소·같은 크기·둘 다 NOLOAD 인가 */
static bool shared_consistent(const image_t *a, const image_t *b)
{
    /* TODO: 두 이미지 모두 .noinit 이 있고, 같은 VMA·같은 크기·둘 다 LOAD 아님 */
    (void)a; (void)b;
    return false;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static char g_buf[8][8192];
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
static sect_t *sect_mut(image_t *img, const char *name)
{
    for (int i = 0; i < img->n; i++) if (strcmp(img->s[i].name, name) == 0) return &img->s[i];
    static sect_t dummy;
    return &dummy;
}
static region_t *region_mut(layout_t *L, const char *name)
{
    for (int i = 0; i < L->n; i++) if (strcmp(L->r[i].name, name) == 0) return &L->r[i];
    static region_t dummy;
    return &dummy;
}
static uint32_t xs = 0x2545F491u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

static void print_layout(const layout_t *L)
{
    printf("%-8s %-10s %-10s %8s  %s\n", "region", "origin", "end", "size", "sectors");
    for (int i = 0; i < L->n; i++) {
        const region_t *r = &L->r[i];
        int s0 = sector_index(&F4_1M, r->origin, NULL), s1 = sector_index(&F4_1M, r->origin + r->length - 1u, NULL);
        if (r->length % KB == 0) printf("%-8s 0x%08x 0x%08x %6u KB  ", r->name, r->origin, r->origin + r->length, r->length / KB);
        else                     printf("%-8s 0x%08x 0x%08x %6u B   ", r->name, r->origin, r->origin + r->length, r->length);
        if (s0 >= 0 && s1 >= 0) printf("%d-%d\n", s0, s1); else printf("-\n");
    }
}
static void print_usage(const char *tag, const image_t *img, const layout_t *L, const char *home)
{
    const region_t *h = find_region(L, home);
    uint32_t fl = 0, ram = 0, sh = 0;
    for (int i = 0; i < img->n; i++) {
        const sect_t *s = &img->s[i];
        if (!(s->flags & SF_ALLOC) || s->size == 0) continue;
        if ((s->flags & SF_LOAD) && h && s->lma + s->size - h->origin > fl) fl = s->lma + s->size - h->origin;
        if (s->vma >= 0x20000000u) { if (strcmp(s->name, ".noinit") == 0) sh += s->size; else ram += s->size; }
    }
    printf("%-5s flash %4u B / %u KB (%s)   RAM %u B   SHARED %u B\n", tag, fl, h ? h->length / KB : 0u, home, ram, sh);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    layout_t L, Lbad;
    image_t boot, app, orphan, badhdr;
    sect_t tmp[MAX_SECT];

    /* ---------- Part A: 파싱 ---------- */
    load_image(load(dir, "cap_app.txt", 0), &app);
    load_image(load(dir, "cap_boot.txt", 1), &boot);
    load_image(load(dir, "cap_bad_orphan.txt", 2), &orphan);
    load_image(load(dir, "cap_bad_hdr.txt", 3), &badhdr);
    CHECK(app.n == 17 && boot.n == 16);
    const sect_t *s = find_sect(&app, ".img_hdr");
    CHECK(s && s->size == 0x20u && s->vma == 0x08020000u && s->lma == 0x08020000u &&
          s->flags == (SF_CONTENTS | SF_ALLOC | SF_LOAD | SF_READONLY));
    s = find_sect(&app, ".data");
    CHECK(s && s->size == 4u && s->vma == 0x20000100u && s->lma == 0x08020388u && (s->flags & SF_LOAD));
    s = find_sect(&app, ".bss");
    CHECK(s && s->vma == 0x20000104u && s->flags == SF_ALLOC);
    s = find_sect(&app, ".noinit");
    CHECK(s && s->vma == 0x20000000u && s->size == 0x20u && s->flags == SF_ALLOC);
    s = find_sect(&app, ".text");
    CHECK(s && s->size == 0xc8u && (s->flags & SF_CODE));
    s = find_sect(&app, ".debug_info");
    CHECK(s && !(s->flags & SF_ALLOC));
    s = find_sect(&boot, ".isr_vector");
    CHECK(s && s->vma == 0x08000000u && s->size == 0x40u);
    CHECK(parse_sections(load(dir, "cap_app.txt", 4), tmp, 3) == -1);                 /* 용량 초과 보고 */
    CHECK(parse_sections("", tmp, MAX_SECT) == 0 && parse_sections("garbage\n  1 two\n", tmp, MAX_SECT) == 0);
    CHECK(find_sect(&orphan, ".noinit_shared") != NULL && find_sect(&orphan, ".noinit") == NULL);
    CHECK(app.hdr_addr == 0x08020000u && app.hdr_len == 32 && rd32(app.hdr) == IMG_MAGIC);
    CHECK(badhdr.hdr_len == 36 && rd32(badhdr.hdr) == 0x7A67u && rd32(badhdr.hdr + 4) == IMG_MAGIC);

    CHECK(parse_memcfg(load(dir, "cap_memcfg.txt", 5), &L) == 8);
    CHECK(L.n == 8 && strcmp(L.r[3].name, "SLOT_A") == 0 && L.r[3].origin == 0x08020000u && L.r[3].length == 0x60000u);
    CHECK(find_region(&L, "RAM") && find_region(&L, "RAM")->origin == 0x20000100u &&
          find_region(&L, "RAM")->length == 0x1ff00u && find_region(&L, "*default*") == NULL);
    CHECK(parse_memcfg(load(dir, "cap_bad_overlap_memcfg.txt", 6), &Lbad) == 8 &&
          find_region(&Lbad, "SLOT_B")->origin == 0x08060000u);
    CHECK(parse_memcfg("BOOT 0x08000000 0x00008000 xr\n", &Lbad) == 0);              /* 헤더 줄 전에는 무시 */

    /* ---------- Part B: 섹터 맵 + 레이아웃 ---------- */
    {
        static const uint32_t starts[12] = { 0x08000000u, 0x08004000u, 0x08008000u, 0x0800C000u, 0x08010000u,
            0x08020000u, 0x08040000u, 0x08060000u, 0x08080000u, 0x080A0000u, 0x080C0000u, 0x080E0000u };
        int ok = 0;
        for (int i = 0; i < 12; i++) {
            uint32_t st = 0;
            ok += sector_index(&F4_1M, starts[i], &st) == i && st == starts[i];
            ok += sector_index(&F4_1M, starts[i] - 1u, NULL) == i - 1;            /* 직전 바이트 = 이전 섹터 */
        }
        CHECK(ok == 24);
        CHECK(sector_index(&F4_1M, 0x080FFFFFu, NULL) == 11 && sector_index(&F4_1M, 0x08100000u, NULL) == -1);
        int bad = 0;                                                               /* 1 KB 간격 전수 vs 누적합 */
        for (uint32_t a = 0x08000000u; a < 0x08100000u; a += 1024u) {
            int want = 0; uint32_t acc = F4_1M.base;
            while (a >= acc + F4_1M.sect_size[want]) acc += F4_1M.sect_size[want++];
            uint32_t st = 0;
            bad += sector_index(&F4_1M, a + 17u, &st) != want || st != acc;
        }
        CHECK(bad == 0);
    }
    CHECK(layout_check(&L, &F4_1M) == 0);
    parse_memcfg(load(dir, "cap_bad_overlap_memcfg.txt", 6), &Lbad);
    CHECK(layout_check(&Lbad, &F4_1M) == L_OVERLAP);                               /* 실제 캡처: SLOT_B 0x08060000 */
    Lbad = L; region_mut(&Lbad, "BOOT")->length = 24 * KB;
    CHECK(layout_check(&Lbad, &F4_1M) == L_ALIGN);                                 /* 섹터 1 중간에서 끝남 */
    Lbad = L; region_mut(&Lbad, "META")->origin = 0x08009000u;
    CHECK(layout_check(&Lbad, &F4_1M) == (L_ALIGN | L_OVERLAP));                   /* NVS 와도 겹침 */
    Lbad = L; region_mut(&Lbad, "SCRATCH")->length = 256 * KB;
    CHECK(layout_check(&Lbad, &F4_1M) == L_OUTSIDE);
    Lbad = L; region_mut(&Lbad, "SLOT_B")->length = 256 * KB;
    CHECK(layout_check(&Lbad, &F4_1M) == L_SLOT_SIZE);
    Lbad = L; region_mut(&Lbad, "META")->length = 0;
    CHECK(layout_check(&Lbad, &F4_1M) == L_EMPTY);
    Lbad = L; snprintf(region_mut(&Lbad, "SLOT_B")->name, 16, "SLOT_2");
    CHECK(layout_check(&Lbad, &F4_1M) == L_MISSING);
    Lbad = L; region_mut(&Lbad, "SHARED")->origin = 0x200000F0u;
    CHECK(layout_check(&Lbad, &F4_1M) == L_OVERLAP);                               /* RAM 쪽 겹침, 섹터 검사 없음 */
    {   /* 성질 테스트: 무작위 레이아웃 400 개, 겹침·정렬 판정을 4 KB 단위 칠하기(brute force)와 비교 */
        int mism = 0;
        for (int t = 0; t < 400; t++) {
            layout_t R = L;                                                        /* 이름·RAM/SHARED 는 그대로 */
            uint8_t cover[256]; memset(cover, 0, sizeof cover);                    /* 1 MB / 4 KB = 256 칸 */
            bool want_ov = false, want_al = false;
            for (int k = 0; k < 6; k++) {                                          /* 플래시 6 영역을 무작위로 */
                uint32_t u0 = xorshift32() % 256u, len = 1u + xorshift32() % 40u;
                if (xorshift32() % 3u) {                                           /* 2/3 는 섹터 하나에 딱 맞춤 */
                    uint32_t st = 0; int si = sector_index(&F4_1M, 0x08000000u + u0 * 4096u, &st);
                    if (si >= 0) { u0 = (st - 0x08000000u) / 4096u; len = F4_1M.sect_size[si] / 4096u; }
                }
                if (u0 + len > 256u) len = 256u - u0;                              /* 플래시 안으로 자름 */
                R.r[k].origin = 0x08000000u + u0 * 4096u; R.r[k].length = len * 4096u;
                for (uint32_t u = u0; u < u0 + len; u++) if (cover[u]++) want_ov = true;
                if (!on_boundary(&F4_1M, R.r[k].origin) || !on_boundary(&F4_1M, R.r[k].origin + R.r[k].length)) want_al = true;
            }
            unsigned got = layout_check(&R, &F4_1M);                               /* SLOT 크기 비트는 무시 */
            mism += (((got & L_OVERLAP) != 0) != want_ov) + (((got & L_ALIGN) != 0) != want_al) +
                    ((got & ~(L_OVERLAP | L_ALIGN | L_SLOT_SIZE)) != 0);
        }
        CHECK(mism == 0);
    }

    /* ---------- Part C: 이미지 검사 (실제 빌드 + 실제 고장 빌드 + 메모리 안 주입) ---------- */
    CHECK(image_check(&app, &L, "SLOT_A") == 0);
    CHECK(image_check(&boot, &L, "BOOT") == 0);
    CHECK(shared_consistent(&boot, &app));
    CHECK(image_check(&orphan, &L, "SLOT_A") == (I_NOINIT_MISSING | I_IMG_SIZE));  /* orphan: .noinit_shared 가 RAM+플래시 */
    CHECK(!shared_consistent(&boot, &orphan));
    CHECK(image_check(&badhdr, &L, "SLOT_A") == I_HDR_MAGIC);                       /* 섹션 주소는 맞는데 내용이 틀림 */
    image_t x;
    x = app; sect_mut(&x, ".img_hdr")->vma += 4u;
    CHECK(image_check(&x, &L, "SLOT_A") == I_HDR_SECT);
    x = app; x.hdr[8 + 2] = 0x08u;                                                  /* load_addr = 0x08080000 (SLOT_B 로 링크) */
    CHECK(image_check(&x, &L, "SLOT_A") == I_HDR_FIELDS);
    x = app; x.hdr[4] = 0x00u; x.hdr[5] = 0x01u;                                    /* hdr_size 0x100 */
    CHECK(image_check(&x, &L, "SLOT_A") == I_HDR_FIELDS);
    x = app; sect_mut(&x, ".isr_vector")->vma = 0x08020100u; sect_mut(&x, ".isr_vector")->lma = 0x08020100u;
    CHECK(image_check(&x, &L, "SLOT_A") == I_VECTOR);
    x = app; sect_mut(&x, ".noinit")->flags |= SF_LOAD | SF_CONTENTS;
    CHECK(image_check(&x, &L, "SLOT_A") == I_NOINIT_LOAD);
    x = app; sect_mut(&x, ".noinit")->vma = 0x20000020u;
    CHECK(image_check(&x, &L, "SLOT_A") == I_NOINIT_ADDR && !shared_consistent(&boot, &x));
    x = app; sect_mut(&x, ".bss")->vma = 0x08030000u;                               /* NOBITS 가 플래시에 */
    CHECK(image_check(&x, &L, "SLOT_A") == I_REGION);
    x = app; sect_mut(&x, ".text")->size = 0x60000u;                                /* 슬롯을 넘는 코드 */
    CHECK(image_check(&x, &L, "SLOT_A") == (I_REGION | I_IMG_SIZE));
    CHECK(image_check(&app, &L, "SLOT_B") == (I_REGION | I_VECTOR | I_HDR_SECT));  /* A 용 이미지를 B 로 검사 */
    x = boot; sect_mut(&x, ".noinit")->size = 0x24u;                                /* 구조체가 한쪽만 커짐 */
    CHECK(!shared_consistent(&x, &app));

    printf("\n");
    print_layout(&L);
    print_usage("boot", &boot, &L, "BOOT");
    print_usage("app", &app, &L, "SLOT_A");
    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
