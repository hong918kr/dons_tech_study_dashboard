---
id: OS-B01
time: 70분
updated: 2026-11-11
---
# OS-B01 프로세스 생성: fork/exec/wait로 미니 셸

## 학습 목표

- `fork → execvp → waitpid` 로 명령 한 줄을 실행하는 미니 셸을 짜고, "fork 는 한 번 불려 두 번 돌아온다", "exec 는 성공하면 돌아오지 않는다" 를 코드로 설명한다.
- 종료 상태(status) 를 `WIFEXITED/WEXITSTATUS` 와 `WIFSIGNALED/WTERMSIG` 로 나누고, 셸의 `$?` 관례(정상 종료 = 0..255, 시그널 종료 = 128+번호, 못 찾음 = 127, 실행 불가 = 126)를 진짜 프로그램으로 확인한다.
- `cmd &` 백그라운드 실행과 SIGCHLD 를 구현하고, 표준 시그널은 쌓이지 않기 때문에 회수는 반드시 `waitpid(-1, &st, WNOHANG)` **루프** 여야 한다는 것을 결정적 테스트로 보인다.
- 좀비(ps 의 `Z`, `<defunct>`)와 고아(부모가 먼저 끝나 launchd = pid 1 로 입양)를 이 맥의 실제 `ps` 출력으로 본다.
- 같은 부품으로 init/systemd 를 줄인 **슈퍼바이저**(on-failure 재시작, 재시작 한도, TERM → KILL 종료)를 만들고 임베디드 리눅스의 init·watchdog 데몬과 연결한다.

## 1. 문제

"셸은 명령을 어떻게 실행하나요? fork 와 exec 를 왜 나눴죠? 좀비 프로세스는 뭐고 어떻게 없애죠?" — OS·임베디드 리눅스 면접의 단골 첫 질문이다. 펌웨어 쪽에서도 매일 나온다: 임베디드 리눅스 보드의 init(BusyBox `init` 의 `respawn`, systemd 의 `Restart=on-failure`, ELX-B12/ELX-B13)은 이 레슨의 Part C 를 크게 만든 것이고, 센서 데몬을 띄우고 죽으면 다시 띄우는 watchdog 데몬, 테스트 장비에서 `fastboot`/`openocd` 같은 도구를 띄우고 종료 코드로 PASS/FAIL 을 가르는 검증 스크립트가 전부 `fork/exec/wait` 다. MMU 가 없는 Cortex-M 에는 `fork` 자체가 없다 — RTOS 는 태스크를 "복제" 하지 않고 함수 포인터와 스택으로 새로 만든다 (RTOS-B02, RTOS-B07). 그 차이를 아는 것도 면접 포인트다.

UNIX 는 프로세스를 **두 단계** 로 만든다 (OSTEP 5장). ① `fork()` 는 호출한 프로세스의 (거의) 정확한 복사본을 만든다 — 부모에게는 자식 pid, 자식에게는 0 을 돌려준다. ② 자식이 `exec*()` 로 자기 주소 공간을 다른 프로그램으로 **갈아 끼운다**. 성공하면 돌아오지 않는다. ③ 부모는 `wait*()` 로 자식의 종료 상태를 **회수(reap)** 한다. 회수되기 전까지 끝난 자식은 커널의 프로세스 표에 pid 와 종료 상태만 남은 **좀비(zombie)** 로 있다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 명령의 일생: fork → exec → exit → (좀비) → waitpid</text>
  <text x="10" y="62" fill="currentColor">부모 (셸)</text>
  <line x1="90" y1="58" x2="720" y2="58" stroke="currentColor"/>
  <text x="10" y="162" fill="currentColor">자식</text>
  <rect x="120" y="44" width="70" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="132" y="63" fill="currentColor">fork()</text>
  <text x="200" y="48" fill="currentColor" font-size="12">리턴값 = 자식 pid</text>
  <path d="M155 72 L155 145" stroke="currentColor" fill="none"/>
  <text x="162" y="112" fill="currentColor" font-size="12">리턴값 = 0</text>
  <line x1="155" y1="158" x2="560" y2="158" stroke="currentColor"/>
  <rect x="210" y="144" width="110" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="220" y="163" fill="currentColor">execvp(argv)</text>
  <text x="210" y="192" fill="currentColor" font-size="12">주소 공간 교체 (pid 그대로)</text>
  <text x="210" y="208" fill="currentColor" font-size="12">실패하면 돌아옴 → _exit(127)</text>
  <rect x="340" y="144" width="110" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="348" y="163" fill="currentColor" font-size="12">새 프로그램 실행</text>
  <rect x="460" y="144" width="80" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="470" y="163" fill="currentColor">exit(n)</text>
  <rect x="545" y="144" width="110" height="28" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="555" y="163" fill="currentColor">좀비 (Z)</text>
  <text x="545" y="192" fill="currentColor" font-size="12">pid + status 만 남음</text>
  <path d="M540 144 L600 72" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="520" y="100" fill="currentColor" font-size="12">SIGCHLD</text>
  <rect x="600" y="44" width="110" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="606" y="63" fill="currentColor" font-size="12">waitpid → 회수</text>
  <path d="M655 144 L655 74" stroke="currentColor" fill="none"/>
  <text x="662" y="112" fill="currentColor" font-size="12">status</text>
  <rect x="90" y="236" width="630" height="54" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="100" y="256" fill="currentColor" font-size="12">fork 와 exec 사이 = 자식 환경을 바꿀 자리: 시그널 마스크 초기화, chdir, dup2 로 리다이렉션(OS-B02)</text>
  <text x="100" y="276" fill="currentColor" font-size="12">부모가 끝까지 wait 하지 않으면? 부모가 먼저 끝나면? → Part B 의 좀비 / 고아</text>
