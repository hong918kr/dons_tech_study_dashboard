/* ARM-B06 비트 연산 명령: BIC, UBFX, BFI, CLZ, RBIT — AArch64 asm (모두 leaf, 스택 안 씀)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 harness.c {starter|solution}.S shared_bugs.S -o /tmp/ARM-B06_v && /tmp/ARM-B06_v
 * 같은 파일이 macOS(Mach-O)와 Linux(ELF) 둘 다에서 빌드된다 — FUNC/ENDF 매크로가 차이를 숨긴다.
 */
#ifdef __APPLE__
    .macro FUNC name            // Mach-O: C 이름 앞에 밑줄(_), .type/.size 없음
    .text
    .globl _\name
    .p2align 2
_\name:
    .endm
    .macro ENDF name
    .endm
#else
    .macro FUNC name            // ELF: 이름 그대로 + 심볼 타입/크기
    .text
    .global \name
    .type \name, %function
    .p2align 2
\name:
    .endm
    .macro ENDF name
    .size \name, . - \name
    .endm
#endif

// ======================= Part A — 필드 추출/삽입 =======================
// NFC_STAT: [0] READY [1] ECC_FAIL [7:4] STATE [15:8] ERRCNT [23:16] TEMP(signed) [31:28] VER

// uint32_t stat_get_state(uint32_t stat)   == FIELD_GET(0xF0, stat)
FUNC stat_get_state
    ubfx    w0, w0, #4, #4          // w0 = (stat >> 4) & 0xF   (= UBFM w0,w0,#4,#7)
    ret
ENDF stat_get_state

// int32_t stat_get_temp(uint32_t stat)     부호 있는 8비트 필드 → int32
FUNC stat_get_temp
    sbfx    w0, w0, #16, #8         // bits 23:16 꺼내고 bit 23 을 위로 복사 (부호 확장)
    ret
ENDF stat_get_temp

// uint32_t stat_prep_state(uint32_t state) == FIELD_PREP(0xF0, state)
FUNC stat_prep_state
    ubfiz   w0, w0, #4, #4          // w0 = (state & 0xF) << 4, 나머지 0
    ret
ENDF stat_prep_state

// uint32_t stat_set_state(uint32_t stat, uint32_t state) == FIELD_REPLACE(0xF0, stat, state)
FUNC stat_set_state
    bfi     w0, w1, #4, #4          // w0[7:4] = w1[3:0], 다른 비트는 그대로
    ret
ENDF stat_set_state

// uint64_t field_get_var(uint64_t reg, unsigned lsb, unsigned width)   1<=width<=64, lsb+width<=64
// UBFX 의 lsb/width 는 "즉시값" 뿐 → 실행 중에 정해지는 위치는 LSR(가변) + AND 마스크로.
FUNC field_get_var
    lsr     x0, x0, x1              // LSRV: 시프트 양 = x1 mod 64 (하위 6비트만 씀)
    neg     w2, w2                  // w2 = -width → mod 64 로 보면 64-width
    mov     x3, #-1                 // x3 = 0xFFFF_FFFF_FFFF_FFFF
    lsr     x3, x3, x2              // mask = ~0 >> (64-width), width=64 → 시프트 0 → 전부 1
    and     x0, x0, x3
    ret
ENDF field_get_var

// ======================= Part B — RBIT / CLZ / REV =======================

// uint32_t rbit32(uint32_t x)
FUNC rbit32
    rbit    w0, w0                  // bit i → bit 31-i
    ret
ENDF rbit32

// uint64_t rbit64(uint64_t x)
FUNC rbit64
    rbit    x0, x0                  // bit i → bit 63-i
    ret
ENDF rbit64

// int hsb32(uint32_t x)   최상위 1비트 위치, x == 0 → -1
FUNC hsb32
    clz     w0, w0                  // 앞쪽 0의 개수 (x==0 → 32, ISA 가 정의)
    mov     w1, #31
    sub     w0, w1, w0              // 31 - clz  (x==0 → -1 이 공짜로 나온다)
    ret
ENDF hsb32

// int hsb64(uint64_t x)   x == 0 → -1
FUNC hsb64
    clz     x0, x0                  // x==0 → 64
    mov     w1, #63
    sub     w0, w1, w0              // 63 - clz (int 이므로 w 로 계산)
    ret
ENDF hsb64

