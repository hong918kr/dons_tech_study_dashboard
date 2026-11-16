---
id: OS-A02
time: 110분
updated: 2026-11-15
---
# OS-A02 디버거는 어떻게 동작하나: ptrace로 미니 디버거

## 학습 목표

- `ptrace(PTRACE_TRACEME / PEEKDATA / POKEDATA / GETREGSET / SINGLESTEP / CONT / DETACH)` 로 자식을 멈추고, 메모리·레지스터를 대신 읽고 쓰는 **미니 디버거**를 짜고, 각 요청의 반환값·errno 함정(PEEK 의 `-1`)을 설명한다.
- aarch64 에서 소프트웨어 브레이크포인트 = 4 바이트 `BRK #0`(0xd4200000) 를 8 바이트 PEEK/POKE 워드 안에 **read-modify-write** 로 심고, 트랩 후 **PC 가 BRK 주소 그대로**임을 실측·커널 소스로 확인한다 (x86 int3 의 "PC-1 되감기" 와 대비).
- **step-over-breakpoint**(원래 명령 복원 → SINGLESTEP → 재삽입 → CONT), 히트 카운터, 싱글스텝 명령 수(asm 함수 = objdump 와 정확히 일치), `waitpid` 상태 + `PTRACE_GETSIGINFO` si_code(`TRAP_BRKPT` vs `TRAP_TRACE`) 해석을 구현한다.
- finish(복귀 주소 임시 BP) + 반환값 바꿔 끼우기, 조건부 BP, 시그널 통과, detach/kill 정리 — "스크립트형 디버깅" 으로 fault injection 을 한다.
- HW 브레이크포인트/워치포인트(`NT_ARM_HW_BREAK/WATCH`), Yama `ptrace_scope`, macOS(PT_DENY_ATTACH, PEEK 없음), MCU 디버그(GDB RSP + OpenOCD, Cortex-M FPB · BKPT)와 연결한다.

## 1. 문제

"디버거의 브레이크포인트는 실제로 뭘 하나요?" — 펌웨어/시스템 면접의 단골이다. 답은 **"그 주소의 명령어를 트랩 명령으로 덮어쓰고, 트랩이 나면 원래 명령으로 되돌려 한 스텝 실행한 뒤 다시 덮어쓴다"** 이고, 꼬리질문은 거기서부터다: PC 는 어디를 가리키나, 8 바이트 쓰기로 4 바이트 명령을 어떻게 고치나, 코드 페이지는 읽기 전용인데 어떻게 쓰나, 계속 실행하면 왜 같은 자리에서 또 멈추지 않나, 플래시에서 도는 MCU 코드에는 왜 이 방법을 못 쓰나. TOOL-B10 에서 lldb/gdb 를 **쓰는** 쪽을 했다면, 이 레슨은 그 디버거를 **만드는** 쪽이다. 실리콘 검증·보드 브링업에서 OpenOCD/J-Link 스크립트로 "BP 에서 멈춰 레지스터 바꾸고 계속" 하는 것이 같은 동작이다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">디버거 = 부모 프로세스, 디버기 = 자식, 커널 = 중개자</text>
  <rect x="10" y="40" width="195" height="250" class="f-acc" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-weight="bold">디버거 (부모)</text>
  <text x="22" y="86" fill="currentColor" font-family="monospace" font-size="12">PEEKDATA / POKEDATA</text>
  <text x="22" y="106" fill="currentColor" font-family="monospace" font-size="12">GETREGSET NT_PRSTATUS</text>
  <text x="22" y="126" fill="currentColor" font-family="monospace" font-size="12">SINGLESTEP / CONT</text>
  <text x="22" y="146" fill="currentColor" font-family="monospace" font-size="12">waitpid → GETSIGINFO</text>
  <text x="22" y="176" fill="currentColor" font-size="12">BP 표: addr, 원래 명령 4 B</text>
  <text x="22" y="196" fill="currentColor" font-size="12">pending_sig (돌려줄 시그널)</text>
  <rect x="240" y="40" width="245" height="250" class="f-mut" stroke="currentColor"/>
  <text x="252" y="62" fill="currentColor" font-weight="bold">커널</text>
  <text x="252" y="86" fill="currentColor" font-size="12">access_remote_vm(FOLL_FORCE)</text>
  <text x="252" y="104" fill="currentColor" font-size="12">  → 읽기전용 코드도 COW 로 씀</text>
  <text x="252" y="130" fill="currentColor" font-size="12">EL0 BRK 예외 → do_el0_brk64</text>
  <text x="252" y="148" fill="currentColor" font-size="12">  → SIGTRAP, TRAP_BRKPT</text>
  <text x="252" y="174" fill="currentColor" font-size="12">SPSR.SS 스텝 → do_el0_softstep</text>
  <text x="252" y="192" fill="currentColor" font-size="12">  → SIGTRAP, TRAP_TRACE</text>
  <text x="252" y="218" fill="currentColor" font-size="12">tracee 의 시그널 = 정지(stop)</text>
  <text x="252" y="236" fill="currentColor" font-size="12">  → 부모 waitpid 가 깨어남</text>
  <rect x="520" y="40" width="210" height="250" class="f-ok" stroke="currentColor"/>
  <text x="532" y="62" fill="currentColor" font-weight="bold">디버기 (fork 한 자식)</text>
  <text x="532" y="86" fill="currentColor" font-family="monospace" font-size="12">PTRACE_TRACEME</text>
  <text x="532" y="106" fill="currentColor" font-family="monospace" font-size="12">raise(SIGSTOP)</text>
  <text x="532" y="126" fill="currentColor" font-family="monospace" font-size="12">p_add(i, 7) ...</text>
  <text x="532" y="156" fill="currentColor" font-size="12">같은 바이너리 · 같은 주소</text>
  <text x="532" y="176" fill="currentColor" font-size="12">(fork 는 배치를 복사,</text>
  <text x="532" y="196" fill="currentColor" font-size="12"> ASLR 은 exec 때만)</text>
  <rect x="532" y="215" width="185" height="26" class="f-warn" stroke="currentColor"/>
  <text x="542" y="233" fill="currentColor" font-family="monospace" font-size="12">target_add: BRK #0</text>
  <path d="M205 100 L240 100" stroke="currentColor"/><path d="M233 95 L240 100 L233 105" fill="none" stroke="currentColor"/>
  <path d="M485 100 L520 100" stroke="currentColor"/><path d="M513 95 L520 100 L513 105" fill="none" stroke="currentColor"/>
  <path d="M520 228 L485 228" stroke="currentColor"/><path d="M492 223 L485 228 L492 233" fill="none" stroke="currentColor"/>
  <path d="M240 228 L205 228" stroke="currentColor"/><path d="M212 223 L205 228 L212 233" fill="none" stroke="currentColor"/>
  <text x="10" y="312" fill="currentColor" font-size="12">→ 명령: 부모가 커널에 부탁해 자식의 메모리/레지스터를 바꾼다</text>
  <text x="10" y="330" fill="currentColor" font-size="12">← 이벤트: 자식의 트랩이 부모의 waitpid 로 돌아온다</text>
</svg>
```

**환경 (이 레슨의 모든 숫자)**: Lima VM `elinux` (vz, aarch64, 6 vCPU) · Ubuntu 26.04.1 LTS · 실행 커널 **7.0.0-38-generic** · glibc 2.43 · gcc 15.2.0 · clang 21.1.8 · gdb 17.1. 커널 소스 인용은 VM 의 `/var/lab/src/linux-6.18.55/` 기준이다 (실행 커널과 버전이 다르다 — 인용한 경로들은 동작을 실측으로 함께 확인했다). `kernel.yama.ptrace_scope = 1`. 호스트: Apple M6, 측정 중 맥 load average 10–17 (다른 세션들), VM load 3–4.

**자기완결 설계.** 디버거와 디버기가 한 파일이다. `dbg_spawn(fn, arg)` 가 fork → 자식은 `PTRACE_TRACEME` → `raise(SIGSTOP)` 로 멈추고, 부모가 BP 를 심은 뒤 `cont` 하면 자식이 `fn(arg)` 를 실행하고 그 반환값으로 `_exit` 한다. 부모가 아는 `&target_add` 가 자식에서도 그대로 유효하다. 따로 빌드한 프로그램을 exec 해서 디버깅하는 길은 `demo_exec.c` 가 보여 준다(PIE 로드 베이스 + ELF 심볼표).

### Part A — 메모리, BRK 심기, 레지스터, 이벤트, continue

- `peek_word / poke_word` — `PTRACE_PEEKDATA / POKEDATA`. 반환 0 / `-errno`. PEEK 은 **읽은 값을 반환값으로** 주므로 `-1` 이 정상 데이터일 수 있다 → `errno = 0` 후 판단.
- `read_insn / write_insn` — 4 바이트 명령을 8 바이트 워드 안에서 read-modify-write. `addr % 4 != 0` → `-EINVAL`.
- `bp_set / bp_clear` — 원래 명령 4 바이트 저장, `BRK #0` 쓰기 / 복원. 같은 워드의 두 절반에 BP 두 개가 있어도 서로 안 지워야 한다.
- `get_regs / set_regs` — arm64 에는 `PTRACE_GETREGS` 가 없다 → `PTRACE_GETREGSET` + `NT_PRSTATUS` + `struct iovec` (`struct user_pt_regs` = x0..x30, sp, pc, pstate).
- `classify` — waitpid 상태를 EXIT / KILLED / SIGNAL(정지 시그널) / BREAK(`TRAP_BRKPT`) / STEP(`TRAP_TRACE`) 로.
- `dbg_step` / `dbg_cont` — PC 에 우리 BP 가 있으면 원래 명령으로 한 스텝(step-over) 후 재삽입. 보류 시그널은 재개 때 돌려준다.
- 테스트: `target_add` 를 10 번 부르는 루프에 BP → **히트 10 번**, 매 히트 `x0 == i`, `PEEKDATA(&g_counter) == i+1`, 종료 코드 115 그대로. 부모 자신의 코드·변수는 안 바뀜(COW).

### Part B — 싱글스텝 명령 수

