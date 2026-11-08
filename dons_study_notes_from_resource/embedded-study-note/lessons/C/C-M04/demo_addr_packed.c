/* C-M04 demo — packed 멤버의 주소(&w->len)를 꺼내 일반 포인터로 쓰면?
 *   cc -std=c11 -Wall -Wextra -O2 demo_addr_packed.c -o apk            (경고 관찰, -Werror 없이)
 *   cc -std=c11 -g -O1 -fsanitize=alignment demo_addr_packed.c -o apk_san && ./apk_san
 * 일부러 UB 를 담은 관찰용 파일이다 (하네스 아님).
 */
#include <stdint.h>
#include <stdio.h>

struct __attribute__((packed)) wire_hdr { uint8_t type; uint16_t len; uint32_t seq; };

/* "레거시" API — 인자는 정렬된 uint16_t* 라고 가정한다 (C 타입 규칙상 당연한 가정) */
static void __attribute__((noinline)) u16_add(uint16_t *p, uint16_t d) { *p = (uint16_t)(*p + d); }

static _Alignas(4) uint8_t buf[16] = { 0xA5, 0x02, 0x10, 0x00, 0x78, 0x56, 0x34, 0x12 };

int main(void) {
    struct wire_hdr *w = (struct wire_hdr *)(buf + 2);   /* 헤더가 오프셋 2 → len 은 주소 4k+3 */

    w->len = 0x0010;                 /* OK: 컴파일러가 "packed 멤버" 임을 알고 비정렬 안전 코드를 낸다 */
    u16_add(&w->len, 1);             /* 위험: 정렬 정보가 사라진 uint16_t* 로 전달된다 */

    uint16_t *lp = &w->len;          /* 같은 문제 — 포인터 변수에 담아도 */
    printf("len=%u (addr %% 2 = %u)\n", (unsigned)*lp, (unsigned)((uintptr_t)lp % 2));
    return 0;
}
