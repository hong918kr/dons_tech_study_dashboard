---
id: BM-A10
time: 110분
updated: 2026-11-15
---
# BM-A10 인터럽트 지연 측정: DWT CYCCNT, tail-chaining, late arrival

## 학습 목표

- Cortex-M3/M4 예외 진입·복귀의 사이클 예산(진입 12, tail-chaining 6, late arrival, pop pre-emption, lazy FP 17 워드, 플래시 wait state)을 **모델로 계산**하고, 각 숫자가 어떤 문서 값이고 어떤 것이 내 가정인지 구분해 말할 수 있다.
- DWT CYCCNT 로 "트리거 → ISR 첫 명령" 을 재는 펌웨어를 설계할 수 있다: TRCENA/CYCCNTENA, back-to-back 보정, naked ISR, 감김 안전 차이, 그리고 **모르는 트리거 경로는 차분 측정으로 소거**한다는 원칙.
- QEMU 가 왜 이 측정을 못 하는지 **직접 확인한 근거**(DEMCR·DWT 가 RAZ/WI, CYCCNT = 0)로 설명한다.
- UART 덤프를 검증(잘림·손상·멈춘 카운터)하고 min/p50/p99/max, 히스토그램, **쌍봉(ART hit/miss)** 을 찾아 모델과 비교하는 분석기를 만든다.

## 1. 문제

"인터럽트 지연이 몇 사이클이죠?" 는 모터 제어·RF 타이밍·안전 차단을 다루는 펌웨어 면접의 단골이고, 정답 "12 사이클" 다음에 오는 꼬리질문 — **"그걸 어떻게 재셨어요? 168 MHz 에서 플래시 5 WS 면? FPU 켜면?"** — 에서 실력이 갈린다. Don 이 SSD 컨트롤러에서 NVMe 커맨드 지연을 타임스탬프로 쪼개던 것과 같은 일을, 이번엔 코어 사이클 단위로 한다.

**정직하게 먼저 밝힌다.** 이 아이템의 env 는 `board` 인데 지금 보드가 없다. 그리고 QEMU 는 사이클을 세지 않는다 — 이 레슨에서 `mps2-an386` 으로 직접 확인했다(§5 Part B: DEMCR 쓰기 무시, DWT 영역은 "unassigned PPB", CYCCNT 는 항상 0). 그래서 이 레슨은 세 조각이다.

| 조각 | 무엇 | 어디서 돌았나 | 숫자의 지위 |
|---|---|---|---|
| A | Cortex-M4 예외 타이밍 **사이클 모델** + 사이클 단위 이벤트 시뮬레이터 | 맥, `solution.c` | **모델 값** (ARM 문서 대표값 + 내 가정) |
| B | 측정 펌웨어 `board/latency_fw.c` (STM32F407) | VM 에서 **빌드만**, 보드 실행 없음 | 측정 숫자 **없음** |
| C | 덤프 분석기 | 맥, `solution.c` | 입력은 **합성(SYNTHETIC) 덤프** — 모델 + 고정 시드 지터 |

이 레슨의 어떤 표도 실측이 아니다. 실측은 보드를 붙인 날 `latency_fw.c` 를 올려 Part C 분석기에 덤프를 넣을 때 처음 생긴다. 인터럽트 지연의 깊은 이야기(마이크로아키텍처, 버스 매트릭스, M7 캐시)는 ARM-A08 (다음 단계) 의 몫이다.

아래 그림은 모델(0 WS)이 그리는 네 가지 진입 경로다. 숫자는 `solution.c` / `demo_sweep.c` 의 출력이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">예외 진입 경로 — 모델 (0 WS, 사이클). 측정 아님</text>
  <text x="10" y="50" fill="currentColor">① 정상 진입</text>
  <rect x="130" y="36" width="144" height="22" class="f-warn" stroke="currentColor"/>
  <text x="138" y="51" fill="currentColor">스태킹 8 워드 ∥ 벡터 읽기</text>
  <rect x="274" y="36" width="120" height="22" class="f-ok" stroke="currentColor"/>
  <text x="282" y="51" fill="currentColor">ISR 본문</text>
  <text x="130" y="74" fill="currentColor">IRQ 도착 → 첫 명령 = 12</text>
  <text x="10" y="112" fill="currentColor">② tail-chaining</text>
  <rect x="130" y="98" width="120" height="22" class="f-ok" stroke="currentColor"/>
  <text x="138" y="113" fill="currentColor">ISR A</text>
  <rect x="250" y="98" width="72" height="22" class="f-acc" stroke="currentColor"/>
  <text x="256" y="113" fill="currentColor">6</text>
  <rect x="322" y="98" width="120" height="22" class="f-ok" stroke="currentColor"/>
  <text x="330" y="113" fill="currentColor">ISR B</text>
  <text x="456" y="106" fill="currentColor">언스태킹 12 + 재스태킹 12 = 24 대신 6</text>
  <text x="456" y="122" fill="currentColor">(스택 프레임을 그대로 재사용)</text>
  <text x="10" y="174" fill="currentColor">③ late arrival</text>
  <rect x="130" y="160" width="144" height="22" class="f-warn" stroke="currentColor"/>
  <text x="138" y="175" fill="currentColor">낮은 IRQ 의 스태킹</text>
  <path d="M166 150 L166 160" stroke="currentColor"/>
  <text x="140" y="146" fill="currentColor">높은 IRQ 도착 (d=3)</text>
  <rect x="274" y="160" width="100" height="22" class="f-ok" stroke="currentColor"/>
  <text x="280" y="175" fill="currentColor">높은 ISR</text>
  <rect x="374" y="160" width="72" height="22" class="f-acc" stroke="currentColor"/>
  <text x="380" y="175" fill="currentColor">6</text>
  <rect x="446" y="160" width="100" height="22" class="f-mut" stroke="currentColor"/>
  <text x="452" y="175" fill="currentColor">낮은 ISR</text>
  <text x="130" y="198" fill="currentColor">높은 쪽 지연 = 12 − 3 = 9 (최소 6, 모델 가정) · 낮은 쪽은 뒤에 꼬리 물기</text>
  <text x="10" y="244" fill="currentColor">④ pop pre-emption</text>
  <rect x="130" y="230" width="120" height="22" class="f-ok" stroke="currentColor"/>
  <text x="138" y="245" fill="currentColor">ISR A</text>
  <rect x="250" y="230" width="48" height="22" class="f-mut" stroke="currentColor"/>
  <text x="254" y="245" fill="currentColor">pop…</text>
  <path d="M298 220 L298 230" stroke="currentColor"/>
  <text x="262" y="216" fill="currentColor">B 도착 (d=4)</text>
  <rect x="298" y="230" width="72" height="22" class="f-acc" stroke="currentColor"/>
  <text x="304" y="245" fill="currentColor">6</text>
  <rect x="370" y="230" width="100" height="22" class="f-ok" stroke="currentColor"/>
  <text x="376" y="245" fill="currentColor">ISR B</text>
  <text x="130" y="270" fill="currentColor">언스태킹을 버리고 꼬리 물기로: B 지연 6 (안 하면 (12−4)+12 = 20)</text>
  <rect x="10" y="288" width="720" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="303" fill="currentColor">플래시 5 WS + ART miss: 벡터 읽기 +5, 첫 명령 줄 +5 → 진입 22, 꼬리 물기 16.  FPCA=1 · LSPEN=0: +17 → 29.</text>
  <text x="20" y="317" fill="currentColor">ARM 문서 값 = 진입 12, 꼬리 물기 6 (0 WS). 복귀 12, late 최소 6, FP 워드당 1, miss 당 WS = 가정.</text>
