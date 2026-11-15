---
id: ARM-M07
time: 80분
updated: 2026-11-15
---
# ARM-M07 NEON 바이트 처리: memchr·히스토그램·체크섬

## 학습 목표

- `neon_memchr` 를 AArch64 어셈블리로 짠다. `CMEQ` 로 16 바이트를 한 번에 비교하고, `SHRN #4` 로 **64비트 니블 마스크(nibble mask)** 를 만들고, `RBIT + CLZ` 로 첫 일치 위치를 구한다. NEON 에 x86 `PMOVMSKB` 가 없어서 쓰는 대안 4가지를 비교해 말한다.
- 꼬리(tail) 처리 세 방식의 장단점을 안다. **스칼라 꼬리**, **겹치는 마지막 16 B 읽기**, **정렬 내림(aligned-down) + 마스킹**(libc 방식)이다. 각 방식이 페이지 경계에서 안전한 이유를 `PROT_NONE` 가드 페이지 테스트로 증명한다.
- `checksum_u8` 를 `UADALP`(쌍 단위 넓히며 누적)로 짠다. 누산기 폭마다 **오버플로 지평(horizon)** 을 계산한다. u16 레인은 128 스텝(2048 B)마다 비워야 한다. 그 경계 바로 앞과 바로 뒤를 테스트한다.
- `minmax_u16` 을 `UMIN/UMAX` + `UMINV/UMAXV` 로 짜고, signed 판(`SMIN`)이 12비트 ADC 데이터에서는 **테스트를 통과해 버리는** 함정을 실측으로 보인다.
- M6 실측(부하 큼): 스칼라 C 대비 10배 이상 빠르다. 손으로 짠 memchr 는 libc 와 비슷하다. clang 자동 벡터화 바이트 합은 손 asm 보다 대체로 2~3배 느리다. 이 차이를 디스어셈블리로 설명한다.

## 1. 문제

"UART 수신 버퍼에서 프레임 구분자(0x7E)를 제일 빨리 찾는 법은?", "패킷 체크섬을 SIMD 로 하면 어디서 넘치나?" — 펌웨어·SoC 면접에서 NEON 을 물을 때 단골로 나오는 **바이트 스트림 처리** 질문이다. 곱셈 많은 DSP 보다 이쪽이 더 흔하다. SSD 컨트롤러에서도 같은 패턴을 쓴다. 호스트 명령 버퍼 파싱, 메타데이터 체크섬, 펌웨어 이미지 검증이 그렇다. 크래시 덤프 맨 위에 `_platform_memchr` 이 찍혀 있을 때 그 asm 을 읽는 힘도 여기서 나온다.

ARM-B10 은 스칼라 `strlen/memcpy` 를 짰고, "SIMD 판은 ARM-M07" 로 넘겼다. ARM-M06 은 NEON 레지스터 뷰(`v0.16b/8h/4s`), 실수·정수 내적, 레인 리덕션 기초를 다룬다 (병렬 작성 중 — 여기서는 id 로만 참조). 이번 레슨은 **비교 → 마스크 → 위치**, **넓히며 누적**, **레인별 최소·최대 → 가로 리덕션** 세 가지 기법을 바이트·16비트 데이터에 적용한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">neon_memchr("hello~world~~xyz", '~'=0x7E, 18) — 첫 16 B 청크, lldb 실측 값</text>
  <text x="10" y="50" fill="currentColor">q1</text>
  <g stroke="currentColor">
    <rect x="40" y="34" width="42" height="24" class="f-mut"/><rect x="82" y="34" width="42" height="24" class="f-mut"/>
    <rect x="124" y="34" width="42" height="24" class="f-mut"/><rect x="166" y="34" width="42" height="24" class="f-mut"/>
    <rect x="208" y="34" width="42" height="24" class="f-mut"/><rect x="250" y="34" width="42" height="24" class="f-acc"/>
    <rect x="292" y="34" width="42" height="24" class="f-mut"/><rect x="334" y="34" width="42" height="24" class="f-mut"/>
    <rect x="376" y="34" width="42" height="24" class="f-mut"/><rect x="418" y="34" width="42" height="24" class="f-mut"/>
    <rect x="460" y="34" width="42" height="24" class="f-mut"/><rect x="502" y="34" width="42" height="24" class="f-acc"/>
    <rect x="544" y="34" width="42" height="24" class="f-acc"/><rect x="586" y="34" width="42" height="24" class="f-mut"/>
    <rect x="628" y="34" width="42" height="24" class="f-mut"/><rect x="670" y="34" width="42" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="61" y="51">68 h</text><text x="103" y="51">65 e</text><text x="145" y="51">6c l</text><text x="187" y="51">6c l</text>
    <text x="229" y="51">6f o</text><text x="271" y="51">7e ~</text><text x="313" y="51">77 w</text><text x="355" y="51">6f o</text>
    <text x="397" y="51">72 r</text><text x="439" y="51">6c l</text><text x="481" y="51">64 d</text><text x="523" y="51">7e ~</text>
    <text x="565" y="51">7e ~</text><text x="607" y="51">78 x</text><text x="649" y="51">79 y</text><text x="691" y="51">7a z</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="61" y="30">0</text><text x="271" y="30">5</text><text x="523" y="30">11</text><text x="565" y="30">12</text><text x="691" y="30">15</text>
  </g>
  <text x="300" y="80" fill="currentColor">cmeq v1.16b, v1.16b, v0.16b   (v0 = dup 0x7E)</text>
  <text x="10" y="108" fill="currentColor">v1</text>
  <g stroke="currentColor">
    <rect x="40" y="92" width="210" height="24" class="f-mut"/><rect x="250" y="92" width="42" height="24" class="f-acc"/>
    <rect x="292" y="92" width="210" height="24" class="f-mut"/><rect x="502" y="92" width="84" height="24" class="f-acc"/>
    <rect x="586" y="92" width="126" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="145" y="109">00 00 00 00 00</text><text x="271" y="109">ff</text><text x="397" y="109">00 00 00 00 00</text>
    <text x="544" y="109">ff ff</text><text x="649" y="109">00 00 00</text>
  </g>
  <text x="220" y="140" fill="currentColor">shrn v1.8b, v1.8h, #4 — 16비트 레인(바이트 2개)마다 비트 [11:4] 를 남김</text>
  <g stroke="currentColor">
    <rect x="40" y="152" width="84" height="24" class="f-mut"/><rect x="124" y="152" width="84" height="24" class="f-mut"/>
    <rect x="208" y="152" width="84" height="24" class="f-acc"/><rect x="292" y="152" width="84" height="24" class="f-mut"/>
    <rect x="376" y="152" width="84" height="24" class="f-mut"/><rect x="460" y="152" width="84" height="24" class="f-acc"/>
    <rect x="544" y="152" width="84" height="24" class="f-acc"/><rect x="628" y="152" width="84" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="82" y="169">00</text><text x="166" y="169">00</text><text x="250" y="169">f0</text><text x="334" y="169">00</text>
    <text x="418" y="169">00</text><text x="502" y="169">f0</text><text x="586" y="169">0f</text><text x="670" y="169">00</text>
  </g>
  <text x="40" y="196" fill="currentColor">출력 바이트 j = (바이트 2j+1 하위 니블)&lt;&lt;4 | (바이트 2j 상위 니블) → 바이트 i ↔ 비트 4i..4i+3</text>
  <rect x="40" y="212" width="672" height="104" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace">
    <text x="56" y="234">fmov x3, d1     x3 = 0x000ff00000f00000   (니블 5, 11, 12 = 0xf)</text>
    <text x="56" y="256">rbit x3, x3     x3 = 0x00000f00000ff000   (비트 순서 뒤집기)</text>
    <text x="56" y="278">clz  x3, x3     x3 = 0x14 = 20             (= 가장 낮은 1비트 위치 = ctz)</text>
    <text x="56" y="300">add  x0, x0, x3, lsr #2   → p + 5   (20 / 4 = 첫 일치 바이트)</text>
  </g>
