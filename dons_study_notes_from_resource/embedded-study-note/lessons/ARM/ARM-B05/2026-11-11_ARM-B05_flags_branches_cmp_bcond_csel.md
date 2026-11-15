---
id: ARM-B05
time: 60분
updated: 2026-11-11
---
# ARM-B05 조건 플래그와 분기: CMP, B.cond, CSEL

## 학습 목표

- `CMP`/`CMN`/`TST` 가 NZCV 플래그를 **한 번** 세팅하고, 그 뒤의 조건 코드(condition code)가 그것을 **signed 로 볼지 unsigned 로 볼지** 고른다는 것을 표 하나로 설명한다 (GT/GE/LT/LE vs HI/HS/LO/LS, MI/PL, VS/VC).
- max/min/abs/sign/clamp 를 **분기판**(`B.cond`, `CBZ`, `TBZ/TBNZ`)과 **branchless 판**(`CSEL`/`CSINC`/`CSNEG`)으로 각각 쓰고, 경계값(INT64_MIN/MAX, UINT64_MAX)으로 검증한다.
- `abs(INT64_MIN)` 처럼 C 에서는 UB 인 값을 ISA 가 어떻게 정의하는지 알고, 테스트에서 C 로 계산하지 않고 기대값을 손으로 쓴다.
- GT↔HI 를 바꿔 쓴 버그를 **일부러 심고**, 어떤 테스트가 그것을 잡고 어떤 테스트(작은 양수 무작위)는 절대 못 잡는지 보인다.
- 펌웨어 문맥: 포화 덧셈(VS), 32비트 tick wrap 비교(PL 이지 GE/HS 가 아님), 상태 레지스터 스캔(TST + CBZ).

## 1. 문제

면접관이 crash dump 의 디스어셈블리를 보여 주며 "이 `csel x0, x0, x1, hi` 는 뭐 하는 코드죠? 인자가 `int64_t` 라면?" 하고 물으면, 답은 조건 코드 **두 글자** 에 달려 있다. SSD 컨트롤러 펌웨어(Cortex-R/A 코어)의 LBA 범위 검사, Cortex-M 의 SysTick 타임아웃, Apple/NVIDIA AArch64 SoC 의 드라이버 — 어디서든 "signed 조건을 unsigned 값에 쓴" 버그는 평소엔 안 보이다가 경계값에서만 터진다.

AArch64 에서 비교는 두 단계다. ① `cmp x0, x1` 은 `x0 - x1` 을 계산해 결과는 버리고 **NZCV 4비트만** 남긴다. ② 그다음 명령(`b.gt`, `csel …, hi` …)의 조건 코드가 이 4비트를 어떤 식으로 읽을지 정한다. CPU 는 레지스터 안의 값이 signed 인지 unsigned 인지 **모른다** — 고르는 것은 조건 코드, 즉 우리다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 64비트 패턴, 두 가지 순서 — cmp 는 하나, 조건 코드가 해석을 고른다</text>
  <text x="10" y="58" fill="currentColor">unsigned 순서 (HS HI LO LS)</text>
  <line x1="60" y1="80" x2="700" y2="80" stroke="currentColor" stroke-width="2"/>
  <g stroke="currentColor"><line x1="60" y1="73" x2="60" y2="87"/><line x1="375" y1="73" x2="375" y2="87"/><line x1="385" y1="73" x2="385" y2="87"/><line x1="700" y1="73" x2="700" y2="87"/></g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="60" y="104">0x0000…0000</text><text x="330" y="104">0x7FFF…FFFF</text><text x="440" y="104">0x8000…0000</text><text x="680" y="104">0xFFFF…FFFF</text>
  </g>
  <circle cx="72" cy="80" r="6" class="f-ok" stroke="currentColor"/><text x="72" y="66" fill="currentColor" text-anchor="middle">1</text>
  <circle cx="700" cy="80" r="6" class="f-warn" stroke="currentColor"/><text x="700" y="66" fill="currentColor" text-anchor="end">-1 은 여기 (가장 큼)</text>
  <text x="10" y="148" fill="currentColor">signed 순서 (GE GT LT LE)</text>
  <line x1="60" y1="170" x2="700" y2="170" stroke="currentColor" stroke-width="2"/>
  <g stroke="currentColor"><line x1="60" y1="163" x2="60" y2="177"/><line x1="375" y1="163" x2="375" y2="177"/><line x1="385" y1="163" x2="385" y2="177"/><line x1="700" y1="163" x2="700" y2="177"/></g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="80" y="194">0x8000…0000 (MIN)</text><text x="330" y="194">0xFFFF…FFFF</text><text x="430" y="194">0x0000…0000</text><text x="650" y="194">0x7FFF…FFFF (MAX)</text>
  </g>
  <circle cx="375" cy="170" r="6" class="f-warn" stroke="currentColor"/><text x="365" y="156" fill="currentColor" text-anchor="end">-1</text>
  <circle cx="397" cy="170" r="6" class="f-ok" stroke="currentColor"/><text x="407" y="156" fill="currentColor">1</text>
  <rect x="10" y="212" width="720" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="232" fill="currentColor" font-family="monospace">cmp x0, x1   (x0 = -1, x1 = 1)  →  NZCV = N1 Z0 C1 V0   (실측, demo_flags)</text>
  <text x="20" y="252" fill="currentColor" font-family="monospace">gt: Z==0 &amp;&amp; N==V → 거짓 (-1 &lt; 1)      hi: C==1 &amp;&amp; Z==0 → 참 (0xFFFF… &gt; 1)</text>
</svg>
```

이 레슨에서 쓰는 명령:

| 분류 | 명령 | 하는 일 | 플래그 |
|---|---|---|---|
| 비교 | `cmp a, b` / `cmn a, b` / `tst a, b` | `subs zr, a, b` / `adds zr, a, b` / `ands zr, a, b` 의 별칭 | NZCV 세팅 (TST 는 C=V=0) |
| 조건 분기 | `b.<cond> label` | 조건이 참이면 분기 (±1 MiB) | 읽기만 |
| 0 비교 분기 | `cbz` / `cbnz Rt, label` | Rt == 0 / ≠ 0 이면 분기 (±1 MiB) | **안 건드림** |
| 비트 테스트 분기 | `tbz` / `tbnz Rt, #bit, label` | 비트 하나가 0 / 1 이면 분기 (±32 KiB) | **안 건드림** |
| 조건 선택 | `csel` / `csinc` / `csinv` / `csneg Rd, Rn, Rm, cond` | `cond ? Rn : (Rm, Rm+1, ~Rm, -Rm)` | 읽기만 |
| 위의 별칭 | `cset`, `csetm`, `cinc`, `cinv`, `cneg` | 5. 풀이의 그림 3 | 읽기만 |

플래그 산술 자체(N, Z, C, V 를 덧셈/뺄셈 결과에서 계산하는 법)는 ARCH-B02 에서 했다. 여기서는 그 플래그를 **어느 조건으로 읽느냐** 에 집중한다.

### Part A — max/min 여덟 개: 분기판 vs branchless 판

`smax`, `smin` (int64_t), `umax`, `umin` (uint64_t) 을 각각 `_br`(`cmp` + `b.cond` + `mov`) 과 `_sel`(`cmp` + `csel`) 로 쓴다 → 8개 함수. 모두 leaf 함수, 스택 안 씀.

### Part B — abs / sign / clamp 와 경계값, 그리고 주입 버그

- `sabs_br` (`tbz` 로 부호 비트 63 검사 + `neg`), `sabs_sel` (`csneg`). **정의**: `abs(INT64_MIN) == INT64_MIN` (ISA 의 `neg` 처럼 wrap). C 로 계산하면 UB 이므로 테스트는 기대값을 손으로 쓴다.
- `sign_br` (`cbz` + `tbnz`), `sign_sel` (`asr` + `csinc`) → -1 / 0 / +1.
- `clamp_br`, `clamp_sel` = `min(max(x, lo), hi)`. `lo > hi` 이면 이 정의대로 `hi`.
- `shared_bugs.S` 에 **일부러 틀린** `umax_bug_gt` (unsigned 인데 GT), `smax_bug_hi` (signed 인데 HI) 가 있다. 하네스는 "경계표 테스트는 이 버그를 잡는가?", "작은 양수 무작위 테스트는?" 을 검사한다.

### Part C — 펌웨어: 포화 덧셈, tick 타임아웃, 상태 레지스터 스캔

