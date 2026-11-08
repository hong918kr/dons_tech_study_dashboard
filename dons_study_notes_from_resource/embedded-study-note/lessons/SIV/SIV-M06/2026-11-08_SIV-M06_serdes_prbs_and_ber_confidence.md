---
id: SIV-M06
time: 90분
updated: 2026-11-08
---
# SIV-M06 SerDes PRBS 생성·검사와 BER 신뢰구간

## 학습 목표

- PRBS7/15/31 을 **Fibonacci LFSR** 로 구현하고, 주기(2^n − 1)·균형·run 성질로 다항식이 맞는지 **테스트로** 확인한다.
- **self-synchronizing checker** 를 HUNT(받은 비트로 상태 채우기) → LOCKED(로컬 LFSR 로 예측) 두 모드로 짜고, 왜 LOCKED 에서 받은 비트를 상태에 넣으면 안 되는지(**오류 증식**) 설명한다.
- bit slip(CDR 순간 unlock)·P/N 극성 반전·패턴 선택 오류를 checker 의 **서명(signature)** 으로 구분한다: 동기 상실 / 예측 100% 틀림 / 예측 50% 틀림.
- "BER < 1e-12 를 95% 신뢰도로" 에 필요한 비트 수 **N ≥ −ln(1−CL)/BER ≈ 3.0e12** 를 유도하고, 오류 k 개 허용 시 Poisson 으로 일반화한다.
- 링크 속도별 측정 시간을 계산하고, lane 별 판정(PASS / FAIL / **INCONCLUSIVE**)을 디버그 엔지니어가 바로 행동할 수 있는 한 줄로 낸다.

## 1. 문제

고속 직렬 링크(SerDes: PCIe, Ethernet PHY, MIPI, DDR PHY 의 일부) 브링업에서 가장 먼저 돌리는 측정이 **PRBS 루프백 BER 시험**이고, 면접에서는 "PRBS checker 를 짜보라" 와 "에러 0 개면 BER 이 몇이라고 말할 수 있나?" 가 한 세트로 나온다 — 앞은 비트 연산, 뒤는 통계를 본다.

TX 쪽 패턴 생성기(pattern generator)가 의사 난수 비트열(PRBS, Pseudo-Random Binary Sequence)을 내보내고, RX 쪽 검사기(checker)가 **같은 다항식으로 다음 비트를 예측**해 틀린 비트를 센다. 패턴을 미리 맞춰 둘 필요가 없는 이유: checker 가 받은 비트 n 개로 자기 상태를 채우면 바로 TX 와 같은 위상이 된다(self-synchronizing). 실리콘 안에서는 PHY 의 PRBS 선택 레지스터 + 오류 카운터 레지스터로 보이고(FPGA 트랜시버, 예: AMD/Xilinx GT 계열도 PRBS7~31 생성기·검사기·오류 카운터를 내장), 검증 엔지니어는 그 카운터를 읽어 "이 lane 은 BER 몇 이하" 라고 **통계적으로** 주장한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m06-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">PRBS7 = x^7 + x^6 + 1 (Fibonacci LFSR, 상태 s 의 bit0 = 가장 최근 비트)</text>
  <g stroke="currentColor" stroke-width="1.2">
    <rect x="110" y="70" width="60" height="40" class="f-mut"/>
    <rect x="180" y="70" width="60" height="40" class="f-mut"/>
    <rect x="250" y="70" width="60" height="40" class="f-mut"/>
    <rect x="320" y="70" width="60" height="40" class="f-mut"/>
    <rect x="390" y="70" width="60" height="40" class="f-mut"/>
    <rect x="460" y="70" width="60" height="40" class="f-acc"/>
    <rect x="530" y="70" width="60" height="40" class="f-acc"/>
    <circle cx="620" cy="140" r="14" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="140" y="95">bit0</text><text x="210" y="95">bit1</text><text x="280" y="95">bit2</text>
    <text x="350" y="95">bit3</text><text x="420" y="95">bit4</text><text x="490" y="95">bit5</text><text x="560" y="95">bit6</text>
    <text x="490" y="64" font-size="12">탭 k=6</text><text x="560" y="64" font-size="12">탭 n=7</text>
    <text x="620" y="145">⊕</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m06-a)">
    <path d="M560 110 L560 140 L604 140"/>
    <path d="M490 110 L490 180 L620 180 L620 156"/>
    <path d="M620 126 L620 42 L60 42 L60 90 L108 90"/>
    <path d="M60 90 L60 150"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="168">출력 b</text>
    <text x="650" y="136">b = bit6</text><text x="650" y="152">  ⊕ bit5</text>
    <text x="10" y="210">다음 상태 s = (s &lt;&lt; 1 | b) &amp; 0x7F  ·  점화식 s[t] = s[t−7] ⊕ s[t−6]</text>
    <text x="10" y="230">주기 2^7 − 1 = 127 (상태 0 을 뺀 127 개 상태를 한 바퀴)  ·  상태 0 은 lock-up: 영원히 0</text>
  </g>
</svg>
```

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m06-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">시험 구성: TX 생성기 → 채널(결함 주입) → RX checker → 판정·리포트</text>
  <g stroke="currentColor" stroke-width="1.2">
    <rect x="10" y="50" width="130" height="60" rx="6" class="f-mut"/>
    <rect x="180" y="40" width="200" height="120" rx="6" class="f-warn"/>
    <rect x="420" y="50" width="150" height="60" rx="6" class="f-acc"/>
    <rect x="610" y="50" width="120" height="60" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="75" y="75">PRBS 생성기</text><text x="75" y="95" font-size="12">TX: PRBS31 (lane 4: 15)</text>
    <text x="280" y="62">채널 (결함 주입)</text>
    <text x="495" y="75">checker</text><text x="495" y="95" font-size="12">HUNT → LOCKED</text>
    <text x="670" y="75">lane_judge</text><text x="670" y="95" font-size="12">+ lane_report</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="192" y="84">• 랜덤 비트 반전 (BER p, xorshift64)</text>
    <text x="192" y="104">• bit slip: TX 비트 하나 사라짐</text>
    <text x="192" y="124">• P/N swap: 모든 비트 반전</text>
    <text x="192" y="144">• 패턴 선택 오류 (PRBS15→31)</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m06-b)">
    <path d="M140 80 L178 80"/><path d="M380 80 L418 80"/><path d="M570 80 L608 80"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="420" y="135">카운터: bits · errors · first_err_at</text>
    <text x="420" y="153">hunt_pred · hunt_miss · locks · sync_loss</text>
    <text x="10" y="195">실보드: 생성기/checker 는 PHY 안의 하드웨어, 위 카운터는 MMIO 레지스터 (보통 포화형·RC 카운터 — 연습 4).</text>
    <text x="10" y="215">이 레슨: 비트 단위 C 모델. 채널은 결정적 PRNG 로 결함을 심어 "checker 가 정말 잡는가" 를 CHECK 한다.</text>
    <text x="10" y="235">측정 시간(초·시간 단위)은 계산만 한다 — 3e12 비트를 실제로 흘리는 건 HW 의 몫.</text>
  </g>
</svg>
```

