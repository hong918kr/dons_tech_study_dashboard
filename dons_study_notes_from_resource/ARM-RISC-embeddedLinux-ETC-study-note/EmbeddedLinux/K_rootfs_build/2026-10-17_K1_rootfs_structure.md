# K1. rootfs 구조 — FHS, 필수 구성, 장치 노드, devtmpfs, 부팅해서 셸이 뜨기까지 무엇이 필요한가

> **이 노트를 다 읽으면**: 커널이 rootfs에서 실제로 요구하는 것(실행 파일 하나)부터 셸·로그인·모듈까지 필요한 것을 층별로 말할 수 있다 · 정적/동적 init, 로더·libc, `/dev/console`이 빠졌을 때 나오는 커널 메시지를 직접 재현하고 해독할 수 있다 · major/minor·`mknod`·devtmpfs·정적 `/dev`·mdev/udev의 차이를 설명할 수 있다 · sudo 없이 BusyBox + `/etc` + 모듈로 수 MB짜리 ext4 rootfs를 만들어 `root=/dev/vda rootwait`로 부팅하고, fakeroot가 왜 필요한지 실측으로 보여 줄 수 있다
> **왜 필요한가**: "What's the minimum needed to boot to a shell?", "Kernel panics with 'No working init found' — how do you debug?"는 Embedded Linux 면접 단골이다. 실무에서는 Buildroot·Yocto가 만든 이미지가 부팅 안 될 때, 결국 rootfs 안에 무엇이 빠졌는지를 손으로 찾아야 한다
> **Don 기준 난이도**: 펌웨어 이미지 레이아웃, 부트 시퀀스, 링커가 묶는 "한 덩어리 이미지"는 이미 안다 / 커널과 별도로 "파일시스템 안의 프로그램"이 PID 1이 된다는 구조, ELF 로더·장치 노드·파일 소유권처럼 파일시스템에 기록되는 메타데이터가 새로 배울 부분
> **선행 노트**: B3(ELF·동적 링킹), C6 §8(rootfs 마운트 경로), C7 §1·§3(PID 1, initramfs·switch_root). J3(읽기 전용 root + overlay)와 K2(BusyBox)·K3(Buildroot)·K4/K5(Yocto)가 이 노트 위에 올라간다

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어에서는 리셋 벡터부터 `main()`, 태스크 생성, 명령 처리까지 전부 **링커가 묶은 이미지 하나** 안에서 일어난다. Linux는 다르다. 커널 `Image`는 자기 일을 끝내면 **파일시스템에서 실행 파일 하나를 찾아 PID 1로 실행**하고, 그 뒤의 모든 것(셸, 서비스, 센서 데몬)은 그 파일시스템 안의 파일이다. 이 "커널이 처음 `/`로 삼는 파일시스템"이 **rootfs(root filesystem)**다.

```svg
<svg viewBox="0 0 680 315" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="20" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="85" y="43" font-size="14" text-anchor="middle">커널 Image</text>
<text x="85" y="63" font-size="12" text-anchor="middle">드라이버·FS 내장</text>
<rect x="180" y="20" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="255" y="43" font-size="14" text-anchor="middle">루트(/) 확보</text>
<text x="255" y="63" font-size="12" text-anchor="middle">initramfs / root=</text>
<rect x="350" y="20" width="150" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="425" y="43" font-size="14" text-anchor="middle">PID 1 실행</text>
<text x="425" y="63" font-size="12" text-anchor="middle">/init, /sbin/init</text>
<rect x="520" y="20" width="150" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="595" y="43" font-size="14" text-anchor="middle">셸 · 앱</text>
<text x="595" y="63" font-size="12" text-anchor="middle">getty, sh, 서비스</text>
<line x1="160" y1="48" x2="178" y2="48" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="330" y1="48" x2="348" y2="48" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="500" y1="48" x2="518" y2="48" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="425" y1="76" x2="425" y2="118" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<text x="340" y="112" font-size="13" text-anchor="middle">PID 1이 뜨고 셸까지 가려면 rootfs 안에 있어야 하는 것</text>
<rect x="10" y="130" width="126" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="73" y="153" font-size="13" text-anchor="middle">init 실행 파일</text>
<text x="73" y="172" font-size="12" text-anchor="middle">정적 또는 동적</text>
<rect x="142" y="130" width="126" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="205" y="153" font-size="13" text-anchor="middle">로더 + libc</text>
<text x="205" y="172" font-size="12" text-anchor="middle">동적일 때만</text>
<rect x="274" y="130" width="126" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="337" y="153" font-size="13" text-anchor="middle">/dev/console</text>
<text x="337" y="172" font-size="12" text-anchor="middle">fd 0 · 1 · 2</text>
<rect x="406" y="130" width="126" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="469" y="153" font-size="12" text-anchor="middle">/proc /sys /dev</text>
<text x="469" y="172" font-size="12" text-anchor="middle">init이 마운트</text>
<rect x="538" y="130" width="126" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="601" y="153" font-size="13" text-anchor="middle">/etc 설정</text>
<text x="601" y="172" font-size="12" text-anchor="middle">inittab, fstab</text>
<text x="20" y="214" font-size="13">빨강·주황: 없으면 panic이거나 출력이 사라진다.</text>
<text x="20" y="234" font-size="13">회색: 없어도 셸은 뜨지만 ps·mount·로그인이 깨진다.</text>
<text x="20" y="262" font-size="13">bare-metal 비유: 리셋 벡터에서 main()까지는 링커가 묶은 한 이미지 안의 일이다.</text>
<text x="20" y="282" font-size="13">Linux는 커널 이미지와 따로 "파일시스템 안의 프로그램 하나"를 찾아 PID 1로 실행한다.</text>
<text x="20" y="302" font-size="13">그 파일시스템 전체가 rootfs이고, 이 노트는 그 안에 무엇이 꼭 있어야 하는지를 다룬다.</text>
</svg>
```
그림 1 — 커널이 rootfs에서 요구하는 것과, 셸까지 가려면 더 필요한 것

이 노트의 질문은 하나다: **"부팅해서 셸 프롬프트가 뜨려면 rootfs에 정확히 무엇이 있어야 하나?"** 답을 층으로 쌓는다.

| 층 | 들어가는 것 | 빠지면 | 이 노트 |
|---|---|---|---|
| 0 | 실행 파일 하나 (`/init` 또는 `/sbin/init`) | `No working init found` panic | §1 |
| 1 | 동적 링크면 로더 + libc + 라이브러리 | `Failed to execute /init (error -2)` | §3 |
| 2 | `/dev/console` (또는 devtmpfs) | `Warning: unable to open an initial console`, 출력 없음 | §4, §5 |
| 3 | `/proc` `/sys` `/dev` 마운트 | `ps`, `mount`, `top`이 깨짐 | §6 |
| 4 | `/etc` (inittab, fstab, passwd …) | init이 기본값으로 돌거나 로그인 불가 | §6, §7 |
| 5 | `/lib/modules/$(uname -r)`, `/lib/firmware` | `modprobe` 실패, 드라이버 probe 실패 | §7 |
| 6 | 올바른 소유자·권한 | 일반 사용자가 시스템 파일을 고칠 수 있음 | §9 |

실험 환경: 노트의 VM 명령은 Lima Ubuntu VM `elinux`(호스트 커널 `7.0.0-38-generic`, aarch64)에서, 게스트는 공용 랩 커널 Linux 6.18.55(`/var/lab/out/Image`, 모듈 예제는 `Image-fs` = `6.18.55-fs`)를 QEMU `virt`로 부팅했다. 모든 산출물은 `/var/tmp/K1/`에 있다. 부팅은 아래 래퍼 하나로 한다.

```sh
#!/bin/bash
# k1_run.sh <tag> <timeout_s> <kernel> <initrd|-> "<cmdline>" [extra qemu args...]
# log -> /var/tmp/K1/k1_<tag>.log
tag=$1; to=$2; kern=$3; rd=$4; cmd=$5; shift 5
args=(-machine virt -cpu cortex-a57 -m 512M -nographic -no-reboot -kernel "$kern")
[ "$rd" != "-" ] && args+=(-initrd "$rd")
timeout "$to" qemu-system-aarch64 "${args[@]}" -append "$cmd" "$@" </dev/null > /var/tmp/K1/k1_$tag.log 2>&1
echo "exit=$? lines=$(wc -l < /var/tmp/K1/k1_$tag.log)"
```

`exit=0`은 게스트가 스스로 전원을 껐다는 뜻이고, `exit=124`는 `timeout`이 QEMU를 죽였다는 뜻(대개 panic 후 멈춤)이다.

---

## 1. rootfs란 — 커널이 요구하는 것은 파일 하나

### 1.1 직관

커널 입장에서 rootfs는 "`execve()`할 파일 하나를 찾을 장소"일 뿐이다. 커널은 부팅 끝에 다음 순서로 PID 1 후보를 실행해 본다(`init/main.c`의 `kernel_init()`, C6 §8에서 소스를 봤다).

1. `rdinit=`(기본 `/init`) — initramfs가 있을 때
2. `init=`으로 지정한 경로 — 지정했는데 실패하면 바로 panic
3. `/sbin/init`, `/etc/init`, `/bin/init`, `/bin/sh` — 차례로
4. 전부 실패하면 `Kernel panic - not syncing: No working init found.`

그러니 **이론상 최소 rootfs는 정적 링크된 실행 파일 하나**다. 라이브러리도, 셸도, `/etc`도 필요 없다. 펌웨어 비유로는 "부트로더가 점프할 엔트리 포인트 하나만 있는 이미지"다.

### 1.2 예제 1 — 정적 C 프로그램 하나가 rootfs 전부

무엇을 확인하나: 파일 하나(`/init`)짜리 cpio 아카이브로 부팅이 되는지, 그리고 커널이 init에게 무엇을 넘겨주는지(argv, 환경 변수).

```c
/* k1_init.c: the smallest useful PID 1. Mode comes from the kernel cmdline (K1_MODE=... becomes an env var) */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <time.h>
#include <unistd.h>

static double boot_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_BOOTTIME, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv, char **envp)
{
    const char *mode = getenv("K1_MODE");
    if (!mode) mode = "loop";

    if (fcntl(1, F_GETFD) < 0) {               /* no /dev/console -> fd 0/1/2 never opened */
        int e = errno;
        if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) < 0) return 10;
        int fd = open("/dev/ttyAMA0", O_RDWR);
        if (fd < 0) return 11;
        dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
        printf("K1: fd 1 was not open (errno=%d %s); mounted devtmpfs, reopened /dev/ttyAMA0\n",
               e, strerror(e));
    }
    printf("K1: hello from %s, pid=%d, t=%.3fs\n", argv[0], getpid(), boot_s());
    for (int i = 0; i < argc; i++) printf("K1:   argv[%d]=%s\n", i, argv[i]);
    for (char **e = envp; *e; e++) printf("K1:   env %s\n", *e);

    for (int tick = 1; tick <= 3; tick++) {    /* a real init would loop forever here */
        sleep(1);
        printf("K1: tick %d\n", tick);
    }
    if (strcmp(mode, "exit") == 0) {
        printf("K1: returning from main() as PID 1 ...\n");
        fflush(stdout);
        return 0;                              /* -> kernel panic */
    }
    printf("K1: done, powering off via reboot(2)\n");
    fflush(stdout);
    sync();
    reboot(RB_POWER_OFF);
    return 1;                                  /* not reached */
}
```

진짜 init이라면 마지막 루프가 `for (;;)`여야 한다. 여기서는 자동 실행을 위해 3초 뒤 `reboot(RB_POWER_OFF)`로 끈다. fd 검사 블록은 §5에서 쓴다.

```sh
cd /var/tmp/K1
S=/Users/donh/.elinux_scratch/K1
gcc -std=gnu11 -Wall -Wextra -O2 -static $S/k1_init.c -o k1_init_static
gcc -std=gnu11 -Wall -Wextra -O2 $S/k1_init.c -o k1_init_dyn
ls -l k1_init_static k1_init_dyn; file k1_init_static k1_init_dyn
mkdir -p r1 && cp k1_init_static r1/init && (cd r1 && find . | cpio -o -H newc --quiet) > k1_r1.cpio
ls -l k1_r1.cpio && cpio -itv < k1_r1.cpio
```

```text
-rwxrwxr-x 1 donh donh  71248 Oct 17 17:38 k1_init_dyn
-rwxrwxr-x 1 donh donh 713936 Oct 17 17:38 k1_init_static
k1_init_static: ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=86f24ffb01384ba7ca70526b84bb721aaf3af80c, for GNU/Linux 3.7.0, not stripped
k1_init_dyn:    ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, BuildID[sha1]=000bcb46bdaa9d50207b44247f13a4df5dfc2c53, for GNU/Linux 3.7.0, not stripped
-rw-rw-r-- 1 donh donh 714752 Oct 17 17:38 k1_r1.cpio
drwxrwxr-x   2 donh     donh            0 Oct 17 17:38 .
-rwxrwxr-x   1 donh     donh       713936 Oct 17 17:38 init
1396 blocks
```

```sh
$S/k1_run.sh e1loop 60 /var/lab/out/Image k1_r1.cpio "console=ttyAMA0 quiet hello_arg K1_MODE=loop"
cat k1_e1loop.log
```

```text
exit=0 lines=11
K1: hello from /init, pid=1, t=0.577s
K1:   argv[0]=/init
K1:   argv[1]=hello_arg
K1:   env HOME=/
K1:   env TERM=linux
K1:   env K1_MODE=loop
K1: tick 1
K1: tick 2
K1: tick 3
K1: done, powering off via reboot(2)
[    3.627321] reboot: Power down
```

출력에서 볼 것:

- **rootfs = 714 KB 파일 하나**로 PID 1까지 갔다. 커널은 cpio를 메모리 위 rootfs(ramfs/tmpfs)에 풀고 `/init`을 실행했다.
- 커널이 모르는 커맨드라인 인자는 init에게 간다: `=`가 없으면 **argv**(`hello_arg`), `=`가 있으면 **환경 변수**(`K1_MODE=loop`). 커널은 `HOME=/`, `TERM=linux`를 기본으로 넣는다. 부팅 로그에는 `Unknown kernel command line parameters "...", will be passed to user space.`가 찍힌다(예제 9 로그 37행에서 다시 본다).
- `quiet`라 커널 로그는 거의 안 나오고, 마지막 `reboot: Power down`만 보인다.

함정: `/dev/console`을 만든 적이 없는데 `printf`가 콘솔에 나왔다. 랩 커널은 `CONFIG_INITRAMFS_SOURCE=""`라 커널 안에 **기본 initramfs**(`usr/default_cpio_list`)가 들어 있고, 거기에 `/dev`, `/dev/console`(c 5 1), `/root`가 있다. 부트로더가 준 cpio는 그 위에 풀린다. 그래서 initramfs에서는 `/dev/console`을 잊어도 대개 티가 안 난다 — §5에서 이 안전망을 일부러 걷어 낸다.

### 1.3 cpio newc — 아카이브 안에는 소유자·모드·장치 번호가 들어 있다

무엇을 확인하나: initramfs 형식(newc)이 파일마다 무엇을 기록하는지.

```sh
xxd k1_r1.cpio | head -8
```

```text
00000000: 3037 3037 3031 3030 3434 4545 4631 3030  0707010044EEF100
00000010: 3030 3431 4644 3030 3030 3031 4635 3030  0041FD000001F500
00000020: 3030 3033 4538 3030 3030 3030 3032 3641  0003E8000000026A
00000030: 4434 3135 3042 3030 3030 3030 3030 3030  D4150B0000000000
00000040: 3030 3030 4644 3030 3030 3030 3031 3030  0000FD0000000100
00000050: 3030 3030 3030 3030 3030 3030 3030 3030  0000000000000000
00000060: 3030 3030 3032 3030 3030 3030 3030 2e00  00000200000000..
00000070: 3037 3037 3031 3030 3434 4545 4632 3030  0707010044EEF200
```

출력에서 볼 것: 헤더는 전부 **ASCII 16진수 8자리 필드**다. `070701`(magic) → ino `0044EEF1` → mode `000041FD`(= `040775`, 디렉터리 0775) → uid `000001F5`(= 501) → gid `000003E8`(= 1000) → nlink 2 → mtime … 마지막에 이름 `.`. 즉 cpio는 **빌드한 사람의 uid/gid를 그대로 기록**한다. 이게 §9 fakeroot 이야기의 출발점이다. 장치 노드라면 `rdevmajor`/`rdevminor` 필드에 번호가 들어간다(형식: docs.kernel.org의 "ramfs, rootfs and initramfs", `Documentation/driver-api/early-userspace/buffer-format.rst`).

### 1.4 예제 2 — PID 1이 끝나면: `Attempted to kill init!`

무엇을 확인하나: init이 `main()`에서 return하면 무슨 일이 생기는지. C7 §1.3에서 셸 스크립트로 이미 봤고, 여기서는 C init으로 다시 보며 **panic 직전 출력이 사라지는** 현상을 하나 더 본다.

```sh
$S/k1_run.sh e1exit 40 /var/lab/out/Image k1_r1.cpio "console=ttyAMA0 quiet K1_MODE=exit"
head -20 k1_e1exit.log
```

```text
exit=124 lines=31
K1: hello from /init, pid=1, t=0.605s
K1:   argv[0]=/init
K1:   env HOME=/
K1:   env TERM=linux
K1:   env K1_MODE=exit
K1: tick 1
K1: tick 2
[    3.655505] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000000
[    3.656440] CPU: 0 UID: 0 PID: 1 Comm: init Not tainted 6.18.55 #1 PREEMPT 
[    3.656606] Hardware name: linux,dummy-virt (DT)
[    3.656776] Call trace:
[    3.656950]  show_stack+0x28/0x7c (C)
[    3.657461]  dump_stack_lvl+0x60/0x80
[    3.657595]  dump_stack+0x18/0x24
[    3.657704]  vpanic+0xf8/0x2dc
[    3.657857]  abort+0x0/0x4
[    3.658036]  make_task_dead+0x0/0x1b0
[    3.658294]  do_group_exit+0x30/0xa4
[    3.658494]  __arm64_sys_exit_group+0x18/0x1c
[    3.658714]  invoke_syscall.constprop.0+0x40/0xe0
```

출력에서 볼 것:

- `exitcode=0x00000000` — `return 0`이 `exit_group(0)`이 됐고, `kernel/exit.c`의 `do_exit()`이 "죽는 태스크가 init이고 그 네임스페이스의 마지막 스레드"임을 보고 panic했다.
- **`K1: tick 3`과 `K1: returning ...`이 로그에 없다.** 같은 명령에 `panic=1`을 붙여 다시 돌려도 똑같이 `tick 2`까지만 나왔다. 프로그램은 두 줄을 `write()`했지만 그 바이트는 tty 계층 버퍼에 있었고, UART로 다 밀려나기 전에 panic이 CPU를 멈췄다. **필드 장비에서 "panic 직전 앱 로그가 안 남는" 이유 중 하나**다. 마지막 순간의 기록이 필요하면 `/dev/kmsg`에 쓰거나(커널 printk 버퍼라 panic 출력과 함께 나간다) pstore(L7)를 쓴다.
- `exit=124`: panic 후 멈춰서 `timeout`이 끝냈다. 제품이라면 `panic=5`(5초 후 리부트)나 HW 워치독을 둔다.

---

## 2. FHS — 디렉터리는 왜 이렇게 나뉘나

### 2.1 직관 — "수명과 쓰기 여부"로 나눈 것

FHS(Filesystem Hierarchy Standard)는 디렉터리 이름 목록이 아니라 **"이 파일은 언제 바뀌고, 누가 쓰고, 재부팅 후에도 남아야 하나"** 로 나눈 분류다. 펌웨어로 치면 코드 영역(XIP/ROM), 설정 영역(NVM의 config 블록), 로그 영역, RAM 영역을 나누는 것과 같다.

```svg
<svg viewBox="0 0 680 355" xmlns="http://www.w3.org/2000/svg">
<rect x="8" y="10" width="160" height="290" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="88" y="32" font-size="14" text-anchor="middle">읽기 전용 코드</text>
<text x="88" y="50" font-size="12" text-anchor="middle">이미지에 구워 넣음</text>
<text x="20" y="80" font-size="12">/usr/bin, /usr/sbin</text>
<text x="20" y="100" font-size="12">/bin → usr/bin</text>
<text x="20" y="120" font-size="12">/sbin → usr/sbin</text>
<text x="20" y="140" font-size="12">/lib → usr/lib</text>
<text x="20" y="160" font-size="12">/lib/ld-linux-*.so.1</text>
<text x="20" y="180" font-size="12">/lib/modules/(ver)</text>
<text x="20" y="200" font-size="12">/lib/firmware</text>
<text x="20" y="220" font-size="12">/usr/share</text>
<text x="20" y="240" font-size="12">/opt (3rd-party 앱)</text>
<rect x="174" y="10" width="160" height="290" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="254" y="32" font-size="14" text-anchor="middle">설정</text>
<text x="254" y="50" font-size="12" text-anchor="middle">이미지 기본값 + 수정</text>
<text x="186" y="80" font-size="12">/etc/inittab</text>
<text x="186" y="100" font-size="12">/etc/fstab</text>
<text x="186" y="120" font-size="12">/etc/passwd, group</text>
<text x="186" y="140" font-size="12">/etc/shadow (0600)</text>
<text x="186" y="160" font-size="12">/etc/hostname, hosts</text>
<text x="186" y="180" font-size="12">/etc/resolv.conf</text>
<text x="186" y="200" font-size="12">/etc/profile</text>
<text x="186" y="220" font-size="12">/etc/init.d/rcS</text>
<rect x="340" y="10" width="160" height="290" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="420" y="32" font-size="14" text-anchor="middle">영속 쓰기 데이터</text>
<text x="420" y="50" font-size="12" text-anchor="middle">재부팅 후에도 남음</text>
<text x="352" y="80" font-size="12">/var/lib (앱 상태)</text>
<text x="352" y="100" font-size="12">/var/log (영속이면)</text>
<text x="352" y="120" font-size="12">/home/(user)</text>
<text x="352" y="140" font-size="12">/root</text>
<text x="352" y="160" font-size="12">/data 같은 별도 파티션</text>
<text x="352" y="180" font-size="12">/mnt, /media</text>
<text x="352" y="200" font-size="12">(임시 마운트 지점)</text>
<rect x="506" y="10" width="166" height="290" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="589" y="32" font-size="14" text-anchor="middle">부팅마다 새로</text>
<text x="589" y="50" font-size="12" text-anchor="middle">RAM · 커널이 만들어 냄</text>
<text x="518" y="80" font-size="12">/proc (proc)</text>
<text x="518" y="100" font-size="12">/sys (sysfs)</text>
<text x="518" y="120" font-size="12">/dev (devtmpfs)</text>
<text x="518" y="140" font-size="12">/dev/pts (devpts)</text>
<text x="518" y="160" font-size="12">/run (tmpfs)</text>
<text x="518" y="180" font-size="12">/tmp (tmpfs)</text>
<text x="518" y="200" font-size="12">/var/run → /run</text>
<text x="518" y="220" font-size="12">빈 디렉터리만 이미지에</text>
<text x="20" y="322" font-size="13">왼쪽 두 칸은 SquashFS/EROFS 같은 읽기 전용 FS에 넣을 수 있다.</text>
<text x="20" y="342" font-size="13">오른쪽 두 칸은 쓰기가 필요하다: 별도 파티션, tmpfs, overlay (J3).</text>
</svg>
```
그림 2 — FHS를 수명·쓰기 여부로 다시 그린 것

| 디렉터리 | 무엇이 들어가나 | 임베디드에서의 의미 |
|---|---|---|
| `/bin`, `/sbin` | 사용자·관리자 명령. 요즘은 `/usr/bin`, `/usr/sbin`의 심볼릭 링크(usrmerge) | BusyBox 링크 수백 개가 여기 |
| `/usr` | 나머지 프로그램·라이브러리·데이터 (`/usr/lib`, `/usr/share`) | 읽기 전용 이미지의 대부분 |
| `/lib` | 공유 라이브러리, 동적 로더 `ld-linux-aarch64.so.1`, `/lib/modules`, `/lib/firmware` | 빠지면 동적 프로그램 전부 실행 불가 |
| `/etc` | 호스트별 설정 (텍스트) | 공장 기본값 + 필드에서 바뀌는 값이 섞이는 곳 — J3의 overlay 대상 |
| `/dev` | 장치 노드 | devtmpfs가 런타임에 채움 (§5) |
| `/proc`, `/sys` | 커널이 보여 주는 가상 파일시스템 | 이미지에는 빈 디렉터리만 |
| `/run` | 부팅 이후의 런타임 상태(PID 파일, 소켓). tmpfs | 재부팅하면 사라져야 하는 것 |
| `/tmp` | 임시 파일. 보통 tmpfs | flash 마모 방지 |
| `/var` | 바뀌는 데이터: `/var/log`, `/var/lib`(앱 상태), `/var/cache` | 영속 파티션 또는 tmpfs로 결정해야 함 |
| `/home`, `/root` | 사용자 홈, root 홈 | 장비에서는 거의 비어 있음 |
| `/opt` | 패키지 관리자 밖의 3rd-party 앱 묶음 | 벤더 SDK·AI 런타임이 자주 여기 |
| `/mnt`, `/media` | 임시 마운트 지점 (USB, SD) | 현장 로그 추출용 USB |

### 2.2 예제 3 — 현대 배포판의 usrmerge와 가상 FS (VM 호스트)

무엇을 확인하나: Ubuntu 26.04에서 `/bin`·`/lib`가 실제 디렉터리인지, 그리고 부팅마다 새로 생기는 디렉터리가 어떤 파일시스템인지.

```sh
stat -c "%A %N" /bin /sbin /lib /usr/bin
for m in / /proc /sys /dev /dev/pts /dev/shm /run /tmp; do findmnt -n -o TARGET,FSTYPE,SOURCE --mountpoint $m; done
```

```text
lrwxrwxrwx '/bin' -> 'usr/bin'
lrwxrwxrwx '/sbin' -> 'usr/sbin'
lrwxrwxrwx '/lib' -> 'usr/lib'
drwxr-xr-x '/usr/bin'
/ ext4 /dev/vda1
/proc proc proc
/sys sysfs sysfs
/dev devtmpfs devtmpfs
/dev/pts devpts devpts
/dev/shm tmpfs tmpfs
/run tmpfs tmpfs
/tmp tmpfs tmpfs
```

출력에서 볼 것: `/bin`, `/sbin`, `/lib`는 **`/usr` 아래로의 심볼릭 링크**다(usrmerge). 이유는 "읽기 전용 OS 전체를 `/usr` 하나에 몰아서 통째로 교체·검증하기 쉽게"다. 그리고 루트 ext4를 빼면 7개가 전부 커널이 RAM에 만드는 가상 FS다. 이미지에는 빈 마운트 지점만 있으면 된다.

함정: usrmerge 배포판에서 가져온 바이너리의 `PT_INTERP`는 `/lib/ld-linux-aarch64.so.1`이다. 내 rootfs에 `/lib`가 링크가 아니라 실제 디렉터리여도 상관없지만, **그 경로에 로더가 실제로 있어야** 한다(§3). Buildroot는 기본으로 usrmerge 형태(`BR2_ROOTFS_MERGED_USR`) 선택지를 주고, Yocto는 `usrmerge` DISTRO_FEATURE로 켠다(K3, K4).

---

## 3. 동적 링크 init — 로더와 libc가 rootfs에 있어야 한다

### 3.1 직관 — "실행 파일"은 사실 두 파일이다

동적 링크 ELF를 `execve()`하면 커널은 그 파일만 올리지 않는다. 프로그램 헤더의 **`PT_INTERP`**(로더 경로)를 읽어 **로더(`ld-linux-aarch64.so.1`)를 먼저 메모리에 올리고 로더의 진입점으로 점프**한다. 로더가 `DT_NEEDED`에 적힌 라이브러리(`libc.so.6`)를 찾아 매핑하고 재배치한 다음에야 `main()`이 돈다(B3 §5~§6). 펌웨어로 치면 "앱 이미지를 실행하려면 먼저 로더 ROM 코드가 있어야 하는" 구조다.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="10" width="330" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="175" y="31" font-size="13" text-anchor="middle">kernel_init() → run_init_process("/init")</text>
<text x="175" y="50" font-size="12" text-anchor="middle">커널 스레드가 execve 경로로 들어간다</text>
<rect x="10" y="80" width="330" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="175" y="101" font-size="13" text-anchor="middle">load_elf_binary(): 프로그램 헤더 읽기</text>
<text x="175" y="120" font-size="12" text-anchor="middle">PT_INTERP = /lib/ld-linux-aarch64.so.1</text>
<rect x="10" y="150" width="330" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="175" y="171" font-size="13" text-anchor="middle">커널이 로더 파일을 직접 연다</text>
<text x="175" y="190" font-size="12" text-anchor="middle">있으면 로더의 진입점으로 점프</text>
<rect x="10" y="220" width="330" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="175" y="241" font-size="13" text-anchor="middle">ld.so (유저 공간): NEEDED libc.so.6 찾기</text>
<text x="175" y="260" font-size="12" text-anchor="middle">/lib/aarch64-linux-gnu → /lib → /usr/lib</text>
<rect x="10" y="290" width="330" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="175" y="311" font-size="13" text-anchor="middle">main() 실행</text>
<text x="175" y="330" font-size="12" text-anchor="middle">"K1: hello from /init, pid=1"</text>
<line x1="175" y1="60" x2="175" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="175" y1="130" x2="175" y2="148" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="175" y1="200" x2="175" y2="218" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="175" y1="270" x2="175" y2="288" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="370" y="135" width="300" height="80" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="520" y="155" font-size="12" text-anchor="middle">로더 없음 → execve = -ENOENT</text>
<text x="520" y="174" font-size="12" text-anchor="middle">Failed to execute /init (error -2)</text>
<text x="520" y="193" font-size="12" text-anchor="middle">/sbin/init ... /bin/sh 차례로 시도</text>
<text x="520" y="209" font-size="12" text-anchor="middle">→ No working init found</text>
<rect x="370" y="230" width="300" height="80" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="520" y="250" font-size="12" text-anchor="middle">libc 없음 → ld.so가 메시지 출력</text>
<text x="520" y="269" font-size="12" text-anchor="middle">error while loading shared libraries</text>
<text x="520" y="288" font-size="12" text-anchor="middle">exit(127) → Attempted to kill init!</text>
<text x="520" y="304" font-size="12" text-anchor="middle">exitcode=0x00007f00</text>
<line x1="340" y1="175" x2="368" y2="175" stroke="#d0564a" marker-end="url(#ah3)"/>
<line x1="340" y1="245" x2="368" y2="265" stroke="#d0564a" marker-end="url(#ah3)"/>
<text x="370" y="40" font-size="13">파란색: 커널 안에서 일어남</text>
<text x="370" y="62" font-size="13">주황색: 이미 유저 공간(PID 1)</text>
<text x="370" y="84" font-size="13">빨간색: 두 가지 실패 — 메시지가 다르다</text>
<text x="370" y="106" font-size="13">"파일이 있는데 ENOENT"의 정체 (B3 §8)</text>
</svg>
```
그림 3 — 동적 init이 실행되기까지 두 번의 파일 탐색, 그리고 두 가지 다른 실패

### 3.2 예제 4 — 로더 없이 동적 init을 부팅하면

무엇을 확인하나: 같은 프로그램을 동적 링크로 바꿔 `/init` 하나만 넣었을 때의 정확한 커널 메시지.

```sh
mkdir -p r2 && cp k1_init_dyn r2/init && (cd r2 && find . | cpio -o -H newc --quiet) > k1_r2.cpio
$S/k1_run.sh e2nolib 40 /var/lab/out/Image k1_r2.cpio "console=ttyAMA0 K1_MODE=loop"
sed -n 245,280p k1_e2nolib.log
```

```text
exit=124 lines=293
[    0.722681] PM: genpd: Disabling unused power domains
[    0.768896] Freeing unused kernel memory: 3264K
[    0.769927] Run /init as init process
[    0.779760] Failed to execute /init (error -2)
[    0.779813] Run /sbin/init as init process
[    0.779821]   with arguments:
[    0.779828]     /sbin/init
[    0.779833]   with environment:
[    0.779836]     HOME=/
[    0.779840]     TERM=linux
[    0.779843]     K1_MODE=loop
[    0.779920] Run /etc/init as init process
[    0.779927]   with arguments:
[    0.779931]     /etc/init
[    0.779934]   with environment:
[    0.779940]     HOME=/
[    0.779943]     TERM=linux
[    0.779947]     K1_MODE=loop
[    0.779973] Run /bin/init as init process
[    0.779978]   with arguments:
[    0.779981]     /bin/init
[    0.779985]   with environment:
[    0.779988]     HOME=/
[    0.779992]     TERM=linux
[    0.779995]     K1_MODE=loop
[    0.780013] Run /bin/sh as init process
[    0.780017]   with arguments:
[    0.780020]     /bin/sh
[    0.780024]   with environment:
[    0.780027]     HOME=/
[    0.780030]     TERM=linux
[    0.780034]     K1_MODE=loop
[    0.780177] Kernel panic - not syncing: No working init found.  Try passing init= option to kernel. See Linux Documentation/admin-guide/init.rst for guidance.
```

출력에서 볼 것:

- **`/init`은 분명히 있는데 `error -2`(ENOENT)**다. 없는 것은 `/init`이 아니라 `PT_INTERP`가 가리키는 `/lib/ld-linux-aarch64.so.1`이다. 커널은 어느 파일이 없는지 말해 주지 않는다. 이게 B3 §8의 "파일이 있는데 not found"가 PID 1에서 일어난 모습이다.
- `rdinit=` 실패는 panic이 아니라 **경고 후 다음 후보로** 넘어간다. 그래서 진짜 원인(첫 줄)이 뒤의 긴 목록에 묻힌다. 로그에서 맨 처음 `Failed to execute`를 찾는 습관을 들인다.
- `quiet`를 빼야 이 줄들이 보인다. `quiet`로는 마지막 panic 한 줄만 보여서 디버깅이 어렵다.

### 3.3 예제 5 — readelf·ldd로 필요한 것을 찾아 하나씩 넣기

무엇을 확인하나: 동적 실행 파일이 rootfs에 요구하는 파일 목록을 도구로 뽑고, 하나씩 넣으며 실패 메시지가 어떻게 바뀌는지.

```sh
readelf -l k1_init_dyn | grep interpreter
readelf -d k1_init_dyn | grep NEEDED
ldd k1_init_dyn
/lib/ld-linux-aarch64.so.1 --help | sed -n "/search path/,/^$/p"
```

```text
      [Requesting program interpreter: /lib/ld-linux-aarch64.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-aarch64.so.1]
	linux-vdso.so.1 (0x0000f5cfd23e9000)
	libc.so.6 => /usr/lib/aarch64-linux-gnu/libc.so.6 (0x0000f5cfd21a0000)
	/lib/ld-linux-aarch64.so.1 (0x0000f5cfd23a0000)
Shared library search path:
  (libraries located via /etc/ld.so.cache)
  /lib/aarch64-linux-gnu (system search path)
  /usr/lib/aarch64-linux-gnu (system search path)
  /lib (system search path)
  /usr/lib (system search path)
