/* ARM-B11 demo: FJCVTZS (ARMv8.3 FEAT_JSCVT, JavaScript ToInt32 의미: 2^32 로 감싸기) vs FCVTZS (포화)
 *   cc -std=c11 -O2 demo_fjcvtzs.c -o dfj && ./dfj
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
static int32_t fj(double d){ int32_t r; __asm__("fjcvtzs %w0, %d1" : "=r"(r) : "w"(d) : "cc"); return r; }
static int32_t zs(double d){ int32_t r; __asm__("fcvtzs %w0, %d1" : "=r"(r) : "w"(d)); return r; }
int main(void){ double v[]={3e9, -3e9, 4294967297.5, NAN, INFINITY, -2.7}; for(int i=0;i<6;i++) printf("%-14g fjcvtzs=%d fcvtzs=%d\n", v[i], fj(v[i]), zs(v[i])); return 0;}
