---
id: CPP-B06
time: 50분
updated: 2026-11-07
---
# CPP-B06 RAII 기초: 인터럽트 비활성화 CriticalSection 가드

## 학습 목표

- **RAII**(Resource Acquisition Is Initialization)로 "들어가기(생성자) ↔ 나오기(소멸자)"를 짝지어, `return`·`break`·`continue`·블록 끝 어디로 나가도 정리가 빠지지 않는 코드를 쓴다.
- `CriticalSection` 이 PRIMASK를 **저장했다가 복원**해야 하는 이유(중첩, 부트 초기, ISR 안 호출)를 설명하고, "무조건 enable" 가드가 어떻게 깨지는지 테스트로 보여 준다.
- C의 `goto cleanup` 패턴을 RAII로 바꾸고, 생성 코드가 **완전히 같다**는 것(비용 0)을 어셈블리로 확인한다.
- `CriticalSection();` (이름 없는 임시 객체) 버그를 알고, 생성자에 `[[nodiscard]]` 를 붙여 컴파일러가 잡게 만든다.
- ISR과 공유하는 상태(이벤트 플래그, 64비트 틱)를 임계 구역(critical section)으로 지키고, 마스크 중에 온 인터럽트는 **pending으로 남았다가 복원 순간 실행**된다는 것을 이해한다.

## 1. 문제

ISR과 메인 루프가 같은 변수를 만지는 순간 "인터럽트 잠깐 끄기"가 필요하고, 면접에서는 그 코드를 **중첩·조기 return에서도 안전하게** 쓸 수 있는지를 본다 — C++ 펌웨어 면접의 RAII 첫 질문이 거의 항상 이것이다.

C에서는 이렇게 쓴다: `pm = __get_PRIMASK(); __disable_irq(); ... if (pm == 0) __enable_irq();`. 문제는 `...` 안에 `return` 이 하나라도 생기면 복원 코드를 건너뛴다는 것. 그래서 C 코드는 출구를 하나로 모으려고 `goto out;` 을 쓴다. C++에서는 **지역 객체의 소멸자가 스코프를 떠나는 모든 길에서 자동으로 호출**되므로, 복원을 소멸자에 넣으면 끝이다. 남은 질문은 하나 — 소멸자가 "켜기(enable)"를 해야 하나, "원래대로(restore)"를 해야 하나?

```svg
<svg viewBox="0 0 720 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">중첩된 임계 구역에서 PRIMASK 변화 (채워진 칸 = IRQ 마스크됨)</text>
  <g stroke="currentColor" stroke-dasharray="4 3" opacity=".6">
    <line x1="140" y1="40" x2="140" y2="185"/>
    <line x1="280" y1="40" x2="280" y2="185"/>
    <line x1="430" y1="40" x2="430" y2="185"/>
    <line x1="610" y1="40" x2="610" y2="185"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="140" y="55">outer 생성</text>
    <text x="280" y="55">inner 생성</text>
    <text x="430" y="55">inner 소멸</text>
    <text x="610" y="55">outer 소멸</text>
  </g>
  <text x="10" y="94" fill="currentColor">저장·복원</text>
  <rect x="140" y="75" width="470" height="30" class="f-acc" stroke="currentColor"/>
  <text x="150" y="95" fill="currentColor">PRIMASK=1 — inner 는 "원래 값 1" 로 복원 → outer 끝까지 보호</text>
  <text x="10" y="154" fill="currentColor">무조건 enable</text>
  <rect x="140" y="135" width="290" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="430" y="135" width="180" height="30" class="f-warn" stroke="currentColor"/>
  <text x="150" y="155" fill="currentColor">PRIMASK=1</text>
  <text x="440" y="155" fill="currentColor">구멍: IRQ 켜짐!</text>
  <text x="10" y="215" fill="currentColor">inner 의 소멸자가 "켜기"를 하면, outer 는 아직 공유 데이터를 만지는 중인데 ISR 이 끼어든다.</text>
  <text x="10" y="240" fill="currentColor">inner 는 자기가 바깥에 중첩됐는지 모른다 → 들어올 때 상태를 저장하고, 나갈 때 그대로 되돌린다.</text>
  <text x="10" y="262" fill="currentColor">(starter 하네스의 NaiveIrqGuard 테스트가 이 구멍을 재현한다)</text>
</svg>
```

호스트(맥)에는 PRIMASK가 없으니 **mock**을 쓴다: 전역 `g_primask` + CMSIS와 같은 이름의 `__disable_irq()` / `__enable_irq()` / `__get_PRIMASK()`. mock은 실제 NVIC처럼 **마스크 중에 들어온 인터럽트를 pending으로 기억했다가 `__enable_irq()` 순간 실행**한다. `preempt_point()` 는 "메인 코드의 이 지점에서 인터럽트가 끼어들 수 있다"를 흉내 내는 훅이다.

### Part A — CriticalSection 가드

`class CriticalSection` 의 생성자와 소멸자를 채운다.

- 생성자: 현재 PRIMASK를 `saved_` 에 저장 → 인터럽트 끄기. (순서가 중요하다.)
- 소멸자: **원래 켜져 있었을 때만** 다시 켠다. 무조건 `__enable_irq()` 하면 안 된다.
- 생성자에는 이미 `[[nodiscard]]` 가 붙어 있고, 복사는 `= delete` (자세한 복사/이동 금지 설계는 CPP-B07 참고).

테스트: 기본 진입/해제, 2단 중첩, **이미 마스크된 상태에서 진입**(부트 코드가 꺼 둔 구간), 그리고 반례 `NaiveIrqGuard` 가 중첩에서 구멍을 내는 것.

### Part B — 출구가 여러 개인 함수

`claim_slot_c()` 는 ISR과 공유하는 DMA 디스크립터 슬롯 풀에서 빈 슬롯을 하나 집는 C 스타일 함수다(`goto out` 으로 출구 하나). 이것을 `CriticalSection` + **조기 return** 으로 다시 쓴 `claim_slot()` 을 작성한다 (`goto` 도, `rc` 변수도 없이).