</svg>
```

| 호출 | 하는 일 | 돌아오는 값 |
|---|---|---|
| `fork()` | 호출한 프로세스 복제 (주소 공간은 COW, OS-M03) | 부모: 자식 pid, 자식: 0, 실패: -1 |
| `execvp(file, argv)` | `file` 에 `/` 가 없으면 `PATH` 에서 찾아 주소 공간을 그 프로그램으로 교체 | 성공: **돌아오지 않음**, 실패: -1 + `errno` |
| `waitpid(pid, &st, opts)` | `pid` (−1 이면 아무 자식) 의 종료를 기다려 회수 | 회수한 pid, `WNOHANG` 이고 아직이면 0, 자식 없으면 -1/`ECHILD` |
| `_exit(n)` | stdio flush·atexit 없이 바로 커널로 | 돌아오지 않음 |

### Part A — 토크나이저 + 포그라운드 실행 + 종료 상태 해석

- `int tokenize(char *line, char *argv[], int max, bool *bg)` — 줄을 **제자리(in-place)** 에서 잘라 `argv[]` 를 채운다. 지원 범위(명시): 공백·탭·개행 구분, `'...'` 와 `"..."` (안의 글자는 그대로, 이스케이프·`$` 확장 없음), 붙은 따옴표 이어 붙이기 (`a'b c'd` → `ab cd`), `''` → 빈 인자, 따옴표 밖 `&` 는 **줄 끝에서만** 백그라운드 표시. `|` `<` `>` 는 보통 글자다 (파이프·리다이렉션은 OS-B02).
- `pid_t spawn(char *const argv[])` — fork → 자식은 시그널 마스크를 비우고 `execvp`. 실패하면 `_exit(127)` (못 찾음) / `_exit(126)` (찾았지만 실행 불가).
- `int wait_status(pid_t)` (EINTR 재시도), `int status_code(int status)` — 정상 종료면 종료 코드, 시그널이면 128+번호.
- `int sh_line(shell_t *, const char *line)` — 한 줄 실행. 내장 명령 `cd`, `exit`.
- 테스트: `true`, `/usr/bin/false`, `sh -c 'exit 42'`, `sh -c 'exit 300'`, `sh -c 'kill -9 $$'`, 없는 명령, 실행 권한 없는 파일.

### Part B — 백그라운드 `&` + SIGCHLD + 좀비 / 고아

- `install_sigchld()` — `sigaction(SIGCHLD)`, 핸들러는 **플래그만** 세운다 (핸들러 안에서 할 수 있는 일의 규칙은 OS-B07).
- `reap_jobs()` — 메인 루프가 플래그를 보면 호출. `waitpid(-1, &st, WNOHANG)` 를 0 이 나올 때까지 돌려 끝난 작업을 **전부** 회수하고 작업 표를 갱신한다.
- 체크: SIGCHLD 를 막은 채 자식 3개가 끝나면 핸들러는 **1번** 만 돈다 → 회수 루프가 한 번에 3개를 거둬야 한다. 회수 전 pid 는 좀비로 살아 있고(`kill(pid, 0) == 0`), 회수 후엔 `ESRCH`. 고아는 부모가 pid 1 로 바뀐다.

### Part C — 미니 슈퍼바이저 (init / systemd 축소판)

서비스 5개(`true`, 세그폴트로 죽는 것, `exit 3` 하는 것, `sleep 5`, TERM 을 무시하는 `sleep 5`)를 띄우고 `sup_reap()` 이 정책을 적용한다: 종료 코드 0 → 끝(STOPPED), 그 외 → 한도까지 재시작, 한도 초과 → FAILED. `sup_shutdown()` 은 SIGTERM → 유예 시간 → SIGKILL 로 정리하고 전부 회수한다. 재시작 횟수·마지막 `$?` 를 정확히 체크한다.

## 2. Problem (English)

> **Mini shell with fork/exec/wait.**
>
> Implement the core of a UNIX shell in C (POSIX):
>
> ```c
> int   tokenize(char *line, char *argv[], int max, bool *bg); // in-place; returns argc, -1 on syntax error
> pid_t spawn(char *const argv[]);      // fork + execvp; child exits 127 (not found) / 126 (not executable)
> int   wait_status(pid_t pid);          // waitpid, retry on EINTR; raw status or -1
> int   status_code(int status);         // exit code, or 128 + signal number
> int   sh_line(shell_t *sh, const char *line);   // run one line; builtins cd, exit; trailing & = background
> int   install_sigchld(void);           // SIGCHLD handler that only sets a flag
> int   reap_jobs(shell_t *sh);          // reap ALL finished children with waitpid(-1, ..., WNOHANG)
> ```
>
> Tokens are separated by blanks; single and double quotes group text literally (no escapes or expansion). An unquoted `&` may only appear at the end of the line. No pipes or redirection.
>
> Example: `sh -c 'exit 300'` → 44, `sh -c 'kill -9 $$'` → 137, `no_such_cmd` → 127, a non-executable file → 126, `sleep 1 &` → prints `[1] <pid>` and returns 0 immediately.
>
> Part C: using the same pieces, write a supervisor that restarts a service when it exits with a non-zero code or is killed by a signal, up to `max_restarts` times, and shuts everything down with SIGTERM followed by SIGKILL after a grace period.
>
> Follow-ups: Why are fork and exec separate system calls? Why must the child call `_exit` and not `exit` when exec fails? What is a zombie, an orphan, and who reaps the orphan? Why must the SIGCHLD reaper loop? Why is `cd` a builtin? What does the child inherit across exec (fds, signal mask, ignored signals)? What do you do on a system without an MMU?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.21 초에 끝남 — 멈추지 않는다):

```text
-- Part A: tokenize / spawn / status
FAIL starter.c:233  tok_is("  ls   -l\t/tmp \n", 3, false, (const char *const[]){ "ls", "-l", "/tmp" })
...
FAIL starter.c:262  status_code(st) == 137
...
-- Part B: background jobs / SIGCHLD / zombie / orphan
FAIL starter.c:313  install_sigchld() == 0
...
FAIL starter.c:341  reap_jobs(&sh) == 3
...
-- Part C: supervisor
...
FAIL starter.c:432  svc[4].state == SVC_STOPPED && svc[4].last_code == 128 + SIGKILL
10/74 passed
```

하네스 설계:

- **진짜 프로그램을 실행한다.** macOS 에는 `/bin/true`·`/bin/false` 가 **없다** (`/usr/bin/true`, `/usr/bin/false` 만 있다 — 이 맥에서 `ls` 로 확인). 그래서 테스트는 `PATH` 검색(`execvp("true")`)과 절대 경로 `/usr/bin/false` 를 쓰고, `/bin/true` 는 "있으면 0, 없으면 127" 로 체크한다 — 리눅스에서도 같은 코드가 통과한다.
- **모든 fork 는 회수한다.** 마지막 체크는 `waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD` (남은 자식 0). 틀린 구현이 남긴 자식은 하네스가 SIGKILL + waitpid 로 정리한다.
- **기다림에는 제한 시간이 있다.** 좀비 대기(2 초), 백그라운드 완료 대기(3 초), 슈퍼바이저 정착(2 초), 고아 손자는 `alarm(3)`. 시작 코드는 0.2 초, 정답은 약 1.2 초에 끝난다.
- `run_argv()` 는 `spawn` 이 돌아온 뒤 `getpid() != g_main_pid` 면 `_exit(120)` 한다 — exec 실패 후 `_exit` 를 빼먹어 **자식이 하네스 코드를 계속 실행** 하는 버그(7. 실수 1번)가 테스트 전체를 두 번 돌리는 사고를 막는다.
- 없는 명령 테스트 때문에 `msh: no_such_cmd_os_b01: command not found` 같은 줄이 stderr 에 찍히는 것은 정상이다.

관찰용 파일 (하네스와 별개, 전부 같은 strict 플래그로 빌드됨):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_zombie_orphan.c -o dzo && ./dzo        # ps 로 Z 상태와 PPID=1 보기
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sigchld_coalesce.c -o dsc && ./dsc 50 5 # SIGCHLD 몇 번 오나
cc -std=c11 -Wall -Wextra -Werror -O2 demo_exit_dup.c -o dex && ./dex exit | cat   # exit vs _exit 버퍼 중복
cc -std=c11 -Wall -Wextra -Werror -O2 demo_shell.c -o msh && ./msh                 # 대화형 미니 셸 (solution.c 포함)
```

## 4. 힌트

<details>
<summary>힌트 1 — 토크나이저: 읽는 포인터와 쓰는 포인터를 따로</summary>

따옴표를 지우면 글자가 줄어들 뿐 늘지 않는다. 그래서 `r`(읽기)·`w`(쓰기) 두 포인터로 같은 버퍼를 앞에서부터 덮어써도 아직 안 읽은 글자를 망가뜨리지 않는다 (`w <= r` 이 항상 성립). 토큰이 시작될 때 `argv[argc++] = w`, 끝날 때 `*w++ = '\0'`. 단, 토큰을 끝낸 글자가 `&` 이면 `'\0'` 을 쓰기 **전에** 읽어 둬야 한다 (`w == r` 일 수 있다).

</details>

<details>
<summary>힌트 2 — spawn: 자식 쪽 코드는 반드시 _exit 로 끝난다</summary>

`if (pid == 0) { ...; execvp(argv[0], argv); /* 여기 오면 실패 */ _exit(...); }` — `execvp` 다음 줄은 실패했을 때만 실행된다. `errno == ENOENT` 면 127, 그 외(`EACCES` 등)는 126. `exit()` 를 부르면 부모에게서 복사해 온 stdio 버퍼가 한 번 더 flush 된다 (`demo_exit_dup.c`).

