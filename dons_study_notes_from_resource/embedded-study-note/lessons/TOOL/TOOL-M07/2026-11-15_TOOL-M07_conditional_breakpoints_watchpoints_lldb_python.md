---
id: TOOL-M07
time: 80분
updated: 2026-11-15
---
# TOOL-M07 디버거 심화: 조건부 BP·워치포인트·스크립팅

## 학습 목표

- "어느 tick 에서 이웃 필드가 깨진다" 류의 버그를 **하드웨어 워치포인트(hardware watchpoint) + 조건**으로 한 번에 범인 줄까지 몰아가고, 멈춘 PC 가 왜 범인 줄 *다음* 줄인지(AArch64 / Thumb) 설명할 수 있다.
- 조건부 breakpoint(`-c`), ignore count(`-i`), auto-continue 로깅 BP(`-G true -C ...`), `breakpoint command add`, `watchpoint modify -c`, `watchpoint command add -F` 를 batch 파일(`lldb -b -s`)로 묶어 **재현 가능한 디버깅 스크립트**를 만든다.
- lldb Python API(SBValue)로 `rbdump` 명령과 `type summary add -F` 요약을 쓰고, 그 함정(uint8_t 의 `GetValue()`, 포인터의 `GetLoadAddress()`, 합성 자식이 멤버를 가리는 것)을 안다.
- 하드웨어 자원 한계(이 M6 맥: 워치포인트 4개 · breakpoint 6개)와 비용(조건 평가 1회 ≈ 수 ms)을 실측으로 말하고, 같은 사냥을 gdb(`watch … if`, `commands`, pretty-printer)와 Cortex-M3 QEMU 원격에서 재현한다.

## 1. 문제

펌웨어 면접 디버깅 라운드의 단골 — "값이 가끔 깨지는데, 7000번째 루프쯤에서요. 어떻게 찾을 거예요?" 정답은 printf 를 더 넣는 게 아니라 **"그 메모리에 누가 쓰는지 CPU 에게 물어보는 것"**(워치포인트)이다. SSD 컨트롤러 펌웨어에서 명령 큐 옆 구조체가 가끔 깨지던 버그, Cortex-M 에서 DMA 디스크립터 옆 상태 변수가 바뀌던 버그가 다 이 모양이다. TOOL-B10 이 "멈추고 · 스택 보고 · 한 줄씩" 이었다면, 이 레슨은 **멈출 조건을 디버거에게 맡기고, 보는 방법을 스크립트로 만드는 것**이다.

재료 `buggy.c`(고치지 않는다): UART 송신 경로 흉내. `link_task()` 가 프레임을 SLIP(RFC 1055)으로 인코딩해 링 버퍼 `dev.tx` 에 넣고, `uart_tx_isr()` 가 매 tick 1~2 바이트를 빼 간다. 데이터 안의 `0xC0`(END)·`0xDB`(ESC)는 2바이트(`DB DC` / `DB DD`)로 바꿔 넣는다. LCG 시드가 고정이라 **항상 같은 tick 에서** 깨진다:

```text
$ cc -std=c11 -g -O0 buggy.c -o buggy && ./buggy        (실제 출력, exit 1)
tick 7343: bad link_state 0xdd
ticks=7343 frames=658 bad_state=1
```

`link_state` 는 0..3 만 가능한 상태 변수인데 `0xdd` 가 됐다. 메모리 배치를 보면 용의자가 보인다 — 링 버퍼 `buf[16]` 바로 뒤에 `link_state` 가 붙어 있다 (주소는 실측, lldb 는 기본으로 ASLR 을 끄므로 매번 같다):

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">struct uart_dev dev @ 0x100008010 — 실측 (memory read, 워치포인트 정지 시점)</text>
  <rect x="10" y="40" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <text x="18" y="58" fill="currentColor" font-size="12">head</text>
  <text x="18" y="74" fill="currentColor" font-family="monospace" font-size="12">=15</text>
  <rect x="70" y="40" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <text x="78" y="58" fill="currentColor" font-size="12">tail</text>
  <text x="78" y="74" fill="currentColor" font-family="monospace" font-size="12">=13</text>
  <rect x="130" y="40" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <text x="136" y="58" fill="currentColor" font-size="12">count</text>
  <text x="136" y="74" fill="currentColor" font-family="monospace" font-size="12">=2</text>
  <rect x="190" y="40" width="380" height="40" class="f-ok" stroke="currentColor"/>
  <text x="200" y="58" fill="currentColor" font-size="12">buf[0..15]  (0x10000801c ~ 0x10000802b)</text>
  <text x="200" y="74" fill="currentColor" font-family="monospace" font-size="12">e2 4b 6b 5b c0 6c fc c7 e5 e1 10 07 d6 95 a6 db</text>
  <rect x="570" y="40" width="80" height="40" class="f-warn" stroke="currentColor"/>
  <text x="578" y="58" fill="currentColor" font-size="12">link_state</text>
  <text x="578" y="74" fill="currentColor" font-family="monospace" font-size="12">dc 00 00 00</text>
  <rect x="650" y="40" width="80" height="40" class="f-mut" stroke="currentColor"/>
  <text x="656" y="58" fill="currentColor" font-size="12">frames_sent</text>
  <text x="10" y="100" fill="currentColor" font-family="monospace" font-size="12">+0x00</text>
  <text x="190" y="100" fill="currentColor" font-family="monospace" font-size="12">+0x0c</text>
  <text x="540" y="100" fill="currentColor" font-family="monospace" font-size="12">+0x1b</text>
  <text x="575" y="100" fill="currentColor" font-family="monospace" font-size="12">+0x1c (0x10000802c)</text>
  <path d="M545 128 L545 84" stroke="currentColor" stroke-width="1.5"/>
  <path d="M610 154 L610 84" stroke="currentColor" stroke-width="2"/>
  <text x="150" y="135" fill="currentColor" font-size="12">buf[head] = 0xDB  (head=15, 정상) → 0x10000802b</text>
  <text x="150" y="160" fill="currentColor" font-size="12">buf[head+1] = 0xDC → buf[16] = link_state 하위 바이트 (리틀 엔디언)</text>
  <rect x="10" y="180" width="720" height="128" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="200" fill="currentColor" font-size="12">tick 6800: 마지막 칸(head=15)에서 0xC0 을 escape → link_state 1 → 0xdc.</text>
  <text x="20" y="220" fill="currentColor" font-size="12">같은 tick 에 프레임이 끝나 link_state = WAIT_ACK 로 덮여 증상 없이 사라졌다.</text>
  <text x="20" y="245" fill="currentColor" font-size="12">tick 7342: 같은 경로(0xDB → DB DD) → link_state = 0xdd</text>
  <text x="20" y="265" fill="currentColor" font-size="12">→ 다음 tick 7343 의 switch default 가 처음으로 알아챈다.</text>
  <text x="20" y="292" fill="currentColor" font-size="12">증상(7343) ≠ 첫 오염(6800) ≠ 범인 함수(rb_put_escaped) — printf 로는 셋을 잇기 어렵다.</text>