- `count_steps` — 함수 입구 BP 에서 멈춘 상태로, 입구의 `x30`(복귀 주소)에 PC 가 닿을 때까지 `dbg_step` 횟수.
- asm 으로 쓴 `asm_leaf4`(4 명령) = **정확히 4**, `asm_loop(n)` = **정확히 2n+2** (n = 1, 5, 32). C 함수 `c_poly` 는 컴파일러마다 다르므로 **범위**로 검사하고, objdump 와 맞는지는 md 에서 보인다.

### Part C — 스크립트형 디버깅: 센서 드라이버 오류 경로 주입

센서 데몬이 `sensor_read(ch)` 를 16 채널 돌며 읽고, 음수(에러)면 에러 카운트를 올린다. 실보드에서는 채널 5 를 일부러 고장 내기 어렵다 → 디버거로 **반환값을 바꿔 끼운다** (JTAG 스크립트 fault injection 과 같은 방식).

- `run_cond(pred)` — 조건부 BP: 히트마다 `pred(regs)` 가 참일 때만 멈춘다 (`x0 == 5`).
- `dbg_finish` — 복귀 주소에 임시 BP, `sp` 가 입구와 같을 때의 복귀에서 멈춤 → `x0` = 반환값 51 → `-EIO` 로 바꿔 `set_regs`.
- `dbg_detach` — BP 를 모두 지우고 `PTRACE_DETACH`, 자식은 정상 종료. (대조군: BP 를 남긴 채 놓아주면 SIGTRAP 으로 죽는다 — 실측.)
- 시그널 통과: 디버기가 스스로 `raise(SIGUSR1)` → signal-delivery-stop → 다시 넘겨줘야 핸들러가 돈다. kill 정리: SIGKILL + 회수, 좀비 없음.

## 2. Problem (English)

> Build a minimal debugger for aarch64 Linux with `ptrace`. The debugger forks the debuggee from the same binary;
> the child calls `PTRACE_TRACEME`, stops itself with `SIGSTOP`, then runs a target function.
>
> ```c
> int  peek_word(pid_t, uintptr_t addr, uint64_t *out);      // 0 or -errno (PEEKDATA may legally return -1)
> int  poke_word(pid_t, uintptr_t addr, uint64_t w);
> int  read_insn(pid_t, uintptr_t addr, uint32_t *insn);      // 4-byte insn inside an 8-byte word
> int  write_insn(pid_t, uintptr_t addr, uint32_t insn);      // read-modify-write; -EINVAL if addr % 4
> int  bp_set(dbg_t *, uintptr_t addr);                       // save original insn, write BRK #0 (0xd4200000)
> int  bp_clear(dbg_t *, uintptr_t addr);                     // restore; -ENOENT if none
> int  get_regs(pid_t, struct user_pt_regs *);               // PTRACE_GETREGSET + NT_PRSTATUS
> ev_t classify(pid_t, int wstatus);                          // EXIT/KILLED/SIGNAL/BREAK(TRAP_BRKPT)/STEP(TRAP_TRACE)
> ev_t dbg_step(dbg_t *);  ev_t dbg_cont(dbg_t *);            // step over our breakpoint if PC is on one
> long count_steps(dbg_t *);                                  // from function entry until PC == entry LR
> int  dbg_finish(dbg_t *, struct user_pt_regs *after);       // temp BP at LR, same SP
> ev_t run_cond(dbg_t *, pred_fn, int *hits);                 // conditional breakpoint
> int  dbg_detach(dbg_t *);                                   // remove all BPs, detach, reap
> ```
>
> Constraints: never hang (every `waitpid` has a 2 s `alarm` watchdog; `PTRACE_O_EXITKILL`; children always reaped);
> a breakpoint must not change program results (hit 10 times in a loop, exit code identical); two breakpoints in the
> same 8-byte word must not clobber each other; signals the debuggee receives must be re-delivered.
> Example: BP on `target_add`, loop of 10 → 10 hits, `x0 == i` on hit i, exit code 115. Stepping a 4-instruction
> leaf from its entry BP reaches the return address in exactly 4 steps.
> Follow-ups: where is PC after the trap on arm64 vs x86? How does a debugger write to read-only text? What breaks
> with multiple threads? Why can't you single-step through an LL/SC loop? How do MCUs do this without an OS?

## 3. 시작 코드

<!-- code: starter.c -->

**리눅스 aarch64 전용** (`/* ENV: linux */` — 맥 검증기는 건너뛴다. 다른 아키텍처면 `#error`). 맥 홈은 VM 에 읽기 전용으로 마운트되므로 VM 의 `/tmp/OS-A02_work/` 로 복사해서 빌드한다. 추가 빌드 플래그는 없다(스레드를 안 쓴다).

```bash
limactl shell elinux                     # 이후 VM 안
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/OS/OS-A02
mkdir -p /tmp/OS-A02_work && cp $L/*.c /tmp/OS-A02_work/ && cd /tmp/OS-A02_work
gcc   -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
clang -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

`#define _GNU_SOURCE` 를 include **앞에** 둬야 `-std=c11` 에서 `TRAP_BRKPT`/`TRAP_TRACE`, `PTRACE_GETREGSET` 등이 보인다. `<sys/ptrace.h>` 와 `<asm/ptrace.h>`(struct `user_pt_regs`)는 함께 include 해도 이 glibc 에서 충돌하지 않았다. 시작 코드 실행 (앞부분, 실제 출력, 10 ms 미만 — 스텁은 "아무것도 안 하는" 안전한 값을 돌려주고, `dbg_cont` 스텁은 그냥 `PTRACE_CONT` 라서 자식은 바로 끝까지 달린다):

```text
== Part A: 메모리 · 명령 워드 · BP 심기/지우기 ==
PASS  dbg_spawn(&d, sc_magic, 0) == 0
FAIL  peek_word(d.pid, (uintptr_t)&g_magic, &w) == 0 && w == 0x1122334455667788ull  (line 350)
FAIL  peek_word(d.pid, 8, &w) == -EIO  (line 351)
...
FAIL  e.kind == EVK_BREAK && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL  (line 465)
PASS  waitpid(pid, &st, WNOHANG) == -1 && errno == ECHILD
timeouts=0
7/51 passed                     (exit=1)
```

(PASS 7 개는 spawn 성공과 "부모 쪽은 안 바뀜" 같은, 빈 스텁에도 우연히 맞는 검사다.)

## 4. 힌트

<details>
<summary>힌트 1 — PEEKDATA 의 -1 과 errno</summary>

`ptrace(PTRACE_PEEKDATA, pid, addr, NULL)` 의 반환값이 **읽은 워드**다. 그 워드가 `0xffffffffffffffff` 이면 반환값이 `-1` 이라 오류와 구분이 안 된다. 그래서 man 페이지(ptrace(2), NOTES)대로 `errno = 0` 을 먼저 하고, `-1 && errno != 0` 일 때만 오류. 매핑 안 된 주소는 커널 `generic_ptrace_peekdata` 가 `-EIO` 를 준다 (kernel/ptrace.c:1434).

</details>

<details>
<summary>힌트 2 — 4 바이트를 8 바이트 워드 안에 쓰기</summary>

`base = addr & ~7`, 워드를 peek → `memcpy((char *)&w + (addr & 7), &insn, 4)` → poke. 리틀엔디언이라 `addr % 8 == 4` 면 워드의 위쪽 32 비트다. 복원할 때도 **지금 워드를 다시 읽어서** 내 4 바이트만 바꾼다. BP 를 심을 때 저장해 둔 8 바이트를 통째로 다시 쓰면, 그 사이 같은 워드의 다른 절반에 심은 BP 가 사라진다.

</details>

<details>
<summary>힌트 3 — GETREGSET 은 iovec 를 받는다</summary>

`struct iovec iov = { &regs, sizeof regs }; ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &iov)`. 커널은 실제로 채운 길이를 `iov.iov_len` 에 돌려준다 (`user_pt_regs` = 34 × 8 = 272 바이트). `NT_PRSTATUS` 는 `<elf.h>` 에 있다.

</details>

<details>
<summary>힌트 4 — 트랩 후 PC 는 어디? (먼저 실험해 볼 것)</summary>

BP 에 걸린 뒤 `get_regs` 로 `pc` 를 찍고 `&target_add` 와 비교해 보라. arm64 의 BRK 는 "예외를 일으킨 명령의 주소" 를 돌려줄 주소로 남긴다. 그러면 step-over 에서 PC 를 고칠 필요가 있나? (x86 에서는 int3 를 **실행한 뒤**라서 PC = addr + 1.)

</details>

<details>
<summary>힌트 5 — step-over 와 finish</summary>

`dbg_step`: `bp_find(pc)` 면 원래 명령 복원 → `PTRACE_SINGLESTEP` → `dbg_wait`(TRAP_TRACE 기대) → BRK 재삽입. `dbg_cont`: BP 위면 먼저 `dbg_step`, 그다음 `PTRACE_CONT`. `dbg_finish`: 입구의 `x30` 에 임시 BP, `dbg_cont` 를 반복하다 `pc == x30 && sp == 입구 sp` 면 성공. 임시 BP 는 반드시 지운다.

</details>

## 5. 풀이

### Part A — PEEK/POKE: 왜 읽기 전용 코드에 쓸 수 있나

PEEK/POKE 는 커널 `generic_ptrace_peekdata/pokedata`(kernel/ptrace.c:1434, 1446) → `ptrace_access_vm` → `access_remote_vm(..., FOLL_FORCE [| FOLL_WRITE])` 다. 조건은 kernel/ptrace.c:54–60: **내가 이 tracee 의 tracer 이고**, 대상이 덤프 가능(dumpable)이어야 한다 — 그래서 `prctl(PR_SET_DUMPABLE, 0)` 한 프로세스는 부모라도 PEEK 이 `EIO` 가 된다. 코드 페이지는 `r-x` 인데 POKE 가 되는 이유는 `FOLL_FORCE` 다. mm/gup.c:584–600 `can_follow_write_common` 이 "FOLL_FORCE 면 쓰기 권한 없는 **private** 매핑에도 쓰되, 공유(`VM_SHARED`) 매핑에는 효과 없음" 을 정한다. private 매핑에 쓰면 COW 로 그 프로세스만의 사본이 생긴다. 테스트로 확인했다:

| 검사 | 결과 | 의미 |
|---|---|---|
| `peek_word(&g_magic)` | `0x1122334455667788` | 자식 주소 공간에서 읽음 |
| `peek_word(8)` | `-EIO` | 매핑 안 된 주소 → `EIO` (EFAULT 아님) |
| `poke_word(&g_magic, 0x2a)` 후 자식 종료 코드 | `0x2a` | 자식은 바뀐 값을 봤다 |
| 부모의 `g_magic`, 부모의 `asm_leaf4` 첫 명령 | 그대로 | fork 후 별개 주소 공간 + 코드 POKE 는 자식 사본에만 (COW) |

같은 파일을 실행 중인 **다른 프로세스**나 디스크의 바이너리는 안 바뀐다 — 공유 라이브러리에 BP 를 걸어도 마찬가지다(그 페이지가 이 프로세스에서 private 사본이 될 뿐).

### Part A — BRK 를 워드 안에 심기

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">PEEK/POKE 단위 = 8 바이트, 명령 = 4 바이트 (리틀엔디언, asm_leaf4 는 8 정렬)</text>
  <text x="10" y="53" fill="currentColor">원래 워드</text>
  <rect x="110" y="35" width="200" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="310" y="35" width="200" height="26" class="f-ok" stroke="currentColor"/>
  <text x="116" y="53" fill="currentColor" font-family="monospace" font-size="11">+0 11000400 add w0,w0,#1</text>
  <text x="316" y="53" fill="currentColor" font-family="monospace" font-size="11">+4 0b000000 add w0,w0,w0</text>
  <text x="520" y="53" fill="currentColor">비트 0–31 | 비트 32–63</text>
  <text x="10" y="103" fill="currentColor">BP 2 개 심음</text>
  <rect x="110" y="85" width="200" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="310" y="85" width="200" height="26" class="f-warn" stroke="currentColor"/>
  <text x="116" y="103" fill="currentColor" font-family="monospace" font-size="11">+0 d4200000 BRK #0</text>
  <text x="316" y="103" fill="currentColor" font-family="monospace" font-size="11">+4 d4200000 BRK #0</text>
  <text x="520" y="103" fill="currentColor" font-family="monospace" font-size="11">bp[0].orig = 11000400</text>
  <text x="520" y="119" fill="currentColor" font-family="monospace" font-size="11">bp[1].orig = 0b000000</text>
  <text x="10" y="163" fill="currentColor">맞는 clear(+0)</text>
  <rect x="110" y="145" width="200" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="310" y="145" width="200" height="26" class="f-warn" stroke="currentColor"/>
  <text x="116" y="163" fill="currentColor" font-family="monospace" font-size="11">+0 11000400 복원</text>
  <text x="316" y="163" fill="currentColor" font-family="monospace" font-size="11">+4 d4200000 살아 있음</text>
  <text x="520" y="163" fill="currentColor">지금 워드 peek → 4 B 교체</text>
  <text x="10" y="213" fill="currentColor">틀린 clear(+0)</text>
  <rect x="110" y="195" width="200" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="310" y="195" width="200" height="26" class="f-mut" stroke="currentColor"/>
  <text x="116" y="213" fill="currentColor" font-family="monospace" font-size="11">+0 11000400</text>
  <text x="316" y="213" fill="currentColor" font-family="monospace" font-size="11">+4 0b000000 BP 사라짐!</text>
  <text x="520" y="213" fill="currentColor">심을 때 저장한 8 B 를</text>
  <text x="520" y="229" fill="currentColor">통째로 poke</text>
  <text x="10" y="266" fill="currentColor">CHECK: BP 두 개 후 워드 == (BRK0 &lt;&lt; 32 | BRK0), 하나 지워도 다른 절반은 BRK0,</text>
  <text x="10" y="283" fill="currentColor">둘 다 지우면 원래 워드. x86 int3 는 1 바이트, arm64 는 4 정렬 강제 (addr % 4 → -EINVAL).</text>
</svg>
```

`BRK #imm16` 인코딩은 `0xd4200000 | imm16 << 5` 다 (커널 arch/arm64/include/asm/insn-def.h:15 `AARCH64_BREAK_MON 0xd4200000`). 디버거는 보통 `#0` 을 쓴다. 다른 imm 은 다른 주인이 있다: 커널 uprobes 는 `BRK #0x005`(arch/arm64/include/asm/brk-imm.h:25 — `do_el0_brk64` 가 이 imm 이면 uprobe 핸들러로 먼저 보낸다), 커널 `BUG()` 는 `#0x800`, gcc 의 `__builtin_trap()` 은 `#0x3e8`(이 VM 실측: `insn=0xd4207d00 imm=0x3e8`).

### Part A — 트랩 후 PC: arm64 는 되감지 않는다

먼저 실험(probe, 실제 출력):

```text
stop sig=5 si_code=1 (TRAP_BRKPT=1) si_addr=0xab4fcd6e1040 pc=0xab4fcd6e1040 bp=0xab4fcd6e1040 x0=4 pstate=0x60000000 iov_len=272
after step sig=5 si_code=2 (TRAP_TRACE=2) pc=0xab4fcd6e1044
```

**관찰: BRK 트랩 후 `pc == BP 주소`, `si_addr == pc`.** 원래 명령을 복원하고 그대로 스텝/계속하면 된다. 커널 경로: EL0 동기 예외 `ESR_ELx_EC_BRK64` → `el0_brk64`(arch/arm64/kernel/entry-common.c:735) → `do_el0_brk64`(debug-monitors.c:254–262) → `send_user_sigtrap(TRAP_BRKPT)` → `arm64_force_sig_fault(SIGTRAP, si_code, instruction_pointer(regs), ...)`(debug-monitors.c:163–175). 아무도 `regs->pc` 를 건드리지 않는다. BRK 는 "예외를 일으킨 그 명령" 을 복귀 주소로 남기는 동기 예외이기 때문이다 (Arm ARM 의 preferred return address 규칙 — 문서 기준).

| | arm64 `BRK #0` | x86-64 `int3` (0xCC) |
|---|---|---|
| 크기 | 4 바이트, 4 정렬 | 1 바이트, 아무 데나 |
| 트랩 후 PC | **BRK 주소** (실측) | int3 **다음** = addr + 1 (트랩형 예외) |
| step-over 전 PC 수정 | 필요 없음 | `rip -= 1` 해야 함 (gdb 가 함) |
| 커널 | `do_el0_brk64` → `TRAP_BRKPT` | `do_int3_user` → `do_trap(X86_TRAP_BP, SIGTRAP, ...)` (arch/x86/kernel/traps.c:873) |

x86 의 "+1" 은 커널 코드에서 직접 확인한 것이 아니라 int3 가 트랩(실행 후 보고)이라는 x86 규칙에서 나온 것이다 — 이 VM 은 aarch64 라서 실측하지 못했다. 이 차이 때문에 x86 습관대로 `pc -= 4` 를 하면 arm64 에서는 **바로 앞 명령을 한 번 더 실행**한다 (7. 자주 하는 실수).

### Part A — 레지스터와 이벤트 해석

arm64 의 GPR 레지스터셋은 arch/arm64/kernel/ptrace.c:1589–1596 `[REGSET_GPR] = { USER_REGSET_NOTE_TYPE(PRSTATUS), .n = sizeof(struct user_pt_regs) / sizeof(u64), ... }` 이고, 모양은 uapi `asm/ptrace.h:88–93` `struct user_pt_regs { __u64 regs[31]; __u64 sp; __u64 pc; __u64 pstate; }`. 테스트가 `target_add` 첫 히트에서 찍은 실제 덤프 (gcc -O2):

```text
  x0  0000000000000000  x1  0000000000000007  x2  0000af1381172be0  x3  0000000000000000
  ...
  x28 0000af1381172bc0  x29 0000ffffc888c930  x30 0000af1381172c68
  sp  0000ffffc888c930  pc  0000af1381172be0  pstate 20000800 (NZCV=2, M[4:0]=0)
```

- `x0 = 0, x1 = 7` — AAPCS64 인자 레지스터. `x2 == pc` 인 것은 호출자가 `blr x2` 로 불렀기 때문(volatile 함수 포인터).
- `pstate 0x20000800`: `M[4:0] = 0` → EL0t, AArch64(nRW = 0). `0x800` = `PSTATE.BTYPE = 0b10`(uapi ptrace.h:73 `PSR_BTYPE_C`) — `blr` 간접 호출 직후라서다. 직접 `bl` 로 부른 probe 에서는 `0x60000000` 이었다. (BTI 가 강제된 페이지에서 의미가 있다 — 이 바이너리는 `-mbranch-protection` 없이 빌드돼 강제되지 않는다.)

`classify` 의 판정표:

| waitpid 상태 | 추가 정보 | 이벤트 |
|---|---|---|
| `WIFEXITED` | `WEXITSTATUS` | EXIT (이미 회수됨 → `pid = 0`) |
| `WIFSIGNALED` | `WTERMSIG` | KILLED |
| `WIFSTOPPED`, sig ≠ SIGTRAP | — | SIGNAL (signal-delivery-stop: 다음 재개 때 돌려준다) |
| `WIFSTOPPED`, SIGTRAP | `GETSIGINFO.si_code == TRAP_BRKPT (1)` | BREAK |
| 〃 | `si_code == TRAP_TRACE (2)` | STEP (`do_el0_softstep`, debug-monitors.c:181–192) |
| 〃 | `si_code == TRAP_HWBKPT (4)` | HW BP/워치 (`ptrace_hbptriggered`, ptrace.c:200) — demo_hwbp |

회수 여부(`pid = 0`)는 classify 가 아니라 **raw 상태**로 판단한다(주어진 `dbg_wait`). classify 가 틀려도 이미 회수된 pid 에 `kill` 을 보내는 사고(pid 재사용)가 없게 하려는 것이다.

