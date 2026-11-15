---
id: BM-M09
time: 90분
updated: 2026-11-11
---
# BM-M09 ADC + DMA 더블 버퍼링 파이프라인

## 학습 목표

- ADC 연속 변환 → DMA circular(2N 칸) → HT/TC 인터럽트로 **반쪽씩** 처리하는 파이프라인을 레지스터 순서까지 설정한다 (EN=0 확인, 남은 플래그 클리어, PAR/M0AR/NDTR, CR, EN, ADC `DMA|DDS|CONT`).
- 처리 시간 예산을 식으로 말한다: 반쪽 주기 N·S, 첫 반쪽이 덮이기 시작하는 시점 (N+1)·S, 비용이 N·S 를 δ 만큼 넘으면 **⌈S/δ⌉−1 번째 반쪽**에서 처음 덮어쓰기 — 시뮬레이션이 정확히 그 반쪽에서 잡는다.
- "처리가 끝난 순간 DMA 가 이미 내 반쪽에 다시 들어왔나" 를 NDTR + 자기 플래그로 판정하고, **NDTR 을 먼저 읽어야 하는 이유**(반대 순서는 k = 2N−1 에서 놓친다)를 증명한다.
- 덮어쓰기·늦은 진입·ADC OVR(DDS/CIRC 설정 버그) 뒤에 데이터를 조용히 섞지 않고 GAP 표시 + 재동기화 + 복구한다.
- 샘플링 속도 × N × 사이클 수 → CPU 부하와 최악 지연 표를 만들고, F4 이중 버퍼 모드(DBM/CT)와 비교한다.

## 1. 문제

모터 전류, 마이크, 배터리 전압, RF 전력 검출기처럼 **끊기지 않고 들어오는 샘플**을 다루는 펌웨어는 거의 전부 이 구조다: ADC 가 타이머 박자로 변환하고, DMA 가 원형 버퍼를 채우고, CPU 는 버퍼의 절반이 찰 때마다 그 절반을 처리한다. 면접에서는 "HT/TC 로 핑퐁 버퍼를 어떻게 짜나요?" 다음에 반드시 **"처리가 늦으면 어떻게 알죠?"** 가 나오고, 거기서 NDTR 해석의 off-by-one 과 레지스터 읽기 순서의 경쟁(race)이 갈린다. Don 이 SSD 컨트롤러에서 본 "호스트 DMA 가 쓰는 동안 펌웨어가 같은 버퍼를 읽으면?" 문제와 같은 뿌리다.

이 레슨은 UART RX 용 circular DMA(COMM-M11: 바이트 스트림, IDLE, 가변 길이 프레임)와 짝이다. 여기서는 **고정 속도 샘플 스트림**이라 "언제 처리해야 하나" 가 HT/TC 로 정해지고, 질문의 중심이 **처리 시간 예산과 덮어쓰기 검출**로 옮겨 간다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">파이프라인 (이 레슨의 mock — 주소·비트는 RM0090 기준 대표값)</text>
  <rect x="10" y="40" width="130" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="60" fill="currentColor" font-weight="bold">ADC1</text>
  <text x="20" y="78" fill="currentColor">CONT, S 클럭/변환</text>
  <text x="20" y="96" fill="currentColor">DR, SR.EOC/OVR</text>
  <rect x="190" y="40" width="160" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="200" y="60" fill="currentColor" font-weight="bold">DMA2 Stream0 ch0</text>
  <text x="200" y="78" fill="currentColor">P→M, 16비트, MINC, CIRC</text>
  <text x="200" y="96" fill="currentColor">NDTR 2N→1→2N, HT, TC</text>
  <rect x="400" y="40" width="150" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="410" y="60" fill="currentColor" font-weight="bold">SRAM buf[2N]</text>
  <text x="410" y="78" fill="currentColor">반쪽0 [0,N)</text>
  <text x="410" y="96" fill="currentColor">반쪽1 [N,2N)</text>
  <rect x="600" y="40" width="130" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="610" y="60" fill="currentColor" font-weight="bold">출력 스트림</text>
  <text x="610" y="78" fill="currentColor">mean, EMA</text>
  <text x="610" y="96" fill="currentColor">flags OVR / GAP</text>
  <path d="M140 75 L190 75" stroke="currentColor" fill="none" marker-end="url(#a1)"/>
  <path d="M350 75 L400 75" stroke="currentColor" fill="none" marker-end="url(#a1)"/>
  <text x="146" y="68" fill="currentColor">요청</text>
  <text x="356" y="68" fill="currentColor">쓰기</text>
  <defs><marker id="a1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <rect x="190" y="150" width="360" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="200" y="170" fill="currentColor" font-weight="bold">DMA2_Stream0 ISR (IRQ 56): HT → 반쪽0, TC → 반쪽1</text>
  <text x="200" y="188" fill="currentColor">그 비트만 W1C → process_half → pipe_reentered? → out_push</text>
  <text x="200" y="203" fill="currentColor">처리 중에도 ADC/DMA 는 계속 돈다 (예산 = N·S 클럭)</text>
  <path d="M270 110 L270 150" stroke="currentColor" fill="none" stroke-dasharray="4 3" marker-end="url(#a1)"/>
  <text x="276" y="135" fill="currentColor">HTIF/TCIF → 선(line)</text>
  <path d="M475 150 L475 110" stroke="currentColor" fill="none" marker-end="url(#a1)"/>
  <text x="481" y="135" fill="currentColor">읽기</text>
  <path d="M550 178 L665 178 L665 110" stroke="currentColor" fill="none" marker-end="url(#a1)"/>
  <rect x="10" y="150" width="150" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="170" fill="currentColor" font-weight="bold">main loop</text>
  <text x="20" y="188" fill="currentColor">pipe_health(): SR.OVR?</text>
  <text x="20" y="203" fill="currentColor">→ DMA 재무장 + 재시작</text>
  <path d="M75 150 L75 110" stroke="currentColor" fill="none" stroke-dasharray="4 3" marker-end="url(#a1)"/>
  <text x="10" y="236" fill="currentColor">ADC 는 S 클럭마다 1 샘플, 같은 클럭에 DMA 가 DR 을 읽어 buf 에 쓴다. 반쪽 = N·S 클럭.</text>
