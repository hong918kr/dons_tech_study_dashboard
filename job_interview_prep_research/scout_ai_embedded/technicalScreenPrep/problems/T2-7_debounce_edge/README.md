# T2-7 · Debounce an Over-Temp Alert Pin

> **ID**: T2-7 · **Priority**: P1 · **Pattern**: streaming state machine · consecutive-sample counter · **Time**: 20 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T2-7` / `./run.sh T2-7 sol`

## 1. Problem

A temperature sensor drives an active-high **ALERT** pin when it overheats. A 1 kHz timer ISR samples the pin and calls `deb_update()` once per sample. Motor EMI puts short glitches on the line, so a single sample cannot be trusted.

Implement a debouncer with edge detection:

- The debounced (**stable**) level changes only after the raw input has been **different from the stable level for `n_required` consecutive samples**. Any sample equal to the stable level resets the count.
- On the sample where the stable level changes, return `EDGE_RISING` (0 → 1) or `EDGE_FALLING` (1 → 0) **exactly once**. Otherwise return `EDGE_NONE`.
- `raw` is 0 or non-zero (non-zero means high). `n_required == 0` is treated as 1.
- `deb_init()` sets the starting stable level; no edge is reported for it.
- O(1) time and memory per sample. No `malloc`.

```c
typedef enum { EDGE_NONE = 0, EDGE_RISING, EDGE_FALLING } Edge;
typedef struct { uint8_t stable; uint16_t count; uint16_t n_required; } Debouncer;

void deb_init(Debouncer *d, uint16_t n_required, uint8_t initial);
Edge deb_update(Debouncer *d, uint8_t raw);
```

### Example

```
n_required = 3, initial = 0
i   :  0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
raw :  0 1 0 1 1 1 1 0 1 1  0  0  0  1  0  0

edges: RISING at i = 5, FALLING at i = 12
       (the single-sample glitches at i = 1, 7, 13 are ignored)
```

### Constraints

- Called from an ISR: no blocking, no printing, no dynamic memory.
- The input may never settle (pure noise); the function must simply keep returning `EDGE_NONE`.

### Clarifying questions to ask (first 3 minutes)

1. Does "stable" mean N consecutive identical samples, or N out of the last M?
2. Should the edge be reported on the N-th sample, or should I report the time of the first sample of the new level?
3. What is the initial level at power-up — assume low, or take the first sample?
4. Is the debounce defined in samples or in milliseconds (is the sample rate fixed)?
5. Do we need both edges, or only the rising (alert asserted) edge?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh T2-7`로 바로 테스트하세요. 테스트 6개가 들어 있습니다. `run()`은 테스트용 헬퍼라 그대로 두면 됩니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 무엇을 기억해야 하나?</summary>

두 값이면 충분합니다. **지금 확정된 레벨(`stable`)**과 **stable과 다른 샘플이 연속으로 몇 개 왔는지(`count`)**. 입력이 0/1 두 값뿐이라 "stable과 다르다" = "새 후보 레벨과 같다"이므로 후보 레벨을 따로 저장할 필요가 없습니다.

</details>

<details>
<summary>Hint 2 — 샘플 하나가 오면 경우는 세 가지</summary>

1. `raw == stable` → `count = 0` (글리치가 끝남), NONE
2. `raw != stable`이고 `++count < N` → 아직 판단 보류, NONE
3. `++count == N` → `stable = raw`, `count = 0`, 방향에 따라 RISING/FALLING

</details>

<details>
<summary>Hint 3 — 엣지가 한 번만 나오게</summary>

엣지를 낸 직후 `stable`이 새 레벨로 바뀌므로, 다음 샘플부터는 1번 경우(`raw == stable`)가 되어 자동으로 조용해집니다. "이전 raw와 비교"로 엣지를 만들면 글리치마다 엣지가 튀니 주의하세요.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

위는 raw 입력, 아래는 디바운스된 `stable`입니다. 노란 칸은 한 샘플짜리 글리치로, `count`가 1까지 올라갔다가 바로 0으로 돌아갑니다. 같은 레벨이 3번 연속 이어진 순간(i = 5, i = 12)에만 `stable`이 바뀌고 엣지가 한 번 나옵니다.

<div class="chart" data-src="chart1.svg" data-caption="N = 3 디바운스: 글리치(i = 1, 7, 13)는 무시되고, raw가 바뀐 뒤 3번째 샘플에서 RISING(i = 5)·FALLING(i = 12)이 한 번씩 나옴. 대가로 2샘플(2 ms) 지연이 생김"></div>

디바운스는 **지연과 맞바꾸는 신뢰도**입니다. raw는 i = 3에 올라갔지만 확정은 i = 5입니다. N을 키우면 글리치에 강해지지만 과열 알림이 그만큼 늦어집니다. 면접에서는 "N × 샘플 주기 = 최대 지연(여기서는 3 ms)"을 꼭 말하세요.

### 4.2 손으로 따라가기 (trace)

`count`, `stable`은 그 샘플을 처리한 **뒤**의 값입니다.