### Part A — step-over-breakpoint

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">BP 위에서 continue: 원래 명령 한 개를 "BP 없이" 실행하고 다시 심는다</text>
  <rect x="10" y="40" width="134" height="50" class="f-warn" stroke="currentColor"/>
  <text x="18" y="60" fill="currentColor">① BRK 트랩</text><text x="18" y="78" fill="currentColor" font-family="monospace" font-size="11">pc=A TRAP_BRKPT</text>
  <rect x="156" y="40" width="134" height="50" class="f-mut" stroke="currentColor"/>
  <text x="164" y="60" fill="currentColor">② write_insn</text><text x="164" y="78" fill="currentColor" font-family="monospace" font-size="11">A ← orig</text>
  <rect x="302" y="40" width="134" height="50" class="f-acc" stroke="currentColor"/>
  <text x="310" y="60" fill="currentColor">③ SINGLESTEP</text><text x="310" y="78" fill="currentColor" font-family="monospace" font-size="11">pc=A+4 TRAP_TRACE</text>
  <rect x="448" y="40" width="134" height="50" class="f-mut" stroke="currentColor"/>
  <text x="456" y="60" fill="currentColor">④ write_insn</text><text x="456" y="78" fill="currentColor" font-family="monospace" font-size="11">A ← BRK0</text>
  <rect x="594" y="40" width="136" height="50" class="f-ok" stroke="currentColor"/>
  <text x="602" y="60" fill="currentColor">⑤ PTRACE_CONT</text><text x="602" y="78" fill="currentColor">다음 히트까지</text>
  <path d="M144 65 L156 65" stroke="currentColor"/><path d="M290 65 L302 65" stroke="currentColor"/>
  <path d="M436 65 L448 65" stroke="currentColor"/><path d="M582 65 L594 65" stroke="currentColor"/>
  <text x="10" y="125" fill="currentColor" font-weight="bold">②–④ 를 빼먹으면</text>
  <text x="10" y="145" fill="currentColor">PTRACE_CONT → 같은 BRK 를 다시 실행 → 같은 pc 에서 TRAP_BRKPT … 영원히</text>
  <text x="10" y="180" fill="currentColor" font-weight="bold">③ 의 조건</text>
  <text x="10" y="200" fill="currentColor">③ 은 분기(b.ne, ret …)여도 된다: 커널이 SPSR.SS 를 세팅 → 명령 하나 뒤 Software Step 예외</text>
  <text x="10" y="218" fill="currentColor">(user_enable_single_step, debug-monitors.c:382–388). 착지한 곳이 다른 BP 라도 아직</text>
  <text x="10" y="236" fill="currentColor">실행 전이므로 STEP 으로 보고되고, 다음 CONT 에서 BREAK 가 된다.</text>
  <text x="10" y="268" fill="currentColor" font-weight="bold">멀티스레드라면</text>
  <text x="10" y="286" fill="currentColor">②–④ 사이에 다른 스레드가 A 를 지나가면 BP 를 놓친다 → gdb 는 원래 명령을</text>
  <text x="10" y="304" fill="currentColor">다른 곳에 복사해 실행한다 (displaced stepping, Q3).</text>
</svg>
```

`dbg_step` 은 이 ②–④ 를, `dbg_cont` 는 "BP 위면 `dbg_step` 먼저, 그다음 CONT" 를 한다. 결과 (solution 실제 출력):

```text
  hits=10 last event kind=4 code=115
PASS  hits == N_LOOP && args_ok
PASS  e.kind == EVK_EXIT && e.code == 115
```

10 번 모두 `x0 == i`, `g_counter == i+1`, 그리고 BP 가 있어도 프로그램 결과(115)가 같다.

**보류 시그널.** 디버기가 받은 시그널(예: `raise(SIGUSR1)`)은 먼저 디버거에게 정지로 보고된다. `PTRACE_CONT` 의 `data` 로 그 번호를 다시 줘야 디버기에게 전달된다. 0 을 주면 **삼켜진다** — starter 의 순진한 `dbg_cont` 가 그렇게 해서 핸들러가 안 돌고 종료 코드가 1 이 나온다(정답은 42). gdb 의 `handle SIGUSR1 pass/nopass` 가 이 선택이다.

### Part B — 싱글스텝 명령 수와 objdump

`count_steps` 는 입구에서 `x30` 을 읽고 `pc == x30` 이 될 때까지 센다. 예상값은 `objdump -d` 로 한 번 계산했다 (VM, gcc 15.2 -O2 solution 바이너리):

```text
0000000000002bc0 <asm_leaf4>:            0000000000002bd0 <asm_loop>:
    2bc0: 11000400  add  w0, w0, #0x1        2bd0: 2a0003e1  mov  w1, w0
    2bc4: 0b000000  add  w0, w0, w0          2bd4: 71000421  subs w1, w1, #0x1
    2bc8: 51000c00  sub  w0, w0, #0x3        2bd8: 54ffffe1  b.ne 2bd4 <asm_loop+0x4>
    2bcc: d65f03c0  ret                      2bdc: d65f03c0  ret
```

| 함수 | 근거 | 예상 | 실측 (gcc / clang / ASan) | 검사 |
|---|---|---|---|---|
| `asm_leaf4` | 직선 4 명령, 마지막 `ret` 스텝이 LR 에 착지 | 4 | 4 / 4 / 4 | **정확히** |
| `asm_loop(n)` | `mov` 1 + (`subs`, `b.ne`) × n + `ret` 1 | 2n+2 → 4, 12, 66 | 4 12 66 (전부) | **정확히** |
| `c_poly` | 컴파일러가 정한다 | gcc -O2: 6, clang -O2: 5 | 6 / 5 / gcc ASan+UBSan 32, clang 21 | 범위 3–200 |

asm 함수는 직접 썼으니 명령 수가 고정이다 → 정확히 비교한다. C 함수는 컴파일러·플래그에 따라 달라서(아래 objdump, UBSan 은 곱셈 오버플로 검사를 넣어 32) **범위**로 검사하고, 기본 빌드 수치가 objdump 의 명령 수와 정확히 같다는 것을 여기서 보인다:

```text
gcc -O2 <c_poly>:   mul w1,w0,w0 · add w0,w0,w0,lsl #2 · add w1,w1,w1,lsl #1 · add w0,w1,w0 · sub w0,w0,#0x7 · ret   = 6
clang -O2 <c_poly>: add w8,w0,w0,lsl #1 · mov w9,#-7 · add w8,w8,#0x5 · madd w0,w8,w0,w9 · ret               = 5
```

첫 스텝도 정확하다: `asm_leaf4` 입구 BP 에서 `dbg_step` 한 번 → `pc == LEAF + 4`, `si_code == TRAP_TRACE`, `x0 == 6` (5 + 1 이 실제로 실행됨), 그리고 입구 워드는 다시 `BRK0`.

**왜 `asm_leaf4` 를 C 가 아니라 asm 으로?** C 로 쓰면 gcc 가 `x * 2 + ...` 를 다른 명령 조합으로 바꾸거나, `-O0` 에서는 스택 프레임을 만든다 — "정확히 N" 을 검사할 근거가 사라진다. 그리고 C 타깃은 모두 **volatile 함수 포인터**로 부른다. gcc 는 상수 인자를 보고 `target_add.constprop.0` 같은 복제본을 만들 수 있고, 그러면 `&target_add` 에 심은 BP 를 아무도 안 지나간다 (gdb 가 `foo.constprop.0` 이나 `foo.isra.0` 을 보여 주는 이유).

### Part C — 조건부 BP, finish, 반환값 주입

```text
PASS  e.kind == EVK_BREAK && e.pc == SENSOR && hits == BAD_CH + 1      ← ch 0..5 에서 6 번 걸렸고, ch==5 에서만 멈춤
PASS  dbg_finish(&d, &r) == 0 && r.regs[0] == BAD_CH * 10 + 1          ← 원래 반환값 51
PASS  set_regs(d.pid, &r) == 0                                          ← x0 = -EIO
PASS  hits == N_CH && e.kind == EVK_EXIT && e.code == 15                ← 오류 1 건 + 나머지 합(1165)%10
```

- **조건부 BP 의 비용**: 조건이 거짓인 5 번도 매번 트랩 → 디버거로 문맥 전환 → 레지스터 읽기 → step-over → 재개다. gdb 의 `break f if ch == 5` 도 기본은 같은 방식이라(디버거 쪽 평가) 뜨거운 루프에 걸면 프로그램이 극적으로 느려진다. gdbserver 는 조건을 타깃 쪽에서 평가하는 방식(`target-side` 조건)을 지원한다 (문서 기준).
- **finish 의 sp 조건**: `pc == LR` 만 보면 재귀 함수에서 안쪽 호출의 복귀에 먼저 멈춘다. arm64 에서는 호출 직전과 복귀 직후의 `sp` 가 같으므로 `sp == 입구 sp` 를 함께 본다.
- **반환값 주입**: `ret` 직후(복귀 주소)에서 `x0` 를 바꾼다. 함수 안쪽 상태는 정상인 채 호출자만 실패를 본다 — 드라이버 오류 경로 테스트, 실리콘 검증에서 "레지스터 읽기 결과를 바꿔 에러 핸들러 커버리지 올리기" 와 같은 기법이다.

### Part C — 시그널, detach, kill

| 시나리오 | 실측 |
|---|---|
| 디버기 `raise(SIGUSR1)` → cont | `EVK_SIGNAL code=10` (signal-delivery-stop) → 다시 cont(시그널 전달) → 종료 코드 42 |
| BP 히트 상태에서 `dbg_detach` | BP 모두 지움 → `PTRACE_DETACH` → 자식 정상 종료 코드 6 |
| BP 를 **남긴 채** `PTRACE_DETACH` | 자식이 BRK 를 재실행 → 받아 줄 tracer 없음 → `WTERMSIG == SIGTRAP` |
| BP 히트 상태에서 `dbg_kill` | `WTERMSIG == SIGKILL`, 이후 `waitpid(pid, WNOHANG)` = `ECHILD` (좀비 없음) |

detach 할 때 PC 를 고칠 필요가 없는 것도 arm64 의 이점이다: BP 히트 상태에서 BP 를 지우면 PC 는 이미 원래 명령을 가리킨다. (x86 이었다면 `rip -= 1` 을 먼저.) `PTRACE_O_EXITKILL` 은 디버거 자신이 죽어도 디버기를 남기지 않는 보험이다 — kernel/ptrace.c:600–612 `exit_ptrace` 에서 `PT_EXITKILL` 이면 `SIGKILL`. 이 VM 의 gdb 17.1 도 같은 옵션을 켠다 (strace 로 본 실제 호출: `PTRACE_SETOPTIONS ... PTRACE_O_TRACESYSGOOD|...|PTRACE_O_TRACEEXEC|PTRACE_O_EXITKILL`).

**core dump 함정 (실제로 겪음).** "BP 를 남긴 채 detach" 대조군은 자식을 SIGTRAP 으로 죽인다. SIGTRAP 은 core dump 대상 시그널이고, 이 VM 의 `core_pattern` 은 `|/usr/share/apport/apport ...` 파이프다. 처음에는 자식에서 `RLIMIT_CORE = 0` 으로 막았는데, solution 이 1.1 s 걸리더니 호스트 부하가 오르자 이 대기가 2 s 감시를 넘어 **간헐적으로 50/51** 이 됐다. 원인은 fs/coredump.c:981 의 주석 그대로 — "Normally core limits are irrelevant to pipes" — 커널은 한도 0 이어도 apport 를 띄운다. 고친 방법: `RLIMIT_CORE = 1`. 1 은 커널이 "core 핸들러가 다시 죽는 재귀" 를 막으려고 쓰는 특수값이라 파이프면 즉시 포기하고(coredump.c:978–994, dmesg 에 `coredump: <pid>(g_solution): RLIMIT_CORE is set to 1, aborting core` 한 줄), 파일 core 면 1 바이트 < 페이지 크기라 안 쓴다(:884). 이후 solution·practice 는 각각 10 ms 미만, 8 회 연속 51/51. 교훈: **"자식이 시그널로 죽는" 테스트의 시간은 내 코드가 아니라 시스템의 core dump 정책이 정한다** — CI 에서 크래시 테스트가 느리거나 /var/crash 가 차는 흔한 이유다.

### HW 브레이크포인트 / 워치포인트 (`demo_hwbp.c`, 이 VM 에서 동작함)

소프트웨어 BP 는 코드를 고쳐야 한다. 코드가 ROM/플래시에 있거나, 공유 매핑이거나, 데이터 접근을 잡아야 하면(워치포인트) CPU 의 디버그 레지스터를 쓴다. arm64 Linux 는 `PTRACE_SETREGSET` + `NT_ARM_HW_BREAK` / `NT_ARM_HW_WATCH` 로 `struct user_hwdebug_state`(uapi ptrace.h:102–110) 를 쓴다. 제어 워드는 arch/arm64/include/asm/hw_breakpoint.h:33–42 `encode_ctrl_reg`: `len << 5 | type << 3 | privilege << 1 | enabled` (실행 = type 0, len 0xf; 쓰기 감시 = type 2, 8 바이트 len 0xff; EL0 = privilege 2). 실제 출력:

```text
HW breakpoint slots 6 (debug arch 0x6), watchpoint slots 4
hw bp set=0: sig=5 si_code=4 (TRAP_HWBKPT=4) si_addr==hw_target yes pc==hw_target yes, text word unchanged yes
  CONT without disabling: stopped again sig=5 si_code=4 at same pc yes
