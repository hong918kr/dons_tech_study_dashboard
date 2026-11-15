/* TOOL-B06 size로 섹션 크기 읽기: text data bss
 *  size 출력 텍스트(Berkeley / -A(SysV)) → 섹션 분류 → Flash/RAM 사용량 → 예산(CI 게이트) 검사.
 *  테스트 데이터 = demo_size.sh 가 실제로 찍은 size_*.txt 를 글자 그대로 문자열로 넣은 것.
 *  공식: Flash = text + data,  RAM = data + bss   (.data 는 Flash 에 초기값, RAM 에 실행 사본)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 공통: 한 줄씩 읽기 (TODO 아님) ---------------- */
/* *p 에서 한 줄을 buf 로 복사(개행 제외, 너무 길면 잘림)하고 *p 를 다음 줄로 옮긴다. 끝이면 false. */
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

/* ---------------- Part A: Berkeley 형식 (size 기본 출력) ---------------- */
typedef struct {
    uint32_t text, data, bss, dec;
    char file[48];
} berk_row_t;

/* "text data bss dec hex filename" 머리줄 아래의 행들을 rows 에 채운다.
 * 반환: 행 수(≥1). 머리줄 없음 / 행 0개 / max 초과 / dec ≠ text+data+bss / hex ≠ dec → -1.
 * 머리줄 뒤에서 숫자 6칸으로 읽히지 않는 첫 줄(빈 줄, "$ ..." 명령줄)이 표의 끝이다. */
static int parse_berkeley(const char *txt, berk_row_t *rows, int max) {
    (void)txt; (void)rows; (void)max; (void)next_line;
    return -1; /* TODO: 머리줄 찾기 → 행마다 6칸 읽기 → dec/hex 검증 */
}

static uint32_t berk_flash(const berk_row_t *r) { (void)r; return 0; /* TODO */ }
static uint32_t berk_ram(const berk_row_t *r) { (void)r; return 0; /* TODO */ }

/* ---------------- Part B: -A (SysV) 형식과 섹션 분류 ---------------- */
typedef struct {
    char name[32];
    uint32_t size, addr;
} section_t;

typedef enum {
    REG_NONE,      /* 이미지에 안 실림: .comment .ARM.attributes .debug_* .note.* ... */
    REG_FLASH,     /* Flash 에만: 코드, const, 벡터 표, unwind 표, 생성자 표 */
    REG_FLASH_RAM, /* 둘 다: .data — Flash 에 초기값(LMA) + RAM 에 실행 사본(VMA) */
    REG_RAM        /* RAM 에만: .bss, .noinit, 힙/스택 예약 */
} region_t;

/* "section size addr" 머리줄부터 "Total N" 줄까지 읽는다. 숫자는 10진 또는 0x 16진(-x 옵션).
 * 반환: 섹션 수. 머리줄 없음 / Total 없음 / Total ≠ 크기 합 / 칸 수 이상 / 이름이 너무 김 / max 초과 → -1. */
static int parse_sysv(const char *txt, section_t *secs, int max) {
    (void)txt; (void)secs; (void)max;
    return -1; /* TODO: "section size addr" ~ "Total", strtoul(.., 0), 합계 검증 */
}

/* 이름으로 분류. ".text" 는 ".text" 자체와 ".text.xxx"(-ffunction-sections 조각)만 — ".textual" 은 아님. */
static region_t classify(const char *name) {
    (void)name;
    return REG_NONE; /* TODO: 이름(정확히 또는 "이름.xxx") → 영역 */
}

static uint32_t flash_bytes(const section_t *s, int n) {
    (void)s; (void)n;
    return 0; /* TODO: REG_FLASH + REG_FLASH_RAM */
}

static uint32_t ram_bytes(const section_t *s, int n) {
    (void)s; (void)n;
    return 0; /* TODO: REG_RAM + REG_FLASH_RAM */
}

/* ---------------- Part C: 예산 검사와 CI 크기 게이트 ---------------- */
typedef struct {
    uint32_t flash, ram;
} usage_t;

typedef struct {
    uint32_t flash_limit, ram_limit;           /* 칩 용량 (바이트) */
    uint32_t max_flash_growth, max_ram_growth; /* PR 하나가 늘려도 되는 최대치 */
} budget_t;

enum { GATE_OK = 0, GATE_FLASH_OVER = 1, GATE_RAM_OVER = 2, GATE_FLASH_GROWTH = 4, GATE_RAM_GROWTH = 8 };

