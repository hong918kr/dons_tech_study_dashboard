/* C-M12 demo — UB 카탈로그: 같은 소스가 -O0 과 -O2 에서 다른 답을 내는 것을 직접 본다.
 * 일부러 UB 를 담은 파일이다. strict 빌드(-Werror) 대상이 아니다.
 *
 *   cc -std=c11 -O0 -w demo_ub_catalog.c -o ub_O0 && ./ub_O0          # 안전하게 돌릴 수 있는 것 전부
 *   cc -std=c11 -O2 -w demo_ub_catalog.c -o ub_O2 && ./ub_O2
 *   ./ub_O2 2                                                           # ② NULL 역참조 (맥에선 SIGSEGV)
 *   cc -std=c11 -O2 -S demo_ub_catalog.c -o - | less                   # 컴파일러가 무엇을 지웠나
 *   cc -std=c11 -g -O1 -fsanitize=undefined demo_ub_catalog.c -o ub_ubsan && ./ub_ubsan
 *   cc -std=c11 -g -O1 -fsanitize=address   demo_ub_catalog.c -o ub_asan  && ./ub_asan 7
 *   cc -std=c11 -Wall -Wextra -O2 -c demo_ub_catalog.c -o /dev/null    # 컴파일 타임 경고는 무엇을 잡나
 *
 * 각 함수는 noinline + 입력은 volatile 에서 읽는다 → 호출 지점에서 상수로 접히지 않고,
 * 컴파일러가 "함수 몸체 안의 UB" 만 보고 최적화한 결과를 볼 수 있다.
 */
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define NOINL __attribute__((noinline))

/* ① signed overflow — "x+1 > x" 와 "양수에서 시작해 i>0 인 동안 i++" */
NOINL int plus_one_gt(int x) { return x + 1 > x; }
NOINL int count_up(int start) {
    int n = 0;
    for (int i = start; i > 0; i++) n++;           /* INT_MAX 다음은? */
    return n;
}

/* ② 역참조한 다음에 NULL 검사 */
NOINL int read_then_check(const int *p) {
    int v = *p;                                    /* 여기서 이미 *p */
    if (p == NULL) return -1;                      /* 이 검사는 살아남을까 */
    return v;
}

/* ③ 초기화 안 된 지역 변수 */
NOINL int uninit(int c) {
    int x;
    if (c) x = 5;
    return x;                                      /* c == 0 이면 ? */
}

/* ④ 배열 범위 밖 — 고전적인 <= 실수 (Regehr 의 예) */
static int table[4] = { 1, 2, 3, 4 };
NOINL bool in_table(int v) {
    for (int i = 0; i <= 4; i++)                   /* i == 4 는 table[4] — 범위 밖 */
        if (table[i] == v) return true;
    return false;
}

/* ⑤ 시프트 양 >= 폭 (C-B03 의 연장) */
static unsigned low_mask(unsigned n) { return (1u << n) - 1u; }
NOINL unsigned low_mask_rt(unsigned n) { return low_mask(n); }   /* n 은 런타임 값 */
NOINL unsigned low_mask_32(void)       { return low_mask(32); }  /* n 이 상수 32 로 인라인 */

/* ⑥ unsequenced 수정 — 빅엔디안 16비트를 한 줄로 읽기 (C-B14 의 연장) */
NOINL unsigned be16_oneliner(const uint8_t *p) { return (unsigned)(*p++ << 8 | *p++); }

/* ⑦ 끝난 지역 변수의 주소를 돌려준다 (dangling pointer) */
NOINL int *make_local(void) {
    int local = 42;
    int *p = &local;                               /* 직접 return &local 이면 경고가 나므로 한 단계 숨김 */
    return p;
}
NOINL int stomp(void) { volatile int junk[8]; for (int i = 0; i < 8; i++) junk[i] = 1000 + i; return junk[0]; }

/* ⑧ strict aliasing 위반 — 상세는 C-M13 */
NOINL int alias(int *a, float *b) {
    *a = 1;
    *b = 0.0f;                                     /* "float* 는 int 객체를 가리킬 수 없다" */
    return *a;
}

/* ⑨ 보너스: INT_MIN / -1, x / 0 */
NOINL int divi(int a, int b) { return a / b; }

static volatile int v_intmax = INT_MAX, v_start = INT_MAX - 3, v_zero = 0, v_42 = 42, v_5 = 5,
                    v_intmin = INT_MIN, v_m1 = -1, v_7 = 7;
static volatile unsigned v_32 = 32;

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);              /* sanitizer 메시지와 출력 순서를 맞춘다 */
    int k = argc > 1 ? atoi(argv[1]) : 0;
    int value = 7;

    if (k == 0 || k == 1) {
        printf("[1] plus_one_gt(INT_MAX)    = %d\n", plus_one_gt(v_intmax));
        printf("[1] count_up(INT_MAX-3)     = %d\n", count_up(v_start));
    }
    if (k == 2) {
        printf("[2] read_then_check(&value) = %d\n", read_then_check(&value));
        printf("[2] read_then_check(NULL)   = ...\n");
        printf("%d\n", read_then_check(v_zero ? &value : NULL));
    }
    if (k == 0 || k == 3) printf("[3] uninit(0)               = %d\n", uninit(v_zero));
    if (k == 0 || k == 4) {
        printf("[4] in_table(42)            = %d\n", in_table(v_42));
        printf("[4] in_table(5)             = %d\n", in_table(v_5));
    }
    if (k == 0 || k == 5) {
        printf("[5] low_mask_rt(32)         = 0x%08X\n", low_mask_rt(v_32));
        printf("[5] low_mask_32()           = 0x%08X\n", low_mask_32());
    }
    if (k == 0 || k == 6) {
        const uint8_t b[2] = { 0x12, 0x34 };
        printf("[6] be16_oneliner({12,34})  = 0x%04X\n", be16_oneliner(b));
    }
    if (k == 0 || k == 7) {
        int *p = make_local();
        int first = *p;                            /* 바로 읽기 */
        (void)stomp();                             /* 같은 스택 영역을 다른 함수가 사용 */
        printf("[7] *make_local(): first=%d after_call=%d\n", first, *p);
    }
    if (k == 0 || k == 8) {
        int u = 0;
        printf("[8] alias(&u, (float*)&u)   = %d\n", alias(&u, (float *)&u));
    }
    if (k == 0 || k == 9) {
        printf("[9] INT_MIN / -1            = %d\n", divi(v_intmin, v_m1));
        printf("[9] 7 / 0                   = %d\n", divi(v_7, v_zero));
    }
    return 0;
}
