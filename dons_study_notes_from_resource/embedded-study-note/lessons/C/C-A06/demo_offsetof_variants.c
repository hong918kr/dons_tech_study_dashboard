/* C-A06 관찰: offsetof 를 만드는 세 가지 방법
 *   ① 고전 널 포인터 형태   ((size_t)&((T *)0)->m)        — 옛 libc/임베디드 헤더, 면접 단골 답
 *   ② 정적 저장소(arena) 기준 — 이 레슨의 MY_OFFSETOF
 *   ③ <stddef.h> offsetof    — 이 Mac(clang) 에선 __builtin_offsetof
 *
 *   cc -std=c11 -Wall -Wextra -O2 demo_offsetof_variants.c -o /tmp/C-A06_dv && /tmp/C-A06_dv
 *   cc -std=c11 -Wall -Wextra -pedantic -O2 -DICE_USE demo_offsetof_variants.c -o /tmp/C-A06_dv   # 상수식 자리에 쓰기
 *   cc -std=c11 -g -O1 -fsanitize=undefined demo_offsetof_variants.c -o /tmp/C-A06_dvu && /tmp/C-A06_dvu
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define OFF_NULL(T, m) ((size_t)&((T *)0)->m)

static _Alignas(64) unsigned char arena_[1024];
#define OFF_ARENA(T, m)                                                                   \
    (sizeof(char[(sizeof(T) <= sizeof arena_ && _Alignof(T) <= 64) ? 1 : -1]) - 1u +      \
     (size_t)((unsigned char *)&((T *)(void *)arena_)->m - arena_))

struct pkt { uint8_t type; uint32_t seq; struct { uint16_t len; uint8_t crc[2]; } hdr; uint8_t payload[8]; };

#ifdef ICE_USE
/* 정수 상수식(ICE)이 필요한 자리: _Static_assert, enum, case 라벨 */
_Static_assert(offsetof(struct pkt, seq) == 4, "builtin");
_Static_assert(OFF_NULL(struct pkt, seq) == 4, "null form");
_Static_assert(OFF_ARENA(struct pkt, seq) == 4, "arena form");
enum { HDR_OFF = OFF_NULL(struct pkt, hdr) };
#endif

int main(int argc, char **argv) {
    (void)argv;
    int i = argc + 2;                                  /* 컴파일러가 모르는 런타임 값 (인자 없으면 3) */
    printf("            seq  hdr.len  payload[i=%d]\n", i);
    printf("builtin  : %4zu %8zu %8zu\n", offsetof(struct pkt, seq), offsetof(struct pkt, hdr.len),
           offsetof(struct pkt, payload[i]));
    printf("null form: %4zu %8zu %8zu\n", OFF_NULL(struct pkt, seq), OFF_NULL(struct pkt, hdr.len),
           OFF_NULL(struct pkt, payload[i]));
    printf("arena    : %4zu %8zu %8zu\n", OFF_ARENA(struct pkt, seq), OFF_ARENA(struct pkt, hdr.len),
           OFF_ARENA(struct pkt, payload[i]));
    return 0;
}