### Part A — PRBS 생성기와 self-synchronizing checker

- `prbs_feedback(p, s)`, `prbs_next(g)` — `x^n + x^k + 1` 의 Fibonacci LFSR. 다항식은 `PRBS7 (7,6)`, `PRBS15 (15,14)`, `PRBS31 (31,28)`.
- 테스트: PRBS7·PRBS15 주기가 정확히 127·32767, 한 주기의 1 개수 2^(n−1), 최장 1-run = n, 최장 0-run = n−1. PRBS31 은 30만 비트에서 점화식 `s[t] = s[t−31] ⊕ s[t−28]` 과 균형(전체 주기 2^31−1 은 demo 에서 실제로 돌림). 시드 0 은 lock-up 이라 강제로 바꾼다.
- `chk_hunt`, `chk_locked` — HUNT: 처음 n 비트는 받은 그대로 상태에 싣고, 그 뒤 예측 == 받은 비트가 **32번 연속**이면 LOCKED. LOCKED: 로컬 LFSR 이 기대 비트를 만들고 상태는 **기대 비트**로 민다.
- 테스트: 패턴 3종 × 임의 위상 20개에서 정확히 `n + 32` 비트 뒤 lock, 오류 0 / 정해진 위치 5곳(연속 2비트 포함) 오류 → 정확히 5 / PRBS15 를 PRBS31 checker 에 넣으면 lock 안 됨, 예측 ~50% 틀림.

### Part B — 오류 증식 · 동기 상실 · 극성 · BER 신뢰구간

1. `ff_count_errors` — 받은 비트로만 예측하는 순수 feed-forward checker. 고립 오류 4개 → **12개**로 센다(3배). 붙어 있는 두 오류는 서로 상쇄되기도 한다.
2. `chk_loss` — LOCKED 에서 64비트 창 안 오류가 16개(25%)에 닿으면 동기 상실 → HUNT 로 돌아가 재동기. 1비트 **빠짐**과 1비트 **끼어듦** 모두 `sync_loss == 1, locks == 2`, 재동기 후 오류 증가 없음.
3. BER 1e-3 랜덤 주입 → checker 가 센 오류 == **주입한 오류 수 정확히**.
4. P/N 반전 → HUNT 예측이 **100%** 틀림(우연이 아니라 대수적 필연), `invert = true` 면 정상 lock.
5. `poisson_cdf`, `ber_lambda`, `ber_bits_needed`, `ber_upper`, `ber_confidence` — λ 표(2.996 / 4.744 / 6.296 / 7.754), 1e-12 @95% → 2.996e12 비트(이항식 정확해와 상대오차 < 1e-6), 1e12 비트 무오류 → 신뢰도 63.2%, 그리고 Monte-Carlo 로 "BER = 목표인 링크는 5% 만 통과" 를 확인.

### Part C — 6-lane 회귀 밤샘: 심어 둔 결함을 판정하고 리포트

lane 당 40만 비트, 목표 BER 1e-5 @95% (무오류면 299,574 비트가 필요하므로 40만이면 "깨끗한 lane 은 PASS 가능"). 심어 둔 것:

| lane | 결함 | 기대 판정 |
|---|---|---|
| 0 | 없음 | PASS |
| 1 | BER 1e-4 (eye 가 닫힘) | FAIL BER |
| 2 | 20만 번째 비트에서 bit slip (CDR 순간 unlock) | FAIL SYNC_LOSS |
| 3 | 보드에서 P/N 바뀜 | FAIL POLARITY (invert 로 재시험해 확인) |
| 4 | TX 가 PRBS15 를 보냄 | FAIL NO_LOCK |
| 5 | 실제 BER 2e-6 (목표보다 좋음) | **INCONCLUSIVE** — 비트가 모자라 증명 불가 |

- `lane_judge` — lock 0 → 예측 틀림 비율 ≥ 90% 면 POLARITY, 아니면 NO_LOCK. sync_loss > 0 → SYNC_LOSS. 단측 상한 ≤ 목표 → PASS, 점추정 ≥ 목표 → FAIL BER, 그 사이 → INCONCLUSIVE + 필요한 총 비트 수.
- `lane_report` — `FAIL lane=2 pat=PRBS31 verdict=SYNC_LOSS ... first_loss@200033 ... -> bit slip/CDR unlock: ...` 처럼 증거 + 다음 행동. 버퍼가 작아도 넘치지 않고 "썼어야 할 길이" 반환.
- `ber_test_seconds` — 10/25/56/112 Gb/s 에서 1e-12, 1e-15 를 증명하는 데 걸리는 시간 표.

## 2. Problem (English)