</details>

<details>
<summary>힌트 3 — 자식이 물려받는 것: 시그널 마스크</summary>

fork 와 exec 를 거쳐도 **시그널 마스크와 SIG_IGN 설정은 그대로 상속** 된다 (핸들러로 잡던 시그널만 exec 때 기본값으로 돌아간다). 부모가 SIGCHLD 나 SIGTERM 을 막아 둔 채 spawn 하면 자식 프로그램도 막힌 채 시작한다. 자식에서 `sigprocmask(SIG_SETMASK, &empty, NULL)`.

</details>

<details>
<summary>힌트 4 — SIGCHLD 하나 = "적어도 한 명이 끝났다"</summary>

시그널은 "보류 중(pending)" 비트 하나다. 자식 3명이 거의 동시에 끝나면 핸들러는 1번만 돌 수 있다. 그래서 회수는 "한 번 깨면 끝난 것을 전부": `while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {...}`. 플래그는 루프 **전에** 내린다 — 루프 뒤에 내리면 루프와 내리기 사이에 끝난 자식의 알림을 지운다.

</details>

<details>
<summary>힌트 5 — 슈퍼바이저의 종료: 부탁(TERM) 후 강제(KILL), 그리고 회수</summary>

`kill(pid, SIGTERM)` 은 요청이다 — 프로세스가 무시할 수 있다(`trap '' TERM`). 유예 시간 동안 `waitpid(pid, &st, WNOHANG)` 로 확인하고, 남은 것은 `SIGKILL` (무시·차단 불가) 후 `waitpid` 로 반드시 회수한다.

</details>

## 5. 풀이

### Part A — 토크나이저: 두 포인터로 제자리 자르기

`a'b c'd  x&` 를 자를 때 버퍼가 어떻게 변하나:

| 단계 | 읽은 글자(r) | 동작 | 버퍼 앞부분 (w 까지) |
|---|---|---|---|
| 1 | `a` | 토큰 시작 `argv[0] = w`, 복사 | `a` |
| 2 | `'` | 따옴표 모드 시작, 따옴표 자체는 안 씀 | `a` |
| 3 | `b c` | 따옴표 안: 공백도 그대로 복사 | `ab c` |
| 4 | `'` | 닫는 따옴표, 건너뜀 | `ab c` |
| 5 | `d` | 같은 토큰에 이어 붙임 | `ab cd` |
| 6 | 공백 | 토큰 끝 → `'\0'` | `ab cd\0` |
| 7 | `x` | `argv[1] = w`, 복사 | `ab cd\0x` |
| 8 | `&` | `stop = '&'` 를 먼저 읽고 `'\0'` 쓰기, `bg = true` | `ab cd\0x\0` |
| 9 | 끝 | `argv[2] = NULL`, 반환 2 | |

오류 규칙: 닫히지 않은 따옴표(`echo 'abc`), `&` 뒤에 또 토큰(`a & b`), `&` 만(`  &  `), 인자 15개 초과(`MAX_ARGS` 16 칸 중 1칸은 NULL) → -1. `sh_line` 은 -1 이면 `$? = 2` (bash 의 문법 오류 코드와 같게).

### Part A — spawn: 한 번 불러 두 번 돌아오기

```c
static pid_t spawn(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {                                  /* ---- 자식 ---- */
        sigset_t none; sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);       /* 마스크는 exec 를 지나 상속된다 */
        execvp(argv[0], argv);                       /* 성공하면 여기서 끝 (돌아오지 않음) */
        int e = errno;
        dprintf(STDERR_FILENO, "msh: %s: %s\n", argv[0], ...);
        _exit(e == ENOENT || e == ENOTDIR ? 127 : 126);
    }
    return pid;                                      /* ---- 부모 ---- */
}
```

세 가지가 면접에서 갈린다.

**① 127 과 126.** POSIX 셸 규격(XCU 2.8.2)은 "명령을 찾지 못하면 127, 찾았지만 실행할 수 없으면 126" 이라고 정한다. 자식 안에서 exec 가 실패한 이유(`errno`)로 고른다. 실제 출력:

```text
msh: no_such_cmd_os_b01: command not found              → 127
msh: /bin/true: No such file or directory               → 127   (macOS 에는 /bin/true 가 없다)
msh: /tmp/OS-B01_noexec_HOlEyc: Permission denied       → 126   (0644 파일)
```

**② `exit` 이 아니라 `_exit`.** 자식은 부모의 stdio 버퍼를 **복사해서** 가지고 있다. `exit()` 는 그 버퍼를 flush 하고 atexit 핸들러를 돈다 → 부모가 아직 안 내보낸 글자가 두 번 나간다.

<!-- code: demo_exit_dup.c -->

`demo_exit_dup.c` 실제 출력:

```text
$ ./dex | cat
hello from parent 12085 (still in the stdio buffer) | child exit code 127 (_exit)
$ ./dex exit | cat
hello from parent 12088 (still in the stdio buffer) | hello from parent 12088 (still in the stdio buffer) | child exit code 127 (exit)
```

**③ 시그널 마스크 상속.** POSIX `exec` 규정: 잡던(caught) 시그널은 기본 동작으로 돌아가지만, **무시(SIG_IGN) 설정과 시그널 마스크는 그대로** 다. 이 맥에서 부모가 SIGTERM 을 막은 채 `sh -c 'kill -TERM $$; exit 7'` 을 띄우면, 마스크를 안 비운 버전은 TERM 이 막혀 `exit 7` 까지 가서 **7** 을 돌려주고, 비운 버전은 TERM 에 죽어 **143** 이다 (테스트 `status_code(st) == 143`). Part B 처럼 SIGCHLD 를 막은 채 spawn 하는 코드에서 실제로 터지는 버그다.

### Part A — 종료 상태(status) 읽기

`waitpid` 가 채우는 `int status` 는 **구현 정의 인코딩** 이다. 반드시 매크로로 읽는다. 이 맥의 `<sys/wait.h>` 정의(`_WSTATUS(x) = x & 0177`, `WEXITSTATUS(x) = (x >> 8) & 0xff`, `WCOREFLAG 0200`)와 실제 값:

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">wait status 16비트 (macOS·리눅스 공통 배치 — 그래도 매크로로만 읽는다)</text>
  <rect x="40" y="40" width="320" height="40" class="f-ok" stroke="currentColor"/>
  <text x="120" y="65" fill="currentColor">bit 15..8 : 종료 코드</text>
  <rect x="360" y="40" width="60" height="40" class="f-warn" stroke="currentColor"/>
  <text x="368" y="65" fill="currentColor">bit 7</text>
  <rect x="420" y="40" width="280" height="40" class="f-acc" stroke="currentColor"/>
  <text x="440" y="65" fill="currentColor">bit 6..0 : 죽인 시그널 번호</text>
  <text x="40" y="100" fill="currentColor" font-size="12">WEXITSTATUS = (st &gt;&gt; 8) &amp; 0xff</text>
  <text x="340" y="100" fill="currentColor" font-size="12">core 플래그</text>
  <text x="440" y="100" fill="currentColor" font-size="12">0 이면 정상 종료, 0177 이면 정지</text>
  <text x="40" y="135" fill="currentColor" font-family="monospace" font-size="12">exit 42        raw=0x2a00  WIFEXITED   → 42</text>
  <text x="40" y="155" fill="currentColor" font-family="monospace" font-size="12">exit 300       raw=0x2c00  WIFEXITED   → 44   (300 &amp; 0xff)</text>
  <text x="40" y="175" fill="currentColor" font-family="monospace" font-size="12">kill -9 $$     raw=0x0009  WIFSIGNALED → WTERMSIG 9  → $? 137</text>
  <text x="40" y="195" fill="currentColor" font-family="monospace" font-size="12">kill -TERM $$  raw=0x000f  WIFSIGNALED → WTERMSIG 15 → $? 143</text>
  <text x="40" y="215" fill="currentColor" font-family="monospace" font-size="12">kill -SEGV $$  raw=0x000b  WIFSIGNALED → WTERMSIG 11 → $? 139  (ulimit -c 0 → core 비트 0)</text>
  <text x="40" y="240" fill="currentColor" font-size="12">exit 코드는 8비트뿐이라 "exit 137" 과 "SIGKILL 로 죽음" 은 $? 로는 구별되지 않는다 — status 로는 구별된다</text>
