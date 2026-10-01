# 14. 버튼 디바운스 상태기계

> **주제**: 상태기계 · 틱 기반 시간 · 입력 노이즈 · **난이도**: 기초 · **목표 시간**: 20분
> **해설은 보지 말 것**: 먼저 `starters/14_debounce_fsm.c`를 채워 `make run N=14`로 통과시킨다.

## 면접관의 문장

"Every product I have shipped had a bug report that said 'the button sometimes does two things'. Mechanical contacts bounce for a few milliseconds, so a single press looks like five presses to a GPIO read. Write me the debouncer. I want a state machine that I call from my 5 millisecond timer task with the raw GPIO level and the current tick, and that returns at most one event per call: press, release, or long press. No dynamic allocation, no blocking, no hardware headers — the tick comes in as an argument. Then tell me what happens if my task gets delayed and calls you 60 milliseconds late instead of 5, and what happens when your millisecond counter rolls over."

## 요구사항

1. `button_poll()`은 호출당 이벤트를 **최대 하나** 낸다. 없으면 `BTN_EV_NONE`.
2. raw 값이 `DEB_STABLE_MS`(20 ms) **이상** 같은 값으로 유지되면 그 값을 확정한다. 정확히 20 ms는 확정이다(`>=`).
3. 누름 확정 시 `BTN_EV_PRESS`, 뗌 확정 시 `BTN_EV_RELEASE`를 낸다.
4. 확정 전에 raw가 되돌아가면 그 후보는 폐기하고 `n_glitch`를 1 올린다. 이벤트는 내지 않는다.
5. 누름이 확정된 뒤 계속 눌려 있고 **물리적 누름 시각**(누름 후보가 시작된 tick)부터 `DEB_LONG_MS`(800 ms) 이상 지나면 `BTN_EV_LONG_PRESS`를 낸다. 한 번의 누름에서 **딱 한 번만** 낸다.
6. 누른 상태에서 raw가 잠깐 튀었다가 돌아와도 롱프레스 타이머는 리셋되지 않는다. `n_glitch`만 오른다.
7. 롱프레스가 난 뒤 손을 떼면 `BTN_EV_RELEASE`가 나온다. 롱프레스가 `RELEASE`를 대체하지 않는다.
8. 짧은 클릭(800 ms 미만)에서는 `BTN_EV_LONG_PRESS`가 나오지 않는다. 799 ms는 짧은 클릭이다.
9. 확정된 누름·뗌·롱프레스·글리치를 각각 독립된 카운터(`n_press`, `n_release`, `n_long`, `n_glitch`)로 센다. 한 사건이 두 카운터를 동시에 올리지 않는다.
10. 버튼을 아무도 누르지 않는 동안 1000번을 폴링해도 이벤트와 카운터는 전부 0이어야 한다.
11. 부팅 순간 이미 눌려 있으면, 안정 시간이 지난 뒤 한 번의 `BTN_EV_PRESS`가 나와야 한다.
12. 폴링 주기가 안정 시간보다 길어도(예: 50 ms) 연속 두 샘플로 확정되어야 한다. 이때 짧은 펄스를 놓치는 것은 허용한다.
13. tick은 free-running `uint32_t`다. `2^32`에서 wrap해도 모든 판정이 그대로 맞아야 한다.
14. `button_is_down()`은 디바운스된 논리 상태를 돌려준다. 뗌 후보 상태(확정 전)는 아직 "눌림"으로 본다.
15. `btn_event_name()`은 네 enumerator 전부에 대해 각각 `"NONE"`, `"PRESS"`, `"LONG"`, `"RELEASE"`를 돌려준다.

## 인터페이스

```c
#include <stdbool.h>
#include <stdint.h>

#define DEB_STABLE_MS  20u
#define DEB_LONG_MS   800u

typedef enum {
    BTN_IDLE, BTN_WAIT_PRESS, BTN_DOWN, BTN_WAIT_RELEASE
} btn_state_t;

typedef enum {
    BTN_EV_NONE = 0, BTN_EV_PRESS, BTN_EV_LONG_PRESS, BTN_EV_RELEASE
} btn_event_t;

typedef struct {
    btn_state_t st;
    uint32_t    t_edge;
    uint32_t    t_press;
    bool        long_fired;
    uint32_t    n_press, n_long, n_release, n_glitch;
} button_t;

void        button_init(button_t *b, uint32_t now_ms);
bool        button_is_down(const button_t *b);
const char *btn_event_name(btn_event_t e);
btn_event_t button_poll(button_t *b, bool raw_pressed, uint32_t now_ms);
```

