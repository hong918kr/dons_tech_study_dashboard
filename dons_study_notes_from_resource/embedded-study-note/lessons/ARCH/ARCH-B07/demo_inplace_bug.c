/* ARCH-B07 demo — 두 단계(two-phase)를 어기면 무엇이 틀리나
 * 상태를 먼저 갱신(S = next)하고 그 "다음 상태"로 출력을 계산하는 흔한 버그.
 *   Moore: out = f(S_next) → 출력이 입력과 같은 사이클에 나온다 (Mealy 타이밍처럼 보이지만, 실제로는 in→out 조합 경로)
 *   Mealy: out = g(S_next, in) → S_next==S3 이고 in==1 ⇔ "101" 이 막 끝남 → 1011 이 아니라 101 을 검출!
 * 정답 위치: Mealy 3,6,10 / Moore 4,7,11 (solution.c)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_inplace_bug.c -o /tmp/esn_ARCH-B07/inplace && /tmp/esn_ARCH-B07/inplace
 */
#include <stdint.h>
#include <stdio.h>

static const uint8_t MO[5][2] = { {0, 1}, {2, 1}, {0, 3}, {2, 4}, {2, 1} };
static const uint8_t ME[4][2] = { {0, 1}, {2, 1}, {0, 3}, {2, 1} };

int main(void) {
    const uint8_t in[] = {1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 0};
    const int n = (int)(sizeof in / sizeof in[0]);
    uint8_t s = 0, m = 0;
    printf("wrong Moore (out from S_next): ");
    for (int k = 0; k < n; k++) { s = MO[s][in[k]]; if (s == 4) printf("%d,", k); }
    printf("\nwrong Mealy (out from S_next): ");
    for (int k = 0; k < n; k++) { m = ME[m][in[k]]; if (m == 3 && in[k]) printf("%d,", k); }
    printf("\n");
    return 0;
}