> **PRBS generator/checker and BER confidence.**
>
> 1. Implement a Fibonacci LFSR PRBS generator for `x^n + x^k + 1` (PRBS7 `x^7+x^6+1`, PRBS15 `x^15+x^14+1`, PRBS31 `x^31+x^28+1`): `int prbs_next(prbs_gen_t *g)` returns one bit. Prove in tests that PRBS7/15 have period `2^n − 1`.
> 2. Implement a self-synchronizing checker `int chk_push(prbs_chk_t *c, int rx_bit)`: in HUNT it loads received bits into its state and declares lock after 32 consecutive correct predictions; in LOCKED it free-runs its own LFSR and counts mismatches (`bits`, `errors`, `first_err_at`). If ≥16 errors occur within a 64-bit window it declares loss of sync and re-hunts.
> 3. Show that a checker which keeps feeding received bits (feed-forward) triples each isolated error, while the locked checker counts each error once. Show that a bit slip produces exactly one sync-loss event, and that an inverted (P/N-swapped) stream makes every HUNT prediction fail.
> 4. Derive and implement: the number of error-free bits needed to claim `BER < p` with confidence `CL` is `N = −ln(1−CL)/p` (≈ 3.0e12 for 1e-12 at 95%); with up to `k` errors, `N = λ_k/p` where `P(Poisson(λ_k) ≤ k) = 1 − CL`. Provide `ber_upper(errors, bits, cl)` and the test time at 10/25/56/112 Gb/s.
> 5. For a 6-lane regression, classify each lane as PASS / FAIL(BER, SYNC_LOSS, POLARITY, NO_LOCK) / INCONCLUSIVE and print one actionable line per lane.
>
> Constraints: C11, no external libraries except `<math.h>`, deterministic error injection (fixed-seed xorshift64), no timing inside checked tests.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   # 리눅스면 끝에 -lm
```

실제로 돌린 결과 (앞·뒤만):

```text
FAIL starter.c:254  prbs_period(&PRBS7, 1, 1000) == 127
FAIL starter.c:255  prbs_period(&PRBS7, 0x55, 1000) == 127
FAIL starter.c:256  prbs_period(&PRBS15, 1, 100000) == 32767
...
B2 injected=199 counted=0 bits=0 ber=nan ub95=0.000e+00
...
FAIL starter.c:458  NEAR(ber_test_seconds(25, 1e-15, 0.95, 0) / 3600, 33.29, 0.01)
FAIL starter.c:459  NEAR(ber_test_seconds(25, 1e-12, 0.95, 2), 251.83, 0.01)
2/45 passed
```

(우연히 통과하는 2개: A4 의 점화식 검사 — 스텁이 0 만 내면 `0 = 0 ⊕ 0` 이라 점화식을 "만족"한다. 그래서 바로 아래에 균형 검사를 같이 둔다. 그리고 Part C 의 "lane 4 는 lock 0, inv_lock 아님". exit 1.) 주어진 것: xorshift64, 다항식 표, `prbs_seed`(시드 0 방지), `chk_init`/`chk_push`(모드 전환 뼈대), 채널 시뮬레이터 `lane_simulate`, `lane_test`(POLARITY 의심 시 invert 재시험), `ber_test_seconds`, 하네스 전체.

## 4. 힌트

<details>
<summary>힌트 1 — 탭 위치와 비트 순서</summary>

상태의 bit0 이 가장 최근 비트면 bit(n−1) 은 n 비트 전, bit(k−1) 은 k 비트 전이다. `b = ((s >> (n-1)) ^ (s >> (k-1))) & 1`, `s = ((s << 1) | b) & mask`. 탭을 하나 밀어 `(s >> n) ^ (s >> k)` 로 쓰면 bit n 은 마스크 밖이라 늘 0 → 단순 회전이 되어 PRBS7 주기가 **7** 이 된다(직접 돌려 확인). 주기 테스트가 이런 실수를 바로 잡는다.

</details>

<details>
<summary>힌트 2 — HUNT 는 받은 비트, LOCKED 는 기대 비트</summary>

HUNT 에서는 받은 비트로 상태를 채워야 TX 위상에 붙는다. LOCKED 에서도 받은 비트를 넣으면, 오류 하나가 상태에 들어가 k, n 비트 뒤에 예측을 두 번 더 망친다(오류 3배). LOCKED 에선 `s = (s << 1 | exp)`.

</details>

<details>
<summary>힌트 3 — 동기 상실은 "오류율이 갑자기 50%"</summary>

랜덤 오류는 BER 1e-3 이라도 64비트 창에 평균 0.064개. bit slip 이후엔 기대 비트와 받은 비트가 한 칸 어긋나 매 비트 50% 확률로 다르다 → 창당 ~32개. 문턱 16 이면 둘이 확실히 갈린다. 상실하면 `fill = 0, run = 0` 으로 HUNT.

</details>

<details>
<summary>힌트 4 — 무오류 N 비트의 확률</summary>

실제 BER 이 p 이면 N 비트 동안 오류 0 개일 확률은 `(1−p)^N ≈ e^{−Np}`. "만약 BER 이 p 만큼 나빴다면 이렇게 깨끗한 결과는 5% 이하로만 나온다" 가 95% 주장의 뜻이다 → `e^{−Np} ≤ 0.05`.

</details>

<details>
<summary>힌트 5 — λ 는 이분법</summary>

`poisson_cdf(k, λ)` 는 λ 에 대해 단조 감소한다. `lo = 0, hi = k + 20` (부족하면 두 배씩 늘림)에서 200번 반으로 자르면 double 정밀도 끝까지 간다. 큰 λ 에서 `exp(−λ)` 가 언더플로하지 않게 항을 `exp(i·ln λ − λ − lgamma(i+1))` 로 계산.

</details>

## 5. 풀이

### Part A — LFSR 과 checker

**다항식 → 코드.** `x^n + x^k + 1` 은 "n 비트 전과 k 비트 전을 XOR 한 것이 다음 비트" 다. ITU-T O.150 같은 정의 문서는 "n 번째와 k 번째 단(stage)의 출력을 XOR 해 첫 단으로 되먹임" 이라고 쓰는데, 상태 bit0 = 1단이라고 보면 위 코드와 같다. (그 점화식의 특성다항식을 역순으로 쓰면 `x^n + x^{n−k} + 1` — 원시다항식의 역다항식도 원시라서 주기는 같고, 문서마다 표기가 갈리는 이유다. 장비와 맞출 때는 **비트열 자체**를 비교한다.)

| 패턴 | 다항식 | 주기 | 주로 쓰는 곳 |
|---|---|---|---|
| PRBS7 | x^7 + x^6 + 1 | 127 | 짧은 패턴: 빠른 lock, 단기 지터/ISI 확인 |
| PRBS15 | x^15 + x^14 + 1 | 32,767 | O.150 계열 통신 장비 |
| PRBS31 | x^31 + x^28 + 1 | 2,147,483,647 | 고속 SerDes 스트레스 (예: 10GBASE-R 의 PRBS31 테스트 패턴, IEEE 802.3 Clause 49) |

패턴이 길수록 긴 run(연속 같은 비트, 최대 n 개)과 다양한 비트 조합이 나와서, 수신기의 **기저선 변동(baseline wander)·CDR 추종·ISI** 를 더 세게 흔든다. PRBS7 은 run 이 7 이하라 eye 가 실제보다 좋아 보일 수 있다 — "PRBS7 은 통과, PRBS31 은 실패" 는 흔한 결과다.

**최대 길이 수열(m-sequence) 성질 = 테스트할 수 있는 사실.**

| 성질 | 값 (주기 2^n − 1 기준) | 테스트 |
|---|---|---|
| 주기 | 2^n − 1 (상태 0 을 뺀 모든 상태를 한 번씩) | A1, A2 |
| 균형 | 1 이 2^(n−1) 개, 0 이 2^(n−1) − 1 개 | A3 (64/127, 16384/32767) |
| run | 최장 1-run = n (상태 all-1 한 번), 최장 0-run = n − 1 | A3 |
| 점화식 | s[t] = s[t−n] ⊕ s[t−k] | A4 (PRBS31, 30만 비트) |

주기 테스트가 중요한 이유: 탭 하나를 잘못 잡으면 코드는 멀쩡히 "랜덤해 보이는" 비트를 내지만 주기가 짧다(연습 1: `x^8+x^6+1` 은 주기 30). 이런 생성기로 HW checker 를 시험하면 HW 와 모델이 서로 다른 패턴이라 영원히 lock 안 되고, 사람은 HW 를 의심한다.

**checker 상태 기계.**

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m06-c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">self-synchronizing checker (chk_push)</text>
  <g stroke="currentColor" stroke-width="1.3">
    <rect x="40" y="60" width="250" height="110" rx="10" class="f-warn"/>
    <rect x="450" y="60" width="250" height="110" rx="10" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="55" y="84" font-weight="bold">HUNT</text>
    <text x="55" y="106" font-size="12">fill &lt; n: s = s&lt;&lt;1 | rx (적재만)</text>
    <text x="55" y="126" font-size="12">pred = fb(s); 맞으면 run++, 틀리면 run=0</text>
    <text x="55" y="146" font-size="12">s = s&lt;&lt;1 | rx  ← 받은 비트</text>
    <text x="55" y="164" font-size="12">hunt_pred / hunt_miss 집계</text>
    <text x="465" y="84" font-weight="bold">LOCKED</text>
    <text x="465" y="106" font-size="12">exp = fb(s); s = s&lt;&lt;1 | exp ← 기대 비트</text>
    <text x="465" y="126" font-size="12">bits++, exp≠rx 면 errors++</text>
    <text x="465" y="146" font-size="12">64비트 창 안 오류 수 감시</text>
    <text x="465" y="164" font-size="12">first_err_at 기록</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m06-c)">
    <path d="M290 90 L448 90"/>
    <path d="M450 145 L292 145"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="370" y="82">run == 32</text>
    <text x="370" y="137">창 오류 ≥ 16</text>
    <text x="370" y="162">sync_loss++, fill=0</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="200">깨끗한 스트림: n 비트 적재 + 32 연속 정답 → 정확히 n+32 비트 후 lock (PRBS31: 63).</text>
    <text x="10" y="220">HUNT 의 틀림 비율이 진단 정보: ~0% 정상, ~50% 패턴 불일치/잡음, 100% 극성 반전.</text>
    <text x="10" y="240">LOCKED 의 오류는 1개 = 1개 (증식 없음). BER 의 분자·분모는 LOCKED 동안만.</text>
  </g>
</svg>
```

