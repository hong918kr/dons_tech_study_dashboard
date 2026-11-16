---
id: TOOL-B03
time: 60분
updated: 2026-11-15
---
# TOOL-B03 최적화 레벨 비교: -O0 -O1 -O2 -O3 -Os -Oz

## 학습 목표

- 같은 벤치 프로그램을 `-O0 -O1 -O2 -O3 -Os -Oz` (+ `-Og`) 로 빌드해 **`.text` 크기 · 실행 시간** 표를 실제 도구(`objdump -h`, `size -m`, `llvm-size -A`, `arm-none-eabi-size`)로 만들고, 그 숫자로 "펌웨어는 왜 보통 `-Os`/`-O2` 인가" 를 설명한다.
- `-O2` 빌드를 lldb/gdb 로 멈춰 `<variable not available>` / `<optimized out>` 을 직접 보고, 왜 그런지(레지스터 할당 · 생존 구간 · DWARF 위치 목록) 와 `-Og` 가 무엇을 바꾸는지 실측으로 말한다.
- **정의되지 않은 동작(UB, undefined behavior)** 이 있는 코드가 `-O0` 에선 맞고 `-O1`/`-O2` 에서 틀리는 것을 4가지(부호 있는 오버플로 검사, 래핑을 가정한 루프, strict aliasing, 배열 밖 읽기)로 재현하고, UB 없는 코드로 고쳐 **6개 레벨 모두 같은 출력**을 만든다.
- busy-wait 지연(`for (i = 0; i < n; i++);`)이 `-O2` 에서 사라지는 이유와 `volatile` 의 올바른/잘못된 사용, 타이머 기반 지연이 왜 정답인지 안다.

## 1. 문제

"디버그 빌드에서는 되는데 릴리스 빌드에서만 깨져요" — 펌웨어 팀에서 가장 흔한 버그 리포트이고, 면접에서도 "-O2 와 -Os 중 뭘 쓰나요?", "최적화 켜면 왜 변수가 안 보이나요?" 로 자주 나온다. SSD 컨트롤러나 Cortex-M 펌웨어는 Flash 크기 한도 안에서 릴리스 빌드(`-Os`/`-O2`)로 출하되므로, **최적화 레벨이 크기·속도·디버깅·정확성에 무엇을 하는지** 숫자로 알고 있어야 한다.

최적화 레벨은 컴파일러가 돌리는 **변환 패스(pass) 묶음**의 이름이다. 소스의 의미(C 표준이 보장하는 관찰 가능한 동작)는 유지해야 하지만, 그 "의미" 에는 **UB 가 없다는 전제**가 포함된다. 그래서 UB 가 있는 코드는 레벨마다 다른 결과를 낸다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">demo_bench.c 실측: 코드 크기(왼쪽, Cortex-M4 .o 의 .text) vs 실행 시간(오른쪽, Mac arm64)</text>
  <rect x="380" y="30" width="12" height="10" class="f-acc" stroke="currentColor"/><text x="396" y="40" fill="currentColor" font-size="12">clang thumbv7em</text>
  <rect x="510" y="30" width="12" height="10" class="f-ok" stroke="currentColor"/><text x="526" y="40" fill="currentColor" font-size="12">arm-none-eabi-gcc 14</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="70">-O0</text><text x="10" y="104">-O1</text><text x="10" y="138">-O2</text><text x="10" y="172">-O3</text>
    <text x="10" y="206">-Os</text><text x="10" y="240">-Oz</text><text x="10" y="274">-Og</text>
  </g>
  <g stroke="currentColor">
    <rect x="50" y="56" width="66" height="12" class="f-acc"/><rect x="50" y="70" width="71" height="12" class="f-ok"/>
    <rect x="50" y="90" width="54" height="12" class="f-acc"/><rect x="50" y="104" width="42" height="12" class="f-ok"/>
    <rect x="50" y="124" width="274" height="12" class="f-acc"/><rect x="50" y="138" width="52" height="12" class="f-ok"/>
    <rect x="50" y="158" width="299" height="12" class="f-acc"/><rect x="50" y="172" width="79" height="12" class="f-ok"/>
    <rect x="50" y="192" width="51" height="12" class="f-acc"/><rect x="50" y="206" width="34" height="12" class="f-ok"/>
    <rect x="50" y="226" width="33" height="12" class="f-acc"/><rect x="50" y="240" width="34" height="12" class="f-ok"/>
    <rect x="50" y="260" width="58" height="12" class="f-acc"/><rect x="50" y="274" width="40" height="12" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="122" y="66">678</text><text x="127" y="80">736</text>
    <text x="110" y="100">554</text><text x="98" y="114">432</text>
    <text x="330" y="134">2832</text><text x="108" y="148">532</text>
    <text x="355" y="168">3084</text><text x="135" y="182">812</text>
    <text x="107" y="202">526</text><text x="90" y="216">356</text>
    <text x="89" y="236">344</text><text x="90" y="250">356</text>
    <text x="114" y="270">598</text><text x="96" y="284">416</text>
  </g>
  <line x1="410" y1="50" x2="410" y2="290" stroke="currentColor" stroke-dasharray="3 3"/>
  <g stroke="currentColor" class="f-warn">
    <rect x="430" y="62" width="150" height="14"/><rect x="430" y="96" width="53" height="14"/>
    <rect x="430" y="130" width="46" height="14"/><rect x="430" y="164" width="46" height="14"/>
    <rect x="430" y="198" width="42" height="14"/><rect x="430" y="232" width="45" height="14"/>
    <rect x="430" y="266" width="49" height="14"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="586" y="74">94.1 ms</text><text x="489" y="108">33.6 ms</text><text x="482" y="142">28.6 ms</text>
    <text x="482" y="176">28.7 ms</text><text x="478" y="210">26.2 ms</text><text x="481" y="244">28.0 ms</text>
    <text x="485" y="278">30.7 ms</text>
  </g>
  <text x="10" y="310" fill="currentColor" font-size="12">크기는 약 9배 벌어지는데(344 B ~ 3084 B), -O1 이상 끼리의 시간 차이는 실행마다 바뀌는 잡음 수준이다.</text>
  <text x="10" y="326" fill="currentColor" font-size="12">clang 의 -O2/-O3 는 루프 펼치기·벡터화로 커지고, gcc 는 같은 -O2 에서도 훨씬 보수적이다 (5. 풀이 Part A).</text>
</svg>
```

### Part A — 크기·속도 표와 디버거 경험

- `demo_bench.c` (비트 단위 CRC-32 4 KB + 삽입 정렬 512개 + Q15 FIR 16탭) 를 7개 레벨로 빌드한다. 표 열: 커널만 컴파일한 Mach-O `.o` 의 `__text`, 링크된 실행 파일의 `__text` (`size -m`), `--target=thumbv7em-none-eabi` `.o` 의 `.text` (`llvm-size -A`), 5회 실행 **중앙값** 시간, `-O0` 대비 배수, 체크섬(모든 레벨이 같아야 함). VM 에서는 `arm-none-eabi-gcc -mcpu=cortex-m4` 의 Berkeley `text/data/bss` 와 gcc aarch64 실행 시간.
- `demo_optout.c` 의 같은 줄(25행)에 `-O0 / -O1 / -Og / -O2` 빌드로 브레이크포인트를 걸고 `frame variable` (lldb), `info locals` (gdb) 결과를 비교한다.

### Part B — "릴리스에서만 틀리는" 함수 4개 고치기

`starter.c` 에 펌웨어에서 볼 법한 함수 4개가 있다. 전부 `-O0` 에서는 16/16 통과하지만, 각자 UB 를 하나씩 숨기고 있다.

| 함수 | 상황 | 숨은 UB |
|---|---|---|
| `range_ok(off, len, cap)` | 수신 패킷 헤더의 offset/len 범위 검사 | `off + len < off` — 오버플로 검사 자체가 부호 있는 오버플로 |
| `frames_until_wrap(seq, max)` | int32 시퀀스 번호가 넘어가기 전 남은 프레임 수 | `s > 0` 로 래핑을 잡으려 하지만 `s++` 가 `INT32_MAX` 를 넘는 순간 UB |
| `pack_float_word(tx, v)` | DMA TX 슬롯(`uint32_t`)에 float 샘플을 넣고 워드 반환 | 같은 슬롯을 `uint32_t *` 와 `float *` 로 동시에 접근 (strict aliasing 위반) |
| `cal_has_code(code)` | 보정 테이블에 코드가 있나 | `i <= 4` 로 `cal_codes[4]` (배열 밖) 읽기 |

함수 본문만 고쳐(시그니처 유지) `check.sh` 의 모든 검사를 통과시킨다: 6개 레벨 빌드(`-Werror`) · 각 레벨 16/16 · **6개 출력이 바이트 단위로 동일** · `-fsanitize=address,undefined` 깨끗.

### Part C — 펌웨어 빌드에서 레벨 고르기

- Cortex-M4 용 오브젝트 크기를 `arm-none-eabi-gcc` 와 clang 으로 함수별(`nm --print-size`)로 비교해 "왜 `-O2` 가 Flash 를 먹는가" 를 짚는다.
- `starter.c` 의 UB 함수가 Cortex-M4 에서도 똑같이 사라지는지(`movs r0, #1; bx lr`) 확인한다.
- `-g` 가 코드를 바꾸지 않는다는 것, 부분 최적화(`-Og`, 함수 단위 `optnone`)와 CI 레벨 매트릭스(check.sh 가 바로 그것)를 정리한다. 추가 연습에서 busy-wait 지연을 `-O2` 에서 측정한다.

