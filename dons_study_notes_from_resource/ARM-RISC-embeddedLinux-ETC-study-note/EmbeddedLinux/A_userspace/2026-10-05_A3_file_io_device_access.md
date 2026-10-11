# A3. 파일 I/O와 디바이스 접근 — fd 하나로 파일·pipe·센서·레지스터까지 다루는 법

> **이 노트를 다 읽으면**: fd → open file description → inode 3단 구조로 dup/fork/O_CLOEXEC 동작을 예측할 수 있다 · short read·EINTR·EAGAIN을 견디는 I/O 루프를 짤 수 있다 · stdio 버퍼, page cache, 디바이스 캐시를 구분하고 fsync를 언제 불러야 하는지 말할 수 있다 · ioctl 번호를 해독하고 `/dev/i2c-N`, spidev, sysfs로 하드웨어에 접근하는 테스트 툴을 짤 수 있다
> **왜 필요한가**: 드라이버를 만들면 그걸 검증하는 유저 프로그램을 반드시 짜게 되고, 면접에서는 "open() 하면 무슨 일이?", "fsync 안 하면 뭐가 날아가나?", "ioctl 번호는 어떻게 만들어지나?"가 단골이다.
> **Don 기준 난이도**: 버퍼·캐시·전원 차단·버스 트랜잭션은 이미 몸으로 안다 / 새로 배울 것은 POSIX 시맨틱(offset 공유, 시그널과 syscall의 상호작용)과 커널이 그걸 어떤 구조체로 구현하는지다.
> **선행 노트**: A1 (everything is a file, syscall), A2 (fork/exec)

---

## 0. 큰 그림 — 이게 왜 필요한가

bare-metal 펌웨어에서 센서를 읽을 때는 I2C 컨트롤러 레지스터를 직접 만졌다. Linux에서 유저 프로그램은 그럴 수 없다. MMU가 레지스터 영역을 매핑해 주지 않고, 매핑해 준다 해도 같은 컨트롤러를 커널 드라이버가 동시에 쓰고 있다. 그래서 Linux는 **모든 장치 접근을 "파일 열고 → 읽고/쓰고 → 특수 명령(ioctl) → 닫기"라는 하나의 인터페이스로 통일**했다. 커널은 이 호출을 받아 해당 드라이버의 함수로 넘긴다.

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <rect x="20" y="15" width="620" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="330" y="38" font-size="13" text-anchor="middle">유저 공간: 센서 앱 · 드라이버 테스트 툴 (C)</text>
<text x="330" y="60" font-size="12" text-anchor="middle">open · read · write · lseek · ioctl · fsync · close</text> <line x1="330" y1="71" x2="330" y2="108" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/> <text x="345" y="94" font-size="12" text-anchor="start">syscall (SVC 예외처럼 특권 모드로 진입)</text>
<rect x="20" y="112" width="620" height="150" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,4"/> <text x="34" y="130" font-size="13" text-anchor="start">커널</text> <rect x="150" y="120" width="360" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="330" y="145" font-size="13" text-anchor="middle">VFS: fd → struct file → f_op->read / ioctl</text> <rect x="35" y="190" width="190" height="58" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/> <text x="130" y="213" font-size="13" text-anchor="middle">/dev/i2c-0</text>
<text x="130" y="236" font-size="12" text-anchor="middle">i2c-dev → I2C 컨트롤러</text> <line x1="330" y1="160" x2="130" y2="189" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/> <line x1="130" y1="248" x2="130" y2="283" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<rect x="235" y="190" width="190" height="58" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/> <text x="330" y="213" font-size="13" text-anchor="middle">/dev/spidev0.0</text> <text x="330" y="236" font-size="12" text-anchor="middle">spidev → SPI 컨트롤러</text>
<line x1="330" y1="160" x2="330" y2="189" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/> <line x1="330" y1="248" x2="330" y2="283" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/> <rect x="435" y="190" width="190" height="58" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="530" y="213" font-size="13" text-anchor="middle">/sys/.../rx_bytes</text> <text x="530" y="236" font-size="12" text-anchor="middle">sysfs → 드라이버 show()</text> <line x1="330" y1="160" x2="530" y2="189" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<line x1="530" y1="248" x2="530" y2="283" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/> <rect x="20" y="285" width="620" height="42" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/> <text x="330" y="311" font-size="13" text-anchor="middle">하드웨어: IMU (I2C) · 자이로 (SPI) · NIC · eMMC</text>
</svg>
```
그림 1 — 유저 코드는 똑같은 open/read/write/ioctl만 부르고, VFS가 fd를 따라가서 각 드라이버의 함수로 보낸다.

bare-metal 비유로 말하면:

- **fd(file descriptor)** = 커널이 들고 있는 핸들 테이블의 인덱스. RTOS에서 `xQueueCreate()`가 돌려주는 핸들과 같은 역할이다. 유저는 숫자 하나만 들고 있고, 진짜 객체는 커널 메모리에 있다.
- **read/write** = 드라이버의 `.read`/`.write` 콜백을 부르는 통로. HAL의 `HAL_I2C_Master_Receive()`를 직접 부르는 대신 커널에 "이 fd로 읽어 줘"라고 부탁한다.
- **ioctl** = "데이터 스트림으로 표현하기 어려운 제어 명령"용 만능 통로. 레지스터 맵의 control register에 비유할 수 있다.
- **page cache** = SSD 펌웨어의 DRAM write cache와 같은 개념이다. 쓰기는 일단 RAM에 들어가고 나중에 매체로 내려간다. 그래서 fsync가 필요하다.

이 노트의 예제는 모두 Lima Ubuntu VM(aarch64, `uname -r` = `7.0.0-34-generic`, gcc 15.2)에서 `-Wall -Wextra` 경고 0개로 컴파일하고 실행한 결과다.

---

## 1. File descriptor와 3단 구조

### 1.1 직관 — 번호표, 대출 기록, 책

도서관에 비유하면:

- **fd** = 내 손에 든 번호표 (프로세스마다 따로 번호를 매긴다. 0, 1, 2, 3 …)
- **open file description(OFD)** = 대출 기록 카드. "몇 쪽까지 읽었나(offset)", "어떤 모드로 빌렸나(읽기 전용, append …)"가 적혀 있다. `open()`을 부를 때마다 새 카드가 생긴다.
- **inode** = 책 그 자체. 크기, 권한, 데이터가 디스크 어디 있는지. 파일마다 하나다.

번호표 여러 장이 같은 대출 카드를 가리킬 수 있고, 대출 카드 여러 장이 같은 책을 가리킬 수 있다. 이 "여러 대 하나" 관계 두 단계가 dup, fork, O_APPEND, 파일 잠금의 동작을 전부 설명한다.

```svg
<svg viewBox="0 0 660 310" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="95" y="26" font-size="13" text-anchor="middle">fd table (프로세스 A)</text> <text x="340" y="26" font-size="13" text-anchor="middle">open file description</text>
<text x="565" y="26" font-size="13" text-anchor="middle">inode</text> <rect x="20" y="40" width="150" height="26" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/> <text x="30" y="57" font-size="12" text-anchor="start">0  stdin</text>
<rect x="20" y="70" width="150" height="26" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/> <text x="30" y="87" font-size="12" text-anchor="start">1  stdout</text> <rect x="20" y="100" width="150" height="26" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30" y="117" font-size="12" text-anchor="start">2  stderr</text> <rect x="20" y="130" width="150" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="30" y="147" font-size="12" text-anchor="start">3  a = open()</text>
<rect x="20" y="160" width="150" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="30" y="177" font-size="12" text-anchor="start">4  b = open()</text> <rect x="20" y="190" width="150" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="207" font-size="12" text-anchor="start">5  c = dup(a)</text> <rect x="20" y="220" width="150" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="30" y="237" font-size="12" text-anchor="start">10 dup2(a,10)</text>
<rect x="235" y="70" width="210" height="80" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="340" y="92" font-size="12" text-anchor="middle">open file description #1</text> <text x="340" y="114" font-size="12" text-anchor="middle">pos = 4, O_RDWR</text>
<text x="340" y="136" font-size="12" text-anchor="middle">fd 3 · 5 · 10 이 공유</text> <rect x="235" y="180" width="210" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/> <text x="340" y="202" font-size="12" text-anchor="middle">open file description #2</text>
<text x="340" y="224" font-size="12" text-anchor="middle">pos = 0, O_RDONLY</text> <rect x="490" y="110" width="150" height="90" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/> <text x="565" y="135" font-size="13" text-anchor="middle">inode</text>
<text x="565" y="157" font-size="12" text-anchor="middle">/tmp/A3/data.txt</text> <text x="565" y="179" font-size="12" text-anchor="middle">크기 · 권한 · 블록</text> <line x1="170" y1="143" x2="234" y2="118" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<line x1="170" y1="203" x2="234" y2="133" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/> <line x1="170" y1="233" x2="234" y2="146" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/> <line x1="170" y1="173" x2="234" y2="203" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<line x1="445" y1="110" x2="489" y2="140" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/> <line x1="445" y1="210" x2="489" y2="175" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/> <text x="95" y="278" font-size="12" text-anchor="middle">프로세스마다 1개</text>
<text x="95" y="296" font-size="12" text-anchor="middle">FD_CLOEXEC 비트는 여기</text> <text x="340" y="278" font-size="12" text-anchor="middle">open() 할 때마다 1개</text> <text x="340" y="296" font-size="12" text-anchor="middle">pos, O_APPEND, O_NONBLOCK</text>
<text x="565" y="278" font-size="12" text-anchor="middle">파일마다 1개</text> <text x="565" y="296" font-size="12" text-anchor="middle">디스크 위 실체</text>
</svg>
```
그림 2 — 예제 ex1의 상태. fd 3·5·10은 같은 OFD를 공유해서 offset도 공유한다. fd 4는 같은 파일을 따로 open 했으므로 offset이 독립이다.

### 1.2 정의

| 층 | 누가 소유 | 무엇이 들어 있나 | 커널 구조체 |
|---|---|---|---|
| fd table | 프로세스(정확히는 `files_struct`를 공유하는 스레드 묶음) | fd 번호 → OFD 포인터, close-on-exec 비트 | `struct files_struct`, `struct fdtable` |
| open file description | 커널 전역, 참조 카운트로 관리 | 현재 offset, 상태 플래그(O_APPEND, O_NONBLOCK …), 접근 모드, `f_op` | `struct file` |
| inode | 파일시스템 | 크기, 권한, 소유자, 타임스탬프, 데이터 블록 위치, 장치 번호(장치 파일이면) | `struct inode` |

용어 주의: POSIX 표준은 가운데 층을 **open file description**이라 부르고, 커널 코드는 `struct file`이라 부른다. 둘은 같은 것이다. "file descriptor"(fd, 번호)와 "file description"(객체)을 헷갈리지 말 것.

fd 번호 규칙 두 가지:

1. `open()`은 **비어 있는 가장 작은 번호**를 돌려준다. 그래서 `close(1)` 후 `open()`하면 stdout이 그 파일로 바뀐다 (셸 리다이렉션의 원리).
2. 프로세스당 개수 제한은 `RLIMIT_NOFILE`이다. 이 VM에서 `ulimit -n`은 soft 1024, hard 524288이었다. 넘으면 `EMFILE`.

### 1.3 예제 ex1 — open 두 번 vs dup

무엇을 확인하나: 같은 파일을 `open()` 두 번 하면 offset이 따로 놀고, `dup()`/`dup2()`로 만든 fd는 offset을 공유하는지 `/proc/self/fdinfo`로 직접 본다.

```c
/* ex1: open() 두 번 vs dup() — offset을 공유하는가? */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void die(const char *m) { perror(m); exit(1); }

static void show(const char *tag, int fd)
{
    char path[64], line[128];
    snprintf(path, sizeof path, "/proc/self/fdinfo/%d", fd);
    FILE *f = fopen(path, "r");
    if (!f) die("fopen fdinfo");
    printf("%-8s fd=%d  ", tag, fd);
    while (fgets(line, sizeof line, f))           /* pos, flags 두 줄만 */
        if (!strncmp(line, "pos:", 4) || !strncmp(line, "flags:", 6)) {
            line[strcspn(line, "\n")] = 0;
            for (char *q = line; *q; q++) if (*q == '\t') *q = ' ';
            printf("%-18s", line);
        }
    printf("\n");
    fclose(f);
}