</svg>
```

### Part A — 세 함수 (어셈블리 `.S`)

- `const void *neon_memchr(const void *p, int c, size_t n)` — C 의 `memchr` 와 같은 계약이다 (C11 7.24.5.1: c 는 `unsigned char` 로 바꿔 비교). n ≥ 16 이면 16 B 청크마다 `CMEQ → SHRN → FMOV → CBNZ`. n < 16 이면 스칼라 루프. 꼬리 1..15 B 는 **마지막 16 B 를 겹쳐 다시 읽는다**.
- `uint64_t checksum_u8(const uint8_t *p, size_t n)` — 바이트 합. `UADALP v0.8h, v1.16b` 로 8개 u16 레인에 쌓고, **128 스텝마다** `UADDLV` 로 비워 u64 총합에 더한다. 꼬리 0..15 B 는 스칼라.
- `void minmax_u16(const uint16_t *s, size_t n, uint16_t *mn, uint16_t *mx)` — 12비트 ADC 샘플 버퍼의 최소·최대값. 8 레인 `UMIN/UMAX` → `UMINV/UMAXV`. 꼬리 0..7 은 `CSEL`. n = 0 이면 항등원 `mn = 0xFFFF, mx = 0` 이다.
- 테스트: 길이 0..300 × 정렬 0..15 × 일치 모드 6종(없음/처음/끝/전부/임의 1개/같은 청크 2개)을 C 기준 모델과 비교한다. 범위 **바로 뒤 32 B 를 전부 c 로** 채워서, 범위 밖을 보는 구현은 "가짜 일치" 로 걸린다.

### Part B — 경계·지평·심어 둔 버그

- **가드 페이지**: `mmap` 3 페이지 `[PROT_NONE][RW][PROT_NONE]`. 버퍼가 RW 페이지 끝에서 끝나는 경우와 시작에서 시작하는 경우 모두 n = 0..300 을 돌린다. 한 바이트라도 넘으면 SIGBUS 로 죽는다.
- **u16 지평**: 0xFF × 2048 B(정확히 128 스텝)와 0xFF × 2064 B(129 스텝), 그리고 1 MB.
- **버그 6종**(`shared_bugs.S`)을 하네스가 실제로 잡는지 `CHECK` 한다. 그중 둘은 "약한 테스트는 통과한다" 는 것까지 단언한다. `bug_checksum_nofold` 는 2048 B 에서 맞는다. `bug_minmax_signed` 는 12비트 데이터 퍼즈를 통과한다.
- 범위를 넘어 읽는 버그는 `fork` 한 자식 프로세스에서 페이지 끝 5 B 를 읽혀 **실제로 죽는지** 확인한다.

### Part C — 펌웨어 시나리오

- **UART 프레임 파서**: `0x7E | 본문 | 0x7E | 본문 | ...` 스트림 (HDLC 비슷한 형식, 이스케이프는 생략). `neon_memchr` 로 구분자를 찾는다. 본문 바이트 합의 하위 8비트가 0 인지 `checksum_u8` 로 검사한다. 빈 본문(유휴 0x7E 연속)은 건너뛴다. 끝의 미완성 프레임은 다음 수신을 기다린다. 200 프레임 중 29개를 일부러 망가뜨리고 good/bad 개수를 정확히 맞힌다.
- **ADC 윈도우 통계**: 999 샘플(8 × 124 + 꼬리 7) 삼각파의 peak-to-peak 과 클리핑 여부를 진폭 0..2400 에 대해 계산한다. 드라이버가 변환 오류일 때 쓰는 `0xFFFF` 샘플 하나를 **오류로 잡아야** 한다. signed 버그 판은 이걸 놓친다.

## 2. Problem (English)

> **Byte-stream processing with AArch64 Advanced SIMD (NEON).**
>
> Implement as leaf functions in a `.S` file (no stack, only caller-saved registers, `v8`–`v15` untouched):
>
> 1. `const void *neon_memchr(const void *p, int c, size_t n)` — same contract as `memchr`. Compare 16 bytes per step; locate the first match without a scalar re-scan.
> 2. `uint64_t checksum_u8(const uint8_t *p, size_t n)` — exact sum of all bytes for any `n`. State the overflow horizon of every accumulator you use.
> 3. `void minmax_u16(const uint16_t *s, size_t n, uint16_t *mn, uint16_t *mx)` — unsigned min/max of 16-bit ADC samples; for `n == 0` return `0xFFFF / 0`.
>
> Constraints: never read outside `[p, p+n)` (buffers may end at an unmapped page); any `n` and any alignment (`s` is 2-byte aligned). No libc calls.
>
> Example: `neon_memchr("hello~world~~xyz!?", '~', 18)` → offset 5. `checksum_u8` of 2064 bytes of `0xFF` → 526320. `minmax_u16({2048, 2100, 0xFFFF, 1990, …, 1500, 2600}, 10)` → `1500 / 65535`.
>
> Follow-ups: AArch64 has no `PMOVMSKB` — how do you get the index? How does libc's `memchr` read past `n` safely? Why does your u16 accumulator need flushing every 2 KB? Why can a signed min/max pass your ADC tests and still be wrong? Why doesn't SIMD speed up a byte histogram? How fast is it versus scalar, and why is clang's auto-vectorized sum slower than yours?

## 3. 시작 코드

테스트 하네스 (starter 와 solution 이 같이 쓴다. C 기준 모델은 `volatile` 이라 clang 이 벡터화하거나 libc 호출로 바꾸지 못한다):

<!-- code: harness.c -->

어셈블리 시작 코드 — 세 함수 모두 "틀렸지만 안전한" 스텁이다. memchr 는 항상 NULL, 합은 0, minmax 는 빈 집합 값을 쓴다. 메모리는 읽지 않는다:

<!-- code: starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cd lessons/ARM/ARM-M07
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S shared_bugs.S -o /tmp/ARM-M07_starter && /tmp/ARM-M07_starter
```

`shared_bugs.S`(심어 둔 버그 6종, 7. 자주 하는 실수)는 모든 빌드에 같이 링크된다. 처음 실행 결과 (실제 출력. PASS 줄은 빼고 FAIL 일부는 `...` 로 줄였다, exit 1):

```text
FAIL harness.c:219  neon_memchr(s, 'l', 13) == s + 2
...
FAIL harness.c:229  fuzz_memchr(neon_memchr) == 0
FAIL harness.c:232  checksum_u8(d, 5) == 511
...
FAIL harness.c:264  bad == 0
FAIL harness.c:273  checksum_u8(ff, 2048) == 255u * 2048
...
FAIL harness.c:309  good.error == 1
14/35 passed
```

스텁인데도 14개가 PASS 다. "없음/빈 입력" 단언 4개(NULL·합 0·빈 minmax)와 "버그가 잡히는가" 단언 10개다. 버그 단언은 `shared_bugs.S` 를 대상으로 하므로 starter 에서도 이미 참이다.

## 4. 힌트

<details>
<summary>힌트 1 — 비교 결과는 0x00/0xFF 바이트 16개. 이걸 GPR 로 옮길 방법</summary>

`cmeq v1.16b, v1.16b, v0.16b` 를 하면 같은 레인은 0xFF, 다른 레인은 0x00 이 된다. 이제 "어느 레인이 처음 0xFF 인가" 를 알아야 한다. x86 은 `PMOVMSKB` 한 명령으로 레인마다 1비트씩 모은다. AArch64 에는 그 명령이 없다. 대신 16비트 레인 8개를 **4비트씩 오른쪽으로 밀며 좁히면** (`shrn v.8b, v.8h, #4`) 바이트 하나가 니블 하나가 된다. 결과는 64비트이고 `fmov x3, d1` 로 꺼낸다.

</details>

<details>
<summary>힌트 2 — 첫 일치 = 가장 낮은 1비트. ctz 명령이 없다면?</summary>

리틀 엔디언이라 레인 0 이 마스크의 최하위 니블이다. 그래서 가장 낮은 1비트를 찾으면 된다. AArch64 기본 ISA 에는 ctz 가 없다 (FEAT_CSSC 의 `CTZ` 는 최신 확장, 확인 권장). 대신 `rbit` 로 비트를 뒤집은 뒤 `clz` 한다. 그 값을 4 로 나누면(`lsr #2`) 바이트 인덱스다. `add x0, x0, x3, lsr #2` 는 시프트된 레지스터를 더하는 한 명령이다.

</details>

<details>
<summary>힌트 3 — n % 16 꼬리: 넘어 읽지 않으면서 벡터로 끝내는 법</summary>

n ≥ 16 이었다면 남은 r(1..15) 바이트를 위해 **끝에서 16 B 앞**(`p + n − 16`)부터 한 번 더 읽으면 된다. 앞부분 16 − r 바이트는 이미 "일치 없음" 으로 확인됐다. 그러니 이 청크에서 처음 나오는 일치가 곧 답이다. n < 16 이면 겹칠 앞부분이 없으니 스칼라로 간다. 레지스터를 `x0 = end − 16`, `x2 = 16` 으로 놓고 루프 본체로 한 번 더 뛰면 코드가 짧아진다.

</details>

<details>
<summary>힌트 4 — UADALP 한 번에 레인당 최대 얼마가 더해지나</summary>

