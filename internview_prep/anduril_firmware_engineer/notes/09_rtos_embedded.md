# 09. RTOS & 임베디드 개념 (RTOS & Embedded Systems) — Q96~Q102 ⏱️

> 자료구조/알고리즘이 아니라 **실시간 시스템의 개념**을 코드로 증명하는 파트.
> 워치독, 협력형 스케줄러, ISR 공유자원 보호, 우선순위 역전, 인터럽트 지연,
> MMIO volatile, 타이머 휠 — Anduril 류 방산/드론 펌웨어 인터뷰의 "말로 설명하고
> 화이트보드로 증명하라"는 단골 주제들이다.

---

## 왜 Anduril 펌웨어 인터뷰에 나오나

- 드론 FCS/무장 시스템은 **경성 실시간(hard real-time)**. "인터럽트가 늦으면 왜
  안 되나, 얼마나 늦을 수 있나"를 정량적으로 답할 수 있어야 한다 (Q99, Q100).
- 센서 ISR ↔ 제어 루프(main) 데이터 공유는 매 프로젝트에 있다. **레이스를 만들고
  고칠 수 있나**가 실전 실력의 척도 (Q98).
- 워치독(Q96)은 안전(safety) 필수. "행(hang)에서 어떻게 자동 복구하나"는 항상 나온다.
- RTOS 없이 도는 소형 MCU(센서 허브, ESC)도 많다 → **협력형 스케줄러/타이머 휠**을
  직접 짤 수 있어야 한다 (Q97, Q102).
- MMIO `volatile`(Q101)을 빼먹어 폴링 루프가 무한루프로 최적화되는 버그는 신입이
  반드시 한 번 겪는 통과의례. 인터뷰어가 즐겨 묻는다.

---

## 핵심 개념 치트시트

| 개념 | 한 줄 요약 |
|------|-----------|
| Watchdog | down-counter. 정기적 `pat`(reload) 없으면 0 도달 → 리셋. 행 자동 복구. |
| 협력형 스케줄러 | 함수 포인터 + 주기. `now % period == 0` 이면 실행. 선점 없음(짧게 반환). |
| 임계구역(IRQ 마스킹) | `s=save(); disable; RMW; restore(s);` — 저장/복원이라 **중첩 안전**. |
| `_Atomic` | 단일 워드 RMW 를 불가분하게. 멀티코어/멀티스레드 lost update 방지. |
| 우선순위 역전 | 저(低)가 자원 보유 → 고(高) 블록 → 중(中)이 저를 선점 → 고 무한 대기. |
| 우선순위 상속 | 보유자를 대기자 최고 우선순위로 **일시 승격** → 역전 구간 경계(bounded). |
| 인터럽트 지연 | sync+임계구역+상위ISR+문맥저장+prologue. **임계구역 최소화**가 핵심 레버. |
| MMIO `volatile` | 컴파일러의 제거/캐시/재정렬 방지. 폴링 루프 필수. |
| 타이머 휠 | `slot=(now+delay)%N`, `rounds=(delay-1)/N`. O(1) 등록, tick당 슬롯만 순회. |

---

## (Q96) 워치독 타이머

```c
void wdt_tick(wdt_t *w) {
    if (!w || w->expired) return;                 // 만료 후 래치
    if (w->counter == 0 || --w->counter == 0) {   // timeout==0 즉시 만료 엣지 포함
        w->expired = true;
        if (w->cb) w->cb(w->ctx);                 // 실제로는 시스템 리셋
    }
}
void wdt_pat(wdt_t *w) { if (w && !w->expired) w->counter = w->timeout; }
```

- **pat(=kick/feed)** 은 "나 살아있다" 신호. main 루프가 정상이면 주기적으로 호출.
- 만료는 **한 번만** 콜백을 부르고 래치(latch)한다 → 실제 HW 리셋의 단발성 모방.
- **함정**: pat 을 ISR 안에서 하면 main 이 죽어도 워치독이 계속 리로드되어 무용지물.
  "정상 경로가 진짜로 돌 때만" pat 해야 한다(예: 여러 태스크의 완료 플래그를 AND).

---

## (Q97) 협력형 라운드로빈 스케줄러

```c
void sched_run_tick(scheduler_t *s) {
    s->now++;
    for (size_t i = 0; i < s->count; ++i)
        if (s->now % s->tasks[i].period == 0) {   // 주기 도래
            s->tasks[i].runs++;
            s->tasks[i].fn(s->tasks[i].ctx);      // 등록 순서(라운드로빈)로 실행
        }
}
```

- **협력형(cooperative)**: 선점이 없으므로 각 태스크는 **짧게 실행 후 반환**해야 한다.
  블로킹/긴 루프 하나가 전체를 멈춘다. → 상태 머신으로 잘게 쪼개는 습관.
- period=1,2,3 을 6 tick 돌리면 6,3,2 회 실행. 결정론적.
- **RTOS 와 차이**: 우선순위/선점/블로킹이 없다. 단순·결정론적·저오버헤드가 장점.

