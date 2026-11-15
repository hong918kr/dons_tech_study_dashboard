/* ARCH-M03 demo_kernels.c — 측정에 쓰는 커널만 모은 파일 (demo_bench.c 와 따로 컴파일 → 인라인 안 됨)
 *
 * asm 확인:  cc -std=c11 -O2 -S demo_kernels.c -o - | less
 * 벤치:      cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c demo_kernels.c -o bench
 *
 * 모든 커널은 같은 일: a[i] >= t 인 원소의 합.
 *   k_auto_u8     아무 장치 없이 -O2 에 맡김        → clang 이 NEON 으로 벡터화 (분기 없음)
 *   k_scalar_u8   벡터화만 끔                        → clang 이 스스로 CSEL 로 바꿈 (분기 없음)
 *   k_branchy_u8  벡터화 끔 + 조건 블록 안에 빈 asm  → 진짜 b.cond 가 남는다
 *   k_csel_u8     벡터화 끔 + 삼항 연산자            → csel
 *   k_mask_u8     벡터화 끔 + -(x>=t) & x 마스크     → csel 또는 and (asm 으로 확인)
 *   k_*_i32       같은 것의 int32 판
 *   k_max_*       달리는 최댓값: csel 이 루프 의존 사슬에 들어가는 경우 (branchless 가 지는 예)
 */
#include <stddef.h>
#include <stdint.h>

#define NOVEC _Pragma("clang loop vectorize(disable) interleave(disable)")

uint64_t k_auto_u8(const uint8_t *a, size_t n, unsigned t) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++)
        if (a[i] >= t) s += a[i];
    return s;
}

uint64_t k_scalar_u8(const uint8_t *a, size_t n, unsigned t) {
    uint64_t s = 0;
    NOVEC
    for (size_t i = 0; i < n; i++)
        if (a[i] >= t) s += a[i];
    return s;
}

uint64_t k_branchy_u8(const uint8_t *a, size_t n, unsigned t) {
    uint64_t s = 0;
    NOVEC
    for (size_t i = 0; i < n; i++) {
        if (a[i] >= t) {
            __asm__ volatile("" : "+r"(s));   /* 이 블록을 무조건 실행할 수 없게 → if-conversion 금지 */
            s += a[i];
        }
    }
    return s;
}

uint64_t k_csel_u8(const uint8_t *a, size_t n, unsigned t) {
    uint64_t s = 0;
    NOVEC
    for (size_t i = 0; i < n; i++) {
        unsigned x = a[i];
        s += (x >= t) ? x : 0u;
    }
    return s;
}

uint64_t k_mask_u8(const uint8_t *a, size_t n, unsigned t) {
    uint64_t s = 0;
    NOVEC
    for (size_t i = 0; i < n; i++) {
        uint64_t x = a[i];
        s += -(uint64_t)(x >= t) & x;
    }
    return s;
}

int64_t k_branchy_i32(const int32_t *a, size_t n, int32_t t) {
    int64_t s = 0;
    NOVEC
    for (size_t i = 0; i < n; i++) {
        if (a[i] >= t) {
            __asm__ volatile("" : "+r"(s));
            s += a[i];
        }
    }
    return s;
}

int64_t k_csel_i32(const int32_t *a, size_t n, int32_t t) {
    int64_t s = 0;
    NOVEC
    for (size_t i = 0; i < n; i++) {
        int64_t x = a[i];
        s += (a[i] >= t) ? x : 0;
    }
    return s;
}

/* 달리는 최댓값. 랜덤 데이터에서 갱신은 드물다(대략 ln n 번) → 분기는 거의 항상 맞는다. */
uint32_t k_max_branchy(const uint32_t *a, size_t n) {
    uint32_t m = 0;
    NOVEC
    for (size_t i = 0; i < n; i++) {
        if (a[i] > m) {
            __asm__ volatile("" : "+r"(m));
            m = a[i];
        }
    }
    return m;
}

uint32_t k_max_csel(const uint32_t *a, size_t n) {
    uint32_t m = 0;
    NOVEC
    for (size_t i = 0; i < n; i++)
        m = (a[i] > m) ? a[i] : m;     /* cmp + csel 이 m 의 루프 의존 사슬에 들어간다 */
    return m;
}

/* 조건부 해시 갱신 (FNV-1a: h = (h ^ x) * 16777619): 선택(select)이 곱셈 사슬 위에 놓인다.
 *   csel 판: 새 h 를 매 원소 계산하고 csel 로 고른다 → 루프 사슬 = eor + mul + csel, 원소마다
 *   branch 판: 예측이 맞으면 안 갱신하는 원소는 사슬에 안 들어간다 (h 는 그대로 다음으로 넘어감) */
uint32_t k_hash_natural(const uint8_t *a, size_t n, unsigned t) {
    uint32_t h = 2166136261u;
    NOVEC
    for (size_t i = 0; i < n; i++)
        if (a[i] >= t) h = (h ^ a[i]) * 16777619u;
    return h;
}

uint32_t k_hash_branchy(const uint8_t *a, size_t n, unsigned t) {
    uint32_t h = 2166136261u;
    NOVEC
    for (size_t i = 0; i < n; i++) {
        if (a[i] >= t) {
            __asm__ volatile("" : "+r"(h));
            h = (h ^ a[i]) * 16777619u;
        }
    }
    return h;
}

uint32_t k_hash_csel(const uint8_t *a, size_t n, unsigned t) {
    uint32_t h = 2166136261u;
    NOVEC
    for (size_t i = 0; i < n; i++) {
        uint32_t nh = (h ^ a[i]) * 16777619u;
        h = __builtin_unpredictable(a[i] >= t) ? nh : h;
    }
    return h;
}
