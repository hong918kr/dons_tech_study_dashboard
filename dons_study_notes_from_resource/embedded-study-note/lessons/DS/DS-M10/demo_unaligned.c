/* DS-M10 demo — 헤더를 구조체 캐스팅으로 읽으면 안 되는 이유를 눈으로 확인
 * cc -std=c11 -O2 -fsanitize=alignment demo_unaligned.c -o /tmp/DS-M10_demo && /tmp/DS-M10_demo
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct naive_hdr { uint16_t len; uint8_t type; uint32_t ts; };   /* 컴파일러가 패딩을 넣는다 */

/* 링 안에서 ts 는 레코드 시작 + 3 — 레코드 시작 자체도 아무 바이트 위치 */
uint32_t ts_cast(const uint8_t *rec)   { return *(const uint32_t *)(const void *)(rec + 3); }  /* UB */
uint32_t ts_memcpy(const uint8_t *rec) { uint32_t v; memcpy(&v, rec + 3, 4); return v; }      /* 호스트 엔디언 */
uint32_t ts_shift(const uint8_t *rec) {                                                       /* 항상 LE */
    const uint8_t *p = rec + 3;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int main(void) {
    printf("sizeof(struct naive_hdr) = %zu (on-wire header = 7)\n", sizeof(struct naive_hdr));
    printf("offsetof len=%zu type=%zu ts=%zu\n", offsetof(struct naive_hdr, len),
           offsetof(struct naive_hdr, type), offsetof(struct naive_hdr, ts));
    _Alignas(4) uint8_t ring[16] = { 0x05, 0x00, 0x02, 0x44, 0x33, 0x22, 0x11 };
    printf("shift : 0x%08x\n", ts_shift(ring));
    printf("memcpy: 0x%08x\n", ts_memcpy(ring));
    printf("cast  : 0x%08x  <- 이 Mac 에선 값은 맞게 나온다 (UBSan 보고를 보라)\n", ts_cast(ring));
    return 0;
}
