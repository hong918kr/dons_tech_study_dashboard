---
id: ARM-A11
time: 110분
updated: 2026-11-15
---
# ARM-A11 컴파일러 최적화 출력 판독: 벡터화와 언롤

## 학습 목표

- `-Rpass / -Rpass-missed / -Rpass-analysis=loop-vectorize` 리마크와 `-fsave-optimization-record`(YAML)를 읽고, "벡터 폭(VF) × 인터리브 수(IC)"와 "안 된 이유"를 함수별로 말한다.
- 벡터화된 루프의 asm 을 블록 단위로 읽는다: `iter.check → vector.memcheck → vector.body → middle.block → vec.epilog → 스칼라 나머지`. 각 블록이 언제 실행되는지 n 으로 설명한다.
- 벡터화를 막는 두 원인, **별칭(aliasing)** 과 **의존성(dependence)** 을 구별한다. `restrict`, 선택식(select) 다시 쓰기, `#pragma clang fp reassociate`, 하위 히스토그램으로 고치고, 고친 결과를 리마크 **와** asm 으로 확인한다.
- "vectorized" 리마크를 그대로 믿지 않는다. 순서 유지 float 리덕션(스칼라 `fadd` 사슬), `-Os` 의 꼬리 접기(lane 마다 `tbz`)처럼 이름만 벡터인 코드를 asm 에서 알아본다.
- `-O2 / -O3 / -Os` 와 gcc 의 결정 차이를 실측 표로 설명한다. ns/원소 측정으로 "벡터화 = 빨라짐"이 아닌 경우(Amdahl, 지연 사슬)를 보인다.

## 1. 문제

면접에서 "이 루프 왜 벡터화 안 됐어?" 다음에는 "그럼 어떻게 고칠 건데, 고쳐졌는지는 어떻게 확인해?"가 바로 따라온다. Cortex-A SoC(Jetson, i.MX8, 차량용 ADAS, 드론 컴패니언 보드)의 센서·영상 루프와 Apple/NVIDIA 칩의 성능 리뷰에서 매일 하는 일이 컴파일러 출력 읽기다. SSD 컨트롤러의 Cortex-R 펌웨어에서도 "-O2 로 바꿨더니 이 함수만 느려졌다"를 설명하려면 같은 기술이 필요하다. ARM-M06 에서는 NEON 을 손으로 썼다. 이번에는 **컴파일러가 쓴 NEON** 을 읽고, 컴파일러가 못 쓴 이유를 찾아서 고친다.

아래 그림은 이 레슨 첫 커널 `saxpy_plain`(`y[i] = a*x[i] + y[i]`)의 실제 `-O2` 출력이다. `-fno-discard-value-names` 를 주면 블록 이름이 asm 주석으로 남는다(asm_mac.txt). 루프 하나가 **경로 4개**짜리 코드가 된다.

```svg
<svg viewBox="0 0 740 420" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">saxpy_plain -O2 의 블록 구조 (Apple clang 21, 블록 이름은 asm 주석 그대로)</text>
<rect x="20" y="36" width="300" height="44" fill="none" stroke="currentColor"/>
<text x="30" y="54" fill="currentColor" font-family="monospace">entry</text><text x="30" y="72" fill="currentColor" font-family="monospace">cbz x2 → ret            (n = 0)</text>
<rect x="20" y="106" width="300" height="44" fill="none" stroke="currentColor"/>
<text x="30" y="124" fill="currentColor" font-family="monospace">iter.check</text><text x="30" y="142" fill="currentColor" font-family="monospace">cmp x2, #3 / b.ls → 스칼라</text>
<rect x="400" y="36" width="320" height="44" class="f-warn" stroke="currentColor"/>
<text x="410" y="54" fill="currentColor" font-family="monospace">vector.memcheck  (겹침 검사)</text><text x="410" y="72" fill="currentColor" font-family="monospace">y ≥ x+4n ? 또는 x ≥ y+4n ?</text>
<rect x="400" y="106" width="320" height="44" fill="none" stroke="currentColor"/>
<text x="410" y="124" fill="currentColor" font-family="monospace">vector.main.loop.iter.check</text><text x="410" y="142" fill="currentColor" font-family="monospace">cmp x2, #16 / b.hs</text>
<rect x="400" y="176" width="320" height="54" class="f-acc" stroke="currentColor"/>
<text x="410" y="194" fill="currentColor" font-family="monospace">vector.body  16개/반복 = VF 4 × IC 4</text><text x="410" y="212" fill="currentColor" font-family="monospace">ldp q×4 · fmla.4s ×4 · stp q×2</text><text x="410" y="226" fill="currentColor" font-size="12">subs x11, #16 / b.ne</text>
<rect x="400" y="256" width="320" height="44" fill="none" stroke="currentColor"/>
<text x="410" y="274" fill="currentColor" font-family="monospace">middle.block / vec.epilog.iter.check</text><text x="410" y="292" fill="currentColor" font-family="monospace">남은 0 → ret · tst x2,#0xc</text>
<rect x="400" y="326" width="320" height="44" class="f-ok" stroke="currentColor"/>
<text x="410" y="344" fill="currentColor" font-family="monospace">vec.epilog.vector.body  4개/반복</text><text x="410" y="362" fill="currentColor" font-family="monospace">ldr q · fmla.4s · str q</text>
<rect x="20" y="256" width="300" height="54" class="f-mut" stroke="currentColor"/>
<text x="30" y="274" fill="currentColor" font-family="monospace">for.body  (스칼라, 1개/반복)</text><text x="30" y="292" fill="currentColor" font-family="monospace">ldr s · fmadd · str s</text><text x="30" y="306" fill="currentColor" font-size="12">폴백 + 마지막 0..3개</text>
<rect x="20" y="346" width="300" height="34" fill="none" stroke="currentColor"/>
<text x="30" y="368" fill="currentColor" font-family="monospace">for.cond.cleanup: ret</text>
<g stroke="currentColor" fill="none">
<line x1="170" y1="80" x2="170" y2="106"/><line x1="170" y1="150" x2="170" y2="256"/>
<line x1="320" y1="128" x2="400" y2="62"/><line x1="400" y1="72" x2="320" y2="262"/>
<line x1="560" y1="80" x2="560" y2="106"/><line x1="560" y1="150" x2="560" y2="176"/>
<line x1="560" y1="230" x2="560" y2="256"/><line x1="560" y1="300" x2="560" y2="326"/>
<line x1="400" y1="282" x2="320" y2="282"/><line x1="400" y1="350" x2="320" y2="300"/>
<line x1="170" y1="310" x2="170" y2="346"/>
<path d="M720 128 H732 V340 H720"/><path d="M720 192 q14 12 0 26"/>
</g>
<g fill="currentColor" font-size="12">
<text x="176" y="98">n ≥ 1</text><text x="176" y="200">n ≤ 3</text><text x="332" y="92">n ≥ 4</text>
<text x="362" y="167">겹침 → 폴백</text><text x="566" y="98">안 겹침</text>
<text x="566" y="168">n ≥ 16</text><text x="730" y="248" text-anchor="end">n &lt; 16</text><text x="566" y="318">≥ 4 남음</text>
<text x="330" y="276">1..3 남음</text><text x="332" y="350">1..3 남음</text>
<text x="10" y="396">n = 37 이면: 16개 × 2회 → 4개 × 1회 → 스칼라 1개.</text><text x="10" y="414">y == x (제자리)도 "겹침"으로 판정 → 전부 스칼라 (Part C 실측 4배 느림).</text>
</g>
</svg>
```

이 레슨의 커널들 (`solution.c`). "주어진" 것은 고치지 않는다. check_vec.sh 가 그 판정을 고정해서 확인한다.

| 커널 | 종류 | -O2 리마크 (실측) | 핵심 |
|---|---|---|---|
| `saxpy_plain` | 주어짐 | vectorized (4, 4) | `vector.memcheck` 로 겹침을 런타임에 확인 |
| `hist_plain` | 주어짐 | not vectorized: cannot identify array bounds | 간접 저장 `h[p[i]]++` |
| `csum_f32_if` | 주어짐 | not vectorized: value that could not be identified as reduction … | if 형태 float 조건부 합 |
| `csum_i32`, `dot_i16` | 주어짐 | (4, 4), (8, 4) | 정수: `cmgt`+`and`+`saddw`, `smlal/smlal2` |
| `ae_fused` | 주어짐 | not vectorized: cannot identify array bounds | 히스토그램 + 합 + 카운트를 한 루프에 |
| `saxpy_restrict` | **TODO A** | (4, 4), memcheck 없음 | `restrict` |
| `csum_f32_sel` | **TODO A** | (4, 4), 순서 유지 | 선택식으로 다시 쓰기 |
| `csum_f32_fast` | **TODO A** | (4, 4), `fadd.4s` | `#pragma clang fp reassociate(on)` |
| `hist4` | **TODO B** | 본 루프 X, 병합 루프 (4, 4) | 하위 히스토그램 4개 |
| `gain_inplace`, `ae_frame_stats` | **TODO C** | (4, 4), (16, 1) | 포인터 1개 제자리 / 루프 분리 |

### Part A — 리마크 읽기와 세 가지 고치기

- `saxpy_restrict`: `saxpy_plain` 과 같은 식에 `restrict` 를 붙인다. asm 에서 `vector.memcheck` 블록이 사라졌는지 확인한다.
- `csum_f32_sel`: `csum_f32_if` 와 **비트까지 같은** 결과를 내면서 리마크가 "vectorized"가 되게 다시 쓴다(플래그 없이).
- `csum_f32_fast`: 재결합을 허락해서 lane 별 누산(`fadd.4s`)이 되게 한다. 비트는 달라져도 되지만 오차 한계 안에 있어야 한다.
- `-O2 / -O3 / -Os` 리마크를 비교하고, `-Os` 에서 아무것도 벡터화되지 않는 이유를 리마크 문장으로 댄다.

