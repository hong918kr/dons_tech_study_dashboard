---
id: ARM-M02
time: 80분
updated: 2026-11-15
---
# ARM-M02 C 인라인 어셈블리: 제약조건과 Clobber

## 학습 목표

- GCC/Clang extended asm 한 문장을 **계약서**로 읽는다: 템플릿 · 출력 · 입력 · clobber 가 각각 컴파일러에게 무엇을 약속하는지, 약속하지 않은 효과(플래그, 메모리, 시간)는 컴파일러가 모른다는 것을 설명한다.
- `rbit`/`clz`/`ctz`, 캐리 포함 128비트 덧셈(`ADDS`/`ADC`), `CNTVCT_EL0`/`CNTFRQ_EL0` 읽기를 래퍼로 직접 쓴다. 제약은 `r`, `=r`, `+r`, `=&r`, `w`, `I`/`K`/`L`, `Q`, `rZ`, 수정자는 `%w`/`%x`/`%s`/`%d`, clobber 는 `"cc"`/`"memory"` 를 쓴다.
- 제약 하나를 빠뜨린 버그 7종을 **-O2 에서 실제로 재현**하고(Apple clang 21, VM 의 gcc 15 / clang 21), 각 버그가 생성된 asm 의 어느 줄에서 생기는지 짚는다.
- 드라이버 헬퍼(`mmio_write32`, `dma_wmb`, 카운터 데드라인 폴링)를 써서 "디스크립터 → 배리어 → 도어벨" 순서를 컴파일러와 하드웨어 양쪽에서 지킨다.
- 이 맥의 실측값을 말할 수 있다: CNTFRQ = **1 GHz** 인데 실제 증분은 41/42 (= 24 MHz), PMCCNTR_EL0 는 EL0 에서 SIGILL.

## 1. 문제

"이 inline asm, clobber 하나 빠졌는데 보여요?" — 펌웨어 면접의 단골 코드 리뷰 문제다. 부트로더·커널·HAL(CMSIS `__DMB()`, Linux `writel()`)의 바닥은 전부 몇 줄짜리 inline asm 이다. 제약이 틀려도 **-O0 에서는 대개 맞게 돈다.** 버그는 -O2 에서 레지스터 할당이 바뀔 때만 나온다. 그래서 디버그 빌드는 통과하고 릴리스 펌웨어만 깨진다.

핵심은 하나다. **컴파일러는 asm 템플릿 문자열을 읽지 않는다.** 컴파일러가 아는 것은 오퍼랜드 목록에 적힌 것뿐이다. 적지 않은 효과는 없는 것으로 친다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="22" fill="currentColor" font-weight="bold">extended asm = 컴파일러와의 계약서 — 템플릿 안은 블랙박스, 아는 건 콜론 뒤에 적은 것뿐</text>
<rect x="250" y="40" width="240" height="120" rx="6" class="f-mut" stroke="currentColor"/>
<text x="370" y="62" fill="currentColor" text-anchor="middle" font-family="monospace">"adds %x[l], %x[al], %x[bl]</text>
<text x="370" y="80" fill="currentColor" text-anchor="middle" font-family="monospace"> adc  %x[h], %x[ah], %x[bh]"</text>
<text x="370" y="110" fill="currentColor" text-anchor="middle">템플릿: 컴파일러는 해석 안 함</text>
<text x="370" y="130" fill="currentColor" text-anchor="middle">(%-치환만 하고 어셈블러로 넘김)</text>
<rect x="20" y="50" width="200" height="44" rx="4" class="f-acc" stroke="currentColor"/>
<text x="30" y="68" fill="currentColor">출력: "=&amp;r"(l), "=r"(h)</text>
<text x="30" y="86" fill="currentColor" font-size="12">무엇을 언제 쓰나 (&amp; = 입력보다 먼저)</text>
<rect x="20" y="110" width="200" height="44" rx="4" class="f-acc" stroke="currentColor"/>
<text x="30" y="128" fill="currentColor">입력: "r"(a_lo), "r"(b_lo) …</text>
<text x="30" y="146" fill="currentColor" font-size="12">어느 값이 어느 레지스터에</text>
<rect x="520" y="50" width="200" height="44" rx="4" class="f-warn" stroke="currentColor"/>
<text x="530" y="68" fill="currentColor">clobber: "cc"</text>
<text x="530" y="86" fill="currentColor" font-size="12">NZCV 를 덮는다</text>
<rect x="520" y="110" width="200" height="44" rx="4" class="f-warn" stroke="currentColor"/>
<text x="530" y="128" fill="currentColor">clobber: "memory"</text>
<text x="530" y="146" fill="currentColor" font-size="12">아무 메모리나 읽고 쓴다</text>
<path d="M220 72 L250 80" stroke="currentColor" fill="none"/> <path d="M220 132 L250 120" stroke="currentColor" fill="none"/>
<path d="M490 80 L520 72" stroke="currentColor" fill="none"/> <path d="M490 120 L520 132" stroke="currentColor" fill="none"/>
<g fill="currentColor">
<text x="20" y="195" font-weight="bold">적지 않으면 컴파일러가 가정하는 것 → 이 레슨에서 실제로 깨진 것</text>
<text x="20" y="220">· 출력에 &amp; 없음 → "입력을 다 읽은 뒤에 출력을 쓴다" → 출력이 입력 레지스터를 공유 (버그 1)</text>
<text x="20" y="242">· "cc" 없음 → "플래그 그대로" → asm 앞 subs 의 플래그를 asm 뒤 csel 이 씀 (버그 3)</text>
<text x="20" y="264">· "memory" 없음 → "메모리 안 건드림" → 옛 값 재사용 / 저장 자체 삭제 (버그 4·5)</text>
<text x="20" y="286">· %w / %x 수정자 틀림 → 템플릿 글자 그대로 어셈블 → 위 32비트 쓰레기 / 0 (버그 6)</text>
<text x="20" y="308">· volatile 없음 → "입력 같으면 출력 같음" → 카운터 읽기를 합치거나 루프 밖으로 (버그 7)</text>
</g>
</svg>
```

| 래퍼 | 명령 | 이 레슨에서 배우는 제약 |
|---|---|---|
| `rbit32/rbit64`, `clz64`, `ctz64` | RBIT, CLZ | `=r`/`r`, `%w`/`%x`, 명령 2개짜리 템플릿 |
| `add128` | ADDS + ADC | `=&r`, `"cc"`, "캐리 체인은 asm 하나에" |
| `read_cntvct`, `read_cntfrq` | MRS | `volatile` (입력 없는 asm) |
| `add_u64_u32`, `inc_flags`, `zero16` | ADD (uxtw), ADDS, STP | `%w`+`uxtw`, `+r`, `"memory"` |
| `fmadd_d/s`, `add_4095`, `keep_*` | FMADD, ADD #imm, AND #imm | `w`+`%d`/`%s`, `I`/`K`/`L` |
| `mmio_write32/read32`, `dma_wmb/rmb` | STR/LDR, DMB OSHST/OSHLD | `=Q`/`Q`, `rZ`, `"memory"` |

### Part A — 래퍼 6종

`rbit32(uint32_t)`, `rbit64(uint64_t)`, `clz64(uint64_t)` (0 이면 64), `ctz64` (RBIT+CLZ, 0 이면 64), `add128(a_hi, a_lo, b_hi, b_lo, &hi, &lo)` (ADDS/ADC 를 **asm 문장 하나**에), `read_cntvct()`/`read_cntfrq()`. C 기준 구현(비트 루프, `unsigned __int128`)과 랜덤 10만 개로 대조한다. 카운터는 단조 증가하는지, 루프 앞뒤 두 번 읽기가 0 이 아닌지 확인한다.

### Part B — 빠뜨리면 -O2 가 망가뜨리는 것

`demo_bugs.c` 에 제약을 하나씩 뺀 판 7종을 넣고, -O2 로 실제 틀린 값이 나오는지 본다 (early-clobber, 쪼갠 캐리 체인, `"cc"`, `"memory"` 2가지, 폭 수정자 2가지, `volatile`). 각 버그를 고친 래퍼를 solution 이 테스트한다. 테스트는 버그가 드러나는 모양 그대로 짠다: `noinline` 함수 안에서 asm 앞에 `subs` 가 있는 경우, 잘린 `uint32_t` 를 넘기는 경우 등. FP 레지스터(`w` + `%d`/`%s`, 융합 곱셈합의 정확성)와 즉시값 제약(`I`/`K`/`L`, 범위 밖이면 컴파일 에러)도 여기서 다룬다.

### Part C — 드라이버 헬퍼: 가짜 NIC TX 링

SoC 이더넷 드라이버가 패킷을 보내는 순서를 그대로 쓴다: 디스크립터(`addr`, `len`, `flags=OWN`) 쓰기 → `dma_wmb()` → 도어벨 MMIO 쓰기. 장치 모델(하네스)이 도어벨까지 처리하고 DONE 을 표시한다. `nic_wait_done` 은 `CNTVCT` 데드라인으로 폴링하고, 1 ms 가 지나면 -1 을 준다. 배리어가 **바이너리에 실제로 있는지**는 함수 기계어에서 `DMB OSHST` 인코딩(`0xD50332BF`)을 찾아 확인한다.

## 2. Problem (English)

> **Inline-asm wrappers for an AArch64 bring-up library.**
>
> Using GNU extended inline asm only (no intrinsics/builtins in the wrappers), implement:
>
> 1. `uint32_t rbit32(uint32_t)`, `uint64_t rbit64(uint64_t)`, `unsigned clz64(uint64_t)` (clz64(0) == 64), `unsigned ctz64(uint64_t)` (ctz64(0) == 64).
> 2. `void add128(uint64_t a_hi, uint64_t a_lo, uint64_t b_hi, uint64_t b_lo, uint64_t *hi, uint64_t *lo)` with `ADDS`/`ADC`.
> 3. `uint64_t read_cntvct(void)`, `uint64_t read_cntfrq(void)`.
> 4. `uint64_t add_u64_u32(uint64_t base, uint32_t off)` (zero-extend `off`), `uint64_t inc_flags(uint64_t)` using `ADDS`, `void zero16(void *p)` using `STP XZR, XZR`, `double fmadd_d(double,double,double)` / `float fmadd_s(...)` (fused), and immediate forms `x + 4095`, `x & 0xF0F0F0F0`, `x & 0x00FF00FF00FF00FF`.
> 5. `void mmio_write32(volatile uint32_t *, uint32_t)`, `uint32_t mmio_read32(const volatile uint32_t *)`, `void dma_wmb(void)`, `void dma_rmb(void)`, and a TX path `uint32_t nic_tx_post(nic_t *, uint32_t *sw_tail, uint64_t buf, uint32_t len)` plus `int nic_wait_done(nic_t *, uint32_t idx, uint64_t timeout_ticks)`.
>
> Constraints: results must be correct at -O2 regardless of inlining and register allocation; the TX path must order descriptor writes before the doorbell write for both the compiler and the hardware.
>
> Follow-ups: Which outputs need `&`? Why can't the carry chain span two asm statements? What does `"cc"` promise, and how would you *return* the carry instead? When is `volatile` required, and is it a barrier? `"memory"` vs a `"Q"` operand? Why does `"w"` without a modifier fail to assemble? What does `CNTFRQ_EL0` read on Apple Silicon?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/ARM-M02_starter && /tmp/ARM-M02_starter
```

