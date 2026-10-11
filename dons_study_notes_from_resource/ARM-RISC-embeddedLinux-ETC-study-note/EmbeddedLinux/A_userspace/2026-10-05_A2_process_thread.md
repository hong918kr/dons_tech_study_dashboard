# A2. 프로세스와 스레드 — fork·exec·wait, pthread, 그리고 커널에선 둘 다 `task_struct`

> **이 노트를 다 읽으면**: fork/exec/wait로 자식 프로세스를 띄우고 종료 이유를 해석할 수 있다 · 좀비·고아·D 상태를 `ps`와 `/proc`로 찾아낼 수 있다 · PID/TGID/TID와 pthread의 공유 범위를 정확히 말할 수 있다 · nice·SCHED_FIFO·CPU affinity·context switch 카운터를 직접 만져 볼 수 있다
> **왜 필요한가**: "process vs thread 차이?", "zombie가 뭐고 어떻게 없애나?", "fork는 왜 빠른가(COW)?", "D state 프로세스는 왜 kill이 안 되나?" — Embedded Linux 1차 면접의 단골이고, 실무에선 센서 데몬·워치독·런처 코드가 전부 이 API 위에 있다
> **Don 기준 난이도**: RTOS 태스크·스택·컨텍스트 스위치·우선순위는 이미 안다 / 주소 공간 분리, fork의 COW, 좀비 회수, Linux가 프로세스와 스레드를 같은 `task_struct`로 다룬다는 점이 새롭다
> **선행 노트**: A1

---

## 0. 큰 그림 — RTOS 태스크, 프로세스, 스레드

Don이 SSD 펌웨어에서 쓰던 RTOS를 떠올려 보자. 태스크마다 스택과 TCB(Task Control Block)가 있고, 스케줄러가 우선순위대로 TCB를 골라 레지스터를 복원한다. 그런데 **모든 태스크가 같은 주소 공간**을 본다. 태스크 A가 포인터를 잘못 써서 태스크 B의 전역 변수를 덮어써도 아무도 막지 않는다 (MPU를 켜서 영역을 나누지 않았다면).

Linux는 이 그림에 두 가지를 얹는다.

- **프로세스(process)** = 자기만의 가상 주소 공간(page table)을 가진 실행 단위. MMU가 다른 프로세스의 메모리를 아예 "안 보이게" 한다. RTOS 태스크를 MPU로 격리한 것보다 훨씬 강한 격리다. 파일 디스크립터 테이블, 시그널 핸들러, 현재 디렉터리 같은 "자원 묶음"도 프로세스 단위로 따로 가진다.
- **스레드(thread)** = 한 프로세스 안에서 주소 공간과 자원을 **공유**하며 따로 스케줄되는 실행 흐름. 스택·레지스터·TID만 각자 가진다. RTOS 태스크와 가장 닮은 것이 바로 이 스레드다.

그리고 Linux의 핵심 반전: 커널 안에서는 **프로세스도 스레드도 같은 구조체 `task_struct`** 하나로 표현된다. 커널에게 스케줄 단위는 그냥 "task"이고, 두 task가 주소 공간(`mm_struct`)을 공유하면 우리가 스레드라고 부르고, 공유하지 않으면 프로세스라고 부를 뿐이다. 이 관점을 8절에서 `clone()` 플래그로 확인한다.

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<text x="165" y="22" text-anchor="middle" font-size="14">프로세스 2개 (fork)</text>
<text x="495" y="22" text-anchor="middle" font-size="14">프로세스 1개 + 스레드 3개</text>
<rect x="15" y="35" width="140" height="240" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="85" y="56" text-anchor="middle" font-size="13">PID 100</text>
<rect x="175" y="35" width="140" height="240" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="245" y="56" text-anchor="middle" font-size="13">PID 200</text>
<rect x="25" y="68" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="85" y="86" text-anchor="middle" font-size="12">code</text>
<rect x="25" y="98" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="85" y="116" text-anchor="middle" font-size="12">data / bss</text>
<rect x="25" y="128" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="85" y="146" text-anchor="middle" font-size="12">heap</text>
<rect x="25" y="158" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="85" y="176" text-anchor="middle" font-size="12">stack</text>
<rect x="25" y="188" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="85" y="206" text-anchor="middle" font-size="12">fd table</text>
<rect x="25" y="218" width="120" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="85" y="236" text-anchor="middle" font-size="12">page table A</text>
<text x="85" y="254" text-anchor="middle" font-size="12">(mm_struct A)</text>
<rect x="185" y="68" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="245" y="86" text-anchor="middle" font-size="12">code</text>
<rect x="185" y="98" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="245" y="116" text-anchor="middle" font-size="12">data / bss</text>
<rect x="185" y="128" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="245" y="146" text-anchor="middle" font-size="12">heap</text>
<rect x="185" y="158" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="245" y="176" text-anchor="middle" font-size="12">stack</text>
<rect x="185" y="188" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="245" y="206" text-anchor="middle" font-size="12">fd table</text>
<rect x="185" y="218" width="120" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="245" y="236" text-anchor="middle" font-size="12">page table B</text>
<text x="245" y="254" text-anchor="middle" font-size="12">(mm_struct B)</text>
<rect x="345" y="35" width="300" height="240" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="495" y="56" text-anchor="middle" font-size="13">PID(TGID) 300 — 공유 영역</text>
<rect x="360" y="68" width="270" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="495" y="86" text-anchor="middle" font-size="12">code · data / bss · heap</text>
<rect x="360" y="98" width="270" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="495" y="116" text-anchor="middle" font-size="12">fd table · signal handlers · cwd</text>
<rect x="360" y="128" width="270" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="495" y="146" text-anchor="middle" font-size="12">page table 1개 (mm_struct 공유)</text>
<rect x="360" y="170" width="82" height="92" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="401" y="192" text-anchor="middle" font-size="12">TID 300</text>
<text x="401" y="212" text-anchor="middle" font-size="12">stack</text>
<text x="401" y="230" text-anchor="middle" font-size="12">regs</text>
<text x="401" y="248" text-anchor="middle" font-size="12">errno</text>
<rect x="454" y="170" width="82" height="92" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="495" y="192" text-anchor="middle" font-size="12">TID 301</text>
<text x="495" y="212" text-anchor="middle" font-size="12">stack</text>
<text x="495" y="230" text-anchor="middle" font-size="12">regs</text>
<text x="495" y="248" text-anchor="middle" font-size="12">errno</text>
<rect x="548" y="170" width="82" height="92" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="589" y="192" text-anchor="middle" font-size="12">TID 302</text>
<text x="589" y="212" text-anchor="middle" font-size="12">stack</text>
<text x="589" y="230" text-anchor="middle" font-size="12">regs</text>
<text x="589" y="248" text-anchor="middle" font-size="12">errno</text>
<text x="165" y="300" text-anchor="middle" font-size="13">주소 공간이 완전히 분리된다</text>
<text x="165" y="320" text-anchor="middle" font-size="12">(MPU로 격리한 RTOS 태스크보다 강함)</text>
<text x="495" y="300" text-anchor="middle" font-size="13">주소 공간을 공유, 스택·레지스터만 각자</text>
<text x="495" y="320" text-anchor="middle" font-size="12">(RTOS 태스크와 가장 닮은 모습)</text>
</svg>
```

그림 1 — 왼쪽은 fork로 만든 두 프로세스(각자 page table), 오른쪽은 한 프로세스 안의 스레드 3개(page table 하나를 공유하고 스택·레지스터·errno만 각자).

| 항목 | RTOS 태스크 (FreeRTOS 등) | Linux 프로세스 | Linux 스레드 (pthread) |
|---|---|---|---|
| 주소 공간 | 전 태스크 공유 (MPU 선택) | 프로세스마다 따로 (MMU) | 같은 프로세스 안에서 공유 |
| 만드는 API | `xTaskCreate` | `fork()` (+ `exec`) | `pthread_create()` |
| 스택 | 생성 시 크기 지정 | 커널이 자동 확장 (최대 `ulimit -s`) | 기본 8 MiB 가상 (실사용분만 물리 할당) |
| 제어 블록 | TCB | `task_struct` | `task_struct` (같은 구조체!) |
| 한쪽이 죽으면 | 보통 시스템 전체가 위험 | 그 프로세스만 죽는다 | 프로세스 전체가 죽는다 (SIGSEGV 등) |
| 통신 | 전역 변수, 큐 | IPC 필요 (pipe, socket, shm — A7) | 전역 변수 + 락 (A8) |
| 생성 비용 | 작다 (TCB·스택 할당뿐) | 수십~수백 us (10절 측정) | 수십 us (10절 측정) |

센서 키트에 대입해 보면 이렇다. 카메라 파이프라인·GNSS 파서·CAN 모터 제어를 **각각 별도 프로세스**로 돌리면 GNSS 파서가 segfault 나도 모터 제어는 산다(systemd가 파서만 재시작). 반대로 IMU 1 kHz 읽기 스레드와 필터 스레드는 같은 버퍼를 공유해야 하니 **한 프로세스 안의 스레드**로 둔다. "격리가 필요하면 프로세스, 공유가 필요하면 스레드"가 첫 번째 설계 기준이다.

이 노트의 실습 환경: Lima Ubuntu VM `elinux` (aarch64, 4 vCPU). `uname -r` 출력은 `7.0.0-34-generic`, 페이지 크기 4096, `CONFIG_HZ=1000`, `CONFIG_PREEMPT_DYNAMIC=y`(현재 lazy 선점 모드). 모든 예제는 `gcc -std=gnu11 -Wall -Wextra -O2`(스레드 예제는 `-pthread`)로 경고 0개로 빌드했다.

---

## 1. 프로세스 만들기 — `fork()`

### 1.1 직관: "나를 통째로 복제하고, 반환값으로 누가 누군지 구분"

`fork()`는 호출한 프로세스를 거의 그대로 복제한다. 코드·데이터·힙·스택·열린 fd·시그널 핸들러가 전부 복사된 **두 번째 프로세스**가 생기고, 두 프로세스 모두 `fork()` 다음 줄부터 실행을 이어 간다. 구분하는 방법은 반환값 하나뿐이다.

- 부모에게는 자식의 PID(양수)를 돌려준다.
- 자식에게는 0을 돌려준다.
- 실패하면 부모에게 -1 (자식은 안 생김). 흔한 원인: `EAGAIN`(프로세스 수 제한 `RLIMIT_NPROC`), `ENOMEM`.

bare-metal 비유로는 "현재 태스크의 TCB와 스택을 memcpy해서 새 태스크로 등록하고, 새 태스크의 r0(반환 레지스터)만 0으로 바꿔 둔 것"이다. 실제로 커널은 자식의 저장된 레지스터 중 반환값 레지스터(arm64는 x0)를 0으로 만들어 둔다.

### 1.2 부모의 의무: `wait()`로 회수하기

자식이 끝나면 커널은 자식의 대부분 자원(메모리, fd)을 해제하지만 **종료 상태(exit status)와 자원 사용량을 담은 `task_struct`는 남겨 둔다**. 부모가 `wait()`/`waitpid()`로 그 값을 읽어 갈 때까지다. 이것이 4절의 좀비다.

`waitpid(pid, &status, options)`의 인자:

- `pid > 0`: 그 자식만, `pid == -1`: 아무 자식이나, `pid == 0`: 같은 프로세스 그룹의 자식.
- `options`: `WNOHANG`(끝난 자식이 없으면 바로 0 반환 — 폴링용), `WUNTRACED`(정지된 자식도 보고), `WCONTINUED`.
- 더 기다릴 자식이 없으면 -1 + `errno = ECHILD`.

`status`는 정수 하나에 여러 정보가 비트로 packed 되어 있어서 매크로로 풀어야 한다.

| 매크로 | 참이면 | 짝 매크로 |
|---|---|---|
| `WIFEXITED(st)` | `exit()`/`return`으로 정상 종료 | `WEXITSTATUS(st)` → 종료 코드 하위 8비트 (0~255) |
| `WIFSIGNALED(st)` | 시그널로 죽음 | `WTERMSIG(st)` → 시그널 번호, `WCOREDUMP(st)` → core 생성 여부 |
| `WIFSTOPPED(st)` | 정지됨 (`WUNTRACED`일 때만 보고) | `WSTOPSIG(st)` |
| `WIFCONTINUED(st)` | SIGCONT로 재개 (`WCONTINUED`일 때만) | 없음 |

### 1.3 예제 1 — fork 3번, 자식마다 다른 방식으로 죽이고 status 해석하기

정상 종료(42), segfault, SIGTERM으로 죽은 자식 세 개를 만들고 부모가 `waitpid(-1, ...)` 루프로 전부 회수하며 이유를 출력한다.

```c
// ex1: fork 3 children, decode how each one ended
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static void decode(pid_t pid, int st) {
    if (WIFEXITED(st))
        printf("child %d: exited, status=%d\n", pid, WEXITSTATUS(st));
    else if (WIFSIGNALED(st))
        printf("child %d: killed by signal %d (%s)%s\n", pid, WTERMSIG(st),
               strsignal(WTERMSIG(st)), WCOREDUMP(st) ? " core" : "");
    else if (WIFSTOPPED(st))
        printf("child %d: stopped by signal %d\n", pid, WSTOPSIG(st));
}

int main(void) {
    printf("parent pid=%d\n", getpid());
    for (int i = 0; i < 3; i++) {
        fflush(stdout);                 /* flush before fork! */
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(1); }
        if (pid == 0) {                 /* child */
            if (i == 0) exit(42);       /* normal exit */
            if (i == 1) { int *p = NULL; return *(volatile int *)p; } /* SIGSEGV */
            pause();                    /* i == 2: wait to be killed */
            exit(0);
        }
        printf("forked child %d (i=%d)\n", pid, i);
        if (i == 2) { usleep(100000); kill(pid, SIGTERM); }
    }
    int st; pid_t pid;
    while ((pid = waitpid(-1, &st, 0)) > 0)
        decode(pid, st);
    perror("waitpid (expected ECHILD)");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex1_wait.c -o /tmp/A2/ex1_wait && cd /tmp/A2 && ./ex1_wait'
```

```text
parent pid=31689
forked child 31690 (i=0)
forked child 31691 (i=1)
forked child 31692 (i=2)
child 31690: exited, status=42
child 31691: killed by signal 11 (Segmentation fault)
child 31692: killed by signal 15 (Terminated)
waitpid (expected ECHILD): No child processes
```

출력에서 볼 것: 같은 `status` 정수가 매크로에 따라 "종료 코드 42", "시그널 11", "시그널 15"로 해석된다. 마지막 `ECHILD`는 "회수할 자식이 더 없다"는 정상 종료 조건이다. PID는 실행마다 다르다. segfault 난 자식의 `core` 표시가 없는 것은 VM의 core 크기 제한(`ulimit -c` 0) 때문이다.

셸의 `$?`도 같은 규칙을 따른다. 셸은 시그널로 죽은 자식을 `128 + 시그널 번호`로 보여 준다(SIGSEGV면 139). 그래서 CI 로그에서 "exit code 139"를 보면 바로 "segfault"라고 읽으면 된다.

### 1.4 함정 — fork 전에 stdout을 flush하지 않으면 출력이 두 번 나온다

예제 1에는 `fork()` 바로 앞에 `fflush(stdout)`가 있다. 이걸 빼면(파일 `ex1b_dupbuf.c`, 나머지는 동일) 어떻게 되는지 보자. stdout을 파이프나 파일로 돌리면 차이가 드러난다.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && ./ex1b_dupbuf | cat'
```

```text
parent pid=31695
parent pid=31695
forked child 31697 (i=0)
forked child 31698 (i=1)
forked child 31699 (i=2)
child 31697: exited, status=42
child 31698: killed by signal 11 (Segmentation fault)
child 31699: killed by signal 15 (Terminated)
waitpid (expected ECHILD): No child processes
```

