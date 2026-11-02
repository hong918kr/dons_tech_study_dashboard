# E3. 지연과 지터 분해 — "IRQ에서 태스크까지 몇 µs"를 구간으로 쪼개 재고, 히스토그램 봉우리마다 범인을 찾는다

> **이 노트를 다 읽으면**: 인터럽트 지연과 태스크 응답 지연을 하드웨어 진입 · 마스킹 구간 · 상위 ISR · 핸들러 · 커널 경로 · 전환 · 상위 태스크 간섭으로 나누고 각 구간을 따로 잴 수 있다 · GPIO 토글 + scope, DWT CYCCNT, 타이머 스냅샷, 트레이스 중 무엇이 어느 구간을 재는지 고르고, 측정 자체의 비용(observer effect)을 숫자로 뺄 수 있다 · 지연 히스토그램의 봉우리 · 선반 · 꼬리를 보고 원인을 지목한 뒤 원인별 태깅과 하나씩 끄기(ablation)로 증명할 수 있다 · 가장 긴 크리티컬 섹션을 스택에 쌓인 LR로 찾아내고, max-plus 합성으로 최악값 상한을 세우고, 측정을 얼마나 오래 해야 하는지 계산할 수 있다
> **왜 필요한가**: 면접의 "인터럽트 지연이 얼마인가? 어떻게 쟀나? 가끔 튀면 어떻게 찾나?"와 현장의 "드론 rate loop가 가끔 30 µs 늦게 시작한다"는 같은 질문이다. "빠르다"가 아니라 **"기본 8.3 µs, 그중 커널 경로 3.4 µs, 최악 73.9 µs는 네 원인이 겹친 0.35%의 경우"**처럼 수치와 구성으로 답해야 통과한다
> **Don 기준 난이도**: scope · 로직 분석기로 버스 타이밍을 재는 일, 히스토그램으로 꼬리를 보는 습관, SSD 펌웨어의 레이턴시 튜닝은 이미 안다 / 새로 배울 부분은 RTOS 경로(ISR → ready → PendSV → 전환 → API 복귀)를 구간별로 재는 계측점, Cortex-M DWT를 켜는 순서와 함정, 봉우리-원인 매핑과 원인 태깅, 최악값 합성과 측정 시간 계산
> **선행 노트**: A1(지연 · 지터 용어, 평균 vs 꼬리), A7(IRQ → 태스크 8.28 µs 첫 측정), B2(예외 진입 12사이클 · tail-chain 6사이클), B3(NVIC · BASEPRI), C2(컨텍스트 스위치 명령 수), C7(크리티컬 섹션 길이 L 스윕 — 여기서 다시 재지 않는다), C8(커널 진입 비용), EL N3(Linux cyclictest — Linux 쪽은 참조만). 이어지는 노트: E2(우선순위 배치), E4(ISR → 태스크 설계), E7(주기 실행 지터), M3(이벤트 트레이싱), M6(DWT · ETM 하드웨어), N3(응답 시간 분석 이론)
> 🔁 **Bare-Metal 겹침**: BM J7(인터럽트 지연 · 지터 측정: GPIO 토글 + scope, DWT, 최악 경로 찾기) · BM M7(타이밍 측정 기법: GPIO 토글, DWT CYCCNT, 타이머 스냅샷, 측정이 측정을 바꾸는 문제) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "RTOS 없는 super-loop에서의 측정(ISR → 메인 루프 플래그 폴링 지연, 루프 한 바퀴 길이가 곧 최악 지연)"만 더한다

---

## 0. 큰 그림 — "지연이 얼마냐"는 질문을 구간으로 바꾼다

Don이 SSD 펌웨어에서 QoS 레이턴시를 잡던 장면을 떠올려 보자. "4K 랜덤 읽기 99.99% 지연이 가끔 2 ms를 넘는다"는 말은 그 자체로는 고칠 수 없다. 명령 수신 → FTL 조회 → NAND 읽기 → ECC → DMA → 완료 통지로 **구간을 나누고**, 각 구간에 타임스탬프를 찍고, 2 ms짜리 표본만 모아서 **어느 구간이 늘었는지** 봐야 GC나 read retry 같은 범인이 나온다.

RTOS의 인터럽트 지연도 똑같다. "IRQ에서 태스크까지 8 µs"는 결과이고, 그 안에는 성격이 전혀 다른 일곱 조각이 들어 있다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="18" font-size="13" font-weight="bold">IRQ 한 번이 태스크의 첫 줄이 되기까지 — 시간은 왼쪽에서 오른쪽</text>
<line x1="200" y1="34" x2="200" y2="270" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="200" y="46" font-size="12" text-anchor="middle">IRQ 발생</text>
<line x1="335" y1="52" x2="335" y2="270" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="335" y="46" font-size="12" text-anchor="middle">ISR 첫 줄</text>
<line x1="610" y1="52" x2="610" y2="270" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="610" y="46" font-size="12" text-anchor="middle">태스크 첫 줄</text>
<text x="190" y="72" font-size="12" text-anchor="end">① 마스킹 · 상위 ISR 대기</text>
<rect x="200" y="58" width="80" height="20" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="190" y="98" font-size="12" text-anchor="end">② HW 진입 (스태킹 · 벡터)</text>
<rect x="280" y="84" width="30" height="20" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="190" y="124" font-size="12" text-anchor="end">③ 프롤로그 · 디스패치</text>
<rect x="310" y="110" width="25" height="20" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="190" y="150" font-size="12" text-anchor="end">④ ISR 본문 + 커널 호출</text>
<rect x="335" y="136" width="85" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="190" y="176" font-size="12" text-anchor="end">⑤ ISR 퇴장 · PendSV 요청</text>
<rect x="420" y="162" width="20" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="190" y="202" font-size="12" text-anchor="end">⑥ PendSV · 전환 결정</text>
<rect x="440" y="188" width="60" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="190" y="228" font-size="12" text-anchor="end">⑦ 상위 태스크 간섭</text>
<rect x="500" y="214" width="75" height="20" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="190" y="254" font-size="12" text-anchor="end">⑧ 복원 · API 복귀</text>
<rect x="575" y="240" width="35" height="20" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<line x1="200" y1="284" x2="335" y2="284" stroke="currentColor" marker-start="url(#ah1)" marker-end="url(#ah1)"/>
<text x="268" y="300" font-size="12" text-anchor="middle">인터럽트 지연 ①②③</text>
<line x1="200" y1="310" x2="610" y2="310" stroke="currentColor" marker-start="url(#ah1)" marker-end="url(#ah1)"/>
<text x="470" y="326" font-size="12" text-anchor="middle">태스크 응답 지연 ①~⑧</text>
</svg>
```

그림 1 — 지연의 여덟 조각. 빨강(①, ⑦)은 **다른 코드 때문에 기다리는** 시간이라 매번 달라진다(지터의 주범). 회색 · 파랑 · 주황 · 초록은 **경로 자체의 길이**라 거의 일정하다(기본 지연). 이 노트의 모든 측정은 이 그림의 어느 구간을 재는지로 설명된다.

이 노트가 하는 일은 세 가지다.

1. **측정 도구를 고르고 그 비용을 안다** (§1~§3) — 타이머 스냅샷, GPIO 토글 + scope, DWT CYCCNT, 트레이스. 각각 그림 1의 어디부터 어디까지를 재는지, 측정 자체가 몇 ns를 더하는지.
2. **기본 지연을 구간으로 쪼갠다** (§4) — FreeRTOS와 Zephyr에서 같은 IRQ → 태스크 경로를 5~7개 구간으로 재서 누적 막대로 비교한다.
3. **지터를 원인으로 쪼갠다** (§5~§7) — 배경 부하 아래 히스토그램을 만들고, 봉우리 · 선반 · 꼬리를 원인에 매핑하고, 표본마다 원인을 태깅하고, 원인을 하나씩 꺼서 증명한다. 마지막으로 가장 긴 경로를 찾고(스택의 LR), 최악값 상한을 합성하고, 측정 시간을 계산한다.

### 범위 — 다른 노트와의 경계

- **크리티컬 섹션 길이 L 스윕**(PRIMASK · BASEPRI · 스케줄러 잠금 × L = 0 ~ 100 µs)은 **C7 §6**이 이미 쟀다. 결론만 쓴다: 최대 IRQ 지연 ≈ 기본 + L(BASEPRI면 kernel-aware IRQ만), 태스크 지연은 세 잠금 모두 ≈ L + 10 µs. 여기서는 그 L이 **히스토그램에서 어떤 모양으로 보이는지**만 다룬다.
- **컨텍스트 스위치 명령 수**(lean 60 명령, 스택 검사 · 런타임 통계 켜면 104)는 C2, **NVIC 우선순위 · BASEPRI 하드웨어**는 B3, **우선순위 배치 설계**는 E2, **주기 태스크의 주기 지터**는 E7, **RTA 이론**은 N3의 몫이다.
- **Linux 쪽 지연 측정**(cyclictest, osnoise, timerlat, 히스토그램 로그 축, "오래 재야 한다")은 EL N3에서 했다. 이 노트는 **MCU에서 달라지는 점**만 쓴다: 커널/사용자 경계와 cpuidle이 없고, 측정점을 ISR 첫 명령과 PendSV 안까지 직접 넣을 수 있으며, 사이클 단위 하드웨어 시계(DWT, 타이머)를 코드에서 바로 읽는다.

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위에서 bare-metal, FreeRTOS V11.3.2(ARM_CM3 포트), Zephyr v4.4.2를 같은 가상 보드로 돌리고, Python 분석은 VM 호스트에서 돌린다. 타이밍은 모두 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 결정적)이다. **숫자는 QEMU icount 기준이며 실제 칩과 절대값은 다르다** — QEMU는 예외 스태킹 사이클, 플래시 wait state, 캐시, 버스 대기, DWT를 모델링하지 않는다. 그래서 이 노트의 숫자는 **구간의 상대 크기 · 히스토그램 모양 · 원인 구성**을 보여 주는 데 쓰고, 실칩 사이클은 Arm 문서 값을 따로 표로 붙인다. 시계는 FPGAIO COUNTER(0x40028018, 25 MHz, 1 카운트 = 40 ns)와 CMSDK TIMER0의 `RELOAD - VALUE`다.

모든 bare-metal · FreeRTOS 예제가 쓰는 작은 헤더 `e3.h`다(C7의 `c7.h`를 이 노트용으로 고쳤다 — DUALTIMER의 첫 만료 시점을 따로 정할 수 있게 BGLOAD를 쓰고, 1 µs 칸 히스토그램을 더했다). Zephyr 예제는 같은 내용을 `board.h` 없이 레지스터 매크로로 다시 쓴 `z3.h`를 쓴다(내용이 같아서 싣지 않는다).

```c
/* e3.h — E3 latency/jitter measurement helpers, QEMU mps2-an385 (bare-metal + FreeRTOS) */
#ifndef E3_H
#define E3_H
#include "board.h"
#define CYC()       REG32(0x40028018u)          /* FPGAIO COUNTER: 25 MHz from reset, 1 = 40 ns */
#define LED         REG32(0x40028000u)          /* FPGAIO LED register: our stand-in "GPIO pin" */
#define NS(c)       ((unsigned)((c) * 40u))
/* TIMER0 (IRQ 8): the event source. elapsed = cycles since it raised the IRQ */
static inline void t0_start(uint32_t period, uint8_t prio)
{
    TIMER_CTRL(TIMER0_BASE) = 0;
    TIMER_RELOAD(TIMER0_BASE) = period - 1;
    TIMER_VALUE(TIMER0_BASE) = period - 1;
    TIMER_INTCLR(TIMER0_BASE) = 1;
    NVIC_IPR(8) = prio;
    NVIC_ISER0 = 1u << 8;
    TIMER_CTRL(TIMER0_BASE) = 1u | 8u;          /* enable + IRQ enable */
}
static inline void t0_stop(void) { TIMER_CTRL(TIMER0_BASE) = 0; }
static inline uint32_t t0_elapsed(void) { return TIMER_RELOAD(TIMER0_BASE) - TIMER_VALUE(TIMER0_BASE); }
/* DUALTIMER timer1 (IRQ 10): an interfering periodic ISR */
#define DT_LOAD     REG32(0x40002000u)
#define DT_VALUE    REG32(0x40002004u)
#define DT_CTRL     REG32(0x40002008u)
#define DT_INTCLR   REG32(0x4000200Cu)
static inline void dt_start(uint32_t period, uint32_t first, uint8_t prio)
{
    DT_CTRL = 0;
    DT_LOAD = first - 1;                        /* first expiry after 'first' cycles ... */
    REG32(0x40002018u) = period - 1;            /* ... then BGLOAD period (no restart) */
    NVIC_IPR(10) = prio;
    NVIC_ISER0 = 1u << 10;
    DT_CTRL = 0x80u | 0x40u | 0x20u | 0x02u;    /* enable, periodic, IRQ, 32-bit */
}
static inline void spin_cyc(uint32_t c) { uint32_t t = CYC(); while (CYC() - t < c) { } }
static uint32_t e3_seed = 12345u;
static inline uint32_t rnd(uint32_t n) { e3_seed = e3_seed * 1103515245u + 12345u; return (e3_seed >> 16) % n; }
/* fixed-bin histogram, 1 bin = BINW cycles */
#define NBIN 64
typedef struct { uint32_t n, min, max, over; uint64_t sum; uint16_t bin[NBIN]; } hist_t;
#define HN(h) (*(volatile uint32_t *)&(h).n)     /* poll the count from thread code */
static inline void h_add(hist_t *h, uint32_t v, uint32_t binw)
{
    if (!h->n || v < h->min) h->min = v;
    if (v > h->max) h->max = v;
    h->n++; h->sum += v;
    uint32_t b = v / binw;
    if (b < NBIN) h->bin[b]++; else h->over++;
}
#endif
```

`t0_elapsed()`가 이 노트 전체의 기준 시계다. TIMER0은 0을 치는 순간 IRQ를 올리고 RELOAD로 다시 장전되므로, **어디서 읽든 "IRQ가 올라간 뒤 몇 사이클"**이 나온다. ISR 첫 줄에서 읽으면 인터럽트 지연, PendSV 안에서 읽으면 전환까지의 지연, 깨어난 태스크에서 읽으면 태스크 응답 지연이다. 이것이 §2에서 다룰 **타이머 스냅샷** 방법이다. `HN()`은 처음 짠 예제 2가 멈춰서 넣은 매크로다 — `while (h.n < N)`에서 `h`가 `volatile`이 아니니 컴파일러가 루프 밖으로 읽기를 빼서 무한 루프가 됐다(ISR이 바꾸는 값을 스레드가 폴링할 때의 고전적 실수, C7 §1).

---

## 1. 정의 — 인터럽트 지연, 태스크 응답 지연, 지터

> 🔁 이 절은 Bare-Metal BM J7과 겹친다 (주인: 이 노트).

### 1.1 직관 — 택배가 문 앞에 온 순간부터 내가 상자를 여는 순간까지

초인종(IRQ)이 울렸다. 내가 문을 여는 데 걸리는 시간은 (1) 하던 일을 바로 놓을 수 있는가(통화 중이면 끝날 때까지 — 마스킹), (2) 더 급한 사람이 먼저 나갔는가(상위 ISR), (3) 문까지 걸어가는 시간(하드웨어 진입)이다. 여기까지가 **인터럽트 지연**이다. 그런데 택배를 받는 것과 **상자를 열어 쓰는 것**은 다르다. 문에서 받은 상자를 방에 있는 사람(태스크)에게 전달하고(커널 호출), 그 사람이 하던 일을 정리하고(컨텍스트 전환), 그보다 바쁜 형(상위 태스크)이 먼저 쓰고 나서야 연다. 이것까지가 **태스크 응답 지연**이다.

### 1.2 정의

- **인터럽트 지연(interrupt latency)**: 주변장치가 IRQ 신호를 올린 순간부터 그 ISR의 **첫 명령**(또는 첫 "쓸모 있는" 명령)이 실행되는 순간까지. Arm 문서의 "12 cycles"는 이 중 **하드웨어 진입**(②)만, 그것도 최선 조건에서의 값이다.
- **태스크 응답 지연(task response latency, IRQ → task latency)**: 같은 IRQ 순간부터 그 이벤트를 처리할 **태스크의 첫 명령**까지. RTOS에서 설계에 쓰는 숫자는 대개 이쪽이다(A7: "PendSV 핸들러가 수십 사이클"은 이 숫자가 아니다).
- **지터(jitter)**: 같은 지연을 여러 번 쟀을 때의 흔들림. 보통 **peak-to-peak**(최댓값 − 최솟값)로 말하고, 분포 전체는 히스토그램으로 본다. 표준편차는 꼬리를 숨기므로 실시간 계약에는 거의 쓰지 않는다(§7). 주기 태스크의 "시작 시각 흔들림"(release jitter)은 E7, 응답 시간 분석에서 지터를 입력으로 쓰는 법은 N3의 몫이다.

### 1.3 구성 요소 표 — 각 조각은 무엇이 결정하나

| 조각 | 무엇이 결정하나 | 일정한가 | 이 노트에서 재는 곳 |
|---|---|---|---|
| ① 마스킹 구간 | PRIMASK/BASEPRI가 올라가 있던 남은 시간(앱 · 커널 크리티컬 섹션), 같은/높은 우선순위 ISR의 남은 실행 | 아니다 — IRQ가 어느 위상에 떨어지느냐에 따라 0 ~ L | §5 ex7, C7 §6 |
| ② HW 진입 | 스태킹 8워드, 벡터 읽기, 파이프라인 재충전, 플래시 wait state, 진행 중인 버스 트랜잭션, tail-chain/late-arrival 여부 | 거의 — 메모리 대기가 흔들 수 있다 | §3.1 표(실칩 값), ex2 |
| ③ 프롤로그 · 디스패치 | 컴파일러가 만든 `push`, Zephyr `_isr_wrapper`의 표 조회 | 일정 | ex2, ex3, ex6 |
| ④ ISR 본문 + 커널 호출 | 주변장치 클리어, `vTaskNotifyGiveFromISR`/`k_sem_give`(ready 리스트 · 타임 슬라이스) | 거의 — 커널 상태에 따라 경로가 바뀐다 | ex5, ex6 |
| ⑤ ISR 퇴장 | `portYIELD_FROM_ISR` = ICSR.PENDSVSET 쓰기, Zephyr `z_arm_int_exit` | 일정 | ex5, ex6 |
| ⑥ PendSV · 전환 | tail-chain으로 PendSV 진입, 레지스터 저장, 다음 태스크 선택, 스택 검사 · 런타임 통계 | 설정에 따라 다르다 | ex5 lean vs 기본 |
| ⑦ 상위 태스크 간섭 | 더 높은 태스크의 남은 실행 + 그 태스크의 커널 경로 | 아니다 | ex7 med 태스크 |
| ⑧ 복원 · API 복귀 | 레지스터 복원, 예외 복귀, 블로킹 API의 나머지(`ulTaskNotifyTake`의 크리티컬 섹션 탈출) | 일정 | ex5, ex6 |

**흔한 함정**: ①은 "인터럽트 지연"에 들어가는데도 데이터시트의 지연 숫자에는 **절대 들어 있지 않다**. "M4는 인터럽트 지연이 12사이클"이라고 말한 사람의 시스템에서도 64 µs짜리 `__disable_irq()` 구간 하나가 있으면 최악 인터럽트 지연은 64 µs다. 면접에서 숫자를 말할 때는 **어느 조각들의 합인지**를 같이 말한다.

---

## 2. 측정 방법 네 가지 — 무엇부터 무엇까지 재는가

> 🔁 이 절은 Bare-Metal BM M7과 겹친다 (주인: 이 노트).

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="18" font-size="13" font-weight="bold">네 가지 방법이 재는 구간</text>
<line x1="190" y1="30" x2="190" y2="235" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="190" y="44" font-size="12" text-anchor="middle">IRQ 발생</text>
<line x1="340" y1="50" x2="340" y2="235" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="340" y="44" font-size="12" text-anchor="middle">ISR 첫 줄</text>
<line x1="590" y1="50" x2="590" y2="235" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="590" y="44" font-size="12" text-anchor="middle">태스크 첫 줄</text>
<text x="10" y="76" font-size="12">타이머 스냅샷</text>
<line x1="190" y1="72" x2="340" y2="72" stroke="#4a7bd0" stroke-width="3" marker-end="url(#ah2)"/>
<line x1="190" y1="84" x2="590" y2="84" stroke="#4a7bd0" stroke-width="1.5" marker-end="url(#ah2)"/>
<text x="360" y="99" font-size="11">두 화살표 모두 RELOAD − VALUE로 읽는다</text>
<text x="10" y="120" font-size="12">GPIO + scope</text>
<line x1="190" y1="116" x2="355" y2="116" stroke="#e08a3c" stroke-width="3" marker-end="url(#ah2)"/>
<rect x="340" y="104" width="15" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="365" y="132" font-size="11">+ 쓰기 → 핀 지연</text>
<text x="10" y="164" font-size="12">DWT CYCCNT</text>
<line x1="340" y1="160" x2="590" y2="160" stroke="#3f9a6b" stroke-width="3" marker-end="url(#ah2)"/>
<text x="200" y="164" font-size="11">IRQ 순간은 모름</text>
<text x="10" y="208" font-size="12">트레이스</text>
<circle cx="340" cy="204" r="5" fill="#888"/>
<circle cx="420" cy="204" r="5" fill="#888"/>
<circle cx="480" cy="204" r="5" fill="#888"/>
<circle cx="590" cy="204" r="5" fill="#888"/>
<text x="200" y="208" font-size="11">이벤트마다 기록</text>
<text x="370" y="228" font-size="11">ISR 진입 · 퇴장 · 전환 · 태스크 시작</text>
<text x="10" y="252" font-size="11">빨간 점선 = 하드웨어가 IRQ를 올린 순간. 이것을 직접 보는 것은 타이머 스냅샷과 scope뿐이다.</text>
</svg>
```

그림 2 — 방법마다 시작점이 다르다. **IRQ가 올라간 순간**(빨간 점선)을 보려면 그 순간을 하드웨어가 기록해 두거나(타이머 스냅샷, 입력 캡처), 핀으로 밖에서 봐야 한다(scope). DWT CYCCNT와 소프트웨어 트레이스는 **코드가 실행된 두 지점 사이**만 잴 수 있다 — 첫 지점이 ISR 첫 줄이니 ①②③은 보이지 않는다.

### 2.1 타이머 스냅샷 — "IRQ를 올린 장치에게 언제 올렸는지 묻는다"

**정의**: IRQ를 일으키는 타이머(또는 IRQ 시각을 하드웨어로 캡처하는 입력 캡처 채널)의 카운터를 ISR · 태스크에서 읽어 "이벤트 이후 경과"를 계산한다. 이 노트의 TIMER0처럼 **IRQ 원인 자체가 타이머**면 공짜다. 원인이 외부 핀(IMU data-ready)이면, 그 핀을 **입력 캡처(input capture)** 채널에도 연결해 하드웨어가 에지 순간의 카운터 값을 래치하게 한다 — ISR에서 `now - CCR`이 인터럽트 지연이다(입력 캡처 자체는 E5).

**장점**: 장비가 필요 없고, 펌웨어 안에서 수백만 표본을 히스토그램으로 모을 수 있고, ①부터 볼 수 있다. **단점**: 타이머 클럭 해상도(이 보드 40 ns, STM32H7 TIM2를 240 MHz로 돌리면 4.2 ns), 읽기 자체의 비용(아래 ex1), 카운터 랩어라운드(32비트 · 16비트, E5).

### 2.2 GPIO 토글 + scope / 로직 분석기 — "밖에서 본다"

**정의**: 측정하고 싶은 지점에서 남는 GPIO를 올리고(내리고), 오실로스코프나 로직 분석기로 **원인 신호**(IRQ를 만든 핀, 예: IMU INT)와 **디버그 핀**의 에지 간격을 잰다. Don이 매일 버스 장애를 볼 때 쓰는 그 장비다.

읽는 법(그림 3):

