---
id: OS-A09
time: 110분
updated: 2026-11-15
---
# OS-A09 xv6 커널 해킹 1: 시스템 콜 추가 (trace, sysinfo)

## 학습 목표

- 사용자 프로그램의 `trace(mask)` 한 줄이 **usys.S 스텁 → `ecall` → trampoline `uservec` → `usertrap()` → `syscall()` 디스패치 → `sys_trace()` → `a0` 로 반환**되는 길을 실제 xv6-riscv 소스의 파일:줄로 따라가고, gdb 로 그 길 위에 멈춰 본다.
- 새 시스템 콜 하나에 손대야 하는 **7곳**(번호, 디스패치 표, 사용자 스텁, 프로토타입, 커널 구현, proc 필드, Makefile)을 빠짐없이 고친다.
- `trace` 마스크를 **fork 로 상속**하고 exec 를 건너 유지시키며, 왜 자식 쪽에는 `fork -> 0` 줄이 찍히지 않는지 설명한다.
- `sysinfo` 로 빈 메모리(freelist 순회, 잠금)와 프로세스 수를 모아 **`copyout`** 으로 돌려주고, 잘못된 포인터 5가지에서 커널이 어떻게 거절하는지 테스트로 확인한다.
- `make grade` 처럼 QEMU 를 띄워 셸에 명령을 치고 출력을 대조하는 채점기(`run_xv6.py`)를 직접 만들어, 시작 코드는 빨리 FAIL 하고 정답은 `usertests -q` 까지 통과하게 한다.

## 1. 문제

"`read()` 를 부르면 실제로 무슨 일이 일어나나?" — 펌웨어·임베디드 리눅스 면접에서 가장 자주 나오는 OS 질문 중 하나다. 답을 외우는 대신 **진짜 커널에 시스템 콜 두 개를 추가**해 보면, 트랩 진입, 레지스터로 넘어오는 인자, 사용자 포인터 검증, 반환값이 지나가는 레지스터를 손으로 확인하게 된다. Cortex-M RTOS 의 `SVC` 게이트(RTOS-A01, ARM-M09)나 AArch64 의 `svc #0`(ARM-M13)도 같은 모양이다.

대상은 MIT 의 교육용 커널 **xv6-riscv**(upstream main, 커밋 `fa2ddcb7`, 2026-11-05)다. 모든 빌드와 부팅은 Lima VM `elinux`(aarch64 Ubuntu) 안에서 `riscv64-linux-gnu-gcc 15.2.0` + `qemu-system-riscv64 10.2.1` 로 한다. 과제의 뼈대는 MIT 6.1810 의 syscall 랩(2022년 판 "System call tracing" + "Sysinfo")과 같지만, 코드·테스트·채점기는 이 레슨에서 새로 썼다.

