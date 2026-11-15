---
id: ARCH-M02
time: 90분
updated: 2026-11-11
---
# ARCH-M02 포워딩·로드-유즈 해저드·분기 예측기

## 학습 목표

- 5단 파이프라인에 **EX/MEM→EX, MEM/WB→EX 포워딩**을 넣고, 생산자–소비자 거리 1/2/3이 각각 어느 경로(EX/MEM / MEM/WB / split-phase 레지스터 파일)로 풀리는지 사이클 다이어그램으로 보여줄 수 있다.
- 포워딩이 있어도 **load-use는 1사이클 스톨**이 남는 이유를 "데이터가 언제 생기나"로 설명하고, 컴파일러 스케줄링이나 MEM→MEM 경로로 줄이는 방법을 숫자로 비교한다.
- **2비트 포화 카운터 예측기**(16 엔트리, PC & 15, ID 조회)를 구현하고, 히스테리시스 덕분에 이중 루프의 안쪽 루프 탈출에서 바깥 반복마다 **한 번만** 틀린다는 것을 확인한다 (1비트는 두 번).
- 벤치 4개의 CPI를 {포워딩 없음, 있음} × {정적 not-taken, 2비트} 표로 만들고, 시뮬레이터 없이 **손계산 식 모델**로 같은 사이클 수를 내서 서로 대조한다.
- 실제 코어와 연결한다: Cortex-M3/M4(3단, 동적 예측기 없음)의 분기 비용, Cortex-M7의 분기 예측, 컴파일러가 Cortex-M용으로 load 뒤에 독립 명령을 끼워 넣는 실제 asm.

## 1. 문제

"이 루프 CPI가 왜 1.5인가요? 포워딩을 넣으면 얼마나 줄죠? 2비트 예측기로 바꾸면요?" — CPU·SoC 설계 면접(NVIDIA, Apple, 실리콘 검증)과 펌웨어 성능 면접에서 계속 나오는 질문이다. 사이클 단위로 직접 계산할 수 있어야 하고, 검증 쪽에서는 "성능 모델(식)과 사이클 모델이 일치하는가"를 확인하는 일이 그대로 업무다.

이 레슨은 **ARCH-M01 기준선을 축소해 넣었다** (M01과 같은 규칙, 코드는 self-contained). 기준선 규칙:

| 항목 | 규칙 |
|---|---|
| ISA | ARCH-B08/B09 그대로: 16비트, r0~r7 (r0 = 0 고정), imem/dmem 각 256 워드, `0x0000` = `ADD r0,r0,r0` = NOP |
| 단계 | IF ID EX MEM WB, 단계마다 명령 하나, in-order |
| 레지스터 파일 | **split-phase**: WB가 사이클 앞 절반에 쓰고 ID가 뒤 절반에 읽는다 → 같은 사이클에 보인다 |
| 결정 시점 | 레지스터 읽기 = ID, ALU·**BEQ 비교·BEQ/JAL 목표** = EX, dmem = MEM, 레지스터 쓰기 = WB (JAL의 rd = PC+1도 WB) |
| M01 데이터 해저드 | 포워딩 없음. ID에서 소비자를 붙잡고(IF/ID 유지, EX에 거품) 생산자가 WB에 갈 때까지 기다림 → 거리 1 = 2사이클, 거리 2 = 1, 거리 ≥3 = 0. r0은 해저드가 아님 |
| M01 제어 | 정적 not-taken. taken BEQ나 JAL은 EX에서 결정 → IF·ID 두 명령 플러시 → 2사이클 |
| HALT | EX에 오면(앞선 분기가 전부 결정된 시점) IF/ID 플러시, fetch 중지. HALT가 WB에 있는 사이클에 끝 |
| 사이클 | 사이클 1 = PC 0 첫 fetch. 총 사이클 = HALT가 WB에 있는 사이클 번호. retired는 HALT 포함. 해저드 없으면 N개 = N + 4. CPI = cycles / retired |
| 추측 fetch | PC ≥ 256이면 NOP. 테스트 프로그램은 폴트 없음 (파이프라인 예외 = ARCH-M10) |

M02는 이 위에 설정 플래그 두 개를 얹는다. `fwd = 0, bp = NT`이면 M01과 **사이클 수까지 똑같아야** 한다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m02-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">포워딩 경로와 예측기 위치 (이 레슨의 모델)</text>
  <rect x="20" y="110" width="80" height="90" rx="4" class="f-mut" stroke="currentColor"/><text x="60" y="150" text-anchor="middle" fill="currentColor" font-weight="bold">IF</text><text x="60" y="170" text-anchor="middle" fill="currentColor" font-size="12">PC, imem</text>
  <rect x="112" y="100" width="10" height="110" class="f-acc" stroke="currentColor"/>
  <rect x="134" y="110" width="130" height="90" rx="4" class="f-mut" stroke="currentColor"/><text x="199" y="130" text-anchor="middle" fill="currentColor" font-weight="bold">ID</text><text x="199" y="150" text-anchor="middle" fill="currentColor" font-size="12">regfile 읽기</text><text x="199" y="168" text-anchor="middle" fill="currentColor" font-size="12">hazard unit</text>
  <text x="199" y="186" text-anchor="middle" fill="currentColor" font-size="12">2BIT 표 (PC &amp; 15)</text>
  <rect x="276" y="100" width="10" height="110" class="f-acc" stroke="currentColor"/>
  <rect x="298" y="110" width="170" height="90" rx="4" class="f-mut" stroke="currentColor"/><text x="383" y="128" text-anchor="middle" fill="currentColor" font-weight="bold">EX</text>
  <rect x="310" y="138" width="44" height="22" class="f-warn" stroke="currentColor"/><text x="332" y="153" text-anchor="middle" fill="currentColor" font-size="12">MUX a</text>
  <rect x="310" y="168" width="44" height="22" class="f-warn" stroke="currentColor"/><text x="332" y="183" text-anchor="middle" fill="currentColor" font-size="12">MUX b</text>
  <rect x="372" y="140" width="84" height="48" rx="4" fill="none" stroke="currentColor"/><text x="414" y="160" text-anchor="middle" fill="currentColor" font-size="12">ALU, ==</text><text x="414" y="178" text-anchor="middle" fill="currentColor" font-size="12">분기 결정</text>
  <rect x="480" y="100" width="10" height="110" class="f-acc" stroke="currentColor"/><text x="485" y="224" text-anchor="middle" fill="currentColor" font-size="12">EX/MEM</text>
  <rect x="502" y="110" width="90" height="90" rx="4" class="f-mut" stroke="currentColor"/><text x="547" y="150" text-anchor="middle" fill="currentColor" font-weight="bold">MEM</text><text x="547" y="170" text-anchor="middle" fill="currentColor" font-size="12">dmem</text>
  <rect x="604" y="100" width="10" height="110" class="f-acc" stroke="currentColor"/><text x="609" y="224" text-anchor="middle" fill="currentColor" font-size="12">MEM/WB</text>
  <rect x="626" y="110" width="90" height="90" rx="4" class="f-mut" stroke="currentColor"/><text x="671" y="150" text-anchor="middle" fill="currentColor" font-weight="bold">WB</text><text x="671" y="170" text-anchor="middle" fill="currentColor" font-size="12">reg 쓰기</text>
  <g stroke="currentColor" fill="none">
    <path d="M485 100 L485 66 L320 66 L320 136" marker-end="url(#m02-a)"/>
    <path d="M609 100 L609 42 L344 42 L344 136" marker-end="url(#m02-a)"/>
    <path d="M671 200 L671 236 L199 236 L199 202" marker-end="url(#m02-a)"/>
    <path d="M160 110 L160 82 L60 82 L60 108" marker-end="url(#m02-a)" stroke-dasharray="4 3"/>
    <path d="M414 200 L414 270 L40 270 L40 202" marker-end="url(#m02-a)" stroke-dasharray="4 3"/>
  </g>
  <text x="360" y="60" fill="currentColor" font-size="12">EX/MEM → EX (거리 1, LD 제외)</text>
  <text x="360" y="36" fill="currentColor" font-size="12">MEM/WB → EX (거리 2, LD 값 포함)</text>
  <text x="430" y="252" fill="currentColor" font-size="12">거리 3: split-phase 레지스터 파일</text>
  <text x="66" y="78" fill="currentColor" font-size="12">예측 taken / JAL: ID에서 전환, 1사이클</text>
  <text x="60" y="290" fill="currentColor" font-size="12">EX에서 오예측 발견 (NT의 taken 포함): IF·ID 플러시 후 올바른 PC, 2사이클</text>
  <text x="20" y="312" fill="currentColor" font-size="12">LD 값은 MEM 끝에야 생긴다 → 바로 다음 명령의 EX 시작에 못 댄다 = load-use 1사이클 스톨</text>