---

## (Q98) ISR ↔ main 공유 자원 — 레이스와 수정

### 레이스는 진짜다 (재현)

```c
uint32_t tmp = shared.value;   // main 이 0 을 읽음
/* --- 여기서 ISR 발생: shared.value += 100  → 100 --- */
shared.value = tmp + 1;        // main 이 1 을 씀 → ISR 의 +100 유실!
```

RMW(read-modify-write)는 원자적이지 않다. 읽기와 쓰기 사이에 ISR 이 끼면 갱신이 사라진다.

### 해법 A — 임계구역(인터럽트 마스킹), 중첩 안전 관용구

```c
uint32_t s = irq_save();   // prev = PRIMASK; __disable_irq(); return prev;
/* 임계구역: 이 사이엔 ISR 이 못 들어온다 (RMW 불가분) */
shared.value++;
irq_restore(s);            // PRIMASK = prev  ← 무조건 enable 이 아니라 "이전 값"!
```

- **왜 save/restore 인가**: 무조건 `enable` 로 복원하면, 이미 마스킹된 상위 구역에서
  호출했을 때 임계구역을 조기에 열어버린다. 이전 값 복원이라야 **중첩(nesting) 안전**.
- 임계구역은 **최대한 짧게**. 길면 인터럽트 지연(Q100)이 늘어난다.

### 해법 B — `_Atomic` (단일 워드)

```c
atomic_fetch_add_explicit(&counter, 1, memory_order_relaxed);
```

- 카운터 하나 증가처럼 **단일 워드 RMW** 는 원자 연산 하나로 끝. 멀티코어에서도 안전.
- 여러 변수의 일관성(불변식)을 지켜야 하면 원자 하나로는 부족 → 임계구역/락 필요.
- 단일코어 베어메탈에서 ISR↔main 한 워드 공유는 `volatile` + "쓰기 순서 보장"으로도
  전통적으로 충분하지만, 멀티코어/컴파일러 재정렬을 고려하면 `_Atomic` 이 이식성 정답.

---

## (Q99) 우선순위 역전 & 상속/천장

### 무엇이 문제인가

우선순위 H > M > L (단일 CPU, 선점형).

1. L 이 공유자원 R 을 잠그고 임계구역 실행 중.
2. H 가 R 을 원해 **블록**됨 (L 보유).
3. R 과 **무관한** M 이 ready → M(2) > L(1) 이라 M 이 L 을 선점.
4. → H 는 M 이 끝날 때까지 대기. **중간 우선순위가 고우선순위를 무한정 지연**(역전!).

> 실제 사례: **1997 Mars Pathfinder** 가 이 버그로 반복 리셋. VxWorks 의 우선순위
> 상속을 켜서 원격 패치로 해결.

### 해법 1 — 우선순위 상속 (Priority Inheritance)

```c
int pi_effective_priority(int base, const int *waiters, size_t n) {
    int eff = base;
    for (size_t i = 0; i < n; ++i) if (waiters[i] > eff) eff = waiters[i];
    return eff;   // 보유자를 "최고 대기자" 우선순위로 일시 승격
}
```

- L 이 H 의 우선순위를 물려받아 M 보다 높아짐 → L 이 즉시 R 을 마치고 반납 → H 진행.
- H 의 대기 시간이 **"L 의 임계구역 잔여분"으로 경계**된다 (bounded).
- 본 세트의 `simulate_h_finish()`: 상속 없음이면 H 가 **11 tick**에, 상속이면 **6 tick**에
  완료 → 역전이 정량적으로 사라짐을 보인다.

### 해법 2 — 우선순위 천장 (Priority Ceiling Protocol)

```c
int pcp_ceiling(const int *user_prios, size_t n);  // 자원 사용 태스크 중 최고 우선순위
```

- 자원을 잠그는 순간 보유자를 **자원의 천장 우선순위**로 즉시 올린다.
- 상속보다 강한 보장: **교착(deadlock) 예방** + 태스크당 최대 1회 블로킹(chained blocking 방지).

---

## (Q100) 인터럽트 지연 (Interrupt Latency)

```c
worst_case = sync            // 현재 명령/파이프라인 동기화 완료
           + disable_window  // 인터럽트가 disable 됐던 최장 임계구역  ← 우리가 통제!
           + higher_isr      // 먼저 처리되는 상위/동급 우선순위 ISR 합
           + context_save    // CPU 상태 스태킹(문맥 저장)
           + isr_prologue;   // 벡터 페치 + 핸들러 진입
```

- **줄일 수 있는 가장 큰 레버 = `disable_window`**: 임계구역(인터럽트 마스킹 구간)을
  짧게. `max_disable_window()` 로 여러 임계구역 중 최장을 찾아 그게 경계값이 된다.