</svg>
```

```c
static int status_code(int status) {
    if (status < 0) return -1;                       /* wait 실패 */
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;                                       /* WUNTRACED 를 안 쓰므로 정지는 안 온다 */
}
```

POSIX 는 시그널 종료를 "128 보다 큰 값" 으로만 정하고, **128 + 번호** 는 bash·zsh·dash 가 공통으로 쓰는 관례다. `exit 300` 이 44 가 되는 것도 면접 단골이다 — 종료 코드는 `status & 0377` 만 부모에게 간다 (POSIX `_exit`).

### Part A — sh_line 과 내장 명령: 왜 `cd` 는 프로그램이 아닌가

`cd` 를 fork 한 자식에서 실행하면 **자식의** 현재 디렉터리만 바뀌고 자식은 곧 끝난다. 셸 자신의 상태(현재 디렉터리, 변수, `exit`)를 바꾸는 명령은 fork 없이 셸 프로세스 안에서 실행해야 한다 — 그래서 내장(builtin)이다. 테스트는 `sh_line("cd /")` 뒤 하네스의 `getcwd()` 가 `/` 인지 본다. `demo_shell.c` 에서 `cd /tmp` 다음 `pwd` 는 `/private/tmp` 를 찍는다 (macOS 의 `/tmp` 는 `/private/tmp` 로 가는 심볼릭 링크 — 외부 `pwd` 는 물리 경로를 보여 준다).

`demo_shell.c` 로 스크립트를 흘려 넣은 실제 기록 (일부):

```text
msh$ sleep 0.2 &
[1] 12116
  ($? = 0)
msh$ sh -c 'sleep 0.4; exit 3' &
[2] 12118
  ($? = 0)
msh$ echo 'fg runs while 2 bg jobs run'
fg runs while 2 bg jobs run
  ($? = 0)
msh$ sleep 0.6
  ($? = 0)
[2] Exit 3    sh -c 'sleep 0.4; exit 3' &
[1] Done      sleep 0.2 &
msh$ cd /tmp
  ($? = 0)
msh$ pwd
/private/tmp
  ($? = 0)
msh$ echo 'unterminated
msh: syntax error: echo 'unterminated
  ($? = 2)
msh$ exit 4
  ($? = 4)
```

`[2]` 가 `[1]` 보다 먼저 보고된 것에 주목: 두 좀비가 동시에 있을 때 `waitpid(-1)` 이 어느 쪽을 먼저 돌려주는지는 **정해져 있지 않다** (이 맥에서는 나중에 만든 자식이 먼저 나왔다 — 테스트 출력도 `[3] [2] [1]` 순). 순서에 기대지 말고 pid 로 작업을 찾는다.

### Part B — 좀비: 끝났지만 회수되지 않은 프로세스

<!-- code: demo_zombie_orphan.c -->

`demo_zombie_orphan.c` 실제 출력 (자식이 `_exit(7)` 한 뒤 100 ms, 부모는 아직 wait 안 함):

```text
me = 10961
== zombie: child exits, parent does not wait yet ==
  PID  PPID STAT COMM
10997 10961 Z    <defunct>
== after waitpid (exit code 7): the pid is gone ==
  PID  PPID STAT COMM
```

좀비는 메모리·파일을 이미 다 돌려준 상태다. 남은 것은 **프로세스 표 한 칸 + pid + 종료 상태** 뿐 — 부모가 나중에 `waitpid` 로 물어볼 수 있게 커널이 보관하는 것이다. 그래서 좀비는 `kill -9` 로도 없어지지 않는다 (이미 죽었다). 없애는 방법은 ① 부모가 회수 ② 부모가 죽어 pid 1 에 입양 → pid 1 이 회수. 좀비가 쌓이면 pid 와 프로세스 표가 고갈된다 (`fork` 가 `EAGAIN`).

하네스는 `ps` 대신 **회수하지 않고 엿보는** `waitid(P_PID, pid, &si, WEXITED | WNOHANG | WNOWAIT)` 로 "끝났다" 를 확인하고, `kill(pid, 0) == 0` 으로 "그래도 pid 는 살아 있다" 를 체크한다. 회수 후엔 `kill(pid, 0) == -1`, `errno == ESRCH`.

### Part B — SIGCHLD 는 쌓이지 않는다: 회수는 루프로

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">자식 3명 종료 → SIGCHLD 보류 비트는 하나 → 핸들러 1번 → 루프가 3명 회수</text>
  <text x="10" y="60" fill="currentColor">자식 1</text>
  <text x="10" y="90" fill="currentColor">자식 2</text>
  <text x="10" y="120" fill="currentColor">자식 3</text>
  <rect x="80" y="48" width="120" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="80" y="78" width="160" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="80" y="108" width="190" height="16" class="f-ok" stroke="currentColor"/>
  <text x="205" y="61" fill="currentColor" font-size="12">exit</text>
  <text x="245" y="91" fill="currentColor" font-size="12">exit</text>
  <text x="275" y="121" fill="currentColor" font-size="12">exit</text>
  <rect x="200" y="48" width="400" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="240" y="78" width="360" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="270" y="108" width="330" height="16" class="f-warn" stroke="currentColor"/>
  <text x="420" y="61" fill="currentColor" font-size="12">좀비</text>
  <text x="10" y="165" fill="currentColor">SIGCHLD 보류</text>
  <rect x="200" y="152" width="200" height="18" class="f-acc" stroke="currentColor"/>
  <text x="210" y="166" fill="currentColor" font-size="12">1 (두 번째·세 번째는 이미 1)</text>
  <text x="10" y="205" fill="currentColor">셸</text>
  <rect x="80" y="192" width="320" height="18" class="f-mut" stroke="currentColor"/>
  <text x="90" y="206" fill="currentColor" font-size="12">SIGCHLD 막힘 (sigprocmask)</text>
  <rect x="400" y="192" width="60" height="18" class="f-acc" stroke="currentColor"/>
  <text x="404" y="206" fill="currentColor" font-size="12">핸들러</text>
  <rect x="470" y="192" width="130" height="18" class="f-ok" stroke="currentColor"/>
  <text x="476" y="206" fill="currentColor" font-size="12">reap_jobs 루프</text>
  <line x1="600" y1="40" x2="600" y2="215" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="608" y="61" fill="currentColor" font-size="12">회수 ×3</text>
  <text x="10" y="245" fill="currentColor" font-size="12">waitpid(-1, &amp;st, WNOHANG) 를 0 이 나올 때까지: 핸들러 횟수와 상관없이 끝난 자식은 전부 거둔다</text>
</svg>
```

표준 시그널(1–31)은 "보류 중" 비트 하나다. 같은 시그널이 보류 중일 때 또 오면 합쳐진다 (POSIX 는 표준 시그널의 큐잉을 요구하지 않는다 — 실시간 시그널만 큐잉). 테스트 B1 이 이것을 결정적으로 만든다: SIGCHLD 를 막고 → 3개 실행 → 셋 다 좀비가 될 때까지 기다리고 → 막은 동안 핸들러 횟수 변화 0 → 풀면 **정확히 1번** (`g_sigchld_count == c0 + 1`) → `reap_jobs` 가 3을 돌려준다.

