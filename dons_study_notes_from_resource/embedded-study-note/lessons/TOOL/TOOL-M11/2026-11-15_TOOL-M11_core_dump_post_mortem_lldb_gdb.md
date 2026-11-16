---
id: TOOL-M11
time: 80분
updated: 2026-11-15
---
# TOOL-M11 코어 덤프 생성과 사후 분석

## 학습 목표

- macOS 에서 코어 덤프(core dump)가 생기는 조건 — `ulimit -c`, `kern.coredump`/`kern.corefile`, `/cores` 쓰기 권한, 코드 서명과 `com.apple.security.get-task-allow` — 을 실측표로 설명하고, 같은 크래시를 Linux(`core_pattern`, apport, `coredump_filter`)와 비교한다.
- 코어 파일 안에 **무엇이 있고 무엇이 없는지** 말한다: Mach-O `MH_CORE` 의 `LC_SEGMENT_64`(메모리) · `LC_THREAD`(스레드별 레지스터 + FAR/ESR), ELF `ET_CORE` 의 `PT_LOAD` · `NT_PRSTATUS`/`NT_SIGINFO`. 그리고 "프로세스가 없으니 코드는 한 줄도 못 돌린다".
- `lldb -c core ./bin` 으로 **크래시 스레드 → 지역 변수 → 힙 객체 바이트 → 그 바이트를 쓴 스레드** 순서로 원인을 복원하는 사후 분석(post-mortem) 스크립트를 쓰고, `lldb -b` 배치 모드의 함정(첫 에러에서 멈춤)을 피한다.
- 코어 크기(이 맥 ~6.7 GiB vs Linux 26 MB vs lldb `save-core` 86 KB/1.1 MB)가 왜 그렇게 다른지 설명하고, 펌웨어의 "코어 덤프"(RAM + 레지스터 스냅숏, BM-A09 / ARM-M14 / TOOL-A05)와 연결한다.

## 1. 문제

"현장에서 한 번 죽었는데 재현이 안 됩니다. 코어 파일 하나 받았어요" — 시스템/펌웨어 면접의 디버깅 라운드 단골이고, 실제 일(SSD 컨트롤러의 assert 덤프, 임베디드 Linux 게이트웨이의 데몬 크래시, Cortex-M 의 HardFault 덤프)에서도 **사후 분석이 첫 번째이자 유일한 기회**인 경우가 많다. 디버거를 붙여 다시 돌릴 수 없으니, 크래시 순간에 얼어붙은 메모리와 레지스터만으로 "누가 무엇을 망가뜨렸나" 를 말해야 한다.

재료 `crash.c` 는 스레드 3 개가 힙 객체 하나(`struct session`)를 나눠 쓰는 작은 프로그램이다. `rx_parser` 가 패킷의 이름 24 바이트를 `name[16]` 에 복사해 바로 뒤의 포인터 `tx` 를 덮고, 한참 뒤 `tx_worker` 가 그 포인터를 따라가다 죽는다. **크래시 지점(crash site) ≠ 버그 지점(bug site)**, 그리고 버그를 낸 스레드는 이미 다른 일을 하고 있다 — 코어에서만 둘을 이을 수 있다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">크래시 → 코어 파일 → 디버거 (이 맥 / Lima VM 실측)</text>
  <rect x="10" y="40" width="130" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor">crash (pid N)</text>
  <text x="20" y="82" fill="currentColor" font-family="monospace" font-size="12">SIGSEGV in tx_flush</text>
  <path d="M140 70 L175 70" stroke="currentColor" stroke-width="1.5"/>
  <rect x="175" y="34" width="230" height="74" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="185" y="54" fill="currentColor">macOS 커널 (xnu)</text>
  <text x="185" y="72" fill="currentColor" font-family="monospace" font-size="12">ulimit -c ≠ 0 · kern.coredump=1</text>
  <text x="185" y="90" fill="currentColor" font-family="monospace" font-size="12">서명 필수 · /cores 쓰기 가능</text>
  <path d="M405 70 L440 70" stroke="currentColor" stroke-width="1.5"/>
  <rect x="440" y="34" width="290" height="74" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="450" y="54" fill="currentColor" font-family="monospace" font-size="12">/cores/core.%P  ≈ 6.7 GiB, ~21 s</text>
  <text x="450" y="72" fill="currentColor" font-family="monospace" font-size="12">MH_CORE: 139 × LC_SEGMENT_64</text>
  <text x="450" y="90" fill="currentColor" font-family="monospace" font-size="12">4 × LC_THREAD (x0..pc, far, esr)</text>
  <path d="M585 108 L585 130" stroke="currentColor" stroke-width="1.5"/>
  <rect x="440" y="130" width="290" height="46" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="450" y="150" fill="currentColor" font-family="monospace" font-size="12">lldb -b -c core ./crash -s x.lldb</text>
  <text x="450" y="168" fill="currentColor" font-size="12">+ 같은 UUID 의 바이너리 + crash.dSYM</text>
  <rect x="10" y="200" width="130" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="222" fill="currentColor">같은 crash.c</text>
  <text x="20" y="242" fill="currentColor" font-family="monospace" font-size="12">gcc -g -O0 (VM)</text>
  <path d="M140 230 L175 230" stroke="currentColor" stroke-width="1.5"/>
  <rect x="175" y="194" width="230" height="74" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="185" y="214" fill="currentColor">Linux 커널</text>
  <text x="185" y="232" fill="currentColor" font-family="monospace" font-size="12">core_pattern: |apport … (기본)</text>
  <text x="185" y="250" fill="currentColor" font-family="monospace" font-size="12">또는 파일 core.%e.%p</text>
  <path d="M405 230 L440 230" stroke="currentColor" stroke-width="1.5"/>
  <rect x="440" y="194" width="290" height="74" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="450" y="214" fill="currentColor" font-family="monospace" font-size="12">core.tx_worker.&lt;pid&gt;  ≈ 25 MB</text>
  <text x="450" y="232" fill="currentColor" font-family="monospace" font-size="12">ET_CORE: 19 × PT_LOAD</text>
  <text x="450" y="250" fill="currentColor" font-family="monospace" font-size="12">4 × NT_PRSTATUS + NT_SIGINFO …</text>
  <path d="M585 268 L585 288" stroke="currentColor" stroke-width="1.5"/>
  <rect x="440" y="288" width="290" height="34" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="450" y="310" fill="currentColor" font-family="monospace" font-size="12">gdb-multiarch -batch ./crash core</text>
</svg>
```

코어 하나로 알 수 있는 것과 없는 것 (이 레슨에서 모두 실측):

| 질문 | 코어로? | 어디서 |
|---|---|---|
| 어느 스레드가, 어느 줄에서, 어떤 주소 때문에 죽었나 | 예 | `LC_THREAD` 의 pc/far/esr, 스택 메모리 + DWARF |
| 그 순간 지역 변수·힙 객체의 값 | 예 (`-O0`/`-g` 일 때 깨끗) | `LC_SEGMENT_64` 메모리 + DWARF 위치 정보 |
| 다른 스레드는 그때 무엇을 하고 있었나 | 예 | 스레드별 `LC_THREAD` → 각자의 bt |
| 누가 그 바이트를 **썼나** | 직접은 아니오 | 단서만: 아직 스택에 남은 writer 의 프레임, 빵부스러기(breadcrumb) 링 버퍼, 바이트의 모양 |
| 스레드 이름 | Mac 커널 코어: 아니오 / Linux: 크래시 스레드만 파일명 `%e` 로 | 우리 `struct task` 의 `name`(RTOS TCB 처럼) |
| `strlen()` 같은 함수 호출 결과 | 아니오 | 프로세스가 없다 → lldb IR 인터프리터가 못 하는 식은 에러 |

### Part A — 코어를 만들고 `lldb -c` 로 읽기

- `ulimit -c unlimited` + `codesign -s - -f --entitlements ent.plist`(get-task-allow) 로 `crash` 를 죽여 `/cores/core.<pid>` 를 만든다. 크기를 재고, `otool -h`/`otool -l` 로 `MH_CORE` 안의 load command 를 센다. **끝나면 반드시 지운다** — 이 맥에서 하나에 약 6.7 GiB 다.
- `lldb -c /cores/core.<pid> ./crash` 로 `thread list`, `bt`, `register read pc lr sp far esr`, `frame variable`, `p *g_sess`, `memory read`, 다른 스레드의 프레임 선택을 손으로 해 본다.

### Part B — 사후 분석 스크립트의 함정 (starter.lldb 의 4 버그)

체커는 `lldb -b -c <core> <work>/crash -s <variant>.lldb` 를 돌리고 출력에서 사실(fact) 9 개를 grep 한다. starter.lldb 는 그럴듯하지만 네 군데가 실제로 자주 틀리는 자리다 — 스레드 선택, 포인터와 그 주소, 디버그 정보 없는 프레임, 코어에서 불가능한 식(그리고 `lldb -b` 가 첫 에러에서 멈추는 성질). 어느 줄이 왜 틀렸는지 찾는 것이 과제다.

### Part C — 현실: 크기, 대안 코어, Linux, 펌웨어

- 커널 코어가 왜 6.7 GiB 인가, lldb `process save-core --style stack / modified-memory` 의 작은 코어는 무엇을 잃는가, `.ips` 리포트는 코어와 무엇이 다른가.
- Lima VM: apport 기본 경로 → `core_pattern` 을 파일로(끝나면 복원) → ELF 코어 → `gdb-multiarch`/네이티브 gdb → `readelf -n`.
- 펌웨어로 옮기면: MCU 에는 파일 시스템도 커널도 없다. "코어" = 크래시 순간 레지스터 + RAM 일부를 `.noinit`/플래시에 저장하고 다음 부팅에 꺼내는 것(BM-A09, ARM-M14, 형식과 디코더는 TOOL-A05).

## 2. Problem (English)

> **Post-mortem a multi-threaded crash from a core file.**
>
> `crash.c` runs three named tasks (`watchdog`, `tx_worker`, `rx_parser`) that share a heap `struct session`. A field unit crashed with SIGSEGV; you get the binary (with its dSYM) and the core file — you cannot re-run it.
>
> 1. Enable core dumps on macOS and produce `/cores/core.<pid>` (state what is required: limits, sysctls, signing/entitlement). Report the core's size and its load commands. Delete it afterwards.
> 2. Write an lldb command script, run as `lldb -b -c CORE ./crash -s postmortem.lldb`, that prints: the crashing thread's backtrace (frame #0 must be the faulting function), `far`/`esr` of the crashing thread, the crashing frame's locals, the crashing **task's** name, the heap object both as fields and as raw bytes, the writer task's own frame (its locals) and the breadcrumb ring.
> 3. The script must run to the end on a core (no command may fail — `lldb -b` stops at the first error).
>
> Constraints: no hard-coded addresses (ASLR moves them every run); nothing may execute in the target. Follow-ups: what exactly is inside a Mach-O / ELF core; why the macOS core is gigabytes; why a rebuilt binary does not work; how you would get the same answers on a Cortex-M with no OS.

Expected facts are exactly the PASS lines F1–F9 of `bash check.sh solution` (section 6).

## 3. 시작 코드

레슨 폴더:

```text
TOOL-M11/
  crash.c                 # 피해자 프로그램 (Mac + Linux 공용)
  ent.plist               # com.apple.security.get-task-allow = true
  starter.lldb  solution.lldb  practice.py     # 변형(variant) = 사후 분석 스크립트
  check.sh                # 체커: 빌드 → 서명 → 크래시 → /cores 코어 → lldb -b -c → 코어 삭제 → grep
  demo_mac.sh  → demo_mac.txt        # 서명/엔타이틀먼트 행렬, 코어 해부, .ips
  demo_savecore.sh → demo_savecore.txt   # lldb process save-core (stack / modified-memory)
  demo_vm.sh + demo_vm.gdb → demo_vm.txt # Lima VM: apport, core_pattern, gdb-multiarch
