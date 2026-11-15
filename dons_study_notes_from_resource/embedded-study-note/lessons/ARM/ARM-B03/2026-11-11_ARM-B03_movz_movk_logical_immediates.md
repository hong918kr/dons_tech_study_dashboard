---
id: ARM-B03
time: 60분
updated: 2026-11-11
---
# ARM-B03 데이터 이동과 즉시값: MOV, MOVZ/MOVK, 논리 즉시값

## 학습 목표

- 32비트 고정 길이 명령 안에 64비트 상수를 넣을 수 없는 이유를 설명하고, 임의의 64비트 값을 **MOVZ/MOVN + MOVK 최소 시퀀스**로 계획(plan)하고 그 기계어를 손으로 인코딩한다 — 진짜 어셈블러 출력과 비트 단위로 일치.
- `mov x0, #-0xa988` / `movk x0, #0x1234, lsl #16` 같은 디스어셈블리를 보고 **상수를 바로 읽는다** (크래시 덤프·startup 코드 읽기).
- **논리 즉시값(logical immediate / bitmask immediate)** 의 정의(원소 크기 e, 연속 1 의 개수 s, 회전 r)와 `N:immr:imms` 인코딩을 이해하고, 판정 함수 `is_logical_imm64/32` 를 Arm ARM 의 `DecodeBitMasks` 로 **전수 대조**한다 (64비트 5334개, 32비트 1302개).
- 어셈블러 에러(`expected compatible register or logical immediate`)가 왜 나는지, clang 이 인코딩 안 되는 마스크를 어떻게 처리하는지(`mov w8,#..` + `and`) 실제 출력으로 확인한다.
- 펌웨어 레지스터 마스크(0x0000FF00, 0x00FF00FF00FF00FF, 0x12345678 …)의 **명령 개수 비용**을 즉시 판단한다.

## 1. 문제

면접관이 크래시 덤프나 `objdump` 한 조각을 내밀며 "이 `mov x0, #-0xa988` / `movk` 두 줄, x0 에 뭐가 들어가요?" 라고 묻는다 — startup 코드가 주변장치 베이스 주소를 만들 때, 드라이버가 레지스터 마스크를 쓸 때 반드시 나오는 패턴이다. Don 이 다뤘던 SSD 컨트롤러 펌웨어(ARM 코어)든, Apple·NVIDIA 의 AArch64 SoC 든, 상수 로드 방식을 모르면 디스어셈블리 한 줄도 확신 있게 못 읽는다.

AArch64 명령은 **모두 32비트**다. 그래서 64비트 상수는 한 명령에 통째로 못 들어간다. 방법은 셋이다:

1. **MOVZ / MOVN / MOVK** — 16비트 조각(halfword) 하나씩, 위치(hw = 0..3)를 골라 넣는다. 최대 4개.
2. **논리 즉시값** — `AND/ORR/EOR/ANDS/TST` 의 즉시값은 13비트(`N:immr:imms`)로 "반복되는 회전된 1 의 덩어리" 패턴만 표현한다. `mov x0, #0x5555555555555555` 는 사실 `orr x0, xzr, #imm` 한 개다.
3. 리터럴 풀(`ldr x0, =imm`) 또는 주소라면 `ADRP + ADD` (ARM-A09 담당 — 여기선 Q&A 에서만 언급).

명령 인코딩의 기초(필드, opcode, 즉시값 폭)는 ARCH-B08 의 장난감 ISA 에서 이미 했다. 이번에는 진짜 A64 다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">MOVZ / MOVN / MOVK (64-bit, sf=1) 비트 배치 — 예: movk x0, #0x1234, lsl #16 = 0xF2A24680</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="50" width="22" height="34" class="f-mut"/>
    <rect x="42" y="50" width="44" height="34" class="f-acc"/>
    <rect x="86" y="50" width="131" height="34" class="f-mut"/>
    <rect x="217" y="50" width="44" height="34" class="f-warn"/>
    <rect x="261" y="50" width="350" height="34" class="f-ok"/>
    <rect x="611" y="50" width="109" height="34" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="31" y="44">31</text><text x="64" y="44">30:29</text><text x="151" y="44">28:23</text>
    <text x="239" y="44">22:21</text><text x="436" y="44">20:5</text><text x="665" y="44">4:0</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="31" y="72">1</text><text x="64" y="72">11</text><text x="151" y="72">100101</text>
    <text x="239" y="72">01</text><text x="436" y="72">0001 0010 0011 0100</text><text x="665" y="72">00000</text>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="31" y="104">sf</text><text x="64" y="104">opc</text><text x="151" y="104">wide move</text>
    <text x="239" y="104">hw</text><text x="436" y="104">imm16 = 0x1234</text><text x="665" y="104">Rd = x0</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="20" y="140">opc 00 MOVN : Xd = ~(imm16 &lt;&lt; 16*hw)          (나머지 halfword 는 0xffff)</text>
    <text x="20" y="162">opc 10 MOVZ : Xd =  (imm16 &lt;&lt; 16*hw)          (나머지 halfword 는 0x0000)</text>
    <text x="20" y="184">opc 11 MOVK : Xd[16*hw+15 : 16*hw] = imm16  (나머지는 그대로 Keep)</text>
    <text x="20" y="206">opc 01      : 미할당 (unallocated)</text>
  </g>
  <text x="20" y="238" fill="currentColor">hw = 몇 번째 16비트 칸 (0..3), 시프트 = 16*hw → lsl #8 은 없다. w 레지스터(sf=0)는 hw 0..1.</text>
  <text x="20" y="262" fill="currentColor">MOVK = 0xF2800000 | hw&lt;&lt;21 | imm16&lt;&lt;5 | rd. MOVZ 는 0xD2800000, MOVN 은 0x92800000.</text>
</svg>
```

### Part A — MOVZ/MOVN + MOVK 계획과 인코딩

- `uint32_t enc_movz/enc_movk/enc_movn(unsigned rd, uint16_t imm16, unsigned hw)` — 64비트(x 레지스터) 기계어 한 워드.
- `int movz_movk_plan(uint64_t v, insn out[4])` — v 를 만드는 **최소** 시퀀스. 반환 = 명령 수. 0xFFFF 인 halfword 가 0x0000 인 halfword 보다 **많으면** MOVN 으로 시작(동점이면 MOVZ). 첫 명령 = 가장 낮은 "손봐야 할" hw, 나머지 MOVK 는 hw 오름차순.
- 검증 ①: `shared_ref.S` 에 진짜 어셈블러로 조립한 참조 명령 9개를 라벨 `_ref_movs` 아래 두고, 실행 중 그 32비트 워드를 **코드 영역에서 데이터로 읽어** 내 인코더와 비교.
- 검증 ②: `solution.S` 에 손으로 쓴 상수 함수 6개(`k_deadbeef`, `k_cafef00d_deadbeef`, `k_ffffffff_12345678`, `k_minus2`, `k_qemu_uart`, `k_tie`) — 반환값과 **기계어 자체**가 plan 과 똑같아야 한다.

### Part B — 논리 즉시값 판정 (경계·전수)

- `bool is_logical_imm64(uint64_t v, unsigned *N, unsigned *immr, unsigned *imms)` 와 32비트판 `is_logical_imm32`.
- 하네스는 Arm ARM 의 `DecodeBitMasks()` 를 C 로 옮긴 참조 구현으로 `(N, immr, imms)` 8192 조합을 모두 디코드 → 서로 다른 64비트 패턴 **5334개** 집합을 만든다. 정방향(5334개 모두 true + 어셈블러와 같은 정규형 필드) · 역방향(1비트 뒤집기 34만 개 + 0..65535 + 무작위 30만 개에서 true ⇔ 집합 원소) 둘 다 확인.
- 0 과 ~0 은 **인코딩 불가**. 진짜 어셈블러가 고른 필드(`_ref_logic` 13개)와 일치해야 한다.
- 관찰: `and x0, x0, #0x1234` 의 실제 어셈블러 에러(clang · GNU as), 2^32 전수 데모.