</svg>
```

### Part A — 사이클 모델과 사건 시뮬레이터 (맥)

- 파라미터 `cm_params_t CM4 = {entry 12, tail 6, exit 12, late_min 6, fp_words 17}` 와 설정 `cm_cfg_t {ws, art_hit, vec_in_ram, fpca, aspen, lspen}`.
- **TODO A1–A5**: `fetch_penalty` (miss 당 ws, 벡터 + 첫 명령 두 번), `entry_cycles` (FP 즉시 저장 +17), `tail_cycles`/`exit_cycles`, `late_ready` (late arrival 의 시작 시각), `lazy_stall_cycles`.
- 주어진 엔진 `sim_run()` 이 1 사이클씩 NVIC 규칙(선점은 "더 급할 때만", 동률은 번호 작은 쪽, 끝날 때 기다리는 게 있으면 꼬리 물기, 스태킹 중 더 급한 것 → late, 언스태킹 중 → pop)을 돌린다. `latency_model(kind, d)` 가 이름 붙은 시나리오를 사이클로 바꾼다.
- 검사: 시나리오 18 개의 값, 순서 관계(꼬리 물기 < 복귀+진입, late·pop 은 정상 진입보다 나쁘지 않다, WS 에 단조), 무작위 3000 개 사건 집합의 불변식.

### Part B — 측정이 틀리는 길 (주입하고 잡는다)

- **TODO B1** `cyc_delta` (감김 안전), **TODO B2** `lat_correct` (보정, 음수면 `ERR_NEGATIVE`).
- 주입: CYCCNT 감김(0xFFFFFFF8 → 0x0C), 보정 누락, **멈춘 CYCCNT** (QEMU 실측과 같은 all-zero 덤프), UART 덤프 **모든 길이로 잘림**, **모든 위치 × 255 값의 1 바이트 손상**, late arrival 창의 경계(d = 11 대 12), lazy FP 의 세 경우.
- 보드 펌웨어 `board/latency_fw.c` 는 이 함정들을 피하도록 설계했다(§5): naked ISR 의 **첫 명령이 표본**, r1 에 남긴 CYCCNT 주소, 64 번 back-to-back 최소값 보정, 표본마다 타임아웃.

### Part C — 덤프 분석기 (합성 입력)

- **TODO C1** `dump_finish` (개수·체크섬·멈춤 검사), **TODO C2** `stats_compute` (min/max/p50/p99 nearest-rank, 평균×100), **TODO C3** `bimodal_detect` (가장 큰 간격으로 두 무리 나누기).
- 합성 덤프 8 시나리오 × 400 표본 → 파싱 → 보정 → 통계 → 쌍봉 → **모델 대 "측정(SYNTHETIC)" 비교표**. 합성이므로 일치하는 게 당연하다 — 이 Part 가 검증하는 건 **도구** 이고, 숫자의 진짜 비교는 보드 덤프가 들어오는 날 한다.

## 2. Problem (English)

> **Interrupt latency on a Cortex-M4 — model it, instrument it, analyse it.**
>
> 1. *Model.* With zero-wait-state memory, exception entry takes 12 cycles from IRQ assertion to the first handler instruction and tail-chaining takes 6. Assume exception return takes 12, a late-arriving higher-priority IRQ needs at least 6 cycles from its arrival (it reuses the stacking already in progress), each flash access that misses the ART cache costs `ws` extra cycles (vector fetch + first instruction line), and an FP context costs 17 extra words (1 cycle each) that are pushed at entry when `FPCCR.LSPEN = 0`, or at the handler's first FP instruction when `LSPEN = 1`.
>    Implement `fetch_penalty`, `entry_cycles`, `tail_cycles`, `exit_cycles`, `late_ready`, `lazy_stall_cycles`. A provided cycle-stepped simulator uses them.
>    Expected: entry 12; tail gap 6; back-to-back gap 24; 5 WS miss 22, 5 WS hit 12, 5 WS miss with VTOR in SRAM 17; FP full 29, FP lazy 12 (+17 stall if the ISR uses FP); late arrival `d = 3` → 9, `d = 8` → 6; pop pre-emption `d = 4` → 6.
> 2. *Instrument.* Firmware (STM32F407, not run here) samples `DWT->CYCCNT` immediately before the trigger store and as the first instruction of a naked ISR, calibrates with back-to-back reads, and dumps lines `H,core_hz,n` / `C,calib` / `S,id,name,n` / `D,id,idx,t0,t1` / `E,total,sum` over UART, where `sum = core_hz + calib + Σ(t1 − t0) mod 2³²`.
> 3. *Analyse.* `uint32_t cyc_delta(t0, t1)` must be wrap-safe. `lat_correct(raw, calib, &out)` returns `ERR_NEGATIVE` if `raw < calib`. `dump_finish()` returns `ERR_TRUNCATED` (missing `E`, count mismatch), `ERR_CHECKSUM`, or `ERR_STUCK` (every `t0` and every delta is 0). `stats_compute()` gives min/max/p50/p99 by nearest rank and `mean×100`. `bimodal_detect(sorted, n, min_gap = 3, min_frac = 5 %)` splits at the largest gap.
>
> Example: `cyc_delta(0xFFFFFFF8, 0x0000000C) = 20`; a dump read from QEMU (all zeros) → `ERR_STUCK`; a synthetic `entry_ws5_art` scenario (20 % ART misses) → bimodal 14 / 24.
>
> Follow-ups: Why is your measured number 14, not 12? How do you measure tail-chaining without knowing the trigger path? What does a 24 MHz logic analyser see? What changes on a Cortex-M7?

## 3. 시작 코드

<!-- code: starter.c -->

맥에서 (호스트 C, 보드·VM 필요 없음):

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cd <ROOT>/lessons/BM/BM-A10
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/BM-A10_st && /tmp/BM-A10_st    # 17/54, exit 1
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-A10_so && /tmp/BM-A10_so    # 54/54, exit 0
```

보드 펌웨어 빌드 확인과 QEMU CYCCNT 확인 (Lima VM `elinux`, 스크립트가 `/tmp/BM-A10_work` 로 복사해서 빌드):

```bash
limactl shell elinux -- bash -lc 'bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-A10/board/build_vm.sh all'
```

시작 코드 실행 결과 (실제 출력 일부): 모든 비용이 0 이라 시뮬레이터는 끝나지만 숫자가 전부 0, 분석기는 잘린 덤프 113 개와 손상 덤프 1571 개를 받아들이고, QEMU 덤프를 OK 라고 한다.

```text
   entry, 0 WS                                0 (expect 12)
FAIL  entry, 0 WS  (line 446)
   ...
   random: 3000 event sets, violations once=0 prio=0 min=8086 unterminated=0
   wrap: unsigned delta=12, naive int64 delta=-4294967276
   QEMU-style all-zero dump -> OK
   truncation: 362 lengths tried, accepted=113, other-code=0
   1-byte corruption: 92565 variants, accepted=27187 (name/case/comment only), accepted with different data=1571
   d=11: order 0,1 how(high)=nested how(low)=entry
17/54 passed
```

## 4. 힌트

<details><summary>힌트 1 — 플래시 대기는 "몇 번 접근하나" 의 문제</summary>

진입에서 플래시를 건드리는 건 (1) 벡터 표에서 핸들러 주소 읽기 (2) 핸들러 첫 명령 줄 읽기. 각각 miss 면 `ws` 사이클. 벡터 표가 SRAM 이면 (1) 은 공짜, ART hit 면 둘 다 공짜. 스태킹은 SRAM 쓰기라 WS 와 무관(이 모델에서).