이미 작성된 시나리오 함수들(여러 분기 `switch`, 루프 몸통 안 가드 + `continue`/`break`, 루프 바깥 가드 + `break`, `return` 식 안에서 상태 읽기, 재귀 중첩)이 **모든 경로에서 PRIMASK가 복원되는지** 하네스가 검사한다 — Part A가 맞으면 통과해야 한다.

### Part C — ISR과 공유하는 드라이버 상태

- `take_events()` — UART ISR이 비트를 세우는 `g_events` 를 "읽고 지운다". 읽기와 지우기 사이에 들어온 이벤트를 **잃으면 안 된다**.
- `ticks_now()` — SysTick ISR이 올리는 64비트 틱을 32비트 워드 두 개(`g_tick_lo`, `g_tick_hi`)로 저장한다. lo와 hi가 서로 다른 시점의 값이면(찢어진 읽기, torn read) 시간이 2^32 틱 앞으로 튄다.
- 둘 다 이미 임계 구역 **안에서** 호출돼도 바깥 보호를 깨면 안 된다 (pending ISR은 바깥 가드가 끝날 때 실행).

## 2. Problem (English)

> **Interrupt-safe critical section guard (RAII), no exceptions, no heap.**
>
> On a Cortex-M MCU, `__get_PRIMASK()` returns the current interrupt mask (bit 0 = 1 means IRQs are masked), `__disable_irq()` sets it and `__enable_irq()` clears it. Implement:
>
> 1. `class CriticalSection` whose constructor masks interrupts and whose destructor restores the mask **to the state it had on entry**. It must nest correctly, must not enable interrupts if they were already masked on entry, and must be non-copyable. Discarding an unnamed temporary (`CriticalSection();`) should produce a compiler diagnostic.
> 2. `int claim_slot(uint8_t owner)` — find the first free (zero) entry in a 4-entry slot table shared with an ISR, write `owner` into it and return its index; return `-1` if full, `-2` if `owner == 0`. Use early returns, no `goto`.
> 3. `uint32_t take_events()` (atomic fetch-and-clear of an ISR-written bitmask) and `uint64_t ticks_now()` (consistent read of a 64-bit tick counter stored as two 32-bit words, incremented by an ISR).
>
> Constraints: `-std=c++20 -fno-exceptions -fno-rtti`. Explain why "restore the saved state" is correct and "always enable" is not, and what happens to an interrupt that fires while masked.
>
> Example: with `lo = 0xFFFFFFFF, hi = 0` and a SysTick firing between the two reads, `ticks_now()` must return `0x00000000FFFFFFFF` (not `0x00000001FFFFFFFF`), and afterwards the counter reads `0x0000000100000000`.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# FAIL 줄이 쭉 나오고 "13/36 passed", exit 1 — TODO 를 채워 36/36 을 만든다
```

통과하는 13개는 가드와 무관한 것들(반례 `NaiveIrqGuard`, C goto 버전, 가드가 없어도 우연히 맞는 값)이다.

## 4. 힌트

<details>
<summary>힌트 1 — 생성자에서 "읽기"와 "끄기" 순서</summary>

끄고 나서 읽으면 항상 1이 읽힌다 → 소멸자는 영원히 다시 켜지 않는다. **먼저 읽고, 그다음 끈다.** 읽기와 끄기 사이에 인터럽트가 와도 괜찮다 — ISR은 PRIMASK를 들어올 때 상태 그대로 두고 나가기 때문에, 저장한 값은 여전히 맞다.

</details>

<details>
<summary>힌트 2 — 소멸자는 "켜기"가 아니라 "되돌리기"</summary>

`saved_` 의 bit0이 0이었으면(=원래 켜져 있었으면) `__enable_irq()`, 1이었으면 아무것도 안 한다. 실제 CMSIS라면 `__set_PRIMASK(saved_)` 한 줄로도 같다.

</details>

<details>
<summary>힌트 3 — claim_slot 의 인자 검사 위치</summary>

`owner == 0` 검사는 공유 데이터를 안 건드리므로 **가드를 만들기 전에** 한다 → 임계 구역이 짧아진다. 하네스도 이 경우 `__disable_irq()` 호출 수가 늘지 않는 것을 기대한다. 그다음 `CriticalSection cs;` 한 줄, 루프 안에서 찾으면 바로 `return i;`.

</details>

<details>
<summary>힌트 4 — Part C 는 각 함수 첫 줄 하나</summary>

`CriticalSection cs;` 를 맨 위에 두면 `return` 식(`ev`, `(hi<<32)|lo`)은 **소멸자보다 먼저** 평가된다. 그래서 반환값을 가드 안에서 읽은 값으로 안전하게 들고 나갈 수 있다.

</details>

## 5. 풀이

### Part A — 저장 → 끄기 → (작업) → 되돌리기

```cpp
[[nodiscard]] CriticalSection() noexcept {
    saved_ = __get_PRIMASK();      // ① 진입 전 상태 저장
    __disable_irq();               // ② 끈다
}
~CriticalSection() {
    if ((saved_ & 1u) == 0) __enable_irq();   // 원래 켜져 있던 경우에만
}
```

**왜 "켜기"가 아니라 "되돌리기"인가** — 가드를 쓰는 코드는 자기가 어떤 상황에서 불리는지 모른다:

| 호출 상황 | 진입 시 PRIMASK | 무조건 enable 가드 | 저장·복원 가드 |
|---|---|---|---|
| 메인 루프에서 평범하게 | 0 | 0으로 → 맞음 | 0으로 → 맞음 |
| 다른 임계 구역 안 (함수 A가 B를 호출, 둘 다 가드) | 1 | **0으로 → 바깥 보호에 구멍** | 1 유지 → 맞음 |
| 부트 구간 (스타트업·부트로더가 일부러 `cpsid i` 해 둔 동안: 벡터 테이블 이동, 클럭 설정, 앱 점프 직전, RTOS 스케줄러 시작 전) | 1 | **인터럽트가 너무 일찍 켜짐** → 준비 안 된 핸들러 진입 | 1 유지 |
| 의도적으로 IRQ를 꺼둔 구간 (플래시 erase, 저전력 진입 시퀀스, fault 핸들러) | 1 | **구간이 깨짐** | 1 유지 |

(Cortex-M은 리셋 직후 PRIMASK=0 이다. 위의 "꺼진 부트 구간"은 소프트웨어가 일부러 만든 것이다.)

즉 "무조건 enable"은 **이 함수가 항상 가장 바깥, 인터럽트가 켜진 상태에서만 불린다**는 숨은 가정이다. 라이브러리·드라이버 코드에서 그 가정은 거의 항상 언젠가 깨진다. Linux 커널이 `local_irq_disable()/enable()` 과 별도로 `local_irq_save(flags)/local_irq_restore(flags)` 를 두고, Zephyr의 `irq_lock()` 이 "이전 상태 키"를 돌려주고 `irq_unlock(key)` 가 그 키로 되돌리는 것도 같은 이유다.

비용: 저장한 값 하나(레지스터 1개, 넘치면 스택 워드 1개 — `static_assert(sizeof(CriticalSection) == sizeof(uint32_t))`) + `mrs` 1개 + 조건 분기 1개.

**실제 Cortex-M에서의 의미** (ARMv7-M ARM B1.4.3 기준):

- PRIMASK는 1비트 레지스터. 1이면 실행 우선순위를 0으로 올려 **설정 가능한 우선순위의 모든 예외(IRQ, SysTick, PendSV, SVC…)를 막는다**. NMI와 HardFault는 막지 못한다.
- `__disable_irq()` = `cpsid i`, `__enable_irq()` = `cpsie i`, `__get_PRIMASK()` = `mrs rX, primask`. CMSIS(`cmsis_gcc.h`)는 이를 `asm volatile("cpsid i" ::: "memory")` 처럼 **"memory" clobber**와 함께 정의한다 — 이것이 컴파일러가 공유 변수 접근을 임계 구역 밖으로 옮기지 못하게 하는 **컴파일러 배리어** 역할을 한다. mock에는 그런 효과가 없다(호스트 단일 스레드라 필요도 없다).
- PRIMASK는 예외 진입/복귀 때 **자동 저장·복원되지 않는다** (FAULTMASK와 다름). 그래서 "ISR은 PRIMASK를 들어온 그대로 두고 나간다"는 규칙이 있고, 이 덕분에 생성자의 `mrs` 와 `cpsid` 사이에 인터럽트가 끼어도 저장한 값이 유효하다.
- Cortex-M0/M0+(ARMv6-M)에는 BASEPRI가 없어 PRIMASK가 유일한 수단이다. M3 이상은 BASEPRI로 "낮은 우선순위만" 막을 수 있다 (추가 연습 3, 실제 칩 실습은 ARM-M10 참고).

### Part B — 모든 출구가 소멸자를 지나간다

```svg
<svg viewBox="0 0 720 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">cs 가 선언된 스코프를 떠나는 모든 길 → ~CriticalSection()</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="40" width="250" height="30" rx="6"/>
    <rect x="10" y="80" width="250" height="30" rx="6"/>
    <rect x="10" y="120" width="250" height="30" rx="6"/>
    <rect x="10" y="160" width="250" height="30" rx="6"/>
    <rect x="10" y="200" width="250" height="30" rx="6"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="60">return i;   (찾음)</text>
    <text x="20" y="100">return -1;  (꽉 참)</text>
    <text x="20" y="140">continue; / break; (가드가 루프 안)</text>
    <text x="20" y="180">goto 스코프 밖 라벨;</text>
    <text x="20" y="220">블록 끝 }</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M260 55 L430 120"/><path d="M260 95 L430 125"/><path d="M260 135 L430 130"/>
    <path d="M260 175 L430 135"/><path d="M260 215 L430 140"/>
  </g>
  <rect x="430" y="100" width="280" height="55" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="442" y="123" fill="currentColor" font-family="monospace">~CriticalSection()</text>
  <text x="442" y="144" fill="currentColor">saved_ 로 PRIMASK 복원</text>
  <rect x="430" y="185" width="280" height="45" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="442" y="205" fill="currentColor">안 지나가는 길: longjmp, exit(),</text>
  <text x="442" y="223" fill="currentColor">abort(), 무한 루프, 리셋</text>
  <text x="10" y="258" fill="currentColor">C 의 goto out 은 이 화살표들을 손으로 한 점에 모으는 것.</text>
  <text x="10" y="280" fill="currentColor">C++ 는 컴파일러가 출구마다 소멸자 호출을 넣어 준다.</text>
