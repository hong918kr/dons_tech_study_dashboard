/* BM-B07 Reset handler 구현 (호스트 시뮬레이션): .data 복사·.bss 제로화
 *
 * Part A — 워드 단위 crt0: copy_data_words / zero_bss_words / run_init_array / reset_init
 * Part B — 바이트 꼬리(tail) 변형: 정렬 안 된 시작·홀수 길이 (head 바이트 → 워드 본체 → tail 바이트)
 * Part C — 미니 펌웨어 부팅: 콜드 부트 / 웜 리셋 / constructor 가 .data 값을 읽는 순서
 *
 * 가짜 메모리: flash_img[] = Flash (LMA, 0x08000000 으로 표시), ram[] = SRAM (VMA, 0x20000000 으로 표시).
 * "링커 심볼" (_sidata, _sdata, _edata, _sbss, _ebss, __init_array_start/_end) = 이 배열 안을 가리키는
 * 포인터 묶음 crt_syms_t. 진짜 타깃 형태(extern 링커 심볼)는 demo_target_crt0.c 를 본다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   (-v: RAM 덤프)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 가짜 메모리 (하네스가 제공) ======================= */
#define FLASH_BASE  0x08000000u
#define RAM_BASE    0x20000000u
#define FLASH_WORDS 64u                 /* 256 B 짜리 장난감 Flash */
#define RAM_WORDS   32u                 /* 128 B 짜리 장난감 SRAM, _estack = 0x20000080 */
#define RAM_BYTES   (RAM_WORDS * 4u)

static uint32_t flash_img[FLASH_WORDS]; /* 실칩에선 const (읽기 전용) — 여기선 하네스가 "굽는다" */
static uint32_t ram[RAM_WORDS];

typedef void (*init_fn_t)(void);

/* 링커 스크립트가 만들어 주는 심볼 역할. 실제로는 extern uint32_t _sdata; 의 "주소" 들이다. */
typedef struct {
    const uint32_t *sidata;             /* .data 초기값이 Flash 에 놓인 곳 (LMA)  */
    uint32_t *sdata, *edata;            /* .data 가 RAM 에서 살 곳 (VMA) [sdata, edata) */
    uint32_t *sbss, *ebss;              /* .bss [sbss, ebss) */
    const init_fn_t *init_start, *init_end;   /* __init_array_start / __init_array_end */
} crt_syms_t;

/* ======================= Part A: 워드 단위 crt0 ======================= */
/* [dst, dst_end) 를 src 에서 4바이트씩 복사. 길이 0 (dst == dst_end) 이면 아무것도 안 한다. */
static void copy_data_words(uint32_t *dst, const uint32_t *dst_end, const uint32_t *src) {
    (void)dst; (void)dst_end; (void)src;   /* TODO: dst < dst_end 동안 *dst++ = *src++ */
}
/* [dst, dst_end) 를 0 으로. */
static void zero_bss_words(uint32_t *dst, const uint32_t *dst_end) {
    (void)dst; (void)dst_end;              /* TODO */
}
/* __init_array 를 앞에서부터 순서대로 한 번씩 호출. 빈 배열 (start == end) 이면 호출 0번. */
static void run_init_array(const init_fn_t *f, const init_fn_t *end) {
    (void)f; (void)end;                    /* TODO */
}
/* Reset_Handler 의 C 부분: 순서가 곧 정답 — .data 복사 → .bss 제로 → constructor. */
static void reset_init(const crt_syms_t *s) {
    (void)s; (void)copy_data_words; (void)zero_bss_words;   /* TODO: 세 단계를 올바른 순서로 */
}

/* ======================= Part B: 바이트 꼬리 변형 ======================= */
/* 링커 스크립트에 ALIGN(4) 가 빠져 경계가 4의 배수가 아닐 때를 위한 변형.
 * head: dst 가 4바이트 정렬될 때까지 바이트 → body: src 도 정렬돼 있으면 워드 → tail: 남은 바이트.
 * 반환값 = 워드로 옮긴 횟수 (테스트가 "본체를 정말 워드로 했나" 를 확인). */