</details>

<details><summary>힌트 2 — late arrival 은 "다시 처음부터" 가 아니다</summary>

스태킹은 어느 IRQ 를 위해서든 같은 8 워드다. 그래서 더 급한 IRQ 가 스태킹 중에 오면 스태킹은 계속하고 **벡터만** 바꾼다. 시작 시각은 "원래 준비 시각" 과 "도착 + 벡터 재읽기 최소 시간" 중 늦은 쪽: `max(ready_old, t + late_min + penalty)`.

</details>

<details><summary>힌트 3 — 감김과 보정</summary>

`uint32_t` 뺄셈은 2³² 를 법으로 정확하다. 64 비트로 바꾸면 오히려 틀린다(음수). 보정은 "아무 일도 없을 때 같은 읽기 모양이 내는 차이" — 그보다 작은 raw 는 물리적으로 불가능하니 조용히 감기게 두지 말고 에러로.

</details>

<details><summary>힌트 4 — 쌍봉은 정렬 후 가장 큰 틈</summary>

정렬된 값에서 `v[i+1] − v[i]` 가 가장 큰 곳으로 나눈다. 틈이 지터(±1)보다 충분히 크고(≥ 3) 양쪽 모두 무시 못 할 비율(≥ 5 %)일 때만 쌍봉. 양쪽 중앙값이 hit/miss 의 대표값이다.

</details>

## 5. 풀이

### Part A — 모델: 숫자 하나하나의 출처

| 파라미터 | 값 | 출처 / 지위 |
|---|---|---|
| 진입 (0 WS) | 12 | Cortex-M3/M4 TRM, NXP AN12078 등 — "zero wait state 에서 12 사이클" (문서 확인 권장) |
| tail-chaining | 6 | Cortex-M4 TRM 인용 (Nordic DevZone 질의에서 TRM 문구 재인용 — 확인 권장) |
| 복귀(언스태킹) | 12 | **가정**. 문헌에 10~12 로 다르게 인용된다 |
| late arrival 최소 | 6 | **가정**. TRM 이 숫자를 주지 않는다(같은 DevZone 질의가 답을 못 받음). "벡터 재읽기 + 파이프라인 채우기 ≈ 꼬리 물기" 로 잡음 |
| FP 추가 워드 | 17 | S0–S15 + FPSCR (프레임은 예약 워드 포함 26 워드 = 8 + 18). **워드당 1 사이클은 가정** → 0 WS 에서 29 |
| 플래시 miss | +ws / 접근 | **가정**. 벡터 읽기 + 첫 명령 줄, ART hit 이면 0. STM32F4 @168 MHz, 2.7–3.6 V 는 5 WS (RM0090 표 — 확인 권장) |

정답 TODO 는 짧다(§6 diff). 핵심은 엔진이 어떤 순서로 규칙을 적용하느냐다. `sim_run()` 은 매 사이클 ① 도착한 IRQ 를 pending 으로 ② 상태별로:

| 상태 | 그 사이클에 하는 일 |
|---|---|
| RUN (스레드/ISR) | ISR 남은 본문이 0 이면 **끝**: 돌아갈 문맥보다 급한 pending 이 있으면 TAIL(+`tail_cycles`), 없으면 EXIT(+`exit_cycles`). 아니고 현재보다 급한 pending 이 있으면 ENTRY(+`entry_cycles`). 아니면 본문 1 사이클 |
| ENTRY / TAIL | 준비 전이고 목표보다 급한 pending 이 있으면 **late**: 목표 교체, `late_ready`. 준비 시각이 되면 핸들러 시작(lazy 면 FP 멈춤 추가) |
| EXIT | 준비 전에 돌아갈 문맥보다 급한 게 오면 **pop**: TAIL 로 전환. 준비되면 한 단계 복귀 |

late arrival `d = 3` (0 WS) 을 손으로 따라가 보자. 낮은 IRQ(prio 2) 가 t=10 에 도착:

| t | 사건 | 상태 |
|---|---|---|
| 10 | 낮은 IRQ pending → ENTRY, ready = 10 + 12 = 22 | 스태킹 중, 목표 = 낮음 |
| 13 | 높은 IRQ(prio 1) 도착. 13 < 22, prio 1 < 2 → **late** | ready = max(22, 13 + 6) = 22, 목표 = 높음 |
| 22 | 높은 ISR 첫 명령 | 지연 = 22 − 13 = **9** |
| 42 | 높은 ISR 끝, 낮은 IRQ 가 기다림 (prio 2 < 스레드 256) | TAIL, ready = 48 |
| 48 | 낮은 ISR 시작 (how = tail) | 낮은 쪽 지연 38 |

`d` 를 늘리면 `13 + 6` 이 22 를 넘는 순간(d ≥ 6)부터 높은 쪽 지연은 6 에 붙고, d = 12 에선 낮은 ISR 이 **이미 시작**했으므로 평범한 중첩 선점(12)이 된다. `demo_sweep.c` 의 실제 출력:

```text
== late arrival: low (prio 2) arrives t=10, high (prio 1) at t=10+d — high's latency / who starts first
    d | WS0 lat  order | WS5-miss lat  order
    0 |  12     entry  |  22          entry 
    2 |  10     late   |  20          late  
    4 |   8     late   |  18          late  
    6 |   6     late   |  16          late  
   10 |   6     late   |  16          late  
   12 |  12     nested |  16          late  
   20 |  12     nested |  16          late  
   22 |  12     nested |  22          nested
== pop pre-emption: B arrives d cycles after A's BX LR (A alone: exit takes 12)
    d | B latency  how    | without pop (wait exit, then entry)
    0 |   6        tail   |  24
    4 |   6        pop    |  20
   10 |   6        pop    |  14
   12 |  12        entry  |  12
```

(d 가 짝수인 줄만 찍는 데모; 줄 일부 생략.) 5 WS miss 에선 낮은 쪽 진입 자체가 22 라 late 창이 d = 20 까지 넓어진다 — 느린 메모리일수록 late arrival 의 이득이 크다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">높은 IRQ 의 지연 vs 낮은 IRQ 뒤 도착 간격 d — 모델 (demo_sweep.c), 측정 아님</text>
  <line x1="60" y1="220" x2="700" y2="220" stroke="currentColor"/>
  <line x1="60" y1="36" x2="60" y2="220" stroke="currentColor"/>
  <text x="40" y="176" fill="currentColor">6</text>
  <text x="34" y="128" fill="currentColor">12</text>
  <text x="34" y="96" fill="currentColor">16</text>
  <text x="34" y="48" fill="currentColor">22</text>
  <line x1="56" y1="172" x2="700" y2="172" stroke="currentColor" stroke-dasharray="2 4"/>
  <line x1="56" y1="124" x2="700" y2="124" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="52" y="238" fill="currentColor">d=0</text><text x="196" y="238" fill="currentColor">6</text>
  <text x="340" y="238" fill="currentColor">12</text><text x="532" y="238" fill="currentColor">20</text>
  <text x="580" y="238" fill="currentColor">22</text><text x="676" y="238" fill="currentColor">26</text>
  <polyline points="60,124 108,140 156,156 204,172 324,172 348,124 684,124" fill="none" stroke="currentColor" stroke-width="2"/>
  <polyline points="60,44 108,60 156,76 204,92 564,92 588,44 684,44" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3"/>
  <text x="360" y="118" fill="currentColor">0 WS (실선): d ≥ 12 → 낮은 ISR 이 이미 시작 → 중첩 12</text>
  <text x="250" y="86" fill="currentColor">5 WS miss (점선): late 창이 d = 21 까지, 최소 6 + 10</text>
  <rect x="110" y="182" width="200" height="30" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="118" y="201" fill="currentColor">late 창: 지연 ≤ 정상 진입</text>
  <text x="420" y="260" fill="currentColor">세로: 높은 IRQ 도착 → 첫 명령 (사이클)</text>
