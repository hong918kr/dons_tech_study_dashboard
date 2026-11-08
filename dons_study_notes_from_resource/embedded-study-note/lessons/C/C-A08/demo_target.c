/* C-A08 데모: 계약은 "그 빌드" 만 검사한다 — 같은 파일을 타깃별로 컴파일하면 걸리는 계약이 다르다.
 * freestanding 헤더만 쓴다 (thumbv7em 타깃엔 libc 헤더가 없다).
 *
 *   clang -std=c11 -fsyntax-only demo_target.c                                                  # 호스트 arm64 (LP64)
 *   clang -std=c11 -fsyntax-only --target=thumbv7em-none-eabi -mcpu=cortex-m4 demo_target.c       # Cortex-M4 (clang 기본)
 *   clang -std=c11 -fsyntax-only --target=thumbv7em-none-eabi -mcpu=cortex-m4 -fshort-enums demo_target.c
 */
#include <stddef.h>
#include <stdint.h>

typedef enum { PKT_PING = 1, PKT_READ, PKT_WRITE, PKT_STREAM } pkt_type_t;
typedef struct { uint8_t sync; pkt_type_t type; uint16_t len; } hdr_enum_t;  /* 와이어 구조체에 enum (실수) */
typedef struct { uint32_t ctrl, src, dst; void *next; } desc_ptr_t;            /* 디스크립터에 포인터 (실수) */
typedef struct { uint8_t tag; long value; } rec_long_t;                        /* 공유 레코드에 long (실수) */

_Static_assert(sizeof(pkt_type_t) == 1, "pkt_type_t is 1 byte");
_Static_assert(sizeof(hdr_enum_t) == 4, "hdr_enum_t is 4 bytes");
_Static_assert(sizeof(desc_ptr_t) == 16, "desc_ptr_t is 16 bytes");
_Static_assert(sizeof(rec_long_t) == 8, "rec_long_t is 8 bytes");
