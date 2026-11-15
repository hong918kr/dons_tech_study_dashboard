/* TOOL-B06 추가 연습 — 한 번에 검증
 *  연습 1  macho_usage   : Mac `size -m` (Mach-O) 출력 → Flash 비슷한 값 / RAM 비슷한 값
 *  연습 2  top_grower    : 두 -A 출력 비교 → 가장 많이 늘어난 섹션과 Flash 증가량 (CI 코멘트용)
 *  연습 3  data_lma_check: objdump -h 의 .data VMA/LMA → 이미지 끝 주소 == Flash 사용량 확인
 *  (parse_sysv / classify 는 solution.c 와 같은 구현을 복사해 둠)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool next_line(const char **p, char *buf, size_t n) {
    const char *s = *p;
    if (*s == '\0') return false;
    size_t len = strcspn(s, "\n");
    size_t k = len < n - 1 ? len : n - 1;
    memcpy(buf, s, k);
    buf[k] = '\0';
    *p = s + len + (s[len] == '\n');
    return true;
}

typedef struct {
    char name[32];
    uint32_t size, addr;
} section_t;

typedef enum { REG_NONE, REG_FLASH, REG_FLASH_RAM, REG_RAM } region_t;

static int parse_sysv(const char *txt, section_t *secs, int max) {
    char line[256];
    bool in_table = false;
    int n = 0;
    unsigned long sum = 0;
    while (next_line(&txt, line, sizeof line)) {
        char a[64], b[32], c[32];
        int k = sscanf(line, "%63s %31s %31s", a, b, c);
        if (!in_table) {
            if (k == 3 && strcmp(a, "section") == 0 && strcmp(b, "size") == 0 && strcmp(c, "addr") == 0)
                in_table = true;
            continue;
        }
        if (k >= 2 && strcmp(a, "Total") == 0) {
            char *e;
            unsigned long tot = strtoul(b, &e, 0);
            return (*e == '\0' && tot == sum) ? n : -1;
        }
        if (k != 3 || n >= max || strlen(a) >= sizeof secs[n].name) return -1;
        char *e1, *e2;
        unsigned long sz = strtoul(b, &e1, 0), ad = strtoul(c, &e2, 0); /* base 0: "516" 과 "0x204" 둘 다 */
        if (*e1 != '\0' || *e2 != '\0') return -1;
        memcpy(secs[n].name, a, strlen(a) + 1);
        secs[n].size = (uint32_t)sz;
        secs[n].addr = (uint32_t)ad;
        sum += sz;
        n++;
    }
    return -1; /* Total 줄이 없다 = 출력이 잘렸다 */
}

static region_t classify(const char *name) {
    static const struct {
        const char *base;
        region_t reg;
    } rules[] = {
        {".isr_vector", REG_FLASH}, {".text", REG_FLASH}, {".rodata", REG_FLASH}, {".ARM.exidx", REG_FLASH},
        {".ARM.extab", REG_FLASH}, {".init", REG_FLASH}, {".fini", REG_FLASH}, {".init_array", REG_FLASH},
        {".fini_array", REG_FLASH}, {".preinit_array", REG_FLASH}, {".data", REG_FLASH_RAM}, {".bss", REG_RAM},
        {".noinit", REG_RAM}, {".heap", REG_RAM}, {".stack", REG_RAM}, {"._user_heap_stack", REG_RAM},
    };
    for (size_t i = 0; i < sizeof rules / sizeof rules[0]; i++) {
        size_t k = strlen(rules[i].base);
        if (strncmp(name, rules[i].base, k) == 0 && (name[k] == '\0' || name[k] == '.')) return rules[i].reg;
    }
    return REG_NONE; /* .comment .ARM.attributes .debug_* .note.* .llvm_addrsig ... */
}