출력에서 볼 것: `parent pid=` 줄이 두 번 찍혔다. 이유는 stdio 버퍼다. 출력이 터미널이 아니면 glibc는 stdout을 **full buffering**(4 KiB 정도 모아서 한 번에 `write`)한다. 아직 `write`되지 않은 "parent pid=..." 문자열이 유저 메모리 버퍼에 있는 채로 fork하면 그 버퍼도 자식에게 복사된다. `exit(42)`한 자식은 종료하면서 버퍼를 flush하므로 같은 줄이 한 번 더 나온다(시그널로 죽은 자식들은 flush하지 못해서 중복이 없다). 규칙: **fork 전에 `fflush(stdout)`, 자식에서 exec 실패 시엔 `exit()`가 아니라 `_exit()`**. `_exit()`는 stdio 버퍼를 flush하지 않고 `atexit` 핸들러도 돌리지 않는 "커널에 바로 종료 요청"이다.

### 1.5 strace로 보면 fork는 `clone()`이다

glibc의 `fork()`는 실제로 `clone` syscall을 부른다. 다음 절의 exec 예제(예제 3)를 strace로 보면 확인된다 (긴 출력 중 앞부분만 남기고 생략했다).

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && strace -f -e trace=clone,clone3,execve,wait4,exit_group ./ex3_exec 2>&1 | head -16'
```

```text
execve("./ex3_exec", ["./ex3_exec"], 0xffffde338ed8 /* 19 vars */) = 0
parent pid=187491
clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLDstrace: Process 187492 attached
, child_tidptr=0xe660629c0030) = 187492
[pid 187491] wait4(187492child  pid=187492, about to exec
 <unfinished ...>
[pid 187492] execve("/usr/local/sbin/sh", ["sh", "-c", "echo \"in exec'd sh: pid=$$ ppid="...], 0xffffc858d0b8 /* 19 vars */) = -1 ENOENT (No such file or directory)
[pid 187492] execve("/usr/local/bin/sh", ["sh", "-c", "echo \"in exec'd sh: pid=$$ ppid="...], 0xffffc858d0b8 /* 19 vars */) = -1 ENOENT (No such file or directory)
[pid 187492] execve("/usr/sbin/sh", ["sh", "-c", "echo \"in exec'd sh: pid=$$ ppid="...], 0xffffc858d0b8 /* 19 vars */) = -1 ENOENT (No such file or directory)
[pid 187492] execve("/usr/bin/sh", ["sh", "-c", "echo \"in exec'd sh: pid=$$ ppid="...], 0xffffc858d0b8 /* 19 vars */) = 0
in exec'd sh: pid=187492 ppid=187491
[pid 187492] exit_group(7)              = ?
[pid 187492] +++ exited with 7 +++
<... wait4 resumed>, [{WIFEXITED(s) && WEXITSTATUS(s) == 7}], 0, NULL) = 187492
--- SIGCHLD {si_signo=SIGCHLD, si_code=CLD_EXITED, si_pid=187492, si_uid=501, si_status=7, si_utime=0, si_stime=0} ---
...
```

출력에서 볼 것: (1) `fork()`는 `clone(flags=...|SIGCHLD)`로 바뀐다. 공유 플래그(`CLONE_VM` 등)가 하나도 없다 = "전부 복사". `SIGCHLD`는 "자식이 죽으면 부모에게 SIGCHLD를 보내라"는 뜻이다. (2) `wait()`는 `wait4` syscall이다. (3) 자식 종료 후 부모에게 실제로 SIGCHLD가 배달된다. (4) `execvp`는 PATH를 앞에서부터 하나씩 `execve`로 시도한다 — ENOENT가 3번 난 뒤 `/usr/bin/sh`에서 성공한다.

---

## 2. Copy-on-Write — fork가 빠른 이유

### 2.1 직관

1 GiB를 쓰는 프로세스가 fork할 때 1 GiB를 정말 복사한다면 fork 한 번에 수백 ms가 걸릴 것이다. Linux는 **page table만 복사하고, 데이터 페이지는 공유**한다. 대신 부모·자식 양쪽의 해당 PTE를 **읽기 전용(RO)**으로 바꿔 둔다. 어느 쪽이든 그 페이지에 **쓰는 순간** MMU가 permission fault를 내고, 커널의 page fault handler가 "아, COW 페이지구나" 하고 그때서야 4 KiB 한 페이지를 복사해서 쓴 쪽의 PTE를 새 페이지(RW)로 바꾼다. 이것이 copy-on-write(COW)다.

Don의 펌웨어 언어로는: "MMU abort가 났는데 에러 처리가 아니라 정상 경로로, 페이지를 하나 복사하고 PTE를 고친 뒤 같은 명령을 재실행한다". page fault의 자세한 경로는 A5에서 다룬다.

```svg
<svg viewBox="0 0 660 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="160" y="22" text-anchor="middle" font-size="14">(a) fork 직후</text>
<text x="500" y="22" text-anchor="middle" font-size="14">(b) 자식이 페이지에 쓴 뒤</text>
<line x1="330" y1="35" x2="330" y2="270" stroke="currentColor" stroke-opacity="0.3" stroke-dasharray="4 4"/>
<rect x="15" y="50" width="130" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="80" y="69" text-anchor="middle" font-size="13">부모 PTE</text>
<text x="80" y="86" text-anchor="middle" font-size="12">VA 0x...304 RO</text>
<rect x="15" y="160" width="130" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="80" y="179" text-anchor="middle" font-size="13">자식 PTE</text>
<text x="80" y="196" text-anchor="middle" font-size="12">VA 0x...304 RO</text>
<rect x="200" y="105" width="115" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="257" y="124" text-anchor="middle" font-size="13">물리 페이지 P</text>
<text x="257" y="141" text-anchor="middle" font-size="12">x=100 (공유)</text>
<line x1="145" y1="72" x2="198" y2="118" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="145" y1="182" x2="198" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="160" y="240" text-anchor="middle" font-size="12">PTE만 복사, 데이터는 한 벌</text>
<text x="160" y="258" text-anchor="middle" font-size="12">양쪽 다 쓰기 금지로 표시</text>
<rect x="345" y="50" width="130" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="410" y="69" text-anchor="middle" font-size="13">부모 PTE</text>
<text x="410" y="86" text-anchor="middle" font-size="12">VA 0x...304 RW</text>
<rect x="345" y="160" width="130" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="410" y="179" text-anchor="middle" font-size="13">자식 PTE</text>
<text x="410" y="196" text-anchor="middle" font-size="12">VA 0x...304 RW</text>
<rect x="530" y="50" width="115" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="587" y="69" text-anchor="middle" font-size="13">물리 페이지 P</text>
<text x="587" y="86" text-anchor="middle" font-size="12">x=100</text>
<rect x="530" y="160" width="115" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="587" y="179" text-anchor="middle" font-size="13">새 페이지 P'</text>
<text x="587" y="196" text-anchor="middle" font-size="12">x=999 (복사본)</text>
<line x1="475" y1="72" x2="528" y2="72" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="475" y1="182" x2="528" y2="182" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="500" y="240" text-anchor="middle" font-size="12">자식의 write → page fault → 4 KiB 복사</text>
<text x="500" y="258" text-anchor="middle" font-size="12">같은 가상 주소, 다른 물리 페이지</text>
</svg>
```

그림 2 — fork 직후에는 부모·자식 PTE가 같은 물리 페이지를 읽기 전용으로 가리킨다. 한쪽이 쓰면 그 페이지만 복사되고 둘은 갈라진다.

### 2.2 예제 2 — 같은 주소, 다른 값, 그리고 페이지마다 한 번의 fault

64 MiB 버퍼를 부모가 다 채운 뒤 fork한다. 자식은 변수 `x`와 버퍼 절반(8192 페이지)에 쓰고, 그동안 늘어난 minor page fault 수를 `getrusage`로 센다. minor fault란 디스크 I/O 없이 메모리 안에서 해결된 page fault다.

```c
// ex2: copy-on-write — same virtual address, different physical page after write
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>

#define SZ (64UL << 20)   /* 64 MiB */

static long minflt(void) {
    struct rusage ru; getrusage(RUSAGE_SELF, &ru); return ru.ru_minflt;
}

int main(void) {
    char *buf = malloc(SZ);
    if (!buf) { perror("malloc"); exit(1); }
    memset(buf, 'A', SZ);               /* touch every page in parent */
    int x = 100;

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid == 0) {
        printf("child : &x=%p x=%d\n", (void *)&x, x);
        x = 999;
        long f0 = minflt();
        for (size_t i = 0; i < SZ / 2; i += 4096) buf[i] = 'B'; /* write half */
        long f1 = minflt();
        printf("child : x=%d after write; wrote %lu pages, minor faults +%ld\n",
               x, SZ / 2 / 4096, f1 - f0);
        fflush(stdout);
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    printf("parent: &x=%p x=%d buf[0]=%c\n", (void *)&x, x, buf[0]);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex2_cow.c -o ex2_cow && ./ex2_cow'
```

```text
child : &x=0xffffc8cb1204 x=100
child : x=999 after write; wrote 8192 pages, minor faults +8192
parent: &x=0xffffc8cb1204 x=100 buf[0]=A
```

출력에서 볼 것: (1) `&x`가 부모·자식에서 **같은 가상 주소**인데 값은 자식 999, 부모 100이다 — 가상 주소가 같아도 물리 페이지가 다르다. (2) 8192 페이지에 쓰자 minor fault가 정확히 8192번 늘었다. 페이지 하나당 COW fault 한 번이다. 주소 값은 ASLR 때문에 실행마다 다르다.

### 2.3 함정과 실무 포인트

- **fork 비용은 RSS에 비례한다.** 데이터는 안 복사해도 page table은 복사하니까. 10절에서 부모 RSS 0 → 1024 MiB일 때 fork가 73 → 299 us로 늘어나는 것을 직접 잰다. 큰 버퍼를 잡은 카메라 데몬에서 `system()`(내부적으로 fork+exec)을 자주 부르면 의외로 느려진다.
- **fork 후 부모가 계속 쓰면 메모리가 두 배가 될 수 있다.** 자식이 오래 살면서 부모가 버퍼를 갱신하면 COW로 페이지가 하나씩 복사되어 결국 두 벌이 된다. 메모리가 빡빡한 보드에서 OOM의 원인이 된다.
- **멀티스레드 프로세스에서 fork하면 자식에는 fork를 부른 스레드 하나만 있다.** 다른 스레드가 잡고 있던 mutex는 잠긴 채로 복사되어 자식에서 영원히 풀리지 않는다. 그래서 POSIX는 "멀티스레드 프로그램에서 fork한 자식은 exec 전까지 async-signal-safe 함수만 쓰라"고 한다. 실무 규칙: 스레드가 있는 프로그램에서는 fork 직후 바로 exec하거나 `posix_spawn`을 쓴다.

---

## 3. 새 프로그램 실행하기 — `exec` 패밀리

### 3.1 직관: "프로세스는 그대로, 안에 든 프로그램만 갈아 끼운다"

`execve(path, argv, envp)`는 현재 프로세스의 주소 공간을 버리고 새 ELF 파일을 적재해서 그 `main`부터 실행한다. **PID는 그대로**이고 성공하면 **절대 반환하지 않는다**(돌아올 코드가 사라졌으니까). 반환했다면 실패한 것이다.

펌웨어 비유: 부트로더가 같은 CPU·같은 태스크 슬롯에서 새 이미지를 로드하고 엔트리 포인트로 점프하는 것. 셸이 명령을 실행하는 방법이 바로 "fork → 자식에서 exec → 부모는 wait"이다.

### 3.2 이름 규칙 — 글자 하나씩 뜻이 있다

커널 syscall은 `execve` 하나(그리고 fd 기반 `execveat`)이고 나머지는 glibc 래퍼다.

| 함수 | 인자 전달 | PATH 검색 | 환경 변수 |
|---|---|---|---|
| `execl` | 가변 인자 목록 (l = list) | 아니오 | 현재 `environ` |
| `execlp` | 목록 | 예 (p = PATH) | 현재 `environ` |
| `execle` | 목록 | 아니오 | 직접 지정 (e = env) |
| `execv` | 배열 (v = vector) | 아니오 | 현재 `environ` |
| `execvp` | 배열 | 예 | 현재 `environ` |
| `execvpe` | 배열 | 예 | 직접 지정 (GNU 확장) |
| `execve` | 배열 | 아니오 | 직접 지정 (진짜 syscall) |
| `fexecve` | 배열 | 해당 없음 (fd로 지정) | 직접 지정 |

`argv[0]`은 관례상 프로그램 이름이고, 목록과 배열 모두 마지막에 `NULL`이 있어야 한다. 목록형에서는 `(char *)NULL`로 캐스팅해서 넘긴다(가변 인자에서 0이 int로 넘어가는 문제 방지).

### 3.3 exec 후에 남는 것과 사라지는 것

| 남는 것 | 사라지거나 초기화되는 것 |
|---|---|
| PID, PPID, 프로세스 그룹, 세션 | 코드·데이터·힙·스택 (주소 공간 전체) |
| 열린 fd (`O_CLOEXEC`/`FD_CLOEXEC`가 없는 것) | `O_CLOEXEC` fd |
| 현재 디렉터리, umask, rlimit | 시그널 핸들러 (기본 동작으로 리셋, 무시 설정은 유지) |
| 실 UID/GID, nice 값, CPU affinity, 스케줄링 정책 | 다른 스레드들 (exec하면 스레드 하나만 남음) |
| 시그널 마스크, 대기 중 시그널 | mmap 영역, 공유 메모리 attach, atexit 핸들러 |

"fd가 exec 너머로 새는 것"은 실무 버그의 단골이다. 예를 들어 센서 데몬이 `/dev/spidev0.0`을 열어 둔 채 `system("ip link ...")`을 부르면 그 fd가 `ip` 프로세스에도 열려 있다. 그래서 요즘 코드는 `open(..., O_CLOEXEC)`를 기본으로 쓴다 (A3에서 다시).

### 3.4 예제 3 — fork + exec: PID가 유지되는 것과 exec 실패 처리

```c
// ex3: fork + exec — PID stays, program image is replaced
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    printf("parent pid=%d\n", getpid());
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid == 0) {
        printf("child  pid=%d, about to exec\n", getpid());
        fflush(stdout);
        /* sh -c prints its own PID ($$) and parent PID ($PPID) */
        char *argv[] = { "sh", "-c", "echo \"in exec'd sh: pid=$$ ppid=$PPID\"; exit 7", NULL };
        execvp("sh", argv);            /* returns only on error */
        perror("execvp");
        _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0) { perror("waitpid"); exit(1); }
    printf("parent: child exit status=%d\n", WEXITSTATUS(st));

    /* exec failure path: a program that doesn't exist */
    pid = fork();
    if (pid == 0) {
        execlp("no_such_prog", "no_such_prog", (char *)NULL);
        perror("execlp");
        _exit(127);                    /* shell convention: 127 = not found */
    }
    waitpid(pid, &st, 0);
    printf("parent: second child exit status=%d\n", WEXITSTATUS(st));
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex3_exec.c -o ex3_exec && ./ex3_exec'
```

```text
parent pid=33323
child  pid=33324, about to exec
in exec'd sh: pid=33324 ppid=33323
execlp: No such file or directory
parent: child exit status=7
parent: second child exit status=127
```

출력에서 볼 것: exec된 `sh`가 출력한 `$$`(33324)가 exec 전 자식의 PID와 같다 — 프로그램은 바뀌었지만 프로세스는 그대로다. 부모는 `sh`의 `exit 7`을 status로 받는다. 없는 프로그램을 exec하면 `execlp`가 **반환**하고, 관례대로 `_exit(127)`("command not found"와 같은 코드)로 끝낸다.

### 3.5 `posix_spawn` — fork+exec를 한 번에

`posix_spawn()`은 "자식을 만들어 바로 다른 프로그램을 실행"하는 표준 API다. glibc는 내부에서 `clone(CLONE_VM|CLONE_VFORK)`를 써서 부모의 page table을 복사하지 않는다. 그래서 부모 RSS가 커도 비용이 늘지 않는다(10절 측정). 자식에서 fd 재배치(`posix_spawn_file_actions_adddup2`)나 시그널 마스크 설정 정도는 할 수 있다. 큰 메모리를 쥔 데몬이 외부 툴을 실행해야 한다면 `system()` 대신 `posix_spawn`이 정답이다.

`vfork()`는 그 원조다. 자식이 부모의 주소 공간을 **그대로 빌려 쓰고**(복사도 COW도 없음) 부모는 자식이 exec하거나 `_exit`할 때까지 멈춘다. 자식이 변수를 바꾸거나 `return`하면 부모 스택이 망가지므로 "exec 아니면 `_exit`"만 허용된다. 직접 쓰기보다는 `posix_spawn`에 맡기는 것이 안전하다.

---

## 4. 종료, 좀비, 고아

### 4.1 종료 경로 세 가지

- `return` from `main` → `exit(code)`와 같다.
- `exit(code)`: `atexit` 핸들러 실행 → stdio 버퍼 flush → `_exit(code)`.
- `_exit(code)` (또는 `_Exit`): 곧장 커널의 `exit_group` syscall. 프로세스의 **모든 스레드**를 끝낸다.

종료 코드는 하위 8비트만 전달된다. `exit(256)`은 부모에게 0으로 보인다. 관례: 0 = 성공, 1 = 일반 오류, 2 = 사용법 오류, 126 = 실행 권한 없음, 127 = 명령 없음, 128+N = 시그널 N으로 죽음(셸 표기).

### 4.2 좀비(zombie) — 죽었는데 회수되지 않은 자식

자식이 종료하면 커널은 메모리·fd를 정리하고 부모에게 SIGCHLD를 보낸다. 그러나 `task_struct`(PID, 종료 코드, CPU 사용 시간)는 **부모가 wait할 때까지** 남긴다. 이 상태가 `Z`(zombie), `ps`에서 `<defunct>`다. 좀비는 메모리를 거의 안 먹지만 **PID를 하나 차지**한다. 부모가 영원히 wait하지 않으면 PID가 계속 쌓여 결국 `fork()`가 `EAGAIN`으로 실패한다.

좀비는 이미 죽었으므로 `kill -9`로 없앨 수 없다. 없애는 방법은 (1) 부모가 wait하게 고치거나 (2) 부모를 죽여서 좀비를 init에 넘기는 것(init이 회수한다)이다.

```svg
<svg viewBox="0 0 660 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="64" font-size="13">부모</text>
<text x="20" y="164" font-size="13">자식</text>
<line x1="60" y1="60" x2="640" y2="60" stroke="#4a7bd0" stroke-width="3"/>
<line x1="110" y1="60" x2="110" y2="156" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="110" y="45" text-anchor="middle" font-size="12">fork()</text>
<line x1="110" y1="160" x2="400" y2="160" stroke="#e08a3c" stroke-width="3"/>
<line x1="220" y1="150" x2="220" y2="170" stroke="currentColor"/>
<text x="220" y="190" text-anchor="middle" font-size="12">exec()</text>
<text x="165" y="150" text-anchor="middle" font-size="12">자식 코드</text>
<text x="310" y="150" text-anchor="middle" font-size="12">새 프로그램</text>
<text x="400" y="190" text-anchor="middle" font-size="12">exit(42)</text>
<rect x="400" y="145" width="130" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="465" y="165" text-anchor="middle" font-size="12">zombie (Z)</text>
<line x1="400" y1="140" x2="400" y2="72" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#ah5)"/>
<text x="405" y="105" font-size="12">SIGCHLD</text>
<line x1="530" y1="145" x2="530" y2="72" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="530" y="45" text-anchor="middle" font-size="12">waitpid() → status 42</text>
<text x="530" y="215" text-anchor="middle" font-size="12">여기서 task_struct 해제</text>
<text x="250" y="235" text-anchor="middle" font-size="12">좀비 구간 = exit부터 부모의 wait까지</text>
</svg>
```

그림 3 — fork에서 회수까지의 타임라인. 자식이 `exit`한 뒤 부모가 `waitpid`할 때까지가 좀비 구간이다. 이 구간이 끝나지 않으면 좀비가 쌓인다.

### 4.3 고아(orphan)와 reparenting

반대로 **부모가 먼저 죽으면** 자식은 고아가 된다. 커널은 고아를 다른 프로세스에 입양시킨다(reparenting). 기본 입양처는 PID 1(init, 보통 systemd)이고, init은 자식이 죽을 때마다 wait해서 좀비를 치워 준다. 그래서 "고아는 문제가 아니고 좀비가 문제"다.

### 4.4 예제 4 — 좀비를 만들어 ps로 보고, 고아의 새 부모 확인하기

```c
// ex4: make a zombie and an orphan, observe them with ps
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    pid_t z = fork();
    if (z < 0) { perror("fork"); exit(1); }
    if (z == 0) _exit(3);                  /* exits immediately -> zombie */

    pid_t mid = fork();
    if (mid < 0) { perror("fork"); exit(1); }
    if (mid == 0) {                        /* middle process */
        pid_t o = fork();
        if (o == 0) {                      /* grandchild -> orphan */
            sleep(1);                      /* wait until 'mid' died */
            printf("orphan %d: my parent is now %d\n", getpid(), getppid());
            fflush(stdout);
            sleep(2);
            _exit(0);
        }
        _exit(0);                          /* mid exits; grandchild orphaned */
    }
    waitpid(mid, NULL, 0);                 /* reap mid, but NOT z yet */
    sleep(2);

    char cmd[128];
    snprintf(cmd, sizeof cmd, "ps -o pid,ppid,stat,cmd -p %d,%d", getpid(), z);
    printf("--- before wait (zombie %d) ---\n", z); fflush(stdout);
    if (system(cmd) < 0) perror("system");

    int st;
    waitpid(z, &st, 0);
    printf("--- after wait: reaped %d, status=%d ---\n", z, WEXITSTATUS(st)); fflush(stdout);
    if (system(cmd) < 0) perror("system");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex4_zombie.c -o ex4_zombie && ./ex4_zombie; sleep 2; ps -o pid,comm -p 1'