막지 않은 실제 상황에서는? `demo_sigchld_coalesce.c` (자식 50개가 한꺼번에 `_exit`, 5회):

<!-- code: demo_sigchld_coalesce.c -->

실제 출력 일부 (`./dsc 50 5`):

```text
naive n=50  handler ran 44 times  reaped  2  zombies left 48
loop  n=50  handler ran 48 times  reaped 50  zombies left  0
naive n=50  handler ran 46 times  reaped  2  zombies left 48
loop  n=50  handler ran 45 times  reaped 50  zombies left  0
...
naive n=50  handler ran 47 times  reaped  2  zombies left 48
```

두 단계에서 합쳐진다: ① 커널 — 핸들러는 50번이 아니라 43~48번(5회 전부). ② 우리 플래그 — 핸들러가 40번 넘게 돌아도 플래그는 비트 하나라, 1 ms 마다 보는 메인 루프는 2번만 깨어났다. "깨어날 때마다 하나씩" 회수하는 naive 버전은 48개를 좀비로 남겼고, 루프 버전은 50개 전부 거뒀다. 결론: **알림 횟수 ≠ 끝난 자식 수.** 알림은 "적어도 하나" 일 뿐이다.

핸들러에서 직접 `waitpid` 루프를 돌리는 셸도 많다 (`waitpid` 는 async-signal-safe 목록에 있다). 이 레슨은 회수를 메인 루프로 뺐다 — 작업 표 갱신과 `printf` 를 핸들러에서 하면 안 되는 이유는 OS-B07 에서 다룬다. 핸들러 안에서 `waitpid` 를 부를 때는 `errno` 를 저장·복원해야 한다는 것도 OS-B07.

### Part B — 포그라운드와 백그라운드가 같이 있을 때

포그라운드는 `waitpid(pid, ...)` — **그 pid 만** 기다린다. 백그라운드 회수는 `waitpid(-1, ...)`. 테스트 B2 는 `sleep 0.3 &` 가 도는 중에 `sh -c 'exit 9'` 를 포그라운드로 돌려 9 를 받고, 그 사이 백그라운드 작업은 여전히 RUNNING, 0.3 초 뒤 SIGCHLD 경로로 DONE(0) 이 되는지 본다.

반대 설계 — 핸들러가 `waitpid(-1)` 루프로 회수하면서, 메인은 `waitpid(fg_pid)` 로 기다리는 셸 — 에는 함정이 있다. 포그라운드 자식이 끝나는 순간 핸들러가 먼저 그 자식을 회수해 버리면 메인의 `waitpid(fg_pid)` 는 `ECHILD` 를 받는다 (상태를 도둑맞음). 이 레슨처럼 회수를 메인 루프 한 곳에 두거나, 포그라운드 동안 SIGCHLD 를 막거나, 핸들러가 작업 표에 상태를 기록하고 메인은 그것을 기다리는 식(CS:APP 의 `tsh` 방식)으로 피한다.

### Part B — 고아: 부모가 먼저 끝나면 pid 1 이 입양한다

```text
== orphan: C forks G, C exits first ==
-- while C (11058) is alive --
  PID  PPID STAT COMM
11059 11058 S    sleep
-- after C exited --
  PID  PPID STAT COMM
11059     1 S    sleep
  PID  PPID STAT COMM
    1     0 Ss   /usr/appleinternal/sbin/launchd.development
-- after kill(G): reaped by pid 1, not by me --
  PID  PPID STAT COMM
```

C 가 끝나자 G 의 PPID 가 11058 → **1** 로 바뀌었다. 이 맥의 pid 1 은 launchd (이 기기는 내부 빌드라 `launchd.development`). G 는 내 자식이 아니므로 내가 `waitpid` 할 수 없다 — `kill` 후 회수는 launchd 몫이고, 100 ms 뒤 ps 에서 사라졌다. 하네스 B3 도 같은 것을 체크한다 (`getppid()` 가 C 의 pid 에서 1 로 바뀜, `orphan: parent 91033 -> 1`).

리눅스는 다르다: 가장 가까운 **subreaper**(`prctl(PR_SET_CHILD_SUBREAPER)` — systemd 의 사용자 세션, 컨테이너의 init) 가 있으면 그쪽으로, 없으면 그 pid 네임스페이스의 pid 1 로 간다. 그래서 하네스는 macOS 에서만 `== 1` 을 체크하고 그 외에는 "C 가 아니게 바뀜" 만 본다. 임베디드 리눅스에서 pid 1(BusyBox init, systemd)이 **반드시 SIGCHLD 를 받아 회수해야 하는 이유** 가 이것이다 — 고아는 전부 pid 1 에게 오고, pid 1 이 회수하지 않으면 좀비가 쌓인다. 컨테이너에서 `tini` 나 `docker run --init` 을 쓰는 것도 같은 이유다.

### Part C — 슈퍼바이저: on-failure 재시작

```c
while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
    svc_t *v = 서비스 표에서 RUNNING 이고 pid 가 같은 것;
    if (v == NULL) { s->stray++; continue; }       /* 내 서비스가 아닌 자식 (입양된 고아 등) */
    v->last_code = status_code(st);
    if (v->last_code == 0)                 v->state = SVC_STOPPED;          /* 정상 종료 → 끝 */
    else if (v->restarts < v->max_restarts) { v->restarts++; sup_start(v); }/* 재시작 */
    else                                   v->state = SVC_FAILED;           /* 한도 초과 */
}
```

정답 실행 결과 (state 0=STOPPED 1=RUNNING 2=FAILED):

```text
-- Part C: supervisor
  oneshot  state=0 starts=1 restarts=0 last=0
  crashy   state=2 starts=3 restarts=2 last=139
  flaky    state=2 starts=2 restarts=1 last=3
  daemon   state=1 starts=1 restarts=0 last=0
  stubborn state=1 starts=1 restarts=0 last=0
```

그 뒤 `sup_shutdown(&s, 300)`: `daemon`(`sleep 5`)은 SIGTERM 에 죽어 **143**, `stubborn`(`sh -c "trap '' TERM; exec sleep 5"` — SIG_IGN 이 exec 를 지나 `sleep` 에 상속됨)은 300 ms 동안 버티다 SIGKILL 로 **137**. 둘 다 회수했으므로 마지막 `waitpid(-1, NULL, WNOHANG)` 은 `ECHILD`.

| 이 레슨 | 임베디드 리눅스 실물 |
|---|---|
| `max_restarts` 한도 | systemd `Restart=on-failure` + `StartLimitBurst=`/`StartLimitIntervalSec=` (ELX-B13) |
| 종료 코드 0 → 재시작 안 함 | systemd `Restart=on-failure` 와 같은 생각 (단 systemd 는 SIGHUP/SIGINT/SIGTERM/SIGPIPE 로 죽은 것도 "깨끗한 종료" 로 보고 재시작하지 않는다 — 이 레슨은 시그널이면 전부 실패로 셈). BusyBox `/etc/inittab` 의 `respawn` 은 코드와 상관없이 항상 재시작 |
| SIGTERM → 유예 → SIGKILL | systemd `KillSignal=SIGTERM`, `TimeoutStopSec=` 후 SIGKILL. 셧다운 때 BusyBox init 도 TERM 후 KILL |
| `stray` (모르는 자식) | pid 1 은 입양된 고아를 전부 회수해야 한다 |
| FAILED 상태 | 하드웨어 워치독과 연결: 핵심 서비스가 FAILED 면 `/dev/watchdog` 킥을 멈춰 보드 리셋 (ELX-B13) |

