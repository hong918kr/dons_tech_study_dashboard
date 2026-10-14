<!-- theme: pastel -->
# Scout AI — Technical Screen 준비 (CoderPad · C/C++ · 45분)

> **작성일**: 2026-10-12 · **갱신**: 2026-10-13 (리크루터 공식 메일 반영) · **인터뷰**: ⚠ 일정 미선택 — Ashby 링크로 **10-14(수) 오전 11:19 PT 전까지** 가능 시간 제출 · **형식**: CoderPad, 언어 자유(C 추천), 45분 = 15분 경험 + 30분 코딩
> **컨텍스트**: [포지션 컨텍스트](scout_ai_embedded_context.html) · **이 문서의 원본**: `2026-10-12_scout_technical_screen_prep.md`
> **연습 사이트**: [문제별 풀이 페이지 (영어 문제, 한국어 풀이, 차트, 30분 타이머)](technicalScreenPrep/index.html)
> 신뢰도: `[확인됨]` = 리크루터가 직접 말한 것(콜 10-12, 메일 10-12) · `[추정]` = 그 말을 바탕으로 한 해석

---

## 0. TL;DR

- **⚠ 지금 할 일** [확인됨 — 메일 10-12 11:19 AM]: 메일의 Ashby 링크(Schedule Interview Here)로 **48시간 안에, 즉 10-14(수) 오전 11:19 PT 전까지** 가능 시간을 보낸다. 추천 날짜는 §6.0
- **메일에서 새로 확인된 것** [확인됨]: ① 평가 범위에 **"discussion of your experience building and scaling hardware systems"**가 들어 있다 → 15분 경험 파트는 "자기소개"가 아니라 **하드웨어 시스템을 만들고 규모를 키운 경험**을 묻는다(§7) ② **"Practical technical exercises"**(복수형) → 짧은 문제 2개나, 본 문제 + debug/확장 질문일 수 있다 [추정] ③ 언어는 **가장 편한 언어** 자유 선택 ④ 평가 기준은 결과만큼 **사고 과정·소통·협업**(§2.6)
- **형식** [확인됨]: CoderPad, C 또는 C++. 45분 중 15분은 경험 이야기, **30분은 코딩 1문제**. 엣지 케이스 테스트를 직접 하고, 임베디드 관점을 본다
- **문제 성격** [확인됨]: "LeetCode-ish"이면서 Scout 엔지니어링에 가까운 문제. 키워드는 **Array · Item · State · Edge**, **debug**, **embedded constraint**, **센서**, **time · temp**, **과열 감지("return overheating")**
- **가장 유력한 문제** [추정]: `(시간, 온도)` 샘플 배열이나 스트림을 받아 **과열 구간이나 과열 이벤트를 반환**하는 문제. 상태 머신(정상 ↔ 과열), 히스테리시스·연속 N회 확인, 경계 조건이 채점 포인트일 가능성이 높다
- **언어 추천**: **C**. 평소 쓰는 언어이고, "호출자가 넘긴 버퍼에 결과를 쓴다" 같은 패턴 자체가 임베디드 제약을 보여 준다. 결과가 가변 길이면 `out` 배열 + `out_cap` + 반환 개수 패턴을 쓴다
- **최우선 드릴**: T1-1(과열 구간) → T1-2(스트리밍 히스테리시스) → T1-3(이동 평균·상승률) → T1-5(raw 레지스터 → 온도) → Debug 드릴
- **Verkada 때와 비교**: 그때는 "블로킹 벤더 API를 thread-safe로 감싸기 + 히스토리 조회"였다. Scout는 30분 1문제라 **그보다 작고 단일 스레드인 배열·상태 문제일 가능성이 크다** [추정]. 동시성 유형(T1-8)은 읽어 두는 정도로 준비한다

---

## 1. 리크루터 단서 → 해석 → 준비

