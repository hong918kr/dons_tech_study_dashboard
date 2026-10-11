# A1. Linux 전체 구조와 "everything is a file" — 커널·유저 경계, syscall, VFS, /proc·/sys·/dev 한 장 지도

> **이 노트를 다 읽으면**: 유저 공간과 커널 공간이 CPU·MMU 수준에서 어떻게 갈리는지 설명할 수 있다 · arm64에서 syscall 한 번이 `svc #0`부터 `eret`까지 어떤 길을 가는지 그릴 수 있다 · `/dev`·`/proc`·`/sys`에서 원하는 정보를 찾아 읽을 수 있다 · `open("/dev/xyz")`가 드라이버의 `file_operations`까지 가는 경로를 말할 수 있다
> **왜 필요한가**: 면접 첫 질문 "open()을 부르면 무슨 일이 일어나나요?", "user space와 kernel space의 차이는?"의 답이고, 실무에서 장치가 안 보일 때 `/dev`→`/sys`→`dmesg` 순으로 따라가는 감각의 출발점이다
> **Don 기준 난이도**: 예외·특권 모드·MMU·인터럽트는 이미 안다 / Linux가 그것들을 어떻게 "프로세스·파일·syscall"이라는 추상으로 포장하는지가 새로 배울 부분
> **선행 노트**: 없음

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 만든 SSD 펌웨어를 떠올려 보자. 부팅하면 하나의 이미지가 Cortex-R 위에서 돌고, NVMe 명령 처리 태스크·NAND 스케줄러·에러 처리 코드가 **모두 같은 특권 모드, 같은 주소 공간**에서 레지스터를 직접 만진다. 어떤 태스크가 포인터를 잘못 쓰면 다른 태스크의 자료구조가 깨지고, 그 디버깅은 JTAG으로 한다.

Linux는 정반대 철학이다. 하드웨어를 직접 만지는 코드(**커널**)와 일반 프로그램(**유저 프로세스**)을 CPU 특권 레벨과 MMU로 **물리적으로 갈라 놓는다**. 유저 프로그램은 레지스터를 만질 수 없고, 다른 프로세스의 메모리를 볼 수도 없다. 무언가 필요하면 **syscall**(system call, 커널에 일을 부탁하는 정해진 입구)로 커널에 요청한다. 그리고 그 요청의 대부분은 "파일"이라는 하나의 모양으로 통일되어 있다 — 센서도, 시리얼 포트도, 파이프도, 네트워크 소켓도 `open`/`read`/`write`/`close`로 다룬다. 이것이 **"everything is a file"**이다.

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="230" y="22" text-anchor="middle" font-size="14" font-weight="bold">Linux (Cortex-A)</text>
  <rect x="20" y="32" width="420" height="96" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="50" font-size="13">유저 공간 — EL0</text>
  <rect x="30" y="58" width="120" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="90" y="77" text-anchor="middle" font-size="13">센서 데몬</text>
  <rect x="160" y="58" width="120" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="220" y="77" text-anchor="middle" font-size="13">카메라 앱</text>
  <rect x="290" y="58" width="140" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="360" y="77" text-anchor="middle" font-size="13">systemd</text>
  <rect x="30" y="94" width="400" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="230" y="112" text-anchor="middle" font-size="13">libc (glibc / musl) — syscall 래퍼</text>
  <line x1="20" y1="140" x2="440" y2="140" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="200" y="157" text-anchor="middle" font-size="13">시스템 콜 경계 (svc #0 / eret)</text>
  <line x1="400" y1="120" x2="400" y2="190" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="20" y="165" width="420" height="190" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="183" font-size="13">커널 공간 — EL1</text>
  <rect x="140" y="170" width="250" height="22" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="265" y="186" text-anchor="middle" font-size="12">System Call Interface</text>
  <rect x="30" y="200" width="76" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="68" y="218" text-anchor="middle" font-size="13">프로세스</text>
  <text x="68" y="236" text-anchor="middle" font-size="12">sched</text>
  <rect x="111" y="200" width="76" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="149" y="218" text-anchor="middle" font-size="13">메모리</text>
  <text x="149" y="236" text-anchor="middle" font-size="12">MM</text>
  <rect x="192" y="200" width="76" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="230" y="218" text-anchor="middle" font-size="13">VFS</text>
  <text x="230" y="236" text-anchor="middle" font-size="12">fs</text>
  <rect x="273" y="200" width="76" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="311" y="218" text-anchor="middle" font-size="13">네트워크</text>
  <text x="311" y="236" text-anchor="middle" font-size="12">net</text>
  <rect x="354" y="200" width="76" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="392" y="218" text-anchor="middle" font-size="13">IPC</text>
  <text x="392" y="236" text-anchor="middle" font-size="12">pipe·shm</text>
  <rect x="30" y="252" width="400" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="230" y="271" text-anchor="middle" font-size="13">드라이버 (char · block · net · I2C · SPI)</text>
  <rect x="30" y="288" width="400" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="230" y="307" text-anchor="middle" font-size="13">arch/arm64 (예외 벡터 · MMU · GIC)</text>
  <text x="230" y="340" text-anchor="middle" font-size="12">하나의 커널 이미지 + 필요 시 모듈(.ko)</text>
  <rect x="20" y="370" width="420" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="230" y="403" text-anchor="middle" font-size="13">하드웨어: CPU · MMU · GIC · UART · I2C · eMMC</text>
  <text x="560" y="22" text-anchor="middle" font-size="14" font-weight="bold">Bare-metal (SSD FW)</text>
  <rect x="460" y="32" width="200" height="323" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="470" y="60" width="180" height="230" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="140" text-anchor="middle" font-size="13">하나의 이미지</text>
  <text x="560" y="162" text-anchor="middle" font-size="13">모두 특권 모드</text>
  <text x="560" y="184" text-anchor="middle" font-size="13">(SVC / Handler)</text>
  <text x="560" y="206" text-anchor="middle" font-size="13">MPU는 선택</text>
  <text x="560" y="320" text-anchor="middle" font-size="13">함수 호출로 HW 접근</text>
  <rect x="460" y="370" width="200" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="560" y="403" text-anchor="middle" font-size="13">같은 하드웨어</text>
</svg>
```
그림 1 — Linux는 유저 공간(EL0)과 커널 공간(EL1)을 시스템 콜 경계로 가른다. Don의 bare-metal 펌웨어는 모든 코드가 한 특권 모드에서 돈다.

Embedded Linux 엔지니어의 하루는 이 그림의 경계를 오가는 일이다.

- **센서 키트 bring-up**: IMU 드라이버를 올렸는데 `/dev/iio:device0`이 안 생긴다 → `/sys/bus/i2c/devices/`에 디바이스가 있는지, 드라이버가 bind 됐는지 본다 (이 노트 §7).
- **성능 이슈**: 카메라 앱이 프레임을 놓친다 → `strace -c`로 syscall 횟수를 세고, 프레임마다 syscall을 몇 번 하는지 본다 (§4, §5).
- **드라이버 작성**: 내가 쓴 `file_operations.read`가 유저의 `read()`와 어떻게 이어지는지 알아야 테스트 코드를 짠다 (§10, G4).

이 노트는 그 전체 지도를 한 번에 그린다. 각 부분의 깊은 설명은 A2~A10, E, F, G 모듈에서 다시 한다.

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-34-generic`, gcc 15.2, Apple Silicon 위 가상화)에서 실제로 돌린 결과다. PID·주소·inode 번호·시간은 실행마다 다르다.

---

## 1. 유저 공간 vs 커널 공간 — CPU 특권과 MMU 격리

### 1.1 직관 — "같은 CPU, 다른 권한"

Linux에서 유저 프로그램과 커널은 **같은 CPU 코어에서 번갈아** 돈다. 다른 점은 딱 두 가지다.

1. **CPU 특권 레벨**: 커널은 EL1(Exception Level 1), 유저는 EL0에서 돈다. EL0에서는 시스템 레지스터 쓰기, 인터럽트 마스킹, 캐시·TLB 관리 같은 명령이 막혀 있다.
2. **MMU 페이지 권한**: 페이지 테이블 엔트리마다 "EL0가 접근 가능한가" 비트(AP 비트)가 있다. 커널 메모리는 EL1 전용으로 매핑되어 있어서 유저가 주소를 알아도 못 읽는다.

Don에게 익숙한 말로 바꾸면: **유저 프로세스 = MPU(정확히는 MMU)로 격리된 비특권 태스크 + 자기만의 주소 공간**이고, **커널 = 특권 모드에서 도는 RTOS 커널 + 모든 드라이버**다.

### 1.2 ARM Exception Level 정리

| EL | 누가 도나 | Don의 경험과 대응 |
|---|---|---|
| EL0 | 유저 프로세스 (앱, 데몬, 셸) | Cortex-M의 unprivileged Thread mode와 비슷 (단 MMU로 주소 공간까지 분리) |
| EL1 | Linux 커널 (드라이버 포함) | Cortex-R의 SVC/IRQ 모드, Cortex-M의 Handler mode에서 돌던 펌웨어 전체 |
| EL2 | 하이퍼바이저 (KVM, Xen) | 지금 이 VM의 호스트 쪽 |
| EL3 | Secure monitor (TF-A BL31) | 부트 ROM 다음 단계, PSCI 전원 관리 (C5에서 다룸) |

arm64 Linux는 가상 주소 공간도 반으로 쪼갠다. 아래쪽(`0x0000...`)은 **TTBR0**가 가리키는 프로세스별 페이지 테이블(유저), 위쪽(`0xffff...`)은 **TTBR1**이 가리키는 커널 공용 페이지 테이블이다. context switch 때 TTBR0만 바뀌고 커널 쪽은 그대로다. 그래서 syscall로 커널에 들어가도 페이지 테이블을 바꿀 필요가 없다.

```
 64-bit virtual address (arm64, 48-bit VA, 4K pages)

 0xffff_ffff_ffff_ffff  +---------------------------+
                        |  kernel (TTBR1, EL1 only) |
 0xffff_8000_0000_0000  +---------------------------+
                        |   (non-canonical hole)    |
 0x0000_ffff_ffff_ffff  +---------------------------+
                        |  user (TTBR0, per-process)|
 0x0000_0000_0000_0000  +---------------------------+
```

### 1.3 코드로 확인 — 유저가 커널 주소를 읽거나 특권 명령을 쓰면?

무엇을 확인하나: EL0에서 커널 가상 주소를 읽는 것과 `msr daifset`(bare-metal의 `__disable_irq()`)을 실행하는 것이 각각 어떻게 막히는지 본다.

```c
/* ex0: EL0(유저)에서 커널 주소 읽기 / 특권 명령 실행 → 커널이 시그널로 막는다 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void child_kaddr(void)
{
    volatile unsigned long *p = (unsigned long *)0xffff800080000000UL; /* 커널 VA 영역 */
    printf("child: reading %p ...\n", (void *)p);
    fflush(stdout);
    unsigned long v = *p;                 /* EL0에서 커널 페이지 접근 */
    printf("never printed %lu\n", v);
}

static void child_priv(void)
{
    printf("child: msr daifset, #2 (IRQ mask) ...\n");
    fflush(stdout);
    asm volatile("msr daifset, #2");      /* bare-metal에선 __disable_irq() */
    printf("never printed\n");
}

static void run(void (*fn)(void))
{
    fflush(stdout);                       /* fork 전에 stdio 버퍼 비우기 (A2) */
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid == 0) { fn(); _exit(0); }
    int st;
    if (waitpid(pid, &st, 0) < 0) { perror("waitpid"); exit(1); }
    if (WIFSIGNALED(st))
        printf("parent: child killed by signal %d (%s)\n",
               WTERMSIG(st), strsignal(WTERMSIG(st)));
    else
        printf("parent: child exited %d\n", WEXITSTATUS(st));
}

int main(void)
{
    run(child_kaddr);
    run(child_priv);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /tmp/A1 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A1/ex0_priv.c -o /tmp/A1/ex0_priv && /tmp/A1/ex0_priv'
```

```text
child: reading 0xffff800080000000 ...
parent: child killed by signal 11 (Segmentation fault)
child: msr daifset, #2 (IRQ mask) ...
parent: child killed by signal 4 (Illegal instruction)
```

출력에서 볼 것: 커널 주소 읽기는 MMU **permission fault** → 커널의 fault 핸들러 → `SIGSEGV`(11). 특권 명령은 CPU가 EL1로 trap → 커널이 `SIGILL`(4)로 바꿔 보낸다. bare-metal에서는 HardFault/Data Abort 핸들러에서 시스템 전체가 멈췄을 일이, Linux에서는 **그 프로세스 하나만 죽고 끝난다**. 이게 격리의 가치다.

> 함정 메모: 처음 이 코드를 `fflush(stdout)` 없이 돌렸을 때 "parent: ... signal 11" 줄이 두 번 찍혔다. stdout이 파이프면 full buffering이라, 부모 버퍼에 남은 내용이 `fork()`로 자식에게 복사되고 자식이 그것을 다시 flush했기 때문이다. A2에서 자세히 본다.

### 1.4 커널이 유저 메모리를 다룰 때

반대 방향도 규칙이 있다. 커널(EL1)은 기술적으로 유저 주소를 읽을 수 있지만, 아무 포인터나 믿고 읽으면 안 된다 (유저가 커널 주소를 넘길 수도 있고, 페이지가 아직 없을 수도 있다). 그래서 커널은 반드시 `copy_from_user()`/`copy_to_user()`로 유저 버퍼에 접근한다. arm64는 PAN(Privileged Access Never) 기능으로 "EL1이 실수로 유저 메모리를 직접 건드리면 fault"가 나게까지 막는다. 드라이버의 `.read`/`.write`에서 이 함수를 쓰는 이유가 이것이다 (E6, G4).

---

## 2. 커널이 하는 일 — 6개의 큰 덩어리

커널은 "하드웨어를 독점하고, 그것을 안전한 추상화로 나눠 주는 관리자"다. 큰 덩어리는 여섯 개다.

| 서브시스템 | 하는 일 | 유저가 보는 모습 | Don의 bare-metal 대응 | 깊이 다루는 곳 |
|---|---|---|---|---|
| 프로세스·스케줄러 | 태스크 생성·종료, CPU 시간 배분, context switch | `fork`, `exec`, `ps`, `nice`, `/proc/<pid>` | RTOS 태스크 + 스케줄러 | A2, F1 |
| 메모리 관리 (MM) | 페이지 테이블, 물리 페이지 할당, page fault, 스왑·OOM | `mmap`, `malloc`, `/proc/<pid>/maps` | MPU 설정 + 고정 메모리 맵 | A5, F9, F11 |
| VFS·파일시스템 | 모든 "파일"에 같은 API 제공, ext4·FAT 같은 FS | `open`/`read`/`write`, 디렉터리 트리 | FTL 위 블록 접근, 단 파일 개념 없음 | A3, F12, J |
| 디바이스 드라이버 | 하드웨어 레지스터·인터럽트·DMA를 다룬다 | `/dev/xxx`, `/sys/class/xxx`, 네트워크 인터페이스 | Don이 쓰던 펌웨어 코드 대부분 | G, H, I |
| 네트워크 | TCP/IP 스택, 소켓, CAN, 라우팅 | `socket`, `ip`, `ss` | 거의 없음 (NVMe-oF 정도) | A9, I |
| IPC | 프로세스 간 통신·동기화 | pipe, UNIX socket, shm, futex, signal | 큐·세마포어·이벤트 플래그 | A6, A7, A8 |

여기에 **시스템 콜 인터페이스**(위 여섯 개로 들어가는 입구)와 **arch 코드**(예외 벡터, MMU, GIC 같은 CPU 종속 부분)가 붙는다. 그림 1의 가운데가 바로 이 표다.

---

## 3. syscall 메커니즘 (arm64)

### 3.1 직관 — "SVC 예외로 특권 모드에 들어가는 함수 호출"

Don은 Cortex-M에서 `SVC` 명령으로 RTOS 커널 서비스를 부르는 걸 봤을 것이다. Linux syscall도 정확히 그것이다. arm64 규칙은 다음과 같다.