## 2. Problem (English)

> **Optimization levels: size, speed, debuggability, and UB.**
>
> 1. Build `demo_bench.c` at `-O0 -O1 -O2 -O3 -Os -Oz` (and `-Og`). Report `.text` size (host Mach-O and a Cortex-M4 object) and the median run time of 5 runs. The checksum must be identical at every level. Which level would you ship on a 256 KB-Flash Cortex-M4, and why?
> 2. Stop at the same source line in a `-O0`, `-Og` and `-O2` build. Explain why `sum` shows `<variable not available>` (lldb) / `<optimized out>` (gdb) and why `-O0` shows a garbage `r` after `finish`.
> 3. The following functions pass every test at `-O0` but fail at `-O1`/`-O2`:
>    `bool range_ok(int32_t off, int32_t len, int32_t cap)`, `int frames_until_wrap(int32_t seq, int max)`,
>    `uint32_t pack_float_word(struct tx_view tx, float v)`, `bool cal_has_code(int16_t code)`.
>    Rewrite the bodies without undefined behaviour so that the program prints **byte-identical, correct** output at all six levels and is clean under `-fsanitize=address,undefined`. Do not change signatures, do not add `-fwrapv`/`-fno-strict-aliasing`.
>
> Example: `range_ok(INT32_MAX - 10, 100, 1000)` must be `false` (today it returns `true` at `-O2`); `pack_float_word(tx, 1.5f)` must be `0x3FC00000` (today `0` at `-O1`).

## 3. 시작 코드

<!-- code: starter.c -->

검사기는 스크립트다 (Form S). 변형(starter/solution/practice) 하나를 받아 6개 레벨로 빌드·실행·비교하고 새니타이저까지 돌린다. 작업 파일은 전부 `/tmp/TOOL-B03_work/<variant>` 에만 쓴다.

<!-- code: check.sh -->

```bash
bash check.sh starter    # 처음: 7/14 passed, exit 1
bash check.sh solution   # 14/14 passed, exit 0
bash check.sh practice   # 16/16 passed, exit 0
```

`bash check.sh starter` 실제 출력 (Apple clang 21, M6 Mac):

```text
PASS build -O0
PASS run   -O0  (16/16 passed)
PASS build -O1
FAIL run   -O1  (10/16 passed, exit 1)
     FAIL range_ok(INT32_MAX - 10, 100, 1000)          = 1 (want 0)
     FAIL range_ok(100, INT32_MAX, 1000)               = 1 (want 0)
     FAIL frames_until_wrap(INT32_MAX, 10)             = 10 (want 1)
     FAIL frames_until_wrap(INT32_MAX - 5, 100)        = 100 (want 6)
     FAIL pack_float_word(tx, 1.5f)                    = 0 (want 1069547520)
     FAIL pack_float_word(tx, -2.0f)                   = 0 (want 3221225472)
PASS build -O2
FAIL run   -O2  (8/16 passed, exit 1)
     … (-O1 의 6줄 +)
     FAIL cal_has_code(0x5A5A)                         = 1 (want 0)
     FAIL cal_has_code(12345)                          = 1 (want 0)
PASS build -O3
FAIL run   -O3  (8/16 passed, exit 1)          ← -O2 와 같은 8줄
PASS build -Os
FAIL run   -Os  (10/16 passed, exit 1)         ← -O1 과 같은 6줄 (cal_has_code 는 통과!)
PASS build -Oz
FAIL run   -Oz  (10/16 passed, exit 1)         ← 〃
FAIL stdout differs from -O0 at: -O1 -O2 -O3 -Os -Oz
FAIL ASan+UBSan (exit 134)
     starter.c:27:10: runtime error: signed integer overflow: 2147483637 + 100 cannot be represented in type 'int32_t' (aka 'int')
7/14 passed
```

데모 파일 (검사와 별개, 이 디렉터리에 있음 — 출력은 `*.txt` 로 캡처해 둠):

```bash
bash demo_bench.sh mac  > bench_mac.txt    # 크기/시간 표 (Mac) + thumbv7em 함수별 크기
bash demo_bench.sh vm   > bench_vm.txt     # arm-none-eabi-gcc Cortex-M4 크기 + gcc aarch64 시간 (Lima VM)
bash demo_optout.sh mac > optout_mac.txt   # lldb -b, demo_optout.lldb, -O0/-O1/-Og/-O2
bash demo_optout.sh vm  > optout_vm.txt    # gcc -static + qemu-aarch64 -g + gdb-multiarch
bash demo_ub.sh asm     > ub_asm.txt       # starter/solution 의 -O0 / -O2 디스어셈블
bash demo_ub.sh flags   > ub_flags.txt     # -fwrapv / -fno-strict-aliasing, UBSan/ASan 리포트
bash demo_ub.sh vm      > ub_vm.txt        # GCC 15 레벨별, TypeSanitizer, Cortex-M4 asm
bash demo_delay.sh      > delay_mac.txt    # busy-wait 4종 ns/반복 표 + asm (추가 연습)
```

## 4. 힌트

<details>
<summary>힌트 1 — "-O0 에서 맞다" 는 증거가 아니다</summary>

`-O0` 은 문장 하나하나를 스택 메모리 읽기/쓰기로 그대로 옮긴다. UB 가 있어도 "하드웨어가 하는 대로"(2의 보수 래핑, 메모리 그대로 읽기) 동작하는 것처럼 **보일** 뿐이다. 최적화기는 "이 프로그램에 UB 는 없다" 를 전제로 추론하므로, UB 가 일어나는 입력에서는 어떤 결과도 허용된다. 실패하는 레벨의 디스어셈블을 보라: `bash demo_ub.sh asm`.

</details>

<details>
<summary>힌트 2 — 오버플로는 "일어난 뒤" 가 아니라 "일어나기 전" 에 검사</summary>

`off + len < off` 는 더하는 순간 이미 UB 다. 모든 값이 0 이상이라는 걸 먼저 확인했다면 `cap - off` 는 오버플로할 수 없다 → `len <= cap - off` 로 바꿔 쓸 수 있다. 루프도 같다: `s++` **전에** `s == INT32_MAX` 인지 본다. (GCC/Clang 의 `__builtin_add_overflow` 도 정답이다 — 추가 연습 4.)

</details>

<details>
<summary>힌트 3 — 타입 퍼닝(type punning)은 memcpy 로</summary>

C11 6.5p7: 객체는 자기 유효 타입(effective type) 또는 문자 타입 등 허용된 lvalue 로만 접근해야 한다. `uint32_t` 객체에 `float` lvalue 로 쓰면 UB 이고, 최적화기는 "float 저장은 uint32_t 읽기에 영향을 못 준다(TBAA)" 고 보고 `*tx.word` 를 0 으로 단정한다. `memcpy(tx.word, &v, sizeof v)` 는 바이트 복사라 앨리어싱 규칙 밖이고, `-O2` 에서는 `str s0` 한 개로 컴파일된다.

</details>

<details>
<summary>힌트 4 — 배열 길이는 배열에서 가져온다</summary>

`for (i = 0; i <= 4; i++)` 에서 `i == 4` 반복은 UB 다. 컴파일러 입장에서는 "UB 가 없으려면 i==4 까지 가기 전에 반드시 return true 했어야 한다" → 함수 전체가 `return true`. 경계를 `sizeof a / sizeof a[0]` 로 쓰면 테이블 크기가 바뀌어도 안전하다.

</details>

## 5. 풀이

### Part A — 크기·속도 표 (`demo_bench.sh mac`, 실제 출력)

```text
# Apple clang 21.0.0 (clang-2100.0.123.102), arm64, reps=400
level     kernel.o   exe.__text    thumbv7em.o  median_ms  speedup  checksum
-O0          1120         1472            678      94.08     1.0x  checksum=0x529a1652
-O1           664          924            554      33.55     2.8x  checksum=0x529a1652
-O2          2908         3172           2832      28.64     3.3x  checksum=0x529a1652
-O3          3548         3812           3084      28.66     3.3x  checksum=0x529a1652
-Os           680          936            526      26.18     3.6x  checksum=0x529a1652
-Oz           500          740            344      28.00     3.4x  checksum=0x529a1652
-Og           716         1008            598      30.71     3.6x  checksum=0x529a1652
```

