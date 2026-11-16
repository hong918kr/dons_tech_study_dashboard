/* ARM-A07 demo_qc_reorder — 컴파일러는 FPSR.QC 를 "부작용"으로 모른다
 *   cc -std=c11 -O2 demo_qc_reorder.c practice.S -o /tmp/ARM-A07_qc && /tmp/ARM-A07_qc
 *   cc -std=c11 -O0 demo_qc_reorder.c practice.S -o /tmp/ARM-A07_qc0 && /tmp/ARM-A07_qc0
 *   clang -std=c11 -O2 -S -fno-asynchronous-unwind-tables demo_qc_reorder.c -o - | grep -E 'bl\s+_|sqadd|^_'
 * 1 dead   : 결과를 아무도 안 읽는다 → 포화 명령이 통째로 지워진다.
 * 2 const  : 입력이 상수 → 컴파일 시간에 접혀 명령이 없다.
 * 3 late   : 결과를 qc_get() 뒤에서만 쓴다 → 명령이 qc_get() 뒤로 내려갈 수 있다.
 * 4 fenced : 포화 연산을 noinline 함수 안에 두고 결과를 포인터로 내보낸다 → 호출 순서 = 실행 순서.
 */
#include <arm_neon.h>
#include <stdint.h>
#include <stdio.h>

uint32_t qc_get(void);
void     qc_clear(void);

__attribute__((noinline)) static uint32_t sqadd_then_qc(int16_t *y, const int16_t *a, const int16_t *b) {
    vst1q_s16(y, vqaddq_s16(vld1q_s16(a), vld1q_s16(b)));
    return qc_get();                       /* y 는 호출자가 볼 수 있는 메모리 → 저장(=계산)이 이 호출보다 먼저 */
}

int main(void) {
    volatile int16_t va = 32767, vb = 1;                             /* 32767 + 1 → 포화해야 한다 */
    int16_t a[8], b[8], y[8], z[8];
    for (int i = 0; i < 8; i++) { a[i] = va; b[i] = vb; }

    qc_clear();
    int16x8_t dead = vqaddq_s16(vld1q_s16(a), vld1q_s16(b)); (void)dead;
    printf("1 dead  : QC=%u\n", qc_get());

    qc_clear();
    int16x8_t c = vqaddq_s16(vdupq_n_s16(32767), vdupq_n_s16(1));
    uint32_t q2 = qc_get();
    printf("2 const : QC=%u  lane0=%d\n", q2, vgetq_lane_s16(c, 0));

    qc_clear();
    int16x8_t late = vqaddq_s16(vdupq_n_s16(va), vdupq_n_s16(vb));
    uint32_t q3 = qc_get();
    printf("3 late  : QC=%u  lane0=%d\n", q3, vgetq_lane_s16(late, 0));

    qc_clear();
    uint32_t q4 = sqadd_then_qc(z, a, b);
    printf("4 fenced: QC=%u  z[0]=%d\n", q4, z[0]);
    (void)y;
    return 0;
}
