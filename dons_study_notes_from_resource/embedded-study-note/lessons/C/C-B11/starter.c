/* C-B11 변수는 어디에 사는가: text data bss rodata stack heap
 * Part A = 순수 함수(가짜 STM32 메모리 맵), Part B = macOS(arm64, Mach-O) 실제 주소, Part C = Cortex-M 스타트업 흉내
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>          /* pthread_get_stackaddr_np / pthread_get_stacksize_np (macOS) */
#include <malloc/malloc.h>    /* malloc_zone_from_ptr (macOS)                                */
#include <mach-o/getsect.h>   /* getsectiondata: ASLR 슬라이드가 반영된 섹션 시작 주소        */
#include <mach-o/ldsyms.h>    /* _mh_execute_header: 실행 파일 자신의 Mach-O 헤더            */

typedef enum { R_UNKNOWN, R_TEXT, R_RODATA, R_DATA, R_BSS, R_HEAP, R_STACK } region_t;
static const char *const region_name[] = { "?", "text", "rodata", "data", "bss", "heap", "stack" };

typedef struct { uintptr_t lo, hi; region_t r; } range_t;   /* [lo, hi) — hi 는 "끝 다음" */
typedef struct { range_t v[16]; int n; } memmap_t;

static void map_add(memmap_t *m, uintptr_t lo, size_t size, region_t r) {
    if (m->n < 16) m->v[m->n++] = (range_t){ lo, lo + size, r };
}

/* ---------------- Part A: 주소 → 영역 분류 (순수 함수) ---------------- */
static region_t classify(const memmap_t *m, uintptr_t a) {
    (void)m; (void)a;
    /* TODO: m->v[] 를 돌며 lo <= a < hi 인 범위의 영역을 돌려준다 */
    /* (경계: lo 포함, hi 제외. size 0 범위는 어떤 주소와도 안 맞아야 함) */
    return R_UNKNOWN;
}

/* ---------------- Part B: macOS 실행 파일의 실제 섹션으로 맵 만들기 ---------------- */
static void add_sect(memmap_t *m, const char *seg, const char *sect, region_t r) {
    unsigned long size = 0;
    uint8_t *p = getsectiondata(&_mh_execute_header, seg, sect, &size);
    if (p) map_add(m, (uintptr_t)p, size, r);   /* 섹션이 없으면 NULL (size 는 믿지 말 것) */
}

static int build_host_map(memmap_t *m) {
    m->n = 0;
    (void)add_sect;
    /* TODO 1: 어떤 Mach-O 섹션이 어떤 영역인가? add_sect(m, "__TEXT", "__text", R_TEXT); 처럼 등록.
     *         후보: __TEXT,{__text,__const,__cstring,__asan_cstring}  __DATA_CONST,__const
     *               __DATA,{__data,__bss,__common}   (size -m -l ./solution 으로 직접 확인) */
    /* TODO 2: 메인 스레드 스택 범위 [top - size, top) 를 R_STACK 으로 등록.
     *         pthread_get_stackaddr_np() 는 스택 '꼭대기'(높은 주소)를 준다 */


    return m->n;
}

static region_t classify_host(const memmap_t *m, const void *p) {
    (void)m; (void)p;
    /* TODO: classify() 로 먼저 찾고, 못 찾았는데 malloc_zone_from_ptr(p) 가 NULL 이 아니면 heap */

    return R_UNKNOWN;
}

/* ---------------- Part C: Cortex-M Reset_Handler 흉내 ---------------- */
/* 링커 스크립트가 주는 심볼: _sidata(.data 초기값의 flash 주소 = LMA),
 * _sdata/_edata(.data 의 RAM 주소 = VMA), _sbss/_ebss(.bss 의 RAM 범위) */
static void reset_init(const uint32_t *sidata, uint32_t *sdata, uint32_t *edata,
                       uint32_t *sbss, uint32_t *ebss) {
    (void)sidata; (void)sdata; (void)edata; (void)sbss; (void)ebss;
    /* TODO: ① .data 초기값을 flash → RAM 으로 복사  ② .bss 를 0 으로 */
}