처음 실행 결과 (실제 출력, 일부 생략, exit 1 — 멈추지 않는다):

```text
FAIL starter.c:239  rbit32(1u) == 0x80000000u
FAIL starter.c:240  rbit32(0x12345678u) == 0x1E6A2C48u
… (Part A FAIL 7줄 더: rbit64, clz64, ctz64, 랜덤 대조, add128 3개)
info: CNTFRQ_EL0 = 0 Hz
FAIL starter.c:269  freq >= 1000000u && freq <= 2000000000u
info: 2M volatile loads took 0 ticks
FAIL starter.c:277  el > 0
FAIL starter.c:280  add_off_trunc(0x1000, 0xFFFFFFFF00000010ull) == 0x1010u
… (Part B FAIL 11줄 더)
FAIL starter.c:319  code_has_dmb_oshst(nic_tx_post)
… (Part C FAIL 5줄 더)
SKIP 2 polling checks: counter not working yet (Part A)
4/36 passed
```

스텁은 안전한 틀린 값(0, 그대로 반환)을 준다. 카운터가 0 이면 폴링 테스트는 SKIP 한다. 그래서 TODO 를 어떤 순서로 채워도 무한 루프에 빠지지 않는다.

여담: 처음에는 `fmadd_d` 스텁을 C 식 `a * b + c` 로 두었다. 그런데 이 스텁이 "융합" 테스트를 **통과**했다. Apple clang 은 기본 `-ffp-contract=on` 이라 한 식 안의 `a*b+c` 를 `fmadd` 로 합친다. 그래서 스텁을 `0.0` 으로 바꿨다. 반대로 C 기준 구현을 "중간 반올림이 있는 식" 으로 믿으면 안 된다는 뜻이기도 하다 (ARM-B11).

관찰용 파일 (전부 이 디렉터리):

```bash
cc -std=c11 -O2 demo_bugs.c -o /tmp/ARM-M02_bugs && /tmp/ARM-M02_bugs        # 버그 7종 재현 (경고 1개는 의도)
cc -std=c11 -O2 -S -fno-asynchronous-unwind-tables demo_bugs.c -o -          # 버그가 생긴 asm 줄
cc -std=c11 -O2 -S demo_constraints.c -o -                                   # 제약이 무엇으로 찍히나
cc -std=c11 -O2 -c -DERR_IMM demo_constraints.c ; cc -std=c11 -O2 -c -DERR_W demo_constraints.c   # 컴파일 에러
cc -std=c11 -O2 demo_timer.c -o /tmp/ARM-M02_timer && /tmp/ARM-M02_timer    # CNTFRQ/CNTVCT/EL0 접근 실측
# VM (gcc 15 / clang 21, aarch64 Linux): 맥 홈은 읽기 전용이라 /tmp 로 복사해서 빌드
limactl shell elinux -- bash -lc 'mkdir -p /tmp/ARM-M02_work && cd /tmp/ARM-M02_work && cp <lesson>/demo_bugs.c . && gcc -std=gnu11 -O2 demo_bugs.c -o b && ./b'
```

## 4. 힌트

<details>
<summary>힌트 1 — 문법 뼈대와 이름 붙은 오퍼랜드</summary>

```c
__asm__ [volatile] ("템플릿 %x[out], %w[in]"
                    : [out] "=r"(out_var)          /* 출력 */
                    : [in]  "r"(in_var)            /* 입력 */
                    : "cc", "memory");             /* clobber */
```

`%[name]` 은 그 오퍼랜드의 레지스터로 바뀐다. 정수 오퍼랜드의 기본 출력은 x 레지스터다. 32비트 값은 `%w[name]` 으로 w 레지스터를 쓴다. 이름을 쓰면 `%0 %1` 번호를 세다 틀릴 일이 없다.

</details>

<details>
<summary>힌트 2 — early-clobber 판정법</summary>

템플릿을 위에서부터 읽으며 물어본다: "이 출력을 쓴 **뒤에** 아직 읽을 입력이 남아 있나?" 남아 있으면 그 출력은 `=&r` 이다. `add128` 의 `lo` 는 `adds` 에서 쓰이고, 그 다음 `adc` 가 `a_hi`, `b_hi` 를 읽는다 → `&`. `hi` 는 마지막에 쓰이므로 `&` 가 필요 없다. `ctz64` 의 `rbit r, x; clz r, r` 는 r 을 쓴 뒤 x 를 다시 읽지 않는다 → 필요 없다.

</details>

<details>
<summary>힌트 3 — 출력 없는 asm, 입력 없는 asm</summary>

`zero16` 은 레지스터 출력이 없다. 메모리를 쓰는데 주소만 `"r"` 로 넘긴다면, 컴파일러에게 "메모리를 바꾼다" 를 따로 알려야 한다. 방법은 `"memory"` clobber 또는 메모리 오퍼랜드(`"=m"(*(char(*)[16])p)` 같은 꼴)다. `read_cntvct` 는 입력이 없다. 컴파일러 눈에는 "입력 없는 순수 계산 = 상수" 다. 매번 실행하게 만드는 키워드가 필요하다.

</details>

<details>
<summary>힌트 4 — FP 레지스터와 즉시값</summary>

double/float 는 `"w"` 제약 + `%d`/`%s` 수정자를 쓴다 (수정자가 없으면 `v0` 이 찍혀 스칼라 명령이 어셈블되지 않는다). 즉시값은 C 상수를 넘긴다: `"I"(4095)`, `"K"(0xF0F0F0F0u)`, `"L"(0x00FF00FF00FF00FFull)`. 변수는 안 된다.

</details>

<details>
<summary>힌트 5 — MMIO 와 배리어</summary>

MMIO 쓰기: `"str %w[v], %[m]" : [m] "=Q"(*addr) : [v] "rZ"(v)`. `Q` 는 오프셋 없는 `[Xn]` 주소다. 배리어: `dmb oshst` + `"memory"`. 순서: 디스크립터 필드 3개 → `dma_wmb()` → tail 증가 → 도어벨. 폴링: `deadline = read_cntvct() + timeout`. DONE 을 보면 `dma_rmb()` 후 0 을 돌려준다.

</details>

## 5. 풀이

### 0. 문법 한 장 — 제약 · 수정자 · clobber (AArch64)