> 이 xv6 는 최근 버전이라 예전 교재와 이름이 다르다: `printf` → `printk`, `exit/fork/wait` 의 커널 쪽은 `kexit/kfork/kwait`, `sleep` 시스템 콜은 `pause`, `sbrk()` 는 즉시 할당·`sbrklazy()` 는 지연 할당(user/ulib.c:153–162), 첫 프로세스는 initcode 없이 `forkret()` 이 `kexec("/init")` 을 부른다(kernel/proc.c:532). 이 레슨의 파일:줄은 모두 **고치기 전 원본(pinned commit)** 기준이다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">trace(mask) 한 번이 지나가는 길 (xv6-riscv, 원본 파일:줄)</text>
<rect x="10" y="35" width="220" height="110" class="f-mut" stroke="currentColor"/>
<text x="20" y="55" fill="currentColor">U-mode: tracetest</text>
<text x="20" y="77" fill="currentColor" font-size="12" font-family="monospace">trace(mask);  a0 = mask</text>
<text x="20" y="97" fill="currentColor" font-size="12" font-family="monospace">usys.S: li a7, 23</text>
<text x="20" y="117" fill="currentColor" font-size="12" font-family="monospace">        ecall</text>
<text x="20" y="137" fill="currentColor" font-size="12" font-family="monospace">        ret   ← a0 = 반환값</text>
<rect x="260" y="35" width="220" height="110" class="f-warn" stroke="currentColor"/>
<text x="270" y="55" fill="currentColor">trampoline.S uservec (21)</text>
<text x="270" y="77" fill="currentColor" font-size="12">하드웨어: scause=8, sepc=ecall</text>
<text x="270" y="97" fill="currentColor" font-size="12">레지스터 31개 → TRAPFRAME (39–72)</text>
<text x="270" y="117" fill="currentColor" font-size="12">sp/tp/satp ← 커널 값 (75–94)</text>
<text x="270" y="137" fill="currentColor" font-size="12">jalr usertrap (97)</text>
<rect x="510" y="35" width="220" height="110" class="f-acc" stroke="currentColor"/>
<text x="520" y="55" fill="currentColor">trap.c usertrap (38)</text>
<text x="520" y="77" fill="currentColor" font-size="12">scause == 8 → 시스템 콜 (54)</text>
<text x="520" y="97" fill="currentColor" font-size="12">epc += 4 (62)  intr_on (66)</text>
<text x="520" y="117" fill="currentColor" font-size="12">syscall() (68)</text>
<text x="520" y="137" fill="currentColor" font-size="12">prepare_return (88, 101)</text>
<line x1="230" y1="117" x2="260" y2="117" stroke="currentColor"/>
<line x1="480" y1="137" x2="510" y2="137" stroke="currentColor"/>
<rect x="510" y="175" width="220" height="110" class="f-acc" stroke="currentColor"/>
<text x="520" y="195" fill="currentColor">syscall.c syscall() (137)</text>
<text x="520" y="217" fill="currentColor" font-size="12">num = trapframe-&gt;a7 (142)</text>
<text x="520" y="237" fill="currentColor" font-size="12">범위 검사 (143)</text>
<text x="520" y="257" fill="currentColor" font-size="12">a0 = syscalls[num]() (146)</text>
<text x="520" y="277" fill="currentColor" font-size="12">★ 여기서 trace 줄 출력</text>
<line x1="620" y1="145" x2="620" y2="175" stroke="currentColor"/>
<rect x="260" y="175" width="220" height="110" class="f-ok" stroke="currentColor"/>
<text x="270" y="195" fill="currentColor">sysproc.c sys_trace()</text>
<text x="270" y="217" fill="currentColor" font-size="12">argint(0) = trapframe-&gt;a0</text>
<text x="270" y="237" fill="currentColor" font-size="12">(syscall.c argraw 35, argint 58)</text>
<text x="270" y="257" fill="currentColor" font-size="12">p-&gt;tracemask = mask</text>
<text x="270" y="277" fill="currentColor" font-size="12">return 0</text>
<line x1="480" y1="257" x2="510" y2="257" stroke="currentColor"/>
<rect x="10" y="175" width="220" height="110" class="f-warn" stroke="currentColor"/>
<text x="20" y="195" fill="currentColor">trampoline.S userret (100)</text>
<text x="20" y="217" fill="currentColor" font-size="12">satp ← 사용자 페이지 테이블 (110)</text>
<text x="20" y="237" fill="currentColor" font-size="12">레지스터 복원, a0 마지막 (116–148)</text>
<text x="20" y="257" fill="currentColor" font-size="12">sret → sepc = ecall + 4 (152)</text>
<line x1="120" y1="175" x2="120" y2="145" stroke="currentColor"/>
<text x="10" y="315" fill="currentColor" font-size="12">인자와 반환값은 모두 trapframe 의 레지스터 칸을 통해 오간다: 들어올 때 a0..a5 = 인자, a7 = 번호.</text>
<text x="10" y="335" fill="currentColor" font-size="12">나갈 때 a0 = 반환값. 같은 a0 칸을 덮어쓰므로, 인자를 반환 후에 다시 읽으면 안 된다 (추가 연습 1).</text>
<text x="10" y="355" fill="currentColor" font-size="12">사용자 포인터(struct sysinfo *)는 그냥 숫자다. 커널은 직접 역참조하지 않고 copyout 이</text>
<text x="10" y="375" fill="currentColor" font-size="12">사용자 페이지 테이블을 걸어서 쓴다 (vm.c:345). 직접 쓰면 커널이 panic 한다 (7. 실수 4, 실측).</text>
</svg>
```

### Part A — `trace(int mask)`: 호출 추적 출력과 fork 상속

- `trace(mask)` 를 부른 프로세스에서, 번호 `n` 의 시스템 콜이 **반환할 때** `mask` 의 비트 `n` 이 켜져 있으면 콘솔에 `<pid>: syscall <name> -> <ret>` 한 줄을 찍는다. `exit` 처럼 돌아오지 않는 호출은 찍히지 않는다.
- 마스크는 `struct proc` 에 저장한다. **fork 한 자식이 물려받고**, exec 는 같은 `struct proc` 이므로 그대로 남는다. 그래서 `trace 32 wc README` 가 동작한다(32 = `1 << SYS_read`).
- 사용자 명령 `trace MASK CMD ARGS...` 와 테스트 `tracetest basic|fork` 를 함께 넣는다.

### Part B — `sysinfo(struct sysinfo *)`: 빈 메모리와 프로세스 수

- `freemem` = 물리 페이지 할당기(kalloc.c)의 freelist 를 **`kmem.lock` 을 잡고** 세어 × `PGSIZE`(바이트). `nproc` = `proc[]` 에서 상태가 `UNUSED` 가 아닌 것의 수(ZOMBIE 포함).
- 결과를 사용자 포인터에 **`copyout`** 으로 쓴다. 쓸 수 없는 포인터면 -1.
- 테스트(`sysinfotest`, 17개): nproc == 3(init, sh, 자신), `sbrk(k·PGSIZE)` 후 freemem 이 **정확히 k 페이지** 줄고 되돌리면 정확히 원래대로(k = 1, 7, 64), fork 한 자식이 살아 있는 동안 nproc +1, ZOMBIE 도 wait 전까지 셈, wait 후 freemem 이 바이트 단위로 원상 복구(누수 0), 잘못된 포인터 5가지.

### Part C — 채점기와 커널 디버깅 습관

- `check.sh`(VM 안에서 실행): 원본 클론을 `/tmp/OS-A09_work/<variant>/` 로 rsync → 오버레이 → `make` → `run_xv6.py`. 원본 클론은 읽기만 한다.
- `run_xv6.py`: `make qemu CPUS=1` 을 별도 프로세스 그룹으로 띄우고, `$ ` 프롬프트를 기다려 명령을 치고, 커널의 trace 줄을 **정확히** 대조한 뒤 QEMU 를 SIGKILL 한다. 시간 제한이 있어 망가진 커널은 매달리지 않고 FAIL 한다.
- `demo_gdb.sh`: `make qemu-gdb` + `gdb-multiarch` 로 `syscall()` 과 `sys_trace()` 에 브레이크포인트를 걸어 `a7`, `scause`, 반환값을 본다.
- 펌웨어 연결: RTOS 의 SVC 시스템 콜, 리눅스의 시스템 콜 표와 seccomp 위치를 xv6 와 대조한다.

## 2. Problem (English)

> **Add two system calls to xv6-riscv.**
>
> 1. `int trace(int mask)` — sets a per-process trace mask and returns 0. While the mask has bit `n` set, every return from system call number `n` prints one console line `<pid>: syscall <name> -> <return value>`. The mask is inherited by `fork()` and survives `exec()`. Provide a user command `trace MASK CMD [ARGS...]`.
> 2. `int sysinfo(struct sysinfo *info)` — fills `info->freemem` (bytes of free physical memory) and `info->nproc` (number of processes whose state is not `UNUSED`) and returns 0, or returns -1 if `info` is not a writable user address.
>
> Constraints: `struct sysinfo { uint64 freemem; uint64 nproc; }` lives in `kernel/sysinfo.h`. Read the free list and process states under the proper locks. Do not dereference user pointers in the kernel.
>
> Tests (run under QEMU, `CPUS=1`): `tracetest basic` — the 6 trace lines of a fixed sequence (`trace`, `open`, `write`, `close`, a failing `open`, `getpid`) match exactly; `tracetest fork` — the child inherits the mask, the parent's lines are `fork -> child`, `wait -> child`, `getpid -> parent`; `trace 32 wc README` — read returns `512 ×4, 393, 0` for the 2441-byte README; `sysinfotest` — exact page accounting around `sbrk`, `fork`, zombies and five bad pointers; `usertests -q` still passes.
>
> Follow-ups: Why does the child never print `fork -> 0`? What happens if the kernel writes `*(struct sysinfo *)uaddr` directly? How expensive is counting the free list, and how would a production kernel do it? Where do strace and seccomp hook into Linux's syscall path?

## 3. 시작 코드

레슨 폴더에는 `starter.c` 대신 **오버레이 폴더**가 있다. 각 폴더는 xv6 경로를 그대로 따르고, 바뀌는 파일은 **전체 복사본**이다(패치가 아니다). 층은 이렇게 쌓인다.

| 변형 | 만드는 법 | 결과 |
|---|---|---|
| starter | 원본 + `starter/` | 컴파일·부팅은 되지만 `trace`·`sysinfo` 가 -1, trace 줄 없음 → FAIL |
| solution | 원본 + `starter/` + `solution/` | TODO 6곳을 채운 4개 파일만 `solution/` 에 있다 |
| practice | 원본 + `starter/` + `solution/` + `practice/` | 추가 연습 4개 |

`starter/` 가 이미 해 둔 "배관"(plumbing): `kernel/syscall.h`(`SYS_trace 23`, `SYS_sysinfo 24`), `syscall.c` 의 extern + 디스패치 표 항목, `user/usys.pl`(`entry("trace")`, `entry("sysinfo")`), `user/user.h` 프로토타입, `kernel/sysinfo.h`, `kernel/proc.h` 의 `int tracemask;`, `kernel/defs.h` 의 `kfreemem()`·`nproc_active()` 선언, `Makefile` UPROGS 에 `_trace _tracetest _sysinfotest`, 사용자 프로그램 3개. **TODO 는 6곳**: syscall.c 의 이름 표(A2)와 출력(A2), `sys_trace`(A1), `kfork` 상속과 `freeproc` 초기화(A3), `sys_sysinfo`(B1), `kfreemem`·`nproc_active`(B2).

`.c` 파일 첫 줄의 `/* ENV: qemu ... */` 는 이 노트 사이트용 표시다(Mac 에서 직접 빌드하지 않음). 그 한 줄 때문에 오버레이 파일의 줄 번호는 원본보다 1 이상 밀린다 — 본문의 파일:줄은 원본 기준이다.

<!-- code: starter/kernel/syscall.c -->

<!-- code: starter/kernel/sysproc.c -->

<!-- code: starter/user/tracetest.c -->

<!-- code: starter/user/sysinfotest.c -->

<!-- code: starter/user/trace.c -->

<!-- code: starter/kernel/sysinfo.h -->

<!-- code: check.sh -->

<!-- code: run_xv6.py -->

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/OS/OS-A09
limactl shell elinux -- bash -lc "bash $L/check.sh starter"    # 빨리 FAIL, exit 1
limactl shell elinux -- bash -lc "bash $L/check.sh solution"   # 30/30, exit 0 (usertests -q 포함 ~80 s)
limactl shell elinux -- bash -lc "bash $L/check.sh practice"   # 50/50, exit 0
```

시작 코드 실측(전체 2.4 s, 빌드 0.7 s 포함):

```text
built starter in /tmp/OS-A09_work/starter (riscv64-linux-gnu-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0)
FAIL: [guest tracetest basic] basic: trace() returns 0
PASS: [guest tracetest basic] basic: traced syscalls still return their normal values
FAIL: [host] trace lines of one process match exactly (6 lines)  [got []]
...
FAIL: [guest sysinfotest] nproc == 3 (init, sh, sysinfotest)
FAIL: [guest sysinfotest] sbrk 1 page: freemem -1 page, then back
...
PASS: [guest sysinfotest] badptr: address >= MAXVA -> -1
FAIL: [guest sysinfotest] badptr: struct ending exactly at p->sz -> 0
FAIL: [host] sysinfotest finished with 0 failed
10/29 passed (starter, 1 s)
```

일부 PASS 는 정상이다: 스텁이 늘 -1 을 돌려주니 "나쁜 포인터 → -1" 은 우연히 맞는다. 그래서 "끝에서 딱 맞는 포인터 → 0" 테스트를 함께 둔다. starter 는 `usertests` 를 건너뛴다(시작 코드는 커널을 망가뜨리지 않으니 1분을 쓸 이유가 없다).

**`CPUS=1` 을 고른 이유**: (1) `nproc == 3`, "두 sysinfo 사이의 할당은 내 것뿐" 같은 **정확한** 검사는 다른 하트가 아무것도 하지 않을 때만 결정적이다. (2) fork 테스트의 줄 순서. (3) `usertests -q` 가 CPUS=1 에서 57 s, 기본 CPUS=3 에서 67 s 로 오히려 빨랐다(같은 VM, 부하 평균 ~2.6, 원본 커널로 측정). 멀티코어 정확성은 `usertests` 의 기본 실행(`make qemu`)으로 따로 확인하는 것이 맞다 — 이 레슨의 테스트는 그 대체물이 아니다.

## 4. 힌트

<details><summary>힌트 1 — 새 시스템 콜에 손대는 곳 목록</summary>

번호(`kernel/syscall.h`) → 디스패치 표(`kernel/syscall.c` 의 `syscalls[]`, extern 선언) → 사용자 스텁(`user/usys.pl` 이 `li a7, SYS_x; ecall; ret` 을 생성) → 사용자 프로토타입(`user/user.h`) → 커널 구현(`sys_x` in `sysproc.c`) → 필요한 상태(`proc.h`) → 테스트 프로그램(`Makefile` UPROGS). 시작 코드가 앞의 5곳을 해 두었다. 하나라도 빠지면 무엇이 깨지는지는 7. 실수 표.

