/* ARM-B01 demo_abi_diff.c — 같은 C 를 Apple arm64 와 AAPCS64(Linux) 로 컴파일해서 ABI 차이를 asm 으로 본다
 * 실행: bash demo_abi_diff.sh   (clang -S 만 하므로 헤더 없이 프로토타입만 선언)
 */
int printf(const char *, ...);
void use_char(signed char c);
void sink(long *p);

/* ① 가변 인자: Apple 은 전부 스택, AAPCS64 는 고정 인자처럼 x1, x2 */
void call_printf(long a, long b) { printf("%ld %ld\n", a, b); }

/* ② char 의 부호: Apple char = signed, AAPCS64 char = unsigned.
 *    작은 정수 인자 확장: Apple 은 "호출자"가 32비트로 확장, AAPCS64 는 "피호출자"가 확장 */
int take_char(char c) { return c; }
int take_schar(signed char c) { return c + 1; }
void call_char(int v) { use_char((signed char)v); }

/* ③ long double: Apple = double(8B), AAPCS64 = IEEE quad(16B) */
int ld_size(void) { return (int)sizeof(long double); }

/* ④ x18: 레지스터를 잔뜩 쓰게 만들어 할당기가 x18 을 쓰는지 본다 */
void pressure(long *p) {
    long a0=p[0],a1=p[1],a2=p[2],a3=p[3],a4=p[4],a5=p[5],a6=p[6],a7=p[7],
         a8=p[8],a9=p[9],a10=p[10],a11=p[11],a12=p[12],a13=p[13],a14=p[14],a15=p[15],
         a16=p[16],a17=p[17],a18=p[18],a19=p[19],a20=p[20],a21=p[21],a22=p[22],a23=p[23];
    p[0]=a23*a1; p[1]=a22*a2; p[2]=a21*a3; p[3]=a20*a4; p[4]=a19*a5; p[5]=a18*a6;
    p[6]=a17*a7; p[7]=a16*a8; p[8]=a15*a9; p[9]=a14*a10; p[10]=a13*a11; p[11]=a12*a0;
    p[12]=a0+a1+a2+a3+a4+a5+a6+a7+a8+a9+a10+a11+a12+a13+a14+a15+a16+a17+a18+a19+a20+a21+a22+a23;
}