| 표기 | 뜻 | 이 레슨의 쓰임 |
|---|---|---|
| `"r"` | 범용 레지스터 (입력) | 거의 모든 입력 |
| `"=r"` | 쓰기 전용 출력. **입력을 다 읽은 뒤** 쓴다고 가정 → 입력과 같은 레지스터를 받을 수 있음 | `rbit`, `clz`, `add128` 의 `hi` |
| `"+r"` | 읽고 쓰는 오퍼랜드 (입력이자 출력) | `inc_flags(y)` |
| `"=&r"` | early-clobber 출력. 모든 입력과 다른 레지스터 | `add128` 의 `lo`, 연습 2 `mul` |
| `"w"` | FP/SIMD 레지스터 (v0–v31). `x` = v0–v15, `y` = v0–v7 (by-element 용, GCC 문서 기준) | `fmadd_d/s`, 연습 4 |
| `"I"` | ADD 즉시값: 0..4095, 또는 그 값 `<< 12` | `add_4095` |
| `"J"` | SUB 쪽 (음수 ADD) 즉시값 (문서 기준) | — |
| `"K"` / `"L"` | 32 / 64비트 **논리 즉시값** (ARM-B03 의 bitmask immediate) | `keep_high_nibbles`, `keep_even_bytes` |
| `"M"` / `"N"` | 32 / 64비트 MOV 즉시값 (문서 기준, `"N"(0xFFFF0000)` → `mov x0, #4294901760` 실측) | — |
| `"Z"` (`"rZ"`) | 정수 0 이면 zero 레지스터 허용 | `mmio_write32(addr, 0)` |
| `"m"` | 메모리 오퍼랜드 (주소 모드는 컴파일러 마음) | 비교용 (아래 함정) |
| `"Q"` | 메모리, **베이스 레지스터 하나·오프셋 없음** `[Xn]` | `mmio_*`, 배타·acquire 명령 (ARM-M04) |
| `%w` `%x` | 같은 레지스터의 32 / 64비트 이름 (w5 / x5) | 폭 맞추기 |
| `%b` `%h` `%s` `%d` `%q` | v 레지스터의 8/16/32/64/128비트 스칼라 이름 | FP, `addv %b` |
| `%l[label]` | asm goto 의 C 라벨 | 연습 5 |
| `"cc"` | 조건 플래그(NZCV)를 덮는다 | `adds`, `adcs`, `subs`, `cmn` 쓰는 asm |
| `"memory"` | 오퍼랜드에 없는 메모리를 읽거나 쓴다 → 컴파일러 메모리 배리어 | `zero16`, `dma_wmb` |
| `volatile` | 출력이 안 쓰여도 지우지 말고, 합치지(CSE) 말고, 루프 밖으로 빼지 말 것 | `mrs cntvct`, MMIO, `dmb` |

`asm` 대신 `__asm__` 을 쓴 이유: `-std=c11` (GNU 확장 없는 모드)에서 `asm` 키워드는 없다. `__asm__` 은 어느 모드에서나 된다. 출력이 하나도 없는 asm 은 GCC 규칙상 암묵적으로 volatile 이다 (GCC 문서 "Volatile" 절, 기준).

### Part A — 한 줄씩

**RBIT/CLZ/CTZ.** `rbit %w[r], %w[x]` 는 32비트 뒤집기(`rbit32(0x12345678) = 0x1E6A2C48`)다. `%x` 로 쓰면 64비트 판이다. `clz64(0)` 은 ISA 가 64 로 정의한다. 반면 `__builtin_clzll(0)` 은 정의되지 않았다(UB). 그래서 테스트의 C 기준 구현은 0 을 따로 처리한다. `ctz64` 는 두 명령이지만 `&` 가 없다 (힌트 2).

**add128 — early-clobber 가 왜 필요한가.** `&` 를 빼면 컴파일러는 "출력은 입력을 다 읽은 뒤에 쓴다" 고 믿는다. 그래서 출력에 입력 레지스터를 재활용한다. `demo_bugs.c` 의 `bad_add128` (16바이트 구조체를 x0:x1 로 반환하는 모양)을 Apple clang `-O2` 로 본 실제 출력:

```text
_bad_add128:                     ; [l] "=r"   (& 없음)
	adds	x0, x1, x3        ; lo → x0 ... 그런데 x0 은 a_hi 였다
	adc	x1, x0, x2        ; a_hi 대신 방금 쓴 lo 를 읽는다
	ret
_good_add128:                    ; [l] "=&r"
	adds	x8, x1, x3        ; lo 는 입력과 다른 x8
	adc	x1, x0, x2
	mov	x0, x8
	ret
```

입력 `(1 : 2^64-1) + (2 : 1)` 의 정답은 `hi = 4` 다. 버그 판은 `hi = lo(0) + 2 + C(1) = 3` 을 냈다 (아래 결과표 1행). gcc 15 도 **똑같은 두 줄**을 냈다. 같은 asm 을 `*hi, *lo` 포인터 출력 모양(solution 의 시그니처)으로 쓰면 clang 은 x8/x9 를 골라 우연히 맞았다. 이게 이 버그가 무서운 이유다. **호출 모양, 인라인 여부, 최적화 레벨에 따라 나타났다 사라진다.** ARM-M04 에서는 같은 실수가 주소 레지스터를 덮어 SIGSEGV 가 됐다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="22" fill="currentColor" font-weight="bold">early-clobber: 출력이 "언제" 써지는지를 컴파일러에게 알려 주는 표시</text>
<text x="20" y="52" fill="currentColor" font-weight="bold">"=r" (틀림)</text>
<rect x="20" y="62" width="70" height="28" class="f-warn" stroke="currentColor"/><text x="55" y="81" fill="currentColor" text-anchor="middle" font-family="monospace">x0</text>
<text x="100" y="81" fill="currentColor">a_hi  ← 그리고 lo 도 x0 (같은 방)</text>
<rect x="20" y="100" width="300" height="26" class="f-mut" stroke="currentColor"/><text x="30" y="118" fill="currentColor" font-family="monospace">① adds x0, x1, x3   x0 = lo</text>
<rect x="20" y="134" width="300" height="26" class="f-warn" stroke="currentColor"/><text x="30" y="152" fill="currentColor" font-family="monospace">② adc  x1, x0, x2   a_hi 사라짐</text>
<text x="20" y="184" fill="currentColor">hi = lo + b_hi + C = 3  (정답 4)</text>
<text x="400" y="52" fill="currentColor" font-weight="bold">"=&amp;r" (맞음)</text>
<rect x="400" y="62" width="70" height="28" class="f-ok" stroke="currentColor"/><text x="435" y="81" fill="currentColor" text-anchor="middle" font-family="monospace">x8</text>
<text x="480" y="81" fill="currentColor">lo 전용 (입력과 겹치지 않음)</text>
<rect x="400" y="100" width="300" height="26" class="f-mut" stroke="currentColor"/><text x="410" y="118" fill="currentColor" font-family="monospace">① adds x8, x1, x3   x8 = lo</text>
<rect x="400" y="134" width="300" height="26" class="f-ok" stroke="currentColor"/><text x="410" y="152" fill="currentColor" font-family="monospace">② adc  x1, x0, x2   x0 = a_hi 그대로</text>
<text x="400" y="184" fill="currentColor">hi = 1 + 2 + 1 = 4</text>
<g fill="currentColor">
<text x="20" y="220">판정: 템플릿에서 출력을 쓰는 줄 "아래" 에 입력을 읽는 줄이 있으면 &amp;. 마지막에 쓰는 출력(hi)은 필요 없다.</text>
<text x="20" y="244">"+r" (읽고 쓰기) 오퍼랜드는 원래 자기 레지스터를 쓰므로 &amp; 대상이 아니다. 대신 다른 입력과는 안 겹친다.</text>
</g>
</svg>
```

**캐리 체인은 asm 하나에.** `adds` 와 `adc` 를 두 asm 문장으로 쪼개면, 둘 사이에 컴파일러가 무엇을 넣어도 막을 방법이 없다. `"cc"` 는 "이 asm 이 플래그를 **망가뜨린다**" 는 선언이다. "다음 asm 이 이 플래그를 **읽는다**" 는 약속은 아니다. 컴파일러에게 플래그는 asm 문장 사이에 살아 있는 값이 아니다. 다배정밀 덧셈(bignum)을 limb 마다 asm 한 줄로 쓴 `bad_bn_add` 의 실제 clang 출력:

```text
	adds	x8, x8, x9        ; asm 1: limb 0
	str	x8, [x0]
	cmp	x3, #2            ; ← 루프 진입 검사: C 를 덮음 (n=3 → C=1)
	...
LBB1_2:
	ldr	x12, [x10], #8
	ldr	x13, [x9], #8
	adcs	x12, x12, x13     ; asm 2: limb i — C 는 cmp/subs 가 남긴 값
	str	x12, [x11], #8
	subs	x8, x8, #1        ; ← 루프 카운터: 또 C 를 덮음
	b.ne	LBB1_2