- `sat_add_s64(a, b)` — 오디오 샘플·PID 적분항처럼 넘치면 감싸지(wrap) 말고 INT64_MAX/MIN 에 붙어야 하는 덧셈. `adds` 의 V 플래그 + `csel …, vs`.
- `tick_reached(now, deadline)` — 32비트 tick 카운터(SysTick, 1 kHz 면 49.7일마다 wrap)에서 "deadline 이 지났나?" 를 wrap 에 안전하게. `cmp w0, w1` + `cset w0, pl`. 같은 파일의 주입 버그 `tick_naive_hs` (그냥 `now >= deadline` unsigned) 와 `tick_bug_ge` (signed GE) 가 각각 어디서 틀리는지 테스트가 잡는다.
- `find_fault(st, n)` — 상태 레지스터 스냅숏 배열에서 ERR(bit5) 또는 OVR(bit6) 이 선 첫 인덱스, 없으면 -1. `cbz`(n==0 이면 메모리를 읽지 않음) + `tst w, #0x60` + `b.ne`.

## 2. Problem (English)

> **Condition flags, conditional branches and conditional select (AArch64).**
>
> Implement in an AArch64 `.S` file, callable from C (leaf functions, no stack, preserve x19–x28):
>
> ```c
> int64_t  smax_br(int64_t a, int64_t b);   int64_t  smax_sel(int64_t a, int64_t b);
> int64_t  smin_br(int64_t a, int64_t b);   int64_t  smin_sel(int64_t a, int64_t b);
> uint64_t umax_br(uint64_t a, uint64_t b); uint64_t umax_sel(uint64_t a, uint64_t b);
> uint64_t umin_br(uint64_t a, uint64_t b); uint64_t umin_sel(uint64_t a, uint64_t b);
> int64_t  sabs_br(int64_t x);  int64_t sabs_sel(int64_t x);    /* abs(INT64_MIN) == INT64_MIN */
> int64_t  sign_br(int64_t x);  int64_t sign_sel(int64_t x);    /* -1, 0, +1 */
> int64_t  clamp_br(int64_t x, int64_t lo, int64_t hi);         /* == min(max(x, lo), hi) */
> int64_t  clamp_sel(int64_t x, int64_t lo, int64_t hi);
> int64_t  sat_add_s64(int64_t a, int64_t b);                   /* saturating */
> int      tick_reached(uint32_t now, uint32_t deadline);       /* wrap-safe, 32-bit ticks */
> int64_t  find_fault(const uint32_t *st, size_t n);            /* first i with st[i] & 0x60, else -1 */
> ```
>
> Constraints: every `_sel` function and `sat_add_s64`, `tick_reached` must be **branchless** (CSEL family only). `_br` versions must use conditional branches (`B.cond`, `CBZ/CBNZ`, `TBZ/TBNZ`). `find_fault` must not read memory when `n == 0`.
>
> Examples: `smax_sel(-1, 1) == 1`, `umax_sel(UINT64_MAX, 1) == UINT64_MAX`, `sabs_sel(INT64_MIN) == INT64_MIN`, `sat_add_s64(INT64_MAX, 1) == INT64_MAX`, `tick_reached(5, 0xFFFFFFF0) == 1`, `tick_reached(0x7FFFFFF0, 0x80000010) == 0`.
>
> Follow-ups: Which condition codes are signed and which unsigned? Show a test that catches `GT` used for unsigned max — and explain why random tests on small positive numbers never will. When is branchless faster? What does `cmp x0, #-5` assemble to?

## 3. 시작 코드

빌드 규칙 (모든 변형 공통): `harness.c` (또는 `practice.c`) + `<변형>.S` + `shared_bugs.S`. 하네스는 테스트와 `extern` 프로토타입만 있어서 혼자서는 링크되지 않는다 (아래 박스의 자동 빌드 줄 `cc … harness.c` 는 무시).

<!-- code: harness.c -->

TODO 가 있는 파일. 함수마다 `mov x0, #0` / `ret` 스텁 자리를 채운다:

<!-- code: starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S shared_bugs.S -o /tmp/ARM-B05_starter && /tmp/ARM-B05_starter
```

모든 빌드(starter/solution/practice)에 같이 링크되는 주입 버그 4개 (고치지 않는다 — 테스트가 이것을 **잡는지** 본다):

<!-- code: shared_bugs.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_bugs.S -o /tmp/ARM-B05_solution   # 위 starter 빌드와 같은 꼴
```

처음 실행 결과 (실제 출력, 앞부분과 끝만):

```text
-- Part A: max/min --
FAIL harness.c:150  smax_br(-1, 1) == 1 && smax_sel(-1, 1) == 1 && smin_br(-1, 1) == -1 && smin_sel(-1, 1) == -1
FAIL harness.c:151  umax_br(UINT64_MAX, 1) == UINT64_MAX && umax_sel(UINT64_MAX, 1) == UINT64_MAX
FAIL harness.c:152  umin_br(UINT64_MAX, 1) == 1 && umin_sel(UINT64_MAX, 1) == 1
  [mismatch] smax_br(0, 1) = 0, want 1
FAIL harness.c:153  s2_edge("mismatch", "smax_br", smax_br, ref_smax) == 0 && s2_rand("smax_br", smax_br, ref_smax) == 0
…
-- Part B: injected bugs (GT vs HI) --
  [caught] umax_bug_gt(0x0, 0xffffffffffffffff) = 0x0, want 0xffffffffffffffff
  [caught] smax_bug_hi(0, -1) = -1, want 0
  umax_bug_gt caught by random: small-positive 0/100000, full-range 50073/100000
-- Part C: firmware --
…
FAIL harness.c:221  find_fault(st, 1000) == 999 && find_fault(st, 999) == -1
10/44 passed
```

통과하는 10개는 주입 버그 검사 8개(내 코드와 무관)와 정답이 우연히 0 인 2개(`clamp(5, 10, 0)`, `tick_reached(0x7FFFFFF0, 0x80000010)`)다. 스텁은 전부 `mov x0, #0` / `ret` 이라 메모리를 건드리지 않고 멈추지도 않는다.

**하네스 읽는 법**: 각 함수는 ① 이름 붙은 예 ② 경계값 16개의 모든 쌍(clamp 는 세 쌍) ③ 고정 시드 xorshift64 무작위 10만 개(전 범위 / -8..8 / 양 끝 근처를 섞음)를 C 참조 모델과 비교한다. 틀리면 첫 불일치를 `[mismatch] f(a, b) = got, want …` 로 찍는다. 참조 모델은 UB 가 없게 썼다: `ref_abs` 는 INT64_MIN 을 따로 처리하고, `ref_sat_add` 는 더하기 **전에** 넘침을 판정한다.

**Linux(ELF) 에서도 같은 파일**: `.S` 맨 위의 `FUNC/ENDF` 매크로가 Mach-O 와 ELF 차이를 흡수한다.

| | Mach-O (macOS) | ELF (Linux) |
|---|---|---|
| C 이름 `smax_sel` 의 심볼 | `_smax_sel` | `smax_sel` |
| 전역 선언 | `.globl _smax_sel` | `.global smax_sel` + `.type smax_sel,%function` |
| 크기 | (없음) | `.size smax_sel, .-smax_sel` |
| 한 줄에 여러 문장 | `%%` 로 구분 (`;` 는 주석 문자) | `;` 로 구분 (주석은 `//`) |

Lima VM(aarch64 Ubuntu)에서 실제로 돌린 결과:

```bash
# 맥 홈은 VM 에서 읽기 전용 → VM 의 /tmp 로 복사해서 빌드 (gcc 15.2, clang 21.1)
limactl shell elinux -- bash -lc 'L=<ROOT>/lessons/ARM/ARM-B05; rm -rf /tmp/ARM-B05_work && mkdir -p /tmp/ARM-B05_work && cp $L/*.c $L/*.S /tmp/ARM-B05_work/ && cd /tmp/ARM-B05_work && uname -m && for CC in gcc clang; do echo "== $CC"; $CC -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_bugs.S -o s && ./s | tail -1; $CC -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S shared_bugs.S -o t && ./t | tail -1; echo "starter exit=${PIPESTATUS[0]}"; $CC -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o p && ./p | tail -1; done; readelf -s s | grep -E "smax_sel|find_fault"'
```

```text
aarch64
== gcc
44/44 passed
10/44 passed
starter exit=1
13/13 passed
== clang
(gcc 와 같은 네 줄: 44/44, 10/44, starter exit=1, 13/13)
   113: 0000000000003344    12 FUNC    GLOBAL DEFAULT   12 smax_sel
   117: 0000000000003434    48 FUNC    GLOBAL DEFAULT   12 find_fault
```

`readelf -s` 의 `FUNC` 타입과 크기(12 B = 명령 3개)는 `.type`/`.size` 덕분이다.

## 4. 힌트

<details>
<summary>힌트 1 — 분기판의 모양은 전부 같다</summary>

