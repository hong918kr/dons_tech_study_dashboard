/* C-M06 demo — vtable(ops 표) 호출 비용: asm 관찰용 (main 없음, 헤더는 <stdint.h> 하나)
 *
 *   clang -std=c11 -O2 -S demo_vtable.c -o -                                            # 호스트 arm64
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_vtable.c -o - # Cortex-M4
 */
#include <stdint.h>

struct ops { uint32_t (*xform)(uint32_t); };

static uint32_t twice(uint32_t x) { return x * 2u; }

static const struct ops k_ops = { .xform = twice };  /* const + 내부 연결: 컴파일러가 내용을 안다 */
struct ops g_ops_rw = { .xform = twice };             /* 외부 연결 + non-const: 누가 바꿀지 모른다 */

/* ① 직접 호출 → 인라인 */
uint32_t call_direct(uint32_t x) { return twice(x) + 1u; }
/* ② const 표를 통한 호출 → 컴파일러가 표 내용을 알아 "탈가상화(devirtualize)" + 인라인 */
uint32_t call_const_table(uint32_t x) { return k_ops.xform(x) + 1u; }
/* ③ 쓰기 가능한 전역 표 → 매번 표에서 포인터를 읽어 간접 호출 */
uint32_t call_rw_table(uint32_t x) { return g_ops_rw.xform(x) + 1u; }
/* ④ 매개변수로 받은 ops → 간접 호출 (드라이버 코어의 일반적인 모양) */
uint32_t call_param(const struct ops *o, uint32_t x) { return o->xform(x) + 1u; }

/* ⑤ 루프 안에서: 직접 호출은 인라인 + 벡터화, 간접 호출은 원소마다 호출 */
uint32_t sum_direct(const uint32_t *a, int n) {
    uint32_t s = 0;
    for (int i = 0; i < n; i++) s += twice(a[i]);
    return s;
}
uint32_t sum_param(const struct ops *o, const uint32_t *a, int n) {
    uint32_t s = 0;
    for (int i = 0; i < n; i++) s += o->xform(a[i]);
    return s;
}