int main(void)
{
    int a = open("/tmp/A3/data.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (a < 0) die("open a");
    if (write(a, "0123456789", 10) != 10) die("write");
    int b = open("/tmp/A3/data.txt", O_RDONLY);   /* 새 open file description */
    int c = dup(a);                                /* a와 같은 description */
    if (b < 0 || c < 0) die("open/dup");

    if (lseek(a, 3, SEEK_SET) < 0) die("lseek");
    show("a", a); show("b(open)", b); show("c(dup)", c);

    char ch;
    if (read(c, &ch, 1) != 1) die("read c");
    printf("read(c) -> '%c'   (a의 offset 3에서 읽음)\n", ch);
    show("a", a);

    if (dup2(a, 10) < 0) die("dup2");             /* fd 10을 a의 복제로 */
    printf("dup2(a,10): fd10 pos = %ld\n", (long)lseek(10, 0, SEEK_CUR));
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /tmp/A3 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/A3/ex1_fdtable.c -o /tmp/A3/ex1 && cd /tmp/A3 && ./ex1'
```

```text
a        fd=3  pos: 3            flags: 0400002    
b(open)  fd=4  pos: 0            flags: 0400000    
c(dup)   fd=5  pos: 3            flags: 0400002    
read(c) -> '3'   (a의 offset 3에서 읽음)
a        fd=3  pos: 4            flags: 0400002    
dup2(a,10): fd10 pos = 4
```

출력에서 볼 것:

- `lseek(a, 3)`만 했는데 c(dup)의 `pos`도 3이다. 둘이 같은 OFD를 가리키기 때문이다. b는 0 그대로다.
- `read(c)`가 '3'을 읽고 나니 **a의 pos가 4**로 움직였다. offset은 fd가 아니라 OFD에 있다.
- `flags: 0400002`는 8진수다. 끝의 `2`가 `O_RDWR`, `0400000`은 arm64에서의 `O_LARGEFILE`이다(64비트 커널은 자동으로 붙인다). 아키텍처마다 이 비트 값이 다르므로 숫자를 외우지 말고 헤더 상수로 비교한다.
- `/proc/<pid>/fdinfo/<fd>`는 디버깅에 아주 유용하다. 실행 중인 데몬이 파일을 어디까지 읽었는지 바로 보인다.

### 1.4 셸에서 fd 들여다보기

```sh
ls -l /proc/self/fd          # 이 명령(ls) 자신의 fd들
lsof -p <pid>                # 특정 프로세스가 연 모든 파일
cat /proc/<pid>/fdinfo/3     # pos, flags, mnt_id, ino
```

`ls -l /proc/<pid>/fd`의 링크 대상이 `pipe:[98971]`, `socket:[…]`, `/dev/i2c-0`처럼 나오는데, 이것이 "everything is a file"의 실체다. 종류가 무엇이든 fd table 입장에서는 똑같이 `struct file` 하나다.

---

## 2. dup/dup2, fork 상속, O_CLOEXEC

### 2.1 dup과 dup2

- `dup(old)`: 비어 있는 가장 작은 번호에 `old`와 **같은 OFD**를 가리키는 새 fd를 만든다.
- `dup2(old, new)`: 번호를 `new`로 지정한다. `new`가 열려 있으면 먼저 닫는다(원자적으로). 셸의 `2>&1`이 바로 `dup2(1, 2)`다.
- `dup3(old, new, O_CLOEXEC)`: dup2 + close-on-exec 비트를 같이 세팅 (Linux 전용).
- dup으로 만든 새 fd는 **FD_CLOEXEC 비트가 꺼진 상태**로 시작한다. 이 비트는 OFD가 아니라 fd 쪽(그림 2의 왼쪽 열)에 있기 때문이다. 그래서 ex1의 c(dup) flags에 O_CLOEXEC가 보이지 않는다.

### 2.2 fork는 fd table을 복사하고, OFD는 공유한다

`fork()`는 부모의 fd table을 통째로 복사한다. 하지만 복사된 항목이 가리키는 OFD는 **복사하지 않고 참조 카운트만 올린다**. 결과적으로 부모와 자식은 같은 offset을 공유한다. 로그 파일을 부모·자식이 같이 쓰면 서로 덮어쓰지 않고 이어서 써지는 이유가 이것이다.

`exec()`는 fd table을 유지한다. 다만 **close-on-exec 비트가 켜진 fd는 exec 순간에 닫힌다**. `O_CLOEXEC`를 안 붙이면 자식 프로그램(예: `system()`으로 부른 셸 스크립트)이 부모의 장치 fd·소켓을 몰래 물려받는다. 이것을 **fd 누수(fd leak)**라고 부른다.

```svg
<svg viewBox="0 0 660 285" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="105" y="32" font-size="13" text-anchor="middle">부모</text> <text x="105" y="172" font-size="13" text-anchor="middle">자식 (fork 직후 복사본)</text>
<rect x="20" y="40" width="170" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="30" y="57" font-size="12" text-anchor="start">3 → log.txt</text> <rect x="20" y="70" width="170" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="87" font-size="12" text-anchor="start">4 → hostname, CLOEXEC</text> <rect x="20" y="100" width="170" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="30" y="117" font-size="12" text-anchor="start">5 → hostname</text>
<rect x="20" y="180" width="170" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="30" y="197" font-size="12" text-anchor="start">3 → log.txt</text> <rect x="20" y="210" width="170" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="227" font-size="12" text-anchor="start">4 → hostname, CLOEXEC</text> <rect x="20" y="240" width="170" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="30" y="257" font-size="12" text-anchor="start">5 → hostname</text>
<rect x="255" y="40" width="190" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/> <text x="350" y="60" font-size="12" text-anchor="middle">OFD: log.txt</text> <text x="350" y="80" font-size="12" text-anchor="middle">pos 공유 (자식 write → 6)</text>
<rect x="255" y="115" width="190" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/> <text x="350" y="140" font-size="12" text-anchor="middle">OFD: hostname #1</text> <rect x="255" y="190" width="190" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="350" y="215" font-size="12" text-anchor="middle">OFD: hostname #2</text> <line x1="190" y1="53" x2="254" y2="58" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/> <line x1="190" y1="193" x2="254" y2="78" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/>
<line x1="190" y1="83" x2="254" y2="130" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/> <line x1="190" y1="223" x2="254" y2="145" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="5,4" marker-end="url(#ah3)"/> <line x1="190" y1="113" x2="254" y2="203" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/>
<line x1="190" y1="253" x2="254" y2="220" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/> <rect x="470" y="40" width="175" height="80" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/> <text x="557" y="62" font-size="12" text-anchor="middle">fork = fd table 복사</text>
<text x="557" y="84" font-size="12" text-anchor="middle">OFD 는 복사 안 됨</text> <text x="557" y="106" font-size="12" text-anchor="middle">→ offset · flags 공유</text> <rect x="470" y="165" width="175" height="105" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="5,4"/>
<text x="557" y="187" font-size="12" text-anchor="middle">자식이 exec(ls) 하면</text> <text x="557" y="209" font-size="12" text-anchor="middle">fd 3: 유지</text> <text x="557" y="231" font-size="12" text-anchor="middle">fd 4: 닫힘 (O_CLOEXEC)</text>
<text x="557" y="253" font-size="12" text-anchor="middle">fd 5: 유지 → 누수</text> <text x="205" y="268" font-size="12" text-anchor="start">빨간 점선 = exec 때 끊김</text>
</svg>
```
그림 3 — fork 직후 부모·자식 fd table은 같은 OFD들을 가리킨다. 자식이 exec 하면 O_CLOEXEC로 연 fd 4만 끊긴다.

### 2.3 예제 ex2 — offset 공유와 O_CLOEXEC

무엇을 확인하나: 자식이 쓴 뒤 부모가 보는 offset, 그리고 exec된 `ls`가 물려받은 fd 목록.

```c
/* ex2: fork 후 부모·자식이 offset을 공유하는가, exec 때 O_CLOEXEC fd는 사라지는가 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void die(const char *m) { perror(m); exit(1); }

int main(void)
{
    int fd = open("/tmp/A3/log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int secret = open("/etc/hostname", O_RDONLY | O_CLOEXEC);
    int leak   = open("/etc/hostname", O_RDONLY);          /* CLOEXEC 없음 */
    if (fd < 0 || secret < 0 || leak < 0) die("open");

    pid_t pid = fork();
    if (pid < 0) die("fork");
    if (pid == 0) {                                  /* 자식 */
        if (write(fd, "child\n", 6) != 6) die("child write");
        printf("[child ] after write, pos=%ld\n", (long)lseek(fd, 0, SEEK_CUR));
        fflush(stdout);
        /* exec 후 어떤 fd가 남는지 ls로 확인 */
        execlp("ls", "ls", "-l", "/proc/self/fd", (char *)NULL);
        die("execlp");
    }
    if (waitpid(pid, NULL, 0) < 0) die("waitpid");
    printf("[parent] pos seen by parent=%ld (자식이 옮긴 offset)\n",
           (long)lseek(fd, 0, SEEK_CUR));
    if (write(fd, "parent\n", 7) != 7) die("parent write");
    printf("[parent] fd=%d secret(CLOEXEC)=%d leak=%d\n", fd, secret, leak);
    return 0;
}
```

```text
[child ] after write, pos=6
total 0
lr-x------ 1 donh donh 64 Oct  5 22:41 0 -> pipe:[98971]
l-wx------ 1 donh donh 64 Oct  5 22:41 1 -> pipe:[98972]
l-wx------ 1 donh donh 64 Oct  5 22:41 2 -> pipe:[98973]
l-wx------ 1 donh donh 64 Oct  5 22:41 3 -> /tmp/A3/log.txt
lr-x------ 1 donh donh 64 Oct  5 22:41 4 -> /proc/31923/fd
lr-x------ 1 donh donh 64 Oct  5 22:41 5 -> /etc/hostname
[parent] pos seen by parent=6 (자식이 옮긴 offset)
[parent] fd=3 secret(CLOEXEC)=4 leak=5
--- log.txt
child
parent
```

출력에서 볼 것:

- 부모는 아무것도 안 썼는데 `pos seen by parent=6`이다. 자식의 write가 공유 OFD의 offset을 옮겼다. 그래서 log.txt에 "child" 다음 "parent"가 이어서 들어갔다.
- exec된 `ls`의 fd 목록에 3(log.txt)과 5(hostname, CLOEXEC 없음)는 있지만, **원래 fd 4(CLOEXEC)는 없다**. 그 자리(4번)는 `ls`가 `/proc/self/fd` 디렉터리를 열면서 재사용했다(가장 작은 빈 번호 규칙). PID 값은 실행마다 다르다.
- fd 0이 `pipe:`인 이유: `limactl shell`이 tty 없이 실행해서 stdin/stdout이 pipe로 연결되어 있다.

### 2.4 흔한 함정

- 멀티스레드 프로그램에서 `open()` 후 `fcntl(fd, F_SETFD, FD_CLOEXEC)`를 따로 부르면, 그 사이에 다른 스레드가 `fork()+exec()`하는 경쟁이 생긴다. 그래서 **처음부터 `O_CLOEXEC`를 open 플래그로** 준다. `socket()`의 `SOCK_CLOEXEC`, `pipe2(…, O_CLOEXEC)`, `accept4()`, `dup3()`가 같은 이유로 생겼다.
- O_NONBLOCK은 OFD에 있다. 자식에게 물려준 stdin에 O_NONBLOCK을 켜면 **부모 셸의 stdin도 non-blocking**이 된다. 프로그램 종료 후 셸이 이상해지는 버그의 원인이다.

---

## 3. open 플래그

### 3.1 플래그 정리

`open(path, flags, mode)`의 `flags`는 세 그룹이다.

| 그룹 | 플래그 | 의미 | 어디에 저장되나 |
|---|---|---|---|
| 접근 모드 (하나만) | `O_RDONLY` `O_WRONLY` `O_RDWR` | 읽기/쓰기 권한 | OFD (`f_mode`) |
| 생성 플래그 (open 순간에만 작용) | `O_CREAT` | 없으면 만든다. 이때 `mode` 인자가 필요 | 남지 않음 |
| | `O_EXCL` | `O_CREAT`과 같이 쓰면 "이미 있으면 실패(EEXIST)". 락 파일·PID 파일용 | 남지 않음 |
| | `O_TRUNC` | 열면서 크기를 0으로 | 남지 않음 |
| | `O_CLOEXEC` | exec 때 자동으로 닫기 | fd table 비트 |
| | `O_DIRECTORY` `O_NOFOLLOW` | 디렉터리만 / 심볼릭 링크 따라가지 않기 | 남지 않음 |
| 상태 플래그 (F_GETFL로 보임) | `O_APPEND` | 매 write 직전에 offset을 파일 끝으로 (원자적) | OFD (`f_flags`) |
| | `O_NONBLOCK` | 기다려야 하면 기다리지 말고 EAGAIN | OFD |
| | `O_SYNC` / `O_DSYNC` | 매 write가 매체까지 내려간 뒤 반환 (fsync / fdatasync를 매번 하는 효과) | OFD |
| | `O_DIRECT` | page cache를 거치지 않는 I/O (정렬 제약 있음) | OFD |

`mode`는 `umask`로 걸러진다: 실제 권한 = `mode & ~umask`.

### 3.2 예제 ex3 — O_EXCL, umask, O_APPEND, F_SETFL, O_TRUNC

무엇을 확인하나: 생성 플래그와 상태 플래그가 실제로 어떻게 동작하고, 상태 플래그를 `fcntl`로 나중에 바꿀 수 있는지.

```c
/* ex3: O_CREAT/O_EXCL, umask, O_TRUNC, O_APPEND, fcntl(F_GETFL/F_SETFL) */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void die(const char *m) { perror(m); exit(1); }
#define F "/tmp/A3/flags.txt"

int main(void)
{
    unlink(F);
    umask(022);
    int fd = open(F, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) die("open excl");
    struct stat st;
    if (fstat(fd, &st) < 0) die("fstat");
    printf("mode requested 0666, umask 022 -> actual %04o\n", st.st_mode & 0777);
    if (write(fd, "AAAAAAAAAA", 10) != 10) die("write");
    close(fd);

    if (open(F, O_WRONLY | O_CREAT | O_EXCL, 0666) < 0)
        printf("O_CREAT|O_EXCL again -> errno=%d (%s)\n", errno, strerror(errno));

    fd = open(F, O_WRONLY | O_APPEND);
    if (fd < 0) die("open append");
    if (lseek(fd, 0, SEEK_SET) < 0) die("lseek");       /* 처음으로 옮겨도 */
    if (write(fd, "B", 1) != 1) die("write B");          /* 쓰기는 항상 끝에 */
    printf("O_APPEND: after lseek(0)+write, pos=%ld\n", (long)lseek(fd, 0, SEEK_CUR));

    int fl = fcntl(fd, F_GETFL);
    printf("F_GETFL=0%o  O_APPEND? %s  O_NONBLOCK? %s\n", fl,
           fl & O_APPEND ? "yes" : "no", fl & O_NONBLOCK ? "yes" : "no");
    if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) die("F_SETFL");
    fl = fcntl(fd, F_GETFL);
    printf("after F_SETFL: O_NONBLOCK? %s\n", fl & O_NONBLOCK ? "yes" : "no");
    close(fd);

    fd = open(F, O_WRONLY | O_TRUNC);
    if (fd < 0) die("open trunc");
    if (fstat(fd, &st) < 0) die("fstat");
    printf("O_TRUNC: size now %lld\n", (long long)st.st_size);
    close(fd);
    return 0;
}
```

```text
mode requested 0666, umask 022 -> actual 0644
O_CREAT|O_EXCL again -> errno=17 (File exists)
O_APPEND: after lseek(0)+write, pos=11
F_GETFL=0402001  O_APPEND? yes  O_NONBLOCK? no
after F_SETFL: O_NONBLOCK? yes
O_TRUNC: size now 0
```

출력에서 볼 것:

- 0666을 요청해도 umask 022 때문에 0644가 된다.
- `O_APPEND`로 연 fd는 `lseek(0)` 후 써도 **파일 끝(10 다음)에 붙고 pos가 11**이 된다. 여러 프로세스가 같은 로그 파일에 써도 서로 덮어쓰지 않는 이유다. "lseek(END) → write"를 직접 하면 두 동작 사이에 다른 프로세스가 끼어들 수 있지만, O_APPEND는 커널이 inode 락을 잡은 채로 끝 위치를 정하므로 경쟁이 없다.
- `F_SETFL`로 바꿀 수 있는 것은 O_APPEND, O_NONBLOCK, O_ASYNC, O_DIRECT, O_NOATIME 정도다. 접근 모드나 O_SYNC는 F_SETFL로 바뀌지 않는다(Linux에서 O_SYNC 변경은 무시된다).

### 3.3 O_DIRECT 맛보기 — ex17

`O_DIRECT`는 page cache를 건너뛰고 유저 버퍼에서 장치로 바로 DMA하게 한다. DB 엔진처럼 자기 캐시를 가진 프로그램이 쓴다. 대가는 **정렬 제약**이다: 버퍼 주소, 길이, 파일 offset이 장치의 논리 블록 크기(보통 512 B 또는 4 KiB) 배수여야 한다. Don이 SSD 펌웨어에서 봤던 "호스트 DMA 버퍼는 섹터 정렬이어야 한다"는 요구가 유저 API까지 올라온 모습이다.

무엇을 확인하나: 정렬이 어긋나면 EINVAL이 나는 것.

```c
/* ex17: O_DIRECT는 page cache를 건너뛴다 — 대신 정렬 규칙을 지켜야 한다 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    int fd = open("/var/tmp/A3/direct.bin", O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);
    if (fd < 0) { perror("open O_DIRECT"); return 1; }

    static char raw[8192 + 1];
    char *unaligned = raw + 1;                           /* 주소가 1바이트 어긋남 */
    ssize_t w = write(fd, unaligned, 4096);
    printf("unaligned buf, 4096 B -> %zd %s\n", w, w < 0 ? strerrorname_np(errno) : "");

    void *al;
    if (posix_memalign(&al, 4096, 4096) != 0) { perror("posix_memalign"); return 1; }
    memset(al, 'd', 4096);
    w = write(fd, al, 100);                               /* 길이가 블록 배수 아님 */
    printf("aligned buf, 100 B   -> %zd %s\n", w, w < 0 ? strerrorname_np(errno) : "");
    w = write(fd, al, 4096);
    printf("aligned buf, 4096 B  -> %zd\n", w);
    free(al);
    close(fd);
    return 0;
}
```

```text
unaligned buf, 4096 B -> -1 EINVAL
aligned buf, 100 B   -> -1 EINVAL
aligned buf, 4096 B  -> 4096
```

출력에서 볼 것: 주소가 1바이트 어긋나도, 길이가 100바이트여도 EINVAL. `posix_memalign`으로 4 KiB 정렬한 버퍼로 4096 B를 쓰면 성공한다. (`/sys/block/vda/queue/logical_block_size`는 512였다. 파일시스템마다 요구치가 다를 수 있으므로 4 KiB 정렬이 무난하고, 정확한 값은 `statx()`의 `STATX_DIOALIGN`으로 물어볼 수 있다.) O_DIRECT는 **내구성 보장이 아니다**. 장치의 쓰기 캐시에는 여전히 머물 수 있으므로 전원 안전을 원하면 fsync/fdatasync가 따로 필요하다.

---

## 4. read/write의 진짜 의미 — short count, EINTR, EAGAIN

### 4.1 직관

`read(fd, buf, n)`은 "n바이트를 읽어라"가 아니라 **"최대 n바이트까지, 지금 줄 수 있는 만큼 줘라"**다. UART RX FIFO에서 "남은 바이트만큼만 꺼내는" 것과 같다. 반환값 규칙:

| 반환값 | 의미 | 해야 할 일 |
|---|---|---|
| `n` | 요청한 만큼 다 됨 | 다음으로 |
| `0 < r < n` | short read/write. 지금 있는 만큼만 | 나머지를 이어서 다시 호출 |
| `0` (read) | EOF. pipe·소켓은 상대가 닫음 | 루프 종료 |
| `-1`, `errno == EINTR` | 데이터 옮기기 전에 시그널이 끼어듦 | 그냥 다시 호출 |
| `-1`, `errno == EAGAIN` | non-blocking인데 지금 데이터/공간 없음 | poll/epoll로 기다렸다가 다시 (A4) |
| `-1`, 그 밖의 errno | 진짜 에러 (EIO, ENODEV, EFAULT …) | 처리/보고 |

short count가 나오는 대표 장면: pipe·소켓·tty에서 지금 버퍼에 있는 양이 요청보다 적을 때, 시그널이 일부 전송 후에 끼어들 때, non-blocking pipe에 남은 공간이 요청보다 적을 때, 디스크가 꽉 찼을 때, 그리고 sysfs·드라이버가 원래 한 번에 그만큼만 줄 때. 정규 파일은 보통 다 읽혀서 "short read는 안 일어난다"고 착각하기 쉬운데, 장치·pipe에서는 일상이다.

### 4.2 예제 ex4 — short read와 read_full/write_full

무엇을 확인하나: 30바이트를 요청해도 pipe에 10바이트만 있으면 10이 돌아오는 것, 그리고 루프로 원하는 양을 채우는 표준 함수.

```c
/* ex4: pipe에서 short read가 실제로 나오는 것 + read_full/write_full 루프 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static void die(const char *m) { perror(m); exit(1); }

/* n바이트를 다 읽거나, EOF/에러에서 멈춘다. 반환: 읽은 바이트 수, 에러면 -1 */
ssize_t read_full(int fd, void *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r < 0) {
            if (errno == EINTR) continue;      /* 시그널에 끊김 → 다시 */
            return -1;                         /* EAGAIN 포함 진짜 에러 */
        }
        if (r == 0) break;                     /* EOF: 상대가 닫음 */
        got += (size_t)r;
    }
    return (ssize_t)got;
}

ssize_t write_full(int fd, const void *buf, size_t n)
{
    size_t put = 0;
    while (put < n) {
        ssize_t w = write(fd, (const char *)buf + put, n - put);
        if (w < 0 && errno == EINTR) continue;
        if (w < 0) return -1;
        put += (size_t)w;
    }
    return (ssize_t)put;
}