hw watch set=0: sig=5 si_code=4 si_addr==&g_watch yes, pc=work+0x34, g_watch still 0 (store not yet done)
  disable + SINGLESTEP: si_code=2 (TRAP_TRACE=2), g_watch now 0x305944cc134933d1
  HW watch: CONT → hit took 63 us for N=4000 loop iterations
SW watch (single-step + PEEK each step): 24008 steps, 145690 us total, 6.07 us/step, 2303x slower than HW
```

- Apple Virtualization(vz) VM 안에서도 BP 6 개 · 워치 4 개 슬롯이 보이고 실제로 트리거됐다.
- **HW BP 도 ptrace 사용자가 직접 넘겨야 한다**: 끄지 않고 CONT 하면 같은 PC 에서 또 멈췄다. 커널 hw_breakpoint.c:667 `if (is_default_overflow_handler(bp)) step = 1;` — ptrace 가 건 BP 는 오버플로 핸들러가 `ptrace_hbptriggered` 라서 커널이 자동 스텝을 안 해 준다. 그래서 디버거는 "끄기 → SINGLESTEP → 다시 켜기" 를 한다(소프트웨어 BP 의 step-over 와 같은 모양).
- **워치포인트는 저장 "전" 에 멈춘다**: 정지 시점 `g_watch` 는 아직 0, `pc` 는 `str` 명령(`work+0x34`). 끄고 한 스텝 하면 값이 바뀐다.
- **소프트웨어 워치포인트는 3 자릿수 이상 느리다**: 같은 감시를 매 명령 SINGLESTEP + PEEK 로 하면 24008 스텝(루프 본문 6 명령 × 4000 + 8)에 세 번 돌려 6.1 / 37 / 110 µs/step (HW 대비 2303× / 12702× / 36929×) — 호스트 load 10–17 이라 크게 흔들렸다. **비율의 크기(10³–10⁴ 배 이상)만 믿을 것.** gdb 가 "Hardware watchpoint 2" 를 못 얻으면 "Software watchpoint" 로 떨어지면서 극적으로 느려지는 이유다.

### Yama `ptrace_scope` (`demo_yama.c`)

```text
kernel.yama.ptrace_scope = 1, euid = 501
sibling  → T1 (no PR_SET_PTRACER)      : denied, errno=1 (Operation not permitted)
sibling  → T2 (PR_SET_PTRACER_ANY)     : OK (stopped sig 19, detached)
parent   → T1 (ancestor)               : OK (stopped sig 19, detached)
```

security/yama/yama_lsm.c:23–28 의 0 DISABLED / 1 RELATIONAL / 2 CAPABILITY / 3 NO_ATTACH 중 이 VM 은 1(Ubuntu 기본, 소스의 기본값도 `YAMA_SCOPE_RELATIONAL`). `yama_ptrace_access_check`(yama_lsm.c:349–) 는 attach 때 "후손이거나, `PR_SET_PTRACER` 로 허락받았거나, `CAP_SYS_PTRACE`" 를 요구한다. 이 레슨의 디버거는 `PTRACE_TRACEME`(자식이 부모를 지명)라서 scope 1 에 걸리지 않는다. 이미 돌고 있는 서비스에 `gdb -p` 가 "Operation not permitted" 를 내는 흔한 이유가 이것이다.

### exec 한 별도 프로그램 (`demo_exec.c` + `demo_target.c`)

```text
exec stop: sig=5 pc=0xf7ed8683e5c0
  maps: bfc4243c0000-bfc4243c1000 r-xp 00000000 00:26 294147                     /tmp/OS-A02_work/target
  maps: bfc4243df000-bfc4243e1000 rw-p 0000f000 00:26 294147                     /tmp/OS-A02_work/target
  maps: f7ed86822000-f7ed8684c000 r-xp 00000000 fd:01 3096                       /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1
        ^ PC is here (dynamic loader)