</svg>
```

모델 요약 (전부 **이 레슨의 정의**, STM32F4 를 본뜸 — RM0090 확인 권장):

| 요소 | 모델의 동작 |
|---|---|
| ADC1 (BM-M07 ADC 모델 축약판) | `CR2` 의 ADON·CONT + `SWSTART` → S 클럭마다 변환. EOC 가 남아 있는데 새 결과 → `SR.OVR`, 이후 DMA 요청 막힘. `DR` 읽기 = EOC 클리어 |
| DDS (`CR2` bit 9) | 0 이면 DMA 의 마지막 전송(NDTR→0) 뒤 요청을 더 안 냄 → circular 에서 **한 바퀴 돌고 멈춤**. `CR2.DMA` 에 0 을 써야 풀림 |
| DMA2 Stream0 (BM-M08 DMA 모델 축약판) | 요청마다 DR → `M0AR + 2·(2N−NDTR)`, NDTR−1. NDTR = N 이 되면 HTIF, 0 이면 TCIF + 재장전(CIRC) 또는 EN=0. `EN=1` 동안 NDTR/PAR/M0AR/CR 쓰기 무시, EN 끄면 몇 번 읽은 뒤 0 |
| LISR / LIFCR | stream 0 = 그룹 오프셋 0: FEIF 0, DMEIF 2, TEIF 3, HTIF 4, TCIF 5. LIFCR 은 W1C, 읽으면 0 |
| NVIC-lite | DMA 모델이 IRQ 선(level)을 올린다. `sim_step()` = 하드웨어 1 클럭 → 펜딩이면 핸들러 호출. 반환 뒤 아직 펜딩이면 **시간 안 흐르고 바로** 다음 핸들러(tail-chaining). ISR 안의 `SIM_WORK_*()` 는 클럭을 소비하지만 다른 ISR 을 부르지 않는다(같은 우선순위) |
| 정답(truth) | 모델은 각 칸에 든 샘플의 **절대 번호**를 안다. 하네스가 레코드마다 "처리 끝에 h 에 새 샘플이 써졌나(k ≥ N+1)" 와 "읽은 샘플이 찢겼나(torn)" 를 계산 |

### Part A — 설정과 반쪽 처리

- `dma_arm(p)`: EN=0 쓰고 EN 이 0 으로 읽힐 때까지(≤100번) 대기 → `LIFCR = 0x3D`(stream 0 전부) → `PAR = &ADC1->DR`, `M0AR = buf`, `NDTR = 2N`, `FCR = 0` → `CR = cfg` → `CR = cfg|EN`.
- `pipe_start(p, N)`: N ∈ [2, 256], `dma_arm`, NVIC enable, ADC `SR = 0`, `CR2 = ADON|CONT|DMA|DDS` → `|SWSTART`.
- `process_half(p, h, &mean, &ema)`: 평균(반올림) + 샘플 단위 EMA (Q8, α = 1/16, **반쪽 경계를 넘어 상태가 이어짐** — 반쪽 순서가 틀리거나 섞이면 값이 달라진다).
- `pipe_dma_irq(p)`: 플래그로 반쪽 고르기 → **그 비트 하나만** W1C → 처리 → 검사 → `out_push`.
- 테스트: 400 반쪽 연속 스트림이 기준 필터와 비트 단위로 같다. N ∈ {2,3,7,32,256} × S ∈ {1,3,16} 예산 안 → 900 레코드 전부 같다. 남은 EN=1 + TCIF 위에서 재시작해도 가짜 첫 IRQ 없음.

### Part B — 예산 초과, 덮어쓰기 검출, 경쟁

- `pipe_reentered(p, h)`: **NDTR 먼저, LISR 나중** (각 1번). `pos = 2N − NDTR`. "pos 가 h 안 **그리고 h 의 첫 칸이 아님**" 또는 "h 자신의 플래그가 다시 섰다" → true.
- `pipe_resync(p)`: 밀린 HT|TC 를 한 번에 버리고 EMA 무효, 다음 레코드에 GAP.
- 테스트: 비용 511 / 512 / 513 / 516 / 527 / 528 / 600 클럭(N=32, S=16) → 첫 OVR 레코드가 예측과 **정확히** 같은 번호, OVR 다음 레코드는 한 반쪽 건너뛴 GAP. 검사 없는 ISR 은 찢긴 데이터를 조용히 낸다(반례). ISR 이 300 / 100 / 106 클럭 막힘 → late-entry / 무사 / OVR. 무작위 500 설정(9754 레코드)에서 **오검출 0, 미검출 0**. NDTR↔LISR 읽기 사이에 DMA 전송 1개를 끼우는 전수 테스트(k = 0..3N, 두 반쪽).

### Part C — 메인 루프 헬스 체크와 예산

- `pipe_health(p)`: `ADC_SR.OVR` → 복구(DMA=0 → `dma_arm` → SR 클리어 → DMA|DDS → SWSTART), 로그, GAP.
- 주입: `DDS` 를 깜빡한 초기화, `CIRC` 를 깜빡한 초기화 → 둘 다 한 바퀴 뒤 OVR → 정확히 1번 복구 → 이후 레코드 정상.
- `budget_calc()`: fs, N, 사이클/샘플, 고정 사이클, CPU Hz → 반쪽 주기, 처리 시간, 부하(‰), 최악 지연, ok. demo 로 실제 단위 표.

## 2. Problem (English)

> **ADC → circular DMA → half/complete-transfer double buffering, with overrun detection.**
>
> ADC1 converts continuously (one sample every S clocks); DMA2 Stream0 (channel 0, peripheral-to-memory, 16-bit, MINC, CIRC) writes into `uint16_t buf[2N]`. HTIF fires when the first half `[0,N)` is full, TCIF when `[N,2N)` is full (then NDTR reloads to 2N). The DMA IRQ handler processes the half that just filled (block mean + a per-sample EMA whose state carries across halves) and pushes one record per half.
>
> 1. `int pipe_start(pipe_t *p, uint32_t n_half)` — program the stream safely (disable and wait for EN=0, clear all stream-0 flags, PAR/M0AR/NDTR, CR, then EN) and start the ADC with `DMA|DDS|CONT`.
> 2. `void pipe_dma_irq(pipe_t *p)` — clear only the flag you handle; if both HT and TC are pending on entry you cannot tell their order: drop both and mark a gap.
> 3. `bool pipe_reentered(const pipe_t *p, uint32_t h)` — after processing half h, has the DMA already written at least one new sample into h? Use NDTR and the flags. Explain the read order.
> 4. `int pipe_health(pipe_t *p)` (main loop) — detect ADC overrun (e.g. DDS or CIRC forgotten) and recover without restarting the MCU.
> 5. `int budget_calc(...)` — CPU load and worst-case latency for a given fs, N, cycles/sample, fixed cycles, CPU clock.
>
> Example (N = 32, S = 16 → half period 512 clocks): processing cost 512 never overruns; 513 overruns first at record 15; 528 = (N+1)·S overruns on the very first half. Records after an overrun carry `GAP` and start a fresh EMA.
>
> Follow-ups: why not TC only? What does `NDTR == 2N` mean? What breaks if you read LISR before NDTR? What is DDS and what does the bug look like in the field? DBM vs circular+HT? Cortex-M7 cache?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
./starter -v     # 레코드 표 + 로그 (정답 코드로 돌리면 5. 풀이의 표가 나온다)
```