</svg>
```

체크 테스트는 **결정적인 것만** 본다: 사이클 수, 스톨/플러시 수, 포워딩 경로별 사용 횟수, 예측 실패 위치, 그리고 모든 실행에서 **아키텍처 상태(r0–r7, dmem, retired)가 단일 사이클 기준 모델과 같은지**. 시간 측정은 없다 (실제 코어 관찰은 `demo_sched.c`의 asm만).

### Part A — 포워딩 MUX

- `fwd_src(p, r)` — EX에 있는 명령이 레지스터 r을 읽을 때 값의 출처: `FWD_EXMEM`(바로 앞 명령, 지금 MEM) / `FWD_MEMWB`(두 칸 앞, 지금 WB) / `FWD_NONE`. 둘 다 r을 쓰면 **더 젊은 쪽**이 이긴다. MEM에 있는 LD는 아직 값이 없으니 EX/MEM에서 넘겨주면 안 된다.
- `hazard_stall(p, d)`의 포워딩 ON 분기 — 무엇이 남는가 (M01 분기는 주어짐).
- 테스트: 거리 1 체인(12 → 8사이클), 거리 1/2/3 경로, 생산자 두 개, BEQ 비교 입력, ST 베이스+데이터, JAL 링크를 읽는 명령, r0.

### Part B — load-use와 2비트 예측기

- `hazard_stall` 포워딩 ON: 바로 앞이 LD이고 그 rd를 읽으면 1사이클 (ALU 입력, BEQ, ST 데이터, LD 베이스 모두).
- `ctr2_next(c, taken)`, `ctr2_taken(c)` — 0 강한 NT, 1 약한 NT, 2 약한 T, 3 강한 T. 초기값 1.
- `ex_redirect(pol, d, pc, pred_taken, taken, &npc)` — EX에서 fetch를 바로잡아야 하나. NT는 M01 그대로(주어짐), 2BIT는 TODO.
- 테스트: 카운터 전이표 8칸, JAL 1사이클, "예측 taken인데 not-taken"이 2사이클, 이중 루프 M=4 K=5의 PC별 오예측, **무작위 프로그램 2000개 × 설정 4개**를 기준 모델과 대조.

### Part C — 벤치 CPI 표와 손계산 식 모델

- 벤치 4개: 배열 합(load-use), x = 10x + 7 체인(거리 1 RAW), 키 개수 세기(데이터 의존 BEQ) 무작위/정렬.
- `eq_cycles(tr, n, cfg)` — 시뮬레이터 없이 은퇴 명령 흐름만으로 사이클·스톨·플러시·오예측을 계산하는 식 모델. 벤치 전부와 무작위 프로그램 1000개 × 4설정에서 사이클 시뮬레이터와 **완전히 같아야** 한다.
- M01 공식(배열 합 `10N + 12`, 체인 `18K + 13` 등)을 손으로 유도해서 `fwd=0, bp=NT` 결과와 대조.

## 2. Problem (English)

You are given a cycle-level model of a classic 5-stage in-order pipeline (IF ID EX MEM WB) for a 16-bit toy ISA (ADD, ADDI, LD, ST, BEQ, JAL, HALT; 8 registers, r0 = 0). The baseline (ARCH-M01) has no forwarding: the ID-stage hazard unit stalls a consumer until its producer reaches WB (split-phase register file), and branches/JAL are resolved in EX with static not-taken fetch (2-cycle penalty). Extend it with configuration flags:

**Part A.** Implement `int fwd_src(const pipe_t *p, uint8_t r)` returning which pipeline latch feeds the EX operand for register `r`: EX/MEM (producer one ahead; never a load), MEM/WB (producer two ahead, including load data), or none. The youngest producer wins; r0 is never forwarded.

**Part B.** With forwarding on, the only remaining data stall is load-use: a consumer immediately after `LD rd` that reads `rd` stalls exactly 1 cycle. Implement the 2-bit saturating counter (`ctr2_next`, `ctr2_taken`; 16 entries indexed by `PC & 15`, initialized to weakly-not-taken). The predictor is read in ID: predicted-taken BEQ and every JAL redirect fetch from ID (1-cycle bubble). Implement `ex_redirect` so that a BEQ whose EX outcome differs from the fetched direction flushes IF and ID and refetches the correct PC (2 cycles). Final architectural state must equal a single-cycle reference ISS for every program in every configuration.

**Part C.** Implement `eqres_t eq_cycles(const dyn_t *tr, size_t n, cfg_t cfg)`: given only the retired instruction stream, compute total cycles, stall cycles, flushed (control-bubble) cycles and mispredictions without simulating the pipeline. It must agree exactly with the cycle-level model.

Example: `ADDI r1,r0,5; ADD r2,r1,r1; ADD r3,r2,r1; HALT` takes 12 cycles without forwarding (4 stalls) and 8 with forwarding. `LD r1,0(r0); ADD r2,r1,r1; HALT` takes 8 cycles with forwarding (1 stall). Array sum over N elements: `10N+12` cycles (no forwarding, NT) vs `8N+13` (forwarding, 2-bit, N ≥ 1).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-M02
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
mkdir -p /tmp/esn_ARCH-M02
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/esn_ARCH-M02/st && /tmp/esn_ARCH-M02/st; echo "exit $?"
```

실제 출력 (발췌):

```text
-- Part A: forwarding --
      [chain, Fwd+NT]
      cycles=8 retired=4 CPI=2.000 stalls=0 flushes=0
FAIL  p->cycles == 8 && p->stalls == 0 && p->reg[2] == 10 && p->reg[3] == 15  (line 517)
FAIL  p->fwd_used[FWD_EXMEM] == 3 && p->fwd_used[FWD_MEMWB] == 1  (line 518)
...
-- Part B: load-use, 2-bit predictor, fuzz oracle --
FAIL  ctr2_next(0, false) == 0 && ctr2_next(0, true) == 1 && ctr2_next(1, false) == 0 && ctr2_next(1, true) == 2  (line 611)
...
      fuzz: 8000 runs, 4928 mismatches
FAIL  bad == 0  (line 704)
-- Part C: benchmarks, M01 formulas, equation model --
FAIL  eq_ok  (line 771)
...
      eq model vs cycle sim: 4000 random runs, 4000 mismatches
FAIL  bad == 0  (line 856)
11/40 passed
exit 1
```

M01 기준선 쪽 테스트(포워딩 없는 체인 12사이클, r0, BEQ 스톨 2)는 starter에서도 통과한다. 재미있는 것은 `[chain, Fwd+NT]`: `hazard_stall`의 포워딩 분기가 비어 있어서 **스톨 없이 8사이클에 끝나지만 r2/r3 값이 틀린다** — 빠르고 틀린 하드웨어. 퍼징이 이걸 8000번 중 4928번 잡는다.

## 4. 힌트

<details><summary>힌트 1 — 래치에 "지금 무엇이 있나"로 생각하기</summary>

EX에 명령 k가 있을 때, `p->s[S_MEM]`(EX/MEM 래치)에는 k−1이, `p->s[S_WB]`(MEM/WB 래치)에는 k−2가 있다. 각 래치에는 `wb`(rd ≠ r0에 쓰나), `d.rd`, `res`가 있다. EX/MEM의 `res`는 EX 결과(LD라면 **주소**), MEM/WB의 `res`는 최종 값(LD라면 읽은 값)이다. 검사 순서가 우선순위다.
</details>

<details><summary>힌트 2 — 포워딩이 있으면 무엇이 남나</summary>

거리 1의 ALU 결과는 EX 끝에 생기고, 다음 명령의 EX 시작에 EX/MEM 래치에서 받으면 된다. LD 값은 MEM 끝에 생긴다. 다음 명령은 그 순간 EX를 **시작**해야 하므로 시간을 거슬러야 한다. 그러니 "ID에 있는 소비자가 읽는 레지스터를 지금 EX의 LD가 쓴다"면 한 사이클 붙잡는다. 거리 2 LD는? 소비자가 EX일 때 LD는 WB → MEM/WB 경로.
</details>

<details><summary>힌트 3 — 2비트 카운터는 덧셈 + 포화</summary>

taken이면 +1 (최대 3), not-taken이면 −1 (최소 0). 예측은 상위 비트(≥ 2). 한 번 틀려도 강한 상태(0 또는 3)에서는 약한 상태로만 옮겨 가므로 예측이 바로 뒤집히지 않는다 — 이게 히스테리시스.
</details>