static usage_t usage_of(const section_t *s, int n) {
    usage_t u = {flash_bytes(s, n), ram_bytes(s, n)};
    return u;
}

/* 딱 맞으면(== limit) 들어간다. */
static bool fits(usage_t u, uint32_t flash_limit, uint32_t ram_limit) {
    (void)u; (void)flash_limit; (void)ram_limit;
    return false; /* TODO */
}

/* 용량 초과와 "이번 변경으로 너무 많이 늘었다" 를 비트로. 줄어든 것은 절대 GROWTH 가 아니다. */
static unsigned size_gate(usage_t before, usage_t after, const budget_t *b) {
    (void)before; (void)after; (void)b;
    return GATE_OK; /* TODO: 초과 비트 + 증가량 비트 (unsigned 뺄셈 주의) */
}

/* "Flash 7496/65536 B (11.4%), RAM 2032/16384 B (12.4%)" — 퍼센트는 소수 1자리, 반올림.
 * 반환: 쓴 글자 수. limit 이 0 이거나 buf 가 모자라면 -1. */
static int format_usage(char *buf, size_t n, usage_t u, const budget_t *b) {
    (void)buf; (void)n; (void)u; (void)b;
    return -1; /* TODO: 0.1% 단위 반올림, snprintf 잘림 검사 */
}

/* ---------------- 실제 size 출력 (demo_size.sh → size_*.txt, 글자 그대로) ---------------- */
/* size_vm_init.txt */
static const char VM_INIT[] =
    "$ arm-none-eabi-size fw.elf    # -DTBL_INIT --specs=nano.specs --specs=nosys.specs \n"
    "   text\t   data\t    bss\t    dec\t    hex\tfilename\n"
    "    212\t    516\t   1604\t   2332\t    91c\tfw.elf\n"
    "$ arm-none-eabi-size -A fw.elf\n"
    "fw.elf  :\n"
    "section             size        addr\n"
    ".isr_vector           64   134217728\n"
    ".text                148   134217792\n"
    ".rodata                0   134217940\n"
    ".data                516   536870912\n"
    ".bss                  64   536871428\n"
    "._user_heap_stack   1540   536871492\n"
    ".comment              38           0\n"
    ".ARM.attributes       46           0\n"
    "Total               2416\n"
    "\n"
    "\n";
/* size_vm_init_hex.txt */
static const char VM_INIT_HEX[] =
    "$ arm-none-eabi-size -A -x fw.elf    # -DTBL_INIT\n"
    "fw.elf  :\n"
    "section              size         addr\n"
    ".isr_vector          0x40    0x8000000\n"
    ".text                0x94    0x8000040\n"
    ".rodata               0x0    0x80000d4\n"
    ".data               0x204   0x20000000\n"
    ".bss                 0x40   0x20000204\n"
    "._user_heap_stack   0x604   0x20000244\n"
    ".comment             0x26          0x0\n"
    ".ARM.attributes      0x2e          0x0\n"
    "Total               0x970\n"
    "\n"
    "\n";
/* size_vm_const.txt */
static const char VM_CONST[] =
    "$ arm-none-eabi-size fw.elf    # -DTBL_CONST --specs=nano.specs --specs=nosys.specs \n"
    "   text\t   data\t    bss\t    dec\t    hex\tfilename\n"
    "    708\t      4\t   1604\t   2316\t    90c\tfw.elf\n"
    "$ arm-none-eabi-size -A fw.elf\n"
    "fw.elf  :\n"
    "section             size        addr\n"
    ".isr_vector           64   134217728\n"
    ".text                132   134217792\n"
    ".rodata              512   134217924\n"
    ".data                  4   536870912\n"
    ".bss                  64   536870916\n"
    "._user_heap_stack   1540   536870980\n"
    ".comment              38           0\n"
    ".ARM.attributes       46           0\n"
    "Total               2400\n"
    "\n"
    "\n";
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
/* size_vm_printf_full.txt */
static const char VM_PRINTF_FULL[] =
    "$ arm-none-eabi-size fw.elf    # -DTBL_CONST -DWITH_STRINGS -DWITH_PRINTF --specs=nosys.specs \n"
    "   text\t   data\t    bss\t    dec\t    hex\tfilename\n"
    "  31792\t   1724\t   1980\t  35496\t   8aa8\tfw.elf\n"
    "$ arm-none-eabi-size -A fw.elf\n"
    "fw.elf  :\n"
    "section               size        addr\n"
    ".isr_vector             64   134217728\n"
    ".text                30236   134217792\n"
    ".rodata               1484   134248032\n"
    ".ARM.exidx               8   134249516\n"
    ".data                 1724   536870912\n"
    ".bss                   444   536872636\n"
    "._user_heap_stack     1536   536873080\n"
    ".comment                38           0\n"
    ".ARM.attributes         44           0\n"
    ".debug_info         109847           0\n"
    ".debug_abbrev        26853           0\n"
    ".debug_loclists      54699           0\n"
    ".debug_aranges        1968           0\n"
    ".debug_line          54321           0\n"
    ".debug_str            8045           0\n"
    ".debug_frame          5032           0\n"
    ".debug_rnglists       1294           0\n"
    ".debug_line_str        337           0\n"
    "Total               297974\n"
    "\n"
    "\n";
