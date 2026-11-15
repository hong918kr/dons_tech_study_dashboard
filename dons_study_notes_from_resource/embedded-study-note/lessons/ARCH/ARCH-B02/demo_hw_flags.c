/* ARCH-B02 demo_hw_flags.c — C 모델의 NZCV 를 진짜 CPU(Apple M6, AArch64) 플래그와 대조
 *
 *   ADDS/SUBS/ADCS/SBCS 를 inline asm 으로 실행하고 `mrs x, nzcv` 로 플래그를 읽는다 (비트 31..28 = N Z C V).
 *   8비트 플래그는 AArch64 에 없으므로 "피연산자를 << 24 해서 32비트로 계산" 트릭으로 하드웨어에서 얻는다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_hw_flags.c -o demo_hw_flags && ./demo_hw_flags
 * (arm64 전용. x86 은 demo_x86_flags.c)
 */
#include <stdint.h>
#include <stdio.h>

#if !defined(__aarch64__)
#error "AArch64 전용 데모 (Apple Silicon 에서 네이티브로 빌드)"
#endif

/* ---- 모델 (solution.c 와 같은 식, 압축본) ---- */
typedef struct { uint32_t res; uint8_t nzcv; } alu_t;
static alu_t m_adc(uint32_t a, uint32_t b, unsigned cin, unsigned w) {
    uint32_t m = (uint32_t)((UINT64_C(1) << w) - 1u);
    a &= m; b &= m;
    uint64_t s = (uint64_t)a + b + (cin & 1u);
    uint32_t r = (uint32_t)s & m;
    unsigned n = (r >> (w - 1)) & 1u, z = r == 0, c = (unsigned)(s >> w) & 1u;
    unsigned v = (((a ^ r) & (b ^ r)) >> (w - 1)) & 1u;
    alu_t o = { r, (uint8_t)(n << 3 | z << 2 | c << 1 | v) };
    return o;
}

/* ---- 하드웨어 ---- */
enum { OP_ADDS, OP_SUBS, OP_ADCS, OP_SBCS };
static alu_t hw32(int op, uint32_t a, uint32_t b, unsigned cin) {
    uint32_t r; uint64_t f, fin = (uint64_t)(cin & 1u) << 29;   /* C = bit 29 */
    switch (op) {
    case OP_ADDS: __asm__ volatile("adds %w0, %w2, %w3\n\tmrs %1, nzcv" : "=&r"(r), "=r"(f) : "r"(a), "r"(b) : "cc"); break;
    case OP_SUBS: __asm__ volatile("subs %w0, %w2, %w3\n\tmrs %1, nzcv" : "=&r"(r), "=r"(f) : "r"(a), "r"(b) : "cc"); break;
    case OP_ADCS: __asm__ volatile("msr nzcv, %4\n\tadcs %w0, %w2, %w3\n\tmrs %1, nzcv"
                                   : "=&r"(r), "=&r"(f) : "r"(a), "r"(b), "r"(fin) : "cc"); break;
    default:      __asm__ volatile("msr nzcv, %4\n\tsbcs %w0, %w2, %w3\n\tmrs %1, nzcv"
                                   : "=&r"(r), "=&r"(f) : "r"(a), "r"(b), "r"(fin) : "cc"); break;
    }
    alu_t o = { r, (uint8_t)(f >> 28) };
    return o;
}
static alu_t model(int op, uint32_t a, uint32_t b, unsigned cin, unsigned w) {
    switch (op) {
    case OP_ADDS: return m_adc(a, b, 0, w);
    case OP_SUBS: return m_adc(a, ~b, 1, w);
    case OP_ADCS: return m_adc(a, b, cin, w);
    default:      return m_adc(a, ~b, cin, w);
    }
}

static uint32_t rng = 0x2545F491u;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static const char *name[] = { "ADDS", "SUBS", "ADCS", "SBCS" };
static void fl(char *s, uint8_t f) { s[0] = f & 8 ? 'N' : '-'; s[1] = f & 4 ? 'Z' : '-'; s[2] = f & 2 ? 'C' : '-'; s[3] = f & 1 ? 'V' : '-'; s[4] = 0; }

int main(void) {
    static const uint32_t edge[] = { 0, 1, 2, 0x7FFFFFFE, 0x7FFFFFFF, 0x80000000, 0x80000001,
                                     0xFFFFFFFE, 0xFFFFFFFF, 0x40000000, 0xC0000000, 0xFFFF };
    const unsigned NE = sizeof edge / sizeof edge[0];

    printf("sample rows (32-bit)      op    a          b          cin  model       hw\n");
    const struct { int op; uint32_t a, b; unsigned cin; } s[] = {
        { OP_ADDS, 0x7FFFFFFF, 1, 0 }, { OP_ADDS, 0xFFFFFFFF, 1, 0 }, { OP_SUBS, 5, 3, 0 },
        { OP_SUBS, 3, 5, 0 }, { OP_SUBS, 0x1234, 0, 0 }, { OP_SUBS, 0x80000000, 1, 0 },
        { OP_ADCS, 0xFFFFFFFF, 0, 1 }, { OP_SBCS, 0, 0, 0 } };
    for (unsigned i = 0; i < sizeof s / sizeof s[0]; i++) {
        alu_t m = model(s[i].op, s[i].a, s[i].b, s[i].cin, 32), h = hw32(s[i].op, s[i].a, s[i].b, s[i].cin);
        char fm[5], fh[5]; fl(fm, m.nzcv); fl(fh, h.nzcv);
        printf("                          %-5s 0x%08X 0x%08X %u    %08X %s %08X %s %s\n", name[s[i].op],
               s[i].a, s[i].b, s[i].cin, m.res, fm, h.res, fh, (m.res == h.res && m.nzcv == h.nzcv) ? "ok" : "MISMATCH");
    }

    /* 32비트: 경계값 전쌍 + 무작위 1,000,000쌍, 연산 4개, cin 0/1 */
    long n32 = 0, bad32 = 0;
    for (unsigned i = 0; i < NE * NE + 1000000u; i++) {
        uint32_t a = i < NE * NE ? edge[i / NE] : xs32(), b = i < NE * NE ? edge[i % NE] : xs32();
        for (int op = 0; op < 4; op++)
            for (unsigned cin = 0; cin < 2; cin++) {
                alu_t m = model(op, a, b, cin, 32), h = hw32(op, a, b, cin);
                n32++;
                if (m.res != h.res || m.nzcv != h.nzcv) {
                    if (bad32++ < 5) printf("MISMATCH %s a=%08X b=%08X cin=%u\n", name[op], a, b, cin);
                }
            }
    }
    printf("32-bit  : %ld cases, %ld mismatches (model vs mrs nzcv)\n", n32, bad32);

    /* 8비트: 전수 65536쌍, 하드웨어는 (a<<24) op (b<<24) 로 8비트 플래그를 얻는다 */
    long n8 = 0, bad8 = 0;
    for (uint32_t a = 0; a < 256; a++)
        for (uint32_t b = 0; b < 256; b++)
            for (int op = OP_ADDS; op <= OP_SUBS; op++) {
                alu_t m = model(op, a, b, 0, 8), h = hw32(op, a << 24, b << 24, 0);
                n8++;
                if (m.res != h.res >> 24 || m.nzcv != h.nzcv) bad8++;
            }
    printf("8-bit   : %ld cases (ADDS/SUBS, operands << 24), %ld mismatches\n", n8, bad8);
    return (bad32 || bad8) ? 1 : 0;
}