| 리크루터가 한 말 [확인됨] | 해석 [추정] | 준비 |
|---|---|---|
| Leetcode-ish, Scout engineering type of problem | 알고리즘은 쉬운~중간. 포장이 센서·로봇 도메인 | Tier 1 문제 은행 |
| CoderPad, C or C++ | 자동완성 거의 없음, 직접 `main()`에서 테스트 | §2.4 템플릿을 외워서 30초 안에 입력 |
| 15 min about you → 30 min coding | 코딩 시간이 짧다. 설계 토론보다 **완성된 동작하는 코드** | §2 30분 운영법 |
| test edge case | 면접관이 엣지 케이스를 직접 던지거나, 내가 테스트를 써야 함 | §2.2 엣지 케이스 체크리스트 |
| embedded angle / embedded constraint | malloc 금지, 고정 메모리, 정수 연산, 오버플로, 타임스탬프 랩어라운드, O(1) 메모리 스트리밍 | §2.3 임베디드 포인트, §5 치트시트 |
| Array, Item, State, Edge | **Array** = 샘플 배열 · **Item** = 구조체 레코드(센서 ID·시간·온도) · **State** = 상태 머신 · **Edge** = 경계 조건 | T1-1·T1-2·T1-4 |
| debug | 버그 있는 코드를 고치거나, 내 코드를 디버깅하는 과정을 보여 주기 | §4 Debug 드릴 |
| Sensor, time temp, want to detect, over head | 온도 센서 시계열에서 과열 감지. "over head"는 **threshold 초과**나 **오버헤드(메모리·CPU 비용)** 둘 중 하나로 들림 → 둘 다 설명할 준비 | T1-1~T1-3, 복잡도·메모리 말하기 |
| return over heating | 과열 여부, 과열 시작 시점, 과열 구간 목록 중 하나를 **반환** | T1-1(구간 목록), T1-2(이벤트) |
| (메일) Practical technical exercises, problem solving | 복수형 → 30분 안에 **본 문제 + 변형/확장** 또는 **짧은 문제 2개** [추정] | 한 문제를 20분 안에 끝내고 남는 시간에 follow-up을 받는 연습 |
| (메일) **discussion of your experience building and scaling hardware systems** | 15분 경험 파트의 주제. "무엇을 만들었고, 1대 → 다수/양산으로 키울 때 무엇이 깨졌고 어떻게 해결했나" | **§7** 스토리 3개 |
| (메일) choose the coding language you're most comfortable with | C/C++ 외 언어도 허용된다는 뜻. 임베디드 직군이므로 C가 가장 안전 | C로 확정 |
| (메일) Tips: think aloud, clarifying Qs, collaborative, don't rush | 정답만큼 **과정**을 채점한다 | **§2.6** 행동 규칙 |

---

## 2. 30분 코딩 운영법

### 2.1 타임라인

| 시간 | 할 일 | 말로 할 것 (영어) |
|---|---|---|
| 0–3분 | 문제를 다시 말하고 **질문 3~5개** | "Let me restate the problem…", "A few clarifying questions…" |
| 3–6분 | 예시 하나를 손으로 풀고, 접근법과 복잡도를 말함 | "I'll do a single pass with a small state machine — O(n) time, O(1) extra memory." |
| 6–20분 | 코드 작성 (함수 먼저, 테스트는 나중) | 변수 이름을 말하면서 쓴다. 침묵 30초 이상 금지 |
| 20–27분 | `main()`에 테스트 3~5개: 정상 1, 엣지 2~3 | "Let me test the empty input and the case where it's still overheating at the end." |
| 27–30분 | 복잡도, 임베디드 개선점, follow-up | "On an MCU I'd avoid floats, use fixed-point, and keep state per sensor in a static array." |

### 2.2 엣지 케이스 체크리스트 (센서 문제용)
- 빈 입력 `n == 0`, 샘플 1개
- **정확히 threshold와 같은 값** — `>` 인지 `>=` 인지 질문
- 끝까지 과열 상태로 끝남 → 열린 구간을 닫아 줘야 함
- 시작부터 과열 상태
- 스파이크(1샘플만 튐), 경계에서 깜빡임(flapping) → 히스테리시스·디바운스
- 타임스탬프: 중복, 역순, 큰 공백(드롭아웃), `uint32_t` **랩어라운드**
- 센서 에러 값(예: `INT16_MIN`, 범위 밖 −40~150°C 초과), NaN(float인 경우)
- 출력 버퍼가 꽉 참(`out_cap` 초과) → 잘라내고 필요한 개수를 반환
- 음수 온도(정수 나눗셈 반올림 방향), 정수 오버플로(합계)

### 2.3 처음 3분에 할 질문
1. Are samples sorted by time and evenly spaced? Can timestamps wrap or go backwards?
2. Is "overheating" `>= threshold` or `> threshold`? Does it need to last for a minimum duration or N consecutive samples?
3. Should recovery use a lower threshold (hysteresis)?
4. What should I return — a boolean, the first overheat time, or every interval? Where does an interval end — at the last hot sample or the first cool one?
5. Units — integer tenths of a degree, or float? Any invalid sensor values I should skip?
6. Is this batch over an array, or streaming one sample at a time? Any memory limits?

### 2.4 CoderPad에 30초 안에 쓰는 템플릿 (C)
```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* solution here */

int main(void)
{
    /* tests here */
    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
```

### 2.5 임베디드 관점으로 말할 포인트
- **메모리**: 스트리밍이면 O(1) 상태만 유지. `malloc` 대신 호출자 버퍼나 `static` 배열
- **정수 연산**: 온도를 0.1°C나 0.01°C 단위 `int16_t`/`int32_t`로. FPU 없는 MCU에서 float는 비싸다
- **오버플로**: 합계는 넓은 타입(`int32_t`)으로. 평균 분모 0 체크
- **시간**: `uint32_t` ms 틱은 49.7일마다 랩 → `(uint32_t)(now - start) >= timeout`으로 비교
- **ISR 맥락**: ISR에서는 샘플만 넣고(링버퍼), 판단은 태스크에서. 공유 변수는 `volatile`/atomic, 읽고 지우기는 크리티컬 섹션
- **신뢰성**: 센서 고장(고정값·드롭아웃·스파이크)이면 과열 판단을 하지 말고 fault로 보고 — 안전 측(fail-safe) 동작 언급