| 항목 | arm64 Linux 규칙 |
|---|---|
| 진입 명령 | `svc #0` (즉시값은 무시, 항상 0을 쓴다) |
| syscall 번호 | `x8` 레지스터 |
| 인자 1~6 | `x0`~`x5` |
| 반환값 | `x0` (성공이면 0 이상, 실패면 `-errno`, 즉 -4095 ~ -1) |
| 복귀 명령 | 커널이 `eret`으로 EL0 복귀 |
| 번호 정의 | `asm-generic/unistd.h` (arm64는 generic 테이블을 쓴다. x86_64와 번호가 다르다) |

VM의 헤더에서 직접 확인하자.

```sh
grep -E "define __NR_(read|write|openat|close|ioctl|getpid|mmap|clock_gettime|exit_group) " /usr/include/asm-generic/unistd.h
```

```text
#define __NR_ioctl 29
#define __NR_openat 56
#define __NR_close 57
#define __NR_read 63
#define __NR_write 64
#define __NR_exit_group 94
#define __NR_clock_gettime 113
#define __NR_getpid 172
#define __NR_mmap __NR3264_mmap
```

참고로 arm64에는 `open` syscall이 아예 없다. libc의 `open()`은 내부에서 `openat(AT_FDCWD, ...)`(번호 56)을 부른다. 아래 strace 출력에서 `openat`만 보이는 이유다.

### 3.2 코드로 확인 — 같은 write를 세 가지 방법으로

무엇을 확인하나: libc 래퍼 `write()`, 범용 래퍼 `syscall()`, 그리고 레지스터를 직접 채우고 `svc #0`을 쏘는 인라인 어셈블리가 모두 같은 일을 한다는 것.

```c
/* ex1: 같은 write를 세 가지 방법으로 — libc 래퍼, syscall(), 인라인 svc #0 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

static long raw_write(int fd, const void *buf, unsigned long len)
{
    register long x0 asm("x0") = fd;     /* 인자 1 → x0 */
    register long x1 asm("x1") = (long)buf; /* 인자 2 → x1 */
    register long x2 asm("x2") = len;    /* 인자 3 → x2 */
    register long x8 asm("x8") = __NR_write; /* syscall 번호 → x8 */
    asm volatile("svc #0"                /* EL0 → EL1 예외 */
                 : "+r"(x0)
                 : "r"(x1), "r"(x2), "r"(x8)
                 : "memory");
    return x0;                           /* 반환값도 x0 */
}

int main(void)
{
    const char *a = "1) libc write()\n";
    const char *b = "2) syscall(SYS_write, ...)\n";
    const char *c = "3) inline asm svc #0\n";

    ssize_t r1 = write(1, a, strlen(a));
    long r2 = syscall(SYS_write, 1, b, strlen(b));
    long r3 = raw_write(1, c, strlen(c));

    printf("__NR_write=%d __NR_getpid=%d __NR_openat=%d\n",
           __NR_write, __NR_getpid, __NR_openat);
    printf("return: %zd %ld %ld\n", r1, r2, r3);
    return 0;
}
```

```text
1) libc write()
2) syscall(SYS_write, ...)
3) inline asm svc #0
__NR_write=64 __NR_getpid=172 __NR_openat=56
return: 16 27 21
```

출력에서 볼 것: 세 방법 모두 쓴 바이트 수(16, 27, 21)를 돌려준다. libc의 `write()`도 결국 `x8=64`를 넣고 `svc #0`을 하는 얇은 래퍼일 뿐이다.

### 3.3 libc 래퍼 vs `syscall()` vs 직접 svc

| 방법 | 언제 쓰나 | 주의 |
|---|---|---|
| libc 래퍼 (`write`, `open`, `ioctl`) | 평소 99% | 일부 래퍼는 추가 일을 한다 (`open` → `openat` 변환, 취소 지점 처리 등) |
| `syscall(SYS_xxx, ...)` | libc에 래퍼가 아직 없는 새 syscall (예전의 `gettid`, 최신 커널 기능) | 실패 시 -1 + `errno`로 바꿔 주는 건 똑같다 |
| 인라인 `svc #0` | 교육용, libc 없는 초소형 init, 셸코드 | 반환값이 `-errno` 그대로다. `errno`가 설정되지 않는다 |

### 3.4 반환값 규약 — 커널은 `-errno`, libc는 `-1` + `errno`

커널 안의 함수들은 실패를 **음수 에러 코드**로 돌려준다 (`return -ENOENT;`). 드라이버를 쓸 때도 똑같이 `return -EINVAL;`처럼 쓴다. 이 값이 그대로 `x0`에 실려 유저로 온다. libc 래퍼는 `x0`가 -4095~-1 범위면 "에러"로 보고, `errno = -x0`로 저장한 뒤 `-1`을 반환한다.

무엇을 확인하나: 같은 `openat`을 libc로 부를 때와 직접 `svc`로 부를 때 반환값이 어떻게 다른지.

```c
/* ex2: 커널은 -errno를 돌려주고, libc가 그것을 -1 + errno로 바꾼다 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

static long raw_openat(int dirfd, const char *path, int flags)
{
    register long x0 asm("x0") = dirfd;
    register long x1 asm("x1") = (long)path;
    register long x2 asm("x2") = flags;
    register long x3 asm("x3") = 0;
    register long x8 asm("x8") = __NR_openat;
    asm volatile("svc #0" : "+r"(x0)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory");
    return x0;
}

static void show(const char *path, int flags)
{
    errno = 0;
    int fd = open(path, flags);
    int saved = errno;
    long raw = raw_openat(AT_FDCWD, path, flags);
    printf("%-22s libc: ret=%2d errno=%2d (%s)\n", path, fd, saved,
           saved ? strerrorname_np(saved) : "-");
    printf("%-22s raw : ret=%ld\n", "", raw);
    if (fd >= 0) close(fd);
    if (raw >= 0) close((int)raw);
}

int main(void)
{
    show("/etc/hostname", O_RDONLY);       /* 성공 */
    show("/no/such/file", O_RDONLY);       /* ENOENT = 2 */
    show("/etc/shadow", O_RDONLY);         /* EACCES = 13 (일반 유저) */
    show("/etc", O_WRONLY);                /* EISDIR = 21 */
    return 0;
}
```

```text
/etc/hostname          libc: ret= 3 errno= 0 (-)
                       raw : ret=4
/no/such/file          libc: ret=-1 errno= 2 (ENOENT)
                       raw : ret=-2
/etc/shadow            libc: ret=-1 errno=13 (EACCES)
                       raw : ret=-13
/etc                   libc: ret=-1 errno=21 (EISDIR)
                       raw : ret=-21
```

출력에서 볼 것: 커널이 실제로 돌려준 값은 `-2`, `-13`, `-21`이다. libc는 그것을 `-1`과 `errno = 2, 13, 21`로 바꿨다. 성공한 경우 fd가 3, 4로 늘어나는 것도 보자 — 새 fd는 항상 **비어 있는 가장 작은 번호**다.

> 함정: `errno`는 **실패했을 때만** 의미가 있다. 성공한 호출은 `errno`를 0으로 지우지 않는다. "`errno != 0`이면 에러"라고 판단하면 틀린다. 반환값을 먼저 본다.

### 3.5 syscall 한 번의 전체 경로

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="8" y="56" width="330" height="336" fill="#4a7bd0" fill-opacity="0.06" stroke="none"/>
  <rect x="342" y="56" width="330" height="336" fill="#e08a3c" fill-opacity="0.06" stroke="none"/>
  <text x="16" y="386" font-size="12">EL0</text>
  <text x="640" y="386" font-size="12">EL1</text>
  <rect x="20" y="16" width="140" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="90" y="37" text-anchor="middle" font-size="13">앱 (main)</text>
  <rect x="180" y="16" width="140" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="37" text-anchor="middle" font-size="13">libc 래퍼</text>
  <rect x="350" y="16" width="140" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="420" y="37" text-anchor="middle" font-size="13">커널 진입부</text>
  <rect x="520" y="16" width="140" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="590" y="37" text-anchor="middle" font-size="13">sys_openat</text>
  <line x1="90" y1="48" x2="90" y2="380" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="4 4"/>
  <line x1="250" y1="48" x2="250" y2="380" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="4 4"/>
  <line x1="420" y1="48" x2="420" y2="380" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="4 4"/>
  <line x1="590" y1="48" x2="590" y2="380" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="4 4"/>
  <line x1="90" y1="80" x2="248" y2="80" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="170" y="74" text-anchor="middle" font-size="12">open(path, flags)</text>
  <text x="244" y="112" text-anchor="end" font-size="12">x0~x3 인자, x8=56</text>
  <line x1="250" y1="145" x2="418" y2="145" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="335" y="139" text-anchor="middle" font-size="12">svc #0 (동기 예외)</text>
  <text x="428" y="178" font-size="12">el0t_64_sync → el0_svc</text>
  <text x="428" y="205" font-size="12">table[x8] 호출</text>
  <line x1="420" y1="235" x2="588" y2="235" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="505" y="229" text-anchor="middle" font-size="12">__arm64_sys_openat</text>
  <rect x="535" y="250" width="130" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="600" y="268" text-anchor="middle" font-size="12">VFS → 드라이버</text>
  <line x1="590" y1="305" x2="422" y2="305" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <text x="505" y="299" text-anchor="middle" font-size="12">x0 = fd 또는 -errno</text>
  <line x1="420" y1="335" x2="252" y2="335" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <text x="335" y="329" text-anchor="middle" font-size="12">eret (EL0 복귀)</text>
  <line x1="250" y1="365" x2="92" y2="365" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <text x="170" y="359" text-anchor="middle" font-size="12">fd 또는 -1 + errno</text>
</svg>
```
그림 2 — arm64에서 `open()` 한 번의 왕복. 왼쪽 두 레인은 EL0, 오른쪽 두 레인은 EL1이다. 함수 이름은 커널 7.0에서 ftrace로 확인한 것(§10).

순서를 말로 쓰면:

1. 앱이 `open(path, flags)` 호출 → libc가 `x0=AT_FDCWD, x1=path, x2=flags, x3=mode, x8=56`을 채운다.
2. `svc #0` → CPU가 **동기 예외**를 일으키고 EL1으로 올라가 `VBAR_EL1` 벡터 테이블의 "Lower EL, AArch64, Synchronous" 엔트리로 점프한다. 이때 하드웨어가 복귀 주소를 `ELR_EL1`, 상태를 `SPSR_EL1`에 저장한다 (Don이 아는 예외 진입과 같다).
3. 커널 엔트리 코드(`el0t_64_sync`)가 유저 레지스터 전부를 커널 스택의 `struct pt_regs`에 저장한다.
4. `el0t_64_sync_handler` → `el0_svc` → `do_el0_svc` → `invoke_syscall`이 `sys_call_table[x8]`에서 핸들러를 찾아 부른다.
5. `__arm64_sys_openat` → VFS → (장치 파일이면) 드라이버의 `.open`.
6. 반환값을 `pt_regs->regs[0]`에 넣고, 레지스터 복원 후 `eret`. 돌아가기 직전 "다시 스케줄해야 하나?", "보낼 시그널이 있나?"를 확인한다 — 이것이 syscall 복귀가 **preemption·시그널 전달 지점**이 되는 이유다 (A6, F1).

### 3.6 /proc로 "지금 어떤 syscall 중인가" 보기

커널은 각 프로세스가 지금 어떤 syscall 안에 있는지 `/proc/<pid>/syscall`로 보여 준다. 자기 자신을 `cat`으로 읽어 보면:

```sh
cat /proc/self/syscall
```

```text
63 0x3 0xffffe68ef720 0x10000 0x0 0x0 0x0 0xffffe68ef700 0xe491035b8464
```

출력에서 볼 것: 첫 숫자 63은 `read`의 번호다 — `cat`이 이 파일을 `read()`하는 순간을 찍었다. 그 뒤는 `x0`~`x5` 인자(fd 3, 버퍼 주소, 크기 0x10000 …), 유저 스택 포인터, 유저 PC다. 행(hang) 걸린 프로세스가 어디서 멈췄는지 볼 때 실제로 쓴다.

---

## 4. strace — syscall을 눈으로 보는 도구

`strace`는 `ptrace` syscall로 다른 프로세스의 syscall 진입·복귀마다 멈춰서 인자와 반환값을 찍는다. Don에게는 "모든 SVC 진입에 브레이크포인트를 건 JTAG"이라고 생각하면 된다.

무엇을 확인하나: 아주 작은 프로그램 하나가 실제로 몇 개의 syscall을 하는지, 그중 "내 코드"는 어디인지.

```c
/* ex3: strace로 볼 가장 작은 프로그램 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
int main(void)
{
    char buf[64];
    int fd = open("/etc/hostname", O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }
    ssize_t n = read(fd, buf, sizeof buf);
    if (n < 0) { perror("read"); return 1; }
    close(fd);
    printf("hostname: %.*s", (int)n, buf);
    return 0;
}
```

```sh
strace ./hello
```

```text
execve("./hello", ["./hello"], 0xffffef3f76c0 /* 19 vars */) = 0
brk(NULL)                               = 0xaee6dbf2c000
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0xfebf6073f000
faccessat(AT_FDCWD, "/etc/ld.so.preload", R_OK) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
...
openat(AT_FDCWD, "/usr/lib/aarch64-linux-gnu/libc.so.6", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\3\0\0\0\0\0\0\0\0\3\0\267\0\1\0\0\0\3101\2\0\0\0\0\0"..., 832) = 832
...
close(3)                                = 0
set_tid_address(0xfebf60740388)         = 31301
set_robust_list(0xfebf60740040, 24)     = 0
rseq(0xfebf60740740, 0x21, 0, 0xd428bc00) = 0
...
getrandom("\xc7\x3b\x02\x9f\x99\x87\xd7\x76", 8, GRND_NONBLOCK) = 8
munmap(0xfebf60739000, 24207)           = 0
openat(AT_FDCWD, "/etc/hostname", O_RDONLY) = 3
read(3, "lima-elinux\n", 64)            = 12
close(3)                                = 0
fstat(1, {st_mode=S_IFIFO|0600, st_size=0, ...}) = 0
brk(NULL)                               = 0xaee6dbf2c000
brk(0xaee6dc000000)                     = 0xaee6dc000000
write(1, "hostname: lima-elinux\n", 22hostname: lima-elinux
) = 22
exit_group(0)                           = ?
+++ exited with 0 +++
```

(동적 로더의 `mmap`/`mprotect` 줄 일부를 `...`로 생략했다. syscall은 모두 38개다 — 아래 `-c` 결과.)

출력에서 볼 것:

- 내 코드는 끝의 `openat` → `read` → `close` 세 줄과 `printf`가 만든 `write` 하나뿐이다. 앞의 대부분은 **동적 로더**(`ld-linux-aarch64.so.1`)가 `libc.so.6`을 찾아 `mmap`하는 과정이다 (B3에서 자세히).
- `fstat(1, ...) = S_IFIFO`: stdout이 터미널이 아니라 파이프다 (limactl이 출력을 파이프로 받기 때문). libc는 이걸 보고 stdout을 full buffering으로 정한다 — §1.3의 fork 함정과 같은 뿌리.
- `brk`: `printf`의 버퍼를 위해 libc의 `malloc`이 힙을 늘렸다.

요약 모드 `-c`는 syscall별 횟수를 센다:

```sh
strace -c ./hello > /dev/null
```

```text
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
  0.00    0.000000           0         1         1 ioctl
  0.00    0.000000           0         1         1 faccessat
  0.00    0.000000           0         3           openat
  0.00    0.000000           0         3           close
  0.00    0.000000           0         2           read
  0.00    0.000000           0         1           write
  0.00    0.000000           0         3           fstat
  0.00    0.000000           0         1           set_tid_address
  0.00    0.000000           0         1           set_robust_list
  0.00    0.000000           0         3           brk
  0.00    0.000000           0         3           munmap
  0.00    0.000000           0         1           execve
  0.00    0.000000           0         8           mmap
  0.00    0.000000           0         4           mprotect
  0.00    0.000000           0         1           prlimit64
  0.00    0.000000           0         1           getrandom
  0.00    0.000000           0         1           rseq
------ ----------- ----------- --------- --------- ----------------
100.00    0.000000           0        38         2 total
```

