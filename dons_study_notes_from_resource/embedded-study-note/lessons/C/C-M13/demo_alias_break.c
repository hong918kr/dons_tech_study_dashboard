/* C-M13 demo — strict aliasing 위반이 -O0 과 -O2 에서 다른 결과를 내는 것을 관찰한다.
 * 이 파일은 일부러 UB 를 담고 있다 (하네스가 아님). -Werror 없이 빌드:
 *   cc -std=c11 -O0 demo_alias_break.c -o /tmp/ab0 && /tmp/ab0
 *   cc -std=c11 -O2 demo_alias_break.c -o /tmp/ab2 && /tmp/ab2
 *   cc -std=c11 -O2 -fno-strict-aliasing demo_alias_break.c -o /tmp/abn && /tmp/abn
 *   cc -std=c11 -g -O1 -fsanitize=address,undefined demo_alias_break.c -o /tmp/abs && /tmp/abs   (아무 보고 없음)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ① 고전 예제: int 와 float 는 서로 aliasing 할 수 없다고 가정 → return 1 상수 */
__attribute__((noinline)) int f(int *i, float *fp) {
    *i = 1;
    *fp = 0.0f;
    return *i;
}

/* ② 버퍼 → uint32_t: 16비트 쓰기 뒤의 32비트 읽기 (패킷 헤더 패치 흉내) */
__attribute__((noinline)) uint32_t patch_len(uint32_t *word, uint16_t *half) {
    *word = 0x11112222u;
    *half = 0x3333u;          /* 같은 메모리의 하위 16비트를 고친다 (리틀 엔디언) */
    return *word;             /* 기대: 0x11113333 */
}

/* ③ 같은 ② 를 GCC/clang 확장 may_alias 타입으로: "이 타입은 char 처럼 무엇이든 aliasing 할 수 있다" */
typedef uint16_t __attribute__((may_alias)) u16_alias;
__attribute__((noinline)) uint32_t patch_len_may_alias(uint32_t *word, u16_alias *half) {
    *word = 0x11112222u;
    *half = 0x3333u;
    return *word;             /* 컴파일러가 다시 읽는다 */
}

/* ④ 같은 ② 를 memcpy 로: 표준만으로 안전 */
__attribute__((noinline)) uint32_t patch_len_memcpy(uint32_t *word, void *half_addr) {
    uint16_t h = 0x3333u;
    *word = 0x11112222u;
    memcpy(half_addr, &h, sizeof h);   /* 문자 타입 복사로 정의됨 → 모든 객체와 alias 가능 */
    return *word;
}

/* ⑤ float 부호 뒤집기를 uint32_t* 로 — 읽기→쓰기→다시 읽기 */
__attribute__((noinline)) float flip_sign(float *x) {
    float before = *x;
    *(uint32_t *)x ^= 0x80000000u;   /* 위반: float 객체를 uint32_t lvalue 로 수정 */
    return before + *x;              /* 기대: before + (-before) = 0 */
}

int main(void) {
    int obj = 0;
    int ret = f(&obj, (float *)&obj);
    printf("1) f(&obj, (float*)&obj) = %d   (memory now 0x%08x)\n", ret, (unsigned)obj);

    uint32_t w = 0;
    uint32_t r = patch_len(&w, (uint16_t *)&w);
    printf("2) patch_len -> 0x%08x  (memory now 0x%08x)\n", (unsigned)r, (unsigned)w);

    w = 0;
    r = patch_len_may_alias(&w, (u16_alias *)&w);
    printf("3) patch_len_may_alias -> 0x%08x\n", (unsigned)r);
    w = 0;
    r = patch_len_memcpy(&w, &w);
    printf("4) patch_len_memcpy    -> 0x%08x\n", (unsigned)r);

    float x = 3.5f;
    printf("5) flip_sign(3.5) = %g\n", (double)flip_sign(&x));
    return 0;
}
