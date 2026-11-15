---
id: ARM-B09
time: 60분
updated: 2026-11-11
---
# ARM-B09 lldb로 명령어 단위 실행과 레지스터 관찰

## 학습 목표

- lldb 로 어셈블리 함수를 **명령어 한 개씩** 실행한다: `si`/`ni` 차이(특히 `bl` 위에서), `register read`, `memory read`, `disassemble`, `finish`.
- `cpsr` 값(예: `0x60000000`)을 보고 NZCV 를 바로 읽고, 어떤 명령이 플래그를 바꿨는지 말할 수 있다.
- "테스트가 거의 다 통과하는" off-by-one 을 **스텝 표(pc·명령·x0·x1·x2·플래그)** 로 찾아낸다. 가드 페이지와 read watchpoint 로 "NUL 다음 1 바이트를 읽는다" 를 눈에 보이게 만든다.
- 크래시를 덤프처럼 읽는다: 멈춘 `pc`, `lr`, `far`, `esr` 로 원인 명령과 주소를 찾는다. `register write` 로 가설을 재빌드 없이 확인한다.
- 같은 작업을 gdb 명령으로 옮길 수 있다 (Cortex-M + QEMU/OpenOCD 원격 디버깅 대비).

## 1. 문제

"이 크래시 덤프에서 뭘 보죠? `pc` 가 이 명령을 가리키고 `x1` 이 이 값이면?" 펌웨어 면접의 디버깅 라운드는 결국 **레지스터와 메모리를 보고 원인을 말하는 능력**을 본다. Don 이 SSD 컨트롤러(ARM 코어)에서 JTAG/T32 로 하던 일 — 멈추고, 한 명령 진행하고, 레지스터 보고, 메모리 덤프 보고, 값 바꿔서 다시 돌려 보기 — 을 이 맥(Apple M6, AArch64)의 lldb 로 똑같이 한다. Cortex-M 의 HardFault 분석(ARM-M12, ARM-M14)과 QEMU gdb 원격 디버깅(TOOL-A03)도 같은 손놀림이다.

재료는 **일부러 버그를 심은 어셈블리 `my_strlen`** 이다. 테스트 1172 개 중 1154 개는 통과한다. 틀리는 건 한 길이(빈 문자열)뿐이고, 문자열이 페이지 끝에 있으면 프로세스가 죽는다. 코드를 눈으로 읽어서 찾아도 되지만, 이 레슨의 목표는 **디버거에서 레지스터 값으로 증거를 잡는 것**이다.

```svg
<svg viewBox="0 0 740 285" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">빈 문자열 "" 을 페이지 끝에 두면: 버그 판의 첫 ldrb 는 NUL 을 건너뛰고 가드 페이지를 읽는다</text>
  <rect x="40" y="70" width="400" height="44" class="f-ok" stroke="currentColor"/>
  <rect x="440" y="70" width="280" height="44" class="f-warn" stroke="currentColor"/>
  <text x="50" y="62" fill="currentColor">RW 페이지 (16 KB)</text>
  <text x="450" y="62" fill="currentColor">가드 페이지 (PROT_NONE)</text>
  <g stroke="currentColor"><line x1="140" y1="70" x2="140" y2="114"/><line x1="240" y1="70" x2="240" y2="114"/><line x1="340" y1="70" x2="340" y2="114"/><line x1="540" y1="70" x2="540" y2="114"/><line x1="640" y1="70" x2="640" y2="114"/></g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="90" y="97">'x'</text><text x="190" y="97">'x'</text><text x="290" y="97">'x'</text><text x="390" y="97">00</text>
    <text x="490" y="97">??</text><text x="590" y="97">??</text>
    <text x="90" y="134" font-size="12">…7ffc</text><text x="190" y="134" font-size="12">…7ffd</text><text x="290" y="134" font-size="12">…7ffe</text>
    <text x="390" y="134" font-size="12">…7fff</text><text x="490" y="134" font-size="12">…8000</text><text x="590" y="134" font-size="12">…8001</text>
  </g>
  <path d="M390 190 L390 140" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M384 150 L390 140 L396 150" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="300" y="206" fill="currentColor" font-family="monospace">x0 = s = 0x100027fff</text>
  <path d="M490 190 L490 140" stroke="currentColor" fill="none" stroke-width="2" stroke-dasharray="5 3"/>
  <path d="M484 150 L490 140 L496 150" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="470" y="206" fill="currentColor" font-family="monospace">ldrb w2, [x1, #1]!  → x1+1</text>
  <text x="470" y="226" fill="currentColor" font-family="monospace">= 0x100028000 → EXC_BAD_ACCESS</text>
  <text x="10" y="250" fill="currentColor">버그 판: x1 = s 에서 시작 → 첫 읽기가 s[1] (NUL 너머).</text>
  <text x="10" y="270" fill="currentColor">고친 판: x1 = s − 1 에서 시작 → 첫 읽기가 s[0] = NUL → 길이 0, 가드 페이지는 안 건드림.</text>
</svg>
```

(주소는 `demo_crash.txt` 의 실제 값. Apple Silicon 의 페이지는 16 KB = `0x4000`.)

### Part A — lldb 로 한 명령씩: 버그를 레지스터 값으로 찾기

`demo_debug.c` 가 `two_lengths("abc", "")` 를 부르고, 그 안에서 `my_strlen` 을 두 번 `bl` 한다. lldb 배치 스크립트(`demo_debug.lldb`, `demo_trace.lldb`)로:

- `bl` 위에서 `ni` (함수 전체를 한 번에 넘김) 와 `si` (함수 안으로 들어감) 를 비교한다. `lr` 이 어떻게 바뀌는지 본다.
- `"abc"` 한 번을 `si` 로 `ret` 까지 따라가며 **스텝 표**(pc, 명령, x0, x1, x2, cpsr)를 만든다. "첫 `ldrb` 가 무엇을 읽었나?" 가 결정적 증거다.
- `cpsr` 의 상위 4 비트를 NZCV 로 해석한다.

### Part B — 경계와 증거: 정렬 격자, 가드 페이지, read watchpoint

- `harness.c` 가 길이 0..64 × 시작 정렬 0..15 (NUL 뒤에는 쓰레기 `'#'`), 페이지 끝 문자열(NUL = RW 페이지의 마지막 바이트), 페이지 시작 문자열(`s[-1]` 은 가드)을 시험한다. fault 는 `sigsetjmp` 로 잡아 FAIL 로 바꾼다.
- lldb **read watchpoint** 로 "NUL 다음 바이트(`$x0 + 1`)를 읽는 순간" 멈춘다.
- 버그를 한 줄로 고친다 (`starter.S` → `solution.S`).

### Part C — 크래시 덤프 읽기와 `register write` 로 가설 검증

- `./dbg crash` 는 `""` 을 가드 페이지 바로 앞에 두고 버그 판을 부른다 → `EXC_BAD_ACCESS`. 멈춘 상태에서 `pc`·`lr`·`x0`·`x1`·`far`·`esr`·`bt` 만으로 "어느 명령이, 어느 주소를, 왜" 를 말한다.
- 다시 실행해서 `my_strlen` 첫 명령 뒤에 `register write x1 $x0-1` → 크래시 없이 `len=0`. 소스를 고치기 **전에** 가설을 확인하는 방법이다 (JTAG 로 레지스터를 바꿔 보는 것과 같다).
- 같은 세션을 gdb(Lima VM, qemu-aarch64 gdbstub)로 재현하고 lldb ↔ gdb 대응표를 만든다.

## 2. Problem (English)

