/* BM-B07 추가 연습 — 한 번에 검증
 *
 * 연습 1 — 표(table) 기반 초기화: CMSIS GCC startup 의 __copy_table / __zero_table 방식으로
 *          SRAM .data + CCM .ccmdata 두 곳을 복사하고 .bss + .ccmbss 두 곳을 0 으로.
 * 연습 2 — 링커 심볼 값 검사기: map 파일/nm 에서 읽은 주소로 ALIGN·순서·겹침·범위·스택 여유를 검사
 *          (링커 스크립트 ASSERT() 로 옮길 규칙들).
 * 연습 3 — newlib __libc_init_array / __libc_fini_array 순서 + constructor priority 정렬.
 * 연습 4 — .noinit 웜 부트 카운터: reset_init 가 건드리지 않는 영역, 매직 + 반전 매직.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x1234ABCDu;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

typedef void (*init_fn_t)(void);
static char g_log[64];
static unsigned g_log_n;
static void log_ch(char c) { if (g_log_n + 1u < sizeof g_log) { g_log[g_log_n++] = c; g_log[g_log_n] = '\0'; } }
static void log_clear(void) { g_log_n = 0; g_log[0] = '\0'; }

/* ======================= 연습 1: copy/zero 표 ======================= */
/* CMSIS 5 cmsis_gcc.h 의 __cmsis_start() 와 같은 모양 (확인 권장): wlen 은 "워드" 개수 */
typedef struct { const uint32_t *src; uint32_t *dest; uint32_t wlen; } copy_table_t;
typedef struct { uint32_t *dest; uint32_t wlen; } zero_table_t;

static void init_from_tables(const copy_table_t *c, const copy_table_t *c_end,
                             const zero_table_t *z, const zero_table_t *z_end) {
    for (; c < c_end; c++)
        for (uint32_t i = 0; i < c->wlen; i++) c->dest[i] = c->src[i];
    for (; z < z_end; z++)
        for (uint32_t i = 0; i < z->wlen; i++) z->dest[i] = 0u;
}

static uint32_t flash1[32];              /* .data 초기값 @0 (3 워드), .ccmdata 초기값 @8 (2 워드) */
static uint32_t sram1[16];               /* 0x20000000: .data w2..w4, .bss w5..w9 */
static uint32_t ccm[8];                  /* 0x10000000 (STM32F405/407 CCM, 확인 권장): .ccmdata w0..1, .ccmbss w2..4 */

static void ex1(void) {
    for (unsigned i = 0; i < 32; i++) flash1[i] = 0x1000u + i;
    for (unsigned i = 0; i < 16; i++) sram1[i] = 0xCCCCCCCCu;
    for (unsigned i = 0; i < 8; i++) ccm[i] = 0xCCCCCCCCu;
    const copy_table_t ct[2] = { { &flash1[0], &sram1[2], 3u }, { &flash1[8], &ccm[0], 2u } };
    const zero_table_t zt[2] = { { &sram1[5], 5u }, { &ccm[2], 3u } };
    init_from_tables(&ct[0], &ct[2], &zt[0], &zt[2]);
    CHECK(sram1[2] == 0x1000u && sram1[4] == 0x1002u);                        /* .data */
    CHECK(sram1[5] == 0u && sram1[9] == 0u && sram1[10] == 0xCCCCCCCCu);      /* .bss + 바깥 */
    CHECK(sram1[1] == 0xCCCCCCCCu);
    CHECK(ccm[0] == 0x1008u && ccm[1] == 0x1009u);                            /* .ccmdata */
    CHECK(ccm[2] == 0u && ccm[4] == 0u && ccm[5] == 0xCCCCCCCCu);             /* .ccmbss + 바깥 */
    /* 빈 표 (copy 0 항목, zero 항목의 wlen = 0) → 아무것도 안 바뀜 */
    uint32_t snap[16]; memcpy(snap, sram1, sizeof snap);
    const zero_table_t z0[1] = { { &sram1[0], 0u } };
    init_from_tables(&ct[0], &ct[0], &z0[0], &z0[1]);
    CHECK(memcmp(snap, sram1, sizeof snap) == 0);
}