</svg>
```

순서 관계 검사(모든 WS 0..7 × ART × VTOR 위치, d = 0..30)와 무작위 3000 사건 집합(IRQ 2–6 개, 우선순위 0–3, FP 무작위)의 불변식이 전부 통과한다:

```text
   random: 3000 event sets, violations once=0 prio=0 min=0 unterminated=0
```

- `once`: 모든 IRQ 가 정확히 한 번 처리. `prio`: 핸들러가 시작하는 순간, **그보다 먼저 와서 기다리는 더 급한 IRQ** 가 없다(late arrival 규칙이 이걸 보장). `min`: 어떤 핸들러도 도착 후 min(tail, late_min) = 6 보다 빨리 시작하지 않는다. 시작 코드에선 `min=8086` — 비용 0 이면 물리적으로 불가능한 0 사이클 진입이 생긴다.

### Part B — 측정 체인: 무엇을 재고, 무엇을 모르나

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">CYCCNT 로 재는 것 = 여러 조각의 합 (board/latency_fw.c)</text>
  <rect x="10" y="34" width="120" height="44" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="52" fill="currentColor">main: ldr t0,[r1]</text>
  <text x="18" y="68" fill="currentColor">r1 = &amp;CYCCNT</text>
  <rect x="140" y="34" width="120" height="44" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="148" y="52" fill="currentColor">str → NVIC_STIR</text>
  <text x="148" y="68" fill="currentColor">(또는 SWIER/GPIO)</text>
  <rect x="270" y="34" width="130" height="44" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="278" y="52" fill="currentColor">버스 → NVIC 펜딩</text>
  <text x="278" y="68" fill="currentColor">트리거 경로 = 모름</text>
  <rect x="410" y="34" width="150" height="44" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="418" y="52" fill="currentColor">진입: 스태킹 ∥ 벡터</text>
  <text x="418" y="68" fill="currentColor">모델 12 (+WS, +FP)</text>
  <rect x="570" y="34" width="160" height="44" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="578" y="52" fill="currentColor">ISR 첫 명령:</text>
  <text x="578" y="68" fill="currentColor">ldr r0,[r1] = t_isr</text>
  <path d="M130 56 L140 56 M260 56 L270 56 M400 56 L410 56 M560 56 L570 56" stroke="currentColor"/>
  <line x1="70" y1="92" x2="650" y2="92" stroke="currentColor"/>
  <text x="250" y="108" fill="currentColor">raw = t_isr − t0  (uint32_t 뺄셈 → 감김 안전)</text>
  <rect x="10" y="124" width="350" height="70" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="142" fill="currentColor">보정 calib: ldr a,[r1]; ldr b,[r1] → b − a</text>
  <text x="18" y="160" fill="currentColor">64 번 중 최소값. raw − calib = "LDR 하나 뒤"부터</text>
  <text x="18" y="178" fill="currentColor">그래도 트리거 경로(가정 2)는 남는다 → 절대값은 의심</text>
  <rect x="380" y="124" width="350" height="70" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="388" y="142" fill="currentColor">차분 측정: 같은 트리거, 설정만 다르게</text>
  <text x="388" y="160" fill="currentColor">(WS5 − WS0), (FP full − FP lazy) → 경로 소거</text>
  <text x="388" y="178" fill="currentColor">ISR→ISR 갭(tail, lazy 멈춤)은 경로 자체가 없다</text>
  <rect x="10" y="208" width="720" height="82" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="18" y="226" fill="currentColor">QEMU mps2-an386 실측 (board/build_vm_output.txt):</text>
  <text x="18" y="244" fill="currentColor">DEMCR 쓰기 후 0 ("NVIC: Bad read offset 0xdfc"), CYCCNT 0 ("unassigned PPB 0x1004")</text>
  <text x="18" y="262" fill="currentColor">ISR 는 돌았다(isr runs 1), t0 = t_isr = 0. 같은 루프 SysTick 차 24784 / 27334 / 25473 / 22504</text>
  <text x="18" y="280" fill="currentColor">= 호스트 시간. -icount shift=0 이면 30000 고정 = 명령 수 시간, 사이클 아님</text>
</svg>
```

**QEMU 를 먼저 확인했다.** `board/qemu_cyccnt_probe.c` 는 보드 펌웨어와 **같은** `dwt_cyccnt.h` (TRCENA → LAR 키 → CYCCNTENA → 100 번 루프 자기검사)를 쓰고, 같은 "t0 → STIR → naked ISR 첫 명령" 측정을 한 번 한다. 실제 출력:

```text
DEMCR before        = 0x00000000
DWT_CTRL before     = 0x00000000
DWT_LSR             = 0x00000000
DEMCR after TRCENA  = 0x00000000
DWT_CTRL after ENA  = 0x00000000
dwt_cyccnt_init rc  = 0x00000002
isr runs            = 0x00000001
t0 (before STIR)    = 0x00000000
t_isr (ISR 1st ins) = 0x00000000
delta               = 0x00000000
200k-loop CYCCNT d  = 0x00000000
200k-loop SysTick d = 0x000060d0
VERDICT: CYCCNT stuck -> QEMU has no cycle counter; latency must be measured on a board
qemu exit=0
== run 2: -d unimp,guest_errors (who handles 0xE000EDFC / 0xE0001000?) ==
      3 NVIC: Bad read offset 0xdfc
      1 NVIC: Bad write offset 0xdfc
      4 Read of unassigned area of PPB: offset 0x1000
      6 Read of unassigned area of PPB: offset 0x1004
      1 Write of unassigned area of PPB: offset 0x1000
      1 Write of unassigned area of PPB: offset 0x1004
```

해석(관찰 기반): QEMU 10.2.1 의 v7-M NVIC 는 DEMCR(0xE000EDFC) 를 모르는 오프셋으로 기록하고 0 을 돌려준다. DWT 블록(0xE0001000) 은 아예 모델이 없어 "unassigned PPB" — 읽기 0, 쓰기 무시, **fault 도 없다**. 그래서 측정 코드는 조용히 0 을 낸다. 이게 `dwt_cyccnt_init()` 의 자기검사(rc = 2 "멈춤")와 분석기의 `ERR_STUCK` 이 있는 이유다. 실보드에서 TRCENA 를 빼먹어도 같은 증상이다. `-icount shift=0` 은 시간을 결정적으로 만들지만(30000 고정) 모든 명령이 같은 비용이라 예외 진입·WS 를 모델링하지 않는다.

**보드 펌웨어 설계** (`board/latency_fw.c`, STM32F407 리셋 클럭 HSI 16 MHz 그대로 — PLL 은 BM-A01 의 몫):

1. **naked ISR, 첫 명령 = 표본.** C 핸들러는 첫 C 문장 전에 `push` 프롤로그가 있어 1+N 사이클이 섞인다. 주소를 읽는 `ldr r0, =CYCCNT` 도 필요 없게, main 이 트리거 직전 **r1 = &CYCCNT** 를 둔다. 예외 진입은 r0–r3 를 스택에 *복사* 할 뿐 지우지 않으므로(ARMv7-M, 확인 권장) ISR 의 첫 명령 `ldr r0,[r1]` 이 바로 표본이다. VM 빌드의 실제 디스어셈블:

```text
08000188 <EXTI0_IRQHandler>:
 8000188:	ldr	r0, [r1, #0]
 800018a:	ldr	r2, [pc, #32]	@ (80001ac <EXTI0_IRQHandler+0x24>)
080001cc <EXTI2_IRQHandler>:
 80001cc:	ldr	r0, [r1, #0]
 80001ce:	vadd.f32	s0, s0, s0
 80001d2:	ldr	r3, [r1, #0]
== trigger sequences (ldr t0,[r1] / str trigger / dsb / isb [/ ldr resume]) ==
 80005c0:	ldr.w	r9, [r1]
 80005c4:	str	r5, [r0, #0]
 80005c6:	dsb	sy
 80005ca:	isb	sy
```

2. **꼬리 물기는 ISR→ISR 로 잰다.** NVIC_ISPR0 에 EXTI0|EXTI1 을 **한 번의 store** 로 펜딩(같은 우선순위 0x40). EXTI0 의 마지막 표본을 **r3 에도 남기면**, 꼬리 물기는 언스태킹을 안 하므로 EXTI1 이 r3 를 그대로 본다 — 트리거 경로가 아예 없는 측정. 끝 표본 뒤 `str` 1 개는 모델 밖 오프셋 1 로 기록(Part C 의 `offs`).
3. **WS 효과**: 16 MHz 에서도 FLASH_ACR LATENCY 를 0→5 로 올리는 건 합법(필요보다 많은 WS 는 늘 안전). ART 켬/끔, 그리고 매 표본 전 ICRST/DCRST 로 캐시를 비우는 "cold" 시나리오.
4. **FP**: `vmov` 한 번으로 CONTROL.FPCA = 1, `mrs/bic/msr control` 로 0. FPCCR LSPEN 0/1. lazy 멈춤은 EXTI2 의 `ldr – vadd – ldr` 사이.
5. **late arrival**: 소프트웨어로는 스태킹 중 12 사이클 창을 못 맞춘다 → TIM2(낮음, CCR1 = 2000) 와 TIM5(높음, CCR1 = 2000 + d) 를 같은 16 MHz 로. 둘을 연달아 켜는 skew 는 상수라 d 축이 밀릴 뿐. 순서(high_first)는 `#` 주석 줄로.
6. 표본마다 `WAIT_LOOPS` 타임아웃 → 실패 표본은 raw 0 으로 남겨 분석기에서 눈에 띄게. 빌드 결과: `text 4628, bss 8060` (표본 버퍼 2 × 1000 워드).

**함정과 그 검사 (Part B 실제 출력):**

```text
   wrap: unsigned delta=20, naive int64 delta=-4294967276
   QEMU-style all-zero dump -> STUCK
   truncation: 362 lengths tried, accepted=0, other-code=0
   1-byte corruption: 92565 variants, accepted=23918 (name/case/comment only), accepted with different data=0
   d=11: order 1,0 how(high)=late how(low)=tail
   d=12: order 0,1 how(high)=nested depth=2
```

- **감김**: CYCCNT 는 16 MHz 에서 268 초, 168 MHz 에서 25.6 초마다 감긴다(연습 2). 1000 표본을 도는 동안 한 번은 반드시 걸린다. `(int64_t)t1 − t0` 는 −4294967276.
- **잘림**: UART 캡처를 일찍 끊으면 마지막 줄이 반쯤 온다. 16 진수를 **정확히 8 자리** 요구 + `E` 줄 필수 + 시나리오별 개수 대조로 0..L−2 길이 전부 거부. 마지막 개행만 빠진 건 정상 처리.
- **1 바이트 손상 전수**: 363 위치 × 255 값. 받아들여진 23918 개는 전부 데이터가 같다(주석·이름 글자, 16 진수 대소문자). 체크섬에 `core_hz` 와 `calib` 도 넣었기 때문에 이 둘이 바뀌어도 걸린다 — 빼면 calib 한 자리 손상이 **모든 표본을 조용히 치우치게** 한다. 합(sum)은 1 바이트 손상엔 충분하지만 두 표본 교환 같은 건 못 잡는다 → 실제론 CRC-32 (COMM-B08).
- **보정 누락**: 모든 표본이 정확히 calib 만큼 크다(검사 B8). 크기는 작아도(1~2) "12 대 14" 논쟁의 절반이다.

### Part C — 분석기: 합성 덤프에서 표까지

덤프 형식 v1 은 펌웨어 `emit_scn()` 과 분석기 `dump_parse()` 가 공유한다. 합성 덤프는 첫 줄이 `# SYNTHETIC - BM-A10 model + xorshift32 jitter (seed 0x00c0ffee), NOT a measurement` 이고 분석기는 이 표시를 `synthetic = 1` 로 들고 다닌다. 표본 = `calib + offs + (miss ? 모델_miss : 모델) + (10 % 확률 +1)`, t0 는 무작위(50 표본마다 한 번은 감김 직전).

`offs` 는 모델 밖의 **고정 오프셋 가정**: STIR 트리거 경로 2, 갭 측정의 끝 표본 뒤 STR 1, lazy 멈춤 측정의 VADD 1. 실보드에서 이 값들이 진짜 몇인지가 측정의 첫 번째 결과물이 될 것이다.

실제 출력 (SYNTHETIC — 모델과 일치하는 건 당연, 도구의 끝단까지 검증한 것):

```text
   dump 82603 bytes, parse=OK, synthetic=1, scenarios=8, samples=3200
   scenario         model  offs expect |  min  p50  p99  max    mean | bimodal (lo/hi, hi share)
   entry_ws0           12     2    14 |   14   14   15   15   14.11 | no
   entry_ws5_art       12     2    14 |   14   14   25   25   16.32 | YES 14/24, 22.2%
   entry_ws5_noart     22     2    24 |   24   24   25   25   24.10 | no
   tail_gap_ws0         6     1     7 |    7    7    8    8    7.06 | no
   exit_ws0            12     1    13 |   13   13   14   14   13.11 | no
   entry_fp_full       29     2    31 |   31   31   32   32   31.10 | no
   entry_fp_lazy       12     2    14 |   14   14   15   15   14.12 | no
   lazy_stall          17     1    18 |   18   18   19   19   18.07 | no
54/54 passed
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">entry_ws5_art 히스토그램 — SYNTHETIC (모델 + 지터, 실측 아님), n = 400</text>
  <line x1="60" y1="210" x2="720" y2="210" stroke="currentColor"/>
  <rect x="100" y="70" width="40" height="140" class="f-ok" stroke="currentColor"/>
  <text x="104" y="64" fill="currentColor">279</text>
  <rect x="150" y="194" width="40" height="16" class="f-ok" stroke="currentColor"/>
  <text x="158" y="188" fill="currentColor">32</text>
  <rect x="520" y="169" width="40" height="41" class="f-warn" stroke="currentColor"/>
  <text x="528" y="163" fill="currentColor">81</text>
  <rect x="570" y="206" width="40" height="4" class="f-warn" stroke="currentColor"/>
  <text x="582" y="200" fill="currentColor">8</text>
  <text x="108" y="228" fill="currentColor">14</text><text x="158" y="228" fill="currentColor">15</text>
  <text x="528" y="228" fill="currentColor">24</text><text x="578" y="228" fill="currentColor">25</text>
  <text x="300" y="228" fill="currentColor">… 16–23: 0 개 (틈 9 ≥ min_gap 3) …</text>
  <text x="210" y="100" fill="currentColor">ART hit: 12 + 오프셋 2 (+1 지터)</text>
  <text x="380" y="140" fill="currentColor">miss: 22 + 2 — 벡터 +5, 첫 줄 +5</text>
  <text x="210" y="246" fill="currentColor">p50 = 14 는 hit 만 본다. 평균 16.32 는 어느 쪽도 아닌 값. 최악은 max/p99 = 25.</text>
</svg>
```