출력에서 볼 것: 이번에는 stdout을 `/dev/null`로 돌렸더니 실패한 `ioctl`이 하나 생겼다. libc가 "stdout이 터미널인가?"를 `ioctl(TCGETS)`로 물어봤고, `/dev/null`은 터미널이 아니라 `ENOTTY`로 실패한 것이다. 같은 프로그램도 **fd 1이 무엇에 연결됐는지**에 따라 syscall이 달라진다.

자주 쓰는 옵션 몇 개:

```sh
strace -e trace=openat,read,close -e raw=openat ./hello   # 특정 syscall만, openat은 날 숫자로
strace -T -e trace=openat,read ./hello                    # 각 syscall에 걸린 시간
```

```text
openat(0xffffffffffffff9c, 0xb52e11550ac8, 0, 0) = 0x3
read(3, "lima-elinux\n", 64)            = 12
close(3)                                = 0
hostname: lima-elinux
+++ exited with 0 +++

openat(AT_FDCWD, "/etc/hostname", O_RDONLY) = 3 <0.000028>
read(3, "lima-elinux\n", 64)            = 12 <0.000030>
hostname: lima-elinux
+++ exited with 0 +++
```

출력에서 볼 것: `-e raw`로 보면 `AT_FDCWD`는 사실 `-100`(`0xff...ff9c`)이고, 경로는 그냥 포인터다 — strace가 평소에 예쁘게 디코딩해 주던 것이다. `-T`의 시간(28 µs)은 strace의 `ptrace` 정지 비용이 섞여 있어서 실제보다 훨씬 크다. 진짜 비용은 다음 절처럼 직접 잰다.

> 실무 팁: `strace -f -p <PID>`로 이미 돌고 있는 데몬에 붙고(`-f`는 스레드·자식까지), `-e trace=%file`, `%network`, `%desc` 같은 그룹 필터를 쓴다. "장치를 못 연다"는 버그의 절반은 `strace`의 `openat(...) = -1 ENOENT/EACCES` 한 줄로 끝난다.

---

## 5. syscall은 얼마나 비싼가 — 직접 재 보기

### 5.1 측정 코드

무엇을 확인하나: 일반 함수 호출, syscall, vDSO, 실제 장치 파일 I/O의 1회 비용을 200만 번 반복해서 ns 단위로 비교한다.

```c
/* ex6: 함수 호출 vs syscall vs vDSO — 1회당 ns 측정 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

#define N 2000000
static __attribute__((noinline)) long nop_fn(long x) { asm volatile(""); return x + 1; }
static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}
#define BENCH(name, stmt) do {                         \
        double t0 = now_ns();                          \
        for (long i = 0; i < N; i++) { stmt; }         \
        printf("%-30s %7.1f ns\n", name, (now_ns() - t0) / N); \
    } while (0)

int main(void)
{
    struct timespec ts;
    char c;
    volatile long sink = 0;
    int zfd = open("/dev/zero", O_RDONLY), nfd = open("/dev/null", O_WRONLY);
    if (zfd < 0 || nfd < 0) { perror("open"); exit(1); }

    BENCH("function call (noinline)", sink += nop_fn(i));
    BENCH("getpid() libc", sink += getpid());
    BENCH("syscall(SYS_getpid)", sink += syscall(SYS_getpid));
    BENCH("clock_gettime() vDSO", clock_gettime(CLOCK_MONOTONIC, &ts));
    BENCH("syscall(SYS_clock_gettime)", syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts));
    BENCH("read(/dev/zero, 1B)", sink += read(zfd, &c, 1));
    BENCH("write(/dev/null, 1B)", sink += write(nfd, &c, 1));
    BENCH("open+close(/dev/null)", close(open("/dev/null", O_RDONLY)));
    return (int)(sink & 0);
}
```

세 번 돌린 결과 (첫 번째 실행을 그대로 싣고, 나머지 두 번도 ±10% 안이었다):

```text
function call (noinline)           0.9 ns
getpid() libc                    124.8 ns
syscall(SYS_getpid)              124.6 ns
clock_gettime() vDSO              18.9 ns
syscall(SYS_clock_gettime)       157.6 ns
read(/dev/zero, 1B)              166.6 ns
write(/dev/null, 1B)             161.6 ns
open+close(/dev/null)            669.8 ns
```

```svg
<svg viewBox="0 0 640 360" xmlns="http://www.w3.org/2000/svg">
  <text x="320" y="24" text-anchor="middle" font-size="13">1회 호출 비용 (ns, VM 실측, 짧을수록 빠름)</text>
  <line x1="210.0" y1="44" x2="210.0" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
  <line x1="264.3" y1="44" x2="264.3" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
  <line x1="318.6" y1="44" x2="318.6" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
  <line x1="372.9" y1="44" x2="372.9" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
  <line x1="427.1" y1="44" x2="427.1" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
  <line x1="481.4" y1="44" x2="481.4" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
  <line x1="535.7" y1="44" x2="535.7" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
  <line x1="590.0" y1="44" x2="590.0" y2="322" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="202" y="65" text-anchor="end" font-size="12">function call (noinline)</text>
  <rect x="210" y="50" width="1.5" height="22" fill="#888" fill-opacity="0.75"/>
  <text x="217.5" y="65" font-size="12">0.9 ns</text>
  <text x="202" y="99" text-anchor="end" font-size="12">clock_gettime() vDSO</text>
  <rect x="210" y="84" width="10.3" height="22" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="226.3" y="99" font-size="12">18.9 ns</text>
  <text x="202" y="133" text-anchor="end" font-size="12">getpid() libc</text>
  <rect x="210" y="118" width="67.7" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="283.7" y="133" font-size="12">124.8 ns</text>
  <text x="202" y="167" text-anchor="end" font-size="12">syscall(SYS_getpid)</text>
  <rect x="210" y="152" width="67.6" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="283.6" y="167" font-size="12">124.6 ns</text>
  <text x="202" y="201" text-anchor="end" font-size="12">syscall(SYS_clock_gettime)</text>
  <rect x="210" y="186" width="85.6" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="301.6" y="201" font-size="12">157.6 ns</text>
  <text x="202" y="235" text-anchor="end" font-size="12">write(/dev/null, 1B)</text>
  <rect x="210" y="220" width="87.7" height="22" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="303.7" y="235" font-size="12">161.6 ns</text>
  <text x="202" y="269" text-anchor="end" font-size="12">read(/dev/zero, 1B)</text>
  <rect x="210" y="254" width="90.4" height="22" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="306.4" y="269" font-size="12">166.6 ns</text>
  <text x="202" y="303" text-anchor="end" font-size="12">open+close(/dev/null)</text>
  <rect x="210" y="288" width="363.6" height="22" fill="#d0564a" fill-opacity="0.75"/>
  <text x="579.6" y="303" font-size="12">669.8 ns</text>
  <line x1="210" y1="322" x2="590" y2="322" stroke="currentColor"/>
  <text x="210.0" y="338" text-anchor="middle" font-size="12">0</text>
  <text x="264.3" y="338" text-anchor="middle" font-size="12">100</text>
  <text x="318.6" y="338" text-anchor="middle" font-size="12">200</text>
  <text x="372.9" y="338" text-anchor="middle" font-size="12">300</text>
  <text x="427.1" y="338" text-anchor="middle" font-size="12">400</text>
  <text x="481.4" y="338" text-anchor="middle" font-size="12">500</text>
  <text x="535.7" y="338" text-anchor="middle" font-size="12">600</text>
  <text x="590.0" y="338" text-anchor="middle" font-size="12">700</text>
  <text x="400" y="355" text-anchor="middle" font-size="12">ns / call</text>
</svg>
```
그림 3 — ex6 측정값. 가장 싼 syscall(`getpid`)도 함수 호출의 약 140배다. vDSO는 커널에 들어가지 않으므로 syscall보다 약 8배 빠르다.

### 5.2 숫자 읽는 법

- **함수 호출 0.9 ns vs `getpid` 125 ns**: `getpid`는 커널이 하는 일이 거의 없다 (`current->tgid` 읽기). 그런데도 125 ns가 드는 건 **경계를 넘는 비용** 때문이다: 예외 진입, 레지스터 31개 저장·복원, Spectre/Meltdown 계열 완화 코드, 복귀 전 확인(시그널·재스케줄), `eret`. 이것이 "syscall 하나의 고정 비용"이다.
- **libc `getpid()` = `syscall(SYS_getpid)`**: glibc 2.25부터 `getpid`를 캐시하지 않으므로 매번 진짜 syscall이다.
- **vDSO 19 ns**: `clock_gettime`은 커널이 모든 프로세스에 매핑해 주는 작은 공유 라이브러리 **vDSO**(virtual dynamic shared object) 안에서 처리된다. 커널이 시간 정보를 읽기 전용 페이지(vvar)에 계속 갱신해 두고, 유저는 거기서 값을 읽고 `CNTVCT_EL0`(ARM generic timer 카운터, EL0에서 읽기 허용)를 직접 읽어 보정한다. 경계를 안 넘으니 빠르다. 같은 일을 `syscall()`로 강제하면 158 ns.
- **`read(/dev/zero, 1B)` 167 ns**: 고정 비용 + VFS(`fdget`, 권한 검사, 드라이버 `.read_iter` 호출, 1바이트 `copy_to_user`). 1바이트씩 읽는 루프가 왜 나쁜지 보여 준다 — 1 MB를 1바이트씩 읽으면 syscall만 0.17초다.
- **`open+close` 670 ns**: 경로 탐색(`/` → `dev` → `null` dentry 조회), `struct file` 할당, 드라이버 `.open`, 그리고 `close`의 정리. **열기는 한 번, 읽기는 여러 번**이 원칙인 이유.

> 측정 주의: 이 값은 Apple Silicon 위 가상 머신의 게스트 커널(EL1)에서 잰 값이다. syscall 자체는 하이퍼바이저로 나가지 않으므로 경향은 실제 하드웨어와 같지만, 절대값은 CPU·클럭·커널 설정(완화 옵션)에 따라 크게 다르다. 더 느린 임베디드 코어(예: 저클럭 Cortex-A53)에서는 몇 배 커질 수 있으니, 타깃 보드에서 같은 코드를 돌려 확인하는 습관을 들이자.

### 5.3 Embedded 관점의 결론

- 센서 데이터를 1샘플마다 `read()` 하지 말고, 드라이버가 FIFO·버퍼를 모아 한 번에 넘기게 한다 (IIO buffer, V4L2 buffer queue, `mmap` 공유 버퍼 — A5, I).
- 타임스탬프는 `clock_gettime(CLOCK_MONOTONIC)`이 vDSO라서 싸다. 마음 놓고 써도 된다.
- 200 Hz IMU 루프에 syscall 몇 개는 아무 문제 없다 (200 × 1 µs = 0.02% CPU). 문제가 되는 건 **수십만 회/초** 단위다 — 고속 ADC, 10 GbE 패킷, 1바이트 UART 읽기 루프.

---

## 6. VFS와 "everything is a file"

### 6.1 직관 — 하나의 리모컨, 여러 기계

VFS(Virtual File System, 가상 파일 시스템)는 "파일처럼 생긴 모든 것"에 **같은 버튼(open/read/write/close/ioctl/mmap/poll)**을 달아 주는 커널 계층이다. 버튼을 누르면 실제로 무엇이 일어날지는 **그 파일 뒤에 있는 구현**(ext4, 파이프, 소켓, 드라이버)이 정한다. C로 말하면 "함수 포인터 테이블로 하는 다형성"이고, 그 테이블이 바로 `struct file_operations`다. Don이 펌웨어에서 `struct nand_ops { int (*read_page)(...); ... }` 같은 HAL 테이블을 만들어 본 적이 있다면 정확히 그 패턴이다.

그래서 "everything is a file"의 정확한 의미는 **"모든 것이 디스크 위 파일"이 아니라 "모든 것이 fd로 다뤄진다"**다. 예외도 있다 — 네트워크 인터페이스(`eth0`)는 `/dev`에 노드가 없고 소켓·netlink로 다룬다.

### 6.2 파일의 7가지 종류

| 종류 | `ls -l` 첫 글자 | `S_IFMT` 값 (8진) | 예 | 뒤에서 일하는 것 |
|---|---|---|---|---|
| 일반 파일 | `-` | `0100000` | `/etc/hostname` | 파일시스템 (ext4 등) |
| 디렉터리 | `d` | `0040000` | `/tmp` | 파일시스템 |
| 문자 디바이스 | `c` | `0020000` | `/dev/null`, `/dev/ttyS0`, `/dev/i2c-0` | char 드라이버 (`cdev`) |
| 블록 디바이스 | `b` | `0060000` | `/dev/vda`, `/dev/mmcblk0` | block 계층 + 블록 드라이버 |
| FIFO (이름 있는 파이프) | `p` | `0010000` | `mkfifo`로 만든 파일 | pipefs |
| 소켓 | `s` | `0140000` | `/run/systemd/journal/socket` | 네트워크 스택 (AF_UNIX) |
| 심볼릭 링크 | `l` | `0120000` | `/proc/self`, `/dev/stdin` | 경로 문자열 |

`st_mode`는 16비트 안에 **종류 4비트 + 특수 비트 3개 + 권한 9비트**를 담는다.

```
 st_mode (16 bits shown)

  15  14  13  12 | 11   10   9  |  8 7 6 | 5 4 3 | 2 1 0
 +---------------+--------------+--------+-------+-------+
 |  file type    | suid sgid stk|  user  | group | other |
 |  (S_IFMT)     |              |  rwx   |  rwx  |  rwx  |
 +---------------+--------------+--------+-------+-------+
   0010 FIFO   0020 CHR   0040 DIR   0060 BLK
   0100 REG    0120 LNK   0140 SOCK          (octal, x 0o1000)
```

### 6.3 코드로 확인 — 파일 종류와 major/minor

무엇을 확인하나: `lstat()`이 돌려주는 `st_mode`에서 종류 비트를 뽑고, 장치 파일이면 `st_rdev`에서 major/minor를 뽑는다.

```c
/* ex4: lstat()으로 파일 종류(S_IFMT 비트)와 major/minor를 출력 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

static const char *kind(mode_t m)
{
    switch (m & S_IFMT) {             /* 상위 4비트가 파일 종류 */
    case S_IFREG:  return "regular";
    case S_IFDIR:  return "directory";
    case S_IFCHR:  return "char dev";
    case S_IFBLK:  return "block dev";
    case S_IFIFO:  return "fifo/pipe";
    case S_IFSOCK: return "socket";
    case S_IFLNK:  return "symlink";
    default:       return "?";
    }
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (lstat(argv[i], &st) < 0) { perror(argv[i]); continue; }
        printf("%-28s mode=%07o S_IFMT=%07o %-10s",
               argv[i], (unsigned)st.st_mode,
               (unsigned)(st.st_mode & S_IFMT), kind(st.st_mode));
        if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
            printf(" major=%u minor=%u", major(st.st_rdev), minor(st.st_rdev));
        printf(" ino=%lu\n", (unsigned long)st.st_ino);
    }
    return 0;
}
```

```sh
mkfifo /tmp/A1/myfifo
./ex4_ftype /etc/hostname /tmp /dev/null /dev/zero /dev/tty /dev/tty0 /dev/ttyS0 /dev/vda /dev/vda1 \
            /tmp/A1/myfifo /run/systemd/journal/socket /proc/self /dev/stdin
```

```text
/etc/hostname                mode=0100644 S_IFMT=0100000 regular    ino=1332
/tmp                         mode=0041777 S_IFMT=0040000 directory  ino=1
/dev/null                    mode=0020666 S_IFMT=0020000 char dev   major=1 minor=3 ino=5
/dev/zero                    mode=0020666 S_IFMT=0020000 char dev   major=1 minor=5 ino=7
/dev/tty                     mode=0020666 S_IFMT=0020000 char dev   major=5 minor=0 ino=12
/dev/tty0                    mode=0020600 S_IFMT=0020000 char dev   major=4 minor=0 ino=14
/dev/ttyS0                   mode=0020660 S_IFMT=0020000 char dev   major=4 minor=64 ino=90
/dev/vda                     mode=0060660 S_IFMT=0060000 block dev  major=253 minor=0 ino=141
/dev/vda1                    mode=0060660 S_IFMT=0060000 block dev  major=253 minor=1 ino=142
/tmp/A1/myfifo               mode=0010664 S_IFMT=0010000 fifo/pipe  ino=388
/run/systemd/journal/socket  mode=0140666 S_IFMT=0140000 socket     ino=51
/proc/self                   mode=0120777 S_IFMT=0120000 symlink    ino=4026531840
/dev/stdin                   mode=0120777 S_IFMT=0120000 symlink    ino=175
```

