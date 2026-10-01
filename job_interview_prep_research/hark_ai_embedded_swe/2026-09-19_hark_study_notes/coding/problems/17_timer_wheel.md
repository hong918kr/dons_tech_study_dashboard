# 17. 소프트웨어 타이머와 wrap-safe 시간 비교

> **주제**: 소프트웨어 타이머 · 틱 wrap · 콜백 · 주기/원샷 · **난이도**: 중급 · **목표 시간**: 30분
> **해설은 보지 말 것**: 먼저 `starters/17_timer_wheel.c`를 채워 `make run N=17`로 통과시킨다.

## 면접관의 문장

"This part has three hardware timers and my firmware wants twelve: LED blink, sensor poll, link timeout, watchdog kick, debounce, and so on. So we run one millisecond tick and put software timers on top. Write me that. One-shot and periodic, a callback with a context pointer, cancel, and a query for time remaining. No allocation, fixed number of slots. Now the part I actually care about: the tick is a free-running 32-bit counter, so it rolls over every 49.7 days. Our last product had a bug where every timeout fired instantly a month and a half after boot. Write the comparison so that cannot happen, and write me a test that runs across the rollover."

## 요구사항

1. 시간 비교는 반드시 **부호 있는 차이**로 한다. `tw_reached(now, deadline)`은 `(int32_t)(now - deadline) >= 0`이다. `now >= deadline` 같은 절대 비교는 어디에도 없어야 한다.
2. `tw_reached(x, x)`는 `true`다(도달 포함).
3. `tw_reached`는 wrap을 건너도 맞아야 한다. `tw_reached(0, 0xFFFFFFFF)`는 `true`, `tw_reached(0xFFFFFFFF, 0)`는 `false`다.
4. 이 기법의 한계를 코드와 테스트에 명시한다. 차이가 `2^31` 이상이면 부호가 뒤집힌다. `tw_reached(0x7FFFFFFF, 0)`는 `true`이고 `tw_reached(0x80000000, 0)`는 `false`다.
5. 그 한계 때문에 `delay_ms`와 `period_ms`는 `TW_MAX_DELAY`(`0x7FFFFFFF`) 이하여야 한다. 초과하면 `tw_start()`가 `TW_INVALID_ID`를 돌려준다. 경계값 `TW_MAX_DELAY`는 허용한다.
6. 만료 시각은 **절대 tick**으로 저장한다(`expiry = now + delay`). 매 틱마다 남은 시간을 깎는 방식이 아니다.
7. `tw_start()`는 빈 슬롯의 id(0 이상)를 돌려준다. 슬롯이 없으면 `TW_INVALID_ID`. 슬롯은 `TW_MAX_TIMERS`(8)개다.
8. 인자 검증을 슬롯을 건드리기 **전에** 다 끝낸다. 실패한 `tw_start()`가 상태를 반쯤 바꿔 놓아서는 안 된다. `cb == NULL`은 거부한다.
9. `period_ms == 0`이면 one-shot이다. one-shot은 실행 후 스스로 비활성화된다.
10. `delay_ms == 0`은 "다음 `tw_tick()`에서 즉시"를 뜻한다. `tw_tick()`에 같은 tick 값을 넘겨도 실행된다.
11. 주기 타이머는 `tw_tick()` 한 번당 **최대 한 번**만 실행된다.
12. 재장전은 `expiry += period`다. `now + period`가 아니다. `tw_tick()`이 37 ms 늦게 불려도 다음 만료는 원래 격자(100, 200, 300...)에 붙어 있어야 한다. **drift-free**다.
13. 너무 늦어 다음 만료도 이미 지났으면 그 주기는 버리고 `n_missed`를 올린다. 콜백을 몰아서 여러 번 부르지 않는다. 10 ms 주기 타이머를 1000 ms 만에 처음 보면 실행은 1회, `n_missed`는 99다.
14. 만료 목록은 콜백 실행 **전에** 한 번 확정한다. 콜백 안에서 새로 만든 타이머는 같은 `tw_tick()`에서 실행되지 않는다.
15. 콜백이 아직 실행되지 않은 다른 타이머를 취소하면 그 콜백은 실행되지 않는다. 미래로 재시작했으면 새 만료 시각을 존중한다.
16. 재장전·비활성화를 콜백 호출 **전에** 끝낸다. 콜백이 자기 자신을 취소하거나 재시작할 수 있어야 한다.
17. `tw_cancel()`은 범위 밖 id와 이미 비활성인 타이머에 대해 `false`다. 취소한 슬롯은 재사용되어야 한다.
18. `tw_remaining()`은 남은 ms를 준다. 활성이지만 이미 만료 시각을 지났으면 `0`, 비활성이거나 범위 밖이면 `false`다.
19. 같은 tick에 만료된 타이머 여러 개는 한 번의 `tw_tick()`에서 전부 실행된다. 반환값이 실행한 개수다.
20. 테스트는 `2^32` wrap을 **반드시 관통**해야 한다. 촘촘한 tick(1 ms)과 성긴 tick(7 ms) 두 경우 모두에서 정확한 실행 횟수를 확인한다.

## 인터페이스