MCU 쪽은? FreeRTOS 에는 "프로세스" 가 없고 태스크가 죽으면 보통 시스템 전체가 HardFault 다. 그래서 MCU 의 "슈퍼바이저" 는 **태스크별 체크인 + 하드웨어 워치독** (모든 태스크가 기한 안에 체크인해야 IWDG 를 킥) 모양이고, 복구 단위는 재시작이 아니라 리셋이다. 리셋 원인을 `.noinit` RAM 에 남기는 것은 BM-A09.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

```text
-- Part A: tokenize / spawn / status
(stderr: command not found / No such file or directory / Permission denied / syntax error / cd 실패 메시지 6줄)
-- Part B: background jobs / SIGCHLD / zombie / orphan
[1] 89783
[2] 89784
[3] 89785
[3] Killed(9) sh -c 'kill -9 $$' &
[2] Exit 3    sh -c 'exit 3' &
[1] Done      true &
[1] 89786
[1] Done      sleep 0.3 &
orphan: parent 91033 -> 1
-- Part C: supervisor
  (위 표)
74/74 passed
```

약 1.2 초 (대부분 `sleep 0.3` 과 종료 유예 300 ms). 6번 반복 모두 `74/74`, 다른 디렉터리(`/tmp`)에서 실행해도 같다 (데이터 파일 필요 없음, 임시 파일은 `/tmp/OS-B01_noexec_*` 하나를 만들고 지운다). ASan + UBSan (`-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer`) 도 `74/74`, 경고 0.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| exec 실패 뒤 `_exit` 없음 (`execvp(...); perror(...);` 로 끝) | 자식이 `spawn` 에서 **돌아와** 부모 코드를 계속 실행 → 셸이 둘, 명령이 두 번 실행. 하네스는 `getpid() != g_main_pid` 로 잡아 `_exit(120)` | `execvp(...); _exit(127);` |
| 자식에서 `exit()` | 복사된 stdio 버퍼 재출력, atexit 핸들러 중복 (`demo_exit_dup.c`: "hello" 두 번) | `_exit()` |
| 실패 코드를 아무거나 (`_exit(1)`) | 호출자가 "명령이 1 을 돌려줌" 과 "명령 없음" 을 구별 못 함 | `ENOENT` → 127, 그 외 → 126 |
| `WEXITSTATUS(st)` 만 봄 | 시그널로 죽으면 종료 코드 바이트는 0 → "성공" 으로 보임 (`kill -9` → 0!) | `WIFEXITED` 먼저, 아니면 `WIFSIGNALED` → 128+sig |
| `status >> 8` 직접 계산 | 인코딩은 구현 정의 | 매크로만 |
| SIGCHLD 하나에 `waitpid` 하나 | 알림은 합쳐진다 → 좀비 남음 (실측 50개 중 48개) | `while (waitpid(-1, &st, WNOHANG) > 0)` |
| 회수 루프 **뒤에** 플래그 내림 | 루프 끝과 내리기 사이에 끝난 자식의 알림이 지워져 다음 알림까지 좀비 | 플래그를 먼저 내리고 루프 |
| 백그라운드 회수에 `WNOHANG` 없이 `waitpid(-1, ..., 0)` | 아직 도는 작업이 있으면 셸이 멈춤 | `WNOHANG` |
| 부모가 막아 둔 시그널을 자식에 그대로 | 마스크·SIG_IGN 은 exec 후에도 유지 → TERM 에 안 죽는 자식 (테스트: 143 대신 7) | 자식에서 `sigprocmask(SIG_SETMASK, &empty, NULL)`, 무시하던 시그널은 `SIG_DFL` 로 |
| `cd` 를 fork 해서 실행 | 자식의 디렉터리만 바뀌고 셸은 그대로 | 내장 명령으로 셸에서 `chdir` |
| 종료 시 SIGKILL 만, 또는 TERM 만 | KILL 만: 서비스가 정리(플래시 쓰기 마무리) 못 함. TERM 만: 무시하는 프로세스가 영원히 남음 | TERM → 유예 → KILL → **회수** |
| `kill` 만 하고 `waitpid` 안 함 | 죽인 프로세스도 좀비로 남는다 | 죽였으면 회수까지 |

## 8. 면접 Q&A

<details>
<summary>Q1. fork 와 exec 를 왜 하나로 합치지 않았나요?</summary>

둘 사이에 **자식의 환경을 바꾸는 코드** 를 끼워 넣을 수 있기 때문이다 (OSTEP 5.4). 리다이렉션은 `fork → (자식) open + dup2 → exec`, 파이프는 `pipe → fork ×2 → dup2 → exec` (OS-B02). 시그널 마스크 초기화, `chdir`, `setsid`, 권한 낮추기(`setuid`)도 이 자리에서 한다. 새 프로그램은 아무것도 몰라도 바뀐 fd 0/1/2 를 그대로 쓴다. 꼬리: "단점은?" → 큰 프로세스의 fork 는 페이지 테이블 복사 비용이 있고(COW 라도), 멀티스레드 프로그램에서 fork 하면 **호출한 스레드만** 복제되어 다른 스레드가 쥐고 있던 락이 영원히 잠긴 채 남는다 — 그래서 fork 후 exec 전에는 async-signal-safe 함수만 써야 한다 (POSIX `fork`). 대안이 `posix_spawn` (연습 2).

</details>

<details>
<summary>Q2. 좀비와 고아의 차이는? 좀비는 kill -9 로 없앨 수 있나요?</summary>

좀비: **자식이** 끝났는데 부모가 아직 회수 안 함 — 프로세스 표 칸 + pid + status 만 남음 (ps `Z`, 이 맥에서 COMM `<defunct>`). 이미 죽었으니 `kill -9` 는 의미가 없다. 없애려면 부모가 `wait` 하거나 부모를 끝내서 pid 1 이 입양·회수하게 한다. 고아: **부모가** 먼저 끝난 살아 있는 프로세스 — pid 1(macOS launchd, 리눅스는 subreaper 또는 init)에게 입양되어 계속 돈다. 고아가 끝나면 새 부모가 회수한다. 꼬리: "좀비가 왜 위험?" → pid 와 프로세스 표 고갈 (`fork` 실패 `EAGAIN`). 장시간 도는 데몬이 자식을 회수하지 않는 버그가 전형적이다.

</details>

<details>
<summary>Q3. SIGCHLD 핸들러에서 왜 waitpid 를 루프로 돌리나요? WNOHANG 은 왜?</summary>

표준 시그널은 큐잉되지 않는다. 보류 비트가 서 있는 동안 같은 시그널이 또 오면 합쳐진다 → 자식 N 명이 끝나도 핸들러는 N 번보다 적게 돈다. 이 레슨 B1 은 막은 상태에서 3명 → 1번, 막지 않은 실측에서도 50명 → 43~48번이었다. 그래서 한 번 깨면 "끝난 것 전부" 를 거둬야 하고, 아직 도는 자식이 있을 수 있으니 `WNOHANG` 으로 막히지 않게 한다 (0 반환 = 아직 끝난 자식 없음, -1/`ECHILD` = 자식 없음). 꼬리: "실시간 시그널이면?" → `SIGRTMIN+n` 은 큐잉되지만 SIGCHLD 는 표준 시그널이다. 리눅스는 `signalfd`/`pidfd` (OS-M18), macOS 는 kqueue `EVFILT_PROC` + `NOTE_EXIT` 로 시그널 없이 알림을 받을 수 있다 (kqueue 이벤트 루프는 OS-A11).

</details>

<details>
<summary>Q4. exec 후 자식은 무엇을 물려받나요?</summary>

