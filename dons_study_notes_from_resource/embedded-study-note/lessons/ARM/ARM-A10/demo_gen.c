/* ARM-A10 demo_gen.c — 분기 보호(branch protection) 코드 생성 관찰용 함수 5개.
 * 빌드: gen.sh 참고 (arm64 / -mbranch-protection=standard / -arch arm64e). */
#include <stddef.h>
extern int sink(int);                       /* 다른 오브젝트에 있는 함수 (링크 안 함) */

int leaf_add(int a, int b) { return a + b; }            /* 리프: LR 을 저장하지 않는다 */

int nonleaf(int x) { return sink(x) + 1; }               /* 비리프: LR 을 스택에 저장 */

int early_exit(int x) {                                  /* 빠른 경로는 프롤로그 없이 반환? */
    if (x < 0) return -1;
    return sink(x) * 2;
}

int tail_call(int x) { return sink(x + 3); }             /* 꼬리 호출: b sink */

int call_ptr(int (*fp)(int), int x) { return fp(x) + fp(x + 1); }  /* 간접 호출 blr */