</svg>
```

### Part A — 조건부 워치포인트 + 로깅 BP 로 범인 줄 찾기 (`<variant>.lldb`)

- `lldb -b -s solution.lldb ./buggy` 한 번으로: `main` 에서 멈춤 → `dev.link_state` 에 **하드웨어 워치포인트** → `watchpoint modify -c 'dev.link_state > 3'`(정상 전이 0→1→2→0 은 조용히 넘김) → `continue` → 멈춘 곳에서 `bt`, `image lookup -a $pc-4`(범인 명령어의 소스 줄), `p g_tick`, `memory read`.
- 동시에 **로깅 BP**: `breakpoint set -n rb_put_escaped -c 'rb->head == 15' -G true -C 'p g_tick' -C 'p *rb'` — 멈추지 않고 위험 경로가 돌 때마다 tick 과 버퍼 상태만 찍는다(코드 수정 없는 printf).
- starter.lldb 는 조건 한 줄이 빠져 있다(TODO 5) — 그러면 무엇이 멈추는지 실측으로 본다.

### Part B — lldb Python: `rbdump` 명령과 요약 제공자 (`<variant>.py` → `ringbuf_cmd.py`)

- `rbdump <expr>`: `struct ringbuf` 나 그 포인터를 **논리 순서**(가장 오래된 바이트부터)로 덤프. 헤더에 주소 · cap · count · tail · head · 상태(empty / full / wrapped / linear).
- `type summary add -F ringbuf_cmd.rb_summary ringbuf`: `p dev.tx`, `frame variable rb`, `bt` 의 인자 표시까지 한 줄 요약 `count=7/16 tail=12 head=3 [41 42 …]`.
- starter.py 의 TODO 4개 = 실제로 물리는 함정 4개: ① 물리 순서로 출력(감김 무시) ② `head == tail` 만 보고 empty 판정(full 과 구분 못 함) ③ `uint8_t` 원소를 `GetValue()` 로 읽음 ④ 포인터에 `GetLoadAddress()`. 채점용 고정 상태는 `rbstates.c`(linear / wrapped / full / empty / 0xC0·0xDB / 포인터 / 일부러 망가뜨린 것).

### Part C — 펌웨어 현실: 자원 · 비용 · gdb · Cortex-M

- 하드웨어 워치포인트는 **몇 개**고, **몇 바이트**까지, 커널이 쓴 것도 잡나? `modify` 와 `write` 는 뭐가 다른가? — `demo_lldb.sh` 로 실측.
- 조건부 BP/WP 는 왜 느린가 — 정지 1회당 비용을 재서 "실시간 펌웨어에 조건부 BP 를 걸면 생기는 일" 을 숫자로 말한다.
- 같은 사냥을 gdb 로: VM 의 `qemu-aarch64 -g`(사용자 모드 — 하드웨어 워치포인트가 없다는 것도 실측) 와 **Cortex-M3 이미지**(`qemu-system-arm -M mps2-an385 -gdb`, OpenOCD/J-Link 원격과 같은 모양), gdb Python pretty-printer `ringbuf_gdb.py`.

## 2. Problem (English)

> **Find a rare memory corruption with a debugger, then script the debugger.**
>
> `buggy.c` (do not modify it) runs a periodic task that SLIP-encodes frames into a 16-byte ring buffer `dev.tx`; a fake UART ISR drains it. After ~7000 ticks it prints `tick 7343: bad link_state 0xdd` and exits 1. `link_state` must always be 0..3.
>
> 1. Write an lldb command file that, run as `lldb -b -s hunt.lldb ./buggy`, stops **exactly at the write that corrupts `dev.link_state`** (not at legitimate state changes), and prints: the backtrace, the source line of the faulting store, the tick number, and the ring buffer state. It must also log — without stopping — every call where the encoder writes a 2-byte escape at the last slot.
> 2. Implement `ringbuf_cmd.py`: a command `rbdump <expr>` and a summary provider for `struct ringbuf`.
>    - accept a struct or a pointer to it; print its address; elements in logical order (oldest first); distinguish empty from full when `head == tail`;
>    - `rbdump rb_wrapped` (tail 12, count 7) must print exactly
>      `ringbuf @ 0x… cap=16 count=7 tail=12 head=3 wrapped` followed by `  [0] buf[12] = 0x41` … `  [6] buf[2] = 0x47`;
>    - summary: `count=7/16 tail=12 head=3 [41 42 43 44 45 46 47]`, `count=0/16 tail=9 head=9 empty`;
>    - a bad expression gives an lldb `error:`, never a Python traceback.
> 3. Follow-ups: why does the debugger report the line *after* the culprit? How many hardware watchpoints do you have and what if you need a 100-byte region? What does a conditional breakpoint cost in a 1 kHz control loop? How would you do this on a Cortex-M over SWD, and without a debugger in the field?

## 3. 시작 코드

차례대로: `buggy.c`(디버깅 대상 — 고치지 않는다) · `starter.py`(Part B, TODO 1~4 — `ringbuf_cmd.py` 로 복사해서 import) · `starter.lldb`(Part A 세션, TODO 5) · `check.sh`(맥 전용, 약 20 초).

<!-- code: buggy.c -->

<!-- code: starter.py -->

<!-- code: starter.lldb -->

<!-- code: check.sh -->

```bash
bash check.sh starter     # FAIL 여러 줄, exit 1
bash check.sh solution    # 20/20, exit 0
bash check.sh practice    # 26/26, exit 0 (추가 연습 6개 포함)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -g -O0 buggy.c -o buggy && cp starter.py ringbuf_cmd.py && lldb -b -s starter.lldb ./buggy
```

`bash check.sh starter` 실제 출력 (exit 1):

```text
PASS build buggy + rbstates (-g -O0, 0 warnings)
PASS symptom: ./buggy reports 'tick 7343: bad link_state 0xdd'
FAIL TODO(1) logical order: tail → wrap → head (wrapped/linear/full)
FAIL TODO(2) empty vs full when head == tail (count decides)
FAIL TODO(3) uint8_t elements read as ints (0xc0 0xdb 0x00 0x7f)
FAIL TODO(4) address of struct and of struct * (same 0x…)
FAIL rbdump rb_wrapped: exact text
FAIL rbdump prb (pointer): same text as rb_wrapped
FAIL rbdump rb_full: 'full' + 16 elements, last = buf[3]
PASS rbdump rb_empty: one header line ending 'empty'
FAIL summary: p rb_wrapped
FAIL summary: p prb (through the pointer)
FAIL summary: p rb_empty / frame variable rb_slip
FAIL no Python traceback while formatting
PASS rbdump on a bad expression: clean 'error:' (no traceback)
FAIL hunt: first stop after main = the watchpoint, inside rb_put_escaped
FAIL hunt: culprit = instruction before PC → buggy.c:61
FAIL hunt: corruption happened at tick 6800 (first, silent), not 7343
FAIL hunt: rbdump rb at the stop
FAIL hunt: 2nd dangerous call seen at tick 7342, program then reports 7343
4/20 passed
```

채점기 구조: (1) **단위** — `t_unit.py` 가 lldb 안에서 `logical_slots` · `rb_state` 를 손으로 만든 `RB` 객체로, `read_bytes` · `rb_address` 를 실제 SBValue 로 부른다 → TODO 하나에 FAIL 한 줄. (2) **통합** — `rbdump` / `p` 출력 원문 비교(주소는 같은 세션의 `p &rb_wrapped` 에서 읽는다). (3) **사냥** — `<variant>.lldb` 기록에서 정지 이유 · `image lookup` 줄 번호(= `grep -n 'rb->buf\[rb->head + 1\]' buggy.c`) · tick 을 확인.

## 4. 힌트

<details><summary>힌트 1 — 워치포인트가 정상 쓰기에서 자꾸 멈춘다</summary>

`link_state` 는 정상 코드도 프레임마다 0→1→2→0 으로 쓴다. 멈출지 말지를 **새 값**으로 정하면 된다 — 워치포인트 조건은 쓰기가 끝난 뒤 평가된다. "있을 수 없는 값" 이 뭔지 enum 을 보라.
</details>

<details><summary>힌트 2 — 멈춘 줄에는 link_state 가 없다</summary>

AArch64 의 워치포인트 예외는 store 명령어가 메모리를 바꾸기 *전에* 걸리고, 디버거는 그 명령어 하나를 실행(single-step)시킨 뒤 보고한다. 그래서 PC 는 범인 *다음* 명령어. 고정 길이 4바이트 명령어니까 `$pc-4` 의 소스 줄을 물어보면 된다 (`image lookup -a`).
</details>

<details><summary>힌트 3 — rbdump 순서와 empty/full</summary>

논리 i 번째 원소의 물리 칸 = `(tail + i) % cap`. `head == tail` 은 empty 이기도 full 이기도 하다 — 그래서 구조체에 `count` 가 있다.
</details>

<details><summary>힌트 4 — SBValue 함정</summary>

`uint8_t` 는 `unsigned char` 라서 `GetValue()` 가 `"'A'"`, `"'\\xc0'"` 같은 **문자 리터럴 문자열**을 준다. 숫자는 `GetValueAsUnsigned()`. 포인터 SBValue 는 `GetChildMemberWithName` 으로 멤버까지 자동으로 따라가지만, 포인터 SBValue 의 `GetLoadAddress()` 는 **포인터 변수 자신**의 주소(전역 `prb` 면 prb 가 놓인 곳)이거나, 식 결과(`&dev.tx`)라 메모리에 없으면 `0xffffffffffffffff` 다 — 포인터면 *값*이 곧 구조체 주소.
</details>

## 5. 풀이

### 5.1 Part A — 하드웨어 워치포인트가 하는 일

breakpoint 와 watchpoint 는 구현이 다르다. 실측: 이 맥(M6)은 `sysctl hw.optional.breakpoint: 6`, `hw.optional.watchpoint: 4`.

| 종류 | 어떻게 | 개수 | 비용 |
|---|---|---|---|
| 소프트웨어 BP (`b func`) | 코드 바이트를 trap 명령어(A64 `brk`)로 덮어쓴다 (Flash 에서는 불가) | 무제한 | 지날 때만 정지 |
| 하드웨어 BP (`breakpoint set -H`) | 디버그 레지스터 DBGBVR/DBGBCR 에 주소 — ROM/Flash 코드용 | 6 (이 맥) | 같음 |
| 하드웨어 워치포인트 | DBGWVR(주소) + DBGWCR(크기 · load/store) 비교기가 **모든 메모리 접근**을 본다 | 4 (이 맥) | 맞을 때까지 0 |
| 조건 (`-c`) | CPU 는 조건을 모른다: 매번 멈춰서 **디버거가** 식을 평가하고 다시 돌린다 | — | 정지 1회 ≈ ms |

(DBGWVR/DBGWCR 이름은 Arm 아키텍처 디버그 레지스터 — macOS 에서는 debugserver 가 커널을 통해 스레드 상태로 설정한다. 확인 권장.)

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">조건부 워치포인트 1회 히트의 흐름 (AArch64, lldb + debugserver)</text>
  <rect x="10" y="40" width="150" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="60" fill="currentColor" font-size="12">CPU: strb 실행</text>
  <text x="20" y="78" fill="currentColor" font-size="12">주소 == DBGWVR?</text>
  <path d="M160 65 L195 65" stroke="currentColor" stroke-width="1.5"/>
  <rect x="195" y="40" width="160" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="205" y="60" fill="currentColor" font-size="12">워치포인트 예외</text>
  <text x="205" y="78" fill="currentColor" font-size="12">(store 실행 전, PC=store)</text>
  <path d="M355 65 L390 65" stroke="currentColor" stroke-width="1.5"/>
  <rect x="390" y="40" width="170" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="400" y="60" fill="currentColor" font-size="12">디버거: WP 끄고 (구현상)</text>
  <text x="400" y="78" fill="currentColor" font-size="12">1 명령어 step, WP 다시 켬</text>
  <path d="M560 65 L595 65" stroke="currentColor" stroke-width="1.5"/>
  <rect x="595" y="40" width="135" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="605" y="60" fill="currentColor" font-size="12">lldb: 새 값 읽고</text>
  <text x="605" y="78" fill="currentColor" font-size="12">조건 식 평가</text>
  <path d="M662 90 L662 125" stroke="currentColor" stroke-width="1.5"/>
  <rect x="440" y="125" width="290" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="450" y="145" fill="currentColor" font-size="12">거짓(1826번): 조용히 resume → 다시 실행</text>
  <text x="450" y="162" fill="currentColor" font-size="12">정상 전이 0→1→2→0 은 여기서 걸러짐</text>
  <rect x="10" y="125" width="410" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="145" fill="currentColor" font-size="12">참(1번): 사용자에게 보고. PC = store 다음 명령어 (buggy.c:62)</text>
  <text x="20" y="162" fill="currentColor" font-size="12">범인 = $pc-4 (A64 고정 4바이트) → image lookup → buggy.c:61</text>
  <text x="10" y="200" fill="currentColor" font-size="12">• 히트 1827번 = 정상 쓰기 1826 + 범인 1 (실측: 파이썬 콜백 판의 호출 수). 거짓인 히트마다 정지·재개 왕복이 든다.</text>
  <text x="10" y="220" fill="currentColor" font-size="12">• Thumb(Cortex-M) 은 명령어가 2/4 바이트 → x/3i $pc-4 로 직전 store 확인 (5.5: strb 2바이트).</text>
  <text x="10" y="240" fill="currentColor" font-size="12">• 커널이 read() 안에서 쓴 것은 안 걸렸다 (5.3 실측) — DMA 쓰기도 같은 이유로 의심할 것.</text>
</svg>
```