### Part B — 별칭·의존성 함정과 히스토그램

- `saxpy_plain` 을 **겹친 버퍼**(y = x+d, d = −40..40, 제자리 포함)로 불러도 순차 의미 그대로인지 테스트한다. `restrict` 버전은 겹친 버퍼로 부르지 않는다(UB). 대신 demo 에서 "약속을 어기면 실제로 무엇이 나오나"를 본다.
- `hist4`: `c[4][256]` 하위 히스토그램 4개로 같은 칸 연속 증가의 의존 사슬을 끊고 마지막에 병합한다. `h` 에 **더하는** 계약, 꼬리 n % 4 를 지킨다. 전부 같은 값인 입력과 0..255 반복 입력으로 테스트한다.

### Part C — 펌웨어 버퍼 두 개

- `gain_inplace(buf, g, b, n)`: DMA 핑퐁 버퍼를 제자리에서 보정한다(`buf = g*buf + b`). `saxpy_plain(buf, buf, …)` 처럼 포인터 2개짜리 함수를 제자리로 부르면 왜 느려지는지 측정으로 보인다.
- `ae_frame_stats(st, img, n, thr)`: 카메라 자동 노출(AE)용 8비트 프레임 통계(히스토그램 256칸, 픽셀 합, `thr` 초과 픽셀 수)를 구한다. `ae_fused` 와 같은 결과를 내되, 루프를 나눠서 합·카운트 부분이 벡터화되게 한다. 그리고 **빨라졌는지 재 본다.**

## 2. Problem (English)

> **Read the vectorizer's output, then fix the loops it could not vectorize.**
>
> Given plain C kernels (`saxpy_plain`, `hist_plain`, `csum_f32_if`, `csum_i32`, `dot_i16`, `ae_fused`), compile with `-O2`, `-O3`, `-Os` and `-Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize`. For each loop, report the vectorization width / interleave count or the reason it failed, and point to the corresponding asm blocks (runtime alias check, vector body, epilogue, scalar remainder).
>
> Then implement:
> 1. `void saxpy_restrict(float *restrict y, const float *restrict x, float a, size_t n)` — no runtime alias check in the output.
> 2. `float csum_f32_sel(const float *a, size_t n, float t)` — sum of `a[i] > t`; **bit-identical** to the sequential loop, yet reported as vectorized, without fast-math flags.
> 3. `float csum_f32_fast(...)` — same sum with reassociation allowed; must satisfy `|s − exact| ≤ γ_m·Σ|a_i|` over the `m` selected elements, `γ_m = m·u/(1 − m·u)`, `u = 2^-24`.
> 4. `void hist4(uint32_t h[256], const uint8_t *p, size_t n)` — adds counts into `h`; break the store-to-load dependency with sub-histograms.
> 5. `void gain_inplace(float *buf, float g, float b, size_t n)` and `void ae_frame_stats(struct ae_stats *st, const uint8_t *img, size_t n, uint8_t thr)` (histogram, pixel sum, count of pixels `> thr`), identical to `ae_fused`.
>
> Constraints: `saxpy_plain` must stay correct for overlapping buffers; never call a `restrict` function with overlapping arguments in tests; plain `cc -O2` (no `-ffast-math`).
>
> Example: with `y = x + 1`, `a = 1`, all ones, `saxpy_plain` must produce `y[i] = i + 2` (a running sum), not `2` everywhere.
>
> Follow-ups: Why did the compiler insert a runtime check? Why does `-Os` not vectorize at all? The remark says "vectorized" — is it actually faster? What changes on Cortex-M55 (Helium) or SVE?

## 3. 시작 코드

`starter.c` = 주어진 커널 + TODO 커널 6개(스텁) + 테스트 하네스. 이 레슨의 TODO 는 C 다. 진짜 "답"은 다음 세 가지가 함께 맞을 때다. ① 하네스 PASS(정확성) ② `check_vec.sh` PASS(컴파일러가 의도대로 판단했는가) ③ asm 을 읽고 설명할 수 있음.

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/ARM-A11_starter && /tmp/ARM-A11_starter
bash check_vec.sh starter      # 벡터화 판정 (starter 는 27/46)
```

처음 실행 결과 (실제 출력 일부):

```text
FAIL starter.c:183  bad == 0
FAIL starter.c:186  y4[0] == 12 && y4[3] == 48 && y4[4] == 60
…
FAIL starter.c:307  s2.sum == 255u * 65536u && s2.bright == 65536 && s2.hist[255] == 65536
11/30 passed
```

`check_vec.sh starter` 는 27/46 이다. 주어진 커널 판정 21개와 "asm 에 X 가 없다"류 판정 6개(스텁에는 루프가 없으니 당연히 없음)는 통과하고, TODO 커널 판정 19개에서 FAIL 한다(`FAIL -O2 saxpy_restrict: expected vectorized 4x4, got 'none'` 등).

관찰용 파일 (테스트와 별개, 각 출력 파일은 실측 그대로):

```bash
bash check_vec.sh                     # solution + practice 판정 63개 (체커가 돌리는 것)
bash demo_remarks.sh > remarks_mac.txt   # -O2/-O3/-Os 리마크, 함수별 명령 수, demo_pragmas.c 의 실패/경고
bash demo_asm.sh > asm_mac.txt           # 이 md 의 asm 발췌 원본
bash demo_flags.sh > flags_mac.txt       # float 조건부 합 5종 × FP 플래그 5종 표 (demo_csum.c)
cc -std=c11 -O2 -c solution.c -Dmain=a11_solution_main -o /tmp/ARM-A11_sol.o && \
  cc -std=c11 -O2 demo_restrict_lie.c /tmp/ARM-A11_sol.o -o /tmp/ARM-A11_lie && /tmp/ARM-A11_lie   # → restrict_lie_mac.txt
bash demo_bench.sh > bench_mac.txt       # ns/원소, 커널을 -O2 와 -Os 로 (3회 실행분 저장)
limactl shell elinux -- bash -lc "bash $PWD/demo_gcc_vm.sh" > gcc_vm.txt   # 같은 파일을 VM 의 gcc 15 로
```

## 4. 힌트

<details>
<summary>힌트 1 — 리마크 플래그 3개는 역할이 다르다</summary>

`-Rpass=loop-vectorize` 는 성공("vectorized loop (vectorization width: 4, interleaved count: 4)"), `-Rpass-missed=` 는 실패 사실, `-Rpass-analysis=` 는 **이유**를 보여 준다. 이유는 analysis 에만 있다. `-Rpass=` 는 정규식 하나만 받는다. `-Rpass=loop-vectorize -Rpass=loop-unroll` 처럼 두 번 쓰면 뒤 것만 남는다(실측: vectorized 리마크가 사라졌다). 둘 다 보려면 `-Rpass='loop-vectorize|loop-unroll'` 로 쓴다. 함수 이름이 필요하면 `-fsave-optimization-record` 로 YAML 을 받는다(`Function:`, `Name:`, `VectorizationFactor:` 필드).

</details>

<details>
<summary>힌트 2 — restrict 는 "이 포인터로만 접근한다"는 약속</summary>

`y` 와 `x` 가 겹칠 수 있으면 `y[i]` 저장이 다음 반복의 `x[i+1]` 읽기를 바꿀 수 있다. 그러면 16개를 한꺼번에 읽는 것이 틀린 답이 된다. 그래서 컴파일러는 겹침을 런타임에 검사하고, 겹치면 스칼라로 간다. `restrict` 는 이 검사를 없애도 되는 근거를 준다. 대신 겹친 버퍼로 부르면 UB 다.

</details>

<details>
<summary>힌트 3 — float 합이 "리덕션"으로 인식되려면</summary>

`if (c) s += a;` 는 IR 에서 `s = c ? s + a : s` 가 된다. `s += c ? a : 0.0f` 는 `s = s + (c ? a : 0)` 이 된다. 둘은 수학적으로 같다. 하지만 리덕션 인식기에게는 뒤쪽만 "평범한 덧셈 사슬"이다. 그리고 s 가 −0 이 될 수 없으면 두 식의 결과는 비트까지 같다. 왜 −0 이 안 되는지 따져 보라.

</details>

<details>
<summary>힌트 4 — 히스토그램은 왜 안 되고, 무엇을 바꿀 수 있나</summary>

lane 4개가 같은 칸 `h[7]` 을 동시에 올리면 충돌이다. NEON 에는 scatter 도 충돌 검출도 없다. 그래서 벡터화는 포기하고 **스칼라 ILP** 를 노린다. 연속된 원소 4개를 서로 다른 표 `c[0..3]` 에 세면, 같은 칸이 연달아 나와도 바로 앞 저장을 기다리지 않는다. 병합 루프(256칸 × 4표 덧셈)는 벡터화된다.

</details>

<details>
<summary>힌트 5 — "vectorized" 인데 왜 안 빨라지나</summary>

asm 의 `vector.body` 를 본다. 덧셈이 `fadd.4s` 인가, `fadd s1, s1, sN` 16개가 줄지어 있나? 뒤쪽은 load 만 벡터이고 덧셈은 여전히 1개씩 순서대로 한다. 걸리는 시간은 fadd 지연 × 원소 수 그대로다. 또 루프의 병목이 다른 데(히스토그램)에 있으면, 나머지를 벡터로 바꿔도 전체 시간은 거의 그대로다.

</details>

## 5. 풀이

### 0. 도구 — 리마크, YAML, 이름 붙은 asm

| 보고 싶은 것 | 명령 | 이 레슨의 사용처 |
|---|---|---|
| 성공/실패/이유 (줄 번호) | `cc -O2 -c f.c -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize` | remarks_mac.txt |
| 함수 이름 + 이유 코드 | `-fsave-optimization-record -foptimization-record-passes=loop-vectorize` → YAML | check_vec.sh |
| 블록 이름이 남은 asm | `cc -O2 -S -fno-asynchronous-unwind-tables -fno-discard-value-names f.c` | asm_mac.txt |
| 벡터화 직전 IR | `-mllvm -print-before=loop-vectorize -mllvm -filter-print-funcs=f` | 아래 csum 분석 |
| gcc 판정 | `gcc -O2 -fopt-info-vec-optimized` / `-fopt-info-vec-missed` | gcc_vm.txt |

YAML 의 `Name:` 은 사람이 읽는 문장 대신 안정된 코드를 준다. check_vec.sh 는 이 코드로 판정한다. 이 레슨에서 실제로 나온 코드: `Vectorized`, `CantIdentifyArrayBounds`, `NonReductionValueUsedOutsideLoop`, `UnsafeDep`, `CantVersionLoopWithOptForSize`, `VectorizationNotBeneficial`, `InterleavingBeneficialButDisabled`, `CantReorderFPOps`(하네스의 double 합), `AllDisabled`(`vectorize(disable)` 참조 루프).

### Part A — saxpy: memcheck 블록 읽기

`-O2` 리마크는 `solution.c:23:5: remark: vectorized loop (vectorization width: 4, interleaved count: 4)` 한 줄이다. VF 4 = float 4개 = q 레지스터 하나, IC 4 = 그런 벡터를 한 반복에 4개(독립 사슬 4개)다. 그래서 `vector.body` 는 한 번에 16개를 처리한다. "언롤"은 여기서 일어난다. 벡터 루프를 4배로 펼친 것이 인터리브다. 겹침 검사는 asm 의 이 두 블록이다(asm_mac.txt).

```text
; %bb.2:                                ; %vector.memcheck
	lsl	x8, x2, #2          ; 4n 바이트
	add	x9, x1, x8          ; x + 4n
	cmp	x0, x9
	b.hs	LBB0_8              ; y ≥ x+4n  → 안 겹침 → 벡터 경로