`uadalp v0.8h, v1.16b` 는 인접한 바이트 두 개를 더해(최대 255 + 255 = 510) u16 레인에 누적한다. u16 최댓값 65535 를 510 으로 나누면 128.5 다. 그래서 **128 스텝(2048 B)** 이 한계다. 카운터 `x3 = 128` 을 두고 0 이 되면 `uaddlv s2, v0.8h` 로 레인 8개를 합쳐(최대 8 × 65535 < 2³²) x2 에 더한 뒤 v0 을 0 으로 다시 시작한다. minmax 는 넘칠 일이 없다. 대신 **unsigned** 비교(`umin`, `csel ... lo/hi`)인지 확인하자.

</details>

## 5. 풀이

### Part A — `neon_memchr` 한 줄씩

| 명령 | 하는 일 | 왜 |
|---|---|---|
| `and w1, w1, #0xff` | c → `(unsigned char)c` | 스칼라 경로의 `cmp w3, w1` 이 0x141 과 0x41 을 다르게 본다. `dup` 은 어차피 하위 8비트만 쓴다 |
| `cmp x2, #16` / `b.lo 3f` | n < 16 → 스칼라 | 16 B 를 읽으면 범위를 넘는다 |
| `dup v0.16b, w1` | c 를 16 레인에 복제 | 루프 밖에서 한 번만 |
| `1: ldr q1, [x0]` | 16 B 읽기 | 정렬 불필요 (Normal 메모리, EL0) |
| `cmeq v1.16b, v1.16b, v0.16b` | 레인별 같음 → 0xFF | 결과가 "마스크" 형태라 바로 AND/OR 가능 |
| `shrn v1.8b, v1.8h, #4` | 128비트 → 64비트 니블 마스크 | PMOVMSKB 대용 (Q1) |
| `fmov x3, d1` | SIMD → GPR | 하위 64비트만 옮긴다 |
| `cbnz x3, 2f` | 일치 있으면 위치 계산 | |
| `add/sub/cmp/b.hs` | p += 16, n −= 16, 16 이상이면 반복 | |
| `cbz x2, 5f` | 딱 떨어졌으면 NULL | |
| `add x0, x0, x2` / `sub x0, x0, #16` / `mov x2, #16` / `b 1b` | 마지막 16 B 를 겹쳐 한 번 더 | 꼬리도 벡터로, 넘어 읽기 없음 |
| `2: rbit` / `clz` / `add x0, x0, x3, lsr #2` | ctz/4 = 인덱스 | |

lldb 로 그림 1의 예를 한 명령씩 실행했다 (`demo_trace.c` + `demo_trace.lldb`, 원본 `trace_m6.txt`):

```bash
cc -std=c11 -g -O0 demo_trace.c solution.S -o /tmp/ARM-M07_trace && lldb -b -s demo_trace.lldb /tmp/ARM-M07_trace
```

```text
(lldb) register read x0 x1 x2
      x0 = 0x0000000100000678  ARM-M07_trace`main.frame
      x1 = 0x000000000000007e
      x2 = 0x0000000000000012
(lldb) register read v0 v1            // ldr q1 직후
      v0 = {0x7e 0x7e 0x7e 0x7e 0x7e 0x7e 0x7e 0x7e 0x7e 0x7e 0x7e 0x7e 0x7e 0x7e 0x7e 0x7e}
      v1 = {0x68 0x65 0x6c 0x6c 0x6f 0x7e 0x77 0x6f 0x72 0x6c 0x64 0x7e 0x7e 0x78 0x79 0x7a}
(lldb) register read v1               // cmeq 직후
      v1 = {0x00 0x00 0x00 0x00 0x00 0xff 0x00 0x00 0x00 0x00 0x00 0xff 0xff 0x00 0x00 0x00}
(lldb) register read v1               // shrn 직후 (상위 8 바이트는 0 으로 지워진다)
      v1 = {0x00 0x00 0xf0 0x00 0x00 0xf0 0x0f 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00}
(lldb) register read x3               // fmov 직후
      x3 = 0x000ff00000f00000
(lldb) register read x3               // rbit 직후
      x3 = 0x00000f00000ff000
(lldb) register read x3               // clz 직후
      x3 = 0x0000000000000014
(lldb) register read x0               // add 직후
      x0 = 0x000000010000067d  ARM-M07_trace`main.frame + 5
index = 5
```

세 가지를 읽어 두자.

1. lldb 는 Apple 식 표기(`cmeq.16b v1, v1, v0`, `shrn.8b v1, v1, #0x4`)로 보여 준다. 같은 명령이다. 소스의 `v1.16b` 처럼 레인 모양을 각 피연산자에 붙이는 건 GNU/Arm 표준 문법이다.
2. `shrn` 처럼 결과가 64비트(`.8b`)인 SIMD 명령은 **상위 64비트를 0 으로 지운다**. 위 덤프에서 v1 의 상위 8 바이트가 0 이 된 게 그 증거다. `fmov x3, d1` 은 하위 64비트만 가져간다.
3. 니블 5·11·12 가 0xf 다. 같은 청크의 일치 3개가 전부 마스크에 남는다. 그중 **가장 낮은 것** 이 답이다. `rbit` 를 빠뜨리면 `clz` 가 가장 높은 니블 12 에서 센 값(12)을 준다. 그러면 12 / 4 = 3 이 나와서 엉뚱한 위치가 된다 (버그 1, 실측 `norbit=3`).

같은 아이디어를 intrinsic 으로 쓰면 (`demo_intrin.c`, `clang -O2 -S` 실제 출력):

```text
_first_match16:                    // vceqq_u8 / vshrn_n_u16 / vget_lane_u64 / __builtin_ctzll
	ldr	q0, [x0]
	dup.16b	v1, w1
	cmeq.16b	v0, v0, v1
	shrn.8b	v0, v0, #4
	fmov	x8, d0
	rbit	x8, x8
	clz	x8, x8
	lsr	w0, w8, #2                 // mask==0 → clz=64 → 16 : 분기 "mask ? ... : 16" 이 사라졌다
	ret
_sum16:                            // vpaddlq_u8 + vaddlvq_u16 → 한 명령으로 합쳐짐
	ldr	q0, [x0]
	uaddlv.16b	h0, v0
	fmov	w0, s0
	ret
```

| asm | intrinsic (`<arm_neon.h>`) |
|---|---|
| `dup v.16b, w` / `ldr q` | `vdupq_n_u8` / `vld1q_u8` |
| `cmeq v.16b` | `vceqq_u8` |
| `shrn v.8b, v.8h, #4` | `vshrn_n_u16(vreinterpretq_u16_u8(x), 4)` |
| `fmov x, d` | `vget_lane_u64(vreinterpret_u64_u8(x), 0)` |
| `uadalp v.8h, v.16b` / `uaddlv s, v.8h` | `vpadalq_u8` / `vaddlvq_u16` |
| `umin/umax v.8h` / `uminv/umaxv h` | `vminq_u16`/`vmaxq_u16` / `vminvq_u16`/`vmaxvq_u16` |

### Part A — 꼬리와 페이지: 세 가지 전략

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">n = 37, 버퍼 끝 = 페이지 끝 (다음 페이지 PROT_NONE). 16 B 정렬 경계 = 점선</text>
  <g stroke="currentColor" stroke-dasharray="3 3">
    <line x1="60" y1="30" x2="60" y2="290"/><line x1="220" y1="30" x2="220" y2="290"/>
    <line x1="380" y1="30" x2="380" y2="290"/><line x1="540" y1="30" x2="540" y2="290"/>
  </g>
  <line x1="590" y1="26" x2="590" y2="292" stroke="currentColor" stroke-width="3"/>
  <rect x="590" y="30" width="140" height="260" class="f-warn" stroke="currentColor"/>
  <text x="600" y="50" fill="currentColor">PROT_NONE 페이지</text>
  <text x="600" y="68" fill="currentColor">닿으면 SIGBUS</text>
  <text x="520" y="44" fill="currentColor">페이지 끝</text>
  <rect x="220" y="52" width="370" height="20" class="f-mut" stroke="currentColor"/>
  <text x="300" y="66" fill="currentColor">[p, p+37)   (p = 정렬 + 0)</text>
  <text x="10" y="102" fill="currentColor">①</text>
  <rect x="220" y="88" width="160" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="380" y="88" width="160" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="430" y="112" width="160" height="20" class="f-ok" stroke="currentColor"/>
  <text x="30" y="102" fill="currentColor">내 방식</text>
  <text x="30" y="126" fill="currentColor">겹침 꼬리</text>
  <text x="440" y="126" fill="currentColor">end−16 .. end (겹침)</text>
  <text x="10" y="166" fill="currentColor">②</text>
  <text x="30" y="166" fill="currentColor">버그 3</text>
  <rect x="220" y="152" width="160" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="380" y="152" width="160" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="540" y="152" width="160" height="20" class="f-warn" stroke="currentColor"/>
  <text x="550" y="166" fill="currentColor">16 B 통째로 → 넘침</text>
  <text x="10" y="216" fill="currentColor">③</text>
  <text x="30" y="216" fill="currentColor">정렬 내림</text>
  <text x="30" y="232" fill="currentColor">(libc)</text>
  <rect x="60" y="202" width="160" height="20" class="f-mut" stroke="currentColor"/>
  <text x="70" y="216" fill="currentColor">p 가 정렬 + 5 라면</text>
  <rect x="220" y="226" width="160" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="380" y="226" width="160" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="540" y="226" width="50" height="20" class="f-ok" stroke="currentColor"/>
  <text x="70" y="262" fill="currentColor">정렬된 16 B 읽기는 16 의 배수 경계(페이지 경계 포함)를 절대 넘지 않는다.</text>
  <text x="70" y="280" fill="currentColor">p 앞·p+n 뒤 바이트는 읽지만 같은 블록 안 → 마스크로 지우고 n 과 비교 (Q3)</text>