LOCK_RUN = 32 의 근거: 잘못된 위상·패턴에서 예측은 비트마다 대략 50% 맞으니 32번 연속 맞을 확률은 2^−32 ≈ 2.3e-10. 너무 크게 잡으면 BER 이 높은 lane 에서 lock 자체가 어렵다(예측 하나가 3비트에 의존하므로 정답 확률 ≈ (1−p)^3). 실제 PHY 는 이 값을 레지스터로 노출하기도 한다.

### Part B — 면접관이 파는 곳

**① 오류 증식 (error multiplication).** 받은 비트로 예측하면 오류 하나가 세 번 보인다.

| 시점 | 예측에 쓰는 비트 | 오류 비트 j 가 끼는가 |
|---|---|---|
| t = j | rx[j] 자체가 틀림 | ✔ |
| t = j + k | rx[t−k] = rx[j] | ✔ |
| t = j + n | rx[t−n] = rx[j] | ✔ |

B1 실행: 고립 오류 4개 → feed-forward 12, LOCKED 4. 붙어 있는 두 오류(거리 n−k = 1)는 t = j+7 에서 두 오류가 같은 XOR 에 들어가 상쇄 → 2개가 **4개**로 세어진다(6 이 아니다). 그래서 feed-forward 카운터로는 "오류 수 ÷ 3" 도 정확하지 않다. 같은 현상이 self-synchronous scrambler 에도 있다: 10GBASE-R 의 64b/66b scrambler(x^58 + x^39 + 1)는 선로 오류 하나를 descrambler 출력에서 3개로 만든다.

**② 동기 상실과 재동기 (bit slip).** CDR 이 순간적으로 unlock 되거나 elastic buffer 가 비트를 넣고/빼면, 수신 비트열이 한 칸 어긋난다. 로컬 LFSR 은 여전히 원래 위상이라 이후 모든 비트가 랜덤하게 50% 틀린다 — 재동기 안 하면 BER 0.5 로 보고되고, **"한 번의 slip" 이 "엄청난 BER" 로 둔갑한다**. 반대로 아무 문턱 없이 바로 재동기하는 checker 는 slip 을 숨긴다. 그래서 `sync_loss` 를 **별도 카운터**로 남긴다.

B3/B4 실행: slip 위치 10000, `first_loss_at` 은 그로부터 64비트 안, 상실 전까지 센 오류 16 ≤ errors < 128, 재동기 후 5000비트 동안 오류 증가 0. Part C 의 lane 2 는 `first_loss@200033` (slip 후 33비트, 그 사이 16개 틀림 — 50% 확률에 맞는 숫자).

**③ 극성 반전이 100% 틀리는 이유.** 받은 비트가 모두 반전(r = ¬s)이면 예측은 `¬s[t−n] ⊕ ¬s[t−k] = s[t−n] ⊕ s[t−k] = s[t]` 이고, 받은 값은 `¬s[t]` — **항상** 반대다. 이건 삼항식만의 우연이 아니다: 원시다항식은 항 개수가 홀수여야 하고(짝수면 x = 1 이 근 → (x+1) 로 나눠짐), 그래서 XOR 되는 탭 개수(상수항 제외)는 항상 짝수 → 반전이 상쇄된다. B5 실행: `hunt_miss == hunt_pred == 4969` (5000 − 31 적재). 패턴 선택 오류는 다른 m-sequence 의 이동이라 ~50% (A8: 50.0%) — **틀림 비율 하나로 원인이 갈린다.**

| HUNT 틀림 비율 | 의미 | 다음 행동 |
|---|---|---|
| ≈ 0% 이고 lock | 정상 | BER 측정 |
| ≈ 50%, lock 없음 | 다른 패턴, 신호 없음(잡음), 아주 나쁜 eye | TX/RX PRBS 선택 레지스터, 루프백 경로, 신호 존재(squelch) 확인 |
| ≈ 100%, lock 없음 | P/N 반전 | 보드 배선 확인 또는 RX/TX polarity invert 비트 |
| lock 후 sync_loss | bit slip, CDR unlock, 간헐적 끊김 | CDR lock 상태, refclk ppm 차이, 전원 노이즈 |

참고로 일부 표준 정의는 PRBS 출력을 반전해서 쓰기 때문에 BERT·트랜시버에는 보통 PRBS "invert" 옵션이 따로 있다. 장비와 칩이 서로 다른 관례를 쓰면 위의 100% 서명이 그대로 나온다.

**④ 랜덤 오류는 정확히 세어진다.** B2: lock 후 BER 1e-3 주입, `injected=199 counted=199 bits=199937 ber=9.953e-04 ub95=1.119e-03`. "주입한 수 == 센 수" 를 CHECK 해야 checker 를 믿을 수 있다 — 오류 증식이나 동기 상실 오탐이 있으면 여기서 깨진다.

**⑤ BER 신뢰구간 — 유도.**

N 비트를 보냈고 실제 BER 이 p 라면, 각 비트는 독립적으로 확률 p 로 틀린다고 모델링한다(이진 대칭 채널). 오류 수 X ~ Binomial(N, p), p 가 작고 N 이 크면 X ≈ Poisson(λ = Np).

1. 오류 0 개일 확률: `P(X = 0) = (1 − p)^N ≈ e^{−Np}`.
2. "BER < p 를 신뢰도 CL 로 주장한다" = "만약 실제 BER 이 p 라면, 오류 0 개라는 결과가 나올 확률이 1 − CL 이하" (p 보다 나쁜 링크에서는 그 확률이 더 작다).
3. `e^{−Np} ≤ 1 − CL` → **`N ≥ −ln(1 − CL) / p`**.
4. CL = 0.95: `−ln 0.05 = ln 20 = 2.9957` → p = 1e-12 이면 **N ≥ 2.9957e12 ≈ 3.0e12 비트**. (통계에서 "rule of three": 0/N 이면 95% 상한 ≈ 3/N.)
5. 정확한 이항식으로는 `N ≥ ln(1−CL) / ln(1−p)`. B7 에서 `log1p(-1e-12)` 로 계산해 Poisson 근사와 상대오차 < 1e-6 확인 — p 가 작으면 근사는 사실상 정확하다.