; %bb.3:                                ; %vector.memcheck
	add	x8, x0, x8          ; y + 4n
	cmp	x1, x8
	b.hs	LBB0_8              ; x ≥ y+4n  → 안 겹침 → 벡터 경로
LBB0_4:                     ; 둘 다 아니면: 겹침 → 스칼라 for.body 로
```

본문은 `ldp q1,q2 / ldp q3,q4`(x 16개), `ldp q5,q6 / ldp q7,q16`(y 16개), `fmla.4s v5, v1, v0[0]` ×4(a 는 by-element 로 `v0[0]`, `dup` 없음), `stp` ×2 다. 경로를 n 별로 정리하면:

| n | 실행되는 블록 | 메모 |
|---|---|---|
| 0 | entry → ret | |
| 1..3 | iter.check → for.body | 검사 비용이 아까워 바로 스칼라 |
| 4..15 (안 겹침) | memcheck → epilog 4개씩 → for.body 0..3개 | main 루프 건너뜀 |
| ≥ 16 (안 겹침) | memcheck → vector.body 16개씩 → epilog → for.body | n = 37: 16×2, 4×1, 스칼라 1 |
| 겹침 (y == x 포함) | memcheck → for.body 전부 | **정확성은 지키지만 4배 느림** |

`saxpy_restrict` 의 진입부는 `iter.check → vector.main.loop.iter.check → vector.ph` 로 바로 간다. memcheck 블록이 없다(asm_mac.txt). 본문은 바이트 하나까지 같다. 함수 길이는 61 → 54 명령이다(remarks_mac.txt 의 objdump 집계).

FP 축약(contraction): Apple clang 의 기본은 `-ffp-contract=on` 이라서 `a * x[i] + y[i]` 가 한 문장 안에서 fused 된다. 스칼라 경로는 `fmadd`, 벡터 경로는 `fmla` 다. 둘 다 반올림을 한 번 하므로 결과가 **비트까지 같다**. 그래서 하네스는 saxpy 를 `memcmp` 로 비교한다. `a*x` 를 별도 변수에 받았다면 축약이 안 되어 비교 방식이 달라졌을 것이다(ARM-B11 참고).

### Part A — float 조건부 합: 같은 수학, 다른 IR

먼저 실패하는 원본을 본다. `csum_f32_if` 의 -O2 asm 은 스칼라 루프 하나다(asm_mac.txt).

```text
LBB2_1:                                 ; %for.body
	ldr	s2, [x0], #4
	fadd	s3, s1, s2          ; 일단 더해 보고
	fcmp	s2, s0
	fcsel	s1, s3, s1, gt      ; 조건이면 채택 (분기 없는 if-conversion)
	subs	x1, x1, #1
	b.ne	LBB2_1
```

벡터화 직전 IR(`-mllvm -print-before=loop-vectorize`)을 보면 이유가 보인다.

```text
csum_f32_if :  %16 = fadd float %12, %14            ; s + a
               %17 = select i1 %15, float %16, float %12   ; c ? s+a : s   ← "조건부 리덕션"
csum_f32_sel:  %16 = select i1 %15, float %14, float 0.0   ; c ? a : 0
               %17 = fadd float %12, %16            ; s + (…)          ← 평범한 fadd 사슬
```

if 형태의 리마크는 `value that could not be identified as reduction is used outside the loop` 이다. 선택식 형태는 `vectorized loop (4, 4)` 다. 두 결과가 비트까지 같은 이유는 이렇다. 고르지 않은 원소에서 if 형태는 s 를 그대로 두고, 선택식 형태는 `s + 0.0` 을 한다. `s + (+0) = s` 가 깨지는 경우는 s = −0 뿐이다(−0 + +0 = +0). 그런데 s 는 +0 에서 시작한다. 그리고 반올림 모드가 기본(최근접)일 때 두 수의 합이 −0 이 되려면 둘 다 −0 이어야 한다. 그래서 s 는 절대 −0 이 되지 않는다. NaN 은 비교가 거짓이라 빠지고, `t = NaN` 이면 아무것도 고르지 않는다. 하네스는 `same_bits` 로 비트 비교를 하고 특수값(NaN, ±Inf, −0)도 넣는다. 모든 s 에서 같게 하고 싶으면 `-0.0f` 를 더하면 된다(`x + (−0) = x` 는 항상 성립). demo 에서 이 형태도 "vectorized (4, 4)" 로 나왔다.

그런데 **"vectorized" 는 빨라졌다는 뜻이 아니다.** `csum_f32_sel` 의 `vector.body` 를 보자.

```text
	fcmgt.4s	v16, v4, v3     ; 비교 4개 × 4
	and.16b	v4, v16, v4          ; 안 고른 lane = 0
	mov	s16, v4[3] … (lane 꺼내기 12개)
	fadd	s1, s1, s4           ; ← 스칼라 fadd 16개가 s1 하나에 줄줄이
	fadd	s1, s1, s21
	… (16개)
```

순서 유지(ordered, strict in-order) 리덕션이다. load·비교·마스크는 벡터로 하고, 덧셈은 C 의 순서 그대로 한 줄 사슬로 한다. 그래서 결과가 스칼라와 비트까지 같고, 속도는 fadd 지연에 묶인다. `csum_f32_fast` 에 `#pragma clang fp reassociate(on)` 을 주면 덧셈이 lane 별 누산 4개(`fadd.4s v3..v6`)로 바뀌고, 끝에서 한 번 트리로 합친다(`fadd.4s` ×3 → `faddp.4s` → `faddp.2s`).

