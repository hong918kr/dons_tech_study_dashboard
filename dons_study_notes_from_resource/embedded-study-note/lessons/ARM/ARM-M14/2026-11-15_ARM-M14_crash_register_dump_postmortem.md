---
id: ARM-M14
time: 80분
updated: 2026-11-15
---
# ARM-M14 크래시 레지스터 덤프로 원인 역추적

## 학습 목표

- 크래시 덤프를 **정해진 순서**(fault 주소 → pc 의 명령 → 베이스 레지스터 값 → 그 레지스터를 쓴 명령 → bt/fp 사슬 → lr 상식 검사)로 읽고, 원인 소스 라인까지 말로 설명한다.
- 이 맥(M6, macOS 27.2)에서 실제로 죽인 3종 크래시 — NULL 구조체 멤버, 스택 오버런으로 깨진 LR, 해제 후 사용(use-after-free) — 의 lldb 덤프를 읽고 각 증상의 **지문**(fault 주소 = offset, pc = ASCII, 레지스터 = 0x5555…)을 알아본다.
- `EXC_BAD_ACCESS` 의 code(1 / 257 / 261)와 ESR(EC·DFSC)을 해석하고, PAC(arm64e)가 켜지면 같은 버그의 증상이 어떻게 바뀌는지 실측으로 안다.
- lldb 텍스트 덤프를 파싱해 원인을 분류하고 근거(멤버 이름, def 명령, 죽은 스택의 리턴 주소)를 내는 C 도구를 만든다.

## 1. 문제

"이 크래시 로그 보고 원인 라인 찾아봐요" — 펌웨어·실리콘 검증 면접의 단골이고, 현장에서는 고객 필드의 `.ips` 리포트, Cortex-M HardFault 레지스터 덤프(ARM-M12), SSD 컨트롤러의 assert 덤프가 같은 모양으로 온다. 재현이 안 되는 크래시일수록 **레지스터 몇 개와 디스어셈블리 몇 줄**만으로 추론해야 한다.

면접에서 말하는 순서는 늘 같다. 이 레슨의 도구도 이 순서를 코드로 옮긴다.

