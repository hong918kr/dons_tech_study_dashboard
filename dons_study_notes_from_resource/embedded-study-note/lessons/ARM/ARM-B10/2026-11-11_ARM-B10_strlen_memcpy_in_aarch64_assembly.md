---
id: ARM-B10
time: 65분
updated: 2026-11-11
---
# ARM-B10 어셈블리로 strlen·memcpy 구현

## 학습 목표

- 바이트 루프 `memcpy_byte` · `strlen_byte` 를 AArch64 어셈블리로 직접 쓰고, 명령 하나하나가 레지스터를 어떻게 바꾸는지 추적표로 설명한다.
- `memcpy_16` 을 **머리(head) → 몸통(body) → 꼬리(tail)** 3단으로 쓴다. 머리는 dst 를 16바이트 경계에 맞추고, 몸통은 `LDP/STP` 로 16 B 씩 옮기고, 꼬리는 `TBZ` 로 n 의 비트를 보고 8/4/2/1 B 를 옮긴다.
- 길이 0..300 × src 정렬 0..15 × dst 정렬 0..15 (77,056 호출) **퍼즈 테스트**를 만든다. dst 앞뒤 가드 바이트(canary)와 **페이지 끝 테스트**(다음 페이지 `PROT_NONE`)로 "한 바이트 더 쓰기/읽기" 를 잡는다.
- libc 와 실측 비교: 바이트 루프 ~4 GB/s, 16 B 루프 ~60 GB/s, libc ~85–110 GB/s (M6, 소음 있음). 그 차이를 Apple libc 의 **실제 디스어셈블리**로 설명한다.
- 겹치는 영역(overlap)이 왜 memcpy 계약 밖인지, 컴파일러가 C 루프를 `memcpy` 호출로 바꾸는 경우가 언제인지 말한다.

## 1. 문제

"memcpy 를 짜 보세요. 더 빠르게 하려면요?" 는 펌웨어 면접의 고전이다. 같은 질문이 "정렬 안 된 포인터면요?", "겹치면요?", "끝에서 한 바이트 더 읽으면요?" 로 이어진다. SSD 컨트롤러에서는 NAND 페이지 버퍼와 호스트 버퍼 사이의 복사가 이것이다. 부트 코드의 `.data` 복사(BM-B08)도 이것이다. 크래시 덤프 맨 위 프레임에서 가장 자주 보는 함수도 `memcpy` 다.

ARM-B04 에서 주소 지정 모드(`[x1], #1` post-index, `LDP/STP`)를 배웠다. ARM-B05 에서는 `CBZ/TBZ`·조건 분기를 배웠다. 이번에는 그것들을 조립해 **진짜 라이브러리 함수**를 만든다. 범위는 일반 정수 레지스터까지다. SIMD(q 레지스터, `UMINV`)로 하는 빠른 판은 ARM-M07 이 맡는다. 성능 측정 도구 자체(시스템 카운터)는 ARM-M03 이 맡는다.

```svg
<svg viewBox="0 0 740 306" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">memcpy_16(dst+13, src+1, 40) — 머리 3 B · 몸통 2×16 B · 꼬리 5 B (lldb 실측과 같은 예)</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="72">dst</text><text x="10" y="152">src</text>
  </g>
  <rect x="40" y="54" width="70" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="110" y="54" width="42" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="152" y="54" width="224" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="376" y="54" width="224" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="600" y="54" width="56" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="656" y="54" width="14" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="670" y="54" width="42" height="28" class="f-mut" stroke="currentColor"/>
  <g stroke="currentColor" stroke-width="2.5">
    <line x1="152" y1="44" x2="152" y2="92"/><line x1="376" y1="44" x2="376" y2="92"/><line x1="600" y1="44" x2="600" y2="92"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="75" y="73">canary</text><text x="131" y="73">3</text>
    <text x="264" y="73">ldp/stp #1</text><text x="488" y="73">ldp/stp #2</text>
    <text x="628" y="73">4</text><text x="663" y="73">1</text><text x="691" y="73">canary</text>
    <text x="110" y="106">+13</text><text x="152" y="106">+16</text><text x="376" y="106">+32</text>
    <text x="600" y="106">+48</text><text x="670" y="106">+53</text>
  </g>
  <rect x="110" y="134" width="42" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="152" y="134" width="224" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="376" y="134" width="224" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="600" y="134" width="70" height="28" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="110" y="186">+1</text><text x="152" y="186">+4</text><text x="376" y="186">+20</text>
    <text x="600" y="186">+36</text><text x="670" y="186">+41</text>
    <text x="376" y="153">src 는 비정렬 (src+4, +20) — 일반 메모리면 LDP 허용</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="214">굵은 세로선 = dst 의 16 B 경계. 머리 k = (−dst) &amp; 15 = 3 바이트를 ldrb/strb 로.</text>
    <text x="10" y="234">→ 이후 모든 stp 는 정렬된 주소. x2: 40 → 37 (머리 뒤) → 블록 수 37 &gt;&gt; 4 = 2.</text>
    <text x="10" y="254">꼬리 37 &amp; 15 = 5 = 0b0101 → tbz 로 비트 2 (4 B) 와 비트 0 (1 B) 만 복사.</text>
    <text x="10" y="274">회색 canary = 테스트가 dst 앞뒤에 깔아 둔 0xA5. 한 바이트라도 바뀌면 FAIL.</text>
    <text x="10" y="294">src 와 dst 의 어긋남이 다르면 둘 다 맞출 수 없다 → dst 하나만 맞춘다.</text>
  </g>
</svg>
```

### Part A — 바이트 루프 두 개

- `void *memcpy_byte(void *dst, const void *src, size_t n)` — 한 바이트씩 `ldrb/strb` post-index 로 복사하고, **원래 dst 를 돌려준다**. n = 0 이면 아무것도 건드리지 않는다.
- `size_t strlen_byte(const char *s)` — NUL 까지 한 바이트씩 읽는다. NUL 은 세지 않는다. 0x80..0xFF 바이트도 문자다.
- 단위 테스트: `"hello"`, n = 0, 0x00..0xFF 전부, `""`, `"a"`, 고위 바이트 문자열, 255자.

### Part B — 정렬 + 16바이트 LDP/STP, 그리고 퍼즈

- `void *memcpy_16(void *dst, const void *src, size_t n)` — 위 그림의 3단 구조. 스택을 쓰지 않는 leaf 함수이고, x3..x6 만 쓴다.
- 퍼즈: 두 함수 각각 길이 0..300 × src 정렬 0..15 × dst 정렬 0..15 = **77,056 호출**. dst 앞뒤 32 B canary 를 포함한 버퍼 **전체**를 C 기준 모델 결과와 `memcmp` 한다. 반환값도 확인한다.
- strlen 퍼즈: 길이 0..300 × 정렬 0..15. NUL 뒤 바이트는 전부 0 이 아니게 둔다. 그래야 NUL 을 지나쳐 읽으면 값이 틀린다.
- 페이지 끝: `mmap` 2 페이지 중 두 번째를 `PROT_NONE` 으로 막는다. src 와 dst 가 **페이지 마지막 바이트에서 끝나게** 놓고 n = 0..300 을 돌린다. strlen 은 NUL 을 페이지 마지막 바이트에 둔다. 범위 밖을 한 바이트라도 건드리면 그 자리에서 죽는다.
- 큰 크기: 0..64 KB 랜덤 길이 · 랜덤 오프셋 200회.
- 심어 둔 버그 6종(`demo_bugs.S`)이 이 테스트에 **실제로 걸리는지** 확인한다.

### Part C — 펌웨어 버퍼 경로 + 벤치마크

- 하네스에 있는 펌웨어식 코드가 내 asm 을 쓴다:
  - `ring_read()` — UART/NIC DMA 수신 링(256 B)에서 tail 부터 len 바이트를 꺼낸다. 끝에서 감기면 `memcpy_16` 두 번이다. tail 0..255 × len 0..256 을 전수 검사한다.
  - `log_append()` — 부트 로그에 `strlen_byte` + `memcpy_16` 으로 덧붙인다. 넘치면 자르고 항상 NUL 로 끝낸다.
  - 4 KB NAND 페이지 → 4 B 어긋난 호스트 버퍼 복사.