e_type=ET_DYN(PIE) base=0xbfc4243c0000 checksum_packet st_value=0xa40 → bp=0xbfc4243c0a40
AT_ENTRY=0xbfc4243c0900 == base + e_entry(0x900) ? yes
hit 1: checksum_packet(name="imu", len=3)  lr=0xbfc4243c0878
hit 2: checksum_packet(name="gps", len=3)  lr=0xbfc4243c0878
hit 3: checksum_packet(name="baro", len=4)  lr=0xbfc4243c0878
[target] imu  96e7415a
[target] gps  413be7a3
[target] baro 57da7f2f
exited code=22 after 3 hits
```

- `PTRACE_TRACEME` 후 `execve` 가 성공하면 커널이 SIGTRAP 으로 세운다. 그 순간 PC 는 `main` 은커녕 실행 파일 안도 아니고 **동적 로더의 진입점**이다. libc 는 아직 매핑도 안 됐다 → libc 함수에 BP 를 걸려면 로더가 끝난 뒤(예: 실행 파일의 `main` BP 에서)에 걸어야 한다. gdb 가 "pending breakpoint" 를 두는 이유.
- PIE 는 `st_value` 가 0 기준 상대 주소다(`nm`: `0000000000000a40 T checksum_packet`). 로드 베이스 = maps 에서 실행 파일 경로·offset 0 인 첫 줄. `auxv` 의 `AT_ENTRY == base + e_entry` 로 교차 확인했다.
- `x0` 는 `const char *` — 디버기 주소라서 PEEKDATA 로 8 바이트씩 읽어 NUL 까지 모은다. 타깃의 `printf` 줄이 히트 뒤에 몰려 나온 것은 stdout 이 파이프라 완전 버퍼링돼 종료 때 flush 됐기 때문이다.

### macOS — 같은 이름, 다른 세계 (맥에서 실행해 봄)

macOS 27.2 `man 2 ptrace` 에는 `PT_TRACE_ME, PT_DENY_ATTACH, PT_SIGEXC, PT_CONTINUE, PT_STEP, PT_KILL, PT_ATTACHEXC, PT_DETACH` 만 설명돼 있다. SDK 헤더(`MacOSX26.4.sdk/usr/include/sys/ptrace.h`)에는 `PT_READ_D` 등이 정의돼 있지만 실제로 불러 보면:

```text
stopped=1 sig=17
PT_READ_D r=-1 errno=22 (Invalid argument)
PT_WRITE_D r=-1 errno=22 (Invalid argument)
PT_STEP r=0 errno=0 (Undefined error: 0)
after step: stopped=1 sig=5 exited=0
killed=1 sig=9
exit=139          ← PT_DENY_ATTACH 한 자식에 PT_ATTACHEXC → 부모가 SIGSEGV
```

메모리 읽기/쓰기는 ptrace 로 안 된다. 마지막 줄은 man 페이지 그대로다: "An attempt by the parent to trace a process which has set this flag will result in a segmentation violation in the parent." (이 실험은 고아 자식과 `.ips` 크래시 리포트를 남겨서 직접 정리했다 — 재현하지 말 것.) lldb/debugserver 는 Mach 쪽 API — `task_for_pid` 로 얻은 task port 에 `mach_vm_read/write`, `thread_get_state/set_state`, exception port 로 트랩 받기 — 를 쓰고, 다른 프로세스의 task port 를 얻으려면 서명/entitlement(`get-task-allow` 등)와 SIP 조건이 붙는다 (문서·일반 지식 기준, 이 레슨에서 Mach API 로 디버거를 짜 보지는 않았다).

### 펌웨어 연결 — MCU 에는 ptrace 가 없다

- **GDB Remote Serial Protocol**: `arm-none-eabi-gdb` 는 GDB 서버(OpenOCD, J-Link GDB Server, pyOCD, QEMU gdbstub)와 텍스트 패킷으로 말한다. 이 레슨의 함수가 그대로 패킷이 된다: `m addr,len`/`M` = PEEK/POKE, `g`/`G` = GETREGSET/SETREGSET, `Z0`/`z0` = 소프트웨어 BP 심기/지우기, `Z1` = HW BP, `Z2` = 쓰기 워치포인트, `vCont;s`/`vCont;c` = SINGLESTEP/CONT, 정지 응답 `T05` = SIGTRAP (GDB 매뉴얼 "Remote Protocol" 기준). 서버가 그 아래에서 SWD/JTAG 로 코어를 멈춘다(halting debug, `DHCSR`).
- **Cortex-M 의 BP**: 코드가 플래시에 있어 POKE 처럼 고쳐 쓸 수 없다 → FPB(Flash Patch and Breakpoint) 비교기 = HW BP. 개수가 적다(코어·구현마다 다름 — Cortex-M3/M4 는 보통 명령 비교기 6 개, M0/M0+ 는 더 적다 — 해당 TRM 확인). RAM 에서 도는 코드라면 소프트웨어 BP = Thumb `BKPT #imm8`(16 비트 `0xBExx`)를 쓸 수 있다. 디버거가 붙어 있지 않을 때 `BKPT` 를 만나면 (DebugMonitor 예외도 꺼져 있으면) HardFault 로 승격된다 — ARM-M12(HardFault 분석기)에서 `HFSR.DEBUGEVT` 로 보이는 경우다 (ARMv7-M ARM 기준, 실보드로 확인하지 않음). 세미호스팅이 `BKPT 0xAB` 를 쓰는 것도 같은 구조다.
- **워치포인트 = DWT 비교기**: BM-A10 에서 쓴 DWT 의 `CYCCNT` 옆에 주소 비교기들이 있다. "누가 이 변수를 망가뜨리나" 를 MCU 에서 잡는 가장 싼 도구.
- **멈추면 세상은 계속 돈다**: 리눅스 디버기는 멈춰도 다른 프로세스가 기다려 주지만, MCU 를 halt 하면 모터 PWM·DMA·워치독은 계속 간다(일부는 `DBGMCU` freeze 설정 가능 — STM32 기준). 그래서 실시간 루프에서는 BP 대신 ITM/RTT 트레이스나 사후 덤프(ARM-M14)를 쓴다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
# VM 안, /tmp/OS-A02_work/
gcc   -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 51/51 passed (8 회 반복 + cwd=/ 에서도 동일, <10 ms)
clang -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 51/51 passed
gcc   -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan   # 51/51, 오류 0
clang -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan   # 51/51, 오류 0
```

ASan 빌드에서도 디버거·디버기가 같은 (계측된) 바이너리다. 자식은 `_exit` 로 끝나므로 LeakSanitizer 가 자식에서 돌지 않고, 부모의 LSan(종료 시 자기 스레드를 ptrace 로 세우는 방식)은 부모가 누구에게도 trace 되지 않으니 충돌하지 않았다. 싱글스텝 수는 `c_poly` 만 바뀌었다(32/21 — 범위 검사인 이유).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (ptrace(PTRACE_PEEKDATA, ...) == -1) return -1;` | 워드 값이 `0xffff…ffff` 면 정상 데이터를 오류로 본다 | `errno = 0; w = ptrace(...); if (w == -1 && errno) return -errno;` |
| BP 심을 때 저장한 **8 바이트 워드**를 통째로 복원 | 같은 워드의 다른 절반에 그 뒤 심은 BP 를 지운다 (Part A 두 BP 테스트가 잡는다) | 원래 **4 바이트**만 저장, 복원은 지금 워드를 다시 읽어 4 바이트만 교체 |
| x86 습관: 히트 후 `pc -= 1`(또는 `-= 4`) | arm64 는 PC 가 이미 BRK 주소 → 앞 명령을 한 번 더 실행 | arm64 는 PC 그대로 복원·스텝. 아키텍처별로 "트랩 후 PC" 를 실측해서 정한다 |
| BP 위에서 그냥 `PTRACE_CONT` | 같은 BRK 를 다시 실행 → 무한히 같은 자리 | 원래 명령 복원 → SINGLESTEP → 재삽입 → CONT |
| `PTRACE_CONT` 의 data 를 늘 0 | 디버기의 시그널을 삼킨다 (핸들러 안 돎, SIGINT 로 안 꺼짐) | signal-delivery-stop 의 시그널을 기억했다가 다음 재개의 data 로 |
| 프로그램 자신의 `brk #imm` 에서 그냥 cont | 그 BRK 를 또 실행 → 무한 트랩 | 내 BP 표에 없고 BRK 면 `pc += 4` (연습 2). 반대로 내 BP 에 `pc += 4` 하면 원래 명령을 건너뛴다 |
| detach 전에 BP 를 안 지움 | 디버기가 BRK 를 만나면 받아 줄 tracer 가 없다 → SIGTRAP 사망 (실측) | 모든 BP `bp_clear` 후 `PTRACE_DETACH` |
| finish 를 `pc == LR` 로만 판정 | 재귀면 안쪽 호출의 복귀에서 멈춘다 | `pc == LR && sp == 입구 sp` |
| 타깃 함수를 직접 상수 인자로 호출 | gcc `*.constprop.0` 복제본 → BP 를 안 지나감 | volatile 함수 포인터 / 디버그 빌드 / 실제 호출 경로 확인(`objdump`) |
| 크래시 테스트 자식에 `RLIMIT_CORE = 0` 이면 충분하다고 가정 | core_pattern 이 파이프(apport/systemd-coredump)면 한도와 무관하게 핸들러가 돈다 → 느리고 부하에 따라 감시 시간을 넘는다 (실제로 50/51) | 테스트 자식은 `RLIMIT_CORE = 1`(커널 특수값) 또는 시그널로 죽지 않게 설계 |
| 이미 회수한 pid 에 `kill` | pid 재사용 시 남의 프로세스를 죽인다 | 회수는 raw `waitpid` 상태로 추적하고, 회수 후 `pid = 0` |

## 8. 면접 Q&A

<details>
<summary>Q1. 디버거의 소프트웨어 브레이크포인트는 실제로 무엇을 하나? 꼬리: 코드 페이지는 읽기 전용인데 어떻게 쓰나?</summary>

그 주소의 명령을 트랩 명령(arm64 `BRK #0` 4 바이트, x86 `int3` 1 바이트)으로 덮어쓰고 원래 명령을 보관한다. 디버기가 그 명령을 실행하면 동기 예외 → 커널이 SIGTRAP 을 보내고, tracee 의 시그널은 정지로 바뀌어 디버거의 `waitpid` 가 깨어난다. 계속할 때는 원래 명령을 되돌려 한 스텝 실행하고 다시 덮어쓴다. 쓰기는 커널이 대신 한다: `PTRACE_POKEDATA` → `access_remote_vm(FOLL_FORCE|FOLL_WRITE)` 이고, FOLL_FORCE 는 쓰기 권한 없는 **private** 매핑에도 COW 사본을 만들어 쓴다(mm/gup.c `can_follow_write_common`). 그래서 디스크의 바이너리나 같은 파일을 실행 중인 다른 프로세스는 영향이 없다 — 이 레슨에서 부모의 코드가 그대로인 것을 CHECK 했다.

</details>

<details>
<summary>Q2. BRK 트랩 후 PC 는 어디를 가리키나? x86 과 무엇이 다른가? 꼬리: 디버거는 자기 BP 와 프로그램 자신의 BRK 를 어떻게 구분하나?</summary>

arm64: **BRK 주소 그대로** (실측 `pc == si_addr == bp`; 커널 `do_el0_brk64` 는 PC 를 건드리지 않는다). x86 int3: 트랩형이라 다음 명령 주소 = addr + 1 → 디버거가 `rip -= 1`. 구분: 정지 PC 가 내 BP 표에 있으면 내 것. 아니면 명령을 읽어 `(insn & 0xffe0001f) == 0xd4200000` 이면 프로그램의 BRK 이고 imm16 으로 주인을 알 수 있다(`#0x3e8` = gcc `__builtin_trap`, `#5` = 커널 uprobes). 프로그램 BRK 는 `pc += 4` 로 넘기거나 사용자에게 "trap" 으로 보고한다 (연습 2 에서 `imm == 0x123` 확인).

</details>

<details>
<summary>Q3. step-over-breakpoint 를 멀티스레드 프로그램에서 하면 무엇이 틀리나? 이 디버거를 어떻게 바꾸겠나?</summary>

원래 명령을 되돌려 놓은 ②–④ 창 동안 다른 스레드가 그 주소를 지나가면 BP 를 놓친다. 또 각 스레드는 따로 정지·재개되므로(`ptrace` 는 스레드 단위) 한 스레드의 SIGTRAP 을 처리하는 동안 다른 스레드는 계속 돈다. 바꾸는 법: (1) all-stop — 이벤트가 오면 모든 스레드를 세운다(`PTRACE_INTERRUPT`/`SIGSTOP`, `waitpid(__WALL)`, `PTRACE_O_TRACECLONE` 으로 새 스레드도 추적), step-over 동안 그 스레드만 스텝; (2) **displaced stepping** — 원래 명령을 다른 스크래치 주소에 복사해서 거기서 실행하고 PC/분기 결과를 보정, BP 는 한 번도 지우지 않는다. gdb 가 리눅스에서 쓰는 방식이 (2) 다 (gdb 문서 "displaced stepping" 기준, 이 레슨에서 구현하지 않음). PC-상대 명령(`adr`, `b`, `ldr literal`)은 복사하면 의미가 바뀌어 에뮬레이션이 필요하다는 것이 어려운 부분이다.