```

피해자 — 버그는 89 줄(`memcpy` 길이를 패킷에서 그대로 믿음), 크래시는 115 줄:

<!-- code: crash.c -->

고칠 파일 — `starter.lldb`:

<!-- code: starter.lldb -->

체커 — 변형마다 **새 코어를 만든다**(약 21 초 + lldb 약 10 초, 총 35 초 안팎). 코어는 lldb 가 끝나자마자 지우고, 중간에 실패/Ctrl-C 해도 `trap` 이 지운다. 사실(F1–F9)은 lldb 출력에서 정확한 문자열로 확인한다 — 주소는 ASLR 로 매번 바뀌므로 `tx`(바이트 내용으로 정해지는 값)·`esr`·이름만 고정값으로 본다.

<!-- code: check.sh -->

```bash
bash check.sh starter     # 7/14, exit 1
bash check.sh solution    # 14/14, exit 0
bash check.sh practice    # 13/13, exit 0  (9. 추가 연습)
```

`bash check.sh starter` 실제 출력:

```text
== starter: lldb-2100.0.16.12, 27.2 ==
PASS C1 crash built -g -O0 and ad-hoc signed with com.apple.security.get-task-allow
…
PASS C3 kernel wrote /cores/core.12847 (6717 MiB)
PASS C4 Mach-O filetype 4 (MH_CORE) with 4 LC_THREAD (one per thread)
     (core deleted; lldb exit 1; transcript /tmp/TOOL-M11_work/starter/lldb.txt)
FAIL F1 crashing thread backtrace
     got: * frame #0: 0x00000001875cca80 libsystem_kernel.dylib`__ulock_wait + 8
FAIL F2 fault registers
     got:  far = 0x0000000000000000  esr = 0x56000080
FAIL F3 crashing frame locals
     tx_flush's 'tx' not printed
FAIL F4 crashing task name
     self->name of the crashing thread not printed
PASS F5 heap object fields: name = "front-left-camer" (16 bytes, no NUL)
FAIL F6 heap raw bytes
     got: 0x104968078: 50 91 04 05 01 00 00 00 77 61 74 63 68 64 6f 67  P.......watchdog|0x104968088: …
FAIL F7 writer frame
     rx_parser_main locals not printed
PASS F8 breadcrumb ring: rx_parser rx_name 24
FAIL F9 script stopped early
     error: Can't evaluate the expression without a running target due to: Interpreter doesn't handle one of the expression's opcodes
PASS core removed from /cores
7/14 passed
```

## 4. 힌트

<details>
<summary>힌트 1 — 코어를 열면 어느 스레드가 선택돼 있나?</summary>

`thread list` 의 `*` 표시를 보라. 커널이 `LC_THREAD` 마다 예외 상태(far/esr)를 적어 두고, lldb 는 "stop reason 이 있는 스레드" 를 골라 둔다. 그러니 아무 것도 고르지 않는 것이 정답이다. 확인만 하고 싶으면 `thread info`.

</details>

<details>
<summary>힌트 2 — `g_sess` 와 `&g_sess`, 그리고 writer 의 프레임</summary>

`memory read` 의 인자는 **주소식**이다. `g_sess` 는 포인터 값(힙 주소), `&g_sess` 는 그 포인터가 사는 전역 변수의 주소(`__DATA`). starter 출력의 `50 91 04 05 01 00 00 00` 은 리틀 엔디언으로 읽은 힙 주소 `0x105049150` 이고, 그 뒤 `watchdog` 은 바로 옆 전역 `g_tasks[0].name` 이다. 같은 맥락으로 writer 의 지역 변수는 어느 프레임에 있을까? `rx_parser` 는 일을 마치고 `pthread_cond_wait` 에서 잠들어 있다. frame #0/#1 은 libsystem(디버그 정보 없음), 우리 코드는 frame #2 `rx_parser_main`. `frame select 2` 후 `frame variable self->name last_len p`. `-O0` 이라 지역 변수가 스택에 그대로 있다.

</details>

<details>
<summary>힌트 3 — 코어에서 되는 식, 안 되는 식</summary>

lldb 는 식을 IR 로 컴파일한 뒤, 프로세스가 없으면 **IR 인터프리터**로 흉내 낸다. 메모리 읽기·산술·`sizeof`·멤버 접근은 되지만 함수 호출은 안 된다. `strlen` 대신 `p sizeof(g_sess->name)` 이나 `p g_sess->name`(char 배열 요약)을 쓰면 된다. `settings show interpreter.stop-command-source-on-error` 가 `true` 인 것도 확인해 보라.

</details>

## 5. 풀이

### Part A — 코어가 생기기까지: 조건과 실측 행렬

이 맥의 기본값(`demo_mac.txt`):

```text
== macOS 27.2 (26B7125), arm64
== ulimit -c soft/hard: 0 / unlimited
kern.coredump: 1
kern.corefile: /cores/core.%P
kern.sugid_coredump: 0
drwxrwxr-t  2 root  admin  64 Nov 15 17:38 /cores
System Integrity Protection status: enabled (Apple Internal).
Developer mode is currently disabled.
```

- **soft limit 0** 이 코어를 막는 첫 번째 문. 셸마다 `ulimit -c unlimited`(hard 가 unlimited 라 일반 사용자도 올릴 수 있다). 자식 프로세스가 물려받으므로 체커는 `( ulimit -c unlimited && exec ./crash )` 서브셸로 한정한다.
- `kern.corefile = /cores/core.%P` — `%P` 는 pid. `/cores` 는 `root:admin`, `drwxrwxr-t`(sticky) — admin 그룹(Don 계정 포함)만 쓸 수 있고 자기 파일만 지울 수 있다. 다른 사용자라면 `sudo chmod 1777 /cores` 가 흔한 처방이다(확인 권장).
- `kern.sugid_coredump = 0` — setuid/setgid 프로세스는 코어를 안 남긴다(비밀이 메모리에 있을 수 있으니). Linux 의 `fs.suid_dumpable` 과 같은 생각.

서명 · 엔타이틀먼트 행렬 (같은 `crash.c`, 변형만 서명이 다름):

```text
== signature of each variant
linker    flags=0x20002(adhoc,linker-signed) get-task-allow=0
unsigned  flags=none                   get-task-allow=0
gta       flags=0x2(adhoc)             get-task-allow=1
rt        flags=0x10002(adhoc,runtime) get-task-allow=0
rt_gta    flags=0x10002(adhoc,runtime) get-task-allow=1