- `demo_bench.c`: 16 B / 256 B / 4 KB / 1 MB 에서 byte vs 16 B vs libc `memcpy`, byte vs libc `strlen`. 결과는 중앙값 표로 만든다. libc 가 이기는 이유는 lldb 로 libc 를 디스어셈블해서 확인한다.

## 2. Problem (English)

> **Implement memcpy and strlen in AArch64 assembly.**
>
> Write, as AAPCS64 leaf functions in a `.S` file (no stack use, only caller-saved scratch registers):
>
> 1. `void *memcpy_byte(void *dst, const void *src, size_t n)` — byte loop; returns `dst`.
> 2. `size_t strlen_byte(const char *s)` — byte loop.
> 3. `void *memcpy_16(void *dst, const void *src, size_t n)` — copy bytes until `dst` is 16-byte aligned, then 16 bytes per iteration with `LDP/STP` of two X registers, then the `n % 16` tail. `src` may have any alignment.
>
> Constraints: never read outside `[src, src+n)` or write outside `[dst, dst+n)` — not even one byte, not even "harmlessly" within the same cache line. `n == 0` is valid. Overlapping buffers are out of contract (that is `memmove`).
>
> Test: all `n` in 0..300 × src alignment 0..15 × dst alignment 0..15 with guard bytes around `dst`, plus buffers that end exactly at an inaccessible page. Then benchmark against libc for 16 B, 256 B, 4 KB and 1 MB, and explain the gap.
>
> Example: `memcpy_16(dst+13, src+1, 40)` → 3 head bytes, 2 LDP/STP blocks, 5 tail bytes (4 + 1); returns `dst+13`. `strlen_byte("\x80\xff") == 2`.
>
> Follow-ups: Why align `dst` and not `src`? What happens on a Cortex-M0 or with the MMU off? Why may libc's `strlen` read past the NUL while yours may not? Why can't `memcpy` handle overlap? When does the compiler turn your C loop into a call to `memcpy`?

## 3. 시작 코드

테스트 하네스 (starter 와 solution 이 같이 쓴다):

<!-- code: harness.c -->

어셈블리 시작 코드 — 세 함수가 모두 "틀렸지만 안전한" 스텁이다. 복사하지 않고 dst 만 돌려주거나 0 을 돌려준다:

<!-- code: starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S -o /tmp/ARM-B10_starter && /tmp/ARM-B10_starter
```

처음 실행 결과 (실제 출력, 0.13 s):

```text
FAIL harness.c:123  r == d && memcmp(d, "helloxx", 8) == 0
PASS memcpy_byte(z, "AB", 0) == z && z[0] == 1 && z[1] == 2
FAIL harness.c:130  memcmp(out, all, 256) == 0
PASS strlen_byte("") == 0
FAIL harness.c:134  strlen_byte("a") == 1
...
  first mismatch: len=1 src_align=0 dst_align=0
FAIL harness.c:147  bad == 0
PASS bad_ret == 0
  first mismatch: len=1 align=0 got=0
FAIL harness.c:149  fuzz_strlen() == 0
FAIL harness.c:160  bad_pe == 0
...
FAIL harness.c:212  pos == 15 && strcmp(log, "BOOT:nand ok ft") == 0
3/18 passed
```

`.S`(대문자)는 C 전처리기를 먼저 거친다. 그래서 `#ifdef __APPLE__` / `SYM()` 매크로로 **같은 파일**이 맥(Mach-O, `_memcpy_16`)과 리눅스(ELF, `memcpy_16` + `.type/.size`) 양쪽에서 어셈블된다. 주석은 `//` 와 `/* */` 만 쓴다 (`;` 의 함정은 7. 자주 하는 실수).

## 4. 힌트

<details>
<summary>힌트 1 — x0 은 건드리지 말고 커서를 따로</summary>

memcpy 는 **원래 dst 를 돌려준다**. 그런데 반환 레지스터가 x0 이다. 그래서 첫 줄은 `mov x3, x0`(커서 복사)이고, 이후에는 x3 만 움직인다. x0..x17 은 호출자 저장(caller-saved) 레지스터라 leaf 함수가 마음대로 써도 된다. strlen 도 같다: `mov x1, x0` 로 시작점을 남겨 두면 끝에 `x1 - x0` 으로 길이가 나온다.

</details>

<details>
<summary>힌트 2 — 루프 카운터는 subs + b.ne, 그 전에 n == 0 확인</summary>

`subs x2, x2, #1` 은 빼면서 Z 플래그를 세운다. 그래서 `b.ne 1b` 하나로 "아직 남았나" 를 판단한다 (ARM-B05). 단, 이건 do-while 이다. n = 0 으로 들어오면 첫 `subs` 가 0 − 1 = 2⁶⁴ − 1 을 만든다. 그 뒤로 끝없이 쓴다. 루프 앞에 `cbz x2, done`.

</details>

<details>
<summary>힌트 3 — 머리 바이트 수 = (−dst) & 15</summary>

dst 를 16의 배수로 올리려면 몇 바이트가 필요할까? `(16 − dst % 16) % 16` 이고, 2의 보수로는 `(−dst) & 15` 다. `neg x4, x3` + `and x4, x4, #15` 두 명령이면 된다. 이 경로는 n ≥ 16 일 때만 타므로 `n − k` 가 음수가 될 수 없다.

</details>

<details>
<summary>힌트 4 — 꼬리 0..15 바이트는 루프 없이 TBZ 네 번</summary>

꼬리 t (0..15) 는 이진수로 `8·b3 + 4·b2 + 2·b1 + 1·b0` 이다. `tbz x2, #3, skip` → `ldr x5`/`str x5` (8 B). 같은 방식으로 비트 2 는 `ldr w5` (4 B), 비트 1 은 `ldrh` (2 B), 비트 0 은 `ldrb` (1 B). 순서대로 하면 포인터가 정확히 t 만큼 전진한다. n < 16 인 경우도 이 꼬리 코드로 바로 보내면 끝이다.

</details>

## 5. 풀이

### Part A — `memcpy_byte` 한 줄씩

| 명령 | 하는 일 | 왜 이 명령 |
|---|---|---|
| `mov x3, x0` | 커서 = dst | x0 은 반환값으로 보존 |
| `cbz x2, 2f` | n == 0 → ret | 아래 루프가 do-while 이라 필수 |
| `1: ldrb w4, [x1], #1` | w4 = *src, src += 1 | post-index: 접근 **후** 베이스 갱신 (ARM-B04). `ldrb` 는 상위 비트를 0 으로 채운다 |
| `strb w4, [x3], #1` | *dst = w4, dst += 1 | w 레지스터의 하위 8비트만 쓴다 |
| `subs x2, x2, #1` | n −= 1, 플래그 갱신 | 별도 `cmp` 없이 Z 로 종료 판단 |
| `b.ne 1b` | n ≠ 0 이면 반복 | `1b` = 뒤쪽(backward) 가장 가까운 `1:` |
| `2: ret` | `br x30` 과 같은 효과 + "함수 복귀" 힌트 | x0 = 원래 dst |

루프 한 바퀴 = 명령 4개 = 1 바이트. 아래 벤치마크에서 이 루프는 크기와 상관없이 **~4.0 GB/s** 로 일정하다. 대략 0.25 ns 에 1 바이트다. M6 P 코어 클럭이 4 GHz 대라면 "사이클당 한 바퀴" 쯤이다 (클럭은 미확인). 매 바퀴에 **조건 분기 하나**와 store 하나가 있어서, 코어가 아무리 넓어도 바퀴 수가 병목이 된다 (추정, 마이크로아키텍처 문서로 확인 권장).

### Part A — `strlen_byte`: 커서가 NUL 을 지나서 멈춘다

```text
mov  x1, x0          // 커서
1: ldrb w2, [x1], #1  // 읽고 전진 — NUL 을 읽은 뒤에도 전진한다
   cbnz w2, 1b
sub  x0, x1, x0      // (NUL 다음 주소) − 시작
sub  x0, x0, #1      //  − 1  =  길이
```

`"hi"` 를 시작 주소 s 로 손 추적하면:

| # | 명령 | w2 | x1 | 분기 |
|---|---|---|---|---|
| 1 | `ldrb w2, [x1], #1` | 'h' (0x68) | s+1 | `cbnz` 다시 |
| 2 | `ldrb w2, [x1], #1` | 'i' (0x69) | s+2 | 다시 |
| 3 | `ldrb w2, [x1], #1` | 0 | s+3 | 빠져나감 |
| 4 | `sub x0, x1, x0` | | | x0 = 3 |
| 5 | `sub x0, x0, #1` | | | **x0 = 2** |

