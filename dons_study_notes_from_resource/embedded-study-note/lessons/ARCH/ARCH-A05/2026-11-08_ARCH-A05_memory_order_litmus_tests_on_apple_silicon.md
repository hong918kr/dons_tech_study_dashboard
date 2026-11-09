---
id: ARCH-A05
time: 110분
updated: 2026-11-08
---
# ARCH-A05 메모리 순서 리트머스 테스트 on Apple M2

## 학습 목표

- litmus7 방식(반복마다 새 변수, 같이 출발, 결과는 배열에 기록 → 끝나고 히스토그램)의 **리트머스 하네스** 를 C11 + pthreads 로 직접 짜고, 하네스 설계(동기화 방식, 변수 배치)가 관찰 빈도를 **1000배 이상** 바꾼다는 것을 숫자로 본다.
- SB · MP · LB (+ IRIW · 2+2W) 를 relaxed / acq-rel / seq_cst / fence 네 변형으로 100만 번씩 돌려 "이상한 결과" 를 세고, **C11 이 금지한 칸은 정확히 0, 허용한 칸은 나오거나 안 나온다** 를 구분해 말한다 (0 번 관찰 ≠ 금지).
- 같은 소스를 `-arch x86_64` 로 빌드해 **Rosetta 2(= TSO 처럼 동작)** 에서 돌려, "ARM 에서는 보이고 x86 에서는 안 보이는 재배치" (MP · 2+2W · IRIW) 와 "둘 다 보이는 재배치" (SB) 를 한 기계에서 실측한다.
- 각 변형의 실제 기계어 (`stlr`/`ldapr`/`ldar`/`dmb ish`/`dmb ishld`, x86 의 `mov`/`xchg`/`lock or`) 를 `-S` 로 보고 결과와 연결한다.
- 실리콘 검증 관점: (테스트, 변형) × (C11, ARMv8, TSO) 허용 표로 관찰을 판정하는 **회귀 게이트** — "금지인데 1 번이라도 나오면 버그" — 를 만든다.

## 1. 문제

멀티코어 SoC 펌웨어(Cortex-A 클러스터, STM32H7 M7+M4, i.MX8 의 A53+M4)와 실리콘 검증에서 "이 코드가 다른 코어에서 **어떤 순서로 보이나**" 는 추측이 아니라 **측정과 규칙** 으로 답해야 하는 질문이다. 면접은 "x86 에서 잘 돌던 lock-free 코드가 ARM 서버에서 깨졌다 — 왜?", "리트머스를 1000만 번 돌렸는데 0 이면 안전한가?", "새 칩의 메모리 순서를 어떻게 검증하나?" 를 묻는다. 이 레슨은 그 질문들에 **Apple M6 에서 직접 센 숫자** 로 답하게 만든다.

리트머스 테스트(litmus test)는 스레드 2~4 개, 연산 1~2 개짜리 작은 프로그램이다. 모든 변수는 처음에 0. 결과(레지스터 값, 최종 메모리 값)의 조합 중 **순차 일관성(SC, sequential consistency) 으로는 나올 수 없는 것** 을 "이상한 결과(weird)" 라 부른다. 약한 메모리 모델(ARMv8)은 그중 일부를 허용하고, 하드웨어가 실제로 그걸 내는지 **세 보는 것** 이 이 레슨의 일이다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">세 가지 기본 리트머스 — 모든 변수 초기값 0, 점선 = "이상한 결과" 를 만드는 순환</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="30" width="235" height="200" rx="6"/>
    <rect x="252" y="30" width="235" height="200" rx="6"/>
    <rect x="494" y="30" width="235" height="200" rx="6"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="50" font-weight="bold">SB (store buffering)</text>
    <text x="20" y="74">T0</text><text x="140" y="74">T1</text>
    <text x="20" y="100" font-family="monospace">x = 1</text><text x="140" y="100" font-family="monospace">y = 1</text>
    <text x="20" y="160" font-family="monospace">r0 = y</text><text x="140" y="160" font-family="monospace">r1 = x</text>
    <text x="20" y="200">weird: r0=0 ∧ r1=0</text>
    <text x="20" y="218">store→load 재배치 (store 버퍼)</text>
    <text x="262" y="50" font-weight="bold">MP (message passing)</text>
    <text x="262" y="74">T0</text><text x="382" y="74">T1</text>
    <text x="262" y="100" font-family="monospace">x = 1 (data)</text><text x="382" y="100" font-family="monospace">r0 = y (flag)</text>
    <text x="262" y="160" font-family="monospace">y = 1 (flag)</text><text x="382" y="160" font-family="monospace">r1 = x (data)</text>
    <text x="262" y="200">weird: flag=1 ∧ data=0</text>
    <text x="262" y="218">store→store 또는 load→load</text>
    <text x="504" y="50" font-weight="bold">LB (load buffering)</text>
    <text x="504" y="74">T0</text><text x="624" y="74">T1</text>
    <text x="504" y="100" font-family="monospace">r0 = x</text><text x="624" y="100" font-family="monospace">r1 = y</text>
    <text x="504" y="160" font-family="monospace">y = 1</text><text x="624" y="160" font-family="monospace">x = 1</text>
    <text x="504" y="200">weird: r0=1 ∧ r1=1</text>
    <text x="504" y="218">load→store 재배치</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5">
    <path d="M40 106 V146"/><path d="M160 106 V146"/>
    <path d="M282 106 V146"/><path d="M402 106 V146"/>
    <path d="M524 106 V146"/><path d="M644 106 V146"/>
  </g>
  <g stroke="currentColor" stroke-dasharray="4 3" fill="none" stroke-width="1.5">
    <path d="M70 156 L140 104"/><path d="M190 156 L70 104"/>
    <path d="M330 156 L382 104"/><path d="M430 156 L330 104"/>
    <path d="M555 156 L624 104"/><path d="M675 156 L555 104"/>
  </g>
  <text x="10" y="252" fill="currentColor">실선 = 프로그램 순서(po). 점선 = 읽은 값의 출처 (SB: 옛 값 0 = fr, MP·LB: 상대 store = rf).</text>
  <text x="10" y="270" fill="currentColor">po 를 다 지키면 실선 + 점선이 사이클 → SC 불가능. 나왔다면 어느 po 가 다른 코어 눈에 깨진 것.</text>
