/* PCIE-B05 추가 연습 — 한 번에 검증
 *
 * 연습 1: Expansion ROM BAR(0x30) sizing — 0xFFFFFFFF 가 아니라 PCI_ROM_ADDRESS_MASK(~0x7FF)를 쓰는 이유 (enable 비트)
 * 연습 2: /sys/bus/pci/devices/<BDF>/resource 파서 — 실측 텍스트(QEMU nvme, Lima VM 00:06.0)에서 종류/크기
 * 연습 3: lspci 의 [size=...] 표기 (1024 로 나눠지는 동안 K/M/G)
 * 연습 4: 한 창에 BAR 여러 개 배치 — index 순서 vs 큰 것부터: 차지하는 범위(span) 비교 (PCIE-A08 예고편)
 *
 * 자급자족: config mock 은 solution.c 의 축약판 (바이트 배열 + 바이트별 wmask).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PCI_COMMAND             0x04
#define PCI_COMMAND_MEMORY      0x2
#define PCI_ROM_ADDRESS         0x30
#define PCI_ROM_ADDRESS_ENABLE  0x01
#define PCI_ROM_ADDRESS_MASK    (~0x7ffU)
#define IORESOURCE_IO           0x00000100
#define IORESOURCE_MEM          0x00000200
#define IORESOURCE_PREFETCH     0x00002000
#define IORESOURCE_SIZEALIGN    0x00040000
#define IORESOURCE_MEM_64       0x00100000

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { printf("FAIL %s  (line %d)\n", #expr, __LINE__); } } while (0)

/* ---------- mock (solution.c 축약판) ---------- */
typedef struct {
    uint8_t  cfg[256], wmask[256];
    uint32_t rom_on_during_sizing;   /* ROM enable=1 && Command.Memory=1 && ROM 주소가 all-1s 상태였던 write 수 */
    uint32_t rom_wm;
} mdev;
static uint32_t rd32(const mdev *d, uint16_t o) {
    return (uint32_t)d->cfg[o] | (uint32_t)d->cfg[o + 1] << 8 | (uint32_t)d->cfg[o + 2] << 16 | (uint32_t)d->cfg[o + 3] << 24;
}
static void wr32(mdev *d, uint16_t o, uint32_t v) {
    for (int b = 0; b < 4; b++) d->cfg[o + b] = (uint8_t)((d->cfg[o + b] & ~d->wmask[o + b]) | ((uint8_t)(v >> (8 * b)) & d->wmask[o + b]));
    uint32_t rom = rd32(d, PCI_ROM_ADDRESS);
    bool mem_on = d->cfg[PCI_COMMAND] & PCI_COMMAND_MEMORY;
    if (d->rom_wm && mem_on && (rom & PCI_ROM_ADDRESS_ENABLE) && (rom & d->rom_wm & PCI_ROM_ADDRESS_MASK) == (d->rom_wm & PCI_ROM_ADDRESS_MASK))
        d->rom_on_during_sizing++;
}
/* ROM: 주소 비트 31:11 중 크기에 맞는 것 + bit0 enable 이 쓰기 가능 */
static void mdev_init_rom(mdev *d, uint32_t rom_size, uint32_t assigned, bool mem_on) {
    memset(d, 0, sizeof *d);
    d->cfg[PCI_COMMAND] = mem_on ? PCI_COMMAND_MEMORY : 0;
    if (rom_size == 0) return;
    d->rom_wm = (~(rom_size - 1) & PCI_ROM_ADDRESS_MASK) | PCI_ROM_ADDRESS_ENABLE;
    for (int b = 0; b < 4; b++) { d->wmask[PCI_ROM_ADDRESS + b] = (uint8_t)(d->rom_wm >> (8 * b)); d->cfg[PCI_ROM_ADDRESS + b] = (uint8_t)((assigned & d->rom_wm) >> (8 * b)); }
}