post-index 라서 NUL 을 읽은 순간에도 x1 이 한 칸 더 간다. 그래서 `− 1` 이 필요하다. 빼먹으면 `strlen("") = 1` 이 된다 (demo_bugs 의 버그 6, 실제 출력 `strlen("") = 1, strlen("abc") = 4`). 이 문제를 lldb 로 한 단계씩 잡는 연습은 ARM-B09 에 있다.

`ldrb` 대신 `ldrsb` 를 써도 strlen 은 맞는다. 0x80 이 0xFFFFFF80 이 돼도 여전히 0 이 아니기 때문이다. 부호 확장 함정이 실제로 터지는 곳은 바이트끼리 **크기를 비교하는** strcmp 류다 (ARM-B09 연습). 그래도 C 의 `unsigned char` 의미와 맞는 `ldrb` 가 기본이다.

### Part B — `memcpy_16`: lldb 로 본 실제 추적

`demo_trace.c` 로 `memcpy_16(dst+13, src+1, 40)` 을 한 명령씩 실행했다 (`lldb -b -s demo_trace.lldb`, `si` 40번). 값은 **각 명령 실행 후** 의 레지스터다. 원본 주소는 src = 0x100008000, dst = 0x100008040 이고, 표에서는 기준 대비 오프셋으로 바꿨다. 전체 표는 `trace_m6.txt` 에 있다.

```bash
cc -std=c11 -g -O0 demo_trace.c solution.S -o /tmp/ARM-B10_trace && lldb -b -s demo_trace.lldb /tmp/ARM-B10_trace
```

```text
 # off  insn                          x1        x2    x3        x4
 1 + 0 mov    x3, x0                 src+1     40    dst+13    1        ← x4 는 이전 쓰레기값
 4 +12 neg    x4, x3                 src+1     40    dst+13    0xfffffffeffff7fb3
 5 +16 and    x4, x4, #0xf           src+1     40    dst+13    3        ← k = 3
 6 +20 sub    x2, x2, x4             src+1     37    dst+13    3
 8 +28 ldrb   w5, [x1], #0x1         src+2     37    dst+13    3
 9 +32 strb   w5, [x3], #0x1         src+2     37    dst+14    3
   ... (머리 3바퀴)
19 +40 b.ne   0x100000614            src+4     37    dst+16    0        ← dst 정렬 완료
20 +44 lsr    x4, x2, #4             src+4     37    dst+16    2        ← 블록 2개
21 +48 and    x2, x2, #0xf           src+4     5     dst+16    2        ← 꼬리 5
23 +56 ldp    x5, x6, [x1], #0x10    src+20    5     dst+16    2
24 +60 stp    x5, x6, [x3], #0x10    src+20    5     dst+32    2
27 +56 ldp    x5, x6, [x1], #0x10    src+36    5     dst+32    1
28 +60 stp    x5, x6, [x3], #0x10    src+36    5     dst+48    1
31 +72 tbz    w2, #0x3, 0x10000064c  src+36    5     dst+48    0        ← 비트3 = 0 → 8 B 건너뜀
33 +88 ldr    w5, [x1], #0x4         src+40    5     dst+48    0
34 +92 str    w5, [x3], #0x4         src+40    5     dst+52    0
35 +96 tbz    w2, #0x1, 0x100000664  src+40    5     dst+52    0        ← 비트1 = 0
37 +112 ldrb  w5, [x1]               src+40    5     dst+52    0
38 +116 strb  w5, [x3]               src+40    5     dst+52    0        ← 마지막 바이트 dst+52
39 +120 ret
```

(프로그램은 이어서 `ok=1` 을 출력했다. `memcmp` 로 확인한 값이다.)

읽을 거리 세 가지:

1. **lldb 는 `tbz x2, #3` 을 `tbz w2, #0x3` 로 보여 준다.** TBZ 인코딩의 비트 번호는 b5:b40 6비트다. b5 = 0(비트 0..31)이면 디스어셈블러가 W 이름으로 표기한다. 테스트하는 비트는 같다.
2. 마지막 1 바이트는 post-index 가 없다 (`ldrb w5, [x1]`). 이후 포인터를 쓸 일이 없으니 갱신을 생략했다.
3. 몸통의 `ldp` 는 `src+4`, `src+20` 처럼 **정렬 안 된 주소**에서 읽는다. 맥(EL0, 일반 메모리)에서는 아키텍처상 허용되고, 테스트도 통과한다. 이게 안 되는 환경은 면접 Q3 에서 다룬다.

LDP post-index 의 실제 인코딩 (`objdump -d` 로 본 `a8c11825`):

```svg
<svg viewBox="0 0 740 252" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ldp x5, x6, [x1], #16  =  0xa8c11825  (LDP, 64-bit, post-index)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="44" height="34" class="f-acc"/>
    <rect x="64" y="40" width="66" height="34" class="f-mut"/>
    <rect x="130" y="40" width="22" height="34" class="f-mut"/>
    <rect x="152" y="40" width="66" height="34" class="f-warn"/>
    <rect x="218" y="40" width="22" height="34" class="f-acc"/>
    <rect x="240" y="40" width="154" height="34" class="f-ok"/>
    <rect x="394" y="40" width="110" height="34" class="f-acc"/>
    <rect x="504" y="40" width="110" height="34" class="f-warn"/>
    <rect x="614" y="40" width="110" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="42" y="62">10</text><text x="97" y="62">101</text><text x="141" y="62">0</text>
    <text x="185" y="62">001</text><text x="229" y="62">1</text><text x="317" y="62">0000010</text>
    <text x="449" y="62">00110</text><text x="559" y="62">00001</text><text x="669" y="62">00101</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="42" y="90">31:30</text><text x="97" y="90">29:27</text><text x="141" y="90">26</text>
    <text x="185" y="90">25:23</text><text x="229" y="90">22</text><text x="317" y="90">21:15</text>
    <text x="449" y="90">14:10</text><text x="559" y="90">9:5</text><text x="669" y="90">4:0</text>
    <text x="42" y="110">opc</text><text x="97" y="110">ld/st pair</text><text x="141" y="110">V=0</text>
    <text x="185" y="110">post-idx</text><text x="229" y="110">L=1</text><text x="317" y="110">imm7 = 2</text>
    <text x="449" y="110">Rt2 = x6</text><text x="559" y="110">Rn = x1</text><text x="669" y="110">Rt = x5</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="140">· opc 10 = 64비트 쌍 → 오프셋 = imm7 × 8 = 16 (범위 −512..+504).</text>
    <text x="10" y="160">· V=0 → 정수 레지스터 (V=1 이면 q/d/s 레지스터 쌍).</text>
    <text x="10" y="180">· 25:23 = 001 post / 011 pre / 010 signed offset 인덱스.</text>
    <text x="10" y="200">· L=1 load, L=0 store → stp 는 0xa8811865 (비트 22만 다름).</text>
    <text x="10" y="220">· 한 명령이 16 B 를 옮기고 베이스도 갱신 → 몸통 4명령 = 16 B.</text>
    <text x="10" y="240">· 필드 이름은 Arm ARM (DDI 0487) 기준, 비트값은 objdump 실측.</text>
  </g>
</svg>
```

`objdump -d` 로 본 전체 길이(실측): `memcpy_byte` 7명령 (28 B), `strlen_byte` 6명령 (24 B), `memcpy_16` 31명령 (**124 B**). 리눅스 `readelf -s` 에서도 `memcpy_16` 의 FUNC 크기는 124 다. 맥 objdump 는 첫 함수를 `ltmp0` 라는 섹션 시작 라벨 이름으로 보여 준다 (`nm` 에는 `T _memcpy_byte` 로 정상 출력).

### Part B — 왜 dst 를 맞추나, 왜 하나만 맞추나