</details>

<details><summary>힌트 2 — 인자는 trapframe 에 있다</summary>

`argint(0, &mask)` 는 `myproc()->trapframe->a0` 를 읽는다(syscall.c:35–61). 이 xv6 의 `argint`/`argaddr` 는 `void` 라 실패가 없다 — 주소의 옳고 그름은 `copyin`/`copyout` 이 판단한다(syscall.c:63–65 의 주석). 반환값은 `syscall()` 이 `p->trapframe->a0` 에 넣는다(146줄). 그러면 trace 출력은 어디서 해야 반환값을 알 수 있을까?

</details>

<details><summary>힌트 3 — 이름 표</summary>

`syscalls[]` 와 똑같이 **지정 초기화자**(`[SYS_fork] = "fork"`)로 만들면 번호와 이름이 어긋날 수 없다. 출력은 `printk("%d: syscall %s -> %d\n", ...)` — 반환값은 `(int)` 로 바꿔야 -1 이 `18446744073709551615` 로 찍히지 않는다.

</details>

<details><summary>힌트 4 — 상속은 kfork 한 줄, 하지만 자식은 syscall() 을 지나지 않는다</summary>

`kfork()`(proc.c:259) 의 `safestrcpy(np->name, ...)` 근처에서 `np->tracemask = p->tracemask`. 자식은 `allocproc()` 이 `context.ra = forkret` 으로 만들어 두었으므로(proc.c:146) **`forkret` → `prepare_return` → `userret`** 로 바로 사용자 공간에 간다. 그러면 자식에게 `fork -> 0` 줄이 찍힐까?

</details>

<details><summary>힌트 5 — freemem 과 nproc</summary>

freelist 는 `struct run { struct run *next; }` 의 단일 연결 리스트다(kalloc.c:17–24). `acquire(&kmem.lock)` → 끝까지 세기 → `release`. 프로세스는 `proc[NPROC]` 배열(proc.c:11)을 돌며 `acquire(&p->lock)` 하고 `p->state != UNUSED` 를 센다. 두 함수 모두 `defs.h` 에 선언되어 있다.

</details>

<details><summary>힌트 6 — sysinfo 는 copyout 한 번</summary>

`argaddr(0, &uaddr)` → 커널 스택의 `struct sysinfo si` 를 채움 → `copyout(p->pagetable, p->sz, uaddr, (char *)&si, sizeof(si))` 가 음수면 -1. 커널 페이지 테이블에는 사용자 주소가 매핑되어 있지 않다. `*(struct sysinfo *)uaddr = si` 는 왜 안 될까?

</details>

## 5. 풀이

### 0. 길 따라가기 — 원본 파일:줄

| 단계 | 위치 | 하는 일 |
|---|---|---|
| 스텁 | user/usys.pl:9–22 → 생성된 user/usys.S | `trace: li a7, SYS_trace; ecall; ret` (tracetest.asm: `562: 48dd li a7,23`, `564: 00000073 ecall`) |
| 트랩 | 하드웨어 | U→S 모드, `scause = 8`(Environment call from U-mode), `sepc` = ecall 주소, `stvec` 로 점프 |
| 진입 | kernel/trampoline.S:21 `uservec` | `a0` 를 `sscratch` 에 대피(31) → 나머지 레지스터 30개를 TRAPFRAME 에 저장(39–68) → 원래 a0 저장(71–72) → 커널 sp/tp/usertrap 주소/satp 를 trapframe 에서 읽음(75–84) → `sfence.vma` + `csrw satp`(88–94) → `jalr t0`(97) |
| 분류 | kernel/trap.c:38 `usertrap` | `stvec` 를 kernelvec 로(47), `epc = sepc`(52), `scause == 8`(54): 죽었으면 `kexit`, **`epc += 4`**(62), `intr_on`(66), `syscall()`(68) |
| 디스패치 | kernel/syscall.c:137 | `num = trapframe->a7`(142), `num > 0 && num < NELEM(syscalls) && syscalls[num]`(143), `a0 = syscalls[num]()`(146), 아니면 `unknown sys call` + -1(148–149) |
| 인자 | syscall.c:35 `argraw`, 58 `argint`, 67 `argaddr` | trapframe 의 a0..a5 를 그대로 돌려줌 — 검증 없음 |
| 구현 | sysproc.c `sys_trace`/`sys_sysinfo` (이 레슨) | 마스크 저장 / 수집 + `copyout`(vm.c:345) |
| 복귀 | trap.c:88 `prepare_return`(101) → trampoline.S:100 `userret` | `stvec` = uservec, `sstatus.SPP = 0`, `sepc = epc`(131) → `csrw satp`(110) → 레지스터 복원, a0 마지막(148) → `sret`(152) |

`epc += 4` 가 빠지면 `sret` 이 다시 `ecall` 로 돌아가 같은 시스템 콜을 영원히 반복한다. 리눅스 RISC-V 도 똑같이 한다: `do_trap_ecall_u()` 의 `regs->epc += 4`(arch/riscv/kernel/traps.c:332, Linux 6.18.55).

### Part A — trace: 어디서 찍나

`syscall()` 은 모든 시스템 콜이 지나가는 **유일한 병목**이다. 반환값은 146줄 이후에야 있으므로, 출력도 그 뒤에 둔다.

```c
    p->trapframe->a0 = syscalls[num]();
    // read the mask AFTER the call: trace() itself is then traced by its new mask
    if ((p->tracemask >> num) & 1)
      printk("%d: syscall %s -> %d\n", p->pid, syscallnames[num],
             (int)p->trapframe->a0);
```

설계 선택 세 가지:

| 선택 | 이 레슨 | 이유 |
|---|---|---|
| 마스크를 읽는 시점 | 호출 **후** | `trace(1<<SYS_trace)` 가 자기 자신을 찍는다 — MIT 랩 페이지의 `trace 2147483647 grep hello README` 예도 첫 줄이 `4: syscall trace -> 0` 이다. 반대로 `trace(0)` 은 찍히지 않는다 |
| 출력 단위 | `printk` 한 번 = 한 줄 | `printk` 는 `pr.lock` 을 쥐고 한 번에 찍으므로(printk.c) 여러 하트의 trace 줄이 줄 중간에서 섞이지 않는다 |
| 반환값 형식 | `(int)` | 64비트 `a0` 의 -1 을 -1 로. 대신 `sbrk` 가 돌려주는 주소가 2 GiB 를 넘으면 잘린다 — xv6 사용자 주소는 그보다 훨씬 작다 |

**fork 상속과 "fork -> 0 이 없는 이유"**: 실측 출력이 답이다.

```text
$ tracetest fork
4: syscall fork -> 5
5: syscall getpid -> 5
5: syscall getpid -> 5
4: syscall wait -> 5
4: syscall getpid -> 4
fork: parent=4 child=5
```

부모(4)는 `syscall()` 안에서 `sys_fork` 가 돌아온 뒤 줄을 찍는다. 자식(5)은 `kfork` 가 복사한 trapframe(`a0 = 0`, proc.c:279–282)을 들고 **`forkret()` → `prepare_return()` → `userret`** 으로 처음 사용자 공간에 나타난다(proc.c:513–543). 자식은 `syscall()` 의 146줄 이후를 실행한 적이 없으므로 `fork -> 0` 은 찍히지 않는다. 상속 자체는 자식의 `getpid` 두 줄로 증명된다.

채점기는 이 출력을 **pid 별 순서열**로 비교한다(부모 `[fork→5, wait→5, getpid→4]`, 자식 `[getpid→5]×2`, 다른 pid 0줄). 부모의 `fork` 줄과 자식 줄의 상대 순서는 비교하지 않는다. xv6 커널은 타이머 인터럽트로 커널 안에서도 `yield` 하므로(trap.c `kerneltrap` → `yield`), `sys_fork` 반환과 `printk` 사이에 자식이 먼저 돌 수 있다. CPUS=3 이면 더 그렇다.

**exec 를 건너서**: `trace 32 wc README` 는 sh 가 fork 한 자식이 `trace()` 후 `exec("wc")` 를 한다. exec 는 페이지 테이블만 바꾸고 같은 `struct proc` 을 쓰므로 마스크가 남는다.

```text
$ trace 32 wc README
6: syscall read -> 512
6: syscall read -> 512
6: syscall read -> 512
6: syscall read -> 512
6: syscall read -> 393
6: syscall read -> 0
48 336 2441 README
$ wc README
48 336 2441 README
```

`wc` 는 512바이트 버퍼로 읽는다(user/wc.c). README 는 2441바이트 = 4×512 + 393, 그리고 EOF 의 0. exec 가 실행 파일을 읽는 것은 커널 내부의 `readi` 라 `read` 줄로 나타나지 않는다. 다음 줄의 맨 `wc` 에는 trace 줄이 없다 — 마스크는 `trace` 의 프로세스 트리에만 있다.