<details><summary>힌트 4 — ex_redirect 2BIT: 언제, 어디로</summary>

JAL은 ID에서 이미 목표로 돌렸으니 EX에서는 할 일이 없다. BEQ는 `pred_taken != taken`일 때만 고친다. 고칠 주소는 실제 방향 쪽: taken이면 목표, not-taken이면 `pc + 1`. 후자를 빠뜨리면 "예측 taken, 실제 not-taken"에서 잘못된 경로로 계속 간다.
</details>

<details><summary>힌트 5 — 식 모델: EX에 들어가는 사이클만 세면 된다</summary>

in-order 파이프라인에서 명령 k의 EX 사이클 `ex[k]`는 세 가지의 max다: (1) 앞 명령 바로 다음 `ex[k-1] + 1 + extra[k-1]` (extra = 앞 명령이 만든 제어 거품), (2) 각 소스 레지스터의 마지막 생산자 j에 대해 no-fwd `ex[j] + 3`, fwd LD `ex[j] + 2`, fwd ALU `ex[j] + 1`. 총 사이클 = `ex[HALT] + 2`. 2BIT 카운터는 프로그램 순서대로 조회·갱신하면 된다 (풀이에서 왜 같은지 설명).
</details>

## 5. 풀이

### Part A — 포워딩: 거리별 경로

| 생산자–소비자 거리 | 소비자가 EX일 때 생산자 위치 | M01 (no-fwd) 스톨 | 포워딩 경로 |
|---|---|---|---|
| 1 (바로 앞) | MEM (EX/MEM 래치) | 2 | **EX/MEM → EX** (LD면 불가 → load-use) |
| 2 | WB (MEM/WB 래치) | 1 | **MEM/WB → EX** |
| ≥ 3 | 이미 끝남 | 0 | 없음 — 소비자가 ID일 때 생산자가 WB였고 split-phase로 읽었다 |

정답은 세 줄이다: `r == 0`이면 NONE, EX/MEM 래치가 r을 쓰고 LD가 아니면 EX/MEM, MEM/WB 래치가 r을 쓰면 MEM/WB.

- **순서가 우선순위다.** `ADDI r1,r0,1; ADDI r1,r0,2; ADD r2,r1,r1`에서 ADD가 EX일 때 EX/MEM에도 MEM/WB에도 r1이 있다. 더 젊은 EX/MEM(값 2)이 이겨야 r2 = 4. 순서를 바꾸면 2가 나온다 — 교과서(P&H 4.7절)의 "double data hazard" 조건이 이것이다.
- **`wb` 플래그에 r0과 "쓰지 않는 명령"이 다 들어 있다.** ST·BEQ·HALT는 rd 필드가 0으로 정규화되어 있고, `ADDI r0,...`는 `wb = false`. 그래서 r0은 포워딩도 스톨도 안 된다 (테스트 `z`).
- **EX/MEM의 LD를 제외하는 이유.** 이 모델에서 EX/MEM 래치의 `res`는 LD의 **주소**다. 제외를 빼먹으면 주소가 값으로 넘어간다. 정상이라면 load-use 스톨이 이 상황을 아예 막으므로 이 줄은 "도달하지 않음"의 표시이기도 하다.
- 모든 소스가 같은 MUX를 탄다: ALU 두 입력, BEQ 비교 두 입력, ST 데이터(rt)와 베이스(rs). JAL의 링크(PC+1)는 EX에서 계산되므로 ALU 결과와 똑같이 넘길 수 있다.

실제 출력 — 같은 체인을 두 설정으로:

```text
      [chain, noFwd+NT]
      pc  instruction         1  2  3  4  5  6  7  8  9 10 11 12
        0 ADDI r1, r0, 5     IF ID EX ME WB
        1 ADD  r2, r1, r1       IF ID ID ID EX ME WB
        2 ADD  r3, r2, r1          IF IF IF ID ID ID EX ME WB
        3 HALT                              IF IF IF ID EX ME WB
        4 NOP                                        IF ID xx
        5 NOP                                           IF xx
      cycles=12 retired=4 CPI=3.000 stalls=4 flushes=0
```

```text
      [chain, Fwd+NT]
      pc  instruction         1  2  3  4  5  6  7  8
        0 ADDI r1, r0, 5     IF ID EX ME WB
        1 ADD  r2, r1, r1       IF ID EX ME WB
        2 ADD  r3, r2, r1          IF ID EX ME WB
        3 HALT                        IF ID EX ME WB
        4 NOP                            IF ID xx
        5 NOP                               IF xx
      cycles=8 retired=4 CPI=2.000 stalls=0 flushes=0
```

`ID ID ID`(스톨 2번)와 IF에서 기다리는 다음 명령의 `IF IF IF`가 M01 규칙 "거리 1 = 2사이클"이다. HALT 뒤의 NOP 두 개(`xx`)는 HALT가 EX에서 버린 추측 fetch다 — 사이클은 들지 않는다 (`flushes`에 세지 않고 따로 센다). 포워딩 버전의 경로 사용 횟수: EX/MEM 3번(ADD r2의 두 입력 + ADD r3의 r2), MEM/WB 1번(ADD r3의 r1).

### Part B — load-use: 왜 포워딩으로 못 없애나

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m02-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">LD r1 → ADD r2,r1,r1 : 값이 생기는 순간 vs 필요한 순간</text>
  <g fill="currentColor" font-size="12">
    <text x="190" y="44">1</text><text x="250" y="44">2</text><text x="310" y="44">3</text><text x="370" y="44">4</text>
    <text x="430" y="44">5</text><text x="490" y="44">6</text><text x="550" y="44">7</text><text x="610" y="44">8</text>
  </g>
  <text x="10" y="74" fill="currentColor">LD r1, 0(r0)</text>
  <rect x="170" y="56" width="58" height="26" class="f-mut" stroke="currentColor"/><text x="190" y="74" fill="currentColor">IF</text>
  <rect x="230" y="56" width="58" height="26" class="f-mut" stroke="currentColor"/><text x="250" y="74" fill="currentColor">ID</text>
  <rect x="290" y="56" width="58" height="26" class="f-mut" stroke="currentColor"/><text x="310" y="74" fill="currentColor">EX</text>
  <rect x="350" y="56" width="58" height="26" class="f-acc" stroke="currentColor"/><text x="370" y="74" fill="currentColor">ME</text>
  <rect x="410" y="56" width="58" height="26" class="f-mut" stroke="currentColor"/><text x="430" y="74" fill="currentColor">WB</text><text x="10" y="118" fill="currentColor">ADD (스톨 없다면)</text>
  <rect x="230" y="100" width="58" height="26" class="f-mut" stroke="currentColor"/><text x="250" y="118" fill="currentColor">IF</text>
  <rect x="290" y="100" width="58" height="26" class="f-mut" stroke="currentColor"/><text x="310" y="118" fill="currentColor">ID</text>
  <rect x="350" y="100" width="58" height="26" class="f-warn" stroke="currentColor"/><text x="370" y="118" fill="currentColor">EX</text><text x="420" y="118" fill="currentColor" font-size="12">← 사이클 4 시작에 값이 필요, 값은 4 끝에 생김 ✗</text><text x="10" y="168" fill="currentColor">ADD (1 스톨)</text>
  <rect x="230" y="150" width="58" height="26" class="f-mut" stroke="currentColor"/><text x="250" y="168" fill="currentColor">IF</text>
  <rect x="290" y="150" width="58" height="26" class="f-mut" stroke="currentColor"/><text x="310" y="168" fill="currentColor">ID</text>
  <rect x="350" y="150" width="58" height="26" class="f-warn" stroke="currentColor"/><text x="370" y="168" fill="currentColor">ID</text>
  <rect x="410" y="150" width="58" height="26" class="f-ok" stroke="currentColor"/><text x="430" y="168" fill="currentColor">EX</text>
  <rect x="470" y="150" width="58" height="26" class="f-mut" stroke="currentColor"/><text x="490" y="168" fill="currentColor">ME</text>
  <rect x="530" y="150" width="58" height="26" class="f-mut" stroke="currentColor"/><text x="550" y="168" fill="currentColor">WB</text>
  <path d="M408 84 L408 146" stroke="currentColor" fill="none" marker-end="url(#m02-b)"/>
  <text x="200" y="210" fill="currentColor" font-size="12">MEM/WB → EX: 사이클 4 끝의 값이 사이클 5 시작에 도착 ✓ (스톨 1 = 거품 1개)</text>