출력에서 볼 것:

- 7종류가 전부 나왔다. `/tmp`의 `041777`에서 `1`은 sticky 비트(남의 파일 삭제 금지)다.
- `/dev/null`과 `/dev/zero`는 **같은 major 1**(드라이버 `mem`), minor만 3과 5로 다르다. 한 드라이버가 minor로 여러 장치를 구분한다.
- `/dev/ttyS0`(4:64)와 `/dev/tty0`(4:0)도 같은 major 4 — 둘 다 TTY 계층이다.
- 장치 파일 자체는 **데이터가 없다**. inode에 "major:minor = 어느 드라이버의 몇 번 장치"라는 주소만 들어 있다. `ls -l`의 크기 자리에 크기 대신 `1, 3`이 찍히는 이유다.

```sh
ls -l /etc/hostname /dev/null /dev/vda /tmp/A1/myfifo /run/systemd/journal/socket /proc/self
```

```text
crw-rw-rw- 1 root root   1, 3 Oct  5 22:38 /dev/null
brw-rw---- 1 root disk 253, 0 Oct  5 22:38 /dev/vda
-rw-r--r-- 1 root root     12 Oct  5 22:38 /etc/hostname
lrwxrwxrwx 1 root root      0 Oct  5 22:38 /proc/self -> 31949
srw-rw-rw- 1 root root      0 Oct  5 22:38 /run/systemd/journal/socket
prw-rw-r-- 1 donh donh      0 Oct  5 22:41 /tmp/A1/myfifo
```

### 6.4 major/minor — 장치 번호 체계

- **major**: "어느 드라이버인가". 커널이 major → 드라이버 테이블을 들고 있다.
- **minor**: "그 드라이버의 몇 번째 장치인가". 드라이버가 자기 마음대로 해석한다.
- 커널 내부 타입은 `dev_t`(32비트: major 12비트, minor 20비트), 매크로 `MAJOR()`, `MINOR()`, `MKDEV()`. 유저 쪽은 `<sys/sysmacros.h>`의 `major()`, `minor()`.

지금 커널에 등록된 major 목록은 `/proc/devices`에 있다 (일부 발췌).

```sh
cat /proc/devices
```

```text
Character devices:
  1 mem
  4 /dev/vc/0
  4 tty
  4 ttyS
  5 /dev/tty
  5 /dev/console
  5 /dev/ptmx
...
 89 i2c
...
136 pts
...
246 rtc
...
254 gpiochip
...

Block devices:
  7 loop
  8 sd
...
252 device-mapper
253 virtblk
254 mdp
259 blkext
```

(목록이 길어서 중간을 `...`로 생략했다.)

출력에서 볼 것: `89 i2c`가 `/dev/i2c-0`의 major, `253 virtblk`가 `/dev/vda`의 major다. 숫자가 큰 쪽(234~254 등)은 커널이 **동적으로 배정**한 것이다 — 드라이버가 `alloc_chrdev_region()`을 쓰면 빈 번호를 받는다 (G4). 그래서 "major 번호를 하드코딩하지 말라"는 규칙이 있다.

### 6.5 /dev 둘러보기 — char가 압도적으로 많다

```sh
ls -l /dev | cut -c1 | sort | uniq -c
```

```text
     13 b
    168 c
     14 d
      8 l
      1 t
```

(`t`는 `ls -l`의 첫 줄 "total 0"이다.)

```sh
ls -l /dev/null /dev/zero /dev/random /dev/urandom /dev/mem /dev/kmsg /dev/tty /dev/tty0 /dev/tty1 /dev/ttyS0 /dev/ttyS1 /dev/vda /dev/i2c-0 /dev/gpiochip0
```

```text
crw------- 1 root root    254,  0 Oct  5 22:38 /dev/gpiochip0
crw-rw---- 1 root i2c      89,  0 Oct  5 22:41 /dev/i2c-0
crw-r--r-- 1 root root      1, 11 Oct  5 22:38 /dev/kmsg
crw-r----- 1 root kmem      1,  1 Oct  5 22:38 /dev/mem
crw-rw-rw- 1 root root      1,  3 Oct  5 22:38 /dev/null
crw-rw-rw- 1 root root      1,  8 Oct  5 22:38 /dev/random
crw-rw-rw- 1 root tty       5,  0 Oct  5 22:38 /dev/tty
crw------- 1 root tty       4,  0 Oct  5 22:38 /dev/tty0
crw------- 1 root tty       4,  1 Oct  5 22:38 /dev/tty1
crw-rw---- 1 root dialout   4, 64 Oct  5 22:38 /dev/ttyS0
crw-rw---- 1 root dialout   4, 65 Oct  5 22:38 /dev/ttyS1
crw-rw-rw- 1 root root      1,  9 Oct  5 22:38 /dev/urandom
brw-rw---- 1 root disk    253,  0 Oct  5 22:38 /dev/vda
crw-rw-rw- 1 root root      1,  5 Oct  5 22:38 /dev/zero
```

출력에서 볼 것: `mem` 드라이버(major 1) 하나가 `mem`(1), `null`(3), `zero`(5), `random`(8), `urandom`(9), `kmsg`(11)를 다 제공한다. 권한·그룹(`dialout`, `i2c`, `disk`)도 장치마다 다르다 — 일반 유저가 `/dev/ttyS0`을 열려면 `dialout` 그룹에 들어가야 한다. (이 VM의 `/dev/i2c-0`과 `/dev/gpiochip0`은 실습용으로 `i2c-stub`, `gpio-sim` 모듈이 만든 가짜 장치다.)

### 6.6 char vs block — 한 줄 차이

| 구분 | char device | block device |
|---|---|---|
| 단위 | 바이트 스트림 (임의 크기) | 고정 크기 블록 (512B, 4KB) |
| 중간 계층 | 없음, 드라이버의 `file_operations` 직행 | page cache + block layer (I/O 스케줄러, bio) |
| 랜덤 접근 | 드라이버 마음 (대개 seek 의미 없음) | 항상 가능 |
| 예 | UART, I2C, GPIO, 센서, `/dev/null` | eMMC, SD, NVMe, virtio-blk |
| 드라이버 작성 | G4 | J 모듈 (Don의 SSD 경험이 쓰이는 곳) |

---

## 7. 세 개의 특수 파일시스템 — /proc, /sys, /dev

### 7.1 큰 그림

`/proc`, `/sys`, `/dev`는 디스크에 없다. 커널이 메모리에서 **읽는 순간 내용을 만들어 내는** 가상 파일시스템이다.

```sh
grep -E " (/proc|/sys|/dev|/dev/pts|/sys/kernel/tracing) " /proc/self/mounts
```

```text
devtmpfs /dev devtmpfs rw,nosuid,size=1670976k,nr_inodes=417744,mode=755,inode64 0 0
devpts /dev/pts devpts rw,nosuid,noexec,relatime,gid=5,mode=600,ptmxmode=000 0 0
sysfs /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0
proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0
tracefs /sys/kernel/tracing tracefs rw,nosuid,nodev,noexec,relatime 0 0
```

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="15" width="210" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="36" text-anchor="middle" font-size="14" font-weight="bold">/proc (procfs)</text>
  <text x="115" y="56" text-anchor="middle" font-size="12">프로세스 · 커널 상태</text>
  <rect x="235" y="15" width="210" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="36" text-anchor="middle" font-size="14" font-weight="bold">/sys (sysfs)</text>
  <text x="340" y="56" text-anchor="middle" font-size="12">디바이스 모델 트리</text>
  <rect x="460" y="15" width="210" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="36" text-anchor="middle" font-size="14" font-weight="bold">/dev (devtmpfs)</text>
  <text x="565" y="56" text-anchor="middle" font-size="12">디바이스 노드 (I/O 입구)</text>
  <rect x="10" y="75" width="210" height="200" rx="6" fill="none" stroke="#4a7bd0"/>
  <text x="20" y="98" font-size="12">/proc/self → 내 PID</text>
  <text x="20" y="122" font-size="12">/proc/&lt;pid&gt;/maps, fd/</text>
  <text x="20" y="146" font-size="12">/proc/cpuinfo</text>
  <text x="20" y="170" font-size="12">/proc/interrupts</text>
  <text x="20" y="194" font-size="12">/proc/meminfo, /proc/devices</text>
  <text x="20" y="218" font-size="12">/proc/sys/ (sysctl 튜닝)</text>
  <text x="20" y="250" font-size="12">텍스트 표 · 여러 값</text>
  <rect x="235" y="75" width="210" height="200" rx="6" fill="none" stroke="#3f9a6b"/>
  <text x="245" y="98" font-size="12">/sys/devices/ (실제 트리)</text>
  <text x="245" y="122" font-size="12">/sys/bus/{i2c,spi,platform}</text>
  <text x="245" y="146" font-size="12">/sys/class/{net,tty,rtc}</text>
  <text x="245" y="170" font-size="12">/sys/dev/char/M:m → 링크</text>
  <text x="245" y="194" font-size="12">/sys/module/</text>
  <text x="245" y="218" font-size="12">/sys/kernel/tracing</text>
  <text x="245" y="250" font-size="12">속성 파일 1개 = 값 1개</text>
  <rect x="460" y="75" width="210" height="200" rx="6" fill="none" stroke="#e08a3c"/>
  <text x="470" y="98" font-size="12">/dev/null 1:3</text>
  <text x="470" y="122" font-size="12">/dev/ttyS0 4:64</text>
  <text x="470" y="146" font-size="12">/dev/i2c-0 89:0</text>
  <text x="470" y="170" font-size="12">/dev/vda 253:0 (block)</text>
  <text x="470" y="194" font-size="12">/dev/gpiochip0</text>
  <text x="470" y="218" font-size="12">/dev/pts/ (devpts)</text>
  <text x="470" y="250" font-size="12">open → read/write/ioctl</text>
  <rect x="10" y="300" width="210" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="115" y="325" text-anchor="middle" font-size="13">probe() → device_add()</text>
  <rect x="235" y="300" width="210" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="325" text-anchor="middle" font-size="13">sysfs 디렉터리 생성</text>
  <rect x="460" y="300" width="210" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="325" text-anchor="middle" font-size="13">devtmpfs 노드 생성</text>
  <line x1="220" y1="320" x2="233" y2="320" stroke="currentColor" marker-end="url(#ah3)"/>
  <path d="M115,340 L115,356 L565,356 L565,342" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="235" y="370" width="435" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="452" y="393" text-anchor="middle" font-size="13">uevent (netlink) → udev: 권한 · 이름 · 심볼릭 링크</text>
  <line x1="340" y1="340" x2="340" y2="368" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```
그림 4 — 세 가상 파일시스템의 역할. 드라이버가 장치를 등록하면(`device_add`) sysfs 디렉터리와 `/dev` 노드가 동시에 생기고, uevent를 받은 udev가 권한·이름을 다듬는다.

| 파일시스템 | 마운트 위치 | 무엇을 담나 | 형식 규칙 | 드라이버 개발자가 쓰는 API |
|---|---|---|---|---|
| procfs | `/proc` | 프로세스별 정보 + 오래된 시스템 정보 + sysctl | 사람이 읽는 텍스트 표 | `proc_create`, `seq_file` (새 코드에선 지양) |
| sysfs | `/sys` | 커널 디바이스 모델(bus·device·driver·class) 그 자체 | **파일 하나에 값 하나** | `DEVICE_ATTR`, attribute group (G8) |
| devtmpfs | `/dev` | 장치 노드 (I/O 입구) | 노드 = major:minor 주소 | `cdev_add` + `device_create` (G4) |
| debugfs | `/sys/kernel/debug` | 디버그 전용, 규칙 없음 | 자유 | `debugfs_create_*` (G8) |
| tracefs | `/sys/kernel/tracing` | ftrace 인터페이스 | ftrace 형식 | (M 모듈) |

### 7.2 /proc — 프로세스와 커널의 계기판

`/proc`의 숫자 디렉터리 하나하나가 프로세스이고, `/proc/self`는 "지금 이 파일을 여는 프로세스 자신"을 가리키는 심볼릭 링크다.

무엇을 확인하나: 한 프로세스가 pipe, socket, 장치 파일, 일반 파일을 열었을 때 `/proc/self/fd`에서 각각 어떻게 보이는지, `/proc/self/status`에 무엇이 있는지.

```c
/* ex5: /proc/self로 "나 자신"을 들여다본다 — fd 종류, exe, status 일부 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

int main(void)
{
    int p[2], s[2];
    if (pipe(p) < 0) { perror("pipe"); exit(1); }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) < 0) { perror("socketpair"); exit(1); }
    int dn = open("/dev/null", O_RDONLY);
    int hf = open("/etc/hostname", O_RDONLY);
    if (dn < 0 || hf < 0) { perror("open"); exit(1); }

    char buf[256], path[288], link[512];
    ssize_t n = readlink("/proc/self", buf, sizeof buf - 1);
    if (n < 0) { perror("readlink"); exit(1); }
    buf[n] = '\0';
    printf("getpid()=%d  /proc/self -> %s\n", getpid(), buf);

    n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n < 0) { perror("readlink exe"); exit(1); }
    buf[n] = '\0';
    printf("exe -> %s\n", buf);

    DIR *d = opendir("/proc/self/fd");
    if (!d) { perror("opendir"); exit(1); }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "/proc/self/fd/%s", e->d_name);
        n = readlink(path, link, sizeof link - 1);
        if (n < 0) continue;
        link[n] = '\0';
        printf("  fd %-2s -> %s\n", e->d_name, link);
    }
    closedir(d);

    FILE *f = fopen("/proc/self/status", "r");
    if (!f) { perror("status"); exit(1); }
    while (fgets(buf, sizeof buf, f))
        if (!strncmp(buf, "Name", 4) || !strncmp(buf, "State", 5) ||
            !strncmp(buf, "PPid", 4) || !strncmp(buf, "VmRSS", 5) ||
            !strncmp(buf, "Threads", 7) || !strncmp(buf, "voluntary", 9))
            fputs(buf, stdout);
    fclose(f);
    return 0;
}
```

```sh
./ex5_procself < /dev/null
```

```text
getpid()=32849  /proc/self -> 32849
exe -> /tmp/A1/ex5_procself
  fd 0  -> /dev/null
  fd 1  -> pipe:[103696]
  fd 2  -> pipe:[103697]
  fd 3  -> pipe:[102114]
  fd 4  -> pipe:[102114]
  fd 5  -> socket:[102115]
  fd 6  -> socket:[102116]
  fd 7  -> /dev/null
  fd 8  -> /etc/hostname
  fd 9  -> /proc/32849/fd
Name:	ex5_procself
State:	R (running)
PPid:	32820
VmRSS:	    1364 kB
Threads:	1
voluntary_ctxt_switches:	0
```

출력에서 볼 것:

- fd 3과 4는 **같은 pipe 객체**(inode 102114)의 읽기 끝과 쓰기 끝이다. 경로가 없는 객체는 `종류:[inode]` 형식으로 보인다 — pipe와 socket도 커널 안에서는 진짜 inode를 가진 "파일"이다 (pipefs, sockfs).
- fd 9는 `opendir()`이 연 디렉터리 자체다. 관찰하는 행위가 fd를 하나 더 만든다.
- `/proc/self`가 가리키는 숫자가 `getpid()`와 같다.

`/proc/self` 안에는 이런 것들이 있다:

```sh
ls /proc/self/ | column -c 100
```

```text
attr			io			numa_maps		smaps_rollup
autogroup		ksm_merging_pages	oom_adj			stack
auxv			ksm_stat		oom_score		stat
cgroup			latency			oom_score_adj		statm
clear_refs		limits			pagemap			status
cmdline			loginuid		patch_state		syscall
comm			map_files		personality		task
coredump_filter		maps			projid_map		timens_offsets
cwd			mem			root			timers
environ			mountinfo		sched			timerslack_ns
exe			mounts			schedstat		uid_map
fd			mountstats		sessionid		wchan
fdinfo			net			setgroups
gid_map			ns			smaps
```

자주 쓰는 것만 추리면: `maps`(메모리 맵, A5), `fd/`와 `fdinfo/`(열린 파일, A3), `status`(상태·메모리·컨텍스트 스위치 수, A2), `task/`(스레드별 디렉터리, A2), `stack`(커널 스택, root 필요), `wchan`(어디서 잠들어 있나), `syscall`(§3.6), `limits`(rlimit).

`fdinfo`는 fd 하나의 커널 쪽 상태를 보여 준다:

```sh
cat /proc/self/fdinfo/0
```

```text
pos:	0
flags:	00
mnt_id:	18
ino:	103601
```

출력에서 볼 것: `pos`는 `struct file`의 `f_pos`(현재 오프셋), `flags`는 `f_flags`(`O_RDONLY` = 0)다. 즉 **커널 구조체 필드가 그대로 노출**된 것이다 (§10).

시스템 전체 정보 쪽의 대표들:

```sh
cat /proc/version
head -12 /proc/interrupts
```

```text
Linux version 7.0.0-34-generic (buildd@bos03-arm64-067) (aarch64-linux-gnu-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #34-Ubuntu SMP PREEMPT_DYNAMIC Wed Sep  2 14:34:54 UTC 2026
           CPU0       CPU1       CPU2       CPU3       
 10:      84888     102736     279081      81608    GICv3  27 Level     arch_timer
 13:          0          0          0          0 ARMH0061:00   6 Edge      ACPI:Event
 22:          0          0          0          0 GICv2m-PCI-MSIX-0000:00:0b.0   0 Edge      virtio7-config
 23:          0          0          0          0 GICv2m-PCI-MSIX-0000:00:0b.0   1 Edge      virtio7-inflate
 24:          0          0          0          0 GICv2m-PCI-MSIX-0000:00:0b.0   2 Edge      virtio7-deflate
 25:          0          0          0          0 GICv2m-PCI-MSIX-0000:00:05.0   0 Edge      virtio1-config
 26:          0          0          0          0 GICv2m-PCI-MSIX-0000:00:05.0   1 Edge      virtio1-input
 27:         17          0          0          0 GICv2m-PCI-MSIX-0000:00:05.0   2 Edge      virtio1-output
 28:          0          0          0          0 GICv2m-PCI-MSIX-0000:00:06.0   0 Edge      virtio2-config
 29:      47338          0          0          0 GICv2m-PCI-MSIX-0000:00:06.0   1 Edge      virtio2-req.0
 30:          0          0          0          0 GICv2m-PCI-MSIX-0000:00:07.0   0 Edge      virtio3-config
```

출력에서 볼 것: `/proc/interrupts`는 Don이 가장 반가워할 파일이다. 열은 **Linux IRQ 번호 · CPU별 발생 횟수 · 인터럽트 컨트롤러(irqchip) · 하드웨어 IRQ 번호 · 트리거 타입 · 핸들러 이름**이다. `arch_timer`는 GIC PPI 27번(ARM generic timer, `CONFIG_HZ=1000`이라 코어당 초당 최대 1000번), `virtio2-req.0`(디스크 `vda`의 완료 인터럽트)은 MSI-X로 들어오고 전부 CPU0이 받았다. "센서 인터럽트가 안 들어와요"라는 이슈가 오면 제일 먼저 이 파일을 두 번 읽어서 카운트가 느는지 본다 (F6, H6).

### 7.3 /sys — 디바이스 모델을 그대로 보여 주는 트리

`/sys`는 커널의 **디바이스 모델**(bus·device·driver·class, G2)을 디렉터리 트리로 그대로 펼친 것이다.

```sh
ls /sys
ls /sys/bus
```

```text
block
bus
class
dev
devices
firmware
fs
hypervisor
kernel
module
power
acpi
amba
...
i2c
...
pci
...
platform
...
spi
usb
virtio
...
```

(`/sys/bus` 목록은 길어서 일부만 남기고 `...`로 생략했다.)

| 디렉터리 | 관점 | 예 |
|---|---|---|
| `/sys/devices/` | **유일한 진짜 트리**. 물리 연결 순서(SoC → 버스 → 장치) | `/sys/devices/platform/serial8250/.../ttyS0` |
| `/sys/bus/<버스>/devices`, `drivers` | 버스 관점. 어떤 장치와 드라이버가 있나 | `/sys/bus/i2c/devices/1-0068` |
| `/sys/class/<클래스>/` | 기능 관점 ("네트워크 장치 전부", "RTC 전부") | `/sys/class/net/eth0` |
| `/sys/dev/char`, `/sys/dev/block` | major:minor로 찾기 | `/sys/dev/char/1:3` |
| `/sys/module/<이름>/` | 로드된 모듈과 파라미터 | `/sys/module/dummy/` |

`/sys/bus`, `/sys/class`, `/sys/dev` 아래는 대부분 `/sys/devices/` 쪽을 가리키는 **심볼릭 링크**다.

```sh
ls -l /sys/class/net/
for f in address mtu operstate speed; do printf "%s: " $f; cat /sys/class/net/eth0/$f; done
readlink -f /sys/class/net/eth0/device/driver
```

```text
total 0
lrwxrwxrwx 1 root root 0 Oct  5 22:42 eth0 -> ../../devices/pci0000:00/0000:00:01.0/virtio0/net/eth0
lrwxrwxrwx 1 root root 0 Oct  5 22:42 lo -> ../../devices/virtual/net/lo
address: 52:55:55:a5:ed:31
mtu: 1500
operstate: up
speed: -1
/sys/bus/virtio/drivers/virtio_net
```

출력에서 볼 것: sysfs 속성은 **파일 하나에 값 하나**다. `cat` 한 번으로 MAC 주소, MTU를 읽는다. 셸 스크립트·테스트 코드에서 파싱이 쉽다는 것이 sysfs의 설계 목표다. `eth0`은 PCI 버스의 virtio 장치 위에 있고, `virtio_net` 드라이버가 붙어 있다 — 이 "장치 ↔ 드라이버 바인딩"을 sysfs 링크로 확인하는 습관이 bring-up의 핵심이다.

### 7.4 /dev와 /sys를 이어 주는 코드

무엇을 확인하나: `/dev` 노드 하나에서 출발해 major:minor → `/sys/dev/...` → 실제 sysfs 디바이스 경로 → `uevent` 내용 → 붙은 드라이버 이름까지 따라간다. bring-up 때 "이 노드는 어느 하드웨어·드라이버 것인가"를 찾는 그 과정이다.

```c
/* ex7: /dev 노드 → major:minor → /sys/dev/... → sysfs 디바이스와 드라이버 */
#define _GNU_SOURCE
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

