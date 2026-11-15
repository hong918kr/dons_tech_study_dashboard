/* ARM-M02 demo_constraints.c — 제약이 asm 텍스트에 "무엇으로 찍히는지" 와, 틀린 제약의 컴파일 에러 (main 없음)
 *   cc  -std=c11 -O2 -S demo_constraints.c -o -                 (clang: 어떻게 찍히나)
 *   cc  -std=c11 -O2 -c -DERR_IMM demo_constraints.c            (I/K/L 범위 밖, 변수를 I 에)
 *   cc  -std=c11 -O2 -c -DERR_W   demo_constraints.c            ("w" 에 수정자 없음 → v0)
 *   VM: gcc -std=gnu11 -O2 -S demo_constraints.c -o - ; gcc -O2 -c -DERR_M demo_constraints.c   ("m" + ldar)
 */
#include <stdint.h>

uint64_t imm_I_shift(uint64_t x) { uint64_t r; __asm__("add %x0, %x1, %2" : "=r"(r) : "r"(x), "I"(0x1000)); return r; }
uint32_t imm_K(uint32_t x) { uint32_t r; __asm__("and %w0, %w1, %2" : "=r"(r) : "r"(x), "K"(0xFF00FF00u)); return r; }
void     z_store(uint32_t *p) { __asm__ volatile("str %w1, %0" : "=Q"(*p) : "rZ"(0u)); }      /* gcc: wzr */
uint64_t mem_m(const uint64_t *p) { uint64_t r; __asm__("ldr %x0, %1" : "=r"(r) : "m"(p[3])); return r; }
uint64_t mem_Q(const uint64_t *p) { uint64_t r; __asm__("ldar %x0, %1" : "=r"(r) : "Q"(p[3])); return r; }
float    f_abs(float a) { float r; __asm__("fabs %s0, %s1" : "=w"(r) : "w"(a)); return r; }

#ifdef ERR_IMM
uint64_t bad_I(uint64_t x) { uint64_t r; __asm__("add %x0, %x1, %2" : "=r"(r) : "r"(x), "I"(4097)); return r; }
uint32_t bad_K(uint32_t x) { uint32_t r; __asm__("and %w0, %w1, %2" : "=r"(r) : "r"(x), "K"(0x12345678u)); return r; }
uint64_t bad_L(uint64_t x) { uint64_t r; __asm__("and %x0, %x1, %2" : "=r"(r) : "r"(x), "L"(0ull)); return r; }
uint64_t var_I(uint64_t x, uint64_t k) { uint64_t r; __asm__("add %x0, %x1, %2" : "=r"(r) : "r"(x), "I"(k)); return r; }
#endif
#ifdef ERR_W
double bad_w(double a, double b, double c) { double r; __asm__("fmadd %0, %1, %2, %3" : "=w"(r) : "w"(a), "w"(b), "w"(c)); return r; }
#endif
#ifdef ERR_M
uint64_t bad_m(const uint64_t *p) { uint64_t r; __asm__("ldar %x0, %1" : "=r"(r) : "m"(p[3])); return r; }
#endif