```svg
<svg viewBox="0 0 740 334" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">같은 "vectorized (4, 4)" 리마크, 다른 덧셈 그래프 (vector.body 한 반복 = 16원소)</text>
<text x="10" y="44" fill="currentColor" font-weight="bold">csum_f32_sel — 순서 유지</text>
<g stroke="currentColor">
<rect x="10" y="54" width="22" height="20" class="f-mut"/><rect x="32" y="54" width="22" height="20" class="f-mut"/><rect x="54" y="54" width="22" height="20" class="f-mut"/><rect x="76" y="54" width="22" height="20" class="f-mut"/>
<rect x="104" y="54" width="22" height="20" class="f-mut"/><rect x="126" y="54" width="22" height="20" class="f-mut"/><rect x="148" y="54" width="22" height="20" class="f-mut"/><rect x="170" y="54" width="22" height="20" class="f-mut"/>
<rect x="198" y="54" width="22" height="20" class="f-mut"/><rect x="220" y="54" width="22" height="20" class="f-mut"/><rect x="242" y="54" width="22" height="20" class="f-mut"/><rect x="264" y="54" width="22" height="20" class="f-mut"/>
<rect x="292" y="54" width="22" height="20" class="f-mut"/><rect x="314" y="54" width="22" height="20" class="f-mut"/><rect x="336" y="54" width="22" height="20" class="f-mut"/><rect x="358" y="54" width="22" height="20" class="f-mut"/>
</g>
<text x="10" y="90" fill="currentColor">ldp q · fcmgt.4s · and.16b — 벡터 (16 lane)</text>
<g stroke="currentColor" fill="none"><circle cx="21" cy="120" r="8"/><circle cx="66" cy="120" r="8"/><circle cx="111" cy="120" r="8"/><circle cx="156" cy="120" r="8"/><circle cx="201" cy="120" r="8"/><circle cx="246" cy="120" r="8"/><circle cx="291" cy="120" r="8"/><circle cx="336" cy="120" r="8"/>
<line x1="29" y1="120" x2="58" y2="120"/><line x1="74" y1="120" x2="103" y2="120"/><line x1="119" y1="120" x2="148" y2="120"/><line x1="164" y1="120" x2="193" y2="120"/><line x1="209" y1="120" x2="238" y2="120"/><line x1="254" y1="120" x2="283" y2="120"/><line x1="299" y1="120" x2="328" y2="120"/></g>
<text x="352" y="124" fill="currentColor">… ×16</text>
<text x="10" y="150" fill="currentColor" font-family="monospace">fadd s1, s1, sK  (한 줄 사슬, 16단)</text>
<text x="10" y="170" fill="currentColor">시간 ≈ 16 × fadd 지연. 결과 = 스칼라와 비트 동일</text>
<rect x="10" y="182" width="370" height="26" class="f-warn" stroke="currentColor"/>
<text x="18" y="200" fill="currentColor">실측 min 0.55–0.63 ns/원소 (if 형태 1.16–1.29)</text>
<line x1="392" y1="34" x2="392" y2="288" stroke="currentColor" stroke-dasharray="4 4"/>
<text x="404" y="44" fill="currentColor" font-weight="bold">csum_f32_fast — reassociate(on)</text>
<g stroke="currentColor">
<rect x="404" y="54" width="76" height="20" class="f-acc"/><rect x="486" y="54" width="76" height="20" class="f-acc"/><rect x="568" y="54" width="76" height="20" class="f-acc"/><rect x="650" y="54" width="76" height="20" class="f-acc"/>
</g>
<g fill="currentColor" font-family="monospace" text-anchor="middle"><text x="442" y="68">v3 +=</text><text x="524" y="68">v4 +=</text><text x="606" y="68">v5 +=</text><text x="688" y="68">v6 +=</text></g>
<text x="404" y="90" fill="currentColor" font-family="monospace">fadd.4s ×4 — 서로 독립인 사슬 16개</text>
<text x="404" y="110" fill="currentColor">(누산기 4개 × lane 4개), 반복당 사슬 깊이 1</text>
<g stroke="currentColor" fill="none"><line x1="442" y1="150" x2="483" y2="168"/><line x1="524" y1="150" x2="483" y2="168"/><line x1="606" y1="150" x2="647" y2="168"/><line x1="688" y1="150" x2="647" y2="168"/><line x1="483" y1="174" x2="565" y2="192"/><line x1="647" y1="174" x2="565" y2="192"/></g>
<text x="404" y="138" fill="currentColor" font-size="12">middle.block (루프 끝에서 한 번):</text>
<text x="404" y="210" fill="currentColor" font-family="monospace">fadd.4s ×3 → faddp.4s → faddp.2s</text>
<rect x="404" y="222" width="322" height="26" class="f-ok" stroke="currentColor"/>
<text x="412" y="240" fill="currentColor">실측 min 0.05–0.06 ns/원소 (약 10배)</text>
<text x="404" y="270" fill="currentColor">결과 비트는 달라질 수 있다 → 오차 한계로 검증</text>
<text x="10" y="236" fill="currentColor">if 형태가 더 느린 이유(추정): 사슬 한 단이</text>
<text x="10" y="254" fill="currentColor">fadd + fcsel 두 명령 (asm 위). sel 은 fadd 하나</text>
<text x="10" y="306" fill="currentColor">측정: demo_bench.c, n = 4096, 부하 평균 약 24 (12코어), 3회 실행 × 21표본의 최솟값 범위.</text>
<text x="10" y="324" fill="currentColor">비율로만 읽을 것. 어느 코어(Super/P/E)에서 돌았는지는 고정하지 못했다.</text>
</svg>
```

재결합 결과는 오차 한계로 검증한다. 고른 원소 m 개의 합을 어떤 순서로 하든 `|s − exact| ≤ γ_m Σ|a_i|`, `γ_m = m·u/(1 − m·u)` 다(Higham 4장, 합의 오차). exact 는 double 로 계산한다. 하네스 실측 최악 비율 `g_worst_ratio ≤ 1` 이 PASS 다. 작은 정수 값(|a| ≤ 1000, 합 < 2^24)이면 어떤 순서로 더해도 정확하므로 비트까지 같다는 CHECK 도 넣었다. "순서만 바꿨다"는 것을 따로 확인하는 장치다.

FP 플래그별로 무엇이 되는지는 `demo_flags.sh` 실측 표로 본다(flags_mac.txt). ord = 순서 유지(스칼라 fadd 사슬), re = lane 별 `fadd.4s`.

```text
flags \ function                              v_if       v_sel      v_if_ra    v_sel_ra   v_sum
-O2 (none)                                    no         4x4 ord    no         4x4 re     4x4 ord
-O2 -fassociative-math                        no         4x4 ord    no         4x4 re     4x4 ord
-O2 -fassociative-math -fno-signed-zeros      no         4x4 re     no         4x4 re     4x4 re
-O2 -ffinite-math-only                        4x4 ord    4x4 ord    4x4 re     4x4 re     4x4 ord
-O2 -ffast-math                               4x4 re     4x4 re     4x4 re     4x4 re     4x4 re
```

읽는 법:
- 조건 없는 합 `v_sum` 도 플래그 없이 "4x4" 지만 ord 다. ARM-M06 의 "clang 은 곱만 벡터로 하고 fadd 를 순서대로 남겼다"와 같은 현상이다.
- `-fassociative-math` 단독으로는 아무것도 바뀌지 않았고, `-fno-signed-zeros` 와 함께여야 re 가 되었다. clang 문서상 associative-math 는 signed-zeros·trapping-math 해제와 묶여야 효과가 있다고 알고 있다(문서 확인 권장). 관찰은 위 표 그대로다.
- if 형태(`v_if`, `v_if_ra`)는 reassociate 만으로는 안 되고 `-ffinite-math-only` 가 있어야 됐다. 이 clang 의 조건부 리덕션 인식이 NaN/Inf 가정에 걸려 있다는 뜻으로 읽힌다(내부 이유는 LLVM 소스 확인 권장). **결론: 플래그로 해결하기보다 소스를 선택식으로 바꾸는 쪽이 이식성이 좋다.**
- `#pragma clang fp reassociate(on)` 은 함수 하나에만 적용된다. 전역 `-ffast-math` 는 NaN 검사(`isnan`)까지 지워 버릴 수 있는 위험한 스위치다. 펌웨어에서는 파일 단위 플래그보다 이 pragma 가 낫다.

### Part A — 정수 커널은 왜 그냥 되나

정수 덧셈은 결합법칙이 정확히 성립한다(부호 있는 넘침은 UB 라서 컴파일러가 "없다"고 가정해도 된다). 그래서 플래그 없이 lane 별로 나눈다.

| 커널 | -O2 리마크 | vector.body 핵심 (asm_mac.txt) | 읽는 법 |
|---|---|---|---|
| `csum_i32` (int32 → int64) | (4, 4) | `cmgt.4s` → `and.16b` → `saddw.2d / saddw2.2d` ×8 | 비교 마스크로 0/원값을 고른 뒤 int64 로 넓히며 더함. 누산기 8개 |
| `dot_i16` (int16 → int32) | (8, 4) | `ldp q` ×4 → `smlal.4s / smlal2.4s` ×8, `subs x11, #32` | VF 8 = int16 8개. 넓히는 곱셈-누산이 아래/위 반쪽에 하나씩 → 반복당 32원소 |
| `ae_frame_stats` 합·카운트 (u8) | (16, 1) | `tbl.16b` ×4 → `uaddw/uaddw2.2d` ×8, `cmhi.16b` → `ushll` 사슬 → `add.4s` | 바이트 → u64 로 넓히는 비용이 커서 IC 1 (리마크: interleaving not beneficial) |

`dot_i16` 의 계약은 "부분합이 int32 를 넘지 않는다"다. C 에서 넘침은 UB 라서, 테스트는 |a|,|b| ≤ 2000, n ≤ 200 으로 범위를 증명한 입력만 쓴다(200 × 4·10⁶ < 2^31). `(−32768)²` 하나는 2^30 이라 OK, 두 개면 넘침이므로 C 로 계산하지 않는다.

### Part B — 겹침 테스트와 restrict 거짓말

하네스의 겹침 테스트: `overlap_case(d, 0, n)` 이 y = x + d (d = −40..40) × n 여러 개에서 `saxpy_plain` 결과와 순차 참조를 **버퍼 전체** `memcmp` 로 비교한다(범위 밖 쓰기도 잡힘). 전부 PASS. memcheck 가 겹침을 보고 스칼라로 갔기 때문이다.

`restrict` 버전을 겹친 버퍼로 부르면 UB 라서 테스트에 넣지 않는다. 대신 demo_restrict_lie.c 에서 다른 번역 단위로 컴파일해(호출 지점의 겹침을 컴파일러가 모르게) **이번 빌드에서** 무엇이 나오는지 봤다(restrict_lie_mac.txt, 실제 출력).

