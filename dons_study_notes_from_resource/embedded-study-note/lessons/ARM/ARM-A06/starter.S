/* ARM-A06 ARMv8 CRC32 명령으로 CRC 가속 — 손으로 쓴 CRC-32C (crc32cb/ch/cw/cx), 맥 Mach-O / 리눅스 ELF 공용
 * uint32_t crc32c_asm(uint32_t crc, const void *p, size_t n)   zlib 모양 API (처음 crc = 0, 이어 부르면 스트리밍)
 * 인자: w0 = crc, x1 = p, x2 = n. 반환 w0. 잎(leaf) 함수: 스택·callee-saved 레지스터 안 씀 (x3 만 임시).
 * 순서: ~crc → 머리(p 를 8 B 정렬로: 1·2·4 B) → 본문 8 B 씩 crc32cx → 꼬리(4·2·1 B) → ~결과.
 * p+n 밖은 절대 읽지 않는다 (머리 단계마다 n 을 먼저 확인, 꼬리는 n 의 비트 2·1·0 만큼만).
 * CRC32 명령은 FEAT_CRC32 (ARMv8.0 선택, v8.1 부터 필수). 리눅스 generic armv8-a 대비 .arch_extension crc.
 */
#ifdef __APPLE__
#define SYM(x) _##x
#define FTYPE(x)
#define FSIZE(x)
#else
#define SYM(x) x
#define FTYPE(x) .type x, %function
#define FSIZE(x) .size x, .-x
#endif

    .arch_extension crc
    .text

    .globl SYM(crc32c_asm)
    FTYPE(crc32c_asm)
    .p2align 4
SYM(crc32c_asm):
    // TODO: mvn 으로 조건화 → 머리(tbz x1,#0/#1/#2 + n 확인, crc32cb/ch/cw) → 본문(ldr x3,[x1],#8 + crc32cx)
    // TODO: → 꼬리(tbz x2,#2/#1/#0) → mvn. 지금은 crc 를 그대로 돌려준다 (안전한 틀린 값).
    ret
    FSIZE(crc32c_asm)