### 2.6 리크루터 Tips → 행동 규칙 (메일 10-12)

| 메일의 Tip [확인됨] | 내가 할 행동 | 입에 붙일 문장 (영어) |
|---|---|---|
| Fundamentals of your primary language | 템플릿·`stdint` 타입·구조체·포인터 문법에서 막히지 않기. 연습 사이트의 stub을 백지에서 다시 쓰기 | — |
| Communicate your thought process | 코드 쓰기 전에 접근법과 복잡도를 말하고, 쓰면서 변수의 의미를 말한다 | "My plan is a single pass with a two-state machine; I'll keep start and last-hot time." |
| Ask clarifying questions | §2.3 질문 중 3~5개. 답을 받으면 **가정을 화면 주석으로 적는다** | "Before I code — is a reading exactly at the threshold hot?" · "Let me write down the assumptions as a comment." |
| Be clear and concise | 한 문장에 하나의 생각. 영어가 꼬이면 짧게 다시 말한다 | "In short: open on the first hot sample, close on the first cool one." |
| Think collaboratively, discuss tradeoffs | 선택지를 2개 말하고 하나를 고른 이유를 말한다. 면접관 의견을 묻는다 | "I could store all intervals or stream events; since you mentioned an MCU, I'll stream — does that match what you had in mind?" |
| Stay engaged when stuck | 30초 이상 침묵 금지. 막히면 작은 예시를 손으로 돌린다 | "I'm a bit stuck on the end condition — let me trace the example by hand." |
| Don't rush | 처음 3분은 질문과 정리에 쓴다. 이게 손해가 아니라 점수다 | "Let me take a moment to organize this." |

---

## 3. 문제 은행

> 각 문제는 **30분 타이머 + 빈 에디터**로 푼다. 시그니처와 테스트는 그대로 써도 되고, 면접처럼 내가 정해도 된다.
> 우선순위: `P0` = 반드시 · `P1` = 시간 되면 · `P2` = 읽기만

### 3.1 Tier 1 — 센서·과열 계열 (가장 유력)

| ID | 문제 | 핵심 | 우선순위 |
|---|---|---|---|
| [T1-1](technicalScreenPrep/problems/T1-1_overheat_intervals/index.html) | 과열 구간 찾기 (배열) | 한 번 순회 + 구간 열고 닫기 + 최소 지속시간 | P0 |
| [T1-2](technicalScreenPrep/problems/T1-2_streaming_hysteresis/index.html) | 스트리밍 과열 감지기 (히스테리시스 + 연속 N회) | 상태 머신, O(1) 상태 | P0 |
| [T1-3](technicalScreenPrep/problems/T1-3_moving_average/index.html) | 이동 평균 · 상승률 감지 | 링버퍼, 누적합, 정수 반올림 | P0 |
| [T1-4](technicalScreenPrep/problems/T1-4_multi_sensor/index.html) | 다중 센서 과열 (Item = 센서별 상태) | 구조체 배열, ID 검증, 순서 | P1 |
| [T1-5](technicalScreenPrep/problems/T1-5_raw_register_to_temp/index.html) | raw 레지스터 → 온도 변환 + 판정 | 2의 보수 부호 확장, 고정소수점 | P0 |
| [T1-6](technicalScreenPrep/problems/T1-6_sensor_fault_detect/index.html) | 센서 고장 감지 (stuck · dropout · spike · 범위 밖) | 이전 샘플과 비교, 여러 상태 | P1 |
| [T1-7](technicalScreenPrep/problems/T1-7_time_above_threshold/index.html) | 구간 내 과열 시간 합계 (보간 포함) | 구간 겹침, 선형 보간 정수 계산 | P1 |
| [T1-8](technicalScreenPrep/problems/T1-8_blocking_api_wrapper/index.html) | 블로킹 온도 API 감싸기 (Verkada 유형) | 전담 스레드, 최신값, 윈도우 max | P2 |

#### T1-1 · 과열 구간 찾기 (P0) — [풀어 보기 →](technicalScreenPrep/problems/T1-1_overheat_intervals/index.html)
온도 샘플 배열이 시간순으로 주어진다. 온도가 `thresh` 이상인 상태가 **`min_ms` 이상 지속된 구간**을 모두 반환하라.
```c
typedef struct { uint32_t t_ms; int16_t temp_c10; } Sample;     /* temp in 0.1 °C */
typedef struct { uint32_t start_ms, end_ms; } Interval;

/* Writes up to out_cap intervals into out; returns how many intervals were found. */
size_t find_overheat(const Sample *s, size_t n, int16_t thresh, uint32_t min_ms,
                     Interval *out, size_t out_cap);
```
- **테스트**: `thresh=800, min_ms=1000`, 샘플 `(0,750) (500,810) (1000,820) (1500,830) (2000,790) (2500,850) (3000,700)` → `{500,1500}` 1개 (구간 끝 = 마지막 과열 샘플로 정의한 경우. 두 번째 구간 2500~2500은 0ms라 제외)
- **같은 입력, 다른 정의**: 끝 = 첫 정상 샘플이면 `{500,2000}`(1500ms), `{2500,3000}`(500ms → 제외). **질문으로 정의를 먼저 정하는 게 채점 포인트**
- **엣지**: `n=0` → 0 · 끝까지 과열 `(0,900)(400,900)(800,900)(1200,900)` → `{0,1200}` · 정확히 `thresh` · `out_cap=0`일 때도 개수는 정확히 반환
- **Follow-up**: 스트리밍으로 바꾸면? (→ T1-2) · 샘플 간격이 `max_gap_ms`보다 크면 구간을 끊어야 하나? · 메모리 O(1)인가?