크기를 어디서 읽었나 (같은 실행에서 나온 원본 출력):

```text
$ size -m bench.O2                       $ /usr/bin/objdump -h k.O2.o
Segment __TEXT: 16384                    Idx Name             Size     VMA              Type
	Section __text: 3172                   0 __text           00000b5c 0000000000000000 TEXT
	Section __stubs: 48                    1 __literal16      00000040 0000000000000b60 DATA
	Section __const: 80                    2 __literal8       00000010 0000000000000ba0 DATA
	Section __cstring: 31                  3 __bss            00002400 0000000000000c30 BSS
	Section __unwind_info: 104             4 __compact_unwind 00000080 0000000000000bb0 DATA
…
Segment __DATA: 16384
	Section __bss: 9216 (zerofill)
```

- `0xb5c` = 2908 바이트가 표의 `kernel.o`. `__literal16` (16바이트 상수 = NEON 벡터 상수)가 `-O2` 에서 생긴 것 자체가 **벡터화**의 흔적이다.
- 실행 파일 `__text` 는 커널 + `main` + 타이밍 코드라 약 240~350 B 더 크다. 펌웨어 관점의 숫자는 오른쪽 `thumbv7em.o` (`llvm-size -A t.O2.o` 의 `.text`) 와 아래 VM 표다.
- 시간은 **잡음이 크다** — 이 맥에서 다른 작업이 함께 돌았다. 같은 스크립트 4회 실행의 중앙값 범위: `-O0` 94–122 ms, `-O1` 30.4–34.8, `-O2` 27.9–31.3, `-O3` 24.9–29.0, `-Os` 26.2–33.1, `-Oz` 28.0–31.1, `-Og` 30.7–35.7. 결론은 범위로 말한다: **-O0 → 최적화 레벨 = 약 3~4배**, -O1 이상끼리는 이 벤치에선 ±15% 안에서 순위가 바뀐다.

VM (`demo_bench.sh vm`, 실제 출력 — 펌웨어 숫자는 왼쪽 세 열):

```text
# arm-none-eabi-gcc (15:14.2.rel1-1) 14.2.1 20241119  (-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -c)
# gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0  aarch64 Linux (VM), reps=400
level     text     data      bss aarch64.text  median_ms  speedup  checksum
-O0       736        0    13312         1156     137.49     1.0x  checksum=0x529a1652
-O1       432        0    13312          548      28.91     4.8x  checksum=0x529a1652
-O2       532        0    13312          944      27.42     5.0x  checksum=0x529a1652
-O3       812        0    13312         3304      20.42     6.7x  checksum=0x529a1652
-Os       356        0    13312          492      36.98     3.7x  checksum=0x529a1652
-Oz       356        0    13312          492      34.74     4.0x  checksum=0x529a1652
-Og       416        0    13312          596      29.14     4.7x  checksum=0x529a1652
```

(VM 시간은 더 흔들린다: 세 번 돌린 `-O0` 중앙값이 232 / 161 / 137 ms. 크기는 매번 똑같다.)

표에서 읽을 것:

| 관찰 | 이유 |
|---|---|
| `-O0` 가 가장 느리고 `-O1` 보다도 크다 | 모든 변수를 매번 스택에 쓰고 다시 읽는다 (`str`/`ldr` 범벅, 아래 `range_ok` -O0 asm). 크기도 손해 |
| clang `-O2`/`-O3` 가 4~5배 커짐 (thumb 554 → 2832 → 3084) | 루프 펼치기(unroll) · 벡터화 · 인라이닝. 함수별 표(Part C)에서 `isort_i16` 74 → 804 B |
| gcc 는 `-O2` 에서도 532 B | GCC 의 `-O2` 는 펼치기를 거의 안 함(비용 모델이 보수적), `-O3` 에서 812 B |
| `-Os` ≈ `-O1` 크기, 속도는 `-O2` 급 | 크기를 키우는 변환만 끈 `-O2`. 이 벤치에선 시간 손해가 잡음 수준 |
| `-Oz` 가 가장 작음 (clang 344 B), gcc 는 `-Os` = `-Oz` (356) | clang `-Oz` 는 크기를 위해 속도를 더 포기한다. gcc 14 에서 이 코드는 두 레벨의 결과가 같았다 |
| clang thumb `.bss`: `-O1/-O2/-O3/-Os/-Og` 9216, `-O0`/`-Oz` 13312, gcc 는 모든 레벨 13312 | clang 이 `fir_q15` 를 인라인한 레벨에서 쓰기만 하고 아무도 읽지 않는 `static int16_t g_out[2048]` (4096 B) 를 통째로 지웠다 (`llvm-nm t.O2.o` 에 `g_out` 없음, `t.O0.o` 엔 있음). 최적화는 **RAM** 도 바꾼다 |
| 체크섬이 14개 빌드 모두 `0x529a1652` | UB 없는 코드는 어느 레벨에서도 같은 답 — 이것이 Part B 의 목표 |

`-Og` 를 clang 이 실제로 어떻게 다루나 (`cc -### -Og -c demo_bench.c`, 이 맥 Apple clang 21 과 VM 의 clang 21.1.8 둘 다):

```text
"-Og"
"-fextend-variable-liveness=all"
```

예전 clang 은 `-Og` 를 그냥 `-O1` 로 취급했다고 알려져 있지만(확인 권장), clang 21 은 `-O1` 급 파이프라인에 **변수 생존 구간 연장** 플래그를 더 붙인다. 실제로 `-Og` 오브젝트는 `-O1` 과 달랐다(`cmp` 불일치, `__text` 1008 vs 924 B).

### Part A — 디버거로 본 `-O2`

`demo_optout.c` 의 `filter_block` 25행 `report(result);` 에서 멈춘다. lldb 실제 출력 (발췌, 전체는 `optout_mac.txt`):

```text
=========== clang -g -O0
Breakpoint 1: where = optout_O0`filter_block + 196 at demo_optout.c:25:9, address = 0x0000000100000628
(lldb) frame variable
(const int16_t *) x = 0x00000001000006a4
(int) n = 8
(int16_t) gain = 3
(int32_t) sum = 156
(int32_t) peak = 75
(int32_t) avg = 19
(int32_t) result = 37
(lldb) expression sum + peak
(int32_t) $0 = 231
(lldb) thread step-out
Return value: (int32_t) $1 = 37
-> 32  		int32_t r = filter_block(samples, 8, 3);
(lldb) frame variable r
(int32_t) r = -191053608

=========== clang -g -Og
warning: optout_Og was compiled with optimization - stepping may behave oddly; variables may not be available.
    frame #0: 0x00000001000005bc optout_Og`filter_block(x=0x0000000100000620, n=8, gain=3) at demo_optout.c:25:2 [opt]
(lldb) frame variable
(const int16_t *) x = 0x0000000100000620
(int) n = 8
(int16_t) gain = 3
(int32_t) sum = <variable not available>
(int32_t) peak = <variable not available>
(int32_t) avg = 19
(int32_t) result = 37

=========== clang -g -O2
Breakpoint 1: where = optout_O2`filter_block + 516 at demo_optout.c:25:2, address = 0x0000000100000758
    frame #0: 0x0000000100000758 optout_O2`filter_block(x=<unavailable>, n=8, gain=3) at demo_optout.c:25:2 [opt]
(lldb) frame variable
(const int16_t *) x = <could not evaluate DW_OP_entry_value: no matching call site param found>
(int) n = 8
(int16_t) gain = 3
(int32_t) sum = <variable not available>
(int32_t) peak = <variable not available>
(int32_t) avg = 19
(int32_t) result = <no location, value may have been optimized out>
(lldb) expression sum + peak
error: Couldn't materialize: couldn't get the value of variable sum: variable not available
(lldb) thread step-out
Return value: (int32_t) $0 = 37
-> 33  		printf("result=%d\n", r);
(lldb) frame variable r
(int32_t) r = 37
```

GCC + gdb (VM, `qemu-aarch64 -g 23403` + `gdb-multiarch` — VM 의 네이티브 gdb 는 SVE 오류로 못 씀) — 제목에 나오는 바로 그 `<optimized out>`:

```text
=========== gcc -g -O0                      =========== gcc -g -O2
x = 0x45b498 <samples>                       Breakpoint 1, filter_block (x=x@entry=0x461cd0 <samples>, n=n@entry=8, gain=gain@entry=3)
n = 8                                        x = 0x461cd0 <samples>
gain = 3                                     n = 8
sum = 156                                    gain = 3
peak = 75                                    sum = <optimized out>
avg = 19                                     peak = 75
result = 37                                  avg = <optimized out>
$1 = 231                                     result = <optimized out>
                                             value has been optimized out      ← print sum + peak