/* size_vm_objs_total.txt */
static const char VM_OBJS_TOTAL[] =
    "$ arm-none-eabi-size -t startup.o fw.o\n"
    "   text\t   data\t    bss\t    dec\t    hex\tfilename\n"
    "    132\t      0\t      0\t    132\t     84\tstartup.o\n"
    "     80\t    516\t     64\t    660\t    294\tfw.o\n"
    "    212\t    516\t     64\t    792\t    318\t(TOTALS)\n";
/* size_mac_strings.txt */
static const char MAC_STRINGS[] =
    "$ llvm-size -A fw.o    # -DTBL_CONST -DWITH_STRINGS\n"
    "fw.o  :\n"
    "section             size   addr\n"
    ".text                134      0\n"
    ".ARM.exidx             8      0\n"
    ".rodata              524      0\n"
    ".data                  4      0\n"
    ".rodata.str1.1        39      0\n"
    ".bss                  64      0\n"
    ".comment              51      0\n"
    ".note.GNU-stack        0      0\n"
    ".ARM.attributes       65      0\n"
    ".llvm_addrsig          0      0\n"
    "Total                889\n"
    "\n"
    "\n"
    "$ llvm-size fw.o\n"
    "   text\t   data\t    bss\t    dec\t    hex\tfilename\n"
    "    705\t      4\t     64\t    773\t    305\tfw.o\n";
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
/* 일부러 망가뜨린 입력 */
static const char BAD_DEC[] =
    "   text\t   data\t    bss\t    dec\t    hex\tfilename\n"
    "    212\t    516\t   1604\t   2333\t    91d\tfw.elf\n"; /* dec 이 1 틀림 */
static const char BAD_TOTAL[] =
    "section             size        addr\n"
    ".text                148   134217792\n"
    ".data                516   536870912\n"
    "Total                600\n"; /* 664 여야 함 */
static const char NO_TOTAL[] =
    "section             size        addr\n"
    ".text                148   134217792\n";

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define MAXS 32
static int sysv(const char *txt, section_t *s) { return parse_sysv(txt, s, MAXS); }