static void cat_attr(const char *dir, const char *name)
{
    char p[PATH_MAX + 32], line[256];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "r");
    if (!f) return;                       /* 없는 속성은 조용히 넘어간다 */
    while (fgets(line, sizeof line, f))
        printf("    %s: %s", name, line);
    fclose(f);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) < 0) { perror(argv[i]); continue; }
        if (!S_ISCHR(st.st_mode) && !S_ISBLK(st.st_mode)) {
            printf("%s: not a device node\n", argv[i]);
            continue;
        }
        char link[64], real[PATH_MAX], drv[PATH_MAX + 32], dt[PATH_MAX];
        snprintf(link, sizeof link, "/sys/dev/%s/%u:%u",
                 S_ISCHR(st.st_mode) ? "char" : "block",
                 major(st.st_rdev), minor(st.st_rdev));
        if (!realpath(link, real)) { perror(link); continue; }
        printf("%s (%s)\n  -> %s\n", argv[i], link, real);
        cat_attr(real, "uevent");
        snprintf(drv, sizeof drv, "%s/device/driver", real);
        if (realpath(drv, dt))
            printf("    driver: %s\n", basename(dt));
        else
            printf("    driver: (none - virtual device)\n");
    }
    return 0;
}
```

```sh
./ex7_dev2sys /dev/null /dev/ttyS0 /dev/vda /dev/rtc0 /etc/hostname
```

```text
/dev/null (/sys/dev/char/1:3)
  -> /sys/devices/virtual/mem/null
    uevent: MAJOR=1
    uevent: MINOR=3
    uevent: DEVNAME=null
    uevent: DEVMODE=0666
    driver: (none - virtual device)
/dev/ttyS0 (/sys/dev/char/4:64)
  -> /sys/devices/platform/serial8250/serial8250:0/serial8250:0.0/tty/ttyS0
    uevent: MAJOR=4
    uevent: MINOR=64
    uevent: DEVNAME=ttyS0
    driver: port
/dev/vda (/sys/dev/block/253:0)
  -> /sys/devices/pci0000:00/0000:00:06.0/virtio2/block/vda
    uevent: MAJOR=253
    uevent: MINOR=0
    uevent: DEVNAME=vda
    uevent: DEVTYPE=disk
    uevent: DISKSEQ=9
    driver: virtio_blk
/dev/rtc0 (/sys/dev/char/246:0)
  -> /sys/devices/platform/rtc-efi.0/rtc/rtc0
    uevent: MAJOR=246
    uevent: MINOR=0
    uevent: DEVNAME=rtc0
    driver: rtc-efi
/etc/hostname: not a device node
```

출력에서 볼 것:

- `/dev/vda`는 `pci0000:00/0000:00:06.0/virtio2/block/vda` — PCI 슬롯 6의 virtio 장치에 `virtio_blk` 드라이버가 붙었다. 실제 보드라면 `/sys/devices/platform/soc/30b40000.mmc/...` 같은 Device Tree 노드 이름이 보인다 (D 모듈).
- `/dev/rtc0`은 platform 버스의 `rtc-efi` 드라이버가 만든 것이다. 센서 키트의 I2C RTC라면 여기에 `rtc-ds1307` 같은 이름이 나온다.
- `/dev/ttyS0`의 드라이버가 `port`로 나오는 건 최근 커널의 serial core가 "serial8250 컨트롤러 → 포트" 사이에 `serial-base` 버스 장치를 하나 더 두기 때문이다. 트리를 한 단계 더 올라가면 `serial8250` 드라이버가 보인다.
- `uevent` 파일은 udev가 받는 이벤트 내용과 같다 (`MAJOR`, `MINOR`, `DEVNAME`). udev는 이것을 보고 `/dev` 노드의 권한과 심볼릭 링크를 정한다.

### 7.5 /dev는 누가 만드나 — devtmpfs와 uevent

예전 Linux(2.4 시절)는 `/dev`에 노드 수천 개를 미리 `mknod`로 만들어 두었다. 지금은:

1. 드라이버가 장치를 등록하면(`device_create()` → `device_add()`), 커널이 **devtmpfs에 노드를 바로 만든다** (`CONFIG_DEVTMPFS=y`, `CONFIG_DEVTMPFS_MOUNT=y` — 이 VM 커널 설정에서 확인).
2. 동시에 **uevent**(netlink 소켓으로 보내는 "장치 생김/사라짐" 메시지)를 유저 공간에 보낸다.
3. udev(systemd-udevd, 또는 임베디드에선 mdev)가 uevent를 받고 규칙대로 권한·소유자 변경, `/dev/serial/by-id/...` 같은 심볼릭 링크 생성, 필요한 모듈 로드(modalias)를 한다 (G9).

uevent를 직접 보자. 가상 네트워크 장치를 만들었다 지운다:

```sh
sudo udevadm monitor --kernel --property --subsystem-match=net &
sudo ip link add dum1 type dummy; sudo ip link del dum1
```

```text
monitor will print the received events for:
KERNEL - the kernel uevent

KERNEL[330.122929] add      /devices/virtual/net/dum1 (net)
ACTION=add
DEVPATH=/devices/virtual/net/dum1
SUBSYSTEM=net
INTERFACE=dum1
IFINDEX=5
SEQNUM=4454

KERNEL[330.430899] remove   /devices/virtual/net/dum1 (net)
ACTION=remove
DEVPATH=/devices/virtual/net/dum1
SUBSYSTEM=net
INTERFACE=dum1
IFINDEX=5
SEQNUM=4459
```

출력에서 볼 것: `DEVPATH`는 `/sys` 아래 경로 그대로다. 장치가 생기고 없어질 때마다 커널이 이런 키=값 메시지를 뿌린다. 임베디드 실무에서 "USB-시리얼 GNSS를 꽂을 때마다 `/dev/ttyUSB0`·`ttyUSB1` 순서가 바뀐다" 같은 문제를 udev 규칙으로 고정하는 것도 이 메커니즘이다.

---

## 8. Monolithic 커널 + 모듈 vs 마이크로커널·RTOS

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <text x="115" y="20" text-anchor="middle" font-size="13" font-weight="bold">Monolithic (Linux)</text>
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">Microkernel (QNX, seL4)</text>
  <text x="565" y="20" text-anchor="middle" font-size="13" font-weight="bold">RTOS (FreeRTOS 등)</text>
  <rect x="10" y="30" width="210" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="57" text-anchor="middle" font-size="13">앱 프로세스 (EL0)</text>
  <rect x="10" y="84" width="210" height="190" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="115" y="104" text-anchor="middle" font-size="12">커널 (EL1, 한 주소 공간)</text>
  <rect x="22" y="114" width="186" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="115" y="133" text-anchor="middle" font-size="12">sched · MM · IPC</text>
  <rect x="22" y="148" width="186" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="115" y="167" text-anchor="middle" font-size="12">VFS · 파일시스템 · 네트워크</text>
  <rect x="22" y="182" width="186" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="115" y="201" text-anchor="middle" font-size="12">드라이버 (빌트인)</text>
  <rect x="22" y="216" width="186" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
  <text x="115" y="235" text-anchor="middle" font-size="12">+ 모듈 (.ko) 런타임 로드</text>
  <text x="115" y="264" text-anchor="middle" font-size="12">호출 = 함수 호출 (빠름)</text>
  <rect x="235" y="30" width="100" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="285" y="57" text-anchor="middle" font-size="12">앱</text>
  <rect x="345" y="30" width="100" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="395" y="57" text-anchor="middle" font-size="12">FS 서버</text>
  <rect x="235" y="84" width="100" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="285" y="111" text-anchor="middle" font-size="12">드라이버 서버</text>
  <rect x="345" y="84" width="100" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="395" y="111" text-anchor="middle" font-size="12">네트워크 서버</text>
  <text x="340" y="160" text-anchor="middle" font-size="12">모두 유저 모드 프로세스</text>
  <text x="340" y="180" text-anchor="middle" font-size="12">서로 메시지(IPC)로 통신</text>
  <rect x="235" y="200" width="210" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="227" text-anchor="middle" font-size="12">작은 커널: IPC · 스케줄 · MMU</text>
  <text x="340" y="264" text-anchor="middle" font-size="12">드라이버 죽어도 재시작 가능</text>
  <rect x="460" y="30" width="210" height="214" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="110" text-anchor="middle" font-size="13">태스크 + 드라이버 + 커널</text>
  <text x="565" y="134" text-anchor="middle" font-size="13">대개 한 특권 · 한 주소 공간</text>
  <text x="565" y="158" text-anchor="middle" font-size="13">MPU 격리는 선택</text>
  <text x="565" y="264" text-anchor="middle" font-size="12">결정적 지연, 작은 크기</text>
</svg>
```
그림 5 — 세 가지 OS 구조. Linux는 드라이버까지 커널 주소 공간에서 돌리는 monolithic 구조이고, 모듈로 런타임 확장성을 얻는다.

| 구조 | 장점 | 단점 | 대표 |
|---|---|---|---|
| Monolithic + 모듈 | 서브시스템 간 호출이 함수 호출이라 빠르다. 드라이버 생태계가 거대하다 | 드라이버 버그 하나가 커널 전체를 죽인다 (Oops/panic) | Linux, FreeBSD |
| Microkernel | 드라이버·FS가 유저 프로세스라 격리·재시작이 가능, 검증하기 쉽다 | 메시지 IPC 비용, 드라이버 생태계 작음 | QNX, seL4, (부분적으로) Zircon |
| RTOS | 결정적 지연, 작은 메모리, 단순함 | 메모리 보호·프로세스 개념이 약하거나 없음, 네트워크·FS·드라이버를 직접 붙여야 함 | FreeRTOS, Zephyr, ThreadX |