통계 정의: p50/p99 는 nearest-rank (`rank = ceil(q·n)`, 정렬[rank−1]). 무작위 2000 배열로 "count(v ≤ x) ≥ q·n 인 최소 x" 라는 정의 그대로와 대조했다. 평균은 쌍봉에서 **거짓말**을 한다 — 16.32 사이클짜리 인터럽트는 한 번도 없었다. 그래서 지연 보고는 항상 min / p50 / p99 / max + 히스토그램.

**표로 정리 (과제가 요구한 WS · ART · FP 표, 모델 사이클 — `practice.c` 연습 4 실제 출력):**

```text
       WS | ART hit | miss | miss+VTOR RAM | miss+FP full | miss+FP lazy
        0 |      12 |   12 |            12 |           29 |           12
        1 |      12 |   14 |            13 |           31 |           14
        3 |      12 |   18 |            15 |           35 |           18
        5 |      12 |   22 |            17 |           39 |           22
```

(WS 2, 4 줄 생략.) 168 MHz 에서 최악 칸 39 사이클 = 232 ns. lazy 열은 "ISR 이 FP 를 안 쓸 때" — 쓰면 첫 FP 명령에서 +17, 복귀에서 +17.

**보드가 생기면** (이 레슨의 다음 걸음): `latency_fw.elf` 를 올리고 USART2 를 115200 으로 캡처 → 그 텍스트를 `dump_parse()` 에 넣는다. 보고는 "모델 | 실측 p50 | 실측 p99 | 차분(WS5−WS0) | 차분(full−lazy) | tail 갭 | late 순서 전환점" 표. 모델과 다르면 **모델을 고친다** — 특히 복귀 12, late 최소 6, miss 당 WS 는 가정이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

## 7. 자주 하는 실수

1. **C 핸들러의 첫 줄에서 CYCCNT 를 읽고 "진입 지연" 이라 부른다.** 그 앞에 `push {r4-r6,lr}` (1+4 사이클, 확인 권장)와 주소 리터럴 로드가 있다. → naked ISR + 첫 명령 표본, 또는 프롤로그를 디스어셈블해 따로 적는다.
2. **TRCENA 없이 CYCCNTENA 만 켠다.** DWT 는 꺼진 채라 0 이 나온다 — QEMU 와 똑같은 증상. → `DEMCR |= TRCENA` 먼저, 그리고 **켠 뒤 값이 변하는지 자기검사** (`dwt_cyccnt_init()` rc 2).
3. **Cortex-M7 에서 LAR 을 안 푼다.** 일부 M7 부품은 `DWT_LAR = 0xC5ACCE55` 없이 DWT 쓰기를 무시한다(확인 권장). M3/M4 에서도 써서 해는 없도록 공통 코드에 넣었다.
4. **`int64_t` 로 바꾸면 감김이 안전해진다고 생각한다.** 반대다: `(int64_t)0xC − 0xFFFFFFF8 = −4294967276`. `uint32_t` 뺄셈 하나가 정답(구간 < 2³² 일 때).
5. **보정을 빼먹거나, 보정을 다른 명령 모양으로 잰다.** 보정은 측정과 **같은 LDR 모양**이어야 한다. 그리고 `raw < calib` 를 0 으로 자르지 말고 에러로 — 순서가 뒤집힌 측정의 신호다.
6. **평균만 보고한다.** ART hit/miss 처럼 쌍봉이면 평균은 존재하지 않는 값(16.32). 최악 지연 예산은 max (그리고 p99 는 "얼마나 자주").
7. **트리거 경로를 진입 지연에 포함한 채 ARM 의 12 와 비교한다.** STIR/SWIER/GPIO 동기화기마다 다르다. 절대값 대신 **차분**(같은 트리거, 설정만 바꿈)과 ISR→ISR 갭을 믿는다.
8. **ISR 끝에서 플래그 지우고 바로 복귀.** 지우기가 버스에서 늦게 도착하면 같은 IRQ 로 다시 들어온다(BM-M01 late clear) — 측정 ISR 에 `dsb` 를 둔 이유.

## 8. 면접 Q&A

<details><summary>Q1. Cortex-M4 인터럽트 지연은 몇 사이클이고, 그 숫자의 조건은?</summary>

0 wait state 메모리에서 IRQ 가 NVIC 에 도착한 뒤 ISR 첫 명령까지 12 사이클 (ARM TRM/앱노트 — 확인 권장). 조건: 벡터와 ISR 코드가 0 WS 에서 읽히고, FP 문맥이 즉시 저장되지 않고, 더 긴 마스킹 구간(PRIMASK/BASEPRI)이 없고, 현재 명령이 오래 걸리지 않을 때(LDM/STM 은 중단·재개 가능). 실제 STM32F4 @168 MHz 5 WS 에서 ART miss 면 모델상 22, FP 즉시 저장이면 +17.

</details>

<details><summary>Q2. 12 사이클 동안 코어는 무엇을 하나요?</summary>

8 워드(r0–r3, r12, LR, PC, xPSR) 스태킹을 데이터 버스로, 동시에 벡터 표에서 핸들러 주소를 명령 버스로 읽고(하버드 구조의 병렬 — 버스 배정은 TRM 확인 권장), 파이프라인을 다시 채운다. LR 에 EXC_RETURN 을 넣는다. 그래서 벡터 표를 SRAM 으로 옮기면(VTOR) 플래시 대기 하나가 사라진다 — 모델에서 5 WS miss 22 → 17.

</details>

<details><summary>Q3. tail-chaining 과 late arrival 을 구분해 설명하고, 각각 몇 사이클을 아끼나요?</summary>

tail-chaining: ISR A 가 끝날 때 B 가 기다리고 있으면 언스태킹·재스태킹 없이 벡터만 읽고 B 로 — 복귀 12 + 진입 12 = 24 대신 6 (n 개면 18(n−1) 절약, 연습 5). late arrival: 낮은 IRQ 를 위해 스태킹하는 중 더 급한 IRQ 가 오면 같은 스태킹을 재사용하고 벡터만 바꿔 급한 것부터 — 급한 쪽 지연은 12 보다 짧아질 수 있고(모델 d=3 → 9, 최소 6 은 가정), 낮은 쪽은 뒤에 꼬리 물기로. 셋째로 pop pre-emption: 언스태킹 중 새 IRQ 가 오면 언스태킹을 버리고 꼬리 물기로.

</details>

<details><summary>Q4. DWT CYCCNT 로 재셨다면, 측정값이 12 가 아니라 14 였던 이유는?</summary>

재는 구간이 "IRQ 도착 → 첫 명령" 이 아니라 "t0 를 읽은 LDR → 트리거 store → 버스 → NVIC 펜딩 → 진입 → 표본 LDR" 이기 때문. back-to-back 읽기로 LDR 비용은 빼지만 트리거 경로는 모른다. 그래서 (1) 절대값은 "소프트웨어가 보는 지연" 으로 보고하고, (2) 진짜 비교는 차분으로: 같은 트리거에서 WS 만 바꾼 차, FP 설정만 바꾼 차, 그리고 ISR→ISR 갭(꼬리 물기)은 트리거 경로가 아예 없다. 이 레슨의 합성 덤프는 그 오프셋을 2 로 **가정**했다.

