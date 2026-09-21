# Verkada 문제 은행 — 세트 작성 스펙 (작업자용)

이 문서는 `verkada_prep/` 문제 은행의 **한 세트**를 만드는 규칙이다. 앤두릴 키트
(`../../anduril_firmware_engineer/`)와 **완전히 동일한 포맷**을 쓰되, 내용은 Verkada
Connectivity 팀(Senior Embedded Linux Engineer) 인터뷰에 맞춘다.

## 0. 이 인터뷰의 맥락 (문제 선정 기준)

2026-09-15 리크루터 안내 메일이 복습 주제를 명시했다:
① thread safety ② synchronization (mutex, condition variable, **atomic operations**)
③ real-time data handling patterns (such as **double buffering**) ④ design principles for
**modular, testable embedded software**. 세션은 2파트(problem solving + system design)다.

평가 기준: **설계 결정과 trade-off의 정당화 · 깨끗하고 유지보수 가능한 코드 ·
edge case와 concurrent 시스템의 견고성 · 커뮤니케이션의 명료함.**

제품 맥락(문제 시나리오는 여기서 가져올 것):
- **GC31-E 셀룰러 게이트웨이**: LTE Cat 12, 듀얼 nano SIM 자동 failover, GigE 3포트(WAN/LAN),
  802.3bt PoE++ 출력 2포트(총 60W 예산), Perpetual PoE(게이트웨이 재부팅 중에도 급전 유지),
  -40~50°C, IP66, -25°C 이하에서 소비전력 9W→19W(히터)
- **GW31-E Wi-Fi 게이트웨이**: 최대 1,000ft 거리 AP 연결, Command에서 원격 power-cycle,
  **오프라인 30분이면 자가 재부팅**, 오프라인 상태에서도 **BLE로 Wi-Fi 설정**
- **MT81 보안 트레일러**: 태양광 + 6/13kWh 배터리, 카메라 최대 6대
- **공기질 센서(SV)**: MCU + FreeRTOS 급, I2C/UART 센서
- 전체 fleet: 170개국 200만 대 이상, 클라우드 = Command

## 1. 산출물 (세트 하나당 파일 4개)

```
solutions/NN_slug.c   정답 — 컴파일 경고 0, 전 테스트 PASS
problems/NN_slug.c    연습 stub — 컴파일/실행 되지만 대부분 FAIL
data/NN_slug.json     대시보드용 구조화 데이터
notes/NN_slug.md      한국어 스터디 노트
```

`NN_slug`는 배정받은 세트 이름 그대로 쓴다 (예: `02_condvar_queues`).

## 2. C 파일 형식 (엄수)

### 2.1 solutions/NN_slug.c

```c
// NN_slug.c  —  REFERENCE SOLUTION
// <한국어 세트 제목> (<English title>)  —  Q<시작>~Q<끝>
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra -pthread NN_slug.c -o /tmp/vk_slug && /tmp/vk_slug
// <이 세트가 Verkada 인터뷰에서 왜 중요한지 2~4줄>
// ---------------------------------------------------------------------------
#include <stdio.h>
/* ... 필요한 헤더 ... */

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// Q<n>. <문제 제목>
// ---------------------------------------------------------------------------
// <2~5줄 설명: 무엇을 왜 구현하는가 + Verkada 맥락>
// ===========================================================================
<구현>

/* ... 문제 반복 ... */

int main(void) {
    printf("== Q1~Q2 <소제목> ==\n");
    T("cb_push stores byte", ...);
    /* ... */
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
```

규칙:
- 문제 번호는 배정받은 **전역 범위**를 쓴다(세트마다 이어짐). 주석에 `Q7.`처럼 표기.
- 문제 1개당 **테스트 2개 이상**(`T(...)`). 세트 전체 40~70개 체크가 목표.
- 한국어 주석 기본, 기술 용어는 영어 그대로.
- `main()`은 **10초 안에** 끝나야 한다. 스레드 테스트는 반복 횟수를 적당히(수만 회 이내).
- **결정적(deterministic)이어야 한다.** 타이밍에 의존하는 단정은 넉넉한 경계로
  (예: `ticks >= 15 && ticks <= 25`). 절대 플래키하면 안 된다.

### 2.2 problems/NN_slug.c

`solutions/`와 **완전히 같은 파일**에서 구현부만 비운 것:
- 헤더 주석 첫 줄을 `// NN_slug.c  —  PRACTICE STUB (직접 채워넣기)`로, 빌드 안내를
  `make prob N=NN_slug`로 바꾼다.
- 각 문제 앞에 아래 형식의 문제 설명 블록을 넣는다(**stub에만** 있는 블록):
```c
/* ---------------------------------------------------------------------------
 * Q7.  <제목>
 *   KO: <무엇을 구현하는지 2~4줄>
 *   EN: <same in English, 1~3 lines>
 *   ex: <입력 -> 기대 출력 한 줄>
 * ------------------------------------------------------------------------- */
```
- 함수 본문은 `(void)param; // TODO: implement` + 더미 리턴으로 바꾼다.
  **stub도 반드시 컴파일·실행된다**(미구현이라 대부분 FAIL이 뜰 뿐).
- 테스트 하네스와 `main()`은 정답과 동일하게 유지(“건드리지 말 것” 주석 추가).
- 스레드/블로킹 테스트가 미구현 상태에서 **영원히 멈추면 안 된다** — 스핀 상한이나
  타임아웃을 넣어 FAIL로 빠져나오게 설계할 것.