**모듈(LKM, loadable kernel module)**은 Linux가 monolithic의 단점(모든 드라이버를 이미지에 넣어야 함)을 덜기 위한 장치다. `.ko` 파일을 런타임에 커널 주소 공간에 링크해 넣는다. 들어간 뒤에는 빌트인 코드와 **똑같은 권한**으로 돈다 — 격리가 아니라 "나중에 링크"일 뿐이다.

```sh
lsmod | head -8
wc -l < /proc/modules
grep -E "CONFIG_MODULES=|CONFIG_VIRTIO_BLK=|CONFIG_EXT4_FS=" /boot/config-$(uname -r)
```

```text
Module                  Size  Used by
vcan                   12288  0
ib_core               569344  0
i2c_stub               24576  0
can_raw                28672  0
can                    32768  1 can_raw
gpio_sim               32768  13
dev_sync_probe         12288  1 gpio_sim

88
CONFIG_MODULES=y
CONFIG_VIRTIO_BLK=y
CONFIG_EXT4_FS=y
```

출력에서 볼 것: 지금 88개 모듈이 로드돼 있다. `lsmod`는 사실 `/proc/modules`를 예쁘게 찍는 도구다. `Used by`는 참조 카운트 — `can_raw`가 `can`을 쓰고 있어서 `can`을 먼저 내릴 수 없다. 반면 `virtio_blk`와 `ext4`는 `=y`(빌트인)다. 루트 파일시스템을 읽는 데 필요한 드라이버는 모듈로 두면 부팅 중 닭과 달걀 문제가 생기므로 빌트인으로 넣거나 initramfs에 모듈을 넣는다 (C6, C7).

앞의 `ip link add dum1 type dummy` 실험 뒤에 확인하면:

```sh
lsmod | grep dummy
ls /sys/module/dummy
cat /sys/module/dummy/refcnt
```

```text
dummy                  12288  0
coresize
holders
initsize
initstate
notes
refcnt
sections
srcversion
taint
uevent
0
```

출력에서 볼 것: `dummy` 모듈을 직접 `modprobe`한 적이 없는데 로드돼 있다. `ip link add ... type dummy`를 받은 커널이 "dummy 타입을 아는 코드가 없네" → `request_module()`로 유저 공간의 `modprobe`를 불러 **자동 로드**한 것이다. 모듈도 `/sys/module/` 아래에 자기 디렉터리를 갖는다 — 또 "파일"이다. 모듈 작성은 E3에서 직접 한다.

> Don 관점: Linux 드라이버 개발에서 "bare-metal의 격리 없음"은 그대로다. 드라이버의 널 포인터 하나는 커널 Oops이고, 잠금 중 버그는 시스템 전체 hang이다. 유저 공간의 안전망(프로세스 하나만 죽음)은 **커널 안에는 없다**.

---

## 9. A1~A10과 이후 모듈의 지도

A 모듈은 "커널 바깥에서 본 Linux"다. 각 노트에서 배우는 유저 API는 나중에 드라이버 쪽에서 **짝이 되는 커널 함수**로 다시 나온다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="144" width="130" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="75" y="169" text-anchor="middle" font-size="13" font-weight="bold">A1 큰 그림</text>
  <text x="75" y="189" text-anchor="middle" font-size="12">(지금 여기)</text>
  <text x="270" y="14" text-anchor="middle" font-size="12">유저 API (A)</text>
  <text x="550" y="14" text-anchor="middle" font-size="12">커널·드라이버 쪽 짝</text>
  <g font-size="12">
    <rect x="170" y="22" width="200" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="180" y="41">A2 프로세스 · 스레드</text>
    <rect x="430" y="22" width="240" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="440" y="41">F1 스케줄러 · task_struct</text>
    <rect x="170" y="58" width="200" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="180" y="77">A3 파일 I/O · ioctl</text>
    <rect x="430" y="58" width="240" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="440" y="77">G4 cdev · file_operations</text>
    <rect x="170" y="94" width="200" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="180" y="113">A4 select/poll/epoll</text>
    <rect x="430" y="94" width="240" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="440" y="113">G6 .poll · poll_wait · F3</text>
    <rect x="170" y="130" width="200" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="180" y="149">A5 mmap · 가상 메모리</text>
    <rect x="430" y="130" width="240" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="440" y="149">G5 .mmap · F9 · F11</text>
    <rect x="170" y="166" width="200" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="180" y="185">A6 시그널</text>
    <rect x="430" y="166" width="240" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="440" y="185">F3 interruptible sleep</text>
    <rect x="170" y="202" width="200" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="180" y="221">A7 IPC</text>
    <rect x="430" y="202" width="240" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="440" y="221">F12 VFS · pipefs · sockfs</text>
    <rect x="170" y="238" width="200" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="180" y="257">A8 동기화 (유저)</text>
    <rect x="430" y="238" width="240" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="440" y="257">F4 spinlock · mutex · futex</text>
    <rect x="170" y="274" width="200" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="180" y="293">A9 소켓 · SocketCAN</text>
    <rect x="430" y="274" width="240" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="440" y="293">I · netdev · CAN 드라이버</text>
    <rect x="170" y="310" width="200" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="180" y="329">A10 셸 생존 키트</text>
    <rect x="430" y="310" width="240" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="440" y="329">M 디버깅 · ftrace · perf</text>
  </g>
  <g stroke="currentColor" stroke-opacity="0.5">
    <line x1="140" y1="174" x2="170" y2="36"/>
    <line x1="140" y1="174" x2="170" y2="72"/>
    <line x1="140" y1="174" x2="170" y2="108"/>
    <line x1="140" y1="174" x2="170" y2="144"/>
    <line x1="140" y1="174" x2="170" y2="180"/>
    <line x1="140" y1="174" x2="170" y2="216"/>
    <line x1="140" y1="174" x2="170" y2="252"/>
    <line x1="140" y1="174" x2="170" y2="288"/>
    <line x1="140" y1="174" x2="170" y2="324"/>
  </g>
  <g stroke="currentColor">
    <line x1="372" y1="36" x2="428" y2="36" marker-end="url(#ah4)"/>
    <line x1="372" y1="72" x2="428" y2="72" marker-end="url(#ah4)"/>
    <line x1="372" y1="108" x2="428" y2="108" marker-end="url(#ah4)"/>
    <line x1="372" y1="144" x2="428" y2="144" marker-end="url(#ah4)"/>
    <line x1="372" y1="180" x2="428" y2="180" marker-end="url(#ah4)"/>
    <line x1="372" y1="216" x2="428" y2="216" marker-end="url(#ah4)"/>
    <line x1="372" y1="252" x2="428" y2="252" marker-end="url(#ah4)"/>
    <line x1="372" y1="288" x2="428" y2="288" marker-end="url(#ah4)"/>
    <line x1="372" y1="324" x2="428" y2="324" marker-end="url(#ah4)"/>
  </g>
</svg>
```
그림 6 — A 모듈 각 노트와, 그 API를 커널·드라이버 쪽에서 구현하는 모듈의 짝.

| 이 노트에서 나온 것 | 다시 깊게 보는 곳 |
|---|---|
| syscall 진입·복귀, `copy_to_user`, `-errno` 규약 | E6 유저 ↔ 커널 경계 |
| fd, `struct file`, `file_operations` | A3 (유저 쪽), F12 (VFS 내부), G4 (드라이버 쪽) |
| major/minor, `/dev` 노드 | G4 문자 디바이스, G7 misc device |
| `/sys` 트리, bus·device·driver | G2 디바이스 모델, G8 sysfs 속성 만들기 |
| uevent, udev | G9 핫플러그와 udev |
| `/proc/interrupts` | F6 인터럽트 처리, H6 인터럽트 컨트롤러 |
| 모듈, `lsmod`, 자동 로드 | E3 커널 모듈 |
| strace, ftrace | A10 셸 키트, M 디버깅·트레이싱 |

---

## 10. 커널 안에서는 무슨 일이 일어나나 — `open("/dev/xyz")`의 전체 여정

### 10.1 등장 구조체

| 구조체 | 무엇인가 | Don의 비유 |
|---|---|---|
| `task_struct` | 프로세스(스레드) 하나. `files` 필드로 fd 테이블을 가리킨다 | RTOS TCB |
| `files_struct` / `fdtable` | fd 번호 → `struct file *` 배열 | 핸들 테이블 |
| `struct file` | "열린 파일 하나". `f_pos`, `f_flags`, `f_op`, `private_data` | open 시 할당되는 세션 컨텍스트 |
| `struct inode` | 파일 자체 (메타데이터). 장치면 `i_rdev`(major:minor) | 장치 디스크립터 |
| `struct dentry` | 경로의 한 구성 요소 (`dev`, `null`). 경로 탐색 캐시 | 이름 → 객체 lookup 캐시 |
| `struct cdev` | 문자 장치 하나를 커널에 등록한 것. `ops`를 들고 있다 | 드라이버 등록 레코드 |
| `struct file_operations` | `.open`, `.read`, `.write`, `.unlocked_ioctl`, `.mmap`, `.poll`, `.release` 함수 포인터 테이블 | HAL ops 테이블 |

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="180" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="41" text-anchor="middle" font-size="13">task_struct</text>
  <text x="100" y="60" text-anchor="middle" font-size="12">→ files (fd 테이블)</text>
  <rect x="10" y="90" width="180" height="110" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="110" text-anchor="middle" font-size="13">fd 테이블</text>
  <text x="22" y="135" font-size="12">0 → file (stdin)</text>
  <text x="22" y="157" font-size="12">1 → file (stdout)</text>
  <text x="22" y="179" font-size="12">3 → file (/dev/null)</text>
  <line x1="100" y1="70" x2="100" y2="88" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="230" y="90" width="200" height="110" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="330" y="110" text-anchor="middle" font-size="13">struct file</text>
  <text x="242" y="135" font-size="12">f_pos, f_flags</text>
  <text x="242" y="157" font-size="12">f_op → 어느 ops?</text>
  <text x="242" y="179" font-size="12">private_data (드라이버용)</text>
  <line x1="160" y1="175" x2="228" y2="150" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="230" y="225" width="200" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="330" y="246" text-anchor="middle" font-size="13">inode (/dev/null)</text>
  <text x="330" y="265" text-anchor="middle" font-size="12">i_rdev = 1:3, char</text>
  <line x1="330" y1="200" x2="330" y2="223" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="480" y="20" width="190" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="575" y="38" text-anchor="middle" font-size="12">ext4_file_operations</text>
  <text x="575" y="56" text-anchor="middle" font-size="12">일반 파일</text>
  <rect x="480" y="72" width="190" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="575" y="90" text-anchor="middle" font-size="12">pipefifo_fops</text>
  <text x="575" y="108" text-anchor="middle" font-size="12">pipe / FIFO</text>
  <rect x="480" y="124" width="190" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="575" y="142" text-anchor="middle" font-size="12">socket_file_ops</text>
  <text x="575" y="160" text-anchor="middle" font-size="12">socket</text>
  <rect x="480" y="176" width="190" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="575" y="194" text-anchor="middle" font-size="12">null_fops</text>
  <text x="575" y="212" text-anchor="middle" font-size="12">/dev/null (mem.c)</text>
  <rect x="480" y="228" width="190" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="575" y="246" text-anchor="middle" font-size="12">my_sensor_fops</text>
  <text x="575" y="264" text-anchor="middle" font-size="12">G4에서 직접 작성</text>
  <g stroke="currentColor" stroke-dasharray="4 3" stroke-opacity="0.6">
    <line x1="430" y1="152" x2="478" y2="42" marker-end="url(#ah5)"/>
    <line x1="430" y1="152" x2="478" y2="94" marker-end="url(#ah5)"/>
    <line x1="430" y1="152" x2="478" y2="146" marker-end="url(#ah5)"/>
    <line x1="430" y1="152" x2="478" y2="250" marker-end="url(#ah5)"/>
  </g>
  <line x1="430" y1="152" x2="478" y2="198" stroke="#d0564a" stroke-width="2" marker-end="url(#ah5)"/>
  <rect x="10" y="310" width="120" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="70" y="332" text-anchor="middle" font-size="12">openat()</text>
  <rect x="145" y="310" width="120" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="205" y="332" text-anchor="middle" font-size="12">path_openat</text>
  <rect x="280" y="310" width="120" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="332" text-anchor="middle" font-size="12">do_dentry_open</text>
  <rect x="415" y="310" width="120" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="475" y="332" text-anchor="middle" font-size="12">chrdev_open</text>
  <rect x="550" y="310" width="120" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="610" y="332" text-anchor="middle" font-size="12">memory_open</text>
  <g stroke="currentColor">
    <line x1="130" y1="327" x2="143" y2="327" marker-end="url(#ah5)"/>
    <line x1="265" y1="327" x2="278" y2="327" marker-end="url(#ah5)"/>
    <line x1="400" y1="327" x2="413" y2="327" marker-end="url(#ah5)"/>
    <line x1="535" y1="327" x2="548" y2="327" marker-end="url(#ah5)"/>
  </g>
  <text x="340" y="378" text-anchor="middle" font-size="12">chrdev_open: i_rdev의 major 1로 cdev 조회 → f_op = memory_fops</text>
  <text x="340" y="400" text-anchor="middle" font-size="12">memory_open: minor 3 → f_op를 null_fops로 교체</text>
  <text x="340" y="422" text-anchor="middle" font-size="12">이후 write(fd) → vfs_write → f_op-&gt;write = write_null</text>
</svg>
```
그림 7 — `open("/dev/null")`의 디스패치. 같은 `struct file`의 `f_op`가 무엇을 가리키느냐에 따라 같은 `read`/`write`가 ext4, 파이프, 소켓, 드라이버로 갈라진다. 아래 줄은 열 때 `f_op`가 정해지는 경로.

### 10.2 단계별로

1. **syscall 진입**: `openat` → `__arm64_sys_openat` → `do_sys_openat2` (§3.5).
2. **빈 fd 번호 확보**: 프로세스의 fd 테이블에서 가장 작은 빈 번호를 예약한다.
3. **경로 탐색**: `path_openat`이 `/` → `dev` → `null`을 dentry 캐시에서 따라가 inode를 찾는다. `/dev`가 devtmpfs라는 것도 여기서 마운트 테이블을 보고 넘어간다. 권한 검사(`rw-rw-rw-`)도 이 단계.
4. **`struct file` 할당**: `f_flags`, `f_pos = 0`, 그리고 `f_op`는 일단 inode가 준 기본값. 장치 파일 inode는 `init_special_inode()`에서 `def_chr_fops`(open만 있는 테이블)를 받아 두었다.
5. **`do_dentry_open` → `f_op->open` 호출** = `chrdev_open`.
6. **`chrdev_open`**: `inode->i_rdev`(1:3)로 `cdev_map`에서 등록된 `cdev`를 찾고(`kobj_lookup`), 그 드라이버 모듈의 참조 카운트를 올리고(`try_module_get`), `file->f_op`를 **드라이버의 `cdev->ops`로 바꿔 끼운 뒤** 드라이버의 `.open`을 부른다. major 1 `mem` 드라이버의 `.open`은 `memory_open`.
7. **`memory_open`**: minor 번호(3)로 내부 표에서 `null` 항목을 찾아 `f_op`를 한 번 더 `null_fops`로 바꾼다. (보통 드라이버는 여기서 `file->private_data`에 장치 구조체를 걸어 둔다 — G4의 핵심 패턴.)
8. **fd 설치**: fd 테이블[3] = 이 `struct file`. 유저에게 3을 돌려준다.
9. 이후 `write(3, ...)` → `fdget` → `vfs_write` → `file->f_op->write` → `write_null` (그냥 길이를 돌려주는 함수).

### 10.3 ftrace로 실제 경로 확인

무엇을 확인하나: 위 설명이 지어낸 게 아니라 커널 7.0에서 정말 그 함수들을 지나는지. ftrace(커널 내장 함수 트레이서, M 모듈)로 확인한다. 추적 대상 프로그램은 다음과 같다.

