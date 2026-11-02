# N06F · 연습④ AI 가속기 · SoC 레벨 · 안전 — NPU · 전원/DVFS · secure boot · 기능 안전 · SLT
<!-- theme: pastel -->
> **묶음**: 연습④ AI 가속기 · SoC 레벨 · 안전 (블록 F1–F5) · **우선**: P0 ★ (F1 NPU 최우선, F5 다음) · **목록**: [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) · **이전/다음**: [N06E 고속 I/O, 센서](2026-11-01_N06E_plans_highspeed_io_sensor.html) / [N06G 플랫폼, 회귀, 자동화](2026-11-01_N06G_platforms_regression_automation.html) · **관련**: [deck §5.1 Q4/Q5/Q9, §5.4 Q22, §6.3, §7 #7/#9](2026-10-25_rivian_hm_screen_prep.html) · [성능 메트릭 SSD vs SoC](2026-10-30_perf_metrics_ssd_vs_soc.html) · [BV15 자동차, ISO 26262](2026-11-01_BV15_automotive_board_considerations.html) · [V11 INT8 golden](hmScreenPrep/problems/V11_npu_int8_golden/index.html) · [V4 perf 통계](hmScreenPrep/problems/V4_perf_bench_stats/index.html) · [BV12 shmoo/PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [BV03 전원 트리](2026-11-01_BV03_power_tree_sequencing_pmic.html) · [BV08 부트 흐름](2026-11-01_BV08_boot_flow_bringup_firmware.html) · **코드**: [onsitePrep/n06/f/](onsitePrep/n06/f/run.sh) (FP 허용오차 연구 · roofline 계산기 · ECC fault injection 캠페인, 실제 출력 [run_output.txt](onsitePrep/n06/f/run_output.txt))

## 0. TL;DR · 사용법

- **이 페이지로 답할 수 있는 것**: "How would you validate our NPU?" (★ 온사이트 확률 최상 — Rivian은 자체 NPU "NNE"를 RAP1에 넣었고 이 팀이 그 칩을 검증 [1][추정: 팀 범위]), "DVFS/thermal 검증", "secure boot 검증", "safety mechanism을 어떻게 증명하나", "SLT에 뭘 넣나".
- **블록 5개**: **F1 NPU ★★** (가장 길다) · F2 전원 관리 · DVFS · thermal · F3 secure boot · crypto engine · F4 기능 안전 (ISO 26262) fault injection · **F5 SoC 레벨 시나리오 · SLT 콘텐츠 ★**.
- **외울 것 3개 (F1)**:
  1. **INT8은 bit-exact, FP16/BF16은 "같은 반올림 입력 + 정확한 누적" reference 대비 scale-aware 허용오차** (`k·√K·u·Σ|a·b|`). 상대오차(`|ref|` 기준)·고정 절대오차는 둘 다 틀린 규칙 — [코드 출력](onsitePrep/n06/f/run_output.txt)으로 증명.
  2. **실패하면 mismatch 패턴으로 HW 유닛을 짚는다**: 마지막 열만 → partial tile, 한 채널만 → bias/scale 로딩, 전부 ±1 → rounding, 큰 값만 → accumulator 폭 ([V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html)).
  3. **성능은 roofline으로 "기대값"을 먼저 계산하고 측정을 그 대비 %로 말한다**: AI = ops/bytes, ridge = peak/BW (예: 8.19 TOPS / 51.2 GB/s = 160 ops/B).
- **연습 방법**: 블록 하나 고르기 → **10분 타이머** → §1의 7단계 틀대로 소리 내어 말하기(녹음) → 그 블록의 `### 2. 테스트 매트릭스`와 비교해 빠진 카테고리 체크 → `### 7. 꼬리 질문`의 `<details>`를 열기 전에 먼저 답해 보기.
- **30초 영어 답 (F1 headline)**: "I'd split NPU validation into four layers: numerical correctness per operator against a bit-exact golden model, structure-aware tests that hit tile boundaries, layouts and every SRAM bank, system behavior — concurrency with cameras on the same DDR, DVFS, thermal, hang recovery, ECC — and finally end-to-end networks for accuracy and performance against a roofline model. INT8 must be bit-exact; floating point is compared against a reference with the same rounded inputs and a tolerance that scales with K and the sum of magnitudes. When something fails, the mismatch pattern tells me which hardware unit to suspect."

## 1. 공통 틀 복습

자세한 건 [N06A 템플릿](2026-11-01_N06A_methodology_test_plan_template.html). 여기선 한 장만.

**10분 7단계 프레임**: ① clarify 30초 → ② 블록 그림 1분 → ③ 카테고리 매트릭스 3분 → ④ 우선순위·플랫폼 1분 → ⑤ 통과 기준·커버리지 1분 → ⑥ 자동화·회귀 1분 → ⑦ 리스크·"A0 첫 주" 1분 (+ 남은 시간 꼬리 질문)

| # | 카테고리 | NPU/SoC 블록에서 한 줄 예 |
|---|---|---|
| ① | 레지스터 | reset 값 · RW/RO/W1C · ID/version · perf counter clear |
| ② | 기본 기능 | 단일 연산자 golden 비교 |
| ③ | 설정 조합 | dtype × 연산자 × layout × OPP |
| ④ | 경계·corner | partial tile · −128/127 · K 최대 · 0 크기 |
| ⑤ | 인터럽트 | job-done · error IRQ · 누락 0 |
| ⑥ | 에러·negative | 잘못된 descriptor · SMMU fault · 잡 abort |
| ⑦ | 리셋·전원 | 블록 단독 reset · power gating · retention |
| ⑧ | 동시성·stress | NPU + 카메라 DMA + CPU가 같은 DDR |
| ⑨ | 성능 | TOPS · MAC util · roofline 대비 % · TOPS/W |
| ⑩ | PVT·margin | Vmin/Fmax shmoo with 최악 전력 패턴 |
| ⑪ | soak | 24–72 h · CRC signature · rule of three |
| ⑫ | 시스템/interop | 실제 컴파일러 산출물 · 런타임 · end-to-end 네트워크 |

- Staff 시그널 3개: **DV가 덮은 건 반복 안 함 · 종료 기준 먼저 합의 · 같은 테스트를 FPGA → 실리콘 → SLT로 재사용**.

## F1 · NPU / AI accelerator (★★ 최우선)

### 0. 🔰 이 블록이 뭔가

- **비유**: NPU는 **곱셈만 하는 거대한 공장**이다. 바닥에 계산기 수천 대가 바둑판처럼 깔려 있고(MAC array), 옆에 작은 자재 창고(on-chip SRAM)가 있으며, 트럭(DMA)이 멀리 있는 큰 창고(DDR)에서 자재(입력 이미지·가중치)를 날라 온다. 공장장(command processor)은 컴파일러가 써 준 **작업 지시서(command stream)** 대로 "이 자재를 이 계산기 블록에 넣고, 결과를 저기 쌓아라"를 반복한다. 공장이 느린 이유는 둘 중 하나다: **계산기가 바쁘거나(compute bound), 트럭이 늦거나(memory bound)**.
- **정확한 정의**: 신경망 추론의 핵심 연산(행렬곱 GEMM, convolution)을 **저정밀 정수(INT8)·FP16/BF16**으로 대량 병렬 처리하는 고정 기능 + 일부 프로그래머블 가속기. 보통 ① MAC array(systolic 또는 dot-product 엔진) ② vector/post-processing unit(activation, pooling, **requantization**, eltwise) ③ scratchpad SRAM(여러 bank) ④ DMA 엔진(DDR ↔ SRAM, 보통 double buffering) ⑤ command processor/sequencer ⑥ CPU용 레지스터 인터페이스(APB/AXI-lite) ⑦ DDR로 가는 AXI/NoC master(+SMMU) ⑧ 인터럽트 ⑨ perf counter ⑩ SRAM ECC·watchdog 같은 안전 기능.
- **RAP1 공개 정보**: Rivian 자체 neural net engine(NNE)이 RAP1 칩 위에서 돈다, **ACM3 모듈 = 1600 sparse INT8 TOPS**, 결정적 지연(deterministic latency) 강조, ASIL 요구사항을 겨냥 [1][2][확인됨]. 칩당 800 TOPS · Cortex-A720AE 14개 + Cortex-R52 8개는 2차 보도 [4][추정]. Cortex-A720AE 사용은 Arm 발표로 확인 [3]. **NNE 내부 구조(MAC 수·SRAM 크기·dtype)는 비공개 → 이 페이지의 숫자는 전부 가정** [추정].

```text
              CPU cluster (Arm)                      camera / ISP DMA        other masters
                   │ APB/AXI-lite (regs)                    │                     │
                   ▼                                        ▼                     ▼
 ┌──────────────────── NPU (NNE) ────────────────────┐   ┌──────── NoC / AXI interconnect ────────┐
 │ cmd processor ◄── command stream (compiler output) │   │    QoS · arbitration · SMMU            │──► DDR ctrl ──► LPDDR
 │      │                                             │   └───────────────▲────────────────────────┘
 │      ▼                                             │                   │ AXI master (read wgt/act, write out)
 │  DMA in ──► SRAM banks (act / wgt, double buffer) ─┼───────────────────┘
 │                 │                                   │
 │                 ▼                                   │
 │        MAC array  (e.g. 64x64 INT8, int32 acc)      │   perf counters: busy / stall / DDR bytes
 │                 │                                   │   safety: SRAM ECC, job watchdog, parity
 │                 ▼                                   │
 │   vector unit: bias · requant · ReLU · pool · LUT   │──► IRQ (job done / error) ──► GIC
 │                 │                                   │
 │  DMA out ◄──────┘                                   │   clocks: NPU PLL (DVFS) · power domain (gating)
 └─────────────────────────────────────────────────────┘
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| MAC | Multiply-Accumulate | 곱하고 더하기 1번 = 2 ops | TOPS 계산의 단위 |
| INT8 · zero point · scale | 8-bit 정수 양자화 | 실수 = scale × (정수 − zero point) | 비교가 **bit-exact** 여야 하는 이유 (정수 연산) |
| requant | requantization | int32 누적값을 scale 곱해 다시 int8로 | 반올림 규칙 하나로 ±1 mismatch가 남 |
| accumulator | 누적기 | 곱의 합을 담는 넓은 레지스터 (보통 int32) | 폭이 모자라면 큰 K에서 wrap |
| FP16 · BF16 | half · brain float | 16비트 실수. FP16 = 지수 5·가수 10, BF16 = 지수 8·가수 7 [6][7] | 누적 순서에 따라 비트가 달라짐 → 허용오차 필요 |
| ULP · u | unit in the last place · unit roundoff | 그 형식에서 한 칸 차이. fp32 u = 2^-24 | 허용오차의 "자" |
| tile | 타일 | MAC array 한 번에 처리하는 조각 (예 64×64) | 마지막 **partial tile** 이 버그 단골 |
| layout | 메모리 배치 | NCHW / NHWC / 블록 포맷, padding, stride | 주소 계산 버그 = 일부 채널만 틀림 |
| golden model | 기준 모델 | 아키텍처·컴파일러 팀의 bit-accurate C 모델 | "정답"의 출처 |
| TOPS · sparse TOPS | tera ops/s | peak = 2 × MAC 수 × 클럭. sparse = 0 건너뛰기 가정한 명목값(보통 dense의 2배) | 마케팅 숫자 vs 측정 숫자 구분 |
| MAC utilization | MAC 사용률 | 실제 연산 / (peak × 시간) | 성능 KPI |
| roofline · AI · ridge | arithmetic intensity | AI = ops/bytes, ridge = peak/BW. AI < ridge 이면 memory bound [5] | utilization이 낮은 이유 설명 |
| TOPS/W | 전력 효율 | 달성 TOPS / 측정 전력 | 차량 전력·발열 예산 |
| command stream · descriptor | 작업 지시서 | 컴파일러가 만든 레이어별 명령 목록 | 실패 재현 = 같은 stream 재생 |
| hang · job watchdog | 멈춤 · 잡 타임아웃 | 잡이 안 끝나면 감지하고 블록 reset | 차량에선 "멈춤 = 안전 문제" |

### 1. 먼저 물어볼 것 (clarify)

- "Which data types are in scope — INT8 only, or FP16/BF16 too — and what's the accumulator width and the rounding rule in requant?" → 가정: INT8 (int32 acc, gemmlowp식 정수 requant) 주력 + FP16/BF16 경로 일부.
- "Is there a bit-accurate C model from the architecture or compiler team, and does the compiler already emit command streams, or do we hand-write descriptors for bring-up?" → 가정: bit-accurate C 모델 있음, 컴파일러는 A0 시점에 기본 연산자만 안정.
- "Which operators run on the NPU and which fall back to the CPU? Any non-linear ops via lookup tables?" → 가정: conv/depthwise/FC/pool/eltwise/activation(LUT) on NPU.
- "What's in scope for me — functional only, or also performance and power sign-off? What are the targets — frames per second per camera, latency budget?" → 가정: 기능 + 성능 + 전력, 목표는 아키텍처 모델 대비 % [추정].
- "What's available pre-silicon — an FPGA image with a scaled-down array, or emulation only?" → 가정: FPGA에 축소 array(예 16×16)로 기능, 에뮬로 full-size 일부 [추정].
- "Which safety mechanisms does the NPU have, and what ASIL is the target?" → 가정: SRAM ECC, job watchdog, 레지스터 parity; ASIL 등급은 비공개 [1][추정].

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | reset 값 · RW/RO/W1C · ID/version · perf counter clear | 전 레지스터, 스펙 표 자동 생성 | 둘 다 | 스펙과 100% 일치 | P0 |
| 기본 기능 | 단일 INT8 GEMM / 1×1 conv golden 비교 | 8×8×8 부터 시작, seed 고정 | 둘 다 | bit-exact, mismatch 0 | P0 |
| 연산자별 | conv 1×1·3×3·stride 2·dilation, depthwise, FC, pool, eltwise add, concat, ReLU/ReLU6, LUT activation | 연산자 × per-tensor/per-channel | 둘 다 | INT8 bit-exact · LUT는 golden LUT와 bit-exact | P0 |
| 수치 corner | −128/127, zero point 극단, rounding tie(.5), saturation, relu clamp, K 최대에서 acc 최대 | directed 벡터 + random corner 주입 | 둘 다 | bit-exact; K_max × 128 × 128 < 2^31 확인 | P0 |
| FP16/BF16 | fp16/bf16 GEMM, denormal·Inf·NaN, 누적 dtype 확인 | random + cancel + one-hot | 둘 다 | `abs(hw−ref) ≤ 2√K·u·Σ abs(a·b)` (아래 2a), NaN/Inf는 스펙대로 | P1 |
| 경계 (tile) | M, N, K = 1 · tile−1 · tile · tile+1 · 비배수 · 최대, 모든 SRAM bank | shape sweep (수천 개) | FPGA 주, Si 샘플 | bit-exact | P0 |
| 레이아웃 | NCHW/NHWC/블록 포맷, channel padding, pitch, 비정렬 base, 큰 주소(>4 GB) | layout × shape | 둘 다 | bit-exact + 출력 버퍼 앞뒤 guard canary 불변 | P1 |
| 인터럽트 | job-done IRQ, error IRQ, coalescing, mask/clear 순서 | 10^6 잡 연속 | 둘 다 | 잡당 IRQ 1회, 누락·중복 0 | P0 |
| 에러·negative | 잘못된 opcode, 범위 밖 주소, 정렬 위반, SMMU fault, 실행 중 abort | 주입 테스트 | 둘 다 | 정의된 error code + IRQ, hang 없음, 다음 잡 정상 | P1 |
| hang 복구 | 의존성 끊긴 descriptor로 고의 stall → job watchdog → 블록 soft reset → 재실행 | 1000회 반복 | Si 주 | 감지 ≤ watchdog 설정값 [추정], reset 후 첫 잡 bit-exact, 상태 잔재 없음 | P1 |
| 리셋·전원 | NPU 단독 reset, power gating off/on, 재초기화 | 1000 cycle | Si | 매 cycle golden pass, 레지스터 reset 값 복귀 | P1 |
| 동시성 | 잡 큐 우선순위·선점, (있다면) 멀티 NPU 코어, NPU + 카메라 DMA + CPU memcpy 동시 | traffic generator로 DDR 부하 0/50/90% | Si (FPGA는 기능만) | bit-exact 유지, 잡 latency p99 ≤ 예산 [추정] | P1 |
| 성능 (micro) | GEMM/conv shape sweep → 달성 TOPS, MAC util, DDR bytes | 클럭 고정, 캐시·DVFS 고정 | Si (FPGA는 비율만) | 측정 ≥ roofline 모델의 X% [추정: 아키텍처팀과 합의] | P1 |
| 성능 (network) | end-to-end 네트워크 latency p50/p99, inferences/s, DDR BW | 단독 vs 전체 카메라 부하 | Si | 프레임 예산 이내 (예: 30 fps면 33 ms 안에 전체 파이프라인) [추정] | P1 |
| 전력 효율 | TOPS/W, power virus(최대 toggle) 최대 전류, IR drop | rail 전류 측정 | Si | 전력 ≤ 예산, Vmin 마진 유지 | P1 |
| 정확도 (E2E) | 실제 검출/세그멘테이션 네트워크 출력 | dataset 부분집합 N장 | Si (FPGA 축소판) | 출력 tensor가 컴파일러 bit-accurate sim과 bit-exact; 정확도 ≥ FP32 기준의 99% (MLPerf식 [8]) | P1 |
| 결정성 | 같은 입력 N회 → 같은 출력 + latency 분포 | 1000회 | Si | 출력 bit-identical, latency jitter ≤ X [추정] | P1 |
| ECC/안전 | SRAM ECC 1-bit/2-bit 주입, 레지스터 parity, 카운터·IRQ | inject 기능 [추정 존재] | Si | 1-bit: 정정+CE 카운트, 2-bit: UE → 잡 에러 보고 (→ F4) | P1 |
| DVFS | 각 OPP에서 golden, 전환 중 잡 실행 | 전 OPP × 전환 1000회 | Si | bit-exact, 전환 중 hang 0 (→ F2) | P1 |
| thermal | throttling 임계 동작, 고온·저온에서 golden | Tj −40 / 25 / 125 °C [추정 등급] | Si | 정확도 불변, 성능 저하가 정책대로 | P1 |
| PVT margin | Vmin/Fmax shmoo with GEMM stress (SRAM·MAC 최악 toggle) | V × F × T × 코너 칩 | Si | 공칭 대비 margin ≥ guardband [추정] | P2 |
| soak | 랜덤 shape + 실제 네트워크 루프, 출력 CRC signature 비교 | 24–72 h, 여러 보드 | Si | mismatch 0; 실패율 상한 ≈ 3/n (rule of three) | P2 |
| 시스템/interop | 실제 드라이버·런타임·컴파일러 산출물, 멀티 테넌트(여러 모델 번갈아) | 릴리스 컴파일러 버전 고정 | Si | 모든 레퍼런스 모델 pass | P1 |

#### 2a. Golden 비교 정책 — INT8 bit-exact vs FP16/BF16 허용오차 (★ 면접에서 꼭 나옴)

| 데이터 경로 | 비교 방식 | reference를 어떻게 만드나 | 허용 |
|---|---|---|---|
| INT8 (int32 acc, 정수 requant) | **bit-exact** | 같은 수식(예: SRDHM → RDBPOT, tie 규칙까지)의 C 모델 | 0 mismatch. ±1 이 있으면 rounding 버그 |
| INT8 비선형 (LUT sigmoid 등) | bit-exact vs golden LUT | LUT 생성 스크립트와 같은 테이블 | 0 mismatch (LUT 자체 정확도는 별도 스펙) |
| FP16/BF16 GEMM | **scale-aware 허용오차** | **HW와 똑같이 반올림한 입력** + 정확한 누적(fp64 `fsum`) | `abs(hw−ref) ≤ k·√K·u_acc·Σ abs(a·b)` (통계적), 상한 `γ_K·Σ abs(a·b)`, `γ_K = K·u/(1−K·u)` |
| 누적 순서가 스펙에 고정된 경우 | bit-exact 가능 | 순서까지 같은 C 모델 | 0 mismatch |
| 네트워크 출력 | bit-exact vs 컴파일러의 bit-accurate 시뮬레이터 | 같은 컴파일 산출물 | 0 mismatch |
| 네트워크 정확도 | 정확도 지표 | FP32 원본 모델 | 예: FP32의 ≥ 99% (MLPerf 기준이 이 형태 [8]) — 이건 **양자화 품질** 이지 HW 버그 판정이 아님 |

- **허용오차 정하는 순서**: ① 스펙에서 **누적 dtype·순서·반올림 모드** 확인 → ② reference는 **입력 반올림을 HW와 동일하게** (안 그러면 입력 양자화 오차를 HW 오차로 착각) → ③ 이론 상한(γ_K)과 통계 규칙(√K) 계산 → ④ **알려진 정상 구현 2개(예: 순서만 다른 모델, 이전 칩)** 로 오차 분포를 재서 k 결정 → ⑤ **버그 모델(누적 fp16, 항 하나 누락)** 이 그 허용오차에 걸리는지 확인 (= test of the test) → ⑥ 허용오차로 못 잡는 작은 버그는 **directed 벡터(one-hot)** 로 잡는다.
- **실제 실행 결과** (`onsitePrep/n06/f/fp_tolerance.py`, fp16 입력, fp32 누적, 벡터 50개 = random 48 + 상쇄 1 + one-hot 1). fail 열은 각 규칙에서 실패한 벡터 수 (발췌 — K=64/256과 bf16 표는 run_output.txt):

```text
K= 1024  N=50 vectors (48 random + 1 cancel + 1 onehot)  gamma_K/u32=1024.1
  model       worst_err_ulpS    err/tol  exact  bound  stat  rtol  atol
  spec_seq              1.12      0.001     47      0     0     1     0
  spec_tile             0.21      0.000     41      0     0     0     0
  bug_f16acc        14977.86     14.626     49     37    48    45    41
  bug_drop       16777216.00  16383.000     50     44    50    47    46

K= 4096  N=50 vectors (48 random + 1 cancel + 1 onehot)  gamma_K/u32=4097.0
  model       worst_err_ulpS    err/tol  exact  bound  stat  rtol  atol
  spec_seq              1.19      0.000     49      0     0     1     0
  spec_tile             0.33      0.000     41      0     0     1     0
  bug_f16acc        16528.20      4.034     49     10    47    45    47
  bug_drop       16777216.00   4095.000     50     18    48    43    48

== two CORRECT implementations, different summation order ==
K=  256: spec_seq vs spec_tile differ bit-wise in 40/50 outputs
K= 4096: spec_seq vs spec_tile differ bit-wise in 46/50 outputs

== trap: reference computed from UNROUNDED fp64 inputs (K=1024, spec_seq HW) ==
  fp16: worst err/bound =    0.4 (pass), err/stat =     6.6 (FALSE FAIL)  <- input quantization, not HW error
  bf16: worst err/bound =    2.9 (FALSE FAIL), err/stat =    47.1 (FALSE FAIL)  <- input quantization, not HW error
```

- **읽는 법 (면접에서 말할 결론 5개)**:
  1. **정상 구현 두 개(spec_seq, spec_tile)도 bit 단위로 46/50 다르다** → 순서가 스펙에 없으면 FP bit-exact 비교는 불가능.
  2. 정상 구현의 실제 오차는 **≈1 u·Σ abs(ab)** 수준인데 이론 상한 γ_K는 **K u·Σ** → K=4096에서 상한은 4000배 느슨해서 **fp16 누적 버그가 50개 중 10개만 걸림**. 통계 규칙(2√K)은 정상 0 fail, 버그 47 fail.
  3. **상대오차 규칙(1e-3·abs(ref))은 상쇄 벡터(정답 = 0)에서 정상 HW도 실패** (rtol 열의 1) → 잘못된 규칙.
  4. **항 하나 누락(bug_drop)은 random 벡터로는 놓칠 수 있다** (stat 규칙에서도 K=4096에서 48/50) → **one-hot directed 벡터는 항상 잡음**. 허용오차 + directed 테스트를 같이 쓰는 이유.
  5. **reference를 반올림 안 한 fp64 입력으로 만들면 정상 HW가 실패** (bf16 47배 초과) → 입력 반올림을 reference에 똑같이.

#### 2b. Mismatch 패턴 → 의심 유닛 (INT8 코드는 [V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html))

| 패턴 | 의심 | 다음 행동 |
|---|---|---|
| 마지막 몇 열/행만 틀림 (N % tile ≠ 0 일 때) | partial tile 마스크·K 루프 끝 | tile±1 shape만 sweep |
| 한 출력 채널만 틀림 | 그 채널 bias/scale/shift 로딩 | per-channel 파라미터 덤프 비교 |
| 모든 위치 ±1, 절반쯤 | requant 반올림(tie 규칙, floor vs round) | tie 값 directed 벡터 |
| abs(acc) 큰 곳만 크게 틀림 | accumulator 폭/포화 | K 최대 + 극단값 벡터 |
| 특정 SRAM bank 주소 범위만 | bank 디코딩·ECC·타이밍 | bank별 패턴, 전압 내려 margin 확인 |
| 일정 stride로 반복 | layout/pitch 계산, 주소 비트 | 주소 비트 walking 테스트 |
| 랜덤 위치, 부하·온도 의존 | 타이밍 margin, DMA/compute race | DDR 부하·V·T sweep, double buffer 동기화 확인 |
| 첫 잡만 틀림 / reset 직후만 | 초기화 순서, 레지스터 기본값 | reset 후 레지스터 덤프 diff (FPGA vs Si) |

#### 2c. 성능 — roofline 계산 예 (손으로 1번, 코드로 확인)

- **가정 NPU** [추정, RAP1 아님]: 64×64 = 4096 INT8 MAC @ 1.0 GHz → peak = 2 × 4096 × 10^9 = **8.192 TOPS**. DDR = LPDDR5-6400 × 64 bit = 6400 MT/s × 8 B = **51.2 GB/s**. ridge = 8.192e12 / 51.2e9 = **160 ops/B** (deck·[성능 메트릭](2026-10-30_perf_metrics_ssd_vs_soc.html)과 같은 가정 — 여기선 **레이어별 계산과 tiling 영향**만 추가).
- **손 계산 [예시] — 카메라 해상도 conv 3×3, 480×272, 64→64 채널, INT8, batch 1**:
  - MACs = 480 × 272 × 64(out) × 3 × 3 × 64(in) = 130,560 × 36,864 = **4.813 × 10^9** → ops = 9.63 × 10^9
  - bytes (각 tensor가 DDR을 1번만 지난다고 가정) = 입력 130,560 × 64 = 8.36 MB + 출력 8.36 MB + 가중치 3·3·64·64 = 36.9 KB ≈ **16.75 MB**
  - AI = 9.63e9 / 16.75e6 ≈ **575 ops/B > 160** → compute bound → 최소 시간 = 9.63e9 / 8.192e12 ≈ **1.18 ms**
  - 만약 SRAM이 작아서 출력 채널을 8개씩 나눠 계산하고 입력을 8번 다시 읽으면 bytes = 8 × 8.36 + 8.36 ≈ 75 MB → AI ≈ 128 **< 160 → memory bound** 로 바뀜 → 1.47 ms (25% 느림). **같은 레이어도 컴파일러 tiling에 따라 bound가 바뀐다.**
- **코드 출력** (`roofline.py`, 실제 실행, §2는 일부 행 발췌):

```text
== 1. per-layer roofline (batch 1, INT8, ideal reuse: every tensor touches DDR once) ==
  layer                                 MMACs      MB      AI   map   TOPS  %peak    bound     t_us
  conv3x3 56x56 64->64 (ResNet st1)     115.6    0.44   527.2  1.00   8.19 100.0%  compute     28.2
  conv1x1 28x28 512->128                 51.4    0.57   181.0  1.00   8.19 100.0%  compute     12.5
  conv3x3 480x272 64->64 (camera)      4813.0   16.75   574.7  1.00   8.19 100.0%  compute   1175.0
  dwconv3x3 112x112 C=32                  3.6    0.80     9.0  0.01   0.06   0.8%  MAPPING    112.9
  conv3x3 56x56 3->64 (stem-like)         5.4    0.21    51.1  0.05   0.38   4.7%  MAPPING     28.2
  FC 2048->1000 batch 1                   2.0    2.06     2.0  0.98   0.10   1.2%   MEMORY     40.1
  FC 2048->1000 batch 16                 32.8    2.10    31.2  0.98   1.60  19.5%   MEMORY     41.0

== 2. same camera layer, SRAM too small -> output-channel tiling re-reads the input ==
  camera layer, Cout tile=16 (x4 input)   4813.0   41.82   230.2  1.00   8.19 100.0%  compute   1175.0
  camera layer, Cout tile= 8 (x8 input)   4813.0   75.24   127.9  1.00   6.55  80.0%   MEMORY   1469.5
  camera layer, Cout tile= 4 (x16 input)   4813.0  142.09    67.7  1.00   3.47  42.3%   MEMORY   2775.1

== 3. measured vs model  [example numbers, not real silicon] ==
  ops = 9.63 GOP ; roofline time = 1175 us ; measured = 1650 us
  achieved = 5.83 TOPS = 71.2% of peak, 71.2% of this layer's roof
  TOPS/W  = 5.83 / 6.0 W = 0.97 TOPS/W
  cross-check: MAC-busy counter 1174000 / 1650000 cycles = 0.712 vs timer-based 0.712 -> consistent
```

- **세 번째 천장 "MAPPING"**: depthwise conv나 입력 채널 3개짜리 stem은 DDR도 MAC도 한계가 아닌데 **64×64 array의 대부분이 놀아서** 느리다 (map 0.01–0.05). roofline 두 선(compute·memory)만 말하면 이걸 놓친다 — "the compute roof itself depends on how well the layer shape maps onto the array".
- **FC batch 1은 AI ≈ 2 → peak의 1.2%**: 가중치를 한 번 쓰고 버리기 때문. batch 16이면 AI 31, 19.5%. 차량 추론은 batch 1(프레임 단위)이 많아서 FC/attention 류가 memory bound가 되기 쉽다 [추정].
- **sparse TOPS 주의**: 2:4 structured sparsity를 가정한 명목값은 dense의 2배. "1600 sparse INT8 TOPS" [1]는 dense 기준으로 800 TOPS 정도로 읽는 게 안전 [추정: 2:4 가정]. 측정 리포트엔 **dense/sparse, dtype, batch, 클럭을 이름에 포함**.
- **측정 신뢰도 체크**: timer 기반 달성률과 **MAC-busy perf counter** 비율이 일치해야 한다 (위 출력 0.712 vs 0.712). 어긋나면 카운터 정의·타이머 주파수·측정 구간 중 하나가 틀림. 통계·반복·warm-up은 [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html).

### 3. 우선순위 · A0 첫 주

- **P0 bring-up (A0 첫 주, NPU 차례가 오면 — DDR·클럭이 먼저 살아야 함, deck §5.1 Q1)**:
  1. NPU 전원 도메인 on, 클럭 enable, reset 해제 → ID/version 레지스터 읽기 → **레지스터 sanity** (자동 생성 표)
  2. **가장 작은 INT8 GEMM 1개** (8×8×8, SRAM에서 SRAM, DMA 없이 가능하면) → bit-exact → job-done IRQ 확인
  3. DMA 경유 DDR → NPU → DDR 같은 GEMM → bit-exact (DDR 경로·SMMU 매핑 확인)
  4. FPGA에서 이미 통과한 **연산자 smoke set** (각 연산자 1 shape) + **tile 경계 shape 10개**
  5. 첫 **실제 네트워크 1개** (컴파일러 산출물) → 출력 tensor bit-exact vs 컴파일러 시뮬레이터
- **P1 (2–6주)**: 전체 shape sweep, layout, 에러·hang 복구, 동시성(DDR 부하), DVFS/thermal, ECC 주입, 성능 micro + network, 전력·TOPS/W, 결정성.
- **P2 (특성화·양산 준비)**: PVT shmoo(Vmin/Fmax with stress), 72 h soak, 코너 칩, SLT용 축약 콘텐츠 (→ F5).
- **일부러 안 하는 것**: MAC array 내부 각 PE의 기능 조합 전수(= DV가 RTL 시뮬·formal로 커버), 컴파일러 최적화 정확성 자체(컴파일러 팀 테스트), 양자화 정확도 튜닝(ML 팀). **나는 "실리콘에서만 보이는 것" — 실제 클럭·전압·온도, 실제 DDR 경합, 실제 전력, 긴 시간 — 과 HW/SW 경계에 집중**한다고 말하기.

### 4. 장비 · 셋업 · 관찰 지점

```text
 Host (Python)                                    Target board (A0 silicon)
 ┌──────────────────────────┐   JTAG / UART      ┌────────────────────────────────────────────┐
 │ stimulus gen (seeded)    │──────────────────► │ bare-metal runner (CPU)                    │
 │ golden C model / sim     │   PCIe or Ethernet │   load weights/acts → DDR                  │
 │ compare + mismatch diag  │ ◄──────────────────│   kick NPU cmd stream → wait IRQ           │
 │ results DB (chip, V/F/T, │   results / dumps  │   read perf counters, CRC of outputs       │
 │  build hash, seed)       │                    │   traffic generator on other masters       │
 └───────────▲──────────────┘                    └───────┬──────────────┬─────────────────────┘
             │ PSU / PMIC (V sweep), thermostream (T)    │ NPU rail      │ GPIO marker (job start/end)
             └────────────── control ─────────────────── shunt + DAQ    scope / logic analyzer
```

- **Stimulus/golden 흐름 두 가지**: ① host에서 golden 계산 → 출력 전체 readback 비교 (느리지만 mismatch 위치를 앎, bring-up·디버그용) ② golden 출력의 **CRC/signature만 타깃에 저장** → 타깃이 자체 비교 (빠름, soak·SLT용). 실패하면 ①로 재생.
- **읽을 것**: NPU status/error 레지스터, error code, job ID, perf counter(cycles busy/stall, DDR read/write bytes, SRAM bank conflict [추정 존재]), ECC CE/UE 카운터·주소 로그, watchdog 상태, DDR 컨트롤러·NoC 성능 모니터, 온도 센서·현재 OPP.
- **전력**: PMIC telemetry(있으면) + 정밀 shunt·DAQ로 NPU rail 전류. power virus 패턴에서 **droop을 scope로** (BV12 PI 관점).
- **타이밍**: job 시작/끝에 GPIO 토글 → scope로 IRQ latency와 software 측정 비교.
- **Linux 단계**(드라이버가 있으면): `perf stat`으로 CPU 측 오버헤드, 런타임 trace, `/sys/class/thermal/thermal_zone*/temp`, devfreq `cur_freq` [추정: 드라이버 지원에 따름].

### 5. 잘 터지는 버그

| 증상 | 그럴듯한 원인 | 플랜에서 잡는 곳 |
|---|---|---|
| 마지막 열/채널만 틀림 | partial tile 마스크 / K 루프 끝 off-by-one | 경계 sweep (tile±1), one-hot directed |
| 전부 ±1, 절반 위치 | requant 반올림 tie 규칙이 golden과 다름 | 수치 corner (tie 값) |
| 단독 실행은 OK, 카메라 켜면 가끔 틀림 | double buffering 동기화 — DMA가 늦을 때 compute가 먼저 시작 (race) | 동시성 행 + DDR 부하 90% |
| 고온 저전압에서만 랜덤 mismatch | SRAM/MAC 타이밍 margin, IR drop (power virus) | PVT shmoo + soak, ECC CE 카운터 추세 |
| 특정 command 순서에서 멈춤 | sequencer 의존성/credit deadlock | hang 복구 행 + 랜덤 stream fuzz |
| watchdog reset 후 다음 잡이 틀림 | soft reset이 일부 상태(포인터·credit)를 안 지움 | reset 후 첫 잡 bit-exact |
| 벤치마크가 실행마다 20% 차이 | DVFS/thermal throttling, DDR refresh, 다른 master | 클럭 고정 + 반복 + p50/p99 (deck §7 #9) |
| FPGA에선 compute bound인데 Si에선 memory bound | FPGA의 클럭 비율 왜곡 (DDR이 상대적으로 빠름) | 성능은 Si에서만 sign-off ([성능 메트릭 §7.1](2026-10-30_perf_metrics_ssd_vs_soc.html)) |

### 6. 10분 영어 답 뼈대

1. "Before I start: which data types and accumulator width, is there a bit-accurate C model, which operators run on the NPU, and is performance and power in my scope? I'll assume INT8 with int32 accumulation, a C model from the compiler team, and yes to performance."
2. "Here's the block: command processor reading a compiler-generated stream, DMA into banked SRAM, a MAC array, a vector unit for bias, requant and activation, an AXI master to DDR, interrupts, perf counters and ECC."
3. "I'd organize the plan in four layers. Layer one, numerical correctness: every operator against the golden model, bit-exact for INT8, with directed corners — minus 128, 127, zero-point extremes, rounding ties, maximum K."
4. "For FP16 or BF16, bit-exact isn't possible unless the summation order is specified, so the reference uses the same rounded inputs with exact accumulation, and the tolerance scales with square-root-K times the sum of magnitudes; I calibrate it on two known-good implementations and check that injected bugs fail it."
5. "Layer two, structure-aware: shape sweeps at tile minus one, tile, tile plus one, every SRAM bank, every layout and padding, with guard canaries around output buffers."
6. "Layer three, system behavior: interrupts, error descriptors, hang detection and recovery, block reset, concurrency with cameras on the same DDR, every DVFS point, hot and cold, and ECC injection."
7. "Layer four, real workloads: compiled networks bit-exact against the compiler's simulator, accuracy against FP32 as a separate metric, and performance."
8. "For performance I compute a roofline per layer first — for example 4096 MACs at 1 GHz is 8.2 TOPS, with 51 GB/s the ridge is 160 ops per byte — and report measured as a percentage of that roof, cross-checked against the MAC-busy counter."
9. "Platforms: functional layers run on the FPGA first with a smaller array; on silicon I focus on timing, power, PVT, concurrency and performance, because FPGA clock ratios distort performance."
10. "Priorities on A0: register sanity, one small GEMM, then via DDR, then the operator smoke set and one real network. Everything else is P1, and PVT and soak are P2."
11. "Pass criteria: zero mismatches for INT8, tolerance rule for floating point, zero missed interrupts in a million jobs, recovery from every injected hang, and performance within an agreed percentage of the model."
12. "Automation: seeded generators, CRC signatures for long runs, and every result tagged with chip ID, voltage, frequency, temperature and build."
13. "When something fails, the mismatch pattern tells me where to look — last columns means partial tile, one channel means bias or scale loading, plus-or-minus one means rounding."
14. "Biggest risks: concurrency races that only show under real DDR load, and margin at hot and low voltage — so those get early soak time."

### 7. 꼬리 질문

**Q1. "Why not just compare everything with a tolerance, even INT8?"**

<details><summary>답</summary>

"Because INT8 with integer requantization is fully deterministic — the spec defines every bit, including how ties round. A tolerance of one LSB would hide exactly the bugs that matter: a wrong rounding mode or a dropped bias term on a small value. I keep INT8 bit-exact and use tolerance only where the arithmetic is genuinely order-dependent, which is floating-point accumulation."

한국어 핵심: INT8은 비트까지 정의됨 → 허용오차는 rounding/bias 버그를 숨김. FP 누적만 허용오차.

</details>

**Q2. "All operator tests pass, but end-to-end accuracy dropped by half a percent. What do you do?"**

<details><summary>답</summary>

"First I separate hardware from software: run the same compiled network on the compiler's bit-accurate simulator. If the silicon output is bit-exact with the simulator, the hardware is fine and it's a quantization or compiler issue for the ML and compiler teams. If it's not, I bisect layer by layer — dump intermediate tensors and find the first layer that diverges, then extract that layer's exact shape, layout and parameters into a unit test. Usually it's a combination the operator tests didn't cover, like a particular padding with a partial tile, so I add that shape class to the sweep."

한국어 핵심: 시뮬레이터와 bit-exact 여부로 HW/SW 가르기 → 레이어 이분 탐색 → 첫 발산 레이어를 단위 테스트로 → sweep에 추가.

</details>

**Q3. "Our NPU utilization is only 40% on this model. Is that a hardware problem?"**

<details><summary>답</summary>

"Not necessarily. I'd compute per-layer arithmetic intensity and the roof for each layer — memory roof, compute roof, and also how well the shape maps onto the array, because depthwise convs or a three-channel stem leave most of the array idle. If a layer is far below its own roof, I look at counters: DDR bandwidth near peak means memory bound, so tiling and reuse are the levers; low DDR traffic with high stall cycles means a scheduling or dependency issue, possibly in hardware. I'd also sweep the NPU clock: if performance doesn't scale, the bottleneck is outside the MAC array."

한국어 핵심: 레이어별 roofline(+mapping 천장) → 카운터(DDR BW vs stall) → NPU 클럭 sweep으로 bound 확인. → [성능 메트릭 §10](2026-10-30_perf_metrics_ssd_vs_soc.html)

</details>

**Q4. "Results are wrong only at hot temperature on one board. Walk me through it."**

<details><summary>답</summary>

"Temperature dependence points to margin, not logic. I'd check whether it follows the chip or the board by swapping, then sweep voltage at hot: if raising voltage a few percent makes it disappear, it's a timing or IR-drop margin issue. I'd check whether the mismatches cluster in one SRAM bank or appear randomly, read ECC corrected-error counters, and run a power-virus pattern to see if droop correlates. Then it becomes a characterization item with design: a Vmin shmoo across corner parts, and either a voltage guardband or a fix in the next stepping."

한국어 핵심: 온도 의존 = margin → 칩/보드 swap → 고온 전압 sweep → bank 군집·ECC CE·droop 상관 → shmoo·guardband/stepping. ([BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html))

</details>

**Q5. "How do you test hang detection and recovery?"**

<details><summary>답</summary>

"I create hangs on purpose: a command stream with a dependency that never resolves, a DMA to an address the SMMU blocks, or a descriptor with an illegal opcode. For each I check that the job watchdog fires within its configured time, the error is reported with the job ID, the block reset brings every register back to its reset value, and — most important — the next job is bit-exact. I loop that a thousand times with other masters running, because recovery bugs usually leave a little state behind that breaks the job after."

한국어 핵심: 고의 hang 3종 → watchdog 시간·에러 보고·reset 값 복귀·**다음 잡 bit-exact** → 부하 중 1000회 반복.

</details>

### 8. Don 연결

- "At Solidigm I brought up Cortex-R cores and verified SoC blocks — DMA, SRAM, DRAM — on FPGA before RTL freeze, finding [숫자 채우기] RTL bugs; an NPU is another DMA-plus-SRAM-plus-compute subsystem, and the golden-model and mismatch-characterization approach is the same one I'd use."
- "I also did performance tuning with the silicon validation team on the SSD controller — measuring achieved versus theoretical throughput and explaining the gap — which maps directly to roofline-based NPU performance validation."

## F2 · 전원 관리 · DVFS · thermal

### 0. 🔰 이 블록이 뭔가

- **비유**: 건물의 **전기·냉난방 관리실**. 쓰지 않는 층은 전기를 끄고(power gating), 바쁜 층은 전력을 더 주고(DVFS: 전압·주파수 올리기), 너무 더우면 강제로 일을 줄인다(thermal throttling). 관리실이 실수하면 **층이 꺼진 채로 사람이 들어가거나(접근 hang)**, 전압이 모자란 채로 속도를 올려 **계산이 틀린다**.
- **정확한 정의**: SoC의 **PMU/전원 컨트롤러**(전원 도메인 on/off 시퀀스, isolation·retention), **clock & reset controller**(PLL, 분주, 클럭 게이팅), **DVFS**(OPP = 전압·주파수 쌍 테이블; 순서 규칙 "올릴 땐 전압 먼저, 내릴 땐 주파수 먼저"), 외부 **PMIC** 인터페이스(I2C/SPMI 또는 AVS), **thermal sensor(TSENS)** 와 throttling 하드웨어/펌웨어, 저전력 상태(suspend/resume).

```text
   OS/firmware governor ──► DVFS request ──► PMU / power controller ──► PMIC (I2C/SPMI)  ──► rails: VDD_CPU, VDD_NPU, VDD_SOC, VDDQ
                                                │                                              │
                                                ├──► clock ctrl: PLL relock / divider switch    │
                                                ├──► power domains: iso → off → on → reset → un-iso, retention
                                                ▼
   thermal sensors (TSENS, per cluster/NPU) ──► thresholds ──► throttle (cap OPP) / IRQ / emergency shutdown
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| power domain | 전원 도메인 | 따로 끌 수 있는 회로 묶음 | 꺼진 도메인 접근 = bus hang |
| isolation · retention | 격리 · 보존 | 끄기 전 출력 고정, 일부 상태 저장 | 순서 틀리면 깨어난 뒤 쓰레기 값 |
| DVFS · OPP | Dynamic Voltage & Frequency Scaling · Operating Performance Point | 바쁠 때 V·F 올리고 한가할 때 내림 | 전환 중 데이터 오류·hang |
| PLL relock | 위상 고정 재획득 | 주파수 바꿀 때 PLL이 다시 안정 | 전환 시간·glitch |
| AVS | Adaptive Voltage Scaling | 칩마다 필요한 최소 전압으로 | 칩 코너마다 다른 Vmin |
| TSENS | thermal sensor | 다이 위 온도계 | 보정 오차가 곧 throttle 오차 |
| throttling | 열 제한 | 너무 뜨거우면 OPP 상한 낮춤 | 성능 변동의 주범 |
| Tj · Ta | junction · ambient | 다이 온도 · 주변 온도 | 자동차 등급은 Ta 기준, 다이는 더 뜨거움 |

### 1. 먼저 물어볼 것 (clarify)

- "Which domains and OPPs exist, and who owns the DVFS sequence — hardware state machine, a power-management microcontroller, or OS drivers?" → 가정: 전원 관리 MCU 펌웨어 + OS governor.
- "Is the PMIC on I2C/SPMI or AVS, and what's its step size and slew rate?" → 가정: SPMI/I2C, 수 mV step [추정].
- "What are the thermal thresholds and the policy — throttle levels, emergency shutdown temperature?" → 가정: 2단계 throttle + shutdown.
- "Is retention required for any domain across suspend, and what's the resume latency budget?" → 가정: NPU 비보존, CPU/SRAM 일부 보존.
- "Are temperature sensors calibrated per part at ATE, or do we calibrate?" → 가정: ATE trim, 검증은 정확도 확인.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | PMU/clock/thermal 레지스터 reset 값·RW | 전 레지스터 | 둘 다 | 스펙 일치 | P0 |
| 기본 기능 | 각 도메인 on → 접근 → off → on, reset 값 복귀 | 도메인 × 1000 cycle | 둘 다 (FPGA는 시퀀스 논리만) | 매 cycle 레지스터·기능 smoke pass | P0 |
| 시퀀스 | isolation/retention 순서, 꺼진 도메인 접근 시 동작 | 정상 + 고의 위반 | 둘 다 | 위반 시 정의된 bus error (hang 아님) | P1 |
| DVFS 기능 | 모든 OPP 쌍 전환 (up/down), 각 OPP에서 블록 golden | OPP n × n 전환 | Si | 전환 성공 100%, golden pass | P0 |
| DVFS 중 트래픽 | 전환하는 동안 NPU 잡·DMA·DDR 트래픽 계속 | 10^4 전환 under load | Si | 데이터 오류 0, hang 0, 타임아웃 0 | P1 |
| 전환 시간 | 요청 → 새 OPP 안정 (PLL relock + 전압 slew) | GPIO 마커 + scope on rail | Si | ≤ 스펙 [추정: 수십–수백 µs] | P1 |
| 전압 순서 | up: V 먼저, down: F 먼저 — 레일 파형 확인 | scope 2채널 (rail, 클럭 모니터 출력) | Si | 순서 위반 0 | P1 |
| AVS/Vmin | OPP별 Vmin 측정 vs 테이블 전압 | 코너 칩 SS/TT/FF × T | Si | 테이블 전압 ≥ Vmin + guardband | P2 |
| thermal 정확도 | TSENS 읽기 vs 기준 (thermostream + 외부 다이오드/열전대) | −40 / 25 / 85 / 125 °C [추정] | Si | 오차 ≤ ±X °C [추정 스펙] | P1 |
| throttling | 임계 넘김 → OPP 상한 적용, 내려오면 복귀, hysteresis | 가열 + 임계 일부러 낮춤 | Si | 반응 시간 ≤ 예산, 진동(oscillation) 없음 | P1 |
| emergency | shutdown 임계 → 하드웨어 reset/shutdown | 임계 낮춰서 안전하게 | Si | 정의대로 shutdown, reset reason 기록 | P1 |
| 인터럽트 | thermal IRQ, PMIC fault IRQ(UV/OV) | 주입 | Si | IRQ 1회, 상태 레지스터 정확 | P1 |
| suspend/resume | 시스템 suspend → resume, 보존 상태 비교 | 5000 cycle | Si | 실패 0, resume latency p99 ≤ 예산 | P1 |
| 전력 | 각 상태(idle, gating, 최대 부하) 레일별 전력 | DAQ | Si | 전력 예산 이내, leakage vs T 곡선 | P1 |
| 동시성 | 여러 도메인 동시 전환 + 인터럽트 폭주 | random | Si | hang 0 | P2 |
| soak | 랜덤 OPP·gating·온도 사이클 24 h | chamber | Si | 오류 0 | P2 |

### 3. 우선순위 · A0 첫 주

- **P0**: 도메인 on/off 기본 시퀀스(특히 NPU·카메라 도메인 — 다른 블록 bring-up의 전제), 부트 OPP에서 안정, OPP 테이블 전환 1회씩, thermal sensor 읽기가 상식적인 값.
- **P1**: DVFS 중 트래픽, 전환 시간, throttling·emergency, suspend/resume 수천 회, 전력 측정.
- **P2**: AVS·Vmin 특성화(코너 칩), 동시 다발 전환 stress, 온도 사이클 soak.
- **안 하는 것**: 전원 시퀀스 FSM의 모든 상태 전이 조합(DV/formal), PMIC 자체 레귤레이터 특성(보드/PMIC 벤더), 열 설계(기구팀).

### 4. 장비 · 셋업 · 관찰 지점

- 프로그래머블 PSU/PMIC margining, **scope 4채널**(rail 2개 + 클럭 모니터/분주 출력 + GPIO 마커), 전류 shunt + DAQ, **thermostream/chamber**, 기준 온도계(열전대, 다이 근처). 보드 쪽은 [BV03](2026-11-01_BV03_power_tree_sequencing_pmic.html)·[BV11](2026-11-01_BV11_lab_instruments_measurement.html).
- 읽을 것: PMU 상태 레지스터, PLL lock 상태, 현재 OPP, TSENS 값, throttle 이벤트 카운터, PMIC fault 레지스터, reset reason.
- Linux 단계: `/sys/class/thermal/`, `cpufreq`/`devfreq` sysfs, `powercap`(있으면) [추정: 플랫폼 지원에 따름].

### 5. 잘 터지는 버그

| 증상 | 그럴듯한 원인 | 잡는 곳 |
|---|---|---|
| DVFS up 직후 드물게 계산 오류 | 전압이 목표에 도달하기 전에 주파수 상승 (slew 대기 부족) | 전압 순서 scope + 전환 중 트래픽 |
| 도메인 재기동 후 첫 접근 hang | reset 해제 전에 isolation 해제 / 클럭 미공급 | 시퀀스 행 |
| resume 후 가끔 블록 설정 틀림 | retention 대상 레지스터 누락 | suspend/resume 5000회 + 상태 비교 |
| throttle이 켜졌다 꺼졌다 반복 | hysteresis 없음/센서 노이즈 | throttling 행 |
| 칩마다 다른 온도 값 | TSENS trim 미적용 | thermal 정확도 (여러 칩) |
| 저온에서만 Vmin 악화 | temperature inversion (저전압에서 저온이 느린 코너) [deck §5.1 Q4] | AVS/Vmin × T |

### 6. 10분 영어 답 뼈대

1. "Clarify: which domains and OPPs, who owns the sequence — PMU firmware or OS — PMIC interface, thermal policy."
2. "Picture: governor requests an OPP, the power controller sequences the PMIC and PLL, domains go through isolation, power, reset, un-isolation, and thermal sensors cap the OPP."
3. "Functional: every domain on/off a thousand times with a smoke test after each, and illegal access to an off domain must return a bus error, not hang."
4. "DVFS: every OPP transition up and down, golden tests at each OPP, then transitions under live NPU and DMA traffic — zero data errors."
5. "Ordering and timing on the scope: voltage before frequency going up, frequency before voltage going down, and transition latency."
6. "Thermal: sensor accuracy against a reference at several temperatures, throttling with hysteresis, emergency shutdown with reset reason."
7. "Suspend/resume thousands of cycles with a state compare, and power per state."
8. "Characterization: Vmin per OPP on corner parts across temperature — remember cold can be the slow corner."
9. "Priority: domain sequencing first because every other block depends on it."
10. "Risks: races between DVFS and traffic, retention gaps — both need long randomized runs."

### 7. 꼬리 질문

**Q1. "How do you prove DVFS transitions are safe under load?"**

<details><summary>답</summary>

"I run self-checking traffic — NPU jobs with CRC-checked outputs and DMA copies with pattern checks — while a second agent requests random OPP transitions, tens of thousands of times, and I capture rail voltage and a clock monitor on the scope with a GPIO marker at each request. Zero data errors and zero timeouts is the pass, and the scope proves the ordering. Then I repeat at hot and cold, and on slow-corner parts."

한국어 핵심: 자가검증 트래픽 + 랜덤 OPP 전환 수만 번 + scope로 순서 증명 → 온도·코너 반복.

</details>

**Q2. "How do you validate the thermal sensors?"**

<details><summary>답</summary>

"With the chip idle in a thermal forcer at a stable set point, so die temperature is close to the forcer temperature, I compare every sensor against a reference at several points across the range, on several parts, to check calibration and offset. Then under load I check that sensors near the hot spots read higher in a consistent way, and that the throttle and shutdown thresholds trigger at the right reading."

한국어 핵심: idle + 열 강제 장치로 기준 대비 정확도(여러 점·여러 칩) → 부하 시 핫스팟 일관성 → 임계 동작.

</details>

**Q3. "Why does a benchmark vary 20% run to run, and what do you do?"**

<details><summary>답</summary>

"Most often DVFS or thermal throttling — the clock isn't what you think. I log the OPP and temperature with every measurement, pin the clocks for performance tests, warm up, repeat, and report p50 and p99. Separately I measure the throttled behavior on purpose, because the customer will see it."

한국어 핵심: OPP·온도 기록, 클럭 고정, 반복·percentile, throttle 동작은 별도로 측정.

</details>

### 8. Don 연결

- "At SK hynix I automated chamber-based reliability testing across temperature [숫자 채우기: 온도 범위·챔버 수] and built shmoo sweeps — the same infrastructure I'd use for Vmin per OPP and thermal-sensor accuracy."

## F3 · Secure boot · crypto engine

### 0. 🔰 이 블록이 뭔가

- **비유**: 건물 출입 **신분증 검사 사슬**. 정문 경비(Boot ROM, 바꿀 수 없음)가 1층 관리인(1단계 부트로더)의 신분증(서명)을 본사 도장(OTP에 새긴 공개키 해시)과 대조하고, 통과한 관리인이 다시 2층 관리인을 검사한다. 한 명이라도 가짜면 **문을 안 연다**. 열쇠(키)를 잃어버리면 그 열쇠를 **폐기(revoke)** 하고, 옛날 신분증 재사용은 **버전 카운터(anti-rollback)** 로 막는다.
- **정확한 정의**: **Secure boot** = Boot ROM이 OTP/eFuse에 있는 공개키 해시로 첫 이미지 서명(ECDSA/RSA + SHA-2)을 검증하고, 각 단계가 다음 단계를 검증하는 **chain of trust**. 관련: 키 폐기 비트, anti-rollback(monotonic counter in fuses), 디버그 포트 잠금/인증 디버그, 보안 상태(개발/양산) 전환. **Crypto engine** = AES/SHA/ECC(공개키)/TRNG 하드웨어 가속기, 보통 DMA 모드와 키 슬롯(소프트웨어가 키를 못 읽음).

```text
 OTP / eFuse: ROTPK hash · revoke bits · anti-rollback counter · lifecycle (dev/prod) · debug-lock
      │
 Boot ROM ──verify sig──► BL1 (SRAM) ──verify──► BL2 / TF-A ──verify──► OS / NPU firmware
      │                        │
      └── uses crypto engine (SHA-2, ECDSA/RSA verify) ◄── key slots (not CPU-readable), TRNG
 fail path: stop + error code on UART/register, or fall back to recovery image
 JTAG/SWD: locked in prod lifecycle, or authenticated debug
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| chain of trust | 신뢰 사슬 | 단계마다 다음 단계를 검증 | 한 고리라도 빠지면 무의미 |
| ROTPK | Root of Trust Public Key | OTP에 해시로 새긴 루트 키 | 바꿀 수 없음 → 검증용 칩은 신중히 |
| OTP / eFuse | One-Time Programmable | 한 번 쓰면 못 지움 | **테스트 실수 = 칩 벽돌** |
| anti-rollback | 롤백 방지 | 버전 카운터보다 낮은 이미지는 거부 | 옛 취약 이미지 재사용 차단 |
| KAT | Known-Answer Test | 정답이 공개된 입력으로 암호 연산 검증 | 엔진 정확성 증명 (NIST CAVP 벡터 [9]) |
| TRNG | True Random Number Generator | 물리 잡음 기반 난수 | 건강 검사·통계 시험 |
| lifecycle | 생명주기 상태 | 개발 → 양산 → RMA 등 | 상태마다 디버그·키 정책이 다름 |

### 1. 먼저 물어볼 것 (clarify)

- "Which algorithms and key sizes does the ROM verify, and is there a recovery path on failure?" → 가정: ECDSA P-256/384 + SHA-256/384, 실패 시 정지 + 에러 코드 [추정].
- "Can we emulate fuses — shadow registers on FPGA or a fuse-override test mode on early silicon — so we don't burn real OTP?" → 가정: FPGA 섀도, Si는 희생 샘플만 실제 burn.
- "What's the lifecycle model and debug policy in each state?" → 가정: dev(열림)/prod(잠금 + 인증 디버그).
- "Is side-channel/fault-attack resistance in my scope, or a separate security lab?" → 가정: 별도 보안 랩, 나는 기능·성능.
- "Which crypto engine modes and throughput targets (e.g., for image hash at boot, secure storage)?" → 가정: 부팅 시간 예산 기준.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | crypto/OTP 컨트롤러 레지스터, 키 슬롯 **읽기 불가** 확인 | 전 레지스터 | 둘 다 | 스펙 일치, 키 슬롯 read = 0 또는 에러 | P0 |
| 기본 기능 | 정상 서명 이미지 부팅 (각 단계) | 섀도 퓨즈 | 둘 다 | 100% 부팅 | P0 |
| negative — 서명 | 서명 1비트 변조, 이미지 1비트 변조, 다른 키로 서명, 잘린 이미지, 헤더 길이 거짓 | 이미지 변형 세트 | 둘 다 | **전부 거부** + 정의된 에러 코드, 코드 실행 0 | P0 |
| negative — 파서 | 헤더 필드 극단값(길이 0/최대/오버플로), 오프셋 범위 밖 | fuzz 수천 개 | FPGA 주 | crash/hang 없이 거부 | P1 |
| 키 폐기 | revoke 비트 설정 후 그 키로 서명된 이미지 | 섀도 → 희생 샘플 | 둘 다 | 거부, 다음 키로 부팅 | P1 |
| anti-rollback | 카운터보다 낮은 버전 이미지, 같은 버전, 높은 버전 + 카운터 증가 | 섀도 → 희생 샘플 | 둘 다 | 낮은 버전 거부, 카운터 단조 증가 | P1 |
| 디버그 잠금 | prod 상태에서 JTAG 연결 시도, 인증 디버그 성공/실패 | lifecycle별 | Si | 잠금 상태에서 접근 불가, 인증 시만 열림 | P0 |
| lifecycle 전환 | dev → prod 전환, 되돌리기 불가 확인 | 희생 샘플 | Si | 비가역, 정책 일치 | P1 |
| crypto KAT | AES(ECB/CBC/CTR/GCM), SHA-2, ECDSA verify — NIST CAVP 벡터 | 전 모드·키 길이 | 둘 다 | 전 벡터 일치 | P0 |
| 경계 | 메시지 길이 0, 1, 블록−1, 블록, 블록+1, 최대; 비정렬 버퍼 | sweep | 둘 다 | 소프트웨어 레퍼런스(OpenSSL 등)와 일치 | P1 |
| 에러 | 잘못된 키 슬롯, DMA 주소 fault, 동작 중 abort | 주입 | 둘 다 | 에러 보고, 키 유출 없음, 다음 연산 정상 | P1 |
| 인터럽트 | 완료/에러 IRQ | 10^5 연산 | 둘 다 | 누락 0 | P1 |
| 성능 | AES/SHA throughput vs 메시지 크기, 부팅 시 해시+검증 시간 | 클럭 고정 | Si | 부팅 시간 예산 이내 [추정] | P1 |
| TRNG | 건강 검사(repetition/adaptive proportion), 출력 통계 | 장시간 수집 | Si | 건강 검사 pass, 통계 시험 pass [추정: SP 800-90B류] | P1 |
| 동시성 | 여러 마스터가 crypto 엔진 공유, 보안/비보안 세계 경합 | random | Si | 결과 일치, 권한 위반 0 | P2 |
| PVT | 부팅 + KAT를 V/T 코너에서 | V × T | Si | 전 코너 pass | P2 |
| 시스템 | 실제 서명 파이프라인 산출 이미지로 전체 부팅 체인 + OTA 업데이트 시나리오 | 릴리스 툴 | Si | 정상 부팅, 롤백 정책대로 | P1 |

### 3. 우선순위 · A0 첫 주

- **P0**: 섀도/override 퓨즈로 정상 서명 부팅, 변조 이미지 거부 3종, crypto KAT 기본(SHA-256, ECDSA verify — ROM이 쓰는 것), 디버그 잠금이 dev 상태에선 **열려 있는지**(안 그러면 bring-up 자체가 막힘).
- **P1**: 폐기·anti-rollback·lifecycle (희생 샘플), 전체 KAT, 성능, TRNG, 파서 fuzz.
- **P2**: 동시성, PVT.
- **안 하는 것**: 암호 알고리즘 자체의 보안성, side-channel/glitch 공격 저항(전문 보안 랩·외부 평가), ROM 코드 리뷰(보안팀). 단, **"OTP를 태우는 테스트는 희생 샘플과 승인 절차로"** 라는 운영 규칙은 내 책임.

### 4. 장비 · 셋업 · 관찰 지점

- 서명 툴 + 테스트 키(양산 키 절대 아님), **이미지 변형 생성 스크립트**(비트 플립·잘림·헤더 변조), UART 부트 로그·ROM 에러 코드 레지스터, JTAG(잠금 확인용), 희생 샘플 추적표(어느 칩에 어떤 퓨즈를 태웠나).
- KAT: NIST CAVP 응답 파일 형식 벡터를 파싱해 타깃에서 실행 [9]. 부팅 시간: 리셋 해제 → 각 단계 진입 GPIO 마커 → scope.

### 5. 잘 터지는 버그

| 증상 | 그럴듯한 원인 | 잡는 곳 |
|---|---|---|
| 변조된 이미지가 부팅됨 | 검증 결과를 확인하지 않는 분기, 길이 필드를 서명 범위 밖에 둠 | negative 서명·파서 |
| 특정 길이에서만 해시 틀림 | 패딩/마지막 블록 처리 | 경계 sweep |
| prod 칩에서 JTAG가 열림 | lifecycle 디코딩 / 퓨즈 읽기 타이밍 | 디버그 잠금 |
| 저온에서만 부팅 실패 | OTP 읽기 마진, ROM 타이밍 | PVT 부팅 |
| 키 슬롯 내용이 다른 마스터에 보임 | 보안 속성(TrustZone/버스 보안 비트) 전파 누락 | 레지스터·동시성 |
| 재부팅 수천 회 중 1회 검증 실패 | TRNG/엔진 초기화 race | 부팅 N천 회 (F5) |

### 6. 10분 영어 답 뼈대

1. "Clarify: algorithms and key sizes, recovery behavior, lifecycle states, whether fuses can be emulated, and whether side-channel is in scope."
2. "Picture: ROM verifies the first stage against a key hash in fuses, each stage verifies the next, crypto engine with key slots, debug lock tied to lifecycle."
3. "Positive path: correctly signed images boot through every stage."
4. "Negative path is the heart of it: flip a bit in the signature, in the image, sign with the wrong key, truncate, lie in the header length — every one must be rejected with a defined error and no code executed."
5. "Fuzz the header parser on FPGA, because ROM bugs can't be patched."
6. "Revocation, anti-rollback and lifecycle on emulated fuses first, then on a small set of sacrificial parts with a tracking sheet — burning OTP is irreversible."
7. "Debug lock: closed in production, open only with authentication."
8. "Crypto engine: NIST known-answer vectors for every mode, length sweeps, error injection, throughput against the boot-time budget, TRNG health tests."
9. "PVT boots and thousands of reboots to catch marginal fuse reads or init races."
10. "Out of scope for me: side-channel and glitch resistance — that's a security lab — but I'd make sure their findings feed back as regression tests."

### 7. 꼬리 질문

**Q1. "How do you test anti-rollback without destroying your chips?"**

<details><summary>답</summary>

"Develop and debug the logic with fuse emulation — shadow registers on the FPGA or a fuse-override test mode on early silicon — so the counter can be reset. Only the final confirmation runs on real fuses, on a small number of sacrificial parts, with each burn recorded so nobody reuses those chips for something else."

한국어 핵심: 섀도/override로 개발·디버그 → 실제 퓨즈는 희생 샘플 소수 + 기록.

</details>

**Q2. "Why is the negative path more important than the positive path for secure boot?"**

<details><summary>답</summary>

"Because the positive path gets tested every day by everyone who boots the chip, while a bug in the negative path is invisible until an attacker uses it — and in ROM it can't be fixed without a new stepping. So I generate a systematic set of corrupted images and malformed headers and require every one to be rejected."

한국어 핵심: 정상 경로는 매일 검증됨, 거부 경로 버그는 공격 전엔 안 보이고 ROM이라 못 고침.

</details>

**Q3. "How do you know the crypto engine is correct?"**

<details><summary>답</summary>

"Known-answer tests from published vectors such as NIST's for each algorithm and mode, plus length and alignment sweeps compared with a software library, plus error cases — and I confirm that key slots can't be read back by software."

한국어 핵심: NIST KAT + 길이/정렬 sweep vs SW 라이브러리 + 에러 + 키 슬롯 읽기 불가.

</details>

### 8. Don 연결

- "SSD controllers also have a secure boot chain and hardware crypto, so I'm familiar with signed firmware images and rejection paths from the firmware side [숫자 채우기: 관여한 범위가 있으면]; on this team I'd own the validation plan for it."

## F4 · Functional safety (ISO 26262) 안전 메커니즘 검증 — fault injection

### 0. 🔰 이 블록이 뭔가

- **비유**: 차의 **화재경보기 점검**. 경보기가 "있다"는 건 의미가 없다. **연기를 일부러 피워서(fault injection)** 실제로 울리는지(감지), 관리실에 신호가 가는지(보고), 스프링클러가 정해진 시간 안에 도는지(반응) 확인해야 한다. 그리고 요리할 때마다 울리면(오탐) 그것도 고장이다.
- **ISO 26262 쉬운 말로** (공개 요약 기준 [10][11], 깊이는 [추정]): 자동차 전기·전자 시스템의 **기능 안전** 표준. "고장이 나도 사람을 다치게 하지 않도록 설계·검증하라".
  - **ASIL A–D**: 위험도 등급. 심각도(S) × 노출 빈도(E) × 운전자가 제어할 수 있는가(C)로 정해지고 D가 가장 엄격. 안전과 무관하면 QM.
  - **안전 메커니즘**: 고장을 감지·제어하는 장치 — ECC, lockstep CPU, BIST, watchdog, CRC, 전압·클럭·온도 모니터.
  - **Diagnostic coverage (DC)**: 그 메커니즘이 **고장의 몇 %를 잡나**. fault injection 결과가 증거.
  - **SPFM / LFM**: 칩 전체에서 단일 고장·잠재 고장을 얼마나 덮었나 (%). ASIL D는 SPFM ≥ 99 %, LFM ≥ 90 % — 표는 [BV15 §2.4](2026-11-01_BV15_automotive_board_considerations.html) (여기선 반복 안 함).
  - **FTTI**: 고장이 생긴 뒤 **위험이 되기까지 허용되는 시간**. 감지 시간 + 반응 시간 ≤ FTTI [10].
  - **잠재 고장(latent)**: 안전 메커니즘 자체가 고장난 것 — 평소엔 안 보임 → 시동/주기 self-test로 잡음.
- **이 페이지의 추가분**: BV15는 **보드·SoC 일반 메커니즘**. 여기선 ① **NPU/AI SoC 특유의 메커니즘** ② **fault injection 캠페인을 테스트 플랜으로** ③ **DC를 숫자로 계산하는 연습(코드)**.

```text
  fault (bit flip, stuck-at, clock loss, V droop, SW hang)
     │ injected via: ECC inject reg · test mode · V/clock/temp manipulation · SW corruption
     ▼
  safety mechanism ──detect──► report (status reg, IRQ, error pin to safety MCU / R52 island)
     │                               │
     │  detection time               ▼ reaction time
     │                         safe state (drop frame, re-run job, degrade, alert driver, reset)
     └──────────────── detection + reaction ≤ FTTI ──────────────────────┘
  RAP1: Cortex-R52 cores reported [4] → likely a safety island that collects errors [추정]
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| ASIL | Automotive Safety Integrity Level | 위험도 등급 A–D | 요구 커버리지·절차 결정 |
| DC | Diagnostic Coverage | 메커니즘이 잡는 고장 비율 | fault injection이 증거 |
| SPFM · LFM | Single-Point / Latent Fault Metric | 칩 전체 커버리지 % | ASIL 목표치 |
| FMEDA | Failure Modes, Effects & Diagnostic Analysis | 블록별 고장률 × DC 표 | SPFM/LFM 계산 근거 |
| FTTI | Fault Tolerant Time Interval | 위험까지 허용 시간 | 감지+반응 시간 측정 필요 |
| safe state | 안전 상태 | 위험 없는 동작 상태 | 반응의 목적지 |
| SDC | Silent Data Corruption | 아무도 모르게 틀린 데이터 | 가장 위험한 결과 |
| safety island | 안전 섬 | 오류를 모아 판단하는 독립 코어(lockstep R52 류) | 반응 경로의 중심 |

### 1. 먼저 물어볼 것 (clarify)

- "Which safety goals and ASIL apply to the perception path, and which safety mechanisms are claimed in the FMEDA for the NPU and memories?" → 가정: 안전팀이 메커니즘 목록·목표 DC 제공 [추정].
- "What's the FTTI budget for the perception path, and what's the defined safe state — drop the frame, re-run, degrade?" → 가정: 프레임 단위, 수십–수백 ms [추정].
- "Which injection hooks exist in silicon — ECC inject registers, lockstep self-test, BIST fault modes?" → 가정: ECC inject, BIST 테스트 모드 있음.
- "Who does pre-silicon fault simulation — the safety team with RTL fault-injection tools — and how do my silicon results connect to their numbers?" → 가정: 안전팀 RTL 시뮬 + 내 실리콘 증거로 보강.
- "Where are errors aggregated — a safety island, an external safety MCU, an error pin?" → 가정: 칩 내부 safety island + 외부 핀.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | 에러 상태·마스크·inject·카운터 레지스터 | 전 레지스터 | 둘 다 | 스펙 일치, inject 비트 self-clear 동작 | P0 |
| ECC 1-bit | NPU SRAM·L2·DDR에 1-bit 주입 (모든 bank, 데이터·체크 비트 위치 포함) | 위치 sweep | 둘 다 | 정정 100%, CE 카운터 +1, 주소 로그 정확 | P0 |
| ECC 2-bit | 2-bit 주입 | 위치 random | 둘 다 | UE 감지 100%, 잡 에러 보고 + 안전 반응 | P0 |
| ECC 3+ bit | 다중 비트 (SEC-DED 한계 밖) | random | 둘 다 | **DC 측정·문서화** (100% 아님이 정상, 아래 코드) | P2 |
| 주소/제어 보호 | 주소 parity, 레지스터 parity, 버스 E2E CRC 주입 | inject 모드 | Si | 감지 + 보고 | P1 |
| NPU 출력 무결성 | 출력 CRC/E2E, 이중 실행 비교(같은 잡 2회 또는 2코어) — 채택된 경우 | 고의 데이터 변조 | Si | 불일치 감지 100% [추정: 메커니즘 존재 시] | P1 |
| job watchdog | 고의 hang (F1) | 1000회 | Si | 감지 ≤ 설정 시간, safe state 진입 | P0 |
| 시스템 watchdog | kick 중단, 윈도우 위반(너무 이른 kick) | — | Si | reset, reason=WDT | P1 |
| lockstep | 비교기 self-test/inject 모드 (R52 등) | 벤더 모드 | Si | 불일치 감지 → safety island 보고 | P1 |
| BIST (잠재 고장) | 시동 MBIST/LBIST 정상 + 주입 결함 모드, 주기 self-test | 부팅 1000회 | Si | 정상 PASS, 결함 시 부팅 차단/보고, 시간 ≤ 부팅 예산 | P1 |
| 모니터 | 전압·클럭·온도 모니터 임계 (PMIC margin, PLL 이탈, 열) | 실제 물리 조작 | Si | 임계 정확도, 감지 지연 | P1 |
| **반응 시간 (FTTI)** | 주입 순간 GPIO 마커 → 에러 핀/safe-state 신호 | scope, 부하·온도별 | Si | 감지+반응 최악값 ≤ FTTI 예산 [추정] | P0 |
| 오탐 (false positive) | 주입 없이 PVT 전 범위 + 최대 부하 + 전원 이벤트 | 72 h | Si | 안전 이벤트 **0** | P1 |
| 보고 경로 | 모든 에러 소스가 safety island/에러 핀까지 도달 (마스크 기본값 포함) | 소스 × 1 | Si | 경로 100% 연결 | P0 |
| 회복 | 정정 가능 에러 후 계속 동작, 정정 불가 후 블록 reset → 정상 | 반복 | Si | 다음 잡 bit-exact | P1 |

### 3. 우선순위 · A0 첫 주

- **P0**: 에러 레지스터·보고 경로가 연결돼 있는지(소스마다 1번 주입 → safety island에 보이나), ECC 1/2-bit 기본, job watchdog. 이유: **보고 경로가 끊겨 있으면 나머지 메커니즘 검증이 전부 무의미**.
- **P1**: 반응 시간 측정, BIST, lockstep, 모니터, 오탐 장시간.
- **P2**: 다중 비트 DC 특성화, 코너 칩 반복.
- **안 하는 것**: RTL 수준 대규모 fault simulation(안전팀·DV 도구 영역), FMEDA 계산 자체(안전 엔지니어). 나는 **실리콘에서 "메커니즘이 실제로 감지·보고·반응한다"는 증거와 시간 측정**을 낸다.

### 4. 장비 · 셋업 · 관찰 지점

- scope(주입 GPIO 마커 ↔ 에러 핀 ↔ reset/safe-state 신호), PMIC margining·PSU, 클럭 소스 조작(테스트 모드), thermostream. 추적표: **메커니즘 ↔ 안전 요구사항 ID ↔ 테스트 ID ↔ 주입 방법 ↔ 기대 감지/보고/반응 ↔ 측정 시간** ([BV15 §3.2](2026-11-01_BV15_automotive_board_considerations.html) 체크리스트 형식).
- **DC 계산 연습 — 실제 실행** (`ecc_fault_inject.py`: SEC-DED (72,64) 모델에 1/2/3-bit 무작위 주입 각 20,000회, 결과를 정정 · 감지 · SDC로 분류):

```text
exhaustive 1-bit sweep on one word: 72/72 positions corrected

== reference decoder : 20000 random injections per fault model ==
  fault  corrected  detected     SDC  no_eff       DC
  1-bit      20000         0       0       0  100.00%
  2-bit          0     20000       0       0  100.00%
  3-bit          0      4882   15118       0   24.41%

== BUG: syndrome bit2 stuck-at-0 : 20000 random injections per fault model ==
  fault  corrected  detected     SDC  no_eff       DC
  1-bit      10251         0    9749       0   51.25%
  2-bit          0     19719     275       6   98.59%
  3-bit         15      4797   15188       0   24.06%

== BUG: dead checker (always OK) : 20000 random injections per fault model ==
  fault  corrected  detected     SDC  no_eff       DC
  1-bit          0         0   17787    2213    0.00%
  2-bit          0         0   19772     228    0.00%
  3-bit          0         0   19987      13    0.00%
```

- **읽는 법**: ① 정상 SEC-DED는 1-bit 100% 정정, 2-bit 100% 감지, **3-bit는 76%가 잘못 정정(SDC)** → "ECC가 있다 = 100% 안전"이 아니다. DC는 **fault model별로** 말해야 한다. ② 신드롬 비트 하나가 stuck인 decoder는 1-bit **51%** 만 정정 — 주입 캠페인이 없으면 "ECC 있음"으로 통과했을 버그. ③ 죽은 checker는 DC 0% — 그런데 체크 비트에만 맞은 주입은 데이터가 멀쩡해 보임(no_eff) → **주입 결과를 "데이터가 맞나"로만 판정하면 안 되고 "보고됐나"를 봐야** 한다. ④ ISO 26262 SPFM/LFM은 이런 메커니즘별 DC를 보호 대상 회로의 고장률(FIT)로 가중 합산한 FMEDA 결과 [추정: 공개 요약 수준].

### 5. 잘 터지는 버그

| 증상 | 그럴듯한 원인 | 잡는 곳 |
|---|---|---|
| ECC 정정은 되는데 안전 반응이 없음 | 에러 마스크 기본값이 masked, 보고 라우팅 누락 | 보고 경로 행 |
| 1-bit 일부 위치만 정정 실패 | 신드롬 디코딩/체크 비트 위치 버그 (코드 예) | ECC 1-bit 위치 sweep |
| 반응 시간이 부하 중에만 예산 초과 | 에러 IRQ 우선순위·ISR 지연, 안전 SW 스케줄링 | FTTI 측정 under load |
| 정상 주행 조건에서 전압 모니터 트립 | 임계값과 실제 droop 사이 여유 부족 | 오탐 행 |
| BIST가 통과로 나오는데 실제론 안 돌았음 | 결과 레지스터 기본값 = PASS, 실행 여부 미확인 | BIST 결함 주입 모드 (반드시 FAIL 나와야) |
| 정정 불가 에러 후 블록이 회복 안 됨 | reset 범위 부족 | 회복 행 |

### 6. 10분 영어 답 뼈대

1. "Clarify: safety goals and ASIL for the perception path, the FTTI budget and safe state, which mechanisms are claimed in the FMEDA, and which injection hooks exist."
2. "Principle: a safety mechanism is only proven when I inject a fault and see detection, reporting and reaction inside the time budget — and see no false alarms in normal operation."
3. "First, the reporting path: inject once per error source and confirm it reaches the safety island and the error pin. If that's broken, nothing else counts."
4. "Memories: ECC single-bit at every bank and bit position, corrected and counted with the address; double-bit detected with the safe reaction; multi-bit characterized, not assumed."
5. "Compute: job watchdog with deliberate hangs, output CRC or redundant execution if the architecture uses them, lockstep self-test on the safety cores."
6. "Latent faults: startup and periodic BIST, including the fault-injection mode where BIST must fail."
7. "Monitors: voltage, clock and temperature, using real physical manipulation where possible."
8. "Timing: a GPIO marker at injection and the safety output on the scope, worst case across load and temperature, compared with FTTI."
9. "False positives: 72 hours across PVT and power events with zero safety events."
10. "Deliverable: a traceability table from mechanism to requirement to test to measured result, which feeds the safety team's FMEDA."

### 7. 꼬리 질문

**Q1. "What is diagnostic coverage and how does validation contribute to it?"**

<details><summary>답</summary>

"Diagnostic coverage is the fraction of faults of a given type that a mechanism detects or controls. The safety team estimates it in the FMEDA, often with RTL fault simulation; on silicon I provide evidence — injection campaigns per fault model, counting corrected, detected and silent outcomes. For example, a SEC-DED code covers all single and double bit errors but miscorrects most triple-bit errors, so the coverage claim has to name the fault model."

한국어 핵심: DC = fault model별 감지 비율. FMEDA 추정을 실리콘 주입 캠페인으로 뒷받침. SEC-DED 3-bit 예.

</details>

**Q2. "How do you measure that a reaction meets the FTTI?"**

<details><summary>답</summary>

"I mark the injection instant with a GPIO from the same code that triggers the injection, and probe the safety output or reset on another scope channel. I collect the distribution across many injections, under full load and at temperature extremes, and compare the worst case — not the average — with the budget, leaving margin."

한국어 핵심: GPIO 마커 ↔ 안전 출력 scope, 부하·온도별 분포, 최악값 vs 예산.

</details>

**Q3. "How would you protect NPU computation itself, and how would you validate it?"**

<details><summary>답</summary>

"Options are ECC on the SRAMs, parity on control paths, periodic built-in self-tests of the MAC array with known patterns, and at the system level redundant execution or plausibility checks on outputs. I'd validate each by injecting faults — corrupt a weight in SRAM through the ECC inject path, use a BIST fault mode, or corrupt one copy of a redundant job — and confirm detection and the reaction, such as dropping the frame and re-running the job, within the budget. Which of these RAP1 actually uses I don't know, so I'd ask."

한국어 핵심: SRAM ECC·제어 parity·MAC array 주기 BIST·중복 실행/타당성 검사 → 각각 주입으로 검증. RAP1 실제 방식은 모름 → 질문.

</details>

**Q4. "How is this different from normal functional validation?"**

<details><summary>답</summary>

"Functional validation shows the chip does the right thing when nothing is broken. Safety validation shows it does a safe thing when something is broken — so the stimulus is a fault, the pass criterion includes a time, and false alarms are failures too."

한국어 핵심: 기능 = 고장 없을 때 옳게, 안전 = 고장 났을 때 안전하게 + 시간 + 오탐도 실패.

</details>

### 8. Don 연결

- "I haven't owned an ISO 26262 work product, but SSD controllers rely on the same mechanisms — ECC, end-to-end data protection, watchdogs, power-loss handling — and at Apple I built factory test nodes that stress parts to expose latent defects [숫자 채우기], which is the same mindset as proving a mechanism catches faults."

## F5 · SoC 레벨 시스템 시나리오 · SLT 콘텐츠 (★)

### 0. 🔰 이 블록이 뭔가

- **비유**: 부품 검사(블록 테스트)는 다 끝났다. 이제 **차를 실제로 몰아 보는 단계** — 비 오는 밤, 고속도로, 에어컨·라디오·와이퍼를 **동시에** 켜고. 그리고 공장에서 **모든 차**를 5분씩 몰아 볼 수는 없으니, 지금까지 고장을 가장 많이 찾아낸 코스만 골라 **짧은 출고 시험 코스(SLT)** 를 만든다.
- **정확한 정의**: **SoC 레벨 시나리오** = 여러 블록이 실제 데이터 흐름으로 동시에 도는 end-to-end 시험: 카메라 N개 → CSI-2 → ISP → DDR → NPU → CPU 후처리 → 출력(Ethernet/CAN) [추정: 전형적 ADAS 경로], 최대 동시 부하, 반복 부팅, 장시간. **SLT(system-level test)** = 양산에서 각 칩을 실제 보드 같은 환경에서 부팅시켜 짧은 실제 워크로드를 돌려 ATE(구조 시험: scan, MBIST)가 놓친 불량을 걸러내는 단계 (deck §5.1 Q5).

```text
 cameras x N ──► CSI-2 RX ──► ISP ──► DDR ◄──► NPU ──► DDR ──► CPU post-proc ──► Ethernet / CAN out
   (FSYNC, PTP time)          │         ▲         │                 │
                              └─ all on one NoC / LPDDR ─ contention, QoS ─┘
 SoC-level: concurrency · end-to-end latency · power virus · boot N000 · 72 h soak · thermal
                │
                ▼ select & shrink (seconds per chip, self-checking)
 SLT content: boot + DDR stress + NPU golden CRC + link loopbacks + max-power burst  ← from validation tests
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| E2E latency | end-to-end 지연 | 센서 노출 → 결과 출력까지 | 차량 반응 시간 예산 |
| power virus | 최대 전력 패턴 | 일부러 모든 회로를 최대로 토글 | 최악 전류·droop |
| QoS | Quality of Service | 버스 우선순위·대역폭 보장 | 카메라가 NPU 때문에 굶지 않게 |
| SLT | System-Level Test | 양산 칩마다 실제 부팅·워크로드 | ATE가 놓친 불량 |
| test escape | 불량 유출 | 시험을 통과한 불량 칩 | SLT 콘텐츠 선택 기준 |
| self-checking | 자가 판정 | 타깃이 스스로 CRC로 합격 판정 | 양산에서 host golden 불가 |

### 1. 먼저 물어볼 것 (clarify)

- "What's the reference system configuration — number and type of cameras, other sensors, models running — and the end-to-end latency budget?" → 가정: 카메라 여러 대 + 레이더, 프레임 예산 내 [추정: Rivian은 5 Gpixel/s 처리 언급 [1]].
- "Is SLT content in my scope, and what's the per-part time budget and the SLT platform — socketed board, handler, temperature?" → 가정: 수십 초–수 분/칩 [추정], 상온 + 고온 1점.
- "Which escape data exists — ATE fallout, early field or validation silicon-only failures?" → 가정: 검증 실패 DB + ATE 데이터 공유됨.
- "Multi-chip over RivLink in scope?" → 가정: 이번 플랜은 단일 칩, 칩간 링크는 별도 [추정].

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 시스템 기능 | 카메라 N개 → ISP → NPU → 출력 end-to-end, 결과 CRC/정확도 | 녹화 영상 재생(패턴 생성기) + 실제 센서 | Si (FPGA는 축소 경로) | 출력 bit-exact vs 기준 재생, 프레임 drop 0 | P0 |
| E2E latency | stage별 타임스탬프 (FSYNC → SOF → ISP done → NPU done → 출력) | 단독 vs 전체 부하 | Si | p99 ≤ 예산 [추정], stage별 예산 표 (deck §6.6) | P1 |
| 최대 동시성 | 모든 카메라 + NPU 최대 + CPU memcpy + 네트워크 트래픽 | 90% DDR 부하 | Si | 오류 0, QoS: 카메라 경로 언더런 0 | P0 |
| power virus | 최대 전력 조합 (NPU 최대 toggle + CPU + DDR) | 고온 | Si | droop ≥ Vmin 마진, thermal 정책대로 | P1 |
| 부팅 반복 | cold/warm boot 5000–10000회, 부팅 시간 분포 | 온도 3점 | Si | 실패 0 (rule of three → 상한 3/n), 부팅 시간 p99 ≤ 예산 | P1 |
| 리셋 상호작용 | 동작 중 블록 단독 reset·watchdog reset이 다른 블록에 영향 없는지 | 랜덤 | Si | 다른 경로 오류 0 | P1 |
| 전원 이벤트 | cold crank·전원 dip 중 동작 (보드 레벨, BV15) | 프로그래머블 PSU | Si | 정의된 동작 (reset 또는 유지) | P2 |
| 시간 동기 | 카메라 간 skew, PTP 동기 상태에서 timestamp | 다중 카메라 | Si | skew ≤ X [추정] ([L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html)) | P1 |
| soak | 72 h 전체 시나리오, 온도 사이클 | 여러 보드 | Si | 오류 0, ECC CE 추세 평탄 | P2 |
| SW 스택 | 실제 OS·드라이버·런타임·OTA 업데이트 후 동작 | 릴리스 | Si | 시나리오 pass | P1 |
| **SLT 후보 선정** | 검증 테스트별 "유일 검출 수 / 실행 시간" 계산 → 집합 선택 | 검증 실패 DB | 데이터 | 선택 세트가 과거 실리콘-only 실패의 ≥ X% 재현 [추정] | P1 |
| **SLT 상관** | 선택 세트를 양산 플랫폼에서 실행 → ATE·검증 결과와 상관 | 수백–수천 칩 | SLT 보드 | 양품 false-fail ≈ 0, 알려진 불량 검출 | P2 |
| SLT 운영 | 결정성(같은 칩 반복 = 같은 결과), 실행 시간, 장비 의존성 | 반복 | SLT 보드 | 반복 일치 100%, 시간 ≤ 예산 | P2 |

#### 2a. SLT 콘텐츠 고르는 법 (★ Manjiang 경력 = production SLT)

| 기준 | 질문 | 예 |
|---|---|---|
| 검출력 | 이 테스트가 **다른 테스트가 못 잡은** 실리콘 실패를 잡았나? | 검증 DB에서 "유일 검출" 카운트 |
| 시간 효율 | 유일 검출 수 / 실행 초 | 72 h soak는 탈락, 20 s NPU stress는 후보 |
| 실리콘 민감도 | Vmin·Fmax·온도에 민감한 경로를 건드리나? | shmoo에서 margin 최저였던 패턴 |
| ATE 보완 | scan/MBIST가 못 보는 것인가? (실속도 타이밍, 블록 간 상호작용, PHY, 전원 무결성) | DDR training + 트래픽, power virus, 링크 loopback |
| 자가 판정 | host golden 없이 CRC로 합격 판정 가능한가? | NPU golden 출력 CRC를 이미지에 내장 |
| 결정성 | 양품이 매번 같은 결과? (flaky면 수율 손실) | 랜덤 seed 고정 |
| 장비 독립 | 계측기 없이 보드만으로? | 내부 loopback, 온칩 센서 |

- **[예시] 선택 절차**: 검증 실패 DB에서 실리콘에서만 나온 실패 120건을 테스트별로 → 실행 시간 대비 유일 검출이 큰 순서로 greedy 선택(set cover) → 예: ① 부팅 + DDR training + March 패턴 30 s ② NPU 최대 전력 GEMM + golden CRC 20 s at Vmin−guard ③ 카메라 패턴 생성기 → ISP → NPU 경로 15 s ④ PCIe/Ethernet 내부 loopback 10 s → 합계 75 s로 120건 중 [숫자: 데이터로 계산] 재현 → 시간 예산 넘으면 마지막 항목부터 잘라 trade-off 표로 제시.
- **"Validation tests are the source for SLT content"** — 같은 C 테스트를 HAL만 바꿔 쓰는 구조가 여기서 이득 (N06G).
- SLT 실패 칩 → 검증팀으로 돌아와 root cause → 새 테스트 → 다시 SLT 개선: **피드백 루프**를 말하면 Staff 시그널.

### 3. 우선순위 · A0 첫 주

- **P0** (블록 bring-up이 어느 정도 된 뒤, 보통 A0 수 주 후): 단일 카메라 → ISP → NPU → 출력 1개 경로 end-to-end, 이어서 전체 카메라 동시. 이유: 통합 버그(QoS, 주소 맵, 인터럽트 공유)는 블록 테스트로 안 보임.
- **P1**: E2E latency, 최대 동시성·power virus, 부팅 반복, SLT 후보 선정(데이터가 쌓이면 바로 시작 — 양산 일정이 길다).
- **P2**: 72 h soak, 전원 이벤트, SLT 상관·운영.
- **안 하는 것**: 블록 레벨에서 이미 덮은 조합을 시스템 레벨에서 전수 반복 — 시스템 레벨은 **상호작용과 실제 부하**만.

### 4. 장비 · 셋업 · 관찰 지점

- 카메라 **패턴 생성기/영상 재생 장비**(같은 입력 반복 = 결정적 비교), 실제 센서 세트, LED strobe(지연 ground truth, deck §6.6), scope(FSYNC·결과 GPIO), 프로그래머블 PSU, chamber, 보드 팜(N06G). 읽을 것: 블록별 drop/언더런 카운터, NoC/DDR 성능 모니터, 온도·OPP, ECC 카운터, 부팅 단계 타임스탬프.

### 5. 잘 터지는 버그

| 증상 | 그럴듯한 원인 | 잡는 곳 |
|---|---|---|
| NPU를 최대로 돌리면 카메라 프레임 drop | QoS 설정 — 실시간 경로 우선순위 부족, DDR 대역폭 초과 | 최대 동시성 |
| 부팅 5000회 중 2회 hang | 전원/클럭 안정 전 접근, 초기화 race | 부팅 반복 + 단계 타임스탬프 |
| 고온 + power virus에서만 리셋 | droop가 UV 모니터 임계 아래로 | power virus |
| E2E latency p99만 튐 | 공유 인터럽트·CPU 스케줄링, DDR refresh | E2E latency 분포 |
| SLT에서 양품이 가끔 fail (수율 손실) | 테스트 비결정성(타이밍 의존 판정) | SLT 운영 (반복 일치) |
| 검증에선 안 나온 불량이 필드에서 | 콘텐츠가 실리콘 민감 경로를 안 건드림 | SLT 후보 선정의 민감도 기준 + 피드백 루프 |

### 6. 10분 영어 답 뼈대

1. "Clarify: reference configuration — cameras, models, latency budget — and whether SLT is in scope with its time budget."
2. "Picture the data path: cameras into CSI-2 and the ISP, frames in DDR, the NPU, CPU post-processing, results out — all sharing one interconnect and LPDDR."
3. "System tests target interactions, not block features: end-to-end with replayed video so outputs are deterministic, then all cameras plus maximum NPU load plus background traffic."
4. "QoS: the real-time camera path must never underrun, even at 90% DDR load."
5. "End-to-end latency with stage timestamps on one time base, reported as p99 against a per-stage budget, verified by an LED-and-scope ground truth."
6. "Power virus at hot for worst-case current and droop, thousands of boots at three temperatures, and a 72-hour soak."
7. "For SLT, I'd choose from validation tests using data: which tests uniquely caught silicon-only failures, per second of runtime, and which hit margin-sensitive paths that ATE structural tests can't — then make them self-checking and deterministic."
8. "Then correlate on production hardware: no false fails on good parts, known bad parts caught, and close the loop — every SLT or field failure becomes a new validation test."

### 7. 꼬리 질문

**Q1. "SLT time is limited to 60 seconds per part. How do you choose?"**

<details><summary>답</summary>

"I'd treat it as a coverage-per-second problem using real data. From the validation failure database I take every silicon-only failure, map which tests detected it, and greedily pick tests with the most unique detections per second, favoring tests that stress margin-sensitive paths at a guard-banded voltage. Then I show the trade-off curve — coverage versus seconds — so product and test engineering can decide where to cut."

한국어 핵심: 실리콘-only 실패 DB → 초당 유일 검출 greedy → margin 민감 경로 우선 → 커버리지-시간 곡선으로 의사결정.

</details>

**Q2. "Why do you need SLT if ATE has 99% fault coverage?"**

<details><summary>답</summary>

"ATE fault coverage is measured against modeled faults, usually stuck-at and transition faults on scan logic, at tester conditions. Real failures also come from at-speed interactions between blocks, analog and PHY behavior, power delivery under real workloads, and timing paths that scan doesn't exercise the same way. SLT runs the real system and catches those, at the cost of test time."

한국어 핵심: ATE 커버리지 = 모델링된 고장 기준. 블록 상호작용·아날로그·실부하 전원·실속도 경로는 SLT가 잡음.

</details>

**Q3. "Frames drop only when everything runs together. How do you debug?"**

<details><summary>답</summary>

"I'd read the drop and underrun counters to find which stage loses the frame, and the interconnect and DDR monitors to see bandwidth and latency per master at that moment. Then I vary one load at a time — NPU off, CPU traffic off — to find the aggressor, and check the QoS settings for the camera path. If the total demand exceeds DDR bandwidth, it's a system budget problem, not a bug, and that goes back to architecture."

한국어 핵심: drop/언더런 카운터로 단계 특정 → NoC/DDR 모니터 → 부하 하나씩 끄기 → QoS 확인 → 총 대역폭 초과면 아키텍처 문제.

</details>

### 8. Don 연결

- "At Apple I built factory test nodes that stress parts to expose latent defects before shipment [숫자 채우기: 노드 수·검출 건], which is exactly the SLT question — choosing short, high-yield stress content from what validation learned."
- "At SK hynix, chamber-based reliability automation across [숫자 채우기] DUTs taught me that long-run, multi-unit data is what tells you which tests actually find failures."

## 자주 틀리는 것

1. **FP16/BF16 결과를 bit-exact로 비교** → 정상 구현끼리도 46/50 다름 (코드 출력). 순서가 스펙에 고정된 경우에만 bit-exact.
2. **FP 허용오차를 `abs(ref)` 기준 상대오차로** → 정답이 0 근처(상쇄)면 정상 HW가 실패. **Σ abs(a·b)** 기준으로.
3. **reference를 반올림 안 한 FP32/FP64 입력으로 계산** → 입력 양자화 오차를 HW 버그로 오판 (bf16 47배 초과).
4. **허용오차만 쓰고 directed 벡터 생략** → 항 하나 누락 같은 작은 버그를 random 벡터가 놓침. one-hot·corner 벡터를 같이.
5. **INT8에 ±1 허용** → rounding/bias 버그를 숨김. INT8 정수 경로는 bit-exact.
6. **"정확도 99%" 를 HW 합격 기준으로** → 정확도는 양자화·컴파일러 품질 지표. HW 합격은 **컴파일러 bit-accurate 시뮬레이터와 bit-exact**.
7. **roofline을 compute·memory 두 선으로만** → depthwise·stem처럼 array 매핑 효율이 낮은 레이어(MAPPING 천장)를 설명 못 함. 그리고 **FPGA 성능 숫자로 sign-off** 금지.
8. **sparse TOPS를 dense처럼 비교** → 명목값 2배. 리포트에 dtype·dense/sparse·batch·클럭 명시.
9. **"ECC 있음 = 안전"** → SEC-DED 3-bit는 76% SDC. DC는 fault model별로. 그리고 **보고 경로·반응 시간·오탐**까지 봐야 메커니즘 검증.
10. **OTP 퓨즈를 일반 검증 칩에 태우기** → 비가역. 섀도/override → 희생 샘플 + 기록.

## 출처

1. Rivian, "Rivian Autonomy & AI Day" (RAP1, NNE, 1600 sparse INT8 TOPS, 5 Gpixel/s, deterministic latency, ASIL 언급) — https://rivian.com/stories/rivian-autonomy-ai-day
2. electrive, "Rivian unveils custom AI chip and next level of automated driving" (5 nm, MCM) — https://www.electrive.com/2025/12/12/rivian-unveils-custom-ai-chip-and-next-level-of-automated-driving/
3. Arm Newsroom, Arm × Rivian autonomy platform (Cortex-A720AE) — https://newsroom.arm.com/news/arm-rivian-autonomy-platform
4. Uncrewed Systems, "Rivian develops its own silicon for self-driving" (14× A720AE, 8× R52, 800 TOPS/chip — 2차 보도) — https://www.uncrewed-systems.com/rivian-develops-its-own-silicon-for-self-driving/
5. Williams, Waterman, Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", CACM 52(4), 2009 — https://www.cs.colostate.edu/~cs475/f19/more_assignments/Labs/L10/RooflineCACM2009.pdf
6. Google Cloud blog, "BFloat16: The secret to high performance on Cloud TPUs" (1/8/7 형식) — https://cloud.google.com/blog/products/ai-machine-learning/bfloat16-the-secret-to-high-performance-on-cloud-tpus
7. N. Higham, "What Is Bfloat16 Arithmetic?" — https://nhigham.com/2020/06/02/what-is-bfloat16-arithmetic/
8. Dell Technologies InfoHub, "Introduction to MLPerf Inference v1.0" (정확도 목표 = FP32 reference의 99%/99.9%) — https://infohub.delltechnologies.com/p/introduction-to-mlperf-tm-inference-v1-0-performance-with-dell-emc-servers
9. NIST CSRC, Cryptographic Algorithm Validation Program — Block Ciphers (AES KAT 벡터) — https://csrc.nist.gov/Projects/cryptographic-algorithm-validation-program/Block-Ciphers
10. Texas Instruments, functional safety FTTI 설명 (SFFS309A) — https://www.ti.com/document-viewer/lit/html/SFFS309A/GUID-B05AEB04-AF49-41D2-93DE-D6A237066E2A
11. Semiconductor Engineering, "The ABCs of ISO 26262" — https://semiengineering.com/the-abcs-of-iso-26262/