```text
b[1..40] after y[i] = 1*x[i] + y[i] with y = x + 1, all ones
saxpy_plain      2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41
saxpy_restrict   2  2  2  2  2  2  2  2  2  2  2  2  2  2  2  2  3  2  2  2  2  2  2  2  2  2  2  2  2  2  2  2  3  2  2  2  3  2  2  2
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">restrict 약속 위반의 실제 결과: 3 이 나오는 자리 = 블록 경계 (y = x+1, n = 40)</text>
<g stroke="currentColor">
<rect x="20" y="40" width="272" height="30" class="f-acc"/><rect x="292" y="40" width="272" height="30" class="f-acc"/><rect x="564" y="40" width="68" height="30" class="f-ok"/><rect x="632" y="40" width="68" height="30" class="f-ok"/>
</g>
<g fill="currentColor" text-anchor="middle"><text x="156" y="60">vector.body #1: i = 0..15</text><text x="428" y="60">vector.body #2: i = 16..31</text><text x="598" y="60">32..35</text><text x="666" y="60">36..39</text></g>
<text x="632" y="35" fill="currentColor" text-anchor="middle">vec.epilog (4개씩)</text>
<g fill="currentColor" font-family="monospace" text-anchor="middle">
<text x="28" y="96">2</text><text x="45" y="96">2</text><text x="62" y="96">2</text><text x="79" y="96">2</text><text x="96" y="96">2</text><text x="113" y="96">2</text><text x="130" y="96">2</text><text x="147" y="96">2</text><text x="164" y="96">2</text><text x="181" y="96">2</text><text x="198" y="96">2</text><text x="215" y="96">2</text><text x="232" y="96">2</text><text x="249" y="96">2</text><text x="266" y="96">2</text><text x="283" y="96">2</text>
<text x="300" y="96" font-weight="bold">3</text><text x="317" y="96">2</text><text x="334" y="96">2</text><text x="351" y="96">2</text><text x="368" y="96">2</text><text x="385" y="96">2</text><text x="402" y="96">2</text><text x="419" y="96">2</text><text x="436" y="96">2</text><text x="453" y="96">2</text><text x="470" y="96">2</text><text x="487" y="96">2</text><text x="504" y="96">2</text><text x="521" y="96">2</text><text x="538" y="96">2</text><text x="555" y="96">2</text>
<text x="572" y="96" font-weight="bold">3</text><text x="589" y="96">2</text><text x="606" y="96">2</text><text x="623" y="96">2</text><text x="640" y="96" font-weight="bold">3</text><text x="657" y="96">2</text><text x="674" y="96">2</text><text x="691" y="96">2</text>
</g>
<g stroke="currentColor" fill="none"><path d="M283 104 q8 24 17 0"/><path d="M555 104 q8 24 17 0"/><path d="M623 104 q8 24 17 0"/></g>
<text x="20" y="150" fill="currentColor">한 블록 안에서는 x[i] 를 모두 먼저 읽는다(ldp q ×4) → 이웃 lane 이 방금 쓴 값을 못 보고 1+1 = 2.</text>
<text x="20" y="170" fill="currentColor">블록의 첫 원소만 이전 블록이 이미 저장한 값(2)을 읽어 2+1 = 3. 순차 의미라면 y[i] = i + 2 (누적).</text>
<text x="20" y="194" fill="currentColor">이 무늬는 VF×IC = 16 과 epilog 폭 4 를 그대로 드러낸다 → 결과만 보고도 asm 의 블록 구조를 맞힐 수 있다.</text>
<text x="20" y="218" fill="currentColor">UB 이므로 다른 컴파일러·플래그·n 에서는 다른 값이 나올 수 있다. "이번에 이렇게 나왔다"일 뿐이다.</text>
</svg>
```

펌웨어 연결: `memcpy` 는 겹침을 허용하지 않고 `memmove` 는 허용한다. 같은 구분이 표준 함수 원형에 `restrict` 로 적혀 있다(C11 7.24.2.1 의 `memcpy(void * restrict s1, …)`). DMA 원형 버퍼에서 "읽기 창과 쓰기 창이 겹칠 수 있는가"는 restrict 를 붙이기 전에 반드시 답해야 하는 질문이다.

### Part B — 히스토그램: 왜 안 되고, hist4 는 무엇을 하나

리마크 두 가지가 서로 다른 이유를 말한다(remarks_mac.txt).

```text
solution.c:28:36: remark: loop not vectorized: cannot identify array bounds        ← hist_plain: h 의 크기를 모름
solution.c:102:34: remark: loop not vectorized: unsafe dependent memory operations in loop. …
Unsafe indirect dependence. Memory location is the same as accessed at solution.c:102:24   ← hist4 꼬리: c 는 크기를 알지만 간접 의존
```

`h` 가 포인터라서 범위를 모르면 런타임 겹침 검사도 만들 수 없다("array bounds"). `c[4][256]` 처럼 크기를 알면 그다음 관문인 의존성에서 막힌다. 같은 칸을 두 lane 이 동시에 올릴 수 있다("indirect dependence"). NEON 에는 gather/scatter 와 충돌 검출이 없다. 그래서 히스토그램은 이 컴파일러로는 어떤 형태로도 벡터화되지 않는다. 연습 4 의 누적합도 같은 관문의 다른 문장(`Backward loop carried data dependence`)으로 막힌다.

`hist4` 의 본 루프 asm 은 `ldrb → ldr → add → str` 4묶음이 서로 다른 표 베이스(x22, x10, x11, x12)로 펼쳐져 있다(asm_mac.txt). 의도는 "같은 값이 연속으로 와도 묶음끼리는 서로 기다리지 않게"다. 병합 루프 `h[b] += c0+c1+c2+c3` 는 (4, 4) 로 벡터화된다.

**측정 결과는 예상과 달랐다** (bench_mac.txt, ns/원소, 3회 실행의 최솟값 범위).

| 입력 | hist_plain | hist4 |
|---|---|---|
| 무작위 바이트 64 KB | 0.243–0.263 | 0.220–0.260 |
| 전부 같은 바이트 | 0.256–0.286 | 0.189–0.241 |

교과서적으로는 "같은 바이트만 들어오면 `str [h+k]` → 다음 `ldr [h+k]` 의 저장→적재 전달 지연이 매 원소에 쌓여 몇 배 느려져야" 한다. M6 에서는 hist_plain 의 같은-바이트 입력이 무작위와 거의 같았고(원소당 약 0.25 ns), hist4 의 이득도 0–30% 수준으로 잡음과 겹친다. 해석(추정, 공개 문서로 확인 못 함): Apple 의 큰 OoO 코어는 같은 주소의 저장→적재를 매우 짧은 지연으로 넘기는 장치(메모리 리네이밍 계열)를 가진 것으로 보인다. Cortex-A53/A55 같은 in-order 코어나 Cortex-M7 에서는 전달 지연이 그대로 보일 수 있다. 하지만 **이 레슨에서는 재지 않았다** — VM 도 같은 M6 코어라 대신할 수 없다. 면접 답은 "기법은 이것이고, 효과는 코어마다 재서 판단한다"이다. 이 기법의 비용도 알아야 한다. `c[4][256]` uint32 = **4 KB 스택**이다. asm 에 `___chkstk_darwin`(스택 프로브)과 stack protector 가 붙었다. 펌웨어 태스크 스택(1–2 KB)에는 그대로 못 넣는다 → 연습 5(uint16 카운터 + 청크 비우기, 2 KB).

### Part B — `-Os`: 왜 아무것도 벡터화되지 않나

`-Os` 리마크(remarks_mac.txt)는 원인을 직접 말한다.

```text
solution.c:23:5: remark: loop not vectorized: runtime pointer checks needed. Enable vectorization of this loop with
                 '#pragma clang loop vectorize(enable)' when compiling with -Os/-Oz
solution.c:42:5: remark: the cost-model indicates that vectorization is not beneficial      (csum_i32, dot_i16, saxpy_restrict … 모두)
solution.c:103:5: remark: vectorized loop (vectorization width: 4, interleaved count: 1)    (hist4 병합 루프: 반복 256 고정)
```

크기 최적화에서는 **스칼라 꼬리 루프를 따로 두지 않는다**. 대신 꼬리를 벡터 본문에 마스크로 접어 넣어야(tail folding) 한다. LLVM LoopVectorize 의 "optsize 면 scalar epilogue 금지" 규칙으로 알고 있다(소스 확인 권장). NEON 에는 마스크 load/store 가 없다. 그래서 비용 모델이 "이득 없음"을 낸다. 반복 수가 256 으로 고정된 병합 루프만 꼬리가 없어서 (4, 1) 로 됐다. 이 해석을 확인하려고 demo_pragmas.c 에서 `-Os` + `restrict` + `vectorize(enable)` 로 강제했다. 리마크는 "vectorized (4, 1)" 이다. 그런데 asm 은 이렇다.

```text
LBB1_2:                                 ; %vector.body
	cmhs.2d	v5, v1, v3          ; lane 인덱스 ≤ n-1 ?  (마스크 계산)
	xtn.2s / uzp1.4h / umov.h w11, v6[0]
	tbz	w11, #0, LBB1_4      ; lane 0 이 꺼져 있으면 건너뜀
; %bb.3:                                ; %pred.store.if
	ldur s6 / ldur s7 / fmadd s6 / stur s6     ; lane 0 을 스칼라로
	… lane 1, 2, 3 도 각각 tbz + 스칼라 ldr/fmadd/str (pred.store.if10/12/14)
```

블록 이름 `pred.store.if` 가 꼬리 접기(predication)의 증거다. "벡터화"된 루프가 실제로는 lane 마다 분기하는 스칼라 코드다. 같은 demo 에서 포인터 2개짜리(겹침 검사가 필요한) 버전은 pragma 를 줬는데도 실패했다. 이때는 `-Wpass-failed` 경고가 나므로 `-Werror` 빌드가 깨진다. 펌웨어 시사점은 이렇다. **SVE(`whilelt`)나 Cortex-M55/M85 Helium(`VCTP`, `LETP`)처럼 predication 이 하드웨어에 있는 ISA 에서는** 꼬리 접기가 싸다. 같은 `-Os` 가 다른 결정을 낼 수 있다(이 레슨에서는 미확인, 다음 단계).

`-O3` 는 이 커널들에서 `-O2` 와 **asm 이 글자 하나까지 같았다**(함수별 objdump 비교, remarks_mac.txt). 다른 것은 `ae_fused`/`ae_frame_stats` 의 n = 0 경로 꼬리 복제(32 → 37 명령)뿐이다. clang 에서 -O3 의 차이는 주로 인라이닝 문턱과 몇몇 패스다. 단순한 루프 벡터화 결정은 -O2 에서 이미 끝난다.

| 함수 | 명령 수 -O2 / -O3 / -Os | -O2 ns/원소 (min) | -Os ns/원소 (min) |
|---|---|---|---|
| `saxpy_plain` (안 겹침) | 61 / 61 / 8 | 0.058–0.063 | 0.236–0.259 |
| `saxpy_restrict` | 54 / 54 / 8 | 0.057–0.063 | 0.226–0.247 |
| `csum_f32_sel` | 93 / 93 / 13 | 0.551–0.625 | 0.560–0.611 |
| `csum_f32_fast` | 76 / 76 / 13 | 0.049–0.062 | 0.547–0.600 |
| `csum_i32` | 88 / 88 / 10 | 0.068–0.085 | 0.225–0.353 |
| `dot_i16` | 81 / 81 / 9 | 0.024–0.029 | 0.264–0.310 |