#### T1-2 · 스트리밍 과열 감지기 (P0) — [풀어 보기 →](technicalScreenPrep/problems/T1-2_streaming_hysteresis/index.html)
샘플이 하나씩 들어온다. **연속 `n_confirm`회 `>= high`이면 과열 시작**, 과열 중에 **연속 `n_confirm`회 `<= low`이면 회복**. 이벤트가 생긴 샘플에서만 이벤트를 반환하라.
```c
typedef enum { EV_NONE, EV_OVERHEAT, EV_RECOVERED } Event;
typedef struct { /* you design it */ } Detector;

void  det_init(Detector *d, int16_t high, int16_t low, uint8_t n_confirm);
Event det_update(Detector *d, int16_t temp_c10);
```
- **테스트**: `high=850, low=800, n=3`, 입력 `840 860 870 880 860 820 790 780 770` → index 3에서 `EV_OVERHEAT`, index 8에서 `EV_RECOVERED`, 나머지 `EV_NONE`
- **스파이크**: `900 700 900 700` → 이벤트 없음 (연속 카운트가 리셋되는지 확인)
- **엣지**: `low >= high` 입력 검증 · 센서 에러 값(`INT16_MIN`)은 무시하되 카운트를 리셋할지 질문 · 과열 중 다시 `>= high` → 이벤트 중복 금지
- **Follow-up**: 횟수 대신 **시간 기반**(3초 지속) — 타임스탬프 랩 안전 비교 · 경고/위험/셧다운 3단계 임계값 · 콜백 방식

#### T1-3 · 이동 평균 · 상승률 감지 (P0) — [풀어 보기 →](technicalScreenPrep/problems/T1-3_moving_average/index.html)
최근 `WIN`개 샘플의 이동 평균을 O(1)로 유지하라. 그다음 **평균 ≥ 임계값** 또는 **상승률(최신 − 가장 오래된 값) ≥ R**이면 과열로 판정하라.
```c
#define WIN 8
typedef struct { int16_t buf[WIN]; uint8_t idx, count; int32_t sum; } MovAvg;

void    ma_init(MovAvg *m);
int16_t ma_push(MovAvg *m, int16_t x);   /* returns the current average */
```
- **테스트 (WIN=4로 바꿔서)**: `10→10, 20→15, 30→20, 40→25, 50→35` (윈도우가 20,30,40,50이 됨)
- **엣지**: 윈도우가 덜 찼을 때 `count`로 나누기 · 음수 반올림(C 나눗셈은 0 방향 절삭: `-15/2 = -7`) · `int16_t` 극값을 8개 합하면 `int16_t`로는 넘침
- **Follow-up**: `WIN`이 2의 거듭제곱이면 나눗셈 대신 시프트(음수는 주의) · EMA(`avg += (x - avg) >> k`)와의 비교: 메모리 1워드, 지연 특성

#### T1-4 · 다중 센서 과열 (P1) — [풀어 보기 →](technicalScreenPrep/problems/T1-4_multi_sensor/index.html)
여러 센서의 읽기 값이 섞여서 들어온다. 센서마다 T1-2 상태를 따로 유지하고, **과열이 시작된 (sensor_id, t_ms)를 시간순으로** 반환하라.
```c
#define MAX_SENSORS 16
typedef struct { uint8_t id; uint32_t t_ms; int16_t temp_c10; } Reading;
typedef struct { uint8_t id; uint32_t t_ms; } Alert;

size_t process(const Reading *r, size_t n, Alert *out, size_t out_cap);
```
- **엣지**: `id >= MAX_SENSORS` → 무시하고 카운트 · 센서별 타임스탬프 역행 → 버림 · 한 번도 안 나온 센서
- **Follow-up**: ID가 32비트 임의값이면? (작은 오픈 어드레싱 해시) · "지금 과열 중인 센서 수" O(1)로 · 가장 먼저 과열된 센서

#### T1-5 · raw 레지스터 → 온도 (P0) — [풀어 보기 →](technicalScreenPrep/problems/T1-5_raw_register_to_temp/index.html)
TMP102형 센서: 2바이트 big-endian, **12비트 2의 보수가 왼쪽 정렬**, 1 LSB = 0.0625°C. 0.01°C 단위로 반환하고(반올림), 과열 여부도 판정하라.
```c
int16_t raw_to_c100(uint8_t msb, uint8_t lsb);
```
- **테스트**: `0x19,0x00 → 2500` · `0x7F,0xF0 → 12794` (127.9375°C) · `0xFF,0xF0 → -6` (−0.0625°C) · `0xE7,0x00 → -2500`
- **핵심**: `(int16_t)((msb << 8) | lsb) / 16`로 부호를 살린다. 음수 `>>`는 C에서 구현 정의 → 하위 4비트가 0이라 `/16`이 정확하다고 설명
- **엣지**: 음수 반올림(−6.25 → −6) · `int` 승격 · 13비트 확장 모드(EM 비트)
- **Follow-up**: I2C로 이 2바이트를 읽는 순서(포인터 레지스터 write → repeated start → read 2) · 센서가 NACK하면?

