---
id: SIV-M05
time: 90분
updated: 2026-11-08
---
# SIV-M05 DDR 검증: Training 결과와 메모리 스트레스

## 학습 목표

- DDR PHY training 결과 레지스터(rank × byte lane × RD/WR 의 DQS–DQ delay window)를 읽어 **eye 폭·centering margin** 을 UI 단위로 계산하고, OK / WARN / INCONC / FAIL 을 근거와 함께 판정할 수 있다.
- rank 교집합, delay line 끝에 닿은(clipped) 창, 경계값(22 tap vs 23 tap)처럼 면접에서 파고드는 판정 함정을 코드로 처리한다.
- solid / checkerboard / own-address / LFSR(주소·데이터) / row hammer 패턴이 **각각 어떤 결함을 잡고 어떤 결함을 놓치는지** 측정 매트릭스로 설명한다.
- 오류를 lane / DQ 비트 / 방향(0→1, 1→0) / bank·row / 바이트 주소로 쌓는 **비트 단위 오류 맵** 을 만들고, 그것으로 PIN·LANE·ROW·ADDR 원인을 구별하는 진단 규칙을 쓴다.
- training WARN 과 stress 오류를 엮어 "보드 EVT1-07 의 lane2 읽기 마진" 처럼 디버그 엔지니어가 바로 움직일 수 있는 리포트를 만든다.

## 1. 문제

DRAM 이 안 올라오면 칩 위의 다른 어떤 것도 검증할 수 없다 — 그래서 첫 실리콘·첫 보드에서 **DDR bring-up 과 메모리 스트레스는 가장 먼저, 가장 오래 도는 검증** 이고, Rivian·NVIDIA·Apple 류 실리콘/보드 검증 면접의 단골 주제("training 결과에서 뭘 보나요?", "memtest 가 통과했는데 시스템이 죽어요")다.

DDR 인터페이스는 수백 ps 짜리 비트(UI, unit interval)를 보드 트레이스 너머로 주고받는다. lane(=byte lane, DQ 8개 + DQS 한 쌍)마다 트레이스 길이·부하가 달라서, PHY 는 부팅 때 **training** 으로 lane 별 지연을 맞춘다. DDR4 기준 대략 이런 순서다 (세부는 PHY·DRAM 종류마다 다르다):

| 단계 | 무엇을 맞추나 | 결과로 남는 것 |
|---|---|---|
| ZQ calibration | 출력 드라이버·ODT 저항값 | 임피던스 코드 |
| Write leveling | DRAM 에 도착하는 DQS 를 CK 에 정렬 (fly-by 토폴로지 보정) | lane 별 WR DQS 지연 |
| Read DQS gate training | 읽기 때 DQS 를 받기 시작할 시점(프리앰블) | gate 지연 |
| Read eye centering | 읽기 DQ 를 DQS 로 샘플할 지연 — DDR4 는 MPR 패턴으로 | **RD window [L,R] + 적용 delay** |
| Write eye centering | 쓰기 DQ 와 DQS 의 관계 (쓰고 읽어 비교) | **WR window [L,R] + 적용 delay** |
| VrefDQ training | DRAM 쪽 수신 기준 전압 (DDR4 MR6) | Vref 코드 → 2D eye (연습 1) |

벤더 PHY 는 이 과정을 PHY 안의 마이크로컨트롤러 펌웨어로 돌리고, 결과를 CSR 이나 메시지 블록으로 내놓는 경우가 많다. 검증 엔지니어의 일은 "training done=1 이니 OK" 가 아니라 **결과 숫자를 읽어 마진이 충분한지** 판정하고, 그다음 **패턴 스트레스** 로 실제 데이터가 깨지지 않는지 확인하는 것이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">한 lane 의 RD window (delay tap 축, 64 tap = 1 UI = 312.5 ps @ DDR4-3200)</text>
  <line x1="40" y1="120" x2="700" y2="120" stroke="currentColor" stroke-width="2"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="40" y="140">0</text><text x="700" y="140">127 (max_tap)</text>
  </g>
  <rect x="150" y="60" width="155" height="50" class="f-mut" stroke="currentColor"/>
  <rect x="160" y="70" width="155" height="50" class="f-acc" stroke="currentColor" opacity="0.8"/>
  <text x="152" y="56" fill="currentColor" font-size="12">rank0 [20,52]</text>
  <text x="250" y="56" fill="currentColor" font-size="12">rank1 [22,53]</text>
  <rect x="160" y="160" width="150" height="34" class="f-ok" stroke="currentColor"/>
  <text x="168" y="182" fill="currentColor" font-size="12">교집합 [22,52] = 30 tap</text>
  <line x1="235" y1="150" x2="235" y2="210" stroke="currentColor" stroke-width="3"/>
  <text x="240" y="224" fill="currentColor" font-size="12">dly=37 (적용된 지연)</text>
  <path d="M160 240 L235 240 M235 240 L310 240" stroke="currentColor"/>
  <text x="168" y="256" fill="currentColor" font-size="12">margin = min(37-22, 52-37) = 15 tap = 234 mUI</text>
  <g fill="currentColor" font-size="12">
    <text x="400" y="170">width = 30 × 1000/64 = 468 mUI (146.48 ps)</text>
    <text x="400" y="192">WARN: width &lt; 350 mUI 또는 margin &lt; 120 mUI</text>
    <text x="400" y="214">INCONC: L=0 또는 R=max_tap (경계를 못 찾음)</text>
    <text x="400" y="236">FAIL: lane_err, 빈 창, rank 교집합 없음, dly 가 창 밖</text>
  </g>
  <rect x="40" y="70" width="14" height="40" class="f-warn" stroke="currentColor"/>
  <text x="58" y="94" fill="currentColor" font-size="12">clipped?</text>
  <text x="10" y="288" fill="currentColor" font-size="12">rank 가 delay 를 공유하는 PHY 라면 실제로 쓸 수 있는 창은 rank 창의 교집합이다.</text>