// uint32_t nibrev32(uint32_t x)   0x12345678 → 0x87654321
FUNC nibrev32
    rev     w0, w0                  // 바이트 역순: 0x78563412
    and     w1, w0, #0x0f0f0f0f     // 각 바이트의 아래 니블: 0x08060402
    lsr     w0, w0, #4              // 위 니블을 아래로:      0x07856341
    and     w0, w0, #0x0f0f0f0f     //                        0x07050301
    orr     w0, w0, w1, lsl #4      // 아래 니블은 위로 올려 합친다: 0x87654321
    ret
ENDF nibrev32

// uint32_t bswap32(uint32_t x)
FUNC bswap32
    rev     w0, w0                  // b3 b2 b1 b0 → b0 b1 b2 b3
    ret
ENDF bswap32

// uint16_t bswap16(uint16_t x)    x 의 상위 16비트가 쓰레기여도 결과는 깨끗
FUNC bswap16
    rev     w0, w0                  // [b3 b2 b1 b0] → [b0 b1 b2 b3]
    lsr     w0, w0, #16             // → [0 0 b0 b1]  : 바이트 교환 + 0 확장
    ret
ENDF bswap16

// ======================= Part C — 펌웨어 =======================

// void stat_decode(uint32_t stat, nfc_stat_t *out)   out: ready,ecc_fail,state,errcnt,temp,ver (바이트 0..5)
FUNC stat_decode
    and     w2, w0, #1              // READY
    strb    w2, [x1]
    ubfx    w2, w0, #1, #1          // ECC_FAIL
    strb    w2, [x1, #1]
    ubfx    w2, w0, #4, #4          // STATE
    strb    w2, [x1, #2]
    ubfx    w2, w0, #8, #8          // ERRCNT
    strb    w2, [x1, #3]
    lsr     w2, w0, #16             // TEMP: strb 는 하위 8비트만 저장 → 부호 확장 불필요
    strb    w2, [x1, #4]
    lsr     w2, w0, #28             // VER (최상위 필드는 LSR 하나로 끝)
    strb    w2, [x1, #5]
    ret
ENDF stat_decode

// uint32_t reg_clrset(uint32_t reg, uint32_t clr, uint32_t set)   (reg & ~clr) | set
FUNC reg_clrset
    bic     w0, w0, w1              // reg & ~clr  (NOT 이 명령 안에 들어 있다)
    orr     w0, w0, w2              // | set
    ret
ENDF reg_clrset

// uint32_t stat_patch(uint32_t stat, uint32_t new_state)   STATE=new, ERRCNT=0, ECC_FAIL=0
FUNC stat_patch
    bfi     w0, w1, #4, #4          // STATE ← new_state[3:0]
    bfc     w0, #8, #8              // ERRCNT ← 0  (= BFI w0, wzr, #8, #8)
    and     w0, w0, #0xfffffffd     // ECC_FAIL ← 0 (논리 즉시값, bic w0,w0,#2 로 써도 같은 명령)
    ret
ENDF stat_patch

// uint32_t cdb10_lba(const uint8_t *cdb)   SCSI READ(10): LBA = bytes 2..5, big-endian
FUNC cdb10_lba
    ldur    w0, [x0, #2]            // 정렬 안 된 4바이트 로드 (일반 메모리에선 허용)
    rev     w0, w0                  // BE → 호스트(LE)
    ret
ENDF cdb10_lba

// uint16_t cdb10_len(const uint8_t *cdb)   transfer length = bytes 7..8, big-endian
FUNC cdb10_len
    ldurh   w0, [x0, #7]            // 2바이트 로드, 상위 16비트는 0
    rev16   w0, w0                  // 각 halfword 안에서 바이트 교환 (상위 halfword 는 0 그대로)
    ret
ENDF cdb10_len

// void cdb10_build(uint8_t *cdb, uint32_t lba, uint16_t len)
FUNC cdb10_build
    str     xzr, [x0]               // bytes 0..7 = 0
    strh    wzr, [x0, #8]           // bytes 8..9 = 0
    mov     w3, #0x28               // opcode READ(10)
    strb    w3, [x0]
    rev     w1, w1                  // LBA → big-endian
    stur    w1, [x0, #2]
    rev16   w2, w2                  // len 의 하위 halfword 바이트 교환
    sturh   w2, [x0, #7]            // 하위 16비트만 저장 → len 상위 쓰레기 무관
    ret
ENDF cdb10_build