/* ---------- 연습 1: ROM sizing ---------- */
static uint32_t rom_size(mdev *d, uint32_t sizing_pattern) {
    uint32_t orig = rd32(d, PCI_ROM_ADDRESS);
    wr32(d, PCI_ROM_ADDRESS, sizing_pattern);
    uint32_t rb = rd32(d, PCI_ROM_ADDRESS) & PCI_ROM_ADDRESS_MASK;
    wr32(d, PCI_ROM_ADDRESS, orig);
    return rb & (~rb + 1);
}

/* ---------- 연습 2: sysfs resource 파서 ---------- */
typedef struct { uint64_t start, end, flags; } res_line;
typedef struct { const char *kind; bool pf; uint64_t size; } res_info;
/* text 의 줄 6개(BAR0..5)를 파싱. 반환: 파싱한 줄 수, 형식 오류면 -1 */
static int parse_resource(const char *text, res_line out[6]) {
    const char *p = text;
    for (int i = 0; i < 6; i++) {
        char *e;
        for (int f = 0; f < 3; f++) {
            while (*p == ' ') p++;
            if (p[0] != '0' || p[1] != 'x') return -1;
            uint64_t v = strtoull(p, &e, 16);
            if (e == p + 2) return -1;
            if (f == 0) out[i].start = v; else if (f == 1) out[i].end = v; else out[i].flags = v;
            p = e;
        }
        if (*p == '\n') p++; else if (*p != '\0') return -1;
        if (*p == '\0' && i < 5) return i + 1;
    }
    return 6;
}
static res_info res_decode(const res_line *r) {
    res_info x = { "none", false, 0 };
    if (r->flags == 0 || r->end < r->start) return x;
    x.size = r->end - r->start + 1;
    x.pf = (r->flags & IORESOURCE_PREFETCH) != 0;
    x.kind = (r->flags & IORESOURCE_IO) ? "IO" : (r->flags & IORESOURCE_MEM_64) ? "Mem64" : (r->flags & IORESOURCE_MEM) ? "Mem32" : "?";
    return x;
}

/* ---------- 연습 3: lspci [size=] ---------- */
static void fmt_size(uint64_t x, char *buf, size_t cap) {
    static const char *suf[] = { "", "K", "M", "G", "T" };
    unsigned i = 0;
    while (i < 4 && x && x % 1024 == 0) { x /= 1024; i++; }
    snprintf(buf, cap, "%" PRIu64 "%s", x, suf[i]);
}

/* ---------- 연습 4: 배치 span ---------- */
static uint64_t align_up(uint64_t x, uint64_t a) { return (x + a - 1) & ~(a - 1); }
static uint64_t span_in_order(const uint64_t *sz, int n) {
    uint64_t cur = 0;
    for (int i = 0; i < n; i++) cur = align_up(cur, sz[i]) + sz[i];
    return cur;
}
static uint64_t span_largest_first(const uint64_t *sz, int n) {
    uint64_t s[16]; memcpy(s, sz, sizeof(uint64_t) * (size_t)n);
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && s[j] > s[j - 1]; j--) { uint64_t t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
    return span_in_order(s, n);   /* 큰 것부터면 매번 이미 정렬돼 있어 빈틈 0 */
}