```

(gcc `-Og`: `sum = <optimized out>`, 나머지 `peak/avg/result` 는 값이 보였다.)

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">clang -O2 filter_block 의 DWARF 위치 목록 (.o 주소 0xc ~ 0x230, dwarfdump 실측)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="58">x</text><text x="10" y="88">sum</text><text x="10" y="118">peak</text>
    <text x="10" y="148">avg</text><text x="10" y="178">result</text>
  </g>
  <g stroke="currentColor">
    <rect x="110" y="46" width="527" height="16" class="f-ok"/>
    <rect x="637" y="46" width="34" height="16" class="f-mut"/>
    <rect x="135" y="76" width="449" height="16" class="f-mut"/>
    <rect x="584" y="76" width="29" height="16" class="f-ok"/>
    <rect x="135" y="106" width="449" height="16" class="f-mut"/>
    <rect x="584" y="106" width="29" height="16" class="f-ok"/>
    <rect x="633" y="136" width="20" height="16" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="120" y="58">W0 [0xc, 0x210)</text>
    <text x="145" y="88">상수 0 [0x24, 0x1dc) — 벡터 루프 구간</text>
    <text x="145" y="118">상수 INT32_MIN [0x24, 0x1dc)</text>
    <text x="520" y="102">W10</text><text x="520" y="132">W11</text>
    <text x="560" y="166">W20 [0x20c, 0x220)</text>
    <text x="110" y="178">위치 정보 자체가 없음</text>
  </g>
  <line x1="637" y1="34" x2="637" y2="190" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3"/>
  <text x="440" y="210" fill="currentColor" font-size="12">25행 BP = 0x210 (bl report)</text>
  <text x="10" y="236" fill="currentColor" font-size="12">0x210 은 sum/peak 의 어떤 범위에도 없음 → not available. avg 는 W20 범위 안 → 19. x 는 entry_value 구간 → 복원 실패.</text>
  <text x="10" y="256" fill="currentColor" font-size="12">-O0 은 모든 변수가 함수 전체에서 스택 슬롯 하나 — 그래서 언제 멈춰도 보인다.</text>
</svg>
```

같은 함수를 `-g -O2 -c` 로 만든 `.o` 의 DWARF (`bash demo_optout.sh mac` 끝부분, 실제 출력 발췌). 브레이크포인트 `filter_block + 516` = `.o` 주소 `0xc + 0x204 = 0x210` (`bl report`):

```text
DW_TAG_formal_parameter  x     [0x00c, 0x210): DW_OP_reg0 W0
                               [0x210, 0x230): DW_OP_entry_value(DW_OP_reg0 W0), DW_OP_stack_value
DW_TAG_variable          sum   [0x024, 0x1dc): DW_OP_lit0, DW_OP_stack_value
                               [0x1dc, 0x1f8): DW_OP_reg10 W10
DW_TAG_variable          peak  [0x024, 0x1dc): DW_OP_constu 0xffffffff80000000, DW_OP_stack_value
                               [0x1dc, 0x1f8): DW_OP_reg11 W11
DW_TAG_variable          avg   [0x20c, 0x220): DW_OP_reg20 W20
DW_TAG_variable          result   (DW_AT_location 없음)
     208:  sdiv w20, w10, w1     ; avg = sum / n  — sum(W10) 의 마지막 사용
     20c:  add  w0, w20, w19    ; result 는 w0 에 잠깐
     210:  bl   report          ; ← 25행
```

(주소는 실제 출력을 보기 좋게 줄였다: `0x0000000000000024` → `0x024`.)

해석:

- `-O0` 은 모든 지역 변수에 **스택 슬롯**을 하나씩 주고 함수 내내 그 자리에 둔다 → 언제 멈춰도 값이 보인다. 대신 느리고 크다.
- 최적화 빌드는 변수를 **레지스터**에 두고, 마지막 사용 이후엔 그 레지스터를 다른 값에 재사용한다. 디버그 정보(DWARF)는 위처럼 "주소 범위 [0x1dc, 0x1f8) 동안 `sum` 은 `W10`" 같은 **위치 목록(location list)** 으로 기록하는데, 25행 주소가 어느 범위에도 없으면 lldb 는 `<variable not available>`, gdb 는 `<optimized out>` 이라 말한다. 값을 아예 계산하지 않고 상수로 접힌 경우도 같다.
- `x=<unavailable>` / `DW_OP_entry_value`: 인자 `x` 는 첫 사용 후 레지스터 `x0` 이 덮였고, "호출 시점의 값" 으로 복원하는 DWARF 식(`DW_OP_entry_value`)도 호출 지점 정보가 없어 실패했다. gdb 의 `x=x@entry=0x461cd0` 은 그 복원이 **성공**한 경우다.
- `-O0` 에서 `step-out` 후 `r = -191053608`: 아직 32행의 대입이 실행되기 **전**에 멈췄으므로 `r` 의 스택 슬롯은 쓰레기다(반환값은 `Return value: 37` 줄에 있다). `-O2` 는 반환 후 바로 33행으로 가서 `r = 37` (레지스터 `w0`). "-O0 이면 다 맞게 보인다" 도 사실이 아니다.
- `-Og` (clang 21): 인자 `x` 와 `result` 는 살아났지만 루프 누적값 `sum/peak` 는 여전히 안 보였다. "-Og 면 다 보인다" 가 아니라 "-O2 보다 낫다" 정도로 기억한다.

최적화 빌드를 디버깅하는 실전 요령: 레지스터/디스어셈블 보기(`register read`, `disassemble --line` / gdb `info registers`, `layout asm`), 보고 싶은 함수만 `__attribute__((optnone))` (clang) / `__attribute__((optimize("O0")))` (GCC) 또는 그 파일만 `-O0`/`-Og`, 값을 잠시 `volatile` 지역 변수에 복사. 단, 빌드 전체를 `-O0` 로 바꾸면 **타이밍·UB 버그가 사라질 수 있다**(Heisenbug). 위치 목록·`addr2line`·DWARF 자세히는 TOOL-M14, 디버거 기본 명령은 TOOL-B10.

### Part B — UB 4종이 어떻게 사라지나

`bash demo_ub.sh asm` 실제 출력. 먼저 `-O0` 의 `range_ok` (일부):

```text
      48:      	ldr	w8, [sp, #0x8]        ; off
      4c:      	ldr	w9, [sp, #0x4]        ; len
      50:      	add	w8, w8, w9            ; off + len  (하드웨어는 그냥 래핑)
      54:      	ldr	w9, [sp, #0x8]        ; off
      58:      	subs	w8, w8, w9            ; (off+len) - off 의 부호를 본다
      5c:      	b.ge	0x78                  ; >= 0 이면 다음 검사, 아니면 false
```

`-O0` 은 C 문장을 글자 그대로 옮기므로 래핑된 합을 실제로 비교한다 → "우연히" 맞다. `-O2`:

```text
<_range_ok>:                              <_frames_until_wrap>:
   orr	w8, w1, w0                           bic	w8, w1, w1, asr #31   ; max(max, 0)
   orr	w8, w8, w2                           cmp	w0, #0x0
   cmn	w8, #0x1          ; (off|len|cap) >= 0 ?   csel	w0, w8, wzr, gt     ; seq > 0 ? max : 0
   add	w8, w1, w0        ; off + len
   ccmp	w8, w2, #0x0, gt  ; ... <= cap ?      <_pack_float_word>:
   cset	w0, le                               str	wzr, [x0]             ; *tx.word = 0
   ret                                       str	s0, [x1]              ; *tx.sample = v
                                             mov	w0, #0x0              ; return 0  (다시 읽지 않음!)
<_cal_has_code>:                             ret
   mov	w0, #0x1          ; return true
   ret
```