== crash matrix
gta       ulimit -c 0         rc=139 core=none               0s
unsigned  ulimit -c unlimited rc=137 core=none               0s
linker    ulimit -c unlimited rc=139 core=7048265744 bytes   21s
rt        ulimit -c unlimited rc=139 core=7048265744 bytes   22s
rt_gta    ulimit -c unlimited rc=139 core=7044071440 bytes   22s
gta       ulimit -c unlimited rc=139 core=7044071440 bytes   21s
```

읽는 법:

| 행 | 관찰 | 해석 |
|---|---|---|
| `gta`, `ulimit -c 0` | rc 139, 코어 없음 | 139 = 128 + 11(SIGSEGV). limit 0 이면 커널이 아예 안 쓴다 |
| `unsigned` | rc **137** = 128 + 9(SIGKILL) | Apple Silicon 은 서명 없는 코드를 실행 자체를 안 한다 — 크래시 전에 죽는다 |
| `linker`(링커 ad-hoc 서명), `rt`(hardened runtime) — 둘 다 엔타이틀먼트 없음 | 코어 생김 | **이 맥에서는** get-task-allow 없이도 코어가 나왔다 |
| `gta`, `rt_gta` | 코어 생김 | 아이템 env_note 가 말하는 정석 조합 |

여기서 정직하게 짚을 점: 아이템 설명과 Apple 개발자 포럼의 안내는 "최근 macOS 에서 코어를 받으려면 `com.apple.security.get-task-allow` 엔타이틀먼트가 필요하다" 이다. 이 맥은 SIP 가 **Apple Internal** 모드로 표시되는 내부 빌드라 정책이 다를 수 있다 — 일반 고객용 macOS 에서는 `linker`/`rt` 행이 `core=none` 이 될 수 있다(확인 권장). 그래서 체커는 항상 `codesign -s - -f --entitlements ent.plist` 로 서명한다 — 어느 쪽 macOS 에서도 동작하게. get-task-allow 는 원래 "다른 프로세스(디버거)가 내 task port 를 얻어도 된다" 는 표시이고, Xcode 디버그 빌드에 자동으로 붙고 App Store/배포 빌드에서는 빠진다.

시간: 코어 하나에 약 21 초. 커널이 6.7 GiB 를 디스크에 쓰는 동안 프로세스는 좀비로 남아 있고, `wait` 는 쓰기가 끝나야 돌아온다(체커가 `wait` 직후 바로 파일을 읽을 수 있는 이유).

### Part A — 코어 파일 해부

```text
== anatomy of /cores/core.87935 (crash.gta)
Mach header
      magic  cputype cpusubtype  caps    filetype ncmds sizeofcmds      flags
 0xfeedfacf 16777228          0  0x00           4   144      11296 0x00000000
-- load commands:
   1 LC_NOTE
 139 LC_SEGMENT_64
   4 LC_THREAD
-- 139 LC_SEGMENT_64, file bytes 7048249344 (6722 MiB)
   initprot 0x00000005 (r-x):   4374 MiB
   initprot 0x00000001 (r--):   2102 MiB
   initprot 0x00000003 (rw-):    245 MiB
-- 5 biggest segments:
   vmaddr 0x00000002e5be8000    1574.4 MiB  initprot 0x00000001
   …
-- LC_THREAD exception state per thread (far / esr):
   thread 1: far 0x0000000000000000 esr 0x56000080
   thread 2: far 0x0000000000000000 esr 0x56000080
   thread 3: far 0x656c75646f6d2d61 esr 0x92000004
   thread 4: far 0x0000000000000000 esr 0x56000080
(lldb) memory region 0x2e5be8000
[0x00000002e5be8000-0x0000000348250000) r-- __LINKEDIT
```

- 코어도 Mach-O 다: magic `0xfeedfacf`(64 비트), cputype `16777228` = `0x0100000C` = ARM64, **filetype 4 = `MH_CORE`**(실행 파일은 2 `MH_EXECUTE`, .o 는 1 — TOOL-B04).
- `LC_SEGMENT_64` 하나 = 프로세스의 VM 영역 하나(이름 없음, vmaddr/vmsize/파일 오프셋). 139 개 합이 6722 MiB. 그중 r-x 4.3 GiB + r-- 2.1 GiB 는 **dyld 공유 캐시**(모든 시스템 dylib 를 묶은 거대한 매핑)다 — 제일 큰 1.5 GiB 가 공유 캐시의 `__LINKEDIT`. 쓰기 가능한 rw- 영역은 245 MiB(내용은 따로 분석하지 않았다 — malloc 존, 스레드 스택, 라이브러리 데이터 등). xnu 는 읽기 전용 파일 매핑도 빼지 않고 다 쓰는 것으로 보인다(이 실측 기준; 커널 정책은 확인 권장).
- `LC_THREAD` 하나 = 스레드 하나: `ARM_THREAD_STATE64`(x0–x28, fp, lr, sp, pc, cpsr) + `ARM_EXCEPTION_STATE64`(far, esr, exception). 크래시 스레드(3)의 `far = 0x656c75646f6d2d61` 이 문제의 주소. 나머지 세 스레드의 `esr 0x56000080` 은 EC = `0x56000080 >> 26` = **0x15 (SVC64)**, ISS 의 imm16 = `0x80` — "마지막 예외가 `svc #0x80` 시스템 콜" 이라는 뜻이다. 잠든 스레드는 모두 시스템 콜 안에 있으니 당연하다. ESR 디코딩은 ARM-M14 연습 1 과 같다.
- `LC_NOTE`(`addrable bits`) — 주소 중 유효 비트 수(PAC 마스크용). Mach-O 코어의 확장 메타데이터 자리다.

### Part A — `lldb -c` 로 읽기 (solution 실제 출력, 발췌)

```text
(lldb) thread list
Process 0 stopped
  thread #1: tid = 0x0000, 0x00000001875cca80 libsystem_kernel.dylib`__ulock_wait + 8
  thread #2: tid = 0x0001, 0x00000001875ce228 libsystem_kernel.dylib`__semwait_signal + 8
* thread #3: tid = 0x0002, 0x0000000104af8b3c crash`tx_flush(s=0x0000000104b39150) at crash.c:115:19, stop reason = ESR_EC_DABORT_EL0 (fault address: 0x656c75646f6d2d61)
  thread #4: tid = 0x0003, 0x00000001875ce420 libsystem_kernel.dylib`__psynch_cvwait + 8
(lldb) bt
* thread #3, stop reason = ESR_EC_DABORT_EL0 (fault address: 0x656c75646f6d2d61)
  * frame #0: 0x0000000104af8b3c crash`tx_flush(s=0x0000000104b39150) at crash.c:115:19
    frame #1: 0x0000000104af89f8 crash`tx_worker_main(arg=0x0000000104b000a8) at crash.c:129:18
    frame #2: 0x000000018760fa3c libsystem_pthread.dylib`_pthread_start + 116
(lldb) register read pc lr sp far esr
      pc = 0x0000000104af8b3c  crash`tx_flush + 24 at crash.c:115:19
      lr = 0x0000000104af89f8  crash`tx_worker_main + 148 at crash.c:129:11
      sp = 0x000000016b41af80
     far = 0x656c75646f6d2d61
     esr = 0x92000004