</svg>
```

표준 근거: "스코프를 떠날 때(어떤 방법이든) 그 스코프에서 생성된 자동 저장 기간 객체는 생성의 역순으로 소멸한다" — [stmt.jump]/2. `return`, `break`, `continue`, `goto` 모두 포함된다.

**C의 `goto cleanup` 과 비교**

| | C `goto out` | C++ RAII |
|---|---|---|
| 출구 추가 | 새 `return` 을 쓰면 복원 누락 — 리뷰어가 잡아야 함 | 그냥 `return` — 자동 |
| 반환값 | `rc` 변수에 담아 라벨까지 운반 | `return i;` 바로 |
| 짝 맞추기 | `pm` 저장과 복원이 함수 양 끝에 흩어짐 | 한 클래스 안에 붙어 있음 |
| C++로 옮기면 | `goto` 가 초기화된 지역 변수 선언을 건너뛰면 **컴파일 에러** ([stmt.dcl]/3) | 해당 없음 |
| 생성 코드 | — | **같다** (아래 어셈블리) |

마지막 줄은 `demo_pitfalls.cpp` 에서 직접 확인했다 (`-DSHOW_ERROR`):

```text
demo_pitfalls.cpp:43:16: error: cannot jump from this goto statement to its label
   43 |     if (x < 0) goto out;
      |                ^
demo_pitfalls.cpp:44:21: note: jump bypasses variable initialization
   44 |     CriticalSection cs;                   // goto 가 이 초기화를 건너뛴다 → ill-formed