</svg>
```

이번 레슨의 mock DRAM(x32, 4 MiB, 8 bank × 128 row × 1024 col)에 심는 결함:

| 결함 | 물리적 원인 (실제 예) | 오류 맵에 보이는 모양 |
|---|---|---|
| DQ 핀 stuck (DQ13=0) | BGA 볼 크랙, 트레이스 open, PHY 비트 고장 | 비트 1개, 모든 row, 한 방향(1→0)만 |
| row 주소 비트 alias | 주소/명령선 short·open, 디코더 결함 | word 전체가 **다른 주소의 데이터**, own-address 에서 diff = 주소 비트 1개 |
| hammer 에 약한 행 | 셀 누설 + 이웃 행 반복 활성화 (RowHammer) | 한 (bank,row) 의 셀 몇 개, hammer 패턴에서만 |
| odd-mode crosstalk (DQ18) | 이웃 DQ 가 반대로 토글할 때 victim 전이 지연 | 데이터 의존: checkerboard 에서 대량, random 에서 1/32, solid 0 |
| lane 마진 부족 (Part C) | lane2 읽기 eye 가 좁음 (SI, Vref, 지터) | 한 lane 의 비트 몇 개, 여러 row 에 흩어진 **간헐** 오류 |

### Part A — training 결과 레지스터 해석과 마진 판정

PHY 결과 레지스터(mock)는 `bus_ops_t` 뒤에 있다 (SIV-B02 의 bus ops 를 축약해 넣었다). `PHY_CFG`(taps/UI, max_tap, MT/s), `PHY_TSTAT`(done, rank별 lane_err, rank1 존재), `PHY_WIN(rank,lane,dir)` = `[7:0] L, [15:8] R`, `PHY_DLY(lane,dir)` = 적용된 delay.

- `win_decode` / `win_intersect` — 창 해석과 rank 교집합.
- `check_lane` — 우선순위 FAIL > INCONC > WARN > OK 로 판정하고 width/margin 을 tap·mUI·fs 로 채운다.
- 정상 보드는 8개(lane × RD/WR) 모두 OK, 심은 결함 6종(좁은 eye, off-center delay, lane_err, rank 창 분리, clipped, 창 밖 delay)과 경계값(22/23 tap), rank 1개 보드를 CHECK 한다.

### Part B — 스트레스 패턴과 비트 단위 오류 맵

- `errmap_record` — 불일치 한 건을 failbits, 비트별 rise/fall, (bank,row), own-address diff AND/OR, 첫 4줄 리포트로 쌓는다.
- `checker_word` / `pat_checker` — (row ⊕ col) 패리티 checkerboard 와 그 반전.
- `lfsr20_next` / `pat_lfsr` — x²⁰+x¹⁷+1 Galois LFSR 주소 순서 + xorshift32 데이터. 기대값은 seed 를 다시 돌려 **재생성** 한다.
- `pat_hammer` — row stripe 로 채우고, bank 마다 victim 의 양쪽 행을 번갈아 읽어 ACT 를 몰아준 뒤 전체 확인.
- 5 결함 × 5 패턴 매트릭스를 측정하고 "누가 무엇을 잡나" 를 CHECK 한다.

### Part C — 야간 회귀: 보드 5장 진단 리포트

- `diagnose(errmap, training)` — PASS / ADDR / PIN / ROW_CELL / LANE_MARGIN / UNKNOWN 규칙 기반 분류. PIN·LANE 은 같은 lane 의 training 판정이 OK 가 아니면 *corroborated*.
- 보드 EVT1-01(정상) / -03(주소 alias) / -07(lane2 읽기 마진 + 간헐 오류) / -11(hammer 약한 행) / -14(DQ13 stuck + training lane_err) 를 모두 맞히고, EVT1-07 을 seed 3개로 다시 돌려 **오류 수는 달라도 진단은 같은지** 확인한다.

## 2. Problem (English)

> **DDR training margin check and memory stress with a bit-level error map.**
>
> 1. A DDR PHY exposes per-rank, per-byte-lane read and write DQS/DQ delay windows `[L,R]` (in delay taps), the applied delay per lane, taps-per-UI, the delay-line length, and a status register with per-rank lane error bits. Implement `lane_st_t check_lane(const bus_ops_t *phy, unsigned lane, unsigned dir, const train_limits_t *lim, lane_rep_t *out)` that intersects the rank windows (the delay is shared by ranks), computes eye width and centering margin `min(dly-L, R-dly)` in taps, milli-UI and femtoseconds, and classifies the lane as FAIL (lane error, empty window, disjoint ranks, delay outside window), INCONCLUSIVE (window clipped at tap 0 or max_tap), WARN (width < `min_width_mui` or margin < `min_margin_mui`) or OK.
> 2. On a 4 MiB x32 DRAM model with injectable faults, implement checkerboard, LFSR-ordered random data (x²⁰+x¹⁷+1 address LFSR, regenerate expected data from the seed instead of storing it) and a double-sided row-hammer pattern. Record every mismatch into an error map indexed by DQ bit (with direction), bank/row and pattern; keep the first four failures as `FAIL mem=DDR0 pat=<p> addr=<abs> bank= row= col= exp= act= diff= dq=[...]`. Show which pattern detects which fault.
> 3. Classify a board's result as PIN / ADDR / ROW_CELL / LANE_MARGIN and corroborate it with the training result.
>
> Follow-ups: Why can't a checkerboard find an address-line short? Why does a sequential memtest never trigger RowHammer? Your nightly run shows one bit error in 8 hours — what next?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # SDK 27.2 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 실제 출력 끝부분: "6/40 passed" (exit 1) — TODO 10곳을 채워 40/40 을 만든다
```

mock PHY, mock DRAM 과 결함 주입, `pat_solid` · `pat_own_addr`(예시 패턴), 출력 함수, 하네스는 주어진다. 시작 상태에서는 모든 lane 이 OK 로 나오고(판정 함수가 비어 있으므로) 매트릭스의 checker/lfsr/hammer 열이 0 이다. 정상 보드 0 오류 같은 CHECK 는 처음부터 PASS 한다 — 아무것도 안 하는 테스트도 PASS 하기 때문에 `reads/pattern` 으로 **패턴이 실제로 일했는지** 를 같이 본다 (SIV-B07 과 같은 교훈).

## 4. 힌트

<details>
<summary>힌트 1 — 창 해석과 교집합</summary>

`L = reg & 0xFF`, `R = (reg >> 8) & 0xFF`, 유효 조건은 `R > L` (R == L 은 폭 0). 교집합은 `[max(L), min(R)]` 이고 결과가 `R > L` 이어야 유효. 시작값을 `[0, max_tap]` 인 유효 창으로 두면 rank 수와 상관없이 같은 루프로 처리된다.

</details>

<details>
<summary>힌트 2 — 판정 순서와 단위</summary>

FAIL 사유를 먼저 다 거른 다음 clipped(INCONC), 그다음 WARN. mUI 는 `taps * 1000 / taps_per_UI` — 정수 나눗셈의 **내림** 은 폭을 작게 보므로 보수적이다. fs 는 `taps * (1e9 / MT/s) / taps_per_UI` (UI = 1e9/3200 = 312500 fs). rank 1개 보드(`TSTAT[16]=0`)면 rank1 레지스터는 쓰레기일 수 있으니 읽지 않는다.

</details>

<details>
<summary>힌트 3 — LFSR 은 0 을 못 간다</summary>

Galois 오른쪽 시프트: `lsb = s & 1; s >>= 1; if (lsb) s ^= 0x90000;`. 0 은 고정점이라 LFSR 은 1..2²⁰-1 만 돈다 → word 0 을 따로 먼저 쓴다. 읽기 때는 `ds = seed` 로 되돌리고 **같은 순서** 로 다시 돌리면 기대값이 그대로 나온다 (두 번째 4 MiB 버퍼 불필요).

</details>

<details>
<summary>힌트 4 — hammer 는 "다른 행" 을 번갈아</summary>

