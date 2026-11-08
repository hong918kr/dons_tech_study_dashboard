/* C-B09 demo_poll.c — volatile 유무에 따라 -O2 코드가 어떻게 달라지나
 * 빌드: clang -std=c11 -O2 -S demo_poll.c -o - | less     (arm64 asm 보기)
 * 실행 파일이 아니다 (main 없음). 각 함수 쌍을 asm으로 비교한다. */
#include <stdint.h>

int          ready;     /* ISR이 1로 바꿔 준다고 "가정"한 플래그 — volatile 없음 */
volatile int ready_v;   /* 같은 플래그, volatile                                  */

/* ① 폴링 루프 */
void wait_plain(void)    { while (!ready)   { } }
void wait_volatile(void) { while (!ready_v) { } }

/* ② 같은 변수를 두 번 읽기 (각각 별도의 full expression) */
int read_twice_plain(void)    { int a = ready;   int b = ready;   return a + b; }
int read_twice_volatile(void) { int a = ready_v; int b = ready_v; return a + b; }

/* ③ 같은 변수에 연달아 두 번 쓰기 (예: FIFO 레지스터에 2바이트 push) */
void write_twice_plain(void)    { ready   = 0x41; ready   = 0x42; }
void write_twice_volatile(void) { ready_v = 0x41; ready_v = 0x42; }

/* ④ 지연 루프 */
void delay_plain(void)    { for (int i = 0; i < 1000; i++) { } }
void delay_volatile(void) { for (volatile int i = 0; i < 1000; i++) { } }

/* ⑤ volatile 은 non-volatile 접근의 순서/존재를 지켜 주지 않는다 */
int data;                         /* 일반 변수 */
void publish(void) {
    data    = 1;                  /* ISR이 ready_v==1을 보면 data==1을 기대 */
    ready_v = 1;
    data    = 2;
}

/* ⑥ ①과 같은 의미지만 제어식이 상수(for(;;))인 루프 — C11 6.8.5p6 "종료 가정" 대상이 아님 */
void wait_plain_for(void) { for (;;) { if (ready) break; } }

/* ⑦ 추가 연습 5 (접근 횟수 퀴즈) — asm 에서 ldr/str 개수를 세어 본다 */
void q_a(volatile uint32_t *r) { *r |= 1u; }            /* 읽기 ? 쓰기 ? */
void q_b(volatile uint32_t *r) { *r = *r; }
void q_c(volatile uint32_t *r) { (void)*r; }             /* "dummy read" */
void q_d(volatile uint32_t *r) { *r &= ~2u; *r |= 4u; }
