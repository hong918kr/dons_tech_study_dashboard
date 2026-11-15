/* ARM-B04 demo_clang.c — clang 이 같은 C 를 어떤 주소 지정 모드로 내리는지 보기
 * clang -S -O2 -fno-vectorize -fno-unroll-loops -fno-asynchronous-unwind-tables -o - demo_clang.c */
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t id; int32_t temp; uint64_t ts; } rec_t;

uint64_t c_sum(const uint64_t *a, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s += a[i];
    return s;
}
int64_t c_gather(const int32_t *base, const int32_t *idx, size_t n) {
    int64_t s = 0;
    for (size_t i = 0; i < n; i++) s += base[idx[i]];
    return s;
}
int64_t c_rec_sum_temp(const rec_t *r, size_t n) {
    int64_t s = 0;
    for (size_t i = 0; i < n; i++) s += r[i].temp;
    return s;
}
rec_t *c_rec_log_push(rec_t *slot, uint32_t id, int32_t temp, uint64_t ts) {
    slot->id = id; slot->temp = temp; slot->ts = ts;
    return slot + 1;
}