`solution.lldb` 실행 기록(`demo_hunt.txt`, 앞부분 `main` 정지는 생략):

```text
(lldb) breakpoint set -n rb_put_escaped -c 'rb->head == 15' -G true -C 'p g_tick' -C 'p *rb'
Breakpoint 2: where = buggy`rb_put_escaped + 12 at buggy.c:58:18, address = 0x0000000100000a78
(lldb) watchpoint set variable dev.link_state
Watchpoint created: Watchpoint 1: addr = 0x10000802c size = 4 state = enabled type = m
…
(lldb) watchpoint modify -c 'dev.link_state > 3'
(lldb) continue
(lldb)  p g_tick
(uint32_t) 6800
(lldb)  p *rb
(ringbuf) count=2/16 tail=13 head=15 [95 a6]

Watchpoint 1 hit:
old value: 0
new value: 220
* thread #1, queue = 'com.apple.main-thread', stop reason = watchpoint 1
    frame #0: 0x0000000100000ae8 buggy`rb_put_escaped(rb=count=2/16 tail=13 head=15 [95 a6], b='\xc0') at buggy.c:62:17
   61  	    rb->buf[rb->head + 1] = (b == SLIP_END) ? SLIP_ESC_END : SLIP_ESC_ESC;
-> 62  	    rb->head = (rb->head + 2) % RB_CAP;
(lldb) bt 3
  * frame #0: 0x0000000100000ae8 buggy`rb_put_escaped(rb=count=2/16 tail=13 head=15 [95 a6], b='\xc0') at buggy.c:62:17
    frame #1: 0x00000001000009b0 buggy`slip_put(rb=count=2/16 tail=13 head=15 [95 a6], b='\xc0') at buggy.c:80:16
    frame #2: 0x00000001000007a4 buggy`link_task at buggy.c:99:18
(lldb) image lookup -a $pc-4
      Summary: buggy`rb_put_escaped + 120 at buggy.c:61:27
(lldb) p g_tick
(uint32_t) 6800
(lldb) p/x dev.link_state
(uint32_t) 0x000000dc
(lldb) rbdump rb
ringbuf @ 0x100008010 cap=16 count=2 tail=13 head=15 linear
  [0] buf[13] = 0x95
  [1] buf[14] = 0xa6
(lldb) memory read -s1 -fx -c 20 &dev.tx.buf[0]
0x10000801c: 0xe2 0x4b 0x6b 0x5b 0xc0 0x6c 0xfc 0xc7
0x100008024: 0xe5 0xe1 0x10 0x07 0xd6 0x95 0xa6 0xdb
0x10000802c: 0xdc 0x00 0x00 0x00
(lldb) watchpoint delete 1
(lldb) continue
(lldb)  p g_tick
(uint32_t) 7342
(lldb)  p *rb
(ringbuf) count=1/16 tail=14 head=15 [b9]
tick 7343: bad link_state 0xdd
ticks=7343 frames=658 bad_state=1
Process 41390 exited with status = 1 (0x00000001)
```

줄마다 읽기:

| 기록 | 뜻 |
|---|---|
| `type = m` | `watchpoint set variable` 의 기본 타입은 **modify**: 하드웨어는 모든 쓰기에서 걸리지만 값이 안 바뀐 쓰기는 lldb 가 보고하지 않는다 (5.3 에서 write 와 비교). |
| 로깅 BP 출력이 먼저 | 같은 tick 6800 에 `rb_put_escaped` 입구(58행)가 먼저 실행된다. `-G true` 라 멈추지 않고 `-C` 명령만 돌았다 — 코드 수정 없는 printf. |
| `old value: 0` | lldb 의 old value 는 **마지막으로 보고한 값**이다. 조건이 거짓이라 넘어간 1826번은 반영되지 않았다 (gdb 는 같은 순간 `Old value = 1` — 5.5). |
| `buggy.c:62` vs `image lookup … :61` | 멈춘 PC 는 store 다음. `$pc-4` 가 범인 store → **61행 `rb->buf[rb->head + 1]`**. |
| `count=2 tail=13 head=15 linear` | 버퍼 자체 불변식은 멀쩡하다(head == (13+2)%16). 오염은 버퍼 **밖**(buf[16])이라 버퍼 검사로는 영원히 못 찾는다. |
| `memory read` | `…a6 db` 다음 `dc 00 00 00` = buf[15]=ESC 다음 칸이 link_state 의 하위 바이트. |
| tick 6800 → 7342 → 7343 | 첫 오염(6800)은 같은 tick 에 `link_state = LINK_WAIT_ACK` 로 덮여 사라졌고, 두 번째(7342)가 다음 tick 에 증상(7343)을 냈다. 증상 tick 에 조건부 BP 를 걸었다면 첫 사건을 놓쳤다. |

고치는 건 한 줄 — 두 번째 바이트도 감아야 한다 (`demo_fix.txt`, 고친 사본으로 끝까지 실행):

```text
61c61
<     rb->buf[rb->head + 1] = (b == SLIP_END) ? SLIP_ESC_END : SLIP_ESC_ESC;
---
>     rb->buf[(rb->head + 1) % RB_CAP] = (b == SLIP_END) ? SLIP_ESC_END : SLIP_ESC_ESC;
ticks=20001 frames=1808 bad_state=0
rc=0
```

