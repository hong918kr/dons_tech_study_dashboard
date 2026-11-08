/* C-M12 demo — 같은 UB 함수들을 Cortex-M4 로 크로스 컴파일해 asm 만 본다 (main 없음, freestanding).
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_ub_cm4.c -o -
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S -fno-delete-null-pointer-checks demo_ub_cm4.c -o -
 *   cc -std=c11 -O2 -S demo_ub_cm4.c -o -                    (같은 파일을 arm64 맥 네이티브로)
 * Cortex-M 에서 주소 0 은 보통 벡터 테이블(초기 SP 값)이 있는 "읽을 수 있는" 메모리다.
 * 그래서 *NULL 이 폴트 없이 값을 돌려주고, 지워진 NULL 검사의 대가가 조용한 오답이 된다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int plus_one_gt(int x) { return x + 1 > x; }

int count_up(int start) {
    int n = 0;
    for (int i = start; i > 0; i++) n++;
    return n;
}

int read_then_check(const int *p) {
    int v = *p;                   /* 역참조 먼저 */
    if (p == NULL) return -1;     /* 검사는 나중 */
    return v;
}

typedef struct { uint32_t id; int32_t temp; } sensor_t;
int32_t sensor_temp(const sensor_t *s) {   /* 같은 실수, 구조체 멤버 버전 */
    int32_t t = s->temp;          /* 역참조 먼저 */
    if (s == NULL) return -999;   /* 검사는 나중 */
    return t;
}

static const int table[4] = { 1, 2, 3, 4 };
bool in_table(int v) {
    for (int i = 0; i <= 4; i++)
        if (table[i] == v) return true;
    return false;
}

int divi(int a, int b) { return a / b; }

/* ---- 수정본: solution.c Part A 와 같은 코드. 사전 검사는 몇 명령어가 되나? ---- */
bool add_i32(int32_t a, int32_t b, int32_t *out) {
    if ((b > 0 && a > INT32_MAX - b) || (b < 0 && a < INT32_MIN - b)) return false;
    *out = a + b;
    return true;
}
bool mul_i32(int32_t a, int32_t b, int32_t *out) {
    if (a > 0) {
        if (b > 0) { if (a > INT32_MAX / b) return false; }
        else       { if (b < INT32_MIN / a) return false; }
    } else {
        if (b > 0) { if (a < INT32_MIN / b) return false; }
        else       { if (a != 0 && b < INT32_MAX / a) return false; }
    }
    *out = a * b;
    return true;
}
bool mul_i32_wide(int32_t a, int32_t b, int32_t *out) {   /* 32비트라면 넓혀서 검사해도 된다 */
    int64_t r = (int64_t)a * b;
    if (r < INT32_MIN || r > INT32_MAX) return false;
    *out = (int32_t)r;
    return true;
}