</svg>
```

변형 네 개 (같은 테스트, 메모리 순서만 바꿈):

| 변형 | store | load | 사이 펜스 | C11 이 금지하는 테스트 |
|---|---|---|---|---|
| relaxed | relaxed | relaxed | — | 없음 (전부 허용) |
| acq/rel | release | acquire | — | MP, LB (SB 는 여전히 허용) |
| seq_cst | seq_cst | seq_cst | — | SB, MP, LB, IRIW, 2+2W 전부 |
| fence | relaxed | relaxed | 테스트별 최소 펜스 | SB(seq_cst 펜스), MP(release/acquire 펜스), LB(acq_rel 펜스), 2+2W(seq_cst) |

### Part A — 하네스: 변형 표 · 스핀 장벽 · 결과 키 · 히스토그램

1. `ord(V, kind)` / `fence_ord(T, V, t)` — 변형 → store/load 의 `memory_order`, FENCE 변형에서 스레드 t 가 두 연산 사이에 넣을 펜스 (없으면 `memory_order_relaxed` = 아무것도 아님).
2. `sbar_wait` — n 스레드 스핀 장벽 (세대 번호 방식). 장벽 앞의 모든 쓰기가 장벽 뒤의 읽기보다 happens-before 여야 한다 (셀 초기화가 다음 배치에 보이게).
3. `outcome_key` / `lit_tally` — 반복 j 의 결과 슬롯들을 2 비트씩 묶은 키, 그리고 히스토그램.

하네스 본체(`lit_run`)는 주어진다: 변수 `x`, `y` 는 반복마다 **새 칸** (`cell_t` = 128 B, 한 칸 = 한 캐시 라인), 4096 반복 배치, 배치마다 스레드 0 이 칸을 0 으로 되돌리고 최종값을 기록한다. 출발 동기화는 세 가지: 반복마다 장벽(`M_EACH`), 배치 시작에만(`M_BATCH`), **공통 시간축** (`M_TIME`: `mach_absolute_time()` 의 같은 눈금에 모든 스레드가 출발 — litmus7 의 timebase 동기화와 같은 생각).

체크: 표 값, 장벽 세대 수와 "장벽 뒤에는 상대 쓰기가 보인다", 손으로 채운 결과 배열의 히스토그램, 그리고 **실제 스레드로 10만 번 돌린 C11 금지 칸 8 개의 이상한 결과 = 정확히 0**, 반복 수 합계.

### Part B — 강제 스케줄: SC 가 허용하는 결과를 전부, 정확히

`lit_enum_scheds` — 각 스레드의 프로그램 순서를 지키는 **모든 인터리빙** 을 사전식으로 만든다 (SB/MP/LB/2+2W = 6 개, IRIW = 180 개). 같은 연산 코드(`step`)를 (1) 한 스레드에서 차례대로, (2) **실제 스레드 + 차례 토큰** 으로 돌리면 결과가 매번 정해진다 → 히스토그램이 손으로 센 값과 정확히 같아야 한다. 이것이 "하네스가 허용된 결과를 모두 만들어 낼 수 있다" 의 결정적 증거이고, 실제 실행에서 "C11 금지 칸의 결과가 모두 SC 집합 안" 을 확인하는 기준 집합이 된다.

### Part C — 실리콘 검증 게이트

`ALLOW[test][variant][model]` (C11 / 이 맥의 arm64 / x86-64 TSO) 표가 주어진다. `lit_verdict(weird, lang_allows, hw_allows)` 를 써서 관찰을 판정한다: 나옴(허용) / **허용인데 0** ("안 보였을 뿐") / 금지 + 0 / **HW-BUG** (C11 은 허용하지만 하드웨어 모델이 금지하는데 나옴) / **LANG-BUG** (C11 금지인데 나옴 — 컴파일러 매핑, 실리콘, 하네스 중 하나가 틀림). `lit_gate` 는 관찰 목록에서 버그 수와 첫 버그를 돌려준다. 합성 데이터(정상 칩 / MP acq-rel 에서 3 번 나온 가상의 버그 칩)로 체크한다.

## 2. Problem (English)

> **Litmus testing on real hardware.** Build a litmus7-style harness in C11 + pthreads: each test has 2–4 threads with 1–2 atomic accesses each; every iteration uses fresh, cache-line-separated variables; threads start each iteration together (spin barrier, or a common timebase); per-iteration results go into arrays and are histogrammed afterwards.
>
> **A.** Implement `ord(variant, kind)` / `fence_ord(test, variant, thread)` for four variants (relaxed, acquire/release, seq_cst, relaxed + minimal fence), a sense/generation spin barrier `sbar_wait`, `outcome_key` (2 bits per result slot) and `lit_tally`. Run SB, MP, LB with ≥10^6 iterations per variant; report the count of the non-SC outcome. With seq_cst (SB) and acquire/release (MP, LB) the forbidden outcome must be exactly 0.
>
> **B.** Implement `lit_enum_scheds` — all program-order-preserving interleavings in lexicographic order. Using the same per-operation code, evaluate each schedule single-threaded and with real threads passing a turn token; both histograms must equal the hand-derived SC outcome counts (SB: (1,1)×4, (0,1)×1, (1,0)×1).
>
> **C.** Implement `lit_verdict(weird, lang_allows, hw_allows)` and a regression gate over observations, given an allow-table for C11, Armv8 (as compiled by clang) and x86-TSO. Rebuild the same source with `-arch x86_64` and run it under Rosetta 2: which weird outcomes survive?
>
> Follow-ups: Which reorderings show up on Arm but not on x86? You ran LB 10^7 times and saw 0 — is LB forbidden? Why can't acquire/release fix SB? What is multi-copy atomicity and why is IRIW forbidden on Armv8 with acquire loads but allowed by C11? How would you use litmus tests to validate new silicon?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st     # 9/45 passed, exit 1
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so    # 45/45 passed
```

시작 코드 실제 출력 (끝 부분):

```text
FAIL starter.c:409  atomic_load(&b1.gen) == 5u && atomic_load(&b1.count) == 0u
FAIL starter.c:413  outcome_key(&c, 2, 0) == K2(0, 0) && outcome_key(&c, 2, 1) == K2(1, 0) && outcome_key(&c, 2, 5) == K2(2, 1)
...
SKIP 11 checks (Part A threads): unit checks failed
FAIL starter.c:462  lit_enum_scheds(&TESTS[T_SB], sc, MAXSCHED) == 6
...
SKIP 4 checks (Part B threads): unit checks failed
FAIL starter.c:496  lit_verdict(5, true, true) == VD_SEEN
...
9/45 passed
```

구조: Part 마다 단일 스레드 단위 체크가 다 통과해야 실제 스레드 체크가 돈다 (안 그러면 SKIP = 실패로 셈) — 그래서 시작 코드는 경쟁이 일어나든 말든 **항상 같은 이유로** 실패한다. 모든 스핀에는 상한(`SPIN_LIMIT`, 약 1~수 초)이 있어 하네스가 망가져도 멈추지 않는다.

관찰용 파일 (`demo_litmus.c` 는 `solution.c` 를 `main` 이름만 바꿔 포함):

```bash
cc -std=c11 -O2 -pthread demo_litmus.c -o lit_arm && ./lit_arm 1000000 time 3          # arm64, 100만 × 3회, 약 13 초
clang -arch x86_64 -std=c11 -O2 -pthread demo_litmus.c -o lit_x86 && ./lit_x86 1000000 time 3   # Rosetta 2
./lit_arm 1000000 each ; ./lit_arm 1000000 batch                                      # 동기화 방식 비교
cc -std=c11 -O2 -pthread -DCELL=4 demo_litmus.c -o lit_packed && ./lit_packed 1000000 time   # 변수 빽빽하게
./lit_arm 10000000 time 1 LB                                                          # LB 만 1000만 번
cc -std=c11 -O2 -S solution.c -o - | less            # 변형별 body_* 함수의 실제 기계어
clang -arch x86_64 -std=c11 -O2 -S solution.c -o -   # x86-64
```

## 4. 힌트

<details><summary>힌트 1 — 변형 표는 "무엇을 무엇보다 먼저 보이게 해야 하나" 로</summary>

MP 는 writer 의 data→flag (store→store) 와 reader 의 flag→data (load→load) 두 순서가 필요하다: release store 가 앞의 것을 묶고 acquire load 가 뒤의 것을 묶는다. LB 는 각 스레드의 load→store: acquire load 하나로도 충분하다. SB 는 store→load — release 는 "앞의 것 → 이 store", acquire 는 "이 load → 뒤의 것" 이라 **store 다음 load** 는 어느 쪽도 묶지 않는다. 그래서 SB 만 seq_cst 가 필요하다. 펜스 변형도 같다: SB 는 seq_cst 펜스, MP 는 writer release 펜스 + reader acquire 펜스 (C11 7.17.4 의 fence–fence 동기화), LB 는 양쪽 acq_rel 펜스.
</details>

<details><summary>힌트 2 — 장벽: 세대 번호를 먼저 읽는다</summary>

도착하기 **전에** `g = gen` 을 읽어 둔다. `fetch_add(count)` 의 결과 + 1 이 n 이면 마지막 도착자 — `count = 0` 을 먼저 하고 `gen = g + 1` 을 release 로. 나머지는 `gen != g` 가 될 때까지 acquire load 로 돈다. 순서가 바뀌면 (gen 먼저, count 나중) 빨리 깬 스레드의 다음 `fetch_add` 가 리셋 전 count 에 더해진다. 1 스레드 장벽은 부를 때마다 gen 이 1 씩 오른다 — 체크가 그걸 본다.
</details>

<details><summary>힌트 3 — 인터리빙 열거는 "남은 연산 수" 배열로</summary>

`left[t]` = 스레드 t 가 아직 안 한 연산 수. 위치 pos 에서 t = 0, 1, … 순서로 `left[t] > 0` 인 것을 고르고 재귀, 돌아와서 되돌린다. 다 채우면(`pos == nsteps`) 한 개 완성. 개수는 다항계수 nsteps! / Π nops[t]! — SB 는 4!/(2!2!) = 6, IRIW 는 6!/(1!1!2!2!) = 180.
</details>

<details><summary>힌트 4 — 판정은 "0 인가?" 부터</summary>

0 이면 버그일 수 없다 — 하드웨어 모델이 허용하면 "안 보였을 뿐", 아니면 기대대로. 0 이 아니면: C11 이 금지했나? (그러면 LANG-BUG) → 아니면 하드웨어 모델이 금지했나? (HW-BUG) → 둘 다 허용이면 그냥 관찰.
</details>

## 5. 풀이

### 0. 용어와 규칙 한 장 (자세한 건 C-A03 · MCPP-M13 · ARM-M05)

- **허용(allowed) / 금지(forbidden)** 는 **모델** (C11, ARMv8, x86-TSO) 이 정한다. **관찰(observed)** 은 하드웨어가 정한다. 관찰 1 번 = 그 결과가 그 하드웨어에서 허용된다는 증명. 관찰 0 번 = 아무것도 증명하지 않는다.
- C11 의 금지 근거: release/acquire 짝 = synchronizes-with (C11 5.1.2.4), 펜스 동기화 (7.17.4), seq_cst 연산·펜스의 단일 전체 순서 S (7.17.3). SB 의 (0,0) 은 S 로만 막힌다.
- ARMv8 은 **other-multi-copy-atomic** 이다: 한 코어의 store 가 다른 코어 하나에게 보이면 모든 다른 코어에게도 보인다 (Arm ARM DDI 0487, B2 "The AArch64 Application Level Memory Model"; 2017 년경 개정, Pulte et al. POPL 2018 이 정식화 — 문헌 기준). 그래서 각 스레드 안의 순서만 지키면 IRIW 도 금지된다. POWER 는 그렇지 않다.
- x86-TSO: 각 코어의 store 는 FIFO store 버퍼를 거친다. 허용되는 재배치는 **store → (다른 주소의) load** 하나뿐 (Sewell et al. CACM 2010; Intel SDM Vol.3A "Memory Ordering" 절 — 판본마다 절 번호가 달라 확인 필요).

