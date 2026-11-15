---
id: ARM-B04
time: 60분
updated: 2026-11-11
---
# ARM-B04 로드/스토어와 주소 지정 모드

## 학습 목표

- AArch64 의 다섯 가지 주소 지정 모드 `[xn, #imm]`, `[xn, #imm]!`(pre-index), `[xn], #imm`(post-index), `[xn, xm, lsl #s]`, `[xn, wm, sxtw/uxtw #s]` 를 보고 **접근 주소**와 **명령 뒤 base 레지스터 값**을 바로 말할 수 있다.
- `ldrb/ldrh/ldr w/ldrsb/ldrsh/ldrsw` 중 C 타입에 맞는 로드를 고르고, 틀린 선택(`ldr w` 로 `int32_t` 합)이 어떤 값을 만드는지 설명한다.
- `LDP/STP` 로 두 개씩 복사하고 홀수 꼬리를 처리한다. 프롤로그 `stp x29, x30, [sp, #-16]!` 가 왜 pre-index 인지 안다.
- 오프셋 범위(imm12 × 크기, imm9 비스케일 → `ldur`, imm7 × 8 for LDP)를 진짜 어셈블러 에러로 확인한다.
- C 구조체 배열을 `offsetof` + stride 로 asm 에서 읽고, `_Static_assert` 로 레이아웃 불일치를 컴파일 타임에 막는다.

## 1. 문제

크래시 덤프에서 `ldr x8, [x19, #0x28]` 이나 `ldp x29, x30, [sp], #16` 을 보고 "어느 구조체의 어느 필드를 읽다 죽었나 / 스택에서 무엇을 꺼냈나" 를 바로 말하는 것 — 이게 펌웨어 면접의 "이 asm 뭐 하는 코드야?" 라운드의 절반이다. SSD 컨트롤러 펌웨어(Cortex-R/A 코어)의 FTL 테이블 조회, Cortex-M 의 링 버퍼, Apple/NVIDIA AArch64 SoC 드라이버의 디스크립터 채우기 모두 결국 **load/store + 주소 계산**이다.