static unsigned copy_bytes_tail(uint8_t *dst, const uint8_t *dst_end, const uint8_t *src) {
    (void)dst; (void)dst_end; (void)src;   /* TODO: head 바이트 → (src 도 정렬이면) 워드 → tail 바이트 */
    return 0;
}
static unsigned zero_bytes_tail(uint8_t *dst, const uint8_t *dst_end) {
    (void)dst; (void)dst_end;              /* TODO */
    return 0;
}

typedef struct {
    const uint8_t *sidata;
    uint8_t *sdata, *edata, *sbss, *ebss;
    const init_fn_t *init_start, *init_end;
} crt_bsyms_t;

static void reset_init_bytes(const crt_bsyms_t *s) {
    (void)s;                               /* TODO: Part A 와 같은 순서, 바이트 꼬리 함수로 */
}

/* ======================= Part C: 미니 펌웨어 (레이아웃은 주어진 것) ======================= */
/* RAM 워드 배치 — 링커가 정해 준 주소라고 생각한다.
 *   w3        canary_lo   0x2000000C
 *   w4..w8    .data       0x20000010 .. 0x20000024 (5 words)
 *   w9..w12   .bss        0x20000024 .. 0x20000034 (4 words)
 *   w13       canary_hi   0x20000034
 *   w14..w15  .noinit     0x20000038 (리셋을 넘어 살아남아야 함)
 *   w16..w31  heap/stack  (_estack = 0x20000080)
 * Flash: w0 = _estack, w1 = Reset_Handler|1, w16..w39 .text, w40..w44 = .data 초기값 (_sidata = 0x080000A0) */
enum { W_CANARY_LO = 3, W_SDATA = 4, W_EDATA = 9, W_SBSS = 9, W_EBSS = 13, W_CANARY_HI = 13,
       W_NOINIT = 14, F_SIDATA = 40, DATA_WORDS = W_EDATA - W_SDATA };
#define CANARY_LO  0xA5A5A5A5u
#define CANARY_HI  0x5A5A5A5Au
#define BOOT_MAGIC 0xB007C0DEu

#define G_BAUD        ram[4]            /* .data  uint32_t g_baud = 115200;       */
#define G_LED_PIN     ram[5]            /* .data  uint32_t g_led_pin = 5;         */
#define G_NAME        ((const char *)(const void *)&ram[6])   /* .data char g_name[8] = "BM-B07"; */
#define G_MODE        ram[8]            /* .data  uint32_t g_mode = 2;            */
#define G_CORE_HZ     ram[9]            /* .bss   uint32_t g_core_hz;             */
#define G_UART_DIV    ram[10]           /* .bss   uint32_t g_uart_div;            */
#define G_TICKS       ram[11]           /* .bss   uint32_t g_ticks;               */
#define G_RX_COUNT    ram[12]           /* .bss   uint32_t g_rx_count;            */
#define G_BOOT_MAGIC  ram[14]           /* .noinit                                 */
#define G_RESET_COUNT ram[15]           /* .noinit                                 */

/* constructor 들 (C++ 전역 객체 생성자 / __attribute__((constructor)) 역할) + 하네스 계측 */
static char g_log[32];
static unsigned g_log_n;
static uint32_t g_seen_baud, g_seen_core_hz;
static void log_ch(char c) { if (g_log_n + 1u < sizeof g_log) { g_log[g_log_n++] = c; g_log[g_log_n] = '\0'; } }
static void log_clear(void) { g_log_n = 0; g_log[0] = '\0'; }