### Part A — 변형 표의 이유

| 테스트 | 필요한 순서 | acq/rel 로 되나 | 최소 펜스 (FENCE 변형) |
|---|---|---|---|
| SB | 각 스레드 store → load | **안 됨** — release 는 store 앞, acquire 는 load 뒤만 묶는다 | 양쪽 `atomic_thread_fence(seq_cst)` |
| MP | writer store→store, reader load→load | 됨: flag 를 release/acquire 로 | writer release 펜스 + reader acquire 펜스 |
| LB | 각 스레드 load → store | 됨: acquire load (또는 release store) 하나로 | 양쪽 acq_rel 펜스 (서로 동기화) |
| 2+2W | 각 스레드 store → store, 최종값 | **안 됨** (C11) — 읽기가 없어 synchronizes-with 가 안 생긴다 | 양쪽 seq_cst 펜스 |
| IRIW | 두 reader 의 load → load + 쓰기들이 모두에게 같은 순서 | **안 됨** (C11) — seq_cst 만 금지 | (acquire 펜스로는 C11 상 부족) |

LB 의 acq_rel 펜스가 왜 충분한가: r0=1 이면 T0 의 load 가 T1 의 `x=1` 을 읽었다. T1 의 펜스(release 역할)가 그 store 앞, T0 의 펜스(acquire 역할)가 그 load 뒤 → T1 펜스 synchronizes-with T0 펜스 (7.17.4) → T1 의 `r1=y` 가 T0 의 `y=1` 보다 happens-before → `r1=y` 는 `y=1` 을 읽을 수 없다 (read-write coherence). 그래서 r1=0.

`ord` 와 `fence_ord` 는 작은 `switch` 지만 **상수로 인라인되면 사라진다**. 테스트 코드는 연산 k 를 `[from, to)` 일 때만 실행하는 한 함수로 쓰고, 실제 스레드용 `body_*` 는 `(V, 0, MAXOPS)` 상수로, 강제 스케줄용 `step_*` 은 `(k, k+1)` 로 부른다 — 같은 코드가 두 경로에 쓰인다.

### Part A — 실제 기계어 (하네스의 body 함수 그대로)

`cc -std=c11 -O2 -S solution.c` (Apple clang 21, arm64) 과 `clang -arch x86_64 -std=c11 -O2 -S solution.c` 에서 두 메모리 접근 사이의 명령만 추렸다 (주소 계산 `ldr x8,[x0,#8]` 같은 포인터 load 는 생략 — 순서에 영향 없음):

| 함수 | arm64 실제 출력 | x86-64 실제 출력 |
|---|---|---|
| SB T0 relaxed | `str w10,[x8,x9]` … `ldr w8,[x8,x9]` | `movl $1,(%rax,%rsi)` … `movl (%rax,%rsi),%eax` |
| SB T0 acq/rel | `stlr w10,[x8]` … `ldapr w8,[x8]` | `movl` … `movl` (relaxed 와 같음) |
| SB T0 seq_cst | `stlr w10,[x8]` … `ldar w8,[x8]` | `xchgl %ecx,(%rax,%rsi)` … `movl` |
| SB T0 fence | `str` ; `dmb ish` ; `ldr` | `movl` ; `lock orl $0,-64(%rsp)` ; `movl` |
| MP T0 fence (release 펜스) | `str` ; `dmb ish` ; `str` | `movl` ; `movl` (펜스 명령 없음) |
| MP T1 acq/rel | `ldapr` ; `strb` ; `ldapr` | `movl` ; `movb` ; `movl` |
| MP T1 fence (acquire 펜스) | `ldr` ; `strb` ; `dmb ishld` ; `ldr` | `movl` ; `movb` ; `movl` |
| LB T0 acq/rel | `ldapr` ; `strb` ; `stlr` | — |
| LB T0 fence (acq_rel 펜스) | `ldr` ; `strb` ; `dmb ish` ; `str` | `movl` ; `movb` ; `movl` (펜스 없음) |
| 2+2W T0 acq/rel = seq_cst | `stlr` ; `stlr` | — |
| 2+2W T0 fence | `str` ; `dmb ish` ; `str` | — |
| IRIW T2 acq/rel / seq_cst / fence | `ldapr;ldapr` / `ldar;ldar` / `ldr;dmb ishld;ldr` | — |

실제 출력 두 개를 그대로 (앞에서 말한 포인터 load 까지 전부, 지시문만 뺌 — arm64 SB T0 seq_cst 와 x86-64 SB T0 seq_cst):

```text
_body_sb_t0_sc:                         ; arm64          _body_sb_t0_sc:                  ## x86-64
	ldr	x8, [x0]                                       	movq	(%rdi), %rax
	lsl	x9, x1, #7                                     	shlq	$7, %rsi
	add	x8, x8, x9                                     	movl	$1, %ecx
	mov	w10, #1                                        	xchgl	%ecx, (%rax,%rsi)      ; ← store→load 를 막는 자리
	stlr	w10, [x8]                                      	movq	8(%rdi), %rax
	ldr	x8, [x0, #8]                                   	movl	(%rax,%rsi), %eax
	add	x8, x8, x9                                     	movq	16(%rdi), %rcx
	ldar	w8, [x8]                 ; ← acq/rel 이면 ldapr  	movb	%al, (%rcx,%rdx)
	ldr	x9, [x0, #16]                                  	retq
	strb	w8, [x9, x2]
	ret
```

읽을 점:

- arm64 acquire load = `ldapr` (RCpc, FEAT_LRCPC), seq_cst load = `ldar` (RCsc). 둘의 차이가 **정확히 SB** 다: `stlr` 다음 `ldar` 는 순서가 있고 `stlr` 다음 `ldapr` 는 없다 (ARM-M05 가 asm 수준에서 실측).
- release 펜스도 `dmb ish` (전체), acquire 펜스만 `dmb ishld` 로 가볍다.
- x86-64: release store / acquire load / relaxed 가 **모두 그냥 `mov`** 다. TSO 가 이미 그만큼을 보장하기 때문. 유일하게 비싼 것은 store→load 를 막는 자리: seq_cst store = `xchg`, seq_cst 펜스 = `lock orl $0,-64(%rsp)` (이 clang 은 `mfence` 대신 스택에 대한 lock 접두 RMW 를 낸다 — 실제 출력). release/acquire/acq_rel 펜스는 명령 0 개(컴파일러 배리어만).
- 2+2W 의 release store 는 arm64 에서 `stlr; stlr` — Arm 규칙상 `stlr` 은 앞의 모든 접근 뒤에 보이므로 하드웨어는 2+2W 를 금지한다. **C11 은 허용** 하는데 이 컴파일 결과가 더 강할 뿐이다 (다른 컴파일러·다른 ISA 에서는 아닐 수 있다).

### Part A — 하네스: 왜 이렇게 생겼나

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">출발 동기화가 겹침을 만든다 — 반복마다 장벽 vs 공통 시간축</text>
  <text x="10" y="44" fill="currentColor" font-weight="bold">M_EACH (반복마다 스핀 장벽)</text>
  <g stroke="currentColor">
    <path d="M90 62 H720"/><path d="M90 98 H720"/>
  </g>
  <g fill="currentColor"><text x="10" y="66">T0 (마지막)</text><text x="10" y="102">T1 (도는 쪽)</text></g>
  <rect x="100" y="54" width="18" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="120" y="54" width="40" height="16" class="f-acc" stroke="currentColor"/>
  <text x="124" y="66" fill="currentColor">x=1;y?</text>
  <rect x="100" y="90" width="110" height="16" class="f-mut" stroke="currentColor"/>
  <text x="104" y="102" fill="currentColor">gen 바뀜 보기 ~수십 ns</text>
  <rect x="212" y="90" width="40" height="16" class="f-acc" stroke="currentColor"/>
  <text x="216" y="102" fill="currentColor">y=1;x?</text>
  <text x="270" y="84" fill="currentColor">→ T0 끝난 뒤 T1 시작: 겹침 거의 없음</text>
  <text x="10" y="144" fill="currentColor" font-weight="bold">M_TIME (mach_absolute_time 의 같은 눈금에 출발)</text>
  <g stroke="currentColor">
    <path d="M90 162 H720"/><path d="M90 198 H720"/>
  </g>
  <g stroke="currentColor" stroke-dasharray="2 3">
    <path d="M200 150 V210"/><path d="M380 150 V210"/><path d="M560 150 V210"/>
  </g>
  <g fill="currentColor"><text x="10" y="166">T0</text><text x="10" y="202">T1</text></g>
  <rect x="202" y="154" width="40" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="204" y="190" width="40" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="382" y="154" width="40" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="381" y="190" width="40" height="16" class="f-acc" stroke="currentColor"/>
  <text x="430" y="184" fill="currentColor">점선 = 200 ns 간격의 출발 시각 t0 + i·gap</text>
  <text x="10" y="232" fill="currentColor">두 코어가 같은 카운터 눈금 변화를 거의 동시에 본다 → 몸체가 겹친다 → store 가 버퍼에 있을 때 상대가 load.</text>
