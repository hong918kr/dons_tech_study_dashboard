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
    mov     w0, #0                  // TODO: UBFX 로 bits 7:4 꺼내기
    ret
ENDF stat_get_state

// int32_t stat_get_temp(uint32_t stat)     부호 있는 8비트 필드 → int32
FUNC stat_get_temp
    mov     w0, #0                  // TODO: SBFX 로 bits 23:16 꺼내고 부호 확장
    ret
ENDF stat_get_temp

// uint32_t stat_prep_state(uint32_t state) == FIELD_PREP(0xF0, state)
FUNC stat_prep_state
    mov     w0, #0                  // TODO: UBFIZ 로 (state & 0xF) << 4
    ret
ENDF stat_prep_state

// uint32_t stat_set_state(uint32_t stat, uint32_t state) == FIELD_REPLACE(0xF0, stat, state)
FUNC stat_set_state
                                    // TODO: BFI 로 w0[7:4] = w1[3:0] (지금은 stat 그대로 반환)
    ret
ENDF stat_set_state

// uint64_t field_get_var(uint64_t reg, unsigned lsb, unsigned width)   1<=width<=64, lsb+width<=64
// UBFX 의 lsb/width 는 "즉시값" 뿐 → 실행 중에 정해지는 위치는 LSR(가변) + AND 마스크로.
FUNC field_get_var
    mov     x0, #0                  // TODO: LSR(가변) + AND 마스크. width=64 도 맞아야 한다
    ret
ENDF field_get_var

// ======================= Part B — RBIT / CLZ / REV =======================

// uint32_t rbit32(uint32_t x)
FUNC rbit32
    mov     w0, #0                  // TODO: RBIT
    ret
ENDF rbit32

// uint64_t rbit64(uint64_t x)
FUNC rbit64
    mov     x0, #0                  // TODO: RBIT (64비트)
    ret
ENDF rbit64

// int hsb32(uint32_t x)   최상위 1비트 위치, x == 0 → -1
FUNC hsb32
    mov     w0, #0                  // TODO: 31 - CLZ, x==0 → -1
    ret
ENDF hsb32

// int hsb64(uint64_t x)   x == 0 → -1
FUNC hsb64
    mov     w0, #0                  // TODO: 63 - CLZ, x==0 → -1
    ret
ENDF hsb64

// uint32_t nibrev32(uint32_t x)   0x12345678 → 0x87654321
FUNC nibrev32
    mov     w0, #0                  // TODO: REV + 바이트마다 니블 교환
    ret
ENDF nibrev32

// uint32_t bswap32(uint32_t x)
FUNC bswap32
    mov     w0, #0                  // TODO: REV
    ret
ENDF bswap32

// uint16_t bswap16(uint16_t x)    x 의 상위 16비트가 쓰레기여도 결과는 깨끗
FUNC bswap16
    mov     w0, #0                  // TODO: REV 후 LSR #16 (상위 쓰레기 무시)
    ret
ENDF bswap16

// ======================= Part C — 펌웨어 =======================

// void stat_decode(uint32_t stat, nfc_stat_t *out)   out: ready,ecc_fail,state,errcnt,temp,ver (바이트 0..5)
FUNC stat_decode
                                    // TODO: 필드 6개를 꺼내 strb 로 out[0..5] 에 저장
    ret
ENDF stat_decode

// uint32_t reg_clrset(uint32_t reg, uint32_t clr, uint32_t set)   (reg & ~clr) | set
FUNC reg_clrset
                                    // TODO: BIC + ORR (지금은 reg 그대로 반환)
    ret
ENDF reg_clrset

// uint32_t stat_patch(uint32_t stat, uint32_t new_state)   STATE=new, ERRCNT=0, ECC_FAIL=0
FUNC stat_patch
                                    // TODO: BFI + BFC + AND 즉시값 (지금은 stat 그대로)
    ret
ENDF stat_patch

// uint32_t cdb10_lba(const uint8_t *cdb)   SCSI READ(10): LBA = bytes 2..5, big-endian
FUNC cdb10_lba
    mov     w0, #0                  // TODO: LDUR + REV
    ret
ENDF cdb10_lba

// uint16_t cdb10_len(const uint8_t *cdb)   transfer length = bytes 7..8, big-endian
FUNC cdb10_len
    mov     w0, #0                  // TODO: LDURH + REV16
    ret
ENDF cdb10_len

// void cdb10_build(uint8_t *cdb, uint32_t lba, uint16_t len)
FUNC cdb10_build
                                    // TODO: 10바이트 0 채우기, opcode 0x28, REV/REV16 후 STUR/STURH
    ret
ENDF cdb10_build
