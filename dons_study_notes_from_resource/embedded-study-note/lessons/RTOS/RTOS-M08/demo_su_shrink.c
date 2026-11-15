/* RTOS-M08 데모 — .su 숫자가 "버퍼 크기" 와 다를 때 (freestanding, 링크 안 함: .su 와 asm 만 본다)
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -O2 -fstack-usage -c demo_su_shrink.c -o /tmp/RTOS-M08_shrink.o
 *   cat /tmp/RTOS-M08_shrink.su       (-O0 으로도 돌려 비교. LLVM IR 은 -S -emit-llvm -o -)
 */
#include <stdint.h>

void sink(char *p);

/* ① 오케스트레이터가 본 4 바이트: b[0] 만 쓰니 나머지 63 B 는 죽은 메모리 → SROA 가 alloca 를 1 B 로 쪼갠다 */
int only_first(int x)      { volatile char b[64]; b[0] = (char)x; return b[0]; }
/* ② 인덱스가 변수 → 어느 칸이든 될 수 있으니 64 B 전체가 남는다 */
int any_index(int x)       { volatile char b[64]; b[x & 63] = (char)x; return b[(x + 1) & 63]; }
/* ③ 주소가 밖으로 나간다(escape) → 컴파일러는 sink 가 64 B 를 다 볼 수 있다고 가정 */
int escapes(int x)         { char b[64]; b[0] = (char)x; sink(b); return b[1]; }
/* ④ 안 쓰는 큰 배열은 아예 사라진다 (volatile 도 아님) */
int unused_big(int x)      { char b[1024]; (void)b; return x + 1; }
/* ⑤ VLA: 크기를 실행 중에 정한다 → .su 한정자가 dynamic (상한 없음 → 예산 도구는 에러로 처리해야) */
int vla(int n)             { char b[n]; sink(b); return b[0]; }
/* ⑥ 크기를 마스크로 묶어도 bounded 가 안 나온다 (clang 21 · arm-none-eabi-gcc 14.2 둘 다 dynamic 으로 봤다 — md) */
int vla_masked(int n)      { char b[(n & 31) + 1]; sink(b); return b[0]; }