- **트리거는 원인 신호의 에지**에 건다(CH1). 디버그 핀(CH2)의 에지까지가 지연이다.
- **무한 잔상(infinite persistence)** 모드로 수 분을 띄워 두면 CH2 에지가 퍼진 **띠**가 된다. 띠의 왼쪽 끝이 최솟값, 오른쪽 끝이 지금까지의 최댓값이다. 띠 안의 진하기가 곧 히스토그램이다. 요즘 scope는 측정값 히스토그램 · 통계(min/max/표준편차, 표본 수)를 직접 준다.
- **set-to-pin 지연을 뺀다**: GPIO 레지스터 쓰기 명령 → 버스(AHB/APB 브리지) → 출력 레지스터 → 출력 드라이버 → 패드의 상승 시간. 버스 경로와 출력 속도 설정(STM32 `OSPEEDR`), 부하 커패시턴스에 달려 있어 칩마다 다르다. **잰다**: ISR 첫 줄에서 핀을 올리고 바로 다음 줄에서 내리면 그 펄스 폭이 "쓰기 한 번 + 명령 한 줄"이고, 같은 핀을 두 번 연속 토글한 에지 간격으로 쓰기 한 번의 비용을 잡을 수 있다.
- **하나 이상 핀을 쓴다**: ISR 진입(CH2), 태스크 시작(CH3)을 따로 두면 그림 1의 ①~③과 ④~⑧을 한 화면에서 나눌 수 있다. 로직 분석기는 채널이 많고 기록이 길어서 "가끔 튀는" 표본을 잡는 데 유리하다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="18" font-size="13" font-weight="bold">scope 화면 읽기 — 무한 잔상 모드</text>
<text x="10" y="70" font-size="12">CH1 IMU INT</text>
<polyline points="110,80 200,80 204,45 640,45" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<text x="210" y="38" font-size="11">트리거 (에지 = 0 ns)</text>
<text x="10" y="150" font-size="12">CH2 디버그 GPIO</text>
<rect x="290" y="115" width="150" height="45" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="3 3"/>
<polyline points="110,160 290,160 294,115 640,115" fill="none" stroke="#e08a3c" stroke-width="2"/>
<polyline points="110,160 440,160 444,115" fill="none" stroke="#e08a3c" stroke-width="1" stroke-opacity="0.6"/>
<text x="365" y="108" font-size="11" text-anchor="middle">잔상의 띠 = 지터</text>
<line x1="204" y1="185" x2="292" y2="185" stroke="currentColor" marker-start="url(#ah3)" marker-end="url(#ah3)"/>
<text x="248" y="200" font-size="11" text-anchor="middle">최소 지연</text>
<line x1="204" y1="215" x2="442" y2="215" stroke="currentColor" marker-start="url(#ah3)" marker-end="url(#ah3)"/>
<text x="323" y="230" font-size="11" text-anchor="middle">지금까지 본 최대 지연</text>
<text x="470" y="150" font-size="11">각 에지 = 실제 지연</text>
<text x="470" y="165" font-size="11">+ GPIO 쓰기 → 핀 지연</text>
<text x="470" y="180" font-size="11">(set-to-pin, 미리 재서 뺀다)</text>
</svg>
```

그림 3 — scope로 보는 인터럽트 지연. 오른쪽 끝은 "지금까지" 본 최댓값일 뿐이다(§7.4 — 오래 띄워 둘수록 넓어진다).

### 2.3 DWT CYCCNT — "코어 클럭 사이클을 코드에서 직접 센다"

**정의**: Cortex-M3/M4/M7/M33/M55/M85(Armv7-M · Armv8-M Mainline)의 DWT(Data Watchpoint and Trace) 블록에 있는 32비트 사이클 카운터다. 코어 클럭마다 1씩 오르므로 480 MHz H7이면 해상도 2.08 ns, 약 8.9초에 한 바퀴 돈다(2^32 / 480 MHz). **Cortex-M0/M0+(Armv6-M)에는 없다.** 켜는 순서(레지스터 주소는 CMSIS `core_cm3.h`/`core_cm7.h`, Zephyr `arch/arm/include/cortex_m/dwt.h`에서 확인):

1. `DEMCR`(0xE000EDFC)의 bit 24 **TRCENA** = 1 — DWT · ITM 블록에 전원을 넣는다. 디버거가 붙어 있으면 이미 켜져 있을 때가 많아서, "디버거로는 되는데 단독 부팅에서는 0"인 버그가 여기서 나온다.
2. (일부 Cortex-M7) **잠금 해제**: `DWT_LSR`(0xE0001FB4) bit 0이 "잠금 있음", bit 1이 "잠김"이면 `DWT_LAR`(0xE0001FB0)에 0xC5ACCE55를 쓴다. Zephyr `dwt_access()`가 `CONFIG_CPU_CORTEX_M7`에서만 LSR을 보고 이 일을 한다. STM32F7/H7에서 이걸 빼먹으면 CYCCNT가 0에 머문다는 보고가 흔하다.
3. `DWT_CYCCNT`(0xE0001004) = 0, `DWT_CTRL`(0xE0001000) bit 0 **CYCCNTENA** = 1.
4. **확인**: `DWT_CTRL` bit 25 **NOCYCCNT**가 1이면 카운터가 없는 구현이다. 하지만 아래 ex4가 보이듯 **이 비트만 믿으면 안 되고, 실제로 오르는지 자가 시험**을 한다.

같은 DWT에는 **EXCCNT**(0xE000100C, 8비트, `DWT_CTRL` bit 18 EXCEVTENA) — "예외 진입 · 퇴장 오버헤드 사이클"을 세는 카운터 — 도 있다. ②를 직접 재는 하드웨어 카운터다(8비트라 256마다 한 바퀴, 오버플로 이벤트를 ITM으로 내보내는 용도라 단독으로는 짧은 구간에만 쓴다). DWT의 나머지(워치포인트 · PC 샘플링 · ITM/SWO)는 M6의 몫이다.

**주의**: CYCCNT는 코어 클럭을 센다 — 클럭이 멈추는 저전력 상태에서는 멈출 수 있고(칩마다 확인), 디버거가 코어를 halt하면 증가하지 않는다. 그리고 **IRQ가 올라간 순간은 모른다**(그림 2). 그래서 실무 조합은 "IRQ 시각은 타이머 입력 캡처, 그 뒤 구간은 CYCCNT"다.

### 2.4 트레이스 — "모든 이벤트에 타임스탬프"

- **소프트웨어 이벤트 트레이스**: SEGGER SystemView, Percepio Tracealyzer가 대표다. 커널의 trace 훅(FreeRTOS `traceTASK_SWITCHED_IN` 등, Zephyr tracing 서브시스템)과 ISR 진입 · 퇴장 훅에서 이벤트와 타임스탬프(Cortex-M에서는 흔히 CYCCNT)를 RAM 버퍼에 쓰고, 디버그 프로브(J-Link RTT 등)나 UART로 뽑아 타임라인으로 보여 준다. ISR 진입부터 태스크 전환까지를 **모든 표본에 대해** 볼 수 있는 대신 이벤트마다 기록 비용이 든다(아래 ex1, §4의 계측 비용).
- **하드웨어 명령 트레이스**: ETM(Embedded Trace Macrocell)은 실행한 명령 흐름을 트레이스 포트나 ETB로 내보낸다 — 계측 코드 없이 사이클 정확도에 가깝다. 대신 트레이스 포트 핀과 비싼 프로브가 필요하다. MTB(M0+/M33)는 RAM에 분기 기록만 남긴다.
- 타임라인 읽는 법과 도구 설정은 **M3**, ETM · ITM 하드웨어는 **M6**에서 다룬다. 이 노트에서는 trace 훅을 "내가 직접 찍는 타임스탬프 자리"로만 쓴다(§4).

### 2.5 무엇을 언제 쓰나 — 선택표

| 상황 | 첫 선택 | 이유 |
|---|---|---|
| 데이터시트 숫자를 의심, 보드 bring-up | GPIO + scope | 펌웨어 시계를 믿기 전에 밖에서 본다. 원인 신호까지 보인다 |
| 운전 중 지연 분포 · 최악값 수집(수 시간) | 타이머 스냅샷 + 펌웨어 히스토그램 | 장비 없이 수백만 표본, 필드에서도 돈다 |
| ISR · 커널 경로 구간 분해 | CYCCNT(또는 타이머) 여러 지점 | 명령 단위 해상도 |
| "가끔 튐"의 원인 찾기 | 트레이스(SystemView · Tracealyzer) 또는 원인 태깅(§6) | 그 순간 무엇이 돌았는지가 필요 |
| 계측이 타이밍을 너무 바꿀 때 | ETM, 또는 scope + 핀 1개 | 비침투 |

---

## 3. 측정의 비용 — 측정이 측정을 바꾼다

> 🔁 이 절은 Bare-Metal BM M7과 겹친다 (주인: 이 노트).

### 3.1 실칩의 하드웨어 진입 값 — QEMU로 재지 못하는 것

QEMU는 예외 스태킹 사이클을 0으로 친다(B2 §8.2). 그래서 ② 구간은 Arm 문서 값으로 적는다. **아래 표는 측정이 아니다.**

| 항목 | 값 | 출처 · 조건 |
|---|---|---|
| IRQ → ISR 첫 명령 (M3 · M4) | 12 사이클 | Cortex-M3 TRM r2p0 §5.5, Yiu(Arm blog 2016). zero wait state 메모리, 더 높은 예외에 막히지 않을 때 |
| IRQ → ISR 첫 명령 (M0 / M0+) | 16 / 15 사이클 | 같은 블로그 |
| tail-chain (ISR 끝 → 다음 ISR) | 6 사이클 | Cortex-M3 TRM §5.6 — 대기 중인 IRQ는 앞 ISR이 끝날 때까지 **전부** 기다린 뒤 6사이클 |
| late-arrival | 스태킹을 다시 하지 않음 | 낮은 IRQ 스태킹 도중 높은 IRQ가 오면 벡터만 바꾼다(B2 §8) |
| 플래시 wait state | 칩 · 클럭 · 전압마다 | 예: STM32F405/407은 2.7 ~ 3.6 V에서 150 ~ 168 MHz면 5 WS(RM0090의 Flash 접근 시간 표). 벡터 · 핸들러가 플래시에 있으면 ART · I-cache가 놓칠 때 진입이 늘어난다 |
| `push` N개 레지스터 | 약 1 + N 사이클 | Cortex-M3 TRM 명령 타이밍(LDM/STM 계열). QEMU icount는 명령 1개로 센다(ex3) |

실칩에서는 여기에 진행 중인 느린 버스 접근(APB 주변장치 읽기가 끝나야 한다), 다른 버스 마스터(DMA)와의 경합이 더해진다. 그래서 "12사이클"은 **하한**이지 예산이 아니고, 실칩에서는 반드시 GPIO + scope나 입력 캡처 + CYCCNT로 다시 잰다.

### 3.2 예제 1 — 측정 도구 하나하나의 값

무엇을 확인하나: 이 노트가 쓰는 계측 동작(카운터 읽기, 타이머 스냅샷, us 시계, "GPIO" 토글, RAM 트레이스 한 건, printf 한 줄)이 각각 몇 ns를 먹는지 1000번 평균으로 잰다. bare-metal, ICOUNT=1. GPIO 대신 FPGAIO LED 레지스터(0x40028000)에 쓴다 — 실제 핀으로 나가지는 않지만 "주변장치 레지스터에 쓰기 두 번"의 비용은 같다.

```c
/* ex1 — what does each measurement probe cost? (bare-metal, QEMU icount) */
#include "e3.h"
#define N 1000
typedef struct { uint32_t cyc, id; } ev_t;
static ev_t ring[256];
static volatile uint32_t widx, sink;
static void trace_ev(uint32_t id)                   /* RAM trace buffer entry */
{
    uint32_t i = widx++ & 255u;
    ring[i].cyc = CYC();
    ring[i].id = id;
}
#define MEASURE(name, body) do {                                   \
        uint32_t t0 = CYC();                                       \
        for (int k = 0; k < N; k++) { body; }                      \
        uint32_t dt = CYC() - t0;                                  \
        board_printf_raw("%s %u ns/op\n", name, NS(dt) / N);       \
    } while (0)
