/* C-A08 데모: "이 줄은 이 메시지로 빌드가 실패해야 한다" 를 clang 의 -verify 모드로 테스트한다.
 * (clang 과 libc++ 의 negative compile test 가 쓰는 방식. 기대한 에러가 정확히 나오면 exit 0.)
 *
 *   clang -std=c11 -fsyntax-only -Xclang -verify -Xclang -verify-ignore-unexpected=note demo_verify.c; echo "exit=$?"
 *   clang -std=c11 -fsyntax-only -DBREAK_CONTRACT -Xclang -verify -Xclang -verify-ignore-unexpected=note demo_verify.c; echo "exit=$?"
 */
#include <stddef.h>
#include <stdint.h>

#define BUILD_BUG_ON_ZERO(cond) \
    (0 * sizeof(struct { _Static_assert(!(cond), "BUILD_BUG_ON_ZERO(" #cond ")"); char c_; }))
#define LOWBIT(m)    ((uint32_t)((uint32_t)(m) & (0u - (uint32_t)(m))))
#define FIELD_MAX(m) ((uint32_t)(m) / (LOWBIT(m) | ((uint32_t)(m) == 0u)))
#define MASK_IS_CONTIGUOUS(m) ((uint32_t)(m) != 0u && (FIELD_MAX(m) & (FIELD_MAX(m) + 1u)) == 0u)
#define FIELD_FITS(m, v) ((uintmax_t)(v) <= FIELD_MAX(m))
#if defined(BREAK_CONTRACT)          /* 누가 범위 검사를 "최적화" 로 지워 버렸다고 하자 */
#define FIELD_PREP_CONST(m, v) ((uint32_t)(BUILD_BUG_ON_ZERO(!MASK_IS_CONTIGUOUS(m)) + (uint32_t)(v) * LOWBIT(m)))
#else
#define FIELD_PREP_CONST(m, v) ((uint32_t)(BUILD_BUG_ON_ZERO(!MASK_IS_CONTIGUOUS(m)) \
                              + BUILD_BUG_ON_ZERO(!FIELD_FITS(m, v)) + (uint32_t)(v) * LOWBIT(m)))
#endif

uint32_t ok  = FIELD_PREP_CONST(0x38u, 7u);
uint32_t big = FIELD_PREP_CONST(0x38u, 8u);   // expected-error {{BUILD_BUG_ON_ZERO(!FIELD_FITS(0x38u, 8u))}}
uint32_t gap = FIELD_PREP_CONST(0x28u, 1u);   // expected-error {{BUILD_BUG_ON_ZERO(!MASK_IS_CONTIGUOUS(0x28u))}}
uint32_t rt(uint32_t v) { return FIELD_PREP_CONST(0x38u, v); }  // expected-error {{not an integral constant expression}}