`cmp x0, x1` → "x0 가 답인 조건" 이면 `b.<cond> 1f` 로 건너뛰고, 아니면 `mov x0, x1`. 라벨 `1:` 에 `ret`. smax 는 `ge`, smin 은 `le`, umax 는 `hs`, umin 은 `ls`.

</details>

<details>
<summary>힌트 2 — CSEL 은 "x0 를 고를 조건" 을 쓴다</summary>

`csel x0, x0, x1, <cond>` = `cond ? x0 : x1`. smax 는 `gt` (같으면 어느 쪽이든 같다), umax 는 `hi`, smin `lt`, umin `lo`. 조건 코드 이름 앞 글자로 구분: **G/L = signed (Greater/Less)**, **H/L(O/S) = unsigned (Higher/Lower)**.

</details>

<details>
<summary>힌트 3 — abs 와 sign: 부호 비트 하나면 된다</summary>

음수 ⇔ bit 63 이 1 → `tbz x0, #63, 1f` / `tbnz x0, #63, …`. branchless abs 는 `cmp x0, #0` 후 `csneg x0, x0, x0, ge` (= `ge ? x : -x`). sign 은 `asr x8, x0, #63` 이 이미 음수면 -1, 아니면 0 을 준다 — 양수일 때만 1 로 바꾸면 된다: `csinc x0, x8, xzr, le` (= `le ? x8 : 0+1`).

</details>

<details>
<summary>힌트 4 — 포화 덧셈: 포화값을 먼저, adds 는 마지막에</summary>

넘침(V=1)은 a, b 부호가 같을 때만 생기고, 그때 포화 방향은 a 의 부호다. `asr x9, x0, #63` (0 또는 -1) 를 `eor x9, x9, #0x7fffffffffffffff` 하면 INT64_MAX 또는 INT64_MIN. 이 두 명령은 플래그를 안 바꾸므로 `adds` 를 그 뒤에 두고 바로 `csel x0, x9, x0, vs`.

</details>

<details>
<summary>힌트 5 — tick: 알고 싶은 건 "차이의 부호"</summary>

`(int32_t)(now - deadline) >= 0` 는 "32비트로 뺀 결과의 bit31 이 0" 이다. 그건 N 플래그 → 조건 `pl`. `ge` (N==V) 는 "now ≥ deadline as int32" 라는 **다른** 질문이고, `hs` 는 unsigned 대소다. 인자가 `uint32_t` 라 w 레지스터를 써야 한다 (x 의 상위 32비트는 보장 없음, ARM-B01).

</details>

## 5. 풀이

### 0. 조건 코드 16개 — NZCV 에서 바로 유도

`cmp a, b` 는 `a - b` 를 계산한다. N = 결과의 최상위 비트, Z = 결과가 0, **C = 빌림(borrow)이 없었다 (= unsigned 로 a ≥ b)**, V = signed 넘침 (ARCH-B02). 그러면:

- unsigned `a ≥ b` ⇔ C=1 → **HS**. `a > b` ⇔ C=1 이고 Z=0 → **HI**.
- signed `a < b` ⇔ "진짜 차이가 음수". 넘침이 없으면 N 이 곧 부호, 넘침이 있으면 N 이 반대로 뒤집혀 있다 → 진짜 부호 = N⊕V. 그래서 **LT ⇔ N≠V**, **GE ⇔ N==V**, **GT ⇔ Z=0 ∧ N==V**.
- **MI/PL** 은 넘침을 무시한 결과의 bit63 (N) 그대로다. `cmp x, #0` 처럼 넘칠 수 없는 비교에서만 LT/GE 와 같다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">조건 코드 cond[3:0] — 짝(bit0 만 다름)은 서로의 반대 (1111 제외)</text>
  <rect x="10" y="34" width="720" height="24" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-weight="bold"><text x="20" y="51">cond</text><text x="80" y="51">bit0=0</text><text x="180" y="51">조건식</text><text x="330" y="51">bit0=1</text><text x="430" y="51">조건식</text><text x="580" y="51">뜻 (cmp a, b 후)</text></g>
  <rect x="76" y="160" width="244" height="22" class="f-warn" opacity="0.35"/>
  <rect x="76" y="208" width="244" height="22" class="f-acc" opacity="0.35"/>
  <g font-family="monospace" fill="currentColor">
    <text x="20" y="78">000x</text><text x="80" y="78">EQ</text><text x="180" y="78">Z==1</text><text x="330" y="78">NE</text><text x="430" y="78">Z==0</text>
    <text x="20" y="102">001x</text><text x="80" y="102">HS(CS)</text><text x="180" y="102">C==1</text><text x="330" y="102">LO(CC)</text><text x="430" y="102">C==0</text>
    <text x="20" y="126">010x</text><text x="80" y="126">MI</text><text x="180" y="126">N==1</text><text x="330" y="126">PL</text><text x="430" y="126">N==0</text>
    <text x="20" y="150">011x</text><text x="80" y="150">VS</text><text x="180" y="150">V==1</text><text x="330" y="150">VC</text><text x="430" y="150">V==0</text>
    <text x="20" y="174">100x</text><text x="80" y="174">HI</text><text x="180" y="174">C==1&amp;&amp;Z==0</text><text x="330" y="174">LS</text><text x="430" y="174">!(C==1&amp;&amp;Z==0)</text>
    <text x="20" y="198">101x</text><text x="80" y="198">GE</text><text x="180" y="198">N==V</text><text x="330" y="198">LT</text><text x="430" y="198">N!=V</text>
    <text x="20" y="222">110x</text><text x="80" y="222">GT</text><text x="180" y="222">Z==0&amp;&amp;N==V</text><text x="330" y="222">LE</text><text x="430" y="222">!(Z==0&amp;&amp;N==V)</text>
    <text x="20" y="246">111x</text><text x="80" y="246">AL</text><text x="180" y="246">항상</text><text x="330" y="246">(NV)</text><text x="430" y="246">항상 (A64)</text>
  </g>
  <g fill="currentColor"><text x="580" y="78">a == b / a != b</text><text x="580" y="102">unsigned ≥ / &lt;</text><text x="580" y="126">결과 &lt; 0 / ≥ 0</text>
    <text x="580" y="150">signed 넘침 O / X</text><text x="580" y="174">unsigned &gt; / ≤</text><text x="580" y="198">signed ≥ / &lt;</text><text x="580" y="222">signed &gt; / ≤</text></g>
  <text x="10" y="282" fill="currentColor" font-weight="bold">B.cond 인코딩 (실측: 0x5400010a = b.ge, 자기보다 +0x20 바이트 앞)</text>
  <g stroke="currentColor"><rect x="10" y="294" width="200" height="30" class="f-mut"/><rect x="210" y="294" width="380" height="30" class="f-acc"/><rect x="590" y="294" width="40" height="30" class="f-mut"/><rect x="630" y="294" width="100" height="30" class="f-ok"/></g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle"><text x="110" y="314">0101 0100</text><text x="400" y="314">imm19 = 8 (×4 = +32 B)</text><text x="610" y="314">0</text><text x="680" y="314">1010 GE</text></g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="110" y="342">31..24</text><text x="400" y="342">23..5 (±1 MiB)</text><text x="610" y="342">4</text><text x="680" y="342">3..0 cond</text></g>