- ISR 은 **짧게**: 무거운 일은 플래그만 세우고 main/하위 우선순위로 defer (top/bottom half).
- 실시간 검증: `worst_case + ISR_실행시간 <= deadline` (`meets_deadline`).
- **오버플로 주의**: 기여 요소 합산은 포화 덧셈(saturating)으로 UB 회피.
- 측정 팁: 단조 시계(`CLOCK_MONOTONIC`)로 임계구역/ISR 길이를 실측해 경계를 근거화.
  (실 타깃에선 GPIO 토글 + 로직 애널라이저, DWT 사이클 카운터 사용)

---

## (Q101) 메모리맵 레지스터 접근 — `volatile`

```c
#define UART_SR (*(volatile uint32_t *)0x40011000u)   // 실제 타깃
while (!(UART_SR & TXE)) { }   // volatile 없으면 컴파일러가 "안 변하는 값"으로 보고
                               // 조건을 한 번만 읽어 무한루프로 최적화!  ← 대표 버그
```

- `volatile` = "이 메모리는 내가 모르는 사이 바뀔 수 있다" → 컴파일러가 **매 접근마다
  실제 메모리를 읽고/쓰고, 순서를 유지, 제거하지 않음**.
- 필드 수정(RMW)은 **읽고-마스크-쓰기**: `v &= ~mask; v |= (val<<shift)&mask;`
  (shift ≥ 32 은 UB → 반드시 가드).
- **함정**: `volatile` 은 원자성/메모리배리어를 보장하지 **않는다**. ISR 공유엔
  `volatile` + 임계구역/`_Atomic` 을 함께 써야 한다 (Q98).
- **함정**: 32비트 레지스터를 바이트 단위로 접근하면 안 되는 주변장치가 있다 →
  접근 폭(access width)을 데이터시트대로.

---

## (Q102) 소프트웨어 타이머 휠 (malloc 없이)

```c
// 등록: delay tick 뒤 만료
slot   = (now + delay) % TW_SLOTS;
rounds = (delay - 1) / TW_SLOTS;   // 그 슬롯을 몇 바퀴 더 지나야 하나

// tick: 현재 슬롯만 순회
now++; s = now % TW_SLOTS;
for (각 타이머 in slot[s])
    if (rounds == 0) 발동 + 반납;   // 정확히 now == 등록시각+delay 에 발동
    else             rounds--;
```

- **왜 휠인가**: 단순 리스트는 tick 마다 전체를 스캔(O(n)). 휠은 **해당 슬롯만** 본다.
  등록 O(1), tick당 O(슬롯 내 타이머 수). 대량 타이머에 유리 (Linux 커널이 사용).
- `rounds` 공식 `(delay-1)/N` 검증: delay<N → 0(이번 바퀴), delay=N → 0(첫 통과에 발동),
  delay=2N → 1. off-by-one 주의.
- **노드는 정적 풀 + free-list** (힙 없음, 결정론적) — Q75 와 동일 임베디드 관용구.
- **함정**: 콜백 안에서 타이머를 재등록/취소할 때 순회 리스트가 깨지지 않게
  **먼저 unlink 후 콜백** 호출 (본 구현이 그 순서).

---

## 인터뷰 팔로업 & 임베디드 함정 모음

- **Q. 워치독 pat 을 어디서?** → 정상 경로가 실제로 돈다는 증거가 모일 때만. ISR 단독 pat 금지.
- **Q. `_Atomic` vs 임계구역?** → 단일 워드면 원자, 여러 변수 불변식이면 임계구역/락.
- **Q. 왜 IRQ 복원이 save/restore?** → 중첩 임계구역에서 조기 enable 방지.
- **Q. 우선순위 역전을 어떻게 없애나?** → 상속(경계) 또는 천장(교착 예방+1회 블로킹).
- **Q. 인터럽트 지연을 어떻게 줄이나?** → 임계구역·ISR 최소화, 무거운 일은 defer.
- **Q. `volatile` 이면 스레드 안전?** → 아니다. 원자성/배리어 없음. 별도 보호 필요.
- **함정: 협력형 스케줄러에서 한 태스크가 블로킹** → 전체 정지. 상태머신으로 분할.
- **함정: 타이머 휠 rounds off-by-one** → `(delay-1)/N` 로 검증.
- **함정: ISR 과 main 이 같은 free-list/버퍼 공유** → 경쟁. SPSC 설계 또는 임계구역.

---

## 복잡도 요약

| 연산 | 시간 | 공간 |
|------|------|------|
| wdt_tick / wdt_pat | O(1) | O(1) |
| sched_run_tick | O(태스크 수) | O(1) |
| irq_save / restore | O(1) | O(1) |
| atomic_add | O(1) | O(1) |
| pi_effective_priority / pcp_ceiling | O(대기자/사용자 수) | O(1) |
| irq_latency_wc | O(1) | O(1) |
| reg_* (MMIO) | O(1) | O(1) |
| tw_start | O(1) | O(1) (정적 풀) |
| tw_tick | O(슬롯 내 타이머 수) | O(1) |