```

`{1,0,0} + {1,0,0}` 이 `{2,1,1}` 이 됐다 (결과표 2행). 고치는 법은 루프까지 asm 하나에 넣고, **플래그를 안 건드리는** `sub`(s 없음)와 `cbnz` 로 도는 것이다 (연습 3).

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="22" fill="currentColor" font-weight="bold">C 플래그는 asm 문장 경계를 못 건넌다 — 사이에 낀 컴파일러 코드가 NZCV 를 쓴다</text>
<rect x="20" y="45" width="150" height="34" rx="4" class="f-acc" stroke="currentColor"/><text x="95" y="67" fill="currentColor" text-anchor="middle" font-family="monospace">asm: adds</text>
<rect x="200" y="45" width="150" height="34" rx="4" class="f-warn" stroke="currentColor"/><text x="275" y="67" fill="currentColor" text-anchor="middle" font-family="monospace">cmp x3, #2</text>
<rect x="380" y="45" width="150" height="34" rx="4" class="f-acc" stroke="currentColor"/><text x="455" y="67" fill="currentColor" text-anchor="middle" font-family="monospace">asm: adcs</text>
<rect x="560" y="45" width="150" height="34" rx="4" class="f-warn" stroke="currentColor"/><text x="635" y="67" fill="currentColor" text-anchor="middle" font-family="monospace">subs x8, #1</text>
<path d="M170 62 L200 62" stroke="currentColor"/><path d="M350 62 L380 62" stroke="currentColor"/><path d="M530 62 L560 62" stroke="currentColor"/>
<text x="95" y="100" fill="currentColor" text-anchor="middle">C = 진짜 캐리</text>
<text x="275" y="100" fill="currentColor" text-anchor="middle">C = (n ≥ 2) 로 덮임</text>
<text x="455" y="100" fill="currentColor" text-anchor="middle">엉뚱한 C 를 더함</text>
<text x="635" y="100" fill="currentColor" text-anchor="middle">다음 limb 도 오염</text>
<rect x="20" y="125" width="690" height="34" rx="4" class="f-ok" stroke="currentColor"/>
<text x="30" y="147" fill="currentColor" font-family="monospace">asm 하나: cmn xzr,xzr | 1: ldr; ldr; adcs; str; sub n,n,#1; cbnz n,1b | cset c, cs</text>
<g fill="currentColor">
<text x="20" y="190">sub (s 없음), cbnz, ldr/str post-index 는 NZCV 를 읽지도 쓰지도 않는다 → 캐리가 루프 내내 산다.</text>
<text x="20" y="214">캐리를 C 로 돌려받고 싶으면 cset, 또는 플래그 출력 오퍼랜드 "=@cccs"(c) (Q3).</text>
</g>
</svg>
```

**카운터 읽기.** `mrs %x[v], cntvct_el0` 은 입력이 없다. `volatile` 이 없으면 두 번 읽기가 하나로 합쳐진다 (버그 7). `demo_timer.c` 로 이 맥에서 실측한 값:

```text
CNTFRQ_EL0 = 1000000000 Hz
hw.tbfrequency = 24000000 Hz, mach_timebase_info = 125/3 ns per tick
CNTVCT increments: 42 41 42 42 41 42 42 41 42 42 41 42 42 41 42 42 41 42 42 41 42 42 41 42
sleep 50ms: dCNTVCT = 55014000 ticks = 55.014 ms (via CNTFRQ), clock_gettime = 55.015 ms
            dmach   = 1320341 ticks, dCNTVCT/dmach = 41.6665
EL0 access probe (fork per register):
  cntvct_el0   readable: 0x19bc9be5936f
  cntpct_el0   readable: 0x19bc9be5f5cf
  pmccntr_el0  TRAP: signal 4 (SIGILL)
```

| 관찰 | 해석 (확인한 것 / 추정) |
|---|---|
| CNTFRQ = **1 GHz** (M6, macOS 27.2) | 흔히 알려진 "Apple 은 24 MHz" 와 다르다. Armv8.6 부터 시스템 카운터 주파수를 1 GHz 로 고정한다 (FEAT_ECV 와 함께 도입, 문서 기준·확인 권장). 이 맥은 `hw.optional.arm.FEAT_ECV: 1` |
| 증분이 41/42/42 반복 | 평균 41.667 = 1000/24 → **실제 해상도는 여전히 24 MHz** (41.67 ns). 24 MHz 카운터를 1 GHz 단위로 환산해 보여 주는 것으로 보인다 |
| dCNTVCT / dmach = 41.6665 | `mach_absolute_time` 은 24 MHz 단위(timebase 125/3 ns). 절대값은 달라서(오프셋) 차이만 비교 가능 |
| VM(같은 맥, Linux 게스트)에서는 CNTFRQ = 24000000, 증분 1 | 게스트에는 24 MHz 원본이 보인다 (아래 VM 출력). 그래서 **CNTFRQ 를 하드코딩하지 말고 읽어야** 한다 |
| PMCCNTR_EL0 → SIGILL | 사이클 카운터는 EL0 에 안 열려 있다 (macOS, Linux 게스트 둘 다). 사이클 단위 측정은 ARM-M03 의 보정 방법을 쓴다 |
| CNTPCT_EL0: macOS 읽힘 / Linux 게스트 SIGILL | 물리 카운터 EL0 접근은 OS 가 정한다 (CNTKCTL_EL1.EL0PCTEN, 문서 기준) |

VM 의 `demo_timer` 실제 출력: `CNTFRQ_EL0 = 24000000 Hz`, `CNTVCT increments: 1 1 1 …`, `cntpct_el0 TRAP: signal 4 (SIGILL)`.

`mrs cntvct` 는 앞 명령들이 끝나기 전에 미리 실행될 수 있다 (투기적 실행). 그래서 짧은 구간을 정확히 재려면 앞에 `isb` 를 둔다. 이 주제와 latency 측정은 ARM-M03 이 맡는다. 이 레슨의 폴링(1 ms)에는 영향이 없다.

### Part B — 버그 7종 실측 (`demo_bugs.c`)

<!-- code: demo_bugs.c -->

같은 소스를 세 컴파일러로 -O2 빌드해 실행한 실제 결과 (`BUG` = 틀린 값 재현, `ok` = 이 컴파일러에선 우연히 맞음):

| # | 빠뜨린 것 | Apple clang 21 (맥) | gcc 15.2 (VM) | clang 21.1.8 (VM) | 틀린 값 |
|---|---|---|---|---|---|
| 1 | `&` (add128) | BUG | BUG | BUG | hi = 3 (정답 4) |
| 2 | 캐리 체인 쪼갬 | BUG | BUG | BUG | `{2,1,1}` (정답 `{2,0,0}`) |
| 3a | `"cc"` (asm 앞 `subs`) | BUG | BUG | BUG | 0 (정답 2) |
| 3b | `"cc"` (asm 앞 비교 `a<b`) | ok | BUG | BUG | 0 (정답 6) |
| 4 | `"memory"` (asm 이 포인터로 씀) | BUG | BUG | BUG | 5 (정답 0) |
| 5a | `"memory"` (asm 이 포인터로 읽음, volatile) | ok | BUG | ok | `0xb4b4b4b4` (정답 21) |
| 5b | 같은 것, volatile 아님 | BUG | BUG | BUG | `0xb4b4b4b4` |
| 6a | uint32_t 를 `%x` 로 | BUG | BUG | BUG | `0xffffffff00001010` |
| 6b | uint64_t 결과를 `%w` 로 | BUG | BUG | BUG | `0x80000000` |
| 7a | `volatile` (두 번 읽기) | BUG | ok | BUG | 경과 0 |
| 7b | `volatile` (데드라인 폴링) | BUG | BUG | BUG | 데드라인 영원히 안 옴 (cap 에서 멈춤) |
| | 합계 | 10/12 | 11/12 | 11/12 | |

**3. `"cc"` — clang 은 정말 플래그를 asm 너머로 살려 둔다.** 처음엔 "clang 은 asm 을 늘 플래그 clobber 로 본다" 는 소문을 의심했다. LLVM IR 을 확인했다: `"cc"` 를 적으면 `"=r,0,~{cc}"`, 안 적으면 `"=r,0"`. 기계어 단계(MIR)에서도 `"cc"` 가 있을 때만 `implicit-def early-clobber $nzcv` 가 붙는다. 그러니 빠뜨리면 실제로 틀린다:

```text
_bad_cc_clang:                   ; d = x - b; asm("adds y,y,#1"); return d == 0 ? y : d;
	subs	x8, x0, x1        ; d 와 플래그(Z = d==0) 를 한 번에
	adds	x2, x2, #1        ; asm — Z 를 덮었는데 컴파일러는 모름
	csel	x0, x2, x8, eq    ; asm 의 Z 로 고름: y=-1 → 0 → Z=1 → y 반환
	ret
_good_cc:                        ; "cc" 추가
	adds	x2, x2, #1        ; asm 을 먼저
	subs	x8, x0, x1        ; 비교를 asm 뒤로 옮겨 다시 만든다
	csel	x0, x2, x8, eq
```

gcc 는 더 평범한 코드에서 걸렸다 (`c = a < b; asm(adds); return c ? x : 0;` → gcc 출력 `cmp x0, x1` / asm `adds x2, x2, #1` / `csel x0, x2, xzr, lt`). 3b 를 Apple clang 은 우연히 비교를 asm 뒤에 두어 피했다. 하지만 VM 의 clang 21.1.8 은 걸렸다. **같은 clang 계열이어도 버전에 따라 다르다.** 규칙은 하나다: 플래그를 바꾸는 명령(`adds`, `subs`, `cmp`, `cmn`, `tst`, `ands`, `adcs`, `ccmp`…)이 있으면 `"cc"`.