/* ---------------- 연습 1: Mac size -m ---------------- */
/* 두 가지 줄 모양을 다 읽는다:
 *   .o  : "\tSection (__TEXT, __text): 112"        (세그먼트 이름이 괄호 안에)
 *   실행 파일: "Segment __DATA: 16384" 다음 "\tSection __data: 516"  (세그먼트는 앞 줄에서)
 * flash_like = __TEXT 의 모든 섹션 + __DATA,__data + __DATA,__const   (펌웨어로 치면 "이미지에 실리는 바이트")
 * ram_like   = __DATA,__data + __DATA,__bss + __DATA,__common          (zerofill 은 파일에 없음)
 * __LD,__compact_unwind 같은 링커 전용 섹션은 어느 쪽에도 넣지 않는다.
 * 첫 "total N" 이 아닌 줄들을 읽다가, 처음 나오는 "$ " 명령줄(두 번째 블록 시작)에서 멈춘다 — start 는 블록 시작 위치. */
typedef struct {
    uint32_t flash_like, ram_like;
    int nsect;
} macho_usage_t;

static macho_usage_t macho_usage(const char *start) {
    macho_usage_t u = {0, 0, 0};
    char line[256], seg[32] = "";
    const char *p = start;
    bool first = true;
    while (next_line(&p, line, sizeof line)) {
        if (line[0] == '$') {
            if (!first) break; /* 다음 명령 블록 */
            first = false;
            continue;
        }
        char sg[32] = "", sc[32] = "";
        unsigned long v;
        if (sscanf(line, "Segment %31[^:]: %lu", sg, &v) == 2) {
            snprintf(seg, sizeof seg, "%s", sg);
            continue;
        }
        if (sscanf(line, " Section (%31[^,], %31[^)]): %lu", sg, sc, &v) == 3) {
            /* .o 형식: 세그먼트가 괄호 안에 */
        } else if (sscanf(line, " Section %31[^:]: %lu", sc, &v) == 2) {
            snprintf(sg, sizeof sg, "%s", seg);
        } else {
            continue;
        }
        u.nsect++;
        bool text = strcmp(sg, "__TEXT") == 0, dseg = strcmp(sg, "__DATA") == 0;
        bool init = dseg && (strcmp(sc, "__data") == 0 || strcmp(sc, "__const") == 0);
        bool zero = dseg && (strcmp(sc, "__bss") == 0 || strcmp(sc, "__common") == 0);
        if (text || init) u.flash_like += (uint32_t)v;
        if ((dseg && strcmp(sc, "__data") == 0) || zero) u.ram_like += (uint32_t)v;
    }
    return u;
}

/* ---------------- 연습 2: 가장 많이 늘어난 섹션 ---------------- */
/* 이미지에 실리는 섹션(classify != REG_NONE)만 본다. before 에 없던 섹션은 0 에서 늘어난 것.
 * 반환: after 쪽 섹션 인덱스(늘어난 게 없으면 -1), *grow 에 증가 바이트, *flash_delta 에 Flash 증감(부호 있음). */
static int top_grower(const section_t *b, int nb, const section_t *a, int na, uint32_t *grow, int32_t *flash_delta) {
    int best = -1;
    uint32_t best_g = 0;
    int64_t fb = 0, fa = 0;
    for (int i = 0; i < nb; i++) {
        region_t g = classify(b[i].name);
        if (g == REG_FLASH || g == REG_FLASH_RAM) fb += b[i].size;
    }
    for (int i = 0; i < na; i++) {
        region_t g = classify(a[i].name);
        if (g == REG_NONE) continue;
        if (g == REG_FLASH || g == REG_FLASH_RAM) fa += a[i].size;
        uint32_t old = 0;
        for (int j = 0; j < nb; j++)
            if (strcmp(b[j].name, a[i].name) == 0) old = b[j].size;
        if (a[i].size > old && a[i].size - old > best_g) {
            best_g = a[i].size - old;
            best = i;
        }
    }
    *grow = best_g;
    *flash_delta = (int32_t)(fa - fb);
    return best;
}

/* ---------------- 연습 3: .data 의 VMA / LMA ---------------- */
/* objdump -h 의 섹션 줄: "  3 .data         00000204  20000000  080000d4  00020000  2**2" (16진)
 * .data 를 찾아 VMA 가 RAM 창, LMA 가 Flash 창 안인지 보고, Flash 이미지 끝 = LMA + size 를 돌려준다.
 * (.data 가 Flash 에 마지막으로 놓이는 섹션이라는 fw.ld 배치를 가정) 실패하면 0. */