```c
/* ex8: ftrace로 추적할 대상 — /dev/null을 열고 쓰고 닫는다 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    int fd = open("/dev/null", O_WRONLY);
    if (fd < 0) { perror("open"); exit(1); }
    if (write(fd, "hi", 2) != 2) { perror("write"); exit(1); }
    close(fd);
    return 0;
}
```

첫 번째 실험: `chrdev_open`이 불릴 때 **콜 스택**을 찍는다 (root 필요).

```sh
T=/sys/kernel/tracing
echo chrdev_open > $T/set_ftrace_filter
echo 1 > $T/options/func_stack_trace
echo function > $T/current_tracer
bash -c "echo \$\$ > $T/set_ftrace_pid; echo 1 > $T/tracing_on; exec /tmp/A1/ex8_opendev"
echo 0 > $T/tracing_on; cat $T/trace
```

```text
# tracer: function
#
# entries-in-buffer/entries-written: 2/2   #P:4
#
#                                _-----=> irqs-off/BH-disabled
#                               / _----=> need-resched
#                              | / _---=> hardirq/softirq
#                              || / _--=> preempt-depth
#                              ||| / _-=> migrate-disable
#                              |||| /     delay
#           TASK-PID     CPU#  |||||  TIMESTAMP  FUNCTION
#              | |         |   |||||     |         |
     ex8_opendev-34758   [001] .....   299.619447: chrdev_open <-do_dentry_open
     ex8_opendev-34758   [001] .....   299.619448: <stack trace>
 => ftrace_caller
 => chrdev_open
 => do_dentry_open
 => vfs_open
 => do_open
 => path_openat
 => do_file_open
 => do_sys_openat2
 => __arm64_sys_openat
 => invoke_syscall.constprop.0
 => el0_svc_common.constprop.0
 => do_el0_svc
 => el0_svc
 => el0t_64_sync_handler
 => el0t_64_sync
```

출력에서 볼 것: 아래에서 위로 읽으면 그림 2와 그림 7이 한 줄로 이어진다 — `el0t_64_sync`(예외 벡터) → `el0_svc` → `invoke_syscall` → `__arm64_sys_openat` → `do_sys_openat2` → `path_openat` → `do_open` → `vfs_open` → `do_dentry_open` → `chrdev_open`. (6.x 커널에서는 `do_file_open` 자리에 `do_filp_open`이 보인다. 이름이 버전에 따라 바뀌는 대표 예다.)

두 번째 실험: `chrdev_open` 안에서 무엇을 부르는지, 그리고 `write()`가 어디로 가는지 `function_graph`로 본다 (설정 명령은 첫 실험과 같은 패턴이라 생략, `set_graph_function`만 바꿨다).

```text
 3)               |  chrdev_open() {
 3)   0.125 us    |    _raw_spin_lock();
 3)   0.083 us    |    try_module_get();
 3)   0.125 us    |    _raw_spin_unlock();
 3)   0.083 us    |    try_module_get();
 3)   0.084 us    |    module_put();
 3)   0.166 us    |    memory_open();
 3)   1.875 us    |  }
```

```text
 0)               |  ksys_write() {
 0)   0.208 us    |    fdget_pos();
 0)               |    vfs_write() {
 0)               |      rw_verify_area() {
 0)               |        security_file_permission() {
 0)               |          apparmor_file_permission() {
 0)   0.083 us    |            dynamic_might_resched();
 0)               |            aa_file_perm() {
 0)   0.083 us    |              __rcu_read_lock();
 0)   0.083 us    |              __rcu_read_unlock();
 0)   0.459 us    |            }
 0)   0.833 us    |          }
 0)   1.042 us    |        }
 0)   1.208 us    |      }
 0)   0.083 us    |      write_null();
 0)   0.083 us    |      __fsnotify_parent();
 0)   1.958 us    |    }
 0)   3.209 us    |  }
```

(각 트레이스 앞에 다른 CPU의 `mutex_unlock()` 한 줄이 섞여 있어 지웠다.)

출력에서 볼 것:

- `chrdev_open` 안에서 `try_module_get`(드라이버 모듈이 열린 동안 언로드되지 않게 잠금) 후 `memory_open`을 부른다. 10.2의 6~7단계 그대로다.
- `write()`는 `ksys_write` → `fdget_pos`(fd → `struct file`) → `vfs_write` → 보안 검사(AppArmor LSM) → **`write_null`**. 드라이버의 일은 맨 아래 한 줄(0.083 µs)뿐이고 나머지는 VFS 공통 경로다. 내가 드라이버의 `.write`를 쓰면 `write_null` 자리에 내 함수가 들어간다.
- 함수 그래프 시간은 트레이싱 오버헤드가 포함된 값이다. §5의 측정값과 직접 비교하지 않는다.

### 10.4 드라이버를 쓸 때 다시 만나는 지점

| 유저가 부르는 것 | 커널 경로 | 드라이버가 채우는 것 | 모듈 |
|---|---|---|---|
| `open("/dev/my_imu")` | `chrdev_open` → `cdev->ops->open` | `.open`: `container_of`로 장치 구조체 찾아 `private_data`에 저장 | G4 |
| `read(fd, buf, n)` | `vfs_read` → `f_op->read` | `.read`: 데이터 준비, `copy_to_user`, 없으면 wait queue에서 잠듦 | G4, F3 |
| `ioctl(fd, CMD, arg)` | `vfs_ioctl` → `f_op->unlocked_ioctl` | `.unlocked_ioctl`: `_IOR`/`_IOW` 명령 처리 | G5 |
| `mmap(...)` | `mmap_region` → `f_op->mmap` | `.mmap`: `remap_pfn_range` 또는 dma-buf | G5, H9 |
| `poll`/`epoll_wait` | `vfs_poll` → `f_op->poll` | `.poll`: `poll_wait` + 마스크 반환 | G6, A4 |
| `close(fd)` (마지막 참조) | `fput` → `__fput` → `f_op->release` | `.release`: 자원 정리 | G4 |
| `cat /sys/.../attr` | sysfs → `kobject` 속성 `show` | `DEVICE_ATTR_RO` 의 `show` 함수 | G8 |

---

## 11. Embedded 실무에서 다시 보기

### 11.1 "센서 노드가 안 생겨요" — `/dev`가 아니라 `/sys`부터

예를 들어 센서 키트에 I2C IMU(주소 0x68)를 붙였는데 `/dev/iio:device0`이 없다. 순서대로 본다.

```sh
ls /sys/bus/i2c/devices/                 # 1-0068 같은 디바이스가 있나? (DT가 기술했나: D 모듈)
cat /sys/bus/i2c/devices/1-0068/name     # compatible에서 온 이름
ls -l /sys/bus/i2c/devices/1-0068/driver # 드라이버가 bind 됐나? 링크가 없으면 미바인딩
dmesg | grep -i -E "i2c|imu|probe"       # probe 실패 메시지 (-EPROBE_DEFER, -ENXIO, -EREMOTEIO)
cat /proc/interrupts                     # 데이터레디 IRQ 카운트가 증가하나
```

디바이스는 있는데 `driver` 링크가 없다 → 드라이버 모듈이 안 올라왔거나 `compatible`이 안 맞는다 (D3, G2). 디바이스도 없다 → Device Tree 문제다. 이 진단 순서 자체가 그림 4다.

### 11.2 테스트 툴을 "파일 API"로 짠다

드라이버를 쓰면 유저 테스트 코드는 언제나 같은 모양이다. 예를 들어 GNSS UART(`/dev/ttyS1`)든, 내 IMU char 드라이버든, `open` → (`ioctl`로 설정) → `read` 루프 → `close`. 그래서 A3의 파일 I/O 코드가 모든 드라이버 검증의 기본 틀이 된다. 반대로 드라이버를 설계할 때도 "유저가 `cat`으로 읽을 수 있게"(sysfs), "`read()`가 블로킹되게"(wait queue) 같은 **파일 의미론**을 지켜야 한다.

### 11.3 syscall 수 줄이기

카메라 30 fps × 4채널을 처리하는 앱이 프레임마다 `open`/`ioctl`/`read`/`close`를 반복하면 §5의 비용이 프레임당 수 µs씩 쌓이고, 무엇보다 `read()`의 메모리 복사가 크다. V4L2는 그래서 `mmap`된 버퍼 큐 + `VIDIOC_QBUF`/`DQBUF` + `poll` 구조를 쓴다 (A4, A5, I). `strace -c -f -p <pid>`로 10초 붙여서 syscall 분포를 보는 것이 최적화의 첫 단계다.

### 11.4 `/proc`·`/sys`를 모니터링 데몬에서 읽기

필드 장비의 헬스 모니터는 대개 `/proc/meminfo`, `/proc/loadavg`, `/sys/class/thermal/thermal_zone*/temp`, `/sys/class/net/eth0/statistics/rx_errors` 같은 파일을 주기적으로 읽는 것이 전부다. 별도 API가 필요 없다는 것이 "everything is a file"의 실무적 이득이다. 단 이 파일들은 **읽을 때마다 커널이 내용을 새로 생성**하므로, 매번 `open`부터 다시 하거나 `lseek(fd, 0, SEEK_SET)` 후 다시 `read`해야 최신 값이 나온다.

```sh
cat /proc/loadavg
grep -E "MemTotal|MemFree|MemAvailable" /proc/meminfo
```

```text
0.44 0.18 0.06 4/250 34275
MemTotal:        3988828 kB
MemFree:         3263308 kB
MemAvailable:    3484084 kB
```

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| syscall 실패를 `errno != 0`으로 판단 | 정상 동작인데 에러로 처리, 혹은 이전 에러 코드 오인 | 성공한 호출은 `errno`를 지우지 않는다 | 반환값(-1, NULL)을 먼저 보고 그때만 `errno`를 읽는다 |
| `syscall()`/직접 `svc`의 반환값을 libc처럼 다룸 | `-2`를 fd로 착각, `errno`가 비어 있음 | 직접 svc는 `-errno`를 그대로 준다 | `syscall()` 래퍼를 쓰거나 `if (r < 0 && r > -4096)`로 판정 |
| 장치 major 번호를 코드에 하드코딩 | 다른 보드·커널에서 엉뚱한 장치 열기 | 동적 major는 부팅마다 달라질 수 있다 | `/dev` 이름이나 `/sys/class/...`로 찾는다, 드라이버는 `alloc_chrdev_region` |
| `/dev`에 노드가 없으니 드라이버 문제라고 단정 | 드라이버 코드만 들여다보며 시간 낭비 | DT·버스 단계에서 디바이스가 아예 안 생겼을 수 있다 | `/sys/bus/<bus>/devices` → `driver` 링크 → `dmesg` 순서로 본다 |
| `/proc`·`/sys` 파일을 한 번 열고 계속 `read` | 두 번째부터 0바이트(EOF) | seq_file·sysfs는 offset 기반, 끝까지 읽으면 EOF | 매 주기 `lseek(fd, 0, SEEK_SET)` 후 읽거나 다시 연다 |
| 1바이트씩 `read()`하는 루프 | CPU 사용률이 이상하게 높다 | syscall 고정 비용(수백 ns) × 호출 수 | 버퍼로 크게 읽기, 드라이버 쪽 FIFO/버퍼 사용 |
| `strace -T`의 시간을 실제 성능으로 믿음 | 실제보다 수십 배 느린 것으로 오판 | ptrace 정지·재개 오버헤드 | `clock_gettime` 직접 측정, `perf trace`, ftrace |
| sysfs 속성 하나에 여러 값·표를 넣는 드라이버 | 리뷰에서 거절, 파싱 깨짐 | sysfs 규칙은 "파일 하나에 값 하나" | 값마다 속성 분리, 덤프성 정보는 debugfs로 |
| 모듈이 커널 안에서 격리된다고 생각 | 모듈 버그로 전체 시스템 Oops·hang | 모듈은 커널 주소 공간에 그대로 링크된다 | 커널 코드는 bare-metal처럼 방어적으로, KASAN·lockdep으로 테스트 (M) |

---

## 13. 면접에서 이렇게 말한다

**Q.** "What is the difference between user space and kernel space?"

**A.** 같은 CPU에서 도는 코드가 특권 레벨과 메모리 권한으로 나뉜 것이다. ARM에선 유저가 EL0, 커널이 EL1이고, MMU 페이지 테이블의 권한 비트로 커널 메모리는 EL0에서 접근이 막힌다. 그래서 유저 프로세스는 다른 프로세스나 커널을 깨뜨릴 수 없고, 하드웨어가 필요하면 syscall로 커널에 부탁한다. 커널 안에는 그런 보호가 없어서 드라이버 버그는 시스템 전체 문제가 된다.

> User space and kernel space are separated by CPU privilege and the MMU. On ARM, applications run at EL0 and the kernel at EL1; kernel mappings are marked privileged-only in the page tables, so a user process can't touch kernel memory or hardware registers directly — it has to ask the kernel through a system call. That isolation means a buggy app just gets a SIGSEGV, whereas a bug in kernel code, including drivers, can take down the whole system.

**꼬리 질문**: "How does the kernel safely access a user buffer?" → `copy_from_user`/`copy_to_user`, 주소 검증과 fault 처리, arm64 PAN.

**Q.** "Walk me through what happens when a program calls open() on a device file."

**A.** libc가 `openat` 번호를 x8에 넣고 `svc #0`을 실행하면 EL1 예외 벡터로 들어가 syscall 테이블에서 `sys_openat`을 부른다. VFS가 경로를 탐색해 inode를 찾고, 장치 파일이면 inode에 든 major:minor로 등록된 cdev를 찾아 `struct file`의 `f_op`를 드라이버의 `file_operations`로 바꾸고 드라이버의 `.open`을 호출한다. 마지막으로 fd 테이블의 빈 번호에 `struct file`을 넣고 그 번호를 돌려준다. 이후 `read`/`write`는 fd → `struct file` → `f_op->read`로 바로 드라이버에 간다.

> The libc wrapper puts the openat syscall number in x8 and executes svc #0, which traps to EL1 and dispatches through the syscall table to do_sys_openat2. The VFS walks the path to the inode; for a character device the inode holds a dev_t, so chrdev_open looks up the registered cdev by major/minor, swaps the file's f_op to the driver's file_operations and calls the driver's open, which usually stashes its device struct in file->private_data. The kernel then installs the struct file in the lowest free slot of the fd table and returns that number. Later reads and writes go fd → struct file → f_op->read/write straight into the driver.

**꼬리 질문**: "Where would a driver store per-open state?" → `file->private_data`.

**Q.** "What does 'everything is a file' mean in Linux? Is it actually true?"

**A.** 일반 파일뿐 아니라 장치, 파이프, 소켓, 커널 정보(`/proc`, `/sys`)까지 fd 하나와 open/read/write/close라는 공통 인터페이스로 다룬다는 뜻이다. VFS가 `file_operations` 함수 포인터 테이블로 이것을 구현한다. 완전히 참은 아니다 — 네트워크 인터페이스는 `/dev` 노드가 없고 소켓·netlink로 다루며, 많은 장치 제어는 결국 `ioctl`이라는 비정형 탈출구를 쓴다.

> It means most kernel objects — regular files, device nodes, pipes, sockets, and kernel state under /proc and /sys — are accessed through a file descriptor with the same open/read/write/close/ioctl/poll interface. The VFS implements that with a per-object file_operations table, so the same read() can land in ext4, the pipe code, the socket layer or a driver. It's not literally everything: network interfaces have no device node and are managed through sockets and netlink, and a lot of device control escapes into ioctl.

**Q.** "What are major and minor numbers?"

**A.** 장치 파일 inode에 들어 있는 장치 주소다. major는 어느 드라이버인지, minor는 그 드라이버 안에서 몇 번째 장치인지를 뜻한다. 예를 들어 `/dev/null`과 `/dev/zero`는 같은 major 1(mem 드라이버)에 minor 3과 5다. 요즘 드라이버는 `alloc_chrdev_region`으로 major를 동적으로 받고, 노드는 devtmpfs와 udev가 자동으로 만든다.

> They're the device address stored in a device node's inode: the major selects the driver, the minor is interpreted by that driver to pick an instance. /dev/null and /dev/zero, for example, are both major 1 — the mem driver — with minors 3 and 5. Modern drivers get a dynamic major with alloc_chrdev_region, and the node itself is created automatically by devtmpfs and adjusted by udev, so you shouldn't hard-code majors.