```

출력에서 볼 것:

- `readelf -l`의 interpreter = **커널이 직접 여는 파일**. `readelf -d`의 NEEDED = **로더가 찾는 파일**. `ldd`는 호스트 기준 해석 결과다(호스트의 실제 경로 `/usr/lib/...`를 보여 준다). 크로스 빌드한 타깃 바이너리에 호스트 `ldd`를 쓰면 안 되고, `readelf -d`나 `<toolchain>-readelf`를 쓴다(B3 §8).
- 로더의 기본 탐색 경로: `/etc/ld.so.cache` → `/lib/aarch64-linux-gnu` → `/usr/lib/aarch64-linux-gnu` → `/lib` → `/usr/lib`. 이 순서는 Ubuntu glibc가 빌드될 때 정해진 것이다. 임베디드 rootfs에는 보통 `ld.so.cache`가 없으므로 이 기본 경로 안에 라이브러리를 두면 된다.

이제 세 가지 rootfs를 만든다: (a) 로더만, (b) 로더 + `/lib/aarch64-linux-gnu/libc.so.6`, (c) 로더 + `/lib/libc.so.6`.

```sh
mk(){ (cd $1 && find . | cpio -o -H newc --quiet) > $2; }
mkdir -p r2a/lib; cp k1_init_dyn r2a/init; cp -L /lib/ld-linux-aarch64.so.1 r2a/lib/; mk r2a k1_r2a.cpio
cp -a r2a r2b; mkdir -p r2b/lib/aarch64-linux-gnu; cp -L /lib/aarch64-linux-gnu/libc.so.6 r2b/lib/aarch64-linux-gnu/; mk r2b k1_r2b.cpio
cp -a r2a r2c; cp -L /lib/aarch64-linux-gnu/libc.so.6 r2c/lib/; mk r2c k1_r2c.cpio
for t in r2a r2b r2c; do $S/k1_run.sh e2$t 40 /var/lab/out/Image k1_$t.cpio "console=ttyAMA0 quiet"; grep -v "^\[ .*\] \( \|CPU\|Hard\|Call\|Kernel Off\|PHYS\|Mem\)" k1_e2$t.log | head -4; done
find r2b -type f -exec ls -l {} +
```

```text
exit=124 lines=25
/init: error while loading shared libraries: libc.so.6: cannot open shared object file: No such file or directory
[    0.627177] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00007f00
[    0.630603] ---[ end Kernel panic - not syncing: Attempted to kill init! exitcode=0x00007f00 ]---
qemu-system-aarch64: terminating on signal 15 from pid 1395843 (timeout)
exit=0 lines=9
K1: hello from /init, pid=1, t=0.765s
K1:   argv[0]=/init
K1:   env HOME=/
K1:   env TERM=linux
exit=0 lines=9
K1: hello from /init, pid=1, t=0.669s
K1:   argv[0]=/init
K1:   env HOME=/
K1:   env TERM=linux
-rwxrwxr-x 1 donh donh   71248 Oct 17 17:40 r2b/init
-rwxr-xr-x 1 donh donh 1788240 Oct 17 17:40 r2b/lib/aarch64-linux-gnu/libc.so.6
-rwxr-xr-x 1 donh donh  201872 Oct 17 17:40 r2b/lib/ld-linux-aarch64.so.1
```

출력에서 볼 것:

- (a) 로더만 있으면 실패의 **종류가 바뀐다.** 이번에는 커널이 아니라 유저 공간의 로더가 `error while loading shared libraries: libc.so.6`을 출력하고 `exit(127)` → `exitcode=0x00007f00`(상위 바이트 0x7f = 127) → "Attempted to kill init!". `0x7f00`을 보면 "init이 로더 단계에서 죽었다"고 바로 읽는다.
- (b)와 (c) 모두 성공했다. 기본 탐색 경로에 `/lib/aarch64-linux-gnu`와 `/lib`가 둘 다 있기 때문이다.
- 동적 init에 필요한 최소 세트 = init 71 KB + libc 1.79 MB + 로더 202 KB ≈ 2.0 MB. 정적 init 하나(714 KB)보다 크다. **프로그램이 하나뿐이면 정적이 작고, 여러 개면 libc를 공유하는 동적이 작다.**

참고 실측: 같은 `k1_init.c`를 musl로 정적 링크하면(`musl-gcc -static -s`) 66,960바이트, cpio 67,584바이트로 부팅된다(실행 확인함, 출력은 예제 1과 같은 `K1: hello from /init, pid=1`). C 라이브러리 선택(glibc·musl·uClibc-ng)은 B2에서 다뤘다.

---

## 4. 장치 노드 — 이름이 아니라 (타입, major, minor)

### 4.1 직관

`/dev/ttyAMA0`은 데이터가 든 파일이 아니다. 파일시스템에 기록된 **inode 하나**인데, 그 inode에는 "문자 장치(c) 또는 블록 장치(b)" 타입과 **major·minor 번호 한 쌍**(`dev_t`)만 들어 있다. `open()`하면 커널은 이름은 보지 않고 그 번호로 **드라이버를 찾는다.** 펌웨어 비유: 레지스터 맵의 "주소"가 major(어느 주변장치 블록), "인덱스"가 minor(그 블록의 몇 번째 채널)다.

- **major**: 어느 드라이버인가. 예: 1 = mem(`/dev/null`, `/dev/zero`), 5 = `/dev/console`·`/dev/tty`, 204 = `ttyAMA`(PL011 UART), 254 = 이 커널에서는 virtblk(동적 할당).
- **minor**: 그 드라이버 안의 어느 인스턴스인가. `/dev/null` = 1:3, `/dev/zero` = 1:5, `ttyAMA0` = 204:64.
- 커널 안에서 `dev_t`는 `MAJOR()`/`MINOR()`/`MKDEV()` 매크로로 다룬다. 문자 드라이버를 쓸 때(G4) `alloc_chrdev_region()`으로 번호를 받고 `cdev_add()`로 `cdev_map`에 등록하는 바로 그 번호다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="18" font-size="13" text-anchor="middle">누가 노드를 만드나 (셋 중 하나 이상)</text>
<rect x="10" y="28" width="210" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="115" y="51" font-size="13" text-anchor="middle">devtmpfs</text>
<text x="115" y="71" font-size="12" text-anchor="middle">device_add() 때 커널이 생성</text>
<rect x="235" y="28" width="210" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="51" font-size="13" text-anchor="middle">정적 /dev (mknod)</text>
<text x="340" y="71" font-size="12" text-anchor="middle">이미지 빌드 때 fakeroot로</text>
<rect x="460" y="28" width="210" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="51" font-size="13" text-anchor="middle">mdev / udev</text>
<text x="565" y="71" font-size="12" text-anchor="middle">uevent 받아 규칙대로 (L2)</text>
<line x1="115" y1="84" x2="250" y2="118" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="340" y1="84" x2="340" y2="118" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="565" y1="84" x2="430" y2="118" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="190" y="120" width="300" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="144" font-size="13" text-anchor="middle">/dev/ttyAMA0 = 파일시스템의 inode 하나</text>
<text x="340" y="166" font-size="12" text-anchor="middle">type c · major 204 · minor 64 · 0600</text>
<line x1="340" y1="180" x2="340" y2="200" stroke="currentColor"/>
<line x1="340" y1="200" x2="115" y2="200" stroke="currentColor"/>
<line x1="115" y1="200" x2="115" y2="218" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="10" y="220" width="210" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="243" font-size="13" text-anchor="middle">open() → chrdev_open()</text>
<text x="115" y="263" font-size="12" text-anchor="middle">i_fop = def_chr_fops</text>
<rect x="235" y="220" width="210" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="243" font-size="13" text-anchor="middle">cdev_map에서 204:64 찾기</text>
<text x="340" y="263" font-size="12" text-anchor="middle">kobj_lookup()</text>
<rect x="460" y="220" width="210" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="243" font-size="13" text-anchor="middle">드라이버 fops로 교체</text>
<text x="565" y="263" font-size="12" text-anchor="middle">pl011 tty의 open/read/write</text>
<line x1="220" y1="248" x2="233" y2="248" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="445" y1="248" x2="458" y2="248" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="340" y="296" font-size="12" text-anchor="middle">이름은 상관없다. 같은 c 204:64면 /tmp/k1_ser도 같은 UART다 (예제 6)</text>
</svg>
```
그림 4 — 장치 노드를 만드는 세 가지 방법과, `open()`이 드라이버까지 가는 길

### 4.2 예제 6 — major/minor는 어디서 오나, 손으로 `mknod`

무엇을 확인하나: `/proc/devices`와 `/sys`가 같은 번호를 보여 주는지, 손으로 만든 노드가 devtmpfs 노드와 똑같이 동작하는지. 랩 initramfs를 복사하고 스크립트를 넣어 `LAB_CMD`로 실행한다(빈 8 MB virtio 디스크도 붙여 블록 장치 번호를 본다).

```sh
#!/bin/sh
# k1_dev.sh: where major/minor numbers come from, and mknod by hand
echo "--- /proc/devices (part)"; grep -E "^ *(1|4|5|204|254) " /proc/devices
echo "--- sysfs knows the numbers too"
cat /sys/class/tty/ttyAMA0/dev /sys/class/mem/null/dev /sys/block/vda/dev 2>&1
ls -l /sys/dev/char/204:64 /sys/dev/char/1:3 | sed 's/.*char\///'
echo "--- devtmpfs node vs hand-made node"
ls -l /dev/ttyAMA0 /dev/null | cat
mknod /tmp/k1_null c 1 3
mknod /tmp/k1_ser c 204 64
mknod /tmp/k1_bad c 1 99
ls -l /tmp/k1_null /tmp/k1_ser /tmp/k1_bad | cat
echo "--- use them"
echo "K1: written through a hand-made node" > /tmp/k1_ser
echo hi > /tmp/k1_null && echo "k1_null swallowed the write, size now $(stat -c %s /tmp/k1_null)"
echo hi > /tmp/k1_bad || echo "open of c 1:99 failed (no driver behind that minor)"
echo "--- the name is irrelevant; only type+major+minor matter"
mv /tmp/k1_null /tmp/whatever; head -c 4 /tmp/whatever | wc -c
```

```sh
cp -a /var/lab/build/rootfs r4 && cp $S/k1_dev.sh r4/ && chmod +x r4/k1_dev.sh
(cd r4 && find . | cpio -o -H newc --quiet | gzip) > k1_r4.cpio.gz && truncate -s 8M k1_blank.img
$S/k1_run.sh e4 60 /var/lab/out/Image k1_r4.cpio.gz "console=ttyAMA0 quiet LAB_CMD=/k1_dev.sh" \
  -drive file=k1_blank.img,format=raw,if=none,id=k1d -device virtio-blk-device,drive=k1d
cat k1_e4.log
```

```text
exit=0 lines=32
lab initramfs: 6.18.55 up 0.61s
--- /proc/devices (part)
  1 mem
  4 /dev/vc/0
  4 tty
  4 ttyS
  5 /dev/tty
  5 /dev/console
  5 /dev/ptmx
204 ttyAMA
254 gpiochip
254 virtblk
--- sysfs knows the numbers too
204:64
1:3
254:0
1:3 -> ../../devices/virtual/mem/null
204:64 -> ../../devices/platform/9000000.pl011/9000000.pl011:0/9000000.pl011:0.0/tty/ttyAMA0
--- devtmpfs node vs hand-made node
crw-rw-rw-    1 0        0           1,   3 Jan  1  1970 /dev/null
crw-------    1 0        0         204,  64 Jan  1  1970 /dev/ttyAMA0
crw-r--r--    1 0        0           1,  99 Oct 18 00:43 /tmp/k1_bad
crw-r--r--    1 0        0           1,   3 Oct 18 00:43 /tmp/k1_null
crw-r--r--    1 0        0         204,  64 Oct 18 00:43 /tmp/k1_ser
--- use them
K1: written through a hand-made node
k1_null swallowed the write, size now 0
/k1_dev.sh: line 16: can't create /tmp/k1_bad: No such device or address
open of c 1:99 failed (no driver behind that minor)
--- the name is irrelevant; only type+major+minor matter
0
[    0.807900] reboot: Power down
```

출력에서 볼 것:

- `/proc/devices`는 **major → 드라이버 이름** 표다. 254가 `gpiochip`과 `virtblk` 두 번 나오는 것은 문자 장치 표와 블록 장치 표가 따로이기 때문이다(앞쪽이 "Character devices:", 뒤쪽이 "Block devices:" 섹션, 위 grep은 제목 줄을 걸렀다).
- `/sys/class/.../dev` 파일과 `/sys/dev/char/<maj>:<min>` 링크가 같은 번호를 준다. **mdev·udev가 노드를 만들 때 읽는 정보가 바로 이것**이다(§5).
- `/tmp/k1_ser`(이름도 위치도 다름)에 쓴 문자열이 콘솔(UART)에 나왔다. 번호만 같으면 같은 장치다. `ls -l`의 크기 칸이 `1,   3`처럼 **major, minor**로 바뀌는 것도 확인한다.
- `c 1:99`는 노드는 만들어지지만 열면 `ENXIO`(No such device or address)다. mem 드라이버의 `memory_open()`이 모르는 minor를 거절한다. 노드 존재 ≠ 드라이버 존재.
- 시간이 `Jan 1 1970`인 노드는 devtmpfs가 만든 것, `Oct 18 00:43`은 손으로 만든 것이다. 게스트에 RTC 기준 시각이 있어 손 노드는 현재 시각(UTC)이 찍혔다.

함정: VM 호스트에서 일반 사용자로 `mknod`하면 `Operation not permitted`다(예제 12에서 본다). 장치 노드 생성에는 `CAP_MKNOD`가 필요하다. 그래서 이미지 빌드에서 정적 `/dev`를 넣으려면 root, fakeroot, 또는 장치 테이블(Buildroot의 `device_table.txt`, `makedevs`)이 필요하다.

---

## 5. `/dev`를 채우는 네 가지 방법, 그리고 `/dev/console`

### 5.1 네 가지 방법 비교

| 방법 | 누가 만드나 | 언제 | 장점 | 단점 |
|---|---|---|---|---|
| 정적 `/dev` | 이미지 빌드가 `mknod` | 빌드 때 | 커널 기능 불필요, 가장 단순 | 장치 추가·핫플러그에 무력, 번호가 바뀌면 깨짐 |
| devtmpfs | 커널 (`device_add()` → `kdevtmpfs` 스레드) | 드라이버가 장치를 등록하는 순간 | 유저 공간 없이 항상 정확, 부팅 초기부터 존재 | 이름·권한 정책이 기본값 (대부분 root 0600) |
| mdev (BusyBox) | 유저 공간, uevent마다 실행 (`/proc/sys/kernel/hotplug` 또는 `mdev -d`) | uevent 시 | 작다, `/etc/mdev.conf`로 권한·심볼릭 링크 | 규칙 표현력이 udev보다 약함 |
| udev (systemd-udevd) | 유저 공간 데몬, netlink uevent | uevent 시 | 규칙·고정 이름·권한·펌웨어 로드 트리거 | 무겁다, systemd와 묶여 있음 (L2) |

현대 임베디드의 표준은 **"devtmpfs가 바닥을 깔고, mdev 또는 udev가 그 위에서 권한·심볼릭 링크·모듈 로드를 조정"**하는 조합이다. `CONFIG_DEVTMPFS=y`는 거의 필수이고, `CONFIG_DEVTMPFS_MOUNT=y`면 커널이 `root=`로 마운트한 루트의 `/dev`에 devtmpfs를 자동으로 붙인다. 주의: **initramfs에는 자동 마운트되지 않는다.** `prepare_namespace()`의 끝에서 `devtmpfs_mount()`를 부르는데, initramfs 경로는 `prepare_namespace()`를 지나지 않는다. 그래서 랩 initramfs의 `/init`은 `mount -t devtmpfs devtmpfs /dev`를 직접 한다. 카메라가 `/dev/video0`이었다 `video1`이었다 하는 문제처럼 **고정 이름이 필요하면 udev 규칙**이고, 그건 L2와 G9에서 다룬다.

### 5.2 `/dev/console`이 특별한 이유

커널은 init을 실행하기 직전 `console_on_rootfs()`에서 **`/dev/console`을 열어 fd 0, 1, 2로 복제**한다. 이 세 fd가 PID 1의 stdin/stdout/stderr가 되고, fork된 모든 자식이 물려받는다.

```sh
sed -n 1510,1522p /var/lab/src/linux-6.18.55/init/main.c
```

```text
void __init console_on_rootfs(void)
{
	struct file *file = filp_open("/dev/console", O_RDWR, 0);

	if (IS_ERR(file)) {
		pr_err("Warning: unable to open an initial console.\n");
		return;
	}
	init_dup(file);
	init_dup(file);
	init_dup(file);
	fput(file);
}
```

중요한 순서: `kernel_init_freeable()`에서 `console_on_rootfs()`는 **initramfs를 다 푼 직후, `root=` 장치를 마운트하기 전**에 불린다(1557~1570행: `wait_for_initramfs()` → `console_on_rootfs()` → rdinit 검사 → `prepare_namespace()`). 그래서 `root=/dev/vda`로 부팅해도 콘솔은 **ext4 디스크가 아니라 메모리 rootfs(기본 initramfs)의 `/dev/console`**로 열린다. ext4 이미지에 `/dev/console`이 없어도 셸이 뜨는 이유다(예제 10에서 확인).

### 5.3 예제 7 — 기본 initramfs의 `/dev/console`을 지우면

무엇을 확인하나: "Warning: unable to open an initial console"을 실제로 만들고, 그때 init의 fd 상태가 어떤지.

기본 initramfs의 `/dev/console`을 없애는 방법: cpio 압축 해제 코드(`init/initramfs.c`의 `clean_path()`)는 **같은 이름이 다른 타입으로 오면 기존 것을 지운다.** 그래서 내 cpio에 `dev/console`을 **디렉터리**로 넣으면 커널 기본 노드가 지워지고, 디렉터리는 `O_RDWR`로 열 수 없으니 콘솔 열기가 실패한다. (실무에서 이 상황은 `CONFIG_INITRAMFS_SOURCE`에 `/dev/console` 없는 디렉터리를 지정해 커널에 initramfs를 내장할 때 생긴다.)

```sh
mkdir -p r3/dev/console; cp k1_init_static r3/init; (cd r3 && find . | cpio -o -H newc --quiet) > k1_r3.cpio
cpio -itv < k1_r3.cpio
$S/k1_run.sh e3nocon 40 /var/lab/out/Image k1_r3.cpio "console=ttyAMA0"
sed -n 245,252p k1_e3nocon.log
```

```text
drwxrwxr-x   3 donh     donh            0 Oct 17 17:41 .
drwxrwxr-x   3 donh     donh            0 Oct 17 17:41 dev
drwxrwxr-x   2 donh     donh            0 Oct 17 17:41 dev/console
-rwxrwxr-x   1 donh     donh       713936 Oct 17 17:41 init
1396 blocks
exit=0 lines=259
[    0.672381] Warning: unable to open an initial console.
[    0.709569] Freeing unused kernel memory: 3264K
[    0.710187] Run /init as init process
K1: fd 1 was not open (errno=9 Bad file descriptor); mounted devtmpfs, reopened /dev/ttyAMA0
K1: hello from /init, pid=1, t=0.695s
K1:   argv[0]=/init
K1:   env HOME=/
K1:   env TERM=linux
```

출력에서 볼 것:

- 경고가 정확히 그 문구로 나왔다. 그리고 init의 `fcntl(1, F_GETFD)`가 `EBADF` — **fd 1이 아예 열려 있지 않다.** 보통 프로그램이라면 `printf`가 조용히 실패하고 화면에는 아무것도 안 나온다. "커널 로그는 다 나오는데 init 이후 아무 출력이 없다"의 대표 원인이다.
- `k1_init`은 자구책으로 devtmpfs를 `/dev`에 마운트하고 `/dev/ttyAMA0`을 직접 열어 0/1/2에 `dup2`했다. 그래서 그 뒤 출력은 보인다. BusyBox init은 환경 변수 `CONSOLE`(커널 커맨드라인에 `CONSOLE=/dev/ttyAMA0`을 주면 init의 환경 변수가 된다)이 있으면 그 장치를 열어 0/1/2로 쓰고, 없으면 닫힌 fd를 `/dev/null`로 채울 뿐이다(BusyBox 1.37 `init/init.c`의 `console_init()`). 즉 콘솔이 없으면 BusyBox 시스템도 조용해진다. 셸 스크립트 `/init`이라면 devtmpfs를 먼저 마운트한 뒤 `exec </dev/console >/dev/console 2>&1` 같은 재지정을 두는 방법도 있다.
- `t=0.695s`가 printk의 `0.710187`보다 작은 것은 `CLOCK_BOOTTIME`과 printk 시계(`local_clock()`)의 기준이 조금 다르기 때문이다. 같은 축으로 비교하지 않는다.