static void ctor_clock(void) { log_ch('C'); G_CORE_HZ = 16000000u; }            /* .bss 에 쓴다 */
static void ctor_uart(void) {                                                     /* .data 를 읽는다 */
    uint32_t baud = G_BAUD;
    log_ch('U'); g_seen_baud = baud; g_seen_core_hz = G_CORE_HZ;
    G_UART_DIV = baud ? (G_CORE_HZ + baud / 2u) / baud : 0u;                     /* 16e6/115200 → 139 */
}
static void ctor_led(void) { log_ch('L'); }

static const init_fn_t FW_INIT[] = { ctor_clock, ctor_uart, ctor_led };
static const crt_syms_t FW_SYMS = {
    &flash_img[F_SIDATA], &ram[W_SDATA], &ram[W_EDATA], &ram[W_SBSS], &ram[W_EBSS],
    &FW_INIT[0], &FW_INIT[3],
};

/* main(): 부팅 직후 상태를 검사해 오류 비트를 돌려주고, 그 다음 "앱이 돌면서" 변수를 바꾼다. */
static uint32_t fw_main(void) {
    uint32_t err = 0;
    if (G_BAUD != 115200u || G_LED_PIN != 5u || G_MODE != 2u) err |= 1u;   /* .data 초기값 */
    if (memcmp(G_NAME, "BM-B07", 7) != 0)                       err |= 2u;   /* .data 문자열 */
    if (G_UART_DIV != 139u)                                     err |= 4u;   /* ctor 결과    */
    if (G_TICKS != 0u || G_RX_COUNT != 0u)                      err |= 8u;   /* .bss == 0   */
    if (G_BOOT_MAGIC != BOOT_MAGIC) { G_BOOT_MAGIC = BOOT_MAGIC; G_RESET_COUNT = 0u; }   /* 콜드 부트 */
    G_RESET_COUNT++;
    G_BAUD = 9600u; G_TICKS += 1234u; G_RX_COUNT = 7u;                       /* 앱 실행 흔적 */
    return err;
}

/* Reset_Handler 전체: 런타임 초기화 후 main. (실칩: main 이 돌아오면 무한 루프) */
static uint32_t fw_boot(void) {
    /* TODO: main 전에 할 일 */
    return fw_main();
}

/* ======================= 하네스: 전원·이미지·검증 ======================= */
static uint32_t g_rng = 0x2545F491u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* 전원 투입: SRAM 은 쓰레기 (mode 0 = 난수, 1 = 전부 0 — 디버거/시뮬레이터가 0 으로 채운 경우, 2 = 0xCC) */
static void sim_power_on(int mode) {
    for (unsigned i = 0; i < RAM_WORDS; i++)
        ram[i] = mode == 0 ? xs32() : mode == 1 ? 0u : 0xCCCCCCCCu;
}
static void flash_program_image(void) {
    for (unsigned i = 0; i < FLASH_WORDS; i++) flash_img[i] = 0xFFFFFFFFu;     /* 지워진 Flash */
    flash_img[0] = RAM_BASE + RAM_BYTES;                                       /* _estack */
    flash_img[1] = FLASH_BASE + 16u * 4u + 1u;                                 /* Reset_Handler | Thumb */
    for (unsigned i = 2; i < 16; i++) flash_img[i] = FLASH_BASE + 18u * 4u + 1u;   /* Default_Handler */
    for (unsigned i = 16; i < F_SIDATA; i++) flash_img[i] = 0xBF00BF00u;      /* .text (nop nop) */
    uint32_t img[DATA_WORDS] = { 115200u, 5u, 0u, 0u, 2u };
    memcpy(&img[2], "BM-B07\0", 8);                                            /* char g_name[8] */
    memcpy(&flash_img[F_SIDATA], img, sizeof img);                             /* .data LMA */
}
static void place_canaries(void) { ram[W_CANARY_LO] = CANARY_LO; ram[W_CANARY_HI] = CANARY_HI; }