</svg>
```

| 전략 | 어디서 | 장점 | 주의 |
|---|---|---|---|
| ① 비정렬 루프 + **겹침 마지막 청크** + n<16 스칼라 | 이 레슨 `neon_memchr` | 범위 밖을 전혀 안 읽음. 검증 쉬움 | n<16 은 스칼라 (최대 15 바퀴) |
| ② 그냥 16 B 씩 끝까지 | 버그 3 | 짧다 | n 밖 일치 반환 + 페이지 끝에서 **SIGBUS** (실측) |
| ③ **정렬 내림** 첫 청크 + 앞부분 마스킹 + 끝에서 인덱스 ≥ n 이면 NULL | Apple libc, 연습 2 `strlen_neon` | n 모르는 strlen 에 필수. 분기 적음 | 객체 밖을 읽으므로 C 가 아니라 "구현" 의 특권 (Q3) |

Apple libc `_platform_memchr` 의 실제 디스어셈블리(`libc_memchr_m6.txt`)는 ③ 이다. 핵심 줄만 보자:

```text
    0x18c534030 <+16>:  and    x1, x0, #0xf                    // k = p % 16
    0x18c534034 <+20>:  and    x0, x0, #0xfffffffffffffff0     // 정렬 내림
    0x18c534038 <+24>:  ldr    q0, [x0]
    0x18c53403c <+28>:  sub.16b v0, v0, v1                     // byte − c : 일치 레인 = 0
    0x18c534040 <+32>:  adr    x3, 0x18c534000                 // 테이블: {0,1,…,15} 다음 0 × 16 (memory read 로 확인)
    0x18c534044 <+36>:  ldr    q3, [x3], #0x10                 // q3 = 인덱스 벡터 {0..15}
    0x18c534048 <+40>:  sub    x3, x3, x1
    0x18c53404c <+44>:  ldr    q2, [x3]                        // 앞쪽 k 레인만 0 이 아닌 값
    0x18c534050 <+48>:  orr.16b v0, v0, v2                     //  → "p 앞 바이트" 를 일치 아님으로 가림
    0x18c534054 <+52>:  adds   x2, x2, x1                      // n += k (청크 기준 길이)
    ...
    0x18c534068 <+72>:  uminv.16b b2, v0                       // 최솟값 0 = 어딘가 일치 (테스트)
    0x18c53406c <+76>:  fmov   w3, s2
    0x18c534070 <+80>:  cbz    w3, 0x18c534088
    ...
    0x18c534088 <+104>: eor.16b v1, v1, v1
    0x18c53408c <+108>: cmhi.16b v0, v0, v1                    // 비일치 → 0xff, 일치 → 0
    0x18c534090 <+112>: orr.16b v0, v0, v3                     // 일치 레인 = 자기 인덱스
    0x18c534094 <+116>: uminv.16b b0, v0                       // 최솟값 = 첫 일치 인덱스 (위치)
    0x18c534098 <+120>: fmov   w3, s0
    0x18c53409c <+124>: add    x0, x0, x3
    0x18c5340a0 <+128>: eor    x1, x1, x1
    0x18c5340a4 <+132>: cmp    x3, x2
    0x18c5340a8 <+136>: csel   x0, x1, x0, hs                  // 인덱스 ≥ 남은 n 이면 NULL
```

마스크를 GPR 로 옮기지 않는다. **`UMINV` 하나로 테스트하고, 인덱스 벡터를 OR 한 뒤 `UMINV` 한 번 더로 위치를 구한다.** PMOVMSKB 대안이 하나 더 생긴 셈이다 (Q1 표).

### Part A — `checksum_u8`: 넓히며 누적과 지평

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">uadalp v0.8h, v1.16b — 인접 바이트 2개 → u16 레인 하나에 누적</text>
  <g stroke="currentColor">
    <rect x="40" y="34" width="40" height="22" class="f-mut"/><rect x="80" y="34" width="40" height="22" class="f-mut"/>
    <rect x="120" y="34" width="40" height="22" class="f-mut"/><rect x="160" y="34" width="40" height="22" class="f-mut"/>
    <rect x="200" y="34" width="40" height="22" class="f-mut"/><rect x="240" y="34" width="40" height="22" class="f-mut"/>
    <rect x="600" y="34" width="40" height="22" class="f-mut"/><rect x="640" y="34" width="40" height="22" class="f-mut"/>
    <rect x="40" y="96" width="80" height="24" class="f-acc"/><rect x="120" y="96" width="80" height="24" class="f-acc"/>
    <rect x="200" y="96" width="80" height="24" class="f-acc"/><rect x="600" y="96" width="80" height="24" class="f-acc"/>
    <line x1="60" y1="56" x2="80" y2="96"/><line x1="100" y1="56" x2="80" y2="96"/>
    <line x1="140" y1="56" x2="160" y2="96"/><line x1="180" y1="56" x2="160" y2="96"/>
    <line x1="220" y1="56" x2="240" y2="96"/><line x1="260" y1="56" x2="240" y2="96"/>
    <line x1="620" y1="56" x2="640" y2="96"/><line x1="660" y1="56" x2="640" y2="96"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="60" y="50">b0</text><text x="100" y="50">b1</text><text x="140" y="50">b2</text><text x="180" y="50">b3</text>
    <text x="220" y="50">b4</text><text x="260" y="50">b5</text><text x="620" y="50">b14</text><text x="660" y="50">b15</text>
    <text x="80" y="113">h0 +=</text><text x="160" y="113">h1 +=</text><text x="240" y="113">h2 +=</text><text x="640" y="113">h7 +=</text>
    <text x="440" y="50">· · ·</text><text x="440" y="113">· · ·</text>
  </g>
  <text x="300" y="80" fill="currentColor">한 스텝에 레인당 최대 255+255 = 510</text>
  <rect x="40" y="140" width="660" height="98" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="56" y="162">128 스텝 × 510 = 65280 ≤ 65535   → 블록 = 128 × 16 B = 2048 B</text>
    <text x="56" y="184">블록 끝: uaddlv s2, v0.8h  (8 레인 합 ≤ 8 × 65535 = 524280, u32 에 들어감)</text>
    <text x="56" y="206">        fmov w4, s2 ; add x2, x2, x4   (u64 총합: 2⁶⁴ / 255 ≈ 7.2 × 10¹⁶ B 까지 안전)</text>
    <text x="56" y="228">129 스텝이면 65790 → u16 에서 감김 (버그 4: 0xFF × 2064 B → 2032, 정답 526320)</text>
  </g>
</svg>
```

| 누산기 | 한 스텝에 레인당 최대 증가 | 안전한 스텝 수 | 바이트로 |
|---|---|---|---|
| u8 레인 `add v.16b` (바이트 그대로 더하기) | 255 | **1** | 16 B — 쓸 수 없다 |
| u8 레인 `sub v.16b, mask` (일치 개수, 연습 1) | 1 | 255 | 4080 B |
| u16 레인 `uadalp v.8h, v.16b` (이 레슨) | 510 | 128 | 2048 B |
| u32 레인 `uadalp v.4s, v.8h` (블록마다 u16 → u32) | 2 × 65280 = 130560 / 블록 | 32896 블록 | ≈ 64 MiB |
| u32 스칼라 총합 | 255 / 바이트 | — | ⌊(2³²−1)/255⌋ = 16,843,009 B ≈ 16 MiB |
| u64 스칼라 총합 (이 레슨) | 255 / 바이트 | — | 사실상 무한 |