</svg>
```

ALU 결과는 EX 끝에 생기므로 바로 다음 명령의 EX 시작(같은 경계)에 넘겨줄 수 있다. LD는 한 단계 늦게(MEM 끝) 생기므로 같은 경계가 한 사이클 뒤로 밀린다. 포워딩은 "기다린 값을 레지스터 파일을 거치지 않고 받는 것"이지 값을 일찍 만드는 게 아니다. 그래서 해저드 유닛에는 이 한 줄이 남는다:

```c
if (ex->v && ex->wb && ex->d.op == OP_LD && ex->d.rd == r) return true;   /* 포워딩 ON */
```

```text
      [load-use, Fwd+NT]
      pc  instruction         1  2  3  4  5  6  7  8
        0 LD   r1, 0(r0)     IF ID EX ME WB
        1 ADD  r2, r1, r1       IF ID ID EX ME WB
        2 HALT                     IF IF ID EX ME WB
        3 NOP                            IF ID xx
        4 NOP                               IF xx
      cycles=8 retired=3 CPI=2.667 stalls=1 flushes=0
```

같은 규칙이 모든 소비자에 적용된다 (테스트로 확인): LD → ST 데이터 1, LD → BEQ 1, LD → LD 베이스(포인터 따라가기, `dmem[dmem[5]]`) 1, LD → 거리 2 사용 0, `LD r0` 다음 r0 사용 0. ST 데이터는 사실 MEM 단계에서야 필요하므로 **MEM→MEM 경로**를 추가하면 없앨 수 있다 (연습 4: memcpy가 10N+11 → 9N+11). 포인터 따라가기는 다음 주소가 방금 읽은 값이라 어떤 경로나 스케줄링으로도 못 숨긴다 — MEM-B10의 포인터 체이싱 측정이 바로 이 지연을 잰다.

#### 2비트 카운터와 제어 비용

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m02-c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">2비트 포화 카운터 (T = taken, N = not-taken), 초기값 01</text>
  <circle cx="110" cy="100" r="36" class="f-mut" stroke="currentColor"/><text x="110" y="96" text-anchor="middle" fill="currentColor">00</text><text x="110" y="114" text-anchor="middle" fill="currentColor" font-size="12">강한 NT</text>
  <circle cx="280" cy="100" r="36" class="f-mut" stroke="currentColor" stroke-width="2.5"/><text x="280" y="96" text-anchor="middle" fill="currentColor">01</text><text x="280" y="114" text-anchor="middle" fill="currentColor" font-size="12">약한 NT</text>
  <circle cx="450" cy="100" r="36" class="f-acc" stroke="currentColor"/><text x="450" y="96" text-anchor="middle" fill="currentColor">10</text><text x="450" y="114" text-anchor="middle" fill="currentColor" font-size="12">약한 T</text>
  <circle cx="620" cy="100" r="36" class="f-acc" stroke="currentColor"/><text x="620" y="96" text-anchor="middle" fill="currentColor">11</text><text x="620" y="114" text-anchor="middle" fill="currentColor" font-size="12">강한 T</text>
  <g stroke="currentColor" fill="none">
    <path d="M140 82 Q195 55 250 82" marker-end="url(#m02-c)"/><path d="M310 82 Q365 55 420 82" marker-end="url(#m02-c)"/><path d="M480 82 Q535 55 590 82" marker-end="url(#m02-c)"/>
    <path d="M250 118 Q195 145 140 118" marker-end="url(#m02-c)"/><path d="M420 118 Q365 145 310 118" marker-end="url(#m02-c)"/><path d="M590 118 Q535 145 480 118" marker-end="url(#m02-c)"/>
    <path d="M80 82 Q50 60 74 92" marker-end="url(#m02-c)"/><path d="M650 82 Q680 60 656 92" marker-end="url(#m02-c)"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="190" y="56">T</text><text x="360" y="56">T</text><text x="530" y="56">T</text>
    <text x="190" y="152">N</text><text x="360" y="152">N</text><text x="530" y="152">N</text>
    <text x="40" y="60">N</text><text x="686" y="60">T</text>
  </g>
  <text x="10" y="186" fill="currentColor" font-size="12">예측 = 상위 비트. 안쪽 루프 탈출(T 한 번)은 00 → 01 로만 옮긴다 → 다음 진입 첫 N 은 여전히 맞춘다</text>
  <text x="10" y="204" fill="currentColor" font-size="12">1비트라면 탈출 T 로 비트가 T 가 되고, 다음 진입 첫 N 에서 또 틀린다 (루프당 2번)</text>
</svg>
```

제어 비용 표 (BEQ 한 번, JAL 한 번당 거품 수):

| 경우 | NT (M01) | 2BIT |
|---|---|---|
| BEQ not-taken, 예측 NT | 0 | 0 |
| BEQ taken, 예측 NT | 2 (EX 플러시) | 2 |
| BEQ taken, 예측 T | — | 1 (ID에서 전환, IF 한 칸 버림) |
| BEQ not-taken, 예측 T | — | 2 (ID 전환 1 + EX에서 되돌림 1) |
| JAL | 2 | 1 |

"예측 T, 실제 NT"가 2인 이유: ID 전환 때 IF의 `pc+1`을 이미 버렸고(거품 1), EX에서 목표 쪽 명령을 버리고 `pc+1`을 다시 가져온다(1 더). 결과적으로 not-taken 쪽 명령이 원래보다 2사이클 늦게 EX에 들어간다. 테스트 `flip`(9명령, 루프 2회)이 이 경우다 — PC 2의 BEQ가 첫 번째엔 taken(01 → 예측 NT, 틀림 2), 두 번째엔 not-taken(10 → 예측 T, 틀림 2). 실제 출력 요약 `cycles=24 retired=13 CPI=1.846 stalls=0 flushes=7`: 플러시 7 = PC 2 두 번(2 + 2) + PC 6 탈출(2) + JAL(1), 항등식 `cycles = retired + 4 + stalls + flushes` = 13 + 4 + 0 + 7 = 24. (다이어그램 24열은 `solution.c` 실행 출력에 있다.)

> **ARCH-M01과 `flushes` 세는 법이 다르다.** 여기서는 버린 사이클 슬롯 수(오예측 2, ID 리다이렉트 1)를 세므로 항등식이 `+ flushes`이고, ARCH-M01은 플러시 *사건* 수(사건마다 2사이클)를 세서 `+ 2 × flushes`이다. 총 사이클은 같다 — 예: 포워딩 끔 + NT에서 배열 합은 두 레슨 모두 `10N+12`.

**이중 루프 히스테리시스** (`prog_nest`, M=4, K=5). 안쪽 탈출 분기(PC 3)는 바깥 반복마다 "N 5번, T 1번":

```text
      nest M=4 K=5 Fwd+2BIT: mispred inner-exit(pc3)=4 outer-exit(pc1)=1 back(pc6)=1 back(pc8)=1 total=7
```

PC 3은 정확히 M = 4번 틀린다: 탈출 T에서 00 → 01, 다음 진입 첫 N은 01 = NT 예측이라 맞고 다시 00. 바깥 탈출(PC 1) 1번, 뒤로 가는 `BEQ r0,r0` 두 개(PC 6, 8)는 첫 실행만 01 → NT로 틀리고 그다음부터 10/11. 합계 M + 3 = 7. NT라면 taken BEQ 전부 = 4 + 20 + 4 + 1 = 29번 2사이클씩이다. 1비트(연습 3)는 PC 3에서 2M − 1 = 7번.

**JAL**은 2BIT에서 항상 ID 전환이라 1사이클 (`JAL r0,+1` 테스트: NT 9사이클 플러시 2 → 2BIT 8사이클 플러시 1, 다이어그램에서 건너뛴 `ADDI r1`은 `IF xx`).

**모델링 결정 두 가지** (식 모델과 맞추려면 명시해야 한다):
1. 2BIT 표는 EX에서 갱신하고 ID에서 조회한다. 한 사이클 안에서 EX를 먼저 평가하므로, 같은 사이클에 ID에 있는 젊은 분기는 갱신된 값을 본다 (레지스터 파일의 split-phase와 같은 규약). 그러면 표 갱신 순서가 **프로그램 순서**와 같아진다. 실제 코어는 보통 retire 때 갱신해서 가까운 분기끼리는 오래된 값을 볼 수 있다 — 그 효과는 이 모델에 없다.
2. 데이터 스톨 중인 분기는 ID를 떠나는 사이클에 예측·전환한다. 스톨 사이클 동안 미리 목표를 가져오면 1사이클을 숨길 수 있지만 그만큼 하드웨어가 복잡해진다.

