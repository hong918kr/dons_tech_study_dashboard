/* ARCH-M04 demo — 직접 사상 캐시 스윕 표 (solution.c 의 판정 로직을 축소해 넣었다)
 *
 *   표 1  stride 스윕: (a) 64 KB 를 한 번 훑기 (streaming)  (b) 원소 64개를 4바퀴 도는 루프
 *   표 2  64×64 int 행렬: 행/열 순회 × pitch (패딩)
 *   표 3  열 순회를 고치는 두 방법: 캐시를 키우기 vs pitch 를 한 라인 늘리기
 *   표 4  라인 크기 스윕 (용량 4 KB 고정): 행 순회 vs 열 순회(pitch 288)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o demo && ./demo
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_SETS 4096u
static struct { uint32_t B, S, ob; uint8_t v[MAX_SETS]; uint32_t tag[MAX_SETS]; uint64_t acc, miss; } c;

static void init(uint32_t C, uint32_t B) {
    memset(&c, 0, sizeof c);
    c.B = B; c.S = C / B;
    while ((1u << c.ob) < B) c.ob++;
}
static void access_(uint32_t a) {
    uint32_t line = a >> c.ob, idx = line & (c.S - 1), tag = line / c.S;
    c.acc++;
    if (c.v[idx] && c.tag[idx] == tag) return;
    c.miss++; c.v[idx] = 1; c.tag[idx] = tag;
}
static double hr(void) { return c.acc ? 100.0 * (double)(c.acc - c.miss) / (double)c.acc : 0.0; }

static uint32_t sets_used(uint32_t base, uint32_t count, uint32_t stride) {
    static uint8_t m[MAX_SETS]; memset(m, 0, sizeof m);
    uint32_t k = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = ((base + i * stride) >> c.ob) & (c.S - 1);
        if (!m[idx]) { m[idx] = 1; k++; }
    }
    return k;
}

static double matrix(uint32_t C, uint32_t B, uint32_t n, uint32_t pitch, int by_col) {
    init(C, B);
    for (uint32_t o = 0; o < n; o++)
        for (uint32_t in = 0; in < n; in++) {
            uint32_t i = by_col ? in : o, j = by_col ? o : in;
            access_(0x20000000u + i * pitch + j * 4);
        }
    return hr();
}

int main(void) {
    const uint32_t C = 4096, B = 32, base = 0x20000000u;

    printf("== 1. stride sweep (C = %u B, B = %u B, direct-mapped, %u lines) ==\n", C, B, C / B);
    printf("stride(B)  stream 64KB hit%%  | loop 64 elems x4: lines  sets  misses  hit%%\n");
    for (uint32_t s = 4; s <= 8192; s *= 2) {
        init(C, B);
        for (uint32_t a = 0; a < 65536; a += s) access_(base + a);
        double stream = hr();
        init(C, B);
        for (int p = 0; p < 4; p++)
            for (uint32_t k = 0; k < 64; k++) access_(base + k * s);
        uint32_t lines = (63 * s) / B + 1 < 64 ? (63 * s) / B + 1 : 64;
        printf("%9u  %16.1f  | %24u  %4u  %6llu  %5.1f\n", s, stream, lines, sets_used(base, 64, s),
               (unsigned long long)c.miss, hr());
    }

    printf("\n== 2. 64x64 int32 matrix (16 KB), C = 4 KB, B = 32 B: hit%% by traversal and row pitch ==\n");
    printf("pitch(B)  pad(B)   row-major  col-major\n");
    static const uint32_t pitches[] = {256, 260, 272, 288, 320, 384, 512};
    for (unsigned k = 0; k < sizeof pitches / sizeof pitches[0]; k++)
        printf("%8u  %6u  %9.1f  %9.1f\n", pitches[k], pitches[k] - 256,
               matrix(C, B, 64, pitches[k], 0), matrix(C, B, 64, pitches[k], 1));

    printf("\n== 3. fixing the column walk (B = 32): grow the cache vs pad one line ==\n");
    printf("C(KB)  col pitch 256  col pitch 288\n");
    for (uint32_t cap = 4096; cap <= 32768; cap *= 2)
        printf("%5u  %13.1f  %13.1f\n", cap / 1024, matrix(cap, B, 64, 256, 1), matrix(cap, B, 64, 288, 1));

    printf("\n== 4. line-size sweep, C = 4 KB fixed (pitch = 256 + B) ==\n");
    printf("B(B)  lines  row-major  col-major\n");
    for (uint32_t b = 8; b <= 256; b *= 2)
        printf("%4u  %5u  %9.1f  %9.1f\n", b, C / b, matrix(C, b, 64, 256 + b, 0), matrix(C, b, 64, 256 + b, 1));
    return 0;
}