</details>

<details><summary>Q5. lazy stacking 은 무엇을 미루고, 언제 오히려 손해인가요?</summary>

FPCA=1 인 문맥을 선점할 때 26 워드 프레임 **공간만** 잡고 S0–S15+FPSCR 저장은 ISR 이 첫 FP 명령을 실행할 때로 미룬다(FPCCR.LSPEN, LSPACT/FPCAR — 확인 권장). FP 를 안 쓰는 ISR 은 진입·복귀 모두 공짜(모델 12/12). 손해는 거의 없지만 지연이 **ISR 안으로 이동**한다: 첫 FP 명령에서 17 사이클 멈춤 — 하드 데드라인이 ISR 안쪽 특정 시점이면 그 지터가 문제. 고정 지연이 더 중요하면 LSPEN=0 (매번 29), 아예 ISR 에서 FP 금지가 흔한 정책.

</details>

<details><summary>Q6. 같은 펌웨어인데 지연 히스토그램이 두 봉우리입니다. 무엇을 의심하나요?</summary>

캐시/가속기 hit 대 miss (STM32F4 ART, M7 I-cache/TCM), 플래시 프리페치 버퍼, 다른 버스 마스터(DMA)와의 충돌, 진입 순간 실행 중이던 명령의 종류(나눗셈·LDM), 그리고 측정 자체(타임아웃 표본 섞임). 확인: ART 를 끄면 한 봉우리(miss 쪽)로, ICRST 로 매번 비우면 miss 로 몰리는지. 처방: ISR 과 벡터 표를 SRAM/CCM/ITCM 으로(`__attribute__((section(".ramfunc")))` + VTOR), 또는 최악값을 예산에 넣는다. 평균은 보고하지 않는다.

</details>

<details><summary>Q7. QEMU 로 이걸 잴 수 없나요? 증거는?</summary>

못 잰다. QEMU 는 기능 에뮬레이터라 사이클 모델이 없다. 이 레슨에서 확인한 사실: `mps2-an386` 에서 DEMCR 쓰기가 무시되고("NVIC: Bad read offset 0xdfc"), DWT 영역은 "unassigned PPB" 라 읽기 0·쓰기 무시·fault 없음 → CYCCNT 항상 0. SysTick 은 호스트 시간을 따라 같은 루프에서 22504~27334 로 흔들리고, `-icount shift=0` 은 30000 으로 고정되지만 명령마다 같은 비용이다. QEMU 로는 **로직**(우선순위, 꼬리 물기 순서 — BM-M02) 을, 보드로는 **시간**을 검증한다.

</details>

<details><summary>Q8. (스태프 레벨) 168 MHz 모터 제어 ISR 의 지연 지터를 ±5 ns 안에 넣으라면 어떻게 바꾸겠나요?</summary>

5 ns ≈ 0.84 사이클 → 사실상 **지터 0** 이어야 한다. (1) ISR 과 벡터 표를 0 WS 메모리(CCM 은 F4 에서 데이터 전용이라 SRAM1 + VTOR, M7 이면 ITCM)로 → hit/miss 쌍봉 제거. (2) ISR 에서 FP 금지 또는 LSPEN=0 으로 고정 비용화. (3) 그 IRQ 를 최고 우선순위로, 다른 코드의 PRIMASK 구간 금지(BASEPRI 로 그 아래만 마스킹 — ARM-M10). (4) 그래도 남는 지터(진행 중 명령, 버스 충돌)는 **하드웨어에 맡긴다**: 타이머 출력 비교/PWM 프리로드로 펄스 타이밍을 HW 가 만들고, ISR 은 "다음 값" 만 미리 계산(BM-M05). 즉 소프트웨어 지연을 지터 경로에서 빼는 게 정답.

</details>

<details><summary>Q9. (스태프 레벨) 이 측정 설계가 틀리는 경우는?</summary>

(1) r1 트릭: 트리거와 ISR 사이에 다른 ISR 이 끼면 r1 이 바뀔 수 있다 — 측정 중 다른 IRQ 를 끄거나, 결과를 순서 검사로 걸러야 한다. (2) DSB/ISB 후에도 펜딩이 반영되기 전 명령이 실행될 수 있다면(구현 의존 — 확인 권장) exit 갭 표본이 섞인다. (3) late arrival 의 TIM2/TIM5 시작 skew 는 상수라고 가정했다 — APB 동기화로 ±1 흔들리면 d 축이 번진다. (4) 16 MHz HSI 에서 잰 WS 효과를 168 MHz 로 그대로 옮길 수 없다(ART 프리페치·버스 비율이 다름). (5) M7 은 분기 예측·캐시·듀얼 이슈로 "12" 자체가 다르다. (6) 체크섬이 합이라 표본 교환을 못 잡는다. 각각을 알고 쓰는 게 핵심.

</details>

## 9. 추가 연습

`practice.c` 는 호스트 C (맥) 다. 측정 설계에서 실제로 부딪히는 계산 다섯 개.

### 연습 1 — 로직 애널라이저로 보면? `la_interval()`

LA 가 코어 T 사이클마다 한 번 샘플한다(T = core / LA 클럭). 두 에지가 k 샘플 떨어져 보였을 때 실제 간격의 범위를 구하고, 모든 위상에 대한 brute force 와 대조하라. 168 MHz 코어 + 24 MHz LA 로 12 사이클 진입을 볼 수 있나?

<details><summary>답</summary>

`k = 0 → [0, T−1]`, `k ≥ 1 → [(k−1)T+1, (k+1)T−1]` (T = 1..12, 위상 전부 brute force 와 정확히 일치). T = 7, k = 2 → [8, 20] — 12 와 20 이 같아 보인다. 12 와 22 를 한 번에 가르려면 구간 폭 2T−2 < 10 → T ≤ 5 → LA ≥ 34 MHz (실제 출력 그대로; 정확히는 33.6 MHz). 그래서 사이클 단위는 CYCCNT, LA 는 "핀에서 핀까지"(GPIO 동기화기 포함) 확인용. 반복 측정해 위상을 평균내는 방법도 있다(HWLAB-M05).

</details>

### 연습 2 — CYCCNT 감김 주기와 64 비트 확장

`cyccnt_wrap_ms(core_hz)` 와, 주기보다 자주 불리면 정확한 `cyc64_update()`.

<details><summary>답</summary>

2³²·1000 / f: 16 MHz → 268435 ms, 168 MHz → 25565 ms, 480 MHz → 8947 ms. 확장은 `ext += (uint32_t)(now − last)` — 무작위 20000 걸음(각 < 2³²) 전부 정확. 한 주기 이상 못 부르면 2³² 가 통째로 사라지고 **감지 불가**(검사로 보임) → SysTick 같은 주기 작업에서 부른다.

</details>

### 연습 3 — 최악 응답 시간: 지연 + 선점 + 마스킹

R = B + (C + O) + Σ ceil(R/Tj)·(Cj + O), O = 진입 + 복귀 = 24 (모델). 고정점 반복을 구현하고, "모두 t=0 해제 + 직전 B 동안 마스킹" 임계 순간 시뮬레이션과 대조하라.

<details><summary>답</summary>

손 계산 예: C=200, B=150, hp = {40/1000, 100/3000} → R = 562 사이클 (3345 ns @168 MHz). 무작위 2000 집합(이용률 ≤ 80 %) 이 시뮬레이션과 전부 일치. 교훈: 지연 12 는 시작일 뿐, **가장 긴 마스킹 구간 B** 와 더 급한 ISR 들이 최악을 지배한다. 꼬리 물기는 O 를 일부 줄여 주지만 이 식은 보수적으로 매번 24 를 센다. 깊은 분석은 RTOS 트랙의 RTA 와 ARM-A08.