</svg>
```

진하게 칠한 HI 와 GT 가 이 레슨의 주인공이다 — 둘 다 "a > b" 인데 하나는 C, 하나는 N==V 를 본다.

실제 플래그로 확인 (`demo_flags.c` + `demo_flags.S` — `cmp` 직후 `mrs x0, nzcv` 로 읽어 온다):

```bash
cc -std=c11 -O2 demo_flags.c demo_flags.S -o /tmp/ARM-B05_flags && /tmp/ARM-B05_flags
```

```text
cmp a, b                           flags       | eq ne hs lo mi pl vs vc hi ls ge lt gt le
x: -1, 1                           N1 Z0 C1 V0 |  .  1  1  .  1  .  .  1  1  .  .  1  .  1
x: 1, -1                           N0 Z0 C0 V0 |  .  1  .  1  .  1  .  1  .  1  1  .  1  .
x: 5, 5                            N0 Z1 C1 V0 |  1  .  1  .  .  1  .  1  .  1  1  .  .  1
x: -5, 0   (sign_sel)              N1 Z0 C1 V0 |  .  1  1  .  1  .  .  1  1  .  .  1  .  1
x: INT64_MIN, 1                    N0 Z0 C1 V1 |  .  1  1  .  .  1  1  .  1  .  .  1  .  1
x: INT64_MAX, -1                   N1 Z0 C0 V1 |  .  1  .  1  1  .  1  .  .  1  1  .  1  .
w: 5, 0xFFFFFFF0   (tick wrap)     N0 Z0 C0 V0 |  .  1  .  1  .  1  .  1  .  1  1  .  1  .
w: 0x7FFFFFF0, 0x80000010          N1 Z0 C0 V1 |  .  1  .  1  1  .  1  .  .  1  1  .  1  .
```

읽을 거리 세 개:

1. `-1, 1` 줄: **hi=1, gt=0**. 같은 플래그에서 unsigned 는 "-1 이 크다", signed 는 "-1 이 작다".
2. `INT64_MIN, 1` 줄: 뺄셈이 넘쳐(V=1) 결과가 양수(N=0)로 보인다 → **mi=0 인데 lt=1**. "음수인지 보려면 MI" 가 비교에서는 틀리는 이유.
3. `5, 5` 줄: ge·le·hs·ls 는 참, gt·lt·hi·lo 는 거짓. 같은 값이 어느 쪽으로 가는지가 "≥ vs >" 의 전부다.

> x86 과 반대인 점: x86 의 CF 는 뺄셈에서 **빌림이 있으면 1** (`jb` = CF=1). ARM 의 C 는 **빌림이 없으면 1** 이다 (그래서 HS = "Higher or Same" = C 세트). x86 습관으로 "C=1 이면 작다" 라고 읽으면 정확히 거꾸로다.

### Part A — max/min: 분기 한 개 vs csel 한 개

`smax_sel(-1, 1)` 을 한 명령씩 따라가면:

| # | 명령 | x0 | x1 | NZCV | 설명 |
|---|---|---|---|---|---|
| 0 | (진입) | -1 (0xFFFF…FFFF) | 1 | ? | |
| 1 | `cmp x0, x1` | -1 | 1 | N1 Z0 C1 V0 | -1 - 1 = -2, 넘침 없음 |
| 2 | `csel x0, x0, x1, gt` | **1** | 1 | 그대로 | gt = Z0 ∧ N==V → 거짓 → x1 |
| 3 | `ret` | 1 | | | |

같은 입력에서 2번 줄을 `hi` 로 바꾸면(C1 ∧ Z0 → 참) x0 = -1 이 남는다 — 그게 `smax_bug_hi` 다.

분기판 `smax_br`:

```text
cmp     x0, x1
b.ge    1f          // a >= b 면 x0 가 이미 답 → mov 건너뜀
mov     x0, x1
1:  ret
```

| 판 | 명령 수 (경로) | 특징 |
|---|---|---|
| `_br` | 3 (분기 taken) 또는 4 | 분기 예측이 맞으면 싸다. 틀리면 파이프라인을 비운다 |
| `_sel` | 3 고정 | 데이터에 관계없이 같은 경로. 대신 x0, x1 **둘 다** 준비돼야 결과가 나온다 (데이터 의존) |

`ge` 대신 `gt` 를 써도 smax_br 는 맞다 — a == b 면 어느 쪽을 돌려줘도 같은 값이기 때문이다. 실제로 `sed` 로 이런 변이를 몇 개 만들어 하네스를 돌려 봤다: `hi→gt`, `hs→ge`, `le→lt`(sign_sel), `pl→ge`(tick), `vs→mi`(sat_add), `#0x60→#0x20`(find_fault) 는 모두 FAIL 로 잡혔고, `csneg …, ge → gt` (sabs_sel) 와 clamp_br 의 `b.ge → b.gt` 두 개는 **44/44 그대로 통과**했다. 둘 다 "같을 때 어느 쪽을 골라도 결과가 같은" 동치 변이(equivalent mutant) 라서 테스트가 잡을 방법이 없고, 잡을 필요도 없다 (`-0 == 0`, `x == lo` 면 `lo` 를 돌려도 같은 값).

### Part B — abs/sign/clamp 와 INT64_MIN

**abs**. `neg x0, x0` 는 `sub x0, xzr, x0` 다. 0 - 0x8000…0000 = 0x8000…0000 (mod 2^64) — ISA 에서는 정의된 결과(wrap)이고 예외도 없다. 그래서 이 레슨의 약속은 `abs(INT64_MIN) == INT64_MIN`. C 에서는 `-x` 가 signed 넘침 = **UB** (C11 6.5p5) 라 컴파일러가 "abs 는 음수가 아니다" 를 **가정해도 된다**. `demo_ub.c` 로 실제로 본 결과:

```bash
cc -std=c11 -O2 demo_ub.c solution.S -o /tmp/ARM-B05_ub && /tmp/ARM-B05_ub
```

```text
asm sabs_sel(INT64_MIN)  = -9223372036854775808   negative? 1
C   c_abs(INT64_MIN)     = -9223372036854775808   negative? 0
```

`c_abs` 가 돌려준 값은 분명히 음수인데, `c_abs(x) < 0` 은 0 이라고 한다. `clang -O2 -S` 로 보면 이유가 보인다:

```text
_c_abs:
	cmp	x0, #0
	cneg	x0, x0, mi
	ret
_c_abs_is_negative:
	mov	w0, #0                          ; =0x0
	ret
```

`c_abs_is_negative` 는 아예 **상수 0** 으로 컴파일됐다. `-fsanitize=undefined` 로 빌드하면 UBSan 이 정확히 짚는다:

```text
demo_ub.c:11:76: runtime error: negation of -9223372036854775808 cannot be represented in type 'int64_t' (aka 'long long'); cast to an unsigned type to negate this value to itself
```

그래서 하네스는 `sabs_br(INT64_MIN) == INT64_MIN` 처럼 **기대값을 손으로** 쓰고, 참조 모델 `ref_abs` 도 INT64_MIN 을 먼저 걸러낸다. "C 추상 기계의 UB" 와 "ISA 가 정한 동작" 은 다른 층의 이야기다 — asm 함수의 동작은 ISA 가 정의하고, 그 결과를 C 가 받는 순간부터는 평범한 값이다.

분기판은 `tbz x0, #63, 1f` 한 줄로 "음수 아님" 을 거른다. `tbz` 는 플래그를 안 건드리고 비트 하나만 본다 (±32 KiB 범위). branchless 판은 `cmp x0, #0` + `csneg x0, x0, x0, ge`. clang 은 같은 C 를 `cneg x0, x0, mi` 로 냈는데, `x - 0` 은 넘칠 수 없어서 V=0 → MI ≡ LT 라 둘 다 맞다.

**sign**. `sign_sel(x)` 를 세 입력으로 따라가면 (`asr x8, x0, #63` → `cmp x0, #0` → `csinc x0, x8, xzr, le`):