</svg>
```

설계 선택과 이유:

- **반복마다 새 변수**: `x[i]`, `y[i]` 는 반복 i 전용. 지난 반복의 값이 남아 있으면 "옛 1" 을 읽어 결과가 가려진다 (연습 3 이 강제 스케줄로 정확히 보여 준다). 배치 끝에서 스레드 0 이 지우고, 다음 배치 시작 장벽이 그 지우기를 모두에게 happens-before 로 넘긴다.
- **한 칸 = 128 B** (`_Alignas(128)`, `sysctl hw.cachelinesize` = 128): 이웃 반복·다른 변수와 줄을 공유하지 않는다. `-DCELL=4` 로 빽빽하게 만들면 빈도가 크게 바뀐다 (아래 표).
- **결과는 배열에, 집계는 나중에**: 몸체 안에서 히스토그램을 만지면 그 접근 자체가 타이밍을 바꾸고 공유 줄을 만든다. 몸체는 `strb` 하나만 한다.
- **공유 변수는 모두 `_Atomic`**: plain 변수로 리트머스를 짜면 C11 데이터 레이스 = UB 이고, 컴파일러가 load 를 합치거나 옮겨도 된다 — 하드웨어가 아니라 컴파일러를 재는 꼴이 된다. relaxed 원자 = 그냥 `ldr`/`str` (위 표).
- 맥에는 코어 고정이 없다 (`sched_setaffinity` 없음, LLAT-M05 · ARM-M05). QoS 도 안 줬다 → 스레드는 P/E 아무 데나 앉는다. 숫자는 실행마다 흔들린다.

`demo_litmus.c`:

<!-- code: demo_litmus.c -->

### Part A — 실측: Apple M6 (arm64) vs Rosetta 2 (x86-64)

기계: `sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu` = `Apple M6` / 2 / 4 / 6, `cc --version` = `Apple clang version 21.0.0 (clang-2100.0.123.102)`. `./lit_arm 1000000 time 3` 와 `./lit_x86 1000000 time 3` (한 칸 = 100만 회 실행 1 번). 두 세트를 다른 시각에 돌렸다 — 10 개 레슨 작성 세션이 동시에 벤치마크를 돌리는 중이라 **절대값보다 0 이냐 아니냐와 비율** 을 보라.

세트 1 — `uptime` load 7.54 → 7.70 (arm64), 7.70 → 8.77 (x86):

| 테스트 · 변형 | arm64 (M6) 3 회 | x86-64 Rosetta 3 회 | C11 | ARMv8 | TSO |
|---|---|---|---|---|---|
| SB relaxed | 345663 · 345000 · 346898 | 770911 · 765504 · 785648 | 허용 | 허용 | 허용 |
| SB acq/rel | 394 · 843 · 316 | 912892 · 979280 · 977579 | 허용 | 허용 | 허용 |
| SB seq_cst / fence | 0 ×6 | 0 ×6 | 금지 | 금지 | 금지 |
| MP relaxed | 41736 · 32035 · 28692 | **0 · 0 · 0** | 허용 | 허용 | 금지 |
| MP acq/rel / seq_cst / fence | 0 ×9 | 0 ×9 | 금지 | 금지 | 금지 |
| LB relaxed | **0 · 0 · 0** | 0 · 0 · 0 | 허용 | 허용 | 금지 |
| LB 나머지 3 변형 | 0 ×9 | 0 ×9 | 금지 | 금지 | 금지 |
| IRIW relaxed | 870 · 1156 · 2624 | **0 · 0 · 0** | 허용 | 허용 | 금지 |
| IRIW acq/rel / fence | 0 ×6 | 0 ×6 | **허용** | 금지 | 금지 |
| IRIW seq_cst | 0 ×3 | 0 ×3 | 금지 | 금지 | 금지 |
| 2+2W relaxed | 103879 · 76299 · 64623 | **0 · 0 · 0** | 허용 | 허용 | 금지 |
| 2+2W acq/rel | 0 ×3 | 0 ×3 | **허용** | 금지 | 금지 |
| 2+2W seq_cst / fence | 0 ×6 | 0 ×6 | 금지 | 금지 | 금지 |

세트 2 — load 4.01 → 4.76 (arm64), 4.76 → 9.08 (x86): arm64 SB relaxed 192077 · 366806 · 398789, SB acq/rel 684 · 23 · 3089, MP relaxed 18237 · 21514 · 17182, IRIW relaxed 156 · 487 · 157, 2+2W relaxed 14557 · 16272 · 9865, LB 전부 0. Rosetta SB relaxed 987676 · 984036 · 987260, SB acq/rel 991097 · 990915 · 989015, 나머지 전부 0. 금지 칸은 두 세트 모두 0.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">이상한 결과 / 100만 회 — 세트 1 의 3 회 평균, 로그 눈금 (실측)</text>
  <g stroke="currentColor" stroke-dasharray="2 3">
    <path d="M230 30 V300"/><path d="M302 30 V300"/><path d="M374 30 V300"/><path d="M446 30 V300"/><path d="M518 30 V300"/><path d="M590 30 V300"/><path d="M662 30 V300"/>
  </g>
  <g fill="currentColor">
    <text x="226" y="316">1</text><text x="296" y="316">10</text><text x="364" y="316">100</text><text x="434" y="316">10³</text><text x="506" y="316">10⁴</text><text x="578" y="316">10⁵</text><text x="650" y="316">10⁶</text>
    <text x="10" y="52">SB relaxed</text><text x="10" y="90">SB acq/rel</text><text x="10" y="128">MP relaxed</text>
    <text x="10" y="166">LB relaxed</text><text x="10" y="204">IRIW relaxed</text><text x="10" y="242">2+2W relaxed</text>
    <text x="10" y="280">C11 금지 칸 전부</text>
  </g>
  <rect x="230" y="38" width="399" height="13" class="f-warn" stroke="currentColor"/>
  <rect x="230" y="53" width="424" height="13" class="f-acc" stroke="currentColor"/>
  <rect x="230" y="76" width="195" height="13" class="f-warn" stroke="currentColor"/>
  <rect x="230" y="91" width="431" height="13" class="f-acc" stroke="currentColor"/>
  <rect x="230" y="114" width="326" height="13" class="f-warn" stroke="currentColor"/>
  <rect x="230" y="190" width="230" height="13" class="f-warn" stroke="currentColor"/>
  <rect x="230" y="228" width="354" height="13" class="f-warn" stroke="currentColor"/>
  <g class="f-ok" stroke="currentColor">
    <rect x="230" y="129" width="4" height="13"/><rect x="230" y="152" width="4" height="13"/><rect x="230" y="167" width="4" height="13"/>
    <rect x="230" y="205" width="4" height="13"/><rect x="230" y="243" width="4" height="13"/><rect x="230" y="266" width="4" height="13"/>
  </g>
  <g fill="currentColor">
    <text x="634" y="49">345854</text><text x="658" y="64">774021</text>
    <text x="430" y="87">518</text><text x="665" y="102">956584</text>
    <text x="561" y="125">34154</text><text x="240" y="140">0 (Rosetta)</text>
    <text x="240" y="163">0 / 1600만 (arm64: 600만 + 1000만)</text><text x="240" y="178">0 (Rosetta)</text>
    <text x="465" y="201">1550</text><text x="240" y="216">0 (Rosetta)</text>
    <text x="589" y="239">81600</text><text x="240" y="254">0 (Rosetta)</text>
    <text x="240" y="277">0 — arm64 · Rosetta 모두, 모든 실행</text>
  </g>
  <rect x="10" y="328" width="12" height="10" class="f-warn" stroke="currentColor"/><text x="28" y="337" fill="currentColor">arm64 네이티브 (M6)</text>
  <rect x="180" y="328" width="12" height="10" class="f-acc" stroke="currentColor"/><text x="198" y="337" fill="currentColor">같은 소스 -arch x86_64 (Rosetta 2)</text>
  <rect x="430" y="328" width="12" height="10" class="f-ok" stroke="currentColor"/><text x="448" y="337" fill="currentColor">관찰 0</text>
</svg>
```

히스토그램 그대로 (세트 1 첫 실행, `*` = 이상한 결과):

```text
arch=arm64 translated(Rosetta)=0  N=1000000  barrier=timebase  cell=128B  runs=3
SB   relaxed  weird=345663   seen            0.22s | [r0=0 r1=0]*345663 [r0=1 r1=0]:289371 [r0=0 r1=1]:328534 [r0=1 r1=1]:36432
SB   seq_cst  weird=0        zero(ok)        0.22s | [r0=1 r1=0]:494840 [r0=0 r1=1]:479491 [r0=1 r1=1]:25669
MP   relaxed  weird=41736    seen            0.22s | [flag=0 data=0]:613295 [flag=1 data=0]*41736 [flag=0 data=1]:43881 [flag=1 data=1]:301088
MP   acq/rel  weird=0        zero(ok)        0.22s | [flag=0 data=0]:699750 [flag=0 data=1]:44979 [flag=1 data=1]:255271
LB   relaxed  weird=0        allowed-unseen  0.22s | [r0=0 r1=0]:482253 [r0=1 r1=0]:249664 [r0=0 r1=1]:268083
arch=x86_64 translated(Rosetta)=1  N=1000000  barrier=timebase  cell=128B  runs=3
SB   relaxed  weird=770911   seen            0.21s | [r0=0 r1=0]*770911 [r0=1 r1=0]:117227 [r0=0 r1=1]:111793 [r0=1 r1=1]:69
SB   seq_cst  weird=0        zero(ok)        0.21s | [r0=1 r1=0]:39810 [r0=0 r1=1]:54207 [r0=1 r1=1]:905983
MP   relaxed  weird=0        zero(ok)        0.21s | [flag=0 data=0]:988002 [flag=0 data=1]:3236 [flag=1 data=1]:8762
```

