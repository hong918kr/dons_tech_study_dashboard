/* ARM-A11 demo_csum: float 조건부 합 4가지 쓰기 × 플래그 5가지 → 벡터화되나, 순서를 지키나 (bash demo_flags.sh) */
#include <stddef.h>

float v_if(const float *a, size_t n, float t) {           /* if 형태 */
    float s = 0.0f;
    for (size_t i = 0; i < n; i++)
        if (a[i] > t) s += a[i];
    return s;
}
float v_sel(const float *a, size_t n, float t) {          /* 선택식 + 0.0f */
    float s = 0.0f;
    for (size_t i = 0; i < n; i++) s += (a[i] > t) ? a[i] : 0.0f;
    return s;
}
float v_if_ra(const float *a, size_t n, float t) {        /* if 형태 + reassociate pragma */
#pragma clang fp reassociate(on)
    float s = 0.0f;
    for (size_t i = 0; i < n; i++)
        if (a[i] > t) s += a[i];
    return s;
}
float v_sel_ra(const float *a, size_t n, float t) {       /* 선택식 + reassociate pragma */
#pragma clang fp reassociate(on)
    float s = 0.0f;
    for (size_t i = 0; i < n; i++) s += (a[i] > t) ? a[i] : 0.0f;
    return s;
}
float v_sum(const float *a, size_t n) {                   /* 조건 없는 합 (비교용) */
    float s = 0.0f;
    for (size_t i = 0; i < n; i++) s += a[i];
    return s;
}
