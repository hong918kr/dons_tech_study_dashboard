// C17_buddy.c — 64KB 이진 버디 할당기: 분할(split)과 XOR 로 버디 찾기, 재귀 병합
// build: cc -Wall -Wextra -O0 code/C17_buddy.c -o .work/bin/C17_buddy
// 블록 크기 = 1KB << order (order 0..6 → 1KB..64KB). 오프셋은 64KB 영역 시작 기준.
#include <stdio.h>
#include <stdint.h>

#define KB 1024u
#define MAX_ORDER 6                      // 64KB
#define NBLK 64                          // 1KB 단위 블록 수

// free[order][i] = 1 이면 (i * 1KB<<order) 오프셋의 order 크기 블록이 free
static uint8_t freemap[MAX_ORDER + 1][NBLK];

static uint32_t bsize(int o) { return KB << o; }

static void dump(const char *tag) {
    printf("  %-22s free:", tag);
    for (int o = MAX_ORDER; o >= 0; o--)
        for (unsigned i = 0; i < NBLK >> o; i++)
            if (freemap[o][i]) printf(" [%uKB @%uKB]", bsize(o) / KB, i * bsize(o) / KB);
    printf("\n");
}

static int order_for(uint32_t n) {        // n 을 담는 가장 작은 2^k KB
    int o = 0;
    while (bsize(o) < n) o++;
    return o;
}

static int64_t buddy_alloc(uint32_t n) {
    int want = order_for(n);
    int o = want;
    while (o <= MAX_ORDER) {              // want 이상에서 free 블록 찾기
        for (unsigned i = 0; i < NBLK >> o; i++)
            if (freemap[o][i]) {
                freemap[o][i] = 0;
                uint32_t off = i * bsize(o);
                while (o > want) {        // 반으로 쪼개며 내려간다. 오른쪽 반은 free 로 남김
                    o--;
                    uint32_t right = off + bsize(o);
                    freemap[o][right / bsize(o)] = 1;
                    printf("    split -> %uKB @%uKB (사용 후보) + %uKB @%uKB (free)\n",
                           bsize(o) / KB, off / KB, bsize(o) / KB, right / KB);
                }
                printf("  alloc(%uKB) -> %uKB 블록 @%uKB (내부 단편화 %uKB)\n",
                       n / KB, bsize(want) / KB, off / KB, (bsize(want) - n) / KB);
                return off;
            }
        o++;
    }
    printf("  alloc(%uKB) -> 실패\n", n / KB);
    return -1;
}

static void buddy_free(uint32_t off, uint32_t n) {
    int o = order_for(n);
    printf("  free(%uKB 블록 @%uKB)\n", bsize(o) / KB, off / KB);
    while (o < MAX_ORDER) {
        uint32_t buddy = off ^ bsize(o);  // 핵심: 버디 주소는 딱 한 비트만 다르다
        printf("    buddy of @%-2uKB (%2uKB) = @%-2uKB  (0x%05x ^ 0x%05x = 0x%05x) -> %s\n",
               off / KB, bsize(o) / KB, buddy / KB, off, bsize(o), buddy,
               freemap[o][buddy / bsize(o)] ? "free, 병합" : "사용 중, 멈춤");
        if (!freemap[o][buddy / bsize(o)]) break;
        freemap[o][buddy / bsize(o)] = 0;
        if (buddy < off) off = buddy;
        o++;
    }
    freemap[o][off / bsize(o)] = 1;
}

int main(void) {
    freemap[MAX_ORDER][0] = 1;
    printf("[1] 원문 예제: 64KB 에서 7KB 요청\n");
    dump("init");
    int64_t a = buddy_alloc(7 * KB);
    dump("after alloc 7KB");

    printf("\n[2] 하나 더: 3KB 요청\n");
    int64_t b = buddy_alloc(3 * KB);
    dump("after alloc 3KB");

    printf("\n[3] 7KB 블록 반환: 버디(8KB @8KB)는 3KB 때문에 쪼개져 일부 사용 중 → 병합 없이 멈춤\n");
    buddy_free((uint32_t)a, 7 * KB);
    dump("after free 7KB");

    printf("\n[4] 3KB 블록 반환: 64KB 까지 연쇄 병합\n");
    buddy_free((uint32_t)b, 3 * KB);
    dump("after free 3KB");
    return 0;
}