static uint32_t data_lma_check(const char *txt, uint32_t flash_org, uint32_t flash_len, uint32_t ram_org,
                               uint32_t ram_len) {
    char line[256];
    while (next_line(&txt, line, sizeof line)) {
        unsigned idx;
        char name[32];
        unsigned long size, vma, lma;
        if (sscanf(line, " %u %31s %lx %lx %lx", &idx, name, &size, &vma, &lma) != 5) continue;
        if (strcmp(name, ".data") != 0) continue;
        bool vma_ok = vma >= ram_org && vma + size <= (unsigned long)ram_org + ram_len;
        bool lma_ok = lma >= flash_org && lma + size <= (unsigned long)flash_org + flash_len;
        return (vma_ok && lma_ok && vma != lma) ? (uint32_t)(lma + size) : 0u;
    }
    return 0u;
}

/* ---------------- 실제 출력 (demo_size.sh) ---------------- */
/* size_mac_host.txt */
static const char MAC_HOST[] =
    "$ size -m fw.o        # cc -Os -DTBL_INIT -c (arm64 Mach-O)\n"
    "Segment : 728\n"
    "\tSection (__TEXT, __text): 112\n"
    "\tSection (__DATA, __data): 516\n"
    "\tSection (__DATA, __bss): 64 (zerofill)\n"
    "\tSection (__LD, __compact_unwind): 32\n"
    "\ttotal 724\n"
    "total 728\n"
    "$ size fw.o\n"
    "__TEXT\t__DATA\t__OBJC\tothers\tdec\thex\n"
    "112\t580\t0\t32\t724\t2d4\n"
    "$ size -m host_fw     # linked executable\n"
    "Segment __PAGEZERO: 4294967296 (zero fill) \n"
    "Segment __TEXT: 16384\n"
    "\tSection __text: 112\n"
    "\tSection __unwind_info: 88\n"
    "\ttotal 200\n"
    "Segment __DATA: 16384\n"
    "\tSection __data: 516\n"
    "\tSection __bss: 64 (zerofill)\n"
    "\ttotal 580\n"
    "Segment __LINKEDIT: 16384\n"
    "total 4295016448\n"
    "$ size host_fw\n"
    "__TEXT\t__DATA\t__OBJC\tothers\tdec\thex\n"
    "16384\t16384\t0\t4294983680\t4295016448\t10000c000\n";
/* size_vm_strings.txt */
static const char VM_STRINGS[] =
    "$ arm-none-eabi-size fw.elf    # -DTBL_CONST -DWITH_STRINGS --specs=nano.specs --specs=nosys.specs \n"
    "   text\t   data\t    bss\t    dec\t    hex\tfilename\n"
    "    796\t      4\t   1604\t   2404\t    964\tfw.elf\n"
    "$ arm-none-eabi-size -A fw.elf\n"
    "fw.elf  :\n"
    "section             size        addr\n"
    ".isr_vector           64   134217728\n"
    ".text                168   134217792\n"
    ".rodata              564   134217960\n"
    ".data                  4   536870912\n"
    ".bss                  64   536870916\n"
    "._user_heap_stack   1540   536870980\n"
    ".comment              38           0\n"
    ".ARM.attributes       46           0\n"
    "Total               2488\n"
    "\n"
    "\n";
/* size_vm_printf.txt */
static const char VM_PRINTF[] =
    "$ arm-none-eabi-size fw.elf    # -DTBL_CONST -DWITH_STRINGS -DWITH_PRINTF --specs=nano.specs --specs=nosys.specs \n"
    "   text\t   data\t    bss\t    dec\t    hex\tfilename\n"
    "   7400\t     96\t   1936\t   9432\t   24d8\tfw.elf\n"
    "$ arm-none-eabi-size -A fw.elf\n"
    "fw.elf  :\n"
    "section               size        addr\n"
    ".isr_vector             64   134217728\n"
    ".text                 6684   134217792\n"
    ".rodata                644   134224476\n"
    ".ARM.exidx               8   134225120\n"
    ".data                   96   536870912\n"
    ".bss                   396   536871008\n"
    "._user_heap_stack     1540   536871404\n"
    ".comment                38           0\n"
    ".ARM.attributes         44           0\n"
    ".debug_info          62443           0\n"
    ".debug_abbrev        15185           0\n"
    ".debug_loclists      10426           0\n"
    ".debug_aranges        1144           0\n"
    ".debug_line          20422           0\n"
    ".debug_str            5176           0\n"
    ".debug_frame          2392           0\n"
    ".debug_rnglists        503           0\n"
    ".debug_line_str        159           0\n"
    "Total               127364\n"
    "\n"
    "\n";