`csum_f32_sel` 은 리마크상 벡터화됐지만 **-Os 스칼라와 속도가 같다**. 순서 유지 리덕션은 fadd 사슬이 병목이라 load 를 벡터로 바꿔도 소용이 없다. 반대로 `dot_i16` 은 -Os 에서 약 10배 느리다. 코드 크기 8~13 명령 대 54~93 명령의 교환이다. Part C 의 결론과 이어진다. 크기가 중요한 펌웨어라도 핫 루프 함수 하나만 `__attribute__((optimize))` 류나 파일 분리로 -O2 를 주는 것이 흔한 해법이다(clang 은 함수 단위 optimize 속성이 제한적 — 확인 권장).

### Part C — DMA 버퍼 제자리 보정과 자동 노출 통계

`gain_inplace(buf, g, b, n)` 는 포인터가 1개라서 겹침 질문 자체가 없다. -O2 (4, 4), memcheck 없음(check_vec). 측정(-O2, min):

| 호출 | ns/원소 | 경로 |
|---|---|---|
| `saxpy_plain(y, x, …)` 안 겹침 | 0.058–0.063 | vector.body |
| `saxpy_plain(y, y, …)` 제자리 | 0.255–0.282 | memcheck → **스칼라 폴백** |
| `gain_inplace(buf, …)` | 0.039–0.044 | vector.body (검사 없음) |

같은 일을 하는데 범용 2-포인터 함수에 같은 버퍼를 넘기면 4배 이상 느리다. 결과는 맞으니 테스트로는 절대 안 잡힌다. "DMA 완료 ISR 다음의 후처리가 왜 이렇게 느리지?"의 흔한 정답이다. 고치는 법은 두 가지다. ① 제자리 전용 함수(1-포인터)를 둔다. ② 출력 버퍼를 분리하고 `restrict` 로 약속한다.

`ae_frame_stats` 는 루프를 나눈다(loop fission). 히스토그램은 hist4 로(스칼라), 합과 카운트는 한 루프에 묶는다(16, 1). check_vec 에서 `ae_fused` 는 `CantIdentifyArrayBounds`(루프 전체가 스칼라)로, `ae_frame_stats` 는 vectorized 16x1 로 판정됐다. 그런데 측정은:

| | -O2 min ns/원소 | -Os min ns/원소 |
|---|---|---|
| `ae_fused` (한 루프) | 0.300–0.446 | 0.272–0.292 |
| `ae_frame_stats` (분리) | 0.278–0.387 | 0.482–0.511 |

-O2 에서는 거의 같다. 분리 버전은 데이터를 두 번 읽고, 히스토그램(약 0.22–0.26 ns/원소)이 원래부터 시간의 대부분이었다. 넓은 OoO 코어는 스칼라 히스토그램 루프 사이사이에 합·비교 몇 개를 거의 공짜로 끼워 넣는다. 벡터화된 부분의 몫이 작으니 전체는 Amdahl 법칙대로 거의 그대로다(ARCH-A11). `-Os` 에서는 분리가 오히려 1.7배 느리다(두 번째 패스도 스칼라). **"리마크가 vectorized 로 바뀌었다"는 목표가 아니다.** 목표는 측정한 시간이다. 이 판단을 보여 주는 것이 Part C 의 답이다. 정확성은 두 버전 모두 길이 0..65536, 포화 프레임(전부 255, thr 254/255)에서 비트까지 같다.

### gcc 는? (VM, gcc 15.2, gcc_vm.txt)

| | -O2 | -O3 | -Os |
|---|---|---|---|
| 벡터화된 커널 루프 | `dot_i16`, hist4 병합, `ae_frame_stats` 합 (3개) | 9개 (`hist_plain`·`csum_f32_if`·`ae_fused`·hist4 본 루프만 제외) | 없음 |
| saxpy_plain | 안 됨 | `loop versioned for vectorization because of possible aliasing` | 안 됨 |
| csum_f32_if | `not vectorized: unsupported control flow in loop.` | 안 됨 | 안 됨 |

gcc 12 이후 `-O2` 는 매우 싼 비용 모델(`-fvect-cost-model=very-cheap`)로 벡터화한다고 알고 있다. 런타임 버전 분기 같은 추가 비용이 드는 루프는 피한다(gcc 문서 확인 권장). 실측도 O2 에서는 3개뿐이었다. gcc 는 `#pragma clang fp reassociate` 를 무시한다. 그래서 `csum_f32_fast` 도 순서 유지다(`-O3 -S` 에서 `fadd v.4s` 0개, 스칼라 `fadd s` 7개 — 실측). **같은 소스라도 컴파일러가 바뀌면 판정이 바뀐다.** 그래서 펌웨어 CI 에는 check_vec.sh 같은 "판정 회귀 테스트"를 툴체인 버전과 함께 고정해 두는 게 좋다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/ARM-A11_solution && /tmp/ARM-A11_solution
# 30/30 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/ARM-A11_san && /tmp/ARM-A11_san
# 30/30 passed (sanitizer 보고 0)
bash check_vec.sh
# … PASS -O2 saxpy_restrict: asm has no /vector\.memcheck/ … PASS -Os hist4: vectorized 4x1 …
# 63/63 passed
```

check_vec.sh 는 이렇게 동작한다. solution.c / practice.c 를 `-O2 -O3 -Os` 로 다시 컴파일한다. YAML 에서 `함수 / Passed·Missed·Analysis / Name / VF / IC` 를 뽑고, 기대값(`vec saxpy_plain 4x4`, `novec hist_plain CantIdentifyArrayBounds`, `asm_not saxpy_restrict 'vector\.memcheck'`, `asm_has csum_f32_fast 'fadd\.4s'` …)과 비교한다. 기대값은 Apple clang 21(clang-2100.0.123.102) 실측이다. 다른 버전에서 FAIL 이 나면 그 줄이 "무엇이 바뀌었나"를 알려 준다. 하네스만으로는 못 잡는 변이가 있다. 비공개 사본에 넣어 본 결과 — `restrict` 를 지움, `reassociate` pragma 를 지움, 선택식을 if 로 되돌림 — 세 경우 모두 하네스는 30/30 이고 check_vec 만 FAIL(44/46) 했다. 반대로 hist4 병합에서 `c[3]` 누락, 꼬리 누락, `>=` 비교, gain 식 오류는 하네스가 잡았다.

## 7. 자주 하는 실수

1. **리마크만 보고 끝낸다.** "vectorized (4, 4)" 인데 덧셈은 스칼라 사슬이거나(`csum_f32_sel`), lane 마다 분기하는 스칼라 코드다(`-Os` 꼬리 접기). → asm 의 `vector.body` 에서 연산 명령의 형태(`fadd.4s` 냐 `fadd s` 냐)를 확인하고 측정한다.
2. **겹친 버퍼로 restrict 함수를 부른다.** 결과가 블록 경계마다 틀린다(실측 무늬 2…2 3 2…). 테스트에서는 버퍼가 우연히 안 겹쳐 통과하기 쉽다. → 함수 주석에 "겹치면 안 됨"을 쓰고, 제자리 사용이 있으면 1-포인터 버전을 따로 둔다.
3. **`-ffast-math` 를 전역으로 켠다.** 벡터화는 되지만 `isnan()` 검사·NaN 전파·−0 처리까지 같이 사라질 수 있다. 센서 결함 검출 코드가 조용히 죽는다. → 필요한 함수에만 `#pragma clang fp reassociate(on)`, 그리고 오차 한계 테스트.
4. **pragma 가 실패하는 걸 모른다.** `vectorize(enable)` / `vectorize_width` 가 실패하면 `-Wpass-failed` 경고가 나서 `-Werror` 빌드가 깨진다. `-O1`(ASan 빌드)에서는 벡터라이저가 돌지 않아 같은 경고가 난다(practice.c 실측 → 그 함수만 `#pragma clang diagnostic ignored "-Wpass-failed"`).
5. **`-Rpass=` 를 여러 번 쓴다.** 마지막 정규식만 남는다. → `-Rpass='loop-vectorize|loop-unroll'`.
6. **벡터화를 성능으로 착각한다.** `ae_frame_stats` 는 판정이 좋아졌지만 시간은 그대로였다(-Os 에서는 1.7배 느림). → 바꾼 뒤 반드시 ns/원소를 잰다. 부하가 큰 기계면 최솟값과 비율로 읽는다.
7. **4 KB 하위 히스토그램을 태스크 스택에 둔다.** 호스트에서는 `___chkstk_darwin` 이 처리하지만, Cortex-M 태스크 스택에서는 넘침으로 조용히 다른 메모리를 덮는다. → uint16 + 청크(연습 5) 또는 정적 버퍼.
8. **volatile 버퍼나 루프 안 함수 호출을 두고 벡터화를 기대한다.** 리마크: `read with atomic ordering or volatile read`, `call instruction cannot be vectorized`(demo_pragmas.c 실측). → DMA 완료 후 일반 포인터로 바꿔서 처리하고(필요한 배리어·캐시 관리는 그 앞에서, ARM-M05), 콜백은 루프 밖으로 뺀다.

## 8. 면접 Q&A

<details>
<summary>Q1. "vectorization width: 4, interleaved count: 4" 를 풀어 말해 봐. 왜 IC 가 1 이 아니지?</summary>

VF 4 = 128비트 레지스터 하나에 float 4개, IC 4 = 그런 벡터 연산을 반복마다 독립적으로 4개 발행한다. 그래서 반복당 16원소다(`subs x11, #16`). IC 는 벡터 루프의 언롤이다. 목적은 ① FMLA 처럼 지연이 몇 사이클인 명령을 독립 사슬 여러 개로 겹쳐 파이프를 채우는 것 ② 루프 오버헤드(카운터·분기)를 나누는 것 ③ `ldp q` 처럼 2개씩 읽는 명령을 쓰는 것이다. 필요한 사슬 수 ≈ 지연 × 사이클당 발행 수(ARM-M06 Q4 의 리틀 법칙). `ae_frame_stats` 가 IC 1 인 것은 바이트 → u64 넓히기 명령이 이미 많아서 레지스터·발행 폭이 남지 않기 때문이다(리마크: interleaving not beneficial). `#pragma clang loop interleave_count(1)` 로 고정하면 리마크가 "interleaving is beneficial but is explicitly disabled" 로 바뀐다(연습 1).