int main(void) {
    printf("== 연습 1: ROM BAR ==\n");
    mdev d;
    mdev_init_rom(&d, 64 * 1024, 0xFEB00000u, true);
    uint32_t before = rd32(&d, PCI_ROM_ADDRESS);
    CHECK(rom_size(&d, PCI_ROM_ADDRESS_MASK) == 0x10000);
    CHECK(rd32(&d, PCI_ROM_ADDRESS) == before && before == 0xFEB00000u);
    CHECK(d.rom_on_during_sizing == 0);
    CHECK(rom_size(&d, 0xFFFFFFFFu) == 0x10000);          /* 크기는 같게 나오지만 */
    CHECK(d.rom_on_during_sizing == 1);                    /* ROM 디코드가 0xFFFF0000 에서 잠깐 켜졌다 */
    mdev_init_rom(&d, 0, 0, true);
    CHECK(rom_size(&d, PCI_ROM_ADDRESS_MASK) == 0);        /* ROM 없음 */
    mdev_init_rom(&d, 2048, 0, false);
    CHECK(rom_size(&d, PCI_ROM_ADDRESS_MASK) == 2048);     /* 최소 2KB (bit 11) */

    printf("== 연습 2: sysfs resource ==\n");
    const char *qemu_nvme =                                /* capture_qemu_bar_sizing.txt */
        "0x0000000010000000 0x0000000010003fff 0x0000000000140204\n"
        "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
        "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
        "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
        "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
        "0x0000000000000000 0x0000000000000000 0x0000000000000000\n";
    const char *vm_0006 =                                  /* capture_vm_0006_bars.txt */
        "0x0000000380020000 0x000000038002ffff 0x0000000000140204\n"
        "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
        "0x0000000050001540 0x000000005000157f 0x0000000000040200\n"
        "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
        "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
        "0x0000000000000000 0x0000000000000000 0x0000000000000000\n";
    res_line rl[6];
    CHECK(parse_resource(qemu_nvme, rl) == 6);
    res_info ri = res_decode(&rl[0]);
    CHECK(strcmp(ri.kind, "Mem64") == 0 && !ri.pf && ri.size == 0x4000);
    CHECK((rl[0].flags & 0xFF) == 0x04);                  /* 하위 8비트 = BAR 의 원래 type 비트 */
    CHECK((rl[0].flags & IORESOURCE_SIZEALIGN) != 0);
    CHECK(strcmp(res_decode(&rl[1]).kind, "none") == 0);
    CHECK(parse_resource(vm_0006, rl) == 6);
    ri = res_decode(&rl[0]);
    CHECK(strcmp(ri.kind, "Mem64") == 0 && ri.size == 0x10000 && rl[0].start == 0x380020000ull);
    ri = res_decode(&rl[2]);
    CHECK(strcmp(ri.kind, "Mem32") == 0 && ri.size == 64 && rl[2].start % 64 == 0);  /* 64B BAR 도 자연 정렬 */
    CHECK(parse_resource("0x10 0x1f\n", rl) == -1);        /* 필드 모자람 */
    CHECK(parse_resource("zz 0x1 0x2\n", rl) == -1);

    printf("== 연습 3: lspci [size=] ==\n");
    char b[16];
    fmt_size(256, b, sizeof b);              CHECK(strcmp(b, "256") == 0);
    fmt_size(4096, b, sizeof b);             CHECK(strcmp(b, "4K") == 0);
    fmt_size(0x4000, b, sizeof b);           CHECK(strcmp(b, "16K") == 0);
    fmt_size(256ull << 20, b, sizeof b);     CHECK(strcmp(b, "256M") == 0);
    fmt_size(8ull << 30, b, sizeof b);       CHECK(strcmp(b, "8G") == 0);
    fmt_size(64, b, sizeof b);               CHECK(strcmp(b, "64") == 0);
    fmt_size(1536, b, sizeof b);             CHECK(strcmp(b, "1536") == 0);

    printf("== 연습 4: 배치 span ==\n");
    const uint64_t sz[4] = { 4096, 65536, 4096, 1u << 20 };
    uint64_t s1 = span_in_order(sz, 4), s2 = span_largest_first(sz, 4);
    printf("  sizes 4K,64K,4K,1M: index order span = 0x%" PRIX64 ", largest-first span = 0x%" PRIX64 "\n", s1, s2);
    CHECK(s1 == 0x200000);
    CHECK(s2 == 0x112000);
    const uint64_t sz2[3] = { 16, 64, 16 };          /* 작은 BAR 들 (virtio 의 64B BAR 처럼) */
    CHECK(span_in_order(sz2, 3) == 0x90 && span_largest_first(sz2, 3) == 0x60);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