int main(void)
{
    int p[2];
    if (pipe(p) < 0) die("pipe");
    pid_t pid = fork();
    if (pid < 0) die("fork");
    if (pid == 0) {                            /* 자식: 10바이트씩 3번, 사이에 쉼 */
        close(p[0]);
        for (int i = 0; i < 3; i++, usleep(100 * 1000))
            if (write_full(p[1], "ABCDEFGHIJ", 10) != 10) die("write_full");
        _exit(0);
    }
    close(p[1]);
    char buf[64];
    ssize_t r = read(p[0], buf, 30);           /* 30바이트 달라고 해도 */
    printf("plain read(30)  -> %zd bytes\n", r);
    ssize_t f = read_full(p[0], buf, 20);      /* 나머지 20바이트는 루프로 */
    printf("read_full(20)   -> %zd bytes\n", f);
    printf("read_full(10) at EOF -> %zd bytes\n", read_full(p[0], buf, 10));
    waitpid(pid, NULL, 0);
    return 0;
}
```

```text
plain read(30)  -> 10 bytes
read_full(20)   -> 20 bytes
read_full(10) at EOF -> 0 bytes
```

`strace -f -e trace=read,write`로 보면 커널이 정확히 무엇을 돌려줬는지 보인다 (동적 링커의 read 한 줄과 무관한 줄은 생략):

```text
202668 read(3 <unfinished ...>
202669 write(4, "ABCDEFGHIJ", 10 <unfinished ...>
202668 <... read resumed>, "ABCDEFGHIJ", 30) = 10
202668 read(3 <unfinished ...>
202669 write(4, "ABCDEFGHIJ", 10)       = 10
202668 <... read resumed>, "ABCDEFGHIJ", 20) = 10
202668 read(3 <unfinished ...>
202669 write(4, "ABCDEFGHIJ", 10)       = 10
202668 <... read resumed>, "ABCDEFGHIJ", 10) = 10
202668 read(3, "", 10)                  = 0
202668 write(1, "plain read(30)  -> 10 bytes\nread"..., 88) = 88
```

출력에서 볼 것: 첫 `read(3, …, 30)`은 10을 돌려줬다. read_full은 내부에서 `read(…, 20) = 10`, `read(…, 10) = 10`으로 두 번 불러서 20을 채웠다. 마지막 `read = 0`은 자식이 끝나 쓰기 쪽이 모두 닫혔다는 EOF 신호다. 이 `read_full`/`write_full` 두 함수는 센서 프레임(예: 고정 길이 32바이트 패킷)을 다루는 모든 툴에 그대로 복사해 쓴다.

### 4.3 예제 ex5 — EAGAIN, non-blocking short write, EINTR

무엇을 확인하나: O_NONBLOCK이면 기다리는 대신 EAGAIN, 공간이 부족하면 short write, 시그널이 블록 중인 read를 깨우면 EINTR, SA_RESTART가 있으면 자동 재시작.

```c
/* ex5: O_NONBLOCK → EAGAIN, non-blocking short write, SA_RESTART 없는 EINTR */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void die(const char *m) { perror(m); exit(1); }
static volatile sig_atomic_t hits;
static void on_alarm(int s) { (void)s; hits++; }

int main(void)
{
    int p[2];
    char buf[100000];
    memset(buf, 'x', sizeof buf);

    if (pipe2(p, O_NONBLOCK) < 0) die("pipe2");
    ssize_t r = read(p[0], buf, 10);                  /* 빈 pipe */
    printf("nonblock read on empty pipe -> %zd, errno=%s\n", r, strerrorname_np(errno));
    printf("pipe capacity (F_GETPIPE_SZ) = %d\n", fcntl(p[1], F_GETPIPE_SZ));
    ssize_t w = write(p[1], buf, sizeof buf);         /* 100000 바이트 요청 */
    printf("nonblock write(100000) -> %zd (short write)\n", w);
    w = write(p[1], buf, 1);
    printf("write(1) on full pipe -> %zd, errno=%s\n", w, strerrorname_np(errno));
    close(p[0]); close(p[1]);

    /* 블로킹 pipe + SA_RESTART 없는 시그널 → EINTR */
    if (pipe(p) < 0) die("pipe");
    struct sigaction sa = { .sa_handler = on_alarm };  /* sa_flags = 0 */
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGALRM, &sa, NULL) < 0) die("sigaction");
    alarm(1);
    r = read(p[0], buf, 10);                          /* 아무도 안 씀 → 블록 */
    printf("blocking read interrupted -> %zd, errno=%s\n", r, strerrorname_np(errno));

    sa.sa_flags = SA_RESTART;                         /* 이번엔 자동 재시작 */
    if (sigaction(SIGALRM, &sa, NULL) < 0) die("sigaction");
    if (fork() == 0) { sleep(2); if (write(p[1], "late", 4) != 4) _exit(1); _exit(0); }
    alarm(1);                                         /* 1초에 시그널, 2초에 데이터 */
    r = read(p[0], buf, 10);
    printf("with SA_RESTART -> %zd bytes (handler ran %d times)\n", r, (int)hits);
    return 0;
}
```

```text
nonblock read on empty pipe -> -1, errno=EAGAIN
pipe capacity (F_GETPIPE_SZ) = 65536
nonblock write(100000) -> 65536 (short write)
write(1) on full pipe -> -1, errno=EAGAIN
blocking read interrupted -> -1, errno=EINTR
with SA_RESTART -> 4 bytes (handler ran 2 times)
```

출력에서 볼 것:

- 빈 non-blocking pipe를 읽으면 즉시 `EAGAIN` (`EWOULDBLOCK`과 같은 값).
- pipe 용량은 기본 65536(64 KiB, `F_SETPIPE_SZ`로 `/proc/sys/fs/pipe-max-size` = 1 MiB까지 늘릴 수 있다). 100000바이트를 요청하면 **65536만 쓰고 반환**한다. 반환값을 무시하면 34464바이트가 조용히 사라진다.
- `sa_flags = 0`으로 등록한 핸들러가 블록 중인 read를 깨우면 `EINTR`. `SA_RESTART`를 주면 커널이 read를 다시 걸어 주므로 1초에 시그널이 와도(핸들러 실행 횟수 2: 앞의 것 1번 + 이번 1번) 2초에 들어온 4바이트를 정상으로 받는다.
- 주의: SA_RESTART가 있어도 재시작되지 않는 syscall이 있다(`poll`, `epoll_wait`, `nanosleep`, 타임아웃이 걸린 소켓 연산 등, man 7 signal 참고). 그러니 **EINTR 처리는 항상 넣는다**.

### 4.4 커널 쪽에서 보면

드라이버의 `.read`는 보통 이렇게 생겼다 (자세한 것은 F3, G4):

```c
/* 드라이버 쪽 의사 코드 */
if (fifo_empty(dev)) {
    if (filp->f_flags & O_NONBLOCK)
        return -EAGAIN;                       /* ex5의 EAGAIN이 여기서 */
    if (wait_event_interruptible(dev->wq, !fifo_empty(dev)))
        return -ERESTARTSYS;                  /* 시그널 → EINTR 또는 자동 재시작 */
}
n = min(count, fifo_len(dev));                /* 있는 만큼만 → short read */
if (copy_to_user(ubuf, dev->buf, n))
    return -EFAULT;
return n;
```

`-ERESTARTSYS`를 돌려주면 커널의 시그널 처리 코드가 SA_RESTART 여부를 보고 "syscall을 다시 걸지, 유저에게 EINTR을 줄지"를 정한다. 즉 ex5에서 본 두 동작은 드라이버가 이 한 줄로 만들어 내는 것이다.

---

## 5. lseek, pread/pwrite, 그리고 hole

### 5.1 정의

- `lseek(fd, off, whence)`: OFD의 offset만 바꾼다. I/O는 하지 않는다. `SEEK_SET`(처음 기준), `SEEK_CUR`(현재 기준), `SEEK_END`(끝 기준). 현재 위치를 알고 싶으면 `lseek(fd, 0, SEEK_CUR)`.
- `pread(fd, buf, n, off)` / `pwrite(…)`: 지정한 offset에서 읽고 쓰되 **OFD의 offset을 건드리지 않는다**. 원자적인 "seek + read"다. 여러 스레드가 같은 fd를 공유하면서 서로 다른 위치를 읽을 때 필수다 (lseek + read를 따로 하면 사이에 다른 스레드가 offset을 옮긴다).
- pipe, 소켓, tty는 seek이 안 된다 → `lseek`은 `ESPIPE`.
- 파일 끝 너머로 seek 후 쓰면 그 사이가 **hole**이 된다. 읽으면 0이 나오지만 디스크 블록은 할당되지 않는다(sparse file).

### 5.2 예제 ex6 — 가짜 레지스터 맵을 pread/pwrite로

무엇을 확인하나: pread/pwrite는 offset을 안 움직인다, 끝 너머 쓰기는 hole을 만든다.

```c
/* ex6: pread/pwrite는 offset을 안 움직인다 + lseek로 끝 너머 쓰면 hole */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void die(const char *m) { perror(m); exit(1); }

int main(void)
{
    int fd = open("/tmp/A3/reg.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) die("open");
    /* 가짜 "레지스터 맵": 4바이트 레지스터 16개 */
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t v = 0xA0000000u | i;
        if (write(fd, &v, 4) != 4) die("write");
    }
    printf("after sequential writes: pos=%ld\n", (long)lseek(fd, 0, SEEK_CUR));

    uint32_t v;
    if (pread(fd, &v, 4, 5 * 4) != 4) die("pread");          /* reg[5] */
    printf("pread reg[5]=0x%08x, pos still=%ld\n", v, (long)lseek(fd, 0, SEEK_CUR));
    v = 0xDEADBEEF;
    if (pwrite(fd, &v, 4, 2 * 4) != 4) die("pwrite");        /* reg[2] */
    if (pread(fd, &v, 4, 2 * 4) != 4) die("pread");
    printf("pwrite reg[2] -> read back 0x%08x\n", v);

    printf("lseek(0, SEEK_END) = file size %ld\n", (long)lseek(fd, 0, SEEK_END));
    if (lseek(fd, 1 << 20, SEEK_SET) < 0) die("lseek");       /* 1 MiB 지점으로 점프 */
    if (write(fd, "Z", 1) != 1) die("write Z");
    struct stat st;
    if (fstat(fd, &st) < 0) die("fstat");
    printf("st_size=%lld  allocated=%lld bytes (st_blocks*512)\n",
           (long long)st.st_size, (long long)st.st_blocks * 512);
    printf("SEEK_DATA from 5000 -> %ld, SEEK_HOLE from 0 -> %ld\n",
           (long)lseek(fd, 5000, SEEK_DATA), (long)lseek(fd, 0, SEEK_HOLE));
    return 0;
}
```

```text
after sequential writes: pos=64
pread reg[5]=0xa0000005, pos still=64
pwrite reg[2] -> read back 0xdeadbeef
lseek(0, SEEK_END) = file size 64
st_size=1048577  allocated=8192 bytes (st_blocks*512)
SEEK_DATA from 5000 -> 1048576, SEEK_HOLE from 0 -> 4096
8 -rw-r--r-- 1 donh donh 1048577 Oct  5 22:43 reg.bin
```

출력에서 볼 것:

- `pread`로 reg[5]를 읽어도 pos는 64 그대로다.
- 1 MiB 지점에 1바이트를 쓰면 `st_size`는 1048577이지만 실제 할당은 8192바이트(첫 블록 + 마지막 블록)뿐이다. `ls -ls`의 맨 앞 숫자(할당 KiB)도 8이다. `SEEK_DATA`/`SEEK_HOLE`로 데이터가 어디 있는지 찾을 수 있다.
- 실무 연결: 많은 장치 노드가 "offset = 레지스터 주소" 모델을 쓴다. 예를 들어 I2C EEPROM 드라이버(at24)는 sysfs의 `eeprom` 바이너리 속성 파일을 노출하고, `pread(fd, buf, 16, 0x40)`이 EEPROM 0x40번지부터 16바이트를 읽는다. NVMEM, `/dev/port`, `/proc/<pid>/mem`도 같은 모델이다.

---

## 6. Blocking vs non-blocking — 장치와 pipe에서

### 6.1 직관

bare-metal에서 UART 수신을 기다리는 방법은 두 가지였다: 플래그를 polling 하며 **busy-wait**하거나, 인터럽트가 올 때까지 태스크를 **block**시키거나(RTOS 세마포어). Linux의 blocking read는 후자다. 데이터가 없으면 커널이 프로세스를 wait queue에 넣고 재운다(CPU 0%). 데이터가 오면 드라이버의 인터럽트 핸들러가 `wake_up()`으로 깨운다.

non-blocking(`O_NONBLOCK`)은 "기다릴 상황이면 즉시 EAGAIN으로 돌아와라"다. 혼자서는 쓸모가 적고, **poll/epoll과 짝을 이뤄** "여러 fd 중 준비된 것만 처리"하는 이벤트 루프를 만든다(A4).

### 6.2 어떤 fd가 블록하나

| fd 종류 | read가 블록하는 조건 | write가 블록하는 조건 | O_NONBLOCK 의미 |
|---|---|---|---|
| 정규 파일 (ext4 등) | 사실상 안 함 (디스크 I/O 대기는 "블록"으로 치지 않음) | 안 함 (page cache에 씀) | 효과 없음 |
| pipe / FIFO | 비었고 writer가 살아 있을 때 | 가득 찼을 때 (64 KiB) | EAGAIN |
| 소켓 | 수신 버퍼가 비었을 때 | 송신 버퍼가 가득 찼을 때 | EAGAIN |
| tty / UART (`/dev/ttyS0`) | termios의 VMIN/VTIME 조건이 안 찼을 때 | 출력 버퍼가 찼을 때 | EAGAIN |
| char 장치 (센서, `/dev/input/event0`) | 드라이버 구현 나름 (보통 새 데이터 없을 때) | 드라이버 나름 | 드라이버가 `f_flags`를 확인해서 구현 |
| `/dev/i2c-N`, spidev | 블록하지 않는다. 트랜잭션이 끝날 때까지 동기적으로 기다릴 뿐 | 같음 | 사실상 무시 |
| sysfs 속성 | 블록 안 함 (값을 즉시 만들어 줌) | 블록 안 함 | 무시 |

정규 파일 read가 디스크를 기다리는 동안 프로세스는 `D` 상태(uninterruptible sleep)가 되지만, POSIX 의미로는 "블록"이 아니라서 O_NONBLOCK도, poll도 효과가 없다. 정규 파일에 epoll을 걸면 `EPERM`이 난다. 진짜 비동기 디스크 I/O가 필요하면 io_uring을 쓴다.

### 6.3 흔한 함정

- non-blocking fd에 `read_full` 같은 루프를 그대로 돌리면 EAGAIN에서 에러로 빠지거나, EAGAIN을 continue로 처리하면 **busy loop**(CPU 100%)가 된다. non-blocking이면 반드시 poll로 기다린다.
- 장치 드라이버가 O_NONBLOCK을 무시하도록 짜여 있으면 유저가 아무리 플래그를 켜도 블록한다. 테스트 툴을 짤 때 드라이버가 실제로 무엇을 지원하는지 확인한다(G4, G6).

---

## 7. 버퍼링 계층과 내구성 — fflush, fsync, fdatasync

### 7.1 직관 — 데이터가 머무는 곳은 여섯 군데

Don이 SSD 펌웨어에서 봤던 "호스트가 보낸 데이터가 DRAM write buffer에 있다가 NAND program으로 내려가는" 구조가 Linux 쪽에는 몇 단계 더 붙어 있다.

```svg
<svg viewBox="0 0 660 412" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <rect x="120" y="20" width="320" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="280" y="47" font-size="13" text-anchor="middle">앱 메모리 (char buf[])</text>
<line x1="280" y1="64" x2="280" y2="81" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/> <rect x="120" y="82" width="320" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="280" y="109" font-size="13" text-anchor="middle">stdio 버퍼 (FILE*, 유저 공간)</text>
<line x1="280" y1="126" x2="280" y2="143" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/> <rect x="120" y="144" width="320" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/> <text x="280" y="171" font-size="13" text-anchor="middle">page cache (커널 RAM, dirty page)</text>
<line x1="280" y1="188" x2="280" y2="205" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/> <rect x="120" y="206" width="320" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/> <text x="280" y="233" font-size="13" text-anchor="middle">블록 계층 · 드라이버 큐 (bio/request)</text>
<line x1="280" y1="250" x2="280" y2="267" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/> <rect x="120" y="268" width="320" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/> <text x="280" y="295" font-size="13" text-anchor="middle">디바이스 쓰기 캐시 (eMMC/SSD DRAM)</text>
<line x1="280" y1="312" x2="280" y2="329" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/> <rect x="120" y="330" width="320" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/> <text x="280" y="357" font-size="13" text-anchor="middle">NAND / 비휘발 매체</text>
<text x="455" y="77" font-size="12" text-anchor="start">fprintf / fwrite</text> <text x="455" y="139" font-size="12" text-anchor="start">fflush() → write()</text> <text x="455" y="201" font-size="12" text-anchor="start">fsync / fdatasync</text>
<text x="455" y="217" font-size="12" text-anchor="start">또는 writeback (기본 30초)</text> <text x="455" y="263" font-size="12" text-anchor="start">블록 I/O 완료</text> <text x="455" y="325" font-size="12" text-anchor="start">FLUSH CACHE / FUA</text>
<text x="60" y="82" font-size="13" text-anchor="middle">유저</text> <text x="60" y="100" font-size="13" text-anchor="middle">공간</text> <text x="60" y="206" font-size="13" text-anchor="middle">커널</text>
<text x="60" y="330" font-size="13" text-anchor="middle">저장</text> <text x="60" y="348" font-size="13" text-anchor="middle">장치</text> <text x="330" y="398" font-size="12" text-anchor="middle">NAND에 닿기 전의 데이터는 전원이 나가면 사라질 수 있다 · O_DIRECT는 page cache를 건너뛴다</text>
</svg>
```
그림 4 — `fprintf` 한 줄이 NAND 셀에 도달하기까지. 각 화살표를 넘기는 함수가 다르다.

| 함수 | 넘기는 경계 | 반환 시점의 보장 |
|---|---|---|
| `fflush(FILE *)` | stdio 버퍼 → 커널 (`write()` 호출) | 다른 프로세스가 read하면 보인다. 전원 차단에는 무방비 |
| `write()` | 유저 → page cache | 위와 같음. 커널 크래시·전원 차단 시 사라질 수 있음 |
| `fdatasync(fd)` | page cache의 데이터 + **데이터를 다시 읽는 데 필요한 메타데이터(크기 등)** → 장치, 그리고 장치 캐시 flush | 이 파일의 데이터가 비휘발 매체에 있다 |
| `fsync(fd)` | fdatasync + **모든 메타데이터(mtime 등)** | 파일 전체 상태가 매체에 있다 |
| `sync()` / `syncfs()` | 시스템 전체 / 그 파일시스템 전체 | 전부 (느림) |
| `O_SYNC` / `O_DSYNC` | 매 `write()`마다 fsync / fdatasync를 한 효과 | 매 write 반환 = 매체 도달 |

중요한 두 가지:

- **`fclose()`나 `close()`는 fsync를 하지 않는다.** close가 성공해도 데이터는 page cache에 있을 수 있다.
- fsync가 없어도 커널은 결국 내려 쓴다(writeback). 이 VM에서 `vm.dirty_expire_centisecs` = 3000(30초 지난 dirty page를 내림), `dirty_writeback_centisecs` = 500(5초마다 flusher 스레드가 깸)이었다. 즉 **전원이 나가면 최대 수십 초 분량을 잃을 수 있다.**

### 7.2 예제 ex9 — strace로 계층 경계 보기

무엇을 확인하나: `fprintf`는 syscall을 만들지 않고, `fflush`가 `write`를, `fsync`가 `fsync`를 만든다.

```c
/* ex9: fprintf → (stdio 버퍼) → fflush → write() → (page cache) → fsync → 디스크 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void die(const char *m) { perror(m); exit(1); }

int main(void)
{
    FILE *f = fopen("/var/tmp/A3/cal.txt", "w");
    if (!f) die("fopen");
    for (int i = 0; i < 3; i++)
        fprintf(f, "imu_bias[%d]=%d\n", i, 100 + i);   /* 아직 syscall 없음 */
    fprintf(stderr, "-- before fflush\n");
    if (fflush(f) != 0) die("fflush");                  /* 여기서 write() 1번 */
    fprintf(stderr, "-- before fsync\n");
    if (fsync(fileno(f)) != 0) die("fsync");            /* 여기서 디스크까지 */
    if (fclose(f) != 0) die("fclose");
    return 0;
}
```

```sh
strace -o ex9.st -e trace=openat,write,fsync,close ./ex9 2>/dev/null
```

```text
openat(AT_FDCWD, "/var/tmp/A3/cal.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
write(2, "-- before fflush\n", 17)      = 17
write(3, "imu_bias[0]=100\nimu_bias[1]=101\n"..., 48) = 48
write(2, "-- before fsync\n", 16)       = 16
fsync(3)                                = 0
close(3)                                = 0
+++ exited with 0 +++
```

(동적 링커가 라이브러리를 여닫는 앞부분 줄은 생략했다.) 출력에서 볼 것: `fprintf`를 세 번 했지만 `write(3, …)`는 **fflush 시점에 48바이트 한 번**이다. stderr(fd 2)는 버퍼링이 없어서 메시지마다 바로 write가 나간다. stdout은 터미널이면 줄 단위(line buffered), pipe·파일이면 블록 단위(full buffered)라서 "printf가 파이프로 보내면 늦게 나온다"는 현상이 생긴다. 실시간 로그는 `setvbuf(stdout, NULL, _IOLBF, 0)` 또는 stderr를 쓴다.

### 7.3 예제 ex7 — 버퍼 크기가 처리량을 결정한다

무엇을 확인하나: syscall 한 번의 고정 비용 때문에 작은 write를 많이 하면 느리다. stdio가 이걸 어떻게 숨기는지.

```c
/* ex7: write() 버퍼 크기별 처리량 + stdio(fputc)가 어떻게 이를 숨기는가 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void die(const char *m) { perror(m); exit(1); }
#define TOTAL (8u << 20)                       /* 8 MiB씩 쓴다 */
#define OUT "/tmp/A3/tput.bin"

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static double run_write(size_t bs)
{
    static char buf[1 << 20];
    int fd = open(OUT, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) die("open");
    double t0 = now();
    for (size_t done = 0; done < TOTAL; done += bs)
        if (write(fd, buf, bs) != (ssize_t)bs) die("write");
    double dt = now() - t0;
    close(fd);
    return TOTAL / dt / 1e6;                   /* MB/s */
}

static double run_fputc(void)
{
    FILE *f = fopen(OUT, "w");
    if (!f) die("fopen");
    double t0 = now();
    for (size_t i = 0; i < TOTAL; i++) fputc('x', f);
    if (fclose(f) != 0) die("fclose");
    return TOTAL / (now() - t0) / 1e6;
}

int main(void)
{
    size_t sizes[] = { 1, 16, 256, 4096, 65536, 1 << 20 };
    printf("%8s %10s %12s\n", "bs", "syscalls", "MB/s(best3)");
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        double best = 0;
        for (int k = 0; k < 3; k++) { double v = run_write(sizes[i]); if (v > best) best = v; }
        printf("%8zu %10zu %12.1f\n", sizes[i], TOTAL / sizes[i], best);
    }
    double best = 0;
    for (int k = 0; k < 3; k++) { double v = run_fputc(); if (v > best) best = v; }
    printf("%8s %10s %12.1f\n", "fputc", "(stdio)", best);
    return 0;
}
```

```text
      bs   syscalls  MB/s(best3)
       1    8388608          4.3
      16     524288         68.5
     256      32768       1009.0
    4096       2048       6917.3
   65536        128       7977.1
 1048576          8       8593.8
   fputc    (stdio)        449.5
