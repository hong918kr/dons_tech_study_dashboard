/* C-B15 demo: SWAP 매크로의 이름 포획(name capture).
 *   cc -std=c11 -Wall -Wextra -c demo_capture.c          → 아무 경고 없음 (!)
 *   cc -std=c11 -Wall -Wextra -Wshadow -c demo_capture.c → -Wshadow 경고로 겨우 잡힌다
 *   cc -E -P demo_capture.c                              → int tmp = (tmp); 이 보인다
 *   cc -std=c11 -Wall -Wextra -Wshadow -c -DFIXED demo_capture.c → 경고 없음
 * 주의: 버그 버전은 초기화 안 된 지역 변수를 읽는다(UB) — 실행 결과를 믿지 말 것.
 */
#if defined(FIXED)
#define SWAP(a, b) do { __typeof__(a) swap_tmp_ = (a); (a) = (b); (b) = swap_tmp_; } while (0)
#else
#define SWAP(a, b) do { int tmp = (a); (a) = (b); (b) = tmp; } while (0)
#endif

int demo(void) {
    int tmp = 1, other = 2;     /* 호출자도 하필 tmp 라는 이름을 쓴다 */
    SWAP(tmp, other);
    return tmp * 10 + other;    /* 기대: 21 */
}
