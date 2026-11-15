/* ARM-M01 demo_szero.c — demo_szero.S 드라이버. build: cc -O2 demo_szero.c demo_szero.S -o /tmp/ARM-M01_szero */
#include <stdio.h>
#include <stdint.h>
void zt(uint64_t,uint64_t,uint64_t*);
int main(void){uint64_t r[2]; zt(0,0,r); printf("after fmov s16,#1.0: d16=0x%016llx upper=0x%016llx\n",(unsigned long long)r[0],(unsigned long long)r[1]);}
