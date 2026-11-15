/* ALGO-B06 demo: Cortex-M4 에서 퀵소트 한 프레임은 몇 바이트인가 (계측 없는 순수 버전)
 * libc 헤더 없이 <stdint.h>/<stddef.h> 만 — 맥에서 크로스 컴파일해 어셈블리와 스택 사용량만 본다.
 *
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S demo_m4.c -o demo_m4.s
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -c -fstack-usage demo_m4.c -o demo_m4.o && cat demo_m4.su
 */
#include <stddef.h>
#include <stdint.h>

static inline void swp(int16_t *a, int i, int j) { int16_t t = a[i]; a[i] = a[j]; a[j] = t; }

/* 교과서 Lomuto (마지막 원소 피벗, 양쪽 재귀) */
void qs_lomuto(int16_t *a, int lo, int hi) {
    if (lo >= hi) return;
    int16_t pivot = a[hi];
    int i = lo;
    for (int j = lo; j < hi; j++)
        if (a[j] <= pivot) swp(a, i++, j);
    swp(a, i, hi);
    qs_lomuto(a, lo, i - 1);
    qs_lomuto(a, i + 1, hi);
}

static int hoare(int16_t *a, int lo, int hi) {
    int16_t pivot = a[lo];
    int i = lo - 1, j = hi + 1;
    for (;;) {
        do { i++; } while (a[i] < pivot);
        do { j--; } while (pivot < a[j]);
        if (i >= j) return j;
        swp(a, i, j);
    }
}

/* median-of-3 + Hoare + 작은 쪽만 재귀 */
void qs_safe(int16_t *a, int lo, int hi) {
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] < a[lo]) swp(a, mid, lo);
        if (a[hi] < a[mid]) { swp(a, hi, mid); if (a[mid] < a[lo]) swp(a, mid, lo); }
        swp(a, lo, mid);
        int j = hoare(a, lo, hi);
        if (j - lo < hi - j) { if (lo < j) qs_safe(a, lo, j); lo = j + 1; }
        else                 { if (j + 1 < hi) qs_safe(a, j + 1, hi); hi = j; }
    }
}
