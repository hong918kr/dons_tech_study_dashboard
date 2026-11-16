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
    mvn     w0, w0                      // 조건화: 상태 = ~crc  (명령 자체는 ~ 를 안 한다)
    cbz     x2, 9f                      // n = 0 → ~~crc = crc
    // ---- 머리: p 의 비트 0·1·2 를 하나씩 지운다 (n 이 모자라면 꼬리로) ----
    tbz     x1, #0, 1f                  // p 홀수?
    ldrb    w3, [x1], #1
    crc32cb w0, w0, w3                  // 1 바이트
    sub     x2, x2, #1
1:  tbz     x1, #1, 2f                  // p & 2 ?
    cmp     x2, #2
    b.lo    7f                          // 남은 게 2 B 미만 → 꼬리 (n < 8 보장)
    ldrh    w3, [x1], #2
    crc32ch w0, w0, w3                  // 2 바이트 (리틀 엔디언: 낮은 주소 바이트가 먼저 들어간다)
    sub     x2, x2, #2
2:  tbz     x1, #2, 3f                  // p & 4 ?
    cmp     x2, #4
    b.lo    7f
    ldr     w3, [x1], #4
    crc32cw w0, w0, w3                  // 4 바이트
    sub     x2, x2, #4
    // ---- 본문: p 는 이제 8 B 정렬 ----
3:  cmp     x2, #8
    b.lo    7f
4:  ldr     x3, [x1], #8                // 정렬된 8 B 로드
    crc32cx w0, w0, x3                  // 8 바이트 (지연 ~3 사이클 → 이 루프는 지연에 묶인다, Part B)
    sub     x2, x2, #8
    cmp     x2, #8
    b.hs    4b
    // ---- 꼬리: 0 ≤ x2 < 8, 비트 2·1·0 순서 = 메모리 순서 ----
7:  tbz     x2, #2, 5f
    ldr     w3, [x1], #4
    crc32cw w0, w0, w3
5:  tbz     x2, #1, 6f
    ldrh    w3, [x1], #2
    crc32ch w0, w0, w3
6:  tbz     x2, #0, 9f
    ldrb    w3, [x1]
    crc32cb w0, w0, w3
9:  mvn     w0, w0                      // xorout ~0
    ret
    FSIZE(crc32c_asm)