- src 와 dst 의 어긋남(addr % 16)이 다르면 **둘을 동시에 맞출 방법이 없다**. 그래서 하나를 고른다.
- dst 를 고르는 흔한 이유: 비정렬 **store** 는 캐시 라인 두 개에 걸칠 수 있고, store 버퍼에서 쪼개지는 비용이 load 쪽보다 크다고 알려져 있다. 일반론이며 코어마다 다르다. Apple libc 도 (아래 디스어셈블리) **dst 를 32 B 경계로** 맞춘다: `add x3, x3, #0x20` / `and x3, x3, #~0x1f`.
- 그런데 이 맥에서는 차이가 안 보였다. `demo_bugs` 의 버그 5는 정렬 계산을 틀려서(`dst & 15`) dst 가 끝까지 정렬되지 않는 판이다. 4 KB(dst+3, src+1) 복사 시간을 정상판과 3회 비교한 실측: 72.8 / 71.1, 68.0 / 71.5, 71.0 / 71.8 ns. **차이가 소음 안이다.** M 시리즈 코어는 비정렬 접근 페널티가 작은 편으로 보인다 (이 측정 한정). Cortex-A53 급 코어나 Device 메모리에서는 얘기가 완전히 달라진다 (Q3).

### Part B — 퍼즈 설계: 무엇을 잡나

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">페이지 끝 테스트 — 다음 페이지가 PROT_NONE 이면 "한 바이트 더" 가 즉시 크래시가 된다</text>
  <rect x="40" y="40" width="460" height="40" class="f-ok" stroke="currentColor"/>
  <rect x="500" y="40" width="220" height="40" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="50" y="65">페이지 1 (읽기/쓰기 가능, 16 KB)</text>
    <text x="510" y="65">페이지 2 = PROT_NONE</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="4 3">
    <line x1="340" y1="36" x2="340" y2="230"/><line x1="420" y1="36" x2="420" y2="230"/><line x1="500" y1="36" x2="500" y2="230"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="340" y="98">16 B 경계</text><text x="420" y="98">16 B 경계</text><text x="500" y="98">페이지 경계</text>
  </g>
  <rect x="380" y="112" width="120" height="22" class="f-acc" stroke="currentColor"/>
  <text x="10" y="128" fill="currentColor" font-size="12">memcpy_16 (정답)</text>
  <text x="510" y="128" fill="currentColor" font-size="12">src+n 에서 정확히 멈춤 → 통과</text>
  <rect x="420" y="146" width="80" height="22" class="f-acc" stroke="currentColor"/>
  <text x="10" y="162" fill="currentColor" font-size="12">libc strlen (16 B 정렬 청크)</text>
  <text x="510" y="162" fill="currentColor" font-size="12">NUL 뒤도 읽지만 경계 안 넘음</text>
  <rect x="460" y="180" width="80" height="22" class="f-warn" stroke="currentColor"/>
  <text x="10" y="196" fill="currentColor" font-size="12">버그 3: 꼬리를 16 B 로 올림</text>
  <text x="550" y="196" fill="currentColor" font-size="12">경계 넘음 → SIGBUS</text>
  <text x="10" y="226" fill="currentColor" font-size="12">16 KB(페이지)는 16의 배수 → 16 B 정렬 청크는 절대 두 페이지에 걸치지 않는다. 정렬 안 된 16 B 읽기는 걸칠 수 있다.</text>
  <text x="10" y="244" fill="currentColor" font-size="12">canary 는 "같은 페이지 안에서 넘친 쓰기" 를, PROT_NONE 은 "페이지를 넘은 읽기/쓰기" 를 잡는다 — 둘 다 필요.</text>
</svg>
```

세 겹으로 잡는다:

| 검사 | 잡는 버그 | 못 잡는 것 |
|---|---|---|
| dst 버퍼 **전체**(canary 포함) vs C 기준 모델 | 덜 복사, 더 복사(넘친 쓰기), 엉뚱한 위치 | src 쪽 과다 읽기 |
| 반환값 == dst | x0 을 커서로 써 버림 | — |
| 페이지 끝 (src·dst 가 페이지 마지막 바이트에서 끝남) | 페이지를 넘는 과다 읽기·쓰기 → 즉시 크래시 | 같은 페이지 안의 과다 읽기 (무해해서 관찰 불가) |

C 기준 모델 `ref_copy` 는 `volatile` 포인터로 쓴다. 그래야 컴파일러가 이 루프를 다시 libc `memcpy` 호출로 바꾸지 못하고, 기준이 내 함수와 독립적으로 남는다 (아래 `demo_idiom`).

**심은 버그가 실제로 걸리는가** — `cc -std=c11 -O2 demo_bugs.c demo_bugs.S solution.S -o /tmp/ARM-B10_bugs && /tmp/ARM-B10_bugs`. 각 버그는 fork 한 자식 프로세스에서 돈다. 실제 출력:

```text
bug 1: byte loop, no n==0 check, n = 0
  calling with n = 0 ...
  -> child killed by signal 10 (Bus error: 10)
bug 2: tail without the 1-byte step
  fuzz: first mismatch len=1 sa=0 da=0 at dst_buf[32] (dst starts at 32, ends at 33)
  fuzz: 38528/77056 content mismatches, 0 wrong return values
  page-end src: survived
  page-end dst: survived
bug 3: tail rounded up to a full 16-byte block
  fuzz: first mismatch len=1 sa=0 da=0 at dst_buf[33] (dst starts at 32, ends at 33)
  fuzz: 72192/77056 content mismatches, 0 wrong return values
  -> child killed by signal 10 (Bus error: 10)
bug 4: x0 used as cursor
  fuzz: 0/77056 content mismatches, 76800 wrong return values
  page-end src: survived
  page-end dst: survived
bug 5: head count dst&15 instead of -dst&15
  fuzz: 0/77056 content mismatches, 0 wrong return values
  page-end src: survived
  page-end dst: survived
bug 6: strlen counts the NUL
  strlen("") = 1, strlen("abc") = 4
```

숫자를 검산해 보면 퍼즈가 왜 **모든 정렬 조합**을 돌아야 하는지 보인다.

- **버그 2 = 38,528.** 홀수 길이만 틀릴 것 같지만 아니다. n ≥ 16 이면 꼬리 = (n − k) & 15 이고, k = (−dst) & 15 는 dst 정렬에 따라 바뀐다. 그래서 **짝수 n 도 dst 정렬 절반에서 틀린다.**
  - n < 16: 홀수 n 8개 × 256 = 2,048
  - n = 16..300: 285개 × (dst 정렬 8개 × src 16개) = 36,480
  - 합 38,528 ✓
  - dst 정렬을 하나만(예: 0) 테스트했다면 짝수 n 은 다 통과했을 것이다.
- **버그 3 = 72,192** = 77,056 − (16의 배수 길이 19개 × 256) ✓. canary 가 한 바이트 넘친 쓰기를 잡았다(`dst_buf[33]`, dst 는 32..33). 이어서 페이지 끝 src 테스트에서 과다 읽기로 죽었다.
- **버그 4 = 76,800** = 77,056 − 256 (n = 0 이면 x0 이 안 움직임) ✓. 내용은 다 맞으니 **반환값 검사가 없으면 못 잡는다.**
- **버그 5 는 테스트를 통과한다.** 정확성은 같고 성능 의도만 깨진 버그다. 테스트가 아니라 리뷰나 벤치마크가 잡을 몫이다.
- 버그 1의 신호: macOS 에서 보호된 페이지 접근은 SIGSEGV 가 아니라 **SIGBUS(10)** 로 오는 경우가 많다 (이 맥 실측. 리눅스는 보통 SIGSEGV).

### Part B — 겹침(overlap)은 계약 밖

C11 7.24.2.1 은 "복사가 겹치는 객체 사이에서 일어나면 동작은 정의되지 않는다" 고 한다. 프로토타입의 `restrict` 도 같은 약속이다. 그래서 구현은 방향을 고를 필요가 없다.

`memcpy_16` 에 dst = src + 1 처럼 겹치게 주면, 앞쪽부터 복사하면서 방금 쓴 바이트를 다시 읽어 같은 값이 퍼진다. 몸통은 16 B 씩 읽고 써서 그 패턴이 또 달라진다. 결과는 구현마다 다르다. 겹침이 필요하면 `memmove` 다. 방향 판단은 `(dst − src) <u n` 이면 뒤에서부터 복사하는 것이다 (추가 연습 2). Apple 의 `_platform_memmove` 도 첫 세 명령이 정확히 이 판단이다: `sub x3, x0, x1` / `cmp x3, x2` / `b.lo` (아래 디스어셈블리). 이 맥에서 C 의 `memcpy` 함수 포인터를 lldb `image lookup` 으로 보면 **`_platform_memmove` 로 해석된다** (`libc_disasm_m6.txt` 끝). 그래도 다른 libc(newlib, glibc)는 아니므로 겹침에 기대면 안 된다.

### Part B — C 기준 모델과 컴파일러 (`demo_idiom.c`)

C 로 쓴 바이트 복사 루프를 컴파일러가 어떻게 내리는지 직접 봤다 (Apple clang 21, Lima VM 의 gcc 15.2):

```bash
clang -O2 -S -fno-asynchronous-unwind-tables [-fno-builtin | -ffreestanding] [-DMY_MEMCPY] demo_idiom.c -o -
limactl shell elinux -- bash -lc 'mkdir -p /tmp/ARM-B10_work && cp /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-B10/demo_idiom.c /tmp/ARM-B10_work/ && cd /tmp/ARM-B10_work && gcc -O2 -S [-ffreestanding] -DMY_MEMCPY demo_idiom.c -o -'
```

| 함수 / 옵션 | 결과 (실제 asm 요약) |
|---|---|
| `copy_loop` (restrict 없음), clang -O2 | `memcpy` 호출 **아님**. 겹침 런타임 검사 `sub x9, x0, x1; cmp x9, #63` 뒤 `ldp q0, q1` 64 B 벡터 루프 + 8 B + 1 B 루프 |
| `copy_loop_restrict`, clang -O2 | `cbz x2, …` 다음 **`b _memcpy`** (꼬리 호출) |
| `copy_loop_restrict`, gcc -O2 | **`bl memcpy`** |
| 같은 함수, clang/gcc `-ffreestanding` 또는 `-fno-builtin` | 루프 유지 (벡터화된 `ldp q`/`ldr q31`) |
| 이름이 `memcpy` 인 함수 안의 같은 루프, clang·gcc -O2 | 자기 자신 호출로 바꾸지 **않았다** (둘 다 루프 유지) |