```

```text
orphan 33329: my parent is now 1
--- before wait (zombie 33327) ---
    PID    PPID STAT CMD
  33326   33289 S    ./ex4_zombie
  33327   33326 Z    [ex4_zombie] <defunct>
--- after wait: reaped 33327, status=3 ---
    PID    PPID STAT CMD
  33326   33289 S    ./ex4_zombie
    PID COMMAND
      1 systemd
```

출력에서 볼 것: (1) 회수 전에는 자식 33327이 `STAT Z`, `<defunct>`로 남아 있다. (2) `waitpid` 후에는 `ps` 목록에서 아예 사라진다 — 이제 진짜로 PID가 반환됐다. (3) 중간 프로세스가 죽은 뒤 손자(orphan)의 `getppid()`가 1, 즉 systemd로 바뀌었다.

### 4.5 예제 5 — subreaper: "고아는 init 말고 나한테 줘"

`prctl(PR_SET_CHILD_SUBREAPER, 1)`을 부른 프로세스는 자기 **자손** 중 고아가 생기면 init 대신 그 고아를 입양한다. systemd가 서비스별로, 컨테이너 런타임의 init(tini 등)이 컨테이너 안에서 이 기능을 쓴다. 장비의 "슈퍼바이저" 프로세스가 자기 밑의 워커들을 끝까지 책임지게 할 때 유용하다.

```c
// ex5: become a child subreaper — orphans get reparented to us, not to PID 1
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/wait.h>

int main(void) {
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0) { perror("prctl"); exit(1); }
    printf("subreaper pid=%d\n", getpid()); fflush(stdout);

    pid_t mid = fork();
    if (mid < 0) { perror("fork"); exit(1); }
    if (mid == 0) {
        if (fork() == 0) {                 /* grandchild */
            sleep(1);
            printf("grandchild %d: ppid=%d\n", getpid(), getppid());
            fflush(stdout);
            _exit(5);
        }
        _exit(0);                          /* orphan the grandchild */
    }
    int st; pid_t p;
    while ((p = wait(&st)) > 0)            /* we reap BOTH mid and grandchild */
        printf("subreaper reaped %d (status %d)\n", p, WEXITSTATUS(st));
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex5_subreaper.c -o ex5_subreaper && ./ex5_subreaper'
```

```text
subreaper pid=33947
grandchild 33949: ppid=33947
subreaper reaped 33948 (status 0)
subreaper reaped 33949 (status 5)
```

출력에서 볼 것: 손자의 새 부모가 1이 아니라 subreaper(33947)다. 그래서 subreaper는 자기가 직접 fork하지 않은 손자(33949)의 종료 코드 5까지 `wait()`로 받는다.

### 4.6 좀비를 안 만드는 방법 정리

- 동기적으로: 자식마다 `waitpid(pid, &st, 0)`.
- 이벤트 루프에서: SIGCHLD를 받으면 `while (waitpid(-1, &st, WNOHANG) > 0) ;`로 끝난 자식을 전부 회수 (시그널은 합쳐질 수 있으니 반드시 루프). 시그널 처리 상세는 A6.
- 자식 상태에 관심이 없다면: `signal(SIGCHLD, SIG_IGN)` 또는 `sigaction`에 `SA_NOCLDWAIT` — 커널이 자식을 자동 회수한다(대신 `wait`로 상태를 받을 수 없다).
- 이중 fork: 손자를 고아로 만들어 init에 맡긴다 (11절 데몬).
- Linux 5.3+의 `pidfd_open()` + `poll()`/`epoll`: 자식 종료를 fd 이벤트로 받는다 (A4와 짝).

---

## 5. 프로세스 상태 — R, S, D, T, Z

### 5.1 상태 전이도

`ps`의 `STAT` 첫 글자(또는 `/proc/<pid>/status`의 `State:`)가 task의 상태다. RTOS의 Ready/Running/Blocked/Suspended와 거의 일대일로 대응하는데, Linux는 Blocked를 **S와 D 두 종류**로 나눈다는 점이 핵심이다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="30" width="150" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="95" y="55" text-anchor="middle" font-size="13">fork() / clone()</text>
<rect x="250" y="170" width="180" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="191" text-anchor="middle" font-size="13">R: running / runnable</text>
<text x="340" y="209" text-anchor="middle" font-size="12">CPU 위 또는 runqueue</text>
<rect x="470" y="30" width="200" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="570" y="51" text-anchor="middle" font-size="13">S: interruptible sleep</text>
<text x="570" y="69" text-anchor="middle" font-size="12">시그널로도 깨어남</text>
<rect x="470" y="310" width="200" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="570" y="331" text-anchor="middle" font-size="13">D: uninterruptible sleep</text>
<text x="570" y="349" text-anchor="middle" font-size="12">시그널 무시, kill -9도 대기</text>
<rect x="20" y="170" width="150" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="95" y="191" text-anchor="middle" font-size="13">T: stopped</text>
<text x="95" y="209" text-anchor="middle" font-size="12">SIGSTOP, 디버거</text>
<rect x="250" y="330" width="180" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="351" text-anchor="middle" font-size="13">Z: zombie</text>
<text x="340" y="369" text-anchor="middle" font-size="12">task_struct만 남음</text>
<rect x="20" y="330" width="150" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="95" y="360" text-anchor="middle" font-size="13">사라짐 (reaped)</text>
<line x1="150" y1="70" x2="258" y2="168" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="430" y1="175" x2="518" y2="82" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="560" y1="82" x2="432" y2="190" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="464" y="128" text-anchor="end" font-size="12">block</text>
<text x="530" y="130" font-size="12">wake_up 또는 signal</text>
<line x1="430" y1="205" x2="518" y2="308" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="560" y1="308" x2="432" y2="215" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="470" y="282" text-anchor="end" font-size="12">I/O 대기</text>
<text x="515" y="262" font-size="12">wake_up만</text>
<line x1="250" y1="185" x2="172" y2="185" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="170" y1="205" x2="248" y2="205" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="210" y="178" text-anchor="middle" font-size="12">SIGSTOP</text>
<text x="210" y="222" text-anchor="middle" font-size="12">SIGCONT</text>
<line x1="340" y1="220" x2="340" y2="328" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="348" y="280" font-size="12">exit</text>
<line x1="250" y1="355" x2="172" y2="355" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="210" y="347" text-anchor="middle" font-size="12">wait</text>
</svg>
```

그림 4 — task 상태 전이도. S와 D는 둘 다 "무언가를 기다리며 잠든" 상태지만, S는 시그널로도 깨어나고 D는 기다리던 사건(wake_up)으로만 깨어난다.

| STAT | 커널 상태 상수 | 뜻 | RTOS 대응 |
|---|---|---|---|
| R | `TASK_RUNNING` | CPU에서 실행 중이거나 runqueue에서 차례를 기다림 | Running + Ready |
| S | `TASK_INTERRUPTIBLE` | 이벤트 대기, 시그널이 오면 깨어나 syscall이 `EINTR`로 끝날 수 있음 | Blocked |
| D | `TASK_UNINTERRUPTIBLE` (`TASK_KILLABLE` 포함) | 이벤트 대기, 시그널로 깨어나지 않음 | 타임아웃 없는 Blocked |
| T | `__TASK_STOPPED` | SIGSTOP/SIGTSTP로 정지 | Suspended |
| t | `__TASK_TRACED` | 디버거(ptrace)가 정지시킴 | 디버거 halt |
| Z | `EXIT_ZOMBIE` | 종료, 부모의 wait 대기 | 없음 |
| I | `TASK_IDLE` | 커널 스레드 전용 "할 일 없음" 대기, load average에 안 잡힘 | idle 태스크 |

`STAT` 두 번째 글자 이후는 부가 정보다: `s` 세션 리더, `l` 멀티스레드, `+` 포그라운드 프로세스 그룹, `<` 높은 우선순위(음수 nice), `N` 낮은 우선순위.

### 5.2 예제 6 — 다섯 가지 상태를 한 화면에 만들기

바쁜 루프(R), `pause()`(S), SIGSTOP(T), 회수 안 된 자식(Z), 그리고 **vfork 부모(D)**를 만든다. `vfork()`를 부른 부모는 자식이 exec/exit할 때까지 커널 안에서 `TASK_KILLABLE`로 기다리는데, `TASK_KILLABLE`은 `TASK_UNINTERRUPTIBLE`에 "SIGKILL만은 받는다"는 비트를 더한 것이라 `ps`에는 D로 보인다. 드라이버 없이 D 상태를 만들어 볼 수 있는 손쉬운 방법이다.

```c
// ex6: put children into R, S, T, Z and D, then look at them with ps
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static pid_t spawn(int kind) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid) return pid;
    switch (kind) {
    case 'R': for (volatile unsigned long i = 0;; i++) ; /* busy loop */
    case 'S': pause(); break;                  /* interruptible sleep */
    case 'Z': _exit(0);                        /* zombie until reaped */
    case 'D': if (vfork() == 0) { sleep(3); _exit(0); } /* parent waits in D */
              pause(); break;
    }
    _exit(0);
}

int main(void) {
    pid_t r = spawn('R'), s = spawn('S'), t = spawn('S'), z = spawn('Z'), d = spawn('D');
    kill(t, SIGSTOP);                          /* stopped -> T */
    sleep(1);
    char cmd[200];
    snprintf(cmd, sizeof cmd,
        "ps -o pid,ppid,stat,wchan:24,cmd --ppid %d,%d", getpid(), d);
    fflush(stdout);
    if (system(cmd) < 0) perror("system");
    printf("main=%d R=%d S=%d T=%d Z=%d D=%d\n", getpid(), r, s, t, z, d);
    kill(r, SIGKILL); kill(s, SIGKILL); kill(t, SIGKILL); kill(d, SIGKILL);
    while (wait(NULL) > 0) ;
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex6_states.c -o ex6_states && ./ex6_states'
```

```text
    PID    PPID STAT WCHAN                    CMD
  34476   34475 R    -                        ./ex6_states
  34477   34475 S    poll_schedule_timeout.co ./ex6_states
  34478   34475 T    do_signal_stop           ./ex6_states
  34479   34475 Z    -                        [ex6_states] <defunct>
  34480   34475 D    kernel_clone             ./ex6_states
  34481   34480 S    hrtimer_nanosleep        ./ex6_states
  34482   34475 S    do_wait                  sh -c -- ps -o pid,ppid,stat,wchan:24,cmd --ppid 34475,34480
main=34475 R=34476 S=34477 T=34478 Z=34479 D=34480
```

출력에서 볼 것:

- `WCHAN`("wait channel")은 그 task가 커널의 **어느 함수 안에서 잠들어 있는지**를 보여 준다. 디버깅에서 가장 유용한 칼럼이다.
- S 자식은 `pause()`를 불렀는데 wchan이 `poll_schedule_timeout`이다. arm64에는 `pause` syscall이 없어서 glibc가 `ppoll(NULL, 0, NULL, NULL)`로 구현하기 때문이다(칼럼 폭 24에서 잘림).
- D 상태 34480은 `kernel_clone` 안(vfork 완료 대기)에 있고, 그 자식 34481은 `sleep(3)` 중이라 `hrtimer_nanosleep`에서 S다.
- `system()`이 만든 `sh`는 `do_wait`, 즉 `ps`가 끝나기를 wait 중이다.

### 5.3 D 상태가 드라이버 개발자에게 중요한 이유

D 상태는 대부분 **커널 코드(드라이버 포함)가 "중간에 끊기면 안 되는" 대기**를 할 때 생긴다. 예를 들어 블록 장치 I/O 완료, 페이지 캐시에 페이지를 읽어 오는 중, 일부 드라이버의 `wait_event()` 같은 곳이다.

| 드라이버의 대기 API (F3) | 태스크 상태 | 시그널이 오면 |
|---|---|---|
| `wait_event(wq, cond)` | D (`TASK_UNINTERRUPTIBLE`) | 무시, cond가 참이 될 때까지 |
| `wait_event_killable(wq, cond)` | D로 보임 (`TASK_KILLABLE`) | SIGKILL만 받고 `-ERESTARTSYS` 반환 |
| `wait_event_interruptible(wq, cond)` | S (`TASK_INTERRUPTIBLE`) | 깨어나서 `-ERESTARTSYS` 반환 |
| `wait_event_interruptible_timeout(...)` | S | 시그널 또는 타임아웃 |
| `wait_for_completion(&c)` | D | 무시 |

실무에서 보이는 증상과 의미:

- **`kill -9`가 안 듣는 프로세스**: 거의 항상 D 상태다. 하드웨어 응답(예: I2C 센서가 clock stretching을 끝내지 않음, DMA 완료 인터럽트가 안 옴)을 `wait_event()`나 `wait_for_completion()`으로 **타임아웃 없이** 기다리는 드라이버가 범인이다. 시그널이 "대기 중"으로 쌓여 있다가 D에서 빠져나오는 순간 처리된다.
- **load average가 CPU 사용률에 비해 높음**: Linux의 load average는 R뿐 아니라 D 상태 task 수도 더한다. CPU는 놀고 있는데 load가 8이면 D에 갇힌 task를 의심한다 (`ps -eo stat,pid,wchan,cmd`에서 D 찾기 → 셸 예시는 아래).
- **dmesg의 "task ... blocked for more than 120 seconds"**: 커널의 hung task detector(`khungtaskd`)가 D 상태로 `/proc/sys/kernel/hung_task_timeout_secs`(이 VM은 120) 이상 머문 task를 찾아 스택을 찍어 준다. 이 스택이 곧 "어느 드라이버의 어느 대기에서 막혔나"의 답이다.

```sh
# D 상태 task와 그 wait channel 찾기 (+ 커널 스택은 root 필요)
ps -eo stat,pid,wchan:32,cmd | awk '$1 ~ /^D/'
sudo cat /proc/<pid>/stack
```

드라이버 작성 규칙(F3에서 자세히): 유저가 기다리는 `read()`/`ioctl()` 경로에서는 기본적으로 `wait_event_interruptible`(또는 최소 `_killable`)을 쓰고 **타임아웃**을 두며, 반환값 `-ERESTARTSYS`를 그대로 위로 올린다. "하드웨어가 응답 안 하면 영원히 D"인 드라이버는 리뷰에서 반드시 지적된다.

---

## 6. PID, TGID, TID — 이름이 헷갈리는 이유

### 6.1 정의

커널은 모든 task에 고유 번호 `pid`를 준다. 같은 스레드 그룹(=유저가 말하는 프로세스)에 속한 task들은 리더의 `pid`를 `tgid`(thread group ID)로 공유한다. 문제는 **유저 공간 API의 이름이 커널과 반대**라는 점이다.

| 유저 공간에서 부르는 이름 | 얻는 방법 | 커널 `task_struct` 필드 |
|---|---|---|
| PID (프로세스 ID) | `getpid()` | `tgid` |
| TID (스레드 ID) | `gettid()` (glibc 2.30+) | `pid` |
| PPID | `getppid()` | `real_parent->tgid` |
| `pthread_t` | `pthread_self()` | 없음 (glibc 내부 포인터, 커널은 모름) |

메인 스레드는 TID == PID다. `pthread_t`는 TID가 아니라 glibc가 쓰는 스레드 디스크립터의 주소일 뿐이라 `ps`나 `/proc`에서 찾을 수 없다. 로그에 스레드를 찍을 때는 `gettid()`를 쓴다.

### 6.2 예제 8 — getpid vs gettid, `/proc/<pid>/task`, 그리고 스레드별 errno

```c
// ex8: getpid vs gettid, /proc/<pid>/task, and errno is per-thread
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

static pthread_barrier_t bar;

static void *worker(void *arg) {
    long id = (long)arg;
    if (id == 1) close(-1);              /* sets errno = EBADF in THIS thread */
    pthread_barrier_wait(&bar);
    printf("thread %ld: getpid=%d gettid=%d pthread_self=%#lx errno=%d\n",
           id, getpid(), gettid(), (unsigned long)pthread_self(), errno);
    fflush(stdout);
    pthread_barrier_wait(&bar);          /* stay alive while main inspects /proc */
    pthread_barrier_wait(&bar);
    return NULL;
}

int main(void) {
    pthread_t th[2];
    pthread_barrier_init(&bar, NULL, 3);
    for (long i = 0; i < 2; i++)
        if (pthread_create(&th[i], NULL, worker, (void *)(i + 1))) { perror("pthread_create"); exit(1); }
    pthread_barrier_wait(&bar);
    printf("main    : getpid=%d gettid=%d errno=%d\n", getpid(), gettid(), errno);
    pthread_barrier_wait(&bar);
    char cmd[160];
    snprintf(cmd, sizeof cmd,
             "ls /proc/%d/task; grep -E '^(Tgid|Pid|PPid|Threads):' /proc/%d/status;"
             " ps -L -o pid,lwp,nlwp,stat,comm -p %d", getpid(), getpid(), getpid());
    fflush(stdout);
    if (system(cmd) < 0) perror("system");
    pthread_barrier_wait(&bar);
    for (int i = 0; i < 2; i++) pthread_join(th[i], NULL);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex8_tid.c -o ex8_tid -pthread && ./ex8_tid'
```

```text
thread 2: getpid=34894 gettid=34939 pthread_self=0xe4fff5b0f160 errno=0
thread 1: getpid=34894 gettid=34938 pthread_self=0xe4fff631f160 errno=9
main    : getpid=34894 gettid=34894 errno=0
34894
34938
34939
Tgid:	34894
Pid:	34894
PPid:	2266
Threads:	3
    PID     LWP NLWP STAT COMMAND
  34894   34894    3 Ssl  ex8_tid
  34894   34938    3 Ssl  ex8_tid
  34894   34939    3 Ssl  ex8_tid
```

출력에서 볼 것:

- 세 스레드 모두 `getpid()`는 34894, `gettid()`는 각자 다르다. 메인 스레드는 TID == PID.
- `/proc/34894/task/` 아래에 TID마다 디렉터리가 있다. 각 `/proc/34894/task/<tid>/status`, `stat`, `stack`이 스레드 단위 정보다.
- `/proc/<pid>/status`의 `Tgid`/`Pid`는 커널 용어 그대로다. `Threads: 3`.
- `ps -L`은 스레드를 한 줄씩 보여 준다. `LWP`(light-weight process)가 TID, `NLWP`가 스레드 수. STAT `Ssl` = 잠자는 중, 세션 리더, 멀티스레드.
- **errno는 스레드마다 따로다.** thread 1만 `close(-1)`로 `EBADF`(9)를 만들었고 다른 스레드의 errno는 0이다. glibc의 `errno`는 전역 변수가 아니라 `(*__errno_location())`, 즉 스레드 로컬 저장소(TLS)에 있는 값이다.

### 6.3 `/proc/<pid>/status`에서 자주 보는 줄

```sh
grep -E "^(Name|State|Tgid|Pid|PPid|Threads|Cpus_allowed_list|voluntary|nonvol)" /proc/self/status
```

```text
Name:	grep
State:	R (running)
Tgid:	187632
Pid:	187632
PPid:	2266
Threads:	1
Cpus_allowed_list:	0-3
voluntary_ctxt_switches:	24
nonvoluntary_ctxt_switches:	4
```

출력에서 볼 것: 상태(R), 스레드 수, 허용 CPU 목록(9절 affinity), 그리고 context switch 카운터 두 개(9.4절). 그 밖에 `VmRSS`(실제 물리 메모리), `SigBlk`/`SigCgt`(막은/잡은 시그널 비트맵 — A6)도 디버깅에 자주 쓴다.

---

## 7. pthreads — 만들기, 기다리기, 떼어 놓기

### 7.1 기본 API

| 함수 | 하는 일 | 주의 |
|---|---|---|
| `pthread_create(&t, attr, fn, arg)` | 새 스레드에서 `fn(arg)` 실행 | 실패 시 **errno가 아니라 반환값**으로 에러 코드 |
| `pthread_join(t, &ret)` | 스레드 종료 대기 + 반환값 받기 | join 안 하면 스레드판 좀비(스택 등이 회수 안 됨) |
| `pthread_detach(t)` | "아무도 join 안 함, 끝나면 알아서 정리" | detach한 스레드는 join 불가 |
| `pthread_exit(ret)` | 현재 스레드만 종료 | 메인에서 부르면 다른 스레드는 계속 실행 |
| `pthread_attr_setstacksize` | 스택 크기 지정 | 기본은 `ulimit -s`(이 VM 8192 KiB) |
| `pthread_setname_np(t, "imu")` | 스레드 이름 (15자 + NUL) | `ps -L -o comm`, `top -H`에 보임 — 실무 필수 |

주의할 차이: 프로세스는 `exit()` 하나로 끝나지만, **어느 스레드든 `exit()`를 부르면 프로세스 전체(모든 스레드)가 끝난다**. 스레드 하나만 끝내려면 `fn`에서 `return`하거나 `pthread_exit`를 쓴다. 또 어느 스레드에서 segfault가 나도 프로세스 전체가 죽는다 — 격리가 없다는 뜻이다.

### 7.2 예제 7 — 전역 카운터 공유와 레이스 미리보기

스레드 4개가 공유 카운터를 각각 100만 번 증가시킨다. 하나는 그냥 `++`, 하나는 atomic 연산.

```c
// ex7: threads share globals — a plain counter loses updates, an atomic one doesn't
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define NT 4
#define N  1000000

static volatile long plain;          /* volatile does NOT make ++ atomic */
static long atomic_cnt;

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++) {
        plain++;                                         /* load, add, store */
        __atomic_fetch_add(&atomic_cnt, 1, __ATOMIC_RELAXED); /* one atomic RMW */
    }
    return NULL;
}

int main(void) {
    pthread_t th[NT];
    for (int i = 0; i < NT; i++) {
        int e = pthread_create(&th[i], NULL, worker, NULL);
        if (e) { fprintf(stderr, "pthread_create: %d\n", e); exit(1); }
    }
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
    printf("expected %d\n", NT * N);
    printf("plain    %ld (lost %ld)\n", plain, (long)NT * N - plain);
    printf("atomic   %ld\n", atomic_cnt);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex7_race.c -o ex7_race -pthread && ./ex7_race && ./ex7_race'
```

```text
expected 4000000
plain    2640597 (lost 1359403)
atomic   4000000
expected 4000000
plain    2745272 (lost 1254728)
atomic   4000000
```

출력에서 볼 것: 일반 `++`는 매번 다른 값으로 100만 번 이상을 잃어버린다. `plain++`은 load → add → store 세 단계라 두 스레드가 같은 값을 읽고 둘 다 +1을 저장하면 하나가 사라진다(lost update). `volatile`은 "레지스터에 캐시하지 말라"일 뿐 원자성을 주지 않는다. `__atomic_fetch_add`는 arm64에서 하나의 원자적 read-modify-write(LSE `ldadd` 명령, LSE가 없는 CPU에서는 `ldxr/stxr` 재시도 루프)로 실행되어 한 번도 잃지 않는다.

Don에게 익숙한 장면: ISR과 메인 루프가 공유하던 카운터를 인터럽트 disable로 보호했던 것. 멀티코어 Linux에서는 "인터럽트 끄기"가 다른 코어를 막지 못하므로 atomic이나 mutex가 필요하다. mutex·condvar·futex는 A8에서, 커널의 spinlock·atomic은 F4에서 다룬다.

### 7.3 thread-safety 체크리스트

- **thread-safe 함수**: 여러 스레드가 동시에 불러도 안전한 함수. POSIX 함수 대부분은 thread-safe이지만 예외가 있다: `strtok`, `localtime`, `gmtime`, `rand`, `strerror`(구현에 따라), `getenv`/`setenv` 조합 등. 대체: `strtok_r`, `localtime_r`, `rand_r`, `strerror_r`.
- **reentrant(재진입 가능)와는 다르다.** reentrant는 "같은 스레드에서 실행 도중 다시 들어와도(시그널 핸들러 등) 안전"이라는 더 강한 조건이다. 시그널 핸들러에서는 async-signal-safe 함수만 써야 한다(A6).
- **stdio는 내부 락이 있어** `printf` 한 번의 출력이 다른 스레드 출력과 섞이지 않는다. 대신 그 락 때문에 고빈도 로그가 병목이 될 수 있다.
- `static` 지역 변수, 전역 버퍼를 돌려주는 함수는 thread-unsafe의 신호다.
- 스레드별 데이터가 필요하면 `__thread`(또는 C11 `_Thread_local`) 변수를 쓴다. errno가 바로 이 방식이다.

---

## 8. `clone()` — 프로세스와 스레드는 플래그 차이일 뿐

### 8.1 strace로 본 pthread_create

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && strace -f -e trace=clone,clone3 ./ex7_race 2>&1 | head -4'
```

```text
clone3({flags=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID, child_tid=0xfd280e98f588, parent_tid=0xfd280e98f230, exit_signal=0, stack=0xfd280e180000, stack_size=0x80ea40, tls=0xfd280e98f880}strace: Process 187498 attached
 => {parent_tid=[187498]}, 88) = 187498
[pid 187497] clone3({flags=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID, child_tid=0xfd280e17f588, parent_tid=0xfd280e17f230, exit_signal=0, stack=0xfd280d970000, stack_size=0x80ea40, tls=0xfd280e17f880}strace: Process 187499 attached
 => {parent_tid=[187499]}, 88) = 187499