오류 k 개까지 허용하면 `P(X ≤ k; λ) = 1 − CL` 을 만족하는 λ_k 를 찾고 `N = λ_k / p`. 이 λ_k 는 카이제곱 분포로 `χ²(CL; 2(k+1)) / 2` 와 같다(같은 값이 BER 계산기·테스트 장비 문서에 표로 나온다).

| k (허용 오류) | λ_k @95% | λ_k @99% | 1e-12 @95% 필요 비트 |
|---|---|---|---|
| 0 | 2.996 | 4.605 | 3.00e12 |
| 1 | 4.744 | 6.638 | 4.74e12 |
| 2 | 6.296 | 8.406 | 6.30e12 |
| 3 | 7.754 | 10.045 | 7.75e12 |

뒤집어 쓰면 **측정 결과의 상한**: 오류 k 개 / N 비트 → `BER_ub = λ_k / N` (`ber_upper`). 그리고 이미 끝난 측정의 신뢰도: 1e12 비트 무오류면 `1 − e^{−1} = 63.2%` 뿐이다(B8) — "1e12 비트 돌렸으니 1e-12" 는 흔한 오답.

**⑥ 95% 의 진짜 의미 — OC 곡선.** CL 은 "BER < p 일 확률 95%" 가 아니다(빈도주의에서 BER 은 고정값). **"BER 이 정확히 목표인 링크가 이 시험을 통과할 확률이 5%"** 라는 시험 절차의 성질이다. B10 은 p = 1e-3, N = 2996 비트로 1000번 시험해 `pass@target=50/1000` (이론 5.0%), `pass@target/10=732/1000` (이론 74.1%). 즉 **목표보다 10배 좋은 링크도 무오류 시험에서 26% 는 떨어진다** — 연습 3 이 이걸 고치는 시험 설계다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">통과 확률 vs (실제 BER / 목표 BER) — 무오류(k=0) vs 오류 2개 허용(k=2)</text>
  <g stroke="currentColor" fill="none">
    <path d="M60 40 L60 260 L700 260"/>
    <path d="M487 40 L487 260" stroke-dasharray="4 4" opacity=".6"/>
    <path d="M103 40 L103 260" stroke-dasharray="4 4" opacity=".6"/>
    <polyline stroke-width="2" points="60,40 81,71 103,97 124,120 145,139 167,156 188,170 209,183 231,194 252,203 273,211 295,218 316,224 337,229 359,233 380,237 401,240 423,243 444,245 465,247 487,249 508,251 529,252 551,253 572,254 593,255 615,256 636,256 657,257 679,257 700,258"/>
    <polyline stroke-width="2" stroke-dasharray="7 4" points="60,40 81,41 103,46 124,55 145,69 167,86 188,105 209,123 231,141 252,158 273,174 295,188 316,200 337,211 359,219 380,227 401,233 423,238 444,243 465,246 487,249 508,251 529,253 551,255 572,256 593,257 615,257 636,258 657,258 679,259 700,259"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <circle cx="103" cy="97" r="4"/><circle cx="103" cy="46" r="4"/><circle cx="487" cy="249" r="4"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="44">1.0</text><text x="30" y="154">0.5</text><text x="40" y="264">0</text>
    <text x="55" y="278">0</text><text x="95" y="278">0.1</text><text x="480" y="278">1.0</text><text x="690" y="278">1.5</text>
    <text x="300" y="298">실제 BER ÷ 목표 BER</text>
    <text x="112" y="100">k=0: 74.1%</text><text x="112" y="58">k=2: 97.4%</text>
    <text x="495" y="238">둘 다 5% (설계상)</text>
    <text x="250" y="120">실선 e^(−2.996 r) : 무오류, N = 3.0/목표</text>
    <text x="250" y="140">점선 P(X≤2; 6.296 r) : N = 6.3/목표</text>
  </g>
</svg>
```

### Part C — 회귀 밤샘 리포트

`lane_judge` 의 순서에 이유가 있다: lock 이 안 되면 BER 을 계산할 분모가 없고(먼저 원인 분류), sync_loss 가 있으면 오류 수가 slip 버스트로 오염돼 BER 이 의미 없다. 그 다음에야 통계 판정. 그리고 **INCONCLUSIVE 를 FAIL 과 섞지 않는다** — "비트가 모자라 증명 못 함" 은 하드웨어 결함이 아니라 시험 시간 부족이다.

실제 실행한 Part C 출력:

```text
-- Part C: lane report (target BER 1e-05 @ 95%, 400000 bits/lane) --
PASS lane=0 pat=PRBS31 verdict=PASS rx=400000 bits=399937 err=0 ber=0.00e+00 ub95=7.49e-06 target=1.00e-05
FAIL lane=1 pat=PRBS31 verdict=BER rx=400000 bits=399937 err=38 ber=9.50e-05 ub95=1.25e-04 target=1.00e-05 first_err@2621 -> eye/margin: run SIV-M07 shmoo on this lane
FAIL lane=2 pat=PRBS31 verdict=SYNC_LOSS rx=400000 loss=1 first_loss@200033 relocks=1 err=16 -> bit slip/CDR unlock: check CDR lock, refclk ppm
FAIL lane=3 pat=PRBS31 verdict=POLARITY rx=400000 hunt_miss=100.0% inverted_lock=yes -> P/N swap: check board or set RX invert
FAIL lane=4 pat=PRBS31 verdict=NO_LOCK rx=400000 hunt_miss=50.0% -> pattern mismatch (TX/RX PRBS select) or no signal
INCONCLUSIVE lane=5 pat=PRBS31 verdict=INCONCLUSIVE rx=400000 bits=399937 err=3 ber=7.50e-06 ub95=1.94e-05 target=1.00e-05 -> need 775366 bits with <=3 err (rerun longer)
```

| lane | 읽는 법 | 다음 행동 |
|---|---|---|
| 0 | 399,937 비트(lock 에 63비트 사용) 무오류 → 상한 7.49e-6 < 1e-5 | PASS. 단, 이 숫자는 "1e-5 이하" 만 말한다 |
| 1 | 오류 38개, 점추정 9.5e-5 가 이미 목표의 9.5배 → 비트를 더 돌릴 필요 없이 FAIL | eye margin 측정: 전압·위상 shmoo(SIV-M07), CTLE/DFE 설정, 채널 손실 |
| 2 | slip 1회, slip 버스트 16개 외엔 깨끗 | BER 문제가 아니라 **클럭 문제**: CDR lock 표시, refclk 주파수 차(ppm), SSC 설정 |
| 3 | 예측 100% 틀림, invert 로 재시험하면 lock | 회로도/레이아웃 P/N 확인, 의도된 swap 이면 polarity 레지스터로 보정 |
| 4 | 50% 틀림, invert 해도 안 됨 | TX/RX PRBS 선택 레지스터 불일치 확인 (이 lane 만 PRBS15) |
| 5 | 실제로는 2e-6 인 좋은 lane 이지만 오류 3개 → 상한 1.94e-5 | 77.5만 비트(오류 ≤ 3 유지 시)로 재측정. 판정을 FAIL 로 내면 멀쩡한 보드를 RMA 한다 |

lane 5 는 시드 고정 결과 오류 3개가 나왔다(기대값 0.8 — 운이 나쁜 경우). 이것이 바로 INCONCLUSIVE 가 필요한 이유다: 점추정 7.5e-6 은 목표보다 좋지만 그걸 95% 로 **증명**하지는 못한다.

**측정 시간 (실제 출력).** 오류 0 개, 95%:

```text
-- BER test time @95% CL, 0 errors --
    10 Gb/s  1e-12:    299.6 s   1e-15:    299573 s (83.21 h)
    25 Gb/s  1e-12:    119.8 s   1e-15:    119829 s (33.29 h)
    56 Gb/s  1e-12:     53.5 s   1e-15:     53495 s (14.86 h)
   112 Gb/s  1e-12:     26.7 s   1e-15:     26748 s (7.43 h)
