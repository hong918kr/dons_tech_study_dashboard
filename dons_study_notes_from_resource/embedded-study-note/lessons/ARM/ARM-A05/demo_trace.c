/* ARM-A05 demo_trace.c — 커널 한 블록을 손계산 가능한 값으로 돌려 본다 (5장 표의 실제 출력)
 * 빌드: cc -std=c11 -O2 demo_trace.c solution.S -o /tmp/ARM-A05_trace && /tmp/ARM-A05_trace */
#include <stdio.h>
#include <stdint.h>
extern void sdot_4x4_kernel(const int8_t *, const int8_t *, size_t, int32_t *, size_t);
extern void smmla_8x8_kernel(const int8_t *, const int8_t *, size_t, int32_t *, size_t);
int main(void){
  int8_t Ap[16]={0,1,2,3,10,11,12,13,20,21,22,23,30,31,32,33};
  int8_t Bp[16]={1,1,1,1, 2,2,2,2, 3,3,3,3, -1,-1,-1,-1};
  int32_t C[16];
  sdot_4x4_kernel(Ap,Bp,1,C,4);
  for(int r=0;r<4;r++) printf("row %d: %d %d %d %d\n",r,C[4*r],C[4*r+1],C[4*r+2],C[4*r+3]);
  /* smmla one block: A rows i: bytes = i+1 (k=0..7); B cols j: bytes = (k&1? -1:1)*(j+1) */
  int8_t A8[64],B8[64]; int32_t C8[64];
  for(int i=0;i<8;i++)for(int k=0;k<8;k++)A8[i*8+k]=(int8_t)(i+1);
  for(int j=0;j<8;j++)for(int k=0;k<8;k++)B8[j*8+k]=(int8_t)(k<3? (j+1):0);
  smmla_8x8_kernel(A8,B8,1,C8,8);
  for(int r=0;r<2;r++){printf("smmla row %d:",r);for(int c=0;c<8;c++)printf(" %d",C8[r*8+c]);printf("\n");}
}
