/* ARM-A11 demo_restrict_lie: restrict 약속을 어기면(겹친 버퍼) 무슨 일이 나나 — 정의되지 않은 동작(UB)의 "이번" 결과
 * 테스트에는 절대 넣지 않는다. 다른 번역 단위(solution.c)의 함수를 부르므로 컴파일러는 호출 지점의 겹침을 모른다.
 *   cc -std=c11 -O2 -c solution.c -Dmain=a11_solution_main -o /tmp/ARM-A11_sol.o
 *   cc -std=c11 -O2 demo_restrict_lie.c /tmp/ARM-A11_sol.o -o /tmp/ARM-A11_lie && /tmp/ARM-A11_lie
 */
#include <stddef.h>
#include <stdio.h>

void saxpy_plain(float *y, const float *x, float a, size_t n);
void saxpy_restrict(float *restrict y, const float *restrict x, float a, size_t n);

static void show(const char *name, const float *b, int n) {
    printf("%-15s", name);
    for (int i = 1; i <= n; i++) printf("%3.0f", b[i]);
    printf("\n");
}

int main(void) {
    enum { N = 40 };
    float b1[N + 1], b2[N + 1];
    for (int i = 0; i <= N; i++) b1[i] = b2[i] = 1.0f;
    saxpy_plain(b1 + 1, b1, 1.0f, N);       /* y = x+1 겹침: 순차 의미 → y[i] = i+2 (누적) */
    saxpy_restrict(b2 + 1, b2, 1.0f, N);    /* 같은 호출, restrict 약속 위반 = UB */
    printf("b[1..%d] after y[i] = 1*x[i] + y[i] with y = x + 1, all ones\n", N);
    show("saxpy_plain", b1, N);
    show("saxpy_restrict", b2, N);
    return 0;
}