```c
#include <stdbool.h>
#include <stdint.h>

#define TW_MAX_TIMERS  8u
#define TW_MAX_DELAY   0x7FFFFFFFu
#define TW_INVALID_ID  (-1)

typedef void (*tw_cb_t)(void *ctx);

typedef struct {
    bool     active;
    uint32_t expiry;
    uint32_t period;   /* 0 = one-shot */
    tw_cb_t  cb;
    void    *ctx;
} tw_timer_t;

typedef struct {
    tw_timer_t t[TW_MAX_TIMERS];
    uint32_t   now;
    uint32_t   n_fired;
    uint32_t   n_missed;
} tw_t;

bool     tw_reached(uint32_t now, uint32_t deadline);
void     tw_init(tw_t *w, uint32_t now);
int      tw_start(tw_t *w, uint32_t delay_ms, uint32_t period_ms, tw_cb_t cb, void *ctx);
bool     tw_cancel(tw_t *w, int id);
bool     tw_remaining(const tw_t *w, int id, uint32_t *out_ms);
unsigned tw_tick(tw_t *w, uint32_t now);
```

## 제약

- 동적 할당 금지. 슬롯은 고정 배열이다.
- 하드웨어·RTOS 헤더 금지. tick 값은 `tw_tick()`의 인자로 들어온다.
- 콜백은 메인 루프 문맥에서 실행된다고 가정한다. ISR 문맥이 아니다.
- 정렬된 리스트나 힙을 만들지 않는다. 슬롯 8개를 매번 훑는다(O(n), n=8). 자료구조 최적화는 이 문제의 범위가 아니다.
- C11 표준 라이브러리만. `cc -std=c11 -Wall -Wextra -O2`에서 경고 0개.

## 예시 동작

wrap-safe 비교를 수직선으로.

```
   deadline                                              wrap
      |                                                   |
 ...--+-----------------------------------------------+----+----+--...
   0xFFFFFFFB                                   0xFFFFFFFF  0x00000005
      ^                                                        ^
   deadline = 0xFFFFFFFB                              now = 0x00000005

   now - deadline = 0x00000005 - 0xFFFFFFFB = 0x0000000A  (= +10)
   (int32_t)0x0000000A = +10 >= 0  ->  도달.  맞다.
   now >= deadline    ->  5 >= 4294967291  ->  false.  틀리다.
```

one-shot.

```
tw_init(&w, 1000);  tw_start(&w, 50, 0, cb, ctx);   -> expiry = 1050
tw_tick(&w, 1049) -> 0개 실행
tw_tick(&w, 1050) -> 1개 실행, 그 뒤 타이머는 비활성
tw_tick(&w, 5000) -> 0개
```

drift-free 재장전. 100 ms 주기를 37 ms 늦게 봤다.

```
expiry 격자:   100    200    300
tw_tick(137)  -> 실행. expiry = 100 + 100 = 200   (137 + 100 = 237 이 아니다)
tw_remaining  -> 63
tw_tick(199)  -> 0개
tw_tick(200)  -> 실행
```

놓친 주기 버리기. 10 ms 주기를 1000 ms 만에 처음 봤다.

```
tw_tick(&w, 1000) -> 1개 실행
  expiry: 10 -> 20 -> ... -> 990 -> 1010
                             ^ 마지막으로 지난 만료에서 한 번만 실행
  n_missed = 99
tw_remaining -> 10   (다음 만료 1010)
```

wrap 관통. `0xFFFFFF00`에서 시작해 1 ms씩 600번.

```
30 ms 주기  -> 정확히 20번 실행
300 ms one-shot -> 1번 실행, 실행 시각은 0x0000002C (wrap 뒤 44 ms)
n_missed = 0
```

같은 상황에서 순진한 `now >= expiry`를 쓰면, 300 ms one-shot은 첫 tick에서 바로 만료로 판정되어 299 ms 빠르게 터지고, 30 ms 주기 타이머는 wrap 지점에서 매 tick 터져 600 ms 동안 23번(정답 20번) 실행된다.

## 스스로 점검할 질문

1. `(int32_t)(now - deadline) >= 0`에서 `>= 0` 대신 `> 0`을 쓰면 어떤 요구사항이 깨지나?
2. 캐스트를 빼고 `(now - deadline) >= 0`으로 쓰면 컴파일러가 무엇을 하나? 왜 항상 true인가?
3. `expiry = now + delay`에서 덧셈이 오버플로하면? 그것이 문제가 되지 않는 이유는 무엇인가?
4. `TW_MAX_DELAY`가 `2^31 - 1`인 이유를 수직선으로 설명할 수 있나? 1 ms tick에서 며칠인가?
5. 재장전을 `now + period`로 하면 100 ms 주기 타이머가 하루 뒤에 몇 ms 밀리나?
6. 놓친 주기를 버리지 않고 몰아서 실행하면 어떤 악순환이 생기나? 버리는 쪽의 위험은 무엇인가?
7. 콜백 호출 전에 재장전을 하지 않으면, 콜백이 자기 자신을 취소했을 때 무슨 일이 생기나? 만료 목록을 스냅샷하지 않으면 어떤 비결정성이 생기나?
8. 이 콜백을 tick ISR에서 직접 부르면 무엇이 위험해지나?

## follow-up (면접관이 이어서 물을 것)

1. "타이머가 200개다. 매 틱 전수 조사 말고 무엇을 쓰나?"
2. "진짜 timer wheel(bucket 배열)은 이것과 어떻게 다른가?"
3. "타이머 콜백에서 다른 타이머를 취소하는 것이 안전한가? 왜?"
4. "저전력 모드에서 CPU가 자는 동안 tick이 멈춘다. 무엇을 바꿔야 하나?"
5. "64비트 tick을 쓰면 이 모든 논의가 사라지나? 왜 안 그러나?"

---

해설: [L2 노트](../drills/L2_embedded_idioms.html) §4