```

```svg
<svg viewBox="0 0 640 352" xmlns="http://www.w3.org/2000/svg">
<text x="320.0" y="22" text-anchor="middle" font-size="14">write() 처리량 vs 버퍼 크기 (8 MiB, tmpfs, best of 3)</text> <text x="122" y="67.0" text-anchor="end" font-size="12">write bs=1</text> <rect x="130" y="50" width="74.4" height="26" fill="#d0564a" fill-opacity="0.75"/>
<text x="210.4" y="67.0" text-anchor="start" font-size="12">4.3 MB/s</text> <text x="122" y="103.0" text-anchor="end" font-size="12">write bs=16</text> <rect x="130" y="86" width="215.7" height="26" fill="#d0564a" fill-opacity="0.75"/>
<text x="351.7" y="103.0" text-anchor="start" font-size="12">68.5 MB/s</text> <text x="122" y="139.0" text-anchor="end" font-size="12">write bs=256</text> <rect x="130" y="122" width="353.0" height="26" fill="#e08a3c" fill-opacity="0.75"/>
<text x="489.0" y="139.0" text-anchor="start" font-size="12">1009 MB/s</text> <text x="122" y="175.0" text-anchor="end" font-size="12">write bs=4K</text> <rect x="130" y="158" width="451.2" height="26" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="575.2" y="175.0" text-anchor="end" font-size="12">6917.3 MB/s</text> <text x="122" y="211.0" text-anchor="end" font-size="12">write bs=64K</text> <rect x="130" y="194" width="458.5" height="26" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="582.5" y="211.0" text-anchor="end" font-size="12">7977.1 MB/s</text> <text x="122" y="247.0" text-anchor="end" font-size="12">write bs=1M</text> <rect x="130" y="230" width="462.3" height="26" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="586.3" y="247.0" text-anchor="end" font-size="12">8593.8 MB/s</text> <text x="122" y="283.0" text-anchor="end" font-size="12">fputc (stdio)</text> <rect x="130" y="266" width="311.7" height="26" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="447.7" y="283.0" text-anchor="start" font-size="12">449.5 MB/s</text> <line x1="130" y1="302" x2="600" y2="302" stroke="currentColor"/> <line x1="130" y1="44" x2="130" y2="302" stroke="currentColor"/>
<line x1="130.0" y1="302" x2="130.0" y2="307" stroke="currentColor"/> <text x="130.0" y="320" text-anchor="middle" font-size="12">1</text> <line x1="247.5" y1="302" x2="247.5" y2="307" stroke="currentColor"/>
<text x="247.5" y="320" text-anchor="middle" font-size="12">10</text> <line x1="365.0" y1="302" x2="365.0" y2="307" stroke="currentColor"/> <text x="365.0" y="320" text-anchor="middle" font-size="12">100</text>
<line x1="482.5" y1="302" x2="482.5" y2="307" stroke="currentColor"/> <text x="482.5" y="320" text-anchor="middle" font-size="12">1000</text> <line x1="600.0" y1="302" x2="600.0" y2="307" stroke="currentColor"/>
<text x="600.0" y="320" text-anchor="middle" font-size="12">10000</text> <text x="365.0" y="340" text-anchor="middle" font-size="12">MB/s (로그 스케일)</text>
</svg>
```
그림 5 — 위 측정값(tmpfs, 즉 page cache만 거치는 쓰기). 1바이트 write는 4.3 MB/s, 4 KiB 이상은 7~9 GB/s로 2000배 가까이 차이 난다.

출력에서 볼 것:

- bs=1에서 838만 번 syscall에 약 2초가 걸렸다. 8 MiB ÷ 4.3 MB/s ≈ 1.95 s ÷ 8388608 ≈ **syscall 한 번에 약 230 ns**. 이것이 유저↔커널 경계를 넘는 고정비다(A1의 syscall 비용과 같은 이야기).
- 4 KiB부터는 고정비가 묻히고 memcpy 속도(page cache로의 복사)가 한계가 된다. 그 이상은 이득이 작다.
- `fputc`로 1바이트씩 써도 449.5 MB/s다. stdio가 4 KiB 정도 모아서 write 한 번으로 보내기 때문이다. 대신 바이트마다 함수 호출 비용(멀티스레드 프로그램이면 FILE 락 비용까지)이 남아 큰 버퍼 write보다는 느리다.
- 수치는 이 VM(Apple Silicon 위 aarch64 게스트)의 값이다. 보드에서는 절대값이 다르지만 모양은 같다.

### 7.4 예제 ex8 — 내구성의 가격

무엇을 확인하나: 4 KiB 레코드 하나를 "확실히" 쓰는 방식별 비용. tmpfs가 아닌 ext4(`/var/tmp`) 위에서 측정한다.

```c
/* ex8: 4 KiB 레코드 하나를 "내구성 있게" 쓰는 비용 — 방식별 평균 지연 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void die(const char *m) { perror(m); exit(1); }
#define N 300
#define F "/var/tmp/A3/journal.bin"           /* tmpfs가 아닌 ext4 위 */

static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

enum mode { PLAIN, FDATASYNC, FSYNC, OSYNC, ODSYNC };
static const char *name[] = { "write only", "write+fdatasync", "write+fsync",
                              "O_SYNC", "O_DSYNC" };

static double bench(enum mode m)
{
    int extra = m == OSYNC ? O_SYNC : m == ODSYNC ? O_DSYNC : 0;
    int fd = open(F, O_WRONLY | O_CREAT | O_TRUNC | extra, 0644);
    if (fd < 0) die("open");
    char rec[4096];
    memset(rec, 'r', sizeof rec);
    /* 파일을 미리 키워 둠: 덮어쓰기는 메타데이터(크기) 변경이 없다 */
    for (int i = 0; i < N; i++) if (write(fd, rec, sizeof rec) != sizeof rec) die("pre");
    if (fsync(fd) < 0) die("fsync pre");
    if (lseek(fd, 0, SEEK_SET) < 0) die("lseek");
    double t0 = now_us();
    for (int i = 0; i < N; i++) {
        if (write(fd, rec, sizeof rec) != sizeof rec) die("write");
        if (m == FDATASYNC && fdatasync(fd) < 0) die("fdatasync");
        if (m == FSYNC && fsync(fd) < 0) die("fsync");
    }
    double avg = (now_us() - t0) / N;
    close(fd);
    return avg;
}

int main(void)
{
    for (int m = PLAIN; m <= ODSYNC; m++)
        printf("%-16s avg %8.1f us per 4 KiB record\n", name[m], bench(m));
    return 0;
}
```

```text
$ ./ex8 && ./ex8
write only       avg      0.6 us per 4 KiB record
write+fdatasync  avg     74.0 us per 4 KiB record
write+fsync      avg    108.8 us per 4 KiB record
O_SYNC           avg    135.0 us per 4 KiB record
O_DSYNC          avg     80.6 us per 4 KiB record
write only       avg      0.7 us per 4 KiB record
write+fdatasync  avg     83.4 us per 4 KiB record
write+fsync      avg    111.5 us per 4 KiB record
O_SYNC           avg    135.8 us per 4 KiB record
O_DSYNC          avg     59.5 us per 4 KiB record
$ cat /sys/block/vda/queue/write_cache
write back
```

```svg
<svg viewBox="0 0 640 280" xmlns="http://www.w3.org/2000/svg">
<text x="320.0" y="22" text-anchor="middle" font-size="14">4 KiB 레코드 1개당 평균 지연 (ext4 on virtio, 300회)</text> <text x="142" y="67.0" text-anchor="end" font-size="12">write only</text> <rect x="150" y="50" width="1.8" height="26" fill="#888" fill-opacity="0.75"/>
<text x="157.8" y="67.0" font-size="12">0.6 us</text> <text x="142" y="103.0" text-anchor="end" font-size="12">write + fdatasync</text> <rect x="150" y="86" width="222.0" height="26" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="378.0" y="103.0" font-size="12">74 us</text> <text x="142" y="139.0" text-anchor="end" font-size="12">O_DSYNC</text> <rect x="150" y="122" width="241.8" height="26" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="397.8" y="139.0" font-size="12">80.6 us</text> <text x="142" y="175.0" text-anchor="end" font-size="12">write + fsync</text> <rect x="150" y="158" width="326.4" height="26" fill="#e08a3c" fill-opacity="0.75"/>
<text x="482.4" y="175.0" font-size="12">108.8 us</text> <text x="142" y="211.0" text-anchor="end" font-size="12">O_SYNC</text> <rect x="150" y="194" width="405.0" height="26" fill="#e08a3c" fill-opacity="0.75"/>
<text x="561.0" y="211.0" font-size="12">135 us</text> <line x1="150" y1="230" x2="600" y2="230" stroke="currentColor"/> <line x1="150" y1="44" x2="150" y2="230" stroke="currentColor"/>
<line x1="150.0" y1="230" x2="150.0" y2="235" stroke="currentColor"/> <text x="150.0" y="248" text-anchor="middle" font-size="12">0</text> <line x1="225.0" y1="230" x2="225.0" y2="235" stroke="currentColor"/>
<text x="225.0" y="248" text-anchor="middle" font-size="12">25</text> <line x1="300.0" y1="230" x2="300.0" y2="235" stroke="currentColor"/> <text x="300.0" y="248" text-anchor="middle" font-size="12">50</text>
<line x1="375.0" y1="230" x2="375.0" y2="235" stroke="currentColor"/> <text x="375.0" y="248" text-anchor="middle" font-size="12">75</text> <line x1="450.0" y1="230" x2="450.0" y2="235" stroke="currentColor"/>
<text x="450.0" y="248" text-anchor="middle" font-size="12">100</text> <line x1="525.0" y1="230" x2="525.0" y2="235" stroke="currentColor"/> <text x="525.0" y="248" text-anchor="middle" font-size="12">125</text>
<line x1="600.0" y1="230" x2="600.0" y2="235" stroke="currentColor"/> <text x="600.0" y="248" text-anchor="middle" font-size="12">150</text> <text x="375.0" y="268" text-anchor="middle" font-size="12">마이크로초 (us)</text>
</svg>
```
그림 6 — 첫 번째 실행 값. write만 하면 0.6 us, 내구성을 요구하면 100배 이상 비싸다.

출력에서 볼 것:

- `write only`는 page cache에 복사만 하므로 1 us도 안 걸린다. 그러나 이 상태로 전원이 나가면 데이터가 없다.
- `fdatasync`(74~83 us)가 `fsync`(109~112 us)보다 싸다. 덮어쓰기라 파일 크기는 그대로지만 **mtime이 바뀌므로**, fsync는 inode 갱신까지 ext4 저널에 커밋해야 하고 fdatasync는 "데이터를 다시 읽는 데 필요 없는 메타데이터"라서 건너뛴다. 로그·저널처럼 자주 sync 하는 파일은 미리 크기를 잡아 두고(`fallocate`) fdatasync를 쓰는 게 정석이다.
- `O_DSYNC`는 두 번 실행에서 80.6 us, 59.5 us로 흔들렸다. 측정값은 실행마다 다르다.
- 주의: 이 VM의 디스크는 virtio이고 그 뒤에 macOS 호스트 파일이 있다. 호스트가 flush를 어떻게 처리하는지에 따라 숫자가 달라진다. 실제 eMMC/SD 카드에서는 cache flush가 실제 NAND program을 기다려야 해서 보통 이보다 훨씬 느리다(ms 단위도 흔하다). **보드에서는 반드시 직접 재 보라.**

### 7.5 Don의 SSD 경험과 연결 — 장치 쪽 캐시

Linux가 fsync를 처리할 때 마지막에 하는 일은 장치에 **cache flush 명령**을 보내는 것이다 (`/sys/block/vda/queue/write_cache`가 `write back`이면 커널이 flush를 보낸다). NVMe라면 Flush 명령, SATA는 FLUSH CACHE, eMMC는 EXT_CSD의 FLUSH_CACHE, SCSI/UFS는 SYNCHRONIZE CACHE다. 또 일부 쓰기에는 FUA(Force Unit Access, "이 쓰기는 캐시에 머물지 말고 매체까지") 비트를 단다. ext4 저널 커밋 블록이 대표적이다.

Don이 만든 엔터프라이즈 SSD는 PLP(power loss protection) 커패시터가 있어서 전원이 나가도 DRAM의 데이터를 NAND로 내릴 수 있었다. 그래서 Flush가 싸다. 그러나 **임베디드 보드의 eMMC/SD/저가 UFS에는 보통 PLP가 없다.** 이런 장치에서는

1. fsync 없이 쓴 데이터는 page cache에서 날아가고,
2. fsync를 했어도 장치가 flush를 제대로 구현하지 않으면 장치 DRAM에서 날아가며,
3. 최악의 경우 FTL 매핑 테이블이 깨져서 **방금 쓴 것과 무관한 오래된 데이터**까지 손상된다(Don이 FTL에서 매핑 테이블 저널링을 왜 했는지 떠올리면 된다).

그래서 필드 장비 설계에서는 "어떤 데이터를 언제 fsync 할 것인가"를 의식적으로 정하고(설정·캘리브레이션 값은 매번, 대용량 로그는 묶어서), 파일 갱신은 다음 절의 **write-temp → fsync → rename → fsync(dir)** 패턴을 쓴다. 깊은 이야기는 J5(전원 차단 안전성)에서 다룬다.

---

## 8. ioctl — 스트림으로 표현할 수 없는 제어

### 8.1 직관과 정의

read/write는 "바이트 흐름"이다. 그런데 장치에는 흐름이 아닌 명령이 많다: "SPI 모드를 3으로", "이 I2C 주소로 combined transaction", "터미널 창 크기를 알려줘", "블록 장치 용량은?". 이런 걸 위한 탈출구가 `ioctl`이다.

```c
int ioctl(int fd, unsigned long request, ...);   /* 셋째 인자는 보통 구조체 포인터 */
```

- `request`: 32비트 명령 번호. 드라이버가 정의하고 헤더(`<linux/…>`)로 공개한다.
- 셋째 인자: 명령마다 정해진 타입. 보통 포인터이고, 커널은 `copy_from_user`/`copy_to_user`로 그 메모리를 읽고 쓴다.
- 장치가 모르는 명령이면 `ENOTTY` ("Inappropriate ioctl for device"). 역사적 이유로 이 이름이다. 최근 드라이버는 `-ENOIOCTLCMD`를 돌려주고 VFS가 ENOTTY로 바꾼다.

### 8.2 번호 인코딩 — _IO, _IOR, _IOW, _IOWR

명령 번호를 아무렇게나 정하면 엉뚱한 장치에 보낸 명령이 우연히 다른 의미로 실행될 수 있다. 그래서 번호 안에 **방향, 인자 크기, 드라이버 고유 type, 순번**을 넣는 규칙을 쓴다.

```svg
<svg viewBox="0 0 660 230" xmlns="http://www.w3.org/2000/svg">
<text x="330" y="18" font-size="12" text-anchor="middle">ioctl 번호 32비트 (asm-generic: arm64 · x86 공통) — 예: BLKGETSIZE64 = 0x80081272</text> <rect x="30" y="40" width="37.5" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="48.75" y="65" font-size="12" text-anchor="middle">dir</text>
<rect x="67.5" y="40" width="262.5" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/> <text x="198.75" y="65" font-size="12" text-anchor="middle">size (14 bit)</text> <rect x="330" y="40" width="150" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="405.0" y="65" font-size="12" text-anchor="middle">type (8 bit)</text> <rect x="480" y="40" width="150" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/> <text x="555.0" y="65" font-size="12" text-anchor="middle">nr (8 bit)</text>
<text x="33" y="34" font-size="12" text-anchor="start">31</text> <text x="70.5" y="34" font-size="12" text-anchor="start">29</text> <text x="327" y="34" font-size="12" text-anchor="end">16</text>
<text x="333" y="34" font-size="12" text-anchor="start">15</text> <text x="477" y="34" font-size="12" text-anchor="end">8</text> <text x="483" y="34" font-size="12" text-anchor="start">7</text>
<text x="627" y="34" font-size="12" text-anchor="end">0</text> <rect x="30" y="88" width="37.5" height="34" fill="none" stroke="currentColor" stroke-dasharray="3,3"/> <text x="48.75" y="110" font-size="12" text-anchor="middle">10</text>
<rect x="67.5" y="88" width="262.5" height="34" fill="none" stroke="currentColor" stroke-dasharray="3,3"/> <text x="198.75" y="110" font-size="12" text-anchor="middle">00000000001000 = 8 bytes</text> <rect x="330" y="88" width="150" height="34" fill="none" stroke="currentColor" stroke-dasharray="3,3"/>
<text x="405.0" y="110" font-size="12" text-anchor="middle">0x12 (블록 장치)</text> <rect x="480" y="88" width="150" height="34" fill="none" stroke="currentColor" stroke-dasharray="3,3"/> <text x="555.0" y="110" font-size="12" text-anchor="middle">0x72 = 114</text>
<text x="30" y="150" font-size="12" text-anchor="start">dir: 0 = _IO (인자 없음) · 1 = _IOW (유저 → 커널) · 2 = _IOR (커널 → 유저) · 3 = _IOWR</text> <text x="30" y="172" font-size="12" text-anchor="start">BLKGETSIZE64 = _IOR(0x12, 114, size_t) → dir=2(R), size=8, type=0x12, nr=114</text> <text x="30" y="194" font-size="12" text-anchor="start">SPI_IOC_MESSAGE(n) = _IOW('k', 0, char[32·n]) → size 필드에 전송 개수가 들어간다</text>
<text x="30" y="216" font-size="12" text-anchor="start">TIOCGWINSZ 0x5413 · I2C_RDWR 0x0707 = 규칙 이전의 legacy 번호 (dir · size = 0)</text>
</svg>
```
그림 7 — 32비트 ioctl 번호의 비트 필드 (`include/uapi/asm-generic/ioctl.h`). PowerPC·MIPS·Alpha 등은 dir 3비트·size 13비트로 다르다.

```c
#define _IO(type, nr)              /* 인자 없음 */
#define _IOR(type, nr, argtype)    /* 커널 → 유저로 sizeof(argtype) 바이트 */
#define _IOW(type, nr, argtype)    /* 유저 → 커널 */
#define _IOWR(type, nr, argtype)   /* 양방향 */
/* 분해 매크로: _IOC_DIR(req), _IOC_TYPE(req), _IOC_NR(req), _IOC_SIZE(req) */
```

방향의 R/W는 **유저 관점**이다. `_IOR`은 "유저가 읽는다" = 커널이 유저 버퍼에 쓴다. type 값(매직 넘버)은 `Documentation/userspace-api/ioctl/ioctl-number.rst`에 드라이버별로 등록되어 있다.

### 8.3 예제 ex10 — 실제 번호 분해

무엇을 확인하나: 커널 헤더에 정의된 ioctl 번호를 매크로로 분해하면 그림 7처럼 나오는지.

```c
/* ex10: ioctl request 번호를 비트 필드로 분해해 보기 */
#include <linux/fs.h>          /* BLKGETSIZE64, FS_IOC_GETFLAGS */
#include <linux/i2c-dev.h>     /* I2C_RDWR, I2C_SLAVE */
#include <linux/spi/spidev.h>  /* SPI_IOC_MESSAGE, SPI_IOC_RD_MODE */
#include <linux/watchdog.h>    /* WDIOC_KEEPALIVE */
#include <stdio.h>
#include <sys/ioctl.h>         /* _IOC_DIR 등, TIOCGWINSZ, FIONREAD */