**Q.** "What's the difference between /proc, /sys and /dev?"

**A.** `/dev`는 I/O를 하는 장치 노드(입구), `/sys`는 커널 디바이스 모델(bus·device·driver·class)을 그대로 보여 주는 트리로 파일 하나에 값 하나 규칙을 따르고, `/proc`은 원래 프로세스 정보용이며 `/proc/interrupts`, `/proc/meminfo` 같은 오래된 시스템 정보와 sysctl이 남아 있다. 새 드라이버가 설정값을 노출할 때는 sysfs, 디버그 덤프는 debugfs를 쓴다.

> /dev holds device nodes — the entry points you open to do I/O. /sys is the kernel's device model exported as a tree of buses, devices, drivers and classes, with the rule of one value per attribute file, and it's what udev and bring-up debugging rely on. /proc started as per-process information and still carries legacy system files like /proc/interrupts and /proc/meminfo plus the sysctl tree. For a new driver, tunables go to sysfs and free-form debug output goes to debugfs, not /proc.

**Q.** "How expensive is a system call, and how do you avoid the cost?"

**A.** 커널이 하는 일이 거의 없는 `getpid`도 함수 호출보다 두 자릿수 이상 비싸다 — 내가 arm64 VM에서 재 보니 함수 호출 1 ns, `getpid` 약 125 ns, `open+close` 약 670 ns였다. 비용은 예외 진입·레지스터 저장·완화 코드·복귀 검사에서 온다. 줄이는 방법은 크게 읽고 쓰기(배치), `mmap` 공유 버퍼, `epoll`로 대기 묶기, 그리고 vDSO처럼 아예 커널에 안 들어가는 경로(`clock_gettime`은 약 19 ns)다.

> Even a trivial syscall like getpid is two orders of magnitude more than a function call — on an arm64 VM I measured about 1 ns for a call, roughly 125 ns for getpid and about 670 ns for open plus close. The cost is the exception entry, saving and restoring registers, speculation mitigations and the return-path checks. You amortize it by batching I/O, sharing buffers with mmap, waiting on many sources with epoll, and using the vDSO, which handles clock_gettime entirely in user space in about 19 ns.

**Q.** "Linux is a monolithic kernel — what does that mean, and how do modules fit in?"

**A.** 스케줄러·메모리·파일시스템·네트워크·드라이버가 모두 커널 주소 공간에서 같은 특권으로 돌고, 서로 함수 호출로 통신한다는 뜻이다. 빠르지만 드라이버 버그가 커널 전체를 죽인다. 모듈은 런타임에 `.ko`를 커널에 링크하는 장치일 뿐 격리가 아니다 — 들어가면 빌트인과 같은 권한이다. QNX 같은 마이크로커널은 드라이버를 유저 프로세스로 돌려 격리하지만 IPC 비용을 낸다.

> Monolithic means the scheduler, memory management, filesystems, networking and drivers all run in one kernel address space at the same privilege and call each other directly. That's fast but a single driver bug can crash everything. Loadable modules don't change that — a .ko is just linked into the running kernel and has full privilege once loaded; it gives flexibility, not isolation. A microkernel like QNX runs drivers as user processes for fault isolation, at the cost of message-passing overhead.

**꼬리 질문**: "Why must the rootfs storage driver be built-in or in the initramfs?" → 모듈을 읽어 올 파일시스템이 아직 없으므로.

**Q.** "How would you find out which driver is behind /dev/ttyS0?"

**A.** `stat`으로 major:minor를 얻고 `/sys/dev/char/<major>:<minor>`를 따라가면 `/sys/devices/...`의 실제 디바이스가 나온다. 그 디렉터리의 `device/driver` 링크가 바인딩된 드라이버다. `/proc/devices`로 major의 이름도 확인할 수 있다. 내 VM에서는 `/dev/ttyS0`(4:64)이 platform 버스의 `serial8250` 아래 포트로 나왔다.

> Stat the node to get major:minor, then follow `/sys/dev/char/<major>:<minor>`, which links into the real device under /sys/devices. The device's driver symlink names the bound driver, and /proc/devices maps the major to a name. On my test VM /dev/ttyS0, 4:64, resolved to a port under the serial8250 platform device.

---

## 14. 직접 해보기

1. **개념**: arm64에서 `read(fd, buf, n)` 한 번이 일어날 때 레지스터 `x0`, `x1`, `x2`, `x8`에는 각각 무엇이 들어가고, 반환 후 `x0`에는 무엇이 들어오나? 실패하면 어떻게 다른가?
   정답/힌트: x0=fd, x1=buf, x2=n, x8=63. 반환 x0=읽은 바이트 수(0이면 EOF), 실패면 `-errno`이고 libc가 -1과 `errno`로 바꾼다.
2. **코드**: ex1을 고쳐서 `getpid`를 인라인 `svc #0`으로 부르는 `raw_getpid()`를 만들고 libc `getpid()`와 값이 같은지, ex6 방식으로 시간도 같은지 확인하라.
   정답/힌트: `x8 = __NR_getpid`(172), 인자 없음, clobber에 `"memory"`. 시간은 libc와 거의 같아야 한다 (래퍼 비용은 무시할 수준).
3. **strace 관찰**: `strace -f -e trace=%file,%desc sh -c 'echo hi > /tmp/A1/out.txt'`를 실행하고, 셸 리다이렉션이 어떤 syscall들(`openat`, `dup3`, `close`)로 구현되는지 설명하라.
   정답/힌트: 셸이 파일을 `openat(O_WRONLY|O_CREAT|O_TRUNC)`으로 열고 `dup3`(또는 `dup2`)로 fd 1에 복제한 뒤 원래 fd를 닫는다. "fd 1이 무엇을 가리키나"만 바꾸는 것이다 (A3).
4. **/proc 관찰**: 터미널 하나에서 `sleep 1000 &`를 실행하고 그 PID의 `/proc/<pid>/syscall`, `/proc/<pid>/wchan`, `/proc/<pid>/status`의 `State`를 읽어라. 무엇에서 잠들어 있나?
   정답/힌트: 다른 프로세스의 `syscall` 파일은 ptrace 권한이 필요해서 일반 유저로는 `Operation not permitted`가 나고 `sudo`로 읽어야 한다. 이 VM에서는 첫 숫자 115(`clock_nanosleep`), `State: S (sleeping)`, `wchan` = `hrtimer_nanosleep`이 나왔다.
5. **sysfs 탐색 코드**: `/sys/class/net/*/statistics/rx_bytes`를 1초 간격으로 두 번 읽어 인터페이스별 수신 속도(B/s)를 출력하는 C 프로그램을 짜라. 파일은 매번 다시 열거나 `lseek`하라.
   정답/힌트: `opendir("/sys/class/net")`로 이름을 얻고, 각 경로를 `fopen`→`fscanf("%llu")`→`fclose`. 같은 fd로 두 번째 `read`를 하면 EOF가 나는 것도 일부러 확인해 보라.
6. **ftrace 관찰**: §10.3의 첫 실험에서 `chrdev_open` 대신 `write_null`을 필터로 걸고 `func_stack_trace`를 켜서, `write()`가 `write_null`까지 가는 스택을 직접 얻어라.
   정답/힌트: `el0t_64_sync` … `__arm64_sys_write` → `ksys_write` → `vfs_write` → `write_null` 순서가 보여야 한다. 끝나면 `set_ftrace_filter`, `set_ftrace_pid`를 비우고 `current_tracer`를 `nop`으로 되돌린다.
7. **개념**: `/dev/ttyUSB0`이 꽂을 때마다 `ttyUSB1`로 바뀌어 GNSS 데몬이 실패한다. 이 노트의 어떤 메커니즘으로 고치나?
   정답/힌트: 장치 등록 → uevent → udev 규칙. `udevadm info -a /dev/ttyUSB0`으로 `serial`·`idVendor` 같은 속성을 찾고, `SYMLINK+="gnss0"` 규칙을 만들어 데몬은 `/dev/gnss0`을 연다 (G9, L).

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| user space / kernel space | 유저 공간 / 커널 공간 | EL0에서 도는 프로세스 영역 / EL1에서 도는 커널 영역 |
| Exception Level (EL) | ARM 특권 레벨 | EL0 유저, EL1 커널, EL2 하이퍼바이저, EL3 secure monitor |
| syscall | 시스템 콜 | 유저가 `svc #0`으로 커널 서비스를 요청하는 정해진 입구 |
| `svc #0` | Supervisor Call | arm64에서 EL0 → EL1 동기 예외를 일으키는 명령 |
| errno | 에러 번호 | 커널의 `-errno`를 libc가 저장해 두는 스레드별 변수 |
| vDSO | virtual dynamic shared object | 커널이 모든 프로세스에 매핑하는 작은 라이브러리, `clock_gettime`을 커널 진입 없이 처리 |
| strace | syscall 추적기 | `ptrace`로 다른 프로세스의 syscall과 반환값을 찍는 도구 |
| ftrace | 커널 함수 추적기 | `/sys/kernel/tracing`으로 제어하는 커널 내장 트레이서 |
| VFS | Virtual File System | 모든 "파일"에 공통 API를 주고 실제 구현으로 디스패치하는 계층 |
| file descriptor (fd) | 파일 디스크립터 | 프로세스 fd 테이블의 인덱스, `struct file`을 가리킨다 |
| `struct file` | 열린 파일 객체 | `f_pos`, `f_flags`, `f_op`, `private_data`를 가진 open 한 번의 상태 |
| inode | 아이노드 | 파일 자체의 메타데이터, 장치면 major:minor를 담는다 |
| `file_operations` | 파일 연산 테이블 | `.open`/`.read`/`.write`/`.unlocked_ioctl` 등 함수 포인터 구조체 |
| char device | 문자 디바이스 | 바이트 스트림 장치, 드라이버 `file_operations`로 직행 |
| block device | 블록 디바이스 | 블록 단위 장치, page cache와 block layer를 거친다 |
| major / minor | 장치 번호 | 드라이버 선택 번호 / 드라이버 안 인스턴스 번호 |
| procfs | `/proc` | 프로세스 정보와 일부 시스템 정보를 보여 주는 가상 FS |
| sysfs | `/sys` | 커널 디바이스 모델을 트리로 보여 주는 가상 FS, 파일 하나에 값 하나 |
| devtmpfs | `/dev` | 장치 등록 시 커널이 노드를 자동 생성하는 가상 FS |
| uevent | 장치 이벤트 | 장치 추가·제거를 netlink로 유저 공간에 알리는 메시지 |
| udev | 장치 관리자 | uevent를 받아 권한·이름·심볼릭 링크·모듈 로드를 처리하는 데몬 |
| monolithic kernel | 모놀리식 커널 | 모든 서브시스템과 드라이버가 한 커널 주소 공간에서 도는 구조 |
| LKM | loadable kernel module | 런타임에 커널에 링크되는 `.ko` 파일, 격리는 없다 |
| microkernel | 마이크로커널 | IPC·스케줄·MMU만 커널에 두고 나머지를 유저 서버로 돌리는 구조 |

---

## 16. 요약 & 체크리스트

Linux는 같은 CPU 위에서 커널(EL1)과 유저 프로세스(EL0)를 특권 레벨과 MMU로 갈라 놓고, 유저는 `svc #0` syscall이라는 좁은 문으로만 커널에 일을 부탁한다. syscall은 x8에 번호, x0~x5에 인자, x0에 결과(실패면 `-errno`)를 싣는 예외이고, 함수 호출보다 두 자릿수 비싸다(VM 실측: 1 ns vs 125 ns). 커널이 제공하는 대부분의 자원은 VFS를 통해 "fd로 다루는 파일"로 통일되며, `struct file`의 `f_op` 테이블이 같은 `read()`를 ext4, 파이프, 소켓, 드라이버로 갈라 보낸다. 장치 파일은 major:minor 주소만 가진 inode이고, `open` 때 `chrdev_open`이 그 번호로 드라이버의 `file_operations`를 찾아 끼운다. `/dev`는 I/O 입구, `/sys`는 디바이스 모델 트리, `/proc`은 프로세스·커널 상태 계기판이고, 장치 등록 → sysfs + devtmpfs 노드 + uevent → udev가 하나의 흐름이다. Linux는 monolithic이라 모듈도 커널 안에서 같은 권한으로 돈다.

- [ ] EL0/EL1과 MMU 권한 비트로 유저·커널이 어떻게 격리되는지, bare-metal과 비교해 설명할 수 있다
- [ ] arm64 syscall 규약(x8, x0~x5, `svc #0`, `-errno`)을 말하고 인라인 어셈블리로 syscall을 직접 부를 수 있다
- [ ] libc 래퍼, `syscall()`, 직접 `svc`의 반환값 차이와 `errno` 사용 규칙을 설명할 수 있다
- [ ] `strace`, `strace -c`로 프로그램의 syscall을 보고 내 코드와 로더·libc의 호출을 구분할 수 있다
- [ ] syscall 비용을 직접 측정하고, vDSO가 왜 빠른지 설명할 수 있다
- [ ] 7가지 파일 종류와 `st_mode`의 `S_IFMT` 비트를 코드로 판별할 수 있다
- [ ] major/minor의 의미와 `/proc/devices`, `/dev` 노드의 관계를 설명할 수 있다
- [ ] `/proc/self/fd`, `/proc/<pid>/status`, `/proc/interrupts`를 읽고 해석할 수 있다
- [ ] `/dev` 노드에서 `/sys/dev` → `/sys/devices` → `driver` 링크를 따라가 드라이버를 찾을 수 있다
- [ ] 장치 등록 → devtmpfs·sysfs → uevent → udev 흐름을 그림으로 그릴 수 있다
- [ ] `open("/dev/xyz")`가 `chrdev_open`을 거쳐 드라이버 `file_operations`에 닿는 경로를 ftrace로 확인할 수 있다
- [ ] monolithic + 모듈과 마이크로커널·RTOS의 차이, 모듈이 격리가 아닌 이유를 말할 수 있다

## 참고 자료

- man7.org: [syscall(2)](https://man7.org/linux/man-pages/man2/syscall.2.html) — 아키텍처별 syscall 호출 규약 표 (arm64: `svc #0`, x8, x0~x5)
- man7.org: [syscalls(2)](https://man7.org/linux/man-pages/man2/syscalls.2.html), [errno(3)](https://man7.org/linux/man-pages/man3/errno.3.html), [inode(7)](https://man7.org/linux/man-pages/man7/inode.7.html), [proc(5)](https://man7.org/linux/man-pages/man5/proc.5.html), [sysfs(5)](https://man7.org/linux/man-pages/man5/sysfs.5.html), [vdso(7)](https://man7.org/linux/man-pages/man7/vdso.7.html), [strace(1)](https://man7.org/linux/man-pages/man1/strace.1.html)
- Michael Kerrisk, "The Linux Programming Interface" — 3장(System Programming Concepts), 4장(File I/O), 14장(File Systems), 15장(File Attributes)
- docs.kernel.org: [Linux kernel user's and administrator's guide — devices.txt](https://docs.kernel.org/admin-guide/devices.html) (major/minor 공식 배정표)
- docs.kernel.org: [sysfs - _The_ filesystem for exporting kernel objects](https://docs.kernel.org/filesystems/sysfs.html), [Overview of the Linux Virtual File System](https://docs.kernel.org/filesystems/vfs.html), [ftrace - Function Tracer](https://docs.kernel.org/trace/ftrace.html)
- Bootlin, "Linux kernel and driver development training" 슬라이드 — 커널 구조, 문자 드라이버, 디바이스 모델 부분
- OSTEP 6장 "Mechanism: Limited Direct Execution" — trap·특권 모드·syscall의 OS 이론 배경
- elixir.bootlin.com 소스 위치: `arch/arm64/kernel/entry.S`(`el0t_64_sync`), `arch/arm64/kernel/entry-common.c`(`el0_svc`), `arch/arm64/kernel/syscall.c`(`invoke_syscall`), `fs/open.c`(`do_sys_openat2`), `fs/char_dev.c`(`chrdev_open`), `drivers/char/mem.c`(`memory_open`, `null_fops`, `write_null`)