#### T1-6 · 센서 고장 감지 (P1) — [풀어 보기 →](technicalScreenPrep/problems/T1-6_sensor_fault_detect/index.html)
샘플 스트림에서 고장을 분류하라: **STUCK**(같은 값 N회), **DROPOUT**(간격 > 2×주기), **SPIKE**(|Δ| > 샘플당 최대 변화), **OUT_OF_RANGE**(< −40°C 또는 > 150°C). 고장이 있는 샘플은 과열 판정에서 제외한다.
- **엣지**: 첫 샘플은 Δ가 없음 · 고장 직후 정상 복귀 판정 · 여러 고장이 동시에
- **말할 것**: 고장 시 fail-safe(과열로 간주할지, 마지막 정상값을 쓸지)는 시스템 정책 질문

#### T1-7 · 구간 내 과열 시간 합계 (P1) — [풀어 보기 →](technicalScreenPrep/problems/T1-7_time_above_threshold/index.html)
`[t0, t1]` 안에서 온도가 `thresh` 이상이었던 총 시간을 구하라. 두 가지 가정을 모두 설명할 수 있어야 한다.
- **테스트**: 샘플 `(0,70) (10,90) (20,95) (30,60)`, `thresh=80`
  - step-hold(다음 샘플까지 값 유지): 10~30 → **20ms**
  - 선형 보간: 70→90은 t=5에서 교차, 95→60은 t≈24.29에서 교차 → **≈19.29ms**
- **엣지**: 윈도우가 샘플 범위와 일부만 겹침 · 정수로 보간(곱셈 먼저, 나눗셈 나중, 오버플로 주의)

#### T1-8 · 블로킹 온도 API 감싸기 (P2 — Verkada 유형) — [풀어 보기 →](technicalScreenPrep/problems/T1-8_blocking_api_wrapper/index.html)
`read_temp()`는 최대 1초 블로킹하고 thread-safe가 아니다. `get_latest_temp()`(절대 블로킹 금지), `is_overheating()`(최근 5초 max > 임계값)을 구현하라.
- 30분 1문제로는 크지만, 나오면 **전담 샘플러 스레드 1개 + mutex로 보호한 최신값 + 링버퍼 히스토리** 뼈대로 간다
- 기존 자료: [Verkada 원본 문제 풀이](../verkada_sr_embedded_linux_engineer_connectivity/verkada_sep_25_2026_1st_screening_questions/solutions_with_opus.html) · [같은 유형 10문제](../../dons_study_note_from_experience/ConcurrencyPrepNoteBasedOnVerkadaSrEmbdLinuxEng/index.html) 중 `04_temp_single_flight`

### 3.2 Tier 2 — LeetCode-ish + 임베디드 포장

| ID | 문제 | LeetCode 대응 | 포인트 | 우선순위 | 재사용 |
|---|---|---|---|---|---|
| [T2-1](technicalScreenPrep/problems/T2-1_ring_buffer/index.html) | 고정 크기 링버퍼 (SPSC) | — | full/empty 구분, 2의 거듭제곱 마스크 | P0 | Neros 스크린 기출 |
| [T2-2](technicalScreenPrep/problems/T2-2_packet_parser/index.html) | UART 패킷 파서 (헤더·길이·페이로드·체크섬) | — | 바이트 단위 상태 머신, 재동기화 | P0 | Neros `extract_packet` |
| [T2-3](technicalScreenPrep/problems/T2-3_merge_intervals/index.html) | 과열 구간 병합 (여러 센서) | 56 Merge Intervals | 정렬 후 병합, 경계가 맞닿는 경우 | P1 | |
| [T2-4](technicalScreenPrep/problems/T2-4_window_max/index.html) | 최근 W초 최고 온도 | 239 Sliding Window Maximum | monotonic deque, 고정 배열로 구현 | P1 | Verkada `02_modem_rssi_window` |
| [T2-5](technicalScreenPrep/problems/T2-5_lower_bound/index.html) | 시각 t 이후 첫 샘플 찾기 | 35 / 34 lower_bound | 이진 탐색 off-by-one | P1 | |
| [T2-6](technicalScreenPrep/problems/T2-6_bit_ops/index.html) | 비트 조작 세트 | 191 · 190 · 231 | 필드 추출/삽입, popcount, reverse, 2의 거듭제곱 | P0 | Anduril 문제은행 |
| [T2-7](technicalScreenPrep/problems/T2-7_debounce_edge/index.html) | 버튼·GPIO 디바운스와 엣지 감지 | — | 연속 N 샘플 안정, rising/falling | P1 | |
| [T2-8](technicalScreenPrep/problems/T2-8_running_median/index.html) | 실시간 중앙값 (노이즈 필터) | 295 | 두 힙, 또는 작은 윈도우는 삽입 정렬 | P2 | |
| [T2-9](technicalScreenPrep/problems/T2-9_memory_pool/index.html) | 고정 블록 메모리 풀 | — | free list, O(1) alloc/free | P2 | Anduril 문제은행 |
| [T2-10](technicalScreenPrep/problems/T2-10_top_k_sensors/index.html) | 가장 뜨거운 K개 센서 | 215 / 347 | 크기 K min-heap | P2 | |