</details>

<details>
<summary>Q4. 싱글스텝은 하드웨어적으로 어떻게 되나? 꼬리: LL/SC(ldxr/stxr) 루프를 싱글스텝하면?</summary>

arm64 Linux 는 `user_enable_single_step` 이 tracee 의 저장된 `SPSR` 에 SS 비트를 세운다(debug-monitors.c:382–388). 커널 복귀(eret) 후 명령 하나를 실행하면 Software Step 예외 → `do_el0_softstep` → SIGTRAP `TRAP_TRACE`, 그리고 "ptrace will disable single step unless explicitly asked to re-enable it" (debug-monitors.c:187 주석). 꼬리: 스텝마다 예외 진입·복귀가 일어나는데, 예외가 exclusive monitor 를 지울 수 있어서 `ldxr` 와 `stxr` 사이에서 멈추면 `stxr` 이 영원히 실패하는 루프가 될 수 있다. 그래서 gdb 는 aarch64 의 원자 시퀀스(`ldxr … stxr`)를 통째로 건너뛰는 처리를 갖고 있다 (gdb 소스·문서 기준, 이 레슨에서 재현하지 않음). LSE 의 `cas`/`ldadd` 한 명령이면 이 문제가 없다 (OS-A01 의 outline atomics 참고).

</details>

<details>
<summary>Q5. HW 브레이크포인트와 SW 브레이크포인트의 차이는? 언제 HW 가 반드시 필요한가? 꼬리: 워치포인트는 쓰기 전에 멈추나 후에 멈추나?</summary>

SW: 개수 무제한, 코드를 고친다(쓰기 가능한 private 매핑이어야, 체크섬/자기검사 코드가 눈치챌 수 있음). HW: 디버그 레지스터 비교기, 코드 불변, 개수 제한(이 VM: BP 6 · 워치 4). 필수인 경우: 코드가 ROM/플래시(MCU, XIP), 공유 매핑, 아직 로드되지 않은 코드 주소, 그리고 **데이터 접근** 감시(워치포인트). 이 VM 실측에서 쓰기 워치포인트는 `str` 명령 실행 **전**에 멈췄다(값은 아직 옛 값). ptrace 가 건 HW BP/워치는 커널이 자동으로 넘겨 주지 않으므로(hw_breakpoint.c:667) 디버거가 끄고-스텝-켜기를 한다. 소프트웨어 워치포인트(매 명령 스텝 + 비교)는 실측 2×10³–4×10⁴ 배 느렸다.

</details>

<details>
<summary>Q6. Yama ptrace_scope 는 무엇을 막나? 운영 서버에서 디버깅이 안 될 때 무엇을 보나?</summary>

같은 uid 라도 아무 프로세스에나 attach 하면, 악성 코드 하나가 같은 사용자의 브라우저·ssh-agent 메모리를 읽을 수 있다. scope 1(이 VM)은 attach 를 "조상만, 또는 `PR_SET_PTRACER` 로 허락받은 경우, 또는 `CAP_SYS_PTRACE`" 로 제한한다 — 실측: 형제 attach EPERM, `PR_SET_PTRACER_ANY` 한 대상은 OK, 부모는 OK. 확인 순서: `/proc/sys/kernel/yama/ptrace_scope`, 대상의 dumpable(setuid, `PR_SET_DUMPABLE 0` 이면 PEEK 도 EIO), 컨테이너면 seccomp 프로필과 `CAP_SYS_PTRACE`, 이미 다른 tracer 가 있는지(`/proc/<pid>/status` 의 `TracerPid` — 안티디버깅 코드도 이걸 본다). 서비스가 자기 크래시 핸들러를 위해 `PR_SET_PTRACER` 를 쓰는 패턴도 있다.

</details>

<details>
<summary>Q7. macOS 에서 같은 디버거를 짜려면? 꼬리: PT_DENY_ATTACH 는 진짜 보호인가?</summary>

macOS 의 `ptrace` 는 TRACEME/CONTINUE/STEP/KILL/ATTACHEXC/DETACH 정도이고, 실측으로 `PT_READ_D` 와 `PT_WRITE_D` 는 `EINVAL` 이었다. 메모리·레지스터는 Mach: task port(`task_for_pid`) 위에서 `mach_vm_read/write/protect`, `thread_get_state/set_state`, 트랩은 exception port 로 Mach 메시지를 받는다(lldb 의 debugserver 방식 — 문서 기준). task port 를 얻으려면 서명·entitlement·SIP 조건이 붙는다. `PT_DENY_ATTACH` 는 "부모가 trace 하려 하면 부모가 SIGSEGV" (실측 exit 139) 같은 방어지만, 호출 전에 멈추거나 그 syscall 을 패치하면 우회되는 **속도 방지턱**이지 보안 경계가 아니다.

</details>

<details>
<summary>Q8. MCU(Cortex-M)에서는 운영체제 없이 누가 이 일을 하나? 플래시 코드에 BP 는 어떻게?</summary>

코어 안의 디버그 로직이다. 디버그 프로브가 SWD/JTAG 로 `DHCSR` 에 halt 를 쓰면 코어가 멈추고, 레지스터는 `DCRSR/DCRDR`, 메모리는 AHB-AP 로 읽고 쓴다. 호스트 쪽은 OpenOCD/J-Link 가 GDB 서버로서 RSP 패킷(`m/M`, `g/G`, `Z0/Z1/Z2`, `vCont;s`)을 그 동작으로 번역한다 — 이 레슨의 함수와 1:1 이다. 플래시 코드는 POKE 할 수 없으니 FPB 비교기(HW BP, 개수 적음)를 쓰고, 다 쓰면 gdb 가 "Cannot insert hardware breakpoint" 를 낸다. RAM 코드는 Thumb `BKPT` 로 SW BP 가 가능하다. 디버거 없이 `BKPT` 를 실행하면 HardFault 로 승격될 수 있다(ARM-M12, 문서 기준). 데이터 감시는 DWT 비교기.

</details>

<details>
<summary>Q9. 이 미니 디버거 설계가 틀리는 경우는? 프로덕션 서비스에 "BP 걸고 레지스터 바꾸기" 를 쓴다면 어떻게 바꾸겠나?</summary>