### 5.4 예제 8 — 정말로 fd 0/1/2가 비어 있는지 확인

무엇을 확인하나: 콘솔이 없을 때 PID 1이 받는 fd를 `/proc/self/fd`로 직접 본다. `/dev`에 콘솔이 없으니 결과는 `/dev/kmsg`(커널 로그)에 쓴다 — devtmpfs를 `/mnt`에 따로 붙여서 `kmsg` 노드를 얻는다.

```c
/* k1_fdcheck.c: as PID 1, report which of fd 0/1/2 the kernel opened for us.
 * /dev has no console node, so we mount a private devtmpfs on /mnt just to reach /mnt/kmsg. */
#include <fcntl.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <unistd.h>

int main(void)
{
    if (mount("devtmpfs", "/mnt", "devtmpfs", 0, NULL) < 0) return 10;
    if (mount("proc", "/proc", "proc", 0, NULL) < 0) return 11;
    int k = open("/mnt/kmsg", O_WRONLY);           /* lowest free fd -> tells us 0 was free */
    if (k < 0) return 12;
    for (int i = 0; i < 3; i++) {
        char path[32], target[128] = "";
        snprintf(path, sizeof path, "/proc/self/fd/%d", i);
        ssize_t n = readlink(path, target, sizeof target - 1);
        if (n > 0) target[n] = '\0';
        dprintf(k, "K1fd: fd%d %s %s\n", i, fcntl(i, F_GETFD) < 0 ? "CLOSED" : "open ->", target);
    }
    sync();
    reboot(RB_POWER_OFF);
    return 1;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static $S/k1_fdcheck.c -o k1_fdcheck
mkdir -p rd/dev/console rd/proc rd/mnt && cp k1_fdcheck rd/init && (cd rd && find . | cpio -o -H newc --quiet) > k1_rd.cpio
$S/k1_run.sh e3fd 40 /var/lab/out/Image k1_rd.cpio "console=ttyAMA0" && grep -n "K1fd\|Warning" k1_e3fd.log
```

```text
exit=0 lines=253
245:[    0.526545] Warning: unable to open an initial console.
248:[    0.589538] K1fd: fd0 open -> /mnt/kmsg
249:[    0.590017] K1fd: fd1 CLOSED 
250:[    0.590169] K1fd: fd2 CLOSED 
```

출력에서 볼 것: `open()`은 **가장 작은 빈 fd 번호**를 준다. `kmsg`가 fd 0을 받았다는 것 자체가 "fd 0이 비어 있었다"는 증거다. fd 1, 2도 닫혀 있다. 커널이 init에게 fd를 주는 유일한 통로가 `/dev/console`이라는 뜻이다. 그리고 `dprintf(k, ...)`로 `/dev/kmsg`에 쓴 줄이 printk 타임스탬프를 달고 나오는 것도 볼 것 — PID 1의 디버그 출력을 콘솔과 무관하게 남기는 실용적인 방법이다.

---

## 6. init이 마운트하는 가상 FS와 `/etc` 필수 파일

### 6.1 가상 파일시스템 — 이미지에는 빈 디렉터리만

| 마운트 지점 | FS 타입 | 없으면 깨지는 것 | 누가 마운트 |
|---|---|---|---|
| `/proc` | proc | `ps`, `top`, `free`, `mount`(목록), `/proc/cmdline` 읽기 | init 스크립트 (rcS) 또는 systemd |
| `/sys` | sysfs | mdev/udev, `/sys/class/gpio`, 대부분의 장치 정보 | 동일 |
| `/dev` | devtmpfs | 장치 노드 전부 | `root=` 경로면 커널(`DEVTMPFS_MOUNT`), initramfs면 `/init` |
| `/dev/pts` | devpts | ssh·telnet·`script` 같은 pseudo terminal | init 스크립트 |
| `/run` | tmpfs | PID 파일, 소켓, systemd 상태 | init |
| `/tmp` | tmpfs | 임시 파일 (flash에 쓰면 마모) | init 또는 fstab |
| `/sys/kernel/debug` | debugfs | 디버그 노드 (양산 이미지는 끈다) | 필요할 때 |

C7 §1.4의 최소 PID 1과 예제 9의 rcS가 하는 첫 일이 이 마운트다. 커널은 `root=` 경로에서 `/dev`만 대신 붙여 줄 뿐, **`/proc`과 `/sys`는 언제나 유저 공간의 몫**이다.

### 6.2 `/etc` 필수 파일 — 무엇이 누구에게 읽히나

| 파일 | 읽는 쪽 | 없으면 |
|---|---|---|
| `/etc/inittab` | BusyBox init (sysvinit 형식과 다름) | BusyBox는 내장 기본값(rcS 실행 + 콘솔 셸 등)을 쓴다 (K2) |
| `/etc/init.d/rcS` | inittab의 `::sysinit:` | 마운트·hostname 등 초기화가 안 됨 |
| `/etc/fstab` | `mount -a`, `fsck`, systemd의 fstab generator | `mount -a`가 아무것도 안 함 |
| `/etc/passwd` | `ls -l`의 이름 표시, `login`, `id`, `su` | 이름 대신 숫자 uid, 로그인 불가 |
| `/etc/group` | 그룹 이름, 그룹 권한 확인 | 숫자 gid |
| `/etc/shadow` | `login`, `passwd` (권한 0600, root 소유) | 비밀번호 로그인 불가 |
| `/etc/hostname` | rcS의 `hostname -F`, systemd | 커널 기본 호스트명 (`CONFIG_DEFAULT_HOSTNAME`, 기본 `(none)`) |
| `/etc/hosts` | libc resolver (`localhost` 해석) | `localhost`가 안 풀림 |
| `/etc/resolv.conf` | libc DNS resolver | DNS 실패 — 보통 `/run`의 파일을 가리키는 링크로 둔다 |
| `/etc/profile` | 로그인 셸 | `PATH`, 프롬프트 기본값 |
| `/etc/ld.so.cache` | 동적 로더 | 기본 탐색 경로로 동작 (임베디드에선 흔히 없음) |

`resolv.conf`처럼 **런타임에 바뀌는 파일이 읽기 전용 `/etc`에 있으면** DHCP 클라이언트가 쓸 수 없다. 그래서 `/etc/resolv.conf -> /run/resolv.conf` 같은 링크로 빼는 것이 J3의 "RO root" 설계의 기본기다.

---

## 7. 손으로 만드는 rootfs — BusyBox + `/etc` + 모듈 → ext4 → `root=/dev/vda`

### 7.1 무엇을 넣나

랩 initramfs(BusyBox 1.37 정적, 2.3 MB)를 출발점으로, 다음을 더한다.

- glibc 로더 + libc(동적 앱 하나를 돌리기 위해)
- 커널 모듈 3개(`fuse`, `cuse`, `null_blk`)와 `depmod`가 만든 인덱스 — `Image-fs`(6.18.55-fs)용
- `/etc` 일체(inittab, fstab, passwd, group, shadow, hostname, hosts, resolv.conf, profile, rcS)
- FHS 빈 디렉터리(`/run`, `/var/log`, `/home/k1user`, `/lib/firmware`)

BusyBox의 applet 링크·inittab 형식·mdev 자체는 K2의 주제라 여기서는 쓰기만 한다.

### 7.2 예제 9 — rootfs 디렉터리 만들기 (sudo 없음)

무엇을 확인하나: "부팅해서 셸이 뜨는 시스템"에 필요한 파일을 손으로 다 모으면 얼마나 되나.

```sh
#!/bin/bash
# k1_mkrootfs.sh <outdir>: BusyBox + glibc loader/libc + 3 modules + /etc, built by hand
set -e
D=$1; KV=6.18.55-fs; M=/var/lab/out/modules-fs/lib/modules/$KV
rm -rf "$D"; cp -a /var/lab/build/rootfs "$D"; rm -f "$D/init" "$D/linuxrc"
mkdir -p "$D"/{etc/init.d,run,var/log,home/k1user,lib/firmware,lib/aarch64-linux-gnu,usr/local/bin}
cp -L /lib/ld-linux-aarch64.so.1 "$D/lib/"                       # PT_INTERP path
cp -L /lib/aarch64-linux-gnu/libc.so.6 "$D/lib/aarch64-linux-gnu/" # NEEDED libc.so.6
cp /var/tmp/K1/k1_hello "$D/usr/local/bin/"                     # a dynamic app
for k in kernel/fs/fuse/fuse.ko kernel/fs/fuse/cuse.ko kernel/drivers/block/null_blk/null_blk.ko; do
  install -D -m 644 "$M/$k" "$D/lib/modules/$KV/$k"
done
cp "$M"/modules.builtin* "$M"/modules.order "$D/lib/modules/$KV/"     # what is built in (=y)
depmod -b "$D" "$KV"                                              # -> modules.dep etc.
cd "$D/etc"
cat > inittab <<'T'
::sysinit:/etc/init.d/rcS
::respawn:-/bin/sh
::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a -r
T
cat > fstab <<'T'
# <dev>  <mnt>     <type>  <opts>          <dump> <pass>
/dev/root /        ext4    rw,noatime      0 1
proc     /proc     proc    defaults        0 0
sysfs    /sys      sysfs   defaults        0 0
tmpfs    /run      tmpfs   mode=0755,nosuid,nodev 0 0
tmpfs    /tmp      tmpfs   mode=1777       0 0
devpts   /dev/pts  devpts  gid=5,mode=620  0 0
T
printf 'root:x:0:0:root:/root:/bin/sh\nk1user:x:1000:1000:K1 user:/home/k1user:/bin/sh\n' > passwd
printf 'root:x:0:\ntty:x:5:\nk1user:x:1000:\n' > group
printf 'root::19000:0:99999:7:::\nk1user:!:19000:0:99999:7:::\n' > shadow
chmod 600 shadow
echo k1-sensorkit > hostname
printf '127.0.0.1\tlocalhost\n127.0.1.1\tk1-sensorkit\n' > hosts
echo 'nameserver 10.0.2.3' > resolv.conf
printf 'export PATH=/sbin:/usr/sbin:/bin:/usr/bin:/usr/local/bin\nexport PS1="\\h:\\w# "\n' > profile
cat > init.d/rcS <<'T'
#!/bin/sh
mount -t proc proc /proc
mount -o remount,rw /
mkdir -p /dev/pts && mount -a
hostname -F /etc/hostname
echo "rcS: up $(cut -d' ' -f1 /proc/uptime)s"
grep -q K1_AUTO /proc/cmdline && { /etc/k1_report.sh; sync; poweroff -f; }
T
chmod 755 init.d/rcS
```

몇 가지 설계 메모:

- `/init`과 `/linuxrc`를 지웠다. 이 rootfs는 initramfs가 아니라 `root=`로 붙일 디스크다. 커널은 `init=`이 없으면 `/sbin/init`(BusyBox init)을 실행한다.
- `root::` — **root 비밀번호가 비어 있다.** 개발용으로만 쓰는 설정이다. 양산 이미지는 root 로그인을 막고(`root:!:...` 또는 `*`) 디버그 콘솔 자체를 막는다(L6).
- 모듈은 `/lib/modules/<커널 릴리스>/` 아래에 두고 `depmod -b <rootdir> <KV>`로 `modules.dep` 등을 만든다. `modules.builtin`을 같이 넣는 이유: `modprobe`가 "이건 커널에 =y로 들어 있다"를 알아야 없는 `.ko`를 찾다가 실패하지 않는다.
- `k1_report.sh`는 게스트에서 상태를 찍고 끄는 테스트용 스크립트다. `K1_AUTO`가 커맨드라인에 없으면 rcS는 그냥 끝나고 inittab의 `::respawn:-/bin/sh`로 콘솔 셸이 뜬다.

```sh
#!/bin/sh
# runs inside the guest from rcS when K1_AUTO is on the cmdline
echo "--- whoami / hostname / PID 1"; id; hostname; cat /proc/1/cmdline | tr '\0' ' '; echo
echo "--- mounts"; cut -d' ' -f1-4 /proc/mounts
echo "--- /dev (first 8 of $(ls /dev | wc -l))"; ls /dev | head -8 | tr '\n' ' '; echo
echo "--- ownership as the guest sees it"; ls -ln / | sed -n '2,6p'; ls -l /etc/shadow /bin/busybox | cat
echo "--- dynamic app"; /usr/local/bin/k1_hello 2>&1 | head -1
echo "--- modules for $(uname -r)"; modprobe cuse && lsmod | cat
echo "--- /dev/null sanity"; echo test > /dev/null; ls -l /dev/null | cat
```

동적 앱 `k1_hello.c`는 pid·uid·glibc 버전을 한 줄 찍는 10줄짜리 프로그램이다.

```c
/* k1_hello.c: a normal dynamically-linked app for the hand-made rootfs */
#include <gnu/libc-version.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("k1_hello: pid=%d uid=%d glibc %s\n", getpid(), getuid(), gnu_get_libc_version());
    return 0;
}
```

### 7.3 ext4 이미지 만들기 — 마운트 없이 `mkfs.ext4 -d`

`mkfs.ext4 -d <dir>`는 **디렉터리 내용을 새 파일시스템에 복사하면서 만든다**(e2fsprogs 1.43+). 루프 마운트도 sudo도 필요 없어 빌드 시스템이 쓰는 방식이다(Buildroot·Yocto의 ext4 이미지 생성이 이 옵션을 쓴다).

```sh
gcc -std=gnu11 -Wall -Wextra -O2 $S/k1_hello.c -o k1_hello
$S/k1_mkrootfs.sh /var/tmp/K1/k1_root; install -m755 $S/k1_report.sh k1_root/etc/
cat k1_root/lib/modules/6.18.55-fs/modules.dep
truncate -s 16M k1_root.ext4 && mkfs.ext4 -q -F -L k1root -d k1_root k1_root.ext4 && ls -l k1_root.ext4
```

```text
kernel/fs/fuse/fuse.ko:
kernel/fs/fuse/cuse.ko: kernel/fs/fuse/fuse.ko
kernel/drivers/block/null_blk/null_blk.ko:
-rw-rw-r-- 1 donh donh 16777216 Oct 17 17:53 k1_root.ext4
```

`modules.dep`의 한 줄은 "모듈: 먼저 올려야 할 의존 모듈들"이다. `cuse`는 `fuse`의 심볼을 쓴다.

### 7.4 부팅 — `root=/dev/vda rootwait`

```sh
hd(){ echo "-drive file=$1,format=raw,if=none,id=k1hd -device virtio-blk-device,drive=k1hd"; }
$S/k1_run.sh e9 90 /var/lab/out/Image-fs - "console=ttyAMA0 root=/dev/vda rootwait K1_AUTO" $(hd k1_root.ext4)
grep -nE "Kernel command line|Unknown kernel|virtio_blk virtio0: \[vda\]|check access|EXT4-fs \(vda\)|VFS: Mounted|devtmpfs: mounted|Freeing unused|Run /sbin" k1_e9.log
sed -n "/rcS: up/,\$p" k1_e9.log | grep -v "Unknown symbol"
```

```text
exit=0 lines=318
36:[    0.000000] Kernel command line: console=ttyAMA0 root=/dev/vda rootwait K1_AUTO
37:[    0.000000] Unknown kernel command line parameters "K1_AUTO", will be passed to user space.
198:[    0.986731] virtio_blk virtio0: [vda] 32768 512-byte logical blocks (16.8 MB/16.0 MiB)
248:[    1.128576] check access for rdinit=/init failed: -2, ignoring
249:[    1.153239] EXT4-fs (vda): mounted filesystem f8d44cd4-6014-425a-8423-48c852d02391 ro with ordered data mode. Quota mode: none.
250:[    1.153534] VFS: Mounted root (ext4 filesystem) readonly on device 254:0.
251:[    1.154788] devtmpfs: mounted
252:[    1.190115] Freeing unused kernel memory: 3456K
253:[    1.190660] Run /sbin/init as init process
rcS: up 1.27s
--- whoami / hostname / PID 1
uid=0(root) gid=0(root)
k1-sensorkit
init               
--- mounts
/dev/root / ext4 rw,relatime
devtmpfs /dev devtmpfs rw,relatime,size=216432k,nr_inodes=54108,mode=755
proc /proc proc rw,relatime
sysfs /sys sysfs rw,relatime
tmpfs /run tmpfs rw,nosuid,nodev,relatime,mode=755
tmpfs /tmp tmpfs rw,relatime
devpts /dev/pts devpts rw,relatime,gid=5,mode=620,ptmxmode=000
--- /dev (first 8 of 141)
autofs console cpu_dma_latency full gpiochip0 hwrng input kmsg 
--- ownership as the guest sees it
drwxrwxr-x    2 501      1000          4096 Oct 10 17:06 bin
drwxr-xr-x    6 0        0             2860 Oct 18 00:53 dev
drwxrwxr-x    3 501      1000          4096 Oct 18 00:53 etc
drwxrwxr-x    3 501      1000          4096 Oct 18 00:53 home
drwxrwxr-x    5 501      1000          4096 Oct 18 00:53 lib
-rwxr-xr-x    1 501      k1user     2242744 Oct 10 17:06 /bin/busybox
-rw-------    1 501      k1user          53 Oct 18 00:53 /etc/shadow
--- dynamic app
k1_hello: pid=81 uid=0 glibc 2.43
--- modules for 6.18.55-fs
[    1.559103] fuse: init (API version 7.45)
cuse 16384 0 - Live 0xffffa44d0aaa1000
fuse 180224 1 cuse, Live 0xffffa44d0aa61000
--- /dev/null sanity
crw-rw-rw-    1 root     root        1,   3 Jan  1  1970 /dev/null
[    1.623205] Flash device refused suspend due to active operation (state 20)
[    1.623299] Flash device refused suspend due to active operation (state 20)
[    1.624594] reboot: Power down
```

출력에서 볼 것:

- **부팅해서 셸까지에 필요한 것이 전부 동작했다**: BusyBox init(PID 1의 `cmdline`이 `init`) → rcS가 `/proc` 마운트, `remount,rw`, `mount -a`(fstab), hostname 설정 → 동적 앱(로더 + libc) 실행 → `modprobe`.
- `/proc/mounts`의 루트가 `rw,relatime`이다. fstab의 `noatime`이 적용되지 않은 이유: BusyBox `mount -a`는 **이미 마운트된 `/`를 건너뛰고**, rcS의 `mount -o remount,rw /`는 `rw`만 바꿨다. fstab의 루트 옵션을 쓰려면 `mount -o remount /`처럼 옵션 없이 다시 마운트해 fstab을 읽게 하거나, 커맨드라인 `rootflags=noatime`을 쓴다.
- `/dev`에 노드 141개 — 이미지의 `/dev`는 빈 디렉터리였는데 devtmpfs가 다 채웠다(`dev` 디렉터리 소유자가 0:0인 것도 devtmpfs가 덮었기 때문).
- **소유자가 501:1000이다.** `mkfs.ext4 -d`는 빌드한 사람(VM의 donh, uid 501, gid 1000)의 소유권을 그대로 복사했다. 게다가 gid 1000이 이 rootfs의 `/etc/group`에서는 `k1user`라서 `ls -l`에 `501 k1user`로 보인다. 빌드 PC의 그룹이 기기의 사용자 그룹이 된 것이다. §9에서 고친다.
- `lsmod`: `cuse`가 `fuse`를 쓰고 있다(`fuse ... 1 cuse`). `modprobe cuse` 한 번으로 의존 모듈까지 올라왔다.
- grep에서 뺀 30줄의 `cuse: Unknown symbol fuse_... (err -2)`는 이 BusyBox가 `CONFIG_MODPROBE_SMALL=y`로 빌드됐기 때문이다. modprobe-small은 depmod의 `modules.dep`가 아니라 자기 형식 `modules.dep.bb`를 찾고, 없으면 모듈 디렉터리를 훑으며 **이름이 맞는 모듈부터 일단 `init_module()`을 시도**한다(`modutils/modprobe-small.c`의 `fileAction()`). 의존 모듈이 아직 없으니 처음 시도가 실패하고, 그다음 의존성을 처리해 성공한다. 결과는 같지만 로그가 지저분하다. kmod의 `modprobe`나 BusyBox의 일반 `modprobe`(MODPROBE_SMALL=n)는 `modules.dep` 순서대로 올린다(K2).
- "Flash device refused suspend"는 `Image-fs`에 내장된 CFI NOR(physmap) 드라이버가 poweroff 때 찍는 메시지로 보이며, rootfs와 무관하다.

### 7.5 예제 10 — 같은 이미지, 다른 조건 두 가지

무엇을 확인하나: (a) 다른 커널(`Image`, 릴리스 `6.18.55`)로 부팅하면 모듈이 어떻게 되나, (b) devtmpfs 자동 마운트를 끄면 무엇이 깨지나. 매번 원본 이미지 사본을 쓴다(rw로 마운트되면 이미지가 바뀌므로).

```sh
cp k1_root_pristine.ext4 k1_e10img.ext4; cp k1_root_pristine.ext4 k1_e10nodev.ext4
$S/k1_run.sh e10img 90 /var/lab/out/Image - "console=ttyAMA0 root=/dev/vda rootwait K1_AUTO quiet" $(hd k1_e10img.ext4)
sed -n "/dynamic app/,\$p" k1_e10img.log
$S/k1_run.sh e10nodev 90 /var/lab/out/Image-fs - "console=ttyAMA0 root=/dev/vda rootwait K1_AUTO quiet devtmpfs.mount=0" $(hd k1_e10nodev.ext4)
sed -n "/mounts/,/ownership/p;/null sanity/,\$p" k1_e10nodev.log
```

```text
exit=0 lines=30
--- dynamic app
k1_hello: pid=79 uid=0 glibc 2.43
--- modules for 6.18.55
modprobe: can't change directory to '6.18.55': No such file or directory
--- /dev/null sanity
crw-rw-rw-    1 root     root        1,   3 Jan  1  1970 /dev/null
[    0.905024] reboot: Power down
exit=0 lines=30
--- mounts
/dev/root / ext4 rw,relatime
proc /proc proc rw,relatime
sysfs /sys sysfs rw,relatime
tmpfs /run tmpfs rw,nosuid,nodev,relatime,mode=755
tmpfs /tmp tmpfs rw,relatime
devpts /dev/pts devpts rw,relatime,gid=5,mode=620,ptmxmode=000
--- /dev (first 8 of 1)
pts 
--- ownership as the guest sees it
--- /dev/null sanity
-rw-r--r--    1 root     root             5 Oct 18 00:53 /dev/null
[    1.582475] reboot: Power down
```

출력에서 볼 것:

- (a) `modprobe`는 **`/lib/modules/$(uname -r)`**로 `chdir`한다. 커널 릴리스 문자열(`6.18.55`)과 디렉터리 이름(`6.18.55-fs`)이 한 글자라도 다르면 모듈은 없는 것과 같다. 커널을 `LOCALVERSION`만 바꿔 다시 빌드했는데 rootfs의 모듈을 안 바꿔서 생기는 고전적인 사고다. 이름을 맞춰도 vermagic이 다르면 `insmod`가 거절한다(G 모듈 노트).
- (b) `devtmpfs.mount=0`이면 `/dev`에는 `mount -a`가 만든 `pts`뿐이다. 그런데도 **셸 스크립트는 콘솔에 출력했다** — §5.2대로 fd 0/1/2는 ext4를 마운트하기 전에 메모리 rootfs의 `/dev/console`로 열렸기 때문이다.
- (b)의 진짜 사고는 마지막 줄이다: `echo test > /dev/null`이 **5바이트짜리 일반 파일 `/dev/null`을 만들었다.** 이후 모든 `>/dev/null`이 rootfs(flash)에 쓰기가 되고, 다른 프로그램이 `/dev/null`을 읽으면 "test"가 나온다. 장치 노드가 없는 `/dev`는 조용히 망가진다.

---

## 8. PID 1의 규칙

C7 §1이 PID 1을 자세히 다뤘다. 여기서는 rootfs 설계자가 알아야 할 규칙만 정리하고, 좀비 회수 하나를 수치로 본다.

| 규칙 | 이유 | 어기면 |
|---|---|---|
| 절대 exit하지 않는다 | 커널은 init이 없는 세상을 가정하지 않는다 | `Attempted to kill init! exitcode=0x...` (예제 2, 5) |
| 끝낼 때는 `reboot(2)` | `sync` 후 `RB_POWER_OFF`/`RB_AUTOBOOT` | exit로 끝내면 panic |
| 자식·고아를 `wait()`로 회수 | 부모가 먼저 죽은 프로세스는 PID 1에 입양된다 | 좀비가 쌓여 PID 고갈 (예제 11) |
| 시그널은 핸들러로만 받는다 | 커널이 PID 1에게 기본 동작 시그널을 전달하지 않는다 (`SIGNAL_UNKILLABLE`) | `kill -TERM 1`이 아무 일도 안 함 (C7 §1.5) |
| fd 0/1/2는 `/dev/console`에서 온다 | 커널이 init 직전에 열어 준다 | 출력이 사라짐 (예제 7, 8) |

### 8.1 예제 11 — 회수하는 init과 안 하는 init

무엇을 확인하나: 짧게 사는 자식 20개와, 이중 fork(daemon 방식)로 고아가 된 손자 1개를 만든 PID 1이 `wait()`를 할 때와 안 할 때 좀비 수.

```c
/* k1_reaper.c: PID 1 that does (K1_REAP=1) or does not (K1_REAP=0) reap its children */
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t reaped;
static void on_chld(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) reaped++;   /* collect every dead child */
}

static int count_zombies(void)
{
    int z = 0; DIR *d = opendir("/proc"); struct dirent *e;
    if (!d) { perror("opendir /proc"); exit(1); }
    while ((e = readdir(d))) {
        char p[300], st = 0; FILE *f;
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        snprintf(p, sizeof p, "/proc/%s/stat", e->d_name);
        if ((f = fopen(p, "r"))) { if (fscanf(f, "%*d %*s %c", &st) == 1 && st == 'Z') z++; fclose(f); }
    }
    closedir(d);
    return z;
}

int main(void)
{
    int reap = getenv("K1_REAP") && atoi(getenv("K1_REAP"));
    if (mount("proc", "/proc", "proc", 0, NULL) < 0) { perror("mount"); return 1; }
    if (reap) {
        struct sigaction sa = { .sa_handler = on_chld, .sa_flags = SA_RESTART | SA_NOCLDSTOP };
        sigaction(SIGCHLD, &sa, NULL);
    }
    for (int i = 0; i < 20; i++)                 /* 20 short-lived children (e.g. "sh -c" helpers) */
        if (fork() == 0) _exit(0);
    if (fork() == 0) {                           /* a daemon that double-forks: grandchild -> orphan */
        if (fork() == 0) { sleep(1); _exit(0); }
        _exit(0);
    }
    for (int t = 0; t < 3; t++) {
        sleep(1);                                /* SIGCHLD interrupts sleep; good enough for a demo */
        printf("K1 reap=%d t=%ds zombies=%d reaped=%d\n", reap, t + 1, count_zombies(), (int)reaped);
    }
    fflush(stdout);
    sync();
    reboot(RB_POWER_OFF);
    return 1;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static $S/k1_reaper.c -o k1_reaper
mkdir -p r6/proc && cp k1_reaper r6/init && (cd r6 && find . | cpio -o -H newc --quiet) > k1_r6.cpio
for m in 0 1; do $S/k1_run.sh e6r$m 60 /var/lab/out/Image k1_r6.cpio "console=ttyAMA0 quiet K1_REAP=$m"; cat k1_e6r$m.log; done
```

```text
exit=0 lines=4
K1 reap=0 t=1s zombies=22 reaped=0
K1 reap=0 t=2s zombies=22 reaped=0
K1 reap=0 t=3s zombies=22 reaped=0
[    3.758731] reboot: Power down
exit=0 lines=4
K1 reap=1 t=1s zombies=0 reaped=21
K1 reap=1 t=2s zombies=0 reaped=22
K1 reap=1 t=3s zombies=0 reaped=22
[    2.705923] reboot: Power down
```

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="20" font-size="13">예제 11: 자식 20개 + 이중 fork 고아 1개를 만든 PID 1, 1초마다 /proc의 좀비 수</text>
<line x1="80" y1="220" x2="640" y2="220" stroke="currentColor"/>
<line x1="80" y1="220" x2="80" y2="60" stroke="currentColor"/>
<text x="72" y="224.0" font-size="12" text-anchor="end">0</text>
<text x="72" y="164.0" font-size="12" text-anchor="end">10</text>
<line x1="80" y1="160.0" x2="640" y2="160.0" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3 3"/>
<text x="72" y="104.0" font-size="12" text-anchor="end">20</text>
<line x1="80" y1="100.0" x2="640" y2="100.0" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3 3"/>
<rect x="120" y="88.0" width="50" height="132.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
<text x="145" y="82.0" font-size="12" text-anchor="middle">22</text>
<rect x="180" y="219" width="50" height="1" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="205" y="212" font-size="12" text-anchor="middle">0</text>
<text x="175" y="238" font-size="12" text-anchor="middle">t = 1 s</text>
<rect x="300" y="88.0" width="50" height="132.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
<text x="325" y="82.0" font-size="12" text-anchor="middle">22</text>
<rect x="360" y="219" width="50" height="1" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="385" y="212" font-size="12" text-anchor="middle">0</text>
<text x="355" y="238" font-size="12" text-anchor="middle">t = 2 s</text>
<rect x="480" y="88.0" width="50" height="132.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
<text x="505" y="82.0" font-size="12" text-anchor="middle">22</text>
<rect x="540" y="219" width="50" height="1" fill="#3f9a6b" stroke="#3f9a6b"/>
<text x="565" y="212" font-size="12" text-anchor="middle">0</text>
<text x="535" y="238" font-size="12" text-anchor="middle">t = 3 s</text>
<rect x="440" y="36" width="12" height="12" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
<text x="458" y="47" font-size="12">K1_REAP=0 (wait 안 함)</text>
<rect x="440" y="56" width="12" height="12" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<text x="458" y="67" font-size="12">K1_REAP=1 (SIGCHLD + waitpid)</text>
</svg>
```
그림 5 — 예제 11 측정값: 회수하지 않는 PID 1은 좀비를 영원히 들고 있다

출력에서 볼 것:

- 좀비 22개 = 자식 20 + 이중 fork의 중간 프로세스 1 + **입양된 손자 1**. 손자는 PID 1이 만들지 않았는데도 PID 1의 좀비가 됐다(`find_new_reaper()`가 새 부모로 init을 고른다).
- reap 모드의 t=1에서 `reaped=21`: 손자는 아직 `sleep(1)` 중이었다. t=2에서 22가 된다.
- reap 모드가 1초 일찍 끝난 것(2.7초 vs 3.8초)은 SIGCHLD가 `sleep()`을 깨웠기 때문이다(주석대로 데모 수준의 타이밍이다).
- 셸 스크립트를 PID 1로 쓰면(예: 컨테이너, 단순 initramfs) 이 회수를 셸이 해 주긴 하지만, Python·C 앱을 `init=`으로 바로 띄우면 이 문제가 그대로 생긴다. 그래서 BusyBox init이나 `tini` 같은 작은 init을 앞에 둔다.

---

## 9. 사용자·권한·소유권 — 왜 이미지 빌드에 fakeroot가 필요한가

### 9.1 문제

rootfs 안의 파일은 **기기에서 root 소유**여야 한다(`/bin/busybox` 0:0, `/etc/shadow` 0:0 0600, `/dev/console` c 5,1). 그런데 빌드 서버에서 이미지를 만드는 사람은 일반 사용자다. 일반 사용자는 `chown root`도 `mknod`도 못 한다. 선택지는 세 가지다.

| 방법 | 원리 | 쓰는 곳 |
|---|---|---|
| sudo로 빌드 | 진짜 root | 위험(빌드 스크립트 버그가 호스트를 망침), CI에서 금지되기 일쑤 |
| fakeroot / pseudo | `LD_PRELOAD`로 `chown`·`mknod`·`stat`을 가로채 가짜 소유권 표를 유지 | Buildroot(fakeroot), Yocto(pseudo — 같은 아이디어) |
| 아카이버 옵션 | 기록할 때 소유자를 덮어씀: `cpio -R 0:0`, `tar --owner=0 --group=0`, `mkfs.ext4 -E root_owner=`, 커널 `gen_init_cpio` 목록 파일 | 단순한 경우 |

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="10" width="200" height="100" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="110" y="34" font-size="13" text-anchor="middle">빌드 호스트 디스크</text>
<text x="110" y="56" font-size="12" text-anchor="middle">실제 소유자 501:1000</text>
<text x="110" y="76" font-size="12" text-anchor="middle">dev/console = 빈 일반 파일</text>
<text x="110" y="96" font-size="12" text-anchor="middle">(sudo 없음)</text>
<rect x="240" y="10" width="200" height="100" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="34" font-size="13" text-anchor="middle">fakeroot 세션</text>
<text x="340" y="56" font-size="12" text-anchor="middle">LD_PRELOAD로 chown·mknod</text>
<text x="340" y="76" font-size="12" text-anchor="middle">가로채 가짜 표에 기록</text>
<text x="340" y="96" font-size="12" text-anchor="middle">stat()은 가짜 값을 돌려줌</text>
<rect x="470" y="10" width="200" height="100" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="570" y="34" font-size="13" text-anchor="middle">cpio / ext4 이미지</text>
<text x="570" y="56" font-size="12" text-anchor="middle">stat() 값을 그대로 기록</text>
<text x="570" y="76" font-size="12" text-anchor="middle">0:0, 0600, c 5,1</text>
<text x="570" y="96" font-size="12" text-anchor="middle">기기에서는 진짜 root 소유</text>
<line x1="210" y1="60" x2="238" y2="60" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="440" y1="60" x2="468" y2="60" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="10" y="140" width="660" height="100" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="24" y="164" font-size="13">fakeroot 없이 묶으면 (예제 12 실측):</text>
<text x="24" y="186" font-size="12">/init, /bin/busybox 소유자가 501:1000 → 기기에서 uid 501 프로세스가 /init을 고친다</text>
<text x="24" y="206" font-size="12">gid 1000은 기기의 /etc/group에서 k1user로 보인다 → 빌드 PC의 그룹이 기기 사용자 그룹이 된다</text>
<text x="24" y="226" font-size="12">mknod는 EPERM → 정적 /dev 노드를 못 넣는다 (devtmpfs.mount=0이면 /dev/null이 일반 파일)</text>
</svg>
```
그림 6 — fakeroot는 파일을 바꾸지 않고 "stat()이 보는 세상"만 바꾼다

### 9.2 예제 12 — 같은 트리를 그냥 묶을 때와 fakeroot 안에서 묶을 때

무엇을 확인하나: fakeroot 안팎에서 디스크의 실제 파일, 아카이브 기록, 게스트에서 보이는 소유자가 각각 어떻게 다른지.

```sh
#!/bin/bash
# k1_fakeroot.sh: same tree packed twice - as plain user, and inside one fakeroot session
set -e; cd /var/tmp/K1
rm -rf fr && cp -a /var/lab/build/rootfs fr && echo "k1 secret" > fr/etc/k1_secret && cp /Users/donh/.elinux_scratch/K1/k1_own.sh k1_asuid fr/
echo "--- plain user: mknod is refused"
mknod fr/dev/k1_try c 1 3 2>&1 || true
(cd fr && find . | cpio -o -H newc --quiet | gzip) > k1_plain.cpio.gz
fakeroot -- bash -c '
  cd fr
  chown -R 0:0 .
  chmod 600 etc/k1_secret
  mknod -m 600 dev/console c 5 1
  mknod -m 666 dev/null c 1 3
  echo "--- inside fakeroot:"; ls -ln dev/console dev/null etc/k1_secret
  find . | cpio -o -H newc --quiet | gzip > ../k1_fake.cpio.gz'
echo "--- after fakeroot exits, the real files on disk:"
ls -ln fr/dev/console fr/dev/null fr/etc/k1_secret
echo "--- what the archives recorded:"
for a in k1_plain k1_fake; do echo "$a:"; zcat $a.cpio.gz | cpio -itv --quiet 2>/dev/null | grep -E ":[0-9]{2} (dev/console|etc/k1_secret|bin/busybox)$"; done
```