**starter.lldb 에서는?** (`demo_hunt_starter.txt`) 조건이 없으니 첫 정상 쓰기(tick 3, IDLE→SENDING)에서 멈추고, `image lookup -a $pc-4` 는 정직하게 `buggy.c:90:28`(`dev.link_state = LINK_SENDING;`)을 가리킨다. 그 프레임엔 `rb` 가 없어 `rbdump rb` 가 `error: rbdump: cannot evaluate 'rb'` → `-b` 모드는 **명령 하나가 실패하면 나머지를 실행하지 않는다**. 조건 없는 워치포인트를 정상 쓰기가 잦은 필드에 걸면 1827번 `continue` 를 쳐야 한다.

**가설이 생긴 뒤의 빠른 길.** "마지막 칸에서 2바이트 escape 가 의심" 이 서면, 조건부 BP 로 그 순간까지 간 다음에 워치포인트를 걸면 히트가 1번뿐이라 즉시 끝난다. 그 자리의 명령어 (`demo_options.txt` 마지막 절, 실제 출력):

```text
(lldb) disassemble -s '$pc-12' -c 4
buggy`rb_put_escaped:
    0x100000adc <+112>: mov    w8, #0x1                  ; =1 
    0x100000ae0 <+116>: add    w11, w11, #0x1
    0x100000ae4 <+120>: strb   w9, [x10, w11, uxtw]