/* ======================= 연습 2: 심볼 값 검사기 ======================= */
typedef struct { uint32_t sidata, sdata, edata, sbss, ebss, estack; } sym_vals_t;
typedef struct { uint32_t flash_lo, flash_hi, ram_lo, ram_hi, min_stack; } mem_cfg_t;
typedef enum { LC_OK, LC_ALIGN, LC_ORDER, LC_OVERLAP, LC_RAM_RANGE, LC_FLASH_RANGE, LC_STACK } lc_err_t;

static lc_err_t layout_check(const sym_vals_t *s, const mem_cfg_t *m) {
    if ((s->sidata | s->sdata | s->edata | s->sbss | s->ebss) & 3u) return LC_ALIGN;
    if (s->sdata > s->edata || s->sbss > s->ebss)                     return LC_ORDER;
    if (s->edata > s->sbss)                                           return LC_OVERLAP;
    if (s->sdata < m->ram_lo || s->ebss > m->ram_hi)                  return LC_RAM_RANGE;
    uint32_t dlen = s->edata - s->sdata;
    if (s->sidata < m->flash_lo || s->sidata > m->flash_hi || dlen > m->flash_hi - s->sidata)
        return LC_FLASH_RANGE;                                        /* 덧셈 오버플로 없이 비교 */
    if (s->estack != m->ram_hi || s->estack - s->ebss < m->min_stack) return LC_STACK;
    return LC_OK;
}

static void ex2(void) {
    const mem_cfg_t m = { 0x08000000u, 0x08100000u, 0x20000000u, 0x20020000u, 0x1000u };   /* 1 MB / 128 KB */
    const sym_vals_t good = { 0x08001A3Cu, 0x20000000u, 0x20000110u, 0x20000110u, 0x20000A40u, 0x20020000u };
    sym_vals_t s;
    CHECK(layout_check(&good, &m) == LC_OK);
    s = good; s.edata = 0x2000010Fu; s.sbss = 0x2000010Fu;  CHECK(layout_check(&s, &m) == LC_ALIGN);
    s = good; s.sbss = 0x2000010Cu;                          CHECK(layout_check(&s, &m) == LC_OVERLAP);
    s = good; s.ebss = 0x20000100u;                          CHECK(layout_check(&s, &m) == LC_ORDER);
    s = good; s.ebss = 0x20020010u;                          CHECK(layout_check(&s, &m) == LC_RAM_RANGE);
    s = good; s.sidata = 0x080FFF80u;                        CHECK(layout_check(&s, &m) == LC_FLASH_RANGE);
    s = good; s.sidata = 0xFFFFFFF0u;                        CHECK(layout_check(&s, &m) == LC_FLASH_RANGE);
    s = good; s.ebss = 0x2001F800u;                          CHECK(layout_check(&s, &m) == LC_STACK);
    s = good; s.sdata = s.edata = s.sbss = s.ebss = 0x20000000u;   CHECK(layout_check(&s, &m) == LC_OK);
}

/* ======================= 연습 3: init/fini 순서와 priority ======================= */
typedef struct { char name; unsigned prio; } ctor_t;     /* prio 0 = 우선순위 없음 */

/* ld 기본 스크립트: KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) 다음에 KEEP(*(.init_array)).
 * → 우선순위 있는 것 오름차순(작을수록 먼저), 그 다음 없는 것은 링크 순서. 삽입 정렬 = 안정. */
static size_t link_init_array(const ctor_t *in, size_t n, char *out) {
    size_t k = 0;
    ctor_t tmp[16];
    for (size_t i = 0; i < n && i < 16; i++) if (in[i].prio) {
        size_t j = k;
        while (j > 0 && tmp[j - 1].prio > in[i].prio) { tmp[j] = tmp[j - 1]; j--; }
        tmp[j] = in[i]; k++;
    }
    for (size_t i = 0; i < k; i++) out[i] = tmp[i].name;
    for (size_t i = 0; i < n && k < 16; i++) if (!in[i].prio) out[k++] = in[i].name;
    out[k] = '\0';
    return k;
}