### Part C — 펌웨어 마스크 비용 + 레지스터 패치

- `int and_cost64(uint64_t mask)` / `and_cost32(uint32_t mask)` — `x & mask` 에 드는 명령 수. 기댓값은 **clang -O2 실제 출력에서 센 숫자**.
- `uint32_t pllcfgr_set_plln168(uint32_t r)` (asm) — STM32F4 `RCC_PLLCFGR` 의 PLLN 필드 [14:6] 을 168 로. clear 는 논리 즉시값 AND 1개, set 은 MOVZ + ORR.

## 2. Problem (English)

> **Materializing constants on AArch64.**
>
> 1. Write `uint32_t enc_movz(unsigned rd, uint16_t imm16, unsigned hw)`, `enc_movk`, `enc_movn` (64-bit forms). Write `int movz_movk_plan(uint64_t v, insn out[4])` that returns the minimal MOVZ/MOVN + MOVK sequence for `v` (use MOVN when more halfwords are `0xFFFF` than `0x0000`). Your encodings must match the system assembler bit-for-bit.
> 2. In a `.S` file, write leaf functions `uint64_t k_deadbeef(void)`, `k_cafef00d_deadbeef`, `k_ffffffff_12345678`, `int64_t k_minus2(void)`, `k_qemu_uart` (returns `0x09000000`), `k_tie` (`0xFFFF00000000FFFF`) — no memory loads, exactly the instructions of your plan.
> 3. Write `bool is_logical_imm64(uint64_t v, unsigned *N, unsigned *immr, unsigned *imms)` and a 32-bit variant: return whether `v` is encodable as the immediate of `AND/ORR/EOR/ANDS`, and if so the canonical fields the assembler would emit.
> 4. For firmware masks, return the instruction count of `x & mask` (`and_cost64/32`), and write `uint32_t pllcfgr_set_plln168(uint32_t r)` in assembly that sets bits [14:6] to 168 and preserves the rest.
>
> Examples: `plan(0xFFFFFFFF12345678)` → `movn x0,#0xa987 ; movk x0,#0x1234,lsl #16` (2). `is_logical_imm64(0xFF00)` → true, `N=1 immr=56 imms=7`. `is_logical_imm64(0x1234)` → false. `and_cost32(0x12345678) == 3`.
>
> Follow-ups: How many 64-bit values are logical immediates? Why are 0 and all-ones excluded? How else can you load a 64-bit constant or address?

## 3. 시작 코드

하네스 + TODO 는 C(`starter.c`), 상수 함수 스텁은 `starter.S`, 어셈블러 참조표는 공용 `shared_ref.S`.

<!-- code: starter.c -->

<!-- code: starter.S -->

<!-- code: shared_ref.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c starter.S shared_ref.S -o st && ./st
```

실제 출력 (발췌, 경고 0개, exit 1):

```text
DecodeBitMasks: 64-bit valid=7680 distinct=5334 canonical=5334 | 32-bit valid=3648 distinct=1302 canonical=1302
  ref_movs[0]: assembler=d2824680 yours=00000000
FAIL starter.c:231  w == ref_movs[i]
  … (ref_movs 9개 모두)
FAIL starter.c:235  plan_first_is(0, 1, OP_MOVZ, 0, 0)
  … 
  plan: 200000 bad, first v=0xdc1b77ae0bf34dad
FAIL starter.c:265  k_deadbeef() == 0xDEADBEEFull
FAIL starter.c:266  code_matches_plan(k_deadbeef, 0xDEADBEEFull)
  …
  ref_logic[0] 92781c20: assembler N=1 immr=56 imms=7, yours ok=0 N=9 immr=99 imms=99
  …
FAIL starter.c:378  (code[0] & 0xFF800000u) == 0x12000000u
9/78 passed
```

첫 줄은 하네스의 참조 디코더가 만든 표 크기다 (TODO 와 무관하게 항상 같다). 스텁 asm 은 `mov x0, #0` / `ret` 이라 안전하게 틀린 값만 돌려준다.

## 4. 힌트

<details>
<summary>힌트 1 — 인코더는 상수 하나 + OR 세 개</summary>

필드 위치만 맞추면 된다: `base | hw << 21 | imm16 << 5 | rd`. base 는 sf=1 과 opc, `100101` 을 합친 값 — MOVN `0x92800000`, MOVZ `0xD2800000`, MOVK `0xF2800000`. 확인은 `clang -c shared_ref.S && objdump -d` 로 직접.

</details>

<details>
<summary>힌트 2 — 계획: "공짜로 채워지는 halfword" 를 세라</summary>

MOVZ 는 나머지 칸을 0x0000 으로, MOVN 은 0xFFFF 로 채워 준다. 그래서 0x0000 칸 수 z, 0xFFFF 칸 수 f 를 세고 큰 쪽을 고르면 명령 수 = `max(1, 4 - max(z, f))`. MOVN 의 imm16 은 그 칸 값의 **반전** `~h` 다 (MOVN 이 다시 뒤집으니까).

</details>

<details>
<summary>힌트 3 — 논리 즉시값: 주기 → 1 의 개수 → 회전</summary>

① v 가 0 이나 ~0 이면 false. ② 64 → 32 → … → 2 로 반씩 줄이며 "아래 절반 == 위 절반" 인 동안 원소 크기 e 를 줄인다. ③ 원소(e 비트)의 1 개수 s 를 센다. ④ `ones(s)` 를 e 비트 안에서 오른쪽으로 r 만큼 회전해 원소와 같아지는 r 이 있으면 encodable. 없으면 1 들이 두 덩어리 이상이다.

</details>

<details>
<summary>힌트 4 — imms 의 위쪽 비트는 원소 크기 표시</summary>

`imms = (원소 크기 접두) | (s - 1)`. e=64 는 N=1 + 6비트 전체, e=32 는 `0sssss`, e=16 은 `10ssss`, … e=2 는 `11110s`. 한 식으로: `((~(e-1) << 1) & 0x3F) | (s-1)`. immr = r (0 ≤ r < e — 어셈블러가 쓰는 정규형).

</details>

<details>
<summary>힌트 5 — 32비트판은 64비트판을 재사용</summary>

