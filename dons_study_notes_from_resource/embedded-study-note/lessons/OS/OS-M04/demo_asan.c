/* OS-M04 demo_asan.c — 내 할당기 위의 오버플로를 ASan 은 왜 못 보나, mm_check 는 무엇을 보나.
 *
 * a = mm_malloc(24) → 블록 48 B (payload 용량 32 B). 그런데 40 B 를 쓴다 (8 B 넘침 = a 의 footer).
 *   ./demo_asan          ASan 은 조용하다: mmap 영역 전체가 "정상 메모리" 라서. mm_check 만 잡는다.
 *   ./demo_asan poison   ASan 인식 할당기처럼 hdr/ftr 를 poison 해 두면, 넘치는 순간 ASan 이 멈춘다.
 *
 * build (ASan):   cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer demo_asan.c -o demo_asan
 * build (strict): cc -std=c11 -Wall -Wextra -Werror -O2 demo_asan.c -o demo_asan   (poison 은 꺼짐)
 */
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 테스트용 도우미 */
#define MM_NO_MAIN
#include "solution.c"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define HAVE_ASAN 1
#endif
#endif

int main(int argc, char **argv) {
    int poison = argc > 1 && strcmp(argv[1], "poison") == 0;
    mm_config(KIND_EXPLICIT, FIT_FIRST);
    mm_init();
    char *a = mm_malloc(24), *b = mm_malloc(24);
    printf("a=%p block=%zu B (payload capacity %zu), b=%p\n", (void *)a, blk_size(a), blk_size(a) - DSIZE,
           (void *)b);
    if (poison) {
#ifdef HAVE_ASAN
        ASAN_POISON_MEMORY_REGION(ftrp(a), WSIZE);         /* a 의 footer + b 의 header = redzone */
        ASAN_POISON_MEMORY_REGION(hdrp(b), WSIZE);
        printf("poisoned a's footer and b's header\n");
#else
        printf("built without -fsanitize=address: poison ignored\n");
#endif
    }
    memset(a, 'x', 40);                                    /* 버그: 32 B 용량에 40 B */
    printf("wrote 40 bytes into a 32-byte payload — ASan said nothing\n");
#ifdef HAVE_ASAN
    if (poison) { ASAN_UNPOISON_MEMORY_REGION(ftrp(a), WSIZE); ASAN_UNPOISON_MEMORY_REGION(hdrp(b), WSIZE); }
#endif
    printf("mm_check = %d\n", mm_check(1));
    mem_deinit();
    return 0;
}