---

## 4. Debug 드릴 — "이 코드의 버그는?"

> 실행해 보면서 고치려면 → [Debug 드릴 페이지](technicalScreenPrep/problems/D_debug_drills/index.html) (`./run.sh D buggy`)
> 각 코드를 **1분 안에** 버그를 말하고, 고친 한 줄을 쓴다. 답은 펼쳐서 확인.

**D1 · 평균**
```c
int16_t avg(const int16_t *x, uint8_t n) {
    int16_t sum = 0;
    for (uint8_t i = 0; i < n; i++) sum += x[i];
    return sum / n;
}
```
<details>
<summary>답</summary>

`int16_t` 합계가 넘친다(예: 85.0°C를 0.01°C 단위로 8500, 8개 더하면 68000 > 32767). `n == 0`이면 0으로 나누기. → `int32_t sum`, `if (n == 0) return …`

</details>

**D2 · 타임아웃**
```c
bool timed_out(uint32_t now, uint32_t start, uint32_t timeout) {
    return now > start + timeout;
}
```
<details>
<summary>답</summary>

`start + timeout`이 랩어라운드하면 즉시 참이 되거나 영원히 거짓. → `return (uint32_t)(now - start) >= timeout;`

</details>

**D3 · 링버퍼 push**
```c
#define N 8
static int buf[N];
static unsigned head, tail;
bool push(int v) {
    if ((head + 1) % N == tail) return false;
    buf[head++] = v;
    return true;
}
```
<details>
<summary>답</summary>

`head++`에 모듈로가 없어서 8 이상 인덱스로 쓴다. → `buf[head] = v; head = (head + 1) % N;` (이 방식은 용량이 N−1이라는 것도 말할 것)

</details>

**D4 · 역순 루프**
```c
for (size_t i = n - 1; i >= 0; i--) process(x[i]);
```
<details>
<summary>답</summary>

`size_t`는 항상 ≥ 0이라 무한 루프, `n == 0`이면 첫 인덱스부터 언더플로. → `for (size_t i = n; i-- > 0; )`

</details>

**D5 · 센서 raw 값**
```c
int16_t raw = (msb << 4) | (lsb >> 4);     /* 12-bit two's complement */
float c = raw * 0.0625f;
```
<details>
<summary>답</summary>

부호 확장이 없어서 영하 온도가 큰 양수(최대 255°C 근처)로 읽힌다. → `int16_t raw = (int16_t)((msb << 8) | lsb) / 16;`

</details>

**D6 · ISR 카운터**
```c
static uint32_t overheat_count;                 /* incremented in ISR */
void TEMP_IRQHandler(void) { overheat_count++; }
void task(void) {
    if (overheat_count > 0) { report(overheat_count); overheat_count = 0; }
}
```
<details>
<summary>답</summary>

`volatile`이 없어서 컴파일러가 값을 캐시할 수 있다. 읽기와 0으로 지우기 사이에 ISR이 들어오면 증가분을 잃는다. → `volatile` + 인터럽트 잠깐 끄고 읽고 지우기, 또는 `atomic_exchange(&cnt, 0)`

</details>

**D7 · 구조체 초기화**
```c
void clear(Sample *s) { memset(s, 0, sizeof(s)); }
```
<details>
<summary>답</summary>

`sizeof(s)`는 포인터 크기(8)다. → `sizeof(*s)`

</details>

**D8 · 디바운스 카운터**
```c
if (temp >= HIGH) {
    if (++hot_cnt >= 3) state = OVERHEAT;
}
```
<details>
<summary>답</summary>

온도가 내려가도 `hot_cnt`를 리셋하지 않아서, 떨어져 있는 스파이크 3개로도 과열이 된다. → `else hot_cnt = 0;`

</details>

---

## 5. 치트시트 — 손에 붙어야 하는 관용구

```c
/* wrap-safe time compare */
static inline bool elapsed(uint32_t now, uint32_t since, uint32_t dur) {
    return (uint32_t)(now - since) >= dur;
}

/* caller-provided output buffer: count everything, write what fits */
size_t cnt = 0;
/* ... when an item is found: */
if (cnt < out_cap) out[cnt] = item;
cnt++;
/* return cnt; caller knows it was truncated if cnt > out_cap */

/* rounding integer division (round half away from zero) */
static inline int32_t div_round(int32_t a, int32_t b) {
    return (a >= 0) ? (a + b / 2) / b : (a - b / 2) / b;   /* b > 0 */
}

/* ring buffer with power-of-two size */
#define RB_SIZE 16u                      /* must be a power of two */
#define RB_MASK (RB_SIZE - 1u)
/* count = head - tail (unsigned, wraps correctly); full when count == RB_SIZE */
```