32비트 명령은 원소 크기가 최대 32. 값을 `v << 32 | v` 로 두 번 이어 붙여 64비트 판정에 넣고, 결과 N 이 0 인지(= e ≤ 32) 보면 된다.

</details>

## 5. 풀이

### Part A — 공짜 halfword 를 최대로

| 명령 | 결과 | 쓰는 경우 |
|---|---|---|
| `movz xd, #imm16, lsl #16*hw` | 그 칸만 imm16, 나머지 0 | 0x0000 칸이 많을 때 시작 |
| `movn xd, #imm16, lsl #16*hw` | `~(imm16 << 16*hw)` — 그 칸 `~imm16`, 나머지 0xFFFF | 0xFFFF 칸이 많을 때 (음수 상수) |
| `movk xd, #imm16, lsl #16*hw` | 그 칸만 교체, 나머지 **유지** | 두 번째 이후 |

왜 MOVK 로 시작하면 안 되나: MOVK 는 나머지 칸을 "유지" 하므로 레지스터에 남아 있던 쓰레기가 그대로 남는다. 첫 명령은 반드시 레지스터 전체를 정의하는 MOVZ/MOVN.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">halfword 4칸 중 "공짜" 칸(회색)은 첫 명령이 채워 준다 — 색칠한 칸만 명령이 필요</text>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="95" y="48">hw3</text><text x="165" y="48">hw2</text><text x="235" y="48">hw1</text><text x="305" y="48">hw0</text></g>
  <g stroke="currentColor">
    <rect x="60" y="56" width="70" height="30" class="f-acc"/><rect x="130" y="56" width="70" height="30" class="f-acc"/><rect x="200" y="56" width="70" height="30" class="f-acc"/><rect x="270" y="56" width="70" height="30" class="f-warn"/>
    <rect x="60" y="116" width="70" height="30" class="f-mut"/><rect x="130" y="116" width="70" height="30" class="f-mut"/><rect x="200" y="116" width="70" height="30" class="f-acc"/><rect x="270" y="116" width="70" height="30" class="f-warn"/>
    <rect x="60" y="176" width="70" height="30" class="f-mut"/><rect x="130" y="176" width="70" height="30" class="f-warn"/><rect x="200" y="176" width="70" height="30" class="f-mut"/><rect x="270" y="176" width="70" height="30" class="f-mut"/>
    <rect x="60" y="236" width="70" height="30" class="f-acc"/><rect x="130" y="236" width="70" height="30" class="f-mut"/><rect x="200" y="236" width="70" height="30" class="f-mut"/><rect x="270" y="236" width="70" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="95" y="76">cafe</text><text x="165" y="76">f00d</text><text x="235" y="76">dead</text><text x="305" y="76">beef</text>
    <text x="95" y="136">ffff</text><text x="165" y="136">ffff</text><text x="235" y="136">1234</text><text x="305" y="136">5678</text>
    <text x="95" y="196">0000</text><text x="165" y="196">0001</text><text x="235" y="196">0000</text><text x="305" y="196">0000</text>
    <text x="95" y="256">ffff</text><text x="165" y="256">0000</text><text x="235" y="256">0000</text><text x="305" y="256">ffff</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="360" y="68">z=0 f=0 → 4개</text><text x="360" y="86">movz #0xbeef ; movk ×3</text>
    <text x="360" y="128">z=0 f=2 → MOVN, 2개</text><text x="360" y="146">movn #0xa987 ; movk #0x1234,lsl #16</text>
    <text x="360" y="188">z=3 f=0 → MOVZ, 1개</text><text x="360" y="206">movz #0x1, lsl #32</text>
    <text x="360" y="248">z=2 f=2 동점 → MOVZ, 2개</text><text x="360" y="266">movz #0xffff ; movk #0xffff,lsl #48</text>
  </g>
  <text x="10" y="292" fill="currentColor">경고색 칸 = 첫 명령(MOVZ/MOVN), 강조색 칸 = MOVK, 회색 = 공짜. 마지막 줄은 ORR 로 1개 (연습 3).</text>
</svg>
```

**한 명령씩 추적 (lldb 실제 출력, `demo_trace.lldb`).** `cc -std=c11 -g -O0 solution.c solution.S shared_ref.S -o /tmp/ARM-B03_dbg && lldb -b -s demo_trace.lldb /tmp/ARM-B03_dbg` — 각 `si` 뒤의 stop 정보와 반복되는 `register read x0` 줄은 줄였다:

```text
ARM-B03_dbg`k_cafef00d_deadbeef:
->  0x10000466c <+0>:  mov    x0, #0xbeef               ; =48879
    0x100004670 <+4>:  movk   x0, #0xdead, lsl #16
    0x100004674 <+8>:  movk   x0, #0xf00d, lsl #32
    0x100004678 <+12>: movk   x0, #0xcafe, lsl #48
    0x10000467c <+16>: ret
(lldb) register read x0
      x0 = 0x0000000000000001
(lldb) si
(lldb) register read x0
      x0 = 0x000000000000beef
(lldb) si
      x0 = 0x00000000deadbeef
(lldb) si
      x0 = 0x0000f00ddeadbeef
(lldb) si
      x0 = 0xcafef00ddeadbeef
...
ARM-B03_dbg`k_ffffffff_12345678:
->  0x100004680 <+0>: mov    x0, #-0xa988              ; =-43400
    0x100004684 <+4>: movk   x0, #0x1234, lsl #16
(lldb) register read x0
      x0 = 0x0000000000000053
(lldb) si
      x0 = 0xffffffffffff5678
(lldb) si
      x0 = 0xffffffff12345678
```

| pc | 명령 (내가 쓴 것) | lldb 가 보여 주는 것 | x0 이후 |
|---|---|---|---|
| +0 | `movz x0, #0xbeef` | `mov x0, #0xbeef` | `0x0000_0000_0000_beef` |
| +4 | `movk x0, #0xdead, lsl #16` | 같음 | `0x0000_0000_dead_beef` |
| +8 | `movk x0, #0xf00d, lsl #32` | 같음 | `0x0000_f00d_dead_beef` |
| +12 | `movk x0, #0xcafe, lsl #48` | 같음 | `0xcafe_f00d_dead_beef` |
| +0 | `movn x0, #0xa987` | `mov x0, #-0xa988` | `0xffff_ffff_ffff_5678` |
| +4 | `movk x0, #0x1234, lsl #16` | 같음 | `0xffff_ffff_1234_5678` |

**디스어셈블러는 MOVZ/MOVN 을 `mov` 별칭(alias)으로 보여 준다.** `mov x0, #-0xa988` 은 MOVN: `~0xa987 = …ffff5678 = -0xa988` (2의 보수로 `-(0xa987+1)`). 크래시 덤프에서 음수 `mov` 를 보면 "MOVN 이구나, 위쪽은 전부 1" 이라고 읽으면 된다. 첫 `register read` 의 `0x1`, `0x53` 은 호출 직전 x0 에 남은 값이다 — MOVZ/MOVN 이 전부 덮어쓰므로 상관없다.