ACT 는 같은 bank 에서 **열려 있는 행과 다른 행** 에 접근할 때만 생긴다. 같은 행을 1000번 읽어도 row hit 라 ACT 0 번이다. victim v 의 양쪽 `(v-1, col0)` 과 `(v+1, col0)` 을 번갈아 읽으면 매 접근이 ACT. v-1 과 v+1 은 패리티가 같아 row stripe 데이터가 같다 — victim 만 반대 데이터.

</details>

<details>
<summary>힌트 5 — 진단 규칙의 순서</summary>

ADDR 을 PIN 보다 먼저: stuck 핀도 own-address 에서 "diff = 같은 1비트" 를 만든다. 그래서 ADDR 은 **failbits 가 3개 이상 lane 에 걸칠 것**(LFSR 에서 word 전체가 엉뚱한 데이터)을 추가 조건으로 둔다. PIN 은 failbits 1비트 + **오류 row 3개 이상** — 셀 하나가 1비트만 깨진 경우를 PIN 으로 오진하지 않게.

</details>

## 5. 풀이

### Part A — 숫자로 판정하기

판정 우선순위와 이유:

| 순서 | 조건 | 판정 | 왜 |
|---|---|---|---|
| 1 | `TSTAT[0]=0` | FAIL | training 이 안 끝났는데 남은 숫자는 이전 값일 수 있다 |
| 2 | `lane_err` (rank 별) | FAIL | PHY 가 스스로 경계를 못 찾았다고 말함 |
| 3 | rank 창 하나라도 `R ≤ L` | FAIL | 그 rank 는 이 lane 에서 동작 영역이 없다 |
| 4 | rank 교집합 없음 | FAIL | 각 rank 는 OK 인데 **공유 delay 하나로 둘 다 만족 불가** — rank 별로만 보면 놓친다 |
| 5 | `dly ∉ [L,R]` | FAIL | 펌웨어가 결과를 잘못 적용 (예: rank0 결과만 적용) |
| 6 | 어떤 rank 의 L=0 또는 R=max_tap | INCONC | 진짜 경계가 delay line 밖 — 폭은 하한값, 중심은 틀렸을 수 있음 |
| 7 | width < 350 mUI | WARN | eye 가 좁다 — 온도·전압이 바뀌면 깨질 후보 |
| 8 | margin < 120 mUI | WARN | 창은 넓은데 delay 가 한쪽 끝에 붙었다 (centering 버그) |

350 mUI / 120 mUI 는 **예시 값** 이다. 실제 한계는 SI 시뮬레이션과 PHY 벤더 가이드, 그리고 측정한 지터·온도 드리프트 예산에서 나온다. 중요한 건 "판정 기준이 숫자로 문서화되어 있고 모든 보드에 같은 기준을 쓴다" 는 것이다.

INCONC 를 따로 두는 이유: delay sweep 범위가 모자라 L=0 에서 잘린 창은 "폭 15 tap" 처럼 보여도 실제 폭은 그보다 클 수 있고, PHY 가 계산한 중심은 진짜 중심이 아니다. WARN 으로 뭉개면 "SI 문제" 로 오해해 보드팀을 괴롭히게 되고, OK 로 넘기면 진짜 마진을 모른다. 보통은 delay 범위(coarse delay, 반 UI 오프셋)를 바꿔 다시 sweep 한다.

경계값: 64 tap/UI 에서 350 mUI = 22.4 tap. 22 tap = 343 mUI (WARN), 23 tap = 359 mUI (OK) — CHECK 로 고정했다. `width_mui` 를 반올림하면 22.4 와 22.5 tap 근처 판정이 바뀔 수 있으니 **한쪽(보수적)으로 통일** 하고 리포트에 tap 원값도 같이 찍는다.

실제 출력 (`solution.c` Part A):

```text
[Part A] DDR4-3200 x32, 64 tap/UI, UI=312.5ps, limits: width>=350mUI margin>=120mUI
  good board: all 8 lane/dir OK, lane0 RD eff=[22,52] width=468mUI margin=234mUI
  planted defects:
    WARN   lane=2 dir=RD win=[30,48] width=18t/281mUI(87.8ps) dly=39 margin=9t/140mUI why=eye width below limit
    WARN   lane=1 dir=WR win=[28,58] width=30t/468mUI(146.4ps) dly=31 margin=3t/46mUI why=delay off-center (margin below limit)
    FAIL   lane=3 dir=RD why=lane_err (rank1)
    FAIL   lane=3 dir=WR why=lane_err (rank1)
```

두 번째 줄이 좋은 예다 — eye 폭은 정상 보드와 똑같이 468 mUI 인데 delay 31 이 왼쪽 경계 28 에서 3 tap 떨어져 있다. "폭만 보는" checker 는 이 lane 을 OK 로 보낸다.

### Part B — 패턴이 잡는 것, 놓치는 것

각 패턴이 존재하는 이유:

| 패턴 | 데이터 / 순서 | 노리는 것 |
|---|---|---|
| solid | all-0 → all-1, 순차 | DQ stuck, 데이터 경로 기본. 가장 싸다 |
| checkerboard | (row⊕col) 패리티로 0x55…/0xAA…, 반전 한 번 더 | 인접 셀 반대 값(누설·결합), **버스 위 모든 DQ 가 매 beat 토글** → crosstalk/SSO |
| own-address | data = 자기 주소, 다음 ~주소 | 주소 결함: 모든 주소가 서로 다른 값이라 alias 가 반드시 보임, diff 가 **곧 주소 비트** |
| LFSR 주소 + random 데이터 | x²⁰+x¹⁷+1 순서, xorshift32 | 주소·데이터 동시 무작위: row miss·bank 전환이 많은 트래픽, 데이터 의존 결함을 통계적으로 |
| row hammer | row stripe, victim 양쪽 행 번갈아 ACT | 셀 disturbance (RowHammer). 다른 패턴은 ACT 를 몰아주지 못한다 |

"checkerboard" 는 두 가지 뜻이 섞여 쓰인다. 메모리 셀 배열 관점의 checkerboard 는 **물리적으로** 이웃한 셀이 반대 값이어야 하는데, 논리 주소→물리 위치는 스크램블되어 있어 칩 데이터 없이는 정확히 못 만든다. 시스템 레벨 memtest 의 checkerboard(memtester 의 "Checkerboard") 는 주로 **버스 토글** 효과가 크다 — 이 레슨의 crosstalk 결함이 그것을 보여 준다.

LFSR 순서의 핵심: x²⁰+x¹⁷+1 은 원시(primitive) 다항식이라 상태가 1..2²⁰-1 을 **정확히 한 번씩** 돈다. 0 은 고정점(0 → 0)이라 절대 나오지 않으므로 word 0 은 따로 쓴다. CHECK 로 주기 1048575 와 "0 포함 전 주소 정확히 1번" 을 확인했다. 기대 데이터를 저장하지 않고 seed 로 재생성하는 것은 실전에서 필수다 — 테스트 대상이 DRAM 전체(수 GiB)면 기대값을 둘 곳이 없다.

실제 측정한 매트릭스 (`solution.c` Part B, 숫자는 오류 난 읽기 수):