**정확성 오라클.** 무작위 프로그램(직선 코드 + 앞으로만 가는 BEQ/JAL, HALT로 끝남, LD/ST는 `ADDI rb,r0,imm; LD rx,imm(rb)` 쌍이라 주소가 항상 0..62이고 덤으로 베이스 레지스터 거리 1 RAW가 생긴다) 2000개를 네 설정으로 돌려 기준 모델(ARCH-B09 축소판)과 r0–r7, dmem 전체, retired를 비교했다: `fuzz: 8000 runs, 0 mismatches`. 처음 짠 생성기는 분기 목표를 정한 **뒤에** 쌍을 만들어서, 분기가 쌍의 두 번째 칸(LD)에 떨어져 베이스가 정해지지 않은 프로그램이 나왔다 (172개 불일치 — 기준 모델도 폴트). 종류를 먼저 정하고 분기 오프셋을 2차 패스에서 채우도록 고쳤다. 테스트 생성기의 버그도 오라클이 잡는다.

### Part C — 벤치 CPI 표와 손계산

`solution.c`의 실제 출력 (벤치 데이터 시드 고정, `cycles (CPI)`; 아래는 `stalls/flushes/mispred`):

```text
      program                retired         noFwd+NT           Fwd+NT       noFwd+2BIT         Fwd+2BIT
      sum N=64 (load-use)        390     652 (1.672)     588 (1.508)     589 (1.510)     525 (1.346)
      hash K=40 (ALU chain)      325     733 (2.255)     412 (1.268)     694 (2.135)     373 (1.148)
      count n=200 random        1301    1895 (1.457)    1895 (1.457)    1743 (1.340)    1743 (1.340)
      count n=200 sorted        1301    1895 (1.457)    1895 (1.457)    1606 (1.234)    1606 (1.234)
      stalls / flushes / mispred:
      sum N=64 (load-use)             128/ 130/  65   64/ 130/  65  128/  67/   2   64/  67/   2
      hash K=40 (ALU chain)           322/  82/  41    1/  82/  41  322/  43/   2    1/  43/   2
      count n=200 random                0/ 590/  95    0/ 590/  95    0/ 438/ 100    0/ 438/ 100
      count n=200 sorted                0/ 590/  95    0/ 590/  95    0/ 301/   4    0/ 301/   4
```

| 벤치 | 무엇이 지배하나 | 관찰 |
|---|---|---|
| sum N=64 | load-use(반복당 1) + 뒤로 가는 `BEQ r0,r0`(반복당 1번 taken) | 포워딩: 스톨 128 → 64 (load-use만 남음). 2BIT: 플러시 130 → 67 |
| hash K=40 | 거리 1 RAW 4개/반복 | 포워딩이 거의 전부: 733 → 412. 남은 스톨 1은 프롤로그 `LD r5` → `BEQ r5` |
| count random | 데이터 의존 BEQ (50%) + JAL 200번 | 2BIT 오예측 100 > NT 95. 그래도 2BIT가 빠르다 (아래) |
| count sorted | 같은 코드, 데이터만 정렬 | 2BIT 오예측 4: 진입 1 + 경계 2 + 탈출 1 |

count random을 플러시로 쪼개면: NT 590 = JAL 200 × 2 + taken BEQ 95 × 2. 2BIT 438 = JAL 200 × 1 + 오예측 100 × 2 + 맞춘 taken 38 × 1. **BEQ만 보면 2BIT가 더 비싸다** (탈출 분기 포함, 2·100 + 38 = 238 vs 2·95 = 190): 50% 무작위에서 카운터가 약한 상태 사이를 오가며 대략 절반을 틀리고, 맞춘 taken에도 1사이클을 낸다. 이득은 전부 JAL의 1사이클 전환(200사이클)에서 나온다. 정렬하면 오예측이 4로 떨어진다 — 같은 코드, 데이터만 다르다 (실제 하드웨어 측정은 ARCH-M03). 연습 2의 BTFN(뒤로 가면 taken) 정적 예측은 무작위에서 1695로 2BIT(1743)보다 낫고 정렬에서는 1695로 2BIT(1606)보다 못하다.

#### M01 공식 손으로 유도 (fwd=0, bp=NT이면 M01과 같아야 한다)

배열 합의 반복 하나 (`BEQ@3 LD@4 ADD@5 ADDI@6 ADDI@7 BEQ@8`), `BEQ@3`이 EX에 들어가는 사이클을 T라 하면:

| 명령 | no-fwd NT | fwd NT | 이유 |
|---|---|---|---|
| BEQ r1,r0 (not-taken) | T | T | |
| LD r4,0(r2) | T+1 | T+1 | r2는 지난 반복 ADDI@6, 충분히 멀다 |
| ADD r3,r3,r4 | **T+4** (스톨 2) | **T+3** (스톨 1) | 거리 1 LD: ex[LD]+3 vs +2 |
| ADDI r2 / ADDI r1 | T+5 / T+6 | T+4 / T+5 | |
| BEQ r0,r0 (taken) | T+7 → 다음 +3 | T+6 → 다음 +3 | 플러시 2 |
| 다음 BEQ@3 | T+10 | T+9 | r1(ADDI@7)은 거리 2 + 거품 2라 이미 준비됨 |

반복당 10 / 9사이클. 프롤로그(LD, ADDI, ADD) + 탈출 BEQ(taken, 플러시 2) + ST + HALT = 6명령 + 채움 4 + 플러시 2 = 12. 그래서 **M01 = 10N + 12, fwd NT = 9N + 12**. 2BIT는 `BEQ r0,r0`이 첫 반복에만 2, 그다음 1이라 반복당 1 줄고 한 번 1 더: no-fwd 2BIT 9N + 13, fwd 2BIT 8N + 13 (N ≥ 1, N = 0이면 12). 체인(반복당 8명령, 거리 1 RAW 4개): no-fwd 8 + 8 + 2 = 18, fwd 8 + 0 + 2 = 10 → **18K + 13, 10K + 12, 17K + 14, 9K + 13**. 네 공식 모두 N = 0..12, K = 1..10에서 시뮬레이터와 같다 (`f_ok`, `h_ok`). 포워딩 없음 + NT의 사이클 수가 손으로 유도한 M01 규칙과 같다는 게 "M02가 M01을 그대로 품는다"의 증거다.

배열 합 N = 2, 포워딩 + 2BIT (실제 출력. 같은 입력의 noFwd+NT는 `cycles=32 retired=18 stalls=4 flushes=6`):

```text
      [sum N=2, Fwd+2BIT]
      pc  instruction         1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29
        0 LD   r1, 0(r0)     IF ID EX ME WB
        1 ADDI r2, r0, 16       IF ID EX ME WB
        2 ADD  r3, r0, r0          IF ID EX ME WB
        3 BEQ  r1, r0, +5             IF ID EX ME WB
        4 LD   r4, 0(r2)                 IF ID EX ME WB
        5 ADD  r3, r3, r4                   IF ID ID EX ME WB
        6 ADDI r2, r2, 1                       IF IF ID EX ME WB
        7 ADDI r1, r1, -1                            IF ID EX ME WB
        8 BEQ  r0, r0, -6                               IF ID EX ME WB
        9 ST   r3, 1(r0)                                   IF ID xx
       10 HALT                                                IF xx
        3 BEQ  r1, r0, +5                                        IF ID EX ME WB
        4 LD   r4, 0(r2)                                            IF ID EX ME WB
        5 ADD  r3, r3, r4                                              IF ID ID EX ME WB
        6 ADDI r2, r2, 1                                                  IF IF ID EX ME WB
        7 ADDI r1, r1, -1                                                       IF ID EX ME WB
        8 BEQ  r0, r0, -6                                                          IF ID EX ME WB
        9 ST   r3, 1(r0)                                                              IF xx
        3 BEQ  r1, r0, +5                                                                IF ID EX ME WB
        4 LD   r4, 0(r2)                                                                    IF ID xx
        5 ADD  r3, r3, r4                                                                      IF xx
        9 ST   r3, 1(r0)                                                                          IF ID EX ME WB
       10 HALT                                                                                       IF ID EX ME WB
       11 NOP                                                                                           IF ID xx
       12 NOP                                                                                              IF xx
      cycles=29 retired=18 CPI=1.611 stalls=2 flushes=5
```

32 → 29: ADD가 `ID ID ID` 대신 `ID ID`(load-use만), 두 번째 반복의 `BEQ r0,r0` 뒤에는 `ST` 하나만 `xx`(예측 taken → ID에서 전환, 1사이클). 첫 반복의 `BEQ r0,r0`은 카운터 01이라 NT 예측 → 오예측 2, 탈출 BEQ도 2.