**진짜 어셈블러 인코딩 (`clang -c shared_ref.S && objdump -d`, 실제 출력 발췌):**

```text
       0: d2824680     	mov	x0, #0x1234             ; =4660
       4: d2b7dde1     	mov	x1, #0xbeef0000         ; =3203334144
       8: d2fffffe     	mov	x30, #-0x1000000000000  ; =-281474976710656
       c: f2dbd5a2     	movk	x2, #0xdead, lsl #32
      18: 92800003     	mov	x3, #-0x1               ; =-1
      20: 92ddb965     	mov	x5, #-0xedcb00000001    ; =-261456134144001
```

`d2fffffe` 를 손으로 풀면: `1 10 100101 11 1111111111111111 11110` = MOVZ, hw=3, imm16=0xffff, rd=30 → `0xffff << 48`. objdump 는 이걸 64비트 부호 있는 수로 읽어 `-0x1000000000000` 으로 보여 준다. `enc_movz(30, 0xFFFF, 3)` 가 정확히 이 워드를 만들어야 PASS.

**clang 은 어떻게 하나 (`demo_consts.c`, `clang -O2 -S`, 실제 출력 발췌):**

```text
_c_deadbeef:   mov  w0, #48879          ; =0xbeef
               movk w0, #57005, lsl #16
_c_neg_mostly: mov  x0, #-43400         ; =0xffffffffffff5678
               movk x0, #4660, lsl #16
_c_minus2:     mov  x0, #-2             ; =0xfffffffffffffffe
_c_uart:       mov  w0, #150994944      ; =0x9000000
_c_tie:        mov  x0, #-281474976645121 ; =0xffff00000000ffff   ← 1개! (b2507fe0 = ORR)
_c_rep32:      mov  x0, #22136 ; movk x0, #4660, lsl #16 ; orr x0, x0, x0, lsl #32
```

세 가지가 보인다. ① 위 32비트가 0 이면 clang 은 `w0` 에 쓴다 — **w 레지스터에 쓰면 x 의 위 32비트는 0 이 된다**(아키텍처 규칙)는 걸 이용 (명령 수는 같음). ② `0xffff00000000ffff` 는 MOVZ/MOVK 로 2개지만 회전된 1 덩어리(32개)라 **ORR 논리 즉시값 1개**. 이번 Part A 의 plan 은 MOVZ/MOVN/MOVK 만의 최소이므로 2 가 정답이고, ORR 까지 보는 비용은 연습 3. ③ 위·아래 32비트가 같은 값은 `orr x0, x0, x0, lsl #32` 트릭으로 3개 — 이 레슨 모델 밖(연습 3 에서 표시만).

### Part B — 논리 즉시값 = "반복되는, 회전된, 1 의 한 덩어리"

정의 (Arm ARM `DecodeBitMasks` 기준): 원소 크기 e ∈ {2, 4, 8, 16, 32, 64}, 원소 = 연속된 1 이 s 개 (1 ≤ s ≤ e−1) 를 e 비트 안에서 오른쪽으로 r 회전, 그 원소를 64/e 번 반복. s = e (전부 1) 는 예약이라 **~0 은 불가**, s ≥ 1 이라 **0 도 불가**.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">and w4, w5, #0xffff803f = 0x121158A4 — N:immr:imms 가 패턴을 만드는 방법</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="22" height="30" class="f-mut"/><rect x="42" y="40" width="44" height="30" class="f-mut"/>
    <rect x="86" y="40" width="132" height="30" class="f-mut"/><rect x="218" y="40" width="22" height="30" class="f-warn"/>
    <rect x="240" y="40" width="132" height="30" class="f-acc"/><rect x="372" y="40" width="132" height="30" class="f-ok"/>
    <rect x="504" y="40" width="108" height="30" class="f-mut"/><rect x="612" y="40" width="108" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="31" y="60">0</text><text x="64" y="60">00</text><text x="152" y="60">100100</text><text x="229" y="60">0</text>
    <text x="306" y="60">010001</text><text x="438" y="60">010110</text><text x="558" y="60">00101</text><text x="666" y="60">00100</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="31" y="88">sf</text><text x="64" y="88">opc=AND</text><text x="152" y="88">logical imm</text><text x="229" y="88">N</text>
    <text x="306" y="88">immr = 17 (r)</text><text x="438" y="88">imms: 0 + (s-1)=22</text><text x="558" y="88">Rn = w5</text><text x="666" y="88">Rd = w4</text>
  </g>
  <text x="20" y="124" fill="currentColor">N=0, imms 맨 위 비트 0 → e = 32, s = 23. ones(23) = 0x007fffff 를 32비트 안에서 ROR 17:</text>
  <g stroke="currentColor">
    <rect x="20" y="140" width="357" height="28" class="f-acc"/>
    <rect x="377" y="140" width="189" height="28" class="f-mut"/>
    <rect x="566" y="140" width="126" height="28" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="198" y="159">bit 31 … 15 : 1 × 17</text><text x="471" y="159">bit 14 … 6 : 0 × 9</text><text x="629" y="159">5 … 0 : 1 × 6</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="20" y="196">ROR 17 → bit 0..16 의 1 이 bit 15..31 로, bit 17..22 의 1 이 bit 0..5 로 = 0xffff803f</text>
    <text x="20" y="220">= ~0x7fc0 : STM32 PLLN 필드 [14:6] 를 지우는 마스크 (감싸는 1 덩어리도 OK)</text>
  </g>
  <text x="20" y="252" fill="currentColor">반복 예: 0x00ff00ff00ff00ff → e=16, s=8, r=0 → N=0, imms=100111, immr=0 (b2009c62).</text>
  <text x="20" y="276" fill="currentColor">불가 예: 0x1234 → e=64, 1 이 5개인데 4덩어리 → 어떤 회전도 ones(5) 가 아님.</text>