static void show(const char *name, unsigned long req)
{
    static const char *dirs[] = { "NONE", "W", "R", "RW" };   /* _IOC_WRITE=1, READ=2 */
    printf("%-18s 0x%08lx  dir=%-4s size=%-3lu type=0x%02lx('%c') nr=%lu\n",
           name, req, dirs[_IOC_DIR(req)], (unsigned long)_IOC_SIZE(req),
           (unsigned long)_IOC_TYPE(req),
           _IOC_TYPE(req) >= 0x20 && _IOC_TYPE(req) < 0x7f ? (int)_IOC_TYPE(req) : '.',
           (unsigned long)_IOC_NR(req));
}

int main(void)
{
    printf("_IOC_NRBITS=%d TYPEBITS=%d SIZEBITS=%d DIRBITS=%d\n",
           _IOC_NRBITS, _IOC_TYPEBITS, _IOC_SIZEBITS, _IOC_DIRBITS);
    show("BLKGETSIZE64", BLKGETSIZE64);
    show("FS_IOC_GETFLAGS", FS_IOC_GETFLAGS);
    show("SPI_IOC_RD_MODE", SPI_IOC_RD_MODE);
    show("SPI_IOC_MESSAGE(1)", SPI_IOC_MESSAGE(1));
    show("SPI_IOC_MESSAGE(2)", SPI_IOC_MESSAGE(2));
    show("WDIOC_KEEPALIVE", WDIOC_KEEPALIVE);
    printf("-- legacy (인코딩 규칙 이전에 정해진 번호) --\n");
    show("TIOCGWINSZ", TIOCGWINSZ);
    show("FIONREAD", FIONREAD);
    show("I2C_RDWR", I2C_RDWR);
    show("I2C_SLAVE", I2C_SLAVE);
    return 0;
}
```

```text
_IOC_NRBITS=8 TYPEBITS=8 SIZEBITS=14 DIRBITS=2
BLKGETSIZE64       0x80081272  dir=R    size=8   type=0x12('.') nr=114
FS_IOC_GETFLAGS    0x80086601  dir=R    size=8   type=0x66('f') nr=1
SPI_IOC_RD_MODE    0x80016b01  dir=R    size=1   type=0x6b('k') nr=1
SPI_IOC_MESSAGE(1) 0x40206b00  dir=W    size=32  type=0x6b('k') nr=0
SPI_IOC_MESSAGE(2) 0x40406b00  dir=W    size=64  type=0x6b('k') nr=0
WDIOC_KEEPALIVE    0x80045705  dir=R    size=4   type=0x57('W') nr=5
-- legacy (인코딩 규칙 이전에 정해진 번호) --
TIOCGWINSZ         0x00005413  dir=NONE size=0   type=0x54('T') nr=19
FIONREAD           0x0000541b  dir=NONE size=0   type=0x54('T') nr=27
I2C_RDWR           0x00000707  dir=NONE size=0   type=0x07('.') nr=7
I2C_SLAVE          0x00000703  dir=NONE size=0   type=0x07('.') nr=3
```

출력에서 볼 것:

- `BLKGETSIZE64` = `0x80081272`: dir=R(2), size=8(`size_t`), type=0x12(블록 계층), nr=114.
- `SPI_IOC_MESSAGE(n)`은 size 필드에 `n × sizeof(struct spi_ioc_transfer)`(32바이트)를 넣는다. 1개면 32, 2개면 64. 드라이버는 `_IOC_SIZE(cmd) / 32`로 전송 개수를 알아낸다. 번호에 데이터 길이를 실은 영리한 예다.
- `FS_IOC_GETFLAGS`는 `_IOR('f', 1, long)`으로 정의되어 size=8이지만, 실제로 주고받는 값은 `int`다(헤더 정의가 역사적으로 잘못된 유명한 예, man ioctl_iflags). 번호의 size는 "정의할 때 쓴 타입"일 뿐 커널이 자동 검증하는 게 아니다.
- `TIOCGWINSZ`(0x5413), `FIONREAD`, `I2C_RDWR`(0x0707) 등은 인코딩 규칙이 생기기 전의 legacy 번호라 dir·size가 0이다. 이 값들은 아키텍처마다 다를 수 있으니 항상 헤더 상수를 쓴다.

### 8.4 예제 ex11 — 실제 ioctl 3개

무엇을 확인하나: pipe에 대기 중인 바이트 수(FIONREAD), 터미널 크기(TIOCGWINSZ), 블록 장치 용량(BLKGETSIZE64). 64 MiB 이미지 파일을 loop 장치로 붙여서 테스트한다.

```c
/* ex11: 실제 ioctl 3개 — FIONREAD(pipe), TIOCGWINSZ(tty), BLKGETSIZE64(block dev) */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void die(const char *m) { perror(m); exit(1); }

int main(int argc, char **argv)
{
    int p[2], n = -1;
    if (pipe(p) < 0) die("pipe");
    if (write(p[1], "hello sensor", 12) != 12) die("write");
    if (ioctl(p[0], FIONREAD, &n) < 0) die("FIONREAD");
    printf("FIONREAD on pipe: %d bytes waiting\n", n);

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0)
        printf("TIOCGWINSZ on stdout: errno=%s (%s)\n",
               strerrorname_np(errno), strerror(errno));
    else
        printf("TIOCGWINSZ on stdout: %u rows x %u cols\n", ws.ws_row, ws.ws_col);

    if (argc > 1) {                              /* 예: /dev/loop0 */
        int fd = open(argv[1], O_RDONLY);
        if (fd < 0) die(argv[1]);
        uint64_t bytes;
        if (ioctl(fd, BLKGETSIZE64, &bytes) < 0) die("BLKGETSIZE64");
        printf("BLKGETSIZE64 %s: %llu bytes (%llu MiB)\n", argv[1],
               (unsigned long long)bytes, (unsigned long long)(bytes >> 20));
        if (ioctl(p[0], BLKGETSIZE64, &bytes) < 0)   /* 엉뚱한 fd에 보내면? */
            printf("BLKGETSIZE64 on a pipe: errno=%s\n", strerrorname_np(errno));
        close(fd);
    }
    return 0;
}
```

```sh
truncate -s 64M disk.img
LOOP=$(sudo losetup -f --show disk.img) && echo "loop=$LOOP"
./ex11 | cat                                    # stdout = pipe
echo "--- under a pty (script) + sudo:"
sudo script -qc "stty rows 40 cols 120; ./ex11 $LOOP" /dev/null   # stdout = pty
sudo losetup -d $LOOP
```

```text
loop=/dev/loop0
FIONREAD on pipe: 12 bytes waiting
TIOCGWINSZ on stdout: errno=ENOTTY (Inappropriate ioctl for device)
--- under a pty (script) + sudo:
FIONREAD on pipe: 12 bytes waiting
TIOCGWINSZ on stdout: 40 rows x 120 cols
BLKGETSIZE64 /dev/loop0: 67108864 bytes (64 MiB)
BLKGETSIZE64 on a pipe: errno=ENOTTY
```

`strace -e trace=ioctl`은 번호를 이름으로 해독해서 보여준다:

```text
$ strace -e trace=ioctl ./ex11 2>&1 >/dev/null
ioctl(3, FIONREAD, [12])                = 0
ioctl(1, TCGETS2, 0xffffeb24a678)       = -1 ENOTTY (Inappropriate ioctl for device)
ioctl(1, TIOCGWINSZ, 0xffffeb24aad0)    = -1 ENOTTY (Inappropriate ioctl for device)
+++ exited with 0 +++
```

출력에서 볼 것:

- stdout이 pipe면 TIOCGWINSZ는 `ENOTTY`. `script`로 가짜 터미널(pty)을 주면 40×120이 나온다.
- loop 장치에 BLKGETSIZE64를 보내면 64 MiB. **같은 명령을 pipe에 보내면 ENOTTY** — 그 드라이버(pipe)가 모르는 명령이기 때문이다.
- strace의 `TCGETS2`는 내가 부른 것이 아니라 glibc가 stdout이 터미널인지(버퍼링 모드 결정) 확인하려고 부른 것이다. strace로 남의 프로그램을 볼 때 이런 "라이브러리가 부른 ioctl"이 많이 섞여 있다.

### 8.5 ioctl vs sysfs vs read/write — 언제 무엇을

| 인터페이스 | 잘 맞는 것 | 안 맞는 것 |
|---|---|---|
| read/write | 데이터 스트림, 샘플, 프레임 | 설정·명령 |
| ioctl | 원자적인 복합 명령(I2C combined, SPI 다중 transfer), 구조체 주고받기, 빠른 반복 호출 | 셸에서 손으로 다루기, 사람이 읽는 설정 |
| sysfs 속성 | 값 하나짜리 설정·상태(샘플링 주기, 온도, enable), 셸·스크립트 친화 | 고속 데이터, 여러 값을 원자적으로 바꾸기 |
| mmap | 큰 버퍼 zero-copy(카메라 프레임) | 작은 제어 (A5, G5) |

새 드라이버를 설계할 때 "ioctl을 새로 만들지 말고 가능하면 기존 서브시스템(IIO, V4L2 …)의 인터페이스를 쓰라"는 것이 커널 커뮤니티의 방향이다(I1, G5).

---

## 9. 장치 노드 — char/block, major/minor, mknod

### 9.1 정의

`/dev`의 파일들은 디스크에 데이터가 없는 **특수 파일**이다. inode에 들어 있는 건 두 가지뿐이다: 종류(문자 c / 블록 b)와 **장치 번호(major, minor)**.

- **major**: 어느 드라이버인가 (1 = mem 계열, 4 = tty, 7 = loop, 89 = i2c …)
- **minor**: 그 드라이버가 관리하는 몇 번째 장치인가 (i2c-0, i2c-1 …)
- **char 장치**: 바이트 스트림 + ioctl. 대부분의 주변장치(UART, I2C, SPI, 센서, GPIO chip, watchdog).
- **block 장치**: 고정 크기 블록 단위 랜덤 액세스, page cache와 블록 계층(I/O 스케줄러)을 거친다. eMMC, SD, NVMe, loop.

장치 노드 이름은 관례일 뿐이다. 커널은 **이름이 아니라 (type, major, minor)로** 드라이버를 찾는다. 지금은 devtmpfs와 udev가 드라이버 등록 시점에 노드를 자동으로 만든다(G9, L2).

### 9.2 예제 ex16 — 셸로 장치 번호 보기와 mknod

무엇을 확인하나: `ls -l`의 크기 자리에 major, minor가 나오고, 같은 번호로 노드를 직접 만들면 같은 드라이버가 응답하는지.

```sh
cd /tmp/A3
ls -l /dev/null /dev/zero /dev/vda /dev/vda1 /dev/ttyS0; echo "---"
grep -E "^ *(1|4|7|89|153|253) " /proc/devices; echo "---"
sudo mknod mynull c 1 3 && sudo chmod 666 mynull && ls -l mynull && echo "hello" > mynull
```

```text
crw-rw-rw- 1 root root      1,  3 Oct  5 22:38 /dev/null
crw-rw---- 1 root dialout   4, 64 Oct  5 22:38 /dev/ttyS0
brw-rw---- 1 root disk    253,  0 Oct  5 22:38 /dev/vda
brw-rw---- 1 root disk    253,  1 Oct  5 22:38 /dev/vda1
crw-rw-rw- 1 root root      1,  5 Oct  5 22:38 /dev/zero
---
  1 mem
  4 /dev/vc/0
  4 tty
  4 ttyS
  7 vcs
 89 i2c
253 ttyMV
  7 loop
