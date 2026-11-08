/* C-B08 demo: 캐스트로 const 를 떼고 쓰면 어떻게 되나 (일부러 UB 를 일으키는 데모 — 학습용)
 *   cc -std=c11 -O2 demo_cast_away.c -o demo_cast_away
 *   ./demo_cast_away          # (1) char ** → const char ** 허점 + (2) 지역 const 수정
 *   ./demo_cast_away global   # (3) 전역 const 수정 → 읽기 전용 페이지라 크래시
 */
#include <stdio.h>
#include <string.h>

static const int k_limit = 100;            /* 전역 const → __TEXT,__const (읽기 전용 페이지) */

int main(int argc, char **argv) {
    /* (1) 만약 int ** → const int ** 가 암묵적으로 허용된다면 생기는 일 (여기선 캐스트로 강제) */
    int *p = NULL;
    const int **pp = (const int **)&p;     /* 컴파일러는 경고했을 것 — 캐스트로 입을 막았다 */
    *pp = &k_limit;                        /* 합법: *pp 는 const int * 이므로 const 주소를 담아도 된다 */
    printf("(1) p == &k_limit ? %d  -> 이제 int *p 로 const 객체를 쓸 수 있는 상태\n", p == &k_limit);

    /* (2) 지역 const 를 캐스트로 수정: UB. 컴파일러는 local 이 5 라고 믿고 상수로 접는다 */
    const int local = 5;
    int *w = (int *)&local;
    *w = 6;
    printf("(2) local = %d, *w = %d\n", local, *w);

    /* (3) 전역 const 수정: UB. macOS/Linux 에선 읽기 전용 페이지라 즉시 크래시 */
    if (argc > 1 && strcmp(argv[1], "global") == 0) {
        printf("(3) writing k_limit through p ...\n");
        fflush(stdout);
        *p = 200;
        printf("(3) survived? k_limit = %d\n", k_limit);
    }
    return 0;
}