`tracetest basic` 의 6줄도 정확히 맞았다:

```text
$ tracetest basic
3: syscall trace -> 0
3: syscall open -> 3
3: syscall write -> 5
3: syscall close -> 0
3: syscall open -> -1
3: syscall getpid -> 3
basic: pid=3 fd=3
```

테스트 프로그램은 추적하는 동안 **콘솔에 아무것도 쓰지 않는다**. 사용자 `printf` 는 문자마다 `write` 시스템 콜이고(user/printf.c), 그 출력과 커널의 trace 줄이 같은 UART 를 쓰므로 "hi3: syscall write -> 2" 처럼 한 줄에 섞인다. 이 xv6 의 `uartwrite()` 는 바이트를 모두 THR 에 넣은 뒤에야 돌아오므로(uart.c:80–100) 순서는 인과적이지만, 줄 경계는 보장되지 않는다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">tracemask 의 수명: 설정 → fork 복사 → exec 유지 → 슬롯 반환 시 0</text>
<rect x="10" y="40" width="150" height="50" class="f-acc" stroke="currentColor"/>
<text x="20" y="60" fill="currentColor">sh (pid 2)</text>
<text x="20" y="80" fill="currentColor" font-size="12">tracemask = 0</text>
<rect x="200" y="40" width="160" height="50" class="f-mut" stroke="currentColor"/>
<text x="210" y="60" fill="currentColor">fork 자식 (pid 6)</text>
<text x="210" y="80" fill="currentColor" font-size="12">kfork: 0 복사</text>
<rect x="400" y="40" width="150" height="50" class="f-ok" stroke="currentColor"/>
<text x="410" y="60" fill="currentColor">exec("trace")</text>
<text x="410" y="80" fill="currentColor" font-size="12">trace(32) → 32</text>
<rect x="590" y="40" width="140" height="50" class="f-ok" stroke="currentColor"/>
<text x="600" y="60" fill="currentColor">exec("wc")</text>
<text x="600" y="80" fill="currentColor" font-size="12">같은 proc → 32</text>
<line x1="160" y1="65" x2="200" y2="65" stroke="currentColor"/>
<line x1="360" y1="65" x2="400" y2="65" stroke="currentColor"/>
<line x1="550" y1="65" x2="590" y2="65" stroke="currentColor"/>
<rect x="10" y="130" width="330" height="120" class="f-warn" stroke="currentColor"/>
<text x="20" y="150" fill="currentColor">부모의 fork 반환 경로</text>
<text x="20" y="172" fill="currentColor" font-size="12" font-family="monospace">ecall → usertrap → syscall()</text>
<text x="20" y="192" fill="currentColor" font-size="12" font-family="monospace">  a0 = sys_fork() = 5</text>
<text x="20" y="212" fill="currentColor" font-size="12" font-family="monospace">  mask 검사 → "4: syscall fork -&gt; 5"</text>
<text x="20" y="232" fill="currentColor" font-size="12" font-family="monospace">→ prepare_return → userret</text>
<rect x="400" y="130" width="330" height="120" class="f-mut" stroke="currentColor"/>
<text x="410" y="150" fill="currentColor">자식의 첫 실행 경로</text>
<text x="410" y="172" fill="currentColor" font-size="12" font-family="monospace">scheduler → swtch → forkret</text>
<text x="410" y="192" fill="currentColor" font-size="12" font-family="monospace">  (context.ra = forkret, proc.c:146)</text>
<text x="410" y="212" fill="currentColor" font-size="12" font-family="monospace">  syscall() 를 지나지 않음 → 줄 없음</text>
<text x="410" y="232" fill="currentColor" font-size="12" font-family="monospace">→ prepare_return → userret, a0 = 0</text>
<text x="10" y="272" fill="currentColor" font-size="12">freeproc 에서 tracemask = 0: 지금은 kfork 가 항상 덮어쓰므로 차이가 보이지 않지만,</text>
<text x="10" y="290" fill="currentColor" font-size="12">재사용되는 슬롯의 상태는 반환할 때 지우는 것이 규칙이다.</text>
</svg>
```

### Part B — sysinfo: 세기, 잠그기, 복사하기

**freemem**: kalloc.c 의 할당기는 빈 페이지 자체의 첫 8바이트를 `next` 포인터로 쓰는 침입형(intrusive) 리스트다. 세는 동안 다른 하트가 `kalloc()` 으로 맨 앞 페이지를 떼어 가면, 그 페이지는 바로 `memset(…, 5, PGSIZE)` 로 채워진다(kalloc.c:80). 잠금 없이 세던 쪽이 그 페이지의 `next` 를 읽으면 `0x0505050505050505` 를 따라간다 — 잘못된 개수가 아니라 **커널 패닉**이다. 그래서 `kmem.lock` 을 잡는다.

**nproc**: 각 `p->lock` 을 잡고 `state` 를 읽는다. 다만 64개를 차례로 잠그므로 결과는 "한 순간의 스냅숏"이 아니다(Q5). `sysinfo` 의 결과는 어차피 돌려주는 순간 낡는다 — 정확한 값이 필요한 것은 이 레슨처럼 시스템이 조용할 때뿐이다.

**정확한 페이지 회계 (실측)**:

```text
sysinfo: r=0 freemem=133246976 (32531 pages) nproc=3
sbrk 1 pages: 133246976 -> 133242880 -> 133246976
sbrk 7 pages: 133246976 -> 133218304 -> 133246976
sbrk 64 pages: 133246976 -> 132984832 -> 133246976
fork: nproc 3 -> 4 -> 3, freemem 133242880 -> 133197824 -> 133242880
zombie: nproc 3 -> 4 -> 3
badptr: -1 -1 -1 -1 0
sysinfotest: 17 passed, 0 failed
```

| 관찰 | 해석 |
|---|---|
| 32531 페이지 ≈ 127 MiB | QEMU `-m 128M`, 커널 이미지 끝(`end`)부터 `PHYSTOP` 까지가 freelist. 프로세스 64개의 커널 스택은 부팅 때 미리 할당됨(proc.c:33–44 `proc_mapstacks`) |
| sbrk k 페이지 → 정확히 k×4096 | `sbrk()` 는 이 xv6 에서 **즉시 할당**(`SBRK_EAGER`). 64페이지까지는 sz 가 2 MiB 안이라 새 페이지 테이블 페이지가 필요 없다. 줄일 때 `uvmdealloc` 은 페이지 테이블 페이지를 돌려주지 않으므로, k 를 크게 잡으면 "되돌림" 검사가 1페이지 어긋날 수 있다 — 그래서 작은 k |
| fork 자식 = 45056 B = 11 페이지 | 내 계산: 사용자 메모리 5(`_sysinfotest` 의 텍스트 2 + 데이터 1 + 가드 1 + 스택 1 = sz 20480 — readelf LOAD `0x0..0x1624 R E`, `0x2000 RW` 로 계산) + trapframe 1 + 페이지 테이블 5(루트 1, 낮은 주소용 L1·L0 2, TRAMPOLINE/TRAPFRAME 이 있는 최상위 주소용 L1·L0 2). 개별 페이지를 세어 확인한 것은 아니고, 합이 측정값과 맞는다 |
| wait 후 바이트 단위로 원상 복구 | 누수 0. 파이프 페이지(4096)는 측정 전에 만들어 두고 측정 후에 닫는다 — 순서를 바꾸면 1페이지 차이가 난다 |
| ZOMBIE 도 nproc 에 포함 | 자식이 끝나 파이프 EOF 가 왔어도 `wait` 전까지 슬롯은 `UNUSED` 가 아니다 |

**copyout 의 거절 규칙**(vm.c:345–377)과 다섯 포인터:

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">copyout(pagetable, sz, dstva, src, len): 페이지마다 이 순서로 검사</text>
<rect x="10" y="40" width="230" height="44" class="f-acc" stroke="currentColor"/>
<text x="20" y="66" fill="currentColor">va0 = PGROUNDDOWN(dstva)</text>
<rect x="10" y="100" width="230" height="44" class="f-mut" stroke="currentColor"/>
<text x="20" y="126" fill="currentColor">va0 &gt;= MAXVA ? (352)</text>
<rect x="10" y="160" width="230" height="44" class="f-mut" stroke="currentColor"/>
<text x="20" y="186" fill="currentColor">walkaddr: V 이고 U 인가? (355)</text>
<rect x="10" y="220" width="230" height="44" class="f-mut" stroke="currentColor"/>
<text x="20" y="246" fill="currentColor">PTE_W 있나? (363–365)</text>
<rect x="10" y="280" width="230" height="40" class="f-ok" stroke="currentColor"/>
<text x="20" y="305" fill="currentColor">memmove → 다음 페이지로</text>
<line x1="125" y1="84" x2="125" y2="100" stroke="currentColor"/>
<line x1="125" y1="144" x2="125" y2="160" stroke="currentColor"/>
<line x1="125" y1="204" x2="125" y2="220" stroke="currentColor"/>
<line x1="125" y1="264" x2="125" y2="280" stroke="currentColor"/>
<line x1="240" y1="122" x2="290" y2="122" stroke="currentColor"/>
<line x1="240" y1="182" x2="290" y2="182" stroke="currentColor"/>
<line x1="240" y1="242" x2="290" y2="242" stroke="currentColor"/>
<rect x="290" y="100" width="440" height="44" class="f-warn" stroke="currentColor"/>
<text x="300" y="118" fill="currentColor" font-size="12">예 → -1   ① 0xffffffffffffff00</text>
<text x="300" y="136" fill="currentColor" font-size="12">walk() 가 MAXVA 이상을 다루지 못하므로 먼저 막는다</text>
<rect x="290" y="160" width="440" height="44" class="f-warn" stroke="currentColor"/>
<text x="300" y="178" fill="currentColor" font-size="12">아니오 → vmfault: va &lt; sz 이고 미매핑이면 지금 할당, 아니면 -1</text>
<text x="300" y="196" fill="currentColor" font-size="12">② sz+8페이지 → -1  ④ sz-8 (sz 넘김) → 2번째 페이지 -1</text>
<rect x="290" y="220" width="440" height="44" class="f-warn" stroke="currentColor"/>
<text x="300" y="238" fill="currentColor" font-size="12">아니오 → -1  ③ NULL: 0 번지 = 텍스트 페이지(R-X)</text>
<text x="300" y="256" fill="currentColor" font-size="12">⑤ sz - 16: 마지막 16바이트에 딱 맞음 → 0</text>
<text x="260" y="300" fill="currentColor" font-size="12">④ 는 첫 8바이트를 쓴 뒤 -1: copyout 은 원자적이지 않다 (Q3).</text>
</svg>
```

