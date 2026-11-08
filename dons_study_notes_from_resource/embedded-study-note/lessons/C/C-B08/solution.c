/* C-B08 const 정확히 쓰기: const T* vs T* const
 *
 * Part A: const 위치 4조합 선언       (타입은 _Generic 으로 컴파일 타임에 판정 → 런타임 PASS/FAIL)
 * Part B: 이중 포인터와 const          (const char *const * 를 받는 문자열 테이블 함수)
 * Part C: const-correct UART 드라이버  (const 입력 버퍼, const volatile 상태 레지스터, const 룩업 테이블)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 식 e 의 타입이 정확히 T 인가? _Generic 은 e 를 평가하지 않고 타입만 본다. */
#define IS_TYPE(e, T) _Generic((e), T: 1, default: 0)

/* ---------------- Part A: const 위치 4조합 ---------------- */
static int a = 1, b = 2;

/* TODO: 주석대로 const 를 붙여라 (초기값 &a 는 그대로 둔다) */
static int *p_plain             = &a;   /* ① 가리키는 값도, 포인터도 바꿀 수 있다        */
static const int *p_ro          = &a;   /* ② 가리키는 값은 읽기 전용 (pointer to const)  */
static int *const p_fixed       = &a;   /* ③ 포인터 자체가 고정     (const pointer)      */
static const int *const p_both  = &a;   /* ④ 둘 다 고정                                  */

/* ---------------- Part B: 이중 포인터와 const ---------------- */
/* TODO: 포인터 배열 자체도 바뀌면 안 된다 (문자열도, 슬롯도 읽기 전용) */
static const char *const k_cmds[] = { "reset", "status", "write" };

/* v[0..n-1] 중 key 와 같은 문자열의 인덱스, 없으면 -1. 아무것도 수정하지 않는다. */
static int str_index(const char *const *v, size_t n, const char *key) {
    for (size_t i = 0; i < n; i++)
        if (strcmp(v[i], key) == 0) return (int)i;
    return -1;
}

/* s 의 앞쪽 공백(' ', '\t')을 건너뛴 위치를 돌려준다. 입력이 const 면 결과도 const. */
static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;   /* s 는 const char * — 포인터 자체는 움직여도 된다 */
    return s;
}

/* 사실 확인용: 매개변수의 "맨 바깥" const 는 함수 타입에 포함되지 않는다 (C11 6.7.6.3p15) */
static int deref(int *const p) { return *p; }

/* ---------------- Part C: const-correct UART 드라이버 ---------------- */
#define UART_SR_TXE (1u << 7)   /* 송신 데이터 레지스터 비었음 */

typedef struct {
    const volatile uint32_t SR;   /* 상태 레지스터 — 하드웨어만 바꾼다. 소프트웨어는 읽기 전용 */
    volatile uint32_t DR;   /* 데이터 레지스터 (읽기/쓰기) */
    volatile uint32_t BRR;  /* 보레이트 레지스터 (읽기/쓰기) */
} UART_TypeDef;

/* 테스트용 "하드웨어": hw 쪽으로 상태를 바꾸고, 드라이버는 regs 쪽으로 본다.
   실제 칩이라면 C 객체가 없고 그냥 주소다: (UART_TypeDef *)0x40011000u */
typedef union {
    struct { uint32_t SR, DR, BRR; } hw;
    UART_TypeDef regs;
} uart_sim_t;
static uart_sim_t sim;

/* TODO: UART1 포인터 자체가 다른 곳을 가리키도록 바뀌면 안 된다 */
static UART_TypeDef *const UART1 = &sim.regs;

/* CRC-8 (poly 0x07) 니블 테이블. TODO: 절대 바뀌지 않는 표 → 플래시(.rodata)에 두고 싶다 */
static const uint8_t crc8_nib[16] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
};

/* TODO(시그니처): u 를 읽기만 한다 */
static bool uart_tx_ready(const UART_TypeDef *u) {
    return (u->SR & UART_SR_TXE) != 0;
}