```text
--- plain user: mknod is refused
mknod: Operation not permitted (os error 1)
--- inside fakeroot:
crw------- 1 0 0 5, 1 Oct 17 17:46 dev/console
crw-rw-rw- 1 0 0 1, 3 Oct 17 17:46 dev/null
-rw------- 1 0 0   10 Oct 17 17:46 etc/k1_secret
--- after fakeroot exits, the real files on disk:
-rw-r--r-- 1 501 1000  0 Oct 17 17:46 fr/dev/console
-rw-r--r-- 1 501 1000  0 Oct 17 17:46 fr/dev/null
-rw------- 1 501 1000 10 Oct 17 17:46 fr/etc/k1_secret
--- what the archives recorded:
k1_plain:
-rw-rw-r--   1 donh     donh           10 Oct 17 17:46 etc/k1_secret
-rwxr-xr-x   1 donh     donh      2242744 Oct 10 10:06 bin/busybox
k1_fake:
-rw-------   1 root     root           10 Oct 17 17:46 etc/k1_secret
crw-------   1 root     root       5,   1 Oct 17 17:46 dev/console
-rwxr-xr-x   1 root     root      2242744 Oct 10 10:06 bin/busybox
```

출력에서 볼 것:

- 일반 사용자 `mknod`는 EPERM이다(`(os error 1)`이 붙은 것은 Ubuntu 26.04의 coreutils가 Rust 구현이라서다).
- **fakeroot 안에서는** 장치 노드와 root 소유가 진짜처럼 보인다. **밖에서 보면** 디스크에는 501 소유의 빈 일반 파일만 있다. fakeroot는 파일을 바꾸지 않고, 가짜 표에 적어 두었다가 같은 세션 안의 `stat()`에 그 값을 돌려준다. 그래서 **아카이브를 만드는 명령(cpio, mkfs.ext4, tar)도 같은 fakeroot 세션 안에서** 돌아야 한다. 세션을 나눠야 하면 `fakeroot -s state.db` / `-i state.db`로 표를 저장·복원한다.
- `chmod`는 진짜로 적용됐다(디스크에서도 0600). 가짜로 처리되는 것은 소유자와 장치 노드 같은 "root만 할 수 있는 것"이다.

게스트에서 같은 확인을 한다. BusyBox에 uid를 바꿔 실행하는 applet이 없어서 18줄짜리 도우미를 쓴다.

```c
/* k1_asuid.c: run a command as an ordinary uid/gid (BusyBox here has no su/setpriv for that) */
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s uid gid cmd [args]\n", argv[0]); return 2; }
    gid_t g = (gid_t)atoi(argv[2]);
    if (setgroups(0, NULL) < 0 || setgid(g) < 0 || setuid((uid_t)atoi(argv[1])) < 0) {
        perror("drop privileges");
        return 1;
    }
    execv(argv[3], &argv[3]);
    perror("execv");
    return 127;
}
```

```sh
#!/bin/sh
# k1_own.sh: guest-side check - who owns the files, and can an ordinary uid 501 modify them?
ls -ln /init /bin/busybox /etc/k1_secret | cat
/k1_asuid 501 1000 /bin/sh -c 'echo "# tampered" >> /init && echo "uid 501: appended to /init"'
/k1_asuid 501 1000 /bin/cat /etc/k1_secret
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static $S/k1_asuid.c -o k1_asuid && $S/k1_fakeroot.sh >/dev/null
for a in plain fake; do $S/k1_run.sh e7$a 60 /var/lab/out/Image k1_$a.cpio.gz "console=ttyAMA0 quiet LAB_CMD=/k1_own.sh"; cat k1_e7$a.log; done
```

```text
exit=0 lines=7
lab initramfs: 6.18.55 up 0.64s
-rwxr-xr-x    1 501      1000       2242744 Oct 10 17:06 /bin/busybox
-rw-rw-r--    1 501      1000            10 Oct 18 00:46 /etc/k1_secret
-rwxrwxr-x    1 501      1000           493 Oct 10 17:06 /init
uid 501: appended to /init
k1 secret
[    0.774008] reboot: Power down
exit=0 lines=7
lab initramfs: 6.18.55 up 0.66s
-rwxr-xr-x    1 0        0          2242744 Oct 10 17:06 /bin/busybox
-rw-------    1 0        0               10 Oct 18 00:46 /etc/k1_secret
-rwxrwxr-x    1 0        0              493 Oct 10 17:06 /init
/bin/sh: can't create /init: Permission denied
cat: can't open '/etc/k1_secret': Permission denied
[    0.781729] reboot: Power down
```

출력에서 볼 것: 그냥 묶은 이미지에서는 **기기의 uid 501 프로세스가 PID 1의 스크립트(`/init`)를 고쳤다.** 기기에 우연히 uid 501로 도는 서비스가 있으면 그 서비스가 부팅 경로를 장악할 수 있다는 뜻이다. fakeroot 이미지에서는 둘 다 `Permission denied`. 기능은 똑같이 "동작"하므로 테스트로는 잘 안 잡히는 보안 버그다.

### 9.3 예제 13 — root 소유 ext4 + 정적 `/dev`를 sudo 없이

무엇을 확인하나: 예제 9의 rootfs를 fakeroot 안에서 `chown`·`mknod`·`mkfs.ext4 -d`까지 한 번에 처리하고, devtmpfs를 끈 채(`devtmpfs.mount=0`) 정적 `/dev`만으로 부팅.

```sh
#!/bin/bash
# k1_mkimg.sh <rootdir> <out.ext4>: root-owned ext4 image with a static /dev, no sudo, no loop mount
set -e
D=$1; OUT=$2
fakeroot -- bash -ec "
  chown -R 0:0 '$D'
  chown -R 1000:1000 '$D/home/k1user'
  chmod 600 '$D/etc/shadow'
  mknod -m 600 '$D/dev/console' c 5 1
  mknod -m 666 '$D/dev/null'    c 1 3
  mknod -m 660 '$D/dev/ttyAMA0' c 204 64
  mknod -m 660 '$D/dev/vda'     b 254 0
  rm -f '$OUT'; truncate -s 16M '$OUT'
  mkfs.ext4 -q -F -L k1root -d '$D' '$OUT'
"
debugfs -R "ls -l /dev" "$OUT" 2>/dev/null | grep -v " \.\.\?$"
debugfs -R "stat /etc/shadow" "$OUT" 2>/dev/null | grep -E "^Inode|User"
```

```sh
cp -a k1_root k1_root_fr; $S/k1_mkimg.sh k1_root_fr k1_rootfr.ext4
$S/k1_run.sh e13 90 /var/lab/out/Image-fs - "console=ttyAMA0 root=/dev/vda rootwait K1_AUTO quiet devtmpfs.mount=0" $(hd k1_rootfr.ext4)
sed -n "/dev (first/,/shadow/p;/null sanity/,\$p" k1_e13.log
```

```text
    108   20600 (3)      0      0       0 17-Oct-2026 17:53 console
    109   20666 (3)      0      0       0 17-Oct-2026 17:53 null
    110   20660 (3)      0      0       0 17-Oct-2026 17:53 ttyAMA0
    111   60660 (4)      0      0       0 17-Oct-2026 17:53 vda

Inode: 124   Type: regular    Mode:  0600   Flags: 0x80000
User:     0   Group:     0   Project:     0   Size: 53
exit=0 lines=30
--- /dev (first 8 of 5)
console null pts ttyAMA0 vda 
--- ownership as the guest sees it
drwxrwxr-x    2 0        0             4096 Oct 10 17:06 bin
drwxrwxr-x    3 0        0             4096 Oct 18 00:53 dev
drwxrwxr-x    3 0        0             4096 Oct 18 00:53 etc
drwxrwxr-x    3 0        0             4096 Oct 18 00:53 home
drwxrwxr-x    5 0        0             4096 Oct 18 00:53 lib
-rwxr-xr-x    1 root     root       2242744 Oct 10 17:06 /bin/busybox
-rw-------    1 root     root            53 Oct 18 00:53 /etc/shadow
--- /dev/null sanity
crw-rw-rw-    1 root     root        1,   3 Oct 18 00:53 /dev/null
```

출력에서 볼 것:

- `debugfs`(이미지를 마운트하지 않고 읽는 e2fsprogs 도구)로 보면 mode `20600` = 문자 장치(`020000`) + 0600, `60660` = 블록 장치(`060000`) + 0660. fakeroot의 가짜 노드가 **진짜 장치 노드로 이미지에 기록됐다.** 장치 노드는 크기 0이고 데이터 블록도 없다 — inode 안의 번호가 전부다.
- 게스트에서는 devtmpfs 없이 정적 노드 4개(+ `pts`)만 있는 `/dev`로 부팅됐고, `/dev/null`은 이번에는 진짜 `c 1, 3`이다(예제 10의 일반 파일 사고와 비교). 소유자는 0:0이라 이름도 `root root`로 나온다.
- 이게 devtmpfs가 없던 시절(또는 지금도 아주 작은 시스템)의 방식이다. Buildroot는 `BR2_ROOTFS_DEVICE_CREATION_STATIC` 선택지와 `device_table_dev.txt`로 같은 일을 한다(K3).

---

## 10. initramfs냐 진짜 rootfs냐 — 커널 커맨드라인으로 정한다

### 10.1 두 경로

C6 §8과 C7 §3에서 두 경로를 자세히 봤다. rootfs 관점에서 요약한다.

| | initramfs | 블록 장치 rootfs (`root=`) |
|---|---|---|
| 어디 있나 | 부트로더가 RAM에 올린 cpio (또는 커널 내장) | eMMC·SD·NVMe·virtio 파티션, NFS |
| 형식 | cpio newc (gzip·zstd 등 압축 가능) | ext4, SquashFS, EROFS, UBIFS … |
| 첫 실행 파일 | `rdinit=` (기본 `/init`) | `init=` 또는 `/sbin/init` 등 |
| 커널에 필요한 것 | `CONFIG_BLK_DEV_INITRD` + 압축 해제기 | 저장장치 드라이버와 FS가 **built-in** |
| `/dev` | 커널 기본 cpio의 `/dev/console`만, devtmpfs는 `/init`이 마운트 | `DEVTMPFS_MOUNT=y`면 커널이 마운트 |
| 쓰기 | RAM이라 쓰기 가능, 재부팅하면 사라짐 | `ro`로 마운트 후 init이 `remount,rw` (또는 끝까지 ro — J3) |
| 주 용도 | 진짜 루트를 찾기 위한 early userspace, 복구, 아주 작은 장비 전체 | 일반 제품 |

### 10.2 rootfs 관련 커널 커맨드라인

| 인자 | 뜻 | 메모 |
|---|---|---|
| `root=/dev/mmcblk0p2` | 루트 장치. `PARTUUID=…`, `PARTLABEL=…`, `/dev/nfs`도 가능 | `/dev/sdX`처럼 열거 순서에 의존하는 이름은 피한다 |
| `rootwait` | 루트 장치가 나타날 때까지 기다림. `rootwait=<초>`로 상한 | USB·SD·eMMC처럼 비동기 probe 장치에 필수 |
| `rootdelay=<초>` | 무조건 N초 기다린 뒤 마운트 | 구식. `rootwait`가 낫다 |
| `rootfstype=ext4` | FS 타입 지정. 없으면 등록된 FS를 차례로 시도 | 지정하면 시도 순서 비용과 엉뚱한 FS로 인식될 위험이 줄어든다 |
| `rootflags=…` | 마운트 옵션 (`noatime`, `data=journal` …) | 루트에 fstab보다 먼저 적용 |
| `ro` / `rw` | 루트를 읽기 전용/쓰기로 마운트. 기본은 `ro` | 예제 9의 `readonly on device 254:0` |
| `init=/sbin/init` | 블록 rootfs에서 실행할 init. 실패하면 바로 panic | `init=/bin/sh`는 비상구 (C6 §8.5) |
| `rdinit=/init` | initramfs에서 실행할 init | 실패하면 경고 후 `root=` 경로로 |
| `devtmpfs.mount=0/1` | `CONFIG_DEVTMPFS_MOUNT` 기본값 덮어쓰기 | 예제 10 |
| `console=ttyAMA0,115200` | `/dev/console`이 가리킬 장치 | 여러 개면 마지막 것이 `/dev/console` |
| `panic=5` | panic 후 5초 뒤 리부트 | 필드 장비 필수 |

### 10.3 예제 9 다시 보기 — 루트 마운트부터 init 실행까지의 타임라인

예제 9의 dmesg에서 rootfs 관련 줄만 시간 축에 놓는다.

```svg
<svg viewBox="0 0 680 385" xmlns="http://www.w3.org/2000/svg">
<text x="40" y="20" font-size="13">예제 9 dmesg (Image-fs, root=/dev/vda rootwait) — 루트 장치가 생긴 뒤 init까지</text>
<line x1="40" y1="90" x2="640" y2="90" stroke="currentColor"/>
<line x1="40.0" y1="86" x2="40.0" y2="94" stroke="currentColor"/>
<text x="40.0" y="110" font-size="12" text-anchor="middle">0.95s</text>
<line x1="125.7" y1="86" x2="125.7" y2="94" stroke="currentColor"/>
<text x="125.7" y="110" font-size="12" text-anchor="middle">1.00s</text>
<line x1="211.4" y1="86" x2="211.4" y2="94" stroke="currentColor"/>
<text x="211.4" y="110" font-size="12" text-anchor="middle">1.05s</text>
<line x1="297.1" y1="86" x2="297.1" y2="94" stroke="currentColor"/>
<text x="297.1" y="110" font-size="12" text-anchor="middle">1.10s</text>
<line x1="382.9" y1="86" x2="382.9" y2="94" stroke="currentColor"/>
<text x="382.9" y="110" font-size="12" text-anchor="middle">1.15s</text>
<line x1="468.6" y1="86" x2="468.6" y2="94" stroke="currentColor"/>
<text x="468.6" y="110" font-size="12" text-anchor="middle">1.20s</text>
<line x1="554.3" y1="86" x2="554.3" y2="94" stroke="currentColor"/>
<text x="554.3" y="110" font-size="12" text-anchor="middle">1.25s</text>
<line x1="640.0" y1="86" x2="640.0" y2="94" stroke="currentColor"/>
<text x="640.0" y="110" font-size="12" text-anchor="middle">1.30s</text>
<circle cx="103.0" cy="90" r="4" fill="#4a7bd0"/>
<line x1="103.0" y1="86" x2="103.0" y2="79" stroke="#4a7bd0"/>
<text x="103.0" y="76" font-size="12" text-anchor="middle">1</text>
<circle cx="346.1" cy="90" r="4" fill="#4a7bd0"/>
<line x1="346.1" y1="86" x2="346.1" y2="79" stroke="#4a7bd0"/>
<text x="346.1" y="76" font-size="12" text-anchor="middle">2</text>
<circle cx="388.4" cy="90" r="4" fill="#e08a3c"/>
<line x1="388.4" y1="86" x2="388.4" y2="79" stroke="#e08a3c"/>
<text x="388.4" y="76" font-size="12" text-anchor="middle">3</text>
<circle cx="391.1" cy="90" r="4" fill="#3f9a6b"/>
<line x1="391.1" y1="86" x2="391.1" y2="59" stroke="#3f9a6b"/>
<text x="391.1" y="56" font-size="12" text-anchor="middle">4</text>
<circle cx="451.6" cy="90" r="4" fill="#888"/>
<line x1="451.6" y1="86" x2="451.6" y2="79" stroke="#888"/>
<text x="451.6" y="76" font-size="12" text-anchor="middle">5</text>
<circle cx="452.6" cy="90" r="4" fill="#d0564a"/>
<line x1="452.6" y1="86" x2="452.6" y2="59" stroke="#d0564a"/>
<text x="452.6" y="56" font-size="12" text-anchor="middle">6</text>
<circle cx="616.1" cy="90" r="4" fill="#e08a3c"/>
<line x1="616.1" y1="86" x2="616.1" y2="79" stroke="#e08a3c"/>
<text x="616.1" y="76" font-size="12" text-anchor="middle">7</text>
<circle cx="46" cy="136" r="4" fill="#4a7bd0"/>
<text x="58" y="140" font-size="12">1  [0.986731]  virtio_blk virtio0: [vda] ... (16.8 MB/16.0 MiB)</text>
<text x="90" y="155" font-size="12">→ 루트 장치 probe 끝 (device initcall)</text>
<circle cx="46" cy="170" r="4" fill="#4a7bd0"/>
<text x="58" y="174" font-size="12">2  [1.128576]  check access for rdinit=/init failed: -2, ignoring</text>
<text x="90" y="189" font-size="12">→ initramfs 없음 → prepare_namespace()</text>
<circle cx="46" cy="204" r="4" fill="#e08a3c"/>
<text x="58" y="208" font-size="12">3  [1.153239]  EXT4-fs (vda): mounted filesystem ... ro</text>
<text x="90" y="223" font-size="12">→ mount_root(): 일단 읽기 전용</text>
<circle cx="46" cy="238" r="4" fill="#3f9a6b"/>
<text x="58" y="242" font-size="12">4  [1.154788]  devtmpfs: mounted</text>
<text x="90" y="257" font-size="12">→ 새 루트의 /dev에 devtmpfs</text>
<circle cx="46" cy="272" r="4" fill="#888"/>
<text x="58" y="276" font-size="12">5  [1.190115]  Freeing unused kernel memory: 3456K</text>
<text x="90" y="291" font-size="12">→ __init 코드·데이터 반납</text>
<circle cx="46" cy="306" r="4" fill="#d0564a"/>
<text x="58" y="310" font-size="12">6  [1.190660]  Run /sbin/init as init process</text>
<text x="90" y="325" font-size="12">→ PID 1 = BusyBox init</text>
<circle cx="46" cy="340" r="4" fill="#e08a3c"/>
<text x="58" y="344" font-size="12">7  [1.286062]  EXT4-fs (vda): re-mounted ... r/w.</text>
<text x="90" y="359" font-size="12">→ rcS의 mount -o remount,rw /</text>
</svg>
```
그림 7 — 실측: virtio 디스크가 생긴 뒤 init까지 약 0.2초, 그중 대부분은 다른 initcall이 끝나기를 기다린 시간

읽는 법:

- 1 → 2 사이 0.14초는 rootfs와 무관한 나머지 initcall(다른 드라이버 probe)이다. `prepare_namespace()`는 `wait_for_device_probe()`로 probe가 다 끝나길 기다린 뒤 시작한다.
- 2 → 3 → 4 → 6이 커널의 rootfs 일이다: `mount_root()`(ext4, 읽기 전용) → `devtmpfs_mount()` → `init_mount(".", "/", MS_MOVE)` + `init_chroot(".")`(새 루트로 이동) → `free_initmem()` → `run_init_process("/sbin/init")`. 커널 쪽은 수십 ms다.
- 7은 이미 유저 공간이다. rcS가 `remount,rw`를 한 시점.
- 이 시간축은 TCG 에뮬레이션(QEMU, KVM 없음)이라 실제 보드와 절대값은 다르다. 순서와 비율을 본다.

