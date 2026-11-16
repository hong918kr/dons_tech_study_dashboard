/* ARM-A09 shared_pic.S — 전역 변수·외부 심볼에 PC 상대로 접근하는 asm (주어진 코드, TODO 아님)
 * starter / solution / practice 모두 이 파일을 같이 링크한다. 하네스는 아래 site_* 라벨에서
 * "링크가 끝난 실제 명령 워드" 를 읽어 자기 디코더로 주소를 다시 계산해 본다.
 *
 *   Mach-O (macOS):  adrp xN, _sym@PAGE      + add/ldr ..., _sym@PAGEOFF        (직접)
 *                    adrp xN, _sym@GOTPAGE   + ldr xN, [xN, _sym@GOTPAGEOFF]    (GOT 경유)
 *   ELF (Linux):     adrp xN, sym            + add/ldr ..., :lo12:sym           (직접)
 *                    adrp xN, :got:sym       + ldr xN, [xN, :got_lo12:sym]      (GOT 경유)
 * 같은 명령, 다른 표기. 어셈블러는 둘 다 워드의 즉시값 칸을 0 으로 두고 리로케이션을 남긴다.
 */
#ifdef __APPLE__
#define SYM(n) _##n
#define PAGE(s)        PAGE_(s)          /* 2단계 매크로: 인자(EXT_DATA 등)를 먼저 펼친 뒤 _ 를 붙인다 */
#define PAGE_(s)       _##s@PAGE
#define PAGEOFF(s)     PAGEOFF_(s)
#define PAGEOFF_(s)    _##s@PAGEOFF
#define GOTPAGE(s)     GOTPAGE_(s)
#define GOTPAGE_(s)    _##s@GOTPAGE
#define GOTPAGEOFF(s)  GOTPAGEOFF_(s)
#define GOTPAGEOFF_(s) _##s@GOTPAGEOFF
#define EXT_DATA       __stdoutp          /* libSystem 의 데이터 심볼 (stdout 매크로의 실체) */
#else
#define SYM(n) n
#define PAGE(s)        s
#define PAGEOFF(s)     :lo12:s
#define GOTPAGE(s)     :got:s
#define GOTPAGEOFF(s)  :got_lo12:s
#define EXT_DATA       stdout             /* glibc 의 데이터 심볼 */
#endif

.macro FUNC name
    .globl  SYM(\name)
#ifndef __APPLE__
    .type   SYM(\name), %function
#endif
    .p2align 2
SYM(\name):
.endm
.macro SITE name                          // 하네스가 명령 워드를 읽을 수 있게 전역 라벨을 붙인다
    .globl  SYM(\name)
SYM(\name):
.endm

    .text
// long *pic_addr_local(void) : &pic_g   — 같은 이미지 안의 전역 = ADRP + ADD (리로케이션 2개)
FUNC pic_addr_local
SITE site_local
    adrp    x0, PAGE(pic_g)               // x0 = pic_g 가 있는 4 KB 페이지 주소 (PC 상대 ±4 GB)
    add     x0, x0, PAGEOFF(pic_g)        // + 페이지 안 오프셋 (하위 12비트)
    ret

// long pic_load_tab2(void) : pic_tab[2] — 하위 12비트를 LDR 의 오프셋 칸에 바로 넣는다 (8로 나눈 값)
FUNC pic_load_tab2
SITE site_tab
    adrp    x8, PAGE(pic_tab)
    ldr     x0, [x8, PAGEOFF(pic_tab) + 16]   // 덧셈(addend) 16 → Mach-O 는 ARM64_RELOC_ADDEND 가 따로 붙는다
    ret

// long *pic_addr_local_got(void) : &pic_g 를 일부러 GOT 경유로 — 링커가 이걸 어떻게 바꾸는지 본다
FUNC pic_addr_local_got
SITE site_local_got
    adrp    x0, GOTPAGE(pic_g)            // GOT 칸이 있는 페이지
    ldr     x0, [x0, GOTPAGEOFF(pic_g)]   // GOT 칸에서 8 B 주소를 읽는다 (같은 이미지면 링커가 ADD 로 바꿀 수 있음)
    ret

// FILE **pic_addr_ext_got(void) : 다른 이미지(libSystem / libc.so)의 데이터 → 반드시 GOT
FUNC pic_addr_ext_got
SITE site_ext_got
    adrp    x0, GOTPAGE(EXT_DATA)
    ldr     x0, [x0, GOTPAGEOFF(EXT_DATA)]  // 로더(dyld / ld.so)가 채운 GOT 칸 → &__stdoutp
    ret

// int pic_call_abs(int v) : 다른 이미지의 함수 → B(26비트) 는 stub/PLT 로, stub 이 GOT 칸을 읽어 점프
FUNC pic_call_abs
SITE site_call
    b       SYM(abs)                      // 꼬리 호출: BRANCH26 / JUMP26 리로케이션
    // (돌아오지 않음)

    .data
    .p2align 3
    .globl  SYM(pic_g)
SYM(pic_g):
    .quad   0x1234
    .globl  SYM(pic_tab)
SYM(pic_tab):
    .quad   10, 20, 30, 40
    .globl  SYM(pic_ptr)
SYM(pic_ptr):
    .quad   SYM(pic_g)                    // 데이터 안의 절대 주소 → 로드 시 rebase (ELF: R_AARCH64_RELATIVE)

#ifndef __APPLE__
    .section .note.GNU-stack, "", %progbits
#endif