```text
  errors per (fault, pattern):
  fault \ pattern         solid    checker   own-addr       lfsr     hammer
  good                        0          0          0          0          0
  DQ13 stuck-at-0       1048576    1048576    1048576     524402     927488
  row-bit4 alias              0          0    1048576     524288          0
  weak row b2/r33             0          0          0          0          1
  xtalk DQ18                  0    2096896          0      32661          0
  reads/pattern         2097152    2097152    2097152    1048576    1854976
```

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">검출 매트릭스 (측정) — 어떤 패턴도 혼자서는 전부 못 잡는다</text>
  <g fill="currentColor" font-weight="bold" text-anchor="middle">
    <text x="235" y="52">solid</text><text x="345" y="52">checker</text><text x="455" y="52">own-addr</text>
    <text x="565" y="52">lfsr</text><text x="675" y="52">hammer</text>
  </g>
  <g fill="currentColor">
    <text x="12" y="88">DQ13 stuck-at-0</text><text x="12" y="134">row-bit4 alias</text>
    <text x="12" y="180">weak row (b2,r33)</text><text x="12" y="226">xtalk DQ18</text>
  </g>
  <g stroke="currentColor">
    <rect x="185" y="66" width="100" height="34" class="f-ok"/><rect x="295" y="66" width="100" height="34" class="f-ok"/>
    <rect x="405" y="66" width="100" height="34" class="f-ok"/><rect x="515" y="66" width="100" height="34" class="f-ok"/>
    <rect x="625" y="66" width="100" height="34" class="f-ok"/>
    <rect x="185" y="112" width="100" height="34" class="f-mut"/><rect x="295" y="112" width="100" height="34" class="f-mut"/>
    <rect x="405" y="112" width="100" height="34" class="f-ok"/><rect x="515" y="112" width="100" height="34" class="f-ok"/>
    <rect x="625" y="112" width="100" height="34" class="f-mut"/>
    <rect x="185" y="158" width="100" height="34" class="f-mut"/><rect x="295" y="158" width="100" height="34" class="f-mut"/>
    <rect x="405" y="158" width="100" height="34" class="f-mut"/><rect x="515" y="158" width="100" height="34" class="f-mut"/>
    <rect x="625" y="158" width="100" height="34" class="f-ok"/>
    <rect x="185" y="204" width="100" height="34" class="f-mut"/><rect x="295" y="204" width="100" height="34" class="f-ok"/>
    <rect x="405" y="204" width="100" height="34" class="f-mut"/><rect x="515" y="204" width="100" height="34" class="f-warn"/>
    <rect x="625" y="204" width="100" height="34" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="235" y="88">잡음</text><text x="345" y="88">잡음</text><text x="455" y="88">잡음</text><text x="565" y="88">잡음</text><text x="675" y="88">잡음</text>
    <text x="235" y="134">놓침</text><text x="345" y="134">놓침</text><text x="455" y="134">diff=A19</text><text x="565" y="134">잡음</text><text x="675" y="134">놓침</text>
    <text x="235" y="180">놓침</text><text x="345" y="180">놓침</text><text x="455" y="180">놓침</text><text x="565" y="180">놓침</text><text x="675" y="180">1건</text>
    <text x="235" y="226">놓침</text><text x="345" y="226">거의 매번</text><text x="455" y="226">놓침</text><text x="565" y="226">1/32</text><text x="675" y="226">놓침</text>
  </g>
  <text x="10" y="264" fill="currentColor" font-size="12">같은 데이터를 여러 주소에 쓰는 패턴(solid/checker/stripe)은 주소 alias 를 원리적으로 못 본다.</text>
  <text x="10" y="282" fill="currentColor" font-size="12">순차 접근은 row 당 ACT 1번 → hammer 불가. random 은 ACT 가 많지만 전 행에 흩어져 임계에 못 간다.</text>
</svg>
```

칸마다의 이유:

- **alias × checker/solid/hammer = 0.** row 비트 4 를 무시하면 row r 과 r+16 이 같은 셀이다. 패리티 (row⊕col)&1 과 stripe(row&1)는 bit 4 와 무관하므로 두 주소에 **같은 값** 을 쓴다 → 덮어써도 티가 안 난다. own-address 는 모든 주소가 서로 다른 값이라 반드시 보이고, 실패 줄 `exp=0x80000000 act=0x80080000 diff=0x00080000` 이 곧 답이다: 바이트 주소 bit 19 = word bit 17 = row bit 4.
- **alias × own-addr = 1048576 = NWORDS.** 순차로 쓰면 r+16 이 나중에 써서 r 을 덮는다 → 패스마다 절반(r 쪽)이 실패, 두 패스(주소, ~주소) 합이 NWORDS. LFSR 은 쓰는 순서가 무작위라 어느 쪽이 이기는지 섞이지만 정확히 절반(524288)이다.
- **weak row × hammer = 1.** 모델은 축소했다: 임계 600, aggressor 당 400번 → 한쪽 400 < 600 < 양쪽 800 (연습 2 에서 최소 n 을 측정). 순차 패턴은 row 마다 ACT 1번, LFSR 은 refresh 창(10만 접근)당 row 마다 평균 ~100번 ACT 라 이웃 합이 ~200 으로 임계 아래다. 실제 DRAM 의 임계는 64 ms refresh 창 안의 활성화 수로 말하며, DDR3 시절 보고(Kim et al., 2014)는 최소 약 13.9만 번이었고 이후 세대에서는 훨씬 낮아졌다고 보고된다 (Kim et al., 2020).
- **xtalk × lfsr = 32661.** 결함 조건은 "victim 이 토글하고, 이웃 두 비트가 모두 victim 과 **반대 방향** 으로 토글". 무작위 데이터에서 victim 토글 확률 1/2, 이웃 하나가 반대 값으로 끝나며 토글할 확률 1/2 × 1/2 = 1/4, 두 이웃 → 1/2 × 1/4 × 1/4 = **1/32**. 기대값 1048576/32 = 32768, 표준편차 √(Np(1-p)) ≈ 178 → 측정 32661 은 -0.6σ 로 이론과 일치. checkerboard 는 0x55…↔0xAA… 라 매 읽기가 최악 조합이다 — 2097152 - 2096896 = 256 건만 빠지는데, row 가 바뀌는 지점(패스당 127곳)과 패스의 첫 읽기(직전 버스 값 = 마지막으로 쓴 값)에서 패리티가 이어져 토글이 없기 때문이다 (128 × 2 패스). solid 와 own-address(순차 증가는 bit 17·19 가 bit 18 과 반대로 동시에 토글하는 경우가 없다)는 0.

오류 맵 출력 일부 (실제):

```text
   - weak row b2/r33 / hammer
    reads=1854976 errors=1 failbits=0x04000200  by pattern: solid=0 checker=0 own-addr=0 lfsr=0 hammer=1
    lane1(DQ8-15): DQ9=1(0->1:1,1->0:0)
    lane3(DQ24-31): DQ26=1(0->1:1,1->0:0)
    FAIL mem=DDR0 pat=hammer addr=0x8010A554 bank=2 row=33 col=341 exp=0x00000000 act=0x04000200 diff=0x04000200 dq=[9,26]
   - xtalk DQ18 / checker
    reads=2097152 errors=2096896 failbits=0x00040000  by pattern: solid=0 checker=2096896 own-addr=0 lfsr=0 hammer=0
    lane2(DQ16-23): DQ18=2096896(0->1:1048448,1->0:1048448)
    FAIL mem=DDR0 pat=checker addr=0x80000004 bank=0 row=0 col=1 exp=0xAAAAAAAA act=0xAAAEAAAA diff=0x00040000 dq=[18]
