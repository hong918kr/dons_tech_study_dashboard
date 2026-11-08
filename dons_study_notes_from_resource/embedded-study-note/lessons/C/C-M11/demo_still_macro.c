/* C-M11 demo: 매크로가 여전히 필요한 곳 — 함수 / static const 로 바꾸면 무엇이 깨지나
 *   cc -std=c11 -Wall -Wextra demo_still_macro.c -o demo_still_macro && ./demo_still_macro
 *   -DTRY_CONST : #define 상수를 static const 로 바꾸면 → 상수식이 필요한 세 곳 (clang 은 확장으로 접고 -pedantic 에서 경고)
 *   -DARRAY_PTR : 배열 "매개변수"(= 포인터)에 ARRAY_SIZE → 정적 단언 실패로 컴파일 에러
 */
#include <stdint.h>
#include <stdio.h>

/* ① 상수식: 배열 크기 · case 라벨 · _Static_assert · #if 에는 "정수 상수식" 이 필요하다 */
#ifdef TRY_CONST
static const uint32_t RX_FIFO_DEPTH = 16u;      /* C 에서 const 변수는 상수식이 아니다 (C++ 은 됨) */
static const uint32_t CMD_RESET = 0x01u;
#else
#define RX_FIFO_DEPTH 16u                         /* 또는 enum { RX_FIFO_DEPTH = 16 }; (int 범위만) */
#define CMD_RESET     0x01u
#endif
static uint8_t rx_fifo[RX_FIFO_DEPTH];          /* 파일 범위 배열 크기 */
_Static_assert(RX_FIFO_DEPTH % 4u == 0, "FIFO depth must be a multiple of 4");

static int handle(uint32_t cmd) {
    switch (cmd) {
    case CMD_RESET: return 1;                    /* case 라벨 */
    default:        return 0;
    }
}

/* ② 호출 위치 캡처: __LINE__/__FILE__/__func__ 는 "쓰인 곳" 에서 정해진다 */
static inline int line_inline(void) { return __LINE__; }   /* 항상 이 줄 */
#define LINE_HERE() __LINE__                                /* 부른 줄 */

/* ③ 문자열화 # 와 토큰 붙이기 ## : 함수는 소스 글자를 볼 수 없다 */
#define FW_MAJOR 2
#define FW_MINOR 7
#define STR(x)  #x
#define XSTR(x) STR(x)
static const char fw_version[] = "fw v" XSTR(FW_MAJOR) "." XSTR(FW_MINOR);
#define UART_CR1_UE_Msk 0x1u
#define REG_MSK(P, R, F) P##_##R##_##F##_Msk

/* ④ ARRAY_SIZE: 함수로 만들 수 없다 (넘기는 순간 decay). 포인터면 컴파일 에러가 나게 */
#define IS_ARRAY(a) _Generic(&(a), __typeof__(*(a)) (*)[sizeof(a) / (sizeof(*(a)))]: 1, default: 0)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]) \
    + 0 * sizeof(struct { _Static_assert(IS_ARRAY(a), "ARRAY_SIZE: argument is not an array"); char c_; }))
#ifdef ARRAY_PTR
static size_t count_baud(const uint32_t tbl[5]) { return ARRAY_SIZE(tbl); }   /* tbl 은 사실 const uint32_t * */
#endif

int main(void) {
    rx_fifo[0] = (uint8_t)handle(CMD_RESET);
    printf("rx_fifo[0]=%u  ARRAY_SIZE(rx_fifo)=%zu\n", rx_fifo[0], ARRAY_SIZE(rx_fifo));
    printf("line_inline(): %d, %d   LINE_HERE(): %d, %d\n",
           line_inline(), line_inline(), LINE_HERE(), LINE_HERE());
    printf("%s   REG_MSK(UART, CR1, UE)=0x%x   STR(a + b)=\"%s\"\n", fw_version, REG_MSK(UART, CR1, UE), STR(a + b));
    return 0;
}