(lldb) frame variable
(session *) s = 0x0000000104b39150
(txbuf *) tx = 0x656c75646f6d2d61
(uint32_t) n = 1
```

한 줄씩:

- **`tid = 0x0000 … 0x0003`** — 커널 코어에는 진짜 스레드 id 가 없고 lldb 가 순번을 붙인다. 스레드 이름도 없다(라이브 lldb 에서는 `name = 'tx_worker'` 가 보였다 — `demo_savecore.txt` 첫 줄). 순서는 생성 순서(main, watchdog, tx_worker, rx_parser)로 나왔다.
- **stop reason `ESR_EC_DABORT_EL0`** — 라이브 프로세스의 `EXC_BAD_ACCESS (code=1, address=…)` 대신 lldb 가 코어의 ESR 을 해석해서 보여 준다. `0x92000004`: EC `0x24` = 하위 EL(EL0, 사용자 모드)의 데이터 abort, IL=1, ISS 의 DFSC `0x04` = **레벨 0 변환 실패**(translation fault), WnR=0 → **읽기**. 주소가 사용자 주소 공간(하위 47 비트 남짓)을 한참 벗어나서 페이지 테이블 첫 단계에서 바로 실패한 것이다. 별도 맛보기 프로그램에서 `*(int *)8`(NULL + 8)을 읽었을 때는 `far 0x8, esr 0x92000006`(DFSC 0x06, 레벨 2)이었다 — 같은 "읽기 변환 실패" 라도 주소가 어디냐에 따라 레벨이 다르다.
- **`far = 0x656c75646f6d2d61`** 이 `tx` 와 같다. `tx_flush` 115 줄 `tx->len` — `len` 은 `struct txbuf` 의 오프셋 0 이니 fault 주소 = 포인터 값 그대로. 디스어셈블리도 같은 말을 한다(별도 실측 `disassemble -p -c 3`): `->  0x102264b3c <+24>: ldr    w8, [x8]` — x8 = `tx`, 오프셋 0 의 32 비트 읽기.

- **`n = 1`** — 아직 대입 전이라 스택에 남은 쓰레기. VM 의 gdb 에서는 `n = 59996` 이었다. 코어에서 "초기화 전 지역 변수" 는 의미 없는 값이다.
- 포인터 값을 바이트로 보면 `61 2d 6d 6f 64 75 6c 65` = ASCII **`a-module`**. 포인터 자리에 글자가 있다 → 문자열 복사가 넘쳤다는 강한 지문(ARM-M14 의 "pc = ASCII" 와 같은 추론).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">struct session (힙, 40 B) — rx_parser 의 memcpy 24 B 가 name[16] 을 넘는다</text>
  <text x="10" y="50" fill="currentColor" font-family="monospace" font-size="12">+0x00</text>
  <rect x="60" y="36" width="80" height="26" class="f-mut" stroke="currentColor"/>
  <text x="66" y="54" fill="currentColor" font-family="monospace" font-size="12">magic</text>
  <rect x="140" y="36" width="40" height="26" class="f-mut" stroke="currentColor"/>
  <text x="146" y="54" fill="currentColor" font-family="monospace" font-size="12">id</text>
  <rect x="180" y="36" width="50" height="26" class="f-mut" stroke="currentColor"/>
  <text x="184" y="54" fill="currentColor" font-family="monospace" font-size="12">state</text>
  <rect x="230" y="36" width="250" height="26" class="f-ok" stroke="currentColor"/>
  <text x="240" y="54" fill="currentColor" font-family="monospace" font-size="12">name[16] @+0x08 "front-left-camer"</text>
  <rect x="480" y="36" width="160" height="26" class="f-warn" stroke="currentColor"/>
  <text x="486" y="54" fill="currentColor" font-family="monospace" font-size="12">tx @+0x18 "a-module"</text>
  <rect x="640" y="36" width="50" height="26" class="f-mut" stroke="currentColor"/>
  <text x="646" y="54" fill="currentColor" font-family="monospace" font-size="12">seq</text>
  <path d="M230 80 L640 80" stroke="currentColor" stroke-width="2"/>
  <path d="M630 74 L640 80 L630 86" stroke="currentColor" fill="none"/>
  <text x="240" y="100" fill="currentColor" font-size="12">memcpy(s-&gt;name, p-&gt;payload, p-&gt;len = 24)  — crash.c:89</text>
  <text x="10" y="135" fill="currentColor" font-family="monospace" font-size="12">0x104b39150: de c0 55 5e 07 00 02 00 66 72 6f 6e 74 2d 6c 65  ..U^....front-le</text>
  <text x="10" y="155" fill="currentColor" font-family="monospace" font-size="12">0x104b39160: 66 74 2d 63 61 6d 65 72 61 2d 6d 6f 64 75 6c 65  ft-camera-module</text>
  <text x="10" y="175" fill="currentColor" font-family="monospace" font-size="12">0x104b39170: 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................</text>
  <rect x="274" y="142" width="172" height="18" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <text x="10" y="205" fill="currentColor" font-size="12">리틀 엔디언으로 8 바이트 61 2d 6d 6f 64 75 6c 65 = 0x656c75646f6d2d61 = tx = far</text>
  <text x="10" y="228" fill="currentColor" font-size="12">tx_worker: n = tx-&gt;len → ldr w8, [x8] → 레벨 0 변환 실패 (esr 0x92000004, 읽기)</text>
</svg>
```

크래시 스레드의 "이름" 은 커널 코어에 없으므로, 우리 코드가 메모리에 둔 이름을 읽는다 — RTOS 의 TCB(`pcTaskName`) 와 같은 방식. 스레드 진입 함수가 `struct task *self` 를 받으니 frame #1 에서:

```text
(lldb) frame select 1
frame #1: 0x0000000104af89f8 crash`tx_worker_main(arg=0x0000000104b000a8) at crash.c:129:18
(lldb) frame variable self->name
(char[16]) self->name = "tx_worker"
(lldb) p *g_sess
(session) {
  magic = 1582678238
  id = 7
  state = 2
  name = "front-left-camer"
  tx = 0x656c75646f6d2d61
  seq = 1
  flags = 0
}
```

`magic = 1582678238` = `0x5E55C0DE` → 객체 머리는 멀쩡하다. 즉 "객체 전체가 해제/재사용됐다(UAF)" 가 아니라 "**중간 필드부터** 덮였다" 는 증거. `name` 이 정확히 16 글자로 잘려 보이는 건 NUL 이 없어서 lldb 가 배열 크기에서 멈췄기 때문이다.

그다음은 "누가 썼나". writer 는 일을 끝내고 다음 패킷을 기다리며 잠들어 있다 — **그래서 그 프레임이 아직 스택에 있다**:

```text
(lldb) thread select 4
(lldb) frame select 2
frame #2: 0x0000000104af8afc crash`rx_parser_main(arg=0x0000000104b000d0) at crash.c:107:3
(lldb) frame variable self->name last_len p
(char[16]) self->name = "rx_parser"
(uint32_t) last_len = 24
(pkt) p = (type = '\x01', len = '\x18', payload = "front-left-camera-module")
(lldb) p g_trace
  [1] = (seq = 1, who = "rx_parser", what = "rx_name", arg = 24)   … ([0] main boot, [2] tx_worker tx_flush)
```

증거 사슬: 포인터 자리의 바이트 = `p.payload` 의 17–24 번째 글자, `last_len 24 > sizeof name 16`, 그리고 빵부스러기 링이 순서를 확인해 준다(`rx_parser rx_name 24` 다음에 `tx_worker tx_flush`). 현실에서는 writer 가 이미 리턴했거나 스택이 재사용됐을 수 있다 — 그래서 펌웨어는 **링 버퍼 trace** 를 일부러 둔다(BM-A09 의 `.noinit` 덤프와 같은 이유).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">시간 순서와, 코어가 찍은 순간 (각 스레드의 마지막 프레임)</text>
  <text x="10" y="56" fill="currentColor" font-family="monospace" font-size="12">main</text>
  <text x="10" y="96" fill="currentColor" font-family="monospace" font-size="12">watchdog</text>
  <text x="10" y="136" fill="currentColor" font-family="monospace" font-size="12">rx_parser</text>
  <text x="10" y="176" fill="currentColor" font-family="monospace" font-size="12">tx_worker</text>
  <rect x="140" y="122" width="150" height="24" class="f-warn" stroke="currentColor"/>
  <text x="146" y="139" fill="currentColor" font-size="12">parse_name: 24 B 복사</text>
  <path d="M290 134 L600 134" stroke="currentColor" stroke-dasharray="4 3"/>
  <path d="M290 146 L330 166" stroke="currentColor"/>
  <text x="300" y="118" fill="currentColor" font-size="12">cond_broadcast</text>
  <path d="M90 170 L330 170" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="330" y="160" width="170" height="24" class="f-acc" stroke="currentColor"/>
  <text x="336" y="177" fill="currentColor" font-size="12">tx_flush: tx-&gt;len 읽기</text>
  <text x="510" y="177" fill="currentColor" font-size="12" font-weight="bold">SIGSEGV</text>
  <path d="M600 34 L600 196" stroke="currentColor" stroke-width="2"/>
  <text x="560" y="214" fill="currentColor" font-size="12">코어 = 이 순간</text>
  <text x="610" y="54" fill="currentColor" font-size="12">#1 join 대기</text>
  <text x="610" y="94" fill="currentColor" font-size="12">#2 usleep</text>
  <text x="610" y="138" fill="currentColor" font-size="12">#4 cond_wait</text>
  <text x="610" y="178" fill="currentColor" font-size="12">#3 * 예외</text>
  <text x="10" y="238" fill="currentColor" font-size="12">writer 의 프레임(rx_parser_main, last_len=24)은 잠들어 있기 때문에 남아 있다 — 리턴했다면 사라졌을 증거.</text>
</svg>
```

lldb 가 이걸 어떻게 재구성하나(Medium 깊이):

1. `target create ./crash --core core` — 코어의 메모리에 있는 dyld 이미지 목록으로 로드된 이미지와 **UUID** 를 알아낸다. 주어진 `./crash` 의 `LC_UUID` 가 같아야 그 파일과 `crash.dSYM` 을 쓴다(아래 실수 5, 6).
2. 슬라이드 계산: `image list -o` → `0x2264000` 같은 ASLR 슬라이드. 그래서 주소는 실행마다 다르고, 체커는 주소를 grep 하지 않는다.
3. 스레드별 `LC_THREAD` 를 레지스터 컨텍스트로 쓰고, 코어 메모리의 스택을 읽어 프레임 포인터 사슬(arm64 Darwin 은 fp 를 항상 유지)과 `__unwind_info`/CFI 로 bt 를 푼다.
4. `frame variable` 은 DWARF 의 변수 위치식(`tx` 는 `DW_OP_fbreg +16` — dwarfdump 실측, 위 디스어셈블리의 `[sp, #0x10]` 과 같은 자리)을 그 프레임의 레지스터 값에 적용하고 코어 메모리에서 읽는다. `-O0` 은 모든 지역 변수가 스택에 있어 깨끗하고, `-O2` 면 "변수 사용 불가" 가 늘어난다(TOOL-B03).
5. `p` 식: clang 으로 IR 을 만든 뒤 프로세스가 없으므로 IR 인터프리터로 실행 — 메모리 읽기/산술/멤버 접근은 되고 함수 호출은 안 된다(Part B 버그 4).