```

**시나리오별로 하네스가 확인하는 것**

| 함수 | 경로 | 기대 |
|---|---|---|
| `claim_slot` | 인자 오류(가드 전) / 찾음(루프 안 return) / 꽉 참(루프 뒤 return) | `__disable_irq` 호출 5회 = `__enable_irq` 5회, 끝나면 마스크 해제 |
| `classify` | `switch` 의 case 0, case 1(안쪽 if return), default→아래 return 2가지 | 4번 호출 → 4번 진입·4번 복원, case 1 은 안에서 마스크 상태 확인 |
| `loop_guard_inside(10)` | 회차마다 진입, 홀수 회차 `continue`, i=6 에서 `break` | i=0..6 → 7번 진입·7번 해제, 처리 3개 |
| `sum_until_zero` | 가드는 루프 밖 1개, `break` 는 루프만 빠져나감 | 진입 1번 |
| `masked_during_return` | `return irq_masked();` | **true** — return 식이 소멸자보다 먼저 |
| `nested_depth(5)` | 재귀 6단 중첩 | 진입 6번, enable 은 가장 바깥 1번 |

`masked_during_return` 이 true인 이유: [stmt.return] — "호출 결과의 복사 초기화는 return 피연산자 full-expression 끝의 임시 객체 소멸보다 먼저, 그것은 다시 그 블록의 지역 변수 소멸보다 먼저 일어난다." 그래서 `return g_shared;` 는 **가드 안에서 읽은 값**을 돌려준다. C에서 `rc = g_shared; goto out;` 으로 하던 일이다.

**경쟁 시나리오** — `arm_preempt(isr_claim_any)` 로 검사(`g_slots[0] == 0`)와 쓰기 사이에 ISR을 끼워 넣는다:

| | 가드 없음 | 가드 있음 |
|---|---|---|
| main | slot0 비었네 → (ISR) → slot0 = 0x33 | slot0 비었네 → (ISR은 pending) → slot0 = 0x33 |
| ISR | slot0 = 0xEE (같은 슬롯!) | 복원 순간 실행 → slot1 = 0xEE |
| 결과 | **둘 다 slot0 을 가졌다고 믿음** → DMA 디스크립터 이중 사용 | 0x33 / 0xEE 정상 분리 |

### Part C — 마스크 중의 인터럽트는 사라지지 않고 "미뤄진다"

```svg
<svg viewBox="0 0 720 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">ticks_now(): lo=0xFFFFFFFF, hi=0 에서 두 읽기 사이에 SysTick</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">가드 없음</text>
  <text x="10" y="80" fill="currentColor">main</text><text x="10" y="120" fill="currentColor">ISR</text>
  <line x1="60" y1="76" x2="710" y2="76" stroke="currentColor" opacity=".4"/>
  <line x1="60" y1="116" x2="710" y2="116" stroke="currentColor" opacity=".4"/>
  <rect x="70" y="62" width="170" height="26" class="f-acc" stroke="currentColor"/><text x="78" y="80" fill="currentColor">lo 읽기 = FFFFFFFF</text>
  <rect x="260" y="102" width="160" height="26" class="f-warn" stroke="currentColor"/><text x="268" y="120" fill="currentColor">lo=0, hi=1 (캐리)</text>
  <rect x="440" y="62" width="120" height="26" class="f-acc" stroke="currentColor"/><text x="448" y="80" fill="currentColor">hi 읽기 = 1</text>
  <text x="575" y="80" fill="currentColor">→ 1_FFFFFFFF ✗</text>
  <text x="10" y="165" fill="currentColor" font-weight="bold">가드 있음</text>
  <text x="10" y="195" fill="currentColor">main</text><text x="10" y="235" fill="currentColor">ISR</text>
  <line x1="60" y1="191" x2="710" y2="191" stroke="currentColor" opacity=".4"/>
  <line x1="60" y1="231" x2="710" y2="231" stroke="currentColor" opacity=".4"/>
  <rect x="70" y="177" width="370" height="26" class="f-mut" stroke="currentColor"/>
  <text x="78" y="195" fill="currentColor">cs 진입 · lo=FFFFFFFF · hi=0 · return 값 계산</text>
  <rect x="440" y="177" width="80" height="26" class="f-acc" stroke="currentColor"/><text x="448" y="195" fill="currentColor">~cs 복원</text>
  <text x="230" y="225" fill="currentColor">요청 → pending (NVIC)</text>
  <rect x="530" y="217" width="160" height="26" class="f-ok" stroke="currentColor"/><text x="538" y="235" fill="currentColor">lo=0, hi=1 (캐리)</text>
  <path d="M520 203 L530 217" stroke="currentColor" fill="none"/>
  <text x="10" y="270" fill="currentColor">반환 0_FFFFFFFF ✓ — 틱은 잃지 않고 복원 직후 반영, 다음 호출은 1_00000000.</text>
</svg>
```

- `take_events()` 도 같은 그림이다. 가드가 없으면 `ev = 0x4` 를 읽은 뒤 ISR이 `0x1` 을 세우고, 메인이 `g_events = 0` 으로 **방금 온 RX 이벤트까지 지운다** (lost update, C-B01의 RMW 경쟁과 같은 구조). 가드가 있으면 RX 이벤트는 복원 순간 들어와 `g_events == 0x1` 로 남는다.
- 실제 하드웨어: PRIMASK=1인 동안 IRQ 요청은 NVIC의 **pending 비트**로 래치되고, `cpsie i` 직후 받아들여진다. 단, pending 비트는 IRQ당 **1개**다 — 마스크된 동안 같은 IRQ가 두 번 오면 한 번으로 합쳐진다. UART RX라면 두 번째 바이트는 FIFO/오버런 플래그에만 남는다. 그래서 **임계 구역은 짧게** (수십 사이클 단위), 그 안에서 바쁜 대기·블로킹 호출 금지.
- 마지막 테스트: 바깥 `CriticalSection` 안에서 `ticks_now()` 를 부르면, 안쪽 가드의 소멸자는 저장값 1을 보고 **아무것도 안 한다** → ISR은 바깥 가드가 끝날 때 실행. 무조건 enable 가드였다면 안쪽 소멸 시점에 ISR이 바깥 구역 한가운데에서 실행된다.

64비트 틱을 가드 없이 읽는 방법(hi 재읽기 루프)과 `volatile` 의 한계는 C-M14 / C-B09 참고. `std::atomic` 으로 가능한 경우는 CPP-A04 참고.

### 비용 — goto 버전과 생성 코드가 같다

`demo_cost.cpp` 에 같은 슬롯 함수를 RAII 버전(`claim_raii`)과 C goto 버전(`claim_goto`)으로 쓰고, 호스트가 arm64라서 PRIMASK 대신 같은 역할의 AArch64 명령을 inline asm으로 넣었다 (`mrs x, daif` ≈ `mrs r0, primask`, `msr daifset, #2` ≈ `cpsid i`, `msr daifclr, #2` ≈ `cpsie i`; EL0에서 실행하면 트랩되므로 **컴파일만**).