2048 B 마다 `uaddlv` + `fmov` + `add` 3 명령을 쓴다. 128 스텝 루프에 비하면 비용이 무시할 만하다. 그 대가로 u64 결과가 어떤 n 에도 정확하다. 1 MB 0xFF 테스트가 `255 × 2²⁰ = 267,386,880` 을 정확히 맞힌다.

**clang 자동 벡터화와 비교.** `demo_bench.c` 의 평범한 C 루프 `for (...) s += p[i];` (`uint64_t s`)를 clang -O2 는 이렇게 만들었다 (`objdump -d`, 발췌):

```text
stp	d11, d10, [sp, #-0x20]!          // 누산기가 모자라 callee-saved d8–d11 까지 저장
...
ldp	q28, q29, [x8, #-0x10]           // 루프: 32 B
tbl.16b	v30, { v28 }, v5                // 바이트 → 64비트 레인으로 흩기 (TBL 8번)
...
uaddw2.2d	v1, v1, v31                 // u64 레인 누산기 16개에 더하기 (UADDW/UADDW2 16번)
```

컴파일러는 "u16 레인은 128 스텝 뒤에 넘친다" 는 지평 논리를 모른다. 그래서 C 의 의미(u64 덧셈)를 그대로 지키려고 **바이트를 바로 u64 레인으로 넓힌다**. 32 B 당 SIMD 명령이 20개를 넘는다. 손 asm 은 16 B 당 `ldr + uadalp` 두 개다. 그 차이가 아래 벤치의 대체로 2~3배다. 자동 벡터화 분석 자체는 ARM-A11 의 몫이다.

### Part A — `minmax_u16`

- 8 레인 최솟값을 `0xFFFF`(`dup v0.8h, w4`)로, 최댓값을 0(`movi v1.2d, #0`)으로 시작한다. 이 값이 최소·최대 연산의 **항등원**이다. 그래서 n = 0 일 때 그대로 `0xFFFF / 0` 이 나온다. "빈 윈도우" 를 호출자가 `mn > mx` 로 알아볼 수 있다.
- 루프는 8 샘플마다 `umin v0.8h` / `umax v1.8h` 한 번씩이다. 끝나면 `uminv h0, v0.8h` / `umaxv h1, v1.8h` 로 레인 8개를 가로로 줄인다. 그 뒤 `umov w4, v0.h[0]` 로 꺼낸다.
- 꼬리 0..7 은 `ldrh` + `cmp` + `csel lo/hi` 다. `lo/hi` 가 **unsigned** 조건이다 (ARM-B05).
- `uminv` 같은 가로 연산은 레인 방향 연산보다 지연이 길다 (일반론, 코어별 수치 확인 권장). 그래서 루프 안에서는 레인별 `umin` 만 하고 가로 연산은 끝에서 한 번만 한다. memchr 의 테스트용 `uminv`(libc)와 다른 점이다.

### Part B — 테스트가 진짜 잡는가 (실측)

`demo_bugs.c` 로 버그 판과 정답 판의 반환값을 나란히 찍었다 (원본 `demo_bugs_m6.txt`):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c solution.S shared_bugs.S -o /tmp/ARM-M07_bugs && /tmp/ARM-M07_bugs
```

```text
memchr("hello~world~~xyz", '~', 16): good=5  norbit=3
memchr("hello", 0x100+'l', 5):       good=2  nomask=0x0
memchr("hello~world~~xyz", 'z', 13): good=0x0  overread=15 (past n!)
checksum(0xFF x 2048): good=522240  nofold=522240
checksum(0xFF x 2064): good=526320  nofold=2032
checksum(0xFF x 4096): good=1044480  nofold=520192
minmax good   : mn= 1500 mx=65535
minmax signed : mn=65535 mx= 2600
minmax notail : mn= 1990 mx=65535
neon_memchr        (page_end-5, 'Z', 5) = 0x0, no fault
bug_memchr_overread(page_end-5, 'Z', 5) -> killed by signal 10 (Bus error: 10)
```

- **nofold**: 129 스텝째에 레인마다 65790 → 254 (mod 65536) 로 감긴다. 8 레인 × 254 = **2032**. 2048 B 에서는 정답과 같다. 그래서 하네스는 "2048 에서 같고 2064 에서 다르다" 를 **둘 다** 단언한다. 테스트 데이터를 지평 바로 앞에서 멈추면 이 버그를 놓친다.
- **signed**: `0xFFFF` 를 int16 −1 로 본다. 그래서 그 샘플이 최솟값이 되고 최댓값 후보에서 빠진다. 12비트 ADC 데이터(0..4095)는 int16 양수 범위이므로 **12비트 퍼즈는 0 건 불일치로 통과한다**. 하네스가 `fuzz_minmax(bug_minmax_signed, 12) == 0` 과 `fuzz_minmax(bug_minmax_signed, 16) > 0` 을 같이 단언하는 이유다.
- **overread**: 맥에서 PROT_NONE 페이지 접근은 SIGSEGV 가 아니라 **SIGBUS(10)** 로 왔다 (리눅스는 보통 SIGSEGV). 하네스는 자식 프로세스에서 이걸 재현하고 "죽었다" 를 PASS 로 센다.
- 하네스 퍼즈의 첫 불일치 지점 (solution 실행 실제 출력): norbit `len=16 mode=1`, nomask `len=1 c=360`, overread `len=1 mode=0`, signed `got 63622/20727 want 20727/63622`, notail `len=1 got 65535/0`.
- ASan 은 C 코드 접근만 계측한다. `.S` 의 `ldr q` 는 보지 못한다. 그래서 asm 의 넘어 읽기는 **가드 페이지와 "범위 뒤를 c 로 채우기"** 가 지킨다. 실제로 ASan/UBSan 빌드도 35/35 로 조용했다.

### Part C — 펌웨어 시나리오

**UART 프레임.** `parse_frames()` 는 구분자 사이 본문을 `checksum_u8(...) & 0xFF == 0` 으로 검증한다. 200 프레임(본문 0..300 B), 7개 중 1개 꼴로 첫 바이트를 +1(또는 +2) 해서 29개를 망가뜨렸다. 5개마다 유휴 0x7E 를 하나 더 넣고, 끝에는 40 B 미완성 프레임을 붙였다. 결과는 `good + bad == 200 && bad == 29` 이고, 스칼라 기준 파서와도 일치한다. 실제 HDLC/SLIP 은 본문 안의 0x7E 를 `0x7D 0x5E` 로 이스케이프하고, 체크섬은 바이트 합 대신 CRC-16 을 쓴다 (Q7). 구분자 찾기 + 길이 계산 + 무결성 검사로 나뉘는 구조는 같다.

**ADC 윈도우.** 999 샘플 삼각파 `2048 ± A` 를 0..4095 로 자른다. A = 0..2400(100 단위)에서 p2p = `min(2048+A, 4095) − max(2048−A, 0)` 이다. clipped 는 A ≥ 2047 일 때다. 25 윈도우 모두 맞았다. 그다음 인덱스 517 에 `0xFFFF` 하나를 심었다. 정답 판은 `mx = 65535 > 4095` → **error = 1** 이다. signed 버그 판은 `mx` 가 정상 범위로 나와 error = 0, 즉 고장 난 센서 데이터를 정상으로 넘긴다. 실제 STM32 ADC 드라이버는 오버런 플래그(OVR)를 따로 준다. 그래도 "범위 밖 값 = 고장" 을 데이터 경로에서 한 번 더 거르는 건 흔한 방어 코드다.

### Part C — 벤치마크 (`demo_bench.c`, M6, 실측)

```bash
cc -std=c11 -O2 demo_bench.c solution.S -o /tmp/ARM-M07_bench && /tmp/ARM-M07_bench
```

같은 바이너리를 연달아 3번 돌렸다. 각 칸은 31 샘플의 중앙값이고, 샘플 하나는 약 16 MB 를 처리한다. **부하 평균 33~35**(다른 작성 에이전트 10개 동시 실행)라 소음이 크다. 아래는 세 번째 실행의 원본이고, 3회 범위는 그 아래 표에 있다 (`bench_m6.txt`).

```text
GB/s (median of 31)      size:      64 B     4 KB     1 MB
memchr  scalar C (no vec)               3.40     3.68     3.32 
memchr  neon asm                       32.57    45.68    62.23 
memchr  libc                           28.87    51.02    47.47 
sum8    scalar C (no vec)               3.49     3.31     2.98 
sum8    clang auto-vec C                9.85    14.64    14.84 
sum8    neon asm                       42.11    31.41    30.27 
minmax  scalar C (no vec)               3.35     3.37     3.69 
minmax  neon asm                       37.40    34.54    20.90 
```

| 비교 (4 KB, 3회 범위) | GB/s | 배율 |
|---|---|---|
| memchr 스칼라 / NEON / libc | 2.9–3.7 / 45.7–55.8 / 27.0–51.0 | 실행마다 NEON ÷ 스칼라 = 12–19, libc 와 비슷 |
| 바이트 합 스칼라 / 자동 벡터화 / NEON | 2.9–3.8 / 10.1–15.1 / 18.4–31.4 | 실행마다 NEON ÷ 자동 벡터화 = 2.9, 1.2, 2.1 |
| minmax 스칼라 / NEON | 3.0–3.4 / 34.4–37.9 | × 10 이상 |

해석 (가설 포함, 부하가 큰 상태의 수치):

- 스칼라 바이트 루프가 3 GB/s 근처에서 일정하다. 바퀴당 명령 4~6개(분기 포함)로 1 바이트를 처리하기 때문이다 (ARM-B10 의 바이트 memcpy ~4 GB/s 와 같은 체급).
- NEON memchr 는 4 KB 에서 바퀴당 16 B 를 0.3 ns 안팎에 처리한다. 클럭이 4 GHz 대라면 바퀴당 1~1.5 사이클이다 (클럭 미확인, ARM-M03 의 보정 방법 참고). libc 도 같은 16 B/바퀴 구조라 비슷하다. 더 빠르게 하려면 32~64 B 언롤링 후 `umaxp`/`orr` 로 합쳐서 한 번만 분기하면 된다.
- 64 B 에서는 두 asm 모두 호출 오버헤드와 꼬리 처리가 커서 느리다. 1 MB 는 L2 안에 들어가므로(크기 추정) 메모리 대역폭이 한계가 아니다. 1 MB 칸의 출렁임(20~62)은 부하 소음으로 본다.
- minmax 1 MB 가 4 KB 보다 낮게(20~30) 나온 건 3회 모두 같았다. memchr 1 MB 는 떨어지지 않았으니 단순한 캐시 크기 탓으로 단정하기 어렵다. 원인은 확인하지 못했다. "재현됐지만 원인 미상" 으로 남긴다.

### 같은 .S 를 리눅스(ELF)에서 — Lima VM 실측

```bash
# <lesson> = 이 디렉터리 절대 경로, 뒤쪽 practice 빌드·readelf 는 ... 로 줄임
limactl shell elinux -- bash -lc 'mkdir -p /tmp/ARM-M07_work && cp -f <lesson>/{harness.c,solution.S,shared_bugs.S,practice.c,practice.S} /tmp/ARM-M07_work/ && cd /tmp/ARM-M07_work && gcc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_bugs.S -o sol && ./sol | tail -1 && ...'
```

```text
35/35 passed                                (gcc 15.2.0, aarch64 Linux, PAGESIZE 4096)
10/10 passed                                (practice)
   101: 0000000000002cf0   108 FUNC    GLOBAL DEFAULT   12 minmax_u16
   123: 0000000000002c90    96 FUNC    GLOBAL DEFAULT   12 checksum_u8
   138: 0000000000002c10   124 FUNC    GLOBAL DEFAULT   12 neon_memchr