틀리는 경우: (1) 멀티스레드(Q3), (2) `count_steps`/`finish` 가 longjmp·예외·시그널 핸들러 진입으로 LR 에 안 돌아오는 경로, (3) JIT/자기수정 코드가 BP 자리를 덮어씀, (4) BTI 강제 페이지에서 `pc` 를 함수 중간으로 옮기는 강제 호출, (5) 디버기가 vfork/exec 하면 BP 가 자식/새 이미지로 이어지지 않음(`PTRACE_O_TRACEFORK/EXEC` 필요), (6) 디버거가 죽으면 BP 가 남은 디버기는 SIGTRAP 사망 → 그래서 `EXITKILL`. 프로덕션: attach 는 `PTRACE_SEIZE`(SIGSTOP 부작용 없음) + `PTRACE_INTERRUPT`, 큰 메모리는 PEEK 대신 `process_vm_readv` 나 `/proc/<pid>/mem`, 정지 시간이 SLA 를 깨므로 BP 대신 uprobes/eBPF(커널이 BRK #5 로 같은 일을 하되 디버거 왕복 없이 처리)로 관측만 한다. fault injection 이 목적이면 디버거보다 코드 안의 테스트 훅/LD_PRELOAD 래퍼가 재현성과 비용 면에서 낫다.

</details>

## 9. 추가 연습

### 연습 1 — 소프트웨어 워치포인트: "누가 이 변수를 썼나"

`asm_store(p, v)` = `add w1,w1,#1 · nop · str w1,[x0] · ret`. 입구 BP 에서 시작해 한 스텝마다 감시 주소의 4 바이트를 PEEK 해서, 바뀐 순간 **그 스텝 직전의 PC**(= 쓴 명령)와 스텝 수를 돌려준다.

<details>
<summary>답</summary>

```c
for (long n = 1; n <= MAX_STEPS; n++) {
    uintptr_t pc_before = (uintptr_t)r.pc;
    ev_t e = dbg_step(d);
    if (e.kind != EVK_STEP || read_u32(d->pid, addr, &now) != 0) return -1;
    if (now != old) { *writer_pc = pc_before; *steps = n; *newval = now; return 0; }
    if (e.pc == ret) return -1;
    r.pc = e.pc;
}
```

실측: `writer pc = asm_store+8, steps = 3, new value = 42`. HW 워치포인트는 "쓰기 전" 에 멈추고(PC = str), 소프트웨어 워치포인트는 "쓴 뒤" 에 알아챈다 — 그래서 직전 PC 를 기억해 둬야 같은 답(str 명령)이 나온다.

</details>

### 연습 2 — 내 BP 가 아닌 BRK 넘기기

디버기가 `brk #0x123` 를 직접 실행한다. 디버거는 정지 PC 가 BP 표에 없음을 확인하고, 명령을 읽어 imm16 을 꺼낸 뒤 `pc += 4` 로 넘겨 프로그램을 끝까지 돌려라. 대조군: 디버거 없이 같은 코드를 돌리면 SIGTRAP 으로 죽는다.

<details>
<summary>답</summary>

```c
if (e.kind != EVK_BREAK || bp_find(d, e.pc)) return -1;
if (read_insn(d->pid, e.pc, &ins) != 0 || (ins & 0xffe0001fu) != BRK0) return -1;
*imm = (ins >> 5) & 0xffff;
get_regs(d->pid, &r); r.pc += 4; return set_regs(d->pid, &r);
```

`0xffe0001f` 는 imm16(비트 20:5)을 뺀 BRK 고정 비트 마스크. 실측 `imm == 0x123`, 이후 종료 코드 7. 대조군 `WTERMSIG == SIGTRAP` (`RLIMIT_CORE = 1` 로 apport 를 피했다 — 5. 풀이의 core dump 함정).

</details>

### 연습 3 — 미니 strace

`PTRACE_O_TRACESYSGOOD` 를 켜고 `PTRACE_SYSCALL` 로 돌리면 시스템 콜 진입/복귀마다 `WSTOPSIG == (SIGTRAP | 0x80)` 정지가 온다. `PTRACE_GET_SYSCALL_INFO`(glibc 2.43 `struct __ptrace_syscall_info`)로 진입이면 `nr`·`args[]`, 복귀면 `rval` 을 읽어, 디버기가 `/dev/null` 에 한 `write(fd, "abc", 3)` 5 번을 세라.

<details>
<summary>답</summary>

```c
if (WSTOPSIG(st) != (SIGTRAP | 0x80)) { sig = WSTOPSIG(st) == SIGTRAP ? 0 : WSTOPSIG(st); continue; }
struct __ptrace_syscall_info si;
ptrace(PTRACE_GET_SYSCALL_INFO, d->pid, (void *)sizeof si, &si);
if (si.op == PTRACE_SYSCALL_INFO_ENTRY) { in_target = si.entry.nr == SYS_write && si.entry.args[2] == len; ... }
else if (si.op == PTRACE_SYSCALL_INFO_EXIT) { if (in_target && si.exit.rval == len) out->exits_ok++; }
```

실측 `write(.., 3) entries=5 exits_ok=5 exit_group=1 code=0`. `0x80` 비트가 없으면 시스템 콜 정지와 진짜 SIGTRAP 을 구분할 수 없다. aarch64 에서는 시스템 콜 번호가 `x8`, 인자가 `x0..x5`, 반환이 `x0` 이라 진입/복귀를 토글로 세는 옛 방식도 되지만, GET_SYSCALL_INFO 가 진입/복귀를 직접 알려 준다. `strace` 가 하는 일의 바닥이 이것이다.

</details>

### 연습 4 — 강제 함수 호출 (gdb 의 `call` / `print f(6)`)

`target_add` BP 에서 멈춘 디버기에게 `c_poly(6)` 을 실행시키고 반환값을 받은 뒤 레지스터를 통째로 되돌려, 디버기가 아무 일 없었던 것처럼 끝나게 하라. 복귀 착지점은 디버기가 절대 부르지 않는 `call_pad` 에 임시 BP.

<details>
<summary>답</summary>

```c
get_regs(d->pid, &saved); r = saved;
r.regs[0] = arg; r.pc = fn; r.regs[30] = pad; r.sp = (saved.sp - 256) & ~(uint64_t)15;
bp_set(d, pad); set_regs(d->pid, &r);
e = dbg_cont(d);                 /* fn 실행 → ret → pad 의 BRK */
*ret = (e.kind == EVK_BREAK && e.pc == pad) ? x0 : ...;
bp_clear(d, pad); set_regs(d->pid, &saved);
```

실측 `c_poly(6) in debuggee = 131`(부모가 직접 부른 값과 같음), 복원 후 `pc == target_add, x0 == 1, x1 == 2`, `call_pad` 원래 명령 복원, 종료 코드 3. `sp` 를 내리고 16 정렬하는 것은 AAPCS64 규칙 + 멈춘 함수의 스택을 건드리지 않기 위해서다(AAPCS64 리눅스에는 x86-64 SysV 같은 128 바이트 red zone 이 없다고 알고 있지만 — 문서 기준 — 여유를 둔다). 호출된 함수가 다른 BP 에 걸리거나 크래시하면 디버기 상태가 꼬인다 — gdb 도 "The program being debugged stopped while in a function called from GDB" 로 같은 상황을 알린다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
# VM 안, /tmp/OS-A02_work/
gcc   -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr      # 18/18 passed (<10 ms)
clang -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr      # 18/18 passed
gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 18/18, 오류 0 (clang 도 동일)
```

데모 (위 5. 풀이에 실제 출력을 붙였다, 모두 gcc/clang strict 빌드 통과):

```bash
gcc -std=c11 -Wall -Wextra -Werror -O2 demo_hwbp.c -o hwbp && ./hwbp
gcc -std=c11 -Wall -Wextra -Werror -O2 demo_yama.c -o yama && ./yama
gcc -std=c11 -Wall -Wextra -Werror -O2 -g demo_target.c -o target
gcc -std=c11 -Wall -Wextra -Werror -O2 demo_exec.c -o dexec && ./dexec ./target checksum_packet
```

<!-- code: demo_hwbp.c -->

<!-- code: demo_yama.c -->

<!-- code: demo_exec.c -->

<!-- code: demo_target.c -->

## 10. 핵심 정리

- 디버거 = tracer 부모. `TRACEME`/`ATTACH` → 디버기의 모든 시그널이 **정지**로 바뀌어 `waitpid` 로 온다. 메모리는 `PEEK/POKE`(8 B 워드, `-1` 은 errno 로 판정), 레지스터는 arm64 에서 `GETREGSET + NT_PRSTATUS`.
- SW BP = 명령을 `BRK #0`(0xd4200000, 4 B)로 덮어쓰기. 워드 안 4 바이트만 RMW, 원래 4 바이트만 복원. 코드 페이지 쓰기는 `FOLL_FORCE` 의 private COW — 디스크·다른 프로세스는 그대로.
- arm64 BRK 트랩 후 **PC = BRK 주소** (실측 + `do_el0_brk64`). x86 int3 는 addr+1 → 되감기 필요. 아키텍처마다 "트랩 후 PC" 를 확인하라.
- continue = 원래 명령 복원 → SINGLESTEP(`TRAP_TRACE`) → 재삽입 → CONT. 멀티스레드는 이 창에서 BP 를 놓친다 → all-stop / displaced stepping.
- si_code 로 원인 구분: `TRAP_BRKPT` 1 · `TRAP_TRACE` 2 · `TRAP_HWBKPT` 4. 다른 시그널은 기억했다가 재개 때 돌려준다(안 그러면 삼킴).
- finish = LR 임시 BP + `sp` 비교, 반환값 주입 = 복귀 지점에서 `x0` 교체, detach 전 BP 전부 제거(안 하면 SIGTRAP 사망), `EXITKILL` 로 고아 방지.
- HW BP/워치(이 VM: 6/4 슬롯)는 코드를 안 고친다. 워치는 저장 전에 멈춘다. SW 워치포인트는 10³–10⁴ 배 이상 느리다 (실측).
- MCU: OS 대신 코어 디버그 로직 + 프로브 + GDB RSP(`Z0/Z1/Z2`, `m/M`, `g/G`). 플래시 코드엔 FPB(HW BP), 데이터엔 DWT.

## 참고자료

- Linux man-pages: ptrace(2) <https://man7.org/linux/man-pages/man2/ptrace.2.html> (PEEK 의 errno 판정, `PTRACE_O_EXITKILL`, `PTRACE_GET_SYSCALL_INFO`, signal-delivery-stop), wait(2), proc_pid_maps(5), getauxval(3)
- POSIX `waitpid`: <https://pubs.opengroup.org/onlinepubs/9799919799/functions/waitpid.html>
- Linux 6.18.55 소스 (VM `/var/lab/src/linux-6.18.55/`): arch/arm64/kernel/debug-monitors.c:163–192, 254–262, 382–388 · arch/arm64/kernel/ptrace.c:171–200, 1589–1596 · arch/arm64/kernel/hw_breakpoint.c:631–667 · arch/arm64/include/uapi/asm/ptrace.h:49–74, 88–110 · arch/arm64/include/asm/hw_breakpoint.h:33–54 · arch/arm64/include/asm/insn-def.h:15 · arch/arm64/include/asm/brk-imm.h:25 · kernel/ptrace.c:44–66, 497–517, 600–612, 1434–1453 · mm/gup.c:584–600 · security/yama/yama_lsm.c:23–28, 349–385 · fs/coredump.c:884, 978–997 · arch/x86/kernel/traps.c:873
- Linux 문서: Documentation/admin-guide/LSM/Yama.rst (ptrace_scope)
- GDB 매뉴얼 — Remote Serial Protocol (패킷 `m`, `M`, `g`, `G`, `Z0`–`Z4`, `vCont`), Displaced stepping, Set Breaks: <https://sourceware.org/gdb/current/onlinedocs/gdb/>
- Arm Architecture Reference Manual (Armv8-A): BRK, Software Step, Breakpoint/Watchpoint 레지스터 (DBGBCR/DBGWCR), PSTATE.BTYPE — <https://developer.arm.com/documentation/ddi0487/latest>
- Armv7-M Architecture Reference Manual: FPB, DWT, BKPT, DHCSR, DebugMonitor 예외 — <https://developer.arm.com/documentation/ddi0403/latest>
- macOS 27.2 `man 2 ptrace`, `MacOSX26.4.sdk/usr/include/sys/ptrace.h` (이 맥에서 읽음)
- OSTEP ch.5 Process API (fork/exec/wait), ch.6 Limited Direct Execution (트랩·특권 모드) — 디스크의 book-md
- 이 노트의 관련 레슨: TOOL-B10 (lldb/gdb 사용법), ARM-M12 (HardFault 분석기), ARM-M14 (크래시 레지스터 덤프), BM-A10 (DWT), OS-A01 (outline atomics, LL/SC vs LSE), OS-M03 (COW fork)
- 다음 단계 (아직 없는 레슨): TOOL-M07 (조건부 BP·워치포인트·스크립팅), TOOL-A03 (QEMU gdbstub 로 OpenOCD 흐름), ELX-A02 (KGDB), RISCV-A09 (RISC-V 디버그 스펙·트리거), TOOL-A04 (세미호스팅·ITM/RTT), LPERF-B07 (strace -c · perf trace). lock-free 의 ABA 문제는 이 아이템이 아니라 DS-A04 에서 다룬다.