253 virtblk
---
crw-rw-rw- 1 root root 1, 3 Oct  5 22:45 mynull
bash: line 1: mynull: Permission denied
```

`/tmp`에 만든 노드에 쓰기가 **Permission denied**로 실패했다. 이유는 마운트 옵션이다:

```sh
findmnt -no OPTIONS /tmp
cd /var/tmp/A3 && sudo mknod mynull c 1 3 && sudo chmod 666 mynull && echo hello > mynull && echo "write ok; size=$(stat -c %s mynull)"
cat /sys/dev/char/1:3/uevent
readlink -f /sys/dev/block/253:1
```

```text
rw,nosuid,nodev,size=1994416k,nr_inodes=1048576,inode64,usrquota
write ok; size=0
MAJOR=1
MINOR=3
DEVNAME=null
DEVMODE=0666
/sys/devices/pci0000:00/0000:00:06.0/virtio2/block/vda/vda1
```

출력에서 볼 것:

- `/dev/null`은 `c 1, 3`, `/dev/vda1`은 `b 253, 1`. `/proc/devices`에서 char 쪽 253은 ttyMV, block 쪽 253은 virtblk로 **char와 block은 major 번호 공간이 따로**다.
- `/tmp`는 `nodev`로 마운트되어 장치 노드가 무시된다(보안 옵션). `nodev`가 없는 `/var/tmp`(루트 ext4)에 만든 `c 1 3` 노드는 진짜 /dev/null처럼 동작해서 써도 크기 0이다.
- `/sys/dev/char/<major>:<minor>`와 `/sys/dev/block/…`은 장치 번호에서 sysfs의 장치 트리로 가는 지름길이다. vda1이 PCI 00:06.0의 virtio 장치에 붙어 있다는 것까지 보인다. "이 /dev 노드의 드라이버가 뭐지?"를 찾을 때 실무에서 매일 쓴다.

---

## 10. 유저 공간에서 하드웨어에 접근하기

커널 드라이버를 아직 안 쓰고(또는 못 쓰고) 유저 공간에서 하드웨어를 만지는 방법이 몇 가지 있다. 보드 bring-up 초기, 센서 동작 확인, 생산 테스트 툴에서 자주 쓴다.

| 방법 | 장치 노드 | 언제 쓰나 | 한계 |
|---|---|---|---|
| i2c-dev | `/dev/i2c-N` | 새 I2C 센서 레지스터 확인, i2c-tools | 인터럽트 못 받음, 커널 드라이버와 충돌 가능 |
| spidev | `/dev/spidevB.C` | SPI 장치 프로토타이핑 | DT에 spidev 노드를 따로 선언해야 함, 인터럽트 없음 |
| sysfs / IIO 속성 | `/sys/bus/iio/devices/…` 등 | 드라이버가 있는 장치의 값 읽기·설정 | 고속 데이터는 buffer 인터페이스로 |
| GPIO character device | `/dev/gpiochipN` (libgpiod) | 리셋 핀, 인터럽트 핀 테스트 | (H4. 옛 `/sys/class/gpio`는 폐기 예정) |
| `/dev/mem` mmap | `/dev/mem` | 레지스터 덤프 디버그(최후 수단) | 위험, 보통 막혀 있음 |
| UIO | `/dev/uioN` | 유저 공간 드라이버(FPGA 등) | 인터럽트는 받지만 DMA·전원 관리는 직접 |

### 10.1 i2c-dev와 I2C_RDWR

I2C 센서 레지스터 읽기는 보통 "**START → 주소+W → 레지스터 번호 → repeated START → 주소+R → 데이터 N바이트 → STOP**"이다. Don이 로직 분석기로 매일 보던 그 파형이다. 중간에 STOP이 들어가면 안 되는 센서가 많으므로(레지스터 포인터가 리셋되거나 다른 마스터가 끼어들 수 있음) `write()` 후 `read()`를 따로 부르는 대신 **`I2C_RDWR` ioctl 한 번에 메시지 두 개**를 넘긴다. 커널은 이것을 하나의 트랜잭션(중간은 repeated START)으로 보낸다.

다른 방법은 SMBus 프로토콜(`I2C_SMBUS` ioctl)이다. "read byte data" 같은 정해진 형태만 되지만 SMBus만 지원하는 컨트롤러에서도 동작한다. i2c-tools의 `i2cget`이 이 방식이다.

무엇을 확인하나: 장치가 없으면 open에서 ENOENT, 권한이 없으면 EACCES, 어댑터가 raw I2C를 지원하지 않으면 I2C_RDWR이 EOPNOTSUPP, 주소에 장치가 없으면 에러.

```c
/* ex12: /dev/i2c-N 로 레지스터 읽기 — I2C_RDWR(combined) 와 I2C_SMBUS 두 방식 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>


int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/i2c-1";
    int ADDR = argc > 2 ? (int)strtol(argv[2], NULL, 0) : 0x50;   /* 7-bit 주소 */
    int fd = open(dev, O_RDWR);
    if (fd < 0) { printf("open %s: %s\n", dev, strerrorname_np(errno)); return 1; }

    unsigned long funcs;
    if (ioctl(fd, I2C_FUNCS, &funcs) < 0) { perror("I2C_FUNCS"); return 1; }
    printf("adapter funcs=0x%08lx  I2C(raw)=%s  SMBUS_READ_BYTE_DATA=%s\n", funcs,
           funcs & I2C_FUNC_I2C ? "yes" : "no",
           funcs & I2C_FUNC_SMBUS_READ_BYTE_DATA ? "yes" : "no");

    /* 방식 1: write(reg) + repeated START + read(2) 를 한 트랜잭션으로 */
    unsigned char reg = 0x00, rx[2] = { 0 };
    struct i2c_msg msgs[2] = {
        { .addr = (__u16)ADDR, .flags = 0,        .len = 1, .buf = &reg },
        { .addr = (__u16)ADDR, .flags = I2C_M_RD, .len = 2, .buf = rx },
    };
    struct i2c_rdwr_ioctl_data xfer = { .msgs = msgs, .nmsgs = 2 };
    if (ioctl(fd, I2C_RDWR, &xfer) < 0)
        printf("I2C_RDWR -> %s\n", strerrorname_np(errno));
    else
        printf("I2C_RDWR -> rx = %02x %02x\n", rx[0], rx[1]);

    /* 방식 2: SMBus "read byte data" (주소 고정 후 I2C_SMBUS) */
    if (ioctl(fd, I2C_SLAVE, ADDR) < 0) { perror("I2C_SLAVE"); return 1; }
    union i2c_smbus_data d;
    struct i2c_smbus_ioctl_data sm = { .read_write = I2C_SMBUS_READ,
        .command = 0x10, .size = I2C_SMBUS_BYTE_DATA, .data = &d };
    if (ioctl(fd, I2C_SMBUS, &sm) < 0)
        printf("I2C_SMBUS read reg 0x10 -> %s\n", strerrorname_np(errno));
    else
        printf("I2C_SMBUS read reg 0x10 -> 0x%02x\n", d.byte);
    close(fd);
    return 0;
}
```

먼저 I2C 장치가 전혀 없을 때(기본 경로 `/dev/i2c-1`):

```text
$ ./ex12
open /dev/i2c-1: ENOENT
```

이 VM에는 커널의 가짜 I2C 어댑터인 `i2c-stub` 모듈이 `chip_addr=0x68`(MPU-6050 계열 IMU 주소)로 올라가 있어서 `/dev/i2c-0`이 존재한다. 직접 올리려면 `sudo modprobe i2c-stub chip_addr=0x68`이다.

```sh
cat /sys/class/i2c-dev/i2c-0/name ; ls -l /dev/i2c-0
./ex12 /dev/i2c-0                                            # 일반 사용자
echo "--- sudo, addr 0x50 (no chip):";  sudo ./ex12 /dev/i2c-0 0x50
echo "--- sudo, addr 0x68 (stub chip):"; sudo ./ex12 /dev/i2c-0 0x68
```

```text
SMBus stub driver
crw-rw---- 1 root i2c 89, 0 Oct  5 22:41 /dev/i2c-0
open /dev/i2c-0: EACCES
--- sudo, addr 0x50 (no chip):
adapter funcs=0x0c7f0000  I2C(raw)=no  SMBUS_READ_BYTE_DATA=yes
I2C_RDWR -> EOPNOTSUPP
I2C_SMBUS read reg 0x10 -> ENODEV
--- sudo, addr 0x68 (stub chip):
adapter funcs=0x0c7f0000  I2C(raw)=no  SMBUS_READ_BYTE_DATA=yes
I2C_RDWR -> EOPNOTSUPP
I2C_SMBUS read reg 0x10 -> 0x00
```

`sudo strace -e trace=ioctl`로 보면 (strace가 i2c 번호 이름을 모르면 `_IOC(…)` 형태로 해독한다. 0x7 0x5 = I2C_FUNCS, 0x7 0x7 = I2C_RDWR, 0x7 0x3 = I2C_SLAVE, 0x7 0x20 = I2C_SMBUS):

```text
ioctl(3, _IOC(_IOC_NONE, 0x7, 0x5, 0), 0xffffff2341d0) = 0
ioctl(3, _IOC(_IOC_NONE, 0x7, 0x7, 0), 0xffffff2341d8) = -1 EOPNOTSUPP (Operation not supported)
ioctl(3, _IOC(_IOC_NONE, 0x7, 0x3, 0), 0x50) = 0
ioctl(3, _IOC(_IOC_NONE, 0x7, 0x20, 0), 0xffffff2341e8) = -1 ENODEV (No such device)
```

출력에서 볼 것:

- `ENOENT`: 노드가 없다 = 컨트롤러 드라이버가 안 떴거나 DT에서 버스가 disabled이거나 i2c-dev가 없다. 실제 보드에서 가장 먼저 보는 에러다.
- `EACCES`: 노드는 `root:i2c 0660`이다. 일반 사용자는 `i2c` 그룹에 넣거나 udev rule로 권한을 준다.
- `I2C_FUNCS`로 어댑터 능력을 먼저 물어보는 것이 정석이다. stub은 `I2C_FUNC_I2C` 비트가 없어서(SMBus 전용) `I2C_RDWR`이 `EOPNOTSUPP`로 실패한다. 진짜 SoC I2C 컨트롤러(i.MX, AM62 …)는 보통 둘 다 지원한다.
- 0x50(장치 없음)에서는 `ENODEV`, 0x68(stub 칩)에서는 값 0x00을 읽었다. 실제 하드웨어에서 NACK이 나면 드라이버에 따라 `ENXIO`나 `EREMOTEIO`가 돌아온다(커널 문서 `Documentation/i2c/fault-codes.rst`). 로직 분석기에서 NACK을 본 것과 errno를 짝지어 기억해 두면 디버깅이 빨라진다.
- 주의: 커널 드라이버가 이미 붙은 주소에 `I2C_SLAVE`를 하면 `EBUSY`가 난다. 정말 필요하면 `I2C_SLAVE_FORCE`가 있지만, 드라이버와 동시에 같은 칩을 만지는 것이므로 레지스터 상태가 꼬일 수 있다.

### 10.2 spidev

SPI는 전이중이라 "보내는 동안 받는다". spidev의 `SPI_IOC_MESSAGE(n)`은 `struct spi_ioc_transfer` 배열을 받아 CS를 잡은 채로 n개의 transfer를 연속 실행한다. 아래는 IMU의 WHO_AM_I 레지스터(예: 0x75)를 읽는 전형적인 코드다. 첫 바이트의 MSB가 "읽기" 비트인 것은 많은 SPI 센서의 관례다(데이터시트 확인 필수).

무엇을 확인하나: 이 VM에는 SPI 컨트롤러가 없으므로 컴파일이 되는지, 그리고 장치가 없을 때의 실패 모습.

```c
/* ex13: spidev 전이중 전송 — 장치가 없으면 open 단계에서 ENOENT */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/spidev0.0";
    int fd = open(dev, O_RDWR);
    if (fd < 0) { printf("open %s: %s\n", dev, strerrorname_np(errno)); return 1; }

    uint8_t mode = SPI_MODE_3;              /* CPOL=1, CPHA=1 (IMU에 흔함) */
    uint32_t speed = 1000000;               /* 1 MHz */
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) { perror("cfg"); return 1; }

    /* WHO_AM_I 읽기: 첫 바이트 = 0x80|reg (읽기 비트), 둘째 바이트 동안 데이터가 나옴 */
    uint8_t tx[2] = { 0x80 | 0x75, 0x00 }, rx[2] = { 0 };
    struct spi_ioc_transfer t = {
        .tx_buf = (uintptr_t)tx, .rx_buf = (uintptr_t)rx,
        .len = 2, .speed_hz = speed, .bits_per_word = 8,
    };
    if (ioctl(fd, SPI_IOC_MESSAGE(1), &t) < 0) { perror("SPI_IOC_MESSAGE"); return 1; }
    printf("WHO_AM_I = 0x%02x\n", rx[1]);
    close(fd);
    return 0;
}
```

```text
open /dev/spidev0.0: ENOENT
```

출력에서 볼 것: 장치 노드가 없으면 open에서 ENOENT로 끝난다. 실제 보드에서는 DT의 SPI 컨트롤러 아래에 spidev로 바인딩되는 자식 노드가 있어야 `/dev/spidev<bus>.<cs>`가 생긴다(H7, D 모듈). 번호 해독은 ex10에서 본 대로 `SPI_IOC_MESSAGE(1)` = `0x40206b00`이다.

### 10.3 /dev/mem — 왜 위험한가

`/dev/mem`은 **물리 주소 공간 전체**를 파일로 보여준다. `mmap(fd, offset = 물리주소)`로 레지스터를 유저 공간에 매핑하면 bare-metal처럼 `volatile uint32_t *`로 직접 읽고 쓸 수 있다. 펌웨어 엔지니어에게는 매력적이지만 위험하다:

1. **커널 드라이버와 경쟁**: 드라이버가 같은 레지스터를 read-modify-write 하는 중에 끼어들면 상태가 깨진다. 락도 없다.
2. **전원·클럭 상태를 모른다**: 클럭이 꺼진 블록의 레지스터를 읽으면 SoC에 따라 bus error(SError)로 커널 전체가 죽는다. Don이 bring-up에서 봤던 "클럭 안 켜고 레지스터 읽어서 행" 그대로다.
3. **보안**: 루트가 커널 메모리를 마음대로 읽고 쓸 수 있으면 secure boot·권한 분리가 무의미하다.

그래서 대부분의 커널은 `CONFIG_STRICT_DEVMEM=y`로 **RAM 영역 접근을 막고** I/O 영역만 허용한다(`CONFIG_IO_STRICT_DEVMEM=y`면 드라이버가 점유한 I/O 영역도 막는다). 이 VM은 STRICT_DEVMEM=y, IO_STRICT_DEVMEM은 꺼져 있다. 안전을 위해 이 예제는 **RAM(커널 코드 영역)만** 시도한다. MMIO 영역을 VM에서 함부로 읽지 않는다.

무엇을 확인하나: 일반 사용자와 root가 RAM 주소를 읽으려 할 때 어떻게 막히는지.

```c
/* ex14: /dev/mem — 물리 주소를 그대로 읽으려 하면 무슨 일이? (STRICT_DEVMEM) */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    off_t phys = argc > 1 ? (off_t)strtoull(argv[1], NULL, 0) : 0;
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { printf("open /dev/mem: %s\n", strerrorname_np(errno)); return 1; }

    uint32_t v;
    ssize_t r = pread(fd, &v, 4, phys);
    if (r < 0) printf("pread  phys 0x%llx: %s\n", (unsigned long long)phys, strerrorname_np(errno));
    else       printf("pread  phys 0x%llx: 0x%08x\n", (unsigned long long)phys, v);

    void *m = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, phys & ~0xfffLL);
    if (m == MAP_FAILED) printf("mmap   phys 0x%llx: %s\n", (unsigned long long)phys, strerrorname_np(errno));
    else { printf("mmap   phys 0x%llx: ok, first word 0x%08x\n", (unsigned long long)phys,
                  *(volatile uint32_t *)m); munmap(m, 4096); }
    close(fd);
    return 0;
}
```

```sh
sudo cat /proc/iomem | grep -E "Kernel code|System RAM" | head -3   # 주소 고르기
./ex14 0xbee60000
sudo ./ex14 0xbee60000
```

```text
$ ./ex14 0xbee60000
open /dev/mem: EACCES
$ sudo ./ex14 0xbee60000
pread  phys 0xbee60000: EPERM
mmap   phys 0xbee60000: EPERM
```

출력에서 볼 것: `/dev/mem`은 `root:kmem 0640`이라 일반 사용자는 EACCES. root여도 STRICT_DEVMEM 때문에 RAM은 read도 mmap도 EPERM이다. 레지스터를 봐야 한다면 정식 경로를 쓴다: 드라이버의 debugfs/regmap 덤프(`/sys/kernel/debug/regmap/…`, H2), 또는 UIO. `devmem2`나 busybox `devmem`은 개발 보드에서 레지스터를 잠깐 들여다볼 때만 쓰는 도구다.

### 10.4 sysfs 속성 파일 — 한 파일에 값 하나

sysfs(`/sys`)는 커널 객체(장치, 드라이버, 버스)를 디렉터리 트리로 보여주고, 각 속성(attribute)을 **텍스트 값 하나짜리 파일**로 노출한다. 규칙:

- 한 파일 = 한 값 (ASCII 텍스트, 보통 끝에 개행). 최대 PAGE_SIZE.
- 읽을 때마다 드라이버의 `show()` 함수가 불려서 그 순간의 값을 만든다. 쓰면 `store()`가 불린다.
- 같은 fd로 다시 읽으려면 **`lseek(fd, 0, SEEK_SET)` 후 read** (아니면 EOF가 나온다). 매번 open/close 해도 되지만 고속 폴링에서는 열어 둔 fd를 되감는 게 싸다.

무엇을 확인하나: 되감지 않으면 두 번째부터 EOF이고, 되감으면 새 값이 나오는지. 네트워크 수신 바이트 카운터를 쓰고, 값이 바뀌도록 ping을 동시에 돌린다.

```c
/* ex15: sysfs 속성 파일 — 한 파일에 값 하나, 다시 읽으려면 lseek(0) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void die(const char *m) { perror(m); exit(1); }

int main(void)
{
    const char *p = "/sys/class/net/eth0/statistics/rx_bytes";
    int fd = open(p, O_RDONLY);
    if (fd < 0) die(p);
    char buf[64];
    for (int i = 0; i < 3; i++) {
        ssize_t n = read(fd, buf, sizeof buf - 1);
        if (n < 0) die("read");
        buf[n] = 0;
        printf("read #%d without lseek -> %zd bytes %s", i, n, n ? buf : "(EOF)\n");
    }
    for (int i = 0; i < 3; i++) {
        if (lseek(fd, 0, SEEK_SET) < 0) die("lseek");   /* 되감으면 새 값 */
        ssize_t n = read(fd, buf, sizeof buf - 1);      /* = 드라이버 show() 재호출 */
        if (n < 0) die("read");
        buf[n] = 0;
        printf("read #%d after lseek(0) -> %zd bytes %s", i, n, buf);
        usleep(200 * 1000);
    }
    close(fd);
    return 0;
}
```

```sh
GW=$(ip route | awk "/default/{print \$3}"); (ping -q -c 12 -i 0.1 $GW >/dev/null &) ; ./ex15
```

```text
read #0 without lseek -> 10 bytes 126853751
read #1 without lseek -> 0 bytes (EOF)
read #2 without lseek -> 0 bytes (EOF)
read #0 after lseek(0) -> 10 bytes 126853751
read #1 after lseek(0) -> 10 bytes 126853947
read #2 after lseek(0) -> 10 bytes 126854143
```

출력에서 볼 것: lseek 없이 다시 읽으면 0바이트(EOF). lseek(0) 후에는 매번 `show()`가 다시 불려 값이 늘어난다(126853751 → 126853947 → 126854143, 값은 실행마다 다르다). 드라이버가 `sysfs_notify()`를 지원하는 속성이면 `poll()`로 POLLPRI를 기다렸다가 "lseek(0) + read"로 새 값을 읽는 패턴도 쓴다(G8, A4).

쓰기도 같다: `echo 100 > /sys/bus/iio/devices/iio:device0/sampling_frequency` 같은 한 줄이 드라이버 `store()` 호출이다. C에서 쓸 때는 `write(fd, "100", 3)`처럼 값 하나만 쓴다. 드라이버가 거부하면 write가 `-1 EINVAL`을 돌려주므로 반환값을 반드시 확인한다.

---

## 11. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 660 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <rect x="20" y="15" width="250" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="145" y="40" font-size="13" text-anchor="middle">task_struct (current)</text>
<rect x="20" y="80" width="250" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="145" y="105" font-size="13" text-anchor="middle">files_struct → fdtable</text> <rect x="20" y="145" width="250" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="145" y="170" font-size="13" text-anchor="middle">fd[3] (+ close_on_exec 비트)</text> <line x1="145" y1="55" x2="145" y2="79" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/> <line x1="145" y1="120" x2="145" y2="144" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/>
<rect x="310" y="120" width="330" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/> <text x="475" y="142" font-size="12" text-anchor="middle">struct file  (= open file description)</text> <text x="475" y="166" font-size="12" text-anchor="middle">f_pos, f_flags (O_NONBLOCK …)</text>
<text x="475" y="190" font-size="12" text-anchor="middle">f_op → file_operations</text> <text x="475" y="214" font-size="12" text-anchor="middle">private_data (드라이버가 쓰는 칸)</text> <line x1="270" y1="165" x2="309" y2="165" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/>
<rect x="20" y="215" width="250" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/> <text x="145" y="237" font-size="12" text-anchor="middle">inode: i_rdev = 89:0</text> <text x="145" y="257" font-size="12" text-anchor="middle">i_fop = def_chr_fops</text>
<rect x="20" y="292" width="250" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/> <text x="145" y="317" font-size="12" text-anchor="middle">chrdev_open() → cdev 찾기</text> <line x1="145" y1="267" x2="145" y2="291" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/>
<rect x="310" y="262" width="330" height="105" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/> <text x="475" y="284" font-size="12" text-anchor="middle">i2c-dev 드라이버의 file_operations</text> <text x="475" y="306" font-size="12" text-anchor="middle">.open = i2cdev_open</text>
<text x="475" y="328" font-size="12" text-anchor="middle">.read / .write</text> <text x="475" y="350" font-size="12" text-anchor="middle">.unlocked_ioctl = i2cdev_ioctl</text> <line x1="270" y1="312" x2="309" y2="312" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/>
<line x1="560" y1="230" x2="560" y2="261" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/> <text x="570" y="250" font-size="12" text-anchor="start">f_op 가 가리킴</text> <text x="20" y="357" font-size="12" text-anchor="start">open 때: inode → cdev → f_op 설정</text>
</svg>
```
그림 8 — fd 3에서 드라이버 함수까지. `open()` 때 inode의 장치 번호로 cdev를 찾아 `struct file`의 `f_op`를 드라이버 것으로 바꿔 둔다. 이후 read/ioctl은 `f_op`만 따라간다.

