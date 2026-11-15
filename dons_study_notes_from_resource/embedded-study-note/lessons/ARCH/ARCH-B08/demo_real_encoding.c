/* ARCH-B08 demo: 실제 ISA 는 같은 일을 몇 비트로 인코딩하나?
 * toy ISA 의 ADD / ADDI / LD / BEQ 루프와 같은 모양의 C 함수를
 * Thumb-2 (Cortex-M4, 16/32비트 혼합) 와 AArch64 (32비트 고정) 로 컴파일해서
 * llvm-objdump 로 기계어 워드를 본다. freestanding: 헤더 없음.
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O1 -c demo_real_encoding.c -o /tmp/esn_ARCH-B08/t.o
 *   xcrun llvm-objdump -d /tmp/esn_ARCH-B08/t.o
 *   cc -O1 -c demo_real_encoding.c -o /tmp/esn_ARCH-B08/a.o && xcrun llvm-objdump -d /tmp/esn_ARCH-B08/a.o
 */
int add3(int a, int b) { return a + b; }            /* ADD  r, r, r       */
int addi(int a) { return a - 1; }                    /* ADDI r, r, -1      */
int load4(const int *p) { return p[4]; }             /* LD   r, 4(r) (바이트 주소라 16 B 오프셋) */
int sum(const int *p, int n) {                       /* BEQ 루프 = toy ISA 의 배열 합 */
    int s = 0;
    while (n != 0) { s += *p++; n--; }
    return s;
}