SB 의 결과 네 개 중 SC 가 허용하는 세 개가 모두 실제로 나온다 — 하네스가 양쪽 스레드를 겹치게 만들고 있다는 뜻이다 (`each` 모드의 SB 는 (1,1) 이 100만 번 중 0 ~ 123 번뿐이었다).

읽는 법:

1. **C11 금지 칸 = 모든 실행에서 0** — 하드웨어와 clang 매핑이 약속을 지켰다는 확인(증명은 아님). 이 레슨의 체크가 매 실행 이것을 다시 본다.
2. **ARM 에서는 보이고 TSO 에서는 안 보이는 것: MP · 2+2W · IRIW(relaxed).** 셋 다 store→store 또는 load→load 재배치가 필요하고 TSO 는 둘 다 금지한다. Rosetta 쪽 MP 는 `flag=1` 인 반복 자체가 1% 정도로 적었지만 (세트 1 첫 실행 `flag=1 data=1` 8762 회), arm64 처럼 `flag=1` 중 약 12% 가 이상했다면 300만 회에 수천 번은 나왔어야 한다 — 0 은 우연이 아니다.
3. **SB 는 둘 다 보인다** — x86 에서는 오히려 77~99% 로 압도적이다. TSO 가 허용하는 유일한 재배치가 바로 이것이다. acq/rel 이 relaxed 와 같은 `mov` 라 Rosetta 에서 둘 다 똑같이 깨진다. seq_cst(`xchg`)·펜스(`lock or`)만 0.
4. **arm64 의 SB acq/rel 은 relaxed 보다 약 670 배 적지만 0 은 아니다** (518 / 100만). `stlr` 다음 `ldapr` 은 RCpc 라 앞지를 수 있다 — OS-A03 `demo_sb` (1200만 중 2), C-A03 (8 실행 중 1 번 18 회), ARM-M05 asm (13 회 중 9 회) 와 같은 결론, 이 하네스가 더 자주 잡을 뿐이다.
5. **IRIW relaxed 는 ARM 에서 나오지만 non-multi-copy-atomic 의 증거가 아니다**: reader 의 두 `ldr` 이 서로 재배치되면(load→load) 충분하다. acquire(`ldapr;ldapr`)·`dmb ishld` 로 reader 순서를 묶으면 0 — ARMv8 이 MCA 라서 이게 금지인 것과 일치. C11 은 여전히 허용한다 (POWER 같은 non-MCA 하드웨어에서는 실제로 나온다 — 문헌 기준, 이 맥에서는 확인 불가).
6. **LB 는 ARMv8 이 허용하지만 arm64 합계 1600만 번(세트 1·2 의 600만 + LB 만 1000만, load 11) 중 0** — "허용 ≠ 관찰". LB 가 나오려면 load 가 끝나기 전에 뒤의 store 가 다른 코어에 보여야 하는데, 대부분의 비순차 코어는 store 를 은퇴(retire) 뒤에야 내보내고 은퇴는 앞의 load 가 끝나야 한다 (일반적 구현 설명, M6 내부는 미공개). 0 번이라도 95% 상한은 3/1600만 ≈ 1.9e-7 일 뿐이다 (연습 2). 같은 1000만 회 실행의 히스토그램은 (0,0) 252만 · (1,0) 355만 · (0,1) 393만 — 두 스레드가 잘 겹쳤는데도 (1,1) 은 없었다.

### Part A — 하네스 설계가 숫자를 바꾼다

같은 소스 arm64, 100만 회씩 1 번 (`time` 은 세트 1 의 첫 실행 load 7.5, 나머지 load 8.3–8.5):

| 동기화 / 배치 | SB relaxed | SB acq/rel | MP relaxed | IRIW relaxed | 2+2W relaxed |
|---|---|---|---|---|---|
| `each` 반복마다 스핀 장벽 | 170 | 0 | 2 | 21 | 0 |
| `batch` 배치 시작에만 | 722018 | 0 | 8971 | 0 | 189 |
| `time` 공통 시간축 (기본) | 345663 | 394 | 41736 | 870 | 103879 |
| `time` + `-DCELL=4` (빽빽한 배열) | 292896 | **42045** | 49604 | 431 | 112442 |

- `each` 는 마지막 도착자가 먼저 출발하고, 도는 쪽은 `gen` 변화를 보는 데 수십 ns 가 걸려 두 몸체가 거의 안 겹친다 (히스토그램이 (1,0)≈(0,1)≈50%). OS-A03 의 `demo_sb` 가 "0 이냐 아니냐만 보라" 고 한 이유가 이것이다. 이 장벽만 썼다면 "MP 2 번, 2+2W 0" 이라고 보고했을 것이다.
- `batch` 는 스레드가 배열을 연속으로 훑어 store 가 store 버퍼에 **여러 개** 쌓인다 → relaxed SB 72%. 그러나 acq/rel 은 0: 반복 i 의 `stlr` 은 반복 i-1 의 `ldapr` 뒤에, 반복 i 의 `ldapr` 은 반복 i+1 의 `stlr` 앞에 묶여 in-flight store 가 하나뿐이다 (Arm 규칙에서 나오는 추론).
- `CELL=4` 는 이웃 반복의 변수가 같은 줄에 산다 → 상대 코어가 방금 읽은 줄에 쓰려면 소유권(RFO, MEM-M04 · ARCH-A04)을 가져와야 해서 store 가 버퍼에 오래 머문다 → `stlr; ldapr` 창이 80 배 넓어진다 (원인은 가설). 변수 배치도 리트머스 파라미터다 — litmus7 이 메모리 배치·stride 옵션을 두는 이유 (옵션 이름은 litmus7 문서 기준, 미검증).
- 그래서 실리콘 검증에서 리트머스는 **여러 동기화 방식 × 배치 × 부하** 로 돌린다. "0 번" 의 신뢰는 그 조합의 폭에서 나온다.

### Part B — 강제 스케줄: 손으로 센 SC 결과

SB 의 6 개 인터리빙 (a0: `x=1`, a1: `r0=y`, b0: `y=1`, b1: `r1=x`):

| # | 순서 | (r0, r1) |
|---|---|---|
| 1 | a0 a1 b0 b1 | (0, 1) |
| 2 | a0 b0 a1 b1 | (1, 1) |
| 3 | a0 b0 b1 a1 | (1, 1) |
| 4 | b0 a0 a1 b1 | (1, 1) |
| 5 | b0 a0 b1 a1 | (1, 1) |
| 6 | b0 b1 a0 a1 | (1, 0) |

(0,0) 은 없다 — 그게 "이상한" 의 정의다. 같은 방법으로 MP (flag, data) = (1,1)×1, (0,1)×4, (0,0)×1, **(1,0)×0**; LB = (0,0)×4, (0,1)×1, (1,0)×1, **(1,1)×0**; 2+2W 최종 (x, y) = (2,1)×1, (2,2)×4, (1,2)×1, **(1,1)×0**; IRIW 는 180 개 인터리빙에서 서로 다른 결과 15 개 (16 개 중 weird 하나만 빠짐). 체크가 이 숫자들을 정확히 본다.

차례 토큰으로 실제 스레드에서 같은 스케줄을 돌리는 방법: 반복 j 의 pos 번째 연산 차례 번호 = `j × nsteps + pos`. 스레드는 `turn` 이 자기 차례가 될 때까지 acquire load 로 돌고, 연산 하나를 한 뒤 `turn = 차례+1` 을 release 로 쓴다. release → acquire 사슬이 연산마다 happens-before 를 이어 붙이므로, 그 인터리빙을 **SC 로 실행한 것과 같은 결과가 매번** 나온다. 200 반복 × 6 스케줄 = 1200 반복의 히스토그램 = SC 히스토그램 × 200 — 실제 스레드, 실제 장벽, 실제 기록·집계 경로가 모두 맞다는 결정적 증거다.

이 열거가 **못 하는 것**: SC 인터리빙만 만들기 때문에 약한 결과(weird)는 절대 나오지 않는다. "무엇이 허용되나" 를 모델별로 계산하는 건 공리 모델의 일이다 — MEM-M07 (C11/TSO/ARMv8 열거기), RISCV-A04 (RVWMO), herd7. 이 레슨의 열거는 "SC 기준 집합" 과 "하네스 자가 검사" 용이다.