---

## 11. 크기 예산 — rootfs는 어디에 MB를 쓰나

### 11.1 예제 14 — 손 rootfs의 구성 요소별 크기

```sh
cd /var/tmp/K1/k1_root
du -sk . ; for p in bin/busybox lib/aarch64-linux-gnu/libc.so.6 lib/ld-linux-aarch64.so.1 lib/modules/6.18.55-fs/kernel lib/modules etc usr/local/bin/k1_hello; do du -sk $p; done
echo "files=$(find . -type f | wc -l) symlinks=$(find . -type l | wc -l) dirs=$(find . -type d | wc -l)"
ls -lS lib/modules/6.18.55-fs/ | sed -n 2,5p
```

```text
6052	.
2192	bin/busybox
1748	lib/aarch64-linux-gnu/libc.so.6
200	lib/ld-linux-aarch64.so.1
400	lib/modules/6.18.55-fs/kernel
1756	lib/modules
52	etc
16	usr/local/bin/k1_hello
files=32 symlinks=401 dirs=32
-rw-rw-r-- 1 donh donh 727920 Oct 17 17:53 modules.builtin.modinfo
-rw-r--r-- 1 donh donh 430333 Oct 17 17:53 modules.builtin.alias.bin
-rw-rw-r-- 1 donh donh  64434 Oct 17 17:53 modules.order
-rw-r--r-- 1 donh donh  56761 Oct 17 17:53 modules.builtin.bin
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="20" font-size="13">예제 9의 손 rootfs = 6052 KiB (du -sk), 구성 요소별 크기</text>
<text x="222" y="55" font-size="12" text-anchor="end">bin/busybox (static)</text>
<rect x="230" y="42" width="328.8" height="18" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="564.8" y="55" font-size="12">2192 KiB (36%)</text>
<text x="222" y="85" font-size="12" text-anchor="end">libc.so.6</text>
<rect x="230" y="72" width="262.2" height="18" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="498.2" y="85" font-size="12">1748 KiB (29%)</text>
<text x="222" y="115" font-size="12" text-anchor="end">modules.* 메타데이터</text>
<rect x="230" y="102" width="203.4" height="18" fill="#888" fill-opacity="0.55" stroke="#888"/>
<text x="439.4" y="115" font-size="12">1356 KiB (22%)</text>
<text x="222" y="145" font-size="12" text-anchor="end">fuse·cuse·null_blk .ko</text>
<rect x="230" y="132" width="60.0" height="18" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<text x="296.0" y="145" font-size="12">400 KiB (7%)</text>
<text x="222" y="175" font-size="12" text-anchor="end">ld-linux-aarch64.so.1</text>
<rect x="230" y="162" width="30.0" height="18" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="266.0" y="175" font-size="12">200 KiB (3%)</text>
<text x="222" y="205" font-size="12" text-anchor="end">기타 (디렉터리·링크)</text>
<rect x="230" y="192" width="13.2" height="18" fill="#888" fill-opacity="0.55" stroke="#888"/>
<text x="249.2" y="205" font-size="12">88 KiB (1%)</text>
<text x="222" y="235" font-size="12" text-anchor="end">/etc 전체</text>
<rect x="230" y="222" width="7.8" height="18" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
<text x="243.8" y="235" font-size="12">52 KiB (1%)</text>
<text x="222" y="265" font-size="12" text-anchor="end">k1_hello (동적 앱)</text>
<rect x="230" y="252" width="2.4" height="18" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="238.4" y="265" font-size="12">16 KiB (0%)</text>
<line x1="230" y1="36" x2="230" y2="280" stroke="currentColor"/>
<text x="20" y="302" font-size="12">비교: 정적 init 하나짜리 rootfs = glibc 714 KB, musl 67 KB (예제 1·2)</text>
<text x="20" y="322" font-size="12">VM 호스트 Ubuntu의 /usr/lib/aarch64-linux-gnu 하나만 1.1 GB</text>
</svg>
```
그림 8 — 실측(`du -sk`): 6 MB 중 BusyBox와 glibc가 2/3, 모듈 메타데이터가 모듈 본체보다 크다

출력에서 볼 것:

- **파일 32개 + 심볼릭 링크 401개**. BusyBox 시스템의 "명령 수백 개"는 전부 `/bin/busybox` 하나를 가리키는 링크다(K2).
- **모듈 3개(400 KiB)보다 인덱스 파일(1356 KiB)이 크다.** `modules.builtin.modinfo`(711 KiB)는 커널에 =y로 들어간 모든 드라이버의 modinfo라, 모듈을 몇 개 안 쓰는 작은 시스템에서는 비율이 역전된다. 꼭 필요한 것은 `modules.dep`(`.bin`)·`modules.alias`(`.bin`)·`modules.builtin`(`.bin`) 정도다. 랩 커널의 모듈 전체를 넣으면 `kernel/` 디렉터리만 89 MB(측정: `du -sh .../6.18.55-fs/kernel`)라, **어떤 모듈을 넣을지 고르는 것 자체가 크기 예산**이다.
- glibc를 동적 앱 하나 때문에 넣으면 2 MB다. 같은 앱을 musl 정적으로 하면 67 KB(§3). 반대로 앱이 수십 개면 공유 libc가 이긴다.

### 11.2 예산을 줄이는 손잡이

| 손잡이 | 효과 | 대가 |
|---|---|---|
| `strip` (디버그 심볼 제거) | 정적 init 714 KB → 604 KB (`strip` 실측) | 기기에서 심볼 없는 backtrace (분리 debug 파일로 보완, B3 §9) |
| libc 선택 (glibc → musl) | 정적 바이너리 수백 KB 단위로 감소 | glibc 전용 기능(NSS, 일부 locale) 없음 (B2) |
| BusyBox applet 줄이기 | 수백 KB | 디버깅 도구 부족 (K2) |
| 모듈 선별 + 메타데이터 정리 | MB 단위 | 핫플러그 장치 지원 범위 |
| 압축 읽기 전용 FS (SquashFS·EROFS) | 압축률은 내용에 따라 다름 (J3에서 압축기별 측정) | 쓰기 불가 → overlay·별도 파티션 (J3) |
| locale·문서·헤더 제거 | 배포판 기반에서 큼 | 개발 편의 (K6) |

센서 키트 같은 장비의 감각: BusyBox + musl 시스템은 2~8 MB, Buildroot + glibc + 앱은 20~60 MB, Yocto 일반 이미지는 수십~수백 MB, Jetson L4T(Ubuntu 기반)는 수 GB다. 이 숫자는 일반적인 경험치이고 이 노트에서 잰 값이 아니다.

---

## 12. 커널 안에서는 무슨 일이 일어나나

이 노트의 모든 실험은 커널 함수 몇 개의 경로로 요약된다(6.18.55 소스 기준).

- **루트 확보**: `kernel_init()` → `kernel_init_freeable()` → `wait_for_initramfs()`(initramfs 해제 완료 대기, `do_populate_rootfs()`가 비동기로 풂) → `console_on_rootfs()` → `init_eaccess(ramdisk_execute_command)` 실패 시 `prepare_namespace()` → `wait_for_device_probe()` → (`rootwait`면 `wait_for_root()`) → `mount_root()` → `devtmpfs_mount()` → `init_mount(".", "/", NULL, MS_MOVE, NULL)` → `init_chroot(".")`.
- **init 실행**: `free_initmem()` → `run_init_process()` → `kernel_execve()` → `bprm_execve()` → `load_elf_binary()`(`fs/binfmt_elf.c`). `PT_INTERP`가 있으면 그 경로를 `open_exec()`로 열고 실패하면 그 에러(-ENOENT)가 그대로 `Failed to execute /init (error -2)`가 된다.
- **장치 노드 생성(devtmpfs)**: 드라이버가 `device_add()`(또는 `device_create()`, `cdev_device_add()`) → `devtmpfs_create_node()` → 요청을 큐에 넣고 `kdevtmpfs` 커널 스레드의 `handle_create()` → `vfs_mknod()`. 그래서 드라이버가 장치를 등록하는 순간 `/dev`에 노드가 생긴다. 이름은 `dev_name()` 또는 드라이버의 `devnode` 콜백이 정한다.
- **장치 노드 열기**: inode를 읽을 때 `init_special_inode()`가 `S_IFCHR`면 `i_fop = &def_chr_fops`, `i_rdev = dev_t`를 설정 → `open()` → `chrdev_open()` → `kobj_lookup(cdev_map, i_rdev)`로 `struct cdev`를 찾고 → `filp->f_op`를 **드라이버의 `file_operations`로 교체** → 드라이버의 `.open` 호출. 번호에 등록된 cdev가 없으면 `-ENXIO`(예제 6의 `c 1:99`는 mem 드라이버가 minor를 거절한 경우).
- **PID 1 종료와 고아**: init 자신의 마지막 스레드가 죽으면 `do_exit()`이 `is_global_init(tsk)`를 보고 바로 `panic("Attempted to kill init! exitcode=0x%08x\n")`(`kernel/exit.c`). 다른 프로세스가 죽을 때는 `exit_notify()` → `forget_original_parent()` → `find_new_reaper()`가 그 자식들의 새 부모(subreaper 또는 init)를 고른다 — 예제 11의 입양된 손자.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="170" y="10" width="340" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="35" font-size="13" text-anchor="middle">do_basic_setup(): initcall, 드라이버 probe</text>
<rect x="170" y="70" width="340" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="95" font-size="13" text-anchor="middle">wait_for_initramfs() → console_on_rootfs()</text>
<rect x="170" y="130" width="340" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="155" font-size="13" text-anchor="middle">init_eaccess(rdinit=/init) 성공?</text>
<line x1="340" y1="50" x2="340" y2="68" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="340" y1="110" x2="340" y2="128" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="250" y1="170" x2="130" y2="198" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="430" y1="170" x2="540" y2="198" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="150" y="182" font-size="12" text-anchor="middle">예 (initramfs)</text>
<text x="540" y="182" font-size="12" text-anchor="middle">아니오 (예제 9)</text>
<rect x="10" y="200" width="240" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="130" y="224" font-size="13" text-anchor="middle">root= 처리 건너뜀</text>
<text x="130" y="244" font-size="12" text-anchor="middle">devtmpfs 자동 마운트 없음</text>
<text x="130" y="262" font-size="12" text-anchor="middle">루트 찾기는 /init의 일 (C7 §3)</text>
<rect x="400" y="200" width="270" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="535" y="222" font-size="13" text-anchor="middle">prepare_namespace()</text>
<text x="535" y="242" font-size="12" text-anchor="middle">wait_for_device_probe(), rootwait</text>
<text x="535" y="260" font-size="12" text-anchor="middle">mount_root() → 기본 ro</text>
<text x="535" y="278" font-size="12" text-anchor="middle">devtmpfs_mount()</text>
<text x="535" y="296" font-size="12" text-anchor="middle">MS_MOVE "." → "/", chroot(".")</text>
<rect x="170" y="330" width="340" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340" y="353" font-size="13" text-anchor="middle">free_initmem() → run_init_process()</text>
<text x="340" y="373" font-size="12" text-anchor="middle">rdinit 또는 init= 또는 /sbin/init … /bin/sh</text>
<line x1="130" y1="270" x2="250" y2="328" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="535" y1="310" x2="440" y2="328" stroke="currentColor" marker-end="url(#ah9)"/>
</svg>
```
그림 9 — `kernel_init_freeable()`의 갈림길: initramfs 경로와 `root=` 경로가 다시 합쳐져 init을 실행한다

드라이버를 쓸 때 다시 만나는 지점:

- G4(char device): `alloc_chrdev_region()` + `cdev_add()` + `device_create()` 세 줄이 "예제 6의 `/proc/devices` 한 줄 + `/sys/class/.../dev` + devtmpfs 노드"를 만든다. `class_create()`의 `devnode` 콜백으로 노드 권한(예: 0666)을 정할 수 있다.
- G9(hotplug·udev): `device_add()`가 보내는 uevent의 `MAJOR=`, `MINOR=`, `DEVNAME=` 환경 변수를 mdev/udev가 받아 노드·링크·권한을 만든다.
- G10(firmware loading): `request_firmware("foo.bin")`은 기본으로 `/lib/firmware/<커널 릴리스>/`와 `/lib/firmware/` 등에서 파일을 직접 읽는다(`CONFIG_FW_LOADER`). **rootfs가 마운트되기 전**(built-in 드라이버의 이른 probe)에는 파일이 없어서 실패하므로, 펌웨어가 필요한 built-in 드라이버는 initramfs에 펌웨어를 넣거나 `CONFIG_EXTRA_FIRMWARE`로 커널에 내장한다.

---

## 13. Embedded 실무에서 다시 보기

### 13.1 장면 1 — Buildroot 이미지가 "Run /sbin/init" 다음에 조용하다

증상: 커널 로그는 `Run /sbin/init as init process`까지 나오고 그 뒤 아무것도 없다. 확인 순서:

1. `quiet`를 빼고 `Warning: unable to open an initial console`이 있는지 본다 → 있으면 내장 initramfs·rootfs의 `/dev/console` 문제(예제 7).
2. `console=`이 실제 UART와 맞는지 본다(`ttyS0` vs `ttyAMA0` vs `ttymxc0`). 커널 로그는 earlycon으로 나오는데 `/dev/console`은 다른 장치를 가리킬 수 있다(C6 §5).
3. inittab의 getty가 올바른 tty를 여는지 본다. BusyBox inittab의 `ttyS0::respawn:/sbin/getty -L ttyS0 115200 vt100` 같은 줄의 장치 이름.
4. `init=/bin/sh`로 부팅해 셸이 뜨면 rootfs 자체는 정상이고 init 설정 문제다.

### 13.2 장면 2 — 새 앱을 넣었더니 `-sh: ./sensor_app: not found`

파일은 있는데 not found — 예제 4의 유저 공간 버전이다. 크로스 툴체인이 만든 바이너리의 `PT_INTERP`(`readelf -l`)가 rootfs의 로더 경로와 다르거나(glibc 툴체인으로 빌드해 musl rootfs에 넣음: `/lib/ld-linux-aarch64.so.1` vs `/lib/ld-musl-aarch64.so.1`), NEEDED 라이브러리가 없는 경우다. 확인은 호스트에서 `aarch64-linux-gnu-readelf -l -d sensor_app`로 한다.

### 13.3 장면 3 — 카메라 노드 권한

V4L2 카메라(`/dev/video0`)가 devtmpfs 기본값 root 0600으로 생겨 서비스 사용자(`camsvc`)가 못 연다. 해결은 rootfs 쪽이다: udev 규칙(`SUBSYSTEM=="video4linux", GROUP="video", MODE="0660"`, L2)이나 mdev.conf 한 줄(`video[0-9]+ root:video 0660`), 그리고 `/etc/group`에 `video` 그룹과 서비스 사용자를 넣는다. 이미지 빌드 시스템에서는 사용자·그룹도 선언적으로 만든다(Buildroot `BR2_ROOTFS_USERS_TABLES`, Yocto `useradd` 클래스, K3/K4).

### 13.4 장면 4 — 현장 장비의 rootfs 레이아웃 결정

드론·로봇처럼 전원이 아무 때나 끊기는 장비라면 예제 9처럼 ext4를 rw로 쓰지 않는다. 그림 2의 왼쪽 두 칸(`/usr`, `/lib`, 기본 `/etc`)은 SquashFS/EROFS 읽기 전용, `/etc` 변경분은 overlay, `/var/lib`·로그는 별도 데이터 파티션, `/tmp`·`/run`은 tmpfs. 이 설계와 측정은 J3, 전원 차단 안전성은 J5, A/B 업데이트는 L4다. K1에서 가져갈 것은 **"어느 디렉터리가 어느 수명 칸에 속하는지 분류하는 눈"**이다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 동적 init인데 로더 없음 | `Failed to execute /init (error -2)` → `No working init found` | `PT_INTERP` 경로에 `ld-linux-*.so.1`이 없음 | `readelf -l`로 경로 확인 후 로더 복사, 또는 정적 링크 |
| 로더는 있고 libc 없음 | `error while loading shared libraries` → `exitcode=0x00007f00` | `DT_NEEDED` 라이브러리가 탐색 경로에 없음 | `readelf -d`의 NEEDED를 전부 넣음 |
| init이 return / exit | `Attempted to kill init! exitcode=0x...` | PID 1 종료 | 무한 루프·`exec`·`reboot(2)` |
| `/dev/console` 없음 | `Warning: unable to open an initial console`, init 출력 없음 | 커널 내장 initramfs에 콘솔 노드 누락 | gen_init_cpio 목록에 `nod /dev/console 0600 0 0 c 5 1`, 또는 init에서 직접 열기 |
| devtmpfs 미마운트 | `/dev`가 비고 `/dev/null`이 일반 파일이 됨 | `DEVTMPFS_MOUNT=n`, initramfs에서 마운트 안 함 | `/init`에서 `mount -t devtmpfs devtmpfs /dev` |
| 일반 사용자로 이미지 빌드 | 기기 파일 소유자가 빌드 PC의 uid (예: 501:1000) | 아카이버가 실제 소유자를 기록 | fakeroot/pseudo, `cpio -R 0:0` |
| 모듈 디렉터리 이름 불일치 | `modprobe: can't change directory to '6.18.55'` | `uname -r`과 `/lib/modules/<dir>` 다름 | 커널과 모듈을 같은 빌드에서 설치 (`modules_install INSTALL_MOD_PATH=`) |
| 루트 드라이버가 모듈 | `VFS: Unable to mount root fs on unknown-block(0,0)` | 루트를 읽을 드라이버·FS가 `=m` | `=y`로 빌드하거나 initramfs에서 로드 (C6 §8.5) |
| SD/USB 루트에 `rootwait` 없음 | 가끔만 부팅 실패 (probe 타이밍) | 비동기 probe가 마운트보다 늦음 | `rootwait` (상한 필요하면 `rootwait=10`) |
| `resolv.conf`·로그를 RO `/etc`·`/var`에 | DNS 갱신 실패, 로그 쓰기 에러 | 쓰기 데이터가 읽기 전용 칸에 있음 | `/run`으로 링크, 별도 파티션 (J3) |

---

## 15. 면접에서 이렇게 말한다

**Q.** "What's the minimum needed to boot to a shell?"

**A.** 커널 쪽은 루트를 읽을 드라이버와 FS가 built-in이어야 한다. rootfs 쪽 최소는 실행 파일 하나다 — 정적 링크면 정말 하나, 동적이면 로더와 libc가 추가된다. 셸이 정상적으로 뜨려면 `/dev/console`(fd 0/1/2의 출처), 그리고 셸이 `/proc`·`/sys`·`/dev`를 마운트해야 `ps`·`mount`가 동작한다. 실제로 정적 C 프로그램 하나짜리 714 KB cpio, musl이면 67 KB로 부팅해 봤고, BusyBox + `/etc` + 모듈을 넣은 ext4가 6 MB였다.