```

처음 VM 빌드에서는 gcc 가 하네스의 `if (v < 0) v = 0; if (v > 4095) v = 4095;` 한 줄을 `-Werror=misleading-indentation` 으로 거부했다. clang 은 통과시켰던 줄이다. 삼항식으로 고친 뒤 양쪽 모두 경고 0 이다. 페이지 크기가 16 KB(맥)와 4 KB(VM)로 달라도 가드 페이지 테스트는 `getpagesize()` 를 쓰므로 같은 코드로 돈다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_bugs.S -o /tmp/ARM-M07_solution && /tmp/ARM-M07_solution
# … 35/35 passed  (0.21 s, exit 0)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined harness.c solution.S shared_bugs.S -o /tmp/ARM-M07_asan && /tmp/ARM-M07_asan
# 35/35 passed, stderr 0 줄
```

## 7. 자주 하는 실수

심어 둔 버그 6종 (모든 빌드에 링크되고, 하네스가 잡는 걸 단언한다):

<!-- code: shared_bugs.S -->

| 실수 | 증상 (실측) | 고친 코드 |
|---|---|---|
| `clz` 만, `rbit` 없음 | 같은 청크의 **마지막** 일치 쪽에서 셈: `norbit=3` (정답 5) | `rbit` → `clz` (= ctz) |
| `(unsigned char)c` 생략 | 벡터 경로는 맞고(`dup` 이 하위 8비트) **스칼라 경로만** 틀림: `0x100+'l'` → NULL | 함수 첫 줄 `and w1, w1, #0xff` |
| 꼬리도 16 B 통째 | 범위 밖 'z' 를 반환(`overread=15`), 페이지 끝에서 **SIGBUS** | 겹침 마지막 청크 또는 정렬 내림 + `idx < n` 검사 |
| u16 누산기를 안 비움 | 0xFF × 2064 B → 2032 (정답 526320) | 128 스텝마다 `uaddlv` 로 비우기 |
| `smin/smax` (signed) | 12비트 데이터는 통과, `0xFFFF` → mn=65535, mx=2600 | `umin/umax`, 꼬리 `csel lo/hi`, `ldrh` |
| n % 8 꼬리 무시 | n = 10 에서 1500/2600 을 놓침 → 1990/65535 | 꼬리 스칼라 루프 |
| 루프 안에서 `uminv` 로 매번 가로 리덕션 | 정답은 맞지만 가로 연산이 매 바퀴 들어감 | 레인별 `umin` 누적, 마지막에 한 번 |
| v8–v15 를 임시로 사용 | 테스트는 통과할 수 있음. 호출자의 d8–d15(하위 64비트)가 깨짐 | v0–v7, v16–v31 만 사용 (AAPCS64) |

마지막 두 줄은 이 하네스로 잡히지 않는다. 앞쪽은 성능 문제다. 뒤쪽은 ABI 위반이라 호출자가 그 레지스터를 쓰고 있어야만 드러난다. ARM-M01 의 ABI 검사기 같은 도구가 필요한 종류다.

## 8. 면접 Q&A

<details>
<summary>Q1. NEON 에는 PMOVMSKB 가 없다. 16 바이트 비교 결과에서 첫 일치 위치를 어떻게 얻나?</summary>

| 방법 | 명령 | 특징 |
|---|---|---|
| **SHRN 니블 마스크** (이 레슨) | `shrn v.8b, v.8h, #4` → `fmov x, d` → `rbit/clz` → `lsr #2` | SHRN 부터 인덱스까지 5 명령. 바이트당 4비트. glibc/Arm Optimized Routines 의 최근 memchr/strlen 이 이 방식이다 (확인 권장) |
| **UMAXV 로 테스트 → 위치는 따로** | `umaxv b, v.16b` → `fmov w` → `cbnz` | 테스트는 가볍다. 위치는 찾은 뒤 다시 계산한다 |
| **UMINV + 인덱스 벡터** (Apple libc) | 비일치 0xFF, 일치는 자기 인덱스 → `uminv` | GPR 비트 연산 없음. 가로 연산 지연이 두 번 든다 |
| **가중치 AND + ADDP** | 마스크 & {1,2,4,…,128,…} → `addp` 3번 → 16비트 비트마스크 | 진짜 movemask 와 같은 결과. 명령이 많다 |

x86 은 비트 1개/바이트(16비트) 마스크를 주고, 니블 방식은 64비트 마스크를 준다. 인덱스를 구할 때 `/4` 가 한 번 더 들어갈 뿐이다. 왜 PMOVMSKB 가 없는지는 Arm 이 공식 이유를 밝히지 않았다. 일반적인 설명은 "벡터 → GPR 경로의 비용 + 가로 연산 대신 레인 연산을 권장하는 설계" 다 (추정). SVE 는 비교 결과를 아예 **predicate 레지스터**로 주고, `BRKB`/`CNTP` 로 위치를 센다.

</details>

<details>
<summary>Q2. n 이 16 의 배수가 아닐 때 꼬리를 어떻게 처리하나? 각 방식의 위험은?</summary>