**4·5. `"memory"` — 쓰는 쪽과 읽는 쪽.** 4번(`p[0]=5; asm("stp xzr,xzr,[p]"); return p[0];`)은 세 컴파일러 모두 `mov x0, x8` (=5) 로 asm 앞의 값을 그대로 돌려줬다. 5번은 펌웨어에서 더 위험하다. DMA 엔진에 디스크립터 **주소만** 레지스터로 넘기는 모양이다. gcc 는 `volatile` 이 있어도 디스크립터를 채우는 저장을 **통째로 지웠다**:

```text
bad_dev_read_volatile (gcc 15 -O2, 스택 가드 줄 생략):
	mov	x1, 0             ; (스택 가드 레지스터 정리)
	mov	x0, sp            ; 주소는 넘기는데
	ldp w0, w1, [x0]          ; asm: 장치가 읽는다 — d[0]=x, d[1]=x+1 저장이 없다!
	add	w0, w0, w1        ; → 스택 쓰레기 0x5a5a5a5a + 0x5a5a5a5a = 0xb4b4b4b4
```

Apple clang 은 `volatile` asm 을 "아무 메모리나 읽고 쓸 수 있음" 으로 보수적으로 다뤄 5a 를 피했다 (IR 에 메모리 속성 없음 = 전부 가능). 하지만 이건 문서가 보장하는 동작이 아니다. `volatile` 을 빼면(5b) clang 도 저장을 지웠다. 고치는 법: `"memory"` clobber, 또는 실제로 읽는 범위를 메모리 입력 오퍼랜드로 적는다 (`"m"(*(const uint32_t (*)[2])d)`).

**6. 폭 수정자.** `uint32_t off = (uint32_t)big;` 는 명령을 만들지 않는다. AArch64 에서 32비트 값은 w 레지스터 "보기" 일 뿐이고, x 레지스터의 위 32비트는 정해지지 않는다 (AAPCS64 기준, ARM-B01). 그래서 `"add %0, %1, %2"` 는 `add x0, x0, x1` 로 `big` 의 위 32비트를 그대로 더했다. clang 은 이걸 **경고**로 잡는다 (gcc 15 는 경고 없음):

```text
demo_bugs.c:71:57: warning: value size does not match register size specified by the constraint and modifier [-Wasm-operand-widths]
demo_bugs.c:71:26: note: use constraint modifier "w"
```

고친 판 `add %x[r], %x[b], %w[o], uxtw` 는 `add x0, x0, w1, uxtw` 한 명령이다. 반대 방향(64비트 결과에 `rbit %w0`)은 **경고가 없다.** 그래서 `rbit64(1)` 이 `0x80000000` 이 됐다. `-Werror` 를 믿고 넘어가면 안 된다.

**7. `volatile`.** 출력만 있는 비-volatile asm 은 컴파일러에게 "같은 입력 → 같은 출력" 인 순수 함수다. Apple clang 은 `t1 - t0` 의 두 읽기를 합쳐 **`mov x0, #0`** 만 남겼다. 폴링 루프에서는 `mrs` 를 루프 밖으로 빼 버렸다:

```text
_bad_poll:                       ; while (cnt_nv() < deadline && spins < cap) spins++;
	mrs	x8, CNTVCT_EL0    ; 한 번만 읽고
	add	x9, x8, x0
LBB11_1:
	add	x0, x0, #1
	cmp	x8, x9            ; 루프 안에서 같은 x8 을 계속 비교 → 데드라인 영원히 안 옴
	...
_good_poll (volatile):
LBB2_1:
	add	x0, x0, #1
	mrs	x9, CNTVCT_EL0    ; 매 반복 다시 읽음
	cmp	x9, x8
```

실제 펌웨어였다면 이 타임아웃 루프는 **영원히** 돈다 (데모는 2억 회 cap 이 막았다). gcc 는 7a 를 우연히 피했지만 7b 에서는 걸렸다.

**제약이 무엇으로 찍히나 (`demo_constraints.c`)** — 실제 출력:

| 소스 | Apple clang 21 | gcc 15 |
|---|---|---|
| `"I"(0x1000)` | `add x0, x0, #1, lsl #12` | `add x0, x0, 4096` |
| `"K"(0xFF00FF00)` | `and w0, w0, #0xff00ff00` | `and w0, w0, -16711936` |
| `"rZ"(0)` + `%w1` | `mov w8, #0` / `str w8, [x0]` | `str wzr, [x0]` |
| `"m"(p[3])` | `add x8, x0, #24` / `ldr x0, [x8]` | `ldr x0, [x0, 24]` |
| `"Q"(p[3])` + `ldar` | `add x8, x0, #24` / `ldar x0, [x8]` | `add x0, x0, 24` / `ldar x0, [x0]` |
| `"m"(p[3])` + `ldar` (`-DERR_M`) | 어셈블됨 (clang 은 `m` 에 오프셋을 안 붙임) | `Error: the optional immediate offset can only be 0 at operand 2 -- 'ldar x0,[x0,24]'` |
| `"w"` 수정자 없이 `fmadd %0,…` (`-DERR_W`) | `error: invalid operand for instruction` / `fmadd v0, v0, v1, v2` | `Error: expected a scalar SIMD or floating-point register at operand 1 -- 'fmadd v0,v0,v1,v2'` |
| `"I"(4097)`, `"K"(0x12345678)`, `"L"(0)` | `error: value out of range for constraint 'I'` ('K', 'L' 도 같은 꼴) | 넷 다 `warning: 'asm' operand 2 probably does not match constraints`, 첫 함수(`bad_I`)에서 `error: impossible constraint in 'asm'` |
| `"I"(k)` (변수) | `error: constraint 'I' expects an integer constant expression` | 위와 같은 경고 |

교훈: `"m"` 은 clang 에선 우연히 `[Xn]` 이지만 gcc 는 오프셋을 붙인다 → 오프셋을 못 받는 명령(`ldar`, `ldxr`, `stlr`)에는 **`"Q"`**. `L`(0) 이 에러인 것은 0 과 ~0 이 논리 즉시값으로 인코딩되지 않기 때문이다 (ARM-B03).

**FP: `"w"` + `%d`.** `fmadd_d(1+2^-30, 1-2^-30, -1)` 은 정확한 곱 `1 - 2^-60` 을 반올림 없이 더해 **`-2^-60`** 을 준다. 따로 곱하고 더하면 곱이 1.0 으로 반올림돼 0 이 된다. 테스트는 이 값과, 랜덤 2만 쌍에서 libm `fma()`/`fmaf()` 와의 **비트 단위 일치**를 본다.

### Part C — 드라이버 헬퍼

```c
static inline void mmio_write32(volatile uint32_t *addr, uint32_t v) {
    __asm__ volatile("str %w[v], %[m]" : [m] "=Q"(*addr) : [v] "rZ"(v));
}
static inline void dma_wmb(void) { __asm__ volatile("dmb oshst" : : : "memory"); }
```

- **`"=Q"(*addr)` 출력**: "이 주소 하나를 쓴다" 를 정확히 알린다. 그래서 같은 레지스터를 두 번 쓰면 두 번 다 남는다 (volatile).
- **`"rZ"`**: 0 을 쓸 때 레지스터를 낭비하지 않고 `wzr` 를 쓰게 허용한다. gcc 는 실제로 `str wzr, [x0]` 를 냈다. Apple clang 은 `Z` 를 쓰지 않고 `mov w8, #0` 을 했다 (위 표). Linux arm64 의 `__raw_writel` 도 `"rZ"` 를 쓴다 (커널 소스 기억 기준, 확인 권장).
- **`dma_wmb` = 두 겹의 배리어**: `dmb oshst` 는 **하드웨어** 에게 "앞의 저장이 Outer Shareable 도메인(DMA 마스터 포함)에 먼저 보이게" 를 지시한다. `"memory"` 는 **컴파일러** 에게 "저장을 이 줄 너머로 옮기거나 지우지 마라" 를 지시한다. 둘 중 하나만 있으면 반쪽이다. DMB 의 도메인·종류 선택은 ARM-M05 가 다룬다.

`nic_tx_post` 의 실제 clang 기계어 (`objdump -d`, 순서가 소스 그대로 유지됨):

```text
       c:      	str	x2, [x9]                ; ring[idx].addr
      10:      	mov	w10, #0x1
      14:      	stp	w3, w10, [x9, #0x8]     ; ring[idx].len, .flags = OWN
      18:      	dmb	oshst                   ; dma_wmb()
      1c:      	ldr	w9, [x1]                ; *sw_tail — "memory" 때문에 다시 읽는다
      20:      	add	w9, w9, #0x1
      24:      	str	w9, [x1]
      28:      	add	x10, x0, #0x80          ; &n->tail_db (offsetof = 128)
      2c:      	str	w9, [x10]               ; 도어벨 (mmio_write32)
```