③은 버전에 따라 결과가 다르다는 점이 면접 포인트다. 이 커밋은 "읽기 전용 텍스트 페이지 위로 copyout 금지" 검사(vm.c:363–365)가 있어 -1 이지만, 그 검사가 없는 xv6 라면 `sysinfo(0)` 이 **자기 코드 페이지를 덮어쓰고 0 을 돌려준다**(PTE_U 만 보고 PTE_W 는 안 보므로). 커널이 사용자 대신 쓸 때는 사용자 자신이 쓸 수 있는 곳에만 써야 한다 — 하드웨어가 그 검사를 대신해 주지 않는다(커널은 S 모드이고, 이 접근은 소프트웨어 페이지 워크다).

`sbrklazy()` 로 늘린 페이지에 `sysinfo` 를 부르면 어떻게 될까? copyout 의 357줄이 부르는 `vmfault()` 가 그 자리에서 0으로 채운 페이지를 할당한다(vm.c:459–478). 즉 `copyout` 은 지연 할당 페이지를 처음 만지는 "사용자 대신의 첫 접근"이기도 하다.

### Part C — 채점기 설계

`run_xv6.py` 의 핵심 결정:

| 결정 | 이유 |
|---|---|
| `start_new_session=True` + `os.killpg(SIGKILL)` | `make qemu` 는 make → qemu 두 단계라, make 만 죽이면 QEMU 가 남을 수 있다. 프로세스 그룹째로 죽인다 |
| stdout non-blocking + `select` 0.2 s | 원본 `test-xv6.py` 처럼 1초 sleep 폴링을 하면 명령마다 1초씩 버린다. 그 외 부팅·명령 패턴은 원본 test-xv6.py 와 같다 |
| 명령 완료 = 에코된 명령 줄 뒤 + 출력이 `$ ` 로 끝남 | xv6 sh 는 프롬프트에 줄바꿈이 없다. 콘솔은 타이핑을 에코한다(consoleintr) |
| `panic: ` 이 보이면 즉시 실패, 이후 테스트 중단 | 망가진 커널에서 30 s 제한을 테스트마다 기다리지 않는다 |
| guest PASS/FAIL 줄 + host 검사를 한 집계로 | 사용자 프로그램은 반환값만 볼 수 있고, trace 줄은 콘솔에만 있다. 그래서 둘 다 필요하다 |
| 기대값을 하드코딩하지 않음 | pid·fd 는 프로그램이 찍은 값으로, `read` 반환값은 빌드 트리의 README 크기로 계산한다 |

**gdb 로 길 위에 멈추기** (`demo_gdb.sh` — 두 창짜리 `make qemu-gdb` + `gdb-multiarch` 레시피를 스크립트로 묶은 것. GDB 포트는 uid 에서 나오는 기본값 대신 임의 포트를 쓴다. VM 은 다른 세션도 QEMU 를 쓰기 때문이다):

<!-- code: demo_gdb.sh -->

```bash
limactl shell elinux -- bash -lc "bash $L/demo_gdb.sh"     # check.sh solution 다음에
```

실측 출력(줄 번호는 오버레이된 solution 파일 기준):

```text
0x0000000000001000 in ?? ()
Breakpoint 1 at 0x8000298e: file kernel/syscall.c, line 174.

Breakpoint 1, syscall () at kernel/syscall.c:174
174	{
#0  syscall () at kernel/syscall.c:174
#1  0x000000008000274e in usertrap () at kernel/trap.c:68
#2  0x0000003ffffff09c in ?? ()
$1 = "init", '\000' <repeats 11 times>
$2 = 15
$3 = 0x3b8
$4 = 0x8
Breakpoint 2 at 0x80002c1c: file kernel/sysproc.c, line 120.

Breakpoint 2, sys_trace () at kernel/sysproc.c:120
120	{
#0  sys_trace () at kernel/sysproc.c:120
#1  0x00000000800029ca in syscall () at kernel/syscall.c:182
#2  0x000000008000274e in usertrap () at kernel/trap.c:68
#3  0x0000003ffffff09c in ?? ()
$5 = 3
$6 = 0xa18800
0x00000000800029ca in syscall () at kernel/syscall.c:182
182	    p->trapframe->a0 = syscalls[num]();
Value returned is $7 = 0
$8 = 0xa18800
```

읽는 법: 첫 시스템 콜은 `init` 의 `a7 = 15`(`SYS_open`, init 이 `open("console")`), `scause = 0x8`(U 모드 ecall), `epc = 0x3b8`(init.asm 에서 `ecall` 은 `0x3b4` — usertrap 62줄이 이미 +4 한 값). 백트레이스 #2 `0x3ffffff09c` 는 TRAMPOLINE 페이지(`MAXVA - PGSIZE` = `0x3ffffff000`) 안의 `uservec` 이다. gdb 가 이름을 모르는 이유는 trampoline 코드가 커널 주소(`trampsec`)로 링크되어 있고 여기서는 다른 가상 주소로 실행되기 때문이다. 두 번째 정지는 `tracetest basic` 의 `trace()`: `a0 = 0xa18800` = `1<<23 | 1<<21 | 1<<16 | 1<<15 | 1<<11`(trace, close, write, open, getpid). `finish` 하면 반환값 0 이 `syscall()` 182줄의 대입으로 돌아오고, 그 시점에 `tracemask` 가 이미 설정되어 있다.

### 펌웨어 · 리눅스 연결

| 시스템 | 트랩 명령 | 번호 | 인자/반환 | 디스패치 |
|---|---|---|---|---|
| xv6-riscv | `ecall` | a7 | a0..a5 / a0 (trapframe) | `syscalls[]` + 범위 검사 |
| Linux riscv | `ecall` | a7 | a0..a5 / a0, `orig_a0` 보관 | `do_trap_ecall_u` → `syscall_handler`, `array_index_nospec` (traps.c:327–344) |
| Linux arm64 | `svc #0` | x8 | x0..x5 / x0 | `invoke_syscall` → `syscall_table[array_index_nospec(scno, sc_nr)]` (arch/arm64/kernel/syscall.c:38–49) |
| Cortex-M RTOS | `svc #imm` | 즉치값(스택된 PC 앞 명령에서 읽음) 또는 r0 | 하드웨어가 스택에 쌓은 r0–r3 / r0 | SVC_Handler 의 switch (RTOS-A01, ARM-M09) |

리눅스는 xv6 와 두 가지가 다르다. (1) `regs->orig_a0 = regs->a0`(traps.c:333): 반환값이 a0 를 덮어쓰기 전에 첫 인자를 보관한다 — ptrace 재시작과 seccomp, 그리고 추가 연습 1 의 이유와 같다. (2) 진입 전에 `syscall_trace_enter()` 가 ptrace(strace)를 먼저, 그다음 seccomp 를 부른다(kernel/entry/syscall-common.c:20–47, "Do seccomp after ptrace"). 반환 쪽에는 `trace_sys_exit` 트레이스포인트가 있다(같은 파일 110줄). 우리 `trace` 는 이 중 sys_exit 트레이스포인트의 아주 작은 형제다. 리눅스는 표 인덱스를 `array_index_nospec` 으로 막아 **Spectre v1** 투기적 범위 초과를 막지만, xv6 의 `num < NELEM(syscalls)` 는 그렇지 않다.