교훈: 겹침이 없다는 걸 컴파일러가 알면(`restrict`) 루프는 그냥 `memcpy` 가 된다. `-nostdlib` 펌웨어에서 `memcpy` 를 안 넣었다면 링크 에러가 난다. 직접 만든 `memcpy` 가 다른 함수를 거치는 구조라면 무한 재귀가 될 수도 있다. 이번 두 컴파일러는 자기 자신 호출은 피했지만, gcc 문서는 freestanding 에서도 `memcpy/memmove/memset/memcmp` 는 제공돼야 한다고 명시한다. BM 트랙의 `-fno-tree-loop-distribute-patterns` 플래그가 이 변환을 끄는 gcc 옵션이다.

### Part C — 펌웨어 버퍼 경로

- `ring_read` 는 링 끝에서 감기는 복사를 `memcpy_16` 두 번(두 번째는 0 바이트일 수 있다)으로 처리한다. 그래서 **n = 0 계약**이 실제로 쓰인다. tail 256 × len 257 = 65,792 경우를 C 모듈러 루프와 비교해 전부 맞았다.
- `log_append` 는 16 B 버퍼에 `"BOOT:"` + `"nand ok"` + `" ftl ready"` 를 넣는다. 결과는 `"BOOT:nand ok ft"` (15자 + NUL) 다. 자르기 계산이 `strlen_byte` 결과에 달려 있다. starter 의 `strlen_byte` 는 0 을 돌려주므로 이 테스트도 FAIL 이다.
- 4 KB "NAND 페이지" → `host_buf + 4`. 실제 SSD 에서는 큰 이동을 DMA 엔진이 하고, CPU memcpy 는 메타데이터·헤더·작은 조각에 쓴다. DMA 를 쓰면 캐시 정리(clean/invalidate)가 따라온다 (ARCH-M12).

### Part C — 벤치마크 (`demo_bench.c`, M6, 실측)

```bash
cc -std=c11 -O2 demo_bench.c solution.S -o /tmp/ARM-B10_bench && /tmp/ARM-B10_bench
```

측정 방법:

- 한 샘플 안에서 같은 호출을 ~8 MB 분량만큼 반복한다. 타이머 `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` 의 해상도가 약 42 ns 이기 때문이다.
- 샘플 41개의 **중앙값**을 그 실행의 값으로 한다. 프로그램을 **5번 실행**했고, 아래 표는 5개 값의 중앙값과 [최소–최대] 다. 원본은 `bench_m6.txt` 에 있다.
- libc 함수는 `volatile` 함수 포인터로 부른다 (인라인·제거 방지).
- 측정 중 부하 평균은 ~9 였다 (다른 작업이 함께 돎). 소음이 크니 ±20% 는 의미 없다고 보자.

memcpy (src/dst 64 B 정렬), ns/call (GB/s):

| 크기 | memcpy_byte | memcpy_16 | libc memcpy |
|---|---|---|---|
| 16 B | 5.0 [4.9–5.1] (3.2) | 1.5 [1.2–1.9] (10.7) | 1.7 [1.6–1.7] (9.4) |
| 256 B | 68.2 [68.0–69.3] (3.8) | 6.0 [4.9–6.5] (43) | 2.6 [2.3–3.1] (98) |
| 4 KB | 1012 [989–1022] (4.0) | 71.0 [66.1–71.9] (58) | 41.5 [31.8–43.5] (99) |
| 1 MB | 256,391 [252,906–264,682] (4.1) | 17,214 [16,297–19,448] (61) | 12,547 [8,932–14,182] (84) |
| 4 KB, src+1 / dst+3 | — | 71.4 [66.1–78.0] (57) | 37.7 [36.0–91.8] (109) |

strlen, ns/call (GB/s):

| 길이 | strlen_byte | libc strlen |
|---|---|---|
| 16 | 5.3 [5.2–5.5] (3.0) | 1.2 [0.9–1.7] (13) |
| 256 | 68.8 [67.4–69.1] (3.7) | 5.6 [5.2–6.1] (46) |
| 4 KB | 1012 [990–1031] (4.0) | 73.7 [65.6–73.9] (56) |
| 1 MB | 256,262 [250,577–261,929] (4.1) | 14,869 [14,691–16,238] (71) |

읽는 법:

- **바이트 루프는 크기와 상관없이 ~4 GB/s** 다. 데이터가 L1 이든 1 MB 든 같다. 메모리가 아니라 루프 바퀴 수가 병목이라는 뜻이다.
- **16 B 루프는 ~15배** 빠르다 (4 KB 기준 1012 → 71 ns). 바퀴당 바이트가 16배라서다.
- **16 B 에서는 `memcpy_16` 이 libc 와 비슷하거나 약간 빠르다** (1.5 vs 1.7 ns). libc 는 함수 포인터 호출 + 겹침·크기 분기를 거치고, 내 함수는 `tbz` 몇 개로 끝난다. 차이는 소음 수준이다.
- **256 B ~ 4 KB 에서 libc 가 1.7~2.3배.** 아래 디스어셈블리에 이유가 보인다.
- 1 MB 는 L1 을 넘어 L2 에서 도는 크기라(M6 캐시 크기 미확인) 모두 느려지고 차이가 줄었다.

**libc 가 이기는 이유 — 실제 코드** (`lldb` 로 `_platform_memmove` / `_platform_strlen` 디스어셈블, `libc_disasm_m6.txt`):

