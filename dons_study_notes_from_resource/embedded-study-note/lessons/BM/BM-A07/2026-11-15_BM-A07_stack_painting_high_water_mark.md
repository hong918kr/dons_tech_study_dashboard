---
id: BM-A07
time: 110분
updated: 2026-11-15
---
# BM-A07 스택 오버플로 탐지: 스택 페인팅과 high-water mark

## 학습 목표

- 리셋 직후 메인 스택(MSP)을 `0xDEADBEEF` 로 칠하고(stack painting), 아래쪽부터 훑어 **최고 수위(high-water mark, HWM)** 를 구하는 코드를 쓰고, 왜 **자기 프레임 아래까지는 칠할 수 없는지**(여유 margin) 를 실측 크래시로 설명할 수 있다.
- `-fstack-usage` 의 `.su` 정적값과 페인팅 실측값을 **바이트 단위로 일치**시키고(64 / 200 / 464 B), 어긋나는 원인 — 예외 프레임 32 B, 정렬 패딩 4 B(xPSR bit9), 쓰이지 않은 예약 공간, 꼬리 호출 — 을 하나씩 재현해 말할 수 있다.
- 스택 한계 바로 아래 32 B 가드(카나리)를 링커 스크립트로 놓고, 넘침을 **가드 안에서 잡는 경우 / 이미 .bss 를 망가뜨린 뒤 잡는 경우 / 큰 프레임이 가드를 건너뛰어 아무도 모르는 경우** 를 QEMU 에서 재현한다.
- `-fstack-protector` (함수별 카나리) 를 freestanding 으로 붙이고, 지역 카나리와 스택 구역 가드가 각각 무엇을 잡고 무엇을 놓치는지 구분한다.
- `.su` + 호출 간선으로 정적 최악값을 계산하고, 실측 HWM 과 합쳐 "reserved / static / measured / margin" 출시 체크리스트를 만든다.

## 1. 문제

면접에서 "스택 크기 2 KB 는 어떻게 정했어요?" 에 "넉넉하게요" 라고 답하면 끝이다. 시니어 펌웨어 면접은 **"얼마나 쓰는지 어떻게 재고, 넘치면 어떻게 아나?"** 를 숫자로 묻는다. Cortex-M 의 메인 스택은 MMU 도 가드 페이지도 없는 평범한 RAM 이라, 넘치면 **아무 fault 없이 바로 아래 변수(.bss)를 덮고 계속 돈다** — Don 이 SSD 펌웨어에서 겪었을 "FTL 메타데이터가 어느 날 이상한 값" 류 버그의 전형적인 원인이다. 이 레슨은 진짜 Cortex-M3 (QEMU `mps2-an385`) 에서 스택을 칠하고, 재고, 일부러 넘쳐서 무엇이 잡히고 무엇이 안 잡히는지를 확인한다. 태스크 스택(RTOS)은 RTOS-M08 이 다뤘고, 여기는 **베어메탈 MSP 하나**가 주인공이다.

이 레슨의 RAM 배치 (실측 주소, `a07.ld`):

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">RAM 배치 (QEMU mps2-an385, 실제 링크 주소) — 스택은 오른쪽(높은 주소)에서 왼쪽으로 자란다</text>
  <rect x="10" y="40" width="80" height="60" class="f-mut" stroke="currentColor"/>
  <text x="16" y="62" fill="currentColor">.data/.bss</text>
  <text x="16" y="80" fill="currentColor">(하네스)</text>
  <rect x="90" y="40" width="140" height="60" class="f-ok" stroke="currentColor"/>
  <text x="96" y="62" fill="currentColor">g_neighbor[64]</text>
  <text x="96" y="80" fill="currentColor">.bss 마지막 256 B</text>
  <rect x="230" y="40" width="50" height="60" class="f-warn" stroke="currentColor"/>
  <text x="236" y="62" fill="currentColor">guard</text>
  <text x="236" y="80" fill="currentColor">32 B</text>
  <rect x="280" y="40" width="330" height="60" class="f-acc" stroke="currentColor"/>
  <text x="290" y="62" fill="currentColor">0xDEADBEEF 로 칠해진 영역 (아직 안 쓰임)</text>
  <text x="290" y="80" fill="currentColor">.stack 2048 B = reserved</text>
  <rect x="610" y="40" width="70" height="60" class="f-warn" stroke="currentColor"/>
  <text x="614" y="62" fill="currentColor">쓰인 72 B</text>
  <text x="614" y="80" fill="currentColor">(부팅 직후)</text>
  <path d="M680 40 L680 100" stroke="currentColor"/>
  <text x="2" y="118" fill="currentColor">0x20000000</text>
  <text x="70" y="132" fill="currentColor">0x20000040</text>
  <text x="196" y="118" fill="currentColor">0x20000140</text>
  <text x="262" y="132" fill="currentColor">0x20000160 = _sstack (한계)</text>
  <text x="620" y="118" fill="currentColor">0x20000960</text>
  <text x="620" y="132" fill="currentColor">= _estack (MSP 초기값)</text>
  <path d="M600 160 L300 160" stroke="currentColor"/><path d="M300 160 l8 -4 l0 8 z" fill="currentColor"/>
  <text x="380" y="154" fill="currentColor">스택 성장 방향 (PUSH = 주소 감소)</text>
  <path d="M610 180 L610 196 L680 196 L680 180" stroke="currentColor" fill="none"/>
  <text x="540" y="214" fill="currentColor">Reset 의 SP − 64 B 까지만 칠함 (PAINT_MARGIN)</text>
  <path d="M280 180 L280 196" stroke="currentColor"/>
  <text x="140" y="214" fill="currentColor">HWM 스캔: _sstack 에서 위로, 첫 "칠 아님" 워드</text>
  <text x="10" y="240" fill="currentColor">넘치면: 가드(카나리)를 먼저 밟고 → 그다음 g_neighbor 를 덮는다. RAM 이므로 fault 는 없다.</text>
</svg>
```

### Part A — 칠하기, 재기, `.su` 와 맞추기

- 주어진 것: `a07.ld` (위 그림), startup (BM-B08 축약판), `frames.c` (프레임 크기를 아는 함수: `path_top 264 B → path_mid 136 B → path_leaf 64 B`, IRQ14/15 핸들러, 재귀 함수, `big_sparse`, `ssp_copy`), `run_qemu.sh` 가 `frames.su` 를 `su_gen.h` (`#define SU_path_leaf 64` …) 로 바꿔 펌웨어에 넣는다.
- **TODO 1** `stack_paint(lo, hi)`: `[lo, hi)` 의 모든 워드에 `0xDEADBEEF`. `Reset_Handler` 가 가장 먼저 `[_sstack, SP − 64)` 를 칠한다.
- **TODO 2** `stack_lowest_used()`: `_sstack` 에서 위로 훑어 첫 "칠 아님" 워드의 주소. HWM = `_estack − 그 주소`.
- **TODO 3** `canary_arm()` / `canary_ok()`: 가드 8 워드에 `0xC0FFEE00`, 전부 그대로인지 검사.
- 시험: 깊이 1/2/3 경로의 실측 == `.su` 합, IRQ14 → IRQ15 중첩 (+32 B 예외 프레임 + 핸들러), SP%8==4 에서 IRQ (+4 B 정렬 패딩, 쌓인 xPSR bit9 = 1), 예약만 하고 안 쓴 프레임 (과소 측정).

### Part B — 넘침: 잡히는 것, 늦게 잡히는 것, 안 잡히는 것