### Part C — 판정과 회귀 게이트

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">lit_verdict — 관찰 한 칸을 판정하는 순서</text>
  <rect x="10" y="40" width="130" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor">weird == 0 ?</text>
  <rect x="200" y="30" width="150" height="24" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="208" y="47" fill="currentColor">HW 허용 → allowed-unseen</text>
  <rect x="200" y="60" width="150" height="24" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="208" y="77" fill="currentColor">HW 금지 → zero(ok)</text>
  <rect x="10" y="120" width="130" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="22" y="142" fill="currentColor">C11 금지 ?</text>
  <rect x="200" y="108" width="190" height="24" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="208" y="125" fill="currentColor">예 → LANG-BUG (매핑·실리콘·하네스)</text>
  <rect x="200" y="140" width="190" height="24" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="208" y="157" fill="currentColor">아니오 → HW 모델 금지 ?</text>
  <rect x="440" y="124" width="140" height="24" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="448" y="141" fill="currentColor">예 → HW-BUG</text>
  <rect x="440" y="156" width="140" height="24" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="448" y="173" fill="currentColor">아니오 → seen</text>
  <g stroke="currentColor" fill="none">
    <path d="M140 52 H200"/><path d="M140 66 L200 72"/><path d="M75 74 V120"/>
    <path d="M140 132 L200 120"/><path d="M140 142 L200 152"/><path d="M390 152 L440 136"/><path d="M390 152 L440 168"/>
  </g>
  <text x="84" y="100" fill="currentColor">아니오 (나왔다)</text>
  <text x="10" y="200" fill="currentColor">버그 = HW-BUG 또는 LANG-BUG. 같은 관찰도 어느 HW 모델로 보느냐에 따라 판정이 달라진다 (연습 4).</text>
</svg>
```

- **LANG-BUG**: C11 이 금지했는데 나왔다. 프로그램은 맞다. 그러면 ① 컴파일러가 잘못 매핑했거나 (예: acquire 를 `ldr` 로), ② 실리콘이 자기 아키텍처 규칙을 어겼거나, ③ 하네스가 틀렸다 (변수 재사용, 기록 실수). 체크의 "합성 버그 칩" 은 MP acq/rel 에서 3/100만 → `LANG-BUG` 를 찍는다.
- **HW-BUG**: C11 은 허용(프로그램이 이미 틀림)인데 하드웨어 모델은 금지했는데 나왔다 — 예: x86 칩에서 relaxed MP 가 나옴. 주의: relaxed 는 **컴파일러가** 재배치해도 되므로 먼저 `-S` 를 본다.
- **allowed-unseen**: 이 칸이 커버리지 구멍이다. "LB 는 우리 칩에서 안 나온다" 는 사양이 아니라 관찰이고, 다음 스테핑이나 다른 DVFS 점에서 나올 수 있다. 소프트웨어는 **모델** 에 맞춰 짠다.

실리콘 검증과의 연결: 새 CPU/인터커넥트의 메모리 순서 검증은 대체로 이 모양이다 — diy 로 수백~수천 개 리트머스를 생성 → litmus7 으로 실제 칩에서 여러 모드로 실행 → herd7 모델의 허용 집합과 비교 → **금지 결과가 한 번이라도 나오면 버그 리포트**. Alglave et al. 의 논문들은 이런 식으로 실제 칩의 위반(예: Cortex-A9 의 같은 주소 load→load 순서 위반, Arm 이 erratum 으로 인정 — 문헌 기준, 미검증)을 찾았다. 펌웨어 쪽:

- **단일 코어 Cortex-M**: 한 코어가 자기 자신에게 보는 일반 메모리 순서는 프로그램 순서 그대로라 이 레슨의 재배치는 ISR ↔ main 사이에서 생기지 않는다 (컴파일러 재배치는 여전히 — C-M14, C-A02). 그러나 **장치(Device) 메모리 · DMA · 버스 마스터** 가 끼면 "다른 관찰자" 가 생긴다: DMA 디스크립터 OWN 비트 앞 `dmb`, 캐시 clean 뒤 `dsb` (C-A02, ARCH-M12). (Arm 문서 기준 요약 — 코어·메모리 속성마다 세부가 다르다.)
- **멀티코어 MCU**: STM32H7 듀얼(M7 + M4), nRF5340(M33 × 2), RP2040(M0+ × 2)의 공유 SRAM 메일박스는 이 레슨의 MP 그 자체다. M7 은 쓰기 버퍼·캐시가 있어 순서와 가시성 둘 다 챙겨야 하고 (DMB + 캐시 유지보수 또는 non-cacheable MPU 영역), M0+ 는 단순하지만 컴파일러 배리어 + `dmb` 를 습관으로 둔다 (CPP-A04, ARM-M11). 각 칩의 버스 순서 보장은 레퍼런스 매뉴얼로 확인할 것 — 여기서는 확인 안 함.
- **멀티코어 Cortex-A/Apple**: 이 레슨의 표가 그대로 적용된다. x86 에서 옮겨 온 lock-free 코드가 깨지는 곳 = MP/2+2W/IRIW 칸.

### Rosetta 2 에 대해 (측정한 것과 아닌 것)

측정한 것: 같은 C 소스의 x86-64 바이너리가 Rosetta 2 아래에서 MP · LB · IRIW · 2+2W 의 이상한 결과를 600만 회 중 0 번, SB 는 77~99% 냈다 = **x86-TSO 의 예측과 정확히 일치**. 번역된 x86 `mov` 가 ARM 의 plain `ldr`/`str` 이었다면 MP 가 나왔어야 한다. 측정하지 않은 것: 그 이유. 공개된 분석들은 Apple 코어에 스레드별 **하드웨어 TSO 모드** 가 있고 Rosetta 2 가 번역된 프로세스에서 그것을 켠다고 설명한다 — 이 레슨에서는 확인 안 함. 또한 Rosetta 는 실제 x86 칩이 아니다: 빈도(SB 77~99%)는 진짜 Intel/AMD 와 다를 수 있다. TSan 빌드에서는 원자 연산이 런타임 호출로 바뀌어 허용 칸도 0 이 나왔다 (`info ... SB/relaxed=0`) — 리트머스는 sanitizer 없이 잰다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_asan && ./so_asan
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan
```

실제 출력 (`./so`, load 4.2):

```text
info  N=100000 per run, weird outcomes in ALLOWED variants:  SB/relaxed=31880  SB/acq/rel=0  MP/relaxed=3329  LB/relaxed=0
gate  synthetic chip: MP/acq/rel weird=3/1000000 -> LANG-BUG
45/45 passed
```

`info` 줄은 실행마다 바뀐다 (체크 아님). sanitizer 빌드는 `N_CHECK` 가 10만 → 4000 으로 자동으로 줄어든다 (`__has_feature(thread_sanitizer)` / `address_sanitizer`); 둘 다 45/45, 경고 0. `clang -arch x86_64` 로 빌드해 Rosetta 에서 돌려도 45/45 (`SB/relaxed=93828 SB/acq/rel=95794 MP/relaxed=0`).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|
| 리트머스 변수를 plain `int` 로 | 데이터 레이스 = UB. 컴파일러가 load 를 합치거나 store 를 옮겨도 된다 → 하드웨어가 아니라 컴파일러를 잰다 | `_Atomic` + relaxed (기계어는 같은 `ldr`/`str`) |
| 변수 하나를 모든 반복에 재사용 | 지난 반복의 값이 남거나, 리셋 자체가 경쟁 → 결과가 가려지거나 가짜 weird | 반복마다 새 칸, 리셋은 장벽 사이에서 (연습 3) |
| 반복마다 장벽 하나만 | 마지막 도착자가 먼저 달려 겹침이 거의 없다 → 0 에 가까운 숫자 (SB 170 vs 345663) | 공통 시간축 출발, 여러 모드로 |
| "1000만 번 0 → 금지" | 0/n 의 95% 상한 ≈ 3/n 일 뿐. 모델이 허용하면 언젠가 나온다 (LB) | 정확성은 모델로, 리트머스는 금지 칸 확인용 |
| SB 를 acq/rel 로 막으려 함 | `stlr; ldapr` 은 RCpc — store→load 순서 없음. 실측 518/100만 | 양쪽 seq_cst (`stlr; ldar`) 또는 양쪽 seq_cst 펜스 |
| 펜스를 한쪽만 | 리트머스는 양쪽 순서가 다 필요 (OS-A03 `RV/WV` 실측: 한쪽 seq_cst 도 깨짐) | 두 스레드 모두 |
| TSan 빌드로 재배치를 셈 | TSan 은 원자 연산을 런타임 호출로 바꿔 타이밍·순서가 달라진다 → 허용 칸도 0 | 리트머스는 `-O2` 일반 빌드, TSan 은 레이스 검사용 |
| Rosetta 결과 = x86 칩 결과 | 같은 모델(TSO)의 예측과 일치할 뿐, 빈도와 내부 동작은 다르다 | "TSO 모델 확인" 으로만 말하기 |
| relaxed 에서 x86 이 0 이라 "x86 에선 relaxed 로 충분" | 컴파일러는 relaxed 를 재배치해도 된다. 오늘의 clang 이 안 했을 뿐 | C11 규칙대로 release/acquire |