/* 기대 RAM = 스냅샷 + [d_off, d_off+d_len) ← src, [b_off, b_off+b_len) ← 0. 다른 바이트 수를 센다. */
static unsigned ram_diff(const uint8_t *before, size_t d_off, size_t d_len, const uint8_t *src,
                         size_t b_off, size_t b_len) {
    uint8_t exp[RAM_BYTES];
    memcpy(exp, before, RAM_BYTES);
    if (d_len) memcpy(exp + d_off, src, d_len);
    if (b_len) memset(exp + b_off, 0, b_len);
    const uint8_t *now = (const uint8_t *)ram;
    unsigned bad = 0;
    for (size_t i = 0; i < RAM_BYTES; i++) bad += now[i] != exp[i];
    return bad;
}

/* --- 주어진 버그 변형들 (찾아내는 것이 테스트의 일) --- */
/* (1) 워드 루프 + ALIGN(4) 없는 경계: `for (p = &_sdata; p < &_edata;) *p++ = *s++;` 는 길이를 4의 배수로
 *     "올림" 해서 옮긴다 (GCC 가 실제로 ((end-1-start) & ~3) + 4 바이트를 계산 — 5. 풀이 asm). 바이트로 모델링. */
static void bug_reset_ceil(uint8_t *d, size_t dl, const uint8_t *s, uint8_t *b, size_t bl) {
    for (size_t i = 0; i < (dl + 3u) / 4u * 4u; i++) d[i] = s[i];
    for (size_t i = 0; i < (bl + 3u) / 4u * 4u; i++) b[i] = 0u;
}
/* (2) 워드 개수를 len/4 로 "내림" (count = (_edata - _sdata) / 4 를 바이트 길이로 계산한 경우) */
static void bug_reset_floor(uint8_t *d, size_t dl, const uint8_t *s, uint8_t *b, size_t bl) {
    for (size_t i = 0; i < dl / 4u * 4u; i++) d[i] = s[i];
    for (size_t i = 0; i < bl / 4u * 4u; i++) b[i] = 0u;
}
/* (3) constructor 를 먼저 부르는 Reset_Handler */
static uint32_t bug_boot_ctor_first(void) {
    run_init_array(FW_SYMS.init_start, FW_SYMS.init_end);
    for (unsigned i = 0; i < DATA_WORDS; i++) ram[W_SDATA + i] = flash_img[F_SIDATA + i];
    for (unsigned i = W_SBSS; i < W_EBSS; i++) ram[i] = 0u;
    return fw_main();
}
/* (4) .bss 제로화를 잊은 Reset_Handler */
static uint32_t bug_boot_no_bss(void) {
    for (unsigned i = 0; i < DATA_WORDS; i++) ram[W_SDATA + i] = flash_img[F_SIDATA + i];
    run_init_array(FW_SYMS.init_start, FW_SYMS.init_end);
    return fw_main();
}

/* Part A 용 probe constructor 들 */
static uint32_t g_probe_data, g_probe_bss;
static void probe_a(void) { log_ch('A'); }
static void probe_b(void) { log_ch('B'); }
static void probe_c(void) { log_ch('c'); }
static void probe_read(void) { log_ch('R'); g_probe_data = ram[W_SDATA]; g_probe_bss = ram[W_SBSS]; }