->  0x100000ae8 <+124>: ldr    x9, [sp, #0x10]
(lldb) register read x8 x9 x10 x11
      x8 = 0x0000000000000001
      x9 = 0x00000000000000dc
     x10 = 0x000000010000801c  buggy`dev + 12
     x11 = 0x0000000000000010
```

`x10` = `&dev.tx.buf[0]`(dev + 12), `w11` = `head + 1` = 16, `strb w9, [x10, w11, uxtw]` = `buf[16] = 0xdc` — `% RB_CAP` 이 없다는 게 기계어에 그대로 보인다. (이 판은 `watchpoint set expression -s 4` 로 걸었는데 값 표시는 `new value: 0x00000260000000dc` — 표시용으로 포인터 크기 8바이트를 읽어 옆 필드 `frames_sent`(0x260 = 608)까지 보인다. 감시 자원은 4바이트.)

### 5.2 Part B — lldb Python 확장의 구조

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">rb_wrapped: 물리 배열 vs 논리 순서 (tail=12, count=7, head=3)</text>
  <g font-family="monospace" font-size="12">
    <rect x="10" y="40" width="44" height="34" class="f-ok" stroke="currentColor"/><text x="20" y="62" fill="currentColor">45</text>
    <rect x="54" y="40" width="44" height="34" class="f-ok" stroke="currentColor"/><text x="64" y="62" fill="currentColor">46</text>
    <rect x="98" y="40" width="44" height="34" class="f-ok" stroke="currentColor"/><text x="108" y="62" fill="currentColor">47</text>
    <rect x="142" y="40" width="44" height="34" class="f-mut" stroke="currentColor"/><text x="152" y="62" fill="currentColor">a3</text>
    <rect x="186" y="40" width="44" height="34" class="f-mut" stroke="currentColor"/><text x="196" y="62" fill="currentColor">a4</text>
    <rect x="230" y="40" width="44" height="34" class="f-mut" stroke="currentColor"/><text x="240" y="62" fill="currentColor">…</text>
    <rect x="274" y="40" width="44" height="34" class="f-mut" stroke="currentColor"/><text x="284" y="62" fill="currentColor">ab</text>
    <rect x="318" y="40" width="44" height="34" class="f-ok" stroke="currentColor"/><text x="328" y="62" fill="currentColor">41</text>
    <rect x="362" y="40" width="44" height="34" class="f-ok" stroke="currentColor"/><text x="372" y="62" fill="currentColor">42</text>
    <rect x="406" y="40" width="44" height="34" class="f-ok" stroke="currentColor"/><text x="416" y="62" fill="currentColor">43</text>
    <rect x="450" y="40" width="44" height="34" class="f-ok" stroke="currentColor"/><text x="460" y="62" fill="currentColor">44</text>
    <text x="22" y="92" fill="currentColor">[0]</text><text x="66" y="92" fill="currentColor">[1]</text><text x="110" y="92" fill="currentColor">[2]</text>
    <text x="150" y="92" fill="currentColor">[3]</text><text x="282" y="92" fill="currentColor">[11]</text>
    <text x="326" y="92" fill="currentColor">[12]</text><text x="370" y="92" fill="currentColor">[13]</text>
    <text x="414" y="92" fill="currentColor">[14]</text><text x="458" y="92" fill="currentColor">[15]</text>
  </g>
  <text x="150" y="112" fill="currentColor" font-size="12">↑ head=3 (다음 쓰기)</text>
  <text x="318" y="112" fill="currentColor" font-size="12">↑ tail=12 (가장 오래된 것)</text>
  <text x="520" y="55" fill="currentColor" font-size="12">초록 = 살아 있는 7바이트</text>
  <text x="520" y="75" fill="currentColor" font-size="12">회색 = 쓰레기(0xA0+i)</text>
  <rect x="10" y="130" width="720" height="110" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="150" fill="currentColor" font-size="12">lldb 기본 표시: buf = "EFG\xa3\xa4\xa5…\xabABCD" ← 물리 순서 + 쓰레기 + C 문자열 취급</text>
  <text x="20" y="172" fill="currentColor" font-size="12">TODO(1) 틀린 순서: buf[0..6] = 45 46 47 a3 a4 a5 a6  (starter: [0, 1, 2, 3, 4, 5, 6])</text>
  <text x="20" y="194" fill="currentColor" font-size="12">논리 i 번째 = buf[(tail + i) % cap] → buf[12] buf[13] buf[14] buf[15] buf[0] buf[1] buf[2]</text>
  <text x="20" y="216" fill="currentColor" font-size="12">요약: count=7/16 tail=12 head=3 [41 42 43 44 45 46 47]   상태 = wrapped (tail + count = 19 &gt; 16)</text>
</svg>
```

lldb 의 Python 은 세 겹이다:

| 층 | 이 레슨에서 | 하는 일 |
|---|---|---|
| `command script import x.py` | `__lldb_init_module(debugger, dict)` | 모듈이 로드될 때 한 번 — 여기서 명령·요약을 등록. 등록 문자열에 `__name__` 을 쓰면 파일 이름이 바뀌어도 된다(check.sh 는 `solution.py` 를 `ringbuf_cmd.py` 로 복사해 쓴다). |
| `command script add -f mod.func name` | `rbdump(debugger, command, exe_ctx, result, dict)` | 새 명령. 출력은 `result.AppendMessage`, 실패는 `result.SetError` → lldb 가 `error:` 로 찍고 `-b` 모드면 나머지 명령을 멈춘다. `-o` = 같은 이름 덮어쓰기. |
| `type summary add -F mod.func Type` | `rb_summary(valobj, dict)` → 문자열 | `p`, `frame variable`, `bt` 인자 표시에 자동으로 붙는다. 포인터 `ringbuf *` 에도 적용되고 이때 `valobj` 는 **포인터** 자신이다(실측: `T=ringbuf * ptr=True`). 예외가 나면 lldb 는 traceback 을 찍고 기본 표시로 돌아간다. |

SBValue 에서 값을 꺼내는 함정 — starter 의 단위 테스트 실제 결과 (`/tmp/TOOL-M07_work/starter/unit.txt`):

```text
UNIT order_wrapped BAD got=[0, 1, 2, 3, 4, 5, 6]
UNIT state_full BAD got='empty'
UNIT bytes_slip BAD got=["'\\xc0'", "'\\xdb'", "'\\0'", "'\\x7f'"]
UNIT addr_value OK got=4295000212
UNIT addr_pointer BAD got=4295000064
```

| TODO | starter | 실제로 나온 것 | 고침 |
|---|---|---|---|
| ① 순서 | `list(range(count))` | 물리 칸 0..6 | `[(tail + i) % cap for i in range(min(count, cap))]` — `min` 은 망가진 count(20)에서도 cap 개만 |
| ② empty/full | `head == tail` → empty | `rbdump rb_full` 헤더가 `count=16 … empty` | `count == 0` → empty, `count >= cap` → full |
| ③ 원소 읽기 | `child.GetValue()` | `"'\\xc0'"` 문자열 → `"%02x" %` 에서 `TypeError` → 명령 전체가 traceback | `GetValueAsUnsigned()` (또는 `GetData().uint8`) |
| ④ 주소 | `GetLoadAddress()` | `rbdump prb` → `@ 0x100008000` = **포인터 변수 prb 자신의 주소** (rb_wrapped 는 0x100008094). 식 결과(`&dev.tx`)면 `0xffffffffffffffff` | 포인터 타입이면 `GetValueAsUnsigned()` 가 주소 |

④가 제일 위험하다: 예외도 없이 **그럴듯한 틀린 주소**를 찍는다. 디버거 스크립트가 틀린 주소를 말하면 그다음 `memory read`·워치포인트가 전부 엉뚱한 곳을 본다.

`bash check.sh solution` 실제 출력 (약 18 초, exit 0):

```text
PASS build buggy + rbstates (-g -O0, 0 warnings)
PASS symptom: ./buggy reports 'tick 7343: bad link_state 0xdd'
PASS TODO(1) logical order: tail → wrap → head (wrapped/linear/full)
…  (TODO 2~4, 통합 11줄 모두 PASS)
PASS hunt: first stop after main = the watchpoint, inside rb_put_escaped
PASS hunt: culprit = instruction before PC → buggy.c:61
PASS hunt: corruption happened at tick 6800 (first, silent), not 7343
PASS hunt: rbdump rb at the stop
PASS hunt: 2nd dangerous call seen at tick 7342, program then reports 7343
20/20 passed
```

solution 의 `rbdump` / 요약 실제 출력 (`rbstates`, `checkpoint` 에서):

```text
(lldb) rbdump rb_wrapped
ringbuf @ 0x100008094 cap=16 count=7 tail=12 head=3 wrapped
  [0] buf[12] = 0x41
  [1] buf[13] = 0x42
  …
  [6] buf[2] = 0x47
(lldb) rbdump rb_empty
ringbuf @ 0x100008024 cap=16 count=0 tail=9 head=9 empty
(lldb) p prb
(ringbuf *) 0x0000000100008094 count=7/16 tail=12 head=3 [41 42 43 44 45 46 47]
(lldb) frame variable rb_slip
(ringbuf) rb_slip = count=4/16 tail=14 head=2 [c0 db 00 7f]
(lldb) rbdump nosuch_var
error: rbdump: cannot evaluate 'nosuch_var'
```

`find_value` 가 `GetValueForVariablePath` 를 먼저 쓰는 이유: 변수 경로(`dev.tx`, `rb`, `prb`)는 디버그 정보만으로 풀려서 대상 프로세스에서 코드를 돌리지 않는다. `&dev.tx` 같은 식만 `EvaluateExpression`(clang 으로 컴파일해 JIT 실행 — 느리고, C 프로그램이어도 `Ran expression as 'ISO C++'` 노트가 붙는다)으로 넘긴다.

### 5.3 Part C-1 — 하드웨어 자원의 한계 (실측, `demo_limits.txt` · `demo_modify.txt`)

```text
### (b) 8바이트 워치포인트 5개
(lldb) watchpoint set expression -w write -s 8 -- &g.w[3]
       #3: addr = 0x100008018 size = 8
(lldb) watchpoint set expression -w write -s 8 -- &g.w[4]
error: Watchpoint creation failed (addr=0x100008020, size=8).
error: Setting one of the watchpoint resources failed
```

| 요청 (`-s N -- 주소`) | lldb 가 실제로 쓴 하드웨어 자원 | 읽는 법 |
|---|---|---|
| 1 @ `…43` | 1 @ `…43` | 바이트 단위 OK |
| 3 @ `…41` | 4 @ `…40` | 정렬된 4바이트로 넓힘 |
| 8 @ `…44` | 16 @ `…40` | 2의 거듭제곱 · 정렬된 구간(MASK 방식)으로 넓힘 |
| 16 / 64 @ `…40` | 16 / 64 하나 | 큰 영역도 자원 1개 |
| 24 @ `…40` | **32** @ `…40` | 2의 거듭제곱으로 올림 |
| 8 @ `…4c` (16 경계 걸침) | 8 @ `…48` + 8 @ `…50` = **자원 2개** | 걸치면 개수가 준다 |
| run 전 `watchpoint set variable g` | `error: Command requires a current process.` | 프로세스가 있어야 디버그 레지스터를 쓴다 |

넓혀진 구간 안의 "요청 안 한 바이트" 쓰기를 lldb 가 걸러 주는지는 이 레슨에서 재지 않았다(확인 권장). AArch64 의 큰 영역 감시(MASK 필드, 2^n 바이트 정렬)는 Arm ARM 의 DBGWCR 설명 — 이 맥 lldb 가 그걸 쓰는 것으로 보인다.

`modify` vs `write`, 기본 크기, 커널 쓰기:

```text
### -w modify : same = 5 (x1000) 다음 same = 6
stops = 2, final state = exited
callback calls = 1001
### -w write : same = 5 (x1000) 다음 same = 6
stops = 1001, final state = exited
callback calls = 1001
### expression 워치포인트 -s 생략 → 기본 크기 = 포인터 크기 8 → 옆 필드(counter) 쓰기에도 멈춤
(lldb) watchpoint set expression -- &p.state
Watchpoint created: Watchpoint 1: addr = 0x100008090 size = 8 state = enabled type = m
-> 48  	            p.counter++;              /* 옆 필드 */
### 커널(read 시스템 콜)이 쓴 것은 안 잡힌다
kernel wrote 8 bytes
-> 45  	        printf("user wrote 0x%x\n", kbuf[0]);
```

- **modify**: 하드웨어는 1001번 다 걸린다(콜백 1001번). lldb 가 값 비교 후 바뀐 것만 보고 → 정지 2번(0→5, 5→6). "값이 바뀌는 순간" 을 찾을 땐 modify, "누가 쓰기라도 하는지(같은 값 덮어쓰기 포함)" 는 write. 콜백(`watchpoint command add -F`)은 modify 에서도 매 히트 불린다 — 실측.
- **`watchpoint set expression` 에서 `-s` 생략 = 8바이트**(도움말: "defaults to the target's pointer byte size"). 4바이트 필드에 걸면 옆 필드까지 본다 → 엉뚱한 정지. `watchpoint set variable p.state` 는 `size = 4` 로 잡혀 `p.state = 2` 줄 뒤(50행)에서만 멈췄다(`demo_modify.txt`).
- **커널이 쓴 것은 못 잡는다**: `read()` 가 `kbuf` 에 8바이트를 썼지만 정지는 유저 코드 `memset` 뒤(45행)에서만. 디버그 레지스터는 유저 스레드 문맥에 걸린다. 펌웨어로 옮기면 **DMA 가 쓴 메모리는 DWT 워치포인트에 안 걸릴 수 있다**(DWT 는 코어의 데이터 접근을 본다 — 버스 마스터 DMA 는 별개, 확인 권장).

### 5.4 Part C-2 — 비용: "멈출 때마다 디버거 왕복" (`demo_cost.txt`)

같은 `buggy` 를 여러 방식으로 돌린 벽시계 시간 (3회 중앙값, 다른 작업 12개와 같이 도는 맥이라 흔들림이 크다 — 같은 WP 세션을 따로 돌렸을 때는 12~18 초도 나왔다):

| 경우 | 초 | 디버거 왕복 수 | 1회당 (lldb run 1.01 초 뺌) |
|---|---|---|---|
| native `./buggy` (7343 tick) | 0.08 | 0 | — |
| `lldb -b -o run`, BP/WP 없음 | 1.01 | 0 | — |
| BP `link_task -i 999` (조건 식 없음) | 5.30 | 999 정지 (hit count 1000) | ≈ 4.3 ms |
| BP `link_task -c 'g_tick == 1000'` | 9.48 | 1000 평가 (hit count **1**) | ≈ 8.5 ms |
| BP `rb_put_escaped -c 'rb->head == 15'` (드문 함수) | 2.35 | 30 이하 (`-i 30` 실측: 31번째 호출이 tick 6880) | — |
| WP `dev.link_state` + `-c 'dev.link_state > 3'` | 24.69 | 1827 | ≈ 13 ms |
| WP 같은 것 + 파이썬 콜백(메모리 직접 읽기) | 10.64 | 1827 | ≈ 5.3 ms |

읽는 법:

- **정지·재개 왕복 자체가 ≈ 4 ms**(-i 줄). 조건 *문자열*은 매번 lldb 식 평가기(clang 기반)를 거쳐 ≈ 4 ms 더. 워치포인트는 정지마다 "WP 끄고 한 명령어 step" 이 더 붙어 가장 비싸다. 파이썬 콜백이 `ReadUnsignedFromMemory` 로 4바이트만 읽으면 식 평가기를 안 거쳐 절반 이하.
- 0.08 초 프로그램이 25 초 — **300배**. 실시간 루프에서 조건부 BP 는 "느려진다" 가 아니라 "다른 프로그램이 된다".
- **hit count 의 뜻이 다르다** (실측): `-c` 조건이 거짓인 정지는 hit count 에 안 들어가고(1), `-i` 로 무시한 정지는 들어간다(1000). 파이썬 콜백이 `False` 를 돌려준 정지도 들어간다(추가 연습 ④).
- 그래서 순서: **드문 함수 + 조건부 BP**(2.35 초)로 좁히고, 거기서 워치포인트를 걸면 히트 1번이다(위 "빠른 길"). 정상 쓰기가 잦은 필드에 처음부터 조건부 워치포인트는 "가설 없이도 된다" 는 장점과 시간을 맞바꾼 것.

### 5.5 Part C-3 — gdb 와 Cortex-M (Lima VM, `demo_vm_gdb.sh`)

**(a) qemu-aarch64 사용자 모드 gdbstub 은 하드웨어 워치포인트가 없다** (VM 의 네이티브 gdb 는 SVE 문제로 run 불가 — TOOL-B10):

```text
+watch dev.link_state if dev.link_state > 3
Hardware watchpoint 2: dev.link_state
+continue
Warning:
Could not insert hardware watchpoint 2.
Could not insert hardware breakpoints:
You may have requested too many hardware breakpoints/watchpoints.
```

**(b) 조건부 BP 로 좁힌 뒤 소프트웨어 워치포인트** (`set can-use-hw-watchpoints 0` → gdb 가 한 명령어씩 step 하며 값 비교 — 창이 좁아야 쓸 만하다) + pretty-printer + `commands` 로깅:

```text
+break rb_put_escaped if rb->head == 15
+continue
Breakpoint 1, rb_put_escaped (rb=0x4a1908 <dev>, b=192 '\300') at buggy.c:58
+print g_tick
$1 = 6800
+print *rb
$2 = count=2/16 tail=13 head=15 [95 a6]
+set can-use-hw-watchpoints 0
+watch dev.link_state
Watchpoint 2: dev.link_state
+continue
Watchpoint 2: dev.link_state
Old value = 1
New value = 220
rb_put_escaped (rb=0x4a1908 <dev>, b=192 '\300') at buggy.c:62
62	    rb->head = (rb->head + 2) % RB_CAP;
+info line *($pc - 4)
Line 61 of "buggy.c" starts at address 0x400840 <rb_put_escaped+68> and ends at 0x400878 <rb_put_escaped+124>.
…
+printf "[log] tick %u head=%u count=%u\n", g_tick, rb->head, rb->count
[log] tick 7342 head=15 count=1
[Inferior 1 (process 1132244) exited with code 01]
```

aarch64 Linux · gcc 15 · static 링크로 바뀌어도 tick 6800, 바이트 `95 a6`, 61행이 그대로다 — 버그가 결정적이라서. gdb 의 `Old value = 1` 이 맞는 이전 값(lldb 는 0, 5.1).

**(c) Cortex-M3 이미지** — 같은 `buggy.c` 를 `arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -g -O0 -nostdlib -Dprintf=cm_printf` + `cm_start.c`(벡터 표 · .data 복사 · .bss 0) + `cm.ld` 로 빌드해 `qemu-system-arm -M mps2-an385 -gdb tcp::23474 -S` 에 `gdb-multiarch` 로 붙는다. OpenOCD/J-Link GDB 서버에 SWD 로 붙는 것과 같은 모양(TOOL-A03):

```text
20000004 B dev
+watch dev.link_state if dev.link_state > 3
Hardware watchpoint 2: dev.link_state
+continue
Hardware watchpoint 2: dev.link_state
Old value = 1
New value = 220
rb_put_escaped (rb=0x20000004 <dev>, b=192 '\300') at buggy.c:62
62	    rb->head = (rb->head + 2) % RB_CAP;
+info line *($pc - 2)
Line 61 of "buggy.c" starts at address 0x158 <rb_put_escaped+40> and ends at 0x172 <rb_put_escaped+66>.
+x/3i $pc - 4
   0x16e <rb_put_escaped+62>:	mov	r2, r1
   0x170 <rb_put_escaped+64>:	strb	r2, [r3, #12]
=> 0x172 <rb_put_escaped+66>:	ldr	r3, [r7, #4]
+print g_tick
$1 = 6800
+print dev.tx
$3 = count=2/16 tail=13 head=15 [95 a6]
+info watchpoints
2       hw watchpoint  keep y              dev.link_state
	stop only if dev.link_state > 3
	breakpoint already hit 1 time
gdb session wall time: 5.874906421 s
```

- 시스템 에뮬레이션(qemu-system-arm)의 gdbstub 은 **하드웨어 워치포인트를 받아 준다**(QEMU 가 메모리 접근을 에뮬레이션하며 비교). 실제 칩에서는 이 역할을 코어의 **DWT 비교기**가 한다 — Cortex-M3/M4 는 보통 4개, M0+ 는 2개 정도(구현 옵션 — 칩 TRM 확인 권장).
- Thumb: 범인 `strb r2, [r3, #12]` 는 **2바이트** 명령어라 `$pc-2`. `dev` 가 `0x20000004`(.data 의 `g_seed` 4바이트 다음), `[r3, #12]` 의 12 는 구조체 안 `buf` 의 오프셋 → `r3` 는 `rb + head + 1` 로 읽힌다.
- gdb 는 조건이 참인 히트만 센다 (`already hit 1 time`). 1827번 왕복이 5.9 초 — QEMU 라 빠른 편이고, 실제 SWD 프로브는 정지·레지스터 읽기·재개가 훨씬 느리다(프로브·클럭에 따라 다름 — 측정 권장).
- 실제 Cortex-M DWT 의 워치포인트 디버그 이벤트는 정지가 범인 명령어보다 한두 개 **늦을 수 있다**(ARMv7-M 의 data watchpoint 이벤트는 비동기로 정의됨 — 확인 권장). 그래서 펌웨어에서는 `$pc-2` 하나만 믿지 말고 `x/6i $pc-12` 로 직전 store 들을 본다.

lldb ↔ gdb 대응 (이 레슨에서 쓴 것):

| 하는 일 | lldb | gdb |
|---|---|---|
| 조건부 BP | `breakpoint set -n f -c 'rb->head == 15'` | `break f if rb->head == 15` |
| 조건 나중에 붙이기 | `breakpoint modify -c '…' 1` | `condition 1 …` |
| N번 건너뛰기 | `breakpoint set … -i 30` | `ignore 1 30` |
| 멈추지 않는 로깅 BP | `-G true -C 'p g_tick'` | `commands` / `silent` / `printf …` / `continue` / `end` (또는 `dprintf f,"…",…`) |
| BP 명령 여러 줄 | `breakpoint command add 1` … `DONE` | `commands 1` … `end` |
| 쓰기 워치포인트 | `watchpoint set variable x` / `watchpoint set expression -w write -s 4 -- &x` | `watch x` (읽기 `rwatch`, 둘 다 `awatch`) |
| 워치포인트 조건 | `watchpoint modify -c '…' 1` | `watch x if …` / `condition 2 …` |
| 범인 줄 | `image lookup -a $pc-4` | `info line *($pc-4)` |
| Python 조건/콜백 | `breakpoint command add -F mod.fn` / `watchpoint command add -F` (True=멈춤) | `class B(gdb.Breakpoint): def stop(self): return …` (확인 권장) |
| 자료구조 표시 | `type summary add -F` / `type synthetic add -l` | `gdb.pretty_printers.append(lookup)`, `to_string` / `children` |
| 배치 실행 | `lldb -b -s f.lldb -o … -k …` | `gdb -batch -x f.gdb -ex …` |

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

<!-- code: solution.lldb diff=starter.lldb -->

gdb 판 요약 (VM 데모에서 사용, 채점 안 함):

<!-- code: ringbuf_gdb.py -->

## 7. 자주 하는 실수

1. **조건 없는 워치포인트를 자주 쓰이는 필드에.** → 정상 쓰기마다 멈춘다(starter: tick 3, `buggy.c:90` 에서 정지). 고침: `watchpoint modify -c 'dev.link_state > 3'` — 조건은 **쓰기 후의 새 값**으로 평가된다. 정상 값 범위를 아는 필드(enum, 길이, 매직)가 최고의 워치 대상이다.
2. **멈춘 줄을 범인으로 읽는다.** → 62행 `rb->head = …` 는 무죄. 고침: `image lookup -a $pc-4`(A64) / `info line *($pc-2)`·`x/3i $pc-4`(Thumb). 최적화 빌드에서는 store 가 다른 줄 코드와 섞여 "다음 줄" 이 엉뚱한 함수일 수도 있다.
3. **`watchpoint set expression -- &x` 에 `-s` 생략.** → 8바이트 감시라 옆 필드 쓰기에도 멈춘다(실측: `p.counter++` 에서 정지). 고침: `-s 4` 또는 `watchpoint set variable x`.
4. **증상 tick 에 조건부 BP 를 건다.** → `g_tick == 7343` 에서 멈추면 이미 늦었고, 첫 오염(6800)은 같은 tick 에 덮여서 증상이 없었다. 고침: "언제" 가 아니라 "어디에 쓰나" 로 묻는다 = 워치포인트.
5. **`memory read dev.tx.buf`.** → `error: address expression "dev.tx.buf" resulted in a value whose type can't be converted to an address: uint8_t[16]` (실측). 고침: `memory read -s1 -fx -c 20 &dev.tx.buf[0]`.
6. **배치 파일의 `watchpoint delete`(인자 없음).** → "다 지울까요? [Y/n]" 확인이 **다음 줄(continue)을 답으로 먹었다**(실측: continue 가 실행되지 않음). 고침: `watchpoint delete 1` 처럼 번호를 주거나 `settings set auto-confirm true`.
7. **SBValue 함정 3종** (5.2 표): `uint8_t` 에 `GetValue()`, 포인터에 `GetLoadAddress()`, 그리고 합성 자식(추가 연습 ②)을 등록하면 요약 함수의 `GetChildMemberWithName("buf")` 가 **무효**가 되어 `count=0/0 … empty` 가 찍힌다(실측). 고침: `valobj.GetNonSyntheticValue()` 에서 멤버를 꺼낸다.
8. **`-b` 모드에서 실패한 명령 뒤를 기대한다.** → `rbdump rb` 가 `error:` 를 내면 그 뒤 명령은 실행되지 않는다(starter 기록). Python 예외(traceback)는 `error:` 가 아니라서 계속 간다 — 그래서 명령 안에서는 예외 대신 `result.SetError` 를 쓴다.

## 8. 면접 Q&A

<details><summary>Q1. 소프트웨어 breakpoint 와 하드웨어 breakpoint/watchpoint 는 어떻게 다르고, 왜 워치포인트는 개수가 적나요?</summary>

소프트웨어 BP 는 코드 바이트를 trap 명령어(A64 `brk`, Thumb `bkpt`)로 바꿔 쓰는 것이라 개수 제한이 없지만 **쓸 수 있는 메모리**여야 한다(MCU Flash 에서 실행 중이면 불가 → FPB 하드웨어 BP). 워치포인트는 코드가 아니라 **데이터 접근**을 봐야 해서 trap 을 심을 곳이 없다 — CPU 가 모든 load/store 의 주소를 비교기와 맞춰 보는 하드웨어가 필요하고, 비교기는 비싼 자원이라 몇 개뿐이다. 실측: 이 M6 맥 `hw.optional.watchpoint: 4`, `breakpoint: 6`, 5번째 워치포인트는 `Setting one of the watchpoint resources failed`. Cortex-M3/M4 DWT 4개, FPB 코드 비교기 6개가 흔한 구성(칩마다 확인).

꼬리: 소프트웨어 워치포인트는? — 한 명령어씩 실행하며 값을 비교(gdb `set can-use-hw-watchpoints 0`). 수천~수만 배 느려서 창이 좁을 때만(5.5 b).
</details>

<details><summary>Q2. 워치포인트로 멈췄는데 표시된 줄에는 그 변수가 없습니다. 왜죠?</summary>

AArch64 에서 워치포인트 예외는 store 가 메모리를 바꾸기 *전에* 걸리고(그래서 디버거가 old value 를 볼 수 있다), 디버거는 워치포인트를 잠시 끄고 그 명령어 하나를 step 한 뒤 보고한다 — 그래서 PC 는 범인 **다음** 명령어다(이 메커니즘 설명은 lldb 구현 기준, 확인 권장). 실측: 멈춘 줄 62행, `image lookup -a $pc-4` → 61행 `strb w9, [x10, w11, uxtw]`. Thumb 은 명령어가 2/4바이트라 `x/3i $pc-4` 로 직전 store 를 확인(Cortex-M 실측: 2바이트 `strb`). x86 은 data breakpoint 가 원래 접근 *후* trap 이라 역시 다음 명령어를 가리킨다(확인 권장).
</details>

<details><summary>Q3. 1 kHz 제어 루프에 조건부 breakpoint 를 걸면 어떻게 되나요?</summary>

CPU 는 조건을 평가할 줄 모른다. BP 를 지날 때마다 **멈춤 → 디버거가 레지스터·메모리 읽음 → 식 평가 → 재개** 왕복이다. 맥에서 실측(5.4): 정지 1회 수 ms, 식 평가가 붙으면 더. 1 kHz 루프(1 ms 주기)면 매 주기가 수 ms 씩 늘어나 타이밍이 완전히 바뀌고 — 워치독, UART overrun, 모터 제어 불안정. MCU 에서는 코어가 멈춰도 주변장치·타이머·DMA 는 계속 돈다(DBGMCU freeze 비트로 일부 타이머만 멈출 수 있음 — 칩 확인). 대안: 조건을 **코드에** 넣고(`if (cond) __BKPT(0);`) 거기에 BP, 하드웨어 워치포인트(맞을 때까지 비용 0), 트레이스(ITM/ETM, RTT 로깅 — TOOL-A04).
</details>

<details><summary>Q4. 워치포인트는 4개, 8바이트인데 100바이트 구조체 전체를 감시해야 한다면?</summary>

(1) 2의 거듭제곱 정렬 구간이면 AArch64 MASK 워치포인트 하나로 된다 — 이 맥 lldb 는 64바이트를 자원 1개로 잡았다(실측, 24 → 32 로 올림). 100바이트 → 128 정렬 구간 1개(넘치는 부분의 정지는 걸러야 함). (2) 페이지 보호: `mprotect` 로 읽기 전용 → 쓰기 시 SIGSEGV/SIGBUS 핸들러(Electric Fence 방식, 4 KB 단위). (3) MCU: **MPU 영역**을 읽기 전용으로 → MemManage fault 가 범인 PC 를 준다(ARM-M12 의 CFSR/MMFAR). (4) 카나리(canary): 구조체 앞뒤 매직을 주기적으로 검사해 "언제쯤" 을 좁히고, 그다음 워치포인트. (5) 경계가 의심이면 **경계 바로 뒤 4바이트**만 보면 된다 — 이 레슨처럼 `buf[16]` = `link_state`.
</details>

<details><summary>Q5. 워치포인트가 못 잡는 쓰기는?</summary>

실측: 커널이 시스템 콜(`read`) 안에서 유저 버퍼에 쓴 것은 안 걸렸다. 같은 이유로 DMA(버스 마스터)가 쓴 것, 다른 코어가 공유 메모리에 쓴 것(디버그 레지스터는 코어/스레드별 — 디버거가 모든 스레드에 설정하는지는 도구별, 확인 권장), 같은 물리 메모리를 다른 가상 주소로 매핑해 쓴 것. 펌웨어에서 "워치포인트가 조용한데 값이 바뀐다" 면 DMA 디스크립터 길이·주소를 먼저 의심한다.
</details>

<details><summary>Q6. lldb 의 modify 와 write 워치포인트 차이는?</summary>

하드웨어는 둘 다 모든 쓰기에서 걸린다. `write` 는 매번 보고, `modify`(lldb 기본)는 old/new 를 비교해 바뀐 것만 보고한다. 실측: 같은 값 5 를 1000번 쓰고 6 을 한 번 → modify 정지 2번, write 1001번, 콜백은 둘 다 1001번 불림. 버그가 "같은 값이 계속 써지는 게 문제"(예: 레지스터를 매번 다시 초기화)라면 write 를 써야 한다. gdb `watch` 는 값이 바뀔 때 보고하는 쪽이다(확인 권장).
</details>

<details><summary>Q7. summary, synthetic children, pretty-printer 는 뭐가 다르죠? 회사 자료구조마다 써야 하나요?</summary>

lldb **summary** = 한 줄 문자열(`count=7/16 …`), **synthetic children** = 자식 목록 자체를 바꿈(링 버퍼를 논리 순서 `[0]..[6]` 으로 — `frame variable rb_wrapped[3]` 가 `'D'`, 단 `p rb_wrapped[1]` 같은 식에서는 `does not provide a subscript operator` 실측). gdb 는 pretty-printer 하나가 `to_string` + `children` 을 다 한다. 팀 공용으로 쓸 가치가 있는 것: 링 버퍼 · 리스트 · 해시 · 커맨드 큐 · 상태 머신 enum — 매번 머릿속으로 `(tail + i) % cap` 계산하는 게 디버깅 시간의 큰 부분이라서. `.lldbinit` / `.gdbinit` 에서 자동 로드하고 리포에 같이 둔다. Linux 커널의 `scripts/gdb`(lx-dmesg, lx-ps)와 Zephyr/FreeRTOS 의 RTOS-aware gdb 플러그인이 같은 생각이다(확인 권장).
</details>

<details><summary>Q8. 디버거를 못 붙이는 현장(field) 장비에서 같은 버그를 잡으려면?</summary>

(1) **펌웨어가 DWT 를 직접 설정**: DWT_COMPn/FUNCTIONn 에 주소를 쓰고 DebugMonitor 예외를 켜면, 디버거 없이도 쓰기 순간에 예외 핸들러가 돈다 → 스택의 PC/LR 을 크래시 로그에 남김(BM-A09 .noinit, ARM-M14 레지스터 덤프, TOOL-A05). 레지스터 이름·비트는 ARMv7-M ARM 확인 권장. (2) MPU 가드 영역. (3) 경계 카나리 + `ASSERT` → trap(`demo_trap.c`: `lldb -b -o run -k bt` 가 크래시일 때만 bt 를 찍는 CI 패턴, 실측). (4) 쓰기 경로에 불변식 검사(`rbcheck` 같은) — 단 이 버그처럼 **버퍼 밖**을 덮는 경우 버퍼 자체의 불변식은 멀쩡하다(추가 연습 실측: 오염 순간 `rbcheck rb: OK`).
</details>

## 9. 추가 연습

practice.py = solution.py + 아래 4개, practice.lldb = 같은 사냥을 `-c` 문자열 대신 파이썬 콜백으로. `bash check.sh practice` 가 기본 20개 + 연습 6개를 확인한다.

<details><summary>① rbcheck — 링 버퍼 불변식 검사 명령</summary>

`count ≤ cap`, `head, tail < cap`, `head == (tail + count) % cap` 를 검사해 `OK` 또는 `CORRUPT: 이유; 이유`. 실제 출력:

```text
rbcheck rb_wrapped: OK
rbcheck rb_bad: CORRUPT: count 20 > cap 16; head 15 != (tail + count) % cap = 1
```

사냥 도중(오염 순간)에는 `rbcheck rb: OK` — 오염이 버퍼 밖이라는 증거.
</details>

<details><summary>② 합성 자식(synthetic children) — frame variable 이 논리 순서로</summary>

`type synthetic add -l mod.RingbufSynth ringbuf`. 클래스는 `num_children`, `get_child_at_index`(물리 칸 SBValue 를 `Clone("[i]")`), `get_child_index`, `update`. 함정: 이걸 등록하면 요약 함수가 받는 `valobj` 의 멤버(head/tail/buf)가 가려진다 → `snapshot` 첫 줄에 `valobj.GetNonSyntheticValue()`. 실제 출력:

```text
(lldb) frame variable -Y1 -f x rb_wrapped
(ringbuf) rb_wrapped = {
  [0] = 0x41
  …
  [6] = 0x47
}
(lldb) frame variable -f x rb_wrapped[0] rb_wrapped[6] rb_wrapped[7]
(uint8_t) rb_wrapped[0] = 0x41
(uint8_t) rb_wrapped[6] = 0x47
error: <user expression 0>:1:11: array index 7 is not valid for "(ringbuf) rb_wrapped"
```

(`-Y1` = 요약을 1단계 생략하고 자식을 펼침. 요약과 합성 자식이 둘 다 있으면 기본 표시는 요약 한 줄.)
</details>

<details><summary>③ 파이썬 워치포인트 콜백 = 문자열 조건 대신 코드</summary>

`watchpoint command add -F ringbuf_cmd.wp_bad_link_state 1`. 콜백 `(frame, wp, dict)` 이 `wp.GetWatchAddress()` / `GetWatchSize()` 로 감시 주소를 직접 읽어 `> 3` 일 때만 `True`(멈춤), 아니면 `False`(조용히 계속). 조건식보다 나은 점: 여러 필드·히스토리(직전 값, 히트 횟수)를 볼 수 있다. 실측: `[wp] link_state=0xdc after 1827 hits` 다음 같은 61행.
</details>

<details><summary>④ 파이썬 BP 콜백 — 마지막 칸에서만 멈춤</summary>

`breakpoint command add -F ringbuf_cmd.bp_edge 2`: `frame.FindVariable("rb")` 를 `snapshot` 해 `head == cap - 1` 일 때만 `True`. `-c 'rb->head == 15'` 의 15 를 하드코딩하지 않고 배열 크기에서 구한다. 실측: practice 사냥 기록에 `[bp] rb_put_escaped at the last slot (head=15) after 6 calls` — 6800 이후 7342 까지 6번 불려 5번은 `False`. `-c` 조건이 거짓인 히트는 **hit count 에 안 들어가지만**(solution 로깅 BP 의 hit count 2), 파이썬 콜백이 거른 히트는 다 센다.
</details>

### 한 번에 검증

<!-- code: practice.py -->

practice.lldb (check.sh 가 practice 일 때 `hunt.lldb` 로 복사해 쓴다) — solution.lldb 와 다른 곳만:

```text
# ① -c 문자열 대신 파이썬 조건 콜백 (로깅 BP 는 뺐다)
watchpoint set variable dev.link_state
watchpoint command add -F ringbuf_cmd.wp_bad_link_state 1
# ② 정지 후 rbdump 다음에 rbcheck rb
# ③ 워치포인트를 지운 뒤 파이썬 BP 콜백으로 두 번째 사건에서 멈춤
breakpoint set -n rb_put_escaped
breakpoint command add -F ringbuf_cmd.bp_edge 2
continue
p g_tick
frame variable -Y1 -f x *rb
continue
```

```bash
bash check.sh practice      # 실제: 26/26 passed, exit 0
```

## 10. 핵심 정리

- 워치포인트 = "누가 이 주소에 쓰나" 를 CPU 비교기에 묻는 것. 이 맥 4개(breakpoint 6개), Cortex-M DWT 보통 4개. 맞을 때까지 비용 0.
- 조건(`-c`)은 CPU 가 아니라 디버거가 평가 → 히트마다 정지·재개 왕복(수 ms). 정상 쓰기가 잦은 필드는 "있을 수 없는 새 값" 으로 조건을 건다.
- 멈춘 PC 는 범인 다음 명령어: A64 `image lookup -a $pc-4`, Thumb `x/3i $pc-4` 로 직전 store.
- 증상 시점 ≠ 첫 오염 시점: 이 버그는 6800 에 조용히 오염, 7342 에 다시 오염, 7343 에 증상.
- `-G true -C '…'` 로깅 BP = 재컴파일 없는 printf. `-i` = N번 건너뛰기. `lldb -b -s f.lldb -k bt` = CI 용 배치.
- lldb Python: `command script add -f`(명령), `type summary add -F`(한 줄), `type synthetic add -l`(자식). SBValue: `GetValueAsUnsigned`(not `GetValue` on uint8_t), 포인터는 값이 주소, 합성 자식이 있으면 `GetNonSyntheticValue()`.
- 못 잡는 쓰기: 커널 · DMA · 다른 코어. 넓은 영역은 MASK(2^n 정렬)·MPU·mprotect·카나리.

## 참고자료

- LLDB — Watchpoints / Breakpoints (`help watchpoint set expression`, `help breakpoint set`): <https://lldb.llvm.org/use/tutorial.html>
- LLDB Python API (SBValue, SBWatchpoint, SBBreakpoint 콜백): <https://lldb.llvm.org/python_api.html>
- LLDB Variable Formatting (summary, synthetic children): <https://lldb.llvm.org/use/variable.html>
- GDB — Setting Watchpoints / Break Commands / Pretty Printing API: <https://sourceware.org/gdb/current/onlinedocs/gdb/>
- Arm Architecture Reference Manual (A-profile) — Debug: DBGWVR/DBGWCR, watchpoint exceptions: <https://developer.arm.com/documentation/ddi0487/latest>
- ARMv7-M Architecture Reference Manual — DWT, FPB, DebugMonitor: <https://developer.arm.com/documentation/ddi0403/latest>
- RFC 1055 — SLIP framing (END 0xC0, ESC 0xDB): <https://www.rfc-editor.org/rfc/rfc1055>
- 관련 레슨: TOOL-B10 (lldb 기초), TOOL-B11 (ASan — 같은 버그를 컴파일러로 잡는 방법), TOOL-M11 (코어 덤프), TOOL-M14 (DWARF/심볼), TOOL-A03 (gdb 원격/OpenOCD), TOOL-A05 (크래시 덤프 형식), BM-A09 (.noinit 크래시 덤프), ARM-M12 (HardFault CFSR), ARM-M14 (크래시 레지스터 덤프)