</svg>
```

**imms 의 위쪽 비트 = 원소 크기 표시** (`len = HighestSetBit(N:NOT(imms))`):

| N | imms | 원소 e | s 범위 | 회전 r | 패턴 수 e·(e−1) |
|---|---|---|---|---|---|
| 1 | `ssssss` | 64 | 1..63 | 0..63 | 4032 |
| 0 | `0sssss` | 32 | 1..31 | 0..31 | 992 |
| 0 | `10ssss` | 16 | 1..15 | 0..15 | 240 |
| 0 | `110sss` | 8 | 1..7 | 0..7 | 56 |
| 0 | `1110ss` | 4 | 1..3 | 0..3 | 12 |
| 0 | `11110s` | 2 | 1 | 0..1 | 2 |
| | | | | 합계 | **5334** (32비트 명령은 e ≤ 32 → **1302**) |

`s − 1` 을 imms 에 넣기 때문에 `ssss` 칸이 전부 1 이면 s = e (전부 1) → 예약(UNDEFINED). 하네스의 첫 줄 `64-bit valid=7680 distinct=5334 canonical=5334` 이 이 표를 확인한다: 8192 조합 중 유효 7680 (= 64 × 120 — immr 의 위쪽 비트는 `immr AND levels` 로 무시되므로 같은 값이 여러 번 나온다), 서로 다른 값 5334, immr < e 인 정규형만 세면 정확히 5334 (정규형 ↔ 값이 일대일). 그래서 "어셈블러가 내는 필드" 를 정답으로 정의할 수 있다.

**판정 알고리즘 추적: `0x00FF00FF00FF00FF`**

| 단계 | 값 | 판단 |
|---|---|---|
| e=64 → 32? | 아래 32 `0x00FF00FF` == 위 32 `0x00FF00FF` | 같음 → e=32 |
| e=32 → 16? | `0x00FF` == `0x00FF` | 같음 → e=16 |
| e=16 → 8? | 아래 8 `0xFF` vs 위 8 `0x00` | 다름 → **e=16** 확정 |
| s | popcount(`0x00FF`) | 8 |
| r | `ror16(0x00FF, r) == 0x00FF` | r=0 |
| 필드 | N=0, immr=0, imms=`10` + `0111` | `100111` = 39 |

어셈블러 출력 `b2009c62  orr x2, x3, #0xff00ff00ff00ff` 의 비트 21:16 = 0, 15:10 = 39 — 일치. `0xFF00` 은 e=64 까지 안 줄어들고(아래 32 `0xFF00` ≠ 위 32 `0`), s=8, `ror64(0xFF, 56) = 0xFF00` → N=1, immr=56, imms=7 (`92781c20`).

**진짜 어셈블러가 거절하는 모습** (`demo_bad_imm.S`, 실제 출력):

```text
$ clang -c demo_bad_imm.S -o /dev/null
demo_bad_imm.S:8:17: error: expected compatible register or logical immediate
    and x0, x0, #0x1234
demo_bad_imm.S:9:17: error: expected compatible register or logical immediate
    and w0, w0, #0
demo_bad_imm.S:10:17: error: expected compatible register or logical immediate
    orr x0, x0, #0xffffffffffffffff
demo_bad_imm.S:12:14: error: immediate must be an integer in range [0, 65535].
    movz x0, #0x12345
demo_bad_imm.S:13:18: error: expected 'lsl' with optional integer 0, 16, 32 or 48
    movk x0, #1, lsl #8
demo_bad_imm.S:14:18: error: expected 'lsl' with optional integer 0 or 16
    movz w0, #1, lsl #32
demo_bad_imm.S:15:13: error: expected compatible register or logical immediate
    mov x0, #0x12345678
```

같은 파일을 VM 의 GNU as 2.46 (`gcc -c demo_bad_imm.S`) 로:

```text
demo_bad_imm.S:8: Error: immediate out of range at operand 3 -- `and x0,x0,#0x1234'
demo_bad_imm.S:13: Error: shift amount must be a multiple of 16 at operand 2 -- `movk x0,#1,lsl#8'
demo_bad_imm.S:14: Error: shift amount out of range 0 to 16 at operand 2 -- `movz w0,#1,lsl#32'
demo_bad_imm.S:15: Error: immediate cannot be moved by a single instruction
```

`mov x0, #imm` 은 **별칭 하나로 끝날 때만** 받아 준다(MOVZ, MOVN, ORR-즉시값 중 하나). 어셈블러는 MOVK 를 덧붙여 주지 않는다 — 그건 컴파일러(또는 `ldr x0, =imm` 의사 명령)의 일이다.

**2^32 전수 데모** (`demo_exhaustive32.c`, 16 스레드, M6 에서 두 번 실행):

```text
$ cc -std=c11 -O2 -pthread demo_exhaustive32.c -o ex32 && ./ex32
checked 2^32 values: encodable=1302, roundtrip-bad=0, DecodeBitMasks set=1302, 10.5 s (16 threads)
checked 2^32 values: encodable=1302, roundtrip-bad=0, DecodeBitMasks set=1302, 9.7 s (16 threads)
```

true 라고 답한 값이 전부 디코드로 자기 자신에 돌아오고(⊆ 집합), 그 개수가 집합 크기와 같다(= 집합) → 32비트는 **모든 입력**에서 양방향 증명. 64비트는 2^64 라 하네스의 정방향 5334 전수 + 역방향 표본(1비트 뒤집기 34만 개 + 작은 값 + 무작위)으로 대신한다. 시간은 다른 작업과 같이 도는 M6 기준이라 참고만.

### Part C — 펌웨어 마스크 비용

`x & mask` 비용 규칙: 논리 즉시값이면 `and #imm` **1개**, 아니면 상수를 만들고(MOVZ/MOVN + MOVK) `and xd, xn, xm` 으로 **+1**. clang -O2 실제 출력과 대조 (`demo_consts.c`):

| 마스크 | 폭 | 논리 즉시값? | clang -O2 실제 출력 | 비용 |
|---|---|---|---|---|
| `0x0000FF00` | 64 | ○ | `and x0, x0, #0xff00` | 1 |
| `0x00FF00FF00FF00FF` | 64 | ○ (e=16) | `and x0, x0, #0xff00ff00ff00ff` | 1 |
| `0x1234` | 64 | ✗ | `mov w8, #4660 ; and x0, x0, x8` | 2 |
| `0xCAFEF00DDEADBEEF` | 64 | ✗ | `mov x8 ; movk ×3 ; and` | 5 |
| `0x12345678` | 32 | ✗ | `mov w8, #22136 ; movk w8, #4660, lsl #16 ; and w0, w0, w8` | 3 |
| `~0x0000FF00` | 32 | ○ | `and w0, w0, #0xffff00ff` | 1 |
| `0x00007FC0` (PLLN) | 32 | ○ | `and w0, w0, #0x7fc0` | 1 |
| `0x00000005` | 32 | ✗ (1 이 떨어짐) | `mov w8, #5 ; and w0, w0, w8` | 2 |
| `0xA5A5A5A5` | 32 | ✗ | `mov w8, #42405 ; movk w8, #42405, lsl #16 ; and` | 3 |
| `0xFFFF1234` | 32 | ✗ | `mov w8, #-60876 ; and w0, w0, w8` (MOVN) | 2 |

펌웨어 감각: **CMSIS 스타일 `_Msk` (연속 비트 필드, `GENMASK`)는 항상 1개**다(연습 5에서 2080개 전부 확인). 비용이 드는 건 흩어진 비트 묶음(`0x5`, `0xA5A5A5A5`)과 매직 값(`0x12345678`). 그리고 마스크와 그 반전은 같이 인코딩 가능(회전된 1 덩어리의 보수도 회전된 1 덩어리) → `reg & ~MSK` 의 clear 도 1개. 이게 `BIC #imm` 이 AND 의 별칭인 이유다.

**PLLN 패치 (`pllcfgr_set_plln168`)** — 손으로 쓴 asm 은 3개:

```text
and  w0, w0, #0xffff803f   // clear [14:6]   (논리 즉시값, 위 그림)
movz w8, #0x2a00           // 168 << 6 = 0x2a00 = 0b0010_1010_0000_0000 → 1 이 흩어져 논리 즉시값 불가
orr  w0, w0, w8
```

