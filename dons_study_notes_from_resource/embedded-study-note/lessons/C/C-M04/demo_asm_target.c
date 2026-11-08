/* C-M04 demo — 타깃별 비정렬 접근 asm 비교 (컴파일 전용, main 없음)
 * freestanding: <stdint.h> 만 쓴다 (thumb 타깃엔 <string.h> 가 없어서 __builtin_memcpy 사용).
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm_target.c -o -
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_asm_target.c -o -
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -mno-unaligned-access -O2 -S demo_asm_target.c -o -
 */
#include <stdint.h>

struct __attribute__((packed)) wire_hdr { uint8_t type; uint16_t len; uint32_t seq; };  /* 7 B, align 1 */
struct nat_hdr { uint8_t type; uint16_t len; uint32_t seq; };                           /* 8 B, align 4 */
struct __attribute__((packed)) wire_ts { uint8_t type; uint64_t ts; };                  /* 9 B, align 1 */
struct nat_rec { uint32_t a, b, c, d, e; };                                             /* 20 B, align 4 */
struct __attribute__((packed)) wire_rec { uint8_t t; uint32_t a, b, c, d, e; };         /* 21 B, align 1 */

/* ① packed 멤버 읽기: 컴파일러는 "이 멤버는 정렬 1" 임을 안다 */
uint32_t seq_packed(const struct wire_hdr *w) { return w->seq; }
/* ② memcpy 로 같은 바이트를 지역 변수에 */
uint32_t seq_memcpy(const uint8_t *p) { uint32_t v; __builtin_memcpy(&v, p + 3, 4); return v; }
/* ③ 자연 정렬 구조체 — 컴파일러는 h 가 4 정렬이라고 "믿는다" */
uint32_t seq_natural(const struct nat_hdr *h) { return h->seq; }
/* ④ packed 16비트 쓰기 */
void set_len_packed(struct wire_hdr *w, uint16_t v) { w->len = v; }
/* ⑤ packed 64비트 읽기 */
uint64_t ts_packed(const struct wire_ts *w) { return w->ts; }
/* ⑥ 자연 정렬 구조체 통째 복사 — LDM/STM (v7-M 에서도 비정렬이면 UsageFault) */
void copy_rec(struct nat_rec *d, const struct nat_rec *s) { *d = *s; }
/* ⑦ packed 구조체 통째 복사 */
void copy_wrec(struct wire_rec *d, const struct wire_rec *s) { *d = *s; }
/* ⑧ 정렬된 포인터를 가정하는 평범한 함수 — &w->len 을 받으면? */
void u16_add(uint16_t *p, uint16_t d) { *p = (uint16_t)(*p + d); }
