---
id: ARM-B06
time: 60분
updated: 2026-11-11
---
# ARM-B06 비트 연산 명령: BIC, UBFX, BFI, CLZ, RBIT

## 학습 목표

- C-B04 의 `FIELD_GET` / `FIELD_PREP` / `FIELD_REPLACE` 가 AArch64 에서 각각 **UBFX / UBFIZ / BFI** 명령 하나가 되는 것을 직접 써서 확인하고, 이 셋이 모두 **UBFM / BFM 하나의 인코딩**(immr, imms)이라는 것을 기계어 비트로 읽는다.
- UBFX 는 위치·폭이 **즉시값**만 된다는 제약을 알고, 실행 중에 정해지는 필드는 `LSR`(가변) + `AND` 마스크로 쓴다. 이때 "시프트 양 mod 64" 라는 ISA 규칙이 `width = 64` 경계를 공짜로 처리해 주는(또는 망가뜨리는) 이유를 설명한다.
- `RBIT`, `CLZ`, `REV/REV16` 로 비트 역순·최상위 비트·바이트 교환·**니블 역순**(Rivian 스크린 문제)을 쓰고, `CLZ(0)` 처럼 **C 에선 UB 인데 ISA 에선 정의된** 경계를 테스트로 다룬다.
- 펌웨어 시나리오(NAND 컨트롤러 상태 워드 디코드/패치, 빅 엔디언 SCSI CDB 필드)를 asm 으로 쓰고, clang `-O2` 가 같은 C 코드에 내는 명령과 비교한다.

## 1. 문제

크래시 덤프나 `objdump` 에서 `ubfx w8, w9, #4, #4`, `bfi w0, w1, #8, #3`, `clz`, `rev` 를 보고 **"아, STATE 필드 읽는 코드네 / 빅 엔디언 변환이네"** 라고 바로 읽을 수 있느냐가 펌웨어 면접의 "이 asm 뭐 하는 코드야?" 라운드에서 갈린다. Rivian HM 스크린에서 실제로 나온 "32비트 워드의 니블 순서를 뒤집어라" 도 C 로 풀고 나면 "ARM 에선 몇 명령이면 돼?" 가 꼬리질문이다.

C-B01/C-B04 에서 C 로 하던 일(마스크·시프트로 필드 읽고 쓰기)을 이번엔 **ARM 전용 비트 명령**으로 한다. x86 은 마스크+시프트 두세 명령이 필요한 일을 AArch64 는 비트필드 명령 하나로 끝낸다. SSD 컨트롤러 펌웨어(Cortex-R/A 코어), Cortex-M3/M4 MCU(Thumb-2 에도 같은 이름의 `UBFX/BFI/CLZ/RBIT/REV` 가 있다), Apple/NVIDIA AArch64 SoC 모두 레지스터 필드를 이 명령들로 다룬다.

| 묶음 | 명령 | C 로 쓰면 | 이 레슨에서 |
|---|---|---|---|
| 논리 | `AND` `ORR` `EOR` `BIC` `ORN` | `&` `\|` `^` `& ~` `\| ~` | `BIC` = clear-bits, `ORN` (연습 6) |
| 시프트/회전 | `LSL` `LSR` `ASR` `ROR` (즉시값/레지스터) | `<<` `>>` (unsigned/signed), 회전 | 가변 필드, `ROR` (연습 3) |
| 비트필드 | `UBFX` `SBFX` `UBFIZ` `BFI` `BFXIL` `BFC` | `FIELD_GET` 부호없음/부호있음, `FIELD_PREP`, `FIELD_REPLACE` | Part A, C |
| 세기 | `CLZ` `CLS` | `__builtin_clz` (0 이면 UB), 정규화 | Part B, 연습 5 |
| 순서 | `RBIT` `REV` `REV16` `REV32` | 비트 역순 루프, `__builtin_bswap*` | Part B, C |

이번 레슨의 가짜 레지스터는 NAND 플래시 컨트롤러(NFC)의 채널 상태 워드 `NFC_STAT` 다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">NFC_STAT (32비트) — 예시 값 0x30E507A1 과 필드별 명령</text>
  <g stroke="currentColor">
    <rect x="40" y="50" width="80" height="34" class="f-mut"/>
    <rect x="120" y="50" width="80" height="34" class="f-mut"/>
    <rect x="200" y="50" width="160" height="34" class="f-warn"/>
    <rect x="360" y="50" width="160" height="34" class="f-acc"/>
    <rect x="520" y="50" width="80" height="34" class="f-ok"/>
    <rect x="600" y="50" width="40" height="34" class="f-mut"/>
    <rect x="640" y="50" width="20" height="34" class="f-acc"/>
    <rect x="660" y="50" width="20" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="80" y="72">VER</text><text x="160" y="72">rsvd</text><text x="280" y="72">TEMP (signed)</text>
    <text x="440" y="72">ERRCNT</text><text x="560" y="72">STATE</text><text x="620" y="72" font-size="12">rsvd</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="45" y="44">31</text><text x="115" y="44">28</text><text x="205" y="44">23</text><text x="355" y="44">16</text>
    <text x="365" y="44">15</text><text x="515" y="44">8</text><text x="525" y="44">7</text><text x="595" y="44">4</text>
    <text x="640" y="104">ECC_FAIL</text><text x="705" y="104">READY</text>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="80" y="128">0x3</text><text x="160" y="128">0x0</text><text x="280" y="128">0xE5 = -27</text>
    <text x="440" y="128">0x07</text><text x="560" y="128">0xA</text><text x="620" y="128">0</text>
    <text x="650" y="128">0</text><text x="670" y="128">1</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="40" y="168">ubfx  w0, w0, #4, #4    → STATE  = 0xA      (FIELD_GET)</text>
    <text x="40" y="190">sbfx  w0, w0, #16, #8   → TEMP   = -27      (부호 확장 필드)</text>
    <text x="40" y="212">ubfiz w0, w0, #4, #4    → (s &amp; 0xF) &lt;&lt; 4    (FIELD_PREP)</text>
    <text x="40" y="234">bfi   w0, w1, #4, #4    → STATE 만 교체     (FIELD_REPLACE)</text>
    <text x="40" y="256">bfc   w0, #8, #8        → ERRCNT = 0        (= bfi w0, wzr, #8, #8)</text>
  </g>