```text
libsystem_platform.dylib`_platform_memmove:          (실제 출력에서 발췌, ← 설명은 내가 붙임)
    <+0>:   sub    x3, x0, x1                      ← 겹침 검사 (memmove 겸용)
    <+4>:   cmp    x3, x2
    <+8>:   b.lo   0x18c536ea0  ; <+464>           ← dst 가 src 범위 안이면 뒤에서부터
    <+12>:  mov    x3, x0
    <+16>:  cmp    x2, #0x40
    <+20>:  b.lo   0x18c536e7c  ; <+428>           ← 64 B 미만: 8 B 루프 + 바이트 루프
    ...
    <+36>:  cmp    x2, #0x4, lsl #12  ; =0x4000
    <+40>:  b.lo   0x18c536d58  ; <+136>           ← 16 KB 미만은 같은 구조의 stp 판으로
    <+44>:  add    x3, x3, #0x20
    <+48>:  and    x3, x3, #0xffffffffffffffe0    ← dst 를 32 B 경계로 올림
    <+52>:  ldnp   q2, q3, [x1]                    ← 첫 32 B 는 정렬 무시하고 통째로
    ...
    <+76>:  stnp   q2, q3, [x0]                    ←   원래 dst 에 (머리 루프 없음)
    <+80>:  subs   x2, x2, #0x40
    <+84>:  b.ls   0x18c536d40  ; <+112>
    <+88>:  stnp   q0, q1, [x3]                    ← 몸통: 바퀴당 32 B (q 레지스터 2개)
    <+92>:  add    x3, x3, #0x20
    <+96>:  ldnp   q0, q1, [x1]
    <+100>: add    x1, x1, #0x20
    <+104>: subs   x2, x2, #0x20
    <+108>: b.hi   0x18c536d28  ; <+88>
    <+112>: addpt  x1, x1, x2                      ← 꼬리: 끝에서 잰 마지막 32 B 를
    <+116>: ldnp   q2, q3, [x1]                    ←   겹쳐서 한 번 더 복사 (꼬리 루프 없음)
    <+120>: stnp   q0, q1, [x3]
    <+124>: addpt  x3, x3, x2
    <+128>: stnp   q2, q3, [x3, #0x20]
    <+132>: ret
```

| libc 기법 | 효과 | 이 레슨 판 |
|---|---|---|
| q 레지스터 `LDNP/STNP q0, q1` | 명령당 32 B (내 것은 16 B) | x 레지스터 쌍 (SIMD 는 ARM-M07) |
| 머리를 "첫 32 B 통째 복사 + dst 올림" 으로 | 바이트 머리 루프 없음 (겹쳐 쓰기는 무해) | 최대 15바퀴 `ldrb/strb` |
| 꼬리를 "마지막 32 B 를 끝 기준으로 다시 복사" 로 | 꼬리 분기 없음 | `tbz` 4개 |
| ≥ 16 KB 에서 `stnp`(non-temporal 힌트) | 큰 복사가 캐시를 덜 더럽힘 (힌트일 뿐) | 없음 |
| 크기별 분기 (64 B, 16 KB) | 작은 크기는 단순 경로 | n < 16 만 분기 |

"겹쳐서 다시 복사" 트릭이 가능한 건 이 경로가 n ≥ 64 일 때만 오고, 다시 쓰는 범위(처음 32 B, 마지막 32 B)가 전부 `[dst, dst+n)` 안이기 때문이다. 같은 값을 두 번 쓸 뿐이고, 범위 밖은 건드리지 않는다. `addpt`/`subpt` 는 처음 보는 이름일 수 있다. Armv9.x 의 FEAT_CPA(checked pointer arithmetic) 명령으로 보이며, 여기서는 일반 `add`/`sub` 처럼 읽으면 된다 (M6 기능 목록은 미확인).

`_platform_strlen` 은 `and x1, x0, #0xfffffffffffffff0` → `ldr q0, [x1]` 로 **s 를 16 B 내림 정렬한 청크**부터 읽는다. 시작 앞쪽 바이트는 테이블(`adr x3`)로 덮어 가린다. 그다음 `ldr q0, [x1, #0x10]!` / `uminv.16b` / `cbnz` 루프로 16 B 씩 NUL 을 찾는다. NUL 뒤 바이트까지 읽지만 정렬 청크라서 페이지를 넘지 않는다 (위 그림). 우리 페이지 끝 테스트에 libc strlen 을 넣어도 통과할 구조다. 다만 같은 객체 밖을 읽는 건 C 수준에서는 허용되지 않는다. libc 가 이렇게 해도 되는 이유는 Q5 에서 다룬다.

### 같은 .S 를 리눅스(ELF)에서 — Lima VM 실측

```bash
limactl shell elinux -- bash -lc 'mkdir -p /tmp/ARM-B10_work && cp /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-B10/{harness.c,solution.S,practice.c,practice.S} /tmp/ARM-B10_work/ && cd /tmp/ARM-B10_work && gcc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S -o sol && ./sol | tail -1 && readelf -s sol | grep memcpy_16'
```

```text
18/18 passed                                   (gcc 15.2.0, aarch64, PAGESIZE 4096)
    97: 0000000000001e04   124 FUNC    GLOBAL DEFAULT   12 memcpy_16
```

practice 도 VM 에서 9/9 통과했다. 매크로가 바꾸는 건 심볼 이름(`_` 접두사)과 ELF 전용 `.type x, %function` / `.size x, . - x` 뿐이다. `.size` 가 있어야 `readelf`, `perf`, gdb 백트레이스가 함수 크기(124)를 안다. 지역 라벨은 양쪽 다 되는 숫자 라벨(`1:`, `1b`, `2f`)을 썼다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S -o /tmp/ARM-B10_solution && /tmp/ARM-B10_solution
# … 18/18 passed  (0.15 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined harness.c solution.S -o /tmp/ARM-B10_asan && /tmp/ARM-B10_asan
# 18/18 passed, 경고 없음
```

ASan 은 **C 코드의 접근만** 계측한다. `.S` 안의 `ldp/stp` 는 보지 못한다. 그래서 asm 의 과다 읽기·쓰기는 ASan 이 아니라 canary 와 페이지 끝 테스트가 지킨다. 이 레슨에서 테스트를 직접 설계하는 이유가 이것이다.

## 7. 자주 하는 실수

| 실수 | 증상 (실측) | 고친 코드 |
|---|---|---|
| n == 0 확인 없이 `subs`/`b.ne` 루프 | 0 − 1 = 2⁶⁴−1 바퀴 → **SIGBUS** (버그 1) | 루프 앞 `cbz x2, done` |
| 꼬리에서 한 비트 단계 빠뜨림 | 77,056 중 38,528 불일치 — 짝수 n 도 (버그 2) | `tbz` 를 비트 3·2·1·0 네 개 다 |
| 꼬리를 16 B 블록으로 올려 복사 | canary 파손 + 페이지 끝에서 SIGBUS (버그 3) | 꼬리는 정확히 n & 15 바이트 |
| x0 을 커서로 사용 | 내용 OK, 반환값 76,800회 틀림 (버그 4) | `mov x3, x0` 후 x3 만 전진 |
| strlen 에서 post-index 보정 누락 | `strlen("") = 1` (버그 6) | `sub x0, x0, #1` |
| 머리 바이트 수를 `dst & 15` 로 | 테스트 **통과**, dst 미정렬 (버그 5) | `neg` + `and #15` |
| 맥에서 `_` 없이 `.globl memcpy_16` | 링크: `Undefined symbols for architecture arm64: "_memcpy_16"` | `SYM()` 매크로 |
| `;` 로 주석 | 맥은 통과(Mach-O 에서 `;` 는 주석), 리눅스 gcc: `Error: unknown mnemonic 'return'` | `//` 만 사용 |
| 길이 카운터를 `w2` 로 | n ≥ 4 GiB 에서 잘림. 퍼즈로는 못 잡는다 | `size_t` 는 항상 x 레지스터 |

`;` 주석 함정의 실제 재현이다. 맥에서는 같은 파일이 그냥 어셈블된다. AArch64 ELF 에서는 `;` 가 **문장 구분자**라서 뒤의 단어가 명령으로 읽힌다:

```text
$ gcc -c semi.S          # 4행: mov x0, #1    ; return 1
semi.S:4: Error: unknown mnemonic `return' -- `return 1'
$ clang -c semi.S
semi.S:4:18: error: unrecognized instruction mnemonic
```

거꾸로 Mach-O AArch64 에서는 문장 구분자가 `%%` 다. 그래서 여러 문장을 한 줄에 넣는 매크로는 이식이 안 된다. 이 레슨의 `FUNC_TYPE/FUNC_SIZE` 매크로가 한 줄에 하나씩인 이유다.

## 8. 면접 Q&A

<details>
<summary>Q1. 당신 memcpy 가 libc 보다 느린 이유는? 더 빠르게 하려면?</summary>

실측으로 4 KB 에서 71 ns 대 42 ns 였다. 이유는 다섯 가지다.

1. libc 는 **q 레지스터 쌍(`LDNP q0, q1`)으로 명령당 32 B** 를 옮긴다. 내 것은 x 쌍 16 B 다.
2. 머리·꼬리를 루프 대신 **겹쳐 쓰는 32 B 통째 복사**로 처리해 분기가 거의 없다.
3. 크기별로 경로를 나눈다 (< 64 B, < 16 KB, 그 이상).
4. 큰 복사에는 non-temporal 힌트(`STNP`)를 쓴다.
5. 코어별로 튜닝돼 있다.

다음 단계는 순서대로: 바퀴당 32~64 B 로 언롤링해서 루프 오버헤드를 줄이고(추가 연습 3), q 레지스터를 쓰고(ARM-M07), 머리·꼬리 겹침 트릭을 넣는다. 결론은 "직접 짜기보다 libc/컴파일러 builtin 을 쓰되, 펌웨어에서 libc 가 없거나 특수 메모리면 직접" 이다.

</details>

<details>
<summary>Q2. memcpy 와 memmove 차이는? 겹치면 당신 코드는 어떻게 되나?</summary>

`memcpy` 는 겹치지 않는다는 전제가 계약이다 (C11 7.24.2.1, `restrict`). 겹치면 UB 다. `memmove` 는 임시 버퍼에 복사하는 것처럼 동작한다고 정의돼 있다. 구현은 방향을 고른다: `(dst − src)` 를 부호 없는 수로 봐서 n 보다 작으면 dst 가 src 범위 안이다 → 뒤에서부터 복사한다.

내 `memcpy_16` 은 늘 앞에서부터 복사하므로 dst > src 로 겹치면 원본이 덮인다. 한 바이트 겹침이면 첫 바이트가 계속 퍼지는 식이다. 몸통 16 B 블록 때문에 패턴은 더 복잡해진다. 이 맥의 libc `memcpy` 는 실제로 `_platform_memmove` 로 해석된다 (lldb 실측). 그래서 겹쳐도 맞게 나오지만, 그건 이 플랫폼 우연이다. newlib/glibc 에 기대면 안 된다.

</details>

<details>
<summary>Q3. src 가 정렬이 안 됐는데 LDP 해도 되나? Cortex-M0 이나 MMU 꺼진 부트 코드에선?</summary>

- **AArch64 사용자 모드(EL0), 일반(Normal) 메모리**: `SCTLR_EL1.A = 0` 이면 비정렬 `LDR/LDP` 가 허용된다. 맥과 리눅스가 이 설정이다. 그래서 테스트가 통과했다.
- **Device 메모리**: 비정렬 접근이 alignment fault 다.
- **MMU 를 켜기 전 AArch64**: 데이터 접근이 Device-nGnRnE 로 취급된다 (Arm ARM 기준, 확인 권장). 그래서 MMU 켜기 전 부트 코드의 memcpy 는 정렬된 접근만 써야 한다. 실제 U-Boot/TF-A 초기 코드가 이걸 신경 쓴다. MMU 켜기 자체는 ARM-A03.
- **Cortex-M0/M0+ (ARMv6-M)**: 모든 비정렬 접근이 HardFault 다.
- **Cortex-M3/M4 (ARMv7-M)**: `LDR/STR/LDRH/STRH` 의 비정렬은 지원한다. `LDM/STM/LDRD/STRD` 는 fault 다. `CCR.UNALIGN_TRP` 로 전부 trap 시킬 수도 있다 (ARMv7-M ARM 기준).

그래서 펌웨어 memcpy 는 "정렬 맞으면 워드, 아니면 바이트" 로 가는 경우가 많다.

</details>

<details>
<summary>Q4. 왜 dst 를 정렬하나? src 를 정렬하면 안 되나?</summary>

둘의 어긋남이 다르면 하나만 맞출 수 있다. 흔한 선택은 dst 다. 비정렬 store 가 캐시 라인 두 개에 걸치면 store 쪽에서 쪼개지는 비용이 더 크고, 쓰기 쪽 캐시 라인 전체 쓰기 최적화도 기대할 수 있다 (일반론, 코어마다 다름). Apple libc 도 dst 를 32 B 로 맞춘다 (`and x3, x3, #~0x1f`).

다만 이 M6 에서는 dst 를 안 맞춘 버그 5 판과 시간 차이가 소음 안이었다 (~71 ns vs ~71 ns). "정렬이 중요하다" 는 코어와 메모리 종류에 따라 크기가 달라지는 주장이니 숫자로 말할 것.

</details>

<details>
<summary>Q5. libc strlen 은 NUL 뒤까지 읽던데, 그건 버그 아닌가?</summary>

C 프로그램이 객체 밖을 읽는 건 UB 다. 하지만 libc 는 "구현" 쪽이라 하드웨어 규칙만 지키면 된다. 메모리 보호는 **페이지 단위**(이 맥 16 KB, 리눅스 VM 4 KB)이고, 16 B 로 정렬된 읽기는 절대 페이지 경계를 넘지 않는다. 그래서 NUL 이 든 청크를 통째로 읽어도 fault 가 날 수 없다.

부작용으로 ASan·Valgrind 는 libc 함수를 별도로 처리(인터셉트)한다. MTE(메모리 태깅) 시스템에서는 태그 단위가 16 B 라서, 16 B 정렬 청크 읽기가 태그 경계도 넘지 않는다 (MTE 세부는 확인 권장). 내 asm 이 이 트릭을 쓰려면 반드시 **정렬된** 청크로 읽어야 한다. 추가 연습 1의 `strlen_word` 가 8 B 정렬 판이고, 페이지 끝 테스트를 통과한다.

</details>

<details>
<summary>Q6. C 로 memcpy 를 짰더니 컴파일러가 memcpy 호출로 바꿨다. 언제, 어떻게 막나?</summary>

**언제 바꾸나**: 루프가 겹치지 않는다는 걸 컴파일러가 알 때다 (`restrict`, 또는 다른 증명). 실측으로 `restrict` 판은 clang `b _memcpy`, gcc `bl memcpy` 가 됐다. restrict 없는 판은 겹침 런타임 검사를 넣은 벡터 루프가 됐다.

**막는 방법**: `-ffreestanding` 또는 `-fno-builtin`(clang/gcc 실측으로 루프 유지), gcc 의 `-fno-tree-loop-distribute-patterns`. 테스트의 기준 모델처럼 `volatile` 로 쓰는 방법도 있다.

**펌웨어에서 생기는 일**: `-nostdlib` 인데 memcpy 를 안 넣으면 링크 에러가 난다. 자기 memcpy 안에서 다른 함수를 거쳐 다시 호출되면 재귀가 될 수 있다. gcc 는 freestanding 에서도 `memcpy/memset/memmove/memcmp` 제공을 요구한다고 문서에 적혀 있다.

</details>

<details>
<summary>Q7. 왜 memcpy 는 dst 를 돌려주나? 이 함수에 스택 프레임이 필요한가?</summary>

반환값은 `strcpy(memcpy(...), ...)` 처럼 이어 쓰기 위한 역사적 관례다. 대가로 구현은 x0 을 보존해야 한다. 그래서 커서는 x3 이다.

이 함수는 leaf 이고 x0..x6 만 쓴다. 모두 caller-saved 다. 다른 함수를 부르지 않으니 x30(LR)을 저장할 필요가 없다. callee-saved x19..x28 도 안 쓰니 `stp x29, x30` 프레임 없이 `ret` 만 하면 된다. 프레임이 필요한 경우는 ARM-B08. 디버거 백트레이스는 LR 로 호출자를 찾는다. 크래시 덤프에서 memcpy 가 맨 위 프레임이면 **LR 이 호출자**다. 범인은 보통 그 호출자가 넘긴 n 이나 포인터다.

</details>

<details>
<summary>Q8. 1 MB 를 옮긴다면 CPU memcpy 와 DMA 중 무엇을?</summary>

이 맥에서 1 MB CPU 복사는 libc 로 ~9–14 µs 였다. 그동안 코어 하나를 다 쓴다. 펌웨어에서는 큰 이동(호스트 ↔ NAND 버퍼, 패킷 페이로드)을 DMA 에 맡기고 CPU 는 다른 일을 한다. 대신 비용이 따른다:

- 설정 오버헤드
- 캐시 일관성 처리: CPU 가 쓴 버퍼는 DMA 전에 clean, DMA 가 쓴 버퍼는 CPU 가 읽기 전에 invalidate (ARCH-M12)
- 완료 인터럽트 지연

기준은 보통 "수백 B 이하는 CPU, KB 이상 + 비동기 가능하면 DMA" 다. 정확한 문턱값은 칩마다 다르니 측정해서 정한다.

</details>

## 9. 추가 연습

### 연습 1 — `strlen_word`: 8 바이트씩 (haszero 트릭)

`size_t strlen_word(const char *s)` 를 정렬된 8 B 읽기만으로 구현하라. s 가 정렬 안 돼 있으면 첫 워드에서 s 앞쪽 바이트를 가려야 한다. 페이지 끝 테스트를 통과해야 한다.

<details>
<summary>답</summary>

1. s 를 8 B 로 내림 정렬해서 첫 워드를 읽는다.
2. 리틀 엔디언이라 s 앞쪽 바이트는 **하위 바이트**다. `orn x3, x3, (−1 << 8a)` 로 그 바이트를 0xFF 로 덮는다. 그러면 s 앞에 있는 NUL 이 무시된다.
3. 워드마다 `(v − 0x0101…01) & ~v & 0x8080…80` 을 계산한다. 0 이 아니면 0 바이트가 있다.
4. 그 값에서 **가장 낮은 1비트**가 첫 0 바이트를 가리킨다. 위쪽 바이트에는 빌림(borrow) 때문에 가짜 표시가 생길 수 있지만, 가장 낮은 것은 항상 정확하다.
5. `rbit` + `clz` 로 그 비트 번호를 구하고 `lsr #3` 하면 바이트 인덱스다.

예 (python 으로 검산): `"abc\0xxxx"` → v = 0x7878787800636261 → v − 0x01…01 = 0x77777776ff626160 → & ~v = 0x07070706ff000100 → & 0x80…80 = **0x80000000** → rbit+clz = 31 → 31 >> 3 = 3 ✓.

`0x0101010101010101` 과 `0x8080808080808080` 은 반복 패턴이라 논리 즉시값으로 **한 명령 `mov`** 에 들어간다 (ARM-B03). 0x01..0xFF 모든 바이트값 × 길이 1..16 을 넣은 거짓 양성 테스트와 페이지 끝 테스트를 둘 다 통과한다 (practice.c).

</details>

### 연습 2 — `memmove_byte`: 겹쳐도 맞게

<details>
<summary>답</summary>

`sub x4, x0, x1` / `cmp x4, x2` / `b.hs forward`. dst − src 를 부호 없는 수로 봤을 때 n 이상이면 앞으로 복사해도 안전하다 (dst < src 면 뺄셈이 감겨서 아주 큰 수가 된다). 아니면 두 포인터를 끝으로 옮기고 **pre-index** `ldrb w5, [x1, #-1]!` / `strb w5, [x3, #-1]!` 로 뒤에서부터 복사한다. Apple `_platform_memmove` 의 첫 세 명령과 같은 판단이다. 이동 거리 −40..+40 × n 0..100 을 "임시 버퍼에 복사 후 붙여넣기" 기준 모델과 비교했다.

</details>

### 연습 3 — `memcpy_32`: 바퀴당 32 B

<details>
<summary>답</summary>

몸통 한 바퀴에 독립된 `ldp x5, x6, [x1]` / `ldp x7, x8, [x1, #16]` 두 개를 쓰고 `add x1, x1, #32` 한다. store 도 같은 식이다. 꼬리는 0..31 이 되므로 `tbz` 를 비트 4(16 B, `ldp`/`stp` post-index)부터 시작한다. 레지스터 4개를 쓰는 이유는 두 load 가 서로 기다리지 않게(의존성 없게) 하려는 것이다. 성능 비교는 직접 `demo_bench.c` 에 넣어 보자.

</details>

### 연습 4 — `memset_16(dst, c, n)`

<details>
<summary>답</summary>

`and x1, x1, #0xff` 로 `(unsigned char)c` 를 만든다. `int` 인자의 x1 상위 32비트는 보장되지 않으므로 어차피 마스크가 필요하다 (ARM-B01). 그다음 `mul x1, x1, 0x0101010101010101` 로 8 바이트에 복제한다. 0x5C → 0x5C5C5C5C5C5C5C5C. 이후 구조는 `memcpy_16` 과 같고, 몸통은 `stp x1, x1, [x3], #16` 이다. 값 0x1234, −1, 0x80000041 처럼 하위 바이트만 의미 있는 입력도 테스트한다. 참고로 실제 libc memset 은 큰 0 채우기에 `DC ZVA`(캐시 라인 0 채우기) 를 쓰기도 한다 (확인 권장).

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S -o /tmp/ARM-B10_practice && /tmp/ARM-B10_practice
# 9/9 passed  (ASan/UBSan 빌드도 9/9, Lima VM gcc 빌드도 9/9)
```

## 10. 핵심 정리

- 바이트 루프 = `ldrb/strb` post-index + `subs`/`b.ne`. **루프 앞 `cbz n`**. x0 은 반환값이니 커서는 다른 레지스터에 둔다.
- strlen 의 post-index 커서는 NUL 다음을 가리킨다 → `end − start − 1`.
- 빠른 memcpy = **머리**(dst 정렬, `(−dst) & 15`) + **몸통**(`LDP/STP` 16 B) + **꼬리**(`TBZ` 로 n 의 비트 3·2·1·0).
- src 와 dst 는 동시에 정렬할 수 없다 → dst 를 맞춘다. 일반 메모리 EL0 는 비정렬 LDP 가 되지만 Device 메모리, MMU 끄기 전, Cortex-M0 는 fault 다.
- 테스트 = 모든 길이 × 모든 src/dst 정렬 + canary + 반환값 + 페이지 끝 `PROT_NONE`. 정렬을 다 돌지 않으면 "짝수 길이도 틀리는" 버그를 놓친다.
- M6 실측: 바이트 ~4 GB/s, 16 B 루프 ~60 GB/s, libc ~85–110 GB/s. libc = q 레지스터 32 B + 겹쳐 쓰는 머리·꼬리 + 크기별 분기 + `STNP`.
- libc strlen 이 NUL 뒤를 읽는 건 **정렬된 청크라 페이지를 못 넘기 때문에** 안전하다. 내 코드도 정렬 청크로만 하자.
- 겹침은 memmove 의 몫이다 (`(dst−src) <u n` 이면 뒤에서). C 루프는 `restrict` 면 `memcpy` 호출로 바뀐다. `-ffreestanding`/`-fno-builtin` 으로 막는다.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — LDP/STP (pair, post/pre-index 인코딩), LDRB/STRB, TBZ, Alignment fault·Device memory 규칙: https://developer.arm.com/documentation/ddi0487/latest
- Arm A64 Instruction Set (명령별 페이지, LDP 인코딩): https://developer.arm.com/documentation/ddi0602/latest
- Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64) — x0..x7 인자, caller/callee-saved: https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst
- Apple — Writing ARM64 code for Apple platforms: https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms
- ARMv7-M Architecture Reference Manual (DDI 0403) — 비정렬 접근 지원 범위, `CCR.UNALIGN_TRP`: https://developer.arm.com/documentation/ddi0403/latest
- ISO C11 (N1570) 7.24.2.1 memcpy / 7.24.2.2 memmove: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
- GCC 문서 — `-ffreestanding` 에서도 memcpy/memmove/memset/memcmp 필요, `-ftree-loop-distribute-patterns`: https://gcc.gnu.org/onlinedocs/gcc/Standards.html , https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
- Arm Optimized Routines (memcpy/strlen 레퍼런스 구현, AArch64): https://github.com/ARM-software/optimized-routines
- Bit Twiddling Hacks — "Determine if a word has a zero byte": https://graphics.stanford.edu/~seander/bithacks.html#ZeroInWord
- 이 디렉터리의 실측 파일: `bench_m6.txt` (5회 벤치 원본), `libc_disasm_m6.txt` (Apple libc 디스어셈블), `trace_m6.txt` (memcpy_16 단계 추적)
- 관련 레슨: ARM-B04 (주소 지정 모드), ARM-B05 (플래그·TBZ), ARM-B08 (프레임), ARM-B09 (lldb·strlen 버그 찾기), ARM-M03 (벤치 하네스), ARM-M07 (NEON memchr), ARCH-M12 (DMA·캐시), BM-B08 (부트 .data 복사)
