/* C-B11 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum { R_UNKNOWN, R_TEXT, R_RODATA, R_DATA, R_BSS, R_HEAP, R_STACK } region_t;

/* 연습 1: 선언 형태만 보고 영역 예측 (규칙 엔진)
 * file_scope: 파일 스코프인가, is_static: 블록 안 static 인가, init_nonzero: 0 아닌 초기값이 있나 */
typedef enum { INIT_NONE, INIT_ZERO, INIT_NONZERO } init_t;
static region_t where_lives(bool file_scope, bool is_static, bool is_const, init_t init) {
    if (!file_scope && !is_static) return R_STACK;     /* 자동 저장 기간: const 여도 스택 */
    if (is_const) return R_RODATA;                     /* 정적 저장 기간 + const       */
    return init == INIT_NONZERO ? R_DATA : R_BSS;      /* 초기값 없음 = 0 초기화 = bss */
}

/* 연습 2: 스택은 어느 방향으로 자라나? (주소는 uintptr_t 로 바꿔 비교 — 서로 다른 객체의 < 비교는 UB) */
static bool (*volatile callee_fn)(uintptr_t);         /* volatile 함수 포인터: 인라인 방지 */
static bool callee_is_lower(uintptr_t caller_local) {
    volatile int x = 0;                               /* 지역 주소를 return 하면 -Wreturn-stack-address */
    return (uintptr_t)&x < caller_local;              /* 그래서 비교를 callee 안에서 한다 */
}
static bool stack_grows_down(void) {
    volatile int here = 0;
    callee_fn = callee_is_lower;
    return callee_fn((uintptr_t)&here);               /* 호출된 함수의 지역 변수가 더 낮은 주소? */
}

/* 연습 3: 스택 페인팅으로 최대 사용량(high-water mark) 측정 */
#define STACK_WORDS 64
#define PAINT 0xA5A5A5A5u
static uint32_t task_stack[STACK_WORDS];               /* [0] = 바닥(낮은 주소), [63] = 꼭대기 */
static void stack_paint(void) { for (int i = 0; i < STACK_WORDS; i++) task_stack[i] = PAINT; }
static void simulate_push(int words) {                 /* 꼭대기에서 아래로 words 개 사용 */
    for (int i = 0; i < words; i++) task_stack[STACK_WORDS - 1 - i] = (uint32_t)i;
}
static int stack_unused_words(void) {                  /* 바닥부터 페인트가 남은 워드 수 */
    int n = 0;
    while (n < STACK_WORDS && task_stack[n] == PAINT) n++;
    return n;
}

/* 연습 4: _sbrk 흉내 — 힙은 위로, 스택은 아래로. 부딪히면 실패 */
static uint8_t sram[1024];
static uint8_t *heap_end;                              /* 현재 힙 끝 (처음엔 _end) */
static uint8_t *fake_sp;                               /* 현재 스택 포인터 */
static void *sbrk_sim(size_t incr, size_t guard) {
    uint8_t *prev = heap_end;
    size_t gap = (size_t)(fake_sp - heap_end);         /* 둘 다 sram 안 → 포인터 뺄셈 OK */
    if (gap < incr + guard) return NULL;               /* 스택 가드 영역 침범 = 충돌 */
    heap_end += incr;
    return prev;
}

/* 연습 5: GNU size(Berkeley) 출력 → flash/RAM 사용률 (단위: 0.1 %) */
static void usage_permille(uint32_t text, uint32_t data, uint32_t bss,
                           uint32_t flash_total, uint32_t ram_total,
                           uint32_t *flash_pm, uint32_t *ram_pm) {
    *flash_pm = (uint32_t)((uint64_t)(text + data) * 1000u / flash_total);  /* text 에 rodata 포함됨 */
    *ram_pm   = (uint32_t)((uint64_t)(data + bss) * 1000u / ram_total);
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    CHECK(where_lives(true,  false, false, INIT_NONZERO) == R_DATA);    /* int g = 1;          */
    CHECK(where_lives(true,  false, false, INIT_NONE)    == R_BSS);     /* int g;              */
    CHECK(where_lives(true,  false, false, INIT_ZERO)    == R_BSS);     /* int g = 0;          */
    CHECK(where_lives(true,  false, true,  INIT_NONZERO) == R_RODATA);  /* const int g = 1;    */
    CHECK(where_lives(false, false, true,  INIT_NONZERO) == R_STACK);   /* { const int k = 1; } */
    CHECK(where_lives(false, true,  false, INIT_NONE)    == R_BSS);     /* { static int n; }   */
    CHECK(where_lives(false, true,  false, INIT_NONZERO) == R_DATA);    /* { static int n = 3; } */

    /* 연습 2 (arm64 macOS, Cortex-M 모두 full-descending 스택) */
    CHECK(stack_grows_down() == true);

    /* 연습 3 */
    stack_paint();
    CHECK(stack_unused_words() == 64);
    simulate_push(10);
    simulate_push(40);                 /* 가장 깊었던 순간 = 40 워드 */
    simulate_push(5);
    CHECK(stack_unused_words() == 24);  /* 64 - 40: 지금이 아니라 '최대' 사용량이 남는다 */

    /* 연습 4: sram[0..] 에서 힙 시작, 스택은 sram 끝에서 아래로 */
    heap_end = &sram[0];
    fake_sp  = &sram[1024 - 256];      /* 스택이 256 바이트 쓰는 중 */
    CHECK(sbrk_sim(512, 64) == &sram[0]);
    CHECK(sbrk_sim(256, 64) == NULL);  /* 남은 틈 768-512 = 256 < 256+64 → 실패 */
    CHECK(heap_end == &sram[512]);     /* 실패하면 heap_end 는 그대로 */
    CHECK(sbrk_sim(192, 64) == &sram[512]);   /* 정확히 맞으면 허용 */
    CHECK(sbrk_sim(1, 64) == NULL);           /* 가드 침범 → 실패 */

    /* 연습 5: arm-none-eabi-size 예: text=48212 data=1236 bss=20480, STM32F401(256K/64K) */
    uint32_t fpm = 0, rpm = 0;
    usage_permille(48212, 1236, 20480, 256u * 1024u, 64u * 1024u, &fpm, &rpm);
    CHECK(fpm == 188);                 /* 49448 / 262144 = 18.8 % */
    CHECK(rpm == 331);                 /* 21716 / 65536  = 33.1 % */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