</svg>
```

### Part A — 필드 추출/삽입 (UBFX · SBFX · UBFIZ · BFI + 가변 위치)

- `stat_get_state` (UBFX), `stat_get_temp` (SBFX, 8비트 2의 보수 → `int32_t`), `stat_prep_state` (UBFIZ), `stat_set_state` (BFI) — 각각 명령 **1개 + ret**.
- `field_get_var(reg, lsb, width)` — 64비트, `lsb`/`width` 가 **실행 중에** 정해진다 (1 ≤ width ≤ 64, lsb + width ≤ 64). UBFX 는 즉시값만 받으므로 `LSR`(레지스터) + `AND` 로. **width = 64 도 맞아야 한다.**
- 테스트: C-B04 매크로(압축본)와 무작위 20만 개 대조, `field_get_var` 는 (lsb, width) 2080 쌍 전부 × 8 값.

### Part B — 비트 순서 명령 (RBIT · CLZ · REV) + 일부러 틀린 버전

- `rbit32/rbit64`, `hsb32/hsb64` (최상위 1비트 위치, **x = 0 → -1** 로 정의), `nibrev32` (`0x12345678 → 0x87654321`, REV + 바이트마다 니블 교환), `bswap32` (REV), `bswap16` (REV + LSR).
- `shared_bugs.S` 의 틀린 구현 3개를 테스트가 잡는지 확인: ① `(1 << width) - 1` 마스크 (width = 64 에서 0) ② "니블 역순 = RBIT" 착각 ③ `clz ^ 31` 관용구 (x = 0 에서 63).

### Part C — 펌웨어: 상태 디코드 · 패치 · 빅 엔디언 SCSI CDB

- `stat_decode(stat, &out)` — 필드 6개를 꺼내 구조체 바이트에 `strb`.
- `reg_clrset(reg, clr, set)` — Linux `clrsetbits` 패턴, `BIC + ORR`. `stat_patch(stat, new_state)` — STATE 교체 + ERRCNT = 0 + ECC_FAIL = 0 을 3 명령으로.
- SCSI `READ(10)` CDB(UFS/USB 저장장치가 쓰는 SCSI 명령, 필드는 **빅 엔디언**): `cdb10_lba` (bytes 2..5), `cdb10_len` (bytes 7..8), `cdb10_build`. 버퍼가 **정렬 안 된** 주소(오프셋 0..15)여도 동작하고 앞뒤 가드 바이트를 건드리지 않아야 한다.

## 2. Problem (English)

> **AArch64 bit-manipulation instructions.** Write leaf functions in AArch64 assembly (`.S`, no stack) callable from C. A NAND-controller status word is laid out as `[0] READY, [1] ECC_FAIL, [7:4] STATE, [15:8] ERRCNT, [23:16] TEMP (two's complement), [31:28] VER`.
>
> 1. `uint32_t stat_get_state(uint32_t)`, `int32_t stat_get_temp(uint32_t)`, `uint32_t stat_prep_state(uint32_t)`, `uint32_t stat_set_state(uint32_t stat, uint32_t state)` — one bitfield instruction each. `uint64_t field_get_var(uint64_t reg, unsigned lsb, unsigned width)` with runtime `lsb`/`width` (`1 <= width <= 64`, `lsb + width <= 64`).
> 2. `uint32_t rbit32(uint32_t)`, `uint64_t rbit64(uint64_t)`, `int hsb32(uint32_t)` / `int hsb64(uint64_t)` (index of the highest set bit, `-1` for 0), `uint32_t nibrev32(uint32_t)` (`0x12345678 -> 0x87654321`), `uint32_t bswap32(uint32_t)`, `uint16_t bswap16(uint16_t)`.
> 3. `void stat_decode(uint32_t, nfc_stat_t *)`, `uint32_t reg_clrset(uint32_t reg, uint32_t clr, uint32_t set)`, `uint32_t stat_patch(uint32_t stat, uint32_t new_state)` (set STATE, zero ERRCNT, clear ECC_FAIL, keep everything else), and SCSI READ(10) helpers `uint32_t cdb10_lba(const uint8_t *)`, `uint16_t cdb10_len(const uint8_t *)`, `void cdb10_build(uint8_t *, uint32_t lba, uint16_t len)` — big-endian fields, buffer may be unaligned.
>
> Verify against C reference implementations on random and edge inputs. Examples: `stat_get_temp(0x30E507A1) == -27`, `stat_patch(0x30E507A3, 4) == 0x30E50041`, `hsb64(0) == -1`, `field_get_var(x, 0, 64) == x`.
>
> Follow-ups: Why can't UBFX take a register position? What does `CLZ` return for 0 and why does that matter in C? Who zero-extends a `uint16_t` return value on Apple arm64 vs Linux?

## 3. 시작 코드

빌드는 **C 하네스 + asm 1개 + shared_*.S** 를 한 번에 링크한다. 하네스(`harness.c`)는 starter/solution 이 같이 쓴다.

<!-- code: harness.c -->

<!-- code: starter.S -->

<!-- code: shared_bugs.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S shared_bugs.S -o /tmp/ARM-B06_t && /tmp/ARM-B06_t
```

처음 실행 결과 (실제 출력, PASS 줄은 생략):

```text
FAIL harness.c:184  stat_get_state(0x30E507A1u) == 0xAu
FAIL harness.c:185  stat_get_temp(0x30E507A1u) == -27
… (Part A FAIL 10줄 더)
  lsb=0 w=1 r=ffffffffffffffff asm=0000000000000000 ref=0000000000000001
FAIL harness.c:197  exhaustive_get_var()
FAIL harness.c:200  rbit32(0x00000001u) == 0x80000000u && rbit32(0x12345678u) == 0x1E6A2C48u
… (Part B FAIL 8줄 더)
FAIL harness.c:220  s.ready == 1 && s.ecc_fail == 0 && s.state == 0xA && s.errcnt == 7 && s.temp == -27 && s.ver == 3
… (Part C FAIL 6줄 더)
5/34 passed
```

통과하는 5개는 `shared_bugs.S` 의 "틀린 버전이 틀렸음을 확인" 하는 CHECK 다 (TODO 와 무관). 스텁은 `mov w0, #0` / 그대로 `ret` 이라 메모리를 건드리지 않고 무한 루프도 없다.

## 4. 힌트

<details>
<summary>힌트 1 — UBFX 의 두 숫자</summary>

`ubfx Wd, Wn, #lsb, #width` — "lsb 비트부터 width 비트를 꺼내 맨 아래로, 위는 0". bits 7:4 면 `#4, #4`. 부호 있는 필드는 `sbfx` (꺼낸 필드의 최상위 비트를 위로 복사).

</details>

<details>
<summary>힌트 2 — UBFIZ 와 BFI 의 차이</summary>

둘 다 "아래 width 비트를 lsb 자리로" 옮긴다. `ubfiz` 는 나머지를 **0** 으로 (= `FIELD_PREP`), `bfi` 는 목적지의 나머지 비트를 **그대로 둔다** (= `FIELD_REPLACE`). BFI 는 목적지를 읽기도 하는 명령이라 `Wd` 가 입력 겸 출력이다.

</details>

<details>
<summary>힌트 3 — 가변 위치: 마스크를 어떻게 만들까</summary>

`(1 << width) - 1` 은 width = 64 에서 깨진다 (`LSLV` 는 시프트 양을 **mod 64** 로 쓴다 → `1 << 0`). 반대로 `~0 >> (64 - width)` 를 생각해 보자. `64 - width` 는 `neg` 한 번이면 된다: mod 64 로 보면 `-width ≡ 64 - width`.

</details>

<details>
<summary>힌트 4 — hsb 와 CLZ(0)</summary>

AArch64 `CLZ` 는 입력이 0 이면 32 (w) / 64 (x) 를 돌려준다 — **정의된 동작**이다. `31 - clz` 를 하면 x = 0 일 때 `-1` 이 저절로 나온다.

</details>

<details>
<summary>힌트 5 — 니블 역순 = 바이트 역순 + 바이트 안 니블 교환</summary>

`0x12345678` 을 `REV` 하면 `0x78563412`. 이제 각 바이트 안의 두 니블만 바꾸면 된다: `((x >> 4) & 0x0F0F0F0F) | ((x & 0x0F0F0F0F) << 4)`. `0x0F0F0F0F` 는 논리 즉시값으로 인코딩되고(ARM-B03), `orr` 의 두 번째 피연산자에 `lsl #4` 를 붙일 수 있다.

</details>

## 5. 풀이

### Part A — 비트필드 명령은 전부 UBFM/SBFM/BFM 의 별칭(alias)

`ubfx`, `ubfiz`, `lsr #imm`, `lsl #imm`, `uxtb` 는 사실 **같은 명령 UBFM** 의 다른 이름이다. `sbfx`, `asr #imm`, `sxtb` 는 SBFM, `bfi`, `bfxil`, `bfc` 는 BFM. 어셈블러가 읽기 쉬운 이름을 (immr, imms) 숫자로 바꿔 줄 뿐이다. 아래 기계어는 `objdump -d` 로 본 **실제 인코딩**이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">비트필드 명령 인코딩 (32비트 W 형태): sf | opc | 100110 | N | immr | imms | Rn | Rd</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="40" height="34" class="f-mut"/>
    <rect x="60" y="40" width="70" height="34" class="f-acc"/>
    <rect x="130" y="40" width="150" height="34" class="f-mut"/>
    <rect x="280" y="40" width="40" height="34" class="f-mut"/>
    <rect x="320" y="40" width="120" height="34" class="f-warn"/>
    <rect x="440" y="40" width="120" height="34" class="f-warn"/>
    <rect x="560" y="40" width="80" height="34" class="f-ok"/>
    <rect x="640" y="40" width="80" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="40" y="62">sf</text><text x="95" y="62">opc</text><text x="205" y="62" font-family="monospace">100110</text>
    <text x="300" y="62">N</text><text x="380" y="62">immr</text><text x="500" y="62">imms</text>
    <text x="600" y="62">Rn</text><text x="680" y="62">Rd</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="40" y="90">31</text><text x="95" y="90">30:29</text><text x="205" y="90">28:23</text><text x="300" y="90">22</text>
    <text x="380" y="90">21:16</text><text x="500" y="90">15:10</text><text x="600" y="90">9:5</text><text x="680" y="90">4:0</text>
  </g>
  <text x="20" y="116" fill="currentColor">opc 00=SBFM 01=BFM 10=UBFM · imms ≥ immr: 꺼내기(extract) · imms &lt; immr: 넣기(insert)</text>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="20" y="146">기계어       어셈블리               opc   immr  imms   의미</text>
    <text x="20" y="170">0x53041C00   ubfx  w0, w0, #4, #4   UBFM    4     7    bits 7:4 → 3:0, 위는 0</text>
    <text x="20" y="192">0x13105C00   sbfx  w0, w0, #16, #8  SBFM   16    23    bits 23:16 → 7:0, bit 23 복사</text>
    <text x="20" y="214">0x531C0C00   ubfiz w0, w0, #4, #4   UBFM   28     3    bits 3:0 → 7:4, 나머지 0</text>
    <text x="20" y="236">0x331C0C20   bfi   w0, w1, #4, #4   BFM    28     3    w1[3:0] → w0[7:4], 나머지 유지</text>
    <text x="20" y="258">0x33181FE0   bfc   w0, #8, #8       BFM    24     7    Rn = 31 (wzr) 를 넣는 BFI</text>
    <text x="20" y="280">0x53047C00   lsr   w0, w0, #4       UBFM    4    31    bits 31:4 → 27:0</text>
    <text x="20" y="302">0x531C6C00   lsl   w0, w0, #4       UBFM   28    27    bits 27:0 → 31:4</text>
  </g>
  <text x="20" y="324" fill="currentColor">규칙: extract 는 immr = lsb, imms = lsb+width-1 / insert 는 immr = (32-lsb) mod 32, imms = width-1</text>
</svg>
```

그래서 C-B04 의 매크로와 명령이 1:1 로 맞는다.

| C-B04 (mask = 0xF0) | AArch64 | clang -O2 가 낸 것 (`demo_clang.c`, 실제) |
|---|---|---|
| `FIELD_GET(0xF0, r)` | `ubfx w0, w0, #4, #4` | `ubfx w0, w0, #4, #4` |
| `(int8_t)(r >> 16)` | `sbfx w0, w0, #16, #8` | `sbfx w0, w0, #16, #8` |
| `FIELD_PREP(0xF0, v)` | `ubfiz w0, w0, #4, #4` | `ubfiz w0, w0, #4, #4` |
| `FIELD_REPLACE(0xF0, r, v)` | `bfi w0, w1, #4, #4` | `bfi w0, w1, #4, #4` |
| `(r & ~c) \| s` | `bic` + `orr` | `bic w8, w0, w1` / `orr w0, w8, w2` |

C-B04 의 `FIELD_GET` 은 `/ FIELD_LSB(mask)` 로 나눗셈을 썼지만, 마스크가 상수라 clang 이 "2의 거듭제곱 나누기 = 시프트, 시프트 + AND = UBFX" 로 접어 버린다. **C 에서 매크로로 쓴 필드 코드는 어차피 이 명령들로 컴파일된다** — 덤프에서 `ubfx` 가 보이면 원래 소스는 `FIELD_GET`/`>> & mask` 였다고 거꾸로 읽으면 된다.

```bash
clang -std=c11 -O2 -S -fno-asynchronous-unwind-tables -o - demo_clang.c | grep -v '^\s*\.'
```

**즉시값 제약.** 위치와 폭은 명령어 안의 6비트 필드(immr, imms)다. 그래서 레지스터로 줄 수 없고, 범위도 어셈블러가 검사한다 (`demo_bad_imm.S`, 실제 에러):

```text
$ cc -c demo_bad_imm.S -o /tmp/ARM-B06_bad.o
demo_bad_imm.S:6:5: error: unrecognized instruction mnemonic, did you mean: ubfm?
    ubfx x0, x0, x1, #4
    ^
demo_bad_imm.S:7:22: error: requested insert overflows register
    bfi w0, w1, #30, #4
                     ^
demo_bad_imm.S:8:22: error: expected integer in range [1, 32]
    ubfx w0, w0, #4, #0
                     ^
demo_bad_imm.S:9:23: error: requested extract overflows register
    sbfx x0, x0, #60, #8
                      ^
```

**가변 위치 `field_get_var`.** 5 명령:

```text
lsr  x0, x0, x1     // LSRV: 시프트 양 = x1 mod 64
neg  w2, w2         // -width  (mod 64 로 64-width)
mov  x3, #-1        // ~0
lsr  x3, x3, x2     // mask = ~0 >> (64-width)
and  x0, x0, x3
```

| 입력 (reg = 0xFEDCBA9876543210) | `lsr x0` 후 x0 | `neg w2` 후 w2 (하위 6비트) | mask x3 | 결과 | 틀린 버전 `(1<<w)-1` |
|---|---|---|---|---|---|
| lsb = 8, width = 8 | 0x00FEDCBA98765432 | 0xFFFFFFF8 (56) | 0xFF | 0x32 | 0x32 (맞음) |
| lsb = 60, width = 4 | 0xF | 0xFFFFFFFC (60) | 0xF | 0xF | 0xF (맞음) |
| lsb = 0, width = 64 | 0xFEDCBA9876543210 | 0xFFFFFFC0 (0) | 0xFFFF…FFFF | 원래 값 | **0** (`1 << (64 mod 64)` = 1, 1 − 1 = 0) |

핵심은 **C 와 ISA 의 시프트 규칙이 다르다**는 것이다. C 에서 `x >> 64` 는 UB(C11 6.5.7)라서 기준 구현 `ref_get64` 은 `width >= 64` 를 분기로 피한다. AArch64 의 `LSLV/LSRV` 는 시프트 양의 하위 6비트(64비트) / 5비트(32비트)만 쓰도록 **정의**돼 있다(Arm ARM: `UInt(Xm) MOD datasize`). 그래서 `~0 >> (-width)` 는 width = 64 에서 시프트 0 → 정상, `(1 << width) - 1` 은 마스크 0 → 버그. 무작위 width 1..63 테스트로는 두 버전이 **똑같이 통과**한다 (`naive_agrees_below_64` 가 그걸 보여준다) — 경계값 width = 64 를 명시적으로 넣어야 잡힌다.

참고: `unsigned lsb` 는 w1 로 들어오는데 AAPCS64 에서 x1 의 상위 32비트는 보장되지 않는다(ARM-B01). 여기선 `lsr x0, x0, x1` 이 x1 의 **하위 6비트만** 보므로 상위 쓰레기와 무관하다. clang 은 같은 C 를 `lsr; mov #-1; lsl; cmp w2,#63; csinv; and` 6 명령으로 냈다 (`csinv` 로 width ≥ 64 분기를 없앤 것).

### Part B — RBIT, CLZ, REV

**RBIT** 는 비트 i 를 31−i (64비트면 63−i) 로 보낸다. C 루프 `c_rbit_loop` 를 clang -O2 는 `rbit` 로 바꾸지 **못했다** (실제 출력: 32회 루프에 `bfi w8, w0, #1, #31` 이 들어감). `__builtin_bitreverse32` 를 쓰면 `rbit w0, w0` 한 줄. 즉 "컴파일러가 알아서 해 주겠지" 가 항상 맞지는 않다.

**CLZ 와 x = 0.** AArch64 `CLZ` 는 0 → 32/64 를 돌려주도록 정의돼 있다. 반면 C 의 `__builtin_clz(0)` 은 GCC/Clang 문서상 **결과가 정의되지 않음** — 컴파일러는 "x ≠ 0" 이라고 가정해도 된다. 실제로 본 것:

```text
_c_hsb32_safe:   x ? 31 - __builtin_clz(x) : -1        _c_hsb32_xor:   __builtin_clz(x) ^ 31
	clz	w8, w0                                          	clz	w8, w0
	mov	w9, #31                                         	eor	w0, w8, #0x1f
	sub	w0, w9, w8
```

왼쪽: clang 이 `x ? … : -1` 의 **분기를 지웠다** — AArch64 CLZ(0) = 32 이므로 31 − 32 = −1 이 이미 맞다는 걸 백엔드가 안다. 오른쪽: 흔한 관용구 `clz ^ 31` 은 clz ∈ 0..31 에서 `31 − clz` 와 같지만(31 = 0b11111 이라 뺄셈에 빌림이 없다) clz = 32 이면 `32 ^ 31 = 63`. `shared_bugs.S` 의 `hsb32_eor` 가 정확히 이 모양이고, 하네스는 `hsb32_eor(0) == 63` 으로 차이를 확인한다. C23 `<stdbit.h>` 의 `stdc_leading_zeros(0)` 은 비트 폭을 돌려주도록 정의돼 있다 (확인 권장: 표준 라이브러리 지원 여부).

**니블 역순 `nibrev32`.** lldb 로 한 명령씩 실행한 **실제** 레지스터 값 (`demo_trace.lldb`):

```bash
cc -std=c11 -g -O2 harness.c solution.S shared_bugs.S -o /tmp/ARM-B06_dbg && lldb -b -s demo_trace.lldb /tmp/ARM-B06_dbg
```

| 실행한 명령 | w0 | w1 |
|---|---|---|
| (진입) | 0x12345678 | — |
| `rev w0, w0` | 0x78563412 | — |
| `and w1, w0, #0x0f0f0f0f` | 0x78563412 | 0x08060402 |
| `lsr w0, w0, #4` | 0x07856341 | 0x08060402 |
| `and w0, w0, #0x0f0f0f0f` | 0x07050301 | 0x08060402 |
| `orr w0, w0, w1, lsl #4` | **0x87654321** | 0x08060402 |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">니블 역순 = 바이트 역순(REV) + 바이트 안 니블 교환  —  RBIT 와는 다르다</text>
  <g font-family="monospace" fill="currentColor">
    <text x="20" y="58">x          = 12 34 56 78</text>
    <text x="20" y="88">rev        = 78 56 34 12</text>
    <text x="20" y="118">니블 교환  = 87 65 43 21</text>
  </g>
  <rect x="300" y="40" width="420" height="90" rx="8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="315" y="62">바이트 순서: REV 한 명령 (b3 b2 b1 b0 → b0 b1 b2 b3)</text>
    <text x="315" y="86">니블 교환: and / lsr / and / orr ..., lsl #4 (4 명령)</text>
    <text x="315" y="110">합계 5 명령 + ret. clang 은 C 판을 6 명령으로 냈다.</text>
  </g>
  <g font-family="monospace" fill="currentColor">
    <text x="20" y="168">x      = 0001 0010 0011 0100 0101 0110 0111 1000   (0x12345678)</text>
    <text x="20" y="192">rbit x = 0001 1110 0110 1010 0010 1100 0100 1000   (0x1E6A2C48)</text>
    <text x="20" y="216">nibrev = 1000 0111 0110 0101 0100 0011 0010 0001   (0x87654321)</text>
  </g>
  <rect x="20" y="232" width="700" height="56" rx="8" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="35" y="254">RBIT = 니블 순서 뒤집기 + 각 니블 안 비트 뒤집기. 두 결과가 같으려면 모든 니블이 비트 회문이어야 한다:</text>
    <text x="35" y="276">0000, 0110, 1001, 1111 (0x0, 0x6, 0x9, 0xF) — 이런 값만 테스트하면 버그가 통과해 버린다.</text>
  </g>
</svg>
```

수학적으로 보면 32비트 RBIT 는 "니블 8개의 순서 역전" ∘ "니블마다 4비트 역전" 이다. 그래서 `nibrev_rbit` (RBIT 하나로 니블 역순을 했다고 착각한 버전)은 `0x12345678` 에서 바로 틀리지만, 니블이 {0, 6, 9, F} 뿐인 값에서는 항상 맞는다 (`rbit_fools_palindromic_nibbles` 가 1만 개로 확인). 테스트 입력을 고를 때 "대칭인 값만 고르지 않기" 의 좋은 예다.

**bswap16 과 상위 16비트.** `rev w0, w0` 후 `lsr w0, w0, #16`: `[b3 b2 b1 b0] → [b0 b1 b2 b3] → [0 0 b0 b1]`. 입력의 상위 halfword(b3 b2)는 밀려 나가고 결과는 **0 확장**된다. 왜 신경 쓰나? `demo_narrow_abi.c` 를 두 타깃으로 컴파일한 실제 출력:

| 함수 (C) | arm64-apple-macos | aarch64-linux-gnu |
|---|---|---|
| `uint16_t ret_narrow(uint32_t x) { return (uint16_t)x; }` | `and w0, w0, #0xffff` / `ret` | `ret` |
| `use_ret`: `ext_h() + 1` (uint16_t 반환값 사용) | `bl _ext_h` / `add w0, w0, #1` | `bl ext_h` / `and w8, w0, #0xffff` / `add` |
| `use_arg(uint16_t a)`: `a + 1` | `add w0, w0, #1` | `and w8, w0, #0xffff` / `add` |
| `c_bswap16` | `rev16 w0, w0` | `rev16 w0, w0` |

Apple arm64 에서는 **좁은 인자는 caller 가, 좁은 반환값은 callee 가** 32비트로 확장하고, 받는 쪽은 그걸 믿는다 (Apple 문서는 인자 쪽을 명시; 반환값 쪽은 이 출력으로 관찰한 것 — 확인 권장). 일반 AAPCS64(Linux)는 상위 비트를 보장하지 않으므로 쓰는 쪽이 `and #0xffff` 한다. 그러니 Apple 에서 asm 이 `uint16_t` 를 돌려줄 때 상위 비트에 쓰레기를 남기면, caller 가 `and` 없이 그대로 써서 **틀린다**. `rev + lsr #16` 은 두 ABI 모두에서 안전하다. (clang 의 `rev16 w0, w0` 도 Apple 에서는 입력 상위가 0 이라는 보장 덕에 안전하고, Linux 에서는 반환값 상위가 쓰레기여도 되는 규칙 덕에 안전하다.)

**사이클.** 이 명령들은 모두 단순 정수 ALU 명령이라 최근 Cortex-A/Neoverse 최적화 가이드에서 대개 1사이클 지연·다중 파이프로 나온다 (Apple 은 공개하지 않음 — 일반론으로만, 확인 권장). 요점은 "UBFX 하나 = LSR + AND 둘" 이라 **명령 수와 레지스터 하나**를 아낀다는 것.

### Part C — 펌웨어: 디코드, 패치, 빅 엔디언

**`stat_decode`.** 필드마다 `ubfx` + `strb`. 두 가지 관찰: ① 최하위 필드는 `and w2, w0, #1`, 최상위 필드(VER)는 `lsr w2, w0, #28` 하나로 충분 — 한쪽 끝에 붙은 필드는 마스크 하나 또는 시프트 하나로 끝난다. ② TEMP 를 `int8_t` 에 저장할 때 `sbfx` 가 **필요 없다**. `strb` 는 하위 8비트만 메모리에 쓰므로 `lsr w2, w0, #16` 이면 bits 23:16 이 그대로 저장된다. 부호 확장은 레지스터에서 `int32_t` 로 쓸 때(`stat_get_temp`)만 필요하다.

**`stat_patch(0x30E507A3, 4)`** 추적:

| 명령 | w0 | 무엇이 바뀌었나 |
|---|---|---|
| (진입) | 0x30E507A3 | STATE = 0xA, ERRCNT = 7, ECC_FAIL = 1, READY = 1 |
| `bfi w0, w1, #4, #4` | 0x30E50743 | STATE ← 4 |
| `bfc w0, #8, #8` | 0x30E50043 | ERRCNT ← 0 |
| `and w0, w0, #0xfffffffd` | **0x30E50041** | ECC_FAIL ← 0 |

`and #0xfffffffd` 는 `bic w0, w0, #2` 로 써도 **같은 기계어**가 나온다 (`0x121e7800`, otool/objdump 모두 `and` 로 표시). BIC 즉시값 형은 어셈블러 별칭일 뿐 독립 인코딩이 없다. 0xFFFF00FD 처럼 "ERRCNT 와 ECC_FAIL 을 한 번에" 지우는 마스크는 0 덩어리가 두 개라 **논리 즉시값으로 인코딩되지 않는다**(ARM-B03) → 그래서 BFC + AND 두 명령으로 나눴다. `otool -tv` 는 두 번째 명령을 `bfc w0, #8, #8` 로, Apple `objdump` 는 `bfi w0, wzr, #8, #8` 로 보여준다 — 같은 `0x33181fe0`.

실제 MMIO 레지스터라면 주의: 이 함수는 **값**을 계산할 뿐이다. 레지스터에 쓰는 순간은 RMW 이고(C-B01 Part C), ECC_FAIL 같은 상태 비트가 실제로는 **W1C**(1을 써서 지움)라면 "0 으로 만든 값을 다시 쓰기" 로는 안 지워진다 — 데이터시트의 접근 타입(RW/RO/W1C)을 먼저 본다.

**SCSI READ(10) CDB.** 바이트 0 = opcode 0x28, 2..5 = LBA (빅 엔디언), 6 = group, 7..8 = 전송 길이 (빅 엔디언), 9 = control. NVMe 는 리틀 엔디언이지만 SCSI/UFS(그리고 네트워크 헤더, 많은 I²C/SPI 센서 레지스터)는 빅 엔디언이라 펌웨어는 `REV` 를 자주 쓴다. clang 도 바이트 4개를 `<<` 로 조립한 C 를 `ldr w8, [x0]` + `rev w0, w8` 로 바꿨다 (`c_be32`, 실제).

- `cdb10_lba`: `ldur w0, [x0, #2]` + `rev`. 오프셋 2 는 4의 배수가 아니라 스케일된 `ldr` 즉시값 형이 안 되고 9비트 비스케일 `ldur` 형을 쓴다 (주소 지정 모드 상세는 ARM-B04).
- `cdb10_len`: `ldurh` (상위 16비트 0) + `rev16` (각 halfword 안에서 바이트 교환; 상위 halfword 는 0 그대로) → 깨끗한 `uint16_t`.
- `cdb10_build`: `str xzr` + `strh wzr` 로 10바이트를 0, `strb` 로 opcode, `rev` + `stur`, `rev16` + `sturh`. 하네스는 오프셋 0..15 의 **정렬 안 된** 주소에서 만들고, 앞뒤 가드 바이트(0xA5)가 그대로인지 본다.

정렬 안 된 `ldur/stur` 는 AArch64 **일반(Normal) 메모리**에서 `SCTLR_ELx.A = 0` 이면 허용된다 (macOS/Linux 사용자 공간이 그렇다). **Device 메모리(MMIO)** 에 대한 비정렬 접근은 정렬 폴트이고, 배타 로드/스토어(ARM-M04)와 일부 원자 명령도 정렬이 필요하다 (Arm ARM "Alignment support" — 확인 권장). Cortex-M3/M4 도 일반 `LDR/STR` 비정렬은 허용하지만 `LDM/STM/LDRD` 는 안 된다.

**Linux(ELF)에서도 같은 파일.** `.S` 머리의 `FUNC/ENDF` 매크로가 `#ifdef __APPLE__` 로 갈라진다: Mach-O 는 `.globl _name` / `_name:`, ELF 는 `.global name` / `.type name, %function` / `.size name, . - name`. Lima VM(aarch64 Ubuntu)에서 실제로 빌드·실행한 결과:

```text
$ limactl shell elinux -- bash -lc 'cd /tmp/ARM-B06_work && gcc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_bugs.S -o sol && ./sol | tail -1'
34/34 passed                         # gcc 15.2.0 (GNU as 2.46). clang 21.1.8 로도 34/34
$ readelf -s sol | grep -E 'nibrev32|cdb10_build'
   100: 00000000000021cc    36 FUNC    GLOBAL DEFAULT   12 cdb10_build
   122: 0000000000002138    24 FUNC    GLOBAL DEFAULT   12 nibrev32
```

`.size` 덕에 ELF 심볼에 크기(24 = 명령 6개 × 4바이트)가 붙는다 — gdb/perf 가 함수 경계를 아는 근거다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_bugs.S -o /tmp/ARM-B06_s && /tmp/ARM-B06_s   # 34/34 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   harness.c solution.S shared_bugs.S -o /tmp/ARM-B06_asan && /tmp/ARM-B06_asan                         # 34/34, 경고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `ubfx x0, x0, x1, #4` | 위치/폭은 명령어 안의 즉시값(immr/imms). 어셈블러: `unrecognized instruction mnemonic` | `lsr x0, x0, x1` + `and` 마스크 |
| 마스크를 `(1 << width) - 1` 로 | `LSLV` 는 mod 64 → width = 64 에서 마스크 0. 무작위 테스트는 통과 | `mov x3,#-1` / `neg w2,w2` / `lsr x3,x3,x2` |
| 부호 있는 필드를 `ubfx` 로 | TEMP 0xE5 가 229 로 나옴 | `sbfx w0, w0, #16, #8` → −27 |
| `bfi` 와 `ubfiz` 혼동 | `ubfiz` 는 나머지를 0 으로 지움 → 다른 필드가 사라짐 | 필드 교체는 `bfi` (Wd 가 입력 겸 출력) |
| `31 - __builtin_clz(x)` 를 C 에서 x = 0 으로 호출 | C 에선 UB. 관용구 `clz ^ 31` 은 x = 0 에서 63 | asm `clz` 는 0 → 32 정의됨, 또는 `x ? … : -1` |
| 니블 역순을 `rbit` 로 | 니블 안 비트까지 뒤집힘 (0x12345678 → 0x1E6A2C48) | `rev` + 바이트 안 니블 교환 |
| asm `uint16_t` 반환값 상위에 쓰레기 | Apple caller 는 재확장을 안 한다 | `rev w0,w0` / `lsr w0,w0,#16` (0 확장) |
| 0 덩어리가 둘인 마스크로 `and` 즉시값 (`#0xffff00fd`) | 논리 즉시값으로 인코딩 불가 → `error: expected compatible register or logical immediate` (실제) | `bfc` + `and` 로 나누거나 `mov`+`bic` 레지스터 형 |

## 8. 면접 Q&A

<details>
<summary>Q1. `ubfx w8, w9, #4, #3` 는 C 로 뭔가요? `lsr` + `and` 대신 왜 이걸 쓰나요?</summary>

`w8 = (w9 >> 4) & 0x7` — bits 6:4 를 꺼낸다. 한 명령이라 명령 수·레지스터를 아끼고, 마스크 상수를 만들 필요도 없다 (0x7 은 논리 즉시값이 되지만 임의 폭/위치 조합에서 `and` 즉시값이 늘 되는 건 아니다). 꼬리질문 "ubfx 의 정체는?" → UBFM 의 별칭: immr = lsb(4), imms = lsb+width−1(6). `lsr #n` 도 UBFM(immr = n, imms = 31).

</details>

<details>
<summary>Q2. 비트 위치가 변수면 UBFX 를 못 쓰는데, 어떻게 하나요? 경계는?</summary>

`lsr x0, x0, xLsb` 후 마스크 `~0 >> (64 − width)` 와 AND. `64 − width` 는 `neg` 로 충분하다 (가변 시프트는 시프트 양 mod 64). 경계: width = 64 에서 `(1 << width) − 1` 방식은 ISA 에선 mask = 0, C 에선 UB. 또 C 에서 `x >> 64` 자체가 UB 라 C 구현은 분기가 필요하지만 asm 은 ISA 규칙(mod 64) 덕에 분기 없이 된다. 이 차이를 "C 추상 기계 vs ISA 정의 동작" 이라고 말할 수 있으면 좋다.

</details>

<details>
<summary>Q3. CLZ 의 입력이 0 이면? 우선순위 스케줄러에서는 어떻게 쓰나요?</summary>

AArch64/ARMv7 `CLZ` 는 0 → 레지스터 폭(32/64)을 돌려준다. C `__builtin_clz(0)` 은 정의되지 않으므로 컴파일러가 x ≠ 0 을 가정할 수 있다 — 이번에 본 것처럼 Apple clang 은 `x ? 31 − clz(x) : −1` 의 분기를 **지웠다** (타깃 CLZ 가 0 을 정의하니까). RTOS: 준비 리스트 비트맵에서 "가장 높은 우선순위" = `31 − clz(ready_bitmap)` 한 명령 (FreeRTOS 의 Cortex-M3/M4 포트 최적화 태스크 선택이 이 방식). Cortex-M0(ARMv6-M)에는 CLZ 가 없어 소프트웨어 루프/테이블을 쓴다.

</details>

<details>
<summary>Q4. 32비트 워드의 니블 순서를 뒤집어 보세요. ARM 에서는 몇 명령인가요?</summary>

C: `x = ((x >> 4) & 0x0F0F0F0F) | ((x & 0x0F0F0F0F) << 4);` (바이트 안 니블 교환) 후 바이트 역순(`bswap32`). AArch64: `rev` + `and` + `lsr` + `and` + `orr …, lsl #4` = 5 명령 (두 번째 피연산자의 시프트를 `orr` 에 접어 넣음). 함정: `rbit` 하나로 끝난다고 답하면 틀린다 — RBIT 는 니블 안의 비트 순서까지 뒤집는다. 0x12345678 → RBIT 0x1E6A2C48, 니블 역순 0x87654321.

</details>

<details>
<summary>Q5. 빅 엔디언 패킷/CDB 필드는 어떻게 읽나요? 정렬 안 된 주소면?</summary>

`ldr` 후 `rev` (16비트는 `ldrh` + `rev16`). C 로 바이트 4개를 시프트해 조립해도 clang -O2 는 `ldr` + `rev` 로 바꾼다 (실제 확인). 정렬: AArch64 Normal 메모리에선 `SCTLR.A = 0` 이면 비정렬 `ldr/ldur` 허용 (성능은 캐시 라인을 걸치면 손해일 수 있음). Device/MMIO 비정렬, 배타 접근은 폴트. 이식성이 중요하면 `memcpy` 로 읽고 `__builtin_bswap32` — 컴파일러가 같은 두 명령으로 만든다.

</details>

<details>
<summary>Q6. `BIC` 은 왜 따로 있나요? `BIC` 즉시값은 있나요?</summary>

레지스터 형 `bic Wd, Wn, Wm` = `Wn & ~Wm` 을 한 명령으로 (NOT 을 위해 레지스터·명령 하나를 더 쓰지 않는다). `ORN` (`| ~`), `EON` (`^ ~`) 도 같은 이유. `mvn` 은 `orn Wd, wzr, Wm` 의 별칭. 즉시값 BIC 은 어셈블러 별칭일 뿐이라 `bic w0, w0, #2` 는 `and w0, w0, #0xfffffffd` (같은 기계어 `0x121e7800`)로 바뀐다. 즉 "보수가 논리 즉시값으로 인코딩될 때만" 된다 (ARM-B03).

</details>

<details>
<summary>Q7. asm 함수가 `uint16_t` 를 받거나 돌려줄 때 상위 비트는 누가 책임지나요?</summary>

일반 AAPCS64: 좁은 인자·반환값의 상위 비트는 **unspecified** → 쓰는 쪽이 확장한다 (Linux 타깃 clang 이 `and w8, w0, #0xffff` 를 넣는 것으로 확인). Apple arm64: 문서상 caller 가 32비트 미만 인자를 32비트로 확장하고, 실제 clang 출력상 callee 도 좁은 반환값을 확장하며(`ret_narrow` 의 `and #0xffff`) 받는 쪽은 다시 확장하지 않는다. 그래서 Apple 에서 asm 이 반환값 상위에 쓰레기를 남기면 C caller 가 틀린 값을 쓴다. 안전한 습관: 반환 직전에 0/부호 확장 (`uxth`, `lsr #16` 등).

</details>

<details>
<summary>Q8. AArch64 에는 왜 CTZ 나 ROL 이 (원래) 없나요?</summary>

다른 명령 조합으로 싸게 되기 때문이다: CTZ = `rbit` + `clz`, ROL n = `ror` by −n (`neg` + `ror`; 즉시값이면 어셈블러가 `ror #(32−n)` 으로). 최근 FEAT_CSSC (Armv8.9/9.4 계열 — 확인 권장) 가 `CTZ`, `CNT`, `ABS`, `SMAX/UMIN` 등을 스칼라 명령으로 추가했다. 이 M6 는 `sysctl hw.optional.arm.FEAT_CSSC: 1` 이고, 연습 1 에서 `ctz x0, x0` 를 실제로 실행한다. clang 도 `-march=armv9.4-a` 를 주면 `x ? ctz : 64` 를 `ctz x0, x0` 한 줄로 냈다.

</details>

## 9. 추가 연습

### 연습 1 — ctz64: RBIT + CLZ, 그리고 CSSC CTZ

`int ctz64(uint64_t x)` (가장 낮은 1비트 위치, x = 0 → 64). CTZ 명령 없이 써라. 그 다음 FEAT_CSSC 의 `ctz` 로도.

<details>
<summary>답 보기</summary>

`rbit x0, x0` / `clz x0, x0` — 뒤집으면 뒤쪽 0 이 앞쪽 0 이 된다. x = 0 → 64 가 공짜. clang 도 `x ? __builtin_ctzll(x) : 64` 를 정확히 `rbit` + `clz` 로 냈다(실제). CSSC 판은 `.arch_extension cssc` 후 `ctz x0, x0` 하나. 지원 안 하는 CPU 에선 SIGILL 이므로 하네스가 `sysctlbyname("hw.optional.arm.FEAT_CSSC")` 로 확인하고 호출한다 (Lima VM 의 Linux 에선 확인을 생략하고 SKIP — 그래서 VM 에선 15/15).

</details>

### 연습 2 — 가변 위치 필드 쓰기

`uint64_t field_insert_var(uint64_t reg, unsigned lsb, unsigned width, uint64_t val)` — 실행 중 정해지는 위치/폭의 `FIELD_REPLACE`. BFI 는 즉시값만 받는다.

<details>
<summary>답 보기</summary>

마스크 m = `~0 >> (−width)` (Part A 와 같은 트릭), `val &= m`, `m <<= lsb`, `val <<= lsb`, `bic reg, reg, m`, `orr reg, reg, val` — 8 명령. (lsb, width) 2080 쌍 전부 × 8 값을 C 기준과 대조.

</details>

### 연습 3 — rotl32

`uint32_t rotl32(uint32_t x, unsigned n)` (n mod 32 만큼 왼쪽 회전). AArch64 엔 ROL 이 없다.

<details>
<summary>답 보기</summary>

`neg w1, w1` / `ror w0, w0, w1` — RORV 는 회전 양 mod 32 이므로 −n ≡ 32 − n. C 기준은 `n ? (x << n) | (x >> (32 − n)) : x` — n = 0 에서 `x >> 32` (UB) 를 피하려는 분기가 필요하다. n = 0..63 전부 테스트. 즉시값 `ror` 는 `EXTR Wd, Wn, Wn, #n` 의 별칭이다 (`0x13802000 ror w0, w0, #0x8`).

</details>

### 연습 4 — nibrev64

`0x0123456789ABCDEF → 0xFEDCBA9876543210`.

<details>
<summary>답 보기</summary>

`rev x0, x0` (8바이트 역순) + 64비트 마스크 `0x0f0f0f0f0f0f0f0f` (논리 즉시값으로 인코딩됨) 로 같은 니블 교환. 참고로 `rev w` 와 `rev32 x` 는 같은 opc(10)를 공유한다: `rev w0,w0 = 0x5ac00800`, `rev32 x0,x0 = 0xdac00800` (sf 비트만 다름).

</details>

### 연습 5 — CLS 와 정규화

`int cls32(int32_t x)` — 부호 비트 바로 아래부터 부호와 같은 비트가 몇 개인가. `cls32(1) = 30`, `cls32(0) = cls32(-1) = 31`, `cls32(INT32_MIN) = 0`. 어디에 쓰나?

<details>
<summary>답 보기</summary>

`cls w0, w0` 한 명령. 고정소수점(Q31) DSP 코드에서 "넘치지 않고 왼쪽으로 몇 칸 밀 수 있나" = 정규화 시프트 양 (블록 부동소수점, AGC). 부호 없는 쪽은 CLZ 가 같은 역할. C 기준은 비트 루프 (`x << n` 을 signed 로 하면 UB 라 unsigned 로 본다).

</details>

### 연습 6 — BFXIL 과 ORN

(a) `log_pack(word, stat)`: word[7:0] ← stat[15:8] (ERRCNT), 나머지 보존. (b) `irq_mask_all_except(imr, keep)` = `imr | ~keep` (1 = masked).

<details>
<summary>답 보기</summary>

(a) `bfxil w0, w1, #8, #8` — "원본의 lsb 부터 width 비트를 목적지 **하위**에 넣고 나머지 유지" (BFM 의 extract 형, `0x33083c20`). BFI 의 반대 방향. (b) `orn w0, w0, w1`. 둘 다 C 로는 두세 연산이다.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-B06_p && /tmp/ARM-B06_p   # 16/16 passed (M6, CSSC 포함)
```

## 10. 핵심 정리

- `ubfx`(= FIELD_GET) · `sbfx`(부호 필드) · `ubfiz`(= FIELD_PREP) · `bfi`(= FIELD_REPLACE) · `bfxil` · `bfc` · `lsl/lsr/asr #imm` 은 전부 **UBFM/SBFM/BFM 의 별칭**: (immr, imms) 두 6비트 즉시값.
- 위치/폭이 변수면 `lsr` + 마스크. 마스크는 `~0 >> (−width)` — 가변 시프트는 **mod 폭** 이라 width = 64 가 공짜, `(1 << w) − 1` 은 거기서 0.
- `CLZ(0)` = 32/64 (ISA 정의) vs `__builtin_clz(0)` UB. `31 − clz` 는 0 → −1, `clz ^ 31` 은 0 → 63.
- 니블 역순 = `rev` + 바이트 안 니블 교환 (5 명령). `rbit` 는 니블 안 비트까지 뒤집는다 — {0,6,9,F} 만 쓰는 테스트는 이걸 못 잡는다.
- 빅 엔디언 필드: `ldr/ldur` + `rev`, 16비트는 `ldrh` + `rev16`. Normal 메모리 비정렬 OK, Device 메모리는 폴트.
- `bic`/`orn` = NOT 이 들어간 AND/OR 한 명령. 즉시값 `bic` 은 `and ~imm` 별칭 — 보수가 논리 즉시값이어야 한다.
- Apple arm64 는 좁은 인자(caller)·반환값(callee) 을 확장해 두고 믿는다 → asm 반환값은 깨끗이 확장해서 돌려준다.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — UBFM/SBFM/BFM 와 별칭(UBFX, SBFX, UBFIZ, BFI, BFXIL, BFC, LSL/LSR/ASR), CLZ/CLS/RBIT/REV*, LSLV/LSRV/RORV ("shift amount MOD datasize"), FEAT_CSSC: <https://developer.arm.com/documentation/ddi0487/latest>
- Arm A64 Instruction Set (명령별 페이지): <https://developer.arm.com/documentation/ddi0602/latest>
- Apple, *Writing ARM64 code for Apple platforms* (좁은 인자 확장 등 AAPCS64 와의 차이): <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
- Arm, *Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)*: <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
- GCC 문서 `__builtin_clz` ("if x is 0, the result is undefined"): <https://gcc.gnu.org/onlinedocs/gcc/Bit-Operation-Builtins.html>
- Linux `<linux/bitfield.h>` (FIELD_GET/FIELD_PREP): <https://github.com/torvalds/linux/blob/master/include/linux/bitfield.h>
- T10 SBC — SCSI READ(10) CDB 형식 (UFS 도 같은 CDB 사용)
- 레슨 내 관찰 파일: `demo_clang.c` (clang -O2 출력), `demo_narrow_abi.c` (Apple vs Linux 좁은 타입), `demo_bad_imm.S` (어셈블러 에러), `demo_trace.lldb` (nibrev32 단계 실행)