```

- 1e-12 는 lane 당 수십 초~5분이라 회귀에 넣을 수 있지만, 1e-15 는 25 Gb/s 에서 33시간 — **직접 측정 불가**. 그래서 실무에서는 (a) 전압/위상 오프셋을 줘 BER 을 일부러 높인 점들(1e-6~1e-9)을 재고 **외삽(bathtub curve, Q-factor)** 하거나, (b) 많은 lane·보드를 병렬로 돌린다(lane 마다 따로 주장하려면 lane 마다 N 비트가 필요; "전체 링크의 BER" 을 주장할 때만 비트를 합칠 수 있다).
- 오류 2개 허용 시 25 Gb/s 1e-12 는 251.8 s (CHECK) — 2.1배 길어지지만 좋은 링크가 억울하게 떨어질 확률이 26% → 2.6% 로 준다(연습 3).
- PAM4 링크(56/112 Gb/s)는 비트율 기준이다(심볼율은 절반). 그리고 PAM4 + FEC 링크는 목표 자체가 다르다: Ethernet 의 PAM4 lane 은 FEC 이전 BER 목표가 1e-4 대, PCIe 6.0 도 FEC 를 전제로 FEC 이전 오류율 목표가 1e-6 수준이다. "1e-12" 는 PCIe 1~5세대, 10GBASE-R 같은 NRZ 링크의 숫자다.

**demo — PRBS31 전체 주기 실제 확인 (demo_prbs31_period.c).** 체크되는 테스트에는 넣지 않았다(약 2초, 시간은 기계마다 다름). Apple M6, Apple clang 21.0.0, `-O2`:

```text
PRBS31 serial: period=2147483647 (2^31-1=2147483647) ones=1073741824  1.92 s  1.12 Gbit/s
PRBS31 28-bit parallel: 76695844 words (2147483632 bits)  0.06 s  33.17 Gbit/s  (acc=10293402150296143)
```

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_prbs31_period.c -o demo_prbs31_period && ./demo_prbs31_period
```

한 주기에 1 이 정확히 2^30 개(균형 성질). 비트 직렬은 1.1 Gbit/s 라 실제 SerDes 속도의 1/20 도 안 된다 — HW 생성기가 데이터패스 폭(32/64비트)만큼 **병렬로** 만드는 이유이고, 28비트 병렬판(연습 2)은 30배 빠르다.

**demo — OC 곡선 Monte-Carlo (demo_ber_mc.c).** 비트마다 난수를 뽑는 대신 "첫 오류까지 간격 ~ 기하분포" 로 건너뛰어 3e12 비트 시험을 10만 번씩 시뮬레이션:

```text
zero-error test, N=2.9957e+12 bits, 100000 trials per row
true BER      pass(MC)   pass(theory e^-N*BER)
1.0e-13       74.02%     74.11%
3.0e-13       40.45%     40.71%
5.0e-13       22.43%     22.36%
1.0e-12        5.01%      5.00%
2.0e-12        0.24%      0.25%
3.0e-12        0.01%      0.01%
```

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_ber_mc.c -o demo_ber_mc && ./demo_ber_mc
```

**실무 연결.** Pre-silicon 에서는 PHY RTL 의 PRBS 생성기/검사기를 시뮬레이션·에뮬레이션으로 확인하고(이때 이 레슨 같은 C/Python 참조 모델과 비트열을 비교), post-silicon 에서는 (1) 칩 내부 루프백(near-end/far-end)으로 PHY 자체를, (2) 외부 BERT(Bit Error Rate Tester)·오실로스코프와 연결해 채널·보드를, (3) 실제 상대 칩과 링크 트래픽으로 시스템을 본다. ATE 는 시간 제약 때문에 짧은 PRBS + 내부 루프백, SLT/시스템 검증이 긴 BER 시험과 PVT 코너(SIV-M08)를 맡는다(SIV-M01 참고).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san
```

실제 결과: B2/B10 진단 2줄 + Part C 리포트 6줄 + 측정 시간 표 + `45/45 passed`, exit 0 (전체 0.1 s 미만). ASan/UBSan 빌드도 `45/45 passed`, 경고 없음. `<math.h>` 는 맥에선 libSystem 에 들어 있어 `-lm` 없이 링크되고, 리눅스 gcc 에서는 `-lm` 을 붙인다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `b = (s >> n) ^ (s >> k)` (탭 하나씩 밀림) | 다른 다항식이 됨 → 주기가 짧거나 HW 와 다른 패턴 | `(s >> (n-1)) ^ (s >> (k-1))`, 주기 테스트로 고정 |
| 시드 0 허용 | LFSR 이 0 에 갇혀 영원히 0 출력 — checker 는 "완벽한 lock, 오류 0" 을 보고할 수도 | `prbs_seed` 에서 0 → 0 아닌 값 |
| LOCKED 에서도 받은 비트로 상태 갱신 | 오류 1개가 3개로 (B1), BER 3배 과대 | LOCKED 는 기대 비트 `exp` 를 상태에 싣는다 |
| 동기 상실 검출 없음 | slip 한 번에 BER ≈ 0.5 로 보고 | 창 오류 문턱 → HUNT, `sync_loss` 별도 카운트 |
| 재동기하면서 slip 을 기록 안 함 | 간헐적 CDR 문제를 숨김 | `sync_loss`, `first_loss_at` 리포트 |
| HUNT 중 비트도 BER 분모에 포함 | lock 전 비트는 비교된 게 아님 | LOCKED 동안의 `bits` 만 |
| "1e12 비트 무오류 → BER < 1e-12" | 신뢰도 63% 뿐 | 95% 면 3.0e12 비트 |
| 오류 k 개인데 `k / N` 만 보고 | 점추정은 신뢰구간이 아님 (k=0 이면 0!) | `ber_upper = λ_k / N` |
| 비트 부족을 FAIL 로 판정 | 멀쩡한 lane 을 불량으로 (lane 5) | INCONCLUSIVE + 필요한 비트 수 |
| `exp(-λ) * pow(λ, i) / tgamma(i+1)` | 큰 λ·i 에서 언더/오버플로 | `exp(i·ln λ − λ − lgamma(i+1))` |
| 16비트 카운터를 시험 끝에 한 번만 읽음 | 포화(0xFFFF)되면 오류 수가 하한일 뿐 | poll 간격 < 65535/(rate·BER), 포화 플래그 (연습 4) |
| 여러 lane 비트를 합쳐 lane 별 PASS | 나쁜 lane 하나가 좋은 lane 들에 묻힘 | lane 별 N 비트, 합산은 "링크 전체" 주장에만 |