- SysTick 1 ms 마다 `canary_ok()` 검사. 재귀(`recurse_until`, 프레임 24 B)로 SP 를 한계 아래로 보낸다 (그동안 `cpsid i` — 맨 아래에서 SysTick 프레임이 더 쌓이지 않게).
- **B1** 가드 안에서 멈춤 (8 B 넘침) → 카나리 파괴, SysTick 탐지, 이웃 무사. **B2** 가드를 지나 104 B → 탐지는 되지만 이웃 15 워드가 이미 망가짐. **B3** 256 B 지역 배열을 "예약만" 하는 함수가 가드를 **건너뛰고** 그 아래에서 leaf 가 이웃을 덮음 → 카나리·HWM·SysTick 모두 "정상".
- **B4** `-fstack-protector` (함수 하나만, `stack_protect` 속성): 16 B 버퍼에 20 B → **못 잡음**(패딩), 24 B → `__stack_chk_fail`.
- 결함 주입 실행: `paintbug` (main 안에서 스택 **전체**를 칠함 → 자기 복귀 주소가 `0xDEADBEEF` → HardFault, 쌓인 PC = `0xdeadbeee`), `clash` (`-fstack-clash-protection` 이 B3 를 구해 주나?).

### Part C — 출시 체크리스트

- 펌웨어가 reserved / measured / margin / guard-vs-largest-frame 을 찍고, `practice.c` 가 `.su` + objdump 호출 간선으로 정적 최악값(간접 호출 주석, 재귀 깊이 주석, ISR 중첩 예산)을 계산해 판정표를 만든다.

## 2. Problem (English)

> **Measure and guard the main stack of a bare-metal Cortex-M3.**
>
> Target: QEMU `mps2-an385`. The linker script reserves `.stack` (2048 B, `_sstack`..`_estack`) and a 32-byte `.stack_guard` immediately below `_sstack`; the last `.bss` object (`g_neighbor`, 256 B) sits right below the guard.
>
> 1. `void stack_paint(volatile uint32_t *lo, volatile uint32_t *hi)` — fill `[lo, hi)` with `0xDEADBEEF`. `Reset_Handler` calls it first, with `hi = SP − 64`.
> 2. `volatile uint32_t *stack_lowest_used(void)` — scan upward from `_sstack`, return the first word that is not the paint value (`_estack` if all painted). HWM = `_estack − result`.
> 3. `void canary_arm(void)`, `int canary_ok(void)` — write/check `0xC0FFEE00` in every guard word. `SysTick_Handler` (1 ms) records the first tick at which `canary_ok()` fails.
>
> Constraints: no `memset` (freestanding, `-nostdlib`), no heap, every loop bounded, interrupts masked only around the deliberate overflow.
>
> Expected (QEMU, gcc 14.2.1 `-O2`): repaint-then-call measurements equal the `.su` sums exactly: `leaf 64`, `mid>leaf 200`, `top>mid>leaf 464`; with IRQ14 pended in the leaf `544`; with IRQ15 nested inside IRQ14 `656`; with SP misaligned by 4 at IRQ14 entry `664` and stacked xPSR bit 9 = 1. Recursing 8 B past `_sstack` must destroy the canary and leave `g_neighbor` intact; 104 B past must damage `g_neighbor` (detected late); a 264-byte frame that writes only its top word must jump over the guard undetected.
>
> Follow-ups: Why can't the painter paint up to its own SP? Why scan bottom-up and not top-down? HWM reads 2044 of 2048 — did it overflow? Your canary is intact — are you safe? How do ISRs enter the static budget? What changes with an FPU, an RTOS, or ARMv8-M `MSPLIM`?

## 3. 시작 코드

<!-- code: starter.c -->

같이 쓰는 파일: `frames.c` (프레임을 아는 함수들 — 따로 `-fstack-usage` 로 컴파일), `a07.ld` (링커 스크립트), `run_qemu.sh` (VM 빌드·실행), `practice.c` (호스트 C).

<!-- code: frames.c -->

<!-- code: a07.ld -->

실행은 Lima VM `elinux` 안에서 (Mac 홈은 VM 에서 읽기 전용 → 스크립트가 `/tmp/BM-A07_work` 로 복사해 빌드). 빌드는 2 단계: ① `frames.c` → `frames.o` + `frames.su` → `su_gen.h`, ② `starter.c`/`solution.c` 를 `su_gen.h` 와 함께 컴파일해 `frames.o` 와 링크. `frames.c` 는 `su_gen.h` 를 보지 않으므로 ① 의 숫자가 곧 링크된 코드의 숫자다. Mac 에서:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-A07
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # 28/44, qemu exit=1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 44/44, qemu exit=0 (~0.4 s)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh su"         # .su 두 개, su_gen.h, 호출 간선, 심볼, ssp_copy 디스어셈블
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh paintbug"   # 자기 프레임까지 칠하면? (HardFault, exit 3)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh clash"      # -fstack-clash-protection 실험 (A4 FAIL 은 예상된 것)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c (호스트 C)
```

시작 코드 실행 결과 (실제 출력, 일부 줄 생략 표시):

```text
   text	   data	    bss	    dec	    hex	filename
   7276	      4	   2396	   9676	   25cc	starter.elf