gcc 15 의 출력도 같은 순서였다 (`stp w3, w2, [x6, 8]` → `dmb oshst` → … → `str w2, [x1]`). `1c` 의 재로드는 `"memory"` 의 비용이다. 컴파일러가 레지스터에 들고 있던 `*sw_tail` 을 믿지 않고 다시 읽는다. 그래서 `"memory"` 는 정확하지만 무겁다. 핫 루프의 단순 레지스터 연산 asm 에 습관처럼 붙이지 않는다.

**배리어가 바이너리에 있는지 테스트로 보기.** 단일 스레드 테스트로는 재배치를 관찰할 수 없다 (장치 모델도 같은 스레드다). 그래서 `code_has_dmb_oshst()` 는 `nic_tx_post` 의 기계어를 `ret`(0xD65F03C0)까지 읽으며 `DMB OSHST` = `0xD50330BF | (CRm=0b0010) << 8` = **0xD50332BF** 를 찾는다. 함수 포인터를 데이터로 읽는 것은 ISO C 밖의 기법이다 (`uintptr_t` 경유, 이 플랫폼의 text 는 읽기 가능). 하지만 "-O2 바이너리에 배리어가 남았나" 를 CI 에서 확인하는 실무 기법과 같다 (objdump + grep).

**데드라인 폴링.** `deadline = read_cntvct() + timeout_ticks`, 매 반복 `mmio_read32(&flags)` 와 `read_cntvct()` 를 읽는다. DONE 을 보면 `dma_rmb()` 다. 장치가 DONE 을 쓰기 전에 쓴 다른 필드(실제 NIC 의 전송 상태·타임스탬프)를 이 뒤에 읽어야 순서가 맞는다. 타임아웃은 `freq / 1000` = 1 ms 다. 맥에서는 1,000,000 tick, VM 에서는 24,000 tick 이다. CNTFRQ 를 읽어 쓰므로 둘 다 맞다. 64비트 카운터는 1 GHz 에서도 약 584년 뒤에야 넘친다. 그래서 `now >= deadline` 비교를 써도 된다. 32비트 SysTick 류에서는 `(int32_t)(now - deadline) >= 0` 꼴이어야 한다 (BM 트랙).

### GCC 와 clang 이 다르게 "봐주는" 것 — 기대지 말 것

| 상황 | Apple clang 21 | gcc 15 |
|---|---|---|
| `volatile` asm, `"memory"` 없음, 포인터로 읽기 | 저장 유지 (보수적) | 저장 **삭제** |
| `"cc"` 없음, asm 앞 비교 | 비교를 asm 뒤로 (우연히 맞음) | 비교를 asm 앞에 → 틀림 |
| 비-volatile `mrs` 두 번 | 합쳐서 0 | 이 모양은 유지 (우연히 맞음) |
| `"rZ"` 에 0 | `mov w8,#0` | `wzr` |
| `"m"` | `[Xn]` 만 | `[Xn, #off]` 도 |
| uint32_t 를 `%x` 로 | `-Wasm-operand-widths` 경고 | 조용함 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/ARM-M02_solution && /tmp/ARM-M02_solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/ARM-M02_sol_asan && /tmp/ARM-M02_sol_asan
```

실제 출력 (맥, 0.14 s):

```text
info: CNTFRQ_EL0 = 1000000000 Hz
info: 2M volatile loads took 764458 ticks
info: timeout path waited 1000000 ticks (>= 1000000)
36/36 passed
```

ASan/UBSan 빌드도 `36/36 passed`, 보고 없음. 같은 solution.c 를 VM 에서 gcc 15 와 clang 21 (`-std=c11 -Wall -Wextra -Werror -O2 … -lm`)로 빌드해도 둘 다 `36/36 passed` 다. 이때 `CNTFRQ_EL0 = 24000000 Hz`, 타임아웃 경로는 각각 41097 / 24000 tick 이었다. 래퍼가 두 컴파일러, 두 카운터 주파수에서 같은 의미로 동작한다는 확인이다.

## 7. 자주 하는 실수

**1. 루프·여러 명령 asm 에서 출력에 `&` 를 안 붙인다.** -O0 에서는 출력이 스택 슬롯이라 멀쩡하다. -O2 에서만 입력 레지스터를 공유해 틀린다.

```c
: [l] "=r"(l), [h] "=r"(h)        /* 틀림: lo 가 a_hi 의 x0 을 받음 → hi 오답 */
: [l] "=&r"(l), [h] "=r"(h)       /* 맞음 */
```

**2. 캐리/플래그를 asm 문장 사이로 넘긴다.** `"cc"` 를 붙여도 안 된다. 그건 "덮는다" 는 뜻일 뿐이다. 체인 전체를 asm 하나에 넣거나, 플래그를 `cset`/`"=@cccs"` 로 꺼내 C 변수로 넘긴다.

**3. `adds`/`cmp` 를 쓰고 `"cc"` 를 안 적는다.** 결과 레지스터만 보면 맞아 보인다. 틀리는 곳은 asm **주변** C 코드의 `csel`/`b.cond` 다 (버그 3, clang·gcc 모두 재현).

**4. 주소만 `"r"` 로 넘기고 메모리를 읽거나 쓴다.** 컴파일러는 그 메모리를 모른다. 쓰면 옛 값을 재사용하고, 읽으면 앞의 저장을 지운다.

```c
__asm__ volatile("stp xzr, xzr, [%x[p]]" : : [p] "r"(p));                 /* 틀림 */
__asm__ volatile("stp xzr, xzr, [%x[p]]" : : [p] "r"(p) : "memory");      /* 맞음 (무거움) */
__asm__("stp xzr, xzr, %[m]" : [m] "=Q"(*(uint64_t (*)[2])p));           /* 맞음 (정밀) */
```

**5. 32비트 값을 수정자 없이 쓴다.** `uint32_t` 오퍼랜드는 `%w`, 32비트 값을 64비트 연산에 섞을 땐 `uxtw`/`sxtw`. 64비트 결과에 `%w` 를 쓰면 경고도 없이 위 32비트가 0 이 된다.

**6. 하드웨어를 읽는 asm 에 `volatile` 을 안 붙인다.** `mrs cntvct`, MMIO `ldr`, `dmb` 는 출력이 같아 보여도 매번 실행돼야 한다. 반대로 `volatile` 은 배리어가 아니다. 주변의 일반 load/store 순서까지 고정하려면 `"memory"` 가 필요하다.

**7. `"w"` 오퍼랜드를 `%0` 으로 쓴다 / 오프셋 못 받는 명령에 `"m"`.** 각각 `v0` (어셈블 에러), `[x0, 24]` (gcc 어셈블 에러)가 찍힌다. `%d`/`%s` 와 `"Q"` 를 쓴다.

## 8. 면접 Q&A

<details>
<summary>Q1. early-clobber(`&`)는 언제 필요하고 언제 필요 없나요?</summary>

출력을 쓴 **뒤에** 아직 읽어야 할 입력이 남아 있으면 필요하다. 컴파일러는 기본으로 "출력은 마지막에 쓴다" 고 가정하고, 출력에 입력 레지스터를 재활용할 수 있다. `adds lo; adc hi, a_hi` 에서 `lo` 가 `a_hi` 의 레지스터를 받는 일이 clang·gcc 모두에서 실제로 일어났다. 필요 없는 경우: 마지막 명령만 쓰는 출력(`hi`), 출력을 쓴 뒤 그 출력만 다시 읽는 경우(`rbit r, x; clz r, r`), `"+r"` 오퍼랜드. 꼬리질문 "그냥 다 `&` 붙이면?" → 맞긴 하지만 레지스터를 하나씩 더 쓴다. 레지스터 압박이 큰 루프에서는 spill 이 생길 수 있다.

</details>

<details>
<summary>Q2. 128비트 덧셈을 `adds` asm 하나, `adc` asm 하나로 나누면 왜 안 되나요? "cc" 를 붙이면요?</summary>

두 asm 사이에 컴파일러가 코드를 넣을 수 있다. 루프 검사 `cmp`, 카운터 `subs` 같은 것이고, 이것들이 C 플래그를 덮는다. 실제로 bignum 루프에서 `cmp x3, #2` 와 `subs x8, x8, #1` 이 끼어 `{1,0,0}+{1,0,0} = {2,1,1}` 이 나왔다. `"cc"` 는 "이 asm 이 플래그를 망가뜨린다" 는 선언이다. "다음 asm 에게 플래그를 넘긴다" 는 수단이 아니다. 컴파일러 모델에서 NZCV 는 asm 문장 사이에 살아 있는 값이 아니다. 해결: 체인 전체(루프 포함)를 한 asm 에. 루프는 플래그를 안 건드리는 `sub` + `cbnz` 로 돈다 (연습 3).

</details>

<details>
<summary>Q3. 캐리나 오버플로 여부를 asm 밖 C 코드로 돌려받는 깔끔한 방법은?</summary>