| 함수 | 최적화기의 추론 (UB 가 없다고 가정) | 결과 |
|---|---|---|
| `range_ok` | off, len ≥ 0 이면 수학적으로 `off + len ≥ off` → `off + len < off` 는 항상 거짓 → 검사 삭제. 남은 `off + len <= cap` 은 래핑된 음수와 비교 | `INT32_MAX-10 + 100` 이 음수로 래핑 → `<= 1000` 참 → **악성 헤더 통과** |
| `frames_until_wrap` | `s` 는 양수에서 시작해 `++` 만 함 → 오버플로가 없으니 `s > 0` 은 항상 참 → 루프 = `min(max, …)` 공식 | 래핑을 못 보고 항상 `max` 반환 (`10`, `100`) |
| `pack_float_word` | `float` 저장은 `uint32_t` 객체를 바꿀 수 없다(TBAA) → 방금 쓴 `0` 을 그대로 반환 | 메모리엔 float 비트가 있는데 반환값은 `0` |
| `cal_has_code` | `i == 4` 반복은 UB → 그 전에 반드시 `return true` 했다 → 함수 = `return true` | 아무 코드나 "있음" |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">range_ok: 같은 소스, 두 가지 "올바른" 번역</text>
  <rect x="10" y="36" width="340" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="56" fill="currentColor" font-family="monospace" font-size="12">if (off + len &lt; off) return false;</text>
  <text x="20" y="72" fill="currentColor" font-family="monospace" font-size="12">return off + len &lt;= cap;</text>
  <rect x="10" y="100" width="340" height="96" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="120" fill="currentColor" font-weight="bold">-O0: 문장 그대로</text>
  <text x="20" y="140" fill="currentColor" font-size="12">add → 0x7FFFFFF5 + 100 = 0x80000059 (래핑)</text>
  <text x="20" y="158" fill="currentColor" font-size="12">음수 &lt; off ? 예 → return false</text>
  <text x="20" y="176" fill="currentColor" font-size="12">결과 0 (원하던 답 — 하지만 우연)</text>
  <rect x="390" y="100" width="340" height="96" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="400" y="120" fill="currentColor" font-weight="bold">-O1 이상: "오버플로는 없다"</text>
  <text x="400" y="140" fill="currentColor" font-size="12">len ≥ 0 → off+len ≥ off → 검사 = 항상 거짓</text>
  <text x="400" y="158" fill="currentColor" font-size="12">삭제 → off+len(래핑 음수) &lt;= 1000 ? 예</text>
  <text x="400" y="176" fill="currentColor" font-size="12">결과 1 → 버퍼 밖 복사 허용</text>
  <path d="M180 80 L180 98" stroke="currentColor" stroke-width="2"/>
  <path d="M300 80 L520 98" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="220" fill="currentColor" font-size="12">둘 다 C 표준상 "허용된" 번역이다: 오버플로가 일어나는 입력에서 표준은 아무것도 요구하지 않는다 (C11 6.5p5).</text>
  <text x="10" y="240" fill="currentColor" font-size="12">고친 코드 len &lt;= cap - off 는 어떤 입력에서도 UB 가 없어 번역이 하나뿐이다.</text>
</svg>
```

**고친 방법** (6. 정답 코드):

1. `range_ok` — 음수 거부 후 `if (off > cap) return false; return len <= cap - off;`. 세 값이 모두 0 이상이고 `off <= cap` 이므로 `cap - off` 는 `0..INT32_MAX` 범위, 오버플로 없음. `-O2` 결과는 `sub w9, w2, w0; cmp w1, w9` — 분기 없이 짧다.
2. `frames_until_wrap` — 루프 안에서 `if (s == INT32_MAX) break;` 를 `s++` **전에** 실행. 증가는 절대 넘치지 않는다.
3. `pack_float_word` — `memcpy(tx.word, &v, sizeof v)`. `-O2` asm 이 `str s0, [x0]; fmov w0, s0; ret` 로 바뀐다 — memcpy 호출은 사라지고 저장 + 레지스터 이동만 남는다(비용 0).
4. `cal_has_code` — `i < sizeof cal_codes / sizeof cal_codes[0]`. `-O2` 는 4개 상수(100/200/300/400)와 비교하는 분기 트리가 된다.

`bash check.sh solution` 실제 출력:

```text
PASS build -O0
PASS run   -O0  (16/16 passed)
PASS build -O1
PASS run   -O1  (16/16 passed)
PASS build -O2
PASS run   -O2  (16/16 passed)
PASS build -O3
PASS run   -O3  (16/16 passed)
PASS build -Os
PASS run   -Os  (16/16 passed)
PASS build -Oz
PASS run   -Oz  (16/16 passed)
PASS stdout identical at all 6 levels
PASS ASan+UBSan clean (-O1)
14/14 passed
```

**어떤 도구가 무엇을 잡나** (`bash demo_ub.sh flags`, `vm` 실제 출력 요약):

| 시도 | 결과 (`starter.c`) |
|---|---|
| `-O2` 그대로 | 8/16 |
| `-O2 -fwrapv` (부호 있는 오버플로를 래핑으로 정의) | 12/16 — `range_ok`, `frames_until_wrap` 은 통과, 나머지 둘 실패 |
| `-O2 -fno-strict-aliasing` | 10/16 — `pack_float_word` 만 통과 |
| `-O2 -fwrapv -fno-strict-aliasing` | 14/16 — 배열 밖 읽기는 어떤 플래그로도 "정의" 되지 않음 |
| `-g -O1 -fsanitize=undefined` | `starter.c:27:10: runtime error: signed integer overflow: 2147483637 + 100 …`, `37:43: … 2147483647 + 1 …`, `65:7: runtime error: index 4 out of bounds for type 'int16_t[4]'` — 3개. **aliasing 은 UBSan 이 못 잡는다** |
| `-g -O1 -fsanitize=address` | `ERROR: AddressSanitizer: global-buffer-overflow … READ of size 2 … in cal_has_code starter.c:65`, `0 bytes after global variable 'cal_codes' … of size 8` |
| VM clang 21.1.8 `-fsanitize=type` (TypeSanitizer) | `ERROR: TypeSanitizer: type-aliasing-violation … WRITE of size 4 … with type float accesses an existing object of type int … in pack_float_word starter.c:54:13`. 이 맥에선 `libclang_rt.tysan_osx_dynamic.dylib` 이 없어 링크 실패 |

두 가지 함정이 더 있다:

- **새니타이저 빌드는 버그를 숨길 수 있다.** UBSan `-O1` 빌드는 3건을 보고하면서도 `16/16 passed` 를 찍었다 — 검사 코드가 들어가면서 최적화기가 UB 를 이용하지 못했기 때문. 그래서 check.sh 는 새니타이저 결과를 "보고 0건" 으로 판정하고, 평소 빌드는 따로 6개 레벨에서 돌린다.
- **`-O0` 도 안전하지 않다 — GCC.** VM 의 gcc 15.2 는 `-O0` 에서 이미 `range_ok` 2건을 틀렸다(`-O0: 14/16`, `-Og: 14/16`, `-O2/-O3: 8/16`, `-Os/-Oz: 10/16`). GCC 는 최적화 레벨과 무관하게 앞단(fold)에서 `a + b < a` 같은 식을 단순화한다. 그리고 VM 의 clang 은 `-O1` 에서 Mac 보다 하나 더 틀렸다(`slot = 0`, 9/16) — UB 의 결과는 컴파일러·버전·플랫폼마다 다르다.

### Part C — 펌웨어 빌드에서 레벨 고르기

함수별 바이트 (`demo_bench.sh` 의 `nm --print-size` 부분, 실제 출력):

```text
# clang thumbv7em .o
-O1  bench_run=310 crc32_sw=64 fir_q15=104 isort_i16=74
-O2  bench_run=1388 crc32_sw=362 fir_q15=274 isort_i16=804
-O3  bench_run=1460 crc32_sw=362 fir_q15=348 isort_i16=912
-Os  bench_run=296 crc32_sw=60 fir_q15=98 isort_i16=72
-Oz  bench_run=160 crc32_sw=52 fir_q15=78 isort_i16=52
# arm-none-eabi-gcc Cortex-M4 .o
-O2  bench_run=272 crc32_sw=60 fir_q15=102 isort_i16=62
-O3  bench_run=528 crc32_sw=116 fir_q15=102 isort_i16=62
-Os  bench_run=164 crc32_sw=48 fir_q15=68 isort_i16=44
-Og  bench_run=168 crc32_sw=64 fir_q15=74 isort_i16=54 lcg=24
```

- clang `-O2` 의 `bench_run` 1388 B 는 세 커널을 인라인으로 흡수한 크기다(그래서 커널 함수들도 별도로 남아 있어 **이중**으로 든다). `crc32_sw` 64 → 362 B 는 안쪽 8회 루프 펼치기. 어떤 변환이 일어났는지 이유까지 보려면 `-Rpass=loop-unroll` 등(TOOL-A10), asm 읽기는 ARM-A11.
- Cortex-M 은 Flash 에서 명령을 가져온다. 고속 클럭에서는 Flash wait state 가 있어(예: STM32F4 168 MHz 에서 5 WS — 칩 RM 확인), 펼친 큰 코드가 오히려 프리페치/캐시(ART 가속기 등)를 덜 타 이득이 줄 수 있다. 그래서 많은 MCU 프로젝트의 기본은 **`-Os` (또는 `-O2`) + `-g`**, 그리고 측정해서 핫 루프만 `__attribute__((optimize("O3")))` (GCC) 나 별도 파일 `-O2`. "-O3 가 빠르다" 는 측정 전엔 가설이다.
- Flash 가 빠듯하면 `-Os` → `-Oz` 다음에 `-ffunction-sections -fdata-sections -Wl,--gc-sections`, LTO, newlib-nano — TOOL-M13. 링크 후 섹션 크기 읽기는 TOOL-B06.

UB 는 펌웨어 타깃에서도 똑같이 사라진다 (`demo_ub.sh vm`, `arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -O2 -c starter.c`, 실제 출력):

```text
00000018 <frames_until_wrap>:              0000003c <cal_has_code>:
  18:	cmp	r0, #0                          3c:	movs	r0, #1
  1a:	ite	gt                              3e:	bx	lr
  1c:	bicgt.w	r0, r1, r1, asr #31
  20:	movle	r0, #0
  22:	bx	lr