| i | raw | raw == stable? | 동작 | count | stable | 반환 |
|---|---|---|---|---|---|---|
| 0 | 0 | ✓ | reset | 0 | 0 | NONE |
| 1 | 1 | ✗ | 1 < 3 | 1 | 0 | NONE |
| 2 | 0 | ✓ | reset (글리치 끝) | 0 | 0 | NONE |
| 3 | 1 | ✗ | 1 < 3 | 1 | 0 | NONE |
| 4 | 1 | ✗ | 2 < 3 | 2 | 0 | NONE |
| 5 | 1 | ✗ | 3 == 3 → **commit** | 0 | 1 | **RISING** |
| 6 | 1 | ✓ | reset | 0 | 1 | NONE |
| 7 | 0 | ✗ | 1 < 3 | 1 | 1 | NONE |
| 8 | 1 | ✓ | reset (글리치 끝) | 0 | 1 | NONE |
| 9 | 1 | ✓ | reset | 0 | 1 | NONE |
| 10 | 0 | ✗ | 1 < 3 | 1 | 1 | NONE |
| 11 | 0 | ✗ | 2 < 3 | 2 | 1 | NONE |
| 12 | 0 | ✗ | 3 == 3 → **commit** | 0 | 0 | **FALLING** |
| 13 | 1 | ✗ | 1 < 3 | 1 | 0 | NONE |
| 14 | 0 | ✓ | reset | 0 | 0 | NONE |
| 15 | 0 | ✓ | reset | 0 | 0 | NONE |

### 4.3 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `raw ? 1u : 0u`로 먼저 정규화합니다. GPIO 레지스터에서 읽은 값은 `0x20`처럼 비트 위치 그대로일 수 있어서 `raw == stable`을 바로 비교하면 틀립니다(테스트 6번의 `7`).
- `++d->count < d->n_required`: `n_required`를 `deb_init`에서 최소 1로 고정했기 때문에 N = 1이면 첫 다른 샘플에서 바로 commit됩니다.
- `count`는 N에 닿으면 즉시 0으로 돌아가므로 `uint16_t`가 넘칠 일이 없습니다.

### 4.4 복잡도

- 샘플당 시간 **O(1)**, 분기 2개.
- 메모리 **O(1)** — 구조체 하나(6바이트). 핀이 여러 개면 핀마다 하나씩.

---

## 5. 임베디드 관점으로 말할 것

- **어디서 부르나**: 1 kHz 타이머 ISR에서 GPIO를 읽고 `deb_update()`를 부른 뒤, 엣지가 나오면 플래그나 이벤트 큐(→ [T2-1](../T2-1_ring_buffer/index.html))에 넣고 처리는 메인 루프에서 합니다. ISR 안에서 로그를 찍거나 모터를 끄는 긴 일을 하지 않습니다.
- **GPIO 엣지 인터럽트만 쓰면 안 되는 이유**: 글리치마다 인터럽트가 수십 번 들어와 CPU를 잡아먹습니다. 쓰더라도 첫 엣지에서 인터럽트를 끄고 타이머로 N ms 뒤 레벨을 다시 확인하는 방식이 일반적입니다.
- **하드웨어 대안**: RC 필터 + 슈미트 트리거 입력, 또는 MCU의 디지털 글리치 필터 설정. 소프트웨어 디바운스는 이걸 보완합니다.
- **안전 관점**: 과열 ALERT처럼 중요한 신호는 **올라갈 때는 빠르게(N 작게), 내려갈 때는 느리게(N 크게)** 비대칭으로 두기도 합니다. 히스테리시스와 같은 발상입니다(→ [T1-2](../T1-2_streaming_hysteresis/index.html)).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Debounce in milliseconds instead of samples, with irregular sampling. | `count` 대신 "다른 레벨이 처음 보인 시각" `t_first`를 저장하고, `now - t_first >= debounce_ms`이면 commit합니다. 시각 비교는 `uint32_t` 뺄셈으로 해서 랩어라운드에 안전하게 합니다. |
| What if the line is so noisy that it never has N clean samples in a row? | 연속 대신 **적분기(integrator)** 방식: 높으면 `count++`(최대 N에서 포화), 낮으면 `count--`(0에서 포화), N에 닿으면 1, 0에 닿으면 0. 가끔 섞인 노이즈에도 결국 수렴합니다. |
| Debounce 32 GPIO pins at once. | 핀마다 구조체를 두거나, 포트 레지스터 전체를 한 번에 읽어 비트 단위로 처리하는 **vertical counter**(비트마다 2비트 카운터를 XOR/AND로 갱신)를 씁니다. 32핀을 명령 몇 개로 처리합니다. |
| Asymmetric thresholds for assert and release? | `n_rise`, `n_fall` 두 값을 두고 `level`에 따라 비교 대상을 고릅니다. 과열 알림은 빠르게 켜고 천천히 끕니다. |
| How do you test this on hardware? | 함수 발생기나 다른 MCU 핀으로 정해진 길이의 글리치를 넣고, 엣지 출력 핀을 로직 분석기로 봅니다. 단위 테스트에서는 지금처럼 샘플 배열을 넣습니다. |

## 7. 자주 하는 실수

- 이전 raw와 현재 raw를 비교해서 엣지를 만듦 → 글리치마다 RISING/FALLING이 튐(테스트 2번 실패)
- 다른 샘플이 왔는데 `count`를 리셋하지 않음 → 글리치를 사이에 둔 샘플이 합쳐져 너무 일찍 commit(테스트 5번 실패)
- commit 뒤 엣지를 계속 반환 → "길게 높음" 테스트 3번 실패
- `raw`를 0/1로 정규화하지 않음 → 테스트 6번 실패
- `n_required == 0`을 따로 정하지 않음 → 구현에 따라 "절대 commit 안 됨"이나 "매번 commit"이 갈림. `deb_init`에서 1로 고정하고 그 사실을 말로 밝힙니다