`raw_pressed`는 "지금 눌려 있다"로 이미 해석된 값이다. active-low 핀의 반전은 호출자 책임이다.

## 제약

- 동적 할당 금지. 모든 상태는 `button_t` 안에 있다.
- 블로킹·딜레이 금지. `button_poll()`은 상수 시간이어야 한다.
- 하드웨어·RTOS 헤더 금지. tick과 GPIO 값은 인자로 받는다.
- 경과 시간은 `(uint32_t)(now_ms - 기준시각)` 형태로만 구한다. 절대 시각 비교(`now >= deadline`)는 쓰지 않는다.
- C11 표준 라이브러리만. `cc -std=c11 -Wall -Wextra -O2`에서 경고 0개.
- 필터 계수를 바꿔 넣을 필요는 없다. 상수 두 개로 고정이다.

## 예시 동작

깨끗한 100 ms 누름. 5 ms 폴링.

```
t(ms):   0    5   10   15   20   25  ...  100  105  110  115  120
raw:     1    1    1    1    1    1  ...   0    0    0    0    0
event:   -    -    -    -    P    -  ...   -    -    -    -    R
                             ^ 20 ms 유지 -> PRESS               ^ 20 ms 유지 -> RELEASE
```

누를 때 12 ms 동안 접점이 튀는 경우. 1 ms 폴링.

```
t(ms):   0  1  2  3  4  5  6  7  8  9 10 11 12 13 ... 32
raw:     1  0  1  0  1  0  1  0  1  0  1  0  1  1 ...  1
event:   -  -  -  -  -  -  -  -  -  -  -  -  -  - ...  P
결과:    n_glitch = 6, n_press = 1
                 ^ '0'을 만날 때마다 후보가 취소된다(이벤트 없음)
```

누른 채 1 ms 짜리 튐이 있어도 롱프레스 시각은 밀리지 않는다.

```
t=0    누름 시작
t=20   PRESS
t=400  raw가 1 ms 동안 0 -> n_glitch = 1, 상태는 WAIT_RELEASE
t=401  raw가 1로 복귀 -> DOWN. t_press는 여전히 0
t=800  LONG_PRESS      <- 리셋됐다면 1200까지 안 나온다
t=900  손을 뗌
t=920  RELEASE
```

tick wrap 구간을 관통하는 누름.

```
t = 0xFFFFFF00 에서 누르기 시작 -> 0xFFFFFF14 에서 PRESS
                                -> 0x00000010 부근에서 LONG_PRESS
차이를 unsigned 뺄셈으로 구하므로 wrap이 보이지 않는다.
```

## 스스로 점검할 질문

1. 안정 시간 비교를 `>` 로 쓰면 경계에서 무엇이 달라지나? 어떤 테스트가 깨지나?
2. `t_press`를 "누름 확정 시각"으로 두면 롱프레스가 실제 누름보다 몇 ms 늦게 나오나? 두 정의 중 제품에 맞는 쪽은 어느 쪽인가?
3. 누름 도중 글리치에서 `t_press`를 갱신하면 어떤 사용자 조작이 롱프레스를 영원히 못 내게 만드나?
4. 폴링 주기가 30 ms일 때 20 ms 짜리 진짜 누름이 통째로 사라질 수 있다. 왜인가? 막으려면 무엇을 바꿔야 하나?
5. 이 코드는 시각을 `now - t_edge`로만 비교한다. 만약 `t_edge + DEB_STABLE_MS <= now`로 썼다면 wrap에서 어떤 일이 생기나?
6. `n_glitch`가 공장 라인에서 어떤 불량을 가리키나? 값이 몇이면 정상인가?
7. 이 상태기계를 그대로 GPIO edge 인터럽트 기반으로 옮기면 무엇이 깨지나?
8. 상태를 `bool` 두 개(`stable`, `candidate`)로 표현해도 되는가? enum 네 개가 무엇을 더 주나?

## follow-up (면접관이 이어서 물을 것)

1. "double click도 구분해 달라. 상태를 몇 개 더 만들겠나?"
2. "롱프레스를 누른 채로 200 ms마다 반복 이벤트를 내려면?"
3. "버튼이 16개다. 이 구조체를 16개 두는 것 말고 더 싼 방법이 있나?"
4. "타이머 태스크가 바빠서 폴링이 불규칙해진다. 무엇을 보장해야 하나?"
5. "하드웨어 RC 필터나 슈미트 트리거가 있으면 이 코드를 뺄 수 있나?"

---

해설: [L2 노트](../drills/L2_embedded_idioms.html) §1
