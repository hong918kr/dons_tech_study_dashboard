/* ARM-B03 shared_ref.S — 진짜 어셈블러가 만든 기계어를 "정답지"로 쓰는 표.
 * 이 파일의 명령어는 절대 실행하지 않는다. C 하네스가 이 라벨들을 uint32_t 배열로 읽어서
 * enc_movz/enc_movk/enc_movn, is_logical_imm64/32 의 결과와 비교한다.
 * (코드 영역 = __TEXT,__text / .text 는 읽기+실행 가능이라 데이터처럼 읽어도 된다.)
 * Mach-O(맥)와 ELF(리눅스) 둘 다 빌드되도록 SYM/TYPE/SIZE 매크로를 쓴다.
 */
#ifdef __APPLE__
#  define SYM(n)  _##n
#  define DATA(n)
#  define SIZE(n)
#else
#  define SYM(n)  n
#  define DATA(n) .type n, %object
#  define SIZE(n) .size n, .-n
#endif

    .text
    .p2align 2

/* ---- MOVZ/MOVK/MOVN 64-bit (sf=1) 참조 9개: C 쪽 표와 같은 순서 ---- */
    .globl SYM(ref_movs)
    .globl SYM(ref_movs_end)
    DATA(ref_movs)
SYM(ref_movs):
    movz x0,  #0x1234                 // rd=0  imm=0x1234 hw=0
    movz x1,  #0xbeef, lsl #16        // rd=1  imm=0xbeef hw=1
    movz x30, #0xffff, lsl #48        // rd=30 imm=0xffff hw=3
    movk x2,  #0xdead, lsl #32        // rd=2  imm=0xdead hw=2
    movk x9,  #0                      // rd=9  imm=0      hw=0
    movk x17, #0x8000, lsl #48        // rd=17 imm=0x8000 hw=3
    movn x3,  #0                      // rd=3  -> x3 = ~0
    movn x4,  #0x1, lsl #16           // rd=4  -> x4 = ~0x10000
    movn x5,  #0xedcb, lsl #32        // rd=5
    SIZE(ref_movs)
SYM(ref_movs_end):

/* ---- 32-bit (sf=0, w 레지스터) 참조 4개: 추가 연습 1 용 ---- */
    .globl SYM(ref_movs32)
    .globl SYM(ref_movs32_end)
    DATA(ref_movs32)
SYM(ref_movs32):
    movz w0,  #0xbeef                 // rd=0  hw=0
    movk w0,  #0xdead, lsl #16        // rd=0  hw=1
    movn w7,  #0x5678                 // rd=7  -> w7 = 0xffffa987
    movz w29, #0x900, lsl #16         // rd=29 -> 0x09000000
    SIZE(ref_movs32)
SYM(ref_movs32_end):

/* ---- 논리 즉시값 (AND/ORR/EOR/ANDS #imm) 참조 13개 ---- */
    .globl SYM(ref_logic)
    .globl SYM(ref_logic_end)
    DATA(ref_logic)
SYM(ref_logic):
    and  x0,  x1,  #0xff00                    // e=64: 8 ones starting at bit 8
    orr  x2,  x3,  #0x00ff00ff00ff00ff        // period 16
    eor  x4,  x5,  #0x5555555555555555        // period 2
    ands x6,  x7,  #0x8000000000000001        // run of 2 wrapping around bit 63
    and  x8,  x9,  #0xfffffffffffffffe        // 63 ones
    and  x10, x11, #0x0000000100000001        // period 32, one bit
    orr  x12, x13, #0x7fc0                    // STM32 PLLN field [14:6]
    and  x14, x15, #0xffffffff00000000        // high half
    and  x16, x17, #0x0f0f0f0f0f0f0f0f        // period 8
    and  w0,  w1,  #0xff00                    // 32-bit
    orr  w2,  w3,  #0x80000001                // 32-bit wrap-around run
    and  w4,  w5,  #0xffff803f                // ~0x7fc0 in 32 bits
    eor  w6,  w7,  #0x0f0f0f0f                // 32-bit, period 8
    SIZE(ref_logic)
SYM(ref_logic_end):