```bash
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o demo_cost.s demo_cost.cpp
```

실제 출력 (Apple clang 21, arm64, `claim_raii` 의 앞부분과 복원 부분 발췌, `;` 뒤 한국어 주석은 설명용으로 붙인 것):

```text
_claim_raii:
	cbz	w0, LBB0_6          ; owner==0 → -2 (가드 전)
	mrs	x9, DAIF            ; saved_ = 마스크 읽기   ← 생성자
	msr	DAIFSet, #2         ; IRQ 끄기              ← 생성자
	...                     ; 슬롯 4개 검사 (루프가 완전히 펼쳐짐)
LBB0_9:
	mov	w8, #2
	strb	w0, [x10]
	tbnz	w9, #7, LBB0_12     ; saved_ 의 I 비트가 1이면 건너뜀  ← 소멸자
LBB0_10:
	msr	DAIFClr, #2         ; IRQ 다시 켜기                 ← 소멸자
	mov	x0, x8
	ret
```

두 함수의 출력을 라벨 이름만 정규화해 `diff` 하면 **명령어가 한 줄도 다르지 않다** (차이는 `.loh` 링커 힌트 지시어 번호뿐). 생성자/소멸자는 인라인되어 사라지고, 컴파일러가 출구마다 "복원 분기"를 복제해 넣었을 뿐이다. 객체 `cs` 는 메모리에 존재하지도 않는다 — `saved_` 는 그냥 레지스터 `x9`.

arm-none-eabi-g++ `-O2 -mcpu=cortex-m4` 에서는 (이 맥에 툴체인이 없어 **실행은 못 했고 예상**이다) 같은 모양으로 `mrs r3, PRIMASK` → `cpsid i` → … → bit0 검사(`lsls`/`tst` + 조건 분기) → `cpsie i` 가 나올 것이다. 예외를 끈 빌드(`-fno-exceptions`)라 `.ARM.exidx`/`.ARM.extab` 언와인드 정보도 필요 없다 — RAII 정리는 런타임 언와인딩이 아니라 **컴파일 시점에 출구마다 박아 넣는 코드**이기 때문이다.

### 함정 — `CriticalSection();` 와 `[[nodiscard]]` 생성자

```svg
<svg viewBox="0 0 720 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">객체 수명: 이름 없는 임시 vs 이름 있는 지역 변수</text>
  <g fill="currentColor" font-family="monospace" font-size="13">
    <text x="10" y="60">CriticalSection();  g_x++;  g_y++;  }</text>
    <text x="10" y="135">CriticalSection cs; g_x++;  g_y++;  }</text>
  </g>
  <rect x="10" y="70" width="140" height="14" class="f-warn" stroke="currentColor"/>
  <text x="160" y="82" fill="currentColor">; 에서 이미 소멸 → g_x, g_y 는 보호 없음</text>
  <rect x="10" y="145" width="300" height="14" class="f-ok" stroke="currentColor"/>
  <text x="320" y="157" fill="currentColor">} 까지 살아 있음 → 보호</text>
  <text x="10" y="190" fill="currentColor">임시 객체는 그것을 만든 full-expression 이 끝날 때 소멸한다 ([class.temporary]/4).</text>
</svg>
```

`CriticalSection();` 는 문법적으로 완전히 합법이다 — 임시 객체를 만들고(생성자: 끄기) 그 문장 끝 `;` 에서 바로 소멸(소멸자: 복원)한다. 아래 코드는 **아무 보호도 없이** 실행된다. 클래스에 아무 표시가 없으면 clang은 `-Weverything` 으로도 경고하지 않는다 (직접 확인). `demo_pitfalls.cpp` 실행 결과:

```text
$ c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_pitfalls.cpp -o demo && ./demo
PlainGuard();   -> PRIMASK inside = 0 (0 = NOT protected!)
PlainGuard g;   -> PRIMASK inside = 1
auto cs = CriticalSection(); -> PRIMASK inside = 1
```

**해결: 생성자에 `[[nodiscard]]`.** C++20(P1771R1)부터 [dcl.attr.nodiscard]는 "nodiscard 생성자로 객체를 만드는 명시적 타입 변환(`T()`, `T{}`, `static_cast<T>(...)`)"도 *nodiscard call* 로 정의하고, 그 결과를 버리면 경고를 내도록 **권장**한다 (권장 사항 — 진단이 의무는 아니다). 메시지 인자 `[[nodiscard("...")]]` 도 C++20(P1301R4). `-DSHOW_ERROR` 빌드 결과 (실제 출력):

```text
$ c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR demo_pitfalls.cpp
demo_pitfalls.cpp:37:5: error: ignoring temporary created by a constructor declared with 'nodiscard' attribute: name the guard: CriticalSection cs; [-Werror,-Wunused-value]
   37 |     CriticalSection();                    // 임시 객체 → 이 줄 끝에서 즉시 소멸
demo_pitfalls.cpp:38:5: error: ignoring temporary created by a constructor declared with 'nodiscard' attribute: name the guard: CriticalSection cs; [-Werror,-Wunused-value]
   38 |     CriticalSection{};                    // 중괄호도 같다
demo_pitfalls.cpp:39:23: error: empty parentheses interpreted as a function declaration [-Werror,-Wvexing-parse]
   39 |     CriticalSection cs();                 // most vexing parse: 객체가 아니라 함수 선언
demo_pitfalls.cpp:39:23: note: remove parentheses to declare a variable
```