/* newlib libc/misc/init.c, fini.c 모양: preinit → _init → init_array, fini_array 는 역순 → _fini */
static void libc_init_array(const char *pre, const char *init) {
    for (const char *p = pre; *p; p++) log_ch(*p);
    log_ch('I');                                            /* _init() (crti/crtn) */
    for (const char *p = init; *p; p++) log_ch(*p);
}
static void libc_fini_array(const char *fini) {
    for (size_t n = strlen(fini); n > 0; n--) log_ch(fini[n - 1]);
    log_ch('F');                                            /* _fini() */
}

static void ex3(void) {
    const ctor_t in[] = { {'a', 0}, {'b', 200}, {'c', 101}, {'d', 0}, {'e', 65535} };
    char order[17];
    CHECK(link_init_array(in, 5, order) == 5u && strcmp(order, "cbead") == 0);
    log_clear();
    libc_init_array("p", order);
    log_ch('M');                                            /* main */
    libc_fini_array("xy");                                  /* atexit / 전역 소멸자 */
    CHECK(strcmp(g_log, "pIcbeadMyxF") == 0);
    const ctor_t none[] = { {'q', 0}, {'r', 0} };
    CHECK(link_init_array(none, 2, order) == 2u && strcmp(order, "qr") == 0);
    CHECK(link_init_array(in, 0, order) == 0u && order[0] == '\0');
}

/* ======================= 연습 4: .noinit 웜 부트 카운터 ======================= */
static uint32_t flash4[8];
static uint32_t ram4[16];   /* w0..1 .data, w2..3 .bss, w4..7 .noinit {magic, ~magic, boots, warm} */
#define NI_MAGIC 0x5EB0071Eu
enum { BOOT_COLD, BOOT_WARM };

static void reset_init4(uint32_t *ebss) {                  /* B07 의 워드 crt0 축약판 */
    const uint32_t *src = &flash4[0];
    for (uint32_t *d = &ram4[0]; d < &ram4[2]; ) *d++ = *src++;
    for (uint32_t *d = &ram4[2]; d < ebss; ) *d++ = 0u;
}
static int boot_classify(uint32_t *ni) {
    if (ni[0] == NI_MAGIC && ni[1] == ~NI_MAGIC) { ni[2]++; ni[3]++; return BOOT_WARM; }
    ni[0] = NI_MAGIC; ni[1] = ~NI_MAGIC; ni[2] = 1u; ni[3] = 0u;
    return BOOT_COLD;
}

static void ex4(void) {
    flash4[0] = 7u; flash4[1] = 8u;
    for (unsigned i = 0; i < 16; i++) ram4[i] = xs32();                      /* 전원 투입 */
    reset_init4(&ram4[4]);   CHECK(boot_classify(&ram4[4]) == BOOT_COLD && ram4[6] == 1u);
    ram4[2] = 99u;                                                           /* 앱이 .bss 를 씀 */
    reset_init4(&ram4[4]);   CHECK(boot_classify(&ram4[4]) == BOOT_WARM && ram4[6] == 2u && ram4[2] == 0u);
    reset_init4(&ram4[4]);   CHECK(boot_classify(&ram4[4]) == BOOT_WARM && ram4[6] == 3u && ram4[7] == 2u);
    for (unsigned i = 0; i < 16; i++) ram4[i] = xs32();                      /* 전원 재투입 */
    reset_init4(&ram4[4]);   CHECK(boot_classify(&ram4[4]) == BOOT_COLD && ram4[6] == 1u);
    /* 버그: .noinit 이 .bss 범위 안으로 들어감 (NOLOAD 섹션을 .bss 앞에 둠) → 늘 콜드 */
    reset_init4(&ram4[8]);   CHECK(boot_classify(&ram4[4]) == BOOT_COLD);
    reset_init4(&ram4[8]);   CHECK(boot_classify(&ram4[4]) == BOOT_COLD && ram4[6] == 1u);
}

int main(void) {
    ex1(); ex2(); ex3(); ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