```

`range_ok` 도 `add r0, r1; cmp r0, r2` 만 남았다(검사 삭제). 호스트에서 본 버그가 그대로 칩에 간다 — 그래서 **호스트에서 레벨 매트릭스 + 새니타이저로 잡는 것**이 펌웨어 팀의 현실적인 방어선이다.

`-g` 는 코드를 바꾸지 않는다 (실측): Mac clang `-O0`/`-O2` 에서 `-g` 유무에 따른 `objdump -d` 출력이 동일, VM `arm-none-eabi-gcc` `-O0/-O2/-Os` 에서 `.text` 바이트(`objcopy -O binary -j .text` 후 `cmp`)가 동일했다. 달라진 건 `.o` 파일 크기뿐(`-O2`: 1700 → 8864 B, 디버그 섹션). 펌웨어 이미지(`.bin`/`.hex`)에는 디버그 섹션이 안 들어가므로 **릴리스도 `-g` 로 빌드해 ELF 를 보관**하고, 크래시 주소를 그 ELF 로 해석한다(TOOL-M14).

CI 관점 정리 — check.sh 가 하는 일이 그대로 CI 잡이 된다:

| 단계 | 플래그 | 목적 |
|---|---|---|
| 출하 빌드 | `-Os -g` (또는 `-O2 -g`) + `-Werror` | Flash/RAM 예산 확인 (TOOL-B06) |
| 레벨 매트릭스 | 같은 유닛 테스트를 `-O0` 와 출하 레벨(+`-O3`)로 | 결과가 다르면 UB 의심 |
| 새니타이저 | `-O1 -g -fsanitize=address,undefined` (호스트) | UB·메모리 버그를 "증상" 이 아니라 "원인" 으로 보고 (TOOL-B11, TOOL-B12) |
| 하드닝 선택 | `-fno-strict-aliasing`, `-fwrapv` 는 **정책**으로만 | 기존 코드 보호용. 새 코드를 이것에 기대지 않는다 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
bash check.sh solution    # 14/14 passed
```

## 7. 자주 하는 실수

**1. "-O0 에서 테스트가 다 통과했으니 코드는 맞다."** `starter.c` 는 clang `-O0` 에서 16/16 이었다. GCC 는 `-O0` 에서조차 오버플로 검사를 접었다. → 출하 레벨로 테스트하고, 레벨 매트릭스 + UBSan 을 CI 에 둔다.

**2. 오버플로를 일으킨 뒤 검사한다.**

```c
if (off + len < off) return false;      /* 틀림: 덧셈이 이미 UB, 검사는 삭제됨 */
if (off > cap || len > cap - off) return false;   /* 맞음: 미리 범위로 */
if (__builtin_add_overflow(off, len, &end)) return false;   /* 맞음: GCC/Clang 내장 */
```

**3. 포인터 캐스트로 타입 퍼닝.** `*(float *)&word = v;`, `*(uint32_t *)&f` — 단순한 경우 우연히 동작해서 더 위험하다(여기서도 같은 포인터 하나로 쓰면 맞게 나왔고, 두 포인터로 나뉘자 틀렸다). → `memcpy` (C/C++ 모두), C 에선 union 읽기도 허용(C11 6.5.2.3 각주 95 — 단 포인터로 멤버를 따로 넘기면 보장 안 됨, 확인 권장). 리눅스 커널처럼 `-fno-strict-aliasing` 을 전역 정책으로 쓰는 곳도 있다.

**4. 루프 경계 `<=`.** `for (i = 0; i <= N; i++) a[i]` — 마지막 반복이 UB 라서 컴파일러가 **루프 전체**를 재해석할 수 있다(여기선 `return true`). → `i < sizeof a / sizeof a[0]`, 또는 `ARRAY_SIZE` 매크로.

**5. for 루프 지연 + volatile 잘못 쓰기.** `for (i = 0; i < n; i++);` 은 `-O1` 부터 `ret` 한 줄이 된다. 결과만 `volatile` 변수에 저장해도(`g_sink = i`) 컴파일러는 `i == n` 을 계산해 저장만 하고 루프를 지운다(추가 연습 1). → 하드웨어 타이머(SysTick/DWT, BM-B13 · BM-A10), 꼭 필요하면 `volatile` 카운터.

**6. 최적화 빌드 디버깅이 귀찮아 전체를 `-O0` 로 바꾼다.** 타이밍과 UB 가 바뀌어 버그가 사라지고(Heisenbug), Flash 가 넘칠 수도 있다(-O0 thumb 678 B vs -Os 526 B — 큰 프로젝트에선 차이가 더 벌어지는 경우가 많다, 확인 권장). → 의심 함수만 `optnone`/`optimize("O0")`, 파일 단위 `-Og`, 레지스터·디스어셈블 보기.

**7. 시간 한 번 재고 "-O3 가 12% 빠르다".** 이 맥에서 같은 바이너리 중앙값이 실행마다 15% 넘게 흔들렸다. → 여러 번, 중앙값/최솟값, 같은 조건(부하·클럭)에서. MCU 에선 DWT `CYCCNT` 로 사이클을 센다(ARM-M03, BM-A10).

## 8. 면접 Q&A

<details>
<summary>Q1. -O0, -O1, -O2, -O3, -Os, -Oz, -Og 는 각각 무엇이고, 펌웨어 기본은 뭘 쓰나요?</summary>

`-O0`: 최적화 없음, 모든 변수 스택, 문장별 번역 — 디버깅 최상, 크고 느림(이 벤치 3~4배 느림). `-O1`: 레지스터 할당·상수 전파·죽은 코드 제거 등 기본. `-O2`: 대부분의 최적화(인라이닝 확대, 루프 최적화, clang 은 벡터화 포함) — 일반 릴리스 기본. `-O3`: 더 공격적인 인라이닝·펼치기·벡터화, 코드 증가. `-Os`: `-O2` 에서 크기를 키우는 변환을 뺀 것. `-Oz`: 속도를 더 희생해 크기 최소(clang; GCC 는 12부터 지원으로 기억 — 확인 권장). `-Og`: 디버깅을 해치지 않는 최적화(GCC), clang 21 은 `-O1` 급 + `-fextend-variable-liveness=all`(실측). 펌웨어는 보통 **`-Os -g`** 또는 `-O2 -g`; 이 측정에서 Cortex-M4 `.text` 는 gcc `-Os` 356 B vs `-O2` 532 B vs `-O3` 812 B, 속도는 호스트에서 -O1 이상끼리 잡음 수준이었다. 결정은 Flash 예산과 핫 루프 측정으로.

</details>

<details>
<summary>Q2. 디버그 빌드는 되는데 릴리스 빌드만 이상합니다. 어떻게 접근하나요?</summary>

1순위는 UB 와 데이터 레이스, 2순위는 타이밍(최적화로 빨라져 레이스/하드웨어 대기가 드러남), 3순위는 진짜 컴파일러 버그(드묾). 순서: (1) 같은 테스트를 레벨별로 돌려 언제부터 깨지는지 — 이 레슨의 check.sh. (2) `-fsanitize=undefined,address` 호스트 빌드(이 레슨에선 3건 + ASan 1건으로 바로 위치가 나왔다). aliasing 은 UBSan 이 못 잡으니 `-fno-strict-aliasing` 으로 증상이 사라지는지 보거나 TypeSanitizer(Linux clang 20+). (3) 실패 함수의 `-O0` vs `-O2` asm 비교. (4) 동시성이면 TSan(TOOL-M09), `volatile`/배리어 누락 확인(C-B09, C-A02). 원인을 고치지 않고 해당 파일만 `-O0` 로 빌드하는 건 미봉책.

</details>

<details>
<summary>Q3. -O2 에서 변수가 &lt;optimized out&gt; 으로 나옵니다. 왜 그렇고 어떻게 디버깅하나요?</summary>

최적화된 코드에서 변수는 고정된 메모리 칸이 아니라 구간별로 다른 레지스터에 살거나, 상수로 접히거나, 마지막 사용 후 레지스터가 재사용된다. DWARF 위치 목록에 현재 PC 가 포함되지 않으면 gdb 는 `<optimized out>`, lldb 는 `<variable not available>` 을 보인다(둘 다 실측). 대응: 디스어셈블과 레지스터로 직접 추적(값이 어느 레지스터에서 계산되는지), 필요한 함수만 `optnone` / `optimize("O0")` 또는 그 파일만 `-Og`, 확인하고 싶은 값을 `volatile` 지역에 복사, printf/트레이스. 인자는 gdb 가 `x@entry` 로 호출 시점 값을 복원하기도 한다(`DW_OP_entry_value`).