같은 식을 C 로 쓰면 clang 은 `mov w8, #168 ; bfi w0, w8, #6, #9` **2개**로 만든다 (실제 출력). BFI(bit-field insert) 는 ARM-B06 담당 — "필드 패치 = BFI" 를 거기서 다룬다. 하네스는 asm 첫 워드가 정말 32비트 AND-즉시값(`(w & 0xFF800000) == 0x12000000`)인지 기계어로 확인한다.

**리눅스(ELF)에서도 같은 파일:** `.S` 는 C 전처리기를 거치므로 `#ifdef __APPLE__` 로 심볼 규칙을 나눴다. 맥은 `_k_deadbeef` (밑줄), ELF 는 `k_deadbeef` + `.type k_deadbeef, %function` + `.size k_deadbeef, .-k_deadbeef`. 매크로 한 줄에 여러 문장을 넣지 않는다 — Apple arm64 어셈블러에서 `;` 는 **주석**, GNU as aarch64 에서는 **문장 구분자**라 뜻이 갈린다. Lima VM (aarch64 Ubuntu, gcc 15.2 / clang) 에서 실제로 빌드·실행:

```text
$ limactl shell elinux -- bash -lc 'cd <ROOT>/lessons/ARM/ARM-B03 && for v in starter solution practice; do
    gcc -std=c11 -Wall -Wextra -Werror -O2 $v.c $v.S shared_ref.S -o /tmp/ARM-B03_vm_$v &&
    /tmp/ARM-B03_vm_$v > /tmp/ARM-B03_vm_$v.out; echo "$v exit=$? $(tail -1 /tmp/ARM-B03_vm_$v.out)"; done;
    readelf -s /tmp/ARM-B03_vm_solution | grep -E " (k_deadbeef|ref_logic)$"'
starter exit=1 9/78 passed
solution exit=0 78/78 passed
practice exit=0 24/24 passed
   106: 000000000000326c    12 FUNC    GLOBAL DEFAULT   12 k_deadbeef
   115: 00000000000032f8    52 OBJECT  GLOBAL DEFAULT   12 ref_logic
```

VM 의 clang 으로도 solution 78/78, gcc + ASan/UBSan 도 78/78 (실행 확인).

`.size` 덕분에 `k_deadbeef` 가 12바이트(명령 3개)로, `ref_logic` 은 52바이트(13 워드) OBJECT 로 기록된다 — 맥 Mach-O 에는 이 정보가 없다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_ref.S -o sol && ./sol
# DecodeBitMasks: 64-bit valid=7680 distinct=5334 canonical=5334 | 32-bit valid=3648 distinct=1302 canonical=1302
# 78/78 passed      (M6 에서 0.2 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   solution.c solution.S shared_ref.S -o sol_san && ./sol_san      # 78/78 passed, 경고 없음
```

UB 메모: 이 레슨의 C 코드에는 UB 가 생길 자리가 거의 없지만 두 군데를 조심했다. `1ull << 64` 는 UB 라 `e == 64` 와 `s + 1 == 64` 는 따로 분기했고, `(1ull << s) - 1` 은 s ≤ 63 이 보장될 때만(v ≠ ~0 확인 후) 쓴다. 하드웨어 `LSL` 은 시프트 양을 64 로 나눈 나머지로 쓰지만 C 추상 기계는 그걸 약속하지 않는다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `hw` 에 비트 시프트 양(16, 32)을 넣음 | hw 는 칸 번호 0..3, 시프트 = 16·hw | `enc_movk(rd, imm, 2)` = `lsl #32` |
| MOVK 로 시작 | 나머지 칸이 레지스터의 옛 값(쓰레기) 그대로 | 첫 명령은 MOVZ 또는 MOVN |
| MOVN 의 imm16 에 그 칸 값을 그대로 | MOVN 은 결과를 반전 → 칸 값은 `~imm16` | `imm16 = ~h` (0x5678 → 0xa987) |
| `movn w0, #0` 이 x0 = -1 이라고 생각 | w 쓰기는 위 32비트를 0 으로 → x0 = `0x00000000ffffffff` | 64비트 -1 은 `movn x0, #0` |
| 0 이나 ~0 을 `and #imm` 에 기대 | s=0 / s=e 는 표현 불가 (예약) | 컴파일러는 `x & 0 → mov #0`, `x & ~0 → x` 로 접는다 |
| 32비트 판정을 64비트 함수에 그대로 | `0x0000FF00` 이 64비트로 e=64 라 "N=1" — 32비트 명령에선 N=1 이 UNDEFINED | 값을 두 번 이어 붙이고 N==0 확인 |
| immr 에 r + e (같은 값) | 디코더는 `immr AND levels` 로 같은 패턴을 만들지만 어셈블러 정규형과 비트가 다름 | 0 ≤ r < e |
| `1ull << e` 를 e=64 에서 | C 에서 UB (하드웨어는 mod 64) | `e == 64 ? ~0ull : (1ull << e) - 1` |
| 디버거 브레이크포인트를 건 채 기계어 읽기 테스트 | lldb 소프트웨어 브레이크포인트가 코드 바이트를 BRK 로 바꿔 둔다 → `FAIL ... code_matches_plan(k_cafef00d_deadbeef, ...)` (실제로 봄. `breakpoint disable` 후엔 78/78) | 코드-읽기 검사 땐 브레이크포인트 해제 / 하드웨어 BP |

## 8. 면접 Q&A

<details>
<summary>Q1. AArch64 에서 64비트 상수나 주소를 레지스터에 넣는 방법을 모두 말해 보세요.</summary>

① **MOVZ/MOVN + MOVK** 최대 4개 (상수에 가장 흔함). ② **ORR 논리 즉시값** — `mov x0, #0x5555555555555555` 는 `orr x0, xzr, #imm` 1개. ③ **리터럴 풀** — `ldr x0, =0xcafef00ddeadbeef` 를 맥 clang 으로 조립하면 실제로 `58000080  ldr x0, 0x10` + 코드 뒤의 8바이트 `deadbeef cafef00d` 가 생긴다 (작은 값 `ldr x1, =0x1234` 는 어셈블러가 `mov x1, #0x1234` 로 바꿔 줌 — 실측). 메모리 로드 1번이라 캐시 미스면 느리다. ④ 주소는 **ADRP + ADD** (`@PAGE`/`@PAGEOFF`, ±4 GB PC 상대, 위치 독립 — ARM-A09). 꼬리: "절대 주소를 MOVZ/MOVK 로 넣으면?" → 재배치가 4개(ELF 기준 `R_AARCH64_MOVW_UABS_G0..G3`) 필요하고 위치 독립이 아니라 PIE/ASLR 과 안 맞는다 (기억 기반, 확인 권장); 그래서 펌웨어 링크 시 고정 주소인 MMIO 베이스(예: `0x09000000`)만 MOVZ 로 넣는 게 자연스럽다.

</details>

<details>
<summary>Q2. 이 두 줄 뒤 x0 는? `mov x0, #-0xa988` / `movk x0, #0x1234, lsl #16`</summary>

