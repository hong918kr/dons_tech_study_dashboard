/* demo_dup.c — 목록에 실수를 넣으면 X-macro 가 만든 코드가 "컴파일 에러" 로 막는가
 *   cc -std=c11 -Wall -Wextra -c demo_dup.c                 # 깨끗
 *   cc -std=c11 -Wall -Wextra -c -DDUP_VALUE demo_dup.c     # 값 중복  → switch 의 duplicate case
 *   cc -std=c11 -Wall -Wextra -c -DDUP_NAME  demo_dup.c     # 이름 중복 → enum 상수 재정의
 *   cc -std=c11 -Wall -Wextra -c -DBAD_RANGE demo_dup.c     # int8 범위 밖 → 항목별 _Static_assert
 *   cc -std=c11 -Wall -Wextra -c -DBAD_ALIGN demo_dup.c     # 오프셋 정렬 깨짐 → _Static_assert
 *   cc -std=c11 -Wall -Wextra -c -DDUP_OFS   demo_dup.c     # 오프셋 중복 → reg_name_at 의 duplicate case
 */
#include <stdint.h>
#include <stddef.h>

#if defined(DUP_VALUE)
#define EXTRA_ERR(X) X(STALL, -2)            /* BUSY 와 같은 -2 */
#elif defined(DUP_NAME)
#define EXTRA_ERR(X) X(BUSY, -5)             /* 이름이 겹침 */
#elif defined(BAD_RANGE)
#define EXTRA_ERR(X) X(BIG, -200)            /* int8 와이어 필드에 안 들어감 */
#else
#define EXTRA_ERR(X)
#endif
#if defined(BAD_ALIGN)
#define EXTRA_REG(X) X(FIFO, 0x0Au)
#elif defined(DUP_OFS)
#define EXTRA_REG(X) X(SR2, 0x0Cu)           /* SR 와 같은 오프셋 */
#else
#define EXTRA_REG(X)
#endif

#define ERR_LIST(X) X(OK, 0) X(TIMEOUT, -1) X(BUSY, -2) EXTRA_ERR(X) X(CRC, -10)
#define REG_LIST(X) X(CR1, 0x00u) X(CR2, 0x04u) X(SR, 0x0Cu) EXTRA_REG(X)

#define X_ENUM(n, v) ERR_##n = (v),
enum err { ERR_LIST(X_ENUM) };

#define X_CHECK(n, v) _Static_assert((v) <= 0 && (v) >= -128, "ERR_" #n ": must fit int8");
ERR_LIST(X_CHECK)

const char *err_to_str(int code);
const char *err_to_str(int code) {
#define X_CASE(n, v) case (v): return #n;
    switch (code) { ERR_LIST(X_CASE) default: return "UNKNOWN"; }
#undef X_CASE
}

#define X_RCHECK(n, o) _Static_assert(((o) & 3u) == 0u, "REG " #n ": offset not 32-bit aligned");
REG_LIST(X_RCHECK)

const char *reg_name_at(uint32_t ofs);
const char *reg_name_at(uint32_t ofs) {
#define X_RCASE(n, o) case (o): return #n;
    switch (ofs) { REG_LIST(X_RCASE) default: return NULL; }
#undef X_RCASE
}