```

출력에서 볼 것: `pthread_create`도 결국 `clone3` syscall이고, fork와의 차이는 **플래그**뿐이다. 1.5절의 fork는 `CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD`(공유 없음)였다. 스택(`stack=`, 약 8 MiB)은 glibc가 미리 `mmap`해서 넘겨준다 — 커널은 스레드 스택을 만들어 주지 않는다.

| 플래그 | 공유하는 것 | fork | pthread |
|---|---|---|---|
| `CLONE_VM` | 주소 공간 (`mm_struct`) | 복사 | 공유 |
| `CLONE_FILES` | fd 테이블 (`files_struct`) | 복사 | 공유 |
| `CLONE_FS` | cwd, root, umask (`fs_struct`) | 복사 | 공유 |
| `CLONE_SIGHAND` | 시그널 핸들러 테이블 | 복사 | 공유 |
| `CLONE_THREAD` | 같은 스레드 그룹(TGID)에 넣음 | 아니오 | 예 |
| `CLONE_SETTLS` | 새 TLS 포인터 설정 (arm64 `TPIDR_EL0`) | 아니오 | 예 |
| `CLONE_CHILD_CLEARTID` | 종료 시 tid 변수를 0으로 + futex wake (`pthread_join`의 원리) | 예 | 예 |
| `CLONE_VFORK` | 자식이 exec/exit할 때까지 부모 정지 | 아니오 | 아니오 (`vfork`, `posix_spawn`이 사용) |

이 플래그들을 섞으면 "fd는 공유하지만 주소 공간은 따로" 같은 중간 형태도 만들 수 있다. 컨테이너의 namespace(`CLONE_NEWPID`, `CLONE_NEWNET` …)도 같은 syscall의 플래그다(L8).

### 8.2 `task_struct` 관계도

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="120" y="20" text-anchor="middle" font-size="13">스레드 그룹 (TGID 300)</text>
<rect x="20" y="30" width="200" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="52" text-anchor="middle" font-size="13">task_struct (leader)</text>
<text x="120" y="72" text-anchor="middle" font-size="12">pid=300 tgid=300</text>
<rect x="20" y="110" width="200" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="132" text-anchor="middle" font-size="13">task_struct</text>
<text x="120" y="152" text-anchor="middle" font-size="12">pid=301 tgid=300</text>
<rect x="20" y="190" width="200" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="212" text-anchor="middle" font-size="13">task_struct</text>
<text x="120" y="232" text-anchor="middle" font-size="12">pid=302 tgid=300</text>
<rect x="290" y="30" width="170" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="375" y="51" text-anchor="middle" font-size="13">mm_struct</text>
<text x="375" y="69" text-anchor="middle" font-size="12">VMA + page table</text>
<rect x="290" y="115" width="170" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="375" y="136" text-anchor="middle" font-size="13">files_struct</text>
<text x="375" y="154" text-anchor="middle" font-size="12">fd table</text>
<rect x="290" y="200" width="170" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="375" y="221" text-anchor="middle" font-size="13">signal_struct</text>
<text x="375" y="239" text-anchor="middle" font-size="12">sighand, rlimit</text>
<line x1="220" y1="55" x2="288" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="220" y1="135" x2="288" y2="62" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="220" y1="215" x2="288" y2="70" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="220" y1="65" x2="288" y2="135" stroke="currentColor" stroke-opacity="0.5" marker-end="url(#ah4)"/>
<line x1="220" y1="145" x2="288" y2="142" stroke="currentColor" stroke-opacity="0.5" marker-end="url(#ah4)"/>
<line x1="220" y1="225" x2="288" y2="150" stroke="currentColor" stroke-opacity="0.5" marker-end="url(#ah4)"/>
<line x1="220" y1="75" x2="288" y2="215" stroke="currentColor" stroke-opacity="0.3" marker-end="url(#ah4)"/>
<line x1="220" y1="155" x2="288" y2="222" stroke="currentColor" stroke-opacity="0.3" marker-end="url(#ah4)"/>
<line x1="220" y1="235" x2="288" y2="230" stroke="currentColor" stroke-opacity="0.3" marker-end="url(#ah4)"/>
<text x="580" y="20" text-anchor="middle" font-size="13">fork한 자식</text>
<rect x="500" y="30" width="160" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="580" y="51" text-anchor="middle" font-size="13">mm_struct</text>
<text x="580" y="69" text-anchor="middle" font-size="12">복사본 (COW)</text>
<rect x="500" y="115" width="160" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="580" y="136" text-anchor="middle" font-size="13">files_struct</text>
<text x="580" y="154" text-anchor="middle" font-size="12">복사본</text>
<rect x="500" y="200" width="160" height="70" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="580" y="222" text-anchor="middle" font-size="13">task_struct</text>
<text x="580" y="242" text-anchor="middle" font-size="12">pid=400 tgid=400</text>
<text x="580" y="260" text-anchor="middle" font-size="12">real_parent → 300</text>
<line x1="560" y1="200" x2="560" y2="167" stroke="currentColor" marker-end="url(#ah4)"/>
<polyline points="660,235 672,235 672,55 662,55" fill="none" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="20" y="300" font-size="12">CLONE_VM · CLONE_FILES · CLONE_SIGHAND · CLONE_THREAD</text>
<text x="20" y="318" font-size="12">→ 포인터만 공유하고 참조 카운트를 올린다</text>
<text x="660" y="300" text-anchor="end" font-size="12">플래그 없는 clone (fork)</text>
<text x="660" y="318" text-anchor="end" font-size="12">→ 구조체를 새로 만들어 복사한다</text>
<text x="340" y="348" text-anchor="middle" font-size="12">커널 스케줄러가 보는 단위는 언제나 task_struct 하나하나다</text>
</svg>
```

그림 5 — 스레드 3개는 각자 `task_struct`를 갖지만 `mm`, `files`, `signal` 포인터가 같은 구조체를 가리킨다. fork한 자식은 그 구조체들의 복사본을 따로 가진다.

`task_struct`는 커널에서 가장 큰 구조체 중 하나로, 대략 이런 필드가 있다 (F1에서 자세히).

```
struct task_struct {
    unsigned int        __state;      /* TASK_RUNNING, TASK_INTERRUPTIBLE, ... */
    void               *stack;        /* kernel stack (16 KiB on arm64) */
    int                 prio, static_prio;  /* nice -> static_prio */
    unsigned int        policy;       /* SCHED_NORMAL, SCHED_FIFO, ... */
    struct sched_entity se;           /* fair class (EEVDF) bookkeeping */
    cpumask_t           cpus_mask;    /* affinity */
    struct mm_struct   *mm;           /* NULL for kernel threads */
    pid_t               pid, tgid;
    struct task_struct *real_parent, *parent;
    struct list_head    children, sibling;
    struct files_struct *files;
    struct signal_struct *signal;
    struct sighand_struct *sighand;
    unsigned long       nvcsw, nivcsw; /* voluntary / involuntary ctxt switches */
    char                comm[16];     /* name shown by ps */
    /* ... hundreds more ... */
};
```

이 필드들이 유저 공간 어디서 보이는지 연결해 두면 좋다: `__state` → `ps STAT`, `comm` → `ps comm`과 `pthread_setname_np`, `nvcsw/nivcsw` → `/proc/<pid>/status`의 ctxt_switches, `cpus_mask` → `Cpus_allowed_list`. 커널 스레드(`kthreadd`, `kworker/...`)는 `mm == NULL`인 task다 — 유저 주소 공간이 없다.

드라이버 코드에서는 `current` 매크로가 "지금 이 syscall을 부른 task의 `task_struct *`"다. 예를 들어 드라이버의 `.read`에서 `current->pid`, `current->comm`으로 누가 호출했는지 로그를 찍을 수 있다.

---

## 9. 유저 공간에서 보이는 스케줄링

깊은 스케줄러 내부(EEVDF, 스케줄링 클래스, 선점 모델)는 F1과 N1·N2에서 다룬다. 여기서는 **유저 코드가 만질 수 있는 손잡이 네 개**만 본다: nice, 실시간 정책, CPU affinity, 그리고 context switch 카운터.

### 9.1 정책과 우선순위 한눈에

| 정책 | 클래스 | 우선순위 손잡이 | 성격 |
|---|---|---|---|
| `SCHED_OTHER` (=`SCHED_NORMAL`) | fair (6.6부터 EEVDF, 그전 CFS) | nice -20 ~ +19 | 기본값. CPU 시간을 가중치 비율로 나눔 |
| `SCHED_BATCH` | fair | nice | 배치 작업, 깨어날 때 선점 덜 함 |
| `SCHED_IDLE` | fair | 없음 | 정말 남는 시간에만 |
| `SCHED_FIFO` | rt | 1 ~ 99 | 더 높은 RT가 오거나 스스로 block할 때까지 계속 실행 |
| `SCHED_RR` | rt | 1 ~ 99 | FIFO + 같은 우선순위끼리 time slice |
| `SCHED_DEADLINE` | dl | runtime/deadline/period | EDF. 주기 작업용 (N2) |

RTOS 관점에서 보면 `SCHED_FIFO`가 "고정 우선순위 선점형 스케줄러"에 가장 가깝다. 반대로 `SCHED_OTHER`의 nice는 우선순위가 아니라 **CPU 몫의 비율**이다. nice 값이 1 낮아질 때마다 가중치가 약 1.25배가 되도록 표(`sched_prio_to_weight`)가 정해져 있다: nice 0 = 1024, nice 5 = 335.

### 9.2 예제 12 — nice가 CPU 몫을 바꾸는 정도, 그리고 SCHED_FIFO의 권한

같은 CPU(3번)에 묶인 두 개의 CPU hog를 nice 0과 nice 5로 2초 돌려 루프 횟수를 비교한다. 그다음 `SCHED_FIFO`로 바꿔 본다.

```c
// ex12: nice changes CFS/EEVDF share; SCHED_FIFO needs privilege (CAP_SYS_NICE)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>

static void hog(int niceval) {
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(3, &s);
    if (sched_setaffinity(0, sizeof s, &s) < 0) { perror("affinity"); exit(1); }
    if (setpriority(PRIO_PROCESS, 0, niceval) < 0) { perror("setpriority"); exit(1); }
    struct timespec t0, t; unsigned long n = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    do { clock_gettime(CLOCK_MONOTONIC, &t); n++; } while (t.tv_sec - t0.tv_sec < 2);
    printf("nice %2d: %lu loops\n", niceval, n);
    fflush(stdout);
    _exit(0);
}

int main(void) {
    int nv[2] = { 0, 5 };
    for (int i = 0; i < 2; i++) { pid_t p = fork();
        if (p < 0) { perror("fork"); exit(1); }
        if (p == 0) hog(nv[i]); }
    while (wait(NULL) > 0) ;

    struct sched_param sp = { .sched_priority = 50 };
    if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
        printf("SCHED_FIFO prio 50: failed: %s\n", strerror(errno));
    else
        printf("SCHED_FIFO prio 50: ok, policy now %d (SCHED_FIFO=%d)\n",
               sched_getscheduler(0), SCHED_FIFO);
    printf("FIFO prio range: %d..%d\n",
           sched_get_priority_min(SCHED_FIFO), sched_get_priority_max(SCHED_FIFO));
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex12_sched.c -o ex12_sched && ./ex12_sched && echo "--- with sudo" && sudo ./ex12_sched'
```

```text
nice  0: 52304741 loops
nice  5: 17219856 loops
SCHED_FIFO prio 50: failed: Operation not permitted
FIFO prio range: 1..99
--- with sudo
nice  0: 74157737 loops
nice  5: 24355199 loops
SCHED_FIFO prio 50: ok, policy now 1 (SCHED_FIFO=1)
FIFO prio range: 1..99
```

출력에서 볼 것: (1) 루프 비율이 52.3M / 17.2M ≈ 3.04, 74.2M / 24.4M ≈ 3.04로 가중치 비율 1024 / 335 ≈ 3.06과 거의 같다. nice는 "누가 먼저"가 아니라 "몫을 얼마나"다. (2) 일반 사용자는 `SCHED_FIFO`로 못 바꾼다(`EPERM`). `CAP_SYS_NICE` 권한이나 `RLIMIT_RTPRIO`(`ulimit -r`)가 필요하다. (3) 절대 루프 수가 두 실행에서 다른 것은 VM 위라 호스트 상황에 따라 CPU 속도가 흔들리기 때문이다 — 비율만 본다.

셸에서는 `nice`/`renice`, `chrt`로 같은 일을 한다.

```sh
limactl shell elinux -- bash -lc 'chrt -p $$; sudo chrt -f 30 sleep 5 & sleep 0.5; ps -o pid,cls,rtprio,ni,pri,cmd -C sleep; wait'
```

```text
pid 186504's current scheduling policy: SCHED_OTHER
pid 186504's current scheduling priority: 0
pid 186504's current runtime parameter: 2100000
    PID CLS RTPRIO  NI PRI CMD
 186614  TS      -   0  19 sleep 3
 186628  FF     30   -  70 sleep 5
 186630  TS      -   0  19 sleep 0.5
```

출력에서 볼 것: `CLS` 칼럼이 `TS`(time sharing = SCHED_OTHER)와 `FF`(SCHED_FIFO)로 구분되고 FIFO task는 `RTPRIO 30`이 붙는다. `sleep 3`은 VM에서 동시에 돌던 다른 프로세스라 무시한다. `runtime parameter: 2100000`은 EEVDF가 이 task에 주는 slice(ns)로, 이 VM의 `/sys/kernel/debug/sched/base_slice_ns` 값 2100000(2.1 ms)과 같다.

> 주의: SCHED_FIFO task가 무한 루프를 돌면 같은 CPU의 일반 task는 굶는다. Linux는 이를 막기 위해 기본으로 RT task가 1초 중 0.95초까지만 쓰게 제한한다(`/proc/sys/kernel/sched_rt_runtime_us` = 950000). RT 튜닝은 N2에서 다룬다.

### 9.3 예제 9 — CPU affinity로 한 코어에 묶기

```c
// ex9: CPU affinity — pin this process to one CPU and watch sched_getcpu()
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>

static void show(const char *tag) {
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof set, &set) < 0) { perror("getaffinity"); exit(1); }
    printf("%-10s allowed:", tag);
    for (int c = 0; c < CPU_SETSIZE; c++) if (CPU_ISSET(c, &set)) printf(" %d", c);
    printf("  (count=%d) now on cpu %d\n", CPU_COUNT(&set), sched_getcpu());
}

int main(void) {
    printf("online CPUs = %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    show("default");
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(2, &set);                       /* pin to CPU 2 */
    if (sched_setaffinity(0, sizeof set, &set) < 0) { perror("setaffinity"); exit(1); }
    show("pinned");
    int moved = 0;
    for (int i = 0; i < 1000; i++) { usleep(100); if (sched_getcpu() != 2) moved++; }
    printf("after 1000 sleeps: times seen off cpu 2 = %d\n", moved);
    char cmd[64];
    snprintf(cmd, sizeof cmd, "taskset -p %d", getpid());
    fflush(stdout);
    if (system(cmd) < 0) perror("system");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex9_affinity.c -o ex9_affinity && ./ex9_affinity'
```

```text
online CPUs = 4
default    allowed: 0 1 2 3  (count=4) now on cpu 2
pinned     allowed: 2  (count=1) now on cpu 2
after 1000 sleeps: times seen off cpu 2 = 0
pid 35498's current affinity mask: 4
```

출력에서 볼 것: 마스크를 CPU 2 하나로 바꾸자 1000번 잠들고 깨어나도 항상 CPU 2에서 실행된다. `taskset`은 같은 마스크를 16진수 비트맵(`4` = 비트 2)으로 보여 준다. `sched_setaffinity(0, ...)`의 0은 "호출한 스레드"다 — affinity는 **스레드 단위** 속성이라 멀티스레드 프로그램에서는 `pthread_setaffinity_np`로 스레드마다 지정한다. affinity는 fork와 exec 너머로 상속된다.

실무 용도: Jetson·i.MX8 같은 SoC에서 IMU 읽기 스레드를 한 코어에 고정해 캐시를 덥게 유지하고 마이그레이션 지터를 없앤다. 그 코어를 커널 부팅 인자 `isolcpus=`로 일반 task에서 비워 두는 기법은 N2에서 다룬다. 셸에서는 `taskset -c 2 ./prog`, `taskset -p -c 2 <pid>`.

### 9.4 예제 11 — voluntary vs nonvoluntary context switch

`/proc/<pid>/status`의 두 카운터는 지연 문제를 진단할 때 1순위로 보는 값이다.

- `voluntary_ctxt_switches`: task가 **스스로** CPU를 내려놓은 횟수 (sleep, I/O 대기, 락 대기 = block).
- `nonvoluntary_ctxt_switches`: 실행 중에 **빼앗긴** 횟수 (time slice 소진, 더 높은 우선순위 task 등장 = preemption).

sleeper 하나와 CPU hog 두 개를 모두 CPU 1에 묶고 2초 동안 돌려 비교한다.