### Part B — starter 의 버그 4 개

**버그 1 — `thread select 1`.** starter 의 bt 는 `* frame #0: … __ulock_wait + 8`(main 의 `pthread_join`) 이었고, `register read` 의 `far = 0x0, esr = 0x56000080`(SVC) 는 main 스레드의 것이다. 레지스터는 **스레드마다** 있다. "레지스터 덤프 보세요" 라는 말에 다른 스레드의 레지스터를 읽으면 결론 전체가 틀린다. 코어의 스레드 번호는 크래시 여부와 무관하다(Mac: 생성 순서, Linux gdb: 크래시 스레드가 1 번 — 아래 Part C). 고친 것: `thread info`(확인만). 이 실수 하나로 F1–F3 이 무너지고, F4(이름)는 starter 에서 아예 TODO 였다.

**버그 2 — `&g_sess`.** 출력 `50 91 04 05 01 00 00 00 77 61 74 63 68 64 6f 67  P.......watchdog` — 첫 8 바이트는 힙 주소, 그 뒤는 메모리상 바로 옆 전역 `g_tasks[0].name`. 바이트 덤프가 "그럴듯해 보이는" 게 이 실수의 무서운 점이다. 포인터 변수를 넘기면 lldb 는 그 **값**을 주소로 쓴다: `memory read -c 48 g_sess`. 같은 원리로 `x/6gx s`(gdb) 도 `s` 의 값을 주소로 쓴다.

**버그 3 — writer 의 frame #0.** `thread select 4` 직후 `frame variable` 은 빈 출력(에러도 없다 — 그래서 더 위험하다). frame #0 `__psynch_cvwait`, #1 `_pthread_cond_wait` 는 시스템 라이브러리라 DWARF 가 없다. 우리 코드 frame #2 를 골라야 한다. 숫자 하드코딩(`thread select 4` / `frame select 2`)은 **이 바이너리·이 크래시**에서만 맞는다 — 9. 추가 연습에서 "함수 이름으로 찾기" 로 고친다.

**버그 4 — `strlen` 호출.** 실제 메시지:

```text
(lldb) p (unsigned long)strlen(g_sess->name)
note: Falling back to default language. Ran expression as 'Objective C++'.
error: Can't evaluate the expression without a running target due to: Interpreter doesn't handle one of the expression's opcodes
```

캐스트 없이 `p strlen(...)` 이면 그 전에 `'strlen' has unknown return type; cast the call to its declared return type` 로 실패한다(맛보기 실측). 더 중요한 건 그다음: `lldb -b` 는 첫 에러에서 **남은 명령을 버린다**.

```text
(lldb) settings show interpreter.stop-command-source-on-error
interpreter.stop-command-source-on-error (boolean) = true
```

starter 에서 `POSTMORTEM-END` 가 안 찍히고 lldb 종료 코드가 1 인 이유다. 이 줄이 스크립트 맨 앞에 있었다면 출력 전체가 사라졌을 것이다. 고친 것: `p sizeof(g_sess->name)` → `(unsigned long) 16`. 실행이 필요 없는 식만 쓴다. CI 에서 사후 분석을 돌린다면 마지막 줄 마커 + 종료 코드를 반드시 본다.

부수 관찰: lldb 는 `thread select` 의 상태 출력을 늦게 찍기도 한다(solution 출력에서 `* thread #4 …` 블록이 `p sizeof` 뒤에 나옴) — grep 체커는 순서를 가정하지 않는다.

### Part C — 코어가 너무 클 때: lldb `process save-core`

lldb 로 실행하다가 크래시에서 멈췄을 때(또는 `lldb -p PID` 로 붙었을 때) 디버거가 코어를 쓸 수 있다. `-k` 는 "크래시로 멈추면 실행할 명령" 이다(`-o` 만 쓰면 크래시 후 배치가 멈춘다 — 맛보기 실측).

```text
== run under lldb; at the crash save two cores (-k = commands to run after a crash stop)
* thread #3, name = 'tx_worker', stop reason = EXC_BAD_ACCESS (code=1, address=0x656c75646f6d2d61)
(lldb) process save-core --style stack /tmp/TOOL-M11_work/savecore/stack.core
(lldb) process save-core --style modified-memory /tmp/TOOL-M11_work/savecore/dirty.core
== post-mortem on stack.core
  magic = <read memory from 0x10059d500 failed (0 of 4 bytes read)>      ← frame variable *s
(lldb) p g_trace[1]
error: Couldn't apply expression side effects : Couldn't dematerialize a result variable: couldn't read its memory
== post-mortem on dirty.core
  tx = 0x656c75646f6d2d61                                             ← frame variable *s
(lldb) p g_trace[1]
(trace_ev)  (seq = 1, who = "rx_parser", what = "rx_name", arg = 24)
```

| 코어 | 크기 | bt | 힙 객체 | 전역(`g_trace`) | 비고 |
|---|---|---|---|---|---|
| 커널 `/cores/core.%P` | 7 044 071 440 B, ~21 s | ✓ | ✓ | ✓ | tid 없음, 이름 없음 |
| lldb `--style full` | 7 121 956 864 B, ~4 분 | — | — | — | 손으로 한 번 측정, 스크립트에서는 생략 |
| lldb `--style modified-memory` | 1 105 920 B | ✓ | ✓ | ✓ | 더러워진(dirty) 페이지만 — 이 크래시엔 충분 |
| lldb `--style stack` | 86 016 B | ✓ | ✗ | ✗ | 스택만: bt 와 지역 변수까지 |

교훈: "작은 코어" 는 **무엇을 버렸는지** 알고 골라야 한다. 이 크래시의 핵심 증거는 힙에 있어서 stack 스타일로는 원인 복원이 불가능하다. 또 lldb 아래에서 돌면 ASLR 이 꺼져(`pc 0x100000b3c` 같은 고정 주소) 커널 코어와 주소가 다르다. 두 lldb 코어를 열 때 `Bad CPU type in executable` 이라는 문구가 stderr 에 한 번 찍혔는데 분석 결과에는 영향이 없었다(원인 미확인).

ReportCrash 가 같은 크래시마다 남기는 `.ips`(`~/Library/Logs/DiagnosticReports/`)는 코어가 아니다:

```text
crash.gta-2026-11-15-174320.ips: 18490 bytes
exception: {'codes': '0x0000000000000001, 0x656c75646f6d2d61', 'rawCodes': [1, 7308345369542077793], 'type': 'EXC_BAD_ACCESS', 'signal': 'SIGSEGV', 'subtype': 'KERN_INVALID_ADDRESS at 0x656c75646f6d2d61'}
faultingThread: 2 | frames: ['tx_flush', 'tx_worker_main', '_pthread_start', 'thread_start']
has heap/memory contents: False
```

스레드별 bt 와 레지스터, 이미지 목록(UUID)은 있지만 메모리 내용은 없다 — "어디서" 는 알려 주고 "왜" 는 못 알려 준다. 심볼 붙이기는 TOOL-M14.

### Part C — Lima VM: ELF 코어와 gdb (`demo_vm.txt`)

```text
== core_pattern (default): |/usr/share/apport/apport -p%p -s%s -c%c -d%d -P%P -u%u -g%g -F%F -- %E
== coredump_filter: 00000033
== 1) default: apport pipe
-r-------- 1 donh root 26525696 Nov 15 17:31 core._tmp_TOOL-M11_work_vm_crash.501.<uuid>.1094292.30239435
== 2) core_pattern -> file in the work dir
core_pattern now: /tmp/TOOL-M11_work/vm/core.%e.%p
ulimit -c 0 -> rc=139, cores: 0
core file name: core.tx_worker.1094350   (%e = comm of the CRASHING thread)
25956352 core.tx_worker.1094350
du (allocated KiB): 152
core.tx_worker.1094350: ELF 64-bit LSB core file, ARM aarch64, version 1 (SYSV), SVR4-style
```

- `core_pattern` 이 `|` 로 시작하면 커널이 코어를 **파이프로 프로그램(apport)에** 넘긴다. Ubuntu apport 는 패키지가 아닌 바이너리의 코어를 `/var/lib/apport/coredump/` 에 저장했다(실측; 버전에 따라 다름 — 확인 권장). 시스템 전체 설정이라 실습은 VM 안에서만, 스크립트의 `trap` 이 원래 값으로 복원한다. systemd 배포판이면 `systemd-coredump` + `coredumpctl` 이 이 자리에 있다(이 VM 에는 `coredumpctl`, `eu-stack` 없음).
- `%e` 는 **크래시한 스레드의 comm** 이었다 — `pthread_setname_np` 로 바꾼 `tx_worker` 가 파일 이름에 들어갔다. 파일 이름만으로 크래시 스레드를 알 수 있는 셈.
- 25.9 MB 인데 디스크 할당은 152 KiB — 0 으로 채운 페이지가 구멍(sparse)으로 남는다. `coredump_filter = 0x33`(익명 private/shared + ELF 헤더 + huge private)이 라이브러리 코드 같은 파일 매핑을 빼기 때문에 Mac 처럼 GB 가 되지 않는다. 대신 라이브러리 코드는 분석하는 쪽 디스크의 같은 `.so` 에서 읽어야 한다(버전이 다르면 bt 가 틀어진다).