유지: pid, 부모 pid, 열린 fd (`FD_CLOEXEC` 가 붙은 것만 닫힘), 현재 디렉터리, umask, **시그널 마스크**, **보류 중인 시그널**, **SIG_IGN 설정**, 리소스 한도, 실제 uid/gid. 초기화: 주소 공간 전체(코드·힙·스택·mmap), **잡던 시그널의 핸들러 → SIG_DFL** (핸들러 함수 주소가 새 프로그램에는 없으니까), 스레드(호출 스레드 하나만 남음). 이 레슨의 실측: 부모가 SIGTERM 을 막은 채 실행한 `sh` 는 `kill -TERM $$` 에도 죽지 않고 `exit 7` 했다. 그래서 셸은 자식에서 마스크를 비우고 SIGINT/SIGQUIT 같은 무시 설정을 SIG_DFL 로 되돌린다. 꼬리: "fd 가 새면?" → 자식이 파이프 쓰기 끝을 들고 있어 EOF 가 안 오는 고전적 행(hang) — OS-B02, 그리고 `O_CLOEXEC` 습관.

</details>

<details>
<summary>Q5. 셸의 $? 가 137 이면 무슨 뜻이죠? 126, 127 은?</summary>

128 + 9 → SIGKILL 로 죽음 (리눅스면 OOM killer 를 먼저 의심, 컨테이너 메모리 한도). 139 = SIGSEGV, 143 = SIGTERM, 134 = SIGABRT (`assert`). 127 = 명령을 못 찾음 (PATH, 오타, 그리고 macOS 의 `/bin/true` 처럼 플랫폼 차이), 126 = 찾았지만 실행 불가 (실행 비트 없음 → `EACCES`). 경계 사례는 셸마다 다르다: `#!/no/such/interp` 스크립트를 이 맥에서 돌리면 `/bin/sh`(bash 3.2) 는 "bad interpreter" 로 **126**, zsh 는 **127**, 우리 msh 는 `execvp` 가 `ENOENT` 를 주므로 **127** 이었다. POSIX 는 "시그널이면 128 초과" 까지만 정하고 128+n 은 셸 관례다. 꼬리: "프로그램이 `exit(137)` 하면?" → `$?` 로는 구별 불가, `waitpid` 의 status 로는 `WIFEXITED` 라서 구별된다. 종료 코드는 8비트라 `exit 300` → 44.

</details>

<details>
<summary>Q6. fork 가 없는 시스템(MMU 없는 MCU, uClinux)에서는 어떻게 프로세스를 띄우나요?</summary>

fork 는 "주소 공간 복제" 이고 COW 로 싸게 만들려면 MMU(페이지 테이블)가 필요하다. MMU 없는 리눅스(NOMMU, 예: Cortex-M 의 uClinux)는 `fork` 를 지원하지 않고 `vfork` (자식이 exec/_exit 할 때까지 부모를 멈추고 **같은 메모리** 를 빌려 씀)나 `posix_spawn` 을 쓴다. `vfork` 자식은 `_exit` 나 exec 말고는 거의 아무것도 하면 안 된다 (부모 스택을 망가뜨림). RTOS 는 아예 복제 개념이 없고 `xTaskCreate(fn, name, stack, arg, prio, &h)` 처럼 함수 + 스택으로 새 태스크를 만든다 (RTOS-B02, RTOS-B07). 꼬리: "posix_spawn 의 장점?" → 복제가 없어 큰 프로세스에서도 싸고, macOS 에서는 커널 시스템 콜이라 exec 실패가 **부모의 반환값** 으로 온다 (연습 2: `-ENOENT`, `-EACCES` 실측).

</details>

<details>
<summary>Q7. 임베디드 리눅스의 init(pid 1)은 이 레슨의 어디와 같나요? pid 1 이 죽으면?</summary>

pid 1 은 ① 부팅 때 서비스들을 spawn (`/etc/inittab` 또는 systemd 유닛) ② **모든 고아를 입양해 회수** (SIGCHLD + `waitpid(-1, WNOHANG)` 루프 — Part B 그대로) ③ 정책에 따라 재시작 (`respawn`, `Restart=on-failure` — Part C) ④ 셧다운 때 TERM → KILL. 리눅스에서 pid 1 이 끝나면 커널 패닉("Attempted to kill init!")이다. 또 pid 1 에게는 핸들러를 설치하지 않은 시그널이 전달되지 않는다 — 기본 동작으로 죽지 않게 커널이 보호한다. 그래서 컨테이너 안에서 아무 프로그램을 pid 1 로 돌리면 SIGTERM 이 안 먹고 좀비가 쌓이는 문제가 생긴다 (`tini`). 부팅 흐름은 ELX-B01, rootfs 의 BusyBox init 은 ELX-B12, systemd 유닛·watchdog 은 ELX-B13.

</details>

## 9. 추가 연습

### 연습 1 — 제한 시간 실행 (`timeout` 명령의 핵심)

`int run_timeout(char *const argv[], int ms, bool *timed_out)` — ms 안에 안 끝나면 SIGKILL 하고 raw status 를 돌려준다. 좀비를 남기지 않아야 한다.

<details>
<summary>답 보기</summary>

spawn 후 1 ms 마다 `waitpid(pid, &st, WNOHANG)`. 끝났으면 그 status. 마감이 지나면 `kill(pid, SIGKILL)` 후 **반드시** `wait_status(pid)` 로 회수 — 죽인 프로세스도 좀비가 된다. 체크: `sleep 5` 를 100 ms 로 → `timed_out`, `WTERMSIG == 9`, `$? 137`; `sh -c 'exit 4'` 를 2 초로 → 4, 남은 자식 없음(`ECHILD`). 실물 `timeout(1)` (이 맥에도 `/usr/bin/timeout` 이 있다, GNU coreutils 판도 같음) 은 시간 초과 때 기본으로 SIGTERM 을 보내고 124 를 돌려준다 — 실측 `timeout 0.1 sleep 5; echo $?` → 124. 그리고 보낼 시그널은 자식에서 기본 동작으로 되돌린다고 man 페이지에 적혀 있다 (Q4 의 SIG_IGN 상속 문제를 막는 것). 펌웨어 검증 장비의 "도구가 멈추면 죽이고 FAIL" 이 이것이다.

</details>

### 연습 2 — `posix_spawnp` 로 바꾸기

`pid_t spawn_posix(char *const argv[])` — 성공 pid, 실패 `-errno`. fork 버전과 실패 보고 방식이 어떻게 다른가?

<details>
<summary>답 보기</summary>

```c
int rc = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
return rc == 0 ? pid : -rc;          /* 에러는 errno 가 아니라 반환값 */
```

이 맥 실측: 없는 명령 → `-ENOENT`, `/etc/hosts`(실행 비트 없음) → `-EACCES` — 자식 프로세스의 127/126 이 아니라 **호출 시점에 부모가** 안다. (POSIX 는 exec 실패를 반환값으로 줄 수도, 자식이 127 로 끝나게 할 수도 있다고 허용한다 — 구현마다 다르니 둘 다 처리하는 코드가 이식성 있다.) fd 조작은 `posix_spawn_file_actions_adddup2`, 마스크는 `posix_spawnattr_setsigmask` 로 fork–exec 사이의 일을 대신한다.

</details>

### 연습 3 — fork 개수 세기 (면접 단골)

`for (int i = 0; i < 3; i++) fork();` 뒤에 프로세스는 몇 개? `fork() && fork() || fork();` 는? 직접 세어서 확인하라.

<details>
<summary>답 보기</summary>