```c
// ex11: voluntary vs nonvoluntary context switches (sleeper vs CPU hogs on one CPU)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

static void report(const char *who) {
    char line[128];
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) { perror("fopen"); exit(1); }
    while (fgets(line, sizeof line, f))
        if (strstr(line, "ctxt_switches")) printf("%-8s %s", who, line);
    fclose(f);
    fflush(stdout);
}

static void child(int kind) {
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(1, &s);       /* everyone on CPU 1 */
    if (sched_setaffinity(0, sizeof s, &s) < 0) { perror("affinity"); exit(1); }
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    do {
        if (kind == 0) usleep(1000);                  /* sleeper: blocks often */
        clock_gettime(CLOCK_MONOTONIC, &t);            /* hogs: spin */
    } while (t.tv_sec - t0.tv_sec < 2);
    report(kind == 0 ? "sleeper" : "hog");
    _exit(0);
}

int main(void) {
    for (int k = 0; k < 3; k++) {                      /* 1 sleeper + 2 hogs */
        pid_t p = fork();
        if (p < 0) { perror("fork"); exit(1); }
        if (p == 0) child(k == 0 ? 0 : 1);
    }
    while (wait(NULL) > 0) ;
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex11_ctxt.c -o ex11_ctxt && ./ex11_ctxt'
```

```text
hog      voluntary_ctxt_switches:	0
hog      nonvoluntary_ctxt_switches:	806
hog      voluntary_ctxt_switches:	0
hog      nonvoluntary_ctxt_switches:	808
sleeper  voluntary_ctxt_switches:	1550
sleeper  nonvoluntary_ctxt_switches:	1
```

출력에서 볼 것: hog는 스스로 양보한 적이 0번이고 2초 동안 약 800번 선점당했다. sleeper는 `usleep`마다 스스로 잠들어 voluntary가 1550번이고 거의 선점당하지 않는다(깨어나면 할 일이 짧아서 금방 다시 잔다). 2초 동안 1 ms씩 잤는데 2000번이 아니라 1550번인 것은 sleep 자체의 오버슈트와 깨어난 뒤 CPU를 기다린 시간 때문이다.

진단 규칙: **실시간이어야 할 스레드의 nonvoluntary가 빠르게 증가한다면 누군가에게 CPU를 빼앗기고 있다**는 뜻이다 — 우선순위(SCHED_FIFO)나 affinity·isolcpus를 검토한다. voluntary가 비정상적으로 많다면 락 경합이나 너무 잦은 짧은 I/O를 의심한다. 측정 방법: 일정 간격으로 `/proc/<pid>/task/<tid>/status`를 두 번 읽어 차이를 본다(스레드별 값).

---

## 10. 생성 비용 측정 — fork vs vfork vs posix_spawn vs pthread_create

### 10.1 예제 10 — 부모 크기를 바꿔 가며 2000번 생성+회수

부모가 0 / 64 / 256 / 1024 MiB를 할당해 다 건드린 상태에서 네 가지 방법으로 "만들고 회수하기"를 반복해 1회 평균을 잰다. `posix_spawn`은 `/bin/true`를 실제로 exec까지 하므로 비교 대상이 조금 다르다(exec 비용 포함).

```c
// ex10: cost of creating+reaping: fork, vfork, posix_spawn, pthread_create
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>

#define N 2000
extern char **environ;

static double now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
static void *nop(void *a) { return a; }
static size_t mb;          /* static: safe across vfork */
static char *big;

int main(int argc, char **argv) {
    mb = argc > 1 ? strtoul(argv[1], NULL, 0) : 0;
    if (mb) { big = malloc(mb << 20); if (!big) { perror("malloc"); exit(1); }
              memset(big, 1, mb << 20); }          /* make the parent "heavy" */
    double t0, t;

    t0 = now_us();
    for (int i = 0; i < N; i++) { pid_t p = fork(); if (p == 0) _exit(0);
        if (p < 0) { perror("fork"); exit(1); } waitpid(p, NULL, 0); }
    t = (now_us() - t0) / N; printf("RSS=%4zuMB fork+wait          %8.1f us\n", mb, t);

    t0 = now_us();
    for (int i = 0; i < N; i++) { pid_t p = vfork(); if (p == 0) _exit(0);
        if (p < 0) { perror("vfork"); exit(1); } waitpid(p, NULL, 0); }
    t = (now_us() - t0) / N; printf("RSS=%4zuMB vfork+wait         %8.1f us\n", mb, t);

    char *av[] = { "/bin/true", NULL };
    t0 = now_us();
    for (int i = 0; i < N / 4; i++) { pid_t p;
        if (posix_spawn(&p, "/bin/true", NULL, NULL, av, environ)) { perror("spawn"); exit(1); }
        waitpid(p, NULL, 0); }
    t = (now_us() - t0) / (N / 4); printf("RSS=%4zuMB posix_spawn(true)  %8.1f us\n", mb, t);

    t0 = now_us();
    for (int i = 0; i < N; i++) { pthread_t th;
        if (pthread_create(&th, NULL, nop, NULL)) { perror("pthread_create"); exit(1); }
        pthread_join(th, NULL); }
    t = (now_us() - t0) / N; printf("RSS=%4zuMB pthread_create+join %8.1f us\n", mb, t);
    free(big);
    return 0;
}
```

`mb`와 `big`을 `static`으로 둔 이유: `vfork` 자식이 부모 스택을 공유하므로 GCC가 지역 변수에 `-Wclobbered` 경고를 낸다(처음엔 지역 변수로 썼다가 이 경고를 보고 고쳤다).

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex10_cost.c -o ex10_cost -pthread && for m in 0 64 256 1024; do for k in 1 2 3 4 5; do ./ex10_cost $m; done; done'
```

한 번 실행의 출력 예 (전체 20회 중 RSS 1024 MiB 첫 실행):

```text
RSS=1024MB fork+wait             298.2 us
RSS=1024MB vfork+wait             36.9 us
RSS=1024MB posix_spawn(true)     182.4 us
RSS=1024MB pthread_create+join     37.4 us
```

크기별 5회 실행의 중앙값 (단위 us, VM 위라 실행마다 10~20% 흔들린다):

| 부모 RSS | fork+wait | vfork+wait | pthread_create+join | posix_spawn(/bin/true) |
|---|---|---|---|---|
| 0 MiB | 72.6 | 41.7 | 41.6 | 198.7 |
| 64 MiB | 114.2 | 41.5 | 44.4 | 192.9 |
| 256 MiB | 153.2 | 39.6 | 38.9 | 191.6 |
| 1024 MiB | 299.0 | 36.0 | 37.4 | 182.4 |

```svg
<svg viewBox="0 0 640 340" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="22" text-anchor="middle" font-size="14">프로세스/스레드 생성+회수 1회 비용 (us, 5회 중앙값)</text>
<line x1="70" y1="270.0" x2="620" y2="270.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="274.0" text-anchor="end" font-size="12">0</text>
<line x1="70" y1="215.0" x2="620" y2="215.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="219.0" text-anchor="end" font-size="12">80</text>
<line x1="70" y1="160.0" x2="620" y2="160.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="164.0" text-anchor="end" font-size="12">160</text>
<line x1="70" y1="105.0" x2="620" y2="105.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="109.0" text-anchor="end" font-size="12">240</text>
<line x1="70" y1="50.0" x2="620" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="54.0" text-anchor="end" font-size="12">320</text>
<line x1="70" y1="270" x2="620" y2="270" stroke="currentColor"/>
<line x1="70" y1="50" x2="70" y2="270" stroke="currentColor"/>
<rect x="86.8" y="220.1" width="22" height="49.9" fill="#4a7bd0"/>
<text x="97.8" y="216.1" text-anchor="middle" font-size="12">73</text>
<rect x="112.8" y="241.3" width="22" height="28.7" fill="#3f9a6b"/>
<text x="123.8" y="237.3" text-anchor="middle" font-size="12">42</text>
<rect x="138.8" y="241.4" width="22" height="28.6" fill="#e08a3c"/>
<text x="149.8" y="237.4" text-anchor="middle" font-size="12">42</text>
<rect x="164.8" y="133.4" width="22" height="136.6" fill="#888"/>
<text x="175.8" y="129.4" text-anchor="middle" font-size="12">199</text>
<text x="138.8" y="288" text-anchor="middle" font-size="12">부모 RSS 0 MB</text>
<rect x="224.2" y="191.5" width="22" height="78.5" fill="#4a7bd0"/>
<text x="235.2" y="187.5" text-anchor="middle" font-size="12">114</text>
<rect x="250.2" y="241.5" width="22" height="28.5" fill="#3f9a6b"/>
<text x="261.2" y="237.5" text-anchor="middle" font-size="12">42</text>
<rect x="276.2" y="239.5" width="22" height="30.5" fill="#e08a3c"/>
<text x="287.2" y="235.5" text-anchor="middle" font-size="12">44</text>
<rect x="302.2" y="137.4" width="22" height="132.6" fill="#888"/>
<text x="313.2" y="133.4" text-anchor="middle" font-size="12">193</text>
<text x="276.2" y="288" text-anchor="middle" font-size="12">부모 RSS 64 MB</text>
<rect x="361.8" y="164.7" width="22" height="105.3" fill="#4a7bd0"/>
<text x="372.8" y="160.7" text-anchor="middle" font-size="12">153</text>
<rect x="387.8" y="242.8" width="22" height="27.2" fill="#3f9a6b"/>
<text x="398.8" y="238.8" text-anchor="middle" font-size="12">40</text>
<rect x="413.8" y="243.3" width="22" height="26.7" fill="#e08a3c"/>
<text x="424.8" y="239.3" text-anchor="middle" font-size="12">39</text>
<rect x="439.8" y="138.3" width="22" height="131.7" fill="#888"/>
<text x="450.8" y="134.3" text-anchor="middle" font-size="12">192</text>
<text x="413.8" y="288" text-anchor="middle" font-size="12">부모 RSS 256 MB</text>
<rect x="499.2" y="64.4" width="22" height="205.6" fill="#4a7bd0"/>
<text x="510.2" y="60.4" text-anchor="middle" font-size="12">299</text>
<rect x="525.2" y="245.2" width="22" height="24.8" fill="#3f9a6b"/>
<text x="536.2" y="241.2" text-anchor="middle" font-size="12">36</text>
<rect x="551.2" y="244.3" width="22" height="25.7" fill="#e08a3c"/>
<text x="562.2" y="240.3" text-anchor="middle" font-size="12">37</text>
<rect x="577.2" y="144.6" width="22" height="125.4" fill="#888"/>
<text x="588.2" y="140.6" text-anchor="middle" font-size="12">182</text>
<text x="551.2" y="288" text-anchor="middle" font-size="12">부모 RSS 1024 MB</text>
<rect x="40" y="306" width="12" height="12" fill="#4a7bd0"/>
<text x="57" y="317" font-size="12">fork+wait</text>
<rect x="140" y="306" width="12" height="12" fill="#3f9a6b"/>
<text x="157" y="317" font-size="12">vfork+wait</text>
<rect x="245" y="306" width="12" height="12" fill="#e08a3c"/>
<text x="262" y="317" font-size="12">pthread_create+join</text>
<rect x="420" y="306" width="12" height="12" fill="#888"/>
<text x="437" y="317" font-size="12">posix_spawn(/bin/true)</text>
</svg>
```

그림 6 — 직접 잰 생성+회수 1회 비용. fork만 부모 RSS에 비례해서 커지고, 주소 공간을 복사하지 않는 vfork·pthread_create·posix_spawn은 부모 크기와 무관하다.

### 10.2 해석

- **fork는 RSS에 비례**한다. 데이터는 COW로 공유해도 page table 항목(1 GiB면 PTE 26만 개)을 복사하고 모두 쓰기 금지로 바꿔야 하기 때문이다. 0 → 1024 MiB에서 약 4배.
- **pthread_create는 부모 크기와 무관**하다. `CLONE_VM`이라 page table을 복사하지 않는다. 스택 `mmap`, TLS 설정, task 생성 정도가 비용이다.
- **posix_spawn은 일정**하다. 내부적으로 `CLONE_VM|CLONE_VFORK`라 복사가 없고, 대부분의 시간은 `/bin/true`를 exec하고 동적 링크하는 비용이다. "큰 프로세스에서 외부 명령 실행"이면 1024 MiB에서 이미 fork+exec보다 빠르다(fork 299 us + exec 비용).
- 절대값은 VM(Apple Silicon 위 가상화)이라 실제 보드와 다르다. Cortex-A53급 보드에서는 수 배 느릴 수 있다. **비교와 추세**를 보는 측정이다.

실무 결론: 1초에 수백 번 "작업 단위"를 만들 일이 있으면 스레드 풀을 미리 만들어 재사용하고, 큰 데몬에서 외부 명령은 `posix_spawn`으로 실행한다. 반대로 하루 몇 번 재시작되는 워커 프로세스라면 비용은 무시해도 되고 격리의 이점이 크다.

---

## 11. 데몬, 프로세스 그룹, 세션

### 11.1 용어 세 개

- **프로세스 그룹(process group, PGID)**: 셸 파이프라인 하나(`cat log` 파이프 `grep err`)가 한 그룹이다. Ctrl-C의 SIGINT는 **그룹 전체**에 간다. `kill -TERM -<pgid>`처럼 음수 PID로 그룹 전체에 시그널을 보낼 수 있다.
- **세션(session, SID)**: 로그인 하나에 해당하는 그룹들의 묶음. 세션은 **제어 터미널(controlling terminal)** 하나를 가질 수 있고, 그 터미널이 닫히면 SIGHUP이 간다.
- **데몬(daemon)**: 제어 터미널 없이 백그라운드에서 사는 서비스 프로세스 (`sshd`, `systemd-journald`, 우리의 센서 데몬).

### 11.2 고전적인 daemonize (double fork + setsid)

1. `fork()` 후 부모는 종료 → 셸에 즉시 돌아감, 자식은 프로세스 그룹 리더가 아님이 보장된다.
2. 자식이 `setsid()` → 새 세션·새 그룹의 리더가 되고 제어 터미널과 끊어진다.
3. 한 번 더 `fork()` 후 중간 프로세스 종료 → 손자는 세션 리더가 아니므로 나중에 터미널을 열어도 제어 터미널을 다시 얻지 않는다. 손자는 고아가 되어 init(또는 subreaper)이 입양한다.
4. `chdir("/")`(마운트 해제 방해 방지), `umask` 설정, stdin/stdout/stderr를 `/dev/null`로.

### 11.3 예제 13 — daemonize 후 PGID·SID·TTY 확인

```c
// ex13: classic double-fork daemonize, then look at PGID / SID / TTY
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

