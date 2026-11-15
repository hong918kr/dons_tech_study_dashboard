/* ENV: qemu — BM-B06 부트로더와 앱이 같이 include 하는 "계약" 헤더.
 * 두 이미지는 따로 컴파일·링크되므로, 이 파일과 layout.ld 가 둘 사이의 유일한 약속이다.
 */
#ifndef BM_B06_SHARED_H
#define BM_B06_SHARED_H
#include <stdint.h>

/* ---- 이미지 헤더: 슬롯 시작 주소(offset 0)에 고정. 헤더 예약 0x200 뒤에 벡터 테이블 ---- */
#define IMG_MAGIC        0x31474D49u        /* "IMG1" little-endian */
#define IMG_HDR_VERSION  1u
typedef struct {
    uint32_t magic;        /* IMG_MAGIC                                   */
    uint16_t hdr_size;     /* 0x200 = 헤더 예약 크기 (벡터 테이블 오프셋) */
    uint16_t hdr_version;  /* 헤더 형식 버전                              */
    uint32_t load_addr;    /* 이 이미지가 링크된 슬롯 시작 주소           */
    uint32_t vector_addr;  /* 벡터 테이블 주소 = load_addr + hdr_size     */
    uint32_t img_size;     /* 슬롯 시작 ~ 마지막 LOAD 바이트 (.data LMA 포함) */
    uint32_t fw_version;   /* major<<24 | minor<<16 | patch               */
    uint32_t crc32;        /* 0 — 빌드 후 패키징 도구가 채운다 (BM-A04)   */
    uint32_t reserved;
} img_hdr_t;
_Static_assert(sizeof(img_hdr_t) == 32, "img_hdr_t layout is part of the boot<->app contract");

/* ---- 부트 사유 공유 영역: .noinit (리셋해도 startup 이 지우지 않음), 두 이미지에서 같은 주소 ---- */
#define SHARE_MAGIC      0xB007CAFEu
enum { REASON_COLD = 0, REASON_WDT = 1, REASON_APP_REQ_UPDATE = 2, REASON_BOOT_FAILED_B = 3 };
typedef struct {
    uint32_t magic;        /* SHARE_MAGIC 이면 유효 (콜드 부트 쓰레기 구분)  */
    uint32_t inv_magic;    /* ~SHARE_MAGIC — 쓰레기가 우연히 맞을 확률을 줄임 */
    uint32_t reason;       /* REASON_*                                     */
    uint32_t boot_count;   /* 부트로더가 증가                              */
    uint32_t active_slot;  /* 0 = A, 1 = B                                 */
    uint32_t app_request;  /* 앱 → 부트로더 요청 (업데이트 설치 등)        */
    uint32_t reserved[2];
} boot_share_t;
_Static_assert(sizeof(boot_share_t) == 32, "boot_share_t layout is part of the boot<->app contract");

/* 섹션 이름은 두 링커 스크립트의 .noinit 규칙과 짝 */
#define SHARED_NOINIT __attribute__((section(".noinit.shared")))
extern volatile boot_share_t g_boot_share;

/* layout.ld 가 정의하는 심볼 — 값이 곧 주소 (배열로 선언해 &를 안 붙여도 되게) */
extern const uint8_t __slot_a_start[], __slot_b_start[], __slot_size[], __img_hdr_size[];
extern const uint8_t __shared_start[];
#endif