/* arm-none-eabi-size 의 text/data/bss 숫자 → 실제 flash/RAM 사용량 */
static void mem_usage(uint32_t text, uint32_t rodata, uint32_t data, uint32_t bss,
                      uint32_t *flash, uint32_t *ram) {
    (void)text; (void)rodata; (void)data; (void)bss;
    *flash = 0; *ram = 0;            /* TODO: 어느 섹션이 flash 를, 어느 섹션이 RAM 을 먹는가? */
}

/* ---------------- Part B 관찰 대상: 선언 형태별 변수 ---------------- */
int g_init = 42;                      /* 0 아닌 초기값 전역         */
int g_zero = 0;                       /* 0 으로 초기화한 전역       */
int g_uninit;                         /* 초기값 없는 전역 (tentative definition) */
const int g_const = 7;                /* const 전역                 */
const uint8_t g_lut[256] = { 1, 2, 3 };   /* const 테이블          */
static int s_file = 5;                /* 파일 스코프 static, 초기값 */
static int s_zero;                    /* 파일 스코프 static, 초기값 없음 */
const char *g_msg = "hello";          /* 포인터 변수는 data, 가리키는 문자열은 rodata */
const char *const g_names[] = { "idle", "run" };   /* 포인터까지 const → 재배치 후 읽기 전용 */

static int *counter_addr(void) { static int calls;  calls++; return &calls; }   /* 함수 안 static */
static int *seed_addr(void)    { static int seed = 1234; return &seed; }

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void show(const memmap_t *m, const char *what, const void *p) {
    printf("  %-28s %18p  %s\n", what, p, region_name[classify_host(m, p)]);
}

