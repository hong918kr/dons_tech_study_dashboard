/* ENV: qemu — BM-B06 공통 플래시/RAM 레이아웃 (boot.ld 와 app.ld 가 INCLUDE 한다)
 *
 * 대상: STM32F407 류 1 MB 플래시 + 128 KB SRAM (RM0090 기준 대표값 — RM 확인 권장).
 * 플래시 섹터 맵: 섹터 0-3 = 16 KB, 섹터 4 = 64 KB, 섹터 5-11 = 128 KB.
 * 모든 영역은 섹터 경계에서 시작하고 섹터 경계에서 끝난다 (erase 단위 = 섹터).
 *
 *   BOOT    0x08000000  32 KB  섹터 0-1   부트로더 (리셋 벡터가 여기)
 *   META    0x08008000  32 KB  섹터 2-3   부트 메타데이터 2 벌 (16 KB x 2, BM-A06)
 *   NVS     0x08010000  64 KB  섹터 4     앱 설정 저장 (예약)
 *   SLOT_A  0x08020000 384 KB  섹터 5-7   실행 슬롯 (앱은 여기에 링크된다)
 *   SLOT_B  0x08080000 384 KB  섹터 8-10  다운로드 슬롯 (새 이미지 수신)
 *   SCRATCH 0x080E0000 128 KB  섹터 11    스왑용 (가장 큰 섹터 이상이어야 함)
 *
 *   SHARED  0x20000000 256 B   부트↔앱 공유 .noinit (부트 사유 전달) — 두 이미지에서 같은 주소
 *   RAM     0x20000100 128 KB - 256 B   각 이미지의 .data/.bss/스택 (동시에 한 이미지만 실행)
 */
MEMORY
{
    BOOT    (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
    META    (r)   : ORIGIN = 0x08008000, LENGTH = 32K
    NVS     (r)   : ORIGIN = 0x08010000, LENGTH = 64K
    SLOT_A  (rx)  : ORIGIN = 0x08020000, LENGTH = 384K
    SLOT_B  (r)   : ORIGIN = 0x08080000, LENGTH = 384K
    SCRATCH (r)   : ORIGIN = 0x080E0000, LENGTH = 128K
    SHARED  (rw)  : ORIGIN = 0x20000000, LENGTH = 256
    RAM     (rwx) : ORIGIN = 0x20000100, LENGTH = 128K - 256
}

/* C 코드가 읽는 레이아웃 심볼 — 주소를 C 에 하드코딩하지 않는다 */
__flash_start   = 0x08000000;
__flash_end     = 0x08100000;
__boot_start    = ORIGIN(BOOT);
__meta_start    = ORIGIN(META);
__slot_a_start  = ORIGIN(SLOT_A);
__slot_b_start  = ORIGIN(SLOT_B);
__slot_size     = LENGTH(SLOT_A);
__shared_start  = ORIGIN(SHARED);
__img_hdr_size  = 0x200;            /* 헤더 예약 512 B → 벡터 테이블 = 슬롯 + 0x200 */
__slot_a_vectors = ORIGIN(SLOT_A) + __img_hdr_size;
_estack         = ORIGIN(RAM) + LENGTH(RAM);

/* 레이아웃 불변식 — 두 이미지는 따로 링크되므로 ld 는 서로의 겹침을 못 본다. 여기서 막는다. */
ASSERT(ORIGIN(BOOT)   + LENGTH(BOOT)   <= ORIGIN(META),    "layout: BOOT overlaps META")
ASSERT(ORIGIN(META)   + LENGTH(META)   <= ORIGIN(NVS),     "layout: META overlaps NVS")
ASSERT(ORIGIN(NVS)    + LENGTH(NVS)    <= ORIGIN(SLOT_A),  "layout: NVS overlaps SLOT_A")
ASSERT(ORIGIN(SLOT_A) + LENGTH(SLOT_A) <= ORIGIN(SLOT_B),  "layout: SLOT_A overlaps SLOT_B")
ASSERT(ORIGIN(SLOT_B) + LENGTH(SLOT_B) <= ORIGIN(SCRATCH), "layout: SLOT_B overlaps SCRATCH")
ASSERT(ORIGIN(SCRATCH) + LENGTH(SCRATCH) <= __flash_end,   "layout: SCRATCH past end of flash")
ASSERT(LENGTH(SLOT_A) == LENGTH(SLOT_B),                   "layout: SLOT_A and SLOT_B differ in size")
ASSERT(ORIGIN(SLOT_A) % 128K == 0 && LENGTH(SLOT_A) % 128K == 0, "layout: SLOT_A not on 128 KB sectors")
ASSERT(ORIGIN(SLOT_B) % 128K == 0 && LENGTH(SLOT_B) % 128K == 0, "layout: SLOT_B not on 128 KB sectors")
ASSERT(ORIGIN(SHARED) + LENGTH(SHARED) <= ORIGIN(RAM),     "layout: SHARED overlaps RAM")