## 6. 정답 코드

바뀐 파일은 4개다(나머지 배관은 starter 그대로). 강조된 줄이 TODO 를 채운 곳이다.

<!-- code: solution/kernel/syscall.c diff=starter/kernel/syscall.c -->

<!-- code: solution/kernel/sysproc.c diff=starter/kernel/sysproc.c -->

<!-- code: solution/kernel/proc.c diff=starter/kernel/proc.c -->

<!-- code: solution/kernel/kalloc.c diff=starter/kernel/kalloc.c -->

실행(2회, 같은 결과 — 아래는 2회째):

```text
built solution in /tmp/OS-A09_work/solution (riscv64-linux-gnu-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0)
PASS: [guest tracetest basic] basic: trace() returns 0
PASS: [host] trace lines of one process match exactly (6 lines)
PASS: [host] parent: fork -> child, wait -> child, getpid -> parent
PASS: [host] child inherited the mask: 2 x getpid (and no 'fork -> 0' line)
PASS: [host] trace 32 wc README: read -> [512, 512, 512, 512, 393, 0]
PASS: [host] mask stays in trace's process tree: plain wc prints no trace lines
PASS: [guest sysinfotest] sbrk 64 pages: freemem -64 pages, then back
PASS: [guest sysinfotest] fork: nproc and freemem back exactly after wait
PASS: [guest sysinfotest] badptr: struct ending exactly at p->sz -> 0
PASS: [host] sysinfotest finished with 0 failed
usertests -q: 70 s
PASS: [host] usertests -q: ALL TESTS PASSED
30/30 passed (solution, 72 s)
```

(가운데 PASS 줄 일부 생략.) 1회째는 `usertests -q: 75 s`, 전체 77 s. 망가진 커널이 매달리지 않는다는 것은 7. 실수 4 의 실측이 보여 준다(패닉 → 즉시 FAIL, `11/12 passed`).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `usys.pl` 이나 `user.h` 를 빠뜨림 | 스텁이 없으면 링크 에러 `undefined reference to 'trace'`, 프로토타입이 없으면 `-Werror` 아래 암시적 선언 에러. 디스패치 표만 빠지면 빌드는 되고 실행 시 `unknown sys call 23` + -1 (syscall.c:148) | 힌트 1 의 7곳을 체크리스트로 |
| 이름 표를 순서 배열 `{"fork","exit",...}` 로 | 번호는 1부터인데 배열은 0부터 → 모든 이름이 하나씩 밀림. 중간에 번호가 비면 더 어긋남 | `[SYS_fork] = "fork"` 지정 초기화자 |
| `printk` 를 `syscalls[num]()` 호출 **전**에 | 반환값을 모른다. 마스크를 호출 전에 읽으면 `trace()` 자신은 찍히지 않는다 (설계 선택이지만 MIT 랩 출력 예 `4: syscall trace -> 0` 과 다름) | 146줄 다음에 `p->trapframe->a0` 를 찍음 |
| `sys_sysinfo` 에서 `*(struct sysinfo *)uaddr = si` | 커널 페이지 테이블에는 사용자 주소가 없다. 실측: `scause=0xf sepc=0x80002c6a stval=0x4f40` → `panic: kerneltrap` (QEMU 를 바로 죽여 출력이 `panic: ker` 에서 잘림) | `copyout(p->pagetable, p->sz, uaddr, (char *)&si, sizeof(si))` |
| freelist 를 잠금 없이 셈 | 다른 하트가 떼어 간 페이지는 `memset 5` 로 채워짐 → `next = 0x0505…` 를 따라가 패닉 (CPUS=1 테스트로는 안 잡힘 — 리뷰로 잡아야 하는 종류) | `acquire(&kmem.lock)` … `release` |
| kfork 에서 상속을 빼먹음 | `trace 32 wc README` 는 여전히 동작한다(exec 는 같은 proc). fork 테스트만 잡는다 — 그래서 fork 테스트가 따로 필요하다 | `np->tracemask = p->tracemask;` |
| `printk("%d", p->trapframe->a0)` | uint64 를 `%d` 로 → 실측 `error: format '%d' expects argument of type 'int', but argument 2 has type 'uint64' [-Werror=format=]` (printk 에 format 속성이 있음, defs.h). `%ld` 면 -1 이 그대로지만 MIT 형식과 맞추려면 `(int)` | `(int)p->trapframe->a0` |
| `sbrklazy()` 로 페이지 회계 테스트 | 지연 할당은 freemem 을 바로 줄이지 않는다 → "k 페이지 감소" 실패. 이 xv6 의 `sbrk()` 는 즉시 할당 (ulib.c:153–156) | `sbrk(k * PGSIZE)` (또는 각 페이지를 만진 뒤 측정) |

## 8. 면접 Q&A

<details><summary>Q1. 시스템 콜 인자는 어떻게 커널에 오고, xv6 의 argint 는 왜 실패를 돌려주지 않나?</summary>

호출 규약대로 a0..a5 레지스터에 실린 채 `ecall` 하면, `uservec` 이 모든 레지스터를 그 프로세스의 trapframe 페이지에 저장한다(trampoline.S:39–72). `argint(n)` 은 `trapframe->a<n>` 을 읽을 뿐이다(syscall.c:35–61). 정수는 어떤 값이든 "올바른" 정수이므로 검사할 것이 없다. 포인터는 숫자로 받아 두고, **실제로 쓰거나 읽는 순간** `copyin`/`copyout` 이 사용자 페이지 테이블로 검증한다. 검증을 한곳에 모아 두는 설계다. 리눅스도 같다: `copy_to_user` 가 실패하면 `-EFAULT`.
꼬리: "인자가 6개를 넘으면?" → 레지스터 6개가 한계이므로 구조체 포인터 하나로 넘긴다(sysinfo 가 결과를 그렇게 돌려준다. 리눅스 `clone3`, `openat2` 는 인자 자체를 struct + 크기로 받는다).

</details>

<details><summary>Q2. trace 마스크를 상속했는데 자식에게서는 왜 "fork -> 0" 줄이 안 나오나?</summary>

자식은 시스템 콜에서 "돌아오는" 것이 아니라 **처음 스케줄되는 것**이다. `allocproc` 이 `context.ra = forkret` 으로 만들어 두므로, 자식은 `swtch` → `forkret` → `prepare_return` → `userret` 으로 바로 사용자 공간에 간다. `a0 = 0` 은 kfork 가 복사한 trapframe 에 미리 넣어 둔 값이다(proc.c:282). 출력 코드가 있는 `syscall()` 의 뒷부분은 부모만 지난다. 자식에게도 찍고 싶다면 `forkret` 에서 마스크를 보고 찍어야 한다. 이 질문의 진짜 의도는 "fork 가 두 번 반환한다"는 말이 커널 안에서는 **반환 경로 두 개**라는 것을 아느냐다.

</details>

<details><summary>Q3. copyout 은 원자적인가? 중간에 실패하면 사용자 메모리는 어떻게 되나?</summary>

원자적이지 않다. 페이지 단위로 검사하고 복사하므로, `sz - 8` 처럼 16바이트 구조체가 sz 를 넘으면 **첫 8바이트는 이미 써진 뒤** 두 번째 페이지에서 -1 이 난다(vm.c:350–375). 리눅스 `copy_to_user` 도 "복사하지 못한 바이트 수"를 돌려주고, 시스템 콜은 보통 `-EFAULT` 로 끝난다. 결과 버퍼가 부분적으로 바뀌어 있을 수 있다. 사용자 코드는 실패한 호출의 출력 버퍼 내용을 믿으면 안 된다. 원자성이 필요하면 커널이 먼저 전체 범위를 검증하거나, 결과를 한 페이지 안에 두는 ABI 로 만든다.

</details>

<details><summary>Q4. freemem 을 매번 freelist 순회로 세는 설계, 프로덕션이라면 어떻게 바꾸겠나?</summary>

지금은 O(빈 페이지 수) = 실측 32531 개를 `kmem.lock` 을 쥐고 돈다. 그동안 **모든 하트의 kalloc/kfree 가 멈춘다** — `sysinfo` 를 자주 부르는 모니터링 데몬 하나가 전체 할당 지연을 만든다. 바꾸는 법: (1) `kmem` 에 카운터를 두고 kalloc/kfree 에서 잠금 안에서 ±1 → O(1) 읽기. (2) 더 확장하려면 per-CPU 빈 리스트(6.1810 의 kalloc 랩 주제)와 per-CPU 카운터를 두고, 읽을 때 합산한다. 이 값은 근사치다. 리눅스의 `NR_FREE_PAGES` 같은 vmstat 카운터가 per-CPU 차분을 주기적으로 접어 넣는 방식이다(구현 세부는 확인하지 않음). 트레이드오프는 "정확하지만 비싼 순회" 대 "싸지만 약간 낡은 카운터"다. `sysinfo` 결과는 반환 순간 이미 낡으므로 대부분은 카운터가 맞다. 이 레슨 테스트처럼 **정확한 회계**를 검증하려면 순회 쪽이 독립적인 기준이 된다. 카운터 구현을 순회로 검사하는 디버그 모드를 두는 것도 좋다.