```

맵의 각 축이 하는 일: **DQ 비트 + 방향** 은 pin(한 방향만) vs 마진(양방향) 구별, **(bank,row)** 은 셀/행 결함 vs 버스 결함 구별, **패턴별 카운트** 는 결함 메커니즘(주소/데이터 의존/disturb) 추정. 리포트 줄은 JTAG 로 바로 `mdw 0x8010A554` 해 볼 수 있게 절대 주소, 그리고 bank/row/col 로 DRAM 내부 좌표를 같이 준다. 이 모델의 weak cell 은 저장값을 반전(XOR)시킨다 — 실제 DRAM 은 셀 종류(true/anti cell)에 따라 1→0 또는 0→1 로 한쪽 방향 누설이 많다.

**호스트 처리량 (관찰용 demo)** — `cc -std=c11 -Wall -Wextra -O2 demo_stress_bench.c -o demo_stress_bench && ./demo_stress_bench` (실제 출력, Apple M6, Apple clang 21.0.0):

```text
buffer = 67108864 words (256 MiB), write pass + read/compare pass each, best of 3
pattern           ms      GB/s
solid           34.6     15.50
checker         40.5     13.25
own-addr        35.7     15.03
lfsr           523.9      1.02   (visited 67108864 words)
lfsr/sequential(solid) time ratio = 15.1x, errors=0
```

`./demo_stress_bench 20` (4 MiB, 캐시 안에 들어가는 크기)에서는 같은 비율이 5.7x 였다. 무작위 순서는 캐시·TLB·DRAM row 지역성을 모두 깨기 때문에 느리고, 바로 그 때문에 **다른 스트레스** 다 (실제 칩에서는 ACT/PRE 가 늘고 전류 패턴이 바뀐다). 처음에는 `volatile` 없이 측정했는데 solid 가 98.73 GB/s 로 나왔다 — 컴파일러가 "방금 쓴 값" 이라며 읽기·비교 루프를 지워 버린 것이다. memtest 코드는 반드시 `volatile` 포인터(또는 non-cacheable 매핑)로 접근해야 한다.

### Part C — 오류 맵 → 진단 → 다음 행동

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">diagnose() 규칙 순서 — 앞의 규칙이 뒤의 오진을 막는다</text>
  <g stroke="currentColor">
    <rect x="10"  y="40" width="130" height="56" rx="6" class="f-mut"/>
    <rect x="155" y="40" width="130" height="56" rx="6" class="f-acc"/>
    <rect x="300" y="40" width="130" height="56" rx="6" class="f-acc"/>
    <rect x="445" y="40" width="130" height="56" rx="6" class="f-acc"/>
    <rect x="590" y="40" width="140" height="56" rx="6" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="62" font-weight="bold">errors == 0</text><text x="20" y="82">→ PASS</text>
    <text x="165" y="62" font-weight="bold">own-addr diff 1비트</text><text x="165" y="82">&amp;&amp; ≥3 lane → ADDR</text>
    <text x="310" y="62" font-weight="bold">failbits 1비트</text><text x="310" y="82">&amp;&amp; row ≥3 → PIN</text>
    <text x="455" y="62" font-weight="bold">오류 row ≤ 2</text><text x="455" y="82">→ ROW_CELL</text>
    <text x="600" y="62" font-weight="bold">failbits 한 lane</text><text x="600" y="82">→ LANE_MARGIN</text>
  </g>
  <g stroke="currentColor" stroke-width="2">
    <path d="M140 68 L153 68"/><path d="M285 68 L298 68"/><path d="M430 68 L443 68"/><path d="M575 68 L588 68"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="165" y="120">stuck 핀도 diff 1비트 →</text><text x="165" y="136">lane 조건으로 배제</text>
    <text x="310" y="120">셀 1비트 결함을</text><text x="310" y="136">PIN 으로 오진 방지</text>
    <text x="455" y="120">hammer 에서만이면</text><text x="455" y="136">hammer_only</text>
    <text x="600" y="120">training 이 그 lane 을</text><text x="600" y="136">WARN/FAIL → corroborated</text>
  </g>
  <rect x="10" y="160" width="720" height="86" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="22" y="182">EVT1-03 → ADDR A19      EVT1-07 → LANE_MARGIN lane2 dq=[17,21] (training WARN 과 일치)</text>
    <text x="22" y="204">EVT1-11 → ROW_CELL bank2 row33 hammer-only      EVT1-14 → PIN DQ13 stuck-at-0 (training lane_err 와 일치)</text>
    <text x="22" y="226">training 과 stress 가 같은 곳을 가리키면 확신, 엇갈리면 그 자체가 다음 실험 대상이다.</text>
  </g>
</svg>
```

실제 출력 (`solution.c` Part C, 보드 EVT1-07 과 EVT1-14 부분):

```text
  [EVT1-07] training:
    WARN   lane=2 dir=RD win=[31,46] width=15t/234mUI(73.2ps) dly=38 margin=7t/109mUI why=eye width below limit
  [EVT1-07] stress:
    reads=9195008 errors=79 failbits=0x00220000  by pattern: solid=0 checker=58 own-addr=0 lfsr=21 hammer=0
    lane2(DQ16-23): DQ17=55(0->1:31,1->0:24) DQ21=24(0->1:13,1->0:11)
    FAIL mem=DDR0 pat=checker addr=0x80041678 bank=1 row=8 col=414 exp=0x55555555 act=0x55575555 diff=0x00020000 dq=[17]
    FAIL mem=DDR0 pat=checker addr=0x8008E3E0 bank=6 row=17 col=248 exp=0xAAAAAAAA act=0xAAA8AAAA diff=0x00020000 dq=[17]
    FAIL mem=DDR0 pat=checker addr=0x800A9B0C bank=1 row=21 col=707 exp=0x55555555 act=0x55755555 diff=0x00200000 dq=[21]
    FAIL mem=DDR0 pat=checker addr=0x800AA184 bank=2 row=21 col=97 exp=0x55555555 act=0x55575555 diff=0x00020000 dq=[17]
  [EVT1-07] => FAIL diag=LANE_MARGIN lane=2 (DQ16-23) dq=[17,21] (corroborated by training)

  [EVT1-14] training:
    FAIL   lane=1 dir=RD why=lane_err (rank0)
    FAIL   lane=1 dir=WR why=lane_err (rank0)
  [EVT1-14] stress:
    reads=9195008 errors=4597618 failbits=0x00002000  by pattern: solid=1048576 checker=1048576 own-addr=1048576 lfsr=524402 hammer=927488
    lane1(DQ8-15): DQ13=4597618(0->1:0,1->0:4597618)
```

