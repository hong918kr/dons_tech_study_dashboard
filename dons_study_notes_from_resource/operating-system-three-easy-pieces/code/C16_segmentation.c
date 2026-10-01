// C16_segmentation.c — 원문 Figure 16.5 의 세그먼트 레지스터로 14-bit VA 를 번역한다
// build: cc -Wall -Wextra -O0 code/C16_segmentation.c -o .work/bin/C16_segmentation
//  - 상위 2비트 = 세그먼트 번호 (00 code, 01 heap, 11 stack), 하위 12비트 = 오프셋
//  - stack 은 음의 방향으로 자람: 오프셋 - 최대 세그먼트 크기(4KB) = 음수 오프셋
//  - 보호 비트(R/W/X) 검사까지
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define KB 1024
#define SEG_MASK    0x3000
#define SEG_SHIFT   12
#define OFFSET_MASK 0x0FFF
#define MAX_SEG     (1 << SEG_SHIFT)        // 4KB: 세그먼트 하나가 가질 수 있는 최대 크기

enum { R = 4, W = 2, X = 1 };
struct seg { const char *name; int base, size, grows_pos, prot, valid; };

// Figure 16.5 (세그먼트 2 는 미사용)
static struct seg segs[4] = {
    {"code",  32 * KB, 2 * KB, 1, R | X, 1},
    {"heap",  34 * KB, 2 * KB, 1, R | W, 1},
    {"-",     0,       0,      1, 0,     0},
    {"stack", 28 * KB, 2 * KB, 0, R | W, 1},
};

static const char *acc_name(int a) { return a == R ? "read" : a == W ? "write" : "exec"; }

static void translate(int va, int access) {
    int s = (va & SEG_MASK) >> SEG_SHIFT;
    int off = va & OFFSET_MASK;
    struct seg *g = &segs[s];
    printf("  VA %5d (0x%04x, seg=%d%d off=%4d) %-5s: ", va, va, (s >> 1) & 1, s & 1, off, acc_name(access));
    if (!g->valid) { printf("FAULT (segment %d 미사용)\n", s); return; }
    int pa;
    if (g->grows_pos) {
        if (off >= g->size) { printf("FAULT %s: off %d >= size %d\n", g->name, off, g->size); return; }
        pa = g->base + off;
        printf("%s  PA = %d + %d", g->name, g->base, off);
    } else {
        int neg = off - MAX_SEG;                // 음수 오프셋
        if (abs(neg) > g->size) { printf("FAULT %s: |%d| > size %d\n", g->name, neg, g->size); return; }
        pa = g->base + neg;
        printf("%s  neg off = %d - %d = %d, PA = %d + (%d)", g->name, off, MAX_SEG, neg, g->base, neg);
    }
    if (!(g->prot & access)) { printf(" -> PROTECTION FAULT (%s 금지)\n", acc_name(access)); return; }
    printf(" = %d (%.2f KB)\n", pa, pa / 1024.0);
}

int main(void) {
    printf("[1] 원문 예제\n");
    translate(100, X);                           // code: 100 + 32KB = 32868
    translate(4200, R);                          // heap: 4200-4096=104, 34KB+104 = 34920
    translate(7 * KB, R);                        // heap 끝 너머 → fault
    translate(15 * KB, W);                       // stack: 3KB-4KB = -1KB, 28KB-1KB = 27KB

    printf("\n[2] 스택 경계 확인 (스택은 VA 14KB..16KB, PA 26KB..28KB)\n");
    translate(16 * KB - 1, R);                   // 가장 위 바이트: off 4095 → -1
    translate(14 * KB, R);                       // 가장 아래: off 2048 → -2048 (|−2048| == size, 허용)
    translate(14 * KB - 1, R);                   // 한 칸 더 아래 → fault

    printf("\n[3] 보호 비트\n");
    translate(100, W);                           // 코드 세그먼트에 쓰기 → protection fault
    translate(4200, X);                          // heap 에서 실행 → protection fault (NX 와 같은 발상)

    printf("\n[4] 새 예제: heap 이 1KB 커졌다 (OS 가 heap size 2K -> 3K, sbrk 흉내)\n");
    translate(4096 + 2500, R);                   // 성장 전: fault
    segs[1].size = 3 * KB;
    translate(4096 + 2500, R);                   // 성장 후: 34KB + 2500
    return 0;
}