> **Debug an assembly `strlen` at the instruction level.**
>
> You are given an AArch64 implementation of `size_t my_strlen(const char *s);` (x0 = s, returns the length in x0; leaf function, no stack use).
>
> It passes almost every test, but returns garbage for one input class and crashes the process when the string ends at the last byte of a mapped page.
>
> 1. Using lldb only (no printf), step through the function with `si`, record `pc`, the instruction, `x0`, `x1`, `x2` and the NZCV flags after each instruction for `s = "abc"`. Identify the first instruction whose result contradicts the C contract.
> 2. Explain the difference between `si` and `ni` when `pc` sits on `bl my_strlen`, and what `lr` holds after `si`.
> 3. Write a test that turns "reads one byte past the terminator" into a deterministic failure (hint: `mmap` + `mprotect(PROT_NONE)`), and set a hardware read watchpoint that fires on that byte.
> 4. Given the crashed process (`EXC_BAD_ACCESS`), use `pc`, `lr`, `far` and `esr` to name the faulting instruction, the faulting address and the access type. Validate your fix by patching a register in the debugger before editing the source.
> 5. Fix the bug with a one-line change. Constraints: leaf function, no stack, must not read any byte outside `[s, s+len]`.
>
> Example: `my_strlen("") == 0`, `my_strlen("abc") == 3`, `my_strlen("caf\xc3\xa9") == 5`.

## 3. 시작 코드

테스트 하네스 (starter / solution 공용):

<!-- code: harness.c -->

버그가 있는 asm (TODO = 찾아서 고치기):

<!-- code: starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S -o /tmp/ARM-B09_starter && /tmp/ARM-B09_starter
```

실제 출력 (exit 1):

```text
FAIL harness.c:71  got == len  [align=0 len=0 got=112]
FAIL harness.c:71  got == len  [align=1 len=0 got=111]
FAIL harness.c:71  got == len  [align=2 len=0 got=110]
FAIL harness.c:71  got == len  [align=3 len=0 got=109]
     ... 정렬 격자: FAIL 16개 중 4개만 표시
FAIL harness.c:91  sig == 0 && got == len  [page-end len=0 got=-1 sig=10 (NUL 다음 바이트를 읽음)]
FAIL harness.c:96  sig == 0 && got == len  [page-start len=0 got=-1 sig=10]
1154/1172 passed
```

`sig=10` 은 macOS 의 `SIGBUS` 다 (Lima VM 의 Linux 에서는 같은 테스트가 `sig=11` `SIGSEGV`). `got=112` 같은 값은 NUL 뒤 쓰레기 `'#'` 를 버퍼 끝의 NUL 까지 센 결과다.

관찰용 파일 (테스트와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -g -O0 demo_debug.c starter.S  -o dbg     # 버그 판 디버그 빌드
cc -std=c11 -Wall -Wextra -Werror -g -O0 demo_debug.c solution.S -o fixed   # 고친 판
lldb -b -s demo_debug.lldb dbg      # si/ni, register/memory read, read watchpoint, finish → demo_debug.txt
lldb -b -s demo_trace.lldb dbg      # "abc" 스텝 트레이스 → demo_trace_bug.txt  (fixed 로 → demo_trace_fix.txt)
lldb -b -s demo_crash.lldb dbg      # 크래시 읽기 + register write 가설 검증 → demo_crash.txt
lldb -b -s demo_practice.lldb dbg   # 연습 5: 조건부 브레이크포인트 + stop-hook → demo_practice.txt
cc -g -O1 -fsanitize=address demo_asan_blind.c starter.S -o ab && ./ab      # ASan 은 asm 의 over-read 를 못 본다
# gdb 판 (Lima VM): demo_gdb.gdb → demo_gdb.txt (명령은 파일 머리 주석)
```

`lldb -b` 는 batch 모드다: 스크립트 명령을 차례로 실행하고 끝나면 lldb 도 끝난다. 중간 명령이 에러를 내면 거기서 멈추므로, 에러가 날 수 있는 명령은 맨 끝에 둔다.

## 4. 힌트

<details>
<summary>힌트 1 — 실패 목록을 먼저 읽어라</summary>

FAIL 은 전부 `len=0` 이다. 길이 1..64 는 모든 정렬에서 맞는다. "빈 문자열에서만" 틀리는 루프라면 **첫 반복이 무엇을 하느냐**가 의심 대상이다. 루프가 조건을 먼저 검사하는가(while), 일단 한 번 돌고 검사하는가(do-while)?

</details>

<details>
<summary>힌트 2 — "abc" 에서 첫 ldrb 뒤의 x2 를 보라</summary>

`b my_strlen` → `run` → `si` 두 번 → `register read x1 x2`. `"abc"` 의 첫 글자는 `'a'` = `0x61` 이다. x2 에 무엇이 들어 있나? `memory read -s1 -fx -c8 $x0` 로 문자열 바이트도 같이 보라.

</details>

<details>
<summary>힌트 3 — pre-index 는 "먼저 더하고 읽는다"</summary>

`ldrb w2, [x1, #1]!` 은 `x1 = x1 + 1; w2 = *x1` 이다. `ldrb w2, [x1], #1` (post-index) 는 `w2 = *x1; x1 = x1 + 1`. 어느 쪽이든 "첫 읽기가 s[0]" 이 되게 시작 포인터나 끝 계산을 맞추면 된다. 바꿀 줄은 하나면 충분하다.

</details>

<details>
<summary>힌트 4 — 크래시 덤프는 pc, 그 명령의 주소 계산, far 순서로</summary>

`pc` 가 가리키는 명령의 주소식(`[x1, #1]`)에 크래시 순간의 레지스터 값을 넣어 보라. 그 값이 `far` 와 같으면 범인이다. `x1` 이 아직 `s` 인 이유(writeback 이 안 일어난 이유)도 생각해 보라.

</details>

## 5. 풀이

### Part A — lldb 기본기: 멈추고, 한 명령, 보고

이 레슨에서 쓰는 lldb 명령 (전부 이 맥에서 실제로 실행한 것):

| 하고 싶은 일 | lldb 명령 | 메모 |
|---|---|---|
| 함수 입구에 멈춤 | `b my_strlen` | Mach-O 심볼은 `_my_strlen` 이지만 lldb 는 C 이름으로 찾는다 |
| 소스 줄에 멈춤 | `b demo_debug.c:25` | `-g` 로 빌드한 C 쪽 |
| 명령 1개 (call 안으로) | `si` = `thread step-inst` | stop reason `instruction step into` |
| 명령 1개 (call 은 통째로) | `ni` = `thread step-inst-over` | stop reason `instruction step over` |
| 함수 끝까지 | `finish` = `thread step-out` | 호출자에서 멈춤 → `x0` 이 반환값 |
| 레지스터 | `register read x0 x1 x2 cpsr` / `register read -f b cpsr` | `-s 2` 는 예외 상태(far/esr) |
| 메모리 | `memory read -s1 -fx -c8 $x0` (= gdb 식 `x/8xb $x0`) | `-s` 크기, `-f` 형식, `-c` 개수 |
| 디스어셈블 | `disassemble -n my_strlen`, `disassemble -s $pc -c 1` | `->` 가 현재 pc |
| 레지스터 쓰기 | ``register write x1 `$x0 - 1` `` | 백틱 안은 식으로 계산 |
| read watchpoint | `watchpoint set expression -w read -s 1 -- $x0 + 1` | 주소 식을 계산해서 그 1 바이트를 감시 |

#### si vs ni — `bl` 위에서 갈라진다