</details>

<details>
<summary>Q2. 이 asm 에 있는 비교 두 개짜리 블록은 뭐야? 없애려면?</summary>

`vector.memcheck` 다. `y ≥ x + 4n` 이거나 `x ≥ y + 4n` 이면 두 범위가 안 겹치므로 벡터 경로로 간다. 아니면 스칼라 폴백이다. C 는 두 포인터가 같은 배열을 가리킬 수 있다고 보므로 컴파일러는 겹침을 증명할 수 없다. 그래서 루프 버전을 둘 만든다(loop versioning). 없애는 방법은 ① `restrict` 로 약속 ② 포인터를 하나로(제자리 전용 함수) ③ 크기가 정해진 배열/구조체 멤버로 범위를 알려 주기다. 비용은 진입할 때 비교 몇 개뿐이지만, **제자리 호출(y == x)도 겹침으로 판정되어 전부 스칼라로 간다**(실측 4배 느림). 이 검사는 "같은 인덱스끼리만 겹치면 안전"을 모를 만큼 보수적이다.

꼬리: "restrict 를 붙였는데 겹친 버퍼가 들어오면?" → UB. 이번 빌드에서는 16원소 블록의 첫 원소만 이전 블록의 새 값을 읽는 무늬가 나왔다. 다른 빌드에서는 다를 수 있다.

</details>

<details>
<summary>Q3. float 합 루프가 안 벡터화되는데 -ffast-math 말고 방법은?</summary>

먼저 왜 안 되는지 구분한다. ① 조건 없는 합은 clang 이 이미 "벡터화"는 한다. 다만 순서 유지(스칼라 fadd 사슬)라서 빠르지 않다. ② if 형태 조건부 합은 IR 이 `select(c, s+a, s)` 가 되어 리덕션으로 인식되지 않았다. 방법: `s += c ? a : 0.0f` 로 쓰면 덧셈 사슬이 되어 인식된다(비트 동일, 플래그 없음). 그다음 lane 별 누산까지 원하면 그 함수에만 `#pragma clang fp reassociate(on)` 을 준다. 실측 10배. 결과 비트가 바뀌므로 오차 한계 `γ_m Σ|a_i|` 로 검증하고, 재현성이 필요한 경로(골든 비교)는 순서 유지 버전을 남긴다. NEON intrinsic 으로 직접 쓰는 것도 같은 결정(재결합 허락)을 사람이 명시하는 방법이다(ARM-M06).

</details>

<details>
<summary>Q4. 히스토그램은 왜 벡터화가 안 되고, 너라면 어떻게 빠르게 하겠어?</summary>

`h[p[i]]++` 는 간접 주소 read-modify-write 다. lane 끼리 같은 칸을 칠 수 있는데 NEON 에는 scatter 와 충돌 검출이 없다. 리마크는 `cannot identify array bounds`(h 크기 모름), 크기를 알면 `Unsafe indirect dependence` 다. 방법은 ① 하위 히스토그램 k 개로 의존 사슬을 끊고 병합하기(hist4, 병합은 벡터화) ② 값 범위가 작으면(예: 4~16 bin) 비교 + 카운트로 바꾸기. `cmeq` + `sub` 로 bin 마다 세면 완전히 벡터로 된다 ③ SVE2 의 `HISTCNT/HISTSEG` 나 GPU/DSP 하드웨어 히스토그램 블록(ISP 에 흔함)을 쓰기. M6 에서 hist4 의 이득은 잡음 수준이었다(같은-바이트 입력도 느려지지 않음). 그래서 "코어에 따라 재 보고 결정"이 정답이다. 펌웨어라면 스택 4 KB 비용도 같이 말한다.

</details>

<details>
<summary>Q5. -O2 와 -O3 는 뭐가 달라? 펌웨어를 -Os 로 빌드하면 뭘 잃어?</summary>

clang 에서 이 커널들은 -O2 와 -O3 의 asm 이 글자 하나까지 같았다(차이는 꼬리 복제 1곳). 루프 벡터화는 -O2 에서 이미 켜져 있다. -O3 는 주로 인라이닝 문턱 등이 다르다. gcc 는 다르다. gcc 12+ 의 -O2 는 매우 싼 비용 모델이라 3개만 벡터화했고, -O3 에서 전부(버전 분기 포함)였다(VM 실측). -Os 에서는 둘 다 사실상 벡터화를 안 한다. clang 은 크기 최적화에서 스칼라 꼬리를 금지 → NEON 에서 꼬리 접기가 비싸서 "not beneficial" 이다. 잃는 것: `dot_i16` 약 10배, saxpy 약 4배. 얻는 것: 함수당 50~80 명령. 실무 답은 "이미지 전체는 -Os, 핫 루프 몇 개만 -O2(파일 분리 또는 함수 속성) + 그 함수의 벡터화 판정을 CI 에서 고정".

</details>

<details>
<summary>Q6. 리마크는 vectorized 인데 빨라지지 않았다. 가능한 이유를 다 말해 봐.</summary>

① 순서 유지 float 리덕션: 덧셈이 여전히 직렬 사슬이다(`csum_f32_sel` = -Os 스칼라와 같은 0.56 ns). ② 꼬리 접기/predication 이 NEON 에서 lane 별 분기로 풀렸다(`pred.store.if`). ③ 루프의 병목이 벡터화되지 않은 다른 부분이다(Amdahl, `ae_frame_stats`). ④ n 이 작아서 memcheck·epilog 비용이 본문보다 크다(n ≤ 3 은 스칼라로 바로 감). ⑤ 메모리 대역폭에 묶였다(큰 n 의 saxpy 는 바이트/FLOP 가 커서 DRAM 이 상한, ARCH-A11 roofline). ⑥ 런타임 검사에서 겹침 판정이 나서 폴백 경로로 갔다(제자리 호출). 확인 순서는 asm 블록 → 어떤 경로로 갔나(n·포인터 값) → ns/원소 → 병목 모델이다.

</details>

<details>
<summary>Q7. (staff) 팀 코드베이스에서 "핵심 루프가 벡터화된 상태"를 어떻게 유지하겠나?</summary>

이 레슨의 check_vec.sh 를 CI 로 키운다. ① 핫 루프 목록을 함수 이름으로 고정한다. ② `-fsave-optimization-record` YAML 에서 `Passed/VF/IC` 와 실패 `Name` 코드를 뽑아 기대값과 비교한다(문장은 버전마다 바뀌지만 코드는 비교적 안정적). ③ asm 패턴도 확인한다(`vector.memcheck` 없음, `fadd.4s` 있음). 리마크만으로는 순서 유지·꼬리 접기를 못 가르기 때문이다. ④ 툴체인 버전을 고정하고, 버전을 올릴 때 이 테스트가 깨지면 의도적으로 기대값을 갱신한다. ⑤ 정확성은 별도 하네스에 맡긴다(겹침·특수값·오차 한계). ⑥ 성능 회귀는 고정된 보드에서 ns/원소로 본다. 리마크가 맞아도 느려질 수 있기 때문이다. 이 설계가 틀리는 경우: LTO/PGO 로 인라인 위치가 바뀌면 리마크가 다른 함수 이름 아래로 간다. 그래서 핫 함수에 `noinline` 을 두거나 호출자 이름까지 허용해야 한다.

</details>

<details>
<summary>Q8. (staff) 같은 코드를 Cortex-M55(Helium)나 SVE 코어로 옮긴다면 무엇이 달라질까?</summary>

① predication 이 하드웨어에 있다(Helium `VCTP` + low-overhead loop `LETP`, SVE `whilelt`). 그래서 꼬리를 본문에 접는 비용이 싸다. NEON 의 "스칼라 epilog + vec.epilog" 구조나 -Os 의 lane 별 분기가 필요 없을 수 있다(미확인, 컴파일러별 확인 필요). ② SVE 는 gather/scatter 가 있고, SVE2 에는 히스토그램 보조 명령(`HISTCNT`)이 있다. Helium 에도 gather/scatter 형태의 load/store 가 있는 것으로 안다(문서 확인 권장). 그래도 같은 bin 충돌 문제는 남는다. ③ SVE 의 `FADDA` 는 순서 유지 리덕션을 명령 하나로 한다(ARM-M06 Q2). 빠르지는 않지만 의미가 같다. ④ VF 가 컴파일 시점에 고정이 아니다(SVE "vscale × 4"). 그래서 리마크·검사 스크립트의 기대값 형식이 바뀐다. ⑤ M55 는 in-order 코어라서 M6 에서 효과가 없던 hist4 같은 ILP 기법의 효과가 다를 것이다 → 다시 잰다. 이 설계(check_vec 기대값 고정)가 틀리는 경우가 바로 이 이식이다. 타깃마다 기대값 표를 따로 둔다.

</details>

<details>
<summary>Q9. 컴파일러가 자동으로 못 하는데 사람은 할 수 있는 건 뭐가 있어?</summary>

① 별칭에 대한 약속(`restrict`, 1-포인터 API 설계). ② 의미를 바꾸는 재결합 허락(오차 예산을 사람이 정함). ③ 자료 배치 바꾸기: AoS → SoA. 연습 2 의 IMU 구조체는 `ld4` 로 되긴 하지만 필드 4개를 다 읽어서 대역폭 4배를 쓴다. ④ 알고리즘 바꾸기: 히스토그램 → 하위 표, 누적합 → 블록 스캔(연습 4 는 그대로는 불가). ⑤ 데이터 범위 계약(dot_i16 의 int32 넘침 없음, 청크 크기). ⑥ 측정해서 "벡터화하지 않는 결정". 반대로 사람이 손대지 말아야 할 것도 있다. 컴파일러가 이미 넣은 memcheck 를 "느려 보인다"고 restrict 로 지우는 것은, 호출자가 겹침을 안 만든다는 증명이 있을 때만 해야 한다.