layout: neighbor 0x20000040..0x20000140  guard 0x20000140..0x20000160  stack 0x20000160..0x20000960  CCR=0x00000200
A1 boot: HWM=2048 B of 2048 (paint stopped 64 B below Reset_Handler's SP)
FAIL starter.c:213  hwm_boot >= PAINT_MARGIN && hwm_boot <= 256u
FAIL starter.c:214  _sstack[0] == STACK_PAINT && _sstack[200] == STACK_PAINT
A2 path            static(.su)  measured
   leaf             64        1952
FAIL starter.c:236  d == k_paths[i].su
   … (mid>leaf, top>mid>leaf, A3 세 줄 모두 1952 → FAIL) …
B1 overflow into guard only   depth=81 min_sp=0x20000158 (below limit by 8 B)
     canary=intact systick=silent HWM=2048 neighbor_damaged_words=0
FAIL starter.c:307  !canary_ok()
FAIL starter.c:308  g_trip_tick != 0u
B2 overflow past guard        depth=85 min_sp=0x200000f8 (below limit by 104 B)
     canary=intact systick=silent HWM=2048 neighbor_damaged_words=15 [46..63] e.g. g_neighbor[63]: 1063 -> 0x000011e5
FAIL starter.c:313  !canary_ok()
FAIL starter.c:314  g_trip_tick != 0u
   … (B3, 체크리스트 margin 0 % → FAIL) …
B4 ssp_copy n=16 ok, n=20 (padding only) NOT detected, now n=24 ...
  __stack_chk_fail: ssp_copy frame canary overwritten (n=24) -> stop
28/44 passed
qemu exit=1
```

읽는 법: 넘침 자체(재귀 깊이, `min_sp`, 이웃 15 워드 파손)는 **스텁이어도 똑같이 일어난다** — 하드웨어는 아무것도 막지 않는다. 시작 코드에 없는 것은 "보는 눈" 뿐이다: HWM 은 늘 2048 (모르겠으니 전부), 카나리는 늘 "온전". B2 에서 모터 전류 제한값 1063 이 `0x11e5` 로 바뀌었는데 아무도 모른다.

## 4. 힌트

<details>
<summary>힌트 1 — 칠하기는 그냥 워드 루프, 단 volatile</summary>

`for (p = lo; p < hi; ++p) *p = STACK_PAINT;` 이면 끝이다. 포인터가 `volatile` 이어야 컴파일러가 "아무도 안 읽는 쓰기" 로 보고 지우거나 `memset` 호출로 바꾸지 않는다 (`-nostdlib` 라 `memset` 은 링크 에러, `-fno-tree-loop-distribute-patterns` 가 루프→memset 변환도 막는다 — BM-B07/B08).

</details>

<details>
<summary>힌트 2 — 스캔은 반드시 아래(한계)에서 위로</summary>

프레임 안에는 **안 쓰인 구멍**(정렬 패딩, 덜 채운 배열)이 있다. 위에서 내려오며 "첫 칠해진 워드" 를 찾으면 첫 구멍에서 멈춰 사용량을 크게 과소 보고한다 (연습 5: 5000 번 중 4579 번 틀림). 아래에서 올라가며 "첫 칠 아님" 을 찾으면 구멍과 상관없이 **가장 낮게 쓰인 워드**를 정확히 찾는다.

</details>

<details>
<summary>힌트 3 — 카나리는 8 워드 전부</summary>

넘침은 가드의 **위쪽(높은 주소)부터** 들어온다. B1 은 8 B 만 넘쳐서 `guard[6]`, `guard[7]` 만 바뀐다. `guard[0]` 하나만 보면 놓친다 (§7 실수 3 — 실제 돌연변이 m1 이 그렇게 FAIL).

</details>

<details>
<summary>힌트 4 — 측정 도우미도 스택을 쓴다</summary>

`measure_call` 은 `SP − 64` 아래만 다시 칠한다. 그 64 B 안에 `stack_paint`·`stack_lowest_used` 자신의 프레임(`.su` = 0, 0 B)과 그 사이 들어올 수 있는 예외 프레임이 들어간다. 그래서 64 B 보다 적게 쓰는 경로는 **"64 B 이하"** 로만 보인다 (A4 의 바닥값).

</details>

## 5. 풀이

### Part A — 칠하기·재기·맞추기

**정답 실행 결과** (실제 출력 전체, 5 번 실행해 바이트까지 동일 — md5 같음):

```text
   text	   data	    bss	    dec	    hex	filename
   7972	      4	   2396	  10372	   2884	solution.elf
layout: neighbor 0x20000040..0x20000140  guard 0x20000140..0x20000160  stack 0x20000160..0x20000960  CCR=0x00000200
A1 boot: HWM=72 B of 2048 (paint stopped 64 B below Reset_Handler's SP)
A2 path            static(.su)  measured
   leaf             64        64
   mid>leaf         200        200
   top>mid>leaf     464        464
A3 with interrupts                    expect  measured  xPSR[9] low/high
   top>mid>leaf + IRQ14               544    544       0/0
   top>mid>leaf + IRQ14 > IRQ15       656    656       0/0
   same, SP%8==4 at IRQ14 (+4 +pad4)  664    664       1/0
A4 big_sparse alone: static 264 B, measured 64 B (= PAINT_MARGIN floor: reserved but never written -> invisible to painting)
A5 worst normal HWM (absolute, from _estack) = 768 B
B1 overflow into guard only   depth=81 min_sp=0x20000158 (below limit by 8 B)
     canary=DESTROYED systick=DETECTED HWM=2044 neighbor_damaged_words=0
B2 overflow past guard        depth=85 min_sp=0x200000f8 (below limit by 104 B)
     canary=DESTROYED systick=DETECTED HWM=2044 neighbor_damaged_words=15 [46..63] e.g. g_neighbor[63]: 1063 -> 0x0000147d
B3 big frame jumps over guard depth=77 min_sp=0x200001b8 (above limit by 88 B)
     canary=intact systick=silent HWM=1972 neighbor_damaged_words=16 [12..27] e.g. g_neighbor[27]: 1027 -> 0x200001c7
== MSP stack checklist ==
reserved              2048 B
measured HWM (normal) 768 B
margin                1280 B (62 %)
guard                 32 B, largest single frame 264 B -> jump-over possible
B4 ssp_copy n=16 ok, n=20 (padding only) NOT detected, now n=24 ...
  __stack_chk_fail: ssp_copy frame canary overwritten (n=24) -> stop
44/44 passed
qemu exit=0
```

**① 왜 Reset_Handler 의 맨 처음에 칠하나.** `.data` 복사·`.bss` 0 채우기·`main` 이 스택을 쓰기 **전에** 칠해야 그 사용량까지 잡힌다. `.stack` 은 NOLOAD 라 `.bss` 0 채우기와 겹치지 않고, `stack_paint` 는 전역을 안 쓰므로 `.data` 초기화 전에 불러도 된다.

**② 왜 SP − 64 까지만 (PAINT_MARGIN).** 칠하는 코드도 스택 위에서 돈다. `Reset_Handler` 의 SP 아래에는 곧 `stack_paint` 의 프레임이 생긴다. 칠하는 범위가 그 프레임을 덮으면 **자기 복귀 주소를 칠 값으로 덮는다**. `run_qemu.sh paintbug` 가 바로 그것이다 — `main` 안의 noinline 함수가 `[_sstack, _estack)` 전체를 칠하고 돌아가려 한 실제 결과:

```text
layout: neighbor 0x20000040..0x20000140  guard 0x20000140..0x20000160  stack 0x20000160..0x20000960  CCR=0x00000200
A1 boot: HWM=72 B of 2048 (paint stopped 64 B below Reset_Handler's SP)
painted [_sstack, _estack) from inside main ... returning
HardFault: CFSR=0x00000001 HFSR=0x40000000 stacked pc=0xdeadbeee lr=0x00000217
qemu exit=3
```

`pop {…, pc}` 가 `0xDEADBEEF` 를 꺼냈다 → bit0 = 1 이라 Thumb 상태로 `0xDEADBEEE` 로 분기 → 그 주소는 기본 메모리 맵의 "외부 장치" 구역(0xA0000000–0xDFFFFFFF, 실행 금지 XN — ARMv7-M ARM B3.1 확인 권장)이라 명령 fetch 가 MemManage **IACCVIOL** (CFSR bit0) → MemManage 가 꺼져 있어 HardFault **FORCED**. 크래시 덤프의 PC 에 `0xdeadbeee` 가 보이면 "칠한 스택에서 복귀 주소를 꺼냈다" = 스택이 망가졌다는 강한 증거다 — 페인트 값을 아무 값으로나 고르지 않는 이유 중 하나. 여유 64 B 의 근거: `.su` 로 본 `stack_paint` / `stack_lowest_used` 프레임 0 B + 칠하는 동안 NMI·fault 가 들어오면 생길 예외 프레임 32 B + 정렬 4 B (연습 4 가 `.su` 로 다시 확인: 40 ≤ 64).

**③ 부팅 HWM 72 B.** 칠하지 않은 맨 위 64 B 는 스캔에 "쓰임" 으로 보이므로 HWM ≥ 64 가 바닥이다. 여기에 `main` 진입까지 그 아래로 8 B 가 더 쓰여 72.

**④ 측정 = 다시 칠하고 → 부르고 → 가장 낮은 워드.** `measure_call(fn)` 은 자기 SP 를 읽고, `[_sstack, SP−64)` 를 다시 칠하고, `fn` 을 부른 뒤 `SP − lowest` 를 돌려준다. `frames.c` 의 함수들은 지역 배열을 **전부** 쓰므로 프레임의 가장 낮은 워드까지 실제로 쓰인다 → 측정 = 정적값이 정확히 같아야 한다.

| 경로 | `.su` (frames.su 실제) | 합 | 실측 |
|---|---|---|---|
| leaf | path_leaf 64 (`sub sp,#64`, 리프라 push 없음) | 64 | 64 |
| mid>leaf | path_mid 136 (`push {lr}` 4 + `sub sp,#132`) + 64 | 200 | 200 |
| top>mid>leaf | path_top 264 (`push {r4,lr}` 8 + `sub sp,#256`) + 200 | 464 | 464 |

**⑤ 인터럽트는 `.su` 에 없다.** leaf 안에서 IRQ14 를 걸면 하드웨어가 **예외 프레임 32 B** (r0–r3, r12, lr, pc, xPSR — FPU 없는 M3) 를 쌓고, naked 입구(`Irq14_Handler`, 0 B) 가 `b irq_low_body` (48 B) 로 간다. 그 안에서 더 급한 IRQ15 (우선순위 0x40 < 0x80) 를 걸면 또 32 B + 80 B.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">A3: measure_call 의 SP 아래로 쌓이는 것 (실측 656 B / 정렬 어긋남 664 B)</text>
  <rect x="40" y="30" width="300" height="40" class="f-mut" stroke="currentColor"/>
  <text x="50" y="55" fill="currentColor">path_top 264 B (.su)</text>
  <rect x="40" y="70" width="300" height="30" class="f-mut" stroke="currentColor"/>
  <text x="50" y="90" fill="currentColor">path_mid 136 B</text>
  <rect x="40" y="100" width="300" height="24" class="f-mut" stroke="currentColor"/>
  <text x="50" y="117" fill="currentColor">path_leaf 64 B → 여기서 ISPR 에 IRQ14 쓰기</text>
  <rect x="40" y="124" width="300" height="22" class="f-warn" stroke="currentColor"/>
  <text x="50" y="140" fill="currentColor">예외 프레임 32 B (하드웨어 자동 push)</text>
  <rect x="40" y="146" width="300" height="24" class="f-ok" stroke="currentColor"/>
  <text x="50" y="163" fill="currentColor">irq_low_body 48 B → IRQ15 pend</text>
  <rect x="40" y="170" width="300" height="22" class="f-warn" stroke="currentColor"/>
  <text x="50" y="186" fill="currentColor">예외 프레임 32 B (중첩 선점)</text>
  <rect x="40" y="192" width="300" height="28" class="f-ok" stroke="currentColor"/>
  <text x="50" y="211" fill="currentColor">irq_high_body 80 B</text>
  <text x="350" y="40" fill="currentColor">← measure_call 의 SP (0 B)</text>
  <text x="350" y="122" fill="currentColor">464 = .su 합 (A2)</text>
  <text x="350" y="168" fill="currentColor">544 = 464 + 32 + 0 + 48</text>
  <text x="350" y="218" fill="currentColor">656 = 544 + 32 + 0 + 80  ← 가장 낮은 워드</text>
  <rect x="40" y="240" width="300" height="20" class="f-acc" stroke="currentColor"/>
  <text x="50" y="255" fill="currentColor">SP%8==4 이면: sub 4 + 정렬 패딩 4 → 664</text>
  <text x="350" y="255" fill="currentColor">쌓인 xPSR bit9 = 1 (IRQ14), 0 (IRQ15)</text>
  <text x="10" y="290" fill="currentColor">정적 예산 규칙: 서로 다른 선점 우선순위 단계마다 (예외 프레임 32 B, FPU 확장 프레임이면 104 B)</text>
  <text x="10" y="308" fill="currentColor">+ 정렬 패딩 4 B + 그 핸들러의 최악 경로. 같은 우선순위끼리는 서로 선점 못 하므로 한 단계로 친다.</text>
</svg>
```

**⑥ 정렬 패딩 4 B.** AAPCS 는 **공개 함수 경계에서만** SP 8 B 정렬을 요구한다. 리프 함수 안이나 프롤로그 중간에선 SP%8 == 4 일 수 있다. 그 순간 예외가 들어오면 CCR.STKALIGN = 1 (QEMU 리셋값 `CCR=0x00000200` 실측) 인 코어는 프레임을 8 B 경계로 내리며 4 B 를 비우고, **쌓인 xPSR bit9** 에 "패딩했음" 을 남겨 복귀 때 되돌린다. 이 레슨은 `sub sp,#4` 로 그 순간을 만들어 측정값 664 = 656 + 4 (내 sub) + 4 (패딩), IRQ14 의 xPSR[9] = 1 을 확인했다. 정적 예산에선 단계마다 +4 B 를 잡는다.

**⑦ 과소 측정 (A4).** `big_sparse(0)` 는 256 B 배열을 예약(.su 264)하지만 맨 위 한 워드만 쓴다. 페인팅은 "쓰인 워드" 만 보므로 실측은 바닥값 64 B (PAINT_MARGIN 안). **페인팅 HWM 은 실제 최대 사용량의 하한, `.su` 경로 합(+ISR)은 상한** — 둘은 서로 다른 질문의 답이다: 하한 ≤ 진짜 최대 ≤ 상한 (재귀·간접 호출·ISR 을 상한에 다 넣었을 때만).

**⑧ 꼬리 호출 (실제로 겪은 것).** 처음 `big_sparse` 를 `return call_leaf ? path_leaf(big[63]) : big[63];` 로 썼더니 gcc -O2 가 `add sp,#256; b.w path_leaf` 로 **꼬리 호출(sibling call)** 을 만들었다 — leaf 가 big 의 프레임을 걷어 낸 뒤 같은 높이에서 돈다. 이때 ".su 합 = 264 + 64" 는 과대평가가 된다. 호출 뒤에 `big[63]` 을 다시 읽게 고쳐 꼬리 호출을 막았다 (`frames.c` 주석). 정적 도구는 `b.w` 간선을 "호출" 로 세면 보수적(안전한 쪽)으로 틀린다.

### Part B — 넘침 세 가지와 함수 카나리

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">B1/B2/B3: 어디까지 썼나 (실측 주소, 가로축 0x20000040 → 0x200001e0, 1 px = 1 B)</text>
  <rect x="100" y="34" width="256" height="16" class="f-ok" stroke="currentColor"/>
  <text x="180" y="46" fill="currentColor">g_neighbor 256 B</text>
  <rect x="356" y="34" width="32" height="16" class="f-warn" stroke="currentColor"/>
  <text x="358" y="46" fill="currentColor">guard</text>
  <rect x="388" y="34" width="128" height="16" class="f-mut" stroke="currentColor"/>
  <text x="400" y="46" fill="currentColor">stack (아래쪽 일부)</text>
  <text x="80" y="66" fill="currentColor">0x...040</text><text x="330" y="66" fill="currentColor">0x...140</text>
  <text x="380" y="66" fill="currentColor">0x...160 _sstack</text>
  <text x="10" y="96" fill="currentColor">B1</text>
  <rect x="380" y="84" width="136" height="16" class="f-acc" stroke="currentColor"/>
  <text x="530" y="90" fill="currentColor">min_sp 0x...158</text>
  <text x="530" y="106" fill="currentColor">가드 8 B 파괴 → 탐지, 이웃 무사</text>
  <text x="10" y="136" fill="currentColor">B2</text>
  <rect x="284" y="124" width="232" height="16" class="f-acc" stroke="currentColor"/>
  <text x="530" y="130" fill="currentColor">min_sp 0x...0f8</text>
  <text x="530" y="146" fill="currentColor">이웃 15 워드 파괴 후 탐지 (늦음)</text>
  <text x="10" y="186" fill="currentColor">B3</text>
  <rect x="148" y="174" width="64" height="16" class="f-acc" stroke="currentColor"/>
  <text x="150" y="203" fill="currentColor">leaf 64 B</text>
  <rect x="212" y="174" width="252" height="16" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="250" y="186" fill="currentColor">big[0..62] 예약만 (안 씀)</text>
  <rect x="464" y="174" width="12" height="16" class="f-acc" stroke="currentColor"/>
  <text x="530" y="182" fill="currentColor">E = 0x...1b8 (big 진입 SP)</text>
  <text x="530" y="198" fill="currentColor">이웃 16 워드 파괴, 가드 그대로</text>
  <text x="10" y="236" fill="currentColor">B3 에서 HWM = 1972 B (= _estack − 0x200001ac, big 의 맨 위 12 B 만 보임) → "76 B 남음" 이라고 거짓 보고.</text>
  <text x="10" y="256" fill="currentColor">B1 에서도 HWM = 2044 B: 가장 낮은 _sstack 워드가 recurse_until 프레임의 패딩 구멍이라 칠해진 채 남았다.</text>
  <text x="10" y="276" fill="currentColor">결론: HWM 은 "얼마나 썼나" 의 하한일 뿐 넘침 탐지기가 아니다. 넘침은 가드(+ 가드 ≥ 최대 프레임)가 잡는다.</text>
</svg>
```

**B1 — 가드 안에서 멈춤.** `recurse_until(stop = _sstack − 1)` 은 SP 가 한계 아래로 내려가는 첫 프레임에서 멈춘다. 프레임 24 B (`push {r4,r5,lr}` 12 + `sub sp,#12`) ≤ 가드 32 B 이므로 넘침은 반드시 가드 안 (실측 8 B, 81 단계). SysTick 이 `cpsie i` 직후 다음 틱에서 카나리 파괴를 발견 — 이웃 0 워드 피해. 흥미로운 점: HWM 은 2044 (한계 대비 "4 B 남음") 라고 말한다. `min_sp + 8` = `_sstack` 워드가 그 프레임의 **패딩 구멍**(`sp+8..11`, 아무도 안 씀)이라 칠한 채 남았기 때문. 그래서 B1 의 CHECK 는 `HWM ≥ reserved − 8` ("거의 꽉 참") 이고, 체크리스트 규칙은 "HWM 이 reserved 에 한 프레임 이내로 닿으면 넘친 것으로 취급" 이어야 한다.

**B2 — 가드를 지나침.** `stop = _sguard − 64` → 104 B 넘침, 85 단계. 카나리는 잡지만, 그 전에 이웃 `[46..63]` 중 15 워드(구멍 3 개)가 이미 덮였다. `g_neighbor[63]` 의 "모터 전류 제한값" 1063 → `0x0000147d` 는 `recurse_until` 안 `bl recurse_until` 다음 명령 주소 `0x147c` | Thumb 비트 — **쌓인 복귀 주소(LR)** 다 (`objdump` 로 확인). 실제 장비라면 이 값 그대로 모터 제한으로 쓰였을 것이다. 탐지 지연 = 최대 SysTick 한 주기 + 그동안 쌓인 피해.

**B3 — 가드 건너뛰기.** `big_sparse` 의 프레임 264 B 는 가드 32 B 보다 크다. SP 가 한계 위 88 B (`E = 0x200001b8`) 에 있을 때 들어가면 프레임은 `[E−264, E)` = 가드 전체를 **덮지만 쓰지는 않는다**. 그 아래 leaf (64 B, 전부 씀) 가 `[0x20000070, 0x200000b0)` = 이웃 `[12..27]` 을 덮는다 (`g_neighbor[27]` = `0x200001c7` = leaf 의 `buf[15] = x + 15`, x = E). 카나리 온전, SysTick 조용, HWM 1972. **조용한 손상**의 정석이고, 2017 년 "Stack Clash" 류 공격과 같은 원리다. 대책: 가드 크기 ≥ 프로그램의 가장 큰 단일 프레임 (`.su` 최댓값 264 → 512 B 가드), 큰 배열은 정적/전역으로, 또는 스택 프로빙.

`run_qemu.sh clash` 로 `frames.c` 만 `-fstack-clash-protection` 으로 다시 빌드해 본 실제 결과:

```text
00000198 <big_sparse>:
 198:	f44f 5c80 	mov.w	ip, #4096	@ 0x1000
 19c:	b510      	push	{r4, lr}
 19e:	ebad 0c0c 	sub.w	ip, sp, ip
 1a2:	f8cc 0ef8 	str.w	r0, [ip, #3832]	@ 0xef8
 1a6:	b0c0      	sub	sp, #256	@ 0x100
A4 big_sparse alone: static 264 B, measured 272 B (= PAINT_MARGIN floor: reserved but never written -> invisible to painting)
FAIL solution.c:268  d_sparse <= PAINT_MARGIN
     canary=DESTROYED systick=DETECTED HWM=2044 neighbor_damaged_words=0
     canary=DESTROYED systick=DETECTED HWM=2044 neighbor_damaged_words=16 [43..63] e.g. g_neighbor[63]: 1063 -> 0x000014a1
B3 big frame jumps over guard depth=77 min_sp=0x200001b8 (above limit by 88 B)
     canary=intact systick=silent HWM=1972 neighbor_damaged_words=16 [12..27] e.g. g_neighbor[27]: 1027 -> 0x200001c7
43/44 passed
qemu exit=1
```

gcc 는 프레임을 잡기 전에 **새 프레임 바로 아래** (`sp − 8 − 4096 + 3832 = E − 272`) 에 한 번 써 본다(probe). 그래서 A4 측정이 272 B 로 바뀌었다(예상된 FAIL). 하지만 B3 는 **그대로 못 잡는다**: 프로브 주소 E − 272 는 가드보다 훨씬 아래(이웃 안)다. 스택 클래시 보호는 "가드 페이지(≥ 프로브 간격 4 KB)를 건너뛰지 못하게" 하는 장치라, 32 B 짜리 데이터 카나리에는 맞지 않는다. 결론은 같다: **가드 ≥ 최대 프레임**, 혹은 MPU 가드 + 같은 규칙 (BM-A08).

```svg
<svg viewBox="0 0 740 190" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">B4: ssp_copy 프레임 (frame 32 B, objdump 실제) — 낮은 주소 → 높은 주소</text>
  <rect x="20" y="40" width="320" height="40" class="f-ok" stroke="currentColor"/>
  <text x="120" y="65" fill="currentColor">buf[16]  sp+0 .. sp+15</text>
  <rect x="340" y="40" width="80" height="40" class="f-mut" stroke="currentColor"/>
  <text x="350" y="58" fill="currentColor">패딩</text><text x="350" y="74" fill="currentColor">sp+16..19</text>
  <rect x="420" y="40" width="80" height="40" class="f-warn" stroke="currentColor"/>
  <text x="430" y="58" fill="currentColor">카나리</text><text x="430" y="74" fill="currentColor">sp+20..23</text>
  <rect x="500" y="40" width="80" height="40" class="f-mut" stroke="currentColor"/>
  <text x="510" y="58" fill="currentColor">(빈칸)</text><text x="510" y="74" fill="currentColor">sp+24..27</text>
  <rect x="580" y="40" width="80" height="40" class="f-acc" stroke="currentColor"/>
  <text x="590" y="58" fill="currentColor">LR</text><text x="590" y="74" fill="currentColor">sp+28..31</text>
  <path d="M20 100 L420 100" stroke="currentColor"/><path d="M420 100 l-8 -4 l0 8 z" fill="currentColor"/>
  <text x="20" y="116" fill="currentColor">n = 20: 패딩까지만 → 카나리 그대로 → 못 잡음 (실측 "NOT detected")</text>
  <path d="M20 132 L500 132" stroke="currentColor"/><path d="M500 132 l-8 -4 l0 8 z" fill="currentColor"/>
  <text x="20" y="148" fill="currentColor">n = 24: 카나리 덮음 → 에필로그의 비교 실패 → bl __stack_chk_fail (복귀 전에 멈춤)</text>
  <text x="20" y="176" fill="currentColor">프롤로그: ldr r3,[__stack_chk_guard]; str r3,[sp,#20]  /  에필로그: 다시 읽어 eors → bne __stack_chk_fail</text>
</svg>
```

**B4 — 함수 카나리 (`-fstack-protector`).** 스택 구역 가드와 다른 물건이다: 함수마다 **지역 버퍼와 복귀 주소 사이**에 값을 두고 **복귀 직전** 비교한다 — 버퍼 오버런(스택 스매싱)을 잡지, 스택 **깊이** 넘침은 못 잡는다. freestanding 에선 `__stack_chk_guard` (변수) 와 `__stack_chk_fail` (noreturn 함수) 를 직접 정의한다. 이 레슨은 `frames.c` 를 `-fstack-protector-explicit` 로 컴파일하고 `ssp_copy` 하나에만 `__attribute__((stack_protect))` 를 붙였다. 관찰: 16 B 버퍼 뒤에 4 B 패딩이 있어 **20 B 오버런은 못 잡는다** — 카나리는 "정해진 슬롯 하나" 일 뿐이다. `__stack_chk_guard = 0xF00DA500` 의 맨 아래 바이트 0x00 은 "terminator" 카나리(문자열 복사가 0 에서 멈춤)이고, 제품에선 부팅마다 TRNG/고유 ID 로 바꿔야 한다 (보호 함수가 하나라도 불리기 **전에**).

### Part C — 출시 체크리스트

`run_qemu.sh su` 의 실제 `.su` (발췌) 와 objdump 로 뽑은 호출 간선:

```text
--- solution.su ---
solution.c:133:6:SysTick_Handler	4	static
solution.c:92:32:stack_paint	0	static
solution.c:99:46:stack_lowest_used	0	static
solution.c:146:43:measure_call	16	static
solution.c:211:5:main	80	static
solution.c:354:32:Reset_Handler	8	static
--- call edges (caller callee) from objdump -d solution.elf ---   (30 줄 중 발췌)
Irq14_Handler irq_low_body
Reset_Handler main
main measure_call
main recurse_until
measure_call *INDIRECT*
path_top path_mid
recurse_until *INDIRECT*
recurse_until recurse_until
```

`practice.c` (연습 2~4) 가 이 그대로의 텍스트로 계산한 결과 (실제 출력):

```text
ex2: worst(Reset_Handler) test build (recursion<=86) = 2480 B > reserved; product = 568 B (Reset 8 + main 80 + measure_call 16 + top/mid/leaf 464)
ex3: ISR budget 240 B (SysTick/IRQ14/IRQ15), +HardFault 308 B, FP frames 456 B
ex4: item                 value      rule                    verdict
     reserved             2048 B
     static worst          876 B     <= 75% of reserved      PASS (42%)
     measured HWM (QEMU)   768 B     <= static worst         PASS
     margin (static)      1172 B
     largest frame         264 B     <= guard 32 B          WARN (jump-over)
     PAINT_MARGIN 64 B    covers helpers + SysTick frame  PASS
```

| 항목 | 값 | 출처 | 판정 규칙 (이 레슨의 정책) |
|---|---|---|---|
| reserved | 2048 B | `a07.ld` STACK_SIZE | — |
| static worst | 876 B = 568 (Reset→main→measure_call→top/mid/leaf) + 308 (SysTick/IRQ14/IRQ15/HardFault 각 32+4+핸들러) | `.su` + 간선 + 주석 | ≤ 75 % |
| measured HWM | 768 B (= 104 main 쪽 + 664 A3 최악) | QEMU A5 | ≤ static (넘으면 분석이 뭔가 놓침) |
| margin | 정적 1172 B (57 %), 실측 1280 B (62 %) | | 실측 여유 ≥ 25 % (펌웨어 CHECK) |
| guard vs 최대 프레임 | 32 B < 264 B | `.su` 최댓값 | WARN → 가드 512 B 또는 배열을 정적으로 |

정적 분석이 스스로 못 하는 것 세 가지를 **사람이 주석으로** 채운다 (상용 도구 StackAnalyzer 등도 같은 방식 — 확인 권장): ① 간접 호출(`measure_call` → `fn`) 후보 목록, ② 재귀 깊이(`recurse_until ≤ 86` → 시험 빌드는 일부러 2480 B 로 넘침), ③ 제품에 없는 시험 경로 제외. 주석이 없으면 결과는 UNKNOWN / UNBOUNDED 로 남아야지, 0 으로 셈하면 안 된다. 다른 관점: RTOS-M08 의 `callgraph.sh` 는 같은 일을 clang/Mac 에서 했고, 태스크별 예산표를 만든다.

**다음 단계 (이 레슨 밖).** 넘침을 **즉시 fault** 로 바꾸는 하드웨어: ARMv7-M 은 MPU 가드 영역(BM-A08: 32 B 이상, 크기 = 2 의 거듭제곱, 정렬), ARMv8-M (Cortex-M23/M33/M55) 은 **MSPLIM / PSPLIM** 레지스터 — SP 가 한계 아래로 가는 순간 UsageFault STKOF (스펙 확인 권장). 둘 다 "가드 ≥ 최대 프레임" 문제를 MSPLIM 은 원천적으로 없앤다 (SP 값 자체를 비교하므로 건너뛰기 불가). 크래시 덤프를 남기는 것은 BM-A09, fault 정책은 BM-M13.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

## 7. 자주 하는 실수

**1. 칠하는 함수가 살아 있는 프레임까지 칠한다.** `stack_paint(_sstack, _estack)` 를 main 이나 init 함수 안에서 부르면 자기·호출자의 복귀 주소가 `0xDEADBEEF` 가 된다 (`paintbug`: HardFault, 쌓인 PC `0xdeadbeee`). 고침: Reset_Handler 맨 앞에서 `SP − PAINT_MARGIN` 까지만, margin ≥ 칠하는 코드 프레임 + 예외 프레임.

```c
uint32_t sp = read_sp();
stack_paint(_sstack, (volatile uint32_t *)((sp - PAINT_MARGIN) & ~3u));
```

**2. 위에서 아래로 스캔한다.** 첫 구멍(패딩)에서 멈춰 과소 보고. 돌연변이 m2 (위→아래 스캔) 는 A2/A3 측정이 전부 0 B 로 나와 8 개 FAIL. 고침: `_sstack` 에서 위로 "첫 칠 아님".

**3. 카나리 한 워드만 본다.** `return _sguard[0] == STACK_CANARY;` — 넘침은 가드의 **위쪽**부터 들어오므로 B1 (8 B) 을 놓친다 (돌연변이 m1: `!canary_ok()`, `g_trip_tick != 0` 두 FAIL). 고침: 8 워드 전부.

**4. 측정 중에 SysTick 을 켜 둔다.** 측정 도중 틱이 끼면 32 B + 4 B (SysTick 핸들러) 가 섞여 실행마다 숫자가 바뀐다. 이 레슨은 SysTick 을 Part A 뒤에 켜고, 넘침 재귀 동안 `cpsid i`. "끼어든 값" 이 진짜 최악값의 일부라는 건 맞다 — 그건 **정적 ISR 예산**으로 더한다 (연습 3).

**5. `.su` 합을 그대로 최악값으로 쓴다.** `.su` 는 함수 하나의 프레임이다. 간접 호출·재귀·ISR·꼬리 호출(§5 ⑧)·인라인(인라인된 함수는 `.su` 에서 사라지고 호출자 프레임에 합쳐짐 — `canary_ok` 가 `SysTick_Handler` 4 B 안으로 들어간 것처럼)을 사람이 맞춰야 한다.

**6. HWM 이 reserved 보다 작으니 안 넘쳤다고 결론.** B1 은 넘쳤는데 2044, B3 는 240 B 넘쳤는데 1972 를 보고했다. HWM 은 사용량 **하한**이지 넘침 판정기가 아니다.

**7. 페인트/카나리를 0 으로.** 0 은 지역 변수·패딩·`.bss` 에 가장 흔한 값이라 "안 쓰임" 과 구분이 안 된다. 0xDEADBEEF 는 RAM 주소(0x2000xxxx)·작은 정수·Thumb 코드 주소로 우연히 나오기 어렵고, PC 에 나타나면 눈에 띈다.

## 8. 면접 Q&A

<details>
<summary>Q1. 스택 사용량은 어떻게 재나요? 정적 분석과 페인팅, 어느 걸 믿나요?</summary>

둘 다 쓴다, 서로 다른 쪽에서 막는다. 정적(`-fstack-usage` `.su` + 호출 그래프 + ISR 중첩 예산)은 **상한** — 단, 간접 호출·재귀를 사람이 주석으로 채웠을 때만. 페인팅 HWM 은 **하한** — 시험이 실제로 밟은 경로·실제로 쓴 워드만 보인다 (A4: 264 B 예약이 64 B 이하로 보임). 출시 판정은 "실측 ≤ 정적 ≤ 예산" 이 모두 성립해야 한다; 실측 > 정적이면 정적 분석이 뭔가(어셈블리, 라이브러리, 간접 호출)를 놓친 것이니 그쪽을 먼저 고친다. 이 레슨: 정적 876, 실측 768, 예산 2048.

</details>

<details>
<summary>Q2. 왜 0xDEADBEEF 같은 값으로 칠하나요? 0 이면 안 되나요? 데이터가 우연히 그 값이면?</summary>

0 은 지역 변수 초기값·패딩·NULL 로 너무 흔하다. 0xDEADBEEF 는 홀수(정렬된 포인터 아님), RAM/플래시 주소 범위 밖, 작은 정수 아님 → 우연 일치가 드물고, 크래시 덤프에서 PC/LR 이 0xDEADBEEx 면 "칠한 스택을 꺼냈다" 가 바로 보인다 (paintbug 실측). 우연 일치는 **가장 낮은 워드가 정확히 그 값일 때만** 4 B 과소 보고로 이어진다 (연습 5 에서 재현) — 위쪽 워드가 우연히 같아도 아래→위 스캔은 영향받지 않는다. FreeRTOS 는 태스크 스택을 0xA5 바이트로 칠한다(`tskSTACK_FILL_BYTE` — 확인 권장).

</details>

<details>
<summary>Q3. HWM 이 2048 중 2044 B 입니다. 넘쳤나요?</summary>

모른다 — 그리고 이 레슨 B1 에서 실제로는 8 B 넘쳤다. 가장 낮은 프레임의 맨 아래에 안 쓰인 구멍(패딩)이 있으면 한계 워드가 칠한 채 남아 "4 B 남음" 으로 보인다. 그래서 (1) HWM 이 reserved 에 한 프레임(.su 최대 프레임) 이내로 닿으면 넘침으로 취급하고, (2) 진짜 넘침 판정은 가드(카나리, 더 좋게는 MPU/MSPLIM)에 맡긴다. 꼬리질문 "그럼 HWM 은 왜 재요?" → 여유(margin)를 숫자로 보고하고, 릴리스 간 증가 추세(회귀)를 잡기 위해.

</details>

<details>
<summary>Q4. 스택 아래 카나리가 온전하면 넘침이 없었다고 할 수 있나요?</summary>

아니다. B3: 264 B 지역 배열을 가진 함수가 32 B 가드를 **덮기만 하고 쓰지 않은 채** 그 아래에서 다른 함수가 `.bss` 를 덮었다 — 카나리 온전, SysTick 조용, HWM 1972. 가드는 **가장 큰 단일 프레임 이상**이어야 의미가 있다 (`.su` 최댓값으로 링크 타임에 검사 가능). `-fstack-clash-protection` 도 실측으로 안 됐다: 프로브가 새 프레임 바닥(가드보다 아래)에 찍힌다 — 그 기능은 4 KB 이상 가드 페이지 전제. 대안: 큰 버퍼를 정적/풀로, 가드 키우기, ARMv8-M MSPLIM (SP 값 비교라 건너뛰기 불가).

</details>

<details>
<summary>Q5. 인터럽트는 스택 예산에 어떻게 넣나요? FPU 가 있으면요?</summary>

서로 다른 **선점 우선순위 단계마다** 한 번: 예외 프레임 + 정렬 패딩 4 B + 그 핸들러의 최악 경로. 같은 그룹 우선순위끼리는 서로 선점 못 하므로 그 단계의 최댓값 하나만 (연습 3: IRQ14/15 를 같은 0x80 에 두면 116 B 한 단계). FPU 가 켜진 Cortex-M4F 는 확장 프레임 26 워드 = 104 B. lazy stacking(FPCCR.LSPEN)이면 레지스터 저장은 미뤄도 **공간은 진입 때 예약**된다 — 그래서 페인팅으로 재면 FP 를 안 쓴 ISR 은 그 72 B 가 칠한 채 남아 **과소 측정**될 수 있다. 정적 예산은 항상 104 B 로 잡는다 (이 레슨 숫자로 240 → 456 B). 스펙 세부(ASPEN/LSPEN, 프레임 레이아웃)는 ARMv7-M ARM / Cortex-M4 TRM 확인 권장, 지연 시간 쪽은 BM-A10.

</details>

<details>
<summary>Q6. 재귀나 함수 포인터가 있으면 정적 분석은 어떻게 하나요?</summary>

도구는 답을 못 낸다 — UNBOUNDED / UNKNOWN 으로 남기고 사람이 근거 있는 주석을 단다: 재귀 깊이 상한(입력 크기로 증명), 함수 포인터 후보 집합(디스패치 테이블 전체). 안전 규격에선 아예 금지하는 경우가 많다 (MISRA C:2012 Rule 17.2 재귀 금지 — 확인 권장). 이 레슨의 `recurse_until` 은 시험용이라 제품 경로에서 제외 주석을 달았고, 깊이 86 을 넣으면 2480 B 로 예산을 넘는다는 것도 도구가 정확히 말해 준다 (연습 2).

</details>

<details>
<summary>Q7. (staff) SysTick 에서 카나리를 주기 검사하는 설계가 틀리는 경우는요? 어떻게 바꾸겠나요?</summary>

(1) **지연**: 최대 한 틱 동안 넘침이 진행된다 — B2 처럼 탐지 시점엔 이미 데이터가 망가졌다. (2) **검사기 자신이 같은 스택을 쓴다**: 가장 깊은 순간에 SysTick 이 오면 32 B + 핸들러가 더 아래로 쌓여 넘침을 키운다. (3) 넘친 스택으로 이미 HardFault/잘못된 분기가 나면 검사기는 돌지도 못한다. (4) 건너뛰기(B3)는 원리상 못 잡는다. 바꾼다면: 즉시 탐지 하드웨어 — v7-M 은 가드를 MPU no-access 영역으로(BM-A08, 크기 ≥ 최대 프레임), v8-M 은 MSPLIM; fault 핸들러는 망가진 스택을 쓰지 않도록 전용 fault 스택으로 전환해 덤프(BM-A09); 카나리 검사는 "백업" 으로 남기고 워치독 킥 경로에 묶는다 (탐지 시 킥 중단 → 리셋, TEST-B09 다음 단계).

</details>

<details>
<summary>Q8. -fstack-protector 와 이 레슨의 스택 가드는 무엇이 다른가요?</summary>

`-fstack-protector` 는 함수 **안**의 버퍼 오버런(쓰기가 위로 넘어 복귀 주소로)을 복귀 직전에 잡는다. 스택 가드는 스택 **깊이** 넘침(SP 가 아래로 한계를 넘음)을 잡는다. 방향도, 대상도 다르다. 비용: protect 된 함수마다 프롤로그 2 명령 + 에필로그 비교 (objdump: `ldr/str` + `ldr/eors/bne`). 한계: 슬롯 하나라 패딩 안 오버런(n = 20)은 못 잡고, 쓰기가 카나리를 건너뛰는 인덱스 버그도 못 잡는다. 펌웨어에선 `-fstack-protector-strong` 이 보통 타협점이고, `__stack_chk_guard` 를 부팅마다 난수로 바꿔야 공격 방어 의미가 생긴다.

</details>

<details>
<summary>Q9. (staff) 이걸 FreeRTOS 위로 옮기거나 코어가 둘이면 무엇이 바뀌나요?</summary>

RTOS 에선 스택이 태스크마다 있고(PSP), ISR 은 전부 MSP 를 쓴다 → MSP 예산 = ISR 중첩 예산 + 부팅 코드, 태스크 예산 = 태스크 경로 + 컨텍스트 저장분 (RTOS-M08 이 계산). FreeRTOS 의 `uxTaskGetStackHighWaterMark()` 가 이 레슨의 아래→위 스캔이고, `configCHECK_FOR_STACK_OVERFLOW` 1 은 전환 시 SP 비교, 2 는 스택 끝 16 B 패턴 검사 (확인 권장) — 둘 다 B3 류 건너뛰기에 약하다는 점은 같다. 멀티코어면 코어마다 MSP 가 따로 있고, 페인팅·스캔을 다른 코어가 하면 그 코어 캐시/버스 일관성(ARCH-M12)과 "스캔하는 동안 대상 코어가 쓰는 중" 경쟁을 고려해야 한다 — 보통 각 코어가 자기 스택을 유휴 루프에서 스캔한다.

</details>

## 9. 추가 연습

`practice.c` 는 **호스트 C** 다. 입력은 `run_qemu.sh su` / `solution` 의 실제 출력을 문자열로 붙여 넣은 것 (펌웨어를 바꾸면 다시 붙여 넣는다).

### 연습 1 — `.su` 파서

`su_parse(text, tab, cap, &bad)`: `file:line:col:name<TAB>bytes<TAB>static|dynamic[,bounded]` 줄을 읽는다. 형식 오류·용량 초과는 `bad` 로 보고 (조용히 버리지 않기).

<details><summary>답</summary>

이름은 첫 TAB 앞에서 **마지막** `:` 뒤 (파일 경로에 `:` 가 없다고 가정하지 않기 위해 `strrchr`). 26 개 함수, `path_top` 264, `main` 80. 용량 4 로 읽으면 4 개 + bad 22, 쓰레기 줄 1 개 + `dynamic,bounded` 1 개 → dynamic 플래그 1.

</details>

### 연습 2 — 호출 그래프 최악 경로

`worst(f) = su(f) + max worst(callee)`. 재귀(방문 중 노드 재방문) → 깊이 주석이 있으면 `depth·su(f) + 나머지`, 없으면 UNBOUNDED. `*INDIRECT*` → 후보 주석의 최댓값, 없으면 UNKNOWN. 시험 전용 간선 제외 주석.

<details><summary>답</summary>

주석 없음: `measure_call`, `Reset_Handler` = UNKNOWN. 간접 후보 `{leaf, mid, top, big_sparse}` 를 주면 `measure_call` = 16 + 464 = 480, `Reset_Handler` 는 재귀 때문에 UNBOUNDED. 깊이 86 주석 → `recurse_until` = 86·24 + 328 = 2392, `Reset_Handler` = 8 + 80 + 2392 = 2480 B (> 2048: 시험은 일부러 넘친다). 제품 경로(main→recurse_until 제외) = 568 B. 메모이즈 + 상태 3 색(미방문/방문 중/완료)로 O(V+E).

</details>

### 연습 3 — ISR 중첩 예산

`isr_budget(isrs, n, fp)`: 우선순위 단계마다 (32 또는 104) + 4 + 그 단계 최악 핸들러.

<details><summary>답</summary>

SysTick(0xE0, 4 B) + IRQ14(0x80, 48) + IRQ15(0x40, 80) = 40 + 84 + 116 = 240 B. HardFault(−1, 0 + hardfault_report 8 + sh_hex 24 = 32) 를 더하면 308. FP 확장 프레임이면 단계마다 +72 → 456. IRQ14/15 를 같은 0x80 에 두면 한 단계 116 B.

</details>

### 연습 4 — 출시 체크리스트 판정

정적 = 제품 경로 568 + ISR 308 = 876. 규칙: 정적 ≤ 75 % reserved, 실측(768) ≤ 정적, 최대 프레임 ≤ 가드(아니면 WARN), PAINT_MARGIN ≥ 도우미 프레임 + 32 + 4 + SysTick 4.

<details><summary>답</summary>

876 B = 42 % → PASS, 768 ≤ 876 → PASS, 264 > 32 → WARN (jump-over), 0 + 0 + 32 + 4 + 4 = 40 ≤ 64 → PASS. 실측이 정적보다 크게 나오면 FAIL 이어야 한다 — "측정이 분석을 이겼다" 는 분석이 경로를 놓쳤다는 뜻.

</details>

### 연습 5 — 스캔 방향과 구멍

64 워드 가짜 스택, 가장 낮게 쓰인 위치 L 을 무작위로 고르고 그 위를 20 % 구멍과 함께 채운다 (xorshift32 고정 시드, 5000 회). 아래→위 스캔은 항상 L, 위→아래는?

<details><summary>답</summary>

아래→위 5000/5000 정확, 위→아래 4579 번 틀림 (첫 구멍에서 멈춤). 전부 칠해짐 → 사용 0 (반환 64). 가장 낮은 실제 데이터가 우연히 0xDEADBEEF 이면 한 워드(4 B) 과소 — 이 방법의 원리적 한계.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-A07
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A07_pr && /tmp/BM-A07_pr     # 22/22 passed
# 또는 VM: limactl shell elinux -- bash -lc "bash $PWD/run_qemu.sh practice"
```

## 10. 핵심 정리

- 칠하기: Reset_Handler **맨 처음**, `[_sstack, SP − margin)` 만 — margin ≥ 칠하는 코드 프레임 + 예외 프레임. 살아 있는 프레임을 칠하면 PC = `0xdeadbeee` HardFault.
- HWM: `_sstack` 에서 **위로** 첫 "칠 아님" 워드. 페인팅 = 사용량 **하한**, `.su` + 호출 그래프 + ISR 예산 = **상한**. 실측 ≤ 정적 ≤ 예산.
- 맞춰 본 숫자: `.su` 합 64/200/464 = 실측, IRQ 하나 +32+48, 중첩 +32+80, SP%8==4 → +4 패딩 (xPSR bit9).
- ISR 예산: 선점 단계마다 32 B (FPU 104 B) + 4 B + 핸들러 최악. 같은 우선순위는 한 단계.
- 가드 카나리는 넘침을 **사후에** 본다: 가드 안이면 무피해(B1), 지나치면 이미 손상(B2), 프레임 > 가드면 **못 본다**(B3). 가드 ≥ 최대 프레임, 즉시 탐지는 MPU(BM-A08)/MSPLIM(v8-M).
- HWM 은 넘침 판정기가 아니다: B1 실제 넘침에 2044/2048, B3 에 1972.
- `-fstack-protector` = 함수 안 버퍼 오버런, 복귀 직전, 슬롯 하나(패딩 오버런은 놓침); freestanding 은 `__stack_chk_guard`/`__stack_chk_fail` 직접 정의, 가드는 부팅마다 난수.

## 참고자료

- GCC 매뉴얼 — `-fstack-usage` (`.su` 형식 static/dynamic/bounded), `-fstack-protector[-strong|-explicit]` 와 `stack_protect` 속성, `-fstack-clash-protection`: https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — B1.5.6/B1.5.7 예외 진입 스태킹과 8 바이트 정렬(CCR.STKALIGN, xPSR bit9), B3.1 기본 메모리 맵(XN 구역), B3.2.8 CCR (절 번호 확인 권장).
- Arm, *ARMv8-M Architecture Reference Manual* — MSPLIM/PSPLIM, UsageFault STKOF (확인 권장).
- Arm, *Cortex-M4 Technical Reference Manual* / AN298 "Cortex-M4(F) Lazy Stacking and Context Switching" — FP 확장 프레임 104 B, LSPEN/ASPEN.
- Qualys, "The Stack Clash" (2017) — 가드를 건너뛰는 큰 할당의 원리.
- FreeRTOS 문서 — Stack usage and stack overflow checking (`configCHECK_FOR_STACK_OVERFLOW`, `uxTaskGetStackHighWaterMark`).
- MISRA C:2012 Rule 17.2 (재귀 금지) — 확인 권장.
- 이 트랙: BM-B07/B08 (startup·링커 스크립트), BM-M13 (fault 정책, .noinit), BM-A08 (MPU 가드 — 병행 작성), BM-A09 (크래시 덤프), BM-A10 (인터럽트 지연), RTOS-M08 (태스크 스택 예산, `callgraph.sh`), ARM-M12 (CFSR 해독, 다음 단계).