1. **스칼라 꼬리**: 가장 단순하다. 최대 15 바퀴가 추가된다. 작은 n 이 많으면 느리다.
2. **겹치는 마지막 청크** (`p + n − 16`): n ≥ 16 일 때만 쓸 수 있다. 겹치는 부분이 "이미 확인됨" 이어야 한다. memchr(첫 일치)와 minmax(멱등)에는 맞다. 그러나 **합(checksum)에는 틀린다**. 겹친 바이트가 두 번 더해지기 때문이다. 그래서 `checksum_u8` 의 꼬리는 스칼라다.
3. **정렬 내림 + 마스크**: 정렬된 16 B 읽기는 페이지(4/16 KB, 16 의 배수) 경계를 넘을 수 없다. 그래서 fault 는 나지 않는다. 대신 p 앞과 p+n 뒤 바이트를 마스크로 지워야 한다. strlen 처럼 n 을 모르면 이 방식밖에 없다 (연습 2).

이 레슨의 가드 페이지 테스트가 셋 다 검증하는 방식이다. 버그 3(그냥 16 B)은 실측 SIGBUS 로 죽었다.

</details>

<details>
<summary>Q3. libc memchr 는 p+n 뒤까지 읽는다. 버그 아닌가? 펌웨어에서도 괜찮나?</summary>

C 프로그램이 객체 밖을 읽으면 UB 다. 하지만 libc 는 구현 쪽이라 **하드웨어 규칙**만 지키면 된다. 정렬된 16 B 는 페이지를 넘지 않고, 읽은 값 중 n 밖의 일치는 `cmp x3, x2; csel ... hs` 로 버린다. 그래서 결과는 정확하다.

부작용이 세 가지 있다.

- ASan/Valgrind 는 이런 함수를 인터셉트해서 따로 처리해야 한다.
- MTE(메모리 태깅)에서는 태그 단위가 16 B 라서 정렬 16 B 읽기는 태그 경계를 안 넘는다 (확인 권장).
- **펌웨어에서는 위험할 수 있다.** 버퍼 바로 뒤가 읽기에 부작용이 있는 MMIO 레지스터(예: UART DR — 읽으면 FIFO 가 빠진다)거나, MPU 영역 경계가 32 B 단위라면 이야기가 달라진다. 그래서 Device 메모리나 DMA 버퍼 끝에서는 ① 방식이 안전하다.

</details>

<details>
<summary>Q4. 바이트 합을 SIMD 로 할 때 어디서 넘치나? 왜 clang 자동 벡터화는 느린가?</summary>

u8 레인에 그대로 더하면 한 스텝 만에 넘친다. `UADALP .8h` 는 레인당 스텝마다 최대 510 이다. 그래서 65535/510 → **128 스텝 = 2048 B** 마다 비워야 한다. `UADDLV` 로 u32 합을 꺼내 u64 에 더하면 사실상 넘치지 않는다. 실측으로 129 스텝 버그는 0xFF × 2064 B 에서 2032 를 냈다.

clang 은 이 지평 논리를 증명하지 못한다. 그래서 C 의 u64 의미를 지키려고 바이트를 바로 u64 레인으로 넓힌다 (TBL + UADDW/UADDW2, 누산기 16개, d8–d11 저장까지). 실측 10~15 GB/s 로, 손 asm 보다 대체로 2~3배 느렸다 (3회 중 1회는 1.2배, 부하 소음). 대안은 C 에서 `uint16_t` 레인 누산을 직접 쓰는 것이다 (intrinsic `vpadalq_u8`). 블록 길이를 코드에 드러내야 컴파일러가 따라온다.

</details>

<details>
<summary>Q5. signed min/max 가 ADC 테스트를 통과했다. 무엇이 문제고 테스트를 어떻게 설계하나?</summary>

12비트 ADC 값(0..4095)은 int16 으로 봐도 양수라서 `smin` 과 `umin` 의 결과가 같다. 하지만 드라이버가 오류 표시로 `0xFFFF` 를 쓰거나, 16비트 ADC·차동 입력 오프셋 같은 이유로 0x8000 이상 값이 들어오면 signed 판은 그 값을 음수로 본다. 실측으로 `mn=65535, mx=2600` 이 나와서 고장 감지(`mx > 4095`)를 놓쳤다.

테스트 원칙은 이렇다.

- **타입의 전체 범위**로 퍼즈한다. 데이터의 "현실적" 범위만 쓰면 안 된다.
- 센티널 값(0x0000/0xFFFF/0x8000/0x7FFF)을 명시적으로 넣는다.
- "약한 테스트는 통과한다" 를 하네스에 단언으로 남긴다. 그래야 누가 나중에 테스트를 줄였을 때 드러난다.

조건 코드 lt/gt(signed) 대 lo/hi(unsigned)는 ARM-B05 와 같은 함정이다.

</details>

<details>
<summary>Q6. 바이트 히스토그램은 왜 SIMD 로 잘 안 빨라지나?</summary>

히스토그램은 `hist[p[i]]++` 다. 레인마다 **다른 주소에 읽고-더하고-쓰기**(scatter) 를 해야 한다. 같은 벡터 안에 같은 값이 둘 이상 있으면 충돌도 처리해야 한다. NEON 에는 gather/scatter 가 없다. 그래서 흔한 해법은 이렇다.

1. **스칼라 + 서브 히스토그램 4개** (연습 4). 연속된 같은 바이트가 같은 카운터를 연달아 갱신하면 메모리를 거친 store→load 의존이 생긴다. 카운터를 넷으로 나누면 이 의존이 끊긴다.
2. 빈(bin) 수가 적으면 **빈마다 `CMEQ` + 누적**(연습 1 `count_byte`). 256 빈이면 256 패스라 손해다.
3. SVE2 에는 `HISTCNT`/`HISTSEG` 명령이 있다 (확인 권장).

펌웨어에서는 ADC 코드 분포 확인(DNL/INL 테스트)이나 NAND 읽기 값 분포(SSD 의 read-retry 전압 조정)에 히스토그램을 쓴다.

</details>

<details>
<summary>Q7. 이 체크섬으로 충분한가? UART/NAND 에서는 무엇을 쓰나?</summary>

바이트 합은 **순서 바뀜을 못 잡는다** (`ab` 와 `ba` 의 합이 같다). 같은 비트 위치에서 서로 상쇄되는 오류도 놓친다. 그래서 실제로는 이렇게 쓴다.

- HDLC/Modbus 같은 링크: CRC-16
- Ethernet/zlib: CRC-32
- IP/UDP: 16비트 1의 보수 합 (RFC 1071). 이건 이 레슨처럼 SIMD 로 넓혀 더한 뒤 캐리를 접어 넣는(end-around carry) 방식으로 계산한다.
- NAND: 섹터마다 BCH/LDPC ECC

AArch64 에는 `CRC32B/H/W/X`, `CRC32CB…` 명령이 있다 (FEAT_CRC32, ARMv8.1 부터 필수 — 확인 권장). STM32 같은 MCU 에는 CRC 주변장치가 있다. 바이트 합은 "부트로더가 이미지 끝의 8비트 합 바이트를 확인" 정도의 가벼운 용도에 남아 있다.

</details>

<details>
<summary>Q8. Cortex-M 에는 NEON 이 없다. 같은 일을 MCU 에서 하면?</summary>

- **Cortex-M4/M7/M33 의 DSP 확장**은 32비트 레지스터 안에서 SIMD 를 한다 (SIMD within a register). 바이트 합은 `USADA8 rd, rn, rzero, racc`(0 과의 절대차 합 = 4 바이트 합)로 한 명령에 4 B 씩 된다. 바이트 비교는 `UADD8`/`USUB8` 이 세우는 GE 플래그 + `SEL` 이나, B10 연습 1의 haszero 트릭(`(v − 0x01010101) & ~v & 0x80808080`)으로 한다.
- **Cortex-M55/M85 의 Helium(MVE, Armv8.1-M)** 은 128비트 벡터를 쓴다. `VCMP`, `VADDV`, `VMINV/VMAXV` 처럼 이 레슨과 거의 같은 패턴을 쓸 수 있다. 꼬리는 tail predication(`DLSTP/LETP`)으로 처리한다 (명령 이름은 확인 권장).
- 실무에서는 UART 구분자 찾기를 CPU 가 하지 않는 경우도 많다. **UART 의 character-match 인터럽트**(STM32 의 CMF/ADD 필드) + DMA 로 하드웨어가 0x7E 를 기다리게 한다.

</details>

## 9. 추가 연습

### 연습 1 — `count_byte(p, c, n)`: 히스토그램 한 칸

`size_t count_byte(const void *p, int c, size_t n)` — c 와 같은 바이트 개수를 센다. u8 레인 카운터를 쓰고, 그 지평을 지켜라. 0xA5 × 4080 B / 4096 B / 1 MB 테스트를 통과해야 한다.

<details>
<summary>답</summary>