</details>

### 연습 4 — "표로 정리": WS × ART × FP

본문 모델의 닫힌 식 축약판으로 WS 0..5 표를 만들고 셀을 검사하라.

<details><summary>답</summary>

WS0: 12 / 12 / 12 / 29 / 12, WS5: 12 / 22 / 17 / 39 / 22 (hit / miss / miss+VTOR RAM / miss+FP full / miss+FP lazy). 모든 열이 WS 에 단조. 최악 39 사이클 = 232 ns @168 MHz. 표는 §5 Part C 에 붙였다. 모델 값이다 — 보드 덤프의 `entry_stir_ws*`, `entry_ws5_art*`, `entry_fp_*` 가 같은 칸을 실측으로 채울 자리다.

</details>

### 연습 5 — 차분 측정과 꼬리 물기 절감

모르는 오프셋(0..39)을 공유하는 두 시나리오의 중앙값 차가 WS 벌점을 정확히 되찾는지, 그리고 n 개 동시 펜딩의 오버헤드 `12 + 6(n−1) + 12` 대 `24n`.

<details><summary>답</summary>

300 번 시도 모두 차이 = 숨긴 벌점 (지터 10 % +1 이 양쪽 중앙값을 안 흔든다). 오버헤드: n=2 → 30 대 48, n=4 → 42 대 96, 절감 = 18(n−1). 중앙값을 쓰는 이유: min 은 지터·이상치에 민감하지 않지만 쌍봉 한쪽만 볼 수 있고, 평균은 쌍봉에서 엉뚱하다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A10_pr && /tmp/BM-A10_pr    # 15/15 passed
# 모델 스윕 데모 (레슨 디렉터리에서 — solution.c 를 #include):
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o /tmp/BM-A10_demo && /tmp/BM-A10_demo
```

실행 결과 (실제 출력 일부):

```text
  ex1: T=7 (168 MHz / 24 MHz LA), k=2 -> delta in [8, 20] cycles
  ex1: to separate 12 vs 22 in one shot need T <= 5 -> LA >= 34 MHz at 168 MHz
  ex2: CYCCNT wraps every 268435 ms @16 MHz, 25565 ms @168 MHz, 8947 ms @480 MHz
  ex3: C=200, B=150, O=24, hp={40/1000, 100/3000} -> R = 562 cycles (3345 ns @168 MHz)
  ex3: 2000 random task sets (util <= 80%), RTA vs simulation mismatches = 0
  ex4: worst cell 39 cycles = 232 ns @168 MHz
15/15 passed
```

보드 펌웨어 (`/* ENV: board */`, VM 빌드만), 공용 DWT 헤더, QEMU 확인 펌웨어, 빌드 스크립트와 그 실제 출력:

<!-- code: board/latency_fw.c -->

<!-- code: board/dwt_cyccnt.h -->

<!-- code: board/qemu_cyccnt_probe.c -->

<!-- code: board/build_vm.sh -->

<!-- code: demo_sweep.c -->

## 10. 핵심 정리

- Cortex-M3/M4: 진입 12, tail-chaining 6 (0 WS, ARM 문서). 복귀 12·late 최소 6·FP 워드당 1·miss 당 WS 는 **이 레슨의 가정** — 보드로 확인할 대상.
- 플래시: 벡터 + 첫 명령 줄 두 번 접근. 5 WS miss → 22, ART hit → 12, VTOR 를 SRAM 으로 → 17. 쌍봉이 생긴다.
- FP: LSPEN=0 이면 매 진입 +17 (29), LSPEN=1 이면 ISR 이 FP 를 쓸 때만 첫 FP 명령에서 +17 — 지연이 ISR 안으로 이동.
- late arrival = 스태킹 재사용 + 벡터 교체 (급한 쪽 ≤ 12), pop pre-emption = 언스태킹 버리고 꼬리 물기.
- CYCCNT: TRCENA → (M7: LAR) → CYCCNTENA → **값이 변하는지 자기검사**. `uint32_t` 뺄셈으로 감김 안전. back-to-back 보정은 같은 LDR 모양.
- 측정 = 트리거 경로 + 진입 + 표본 위치. naked ISR 첫 명령 표본, 절대값보다 **차분**과 ISR→ISR 갭을 믿는다.
- QEMU (실측): DEMCR·DWT RAZ/WI, CYCCNT 0, fault 없음 → 로직은 QEMU, 시간은 보드.
- 보고는 min/p50/p99/max + 히스토그램 + 덤프 무결성(잘림·체크섬·멈춤). 평균은 쌍봉에서 거짓말한다.

## 참고자료

- Arm, *Cortex-M4 Technical Reference Manual* (DDI 0439) — 3.? Exception handling (진입 12 / tail-chain 6, 0 WS), 명령 타이밍 표 (LDR/STM/PUSH 1+N); *Cortex-M3 TRM* (DDI 0337) — 같은 절. 절 번호 확인 권장.
- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — B1.5 예외 진입/복귀, late arrival·tail-chaining 서술; C1.6 DWT (CTRL, CYCCNT, NOCYCCNT), C1.8 DEMCR TRCENA; B3.2.20 CPACR; FPCCR/FPCAR (lazy).
- Arm Application Note 298, *Cortex-M4(F) Lazy Stacking and Context Switching* — LSPEN/ASPEN, 26 워드 프레임 (확인 권장).
- NXP AN12078, *Measuring Interrupt Latency* — Cortex-M3/M4 12 사이클(0 WS), tail-chain/late-arrival/pop-pre-emption 이 ISR 간 전환 비용을 줄인다는 서술: https://cn.nxp.com/docs/en/application-note/AN12078.pdf
- Nordic DevZone, "What is the minimum entry latency of a late-arriving exception?" — TRM 의 tail-chaining 6 사이클 인용, late arrival 사이클 수는 TRM 에 없음: https://devzone.nordicsemi.com/f/nordic-q-a/87693/what-is-the-minimum-entry-latency-of-a-late-arriving-exception
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 8 장 예외 처리 (late arrival, pop pre-emption), 13 장 FPU lazy stacking.
- ST RM0090 (STM32F405/407…) — FLASH_ACR (LATENCY, PRFTEN, ICEN, DCEN, ICRST, DCRST), WS 표 (2.7–3.6 V, 168 MHz → 5 WS), EXTI/SYSCFG, TIM2/TIM5, 벡터 표 (EXTI0 = 6, TIM2 = 28, TIM5 = 50). 확인 권장.
- QEMU 10.2.1 실측 (`-d unimp,guest_errors`): "NVIC: Bad read offset 0xdfc" (hw/intc/armv7m_nvic.c 의 기록 문구), "Read of unassigned area of PPB" (v7-M PPB 기본 영역), `-icount` 문서.
- 관련 레슨: BM-M01 (late flag clear, NVIC 레지스터), BM-M02 (QEMU 에서 우선순위·꼬리 물기 순서 = 로직 검증), BM-M05 (PWM: HW 가 타이밍을 만들게), BM-B03 (클럭 켠 뒤 더미 읽기), BM-B14 (UG/PSC 프리로드), BM-A01 (CPACR, 클럭 전환), COMM-B08 (CRC vs 합), HWLAB-M05 (LA 타이밍 측정), RTOS-M08. 다음 단계: ARM-A08 (인터럽트 지연 심화), ARM-M10 (PRIMASK/BASEPRI 마스킹 구간), ARM-M12 (fault 디코드).