</details>

<details>
<summary>Q4. -O3 가 -O2 보다 항상 빠른가요? Cortex-M 에서 -O3 의 단점은?</summary>

아니다. 이 벤치에서 Mac 의 -O2/-O3 중앙값은 실행마다 순위가 바뀌었고(24.9~31.3 ms 범위), 코드는 clang thumb 2832 → 3084 B 로 커졌다. 펼치기·인라이닝은 명령 캐시/Flash 프리페치 효율을 떨어뜨리고 레지스터 압박으로 스필을 늘릴 수 있다. Cortex-M 에선 Flash wait state, 작은(또는 없는) I-캐시, 코드 크기 한도 때문에 `-Os` 가 실측상 비슷하거나 더 나은 경우가 흔하다. 원칙: 기본은 `-Os`/`-O2`, 프로파일에서 핫스팟으로 확인된 함수만 높인다. DSP 루프처럼 SIMD(Cortex-M4 의 `SMLAD` 등)를 쓰는 곳은 CMSIS-DSP 처럼 손으로 최적화된 라이브러리를 쓰는 것이 일반적.

</details>

<details>
<summary>Q5. if (a + b &lt; a) 오버플로 검사가 릴리스에서 사라졌습니다. 왜죠? 올바른 검사는?</summary>

부호 있는 정수 오버플로는 UB 다(C11 6.5p5). 컴파일러는 UB 가 없다고 가정하므로 b ≥ 0 이면 a + b ≥ a 가 수학적으로 참 → 비교를 상수 false 로 접는다(실측: `range_ok` 의 `-O2` asm 에 해당 비교가 없음). 올바른 검사: 연산 **전에** 범위를 확인(`b > 0 && a > INT_MAX - b`), `__builtin_add_overflow`, 또는 C23 `<stdckdint.h>` 의 `ckd_add`. 부호 없는 정수는 래핑이 정의돼 있으므로(6.2.5p9) `uint32_t` 시퀀스 번호/틱은 `(uint32_t)(now - start)` 로 비교하는 게 정석(BM-B13). `-fwrapv` 는 기존 코드 보호용 정책.

</details>

<details>
<summary>Q6. strict aliasing 이 뭔가요? 리눅스 커널은 왜 -fno-strict-aliasing 으로 빌드하나요?</summary>

C11 6.5p7: 객체는 그 유효 타입(또는 부호 변형, 한정자 추가, 이를 포함한 aggregate/union, 문자 타입)으로만 접근해야 한다. 그래서 컴파일러는 `float *` 저장이 `uint32_t` 객체를 바꾸지 못한다고 가정해 로드를 재사용·재배치한다(TBAA — `pack_float_word` 가 `-O1` 부터 0 반환). 커널은 레지스터/패킷 버퍼를 여러 타입 포인터로 다루는 코드가 많아 이 가정을 끄는 쪽을 택했다(Makefile 의 `-fno-strict-aliasing`, 확인 권장). 대가는 일부 최적화 손실. 표준적인 퍼닝은 `memcpy`(최적화되면 비용 0, 실측 `str s0` + `fmov w0, s0`) 또는 `char`/`uint8_t` 포인터로 바이트 접근. 자세히 C-M13.

</details>

<details>
<summary>Q7. for 루프로 짠 지연 함수가 릴리스에서 안 기다립니다. volatile 을 붙이면 되나요?</summary>

빈 루프는 관찰 가능한 동작이 없어서 `-O1` 부터 통째로 삭제된다(실측 `_delay_plain: ret`, Cortex-M4 `bx lr`). 결과만 volatile 변수에 넣으면 컴파일러가 최종값 `n` 만 계산해 저장한다(`str w0, [x8]; ret`) — 잘못된 volatile. **카운터 자체**를 `volatile` 로 하면 매 반복 load/store 가 남아 루프는 살지만, 반복당 시간이 레벨·코드 배치·CPU 에 따라 바뀐다(실측: 같은 volatile 루프가 `-O2` 0.25 ns, `-O3` 약 0.75 ns, `-Oz` 약 1.7 ns/반복). 그래서 정답은 하드웨어 타이머(SysTick, DWT `CYCCNT`, 범용 타이머) 기반 `delay_us` 이고, 레지스터 폴링은 `volatile` 포인터(C-M01)로 한다. volatile 은 "접근을 지우거나 합치지 마라" 일 뿐 원자성·순서(다른 코어/DMA 에 대한)는 보장하지 않는다(C-B09, C-A02).

</details>

<details>
<summary>Q8. -g 를 붙이면 코드가 느려지거나 커지나요? 릴리스에 -g 를 넣어도 되나요?</summary>

GCC/Clang 에서 `-g` 는 생성 코드를 바꾸지 않는 것이 원칙이다(실측: Mac clang `-O0`/`-O2` 디스어셈블 동일, arm-none-eabi-gcc `-O0/-O2/-Os` `.text` 바이트 동일). 늘어나는 건 `.debug_*` 섹션(`.o` 1700 → 8864 B)뿐이고 이것은 로드되지 않는다 — 플래시에 굽는 `.bin`/`.hex` 에는 없다. 그래서 릴리스도 `-g` 로 빌드해 ELF 를 아카이브하고, 필드 크래시의 PC/LR 을 `addr2line` 으로 해석한다(TOOL-M14, TOOL-A05). 배포 바이너리에서 심볼을 빼고 싶으면 `strip`/`objcopy --only-keep-debug` 로 분리.

</details>

## 9. 추가 연습

### 연습 1 — volatile 을 잘못 쓴 지연 vs 올바른 지연 (-O2 실측)

`demo_delay.c` 의 4가지 지연(`delay_plain`, 결과만 volatile 인 `delay_sink`, volatile 카운터 `delay_vcounter`, 빈 `asm volatile` 이 든 `delay_asm`)을 `-O2` 에서 디스어셈블하고 반복당 시간을 재라. `practice.c` 의 `delay_spin(n)` 은 `-O2`/`-Oz` 에서도 루프가 남아야 한다(check.sh 가 objdump 로 역방향 분기를 센다).

<details>
<summary>답 보기</summary>

`bash demo_delay.sh` 의 `-O2` 디스어셈블(실제): `_delay_plain: ret` / `_delay_sink: adrp x8, _g_sink; add; str w0, [x8]; ret` (루프 없음 — 결과만 volatile 은 오용) / `_delay_vcounter`: `ldr w8,[sp,#0xc]; cmp w8,w0; b.hs …; ldr; add; str; b` (매 반복 load/store) / `_delay_asm: cbz w0; subs w0, w0, #1; b.ne` (레지스터 카운터, 빈 asm 은 명령 0개지만 "지울 수 없는 문장" 이라 루프 유지). Cortex-M4 (`clang --target=thumbv7em-none-eabi -O2 -S`): `delay_plain: bx lr`, `delay_sink: movw/movt r1, g_sink; str r0, [r1]; bx lr`. `practice.c` 는 volatile 카운터를 택했고 check.sh 결과는 `delay_spin keeps its loop at -O2 (1 backward branch)`, `-Oz` 도 동일. 타이밍 검사는 부하에 흔들리지 않게 **하한**만 본다: 160만 번 volatile 반복이 100 µs 이상(실측 최솟값 약 0.25 ns/반복 → 약 400 µs, 루프가 지워지면 ≈ 0) 이고 20만 번보다 오래 걸릴 것. 부하는 실행을 느리게만 하므로 이 하한은 거짓 실패가 나지 않는다(처음엔 "8배 반복이 3배 이상" 비율로 검사했다가 다른 작업이 함께 돈 실행에서 2.8배가 나와 실패했다).

</details>

### 연습 2 — 타이머 기반 delay_us

`delay_us(us)` 를 단조 시계(monotonic clock) 데드라인으로 구현하라. 펌웨어라면 `DWT->CYCCNT` 나 SysTick 을 읽는 자리다. 어떤 레벨에서도 2000 µs 이상 기다려야 한다.

<details>
<summary>답 보기</summary>

`start = now_ns(); while (now_ns() - start < us * 1000) {}` — 시계 읽기는 외부 함수 호출(`clock_gettime`)이라 최적화기가 지울 수 없고, 반복당 비용이 바뀌어도 **종료 조건이 시간**이므로 길이가 일정하다. 부호 없는 뺄셈(`now - start`)이라 카운터가 넘어가도 맞는다 — 32비트 `CYCCNT` 는 168 MHz 에서 약 25.6초마다 랩한다(2^32/168e6, BM-B13 · BM-A10). 실측(stderr): 모든 레벨에서 `delay_us(2000): 2000000 ns` 근처(부하가 걸린 한 번은 10.5 ms — 하한만 보장되고 상한은 스케줄링에 달림, RTOS 에서도 같다). 긴 지연은 바쁜 대기 대신 RTOS `vTaskDelay`/타이머 인터럽트로 CPU 를 양보한다.

