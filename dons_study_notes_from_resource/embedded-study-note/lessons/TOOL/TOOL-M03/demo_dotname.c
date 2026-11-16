/* ENV: linux — demo_elf.sh 가 VM 에서 빌드한다 (링크 실패가 정상 결과). */
/* TOOL-M03 demo: 점으로 시작하는 섹션 이름에는 __start_ 가 생기지 않는다. */
#include <stdio.h>
static const int v __attribute__((used, section(".shellcmds"))) = 7;
extern const int __start_shellcmds[];
int main(void) { printf("%p\n", (const void *)__start_shellcmds); return 0; }