TODO 는 여덟 개: A1 `dma_arm`, A2 `pipe_start`, A3 `process_half`, A4 `pipe_dma_irq`, B1 `pipe_reentered`, B2 `pipe_resync`, C1 `pipe_health`, C2 `budget_calc`. mock 코어(BM-B02 축약판, 접근 횟수 훅 포함), ADC/DMA 모델, NVIC-lite, 신호원 `sig()`, 출력 스트림 `out_push()`, 로그 `LOG()`, 반례 두 개(`pipe_dma_irq_naive`, `reentered_flag_first`)는 주어진다. 스텁은 "시작 실패 / 0 / 검출 없음" 이라 모든 시뮬레이션이 시간 상한에서 끝난다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:576  pipe_start(&g_pipe, 32u) == 0
FAIL starter.c:577  *hw(DMA_S0CR) == (PIPE_CR_CFG | DMA_CR_EN)
…
FAIL starter.c:602  g_rec[0].mean == 1143u && g_rec[0].ema == 1153u && g_rec[1].mean == 1452u
…
FAIL starter.c:758  budget_calc(&c, &o) == 0 && o.load_permille == 1000u && o.ok
4/37 passed
```

(exit 1.) 단계별 실측: A1–A4 만 채우면 **22/37**, B1·B2 까지 **29/37**, C1·C2 까지 **37/37**.

## 4. 힌트

<details>
<summary>힌트 1 — 설정 순서: 왜 EN 을 끄고 "기다리나"</summary>

스트림이 켜져 있으면(EN=1) NDTR·주소·설정 비트 쓰기는 무시된다(RM0090 의 규칙을 모델이 따름). 그리고 EN 에 0 을 써도 진행 중인 전송이 끝날 때까지 EN 은 1 로 읽힌다 — 그래서 "쓰고, 0 으로 읽힐 때까지 읽기" (상한 있는 루프). 그다음 **그 스트림의 플래그 전부**를 지운다. 지난 실행의 TCIF 가 남아 있으면 TCIE 를 켜는 순간 IRQ 가 들어와 빈 반쪽을 "처리" 한다.

</details>

<details>
<summary>힌트 2 — ISR: 어떤 비트를 지우나</summary>

HT 를 처리할 거면 `LIFCR = HTIF` **하나만**. `LIFCR = HTIF | TCIF` 로 "깔끔하게" 지우면, 처리하는 동안이 아니라 **진입 순간** 이미 서 있던 다른 반쪽의 이벤트를 버리게 된다. HT 와 TC 가 진입 때 둘 다 서 있으면 어느 쪽이 먼저 찼는지 플래그로는 모른다 → 둘 다 버리고 GAP.

</details>

<details>
<summary>힌트 3 — NDTR → 위치, 그리고 "첫 칸" 함정</summary>

`pos = 2N − NDTR` 은 DMA 가 **다음에 쓸** 칸이다. circular 에서 NDTR 은 0 을 읽을 수 없고(TC 때 바로 2N), `NDTR == 2N` 이면 pos = 0. 반쪽 0 을 처리한 뒤 pos = 0 이 보이면? DMA 가 반쪽 1 을 막 다 쓰고 반쪽 0 의 **첫 칸 직전**에 서 있다 — 아직 한 칸도 안 썼다. 그러니 "pos 가 h 안" 만으로 판정하면 비용이 정확히 반쪽 주기일 때 매번 거짓 경보.

</details>

<details>
<summary>힌트 4 — 한 바퀴 넘게 밀리면 pos 는 거짓말을 한다</summary>

처리 중 DMA 가 2N 개 이상 썼다면 pos 는 다시 "다른 반쪽" 을 가리킨다. 그때 증거는 h 자신의 플래그다: ISR 진입 때 지웠는데 다시 섰다 = h 가 또 찼다. 그리고 순서: 두 레지스터를 읽는 사이에도 DMA 는 움직인다. 어느 순서여야 "k ≥ N+1 이면 반드시 true" 가 되는지, k = 2N−1 에서 전송 하나가 끼는 경우를 손으로 따라가 보라.

</details>

<details>
<summary>힌트 5 — 헬스 체크: OVR 뒤에 무엇을 다시 해야 하나</summary>

OVR 이 서면 ADC 는 DMA 요청을 더 내지 않는다. DMA 쪽은 NDTR 이 어디서 멈췄는지 모르므로 처음부터 다시 무장(`dma_arm`)하는 게 단순하다. ADC 쪽은 `CR2.DMA` 를 0 으로 (DDS=0 래치도 이걸로 풀림), SR 클리어, `DMA|DDS` 다시, `SWSTART`. SR 을 지울 때 OVR 만 지우면? — 7. 자주 하는 실수 6 의 실측 로그를 보라.

</details>

## 5. 풀이

### Part A — 레지스터 값과 첫 레코드

`PIPE_CR_CFG | EN` = `0x00022D19` (실측, 아래 로그의 `CR=0x00022D19`):

| 비트 | 필드 | 값 | 이유 |
|---|---|---|---|
| 27:25 | CHSEL | 0 | DMA2 Stream0 채널 0 = ADC1 (RM0090 요청 매핑 표, 확인 권장. Stream4 ch0 도 ADC1) |
| 17:16 | PL | 10 (high) | 샘플 스트림은 놓치면 OVR — 다른 스트림보다 높게 |
| 14:13 / 12:11 | MSIZE / PSIZE | 01 / 01 | 12비트 결과를 16비트로. 크기가 다르면 FIFO 가 패킹(BM-M08) |
| 10 / 9 | MINC / PINC | 1 / 0 | 메모리는 증가, DR 은 고정 |
| 8 | CIRC | 1 | 끝나면 NDTR=2N 재장전, EN 유지 |
| 7:6 | DIR | 00 | 주변장치 → 메모리 |
| 4 / 3 | TCIE / HTIE | 1 / 1 | 두 반쪽 이벤트. TEIE 는 이 레슨에서 생략(BM-M08) |
| 0 | EN | 1 | 마지막에 따로 쓴다 |

ADC 쪽은 `CR2 = ADON|CONT|DMA|DDS = 0x303`. 실제 보드는 CONT 대신 타이머 TRGO 로 변환을 거는 게 보통이다(EXTEN/EXTSEL — 샘플 간격이 지터 없이 정확). 모델에서는 둘 다 "S 클럭마다 1 샘플" 로 같다. 채널·샘플링 시간·보정은 BM-M07.

첫 레코드 (재시작 테스트, N=16, S=8, 비용 16·1 = 16 클럭): 반쪽 0 은 16번째 변환이 끝나는 **클럭 128** 에 차고(HTIF), 같은 스텝에 ISR 이 들어와 16 클럭 뒤 **144** 에 레코드를 낸다(`g_rec[0].t == 16·8 + 16`). 긴 스트림 테스트(N=32, S=16, 비용 511)는 첫 반쪽 평균 1143, EMA 1153, 둘째 반쪽 평균 1452 — 400 레코드 전부 하네스가 `sig()` 로 독립 계산한 기준과 같다.

EMA 는 `y += (x·256 − y) / 16` (C 나눗셈 = 0 쪽 버림, 정수라 결정적). 상태가 반쪽을 넘어 이어지므로 **반쪽을 건너뛰거나 순서를 바꾸면 그 뒤 값이 전부 달라진다** — 테스트가 "조용한 빈칸" 을 잡는 이유. 그래서 데이터를 버린 뒤에는 EMA 를 버리고(`ema_valid = false`) 다음 레코드에 GAP 을 단다.

### Part B — 예산: 왜 N·S 와 (N+1)·S 두 개인가

반쪽 h 가 시각 R 에 찼다. DMA 는 그다음 N 개를 **다른 반쪽**에 쓰고, (N+1) 번째 전송이 h[0] 에 들어간다. 처리가 지연 L 뒤에 시작해 비용 C 클럭 걸리면, 끝나는 순간까지의 전송 수는 k = ⌊(L + C) / S⌋. 그러므로

- **덮어쓰기(이 레슨의 OVR 정의)**: k ≥ N+1 ⇔ L + C ≥ (N+1)·S.
- **지속 가능성**: 다음 반쪽은 R + N·S 에 찬다. C > N·S 면 다음 ISR 은 C − N·S = δ 만큼 늦게 시작하고, 지연이 반쪽마다 δ 씩 쌓인다: L_i = i·δ.
- 그래서 첫 OVR 은 i·δ + C ≥ (N+1)·S ⇔ (i+1)·δ ≥ S 인 최소 i = **⌈S/δ⌉ − 1**.
- C = N·S 정확히: 여유 0 이지만 ISR 이 다른 반쪽이 차는 바로 그 클럭에 끝나고 바로 다음 ISR 로 이어진다(tail-chain) → 영원히 OVR 없음. 실칩에서는 ISR 진입 지연·지터가 있으므로 이 경계에 서면 안 된다.

시뮬레이션이 예측과 같은지 (N=32, S=16, `demo_budget_sweep`, 1000 반쪽 주기, 실측):

```text
N=32 S=16 half=512 clk, 1000 half periods, cost = pre + N*cps
 cost  load%  pred1st  meas1st  records   OVR  dropped  torn  clean%  late_entry
  480   93.8       -1       -1     1000     0        0     0   100.0           0
  511   99.8       -1       -1     1000     0        0     0   100.0           0
  512  100.0       -1       -1     1000     0        0     0   100.0           0
  513  100.2       15       15      941    58       59     0    88.3           0
  514  100.4        7        7      889   111      111     0    77.8           0
  520  101.6        1        1      667   333      333     0    33.4           0
  527  102.9        1        1      667   333      333     0    33.4           0
  528  103.1        0        0      500   500      500     0     0.0           0
  560  109.4        0        0      500   500      500     0     0.0           0
  640  125.0        0        0      500   500      500     0     0.0           0
  768  150.0        0        0      500   500      500     0     0.0           0
 1023  199.8        0        0      500   500      500     0     0.0           0
 1024  200.0        0        0      334   334      666     0     0.0           0
 1100  214.8        0        0      334   334      666   334     0.0           0
 1536  300.0        0        0      250   250      750   250     0.0           0
```

읽는 법:

- `pred1st` (닫힌 식) = `meas1st` (시뮬레이션) 이 모든 줄에서 같다. 부하 100.2% 에서도 15 반쪽(약 15·512 클럭)은 멀쩡하다가 터진다 — **벤치에서 "잘 돈다" 고 넘어가는 전형적인 경우**.
- OVR 뒤 정책(밀린 반쪽 버림 → 지연 0 으로 리셋) 때문에 패턴은 주기적이다. 주기 P = i* + 1 + ⌊(i*·δ + C)/(N·S)⌋ — 513 → 17, 520 → 3, 528 → 2, 1024 → 3, 1536 → 4 (연습 3 이 전수 확인).
- `torn` (실제로 섞인 샘플이 있는 레코드)은 C ≥ 1100 에서만 나온다. 1100 = 12 + 32·34 → 샘플당 34 클럭으로 읽는 속도가 쓰는 속도(16)보다 느려 DMA 가 읽기 포인터를 추월한다. 528~1024 의 OVR 은 **데이터는 멀쩡하지만 검사 시점에 DMA 가 이미 들어와 있던** 경우다. 검출은 보수적이다: "처리 **끝**에 DMA 가 h 에 있었나" 를 묻기 때문. 더 좁히려면 반쪽을 먼저 복사하고(빠름) 복사 직후 검사, 처리는 복사본으로 — 8. Q6.
- `clean%` 가 0 인 구간(528~1023)도 CPU 는 놀고 있지 않다. 과부하는 어떤 정책으로도 데이터를 잃는다. 고칠 곳은 예산(N, 알고리즘, 클럭)이지 검출기가 아니다.

모델 한 클럭씩 — N=8, S=4 (반쪽 32 클럭), 비용 1 + 8·4 = 33 (δ = 1 → 첫 OVR = ⌈4/1⌉ − 1 = 3). `./solution -v` 실측:

```text
   #  h  flags     ready  late  end    base   k  truth torn  mean   ema
   0  0  -           32     0     65     0   8  no      0    993    925
   1  1  -           64     1     98     8   8  no      0   1109   1004
   2  0  -           96     2    131    16   8  no      0   1222   1092
   3  1  OVR        128     3    164    24   9  yes     0   1247   1153
   4  1  GAP        192     0    225    40   8  no      0   1444   1361
   5  0  -          224     1    258    48   8  no      0   1495   1420
   6  1  -          256     2    291    56   8  no      0   1553   1473
   7  0  OVR        288     3    324    64   9  yes     0   1658   1550
   8  0  GAP        352     0    385    80   8  no      0   1850   1847
   9  1  -          384     1    418    88   8  no      0   1912   1876
  10  0  -          416     2    451    96   8  no      0   1992   1924