`demo_debug.txt` 의 실제 값. `two_lengths` 의 `-O0` 코드에는 `bl my_strlen` 이 두 번 있다 (`+24` 와 `+36`).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">pc 가 bl 위에 있을 때: ni 는 호출을 통째로 실행, si 는 호출된 함수 첫 명령에서 멈춤</text>
  <rect x="10" y="40" width="330" height="130" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="60">two_lengths:</text>
    <text x="20" y="80">+24 0x100000510  bl my_strlen   ; "abc"</text>
    <text x="20" y="100">+28 0x100000514  str x0, [sp, #8]</text>
    <text x="20" y="120">+32 0x100000518  ldr x0, [sp, #0x10]</text>
    <text x="20" y="140">+36 0x10000051c  bl my_strlen   ; ""</text>
    <text x="20" y="160">+40 0x100000520  str x0, [sp]</text>
  </g>
  <rect x="440" y="40" width="290" height="130" rx="6" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="450" y="60">my_strlen:</text>
    <text x="450" y="80">+0 0x1000006a8  mov  x1, x0</text>
    <text x="450" y="100">+4 0x1000006ac  ldrb w2, [x1, #1]!</text>
    <text x="450" y="120">   …</text>
    <text x="450" y="140">+20 0x1000006bc ret</text>
  </g>
  <path d="M330 76 C 360 76, 360 96, 335 96" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M343 90 L335 96 L345 100" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="365" y="90" fill="currentColor">ni</text>
  <path d="M330 136 L440 80" stroke="currentColor" fill="none" stroke-width="2" stroke-dasharray="5 3"/>
  <path d="M428 80 L440 80 L432 89" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="380" y="125" fill="currentColor">si</text>
  <g fill="currentColor">
    <text x="10" y="196">ni @…510 → pc=…514, x0=0x3 : my_strlen("abc") 가 통째로 실행됐다</text>
    <text x="10" y="218">si @…51c → pc=…6a8 (my_strlen+0), lr=0x100000520 (two_lengths+40) = bl 다음 주소</text>
    <text x="10" y="240">ret = br x30 : 마지막 ret 이 pc 를 이 lr 값으로 되돌린다 (스텝 표 마지막 줄)</text>
  </g>
</svg>
```

- `ni` 도 내부적으로는 그냥 "다음 명령까지 실행" 이 아니다. `bl` 이면 호출된 함수가 돌아올 때까지 실행시킨 뒤 멈춘다 (lldb 는 반환 주소에 임시 브레이크포인트를 거는 식으로 구현하는 것으로 알고 있다 — 구현 세부는 확인 권장). 그래서 `ni` 중에 호출된 함수 안의 브레이크포인트·watchpoint 는 그대로 걸린다.
- `si` 로 들어간 직후 `lr` 을 보면 "누가 나를 불렀나" 가 나온다. leaf 함수(스택 프레임 없음)는 `lr` 이 유일한 호출자 정보다. 그래서 크래시 덤프에서 `lr` 이 중요하다 (Part C).

#### 스텝 표 — "abc" 를 `si` 로 끝까지 (`demo_trace_bug.txt` / `demo_trace_fix.txt`, 실제 값)

각 줄은 **그 명령을 실행한 뒤** 값이다. 주소는 `0x1000006xx` 의 하위 3 자리, 데이터 주소는 `s = 0x100008000` 기준 오프셋으로 줄였다. 입구에서 `x1 = s+0x10` 인 건 `two_lengths` 의 두 번째 인자 `b`(= `main.empty`)가 남아 있던 것이다.

| # | pc | 실행한 명령 | x1 | x2 | cpsr | NZCV | 메모 |
|---|---|---|---|---|---|---|---|
| 0 | 6a8 | (입구) | s+0x10 | 쓰레기 | 0x60000000 | -ZC- | 이전 코드가 남긴 플래그 |
| 1 | 6a8 | `mov x1, x0` | **s** | 쓰레기 | 0x60000000 | -ZC- | |
| 2 | 6ac | `ldrb w2, [x1, #1]!` | s+1 | **0x62 'b'** | 0x60000000 | -ZC- | **'a'(0x61) 를 건너뛰었다** ← 버그 |
| 3 | 6b0 | `cmp w2, #0` | s+1 | 0x62 | 0x20000000 | --C- | 0x62 − 0: Z=0, 빌림 없음 C=1 |
| 4 | 6b4 | `b.ne 1b` | s+1 | 0x62 | 0x20000000 | --C- | Z=0 → 분기 → pc=6ac |
| 5 | 6ac | `ldrb w2, [x1, #1]!` | s+2 | 0x63 'c' | 0x20000000 | --C- | |
| 6–7 | | `cmp` / `b.ne` | s+2 | 0x63 | 0x20000000 | --C- | 분기 |
| 8 | 6ac | `ldrb w2, [x1, #1]!` | s+3 | 0x00 | 0x20000000 | --C- | NUL |
| 9 | 6b0 | `cmp w2, #0` | s+3 | 0x00 | 0x60000000 | -ZC- | 0 − 0: Z=1, C=1 |
| 10 | 6b4 | `b.ne 1b` | s+3 | 0x00 | 0x60000000 | -ZC- | Z=1 → 안 감 → pc=6b8 |
| 11 | 6b8 | `sub x0, x1, x0` | s+3 | 0x00 | 0x60000000 | -ZC- | x0 = 3 |
| 12 | 6bc | `ret` | | | | | pc = 0x100000514 = lr |

**결론**: `"abc"` 의 답 3 은 우연히 맞았다. 읽은 바이트는 `b, c, NUL` 뿐이고 `'a'` 는 한 번도 읽지 않았다. 첫 글자를 건너뛰어도 NUL 위치는 그대로라서 길이가 같다. 하지만 `""` 에서는 첫 글자가 곧 NUL 이라, 그걸 건너뛰면 NUL 너머를 읽는다. 고친 판(`demo_trace_fix.txt`)은 1번 줄에서 `x1 = 0x100007fff` (= s−1), 2번 줄에서 `x2 = 0x61 'a'` 로 시작하고, 반복이 한 번 더 돌아서 `ret` 까지 15 명령이다 (버그 판은 12 명령).

#### cpsr → NZCV

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">lldb 의 cpsr: 상위 4 비트가 NZCV (31=N, 30=Z, 29=C, 28=V)</text>
  <g stroke="currentColor">
    <rect x="160" y="40" width="50" height="34" class="f-mut"/><rect x="210" y="40" width="50" height="34" class="f-acc"/>
    <rect x="260" y="40" width="50" height="34" class="f-acc"/><rect x="310" y="40" width="50" height="34" class="f-mut"/>
    <rect x="360" y="40" width="300" height="34" class="f-mut"/>
    <rect x="160" y="110" width="50" height="34" class="f-mut"/><rect x="210" y="110" width="50" height="34" class="f-mut"/>
    <rect x="260" y="110" width="50" height="34" class="f-acc"/><rect x="310" y="110" width="50" height="34" class="f-mut"/>
    <rect x="360" y="110" width="300" height="34" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="185" y="62">N=0</text><text x="235" y="62">Z=1</text><text x="285" y="62">C=1</text><text x="335" y="62">V=0</text>
    <text x="510" y="62">27..0 = 0 (EL0, 나머지 0)</text>
    <text x="185" y="132">N=0</text><text x="235" y="132">Z=0</text><text x="285" y="132">C=1</text><text x="335" y="132">V=0</text>
    <text x="510" y="132">27..0 = 0</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="10" y="62">0x60000000</text><text x="10" y="132">0x20000000</text>
    <text x="670" y="62">-ZC-</text><text x="670" y="132">--C-</text>
  </g>
  <text x="10" y="92" fill="currentColor" font-size="12">cmp w2,#0 with w2=0 → 결과 0 (Z=1), 빌림 없음 (C=1)</text>
  <text x="10" y="162" fill="currentColor" font-size="12">cmp w2,#0 with w2=0x62 → 결과 ≠ 0 (Z=0), 빌림 없음 (C=1).  b.ne 는 Z==0 일 때 분기</text>
  <text x="10" y="188" fill="currentColor" font-size="12">gdb 는 같은 값을 [ EL=0 BTYPE=0 C Z ] 처럼 풀어서 보여 준다 (demo_gdb.txt)</text>
</svg>
```

- 16 진수 첫 자리 하나가 곧 NZCV 다: `6` = `0110` = -ZC-, `2` = `0010` = --C-, `8` = N, `9` = N+V. (`p/t $cpsr >> 28` 도 lldb 에서 동작: `0b...0010`.)
- 플래그를 바꾸는 건 `cmp` 뿐이다. `ldrb`, `mov`, `sub`(S 없음), `b.ne` 는 그대로 둔다 → 스텝 표에서 cpsr 이 바뀌는 줄은 3번과 9번뿐이다. `cbnz`/`cbz` 도 플래그를 안 쓴다 (연습 2 의 루프). 비교·조건 코드 전체는 ARM-B05, 플래그 계산은 ARCH-B02.
- **AArch64 에는 실제 CPSR 레지스터가 없다.** 아키텍처에는 NZCV 같은 PSTATE 필드가 있고, 예외가 날 때 SPSR 에 저장된다. lldb/debugserver 가 이를 AArch32 이름 `cpsr` 로 보여 줄 뿐이다 (Arm ARM 기준. lldb 가 이 이름을 쓰는 건 이 맥에서 확인함). 사용자 코드에서는 `mrs x0, nzcv` 로 읽는다 (ARCH-B02).

### Part B — 왜 len 0 만 틀리나, 그리고 그걸 "보이게" 만드는 법

#### 버그의 모양

```text
    mov   x1, x0          // p = s
1:  ldrb  w2, [x1, #1]!   // p++ ; c = *p      ← 첫 읽기가 s[1]
    cmp   w2, #0
    b.ne  1b
    sub   x0, x1, x0      // return p - s
```

C 로 옮기면 `p = s; do { c = *++p; } while (c);` 이다. 반복 k 번째(1부터)에 `s[k]` 를 읽고, `s[k] == 0` 인 첫 k 를 돌려준다. 그래서 `len ≥ 1` 이면 결과는 len 이다 (`s[1..len-1]` 은 0 이 아니고 `s[len] = 0`). `len = 0` 이면 `s[0]` 을 한 번도 안 보고 `s[1], s[2], …` 를 읽으며 **다음 0 이 나올 때까지** 달린다. 결과는 쓰레기 값이 되거나, 페이지 끝이면 fault 가 난다.

#### 가능한 수정 3가지

| 수정 | 바뀌는 곳 | 길이 | 비고 |
|---|---|---|---|
| ① 시작을 `s − 1` 로: `sub x1, x0, #1` | 1 줄 (solution.S) | 같음 | 첫 pre-index 가 `s[0]` 을 읽는다. 마지막 `sub x0, x1, x0` 는 그대로 맞다 |
| ② post-index + 끝에서 1 빼기 | 2 줄 | +1 명령 | `ldrb w2, [x1], #1` 이면 NUL 을 읽은 뒤 p 가 NUL 다음 → `p − s − 1` (연습 2) |
| ③ while 형태 (읽고 검사, 그다음 증가) | 구조 변경 | +1~2 | 가장 읽기 쉽다. 연습 1 의 `span_printable` 모양 |

① 은 C 로 쓰면 `p = s - 1` 이다. `s` 가 배열 첫 원소를 가리키면 **C 에서는 UB** 다 (C11 6.5.6p8: 포인터 산술은 배열 안 또는 끝 다음 1 칸까지만 정의된다). 어셈블리에는 그런 규칙이 없다. `x1` 은 그냥 64 비트 정수이고, `s − 1` 주소로 **메모리 접근만 안 하면** 아무 문제가 없다. 첫 `ldrb [x1, #1]!` 이 접근하는 주소는 `s` 다. 하네스의 "페이지 시작" 테스트(`s[-1]` 이 가드 페이지)가 이걸 확인한다. ①은 `s−1` 을 읽지 않으므로 fault 가 없다. 이것이 "C 추상 기계의 규칙"과 "ISA 가 정의하는 동작"의 차이다.

(`s = 0` 이면 `s − 1 = 0xFFFF…FFFF` 이고 `[x1, #1]!` 은 다시 0 을 읽는다. 어차피 NULL 은 계약 밖이다.)

#### 가드 페이지: "한 바이트 더 읽기" 를 결정적 실패로

일반 버퍼에서는 NUL 다음 바이트를 읽어도 **아무 일도 안 일어난다**. 그 메모리는 읽을 수 있고, 값이 우연히 0 이면 결과까지 맞는다. 그래서 harness 는:

1. `mmap(3 페이지)` → 양 끝 페이지를 `mprotect(PROT_NONE)`. 가운데 RW 페이지가 `[page, page+16384)`.
2. 페이지 끝 테스트: `s = page + pg − 1 − len`, `s[len] = 0` → NUL 이 RW 페이지의 **마지막 바이트**. 한 바이트라도 더 읽으면 가드 페이지 → `SIGBUS`(macOS) / `SIGSEGV`(Linux).
3. 페이지 시작 테스트: `s = page` → `s[-1]` 을 읽으면 앞 가드에서 fault.
4. 시그널 핸들러가 `siglongjmp` 로 돌아와 FAIL 로 기록한다 (`guarded_strlen`). `sigsetjmp` 와 `siglongjmp` 사이에서 바뀌는 지역 변수는 `volatile` 이어야 값이 보존된다 (C11 7.13.2.1).

이것이 Electric Fence / `libgmalloc`(macOS) / ASan 의 오른쪽 redzone 과 같은 아이디어다. 다만 **ASan 은 손으로 쓴 asm 을 계측하지 못한다** (`demo_asan_blind.c`, 실제 출력):

```text
$ cc -g -O1 -fsanitize=address demo_asan_blind.c starter.S -o ab && ./ab
my_strlen("") = 1                                  ← 1 바이트 malloc 너머를 읽었지만 ASan 은 조용함
$ cc -g -O0 -fsanitize=address -DUSE_C demo_asan_blind.c -o abc && ./abc
==25713==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000991 ...
READ of size 1 at 0x602000000991 thread T0
    #0 0x000104fd091c in my_strlen demo_asan_blind.c:13
```

ASan 은 **컴파일러가 C 의 load/store 앞에 검사 코드를 끼워 넣는 방식**이다. `.S` 파일의 `ldrb` 는 컴파일러를 거치지 않으므로 검사 코드가 없다. (덤: 같은 버그 C 코드를 `-O1` 로 빌드하면 clang 이 루프를 `strlen(s+1)+1` 호출로 바꿔 버린다. 그때는 ASan 의 `strlen` 인터셉터가 대신 잡는다. 실제로 확인했다.) 그래서 asm 루틴은 가드 페이지 테스트가 사실상 유일한 자동 검출 수단이다. 실무에서 SSD 펌웨어의 손 asm memcpy 를 MPU 영역 경계에 대고 시험하는 것과 같은 발상이다.

#### read watchpoint — 읽는 순간 멈춘다

`demo_debug.txt` (`""` 호출의 `my_strlen` 입구에서):

```text
(lldb) memory read -s1 -fx -c8 $x0
0x100008010: 0x00 0x5a 0x65 0x62 0x72 0x61 0x00 0x00
(lldb) watchpoint set expression -w read -s 1 -- $x0 + 1
Watchpoint created: Watchpoint 1: addr = 0x100008011 size = 1 state = enabled type = r
(lldb) continue
* thread #1, queue = 'com.apple.main-thread', stop reason = watchpoint 1
    frame #0: 0x00000001000006b0 ARM-B09_dbg`my_strlen + 8
->  0x1000006b0 <+8>: cmp    w2, #0x0
(lldb) register read x0 x1 x2 cpsr
      x0 = 0x0000000100008010  main.empty
      x1 = 0x0000000100008011  main.empty + 1
      x2 = 0x000000000000005a
...
(lldb) finish
(lldb) register read x0
      x0 = 0x0000000000000006
(lldb) continue
two_lengths = 306 (기대 300)
```

- `"\0Zebra"` 의 NUL 다음 바이트(`'Z'` = 0x5a)를 읽자마자 멈췄다. 멈춘 pc 는 읽은 `ldrb`(+4) **다음** 명령(+8)이다. 이 맥에서 watchpoint 는 접근 명령이 끝난 뒤 보고됐다. AArch64 의 watchpoint 예외는 접근 명령이 끝나기 전에 걸리고, lldb 가 그 명령을 한 스텝 실행한 뒤 보고하는 것으로 알고 있다 (확인 권장). 그래서 범인은 "멈춘 pc 바로 앞 명령" 이다.
- `finish` 후 `x0 = 6` = `"Zebra"` 를 끝까지 센 값이다. 출력 306 = 3·100 + 6.
- 하드웨어 watchpoint 는 CPU 의 디버그 레지스터(DBGWVR/DBGWCR) 수만큼만 쓸 수 있다. Arm ARM 기준 구현마다 2~16 개다. Cortex-M3/M4 는 DWT comparator 4 개가 흔하다 (칩 문서 확인). 그래서 "감시할 주소 1 바이트" 를 정확히 고르는 것이 중요하다.

### Part C — 크래시를 덤프처럼 읽기

`demo_crash.txt` 의 실제 기록 (`./dbg crash`: `""` 을 `0x100027fff` 에, 가드 페이지 `0x100028000`):

```text
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=2, address=0x100028000)
    frame #0: 0x00000001000006ac ARM-B09_dbg`my_strlen + 4
->  0x1000006ac <+4>:  ldrb   w2, [x1, #0x1]!
(lldb) register read pc lr sp x0 x1 x2 cpsr
      pc = 0x00000001000006ac  ARM-B09_dbg`my_strlen + 4
      lr = 0x000000010000064c  ARM-B09_dbg`main + 264 at demo_debug.c:40:9
      sp = 0x000000016fdfea10
      x0 = 0x0000000100027fff
      x1 = 0x0000000100027fff
      x2 = 0x00000000000120a8
    cpsr = 0x20000000
(lldb) register read far esr
     far = 0x0000000100028000
     esr = 0x92000007
(lldb) bt
  * frame #0: 0x00000001000006ac ARM-B09_dbg`my_strlen + 4
    frame #1: 0x000000010000064c ARM-B09_dbg`main(argc=2, argv=0x000000016fdff0f8) at demo_debug.c:40:29
(lldb) memory read -s1 -fx -c1 $x0
0x100027fff: 0x00
```

읽는 순서 (Cortex-M HardFault 를 읽을 때도 같은 순서다. ARM-M12):

| 단계 | 본 것 | 결론 |
|---|---|---|
| 1. 어디서? | `pc = my_strlen+4` (`ldrb w2, [x1, #1]!`) | data abort 는 **그 명령 자신**을 가리킨다 (다음 명령이 아님) |
| 2. 어느 주소? | 주소식 `x1 + 1` = `0x100027fff + 1` = `0x100028000` = `far` | 이 명령이 범인. 주소는 가드 페이지 첫 바이트 |
| 3. 무슨 접근? | `esr = 0x92000007` (아래 그림) | 하위 EL 에서 온 data abort, **읽기**, 변환 실패(level 3) |
| 4. 누가 불렀나? | `lr = main+264` (leaf 라 프레임 없음, lr 이 곧 호출자), `bt` 도 같음 | `demo_debug.c:40` 의 `my_strlen(s)` |
| 5. 입력은? | `x0 = s`, `*s = 0x00` | 빈 문자열인데 `s+1` 을 읽음 → Part A 의 버그와 일치 |
| 6. 부작용? | `x1` 이 아직 `0x100027fff` (pre-index 의 `+1` 이 반영 안 됨) | fault 난 명령은 레지스터 writeback 을 하지 않았다 (관찰. Arm ARM 의 abort 규칙상 base 레지스터는 그대로라고 이해함 — 확인 권장) |

```svg
<svg viewBox="0 0 740 190" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">esr = 0x92000007 해석 (ESR_ELx, Arm ARM 기준 — 확인 권장)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="150" height="40" class="f-acc"/><rect x="170" y="40" width="40" height="40" class="f-mut"/>
    <rect x="210" y="40" width="270" height="40" class="f-mut"/><rect x="480" y="40" width="60" height="40" class="f-ok"/>
    <rect x="540" y="40" width="180" height="40" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle" font-size="12">
    <text x="95" y="57">EC [31:26]</text><text x="95" y="73">100100 = 0x24</text>
    <text x="190" y="57">IL</text><text x="190" y="73">1</text>
    <text x="345" y="57">ISS [24:7]</text><text x="345" y="73">0 (ISV=0, FnV=0 …)</text>
    <text x="510" y="57">WnR[6]</text><text x="510" y="73">0</text>
    <text x="630" y="57">DFSC [5:0]</text><text x="630" y="73">000111</text>
  </g>
  <g fill="currentColor">
    <text x="20" y="108">EC 0x24 = Data Abort from a lower Exception Level (EL0 사용자 코드의 load/store)</text>
    <text x="20" y="128">IL = 1 → 32 비트 명령 · WnR = 0 → 읽기(read)였다 · FnV = 0 → FAR 값이 유효</text>
    <text x="20" y="148">DFSC 0b000111 = Translation fault, level 3 (PROT_NONE 페이지는 페이지 테이블 항목이 무효로 처리된 것으로 보임)</text>
    <text x="20" y="172">macOS 는 이것을 EXC_BAD_ACCESS code=2 (KERN_PROTECTION_FAILURE) → 시그널 SIGBUS(10) 로 전달</text>
  </g>
</svg>
```

Cortex-M 에는 ESR/FAR 대신 CFSR(MMFSR/BFSR/UFSR)·MMFAR·BFAR 이 있다. 생각하는 순서는 같다: **원인 레지스터 → 주소 레지스터 → 멈춘 pc 의 명령 → 그 명령의 주소식에 레지스터 값을 대입.** (ARM-M12 에서 디코더를 짠다.)

#### `register write` 로 가설 검증 — 재빌드 없이

```text
(lldb) b my_strlen
(lldb) process launch -- crash
s=0x100027fff guard=0x100028000
(lldb) si
->  0x1000006ac <+4>:  ldrb   w2, [x1, #0x1]!
(lldb) register read x0 x1
      x0 = 0x0000000100027fff
      x1 = 0x0000000100027fff
(lldb) register write x1 `$x0 - 1`
(lldb) register read x1
      x1 = 0x0000000100027ffe
(lldb) breakpoint delete 1
(lldb) continue
len=0
Process 21137 exited with status = 0 (0x00000000)
```

`mov x1, x0` 직후 `x1` 만 `s−1` 로 바꿨더니 크래시가 사라지고 `len=0` 이 나왔다. 수정 ① 이 맞다는 증거다. 보드에서 JTAG 로 할 때도 똑같다. 플래시를 다시 굽는 데 수 분이 걸리니 레지스터 하나를 바꿔 가설을 먼저 확인하고, 소스는 그다음에 고친다.

#### gdb 로 같은 세션 (Lima VM, aarch64 Linux)

이 VM(M6 위 Apple Virtualization, `/proc/cpuinfo` 에 `sme`·`sme2` 는 있고 `sve` 는 없음)에서는 **네이티브 gdb 17.1 이 `run` 에서 `Unable to fetch SVE/SSVE vector length: Invalid argument.` 로 실패**했다. 그래서 `qemu-aarch64 -g 1234 ./dbg` (사용자 모드 QEMU 의 gdbstub)에 `gdb-multiarch` 로 붙었다. Cortex-M 을 QEMU 나 OpenOCD 로 디버깅할 때와 같은 원격(`target remote`) 모양이다. `demo_gdb.txt` 발췌:

```text
Breakpoint 1, my_strlen () at starter.S:24
24	    mov     x1, x0                     // x1 = 탐색 포인터 p = s
x0             0x4a0040            4849728
x1             0x4a0050            4849744
cpsr           0x60000000          [ EL=0 BTYPE=0 C Z ]
0x4a0040 <text.1>:	0x61	0x62	0x63	0x00	0x23	0x23	0x23	0x23
25	1:  ldrb    w2, [x1, #1]!              // pre-index: p += 1 먼저, 그 다음 w2 = *p
26	    cmp     w2, #0                     // NZCV 갱신 (w2 - 0)
x1             0x4a0041            4849729
x2             0x62                98
...
x0             0x4a0050            4849744
x1             0x4a004f            4849743          ← set $x1 = $x0 - 1
--- program stdout:
two_lengths = 300 (기대 300)
```

gdb 는 `-g` 로 어셈블한 `starter.S` 의 **소스 줄**을 보여 줬다. 이 맥의 lldb 는 같은 함수에서 디스어셈블리만 보여 줬다 (dSYM 에는 `starter.S` 줄 테이블이 있었다. 왜 소스를 안 띄웠는지는 확인하지 못했다). 첫 `ldrb` 뒤 `x2 = 0x62` 라는 증거는 두 디버거에서 같다.

| 하는 일 | lldb | gdb | 이 레슨에서 gdb 로 실제 확인? |
|---|---|---|---|
| 함수 브레이크 | `b my_strlen` | `break my_strlen` | 예 |
| 명령 1개 (안으로) | `si` | `stepi` / `si` | 예 (`stepi`) |
| 명령 1개 (call 넘김) | `ni` | `nexti` / `ni` | 아니오 (문서 기준) |
| 함수 끝까지 | `finish` | `finish` | 예 |
| 레지스터 읽기 | `register read x0 x1 cpsr` | `info registers x0 x1 cpsr`, `p/x $x0` | 예 |
| 레지스터 쓰기 | ``register write x1 `$x0-1` `` | `set $x1 = $x0 - 1` | 예 |
| 메모리 | `memory read -s1 -fx -c8 $x0` / `x/8xb $x0` | `x/8xb $x0` | 예 |
| 디스어셈블 | `disassemble -n my_strlen` | `disassemble my_strlen`, `x/i $pc` | 예 (`disassemble`) |
| read watchpoint | `watchpoint set expression -w read -s 1 -- $x0+1` | `rwatch *(char *)($x0+1)` | 아니오. qemu-user gdbstub 에서 `watch -l` 이 "Could not insert hardware watchpoint" 로 실패함 |
| 조건부 bp | `br set -n my_strlen -c '...'` | `break my_strlen if ...` | lldb 만 (연습 5) |
| 멈출 때마다 출력 | `target stop-hook add -o "..."` | `display/x $x1` | lldb 만 (연습 5) |
| 원격 연결 | `gdb-remote 1234` | `target remote :1234` | gdb 만 |

Cortex-M + QEMU(`-s -S`) + `gdb-multiarch` 세션은 TOOL-A03 과 ARM-M11(`demo_gdb_isr.gdb`)에 있다.

#### Linux/ELF 에서 같은 .S

`starter.S` 의 `SYM/TYPE/SIZE` 매크로가 플랫폼 차이를 흡수한다 (`.S` 는 C 전처리기를 거친다):

| | macOS (Mach-O) | Linux (ELF) |
|---|---|---|
| 심볼 | `.globl _my_strlen` / `_my_strlen:` | `.global my_strlen` / `my_strlen:` |
| 타입·크기 | 없음 | `.type my_strlen, %function` / `.size my_strlen, . - my_strlen` |
| 실행 불가 스택 표시 | 필요 없음 | `.section .note.GNU-stack,"",%progbits` (없으면 링커가 경고) |
| 주석 | `//` (`;` 는 Mach-O arm64 에서 주석) | `//` (`;` 는 문장 구분자) |

`;` 차이는 실제로 확인했다: `mov x0, x1 ; mov x2, x3` 한 줄이 맥에서는 `mov x0, x1` 1 명령(`otool -tv`), VM gcc 에서는 2 명령(`objdump -d`: `aa0103e0`, `aa0303e2`)으로 어셈블됐다.

Lima VM 에서 실제로 빌드·실행 (gcc 15.2.0, 페이지 4096 B): starter `1154/1172 passed` (exit 1, 가드 페이지 줄은 `sig=11`), solution `1172/1172 passed` (gcc, clang 둘 다). `readelf -s` → `my_strlen  24 FUNC GLOBAL` (6 명령 × 4 B = 24 B, `.size` 가 들어간 결과).

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S -o /tmp/ARM-B09_solution && /tmp/ARM-B09_solution
# 1172/1172 passed   (exit 0, 실행 0.004 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined harness.c solution.S -o /tmp/ARM-B09_asan && /tmp/ARM-B09_asan
# 1172/1172 passed   (ASan/UBSan 보고 없음. 단 asm 내부는 ASan 이 계측하지 않는다는 점은 Part B 참고)
```

바뀐 줄은 하나다: `mov x1, x0` → `sub x1, x0, #1`. 명령 수도 그대로다 (`sub` 즉시값 형태도 4 바이트 명령 1개).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 습관 |
|---|---|---|
| pre-index `[x1, #1]!` 와 post-index `[x1], #1` 혼동 | pre 는 "더하고 읽기", post 는 "읽고 더하기". 첫 원소를 건너뛰거나, NUL 다음을 가리킨 채 끝남 | 스텝 표의 **첫 load 결과**를 반드시 확인 (주소 모드 전체는 ARM-B04) |
| do-while 모양 루프로 "0 번 반복" 경우를 놓침 | 빈 입력에서 첫 반복이 이미 범위 밖을 읽음 | 테스트에 len=0 을 항상 넣는다. 아니면 "검사 먼저" while 모양 |
| `"abc"` 가 3 이니 맞다고 결론 | 우연히 맞는 값. 읽은 바이트 집합이 틀렸다 | 반환값만이 아니라 **어떤 주소를 읽었나**를 본다 (watchpoint, 가드 페이지) |
| `bl` 위에서 `ni` 를 기대하고 `si` 를 눌러 남의 함수(예: `printf`) 안으로 들어감 | `si` 는 무조건 다음 명령 하나 | `finish` 로 빠져나온다. 들어가기 싫으면 `ni` |
| watchpoint 가 멈춘 pc 를 범인으로 지목 | 이 맥에서는 접근 명령 **다음** pc 에서 보고됨 | 바로 앞 명령(`disassemble -s $pc-4 -c 2`)을 본다 |
| 크래시 시 `x1` 을 "이미 +1 된 값" 으로 가정 | fault 난 명령은 writeback 을 안 했다 (관찰) | 주소식 `x1 + 1` 을 직접 계산해 `far` 와 맞춘다 |
| ASan 을 켰으니 asm 메모리 버그도 잡힐 거라 믿음 | ASan 은 컴파일러 계측이라 `.S` 는 검사 안 됨 | 가드 페이지·MPU 경계 테스트 |
| Mach-O `.S` 에 `;` 주석 | Linux ELF 에서는 `;` 가 문장 구분자 → 주석 내용이 명령으로 해석됨 | `//` 또는 `/* */` 만 |

## 8. 면접 Q&A

<details>
<summary>Q1. si 와 ni 는 무엇이 다른가요? pc 가 bl 위에 있을 때 각각 어디서 멈추나요?</summary>

`si`(stepi)는 명령 하나만 실행한다. `bl` 이면 호출된 함수의 첫 명령에서 멈추고, 그때 `lr` = `bl` 다음 주소다 (실측: `pc=my_strlen+0`, `lr=two_lengths+40`). `ni`(nexti)는 `bl` 을 하나의 단위로 보고 호출이 돌아온 뒤(`bl` 다음 명령)에서 멈춘다 (실측: `pc=+28`, `x0=3`). 꼬리질문: "ni 중에 호출된 함수 안의 브레이크포인트는?" → 걸린다. 내부적으로 호출된 함수를 실제로 실행시키기 때문이다. "하드웨어로 단일 스텝은 어떻게?" → AArch64 는 디버그 단일 스텝 기능(MDSCR_EL1.SS + SPSR.SS)을 쓴다. Cortex-M 은 DHCSR.C_STEP 이다 (Arm ARM / ARMv7-M ARM 기준, 확인 권장).

</details>

<details>
<summary>Q2. 크래시 덤프에서 pc, lr, far, esr 로 각각 무엇을 알 수 있나요?</summary>

`pc` = fault 난 명령 (data abort 의 경우 그 명령 자신). `far` = 접근하려던 주소. `esr` = 원인(EC: 데이터 abort, 명령 abort, SVC…), 읽기/쓰기(WnR), 오류 종류(DFSC: 변환/권한/정렬). `lr` = leaf 함수라면 호출자, 아니라면 이미 덮였을 수 있으니 프레임 체인(`bt`, ARM-B08)을 본다. 실측: `pc=my_strlen+4`, `far=0x100028000`, `esr=0x92000007` (EC 0x24, 읽기, translation fault L3). 그다음 주소식 `x1+1` 에 레지스터 값을 대입해 `far` 와 맞으면 원인 확정이다. Cortex-M 은 CFSR/MMFAR/BFAR 와 스택에 저장된 PC 로 같은 일을 한다 (ARM-M09, ARM-M12).

</details>

<details>
<summary>Q3. ret 은 실제로 무엇을 하나요? leaf 함수에서 lr 이 왜 중요한가요?</summary>

`ret` 은 `br x30` 과 같은 분기다. 기본 레지스터가 x30 이고, CPU 의 반환 주소 예측기(return stack)에 "함수 복귀" 라는 힌트를 준다 (`br x30` 으로 쓰면 예측 힌트가 다르다 — Arm ARM 기준, 확인 권장). leaf 함수는 `lr` 을 스택에 저장하지 않으므로, 멈춘 순간의 `lr` 이 유일한 호출자 정보다. 반대로 non-leaf 는 `bl` 이 `lr` 을 덮으니 프롤로그에서 `stp x29, x30` 으로 저장해야 한다 (ARM-B08). 스텝 표 마지막 줄에서 `ret` 뒤 pc = 0x100000514 = 그때의 `lr` 이었다.

</details>

<details>
<summary>Q4. 테스트 1154/1172 가 통과하는데 이 버그가 왜 위험한가요? 어떻게 체계적으로 잡나요?</summary>

평소에는 NUL 다음 바이트를 읽어도 증상이 없다. 그 메모리를 읽을 수 있고, 값이 0 이면 결과까지 맞는다. 문자열이 페이지/MPU 영역의 마지막 바이트에 놓이는 드문 배치에서만 fault 가 난다. 펌웨어에서는 이게 "특정 버퍼 배치에서만 나는 HardFault" 가 된다. 체계적으로 잡는 방법: ① 길이 0 과 모든 정렬을 전수로 시험한다. ② 가드 페이지(mmap+mprotect) 또는 MPU no-access 영역을 경계에 둔다. ③ read watchpoint 를 건다. ④ C 코드라면 ASan 을 쓴다. 단 **asm 은 ASan 계측 밖**이다 (`demo_asan_blind.c` 실측: 조용히 1 반환).

</details>

<details>
<summary>Q5. cpsr = 0x60000000 은 무슨 뜻인가요? 어떤 명령이 플래그를 바꾸나요?</summary>

상위 니블 6 = 0110 → N=0 Z=1 C=1 V=0. 예: `cmp w2, #0` 에서 w2=0 이면 결과 0(Z), 빌림 없음(C). 플래그는 `cmp/cmn/tst` 와 S 가 붙은 산술(`adds`, `subs`, `ands`)만 바꾼다. `ldrb`, `mov`, `sub`, 분기, `cbz/cbnz/tbz` 는 안 바꾼다. 그래서 스텝 표에서 cpsr 은 `cmp` 줄에서만 변했다. 꼬리질문: "AArch64 에 CPSR 레지스터가 있나요?" → 없다. PSTATE 필드이고, 예외 때 SPSR_ELx 로 저장된다. 디버거가 편의상 `cpsr` 이라고 부른다.

</details>

<details>
<summary>Q6. 하드웨어 watchpoint 와 소프트웨어 watchpoint 는 무엇이 다른가요? 몇 개까지 쓸 수 있나요?</summary>

하드웨어 watchpoint 는 CPU 디버그 유닛의 주소 비교기(AArch64 DBGWVR/DBGWCR, Cortex-M DWT comparator)가 load/store 주소를 실시간으로 비교한다. 속도 저하가 없지만 개수가 적다 (구현마다 2~16 개, Cortex-M3/M4 는 보통 4 개 — 칩 문서 확인). 소프트웨어 watchpoint 는 디버거가 한 명령씩 실행하며 값을 비교한다. 매우 느리고, 값 변화만 볼 수 있어서 **읽기**는 감시하지 못한다. 실측: 이 맥 lldb 의 `-w read` 는 하드웨어로 동작했다 (`type = r`). Lima VM 의 qemu-user gdbstub 에서는 하드웨어 watchpoint 삽입이 실패했다. 펌웨어에서는 "누가 이 전역 변수/레지스터 미러를 덮어쓰나?" 를 DWT write watchpoint 로 잡는 게 단골 기법이다.

</details>

<details>
<summary>Q7. 디버거로 레지스터를 바꿔서 확인하는 건 어떤 상황에서 쓰나요? 위험은?</summary>

빌드·플래시가 비싼 타깃(SSD 컨트롤러, 큰 SoC 펌웨어)에서 가설을 바로 검증할 때 쓴다. 예: 포인터 1 차이, 레지스터 비트 하나, 분기 방향 (`register write pc ...` 로 경로 건너뛰기). 실측: `x1` 하나를 바꿔 크래시가 `len=0` 으로 바뀌었다. 위험: 컴파일된 C 코드는 같은 값이 다른 레지스터나 스택에도 복사돼 있을 수 있어 일부만 바꾸면 상태가 어긋난다. 하드웨어 레지스터(MMIO)를 디버거로 읽기만 해도 부작용(read-to-clear FIFO, W1C 상태)이 생길 수 있다. 그래서 확인 뒤에는 반드시 소스를 고쳐 정식 테스트를 돌린다.

</details>

<details>
<summary>Q8. 왜 이 맥에서는 SIGBUS, Linux 에서는 SIGSEGV 였나요?</summary>

같은 하드웨어 사건(EL0 의 data abort, translation fault)을 OS 가 다른 시그널로 바꿔 전달한다. macOS 는 Mach 예외 `EXC_BAD_ACCESS` (code=2 `KERN_PROTECTION_FAILURE`)를 거쳐 보호 위반을 `SIGBUS` 로 보낸다. Linux arm64 는 매핑은 있지만 권한이 없는 경우를 `SIGSEGV` + `si_code = SEGV_ACCERR(2)` 로 보낸다 (VM 에서 PROT_NONE 페이지 읽기로 실측: `sig=11 si_code=2`). 그래서 하네스는 둘 다 잡는다 (실측: Mac `sig=10`, VM `sig=11`). 시그널 번호도 플랫폼마다 다르다 (`SIGBUS` 는 macOS 10, Linux arm64 7 — VM `kill -l SIGBUS` = 7).

</details>

## 9. 추가 연습

### 연습 1 — 두 번째 심은 버그: `ldrsb` + 부호 있는 비교

`size_t span_printable(const char *s)` = 앞에서부터 0x20 이상(부호 없이)인 바이트 수. UTF-8 `"caf\xc3\xa9 ok\tx"` → 8. 버그 판 `span_printable_bug` 는 `ldrsb` + `b.lt` 로 짜서 3 을 낸다. lldb 에서 `0xC3` 를 읽은 직후 `w2` 와 cpsr 을 예측하고, 255 개 1 바이트 문자열 전수 테스트로 버그가 정확히 몇 개 입력에서 드러나는지 세라.

<details>
<summary>답 보기</summary>

`ldrsb w2, [x1]` 은 0xC3 을 부호 확장해 `w2 = 0xFFFFFFC3` (= −61) 을 만든다. `cmp w2, #0x20` = 0xFFFFFFC3 − 0x20 = 0xFFFFFFA3 → N=1 (결과 bit31), Z=0, C=1 (부호 없이 보면 빌림 없음), V=0 (−61 − 32 = −93, 넘침 없음) → cpsr = `0xA0000000` = `N-C-`. `b.lt` 는 N≠V 일 때 분기하므로 "−61 < 32" 로 보고 멈춘다. lldb 실측 (`"\xc3"` 한 글자로): `w2 = 0xffffffc3`, `cmp` 뒤 `cpsr = 0xa0000000`, `b.lt` 다음 pc = `+24` (`sub` 로 점프 = 멈춤). 같은 w2 에서 `b.lo`(C=0 일 때 분기)였다면 C=1 이라 안 멈췄겠지만, `ldrsb` 의 0xFFFFFFC3 에 기대는 건 우연이다. 고친 판은 `ldrb`(0 확장, 0..255) + `b.lo`(부호 없이 작으면) 조합이다. 전수 결과: 0x80..0xFF 의 **128 개** 입력에서만 버그 판이 틀린다. 정상적인 ASCII 테스트만 하면 절대 못 잡는다.

</details>

### 연습 2 — post-index 로 고치기

`my_strlen_post`: `ldrb w2, [x1], #1` + `cbnz` 루프로 다시 쓰라. 끝에서 무엇을 보정해야 하나?

<details>
<summary>답 보기</summary>

post-index 는 NUL 을 **읽은 뒤에도** p 를 증가시키므로 루프가 끝나면 p = NUL 다음 → `p − s − 1`. `sub x0, x1, x0` 다음에 `sub x0, x0, #1`. 루프는 `ldrb`+`cbnz` 2 명령 (`cmp`+`b.ne` 3 명령보다 1 개 적다. `cbnz` 는 플래그를 안 건드린다). 보정 `sub` 를 빼먹으면 모든 길이에서 1 크게 나온다 — 하네스 실측: 그 변이체는 `25356/26461` (정렬 격자·페이지 끝 테스트 전부 FAIL).

</details>

### 연습 3 — `my_strnlen(s, n)`: 경계 검사는 읽기 "전에"

NUL 이 없어도 `s[0..n-1]` 밖은 읽지 않는 `my_strnlen` 을 써라. 가드 페이지 바로 앞에 NUL 없는 k 바이트를 두고 `n = k` 로 시험한다.

<details>
<summary>답 보기</summary>

`i < n` 을 **먼저** 확인(`cmp x2, x1` / `b.hs`)하고 그다음 `ldrb w3, [x0, x2]` 를 한다. 순서를 바꾼 변이체(읽고 → NUL 검사 → 경계 검사)는 정렬 격자 테스트를 **전부 통과**했다. 실측: 실패 65 개(`26396/26461`)가 모두 `strnlen no-NUL page-end` 의 `sig=10` 이었다. 본문 버그와 같은 교훈이다: 범위 밖 읽기는 가드 페이지 없이는 안 보인다. `n` 은 `size_t` 이므로 비교는 부호 없는 `b.hs`/`b.lo` 로 한다 (`n = SIZE_MAX` 도 테스트).

</details>

### 연습 4 — cpsr 디코더

lldb 가 준 `cpsr` 값을 `"NZCV"` 문자열(0 인 플래그는 `-`)로 바꾸는 `nzcv_decode(uint32_t cpsr, char out[5])` 를 C 로 써라. 이 레슨에서 실제로 본 값 `0x60000000`, `0x20000000` 을 테스트에 넣어라.

<details>
<summary>답 보기</summary>

비트 31−i 를 i=0..3 순서로 꺼내 `'N','Z','C','V'` 또는 `'-'`. `0x60000000` → `-ZC-`, `0x20000000` → `--C-`, `0x90000000` → `N--V`. 하위 28 비트는 무시한다 (`0x0FFFFFFF` → `----`). 크래시 로그 파서나 HardFault 핸들러가 xPSR 을 사람 말로 찍을 때 쓰는 그대로의 코드다 (Cortex-M xPSR 도 31..28 이 NZCV).

</details>

### 연습 5 — 조건부 브레이크포인트 + stop-hook 으로 자동 트레이스 (lldb 전용)

`my_strlen` 이 두 번 불리는데 `""` 일 때만 멈추고, 멈출 때마다 `x1 x2 cpsr` 이 자동으로 찍히게 하라.

<details>
<summary>답 보기</summary>

```text
breakpoint set -n my_strlen -c '*(unsigned char *)$x0 == 0'
target stop-hook add -o "register read x1 x2 cpsr"
```

`demo_practice.lldb` / `demo_practice.txt` (실측): `"abc"` 호출은 그냥 지나가고 `x0 = main.empty` 에서만 멈췄다. 이후 `si` 마다 `x1 = main.empty + 1`, `x2 = 0x5a` 가 자동 출력됐다. 조건식은 C 식이지만 레지스터를 `$x0` 로 쓸 수 있다. gdb 는 `break my_strlen if *(unsigned char *)$x0 == 0` + `display/x $x1` (문서 기준, 이 레슨에서 gdb 로는 미확인). 루프가 수천 번 도는 곳에서는 조건부 브레이크포인트도 매번 디버거로 왕복하므로 느리다는 점을 기억하라.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S -o /tmp/ARM-B09_practice && /tmp/ARM-B09_practice
# 26461/26461 passed   (exit 0, 0.004 s. ASan/UBSan 빌드와 Lima VM gcc 빌드도 26461/26461)
```

## 10. 핵심 정리

- `si` = 명령 1개(`bl` 이면 안으로, `lr` = 복귀 주소), `ni` = `bl` 을 통째로, `finish` = 호출자로. 멈춘 뒤 `register read`, `memory read -s1 -fx -c N`, `disassemble -n`.
- `cpsr` 상위 니블 = NZCV: `6` = -ZC-, `2` = --C-. 플래그는 `cmp/cmn/tst`·`…s` 명령만 바꾼다. `cbz/cbnz`·`ldr`·`mov` 는 안 바꾼다.
- 스텝 표에서 **첫 load 가 무엇을 읽었나**를 본다. 반환값이 맞아도 읽은 주소가 틀릴 수 있다 (`"abc"` → 3 이지만 `'a'` 를 안 읽음).
- pre-index `[x, #k]!` = 더하고 읽기, post-index `[x], #k` = 읽고 더하기. do-while 루프는 len=0 에서 깨지기 쉽다.
- 범위 밖 읽기는 가드 페이지(mmap+mprotect)·read watchpoint 로 보이게 만든다. **ASan 은 .S 를 계측하지 못한다.**
- 크래시 읽는 순서: `pc` 의 명령 → 주소식에 레지스터 대입 → `far` 와 일치 → `esr`(EC/WnR/DFSC) → `lr`/`bt` 로 호출자. fault 난 명령은 writeback 을 하지 않았다 (실측).
- 고치기 전에 `register write` 로 가설을 확인하고, 그다음 소스를 고쳐 전체 테스트를 돌린다. lldb ↔ gdb: `register read`↔`info registers`, `register write`↔`set $x1=`, `si/ni`↔`stepi/nexti`.

## 참고자료

- [LLDB — GDB to LLDB command map](https://lldb.llvm.org/use/map.html)
- [LLDB Tutorial (breakpoints, watchpoints, stepping)](https://lldb.llvm.org/use/tutorial.html)
- [Arm Architecture Reference Manual for A-profile (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest) — LDRB (immediate) pre/post-index, ESR_ELx (EC, ISS, DFSC), FAR_ELx, PSTATE/NZCV, Self-hosted debug (breakpoints, watchpoints, software step)
- [Arm A64 Instruction Set — LDRB, LDRSB, CMP, B.cond, CBNZ, RET](https://developer.arm.com/documentation/ddi0602/latest)
- [Apple — Writing ARM64 code for Apple platforms](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms)
- [GDB manual — Continuing and Stepping (stepi/nexti), Setting Watchpoints, Registers](https://sourceware.org/gdb/current/onlinedocs/gdb.html/)
- [QEMU user-mode emulation — `-g port` gdbstub](https://www.qemu.org/docs/master/user/main.html)
- [AddressSanitizer — how instrumentation works](https://github.com/google/sanitizers/wiki/AddressSanitizerAlgorithm)
- 관련 레슨: ARM-B04 (주소 지정 모드), ARM-B05 (플래그·조건 분기), ARM-B08 (스택 프레임·frame chain), ARM-B10 (strlen/memcpy 성능), ARM-M12 (HardFault 디코딩), ARM-M14 (크래시 덤프 역추적), TOOL-A03 (QEMU gdb 원격)
