/* ALGO-A11 demo_cm4 — Cortex-M 코드 생성 보기 (실행 파일 아님, -S 로 어셈블리만 본다).
 * freestanding: <stdint.h>/<stdbool.h> 만.
 *   tb_try_exact : 이 레슨의 토큰 버킷 (스케일 level, full_ticks 미리 계산) — 핫 패스에 나눗셈 없음
 *   tb_try_div   : 흔한 형태 "tokens += dt·num/den" (나머지 보관 없이, 매 호출 나눗셈)
 *   swc_try_mul  : 슬라이딩 카운터 비교 prev·(W−el) + cur·W < N·W (창 넘김 쪽엔 el/W 나눗셈 1번)
 * build:
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cm4.c -o -
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_cm4.c -o -
 */
#include <stdbool.h>
#include <stdint.h>

typedef struct { uint32_t level, cap, num, den, burst, full_ticks, last; } tbucket_t;

bool tb_try_exact(tbucket_t *tb, uint32_t now, uint32_t cost) {
    uint32_t dt = now - tb->last;
    if (dt != 0 && dt <= 0x7FFFFFFFu) {
        tb->last = now;
        if (dt >= tb->full_ticks) tb->level = tb->cap;
        else { uint32_t lv = tb->level + dt * tb->num; tb->level = lv < tb->cap ? lv : tb->cap; }
    }
    if (cost > tb->burst) return false;
    uint32_t need = cost * tb->den;
    if (tb->level < need) return false;
    tb->level -= need;
    return true;
}

typedef struct { uint32_t tokens, burst, num, den, last; } tbucket_div_t;

bool tb_try_div(tbucket_div_t *tb, uint32_t now, uint32_t cost) {
    uint32_t add = (now - tb->last) * tb->num / tb->den;   /* 넘침 + 나머지 손실 + 나눗셈 */
    tb->last = now;
    tb->tokens = tb->tokens + add > tb->burst ? tb->burst : tb->tokens + add;
    if (tb->tokens < cost) return false;
    tb->tokens -= cost;
    return true;
}

typedef struct { uint32_t start, prev, cur, N, W; } swc_t;

bool swc_try_mul(swc_t *c, uint32_t now) {
    uint32_t el = now - c->start;
    if (el >= c->W) {
        uint32_t k = el / c->W;
        c->prev = (k == 1) ? c->cur : 0;
        c->cur = 0;
        c->start += k * c->W;
        el -= k * c->W;
    }
    if (c->prev * (c->W - el) + c->cur * c->W >= c->N * c->W) return false;
    c->cur++;
    return true;
}
