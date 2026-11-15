/* ARCH-B03 demo: float <-> FP16 / BF16 변환이 각 타깃에서 어떤 명령이 되는가 (freestanding, 헤더 없음).
 * arm64 (M6)      : cc -O2 -S -o - demo_asm.c
 * arm64 + BF16    : cc -O2 -mcpu=apple-m4 -S -o - demo_asm.c
 * Cortex-M4F      : clang --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard -mthumb -O2 -S demo_asm.c -o -
 * Cortex-M0 (FPU 없음): clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_asm.c -o -
 * (BF16 함수는 arm64 에서만 의미가 있다 — Cortex-M 빌드에서는 -DNO_BF16)
 */
_Float16 to_half(float f)    { return (_Float16)f; }
float    to_float(_Float16 h) { return (float)h; }
#ifndef NO_BF16
__bf16   to_bf16(float f)    { return (__bf16)f; }
#endif