static void dump_ram(const char *title) {
    printf("%s\n", title);
    for (unsigned i = 2; i < 16; i++)
        printf("  0x%08X  w%-2u  0x%08X\n", (unsigned)(RAM_BASE + 4u * i), i, (unsigned)ram[i]);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(int argc, char **argv) {
    bool verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    uint8_t before[RAM_BYTES], fl_before[FLASH_WORDS * 4u];
    const uint8_t *flb = (const uint8_t *)flash_img;
    uint8_t *rb = (uint8_t *)ram;

    /* ---------- Part A: 워드 단위 reset_init ---------- */
    flash_program_image();
    memcpy(fl_before, flash_img, sizeof fl_before);
    sim_power_on(2); place_canaries();
    memcpy(before, ram, RAM_BYTES);
    {
        static const init_fn_t none[1] = { probe_a };
        crt_syms_t s = FW_SYMS; s.init_start = s.init_end = &none[0];          /* 빈 init_array */
        if (verbose) dump_ram("[A] before reset_init (0xCC = power-on garbage)");
        reset_init(&s);
        if (verbose) dump_ram("[A] after reset_init");
    }
    CHECK(ram_diff(before, W_SDATA * 4u, DATA_WORDS * 4u, flb + F_SIDATA * 4u, W_SBSS * 4u, 16u) == 0u);
    CHECK(G_BAUD == 115200u && G_LED_PIN == 5u && G_MODE == 2u);
    CHECK(memcmp(G_NAME, "BM-B07", 7) == 0);
    CHECK(G_CORE_HZ == 0u && G_UART_DIV == 0u && G_TICKS == 0u && G_RX_COUNT == 0u);
    CHECK(ram[W_CANARY_LO] == CANARY_LO && ram[W_CANARY_HI] == CANARY_HI);    /* 경계 바로 바깥 */
    CHECK(ram[W_NOINIT] == 0xCCCCCCCCu && ram[W_NOINIT + 1] == 0xCCCCCCCCu);  /* .noinit 손대지 않음 */
    CHECK(memcmp(fl_before, flash_img, sizeof fl_before) == 0);               /* Flash 는 읽기만 */

    /* 길이 0: .data 도 .bss 도 비어 있음 → RAM 한 바이트도 바뀌면 안 됨 */
    sim_power_on(0); memcpy(before, ram, RAM_BYTES);
    {
        crt_syms_t s = { &flash_img[F_SIDATA], &ram[4], &ram[4], &ram[4], &ram[4], &FW_INIT[0], &FW_INIT[0] };
        reset_init(&s);
    }
    CHECK(memcmp(before, ram, RAM_BYTES) == 0);
    /* .data 0 워드, .bss 3 워드 */
    sim_power_on(0); memcpy(before, ram, RAM_BYTES);
    {
        crt_syms_t s = { &flash_img[F_SIDATA], &ram[4], &ram[4], &ram[4], &ram[7], &FW_INIT[0], &FW_INIT[0] };
        reset_init(&s);
    }
    CHECK(ram_diff(before, 16u, 0u, flb, 16u, 12u) == 0u);

    /* 전수: .data 0..8 워드 × .bss 0..8 워드 (바로 뒤) × 시작 워드 2..4 × 난수 내용 */
    {
        unsigned fails = 0, layouts = 0;
        for (unsigned st = 2; st <= 4; st++)
            for (unsigned dw = 0; dw <= 8; dw++)
                for (unsigned bw = 0; bw <= 8; bw++) {
                    for (unsigned i = 0; i < 8; i++) flash_img[F_SIDATA + i] = xs32();
                    sim_power_on(0); memcpy(before, ram, RAM_BYTES);
                    crt_syms_t s = { &flash_img[F_SIDATA], &ram[st], &ram[st + dw], &ram[st + dw],
                                     &ram[st + dw + bw], &FW_INIT[0], &FW_INIT[0] };
                    reset_init(&s);
                    fails += ram_diff(before, st * 4u, dw * 4u, flb + F_SIDATA * 4u,
                                      (st + dw) * 4u, bw * 4u) != 0u;
                    layouts++;
                }
        if (verbose) printf("[A] exhaustive word layouts: %u, failing: %u\n", layouts, fails);
        CHECK(layouts == 243u && fails == 0u);
        flash_program_image();
    }

    /* init_array: 순서대로 한 번씩, 빈 배열은 0번 */
    {
        static const init_fn_t tab[] = { probe_a, probe_b, probe_c };
        log_clear(); run_init_array(&tab[0], &tab[3]);
        CHECK(strcmp(g_log, "ABc") == 0);
        log_clear(); run_init_array(&tab[0], &tab[0]);
        CHECK(g_log_n == 0u);
        log_clear(); run_init_array(&tab[2], &tab[3]);
        CHECK(strcmp(g_log, "c") == 0);
    }
    /* constructor 는 복사된 .data 와 0 이 된 .bss 를 봐야 한다 */
    {
        static const init_fn_t tab[] = { probe_read };
        crt_syms_t s = FW_SYMS; s.init_start = &tab[0]; s.init_end = &tab[1];
        sim_power_on(2); log_clear(); g_probe_data = g_probe_bss = 0xFFFFFFFFu;
        reset_init(&s);
        CHECK(strcmp(g_log, "R") == 0);
        CHECK(g_probe_data == 115200u && g_probe_bss == 0u);
    }

    /* ---------- Part B: 바이트 꼬리 변형 ---------- */
    /* 전수: data_len 0..13 × bss_len 0..13 (바이트 단위로 바로 뒤) × dst 정렬 0..3 × src 정렬 0..3 */
    {
        unsigned fails = 0, layouts = 0;
        for (unsigned i = 0; i < 8; i++) flash_img[F_SIDATA + i] = xs32();
        for (unsigned doff = 16; doff < 20; doff++)
            for (unsigned soff = 160; soff < 164; soff++)
                for (unsigned dl = 0; dl <= 13; dl++)
                    for (unsigned bl = 0; bl <= 13; bl++) {
                        sim_power_on(0); memcpy(before, ram, RAM_BYTES);
                        crt_bsyms_t s = { flb + soff, rb + doff, rb + doff + dl, rb + doff + dl,
                                          rb + doff + dl + bl, &FW_INIT[0], &FW_INIT[0] };
                        reset_init_bytes(&s);
                        fails += ram_diff(before, doff, dl, flb + soff, doff + dl, bl) != 0u;
                        layouts++;
                    }
        if (verbose) printf("[B] exhaustive byte layouts: %u, failing: %u\n", layouts, fails);
        CHECK(layouts == 3136u && fails == 0u);
        flash_program_image();
    }
    /* 홀수 길이 하나를 눈으로: .data 5 바이트 @16, .bss 3 바이트 @21 (정렬 안 된 시작), 바이트 24 는 남의 것 */
    sim_power_on(2); memcpy(before, ram, RAM_BYTES);
    {
        crt_bsyms_t s = { flb + 160, rb + 16, rb + 21, rb + 21, rb + 24, &FW_INIT[0], &FW_INIT[0] };
        reset_init_bytes(&s);
    }
    CHECK(rb[16] == flb[160] && rb[20] == flb[164] && rb[21] == 0u && rb[23] == 0u && rb[24] == 0xCCu);
    CHECK(ram_diff(before, 16u, 5u, flb + 160, 21u, 3u) == 0u);
    /* 본체를 정말 워드로 옮기나 (head/body/tail 분해) */
    CHECK(copy_bytes_tail(rb + 16, rb + 29, flb + 160) == 3u);   /* 정렬: 3워드 + 꼬리 1 */
    CHECK(copy_bytes_tail(rb + 17, rb + 30, flb + 161) == 2u);   /* head 3 + 2워드 + 꼬리 2 */
    CHECK(copy_bytes_tail(rb + 17, rb + 30, flb + 160) == 0u);   /* 정렬 어긋남 → 전부 바이트 */
    CHECK(copy_bytes_tail(rb + 16, rb + 16, flb + 160) == 0u);   /* 길이 0 */
    CHECK(zero_bytes_tail(rb + 17, rb + 31) == 2u);              /* head 3 + 2워드 + 꼬리 3 */
    CHECK(zero_bytes_tail(rb + 18, rb + 19) == 0u && rb[18] == 0u);
    /* 주어진 버그 모델을 검증기가 잡아내는가 (.data 5 B @16, .bss 5 B @21) */
    sim_power_on(2); memcpy(before, ram, RAM_BYTES);
    bug_reset_ceil(rb + 16, 5u, flb + 160, rb + 21, 5u);
    CHECK(ram_diff(before, 16u, 5u, flb + 160, 21u, 5u) == 3u);     /* ebss 뒤 26..28 을 0 으로 덮음 */
    CHECK(rb[26] == 0u && rb[28] == 0u && rb[29] == 0xCCu);
    sim_power_on(2); memcpy(before, ram, RAM_BYTES);
    bug_reset_floor(rb + 16, 5u, flb + 160, rb + 21, 5u);
    CHECK(ram_diff(before, 16u, 5u, flb + 160, 21u, 5u) == 2u);     /* .data 마지막·.bss 마지막 바이트 */
    CHECK(rb[20] == 0xCCu && rb[25] == 0xCCu);

    /* ---------- Part C: 미니 펌웨어 부팅 ---------- */
    flash_program_image();
    sim_power_on(0); place_canaries(); log_clear();
    CHECK(fw_boot() == 0u);                                     /* 콜드 부트, RAM 은 난수 쓰레기 */
    CHECK(strcmp(g_log, "CUL") == 0 && g_seen_baud == 115200u && G_UART_DIV == 139u);
    CHECK(G_RESET_COUNT == 1u);
    log_clear();
    CHECK(fw_boot() == 0u);                                     /* 웜 리셋: RAM 에 앱이 남긴 값 */
    CHECK(strcmp(g_log, "CUL") == 0 && g_seen_baud == 115200u);
    CHECK(G_RESET_COUNT == 2u && G_BOOT_MAGIC == BOOT_MAGIC);    /* .noinit 은 살아남는다 */
    CHECK(ram[W_CANARY_LO] == CANARY_LO && ram[W_CANARY_HI] == CANARY_HI);
    CHECK(memcmp(fl_before, flash_img, sizeof fl_before) == 0);

    /* 주어진 버그 부트들은 반드시 잡혀야 한다 */
    log_clear();
    uint32_t e1 = bug_boot_ctor_first();                         /* 앞 run 이 g_baud=9600 을 남김 */
    if (verbose) printf("[C] ctor-first : err=0x%X seen_baud=%u core_hz=%u div=%u\n",
                        (unsigned)e1, (unsigned)g_seen_baud, (unsigned)G_CORE_HZ, (unsigned)G_UART_DIV);
    CHECK((e1 & 4u) != 0u && g_seen_baud == 9600u && G_CORE_HZ == 0u);
    sim_power_on(1); place_canaries();                           /* RAM 이 우연히 전부 0 인 콜드 부트 */
    uint32_t e2 = bug_boot_no_bss();
    uint32_t e3 = bug_boot_no_bss();                             /* 이어서 웜 리셋 */
    if (verbose) printf("[C] no-bss     : zero-RAM cold err=0x%X, warm err=0x%X ticks=%u\n",
                        (unsigned)e2, (unsigned)e3, (unsigned)G_TICKS);
    CHECK(e2 == 0u);                                             /* 0 으로 채워진 RAM 이 버그를 숨긴다 */
    CHECK((e3 & 8u) != 0u);                                      /* 웜 리셋에서 드러난다 */
    {
        static const init_fn_t bad_order[] = { ctor_uart, ctor_clock, ctor_led };   /* 링크 순서가 바뀜 */
        crt_syms_t s = FW_SYMS; s.init_start = &bad_order[0]; s.init_end = &bad_order[3];
        sim_power_on(0); log_clear();
        reset_init(&s);
        uint32_t e4 = fw_main();
        if (verbose) printf("[C] bad order  : log=%s seen_core_hz=%u div=%u err=0x%X\n",
                            g_log, (unsigned)g_seen_core_hz, (unsigned)G_UART_DIV, (unsigned)e4);
        CHECK(strcmp(g_log, "UCL") == 0 && g_seen_core_hz == 0u && (e4 & 4u) != 0u);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