> The kernel itself only needs one executable it can exec as PID 1 — /init in an initramfs, or /sbin/init on the root device — plus the storage driver and filesystem built in. If that binary is static, that's literally the whole rootfs; I've booted a 67 KB musl-static init. If it's dynamic, you also need the ELF interpreter named in PT_INTERP and every DT_NEEDED library. For a usable shell you want /dev/console so PID 1 gets fd 0/1/2, devtmpfs on /dev, and proc and sysfs mounted. A BusyBox system with /etc, a few modules and glibc came to about 6 MB in my lab.

**꼬리 질문**: "Why does the kernel need /dev/console?" → `console_on_rootfs()`가 init 직전에 그걸 열어 fd 0/1/2로 복제한다.

**Q.** "Kernel panics with 'No working init found' — how do you debug?"

**A.** 먼저 `quiet`를 빼고 패닉 위쪽의 첫 `Failed to execute ... (error N)` 줄을 찾는다. -2(ENOENT)인데 파일이 있으면 거의 항상 동적 바이너리의 로더가 없는 것이다 — `readelf -l`로 interpreter 경로, `readelf -d`로 NEEDED를 확인한다. 스크립트 init의 shebang 인터프리터(`#!/bin/bash`인데 BusyBox만 있음)가 없어도 -2다. -8(ENOEXEC)이면 커널이 형식을 모르는 것 — 다른 아키텍처 ELF이거나 shebang 없는 스크립트. -13(EACCES)이면 실행 권한이나 `noexec` 마운트. 그다음 `init=/bin/sh`로 비상 셸을 띄워 rootfs가 맞는지 확인한다.

> I'd drop quiet and look for the first "Failed to execute X (error N)" line, because the kernel silently walks through /sbin/init, /etc/init, /bin/init and /bin/sh after the first failure. Error -2 with a file that clearly exists almost always means the dynamic loader in PT_INTERP isn't in the rootfs — readelf -l and readelf -d on the host tell me exactly what's missing. A missing shebang interpreter also shows up as -2. -8 means the kernel doesn't recognize the format — wrong-architecture ELF or a script without a shebang — and -13 is permissions or a noexec mount. Then I boot with init=/bin/sh to confirm the rootfs is sane. If it's "Attempted to kill init" with exitcode 0x7f00 instead, the loader ran but a shared library is missing.

**Q.** "What is devtmpfs, and how is it different from udev or mdev?"

**A.** devtmpfs는 커널이 직접 채우는 tmpfs다. 드라이버가 `device_add()`하는 순간 커널 스레드가 노드를 만든다. 그래서 유저 공간이 뜨기 전부터 노드가 정확히 있다. udev·mdev는 그 위에서 uevent를 받아 권한·그룹·고정 이름 심볼릭 링크를 만들고 모듈 로드·펌웨어 같은 정책을 수행하는 유저 공간 도구다. devtmpfs = 메커니즘, udev/mdev = 정책이다.

> devtmpfs is a kernel-maintained tmpfs: when a driver calls device_add, the kdevtmpfs thread creates the node with the right major/minor immediately, so /dev is correct even before any userspace runs. CONFIG_DEVTMPFS_MOUNT mounts it on /dev after the root device is mounted, but not in an initramfs — there /init has to mount it. udev or mdev sit on top: they consume uevents and apply policy — ownership, permissions, stable symlinks like /dev/imu0, module autoloading. So devtmpfs is mechanism, udev and mdev are policy.

**Q.** "What are major and minor numbers?"

**A.** 장치 노드 inode에 저장된 `dev_t` 두 부분이다. major는 드라이버(`/proc/devices`의 표), minor는 그 드라이버 안의 인스턴스. `open()`은 이름이 아니라 이 번호로 `cdev_map`에서 드라이버의 `file_operations`를 찾는다. 그래서 `mknod /tmp/x c 204 64`도 UART로 동작한다.

> A device node is just an inode holding a type — char or block — and a dev_t made of major and minor. The major selects the driver, the minor selects the instance inside it. On open, chrdev_open looks up the cdev registered for that dev_t and swaps in the driver's file_operations — the name is irrelevant. I demonstrated that by mknod'ing c 204 64 under /tmp and writing to the UART through it.

**Q.** "Why do build systems use fakeroot?"

**A.** rootfs 파일은 기기에서 root 소유여야 하고 정적 장치 노드도 필요하지만, 빌드는 일반 사용자로 한다. 그대로 묶으면 cpio·ext4가 빌드한 사람의 uid를 기록한다. 실험에서 그 이미지로 부팅하니 기기의 uid 501 프로세스가 `/init`을 고칠 수 있었다. fakeroot는 `LD_PRELOAD`로 `chown`·`mknod`·`stat`을 가로채 같은 세션 안의 아카이버에게 가짜 root 소유권을 보여 주고, 아카이버가 그걸 이미지에 기록한다. Yocto의 pseudo도 같은 원리다.

> Files in the image must be root-owned and you may need static device nodes, but builds run unprivileged — and tools like cpio or mkfs.ext4 -d record the real uid. In my lab an image packed without fakeroot let uid 501 on the device rewrite /init. fakeroot LD_PRELOADs chown, mknod and stat, keeps a fake ownership database, and the archiver running inside the same session writes those fake values into the image. Yocto's pseudo is the same idea with a persistent database.

**Q.** "initramfs vs. a root filesystem on a block device — when do you use each?"

**A.** initramfs는 부트로더가 RAM에 올린 cpio로, 커널이 메모리 rootfs에 풀고 `/init`을 실행한다. 루트 장치를 찾기 위해 모듈 로드·복호화·dm-verity 설정·장치 대기를 해야 할 때, 또는 장비 전체가 작아서 RAM에서 돌려도 될 때 쓴다. 블록 rootfs는 커널이 `root=`로 직접 마운트하므로 드라이버·FS가 built-in이어야 하고, initramfs 단계가 없어 부팅이 단순하고 빠르다.

> An initramfs is a cpio archive the bootloader drops in RAM; the kernel unpacks it into rootfs and runs /init, and from then on finding the real root is userspace's job — loading modules, unlocking dm-crypt, setting up dm-verity, waiting for devices, then switch_root. A block-device root is mounted directly by the kernel from root=, which requires the storage driver and filesystem to be built in, but it's simpler and faster. On small fixed-hardware products I'd skip the initramfs; with secure boot plus verity or encrypted root I'd use one.

**Q.** "What must PID 1 do?"

**A.** 절대 exit하지 않고(하면 panic), 끝낼 때는 `reboot(2)`, 자기 자식과 입양된 고아를 `wait()`로 회수하고(안 하면 좀비 누적 — 실험에서 22개가 그대로 남았다), 시그널은 핸들러로만 받는다(커널이 PID 1에게 기본 동작 시그널을 버린다). 그리고 `/proc`·`/sys`·`/dev` 마운트 같은 초기화를 한다.

> Never exit — that's "Attempted to kill init". Shut down via reboot(2) after sync. Reap every child, including orphans reparented to it; in my test a non-reaping init held 22 zombies indefinitely. Handle signals explicitly, since the kernel drops default-action signals sent to init. And do the early setup: mount proc, sysfs, devtmpfs, then start services.

**Q.** "You see 'Warning: unable to open an initial console'. What does it mean?"

**A.** `console_on_rootfs()`가 init 직전에 메모리 rootfs의 `/dev/console`을 못 열었다는 뜻이다. 그러면 PID 1은 fd 0/1/2 없이 시작해 모든 출력이 사라진다. 대개 `CONFIG_INITRAMFS_SOURCE`로 커널에 넣은 디렉터리에 `/dev/console` 노드가 없을 때다. 노드를 목록 파일에 넣거나, init이 devtmpfs를 마운트하고 콘솔을 직접 연다.

> It means the kernel couldn't open /dev/console in the initial rootfs right before exec'ing init, so PID 1 starts with fd 0, 1 and 2 closed and all its output vanishes. The kernel's default built-in initramfs provides that node, so you typically hit this when CONFIG_INITRAMFS_SOURCE points at a directory without it. Fix it in the cpio list with "nod /dev/console 0600 0 0 c 5 1", or have init mount devtmpfs and reopen the console itself.

---

## 16. 직접 해보기

1. 개념: `root=/dev/vda`로 부팅하는데 ext4 이미지에 `/dev/console`이 없다. 셸 출력이 나오는가? 이유는? — 정답/힌트: 나온다. `console_on_rootfs()`가 ext4 마운트 전에 메모리 rootfs(커널 기본 initramfs)의 `/dev/console`을 연다(§5.2, 예제 10).
2. 코드 과제: `k1_init.c`를 고쳐 `/proc`·`/sys`·`/dev`를 마운트하고 `/proc/mounts`를 출력한 뒤 `execv("/bin/sh")`하는 init을 만들고, 랩 BusyBox 하나만 더한 cpio로 부팅해 보라. — 정답/힌트: BusyBox는 정적이라 `/bin/busybox` + `/bin/sh` 링크 하나면 된다. `exec`했으니 PID 1은 셸이 된다.
3. 관찰: 예제 4의 rootfs에 `/lib/ld-linux-aarch64.so.1`을 넣되 **실행 권한 없이**(`chmod 644`) 넣으면 커널 메시지가 어떻게 바뀌는지 예상하고 확인하라. — 정답/힌트: 예상 후 직접 확인할 것(이 노트에서는 실행하지 않았다). `open_exec()`가 인터프리터에 대해 무엇을 검사하는지 `fs/exec.c`에서 찾아본다.
4. 관찰: 예제 6의 게스트에서 `ls -l /sys/dev/block/`과 `cat /proc/partitions`를 비교하고, `/dev/vda`를 `mknod`로 다른 이름에 만들어 `blkid`로 읽어 보라. — 정답/힌트: 블록 장치도 번호(254:0)만 같으면 같은 디스크다. 빈 디스크라 blkid는 아무것도 출력하지 않을 수 있다.
5. 코드 과제: `k1_mkrootfs.sh`에서 `modules.builtin.modinfo`를 빼고 `depmod`를 다시 돌린 뒤 `modprobe cuse`가 여전히 되는지, 크기가 얼마나 줄었는지 재라. — 정답/힌트: `modules.builtin.alias.bin`이 modinfo에서 만들어지므로 둘 다 줄어든다. built-in 모듈의 alias 해석(예: `modprobe` 이름이 built-in 드라이버의 alias일 때)이 영향을 받는다.
6. 개념: 예제 12에서 fakeroot 세션을 두 번으로 나눠 `chown`은 첫 번째, `cpio`는 두 번째에서 하면 아카이브의 소유자는? — 정답/힌트: 501:1000으로 돌아간다. 가짜 표는 세션 메모리에만 있다. `fakeroot -s db` / `-i db`로 이어 붙인다.
7. 관찰: 예제 11의 reaper를 `init=`이 아닌 BusyBox 셸 아래 일반 프로세스로 실행하고 `prctl(PR_SET_CHILD_SUBREAPER, 1)`을 넣었을 때 손자가 누구에게 입양되는지 `/proc/<pid>/status`의 `PPid`로 확인하라. — 정답/힌트: subreaper로 지정한 프로세스에 입양된다(`find_new_reaper()`).

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| rootfs | root filesystem | 커널이 `/`로 삼는 파일시스템. 커널 내부에도 초기 rootfs(ramfs/tmpfs)라는 이름의 FS가 있다 |
| initramfs | initial RAM filesystem | 부트로더가 올리거나 커널에 내장한 cpio를 rootfs에 푼 것 |
| cpio newc | cpio "new ASCII" 형식 | initramfs 형식. 헤더 필드가 16진수 ASCII, magic `070701` |
| FHS | Filesystem Hierarchy Standard | 디렉터리 배치 표준 |
| usrmerge | /usr 병합 | `/bin`, `/sbin`, `/lib`를 `/usr` 아래로 합치고 링크로 남김 |
| PT_INTERP | ELF 인터프리터 프로그램 헤더 | 커널이 먼저 올릴 동적 로더 경로 |
| DT_NEEDED | 동적 섹션 항목 | 로더가 찾아 올릴 공유 라이브러리 이름 |
| ld.so / ld-linux | 동적 링커·로더 | aarch64 glibc는 `/lib/ld-linux-aarch64.so.1` |
| 장치 노드 | device special file | 타입(c/b) + major/minor만 담은 inode |
| major / minor | 장치 번호 | 드라이버 선택 / 인스턴스 선택, 합쳐서 `dev_t` |
| mknod | make node | 장치 노드를 만드는 syscall·명령, `CAP_MKNOD` 필요 |
| devtmpfs | 커널이 관리하는 /dev | 드라이버 등록 시 커널이 노드 생성 |
| mdev | BusyBox 장치 관리자 | uevent마다 실행되는 작은 udev 대체 |
| udev | systemd-udevd | uevent 규칙 엔진 (L2) |
| uevent | 커널 → 유저 장치 이벤트 | netlink 또는 hotplug helper로 전달, `MAJOR=`/`MINOR=` 포함 |
| PID 1 | init 프로세스 | 커널이 처음 실행하는 유저 프로세스, 고아의 부모 |
| reaper | 회수자 | 죽은 자식의 종료 상태를 `wait()`로 거두는 프로세스 |
| fakeroot | 가짜 root 환경 | `LD_PRELOAD`로 소유권 관련 호출을 가로채는 도구 |
| pseudo | Yocto의 fakeroot | 영속 DB를 가진 같은 원리의 도구 |
| `mkfs.ext4 -d` | 디렉터리로 채우며 생성 | 마운트 없이 ext4 이미지 생성 |
| `modules.dep` | 모듈 의존성 표 | `depmod`가 생성, `modprobe`가 읽음 |
| `rootwait` | 루트 장치 대기 | 장치가 나타날 때까지 마운트를 미룸 |

---

## 18. 요약 & 체크리스트

커널이 rootfs에 요구하는 것은 `execve()`할 파일 하나뿐이다. 정적 바이너리라면 그게 rootfs 전부이고(714 KB glibc, 67 KB musl로 부팅했다), 동적이면 `PT_INTERP`의 로더와 `DT_NEEDED`의 라이브러리가 더 필요하다 — 없을 때의 메시지는 각각 `Failed to execute /init (error -2)`와 `exitcode=0x00007f00`으로 다르다. 커널은 init 직전에 `/dev/console`을 열어 fd 0/1/2를 주므로 이 노드가 없으면 출력이 사라진다. 장치 노드는 이름이 아니라 (타입, major, minor)이고, 현대 시스템은 devtmpfs가 노드를 만들고 mdev/udev가 정책을 얹는다. 셸·로그인·모듈까지 가려면 가상 FS 마운트, `/etc` 파일, `/lib/modules/$(uname -r)`이 필요하고, 그 모든 파일이 기기에서 root 소유가 되도록 이미지는 fakeroot 같은 도구 안에서 만들어야 한다. 이 손 rootfs가 K2(BusyBox)·K3(Buildroot)·K4/K5(Yocto)가 자동으로 만들어 주는 것의 정체다.

- [ ] 커널이 PID 1 후보를 찾는 순서(`rdinit=` → `init=` → `/sbin/init` … `/bin/sh`)를 말할 수 있다
- [ ] 정적 바이너리 하나로 initramfs를 만들어 부팅할 수 있다
- [ ] `Failed to execute /init (error -2)`와 `exitcode=0x00007f00`을 구분해 원인을 말할 수 있다
- [ ] `readelf -l`/`-d`로 동적 바이너리가 rootfs에 요구하는 파일을 뽑을 수 있다
- [ ] FHS 디렉터리를 수명·쓰기 여부로 분류하고 usrmerge를 설명할 수 있다
- [ ] major/minor가 커널에서 드라이버로 이어지는 경로(`chrdev_open` → `cdev_map`)를 설명할 수 있다
- [ ] 정적 `/dev`, devtmpfs, mdev, udev의 차이와 devtmpfs가 initramfs에 자동 마운트되지 않는 이유를 말할 수 있다
- [ ] "Warning: unable to open an initial console"을 재현하고 고칠 수 있다
- [ ] BusyBox + `/etc` + 모듈로 ext4 rootfs를 `mkfs.ext4 -d`로 만들어 `root=/dev/vda rootwait`로 부팅할 수 있다
- [ ] fakeroot가 무엇을 가로채고 왜 아카이버와 같은 세션이어야 하는지 설명할 수 있다
- [ ] PID 1의 규칙(exit 금지, 회수, 시그널, 콘솔)을 코드로 보여 줄 수 있다
- [ ] rootfs 크기를 구성 요소별로 재고 줄일 손잡이를 고를 수 있다

## 참고 자료

- docs.kernel.org — "ramfs, rootfs and initramfs" (`Documentation/filesystems/ramfs-rootfs-initramfs.rst`)
- docs.kernel.org — "Explaining the 'No working init found.' boot hang message" (`Documentation/admin-guide/init.rst`)
- docs.kernel.org — "The kernel's command-line parameters" (`Documentation/admin-guide/kernel-parameters.txt`: `root=`, `rootwait`, `rootfstype=`, `init=`, `rdinit=`, `devtmpfs.mount=`)
- docs.kernel.org — "Linux allocated devices" (`Documentation/admin-guide/devices.txt`: 1:3 null, 5:1 console, 204:64 ttyAMA0)
- Linux 6.18 소스: `init/main.c`(`kernel_init`, `console_on_rootfs`), `init/do_mounts.c`(`prepare_namespace`), `init/initramfs.c`, `drivers/base/devtmpfs.c`, `fs/char_dev.c`, `fs/binfmt_elf.c`, `kernel/exit.c` — elixir.bootlin.com에서 같은 경로로 볼 수 있다
- Filesystem Hierarchy Standard 3.0 — refspecs.linuxfoundation.org
- man7.org: `mknod(2)`, `ld.so(8)`, `init(1)`, `wait(2)`, `reboot(2)`, `hier(7)`, `file-hierarchy(7)`
- Michael Kerrisk, "The Linux Programming Interface" — 26장(자식 프로세스 감시), 41~42장(공유 라이브러리)
- Bootlin, "Embedded Linux system development" 교육 슬라이드 — rootfs·BusyBox·장치 파일 장
- BusyBox 1.37 소스: `modutils/modprobe-small.c`, `init/init.c`, `util-linux/mdev.c`; e2fsprogs `mke2fs(8)`의 `-d` 옵션; `fakeroot(1)`
