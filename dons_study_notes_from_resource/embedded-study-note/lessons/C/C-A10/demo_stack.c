/* demo_stack.c — 재귀 TLV 파서 vs 반복 + 깊이 제한 버전의 스택 사용량 (C-A10, 프리스탠딩)
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -fstack-usage -c demo_stack.c -o /tmp/ds.o
 *   cat /tmp/ds.su           # .su 는 -o 의 이름을 따른다. 함수별 프레임 크기 (static = 고정 크기)
 *
 * 재귀 버전의 최악 스택 = 프레임 × (중첩 깊이 + 1). 깊이는 입력(호스트가 보낸 설정 블롭)이 정한다.
 * 반복 버전은 rem[] 배열 크기가 곧 상한 — 링커 맵/스택 분석 도구가 정적으로 합산할 수 있다.
 */
#include <stddef.h>
#include <stdint.h>

int tlv_sum_rec(const uint8_t *p, int len);
int32_t tlv_sum_iter(const uint8_t *p, size_t len);

int tlv_sum_rec(const uint8_t *p, int len)
{
    int sum = 0;
    while (len > 0) {
        int t, l, i;
        if (len < 2)
            return -1;
        t = p[0];
        l = p[1];
        if (l > len - 2)
            return -1;
        if (t & 0x80) {
            int s = tlv_sum_rec(p + 2, l);
            if (s < 0)
                return s;
            sum += s;
        } else {
            for (i = 0; i < l; i++)
                sum += p[2 + i];
        }
        p += 2 + l;
        len -= 2 + l;
    }
    return sum;
}

#define TLV_MAX_DEPTH 8u
int32_t tlv_sum_iter(const uint8_t *p, size_t len)
{
    size_t rem[TLV_MAX_DEPTH + 1u];
    uint32_t depth = 0u;
    size_t pos = 0u;
    int32_t sum = 0;
    int32_t err = 0;
    rem[0] = len;
    while (err == 0) {
        if (rem[depth] == 0u) {
            if (depth == 0u) {
                break;
            }
            depth--;
        } else if (rem[depth] < 2u) {
            err = -1;
        } else {
            const uint8_t type = p[pos];
            const size_t l = p[pos + 1u];
            if (l > (rem[depth] - 2u)) {
                err = -1;
            } else {
                rem[depth] -= 2u + l;
                pos += 2u;
                if ((type & 0x80u) != 0u) {
                    if (depth == TLV_MAX_DEPTH) {
                        err = -2;
                    } else {
                        depth++;
                        rem[depth] = l;
                    }
                } else {
                    for (size_t i = 0u; i < l; i++) {
                        sum += p[pos + i];
                    }
                    pos += l;
                }
            }
        }
    }
    return (err != 0) ? err : sum;
}