#### 식 모델 (eq_cycles)

식은 힌트 5 그대로다 (`ready[rd]` = no-fwd `t+3`, fwd LD `t+2`, fwd ALU `t+1`; `extra` = 위 제어 비용 표). in-order 단일 이슈에서는 각 명령의 EX 사이클이 "앞 명령 + 1 + 제어 거품"과 "데이터 준비" 중 큰 쪽이라는 것만으로 충분하다. 스톨된 명령 뒤의 명령은 IF에서 같이 기다리므로 `ex[k-1] + 1`이 그 효과를 이미 담는다. 래치·플러시·다이어그램을 다루는 사이클 시뮬레이터와 이 20줄 남짓이 벤치 16개 조합 + 무작위 4000개 실행에서 사이클·스톨·플러시·오예측까지 전부 같다: `eq model vs cycle sim: 4000 random runs, 0 mismatches`. 실리콘 팀이 하는 일과 같은 구조다 — 빠른 분석 모델(아키텍트의 스프레드시트)과 사이클 모델(또는 RTL)을 무작위 프로그램으로 상관(correlation) 검증한다.

#### 실제 코어와 컴파일러

- **Cortex-M3/M4**: 3단(Fetch, Decode, Execute), 동적 분기 예측기 없음. M4 TRM의 명령 타이밍 표는 taken 분기를 대략 "1 + P (P = 파이프라인 재충전 1–3사이클)", LDR을 보통 2사이클로 적고, 이웃한 load/store는 주소·데이터 단계를 겹칠 수 있다고 한다 (세부는 TRM 표로 확인). 그래서 짧은 ISR 루프에서 분기 하나가 2–3사이클 — 이 레슨의 NT 2사이클과 같은 크기다.
- **Cortex-M7**: 6단 dual-issue, 분기 예측(BTAC)이 있다 (TRM 기준). 결정적 실행 시간이 필요한 코드에서 예측기 때문에 루프 첫 반복이 느린 것도 이 레슨의 "웜업 오예측"과 같은 현상이다.
- **Cortex-A / Apple M6**: 깊은 out-of-order 파이프라인(Apple은 단계 수를 공개하지 않는다), 히스토리 기반 예측기(TAGE 계열이 업계 표준 기법으로 알려져 있다), L1 load-use도 여러 사이클이다. 원리는 같고 숫자가 크다 (실측: ARCH-M03 분기, MEM-B10 포인터 체이싱).