음수 `mov` 는 MOVN 이다: `-0xa988 = ~0xa987` → x0 = `0xffff_ffff_ffff_5678`. MOVK 가 비트 31:16 만 0x1234 로 바꿔 → `0xffff_ffff_1234_5678`. 크래시 덤프에서 이런 쌍은 대개 작은 음수 오프셋이나 "위쪽이 전부 1 인" 커널 주소다. 꼬리: "objdump 가 `mov x30, #-0x1000000000000` 로 보여 주면?" → MOVZ hw=3 imm=0xffff = `0xffff000000000000` 를 부호 있는 수로 출력한 것뿐.

</details>

<details>
<summary>Q3. 왜 MOVN 이 따로 있나요? MOVZ + MOVK 만으로 충분하지 않나요?</summary>

표현력은 충분하지만 **길이**가 다르다. `-2` 는 MOVZ/MOVK 로 4개, MOVN 으로 1개. 음수 상수·`~mask`·커널 상위 주소처럼 위쪽이 0xFFFF 로 찬 값이 흔해서, "나머지를 1 로 채우는 첫 명령" 하나로 최악을 4 → 1 로 줄인다. 규칙: 0xFFFF 칸이 0 칸보다 많으면 MOVN. 결과 명령 수 = max(1, 4 − max(z, f)).

</details>

<details>
<summary>Q4. 어떤 값이 AND/ORR 즉시값이 될 수 있나요? 몇 개나 되나요? 0 과 -1 은요?</summary>

e ∈ {2,4,…,64} 비트 원소에 연속 1 이 s 개(1 ≤ s < e), 그것을 r 회전해 64/e 번 반복한 값. 개수 Σ e(e−1) = **5334** (64비트), e ≤ 32 만 = **1302** (32비트) — 하네스와 2^32 전수 데모에서 실측. 0 (s=0) 과 전부 1 (s=e) 은 빠진다: `x & 0`, `x | ~0` 같은 건 필요 없는 연산이라 그 인코딩 공간을 아낀 것 (`imms` 의 해당 자리는 예약/UNDEFINED). 꼬리: "`and x0, x0, #0x1234` 쓰면?" → 어셈블러 에러 (`expected compatible register or logical immediate`); 컴파일러는 `mov w8, #0x1234 ; and x0, x0, x8`.

</details>

<details>
<summary>Q5. ADD 즉시값과 AND 즉시값은 같은 규칙인가요?</summary>

아니다. ADD/SUB/CMP 는 **12비트 부호 없는 값, 선택적으로 `lsl #12`** (0..4095 또는 그 ×4096). `add x0, x0, #0x1000` → `91400400 add x0, x0, #0x1, lsl #12`, `#0xfff000` 도 OK, `#0x1001` 은 "integer in range [0, 4095]" 에러 (맥 clang 실측). 논리 명령은 반대로 "산술적으로 작은 값" 이 아니라 "비트 패턴 모양" 이 중요하다 — 0xFF00 은 AND 엔 되고 ADD 엔 lsl 없이 안 되며, 0x1001 은 둘 다 안 된다. 이유: 덧셈 상수는 작은 값이 흔하고, 논리 상수는 마스크가 흔하기 때문 (Arm 의 설계 의도 — 출처 기반 추정, 확인 권장).

</details>

<details>
<summary>Q6. AArch32(A32/Thumb-2) 와 비교하면 즉시값 규칙이 어떻게 다른가요?</summary>

A32 데이터 처리 즉시값은 **8비트 값을 짝수만큼 회전**(imm8 ROR 2·rot4) — 0xFF000000 은 되고 0x101 은 안 됨. Thumb-2 의 "modified immediate" 는 거기에 `0x00XY00XY`, `0xXY00XY00`, `0xXYXYXYXY` 반복 형태가 추가된다. 32비트 임의 상수는 **MOVW/MOVT** (하위/상위 16비트) 두 개 — AArch64 의 MOVZ/MOVK 의 조상이다. Cortex-M 디스어셈블리에서 `movw r0, #0x5678 ; movt r0, #0x1234` 를 보면 같은 원리. (Arm ARM ARMv7 기준 기억 — ARM-B12/B13 에서 실제 출력으로 확인 예정.)

</details>

<details>
<summary>Q7. `mov w0, #0xbeef ; movk w0, #0xdead, lsl #16` 가 64비트 함수 반환값으로 올바른 이유는?</summary>

AArch64 에서 W 레지스터에 쓰는 모든 명령은 X 의 위 32비트를 0 으로 만든다(zero-extend). 그래서 반환형이 `uint64_t` 여도 값이 32비트 안이면 clang 이 `w0` 로 만든다 (`_c_deadbeef` 실제 출력). 반대로 `int` 인 음수를 64비트로 쓰려면 이게 오히려 함정 — `movn w0, #0` 는 x0 = `0x00000000ffffffff` 이지 −1 이 아니다. ABI 에서 int 인자/반환의 위 32비트가 무엇을 보장하는지는 ARM-B01 참고.

</details>

<details>
<summary>Q8. 상수 로드는 몇 사이클이나 걸리나요? 리터럴 풀이 나을 때는?</summary>

MOVZ/MOVK 는 레지스터만 쓰는 단순 ALU 명령이라 보통 1사이클급이고, 일부 코어는 MOVZ+MOVK 쌍을 붙여(fusion) 처리한다고 알려져 있다 (마이크로아키텍처마다 다름 — 단정하지 말 것, ARM-M03 에서 측정). 리터럴 풀은 명령 1개 + 데이터 8바이트라 **코드 크기**가 4개 MOVK 보다 작을 수 있지만 D-cache 로드가 필요하다. 펌웨어(작은 I-cache, 코드 크기 민감)에선 크기, 핫 루프에선 지연을 기준으로 고른다; 실제로 컴파일러는 상수를 루프 밖으로 끌어올려(hoist) 한 번만 만든다.

</details>

## 9. 추가 연습

### 연습 1 — w 레지스터판: 32비트 인코딩과 계획

`enc_mov_wide32(op, rd, imm16, hw)` (sf=0, hw 0..1) 와 `movz_movk_plan32(uint32_t v, insn out[2])` (최대 2개). `shared_ref.S` 의 `_ref_movs32` 4워드와 비교.

<details>
<summary>답 보기</summary>

sf=0 이라 base 가 `0x12800000 | opc << 29` (MOVN `0x12800000`, MOVZ `0x52800000`, MOVK `0x72800000`). 계획은 칸이 두 개뿐: 한 칸이 0 이면 MOVZ 1개, 한 칸이 0xFFFF 면 MOVN 1개(`movn w7, #0x5678` → `0xffffa987`, 실제 워드 `128acf07`), 아니면 MOVZ+MOVK 2개. 무작위 10만 개를 실행해 값과 개수 확인.

</details>

### 연습 2 — 논리 즉시값 명령 전체 인코더

`enc_logical_imm(opc, sf, rd, rn, N, immr, imms)` 와 `assemble_logical(opc, sf, rd, rn, v, &word)` — 인코딩 불가면 false (어셈블러 에러 자리). `_ref_logic` 13워드를 **비트 단위로** 재현.

<details>
<summary>답 보기</summary>