clang 21에서 관찰한 사실 정리:

| 코드 | `[[nodiscard]]` 없음 | 생성자에 `[[nodiscard]]` |
|---|---|---|
| `CriticalSection();` / `CriticalSection{};` | 경고 0 (`-Weverything` 도) | `-Wunused-value` 경고 (기본 활성, `-Wall` 없이도 나옴) |
| `CriticalSection cs();` | `-Wvexing-parse` 경고 (함수 선언이 됨) | 같음 |
| `(void)CriticalSection();` | 경고 0 | 경고 0 — void 캐스트는 "일부러 버림" 표시 |
| `auto cs = CriticalSection();` | 정상 (C++17 보장된 복사 생략, 복사 금지여도 OK) | 정상 |
| `-std=c++17` 에서 생성자 `[[nodiscard]]` | — | 같은 경고 (clang 확장 동작). 메시지 형식은 `-pedantic` 시 `-Wc++20-attribute-extensions` |

클래스 자체에 `struct [[nodiscard]] CriticalSection` 을 붙여도 `CriticalSection();` 이 잡힌다(관찰: "ignoring temporary of type ... declared with 'nodiscard'"). 다만 그러면 이 타입을 값으로 돌려주는 모든 함수에도 영향이 가므로, 의도가 분명한 **생성자 쪽**이 보통 낫다. GCC도 10부터 P1771R1/P1301R4를 지원한다 (cppreference 지원표 기준, 여기선 실행 못 함). clang-tidy의 `bugprone-unused-raii` 검사도 이 패턴을 잡는다 (이 맥엔 미설치).

**복사/이동은 짧게만**: 가드를 복사할 수 있으면 복사본과 원본이 각각 복원 → 이중 복원. 그래서 복사 생성자·대입을 `= delete`. 복사 생성자를 사용자 선언(삭제 포함)하면 이동 생성자는 암시적으로 선언되지 않으므로 이동도 막힌다 (`static_assert(!std::is_move_constructible_v<CriticalSection>)` 로 확인). 자원 소유권 이전·이동 가능한 가드 설계는 CPP-B07에서 다룬다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 36/36 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-exceptions -fno-rtti solution.cpp -o sol_asan && ./sol_asan   # 36/36, 경고 없음
```

데모 파일:

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_pitfalls.cpp -o demo && ./demo   # 임시 가드 버그 런타임 확인
c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR demo_pitfalls.cpp   # nodiscard/vexing parse/goto 에러
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o demo_cost.s demo_cost.cpp   # RAII vs goto 어셈블리 비교 (실행 금지)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 소멸자에서 `__enable_irq();` | 중첩·부트 초기·IRQ 금지 구간에서 인터럽트를 멋대로 켬 | `if ((saved_ & 1u) == 0) __enable_irq();` 또는 `__set_PRIMASK(saved_)` |
| 생성자에서 `__disable_irq(); saved_ = __get_PRIMASK();` | 끈 다음 읽으면 항상 1 → 영원히 다시 안 켜짐 | 먼저 읽고 그다음 끈다 |
| 저장값을 `static uint32_t s_saved;` (전역 1개) | 중첩되면 안쪽이 바깥 저장값을 덮어씀 | 객체 멤버(=스택/레지스터)에 저장 |
| `CriticalSection();` | 임시 객체가 `;` 에서 소멸 → 보호 없음 | `CriticalSection cs;` + 생성자 `[[nodiscard]]` |
| `CriticalSection cs();` | most vexing parse — 함수 선언 | `CriticalSection cs;` 또는 `CriticalSection cs{};` |
| 가드를 함수 맨 위에 두고 긴 작업까지 포함 | 인터럽트 지연(latency) 증가, 같은 IRQ 2회는 1회로 합쳐져 이벤트 손실 | `{ CriticalSection cs; /* 공유 데이터만 */ }` 로 블록을 좁힌다 |
| 가드 안에서 ISR이 세울 플래그를 `while (!flag) {}` 로 대기 | ISR이 실행될 수 없으니 영원히 안 옴 (데드락) | 대기는 임계 구역 밖에서 |
| 가드 안에서 공유값의 **참조**를 밖으로 반환 | 실제 읽기는 소멸 후(보호 밖)에 일어남 | 값으로 복사해 반환 (추가 연습 1의 `auto`) |

## 8. 면접 Q&A

<details>
<summary>Q1. 인터럽트를 끄는 임계 구역을 C++로 어떻게 구현하나요? 소멸자에서 그냥 enable 하면 안 되나요?</summary>

생성자에서 `saved = __get_PRIMASK(); __disable_irq();`, 소멸자에서 `if (!(saved & 1)) __enable_irq();` (또는 `__set_PRIMASK(saved)`). 그냥 enable하면 중첩 시 안쪽 가드가 바깥 구역 중간에 인터럽트를 켜고, 스케줄러 시작 전·플래시 작업처럼 일부러 꺼둔 구간도 깬다. 가드는 호출 문맥을 모르므로 "원래대로"가 유일하게 문맥 무관한 정답. 꼬리질문 "그럼 중첩 카운터 방식은?" → FreeRTOS `taskENTER_CRITICAL()` 이 카운터(`uxCriticalNesting`) + BASEPRI 방식인데, 태스크는 항상 인터럽트가 켜진 상태에서 돈다는 전제가 있고, ISR용으로는 이전 마스크를 반환·복원하는 `taskENTER_CRITICAL_FROM_ISR()` 를 따로 둔다 (추가 연습 2).

</details>

<details>
<summary>Q2. `saved = PRIMASK; cpsid i;` 사이에 인터럽트가 들어오면 저장값이 틀려지지 않나요?</summary>

아니다. Cortex-M에서 PRIMASK는 예외 진입/복귀 때 자동으로 바뀌지 않고, ISR은 PRIMASK를 들어올 때 상태로 두고 나가는 것이 규칙이다. 그래서 ISR이 끝나고 돌아오면 PRIMASK는 읽은 값 그대로이고, 이어서 `cpsid i` 를 하면 된다. 반대로 ISR 안에서 `cpsid i` 만 하고 나가면(짝 안 맞춤) 시스템 전체가 멈춘다 — RAII가 막아 주는 바로 그 버그.

</details>

<details>
<summary>Q3. 인터럽트를 끈 동안 들어온 인터럽트는 어떻게 되나요?</summary>

NVIC에 pending으로 래치되어 있다가 PRIMASK가 0이 되는 순간(우선순위가 허용하면) 처리된다. 단 IRQ마다 pending 비트는 1개라, 마스크 동안 같은 IRQ가 여러 번 오면 한 번으로 합쳐진다 — 그래서 임계 구역은 짧게 유지하고, 주변장치 쪽 FIFO/오버런 플래그로 손실을 감지한다. NMI·HardFault는 PRIMASK로 막히지 않는다.

</details>

<details>
<summary>Q4. RAII 가드는 예외가 없는(-fno-exceptions) 펌웨어에서도 의미가 있나요? 비용은?</summary>

있다. RAII의 핵심은 예외가 아니라 "스코프를 떠나는 모든 길(return/break/continue/goto/블록 끝)에서 소멸자 호출" 이다 ([stmt.jump]). 컴파일러가 출구마다 복원 코드를 정적으로 넣으므로 언와인드 테이블도 필요 없다. -O2에서 C `goto cleanup` 버전과 명령어가 완전히 같았다 (demo_cost). 객체 크기는 저장값 1워드, 보통은 레지스터에만 존재.

</details>

<details>
<summary>Q5. `CriticalSection();` 라고 쓰면 무슨 일이 일어나나요? 어떻게 막나요?</summary>

이름 없는 임시 객체라 그 full-expression 끝(`;`)에서 바로 소멸한다 — 끄자마자 다시 켜고, 이후 코드는 무방비. 문법상 합법이라 표시가 없으면 컴파일러는 침묵한다. 생성자에 `[[nodiscard]]` (C++20, P1771R1)를 붙이면 clang/GCC가 "ignoring temporary created by a constructor declared with 'nodiscard'" 경고를 내고, `-Werror` 로 빌드를 막을 수 있다. 꼬리질문 "`std::unique_lock<std::mutex>(m);` 은?" → 이건 임시 객체가 아니라 `m` 이라는 **새 변수 선언**으로 해석된다 ([stmt.ambig]) — 바깥 `m` 을 가리는 기본 생성 lock이 되어 아무것도 잠그지 않는다. 그래서 가드는 항상 이름을 붙인다.

</details>

<details>
<summary>Q6. 임계 구역 안의 공유 변수에 volatile 이 필요한가요?</summary>

임계 구역 안에서만 접근하는 변수라면, 진입/해제 함수가 컴파일러 배리어(CMSIS의 `asm volatile(... ::: "memory")`)이므로 그 경계를 넘어 접근이 캐시·재배치되지 않는다 → 보통 volatile 없이도 맞다. 하지만 임계 구역 **밖에서** 폴링하는 플래그(`while (!g_rx_done)`)는 volatile(또는 atomic)이 필요하다. 그리고 volatile은 원자성을 주지 않는다 — RMW 경쟁은 임계 구역이 해결한다 (C-B09 참고). 멀티코어라면 PRIMASK는 자기 코어만 막으므로 스핀락/하드웨어 세마포어가 따로 필요하다.

</details>

<details>
<summary>Q7. PRIMASK 대신 BASEPRI를 쓰는 이유는?</summary>

PRIMASK는 모든 설정 가능 우선순위 예외를 막아 모터 제어 같은 급한 IRQ의 지연도 늘린다. BASEPRI(ARMv7-M 이상)는 "우선순위 값 ≥ N" 인 IRQ만 막으므로 급한 IRQ는 계속 돈다 — 단, 그런 IRQ는 보호된 공유 데이터·RTOS API를 건드리면 안 된다 (FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY` 의 의미). M0/M0+에는 BASEPRI가 없다. 중첩 시 `MSR BASEPRI_MAX` 는 더 강하게 막는 방향으로만 쓰므로 진입에 쓰고, 복원은 저장값을 그대로 쓴다 (추가 연습 3, 실제 칩은 ARM-M10).