</details>

## 9. 추가 연습

1. **폭·인터리브 고정.** `dot_i16_w4` — `#pragma clang loop vectorize_width(4) interleave_count(1)`. 리마크가 어떻게 바뀌나? `-O1` 에서는 무슨 일이 생기나?
2. **AoS 한 필드 합.** `struct imu { int16_t ax, ay, az, gx; }` 배열에서 `ax` 만 합하는 `sum_ax`. 어떤 load 명령이 나오나? 대역폭 관점에서 무엇이 아깝나?
3. **실행 시간 보폭.** `sum_stride(a, n, st)` = Σ a[i·st]. 리마크는 "vectorized" 인데 st = 3 이면 어떤 경로로 가나?
4. **제자리 누적합.** `prefix_sum(a, n)`: `a[i] += a[i-1]`. 벡터화가 안 되는 이유를 리마크 문장으로 대고, 순차 의미가 지켜졌는지 확인하는 테스트를 짜라.
5. **펌웨어용 hist4.** `hist4_u16` — uint16 카운터로 스택 2 KB. 넘침 없이 비우는 최대 청크 크기를 증명하라.

<details>
<summary>연습 1 답 — vectorize_width(4) interleave_count(1)</summary>

리마크는 `vectorized loop (vectorization width: 4, interleaved count: 1)` 과 analysis `the cost-model indicates that interleaving is beneficial but is explicitly disabled or interleave count is set to 1` 이다(check_vec: `InterleavingBeneficialButDisabled`). 기본값은 (8, 4) 였다. VF 4 는 int16 4개 = 64비트 `d` 레지스터 load 로 반쪽만 쓴다. 성능을 위해 쓰는 pragma 가 아니라, "pragma 가 판정을 바꾼다"는 것과 그 대가를 보여 주는 연습이다. `-O1` 에서는 벡터라이저가 돌지 않아 `-Wpass-failed` 경고가 났다(실측). 그래서 이 함수만 `#pragma clang diagnostic ignored "-Wpass-failed"` 로 감쌌다. 결과는 int64 참조와 정확히 같다(n = 0..200).

</details>

<details>
<summary>연습 2 답 — ld4.8h</summary>

(8, 4), 본문에 `ld4.8h { v16, v17, v18, v19 }` 가 4번 나온다(asm_mac.txt). 구조체 8바이트 × 8개를 읽어 필드별로 레지스터 4개에 나눈다(deinterleave). 그중 v16(ax)만 `saddw/saddw2` 로 쓴다. 나머지 3개 필드도 메모리에서 다 읽었으므로 **필요한 데이터의 4배 대역폭**이다. 센서 DMA 가 AoS 로 쓴다면 어쩔 수 없다. 하지만 처리 단계가 여럿이면 SoA(`int16_t ax[N], ay[N], …`)로 한 번 바꾸는 편이 낫다. `-Os` 에서는 `VectorizationNotBeneficial` 로 스칼라다.

</details>

<details>
<summary>연습 3 답 — 보폭 1 버전 분기</summary>

진입부에 `cmp x2, #1` / `b.ne LBB2_12`(스칼라)가 있다(asm_mac.txt). 컴파일러가 "st == 1 이면 연속 접근"인 버전을 따로 만들었다(stride versioning). 리마크의 (4, 4) 는 그 버전 이야기다. st = 0, 2, 3, 7 은 전부 스칼라 루프로 간다. 테스트는 보폭 0(같은 원소 반복), 1, 2, 3, 7 × n 여러 개로 int64 참조와 비교한다. 보폭이 상수 2 라면 `ld2` 로 벡터화되는 것을 실험 단계에서 봤다(이 레슨 파일에는 없음). 그러니 보폭이 정해져 있으면 상수로 드러내는 것이 좋다.

</details>

<details>
<summary>연습 4 답 — loop-carried dependence</summary>

리마크: `loop not vectorized: unsafe dependent memory operations in loop … Backward loop carried data dependence. Memory location is the same as accessed at practice.c:41:44`. 반복 i 가 반복 i−1 이 **쓴** 값을 읽는다. 벡터로 4개를 동시에 읽으면 옛 값을 쓰게 된다. 테스트 `{1,1,1,1,1,1}` → `one[5] == 6` 은 순차 의미의 증거다(옛 값을 읽었다면 2). 누적합을 병렬로 하려면 블록 안에서 log 단계 shift-add(Hillis–Steele)를 하고 블록 사이에 캐리를 전달하는 알고리즘 변경이 필요하다. 컴파일러가 해 주지 않는다.

</details>

<details>
<summary>연습 5 답 — 청크 = 4 × 65532</summary>

한 청크에서 본 루프는 `m4/4` 번씩 각 표를 올리고, 꼬리 ≤ 3개는 `c[0]` 에 더한다. 그래서 칸 하나의 최대값은 `c[0]` 의 `m4/4 + 3` 이다. `CHUNK = 4·65532 = 262128` 이면 최대 65532 + 3 = 65535 = UINT16_MAX 로 딱 맞다. `4·65535` 로 잡으면 마지막 부분 청크 m = 262139(4의 배수 아님)에서 같은 값만 들어올 때 65534 + 3 = 65537 → 넘침이다. 이 변이를 비공개 사본에 넣었더니 하네스의 "같은 값 × 경계 길이" CHECK 가 실제로 FAIL 했다. 스택은 `uint16_t c[4][256]` = 2 KB 이고, 청크마다 256칸 병합(벡터화됨)이 추가된다. 리마크: 병합 루프 (8, 4), 본 루프는 `UnsafeDep`/`CantIdentifyArrayBounds`.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/ARM-A11_practice && /tmp/ARM-A11_practice
# 8/8 passed   (ASan/UBSan -O1 빌드도 8/8, 경고 0)
bash check_vec.sh practice
# 17/17 passed
```

## 10. 핵심 정리

- 리마크 3종: `-Rpass`(성공 VF×IC), `-Rpass-missed`(실패), `-Rpass-analysis`(**이유**). 함수 이름·이유 코드는 `-fsave-optimization-record` YAML, 블록 이름은 `-fno-discard-value-names`.
- 벡터화된 루프 = `iter.check → vector.memcheck → vector.body(VF×IC) → middle.block → vec.epilog → 스칼라`. IC = 벡터 루프 언롤 = 독립 사슬 수.
- 별칭 → 런타임 겹침 검사(제자리 호출도 폴백, 4배 느림) → `restrict` 또는 1-포인터 API. restrict 위반은 UB(블록 경계 무늬).
- float 리덕션: if 형태는 `select(c, s+a, s)` 라 인식 실패 → 선택식으로. 플래그 없으면 순서 유지(스칼라 fadd 사슬, 느림), `#pragma clang fp reassociate(on)` 이면 `fadd.4s`(10배) + 오차 한계 테스트.
- 의존성: 히스토그램(간접), 누적합(loop-carried)은 NEON 자동 벡터화 불가 → 하위 표·알고리즘 변경. 효과는 코어마다 잰다(M6 에서 hist4 이득은 잡음 수준).
- -O2 = -O3(이 커널들, clang), -Os = 거의 벡터화 없음(꼬리 접기 비용). gcc -O2 는 매우 싼 모델로 더 보수적. 컴파일러가 바뀌면 판정도 바뀐다 → 판정 회귀 테스트.
- "vectorized" ≠ 빠름: 순서 유지 리덕션, 꼬리 접기, Amdahl(루프 분리한 `ae_frame_stats`). 마지막 판정은 ns/원소.

## 참고자료

- Clang, Auto-Vectorization in LLVM (리마크, pragma, 런타임 검사): <https://llvm.org/docs/Vectorizers.html>
- Clang, Loop hint pragmas `#pragma clang loop vectorize_width/interleave_count/unroll_count`: <https://clang.llvm.org/docs/LanguageExtensions.html#extensions-for-loop-hint-optimizations>
- Clang, `#pragma clang fp reassociate`: <https://clang.llvm.org/docs/LanguageExtensions.html#extensions-to-specify-floating-point-flags>
- Clang, Optimization remarks `-Rpass*`, `-fsave-optimization-record`: <https://clang.llvm.org/docs/UsersManual.html#options-to-emit-optimization-reports>
- Clang, FP options (`-ffast-math`, `-fassociative-math`, `-ffp-contract`): <https://clang.llvm.org/docs/UsersManual.html#controlling-floating-point-behavior>
- GCC, `-fvect-cost-model`, `-fopt-info`: <https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html>, <https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html>
- ISO C11 (N1570) 6.7.3.1 restrict 의 형식 정의, 7.24.2.1 `memcpy` 원형: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- N. J. Higham, *Accuracy and Stability of Numerical Algorithms*, 2nd ed., SIAM 2002 — 4장 (합의 오차 한계 γ_n).
- Arm Architecture Reference Manual (DDI 0487) — FMLA (by element), SMLAL/SMLAL2, SADDW/SADDW2, LD4, TBL, CMGT/FCMGT: <https://developer.arm.com/documentation/ddi0487/latest>
- Arm, Helium (MVE) 소개 — tail predication `VCTP`/`LETP`: <https://developer.arm.com/Architectures/Helium>
- 관련 레슨: TOOL-B03 (최적화 레벨), ARM-M06 (NEON 손 코딩·순서 유지 합), ARM-B11 (FP 레지스터·fp-contract), ARM-M05 (배리어), ARCH-A11 (Amdahl·roofline), ARCH-A06 (OoO·ILP). 다음 단계: TOOL-A10 (최적화 리포트 읽기: 인라이닝까지), DSP 트랙.