루프: 반복마다 살아 있는 모든 프로세스가 fork → 1, 2, 4, 8 → **2ⁿ** (n=3 → 8, n=4 → 16). 식: 원래 P 의 첫 fork → P(참)·C1(0, 거짓). P 는 `&&` 의 두 번째 fork → P(참, `||` 건너뜀)·C2(0 → `||` 의 세 번째 fork → C2·C3). C1 은 `&&` 단락 → `||` 의 세 번째 fork → C1·C4. 합계 P, C1, C2, C3, C4 = **5**. 검증법: 실험 전체를 별도 자식 안에서 돌리고, 모든 프로세스가 파이프에 1 바이트 쓴 뒤 **자기 자식을 전부 wait** 하고 끝난다. 부모는 EOF 까지 읽은 바이트를 센다 (poll 3 초 워치독). 결과 8, 16, 5.

</details>

### 연습 4 — `&&` / `||` 목록

`sh_list("false && no_such_cmd || sh -c 'exit 5'")` → 5. 실행한 명령 수도 돌려라. 따옴표 안의 `&&` 는 연산자가 아니다.

<details>
<summary>답 보기</summary>

왼쪽부터: 첫 명령은 실행. 연산자를 만날 때마다 `&&` 면 "직전 결과 코드가 0 일 때만", `||` 면 "0 이 아닐 때만" 다음 명령을 실행하고, 건너뛴 명령은 결과 코드를 바꾸지 않는다. `false && X || Y` → false(1) → X 건너뜀(코드 1 유지) → `||` 이므로 Y 실행 → 5, 실행 2개. `true || no_such_cmd` → 0, 실행 1개 (그래서 127 메시지가 안 나온다). 따옴표 안을 건너뛰며 연산자를 찾으므로 `sh -c 'exit 0 && exit 9' && true` 의 첫 `&&` 는 `sh` 에게 넘어간다 → 0, 실행 2개.

</details>

### 연습 5 — SIGCHLD 를 SIG_IGN 으로: 좀비 없는 대신 wait 불가

`sigaction(SIGCHLD, SIG_IGN)` 후 자식을 만들고 `waitpid(pid, &st, 0)` 하면? 자식은 좀비로 남나?

<details>
<summary>답 보기</summary>

POSIX(XSI): SIGCHLD 가 SIG_IGN 이거나 `SA_NOCLDWAIT` 면 끝난 자식은 좀비가 되지 않고 바로 사라진다. `wait/waitpid` 는 자식이 모두 끝날 때까지 막혔다가 `-1`/`ECHILD`. 이 맥 실측: `waitpid` → `-1`, `errno == ECHILD`, 그 뒤 `kill(pid, 0)` → `ESRCH` (좀비로도 없음). 종료 상태가 필요 없는 "던져 놓는" 데몬에 쓸 수 있지만, 셸·슈퍼바이저처럼 `$?` 가 필요하면 쓸 수 없다. 주의: SIG_IGN 은 exec 를 지나 상속되므로 이 상태로 자식을 띄우면 자식 프로그램의 `wait` 도 이상해진다 — 자식에서 SIG_DFL 로 되돌린다 (Q4).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 16/16 passed (약 0.3 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 16/16, 경고 0
```

## 10. 핵심 정리

- 프로세스 생성 = **fork**(복제, 한 번 불러 두 번 리턴: 부모 pid / 자식 0) + **exec**(교체, 성공하면 안 돌아옴) + **wait**(회수). 둘 사이가 리다이렉션·마스크 초기화 자리.
- exec 실패한 자식은 **`_exit(127)`**(못 찾음) / **`_exit(126)`**(실행 불가). `exit()` 는 복사된 stdio 버퍼를 또 내보낸다.
- status 는 매크로로: `WIFEXITED → WEXITSTATUS`(8비트, `exit 300` → 44), `WIFSIGNALED → WTERMSIG` → `$? = 128+sig` (137 KILL, 139 SEGV, 143 TERM).
- 시그널 마스크·SIG_IGN 은 exec 를 지나 상속된다 → 자식에서 비운다 (실측: 막힌 TERM → 143 대신 7).
- 좀비 = 끝났지만 회수 안 됨 (ps `Z`, `<defunct>`, `kill -9` 무의미). 고아 = 부모가 먼저 끝남 → pid 1(macOS launchd, 리눅스는 subreaper/init)이 입양·회수.
- SIGCHLD 는 쌓이지 않는다 (막고 3명 → 핸들러 1번, 실측 50명 → 43~48번). 회수는 **플래그 먼저 내리고 `waitpid(-1, &st, WNOHANG)` 루프**.
- `cd`·`exit` 는 셸 자신을 바꾸므로 내장 명령. 포그라운드는 `waitpid(pid)`, 백그라운드는 `waitpid(-1)`.
- init/슈퍼바이저 = spawn + 회수 루프 + 재시작 정책 + TERM→KILL. MCU 에는 fork 가 없다: 태스크 생성 + 워치독 + 리셋.

## 참고자료

- OSTEP 4장 *The Abstraction: The Process*, 5장 *Interlude: Process API* (fork/exec/wait, 셸, 리다이렉션): <https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-api.pdf> (로컬: `operating-system-three-easy-pieces/book-md/C05_interlude_process_api.md`, 한국어 노트 `notes/2026-09-30_C05_process_api.md`)
- POSIX `fork` — 호출 스레드만 복제, fork 후 exec 전 async-signal-safe 제한: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html>
- POSIX `exec` — 잡던 시그널은 SIG_DFL, SIG_IGN·마스크·보류 시그널 유지, FD_CLOEXEC: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/exec.html>
- POSIX `wait`/`waitpid` — `WNOHANG`, status 매크로, SIG_IGN/`SA_NOCLDWAIT` 일 때 ECHILD: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/wait.html>
- POSIX `waitid` — `WNOWAIT` (회수하지 않고 엿보기): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/waitid.html>
- POSIX `_exit` — 종료 코드는 `status & 0377`, 고아는 구현이 정한 시스템 프로세스에 입양: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/_exit.html>
- POSIX Shell Command Language 2.8.2 *Exit Status for Commands* (127 / 126 / 시그널 > 128): <https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_08_02>
- POSIX 2.4 *Signal Concepts* (보류·차단, 표준 시그널 비큐잉): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_04>
- POSIX `posix_spawn`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/posix_spawn.html>
- macOS 매뉴얼: `man 2 fork`, `man 2 execve`, `man 2 wait`, `man 2 sigaction`, `man 3 posix_spawn`; 헤더 `MacOSX26.4.sdk/usr/include/sys/wait.h` (status 비트 정의)
- Linux `prctl(2)` `PR_SET_CHILD_SUBREAPER`: <https://man7.org/linux/man-pages/man2/prctl.2.html>
- systemd.service — `Restart=`, `StartLimitBurst=`, `TimeoutStopSec=`: <https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html>
- BusyBox init `inittab` 예시 (`respawn`, `askfirst`): <https://git.busybox.net/busybox/tree/examples/inittab>
- Bryant & O'Hallaron, *CS:APP* 8장 (프로세스, 시그널, `tsh` 셸 랩)
- 관련 레슨: OS-B02 (파이프·리다이렉션·fd 상속), OS-B07 (시그널 핸들러 안전, self-pipe), OS-B08 (주소 공간), OS-M03 (COW fork), OS-M18 (pidfd·signalfd), OS-A02 (ptrace), OS-A11 (데몬, kqueue), ELX-B01 (부팅 → init), ELX-B12 (BusyBox rootfs), ELX-B13 (systemd 재시작·watchdog), RTOS-B02 (TCB·태스크 상태), RTOS-B07 (FreeRTOS 태스크 생성), BM-A09 (리셋 원인 기록), TOOL-M11 (코어 덤프)