</details>

## 9. 추가 연습

### 연습 1 — 람다를 임계 구역에서 실행하는 `critical(f)`

`critical([&]{ return g_counter; })` 처럼 쓰는 함수 템플릿을 만들어라. `void` 를 돌려주는 람다도 되어야 하고, 람다가 **참조**를 돌려줘도 결과는 값이어야 한다.

<details>
<summary>답 보기</summary>

```cpp
template <class F>
auto critical(F&& f) { CriticalSection cs; return f(); }
```

`return f();` 는 `f()` 가 void여도 void 함수 안에서 합법이다 ([stmt.return]). 반환 타입을 `auto` 로 두면 참조가 벗겨져 **가드 안에서 복사**된다. `decltype(auto)` 를 쓰면 참조가 그대로 나가서 실제 읽기가 보호 밖에서 일어난다 — `static_assert(std::is_same_v<decltype(critical(counter_ref)), uint32_t>)` 로 고정했다.

</details>

### 연습 2 — 중첩 카운터 방식의 함정

`enter_critical()/exit_critical()` 를 전역 카운터로 구현하면(0 → 1에서 끄고, 1 → 0에서 켜기) 중첩은 잘 되지만 **이미 인터럽트가 꺼진 상태에서 시작하면** 어떻게 되나? 고쳐라.

<details>
<summary>답 보기</summary>

1 → 0에서 무조건 켜므로 부트 초기 같은 "원래 꺼져 있던" 상태를 깬다 (practice에서 재현). 고친 버전은 가장 바깥 진입(0 → 1) 때 PRIMASK를 전역 `g_outer_saved` 에 기억하고, 가장 바깥 해제 때 그 값으로 복원한다. 카운터 자체도 공유 데이터이므로 `__disable_irq()` **뒤에** 증가시킨다. RAII 가드는 이 상태를 각 객체(스택)에 저장하므로 전역 카운터가 아예 필요 없다. FreeRTOS Cortex-M 포트에서 `uxCriticalNesting` 의 초기값이 0이 아닌 값(`0xaaaaaaaa`)인 것도 스케줄러 시작 전에 exit가 인터럽트를 켜지 못하게 하려는 장치로 알려져 있다.