`demo_sched.c`: 배열 합과 포인터 따라가기 루프를 세 타깃으로 컴파일한 실제 asm (루프 부분만, 지시문 줄 생략):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -fno-unroll-loops -S demo_sched.c -o -
clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -mthumb -O2 -fno-unroll-loops -S demo_sched.c -o -
cc -std=c11 -O2 -fno-unroll-loops -S demo_sched.c -o -        # arm64 (Apple clang 21)
```

```text
@ cortex-m4  sum_u16
.LBB0_2:                                @ =>This Inner Loop Header: Depth=1
	ldrh	r3, [r2, #2]!
	subs	r1, #1
	add	r0, r3
	bne	.LBB0_2
@ cortex-m4  chase
.LBB1_2:                                @ =>This Inner Loop Header: Depth=1
	ldr	r3, [r2, #4]
	ldr	r2, [r2]
	subs	r1, #1
	add	r0, r3
	bne	.LBB1_2
@ cortex-m7  sum_u16 은 M4 와 같은 명령열, chase
.LBB1_2:                                @ =>This Inner Loop Header: Depth=1
	ldr	r3, [r2, #4]
	subs	r1, #1
	ldr	r2, [r2]
	add	r0, r3
	bne	.LBB1_2
; arm64  sum_u16 스칼라 꼬리 루프 (8개 이상은 NEON 루프로 간다)
LBB0_8:                                 ; =>This Inner Loop Header: Depth=1
	ldrh	w10, [x11], #2
	add	w8, w8, w10
	subs	x9, x9, #1
	b.ne	LBB0_8
; arm64  chase
LBB1_1:                                 ; =>This Inner Loop Header: Depth=1
	ldr	w9, [x0, #8]
	add	w8, w9, w8
	ldr	x0, [x0]
	subs	w1, w1, #1
	b.ne	LBB1_1
```

- Cortex-M 두 타깃 모두 `ldrh r3, [r2, #2]!` 와 그 값을 쓰는 `add r0, r3` 사이에 **루프 카운터 `subs r1, #1`을 끼워 넣었다** — 연습 1의 손 스케줄링과 같은 일을 컴파일러가 한다.
- `chase`(포인터 따라가기)는 M4에서 `ldr r3; ldr r2`를 붙여 두고(이웃 load 겹치기와 맞는 배치), M7에서는 그 사이에 `subs`를 넣었다. 스케줄링 모델이 CPU마다 다르다는 증거다 (왜 그렇게 골랐는지는 LLVM 스케줄링 모델 내부라 추정만 한다).
- arm64(M6용) 스칼라 꼬리 루프는 `ldrh w10` 바로 다음에 `add w8, w8, w10` — out-of-order 코어는 하드웨어가 알아서 기다리므로 컴파일러가 굳이 떼어 놓지 않았다. 대신 8개 이상이면 NEON으로 벡터화했다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_ARCH-M02/so && /tmp/esn_ARCH-M02/so | tail -4
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_ARCH-M02/san && /tmp/esn_ARCH-M02/san | tail -1
```

```text
PASS  p->dmem[1] == 11 && p->cycles == 29
      eq model vs cycle sim: 4000 random runs, 0 mismatches
PASS  bad == 0
40/40 passed
40/40 passed
```

전체 0.1초 안에 끝난다. ASan + UBSan 빌드도 `40/40 passed`, 경고 없음 (practice.c도 `11/11 passed`, 경고 없음). TODO는 다섯 군데: `fwd_src`, `hazard_stall`의 포워딩 분기, `ctr2_next`/`ctr2_taken`, `ex_redirect`의 2BIT 분기, `eq_cycles`.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|
| MEM/WB를 EX/MEM보다 먼저 검사 | 같은 레지스터를 두 명령이 연달아 쓰면 오래된 값이 이긴다 (`two` 테스트: r2 = 2) | 젊은 쪽(EX/MEM) 먼저 |
| EX/MEM의 LD도 포워딩 | 그 래치의 값은 주소다. 실제 하드웨어라면 아직 없는 값 | `m->d.op != OP_LD` + load-use 스톨 |
| 포워딩 켜고 M01 스톨 조건을 그대로 둠 | 정답은 나오지만 포워딩의 이득이 0 (chain 12사이클 그대로) | 포워딩 ON이면 load-use만 |
| load-use 검사를 ALU 소스에만 | BEQ 비교, ST 데이터, LD 베이스도 EX에서 값을 쓴다 | `src_regs`가 모든 소스를 돌려준다 |
| r0 쓰기를 생산자로 봄 | `ADDI r0,..` 뒤에서 쓸데없이 스톨하거나, 더 나쁘게 0이 아닌 값을 포워딩 | `wb = writes_rd() && rd != 0` |
| 오예측 복구 주소를 항상 목표로 | "예측 T, 실제 NT"에서 다시 목표로 간다 → 잘못된 경로 | `taken ? target : pc + 1` |
| 2BIT에서 JAL을 EX에서도 전환 | 이미 ID에서 목표로 갔는데 또 플러시 → 목표 명령 두 번 버림 | JAL은 EX에서 아무것도 안 함 |
| 카운터를 ID에서 예측 직후 갱신 | 아직 결과를 모른다. 틀린 경로 분기가 표를 오염 | EX에서 실제 결과로 갱신 |
| HALT를 ID에서 fetch 정지 | ID의 HALT는 앞 분기가 틀리면 틀린 경로일 수 있다 | EX(비추측 지점)에서 정지 |
| 스톨된 사이클을 플러시와 동시에 셈 | EX 플러시가 ID 명령을 버리면 그 스톨은 의미 없다 → 항등식이 깨진다 | 우선순위 HALT > EX 플러시 > 스톨 > ID 전환 |
| 퍼징 생성기에서 분기 목표를 먼저 정함 | 뒤에 만든 "주소 준비 쌍" 가운데에 떨어져 기준 모델도 폴트 (실제로 겪음) | 2패스 생성 |

## 8. 면접 Q&A

<details><summary>Q1. 포워딩 경로는 몇 개고, 각각 어떤 해저드를 푸나요?</summary>

5단 기본형에서 EX 입력으로 둘: EX/MEM → EX(거리 1, 바로 앞 ALU 결과)와 MEM/WB → EX(거리 2, ALU 결과나 load 값). 거리 3은 레지스터 파일을 앞 절반 쓰기 / 뒤 절반 읽기로 만들어 해결한다. 이 모델에서 체인 `ADDI; ADD; ADD`가 12 → 8사이클, 의존 체인 벤치가 733 → 412.

꼬리: "두 경로가 같은 레지스터를 가리키면?" → 더 젊은 EX/MEM이 이긴다 (P&H의 double data hazard 조건). "포워딩 MUX는 타이밍에 어떤 영향을?" → EX 단계 앞에 MUX가 하나 붙어 ALU 경로가 길어진다. 고속 설계에서 포워딩 망이 임계 경로가 되는 일이 흔하다 (B13 타이밍 슬랙과 연결).
</details>

<details><summary>Q2. 포워딩이 있는데도 load-use 스톨이 남는 이유는? 없애는 방법은?</summary>

load 값은 MEM 끝에 생기고, 바로 다음 명령은 그 시점에 EX를 시작해야 한다. 포워딩은 이미 있는 값을 일찍 전달할 뿐, 아직 없는 값은 못 준다 → 1사이클. 줄이는 법: (1) 컴파일러가 독립 명령을 사이에 넣는다 (`demo_sched.c`의 Cortex-M asm, 연습 1: 9N+12 → 8N+12), (2) 소비자가 ST 데이터면 MEM→MEM 경로 (연습 4), (3) 초기 MIPS I처럼 ISA에 load delay slot을 둬서 컴파일러 책임으로 만든다 (나중 MIPS는 interlock으로 바꿨다).

꼬리: "포인터 따라가기는?" → 다음 주소가 방금 읽은 값이라 스케줄링으로 못 숨긴다. 큰 코어에서는 load-use 지연 × 노드 수가 그대로 실행 시간이 된다 (MEM-B10).
</details>

<details><summary>Q3. 2비트 카운터가 1비트보다 나은 이유를 루프로 설명해 보세요.</summary>

안쪽 루프 탈출 분기는 "N…N T"를 반복한다. 1비트는 탈출 T로 비트가 T가 되고, 다음 진입 첫 N에서 또 틀린다 → 바깥 반복마다 2번. 2비트는 강한 NT(00)에서 T 한 번으로 약한 NT(01)까지만 가서 예측이 그대로 NT → 1번. 이 레슨의 M=4 K=5에서 PC 3 오예측이 1비트 7, 2비트 4.

꼬리: "2비트가 1비트보다 나쁜 패턴은?" → T N T N 처럼 번갈아 가면 01 ↔ 10을 오가며 매번 틀릴 수 있다. 그 해법은 히스토리를 쓰는 예측기(로컬 히스토리, gshare: 전역 히스토리 XOR PC, 그리고 TAGE).
</details>

<details><summary>Q4. 이 루프의 CPI를 손으로 계산해 보세요 (배열 합, 포워딩 없음, not-taken).</summary>

반복당 6명령 + load-use 스톨 2(포워딩 없으니 거리 1 = 2) + 뒤로 가는 무조건 분기 플러시 2 = 10사이클. 머리·꼬리 12 → 10N + 12, N = 64면 652사이클 / 390명령 = CPI 1.672. 포워딩 → 9N + 12, 2비트까지 → 8N + 13 (525, CPI 1.346).

꼬리: "더 줄이려면?" → LD와 ADD 사이에 ADDI를 옮겨 load-use 제거(7N + 13), 무조건 분기를 없애는 루프 회전(조건 분기를 루프 끝으로 — 이 ISA엔 BNE가 없어서 카운터를 0까지 세는 형태로 바꿔야 한다), 루프 펼치기(B09 연습 1).
</details>

<details><summary>Q5. 데이터가 무작위면 2비트 예측기가 정적 not-taken보다 나쁠 수 있나요?</summary>

그 분기만 보면 그렇다. count random에서 BEQ 오예측(루프 탈출 1번 포함)이 2BIT 100, NT 95(= taken 수: hit 94 + 탈출 1). 2BIT는 맞춘 taken에도 ID 전환 1사이클을 낸다. 그런데도 전체는 2BIT가 1743 < 1895인데, JAL 200개가 2 → 1사이클이 됐기 때문이다. 정렬된 데이터는 오예측 4.

꼬리: "그럼 실제로는 어떻게?" → 예측 불가능한 분기는 분기를 없앤다: 조건 선택(Arm `CSEL`, ARM-B05), 마스크 연산 `-(x == k) & 1`. 실제 M6에서 분기형과 무분기형을 재는 것은 ARCH-M03.
</details>

<details><summary>Q6. 분기를 EX가 아니라 ID에서 결정하면 어떻게 되나요?</summary>

not-taken 예측의 taken 비용이 2 → 1사이클로 준다 (교과서 P&H의 개선형). 대신 비교기가 ID로 올라오므로 (1) 비교 입력에 ID로 가는 포워딩이 필요하고, (2) 바로 앞 ALU 결과로 분기하면 1사이클, 바로 앞 LD 결과면 2사이클 스톨이 생기고, (3) ID 단계가 레지스터 읽기 + 비교로 길어져 클럭이 느려질 수 있다. 이 레슨은 비교를 EX에 두고, 대신 "예측 taken이면 ID에서 목표로 전환"을 해서 같은 1사이클을 얻었다.

꼬리: "MIPS의 branch delay slot은?" → 분기 뒤 한 칸을 항상 실행하는 ISA 규칙으로 1사이클을 컴파일러가 채우게 했다. 파이프라인이 깊어지고 예측기가 생기면서 짐이 됐다 — RISC-V, AArch64는 delay slot이 없다.
</details>

<details><summary>Q7. 파이프라인 모델이 맞는지 어떻게 검증했나요?</summary>

두 층. 정확성: 단일 사이클 기준 모델(ISS)과 아키텍처 상태 비교 — 무작위 프로그램 2000개 × 설정 4개, 그리고 예측기는 상태를 바꾸면 안 된다는 불변식. 성능: 사이클 수를 독립적으로 계산하는 식 모델과 비교(4000개, 스톨·플러시·오예측까지), 손으로 유도한 M01 공식과 비교, 항등식 `cycles = retired + 4 + stalls + flushes`. 이건 실리콘 검증의 golden model + 성능 모델 상관(correlation)과 같은 구조다.

꼬리: "무작위 생성기에서 조심할 점은?" → 끝나야 하고(앞으로만 가는 분기), 폴트가 없어야 하고(주소 범위), 해저드가 자주 나와야 한다(레지스터 수를 좁게, BEQ가 taken이 되도록 같은 레지스터 비교). 이 레슨에서 실제로 생성기 버그(분기가 주소 준비 쌍 가운데 착지)를 오라클이 잡았다.
</details>

## 9. 추가 연습

모두 `practice.c`에서 한 번에 확인한다. 연습 2–5는 본문에서 사이클 시뮬레이터와 대조한 **식 모델을 확장**해서 쓴다 (1BIT, BTFN, MEM→MEM은 식 모델에만 넣었고 사이클 시뮬레이터에는 없다).

### 연습 1 — 스케줄링으로 load-use 숨기기

배열 합 루프에서 `ADDI r2, r2, 1`을 LD와 ADD 사이로 옮겨라. 결과가 같은가? 네 설정의 사이클 공식은?

<details><summary>답</summary>

`LD r4,0(r2); ADDI r2,r2,1; ADD r3,r3,r4` — ADDI는 LD가 읽은 r2를 덮어쓰지만(WAR) in-order 파이프라인에서 LD는 이미 ID에서 r2를 읽었으므로 문제없다. ADD는 이제 LD와 거리 2. 포워딩이 있으면 스톨 0 → 반복당 8 → **8N + 12** (원래 9N + 12). 포워딩이 없으면 거리 2 = 1스톨 → **9N + 12** (원래 10N + 12). 포워딩 + 2BIT **7N + 13**. N = 16에서 원본 172/156/141 → 스케줄 156/140/125. 컴파일러가 Cortex-M용으로 `subs`를 끼워 넣은 것(`demo_sched.c`)과 같은 일이다.
</details>

### 연습 2 — BTFN 정적 예측

"뒤로 가는 분기는 taken, 앞으로 가는 분기는 not-taken"을 ID에서 결정하는 정적 예측(BTFN)을 열로 추가하라 (비용은 2BIT와 같은 표). 네 벤치에서 2BIT와 비교하면?

<details><summary>답</summary>

`BEQ r0,r0,loop`(뒤로)를 처음부터 맞히므로 sum/hash는 2BIT보다 1사이클씩 낫다: 524 (= 8·64 + 12), 372 (= 9·40 + 12). 키 개수 세기의 `BEQ r5,r2,hit`는 앞으로 가는 분기라 늘 NT 예측 → 오예측 = hit 수 + 탈출 1 = 95. 무작위에서는 BTFN 1695 < 2BIT 1743 (2BIT가 50% 데이터에서 더 자주 틀린다), 정렬에서는 2BIT 1606 < BTFN 1695 (2BIT는 경계 근처에서만 틀린다). 정적 예측은 하드웨어가 거의 없고 결정적이라 작은 코어에서 여전히 의미가 있다.
</details>

### 연습 3 — 1비트 vs 2비트

같은 이중 루프(M=4, K=5)에서 1비트 예측기(16 엔트리, 초기 NT)의 PC 3 오예측과 전체 오예측은?

<details><summary>답</summary>

PC 3: 첫 바깥 반복은 탈출 T 한 번만 틀리고(1), 그 뒤 반복마다 진입 첫 N과 탈출 T를 둘 다 틀린다(2 × 3) → **2M − 1 = 7**. 전체 = 7 + 바깥 탈출 1 + 뒤로 가는 분기 두 개의 첫 실행 2 = **10** (2비트는 4와 7).
</details>

### 연습 4 — MEM→MEM 포워딩

memcpy 루프(`LD r4,0(r2); ST r4,0(r3)`, ARCH-B09)는 포워딩 + NT에서 몇 사이클? ST **데이터**에 한해 MEM→MEM 경로를 넣으면? ST의 **베이스**가 LD 결과라면?

<details><summary>답</summary>

반복당 7명령 + load-use 1 + 플러시 2 = 10 → **10N + 11** (N = 16: 171). ST는 데이터를 MEM 단계에서야 쓰므로 앞 LD의 MEM 끝 값을 다음 사이클 MEM 시작에 받으면 된다 → 스톨 0 → **9N + 11** (155). 베이스는 EX에서 주소 계산에 필요하므로 여전히 1사이클 (`LD r1; ST r2,0(r1)` 스톨 1).
</details>

### 연습 5 — 손계산

```text
0: ADDI r1, r0, 3
1: LD   r2, 0(r1)        ; dmem[3] = 5
2: ADD  r3, r2, r1
3: BEQ  r3, r0, +1
4: ADDI r4, r0, 1
5: HALT
```

네 설정의 사이클 수는? `dmem[3] = 0xFFFD`(r3 = 0 → taken)이면?

<details><summary>답</summary>

no-fwd NT: EX 사이클 3, 6(거리 1 = 2스톨), 9(LD 거리 1 = 2스톨), 12(2스톨), 13, 14 → HALT WB = **16** (= 6 + 4 + 스톨 6). fwd NT: 3, 4, 6(load-use 1), 7, 8, 9 → **11**. 2BIT는 BEQ 카운터 01 = NT 예측이 맞으므로 같다 (16, 11). taken이면 ADDI r4가 빠져 5명령: no-fwd NT 5 + 4 + 6 + 플러시 2 = **17**, fwd NT 5 + 4 + 1 + 2 = **12**, fwd 2BIT도 첫 실행 01 → 오예측 2로 **12**.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_ARCH-M02/pr && /tmp/esn_ARCH-M02/pr
```

```text
-- 연습 1: 스케줄링으로 load-use 숨기기 --
      N=16  original noFwd/Fwd/Fwd+2BIT = 172/156/141   scheduled = 156/140/125
PASS  s_ok
-- 연습 2: BTFN 열 (모두 포워딩 ON) --
      program              NT     2BIT     BTFN   mispred 2BIT/BTFN
      sum N=64            588      525      524   2/1
      hash K=40           412      373      372   2/1
      count random       1895     1743     1695   100/95
      count sorted       1895     1606     1695   4/95
      (count: hits = 94)
PASS  c_bt[0] == 8 * 64 + 12 && c_bt[1] == 9 * 40 + 12
PASS  m_bt[2] == hits + 1 && c_bt[2] < c_2b[2]
PASS  c_2b[3] < c_bt[3]
-- 연습 3: 1비트 vs 2비트 (이중 루프 M=4 K=5) --
      inner-exit mispred 1BIT=7 2BIT=4, total 1BIT=10 2BIT=7
PASS  in1 == 2 * NEST_M - 1 && t1 == 2 * NEST_M + 2
PASS  r2->mp_pc[3] == NEST_M && r2->mispred == NEST_M + 3
-- 연습 4: MEM->MEM 포워딩 (memcpy) --
      N=16  Fwd+NT 171, +MEM->MEM 155
PASS  m_ok
PASS  xeq(g_tr, g_ntr, FWD_NT_MF)->stalls == 1
-- 연습 5: 손계산 --
PASS  g_ntr == 6 && cyc(NOFWD_NT) == 16 && cyc(FWD_NT) == 11 && cyc(FWD_2B) == 11
PASS  xeq(g_tr, g_ntr, NOFWD_NT)->stalls == 6 && xeq(g_tr, g_ntr, FWD_NT)->stalls == 1
PASS  g_ntr == 5 && cyc(NOFWD_NT) == 17 && cyc(FWD_NT) == 12 && cyc(FWD_2B) == 12 && g_a.reg[4] == 0
11/11 passed
```

관찰용 파일 (CHECK 없음):

<!-- code: demo_sched.c -->

## 10. 핵심 정리

- 포워딩: 거리 1 = EX/MEM → EX, 거리 2 = MEM/WB → EX, 거리 3 = split-phase 레지스터 파일. 같은 레지스터면 **젊은 쪽 먼저**. r0은 생산자도 소비자도 아니다.
- 포워딩 후 남는 데이터 스톨은 **load-use 1사이클** — 값이 MEM 끝에 생기기 때문. 스케줄링(독립 명령 끼우기), ST 데이터용 MEM→MEM 경로로 줄인다. 포인터 따라가기는 못 숨긴다.
- 제어 비용(이 모델): NT의 taken = 2, 2BIT 예측 taken 맞음 = 1, 오예측 = 2, JAL = 2BIT에서 1. 2비트 히스테리시스 = 안쪽 루프 탈출이 바깥 반복당 1번 (1비트는 2번).
- 무작위 데이터의 분기는 2비트가 정적 NT보다도 자주 틀릴 수 있다 (100 vs 95). 예측 불가능하면 분기를 없앤다 (CSEL, ARCH-M03).
- 숫자: 배열 합 M01 10N + 12 → fwd 9N + 12 → fwd + 2BIT 8N + 13; 체인 18K + 13 → 9K + 13. N = 64 CPI 1.672 → 1.346.
- 검증 = ISS와 아키텍처 상태 대조(무작위 8000 실행) + 식 모델과 사이클 대조(4000) + 손 공식 + 항등식 `cycles = retired + 4 + stalls + flushes`.

## 참고자료

- D. A. Patterson, J. L. Hennessy, *Computer Organization and Design* (ARM 또는 RISC-V Edition), 4장 — 4.7 데이터 해저드: 포워딩 vs 스톨 (double data hazard, load-use), 4.8 제어 해저드 (분기를 ID로 옮기기, 동적 예측).
- J. L. Hennessy, D. A. Patterson, *Computer Architecture: A Quantitative Approach*, 부록 C (파이프라인 기초)와 3장 (분기 예측).
- J. E. Smith, "A Study of Branch Prediction Strategies", ISCA 1981 — 2비트 포화 카운터.
- S. McFarling, "Combining Branch Predictors", DEC WRL Technical Note TN-36, 1993 — gshare.
- A. Seznec, P. Michaud, "A case for (partially) TAgged GEometric history length branch prediction", JILP 2006 — TAGE.
- Arm, *Cortex-M4 Technical Reference Manual* — Processor instruction timings (분기 1 + P, load/store 타이밍); *Cortex-M7 Technical Reference Manual* — 파이프라인과 분기 예측. 세부 수치는 문서로 확인할 것.
- 관련 레슨: ARCH-B08 (ISA), ARCH-B09 (단일 사이클 기준 모델), ARCH-M01 (기준선 파이프라인), ARCH-M03 (실제 M6 분기 예측 측정, CSEL), ARCH-M10 (파이프라인과 정밀 예외), ARCH-B13 (임계 경로와 Fmax), MEM-B10 (포인터 체이싱 지연), ARM-B05 (CSEL), SIM-M01 (사이클 근사 모델, 다음 단계).