int main(void) {
    printf("launcher: pid=%d pgid=%d sid=%d\n", getpid(), getpgrp(), getsid(0));
    fflush(stdout);
    pid_t p = fork();
    if (p < 0) { perror("fork"); exit(1); }
    if (p > 0) {                         /* launcher waits for 1st child, then exits */
        waitpid(p, NULL, 0);
        sleep(1);
        char cmd[96];
        snprintf(cmd, sizeof cmd, "ps -o pid,ppid,pgid,sid,tty,stat,cmd -C ex13_daemon");
        fflush(stdout);
        if (system(cmd) < 0) perror("system");
        return 0;
    }
    if (setsid() < 0) { perror("setsid"); _exit(1); } /* new session, no ctty */
    p = fork();                           /* 2nd fork: not a session leader */
    if (p < 0) { perror("fork"); _exit(1); }
    if (p > 0) _exit(0);                  /* 1st child exits -> daemon orphaned */
    umask(022);
    if (chdir("/") < 0) { perror("chdir"); _exit(1); }
    printf("daemon  : pid=%d ppid=%d pgid=%d sid=%d\n",
           getpid(), getppid(), getpgrp(), getsid(0));
    fflush(stdout);
    int fd = open("/dev/null", O_RDWR);   /* detach stdio */
    if (fd < 0) { perror("open"); _exit(1); }
    dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
    sleep(3);                             /* "work" */
    _exit(0);
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex13_daemon.c -o ex13_daemon && ./ex13_daemon; sleep 3'
```

```text
launcher: pid=187182 pgid=187151 sid=187151
daemon  : pid=187184 ppid=1 pgid=187183 sid=187183
    PID    PPID    PGID     SID TT       STAT CMD
 187182  187151  187151  187151 ?        S    ./ex13_daemon
 187184       1  187183  187183 ?        S    ./ex13_daemon
```

출력에서 볼 것: (1) launcher는 자기를 실행한 셸(187151)의 그룹·세션에 속해 있다. (2) 데몬은 PPID 1(init에 입양), PGID·SID가 187183 — 이미 종료한 **중간 프로세스**(`setsid`를 부른 쪽)의 번호다. 그래서 데몬 자신(187184)은 세션 리더가 아니다. 이것이 두 번째 fork의 목적이다. (3) `limactl shell`은 터미널 없이 실행되어 launcher의 TTY도 `?`이지만, 터미널에서 실행했다면 launcher만 `pts/0` 같은 TTY를 갖는다.

### 11.4 요즘 방식 — systemd에 맡긴다

systemd 기반 장비(대부분의 Yocto·Debian·Ubuntu 이미지)에서는 위 과정을 **하지 않는 것이 정석**이다. 프로그램은 포그라운드로 실행되고 stdout/stderr에 로그를 쓰면, systemd가 프로세스 관리·재시작·로그(journald)·cgroup 정리를 다 해 준다. 예를 들어 센서 데몬 unit 파일:

```text
[Unit]
Description=IMU sensor daemon
After=local-fs.target

[Service]
Type=simple
ExecStart=/usr/bin/imu-daemon --rate 1000
Restart=on-failure
RestartSec=1
CPUAffinity=2
CPUSchedulingPolicy=fifo
CPUSchedulingPriority=50

[Install]
WantedBy=multi-user.target
```

`Type=simple`이면 systemd가 fork+exec한 프로세스 자체가 서비스다. `Restart=on-failure`는 비정상 종료(0이 아닌 종료 코드, 시그널) 시 재시작한다 — 그 판단 근거가 바로 4절의 wait status다. `CPUAffinity`, `CPUSchedulingPolicy`는 9절의 손잡이를 설정 파일로 거는 것이다. systemd는 L1에서, BusyBox init 환경은 C7·K2에서 다룬다.

---

## 12. 커널 안에서는 무슨 일이 일어나나

### 12.1 syscall에서 커널 함수까지

유저 API 하나하나가 커널의 어느 함수로 이어지는지 지도부터 보자 (6.x ~ 7.0 기준, 함수 이름은 `kernel/fork.c`, `fs/exec.c`, `kernel/exit.c`).

```
fork() / pthread_create()
  -> clone / clone3 syscall
     -> kernel_clone(args)
        -> copy_process()
             dup_task_struct()        new task_struct + kernel stack
             copy_files()   CLONE_FILES ? refcount++ : dup_fd()
             copy_fs(), copy_sighand(), copy_signal()
             copy_mm()      CLONE_VM    ? refcount++ : dup_mm() -> dup_mmap()
                                          (copies VMAs + PTEs, marks private pages RO = COW)
             copy_thread()  child regs, x0 = 0 on arm64
             alloc_pid()
        -> wake_up_new_task()       state = TASK_RUNNING, enqueue on a CPU
        -> (CLONE_VFORK) wait_for_vfork_done()   parent sleeps killable ("D")

execve()
  -> do_execveat_common() -> bprm_execve()
     -> exec_binprm() -> search_binary_handler() -> load_elf_binary()
        -> begin_new_exec()   kill other threads, exec_mmap(): drop old mm,
                               close O_CLOEXEC fds, reset signal handlers
        -> map PT_LOAD segments, set up stack (argv, envp, auxv), start_thread()

exit() / _exit()
  -> exit_group -> do_group_exit() -> do_exit()
     -> exit_mm(), exit_files(), exit_fs() ...  release resources
     -> exit_notify()   reparent children (forget_original_parent -> find_new_reaper),
                        send SIGCHLD, state = EXIT_ZOMBIE (or auto-reap)
     -> do_task_dead() -> __schedule()   never returns

wait4() / waitpid()
  -> kernel_wait4() -> do_wait()  sleeps on current->signal->wait_chldexit
     -> wait_task_zombie() -> copy status to user -> release_task()   PID freed
```

몇 가지 연결 고리:

- **COW의 실체**: `dup_mmap()`이 부모의 각 VMA를 복사하면서 private 쓰기 가능 페이지의 PTE를 부모·자식 모두 쓰기 금지로 만든다. 나중의 write fault는 `handle_mm_fault()` → `do_wp_page()`(write-protect fault 처리)에서 페이지를 복사한다. 상세는 A5와 F9.
- **좀비의 실체**: `exit_notify()` 이후 `task_struct`는 `EXIT_ZOMBIE` 상태로 남아 `release_task()`를 기다린다. 부모가 `SIGCHLD`를 `SIG_IGN`으로 설정했으면 `exit_notify()`가 곧바로 자동 회수 경로를 탄다.
- **reparenting의 실체**: `find_new_reaper()`가 먼저 같은 스레드 그룹의 살아 있는 다른 스레드, 그다음 `PR_SET_CHILD_SUBREAPER`가 설정된 조상, 마지막으로 PID namespace의 init을 고른다. 4.5절 실험이 이 순서 그대로다.
- **D 상태의 실체**: 드라이버가 `wait_event(wq, cond)`를 부르면 매크로가 `current`를 `TASK_UNINTERRUPTIBLE`로 바꿔 wait queue에 넣고 `schedule()`을 부른다. 다른 곳(보통 IRQ 핸들러)이 `wake_up(&wq)`를 부를 때까지 runqueue에 없다. `_interruptible` 버전은 `TASK_INTERRUPTIBLE`로 잠들고 깨어난 뒤 `signal_pending(current)`를 확인해 `-ERESTARTSYS`를 돌려준다.

### 12.2 드라이버를 쓸 때 다시 만나는 지점

| 이 노트의 개념 | 드라이버/커널에서 다시 나오는 곳 | 노트 |
|---|---|---|
| `task_struct`, `current` | 드라이버 함수 안의 `current->pid`, `current->comm`, 권한 검사 | F1, G2 |
| S vs D 상태 | `wait_event_interruptible` vs `wait_event`, `-ERESTARTSYS` 처리 | F3 |
| blocking `read()`가 S로 잠드는 것 | 드라이버 `.read`에서 데이터가 없으면 wait queue에서 잠들고 IRQ가 `wake_up` | F3, G2 |
| 커널 스레드 (`mm == NULL`) | `kthread_run()`으로 만든 폴링 스레드, `kworker`(workqueue) | F7 |
| 스레드 간 공유 데이터 레이스 | spinlock / mutex / atomic, 프로세스 컨텍스트 vs 인터럽트 컨텍스트 | F4 |
| 우선순위, SCHED_FIFO | threaded IRQ 핸들러는 SCHED_FIFO 50으로 도는 커널 스레드 | F6, N2 |
| `clone` 플래그 공유 | `files_struct` → fd → `struct file` → 드라이버의 `file_operations` | A3, G2 |

Don의 RTOS 경험과 연결하면: RTOS에서 "센서 태스크가 세마포어를 기다리다 ISR이 give하면 깬다"였던 패턴이, Linux에서는 "유저 스레드가 `read()`로 들어와 드라이버의 wait queue에서 S 상태로 잠들고, IRQ 핸들러가 `wake_up_interruptible()`로 깨운다"가 된다. 잠드는 주체가 **유저 스레드의 `task_struct`**라는 점만 다르다.

---

## 13. Embedded 실무에서 다시 보기

### 13.1 센서 키트 프로세스 구조 정하기

로봇 센서 키트(IMU 1 kHz, 카메라 30 fps, GNSS 10 Hz, CAN 모터 500 Hz, LiDAR UDP)를 예로 들면 흔한 설계는 이렇다.

| 구성 요소 | 프로세스/스레드 | 이유 |
|---|---|---|
| IMU 읽기 + 필터 | 한 프로세스 안 스레드 2개, 읽기 스레드는 SCHED_FIFO + affinity | 같은 링버퍼 공유, 지터 최소화 |
| 카메라 캡처 | 별도 프로세스 | 크고 복잡한 라이브러리(V4L2, GStreamer), 크래시 격리 |
| GNSS 파서 | 별도 프로세스 | 외부 입력 파싱 = 버그 위험, 죽어도 재시작 |
| CAN 모터 제어 | 별도 프로세스, SCHED_FIFO 높은 우선순위 | 안전 관련, 다른 컴포넌트와 격리 |
| 슈퍼바이저 | systemd 또는 subreaper 프로세스 | 재시작 정책, 상태 보고 |

### 13.2 예제 14 — 작은 슈퍼바이저: 크래시하면 재시작

systemd가 없는 최소 이미지(BusyBox)에서는 직접 슈퍼바이저를 짜기도 한다. 워커를 `posix_spawnp`로 띄우고, 비정상 종료면 백오프 후 재시작하고, 3번을 넘으면 포기한다.

```c
// ex14: tiny supervisor — spawn a worker, restart it when it crashes (max 3)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>

extern char **environ;

static pid_t start_worker(int gen) {
    char script[96];
    /* stand-in for a sensor daemon: runs briefly, then "crashes" (SIGSEGV) */
    snprintf(script, sizeof script, "echo \"worker gen %d pid $$ up\"; sleep 0.2; kill -SEGV $$", gen);
    char *argv[] = { "sh", "-c", script, NULL };
    pid_t pid;
    int e = posix_spawnp(&pid, "sh", NULL, NULL, argv, environ);
    if (e) { fprintf(stderr, "posix_spawnp: %s\n", strerror(e)); exit(1); }
    return pid;
}

int main(void) {
    int restarts = 0;
    pid_t w = start_worker(0);
    for (;;) {
        int st;
        if (waitpid(w, &st, 0) < 0) { perror("waitpid"); exit(1); }
        if (WIFEXITED(st) && WEXITSTATUS(st) == 0) { puts("worker finished cleanly"); break; }
        if (WIFSIGNALED(st))
            printf("supervisor: worker %d died by signal %d\n", w, WTERMSIG(st));
        else
            printf("supervisor: worker %d exited %d\n", w, WEXITSTATUS(st));
        if (++restarts > 3) { puts("supervisor: too many restarts, giving up"); return 1; }
        fflush(stdout);
        usleep(100000 * restarts);           /* simple linear backoff */
        w = start_worker(restarts);
    }
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A2 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A2/ex14_supervisor.c -o ex14_supervisor && ./ex14_supervisor; echo "exit=$?"'
```

```text
worker gen 0 pid 201004 up
supervisor: worker 201004 died by signal 11
worker gen 1 pid 201006 up
supervisor: worker 201006 died by signal 11
worker gen 2 pid 201008 up
supervisor: worker 201008 died by signal 11
worker gen 3 pid 201010 up
supervisor: worker 201010 died by signal 11
supervisor: too many restarts, giving up
exit=1
```

출력에서 볼 것: 매 세대 새 PID로 재시작되고, 종료 이유(시그널 11)를 wait status로 정확히 구분한다. 실제 제품이라면 여기에 재시작 횟수 로그, 하드웨어 워치독 kick 중단(I9), 상태 LED 같은 정책이 붙는다.

### 13.3 "kill이 안 되는 프로세스" 현장 디버깅

현장에서 카메라 앱이 멈추고 `kill -9`도 안 듣는다는 리포트가 왔다고 하자. 순서:

```sh
ps -eo pid,stat,wchan:32,cmd | grep -i cam     # STAT D? wchan은?
sudo cat /proc/<pid>/stack                      # 커널 스택: 어느 드라이버 함수에서 잠들었나
dmesg | grep -i "blocked for more than"         # hung task 경고가 있었나
```

wchan이나 스택에 `vb2_...`, `wait_for_completion`, 특정 ISP 드라이버 함수가 보이면 "하드웨어 완료 인터럽트를 타임아웃 없이 기다리는 드라이버"가 원인이다. 유저 공간에서 할 수 있는 일은 없고, 드라이버의 대기를 timeout + interruptible로 고치는 것이 해법이다(F3).

### 13.4 스레드 이름은 디버깅의 반이다

스레드가 10개쯤 되는 데몬에서 `top -H`가 전부 `imu-daemon`으로 보이면 어떤 스레드가 CPU를 먹는지 알 수 없다. 스레드를 만들자마자 `pthread_setname_np(t, "imu-read")`를 부르는 습관을 들인다. 이름은 `/proc/<pid>/task/<tid>/comm`에 기록되고 `ps -L -o tid,comm,stat`, `top -H`, `perf`, 커널 hung task 메시지에 그대로 나온다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 자식을 wait하지 않음 | `ps`에 `<defunct>` 누적, 결국 fork가 `EAGAIN` | 종료한 자식의 `task_struct`가 회수 안 됨 | `waitpid`, SIGCHLD 핸들러에서 `WNOHANG` 루프, 또는 `SIG_IGN` |
| fork 전에 stdout flush 안 함 | 로그 줄이 두 번 찍힘 (파일·파이프로 리다이렉트할 때만) | 사용자 공간 stdio 버퍼가 자식에 복사됨 | fork 전 `fflush(NULL)`, 자식은 `_exit` |
| exec 실패 후 자식이 `return` | 부모 코드가 두 벌로 계속 실행 | exec는 실패 시 반환함 | exec 다음 줄에 `perror` + `_exit(127)` |
| 멀티스레드에서 fork 후 복잡한 일 | 자식이 가끔 영원히 멈춤 | 다른 스레드가 잡고 있던 mutex가 잠긴 채 복사 | fork 직후 exec, 또는 `posix_spawn` |
| 큰 데몬에서 `system()` 남발 | 지연 스파이크, 메모리 압박 | fork가 RSS에 비례, COW로 페이지 복사 | `posix_spawn`, 또는 별도 작은 헬퍼 프로세스 |
| 공유 변수를 `volatile`로만 보호 | 카운터 값이 틀림, 재현 어려움 | `++`는 load-add-store, 원자적 아님 | atomic 또는 mutex (A8) |
| `pthread_create` 에러를 errno로 확인 | 에러가 무시되거나 엉뚱한 메시지 | pthread 함수는 에러 코드를 **반환** | `int e = pthread_create(...); if (e) strerror(e)` |
| 스레드에서 `exit()` 호출 | 프로세스 전체가 갑자기 종료 | `exit`는 `exit_group`, 모든 스레드 종료 | 스레드는 `return` 또는 `pthread_exit` |
| 드라이버에서 타임아웃 없는 `wait_event` | 앱이 D 상태로 멈추고 `kill -9` 불가, hung task 경고 | `TASK_UNINTERRUPTIBLE` 무한 대기 | `wait_event_interruptible_timeout` + `-ERESTARTSYS` 처리 |
| SCHED_FIFO 스레드가 busy loop | 같은 CPU의 다른 일이 굶음, 셸 무응답 | RT는 스스로 block할 때까지 실행 | block하는 구조로, affinity로 격리, RT throttling 이해 |

---

## 15. 면접에서 이렇게 말한다

**Q.** What is the difference between a process and a thread in Linux?

**A.** 프로세스는 자기만의 주소 공간과 fd 테이블 같은 자원을 가진 단위이고, 스레드는 그 자원을 공유하면서 스택·레지스터만 따로 가지는 실행 흐름이다. 커널 안에서는 둘 다 `task_struct`이고 `clone()` 플래그(`CLONE_VM`, `CLONE_FILES`, `CLONE_THREAD` …)로 무엇을 공유할지가 정해질 뿐이다. 그래서 스케줄러는 스레드 하나하나를 독립된 task로 스케줄한다.

> A process owns an address space and resources like the file descriptor table; threads share those and only have their own stack, registers and TID. Inside the kernel both are a task_struct — the difference is just which clone flags were used, like CLONE_VM, CLONE_FILES and CLONE_THREAD. The scheduler schedules each thread as an independent task. In practice I use processes for isolation and restartability, and threads when components need to share memory cheaply.

**꼬리 질문**: "그럼 `getpid()`는 스레드마다 다른가?" → 같다. `getpid()`는 TGID, 스레드마다 다른 건 `gettid()`.

**Q.** How does fork work, and why is it fast?

**A.** fork는 호출한 프로세스를 복제해서 부모에겐 자식 PID, 자식에겐 0을 돌려준다. 메모리를 실제로 복사하지 않고 page table만 복사한 뒤 양쪽을 읽기 전용으로 표시하는 copy-on-write를 쓴다. 누가 쓰면 그 페이지만 page fault 경로에서 복사된다. 그래도 page table 복사 비용은 남아서, 제가 측정해 보니 부모 RSS 0에서 1 GiB로 늘리면 fork+wait가 약 73 us에서 300 us로 늘었다.

> fork duplicates the calling process; it returns the child's PID to the parent and zero to the child. It doesn't copy memory up front — it copies the page tables and marks private pages read-only in both, so a write triggers a page fault that copies just that page. That's copy-on-write. The page table copy still scales with RSS: when I measured it, fork plus wait went from about 70 microseconds to about 300 as the parent grew from zero to one gigabyte, which is why large daemons should use posix_spawn.

**Q.** What is a zombie process and how do you get rid of it?

**A.** 이미 종료했지만 부모가 아직 wait하지 않아 종료 상태를 담은 `task_struct`가 남아 있는 프로세스다. 이미 죽었으니 kill로는 못 없앤다. 부모가 `waitpid`로 회수하게 고치거나, SIGCHLD를 `SIG_IGN`으로 두거나, 부모가 죽으면 init이 입양해 회수한다. 고아는 반대로 부모가 먼저 죽은 살아 있는 프로세스이고 init이나 subreaper가 입양한다.

> A zombie has already exited, but its parent hasn't called wait yet, so the kernel keeps the task_struct with the exit status. You can't kill it because it's already dead. The fix is in the parent: reap with waitpid, usually in a SIGCHLD handler looping with WNOHANG, or set SIGCHLD to SIG_IGN. If the parent dies, the zombie gets reparented to init or a subreaper, which reaps it. Zombies matter because each one holds a PID.

**Q.** A process is stuck and kill -9 doesn't work. What's going on?

**A.** 거의 확실히 D 상태, 즉 uninterruptible sleep이다. 커널 코드가 시그널에 깨지 않는 대기를 하고 있어서 SIGKILL도 대기 중으로만 남는다. `ps`의 STAT과 WCHAN, `/proc/<pid>/stack`, dmesg의 hung task 경고로 어느 드라이버의 어느 대기인지 찾는다. 보통 하드웨어 완료를 타임아웃 없이 기다리는 드라이버가 원인이라 드라이버를 interruptible/killable + timeout으로 고친다.

> It's almost certainly in D state, uninterruptible sleep — kernel code is waiting in a way that signals can't interrupt, so SIGKILL just stays pending. I'd check STAT and WCHAN in ps, read /proc/pid/stack, and look for hung-task warnings in dmesg to find which driver wait it's stuck in. The usual root cause is a driver waiting for a hardware completion without a timeout; the fix is wait_event_interruptible or killable with a timeout, and propagating -ERESTARTSYS.

**Q.** What is the difference between fork and exec? Why are they separate?

**A.** fork는 프로세스를 새로 만들고, exec는 현재 프로세스 안의 프로그램을 바꾼다(PID 유지, 성공 시 반환 없음). 둘이 분리되어 있어서 fork와 exec 사이에 자식에서 fd 리다이렉션, 시그널 마스크, uid, affinity 설정 같은 준비를 할 수 있다. 셸의 파이프와 리다이렉션이 이 틈으로 구현된다. 그 준비를 표준화한 것이 `posix_spawn`이다.

> fork creates a new process; exec replaces the program running in the current process — same PID, and it doesn't return on success. Keeping them separate lets the child set things up in between: redirect file descriptors, change signal masks, drop privileges, set CPU affinity. That's exactly how shells implement pipes and redirection. posix_spawn packages the common setups without the cost of copying the address space.

**Q.** How would you make a sensor-reading thread more deterministic on Linux?

**A.** 먼저 `SCHED_FIFO`로 적절한 RT 우선순위를 주고, `pthread_setaffinity_np`로 한 코어에 고정한다. 가능하면 그 코어를 `isolcpus`로 비워 두고 IRQ affinity도 옮긴다. `mlockall`로 page fault를 막는다. 그리고 `/proc/<pid>/task/<tid>/status`의 nonvoluntary context switch와 cyclictest 같은 도구로 실제로 선점·지연이 줄었는지 측정한다.

> I'd give it SCHED_FIFO with an appropriate priority, pin it to a core with pthread_setaffinity_np, and ideally isolate that core and move IRQs away. I'd mlockall to avoid page faults at runtime. Then I'd verify with numbers: the thread's nonvoluntary context-switch count in /proc should stay flat, and cyclictest or timestamps on each sample show the latency distribution. Without measurement it's just guessing.

**꼬리 질문**: "SCHED_FIFO 스레드가 무한 루프를 돌면?" → 같은 CPU의 일반 task가 굶는다. RT throttling(기본 1초 중 0.95초)이 최소한의 안전장치.

**Q.** What do the voluntary and nonvoluntary context switch counters tell you?

**A.** voluntary는 task가 스스로 block해서 CPU를 내놓은 횟수, nonvoluntary는 실행 중에 선점당한 횟수다. 실험에서 CPU hog는 2초에 voluntary 0, nonvoluntary 약 800이었고 sleeper는 voluntary 1550, nonvoluntary 1이었다. RT 스레드의 nonvoluntary가 늘면 누군가 CPU를 빼앗고 있다는 뜻이고, voluntary가 폭증하면 락 경합이나 잘게 쪼개진 I/O를 의심한다.

> Voluntary switches count how often the task blocked on its own — sleep, I/O, a lock. Nonvoluntary counts how often it was preempted while runnable. In a quick test a CPU hog showed zero voluntary and about 800 nonvoluntary switches in two seconds, while a sleeper had 1550 voluntary and one nonvoluntary. For a real-time thread, a rising nonvoluntary count means something is stealing its CPU.

---

## 16. 직접 해보기

1. (개념) `fork()`를 3번 연달아 부르는 코드(`fork(); fork(); fork();`)는 프로세스를 몇 개 만드나? 각 프로세스가 `printf("x\n")`를 한 번씩 하면 몇 줄이 나오나? 출력을 파이프로 보내면서 `fflush`가 없다면 어떻게 달라질 수 있나?
   정답/힌트: 총 8개(원본 포함), 8줄. `printf`가 fork 전에 있었다면 버퍼 복제로 줄 수가 늘어난다(1.4절).
2. (코드) 예제 1을 고쳐 자식 하나가 `raise(SIGSTOP)`로 스스로 멈추게 하고, 부모가 `waitpid(..., WUNTRACED)`로 정지를 감지한 뒤 `SIGCONT`를 보내 끝까지 회수하게 만들어라.
   정답/힌트: `WIFSTOPPED`/`WSTOPSIG`로 정지 감지 → `kill(pid, SIGCONT)` → 다시 `waitpid`.
3. (관찰) 아무 멀티스레드 프로그램(예: 예제 8 변형)을 띄우고 `/proc/<pid>/task/*/comm`과 `/proc/<pid>/task/*/status`의 `voluntary_ctxt_switches`를 스레드별로 출력하는 셸 한 줄을 만들어라.
   정답/힌트: `for t in /proc/<pid>/task/*; do echo $(cat $t/comm) $(grep ^voluntary $t/status); done`
4. (코드) SIGCHLD 핸들러에서 `waitpid(-1, &st, WNOHANG)` 루프로 자식을 회수하는 프로그램을 짜고, 자식 10개가 거의 동시에 죽어도 좀비가 하나도 남지 않는지 `ps`로 확인하라. 루프 대신 `waitpid`를 한 번만 부르면 어떻게 되는지도 비교하라.
   정답/힌트: 시그널은 합쳐지므로 한 번만 부르면 좀비가 남을 수 있다. 상세는 A6.
5. (관찰) `strace -f -e trace=clone,clone3,execve,wait4 sh -c 'ls | wc -l'`을 실행하고 셸이 파이프라인을 위해 몇 번 clone하고 어느 프로세스가 무엇을 exec하는지 그려 보라.
   정답/힌트: 셸이 명령마다 자식을 만들고(clone, flags에 SIGCHLD) 각 자식이 `ls`, `wc`를 exec한다. 자식 사이를 pipe fd가 잇는다(A7).
6. (측정) 예제 10에 "스레드 풀 재사용"(미리 만든 스레드에 condvar로 일을 넘기기) 경로를 추가해 1회 작업 디스패치 비용을 재고 `pthread_create+join`과 비교하라.
   정답/힌트: 디스패치는 보통 수 us 수준으로 생성보다 훨씬 싸다. condvar는 A8.
7. (개념) 왜 `kill -9`는 좀비에도, D 상태 task에도 즉시 효과가 없나? 두 경우의 이유를 각각 한 문장으로 말하라.
   정답/힌트: 좀비는 이미 죽어서 받을 주체가 없고(부모의 wait만 필요), D 상태는 커널이 시그널 확인을 하지 않는 대기에 있어서 깨어날 때까지 pending으로만 남는다.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| process | 프로세스 | 자기 주소 공간과 자원(fd 테이블 등)을 가진 실행 단위 |
| thread | 스레드 | 주소 공간을 공유하고 스택·레지스터만 따로 가진 실행 흐름 |
| task | 태스크 | 커널의 스케줄 단위, `task_struct` 하나 |
| `task_struct` | 태스크 구조체 | 커널이 task마다 유지하는 상태·자원 포인터 묶음 (TCB에 해당) |
| PID / TGID / TID | 프로세스·스레드 그룹·스레드 ID | 유저 PID = 커널 tgid, 유저 TID = 커널 pid |
| fork | 포크 | 현재 프로세스를 복제해 자식을 만드는 syscall (실제로는 clone) |
| copy-on-write (COW) | 쓰기 시 복사 | 페이지를 공유하다가 쓰는 순간 그 페이지만 복사 |
| exec | 실행 이미지 교체 | 현재 프로세스에 새 프로그램을 적재, PID 유지 |
| vfork | 브이포크 | 주소 공간을 빌려 쓰고 부모를 멈추는 fork, exec 직전용 |
| posix_spawn | 포직스 스폰 | 복사 없이 자식 생성 + exec을 한 번에 |
| wait status | 종료 상태 | 정상 종료 코드 또는 시그널 번호가 packed된 정수, `W*` 매크로로 해석 |
| zombie | 좀비 | 종료했지만 부모가 회수하지 않은 task, `Z`, `<defunct>` |
| orphan | 고아 | 부모가 먼저 죽은 살아 있는 프로세스, init이나 subreaper가 입양 |
| subreaper | 서브리퍼 | `PR_SET_CHILD_SUBREAPER`로 자손 고아를 입양하는 프로세스 |
| uninterruptible sleep (D) | 인터럽트 불가 대기 | 시그널로 깨지 않는 커널 대기, `kill -9`도 대기 |
| wchan | wait channel | task가 잠들어 있는 커널 함수 이름 |
| nice | 나이스 값 | fair 클래스에서 CPU 몫을 정하는 가중치 (-20 ~ 19) |
| SCHED_FIFO | 실시간 FIFO 정책 | 고정 우선순위 선점형, block할 때까지 실행 |
| CPU affinity | CPU 친화도 | task가 실행될 수 있는 CPU 집합 |
| voluntary / nonvoluntary ctxt switch | 자발적 / 비자발적 문맥 전환 | 스스로 block한 횟수 / 선점당한 횟수 |
| process group / session | 프로세스 그룹 / 세션 | 작업 제어 단위 / 로그인·제어 터미널 단위 |
| daemon | 데몬 | 제어 터미널 없이 백그라운드에서 사는 서비스 프로세스 |
| `current` | 현재 태스크 | 커널 코드에서 지금 실행 중인 task의 `task_struct` 포인터 |

---

## 18. 요약 & 체크리스트

Linux에서 프로세스는 자기 주소 공간과 자원을 가진 격리 단위이고, 스레드는 그것을 공유하는 실행 흐름이며, 커널에게는 둘 다 `clone()` 플래그만 다른 `task_struct`다. 프로세스는 `fork`(COW로 page table만 복사)로 만들고 `exec`로 프로그램을 바꾸며, 부모는 반드시 `wait`로 종료 상태를 회수해야 좀비가 남지 않는다. 부모가 먼저 죽으면 고아는 init이나 subreaper에 입양된다. task 상태 중 D(uninterruptible sleep)는 드라이버의 타임아웃 없는 대기에서 생기며 `kill -9`가 안 듣는 원인이다. 유저 공간에서는 nice, SCHED_FIFO, affinity로 스케줄링에 개입하고, `/proc/<pid>/status`의 context switch 카운터로 결과를 측정한다. 생성 비용은 fork만 RSS에 비례하므로 큰 프로세스에서는 `posix_spawn`, 잦은 작업에는 스레드 풀을 쓴다.

- [ ] 프로세스·스레드·RTOS 태스크의 차이를 주소 공간과 공유 자원 기준으로 설명할 수 있다
- [ ] fork + exec + waitpid로 자식을 실행하고 `W*` 매크로로 종료 이유를 해석하는 코드를 짤 수 있다
- [ ] copy-on-write가 어떻게 동작하고 fork 비용이 왜 RSS에 비례하는지 설명할 수 있다
- [ ] fork 전 `fflush`, exec 실패 후 `_exit(127)`이 필요한 이유를 말할 수 있다
- [ ] 좀비와 고아를 구분하고, 좀비를 막는 방법 세 가지와 subreaper를 설명할 수 있다
- [ ] R, S, D, T, Z 상태와 WCHAN을 `ps`로 읽고, D 상태가 드라이버의 어떤 대기에서 생기는지 말할 수 있다
- [ ] PID/TGID/TID의 관계를 알고 `gettid`, `/proc/<pid>/task`로 스레드를 찾을 수 있다
- [ ] pthread create/join/detach와 errno가 스레드별인 이유, thread-safe 함수 대체(`_r`)를 설명할 수 있다
- [ ] fork와 pthread_create가 `clone` 플래그 차이일 뿐이라는 것을 strace로 보여 줄 수 있다
- [ ] nice와 SCHED_FIFO의 차이를 설명하고 `chrt`, `taskset`, `sched_setaffinity`를 쓸 수 있다
- [ ] voluntary / nonvoluntary context switch 카운터로 지연 원인을 추정할 수 있다
- [ ] double fork + setsid 데몬과 systemd `Type=simple` 서비스의 차이를 말할 수 있다

---

## 참고 자료

- man7.org: [fork(2)](https://man7.org/linux/man-pages/man2/fork.2.html), [execve(2)](https://man7.org/linux/man-pages/man2/execve.2.html), [wait(2)](https://man7.org/linux/man-pages/man2/wait.2.html), [clone(2)](https://man7.org/linux/man-pages/man2/clone.2.html), [pthreads(7)](https://man7.org/linux/man-pages/man7/pthreads.7.html), [gettid(2)](https://man7.org/linux/man-pages/man2/gettid.2.html), [proc_pid_status(5)](https://man7.org/linux/man-pages/man5/proc_pid_status.5.html), [sched(7)](https://man7.org/linux/man-pages/man7/sched.7.html), [sched_setaffinity(2)](https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html), [prctl(2)](https://man7.org/linux/man-pages/man2/prctl.2.html), [posix_spawn(3)](https://man7.org/linux/man-pages/man3/posix_spawn.3.html), [credentials(7)](https://man7.org/linux/man-pages/man7/credentials.7.html), [ps(1)](https://man7.org/linux/man-pages/man1/ps.1.html)
- Michael Kerrisk, "The Linux Programming Interface" — 24~28장(프로세스 생성·종료·감시·exec), 29~33장(스레드), 34장(프로세스 그룹·세션·작업 제어), 35장(우선순위·스케줄링), 37장(데몬)
- OSTEP (Operating Systems: Three Easy Pieces) — 5장 "Process API", 26장 "Concurrency: An Introduction"
- 커널 문서: [Scheduler (docs.kernel.org)](https://docs.kernel.org/scheduler/index.html), [EEVDF Scheduler](https://docs.kernel.org/scheduler/sched-eevdf.html)
- 커널 소스 (elixir.bootlin.com): `kernel/fork.c` (`kernel_clone`, `copy_process`), `fs/exec.c` (`begin_new_exec`), `kernel/exit.c` (`do_exit`, `exit_notify`, `find_new_reaper`, `do_wait`), `include/linux/sched.h` (`struct task_struct`), `include/linux/wait.h` (`wait_event*`)
- Bootlin, "Embedded Linux system development" 교육 슬라이드 (bootlin.com/training/embedded-linux)