`readelf -n` 노트 요약(스레드당 하나씩 4 개인 것들이 스레드별 상태):

```text
      4 CORE NT_PRSTATUS (prstatus     ← 스레드별 레지스터 + pid/시그널
      1 CORE NT_SIGINFO (siginfo_t      ← si_addr = fault 주소
      1 CORE NT_FILE (mapped            ← 어떤 파일이 어디 매핑됐나
      …  NT_FPREGSET ×4, NT_PRPSINFO, NT_AUXV, LINUX NT_ARM_PAC_MASK/TLS/SSVE/ZA … ×4
```

gdb-multiarch(발췌):

```text
Program terminated with signal SIGSEGV, Segmentation fault.
[Current thread is 1 (Thread 0xea5cdfcbf160 (LWP 1117625))]
* 1    Thread 0xea5cdfcbf160 (LWP 1117625) 0x0000b6387e380fbc in tx_flush (s=0xb6389d855010) at crash.c:115
  2    Thread 0xea5ce06e2f60 (LWP 1117623) __syscall_cancel_arch () at ../sysdeps/unix/sysv/linux/aarch64/syscall_cancel.S:50
$1 = {magic = 1582678238, id = 7, state = 2, name = "front-left-camer", tx = 0x656c75646f6d2d61, seq = 1, flags = 0}
$3 = 11
$4 = 0x656c75646f6d2d61
0xb6389d855018:	"front-left-camera-module\001"
No threads match 'rx_parser'
Thread 4 (Thread 0xea5cdf4af160 (LWP 1117626)):
$5 = "rx_parser\000\000\000\000\000\000"          … ($6 watchdog, $7 tx_worker)
$8 = 24                                       ← tfaas p last_len: 그 프레임에만 있음
```

- Linux 코어에서는 **크래시 스레드가 gdb 의 1 번**(커널이 그 `NT_PRSTATUS` 를 먼저 쓴다). LWP 는 진짜 커널 스레드 id. 나머지 스레드의 번호는 실행마다 달랐다(한 번은 rx_parser 가 3 번, 다음엔 4 번) — 그래서 스크립트는 `tfaas p last_len`(= `thread apply all -s -- frame apply all -s p last_len`: 모든 스레드의 모든 프레임에서 시도하고 실패는 조용히 건너뜀)으로 **이름이 아니라 변수 존재로** writer 를 찾는다.
- `$_siginfo` 는 `NT_SIGINFO` 를 그대로 보여 준다: `si_signo = 11`, `si_addr = 0x656c75646f6d2d61` = Mac 의 `far` 와 같은 값.
- `thread find rx_parser` → `No threads match` — gdb 도 코어에서 스레드 이름을 모른다. 이름은 우리 `struct task` 로 복원(`$5`).
- `x/s s->name` 이 `"front-left-camera-module\001"` — NUL 이 없으니 다음 필드(seq = 1)까지 읽는다. lldb 의 `name = "front-left-camer"` 와 표현만 다르다.
- **네이티브 gdb 도 코어는 읽는다**(`== 4)` 에 같은 스레드 목록). 이 VM 의 네이티브 gdb 가 라이브 프로세스에서 내는 "Unable to fetch SVE/SSVE vector length" 오류(TOOL-B10 이 qemu + gdb-multiarch 로 돌아간 이유)는 코어 분석에서는 나타나지 않았다.

### Part C — 펌웨어로 옮기면

| 데스크톱 코어 | Cortex-M "코어 덤프" |
|---|---|
| 커널이 크래시를 잡아 파일로 | HardFault 핸들러가 스택된 r0–r3, r12, lr, pc, xPSR + CFSR/HFSR/MMFAR/BFAR 저장(ARM-M12) |
| `LC_THREAD` / `NT_PRSTATUS` (스레드별) | RTOS 태스크 목록(TCB)을 돌며 각 태스크의 저장된 sp/스택 일부 |
| `LC_SEGMENT_64` / `PT_LOAD` 전체 메모리 | RAM 전부는 못 담는다 → `.noinit` 영역 또는 플래시 섹터에 **선택한** 영역만(BM-A09) |
| `/cores` 디스크 | 다음 부팅에서 꺼내 UART/BLE/호스트로 업로드 |
| lldb + dSYM(UUID) | 호스트 쪽 디코더 + ELF(build-id)로 심볼 — 형식과 디코더는 TOOL-A05, build-id 정책은 TOOL-A06 |

이 레슨의 `g_trace` 링과 `struct task.name` 은 "커널 코어에 없는 것을 펌웨어가 스스로 남기는" 습관을 그대로 옮긴 것이다. Zephyr 의 coredump 서브시스템, Memfault 같은 서비스가 이 모양을 제품화했다(세부는 확인 권장). 그리고 덤프를 gdb 에 먹이는 길은 둘이다: 덤프를 **ELF 코어 파일로 변환**해 `arm-none-eabi-gdb fw.elf core` 로 열거나, 덤프를 메모리/레지스터 저장소로 삼는 작은 **gdb 원격 서버**를 띄워 `target remote` 로 붙는 것 — Zephyr 의 coredump 도구는 후자(gdbserver 스크립트)를 제공하는 것으로 알고 있다(확인 권장).

## 6. 정답 코드

<!-- code: solution.lldb diff=starter.lldb -->

`bash check.sh solution` 실제 출력:

```text
== solution: lldb-2100.0.16.12, 27.2 ==
…  (C1–C4 는 starter 와 같음: pid 17298, core 6717 MiB)
     (core deleted; lldb exit 0; transcript /tmp/TOOL-M11_work/solution/lldb.txt)
PASS F1 backtrace of the CRASHING thread: frame #0 = tx_flush
PASS F2 fault registers: far = 0x656c75646f6d2d61, esr = 0x92000004
PASS F3 crashing frame locals: (txbuf *) tx = 0x656c75646f6d2d61
PASS F4 crashing task name: tx_worker
PASS F5 heap object fields: name = "front-left-camer" (16 bytes, no NUL)
PASS F6 heap raw bytes: ...ft-camera-module spills over name[16] into tx
PASS F7 writer's own frame: rx_parser, last_len = 24
PASS F8 breadcrumb ring: rx_parser rx_name 24
PASS F9 script ran to the end (no command failed on the core)
PASS core removed from /cores
14/14 passed
```

바뀐 줄이 왜 고쳐지나: `thread info` 는 선택을 바꾸지 않는다(버그 1), `g_sess` 는 값으로 주소를 준다(버그 2), `frame select 2` 가 DWARF 가 있는 우리 프레임으로 간다(버그 3), `sizeof` 는 실행 없이 계산된다(버그 4). 그리고 `frame select 1` + `frame variable self->name` 이 TODO 였던 태스크 이름을 채운다.

참고 — 진짜 원인 수정(코어 분석의 결론)은 `crash.c` 89 줄에서 길이를 믿지 않는 것: `size_t n = p->len < sizeof s->name ? p->len : sizeof s->name - 1; memcpy(s->name, p->payload, n); s->name[n] = '\0';`. 덧붙여 실측: 이 버그는 **ASan 으로도 안 잡힌다** — 넘친 바이트가 같은 malloc 블록 안(`tx` 필드)에 떨어지기 때문이다. `-fsanitize=address` 빌드는 `heap-buffer-overflow` 없이 `tx_worker_main` 의 `SEGV on unknown address` 로만 끝났고, `-O2 -D_FORTIFY_SOURCE=3` 도 그대로 rc 139 였다. 구조체 내부 오버플로(intra-object overflow)는 코어 분석이나 길이 검사로 잡는다(TOOL-B11 참고).

## 7. 자주 하는 실수