| x | x8 = asr 63 | NZCV (cmp x,#0) | le (Z==1 ∨ N≠V)? | 결과 |
|---|---|---|---|---|
| -5 | -1 | N1 Z0 C1 V0 (실측) | 참 → x8 | **-1** |
| 0 | 0 | N0 Z1 C1 V0 | 참 → x8 | **0** |
| 7 | 0 | N0 Z0 C1 V0 | 거짓 → xzr+1 | **1** |

`csinc` 의 "거짓이면 Rm+1" 을 `xzr` 에 쓰면 상수 1 이 공짜로 나온다. clang 은 다른 방법을 골랐다 (`demo_clang.c`): `cset w8, ne` (0 아니면 1) 후 `orr x0, x8, x0, asr #63` — 음수면 -1 과 OR 해서 -1. 같은 함수를 쓰는 방법이 여러 개다.

**clamp**. `min(max(x, lo), hi)` 를 그대로 `cmp/csel ge` → `cmp/csel le` 두 쌍으로 쓴다. 두 번째 `cmp` 는 첫 `csel` 의 **결과** 와 비교한다는 점만 주의. `lo > hi` 같은 이상한 입력도 정의가 하나라서 분기판과 branchless 판이 같은 답(hi)을 낸다 (`clamp_br(5, 10, 0) == 0`).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">CSEL 가족: Rd = cond ? Rn : f(Rm) — 그리고 별칭은 "조건을 뒤집어" 인코딩된다</text>
  <rect x="20" y="50" width="70" height="30" rx="4" class="f-mut" stroke="currentColor"/><text x="55" y="70" fill="currentColor" text-anchor="middle" font-family="monospace">Rn</text>
  <rect x="20" y="110" width="70" height="30" rx="4" class="f-mut" stroke="currentColor"/><text x="55" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">Rm</text>
  <rect x="120" y="105" width="110" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="175" y="122" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">Rm / Rm+1</text><text x="175" y="138" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">~Rm / -Rm</text>
  <path d="M260 45 L320 65 L320 125 L260 145 Z" class="f-ok" stroke="currentColor"/>
  <text x="285" y="70" fill="currentColor" font-size="12">1</text><text x="285" y="130" fill="currentColor" font-size="12">0</text>
  <g stroke="currentColor" fill="none"><path d="M90 65 L260 65"/><path d="M90 125 L120 125"/><path d="M230 125 L260 125"/><path d="M290 175 L290 137"/><path d="M320 95 L360 95"/></g>
  <text x="290" y="192" fill="currentColor" text-anchor="middle" font-family="monospace">cond(NZCV)</text>
  <rect x="360" y="80" width="60" height="30" rx="4" class="f-mut" stroke="currentColor"/><text x="390" y="100" fill="currentColor" text-anchor="middle" font-family="monospace">Rd</text>
  <g font-family="monospace" fill="currentColor" font-size="12">
    <text x="450" y="54">csel   → Rm</text><text x="450" y="74">csinc  → Rm + 1</text><text x="450" y="94">csinv  → ~Rm</text><text x="450" y="114">csneg  → -Rm</text>
    <text x="450" y="146">(cond 가 참이면 언제나 Rn)</text>
  </g>
  <rect x="10" y="208" width="720" height="84" rx="6" class="f-mut" stroke="currentColor"/>
  <g font-family="monospace" fill="currentColor" font-size="12">
    <text x="20" y="228">cset  w0, pl     = csinc w0, wzr, wzr, mi    1a9f47e0  (실측 objdump)</text>
    <text x="20" y="246">csetm x0, lt     = csinv x0, xzr, xzr, ge    da9fa3e0</text>
    <text x="20" y="264">cinc  x8, x8, lt = csinc x8, x8, x8, ge      9a88a508</text>
    <text x="20" y="282">cneg  x0, x0, lt = csneg x0, x0, x0, ge      da80a400     cinv 도 같은 방식</text>
  </g>
</svg>
```

별칭이 진짜로 무엇인지는 `demo_aliases.S` 를 어셈블해 `objdump -M no-aliases` 로 봤다 (실측, 일부):

```bash
cc -c demo_aliases.S -o /tmp/ARM-B05_alias.o && objdump -d /tmp/ARM-B05_alias.o && objdump -d -M no-aliases /tmp/ARM-B05_alias.o
```

```text
별칭 표시 (objdump -d)                 진짜 명령 (-M no-aliases)
 0: eb01001f  cmp   x0, x1             subs  xzr, x0, x1
 4: b100141f  cmn   x0, #0x5           adds  xzr, x0, #0x5     <- 소스는 "cmp x0, #-5"
 8: b100041f  cmn   x0, #0x1           adds  xzr, x0, #0x1
 c: 721b053f  tst   w9, #0x60          ands  wzr, w9, #0x60
10: 1a9f47e0  cset  w0, pl             csinc w0, wzr, wzr, mi
14: da9fa3e0  csetm x0, lt             csinv x0, xzr, xzr, ge
18: 9a88a508  cinc  x8, x8, lt         csinc x8, x8, x8, ge
1c: da80a000  cinv  x0, x0, lt         csinv x0, x0, x0, ge
20: da80a400  cneg  x0, x0, lt         csneg x0, x0, x0, ge
```

(두 출력을 나란히 붙인 것만 편집했다.) `cmp x0, #-5` 는 음수 즉시값을 인코딩할 수 없어서(imm12 는 0..4095, 선택적으로 `lsl #12`) 어셈블러가 `cmn x0, #5` (= x0 + 5 로 플래그) 로 바꿨다. 같은 파일의 B.cond 14개는 하위 4비트만 0x0 → 0xd 로 바뀐다 (`54000240 b.eq` … `540000ad b.le`). crash dump 에서 `54xxxxxN` 을 보면 N 이 조건 코드다.

**주입 버그: GT vs HI**. 하네스의 출력 (solution 빌드, 실측):

```text
-- Part B: injected bugs (GT vs HI) --
  [caught] umax_bug_gt(0x0, 0xffffffffffffffff) = 0x0, want 0xffffffffffffffff
  [caught] smax_bug_hi(0, -1) = -1, want 0
  umax_bug_gt caught by random: small-positive 0/100000, full-range 50156/100000
```

- 경계표(0, ±1, ±2, ±7, INT64_MIN/MAX 근처, 2^31 근처, 2^32 근처)는 **첫 몇 쌍 안에** 버그를 잡는다: `umax(0, UINT64_MAX)` 에서 GT 는 UINT64_MAX 를 -1 로 보고 0 을 고른다.
- **작은 양수 무작위**(`xs() >> 1`, 둘 다 < 2^63)는 10만 번 중 **0번**. 이건 운이 아니라 수학이다: 두 값의 bit63 이 모두 0 이면 signed 순서와 unsigned 순서가 같아서 GT ≡ HI. 하네스는 이것을 `CHECK(small == 0)` 으로 **확인한다**. 실무에서 "랜덤 테스트 많이 돌렸는데요" 가 이 버그를 못 잡는 이유.
- 전 범위 무작위는 약 절반(50156/100000)을 잡는다 — 정확히 "두 값의 bit63 이 다른" 경우가 확률 1/2 이니까.

교훈: 부호 관련 버그를 찾는 테스트는 **부호 경계(0, -1, INT_MIN, INT_MAX, UINT_MAX)를 반드시 포함**해야 한다. 무작위는 보조다.

### Part C — 펌웨어 세 가지

**포화 덧셈 `sat_add_s64(INT64_MAX, 1)`**:

| # | 명령 | x0 | x9 | NZCV |
|---|---|---|---|---|
| 1 | `asr x9, x0, #63` | 0x7FFF…FFFF | 0 | (안 바뀜) |
| 2 | `eor x9, x9, #0x7fffffffffffffff` | | 0x7FFF…FFFF | (안 바뀜) |
| 3 | `adds x0, x0, x1` | 0x8000…0000 | | N1 Z0 C0 V1 |
| 4 | `csel x0, x9, x0, vs` | **0x7FFF…FFFF** | | V=1 → x9 |

순서가 중요하다: `asr`/`eor` 는 플래그를 안 바꾸니 앞에 둘 수 있지만, `adds` 와 `csel` 사이에 플래그를 바꾸는 명령(`adds`, `cmp`, `tst`, `ands` …)이 끼면 V 가 사라진다. clang 은 다른 길을 갔다 (`demo_clang.c` 의 `__builtin_add_overflow` 판): 결과 r 을 먼저 만들고 `asr x9, x8, #63` / `eor x9, x9, #0x8000000000000000` 로 "넘쳤다면 결과의 부호가 뒤집혔으니 그 반대쪽 끝" 을 만든다. 결과는 같다.

**tick 비교**: SysTick ISR 이 `g_tick++` (uint32_t) 하고, 드라이버가 `deadline = g_tick + timeout` 으로 타임아웃을 건다고 하자. tick 은 2^32 마다 wrap 한다 (1 kHz 면 49.7일 — Windows 95/98 이 49.7일 연속 가동 후 멈췄다는 유명한 버그가 바로 이 숫자로 알려져 있다).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">32비트 tick 은 원(mod 2³²) — "지났나?" = now 기준 뒤쪽 반원에 있나</text>
  <path d="M170 150 L170 50 A100 100 0 0 0 170 250 Z" class="f-ok" stroke="currentColor"/>
  <path d="M170 150 L170 50 A100 100 0 0 1 170 250 Z" class="f-warn" stroke="currentColor"/>
  <circle cx="170" cy="50" r="6" class="f-acc" stroke="currentColor"/><text x="170" y="40" fill="currentColor" text-anchor="middle" font-weight="bold">now</text>
  <circle cx="136" cy="56" r="5" fill="currentColor"/><text x="128" y="48" fill="currentColor" font-size="12" text-anchor="end">deadline (now−21)</text>
  <circle cx="204" cy="56" r="5" fill="currentColor"/><text x="212" y="48" fill="currentColor" font-size="12">deadline (now+32)</text>
  <text x="105" y="150" fill="currentColor" text-anchor="middle" font-size="12">과거 반원</text><text x="105" y="166" fill="currentColor" text-anchor="middle" font-size="12">→ reached</text>
  <text x="235" y="150" fill="currentColor" text-anchor="middle" font-size="12">미래 반원</text><text x="235" y="166" fill="currentColor" text-anchor="middle" font-size="12">→ 아직</text>
  <text x="170" y="272" fill="currentColor" text-anchor="middle" font-size="12">now ± 2³¹ (경계)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="320" y="62">tick_reached = (int32_t)(now − dl) &gt;= 0</text>
    <text x="320" y="82">            = cmp w0, w1 / cset w0, pl   (N==0)</text>
    <text x="320" y="118">(now, dl)                 NZCV  hs  ge  pl</text>
    <text x="320" y="140">(5, 0xFFFFFFF0)        N0Z0C0V0  0✗  1   1✓</text>
    <text x="320" y="162">(0x7FFFFFF0,0x80000010) N1Z0C0V1  0   1✗  0✓</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="320" y="196">· hs (unsigned 대소): dl 이 wrap 직전, now 가 wrap 직후면</text>
    <text x="320" y="214">   "아직" 이라고 해서 타임아웃을 놓친다.</text>
    <text x="320" y="236">· ge (signed 대소): 0x7FFF…→0x8000… 를 지날 때 틀린다</text>
    <text x="320" y="254">   (V=1). 여기선 32 tick 일찍 "지났다" 고 한다.</text>
    <text x="320" y="276">· pl: 차이의 부호만 본다 → |now − dl| &lt; 2³¹ 이면 항상 맞다.</text>
  </g>
</svg>
```

표의 NZCV 와 hs/ge/pl 값은 위 `demo_flags` 실측 줄 그대로다. 하네스는 경계 13개 × 13개 쌍 + 무작위 10만 쌍을 C 참조 `(uint32_t)(now - dl) < 0x80000000u` 와 비교한다 (C 에서 `(int32_t)` 로 바꾸는 것은 범위 밖이면 구현 정의(implementation-defined, C11 6.3.1.3p3) 라 참조 모델에서는 피했다). Linux 커널의 `time_after_eq(a, b)` 가 바로 `((long)((a) - (b)) >= 0)` 이다.

주입 버그 결과 (실측): `[caught] tick_naive_hs(0x00000000, 0x80000001) = 0, want 1`, `[caught] tick_bug_ge(0x00000000, 0x80000000) = 1, want 0` — 그리고 이름 붙은 재현 `tick_naive_hs(5, 0xFFFFFFF0) == 0`, `tick_bug_ge(0x7FFFFFF0, 0x80000010) == 1` 이 CHECK 로 고정돼 있다.

인자가 `uint32_t` 이므로 w 레지스터로 비교한다. x0 의 상위 32비트는 AAPCS64 상 보장이 없다 (Apple 은 char/short 만 32비트로 확장해서 넘김 — ARM-B01). `cmp x0, x1` 로 쓰면 상위 쓰레기가 비교에 섞일 수 있고, 무엇보다 64비트 차이의 부호는 32비트 wrap 과 무관하다. clang 은 이 함수를 플래그 없이 `sub w8, w0, w1` / `mvn w8, w8` / `lsr w0, w8, #31` (차이의 bit31 을 뒤집어 꺼냄)로 냈다 — 역시 같은 계산이다.

**상태 레지스터 스캔 `find_fault`**:

```text
    mov   x8, #0              // i = 0
    cbz   x1, 3f              // n == 0 → 메모리 안 읽고 -1
1:  ldr   w9, [x0, x8, lsl #2]
    tst   w9, #0x60           // ERR|OVR ?   (ands wzr, w9, #0x60)
    b.ne  2f                  // Z==0 → 찾음
    add   x8, x8, #1
    cmp   x8, x1
    b.lo  1b                  // i < n  — size_t 이므로 unsigned 조건
3:  mov   x0, #-1
    ret
2:  mov   x0, x8
    ret
```

- 여러 비트 중 "하나라도" 는 `tst` + `b.ne`. 비트가 **딱 하나** 면 `tbnz w9, #5, found` 가 한 명령으로 더 짧다 (플래그도 안 씀).
- `cbz x1` 은 루프를 "아래에서 검사(bottom-tested)" 로 만들기 위한 입구 검사 — 컴파일러도 이렇게 낸다. 하네스가 `find_fault(NULL, 0)` 을 부르므로 이 줄이 빠지면 NULL 을 읽고 죽는다.
- `b.lo` 대신 `b.lt` 를 쓰면 n ≥ 2^63 에서 틀리는 signed/unsigned 버그다 (실제 배열에선 안 나오지만 면접관은 이걸 본다).

### 분기 vs branchless — 실측

`demo_bench.c`: 2^20 쌍에 대해 `smax_br` / `smax_sel` 을 직접 호출, 8회 반복 × 7번 측정의 중앙값, "호출당 ns" (bl/ret 비용 포함). M6, 다른 작업이 돌고 있는 맥에서 연속 3회:

```bash
cc -std=c11 -O2 demo_bench.c solution.S -o /tmp/ARM-B05_bench && /tmp/ARM-B05_bench
```

```text
pattern                      smax_br ns/call  smax_sel ns/call
predictable (a > b always)       0.99             0.74
periodic (a>b, a<b, ...)         0.88             0.52
random 50/50                     3.69             0.77
(2회차) 0.95/0.74, 1.07/0.70, 3.83/0.70     (3회차) 0.96/0.74, 0.93/0.68, 3.80/0.78
```

- branchless 는 데이터 패턴과 무관하게 거의 일정 (0.5–0.8 ns).
- 분기판은 예측 가능하면 비슷하지만, **무작위면 약 3 ns/호출 더 든다**. 절반이 예측 실패라고 보면 실패 한 번에 대략 6 ns 라는 계산이 나오는데, 측정 루프의 다른 비용이 섞인 거친 값이라 사이클 수로 단정하지는 말 것.
- 처음엔 배열을 4096 쌍으로 잡았더니 무작위인데도 1.2 ns 밖에 안 나왔다 (같은 4096개 패턴이 2000번 반복됨). 예측기가 반복되는 패턴을 상당 부분 학습한 것으로 보인다 (추정). 벤치마크 데이터가 예측기에 "외워지지" 않게 크게 잡아야 한다.
- 그래서 규칙: **예측이 어려운 데이터 의존 선택 → csel**. 거의 항상 한쪽으로 가는 검사(에러 경로, 범위 체크) → 분기가 낫거나 같다 (branchless 는 양쪽 값을 다 계산해야 하고, 결과가 입력 둘 다에 의존한다).
- `cmp` + `b.cond` 를 디코더가 한 마이크로옵으로 합치는 compare-and-branch fusion 을 많은 코어가 한다고 알려져 있다 (Apple 코어도 공개 리버스엔지니어링 자료 기준으로 그렇다 — Apple 공식 문서는 아님, 확인 권장). 그래서 "cmp + b.cond = 명령 2개" 로 비용을 세면 안 된다.

### 컴파일러는 C 에서 무엇을 냈나 (`demo_clang.c`, 실측)

```bash
cc -std=c11 -O2 -S -fno-asynchronous-unwind-tables demo_clang.c -o - | grep -v '^\s*\.'
```

| C 함수 | clang -O2 (Apple clang 21) |
|---|---|
| `a > b ? a : b` (int64) | `cmp x0, x1` / `csel x0, x0, x1, gt` |
| 같은 식 (uint64) | `cmp x0, x1` / `csel x0, x0, x1, hi` — **타입이 조건 코드를 고른다** |
| `x < 0 ? -x : x` | `cmp x0, #0` / `cneg x0, x0, mi` |
| `(x > 0) - (x < 0)` | `cmp x0, #0` / `cset w8, ne` / `orr x0, x8, x0, asr #63` |
| `if (x<lo) x=lo; if (x>hi) x=hi;` | `cmp`/`csel …, gt` / `cmp`/`csel …, lt` — `if` 두 개도 분기 없이 |
| `__builtin_add_overflow` 포화 | `adds` / `asr` / `eor #0x8000000000000000` / `csel …, vs` |
| `(int32_t)(now - dl) >= 0` | `sub w8, w0, w1` / `mvn w8, w8` / `lsr w0, w8, #31` |

C 에서 `if` 를 써도 단순한 선택이면 clang 은 csel 로 바꾼다. 거꾸로, 손으로 쓴 asm 이 아니면 "분기판" 을 고집할 방법이 마땅치 않다 — 그래서 이 레슨은 asm 으로 두 판을 다 쓴다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_bugs.S -o /tmp/ARM-B05_solution && /tmp/ARM-B05_solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined harness.c solution.S shared_bugs.S -o /tmp/ARM-B05_asan && /tmp/ARM-B05_asan
```

실제 출력 (ASan/UBSan 빌드도 같은 출력, 경고 없음, exit 0, 0.02 s):

```text
-- Part A: max/min --
-- Part B: abs/sign/clamp --
-- Part B: injected bugs (GT vs HI) --
  [caught] umax_bug_gt(0x0, 0xffffffffffffffff) = 0x0, want 0xffffffffffffffff
  [caught] smax_bug_hi(0, -1) = -1, want 0
  umax_bug_gt caught by random: small-positive 0/100000, full-range 50156/100000
-- Part C: firmware --
  [caught] tick_naive_hs(0x00000000, 0x80000001) = 0, want 1
  [caught] tick_bug_ge(0x00000000, 0x80000000) = 1, want 0
44/44 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| unsigned max 에 `csel …, gt` | GT 는 N==V 를 본다 → 0xFFFF… 를 -1 로 해석 | `csel x0, x0, x1, hi` |
| signed 비교 후 `b.hi`/`b.lo` | C 플래그는 unsigned 대소 → -1 이 가장 큼 | `b.gt` / `b.lt` |
| "음수면" 을 `b.mi` 로 (`cmp a, b` 뒤) | MI 는 넘침 무시. `cmp INT64_MIN, 1` 은 N=0 (실측) | `b.lt` (N≠V). `cmp x, #0` 뒤에만 MI ≡ LT |
| x86 습관으로 "C=1 이면 a<b" | ARM 의 C 는 **빌림 없음** = a ≥ b | `b.hs` = a ≥ b, `b.lo` = a < b |
| `adds` 와 `csel …, vs` 사이에 `cmp`/`tst` | 플래그가 덮여 V 가 사라짐 | 플래그 안 바꾸는 명령(`asr`, `eor`, `mov`)만 사이에 |
| `uint32_t` 인자를 `cmp x0, x1` 로 | 상위 32비트는 보장 없음 + 32비트 wrap 의미가 사라짐 | `cmp w0, w1` |
| tick 비교를 `cset …, hs` 또는 `ge` | wrap 지점 / 0x7FFF…→0x8000… 지점에서 틀림 | `cmp w0, w1` / `cset w0, pl` |
| 루프 카운터(size_t)를 `b.lt` 로 | n ≥ 2^63 이면 음수로 보고 루프 안 돎 | `b.lo` |
| n==0 검사 없이 bottom-tested 루프 | 첫 `ldr` 이 빈 배열(NULL)을 읽음 | 입구에 `cbz x1, done` |
| 테스트에서 `-INT64_MIN`, `llabs(INT64_MIN)` 로 기대값 계산 | C 에선 UB — 컴파일러가 "음수 아님" 으로 접어 버림 (demo_ub: `mov w0, #0`) | 기대값을 상수로 손으로 |
| `tbz x0, #64, …` 또는 변수 비트 번호 | TBZ 의 비트 번호는 즉시값 0..63 (w 형은 0..31) | 변수 위치면 `lsr` + `tbz #0`, 또는 `tst` + 마스크 |

## 8. 면접 Q&A

<details>
<summary>Q1. `cmp x0, x1` 다음에 signed 비교와 unsigned 비교를 어떻게 구분하나요? CPU 는 값이 signed 인지 아나요?</summary>

모른다. `cmp` 는 `subs xzr, x0, x1` 로 NZCV 만 남기고, 해석은 다음 명령의 조건 코드가 정한다. signed: GE(N==V) / LT(N≠V) / GT(Z=0∧N==V) / LE. unsigned: HS(C=1) / LO(C=0) / HI(C=1∧Z=0) / LS. 외우는 법: **G/L = Greater/Less = signed, H/L = Higher/Lower = unsigned**. C 컴파일러는 변수의 **타입** 을 보고 고른다 — 같은 `a > b ? a : b` 가 int64 면 `gt`, uint64 면 `hi` (demo_clang 실측). 꼬리질문 "그럼 -1 과 1 을 비교하면?" → NZCV = N1 Z0 C1 V0, gt 거짓 / hi 참.

</details>

<details>
<summary>Q2. signed less-than 이 왜 N 이 아니라 N≠V 인가요?</summary>

`a - b` 가 넘치지 않으면 결과의 부호(N)가 곧 진짜 차이의 부호다. 넘치면(V=1) 결과의 부호가 뒤집혀 있다. 그래서 진짜 부호 = N⊕V. 예: `cmp INT64_MIN, 1` → 진짜 차이는 음수인데 결과는 0x7FFF…FFFF(양수), N=0 V=1 → LT 참, MI 거짓 (실측). 플래그 계산식 자체는 ARCH-B02.

</details>

<details>
<summary>Q3. 분기 대신 CSEL 을 쓰면 항상 빠른가요?</summary>

아니다. CSEL 은 예측 실패가 없는 대신 ① 양쪽 값을 **모두** 계산해야 하고 ② 결과가 두 입력과 플래그 모두에 데이터 의존해서, 예측이 잘 맞는 분기(투기적 실행으로 한쪽만 기다림)보다 의존 사슬이 길어질 수 있다. 실측(이 맥): 예측 가능한 패턴에서는 둘 다 ~0.7–1.0 ns/호출, 무작위 50/50 에서는 분기판 ~3.7–3.8 ns vs csel ~0.7–0.8 ns. 규칙: 데이터에 따라 무작위로 갈리는 선택 → csel, 거의 한쪽으로만 가는 검사(에러 경로) → 분기. 덤으로 branchless 는 실행 시간이 데이터에 의존하지 않아 **상수 시간(constant-time)** 암호 코드에 쓰인다 (그래도 마이크로아키텍처 보장은 별도 확인 — Arm 의 DIT 비트 등).

</details>

<details>
<summary>Q4. abs(INT_MIN) 은 뭐가 나오나요? C 와 어셈블리가 다른가요?</summary>

ISA: `neg` 는 0 - x mod 2^64 → INT64_MIN 그대로, 예외 없음. C: `-x` 는 signed 넘침 = UB (`llabs(LLONG_MIN)` 도 UB). 차이가 실제로 보인다: clang -O2 는 `c_abs(x) < 0` 을 상수 0 으로 접었지만 실제 값은 음수였다 (demo_ub 실측). 안전하게 쓰려면 ① 결과를 unsigned 로: `uint64_t uabs = x < 0 ? 0 - (uint64_t)x : (uint64_t)x` (INT64_MIN → 2^63, 정확) ② 포화: INT64_MIN → INT64_MAX (추가 연습 5) ③ 입력 범위를 계약으로 막기. 펌웨어에선 오디오/센서 값이 바로 이 경계를 친다 (Q15 의 -1.0 = 0x8000).

</details>

<details>
<summary>Q5. AArch32 에는 거의 모든 명령에 조건이 붙는데(`addne`, `ldreq`), AArch64 는 왜 없앴나요?</summary>

A32 는 명령마다 4비트 cond 필드가 있었고, Thumb-2 는 IT 블록으로 흉내 냈다 (ARM-B13). A64 는 조건 실행을 `B.cond`, `CSEL` 가족, `CCMP/CCMN`, `CSET` 정도로 줄였다. 흔히 드는 이유 (Arm 의 설계 설명·업계 분석 기준, 확인 권장): ① 32비트 명령에 레지스터 5비트 × 3 개를 넣으려면 4비트 cond 가 비싸다 ② 조건부로 "쓸 수도 안 쓸 수도 있는" 명령은 out-of-order 코어에서 옛 목적지 값을 추가 입력으로 읽어야 해서 renaming 이 복잡해진다 ③ 분기 예측이 좋아져서 짧은 분기를 없애는 이득이 줄었다. 대신 남긴 CSEL 은 "항상 쓴다" 는 점에서 OoO 친화적이다.

</details>

<details>
<summary>Q6. 32비트 tick 카운터로 타임아웃을 어떻게 비교하나요? `now >= deadline` 은 왜 안 되나요?</summary>

tick 이 wrap 하면 deadline(wrap 직전 값)이 now(wrap 직후 작은 값)보다 커 보여서 타임아웃을 약 2^32 tick 동안 놓친다. 올바른 비교는 **차이의 부호**: `(int32_t)(now - deadline) >= 0` — Linux `time_after_eq` 가 이 식이다 (FreeRTOS 의 `xTaskCheckForTimeOut` 은 overflow 횟수를 따로 세는 다른 방식으로 같은 문제를 푼다 — 소스 확인 권장). asm 은 `cmp w0, w1` / `cset w0, pl`. 꼬리질문 "`ge` 는?" → GE 는 "now ≥ deadline as int32" 라 0x7FFFFFFF→0x80000000 을 지날 때 틀린다 (실측 `(0x7FFFFFF0, 0x80000010)` 에서 ge=1, pl=0). 전제: 두 시각의 차이가 2^31 tick 미만이어야 한다.

</details>

<details>
<summary>Q7. CBZ/TBZ 는 CMP + B.EQ 와 뭐가 다른가요?</summary>

① 플래그를 읽지도 쓰지도 않는다 → 앞에서 만든 플래그를 보존해야 하는 코드(예: `adds` 결과를 나중에 `csel …, vs` 로 쓸 때)에서도 0 검사·비트 검사를 끼울 수 있다 ② 명령 하나. 대신 범위가 좁다: B.cond / CBZ 는 imm19×4 = ±1 MiB, TBZ 는 imm14×4 = **±32 KiB**. 링커가 너무 먼 TBZ 를 만나면 veneer/역조건+B 로 바꿔야 한다. 또 TBZ 의 비트 번호는 즉시값이라 변수 위치는 못 쓴다.

</details>

<details>
<summary>Q8. `cmp x0, #-5` 는 어떻게 어셈블되나요? 큰 상수와 비교하려면?</summary>

CMP/CMN 의 즉시값은 imm12(0..4095), 선택적으로 `lsl #12`. 음수는 못 담으므로 어셈블러가 `cmn x0, #5` (`adds xzr, x0, #5`) 로 바꾼다 — 실측 `b100141f cmn x0, #0x5`. 범위 밖 상수(예: 0x12345)는 `mov`/`movk` 로 레지스터에 만든 뒤 `cmp x0, x9` (MOVZ/MOVK 는 ARM-B03). 꼬리질문 "cmn 으로 바꿔도 플래그가 똑같나? unsigned 조건도?" → 똑같다. Arm ARM 의 의사코드로 SUBS 는 `AddWithCarry(x, NOT(imm), 1)` 이고 NOT(-5) = 4 이므로 `x + 4 + 1`, CMN 은 `AddWithCarry(x, 5, 0)` = `x + 5 + 0`. unsigned 합과 signed 합이 정수로 같으니 결과·N·Z·C·V 가 모두 같다. 임시 테스트로 `cmp x0, x1(=-5)` 와 `cmn x0, #5` 의 NZCV 를 100만 개 입력(경계값 포함)에서 비교해 차이 0 을 확인했다. 단 0 은 예외라서 (`cmp x, #0` 은 C=1, `cmn x, #0` 은 C=0) 어셈블러가 바꾸지 않는다.

</details>

## 9. 추가 연습

### 연습 1 — 세 갈래 비교 `cmp3_s64(a, b)` → -1 / 0 / +1

qsort 비교 함수 모양. `return a - b;` 로 쓰면 왜 틀리는가? branchless 로.

<details>
<summary>답 보기</summary>

`a - b` 는 넘친다 (`INT64_MIN - 1`). 플래그로 하면 넘침 걱정이 없다: `cmp x0, x1` / `cset x8, ne` (다르면 1) / `cneg x0, x8, lt` (작으면 -1). 테스트에 `cmp3_s64(INT64_MIN, INT64_MAX) == -1` 이 있다.

</details>

### 연습 2 — 비교 한 번으로 unsigned 범위 검사 `in_range_u(x, lo, hi)` (lo ≤ hi)

<details>
<summary>답 보기</summary>

`(x - lo) <= (hi - lo)` unsigned. x < lo 면 `x - lo` 가 wrap 해서 아주 커지므로 한 번에 걸러진다. `sub x8, x0, x1` / `sub x9, x2, x1` / `cmp x8, x9` / `cset w0, ls`. 주소 범위(MMIO 창, LBA 범위) 검사에 쓰는 고전 트릭. `ls` 를 `le` 로 바꾸면 Part B 와 같은 종류의 버그가 된다.

</details>

### 연습 3 — signed 범위 검사를 CCMP 로 `in_range_s(x, lo, hi)`

`lo <= x && x <= hi` 의 `&&` 를 분기 없이.

<details>
<summary>답 보기</summary>

`cmp x0, x1` / `ccmp x0, x2, #0, ge` / `cset w0, le`. CCMP 는 "조건(ge)이 참이면 `cmp x0, x2` 로 플래그를 세팅, 거짓이면 즉시값 nzcv(#0 = N0 Z0 C0 V0)로 세팅". N0 Z0 V0 이면 LE(Z=1 ∨ N≠V)는 거짓 → 0. 즉시값을 잘못 고르면(예 #4 = Z=1) 첫 비교가 실패해도 1 이 나온다. lo > hi 면 항상 0.

</details>

### 연습 4 — 분기 없는 루프 몸통 `count_neg(a, n)`

음수 개수를 세는데, 루프 안에 데이터 의존 분기가 없게 (루프 자체의 `b.ne` 는 예측이 쉬움).

<details>
<summary>답 보기</summary>

```text
    mov  x8, #0
    cbz  x1, 2f
1:  ldr  x9, [x0], #8
    cmp  x9, #0
    cinc x8, x8, lt      // 음수면 +1
    subs x1, x1, #1      // n--, Z = (n == 0)
    b.ne 1b
2:  mov  x0, x8
```

`subs` 가 카운터 감소와 종료 검사를 겸한다. `cinc` 는 `csinc x8, x8, x8, ge`.

</details>

### 연습 5 — 포화 abs `abs_sat(x)`: INT64_MIN → INT64_MAX

<details>
<summary>답 보기</summary>

`cmp x0, #0` / `cneg x0, x0, lt` (INT64_MIN 은 그대로 남음) / `cmp x0, #0` / `cinv x0, x0, lt` — 아직 음수인 값은 INT64_MIN 뿐이고 `~INT64_MIN = INT64_MAX`. 테스트의 기대값은 손으로 (`abs_sat(INT64_MIN) == INT64_MAX`).

</details>

### 한 번에 검증

<!-- code: practice.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-B05_practice   # practice.S 단독 확인
```

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-B05_practice && /tmp/ARM-B05_practice   # 13/13 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c practice.S shared_bugs.S -o /tmp/ARM-B05_pasan && /tmp/ARM-B05_pasan   # 13/13 passed
```

## 10. 핵심 정리

- `cmp a, b` = `subs zr, a, b` → NZCV 만 남긴다. signed/unsigned 해석은 **조건 코드** 가 정한다: G/L(GT GE LT LE) = signed, H/L(HI HS LO LS) = unsigned.
- 유도식: HS = C, HI = C∧¬Z, GE = (N==V), GT = ¬Z∧(N==V). ARM 의 C 는 **빌림 없음** (x86 과 반대). MI/PL 은 넘침을 무시한 부호.
- 짝 조건은 cond bit0 만 다르다 (EQ/NE, HS/LO, …). B.cond 하위 4비트 = cond. 별칭 `cset/cinc/cinv/cneg/csetm` 은 반대 조건으로 인코딩된다.
- branchless: `csel`(Rm) / `csinc`(Rm+1) / `csinv`(~Rm) / `csneg`(-Rm). sign = `asr #63` + `csinc …, xzr, le`. abs = `csneg x, x, x, ge`.
- `abs(INT64_MIN)` 은 ISA 에선 INT64_MIN(wrap), C 에선 UB — 테스트 기대값은 손으로. clang 은 UB 를 근거로 `abs(x) < 0` 을 0 으로 접는다.
- GT↔HI 버그는 부호 경계 값에서만 드러난다. 작은 양수 무작위 테스트는 **수학적으로** 못 잡는다 → 0, ±1, MIN, MAX, UMAX 를 항상 넣는다.
- tick 비교는 차이의 부호: `cmp w0, w1` / `cset w0, pl` (= `time_after_eq`). hs·ge 는 각각 다른 경계에서 틀린다.
- CBZ/TBZ 는 플래그를 안 건드린다 (±1 MiB / ±32 KiB). 예측 불가 선택엔 csel, 거의 한쪽인 검사엔 분기 (실측 random: 3.8 vs 0.7 ns/호출).

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — C1.2.4 Condition code, `ConditionHolds()` 의사코드, CSEL/CSINC/CSINV/CSNEG/CCMP: <https://developer.arm.com/documentation/ddi0487/latest>
- Arm A64 Instruction Set (DDI 0602) — CSEL: <https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions/CSEL--Conditional-select->
- Arm Community, "Condition Codes 1: Condition Flags and Codes": <https://community.arm.com/arm-community-blogs/b/architectures-and-processors-blog/posts/condition-codes-1-condition-flags-and-codes>
- Apple, "Writing ARM64 code for Apple platforms" (인자 확장 규칙): <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
- Linux `include/linux/jiffies.h` — `time_after`, `time_after_eq`: <https://github.com/torvalds/linux/blob/master/include/linux/jiffies.h>
- Dougall Johnson, Apple Firestorm 마이크로아키텍처 메모 (fusion 등, 비공식): <https://dougallj.github.io/applecpu/firestorm.html>
- C11 (N1570) 6.5p5 (예외 조건 = UB), 6.3.1.3p3 (범위 밖 signed 변환 = 구현 정의)
- 관련 레슨: ARCH-B02 (NZCV 계산), ARM-B01 (AAPCS64 인자·w 레지스터), ARM-B03 (MOVZ/MOVK 상수), ARM-B13 (Thumb-2 IT 블록), ARM-M02 (inline asm 의 `"cc"` clobber)