</details>

<details><summary>Q5. nproc 를 셀 때 p->lock 을 하나씩 잡는다. 그래도 틀릴 수 있는 경우는? (이 설계가 틀리는 경우)</summary>

각 슬롯의 `state` 는 정확히 읽지만, 64개를 차례로 잠그므로 **전체가 한 순간의 값은 아니다**. 내가 proc[10] 을 지난 뒤 proc[5] 의 프로세스가 fork 해서 새 자식이 proc[3] 에 생기면, 그 자식은 세지 않는다. 반대로 이미 센 프로세스가 끝나서 해제되어도 숫자에 남는다. 즉 결과는 "그 구간 동안 존재했던 프로세스 수"의 근사다. 정확한 스냅숏이 필요하면 fork/exit 가 모두 잡는 전역 잠금이나 전역 카운터(`nr_processes` 류)가 필요하다. 잠금을 아예 빼면 C 메모리 모델상 데이터 레이스(UB)다. RISC-V 에서 정렬된 4바이트 읽기가 찢어지지는 않지만, 그것은 하드웨어 사실이지 C 의 보장이 아니다. 테스트가 `nproc == 3` 처럼 정확한 값을 기대할 수 있는 것은 CPUS=1 이고 시스템이 조용하기 때문이다 — 그 가정을 테스트 주석에 적어 두었다.

</details>

<details><summary>Q6. 마스크가 int 인데 시스템 콜이 32개를 넘으면? 어떻게 바꾸겠나?</summary>

`1 << 32` 는 int 에서 UB 이고, `(mask >> num) & 1` 도 num ≥ 32 면 UB 다(C11 6.5.7). 지금 번호가 24(연습 포함 27)까지라 괜찮을 뿐이다. 고치는 법: ABI 를 `uint64` 마스크로 바꾸면 63까지. 더 일반적으로는 비트맵을 사용자 포인터로 넘기고 커널이 `copyin` 한다(seccomp 가 BPF 프로그램을 넘기는 것과 같은 방향). ABI 를 바꾸면 기존 바이너리가 깨지므로 리눅스라면 새 번호(`trace2`)나 flags 인자를 추가한다 — 기존 번호의 의미는 절대 바꾸지 않는다. 같은 이유로 **시스템 콜 번호 자체가 ABI** 다. 중간에 번호를 끼워 넣으면 다시 빌드하지 않은 모든 사용자 바이너리가 다른 함수를 부른다.

</details>

<details><summary>Q7. 리눅스에서 같은 일(호출 추적)을 하려면? strace 와 이 trace 의 차이는?</summary>

strace 는 **커널을 고치지 않고** ptrace(`PTRACE_SYSCALL` 또는 `PTRACE_O_TRACESYSGOOD`)로 진입·반환마다 tracee 를 멈추고 레지스터를 읽는다(OS-A02 의 미니 디버거와 같은 메커니즘). 진입 때 인자, 반환 때 결과를 보므로 추가 연습 1 처럼 인자를 미리 잡는 일을 tracer 가 한다. 대가는 시스템 콜마다 컨텍스트 스위치 두 번이라 매우 느리다는 점이다. 커널 안쪽에는 `raw_syscalls:sys_enter/sys_exit` 트레이스포인트가 있어 `perf trace`, bpftrace 가 멈춤 없이 관찰한다(syscall-common.c 의 `trace_sys_enter`/`trace_sys_exit`). seccomp 는 관찰이 아니라 **필터**다. 같은 진입 지점에서 ptrace 다음에 실행되어 허용/거부/kill 을 결정한다(syscall-common.c:45–47). 우리 xv6 trace 는 "커널 안 printk" 라 가장 싸지만 콘솔이 병목이다. 출력 형식과 대상이 커널에 고정되어 있다는 한계도 있다.

</details>

<details><summary>Q8. syscall() 의 범위 검사 `num < NELEM(syscalls)` 면 안전한가?</summary>

구조적으로는 안전하다. 음수·초과·빈 칸(`syscalls[num] == 0`)을 모두 막고 -1 을 돌려준다. 하지만 투기 실행이 있는 CPU 에서는 분기 예측이 "범위 안"으로 틀리게 예측한 동안 `syscalls[num]` 을 범위 밖에서 읽고 그 값으로 간접 호출을 투기적으로 시작할 수 있다(Spectre v1, bounds check bypass). 리눅스는 `array_index_nospec(scno, sc_nr)` 로 인덱스를 데이터 의존적으로 0 으로 깎는다(arm64 syscall.c:48, riscv traps.c:342–343). QEMU TCG 의 xv6 에는 의미가 없지만, 실리콘 위의 커널이라면 사용자 입력이 표 인덱스가 되는 모든 곳에 같은 처리가 필요하다. 펌웨어로 바꿔 말하면: Cortex-M 의 SVC 번호 switch 는 대부분 투기 실행이 없는 코어에서 돌지만, Cortex-A/R 급 보안 펌웨어(TF-A SMC 디스패처 등)는 같은 문제를 다룬다(세부는 확인하지 않음).

</details>

<details><summary>Q9. RTOS(Cortex-M)의 "시스템 콜"은 xv6 와 무엇이 같고 다른가?</summary>

같다: 비특권 코드가 트랩 명령(`svc #imm`)으로 특권 핸들러에 들어가고, 인자는 레지스터, 번호로 표를 찾고, 반환값은 r0 다. 다르다: (1) Cortex-M 은 하드웨어가 r0–r3, r12, lr, pc, xPSR 을 **스택에 자동으로 쌓는다**(ARM-M09). 핸들러는 trapframe 대신 그 스택 프레임에서 인자를 읽고, 반환값도 스택된 r0 칸에 써야 한다 — xv6 의 `trapframe->a0` 와 똑같은 패턴이다. 번호는 스택된 PC 바로 앞 SVC 명령의 즉치값으로 읽기도 한다. (2) MMU 가 없어 `copyout` 같은 페이지 테이블 검증이 없다. MPU 영역 검사(FreeRTOS-MPU 의 권한 검사 류)로 대신하거나 아예 하지 않는다. 그래서 MPU 없는 RTOS 에서 "사용자" 태스크가 넘긴 포인터는 무엇이든 쓸 수 있다. (3) RTOS-A01 처럼 SVC 를 "첫 태스크 시작"에만 쓰고 일반 API 는 함수 호출로 두는 설계가 흔하다. 권한 분리가 없으면 트랩 비용을 낼 이유가 없기 때문이다.

</details>

## 9. 추가 연습

practice 는 solution 위에 `practice/` 를 얹는다. 4개 연습이 모두 한 커널에 들어 있고, `practicetest args|files|clock|procs` 와 `ps` 를 `run_xv6.py practice` 가 채점한다. solution 테스트와 `usertests -q` 도 다시 돈다.

### 연습 1 — trace 가 인자도 찍게 (`traceargs(mask)`)

`traceargs(mask)` 로 고른 시스템 콜은 `11: syscall open("pt.args", 514) -> 3` 처럼 인자까지 찍는다. 기존 `trace` 출력 형식은 바뀌면 안 된다(solution 테스트가 그대로 돌아야 함). 인자는 언제 잡아야 할까?

<details><summary>답</summary>

**호출 전에** 잡는다. 이유는 둘이다. `a0` 는 반환값으로 덮어써진다. 그리고 `exec` 가 성공하면 path 포인터가 가리키던 사용자 메모리 자체가 사라진다. `capture_args()` 가 `trapframe->a0..a2` 를 복사하고, 경로 인자는 `fetchstr` 로 커널 버퍼(`MAXPATH`)에 미리 복사한다. 출력은 시스템 콜별 `printk` 한 번(줄이 섞이지 않게). 마스크는 `trace` 와 같이 fork 상속, `freeproc` 에서 0. 실측:

```text
11: syscall open("pt.args", 514) -> 3
11: syscall read(3, 10) -> 0
11: syscall close -> 0
11: syscall unlink("pt.args") -> 0
```

`close` 는 일부러 `traceargs` 마스크에서 뺐다 → 기존 형식. 리눅스가 `orig_a0` 를 따로 저장하는 이유(traps.c:333)와 같은 문제다. 비용: 인자를 찍는 시스템 콜마다 `copyinstr` 한 번(최대 128바이트)이 추가된다.

</details>

### 연습 2 — `sysinfo.nopenfiles`