다른 보드의 판정 줄 (실제): `[EVT1-01] => PASS diag=PASS`, `[EVT1-03] => FAIL diag=ADDR alias on byte-address bit A19` (failbits=0xFFFFFFFF, 4 lane 모두), `[EVT1-11] => FAIL diag=ROW_CELL bank=2 row=33 hammer-only`.

EVT1-07 을 마진 lane 의 seed 만 바꿔 다시 돌린 결과 (실제):

```text
  EVT1-07 re-run with 3 marginal-lane seeds:
    seed=0x1000 errors=76 failbits=0x00220000 diag=LANE_MARGIN lane=2
    seed=0x104D errors=97 failbits=0x00220000 diag=LANE_MARGIN lane=2
    seed=0x109A errors=80 failbits=0x00220000 diag=LANE_MARGIN lane=2
```

간헐 오류는 **건수가 아니라 모양** 으로 판단한다: 76, 97, 80 건으로 흔들려도 lane2 의 DQ17/DQ21, 양방향(0→1 과 1→0 둘 다), 여러 bank·row 에 흩어짐 — 이 세 가지가 매번 같다. 양방향 + 흩어짐 = 셀이 아니라 **타이밍 마진**. hammer 에서 0건인 이유도 맵이 설명한다: 마진 오류는 lane 바이트가 4비트 이상 토글할 때만 나는데, hammer 의 aggressor 두 행은 stripe 데이터가 같아 토글이 거의 없다.

리포트가 이끄는 다음 행동:

| 진단 | 근거 (맵) | 다음 행동 |
|---|---|---|
| PIN DQ13 stuck-at-0 | 1비트, 1→0 만, 전 패턴·전 row, training lane1 lane_err | X-ray/단면으로 볼·트레이스 확인, 다른 보드 같은 비트인지 (같으면 설계/패키지), DQ swizzle 표로 DRAM 핀 이름 변환 (연습 5) |
| ADDR A19 | own-addr diff=0x00080000 고정, word 전체 오류 | 주소/명령선 A? 넷 (컨트롤러 매핑에서 row bit 4 에 해당하는 핀) 연속성·short 확인 (LPDDR4/DDR5 라면 CA/command bus training 결과도) |
| LANE_MARGIN lane2 | lane2 의 2비트, 양방향, 흩어짐, training RD WARN 234 mUI | 2D(Vref×delay) 재training (연습 1), DQS2/DQ16-23 프로빙, 같은 lane 이 다른 보드에서도 좁은지 → 레이아웃 vs 보드 개체 |
| ROW_CELL b2/r33 | 한 row, hammer 에서만 | refresh 2x 모드로 재현 여부, 다른 온도에서 재현, DRAM 벤더 문의·TRR/RFM 설정 확인, 해당 row repair(PPR) 가능 여부 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 40/40 passed, exit 0 (약 1.2 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san
# ASan/UBSan: 경고 없음, 40/40 passed (약 1.8 s)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| training done 비트만 보고 PASS | done=1 이어도 eye 가 0.2 UI 일 수 있다 | width·margin 을 숫자로 판정 |
| 폭만 보고 margin 을 안 봄 | 창은 넓은데 delay 가 끝에 붙은 centering 버그를 놓침 (Part A 2번째 줄) | `margin = min(dly-L, R-dly)` |
| rank 별로만 판정 | 각 rank OK 인데 교집합이 없으면 공유 delay 로는 동작 불가 | `eff = win_intersect(eff, w)` |
| 잘린 창(L=0)을 WARN/OK 로 처리 | 폭은 하한값, 중심은 틀림 — 원인이 SI 가 아니라 sweep 범위 | INCONC 로 분리하고 sweep 범위 조정 |
| rank 1개 보드에서 rank1 레지스터 읽음 | 리셋값/쓰레기 값이 FAIL 을 만든다 | `TSTAT[16]` 로 rank 수 결정 |
| LFSR 이 주소 0 을 덮는다고 가정 | 0 은 고정점 — word 0 이 테스트에서 빠진다 | word 0 을 따로 처리 + 커버리지 CHECK |
| 비원시 다항식/잘못된 탭 | 주기가 짧아 메모리 일부만 돈다 (연습 3: x⁴+x²+1 은 주기 6) | 탭 표 + 주기·커버 CHECK |
| 기대값을 두 번째 버퍼에 저장 | 테스트할 DRAM 의 절반을 기대값이 차지 | seed 재생성 |
| checkerboard 로 주소 테스트 | 같은 값을 여러 주소에 써서 alias 가 안 보임 (측정 0) | own-address / random 데이터 |
| hammer 를 같은 행 반복 읽기로 구현 | row hit 라 ACT 0 번 | 같은 bank 의 다른 행과 번갈아 |
| memtest 를 캐시 켠 채, `volatile` 없이 | 캐시나 컴파일러가 읽기를 흡수 (demo 에서 98.73 GB/s 로 나옴) | non-cacheable/`volatile`, 버퍼 ≫ LLC |
| 1비트 오류 = 핀 결함으로 단정 | 셀 1개 결함도 1비트 | 오류 row 개수(흩어짐)로 구별 |

## 8. 면접 Q&A

<details>
<summary>Q1. DDR bring-up 순서와 training 단계가 각각 무엇을 맞추는지 설명해 보세요.</summary>

전원·클럭·리셋 → PHY/컨트롤러 초기화 → DRAM 초기화 시퀀스(MR 설정) → ZQ calibration → write leveling → read DQS gate → read eye centering → write eye centering → (DDR4) VrefDQ training → 메모리 테스트. write leveling 은 fly-by 로 CK 가 칩마다 늦게 도착하는 것을 보정해 **DRAM 에서 DQS 가 CK 와 맞게** 하고, read/write centering 은 각 lane 의 **DQ 샘플 시점을 eye 중앙** 에 놓는다. **꼬리질문: "write leveling 과 write centering 의 차이?"** → leveling 은 DQS ↔ CK (클럭 도메인 정렬), centering 은 DQ ↔ DQS (데이터 eye 중앙). 둘 다 해야 쓰기가 마진을 갖는다.

</details>

<details>
<summary>Q2. Training 이 PASS 했는데 무엇을 더 보나요?</summary>

lane 별 eye 폭과 centering margin(UI 단위), rank 교집합, 창이 delay line 끝에 닿았는지, lane 간 편차(한 lane 만 좁으면 그 lane 의 SI/레이아웃), 보드 간·부팅 간 편차(연습 4: 중심이 매번 흔들리면 불안정), 온도/전압 corner 에서의 변화. training 은 **그 순간 그 조건** 의 결과다. 그래서 margin 이 작은 lane 은 shmoo(SIV-M07)와 PVT 매트릭스(SIV-M08)로 다시 본다. **꼬리질문: "margin 을 일부러 줄여 테스트하는 방법은?"** → delay 를 ±k tap 옮기거나 Vref 를 옮겨 놓고 stress 를 돌려 PASS 범위를 잰다 (margining) — 실제 창 폭을 운영 중에 측정하는 방법.

</details>

<details>
<summary>Q3. memtest 패턴은 왜 여러 개인가요? random 하나로 충분하지 않나요?</summary>

결함마다 노출 조건이 다르다 (측정 매트릭스). 주소 alias 는 **모든 주소가 서로 다른 값** 이어야 보이고(own-address/random), crosstalk 은 **이웃 비트 반대 토글** 이 필요하며(checkerboard 가 결정적으로, random 은 확률 1/32 로), RowHammer 는 **한 행에 ACT 를 몰아주는** 접근 패턴이 필요하다(random 은 흩어져서 못 감). random 은 넓게 잡지만 최악 조건을 보장하지 않는다. **꼬리질문: "own-address 와 random 은 둘 다 주소 결함을 잡는데 왜 둘 다?"** → own-address 는 diff 가 곧 주소 비트라 **진단** 이 즉시 되고, random 은 버스·타이밍 스트레스가 크다.

</details>

<details>
<summary>Q4. RowHammer 를 설명하고, 검증에서 어떻게 확인하고, 어떻게 완화하나요?</summary>

한 행을 반복 활성화(ACT)하면 이웃 행 셀의 전하가 교란되어 refresh 전에 비트가 뒤집힌다. 검증: victim 행에 aggressor 와 반대 데이터를 두고, 같은 bank 의 양쪽 행을 번갈아 열어 refresh 창 안에 ACT 를 최대한 몰아준 뒤 victim 을 읽는다 (MemTest86 의 Hammer test 도 같은 발상). 완화: refresh 주기 단축(연습 2: refresh 2배면 이 모델에서 뒤집힘 0), DRAM 내부 TRR, DDR5/LPDDR5 의 RFM(Refresh Management), ECC, 컨트롤러 쪽 ACT 카운팅. **꼬리질문: "순차 memtest 로는 왜 안 잡히나?"** → 순차는 row 하나를 다 읽고 다음 row 로 가서 row 당 ACT 가 1번이다. **꼬리 2: "양쪽 이웃만 보면 되나?"** → 거리 2 행이 영향받는 Half-Double 같은 보고가 있어 완화 설계는 더 넓게 본다.

</details>

<details>
<summary>Q5. 오류가 특정 비트에서만 납니다. 어떻게 원인을 좁히나요?</summary>

오류 맵 4축: ① 비트 수 — 1비트 vs 한 lane 의 여러 비트 vs 전 lane. ② 방향 — 1→0 만이면 stuck-at-0 같은 고정, 양방향이면 타이밍. ③ 공간 — 모든 row 에 흩어지면 버스(핀/lane), 한 row 에 몰리면 셀. ④ 패턴 — 모든 패턴이면 고정 결함, checkerboard/random 에서만이면 데이터 의존, hammer 에서만이면 disturb. 그다음 training 결과와 맞춰 본다. **꼬리질문: "DQ13 이라는데 보드팀은 그 핀이 없다고 해요."** → 보드는 byte lane 안에서 DQ 순서를 바꿔 라우팅(DQ swizzle)하는 일이 흔하다. 컨트롤러 비트 번호를 swizzle 표로 DRAM 핀/넷 이름으로 바꿔서 전달한다 (연습 5).

</details>

<details>
<summary>Q6. 야간 회귀 8시간에 비트 오류 1건. 다음에 무엇을 하나요?</summary>

① 버리지 않는다 — 1건의 위치(lane/bit/row/주소), 패턴, 온도, 시간을 기록. ② 같은 보드로 seed 를 바꿔 반복해 **모양** 이 재현되는지 본다 (Part C: 건수는 76~97 로 흔들려도 lane·비트는 같다). ③ margin 을 일부러 줄여(delay/Vref offset, 온도 올림) 오류율을 키워 재현 시간을 줄인다. ④ 다른 보드 같은 위치인지 확인 → 설계 vs 개체. ⑤ ECC 시스템이면 CE 카운터·주소 로그로 같은 위치가 반복되는지. **꼬리질문: "그럼 몇 시간 돌려야 PASS 라고 할 수 있나?"** → 오류 0건에서 오류율 상한을 신뢰도로 말해야 한다: 95% 신뢰로 율 p 이하를 주장하려면 약 3/p 번 시도 — 유도와 시간 계산은 SIV-M06.

</details>

<details>
<summary>Q7. ECC 가 켜진 시스템에서 메모리 테스트는 어떻게 하나요? 캐시는요?</summary>

ECC 는 1비트 오류를 조용히 고친다 → 테스트는 PASS 처럼 보인다. 테스트 중 corrected/uncorrected 카운터와 오류 주소 레지스터를 반드시 읽고(CE 가 1이라도 늘면 FAIL 후보), 가능하면 ECC bypass 로 raw 를 본다. ECC 동작 자체의 검증은 오류 주입(SIV-M14). 캐시: 캐시를 켠 채 작은 버퍼를 테스트하면 DRAM 이 아니라 캐시를 테스트한다. non-cacheable 매핑을 쓰거나 버퍼를 LLC 보다 훨씬 크게, 그리고 `volatile` 접근 — demo 에서 `volatile` 없이 재면 컴파일러가 읽기를 지워 98.73 GB/s 라는 가짜 숫자가 나왔다. **꼬리질문: "테스트 코드와 스택이 DRAM 에 있으면?"** → 그 영역은 테스트 못 한다. SRAM/TCM 에서 실행하거나(SIV-B07 Q5 와 같은 문제), 영역을 나눠 테스트한다.

</details>

## 9. 추가 연습

### 연습 1 — 2D eye: 최적 Vref 고르기

Vref(0..31) × delay(0..63) pass/fail 격자(타원 eye 모델)에서 Vref 마다 가장 긴 연속 pass 폭을 구하고, 최대 폭인 Vref 가 여러 개면 그 plateau 의 **중앙** 을 골라라. 그 Vref 의 창 중심에서 eye 높이(연속 pass Vref 수)도 구하고, 기본 Vref=12 에서의 폭과 비교하라.

<details>
<summary>답 보기</summary>

실제 출력: `[1] 2D eye: max width=27 taps on Vref 15..21 -> pick Vref=18 [17,43] center=30 height=17 steps; default Vref=12 width=21 [20,40]`.
첫 구현은 "처음 만난 최대" 를 골라 Vref=15 를 냈다 — plateau 의 끝이라 Vref 방향 마진이 3 step 밖에 없다. 폭이 같으면 plateau 중앙(18)이 Vref 방향으로 가장 안전하다. 1D training 을 잘못된 Vref 에서 하면 폭이 21 tap 으로 22% 작게 보인다 — DDR4 가 VrefDQ training 을 두는 이유.

</details>

### 연습 2 — 한쪽 vs 양쪽 hammer, refresh 2배

bank 하나짜리 축소 모델(임계 600)에서 뒤집힘이 처음 생기는 aggressor 당 최소 반복 수 n 을 양쪽/한쪽 hammer 로 구하고, refresh 간격을 500 / 1000 접근으로 줄였을 때 양쪽 hammer 의 결과를 비교하라. (한쪽 hammer 는 같은 행만 반복하면 row hit 이므로 먼 dummy 행과 번갈아 연다.)

<details>
<summary>답 보기</summary>

실제 출력: `[2] hammer TH=600: min n for flip  double=300 single=600  | refresh/500 acc: double=0  refresh/1000 acc: double=300` (0 = n ≤ 5000 에서 안 뒤집힘).
양쪽 hammer 는 victim 이 접근마다 교란되어 절반의 반복으로 임계에 간다. refresh 창이 임계보다 짧으면(500 < 600) 아무리 오래 두드려도 안 뒤집힌다 — DDR4 가 고온에서 refresh 를 2배로 하는 것, 그리고 TRR/RFM 이 "위험한 행만 일찍 refresh" 하는 것의 원리.

</details>

### 연습 3 — LFSR 탭 표 검증

4~20비트 Galois LFSR 탭 표(XAPP052)를 마스크로 바꾸고 각각 주기가 2ⁿ-1 인지, word 0 을 더하면 0..2ⁿ-1 을 정확히 한 번씩 덮는지 확인하라. 비원시 다항식 x⁴+x²+1 도 넣어 검출되는지 보라.

<details>
<summary>답 보기</summary>

실제 출력:

```text
[3] n= 4 mask=0x0000C period=15 covers=yes
[3] n= 8 mask=0x000B8 period=255 covers=yes
[3] n=16 mask=0x0D008 period=65535 covers=yes
[3] n=20 mask=0x90000 period=1048575 covers=yes
[3] non-primitive x^4+x^2+1: period=6 covers=NO
```

17개 모두 통과(CHECK). x⁴+x²+1 = (x²+x+1)² 라 원시가 아니어서 주기 6 — 16 word 중 7개(0 포함)만 테스트한다. 탭을 손으로 옮겨 적다 틀리는 일이 흔하므로 주기·커버 CHECK 를 테스트 자체에 넣어 둔다.

</details>

### 연습 4 — training 반복 안정성

training 을 8번 반복(lane 별 경계 지터: lane0~2 ±1 tap, lane3 ±4 tap)해 lane 별 중심의 최소~최대(spread)와 최악 폭을 구하고, spread > 3 tap 인 lane 을 UNSTABLE 로 표시하라.

<details>
<summary>답 보기</summary>

실제 출력:

```text
[4] lane0 centers 36..38 spread=2 worst_width=28 ok
[4] lane1 centers 39..41 spread=2 worst_width=28 ok
[4] lane2 centers 37..38 spread=1 worst_width=29 ok
[4] lane3 centers 41..45 spread=4 worst_width=27 UNSTABLE
```

한 번의 training 결과만 보면 lane3 도 폭 27~30 tap 으로 OK 다. 부팅마다 중심이 4 tap(62 mUI) 흔들린다는 건 DQS3 쪽 지터/노이즈나 training 알고리즘의 경계 판정이 불안정하다는 뜻 — "가끔 부팅 후 메모리 오류" 의 전형적 원인이다. 리포트에는 평균이 아니라 **최악 폭** 을 쓴다.

</details>

### 연습 5 — DQ swizzle 로 핀 이름 바꾸기

lane2(컨트롤러 DQ16-23) → 칩 U2 의 DQL0-7 swizzle 표 `{3,1,0,2,6,7,5,4}` 가 순열인지 확인하고, Part C 의 failbits `0x00220000` 을 DRAM 핀 이름으로 바꿔라.

<details>
<summary>답 보기</summary>

실제 출력: `[5] failbits=0x00220000 -> DQ17->U2.DQL1 DQ21->U2.DQL7`.
컨트롤러의 DQ21 은 칩에서는 DQL7 이다. 보드팀·DRAM 벤더와 이야기할 때는 반드시 **상대의 이름** 으로 말한다. 표가 순열이 아니면(같은 핀 두 번) 그 자체가 설계 데이터 오류다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 경고 없음
```

## 10. 핵심 정리

- training 결과는 **숫자로** 판정: 폭 = (R-L)·1000/taps_per_UI mUI, margin = min(dly-L, R-dly). FAIL > INCONC(창이 delay line 끝에 닿음) > WARN > OK.
- delay 를 공유하는 rank 는 **교집합** 으로 본다. 폭이 넓어도 delay 가 끝에 붙으면 WARN.
- 패턴마다 잡는 결함이 다르다 (측정): 주소 alias = own-address/random 만, crosstalk = checkerboard(거의 매번)·random(1/32), RowHammer = hammer 만.
- LFSR 주소는 0 을 못 간다 → 따로 처리. 기대값은 seed 로 재생성, 주기·커버는 CHECK.
- hammer = 같은 bank 의 **다른 행** 을 번갈아 (ACT). 순차·random 은 refresh 창 안에서 임계에 못 간다.
- 오류 맵 4축: 비트 수 · 방향 · 공간(row 흩어짐) · 패턴. 1비트라도 한 row 면 셀, 흩어지면 핀.
- 간헐 오류는 건수가 아니라 **모양** 이 재현되는지 본다. training 과 stress 가 같은 lane 을 가리키면 확신.
- memtest 는 `volatile`/non-cacheable, ECC 카운터 확인, 테스트 코드는 테스트 영역 밖.

## 참고자료

- JEDEC JESD79-4 *DDR4 SDRAM* — write leveling, MPR, VrefDQ training(MR6), refresh(tREFI, 고온 2x refresh)
- JEDEC JESD209-4 *LPDDR4* / JESD79-5 *DDR5 SDRAM* — command bus training, RFM(Refresh Management)
- Y. Kim et al., "Flipping Bits in Memory Without Accessing Them: An Experimental Study of DRAM Disturbance Errors", ISCA 2014
- J. S. Kim et al., "Revisiting RowHammer: An Experimental Analysis of Modern DRAM Devices and Mitigation Techniques", ISCA 2020
- [Google Security Blog — Half-Double: Next-Row-Over Assisted Rowhammer (2021)](https://security.googleblog.com/2021/05/introducing-half-double-new-hammering.html)
- [memtester](https://pyropus.ca./software/memtester/) · [stressapptest](https://github.com/stressapptest/stressapptest) · [MemTest86 test descriptions](https://www.memtest86.com/tech_individual-test-descr.html)
- Xilinx XAPP052, "Efficient Shift Registers, LFSR Counters, and Long Pseudo-Random Sequence Generators" — LFSR 탭 표
- 관련 레슨: SIV-B02 (bus ops / mock), SIV-B05 (walking 1s/0s), SIV-B06 (주소 aliasing), SIV-B07 (March C-, data background), SIV-B14 (리포트), SIV-M06 (BER 신뢰구간), SIV-M07 (shmoo), SIV-M08 (PVT), SIV-M14 (ECC 주입), MEM 트랙