- 상태 머신은 `enum` + `switch`. 상태마다 "들어올 때 무엇을 리셋하는지"를 말한다
- 루프 불변식 한 줄을 말하고 코드를 쓴다: "At the top of each iteration, `in_hot` tells whether an interval is open."
- 테스트는 **정상 1개 → 엣지 2~3개** 순서. 실패하면 printf로 상태를 찍는 과정을 보여 주는 것도 "debug" 평가에 도움이 된다

---

## 6. 플랜 — D-5 → D-0

### 6.0 날짜 고르기 (10-14 오전까지 제출)
- **Neros 온사이트가 10-22 또는 10-23**이다. 그 주(10-21~23)는 Neros 준비에 쓰고 Scout 스크린은 피한다
- **추천 슬롯**: **10-19(월) ~ 10-20(화)** → D-5 플랜을 거의 그대로 쓸 수 있다(10-14~10-18 준비). 더 빨리 원하면 **10-16(금)** 오후 → §6 압축 규칙(3일)
- 가능하면 오전 10~11시처럼 머리가 맑은 시간, 앞뒤 30분은 비워 둔다
- Ashby에 여러 슬롯을 넣어 두면 "moving quickly" 상황에서 확정이 빨라진다 [추정]
- 두 회사 준비가 겹친다: C 링버퍼(T2-1)·상태 머신·경험 이야기는 Neros 온사이트에도 그대로 쓰인다

### 6.1 D-5 → D-0 표

> 날짜가 정해지면 아래 표를 역산해서 쓴다. **매 문제 = 30분 타이머 + 빈 에디터(자동완성 없이) + 10분 회고(소리 내어 설명)**.
> 연습 환경: CoderPad 무료 sandbox 또는 `gcc -Wall -Wextra -fsanitize=address,undefined`

| 날 | 목표 | 문제 | 산출물 |
|---|---|---|---|
| D-5 | 형식 적응 + 핵심 1번 | §2.4 템플릿 외우기 · **T1-1** · **T1-2** · Debug D1–D4 | 두 문제 모두 30분 안에 ALL PASS |
| D-4 | 정수·비트 감각 | **T1-3** · **T1-5** · T2-6(비트 세트) · Debug D5–D8 | 반올림·부호 확장을 말로 설명 |
| D-3 | Item·State 확장 | T1-4 · T1-6 · **T2-1** 링버퍼 · **T2-2** 패킷 파서 | Neros 기출 2개를 백지에서 재작성 |
| D-2 | LeetCode 대응 + 모의 1회 | T1-7 · T2-3 · T2-4 · T2-5 → 처음 보는 변형 1개로 **45분 모의**(15분 경험 + 30분 코딩, 녹음) | 녹음 들으면서 침묵 구간·말버릇 체크 |
| D-1 | 모의 2회 + 정리 | T1-1·T1-2 **시간 기반 변형**으로 모의 2회 · T1-8은 뼈대만 읽기 · §5 치트시트 | 질문 6개(§2.3) 입에 붙이기 |
| D-0 | 가볍게 | T1-2를 백지에서 20분 · §7 체크리스트 | — |

**압축 규칙**: 3일 남으면 D-5+D-4(T1-1·T1-2·T1-3·T1-5·Debug 전부) → D-3+D-2(T2-1·T2-2·T1-4 + 모의 1회) → D-1. 2일 이하면 **T1-1·T1-2·T1-5·T2-1·Debug**만.

---

## 7. 15분 경험 파트 — "building and scaling hardware systems"

> 메일에 평가 범위로 명시되어 있다 [확인됨]. 이력 나열이 아니라 **"하드웨어 시스템을 만들었고, 그것을 더 많은 유닛·플랫폼·사람이 쓰도록 키웠다"**는 이야기 2~3개를 준비한다. 숫자(유닛 수, 기간, 결함률, 시간 절감)는 **본인이 확실히 아는 값만** 넣는다 — 아래 `[ ]`는 Don이 채울 칸

**예상 질문 (영어)**
- Tell me about a hardware system you built from scratch. What did bring-up look like?
- How did you take something that worked on one board to hundreds or thousands of units?
- What broke when you scaled? How did you find it?
- How did you make testing or validation scale — tools, automation, telemetry?
- What would you do in your first 90 days as the first embedded engineer in Sunnyvale?

**스토리 A — 양산 규모로 키우기 (Apple: factory test-node)**
- **S/T**: 신규 무선 칩이 시스템에 통합될 때 생기는 latent 결함이 양산(MP) 전에 잡히지 않으면 필드 불량이 된다
- **A**: factory test-node 아키텍처와 stress 시나리오를 설계해, bring-up에서 본 실패 패턴을 공장 테스트 항목으로 바꿨다. PCIe/I2C/SPMI 실패를 스코프·프로토콜 분석기로 루트코즈하고, 그 결과를 테스트에 다시 반영
- **R**: `[MP 전에 잡은 결함 사례 / 커버한 유닛 수 / 라인 적용 범위]`
- **Scout 연결**: "Scout도 같은 센서 키트를 여러 차량에 붙인다 — 한 대에서 된 것을 N대에서 똑같이 동작시키는 검증 체계가 필요하다"

