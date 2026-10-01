# 18. 인터럽트 잠금 RAII 가드

> **주제**: RAII · critical section · 복사·이동 삭제 · `-fno-exceptions` · **난이도**: 기초 · **목표 시간**: 20분
> **해설은 보지 말 것**: 먼저 `starters/18_raii_guard.cpp`를 채워 `make run N=18`으로 통과시킨다.

## 면접관의 문장

"Here's a bug we actually shipped. Somebody added an early `return` in the middle of a critical section and forgot the matching `irq_unlock()`. The device ran for six hours and then stopped responding to the button, because interrupts were off the whole time.

We build this firmware as C++ with `-fno-exceptions -fno-rtti`. Show me the RAII version. A guard object that disables interrupts in its constructor and restores the previous state in its destructor.

I care about three things. One: it has to restore the previous PRIMASK, not blindly enable interrupts — nesting has to work. Two: nobody should be able to copy it, move it, or store it in a container. Three: it must not be bigger or slower than the C version. Then use it in a couple of functions that share state with an ISR."

## 요구사항

1. `IrqLock` 클래스를 만든다. 생성자는 현재 PRIMASK를 저장하고 인터럽트를 끈다. 소멸자는 저장한 값을 그대로 복원한다.
2. 코어 접근은 주어진 `core::get_primask()`, `core::disable_irq()`, `core::set_primask(v)` 세 함수만 쓴다. PRIMASK는 `0` = 인터럽트 허용, `1` = 마스킹이다.
3. 중첩이 동작해야 한다. 이미 잠긴 구간에서 가드를 또 만들면, 안쪽 가드의 `saved()`는 `1`이고, 안쪽 가드가 파괴될 때 PRIMASK는 `1`로 남아야 한다. 가장 바깥 가드가 파괴될 때만 `0`이 된다.
4. 복사 생성, 복사 대입, 이동 생성, 이동 대입을 **모두** `= delete` 한다. 네 가지가 전부 막혔다는 것을 `std::is_copy_constructible` 계열로 확인한다.
5. `sizeof(IrqLock) == sizeof(uint32_t)`여야 한다. 플래그나 포인터를 추가하면 안 된다.
6. `try_consume(n)`: `g_credits`가 `n` 이상이면 `n`만큼 빼고 `g_consumed`에 더한 뒤 `true`. `n == 0`이면 아무것도 하지 않고 `true`. 모자라면 `false`. 세 경로 모두에서 소멸자가 정확히 한 번만 돌아야 한다.
7. `produce(n)`: `g_credits`에 `n`을 더한다.
8. `snapshot(&c, &d)`: `g_credits`와 `g_consumed`를 **같은 잠금 구간 안에서** 함께 읽는다. 두 값의 합이 항상 생산 총량과 같아야 한다.
9. `nested_producer(&inner_saved)`: 가드를 중첩해서 잡고, 안쪽 가드의 `saved()`를 내보낸다. 안쪽 블록과 그 뒤 두 지점에서 각각 `g_credits`를 1씩 올린다.
10. 생성자 호출 횟수(`core::g_disable_calls`)와 소멸자 호출 횟수(`core::g_restore_calls`)는 어떤 실행 경로에서도 정확히 같아야 한다.

## 인터페이스

```cpp
class IrqLock {
public:
    IrqLock() noexcept;
    ~IrqLock();
    /* 복사 4종 삭제 */
    std::uint32_t saved() const noexcept;
private:
    std::uint32_t saved_;
};

static bool try_consume(std::uint32_t n);
static void produce(std::uint32_t n);
static void snapshot(std::uint32_t *credits, std::uint32_t *consumed);
static void nested_producer(std::uint32_t *inner_saved_out);
```

## 제약

- C++17. 힙 금지(`new`, `malloc`, STL 컨테이너 금지). 예외와 RTTI가 꺼진 상태를 가정한다.
- 허용 헤더: `<cstdint> <cstddef> <new> <type_traits> <cassert> <cstdio> <utility>`.
- 소멸자에서 실패를 알릴 방법이 없다. 소멸자는 반드시 성공하는 연산만 해야 한다.
- 가드는 스코프 안에 이름 있는 지역 변수로만 만든다. 임시 객체(`IrqLock();`)로 만들면 그 자리에서 바로 파괴되므로 아무 구간도 보호하지 못한다.
- `c++ -std=c++17 -Wall -Wextra -O2 -g -fno-exceptions -fno-rtti`에서 경고 0개.

## 예시 동작

```
core::g_primask = 0
{
    IrqLock a;            // a.saved() == 0,  PRIMASK -> 1
    {
        IrqLock b;        // b.saved() == 1,  PRIMASK -> 1 (이미 1)
        {
            IrqLock c;    // c.saved() == 1
        }                 // ~c: PRIMASK <- 1   (여전히 잠김)
    }                     // ~b: PRIMASK <- 1   (여전히 잠김)
}                         // ~a: PRIMASK <- 0   (여기서만 열린다)

disable_calls == 3, restore_calls == 3
```

`try_consume`의 세 경로:

```
g_credits = 6, g_consumed = 4
try_consume(0)  -> true    (아무 변화 없음, PRIMASK 복원됨)
try_consume(9)  -> false   (부족, 아무 변화 없음, PRIMASK 복원됨)
try_consume(4)  -> true    (credits 2, consumed 8, PRIMASK 복원됨)
```

## 스스로 점검할 질문

1. 소멸자가 `core::set_primask(0)`을 하드코딩하면 어떤 중첩 시나리오에서 깨지는가? 그 버그는 몇 시간 뒤에 어떤 증상으로 보이는가?
2. 이동 생성자를 허용하려면 무엇이 추가로 필요하고, 가드의 크기와 소멸자는 어떻게 변하는가?
3. 이 가드를 ISR 안에서 만들어도 되는가? ISR 진입 시점의 PRIMASK는 무엇인가?
4. `IrqLock lock;` 대신 `IrqLock();`이라고 쓰면 생성된 기계어가 어떻게 달라지는가?
5. 가드가 잠그는 구간이 200 사이클이라면, 시스템의 최악 인터럽트 지연(latency)에 얼마가 더해지는가?
6. `-O2`에서 이 클래스가 C 버전과 같은 기계어가 된다고 말할 근거는 무엇인가? 어떻게 확인하는가?
7. PRIMASK 대신 BASEPRI를 쓰는 가드는 무엇이 달라지고, 왜 그 편이 나은 경우가 있는가?
8. 예외가 켜져 있다면 이 가드의 의미가 어떻게 달라지는가? 소멸자에 `noexcept`가 붙는 이유는?

## follow-up (면접관이 이어서 물을 것)

1. "Now I want the same guard, but for a mutex in an RTOS. What changes?"
2. "How do you stop somebody from holding this lock for 10 milliseconds?"
3. "Can you write a guard that takes the lock only if it's free, and tells the caller whether it got it?"
4. "Our coding standard forbids C++ in ISRs. Does that rule make sense to you?"
5. "Show me the same critical section without disabling interrupts at all."

---

**해설**: [notes/L3_embedded_cpp.md](../drills/L3_embedded_cpp.html) §2, §3 · **답안**: `solutions/18_raii_guard.cpp`