[t=      0] pipe start N=8 buf=16 NDTR=16
[t=    164] OVERRUN half1: DMA re-entered before processing ended (#1)
[t=    324] OVERRUN half0: DMA re-entered before processing ended (#2)
```

`late` 가 0, 1, 2, 3 으로 쌓이고, 레코드 3 은 3 + 33 = 36 = (8+1)·4 → k = 9 = N+1 → OVR. 그 순간 반쪽 0 (base 32, t=160 에 참)의 HTIF 가 펜딩이지만 `pipe_resync` 가 버린다 → 레코드 4 는 base 40, GAP, late 0, EMA 새로 시작(1444 → 1361 은 새 시작값에서 수렴 중).

```svg
<svg viewBox="0 0 750 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">위 표의 시간축 (N=8, S=4, 비용 33, x = 60 + 1.9·클럭)</text>
  <text x="4" y="58" fill="currentColor">DMA 씀</text>
  <g stroke="currentColor">
    <rect x="60" y="42" width="60.8" height="24" class="f-mut"/><rect x="120.8" y="42" width="60.8" height="24" class="f-acc"/>
    <rect x="181.6" y="42" width="60.8" height="24" class="f-mut"/><rect x="242.4" y="42" width="60.8" height="24" class="f-acc"/>
    <rect x="303.2" y="42" width="60.8" height="24" class="f-mut"/><rect x="364" y="42" width="60.8" height="24" class="f-acc"/>
    <rect x="424.8" y="42" width="60.8" height="24" class="f-mut"/><rect x="485.6" y="42" width="60.8" height="24" class="f-acc"/>
    <rect x="546.4" y="42" width="60.8" height="24" class="f-mut"/><rect x="607.2" y="42" width="60.8" height="24" class="f-acc"/>
    <rect x="668" y="42" width="60.8" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="11">
    <text x="78" y="58">반0</text><text x="139" y="58">반1</text><text x="200" y="58">반0</text><text x="261" y="58">반1</text>
    <text x="322" y="58">반0</text><text x="382" y="58">반1</text><text x="443" y="58">반0</text><text x="504" y="58">반1</text>
    <text x="565" y="58">반0</text><text x="626" y="58">반1</text><text x="686" y="58">반0</text>
  </g>
  <text x="4" y="108" fill="currentColor">ISR 처리</text>
  <g stroke="currentColor">
    <rect x="120.8" y="92" width="62.7" height="24" class="f-ok"/><rect x="183.5" y="92" width="62.7" height="24" class="f-ok"/>
    <rect x="246.2" y="92" width="62.7" height="24" class="f-ok"/><rect x="308.9" y="92" width="62.7" height="24" class="f-warn"/>
    <rect x="424.8" y="92" width="62.7" height="24" class="f-ok"/><rect x="487.5" y="92" width="62.7" height="24" class="f-ok"/>
    <rect x="550.2" y="92" width="62.7" height="24" class="f-ok"/><rect x="612.9" y="92" width="62.7" height="24" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="11">
    <text x="128" y="108">#0 h0</text><text x="191" y="108">#1 h1</text><text x="254" y="108">#2 h0</text><text x="316" y="108">#3 h1 OVR</text>
    <text x="432" y="108">#4 GAP</text><text x="495" y="108">#5 h0</text><text x="558" y="108">#6 h1</text><text x="620" y="108">#7 h0 OVR</text>
  </g>
  <line x1="371.6" y1="36" x2="371.6" y2="124" stroke="currentColor" stroke-width="2"/>
  <text x="300" y="140" fill="currentColor" font-size="11">t=164: DMA 가 반1 첫 칸에 씀 = k 9</text>
  <text x="372" y="156" fill="currentColor" font-size="11">반0(t=160 에 참) 은 resync 로 버림</text>
  <g fill="currentColor" font-size="11">
    <text x="114" y="84">32</text><text x="175" y="84">64</text><text x="236" y="84">96</text><text x="294" y="84">128</text>
    <text x="355" y="84">160</text><text x="416" y="84">192</text><text x="477" y="84">224</text><text x="538" y="84">256</text>
    <text x="599" y="84">288</text><text x="660" y="84">320</text>
  </g>
  <text x="10" y="186" fill="currentColor">끝나자마자 다음 펜딩 ISR 로(tail-chain) → 시작이 1 클럭씩 밀림: late = 0, 1, 2, 3.</text>
  <text x="10" y="204" fill="currentColor">3 + 33 = 36 = (N+1)·S → 반1 이 덮인 뒤 끝남 → OVR → 밀린 반0 버림 → late 0.</text>
  <text x="10" y="222" fill="currentColor">주기 P = 3 + 1 + ⌊(3 + 33)/32⌋ = 5 반쪽 (정상 3, OVR 1, 버림 1).</text>
</svg>
```

### Part B — 판정식과 읽기 순서

반쪽 h 를 처리한 뒤의 상태는 "h 가 찬 뒤 전송 수 k" 하나로 정해진다. 정답 = k ≥ N+1.

| k | DMA 위치 pos | h 의 플래그 | 판정 |
|---|---|---|---|
| 0 .. N−1 | 다른 반쪽 안 | 0 | ok |
| N | **h 의 첫 칸 (offset 0)** — 아직 안 씀 | 0 | ok ← off-by-one 함정 |
| N+1 .. 2N−1 | h 안, offset 1..N−1 | 0 | OVR (pos 로 앎) |
| 2N .. | 다른 반쪽 (pos 로는 안 보임) | **1** (h 가 다시 참) | OVR (플래그로 앎) |

그래서 `in_h = (pos / N == h) && (pos % N != 0)`, 결과 = `in_h || self_flag`. 플래그는 sticky 라 k 가 아무리 커도(3N, 4N …) true 로 남는다 — COMM-M11 의 UART 판정이 "정확히 N 바이트" 를 원리적으로 못 보는 것과 달리, 여기는 HT/TC 가 반쪽마다 따로 있고 ISR 이 자기 플래그를 지운 시점이 기준이라 사각지대가 없다(연습 4 의 DBM 은 있다).

두 레지스터를 읽는 사이에 DMA 가 전송 하나를 끼워 넣으면? 하네스가 접근 횟수 훅(BM-B04 방식)으로 "두 번째 읽기 직전에 전송 1개" 를 주입한다. N=8, 반쪽 0, `./solution -v` 실측:

```text
  k  truth  NDTR-first  flag-first
  6  ok     ok          ok        
  7  ok     ok          ok        
  8  ok     ok          OVR         <- false alarm
  9  OVR    OVR         OVR       
 10  OVR    OVR         OVR       
 11  OVR    OVR         OVR       
 12  OVR    OVR         OVR       
 13  OVR    OVR         OVR       
 14  OVR    OVR         OVR       
 15  OVR    OVR         ok          <- MISS
 16  OVR    OVR         OVR       
 17  OVR    OVR         OVR       
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">k = 2N−1 = 15 에서 전송 1개가 두 읽기 사이에 끼면 (N=8, h=0)</text>
  <text x="10" y="46" fill="currentColor" font-weight="bold">플래그 먼저 (틀림)</text>
  <rect x="10" y="56" width="200" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="74" fill="currentColor">① LISR 읽기 (k=15)</text>
  <text x="18" y="90" fill="currentColor">HTIF = 0 (h0 아직 다 안 덮임)</text>
  <rect x="230" y="56" width="140" height="40" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="238" y="74" fill="currentColor">DMA: h0[7] 씀</text>
  <text x="238" y="90" fill="currentColor">k=16, HTIF=1</text>
  <rect x="390" y="56" width="200" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="398" y="74" fill="currentColor">② NDTR=8 → pos=8</text>
  <text x="398" y="90" fill="currentColor">반쪽1 첫 칸 → "h0 밖"</text>
  <rect x="610" y="56" width="120" height="40" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="618" y="80" fill="currentColor" font-weight="bold">ok → 놓침</text>
  <text x="10" y="136" fill="currentColor" font-weight="bold">NDTR 먼저 (정답)</text>
  <rect x="10" y="146" width="200" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="164" fill="currentColor">① NDTR=9 → pos=7 (k=15)</text>
  <text x="18" y="180" fill="currentColor">h0 안, offset 7 → 이미 OVR</text>
  <rect x="230" y="146" width="140" height="40" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="238" y="164" fill="currentColor">DMA: h0[7] 씀</text>
  <text x="238" y="180" fill="currentColor">k=16, HTIF=1</text>
  <rect x="390" y="146" width="200" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="398" y="164" fill="currentColor">② LISR: HTIF=1</text>
  <text x="398" y="180" fill="currentColor">(pos 가 못 보면 플래그가 봄)</text>
  <rect x="610" y="146" width="120" height="40" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="618" y="170" fill="currentColor" font-weight="bold">OVR → 맞음</text>
  <text x="10" y="216" fill="currentColor">플래그는 0→1 로만 변한다(ISR 이 안 지우는 한) → 나중에 읽을수록 더 많이 안다.</text>
  <text x="10" y="234" fill="currentColor">pos 는 바퀴마다 되돌아간다 → 먼저 읽고, 놓친 한 바퀴는 나중 플래그가 잡는다.</text>
</svg>
```

증명(전송 1개 주입): NDTR 을 k₁ 에, 플래그를 k₂ ∈ {k₁, k₁+1} 에 읽는다. k₁ ≤ N 이면 pos 는 h 밖이거나 offset 0, k₂ ≤ N+1 < 2N 이라 플래그 0 → ok (정답 ok). N+1 ≤ k₁ ≤ 2N−1 이면 pos 가 h 안 → OVR. k₁ ≥ 2N 이면 플래그 1 → OVR. 모든 k 에서 정답과 같다. 반대 순서는 k₁ = 2N−1 에서 "플래그 0 + pos 가 다른 반쪽 첫 칸" 이 되어 놓치고, k₁ = N 에서 거짓 경보를 낸다 — 위 표의 15 와 8, 그리고 테스트가 두 반쪽 × k = 0..24 전부에서 이것만 다르다는 것을 단언한다. 놓침이 더 나쁘다: 덮인 데이터를 깨끗한 데이터로 내보낸다.

### Part B — 무작위 속성 테스트와 반례

500 개 무작위 설정 (N 2..64, S 1..24, 비용 0..2.6·반쪽 주기, 1/3 은 ISR 막힘 주입) → **9754 레코드, OVR 3798, 실제로 찢긴 레코드 2241** (실측). 단언: 모든 레코드에서 `OVR 플래그 == 정답(k ≥ N+1)`, 찢김 ⇒ OVR, OVR 아니면 평균·EMA 가 기준과 같음, GAP 없이 붙은 두 레코드는 절대 번호가 연속, `overruns` 카운터 = OVR 레코드 수. 검사 없는 반례 ISR(비용 600 = 처리 시작 전 이미 덮임)은 찢긴 레코드를 **플래그 없이** 내보내고 값이 기준과 다르다 — 테스트가 그 사실을 단언한다.

늦은 진입(더 높은 우선순위 ISR 이나 긴 임계 구역을 흉내 내 디스패치를 막음, N=16, S=8, 비용 32):

| 막힘 | 결과 (실측 단언) |
|---|---|
| 1000..1300 (300 클럭 > 반쪽 128) | 진입 때 HT+TC 둘 다 → late_entries 1, 레코드 없이 둘 다 버림, 다음 레코드 GAP |
| 1024..1124 (반쪽 7 을 100 클럭 늦게) | 100 + 32 = 132 < 17·8 = 136 → 무사, 40 레코드 전부 정상 |
| 1024..1130 (106 클럭) | 106 + 32 = 138 ≥ 136 → 그 반쪽 1 개만 OVR, 다음 GAP |

### Part C — DDS 를 깜빡한 보드의 기동 로그

`DDS=0` 이면 첫 바퀴 TC 직후 ADC 가 DMA 요청을 멈춘다. EOC 가 안 지워진 채 다음 변환이 끝나면 OVR. 메인 루프는 ISR 이 도는 동안 못 돌므로, ISR(반쪽 1, 비용 128) 이 끝난 1152 에야 본다. `./solution -v` 실측:

```text
   #  h  flags     ready  late  end    base   k  truth torn  mean   ema
   0  0  -          512     0    640     0   8  no      0   1143   1153
   1  1  -         1024     0   1152    32   0  no      0   1452   1456
   2  0  GAP       1664     0   1792    72   8  no      0   1869   1896
   3  1  -         2176     0   2304   104   8  no      0   2202   2214
…
[t=      0] pipe start N=32 buf=64 NDTR=64
[t=   1152] ADC OVR (SR=0x32 CR2=0x00000103) -> recover
[t=   1152] recovered #1: NDTR=64 CR=0x00022D19
ADC ovr_count=7, recoveries=1, halves=7
```

읽는 법: `SR=0x32` = OVR|STRT|EOC, `CR2=0x103` = ADON|CONT|DMA — **DDS 비트(0x200)가 없다**는 것이 원인이다. 레코드 1 의 `k=0` 은 TC 이후 DMA 전송이 0 개였다는 뜻. 복구 동안 변환 7 개(1040..1152 의 OVR 7 번, 절대 번호 64..71)가 사라졌고, 레코드 2 는 base 72 에서 GAP 으로 시작한다. 현장에서는 "부팅 직후 한 번만 데이터가 오고 멈춘다" 로 보고되는 버그다. HAL 에서는 `hadc.Init.DMAContinuousRequests = ENABLE` 이 이 비트다(확인 권장). `CIRC` 를 깜빡한 경우도 같은 모양이다: TC 에서 EN=0 → 요청이 서비스되지 않음 → OVR → 같은 복구(테스트가 둘 다 단언).

### Part C — 예산 표 (실제 단위)

`budget_calc`: 반쪽 주기 = N/fs, 처리 = (N·cps + fixed)/f_cpu, 부하 = 처리/반쪽, 최악 지연 = (N−1)/fs + 처리 (반쪽의 첫 샘플은 N−1 샘플을 기다린 뒤 처리까지). ok 는 정수 비교 `cyc·fs ≤ N·f_cpu`. 168 MHz Cortex-M4, 40 사이클/샘플 + 600 사이클/반쪽(ISR 진입·출력·로그 가정) — demo 실측:

```text
   fs[Hz]     N   half[us]   proc[us]  load[%]  latency[us]  ok
     8000    32     4000.0      11.19      0.3       3886.2  yes
    48000   256     5333.3      64.52      1.2       5377.0  yes
   200000   256     1280.0      64.52      5.0       1339.5  yes
  1000000    32       32.0      11.19     35.0         42.2  yes
  1000000   256      256.0      64.52     25.2        319.5  yes
  1000000  1024     1024.0     247.38     24.2       1270.4  yes
  2400000    32       13.3      11.19     83.9         24.1  yes
  2400000   256      106.7      64.52     60.5        170.8  yes
  2400000  1024      426.7     247.38     58.0        673.6  yes
```

(전체 15 줄 중 발췌.) N 을 키우면 고정 비용(600 사이클)이 더 많은 샘플에 나눠져 부하가 준다(2.4 MSPS: 83.9% → 58.0%). 대신 지연이 N 에 비례해 는다(24 µs → 674 µs). 제어 루프(전류 제어 20 kHz 등)는 지연이, 오디오/로깅은 부하가 먼저 막힌다. 2.4 MSPS 는 F4 ADC 12비트 최대 근처(ADCCLK 36 MHz / 15 사이클 — 데이터시트 확인 권장)라 이 표의 위쪽 끝이다. 실무 규칙: 부하는 ISR 지터·다른 IRQ 를 위해 70% 이하로, 그리고 **최악 ISR 지연**(ARM-A08)을 L 에 넣어 (N+1)·S 와 비교.

### 대안: F4 이중 버퍼 모드 (DBM/CT)

`SxCR.DBM`(bit 18) = 1 이면 스트림이 M0AR 버퍼와 M1AR 버퍼를 번갈아 쓴다. 한 버퍼가 끝날 때마다 TC, `CT`(bit 19) 토글, NDTR 재장전 (circular 자동). 장점: 두 버퍼가 붙어 있을 필요가 없고, **쓰지 않는 쪽 주소 레지스터를 동작 중에 바꿀 수 있어서**(RM0090, 확인 권장) 버퍼 풀에서 다음 빈 버퍼를 끼워 넣는 3중 이상 버퍼링·제로 카피 큐가 된다. 단점: 이벤트가 TC 하나라 "몇 버퍼 밀렸나" 를 플래그로 다 구분 못 한다 — 연습 4 에서 CT·NDTR·TCIF 판정이 k < 3M 에서는 정확하지만 k = 3M 에서 k = M 과 레지스터가 **완전히 같다**는 것을 전수로 보인다. circular + HT/TC 는 반쪽마다 플래그가 달라 그 사각지대가 없다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 37/37 passed, exit 0 (약 0.07 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_budget_sweep.c -o demo_budget_sweep && ./demo_budget_sweep   # 레슨 디렉터리에서 (solution.c 를 #include)
```

타깃 빌드에서는 `UNIT_TEST` 를 빼면 `REG_READ/REG_WRITE` 가 volatile MMIO, `BUF_READ(i)` 가 `adc_buf[i]`(`static volatile uint16_t adc_buf[2*NMAX]`, `ADC_BUF_ADDR = (uint32_t)(uintptr_t)adc_buf`), `SIM_WORK_*()` 는 빈 매크로, `nvic_enable_irq` 는 CMSIS `NVIC_EnableIRQ(DMA2_Stream0_IRQn)`, `LOG` 는 지연 로그 링으로 바꾼다. 드라이버 함수 본문은 그대로다.

## 7. 자주 하는 실수

**1. ISR 진입에서 `LIFCR = HTIF | TCIF`.** 처리할 HT 와 함께, 이미 서 있던(=이미 찬) 다른 반쪽의 TC 까지 지운다. 그 반쪽은 처리되지 않고, EMA 는 GAP 없이 다음 반쪽으로 넘어간다 — 무작위 테스트의 "조용한 빈칸" 단언이 잡는 종류. 고친 코드: `REG_WRITE(DMA_LIFCR, ht ? DMA_S0_HTIF : DMA_S0_TCIF);` 둘 다 서 있을 때만 의도적으로 둘 다 버리고 GAP.

**2. `pos / N == h` 만 본다 (offset 0 을 빠뜨림).** 비용이 정확히 반쪽 주기일 때(C = 512) DMA 는 h 의 첫 칸 직전에 서 있다(k = N). 이걸 OVR 로 세면 512 줄이 "1000 레코드 전부 OVR" 이 된다. 고친 코드: `(pos / p->n == h) && (pos % p->n != 0u)`.

**3. LISR 을 먼저 읽는다.** k = 2N−1 에서 전송 하나가 끼면 놓친다(5. 풀이 표). 고친 코드: NDTR → LISR. 일반 원칙: **단조로 커지는 증거(sticky 플래그)를 나중에 읽는다.** COMM-M11 의 "플래그 클리어 → NDTR" 순서와 같은 사고방식이다.

**4. `DDS` 나 `CIRC` 를 빠뜨린다.** 첫 바퀴는 완벽하게 돌고 멈춘다 → OVR. 벤치에서 "처음 몇 ms 데이터는 맞는데요" 가 단서. 고친 코드: `CR2 = ADON|CONT|DMA|DDS`, `CR |= CIRC`, 그리고 메인 루프 `pipe_health()` 로 OVR 을 감시·복구·로그.

**5. 스트림이 켜진 채로 다시 설정한다 / 남은 플래그를 안 지운다.** EN=1 동안 NDTR·M0AR 쓰기는 무시되어 옛 설정으로 돈다. 지난 실행의 TCIF 가 남아 있으면 TCIE 를 켜는 즉시 ISR 이 반쪽 1 의 쓰레기를 "처리" 한다. 고친 코드: `dma_arm` 의 ① EN=0 + 대기(상한) ② `LIFCR = DMA_S0_ALL` ③ 설정 ④ EN. 테스트 "남은 EN=1 + TCIF/HTIF/FEIF 위에서 재시작" 이 첫 레코드가 t = 144 의 반쪽 0 인지 본다.

**6. 복구에서 OVR 만 지운다 (`SR = ~OVR`).** 이 모델에서는 EOC 가 1 로 남아 있어서 DMA 를 다시 켠 직후 첫 변환이 또 OVR 이 된다. 실측(`pipe_health` 를 그렇게 바꿔 돌린 결과, 발췌):

```text
[t=   1152] ADC OVR (SR=0x32 CR2=0x00000103) -> recover
[t=   1152] recovered #1: NDTR=64 CR=0x00022D19
[t=   1252] ADC OVR (SR=0x32 CR2=0x00000303) -> recover
[t=   1252] recovered #2: NDTR=64 CR=0x00022D19
…
ADC ovr_count=191, recoveries=30, halves=2
```

복구 폭풍: 100 클럭마다 복구, 반쪽은 2 개만. 고친 코드: SR 을 통째로 지우거나 DR 을 한 번 읽어 비운다. 실제 F4 의 OVR/EOC 상호작용은 RM0090 ADC 장 "Overrun"/"Using the DMA" 절 확인 권장 — 요점은 **복구 뒤 다시 OVR 이 나는지까지 테스트**하는 것.

**7. 반쪽 경계가 다채널 프레임을 자른다.** 3 채널 스캔에 NDTR = 32 면 반쪽 16 이 프레임 3 으로 안 나뉘어, 뒤 반쪽이 ch1 부터 시작한다 — 채널이 한 칸씩 밀린 평균. 고친 코드: NDTR = 2·frames·channels (연습 2 `scan_layout_ok`).

**8. Cortex-M7 에서 invalidate 없이 반쪽을 읽는다.** DMA 는 캐시를 거치지 않고 SRAM 에 쓰므로 CPU 는 옛 줄을 본다. 고친 코드: 반쪽 시작 주소·크기를 32 B 정렬로 잡고 HT/TC 콜백 첫 줄에서 그 반쪽만 `SCB_InvalidateDCache_by_Addr`, 또는 MPU 로 non-cacheable (ARCH-M12, COMM-M11 연습 4).

## 8. 면접 Q&A

<details><summary>Q1. 왜 TC 하나만 쓰지 않고 HT 와 TC 두 개를 쓰나요?</summary>

TC 만 쓰면 TC 순간 DMA 는 이미 buf[0] 에 다음 샘플을 쓰기 시작한다 — 처리할 시간이 **한 샘플**(S 클럭)밖에 없다. 반쪽으로 나누면 h 가 찬 뒤 DMA 가 다른 반쪽 N 칸을 쓰는 동안 h 는 안전하다: 예산이 N 샘플(이 모델에선 덮어쓰기 시작까지 N+1 샘플)이 된다. 대가는 메모리 2배와 지연(반쪽 단위 처리). 꼬리질문 "그럼 반쪽 대신 1/4 씩?" → 이벤트가 더 잦아져 ISR 오버헤드가 늘고, 처리 가능한 시간은 "나머지 구역들이 채워지는 시간" 이라 버퍼 시간의 3/4 까지 늘어난다(반쪽은 1/2). 그런 설계가 필요하면 DBM + 버퍼 풀(3개 이상)이 깔끔하다.
</details>

<details><summary>Q2. NDTR 이 2N 이면 DMA 는 어디에 있나요? 0 을 읽을 수 있나요?</summary>

`pos = 2N − NDTR` 은 **다음에 쓸** 칸. NDTR = 2N → pos 0: 방금 TC 로 재장전됐고 buf[0] 은 아직 안 썼다. circular 에서는 0 이 되자마자 재장전되므로 0 을 읽을 일이 없다(모델 정의, RM 확인 권장). normal 모드에서는 끝나면 NDTR = 0 + EN = 0 이다. 꼬리질문 "반쪽 0 을 처리한 뒤 NDTR = 2N 이 보였다면 덮였나요?" → 아니다. 다른 반쪽이 막 다 찼을 뿐 h[0] 은 다음 전송이다 — 여기서 `pos % N != 0` 조건이 나온다.
</details>

<details><summary>Q3. 덮어쓰기를 어떻게 감지하나요? 읽기 순서가 왜 중요하죠?</summary>

ISR 진입 때 자기 플래그를 지우고, 처리 **끝**에서 NDTR 을 읽어 DMA 가 h 안(offset ≥ 1)인지, 그다음 LISR 을 읽어 자기 플래그가 다시 섰는지(한 바퀴 이상) 본다. 플래그는 시간에 따라 0→1 로만 바뀌는 단조 증거라 나중에 읽어야 두 읽기 사이의 진행까지 잡는다. 반대 순서는 k = 2N−1 에서 전송 하나가 끼면 "플래그 0, pos 는 다른 반쪽 첫 칸" 이 되어 놓친다 — 전수 테스트로 확인. 대안: 소프트웨어 반쪽 카운터 + 타임스탬프(DWT CYCCNT)로 "찬 시각 대비 경과 시간" 을 재는 방법도 있다. 그 경우도 카운터를 먼저, 시간을 나중에 읽는 식으로 같은 논리가 필요하다.
</details>

<details><summary>Q4. DDS 는 뭐고, 빠뜨리면 현장에서 어떻게 보이나요?</summary>

ADC 의 "DMA disable selection". 0 이면 DMA 컨트롤러가 마지막 전송(NDTR → 0)을 알린 뒤 ADC 가 더 이상 DMA 요청을 내지 않는다 — normal 모드 단발 수집용. circular 에서는 1 이어야 한다. 빠뜨리면 첫 버퍼 한 바퀴는 완벽하고, 그다음 EOC 가 안 비워져 OVR → ADC 정지. 증상은 "부팅 직후 한 번만 값이 갱신되고 멈춤". HAL 의 `DMAContinuousRequests` 가 이 비트다(확인 권장). 진단: SR 의 OVR, CR2 에 0x200 이 없는지 — 이 레슨 로그의 `CR2=0x00000103` 이 정확히 그 모습이다.
</details>

<details><summary>Q5. 처리 시간이 반쪽 주기와 정확히 같으면요? 예산은 어떻게 잡나요?</summary>

모델에서는 여유 0 으로 영원히 버틴다(512 줄). 실칩에서는 안 된다: ISR 진입 지연, 더 높은 우선순위 IRQ, 플래시 대기 상태, 캐시 미스가 지터를 만들고, 지터가 쌓이면 δ > 0 인 경우와 같다 — 100.2% 부하는 15 반쪽 뒤에 처음 터진다. 예산식: 최악 진입 지연 L_max + 처리 C ≤ (N+1)·S 는 "덮어쓰기 없음" 의 조건, C ≤ N·S 는 "밀리지 않음" 의 조건. 둘 다 여유를 두고(부하 ≤ 70% 정도), 최악 L 은 측정(ARM-A08, SIV-M03)으로 잡는다. N 을 키우면 고정 비용이 분산되지만 지연이 는다(예산 표).
</details>

<details><summary>Q6. 덮어쓰기를 감지한 뒤 무엇을 하나요? 검출이 너무 보수적이지 않나요?</summary>

이 레슨의 정책: 그 레코드에 OVR, 밀린 다른 반쪽도 버려(HT|TC 클리어) 지연을 0 으로 되돌리고, EMA 를 버리고 다음 레코드에 GAP. "최신 데이터 우선"(제어 루프에 맞음). 반대 정책 "밀린 반쪽은 멀쩡하니 처리" 는 처리량 우선이지만 지연이 계속 쌓여 결국 둘 다 펜딩(late entry)이 된다. 보수성: 검사는 "처리 끝에 DMA 가 h 에 있었나" 라서, 읽기 포인터가 쓰기 포인터보다 앞서 있었다면 데이터는 멀쩡해도 OVR 이다(demo 528~1024: OVR 500, torn 0). 좁히는 법: 반쪽을 먼저 로컬 버퍼로 복사(memcpy 는 처리보다 훨씬 빠름) → **복사 직후** 검사 → 처리는 복사본으로. 그러면 위험 구간이 복사 시간뿐이다. 메모리 N 칸이 더 든다.
</details>

<details><summary>Q7. 처리를 ISR 안에서 하나요, 태스크로 넘기나요?</summary>

이 레슨은 과제대로 콜백(ISR) 안에서 처리한다: 단순하고 지연이 가장 작지만, 그동안 같은/낮은 우선순위 IRQ 와 메인 루프가 굶는다(DDS 로그에서 health 가 1152 에야 돈 이유). 태스크로 넘기면(`xTaskNotifyFromISR`, RTOS-M02) ISR 은 반쪽 번호만 넘기고 바로 나간다 — 하지만 판정 논리는 똑같이 필요하다: 태스크가 처리 끝에 NDTR + 플래그(또는 ISR 이 올리는 반쪽 카운터)를 본다. 이때 ISR 이 카운터를 바꾸므로 카운터를 두 번 읽어 같은지 확인하거나 임계 구역(C-M14, ARM-M10)으로 읽는다.
</details>

<details><summary>Q8. Cortex-M7(STM32F7/H7)으로 옮기면 무엇이 바뀌나요?</summary>

D-cache. DMA 는 SRAM 에 직접 쓰므로 CPU 가 반쪽을 읽기 전에 그 범위를 invalidate 해야 한다(HT 에서 앞 반쪽, TC 에서 뒤 반쪽). 반쪽 경계가 32 B 캐시 라인에 맞아야 invalidate 가 옆 데이터를 날리지 않는다 → 버퍼 `aligned(32)`, N·2 B 를 32 의 배수로. 또는 MPU 로 그 영역을 non-cacheable. H7 은 DMA 가 닿을 수 있는 RAM 영역(DTCM 은 일부 DMA 가 못 씀 등)도 확인해야 한다(RM 확인 권장). 원리는 ARCH-M12.
</details>

<details><summary>Q9. 이중 버퍼 모드(DBM)와 circular + HT/TC 는 언제 무엇을 쓰나요?</summary>

circular + HT/TC: 버퍼 하나, 설정 단순, 반쪽마다 다른 플래그라 밀림 판정이 플래그로 완결된다. DBM: 버퍼 두 개(떨어져 있어도 됨), CT 로 현재 버퍼를 알고, 쓰지 않는 쪽 주소를 동작 중에 바꿀 수 있어 버퍼 풀을 돌리는 제로 카피 파이프라인(예: 처리 태스크가 버퍼를 오래 쥐고 있어도 되는 구조)에 맞다. 대신 TC 플래그 하나라 2 버퍼 이상 밀린 것을 플래그로 구분할 수 없다(연습 4: k = 3M 과 k = M 의 레지스터가 같음) → 타임스탬프나 처리 시간 상한 보장이 필요하다.
</details>

## 9. 추가 연습

### 연습 1 — NDTR + 플래그 → 새로 써진 샘플 수

`written_into(h, ndtr, self_flag, n)` 이 0..N 을 돌려준다(한 바퀴 이상이면 N). "h 가 찬 뒤 k 번 전송" 기준 모델과 N = 2..64, 두 반쪽, k = 0..5N 전부(20916 경우) 비교.

<details><summary>답</summary>

`self_flag ? n : (pos / n == h ? pos % n : 0)`, `pos = 2n − ndtr`. offset 0 이 자연스럽게 0 개가 된다 — 판정식 `written_into(...) > 0` 이 본문의 `pipe_reentered` 와 같다. 손 계산: N=8, h=0 → NDTR 16 과 8 은 0 개, 15 는 1 개.
</details>

### 연습 2 — 다채널 스캔: 반쪽이 프레임을 자르지 않게

버퍼 = `[ch0 ch1 … ch(C−1)]` 프레임의 반복. `scan_layout_ok(total, n_ch)` 와 `scan_half_means(buf, total, n_ch, h, out)` (채널별 반올림 평균). 2000 개 무작위 레이아웃을 "인덱스 % C" 로 채널을 직접 판정하는 기준과 비교, 3 채널 × NDTR 32 는 거절.

<details><summary>답</summary>

조건: total 짝수, total ≥ 2C, (total/2) % C == 0. 각 채널 c 는 반쪽 안에서 `h·half + f·C + c`. 실무에서는 NDTR = 2·frames·C 로 아예 계산해서 넣는다. 반쪽이 프레임을 자르면 뒤 반쪽의 첫 값이 ch1 이라 채널이 밀린다 — 테스트 버퍼는 채널 c 값을 1000·c + 잡음으로 만들어 `out[c] / 1000 == c` 로 밀림을 잡는다.
</details>

### 연습 3 — 첫 OVR 과 주기의 닫힌 식

`first_ovr(n, s, c)` = ⌈S/δ⌉ − 1 (δ = c − N·S > 0), `ovr_period(n, s, c)` = i* + 1 + ⌊(i*·δ + c)/(N·S)⌋. 레지스터 없이 시각만 굴리는 사건 시뮬레이션과 N 2..16 × S 1..12 × c 0..3NS 전 격자에서 비교 (OVR 수·레코드 수까지).

<details><summary>답</summary>

사건 시뮬레이션: 반쪽 i 는 R_i = (i+1)·N·S 에 참, 시작 = max(R_i, CPU 가 비는 시각), 끝 − R_i ≥ (N+1)·S 면 OVR 이고 끝 시각까지 찬 반쪽은 버림. 막힘이 없으면 late entry 는 생기지 않는다(테스트가 단언) — OVR 뒤 버림이 지연을 0 으로 돌리기 때문. 1000 반쪽 c = 513 에서 레코드 942, OVR 58 (demo 표의 941 은 마지막 반쪽 ISR 이 시뮬레이션 끝 시각 뒤에 시작해 잘린 것).
</details>

### 연습 4 — DBM 덮어쓰기 판정과 사각지대

DBM: 버퍼 b 를 처리한 뒤 CT, NDTR, TCIF 로 "b 에 새 샘플이 써졌나". `dbm_overrun = (ct == b && ndtr != M) || (tcif && ct != b)`. 모델 상태와 M = 1..64, b = 0/1, k = 0..4M 비교.

<details><summary>답</summary>

k < 3M 에서는 정답과 정확히 같다. k = 3M 은 CT = b, NDTR = M, TCIF = 1 로 k = M(다른 버퍼 하나만 참, b 는 아직 안 씀)과 **세 값이 모두 같다** — 테스트가 (m, b) 마다 정확히 이 한 점만 틀린다는 것(128 개)과 두 상태의 레지스터가 같다는 것을 단언한다. TC 플래그가 버퍼 구분 없이 하나이고 sticky 라 "몇 번" 을 모른다. 대책: 처리 시간 상한을 버퍼 하나 미만으로 보장, 또는 TC ISR 이 버퍼 시퀀스 번호를 올리게 하고(우선순위를 처리보다 높게) 그 번호로 판정.
</details>

### 연습 5 — 예산 역산

`max_n_for_latency(fs, lat_max, cps, fixed, cpu, nmax)` (지연이 N 에 대해 증가 → 이분 탐색), `min_n_for_load(fs, pm_max, …)` (부하가 N 에 대해 감소). 300 개 무작위 조건을 N = 2..2048 brute force 와 비교.

<details><summary>답</summary>

1 MSPS, 40 사이클/샘플 + 600 사이클, 168 MHz: 지연 ≤ 300 µs 인 최대 N = **240**, 부하 ≤ 30% 인 최소 N = **58** (실측). 손 계산: 지연 ≈ (N−1)·1 µs + (40N + 600)/168 µs ≤ 300 → N ≤ 240.2. 부하 (40N + 600)/(168·N) ≤ 0.3 → N ≥ 57.7. 두 조건을 같이 만족하는 N ∈ [58, 240] 중, 메모리와 캐시 정렬을 보고 64·2 또는 128·2 같은 값을 고른다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 16/16 passed
```

```text
1 MSPS: max N for latency<=300us = 240, min N for load<=30% = 58
16/16 passed
```

## 10. 핵심 정리

- 구조: ADC(CONT 또는 타이머 트리거) → DMA circular 2N (`P→M, 16비트, MINC, CIRC, HTIE, TCIE`) → HT = 반쪽 0, TC = 반쪽 1. ADC 는 `DMA|DDS` — DDS 없으면 한 바퀴 뒤 OVR.
- 설정 순서: EN=0 + 0 으로 읽힐 때까지 대기 → 그 스트림 플래그 전부 W1C → PAR/M0AR/NDTR → CR → EN. ISR 은 **처리하는 플래그 하나만** 지운다. HT+TC 동시 = 순서 모름 → 둘 다 버리고 GAP.
- 예산: 반쪽 h 가 찬 뒤 DMA 는 N 샘플 동안 다른 반쪽에 쓰고 (N+1) 번째에 h 를 덮는다. 덮어쓰기 ⇔ L + C ≥ (N+1)·S, 밀림 ⇔ C > N·S, 첫 OVR = ⌈S/δ⌉ − 1. 부하 100.2% 는 15 반쪽 뒤에야 터진다.
- 판정: `pos = 2N − NDTR`; OVR ⇔ (pos 가 h 안 **이고 offset ≠ 0**) 또는 h 의 플래그가 다시 섰다. **NDTR 먼저, 플래그 나중**(단조 증거를 나중에) — 반대는 k = 2N−1 에서 놓친다.
- 덮어쓴 뒤: OVR 표시, 밀린 반쪽 버림, 필터 상태 리셋, 다음 레코드 GAP — 조용한 빈칸 0. 검출은 보수적(처리 끝 기준) → 복사 직후 검사로 좁힌다.
- 메인 루프 health: `ADC_SR.OVR` → DMA=0, 재무장, SR 클리어(EOC 포함), DMA|DDS, SWSTART, 로그. 복구 뒤 다시 OVR 나는지까지 테스트.
- N 선택: 부하(고정 비용 분산)는 N↑ 에 좋고 지연((N−1)/fs + 처리)은 N↓ 에 좋다. M7 은 반쪽 invalidate + 32 B 정렬. DBM 은 버퍼 풀용, TC 하나라 깊은 밀림은 플래그로 못 셈.

## 참고자료

- STMicroelectronics, **RM0090** (STM32F405/415/407/417/427/437/429/439 Reference Manual) — DMA 장: `SxCR`(EN/HTIE/TCIE/CIRC/MINC/PSIZE/MSIZE/PL/DBM/CT/CHSEL), `SxNDTR`(circular 재장전), `LISR/HISR`·`LIFCR/HIFCR`(스트림별 6/16/22 비트 오프셋), DMA2 요청 매핑(ADC1 = Stream0/Stream4 채널 0), EN=1 동안의 쓰기 보호. ADC 장: `CR2.DMA/DDS/CONT/SWSTART`, `SR.EOC/OVR`, "Using the DMA" 와 overrun 복구 절차. 이 레슨의 주소·비트는 여기서 온 대표값을 기억에 의존해 적었다 — RM 확인 권장.
- STMicroelectronics, **AN4031** "Using the STM32F2, STM32F4 and STM32F7 Series DMA controller" — 스트림 설정 순서, 이중 버퍼 모드, FIFO, 주의 사항.
- STMicroelectronics, STM32CubeF4 HAL `stm32f4xx_hal_adc.c` — `HAL_ADC_Start_DMA`, `HAL_ADC_ConvHalfCpltCallback` / `HAL_ADC_ConvCpltCallback`, `ADC_InitTypeDef.DMAContinuousRequests` (DDS) — 버전별 소스 확인 권장.
- ARM, *Cortex-M4 Devices Generic User Guide* / *ARMv7-M Architecture Reference Manual* — 예외 진입·tail-chaining (이 레슨의 "펜딩이면 바로 다음 ISR" 모델의 근거).
- 관련 레슨: BM-B02 (mock 프레임워크), BM-B04 (W1C·접근 횟수 인터리브 훅), BM-B14 (타이머 — 실제로는 TRGO 로 ADC 트리거), BM-M07 (ADC 드라이버: 채널·샘플링 시간·보정), BM-M08 (DMA 드라이버: 플래그 위치, 재설정, TEIF), COMM-M11 (UART RX circular DMA + IDLE — 바이트 스트림판), ARCH-M12 (DMA 캐시 일관성), DS-A02 (ISR↔태스크 링·Bip buffer), C-M14 (ISR 공유 데이터), RTOS-M02 (ISR 지연 처리). 다음 단계: ARM-M10 (PRIMASK/BASEPRI 임계 구역), ARM-A08 (인터럽트 지연 분석 — 이 레슨의 L_max), SIV-M03 (인터럽트 지연·유실 검출).