**스토리 B — 새 하드웨어를 처음부터 bring-up (SK hynix: FPGA → 제품)**
- **S/T**: RTL freeze 전, FPGA에서 Cortex-R/M 코어와 I2C·SPI·DMA·SRAM/DRAM을 처음 살려야 했다
- **A**: 부팅 순서를 단계별로 쪼개고(클럭·리셋 → 메모리 → 버스 → 주변장치), JTAG/Trace32와 logic analyzer로 각 단계를 검증. 발견한 HW 이슈를 RTL 팀에 피드백
- **R**: `[freeze 전에 잡은 RTL 이슈 수 / 단축한 bring-up 기간]` → 이후 Google/Meta용 양산 펌웨어로 이어짐
- **Scout 연결**: "새 컴퓨트 보드와 센서가 계속 바뀌는 환경에서 bring-up을 반복 가능한 절차로 만드는 게 첫 임베디드 엔지니어의 일"

**스토리 C — 검증·디버그를 규모화 (UART 자동화 SDK + NVMe telemetry)**
- **S/T**: 디바이스가 많아지면 사람이 하나씩 붙어서 테스트·디버그할 수 없다
- **A**: 다수 디바이스를 동시에 제어하는 UART 시퀀스 테스트 자동화 SDK, 필드 이슈를 원격으로 진단하는 telemetry 디버그 기능
- **R**: `[동시 제어 디바이스 수 / 줄어든 테스트 시간 / telemetry로 해결한 고객 이슈]`
- **Scout 연결**: "필드에 나간 차량의 센서 데이터 품질을 자동으로 검증하는 툴 = JD의 'Build tools and tests to validate sensor data quality'"

**말할 때 규칙**: 각 스토리 90초 이내 → 면접관이 파고들게 둔다. "scaling"의 세 축(유닛 수·플랫폼 수·사람/팀)을 하나 이상 꼭 언급한다

**공통 기반**
- **Tell me about yourself 60초**: [컨텍스트 4.9 ①](scout_ai_embedded_context.html) 그대로
- **STAR 2개를 준비**:
  1. Apple: 신규 칩 통합 중 I2C/SPMI 인터페이스 실패를 스코프·프로토콜 분석기로 루트코즈한 사례
  2. SK hynix: RTL freeze 전 FPGA에서 I2C/SPI/DMA·메모리 bring-up, 또는 factory test로 latent 결함을 잡은 사례
- **Embedded Linux 질문**: [4.9 ④](scout_ai_embedded_context.html) — 강점(드라이버의 아래 반)부터, 정직하게. [Embedded Linux 노트](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/index.html)로 공부한 범위만 말한다
- **센서 경험 질문** 대비: "센서 드라이버를 직접 출시한 적은 없지만, 주변 칩을 I2C/SPI로 붙이고 신호 레벨에서 검증하는 일은 매일 했다"

**마지막 2분 역질문** (1~2개만)
- The embedded team is two engineers in Georgia — how would the Sunnyvale embedded role work with them day to day?
- What sensors and compute platform does this role bring up first?

---

## 8. 당일 체크리스트

- [ ] (D-? 이전) Ashby 링크로 일정 제출, 확정 메일·캘린더 확인
- [ ] CoderPad 링크·언어(C) 확인, 노트북 전원·이어폰
- [ ] §7 스토리 A·B·C의 `[ ]` 숫자를 채워서 종이에
- [ ] §2.4 템플릿과 §2.3 질문 6개를 종이에
- [ ] 시작 3분: 문제 다시 말하기 + 질문
- [ ] 코드 쓰기 전에 접근법·복잡도를 먼저 말하기
- [ ] 테스트: 정상 1 + 엣지 2~3 (빈 입력, 끝까지 과열, 정확히 threshold)
- [ ] 마지막에 임베디드 개선점 한 문장 (정수 연산·고정 메모리·ISR 분리)
- [ ] 끝나면 실제 문제를 [컨텍스트](scout_ai_embedded_context.html) 6절에 바로 기록

---

## 9. 재사용 자료

- [Neros technical screen 준비 사이트](../neros_tech_senior_firmware_engineer/technicalScreenPrep/index.html) — 링버퍼·`extract_packet` (Neros 실제 기출)
- [Verkada 1차 원본 문제 풀이](../verkada_sr_embedded_linux_engineer_connectivity/verkada_sep_25_2026_1st_screening_questions/solutions_with_opus.html) — ALS 센서 래핑
- [Verkada 유형 동시성 10문제](../../dons_study_note_from_experience/ConcurrencyPrepNoteBasedOnVerkadaSrEmbdLinuxEng/index.html)
- [Anduril C 문제은행 111문제](../anduril_firmware_engineer/index.html) — 비트 조작·링버퍼·메모리 풀
- [Embedded Linux 노트](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/index.html) — 15분 경험 파트 대비