1. asm 안에서 `cset %w[c], cs` 로 레지스터에 담는다 (연습 1·3).
2. **플래그 출력 오퍼랜드** `"=@cccs"(c)` 를 쓴다: 컴파일러가 직접 `cset w0, hs` 를 만든다. 이 맥의 clang 21 과 VM 의 gcc 15 모두에서 동작을 확인했다 (`adds x8, x0, x1` / `cset w0, hs`). 조건 이름은 `@cc` + `eq/ne/cs/hs/cc/lo/mi/pl/vs/vc/hi/ls/ge/lt/gt/le`.
3. asm goto 로 `b.vs %l[label]` 처럼 바로 분기한다 (연습 5).
4. 사실 이 문제라면 `__builtin_add_overflow` / `unsigned __int128` 이 가장 낫다. 컴파일러가 `adds/adc` 를 직접 내고, 상수 접기와 스케줄링도 해 준다.

</details>

<details>
<summary>Q4. `volatile` 은 정확히 무엇을 보장하나요? 배리어인가요?</summary>

보장하는 것: 그 asm 을 지우지 않고, 두 번을 하나로 합치지 않고, 루프 밖으로 빼지 않는다 (실행 횟수 유지). volatile asm 끼리의 상대 순서도 유지된다. 보장하지 **않는** 것: 주변 일반 load/store 와의 순서. GCC 문서도 "volatile asm 도 다른 코드에 대해 움직일 수 있다" 고 적는다. 실제로 gcc 15 는 volatile asm 앞의 저장을 지웠다. 그래서 배리어 매크로는 `volatile` + `"memory"` 둘 다다 (`__DMB()`, `dma_wmb()`, `barrier()`). 출력이 없는 asm 은 GCC 규칙상 암묵적으로 volatile 이다. 입력이 없고 출력만 있는 `mrs cntvct` 는 volatile 이 없으면 순수 함수 취급이다. clang 은 `t1-t0` 를 `mov x0, #0` 으로 만들었다.

</details>

<details>
<summary>Q5. `"memory"` clobber 와 `"m"`/`"Q"` 메모리 오퍼랜드, 뭘 써야 하나요?</summary>

asm 이 건드리는 메모리를 **정확히 알면** 메모리 오퍼랜드가 낫다. 컴파일러는 그 위치만 동기화하고, 나머지 값은 레지스터에 계속 들고 있는다. 주소 레지스터는 컴파일러가 고른다. **범위를 모르거나**(DMA 엔진이 디스크립터 체인을 따라감), **순서 자체가 목적**이면(배리어) `"memory"` 다. 비용도 있다. `nic_tx_post` 에서 `"memory"` 때문에 `*sw_tail` 을 다시 `ldr` 하는 것을 실제로 봤다. 오프셋 못 받는 명령(`ldar`, `ldxr`, `stlr`)에는 `"Q"` 를 쓴다. `"m"` 은 gcc 가 `[x0, 24]` 를 찍어 어셈블 에러가 났다.

</details>

<details>
<summary>Q6. Apple Silicon 에서 CNTVCT 로 시간을 재면 해상도가 얼마죠? 사이클은 못 재나요?</summary>

이 M6 맥에서 실측: `CNTFRQ_EL0 = 1 GHz` 로 읽힌다. 그러나 값은 41, 42, 42 씩 뛴다. 즉 실제 해상도는 24 MHz = 41.67 ns 이고, 1 GHz 단위로 환산돼 보일 뿐이다. 같은 맥의 Linux VM 게스트에서는 CNTFRQ 가 24 MHz, 증분은 1 이었다. 1 GHz 표기는 Armv8.6 의 "카운터 1 GHz 고정" 규칙과 맞는다 (문서 기준). 그래서 주파수는 하드코딩하지 말고 `CNTFRQ_EL0` 를 읽는다. 사이클 카운터 `PMCCNTR_EL0` 는 EL0 에서 SIGILL 이었다 (macOS, Linux 게스트 둘 다). 사이클 단위가 필요하면 N 회 반복으로 해상도를 메우고, 의존 체인으로 보정한다 (ARM-M03). 꼬리: x86 은 `rdtsc` (+`lfence`) 가 유저 모드에서 열려 있고, 불변 TSC 라 주파수 변화와 무관하다. ARM 의 대응물은 CNTVCT + ISB 다.

</details>

<details>
<summary>Q7. Linux 의 `writel()` 은 inline asm 으로 어떻게 생겼나요? 왜 OSH 인가요?</summary>

기억 기준(arch/arm64/include/asm/io.h, 확인 권장): `writel(v, a)` = `__iowmb()` (= `dma_wmb()` = `dmb oshst` + `"memory"`) 다음 `__raw_writel` (`str %w0, [%1]` 에 `"rZ"(val)`). 앞의 DMA 버퍼 쓰기가 장치보다 먼저 보이게 한 뒤 도어벨을 친다. 이 레슨의 `nic_tx_post` 가 같은 구조다. `writel_relaxed` 는 배리어 없이 raw 쓰기만 한다. OSH(Outer Shareable)인 이유: DMA 마스터는 CPU 클러스터(Inner Shareable) 바깥에 있을 수 있다. 그래서 그 관찰자까지 포함하는 도메인이 필요하다. 레지스터 자체가 Device 메모리라 MMIO 끼리의 순서는 Device-nGnRE 속성이 지켜 준다. 일반 메모리(DMA 버퍼)와 MMIO 사이의 순서가 배리어의 일이다.

</details>

<details>
<summary>Q8. inline asm 대신 intrinsic/builtin 을 써야 할 때는요?</summary>

계산용이면 거의 항상 builtin/ACLE 가 낫다. `__rbitll`, `__clzll`, `__arm_rsr64("cntvct_el0")` (`<arm_acle.h>`) 를 이 맥에서 빌드하면 각각 `rbit`, `clz`, `mrs x10, CNTVCT_EL0` 한 줄씩이 나왔다. 컴파일러가 의미를 알기 때문에 상수 접기, 스케줄링, 벡터화를 할 수 있다. 제약 실수가 생길 여지도 없다. `__arm_rsr64` 는 volatile 의미를 이미 갖는다. inline asm 이 필요한 경우: 컴파일러가 모르는 명령이나 시스템 레지스터, 정확한 명령 순서가 의미인 코드(배타 루프 ARM-M04, 배리어 ARM-M05, 예외 진입 스텁), 특정 인코딩을 강제해야 할 때. Cortex-M 의 CMSIS `__get_PRIMASK()` (`mrs %0, primask` + volatile), `__DMB()` (`dmb 0xF` + `"memory"`) 도 이 레슨의 규칙을 그대로 따른다 (ARM-M10).

</details>

<details>
<summary>Q9. 디버그 빌드에선 되는데 릴리스에서만 깨지는 inline asm, 어떻게 찾나요?</summary>

1. `-O2 -S` 로 그 함수의 asm 을 보고, asm 블록 **앞뒤** 명령을 읽는다. 출력 레지스터가 입력과 같은지(`&`), asm 앞의 `subs/cmp` 가 뒤의 `csel/b.cond` 로 이어지는지(`"cc"`), asm 앞 저장이 사라졌거나 뒤에서 옛 값을 쓰는지(`"memory"`), `mrs` 가 루프 밖으로 나갔는지(`volatile`) 본다.
2. clang 의 `-Wasm-operand-widths` 를 켠다.
3. 두 컴파일러(gcc/clang)로 돌려 본다. 이 레슨에서 같은 버그가 컴파일러·버전마다 나왔다 사라졌다.
4. 가능하면 asm 을 builtin 으로 바꿔 결과를 비교한다.

ASan/TSan 은 asm 내부를 계측하지 않는다는 점도 기억한다 (ARM-M04 의 TSan 거짓 양성/음성).

</details>

## 9. 추가 연습

### 연습 1 — `sub128` 과 빌림(borrow)

`SUBS`/`SBCS` 로 128비트 뺄셈을 하고, 빌림이 생겼으면 1 을 돌려라. ARM 의 C 플래그가 뺄셈에서 무엇을 뜻하는지 주의.

<details>
<summary>답 보기</summary>

`subs l, al, bl; sbcs h, ah, bh; cset bw, lo`. ARM 뺄셈의 C 는 "**빌림 없음**" 이다 (x86 CF 와 반대, ARCH-B02). 그래서 빌림 = C 클리어 = `lo`(`cc`) 조건이다. `sbc` 도 `Rn - Rm - NOT(C)` 로 이 규칙을 따른다. 출력 `l` 은 `sbcs` 가 `ah/bh` 를 읽기 전에 쓰이므로 `=&r` 이다. `h` 는 그 뒤 `cset` 이 입력을 안 읽으니 `&` 가 필요 없지만, practice.c 에선 안전하게 붙였다. 테스트: `0 - 1` → 빌림 1, 결과 `2^128-1`. `2^64 - 1`, 같은 값 빼기 → 0. 랜덤 10만 개(4개 중 1개는 상위 워드가 같아 하위 빌림이 결과를 가름)를 `unsigned __int128` 과 대조한다.

</details>

### 연습 2 — `mul_wide`: MUL + UMULH

64×64→128 곱을 asm 하나로. `&` 가 필요한 출력은?

<details>
<summary>답 보기</summary>