## 8. 면접 Q&A

<details>
<summary>Q1. PRBS checker 는 TX 와 어떻게 위상을 맞추나요? 시드를 공유해야 하나요?</summary>

공유할 필요 없다. 다음 비트는 직전 n 비트로 완전히 결정되므로, checker 가 받은 비트 n 개로 자기 상태를 채우면 TX 와 같은 위상이 된다(self-synchronizing). 다만 적재한 n 비트 중 오류가 있으면 틀린 위상에서 시작하므로, 예측이 연속 M 번 맞을 때만 lock 을 선언한다(여기선 32, 오탐 ≈ 2^−32). 꼬리질문 "lock 후에도 받은 비트로 계속 상태를 갱신하면?" → 오류 증식: 오류 하나가 t, t+k, t+n 세 번 보인다(삼항식). lock 후엔 로컬 LFSR 을 자유 동작시킨다.

</details>

<details>
<summary>Q2. 오류가 0 개였습니다. BER 이 몇이라고 말할 수 있나요? 1e-12 를 주장하려면 얼마나 돌려야 하나요?</summary>

"BER 이 0" 이 아니라 "BER < λ/N (신뢰도 CL)" 만 말할 수 있다. 실제 BER 이 p 면 무오류 확률은 `(1−p)^N ≈ e^{−Np}`, 이게 1−CL 이하가 되려면 `N ≥ −ln(1−CL)/p`. 95% 면 `ln 20 ≈ 3.0` 이라 1e-12 → 3.0e12 비트, 10 Gb/s 에서 약 5분, 25 Gb/s 에서 2분. 꼬리질문 "1e12 비트만 돌렸다면?" → `1 − e^{−1} = 63%` 신뢰도. "99% 면?" → `ln 100 = 4.6` → 4.6e12 비트.

</details>

<details>
<summary>Q3. 오류가 3개 나왔습니다. 그래도 목표를 만족한다고 말할 수 있나요?</summary>

Poisson 상한으로 판단한다: `P(X ≤ 3; λ) = 0.05` 인 λ = 7.754 → `BER_ub = 7.754 / N`. 이게 목표 이하면 PASS. 처음부터 "오류 3개 허용" 으로 계획했다면 N = 7.754/p 비트를 돌린다. 꼬리질문 "그럼 항상 k 를 크게 잡는 게 좋나?" → 시험 시간이 늘어나는 대신 목표보다 좋은 링크가 억울하게 떨어질 확률이 준다(k=0: 10배 좋은 링크도 26% FAIL, k=2: 2.6%). 시간과 오판정 비용의 trade-off 다. 주의: 결과를 보고 나서 k 를 고르면 통계가 무효다 — 계획 단계에서 정한다.

</details>

<details>
<summary>Q4. 1e-15 를 25 Gb/s 에서 증명해야 합니다. 33시간을 돌리나요?</summary>

보통 안 돌린다. (1) RX 샘플링 위치/전압 오프셋을 줘서 BER 을 1e-6~1e-9 영역으로 올린 점들을 재고(bathtub curve), 가우시안 랜덤 지터 가정 하에 Q-factor 로 외삽한다 — 몇 분 측정으로 1e-15 지점의 마진을 추정. (2) 여러 lane·보드를 병렬로 돌린다(단, lane 별 주장에는 lane 별 비트가 필요). (3) FEC 가 있는 링크는 FEC 이전 BER 과 FEC 이후 오류(정정 불가 codeword)를 따로 본다. 꼬리질문 "외삽의 약점은?" → 결정적 지터·간헐 이벤트(크로스토크 버스트, 전원 글리치, CDR slip)는 가우시안 꼬리로 안 잡힌다. 그래서 긴 무오류 soak 시험도 병행한다.

</details>

<details>
<summary>Q5. lane 하나가 lock 은 되는데 가끔 오류가 수십 개씩 몰려 나옵니다. 어떻게 분리하나요?</summary>

랜덤 오류(BER)와 버스트/slip 을 분리해야 한다. checker 가 sync_loss 를 따로 세는지, 오류 시각(first_err_at, 가능하면 오류 타임스탬프 히스토그램)을 남기는지 본다. 버스트가 주기적이면 SSC 변조 주기·전원 레귤레이터 스위칭·인접 lane 트래픽 패턴과 상관을 본다. slip 이면 BER 이 아니라 클럭(CDR lock, refclk ppm, SSC 설정, elastic buffer) 문제다. 꼬리질문 "그 lane 의 BER 숫자는 어떻게 보고하나?" → slip 이 있으면 BER 판정 자체를 무효(SYNC_LOSS)로 하고, 원인 수정 후 재측정한다. 버스트를 평균 BER 로 뭉개면 원인이 사라진다.

</details>

<details>
<summary>Q6. checker 가 lock 을 못 합니다. 첫 5분 안에 무엇을 보나요?</summary>

HUNT 의 예측 틀림 비율. ~100% 면 P/N 극성 반전(원시다항식의 XOR 탭 수가 짝수라 반전이 상쇄되어 예측이 항상 반대), ~50% 면 패턴 불일치(TX/RX PRBS 선택, 비트 순서/폭 설정, 8b10b·scrambler 가 켜져 있음)나 신호 없음. 그다음 루프백 모드·lane 매핑(lane swap)·PLL/CDR lock 상태. 꼬리질문 "PRBS invert 옵션은 왜 있나?" → 일부 표준 정의가 반전 출력을 쓰고, 보드가 P/N 을 일부러 바꾸기도 해서 — 장비와 칩의 관례가 다르면 정확히 이 100% 서명이 나온다.

</details>

<details>
<summary>Q7. PRBS7 과 PRBS31 중 무엇으로 시험하나요? 결과가 다르면?</summary>

PRBS31 이 더 스트레스다: 최장 run 31 비트(저주파 성분 → AC 결합 기저선 변동, CDR 이 오래 전이 없이 버팀)와 더 다양한 ISI 패턴. PRBS7 은 run ≤ 7 이라 eye 가 좋아 보인다. PRBS7 통과·PRBS31 실패면 저주파 응답(AC coupling cap, CTLE 저주파 이득), CDR 추종, baseline wander 를 의심한다. 꼬리질문 "실제 트래픽은 scrambler 를 거치는데 PRBS31 이 대표성이 있나?" → 64b/66b 같은 scrambled 데이터는 통계적으로 PRBS31 에 가깝고, 8b/10b 는 run 이 5 이하로 제한되어 PRBS7 쪽에 가깝다. 그래서 실제 인코딩에 맞춰 패턴을 고르고, 최악 패턴은 따로 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — 다른 다항식과 "잘못된 탭"

PRBS9 (x^9 + x^5 + 1), PRBS11 (x^11 + x^9 + 1), PRBS23 (x^23 + x^18 + 1) 의 주기를 확인하라. 그리고 x^8 + x^6 + 1 은?