AArch64 는 load/store 구조(load/store architecture)다. 산술 명령은 레지스터끼리만 계산하고, 메모리는 오직 `LDR/STR` 계열로만 만진다. 대신 주소를 만드는 방법(주소 지정 모드, addressing mode)이 여러 개라서, `a[i]`, `p++`, `r->field` 같은 C 표현을 명령 하나에 담는다. 아래 그림은 `a = {10, 20, 30, 40}` 에서 다섯 모드를 실제로 실행한 결과(`demo_modes.c`, 실제 출력은 5. 풀이)를 그대로 그린 것이다.

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">다섯 모드: 어디를 읽고, 명령 뒤 x0 는 어디? (a = {10, 20, 30, 40})</text>
  <g stroke="currentColor" fill="none">
    <rect x="330" y="40" width="70" height="26"/><rect x="400" y="40" width="70" height="26"/>
    <rect x="470" y="40" width="70" height="26"/><rect x="540" y="40" width="70" height="26"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">
    <text x="365" y="58">10</text><text x="435" y="58">20</text><text x="505" y="58">30</text><text x="575" y="58">40</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" opacity=".75">
    <text x="330" y="82">a+0</text><text x="400" y="82">a+8</text><text x="470" y="82">a+16</text><text x="540" y="82">a+24</text>
  </g>
  <g font-family="monospace" fill="currentColor">
    <text x="10" y="113">ldr x2, [x0, #8]</text>
    <text x="10" y="161">ldr x2, [x0, #8]!</text>
    <text x="10" y="209">ldr x2, [x0], #8</text>
    <text x="10" y="257">ldr x3, [x0, x2, lsl #3]</text>
    <text x="10" y="305">ldr x3, [x0, w2, sxtw #3]</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="330" y="95" width="280" height="26"/><rect x="330" y="143" width="280" height="26"/>
    <rect x="330" y="191" width="280" height="26"/><rect x="330" y="239" width="280" height="26"/>
    <rect x="330" y="287" width="280" height="26"/>
  </g>
  <rect x="400" y="95" width="70" height="26" class="f-acc"/>
  <rect x="400" y="143" width="70" height="26" class="f-acc"/>
  <rect x="330" y="191" width="70" height="26" class="f-acc"/>
  <rect x="470" y="239" width="70" height="26" class="f-acc"/>
  <rect x="400" y="287" width="70" height="26" class="f-acc"/>
  <g stroke="currentColor" stroke-width="3">
    <line x1="330" y1="89" x2="330" y2="127"/><line x1="330" y1="137" x2="330" y2="175"/>
    <line x1="330" y1="185" x2="330" y2="223"/><line x1="330" y1="233" x2="330" y2="271"/>
    <line x1="470" y1="281" x2="470" y2="319"/>
  </g>
  <g stroke="currentColor" stroke-width="2" stroke-dasharray="4 3">
    <line x1="400" y1="137" x2="400" y2="175"/><line x1="400" y1="185" x2="400" y2="223"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="620" y="113">= 20, x0 +0</text>
    <text x="620" y="161">= 20, x0 +8</text>
    <text x="620" y="209">= 10, x0 +8</text>
    <text x="620" y="257">x2=2: 30, +0</text>
    <text x="620" y="305">w2=-1: 20, +0</text>
  </g>
  <text x="10" y="342" fill="currentColor">굵은 선 = 명령 전 x0 (마지막 줄만 x0 = a+16), 점선 = 명령 뒤 x0.</text>
  <text x="10" y="366" fill="currentColor">선이 하나뿐이면 x0 불변. 채운 칸 = 실제로 읽은 8 바이트.</text>
</svg>
```

| 모드 | 문법 | 접근 주소 | 명령 뒤 base | C 로 치면 |
|---|---|---|---|---|
| 오프셋 (offset) | `[xn, #imm]` | xn + imm | 그대로 | `p[k]` (k 상수), `r->field` |
| pre-index | `[xn, #imm]!` | xn + imm | xn + imm | `*(p += k)`, 스택 push |
| post-index | `[xn], #imm` | xn | xn + imm | `*p; p += k` → `*p++` |
| 레지스터 오프셋 | `[xn, xm{, lsl #s}]` | xn + (xm << s) | 그대로 | `a[i]` |
| 확장 레지스터 | `[xn, wm, sxtw/uxtw {#s}]` | xn + (ext(wm) << s) | 그대로 | `a[(int)i]`, `a[(unsigned)i]` |

`s` 는 0 또는 log2(접근 크기)만 된다 (8바이트 로드면 `lsl #3`, 4바이트면 `#2`). `!` 와 `], #` 형식이 base 를 바꾸는 **writeback** 이다.

### Part A — 모드별 배열 합 · gather · 복사

- `sum_imm` / `sum_post` / `sum_pre` / `sum_reg` — 같은 `uint64_t` 배열 합을 모드 하나씩만 써서 4번 구현.
- `sum_gather_s32(base, idx, n)` — `Σ base[idx[i]]`, `idx` 는 **음수 가능** (base 가 배열 가운데를 가리킴). `[xn, wm, sxtw #2]` 사용.
- `copy_ldr_str` (1개씩, post-index) 와 `copy_ldp_stp` (LDP/STP 로 2개씩 + 홀수 꼬리 1개).

### Part B — 폭 · 부호 확장 · 심어 둔 버그

- `sum_u8` (LDRB), `sum_s16` (LDRSH), `sum_s32` (LDRSW) — 결과는 64비트.
- `shared_bugs.S` 에 일부러 틀린 `sum_s32_bug` (LDRSW 대신 `ldr w`) 와 `copy_ldp_stp_notail` (꼬리 누락)이 있다. 하네스는 "양수만/짝수 길이만 테스트하면 놓치고, 음수 하나/홀수 하나면 잡힌다" 를 CHECK 로 증명한다.
- 범위 밖 오프셋·잘못된 shift·writeback 충돌을 진짜 어셈블러에 넣어 본다 (`demo_range.sh`).

### Part C — 센서 로그 레코드 (구조체 배열)

펌웨어가 SRAM 의 로그 버퍼에 `rec_t { uint32_t id; int32_t temp; uint64_t ts; }` (16바이트) 를 쌓는다.

- `rec_sum_temp(r, n)` — `ldrsw x, [x0, #4]` (필드 오프셋) + `add x0, x0, #16` (stride).
- `rec_sum_ts_for_id(r, n, id)` — 레코드 하나를 `ldp x4, x5, [x0], #16` **한 번**에 읽고, `id` 는 x4 의 하위 32비트로 비교.
- `rec_log_push(slot, id, temp, ts)` — `stp w1, w2` + `str x3` 로 쓰고 **다음 슬롯 주소**를 반환 (writeback 된 x0 이 곧 반환값).
- 오프셋은 `.S` 의 `#define REC_TEMP 4` 와 C 의 `_Static_assert(offsetof(rec_t, temp) == 4)` 로 양쪽에 못 박는다.

## 2. Problem (English)

> **AArch64 loads, stores and addressing modes.** Write leaf functions in an AArch64 `.S` file (no stack use; AAPCS64: args in x0–x3, result in x0), callable from C:
>
> 1. `uint64_t sum_imm(const uint64_t *a, size_t n)`, `sum_post`, `sum_pre`, `sum_reg` — the same wrap-around sum, each using only one addressing mode: immediate offset, post-index, pre-index, register offset (`lsl #3`). `n == 0` must not touch memory (`a` may be `NULL`).
> 2. `int64_t sum_gather_s32(const int32_t *base, const int32_t *idx, size_t n)` returning `Σ base[idx[i]]` where `idx[i]` may be negative.
> 3. `void copy_ldr_str(uint64_t *dst, const uint64_t *src, size_t n)` and `copy_ldp_stp` (pairs + odd tail). Must not write outside `dst[0..n)`.
> 4. `uint64_t sum_u8(const uint8_t *, size_t)`, `int64_t sum_s16(const int16_t *, size_t)`, `int64_t sum_s32(const int32_t *, size_t)`.
> 5. Given `typedef struct { uint32_t id; int32_t temp; uint64_t ts; } rec_t;` implement `int64_t rec_sum_temp(const rec_t *, size_t)`, `uint64_t rec_sum_ts_for_id(const rec_t *, size_t, uint32_t id)` (one LDP per record), and `rec_t *rec_log_push(rec_t *slot, uint32_t id, int32_t temp, uint64_t ts)` returning `slot + 1`.
>
> Example: `sum_post({1,2,3,4,5}, 5) == 15`; `sum_imm({UINT64_MAX, 2}, 2) == 1`; with `base = pool + 32`, `sum_gather_s32(base, {-32, 31, -1, 0}, 4) == pool[0] + pool[63] + pool[31] + pool[32]`; `rec_log_push(ring, 7, -40, 123456789)` returns `ring + 1`.
>
> Follow-ups: What is the largest immediate offset for `ldr x0, [x1, #imm]`? What does the assembler do with `ldr x0, [x1, #-8]`? Why is the prologue `stp x29, x30, [sp, #-16]!` pre-index and the epilogue post-index? `ldr w` vs `ldrsw`? `sxtw` vs `uxtw`?

## 3. 시작 코드

파일 구성 (asm 레슨 공통 규칙): `harness.c` = 테스트 + `extern` 선언 (starter/solution 공용), `starter.S` = TODO, `solution.S` = 정답, `shared_bugs.S` = 심어 둔 버그 2개 (모든 빌드에 링크).

<!-- code: harness.c -->

<!-- code: starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 회피
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S shared_bugs.S -o /tmp/ARM-B04_starter && /tmp/ARM-B04_starter
```

starter.S 는 **제어 흐름(루프·카운터·꼬리 분기)은 다 주고, 메모리 명령 자리만** `mov xN, #0` / `nop` 로 비워 두었다. 그래서 무한 루프도, 엉뚱한 메모리 접근도 없이 0 을 돌려준다. 이 레슨의 주제(어떤 모드의 어떤 명령 한 줄인가)에만 집중하라는 뜻이다. 실제 출력 (앞부분):

```text
  (sum_imm: named cases)
FAIL harness.c:130  ok
FAIL harness.c:131  sum_ok_all_n(sums[k])
  ... (sum_post / sum_pre / sum_reg 도 같은 2줄씩)
FAIL harness.c:140  sum_gather_s32(base, named_idx, 4) == (int64_t)INT32_MIN + INT32_MAX - 1 + pool[32]
FAIL harness.c:151  copy_ok_all_n(copy_ldr_str)
FAIL harness.c:152  copy_ok_all_n(copy_ldp_stp)
FAIL harness.c:158  sum_u8(all, 256) == 32640
  ... (sum_s16, sum_s32, rec_* 의 FAIL 들)
FAIL harness.c:231  p == ring + 2 && ring[1].id == 0xFFFFFFFFu && ring[1].temp == INT32_MAX && ring[1].ts == UINT64_MAX
9/36 passed
```

처음부터 PASS 인 9개는 gather 의 `n = 0`, 심어 둔 버그 검출 6개 (shared_bugs.S 는 이미 완성), "없는 id 의 ts 합 = 0", 옆 슬롯 보존 검사다 — 스텁이 0 을 돌려줘도 우연히 맞는 것들.

**Linux/ELF 에서도 같은 파일:** 맥(Mach-O)은 C 이름 앞에 `_` 를 붙이고 `.type/.size` 가 없다. 파일 머리의 매크로가 이 차이를 흡수한다.

```c
#ifdef __APPLE__
#define SYM(n) _##n          /* C 의 sum_post → 심볼 _sum_post */
#define TYPE(n)
#define SIZE(n)
#else
#define SYM(n) n             /* ELF: 접두사 없음 */
#define TYPE(n) .type n, %function
#define SIZE(n) .size n, .-n
#endif
```

`.S` (대문자) 는 C 전처리기를 거치므로 `#define` 이 된다. 주석은 `//` 와 `/* */` 만 쓴다 (`;` 는 Apple 어셈블러에선 주석, GNU as AArch64 에선 문장 구분자라 이식성이 깨진다). Lima VM (aarch64 Ubuntu) 에서 gcc 15.2.0 과 clang 21.1.8 로 같은 파일을 빌드해 starter `9/36`, solution `36/36`, practice `14/14` 를 확인했다. `readelf -s` 로 본 ELF 심볼: `sum_post` FUNC 32바이트 (명령 8개), `rec_log_push` FUNC 12바이트 (명령 3개).

## 4. 힌트

<details>
<summary>힌트 1 — "언제 더하나" 만 구분하면 된다</summary>

오프셋: 더한 주소로 접근, base 그대로. pre (`!`): **먼저** 더하고 접근, base 갱신. post (`], #`): 옛 주소로 접근, **나중에** 더해서 갱신. 스트리밍 루프 (`*p++`) 는 post-index 가 가장 자연스럽다.

</details>

<details>
<summary>힌트 2 — sum_pre 는 시작점을 하나 앞으로</summary>

`ldr x3, [x0, #8]!` 는 첫 반복에서 이미 `a + 8` 을 읽는다. 그래서 루프 전에 `sub x0, x0, #8` 로 `a - 1` 을 만들어 둔다. C 에서 `a - 1` 포인터를 만드는 것만으로 UB 지만 (C11 6.5.6p8), ISA 에선 그냥 정수 뺄셈이다. 단 `n == 0` 이면 아예 들어가지 않는다.

</details>

<details>
<summary>힌트 3 — 레지스터 오프셋의 shift 는 원소 크기</summary>

`a[i]` 의 바이트 주소는 `a + i * 8` → `[x0, x3, lsl #3]`. `int32_t` 면 `lsl #2`. 다른 shift 는 어셈블러가 거부한다 (`demo_range.sh`). 인덱스가 32비트 `int` 면 `[x0, w5, sxtw #2]` — 부호 확장 후 shift.

</details>

<details>
<summary>힌트 4 — 폭과 부호는 명령 이름에</summary>

`ldrb`/`ldrh`/`ldr w` = zero-extend, `ldrsb`/`ldrsh`/`ldrsw` = sign-extend. 목적 레지스터를 `x` 로 쓰면 64비트까지 확장된 값이 들어온다. `int16_t` 합이면 `ldrsh x3, [x0], #2`.

</details>

<details>
<summary>힌트 5 — LDP 복사의 꼬리</summary>

`pairs = n >> 1` 번 `ldp/stp ..., #16` 을 돌린 뒤, `n` 의 bit0 이 1 이면 1개 더. `tbz x2, #0, 끝` 이 "bit0 이 0 이면 점프" 다 (조건 분기 자세히는 ARM-B05).

</details>

<details>
<summary>힌트 6 — rec_t 한 레코드를 LDP 한 번에</summary>

16바이트 = 8 + 8. `ldp x4, x5, [x0], #16` 이면 x4 = 바이트 0..7 (little-endian 이라 하위 32비트 = id, 상위 32비트 = temp), x5 = ts. id 비교는 `cmp w4, w2` — w 레지스터 비교는 하위 32비트만 본다.

</details>

## 5. 풀이

### Part A — 네 가지 합: 무엇이 바뀌고 무엇이 같은가

네 함수 모두 레지스터 할당은 같다: x0 = 포인터, x1 = 남은 개수, x2 = 합, x3/x4 = 읽은 값. 다른 건 **포인터를 누가 전진시키나** 뿐이다.

| 함수 | 루프 본문의 메모리 명령 | 포인터 전진 | 루프당 명령 수 (원소당) |
|---|---|---|---|
| `sum_imm` | `ldr x3, [x0]` · `ldr x4, [x0, #8]` | 별도 `add x0, x0, #16` | 9 / 2원소 = 4.5 |
| `sum_post` | `ldr x3, [x0], #8` | 명령 안에서 | 4 |
| `sum_pre` | `ldr x3, [x0, #8]!` | 명령 안에서 | 4 (+ 루프 전 `sub` 1) |
| `sum_reg` | `ldr x4, [x0, x3, lsl #3]` | 인덱스 `add x3, x3, #1` | 6 (cmp/b.hs/ldr/add/add/b) |

clang 도 같은 C 루프를 post-index 로 내린다. `demo_clang.c` 를 `clang -S -O2 -fno-vectorize -fno-unroll-loops -fno-asynchronous-unwind-tables` 로 본 실제 출력 (Apple clang 21.0.0, `.loh` 등 정리):

```text
_c_sum:
	mov	x8, #0
	cbz	x1, LBB0_2
LBB0_1:
	ldr	x9, [x0], #8          ; ← sum_post 와 같은 모양
	add	x8, x9, x8
	subs	x1, x1, #1
	b.ne	LBB0_1
LBB0_2:
	mov	x0, x8
	ret
```

(옵션 없이 `-O2` 면 같은 루프가 `ldp q4, q5, [x8, #-32]` / `ldp q6, q7, [x8], #64` 같은 NEON 벡터화 코드가 된다 — 그 판독은 ARM-A11, NEON 자체는 ARM-M06.)

**sum_pre 한 줄씩 추적** (`a = A`, 내용 `{10, 20, 30}`, n = 3):

| # | 명령 | x0 | x1 | x2 | x3 | 메모 |
|---|---|---|---|---|---|---|
| 1 | `mov x2, #0` | A | 3 | 0 | – | |
| 2 | `cbz x1, 2f` | A | 3 | 0 | – | n ≠ 0, 안 뜀 |
| 3 | `sub x0, x0, #8` | A−8 | 3 | 0 | – | C 라면 UB 포인터 |
| 4 | `ldr x3, [x0, #8]!` | **A** | 3 | 0 | 10 | 먼저 +8, 그 주소 로드 |
| 5 | `add x2, x2, x3` | A | 3 | 10 | 10 | |
| 6 | `subs x1, x1, #1` | A | 2 | 10 | 10 | Z=0 |
| 7 | `b.ne 1b` | | | | | 돌아감 |
| 4' | `ldr x3, [x0, #8]!` | A+8 | 2 | 10 | 20 | |
| 5'·6' | | A+8 | 1 | 30 | 20 | |
| 4'' | `ldr x3, [x0, #8]!` | A+16 | 1 | 30 | 30 | |
| 5''·6'' | | A+16 | 0 | 60 | 30 | Z=1 → 빠져나옴 |
| 8 | `mov x0, x2` | 60 | | | | 반환 60 |

**인코딩 — 모드는 비트 몇 개 차이다.** solution.S 를 `objdump -d` 한 실제 워드:

```text
  10: f9400404   ldr  x4, [x0, #0x8]          ; 부호 없는 오프셋
  44: f8408403   ldr  x3, [x0], #0x8          ; post-index
  68: f8408c03   ldr  x3, [x0, #0x8]!         ; pre-index
  90: f8637804   ldr  x4, [x0, x3, lsl #3]    ; 레지스터 오프셋
  bc: b8a5d806   ldrsw x6, [x0, w5, sxtw #2]  ; 확장 레지스터
  f4: a8c11424   ldp  x4, x5, [x1], #0x10     ; LDP post-index
```

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 LDR 이라도 형식이 다르다 (Arm ARM: LDR immediate, 확인 권장)</text>
  <text x="40" y="44" fill="currentColor" font-family="monospace">ldr x4, [x0, #8]   = 0xF9400404   부호 없는 오프셋: imm12 = 1 → 1 × 8 = 8 바이트</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="60" y="62">size</text><text x="110" y="62">111</text><text x="150" y="62">V</text><text x="180" y="62">01</text>
    <text x="220" y="62">opc</text><text x="360" y="62">imm12 (× 8 로 스케일)</text><text x="530" y="62">Rn</text><text x="630" y="62">Rt</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="40" y="68" width="40" height="30"/><rect x="80" y="68" width="60" height="30"/><rect x="140" y="68" width="20" height="30"/>
    <rect x="160" y="68" width="40" height="30"/><rect x="200" y="68" width="40" height="30"/><rect x="480" y="68" width="100" height="30"/><rect x="580" y="68" width="100" height="30"/>
  </g>
  <rect x="240" y="68" width="240" height="30" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="13" text-anchor="middle">
    <text x="60" y="88">11</text><text x="110" y="88">111</text><text x="150" y="88">0</text><text x="180" y="88">01</text>
    <text x="220" y="88">01</text><text x="360" y="88">0000 0000 0001</text><text x="530" y="88">00000</text><text x="630" y="88">00100</text>
  </g>
  <text x="40" y="134" fill="currentColor" font-family="monospace">ldr x3, [x0], #8   = 0xF8408403   post-index: imm9 = 8 바이트 (스케일 없음, 부호 있음)</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="60" y="152">size</text><text x="110" y="152">111</text><text x="150" y="152">V</text><text x="180" y="152">00</text>
    <text x="220" y="152">opc</text><text x="250" y="152">0</text><text x="350" y="152">imm9</text><text x="460" y="152">모드</text>
    <text x="530" y="152">Rn</text><text x="630" y="152">Rt</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="40" y="158" width="40" height="30"/><rect x="80" y="158" width="60" height="30"/><rect x="140" y="158" width="20" height="30"/>
    <rect x="160" y="158" width="40" height="30"/><rect x="200" y="158" width="40" height="30"/><rect x="240" y="158" width="20" height="30"/>
    <rect x="480" y="158" width="100" height="30"/><rect x="580" y="158" width="100" height="30"/>
  </g>
  <rect x="260" y="158" width="180" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="440" y="158" width="40" height="30" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="13" text-anchor="middle">
    <text x="60" y="178">11</text><text x="110" y="178">111</text><text x="150" y="178">0</text><text x="180" y="178">00</text>
    <text x="220" y="178">01</text><text x="250" y="178">0</text><text x="350" y="178">0 0000 1000</text><text x="460" y="178">01</text>
    <text x="530" y="178">00000</text><text x="630" y="178">00011</text>
  </g>
  <text x="40" y="214" fill="currentColor">모드 비트 [11:10]: 01 = post-index, 11 = pre-index (0xF8408C03), 00 = LDUR (비스케일 오프셋, base 불변)</text>
  <text x="40" y="238" fill="currentColor">imm12 는 0..4095 × 크기 (양수만), imm9 는 −256..255 바이트. 그래서 범위가 다르다 (Part B).</text>
</svg>
```

**gather 와 sxtw.** `sum_gather_s32` 의 핵심 두 줄:

```text
ldr     w5, [x1, x4, lsl #2]    // k = idx[i]   → w5 에 32비트, x5 의 상위 32비트는 0
ldrsw   x6, [x0, w5, sxtw #2]   // base[k]      → 주소 = x0 + (sext(w5) << 2)
```

k = −1 이면 w5 = 0xFFFFFFFF. `sxtw` 는 이걸 −1 (0xFFFF…FFFF) 로 늘려 `x0 − 4` 를 만들고, `uxtw` 는 4294967295 로 늘려 `x0 + 0x3FFFFFFFC` 를 만든다. clang 은 같은 C 를 다르게 내린다 — 인덱스를 아예 `ldrsw x9, [x1], #4` 로 64비트 부호 확장해서 읽고 `ldrsw x9, [x0, x9, lsl #2]` (64비트 레지스터 + lsl) 를 쓴다. 둘 다 맞다: "부호 확장을 로드 때 하느냐, 주소 계산 때 하느냐" 의 차이다.

**복사 — LDP/STP.** `copy_ldp_stp` 를 n = 3 으로 추적:

| 단계 | 명령 | x0 (dst) | x1 (src) | x2 | x3 | x4, x5 |
|---|---|---|---|---|---|---|
| 1 | `lsr x3, x2, #1` | D | S | 3 | 1 | |
| 2 | `ldp x4, x5, [x1], #16` | D | S+16 | 3 | 1 | s0, s1 |
| 3 | `stp x4, x5, [x0], #16` | D+16 | S+16 | 3 | 1 | dst[0..1] 기록 |
| 4 | `subs x3, x3, #1` → `b.ne` 안 뜀 | | | | 0 | |
| 5 | `tbz x2, #0, 3f` | | | 3 = 0b11 → bit0 = 1, 안 뜀 | | |
| 6 | `ldr x4, [x1]` / `str x4, [x0]` | D+16 | S+16 | | | dst[2] = s2 |

LDP 한 번 = 16바이트 = 명령 1개 + 레지스터 2개. 같은 일을 하는 명령 수가 거의 반이 되고, 프롤로그 `stp x29, x30, [sp, #-16]!` 도 바로 이 명령이다 (연습 4, ARM-B08). (objdump 에서 `tbz x2, #0` 이 `tbz w2, #0x0` 로 찍히는데, 비트 번호 < 32 이면 인코딩의 b5 = 0 이라 디스어셈블러가 w 로 보여 줄 뿐 같은 명령이다.) 어느 쪽이 실제로 더 빠른지는 마이크로아키텍처마다 다르다 — 측정은 ARM-B10.

하네스의 복사 테스트는 n = 0..40 × dst 시작을 0/1 원소 이동 (16바이트 정렬/8바이트 정렬) 으로 돌리고, dst 앞뒤 가드 `0xDEADBEEFCAFEF00D` 가 그대로인지 본다 — "n 개보다 하나 더 쓰기" 버그를 잡는 장치다.

### Part B — 폭과 부호 확장, 그리고 범위

| C 타입 | 로드 | 결과 (x 레지스터) | 예: 메모리 0xFF… |
|---|---|---|---|
| `uint8_t` | `ldrb w3, [..]` | zero-extend | 0xFF → 255 |
| `int8_t` | `ldrsb x3, [..]` | sign-extend | 0xFF → −1 |
| `uint16_t` | `ldrh w3, [..]` | zero-extend | 0xFFFF → 65535 |
| `int16_t` | `ldrsh x3, [..]` | sign-extend | 0xFFFF → −1 |
| `uint32_t` | `ldr w3, [..]` | zero-extend (w 쓰기는 상위 32비트를 0으로) | → 4294967295 |
| `int32_t` | `ldrsw x3, [..]` | sign-extend | → −1 |
| 64비트 | `ldr x3, [..]` | 그대로 | |

`sum_s32_bug` 는 `int32_t` 를 `ldr w3` 로 읽는다. `{1, −1, 3}` 이면 `1 + 4294967295 + 3 = 4294967299` — 하네스가 이 정확한 값을 CHECK 한다. 양수만 넣는 테스트 `{1, 2, 3}` 은 통과해 버린다. 이게 "랜덤 테스트는 부호 비트가 켜진 값을 반드시 포함해야" 하는 이유다 (하네스의 랜덤 데이터는 `(int32_t)(uint32_t)xs64()` 라 절반이 음수).

**C 추상 기계 vs ISA.** asm 의 `add` 는 64비트에서 그냥 wrap 한다. C 의 `int64_t` 합이 넘치면 UB 라서, 참조 구현은 `uint64_t` 로 합을 낸다 (`sum_imm({UINT64_MAX, 2}) == 1`). `int32_t` 합은 n < 2³² 이면 `int64_t` 를 넘칠 수 없어서 그대로 둔다. `sum_pre` 의 `a − 8` 도 C 였다면 UB 인 포인터지만 asm 에선 문제 없다 — 접근만 안 하면 된다.

**`uxtw` 버그의 실제 결과** (`demo_uxtw.c`, base = 배열 가운데, k = −1):

```text
$ cc -std=c11 -O2 demo_uxtw.c demo_uxtw.S -o /tmp/ARM-B04_uxtw && /tmp/ARM-B04_uxtw
sxtw: base[-1] = -222
signal 10 (SIGSEGV=11, SIGBUS=10) fault addr = base + 0x3fffffffc
```

fault 주소가 정확히 `base + 0xFFFFFFFF × 4` 다. 맥은 이 매핑 안 된 주소 접근을 SIGSEGV 가 아니라 **SIGBUS** 로 알렸다 (macOS 에서 어떤 신호가 오는지는 주소 영역에 따라 다를 수 있다 — 이 실행에서 관찰된 값). 크래시 덤프에서 "fault 주소 = 레지스터 + 0x3fffffffc 근처" 를 보면 **음수 32비트 인덱스를 zero-extend** 한 버그를 의심하면 된다. 펌웨어 MCU (MPU 없는 Cortex-M) 라면 크래시 대신 엉뚱한 주소의 값을 조용히 읽는다 — 더 나쁘다.

**오프셋 범위 — 진짜 어셈블러로.** `bash demo_range.sh` 의 실제 출력 (Apple clang 21, 일부 OK 줄 생략 — 전체 27줄은 `demo_range.txt`):

```text
--- 부호 없는 오프셋 (imm12 x 크기): LDR X 는 0..32760, 8의 배수
OK   ldr x0, [x1, #32760]           -> ldr x0, [x1, #0x7ff8]        0xf97ffc20
ERR  ldr x0, [x1, #32768]           -> index must be an integer in range [-256, 255].
OK   ldr x0, [x1, #4]               -> ldur x0, [x1, #0x4]          0xf8404020
OK   ldr x0, [x1, #-8]              -> ldur x0, [x1, #-0x8]         0xf85f8020
OK   ldr w0, [x1, #16380]           -> ldr w0, [x1, #0x3ffc]        0xb97ffc20
OK   ldrb w0, [x1, #4095]           -> ldrb w0, [x1, #0xfff]        0x397ffc20
ERR  ldrb w0, [x1, #4096]           -> index must be an integer in range [-256, 255].
--- 비스케일 (imm9 = -256..255 바이트): LDUR
OK   ldur x0, [x1, #255]            -> ldur x0, [x1, #0xff]         0xf84ff020
ERR  ldur x0, [x1, #256]            -> index must be an integer in range [-256, 255].
--- pre/post-index (imm9)
OK   ldr x0, [x1, #255]!            -> ldr x0, [x1, #0xff]!         0xf84ffc20
ERR  ldr x0, [x1, #256]!            -> invalid operand for instruction
OK   ldr x0, [x1], #-256            -> ldr x0, [x1], #-0x100        0xf8500420
--- LDP/STP (imm7 x 8 = -512..504)
OK   ldp x0, x1, [x2, #504]         -> ldp x0, x1, [x2, #0x1f8]     0xa95f8440
ERR  ldp x0, x1, [x2, #512]         -> index must be a multiple of 8 in range [-512, 504].
ERR  ldp x0, x1, [x2, #4]           -> index must be a multiple of 8 in range [-512, 504].
--- 레지스터 오프셋 (shift 는 0 또는 log2(크기))
ERR  ldr x0, [x1, x2, lsl #2]       -> expected 'lsl' or 'sxtx' with optional shift of #0 or #3
ERR  ldr w0, [x1, w2]               -> expected 'uxtw' or 'sxtw' with optional shift of #0 or #2
--- writeback 과 같은 레지스터 (Arm ARM: CONSTRAINED UNPREDICTABLE) / 자동 LDUR 치환
ERR  ldr x0, [x0], #8               -> unpredictable LDR instruction, writeback base is also a source
ERR  ldp x0, x1, [x0], #16          -> unpredictable LDP instruction, writeback base is also a destination
ERR  ldp x1, x1, [x0]               -> unpredictable LDP instruction, Rt2==Rt
OK   ldrsw x0, [x1, #-4]            -> ldursw x0, [x1, #-0x4]       0xb89fc020
```

읽는 법:

- `ldr x, [xn, #imm]` 의 imm 은 **크기 단위**로 저장된다 (imm12 × 8 → 최대 4095 × 8 = 32760). 음수나 8의 배수가 아닌 값을 쓰면 LLVM 어셈블러가 **알아서 `ldur`** (imm9, 바이트 단위) 로 바꾼다. 둘 다 안 되면 에러 메시지가 `ldur` 기준 범위 `[-256, 255]` 를 말한다 — `#32768` 에러가 엉뚱하게 "−256..255" 라고 하는 이유다.
- 범위 밖 오프셋이 꼭 필요하면 `add x9, x1, #8, lsl #12` 처럼 base 를 먼저 옮기거나, 오프셋을 레지스터에 넣고 레지스터 오프셋 모드를 쓴다 (컴파일러도 그렇게 한다).
- `ldr x0, [x0], #8` 처럼 writeback base 와 목적 레지스터가 같으면 Arm ARM 상 CONSTRAINED UNPREDICTABLE 이라 어셈블러가 거부한다.

### Part C — 구조체 배열: 오프셋과 stride

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">rec_t r[i] 한 개 = 16 바이트 (little-endian)</text>
  <rect x="80" y="44" width="144" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="224" y="44" width="144" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="368" y="44" width="288" height="34" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="152" y="66">id (uint32_t)</text><text x="296" y="66">temp (int32_t)</text><text x="512" y="66">ts (uint64_t)</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="76" y="96">+0</text><text x="220" y="96">+4</text><text x="364" y="96">+8</text><text x="644" y="96">+16</text>
  </g>
  <text x="664" y="66" fill="currentColor" font-size="12">→ r[i+1]</text>
  <path d="M224 110 L224 118 L368 118 L368 110" stroke="currentColor" fill="none"/>
  <text x="80" y="140" fill="currentColor" font-family="monospace">ldrsw x3, [x0, #4]       : 4 바이트만 읽고 부호 확장 → temp</text>
  <path d="M80 160 L80 168 L368 168 L368 160" stroke="currentColor" fill="none"/>
  <path d="M370 160 L370 168 L656 168 L656 160" stroke="currentColor" fill="none"/>
  <text x="80" y="190" fill="currentColor" font-family="monospace">ldp x4, x5, [x0], #16    : x4 = id | (temp &lt;&lt; 32),  x5 = ts,  x0 += 16</text>
  <text x="80" y="214" fill="currentColor" font-family="monospace">cmp w4, w2               : 하위 32 비트 = id 만 비교 (temp 무시)</text>
  <text x="80" y="238" fill="currentColor" font-family="monospace">stp w1, w2, [x0], #8     : id, temp 를 한 번에 쓰고 x0 += 8</text>
  <text x="80" y="260" fill="currentColor">.S 의 #define REC_TEMP 4 / REC_SIZE 16 ↔ C 의 _Static_assert(offsetof / sizeof)</text>
</svg>
```

- **필드 오프셋은 즉시값, stride 는 add.** `rec_sum_temp` 는 `ldrsw x3, [x0, #REC_TEMP]` 로 필드를 읽고 `add x0, x0, #REC_SIZE` 로 다음 레코드. 크래시 덤프의 `ldr x8, [x19, #0x28]` 을 "x19 = 구조체 포인터, 0x28 = 어떤 필드의 offsetof" 로 읽는 근거가 이것이다.
- **clang 은 한 수 더 둔다.** 같은 C (`demo_clang.c`) 의 실제 출력:

```text
_c_rec_sum_temp:
	cbz	x1, LBB2_4
	mov	x8, x0
	mov	x0, #0
	add	x8, x8, #4            ; 포인터를 미리 temp 필드로 옮겨 두고
LBB2_2:
	ldrsw	x9, [x8], #16         ; post-index 로 "필드 읽기 + 다음 레코드" 를 한 명령에
	add	x0, x0, x9
	subs	x1, x1, #1
	b.ne	LBB2_2
	ret
```

  base 를 `r + 4` 로 바꿔 두면 오프셋 모드 + add 두 명령이 post-index 하나가 된다. 루프 본문이 5 → 4 명령. 손으로 짠 `rec_sum_temp` 도 이렇게 바꿀 수 있다 (연습 5 는 다른 방법).
- **LDP 로 레코드 통째로.** `rec_sum_ts_for_id` 는 레코드당 메모리 명령 1개. x4 의 상위 32비트에 temp 가 섞여 있어도 `cmp w4, w2` 는 하위 32비트만 비교한다. 하네스의 `trick` 테스트 (`{5, 5, 10}, {6, 5, 20}`, id = 5 → 10) 는 `cmp x4, x2` 로 잘못 쓰면 0 이 나와 잡힌다.
- **쓰기 쪽.** `rec_log_push` 는 `stp w1, w2, [x0], #8` (id·temp 8바이트) + `str x3, [x0], #8` (ts) — writeback 두 번으로 x0 이 정확히 `slot + 1` 이 되고 그대로 반환값이다. clang 은 `stp w1, w2, [x0]` / `str x3, [x0, #8]` / `add x0, x0, #16` 으로 같은 일을 했다 (명령 수 3 vs 4 — 우리 쪽이 1개 적지만, 측정 없이 "더 빠르다" 고 말하진 말자).
- **레이아웃을 못 박아라.** asm 은 C 구조체를 모른다. 누가 `rec_t` 에 필드를 하나 추가하면 asm 은 조용히 틀린 값을 읽는다. `_Static_assert(offsetof(rec_t, temp) == 4, ...)` 는 그 순간 **컴파일 에러**로 알려 준다. (큰 프로젝트는 asm-offsets 생성 — Linux 의 `asm-offsets.c` — 로 상수를 자동 생성한다.)

`demo_modes.c` 의 실제 출력 (1. 문제의 그림이 이것):

```text
ldr x2, [x0, #8]           base before = a+ 0 B   value = 20   base after = a+ 0 B  (+0)
ldr x2, [x0, #8]!          base before = a+ 0 B   value = 20   base after = a+ 8 B  (+8)
ldr x2, [x0], #8           base before = a+ 0 B   value = 10   base after = a+ 8 B  (+8)
ldr x3, [x0, x2, lsl #3]   base before = a+ 0 B   value = 30   base after = a+ 0 B  (+0)
ldr x3, [x0, w2, sxtw #3]  base before = a+16 B   value = 20   base after = a+16 B  (+0)
```

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_bugs.S -o /tmp/ARM-B04_solution && /tmp/ARM-B04_solution
# 36/36 passed  (0.004 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   harness.c solution.S shared_bugs.S -o /tmp/ARM-B04_asan && /tmp/ARM-B04_asan
# 36/36 passed  — ASan/UBSan 경고 없음 (asm 쪽 접근은 계측되지 않는다는 점은 기억: 가드 검사가 그 공백을 메운다)
```

심어 둔 버그 파일도 같이 본다:

<!-- code: shared_bugs.S -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `int32_t` 를 `ldr w3, [x0], #4` 로 읽어 x 에 더함 | w 쓰기는 zero-extend → −1 이 4294967295 | `ldrsw x3, [x0], #4` |
| 음수 가능 인덱스에 `uxtw` | 주소가 base + 16 GiB 근처 → 실측 SIGBUS | `[x0, w5, sxtw #2]` |
| `ldr x3, [x0, #8]!` 를 a 에서 바로 시작 | 첫 원소를 건너뛰고 a[n] 까지 읽음 (한 칸 밀림) | 시작 전 `sub x0, x0, #8` 또는 post-index |
| 둘째 원소를 `[x0, #1]` 로 | 오프셋은 **바이트** — `ldur` 로 바뀌어 바이트 1..8 을 읽음 | `[x0, #8]` |
| `ldr x0, [x1, x2, lsl #2]` (8바이트 로드) | shift 는 0 또는 3 만 | `lsl #3`, 또는 int32 면 `ldr w0` + `lsl #2` |
| LDP 복사에서 꼬리 누락 | n 홀수면 마지막 원소 안 복사 (`copy_ldp_stp_notail`) | `tbz x2, #0, 끝` + 1개 복사 |
| 꼬리를 `ldp` 로 한 번 더 | n 홀수면 dst[n] 을 덮어씀 → 가드 깨짐 | 꼬리는 `ldr/str` 1개 |
| `n == 0` 검사 없이 `subs`/`b.ne` 루프 | 0 − 1 = 2⁶⁴ − 1 → 사실상 끝없이 읽다가 크래시 (`sum(NULL, 0)` 테스트가 잡음) | 루프 전 `cbz x1, 끝` |
| asm 에 구조체 오프셋 하드코딩만 | C 쪽 필드 추가 시 조용히 틀림 | `_Static_assert(offsetof…)` |
| `ldr x0, [x0], #8` | writeback base = 목적 레지스터 → 어셈블러 거부 (UNPREDICTABLE) | 다른 목적 레지스터 사용 |

## 8. 면접 Q&A

<details>
<summary>Q1. pre-index 와 post-index 의 차이는? 각각 어디에 쓰나요?</summary>

pre-index `[xn, #k]!` 는 xn += k 를 **먼저** 하고 새 주소에 접근, post-index `[xn], #k` 는 옛 주소에 접근한 **뒤** xn += k. 둘 다 base 를 갱신(writeback)한다. 쓰임새: 스택 push 는 pre (`stp x29, x30, [sp, #-16]!` — 공간을 먼저 만들고 저장), pop 은 post (`ldp x29, x30, [sp], #16` — 읽고 나서 반납). 배열 순회 `*p++` 는 post. 꼬리질문 "writeback 없는 형식은?" → `[xn, #k]` (구조체 필드), `[xn, xm, lsl #s]` (인덱스).

</details>

<details>
<summary>Q2. `ldr x0, [x1, #imm]` 에 넣을 수 있는 가장 큰 imm 은? 범위를 넘으면?</summary>

부호 없는 오프셋 형식은 imm12 를 접근 크기로 스케일한다 → 8바이트 로드면 0..32760, 8의 배수 (실측: `#32760` OK, `#32768` 에러). 음수나 정렬 안 된 오프셋은 `ldur` (imm9, −256..255 바이트) 로 인코딩된다 — LLVM 은 `ldr x0, [x1, #-8]` 을 자동으로 `ldur` 로 바꿨다. 둘 다 안 되면 base 를 `add` 로 옮기거나 오프셋을 레지스터에 넣는다. LDP/STP 는 imm7 × 8 이라 −512..504 로 더 좁다. 이유: 32비트 고정 길이 명령에 opcode·레지스터 필드를 넣고 남은 비트가 그만큼이다.

</details>

<details>
<summary>Q3. `ldr w` 와 `ldrsw` 차이는? w 레지스터에 쓰면 x 의 상위 비트는?</summary>

`ldr w3, [..]` 는 32비트를 읽어 w3 에 넣고, AArch64 규칙상 **w 레지스터 쓰기는 x 의 상위 32비트를 0 으로** 만든다 (zero-extend). `ldrsw x3, [..]` 는 32비트를 읽어 64비트로 부호 확장한다. `int32_t` 를 64비트 계산에 쓰면 ldrsw, `uint32_t` 면 ldr w. 같은 원리로 `ldrb/ldrh` (zero) vs `ldrsb/ldrsh` (sign). 이 레슨의 `sum_s32_bug` 가 `{1, −1, 3}` 에서 4294967299 를 내는 게 실증.

</details>

<details>
<summary>Q4. `[x0, w1, sxtw #2]` 와 `[x0, w1, uxtw #2]` 는 언제 쓰나요?</summary>

32비트 인덱스를 64비트 주소에 더할 때 확장 방식을 고른다. C 의 `int i` 로 `a[i]` → sxtw (음수 가능), `unsigned i` → uxtw. 잘못 고르면 i = −1 에서 base + 0x3FFFFFFFC 를 읽는다 (demo_uxtw: 실제로 그 주소에서 fault). 컴파일러는 종종 인덱스를 미리 64비트로 부호 확장해 두고 `[x0, x9, lsl #2]` 를 쓴다 — `size_t` 인덱스를 쓰면 확장 자체가 필요 없어서 루프가 단순해지는 이유 중 하나다.

</details>

<details>
<summary>Q5. LDP/STP 를 쓰면 뭐가 좋나요? LDP 는 원자적인가요?</summary>

명령 하나로 레지스터 두 개 (최대 16바이트, Q 레지스터면 32바이트) 를 옮겨 명령 수·코드 크기가 준다. 프롤로그/에필로그, memcpy, 구조체 복사의 기본 도구. 원자성: 기본적으로 LDP 는 **레지스터 하나 단위**로만 single-copy atomic 이고 16바이트 전체가 원자적이라는 보장은 없다. Armv8.4 의 FEAT_LSE2 가 있으면 16바이트 정렬된 64비트 쌍 LDP/STP 가 (Normal cacheable 메모리에서) 16바이트 single-copy atomic 으로 정의된다고 알고 있다 — Arm ARM 기준, 확인 권장. 128비트 원자 연산이 필요하면 LDXP/STXP 나 CASP (ARM-M04 계열) 를 쓴다.

</details>

<details>
<summary>Q6. AArch64 에서 정렬 안 된 주소로 LDR 하면 어떻게 되나요? Cortex-M 은?</summary>

AArch64 의 일반(Normal) 메모리 접근은 SCTLR_ELx.A = 0 이면 비정렬도 동작한다 (리눅스/macOS 사용자 공간이 이 경우 — 하네스의 `sum_u8` 처럼 바이트 접근은 원래 상관없음). 하지만 Device 메모리 (MMIO) 비정렬 접근, 배타적 로드/스토어 (LDXR 등), 그리고 A = 1 설정에서는 alignment fault 가 난다 (Arm ARM 기준, 확인 권장). Cortex-M0/M0+ (ARMv6-M) 는 비정렬 LDR 이 HardFault, Cortex-M3/M4 는 LDR/LDRH/STR/STRH 의 비정렬은 허용하지만 (CCR.UNALIGN_TRP = 0 일 때) LDM/STM/LDRD/STRD 는 항상 UsageFault (ARMv7-M ARM 기준, 확인 권장) — 펌웨어에서 패킷 버퍼를 `uint32_t *` 로 캐스팅할 때 터지는 단골 원인. 비정렬 실험 자체는 이 레슨 범위 밖.

</details>

<details>
<summary>Q7. 32비트 ARM 의 LDM/STM, PUSH/POP 은 AArch64 에서 어디 갔나요?</summary>

AArch64 는 LDM/STM (레지스터 목록 여러 개를 한 명령에) 을 없애고 LDP/STP (딱 2개) 만 남겼다. 이유로 흔히 드는 것: 명령 하나가 가변 개수의 메모리 접근을 하면 인터럽트/페이지 폴트 중간 처리와 파이프라인 설계가 복잡해진다 (설계 의도는 Arm 공식 문서에 명시된 것이 아니라 일반적 해석 — hedge). Cortex-M (Thumb-2) 에는 여전히 `push {r4-r7, lr}` / `pop {r4-r7, pc}` 가 있다 (ARM-B13). AArch64 에는 PC 를 로드하는 `pop {pc}` 같은 것도 없어서 복귀는 항상 `ret` (x30) 이다.

</details>

<details>
<summary>Q8. 크래시 덤프에 `ldr x8, [x19, #0x28]` 에서 data abort, x19 = 0x0 이면?</summary>

NULL 구조체 포인터 역참조: fault 주소가 0x28 = 그 구조체의 offsetof 어떤 필드. `offsetof(T, field) == 0x28` 인 필드를 찾으면 어떤 포인터가 NULL 이었는지 안다. x19 는 callee-saved 라서 보통 함수 진입 시 인자 포인터를 보관하는 레지스터 — "어느 인자가 NULL 이었나" 까지 좁혀진다. fault 주소가 base + 0x3fffffffc 근처면 Q4 의 uxtw/음수 인덱스, 페이지 경계 바로 뒤면 길이 한 칸 초과를 의심한다. (덤프 역추적은 ARM-M14.)

</details>

## 9. 추가 연습

### 연습 1 — LDP 로 배열 합

`uint64_t sum_ldp(const uint64_t *a, size_t n)` — 원소 두 개를 `ldp` 한 번으로. 홀수 꼬리 처리.

<details>
<summary>답 보기</summary>

`lsr x5, x1, #1` 으로 쌍 개수, 루프에서 `ldp x3, x4, [x0], #16` + add 두 번, 끝나면 `tbz x1, #0` 로 꼬리 1개 `ldr x3, [x0]`. 메모리 명령이 원소당 0.5 개로 준다. `sum_imm` 의 두 `ldr` 을 하나로 합친 버전이다.

</details>

### 연습 2 — 뒤집어 복사

`void reverse_copy(uint64_t *dst, const uint64_t *src, size_t n)`: `dst[i] = src[n-1-i]`.

<details>
<summary>답 보기</summary>

`add x0, x0, x2, lsl #3` 로 dst 끝 다음을 만들고, 루프에서 `ldr x3, [x1], #8` (앞에서 post-index) + `str x3, [x0, #-8]!` (뒤에서 **음수 pre-index**). 뒤쪽 쓰기가 정확히 스택 push 모양 — "먼저 줄이고 쓴다". 가드로 dst[−1], dst[n] 이 안 깨졌는지 확인한다.

</details>

### 연습 3 — 보정 테이블 조회 (uxtw)

ADC 원시값 (`uint8_t`) 으로 256 엔트리 `uint16_t` 보정 테이블을 조회해 합한다: `uint64_t lut_sum_u16(const uint16_t *tab, const uint8_t *keys, size_t n)`.

<details>
<summary>답 보기</summary>

`ldrb w4, [x1], #1` (키, 0..255) → `ldrh w5, [x0, w4, uxtw #1]` (2바이트 원소라 shift 1, 키는 unsigned 라 uxtw). 키 255 를 꼭 테스트한다 — 여기서는 `ldrb` 가 zero-extend 해서 w4 = 255 이므로 sxtw 로 써도 우연히 맞지만, 키를 `ldrsb` 로 잘못 읽으면 −1 → tab[−1] 이 된다.

</details>

### 연습 4 — 스택 push2 / pop2 흉내

`uint64_t *push2(uint64_t *sp, uint64_t a, uint64_t b)` 와 `uint64_t *pop2(uint64_t *sp, uint64_t *a, uint64_t *b)` — 각각 새 sp 반환.

<details>
<summary>답 보기</summary>

push2: `stp x1, x2, [x0, #-16]!` + `ret` — writeback 된 x0 이 곧 새 sp. pop2: `ldp x3, x4, [x0], #16` + 두 `str`. 프롤로그/에필로그의 `stp x29, x30, [sp, #-16]!` / `ldp x29, x30, [sp], #16` 과 같은 모양이고, 16바이트씩 움직이므로 sp 16바이트 정렬이 유지된다 (진짜 스택 프레임은 ARM-B08).

</details>

### 연습 5 — temp 를 asr 로 꺼내기

`int64_t rec_sum_temp_asr(const rec_t *r, size_t n)` — 레코드 앞 8바이트를 x 레지스터 하나로 읽고 temp 를 꺼내라.

<details>
<summary>답 보기</summary>

`ldr x3, [x0], #16` → x3 = id | temp << 32 (little-endian), `asr x3, x3, #32` (산술 시프트 = 부호 확장) 로 temp. post-index 하나로 stride 까지 처리. 빅엔디언 시스템이면 id 와 temp 의 위치가 바뀐다 — 레이아웃·엔디언 가정이 코드에 숨어 있다는 점이 핵심 (비트 명령은 ARM-B06).

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-B04_practice && /tmp/ARM-B04_practice
# 14/14 passed  (ASan/UBSan 빌드도 14/14)
```

## 10. 핵심 정리

- 모드 5개: `[xn,#k]` (base 불변) · `[xn,#k]!` (먼저 더하고 접근, 갱신) · `[xn],#k` (접근 후 더함, 갱신) · `[xn,xm,lsl #s]` · `[xn,wm,sxtw/uxtw #s]`. `s` = 0 또는 log2(크기).
- 오프셋은 **바이트**. imm12 × 크기 (LDR X: 0..32760), imm9 −256..255 (LDUR · pre · post), imm7 × 8 (LDP X: −512..504). 어셈블러가 가능하면 `ldur` 로 자동 치환.
- 폭/부호: `ldrb/ldrh/ldr w` = zero, `ldrsb/ldrsh/ldrsw` = sign. w 레지스터 쓰기는 x 상위 32비트를 0으로.
- 32비트 `int` 인덱스는 `sxtw`, `unsigned` 는 `uxtw`. 틀리면 base + 0x3FFFFFFFC 근처에서 fault.
- LDP/STP = 2개씩. 복사는 쌍 루프 + 홀수 꼬리. push = `stp …, [sp, #-16]!`, pop = `ldp …, [sp], #16`.
- 구조체 필드 = 즉시 오프셋, 원소 이동 = stride. 오프셋은 `_Static_assert(offsetof…)` 로 C 와 동기화.
- writeback base = 목적 레지스터는 UNPREDICTABLE → 어셈블러가 거부.

## 참고자료

- [Arm Architecture Reference Manual for A-profile (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest) — C6.2 LDR (immediate / register), LDRSW, LDP/STP, LDUR; CONSTRAINED UNPREDICTABLE writeback 규칙; FEAT_LSE2
- [Arm A64 Instruction Set Architecture (DDI 0602)](https://developer.arm.com/documentation/ddi0602/latest) — 명령별 인코딩 표
- [Learn the architecture — A64 Instruction Set Architecture Guide (102374)](https://developer.arm.com/documentation/102374/latest) — Loads and stores, addressing modes 장
- [Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst) — 인자 레지스터, 구조체 레이아웃
- [Apple — Writing ARM64 code for Apple platforms](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms) — Mach-O 심볼 `_` 접두사 등
- 이 디렉터리: `demo_modes.c/.S` (모드별 before/after), `demo_range.sh` → `demo_range.txt` (범위·에러), `demo_uxtw.c/.S` (sxtw vs uxtw 크래시), `demo_clang.c` (clang 의 선택)
- 관련 레슨: ARM-B01 (.S 를 C 에서 호출), ARM-B03 (즉시값), ARM-B05 (cbz/b.cond/tbz), ARM-B06 (asr/비트), ARM-B08 (스택 프레임), ARM-B10 (memcpy 성능), ARM-M04 (LDXR/STXR), ARM-A11 (벡터화 출력), C-B04 (필드 매크로), TOOL-B04 (objdump)