1. **`ulimit -c` 를 다른 셸에서 올림.** limit 은 프로세스별로 상속된다. 터미널 A 에서 올리고 B 에서 실행하면 코어가 없다. CI 라면 크래시할 프로세스를 띄우는 바로 그 셸에서.
2. **코어를 쌓아 둠.** 이 맥에서 하나에 6.7 GiB. 체커처럼 `trap '… rm -f "$CORE"' EXIT INT TERM` 으로 반드시 지운다. `/cores` 는 sticky 라 남의 코어는 못 지운다 — 자기 pid 만.
3. **"1 번 스레드 = 크래시 스레드".** Mac 코어는 생성 순서, Linux gdb 는 크래시 스레드가 1 번 — 도구마다 다르다. stop reason(`*`)으로 찾는다.
4. **포인터 변수의 주소를 덤프.** `memory read &p` 는 포인터가 사는 곳, `memory read p` 가 가리키는 곳.
5. **dSYM 없이 분석.** `cc -g` 한 번에 빌드하면 clang 이 `crash.dSYM` 을 만들고 중간 `.o` 는 지운다. dSYM 을 치운 실측: bt 는 ``crash`tx_flush + 24`` 처럼 줄 번호 없이 나오고 `frame variable` 은 `error: debug map object file "/var/folders/…/T/crash-6d75ed.o" containing debug info does not exist, debug info will not be loaded`. Mach-O 실행 파일 자체에는 DWARF 가 없고 "디버그 맵"(어느 .o 에 있나)만 있다. 배포하는 바이너리마다 dSYM 을 보관해야 한다(TOOL-M14).
6. **다시 빌드한 바이너리로 열기.** 한 줄 바꿔 다시 빌드한 `crash_v2`(UUID `9724F2E8…`)로 원래 코어(UUID `EED717C7…`)를 열었더니, lldb 는 **경고 없이** `crash_v2` 를 버리고 코어에 기록된 경로의 원본을 썼다. 원본까지 지우자 코어 안의 이미지 페이지에서 심볼만 읽어 `tx_flush + 24` 처럼 줄 번호 없는 bt 가 나왔다. "같은 소스" 는 의미 없다 — **같은 UUID/build-id** 의 바이너리 + 디버그 정보가 있어야 한다(TOOL-A06).
7. **코어에서 함수 호출.** `p strlen(...)`, `po obj`(Objective-C 런타임 호출) 등은 실패하고, `lldb -b` 는 거기서 멈춘다. 데이터는 `frame variable`/`memory read`/`sizeof` 로.
8. **`-O2` 코어를 `-O0` 처럼 기대.** 최적화 빌드에서는 지역 변수가 레지스터에만 있다가 덮여 `<variable not available>` 이 된다. 필드 크래시 분석이 중요한 제품이라면 릴리스 빌드에도 `-g` 를 켜고(크기 비용은 디버그 정보 파일에만 — TOOL-M13/M14) 핵심 상태는 전역/링 버퍼에 둔다.

## 8. 면접 Q&A

<details>
<summary>Q1. 코어 파일에는 정확히 무엇이 들어 있나요? 무엇이 없나요?</summary>

메모리 영역 스냅숏(Mach-O `LC_SEGMENT_64` 139 개 / ELF `PT_LOAD` 19 개)과 스레드별 레지스터(Mach-O `LC_THREAD` = 범용 레지스터 + far/esr, ELF `NT_PRSTATUS` + `NT_SIGINFO`), 그리고 메타데이터(Linux `NT_FILE` 매핑 목록, `NT_AUXV`, Mach-O `LC_NOTE`). 없는 것: 디버그 정보(별도 dSYM/DWARF), 실행 능력(함수 호출 불가), 과거(누가 언제 썼는지), Mac 커널 코어의 경우 스레드 id·이름. Linux 는 기본적으로 파일 매핑 내용도 뺀다(`coredump_filter`).

</details>

<details>
<summary>Q2. macOS 에서 코어가 안 생깁니다. 무엇을 확인하나요?</summary>

순서대로: ① 크래시하는 그 셸의 `ulimit -c`(기본 0), ② `sysctl kern.coredump`(1) 과 `kern.corefile`(`/cores/core.%P`), ③ `/cores` 쓰기 권한(`root:admin drwxrwxr-t`), ④ 코드 서명 — 서명이 없으면 Apple Silicon 에서 실행조차 안 된다(rc 137), 그리고 일반 macOS 에서는 `com.apple.security.get-task-allow` 엔타이틀먼트(`codesign -s - -f --entitlements ent.plist`). 이 맥(Apple Internal SIP)에서는 엔타이틀먼트 없이도 생겼으니 환경 차이를 의심, ⑤ setuid 이면 `kern.sugid_coredump`, ⑥ 디스크 공간(6.7 GiB). 그리고 시그널이 SIGKILL 이면(OOM, watchdog kill) 코어는 원래 안 생긴다.

</details>

<details>
<summary>Q3. 크래시 주소가 0x656c75646f6d2d61 입니다. 무엇을 의심하나요?</summary>

바이트로 풀면 ASCII `a-module` — 포인터 자리에 문자열이 들어갔다. 즉 버퍼 오버플로가 인접 필드를 덮었고 크래시 지점은 피해자일 뿐이다. 확인: ① `far == 그 포인터 + 필드 오프셋`(여기선 오프셋 0), ② 포인터가 든 객체를 바이트로 덤프해 문자열이 어디서 시작하는지(`name[16]` 부터), ③ 머리(`magic`)가 멀쩡하면 UAF 가 아니라 부분 덮어쓰기, ④ 그 문자열을 다루는 코드(writer)를 찾는다 — 스택에 남은 프레임, trace 링. 0 근처 작은 값이면 NULL + offset, `0x5555…`/`0xaaaa…` 는 해제된 메모리 패턴(ARM-M14).

</details>

<details>
<summary>Q4. 코어에서는 디버거로 왜 함수를 못 부르나요? 그럼 뭘 쓰나요?</summary>

함수 호출 = 대상 프로세스의 스레드를 잠시 빌려 그 코드로 점프시키는 것이다(레지스터 설정 → 실행 → 결과 회수). 코어에는 프로세스도 스레드도 없다. lldb 는 IR 인터프리터로 메모리 읽기·산술·멤버 접근·`sizeof` 정도만 흉내 낸다 — 실측 에러 "Can't evaluate the expression without a running target". 그래서 데이터 구조 해석은 `frame variable`, `memory read`, 데이터 포매터, 디버거 안의 Python(SB API — 이 레슨의 practice.py) 처럼 **읽기만 하는** 도구로 한다. gdb 도 같다(`print func()` 불가, Python pretty-printer 는 가능).

</details>

<details>
<summary>Q5. 왜 macOS 코어는 GB 단위이고 Linux 코어는 수십 MB 인가요? 줄이는 방법은?</summary>

실측 6.7 GiB 중 6.3 GiB 가 dyld 공유 캐시의 r-x/r-- 매핑이었다 — xnu 는 매핑된 영역을 거의 그대로 쓴다. Linux 는 `/proc/<pid>/coredump_filter`(기본 0x33)로 파일 기반 매핑을 빼고, 0 페이지는 sparse 로 남겨 디스크엔 152 KiB 만 썼다. 줄이기: Linux 는 `coredump_filter` 비트 조정, `MADV_DONTDUMP`(큰 버퍼 제외), systemd-coredump 의 압축/크기 제한. Mac 은 lldb `process save-core --style modified-memory`(1.1 MB) 나 `stack`(86 KB, 힙 없음) — 무엇을 버리는지 알고. 근본적으로는 "어떤 상태가 필요한가" 를 정하고 그것만 남기는 펌웨어식 크래시 덤프가 같은 결론이다.

</details>

<details>
<summary>Q6. 다시 빌드한 바이너리로 코어를 열면 안 되는 이유는?</summary>

코어에는 주소만 있고, 주소 → 함수/줄/변수 위치 매핑은 **그 빌드**의 심볼과 DWARF 에 있다. 한 줄만 바뀌어도 코드 배치·스택 오프셋이 달라질 수 있어 엉뚱한 줄과 쓰레기 값이 나온다. 그래서 링커가 `LC_UUID`(Mach-O)/`.note.gnu.build-id`(ELF)를 박고, 디버거는 UUID 가 다르면 그 파일을 버린다 — 실측에서 lldb 는 경고 없이 `crash_v2` 를 무시했다. 운영 규칙: 출시 빌드마다 바이너리 + dSYM/디버그 ELF 를 build-id 로 색인해 보관(심볼 서버), 코어/크래시 리포트의 UUID 로 찾는다(TOOL-M14, TOOL-A06).

</details>

<details>
<summary>Q7. 크래시 스레드와 버그 스레드가 다를 때 코어에서 어떻게 잇나요?</summary>

① 크래시 스레드에서 나쁜 값(`tx`)과 그 값이 들어 있는 객체를 확정, ② 객체를 바이트로 보고 "덮인 모양"(문자열, 길이) 확인, ③ `bt all`/`thread apply all bt` 로 그 객체를 만지는 코드를 가진 스레드를 찾아 **그 스레드의 우리 프레임**에서 지역 변수 확인(여기선 `last_len = 24`, `p.payload`), ④ 빵부스러기(trace 링, 소유자 태그, 시퀀스 번호)로 순서 확인. writer 가 이미 리턴했다면 ③ 이 사라지므로 ④ 가 생명줄이다. 재현이 되면 watchpoint(TOOL-M07)나 ASan(단, 이 버그처럼 구조체 내부 오버플로는 ASan 도 못 잡았다)으로 bug site 를 직접 잡는다.

</details>

<details>
<summary>Q8. 임베디드 Linux 제품에서 코어 덤프 정책을 어떻게 짜겠습니까? MCU 라면?</summary>

Linux: `core_pattern` 을 파이프(systemd-coredump 또는 자체 수집기)로 — 압축·개수/크기 제한·디스크 보호, `coredump_filter`/`MADV_DONTDUMP` 로 큰 버퍼(카메라 프레임 등) 제외, 비밀 정보가 든 프로세스는 `prctl(PR_SET_DUMPABLE, 0)`, 수집기는 build-id 와 함께 서버로 업로드, 호스트에서 `gdb-multiarch` + 심볼 서버로 분석. MCU: 파일 시스템이 없으니 HardFault 핸들러에서 레지스터 + fault 상태 레지스터 + 현재 태스크 스택 일부 + trace 링을 `.noinit` RAM 에 쓰고 리셋, 다음 부팅에서 검증(magic/CRC) 후 플래시/UART 로 내보냄(BM-A09). ELF 코어로 변환하거나 gdb 원격 서버로 내보이면 gdb 를 그대로 쓸 수 있다(TOOL-A05).

</details>

## 9. 추가 연습

`practice.py` = lldb Python(SB API)으로 만든 `pm_report` 명령 하나. **스레드·프레임 번호를 하나도 하드코딩하지 않는** 사후 분석이다. 체커는 `lldb -b -c CORE ./crash -o 'command script import practice.py' -o pm_report` 로 돌리고 아래 P1–P8 을 확인한다.

**연습 1 — 스레드 ↔ 태스크 이름 표.** 모든 스레드의 프레임을 돌며 `struct task *` 타입의 `self` 를 찾아 `self->name` 을 읽어라. 없으면 `(main: no task)`.

<details>
<summary>답</summary>

`frame.FindVariable("self")` → 유효하고 `GetType().GetPointeeType().GetName() == "task"` 이면 `GetChildMemberWithName("name").GetSummary()`. 실측 결과 `#1 → (main: no task)`, `#2 → watchdog`, `#3 → tx_worker`, `#4 → rx_parser`. 타입까지 확인하는 이유: Objective-C 나 다른 코드의 `self` 와 섞이지 않게. 이것이 커널 코어에 없는 "스레드 이름" 을 복원하는 일반적인 방법이다(RTOS 플러그인이 TCB 목록을 읽는 것과 같다).