int main(void)
{
    TIMER_RELOAD(TIMER0_BASE) = 0xFFFFFFFFu;        /* free-running for the snapshot test */
    TIMER_CTRL(TIMER0_BASE) = 1u;
    MEASURE("empty loop            ", __asm volatile("" ::: "memory"));
    MEASURE("CYC() read            ", sink = CYC());
    MEASURE("timer snapshot        ", sink = t0_elapsed());
    MEASURE("board_now_us()        ", sink = board_now_us());
    MEASURE("LED (GPIO) set+clear  ", LED = 1; LED = 0);
    MEASURE("trace_ev() to RAM     ", trace_ev(7));
    uint32_t t0 = CYC();
    board_printf_raw("[%u] isr lat=%u\n", 123u, 456u);   /* the classic printf probe */
    uint32_t dt = CYC() - t0;
    board_printf_raw("printf one line       %u ns (QEMU UART has no baud delay)\n", NS(dt));
    board_printf_raw("same 18 chars @115200 %u ns if blocking on a real UART\n", 18u * 86806u);
    board_exit(0);
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/E3; make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/bm/ex1_probe_cost.c OUT=/var/tmp/rtos/E3/ex1 EXTRA_CFLAGS=-I$S/inc && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E3/ex1/app.elf 30'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4836	     96	    336	   5268	   1494	/var/tmp/rtos/E3/ex1/app.elf
empty loop             64 ns/op
CYC() read             128 ns/op
timer snapshot         192 ns/op
board_now_us()         352 ns/op
LED (GPIO) set+clear   128 ns/op
trace_ev() to RAM      192 ns/op
[123] isr lat=456
printf one line       9760 ns (QEMU UART has no baud delay)
same 18 chars @115200 1562508 ns if blocking on a real UART
[board_exit] code=0
```

출력에서 볼 것(빈 루프 64 ns = 명령 2개를 빼서 읽는다):

- **카운터 읽기 64 ns(명령 2개), 타이머 스냅샷 128 ns(4개), GPIO 올렸다 내리기 64 ns, RAM 트레이스 한 건 128 ns** — 수 명령짜리 계측은 µs 단위 지연을 재는 데 충분히 싸다. 그래도 0은 아니다. 한 경로에 계측점을 5개 넣으면 그만큼 경로가 길어진다(§4에서 실제로 1.0 µs가 늘어난 것을 본다).
- `board_now_us()`는 나눗셈 때문에 288 ns — "µs 시계"로 µs 이하를 재는 건 해상도와 비용 둘 다 손해다. 사이클 단위 시계를 쓰고 계산은 나중에(호스트에서) 한다.
- **printf 한 줄은 9.76 µs**, 그것도 QEMU UART에 baud 지연이 없어서다. 실제 115200 baud UART에 막아 쓰면 18글자에 **1.56 ms** — 재려던 지연(µs)보다 세 자릿수 크다. ISR 안의 printf 계측은 측정이 아니라 **새 지연원**이다. 값은 RAM에 모으고 측정이 끝난 뒤 출력한다(이 노트의 모든 예제가 그렇게 한다).

### 3.3 예제 2 — "ISR 첫 줄"은 어디인가: asm 스텁 vs C vs 레지스터를 많이 쓰는 C

무엇을 확인하나: 같은 TIMER0 IRQ를 세 가지 핸들러로 받는다. V1은 naked asm 스텁(두 번째 명령에서 VALUE를 읽음), V2는 평범한 C 핸들러(첫 줄에서 스냅샷), V3는 레지스터를 많이 쓰는 C 핸들러. CPU가 바쁜 루프 중일 때와 `wfi`로 자는 중일 때도 나눈다.

```c
/* ex2 — IRQ assertion -> handler: asm stub vs plain C vs register-heavy C, busy vs WFI */
#include "e3.h"
#define N 200
static hist_t h;
volatile uint32_t sink;
void isr_c(uint32_t lat)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    h_add(&h, lat, 1);
}
#if V == 1          /* 1: naked asm stub, VALUE read in the 2nd instruction */
__attribute__((naked)) void TIMER0_IRQHandler(void)
{
    __asm volatile("ldr r3, =0x40000004 \n ldr r0, [r3] \n ldr r1, [r3, #4] \n"
                   "subs r0, r1, r0 \n b isr_c \n .ltorg");   /* lat = RELOAD - VALUE */
}
#elif V == 2        /* 2: plain C, snapshot on the first line */
void TIMER0_IRQHandler(void) { isr_c(t0_elapsed()); }
#else               /* 3: C handler that needs many callee-saved registers */
__attribute__((noinline)) static uint32_t mix(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ return a * b + c * d; }
void TIMER0_IRQHandler(void)
{
    uint32_t lat = t0_elapsed();
    uint32_t a = sink, b = a + 3, c = b * 7, d = c ^ a, e = d + b, f = e * c, g = f - d;
    sink = mix(a, b, c, d) + mix(e, f, g, a) + mix(b, d, f, g);
    isr_c(lat);
}
#endif
static void report(const char *m)
{
    board_printf_raw("V%d %s n=%u min=%u avg=%u max=%u ns\n", V, m, h.n, NS(h.min),
                     NS((uint32_t)(h.sum / h.n)), NS(h.max));
    h = (hist_t){0};
}
int main(void)
{
    t0_start(2500 + 13, 0x40);                      /* ~100 us */
    while (HN(h) < N) { }
    report("busy");
    while (HN(h) < N) __asm volatile("wfi");
    report("wfi ");
    t0_stop();
    board_exit(0);
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/E3; for v in 1 2 3; do make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/bm/ex2_entry.c OUT=/var/tmp/rtos/E3/ex2_v$v EXTRA_CFLAGS="-I$S/inc -DV=$v" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E3/ex2_v$v/app.elf 30 | grep -v board_exit; done'
```

```text
V1 busy n=200 min=0 avg=0 max=40 ns
V1 wfi  n=200 min=0 avg=0 max=0 ns
V2 busy n=200 min=120 avg=120 max=160 ns
V2 wfi  n=200 min=120 avg=120 max=120 ns
V3 busy n=200 min=80 avg=80 max=120 ns
V3 wfi  n=200 min=80 avg=80 max=80 ns
```

출력에서 볼 것:

- **V1은 0 ns**다. QEMU에서는 IRQ가 올라간 순간과 핸들러 두 번째 명령 사이에 가상 시간이 거의 흐르지 않는다 — 스태킹 · 벡터 읽기가 공짜이기 때문이다. 실칩이면 여기에 최소 12사이클이 붙는다(§3.1). **QEMU 숫자로 ②를 말하면 안 된다**는 것을 숫자 0이 보여 준다.
- V2(120 ns)가 레지스터를 훨씬 많이 쓰는 V3(80 ns)보다 **느리다**. 이유는 예제 3의 디스어셈블에 있다.
- `wfi` 중이든 바쁜 루프 중이든 같다 — QEMU는 저전력 상태에서의 깨어남 지연(클럭 재시작, 플래시 깨우기)을 모델링하지 않는다. 실칩에서 깊은 sleep 모드의 깨어남은 수 µs ~ 수십 µs가 될 수 있다(칩 데이터시트의 wake-up time 항목, P1). **지연 사양을 말할 때는 "CPU가 깨어 있을 때"인지 같이 말한다.**

### 3.4 예제 3 — 컴파일러가 계측점을 옮긴다 (`objdump`)

무엇을 확인하나: 예제 2의 세 핸들러가 실제로 몇 번째 명령에서 TIMER0 VALUE(오프셋 4)를 읽는지.

```sh
limactl shell elinux -- bash -lc 'for v in 1 2 3; do echo "== V$v"; arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/E3/ex2_v$v/app.elf | awk "/<TIMER0_IRQHandler>:/,/^\$/" | head -16; done'
```

```text
== V1
00000170 <TIMER0_IRQHandler>:
     170:	ldr	r3, [pc, #8]	@ (17c <TIMER0_IRQHandler+0xc>)
     172:	ldr	r0, [r3, #0]
     174:	ldr	r1, [r3, #4]
     176:	subs	r0, r1, r0
     178:	b.w	118 <isr_c>
     17c:	.word	0x40000004

== V2
00000118 <TIMER0_IRQHandler>:
     118:	mov.w	r1, #1073741824	@ 0x40000000
     11c:	movs	r0, #1
     11e:	ldr	r3, [pc, #80]	@ (170 <TIMER0_IRQHandler+0x58>)
     120:	ldr	r2, [r1, #8]
     122:	push	{r4}
     124:	ldr	r4, [r1, #4]
     126:	str	r0, [r1, #12]
     128:	ldr	r0, [r3, #0]
...
== V3
00000124 <TIMER0_IRQHandler>:
     124:	stmdb	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
     128:	mov.w	r8, #1073741824	@ 0x40000000
     12c:	ldr.w	r9, [pc, #160]	@ 1d0 <TIMER0_IRQHandler+0xac>
     130:	ldr.w	r4, [r8, #8]
     134:	ldr.w	r3, [r8, #4]
...
```

(V2 · V3의 나머지 줄은 생략했다.) 출력에서 볼 것:

- V1은 두 번째 명령(`ldr r0, [r3, #0]`)에서 VALUE를 읽는다(이 스텁에서 r3 = 0x40000004이므로 `[r3]`가 VALUE, `[r3, #4]`가 RELOAD).
- **V2는 `isr_c()`를 인라인하면서 명령 순서를 섞었다** — INTCLR에 쓸 값 준비(`movs r0, #1`), 리터럴 로드, `push {r4}`가 VALUE 읽기(0x124, 여섯 번째 명령) 앞에 왔다. C 소스의 "첫 줄"은 기계어의 첫 명령이 아니다. volatile 접근끼리의 순서만 지켜질 뿐, 그 사이에 다른 명령이 얼마든지 끼어든다.
- **V3의 첫 명령은 레지스터 10개를 쌓는 `stmdb`** 하나이고 VALUE는 다섯 번째 명령에서 읽는다. QEMU icount는 이 `stmdb`를 명령 1개(32 ns)로 치지만, 실제 M3에서는 약 1 + 10 = 11사이클이다(§3.1). **QEMU에서는 V3가 더 빨라 보이지만 실칩에서는 V3가 더 느리다.** 프롤로그 비용은 "명령 수"가 아니라 "쌓는 레지스터 수"로 읽는다.
- 실무 규칙: 정밀하게 재야 하면 계측 읽기를 asm 스텁이나 핸들러 맨 앞의 인라인 asm으로 고정하고, 재는 사람이 **디스어셈블로 계측점 위치를 확인**한다. 그리고 계측점 앞에 있는 명령 수(여기서는 1 ~ 5개)를 결과에서 뺀다.

### 3.5 예제 4 — DWT CYCCNT를 교과서대로 켜고, 믿기 전에 시험한다

무엇을 확인하나: §2.3의 순서(TRCENA → 필요하면 LAR 해제 → 0으로 초기화 → CYCCNTENA)를 그대로 하고, 레지스터를 읽어 보고, FPGAIO 시계로 100 µs를 바쁘게 기다리는 동안 CYCCNT가 정말 오르는지 확인한다. 오르지 않으면 대체 시계로 넘어간다. 실칩에서도 그대로 쓸 수 있는 "자가 시험 후 선택" 패턴이다.

```c
/* ex4 — enable DWT CYCCNT the textbook way, then SELF-TEST it before trusting it */
#include "e3.h"
#define DEMCR      REG32(0xE000EDFCu)      /* bit24 TRCENA: power the DWT/ITM blocks */
#define DWT_CTRL   REG32(0xE0001000u)      /* bit0 CYCCNTENA, bit25 NOCYCCNT, [31:28] NUMCOMP */
#define DWT_CYCCNT REG32(0xE0001004u)
#define DWT_EXCCNT REG32(0xE000100Cu)      /* 8-bit exception-overhead counter (EXCEVTENA bit18) */
#define DWT_LAR    REG32(0xE0001FB0u)      /* lock access: write 0xC5ACCE55 (needed on some M7) */
#define DWT_LSR    REG32(0xE0001FB4u)      /* bit0 lock present, bit1 locked */
typedef uint32_t (*clk_fn)(void);
static uint32_t clk_dwt(void)  { return DWT_CYCCNT; }
static uint32_t clk_fpga(void) { return CYC(); }      /* 25 MHz FPGAIO fallback */
static clk_fn pick_clock(void)
{
    DEMCR |= 1u << 24;                     /* 1) TRCENA */
    board_printf_raw("LSR=0x%x (lock present=%u)\n", DWT_LSR, DWT_LSR & 1u);
    if (DWT_LSR & 2u) DWT_LAR = 0xC5ACCE55u; /* 2) unlock only if a lock exists and is set */
    DWT_CYCCNT = 0;                        /* 3) reset, 4) enable */
    DWT_CTRL |= 1u | (1u << 18);
    board_printf_raw("DEMCR=0x%x DWT_CTRL=0x%x NUMCOMP=%u NOCYCCNT=%u\n", DEMCR, DWT_CTRL,
                     DWT_CTRL >> 28, (DWT_CTRL >> 25) & 1u);
    uint32_t a = DWT_CYCCNT, f0 = CYC();
    spin_cyc(2500);                        /* 100 us by the FPGAIO clock */
    uint32_t b = DWT_CYCCNT, f1 = CYC();
    board_printf_raw("100 us busy: CYCCNT %u -> %u, FPGAIO %u -> %u, EXCCNT=%u\n",
                     a, b, f0, f1, DWT_EXCCNT);
    if (b - a > 1000u) { board_printf_raw("clock = DWT CYCCNT\n"); return clk_dwt; }
    board_printf_raw("CYCCNT is not counting -> fall back to FPGAIO COUNTER\n");
    return clk_fpga;
}
int main(void)
{
    clk_fn now = pick_clock();
    uint32_t t = now(); spin_cyc(250); uint32_t d = now() - t;
    board_printf_raw("10 us busy measured with chosen clock = %u ticks\n", d);
    board_exit(0);
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/E3; make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/bm/ex4_dwt.c OUT=/var/tmp/rtos/E3/ex4 EXTRA_CFLAGS=-I$S/inc && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E3/ex4/app.elf 30'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4664	     96	    328	   5088	   13e0	/var/tmp/rtos/E3/ex4/app.elf
LSR=0x0 (lock present=0)
DEMCR=0x0 DWT_CTRL=0x0 NUMCOMP=0 NOCYCCNT=0
100 us busy: CYCCNT 0 -> 0, FPGAIO 1202 -> 3708, EXCCNT=0
CYCCNT is not counting -> fall back to FPGAIO COUNTER
10 us busy measured with chosen clock = 258 ticks
[board_exit] code=0
```

출력에서 볼 것:

- QEMU는 `DEMCR`, `DWT_CTRL`, `CYCCNT`, `EXCCNT` 모두 **읽으면 0, 쓰기는 무시**한다(DWT 미구현). 그런데 **NOCYCCNT도 0**이다 — "카운터 있음"이라고 말하는 셈이다. NOCYCCNT 비트만 검사하는 코드(Zephyr `z_arm_dwt_init_cycle_counter()`의 `__ASSERT`가 이것만 본다)는 이 환경에서 통과하고, 그 뒤 모든 측정이 0 사이클이 된다.
- 자가 시험(100 µs 동안 1000 이상 올랐나)이 이 함정을 잡아 대체 시계로 넘어갔다. 10 µs 대기를 258틱(= 10.3 µs)으로 쟀다 — `spin_cyc` 자체의 마지막 한 바퀴만큼 길다.
- 같은 함정이 실칩에서도 생긴다: TRCENA를 빼먹은 단독 부팅, 잠긴 M7 DWT, CYCCNT가 없는 M0+로 포팅한 코드. **시계는 쓰기 전에 다른 시계로 한 번 검산한다.** Zephyr의 `timing_counter_get()`(`arch/arm/core/cortex_m/timing.c`)도 Cortex-M에서 `CONFIG_CORTEX_M_DWT`이면 CYCCNT를 그대로 돌려준다 — Zephyr 벤치마크(`tests/benchmarks/latency_measure`)를 QEMU에서 돌려 0이 나오면 이 이유다.

### 3.6 observer effect를 다루는 네 가지 규칙

1. **비용을 잰다**: 예제 1처럼 계측 동작 하나의 비용을 재 두고, 계측점이 몇 개인지 센다.
2. **계측을 넣은 빌드와 뺀 빌드의 총합을 비교한다**: 차이가 계측 비용이다. §4에서 FreeRTOS는 1.0 µs(12%), Zephyr tracing은 2.48 µs가 나온다.
3. **출력은 나중에**: ISR에서는 RAM에 숫자만, 형식화와 출력은 측정이 끝난 뒤 낮은 우선순위에서.
4. **계측이 위상을 바꾼다는 것도 잊지 않는다**: 경로가 1 µs 길어지면 다른 주기 이벤트와 만나는 위상이 바뀌어 **히스토그램 모양**이 달라질 수 있다. §5의 예제 7은 원인 태깅 코드 네 줄을 넣자 기본 IRQ → 태스크가 11.08 µs에서 12.28 µs로 옮겨 갔다. 결론을 내기 전에 계측을 줄인 빌드로 한 번 더 확인한다.

---

## 4. 기본 지연을 구간으로 쪼갠다 — FreeRTOS vs Zephyr

### 4.1 예제 5 — FreeRTOS: 계측점 5개

무엇을 확인하나: TIMER0(우선순위 0x40, kernel-aware) ISR이 `vTaskNotifyGiveFromISR`로 우선순위 3 태스크를 깨우는 가장 흔한 경로를 다섯 구간으로 나눈다. 계측점: s1 = ISR 첫 줄, s2 = 클리어 + Give 직후, s3 = `portYIELD_FROM_ISR` 직후, s4 = **PendSV 안에서 스케줄러가 그 태스크를 고른 순간**(`traceTASK_SWITCHED_IN` 훅), s5 = 태스크가 `ulTaskNotifyTake`에서 돌아온 첫 줄. 모두 `t0_elapsed()`(IRQ 이후 경과)다. `INSTR=0`이면 s5만 남긴다(계측 비용 측정). `LEAN`이면 스택 오버플로 검사와 런타임 통계를 끈다.

s4를 찍으려면 커널 소스(tasks.c) 안에 계측점이 필요하다. FreeRTOS의 trace 매크로가 바로 그 자리다 — `vTaskSwitchContext()`가 `taskSELECT_HIGHEST_PRIORITY_TASK()` 바로 다음에 `traceTASK_SWITCHED_IN()`을 부른다(V11.3.2 tasks.c 5274행). 앱 폴더의 `FreeRTOSConfig.h`에 정의하면 tasks.c 안에서 펼쳐지므로 `pxCurrentTCB`도 쓸 수 있다.

```c
/* E3 FreeRTOSConfig.h — lab template + switches for this note */
#ifndef E3_FRTCFG_H
#define E3_FRTCFG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#ifdef LEAN                                   /* production-like: no stack check, no run-time stats */
#undef  configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW 0
#undef  configGENERATE_RUN_TIME_STATS
#define configGENERATE_RUN_TIME_STATS  0
#endif
#ifdef WRAP_TICK                              /* app provides SysTick_Handler -> xPortSysTickHandler */
#undef  xPortSysTickHandler
#endif
/* stamp the moment the scheduler picks the task we are measuring (inside PendSV) */
extern volatile uint32_t e3_armed, e3_s4;
extern void *volatile e3_hi;
#define traceTASK_SWITCHED_IN() do { if (e3_armed && (void *)pxCurrentTCB == e3_hi) {  \
        e3_s4 = TIMER_RELOAD(TIMER0_BASE) - TIMER_VALUE(TIMER0_BASE); e3_armed = 0; } } while (0)
#endif
```

```c
/* ex5 — FreeRTOS: split "TIMER0 IRQ -> task running" into stamps (cycles since IRQ raised) */
#include "FreeRTOS.h"
#include "task.h"
#include "e3.h"
#define N 200
volatile uint32_t e3_armed, e3_s4;
void *volatile e3_hi;
static volatile uint32_t s1, s2, s3;
static hist_t h[6];
static const char *name[6] = { "IRQ -> ISR first line      ", "ISR: clear + GiveFromISR   ",
    "ISR: portYIELD_FROM_ISR    ", "ISR exit -> PendSV -> pick ", "restore -> task resumes    ",
    "TOTAL IRQ -> task          " };
void TIMER0_IRQHandler(void)
{
    if (INSTR) s1 = t0_elapsed();
    TIMER_INTCLR(TIMER0_BASE) = 1;
    BaseType_t w = pdFALSE;
    vTaskNotifyGiveFromISR((TaskHandle_t)e3_hi, &w);
    if (INSTR) { s2 = t0_elapsed(); e3_armed = 1; }
    portYIELD_FROM_ISR(w);
    if (INSTR) s3 = t0_elapsed();
}
static void hi_task(void *a)
{
    (void)a;
    for (int i = 0; i < N; i++) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t s5 = t0_elapsed();
        if (INSTR) {
            h_add(&h[0], s1, 1); h_add(&h[1], s2 - s1, 1); h_add(&h[2], s3 - s2, 1);
            h_add(&h[3], e3_s4 - s3, 1); h_add(&h[4], s5 - e3_s4, 1);
        }
        h_add(&h[5], s5, 1);
    }
    t0_stop();
    for (int k = INSTR ? 0 : 5; k < 6; k++)
        board_printf_raw("%s min=%u avg=%u max=%u ns\n", name[k], NS(h[k].min),
                         NS((uint32_t)(h[k].sum / h[k].n)), NS(h[k].max));
    board_exit(0);
}
int main(void)
{
    TaskHandle_t t;
    xTaskCreate(hi_task, "hi", 512, NULL, 3, &t);
    e3_hi = t;
    t0_start(12500 + 37, 0x40);                 /* ~500 us, kernel-aware priority */
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/E3; for cfg in "default:-DINSTR=1" "default_noinstr:-DINSTR=0" "lean:-DINSTR=1 -DLEAN" "lean_noinstr:-DINSTR=0 -DLEAN"; do n=${cfg%%:*}; f=${cfg#*:}; echo "== $n"; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/frt/ex5_split.c OUT=/var/tmp/rtos/E3/ex5_$n EXTRA_CFLAGS="-I$S/inc $f" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E3/ex5_$n/app.elf 30 | grep -v board_exit; done'
```

```text
== default
IRQ -> ISR first line       min=120 avg=120 max=160 ns
ISR: clear + GiveFromISR    min=3360 avg=3360 max=3400 ns
ISR: portYIELD_FROM_ISR     min=480 avg=480 max=480 ns
ISR exit -> PendSV -> pick  min=3320 avg=3320 max=3360 ns
restore -> task resumes     min=1960 avg=2000 max=3480 ns
TOTAL IRQ -> task           min=9320 avg=9320 max=10800 ns
== default_noinstr
TOTAL IRQ -> task           min=8320 avg=8320 max=9800 ns
== lean
IRQ -> ISR first line       min=120 avg=120 max=600 ns
ISR: clear + GiveFromISR    min=3360 avg=3360 max=3400 ns
ISR: portYIELD_FROM_ISR     min=480 avg=480 max=480 ns
ISR exit -> PendSV -> pick  min=1800 avg=1800 max=1880 ns
restore -> task resumes     min=2000 avg=2000 max=3480 ns
TOTAL IRQ -> task           min=7840 avg=7840 max=9320 ns
== lean_noinstr
TOTAL IRQ -> task           min=6800 avg=6840 max=8320 ns
```

출력에서 볼 것:

- **계측 없는 총합 8.32 µs(기본) / 6.84 µs(lean)** — A7이 잰 8.28 / 6.88 µs와 같은 값이 다른 프로그램에서 재현된다(앱 코드가 조금 달라 ±40 ns).
- **계측을 넣으면 정확히 1.0 µs 늘어난다**(9.32 vs 8.32). 계측점 4개 × 약 6명령 + `e3_armed` 저장 + 훅 안의 비교다. 이 1.0 µs를 모르고 "IRQ → 태스크 9.3 µs"라고 보고하면 12% 틀린다.
- 가장 큰 두 조각은 **ISR 안의 커널 호출(3.36 µs = 84사이클)**과 **PendSV에서 태스크를 고르기까지(3.32 µs)**다. 레지스터 저장 · 복원 자체는 작다(C2 결론과 같다).
- **LEAN이 줄인 것은 정확히 한 구간**이다: "ISR exit → PendSV → pick"이 3.32 → 1.80 µs. 스택 오버플로 검사(`taskCHECK_FOR_STACK_OVERFLOW`)와 런타임 통계는 `vTaskSwitchContext()` 안, 즉 이 구간에서 돈다. 구간으로 쪼개 두면 "설정 하나가 어디에 얼마를 더하는지"가 바로 보인다.
- 최댓값이 가끔 1.48 µs 더 크다(10.80 vs 9.32, "restore" 구간과 lean의 "first line"). 아무 부하가 없어도 생기는 지터다 — 1 kHz SysTick이 이 경로와 겹친 표본이다. 이 정체는 §5에서 원인 태깅으로 확인한다.

### 4.2 예제 6 — Zephyr: tracing 훅으로 계측점 7개

무엇을 확인하나: Zephyr v4.4.2에서 같은 경로(TIMER0 → `k_sem_give` → main 스레드)를 쪼갠다. Zephyr는 `CONFIG_TRACING_USER=y`로 사용자 훅을 준다 — `subsys/tracing/user/tracing_user.c`의 weak 함수들(`sys_trace_isr_enter_user`, `sys_trace_isr_exit_user`, `sys_trace_thread_switched_out_user`, `sys_trace_thread_switched_in_user`)을 앱에서 다시 정의하면 된다. 훅 위치는 소스로 확인했다: ISR 훅은 `arch/arm/core/cortex_m/isr_wrapper.c`의 `_isr_wrapper()` 처음과 끝, 전환 훅은 `swap_helper.S`의 `z_arm_pendsv` 첫머리(`z_thread_mark_switched_out`)와 새 스레드 복원 뒤(`z_thread_mark_switched_in`)다.

```text
# z_dec/prj.conf
CONFIG_BOOT_BANNER=n
CONFIG_TRACING=y
CONFIG_TRACING_USER=y
```

```c
/* ex6 — Zephyr v4.4.2: split "TIMER0 IRQ -> thread running" with tracing user hooks */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>
#define REG(a)  (*(volatile uint32_t *)(a))
#define T0      0x40000000u
#define EL()    (REG(T0 + 8) - REG(T0 + 4))         /* cycles since TIMER0 raised IRQ 8 */
#define N 200
static volatile uint32_t z[8], arm_out, arm_in;     /* z[1..7] = stamps */
static uint32_t mn[8], mx[8], sum[8];
static K_SEM_DEFINE(sem, 0, 1);
static const char *nm[8] = { "", "IRQ -> _isr_wrapper (hook)", "wrapper -> my ISR line 1  ",
    "my ISR: clear + k_sem_give", "ISR return -> wrapper exit", "exit -> PendSV entry      ",
    "PendSV: swap -> switched_in", "-> k_sem_take returns     " };
#if defined(CONFIG_TRACING_USER)
void sys_trace_isr_enter_user(void) { if (__get_IPSR() == 16 + 8) z[1] = EL(); }
void sys_trace_isr_exit_user(void)  { if (__get_IPSR() == 16 + 8) z[4] = EL(); }
void sys_trace_thread_switched_out_user(void) { if (arm_out) { z[5] = EL(); arm_out = 0; arm_in = 1; } }
void sys_trace_thread_switched_in_user(void)  { if (arm_in) { z[6] = EL(); arm_in = 0; } }
#endif
#ifdef BUSY                                         /* a CPU-bound prio-5 thread instead of idle */
static void busy_fn(void *a, void *b, void *c) { for (;;) { } }
K_THREAD_DEFINE(busy, 512, busy_fn, NULL, NULL, NULL, 5, 0, 0);
#endif
static void t0_isr(const void *arg)
{
    ARG_UNUSED(arg);
    z[2] = EL();
    REG(T0 + 0xC) = 1;                              /* INTCLR */
    k_sem_give(&sem);
    z[3] = EL();
    arm_out = 1;
}
static void add(int k, uint32_t v)
{
    if (!mn[k] || v < mn[k]) mn[k] = v;
    if (v > mx[k]) mx[k] = v;
    sum[k] += v;
}
int main(void)
{
    IRQ_CONNECT(8, 2, t0_isr, NULL, 0);
    REG(T0) = 0; REG(T0 + 8) = 12500 + 37 - 1; REG(T0 + 4) = 12500 + 37 - 1;
    irq_enable(8);
    REG(T0) = 1 | 8;
    for (int i = 0; i < N; i++) {
        k_sem_take(&sem, K_FOREVER);
        uint32_t t = EL();
        for (int k = 2; k <= 6; k++) add(k, z[k] - z[k - 1]);
        add(1, z[1]); add(7, t - z[6]); add(0, t);
    }
    REG(T0) = 0;
    for (int k = IS_ENABLED(CONFIG_TRACING_USER) ? 1 : 8; k < 8; k++)
        printk("%s min=%u avg=%u max=%u ns\n", nm[k], mn[k] * 40, sum[k] / N * 40, mx[k] * 40);
    printk("TOTAL IRQ -> thread        min=%u avg=%u max=%u ns\n", mn[0] * 40, sum[0] / N * 40, mx[0] * 40);
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");   /* semihosting exit */
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/E3; /var/rtoslab/bin/rtos-zbuild mps2/an385 $S/z_dec /var/tmp/rtos/E3/z_dec && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E3/z_dec/zephyr/zephyr.elf 30
for v in "notrace:-DCONFIG_TRACING=n -DCONFIG_TRACING_USER=n" "ts_off:-DCONFIG_TIMESLICING=n" "ts_off_notrace:-DCONFIG_TIMESLICING=n -DCONFIG_TRACING=n -DCONFIG_TRACING_USER=n"; do n=${v%%:*}; a=${v#*:}; echo "== $n"; /var/rtoslab/bin/rtos-zbuild mps2/an385 $S/z_dec /var/tmp/rtos/E3/z_dec_$n -- $a >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E3/z_dec_$n/zephyr/zephyr.elf 30; done'
```

```text
           FLASH:       14168 B         4 MB      0.34%
             RAM:        6416 B         4 MB      0.15%
IRQ -> _isr_wrapper (hook) min=360 avg=360 max=360 ns
wrapper -> my ISR line 1   min=520 avg=520 max=520 ns
my ISR: clear + k_sem_give min=12320 avg=12320 max=12320 ns
ISR return -> wrapper exit min=480 avg=480 max=480 ns
exit -> PendSV entry       min=880 avg=880 max=880 ns
PendSV: swap -> switched_in min=1360 avg=1360 max=1360 ns
-> k_sem_take returns      min=640 avg=640 max=640 ns
TOTAL IRQ -> thread        min=16560 avg=16560 max=16560 ns
== notrace
TOTAL IRQ -> thread        min=14080 avg=14080 max=14080 ns
== ts_off
IRQ -> _isr_wrapper (hook) min=360 avg=360 max=360 ns
wrapper -> my ISR line 1   min=520 avg=520 max=520 ns
my ISR: clear + k_sem_give min=3560 avg=3560 max=3560 ns
ISR return -> wrapper exit min=440 avg=440 max=440 ns
exit -> PendSV entry       min=920 avg=920 max=920 ns
PendSV: swap -> switched_in min=1360 avg=1360 max=1360 ns
-> k_sem_take returns      min=640 avg=640 max=640 ns
TOTAL IRQ -> thread        min=7800 avg=7800 max=7800 ns
== ts_off_notrace
TOTAL IRQ -> thread        min=5320 avg=5320 max=5320 ns
```

출력에서 볼 것:

- **Zephyr 기본 설정의 비용은 거의 전부 한 구간에 있다**: `k_sem_give` 12.32 µs. `CONFIG_TIMESLICING=n`이면 3.56 µs로 떨어진다(8.76 µs 차이). 소스를 따라가면 `k_sem_give()` → `z_ready_thread()` → `ready_thread()` → `update_cache()`(kernel/sched.c)가 선점할 스레드를 고르면서 `z_reset_time_slice()`(kernel/timeslicing.c)를 부르고, 그것이 슬라이스 타임아웃을 `z_abort_timeout` → `z_add_timeout`으로 다시 걸면서 SysTick 타이머를 재프로그램한다. A7 · C2에서 "time slice가 비싸다"고 한 그 비용이 **ISR 안에서, 스케줄러 스핀락(=IRQ 잠금)을 잡은 채** 쓰인다는 것이 구간 분해로 보인다(§5의 Zephyr 히스토그램에서 이것이 다른 IRQ의 지연으로 번진다).
- **Zephyr는 결정을 ISR 안에서, FreeRTOS는 PendSV 안에서** 한다. FreeRTOS의 "PendSV → pick"(3.32 µs)에 대응하는 Zephyr 구간은 "PendSV: swap"(1.36 µs)뿐이다 — `z_arm_pendsv`는 이미 정해진 `_kernel.ready_q.cache`로 바꿔 끼우기만 한다(C2 §10).
- Zephyr의 ②③ 쪽은 FreeRTOS보다 길다(0.36 + 0.52 µs vs 0.12 µs). 공용 `_isr_wrapper`가 IPSR로 소프트웨어 ISR 표를 찾아 내 함수를 부르기 때문이다. 이 단계를 없애는 `IRQ_DIRECT_CONNECT`는 E1의 몫이다.
- **tracing 자체가 2.48 µs를 더한다**(16.56 vs 14.08, 7.80 vs 5.32). 훅이 함수 호출이고 `CONFIG_TRACING`이 여러 지점에 훅을 심기 때문이다. Zephyr 쪽 계측 비용이 FreeRTOS(1.0 µs)보다 크다.

같은 측정을 **idle 대신 CPU를 쓰는 prio 5 스레드가 돌 때**도 해 봤다(`-DEXTRA_CFLAGS=-DBUSY`):

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/E3; for v in "busy:-DCONFIG_TRACING=n -DCONFIG_TRACING_USER=n -DEXTRA_CFLAGS=-DBUSY" "busy_trace:-DEXTRA_CFLAGS=-DBUSY" "busy_ts_off:-DCONFIG_TIMESLICING=n -DCONFIG_TRACING=n -DCONFIG_TRACING_USER=n -DEXTRA_CFLAGS=-DBUSY"; do n=${v%%:*}; a=${v#*:}; echo "== $n"; /var/rtoslab/bin/rtos-zbuild mps2/an385 $S/z_dec /var/tmp/rtos/E3/z_dec_$n -- $a >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E3/z_dec_$n/zephyr/zephyr.elf 30; done'
```

```text
== busy
TOTAL IRQ -> thread        min=19480 avg=19480 max=19520 ns
== busy_trace
IRQ -> _isr_wrapper (hook) min=240 avg=240 max=280 ns
wrapper -> my ISR line 1   min=520 avg=520 max=560 ns
my ISR: clear + k_sem_give min=17760 avg=17760 max=17760 ns
ISR return -> wrapper exit min=440 avg=440 max=480 ns
exit -> PendSV entry       min=880 avg=880 max=920 ns
PendSV: swap -> switched_in min=1360 avg=1360 max=1400 ns
-> k_sem_take returns      min=640 avg=640 max=640 ns
TOTAL IRQ -> thread        min=21920 avg=21920 max=21960 ns
== busy_ts_off
TOTAL IRQ -> thread        min=5480 avg=5480 max=5480 ns
```

**무엇이 돌고 있었느냐에 따라 같은 `k_sem_give`가 12.32 µs(idle에서 깨어남) 또는 17.76 µs(prio 5 스레드를 선점)**다. A7의 Zephyr 19.2 µs는 바쁜 스레드가 있던 측정이라 이쪽과 맞는다. 슬라이스를 끄면 두 경우 모두 5.3 ~ 5.5 µs로 모인다. 즉 time slicing은 비용만이 아니라 **"선점당한 쪽이 누구냐"에 따라 달라지는 두 모드(이봉 분포)**를 만든다. 이 정도 세부(정확히 어느 분기가 5.4 µs를 더하는지)는 이 노트에서 명령 추적으로 확인하지 않았다 — C2 §9의 `-d exec` 방법으로 볼 수 있다.

### 4.3 한 장으로 — 누적 막대

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<text x="10.0" y="18.0" font-size="13" text-anchor="start">IRQ → 태스크 지연 분해 (µs, 평균, QEMU icount)</text>
<text x="122.0" y="53.0" font-size="12" text-anchor="end">FreeRTOS 기본</text>
<rect x="130.0" y="34.0" width="2.9" height="26.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="132.9" y="34.0" width="80.6" height="26.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="173.2" y="51.0" font-size="11" text-anchor="middle">3.36</text>
<rect x="213.5" y="34.0" width="11.5" height="26.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
<rect x="225.0" y="34.0" width="79.7" height="26.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<text x="264.9" y="51.0" font-size="11" text-anchor="middle">3.32</text>
<rect x="304.7" y="34.0" width="48.0" height="26.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
<text x="328.7" y="51.0" font-size="11" text-anchor="middle">2.00</text>
<text x="358.7" y="46.0" font-size="11" text-anchor="start">계: 9.28</text>
<text x="358.7" y="59.0" font-size="11" text-anchor="start">계측 없이: 8.32</text>
<text x="122.0" y="97.0" font-size="12" text-anchor="end">FreeRTOS lean</text>
<rect x="130.0" y="78.0" width="2.9" height="26.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="132.9" y="78.0" width="80.6" height="26.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="173.2" y="95.0" font-size="11" text-anchor="middle">3.36</text>
<rect x="213.5" y="78.0" width="11.5" height="26.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
<rect x="225.0" y="78.0" width="43.2" height="26.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<text x="246.6" y="95.0" font-size="11" text-anchor="middle">1.80</text>
<rect x="268.2" y="78.0" width="48.0" height="26.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
<text x="292.2" y="95.0" font-size="11" text-anchor="middle">2.00</text>
<text x="322.2" y="90.0" font-size="11" text-anchor="start">계: 7.76</text>
<text x="322.2" y="103.0" font-size="11" text-anchor="start">계측 없이: 6.84</text>
<text x="122.0" y="141.0" font-size="12" text-anchor="end">Zephyr 기본</text>
<rect x="130.0" y="122.0" width="21.1" height="26.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="151.1" y="122.0" width="295.7" height="26.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="299.0" y="139.0" font-size="11" text-anchor="middle">12.32</text>
<rect x="446.8" y="122.0" width="11.5" height="26.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
<rect x="458.3" y="122.0" width="53.8" height="26.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<text x="485.2" y="139.0" font-size="11" text-anchor="middle">2.24</text>
<rect x="512.1" y="122.0" width="15.4" height="26.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
<text x="533.4" y="134.0" font-size="11" text-anchor="start">계: 16.56</text>
<text x="533.4" y="147.0" font-size="11" text-anchor="start">계측 없이: 14.08</text>
<text x="122.0" y="185.0" font-size="12" text-anchor="end">Zephyr slice 끔</text>
<rect x="130.0" y="166.0" width="21.1" height="26.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="151.1" y="166.0" width="85.4" height="26.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="193.8" y="183.0" font-size="11" text-anchor="middle">3.56</text>
<rect x="236.6" y="166.0" width="10.6" height="26.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
<rect x="247.1" y="166.0" width="54.7" height="26.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<text x="274.5" y="183.0" font-size="11" text-anchor="middle">2.28</text>
<rect x="301.8" y="166.0" width="15.4" height="26.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
<text x="323.2" y="178.0" font-size="11" text-anchor="start">계: 7.80</text>
<text x="323.2" y="191.0" font-size="11" text-anchor="start">계측 없이: 5.32</text>
<line x1="130.0" y1="210" x2="130.0" y2="214" stroke="currentColor"/><text x="130.0" y="226.0" font-size="11" text-anchor="middle">0</text>
<line x1="178.0" y1="210" x2="178.0" y2="214" stroke="currentColor"/><text x="178.0" y="226.0" font-size="11" text-anchor="middle">2</text>
<line x1="226.0" y1="210" x2="226.0" y2="214" stroke="currentColor"/><text x="226.0" y="226.0" font-size="11" text-anchor="middle">4</text>
<line x1="274.0" y1="210" x2="274.0" y2="214" stroke="currentColor"/><text x="274.0" y="226.0" font-size="11" text-anchor="middle">6</text>
<line x1="322.0" y1="210" x2="322.0" y2="214" stroke="currentColor"/><text x="322.0" y="226.0" font-size="11" text-anchor="middle">8</text>
<line x1="370.0" y1="210" x2="370.0" y2="214" stroke="currentColor"/><text x="370.0" y="226.0" font-size="11" text-anchor="middle">10</text>
<line x1="418.0" y1="210" x2="418.0" y2="214" stroke="currentColor"/><text x="418.0" y="226.0" font-size="11" text-anchor="middle">12</text>
<line x1="466.0" y1="210" x2="466.0" y2="214" stroke="currentColor"/><text x="466.0" y="226.0" font-size="11" text-anchor="middle">14</text>
<line x1="514.0" y1="210" x2="514.0" y2="214" stroke="currentColor"/><text x="514.0" y="226.0" font-size="11" text-anchor="middle">16</text>
<line x1="562.0" y1="210" x2="562.0" y2="214" stroke="currentColor"/><text x="562.0" y="226.0" font-size="11" text-anchor="middle">18</text>
<line x1="130" y1="210" x2="562.0" y2="210" stroke="currentColor"/>
<rect x="20.0" y="242.0" width="12.0" height="12.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="37.0" y="253.0" font-size="12" text-anchor="start">진입·디스패치</text>
<rect x="120.1" y="242.0" width="12.0" height="12.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="137.1" y="253.0" font-size="12" text-anchor="start">ISR 안 커널 호출</text>
<rect x="281.9" y="242.0" width="12.0" height="12.0" fill="#888" fill-opacity="0.55" stroke="#888"/><text x="298.9" y="253.0" font-size="12" text-anchor="start">ISR 퇴장</text>
<rect x="382.0" y="242.0" width="12.0" height="12.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="399.0" y="253.0" font-size="12" text-anchor="start">PendSV·전환</text>
<rect x="506.8" y="242.0" width="12.0" height="12.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="523.8" y="253.0" font-size="12" text-anchor="start">복원→태스크</text>
</svg>
```

그림 4 — **측정 차트**: 예제 5 · 6의 구간 평균(계측을 넣은 빌드). 오른쪽 "계측 없이"는 계측점을 뺀 빌드의 총합이다. 색이 같은 조각끼리 비교하면 된다 — 파랑(진입 · 디스패치)은 Zephyr가 길고, 주황(ISR 안 커널 호출)은 Zephyr 기본이 압도적이며, 초록(PendSV · 전환)은 FreeRTOS 기본이 길다.

이 그림을 25 MHz에서 480 MHz 칩으로 옮기면? 명령 수가 같다고 치면 모든 조각이 약 1/19로 줄어든다(A7의 클럭 환산 표와 같은 계산). 하지만 ②(HW 진입 12사이클 + wait state)는 QEMU에 없던 조각이 새로 붙고, 캐시 미스가 조각마다 흔들림을 더한다. **비율과 구성은 옮겨지고, 절대값은 실칩에서 다시 잰다.**

---

## 5. 지터를 만든다 — 배경 부하 아래의 히스토그램

> 🔁 이 절은 Bare-Metal BM J7과 겹친다 (주인: 이 노트).

§4는 "아무도 방해하지 않을 때"의 경로 길이였다. 실제 펌웨어에서는 그림 1의 빨간 조각(①, ⑦)이 매번 다른 길이로 끼어든다. 이번에는 **원인을 알고 있는 방해꾼 넷**을 일부러 넣고, 그것들이 히스토그램에 어떤 모양으로 찍히는지 본다. 원인을 알고 만든 모양을 한 번 보고 나면, 원인을 모르는 실제 히스토그램을 읽을 수 있다.

### 5.1 실험 설계 — 방해꾼 네 개, 원인마다 다른 모양을 예상한다

| 방해꾼 | 구현 | 그림 1의 조각 | 예상 모양 |
|---|---|---|---|
| CS15 | 우선순위 1 태스크(lo)가 `taskENTER_CRITICAL` 안에서 15 µs, 그 밖에서 20 ~ 120 µs(난수) | ① 마스킹 | 기본값에서 +15 µs까지 **평평한 선반**. 면적 ≈ 듀티(15 / 85 ≈ 18%) |
| DT6 | DUALTIMER ISR(0x20, TIMER0보다 높음) 6 µs, 주기 = TIMER0의 정확히 2배, TIMER0보다 2 µs 먼저 | ① 상위 ISR | 표본 절반이 **같은 양만큼** 밀린 **두 번째 날카로운 봉우리**(6 − 2 = 약 4 µs 오른쪽) |
| tick | 1 kHz SysTick(0xFF). 안에서 `xTaskIncrementTick`을 BASEPRI 올리고 돈다 | ① 커널 마스킹 | 드문 짧은 선반(수 µs, 0.3% 안팎) |
| med25 | 우선순위 4 태스크(측정 대상 3보다 높음)가 tick마다 깨어 25 µs 일함 | ⑦ 상위 태스크 | **태스크 쪽 히스토그램에만** 생기는 +25 µs 이상의 꼬리(약 2.5%) |

설계 포인트 세 가지:

- **DT6의 주기를 TIMER0의 정수배로** 맞춘 것은 일부러다. 실무에서 "IMU data-ready와 DShot 타이머가 같은 1 kHz 기준에서 나온다"처럼 **두 이벤트가 같은 클럭에서 나오면 위상이 고정**되어, 간섭이 매번 같은 양으로 들어온다. 이것이 날카로운 봉우리를 만든다.
- **CS15의 쉬는 시간을 난수로** 둔 것도 일부러다. C7 §6에서 고정 간격으로 했더니 IRQ가 늘 같은 위상(잠금 밖)에만 떨어져 최댓값을 못 봤다. 위상이 무작위면 0 ~ 15 µs가 고르게 찍힌다.
- 각 방해꾼은 **자기가 돈 마지막 구간 [t0, t1]**을 FPGAIO 시계로 남긴다. 측정 ISR은 "IRQ가 올라간 시각 a = 지금 − 경과"를 계산해, a부터 지금까지와 겹친 방해꾼을 비트마스크로 기록한다(§6.2의 원인 태깅). SysTick은 앱이 `SysTick_Handler`를 정의해 포트의 `xPortSysTickHandler`를 감싸게 했다(`WRAP_TICK`, 앞의 FreeRTOSConfig.h에서 포트 쪽 이름 바꿈 매크로를 지운다).

### 5.2 예제 7 — FreeRTOS: 두 단계 히스토그램 + 원인 태깅

```c
/* ex7 — FreeRTOS: IRQ->ISR and IRQ->task histograms under load, each sample tagged by cause */
#include "FreeRTOS.h"
#include "task.h"
#include "e3.h"
#ifndef OFF
#define OFF 0                                         /* ablation: 1 no CS, 2 no DT, 4 med idle, 8 no med */
#endif
#define N 4000
#define BIN 25u                                       /* 1 us bins */
volatile uint32_t e3_armed, e3_s4;
void *volatile e3_hi;
typedef struct { volatile uint32_t t0, t1; } span_t; /* last run interval of each blocker */
static span_t cs, dt, tk, md;
static hist_t h_isr, h_tsk;
static uint32_t cnt[2][16], cmax[2][16];             /* [stage][cause bitmask] */
static const char *cn[4] = { "CS15 ", "DT6 ", "tick ", "med25 " };
static volatile uint32_t a_cyc;                      /* when TIMER0 raised the IRQ (FPGAIO time) */
static int hit(span_t *s, uint32_t from, uint32_t to)  /* did [t0,t1] overlap [from,to]? */
{
    uint32_t end = (int32_t)(s->t1 - s->t0) < 0 ? to : s->t1 + 12;  /* t1 < t0: still running */
    return (int32_t)(s->t0 - to) < 0 && (int32_t)(end - from) > 0;
}
static void tag(int st, uint32_t to, uint32_t lat)
{
    int m = hit(&cs, a_cyc, to) | hit(&dt, a_cyc, to) << 1 | hit(&tk, a_cyc, to) << 2
          | (st && hit(&md, a_cyc, to)) << 3;
    cnt[st][m]++; if (lat > cmax[st][m]) cmax[st][m] = lat;
}
void TIMER0_IRQHandler(void)                          /* prio 0x40 */
{
    uint32_t lat = t0_elapsed(), e = CYC(); a_cyc = e - lat;
    TIMER_INTCLR(TIMER0_BASE) = 1;
    if (h_isr.n < N) { h_add(&h_isr, lat, BIN); tag(0, e, lat); }
    BaseType_t w = pdFALSE;
    vTaskNotifyGiveFromISR((TaskHandle_t)e3_hi, &w);
    portYIELD_FROM_ISR(w);
}
void DUALTIMER_IRQHandler(void) { dt.t0 = CYC(); DT_INTCLR = 1; spin_cyc(150); dt.t1 = CYC(); } /* 0x20 */
extern void xPortSysTickHandler(void);
void SysTick_Handler(void) { tk.t0 = CYC(); xPortSysTickHandler(); tk.t1 = CYC(); }
static void lo_task(void *a) { (void)a; for (;;) {    /* prio 1: 15 us critical sections */
    if (!(OFF & 1)) { taskENTER_CRITICAL(); cs.t0 = CYC(); spin_cyc(375); cs.t1 = CYC(); taskEXIT_CRITICAL(); }
    spin_cyc(500 + rnd(2500)); } }
static void med_task(void *a) { (void)a; for (;;) {   /* prio 4: 25 us of work every tick */
    vTaskDelay(1); md.t0 = CYC(); if (!(OFF & 4)) spin_cyc(625); md.t1 = CYC(); } }
static void hi_task(void *a)                          /* prio 3: the task we measure */
{
    (void)a;
    while (HN(h_tsk) < N) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t lat = t0_elapsed();
        h_add(&h_tsk, lat, BIN); tag(1, CYC(), lat);
    }
    t0_stop();
    for (int b = 0; b < NBIN; b++)
        if (h_isr.bin[b] + h_tsk.bin[b]) board_printf_raw("bin %u us: isr=%u task=%u\n", b, h_isr.bin[b], h_tsk.bin[b]);
    for (int s = 0; s < 2; s++) {
        hist_t *h = s ? &h_tsk : &h_isr;
        board_printf_raw("%s n=%u min=%u avg=%u max=%u ns\n", s ? "IRQ->task" : "IRQ->ISR ", h->n,
                         NS(h->min), NS((uint32_t)(h->sum / h->n)), NS(h->max));
        for (int m = 0; m < 16; m++) if (cnt[s][m]) {
            board_printf_raw("  n=%u max=%u ns : ", cnt[s][m], NS(cmax[s][m]));
            for (int b = 0; b < 4; b++) if (m >> b & 1) board_puts(cn[b]);
            board_puts(m ? "\n" : "(no tagged cause)\n");
        }
    }
    board_exit(0);
}
int main(void)
{
    TaskHandle_t t;
    xTaskCreate(hi_task, "hi", 512, NULL, 3, &t); e3_hi = t;
    if (!(OFF & 8)) xTaskCreate(med_task, "med", 256, NULL, 4, NULL);   /* 8 = no med task at all */
    xTaskCreate(lo_task, "lo", 256, NULL, 1, NULL);
    t0_start(12500 + 37, 0x40);
    if (!(OFF & 2)) dt_start(2 * (12500 + 37), 12500 + 37 - 50, 0x20);  /* 2 us before every 2nd T0 */
    vTaskStartScheduler();
    return 1;
}
```

(76줄 — 출력 부분까지 한 파일에 넣느라 70줄을 조금 넘었다.) `hit()`의 "`t1 < t0`이면 아직 도는 중" 처리는 첫 시도에 없었다. 그때는 tick 태그가 ISR 쪽에 **한 번도** 안 나왔다 — TIMER0(0x40)이 SysTick(0xFF)을 선점하면 SysTick의 `t1`은 아직 **지난번** 값이라 겹침 검사에서 빠졌기 때문이다. 원인 태깅 코드는 "선점당해 아직 안 끝난 방해꾼"을 다뤄야 한다.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/E3; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/frt/ex7_load.c OUT=/var/tmp/rtos/E3/ex7 EXTRA_CFLAGS="-I$S/inc -DWRAP_TICK" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E3/ex7/app.elf 120 | tee /var/tmp/rtos/E3/ex7.out'
```

```text
bin 0 us: isr=1640 task=0
bin 1 us: isr=36 task=0
bin 2 us: isr=24 task=0
bin 3 us: isr=23 task=0
bin 4 us: isr=1652 task=0
bin 5 us: isr=47 task=0
...
bin 11 us: isr=47 task=0
bin 12 us: isr=45 task=1550
bin 13 us: isr=43 task=26
...
bin 16 us: isr=22 task=23
bin 17 us: isr=27 task=1604
bin 18 us: isr=23 task=41
...
bin 32 us: isr=0 task=29
bin 33 us: isr=0 task=3
...
bin 53 us: isr=0 task=24
...
bin 57 us: isr=0 task=29
...
IRQ->ISR  n=4000 min=120 avg=4000 max=20920 ns
  n=1629 max=2760 ns : (no tagged cause)
  n=361 max=16080 ns : CS15 
  n=1674 max=7600 ns : DT6 
  n=318 max=20920 ns : CS15 DT6 
  n=10 max=3640 ns : tick 
  n=8 max=8480 ns : DT6 tick 
IRQ->task n=4000 min=12280 avg=17400 max=73920 ns
  n=1547 max=17680 ns : (no tagged cause)
  n=352 max=29080 ns : CS15 
  n=1585 max=22520 ns : DT6 
  n=304 max=34720 ns : CS15 DT6 
  n=59 max=44240 ns : med25 
  n=56 max=50040 ns : DT6 med25 
  n=33 max=53240 ns : tick med25 
  n=9 max=68240 ns : CS15 tick med25 
  n=41 max=59360 ns : DT6 tick med25 
  n=14 max=73920 ns : CS15 DT6 tick med25 
[board_exit] code=0
```

(54개 칸 중 일부만 남기고 `...`로 줄였다. 전체는 그림 5가 그린다. 64 µs를 넘는 표본은 칸 밖이라 그림에 없다 — 최댓값 73.92 µs가 그중 하나다.)

출력에서 볼 것 — 예상한 모양이 그대로 나왔다:

- **IRQ → ISR**: 0 µs 칸(1640)과 **4 µs 칸(1652)**에 거의 같은 높이의 두 봉우리. 두 번째 봉우리는 DT6 태그 1674개와 맞는다 — TIMER0 두 번에 한 번, 2 µs 먼저 시작한 6 µs짜리 상위 ISR이 끝나기를 기다린 표본이다. 그 사이와 오른쪽(1 ~ 20 µs)에 칸마다 20 ~ 50개씩 깔린 **평평한 선반**이 CS15다(태그 361 + 318). 최댓값 20.92 µs는 CS15와 DT6가 **겹친** 표본에서 나왔다.
- **IRQ → 태스크**: 같은 두 봉우리가 12 µs · 17 µs로 **통째로 오른쪽으로 12 µs 옮겨** 나타난다 — ISR 이후의 경로(④~⑧)가 거의 일정하다는 뜻이다. 그리고 ISR 쪽에는 없던 **긴 꼬리(33 ~ 64 µs 이상)**가 생긴다. 태그는 전부 med25가 들어간 조합이다 — ⑦ 상위 태스크 간섭은 **태스크 쪽에만** 나타난다. 같은 표본을 두 단계에서 재면 원인의 종류(①인지 ⑦인지)가 갈린다.
- 꼬리 끝의 작은 무리(53, 57 µs 칸의 24, 29개)는 "tick이 med를 깨우는 순간"에 IRQ가 떨어진 표본이다(tick + med25, 거기에 DT6가 더해지면 +4 µs). **꼬리의 작은 봉우리 = 원인 조합 하나**다.
- **평균 17.40 µs는 아무것도 말하지 않는다**: 최댓값(73.92)의 1/4이고, 실제로 표본이 몰린 두 봉우리(12, 17) 어느 쪽과도 같지 않다.
- "no tagged cause"가 ISR 쪽에서도 최대 2.76 µs까지 있다. 태깅하지 않은 원인이 있다는 신호다 — §6.3에서 찾는다.

### 5.3 예제 8 — Zephyr: 같은 부하, 같은 태깅

무엇을 확인하나: 예제 7과 같은 네 방해꾼을 Zephyr v4.4.2로 옮긴다. CS15는 `irq_lock()`, DT6는 `IRQ_CONNECT(10, 1, ...)`(TIMER0의 2보다 높음), med는 prio 1 스레드가 `k_sleep(K_TICKS(1))`, 측정 대상은 prio 2 스레드다. tick은 1 kHz(`CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`)로 맞췄다(이 보드 기본은 100 Hz, tickless). SysTick 핸들러 `sys_clock_isr`는 벡터 테이블(`vector_table.S`)이 직접 가리키므로, 링커의 `--wrap`으로 감쌌다(`zephyr_ld_options(-Wl,--wrap=sys_clock_isr)` — 벡터 테이블 쪽 참조가 `__wrap_sys_clock_isr`로 간다).

```text
# z_load/CMakeLists.txt (마지막 줄만 추가)
zephyr_ld_options(-Wl,--wrap=sys_clock_isr)
# z_load/prj.conf
CONFIG_BOOT_BANNER=n
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000
```

```c
/* ex8 — Zephyr v4.4.2: the same load as ex7 (irq_lock CS, higher ISR, higher thread), tagged */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>
#include "z3.h"
#define N 4000
#define BIN 25u
static span_t cs, dt, md, tk;
static hist_t h_isr, h_tsk;
static uint32_t cnt[2][16], cmax[2][16];
static const char *cn[4] = { "CS15 ", "DT6 ", "med25 ", "tick " };
static volatile uint32_t a_cyc;
static K_SEM_DEFINE(sem, 0, 1);
static void tag(int st, uint32_t to, uint32_t lat)
{
    int m = hit(&cs, a_cyc, to) | hit(&dt, a_cyc, to) << 1 | (st && hit(&md, a_cyc, to)) << 2
          | hit(&tk, a_cyc, to) << 3;
    cnt[st][m]++; if (lat > cmax[st][m]) cmax[st][m] = lat;
}
static void t0_isr(const void *p)                      /* IRQ_CONNECT prio 2 */
{
    uint32_t lat = t0_elapsed(), e = CYC(); a_cyc = e - lat;
    REG32(T0 + 0xC) = 1;
    if (h_isr.n < N) { h_add(&h_isr, lat, BIN); tag(0, e, lat); }
    k_sem_give(&sem);
}
static void dt_isr(const void *p) { dt.t0 = CYC(); REG32(DT + 0xC) = 1; spin_cyc(150); dt.t1 = CYC(); } /* prio 1 */
void __real_sys_clock_isr(void);                             /* -Wl,--wrap=sys_clock_isr */
void __wrap_sys_clock_isr(void) { tk.t0 = CYC(); __real_sys_clock_isr(); tk.t1 = CYC(); }
static void lo_fn(void *a, void *b, void *c) { for (;;) {     /* prio 5: 15 us irq_lock sections */
    unsigned int key = irq_lock(); cs.t0 = CYC(); spin_cyc(375); cs.t1 = CYC(); irq_unlock(key);
    spin_cyc(500 + rnd(2500)); } }
static void med_fn(void *a, void *b, void *c) { for (;;) {    /* prio 1: 25 us every tick */
    k_sleep(K_TICKS(1)); md.t0 = CYC(); spin_cyc(625); md.t1 = CYC(); } }
static void hi_fn(void *a, void *b, void *c)                  /* prio 2: measured thread */
{
    k_sem_reset(&sem);                                        /* drop the stale give from before we started */
    while (HN(h_tsk) < N) {
        k_sem_take(&sem, K_FOREVER);
        uint32_t lat = t0_elapsed();
        h_add(&h_tsk, lat, BIN); tag(1, CYC(), lat);
    }
    REG32(T0) = 0;
    for (int b = 0; b < NBIN; b++)
        if (h_isr.bin[b] + h_tsk.bin[b]) printk("bin %u us: isr=%u task=%u\n", b, h_isr.bin[b], h_tsk.bin[b]);
    for (int s = 0; s < 2; s++) {
        hist_t *h = s ? &h_tsk : &h_isr;
        printk("%s n=%u min=%u avg=%u max=%u ns (over=%u)\n", s ? "IRQ->thread" : "IRQ->ISR   ", h->n,
               NS(h->min), NS((uint32_t)(h->sum / h->n)), NS(h->max), h->over);
        for (int m = 0; m < 16; m++) if (cnt[s][m]) {
            printk("  n=%u max=%u ns : ", cnt[s][m], NS(cmax[s][m]));
            for (int b = 0; b < 4; b++) if (m >> b & 1) printk("%s", cn[b]);
            printk(m ? "\n" : "(no tagged cause)\n");
        }
    }
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
}
K_THREAD_DEFINE(hi, 1024, hi_fn, NULL, NULL, NULL, 2, 0, 10);
K_THREAD_DEFINE(med, 512, med_fn, NULL, NULL, NULL, 1, 0, 10);
K_THREAD_DEFINE(lo, 512, lo_fn, NULL, NULL, NULL, 5, 0, 10);
int main(void)
{
    IRQ_CONNECT(8, 2, t0_isr, NULL, 0);  irq_enable(8);
    IRQ_CONNECT(10, 1, dt_isr, NULL, 0); irq_enable(10);
    REG32(T0) = 0; REG32(T0 + 8) = 12500 + 37 - 1; REG32(T0 + 4) = 12500 + 37 - 1; REG32(T0) = 1 | 8;
    REG32(DT + 8) = 0; REG32(DT) = 12500 + 37 - 50 - 1; REG32(DT + 0x18) = 2 * (12500 + 37) - 1;
    REG32(DT + 8) = 0x80u | 0x40u | 0x20u | 0x02u;
    return 0;
}
```

`k_sem_reset()` 줄도 첫 시도에는 없었다. 그때 IRQ → 스레드 최댓값이 **476.4 µs**(주기 500 µs에 가까운 값)로 나왔다. 스레드들이 10 ms 늦게 시작하도록(`K_THREAD_DEFINE`의 마지막 인자) 해 둔 사이 TIMER0 ISR이 이미 세마포어를 줘 버려서, 첫 `k_sem_take`가 **오래된 give**로 즉시 돌아와 "마지막 IRQ로부터 경과"를 아무 값이나 읽은 것이다. **혼자 멀리 떨어진 점 하나는 시스템이 아니라 측정의 버그일 때가 많다.**

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/E3; /var/rtoslab/bin/rtos-zbuild mps2/an385 $S/z_load /var/tmp/rtos/E3/z_load && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E3/z_load/zephyr/zephyr.elf 120 | tee /var/tmp/rtos/E3/ex8.out | grep -v "^bin"
/var/rtoslab/bin/rtos-zbuild mps2/an385 $S/z_load /var/tmp/rtos/E3/z_load_tsoff -- -DCONFIG_TIMESLICING=n >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E3/z_load_tsoff/zephyr/zephyr.elf 120 | tee /var/tmp/rtos/E3/ex8_tsoff.out | grep -v "^bin"'
```

```text
           FLASH:       15492 B         4 MB      0.37%
             RAM:        9160 B         4 MB      0.22%
IRQ->ISR    n=4000 min=360 avg=5480 max=40680 ns (over=0)
  n=1532 max=21960 ns : (no tagged cause)
  n=330 max=15840 ns : CS15 
  n=1518 max=25600 ns : DT6 
  n=293 max=21320 ns : CS15 DT6 
  n=122 max=19720 ns : tick 
  n=16 max=32560 ns : CS15 tick 
  n=173 max=25840 ns : DT6 tick 
  n=16 max=40680 ns : CS15 DT6 tick 
IRQ->thread n=4000 min=11600 avg=27360 max=114440 ns (over=409)
  n=1481 max=37240 ns : (no tagged cause)
  n=328 max=31200 ns : CS15 
  n=1455 max=40880 ns : DT6 
  n=293 max=36680 ns : CS15 DT6 
  n=17 max=59920 ns : med25 
  n=26 max=81000 ns : DT6 med25 
  n=155 max=99760 ns : med25 tick 
  n=19 max=106320 ns : CS15 med25 tick 
  n=207 max=105440 ns : DT6 med25 tick 
  n=19 max=114440 ns : CS15 DT6 med25 tick 
IRQ->ISR    n=4000 min=360 avg=4640 max=29880 ns (over=0)
  n=1622 max=8600 ns : (no tagged cause)
  n=367 max=15800 ns : CS15 
  n=1650 max=14240 ns : DT6 
  n=332 max=21280 ns : CS15 DT6 
  n=8 max=7760 ns : tick 
  n=3 max=23720 ns : CS15 tick 
  n=16 max=15720 ns : DT6 tick 
  n=2 max=29880 ns : CS15 DT6 tick 
IRQ->thread n=4000 min=5480 avg=14480 max=75240 ns (over=4)
  n=1588 max=17760 ns : (no tagged cause)
  n=365 max=25040 ns : CS15 
  n=1614 max=23400 ns : DT6 
  n=334 max=30520 ns : CS15 DT6 
  n=25 max=44640 ns : med25 
  n=25 max=52360 ns : DT6 med25 
  n=17 max=55440 ns : med25 tick 
  n=5 max=68960 ns : CS15 med25 tick 
  n=25 max=61000 ns : DT6 med25 tick 
  n=2 max=75240 ns : CS15 DT6 med25 tick 
```

(앞 블록이 기본 설정, 뒤 블록이 `CONFIG_TIMESLICING=n`. 두 번째 빌드의 FLASH/RAM 줄은 출력을 버려서 없다.)

출력에서 볼 것:

- **슬라이스를 끈 Zephyr는 FreeRTOS와 같은 모양이다**: CS15 선반(태그 367 + 332), DT6 봉우리(1650, 칸으로는 5 µs — `_isr_wrapper` 때문에 FreeRTOS보다 1 µs 오른쪽), med 조합의 꼬리, 최댓값은 네 원인이 겹친 2개 표본(75.24 µs). 같은 방해꾼은 같은 RTOS-독립적 모양을 만든다.
- **슬라이스를 켠 기본 설정은 다르다**. 태깅하지 않은 ISR 지연이 최대 **21.96 µs**(끈 쪽 8.6 µs), tick 태그가 122개(끈 쪽 8개), 스레드 쪽 64 µs를 넘는 표본이 409개(10%), 최댓값 114.44 µs. §4.2에서 본 "`k_sem_give` 안의 슬라이스 재설정"은 스케줄러 스핀락(= `irq_lock`)을 잡은 채 SysTick을 다시 프로그램하는 일이다. 이것이 med · lo · hi 사이의 **모든 전환**에서 일어나고, 그동안 TIMER0도 막힌다. 슬라이스 비용은 자기 경로만이 아니라 **다른 IRQ의 ①(마스킹)**로 번진다 — 구간 분해(§4)와 히스토그램(§5)이 같은 범인을 두 방향에서 가리킨다.
- 스레드 쪽 최솟값이 11.60(기본) → **5.48 µs**(끔)로, 슬라이스가 기본 경로에서 6 µs를 먹는다는 §4.2 결론과 같다.
- 실무 판단: 같은 우선순위 스레드끼리 시간을 나눌 필요가 없는 제어 펌웨어라면 `CONFIG_TIMESLICING=n`을 검토한다. 필요하면 `CONFIG_TIMESLICE_PRIORITY`로 슬라이스를 받는 우선순위 범위를 좁힌다(H 모듈).

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<text x="60.0" y="22.0" font-size="13" text-anchor="start">FreeRTOS V11.3.2 (ex7)</text>
<line x1="60" y1="170" x2="660" y2="170" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="170" stroke="currentColor"/>
<line x1="56" y1="170.0" x2="60" y2="170.0" stroke="currentColor"/><text x="53.0" y="174.0" font-size="11" text-anchor="end">1</text>
<line x1="56" y1="132.2" x2="60" y2="132.2" stroke="currentColor"/><text x="53.0" y="136.2" font-size="11" text-anchor="end">10</text>
<line x1="56" y1="94.3" x2="60" y2="94.3" stroke="currentColor"/><text x="53.0" y="98.3" font-size="11" text-anchor="end">100</text>
<line x1="56" y1="56.5" x2="60" y2="56.5" stroke="currentColor"/><text x="53.0" y="60.5" font-size="11" text-anchor="end">1000</text>
<rect x="60.0" y="46.9" width="4.2" height="123.1" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="69.4" y="105.0" width="4.2" height="65.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="78.8" y="111.2" width="4.2" height="58.8" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="88.1" y="111.8" width="4.2" height="58.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="97.5" y="46.8" width="4.2" height="123.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="106.9" y="101.0" width="4.2" height="69.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="116.2" y="102.7" width="4.2" height="67.3" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="125.6" y="99.7" width="4.2" height="70.3" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="135.0" y="99.1" width="4.2" height="70.9" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="144.4" y="101.3" width="4.2" height="68.7" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="153.8" y="99.4" width="4.2" height="70.6" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="163.1" y="101.0" width="4.2" height="69.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="172.5" y="101.6" width="4.2" height="68.4" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="177.2" y="47.8" width="4.2" height="122.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="181.9" y="102.3" width="4.2" height="67.7" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="186.6" y="110.0" width="4.2" height="60.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="191.2" y="104.6" width="4.2" height="65.4" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="195.9" y="111.2" width="4.2" height="58.8" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="200.6" y="101.0" width="4.2" height="69.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="205.3" y="111.8" width="4.2" height="58.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="210.0" y="112.5" width="4.2" height="57.5" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="214.7" y="111.8" width="4.2" height="58.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="219.4" y="109.4" width="4.2" height="60.6" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="224.1" y="47.3" width="4.2" height="122.7" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="228.8" y="111.8" width="4.2" height="58.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="233.4" y="103.0" width="4.2" height="67.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="238.1" y="116.4" width="4.2" height="53.6" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="242.8" y="99.7" width="4.2" height="70.3" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="247.5" y="110.0" width="4.2" height="60.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="252.2" y="99.4" width="4.2" height="70.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="261.6" y="99.4" width="4.2" height="70.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="270.9" y="92.9" width="4.2" height="77.1" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="280.3" y="101.6" width="4.2" height="68.4" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="289.7" y="99.4" width="4.2" height="70.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="299.1" y="102.0" width="4.2" height="68.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="308.4" y="103.4" width="4.2" height="66.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="317.8" y="102.7" width="4.2" height="67.3" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="327.2" y="106.8" width="4.2" height="63.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="336.6" y="108.8" width="4.2" height="61.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="345.9" y="110.0" width="4.2" height="60.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="355.3" y="113.2" width="4.2" height="56.8" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="364.7" y="108.3" width="4.2" height="61.7" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="374.1" y="142.8" width="4.2" height="27.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="383.4" y="138.4" width="4.2" height="31.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="458.4" y="142.8" width="4.2" height="27.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="467.8" y="159.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="477.2" y="159.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="495.9" y="159.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="505.3" y="129.9" width="4.2" height="40.1" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="514.7" y="135.0" width="4.2" height="35.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="524.1" y="138.4" width="4.2" height="31.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="533.4" y="142.8" width="4.2" height="27.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="542.8" y="149.0" width="4.2" height="21.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="552.2" y="149.0" width="4.2" height="21.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="561.6" y="111.2" width="4.2" height="58.8" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="570.9" y="142.8" width="4.2" height="27.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="589.7" y="159.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="599.1" y="108.3" width="4.2" height="61.7" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="608.4" y="159.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="617.8" y="159.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="636.6" y="149.0" width="4.2" height="21.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="645.9" y="142.8" width="4.2" height="27.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="655.3" y="149.0" width="4.2" height="21.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="60.0" y="184.0" font-size="11" text-anchor="middle">0</text>
<text x="135.0" y="184.0" font-size="11" text-anchor="middle">8</text>
<text x="210.0" y="184.0" font-size="11" text-anchor="middle">16</text>
<text x="285.0" y="184.0" font-size="11" text-anchor="middle">24</text>
<text x="360.0" y="184.0" font-size="11" text-anchor="middle">32</text>
<text x="435.0" y="184.0" font-size="11" text-anchor="middle">40</text>
<text x="510.0" y="184.0" font-size="11" text-anchor="middle">48</text>
<text x="585.0" y="184.0" font-size="11" text-anchor="middle">56</text>
<text x="660.0" y="184.0" font-size="11" text-anchor="middle">64</text>
<text x="60.0" y="217.0" font-size="13" text-anchor="start">Zephyr v4.4.2, slice 끔 (ex8)</text>
<line x1="60" y1="365" x2="660" y2="365" stroke="currentColor"/><line x1="60" y1="225" x2="60" y2="365" stroke="currentColor"/>
<line x1="56" y1="365.0" x2="60" y2="365.0" stroke="currentColor"/><text x="53.0" y="369.0" font-size="11" text-anchor="end">1</text>
<line x1="56" y1="327.2" x2="60" y2="327.2" stroke="currentColor"/><text x="53.0" y="331.2" font-size="11" text-anchor="end">10</text>
<line x1="56" y1="289.3" x2="60" y2="289.3" stroke="currentColor"/><text x="53.0" y="293.3" font-size="11" text-anchor="end">100</text>
<line x1="56" y1="251.5" x2="60" y2="251.5" stroke="currentColor"/><text x="53.0" y="255.5" font-size="11" text-anchor="end">1000</text>
<rect x="60.0" y="242.0" width="4.2" height="123.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="69.4" y="301.8" width="4.2" height="63.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="78.8" y="309.7" width="4.2" height="55.3" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="88.1" y="304.4" width="4.2" height="60.6" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="97.5" y="302.8" width="4.2" height="62.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="106.9" y="242.0" width="4.2" height="123.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="111.6" y="319.5" width="4.2" height="45.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="116.2" y="296.0" width="4.2" height="69.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="125.6" y="297.3" width="4.2" height="67.7" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="135.0" y="292.8" width="4.2" height="72.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="144.4" y="298.4" width="4.2" height="66.6" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="149.1" y="242.5" width="4.2" height="122.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="153.8" y="297.7" width="4.2" height="67.3" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="158.4" y="303.3" width="4.2" height="61.7" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="163.1" y="295.0" width="4.2" height="70.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="167.8" y="303.8" width="4.2" height="61.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="172.5" y="296.0" width="4.2" height="69.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="177.2" y="305.0" width="4.2" height="60.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="181.9" y="296.6" width="4.2" height="68.4" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="186.6" y="305.0" width="4.2" height="60.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="191.2" y="297.3" width="4.2" height="67.7" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="195.9" y="305.6" width="4.2" height="59.4" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="200.6" y="294.1" width="4.2" height="70.9" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="205.3" y="242.2" width="4.2" height="122.8" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="210.0" y="306.8" width="4.2" height="58.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="214.7" y="299.2" width="4.2" height="65.8" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="219.4" y="305.6" width="4.2" height="59.4" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="224.1" y="294.1" width="4.2" height="70.9" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="228.8" y="303.3" width="4.2" height="61.7" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="233.4" y="296.0" width="4.2" height="69.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="238.1" y="300.4" width="4.2" height="64.6" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="242.8" y="295.3" width="4.2" height="69.7" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="247.5" y="305.6" width="4.2" height="59.4" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="252.2" y="298.0" width="4.2" height="67.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="256.9" y="315.5" width="4.2" height="49.5" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="261.6" y="294.4" width="4.2" height="70.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="266.2" y="344.0" width="4.2" height="21.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="270.9" y="295.3" width="4.2" height="69.7" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="275.6" y="354.5" width="4.2" height="10.5" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="280.3" y="300.4" width="4.2" height="64.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="289.7" y="289.3" width="4.2" height="75.7" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="299.1" y="304.4" width="4.2" height="60.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="308.4" y="305.0" width="4.2" height="60.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="317.8" y="305.0" width="4.2" height="60.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="327.2" y="301.3" width="4.2" height="63.7" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="331.9" y="354.5" width="4.2" height="10.5" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<rect x="336.6" y="301.3" width="4.2" height="63.7" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="345.9" y="307.5" width="4.2" height="57.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="355.3" y="354.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="364.7" y="337.8" width="4.2" height="27.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="383.4" y="354.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="392.8" y="354.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="477.2" y="354.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="505.3" y="344.0" width="4.2" height="21.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="514.7" y="344.0" width="4.2" height="21.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="542.8" y="344.0" width="4.2" height="21.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="552.2" y="333.4" width="4.2" height="31.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="561.6" y="337.8" width="4.2" height="27.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="570.9" y="354.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="580.3" y="319.5" width="4.2" height="45.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="589.7" y="354.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="599.1" y="344.0" width="4.2" height="21.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="608.4" y="337.8" width="4.2" height="27.2" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="617.8" y="344.0" width="4.2" height="21.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="627.2" y="324.9" width="4.2" height="40.1" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="636.6" y="354.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<rect x="645.9" y="354.5" width="4.2" height="10.5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="60.0" y="379.0" font-size="11" text-anchor="middle">0</text>
<text x="135.0" y="379.0" font-size="11" text-anchor="middle">8</text>
<text x="210.0" y="379.0" font-size="11" text-anchor="middle">16</text>
<text x="285.0" y="379.0" font-size="11" text-anchor="middle">24</text>
<text x="360.0" y="379.0" font-size="11" text-anchor="middle">32</text>
<text x="435.0" y="379.0" font-size="11" text-anchor="middle">40</text>
<text x="510.0" y="379.0" font-size="11" text-anchor="middle">48</text>
<text x="585.0" y="379.0" font-size="11" text-anchor="middle">56</text>
<text x="660.0" y="379.0" font-size="11" text-anchor="middle">64</text>
<text x="370.0" y="410.0" font-size="12" text-anchor="middle">지연 (µs, 1 µs 칸) — 세로축 로그(표본 수)</text>
<rect x="470.0" y="6.0" width="12.0" height="12.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="487.0" y="16.0" font-size="12" text-anchor="start">IRQ→ISR</text><rect x="560.0" y="6.0" width="12.0" height="12.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="577.0" y="16.0" font-size="12" text-anchor="start">IRQ→태스크</text>
</svg>
```

그림 5 — **측정 차트**: 예제 7(FreeRTOS)과 예제 8(Zephyr, 슬라이스 끔)의 1 µs 칸 히스토그램(세로 로그 축). 파랑 = IRQ → ISR, 주황 = IRQ → 태스크. 두 RTOS 모두 ISR 쪽에 **봉우리 둘 + 선반**, 태스크 쪽에 **같은 모양이 오른쪽으로 옮겨 간 것 + 긴 꼬리**가 보인다.

---

## 6. 히스토그램을 읽는다 — 모양 → 원인 → 증명

> 🔁 이 절은 Bare-Metal BM J7과 겹친다 (주인: 이 노트).

### 6.1 여섯 가지 모양

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13" font-weight="bold">히스토그램 모양 → 먼저 의심할 원인</text>
<g>
<line x1="20" y1="110" x2="210" y2="110" stroke="currentColor"/>
<rect x="40" y="40" width="10" height="70" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="128" font-size="12">① 좁은 봉우리 하나</text>
<text x="20" y="144" font-size="11">결정적 경로, 방해 없음</text>
</g>
<g>
<line x1="245" y1="110" x2="435" y2="110" stroke="currentColor"/>
<rect x="265" y="50" width="10" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="325" y="52" width="10" height="58" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="245" y="128" font-size="12">② 같은 꼴 봉우리가 일정 간격</text>
<text x="245" y="144" font-size="11">위상 고정 간섭 (주기 배수, 같은 클럭)</text>
</g>
<g>
<line x1="470" y1="110" x2="660" y2="110" stroke="currentColor"/>
<rect x="490" y="40" width="10" height="70" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="500" y="92" width="100" height="18" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="470" y="128" font-size="12">③ 평평한 선반</text>
<text x="470" y="144" font-size="11">무작위 위상의 차단 구간</text>
<text x="470" y="160" font-size="11">폭 = L, 면적 = 듀티</text>
</g>
<g>
<line x1="20" y1="270" x2="210" y2="270" stroke="currentColor"/>
<rect x="40" y="200" width="10" height="70" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="140" y="258" width="6" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<rect x="170" y="262" width="6" height="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="20" y="288" font-size="12">④ 멀리 작은 무리</text>
<text x="20" y="304" font-size="11">원인이 겹친 조합 (드물다)</text>
</g>
<g>
<line x1="245" y1="270" x2="435" y2="270" stroke="currentColor"/>
<polyline points="255,270 275,215 300,270" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<polyline points="330,270 360,225 395,270" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="245" y="288" font-size="12">⑤ 넓은 언덕 둘</text>
<text x="245" y="304" font-size="11">두 동작 모드 (캐시 적중/미스, 경로 분기)</text>
</g>
<g>
<line x1="470" y1="270" x2="660" y2="270" stroke="currentColor"/>
<rect x="490" y="200" width="10" height="70" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="640" y="262" width="6" height="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="470" y="288" font-size="12">⑥ 홀로 먼 점 하나</text>
<text x="470" y="304" font-size="11">측정 버그 먼저 의심, 그다음 드문 경로</text>
</g>
<text x="10" y="324" font-size="11">가로 = 지연, 세로 = 표본 수. 실제 히스토그램은 이 모양들의 합이다.</text>
</svg>
```

그림 6 — 모양별 첫 번째 의심. 이 노트의 측정에서 ①은 예제 5, ②는 DT6(4 µs 봉우리), ③은 CS15, ④는 tick + med 조합(53 · 57 µs), ⑤는 Zephyr의 idle vs busy `k_sem_give`(12.3 vs 17.8 µs), ⑥은 Zephyr 첫 시도의 476 µs(오래된 세마포어)로 실제로 보았다.

모양마다 숫자를 읽는 법:

- **선반의 폭 = 차단 구간의 길이 L**, **선반의 높이 × 폭(면적) = 그 구간이 켜져 있던 시간 비율**(무작위 위상일 때). 예제 7의 CS15는 폭 약 16 µs, 면적 약 17%(679 / 4000)로 설계값 15 µs · 18%와 맞는다. 거꾸로, 모르는 시스템에서 선반이 보이면 "어떤 L짜리 구간이 듀티 몇 %로 돈다"를 바로 추정할 수 있다.
- **봉우리 사이 간격 = 간섭의 양**, **두 번째 봉우리 높이 ÷ 전체 = 겹치는 비율**. DT6는 간격 4 µs(6 µs − 2 µs 앞섬), 비율 정확히 1/2(두 주기에 한 번).
- **두 단계 비교**: ISR 단계에 없고 태스크 단계에만 있는 모양은 ⑦(상위 태스크)이나 ISR 이후 경로(PendSV를 막는 것) 쪽이다. 두 단계에 같은 양으로 있는 모양은 ①(ISR 진입 전) 쪽이다.

### 6.2 원인 태깅 — 표본마다 "그때 누가 있었나"를 기록한다

모양만으로는 추측이다. 증명은 **표본 하나하나에 원인을 붙이는 것**이다. 예제 7이 쓴 방법은 일반적이다:

1. 의심 가는 방해꾼(크리티컬 섹션, ISR, 태스크)마다 **마지막 실행 구간 [t0, t1]**을 같은 사이클 시계로 남긴다(저장 두 번 — 예제 1 기준 수십 ns).
2. 측정 지점에서 **IRQ가 올라간 시각** a = 지금 − (타이머 경과)를 계산한다. 이 계산이 되는 것이 타이머 스냅샷 방법의 큰 장점이다.
3. [a, 지금]과 겹친 방해꾼을 비트마스크로 만들어, 마스크별 개수와 최댓값을 모은다.
4. 태그별 최댓값이 "그 원인이 만들 수 있는 최악"의 측정값이다. 태그 조합별 최댓값이 §7.2의 합성 재료다.

주의점: 겹쳤다고 원인인 것은 아니다. 예제 7의 ISR 쪽 "tick" 10개는 최대 3.64 µs로 거의 무해하다 — TIMER0이 SysTick을 선점하므로 BASEPRI가 올라간 짧은 구간에 떨어진 경우만 늦는다. **태그 = 상관, 원인 증명 = 다음 절의 끄기**다.

### 6.3 예제 9 — 하나씩 끄기(ablation): 그 원인을 없애면 그 모양이 사라지나

무엇을 확인하나: 예제 7을 그대로 두고 컴파일 플래그 `OFF`로 방해꾼을 하나씩 끈다. 1 = CS 끔, 2 = DT 끔, 4 = med가 깨어나되 일은 안 함, 7 = 셋 다, 15 = med 태스크 자체를 만들지 않음.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/E3; for off in 1 2 4 7 15; do echo "== OFF=$off"; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/frt/ex7_load.c OUT=/var/tmp/rtos/E3/ex9_off$off EXTRA_CFLAGS="-I$S/inc -DWRAP_TICK -DOFF=$off" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/E3/ex9_off$off/app.elf 120 | tee /var/tmp/rtos/E3/ex9_off$off.out | grep -v "^bin\|board_exit"; done'
```

```text
== OFF=1
IRQ->ISR  n=4000 min=120 avg=2560 max=8480 ns
  n=1992 max=2760 ns : (no tagged cause)
  n=1992 max=7600 ns : DT6 
  n=8 max=3640 ns : tick 
  n=8 max=8480 ns : DT6 tick 
IRQ->task n=4000 min=11680 avg=15800 max=57920 ns
  n=1907 max=18040 ns : (no tagged cause)
  n=1894 max=22880 ns : DT6 
  n=58 max=45320 ns : med25 
  n=58 max=50160 ns : DT6 med25 
  n=35 max=53120 ns : tick med25 
  n=48 max=57920 ns : DT6 tick med25 
== OFF=2
IRQ->ISR  n=4000 min=120 avg=1760 max=16120 ns
  n=3190 max=2800 ns : (no tagged cause)
  n=796 max=16120 ns : CS15 
  n=14 max=3400 ns : tick 
IRQ->task n=4000 min=11720 avg=15040 max=68240 ns
  n=3029 max=17960 ns : (no tagged cause)
  n=778 max=28240 ns : CS15 
  n=117 max=45240 ns : med25 
  n=58 max=53240 ns : tick med25 
  n=18 max=68240 ns : CS15 tick med25 
== OFF=4
IRQ->ISR  n=4000 min=120 avg=4000 max=20920 ns
  n=1610 max=2760 ns : (no tagged cause)
  n=382 max=16080 ns : CS15 
  n=1695 max=7600 ns : DT6 
  n=295 max=20920 ns : CS15 DT6 
  n=8 max=3640 ns : tick 
  n=10 max=8480 ns : DT6 tick 
IRQ->task n=4000 min=11760 avg=16480 max=46880 ns
  n=1578 max=18040 ns : (no tagged cause)
  n=371 max=28200 ns : CS15 
  n=1651 max=22880 ns : DT6 
  n=288 max=33040 ns : CS15 DT6 
  n=9 max=20080 ns : med25 
  n=7 max=24920 ns : DT6 med25 
  n=31 max=28000 ns : tick med25 
  n=11 max=43920 ns : CS15 tick med25 
  n=47 max=32800 ns : DT6 tick med25 
  n=7 max=46880 ns : CS15 DT6 tick med25 
== OFF=7
IRQ->ISR  n=4000 min=120 avg=120 max=3600 ns
  n=3984 max=2800 ns : (no tagged cause)
  n=16 max=3600 ns : tick 
IRQ->task n=4000 min=11720 avg=12520 max=28000 ns
  n=3916 max=18040 ns : (no tagged cause)
  n=14 max=20120 ns : med25 
  n=70 max=28000 ns : tick med25 
== OFF=15
IRQ->ISR  n=4000 min=120 avg=120 max=1400 ns
  n=3993 max=160 ns : (no tagged cause)
  n=7 max=1400 ns : tick 
IRQ->task n=4000 min=12200 avg=12280 max=14040 ns
  n=3939 max=12280 ns : (no tagged cause)
  n=61 max=14040 ns : tick 
```

출력에서 볼 것:

- **CS를 끄면(OFF=1) ISR 최댓값이 20.92 → 8.48 µs**, CS15 태그가 사라지고 표본이 "없음"과 DT6로 정확히 반반(1992 / 1992)이 된다. 선반이 CS 때문이었다는 증명이다.
- **DT를 끄면(OFF=2) 4 µs 봉우리가 사라지고**(DT6 태그 0), "없음" 3190 + CS15 796. CS15 표본이 두 배 가까이 된 것은 DT와 겹치던 표본이 CS 단독으로 옮겨 왔기 때문이다.
- **med에게 일을 안 시켜도(OFF=4) med 태그는 남는다**. med는 여전히 매 tick 깨어나 `vTaskDelay`로 다시 잠드는 커널 경로를 돈다. 25 µs 일이 사라진 만큼 꼬리가 73.92 → 46.88 µs로 줄었을 뿐이다.
- **셋 다 꺼도(OFF=7) 태그 없는 꼬리가 18 µs까지 남는다**(기본 11.72 µs). 이것이 §5.2에서 남겨 둔 "태깅하지 않은 원인"이다. 가설: med의 **깨어남 · 잠듦 커널 경로**(md 구간 밖, `vTaskDelay` 호출 안)와 그 안의 크리티컬 섹션. **med 태스크 자체를 없애면(OFF=15) 태그 없는 표본의 최댓값이 12.28 µs(= 기본값)로 붙고 ISR 쪽도 160 ns가 된다** — 가설이 맞았다. "태깅 안 된 꼬리"는 **계측하지 않은 원인이 있다는 신호**이고, 끄기로 범인을 좁힌다.
- 남은 tick 61개(최대 14.04 µs)는 커널 자신의 tick 처리다 — FreeRTOS에서 1 kHz tick의 비용은 이 시스템에서 최악 +1.8 µs다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<text x="10.0" y="18.0" font-size="13" text-anchor="start">원인을 하나씩 끄면 — IRQ→태스크 최댓값과 평균 (µs)</text>
<text x="150.0" y="51.0" font-size="12" text-anchor="end">모두 켬</text><rect x="160.0" y="34.0" width="332.6" height="22.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><rect x="160.0" y="40.0" width="78.3" height="10.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="498.6" y="49.0" font-size="11" text-anchor="start">max 73.92 / avg 17.40</text>
<text x="150.0" y="85.0" font-size="12" text-anchor="end">CS 끔</text><rect x="160.0" y="68.0" width="260.6" height="22.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><rect x="160.0" y="74.0" width="71.1" height="10.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="426.6" y="83.0" font-size="11" text-anchor="start">max 57.92 / avg 15.80</text>
<text x="150.0" y="119.0" font-size="12" text-anchor="end">DT 끔</text><rect x="160.0" y="102.0" width="307.1" height="22.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><rect x="160.0" y="108.0" width="67.7" height="10.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="473.1" y="117.0" font-size="11" text-anchor="start">max 68.24 / avg 15.04</text>
<text x="150.0" y="153.0" font-size="12" text-anchor="end">med 일 없음</text><rect x="160.0" y="136.0" width="211.0" height="22.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><rect x="160.0" y="142.0" width="74.2" height="10.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="377.0" y="151.0" font-size="11" text-anchor="start">max 46.88 / avg 16.48</text>
<text x="150.0" y="187.0" font-size="12" text-anchor="end">셋 다 끔</text><rect x="160.0" y="170.0" width="126.0" height="22.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><rect x="160.0" y="176.0" width="56.3" height="10.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="292.0" y="185.0" font-size="11" text-anchor="start">max 28.00 / avg 12.52</text>
<text x="150.0" y="221.0" font-size="12" text-anchor="end">med 태스크 없음</text><rect x="160.0" y="204.0" width="63.2" height="22.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><rect x="160.0" y="210.0" width="55.3" height="10.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="229.2" y="219.0" font-size="11" text-anchor="start">max 14.04 / avg 12.28</text>
<rect x="160.0" y="236.0" width="12.0" height="10.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><text x="177.0" y="245.0" font-size="11" text-anchor="start">최댓값</text><rect x="240.0" y="236.0" width="12.0" height="10.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="257.0" y="245.0" font-size="11" text-anchor="start">평균</text>
</svg>
```

그림 7 — **측정 차트**: 예제 7 · 9의 IRQ → 태스크 최댓값(빨강 막대)과 평균(파랑 막대). 원인을 하나씩 끌 때 **평균은 12 ~ 17 µs 사이에서 거의 안 움직이지만 최댓값은 74 → 14 µs로 다섯 배 움직인다.** 최악값 문제는 평균으로 보이지 않는다.

### 6.4 Don의 경험과 연결

SSD 펌웨어의 QoS 디버깅에서 Don이 했던 일이 정확히 이 순서다: (1) 지연 분포를 본다(봉우리 · 꼬리), (2) 명령마다 "그때 GC가 돌았나, read retry였나, 다른 다이와 채널 충돌이었나"를 태깅한다, (3) GC를 끄거나 스로틀해서 꼬리가 사라지는지 본다. MCU에서는 GC 대신 크리티컬 섹션 · 상위 ISR · 상위 태스크 · tick이 원인 후보이고, 태깅에 쓰는 시계가 µs가 아니라 40 ns(실칩이면 2 ns)일 뿐이다.

---

## 7. 최악 경로 찾기 — 가장 긴 구간, 상한 합성, 얼마나 오래 재나

> 🔁 이 절은 Bare-Metal BM J7과 겹친다 (주인: 이 노트).

### 7.1 왜 평균은 쓸모가 없나

마감(deadline)은 **모든** job에 걸린다. 1 kHz 제어 루프에서 1000번에 한 번 늦으면 1초에 한 번 늦는 것이다. 평균은 "얼마나 자주 겹치나"(듀티)에 좌우되고, 마감을 결정하는 것은 꼬리다. 예제 7의 IRQ → 태스크로 다시 보자(아래 예제 11 출력): 평균 약 17 µs, p50 17 µs, p90 24 µs, **p99 54 µs**, p99.9 62 µs, 최댓값 73.92 µs. "평균 17 µs라 125 µs(8 kHz) 예산에 넉넉하다"는 말은 맞지만 이유가 틀렸다 — 넉넉한 이유는 **최댓값 74 µs가 125 µs보다 작아서**여야 하고, 그 최댓값도 "지금까지 본 것"일 뿐이다(§7.4). 표준편차도 같은 문제다: 이 분포처럼 봉우리 둘 + 드문 꼬리이면 표준편차는 어느 것도 대표하지 않는다. 실시간 지연은 **min · p50 · p99 · p99.9 · max(+ 표본 수 · 측정 시간)**로 보고한다(EL N3와 같은 형식).

### 7.2 max-plus 합성 — 최악값 상한을 조각으로 세운다

측정으로 얻은 최댓값은 **하한**이다(그 이상이 있을 수 있다). 설계에는 **상한**이 필요하다. 그림 1의 조각들로 상한을 세우는 규칙은 두 가지다.

- **차단(blocking)은 max**: 낮은 우선순위 코드가 마스크를 잡고 있는 구간(크리티컬 섹션, 커널 마스킹, 스케줄러 잠금)은 IRQ가 올라간 순간에 **하나만** 진행 중일 수 있다. 그래서 그중 가장 긴 것 하나만 더한다. 이것이 "가장 긴 크리티컬 섹션"이 핵심 숫자인 이유다(C7).
- **간섭(interference)은 Σ**: 더 높은 우선순위의 ISR · 태스크는 기다리는 동안 **몇 번이고 다시** 들어올 수 있으므로 모두 더한다(기다리는 창이 길면 같은 ISR이 여러 번 — 이 반복 계산이 N3의 응답 시간 분석 RTA다).

그래서 **상한 = 기본 경로 + max(차단 후보들) + Σ(간섭 후보들)**이다. 덧셈과 max만 쓰는 대수라서 max-plus 합성이라고 부른다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13" font-weight="bold">예제 7의 IRQ → 태스크 상한을 조각으로 (µs)</text>
<text x="140" y="62" font-size="12" text-anchor="end">상한 합성</text>
<rect x="150" y="44" width="73.7" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="186" y="63" font-size="11" text-anchor="middle">기본 12.28</text>
<rect x="223.7" y="44" width="100.8" height="28" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="274" y="63" font-size="11" text-anchor="middle">CS 16.80</text>
<rect x="324.5" y="44" width="61.4" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="355" y="63" font-size="11" text-anchor="middle">DT 10.24</text>
<rect x="385.9" y="44" width="245.8" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="509" y="63" font-size="11" text-anchor="middle">tick + med 40.96</text>
<text x="631" y="92" font-size="11" text-anchor="end">= 80.28</text>
<line x1="593.5" y1="36" x2="593.5" y2="110" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="589" y="124" font-size="11" text-anchor="end">측정 최댓값 73.92 (4000개 중 14개)</text>
<text x="20" y="156" font-size="12">빨강 = 차단 → 후보 중 가장 긴 하나 (max). CS15, 커널 마스킹, tick의 BASEPRI 구간 중 CS15가 가장 길다</text>
<text x="20" y="178" font-size="12">파랑 · 주황 = 간섭 → 모두 더한다 (Σ). 상위 ISR, 상위 태스크 + 그것을 깨운 tick</text>
<text x="20" y="200" font-size="12">각 조각 = 예제 7에서 그 원인만 태그된 표본의 최댓값 − 기본값 (예제 11이 계산)</text>
<text x="20" y="222" font-size="12">상한 ≥ 측정 최댓값이어야 정상이다. 거꾸로면 빠진 원인이 있다</text>
</svg>
```

그림 8 — max-plus 합성. 원인별 "초과분"을 측정에서 따로 얻고(원인 태깅 덕분), 차단은 max · 간섭은 Σ로 묶는다. 상한 80.28 µs가 측정 최댓값 73.92 µs보다 크다 — 앞뒤가 맞는다.

이 합성의 약점도 알아야 한다. (1) 각 초과분 자체가 측정 최댓값이라 그 조각의 진짜 최악보다 작을 수 있다 — 조각마다 코드 경로를 보고 최악 입력을 따로 만든다(WCET, N 모듈). (2) 간섭이 기다리는 창 안에 **여러 번** 올 수 있다(창이 DT 주기 1 ms보다 짧아서 여기선 한 번). (3) 태깅하지 않은 원인은 빠진다(§6.3에서 med의 커널 경로를 끄기로 찾은 것처럼).

### 7.3 예제 10 — 가장 긴 크리티컬 섹션을 찾는다: 스택에 쌓인 LR

"우리 펌웨어에서 가장 긴 크리티컬 섹션은 어디인가?" — 수만 줄 코드에서 `taskENTER_CRITICAL`을 grep해서 하나씩 재는 것은 끝이 없다. 하드웨어가 답을 준다: **IRQ가 오래 막혔다가 마침내 들어온 순간, 하드웨어가 쌓은 예외 프레임의 PC는 "마스크를 푼 바로 다음 명령"**이다(B2 §3). 그러니 지연이 큰 표본에서 쌓인 PC(와 LR)를 기록하면, 막고 있던 범인이 그 자리에 있다.

무엇을 확인하나: 우선순위 1 태스크가 세 함수를 부른다 — `ring_put`(2 µs, 매번), `cfg_update`(12 µs, 8번에 한 번), `log_flush`(60 µs, 400번에 한 번, 무작위). TIMER0(약 100 µs 주기) ISR을 naked 스텁으로 받아 예외 프레임 주소를 C 함수에 넘기고, 지연 3 µs 이상인 표본만 **쌓인 LR별**로 개수 · 최댓값 · 그때의 PC를 모은다. 500 · 2000 · 8000 · 20000 표본 시점의 "지금까지 최댓값"도 찍는다(§7.4).

```c
/* ex10 — find the longest critical section: the ISR reads its own stacked PC/LR, keeps the worst */
#include "FreeRTOS.h"
#include "task.h"
#include "e3.h"
volatile uint32_t e3_armed, e3_s4;
void *volatile e3_hi;
typedef struct { uint32_t pc, lr, lat, n; } site_t;
static site_t site[8];                              /* worst latency per stacked LR (caller) */
static volatile uint32_t nsamp, worst;
__attribute__((noinline)) static void ring_put(void)     /* 2 us, very often */
{ taskENTER_CRITICAL(); spin_cyc(50); taskEXIT_CRITICAL(); }
__attribute__((noinline)) static void cfg_update(void)   /* 12 us, sometimes */
{ taskENTER_CRITICAL(); spin_cyc(300); taskEXIT_CRITICAL(); }
__attribute__((noinline)) static void log_flush(void)    /* 60 us, 1 call in 400 */
{ taskENTER_CRITICAL(); spin_cyc(1500); taskEXIT_CRITICAL(); }
void t0_c(uint32_t *frame)                          /* frame[5] = stacked LR, frame[6] = PC */
{
    uint32_t lat = t0_elapsed();
    TIMER_INTCLR(TIMER0_BASE) = 1;
    nsamp++;
    if (lat > worst) worst = lat;
    if (lat < 75) return;                           /* only record > 3 us */
    int i = 0;
    while (i < 7 && site[i].n && site[i].lr != frame[5]) i++;
    site[i].n++; site[i].lr = frame[5];
    if (lat > site[i].lat) { site[i].lat = lat; site[i].pc = frame[6]; }
}
__attribute__((naked)) void TIMER0_IRQHandler(void)
{
    __asm volatile("tst lr, #4 \n ite eq \n mrseq r0, msp \n mrsne r0, psp \n b t0_c");
}
static void lo_task(void *a)
{
    (void)a;
    for (uint32_t k = 0;; k++) {
        ring_put();
        if (k % 8 == 0) cfg_update();
        if (rnd(400) == 0) log_flush();
        spin_cyc(250 + rnd(1250));
    }
}
static void report_task(void *a)
{
    (void)a;
    static const uint32_t at[] = { 500, 2000, 8000, 20000 };
    for (int j = 0; j < 4; j++) {
        while (nsamp < at[j]) vTaskDelay(1);
        board_printf_raw("after %u samples: worst = %u ns\n", at[j], NS(worst));
    }
    t0_stop();
    for (int i = 0; i < 8 && site[i].n; i++)
        board_printf_raw("site lr=0x%x pc=0x%x n=%u worst=%u ns\n", site[i].lr, site[i].pc, site[i].n, NS(site[i].lat));
    board_exit(0);
}
int main(void)
{
    xTaskCreate(lo_task, "lo", 256, NULL, 1, NULL);
    xTaskCreate(report_task, "rep", 256, NULL, 2, NULL);
    t0_start(2500 + 13, 0x40);                      /* ~100 us */
    vTaskStartScheduler();
    return 1;
}
```

(`e3_armed` · `e3_hi`는 공용 FreeRTOSConfig.h의 trace 훅이 참조하므로 정의만 해 둔다.) 첫 빌드(빌드 A)는 기본 옵션이다. 기록된 주소는 호스트에서 `addr2line`으로 함수 · 줄로 바꿨다.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/E3; O=/var/tmp/rtos/E3/ex10; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/frt/ex10_culprit.c OUT=$O EXTRA_CFLAGS="-I$S/inc" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/app.elf 120 | tee $O.out; grep site $O.out | while read s lr pc n w; do printf "%s %s %s %s -> LR: " "$lr" "$pc" "$n" "$w"; arm-none-eabi-addr2line -f -s -e $O/app.elf ${lr#lr=} | paste -sd" "; printf "      PC: "; arm-none-eabi-addr2line -f -s -e $O/app.elf ${pc#pc=} | paste -sd" "; done'
```

```text
after 500 samples: worst = 52960 ns
after 2000 samples: worst = 52960 ns
after 8000 samples: worst = 60760 ns
after 20000 samples: worst = 60760 ns
site lr=0x223 pc=0x210e n=647 worst=13320 ns
site lr=0x21b pc=0x210e n=172 worst=3320 ns
site lr=0x229 pc=0x210e n=80 worst=60760 ns
site lr=0x0 pc=0x215a n=13 worst=3840 ns
[board_exit] code=0
lr=0x223 pc=0x210e n=647 worst=13320 ns -> LR: lo_task ex10_culprit.c:37 (discriminator 1)
      PC: vPortExitCritical port.c:452
lr=0x21b pc=0x210e n=172 worst=3320 ns -> LR: lo_task ex10_culprit.c:37
      PC: vPortExitCritical port.c:452
lr=0x229 pc=0x210e n=80 worst=60760 ns -> LR: rnd e3.h:37
      PC: vPortExitCritical port.c:452
lr=0x0 pc=0x215a n=13 worst=3840 ns -> LR: ?? startup.c:33
      PC: SysTick_Handler port.c:515
```

읽는 법과 함정:

- **PC는 모두 `vPortExitCritical`(port.c 452행, 함수 끝)**이다. 당연하다 — BASEPRI를 0으로 내리는 명령이 거기 있고, 막혀 있던 IRQ는 바로 그다음 명령에서 들어온다. **PC는 "어디서 풀었나"만 말하고 "누가 잡았나"는 말하지 않는다.** 그래서 LR을 같이 기록했다(`vPortExitCritical`은 LR을 건드리지 않는 말단 함수라 쌓인 LR = 그것을 부른 쪽의 복귀 주소).
- 그런데 LR이 `lo_task`를 가리킨다. 세 함수가 `taskEXIT_CRITICAL()`을 **꼬리 호출(tail call)**로 컴파일되어 `bl` 대신 `b.w vPortExitCritical`로 뛰었기 때문이다 — 그 함수의 프레임이 사라지고 LR은 `lo_task` 안의 복귀 지점이 된다. 줄 번호도 인라인된 `rnd()`(e3.h:37)처럼 엉뚱하게 나온다. 그래도 **LR 값 세 개(0x21b, 0x223, 0x229)는 서로 다른 호출 지점**이고, 그 주소를 디스어셈블에서 찾으면 각각 `ring_put`, `cfg_update`, `log_flush` 호출 바로 뒤다.
- 줄 번호까지 바로 보려고 꼬리 호출을 끈 빌드 B(`-fno-optimize-sibling-calls`)를 다시 돌렸다.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/E3; O=/var/tmp/rtos/E3/ex10b; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/frt/ex10_culprit.c OUT=$O EXTRA_CFLAGS="-I$S/inc -fno-optimize-sibling-calls" >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/app.elf 120 | tee $O.out; grep site $O.out | while read s lr pc n w; do printf "%s %s %s -> " "$lr" "$n" "$w"; arm-none-eabi-addr2line -f -s -e $O/app.elf ${lr#lr=} | paste -sd" "; done; arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/E3/ex10/app.elf | awk "/<ring_put>:/,/^\$/"'
```

```text
after 500 samples: worst = 53120 ns
after 2000 samples: worst = 53120 ns
after 8000 samples: worst = 55760 ns
after 20000 samples: worst = 58840 ns
site lr=0x17d pc=0x20fa n=647 worst=13280 ns
site lr=0x15f pc=0x20fa n=159 worst=3280 ns
site lr=0x19f pc=0x20fa n=73 worst=58840 ns
site lr=0x871 pc=0x2014 n=2 worst=3080 ns
site lr=0x0 pc=0x2146 n=18 worst=3880 ns
[board_exit] code=0
lr=0x17d n=647 worst=13280 ns -> cfg_update ex10_culprit.c:13
lr=0x15f n=159 worst=3280 ns -> ring_put ex10_culprit.c:11
lr=0x19f n=73 worst=58840 ns -> log_flush ex10_culprit.c:15
lr=0x871 n=2 worst=3080 ns -> vTaskSwitchContext tasks.c:5235 (discriminator 1)
lr=0x0 n=18 worst=3880 ns -> ?? startup.c:33
00000148 <ring_put>:
     148:	push	{r3, lr}
     14a:	bl	20b0 <vPortEnterCritical>
     14e:	ldr	r2, [pc, #20]	@ (164 <ring_put+0x1c>)
     150:	ldr	r1, [r2, #24]
     152:	ldr	r3, [r2, #24]
     154:	subs	r3, r3, r1
     156:	cmp	r3, #49	@ 0x31
     158:	bls.n	152 <ring_put+0xa>
     15a:	ldmia.w	sp!, {r3, lr}
     15e:	b.w	20fc <vPortExitCritical>
     162:	nop
     164:	.word	0x40028000
```

(디스어셈블은 빌드 A의 `ring_put` — 마지막 `b.w vPortExitCritical`이 꼬리 호출이다.) 출력에서 볼 것:

- 이제 LR이 **`log_flush` 15행(최악 58.84 µs, 73번), `cfg_update` 13행(13.28 µs, 647번), `ring_put` 11행(3.28 µs, 159번)**을 바로 가리킨다. 가장 긴 크리티컬 섹션이 이름과 줄 번호로 나왔다. 소스를 하나도 읽지 않았다.
- 덤으로 커널 자신의 마스킹 구간 두 곳이 잡혔다: `vTaskSwitchContext`(PendSV 안, 3.08 µs)와 SysTick 핸들러(PC가 port.c 515행 `xPortSysTickHandler`, 3.84 ~ 3.88 µs). LR 0x0은 SysTick이 예외 핸들러라 쌓인 LR이 의미 없는 경우다 — PC로 본다.
- **많이 걸린 것 ≠ 긴 것**: `cfg_update`가 647번으로 제일 자주 걸렸지만 범인은 73번뿐인 `log_flush`다. 개수가 아니라 **최댓값으로 정렬**한다.
- 실무 확장: 이 기록 구조체를 `.noinit` RAM에 두면 리셋 뒤에도 남는다(M5). 필드에서 몇 주 돌린 "최악 지연 + 그때의 PC/LR + 타임스탬프"는 어떤 벤치마크보다 값지다. PC 하나만 남기는 대신 쌓인 프레임 몇 워드와 PSP를 같이 남기면 스택을 거슬러 올라갈 수도 있다.
- 같은 아이디어를 하드웨어로 하면 DWT의 PC 샘플링이나 ETM이 된다(M6). 이 방법은 계측 코드가 ISR 하나뿐이라 observer effect가 작다.

### 7.4 오래 재야 하는 이유 — 최댓값은 측정 시간의 함수다

예제 10 출력의 첫 네 줄이 핵심이다. 빌드 A의 최댓값은 500 표본에서 52.96 µs, 8000에서 60.76 µs. 빌드 B는 같은 코드인데(옵션만 다름) 53.12 → 55.76 → 58.84 µs로 **20000 표본이 지나도 계속 올라간다**. 진짜 최악은 `log_flush` 60 µs + 진입 · 퇴장 ≈ 61.5 µs다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<text x="10.0" y="18.0" font-size="13" text-anchor="start">오래 잴수록 최댓값은 올라간다 — ex10 (µs)</text>
<line x1="70" y1="220" x2="590" y2="220" stroke="currentColor"/><line x1="70" y1="220" x2="70" y2="50" stroke="currentColor"/>
<text x="64.0" y="224.0" font-size="11" text-anchor="end">50</text>
<text x="64.0" y="175.4" font-size="11" text-anchor="end">54</text>
<text x="64.0" y="126.9" font-size="11" text-anchor="end">58</text>
<text x="64.0" y="78.3" font-size="11" text-anchor="end">62</text>
<text x="127.7" y="235.0" font-size="11" text-anchor="middle">500</text>
<text x="284.2" y="235.0" font-size="11" text-anchor="middle">2000</text>
<text x="440.8" y="235.0" font-size="11" text-anchor="middle">8000</text>
<text x="544.2" y="235.0" font-size="11" text-anchor="middle">20000</text>
<line x1="70" y1="80.4" x2="590" y2="80.4" stroke="#d0564a" stroke-dasharray="5 4"/><text x="590.0" y="74.4" font-size="11" text-anchor="end">상한 ≈ 61.5 (60 + 진입·퇴장)</text>
<polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="127.7,184.1 284.2,184.1 440.8,89.3 544.2,89.3"/>
<circle cx="127.7" cy="184.1" r="3.5" fill="#4a7bd0"/>
<circle cx="284.2" cy="184.1" r="3.5" fill="#4a7bd0"/>
<circle cx="440.8" cy="89.3" r="3.5" fill="#4a7bd0"/>
<circle cx="544.2" cy="89.3" r="3.5" fill="#4a7bd0"/>
<text x="552.2" y="93.3" font-size="11" text-anchor="start">빌드 A 60.76</text>
<polyline fill="none" stroke="#e08a3c" stroke-width="2" points="127.7,182.1 284.2,182.1 440.8,150.1 544.2,112.7"/>
<circle cx="127.7" cy="182.1" r="3.5" fill="#e08a3c"/>
<circle cx="284.2" cy="182.1" r="3.5" fill="#e08a3c"/>
<circle cx="440.8" cy="150.1" r="3.5" fill="#e08a3c"/>
<circle cx="544.2" cy="112.7" r="3.5" fill="#e08a3c"/>
<text x="552.2" y="116.7" font-size="11" text-anchor="start">빌드 B 58.84</text>
<text x="330.0" y="252.0" font-size="11" text-anchor="middle">표본 수 (로그 축)</text>
</svg>
```

그림 9 — **측정 차트**: 예제 10 두 빌드의 "지금까지 최댓값". 빨간 점선은 코드에서 구한 상한이다. 최댓값은 표본 수의 로그를 따라 계단식으로 오르고, 빌드마다 다른 계단을 밟는다(코드 배치가 바뀌면 위상이 바뀐다).

왜 이렇게 느린가? 60 µs `log_flush`가 400번에 한 번 불리므로 CPU 시간의 약 0.39%가 그 안이다. 지연이 59 µs 이상이 되려면 IRQ가 그 60 µs 창의 **처음 1 µs** 안에 떨어져야 한다(그래야 남은 59 µs를 기다린다). 확률은 0.39% × 1/60 ≈ 0.0065%. 이것을 계산하는 것이 예제 11이다.

### 7.5 예제 11 — 평균 vs 꼬리, 상한 합성, 필요한 측정 시간 (Python)

무엇을 확인하나: 예제 7의 히스토그램에서 백분위를 구하고, §7.2의 상한을 숫자로 합성하고, 예제 10의 "드문 긴 구간"을 99% 확률로 한 번이라도 보려면 몇 표본이 필요한지 계산한다. 확률 p인 사건을 n번 시도에서 한 번 이상 볼 확률이 1 − (1 − p)^n이므로 n = ln(0.01) / ln(1 − p)다.

```python
# ex11 — average vs tail, max-plus composition, and "how long must I run?" from ex7/ex10 numbers
import math, re
def hist(path, col):                       # col 0 = isr, 1 = task; 1 us bins from ex7/ex8 output
    h = {}
    for m in re.finditer(r"bin (\d+) us: isr=(\d+) task=(\d+)", open(path).read()):
        h[int(m[1])] = int(m[2 + col])
    return h
def pct(h, q):
    n, acc = sum(h.values()), 0
    for b in sorted(h):
        acc += h[b]
        if acc >= q * n: return b
h = hist("/var/tmp/rtos/E3/ex7.out", 1)
avg = sum(b * c for b, c in h.items()) / sum(h.values())
print(f"FreeRTOS IRQ->task (1 us bins): avg~{avg:.1f}  p50={pct(h,.5)}  p90={pct(h,.9)}  "
      f"p99={pct(h,.99)}  p99.9={pct(h,.999)}  highest bin={max(h)} us (bins stop at 64)")
# max-plus: one blocking term (max over lower-priority sections) + sum of interference terms
base = 12.28                              # ex7 no-cause minimum
excess = {"CS15": 29.08, "DT6": 22.52, "tick+med25": 53.24}   # single-cause maxima from ex7
ex = {k: v - base for k, v in excess.items()}
bound = base + ex["CS15"] + ex["DT6"] + ex["tick+med25"]
print("excess over base:", {k: round(v, 2) for k, v in ex.items()})
print(f"composed bound = {base} + {ex['CS15']:.2f} + {ex['DT6']:.2f} + {ex['tick+med25']:.2f}"
      f" = {bound:.2f} us   (observed max 73.92 us in 14 of 4000 samples)")
# ex10: log_flush = 60 us once per 400 loop iterations; loop ~ 2 + 12/8 + 60/400 + 35 us
loop = 2 + 12 / 8 + 60 / 400 + 35
p_in = (60 / 400) / loop
print(f"P(sample lands in log_flush) = {p_in:.4f}  -> expected {20000 * p_in:.0f} of 20000 (ex10 saw 73-80)")
for tail in (10, 5, 1):                   # land in the LAST `tail` us of the 60 us window
    p = p_in * tail / 60
    n99 = math.log(0.01) / math.log(1 - p)
    print(f"to see latency >= {60 - tail} us with 99% confidence: {n99:8.0f} samples"
          f" = {n99 * 100e-6:6.1f} s at 10 kHz")
```

```sh
limactl shell elinux -- bash -lc 'python3 /Users/donh/.rtos_scratch/E3/py/ex11_bound.py'
```

```text
FreeRTOS IRQ->task (1 us bins): avg~17.0  p50=17  p90=24  p99=54  p99.9=62  highest bin=63 us (bins stop at 64)
excess over base: {'CS15': 16.8, 'DT6': 10.24, 'tick+med25': 40.96}
composed bound = 12.28 + 16.80 + 10.24 + 40.96 = 80.28 us   (observed max 73.92 us in 14 of 4000 samples)
P(sample lands in log_flush) = 0.0039  -> expected 78 of 20000 (ex10 saw 73-80)
to see latency >= 50 us with 99% confidence:     7117 samples =    0.7 s at 10 kHz
to see latency >= 55 us with 99% confidence:    14237 samples =    1.4 s at 10 kHz
to see latency >= 59 us with 99% confidence:    71194 samples =    7.1 s at 10 kHz
```

출력에서 볼 것:

- 백분위는 64 µs를 넘는 표본(칸 밖)을 빼고 셌으므로 p99.9 이상은 실제보다 조금 작다 — 칸 범위를 최댓값보다 넓게 잡는 것도 측정 설계다.
- **확률 모델이 측정과 맞는다**: `log_flush`에 떨어질 확률 0.39%로 20000개 중 78개를 예상했고, 예제 10은 80개(빌드 A) · 73개(빌드 B)를 봤다.
- **59 µs 이상을 99% 확률로 한 번 보려면 71194 표본 = 10 kHz로 7.1초**가 필요하다. 예제 10은 20000 표본(2초)이라 빌드 B가 58.84 µs에서 멈췄다. 실제 시스템의 드문 경로는 0.39%보다 훨씬 드물다(에러 처리, 플래시 쓰기, 부팅 직후 한 번) — 그래서 **측정은 시간 단위가 아니라 "그 경로가 몇 번 지나갔나"로 설계**하고, 측정으로 못 덮는 부분은 §7.2의 상한(코드 리뷰 + 구간별 최악 입력)으로 덮는다. Linux에서 cyclictest를 몇 시간씩 돌리는 이유(EL N3 §5)와 같은 수학이다.

### 7.6 최악 경로 찾기 체크리스트

1. **모든 마스킹 구간의 목록과 길이**: `--wrap=vPortEnterCritical`로 호출 위치별 최댓값(C7 §6.4), 또는 예제 10처럼 지연이 큰 표본의 쌓인 PC/LR. PRIMASK를 직접 쓰는 코드(`__disable_irq`, 벤더 HAL)도 grep으로 따로 찾는다 — `--wrap`에 안 걸린다.
2. **상위 ISR 목록과 각각의 최악 실행 시간 · 최소 도착 간격** — E2의 우선순위 배치표에 열로 넣는다.
3. **커널의 마스킹 구간**: tick 처리(깨우는 태스크 수에 비례), 전환(`vTaskSwitchContext`), Zephyr 타임 슬라이스 재설정 — 설정(LEAN, TIMESLICING)이 이 값을 바꾼다(§4).
4. **상위 태스크의 실행 + 그 커널 경로**(깨어남 · 잠듦, §6.3의 med).
5. **합성**: 기본 + max(1, 3의 마스킹) + Σ(2, 4) → 상한. 측정 최댓값과 비교해 상한 < 측정이면 빠진 원인을 찾는다.
6. **장기 포착**: 필드 빌드에 "최댓값 + 그때의 PC/LR + 시각" 기록기를 남긴다(예제 10 구조, M5의 noinit).

---

## 8. 예제 12 — 측정 숫자를 차트로 (Python)

그림 4 · 5 · 7 · 9는 예제 5 ~ 10의 출력 숫자를 Python으로 좌표 변환해 만든 SVG다. 아래는 그림 4(누적 막대) 부분이다. 히스토그램(로그 축), 끄기 비교 막대, 최댓값 곡선도 같은 파일에서 같은 방식으로 출력 파일(`ex7.out` 등)을 정규식으로 읽어 만든다(그 부분 약 50줄은 생략). VM에서 실행했다.

```python
# ex12 — measured numbers (ex5, ex6, ex7, ex8, ex9, ex10) -> SVG charts for the note
import re
OUT = "/var/tmp/rtos/E3/"
C = ["#4a7bd0", "#e08a3c", "#888", "#3f9a6b", "#d0564a"]
def T(x, y, s, a="start", sz=12): return f'<text x="{x:.1f}" y="{y:.1f}" font-size="{sz}" text-anchor="{a}">{s}</text>'
def R(x, y, w, h, c, op=0.55): return f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" fill="{c}" fill-opacity="{op}" stroke="{c}"/>'
# (A) stacked breakdown, avg ns per segment (ex5 default/lean, ex6 default/ts_off)
rows = [("FreeRTOS 기본", [120, 3360, 480, 3320, 2000], 8320),
        ("FreeRTOS lean", [120, 3360, 480, 1800, 2000], 6840),
        ("Zephyr 기본", [880, 12320, 480, 2240, 640], 14080),
        ("Zephyr slice 끔", [880, 3560, 440, 2280, 640], 5320)]
seg = ["진입·디스패치", "ISR 안 커널 호출", "ISR 퇴장", "PendSV·전환", "복원→태스크"]
sx, x0 = 24.0, 130                                   # px per us
s = ['<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">',
     T(10, 18, "IRQ → 태스크 지연 분해 (µs, 평균, QEMU icount)", sz=13)]
for i, (name, v, raw) in enumerate(rows):
    y, x = 34 + i * 44, x0
    s.append(T(x0 - 8, y + 19, name, "end"))
    for k, ns in enumerate(v):
        w = ns / 1000 * sx
        s.append(R(x, y, w, 26, C[k]))
        if w > 30: s.append(T(x + w / 2, y + 17, f"{ns/1000:.2f}", "middle", 11))
        x += w
    s.append(T(x + 6, y + 12, f"계: {sum(v)/1000:.2f}", sz=11))
    s.append(T(x + 6, y + 25, f"계측 없이: {raw/1000:.2f}", sz=11))
for j in range(0, 19, 2):
    gx = x0 + j * sx
    s.append(f'<line x1="{gx:.1f}" y1="210" x2="{gx:.1f}" y2="214" stroke="currentColor"/>' + T(gx, 226, j, "middle", 11))
s.append(f'<line x1="{x0}" y1="210" x2="{x0 + 18 * sx:.1f}" y2="210" stroke="currentColor"/>')
lx = 20
for k, n in enumerate(seg):
    s.append(R(lx, 242, 12, 12, C[k]) + T(lx + 17, 253, n, sz=12)); lx += 26 + 13 * len(n.replace("·", "").replace("→", "")) * 0.95
s.append("</svg>"); open(OUT + "figA.svg", "w").write("\n".join(s))
# ... (B) histograms, (C) ablation, (D) worst-so-far: same pattern, omitted
```

```sh
limactl shell elinux -- bash -lc 'python3 /Users/donh/.rtos_scratch/E3/py/ex12_charts.py && cd /var/tmp/rtos/E3 && for f in figA figB figC figD; do python3 -c "import xml.dom.minidom,sys; xml.dom.minidom.parse(sys.argv[1]); print(sys.argv[1], \"ok\")" $f.svg; done'
```

```text
wrote figA..figD: [6328, 16970, 2762, 1934] bytes
figA.svg ok
figB.svg ok
figC.svg ok
figD.svg ok
```

구간 값은 손으로 옮겨 적었다(예제 5 · 6 출력의 avg). 숫자가 바뀌면 차트도 다시 만든다 — **차트를 손으로 그리지 않는 것**이 숫자와 그림이 어긋나지 않게 하는 가장 쉬운 방법이다.

---

## 9. RTOS 커널 안에서는 무슨 일이 일어나나 — 각 구간의 소스 위치

§4의 구간을 소스 함수에 대응시킨다. 한 줄씩 따라가는 것은 C2(PendSV), C1(ready 리스트), E1(FromISR 규칙)의 몫이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="18" font-size="13" font-weight="bold" text-anchor="middle">FreeRTOS V11.3.2 (ARM_CM3)</text>
<text x="510" y="18" font-size="13" font-weight="bold" text-anchor="middle">Zephyr v4.4.2 (Cortex-M)</text>
<rect x="10" y="30" width="320" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="20" y="51" font-size="12">TIMER0_IRQHandler — 벡터에서 바로 (0.12)</text>
<rect x="10" y="78" width="320" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="20" y="99" font-size="12">vTaskNotifyGiveFromISR — ready 리스트 (3.36)</text>
<rect x="10" y="126" width="320" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="147" font-size="12">portYIELD_FROM_ISR — PENDSVSET (0.48)</text>
<rect x="10" y="174" width="320" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="20" y="195" font-size="12">xPortPendSVHandler → vTaskSwitchContext</text>
<rect x="10" y="222" width="320" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="20" y="243" font-size="12">결정 · 스택 검사 · 통계 → 훅 (3.32 / 1.80)</text>
<rect x="10" y="270" width="320" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="20" y="291" font-size="12">복원 → ulTaskNotifyTake 나머지 (2.00)</text>
<rect x="350" y="30" width="320" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="360" y="51" font-size="12">_isr_wrapper → 표 조회 → t0_isr (0.88)</text>
<rect x="350" y="78" width="320" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="360" y="99" font-size="12">k_sem_give → z_ready_thread (3.56)</text>
<rect x="350" y="126" width="320" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="360" y="147" font-size="12">update_cache: 결정 + 슬라이스 재설정 (+8.76)</text>
<rect x="350" y="174" width="320" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="360" y="195" font-size="12">z_arm_int_exit — PendSV 요청 (0.48)</text>
<rect x="350" y="222" width="320" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="360" y="243" font-size="12">z_arm_pendsv — 정해진 cache로 교체 (2.24)</text>
<rect x="350" y="270" width="320" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="360" y="291" font-size="12">복원 → k_sem_take 나머지 (0.64)</text>
<line x1="170" y1="64" x2="170" y2="76" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="170" y1="112" x2="170" y2="124" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="170" y1="160" x2="170" y2="172" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="170" y1="208" x2="170" y2="220" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="170" y1="256" x2="170" y2="268" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="510" y1="64" x2="510" y2="76" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="510" y1="112" x2="510" y2="124" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="510" y1="160" x2="510" y2="172" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="510" y1="208" x2="510" y2="220" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="510" y1="256" x2="510" y2="268" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="340" y="322" font-size="11" text-anchor="middle">괄호 = 이 노트에서 잰 µs (QEMU icount). 빨강 = Zephyr 기본에서만 있는 조각</text>
</svg>
```

그림 10 — 같은 "ISR이 태스크 하나를 깨운다"가 두 커널에서 어느 함수로 흘러가는지. FreeRTOS는 **PendSV 안에서 결정**하고(초록 둘), Zephyr는 **ISR 안의 `update_cache`에서 결정**하고 PendSV는 교체만 한다.

소스에서 확인한 위치(V11.3.2 / v4.4.2):

- FreeRTOS: `vTaskGenericNotifyGiveFromISR()`(tasks.c 8315행 부근, `vTaskNotifyGiveFromISR` 매크로가 부른다) — 태스크를 ready 리스트에 넣고 우선순위가 높으면 `*pxHigherPriorityTaskWoken = pdTRUE`. `portYIELD_FROM_ISR(x)` = `portEND_SWITCHING_ISR(x)`(portmacro.h 98 · 111행) — 참이면 `portNVIC_INT_CTRL_REG`에 PENDSVSET. `xPortPendSVHandler`(port.c) → `vTaskSwitchContext()`(tasks.c) 안에서 런타임 통계 → `taskCHECK_FOR_STACK_OVERFLOW()` → `taskSELECT_HIGHEST_PRIORITY_TASK()` → `traceTASK_SWITCHED_IN()`(이 노트의 s4). LEAN이 줄인 것이 앞의 둘이다.
- FreeRTOS trace 훅 `traceISR_ENTER()` · `traceISR_EXIT()` · `traceISR_EXIT_TO_SCHEDULER()`는 include/FreeRTOS.h에 기본값(빈 매크로)이 있다. GCC/ARM_CM3 포트에서는 `xPortSysTickHandler()`(port.c 491행 부근)가 셋을 모두 부르고, `portYIELD_FROM_ISR` = `portEND_SWITCHING_ISR`가 퇴장 쪽 둘(전환 요청이면 `traceISR_EXIT_TO_SCHEDULER`, 아니면 `traceISR_EXIT`)을 부른다. 하지만 **앱 ISR의 진입(`traceISR_ENTER`)은 커널이 대신 불러 줄 수 없다** — 벡터가 앱 함수로 바로 가기 때문이다. 앱 ISR 첫 줄에 직접 넣어야 하고, SystemView · Tracealyzer의 FreeRTOS 연동도 같은 이유로 ISR 진입 기록 호출을 앱 ISR에 넣으라고 안내한다(M3).
- Zephyr: `_isr_wrapper()`(arch/arm/core/cortex_m/isr_wrapper.c)가 `CONFIG_TRACING_ISR`이면 `sys_trace_isr_enter()`, IPSR − 16으로 `_sw_isr_table` 조회, 내 ISR 호출, `sys_trace_isr_exit()`, `z_arm_exc_exit()`. `k_sem_give()`(kernel/sem.c) → `z_unpend_first_thread()` → `z_ready_thread()` → `ready_thread()` → `update_cache()`(kernel/sched.c, `CONFIG_TIMESLICING`이면 `z_reset_time_slice()`). PendSV 핸들러 `z_arm_pendsv`(swap_helper.S)의 첫머리와 복원 뒤에 `z_thread_mark_switched_out/in`(kernel/thread.c, `CONFIG_INSTRUMENT_THREAD_SWITCHING`) — 이것이 tracing 전환 훅이 불리는 자리다.
- Zephyr의 사용자 tracing 훅은 `subsys/tracing/user/tracing_user.c`에 `__weak` 빈 함수로 있다. 사이클 시계는 `timing_counter_get()`(Cortex-M은 `arch/arm/core/cortex_m/timing.c`, DWT 사용), DWT 켜기는 `arch/arm/include/cortex_m/dwt.h`의 `z_arm_dwt_init()`(TRCENA + M7이면 LSR을 보고 LAR 해제) · `z_arm_dwt_init_cycle_counter()`. Zephyr 공식 지연 벤치마크는 `tests/benchmarks/latency_measure`(ISR → 스레드 등, `timing_counter_get` 사용)다.

---

## 10. 실무에서 다시 보기

### 10.1 드론 비행 제어기 — "rate loop가 가끔 늦게 시작한다"

STM32H7급 FC에서 IMU(8 kHz, 125 µs) data-ready EXTI → DMA로 샘플 읽기 → 완료 ISR → rate loop 태스크 통지, 라는 흔한 구조를 생각하자. 증상: 블랙박스 로그에서 루프 시작 시각이 가끔 20 ~ 40 µs 늦다. 이 노트의 순서대로 하면:

1. **계측점**: IMU INT 핀을 타이머 입력 캡처 채널에도 물린다(IRQ 순간을 하드웨어가 기록). DMA 완료 ISR 첫 줄, rate 태스크의 통지 대기 직후에서 CYCCNT를 찍는다. 각 구간을 1 µs 칸 히스토그램 두 개(ISR 단계, 태스크 단계)로 RAM에 모으고, 착륙 후 MAVLink/USB로 내린다.
2. **모양 읽기**: ISR 단계에 날카로운 두 번째 봉우리가 있으면 같은 클럭에서 나온 상위 ISR(예: DShot 출력 타이머 DMA)이 위상 고정으로 겹친다. 선반이 있으면 어떤 크리티컬 섹션(블랙박스 로거의 플래시 버퍼 교체가 단골)이다. 태스크 단계에만 꼬리가 있으면 상위 태스크(혹은 같은 우선순위 태스크의 time slice)다.
3. **태깅 · 끄기 · LR**: 원인 후보마다 [t0, t1]을 남기고, 의심 가는 것을 끈다. 선반이면 예제 10처럼 지연 큰 표본의 쌓인 LR로 크리티컬 섹션 위치를 바로 찾는다.
4. **판단**: 상한 합성이 125 µs 예산 안인지, 측정 최댓값이 몇 시간 · 몇 번의 "드문 경로"를 덮었는지(로거 플러시가 몇 번 지나갔나)를 같이 보고한다.

### 10.2 로봇 관절 모터 컨트롤러 — 20 kHz FOC는 어디서 재나

FOC 전류 루프(50 µs 주기)는 보통 PWM 타이머의 업데이트 이벤트로 ADC를 트리거하고, ADC 완료 ISR에서 계산한다(E2: zero-latency 우선순위). 여기서 재야 할 것은 **PWM 중심 → ADC 완료 ISR 첫 줄 → PWM 비교값 갱신**이다. 타이머 스냅샷이 가장 자연스럽다 — 같은 PWM 타이머의 카운터를 ISR 첫 줄과 갱신 직후에 읽으면 "PWM 주기 안에서 몇 카운트에 끝났나"가 바로 나오고, 그 히스토그램의 최댓값이 "다음 주기 시작 전에 비교값을 썼나"(마감)를 직접 말해 준다. BASEPRI 문턱 위 ISR이라 ①은 PRIMASK를 쓰는 코드(벤더 HAL의 `__disable_irq`)만 들어온다 — 그 코드를 grep으로 찾아 없애는 것이 최악값 관리의 첫 일이다(C7 §6).

### 10.3 Linux SoC 옆의 센서 허브 (Zephyr)

센서 허브가 IMU 타임스탬프를 찍어 SoC로 보낸다면, 타임스탬프의 정확도가 곧 **인터럽트 지연의 지터**다. ISR에서 `k_cycle_get_32()`를 읽어 타임스탬프로 쓰면 ①②③의 지터가 그대로 측정 오차가 된다. 입력 캡처(하드웨어 타임스탬프)로 바꾸면 지터가 0이 되고, 소프트웨어 지연은 "언제 처리했나"의 문제로만 남는다(E5). 예제 8이 보인 대로 Zephyr 기본 설정의 time slicing은 모든 전환에서 IRQ 잠금 구간을 길게 만든다 — 센서 허브처럼 스레드 몇 개의 결정적 응답이 중요한 시스템에서는 `CONFIG_TIMESLICING=n`을 검토하고, 바꾼 뒤 히스토그램으로 확인한다.

### 10.4 Don의 SSD 펌웨어와 연결

SSD 컨트롤러의 NVMe doorbell → 명령 fetch → 처리 시작 경로에서 Don이 했던 "구간별 타임스탬프 + 분포 + 원인 태깅"이 이 노트와 같은 일이다. 차이는 두 가지다. (1) MCU에서는 측정 시계가 사이클 단위라 **계측 자체의 비용**(예제 1)이 결과와 같은 자릿수가 되기 쉽다 — 그래서 observer effect를 숫자로 빼는 습관이 더 중요하다. (2) RTOS에서는 경로 중간에 **스케줄러의 결정**(PendSV, `update_cache`)이 끼고, 그 결정의 비용이 설정(스택 검사, 런타임 통계, time slicing)에 따라 몇 배 달라진다 — "어떤 설정에서 잰 숫자인가"를 항상 같이 적는다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 데이터시트 12사이클을 "우리 인터럽트 지연"이라고 말한다 | 현장에서 수십 µs가 나와 설명을 못 한다 | ①(마스킹 · 상위 ISR)과 wait state가 빠졌다 | 그림 1의 조각별로 말하고, 최악 마스킹 구간을 더한다 |
| ISR 안에서 printf로 지연을 찍는다 | 지연이 ms 단위로 늘고 IRQ가 사라진다 | 출력 자체가 새 지연원(예제 1: 18글자 1.56 ms) | RAM 히스토그램 · 버퍼에 모으고 나중에 출력 |
| DWT를 켰는데 0만 읽힌다 | 모든 측정값 0 또는 디버거 붙였을 때만 동작 | TRCENA 누락, M7 LAR 잠금, M0+는 CYCCNT 없음, QEMU 미구현 | 켜는 순서 + 다른 시계로 자가 시험(예제 4) |
| C 소스 "첫 줄"을 ISR 진입 시점으로 믿는다 | 같은 코드에서 빌드마다 값이 다르다 | 컴파일러가 프롤로그 · 다른 명령을 앞에 둔다(예제 3) | 디스어셈블로 계측 명령 위치 확인, 필요하면 asm 스텁 |
| 평균 · 표준편차로 보고한다 | "평균 17 µs인데 왜 마감을 놓치나" | 마감은 꼬리가 결정한다(평균은 최댓값의 1/4) | min · p50 · p99 · p99.9 · max + 표본 수 · 시간 |
| 짧게 재고 최댓값을 사양으로 쓴다 | 필드에서 더 큰 값이 나온다 | 드문 경로를 못 만났다(예제 10: 2초로는 59 µs를 못 봄) | 경로가 몇 번 지나갔는지로 측정 길이 설계 + 상한 합성 |
| 고정 간격 부하로 시험한다 | 최악값이 이상하게 작다 | 위상이 고정되어 IRQ가 늘 같은 자리에만 떨어짐(C7) | 부하 간격을 난수로, 측정 주기를 홀수로 |
| 계측을 넣은 숫자를 그대로 보고한다 | 실제보다 10 ~ 20% 크다 | 계측점 비용(예제 5: 1.0 µs, Zephyr tracing 2.48 µs) | 계측 없는 빌드의 총합을 따로 재서 같이 적는다 |
| 첫 표본을 버리지 않는다 | 혼자 동떨어진 큰 값 하나 | 측정 시작 전에 쌓인 이벤트(예제 8 첫 시도 476 µs) | 시작 전에 이벤트 상태 초기화(`k_sem_reset` 등), 첫 표본 버림 |
| 원인 태깅이 진행 중인 방해꾼을 놓친다 | 선점당한 방해꾼이 태그에 안 나온다 | 끝 시각이 지난번 값으로 남아 있음(예제 7 첫 시도) | t1 < t0이면 "아직 도는 중"으로 처리 |

---

## 12. 면접에서 이렇게 말한다

**Q.** How do you measure interrupt latency on a microcontroller?

**A.** IRQ가 올라간 순간을 하드웨어가 기록하게 하는 것이 핵심이다. 원인이 타이머면 그 타이머 카운터를 ISR 첫 명령에서 읽고(타이머 스냅샷), 외부 핀이면 입력 캡처 채널로 에지 시각을 래치한다. 장비로는 원인 핀과 디버그 GPIO를 scope에 걸고 무한 잔상으로 최소 · 최대를 본다. 그다음 구간은 DWT CYCCNT로 찍는다. 측정 자체의 비용은 따로 잰다.

> "I make the hardware record the moment the interrupt was raised. If the source is a timer I read that timer's counter in the first instruction of the ISR; if it's an external pin I route it to an input-capture channel too, so the edge time is latched in hardware. On the bench I put the source pin and a debug GPIO on a scope with infinite persistence to see min and max, after subtracting the GPIO set-to-pin delay. For the later stages — kernel call, PendSV, task resume — I take DWT CYCCNT stamps. And I always measure the probe cost itself: on my QEMU setup the stamps added one microsecond to an eight-microsecond path."

**Q.** What contributes to interrupt latency on a Cortex-M?

**A.** 하드웨어 진입(M3/M4 최소 12사이클: 스태킹 · 벡터 · 파이프라인, 플래시 wait state와 진행 중인 버스 접근이 더함), 그 IRQ를 막고 있던 마스킹 구간(PRIMASK/BASEPRI 크리티컬 섹션, 커널 내부 포함 — 가장 긴 것 하나), 같거나 높은 우선순위 ISR의 실행(전부), 그리고 핸들러 프롤로그다. 데이터시트 숫자는 첫 항목의 최선값일 뿐이다.

> "Four things. The hardware entry — twelve cycles minimum on an M3 or M4 for stacking, vector fetch and pipeline refill, plus flash wait states or a slow bus access in flight. Then any masking window: if code is inside a PRIMASK or BASEPRI critical section, the interrupt waits for the rest of it, and only the longest one matters because only one can be active. Then all equal or higher-priority ISRs that are running or pending. Then the handler prologue — a ten-register push is one instruction but about eleven cycles. The twelve-cycle number is only the best case of the first term."

**꼬리 질문**: "tail-chaining은 지연을 줄이나?" → 연속 ISR 사이의 6사이클로 스태킹을 아끼지만, 뒤에서 기다리는 IRQ는 앞 ISR이 **끝날 때까지 전부** 기다린다. 줄어드는 것은 오버헤드지 대기가 아니다.

**Q.** What's the difference between interrupt latency and task response latency in an RTOS?

**A.** 태스크 응답 지연 = 인터럽트 지연 + ISR 안의 커널 호출(ready 리스트) + PendSV 요청과 tail-chain + 스케줄링 결정과 컨텍스트 전환 + 블로킹 API 복귀 + 더 높은 태스크들의 간섭이다. QEMU에서 FreeRTOS는 0.12 µs 대 8.32 µs, Zephyr는 0.88 대 14.08 µs였다. 설계에 쓰는 숫자는 뒤쪽이다.

> "Task response adds the kernel path on top: the FromISR call that moves the task to the ready list, pending PendSV, the tail-chain into PendSV, the scheduling decision and context switch, the rest of the blocking API, and any higher-priority task that runs first. On a QEMU Cortex-M3 at 25 MHz I measured 0.12 µs to the ISR but 8.3 µs to the task with FreeRTOS, and 0.9 versus 14 µs with Zephyr's default config. Splitting it showed where the cost lives — FreeRTOS decides in PendSV, so stack checking and run-time stats land there; Zephyr decides inside k_sem_give, and with time slicing on that call alone was 12 µs."

**Q.** Your latency histogram has two peaks and a long tail. How do you find the causes?

**A.** 모양으로 가설을 세운다 — 같은 꼴의 날카로운 두 번째 봉우리는 위상 고정된 주기 간섭, 평평한 선반은 무작위 위상의 차단 구간(폭이 길이, 면적이 듀티), 드문 작은 무리는 원인 조합. 그다음 방해꾼마다 실행 구간을 남겨 표본마다 겹친 원인을 태깅하고, 원인을 하나씩 꺼서 그 모양이 사라지는지로 증명한다. 태그가 안 붙은 꼬리는 계측하지 않은 원인이다.

> "First I read the shape. A second sharp peak with the same width is periodic interference that's phase-locked — something derived from the same clock. A flat shelf is a blocking section hit at random phase: its width is the section length, its area is the duty. Small clusters far out are combinations of causes. Then I prove it: each suspect records its last start and end time, the measuring code computes when the IRQ was raised and tags every sample with the suspects that overlapped. Finally I switch suspects off one at a time. In my experiment the 4 µs peak vanished with the higher-priority timer off, the shelf vanished with the critical section off, and an untagged tail turned out to be a higher-priority task's own sleep/wake path — it only disappeared when I removed that task."

**Q.** Why is average latency not useful, and how long do you need to measure?

**A.** 마감은 모든 job에 걸리므로 꼬리가 결정한다. 내 측정에서 평균 17 µs, p99 54 µs, 최댓값 74 µs였다. 드문 경로는 확률로 계산한다 — 0.39% 시간을 차지하는 60 µs 구간의 마지막 1 µs를 99% 확률로 보려면 7만 표본이 필요했다. 그래서 측정 길이는 "그 경로가 몇 번 지나갔나"로 설계하고, 못 덮는 부분은 조각별 최악값을 max-plus로 합성한 상한으로 덮는다.

> "Deadlines apply to every job, so the tail decides. In one run the mean was 17 µs, p99 54, max 74 — the mean was a quarter of the max. Rare paths are a probability problem: a 60-microsecond section that occupies 0.39 percent of the time needs about seventy thousand samples before you see its worst case with 99 percent confidence, and my two-second run stopped at 58.8 instead of 61.5. So I size the measurement by how many times the rare path executed, keep a worst-case recorder in the field build, and back it up with a composed bound: base path, plus the longest blocking section, plus the sum of all higher-priority interference."

**Q.** How do you enable the DWT cycle counter, and what can go wrong?

**A.** DEMCR.TRCENA를 켜고, M7에서 LSR이 잠금을 보이면 LAR에 0xC5ACCE55를 쓰고, CYCCNT를 0으로 하고 DWT_CTRL.CYCCNTENA를 켠다. 함정은 TRCENA를 디버거가 대신 켜 둬서 단독 부팅에서만 0이 되는 경우, M7 잠금, M0/M0+에는 카운터가 없음, 32비트 랩, 저전력 · halt 중 정지. 그래서 켠 뒤 다른 시계로 자가 시험을 한다 — QEMU는 NOCYCCNT=0인데도 CYCCNT가 0에 머문다.

> "Set TRCENA in DEMCR, unlock through the lock access register with 0xC5ACCE55 if the M7's lock status says it's locked, zero CYCCNT and set CYCCNTENA in DWT_CTRL. The classic failures are a debugger having set TRCENA so it only breaks on a standalone boot, the M7 lock, M0 and M0+ simply not having the counter, the 32-bit wrap — under nine seconds at 480 MHz — and the counter stopping in sleep or when halted. So I self-test it against another clock before trusting it. QEMU is a nice example: it reports NOCYCCNT as zero but the counter never moves."

**Q.** How would you find the longest critical section in a large codebase?

**A.** 두 가지를 쓴다. 링커 `--wrap`으로 `vPortEnterCritical/ExitCritical`을 감싸 호출 위치별 최댓값을 모으는 방법, 그리고 지연이 큰 표본에서 ISR이 하드웨어가 쌓은 PC와 LR을 기록하는 방법이다. PC는 마스크를 푼 함수(`vPortExitCritical`)를 가리키고 LR이 호출자를 가리킨다 — 꼬리 호출이면 한 단계 위를 가리키니 디스어셈블로 확인한다. PRIMASK를 직접 쓰는 코드는 grep으로 따로 찾는다.

> "Two complementary ways. Wrap the kernel's enter and exit critical functions with the linker's --wrap option and keep the maximum per call site. Or let the hardware tell me: when the latency of a sample is large, the ISR records the PC and LR from its own exception frame. The PC is the instruction right after the unmask, so it lands in vPortExitCritical; the LR names the caller. With tail calls the LR points one level higher, so I check the disassembly or build that test with sibling-call optimization off. In my test that pointed straight at a 60 µs log flush that ran once in four hundred calls, while the most frequent site was a harmless 12 µs one. Direct PRIMASK users like vendor HAL code I grep for separately."

**Q.** How do you make sure your instrumentation doesn't distort the measurement?

**A.** 계측 동작 하나의 비용을 재고, 계측을 넣은 빌드와 뺀 빌드의 총합을 비교해 차이를 보고한다. ISR에서는 RAM에 숫자만 쓰고 출력은 나중에 한다. 계측이 경로 길이를 바꾸면 다른 주기 이벤트와의 위상도 바뀌므로, 히스토그램 모양 결론은 계측을 줄인 빌드로 한 번 더 확인한다. 정말 비침투가 필요하면 핀 하나 + scope나 ETM을 쓴다.

> "I measure the probe cost itself and report the instrumented and uninstrumented totals side by side — in my FreeRTOS case one microsecond on eight. Inside ISRs I only store numbers to RAM; formatting and printing happen afterwards at low priority, because one printf on a real 115200 UART is over a millisecond. I also remember that a longer path shifts the phase against other periodic events, so I confirm histogram shapes with a lighter build. When it truly must be non-intrusive I use a single GPIO and a scope, or ETM trace."

---

## 13. 직접 해보기

1. **개념**: 1 kHz 제어 루프의 IRQ → 태스크 지연을 4시간 측정했더니 최댓값 40 µs였다. 이 숫자를 사양서에 "최악 40 µs"로 쓰면 안 되는 이유 세 가지를 쓰라.
   정답/힌트: 측정 최댓값은 하한(드문 경로 미포착, §7.4), 측정 설정(계측 비용 · 설정 옵션)이 제품과 다를 수 있음, 위상 고정 부하라면 최악 위상을 못 만났을 수 있음 — 상한 합성(§7.2)과 경로 통과 횟수로 보완.
2. **모양 읽기**: 어떤 히스토그램이 기본값 3 µs에 70%, 3 ~ 11 µs 사이에 평평하게 25%, 23 µs에 5%의 날카로운 봉우리를 갖는다. 원인 후보 두 개와 각각의 길이 · 듀티를 추정하라.
   정답/힌트: 폭 8 µs · 듀티 약 25%의 차단 구간(선반), 20 µs만큼 밀어내는 위상 고정 간섭이 20번에 1번(봉우리).
3. **코드**: 예제 2에 V4를 더하라 — 핸들러 첫 명령에서 `LED = 1`, `isr_c` 끝에서 `LED = 0`. 예제 1의 비용표로 "scope에 보일 펄스 폭"을 예측하고, `objdump`로 명령 수를 세어 확인하라.
   정답/힌트: 펄스 폭 = 두 쓰기 사이 명령 수 × 32 ns(QEMU). 실칩에서는 여기에 set-to-pin 지연이 양쪽 에지에 붙는다.
4. **코드**: 예제 7에 다섯 번째 방해꾼을 넣어라 — TIMER0과 **같은** 우선순위(0x40)의 다른 ISR(예: UART0 TX IRQ를 NVIC STIR로 무작위 pend, 본문 8 µs). 히스토그램에서 어떤 모양이 생기고, 태그로 어떻게 확인하나?
   정답/힌트: 같은 우선순위는 선점하지 못하므로 무작위 위상의 선반(폭 약 8 µs)이 하나 더 생긴다. 같이 pend되면 tail-chain 순서(낮은 IRQ 번호 먼저)도 영향을 준다(B2).
5. **측정**: 예제 8을 `CONFIG_TIMESLICING=y`에 `CONFIG_TIMESLICE_PRIORITY`를 바꿔(측정 대상 스레드 우선순위보다 낮은 스레드만 슬라이스) 다시 돌려라. 태그 없는 ISR 꼬리(21.96 µs)가 어떻게 바뀌나?
   정답/힌트: `z_time_slice_size()`가 0이 되는 스레드로의 전환에서는 `z_add_timeout`이 빠진다(kernel/timeslicing.c) — 해당 전환의 IRQ 잠금 구간이 짧아지는지 히스토그램으로 확인한다.
6. **분석**: 예제 11의 확률 모델로, 예제 10에서 `log_flush` 호출 빈도를 1/4000로 줄이면 "59 µs 이상을 99% 확률로 한 번 보기"에 10 kHz로 몇 초가 필요한지 계산하라.
   정답/힌트: p가 약 1/10이 되므로 약 10배, 70초 남짓. 측정 대신 코드 리뷰로 60 µs 구간을 찾는 편이 싸다는 결론.
7. **실칩 과제**(보드가 있으면): STM32 보드에서 예제 4를 돌려 CYCCNT가 오르는지 보고, EXTI 핀을 타이머 입력 캡처에도 연결해 "IRQ 순간 → ISR 첫 명령"을 CYCCNT와 캡처 값으로 잰 뒤, 같은 구간을 GPIO + scope로 재서 두 값의 차이(= set-to-pin 지연 + 클럭 동기화)를 구하라.
   정답/힌트: 캡처 타이머 클럭과 CYCCNT 클럭(코어)이 다르면 단위를 맞춘다. 차이가 수 ns ~ 수십 ns 규모인지 확인.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| interrupt latency | 인터럽트 지연 | IRQ가 올라간 순간 → ISR 첫 (쓸모 있는) 명령 |
| task response latency | 태스크 응답 지연 | IRQ 순간 → 그 이벤트를 처리하는 태스크의 첫 명령 |
| jitter | 지터 | 같은 지연의 흔들림. 보통 peak-to-peak와 히스토그램으로 본다 |
| masking window | 마스킹 구간 | PRIMASK/BASEPRI가 올라가 IRQ가 들어오지 못하는 시간 |
| blocking term | 차단 항 | 낮은 우선순위 코드가 잡은 비선점 구간. 하나만 더한다(max) |
| interference term | 간섭 항 | 높은 우선순위 ISR · 태스크의 실행. 모두 더한다(Σ) |
| max-plus composition | max-plus 합성 | 기본 + max(차단) + Σ(간섭)으로 최악값 상한을 세우기 |
| timer snapshot | 타이머 스냅샷 | IRQ를 낸 타이머의 카운터를 읽어 "IRQ 이후 경과"를 구하는 방법 |
| input capture | 입력 캡처 | 핀 에지 순간의 타이머 값을 하드웨어가 래치하는 기능(E5) |
| set-to-pin delay | 쓰기 → 핀 지연 | GPIO 레지스터 쓰기부터 패드 전압이 바뀔 때까지 |
| infinite persistence | 무한 잔상 | scope가 모든 트리거의 파형을 겹쳐 남기는 모드. 지터가 띠로 보인다 |
| DWT CYCCNT | 사이클 카운터 | DWT 블록의 32비트 코어 클럭 카운터(Armv7-M · v8-M Mainline) |
| TRCENA | 트레이스 활성 | DEMCR bit 24. DWT · ITM에 전원을 넣는다 |
| LAR / LSR | 잠금 접근 / 상태 | 일부 M7의 DWT 잠금. LAR에 0xC5ACCE55를 써서 푼다 |
| EXCCNT | 예외 오버헤드 카운터 | 예외 진입 · 퇴장 사이클을 세는 8비트 DWT 카운터 |
| observer effect | 관측 효과 | 계측이 재려는 타이밍 자체를 바꾸는 것 |
| cause tagging | 원인 태깅 | 표본마다 그 시간에 겹친 방해꾼을 비트로 기록 |
| ablation | 하나씩 끄기 | 원인 후보를 꺼서 그 모양이 사라지는지로 인과를 증명 |
| phase lock | 위상 고정 | 두 주기 이벤트가 같은 클럭에서 나와 늘 같은 간격으로 만나는 것 |
| tail call | 꼬리 호출 | 함수 끝 호출을 `b`로 바꿔 프레임을 없애는 최적화. LR 추적을 흐린다 |

---

## 15. 요약 & 체크리스트

인터럽트 지연은 하드웨어 진입 · 마스킹 구간 · 상위 ISR · 프롤로그의 합이고, 태스크 응답 지연은 여기에 ISR 안 커널 호출 · PendSV · 전환 결정 · API 복귀 · 상위 태스크 간섭을 더한 것이다. 데이터시트의 12사이클은 그중 한 조각의 최선값일 뿐이다. 측정은 IRQ 순간을 하드웨어가 기록하는 방법(타이머 스냅샷 · 입력 캡처 · scope)으로 시작하고, 뒤 구간은 CYCCNT나 trace 훅으로 찍으며, 계측 비용을 따로 재서 뺀다. QEMU Cortex-M3에서 FreeRTOS의 IRQ → 태스크는 8.32 µs(lean 6.84), Zephyr는 14.08 µs(슬라이스 끔 5.32)였고, 구간 분해는 FreeRTOS의 비용이 PendSV 안의 결정에, Zephyr 기본의 비용이 ISR 안의 `k_sem_give`(슬라이스 재설정)에 있음을 보였다. 지터는 히스토그램으로 읽는다 — 날카로운 두 번째 봉우리는 위상 고정 간섭, 평평한 선반은 무작위 위상의 차단 구간, 먼 작은 무리는 원인 조합, 홀로 먼 점은 먼저 측정 버그. 모양으로 가설을 세우고, 원인 태깅으로 상관을 보이고, 하나씩 끄기로 증명한다. 최악값은 측정 최댓값(하한)과 max-plus 합성(상한)을 같이 보고하고, 측정 길이는 드문 경로가 몇 번 지나갔는지로 정한다. 가장 긴 크리티컬 섹션은 지연이 큰 표본의 쌓인 LR이 이름과 줄 번호로 알려 준다.

- [ ] 인터럽트 지연과 태스크 응답 지연을 그림 1의 조각으로 나눠 말할 수 있다
- [ ] Cortex-M3/M4 진입 12사이클이 하한인 이유(wait state, 버스, 마스킹, 상위 ISR)를 설명할 수 있다
- [ ] 타이머 스냅샷 · GPIO + scope · DWT CYCCNT · 트레이스가 각각 어디서부터 재는지 안다
- [ ] scope 무한 잔상 화면에서 최소 · 최대 지연을 읽고 set-to-pin 지연을 뺄 수 있다
- [ ] DWT CYCCNT를 켜는 순서(TRCENA, M7 LAR, CYCCNTENA)와 자가 시험을 코드로 쓸 수 있다
- [ ] 계측 비용을 재고, 계측 있는 · 없는 총합을 같이 보고할 수 있다
- [ ] trace 훅(`traceTASK_SWITCHED_IN`, Zephyr `sys_trace_*_user`)으로 커널 안 구간을 잴 수 있다
- [ ] 히스토그램의 봉우리 · 선반 · 꼬리 · 홀로 먼 점을 원인 후보에 매핑할 수 있다
- [ ] 원인 태깅과 하나씩 끄기로 지터 원인을 증명할 수 있다
- [ ] 쌓인 PC/LR로 가장 긴 크리티컬 섹션을 찾고, 꼬리 호출 함정을 안다
- [ ] max-plus 합성으로 상한을 세우고 측정 최댓값과 비교할 수 있다
- [ ] 드문 경로를 99% 확률로 보기 위한 표본 수를 계산할 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM J7 | 인터럽트 지연 · 지터 측정 (GPIO 토글 + scope, DWT, 최악 경로 찾기) | §1 정의, §2 측정 방법, §5 히스토그램, §6 모양 → 원인 · 태깅 · 끄기, §7 최악 경로 · 상한 합성 · 측정 시간 | super-loop에서 ISR → 메인 루프 플래그 폴링 지연(루프 한 바퀴 길이가 곧 최악 지연), RTOS 없이 같은 히스토그램 · 태깅을 하는 법 — 이 노트 링크 |
| BM M7 | 타이밍 측정 기법 (GPIO 토글, DWT CYCCNT, 타이머 스냅샷, 측정이 측정을 바꾸는 문제) | §2 네 방법과 선택표, §3 비용 · DWT 켜기 · 자가 시험 · observer effect 규칙 | 레지스터 수준 GPIO 토글 매크로(BSRR 등 칩별), 벤더 HAL 없이 쓰는 최소 측정 헤더 — 이 노트 링크 |

---

## 참고 자료

- Arm, **Cortex-M3 Technical Reference Manual r2p0 (ARM DDI 0337G)** — §5.5 예외 진입 · "12-cycle latency", §5.6 tail-chaining("six cycles"), §5.7 late-arriving, 명령 타이밍(LDM/STM). 사본: https://www.keil.com/dd/docs/datashts/arm/cortex_m3/r2p0/ddi0337g_cortex_m3_r2p0_trm.pdf
- Joseph Yiu, "A Beginner's Guide on Interrupt Latency — and Interrupt Latency of the Arm Cortex-M processors", Arm Community blog, 2016 (M0 16 · M0+ 15 · M3 12 · M4 12사이클, zero wait state 조건): https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/beginner-guide-on-interrupt-latency-and-interrupt-latency-of-the-arm-cortex-m-processors
- Arm, **Armv7-M Architecture Reference Manual** (ARM DDI 0403) — DWT(CYCCNT, EXCCNT, DWT_CTRL 비트), DEMCR.TRCENA
- CMSIS-Core 헤더(VM의 Zephyr 모듈 `modules/hal/cmsis/CMSIS/Core/Include/core_cm3.h`, `core_cm7.h`) — `DWT_Type`(CYCCNT 0x004, EXCCNT 0x00C, M7의 LAR 0xFB0 · LSR 0xFB4), `DWT_CTRL_NOCYCCNT_Pos` 25, `DWT_CTRL_EXCEVTENA_Pos` 18, `CoreDebug_DEMCR_TRCENA_Pos` 24
- STMicroelectronics, **RM0090** (STM32F405/415, F407/417, F427/437, F429/439 Reference Manual) — Flash 접근 시간과 CPU 클럭의 관계 표(wait state 수)
- FreeRTOS Kernel V11.3.2 소스(`/var/rtoslab/src/FreeRTOS-Kernel`): `tasks.c`(`vTaskSwitchContext`의 `traceTASK_SWITCHED_IN`, `vTaskGenericNotifyGiveFromISR`), `include/FreeRTOS.h`(trace 매크로 기본값), `portable/GCC/ARM_CM3/port.c`(`xPortSysTickHandler`의 `traceISR_ENTER`, `vPortExitCritical`), `portable/GCC/ARM_CM3/portmacro.h`(`portEND_SWITCHING_ISR`, `portYIELD_FROM_ISR`) — https://github.com/FreeRTOS/FreeRTOS-Kernel
- Zephyr v4.4.2 소스(`/var/rtoslab/zephyrproject/zephyr`): `arch/arm/core/cortex_m/isr_wrapper.c`, `swap_helper.S`(`z_arm_pendsv`), `vector_table.S`(`sys_clock_isr`), `arch/arm/include/cortex_m/dwt.h`(`dwt_access`, `z_arm_dwt_init`), `arch/arm/core/cortex_m/timing.c`, `kernel/sched.c`(`update_cache`, `ready_thread`), `kernel/timeslicing.c`(`z_reset_time_slice`), `kernel/sem.c`, `kernel/thread.c`(`z_thread_mark_switched_in/out`), `subsys/tracing/user/tracing_user.c`, `tests/benchmarks/latency_measure`
- Zephyr 문서 — Tracing: https://docs.zephyrproject.org/latest/services/tracing/index.html
- SEGGER SystemView: https://www.segger.com/products/development-tools/systemview/ · Percepio Tracealyzer: https://percepio.com/tracealyzer/
- QEMU 문서 — Arm MPS2 and MPS3 boards: https://www.qemu.org/docs/master/system/arm/mps2.html
- Don의 Embedded Linux 노트: EL N3(지연 측정 — cyclictest, 히스토그램, 측정 시간; Linux 쪽은 거기서)
- 같은 RTOS 노트 세트: A1(지연 · 지터 용어), A7(첫 IRQ → 태스크 측정), B2(예외 진입 · tail-chain), B3(NVIC · BASEPRI), C2(컨텍스트 스위치), C7(크리티컬 섹션 L 스윕, `--wrap` 계측), C8(커널 진입), E1 · E2 · E4 · E5 · E7, M3 · M5 · M6, N3
