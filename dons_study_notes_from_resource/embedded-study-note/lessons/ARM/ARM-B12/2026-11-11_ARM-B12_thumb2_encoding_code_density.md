---
id: ARM-B12
time: 60분
updated: 2026-11-11
---
# ARM-B12 Thumb-2 인코딩 관찰: 16비트 vs 32비트 명령

## 학습 목표

- Thumb 명령이 2바이트인지 4바이트인지를 **첫 halfword 의 상위 5비트**만 보고 판정하고(`thumb_insn_len`), 그 규칙을 objdump 로 65536개 값 전부에 대해 확인한다.
- 같은 C 파일을 Cortex-M0(`thumbv6m`) · Cortex-M4(`thumbv7em`) · ARMv7-A ARM 모드(A32) · AArch64 로 `-Os` 컴파일하고, `llvm-objdump -dr` 텍스트를 파싱해 함수별 바이트 · 명령 수 · 2/4바이트 분포 · 코드 속 데이터(리터럴 풀, 분기표)를 숫자로 비교한다.
- objdump 의 `.w`/`.n` 표시, IT 블록, `$t`/`$d` 매핑 심볼, `__aeabi_uidiv` 같은 런타임 헬퍼 호출을 덤프에서 읽어낸다.
- "Thumb-2 가 A32 보다 몇 % 작다", "AArch64 에는 왜 Thumb 이 없나", "M0 에서 나눗셈 한 줄이 flash 를 얼마나 먹나" 를 **이 맥에서 잰 숫자로** 말한다.

## 1. 문제

"이 Cortex-M 크래시 덤프에서 `f8d0 c000` 은 명령 하나예요, 둘이에요?" — Thumb 코드는 길이가 섞여 있어서 PC 주변 바이트를 손으로 읽으려면 길이 규칙을 알아야 한다. flash 가 32 KB 인 MCU 에 부트로더를 넣거나, SSD 컨트롤러의 Cortex-R/M 코어 펌웨어를 ITCM/ROM 에 맞출 때 "Thumb-2 로 빌드하면 얼마나 줄어드나"는 실제 숫자로 답해야 하는 질문이다.

Arm 은 명령 길이가 다른 세 가지 명령어 집합을 갖고 있다.

| 이름 | 길이 | 어디서 | 이 레슨의 타깃 |
|---|---|---|---|
| A32 (예전 이름 "ARM") | 고정 32비트 | Cortex-A/R 의 AArch32 | `--target=armv7a-none-eabi -marm` |
| T32 (Thumb / Thumb-2) | 16비트 + 32비트 섞임 | Cortex-M 전부, Cortex-A/R 의 AArch32 | `--target=thumbv7em-none-eabi` (Cortex-M4), `--target=thumbv6m-none-eabi` (Cortex-M0, 32비트 명령은 BL·MRS·MSR·DMB 등 소수) |
| A64 | 고정 32비트 | AArch64 (Apple M 시리즈, Cortex-A53/A78…) | `--target=aarch64-none-elf` |