`sf<<31 | opc<<29 | 0x12000000 | N<<22 | immr<<16 | imms<<10 | rn<<5 | rd`, opc: AND=0, ORR=1, EOR=2, ANDS=3 (`tst` 는 `ands xzr, …` 의 별칭). 필드는 본문 `is_logical_imm64/32` 가 그대로 준다. `ands x6, x7, #0x8000000000000001` → `f24104e6` (N=1, immr=1, imms=1: 2개짜리 1 덩어리가 bit 63→0 으로 감김).

</details>

### 연습 3 — ORR 즉시값까지 고려한 상수 비용

`best_mov_cost64(v)`: 논리 즉시값이면 1 (`orr xd, xzr, #imm`), MOVZ/MOVN 계획이 2 이하면 그 값, 아니면 "5334 패턴 중 v 와 **한 칸만** 다른 것" 이 있으면 2 (ORR + MOVK), 없으면 계획 개수. clang 실제 출력 개수와 비교.

<details>
<summary>답 보기</summary>

`0x00FF00FF00FF1234` → 2 (clang: `mov x0, #0xff00ff00ff00ff ; movk x0, #4660` 실측과 동일), `0x5555…` → 1, `0xFFFF00000000FFFF` → 1. 예외: `0x1234567812345678` 은 이 모델로 4 지만 clang 은 `orr x0, x0, x0, lsl #32` 로 **3** — 컴파일러(LLVM `AArch64ExpandImm`)는 이런 트릭을 더 안다. 비용은 항상 계획 이하인지 무작위 2000개로 확인.

</details>

### 연습 4 — asm: `mov` 별칭이 ORR 로 바뀌는 것 확인

`practice.S` 에 `k_5555` (`mov x0, #0x5555555555555555`) 와 `k_00ff_1234` (`orr x0, xzr, #0x00ff00ff00ff00ff ; movk x0, #0x1234`) 를 쓰고, 하네스가 첫 워드가 `ORR (64-bit, Rn=xzr)` 인지 기계어로 확인.

<details>
<summary>답 보기</summary>

마스크 `(w & 0xFF8003E0) == 0xB20003E0` = sf=1, opc=01(ORR), `100100`, N 은 무시, Rn=31. `k_5555` 첫 워드는 `b200f3e0` (N=0, immr=0, imms=0x3C → e=2, s=1). MOVZ/MOVK 로는 4개 들 상수가 1개.

</details>

### 연습 5 — GENMASK(h, l) 는 항상 논리 즉시값?

리눅스 `GENMASK(h, l)` / CMSIS `_Msk` 같은 연속 마스크 2080개(64비트) + 528개(32비트)를 전부 판정. 예외와 필드 공식은?

<details>
<summary>답 보기</summary>

연속 1 덩어리 = e=64, s = h−l+1, r = (64−l) mod 64 → N=1, imms = h−l, immr = (64−l) mod 64. 유일한 예외는 `GENMASK(63, 0)` = ~0 (s=e). 32비트도 `GENMASK(31, 0)` 만 예외. 즉 드라이버의 필드 clear/test 는 거의 항상 명령 1개 — 비용은 "값 쓰기" 쪽(흩어진 비트 상수)에서 생긴다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

<!-- code: practice.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_ref.S -o pr && ./pr   # 24/24 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   practice.c practice.S shared_ref.S -o pr_san && ./pr_san                              # 24/24 passed
```

관찰용 파일 정리:

```bash
clang -c demo_bad_imm.S -o /dev/null                                  # 일부러 에러 9개 (VM: gcc -c ...)
clang -O2 -S -fno-asynchronous-unwind-tables demo_consts.c -o -        # 상수/마스크 → clang 출력
cc -std=c11 -O2 -pthread demo_exhaustive32.c -o ex32 && ./ex32         # 2^32 전수 (~10 s)
cc -std=c11 -g -O0 solution.c solution.S shared_ref.S -o /tmp/ARM-B03_dbg && lldb -b -s demo_trace.lldb /tmp/ARM-B03_dbg
```

## 10. 핵심 정리

- A64 명령은 32비트 고정 → 64비트 상수는 **MOVZ/MOVN + MOVK** (16비트 × hw 0..3, 최대 4개), **ORR 논리 즉시값** (1개), **리터럴 풀**, 주소는 **ADRP+ADD**.
- 명령 수 = max(1, 4 − max(#0x0000 칸, #0xFFFF 칸)). 0xFFFF 칸이 더 많으면 MOVN (imm16 = ~칸 값). 첫 명령은 절대 MOVK 가 아니다.
- 디스어셈블리 `mov xd, #-N` = MOVN, `mov xd, #큰값` 은 MOVZ 또는 ORR — 기계어로 확인 (`d2…` MOVZ, `92…` MOVN, `f2…` MOVK, `b2…` ORR-imm).
- 논리 즉시값 = e비트 원소(2..64)에 회전된 1 덩어리(s 개, 1 ≤ s < e) 반복. 64비트 5334개, 32비트 1302개. 0 과 ~0 은 불가.
- 필드: `N:immr:imms`, imms 위쪽 비트가 원소 크기(`0…`/`10…`/`110…`), 아래가 s−1, immr = 회전 r.
- 연속 마스크(GENMASK, `_Msk`)와 그 보수는 항상 1개 → clear/test 는 싸고, 흩어진 비트 상수(0x5, 0x12345678)를 만들 때 비용이 든다.
- W 레지스터 쓰기는 위 32비트를 0 으로 — 컴파일러가 32비트 상수에 `mov w0` 을 쓰는 이유이자 `movn w0, #0 ≠ -1` 함정.

## 참고자료

- [Arm A64 ISA — MOVZ](https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions/MOVZ--Move-wide-with-zero-) · [MOVK](https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions/MOVK--Move-wide-with-keep-) · [MOVN](https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions/MOVN--Move-wide-with-NOT-) · [AND (immediate)](https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions/AND--immediate---Bitwise-AND--immediate--)
- Arm Architecture Reference Manual for A-profile (DDI 0487) — `DecodeBitMasks()` 의사코드 (shared/functions 의 bitmask 디코드), "Move wide (immediate)" / "Logical (immediate)" 인코딩 표
- [Dougall Johnson — Bit-twiddling: optimising AArch64 logical immediate encoding (and decoding)](https://dougallj.wordpress.com/2021/10/30/bit-twiddling-optimising-aarch64-logical-immediate-encoding-and-decoding/)
- LLVM `AArch64AddressingModes.h` (`processLogicalImmediate`, `encodeLogicalImmediate`) · `AArch64ExpandImm.cpp` (ORR+MOVK, `orr lsl #32` 트릭)
- [Apple — Writing ARM64 code for Apple platforms](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms) (Mach-O 심볼 `_` 접두, x18 예약 — ARM-B01)
- 관련 레슨: ARCH-B08 (인코딩 기초), C-B04 (필드 마스크), ARM-B01 (AAPCS64/Apple 차이), ARM-B06 (BFI/UBFX), ARM-B12 (Thumb-2 인코딩), ARM-A09 (ADRP/GOT)
