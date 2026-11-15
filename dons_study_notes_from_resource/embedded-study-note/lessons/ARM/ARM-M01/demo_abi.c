/* ARM-M01 demo_abi.c — 호출자/피호출자 asm 을 Apple(arm64-apple-macos) vs 표준(aarch64-linux-gnu) 으로 비교 (실행 안 함).
 * run: bash demo_abi.sh  → demo_abi_apple_vs_linux.txt */
#include <stdint.h>
typedef struct { int64_t a, b, c, d; } big32;
typedef struct { int64_t lo, hi; } pair16;
typedef struct { float a, b, c, d; } quat4;
typedef struct { float v[5]; } f5;
long sum10(long,long,long,long,long,long,long,long,long,long);
long mixed_stack(long,long,long,long,long,long,long,long,signed char,short,int,long);
big32 make_big(int64_t seed);
pair16 make_pair(int64_t x);
quat4 hfa_scale(quat4 q, float k);
float f5_sum(f5 v);
f5 f5_make(float x);
long call_sum10(void){ return sum10(1,2,3,4,5,6,7,8,9,10); }
long call_mixed(void){ return mixed_stack(1,2,3,4,5,6,7,8,-3,-1000,-100000,0x123456789L); }
long callee_mixed(long a0,long a1,long a2,long a3,long a4,long a5,long a6,long a7,signed char c,short s,int i,long l){ (void)a0;(void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7; return c+s+i+l; }
int64_t call_big(void){ big32 b = make_big(5); return b.a + b.d; }
int64_t call_pair(void){ pair16 p = make_pair(5); return p.lo ^ p.hi; }
float call_hfa(void){ quat4 q = {1,2,3,4}; quat4 r = hfa_scale(q, 2.0f); return r.a + r.d; }
float call_f5(f5 *p){ return f5_sum(*p); }
float call_f5make(void){ f5 r = f5_make(1.5f); return r.v[4]; }
int printf(const char*, ...);
void call_var(void){ printf("%d %d\n", 1, 2); }
typedef struct { int32_t r, g, b; } rgb12;
quat4 quat_add3(quat4 a, quat4 b, quat4 c);
rgb12 make_rgb(int32_t r, int32_t g, int32_t b);
f5 f5_scale(f5 v, float k);
long mixed_iv(long,long,long,long,long,long,long,long,double,long);
long vsum(long n, ...);
float call_add3(void){ quat4 a={1,2,3,4}, b={5,6,7,8}, c={9,10,11,12}; quat4 r = quat_add3(a,b,c); return r.a; }
float callee_add3(quat4 a, quat4 b, quat4 c){ return a.a + b.b + c.c + c.d; }
int32_t call_rgb(void){ rgb12 x = make_rgb(1,2,3); return x.r + x.b; }
rgb12 callee_rgb(int32_t r, int32_t g, int32_t b){ rgb12 x = {r,g,b}; return x; }
float call_f5s(f5 *p){ f5 r = f5_scale(*p, 3.0f); return r.v[0]; }
long call_iv(void){ return mixed_iv(1,2,3,4,5,6,7,8,9.5,10); }
long call_vsum(void){ return vsum(3, 10L, 20L, 30L); }