Thumb 의 길이 규칙은 첫 halfword(16비트 묶음)의 **[15:11] 다섯 비트**에 들어 있다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="22" fill="currentColor" font-weight="bold">Thumb 명령 길이 = 첫 halfword(hw1) 의 [15:11] 로 결정</text>
<rect x="20" y="40" width="200" height="34" class="f-ok" stroke="currentColor"/>
<text x="30" y="62" fill="currentColor" font-family="monospace">hw1 (16-bit 명령 전체)</text>
<text x="240" y="62" fill="currentColor">[15:11] = 00000 … 11100 → 2바이트</text>
<rect x="20" y="88" width="200" height="34" class="f-acc" stroke="currentColor"/>
<rect x="220" y="88" width="200" height="34" class="f-mut" stroke="currentColor"/>
<text x="30" y="110" fill="currentColor" font-family="monospace">hw1 [15:11]=111xx</text>
<text x="240" y="110" fill="currentColor" font-family="monospace">hw2</text>
<text x="440" y="110" fill="currentColor">11101 / 11110 / 11111 → 4바이트</text>
<text x="10" y="160" fill="currentColor" font-weight="bold">예: thumbv7em 덤프 0xa: "ea22 0101  bic.w r1, r2, r1"</text>
<g font-family="monospace" fill="currentColor">
<text x="20" y="188">메모리 주소</text><text x="160" y="188">0x0a</text><text x="230" y="188">0x0b</text><text x="300" y="188">0x0c</text><text x="370" y="188">0x0d</text>
<text x="20" y="214">바이트</text>
</g>
<rect x="150" y="196" width="140" height="26" class="f-acc" stroke="currentColor"/>
<rect x="290" y="196" width="140" height="26" class="f-mut" stroke="currentColor"/>
<g font-family="monospace" fill="currentColor">
<text x="165" y="214">22</text><text x="235" y="214">ea</text><text x="305" y="214">01</text><text x="375" y="214">01</text>
<text x="450" y="214">← 각 halfword 는 little-endian</text>
<text x="20" y="246">hw1 = 0xea22 = 1110 1010 0010 0010 → [15:11] = 11101 → 4바이트</text>
<text x="20" y="268">hw1 = 0x6802 (ldr r2,[r0])   [15:11] = 01101 → 2바이트</text>
</g>
<rect x="20" y="282" width="560" height="22" class="f-ok" stroke="currentColor"/>
<rect x="580" y="282" width="140" height="22" class="f-acc" stroke="currentColor"/>
<rect x="545" y="282" width="35" height="22" class="f-warn" stroke="currentColor"/>
<g font-family="monospace" fill="currentColor" font-size="12">
<text x="24" y="298">0x0000</text><text x="498" y="298">0xE000</text><text x="584" y="298">0xE800 … 0xFFFF</text>
<text x="20" y="322">hw1 &lt; 0xE800 ⇔ 2바이트.  0xE000–0xE7FF(작은 칸) = 16비트 무조건 B — "111" 만 보면 틀린다</text>
</g>
</svg>
```

입력은 실제 펌웨어 모양의 작은 함수 9종(10개 심볼)이다: MMIO 비트 set/clear, 필드 갱신, UART 링 버퍼 push, 비트 단위 CRC-8, switch 상태 기계(프레임 파서), 24 B 설정 구조체 복사, 16비트 포화 덧셈, 바이트 memcpy, ADC 평균(진짜 나눗셈).

<!-- code: firmware.c -->

### Part A — 길이 규칙과 한 줄 파서

- `unsigned thumb_insn_len(uint16_t hw1)` — 2 또는 4. 검증: (1) `thumb_sweep.txt` = 65536개 hw1 을 전부 `.inst.n` 로 박아 objdump 에 디코드시킨 결과의 구간 요약과 전수 비교, (2) 4개 덤프의 **모든 명령 줄**에서 objdump 가 찍은 바이트 수와 비교 (A32/A64 는 전부 4).
- `int parse_line(const char *s, line_t *o)` — `llvm-objdump -dr` 한 줄을 함수 머리 / 명령 / 데이터 / 그 밖(리로케이션, 빈 줄, 머리말) 으로 나누고 주소 · 바이트 수 · 16진 묶음 수 · 첫 묶음 값 · 니모닉을 뽑는다.

### Part B — 함수별 통계와 교차 검증

- `int analyze_dump(FILE *fp, dump_t *d)` — 함수마다 바이트(명령+데이터), 명령 수, 2B/4B 개수, 데이터 바이트, `.w` 표시 개수, 그리고 합계.
- 경계 사례: `tbb` 뒤의 인라인 분기표(`.word`), Cortex-M0 의 리터럴 풀, A32 `ldr pc, [r3, r2, lsl #2]` 뒤 주소표, 루프 정렬용 `nop`, 4바이트인데 `.w` 가 안 붙은 명령, 리로케이션 줄.
- 교차 검증: 함수 바이트 == `llvm-nm -S` 크기, 합계 == `llvm-size -A` 의 `.text`, `2·n16 + 4·n32 + data == bytes`.

### Part C — 비교 표와 펌웨어 판단

- `int saving_permille(unsigned small, unsigned big)` — 몇 ‰ 작아졌나 (반올림, 음수 가능).
- `int parse_reloc(const char *s, char type[32], char sym[64])` — 리로케이션 줄에서 `R_ARM_THM_CALL __aeabi_uidiv` 를 뽑아 **어떤 타깃이 나눗셈을 라이브러리로 부르나** 확인.
- 함수 × 타깃 크기 표, 타깃별 평균 명령 길이 표를 찍고 "M0 냐 M4 냐", "A32 대신 Thumb-2 로 가면 몇 % 줄어드나" 를 판단한다.

## 2. Problem (English)

> **Measure ARM code density from disassembly.**
>
> You are given `llvm-objdump -dr` listings of the same C file compiled with `-Os` for Cortex-M0 (`thumbv6m`), Cortex-M4 (`thumbv7em`), ARMv7-A ARM state (`armv7a -marm`) and AArch64.
>
> 1. Write `unsigned thumb_insn_len(uint16_t hw1)` that returns 2 or 4 given only the first halfword of a T32 instruction. It must agree with the disassembler for all 65536 values.
> 2. Write `int parse_line(const char *s, line_t *o)` that classifies a listing line as a function header, an instruction, inline data (`.word`, literal pool, branch table) or other (relocations, headers), extracting address, byte count, the first encoding unit and the mnemonic.
> 3. Aggregate per function: total bytes, instruction count, 16-bit/32-bit histogram, data bytes. Cross-check against `llvm-nm -S` symbol sizes and the `.text` size from `llvm-size -A`.
> 4. Produce a comparison table (function × target) and the percentage saving of Thumb-2 over A32 (`saving_permille`, rounded half away from zero). List compiler runtime helper calls from `*_CALL` relocations.
>
> Example: `thumb_insn_len(0x4770) == 2` (`bx lr`), `thumb_insn_len(0xe7fe) == 2` (`b .`), `thumb_insn_len(0xf8d0) == 4` (`ldr.w`); `saving_permille(320, 504) == 365`.
>
> Follow-ups: Why is Thumb-2 smaller but not always? What do `.w`/`.n` mean? Why did AArch64 drop a compressed encoding? What does a single `/` cost on Cortex-M0?

## 3. 시작 코드

데이터 파일은 이미 레슨 디렉터리에 있다 (`make_dumps.sh` 가 만든 실제 출력, 다시 만들어도 같은 컴파일러면 같은 내용).

| 파일 | 내용 |
|---|---|
| `dump_thumbv6m.txt` · `dump_thumbv7em.txt` · `dump_armv7a.txt` · `dump_aarch64.txt` | `llvm-objdump -dr fw_<t>.o` (각 6 KB 안팎) |
| `syms_<t>.txt` | `llvm-nm -S -n --defined-only` (주소, 크기) |
| `sizes.txt` | `llvm-size -A` / Berkeley 형식 / `-O2 -Os -Oz` 별 `.text` |
| `thumb_sweep.txt` | 65536개 hw1 스윕 요약 (Part A 정답지) |

<!-- code: make_dumps.sh -->

```bash
bash make_dumps.sh        # (선택) 덤프 다시 만들기 — 2초, 실행 불필요, 오브젝트만
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/ARM-B12_starter && /tmp/ARM-B12_starter
```

주의: `/usr/local/bin/llvm-objdump` 는 **LLVM 3.2** 라서 Thumb 을 못 읽는다 (7. 자주 하는 실수). 스크립트는 `/Library/Developer/CommandLineTools/usr/bin/llvm-objdump` (Apple LLVM 21) 를 쓴다. 프로그램은 소스 파일 옆(`__FILE__` 의 디렉터리)에서 데이터 파일을 찾고, 없으면 현재 디렉터리, 또는 `argv[1]` 로 디렉터리를 준다.

<!-- code: starter.c -->

처음 실행 결과 (실제 출력, 앞부분과 끝):

```text
FAIL starter.c:257  thumb_insn_len(0xe800) == 4
FAIL starter.c:258  thumb_insn_len(0xe8df) == 4
…
  thumbv6m sweep: 6144/65536 mismatches
FAIL starter.c:265  bad == 0 && cov == 65536
  thumbv7em sweep: 6144/65536 mismatches
FAIL starter.c:265  bad == 0 && cov == 65536
FAIL starter.c:269  parse_line("00000054 <crc8>:", &L) == LN_FUNC && L.addr == 0x54 && strcmp(L.name, "crc8") == 0
…  (FAIL 43줄)
FAIL starter.c:326  d[1].bytes < d[2].bytes && d[0].bytes < d[2].bytes
15/58 passed
```

스텁 `thumb_insn_len` 은 늘 2 를 돌려주므로 0xE800–0xFFFF 의 6144개가 틀린다. 스텁 파서는 모든 줄을 "그 밖" 으로 보므로 함수 0개 — 통과하는 15개는 "리로케이션 줄은 명령이 아니다" 같은 음성(negative) 검사다.

## 4. 힌트

<details>
<summary>힌트 1 — 길이 규칙은 숫자 비교 하나로도 된다</summary>

[15:11] 이 `11101`, `11110`, `11111` 중 하나면 4바이트. 다섯 비트 값으로는 0x1D, 0x1E, 0x1F, 즉 `(hw1 >> 11) >= 0x1D`, 다시 말해 `hw1 >= 0xE800`. `11100`(0xE000–0xE7FF) 은 16비트 무조건 분기 `B` 라서 2바이트다 — 상위 3비트 `111` 만 보면 이걸 틀린다.

</details>

<details>
<summary>힌트 2 — 줄 모양 세 가지</summary>

- 함수 머리: **0열부터** 16진 주소, 공백, `<이름>:` — `00000054 <crc8>:` (AArch64 는 주소 16자리)
- 명령/데이터: 공백으로 시작, 주소, `: `, **탭이 나올 때까지** 공백으로 나뉜 16진 묶음, 탭, 니모닉. 묶음 길이로 종류가 보인다: Thumb `f8d0 c000`(4자리 ×1~2), A32/A64 `e5d02000`(8자리), 데이터 `02 25 0b 1e`(2자리 바이트). 바이트 수 = 16진 자릿수 / 2.
- 리로케이션: 탭 3개, 주소, `:  R_ARM_THM_CALL` — 콜론 뒤 첫 묶음이 16진이 아니다.

니모닉이 `.` 으로 시작하면(`.word`, `.short`, `.byte`) 데이터다.

</details>

<details>
<summary>힌트 3 — 집계는 "현재 함수" 포인터 하나</summary>

함수 머리를 만나면 새 `func_t` 를 열고, 이후 명령·데이터 줄을 거기에 더한다. 함수 사이 빈 줄·머리말은 무시. 합계는 마지막에 한 번. `.w` 는 `strstr(mnem, ".w")` — `addne.w` 처럼 조건 뒤에 붙는다.

</details>

<details>
<summary>힌트 4 — 반올림과 음수</summary>

`num = (big - small) * 1000` 을 64비트로. 0.5 에서 0 에서 먼 쪽으로: `num >= 0` 이면 `(2*num + big) / (2*big)`, 음수면 `(2*num - big) / (2*big)` (C 의 정수 나눗셈은 0 쪽으로 자른다, C11 6.5.5).

</details>

## 5. 풀이

### Part A — 길이 규칙: 왜 5비트인가

Thumb-1(ARMv4T~v6) 은 거의 전부 16비트였고, 예외인 `BL`(v5T 부터 `BLX` 도)은 halfword 두 개(첫 halfword `11110`, 둘째 `11111`/`11101`)로 이뤄진 쌍이었다. Thumb-2(ARMv6T2/ARMv7) 는 이 자리를 일반화해 **`11101`, `11110`, `11111` 로 시작하는 hw1 은 뒤에 hw2 가 붙는 32비트 명령**이라고 정했다 (Armv7-M ARM DDI 0403, "Thumb instruction set encoding" 절 — 확인 권장). 디코더는 첫 halfword 만 보고 길이를 알 수 있어서, 명령 페치·분기 예측에 유리하다.

이 규칙이 objdump 와 정말 같은지 65536개 전부 확인했다. `make_dumps.sh` 가 슬롯마다 `[hw1][0xbf00 nop]` 을 `.inst.n` 으로 넣고(매핑 심볼 `$t`, 즉 "여기는 Thumb 코드") 디스어셈블한다. hw1 이 16비트면 objdump 가 hw1, nop 을 따로 읽고, 32비트면 둘을 한 명령으로 묶는다 — 어느 쪽이든 다음 슬롯은 4바이트 뒤라 동기가 깨지지 않는다. 실제 요약 (`thumb_sweep.txt`):

```text
thumbv7em 0000 e7ff 2
thumbv7em e800 ffff 4
# thumbv7em: <unknown> 으로 찍힌 명령 수 — 2바이트 946, 4바이트 1590
thumbv6m 0000 e7ff 2
thumbv6m e800 ffff 4
# thumbv6m: <unknown> 으로 찍힌 명령 수 — 2바이트 2210, 4바이트 6144
# 경계 부분 원문 (thumbv7em):
   39ff8: e7fe         	b	0x39ff8 <sweep+0x39ff8> @ imm = #-0x4
   39ffa: bf00         	nop
   39ffc: e7ff         	b	0x39ffe <sweep+0x39ffe> @ imm = #-0x2
   3a000: e800 bf00    	<unknown>
   3a004: e801 bf00    	<unknown>
```

읽을 거리: (1) 경계는 정확히 0xE800. (2) **정의되지 않은 인코딩(`<unknown>`)도 길이는 규칙대로** — 길이 판정은 디코드보다 먼저다. (3) Cortex-M0(v6m) 는 32비트 공간 6144개가 **전부** `<unknown>`: v6-M 의 32비트 명령은 BL, MSR, MRS, DMB/DSB/ISB 정도뿐인데(DDI 0419 확인 권장) 이 스윕의 hw2 가 항상 `bf00` 이라 그 몇 개와도 맞지 않았다(hw2 가 다르면 일부는 유효). 그래도 길이 규칙은 같다.

정답 함수는 한 줄이다:

```c
unsigned top5 = hw1 >> 11;
return (top5 == 0x1D || top5 == 0x1E || top5 == 0x1F) ? 4u : 2u;
```

**바이트 순서 함정.** objdump 는 32비트 Thumb 명령을 `ea22 0101` 처럼 **halfword 두 개를 논리 순서로** 찍는다. 메모리에는 `22 ea 01 01` 로 들어 있다 (halfword 마다 little-endian). 4바이트 LE 워드로 읽으면 0x0101ea22 가 되어 hw1 이 엉뚱해진다. A32/A64 는 objdump 가 32비트 워드 하나(`e5d02000`)로 찍고, 메모리는 `00 20 d0 e5` 이다.

**작은 예 — `reg_clear_bits` 를 바이트에서 직접 쪼개기** (thumbv7em, 주소 0x8부터):

| pc | 메모리 바이트 | hw1 | [15:11] | 길이 | objdump |
|---|---|---|---|---|---|
| 0x08 | `02 68` | 0x6802 | 01101 | 2 | `ldr r2, [r0]` |
| 0x0a | `22 ea 01 01` | 0xea22 | 11101 | 4 | `bic.w r1, r2, r1` |
| 0x0e | `01 60` | 0x6001 | 01100 | 2 | `str r1, [r0]` |
| 0x10 | `70 47` | 0x4770 | 01000 | 2 | `bx lr` |

합계 10 B = `llvm-nm -S` 의 `reg_clear_bits` 크기 0xa. (Cortex-M0 판은 `bics r2, r1` 16비트라 8 B — v6-M 에는 3-레지스터 BIC 가 없어서 `r2` 를 목적지로 쓰는 2-오퍼랜드 형태를 고른다.)

**줄 파서.** 형식을 표로 고정해 두면 구현은 기계적이다.

| 줄 | 판정 | 핵심 |
|---|---|---|
| `00000054 <crc8>:` | FUNC | 0열이 16진 숫자, 그 뒤 `" <"`, `">:"` |
| `      5a: b1a9         \tcbz\tr1, …` | INSN, 2 B | 바이트 칸 = `:` 다음부터 **첫 탭** 전까지 |
| `      42: eb00 020c    \taddne.w\t…` | INSN, 4 B | 묶음 2개 × 4자리, `group0 = 0xeb00 = hw1` |
| `      96: 02 25 0b 1e  \t.word\t0x1e0b2502` | DATA, 4 B | 묶음 4개 × 2자리, 니모닉이 `.` |
| `     194: 528fffe9     \tmov\tw9, #0x7fff   // =32767` | INSN, 4 B | 뒤쪽 주석은 니모닉 다음이라 무관 |
| `\t\t\t00000124:  R_ARM_THM_CALL\t__aeabi_uidiv` | OTHER | 콜론 뒤 첫 묶음 `R_ARM…` 이 16진이 아님 |

묶음 길이를 2/4/8 로 제한하고 묶음 직후 문자가 공백인지 확인하는 것이 리로케이션 줄을 걸러내는 장치다 (`R` 은 16진이 아니라 길이 0 → 거부).

### Part B — 타깃별 통계 (solution 실제 출력)

```text
target      bytes  insns     2B     4B   data     .w avg B/insn
thumbv6m      302    144    143      1     12      0       2.01
thumbv7em     320    125     92     33      4     26       2.53
armv7a        504    122      0    122     16      0       4.00
aarch64       504    126      0    126      0      0       4.00
```

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="22" fill="currentColor" font-weight="bold">.text 바이트 (-Os, firmware.c 10개 함수) — 막대 = 2B 명령 / 4B 명령 / 코드 속 데이터</text>
<g fill="currentColor"><text x="10" y="64">thumbv6m</text><text x="10" y="104">thumbv7em</text><text x="10" y="144">armv7a</text><text x="10" y="184">aarch64</text></g>
<rect x="100" y="48" width="229" height="24" class="f-ok" stroke="currentColor"/><rect x="329" y="48" width="3" height="24" class="f-acc" stroke="currentColor"/><rect x="332" y="48" width="10" height="24" class="f-warn" stroke="currentColor"/>
<text x="350" y="65" fill="currentColor">302 B · 144 insns · 2.01 B/insn</text>
<rect x="100" y="88" width="147" height="24" class="f-ok" stroke="currentColor"/><rect x="247" y="88" width="106" height="24" class="f-acc" stroke="currentColor"/><rect x="353" y="88" width="3" height="24" class="f-warn" stroke="currentColor"/>
<text x="364" y="105" fill="currentColor">320 B · 125 insns · 2.53 B/insn</text>
<rect x="100" y="128" width="390" height="24" class="f-acc" stroke="currentColor"/><rect x="490" y="128" width="13" height="24" class="f-warn" stroke="currentColor"/>
<text x="512" y="145" fill="currentColor">504 B · 122 insns</text>
<rect x="100" y="168" width="403" height="24" class="f-acc" stroke="currentColor"/>
<text x="512" y="185" fill="currentColor">504 B · 126 insns</text>
<rect x="100" y="212" width="16" height="14" class="f-ok" stroke="currentColor"/><text x="122" y="224" fill="currentColor">2바이트 명령</text>
<rect x="230" y="212" width="16" height="14" class="f-acc" stroke="currentColor"/><text x="252" y="224" fill="currentColor">4바이트 명령</text>
<rect x="360" y="212" width="16" height="14" class="f-warn" stroke="currentColor"/><text x="382" y="224" fill="currentColor">데이터 (.word: 분기표·리터럴 풀)</text>
<text x="10" y="250" fill="currentColor">핵심: 명령 수는 122–144 로 비슷하다. 크기 차이는 거의 전부 "명령 하나의 평균 길이" 에서 나온다.</text>
</svg>
```

표에서 읽을 것:

1. **명령 수는 비슷, 길이가 다르다.** Thumb-2(125개)와 A32(122개)는 같은 일을 거의 같은 명령 수로 한다. 차이는 평균 길이 2.53 B 대 4.00 B. Thumb-2 명령의 92/125 = 73.6% 가 16비트다.
2. **Cortex-M0 는 명령이 더 많지만(144) 거의 다 16비트**(143개, 4바이트는 `bl __aeabi_uidiv` 하나)라서 `-Os` 에서는 v7em 보다 18 B 작았다. v7em 은 `-Os` 에서도 32비트 명령으로 명령 수를 줄이는 쪽을 택하고(예: `rb_push` 의 `ldrh.w`/`add.w`, 리프 함수인데 `push {r7, lr}` 로 lr 을 임시 레지스터로 씀), `small_memcpy` 루프 앞에는 정렬용 `nop`(`bf00`)도 넣었다. `-Oz` 로 가면 순서가 바뀐다 (Part C 표).
3. **데이터도 .text 에 산다.** v7em `fsm_feed` 의 `tbb [pc, r2]` 뒤 4 B 바이트 분기표, v6m `sat_add_s16` 의 리터럴 풀 2워드(0x00007fff, 0xffff8000 — v6-M 에는 32비트 MOVW 가 없다), A32 `fsm_feed` 의 `ldr pc, [r3, r2, lsl #2]` 뒤 4워드 주소표(각 워드에 `R_ARM_ABS32` 리로케이션). objdump 가 이걸 `.word` 로 찍을 수 있는 건 오브젝트의 **매핑 심볼** `$t`(Thumb 코드) / `$a`(ARM 코드) / `$d`(데이터) 덕분이다 (`llvm-objdump -t` 로 보면 `00000096 l .text $d`, `0000009a l .text $t`). 매핑 심볼 없이 선형으로 쪼개면 어긋날 수 있다 — 연습 2.
4. **AArch64 는 데이터 0.** switch 를 비교 사슬로 풀었고(`cmp`/`b.gt`/`cbz`), 상수는 `mov w9, #0x7fff` / `mov w9, #-0x8000`(MOVN) 처럼 명령 안에 넣었다 (ARM-B03).

**`.w` 와 `.n` — 길이 표시가 아니라 "선택 표시".** UAL(통합 어셈블러 문법)에서 `.w` 는 32비트 인코딩, `.n` 은 16비트 인코딩을 요구하는 접미사다. 디스어셈블러는 "같은 니모닉에 16비트 형태도 있는데 이번엔 32비트" 일 때 `.w` 를 찍어 재어셈블 시 같은 바이트가 나오게 한다. 그래서 v7em 의 4바이트 명령 33개 중 7개는 `.w` 가 없다 — 애초에 16비트 형태가 없는 명령이다:

```text
      3a: f003 033f    	and	r3, r3, #0x3f        ← AND 즉시값은 32비트뿐
      92: e8df f002    	tbb	[pc, r2]
     10c: fa90 f011    	qadd16	r0, r0, r1        ← DSP 확장 (Cortex-M4)
     118: f811 3b01    	ldrb	r3, [r1], #1      ← post-index 는 32비트뿐
     11e: f800 3b01    	strb	r3, [r0], #1
     12c: f830 cb02    	ldrh	r12, [r0], #2
     136: fbb2 f0f1    	udiv	r0, r2, r1
```

같은 바이트를 GNU `arm-none-eabi-objdump`(binutils 2.45, VM) 는 `ldrb.w r3, [r1], #1` 로 찍는다 — `.w` 표기는 도구마다 다르다. **길이는 바이트 칸으로만 판단한다.**

어셈블러가 언제 16/32비트를 고르는지 `demo_width.S` 로 확인했다:

<!-- code: demo_width.S -->

```text
$ clang --target=thumbv7em-none-eabi -c demo_width.S -o dw.o && llvm-objdump -d dw.o
       0: 1c40         	adds	r0, r0, #0x1
       2: f110 0001    	adds.w	r0, r0, #0x1
       6: f100 0001    	add.w	r0, r0, #0x1      ← IT 밖에서 플래그 안 쓰는 ADD → 32비트
       a: 4488         	add	r8, r1
       c: f510 7096    	adds.w	r0, r0, #0x12c
      10: 6fc8         	ldr	r0, [r1, #0x7c]
      12: f8d1 0080    	ldr.w	r0, [r1, #0x80]
      16: f811 3b01    	ldrb	r3, [r1], #1
      1a: 2800         	cmp	r0, #0x0
      1c: bf08         	it	eq
      1e: 1c40         	addeq	r0, r0, #0x1     ← IT 안의 16비트 ADD 는 플래그를 안 쓴다
      20: 4770         	bx	lr
```

16비트 데이터 처리 명령 대부분은 **IT 블록 밖에서는 플래그를 쓰고(ADDS), 안에서는 안 쓴다** (CMP/CMN/TST 는 늘 쓰고, 고레지스터 `add r8, r1`·`mov` 는 늘 안 쓴다) — 레지스터 필드를 줄이려고 "S 비트" 를 인코딩에서 뺀 대가다. 그래서 C 에서 플래그가 필요 없는 `add` 도 IT 밖이면 `adds`(16비트)로 바꿀 수 있을 때만 작아진다 (컴파일러는 플래그가 죽어 있으면 그렇게 한다).

지킬 수 없는 `.n` 은 도구마다 다르게 처리됐다 (`demo_width_err.S`, 실제 결과):

```text
$ arm-none-eabi-as -mcpu=cortex-m4 -mthumb demo_width_err.S        # VM, GNU as 2.45
demo_width_err.S:6: Error: cannot honor width suffix -- `add.n r0,r0,#1'
demo_width_err.S:8: Error: cannot honor width suffix -- `ldrb.n r3,[r1],#1'
demo_width_err.S:5: Error: immediate value out of range
demo_width_err.S:7: Error: invalid offset, value too big (0x00000080)
as exit=1

$ clang --target=thumbv7em-none-eabi -c demo_width_err.S           # 맥 Apple clang 21 (VM 의 clang 21.1.8 도 동일)
clang exit=0      ← 에러 없이 adds.w / add.w / ldr.w / ldrb 32비트로 만들어 버림
```

크기 예산이 빡빡한 코드에서 `.n` 을 "16비트 보증" 으로 믿으면 안 된다 — 적어도 이 LLVM 버전에서는. 결과는 objdump/size 로 확인한다.

**IT 블록 읽기.** v7em 덤프의 `rb_push`:

```text
      3e: 4573         	cmp	r3, lr
      40: bf1e         	ittt	ne              ← 다음 3개를 NE 조건으로
      42: eb00 020c    	addne.w	r2, r0, r12
      46: 7111         	strbne	r1, [r2, #0x4]
      48: 8003         	strhne	r3, [r0]
      4a: ebb3 000e    	subs.w	r0, r3, lr
      4e: bf18         	it	ne
      50: 2001         	movne	r0, #0x1        ← return next != tail
```

`IT`(If-Then) 는 16비트 명령 하나로 뒤 1~4개 명령에 조건을 붙인다. A32 는 모든 명령에 4비트 조건 필드가 있어서 같은 함수가 `strhne`/`strbne`/`movwne` 를 IT 없이 쓴다 (연습 3: A32 덤프의 조건부 명령 17개). IT 작성 규칙(4개 제한, T/E 패턴, 분기는 마지막만)은 ARM-B13 에서 직접 써 본다.

### Part C — 함수 × 타깃 비교 (solution 실제 출력)

```text
function (bytes)  thumbv6m thumbv7em    armv7a   aarch64  T2 vs A32
reg_set_bits             8         8        16        16     -50.0%
reg_clear_bits           8        10        16        16     -37.5%
reg_write_field         16        26        24        32      +8.3%
rb_push                 30        40        40        48      +0.0%
crc8                    42        56        92        68     -39.1%
fsm_feed               108       116       204       200     -43.1%
cfg_copy                12        12        20        20     -40.0%
sat_add_s16             28         8        12        36     -33.3%
small_memcpy            18        18        28        24     -35.7%
adc_average             32        26        52        44     -50.0%
TOTAL .text            302       320       504       504     -36.5%
```

함수별로 이유가 다르다 — 면접에서 "항상 X% 작다" 라고 하면 안 되는 이유:

| 함수 | 관찰 (덤프에서) | 이유 |
|---|---|---|
| `reg_write_field` | Thumb-2 가 A32 보다 **크다** (26 vs 24) | A32 는 `bic r12, r12, r1, lsl r2` · `orr r1, r12, r1, lsl r2` 처럼 **레지스터로 시프트한 두 번째 오퍼랜드**가 된다. Thumb-2 는 상수 시프트만 되므로 `lsl.w` 를 따로 쓰고, 레지스터가 모자라 `push {r7, lr}` / `pop {r7, pc}` 까지 붙었다 |
| `rb_push` | 동률 (40 vs 40) | A32 는 조건 실행(`strhne`, `strbne`, `movwne`)으로 분기 없이 끝. Thumb-2 는 IT 2개 + 32비트 명령 6개 + push/pop |
| `sat_add_s16` | v7em 8 B, v6m 28 B, AArch64 36 B | Cortex-M4 의 DSP 명령 `qadd16`(16비트 포화 덧셈 2개를 한 번에) 한 방. M0 는 비교 2번 + 리터럴 풀 8 B. AArch64 스칼라 정수에는 포화 덧셈이 없어 `csel` 두 번 (`sqadd` 는 SIMD 레지스터용) |
| `adc_average` | v7em 26 B 에 `udiv` | M0 와 기본 armv7-a 는 나눗셈 명령이 없어 `bl __aeabi_uidiv` (아래) |
| `crc8`, `fsm_feed` | Thumb-2 가 39–43% 작다 | 작은 즉시값·저레지스터 위주의 평범한 코드 — 16비트 인코딩이 가장 잘 먹히는 경우 |

**런타임 헬퍼 = 숨은 flash 비용.** `parse_reloc` 으로 뽑은 CALL 리로케이션: thumbv6m `R_ARM_THM_CALL __aeabi_uidiv` 1개, armv7a `R_ARM_CALL __aeabi_uidiv` 1개, thumbv7em·aarch64 0개. 이 함수는 링크할 때 libgcc 에서 들어온다. VM 의 arm-none-eabi-gcc 14.2.1 multilib 에서 실제 크기:

```text
-mcpu=cortex-m0 -mthumb -> .../thumb/v6-m/nofp/libgcc.a
0000010c 00000008 T __aeabi_uidivmod
00000000 0000010a T __udivsi3          ← __aeabi_uidiv 의 본체, 266 B
-mcpu=cortex-m4 -mthumb -> .../thumb/v7e-m/nofp/libgcc.a
00000000 00000014 T __udivsi3          ← udiv 를 쓰는 20 B 판
```

M0 에서 `/` 한 줄이 함수 10개 전체(302 B)와 맞먹는 266 B 를 끌고 온다. 그래서 M0 펌웨어는 평균을 낼 때 샘플 수를 2의 거듭제곱으로 잡고 시프트로 나눈다. (`.o` 의 `.text` 만 비교하면 이 비용이 안 보인다는 점도 기억.)

**최적화 수준별 `.text`** (`sizes.txt`, 실측):

| target | -O2 | -Os | -Oz | Thumb-2 vs A32 |
|---|---|---|---|---|
| thumbv7em | 774 | 320 | 278 | -O2 **+2.9%** · -Os −36.5% · -Oz −44.4% |
| thumbv6m | 714 | 302 | **306** | -Oz 가 -Os 보다 4 B 크다 |
| armv7a | 752 | 504 | 500 | |
| aarch64 | 1012 | 504 | 504 | |

`-O2` 에서는 루프 펼치기·정렬 때문에 Thumb-2 가 오히려 A32 보다 크다. 코드 밀도 비교는 반드시 같은 최적화 수준, 크기 최적화(-Os/-Oz) 기준으로 한다. 참고로 GCC 14.2.1 `-Os` 는 같은 파일에서 Cortex-M0/M4 모두 `.text` 316 B 였고, M0 의 switch 를 `bl __gnu_thumb1_case_uqi` 헬퍼로 처리했다 (clang 은 `add pc, r2` 인라인 분기표, 정렬용 `mov r8, r8`).

**그래서 몇 % 인가.** 이 파일, 이 컴파일러, `-Os` 에서 Thumb-2 는 A32 보다 **36.5% 작았다**(320 vs 504). Arm 자료에서 흔히 인용하는 "Thumb-2 는 ARM 코드보다 약 25–30% 작다" 와 같은 방향이지만, 함수 구성에 따라 +8% 에서 −50% 까지 흩어진다 (인용 수치는 문서마다 달라 확인 권장). AArch64 는 A32 와 같은 504 B — 고정 32비트지만 레지스터 31개·풍부한 주소 지정(ARM-B04)·`csel` 류로 명령 수를 비슷하게 유지했다.

**AArch64 에 Thumb 이 없는 이유** (Arm 의 설계 설명 요지, 해석 포함 — 확인 권장): 64비트 ISA 는 레지스터 31개(5비트 필드 ×3)와 넓은 즉시값을 원했고 16비트에는 들어가지 않는다. 고정 길이는 디코더를 단순하게 하고 여러 명령을 한 사이클에 병렬 디코드하기 쉽다 (Apple M 시리즈 같은 넓은 코어). 응용 프로세서는 캐시·메모리가 커서 코드 밀도의 가치가 MCU 보다 낮다. 그래서 Cortex-M 은 아직도 T32 전용이고, AArch64 SoC 의 작은 관리 코어(예: SoC 안의 Cortex-M/R 보조 코어)가 Thumb-2 펌웨어를 돌린다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/ARM-B12_solution && /tmp/ARM-B12_solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/ARM-B12_asan && /tmp/ARM-B12_asan
```

실제 출력: 위 5. 풀이의 두 표 + 마지막 줄 `58/58 passed` (exit 0). ASan/UBSan 빌드도 `58/58 passed`, 경고 없음.

## 7. 자주 하는 실수

**① 옛 llvm-objdump 로 Thumb 오브젝트를 읽는다.** `/usr/local/bin/llvm-objdump` 는 LLVM 3.2 다. 실제 출력:

```text
$ /usr/local/bin/llvm-objdump -d fw_thumbv6m.o 2>/dev/null | head
.text:
       1:	68 0a 43 02     	subeq	r0, r3, #425984
       5:	60 70 47 02     	subeq	r7, r7, #96
```

주소가 1 부터(심볼 값의 Thumb 비트를 그대로 씀), ARM 모드로 디코드해서 쓰레기, stderr 에는 `invalid instruction encoding` 경고 94줄. AArch64 오브젝트는 `unable to get target` 으로 아예 실패. → `/Library/Developer/CommandLineTools/usr/bin/llvm-objdump` 또는 `xcrun llvm-objdump`.

**② 상위 3비트만 본다.** `(hw1 & 0xE000) == 0xE000 ? 4 : 2` 는 0xE000–0xE7FF 의 16비트 `B` 를 32비트로 본다. 비교 연산 실수 `(hw1 >> 11) > 0x1D` 는 `11101` 을 놓친다. 둘 다 65536개 전수 비교에서 **2048개 불일치** 로 걸린다 (작은 C 프로그램으로 실측: 첫 번째는 0xe000 부터). → `(hw1 >> 11) >= 0x1D`.

**③ `.w` 로 길이를 판단한다.** v7em 4바이트 명령 33개 중 7개는 `.w` 가 없고, GNU objdump 는 다른 규칙으로 찍는다. → 바이트 칸의 16진 자릿수 / 2.

**④ objdump 의 `ea22 0101` 을 32비트 워드 0xea220101 로 보고 LE 로 메모리에 놓는다.** 그러면 `01 01 22 ea` 를 기대하게 되는데 실제 메모리(크래시 덤프, flash 이미지)는 `22 ea 01 01` 이다. 거꾸로 메모리에서 32비트 LE 로 읽으면 0x0101ea22 → hw1 을 0x0101 로 오판해 2바이트 명령으로 쪼갠다. → Thumb 은 halfword 단위 LE, hw1 이 먼저.

**⑤ Berkeley `size` 의 text 열로 비교한다.** 같은 오브젝트가 Berkeley 형식으로는 `400`(thumbv7em), SysV 형식(`llvm-size -A`)으로는 `.text 320` + `.ARM.exidx 80` 이다 — text 열에 언와인드 표(`.ARM.exidx`, 함수당 8 B)가 합쳐진다. AArch64 는 504 로 같다(exidx 없음). `-fno-unwind-tables -fno-asynchronous-unwind-tables` 를 줘도 이 clang 에서는 `.ARM.exidx 80` 그대로였다. → 섹션별로 보고 `.text` 끼리 비교. 최종 펌웨어는 링크 후 `.text + .rodata + .data(LMA)` 가 flash 사용량이다.

**⑥ `.n` 이면 16비트가 보증된다고 믿는다.** GNU as 는 에러를 내지만 clang 21 은 조용히 32비트로 만들었다 (5. 풀이). → 결과를 objdump 로 확인.

**⑦ 데이터 줄을 명령으로 센다.** `.word` 줄을 명령으로 세면 v7em 명령 수가 126(1줄), A32 는 126(4줄)이 되고 "평균 4바이트" 계산이 흐트러진다. 니모닉이 `.` 이면 데이터.

## 8. 면접 Q&A

<details>
<summary>Q1. Thumb 명령의 길이를 어떻게 알죠? 크래시 덤프에서 PC 앞 명령을 거꾸로 읽을 수 있나요?</summary>

첫 halfword 의 [15:11] 이 11101/11110/11111 이면 32비트, 아니면 16비트 (= hw1 ≥ 0xE800). 앞으로 읽기는 이 규칙으로 확정되지만 **거꾸로는 모호하다**: PC−2 의 halfword 가 16비트 명령인지, 32비트 명령의 뒤쪽 halfword 인지 알 수 없다 (hw2 는 아무 값이나 될 수 있다). 그래서 함수 시작(심볼)이나 알려진 경계에서 앞으로 디코드한다. 예외 스택의 리턴 주소(stacked PC)로 크래시 지점을 찾을 때도 같은 이유로 함수 시작부터 디스어셈블한다 (ARM-M12).

</details>

<details>
<summary>Q2. Thumb-2 로 빌드하면 ARM 모드보다 얼마나 작아지나요? 항상 작아지나요?</summary>

이 레슨의 실측: `-Os` 에서 320 vs 504 B, 36.5% 작음. 하지만 함수별로 +8.3%(레지스터 시프트 오퍼랜드가 필요한 `reg_write_field`) 부터 −50% 까지, `-O2` 에서는 오히려 +2.9%. 명령 수는 거의 같고(125 vs 122) 평균 길이가 2.53 vs 4 B 라서 줄어든다. A32 의 조건 실행·레지스터 시프트 오퍼랜드가 필요한 코드, 32비트 즉시값이 많은 코드에서는 이득이 줄어든다. 숫자는 항상 "어떤 코드, 어떤 컴파일러, 어떤 최적화" 를 붙여 말한다.

</details>

<details>
<summary>Q3. AArch64 에는 왜 Thumb 같은 16비트 인코딩이 없죠?</summary>

(설계 의도는 Arm 설명 + 해석, 확인 권장) 31개 레지스터 × 3 오퍼랜드 = 15비트가 이미 필요해서 16비트에 쓸모 있는 명령이 거의 안 들어간다. 고정 32비트 정렬은 넓은 슈퍼스칼라 디코드(명령 경계가 항상 4의 배수)와 분기 대상 계산을 단순하게 한다. AArch64 가 겨냥한 응용 프로세서는 캐시가 커서 밀도보다 디코드 폭·성능이 중요하다. 대신 A64 는 명령 하나가 하는 일이 많아(`csel`/`cinc`, `ldp`/`stp`, 확장 레지스터 오퍼랜드) 이 레슨에서도 A32 와 같은 504 B 였다. 참고로 RISC-V 는 반대로 압축 확장(C, 16비트)을 택했다.

</details>

<details>
<summary>Q4. IT 블록이 뭐예요? AArch64 에는 왜 없죠?</summary>

Thumb 명령 대부분은 조건 필드가 없어서, `IT{x{y{z}}} cond` 16비트 명령이 뒤 1~4개 명령에 조건(T = cond, E = 반대 조건)을 붙인다. 덤프 예: `ittt ne` + `addne.w`/`strbne`/`strhne`. 블록 안 16비트 데이터 처리 명령은 플래그를 안 쓰는 등 규칙이 미묘하다. ARMv8-A 의 AArch32 에서는 IT 의 일부 형태(32비트 명령을 덮는 IT, 여러 명령을 덮는 IT)가 deprecated 됐다 (Arm ARM DDI 0487 기준, 확인 권장). AArch64 는 조건 실행을 `csel`/`csinc`/`ccmp` 같은 조건 선택·비교 명령으로 대신해 파이프라인의 조건 실행 부담을 없앴다 (ARM-B05).

</details>

<details>
<summary>Q5. 디스어셈블리의 `.w`, `.n` 은 뭐예요?</summary>

UAL 의 폭 지정 접미사: `.w` = 32비트 인코딩, `.n` = 16비트. 디스어셈블러는 16비트 형태도 있는 명령이 32비트로 인코딩됐을 때 `.w` 를 찍는다 (그래야 재어셈블하면 같은 바이트). 16비트 형태가 없는 `tbb`, `udiv`, post-index `ldrb` 에는 LLVM 은 안 붙이고 GNU objdump 는 `ldrb.w` 로 붙이는 등 도구마다 다르다. 길이 판단에는 쓰지 말 것. 어셈블러 입력에서 지킬 수 없는 `.n` 은 GNU as 는 에러, clang 21 은 조용히 32비트로 바꿨다(실측).

</details>

<details>
<summary>Q6. Cortex-M0 와 M4 중 하나를 고른다면, 코드 크기 관점에서 무엇을 보나요?</summary>

`-Os` 에서 순수 명령 밀도는 M0(Thumb-1 위주)가 오히려 약간 작을 수 있다 (302 vs 320 B, 이 레슨). 하지만 M0 는 나눗셈(`__aeabi_uidiv` 266 B 를 링크), 32비트 상수(리터럴 풀), 포화·DSP(`qadd16` 1개 vs 비교 2번), 비트필드(`ubfx`/`bfi` 없음), 큰 오프셋 로드가 모두 비싸다. 그런 코드가 많으면 M4 가 더 작고 빠르다. 결정은 실제 펌웨어를 두 타깃으로 빌드해 **링크 후** 맵 파일로 비교한다.

</details>

<details>
<summary>Q7. 리터럴 풀이 뭐고, 디스어셈블러는 코드 속 데이터를 어떻게 알아보나요?</summary>

리터럴 풀은 명령 즉시값에 안 들어가는 상수를 함수 끝 등에 두고 PC 기준 `ldr r1, [pc, #0x10]` 로 읽는 방식이다 (v6m `sat_add_s16` 의 0x7fff/0xffff8000). `tbb` 분기표, A32 주소표도 코드 사이 데이터다. ELF for the Arm Architecture 는 매핑 심볼 `$a`/`$t`/`$d` 로 "여기부터 ARM/Thumb/데이터" 를 표시하게 하고, objdump 는 이걸 보고 `.word` 로 찍는다. 매핑 심볼 없이(스트립된 바이너리, 원시 flash 덤프) 선형 디코드하면 데이터를 명령으로 읽고 경계가 어긋날 수 있다 — 연습 2 에서 v6m 의 `0xffff8000` 리터럴의 `ffff` 가 32비트 머리로 읽혀 다음 함수 첫 명령(`0xfc`)을 삼키는 걸 실제로 봤다.

</details>

<details>
<summary>Q8. 함수 포인터의 Thumb 비트는 이 레슨과 무슨 관계죠?</summary>

ELF 심볼 테이블의 Thumb 함수 `st_value` 는 bit0 = 1 이다. 이 레슨의 thumbv7em 오브젝트를 파이썬으로 직접 읽으니 `reg_clear_bits st_value=0x00000009`, `crc8 0x00000055` 였다. `llvm-nm`/`llvm-objdump` 는 이 비트를 지우고 `00000008`, `00000054` 로 보여 준다 (그래서 nm 크기와 objdump 주소를 바로 비교할 수 있다). 옛 objdump 가 주소 1 부터 디코드한 것(실수 ①)이 이 비트 때문이다. `BX`/`BLX` 가 이 비트로 Thumb 상태를 고르고, Cortex-M 은 Thumb 만 되므로 벡터 테이블 항목도 홀수여야 한다 → ARM-M08, ARM-B13.

</details>

## 9. 추가 연습

### 연습 1 — IT 블록 검증기

`int it_len(const char *mnem)` (IT 가 덮는 명령 수, IT 아니면 0) 와 `check_it_blocks(fp, &nblk, &ncov)` 를 작성한다: thumbv7em 덤프의 IT 블록마다 덮인 명령의 조건 접미사가 T 자리면 cond, E 자리면 반대 조건(cond 번호 ^ 1)인지 확인. `.w` 는 떼고 끝 두 글자를 본다.

<details>
<summary>답</summary>

`it` 뒤 글자가 모두 t/e 이고 합계가 4 이하일 때만 IT. 조건 번호 표 eq=0, ne=1, hs(cs)=2, lo(cc)=3, mi=4 … le=13 에서 반대 조건은 마지막 비트만 다르다 (그래서 `^ 1`). 실측: **IT 블록 4개, 덮인 명령 6개, 위반 0** — `ittt ne`(rb_push), `it ne`(rb_push), `it mi`(crc8, `eormi.w`), `it eq`(fsm_feed, `moveq`). 그중 `it mi` 는 CRC 의 "최상위 비트가 1이면 다항식 XOR" 를 분기 없이 만든 것이다.

</details>

### 연습 2 — 매핑 심볼 없이 쪼개기

덤프의 바이트를 메모리 순서로 이어 붙이고(Thumb 묶음은 halfword LE), 0번지부터 `thumb_insn_len` 만으로 쪼갠다 (`blind_split`). objdump 의 명령 시작 주소 중 몇 개를 놓치는가?

<details>
<summary>답</summary>

실측: thumbv7em 은 320 B 에서 objdump 125개, 눈 감고 127조각, **놓친 시작 0** — `tbb` 표 `02 25 0b 1e` 가 halfword 0x2502, 0x1e0b 로 둘 다 16비트 모양이라 운 좋게 어긋나지 않았다 (가짜 명령 2개만 생김). thumbv6m 은 302 B 에서 144 vs 149조각, **놓친 시작 1개 = 0xfc**: `sat_add_s16` 리터럴 `00 80 ff ff` 의 뒤 halfword `0xffff` 가 32비트 머리로 읽혀 `small_memcpy` 첫 명령 `cmp r2, #0`(0xfc)을 삼켰다. 다음 halfword 에서 우연히 다시 맞는다. 원시 flash 덤프를 분석할 때 경계가 이렇게 조용히 틀어진다.

</details>

### 연습 3 — A32 조건 필드 세기

A32 명령 워드의 [31:28] 이 조건이다. AL(0xE)과 무조건 공간(0xF)을 빼고 조건부 명령을 세고, 그 니모닉이 해당 조건 접미사로 끝나는지 확인하라 (`a32_cond_scan`).

<details>
<summary>답</summary>

실측: **조건부 17개 (분기 9: `beq`/`bne`/`bhi`, 그 밖 8: `strhne`, `strbne`, `movwne`, `eormi`, `movweq`, `moveq`, `bxeq` ×2), 접미사 위반 0.** 예: `03002004 movweq` 의 최상위 니블 0 = EQ, `1afffffb bne` 의 1 = NE, `8a00002d bhi` 의 8 = HI. Thumb-2 가 IT 로 붙이는 조건을 A32 는 모든 명령의 4비트로 낸다 — 명령마다 4비트를 내는 것이 A32 가 큰 이유 중 하나이기도 하다.

</details>

### 연습 4 — 최적화 수준 표

`sizes.txt` 의 `target O2 Os Oz` 표를 읽어(`load_opt_table`) `-Os` 가 `-O2` 대비 몇 % 줄였는지 찍고, `-Oz` 가 `-Os` 보다 큰 타깃을 찾아라.

<details>
<summary>답</summary>

실측: thumbv7em 774→320 (58.7%), thumbv6m 714→302 (57.7%), armv7a 752→504 (33.0%), aarch64 1012→504 (50.2%). `-Oz > -Os` 는 thumbv6m 하나 (306 vs 302). `-Oz` 는 "더 작게 하려는 휴리스틱" 이지 보증이 아니다 — 플래그를 바꾸면 늘 측정한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/ARM-B12_practice && /tmp/ARM-B12_practice
```

실제 출력:

```text
IT blocks (thumbv7em): 4 blocks, 6 covered insns, 0 violations
blind split thumbv7em: 320 bytes, objdump 125 insns, blind 127 pieces, missed 0 (first 0x0)
blind split thumbv6m : 302 bytes, objdump 144 insns, blind 149 pieces, missed 1 (first 0xfc)
A32 conditional insns: 17 (branches 9, others 8), suffix violations 0
thumbv7em  O2   774  Os   320  Oz   278  (Os saves 58.7% vs O2)
thumbv6m   O2   714  Os   302  Oz   306  (Os saves 57.7% vs O2)
armv7a     O2   752  Os   504  Oz   500  (Os saves 33.0% vs O2)
aarch64    O2  1012  Os   504  Oz   504  (Os saves 50.2% vs O2)
14/14 passed
```

## 10. 핵심 정리

- Thumb 길이: hw1[15:11] ∈ {11101, 11110, 11111} → 4바이트, 아니면 2 (⇔ hw1 ≥ 0xE800). 0xE000–0xE7FF 는 16비트 `B`. objdump 로 65536개 전수 확인.
- objdump 의 `ea22 0101` = 메모리 `22 ea 01 01` (halfword LE). 길이는 바이트 칸으로, `.w` 로 판단하지 않는다.
- `-Os` 실측: Thumb-2 320 B, Cortex-M0 302 B, A32 504 B, AArch64 504 B. 명령 수는 비슷(122–144), 평균 길이가 2.0 / 2.5 / 4 / 4 B.
- Thumb-2 vs A32 = −36.5% 이지만 함수별 +8% ~ −50%, `-O2` 에선 +2.9%. 레지스터 시프트 오퍼랜드·조건 실행이 A32 의 무기.
- 코드 속 데이터(리터럴 풀, `tbb` 표, 주소표)는 매핑 심볼 `$t/$a/$d` 로 구분. 없으면 선형 디코드가 어긋난다.
- `.o` 의 `.text` 만 보면 런타임 헬퍼가 안 보인다: M0 의 `/` 하나 = libgcc `__udivsi3` 266 B.
- Berkeley `size` 의 text 는 `.ARM.exidx` 까지 합친다 — 섹션별(`-A`)로 비교.
- AArch64 는 고정 32비트(디코드 폭·31 레지스터), 조건 실행은 `csel` 류로. Cortex-M 은 T32 전용.

## 참고자료

- Arm, *Armv7-M Architecture Reference Manual* (DDI 0403), Thumb instruction set encoding (16/32비트 판정, IT, 16비트 명령의 플래그 규칙) — https://developer.arm.com/documentation/ddi0403/latest
- Arm, *Armv6-M Architecture Reference Manual* (DDI 0419) — v6-M 의 32비트 명령 목록 — https://developer.arm.com/documentation/ddi0419/latest
- Arm, *Arm Architecture Reference Manual for A-profile* (DDI 0487), AArch32 T32/A32 인코딩, IT deprecation, A64 — https://developer.arm.com/documentation/ddi0487/latest
- Arm, *ELF for the Arm Architecture* (aaelf32) — 매핑 심볼 `$a/$t/$d`, Thumb 함수 심볼 bit0 — https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst
- Arm, *Run-time ABI for the Arm Architecture* (rtabi32) — `__aeabi_uidiv` 등 헬퍼 — https://github.com/ARM-software/abi-aa/blob/main/rtabi32/rtabi32.rst
- GNU as, ARM 의존 기능 (`.syntax unified`, `.inst.n/.inst.w`, 폭 접미사) — https://sourceware.org/binutils/docs/as/ARM_002dDependent.html
- LLVM, `llvm-objdump` 명령 문서 — https://llvm.org/docs/CommandGuide/llvm-objdump.html
- 관련 레슨: ARM-B03 (MOVZ/MOVK, 즉시값), ARM-B05 (조건 코드, `csel`), ARM-B13 (Thumb-2 asm 과 IT 블록 작성), ARM-M08 (벡터 테이블 · Thumb 비트), ARM-M12 (HardFault 와 PC 복원), TOOL-B04 (nm/objdump/otool)