| 단계 | 무엇을 보나 | lldb 명령 | 이 레슨에서 찾은 것 |
|---|---|---|---|
| 1 | fault 주소 · 예외 종류 | stop reason, `register read far esr` | `address=0x18` → "작은 수 = NULL + offset" |
| 2 | pc 의 명령 | `disassemble -f` (`->` 줄) | `ldr x8, [x0, #0x18]` → 베이스 x0, offset 0x18 |
| 3 | 베이스 레지스터 값 | `register read` | x0 = 0 → NULL 포인터 / x8 = 0x5555… → 해제된 메모리 |
| 4 | 그 레지스터를 **누가** 썼나 | `disassemble -f` 에서 pc 앞쪽 | `ldr x8, [x0, #0x18]` → `r->ops` 를 읽었다 |
| 5 | 호출 사슬 | `bt`, fp 사슬, 죽은 스택 | `bt` 가 frame #0 하나뿐 → fp/lr 이 깨졌다 |
| 6 | lr 상식 검사 | `register read lr` | lr == pc == `0x4242…` → 깨진 LR 로 `ret` 했다 |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">classify() 결정 순서 — 위에서부터 처음 맞는 칸</text>
<rect x="10" y="34" width="250" height="34" class="f-mut" stroke="currentColor"/>
<text x="20" y="56" fill="currentColor">EXC_BAD_ACCESS 이고 pc 를 아나?</text>
<text x="275" y="56" fill="currentColor">아니오 → UNKNOWN</text>
<rect x="10" y="84" width="250" height="34" class="f-mut" stroke="currentColor"/>
<text x="20" y="106" fill="currentColor" font-family="monospace">code == 261 (PAC_FAIL)?</text>
<rect x="470" y="84" width="200" height="34" class="f-warn" stroke="currentColor"/>
<text x="480" y="106" fill="currentColor">예 → PAC_FAIL (arm64e)</text>
<rect x="10" y="134" width="250" height="34" class="f-mut" stroke="currentColor"/>
<text x="20" y="156" fill="currentColor">pc%4≠0 · __text 밖 · 채움 패턴?</text>
<rect x="470" y="134" width="200" height="34" class="f-warn" stroke="currentColor"/>
<text x="480" y="156" fill="currentColor">예 → BAD_PC (깨진 LR)</text>
<rect x="10" y="184" width="250" height="34" class="f-mut" stroke="currentColor"/>
<text x="20" y="206" fill="currentColor">pc 의 명령이 [베이스] 접근?</text>
<text x="275" y="206" fill="currentColor">아니오 → UNKNOWN</text>
<rect x="10" y="234" width="250" height="34" class="f-mut" stroke="currentColor"/>
<text x="20" y="256" fill="currentColor">베이스/주소 = 0x5555… / 0xAAAA…?</text>
<rect x="470" y="234" width="200" height="34" class="f-warn" stroke="currentColor"/>
<text x="480" y="256" fill="currentColor">예 → SCRIBBLE (UAF)</text>
<rect x="10" y="284" width="250" height="34" class="f-mut" stroke="currentColor"/>
<text x="20" y="306" fill="currentColor">베이스 = 0, 주소 = 즉치 &lt; 0x10000?</text>
<rect x="470" y="284" width="200" height="34" class="f-acc" stroke="currentColor"/>
<text x="480" y="306" fill="currentColor">예 → NULL_MEMBER</text>
<g stroke="currentColor" fill="none">
<line x1="135" y1="68" x2="135" y2="84"/><line x1="135" y1="118" x2="135" y2="134"/>
<line x1="135" y1="168" x2="135" y2="184"/><line x1="135" y1="218" x2="135" y2="234"/>
<line x1="135" y1="268" x2="135" y2="284"/>
<line x1="260" y1="101" x2="470" y2="101"/><line x1="260" y1="151" x2="470" y2="151"/>
<line x1="260" y1="251" x2="470" y2="251"/><line x1="260" y1="301" x2="470" y2="301"/>
</g>
<text x="690" y="306" fill="currentColor">그 밖:</text>
<text x="680" y="322" fill="currentColor">UNKNOWN</text>
</svg>
```

크래시 재료는 이 맥에서 직접 만든다 (모두 `-O1 -g -fno-stack-protector`, 실제 AArch64 macOS 프로세스, lldb 배치 세션).

| 데모 | 버그 | 실측 증상 (lldb) |
|---|---|---|
| `demo_crash_null.c` | `find_sensor()` 가 NULL 을 줬는데 `s->read()` 호출 | `EXC_BAD_ACCESS (code=1, address=0x18)`, `ldr x8, [x0, #0x18]`, x0 = 0 |
| `demo_crash_stack.c` | 16 B 지역 버퍼에 48 B 복사 → 저장된 x29/x30 덮음 | `code=257, address=0x4242424242424242`, pc = `0x0000424242424242`, `bt` 1줄 |
| 같은 소스, `-arch arm64e` | 위와 같음, PAC 켜짐 | `code=261, address=0xd65f0fff`, pc = `set_label + 64` 의 `retab` |
| `demo_crash_uaf.c` | free 한 DMA 요청을 큐에 남은 포인터로 재사용 | `MallocZeroOnFree=0 MallocScribble=1`: x8 = `0x5555555555555555` / 기본: x8 = 0, `address=0x0` |

<!-- code: demo_crash_null.c -->

<!-- code: demo_crash_stack.c -->

<!-- code: demo_crash_uaf.c -->

### Part A — 덤프 한 줄 파서

- `parse_reg_line` — `        lr = 0x000000010000059c  ARM-M14_null`main + 64 at demo_crash_null.c:45:5` → 레지스터 번호, 값, 기호. arm64e 의 `x16 = 0x516c… (0x0000…) sym` 형식도.
- `parse_stop_reason` — `stop reason = EXC_BAD_ACCESS (code=1, address=0x18)` → 예외 이름, code, 주소. `breakpoint 1.1` 처럼 주소 없는 것도.
- `parse_insn` — `ldr x8, [x0, #0x18]`, `ldrb w2, [x1, #0x1]!`, `ldr x0, [x1], #0x8`, `ldp x29, x30, [sp, #0x20]`, `ldur x8, [x29, #-0x8]`, `ldr x8, [x0, x1, lsl #3]`, `str`, `blr`, `retab` → 니모닉, 쓰는 레지스터, 베이스, 즉치, pre/post-index.

### Part B — 분류와 깨진 덤프

- `classify(const dump_t*)` — 위 그림의 규칙. 실제 덤프 5개가 각각 NULL_MEMBER / BAD_PC / PAC_FAIL / SCRIBBLE / NULL_MEMBER 로 나와야 한다.
- 경계 사례(합성 덤프 13개): 빈 입력, 잘린 덤프(pc 없음), breakpoint 정지, pc 가 4의 배수 아님, `__text` 밖, 베이스 ≠ 0, 주소 ≠ 베이스+즉치, 디스어셈블 없음, post-index / pre-index NULL, `0xAAAA…`(PreScribble) 베이스, 레지스터 오프셋(인덱스 값 모름) → 모두 "아는 만큼만" 답하고 **절대 추측하지 않는다**.

### Part C — 원인 라인 역추적

- `member_at` — offset → 멤버 이름 (`offsetof` 로 만든 표; 데모 구조체의 축약 복사본).
- `find_def` — "x8 을 누가 썼나": pc 앞 명령을 거꾸로 훑어 그 레지스터를 쓰는 첫 명령.
- `stale_return_scan` — `bt` 가 실패한 BAD_PC 에서, sp 아래 **죽은 스택**에 남은 리턴 주소 중 sp 에 가장 가까운 것 → 어느 함수가 깨진 LR 로 `ret` 했나.
- 주어진 `triage()` 가 이 셋을 묶어 리포트(분류, 함수, 소스 라인, 멤버, def 명령, 객체 모양 힌트)를 만든다.

## 2. Problem (English)

> **Post-mortem from a register dump.**
>
> You are given text dumps captured with `lldb -b` from crashed AArch64 macOS processes: `register read`, `register read far esr`, `disassemble -f` (the faulting instruction is marked `->`), `bt`, optional `memory read` lines and `image dump sections`.
>
> 1. Parse register lines, the stop reason (`EXC_BAD_ACCESS (code=N, address=0xA)`) and A64 load/store operands (`[xn, #imm]`, `[xn, #imm]!`, `[xn], #imm`, `[xn, xm, lsl #s]`, `ldp`).
> 2. `crash_class_t classify(const dump_t *d)`: PAC_FAIL if code is 261; BAD_PC if pc is misaligned, outside `__TEXT.__text` or a fill pattern; SCRIBBLE if the base register or fault address is a malloc scribble pattern (`0x5555…`, `0xAAAA…`); NULL_MEMBER if the base register is 0 and the fault address equals the effective address (< 0x10000); otherwise UNKNOWN. Never guess on partial dumps.
> 3. Recover the culprit: the struct member from the offset, the instruction that defined the base register, and for BAD_PC the most recent return address left in dead stack below sp.
>
> Example: `address=0x18`, `ldr x8, [x0, #0x18]`, `x0 = 0` → NULL_MEMBER, member `read` of `struct sensor`, line `demo_crash_null.c:36`.
>
> Follow-ups: why is `bt` useless after a stack smash? What changes with PAC? Why does a use-after-free on modern macOS often look like a NULL dereference?

## 3. 시작 코드

데이터 파일은 레슨 디렉터리에 들어 있다 (`make_dumps.sh` 가 만든 **실제 lldb 출력**. 정리한 것은 DWARF 섹션 줄 · "Executing commands" 줄 · 레슨 디렉터리 절대 경로뿐).

| 파일 | 내용 |
|---|---|
| `crash_null.txt` · `crash_stack.txt` · `crash_stack_arm64e.txt` · `crash_uaf.txt` · `crash_uaf_zero.txt` | 본문 문제 입력 (각 60–100줄) |
| `crash_pattern.txt` · `linetable_null.txt` · `linetable_uaf.txt` | 추가 연습 입력 |
| `demo_crash_*.lldb` | 각 덤프를 만든 lldb 명령 파일 |

<!-- code: make_dumps.sh -->

```bash
cd lessons/ARM/ARM-M14
bash make_dumps.sh          # (선택) 덤프 다시 만들기 — 힙/스택 주소는 매번 조금 다를 수 있음, 테스트는 그 값에 기대지 않는다
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/ARM-M14_starter && /tmp/ARM-M14_starter
```

<!-- code: starter.c -->

starter 실제 출력 (요약 줄만, FAIL 52줄 생략) — exit 1:

```text
Part A (line parsers): 10/32 FAIL
Part B (parse_dump + classify): 16/31 FAIL
Part C (triage report): 2/17 FAIL
[crash_null.txt] UNKNOWN     func=? line=? caller=?
...
28/80 passed
```

## 4. 힌트

<details><summary>힌트 1 — 레지스터 줄은 "이름 = 0x값 [기호]"</summary>

lldb 의 `register read` 줄은 공백 + 이름 + ` = ` + 16자리 16진수, 그 뒤 두 칸 띄고 기호(`img`func + off at file:line:col`)가 올 수 있다. `fp`/`lr` 은 x29/x30 이다. arm64e 에서는 서명된 포인터 뒤에 `(서명 뗀 값)` 이 한 번 더 붙는다 — 원래 값은 첫 16진수다.
</details>

<details><summary>힌트 2 — 메모리 피연산자는 세 가지 모양</summary>

`[xn, #imm]` (오프셋), `[xn, #imm]!` (pre-index: 주소 = xn+imm, 그 뒤 xn 갱신), `[xn], #imm` (post-index: 주소 = xn, 그 뒤 xn+imm). fault 주소와 비교할 **유효 주소**는 post-index 일 때만 베이스 그대로다 (ARM-B04). `[x0, x1, lsl #3]` 는 x1 을 알아야 하므로 NULL_MEMBER 판정에 쓰지 않는다.
</details>

<details><summary>힌트 3 — 채움 패턴과 작은 offset</summary>

`MallocScribble` 은 해제한 블록을 0x55, 새 블록을 0xAA 로 채운다. 그 값에 멤버 offset 이 더해질 수 있으니 상위 6바이트만 비교한다. 순서도 중요하다: 0x5555… 는 "베이스 = 0" 이 아니므로 SCRIBBLE 을 먼저 보든 나중에 보든 같지만, BAD_PC 는 **명령을 읽기 전에** 판정해야 한다 (pc 가 쓰레기면 디스어셈블이 없다).
</details>

<details><summary>힌트 4 — "누가 x8 을 썼나"</summary>

디스어셈블 목록을 pc 바로 앞부터 거꾸로 훑어 첫 번째로 x8 을 **목적지**로 쓰는 명령을 찾는다. 저장(`str`, `stp`), 비교(`cmp`), 분기(`b`, `bl`, `blr`, `ret*`)는 목적지가 없다. pre/post-index 는 베이스도 쓴다. 함수 안에 정의가 없고 x0–x7 이면 인자 — 원인은 호출자에 있다.
</details>

<details><summary>힌트 5 — bt 가 안 될 때 죽은 스택</summary>

`ret` 직후 sp 는 이미 호출자 프레임을 가리킨다. 방금 끝난 함수의 프레임과, 그 함수가 부른 함수(printf 등)의 프레임은 sp **아래**에 지워지지 않고 남는다. 거기서 `__text` 안을 가리키는 값(= 리턴 주소)을 찾으면 마지막으로 실행되던 함수가 보인다. `memory read -f A` 가 각 워드를 기호로 풀어 준다.
</details>

## 5. 풀이

### Part A — 덤프를 줄 단위로 읽기

덤프의 줄은 다섯 종류뿐이다. 나머지(빈 줄, `(lldb)` 메아리, `Process … launched`)는 무시한다.

| 줄 모양 (실제 덤프에서) | 파서 | 뽑는 것 |
|---|---|---|
| `* thread #1, … stop reason = EXC_BAD_ACCESS (code=1, address=0x18)` | `parse_stop_reason` | exc, code, addr (첫 번째만) |
| `        pc = 0x0000000100000538  ARM-M14_null`poll_sensor + 16 at demo_crash_null.c:36:17` | `parse_reg_line` | 번호 32, 값, 기호 |
| `->  0x100000538 <+16>: ldr    x8, [x0, #0x18]` | `parse_disasm_line` → `parse_insn` | 주소, pc 표시, 명령 |
| `0x16fdfea28: 0x00000001000004f8 ARM-M14_stack`set_label + 48 at …` | `parse_dump` 안 | 메모리 워드 (+기호) |
| `[0x00000001000004f8-0x00000001000005c8)  r-x … ARM-M14_null.__TEXT.__text` | `parse_dump` 안 | 코드 범위 |

함정 몇 개: `frame #0: 0x… ` 줄은 레지스터 줄이 아니다 (이름 뒤에 `=` 가 없다). `Process 9170 exited with status = 9 (0x00000009)` 도 `=` 앞이 레지스터 이름이 아니라서 걸러진다. `x31` 은 A64 에 없는 이름이다 (31번 인코딩은 sp 또는 xzr). 16자리를 넘는 16진수는 거부한다.

### 크래시 1 — NULL 구조체 멤버 (`crash_null.txt`, 실제 출력 발췌)

```text
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0x18)
    frame #0: 0x0000000100000538 ARM-M14_null`poll_sensor(s=0x0000000000000000) at demo_crash_null.c:36:17 [opt]
        x0 = 0x0000000000000000
        x8 = 0x0000000000000007
        fp = 0x000000016fdfea60
        lr = 0x000000010000059c  ARM-M14_null`main + 64 at demo_crash_null.c:45:5
        sp = 0x000000016fdfea50
        pc = 0x0000000100000538  ARM-M14_null`poll_sensor + 16 at demo_crash_null.c:36:17
     far = 0x0000000000000018
     esr = 0x92000006
    0x100000534 <+12>: mov    x19, x0
->  0x100000538 <+16>: ldr    x8, [x0, #0x18]
    0x10000053c <+20>: blr    x8
    frame #1: 0x000000010000059c ARM-M14_null`main(argc=<unavailable>, argv=<unavailable>) at demo_crash_null.c:45:38 [opt]
```

| 순서 | 관찰 | 추론 |
|---|---|---|
| 1 | `address=0x18`, `far = 0x18`, code=1 (`KERN_INVALID_ADDRESS`) | 0 근처 주소 → NULL + 작은 offset 의심 |
| 2 | `->` 줄 `ldr x8, [x0, #0x18]` | 베이스 x0, 즉치 0x18 (64비트 로드 = 포인터) |
| 3 | x0 = 0 | 0 + 0x18 = 0x18 = fault 주소 ✔ → **NULL_MEMBER** |
| 4 | x0 을 쓴 명령이 함수 안에 없음 (`mov x19, x0` 은 x0 을 *읽는다*) | x0 = 첫 번째 인자 `s` → 원인은 호출자 |
| 5 | `bt`: frame #1 = `main … demo_crash_null.c:45:38` | `poll_sensor(s)` 를 부른 줄. `find_sensor(7)` 가 NULL 을 돌려줬다 |
| 6 | lr = `main + 64`, `__text` 안, pc 와 다름 | 프레임 정상 — 스택 손상 아님 |

offset 0x18 → 멤버 이름은 구조체 배치표가 답한다. `ldr x8, …` 다음이 `blr x8` 이니 "함수 포인터 멤버를 읽어 호출" 이고, 표에서 0x18 은 `read` 다.

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">struct sensor 배치 (offsetof, 40 B) — 유효 주소 = x0(0) + 0x18</text>
<g font-family="monospace" fill="currentColor" font-size="12">
<text x="20" y="48">+0x00</text><text x="20" y="78">+0x08</text><text x="20" y="108">+0x10</text>
<text x="20" y="138">+0x18</text><text x="20" y="168">+0x20</text>
</g>
<rect x="70" y="34" width="110" height="24" class="f-mut" stroke="currentColor"/>
<rect x="180" y="34" width="110" height="24" class="f-mut" stroke="currentColor"/>
<rect x="70" y="64" width="220" height="24" class="f-mut" stroke="currentColor"/>
<rect x="70" y="94" width="110" height="24" class="f-mut" stroke="currentColor"/>
<rect x="180" y="94" width="110" height="24" class="f-mut" stroke="currentColor"/>
<rect x="70" y="124" width="220" height="24" class="f-acc" stroke="currentColor"/>
<rect x="70" y="154" width="220" height="24" class="f-mut" stroke="currentColor"/>
<g font-family="monospace" fill="currentColor" font-size="12">
<text x="80" y="50">id</text><text x="190" y="50">flags</text><text x="80" y="80">name (char*)</text>
<text x="80" y="110">last_mC</text><text x="190" y="110">pad</text>
<text x="80" y="140">read (함수 포인터)</text><text x="80" y="170">sample_count</text>
</g>
<text x="310" y="140" fill="currentColor" font-family="monospace">← ldr x8, [x0, #0x18]   x0 = 0</text>
<text x="10" y="210" fill="currentColor" font-weight="bold">프로세스 주소 공간 (image dump sections, 실측)</text>
<rect x="20" y="222" width="330" height="26" class="f-warn" stroke="currentColor"/>
<rect x="350" y="222" width="150" height="26" class="f-ok" stroke="currentColor"/>
<g font-family="monospace" fill="currentColor" font-size="12">
<text x="28" y="240">__PAGEZERO [0, 0x100000000) 권한 ---</text>
<text x="358" y="240">__TEXT 0x1_0000_0000…</text>
<text x="510" y="240">0x18 은 여기(왼쪽)에</text>
</g>
</svg>
```

macOS 64비트 실행 파일은 처음 4 GB 를 `__PAGEZERO`(권한 없음)로 막아 둔다. 그래서 0x18 같은 "NULL + offset" 은 물론, 32비트로 잘린 포인터(상위 32비트가 0이 된 값)도 바로 fault 가 난다. ESR `0x92000006` = EC 0x24(하위 EL 의 data abort) · IL 1 · DFSC 0x06(translation fault, level 2) · WnR 0(읽기) — 추가 연습 1 에서 디코드한다 (필드 위치는 Arm ARM 기준, 확인 권장).

### 크래시 2 — 스택 오버런이 LR 을 덮었다 (`crash_stack.txt`, 실제 출력 발췌)

```text
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=257, address=0x4242424242424242)
    frame #0: 0x0000424242424242
        fp = 0x0000414141414141
        lr = 0x4242424242424242
        sp = 0x000000016fdfea60
        pc = 0x0000424242424242
     far = 0x4242424242424242
     esr = 0x8a000000
Number of bits used in addressing (code): 47
(lldb) bt
  * frame #0: 0x0000424242424242
(lldb) memory read -f A -s 8 -c 12 `$sp - 0x50`
...
0x16fdfea20: 0x000000016fdfea50
0x16fdfea28: 0x00000001000004f8 ARM-M14_stack`set_label + 48 at demo_crash_stack.c:29:12
0x16fdfea30: 0x0000000000000054
0x16fdfea38: 0x000000018757b750 dyld`lsl::MemoryManager::writeProtect(bool) + 196
0x16fdfea40: 0x532d373131504d54
0x16fdfea48: 0x31302d524f534e45
0x16fdfea50: 0x4141414141414141
0x16fdfea58: 0x4242424242424242
0x16fdfea60: 0x4343434343434343
0x16fdfea68: 0x4444444444444444
(lldb) memory read -f c -c 48 `$sp - 0x20`
0x16fdfea40: TMP117-SENSOR-01AAAAAAAABBBBBBBB
0x16fdfea60: CCCCCCCCDDDDDDDD
error: memory read failed for 0x424242424200
error: Failed to disassemble memory at 0x424242424242.
```

읽는 법:

1. **pc 가 코드가 아니다.** `0x…4242` 는 4의 배수도 아니고 `__TEXT.__text` `[0x1000004b0, 0x100000580)` 밖이다. `disassemble` 도 실패한다. code=257 = `EXC_ARM_DA_ALIGN`(0x101, SDK `mach/arm/exception.h`) 이고 ESR `0x8a000000` 의 EC = 0x22 = **PC alignment fault** — 하위 2비트가 `10` 인 주소로 점프했기 때문이다. 4의 배수인 쓰레기였다면 instruction abort(EC 0x20)가 났을 것이다 (추론, 이 레슨에서는 미측정).
2. **lr == pc.** `ret` 은 `br x30` 이므로, x30 이 쓰레기면 pc = x30 이 된다. "lr == pc 이고 둘 다 쓰레기" = **깨진 LR 로 ret 했다**. (`blr x8` 로 쓰레기 함수 포인터를 부른 경우는 lr 이 멀쩡한 리턴 주소라 구별된다.)
3. **값이 ASCII 다.** `0x42` = 'B', fp `0x…4141` = 'A'. 프로그램이 다루는 *데이터*가 저장된 x29/x30 자리를 덮었다 → 스택 버퍼 오버런.
4. **lldb 가 상위 비트를 지운다.** pc/fp 는 `0x0000424242424242` 로, far/lr 은 원래 값으로 보인다. lldb 는 `Number of bits used in addressing (code): 47` 를 보고 코드 포인터의 상위 17비트를 PAC/TBI 비트로 간주해 떼고 보여 준다. 비교할 때는 47비트 마스크를 씌워야 lr == pc 가 성립한다 (도구의 `lr_eq_pc`).
5. **bt 는 포기, 죽은 스택을 본다.** fp 가 `0x…414141` 이라 프레임 사슬이 첫 고리에서 끊긴다. 대신 sp 아래 `0x16fdfea28` 에 `set_label + 48` 이 남아 있다 — `set_label` 이 부른 printf 의 프레임 레코드(저장 fp `0x16fdfea50` = set_label 의 fp, 저장 lr = set_label 안 리턴 주소)가 지워지지 않은 것이다. 그러니 **마지막으로 실행되던 함수 = set_label**, 그 `ret` 이 깨진 LR 을 썼다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">set_label 프레임 (sub sp,#0x30 / stp x29,x30,[sp,#0x20]) — 실측 주소</text>
<g font-family="monospace" font-size="12" fill="currentColor">
<text x="10" y="58">0x16fdfea68</text><text x="10" y="86">0x16fdfea60</text><text x="10" y="114">0x16fdfea58</text>
<text x="10" y="142">0x16fdfea50</text><text x="10" y="170">0x16fdfea48</text><text x="10" y="198">0x16fdfea40</text>
<text x="10" y="226">0x16fdfea38</text><text x="10" y="254">0x16fdfea30</text><text x="10" y="282">0x16fdfea28</text>
<text x="10" y="310">0x16fdfea20</text>
</g>
<rect x="110" y="42" width="300" height="56" class="f-mut" stroke="currentColor"/>
<rect x="110" y="98" width="300" height="28" class="f-warn" stroke="currentColor"/>
<rect x="110" y="126" width="300" height="28" class="f-warn" stroke="currentColor"/>
<rect x="110" y="154" width="300" height="56" class="f-acc" stroke="currentColor"/>
<rect x="110" y="210" width="300" height="56" class="f-mut" stroke="currentColor"/>
<rect x="110" y="266" width="300" height="56" class="f-ok" stroke="currentColor"/>
<g font-family="monospace" font-size="12" fill="currentColor">
<text x="120" y="64">main 의 [sp], [sp+8]</text><text x="120" y="84">"CCCCCCCC" "DDDDDDDD"</text>
<text x="120" y="117">저장 x30 → "BBBBBBBB"</text>
<text x="120" y="145">저장 x29 → "AAAAAAAA"</text>
<text x="120" y="176">label[16] = "TMP117-SENSOR-01"</text>
<text x="120" y="232">printf 인자 슬롯 ('T'=0x54) 등</text>
<text x="120" y="286">[죽은] printf 의 저장 lr</text><text x="120" y="304">= set_label + 48</text>
</g>
<text x="420" y="88" fill="currentColor">← 크래시 때 sp (ret 이 0x30 올림)</text>
<text x="420" y="134" fill="currentColor">← ldp x29, x30, [sp, #0x20] 이 읽음</text>
<text x="420" y="190" fill="currentColor">↑ copy_name 이 48 B 를 위로 씀</text>
<text x="420" y="254" fill="currentColor">← set_label 의 sp</text>
<text x="420" y="300" fill="currentColor">← stale_return_scan 이 찾는 값</text>
</svg>
```

clang 의 AArch64 프레임은 **지역 변수가 프레임 레코드(x29, x30) 아래**에 있다. 그래서 지역 배열이 위로 넘치면 *자기 자신의* 저장 LR 을 덮고, 바로 그 함수의 `ret` 에서 터진다. 덮인 바이트 위치도 계산된다: label 16 B + 저장 x29 8 B → LR 은 버퍼 시작에서 **24 B** (추가 연습 2 가 순환 패턴으로 실측 확인).

**PAC 이 켜지면 증상이 바뀐다** — 같은 소스를 세 가지로 빌드해 실제로 돌린 결과:

| 빌드 | 프롤로그/에필로그 | 실측 증상 |
|---|---|---|
| `arm64` 기본 | `stp x29, x30` … `ret` (PAC 명령 0개, `objdump | grep -c paci` = 0) | code=257, pc = `0x…4242`, bt 불가 |
| `arm64` + `-mbranch-protection=pac-ret` | `paciasp` … `retaa` | **똑같이** code=257, pc = `0x…4242` — 이 맥의 arm64 프로세스에선 A 키 인증이 걸리지 않았다 (관찰; 커널이 arm64 프로세스에 PAC 를 켜 주지 않는 것으로 보임, 확인 권장) |
| `-arch arm64e` | `pacibsp` … `retab` | code=261 = `EXC_ARM_PAC_FAIL`, **pc = `set_label + 64` (retab 자체)**, ESR `0x72000001` (EC 0x1C = FPAC), `address=0xd65f0fff` = `retab` 명령 인코딩 |

`crash_stack_arm64e.txt` 발췌 (실제):

```text
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=261, address=0xd65f0fff)
      pc = 0x0000000100000508  ARM-M14_stack_e`set_label + 64 at demo_crash_stack.c:29:5
      lr = 0x4242424242424242
     x16 = 0x516c00018fb635e4 (0x000000018fb635e4) libsystem_kernel.dylib`__error
     esr = 0x72000001
->  0x100000508 <+64>: retab
```

FEAT_FPAC(`sysctl hw.optional.arm.FEAT_FPAC: 1`)가 있으면 인증 실패가 **그 자리에서** 예외가 된다. 그래서 arm64e 는 "어디로 튀었나" 대신 "어느 함수의 ret 이 실패했나" 를 바로 보여 준다 — 포렌식에는 오히려 쉽다. 이 맥(macOS 27.2 내부 빌드)에서는 서드파티 arm64e 실행이 됐지만, 일반 macOS 에서는 부트 인자 없이는 막힐 수 있다 (확인 권장).

### 크래시 3 — 해제 후 사용 (UAF) 와 할당자의 "0 채우기"

과제 설명은 "`MallocScribble=1` 이면 해제 메모리가 0x55" 였지만, **이 맥에서는 그대로 재현되지 않았다.** 48 B 를 0x11 로 채우고 free 한 뒤 같은 주소를 읽는 탐침(probe)으로 잰 결과 (각 5회, 같은 결과):

| 환경 변수 | free 후 블록 내용 (8 B 워드 6개) | 해석 |
|---|---|---|
| (없음) | `6, 6, 0, 0, 0, 0` | 기본 할당자가 **free 때 0 으로 지운다** (man malloc: `MallocZeroOnFree`, macOS 13+) |
| `MallocScribble=1` | `6, 6, 0, 0, 0, 0` (새 malloc 은 0xAA…) | 0xAA 는 되는데 0x55 는 안 보인다 |
| `MallocZeroOnFree=0` | `<포인터>, 0x7fb, 0x1111…, …` | 옛 내용이 그대로 남는다 (가장 위험: 조용히 "동작") |
| `MallocZeroOnFree=0 MallocScribble=1` | `0x5555…` × 6 | 교과서대로 0x55 |

그래서 덤프를 두 벌 만들었다. Scribble 판(`crash_uaf.txt`)과 기본 판(`crash_uaf_zero.txt`)에서 뽑은 값 (요약 편집, 원문은 각 파일):

```text
crash_uaf.txt       stop reason = EXC_BAD_ACCESS (code=1, address=0x5555555555555555)
                    x0 = 0x0000000150a04100     x8 = 0x5555555555555555     esr = 0x92000004
crash_uaf_zero.txt  stop reason = EXC_BAD_ACCESS (code=1, address=0x0)
                    x0 = 0x0000000100595370     x8 = 0x0000000000000000     esr = 0x92000006
둘 다:                  0x100000548 <+0>:  ldr    x8, [x0, #0x18]
                    ->  0x10000054c <+4>:  ldr    x1, [x8]
crash_uaf_zero.txt  memory read $x0 → 0x0000000000000004 0x0000000000000004 0 0 0 0
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">def 사슬: "x8 을 누가 썼나" 를 한 칸 더 따라간다</text>
<rect x="10" y="40" width="200" height="54" class="f-mut" stroke="currentColor"/>
<text x="20" y="62" fill="currentColor" font-family="monospace">x0 = r (힙, 이미 free)</text>
<text x="20" y="82" fill="currentColor" font-family="monospace">struct dma_req *</text>
<rect x="270" y="40" width="210" height="54" class="f-warn" stroke="currentColor"/>
<text x="280" y="62" fill="currentColor" font-family="monospace">[x0 + 0x18] = r-&gt;ops</text>
<text x="280" y="82" fill="currentColor" font-family="monospace">0x5555… 또는 0</text>
<rect x="540" y="40" width="190" height="54" class="f-acc" stroke="currentColor"/>
<text x="550" y="62" fill="currentColor" font-family="monospace">ldr x1, [x8]</text>
<text x="550" y="82" fill="currentColor" font-family="monospace">ops-&gt;complete → fault</text>
<g stroke="currentColor"><line x1="210" y1="67" x2="270" y2="67"/><line x1="480" y1="67" x2="540" y2="67"/></g>
<text x="215" y="60" fill="currentColor" font-size="12">+0x18</text>
<text x="485" y="60" fill="currentColor" font-size="12">x8</text>
<text x="10" y="130" fill="currentColor">Scribble 판: 베이스 x8 = 0x5555… → SCRIBBLE (r 이 이미 해제됨)</text>
<text x="10" y="155" fill="currentColor">기본 판: x8 = 0, 주소 = 0 → 겉보기 NULL_MEMBER (ops-&gt;complete)</text>
<text x="10" y="180" fill="currentColor">→ def 를 따라가면 x0 객체 = 헤더 2워드 + 나머지 0 (힌트 zeroed)</text>
<text x="10" y="212" fill="currentColor" font-weight="bold">교훈: 최신 macOS 의 UAF 는 NULL 역참조처럼 보인다</text>
<text x="10" y="236" fill="currentColor">진짜 도구 = ASan (해제 위치 demo_crash_uaf.c:42 까지 출력)</text>
</svg>
```

lldb 의 `bt` frame #1 은 `main … demo_crash_uaf.c:43:19` 인데, 같은 덤프의 `lr = main + 120 at demo_crash_uaf.c:44:25` 는 44행이다. lr 은 `bl` **다음** 명령(= 다음 줄 printf 의 시작)을 가리키기 때문이다. lldb 는 호출자 프레임을 기호화할 때 리턴 주소에서 1을 빼서 `bl` 쪽 줄을 보여 준다. 사람이 lr 로 줄을 찾을 때도 **lr − 4** 를 써야 호출한 줄이 나온다 (추가 연습 3 에서 라인 테이블로 확인: 0x1000005d0 → 44행, 0x1000005cc → 43행).

참고로 ASan 빌드(`-fsanitize=address`)는 같은 버그를 이렇게 잡는다 (실제 출력 발췌):

```text
==63397==ERROR: AddressSanitizer: heap-use-after-free on address 0x604000000428 at pc 0x0001008048ec ...
READ of size 8 at 0x604000000428 thread T0
    #0 0x0001008048e8 in finish_req demo_crash_uaf.c:32
0x604000000428 is located 24 bytes inside of 40-byte region [0x604000000410,0x604000000438)
freed by thread T0 here:
    #1 0x0001008049fc in main demo_crash_uaf.c:42
```

"24 bytes inside" = offset 0x18 = `ops`. 필드 크래시 덤프에는 이런 정보가 없으니, 위의 손 추론이 면접에서 요구되는 기술이다.

### Part B — 분류 규칙의 이유

| 규칙 | 왜 이 순서인가 |
|---|---|
| EXC_BAD_ACCESS + pc 필수 | breakpoint 정지나 잘린 덤프에서 "원인" 을 지어내지 않는다 |
| code 261 먼저 | PAC 실패는 pc 가 멀쩡해서 아래 규칙으로는 UNKNOWN 이 된다 |
| BAD_PC 를 명령보다 먼저 | pc 가 쓰레기면 `->` 줄 자체가 없다 |
| SCRIBBLE 을 NULL 보다 먼저 | 0x5555… + offset 은 "작은 주소" 가 아니지만, 순서를 명시해 두면 규칙 추가 때 안전하다 |
| NULL: 베이스 = 0 **그리고** 주소 = 유효주소 | x0 = 0x1000 인데 주소 0x18 이면 다른 이야기다 (합성 #6). 레지스터 오프셋 `[x0, x1, lsl #3]` 은 x1 을 모르므로 UNKNOWN (합성 #12) |
| `< 0x10000` | NULL + 구조체 offset 은 작다. 큰 값은 쓰레기 포인터일 가능성이 높아 다른 분류가 필요하다 (도구는 UNKNOWN) |

### Part C — solution 실제 출력

```text
Part A (line parsers): 32/32 PASS
Part B (parse_dump + classify): 31/31 PASS
Part C (triage report): 17/17 PASS
[crash_null.txt] NULL_MEMBER func=poll_sensor line=demo_crash_null.c:36 caller=main base=x0 off=0x18 def=(argument from caller)
[crash_stack.txt] BAD_PC      func=set_label line=demo_crash_stack.c:29 caller=? lr==pc:yes stale_ra=0x1000004f8@0x16fdfea28
[crash_stack_arm64e.txt] PAC_FAIL    func=set_label line=demo_crash_stack.c:29 caller=?
[crash_uaf.txt] SCRIBBLE    func=finish_req line=demo_crash_uaf.c:32 caller=main base=x8 off=0x0 def="ldr    x8, [x0, #0x18]" hint=fill(0x55/0xAA)
[crash_uaf_zero.txt] NULL_MEMBER func=finish_req line=demo_crash_uaf.c:32 caller=main base=x8 off=0x0 def="ldr    x8, [x0, #0x18]" hint=zeroed
80/80 passed
```

면접 답으로 옮기면:

| 덤프 | 원인 라인 | 한 줄 설명 |
|---|---|---|
| null | 36 `s->read()` (호출: 45행) | `find_sensor(7)` 가 NULL → `s->read` 를 읽는 `ldr x8, [x0, #0x18]` 에서 fault, 0x18 = `read` |
| stack | 27 `copy_name(label, rx, n)` (터진 곳: 29행 `ret`) | `set_label` 의 16 B `label` 에 48 B 복사 → 저장 x29/x30 = "AAAA…"/"BBBB…" → `ret` 이 0x4242… 로 |
| arm64e | 같음 (터진 곳: `retab`, 29행) | 인증 실패로 `retab` 에서 바로 정지 |
| uaf | 43 `finish_req(g_queue[0])` (free: 42행) | 해제된 `r` 의 `ops`(+0x18)를 읽어 0x5555… / 0 → `ops->complete` 읽기에서 fault |

도구가 직접 주는 것은 "터진 라인" 까지다. 27행·42행 같은 **진짜 원인 라인**은 마지막 한 걸음 — 그 함수에서 저장 LR 아래 버퍼에 쓰는 코드(copy_name), 그 객체를 free 한 코드 — 을 사람이 소스에서 찾는다. 면접에서도 "터진 곳 ≠ 원인" 을 분명히 말하면 점수가 된다.

`.ips` 리포트: lldb 밖에서 직접 죽였을 때는 `~/Library/Logs/DiagnosticReports/ARM-M14_*.ips` 가 남았다 (lldb 아래서 죽인 것은 남지 않았다). JSON 안에 같은 정보가 있다 — 예: `"exception": {"type": "EXC_BAD_ACCESS", "signal": "SIGSEGV", "subtype": "KERN_INVALID_ADDRESS at 0x0000000000000018"}`, `"esr": {"value": 2449473542, "description": "(Data Abort) byte read Translation fault"}`, 스택 판은 `"subtype": "EXC_ARM_DA_ALIGN at 0x4242424242424242"`, `"esr": {"description": "(PC alignment)"}`, arm64e 판은 `"EXC_ARM_PAC_FAIL at 0x00000000d65f0fff"`. 단, ASLR 이 켜져 있어 pc 가 `0x100944538` 처럼 밀려 있다 (lldb 는 기본으로 ASLR 을 끈다). 그래서 리포트의 `imageOffset`(여기서 0x538)이나 `atos -l <load address>` 로 바꿔 읽어야 한다 (TOOL-M14).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/ARM-M14_solution && /tmp/ARM-M14_solution   # 80/80, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/ARM-M14_asan && /tmp/ARM-M14_asan   # 깨끗
```

## 7. 자주 하는 실수

**1. fault 주소를 pc 로 착각한다.** `address=0x18` 은 *데이터* 주소다. pc 는 `0x100000538` 로 멀쩡하다. 둘을 섞으면 "0x18 번지 코드가 이상하다" 같은 엉뚱한 결론이 나온다. 고친 습관: stop reason 의 주소 = `far`, 명령 위치 = `pc`, 두 줄을 따로 적는다.

**2. lr 의 줄 번호를 호출 줄로 쓴다.** `lr = main + 120 at …:44:25` 는 호출 *다음* 줄이다. 고친 코드 (추가 연습 3):

```c
const lte_t *x = line_lookup(U, nu, lr - 4);   /* bl 자체의 주소 → 43행 */
```

**3. 스택 오버런 덤프에서 `bt` 를 믿는다.** fp 가 `0x…414141` 이면 lldb 의 프레임 사슬이 첫 칸에서 끊기고 frame #0 하나만 나온다. 이때 "프레임이 하나뿐이니 main 에서 죽었다" 는 틀린 결론이다. 고친 방법: `memory read -f A` 로 sp 아래 죽은 스택을 훑는다 (`stale_return_scan`).

**4. `-fno-stack-protector` 없이 재현하고 "LR 이 안 덮였다" 고 결론낸다.** 이 맥의 Apple clang 은 기본으로 스택 보호(canary)를 넣는다. 같은 데모를 플래그 없이 빌드하면 실측으로 `exit=134` (SIGABRT), `bt` = `__stack_chk_fail` ← `set_label` 이다 — LR 로 `ret` 하기 **전에** 에필로그의 canary 검사가 막는다. 필드 크래시에서 `__stack_chk_fail` 이 보이면 그 자체가 "이 함수 지역 버퍼가 넘쳤다" 는 증거다.

**5. UAF 를 NULL 역참조로 닫는다.** `crash_uaf_zero.txt` 는 x8 = 0, `address=0x0` 이라 분류만 보면 NULL_MEMBER 다. 하지만 x8 은 `ldr x8, [x0, #0x18]` 로 읽은 값이고 x0 는 힙 주소다 → "왜 그 객체의 ops 가 0 인가?" 를 물어야 한다. 고친 코드: 분류 뒤에 `find_def` + `object_hint` 를 반드시 본다.

**6. 47비트 마스크 없이 lr 과 pc 를 비교한다.** lldb 는 pc 를 `0x0000424242424242` 로, lr 은 `0x4242424242424242` 로 보여 줬다. `lr == pc` 를 그대로 비교하면 거짓이다.

```c
r->lr_eq_pc = ((d->reg[R_LR] ^ d->reg[R_PC]) & ((1ull << 47) - 1)) == 0;
```

## 8. 면접 Q&A

<details><summary>Q1. 크래시 덤프를 받으면 무엇부터 보나요?</summary>

fault 주소와 예외 종류 → pc 의 명령 → 그 명령의 베이스 레지스터 값 → 그 레지스터를 쓴 명령 → 호출 사슬 → lr 상식 검사 순서. 첫 세 단계로 "무엇이 잘못된 값인가" 를, 넷째부터 "그 값이 어디서 왔나" 를 찾는다. 이 레슨의 NULL 사례는 3단계(x0 = 0)에서, UAF 사례는 4단계(x8 ← `[x0, #0x18]`)에서 답이 나온다.
</details>

<details><summary>Q2. fault 주소가 0x18 이면 무엇을 알 수 있나요? 0x7fff_xxxx 이면요?</summary>

0 근처의 작은 값은 NULL 포인터 + 구조체 멤버 offset 이다. `ldr x8, [x0, #0x18]` 의 즉치와 같다면 확정이고, offset 표(`offsetof`)로 멤버 이름까지 나온다. 큰 값은 다르다: 스택 근처면 지역 배열 범위 밖 접근, 0x5555…/0xAAAA… 면 malloc 채움 패턴(UAF/미초기화), ASCII 처럼 보이면 데이터가 포인터 자리에 들어간 것(오버런), 상위 32비트가 0 인 큰 값이면 32비트 잘림을 의심한다.
</details>

<details><summary>Q3. pc 가 0x4242424242424242 입니다. 어떻게 된 건가요? bt 는 왜 한 줄인가요?</summary>

pc 가 ASCII('B') 이고 lr 도 같은 값이면, 저장된 x30 이 데이터로 덮였고 `ret`(= `br x30`)이 그 값으로 점프한 것이다. 이번 실측에서는 하위 2비트가 `10` 이라 PC alignment fault(EC 0x22, Mach code 0x101)가 났다. 에필로그가 x29 도 같은 자리에서 복원하므로 fp 역시 쓰레기('A')가 되고, fp 사슬로 되감는 `bt` 는 첫 고리에서 끊긴다. 대신 sp 아래 죽은 스택에 남은 리턴 주소(`set_label + 48`)로 범인 함수를 찾는다.
</details>

<details><summary>Q4. 꼬리: AArch64 에서 지역 버퍼가 넘치면 항상 자기 함수의 LR 이 덮이나요?</summary>

컴파일러 프레임 배치에 달렸다. 이번 clang 출력은 `sub sp, sp, #0x30; stp x29, x30, [sp, #0x20]` — 프레임 레코드가 지역 변수 *위*에 있어 자기 LR 이 덮였고, 바로 그 함수의 `ret` 에서 터졌다. GCC 는 전통적으로 프레임 레코드를 프레임 *아래쪽*(sp 쪽)에 두는 배치를 써서, 지역 버퍼가 넘치면 *호출자의* 저장 LR 이 덮이고 크래시가 한 단계 늦게 난다고 알려져 있다 (확인 권장). 그래서 "터진 함수 = 넘친 함수" 라고 단정하지 말고 프레임 배치를 확인해야 한다.
</details>

<details><summary>Q5. PAC 이 켜지면 같은 버그가 어떻게 보이나요?</summary>

arm64e 빌드는 프롤로그에서 `pacibsp` 로 LR 에 서명하고 `retab` 에서 검증한다. 덮인 LR 은 서명이 맞지 않으므로, FEAT_FPAC 가 있는 이 맥에서는 `retab` 자체에서 예외가 난다: `EXC_BAD_ACCESS (code=261 = EXC_ARM_PAC_FAIL)`, pc = `set_label + 64`, ESR EC 0x1C. 쓰레기 주소로 점프하지 않으니 공격이 막히고, 디버깅은 오히려 쉬워진다 (범인 함수가 pc 에 그대로). 반면 일반 arm64 바이너리에 `-mbranch-protection=pac-ret` 을 줘도 이 맥에서는 증상이 바뀌지 않았다 (관찰).
</details>

<details><summary>Q6. 해제 후 사용(UAF)은 덤프에서 어떻게 알아보나요? MallocScribble 이면 끝인가요?</summary>

교과서 답은 "포인터/주소가 0x5555… 면 free 된 메모리" (MallocScribble, 새 할당은 0xAA). 하지만 이 맥(macOS 27.2)의 기본 할당자는 free 때 블록을 0 으로 지워서, `MallocScribble=1` 만으로는 0x55 가 안 보였고 `MallocZeroOnFree=0` 를 같이 줘야 했다. 기본 상태의 UAF 는 "NULL 역참조" 로 보인다. 그래서 베이스 레지스터의 출처를 한 칸 더 따라가 "그 NULL 을 담고 있던 객체가 왜 0 인가" 를 봐야 한다. 실제 해결 도구는 ASan(할당/해제 스택까지 출력)이고, Guard Malloc(`libgmalloc`)은 해제 페이지를 접근 불가로 만들어 첫 접근에서 죽게 한다.
</details>

<details><summary>Q7. lr 이 가리키는 줄과 bt 의 호출자 줄이 왜 다른가요?</summary>

lr 은 `bl` 다음 명령의 주소다. 그 명령은 다음 소스 줄에 속할 수 있다 (UAF 덤프: lr → 44행, 실제 호출은 43행). 디버거와 심볼리케이터는 호출자 프레임을 기호화할 때 리턴 주소 − 1 을 쓴다. 손으로 addr2line/atos 를 돌릴 때도 호출자 프레임은 lr − 4 로 찾는다. 단, frame #0 의 pc 는 빼지 않는다 (그 명령에서 fault 가 났으므로).
</details>

<details><summary>Q8. 덤프의 주소가 lldb 에서 본 것과 .ips 에서 본 것이 다릅니다.</summary>

lldb 는 기본으로 ASLR 을 끄고 실행하므로 이미지가 `0x100000000` 에 놓인다. lldb 밖 실행은 슬라이드가 붙어 이번 `.ips` 에서는 pc = `0x100944538` 이었다. 비교할 값은 이미지 시작 기준 offset(`imageOffset` = 0x538)이고, `atos -o <binary> -l <load address> <pc>` 나 lldb 의 `image lookup -a` 로 소스 줄을 얻는다. 펌웨어(Cortex-M)는 보통 ASLR 이 없어서 `addr2line -e fw.elf <pc>` 가 바로 통한다 (ARM-M12 의 HardFault 분석과 같은 절차).
</details>

<details><summary>Q9. Cortex-M 에서는 이 절차가 어떻게 바뀌나요?</summary>

같은 순서를 쓰되 재료가 다르다. fault 주소는 BFAR/MMFAR(VALID 비트가 서 있을 때만), 예외 종류는 CFSR/HFSR, pc/lr 은 예외 스택 프레임(r0–r3, r12, lr, pc, xPSR)에서 꺼낸다. NULL + offset 패턴은 똑같이 보이지만, 많은 MCU 는 0번지에 플래시(벡터 테이블)가 매핑돼 있어 NULL **읽기**가 fault 없이 엉뚱한 값을 돌려줄 수 있다 — 그래서 MPU 로 0 근처를 막아 두는 펌웨어가 많다. 자세한 decoder 는 ARM-M12, 정책(리셋, .noinit 기록)은 BM-M13.
</details>

## 9. 추가 연습

### 연습 1 — ESR 디코더

`esr_t decode_esr(uint32_t esr)` — EC[31:26], IL[25], ISS[24:0]. data/instruction abort 면 FSC[5:0] 을 종류(address size / translation / access flag / permission / alignment)와 단계(level 0–3)로, data abort 면 WnR[6] 까지. EC 0x1C(FPAC)는 ISS[1](데이터 키?)와 ISS[0](B 키?). 실제 덤프의 esr 4개 + ARM-B09 의 `0x92000007` + 합성 3개로 검증.

<details><summary>답</summary>

| ESR (출처) | EC | 해석 |
|---|---|---|
| `0x92000006` (null) | 0x24 | data abort, lower EL · translation fault L2 · 읽기 |
| `0x92000004` (uaf 0x5555…) | 0x24 | translation fault **L0** — 사용자 VA 범위를 벗어난 주소라 맨 위 단계에서 실패한 것으로 보인다 (추정) |
| `0x8a000000` (stack) | 0x22 | PC alignment |
| `0x72000001` (arm64e) | 0x1C | PAC fail, 명령 키, **B 키** (`retab`) |
| `0x92000007` (ARM-B09 가드 페이지) | 0x24 | translation fault L3 (마지막 단계 PTE 없음) |

`fsc >> 2` 가 0–3 이면 종류, `fsc & 3` 이 단계다. 0x21 은 정렬 fault. EC/ISS 비트 위치는 Arm ARM D19 (ESR_EL1) 기준, 확인 권장.
</details>

### 연습 2 — 순환 패턴으로 LR 까지 거리 재기

`pattern_create(buf, n)` (Metasploit `Aa0Aa1…` 형식)와 `pattern_offset(pat, value, mask)`. `crash_pattern.txt` (데모를 `pattern` 인자로 실행)의 lr/fp/pc 로 "버퍼 시작 → 저장 LR" 거리를 구한다. fp/pc 는 lldb 가 상위 비트를 지웠으니 47비트 마스크로 비교.

<details><summary>답</summary>

실측 `lr = 0x6241396141386141` → 리틀엔디언 바이트 "Aa8Aa9Ab" → 패턴 offset **24**. fp `0x0000413661413561` 은 마스크 없이는 못 찾고, 47비트 마스크로 offset **16** (저장 x29 자리). 즉 `label`(16 B) 바로 위가 x29, 그 위가 x30 — 프레임 그림과 일치. 3글자 묶음 패턴은 2000 B 안에서 모든 8바이트 창이 서로 달라 offset 이 유일하다 (테스트로 확인).
</details>

### 연습 3 — addr2line: pc · lr · lr−4

`load_line_table()` 로 `image dump line-table` 출력을 표로 읽고, `line_lookup()` 으로 "주소 이하 최대 항목" 을 이분 탐색한다.

<details><summary>답</summary>

| 주소 | 의미 | 결과 |
|---|---|---|
| `0x100000538` | null pc | 36행 17열 |
| `0x10000059c − 4` | null lr−4 | 45행 38열 (`poll_sensor(s)` 호출) |
| `0x1000005d0` | uaf lr | 44행 (printf — 틀린 답) |
| `0x1000005cc` | uaf lr−4 | 43행 19열 (`finish_req(g_queue[0])` — 맞는 답) |
| `0x10000057c` | 줄 번호 없는 항목 | 0 (컴파일러가 만든 코드) |
</details>

### 연습 4 — 포인터 정규화와 ASCII 판정

`canon47(v)`, `same_code_ptr(a, b)`, `ascii8(v, out)`.

<details><summary>답</summary>

arm64e x16 `0x516c00018fb635e4` → `canon47` = `0x18fb635e4` (lldb 가 괄호에 보여 준 값과 같다; 상위 비트는 PAC 서명이라 실행마다 바뀐다 — 앞선 실행에서는 `0x1f378…` 였다). 스택 판의 lr `0x4242…` 와 pc `0x0000424242424242` 는 47비트로 같은 포인터. `ascii8(0x4242424242424242)` = "BBBBBBBB", 죽은 스택 `0x532d373131504d54` = "TMP117-S" — 레지스터나 스택 워드가 출력 가능한 ASCII 8글자면 "문자열이 포인터 자리에 들어갔다" 는 강한 신호다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cd lessons/ARM/ARM-M14
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/ARM-M14_practice && /tmp/ARM-M14_practice   # 33/33 passed
```

## 10. 핵심 정리

- 읽는 순서: **fault 주소 → pc 의 명령 → 베이스 값 → 누가 그 값을 썼나 → bt/죽은 스택 → lr 검사**. 터진 곳과 원인 라인은 다를 수 있다.
- 작은 fault 주소 = NULL + offset → `ldr x8, [x0, #0x18]` 의 즉치와 `offsetof` 로 멤버 이름 (`read`).
- lr == pc == ASCII → 저장된 x30 이 데이터로 덮였고 `ret` 이 점프. bt 대신 sp 아래 죽은 스택의 리턴 주소(`set_label + 48`).
- Mach code: 1 = KERN_INVALID_ADDRESS, 257 = EXC_ARM_DA_ALIGN (PC alignment 포함), 261 = EXC_ARM_PAC_FAIL. ESR EC 0x24 data abort / 0x22 PC align / 0x1C FPAC.
- arm64e(PAC)는 깨진 LR 을 `retab` 에서 바로 잡는다. 이 맥의 arm64 + `pac-ret` 은 증상이 그대로였다.
- macOS 27.2 기본 할당자는 free 때 0 으로 지운다 → UAF 가 NULL 처럼 보인다. 0x55 를 보려면 `MallocZeroOnFree=0 MallocScribble=1`. 진짜 도구는 ASan.
- lldb: 47비트 주소 마스크로 pc/fp 상위 비트를 지워 보여 주고, ASLR 을 끈다. 호출자 줄은 lr − 4.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — ESR_EL1 (EC, IL, ISS, DFSC/IFSC, WnR), FEAT_PAuth / FEAT_FPAC: https://developer.arm.com/documentation/ddi0487/latest
- Apple SDK `mach/arm/exception.h` (`EXC_ARM_DA_ALIGN` 0x101, `EXC_ARM_PAC_FAIL` 0x105) — `/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk/usr/include/mach/arm/exception.h`
- `man malloc` — MallocScribble, MallocZeroOnFree, MallocCheckZeroOnFreeCorruption
- Apple: Diagnosing issues using crash reports and device logs / Understanding the exception types in a crash report: https://developer.apple.com/documentation/xcode/understanding-the-exception-types-in-a-crash-report
- Apple: Preparing your app to work with pointer authentication (arm64e): https://developer.apple.com/documentation/security/preparing-your-app-to-work-with-pointer-authentication
- LLDB command map (GDB ↔ LLDB): https://lldb.llvm.org/use/map.html
- 관련 레슨: ARM-B08 (프레임 레코드, fp 사슬), ARM-B09 (lldb 배치 세션, 가드 페이지 크래시), ARM-M12 (Cortex-M HardFault 분석기), BM-M13 (fault 정책), TOOL-M14 (DWARF/addr2line/atos, 다음 단계)