### 2.3 컴파일·실행 검증 (필수, 스스로 수행)

```sh
cc -std=c11 -Wall -Wextra -O1 -g -pthread solutions/NN_slug.c -o /tmp/vk_sol && /tmp/vk_sol   # 전부 PASS, exit 0
cc -std=c11 -Wall -Wextra -O1 -g -pthread problems/NN_slug.c  -o /tmp/vk_prob && /tmp/vk_prob # 실행은 됨(FAIL 다수)
```
스레드를 쓰는 세트는 추가로:
```sh
cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=thread -pthread solutions/NN_slug.c -o /tmp/vk_ts && /tmp/vk_ts
```
→ **ThreadSanitizer 경고 0**이어야 한다. 의도적으로 race를 보여주는 문제라면 해당
테스트만 분리하고 JSON/노트에 그 사실을 명시한다.

> ⚠️ **플랫폼: macOS (Apple clang, arm64)**. Linux 전용 API(`epoll`, `timerfd`,
> `signalfd`, `pthread_condattr_setclock`)는 **코드에 쓰지 말 것**. 이식 가능한
> `poll()`, `pipe()`, `fcntl`, `clock_gettime(CLOCK_MONOTONIC)`,
> `pthread_cond_timedwait_relative_np`(macOS) / `pthread_cond_timedwait`(그 외)를 쓰고,
> Linux 전용 API는 **노트와 JSON에서 이론으로 다룬다**(면접에서 말해야 하므로 중요).

## 3. data/NN_slug.json 스키마 (키 이름 정확히)

```json
{
  "id": "condvar_queues",
  "order": 2,
  "range": "Q11-20",
  "icon": "🔐",
  "track": "core",
  "title_en": "Condition Variables & Blocking Queues",
  "title_ko": "조건 변수 & 블로킹 큐",
  "summary_ko": "<3~5줄. 이 세트가 무엇이고 인터뷰에서 어디가 핵심인지>",
  "summary_en": "<2~4 lines>",
  "verkada_relevance_ko": "<왜 Verkada 면접에 나오는지 4~6줄. 리크루터 메일 문구와 제품 맥락을 근거로>",
  "concepts": [ { "term": "condition variable", "ko": "...", "en": "..." } ],
  "study_note_md": "<notes/NN_slug.md 본문 전체를 그대로 넣는다>",
  "problems": [
    {
      "num": 11,
      "slug": "bq_push_blocking",
      "title_ko": "블로킹 push",
      "title_en": "Blocking push",
      "difficulty": "easy|medium|hard",
      "tags": ["condvar", "queue", "blocking"],
      "prompt_ko": "<문제 지문 2~4줄>",
      "prompt_en": "<1~3 lines>",
      "signature": "bool bq_push(BQueue *q, Event ev)",
      "examples": [ { "in": "cap=2, push a,b,c", "out": "c는 pop 전까지 블록" } ],
      "hints": ["<가벼운 힌트>", "<중간 힌트>", "<거의 정답>"],
      "solution": "<solutions/의 해당 함수 코드 그대로 (문자열, \\n 이스케이프)>",
      "explanation_ko": "<왜 이렇게 푸는지 3~6줄>",
      "complexity": "O(1) amortized, O(cap) space",
      "pitfalls": ["<흔한 실수 1>", "<2>"],
      "verkada_context": "<이 문제가 어떤 Verkada 상황에 대응하는지 1~2줄>"
    }
  ]
}
```
- `concepts`는 6~10개, `problems`는 배정받은 개수 정확히.
- `hints`는 정확히 3개(점증적).
- `difficulty` 분포: easy 30%, medium 50%, hard 20% 정도.
- JSON은 `python3 -c "import json;json.load(open('data/NN_slug.json'))"`로 파싱 검증할 것.

## 4. notes/NN_slug.md 형식

```markdown
# <아이콘> <한국어 제목> (<English>) — Q<범위>

> <2~4줄 요약: 이 주제가 이 인터뷰에서 왜 중요한지>

---

## 1. 핵심 아이디어
## 2. <주제별 본문 — 표·코드·다이어그램 적극 사용>
## 3. 흔한 함정 (인터뷰 감점 포인트)
## 4. 면접에서 말할 것 (한국어 + 영어 문장)
## 5. 체크리스트
- [ ] ...
```
- 길이 120~250줄. 코드 블록은 ```c 로.
- **4장은 필수**: 면접관에게 소리 내어 말할 문장을 영어로도 제공.

## 5. 품질 기준

1. **정확성 최우선.** 컴파일 안 되거나 틀린 코드는 없느니만 못하다. 반드시 실행 검증.
2. 각 문제는 **독립적으로 작고 드릴 가능해야** 한다. 하나의 거대한 함수 금지.
3. 시나리오는 Verkada 제품에서 가져오되 **억지로 끼워맞추지 말 것**.
4. 이미 만들어진 참고 자료: `../concurrency_practice/solutions/*.c` (검증된 10문제)와
   `../verkada_concurrency_top10.md`. 겹치는 주제는 **더 잘게 쪼개서** 가져다 쓰되
   설명·테스트는 이 스펙 형식으로 다시 쓴다.
5. 한국어 기본, 기술 용어 영어 유지. 존댓말 말고 간결한 설명체.
