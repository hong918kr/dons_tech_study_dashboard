/* C-B12 데모: 같은 6개 구조체가 ABI마다 어떻게 배치되는지 _Static_assert 로 확인
 *   LP64  : cc -std=c11 -fsyntax-only abi_layouts.c
 *   ILP32 : clang --target=thumbv7em-none-eabi -ffreestanding -std=c11 -fsyntax-only abi_layouts.c
 *   레이아웃 덤프: 위 명령에 -Xclang -fdump-record-layouts 추가
 *   패딩 경고    : cc -std=c11 -fsyntax-only -Wpadded abi_layouts.c
 */
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

struct s1 { uint8_t a; uint32_t b; uint8_t c; };
struct s2 { uint32_t b; uint8_t a; uint8_t c; };
struct s3 { uint8_t a; uint16_t b; uint8_t c; uint64_t d; };
struct s4 { uint8_t a; uint64_t d; uint8_t c; };
struct s5 { uint16_t a; uint8_t b[3]; uint32_t c; uint8_t d; };
struct s6 { uint8_t tag; long n; void *p; uint16_t len; };

#define LAYOUT(T, SZ, AL) \
    _Static_assert(sizeof(struct T) == (SZ) && alignof(struct T) == (AL), #T)

/* s1~s5 는 고정 폭 정수만 쓰므로 LP64 와 AAPCS(ILP32) 에서 같다 */
LAYOUT(s1, 12, 4);
LAYOUT(s2, 8, 4);
LAYOUT(s3, 16, 8);   /* AAPCS 도 uint64_t 정렬 = 8 */
LAYOUT(s4, 24, 8);
LAYOUT(s5, 16, 4);

#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu        /* LP64: long = pointer = 8 */
LAYOUT(s6, 32, 8);
_Static_assert(offsetof(struct s6, n) == 8 && offsetof(struct s6, p) == 16
               && offsetof(struct s6, len) == 24, "s6 LP64");
#else                                          /* ILP32: long = pointer = 4 */
LAYOUT(s6, 16, 4);
_Static_assert(offsetof(struct s6, n) == 4 && offsetof(struct s6, p) == 8
               && offsetof(struct s6, len) == 12, "s6 ILP32");
#endif

int abi_layouts_ok;   /* 빈 번역 단위 경고 방지 */