/* size_vm_init_objdump.txt */
static const char VM_INIT_OBJDUMP[] =
    "$ arm-none-eabi-objdump -h fw.elf    # -DTBL_INIT\n"
    "fw.elf:     file format elf32-littlearm\n"
    "\n"
    "Sections:\n"
    "Idx Name          Size      VMA       LMA       File off  Algn\n"
    "  0 .isr_vector   00000040  08000000  08000000  00010000  2**2\n"
    "                  CONTENTS, ALLOC, LOAD, READONLY, DATA\n"
    "  1 .text         00000094  08000040  08000040  00010040  2**2\n"
    "                  CONTENTS, ALLOC, LOAD, READONLY, CODE\n"
    "  2 .rodata       00000000  080000d4  080000d4  00020204  2**0\n"
    "                  CONTENTS, ALLOC, LOAD, DATA\n"
    "  3 .data         00000204  20000000  080000d4  00020000  2**2\n"
    "                  CONTENTS, ALLOC, LOAD, DATA\n"
    "  4 .bss          00000040  20000204  080002d8  00020204  2**0\n"
    "                  ALLOC\n"
    "  5 ._user_heap_stack 00000604  20000244  080002d8  00020244  2**0\n"
    "                  ALLOC\n"
    "  6 .comment      00000026  00000000  00000000  00020204  2**0\n"
    "                  CONTENTS, READONLY\n"
    "  7 .ARM.attributes 0000002e  00000000  00000000  0002022a  2**0\n"
    "                  CONTENTS, READONLY\n";

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1: .o 블록 → __text 112 + __data 516 (Flash 비슷), __data 516 + __bss 64 (RAM 비슷) */
    macho_usage_t o = macho_usage(MAC_HOST);
    CHECK(o.nsect == 4 && o.flash_like == 628u && o.ram_like == 580u);
    /* 실행 파일 블록: __text 112 + __unwind_info 88 + __data 516 */
    const char *exe = strstr(MAC_HOST, "$ size -m host_fw");
    CHECK(exe != NULL);
    macho_usage_t x = macho_usage(exe ? exe : "");
    CHECK(x.nsect == 4 && x.flash_like == 716u && x.ram_like == 580u);

    /* 연습 2: strings → printf(nano) */
    section_t b[32], a[32];
    int nb = parse_sysv(VM_STRINGS, b, 32), na = parse_sysv(VM_PRINTF, a, 32);
    uint32_t grow = 0;
    int32_t fd = 0;
    int t = top_grower(b, nb, a, na, &grow, &fd);
    CHECK(nb == 8 && na == 18 && t >= 0 && strcmp(a[t].name, ".text") == 0);
    CHECK(grow == 6684u - b[1].size && fd == 7496 - 800);
    /* 반대 방향(printf 를 지움) → Flash 증감은 음수, 늘어난 섹션이 하나도 없으니 -1 */
    t = top_grower(a, na, b, nb, &grow, &fd);
    CHECK(fd == 800 - 7496);
    CHECK(t == -1 && grow == 0u);

    /* 연습 3: .data LMA + size == Flash 이미지 끝 → 끝 - 0x08000000 == Flash 사용량 728 */
    uint32_t end = data_lma_check(VM_INIT_OBJDUMP, 0x08000000u, 64u * 1024u, 0x20000000u, 16u * 1024u);
    CHECK(end == 0x080002d8u && end - 0x08000000u == 728u);
    CHECK(data_lma_check(VM_INIT_OBJDUMP, 0x08000000u, 64u * 1024u, 0x30000000u, 16u * 1024u) == 0u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