`struct sysinfo` 에 시스템 전체 열린 파일 수를 더하라. "열린 파일"은 디스크립터 수인가, 열린 파일 표 항목 수인가? `dup` 과 `pipe` 에서 숫자가 어떻게 변해야 하나?

<details><summary>답</summary>

`ftable`(file.c, `NFILE = 100`) 중 `ref > 0` 인 항목 수로 정의했다(`nfile_open()`, `ftable.lock` 아래). 디스크립터가 아니라 **열린 파일 항목**이므로 `dup` 과 fork 는 항목을 공유해 +0, `open` 은 +1, `pipe` 는 읽기·쓰기 항목 2개로 +2. 실측 `files: 1 -> 4 -> 4 -> 6 -> 1`: 기본 1은 init 이 연 콘솔 하나를 init·sh·자신이 fd 0,1,2 로 공유하는 항목이다. 구조체에 필드를 추가하면 크기가 16→24바이트로 바뀐다. 이 레슨은 커널과 사용자 프로그램을 같이 다시 빌드하니 괜찮지만, 실제 ABI 였다면 옛 바이너리의 16바이트 버퍼를 커널이 24바이트로 넘쳐 쓴다. 그래서 리눅스식 확장 구조체는 사용자가 **크기를 함께 넘긴다**(Q6).

</details>

### 연습 3 — `uptime_ms()`

tick(100 ms) 보다 세밀한 시계를 시스템 콜로 제공하라. 근거가 되는 하드웨어 레지스터는? 테스트는 성능 수치 없이 무엇을 검사할 수 있나?

<details><summary>답</summary>

RISC-V `time` CSR(`r_time()`, riscv.h:301)을 10000 으로 나눈다. QEMU virt 의 타이머가 10 MHz 라는 근거는 xv6 의 "1000000 is about a tenth of a second" 주석(trap.c:177–179)이다. 장치 트리를 직접 읽어 확인하지는 않았다. 검사는 설계에서 나오는 **하한**만 둔다: 1000번 연속 호출이 단조 증가, `pause(3)` 은 tick 이 3 이상 늘어남, 그 사이 경과는 ≥ 200 ms(시작 시점이 tick 중간일 수 있으니 3×100 − 100), tick ≤ ms/100 + 5. 실측 `clock: ms 1511 -> 1726 (215 ms), ticks 10 -> 13`. ms/100 = 15 인데 tick 이 10 인 것은 타이머 인터럽트가 부팅 도중에 켜지기 때문이다. 리눅스의 `clock_gettime` 은 이런 값을 **시스템 콜 없이** vDSO 로 읽는다(OS-B03). RISC-V 도 `scounteren.TM` 으로 U 모드에 `time` CSR 읽기를 열어 줄 수 있다고 알고 있다(명세 기준, 이 xv6 에서 시도하지는 않음). 그러면 시스템 콜이 필요 없다 — 연습의 연습.

</details>

### 연습 4 — `getprocs()` 와 `ps`

`int getprocs(struct procinfo *buf, int max)` 를 만들어 pid·state·sz·name 을 돌려주고, 사용자 명령 `ps` 를 작성하라. `copyout` 을 `p->lock` 을 쥔 채 해도 되나?

<details><summary>답</summary>

슬롯마다 `p->lock` 아래에서 지역 `struct procinfo` 로 **스냅숏** → 잠금 해제 → `copyout`. 잠금을 쥔 채 `copyout` 하면, `copyout` 이 지연 할당 페이지에서 `vmfault` → `kalloc` 으로 다른 잠금을 잡는다. 그러면 잠금 순서를 하나 더 만드는 셈이고, 잠금 구간이 사용자 메모리 크기에 비례해 길어진다. 실측 `ps`:

```text
PID STATE NAME SZ
1 sleep init 16384
2 sleep sh 20480
16 run ps 16384
```

테스트: 개수 == `sysinfo.nproc`, init 은 SLEEPING, 호출자는 RUNNING, 파이프에서 막힌 자식은 SLEEPING 이고 이름이 부모와 같음(fork 가 name 복사), `max = 1` 이면 1, NULL 버퍼(텍스트 페이지) → -1. 이 커널의 `procdump()`(Ctrl-P)가 잠금 없이 같은 정보를 찍는 이유는 "멈춘 기계를 더 꼬이게 하지 않으려고"다(proc.c 주석). 디버그 경로와 API 경로의 잠금 정책이 다르다.

</details>

### 한 번에 검증

```bash
limactl shell elinux -- bash -lc "bash $L/check.sh practice"
# ... 50/50 passed (practice, 60 s)   (usertests -q: 58 s)
```

<!-- code: practice/kernel/syscall.c diff=solution/kernel/syscall.c -->

<!-- code: practice/kernel/sysproc.c diff=solution/kernel/sysproc.c -->

<!-- code: practice/kernel/proc.c diff=solution/kernel/proc.c -->

<!-- code: practice/kernel/file.c -->

<!-- code: practice/kernel/sysinfo.h diff=starter/kernel/sysinfo.h -->

<!-- code: practice/kernel/procinfo.h -->

<!-- code: practice/user/practicetest.c -->

<!-- code: practice/user/ps.c -->

## 10. 핵심 정리

- 시스템 콜 = `li a7, N; ecall` → `uservec`(레지스터를 trapframe 에) → `usertrap`(`scause == 8`, `epc += 4`) → `syscall()`(`syscalls[a7]()`) → 반환값은 `trapframe->a0` → `userret`/`sret`.
- 새 시스템 콜 = 번호 · 디스패치 표 · `usys.pl` · `user.h` · `sys_*` 구현 · (proc 상태) · Makefile. 하나만 빠져도 링크 에러, `-Werror`, 또는 `unknown sys call`.
- trace 출력은 `syscall()` 에서 호출 **후**. fork 자식은 `forkret` 으로 시작하므로 `fork -> 0` 이 없다. exec 는 같은 proc 이라 마스크 유지.
- 인자 읽기(`argint`/`argaddr`)는 검증하지 않는다. 사용자 포인터는 `copyout`/`copyin` 만 만진다: MAXVA → V+U → (지연 할당) → PTE_W. 커널이 직접 역참조하면 `panic: kerneltrap`(실측).
- copyout 은 원자적이지 않다(부분 쓰기 후 -1). 공유 자료구조(freelist, proc 상태)는 잠그고 읽되, 결과는 반환 순간 낡은 근사값이다.
- 정확한 회계 테스트(sbrk k 페이지 = 정확히 k×4096, wait 후 바이트 단위 복구)는 CPUS=1 의 조용한 시스템이라 가능하다. 그 가정을 테스트에 적어 둔다.
- 리눅스: `orig_a0` 보관, ptrace → seccomp → 트레이스포인트 순서, `array_index_nospec`. RTOS: SVC + 스택 프레임의 r0, MMU 없음 → 포인터 검증은 MPU 몫.

## 참고자료

- xv6-riscv 소스, 커밋 `fa2ddcb7ca87a77af579380a6eafd5aff163b44f` (github.com/mit-pdos/xv6-riscv, 2026-11-05) — kernel/trampoline.S, trap.c, syscall.c, sysproc.c, proc.c, kalloc.c, vm.c, uart.c, user/usys.pl, test-xv6.py
- MIT 6.1810 Lab: system calls (2022) — "System call tracing", "Sysinfo": https://pdos.csail.mit.edu/6.1810/2022/labs/syscall.html (출력 형식 `<pid>: syscall <name> -> <ret>`, freemem 바이트, nproc = not UNUSED 를 확인)
- Russ Cox, Frans Kaashoek, Robert Morris, *xv6: a simple, Unix-like teaching operating system* (RISC-V 판) 4장 "Traps and system calls"
- RISC-V Privileged Architecture 명세 — `scause`(8 = Environment call from U-mode, 13/15 = load/store page fault), `sepc`, `stvec`, `satp`, `sret`
- Linux 6.18.55: arch/riscv/kernel/traps.c:327–344 (`do_trap_ecall_u`), arch/arm64/kernel/syscall.c:38–49 (`invoke_syscall`), kernel/entry/syscall-common.c:20–47, 110 (ptrace → seccomp → tracepoint)
- man7.org: syscall(2) (아키텍처별 호출 규약 표), ptrace(2), seccomp(2)
- OSTEP 6장 "Limited Direct Execution" (트랩, 트랩 테이블, 시스템 콜 번호)
- 관련 레슨: OS-B03 (시스템 콜 비용), OS-M03 (COW fork 관찰), OS-A02 (ptrace 디버거), OS-A10 (xv6 COW fork — 같은 오버레이 방식), RTOS-A01 (SVC), ARM-M09 (Cortex-M 예외 프레임), ARM-M13 (AArch64 svc/ESR), ELX-M04 (check.sh 커널 실습 패턴). 다음 단계: VIRT-M13 (seccomp-BPF), LPERF-B07 (strace vs perf trace), SEC-M08 (MPU + SVC 게이트)