### 11.1 open

`openat()` → `do_sys_openat2()` → `do_filp_open()` → `path_openat()`이 경로를 따라가 inode를 찾고, `struct file`을 할당한 뒤 `vfs_open()` → `do_dentry_open()`이 `file->f_op = inode->i_fop`로 세팅하고 `f_op->open`을 부른다.

장치 파일이면 `init_special_inode()`가 미리 `i_fop = &def_chr_fops`(char) 또는 `def_blk_fops`(block)로 세팅해 두었다. char면 `def_chr_fops.open` = `chrdev_open()`이 `i_rdev`(major:minor)로 등록된 `struct cdev`를 찾아서 `file->f_op`를 **드라이버의 `file_operations`로 교체**하고 드라이버의 `.open`을 부른다. 마지막으로 `fd_install()`이 빈 fd 번호에 이 `struct file`을 꽂는다. O_CLOEXEC면 `fdtable`의 `close_on_exec` 비트맵에 표시한다.

드라이버를 쓸 때 다시 만나는 곳: `cdev_add()`/`alloc_chrdev_region()`으로 major:minor를 등록하는 것, 그리고 `.open`에서 `filp->private_data`에 장치 구조체를 걸어 두는 관용구(G4, G7 misc device).

### 11.2 read / write

`read()` → `ksys_read()` → `fdget_pos()`(fd → `struct file`, 공유된 정규 파일이면 `f_pos_lock` 잡기) → `vfs_read()` → `file->f_op->read` 또는 `->read_iter`. 드라이버의 `.read(filp, ubuf, count, ppos)`가 `copy_to_user()`로 데이터를 넘기고 넘긴 바이트 수를 반환한다. 이 반환값이 그대로 유저의 `read()` 반환값이다 → **short read는 드라이버가 "그만큼만 줬다"는 뜻**이다. 정규 파일은 `read_iter` → `filemap_read()`가 page cache에서 복사한다. `write()`는 `generic_perform_write()`가 page cache 페이지를 dirty로 표시하고 끝난다. 이것이 ex8의 "write only 0.6 us"다.

### 11.3 ioctl

`ioctl()` → `do_vfs_ioctl()`이 먼저 공통 명령(`FIOCLEX`, `FIONCLEX`, `FIONBIO`, `FIOASYNC`, 정규 파일의 `FIONREAD` 등)을 처리하고, 나머지는 `vfs_ioctl()` → `filp->f_op->unlocked_ioctl(filp, cmd, arg)`로 보낸다. 32비트 유저 프로그램이 64비트 커널에서 부르면 `.compat_ioctl`이 불린다(구조체 레이아웃이 다를 수 있어서). i2c-dev의 경우 `i2cdev_ioctl()` → `I2C_RDWR` → `i2c_transfer()` → 어댑터 드라이버의 `algo->xfer`(옛 이름 `master_xfer`)이고, 어댑터에 그 함수가 없으면 `-EOPNOTSUPP` — ex12에서 stub이 돌려준 그 에러다. spidev는 `spidev_ioctl()` → `spi_sync()`.

### 11.4 fork, exec, close, fsync

- `fork()` → `copy_process()` → `copy_files()` → `dup_fd()`: fdtable을 복사하고 각 `struct file`의 참조 카운트를 올린다(`f_count`, 6.13부터 `file_ref_t f_ref`). `CLONE_FILES`(스레드 생성)면 복사 없이 `files_struct` 자체를 공유한다.
- `exec()` → `do_close_on_exec()`: 비트맵에 표시된 fd만 닫는다.
- `close()`: fd 슬롯을 비우고 참조를 하나 내린다. **마지막 참조가 사라질 때만** 드라이버의 `.release`가 불린다. dup된 fd가 남아 있으면 close해도 드라이버는 모른다 — 드라이버 테스트에서 "release가 안 불려요"의 흔한 원인이다. `.flush`는 close 때마다 불린다.
- `fsync()` → `vfs_fsync_range()` → `f_op->fsync`(ext4는 `ext4_sync_file()`): dirty page writeback → 저널 커밋 → 필요하면 `blkdev_issue_flush()`로 장치에 cache flush.

정리하면 드라이버의 `struct file_operations`가 이 노트의 거의 모든 syscall과 1:1로 대응한다:

| 유저 호출 | 드라이버 콜백 | 다시 볼 노트 |
|---|---|---|
| `open` / 마지막 `close` | `.open` / `.release` | G4 |
| `read` / `write` | `.read` / `.write` (또는 `_iter`) | G4, E6 (copy_to_user) |
| 블록하는 read | `wait_event_interruptible` + `wake_up` | F3 |
| `ioctl` | `.unlocked_ioctl`, `.compat_ioctl` | G5 |
| `lseek` | `.llseek` (`no_llseek`, `default_llseek` …) | G4 |
| `poll` / `epoll` | `.poll` + `poll_wait` | A4, G6 |
| `mmap` | `.mmap` + `remap_pfn_range` | A5, G5 |
| `fsync` | `.fsync` | J2, J5 |
| sysfs 읽기/쓰기 | `show()` / `store()` (`DEVICE_ATTR_RW`) | G8 |

---

## 12. Embedded 실무에서 다시 보기

### 12.1 설정 파일을 전원 차단에 안전하게 저장 — ex18

센서 키트라면 IMU 캘리브레이션 값을 파일에 저장한다. 현장에서 배터리가 빠지는 순간 저장 중이었다면? `fopen("w")`로 덮어쓰다가 끊기면 **빈 파일 또는 반쯤 쓴 파일**이 남고, 다음 부팅에 캘리브레이션이 사라진다. 정석은 "임시 파일에 다 쓰고 → fsync → rename으로 교체 → 디렉터리 fsync"다. `rename()`은 같은 파일시스템 안에서 원자적이라서 전원이 언제 끊겨도 옛 파일 아니면 새 파일만 보인다.

```c
/* ex18: 전원이 언제 나가도 "옛 파일 아니면 새 파일"만 남게 저장하기 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int write_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0) return -1;              /* (EINTR 재시도는 ex4 참고) */
        p += w; n -= (size_t)w;
    }
    return 0;
}

int atomic_save(const char *path, const char *data)
{
    char tmp[256], dir[256];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    snprintf(dir, sizeof dir, "%s", path);

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    if (write_all(fd, data, strlen(data)) < 0 || fsync(fd) < 0) { close(fd); return -1; }
    if (close(fd) < 0) return -1;                        /* 1) 새 내용이 디스크에 */
    if (rename(tmp, path) < 0) return -1;                /* 2) 이름을 원자적으로 교체 */
    int dfd = open(dirname(dir), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) return -1;
    int r = fsync(dfd);                                  /* 3) 디렉터리 엔트리도 디스크에 */
    close(dfd);
    return r;
}

int main(void)
{
    if (atomic_save("/var/tmp/A3/imu_cal.conf", "gyro_bias=0.013\naccel_scale=1.002\n") < 0) {
        perror("atomic_save");
        return 1;
    }
    puts("saved");
    return 0;
}
```

```text
$ strace -o ex18.st -e trace=openat,write,fsync,close,renameat,renameat2 ./ex18
openat(AT_FDCWD, "/var/tmp/A3/imu_cal.conf.tmp", O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC, 0644) = 3
write(3, "gyro_bias=0.013\naccel_scale=1.00"..., 34) = 34
fsync(3)                                = 0
close(3)                                = 0
renameat(AT_FDCWD, "/var/tmp/A3/imu_cal.conf.tmp", AT_FDCWD, "/var/tmp/A3/imu_cal.conf") = 0
openat(AT_FDCWD, "/var/tmp/A3", O_RDONLY|O_CLOEXEC|O_DIRECTORY) = 3
fsync(3)                                = 0
close(3)                                = 0
write(1, "saved\n", 6)                  = 6
+++ exited with 0 +++
```

출력에서 볼 것: `fsync(파일)` → `close` → `renameat` → 디렉터리 open → `fsync(디렉터리)` 순서. 마지막 디렉터리 fsync가 없으면 rename(디렉터리 엔트리 변경) 자체가 아직 디스크에 없을 수 있다. 실제 제품에서는 여기에 CRC를 넣어 읽을 때 검증하고, 중요하면 A/B 두 벌을 둔다(L4의 A/B 업데이트와 같은 발상).

### 12.2 UART GNSS 수신기 — non-blocking + 짧은 read

GNSS 모듈은 `/dev/ttyS1` 같은 UART로 NMEA 문장을 흘려보낸다. read는 문장 경계를 모르고 "지금 FIFO에 있는 만큼" 준다. 그래서

1. `open("/dev/ttyS1", O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC)`
2. termios로 raw 모드, 보율 설정 (H8)
3. epoll로 readable을 기다리고(A4), 읽은 바이트를 링버퍼에 붙인 뒤 `\n` 단위로 잘라 파싱

이 구조는 Don이 펌웨어에서 UART RX 인터럽트 → 링버퍼 → 파서로 짜던 것과 같다. 차이는 "인터럽트 핸들러" 자리를 커널 tty 계층이 대신하고, 유저는 epoll 이벤트를 받는다는 것뿐이다.

### 12.3 새 I2C 센서 bring-up 순서

1. `ls /dev/i2c-*`, `cat /sys/class/i2c-dev/*/name`으로 버스 번호 확인 (없으면 DT·컨트롤러 드라이버 문제)
2. `i2cdetect -y 1`로 주소 응답 확인 (NACK이면 배선·전원·주소 핀; 여기서 scope를 댄다)
3. `i2cget -y 1 0x68 0x75`로 WHO_AM_I 확인
4. ex12처럼 `I2C_RDWR`로 burst read를 해서 데이터시트대로 나오는지 확인
5. 확인되면 커널 드라이버(IIO, I2)로 옮기고, 이후에는 `/sys/bus/iio/devices/iio:deviceN/in_accel_x_raw` 같은 sysfs 속성과 버퍼 인터페이스로 읽는다

유저 공간 i2c-dev 코드는 **확인용**이다. 인터럽트(데이터 준비 핀), 정확한 타임스탬프, 전원 관리가 필요해지면 커널 드라이버로 간다.

### 12.4 드라이버 테스트 툴의 체크리스트

드라이버를 쓰고 나서 테스트 툴을 짤 때 이 노트의 내용이 그대로 체크 항목이 된다: 작은 버퍼로 read 해도 데이터가 안 사라지는가(short read 처리), O_NONBLOCK에서 EAGAIN을 주는가, 블록 중 Ctrl-C가 먹는가(EINTR), 모르는 ioctl에 ENOTTY를 주는가, 두 프로세스가 동시에 열면 어떻게 되는가, fork 후 자식이 닫아도 장치가 살아 있는가(release 시점).

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| read/write 반환값을 n으로 가정 | pipe·소켓·UART에서 데이터가 가끔 잘리거나 섞임 | short count | `read_full`/`write_full` 루프 |
| EINTR을 에러로 처리 | 시그널(타이머, SIGCHLD)이 올 때마다 툴이 랜덤하게 실패 | 블록 중 시그널 | EINTR이면 재시도, 또는 SA_RESTART |
| non-blocking fd에서 EAGAIN을 continue | CPU 100% | busy loop | poll/epoll로 기다리기 (A4) |
| O_CLOEXEC 없이 open | 자식 프로세스가 장치·소켓을 물고 있어 장치가 안 닫히고 포트가 안 풀림 | fd 상속 | 모든 open에 O_CLOEXEC, SOCK_CLOEXEC |
| fclose/close만 하고 끝 | 전원 차단 후 설정 파일이 0바이트 | page cache·장치 캐시 | fsync + rename 패턴 (ex18) |
| fork 전 stdio 버퍼를 안 비움 | 로그 줄이 두 번 찍힘 | 버퍼가 자식에게도 복사됨 | fork 전 `fflush(NULL)` |
| 여러 스레드가 lseek+read 공유 | 엉뚱한 위치의 데이터 | OFD offset 공유 | pread/pwrite |
| sysfs 값을 같은 fd로 다시 read | 두 번째부터 빈 값 | offset이 끝에 있음 | `lseek(fd, 0, SEEK_SET)` 후 read |
| I2C를 write 후 read로 따로 | 센서가 이상한 레지스터 값을 줌 | 사이에 STOP이 들어감 | I2C_RDWR 한 번에 두 메시지 |
| `/dev/mem`으로 레지스터 직접 쓰기 | 가끔 커널 hang·데이터 깨짐, 혹은 EPERM | 드라이버와 경쟁, 클럭 꺼짐, STRICT_DEVMEM | 드라이버·debugfs·UIO 사용 |

---

## 14. 면접에서 이렇게 말한다

**Q.** What happens when two processes write to the same file? Explain file descriptors vs open file descriptions.

**A.** fd는 프로세스별 테이블의 인덱스이고, 그 항목이 커널의 open file description(`struct file`)을 가리킨다. offset과 상태 플래그는 OFD에 있다. 각자 open 했으면 OFD가 따로라 offset도 따로라서 서로 덮어쓸 수 있다. dup이나 fork로 공유했으면 offset을 공유해서 이어 쓴다. 로그처럼 여러 writer가 있으면 O_APPEND를 쓰면 커널이 매 write마다 원자적으로 끝으로 옮긴다.

> There are three levels: the per-process fd table, the open file description which holds the offset and status flags, and the inode. Two independent open() calls create two descriptions with separate offsets, so writers can overwrite each other. dup() or fork() share one description, so they share the offset. For multiple writers to a log file I use O_APPEND, which makes the seek-to-end and write atomic.

**꼬리 질문**: "Where is the close-on-exec flag stored?" → fd 쪽(fdtable 비트맵). 그래서 dup이 이 비트를 복사하지 않는다.

**Q.** Why should you always check the return value of read() and write()?

**A.** 요청보다 적게 처리되는 short count가 정상 동작이기 때문이다. pipe·소켓·tty·장치는 지금 있는 만큼만 준다. 시그널이 오면 EINTR, non-blocking이면 EAGAIN이 온다. 그래서 루프를 돌며 남은 양을 계속 요청하고, EINTR은 재시도, EAGAIN은 poll로 기다린다. 실제로 pipe에 100000바이트를 non-blocking으로 쓰면 65536만 쓰인다.

> read and write may transfer fewer bytes than requested; that is normal for pipes, sockets, ttys and device files. They can also fail with EINTR if a signal arrives, or EAGAIN on a non-blocking fd. So I wrap them in read_full/write_full loops that retry on EINTR, advance the buffer on short counts, and stop on EOF. On non-blocking fds I go back to poll or epoll instead of spinning.