## 8. 면접 Q&A

<details><summary>Q1. Apple Silicon(ARM)에서는 보이고 x86(TSO)에서는 안 보이는 재배치는?</summary>

store→store 와 load→load 재배치가 필요한 것들: MP (flag=1 인데 data=0), 2+2W (최종값이 각 스레드의 첫 store), relaxed IRIW, 그리고 LB (ARM 은 허용하지만 M6 에서는 1600만 번 0). 같은 C 소스를 arm64 와 Rosetta 2 로 돌린 실측 (세트 1 평균): MP relaxed 3.4% vs 0, 2+2W 8.2% vs 0, IRIW 0.16% vs 0. 반대로 SB(store→load)는 둘 다 보인다 — x86 에서도 77~99%. 꼬리: "x86 에서 SB 를 막으려면?" → `mfence` 나 lock 접두 명령. C11 seq_cst store 가 x86 에서 `xchg`, seq_cst 펜스가 `lock orl $0,-64(%rsp)` 로 나오는 이유가 이것이다 (clang 21 실측). 나머지 순서(release/acquire)는 x86 에서 그냥 `mov`.
</details>

<details><summary>Q2. LB 를 1000만 번 돌렸는데 0 입니다. LB 는 금지인가요?</summary>

아니다. ARMv8 아키텍처는 LB 를 허용한다 (C11 도). 0 은 "이 칩, 이 하네스, 이 부하에서 안 나왔다" 이고, 통계적으로는 "확률이 약 3/1000만 = 3e-7 미만일 가능성이 95%" 일 뿐이다 (rule of three: 0/n 이면 95% 상한 ≈ 3/n). LB 는 load 가 끝나기 전에 뒤 store 가 보여야 해서 대부분의 구현에서 드물다. 꼬리: "그럼 relaxed LB 코드를 써도 되나?" → 안 된다. 다음 칩·다른 벤더에서 나올 수 있고, 컴파일러도 재배치할 수 있다. 소프트웨어는 모델에 맞추고, 리트머스의 0 은 "금지 칸이 0 인지" 를 볼 때만 의미가 있다. 반대로 금지 칸에서 1 이 나오면 그건 증명이다.
</details>

<details><summary>Q3. 왜 acquire/release 로 SB 를 못 막나요? 기계어로 설명해 주세요.</summary>

release 는 "앞의 접근 → 이 store", acquire 는 "이 load → 뒤의 접근" 만 묶는다. SB 는 "내 store → 내 다음 load" 순서가 필요한데 어느 쪽에도 해당하지 않는다. arm64 에서 clang 은 release store = `stlr`, acquire load = `ldapr` (RCpc) 를 낸다. Arm 규칙의 "Store-Release 는 뒤의 Load-Acquire 보다 먼저 관찰" 은 RCsc 인 `ldar` 에만 적용되고 `ldapr` 은 빠진다 — 그래서 seq_cst load 만 `ldar` 로 바뀌고, 그 한 글자 차이로 SB 가 518/100만 → 0. 꼬리: "Dekker/Peterson 을 acq/rel 로 짜면?" → 둘 다 임계 구역에 들어간다 (C-A03 연습). "RCU 리더 진입은?" → 같은 SB 모양이라 seq_cst (OS-A03).
</details>

<details><summary>Q4. 리트머스 하네스를 짤 때 무엇이 결과를 좌우하나요?</summary>

① 동기화: 반복마다 스핀 장벽만 쓰면 마지막 도착자가 먼저 출발해 겹침이 없어진다 — 이 맥에서 SB relaxed 170 vs 공통 시간축 345663 (2000 배). ② 변수: 반복마다 새 변수 (재사용하면 옛 값이 결과를 가린다), 캐시 라인 배치 — 빽빽한 배열(`CELL=4`)에서 SB acq/rel 이 80 배 늘었다. ③ 기록: 몸체는 결과를 배열에 쓰기만, 집계는 끝나고. ④ 공유 변수는 relaxed 원자 (UB·컴파일러 재배치 배제). ⑤ 측정 환경: sanitizer 없이, 여러 부하에서, 여러 번. litmus7 이 바로 이 축들(장벽 종류, 메모리 배치, 반복 크기)을 옵션으로 둔다. 꼬리: "하네스 자체가 맞다는 걸 어떻게 아나?" → 강제 스케줄: 모든 SC 인터리빙을 차례 토큰으로 실제 스레드에서 돌려 히스토그램이 손 계산과 정확히 같은지 본다 (Part B).
</details>

<details><summary>Q5. multi-copy atomicity 가 뭐고, IRIW 와 어떤 관계인가요?</summary>

store 하나가 모든 다른 코어에게 "동시에" 보이느냐다. ARMv8 (2017 년경 개정 이후, 문헌 기준) 은 other-multi-copy-atomic: 다른 코어 하나에게 보였으면 모두에게 보인다 (자기 코어는 store 버퍼 포워딩으로 먼저 볼 수 있어서 "other"). 그래서 IRIW 의 두 reader 가 각자 load 순서만 지키면 (`ldapr;ldapr` 나 `ldr;dmb ishld;ldr`) "x 먼저 봤다 / y 먼저 봤다" 로 갈릴 수 없다 — 실측 600만 회 0. POWER 는 non-MCA 라 `lwsync` 로는 IRIW 가 나온다 (문헌 기준). C11 은 POWER 를 위해 IRIW 를 acquire 로는 허용하고 seq_cst 로만 금지한다. 꼬리: "relaxed IRIW 가 M6 에서 1550/100만 나왔다 — ARM 이 non-MCA 라는 뜻?" → 아니다. reader 의 두 `ldr` 이 재배치되면 충분하다 (load→load). reader 순서를 묶으면 0.
</details>

<details><summary>Q6. Rosetta 2 는 x86 코드를 어떻게 ARM 에서 올바르게 돌리나요? 측정으로 뭘 말할 수 있나요?</summary>

측정으로 말할 수 있는 것: 같은 C 소스의 x86-64 바이너리가 Rosetta 아래에서 MP·LB·IRIW·2+2W 를 0 번, SB 를 대부분 냈다 = x86-TSO 의 예측과 일치. 번역된 `mov` 가 ARM 의 plain load/store 였다면 MP 는 네이티브처럼 3% 나왔어야 한다. 이유(공개 분석 기준, 미검증): Apple 코어는 스레드별 하드웨어 TSO 모드를 갖고 Rosetta 가 그것을 켠다. 대안 — 모든 load 를 `ldapr`, 모든 store 를 `stlr` 로 번역 — 도 TSO 를 만들 수 있지만 느리다. 꼬리: "다른 ARM 서버에서 x86 에뮬레이터는?" → 하드웨어 TSO 모드가 없으면 그런 강한 명령/배리어를 넣어야 해서 비용이 크다 (일반론).
</details>

<details><summary>Q7. 새 칩의 메모리 순서 구현을 검증하라는 일을 받으면?</summary>

① 아키텍처 모델(herd7 의 aarch64.cat 등)로 허용 집합을 계산한 리트머스 묶음을 준비 (diy 로 생성: MP, SB, LB, IRIW, WRC, 2+2W, coRR 같은 같은 주소 테스트, 배리어·의존성 변형). ② litmus7 같은 하네스로 실제 칩에서 여러 모드 — 동기화 방식, 메모리 배치, 스레드↔코어 배치(클러스터 안/밖), 캐시 상태, DVFS 점, 백그라운드 트래픽 — 로 수억 회. ③ 금지 칸에서 1 번이라도 나오면 버그 (재현 조건 기록, 칩 설계팀으로). ④ 허용인데 안 나온 칸은 커버리지 리포트 (스트레스 방법을 바꿔 다시). 꼬리: "실리콘 전에는?" → RTL 시뮬레이션/에뮬레이션에서 같은 리트머스 + 버스 모니터의 순서 검사, 프로토콜 수준은 ARCH-A04 같은 일관성 불변식 검사.
</details>

<details><summary>Q8. 단일 코어 Cortex-M4 펌웨어에도 이게 필요한가요?</summary>

CPU 끼리의 재배치(이 레슨의 SB/MP)는 코어가 하나면 생기지 않는다 — 한 코어는 자기 접근을 프로그램 순서대로 본다. ISR ↔ main 사이의 문제는 컴파일러 재배치와 원자성(C-M14)이다. 그러나 "다른 관찰자" 가 있으면 다시 필요하다: DMA 엔진(디스크립터 내용 → OWN 비트, `dmb`), 장치 레지스터(쓰기 버퍼 때문에 MMIO 쓰기 완료를 기다리려면 `dsb` — C-B09 의 WFI 앞), 캐시가 있는 M7 의 DMA 버퍼(clean/invalidate — ARCH-M12), 듀얼 코어 MCU 의 메일박스(MP 그 자체). 꼬리: "RP2040 두 M0+ 코어 사이 플래그는?" → M0+ 는 단순 파이프라인이지만 컴파일러 배리어 + `dmb` (pico-sdk 의 `__dmb()`) 를 쓰고, 상호 배제는 SIO 하드웨어 스핀락 (OS-M07). 버스 순서 보장 세부는 데이터시트 확인 필요.
</details>