</details>

### 연습 3 — 오버플로 없는 중간값

`mid_i32(lo, hi)` (lo ≤ hi) 를 `(lo + hi) / 2` 없이, 어떤 int32 값에서도 UB 없이 바닥(floor) 중간값을 내게 하라. `mid_i32(INT32_MIN, INT32_MAX) == -1`.

<details>
<summary>답 보기</summary>

`uint32_t d = (uint32_t)hi - (uint32_t)lo;` (부호 없는 뺄셈은 래핑이 정의돼 있고, lo ≤ hi 이므로 결과는 정확한 차 0..2^32-1) → `return lo + (int32_t)(d / 2u);` (d/2 < 2^31 이라 int32 로 표현 가능, lo + d/2 ≤ hi 라 오버플로 없음). `lo + (hi - lo) / 2` 는 lo 가 음수이고 hi 가 크면 `hi - lo` 가 넘친다. 이진 탐색의 고전 버그(Java `binarySearch` 2006년 사례)와 같은 모양. `(int32_t)((int64_t)lo + hi) / 2` 는 0 쪽으로 자르므로 음수에서 floor 가 아니다.

</details>

### 연습 4 — 포화 덧셈

ADC 누적이나 오디오 믹싱처럼 넘치면 최대/최소에 붙어야 하는 `sat_add_i32(a, b)` 를 UB 없이 구현하라.

<details>
<summary>답 보기</summary>

`int32_t r; if (__builtin_add_overflow(a, b, &r)) return b > 0 ? INT32_MAX : INT32_MIN; return r;` — 내장 함수는 무한 정밀도로 더한 결과가 r 의 타입에 들어가는지 알려주고 r 에는 래핑된 값을 쓴다. 넘칠 수 있는 방향은 b 의 부호가 결정한다. arm64 에서는 `adds` + 플래그 분기로, Cortex-M4 는 DSP 확장 `QADD` 명령이 이 일을 한 명령에 한다(CMSIS `__QADD`). 테스트: `INT32_MAX - 5 + 100 → INT32_MAX`, `INT32_MIN + 5 + (-100) → INT32_MIN`, `INT32_MIN + INT32_MAX → -1`.

</details>

### 연습 5 — (관찰) 같은 volatile 루프, 레벨마다 다른 속도

`bash demo_delay.sh` 의 표를 보고 "for 루프 지연을 -O0 에서 보정(calibrate)해 두면 릴리스에서 어떻게 되나" 를 답하라.

<details>
<summary>답 보기</summary>

실측(ns/반복, 3회 통과 중 하나, 맥 부하 있음):

```text
level    plain     sink   vcounter      asm
-O0      0.737    0.456      0.495    0.552
-O1      0.000    0.000      0.244    0.240
-O2      0.000    0.000      0.252    0.250
-O3      0.000    0.000      0.851    0.271
-Os      0.000    0.000      0.252    0.229
-Oz      0.000    0.000      1.669    0.251
```

`plain`/`sink` 는 -O1 부터 0 — 지연이 사라진다. `vcounter` 는 -O3 (0.67~0.85) 와 -Oz (1.67~1.84) 가 세 번 모두 -O2 (0.24~0.26) 보다 3~7배 느렸다. 디스어셈블을 보면 같은 load/add/store 인데 -O3 는 루프를 회전(rotate)해 store 직후 같은 주소를 load 하고, -Oz 는 초기화 store 를 루프 머리에 공유한다 — 같은 명령 수라도 배치에 따라 이 코어의 store→load 경로 비용이 다르다는 추정이다(원인은 확인 권장). 즉 -O0 에서 맞춘 지연은 -O2 에서 2배 이상 짧아지고, -Oz 로 바꾸면 다시 길어진다. 결론: 시간은 타이머로.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
bash check.sh practice    # 16/16 passed (6 레벨 × 빌드/실행, 동일 출력, ASan+UBSan, -O2/-Oz 루프 유지)
```

## 10. 핵심 정리

- 레벨 = 패스 묶음. 이 벤치 실측: `-O0` 은 3~4배 느리고, `-O1` 이상끼리는 잡음 수준. 크기는 clang thumb 344 B(`-Oz`) ~ 3084 B(`-O3`), gcc Cortex-M4 356 B(`-Os`) ~ 812 B(`-O3`). 펌웨어 기본은 `-Os -g`(또는 `-O2 -g`), 높은 레벨은 측정된 핫스팟에만.
- 크기 읽기: Mac `objdump -h`(`__text`) / `size -m`, ELF `llvm-size -A` / `arm-none-eabi-size`(Berkeley `text` 에 .rodata 포함), 함수별 `nm --print-size`. 최적화는 RAM 도 바꾼다(clang 이 읽히지 않는 `g_out` 4 KB 삭제).
- 최적화 빌드에서 변수는 구간별 레지스터에 산다 → 위치 목록 밖이면 lldb `<variable not available>`, gdb `<optimized out>`. clang 21 `-Og` = `-O1` 급 + `-fextend-variable-liveness=all`(그래도 다 보이진 않음). `-g` 는 코드를 바꾸지 않는다.
- UB 가 있으면 "맞는 번역" 이 여러 개다: 오버플로 검사 삭제, 래핑 못 보는 루프, TBAA 로 0 반환, 배열 밖 루프가 `return true`. `-O0` 의 정답은 우연이고, GCC 는 `-O0` 에서도 접는다.
- 고치는 법: 연산 **전** 범위 검사 / `__builtin_add_overflow`, 래핑 직전 `break`, `memcpy` 퍼닝, `sizeof a / sizeof a[0]` 경계. 검증: 6개 레벨 동일 출력 + ASan/UBSan(aliasing 은 TySan, Linux).
- 지연은 타이머로. 빈 루프와 "결과만 volatile" 은 사라지고, volatile 카운터는 레벨마다 속도가 3~7배 다르다.

## 참고자료

- GCC Manual — Optimize Options (`-O0…-Oz`, `-Og`, `-fstrict-aliasing`, `-fwrapv`, `-fstrict-overflow`): <https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html>
- Clang Command Line Reference / Users Manual — `-O` 레벨, `-Og`, `-fextend-variable-liveness`: <https://clang.llvm.org/docs/ClangCommandLineReference.html>, <https://clang.llvm.org/docs/UsersManual.html>
- C11 (N1570): 6.5p5 (예외 조건 = UB), 6.5p7 (유효 타입·앨리어싱), 6.2.5p9 (부호 없는 래핑), 6.5.6p8 (배열 밖 포인터 연산), 6.5.2.3 각주 95 (union 퍼닝): <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Chris Lattner, "What Every C Programmer Should Know About Undefined Behavior" (LLVM Blog, 2011): <https://blog.llvm.org/2011/05/what-every-c-programmer-should-know.html>
- John Regehr, "A Guide to Undefined Behavior in C and C++": <https://blog.regehr.org/archives/213>
- Raymond Chen, "Undefined behavior can result in time travel" (배열 밖 루프가 `return true` 가 되는 예): <https://devblogs.microsoft.com/oldnewthing/20140627-00/?p=633>
- Clang UBSan / ASan / TypeSanitizer 문서: <https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html>, <https://clang.llvm.org/docs/AddressSanitizer.html>, <https://clang.llvm.org/docs/TypeSanitizer.html>
- DWARF 5 표준 — location lists, `DW_OP_entry_value`: <https://dwarfstd.org/dwarf5std.html>
- LLDB 튜토리얼 / GDB Manual (Optimized Code): <https://lldb.llvm.org/use/tutorial.html>, <https://sourceware.org/gdb/current/onlinedocs/gdb.html/Optimized-Code.html>
- Arm — Cortex-M4 DWT CYCCNT (ARMv7-M ARM, C1.8): <https://developer.arm.com/documentation/ddi0403/latest/>
- 관련 레슨: C-M12 (UB 카탈로그), C-M13 (strict aliasing), C-B09 (volatile), C-M01 (레지스터 접근), C-A02 (배리어), ARM-B02 · ARM-A11 (asm 읽기, 벡터화/펼치기), ARM-M03 (마이크로벤치), BM-B13 (SysTick 지연), BM-A10 (DWT CYCCNT), TOOL-B06 (size), TOOL-B10 (디버거), TOOL-B11 · TOOL-B12 (ASan · UBSan), TOOL-M09 (TSan), TOOL-M13 (크기 다이어트), TOOL-M14 (DWARF), TOOL-A10 (-Rpass)