int main(void) {
    berk_row_t r[4];
    section_t s[MAXS], s2[MAXS];
    int n, n2;

    /* Part A — Berkeley */
    memset(r, 0, sizeof r);
    CHECK(parse_berkeley(VM_INIT, r, 4) == 1);
    CHECK(r[0].text == 212u && r[0].data == 516u && r[0].bss == 1604u && strcmp(r[0].file, "fw.elf") == 0);
    CHECK(berk_flash(&r[0]) == 728u && berk_ram(&r[0]) == 2120u);
    CHECK(parse_berkeley(VM_PRINTF_FULL, r, 4) == 1 && berk_flash(&r[0]) == 33516u && berk_ram(&r[0]) == 3704u);
    CHECK(parse_berkeley(MAC_STRINGS, r, 4) == 1 && berk_flash(&r[0]) == 709u && berk_ram(&r[0]) == 68u);
    CHECK(parse_berkeley(VM_OBJS_TOTAL, r, 4) == 3 && strcmp(r[2].file, "(TOTALS)") == 0);
    CHECK(r[0].text + r[1].text == r[2].text && r[0].bss + r[1].bss == r[2].bss);
    CHECK(parse_berkeley(VM_OBJS_TOTAL, r, 2) == -1);  /* 자리 모자람 */
    CHECK(parse_berkeley(BAD_DEC, r, 4) == -1);        /* dec ≠ text+data+bss */
    CHECK(parse_berkeley(MAC_HOST, r, 4) == -1);       /* Mach-O size: __TEXT __DATA ... 형식은 Berkeley 아님 */

    /* Part B — -A 와 분류 */
    n = sysv(VM_INIT, s);
    CHECK(n == 8 && strcmp(s[3].name, ".data") == 0 && s[3].size == 516u && s[3].addr == 0x20000000u);
    CHECK(classify(".text") == REG_FLASH && classify(".rodata.str1.1") == REG_FLASH && classify(".isr_vector") == REG_FLASH);
    CHECK(classify(".ARM.exidx") == REG_FLASH && classify(".init_array") == REG_FLASH && classify(".text.startup") == REG_FLASH);
    CHECK(classify(".data") == REG_FLASH_RAM && classify(".data.boot_count") == REG_FLASH_RAM);
    CHECK(classify(".bss") == REG_RAM && classify("._user_heap_stack") == REG_RAM && classify(".noinit") == REG_RAM);
    CHECK(classify(".ARM.attributes") == REG_NONE && classify(".comment") == REG_NONE && classify(".debug_info") == REG_NONE);
    CHECK(classify(".note.GNU-stack") == REG_NONE && classify(".textual") == REG_NONE && classify(".database") == REG_NONE);
    CHECK(flash_bytes(s, n) == 728u && ram_bytes(s, n) == 2120u);
    n = sysv(VM_PRINTF, s);
    CHECK(n == 18 && flash_bytes(s, n) == 7496u && ram_bytes(s, n) == 2032u); /* .debug_* 9개 제외 */
    n2 = sysv(VM_INIT_HEX, s2);
    CHECK(n2 == 8 && s2[3].size == 0x204u && s2[3].addr == 0x20000000u && flash_bytes(s2, n2) == 728u && ram_bytes(s2, n2) == 2120u);
    n = sysv(MAC_STRINGS, s);
    CHECK(n == 10 && flash_bytes(s, n) == 709u && ram_bytes(s, n) == 68u);
    CHECK(sysv(BAD_TOTAL, s) == -1 && sysv(NO_TOTAL, s) == -1 && sysv(VM_OBJS_TOTAL, s) == -1);
    CHECK(parse_sysv(VM_PRINTF, s, 10) == -1);

    /* Part C — 예산과 CI 게이트 (가상 부품: Flash 32 KB / RAM 4 KB, PR 당 +1024 B / +256 B) */
    usage_t u_init = usage_of(s2, n2);
    n = sysv(VM_CONST, s);   usage_t u_const = usage_of(s, n);
    n = sysv(VM_STRINGS, s); usage_t u_str = usage_of(s, n);
    n = sysv(VM_PRINTF, s);  usage_t u_pf = usage_of(s, n);
    n = sysv(VM_PRINTF_FULL, s); usage_t u_full = usage_of(s, n);
    budget_t small = {32768u, 4096u, 1024u, 256u};
    budget_t f4 = {65536u, 16384u, 1024u, 256u};
    CHECK(u_const.flash == 712u && u_const.ram == 1608u && u_str.flash == 800u && u_full.flash == 33516u);
    CHECK(fits(u_pf, small.flash_limit, small.ram_limit) && !fits(u_full, small.flash_limit, small.ram_limit));
    CHECK(fits(u_pf, 7496u, 2032u) && !fits(u_pf, 7495u, 2032u) && !fits(u_pf, 7496u, 2031u));
    CHECK(size_gate(u_init, u_const, &small) == GATE_OK);  /* 줄어듦: 728→712, 2120→1608 */
    CHECK(size_gate(u_const, u_str, &small) == GATE_OK);   /* +88 B */
    CHECK(size_gate(u_str, u_pf, &small) == (GATE_FLASH_GROWTH | GATE_RAM_GROWTH));
    CHECK(size_gate(u_pf, u_full, &small) == (GATE_FLASH_OVER | GATE_FLASH_GROWTH | GATE_RAM_GROWTH));
    char line[96];
    CHECK(format_usage(line, sizeof line, u_pf, &f4) > 0 &&
          strcmp(line, "Flash 7496/65536 B (11.4%), RAM 2032/16384 B (12.4%)") == 0);
    CHECK(format_usage(line, sizeof line, u_full, &small) > 0 &&
          strcmp(line, "Flash 33516/32768 B (102.3%), RAM 3704/4096 B (90.4%)") == 0);
    budget_t zero = {0u, 4096u, 0u, 0u};
    CHECK(format_usage(line, 10, u_pf, &f4) == -1 && format_usage(line, sizeof line, u_pf, &zero) == -1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