## 9. 추가 연습

### 연습 1 — IRIW · 2+2W 를 하네스에 넣기

IRIW (4 스레드) 와 2+2W (읽기 없음 → 배치 끝에 스레드 0 이 최종값을 기록) 의 SC 인터리빙 수와 히스토그램을 구하고, 실제 스레드 seq_cst 실행에서 이상한 결과가 0 인지 확인하라. IRIW acq/rel 은 무엇을 기대하나?

<details><summary>답</summary>

IRIW: 6!/(1!·1!·2!·2!) = 180 개, 서로 다른 결과 15 개 (16 개 중 `1 0 1 0` 만 없음). 2+2W: 6 개, (x, y) = (2,1)×1, (2,2)×4, (1,2)×1, (1,1)×0. seq_cst 는 C11 이 둘 다 금지 → 체크 (실제 스레드 10만 회, 결과가 모두 SC 집합 안). IRIW acq/rel 은 C11 이 **허용** 하므로 체크하지 않고 정보만 찍는다 — ARMv8 은 MCA 라 0 이 나와야 한다 (실측 `info IRIW acq/rel weird=0 / 100000`). 2+2W acq/rel 도 C11 허용이지만 clang 이 `stlr;stlr` 를 내서 이 칩에서는 0 — "C11 이 허용하는데 하드웨어가 더 강하게 동작" 하는 칸이다.
</details>

### 연습 2 — "0 번" 의 통계

`upper95_zero(n)` (0/n 일 때 확률의 95% 상한) 과 `iters_needed(p, conf)` (확률 p 인 사건을 conf 확률로 최소 1 번 보기 위한 반복 수) 를 써라. LB 1000만 번 0 은 무엇을 말하나? SB acq/rel(약 5.2e-4) 을 99% 로 보려면 몇 번?

<details><summary>답</summary>

(1-p)^n = 0.05 → p = 1 - 0.05^(1/n) ≈ 3/n (rule of three). n = 10^6 → 2.996e-6, n = 10^7 → 2.996e-7. "LB 확률이 3e-7 미만일 가능성 95%" 일 뿐 금지가 아니다. 필요 반복 수 = ⌈ln(1-conf)/ln(1-p)⌉: p = 1e-6, 95% → 2,995,731 번; p = 5.2e-4, 99% → 8,854 번. 실리콘 검증에서 "몇 번 돌려야 하나" 의 답이 이 식이고, 하네스 모드에 따라 p 자체가 1000 배 바뀐다는 게 함정이다 (Part A 표).
</details>

### 연습 3 — fresh variable 을 지키지 않으면

`reset = false` (배치 사이에 셀을 안 지움) 로 SB 와 MP 를 강제 스케줄 600 반복(배치 60) 돌려 히스토그램을 예측하고 확인하라.

<details><summary>답</summary>

첫 배치 60 반복만 정상 (스케줄 6 개 × 10): SB (1,1) 40 · (0,1) 10 · (1,0) 10. 그 뒤 540 반복은 지난 배치의 `x=1, y=1` 이 남아 있어 load 가 무조건 1 → 전부 (1,1). 합계 (1,1) = 580. MP 는 첫 배치 (1,1) 10 · (0,1) 40 · (0,0) 10, 나머지 540 은 옛 flag=1·data=1 → (1,1) = 550. 실제 실행이라면 이상한 결과가 **가려져** 0 으로 보이고, 리셋이 장벽 밖에서 경쟁하면 반대로 **가짜** weird 가 생긴다. 강제 스케줄은 이런 하네스 버그를 결정적으로 잡는다.
</details>

### 연습 4 — 실측을 게이트에

세트 1 의 실측(arm64 3 회 합, Rosetta 3 회 합)을 `lit_obs` 배열로 넣고 `lit_gate` 를 돌려라. arm64 데이터를 TSO 모델로 판정하면?

<details><summary>답</summary>

arm64 데이터 / ARMv8 모델: 버그 0, seen 5 칸 (SB relaxed · SB acq/rel · MP relaxed · IRIW relaxed · 2+2W relaxed), allowed-unseen 1 칸 (LB relaxed). Rosetta 데이터 / TSO 모델: 버그 0, seen 2 칸 (SB relaxed · SB acq/rel). arm64 데이터 / **TSO 모델**: 버그 3 개 (MP · IRIW · 2+2W relaxed, 첫 버그 = MP relaxed 102463) — "x86 에서 테스트한 코드를 ARM 에 올리면" 이 바로 이 판정이다. C11 은 그 세 칸을 허용하므로 코드가 원래 틀렸던 것이고, x86 이 그걸 숨겨 줬을 뿐이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr     # solution.c 와 같은 폴더에서 (포함함)
```

실제 출력:

```text
info  IRIW acq/rel weird=0 / 100000 (C11 allows, ARMv8 other-multi-copy-atomic forbids)
info  ARM data judged by TSO model: first bug = MP/relaxed weird=102463
20/20 passed
```

## 10. 핵심 정리

- 리트머스 = 작은 프로그램 + "SC 로는 불가능한 결과". 관찰 1 번 = 허용의 증명, 0 번 = 아무것도 아님 (0/n 의 95% 상한 ≈ 3/n).
- C11: acq/rel 은 MP·LB 를 막고 SB·IRIW·2+2W 는 못 막는다. SB 는 seq_cst(`stlr;ldar`) 또는 양쪽 seq_cst 펜스(`dmb ish`)만. arm64 acquire = `ldapr`(RCpc), seq_cst load = `ldar`(RCsc).
- M6 실측 (100만 회당): SB relaxed ~35만, SB acq/rel ~500, MP relaxed ~3.4만, IRIW relaxed ~1500, 2+2W ~8만, LB 0 (1600만 중). C11 금지 칸은 모든 실행 0.
- Rosetta 2(TSO): SB 77~99%, MP·LB·IRIW·2+2W 0 → "ARM 에서만 보이는 것 = store→store, load→load 재배치". x86 에서 비싼 건 store→load 를 막는 `xchg`/`lock or` 뿐.
- 하네스가 숫자를 정한다: 반복마다 장벽 170 vs 공통 시간축 34만 (SB). 반복마다 새 변수, 결과는 배열, 공유 변수는 relaxed 원자, sanitizer 없이.
- ARMv8 은 other-multi-copy-atomic → reader 순서만 묶으면 IRIW 금지. C11 은 그래도 허용.
- 실리콘 검증: 모델 허용 표 + 다양한 모드로 실행 → 금지 결과 1 번 = 버그. 허용인데 0 = 커버리지 구멍.

## 참고자료

- J. Alglave, L. Maranget, S. Sarkar, P. Sewell, "Litmus: Running Tests against Hardware", TACAS 2011 — litmus7 하네스 설계(반복 배열, 동기화 모드).
- J. Alglave, L. Maranget, M. Tautschnig, "Herding Cats: Modelling, Simulation, Testing, and Data Mining for Weak Memory", ACM TOPLAS 36(2), 2014 — herd7, 하드웨어 실측과 버그 사례.
- C. Pulte, S. Flur, W. Deacon, J. French, S. Sarkar, P. Sewell, "Simplifying ARM Concurrency: Multicopy-Atomic Axiomatic and Operational Models for ARMv8", POPL 2018.
- P. Sewell, S. Sarkar, S. Owens, F. Zappa Nardelli, M. Myreen, "x86-TSO: A Rigorous and Usable Programmer's Model for x86 Multiprocessors", CACM 53(7), 2010.
- Arm Architecture Reference Manual for A-profile (DDI 0487) — B2 "The AArch64 Application Level Memory Model" (other-multi-copy atomicity, Barrier-ordered-before, Load-Acquire / Load-AcquirePC / Store-Release) — <https://developer.arm.com/documentation/ddi0487/latest/>
- Intel 64 and IA-32 Architectures SDM Vol. 3A, "Memory Ordering" 절 (절 번호는 판본마다 다름 — 확인 필요).
- ISO/IEC 9899:2011 (C11) 5.1.2.4 (data race, happens-before), 7.17.3 (memory_order, seq_cst 전체 순서), 7.17.4 (fences).
- diy / litmus7 / herd7 도구 — <https://diy.inria.fr/> (이 맥에는 설치 안 함)
- 관련 레슨: C-A03 (stdatomic, `demo_sb_litmus`), C-A02 (컴파일러 vs CPU 재배치, DMA OWN 비트), ARM-M05 (asm 수준 MP/SB 실측, `stlr;ldapr`), ARM-M04 · ARM-M11 (exclusive), OS-A03 (`demo_sb`: rel/acq 1200만 중 2), OS-M08 (relaxed 메시지 패싱 실측), LLAT-M02 (seqlock fence 빼면 찢어짐 0.035%/0.30%), MCPP-M13 (fence 규칙), MEM-M07 (공리 모델 열거 — 이 레슨 표의 계산 쪽), RISCV-A04 (RVWMO), ARCH-A04 · MEM-M04 (일관성 프로토콜·트래픽), ARCH-M08 (false sharing), ARCH-M12 (DMA 캐시 유지보수), C-M14 (ISR 공유)