</details>

**연습 2 — 크래시 스레드를 stop reason 으로, ESR 디코딩까지.** `thread #1` 이 아니라 `GetStopReason() == eStopReasonException` 인 스레드를 고르고, frame #0 의 `esr`/`far` 레지스터를 EC/DFSC/WnR 로 풀어라.

<details>
<summary>답</summary>

`frame.FindRegister("esr").GetValueAsUnsigned()` — 예외 상태 레지스터도 코어의 `LC_THREAD` 에서 온다. EC = `esr >> 26`, ISS = `esr & 0x1ffffff`, DFSC = `ISS & 0x3f`, WnR = ISS 비트 6. 실측 줄: `esr=0x92000004 EC=0x24 DABT_LOWER (data abort, EL0), DFSC=0x04 translation fault, level 0, read; far=0x656c75646f6d2d61`. 읽기 실패이므로 "누가 덮었나" 를 찾아야 하고, 쓰기 실패(WnR=1)였다면 크래시 지점이 곧 덮어쓰기 지점일 수 있다.

</details>

**연습 3 — 힙 객체 판정 + 빵부스러기 링.** `g_sess` 의 `magic` 이 `0x5E55C0DE` 인지, `name[16]` 안에 NUL 이 있는지, `tx == far` 인지, `tx` 바이트가 텍스트인지 한 줄로. 그리고 원형 버퍼 `g_trace[8]` 을 `g_trace_seq` 로 가장 오래된 것부터 최신까지(8 개를 넘게 쌓였을 때도 맞게).

<details>
<summary>답</summary>

`target.FindFirstGlobalVariable("g_sess")` → `Dereference()` 로 필드, 그리고 원시 바이트는 `process.ReadMemory(addr + 8, 24, err)` — 코어에서도 된다(읽기만 하니까). 실측: `session @0x1013fd150: magic OK, name[16] unterminated, tx=0x656c75646f6d2d61 == far, tx bytes as text 'a-module'`. 네 가지 판정이 "머리는 멀쩡, name 에서 넘침, 그 결과가 크래시 주소" 를 한 줄로 말한다. 링은 `for i in range(max(0, seq - n), seq): e = ring[i % n]` — 배열 순서 그대로 찍으면 한 바퀴 돈 뒤 최신과 오래된 것이 섞인다(TOOL-M07 의 링 버퍼 포매터와 같은 함정). 실측 `trace 0: main boot …`, `trace 1: rx_parser rx_name 24`, `trace 2: tx_worker tx_flush 1`.

</details>

**연습 4 — writer 를 함수 이름으로 찾고 한 줄 결론.** `rx_parser_main` 프레임을 가진 스레드를 찾아 `last_len` 과 `sizeof(g_sess->name)` 을 비교하고 `VERDICT:` 한 줄을 써라.

<details>
<summary>답</summary>

모든 스레드·프레임을 돌며 `GetFunctionName() == "rx_parser_main"`. `sizeof` 는 `frame.EvaluateExpression("sizeof(g_sess->name)")` — 실행이 필요 없는 식이라 코어에서도 된다. 실측: `writer: thread #4 task=rx_parser frame #2 rx_parser_main last_len=24 > sizeof name=16 -> 8 bytes into tx`, `VERDICT: rx_parser overflowed session.name (24 > 16); tx_worker crashed reading s->tx`. 번호는 출력에만 쓰고 찾는 데는 쓰지 않으므로 Linux 처럼 스레드 순서가 바뀌어도 동작한다(gdb 쪽 대응은 `tfaas`).

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
bash check.sh practice
```

실제 출력:

```text
== practice: lldb-2100.0.16.12, 27.2 ==
…  (C1–C4 PASS: pid 20489, core 6717 MiB)
     (core deleted; lldb exit 0; transcript /tmp/TOOL-M11_work/practice/lldb.txt)
PASS P1 thread -> task map from struct task *self (2 watchdog, 3 tx_worker, 4 rx_parser)
PASS P2 crashing thread found by stop reason: tx_worker / tx_flush / crash.c:115
PASS P3 ESR decoded: EC 0x24 data abort, DFSC 0x04 level-0 translation fault, read
PASS P4 heap object check: magic OK, name unterminated, tx == far == 'a-module'
PASS P5 trace ring in logical order (main boot -> rx_parser rx_name 24 -> tx_worker tx_flush)
PASS P6 writer located by function name, not by number: 24 > 16 -> 8 bytes into tx
PASS P7 one-line verdict
PASS P8 lldb exit 0 (no command failed on the core)
PASS core removed from /cores
13/13 passed
```

## 10. 핵심 정리

- 코어 = 크래시 순간의 **메모리 영역 + 스레드별 레지스터**. Mac 은 Mach-O `MH_CORE`(LC_SEGMENT_64 + LC_THREAD 의 far/esr), Linux 는 ELF `ET_CORE`(PT_LOAD + NT_PRSTATUS/NT_SIGINFO). 디버그 정보와 실행 능력은 없다.
- Mac 조건: 그 셸의 `ulimit -c unlimited`, `kern.coredump=1`, `/cores` 쓰기, 서명(+ 일반 macOS 는 get-task-allow). 이 맥에서 하나에 ~6.7 GiB/~21 s — **반드시 지운다**.
- Linux: `core_pattern`(apport 파이프 / `core.%e.%p` 파일, `%e` = 크래시 스레드 comm), `coredump_filter` 로 26 MB(sparse 152 KiB). gdb 는 크래시 스레드가 1 번, 네이티브 gdb 도 코어는 읽는다.
- 분석 순서: stop reason 스레드(번호 아님) → `far/esr` → 그 프레임 지역 변수 → 객체를 필드 + 바이트로 → writer 의 남은 프레임 + trace 링 → 결론 한 줄.
- 코어에서는 함수 호출 불가, `lldb -b` 는 첫 에러에서 멈춘다 → 읽기 전용 명령 + 끝 마커 + 종료 코드 확인.
- 같은 UUID/build-id 의 바이너리와 dSYM/DWARF 가 없으면 줄 번호도 변수도 없다. 빌드마다 보관.
- 펌웨어 코어 = 레지스터 + 선택한 RAM + trace 를 `.noinit`/플래시에 → 다음 부팅에 업로드(BM-A09, ARM-M12/M14, TOOL-A05).

## 참고자료

- `man core`, `man launchctl`(limit), `sysctl kern.corefile` — macOS 코어 설정
- Apple, "Diagnosing issues using crash reports and device logs" 및 `com.apple.security.get-task-allow` 엔타이틀먼트 문서: https://developer.apple.com/documentation/xcode/diagnosing-issues-using-crash-reports-and-device-logs
- LLDB — "Using LLDB with core files" / `process save-core` / Python SB API: https://lldb.llvm.org/use/tutorial.html , https://lldb.llvm.org/python_api.html
- Linux `core(5)` 매뉴얼 (`core_pattern`, `%e %p`, 파이프, `coredump_filter`): https://man7.org/linux/man-pages/man5/core.5.html
- GDB 매뉴얼 — Core File Generation, `$_siginfo`, `thread apply` / `frame apply` / `tfaas`: https://sourceware.org/gdb/current/onlinedocs/gdb/
- Arm ARM — ESR_EL1 (EC, DFSC, WnR) 인코딩: https://developer.arm.com/documentation/ddi0601/latest/AArch64-Registers/ESR-EL1--Exception-Syndrome-Register--EL1-
- Zephyr Project — Core Dump 서브시스템: https://docs.zephyrproject.org/latest/services/debugging/coredump.html
- 관련 레슨: TOOL-B04 (Mach-O/ELF 오브젝트), TOOL-B10 (lldb 기초), TOOL-B11 (ASan), TOOL-M07 (watchpoint·lldb Python), TOOL-M14 (dSYM·심볼화), ARM-M12 (CFSR), ARM-M14 (레지스터 덤프 역추적), BM-A09 (`.noinit` 크래시 덤프), TOOL-A05 (덤프 형식·디코더), TOOL-A06 (build-id)