/* TODO(시그니처): buf 는 읽기만 한다. 보낸 바이트 수를 돌려준다 (TXE 가 안 서면 중단) */
static size_t uart_write(UART_TypeDef *u, const uint8_t *buf, size_t len) {
    size_t sent = 0;
    for (; sent < len; sent++) {
        unsigned spins = 0;
        while (!uart_tx_ready(u)) {
            if (++spins > 1000u) return sent;   /* 타임아웃 */
        }
        u->DR = buf[sent];
    }
    return sent;
}

/* TODO(시그니처): buf 는 읽기만 한다 — const 를 붙이면 컴파일러가 숨은 버그를 찾아준다 */
static uint8_t crc8(const uint8_t *buf, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = (uint8_t)(crc ^ buf[i]);  /* 입력은 읽기만 — 작업은 지역 변수 crc 에서 */
        crc = (uint8_t)((crc << 4) ^ crc8_nib[crc >> 4]);
        crc = (uint8_t)((crc << 4) ^ crc8_nib[crc >> 4]);
    }
    return crc;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* Part A — 타입 판정 (&p 의 타입에는 p 자신의 const 도 드러난다) */
    CHECK(IS_TYPE(&p_plain, int **));
    CHECK(IS_TYPE(&p_ro,    const int **));
    CHECK(IS_TYPE(&p_fixed, int *const *));
    CHECK(IS_TYPE(&p_both,  const int *const *));
    CHECK(*p_plain + *p_ro + *p_fixed + *p_both == 4);
    p_plain = &b;
    CHECK(*p_plain == 2);

    /* Part B */
    CHECK(IS_TYPE(&k_cmds, const char *const (*)[3]));
    CHECK(str_index(k_cmds, 3, "status") == 1);
    CHECK(str_index(k_cmds, 3, "reset") == 0);
    CHECK(str_index(k_cmds, 3, "erase") == -1);
    CHECK(str_index(k_cmds, 0, "reset") == -1);
    char *argv_like[] = { "prog", "-v", "file" };     /* main 의 argv 와 같은 char ** */
    CHECK(str_index((const char *const *)argv_like, 3, "file") == 2);   /* 캐스트가 필요하다 */
    CHECK(strcmp(skip_ws("  \thello"), "hello") == 0);
    CHECK(*skip_ws("   ") == '\0');
    CHECK(IS_TYPE(&skip_ws, const char *(*)(const char *)));
    CHECK(IS_TYPE(&deref, int (*)(int *)));            /* int *const p 와 int *p 는 같은 함수 타입 */
    CHECK(deref(&a) == 1);

    /* Part C — 타입 판정 */
    CHECK(IS_TYPE(&UART1->SR, const volatile uint32_t *));
    CHECK(IS_TYPE(&UART1->DR, volatile uint32_t *));
    CHECK(IS_TYPE(&UART1, UART_TypeDef *const *));
    CHECK(IS_TYPE(&crc8_nib, const uint8_t (*)[16]));
    CHECK(IS_TYPE(&uart_tx_ready, bool (*)(const UART_TypeDef *)));
    CHECK(IS_TYPE(&uart_write, size_t (*)(UART_TypeDef *, const uint8_t *, size_t)));
    CHECK(IS_TYPE(&crc8, uint8_t (*)(const uint8_t *, size_t)));

    /* Part C — 동작 */
    uint8_t msg[] = { 'h', 'i', '!' };
    sim.hw.SR = UART_SR_TXE;                          /* 하드웨어: 송신 가능 */
    CHECK(uart_write(UART1, msg, sizeof msg) == 3);
    CHECK(sim.hw.DR == '!');                          /* 마지막으로 쓴 바이트 */
    sim.hw.SR = 0;                                    /* 하드웨어: 송신 버퍼 가득 참 */
    CHECK(uart_write(UART1, msg, sizeof msg) == 0);   /* 타임아웃 */
    uint8_t check[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    CHECK(crc8(check, sizeof check) == 0xF4);         /* CRC-8/SMBUS 표준 check 값 */
    CHECK(crc8(msg, sizeof msg) == 0x3B);
    CHECK(memcmp(msg, "hi!", 3) == 0);                /* 입력 버퍼가 그대로여야 한다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