`mul l, a, b; umulh h, a, b` 에서 `mul` 이 `l` 을 쓴 뒤 `umulh` 가 `a`, `b` 를 다시 읽는다 → `l` 은 `=&r`. `&` 를 빼면 clang 과 gcc 모두 실제로 `mul x0, x0, x1` / `umulh x1, x0, x1` 을 냈다. `a = 0x8000000000000001, b = 2` 에서 정답 `hi = 1` 대신 **`hi = 0`** 이 나왔다. 이건 **입력에 따라 우연히 맞기도** 한다. `a = ~0, b = 3` 에서는 버그 판도 `hi = 2` 로 정답이었다. 랜덤 테스트가 필요한 이유다. 부호 있는 판은 `smulh` 다. 하위 64비트는 부호와 무관하게 같다 (ARM-B07). 테스트: `~0 × ~0`, `INT64_MIN²`, `-1 × 1`, 랜덤 10만 개를 `(unsigned) __int128` 과 대조한다.

</details>

### 연습 3 — `bn_add`: 캐리 체인을 루프째 asm 하나에

`bad_bn_add` (버그 2) 를 고쳐라. n 워드 덧셈, 반환은 최종 캐리, n = 0 허용.

<details>
<summary>답 보기</summary>

`cmn xzr, xzr` (0+0 → C=0) 으로 시작하고, `cbz n, 2f` 로 n=0 을 거른다. 루프는 `ldr t0,[a],#8; ldr t1,[b],#8; adcs t0,t0,t1; str t0,[r],#8; sub n,n,#1; cbnz n,1b` 다. `sub`(s 없음)와 `cbnz` 는 NZCV 를 건드리지 않아 캐리가 루프 내내 산다. 끝에서 `cset c, cs` 를 한다. 제약: 포인터와 n 은 asm 이 바꾸므로 `"+r"`, 임시 `t0/t1` 은 `=&r`, 메모리를 쓰므로 `"memory"`, 플래그로 `"cc"`. 테스트: 9 워드 전부 `~0` + 1 → 캐리가 끝까지 전파돼 결과 0, 반환 1. n=0 은 아무것도 안 씀. 길이 0..8 × 2만 회(절반은 캐리가 자주 나는 `~0` 근처 값)를 C 기준 구현과 대조한다.

</details>

### 연습 4 — `popcount64`: 스크래치 SIMD 레지스터

GPR 값을 SIMD 로 옮겨 `CNT` + `ADDV` 로 비트 수를 세라. 스크래치 v 레지스터는 어떻게 얻나?

<details>
<summary>답 보기</summary>

아무 FP 타입 변수(`double v`)를 `"=&w"` 출력으로 선언하면 컴파일러가 v 레지스터 하나를 빌려준다. 템플릿: `fmov %d[v], %x[x]; cnt %[v].8b, %[v].8b; addv %b[v], %[v].8b; fmov %w[r], %s[v]`. 수정자 없는 `%[v]` 는 `v0` 으로 찍히므로 `.8b` 를 붙여 배열 표기를 만든다. `%b`/`%s` 는 같은 레지스터의 8/32비트 스칼라 이름이다. `v` 는 `r` 을 쓰기 전에 쓰이고 입력 `x` 는 첫 줄에서 다 읽으므로, `&` 는 엄밀히는 없어도 된다. 다만 입력(GPR)과 출력(v)의 레지스터 종류가 달라 겹칠 수도 없다. NEON 자체는 ARM-M06 이 맡는다. 테스트: 0, `~0`, 양 끝 비트, 랜덤 10만 개를 C 루프와 대조한다.

</details>

### 연습 5 — `add_ovf_s64`: asm goto 로 B.VS

부호 있는 덧셈 오버플로를 asm 안에서 `b.vs` 로 C 라벨에 바로 분기하라.

<details>
<summary>답 보기</summary>

`__asm__ goto("adds %x[s], %x[a], %x[b]\n\tb.vs %l[ovf]" : [s] "=r"(s) : [a] "r"(a), [b] "r"(b) : "cc" : ovf);` 다음 `*res = s; return false; ovf: return true;`. 출력이 있는 asm goto 는 clang ≥ 11, gcc ≥ 11 기능이다 (문서 기준). 출력 값은 fallthrough 경로에서만 믿는다. 이 맥 clang 21, VM gcc 15 에서 빌드되고 통과했다. 테스트: `INT64_MAX+1`, `INT64_MIN+(-1)` → true. `INT64_MIN+INT64_MAX = -1` → false. 랜덤 10만 개를 `__builtin_add_overflow` 와 대조한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/ARM-M02_practice && /tmp/ARM-M02_practice      # 17/17 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o /tmp/ARM-M02_pr_asan && /tmp/ARM-M02_pr_asan   # 17/17, 보고 없음
# VM: gcc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr                                      # 17/17 passed
```

## 10. 핵심 정리

- 컴파일러는 asm 템플릿을 읽지 않는다. 출력·입력·clobber 에 **적은 것만** 안다. 적지 않은 효과(플래그, 메모리, 실행 횟수)는 없는 것으로 친다.
- 출력을 쓴 뒤 입력을 또 읽으면 `=&r`. 플래그를 바꾸면 `"cc"`. 주소만 넘기고 메모리를 읽거나 쓰면 `"memory"` (또는 정확한 `"Q"`/`"m"` 오퍼랜드). 하드웨어를 읽거나 순서가 의미면 `volatile`.
- 캐리 체인은 asm **하나** 안에. 루프는 `sub` + `cbnz` 로 돈다. 캐리를 C 로 받으려면 `cset` 또는 `"=@cccs"`.
- 폭: `uint32_t` 는 `%w` (+`uxtw`), FP 는 `"w"` + `%s`/`%d`. 오프셋 못 받는 명령은 `"Q"`. 즉시값은 `I`/`K`/`L`, 범위 밖이면 컴파일 에러.
- 실측 7종 버그 중 10~11개 판이 -O2 에서 틀린 값을 냈다. 어떤 판이 "우연히 맞는지" 는 컴파일러·버전마다 달랐다. -O0 통과, 한 컴파일러 통과는 증거가 아니다.
- DMA 순서 = 디스크립터 → `dmb oshst` + `"memory"` → 도어벨. 배리어가 바이너리에 있는지 기계어로 확인할 수 있다 (`0xD50332BF`).
- M6 맥: CNTFRQ = 1 GHz 로 읽히지만 실제 증분 41/42 (24 MHz). VM 게스트는 24 MHz. PMCCNTR 는 EL0 에서 SIGILL. 주파수는 항상 읽어서 쓴다.

## 참고자료

- GCC 문서 — Extended Asm (출력/입력/clobber, early-clobber `&`, `volatile`, asm goto, flag output operands `=@cc`): <https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html>; Machine Constraints → AArch64 family (`w x y I J K L M N Q Z Ump`), AArch64 operand modifiers: <https://gcc.gnu.org/onlinedocs/gcc/Machine-Constraints.html>
- Clang Language Extensions — inline assembly / asm goto with outputs: <https://clang.llvm.org/docs/LanguageExtensions.html>; LLVM LangRef "Inline Assembler Expressions" (constraint string, `~{cc}`, `~{memory}`, `sideeffect`): <https://llvm.org/docs/LangRef.html#inline-assembler-expressions>
- Arm Architecture Reference Manual for A-profile (DDI 0487) — RBIT, CLZ, ADDS/ADC/SBC/SBCS, CSET, ADD (extended register), FMADD, DMB (option encodings OSHST/OSHLD), MRS; Generic Timer 장 (CNTVCT_EL0, CNTFRQ_EL0, CNTKCTL_EL1 의 EL0 접근 제어, FEAT_ECV): <https://developer.arm.com/documentation/ddi0487/latest/>
- Arm C Language Extensions (ACLE) — `__rbit`, `__clz`, `__arm_rsr64`: <https://arm-software.github.io/acle/main/acle.html>
- Apple — Writing ARM64 code for Apple platforms (ABI, 32비트 인자 상위 비트): <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>; `mach_absolute_time` / `mach_timebase_info`: <https://developer.apple.com/documentation/kernel/1462446-mach_absolute_time>
- Linux kernel — `arch/arm64/include/asm/io.h` (`__raw_writel`, `__iowmb`), `arch/arm64/include/asm/barrier.h` (`dma_wmb` = `dmb(oshst)`), `Documentation/memory-barriers.txt`: <https://elixir.bootlin.com/linux/latest/source/arch/arm64/include/asm>
- 관련 레슨: ARM-B01 (w/x 레지스터와 ABI), ARM-B03 (논리 즉시값), ARM-B06 (RBIT/CLZ), ARM-B07 (UMULH/SMULH), ARM-B11 (FP 레지스터·FMA), ARM-M04 (LL/SC asm 의 `=&r`·`+Q`·`"memory"`), ARM-M05 (DMB/DSB/ISB), ARM-M03 (카운터 마이크로벤치, 다음 단계), ARM-M10 (Cortex-M PRIMASK asm), ARCH-B02 (NZCV), C-A02 (컴파일러 배리어)