`cmeq` 결과 0xFF 는 2의 보수로 −1 이다. 그래서 `sub v2.16b, v2.16b, v1.16b` 가 일치 레인마다 **+1** 이 된다. 덧셈 없이 뺄셈 한 번으로 센다.

u8 레인은 스텝당 최대 +1 이므로 255 스텝(4080 B)까지 안전하다. 256 번째에 0 으로 감긴다. 그래서 255 스텝마다 `uaddlv h3, v2.16b`(최대 16 × 255 = 4080, u16)로 비워 x5 에 더한다. 꼬리는 `cmp` + `cinc x5, x5, eq` 다. 테스트는 4080 B(경계)와 4096 B(경계 + 1 스텝)를 둘 다 단언한다. 255 를 256 으로 바꾸면 4096 B 테스트가 실패한다.

</details>

### 연습 2 — `strlen_neon(s)`: 정렬 내림 + 마스크 (n 이 없을 때)

<details>
<summary>답</summary>

길이를 모르니 "겹치는 마지막 청크" 를 쓸 수 없다. 정렬된 읽기만 써야 한다. 순서는 이렇다.

1. `and x1, x0, #~15` 로 첫 청크를 정렬 내림한다.
2. `cmeq v0.16b, v0.16b, #0`(0 과 비교 형식) → `shrn` → `fmov x2, d0`.
3. **s 앞의 k = s & 15 바이트는 니블 마스크의 하위 4k 비트**다. `lsr x2, x2, x3`(x3 = 4k)로 밀어내면, 니블 j 가 곧 s+j 다.
4. 0 이 아니면 `rbit/clz/lsr #2` 가 바로 길이다.
5. 아니면 `ldr q0, [x1, #16]!` 로 정렬 청크를 돌고, 찾으면 `(청크 + idx) − s` 를 돌려준다.

테스트에서는 s 앞 바이트를 일부러 0 으로 채운다. 마스크를 빠뜨리면 바로 0 이 나온다. 또 NUL 이 페이지 마지막 바이트인 경우와 s 가 페이지 첫 바이트(앞 페이지 PROT_NONE)인 경우를 둘 다 돌린다. B10 의 `strlen_word`(8 B, haszero 트릭)를 16 B 로 키운 판이다.

</details>

### 연습 3 — `memchr2_neon(p, c1, c2, n)`: `\r` 또는 `\n`

<details>
<summary>답</summary>

`dup` 두 개, `cmeq` 두 번, `orr v3.16b, v3.16b, v4.16b` 다. 나머지(SHRN 마스크, 겹침 꼬리)는 `neon_memchr` 와 같다. 스칼라 경로는 `cmp w4, w1` → `ccmp w4, w2, #4, ne` → `b.eq` 다. 첫 비교가 같으면(eq) `ccmp` 가 비교하지 않고 NZCV 를 즉시값 `#4`(Z=1)로 둔다. 그래서 둘 중 하나라도 같으면 `b.eq` 가 뛴다 (ARM-B05 의 조건 비교). AT 명령 응답 `"+CSQ: 23,99\r\nOK\r\n"` 에서 첫 줄 끝 = 인덱스 11 이다. 바이트 집합이 커지면(예: 공백류 6종) `TBL` 로 니블 룩업을 두 번 하는 "shufti" 기법이 있다 (simdjson 등, 확인 권장).

</details>

### 연습 4 — `hist_u8_4way` (C): 서브 히스토그램 4개

<details>
<summary>답</summary>

`h[4][256]` 에 `i, i+1, i+2, i+3` 을 나눠 세고 마지막에 더한다. 1,000,003 바이트(꼬리 3)로 검증한다. 단순 히스토그램과 `memcmp` 로 같은지, 빈 합 == n 인지, 그리고 `h[0x7E] == count_byte(m, 0x7E, n)`(연습 1과 교차 검증)을 확인한다. 속도 차이는 데이터가 균일 랜덤일 때보다 **같은 값이 길게 이어질 때**(예: ADC 가 거의 일정할 때) 커진다. 측정은 각자 `demo_bench.c` 에 넣어 보자.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-M07_practice && /tmp/ARM-M07_practice
# 10/10 passed  (ASan/UBSan 빌드도 10/10, Lima VM gcc 15.2 빌드도 10/10)
```

## 10. 핵심 정리

- 바이트 검색 = `dup` → `cmeq` (0x00/0xFF) → `shrn .8b, .8h, #4` (64비트 니블 마스크) → `fmov` → `rbit + clz` → `lsr #2`. NEON 에 PMOVMSKB 는 없다. 대안은 UMAXV 테스트, UMINV + 인덱스 벡터(Apple libc), 가중치 + ADDP.
- 꼬리: 스칼라 / **겹치는 마지막 16 B**(검색·minmax 는 OK, 합은 이중 계산이라 안 됨) / **정렬 내림 + 마스크**(정렬 16 B 읽기는 페이지를 못 넘는다. n 모르는 strlen 에 필수).
- 누산 지평: u8 += 바이트 → 1 스텝, u8 += 1 → 255 스텝, `uadalp .8h` → **128 스텝 = 2048 B**, u32 총합 → 16 MiB, u64 → 무한. 경계 앞과 뒤를 **둘 다** 테스트한다.
- min/max 는 항등원(0xFFFF / 0)에서 시작한다. **unsigned**(`umin`, `lo/hi`, `ldrh`)를 쓴다. 가로 리덕션(`uminv`)은 루프 밖에서 한 번만 한다.
- 테스트 설계: 길이 × 정렬 × 일치 위치 + "범위 뒤에 c 채우기" + `PROT_NONE` 가드 페이지(맥은 SIGBUS) + 버그 판을 하네스가 잡는지 단언. ASan 은 `.S` 를 못 본다.
- M6 실측 (부하 33, 소음 큼): 스칼라 ≈ 3 GB/s, NEON memchr ≈ 45–55 GB/s (libc 와 비슷), NEON 합 ≈ 자동 벡터화 × 1.2–2.9 (컴파일러는 u16 지평을 몰라서 u64 로 넓힌다).
- 히스토그램은 scatter 라 SIMD 이득이 작다. 서브 히스토그램 4개, 또는 빈마다 CMEQ 카운트.

## 참고자료

- Arm A64 Instruction Set Architecture (명령별 페이지: CMEQ, SHRN, UADALP, UADDLV, UMINV/UMAXV, RBIT, CLZ, CCMP): https://developer.arm.com/documentation/ddi0602/latest
- Arm Architecture Reference Manual for A-profile (DDI 0487) — SIMD 레지스터 쓰기 시 상위 비트 0, 정렬·Device 메모리 규칙: https://developer.arm.com/documentation/ddi0487/latest
- Arm NEON Intrinsics Reference (`vceqq_u8`, `vshrn_n_u16`, `vpadalq_u8`, `vaddlvq_u16`, `vminvq_u16`): https://developer.arm.com/architectures/instruction-sets/intrinsics/
- Arm Community blog — "Porting x86 vector bitmask optimizations to Arm NEON" (SHRN 니블 마스크 기법): https://community.arm.com/arm-community-blogs/b/infrastructure-solutions-blog/posts/porting-x86-vector-bitmask-optimizations-to-arm-neon
- Arm Optimized Routines (AArch64 memchr/strlen/strchr 레퍼런스 구현): https://github.com/ARM-software/optimized-routines
- AAPCS64 — v8–v15 하위 64비트 callee-saved: https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst
- ISO C11 (N1570) 7.24.5.1 memchr — "converted to an unsigned char": https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
- RFC 1071 — Computing the Internet Checksum (넓혀 더하기 + end-around carry): https://www.rfc-editor.org/rfc/rfc1071
- ARMv7-M Architecture Reference Manual (DDI 0403) — USADA8, UADD8/SEL (DSP 확장): https://developer.arm.com/documentation/ddi0403/latest
- 이 디렉터리의 실측 파일: `bench_m6.txt` (벤치 3회 원본 + uptime), `libc_memchr_m6.txt` (Apple `_platform_memchr` 디스어셈블 + 마스크 테이블), `trace_m6.txt` (neon_memchr lldb 단계 추적), `demo_bugs_m6.txt` (버그 6종 실측)
- 관련 레슨: ARM-B05 (조건 코드 lo/hi vs lt/gt, CCMP), ARM-B09 (lldb), ARM-B10 (스칼라 strlen/memcpy, 가드 페이지, haszero), ARM-M01 (AAPCS64 callee-saved 검사기), ARM-M03 (카운터 벤치 하네스), ARM-M06 (NEON 기초: 레지스터 뷰·내적), ARM-A07 (포화·고정소수점 SIMD), ARM-A11 (자동 벡터화 출력)