int main(void) {
    /* Part A: 가짜 STM32F4 맵 (FLASH 0x0800_0000, SRAM 0x2000_0000, 128 KB) */
    memmap_t mcu = { .n = 0 };
    map_add(&mcu, 0x08000000u, 0x3000, R_TEXT);     /* .isr_vector + .text   */
    map_add(&mcu, 0x08003000u, 0x0800, R_RODATA);   /* .rodata               */
    map_add(&mcu, 0x20000000u, 0x0100, R_DATA);     /* _sdata .. _edata      */
    map_add(&mcu, 0x20000100u, 0x0300, R_BSS);      /* _sbss  .. _ebss       */
    map_add(&mcu, 0x20000400u, 0x1000, R_HEAP);     /* _end   .. (힙 한도)   */
    map_add(&mcu, 0x2001E000u, 0x2000, R_STACK);    /* _estack = 0x2002_0000 */
    map_add(&mcu, 0x08003800u, 0, R_DATA);          /* size 0 범위: 아무것도 안 맞아야 함 */
    CHECK(classify(&mcu, 0x08000000u) == R_TEXT);     /* 첫 바이트 (lo 포함)      */
    CHECK(classify(&mcu, 0x08002FFFu) == R_TEXT);     /* 마지막 바이트            */
    CHECK(classify(&mcu, 0x08003000u) == R_RODATA);   /* hi 는 제외 → 다음 영역   */
    CHECK(classify(&mcu, 0x08003800u) == R_UNKNOWN);  /* _sidata: .data 초기값 이미지 (변수의 집 아님) */
    CHECK(classify(&mcu, 0x200000FCu) == R_DATA);
    CHECK(classify(&mcu, 0x20000100u) == R_BSS);      /* _edata == _sbss 경계     */
    CHECK(classify(&mcu, 0x20000800u) == R_HEAP);
    CHECK(classify(&mcu, 0x2001FFFCu) == R_STACK);    /* 스택 맨 위 워드          */
    CHECK(classify(&mcu, 0x20020000u) == R_UNKNOWN);  /* _estack 자체는 RAM 밖    */
    CHECK(classify(&mcu, 0x00000000u) == R_UNKNOWN);  /* NULL                     */

    /* Part B: 실제 macOS 주소 (실행마다 ASLR 로 주소는 바뀌지만 영역은 같아야 함) */
    memmap_t host;
    CHECK(build_host_map(&host) >= 5);
    int local = 1;
    const int local_k = 5;                 /* const 지역 변수: 그래도 스택! */
    static const int local_tbl[4] = { 1, 2, 3, 4 };   /* static const 지역 → rodata */
    char local_arr[] = "hi";              /* 문자열 리터럴을 스택 배열로 '복사' */
    const char *local_ptr = "hi";          /* 포인터만 스택, 내용은 rodata */
    uint8_t *heap = malloc(32);
    if (!heap) return 1;
    struct { const char *what; const void *p; region_t want; } t[] = {
        { "main (function)",          (const void *)(uintptr_t)main, R_TEXT },
        { "\"literal\" (string lit)",  "literal",                     R_RODATA },
        { "g_const",                  &g_const,                      R_RODATA },
        { "g_lut[256] (const)",       g_lut,                         R_RODATA },
        { "g_names[] (const ptr[])",  g_names,                       R_RODATA },
        { "local_tbl (static const)", local_tbl,                     R_RODATA },
        { "g_init = 42",              &g_init,                       R_DATA },
        { "s_file = 5 (static)",      &s_file,                       R_DATA },
        { "&g_msg (the pointer)",     (const void *)&g_msg,          R_DATA },
        { "g_msg (points to)",        g_msg,                         R_RODATA },
        { "seed = 1234 (fn static)",  seed_addr(),                   R_DATA },
        { "g_zero = 0",               &g_zero,                       R_BSS },
        { "g_uninit",                 &g_uninit,                     R_BSS },
        { "s_zero (static)",          &s_zero,                       R_BSS },
        { "calls (fn static)",        counter_addr(),                R_BSS },
        { "local",                    &local,                        R_STACK },
        { "local_k (const local)",    &local_k,                      R_STACK },
        { "local_arr[] = \"hi\"",     local_arr,                     R_STACK },
        { "&local_ptr",               (const void *)&local_ptr,      R_STACK },
        { "local_ptr (points to)",    local_ptr,                     R_RODATA },
        { "malloc(32)",               heap,                          R_HEAP },
    };
    printf("  %-28s %18s  %s\n", "object", "address", "region");
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        show(&host, t[i].what, t[i].p);
        CHECK(classify_host(&host, t[i].p) == t[i].want);
    }
    free(heap);

    /* Part C: 가짜 flash/RAM 으로 Reset_Handler 실행 */
    uint32_t flash[48], ram[16];
    for (unsigned i = 0; i < 48; i++) flash[i] = 0xF1A50000u + i;    /* .text/.rodata 자리 채움 */
    const uint32_t data_img[4] = { 42, 0x12345678u, 5, 1234 };       /* .data 초기값 (LMA)    */
    memcpy(&flash[40], data_img, sizeof data_img);
    for (unsigned i = 0; i < 16; i++) ram[i] = 0xDEADBEEFu;          /* 전원 켠 직후 쓰레기   */
    /* _sidata=&flash[40], _sdata=&ram[0], _edata=_sbss=&ram[4], _ebss=&ram[10] */
    reset_init(&flash[40], &ram[0], &ram[4], &ram[4], &ram[10]);
    CHECK(memcmp(ram, data_img, sizeof data_img) == 0);              /* .data 복사됨          */
    CHECK(ram[4] == 0 && ram[9] == 0);                               /* .bss 0                */
    CHECK(ram[10] == 0xDEADBEEFu && ram[15] == 0xDEADBEEFu);         /* 스택 영역은 안 건드림 */
    CHECK(flash[40] == 42);                                          /* flash 원본 그대로     */
    reset_init(&flash[40], &ram[12], &ram[12], &ram[12], &ram[12]);  /* 빈 .data/.bss        */
    CHECK(ram[12] == 0xDEADBEEFu);

    uint32_t fl = 0, rm = 0;
    mem_usage(12000, 2048, 256, 768, &fl, &rm);
    CHECK(fl == 14304 && rm == 1024);
    mem_usage(12000, 2048, 256 + 65536, 768, &fl, &rm);              /* 64 KB 테이블에 const 를 빼먹음 */
    CHECK(fl == 14304 + 65536 && rm == 1024 + 65536);                /* flash 도 RAM 도 64 KB 증가 */
    mem_usage(12000, 2048 + 65536, 256, 768, &fl, &rm);              /* const 붙임 → rodata   */
    CHECK(fl == 14304 + 65536 && rm == 1024);                        /* RAM 증가 0            */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