<details>
<summary>답 보기</summary>

511 / 2047 / 8,388,607 (모두 2^n − 1 → 원시다항식). x^8 + x^6 + 1 은 실행 결과 **주기 30** (최대 255). 차수 8 에는 원시 삼항식이 없어서, 8비트 LFSR 은 탭이 4개인 다항식(예: x^8 + x^6 + x^5 + x^4 + 1 — 같은 코드로 주기 255 확인)을 쓴다. 교훈: 다항식을 바꾸면 **주기 테스트를 반드시** 돌린다.

</details>

### 연습 2 — 28비트 병렬 PRBS31

직렬 생성기와 같은 비트열을 한 번에 28비트씩 만들어라.

<details>
<summary>답 보기</summary>

새 비트 j(0 이 먼저) = `s[t+j−31] ⊕ s[t+j−28]` 이고 j ≤ 27 이면 둘 다 이미 상태 안에 있다(bit 30−j, bit 27−j). `x = s ^ (s >> 3)` 의 bit(27−j) 가 그 값이므로 `x & 0x0FFFFFFF` 가 28비트 출력(bit27 이 먼저), 상태는 `(s << 28 | x) & 0x7FFFFFFF`. 2만 워드 동안 직렬과 비트·상태 모두 일치(CHECK). 28 을 넘으면 방금 만든 비트에 의존해 한 번 더 XOR 이 필요하다 — HW 는 32/64비트 폭을 이렇게 행렬(XOR 트리)로 펼친다. demo 에서 직렬 1.1 → 병렬 33 Gbit/s.

</details>

### 연습 3 — 시험 계획: 허용 오류 수 k 고르기

목표 BER 링크는 5% 만 통과하고(95% 신뢰), 목표보다 10배 좋은 링크는 95% 이상 통과해야 한다. 최소 k 와 N 은?

<details>
<summary>답 보기</summary>

N = λ_k/p 로 잡으면 목표 링크의 통과 확률은 정의상 5%. 10배 좋은 링크의 통과 확률 = `P(X ≤ k; λ_k/10)`: 실행 결과 k=0 74.1%, k=1 91.7%, k=2 **97.4%** → k = 2, N = 6.296/p (1e-12 면 6.3e12 비트, 25 Gb/s 에서 4.2분). 이것이 "무오류 시험" 대신 "오류 몇 개 허용 + 더 긴 시험" 을 쓰는 이유다.

</details>

### 연습 4 — 16비트 포화 · RC 오류 카운터

PHY 의 오류 카운터가 16비트, 0xFFFF 에서 포화, 읽으면 0 으로 지워진다(RC). 25 Gb/s, BER 1e-6 이면 최대 poll 간격은? 포화를 어떻게 보고하나?

<details>
<summary>답 보기</summary>

초당 오류 = 25e9 × 1e-6 = 25,000 → 65535 / 25000 = **2.62 s** 보다 자주 읽어야 한다(32비트면 171,799 s). 읽은 값이 0xFFFF 면 그 구간의 실제 오류는 그 이상일 수 있으므로 합계는 **하한**(`≥ 131175`)으로 보고하고 saturated 플래그를 남긴다. 실제 오류 [100, 65535, 70000, 5] → 읽은 합 131,175, 실제 135,640. RC 카운터는 다른 스크립트가 읽어도 지워지므로 한 소유자만 읽게 한다(SIV-B03/B04 의 RC 다루기 참고).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
```

실제 출력:

```text
P1 x^8+x^6+1 period=30 (max 255)
P3 k=0 N=2.996e12 bits (for 1e-12)  pass@target=5.0%  pass@target/10=74.1%
P3 k=1 N=4.744e12 bits (for 1e-12)  pass@target=5.0%  pass@target/10=91.7%
P3 k=2 N=6.296e12 bits (for 1e-12)  pass@target=5.0%  pass@target/10=97.4%
P4 25Gb/s BER 1e-6 16-bit counter: poll every < 2.62 s (32-bit: 171799 s)
11/11 passed
```

## 10. 핵심 정리

- PRBS = 원시다항식 LFSR: `b = s[n−1] ⊕ s[k−1]`, 주기 2^n − 1, 1 이 하나 더 많고, 최장 run n. **다항식을 바꾸면 주기 테스트.** 시드 0 금지.
- PRBS7 `x^7+x^6+1`, PRBS15 `x^15+x^14+1`, PRBS31 `x^31+x^28+1`. 긴 패턴일수록 CDR·기저선 스트레스가 크다.
- checker: HUNT 는 **받은 비트**로 상태를 채우고 M 연속 정답이면 lock, LOCKED 는 **기대 비트**로 자유 동작(오류 증식 방지).
- 서명: HUNT 틀림 100% = P/N 반전, 50% = 패턴 불일치/무신호, lock 후 창 오류 폭증 = bit slip → `sync_loss` 를 BER 과 따로.
- 무오류 N 비트 → `BER < −ln(1−CL)/N`. 95% 는 `3/N`: **1e-12 → 3.0e12 비트** (10G 5분, 25G 2분). 1e12 비트 무오류는 63% 뿐.
- 오류 k 개: `BER_ub = λ_k/N`, λ = 2.996 / 4.744 / 6.296 / 7.754 (k = 0..3, 95%) = χ²(2k+2)/2.
- 판정은 NO_LOCK/POLARITY → SYNC_LOSS → 통계 순서, 비트 부족은 FAIL 이 아니라 **INCONCLUSIVE + 필요한 비트 수**.
- 1e-15 는 직접 못 잰다(25G 에서 33시간) → bathtub/Q 외삽 + 병렬 lane, FEC 링크는 FEC 이전 목표가 따로 있다.

## 참고자료

- ITU-T Recommendation O.150 — General requirements for instrumentation for performance measurements on digital transmission equipment (PRBS 정의)
- IEEE 802.3 Clause 49 (10GBASE-R PCS) — 64b/66b scrambler x^58 + x^39 + 1, PRBS31 test-pattern mode
- PCI Express Base Specification — 전기적 사양의 BER 요구치, Physical Layer Logical Block (LTSSM, PCIe 6.0 FLIT/FEC)
- Maxim Integrated (현 Analog Devices) 애플리케이션 노트 "Statistical Confidence Levels for Estimating Error Probability" — BER 신뢰도 계산
- Keysight / Tektronix BERT 애플리케이션 노트 — bathtub curve, Q-factor 외삽, BER confidence 계산기
- Wikipedia — [Linear-feedback shift register](https://en.wikipedia.org/wiki/Linear-feedback_shift_register), [Pseudorandom binary sequence](https://en.wikipedia.org/wiki/Pseudorandom_binary_sequence), [Rule of three (statistics)](https://en.wikipedia.org/wiki/Rule_of_three_(statistics))
- 관련 레슨: SIV-B02 (mock MMIO), SIV-B03/B04 (RC 레지스터), SIV-B09 (타임아웃 대기 — lock 대기), SIV-B14 (리포트), SIV-M01 (ATE/SLT/시스템 역할), SIV-M04 (PCIe 링크 플로우), SIV-M07 (shmoo/마진), SIV-M08 (PVT)