</details>

### 연습 3 — BASEPRI 가드

mock `__get_BASEPRI/__set_BASEPRI/__set_BASEPRI_MAX` 로 `BasePriGuard(level)` 을 만들어라. 바깥 0x40 안쪽 0x80 이면 안쪽에서도 0x40을 유지(더 약하게 풀리면 안 됨), 안쪽 0x20 이면 0x20으로 강화, 끝나면 각각 이전 값으로 복원.

<details>
<summary>답 보기</summary>

진입: `saved_ = __get_BASEPRI(); __set_BASEPRI_MAX(level);` — `BASEPRI_MAX` 쓰기는 새 값이 0이 아니고 (현재 0이거나 새 값이 더 작을 때 = 더 높은 우선순위까지 막을 때)만 반영된다. 복원: `__set_BASEPRI(saved_);` (BASEPRI_MAX로는 마스크를 낮출 수 없으므로 일반 쓰기). 우선순위 값은 작을수록 급하고, STM32는 상위 4비트만 구현되어 0x10 단위로 의미가 있다. BASEPRI=0x40일 때 우선순위 0x20 IRQ(예: 모터 PWM)는 계속 실행된다.

</details>

### 연습 4 — 임계 구역 길이 측정

DWT->CYCCNT를 흉내 낸 `g_cyccnt` 로, 임계 구역의 최대 유지 사이클을 기록하는 `TimedCriticalSection` 을 만들어라. 카운터가 한 바퀴 돌아도 맞아야 한다.

<details>
<summary>답 보기</summary>

멤버로 `CriticalSection cs_; uint32_t start_;` 를 **이 순서로** 선언한다 — 멤버는 선언 순서로 초기화되고([class.base.init]) 역순으로 소멸하며, 소멸자 본문이 멤버 소멸보다 먼저 실행된다([class.dtor]). 그래서 소멸자 본문에서 `held = g_cyccnt - start_` 를 계산하는 시점에 아직 마스크 상태이고, 그다음 `cs_` 가 복원한다. unsigned 뺄셈은 모듈로 2^32라 `0xFFFFFFF0 → 0x10` 도 0x20으로 맞게 나온다 (C-B02 참고). 실제 칩에선 이 최대값을 인터럽트 지연 예산과 비교한다.

</details>

### 연습 5 — 이름 없는 임시 가드를 런타임으로 잡기

`[[nodiscard]]` 가 없는 `PlainGuard` 로 `PlainGuard();` 버전과 `PlainGuard g;` 버전을 만들고, 함수 안 시점의 마스크 상태를 비교하라.

<details>
<summary>답 보기</summary>

`PlainGuard();` 버전은 `return irq_masked();` 시점에 이미 false — 컴파일 경고 0개로 통과한 버그가 런타임에만 드러난다. 그래서 가드 클래스 생성자에는 항상 `[[nodiscard]]` 를 붙이고 `-Werror` 로 빌드한다. 테스트로 "가드 안에서 마스크 상태"를 한 번이라도 확인하는 습관도 같은 버그를 잡는다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 19/19 passed
```

## 10. 핵심 정리

- RAII = 생성자에서 획득, 소멸자에서 해제. 스코프를 떠나는 **모든 길**(return/break/continue/goto/블록 끝)에서 소멸자가 실행된다 ([stmt.jump]). 예외 없는 빌드에서도 똑같이 동작.
- CriticalSection: **먼저 PRIMASK 읽고 → 끄고**, 소멸자에서 **저장값으로 복원**. "무조건 enable"은 중첩·부트 초기·의도적 금지 구간을 깬다.
- `return 공유값;` 은 소멸자보다 먼저 평가 → 가드 안에서 읽힌다 ([stmt.return]). 참조가 아니라 값으로 내보낸다.
- 마스크 중 IRQ는 pending으로 남았다가 복원 순간 실행. 같은 IRQ 여러 번은 1번으로 합쳐짐 → 임계 구역은 짧게, 안에서 대기 금지.
- `CriticalSection();` 은 즉시 소멸하는 임시 객체 버그 → 생성자에 `[[nodiscard]]` (C++20) + `-Werror`. `CriticalSection cs();` 는 함수 선언.
- C `goto cleanup` 과 -O2 생성 코드가 같다 (비용 0, 객체는 레지스터 하나). C++에서 goto가 초기화를 건너뛰면 컴파일 에러.
- 가드는 복사 금지(`= delete`) — 자세한 건 CPP-B07. BASEPRI 가드·실제 칩은 ARM-M10, atomic 대안은 CPP-A04.

## 참고자료

- [ARMv7-M Architecture Reference Manual (DDI 0403) — B1.4.3 PRIMASK/BASEPRI/FAULTMASK, CPS 명령](https://developer.arm.com/documentation/ddi0403/latest/)
- [CMSIS-Core — Core Register Access: `__disable_irq`, `__get_PRIMASK`, `__set_BASEPRI_MAX`](https://arm-software.github.io/CMSIS_6/latest/Core/group__Core__Register__gr.html)
- [cppreference — RAII](https://en.cppreference.com/w/cpp/language/raii), [`[[nodiscard]]`](https://en.cppreference.com/w/cpp/language/attributes/nodiscard)
- [P1771R1 — `[[nodiscard]]` for constructors](https://wg21.link/p1771r1), [P1301R4 — `[[nodiscard("should have a reason")]]`](https://wg21.link/p1301r4)
- C++20 표준(N4861): [stmt.jump]/2, [stmt.return], [stmt.dcl]/3, [class.temporary]/4, [class.dtor], [class.base.init], [dcl.attr.nodiscard]
- [FreeRTOS — taskENTER_CRITICAL / taskENTER_CRITICAL_FROM_ISR](https://www.freertos.org/taskENTER_CRITICAL_taskEXIT_CRITICAL.html)
- [Zephyr — irq_lock() / irq_unlock(key)](https://docs.zephyrproject.org/latest/kernel/services/interrupts.html)
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors*, 7장 (예외·인터럽트 마스킹)