**Q.** What is the difference between fflush, fsync and fdatasync? When does data actually reach the flash?

**A.** fflush는 stdio 버퍼를 write()로 커널에 넘길 뿐이다. 그때 데이터는 page cache에 있다. fsync는 그 파일의 데이터와 메타데이터를 장치까지 내리고 장치 캐시 flush까지 기다린다. fdatasync는 파일 크기처럼 데이터를 다시 읽는 데 필요한 메타데이터만 같이 내리고 mtime 같은 건 건너뛰어서 더 싸다. 내 VM 측정으로 4 KiB 기준 write만 0.6 us, fdatasync 약 75 us, fsync 약 110 us였다. 임베디드 eMMC는 PLP가 없어서 flush가 실제로 중요하다.

> fflush only moves data from the stdio buffer into the kernel with write(); it is still in the page cache. fsync writes the file's dirty data and all its metadata to the device and issues a cache flush, so it survives power loss. fdatasync skips metadata that isn't needed to read the data back, like mtime, so it's cheaper. Close does not imply fsync. On boards with eMMC and no power-loss protection I use write-to-temp, fsync, rename, then fsync the directory for config files.

**꼬리 질문**: "What's O_DIRECT, does it make writes durable?" → page cache는 건너뛰지만 장치 캐시에 머물 수 있으므로 내구성 보장이 아니다. 여전히 fsync/fdatasync가 필요하다.

**Q.** How is an ioctl request number constructed?

**A.** 32비트 안에 방향 2비트, 인자 크기 14비트, 드라이버 고유 type 8비트, 명령 번호 8비트를 넣는다. `_IOR/_IOW/_IOWR/_IO` 매크로로 만든다. 방향은 유저 관점이라 `_IOR`은 커널이 유저 버퍼에 쓴다. 예로 `BLKGETSIZE64`는 `_IOR(0x12, 114, size_t)` = 0x80081272다. 엉뚱한 장치에 보내면 그 드라이버가 모르는 번호라 ENOTTY가 난다.

> It's a 32-bit value built with _IO, _IOR, _IOW or _IOWR: two direction bits, a 14-bit argument size, an 8-bit type or magic number per driver, and an 8-bit command number. Direction is from user space's point of view. For example BLKGETSIZE64 is _IOR(0x12, 114, size_t), which is 0x80081272. In the driver I switch on cmd in unlocked_ioctl, use copy_from_user and copy_to_user with _IOC_SIZE, and return -ENOTTY for unknown commands.

**Q.** How would you read a register from an I2C sensor from user space?

**A.** `/dev/i2c-N`을 열고 `I2C_FUNCS`로 어댑터 능력을 확인한 뒤, `I2C_RDWR` ioctl로 "레지스터 번호 쓰기" 메시지와 "N바이트 읽기" 메시지 두 개를 한 번에 보낸다. 그러면 중간에 STOP 없이 repeated START로 나간다. SMBus만 되는 컨트롤러면 `I2C_SMBUS` ioctl을 쓴다. 이건 bring-up 확인용이고, 제품에서는 IIO 같은 커널 드라이버로 옮긴다.

> I open /dev/i2c-N, check I2C_FUNCS, and use the I2C_RDWR ioctl with two i2c_msg entries: a one-byte write of the register address and a read with I2C_M_RD. The kernel sends it as one combined transaction with a repeated start, which many sensors require. If the adapter only supports SMBus I use I2C_SMBUS, like i2cget does. That's fine for bring-up, but for production I'd move to a kernel driver, for example IIO, so I get interrupts, timestamps and power management.

**Q.** Why is using /dev/mem to poke registers a bad idea?

**A.** 커널 드라이버와 락 없이 같은 레지스터를 만지게 되고, 클럭·전원이 꺼진 블록을 건드리면 bus error로 시스템이 죽을 수 있고, 보안상 루트에게 물리 메모리 전체를 여는 것이다. 그래서 STRICT_DEVMEM으로 RAM 접근은 막혀 있다. 대신 드라이버, regmap debugfs, UIO를 쓴다.

> It bypasses the driver that owns the hardware, so there's no locking and no knowledge of clock or power state; touching a gated block can trigger a bus error and hang the SoC. It's also a security hole, which is why most kernels enable STRICT_DEVMEM and block RAM access. For debugging I use regmap debugfs or the driver's sysfs, and for real user-space drivers UIO or VFIO.

**Q.** What's the difference between blocking and non-blocking I/O on a device file, and how does the driver implement it?

**A.** blocking이면 데이터가 없을 때 드라이버가 프로세스를 wait queue에 재우고, 인터럽트 핸들러가 데이터가 오면 깨운다. non-blocking이면 드라이버가 `filp->f_flags & O_NONBLOCK`을 보고 바로 -EAGAIN을 준다. 시그널로 깨면 -ERESTARTSYS를 반환해서 커널이 EINTR이나 자동 재시작을 결정한다. 정규 파일에는 O_NONBLOCK이 의미 없다.

> In blocking mode, the driver's read sleeps on a wait queue with wait_event_interruptible until the interrupt handler calls wake_up. In non-blocking mode it checks O_NONBLOCK in filp->f_flags and returns -EAGAIN immediately, and the application uses poll or epoll, which calls the driver's poll method. If a signal interrupts the sleep the driver returns -ERESTARTSYS and the kernel either restarts the call or returns EINTR depending on SA_RESTART.

**Q.** After fork(), the child closes a device fd but the driver's release() isn't called. Why?

**A.** fork 후에는 부모·자식 fd가 같은 `struct file`을 참조 카운트로 공유한다. release는 마지막 참조가 사라질 때만 불린다. 부모가 아직 열고 있으니 안 불리는 게 정상이다. 매 close마다 뭔가 해야 하면 `.flush`를 쓴다.

> Because fork shares the same struct file between parent and child and only bumps its reference count. The driver's release runs only when the last reference is dropped, so as long as the parent still has it open, release is not called. If the driver needs a hook on every close, that's what the flush method is for.

---

## 15. 직접 해보기

1. **개념**: `fd = open("a", O_RDONLY); fd2 = open("a", O_RDONLY); fd3 = dup(fd);` 후 `read(fd3, buf, 10)`을 하면 fd, fd2, fd3의 offset은 각각 얼마인가? 그림 2 형태로 그려 보라.
   정답/힌트: fd와 fd3은 같은 OFD라 둘 다 10, fd2는 0.

2. **코드**: ex4의 `read_full`에 타임아웃을 추가하라. non-blocking fd를 받아 `poll()`로 기다리면서 n바이트를 채우거나 `timeout_ms`가 지나면 지금까지 받은 양을 돌려주는 `read_full_timeout(fd, buf, n, timeout_ms)`.
   정답/힌트: EAGAIN이면 `poll(&pfd, 1, 남은 시간)`; 남은 시간은 `clock_gettime(CLOCK_MONOTONIC)`으로 계산. poll도 EINTR이 날 수 있다.

3. **관찰**: `sleep 100 &`를 띄우고 `ls -l /proc/$!/fd`, `cat /proc/$!/fdinfo/0`을 보라. 그리고 `exec 5>/tmp/x` 를 한 셸에서 `ls -l /proc/$$/fd`로 5번이 생긴 것을 확인하라.
   정답/힌트: `$!`는 마지막 백그라운드 PID, `$$`는 셸 자신. `exec 5>&-`로 닫는다.

4. **strace**: `strace -f -e trace=openat,dup2,dup3,close,execve sh -c 'ls > /tmp/out 2>&1'`을 실행해서 셸이 리다이렉션을 어떤 syscall로 구현하는지 찾아라.
   정답/힌트: 이 VM의 sh(dash)에서는 `openat(/tmp/out, O_WRONLY, O_CREAT, O_TRUNC) = 3` → `fcntl(1, F_DUPFD, 10)`(원래 stdout을 10번에 백업) → `dup3(3, 1, 0)` → `close(3)` → `dup3(1, 2, 0)` → `execve(ls)` 순서였다. arm64에는 dup2 syscall이 없어서 glibc가 `dup3(…, 0)`으로 부른다.

5. **측정**: ex8을 `fallocate`로 미리 공간을 잡은 파일과, 매번 파일 끝에 덧붙이는(append) 경우로 바꿔 fsync·fdatasync 비용을 다시 재 보라. 덧붙이기에서 fdatasync가 fsync와 비슷해지는 이유는?
   정답/힌트: append는 파일 크기(데이터를 읽는 데 필요한 메타데이터)가 매번 바뀌어서 fdatasync도 inode를 커밋해야 한다.

6. **ioctl**: `<linux/watchdog.h>`의 `WDIOC_GETTIMEOUT`, `WDIOC_SETTIMEOUT` 번호를 ex10으로 분해하고, `/dev/watchdog`이 있는 보드라면 현재 타임아웃을 읽는 프로그램을 짜라(주의: watchdog을 열면 타이머가 시작될 수 있다. 닫기 전에 magic close 'V'를 쓴다).
   정답/힌트: 둘 다 `int` 크기(4). GET은 `_IOR`, SET은 `_IOWR`(설정 후 실제 적용값을 돌려줌).

7. **sysfs**: ex15를 `/sys/class/net/eth0/statistics/` 아래 여러 파일(rx_bytes, tx_bytes, rx_packets)을 열어 두고 1초마다 lseek+read로 초당 증가량을 출력하는 미니 `ifstat`으로 바꿔라.
   정답/힌트: fd 3개를 열어 두고 매 주기 `pread(fd, buf, n, 0)`를 쓰면 lseek이 필요 없다(pread는 offset 0부터 읽는다).

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| file descriptor (fd) | 파일 디스크립터 | 프로세스별 fd table의 정수 인덱스 |
| open file description (OFD) | 열린 파일 기술 | open 한 번에 하나 생기는 커널 객체, offset과 상태 플래그 보관 (`struct file`) |
| inode | 아이노드 | 파일 자체의 메타데이터와 데이터 위치, 장치 파일이면 major:minor |
| close-on-exec | exec 시 닫기 | fd 단위 비트, exec 때 자동으로 닫힘 (`O_CLOEXEC`, `FD_CLOEXEC`) |
| short read/write | 부분 전송 | 요청보다 적은 바이트가 처리된 정상 반환 |
| EINTR | 시그널 중단 | 블록 중인 syscall이 시그널 때문에 데이터 없이 반환 |
| EAGAIN | 다시 시도 | non-blocking fd에서 지금은 할 수 없음 (`EWOULDBLOCK`과 같은 값) |
| SA_RESTART | 자동 재시작 | 시그널 핸들러 후 끊긴 syscall을 커널이 다시 걸게 하는 플래그 |
| page cache | 페이지 캐시 | 파일 데이터를 RAM에 캐시하는 커널 계층, write는 여기서 끝남 |
| writeback | 지연 쓰기 | dirty page를 백그라운드로 장치에 내리는 커널 동작 |
| fsync / fdatasync | 동기화 | 파일 데이터(+메타데이터)를 장치 매체까지 내리고 기다림 |
| FUA | Force Unit Access | 이 쓰기는 장치 캐시를 거치지 말고 매체까지 쓰라는 명령 비트 |
| PLP | Power Loss Protection | 전원 차단 시 커패시터로 캐시를 매체에 내리는 SSD 기능 |
| O_DIRECT | 직접 I/O | page cache를 건너뛰는 I/O, 정렬 제약 있음 |
| hole | 구멍 | 파일 안의 할당되지 않은 영역, 읽으면 0 |
| ioctl | I/O 제어 | 드라이버별 제어 명령을 보내는 syscall |
| `_IOR` / `_IOW` | ioctl 매크로 | 방향·크기·type·nr을 32비트로 인코딩 |
| ENOTTY | 부적절한 ioctl | 그 장치가 모르는 ioctl 명령 |
| major / minor | 장치 번호 | 드라이버 식별 / 그 드라이버 안의 장치 식별 |
| char / block device | 문자 / 블록 장치 | 바이트 스트림 장치 / 블록 단위 랜덤 액세스 장치 |
| devtmpfs, udev | 장치 노드 관리 | 커널이 노드를 만들고 udev가 이름·권한을 다듬음 |
| i2c-dev | I2C 유저 인터페이스 | `/dev/i2c-N`으로 I2C 어댑터를 유저에 노출 |
| repeated START | 재시작 조건 | STOP 없이 방향을 바꾸는 I2C 조건, I2C_RDWR이 만들어 줌 |
| spidev | SPI 유저 인터페이스 | `/dev/spidevB.C`, `SPI_IOC_MESSAGE`로 전이중 전송 |
| STRICT_DEVMEM | /dev/mem 제한 | RAM 영역의 /dev/mem 접근을 막는 커널 옵션 |
| sysfs attribute | sysfs 속성 | 값 하나짜리 텍스트 파일, 드라이버의 show/store와 연결 |
| `file_operations` | 파일 연산 표 | 드라이버가 채우는 콜백 구조체 (.open .read .unlocked_ioctl …) |

---

## 17. 요약 & 체크리스트

fd는 프로세스별 번호표이고, 진짜 상태(offset, O_APPEND, O_NONBLOCK)는 open 한 번마다 생기는 open file description(`struct file`)에, 파일 자체 정보는 inode에 있다. dup과 fork는 OFD를 공유하므로 offset을 공유하고, close-on-exec만 fd 쪽 비트다. read/write는 "최대 n바이트"이며 short count, EINTR, EAGAIN을 루프로 다뤄야 한다. 데이터는 stdio 버퍼 → page cache → 장치 캐시 → 매체 순으로 내려가고, fflush·fsync·fdatasync는 각각 다른 경계를 넘긴다. 전원 안전이 필요한 파일은 temp → fsync → rename → fsync(dir). ioctl 번호는 dir·size·type·nr 비트 필드이며, 장치는 major:minor로 드라이버에 연결된다. 유저 공간 하드웨어 접근은 i2c-dev(I2C_RDWR), spidev(SPI_IOC_MESSAGE), sysfs(lseek 0 후 read)가 정식 경로이고 `/dev/mem`은 최후 수단이다. 이 모든 호출은 커널에서 `struct file`의 `f_op` → 드라이버의 `file_operations`로 이어진다.

- [ ] fd, open file description, inode 3단 구조를 그림으로 그리고 각 층에 무엇이 있는지 설명할 수 있다
- [ ] dup/dup2/fork 후 offset이 공유되는지 예측할 수 있다
- [ ] O_CLOEXEC가 왜 필요하고 왜 open 플래그로 줘야 하는지 설명할 수 있다
- [ ] O_APPEND가 lseek+write와 어떻게 다른지 설명할 수 있다
- [ ] short read/write, EINTR, EAGAIN을 처리하는 `read_full`/`write_full`을 보지 않고 짤 수 있다
- [ ] pread/pwrite가 필요한 상황을 말할 수 있다
- [ ] blocking/non-blocking이 드라이버에서 어떻게 구현되는지(wait queue, -EAGAIN, -ERESTARTSYS) 말할 수 있다
- [ ] fflush, fsync, fdatasync, O_SYNC, O_DIRECT의 차이와 비용을 말할 수 있다
- [ ] 전원 차단에 안전한 파일 갱신 패턴을 코드로 짤 수 있다
- [ ] ioctl 번호를 dir/size/type/nr로 분해하고 `_IOR`/`_IOW`의 방향을 설명할 수 있다
- [ ] major/minor로 장치 노드와 드라이버를 연결하고, `/sys/dev/char/M:m`으로 장치를 찾아갈 수 있다
- [ ] I2C_RDWR로 레지스터를 읽는 코드를 짜고, ENOENT/EACCES/EOPNOTSUPP/ENXIO의 의미를 구분할 수 있다

---

## 참고 자료

- man 페이지: [open(2)](https://man7.org/linux/man-pages/man2/open.2.html), [read(2)](https://man7.org/linux/man-pages/man2/read.2.html), [write(2)](https://man7.org/linux/man-pages/man2/write.2.html), [dup(2)](https://man7.org/linux/man-pages/man2/dup.2.html), [fcntl(2)](https://man7.org/linux/man-pages/man2/fcntl.2.html), [lseek(2)](https://man7.org/linux/man-pages/man2/lseek.2.html), [pread(2)](https://man7.org/linux/man-pages/man2/pread.2.html), [fsync(2)](https://man7.org/linux/man-pages/man2/fsync.2.html), [ioctl(2)](https://man7.org/linux/man-pages/man2/ioctl.2.html), [signal(7)](https://man7.org/linux/man-pages/man7/signal.7.html) (SA_RESTART와 재시작되는 syscall 목록), [mknod(2)](https://man7.org/linux/man-pages/man2/mknod.2.html), [mem(4)](https://man7.org/linux/man-pages/man4/mem.4.html), [ioctl_iflags(2)](https://man7.org/linux/man-pages/man2/ioctl_iflags.2.html)
- Michael Kerrisk, "The Linux Programming Interface" — 4장(파일 I/O), 5장(파일 I/O 심화: OFD, dup, pread), 13장(파일 I/O 버퍼링), 44장(pipe), 63장(대안 I/O 모델)
- 커널 문서: [ioctl 번호 규칙 (ioctl-number.rst)](https://docs.kernel.org/userspace-api/ioctl/ioctl-number.html), [Implementing I2C device drivers in userspace (dev-interface)](https://docs.kernel.org/i2c/dev-interface.html), [I2C/SMBUS fault codes](https://docs.kernel.org/i2c/fault-codes.html), [SPI userspace API (spidev)](https://docs.kernel.org/spi/spidev.html), [sysfs 규칙](https://docs.kernel.org/filesystems/sysfs.html)
- 커널 소스(elixir.bootlin.com): `include/uapi/asm-generic/ioctl.h`, `fs/read_write.c`(ksys_read, vfs_read), `fs/ioctl.c`(do_vfs_ioctl), `fs/char_dev.c`(chrdev_open), `fs/file.c`(dup_fd, do_close_on_exec), `drivers/i2c/i2c-dev.c`, `drivers/i2c/i2c-stub.c`, `drivers/spi/spidev.c`, `drivers/char/mem.c`
- Bootlin "Linux kernel and driver development" 교육 슬라이드 — character driver, file_operations 절
- OSTEP 39장 "Interlude: Files and Directories", 42장 "Crash Consistency: FSCK and Journaling"
