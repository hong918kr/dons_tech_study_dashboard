# C7. init과 부팅 완료 — PID 1, initramfs, switch_root, 부팅 시간 줄이기

> **이 노트를 다 읽으면**: PID 1이 반드시 해야 하는 일(가상 FS 마운트·좀비 회수·절대 exit 금지)을 직접 만든 init으로 보여 줄 수 있다 · BusyBox init(`/etc/inittab`)·직접 쓴 `/init` 스크립트·systemd(unit·target·의존성)를 비교하고 `systemd-analyze`로 부팅을 해부할 수 있다 · initramfs가 진짜 rootfs를 찾아 `switch_root`하는 과정을 QEMU에서 처음부터 만들 수 있다 · 부팅 시간을 U-Boot부터 "앱 준비 완료"까지 재고, 어떤 기법이 실제로 몇 초를 줄이는지 측정값으로 말할 수 있다
> **왜 필요한가**: "부팅이 12초 걸린다, 3초로 줄여라"는 Embedded Linux 팀이 받는 가장 흔한 과제 중 하나다. 면접에서는 "What does PID 1 do?", "Why do we need an initramfs?", "How would you cut boot time?"으로 나온다
> **Don 기준 난이도**: 측정 기반 최적화, 리셋 후 타이머로 구간 재기, 펌웨어 부트 시퀀스는 이미 안다 / Linux에서 "부팅 끝"이 커널이 아니라 유저 공간 PID 1의 일이라는 점, initramfs·switch_root·systemd 의존성 그래프가 새로 배울 부분
> **선행 노트**: A2(프로세스·좀비·`wait`), C1(부트 전체 지도), C3(U-Boot 사용), C6(커널 시작 과정). L1(systemd 깊이 보기)로 이어진다

---

## 0. 큰 그림 — "부팅 완료"는 커널이 아니라 PID 1이 정한다

SSD 펌웨어에서 "부팅 완료"는 명확했다. 리셋 벡터 → C 런타임 초기화 → HW 초기화 → NVMe 컨트롤러 Ready(CSTS.RDY=1). 이 시간은 스펙(예: CAP.TO)으로 정해져 있고, Don은 GPIO 토글과 scope로 구간을 쟀을 것이다.

Linux 보드에서는 "부팅 완료"를 커널이 정하지 않는다. 커널은 드라이버를 probe하고 rootfs를 준비한 다음 **유저 공간 프로그램 하나(PID 1, init)** 를 실행하고 손을 뗀다. 그 뒤 파일시스템 마운트, 모듈 로드, 네트워크, 센서 데몬, 카메라 파이프라인을 띄우는 건 전부 init의 일이다. 그래서 "부팅 12초"라고 할 때 그 12초는 보통 **펌웨어 + 커널 + init이 띄운 유저 공간** 셋의 합이고, 셋 중 어디가 큰지 재 보기 전에는 아무것도 줄일 수 없다.

```svg
<svg viewBox="0 0 680 200" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="40" width="104" height="56" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="62.0" y="63.8" text-anchor="middle" font-size="12" font-weight="bold">BootROM</text>
  <text x="62.0" y="80.8" text-anchor="middle" font-size="12">SPL · TF-A</text>
  <line x1="115.0" y1="68.0" x2="119.0" y2="68.0" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="120" y="40" width="104" height="56" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="172.0" y="63.8" text-anchor="middle" font-size="12" font-weight="bold">U-Boot</text>
  <text x="172.0" y="80.8" text-anchor="middle" font-size="12">bootcmd</text>
  <line x1="225.0" y1="68.0" x2="229.0" y2="68.0" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="230" y="40" width="104" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="282.0" y="63.8" text-anchor="middle" font-size="12" font-weight="bold">커널</text>
  <text x="282.0" y="80.8" text-anchor="middle" font-size="12">start_kernel</text>
  <line x1="335.0" y1="68.0" x2="349.0" y2="68.0" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="350" y="40" width="104" height="56" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="402.0" y="63.8" text-anchor="middle" font-size="12" font-weight="bold">initramfs</text>
  <text x="402.0" y="80.8" text-anchor="middle" font-size="12">/init</text>
  <line x1="455.0" y1="68.0" x2="459.0" y2="68.0" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="460" y="40" width="104" height="56" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="512.0" y="63.8" text-anchor="middle" font-size="12" font-weight="bold">진짜 rootfs</text>
  <text x="512.0" y="80.8" text-anchor="middle" font-size="12">switch_root</text>
  <line x1="565.0" y1="68.0" x2="569.0" y2="68.0" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="570" y="40" width="104" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="622.0" y="63.8" text-anchor="middle" font-size="12" font-weight="bold">PID 1 init</text>
  <text x="622.0" y="80.8" text-anchor="middle" font-size="12">서비스 시작</text>
  <text x="115.0" y="26.0" text-anchor="middle" font-size="13" font-weight="bold">C1–C5 (펌웨어)</text>
  <text x="280.0" y="26.0" text-anchor="middle" font-size="13" font-weight="bold">C6</text>
  <text x="510.0" y="26.0" text-anchor="middle" font-size="13" font-weight="bold">C7 — 이 노트</text>
  <line x1="10.0" y1="112.0" x2="670.0" y2="112.0" stroke="currentColor"/>
  <line x1="10.0" y1="106.0" x2="10.0" y2="118.0" stroke="currentColor"/>
  <text x="10.0" y="134.0" font-size="12">전원 ON</text>
  <line x1="230.0" y1="106.0" x2="230.0" y2="118.0" stroke="currentColor"/>
  <text x="230.0" y="134.0" text-anchor="middle" font-size="12">Starting kernel</text>
  <line x1="350.0" y1="106.0" x2="350.0" y2="118.0" stroke="currentColor"/>
  <text x="350.0" y="134.0" text-anchor="middle" font-size="12">Run /init</text>
  <line x1="670.0" y1="106.0" x2="670.0" y2="118.0" stroke="currentColor"/>
  <text x="670.0" y="134.0" text-anchor="end" font-size="12">앱 준비 완료</text>
  <rect x="10" y="150" width="330" height="38" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="175.0" y="173.3" text-anchor="middle" font-size="12">커널이 하는 일: 드라이버 probe, rootfs 준비</text>
  <rect x="350" y="150" width="320" height="38" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510.0" y="173.3" text-anchor="middle" font-size="12">유저 공간이 하는 일: 마운트, 모듈, 서비스</text>
</svg>
```
그림 1 — 부트 체인에서 이 노트가 맡는 구간. C1~C5가 펌웨어, C6이 커널 내부, C7은 커널이 `/init`을 실행한 순간부터 "앱 준비 완료"까지와, 전체 구간의 시간 측정을 다룬다.

이 노트에서 하는 일:

- PID 1이 무엇을 책임지는지 C로 직접 init을 만들어 확인한다 (exit하면 커널이 panic하는 것까지 실제로 본다).
- init 구현 세 가지 — 직접 쓴 `/init` 스크립트, BusyBox init + `/etc/inittab`, systemd — 를 비교한다. systemd는 VM 호스트(Ubuntu)의 실제 부팅 데이터로 본다.
- initramfs가 진짜 rootfs(virtio 디스크의 ext4)를 찾아 `switch_root`하는 과정을 QEMU에서 처음부터 만든다.
- U-Boot → 커널 → 유저 공간 "READY" 마커까지 전 구간을 재는 하네스를 만들고, 부팅 시간 단축 기법을 하나씩 켜면서 **실제로 몇 초가 줄어드는지** 잰다.

> 실행 환경: VM 호스트는 Lima `elinux`(aarch64, `uname -r` = `7.0.0-38-generic`, systemd 259). 게스트는 공용 랩 `/var/lab`의 QEMU `virt`(cortex-a57, 512 MB, KVM 없이 TCG) + Linux 6.18.55 + U-Boot v2026.10 + BusyBox 1.37 static. 작업물은 `/var/tmp/C7/`에 만들었다. PID·주소·시간은 실행마다 다르다 (단, §4.3의 icount 모드는 일부러 결정적으로 만든 것이다).

---

## 1. PID 1이 해야 하는 일

### 1.1 직관 — RTOS의 "idle 태스크 + 태스크 매니저"를 유저 공간에 둔 것

RTOS에서 `main()`은 태스크를 만들고 스케줄러를 시작하면 다시는 돌아오지 않는다. 돌아오면? 보통 `while(1)`이나 HardFault다. PID 1도 같다. 커널이 처음 만드는 유저 프로세스이고, **절대 끝나면 안 된다**. 다른 점은 PID 1에는 커널이 몇 가지 특별한 역할을 떠넘겼다는 것이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="270" y="118" width="140" height="64" rx="5" fill="#d0564a" fill-opacity="0.18" stroke="#d0564a"/>
  <text x="340.0" y="145.5" text-anchor="middle" font-size="14" font-weight="bold">PID 1</text>
  <text x="340.0" y="164.5" text-anchor="middle" font-size="14">init</text>
  <rect x="10" y="20" width="220" height="62" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120.0" y="46.7" text-anchor="middle" font-size="13" font-weight="bold">① 가상 FS 마운트</text>
  <text x="120.0" y="64.7" text-anchor="middle" font-size="13">/proc /sys /dev /run</text>
  <line x1="231.0" y1="51.0" x2="268.0" y2="128.0" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="112" width="220" height="62" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120.0" y="138.7" text-anchor="middle" font-size="13" font-weight="bold">② 고아 입양 + 좀비 회수</text>
  <text x="120.0" y="156.7" text-anchor="middle" font-size="13">wait() / SIGCHLD</text>
  <line x1="231.0" y1="143.0" x2="268.0" y2="150.0" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="204" width="220" height="62" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120.0" y="230.7" text-anchor="middle" font-size="13" font-weight="bold">③ 서비스 시작·재시작</text>
  <text x="120.0" y="248.7" text-anchor="middle" font-size="13">respawn · 의존성 순서</text>
  <line x1="231.0" y1="235.0" x2="268.0" y2="172.0" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="450" y="20" width="220" height="62" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560.0" y="46.7" text-anchor="middle" font-size="13" font-weight="bold">④ 시그널 기본 동작 없음</text>
  <text x="560.0" y="64.7" text-anchor="middle" font-size="13">핸들러 없으면 무시됨</text>
  <line x1="449.0" y1="51.0" x2="412.0" y2="128.0" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="450" y="112" width="220" height="62" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560.0" y="138.7" text-anchor="middle" font-size="13" font-weight="bold">⑤ 종료 처리</text>
  <text x="560.0" y="156.7" text-anchor="middle" font-size="13">shutdown → sync → reboot()</text>
  <line x1="449.0" y1="143.0" x2="412.0" y2="150.0" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="450" y="204" width="220" height="62" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="560.0" y="230.7" text-anchor="middle" font-size="13" font-weight="bold">⑥ 절대 exit 금지</text>
  <text x="560.0" y="248.7" text-anchor="middle" font-size="13">exit = kernel panic</text>
  <line x1="449.0" y1="235.0" x2="412.0" y2="172.0" stroke="currentColor" marker-end="url(#ah2)"/>
</svg>
```
그림 2 — PID 1의 여섯 가지 책임. 왼쪽 셋은 "해야 하는 일", 오른쪽 셋은 커널이 PID 1을 특별 취급해서 생기는 규칙이다.

**정의**:

- **PID 1(init)**: 커널이 부팅 마지막에 `run_init_process()`로 실행하는 첫 유저 프로세스. initramfs가 있으면 `/init`(커널 인자 `rdinit=`로 바꿀 수 있음), 없으면 `root=`를 마운트한 뒤 `init=` 또는 `/sbin/init`.
- **좀비(zombie)**: 끝났지만 부모가 `wait()`로 종료 상태를 거둬 가지 않아 `task_struct`가 남아 있는 프로세스(A2). PID와 커널 메모리를 계속 차지한다.
- **고아(orphan)**: 부모가 먼저 죽은 프로세스. 커널은 고아의 부모를 **PID 1**(또는 `PR_SET_CHILD_SUBREAPER`로 지정된 조상)으로 바꾼다. 그래서 고아가 끝나면 PID 1이 `wait()`해 줘야 좀비가 사라진다.

### 1.2 VM 호스트의 PID 1부터 보자

무엇을 확인하나: 일반 Ubuntu에서 PID 1이 무엇인지, `/sbin/init`이 실제로 무엇을 가리키는지.

```sh
ps -p 1 -o pid,comm,args; readlink /proc/1/exe; ls -l /sbin/init
```

```text
    PID COMMAND         COMMAND
      1 systemd         /usr/lib/systemd/systemd --switched-root --system --deserialize=51
lrwxrwxrwx 1 root root 22 Jul 27 13:44 /sbin/init -> ../lib/systemd/systemd
```

출력에서 볼 것: `--switched-root`. 이 systemd는 **initramfs 안에서 먼저 PID 1로 시작했다가 진짜 루트로 `switch_root`한 뒤 자기 자신을 다시 exec한** 것이다(§3에서 같은 일을 BusyBox로 직접 한다). PID는 끝까지 1로 유지된다.

### 1.3 PID 1이 exit하면 — "Attempted to kill init!"

무엇을 확인하나: PID 1이 할 일을 다 했다고 `exit`하면 무슨 일이 생기는지. 랩 rootfs를 `/var/tmp/C7/r_pid1`로 복사하고 아래 스크립트를 `/c7_exit`로 넣은 뒤 `rdinit=/c7_exit`로 부팅한다.

```sh
#!/bin/sh
mount -t proc proc /proc
echo "C7: /init running as PID $$ - doing my job and then exiting (bug!)"
exit 3
```

```sh
cd /var/tmp/C7
(cd r_pid1 && find . | cpio -o -H newc --quiet | gzip) > c7_pid1.cpio.gz
timeout 60 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd c7_pid1.cpio.gz \
  -append "console=ttyAMA0 quiet rdinit=/c7_exit" 2>&1 | head -30
```

```text
C7: /init running as PID 1 - doing my job and then exiting (bug!)
[    2.456063] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000300
[    2.462308] CPU: 0 UID: 0 PID: 1 Comm: c7_exit Not tainted 6.18.55 #1 PREEMPT
[    2.462980] Hardware name: linux,dummy-virt (DT)
[    2.463556] Call trace:
[    2.463954]  show_stack+0x28/0x7c (C)
[    2.465253]  dump_stack_lvl+0x60/0x80
[    2.465481]  dump_stack+0x18/0x24
[    2.465643]  vpanic+0xf8/0x2dc
[    2.465790]  abort+0x0/0x4
[    2.465913]  make_task_dead+0x0/0x1b0
[    2.466072]  do_group_exit+0x30/0xa4
[    2.466244]  __arm64_sys_exit_group+0x18/0x1c
[    2.466418]  invoke_syscall.constprop.0+0x40/0xe0
[    2.466611]  el0_svc_common.constprop.0+0x38/0xe0
[    2.466942]  do_el0_svc+0x1c/0x28
[    2.467217]  el0_svc+0x38/0x118
[    2.467359]  el0t_64_sync_handler+0xa0/0xe4
[    2.467546]  el0t_64_sync+0x198/0x19c
[    2.468599] Kernel Offset: 0x3bd808a00000 from 0xffff800080000000
[    2.468959] PHYS_OFFSET: 0xfff1000040000000
[    2.469163] CPU features: 0x104000,04027800,40004000,0400421b
[    2.469448] Memory Limit: none
[    2.470097] ---[ end Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000300 ]---
qemu-system-aarch64: terminating on signal 15 from pid 173822 (timeout)
```

출력에서 볼 것:

- `exitcode=0x00000300` — `wait()` 상태 형식이다. 상위 바이트 `0x03`이 `exit 3`의 값이다. 시그널로 죽었다면 하위 바이트에 시그널 번호가 찍힌다(예: segfault면 `0x0000000b`).
- 콜 스택이 `el0_svc` → `__arm64_sys_exit_group` → `do_group_exit`로 들어간다. 즉 **유저가 부른 `exit_group` syscall 안에서** 커널이 panic한다(A1의 `svc #0` 경로 그대로).
- 패닉 후 보드는 멈춘다. 실제 제품이라면 `panic=5` 커널 인자로 5초 뒤 자동 리부트하게 하거나, HW 워치독이 리셋한다. Don의 SSD 경험으로 말하면 "assert 실패 후 무한 루프 → 워치독 리셋"과 같은 구조다.

함정: 셸 스크립트 `/init`의 마지막 줄에 `exec /bin/sh`나 `exec switch_root …`를 빠뜨리면 스크립트가 끝나는 순간 바로 이 panic이다. 또 `/init`이 `set -e`를 쓰면 명령 하나만 실패해도 스크립트가 끝나 panic으로 이어진다.

### 1.4 좀비 회수와 고아 입양 — C로 만든 최소 PID 1

무엇을 확인하나: PID 1이 `wait()`를 안 하면 좀비가 쌓이는지, 고아가 정말 PID 1에 입양되는지, 그리고 PID 1이 끝내는 올바른 방법(exit가 아니라 `reboot()`)이 무엇인지.

```c
/* c7_pid1: 최소 PID 1 — 가상 FS 마운트, 고아 입양, 좀비 회수(reap 모드), exit 대신 poweroff */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <unistd.h>

static void zombies(const char *tag)
{
    DIR *d = opendir("/proc");
    struct dirent *e;
    int n = 0;
    if (!d) { perror("opendir /proc"); return; }
    while ((e = readdir(d))) {
        char p[300], buf[256];
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        snprintf(p, sizeof p, "/proc/%s/stat", e->d_name);
        FILE *f = fopen(p, "r");
        if (!f) continue;
        int pid, ppid; char comm[64], st;
        if (fgets(buf, sizeof buf, f) && sscanf(buf, "%d %63s %c %d", &pid, comm, &st, &ppid) == 4 && st == 'Z') {
            printf("  %s: pid=%d %s state=Z ppid=%d\n", tag, pid, comm, ppid); n++;
        }
        fclose(f);
    }
    closedir(d);
    printf("  %s: zombie count = %d\n", tag, n);
}

int main(int argc, char **argv)
{
    int reap = argc > 1 && strcmp(argv[1], "reap") == 0;
    if (mount("proc", "/proc", "proc", 0, NULL)) perror("mount proc");
    if (mount("sysfs", "/sys", "sysfs", 0, NULL)) perror("mount sysfs");
    if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL)) perror("mount devtmpfs");
    printf("c7_pid1: pid=%d ppid=%d mode=%s\n", getpid(), getppid(), reap ? "reap" : "noreap");
    for (int i = 0; i < 3; i++)               /* 바로 끝나는 자식 3개 */
        if (fork() == 0) _exit(i);
    if (fork() == 0) {                        /* 자식이 손자를 남기고 먼저 죽는다 → 손자는 고아 */
        if (fork() == 0) { sleep(1); printf("  grandchild pid=%d now ppid=%d\n", getpid(), getppid()); _exit(0); }
        _exit(0);
    }
    sleep(2);
    zombies("before");
    if (reap) {
        int st; pid_t p;
        while ((p = waitpid(-1, &st, WNOHANG)) > 0)
            printf("  reaped pid=%d exit=%d\n", p, WEXITSTATUS(st));
        zombies("after");
    }
    printf("c7_pid1: PID 1 must not exit -> sync + reboot(RB_POWER_OFF)\n");
    sync();
    reboot(RB_POWER_OFF);
    perror("reboot");
    for (;;) pause();
}
```

정적 링크로 빌드한다(initramfs에 libc가 없어도 돌게). VM이 arm64라 크로스 컴파일러가 필요 없다. 커널 인자 중 `--` 뒤의 단어는 init의 `argv`로 넘어간다.

```sh
cd /var/tmp/C7
gcc -static -std=gnu11 -Wall -Wextra -O2 ~/.elinux_scratch/C7/c7_pid1.c -o c7_pid1   # 경고 0개
cp c7_pid1 r_pid1/ && (cd r_pid1 && find . | cpio -o -H newc --quiet | gzip) > c7_pid1.cpio.gz
Q="qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel /var/lab/out/Image -initrd c7_pid1.cpio.gz"
timeout 60 $Q -append "console=ttyAMA0 quiet rdinit=/c7_pid1" 2>&1 | tail -10
timeout 60 $Q -append "console=ttyAMA0 quiet rdinit=/c7_pid1 -- reap" 2>&1 | tail -16
```

```text
c7_pid1: pid=1 ppid=0 mode=noreap
  grandchild pid=56 now ppid=1
  before: pid=52 (c7_pid1) state=Z ppid=1
  before: pid=53 (c7_pid1) state=Z ppid=1
  before: pid=54 (c7_pid1) state=Z ppid=1
  before: pid=55 (c7_pid1) state=Z ppid=1
  before: pid=56 (c7_pid1) state=Z ppid=1
  before: zombie count = 5
c7_pid1: PID 1 must not exit -> sync + reboot(RB_POWER_OFF)
[    7.126818] reboot: Power down
```

```text
c7_pid1: pid=1 ppid=0 mode=reap
  grandchild pid=56 now ppid=1
  before: pid=52 (c7_pid1) state=Z ppid=1
  before: pid=53 (c7_pid1) state=Z ppid=1
  before: pid=54 (c7_pid1) state=Z ppid=1
  before: pid=55 (c7_pid1) state=Z ppid=1
  before: pid=56 (c7_pid1) state=Z ppid=1
  before: zombie count = 5
  reaped pid=52 exit=0
  reaped pid=53 exit=1
  reaped pid=54 exit=2
  reaped pid=55 exit=0
  reaped pid=56 exit=0
  after: zombie count = 0
c7_pid1: PID 1 must not exit -> sync + reboot(RB_POWER_OFF)
[    4.047860] reboot: Power down
```

출력에서 볼 것:

- `ppid=0` — PID 1의 부모는 없다(커널의 PID 0, swapper).
- `grandchild pid=56 now ppid=1` — 부모(55)가 먼저 죽자 손자가 **PID 1에 입양**됐다. 그리고 그 손자가 끝나자 PID 1의 좀비가 됐다(56이 좀비 목록에 있다). PID 1은 자기가 만들지도 않은 프로세스의 좀비까지 치워야 한다.
- noreap 모드에서는 좀비 5개가 그대로 남는다. 실제 제품에서 이게 몇 주 쌓이면 `pid_max`에 닿아 `fork: Resource temporarily unavailable`이 난다.
- 끝낼 때 `exit`가 아니라 `sync()` + `reboot(RB_POWER_OFF)`. 그래서 panic 없이 `reboot: Power down`으로 끝났다. BusyBox `poweroff`, systemd `systemctl poweroff`도 결국 이 syscall이다.

실무 연결: Docker 컨테이너 안에서 셸 스크립트나 Python 앱이 PID 1이 되면 같은 문제가 생긴다. 그래서 `tini`나 `dumb-init` 같은 "reap만 하는 작은 init"을 앞에 둔다. 센서 키트의 감독 데몬이 자식 프로세스를 많이 띄운다면 `prctl(PR_SET_CHILD_SUBREAPER, 1)`로 자기를 subreaper로 지정해 고아를 직접 거둘 수도 있다.

### 1.5 PID 1에게는 시그널 기본 동작이 없다

무엇을 확인하나: PID 1에게 `kill -9`를 보내면 어떻게 되는지. 랩의 `LAB_CMD` 기능(명령 하나 실행 후 poweroff)으로 아래 스크립트를 실행한다. 이때 PID 1은 랩 initramfs의 `/init` 셸 스크립트다.

```sh
#!/bin/sh
echo "PID 1 is: $(cat /proc/1/comm)  (me: $$)"
grep -E '^Sig(Ign|Cgt)' /proc/1/status
kill -9 1;    echo "kill -9 1   -> rc=$?"
kill -TERM 1; echo "kill -TERM 1 -> rc=$?"
sleep 0.5
echo "init still alive: $(cat /proc/1/comm), state=$(grep State /proc/1/status)"
```

```sh
timeout 60 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd c7_pid1.cpio.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=/c7_sig.sh" 2>&1 | tail -8
```

```text
lab initramfs: 6.18.55 up 2.14s
PID 1 is: init  (me: 62)
SigIgn:	0000000000000004
SigCgt:	0000000000010002
kill -9 1   -> rc=0
kill -TERM 1 -> rc=0
init still alive: init, state=State:	S (sleeping)
```

출력에서 볼 것: `kill`은 성공(rc=0)했지만 PID 1은 멀쩡하다. 커널은 PID 1의 `signal_struct`에 `SIGNAL_UNKILLABLE` 플래그를 두고, **PID 1이 핸들러를 등록하지 않은 시그널은 같은 PID 네임스페이스 안에서 보낸 경우 버린다**(`kernel/signal.c`의 `sig_task_ignored()`). `SIGKILL`·`SIGSTOP`은 핸들러를 걸 수 없고, 전역 init에게는 아예 전달되지 않는다. `SigCgt` 비트맵 `0x10002`는 비트 1(SIGINT=2)과 비트 16(SIGCHLD=17)이다 — 이 셸은 SIGTERM을 잡지 않으므로 SIGTERM도 무시됐다.

그래서 init 구현은 종료 요청을 **시그널 핸들러로** 받는다. BusyBox init은 SIGUSR1=halt, SIGUSR2=poweroff, SIGTERM=reboot, SIGINT=Ctrl-Alt-Del, SIGHUP=inittab 다시 읽기를 잡는다(§2.2). systemd도 SIGTERM, SIGINT 등을 잡아 각각 다른 동작에 매핑한다.

---

## 2. init 구현 세 가지 — 직접 쓴 `/init`, BusyBox init, systemd

| 항목 | 직접 쓴 `/init` 스크립트 | BusyBox init + inittab | systemd |
|---|---|---|---|
| 크기 | 셸 스크립트 수십 줄 | BusyBox 바이너리에 포함 (수 KB) | 수 MB + libsystemd + dbus 등 |
| 서비스 순서 | 스크립트에 쓴 순서 | sysinit → wait → once → respawn | unit 의존성 그래프 (병렬) |
| 재시작 | 직접 루프를 짜야 함 | `respawn` 액션 | `Restart=`, `WatchdogSec=` |
| 좀비 회수 | 셸이 자기 자식만 | 함 | 함 |
| 로그 | stdout 그대로 | syslogd(선택) | journald |
| 부팅 시간 분석 | 직접 타임스탬프 | 직접 타임스탬프 | `systemd-analyze` 내장 |
| 맞는 곳 | initramfs, 단일 목적 장비, 복구 모드 | 작은 rootfs(수 MB), 센서 노드 | 서비스 많은 게이트웨이, Jetson·i.MX8 급 |

### 2.1 직접 쓴 `/init` — 랩 initramfs의 예

랩의 `/var/lab/build/rootfs/init`이 가장 작은 실전 예다.

```sh
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null
mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null
echo "lab initramfs: $(uname -r) up $(cut -d' ' -f1 /proc/uptime)s"
# LAB_CMD=... on the kernel cmdline runs one command then powers off (for scripted examples)
for a in $(cat /proc/cmdline); do case $a in LAB_CMD=*) C="${a#LAB_CMD=}";; esac; done
if [ -n "$C" ]; then sh -c "$(echo "$C" | tr '~' ' ')"; poweroff -f; fi
exec /bin/sh
```

PID 1의 책임 ①(가상 FS 마운트)과 ⑥(끝나지 않기: 마지막에 `exec /bin/sh`, 또는 `poweroff -f`)을 지킨다. 셸이 PID 1이니 ②(좀비 회수)는 셸이 `wait`하는 자기 자식에 한해서만 된다. 커널 인자를 직접 파싱하는 `for a in $(cat /proc/cmdline)` 패턴은 initramfs 스크립트에서 매우 흔하다(`root=`, `rescue` 같은 인자를 읽는다).

### 2.2 BusyBox init과 `/etc/inittab`

**직관**: BusyBox init은 "inittab에 적힌 표를 보고 프로세스를 띄우고, 죽으면 다시 띄우고, 끄라는 시그널이 오면 정리하는" 작은 슈퍼바이저다. RTOS에서 태스크 테이블을 돌면서 태스크를 생성하는 부트 코드와 비슷하다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="124" height="74" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="72.0" y="43.7" text-anchor="middle" font-size="13" font-weight="bold">sysinit</text>
  <text x="72.0" y="61.7" text-anchor="middle" font-size="13">rcS 실행</text>
  <text x="72.0" y="79.7" text-anchor="middle" font-size="13">끝날 때까지 대기</text>
  <line x1="135.0" y1="57.0" x2="143.0" y2="57.0" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="144" y="20" width="124" height="74" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="206.0" y="43.7" text-anchor="middle" font-size="13" font-weight="bold">wait</text>
  <text x="206.0" y="61.7" text-anchor="middle" font-size="13">한 번 실행</text>
  <text x="206.0" y="79.7" text-anchor="middle" font-size="13">끝날 때까지 대기</text>
  <line x1="269.0" y1="57.0" x2="277.0" y2="57.0" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="278" y="20" width="124" height="74" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340.0" y="43.7" text-anchor="middle" font-size="13" font-weight="bold">once</text>
  <text x="340.0" y="61.7" text-anchor="middle" font-size="13">한 번 실행</text>
  <text x="340.0" y="79.7" text-anchor="middle" font-size="13">기다리지 않음</text>
  <line x1="403.0" y1="57.0" x2="411.0" y2="57.0" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="412" y="20" width="124" height="74" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="474.0" y="43.7" text-anchor="middle" font-size="13" font-weight="bold">respawn</text>
  <text x="474.0" y="61.7" text-anchor="middle" font-size="13">실행 + 감시</text>
  <text x="474.0" y="79.7" text-anchor="middle" font-size="13">죽으면 다시 실행</text>
  <line x1="537.0" y1="57.0" x2="545.0" y2="57.0" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="546" y="20" width="124" height="74" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="608.0" y="43.7" text-anchor="middle" font-size="13" font-weight="bold">askfirst</text>
  <text x="608.0" y="61.7" text-anchor="middle" font-size="13">Enter 대기 후</text>
  <text x="608.0" y="79.7" text-anchor="middle" font-size="13">실행 + 재실행</text>
  <text x="340.0" y="120.0" text-anchor="middle" font-size="13">부팅 단계: 위에서 아래가 아니라 액션 종류 순서로 실행된다</text>
  <line x1="10.0" y1="136.0" x2="670.0" y2="136.0" stroke="currentColor" stroke-dasharray="4 4"/>
  <rect x="10" y="152" width="200" height="54" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110.0" y="174.7" text-anchor="middle" font-size="13" font-weight="bold">SIGUSR2 / SIGTERM</text>
  <text x="110.0" y="192.7" text-anchor="middle" font-size="13">poweroff / reboot</text>
  <line x1="211.0" y1="179.0" x2="239.0" y2="179.0" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="240" y="152" width="200" height="54" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340.0" y="174.7" text-anchor="middle" font-size="13" font-weight="bold">shutdown 액션 실행</text>
  <text x="340.0" y="192.7" text-anchor="middle" font-size="13">umount -a -r 등</text>
  <line x1="441.0" y1="179.0" x2="469.0" y2="179.0" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="470" y="152" width="200" height="54" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570.0" y="174.7" text-anchor="middle" font-size="13" font-weight="bold">SIGTERM → SIGKILL</text>
  <text x="570.0" y="192.7" text-anchor="middle" font-size="13">reboot(RB_POWER_OFF)</text>
  <text x="340.0" y="236.0" text-anchor="middle" font-size="13">SIGINT(Ctrl-Alt-Del) → ctrlaltdel 액션 · SIGHUP → inittab 다시 읽기</text>
  <text x="340.0" y="258.0" text-anchor="middle" font-size="13">&lt;tty&gt;::&lt;action&gt;:&lt;process&gt; — tty가 비면 /dev/console</text>
</svg>
```
그림 3 — BusyBox init의 처리 순서. inittab 줄 순서가 아니라 **액션 종류 순서**(sysinit → wait → once → respawn/askfirst)로 실행된다. 종료 시그널을 받으면 shutdown 액션 → 모든 프로세스에 SIGTERM, SIGKILL → `reboot()`.

형식은 `<tty>::<action>:<process>`이다. tty 칸이 비어 있으면 `/dev/console`을 쓴다. 센서 키트를 흉내 낸 inittab을 만든다.

```text
# /etc/inittab  (BusyBox init 형식: <tty>::<action>:<process>)
::sysinit:/etc/init.d/rcS
::respawn:/usr/bin/c7_sensord
ttyAMA0::respawn:/sbin/getty -L 115200 ttyAMA0 vt100
::ctrlaltdel:/sbin/reboot
::shutdown:/bin/echo "C7 shutdown: stopping c7_sensord, syncing"
::shutdown:/bin/umount -a -r
```

`/etc/init.d/rcS`(sysinit)와 가짜 센서 데몬:

```sh
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null
mkdir -p /dev/pts /run && mount -t devpts devpts /dev/pts
hostname c7-board
echo "C7_READY rcS done uptime=$(cut -d' ' -f1 /proc/uptime)"
```

```sh
#!/bin/sh
echo "c7_sensord: started pid=$$"
while true; do sleep 5; done
```

`/etc/passwd`에는 `root::0:0:root:/root:/bin/sh` 한 줄(비밀번호 없음 — 실습용)을 넣고, 랩 rootfs 복사본에 이 파일들을 얹어 cpio로 묶는다. `/sbin/init`은 `../bin/busybox` 심볼릭 링크이고, `rdinit=/sbin/init`으로 initramfs의 `/init` 대신 BusyBox init을 PID 1로 쓴다. 대화형 콘솔은 `expect`로 자동화한다: 로그인 → 데몬을 죽여 respawn 확인 → 셸 exit로 getty respawn 확인 → `poweroff`로 shutdown 액션 확인.

```text
set timeout 60
spawn qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel /var/lab/out/Image -initrd /var/tmp/C7/c7_bbinit.cpio.gz -append "console=ttyAMA0 quiet rdinit=/sbin/init"
expect "login:"
send "root\r"
expect "# "
send "ps -o pid,ppid,comm,args\r"
expect "# "
send "pidof c7_sensord; kill \$(pidof -s sleep) \$(pgrep -f c7_sensord); sleep 1; pgrep -f c7_sensord\r"
expect "# "
send "echo my shell pid=\$\$; exit\r"
expect "login:"
send "root\r"
expect "# "
send "echo new shell pid=\$\$; poweroff\r"
expect "Power down"
```

```text
C7_READY rcS done uptime=4.70
c7_sensord: started pid=60

c7-board login: root
login[62]: root login on 'ttyAMA0'
~ # ps -o pid,ppid,comm,args
PID   PPID  COMMAND          COMMAND
    1     0 init             init
    2     0 kthreadd         [kthreadd]
...  (커널 스레드 20여 줄 생략)
   25     2 pr/ttyAMA-1      [pr/ttyAMA-1]
...  (커널 스레드 20여 줄 생략)
   60     1 c7_sensord       {c7_sensord} /bin/sh /usr/bin/c7_sensord
   61    60 sleep            sleep 5
   62     1 sh               -sh
   63    62 ps               ps -o pid,ppid,comm,args
~ # pidof c7_sensord; kill $(pidof -s sleep) $(pgrep -f c7_sensord); sleep 1; pg
rep -f c7_sensord
60
Terminated
c7_sensord: started pid=69
69
~ # echo my shell pid=$$; exit
my shell pid=62

c7-board login: root
login[72]: root login on 'ttyAMA0'
~ # echo new shell pid=$$; poweroff
new shell pid=72
~ # C7 shutdown: stopping c7_sensord, syncing
umount: devtmpfs busy - remounted read-only
The system is going down NOW!
Sent SIGTERM to all processes
Sent SIGKILL to all processes
Requesting system poweroff
```

출력에서 볼 것:

- `C7_READY`가 getty보다 먼저 찍혔다 — sysinit이 끝나야 respawn 단계로 간다.
- 데몬(PID 60)을 죽이자 곧바로 `c7_sensord: started pid=69`. respawn이 새 PID로 다시 띄웠다. 셸(PID 62)을 exit하자 getty가 다시 `login:`을 띄웠고, 다시 로그인한 셸은 PID 72다. getty는 `login`을 exec하고 login은 셸을 exec하므로 PID가 이어진다(`login[72]` → `new shell pid=72`).
- `poweroff`(=init에 SIGUSR2) → shutdown 액션 두 줄 실행 → `Sent SIGTERM`/`SIGKILL to all processes` → `reboot()`. 그림 3의 아래 줄 그대로다.

함정: respawn 대상이 즉시 죽는 프로그램이면(예: 경로 오타) BusyBox init은 빠르게 다시 띄우기를 반복한다. 로그가 콘솔을 덮어 원인을 못 보는 일이 흔하다. 개발 중에는 respawn 대신 once로 먼저 확인한다.

### 2.3 systemd — unit, target, 의존성 그래프

**직관**: BusyBox init이 "표를 위에서부터 실행"이라면 systemd는 "**의존성 그래프를 만들고 병렬로 실행**"한다. 각 서비스는 **unit 파일**로 기술하고, 여러 unit을 묶는 동기화 지점이 **target**이다. RTOS 비유로는 이벤트 플래그를 기다리는 태스크들 — `network-online.target`이라는 플래그가 서면 그것을 `After=`로 기다리던 서비스들이 동시에 출발한다.

핵심 용어:

| 지시어 | 뜻 | 주의 |
|---|---|---|
| `Wants=` | 같이 시작해 달라 (실패해도 나는 계속) | 순서는 정하지 않는다 |
| `Requires=` | 같이 시작, 그게 실패·중지되면 나도 | 순서는 정하지 않는다 |
| `After=` / `Before=` | 시작 **순서** | 끌어오지는 않는다 — 보통 Wants와 같이 쓴다 |
| `WantedBy=` (`[Install]`) | `systemctl enable` 때 그 target의 `.wants/`에 링크 | 부팅 때 끌려 들어가는 방법 |
| `Type=notify` | 서비스가 `sd_notify("READY=1")` 해야 "시작됨" | 진짜 준비 완료 시점을 알려 준다 |
| `WatchdogSec=` | 이 주기 안에 `WATCHDOG=1`을 안 보내면 재시작 | SW 워치독 (L1, N 모듈) |

센서 데몬용 unit을 써서 문법을 검사해 본다(VM 호스트에서, 설치하지 않고 검사만).

```text
[Unit]
Description=C7 sensor kit daemon (IMU + GNSS)
After=dev-ttyAMA1.device network-online.target
Wants=network-online.target
StartLimitIntervalSec=30
StartLimitBurst=5

[Service]
Type=notify
ExecStart=/usr/bin/sleep infinity
Restart=on-failure
RestartSec=200ms
WatchdogSec=2s

[Install]
WantedBy=multi-user.target
```

```sh
cd /var/tmp/C7 && systemd-analyze verify ./c7-sensord.service; echo "verify rc=$?"
systemd-analyze security --offline=yes ./c7-sensord.service 2>/dev/null | tail -1
```

```text
/usr/lib/systemd/system/xfs_scrub_all.service:26: Support for option CPUAccounting= has been removed and it is ignored
/usr/lib/systemd/system/system-xfs_scrub.slice:15: Support for option CPUAccounting= has been removed and it is ignored
verify rc=0
→ Overall exposure level for c7-sensord.service: 9.4 UNSAFE :-{
```

출력에서 볼 것: 내 unit은 통과(rc=0)했고, 경고 두 줄은 호스트에 설치된 다른 unit의 것이다(verify는 의존성으로 끌려오는 unit도 읽는다). `security` 점수 9.4는 샌드박싱 옵션(`ProtectSystem=`, `NoNewPrivileges=` 등)을 하나도 안 썼다는 뜻이다 — 국방·보안 장비라면 L 모듈에서 다룬다. `After=dev-ttyAMA1.device`는 "GNSS UART 장치 노드가 생긴 뒤"라는 뜻이다(udev가 만든 device unit).

### 2.4 systemd-analyze로 실제 부팅 해부하기 (VM 호스트)

무엇을 확인하나: systemd가 기록한 부팅 구간(kernel / initrd / userspace), 오래 걸린 unit, 그리고 "최종 target까지 가는 가장 긴 경로(critical chain)".

```sh
systemd-analyze
systemd-analyze blame | head -12
systemd-analyze critical-chain --no-pager | head -30
systemd-analyze plot > /var/tmp/C7/c7_plot.svg
```

```text
Startup finished in 151ms (kernel) + 1.261s (initrd) + 5.023s (userspace) = 6.437s
graphical.target reached after 2.169s in userspace.
```

```text
2.853s cloud-final.service
1.477s dev-rfkill.device
1.477s sys-devices-virtual-misc-rfkill.device
1.463s dev-vport1p0.device
1.463s sys-devices-pci0000:00-0000:00:05.0-virtio1-virtio\x2dports-vport1p0.device
1.386s dev-disk-by\x2ddiskseq-9\x2dpart15.device
...
```

```text
graphical.target @2.169s
└─multi-user.target @2.169s
  └─snapd.seeded.service @1.888s +280ms
    └─basic.target @1.865s
      └─sockets.target @1.865s
        └─sshd-vsock.socket @1.861s +3ms
          └─sysinit.target @1.848s
            └─cloud-init-network.service @1.372s +475ms
              └─cloud-init-local.service @1.292s +79ms
                └─cloud-init-main.service @206ms +263ms
                  └─systemd-remount-fs.service @193ms +9ms
                    └─systemd-fsck-root.service
                      └─dracut-pre-mount.service
                        └─cryptsetup.target @290ms
                          └─systemd-ask-password-console.path @290ms
```

`plot`은 201,998바이트짜리 SVG(사각형 839개 — unit 하나당 막대)를 만들었다. 브라우저로 열면 unit별 간트 차트다. 그 안의 핵심만 journald의 단조 시간(`journalctl -b -o short-monotonic`)과 target의 `After=` 값을 뽑아 다시 그리면 이렇다.

```sh
for t in sysinit basic multi-user graphical; do
  echo "$t.target: $(systemctl show -p After --value $t.target | tr ' ' '\n' | grep -E 'target$' | tr '\n' ' ')"
done
sudo journalctl -b -o short-monotonic --no-pager | grep -E "Switching root|Reached target" | ...   # 필요한 줄만
```

```text
sysinit.target: local-fs.target cryptsetup.target imports.target swap.target integritysetup.target veritysetup.target
basic.target: slices.target sockets.target paths.target sysinit.target
multi-user.target: rescue.target basic.target getty.target
graphical.target: rescue.target multi-user.target
```

```text
[    1.258578] lima-elinux systemd[1]: Reached target initrd-root-fs.target - Initrd Root File System.
[    1.275891] lima-elinux systemd[1]: Reached target initrd.target - Initrd Default Target.
[    1.354281] lima-elinux systemd[1]: Reached target initrd-switch-root.target - Switch Root.
[    1.360336] lima-elinux systemd[1]: Switching root.
[    3.261981] lima-elinux systemd[1]: Reached target sysinit.target - System Initialization.
[    3.279117] lima-elinux systemd[1]: Reached target basic.target - Basic System.
[    3.582691] lima-elinux systemd[1]: Reached target multi-user.target - Multi-User System.
[    3.582836] lima-elinux systemd[1]: Reached target graphical.target - Graphical Interface.
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10.0" y="18.0" font-size="13" font-weight="bold">initrd 단계 (dracut + systemd)</text>
  <rect x="10" y="28" width="150" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="85.0" y="46.8" text-anchor="middle" font-size="12" font-weight="bold">initrd-root-fs</text>
  <text x="85.0" y="63.8" text-anchor="middle" font-size="12">@1.259 s</text>
  <line x1="161.0" y1="51.0" x2="179.0" y2="51.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="180" y="28" width="150" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="255.0" y="46.8" text-anchor="middle" font-size="12" font-weight="bold">initrd.target</text>
  <text x="255.0" y="63.8" text-anchor="middle" font-size="12">@1.276 s</text>
  <line x1="331.0" y1="51.0" x2="349.0" y2="51.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="350" y="28" width="150" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="425.0" y="46.8" text-anchor="middle" font-size="12" font-weight="bold">initrd-switch-root</text>
  <text x="425.0" y="63.8" text-anchor="middle" font-size="12">@1.354 s</text>
  <line x1="501.0" y1="51.0" x2="519.0" y2="51.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="520" y="28" width="150" height="46" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="595.0" y="46.8" text-anchor="middle" font-size="12" font-weight="bold">Switching root</text>
  <text x="595.0" y="63.8" text-anchor="middle" font-size="12">@1.360 s</text>
  <text x="10.0" y="104.0" font-size="13" font-weight="bold">진짜 루트 단계 (After= 관계, 화살표 = 먼저 끝나야 함)</text>
  <rect x="10" y="116" width="150" height="26" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85.0" y="133.3" text-anchor="middle" font-size="12">local-fs.target</text>
  <line x1="161.0" y1="129.0" x2="199.0" y2="166.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="10" y="146" width="150" height="26" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85.0" y="163.3" text-anchor="middle" font-size="12">cryptsetup.target</text>
  <line x1="161.0" y1="159.0" x2="199.0" y2="166.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="10" y="176" width="150" height="26" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85.0" y="193.3" text-anchor="middle" font-size="12">swap.target</text>
  <line x1="161.0" y1="189.0" x2="199.0" y2="166.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="200" y="146" width="130" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="265.0" y="164.8" text-anchor="middle" font-size="12" font-weight="bold">sysinit.target</text>
  <text x="265.0" y="181.8" text-anchor="middle" font-size="12">@3.262 s</text>
  <rect x="10" y="216" width="150" height="26" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85.0" y="233.3" text-anchor="middle" font-size="12">sockets.target</text>
  <line x1="161.0" y1="229.0" x2="349.0" y2="192.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="10" y="246" width="150" height="26" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85.0" y="263.3" text-anchor="middle" font-size="12">paths · slices</text>
  <line x1="161.0" y1="259.0" x2="349.0" y2="192.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="331.0" y1="169.0" x2="349.0" y2="169.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="350" y="146" width="130" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="415.0" y="164.8" text-anchor="middle" font-size="12" font-weight="bold">basic.target</text>
  <text x="415.0" y="181.8" text-anchor="middle" font-size="12">@3.279 s</text>
  <line x1="481.0" y1="169.0" x2="519.0" y2="169.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="520" y="146" width="150" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595.0" y="164.8" text-anchor="middle" font-size="12" font-weight="bold">multi-user.target</text>
  <text x="595.0" y="181.8" text-anchor="middle" font-size="12">@3.583 s</text>
  <rect x="520" y="216" width="150" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595.0" y="234.8" text-anchor="middle" font-size="12" font-weight="bold">graphical.target</text>
  <text x="595.0" y="251.8" text-anchor="middle" font-size="12">@3.583 s</text>
  <line x1="595.0" y1="193.0" x2="595.0" y2="214.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="350" y="216" width="130" height="46" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="415.0" y="234.8" text-anchor="middle" font-size="12">getty.target</text>
  <text x="415.0" y="251.8" text-anchor="middle" font-size="12">+ 각종 서비스</text>
  <line x1="481.0" y1="239.0" x2="540.0" y2="194.0" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="340.0" y="296.0" text-anchor="middle" font-size="12">systemd-analyze: kernel 151 ms + initrd 1.261 s + userspace 5.023 s = 6.437 s</text>
  <text x="340.0" y="318.0" text-anchor="middle" font-size="12">graphical.target은 userspace 2.169 s에 도달 — 나머지는 cloud-final 같은 늦은 oneshot</text>
</svg>
```
그림 5 — VM 호스트(Ubuntu, systemd 259)의 실제 부팅에서 뽑은 target 그래프. 위는 initramfs 안의 systemd(dracut), 아래는 `switch_root` 후 진짜 루트의 systemd. 시간은 커널 부팅 기준 단조 시간이다.

출력에서 볼 것:

- 숫자 맞추기: kernel 0.151 + initrd 1.261 = 1.412 s에 userspace가 시작되고, 거기에 2.169 s를 더하면 3.581 s — journal의 `graphical.target` 도달 시각 3.583 s와 맞는다.
- "Startup finished … 6.437s"는 graphical.target 도달(3.58 s)이 아니라 **모든 부팅 job이 끝난 시각**이다. 그 차이는 `cloud-final.service`(2.853 s) 같은 늦은 oneshot이다. 장비에서 "부팅 완료"를 정의할 때 이 둘을 헷갈리면 안 된다.
- `blame`은 "오래 걸린 순"일 뿐 critical path가 아니다. `.device` unit 1.4 s는 그 장치가 나타나기를 기다린 시간이고 CPU를 쓴 시간이 아니다. 줄일 대상은 `critical-chain`에 나오는 unit이다 (`+` 뒤가 그 unit의 자체 시간, `@` 뒤가 활성화된 시각).

임베디드에서의 의미: systemd 보드에서 부팅 단축의 첫 단계는 항상 이 세 명령이다. `systemd-analyze`는 타깃 보드에서도 그대로 돈다(Yocto의 systemd 이미지에 포함). systemd 자체의 깊은 내용(unit 타입, journald, watchdog 연동)은 L1에서 다룬다.

---

## 3. initramfs — 진짜 rootfs를 찾아서 `switch_root`

### 3.1 initramfs가 뭔가

**직관**: 부트로더가 커널과 함께 메모리에 올려 주는 **작은 임시 루트 파일시스템**이다. 커널은 이것을 RAM 위의 `rootfs`(tmpfs/ramfs)에 풀고 그 안의 `/init`을 PID 1로 실행한다. 진짜 rootfs(eMMC, NVMe, 네트워크)를 마운트하기 위해 필요한 "준비 작업"을 유저 공간 도구로 할 수 있게 해 주는 **early userspace**다.

**정의**:

- 형식: `cpio` 아카이브(`newc` 형식)를 원하는 압축기로 압축한 것. 커널 이미지에 내장(`CONFIG_INITRAMFS_SOURCE`)하거나 부트로더가 따로 넘긴다(`-initrd`, U-Boot `booti`의 두 번째 인자).
- 옛 **initrd**(블록 장치 이미지 + `/linuxrc` + `pivot_root`)와 다르다. initramfs는 파일시스템 드라이버 없이 커널이 cpio를 직접 tmpfs에 풀기만 한다. 랩 rootfs의 `linuxrc` 심볼릭 링크는 그 시절 흔적이다.
- 랩 커널 설정 확인:

```sh
grep -E "BLK_DEV_INITRD|INITRAMFS_SOURCE|RD_|DEVTMPFS=|VIRTIO_BLK=|EXT4_FS=" /var/lab/out/linux-build/.config
```

```text
CONFIG_BLK_DEV_INITRD=y
CONFIG_INITRAMFS_SOURCE=""
CONFIG_RD_GZIP=y
CONFIG_RD_BZIP2=y
CONFIG_RD_LZMA=y
CONFIG_RD_XZ=y
CONFIG_RD_LZO=y
CONFIG_RD_LZ4=y
CONFIG_RD_ZSTD=y
CONFIG_DEVTMPFS=y
CONFIG_VIRTIO_BLK=y
CONFIG_EXT4_FS=y
```

`RD_*`가 커널이 풀 수 있는 initramfs 압축 형식이다. 일곱 가지 다 켜져 있으니 §5.3에서 커널을 다시 빌드하지 않고 cpio의 압축만 바꿔 비교할 수 있다.

### 3.2 initramfs는 왜 쓰나 — 네 가지 역할

| 역할 | 예 (센서 키트·실제 제품) | initramfs 없이 되나 |
|---|---|---|
| 모듈 로드 | rootfs가 있는 스토리지 드라이버가 모듈(`.ko`)일 때 | 드라이버를 `=y`로 넣으면 된다 |
| 루트 찾기 | `root=UUID=…`/`LABEL=` 해석, A/B 슬롯 중 어느 쪽인지 결정, USB 지연 대기 | `root=PARTUUID=`은 커널이 직접 한다 |
| 루트 풀기 | dm-crypt(LUKS) 해독 키를 TPM·OP-TEE에서 받기, dm-verity 루트 해시 검증, LVM, RAID | 불가 — 유저 공간 도구가 필요 |
| 복구·업데이트 | 루트가 깨졌을 때 복구 셸, 공장 플래싱 모드, fsck | 어렵다 |

그리고 **initramfs 자체가 최종 rootfs**인 장비도 있다. 랩이 그렇고, 수 MB짜리 펌웨어 장비(라우터, 작은 센서 노드)는 squashfs도 없이 initramfs 하나로 끝낸다. 그 대가는 RAM 사용량과 매 부팅 압축 해제 시간이다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="200" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/>
  <text x="110.0" y="29.7" text-anchor="middle" font-size="13" font-weight="bold">커널 (PID 0 → 1)</text>
  <rect x="240" y="10" width="200" height="30" rx="5" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/>
  <text x="340.0" y="29.7" text-anchor="middle" font-size="13" font-weight="bold">initramfs /init (PID 1)</text>
  <rect x="470" y="10" width="200" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/>
  <text x="570.0" y="29.7" text-anchor="middle" font-size="13" font-weight="bold">진짜 rootfs (PID 1)</text>
  <rect x="10" y="54" width="200" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110.0" y="73.3" text-anchor="middle" font-size="12">cpio 풀기 → rootfs(tmpfs)</text>
  <rect x="10" y="92" width="200" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110.0" y="111.3" text-anchor="middle" font-size="12">드라이버 probe · initcall</text>
  <rect x="10" y="130" width="200" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110.0" y="149.3" text-anchor="middle" font-size="12">/init 실행 (rdinit=)</text>
  <rect x="240" y="168" width="200" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340.0" y="187.3" text-anchor="middle" font-size="12">proc·sys·dev 마운트</text>
  <rect x="240" y="206" width="200" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340.0" y="225.3" text-anchor="middle" font-size="12">insmod: 스토리지·dm 모듈</text>
  <rect x="240" y="244" width="200" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340.0" y="263.3" text-anchor="middle" font-size="12">root 장치 대기 · 해독 · fsck</text>
  <rect x="240" y="282" width="200" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340.0" y="301.3" text-anchor="middle" font-size="12">mount /dev/vda1 /newroot</text>
  <rect x="240" y="320" width="200" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340.0" y="339.3" text-anchor="middle" font-size="12">mount --move proc sys dev</text>
  <rect x="240" y="358" width="200" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340.0" y="377.3" text-anchor="middle" font-size="12">exec switch_root</text>
  <rect x="470" y="358" width="200" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="570.0" y="377.3" text-anchor="middle" font-size="12">initramfs 파일 삭제 · chroot</text>
  <rect x="470" y="396" width="200" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="570.0" y="415.3" text-anchor="middle" font-size="12">exec /sbin/init → inittab</text>
  <line x1="211.0" y1="145.0" x2="239.0" y2="183.0" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="441.0" y1="373.0" x2="469.0" y2="373.0" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="455.0" y="412.0" text-anchor="end" font-size="12">PID 유지</text>
</svg>
```
그림 4 — initramfs → `switch_root` 순서. 세 칸 모두 PID 1이다. initramfs의 `/init`이 `exec switch_root`하고, switch_root가 다시 `exec /sbin/init`하므로 PID는 바뀌지 않는다.

### 3.3 직접 만들기 — virtio 디스크의 ext4로 switch_root

구성: (1) 작은 initramfs: BusyBox + `/init` 스크립트 + `dm-mod.ko`(모듈 로드 역할 시연용), (2) 160 MiB 디스크 이미지: MBR 파티션 1개 = ext4 진짜 rootfs. 이 rootfs의 `/boot`에는 커널과 initramfs, U-Boot용 `extlinux.conf`도 넣어 §4에서 U-Boot 부팅에 그대로 쓴다.

initramfs의 `/init`:

```sh
#!/bin/sh
# initramfs /init: early userspace -> find real root -> switch_root
mount -t proc proc /proc; mount -t sysfs sysfs /sys; mount -t devtmpfs devtmpfs /dev
echo "[initramfs] PID $$ up $(cut -d' ' -f1 /proc/uptime)s, rootfs type: $(awk '$2=="/"{print $3}' /proc/mounts)"
insmod /lib/modules/dm-mod.ko && echo "[initramfs] loaded dm-mod (would unlock dm-crypt/dm-verity here)"
for a in $(cat /proc/cmdline); do case $a in root=*) ROOT=${a#root=};; esac; done
n=0; while [ ! -b "$ROOT" ] && [ $n -lt 50 ]; do sleep 0.1; n=$((n+1)); done
[ -b "$ROOT" ] || { echo "[initramfs] $ROOT not found"; exec sh; }
mkdir -p /newroot
mount -t ext4 -o ro "$ROOT" /newroot || { echo "[initramfs] mount failed"; exec sh; }
echo "[initramfs] mounted $ROOT ($(blkid $ROOT | sed 's/.*TYPE=/TYPE=/')), switching root"
mount --move /proc /newroot/proc; mount --move /sys /newroot/sys; mount --move /dev /newroot/dev
exec switch_root /newroot /sbin/init
```

포인트:

- `root=`는 커널도 읽는 인자지만 initramfs에 `/init`이 있으면 커널은 루트를 마운트하지 않는다. 그 값을 `/init`이 `/proc/cmdline`에서 읽어 직접 쓴다.
- 장치를 최대 5초 기다린다(`sleep 0.1` × 50). USB·SD·PCIe 스토리지는 probe가 늦게 끝날 수 있어서 실제 initramfs들(dracut, Yocto의 initramfs-framework)도 이런 대기 루프를 갖는다. 커널만 쓰는 경우의 같은 기능이 `rootwait`다.
- 실패하면 `exit`가 아니라 `exec sh` — §1.3의 panic을 피하고 복구 셸을 준다.
- `ro`로 마운트하고 진짜 init의 rcS가 `remount,rw`. fsck를 하려면 이 사이에 한다.
- `mount --move`로 `/proc`, `/sys`, `/dev`를 새 루트로 옮긴다. BusyBox `switch_root`는 옮기지 않은 마운트를 대신 정리해 주지 않는다.

진짜 루트의 `/etc/init.d/rcS`(inittab은 §2.2와 같은 형식, sysinit + getty + shutdown):

```sh
#!/bin/sh
# real rootfs (ext4 on /dev/vda1): proc/sys/dev were moved here by the initramfs
mount -o remount,rw /
echo "[realroot] / is now: $(grep ' / ' /proc/mounts | grep -v rootfs)"
echo "[realroot] PID 1 = $(cat /proc/1/comm), marker: $(cat /etc/c7_realroot)"
echo "[realroot] MemFree after switch_root: $(grep MemFree /proc/meminfo | tr -s ' ')"
/usr/bin/c7_cntvct "C7_READY realroot"
grep -q c7_auto /proc/cmdline && (sleep 0.3; poweroff) &
```

(`c7_cntvct`는 §4.2에서 만드는 "리셋 이후 시간" 측정 프로그램이다. `c7_auto` 인자가 있으면 자동으로 꺼져서 측정 스크립트로 반복 실행할 수 있다.)

디스크와 initramfs를 만드는 스크립트 — `mkfs.ext4 -d`가 디렉터리 내용을 그대로 담은 ext4를 **루트 권한·loop 장치 없이** 만들고, `-E offset=`으로 파티션 위치(1 MiB)에 바로 쓴다:

```sh
#!/bin/bash
set -e; cd /var/tmp/C7; S=/Users/donh/.elinux_scratch/C7/sr
rm -rf ir rr && mkdir -p ir/lib/modules rr
cp -a /var/lab/build/rootfs/. ir/ && rm -f ir/init && cp $S/c7_initramfs_init ir/init
cp /var/lab/build/linux/drivers/md/dm-mod.ko ir/lib/modules/
(cd ir && find . | cpio -o -H newc --quiet | gzip -9) > c7_sr_initrd.gz
cp -a /var/lab/build/rootfs/. rr/ && rm -f rr/init && cp -a $S/rr/. rr/ && mkdir -p rr/root rr/boot/extlinux
gcc -static -O2 -Wall -Wextra /Users/donh/.elinux_scratch/C7/c7_cntvct.c -o rr/usr/bin/c7_cntvct && cp /var/lab/out/Image rr/boot/Image && gzip -9 -c /var/lab/out/Image > rr/boot/Image.gz && cp c7_sr_initrd.gz rr/boot/initrd.gz && cp $S/extlinux.conf rr/boot/extlinux/
rm -f c7_disk.img && truncate -s 160M c7_disk.img
echo "label: dos" > p.sfd; echo "start=2048, type=83, bootable" >> p.sfd; sfdisk -q c7_disk.img < p.sfd
mkfs.ext4 -q -F -L c7root -d rr -E offset=1048576 c7_disk.img 162816k
ls -l c7_sr_initrd.gz c7_disk.img; sfdisk -l c7_disk.img | tail -2
```

```text
-rw-rw-r-- 1 donh donh 167772160 Oct 10 10:38 c7_disk.img
-rw-rw-r-- 1 donh donh   1497883 Oct 10 10:38 c7_sr_initrd.gz
Device       Boot Start    End Sectors  Size Id Type
c7_disk.img1 *     2048 327679  325632  159M 83 Linux
```

부팅(커널·initramfs는 QEMU가 직접 올리고, 디스크는 virtio-blk로 붙인다):

```sh
timeout 90 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd c7_sr_initrd.gz \
  -drive file=c7_disk.img,format=raw,if=none,id=d0 -device virtio-blk-device,drive=d0 \
  -append "console=ttyAMA0 quiet root=/dev/vda1 c7_auto" 2>&1 | tr -d '\r' | tail -16
```

```text
[initramfs] PID 1 up 0.68s, rootfs type: rootfs
[initramfs] loaded dm-mod (would unlock dm-crypt/dm-verity here)
[initramfs] mounted /dev/vda1 (TYPE="ext4"), switching root
[realroot] / is now: /dev/vda1 / ext4 rw,relatime 0 0
[realroot] PID 1 = init, marker: ext4 rootfs on virtio disk (C7)
[realroot] MemFree after switch_root: MemFree: 445732 kB
C7_READY realroot since_reset=1.117 uptime=1.000 pre_kernel=0.117 (cntfrq=62500000 Hz)

(none) login: umount: devtmpfs busy - remounted read-only
The system is going down NOW!
Sent SIGTERM to all processes
Sent SIGKILL to all processes
Requesting system poweroff
[    3.450482] reboot: Power down
```

출력에서 볼 것:

- initramfs 단계에서 `/`의 파일시스템 타입은 `rootfs`(커널 내부의 tmpfs 인스턴스), switch_root 후에는 `/dev/vda1 / ext4`.
- 두 단계 모두 PID 1이다 — `exec`만 했으므로.
- 0.68 s에 initramfs `/init` 시작, 1.00 s에 진짜 루트의 "READY". 즉 모듈 로드 + 디스크 대기 + 마운트 + switch_root + rcS가 약 0.3 s다.

### 3.4 `switch_root`가 실제로 하는 일, `pivot_root`와의 차이

BusyBox `switch_root NEWROOT NEW_INIT`의 동작:

1. 현재 `/`가 `rootfs`(ramfs/tmpfs)인지, 자기가 PID 1인지 확인한다.
2. **initramfs 안의 파일을 전부 지운다**(같은 파일시스템 안에서만, 재귀). initramfs는 RAM에 있으니 지워야 메모리가 돌아온다.
3. `chdir(NEWROOT)` → `mount --move NEWROOT /` → `chroot(".")`.
4. `exec NEW_INIT`.

왜 `pivot_root(2)`를 안 쓰나: `rootfs`는 커널이 처음 만든 특별한 마운트라 떼어 낼(unmount) 수 없다. 그래서 pivot_root가 동작하지 않고, 대신 "내용을 지우고 그 위에 새 루트를 덮는" 방식을 쓴다(커널 문서 `ramfs-rootfs-initramfs.rst`). 반대로 옛 initrd(블록 장치)나 컨테이너 런타임은 `pivot_root`를 쓴다. util-linux에도 같은 이름의 `switch_root`가 있고 동작이 같다.

함정:

- `exec` 없이 `switch_root`를 부르면 PID 1이 아니라서 거부된다.
- initramfs의 프로그램이 파일을 열어 둔 채(예: 백그라운드 데몬) switch_root하면 그 파일이 지워져도 메모리가 해제되지 않고, 데몬은 사라진 루트를 계속 본다. initramfs에서 띄운 데몬은 넘어가기 전에 정리하거나, systemd처럼 "initrd에서 시작해 새 루트로 넘겨 받는" 규약을 따라야 한다.
- `/dev`를 옮기지 않으면 진짜 루트의 init이 콘솔을 못 열 수 있다(`can't open /dev/ttyAMA0`).

---

## 4. 부팅 시간 재기 — U-Boot부터 "앱 준비 완료"까지

### 4.1 "부팅 완료"를 먼저 정의한다

부팅 시간 과제를 받으면 첫 질문은 "**어느 시점까지**?"다. 제품 요구가 "카메라 첫 프레임 1.5 s 이내"인지, "CAN 메시지에 응답 3 s 이내"인지, "로그인 프롬프트"인지에 따라 줄일 대상이 다르다. 그래서 측정 지점(마커)을 정한다.

| 구간 | 시작 마커 | 끝 마커 | 재는 도구 |
|---|---|---|---|
| 펌웨어 | 리셋(전원·리셋 핀) | `Starting kernel ...` | scope + GPIO, U-Boot `bootstage`, `CNTVCT_EL0` |
| 커널 | `Booting Linux on physical CPU` | `Run /init as init process` | `printk.time`(dmesg 타임스탬프), `initcall_debug`, `bootgraph.pl` |
| 초기 유저 공간 | `/init` | switch_root | 스크립트 안 `/proc/uptime` 출력 |
| 서비스 | init 시작 | 앱이 "READY"를 찍는 순간 | `systemd-analyze`, 앱 로그 |

마커는 하나의 시계로 재야 한다. 리셋 직후부터 도는 **arm64 generic timer(`CNTVCT_EL0`)** 가 그 시계로 쓰기 좋다. 커널의 `/proc/uptime`은 커널이 시계를 켠 뒤부터라서 펌웨어 시간이 빠진다.

### 4.2 리셋 이후 시간 — `CNTVCT_EL0` 읽기

무엇을 확인하나: 유저 공간에서 arm64 가상 카운터를 직접 읽어 "리셋 이후 경과 시간"을 얻고, `/proc/uptime`과의 차이로 "커널 이전 시간(펌웨어 + 커널 초기)"을 구한다. Linux arm64는 vDSO 때문에 EL0에서 `CNTVCT_EL0` 읽기를 허용한다.

```c
/* c7_cntvct: arm64 가상 카운터(CNTVCT_EL0)로 "리셋 이후 경과 시간"을 읽는다 — 펌웨어 시간까지 포함 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static inline uint64_t rd_cntvct(void) { uint64_t v; __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v)); return v; }
static inline uint64_t rd_cntfrq(void) { uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }

int main(int argc, char **argv)
{
    uint64_t c = rd_cntvct(), f = rd_cntfrq();
    double up = 0;
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp || fscanf(fp, "%lf", &up) != 1) { perror("/proc/uptime"); exit(1); }
    fclose(fp);
    printf("%s since_reset=%.3f uptime=%.3f pre_kernel=%.3f (cntfrq=%llu Hz)\n",
           argc > 1 ? argv[1] : "C7_READY", (double)c / f, up, (double)c / f - up,
           (unsigned long long)f);
    return 0;
}
```

§3.3의 출력 마지막 줄 `since_reset=1.117 uptime=1.000 pre_kernel=0.117 (cntfrq=62500000 Hz)`가 이 프로그램이다. `-kernel`로 직접 부팅하면 펌웨어가 없으니 `pre_kernel`은 커널이 시계를 켜기 전까지의 초기 부팅(0.12 s)뿐이다. U-Boot를 거치면 여기에 U-Boot 시간이 더해진다(§4.4).

주의: 이 방법은 카운터가 리셋 때 0에서 시작하고(QEMU는 그렇다), 펌웨어가 `CNTVOFF_EL2`(가상 카운터 오프셋)를 건드리지 않을 때만 맞다. 실제 SoC에서는 카운터가 전원 도메인과 함께 리셋되는지 데이터시트로 확인하고, 가장 확실한 방법은 여전히 **GPIO 토글 + scope**(리셋 핀과 "READY" GPIO 사이)다. Don에게 가장 익숙한 그 방법이 업계 표준이기도 하다.

### 4.3 QEMU에서 시간을 믿을 수 있게 — icount

무엇을 확인하나: 같은 VM을 다른 작업자들이 동시에 쓰는 상태(load average 11~12)에서 QEMU 부팅 시간을 재면 얼마나 흔들리는지, 그리고 `-icount`로 이를 해결할 수 있는지.

같은 설정(K0, verbose, switch_root까지)으로 7번 부팅한 `C7_READY` 시점의 게스트 uptime:

```text
K0_verbose@1.0.log   13.522 C7_READY realroot uptime=12.02
K0_verbose@2.0.log    8.529 C7_READY realroot uptime=5.81
K0_verbose@3.0.log   18.600 C7_READY realroot uptime=15.98
K0_verbose@4.0.log    7.697 C7_READY realroot uptime=6.61
K0_verbose@5.0.log    2.777 C7_READY realroot uptime=2.44
K0_verbose@6.0.log    4.943 C7_READY realroot uptime=4.22
K0_verbose@7.0.log    6.893 C7_READY realroot uptime=5.83
```

2.44 s에서 15.98 s까지 6배 차이. 느린 회차의 로그를 보면 특정 단계가 아니라 **모든 줄 사이가 고르게 늘어나 있다** — 호스트 CPU를 다른 QEMU·컴파일러와 나눠 쓴 탓이다. TCG에서 게스트 시계는 호스트 실시간을 따르므로 게스트가 덜 실행되면 같은 일을 하는 데 "게스트 시간"도 더 걸린다.

**icount**: `-icount shift=3`을 주면 QEMU가 게스트 시계를 **실행한 명령어 수로** 진행시킨다. shift=3이면 명령어 하나 = 2^3 = 8 ns, 즉 "초당 1억 2500만 명령어짜리 CPU"를 흉내 낸다. 호스트가 바쁘면 실시간으로는 느려지지만 게스트가 보는 시간은 변하지 않는다. 사이클 정확 시뮬레이터에서 사이클을 세는 것과 비슷한 발상이다.

```sh
for i in 1 2 3; do for a in "" "quiet"; do
  timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -icount shift=3 \
    -kernel /var/lab/out/Image -initrd c7_sr_initrd.gz \
    -drive file=c7_disk.img,format=raw,if=none,id=d0 -device virtio-blk-device,drive=d0 \
    -append "console=ttyAMA0 root=/dev/vda1 c7_auto $a" 2>&1 | tr -d '\r' \
    | grep -E "C7_READY|Run /init" | tr '\n' ' ' | sed "s/^/[$a] /"; echo
done; done
```

```text
[] [    2.454297] Run /init as init process C7_READY realroot uptime=2.83
[quiet] C7_READY realroot uptime=2.75
[] [    2.458251] Run /init as init process C7_READY realroot uptime=2.79
[quiet] C7_READY realroot uptime=2.75
[] [    2.458367] Run /init as init process C7_READY realroot uptime=2.79
[quiet] C7_READY realroot uptime=2.96
```

(이 출력은 `c7_cntvct`를 넣기 전 rcS로 잰 것이라 READY 줄 형식이 짧다.) `Run /init`이 세 번 모두 2.454~2.458 s — 4 ms 안에서 일치한다. 남은 흔들림(READY 2.75~2.96)은 디스크 I/O 완료처럼 실시간에 묶인 이벤트 때문이다.

icount의 한계(숫자를 읽을 때 꼭 기억할 것):

- 명령어 수만 센다. 캐시 미스, 분기 예측, 메모리 대역폭, **저장장치 속도, UART 속도는 반영되지 않는다**. QEMU의 virtio 디스크와 PL011 UART는 사실상 무한히 빠르다.
- 125 MIPS는 실제 Cortex-A53/A72 코어보다 한 자릿수 느리다. 절대값이 아니라 **설정 간 상대 비교**에 쓴다.
- 그래서 아래에서는 icount 값(재현성)과 실시간 값(장치 에뮬레이션 비용 포함, 흔들림)을 같이 보고, 저장장치·UART처럼 QEMU가 모델링하지 않는 비용은 따로 계산한다.

### 4.4 전 구간 측정 하네스

무엇을 확인하나: QEMU의 시리얼 출력 한 줄마다 호스트 시각을 찍고, 마커(U-Boot 배너, `Starting kernel`, `Booting Linux`, `Run /init`, `C7_READY`)와 게스트가 직접 찍은 값(`since_reset`, `uptime`, `[initramfs] PID 1 up`)을 뽑는 스크립트. 실제 보드에서 `grabserial`이 하는 일과 같다.

```python
#!/usr/bin/env python3
"""c7_bt.py: QEMU 부팅을 N번 돌리고 시리얼 줄마다 호스트 시각을 찍어 마커별 시간을 잰다.
usage: c7_bt.py NAME RUNS -- qemu-args...   (log: /var/tmp/C7/logs/NAME.<i>.log)"""
import os, re, statistics, subprocess, sys, time

MARK = [("uboot", r"^U-Boot 20"), ("startk", r"Starting kernel"),
        ("kernel", r"Booting Linux on"), ("init", r"Run /init|Run /sbin/init|Run .* as init"),
        ("ready", r"C7_READY")]

def one(args, log):
    t0 = time.monotonic()
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         stdin=subprocess.DEVNULL)
    seen, ktime, guest, extra = {}, None, None, {}
    with open(log, "w") as f:
        for raw in p.stdout:
            t = time.monotonic() - t0
            line = raw.decode(errors="replace").rstrip("\r\n")
            f.write(f"{t:8.3f} {line}\n")
            m = re.search(r"\[initramfs\] PID 1 up ([0-9.]+)s", line)
            if m: extra["g_irfs"] = float(m.group(1))
            m = re.search(r"since_reset=([0-9.]+).*pre_kernel=([0-9.]+)", line)
            if m: extra["g_reset"], extra["g_prek"] = float(m.group(1)), float(m.group(2))
            for k, rx in MARK:
                if k not in seen and re.search(rx, line):
                    seen[k] = t
                    if k == "init":
                        m = re.search(r"\[\s*([0-9.]+)\]", line)
                        ktime = float(m.group(1)) if m else None
                    if k == "ready":
                        m = re.search(r"uptime=([0-9.]+)", line)
                        guest = float(m.group(1)) if m else None
    p.wait()
    seen["exit"] = time.monotonic() - t0
    return seen, ktime, guest, extra

def main():
    name, runs = sys.argv[1], int(sys.argv[2])
    args = sys.argv[sys.argv.index("--") + 1:]
    os.makedirs("/var/tmp/C7/logs", exist_ok=True)
    res = [one(["timeout", "120"] + args, f"/var/tmp/C7/logs/{name}.{i}.log") for i in range(runs)]
    med = lambda xs: statistics.median(xs) if xs else float("nan")
    cols = []
    for k, _ in MARK + [("exit", "")]:
        xs = [r[0][k] for r in res if k in r[0]]
        if xs:
            cols.append(f"{k}={med(xs):.3f}")
    kt = [r[1] for r in res if r[1] is not None]
    gu = [r[2] for r in res if r[2] is not None]
    if kt: cols.append(f"k_init={med(kt):.3f}")
    if gu: cols.append(f"g_ready={med(gu):.3f}")
    for k in ("g_irfs", "g_reset", "g_prek"):
        xs = [r[3][k] for r in res if k in r[3]]
        if xs: cols.append(f"{k}={med(xs):.3f}")
    print(f"{name:<14} n={runs} " + " ".join(cols))

if __name__ == "__main__":
    main()
```

U-Boot 경로: QEMU에 `-bios u-boot.bin`을 주고 디스크를 붙이면, U-Boot의 기본 `bootcmd=bootflow scan -lb`가 장치를 훑다가 `/boot/extlinux/extlinux.conf`를 찾아 부팅한다. 기본 설정(U0)의 실제 콘솔(앞부분):

```text
U-Boot 2026.10 (Oct 10 2026 - 09:59:26 -0700)

DRAM:  512 MiB
using memory 0x5e44f000-0x5f48f000 for malloc()
Core:  51 devices, 14 uclasses, devicetree: board
Flash: 64 MiB
Loading Environment from Flash...*** Warning - bad CRC, using default environment

In:    serial,usbkbd
Out:   serial,vidconsole
Err:   serial,vidconsole
No USB controllers found
Net:   eth0: virtio-net#32
Hit any key to stop autoboot: 2 ... 1 ... 0
Scanning for bootflows in all bootdevs
...
** Booting bootflow '<NULL>' with efi_mgr
Loading Boot0000 'virtio 0' failed
EFI boot manager: Cannot load any image
Boot failed (err=-14)
Scanning bootdev 'fw-cfg@9020000.bootdev':
fatal: no kernel available
scanning bus for devices...
Scanning bootdev 'virtio-blk#31.bootdev':
  1  extlinux     ready   virtio       1  virtio-blk#31.bootdev.par /boot/extlinux/extlinux.conf
** Booting bootflow 'virtio-blk#31.bootdev.part_1' with extlinux
Retrieving file: /boot/Image
Retrieving file: /boot/initrd.gz
append: console=ttyAMA0 root=/dev/vda1 c7_auto
...
Starting kernel ...
```

(카운트다운의 터미널 제어 문자와 표 머리, `Missing TPMv2` 같은 줄은 `...`로 줄였다.) 볼 것: 2초 카운트다운, 그리고 EFI 부트 매니저와 fw-cfg를 먼저 시도했다가 실패한 뒤에야 virtio를 찾는다. 둘 다 이 보드에 쓸모없는 시간이다.

U-Boot 환경 변수는 `ENV_IS_IN_FLASH`(주소 `0x4000000` = QEMU의 두 번째 pflash, 크기 `0x40000`)에 있다. U-Boot 소스 트리의 `tools/mkenvimage`로 환경 이미지를 만들어 `-drive if=pflash,unit=1`로 붙이면 U-Boot 바이너리를 다시 빌드하지 않고 설정을 바꿀 수 있다. 가장 공격적인 설정(U2, "direct")의 핵심:

```text
bootdelay=0
stdin=serial
stdout=serial
stderr=serial
bootargs=console=ttyAMA0 root=/dev/vda1 c7_auto
bootcmd=load virtio 0:1 ${kernel_addr_r} /boot/Image; load virtio 0:1 ${ramdisk_addr_r} /boot/initrd.gz; booti ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} ${fdt_addr}
```

```sh
/var/lab/build/u-boot/tools/mkenvimage -s 0x40000 -o c7_env_direct.bin c7_env_direct.txt && truncate -s 64M c7_env_direct.bin
U="qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -icount shift=3 -bios /var/lab/out/u-boot.bin \
   -drive file=c7_disk.img,format=raw,if=none,id=d0 -device virtio-blk-device,drive=d0"
python3 c7_bt.py U0_default@1 1 -- $U
for e in delay0 direct directq gz; do python3 c7_bt.py U_$e@1 1 -- $U -drive if=pflash,unit=1,format=raw,file=c7_env_$e.bin; done
```

여섯 가지 설정을 icount로 2회씩 돌린 결과(1회차 원본 줄, 2회차도 `g_*` 값이 같거나 수십 ms 안):

```text
U0_default@1   n=1 uboot=0.038 startk=2.115 kernel=2.521 init=3.283 ready=3.756 exit=6.186 k_init=2.388 g_ready=2.720 g_irfs=2.400 g_reset=5.871 g_prek=3.151
U_delay0@1     n=1 uboot=0.039 startk=0.353 kernel=0.742 init=1.486 ready=2.059 exit=4.497 k_init=2.389 g_ready=2.720 g_irfs=2.400 g_reset=3.677 g_prek=0.957
U_direct@1     n=1 uboot=0.039 startk=0.286 kernel=0.664 init=1.506 ready=1.953 exit=4.362 k_init=2.388 g_ready=2.720 g_irfs=2.400 g_reset=3.534 g_prek=0.814
U_directq@1    n=1 uboot=0.036 startk=0.226 ready=1.814 exit=4.263 g_ready=2.660 g_irfs=2.340 g_reset=3.458 g_prek=0.798
U_gz@1         n=1 uboot=0.036 startk=1.007 ready=2.790 exit=5.235 g_ready=2.670 g_irfs=2.340 g_reset=8.268 g_prek=5.598
K0_verbose@1   n=1 kernel=0.309 init=0.871 ready=1.207 exit=3.686 k_init=2.454 g_ready=2.780 g_irfs=2.470 g_reset=3.160 g_prek=0.380
```

읽는 법: `uboot`~`exit`는 호스트 실시간(흔들림), `g_`로 시작하는 값이 icount 게스트 시간이다. `g_reset` = 리셋부터 READY까지 전체, `g_prek` = 그중 커널 시계가 켜지기 전(펌웨어 + 커널 초기), `g_irfs` = 커널 시작부터 initramfs `/init`까지, `g_ready - g_irfs` = initramfs + switch_root + rcS.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="222.0" y="46.0" text-anchor="end" font-size="12">U0 기본 (bootdelay=2, 스캔)</text>
  <rect x="230.0" y="30.0" width="131.7" height="22.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
  <rect x="361.7" y="30.0" width="104.0" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <rect x="465.7" y="30.0" width="13.6" height="22.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <text x="485.4" y="46.0" font-size="12">5.75</text>
  <text x="222.0" y="82.0" text-anchor="end" font-size="12">U1 bootdelay=0</text>
  <rect x="230.0" y="66.0" width="41.8" height="22.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
  <rect x="271.8" y="66.0" width="104.0" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <rect x="375.8" y="66.0" width="13.6" height="22.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <text x="395.4" y="82.0" font-size="12">3.68</text>
  <text x="222.0" y="118.0" text-anchor="end" font-size="12">U2 직접 load + booti</text>
  <rect x="230.0" y="102.0" width="35.0" height="22.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
  <rect x="265.0" y="102.0" width="104.0" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <rect x="369.0" y="102.0" width="13.9" height="22.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <text x="388.8" y="118.0" font-size="12">3.53</text>
  <text x="222.0" y="154.0" text-anchor="end" font-size="12">U3 U2 + quiet + blacklist</text>
  <rect x="230.0" y="138.0" width="34.6" height="22.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
  <rect x="264.6" y="138.0" width="101.4" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <rect x="366.0" y="138.0" width="13.9" height="22.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <text x="385.9" y="154.0" font-size="12">3.46</text>
  <text x="222.0" y="190.0" text-anchor="end" font-size="12">U4 U3 + Image.gz</text>
  <rect x="230.0" y="174.0" width="241.4" height="22.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
  <rect x="471.4" y="174.0" width="101.4" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <rect x="572.8" y="174.0" width="14.1" height="22.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <text x="592.9" y="190.0" font-size="12">8.24</text>
  <text x="222.0" y="226.0" text-anchor="end" font-size="12">K  -kernel 직접 (펌웨어 없음)</text>
  <rect x="230.0" y="210.0" width="16.5" height="22.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
  <rect x="246.5" y="210.0" width="107.0" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <rect x="353.5" y="210.0" width="13.4" height="22.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <text x="372.9" y="226.0" font-size="12">3.16</text>
  <line x1="230.0" y1="248.0" x2="620.0" y2="248.0" stroke="currentColor"/>
  <line x1="230.0" y1="248.0" x2="230.0" y2="253.0" stroke="currentColor"/>
  <text x="230.0" y="266.0" text-anchor="middle" font-size="12">0</text>
  <line x1="273.3" y1="248.0" x2="273.3" y2="253.0" stroke="currentColor"/>
  <text x="273.3" y="266.0" text-anchor="middle" font-size="12">1</text>
  <line x1="316.7" y1="248.0" x2="316.7" y2="253.0" stroke="currentColor"/>
  <text x="316.7" y="266.0" text-anchor="middle" font-size="12">2</text>
  <line x1="360.0" y1="248.0" x2="360.0" y2="253.0" stroke="currentColor"/>
  <text x="360.0" y="266.0" text-anchor="middle" font-size="12">3</text>
  <line x1="403.3" y1="248.0" x2="403.3" y2="253.0" stroke="currentColor"/>
  <text x="403.3" y="266.0" text-anchor="middle" font-size="12">4</text>
  <line x1="446.7" y1="248.0" x2="446.7" y2="253.0" stroke="currentColor"/>
  <text x="446.7" y="266.0" text-anchor="middle" font-size="12">5</text>
  <line x1="490.0" y1="248.0" x2="490.0" y2="253.0" stroke="currentColor"/>
  <text x="490.0" y="266.0" text-anchor="middle" font-size="12">6</text>
  <line x1="533.3" y1="248.0" x2="533.3" y2="253.0" stroke="currentColor"/>
  <text x="533.3" y="266.0" text-anchor="middle" font-size="12">7</text>
  <line x1="576.7" y1="248.0" x2="576.7" y2="253.0" stroke="currentColor"/>
  <text x="576.7" y="266.0" text-anchor="middle" font-size="12">8</text>
  <line x1="620.0" y1="248.0" x2="620.0" y2="253.0" stroke="currentColor"/>
  <text x="620.0" y="266.0" text-anchor="middle" font-size="12">9</text>
  <text x="628.0" y="266.0" font-size="12">s</text>
  <rect x="20.0" y="280.0" width="14.0" height="12.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
  <text x="40.0" y="291.0" font-size="12">펌웨어+커널 초기</text>
  <rect x="240.0" y="280.0" width="14.0" height="12.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <text x="260.0" y="291.0" font-size="12">커널 → /init</text>
  <rect x="460.0" y="280.0" width="14.0" height="12.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <text x="480.0" y="291.0" font-size="12">initramfs·switch_root·rcS</text>
  <text x="230.0" y="16.0" font-size="12">리셋 이후 C7_READY까지 (QEMU icount 가상 시간, 2회 동일)</text>
</svg>
```
그림 6 — 리셋부터 READY까지의 구간 분해(icount 중앙값). U0→U1의 2초는 `bootdelay` 하나다. U4는 압축 커널을 U-Boot가 푸는 데 4.8 s를 더 쓴다(§5.1). K는 QEMU가 커널을 직접 올린 "펌웨어 없음" 기준선이다.

---

## 5. 부팅 시간 줄이기 — 기법과 측정 결과

원칙은 SSD 펌웨어 성능 튜닝과 같다: **재고 → 가장 큰 덩어리부터 → 다시 잰다**. "흔히 알려진 기법"이 내 보드에서 0 ms일 수도 있다(§5.2에서 실제로 그렇다).

### 5.1 펌웨어 단계 (U-Boot)

| 설정 | 바꾼 것 | `g_prek` (s) | 전체 `g_reset` (s) | 줄어든 시간 |
|---|---|---|---|---|
| U0 | 기본: `bootdelay=2`, bootflow 스캔 | 3.040 | 5.755 | 기준 |
| U1 | `bootdelay=0` | 0.964 | 3.679 | −2.08 s |
| U2 | + 스캔 대신 `load`+`booti` 직접, USB·비디오 콘솔 끔 | 0.807 | 3.527 | −0.15 s |
| U3 | + 커널 `quiet` + `initcall_blacklist` | 0.799 | 3.459 | −0.07 s |
| U4 | U3에서 `Image`(42.7 MB) 대신 `Image.gz`(15.2 MB) | 5.571 | 8.236 | **+4.78 s** |

(icount, 2회 중앙값. U0의 `g_prek`는 3.151/2.930로 회차 간 0.2 s 흔들렸다 — 스캔 중 실시간에 묶인 대기가 있어서다.)

- **`bootdelay`**: 개발 편의용 카운트다운이 그대로 2초다. 양산 이미지에서는 0(또는 `-2`: 키 검사도 안 함)으로 두고, 디버그 진입은 특정 키나 GPIO 스트랩으로 한다.
- **직접 부팅**: bootflow·distro boot 스캔은 "아무 보드에서나 부팅"을 위한 것이다. 장비는 커널 위치를 안다. `load` + `booti`(또는 FIT 이미지 하나를 `bootm`)로 바꾸면 EFI·네트워크·USB 탐색이 사라진다.
- **압축 커널**: U4의 U-Boot 로그에서 원인이 그대로 보인다.

```text
   1.648 15247334 bytes read in 86 ms (169.1 MiB/s)
   1.669 1497883 bytes read in 30 ms (47.6 MiB/s)
   1.670    Uncompressing Kernel Image to 0
   6.281 ## Flattened Device Tree blob at 40000000
```

(왼쪽 숫자는 호스트 시각.) 읽기는 86 ms인데 gzip 해제가 4.6 s. QEMU의 virtio 디스크는 사실상 무한히 빠르니 "덜 읽는" 이득은 없고 "푸는" 비용만 남는다. 실제 보드에서는 판단이 저장장치 속도에 달려 있다. 예를 들어 25 MB/s급 QSPI NOR라면 27.5 MB를 덜 읽어 약 1.1 s를 아끼고, 해제 비용은 실제 코어(수 GHz·캐시 있음)에서 icount의 125 MIPS보다 훨씬 작다. 그래서 규칙은 "느린 저장장치 + 빠른 CPU면 압축, 빠른 eMMC/NVMe면 비압축 `Image`"이고, **반드시 보드에서 재서** 정한다.

### 5.2 커널 단계

같은 switch_root 부팅(`-kernel` 직접)으로 커널 인자만 바꿨다. icount 2회와, VM이 한가할 때(load 2~4) 실시간 5회를 모두 돌렸다.

```text
K0_verbose       ... g_ready=2.780[2.78-2.78] g_irfs=2.470[2.47-2.47] ... n=2
K1_quiet         ... g_ready=2.740[2.74-2.74] g_irfs=2.430[2.43-2.43] ... n=2
K2a_q_physmap    ... g_ready=2.745[2.74-2.75] g_irfs=2.430[2.43-2.43] ... n=2
K2_q_blacklist   ... g_ready=2.725[2.72-2.73] g_irfs=2.410[2.41-2.41] ... n=2
K3_q_bl_lpj      ... g_ready=2.670[2.67-2.67] g_irfs=2.360[2.36-2.36] ... n=2
K4_q_bl_async    ... g_ready=2.690[2.69-2.69] g_irfs=2.370[2.37-2.37] ... n=2
```

(icount, `c7_agg.py`로 모은 중앙값[최소-최대]. 호스트 시각 열은 `...`로 줄였다.)

```text
K0_verbose       ... g_ready=1.230[1.16-1.53] g_irfs=0.880[0.82-1.08] ... n=5
K1_quiet         ... g_ready=1.230[1.12-1.41] g_irfs=0.820[0.75-0.98] ... n=5
K2a_q_physmap    ... g_ready=1.140[1.07-1.24] g_irfs=0.760[0.73-0.82] ... n=5
K2_q_blacklist   ... g_ready=1.160[1.08-1.84] g_irfs=0.770[0.72-1.23] ... n=5
K3_q_bl_lpj      ... g_ready=1.170[1.12-1.61] g_irfs=0.770[0.73-1.14] ... n=5
K4_q_bl_async    ... g_ready=1.130[1.09-1.73] g_irfs=0.760[0.71-1.21] ... n=5
```

(실시간 TCG, 5회 중앙값. 실시간 TCG는 icount의 125 MIPS보다 빨라서 절대값이 작다.)

정직한 결론: **이 보드에서 커널 인자 기법은 각각 수십 ms**다. 하나씩 보자.

**`quiet` / `loglevel=`**: icount −40 ms, 실시간 −60 ms. 작은 이유는 QEMU의 PL011이 무한히 빠르고, 6.18의 PL011 드라이버가 **nbcon 콘솔**(`pl011_console_write_atomic`/`_thread`)이라 출력이 `pr/ttyAMA-1` 프린터 스레드로 넘어가기 때문이다(§2.2의 `ps`에 그 스레드가 보인다). 하지만 실제 보드에서는 다르다. verbose 부팅에서 READY까지 콘솔로 나간 바이트를 세면:

```sh
f=logs/K0_verbose@1.0.log; n=$(grep -n -m1 C7_READY $f | cut -d: -f1); head -$n $f | cut -c10- | wc -lc
```

```text
    260   16977
```

16,977바이트를 115200 baud(8N1 → 초당 11,520바이트)로 내보내면 **1.47 s**다(계산값). 동기식 레거시 콘솔을 쓰는 커널(많은 벤더 BSP 커널)에서는 이 시간만큼 부팅이 늦어진다. 그래서 양산은 `quiet`(=`loglevel=4`)가 기본이고, 로그는 `dmesg`로 나중에 본다.

**`initcall_debug` → `initcall_blacklist=`**: 먼저 무엇이 느린지 잰다. `initcall_debug`의 메시지는 `KERN_DEBUG`라 콘솔에 안 나오므로 게스트 안에서 `dmesg`로 꺼낸다.

```sh
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -icount shift=3 \
  -kernel /var/lab/out/Image -initrd /var/lab/out/initramfs.cpio.gz \
  -drive file=c7_disk.img,format=raw,if=none,id=d0 -device virtio-blk-device,drive=d0 \
  -append "console=ttyAMA0 quiet initcall_debug LAB_CMD=dmesg" 2>&1 | tr -d '\r' > c7_icd_ic.log
grep -oE "initcall [^ ]+ returned -?[0-9]+ after [0-9]+ usecs" c7_icd_ic.log | sort -k6 -n -r | head -8
grep -oE "initcall [^ ]+ returned -?[0-9]+ after [0-9]+ usecs" c7_icd_ic.log | awk '{s+=$6; n++} END {print n, "initcalls, sum usecs", s}'
```

```text
initcall param_sysfs_builtin_init+0x0/0x1c0 returned 0 after 82775 usecs
initcall slab_late_init+0x0/0x138 returned 0 after 64918 usecs
initcall physmap_init+0x0/0x28 returned 0 after 29985 usecs
initcall loop_init+0x0/0x108 returned 0 after 21123 usecs
initcall pty_init+0x0/0x324 returned 0 after 11159 usecs
initcall serial8250_init+0x0/0x110 returned 0 after 8746 usecs
initcall virtio_mmio_init+0x0/0x28 returned 0 after 7849 usecs
initcall armv8_pmu_driver_init+0x0/0x44 returned 0 after 6177 usecs
840 initcalls, sum usecs 744960
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="222.0" y="45.0" text-anchor="end" font-size="12">param_sysfs_builtin_init</text>
  <rect x="230.0" y="30.0" width="340.3" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <text x="576.3" y="45.0" font-size="12">82.8 ms</text>
  <text x="222.0" y="75.0" text-anchor="end" font-size="12">slab_late_init</text>
  <rect x="230.0" y="60.0" width="266.9" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <text x="502.9" y="75.0" font-size="12">64.9 ms</text>
  <text x="222.0" y="105.0" text-anchor="end" font-size="12">physmap_init</text>
  <rect x="230.0" y="90.0" width="123.3" height="20.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
  <text x="359.3" y="105.0" font-size="12">30.0 ms</text>
  <text x="222.0" y="135.0" text-anchor="end" font-size="12">loop_init</text>
  <rect x="230.0" y="120.0" width="86.8" height="20.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
  <text x="322.8" y="135.0" font-size="12">21.1 ms</text>
  <text x="222.0" y="165.0" text-anchor="end" font-size="12">pty_init</text>
  <rect x="230.0" y="150.0" width="45.9" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <text x="281.9" y="165.0" font-size="12">11.2 ms</text>
  <text x="222.0" y="195.0" text-anchor="end" font-size="12">serial8250_init</text>
  <rect x="230.0" y="180.0" width="36.0" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <text x="272.0" y="195.0" font-size="12">8.7 ms</text>
  <text x="222.0" y="225.0" text-anchor="end" font-size="12">virtio_mmio_init</text>
  <rect x="230.0" y="210.0" width="32.3" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <text x="268.3" y="225.0" font-size="12">7.8 ms</text>
  <text x="222.0" y="255.0" text-anchor="end" font-size="12">armv8_pmu_driver_init</text>
  <rect x="230.0" y="240.0" width="25.4" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <text x="261.4" y="255.0" font-size="12">6.2 ms</text>
  <line x1="230.0" y1="272.0" x2="600.0" y2="272.0" stroke="currentColor"/>
  <line x1="230.0" y1="272.0" x2="230.0" y2="277.0" stroke="currentColor"/>
  <text x="230.0" y="290.0" text-anchor="middle" font-size="12">0</text>
  <line x1="353.3" y1="272.0" x2="353.3" y2="277.0" stroke="currentColor"/>
  <text x="353.3" y="290.0" text-anchor="middle" font-size="12">30</text>
  <line x1="476.7" y1="272.0" x2="476.7" y2="277.0" stroke="currentColor"/>
  <text x="476.7" y="290.0" text-anchor="middle" font-size="12">60</text>
  <line x1="600.0" y1="272.0" x2="600.0" y2="277.0" stroke="currentColor"/>
  <text x="600.0" y="290.0" text-anchor="middle" font-size="12">90</text>
  <text x="608.0" y="290.0" font-size="12">ms</text>
  <text x="10.0" y="16.0" font-size="12">initcall_debug Top 8 (icount) — 840개 합계 745 ms / 커널 부팅 2.58 s</text>
  <text x="10.0" y="318.0" font-size="12">주황 = initcall_blacklist= 로 뺀 것 (이 보드에서 안 쓰는 NOR flash · loop)</text>
</svg>
```
그림 9 — icount로 잰 느린 initcall. 840개를 합치면 745 ms로 커널 부팅(2.58 s)의 29%지만, 상위 몇 개를 빼면 나머지는 수 ms짜리 "긴 꼬리"다.

`physmap_init`(CFI NOR flash 탐색 — QEMU virt의 pflash 두 개)과 `loop_init`(loop 장치 8개 생성)은 이 장비에서 안 쓰므로 뺄 수 있다. 실제로 빠졌는지 확인:

```sh
... -append "console=ttyAMA0 quiet initcall_blacklist=physmap_init,loop_init LAB_CMD=dmesg~|~grep~-iE~blacklist\|physmap\|loop:"
```

```text
[    0.454958] initcall loop_init blacklisted
[    0.459378] initcall physmap_init blacklisted
```

효과는 icount −20 ms, 실시간 −60 ms(실시간에서 더 큰 이유: CFI 탐색은 MMIO 접근이 많고, QEMU에서 MMIO 에뮬레이션은 명령어 수가 아니라 호스트 시간을 쓴다). 같은 VM이 붐빌 때 실시간으로 잰 `initcall_debug`에서는 `physmap_init`이 282 ms로 1위였다 — 측정 조건에 따라 순위가 바뀐다는 점도 교훈이다.

주의: `initcall_blacklist=`는 `CONFIG_KALLSYMS`가 있어야 동작하고(없으면 "initcall_blacklist requires CONFIG_KALLSYMS" 경고), 디버깅용 도구다. 양산에서는 그 드라이버를 **Kconfig에서 빼는 것**이 정답이다. 그리고 그게 진짜 큰 레버다: 랩 커널은 `defconfig`(수백 개 SoC의 드라이버 포함)라 840개 initcall과 `param_sysfs_builtin_init`(빌트인 모듈 수에 비례) 같은 비용을 낸다. 보드에 필요한 드라이버만 남긴 설정은 이 745 ms의 대부분과 커널 크기(42.7 MB → 수 MB, §5.1의 읽기 시간)를 같이 줄인다. 이 노트에서는 커널을 다시 빌드하지 않았으므로 이 효과는 측정하지 않았다.

**`lpj=`**: 옛 ARM 보드에서는 `Calibrating delay loop`가 수백 ms를 먹어 `lpj=` 지정이 고전적 기법이었다. arm64에서는 아키텍처 타이머 주파수로 계산하므로 이미 건너뛴다.

```text
[    0.011703] Calibrating delay loop (skipped), value calculated using timer frequency.. 125.00 BogoMIPS (lpj=250000)
[    0.008796] Calibrating delay loop (skipped) preset value.. 125.00 BogoMIPS (lpj=250000)
```

첫 줄이 기본, 둘째 줄이 `lpj=250000`을 준 경우다. 둘 다 "skipped"라 효과가 없어야 한다. 표의 K3이 K2보다 50 ms 빠르게 나왔지만 calibration과는 무관하다 — icount에서도 설정을 바꾸면 수십 ms의 차이가 생길 수 있다는 측정 잡음의 크기로 읽어야 한다. **그러니 이 보드에서 50 ms 이하 차이는 "효과 없음"으로 판단한다.**

**`driver_async_probe=`**: `drivers/base/dd.c`의 `driver_async_probe=drv1,drv2`(`*`이면 전부)는 지정한 드라이버의 probe를 비동기로 돌려 다른 initcall과 겹치게 한다. 효과는 느린 probe(예: 펌웨어 로드·PHY 링크 대기)가 있고 **CPU가 여럿일 때** 나온다. QEMU 단일 코어·빠른 probe뿐인 여기서는 잡음 수준(K4 ≈ K2)이었다. 드라이버 쪽에서는 `probe_type = PROBE_PREFER_ASYNCHRONOUS`로 같은 효과를 낸다(G 모듈).

### 5.3 initramfs 압축 — 일곱 가지 비교

무엇을 확인하나: 같은 내용의 initramfs를 압축 방식만 바꿨을 때 크기와 "커널이 풀고 `/init`에 도달하는 시간"이 어떻게 달라지는지. 내용은 랩 rootfs + 커널 모듈(`drivers/net`, `drivers/media`의 `.ko`를 `strip --strip-debug`)로 23.3 MB — 실제 제품 initramfs에 모듈과 펌웨어가 들어가는 상황을 흉내 낸 것이다. `/init`은 `C7_READY initramfs uptime=…`을 찍고 바로 `poweroff -f`.

VM에는 `lz4` 명령이 없어서, 자기 폴더의 venv에 `pip install lz4`를 하고 커널 `unlz4`가 읽는 **legacy 형식**(`lz4 -l`과 같은 것: 매직 `0x184C2102` + 8 MiB 블록마다 4바이트 길이)을 직접 쓰는 작은 스크립트를 만들었다.

```python
#!/usr/bin/env python3
"""stdin -> stdout: LZ4 legacy frame (magic 0x184C2102, 8 MiB blocks) — 커널 unlz4가 읽는 형식 (= lz4 -l)"""
import struct, sys
import lz4.block
data = sys.stdin.buffer.read()
out = sys.stdout.buffer
out.write(struct.pack("<I", 0x184C2102))
for i in range(0, len(data), 8 << 20):
    c = lz4.block.compress(data[i:i + (8 << 20)], mode="high_compression", compression=9, store_size=False)
    out.write(struct.pack("<I", len(c)) + c)
```

```sh
cd /var/tmp/C7/rc && find . | cpio -o -H newc --quiet > ../c7_comp.cpio && cd ..
cp c7_comp.cpio c7_comp_none.img
gzip -9 -c < c7_comp.cpio > c7_comp_gzip.img
xz --check=crc32 -9 -c < c7_comp.cpio > c7_comp_xz.img      # 커널 xz 해제기는 CRC32 체크만 지원
lzma -9 -c < c7_comp.cpio > c7_comp_lzma.img
bzip2 -9 -c < c7_comp.cpio > c7_comp_bzip2.img
zstd -19 -q -c < c7_comp.cpio > c7_comp_zstd.img
/var/tmp/C7/venv/bin/python c7_lz4legacy.py < c7_comp.cpio > c7_comp_lz4.img
for n in none gzip xz lzma bzip2 zstd lz4; do
  python3 c7_bt.py comp_$n@1 1 -- qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
     -icount shift=3 -kernel /var/lab/out/Image -initrd c7_comp_$n.img -append "console=ttyAMA0"
  # dmesg의 "Unpacking initramfs..." ~ "Freeing initrd memory" 사이 시간을 뽑는다
done
```

```text
6568912 c7_comp_bzip2.img
7199679 c7_comp_gzip.img
8624995 c7_comp_lz4.img
4918158 c7_comp_lzma.img
23266304 c7_comp_none.img
4918976 c7_comp_xz.img
5690867 c7_comp_zstd.img
```

```text
UNPACK comp_none@1 unpack_start=0.395080 freed=1.172308 span=.777228 C7_READY initramfs uptime=2.34 files=885
UNPACK comp_gzip@1 unpack_start=0.395084 freed=4.075567 span=3.680483 C7_READY initramfs uptime=4.59 files=885
UNPACK comp_xz@1 unpack_start=0.395127 freed=14.384192 span=13.989065 C7_READY initramfs uptime=14.90 files=885
UNPACK comp_lzma@1 unpack_start=0.395129 freed=38.255081 span=37.859952 C7_READY initramfs uptime=38.77 files=885
UNPACK comp_bzip2@1 unpack_start=0.395107 freed=25.981452 span=25.586345 C7_READY initramfs uptime=26.49 files=885
UNPACK comp_zstd@1 unpack_start=0.395191 freed=3.997364 span=3.602173 C7_READY initramfs uptime=4.51 files=885
UNPACK comp_lz4@1 unpack_start=0.395131 freed=2.158858 span=1.763727 C7_READY initramfs uptime=2.83 files=885
UNPACK comp_none@2 unpack_start=0.395132 freed=1.172354 span=.777222 C7_READY initramfs uptime=2.34 files=885
...  (2회차 나머지도 1회차와 1 ms 안에서 같다)
```

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="142.0" y="49.0" text-anchor="end" font-size="12">none 23.3 MB</text>
  <rect x="150.0" y="34.0" width="26.3" height="20.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <text x="182.3" y="49.0" font-size="12">2.34 s</text>
  <text x="142.0" y="81.0" text-anchor="end" font-size="12">lz4 8.6 MB</text>
  <rect x="150.0" y="66.0" width="31.8" height="20.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <text x="187.8" y="81.0" font-size="12">2.83 s</text>
  <text x="142.0" y="113.0" text-anchor="end" font-size="12">zstd 5.7 MB</text>
  <rect x="150.0" y="98.0" width="50.7" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <text x="206.7" y="113.0" font-size="12">4.51 s</text>
  <text x="142.0" y="145.0" text-anchor="end" font-size="12">gzip 7.2 MB</text>
  <rect x="150.0" y="130.0" width="51.6" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
  <text x="207.6" y="145.0" font-size="12">4.59 s</text>
  <text x="142.0" y="177.0" text-anchor="end" font-size="12">xz 4.9 MB</text>
  <rect x="150.0" y="162.0" width="167.6" height="20.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
  <text x="323.6" y="177.0" font-size="12">14.90 s</text>
  <text x="142.0" y="209.0" text-anchor="end" font-size="12">bzip2 6.6 MB</text>
  <rect x="150.0" y="194.0" width="298.0" height="20.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
  <text x="454.0" y="209.0" font-size="12">26.49 s</text>
  <text x="142.0" y="241.0" text-anchor="end" font-size="12">lzma 4.9 MB</text>
  <rect x="150.0" y="226.0" width="436.2" height="20.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
  <text x="592.2" y="241.0" font-size="12">38.77 s</text>
  <line x1="150.0" y1="262.0" x2="600.0" y2="262.0" stroke="currentColor"/>
  <line x1="150.0" y1="262.0" x2="150.0" y2="267.0" stroke="currentColor"/>
  <text x="150.0" y="280.0" text-anchor="middle" font-size="12">0</text>
  <line x1="262.5" y1="262.0" x2="262.5" y2="267.0" stroke="currentColor"/>
  <text x="262.5" y="280.0" text-anchor="middle" font-size="12">10</text>
  <line x1="375.0" y1="262.0" x2="375.0" y2="267.0" stroke="currentColor"/>
  <text x="375.0" y="280.0" text-anchor="middle" font-size="12">20</text>
  <line x1="487.5" y1="262.0" x2="487.5" y2="267.0" stroke="currentColor"/>
  <text x="487.5" y="280.0" text-anchor="middle" font-size="12">30</text>
  <line x1="600.0" y1="262.0" x2="600.0" y2="267.0" stroke="currentColor"/>
  <text x="600.0" y="280.0" text-anchor="middle" font-size="12">40</text>
  <text x="610.0" y="280.0" font-size="12">s</text>
  <text x="10.0" y="18.0" font-size="12">같은 23.3 MB cpio를 7가지로 압축 → /init 도달 uptime (icount)</text>
  <text x="10.0" y="302.0" font-size="12">커널 해제 코드의 명령어 수만 반영 — 저장장치 읽기 시간은 0으로 취급</text>
</svg>
```
그림 7 — 같은 23.3 MB cpio를 일곱 가지로 압축했을 때 `/init` 도달 시간(icount). lz4가 비압축 다음으로 빠르고, 압축률이 가장 좋은 xz·lzma는 10~16배 느리다.

저장장치에서 읽는 시간은 QEMU가 모델링하지 않으므로 따로 계산해 붙인다(25 MB/s QSPI NOR, 100 MB/s eMMC 가정 — 계산값).

| 압축 | 크기 (MB) | 압축률 | 해제 구간 (s, icount) | `/init` 도달 (s) | +읽기 @25 MB/s | +읽기 @100 MB/s |
|---|---|---|---|---|---|---|
| none | 23.27 | 1.00 | 0.78 | 2.34 | 3.27 | 2.57 |
| lz4 | 8.62 | 2.70 | 1.76 | 2.83 | 3.17 | 2.92 |
| zstd | 5.69 | 4.09 | 3.60 | 4.51 | 4.74 | 4.57 |
| gzip | 7.20 | 3.23 | 3.68 | 4.59 | 4.88 | 4.66 |
| xz | 4.92 | 4.73 | 13.99 | 14.90 | 15.10 | 14.95 |
| bzip2 | 6.57 | 3.54 | 25.59 | 26.49 | 26.75 | 26.56 |
| lzma | 4.92 | 4.73 | 37.86 | 38.77 | 38.97 | 38.82 |

출력에서 볼 것:

- `none`도 해제 구간이 0.78 s다. 압축을 안 해도 cpio를 풀어 파일 885개를 tmpfs에 만드는 비용(메모리 복사 + VFS)이 있다.
- 해제는 기본적으로 **비동기**(`initramfs_async=1`)다. `Unpacking initramfs...` 후 다른 initcall이 계속 돌고, 커널은 `/init`을 실행하기 직전 `wait_for_initramfs()`에서 기다린다. 그래서 "해제 구간"과 "`/init` 도달"의 차이가 거의 일정(0.9 s)하다.
- 느린 저장장치(25 MB/s)에서는 lz4가 none을 이긴다(3.17 vs 3.27 s). 빠른 eMMC에서는 none이 이긴다. **교차점은 저장장치 대역폭 대 CPU 해제 속도로 정해진다.**
- icount는 명령어 수만 세므로 실제 코어에서의 순위와 다를 수 있다. 특히 zstd는 실제 CPU에서 gzip보다 수 배 빠르게 푸는 것으로 알려져 있는데 여기서는 비슷했다(명령어 수는 비슷하지만 분기·ILP 특성이 다르다). 실제 보드에서 `Unpacking`~`Freeing` 구간을 다시 재서 고른다. 그래도 "xz·lzma·bzip2는 부팅 경로에 두지 않는다"는 결론은 바뀌지 않는다.

### 5.4 유저 공간 단계

이 노트의 QEMU 보드는 유저 공간이 0.3 s뿐이라 측정할 거리가 적지만, 실제 장비에서는 여기가 보통 가장 크다(§2.4의 VM에서도 userspace 5.0 s > kernel 0.15 s).

- **critical path만 남긴다**: "READY"에 필요 없는 서비스는 READY 뒤로 미룬다(systemd라면 `After=`를 정리하고, 늦은 일은 별도 target이나 timer로). BusyBox라면 rcS에서 `&`로 백그라운드.
- **장치 노드**: 전체 udev 대신 devtmpfs(이미 커널이 만들어 줌) + 필요한 것만 mdev/정적 규칙.
- **앱 자체**: 거대한 동적 링크 바이너리는 로딩·재배치 비용이 크다(B2·B3). 정적 링크나 라이브러리 정리.
- **파일시스템**: 읽기 전용 squashfs/erofs 루트 + 쓰기 영역 분리는 fsck를 없앤다(J, K 모듈).

### 5.5 측정 결과 요약

| 기법 | 이 랩에서 측정한 효과 | 실제 보드에서 기대 |
|---|---|---|
| `bootdelay=0` | −2.08 s | 동일 (정확히 카운트다운만큼) |
| bootflow 스캔 → 직접 `booti` | −0.15 s | 장치 탐색이 느린 보드(USB, 네트워크)일수록 큼 |
| `Image.gz` 대신 `Image` | −4.78 s (QEMU는 읽기가 공짜) | 저장장치 속도에 따라 이득 또는 손해 |
| `quiet` | −40~60 ms | 동기 UART 콘솔이면 −1.5 s (16,977 B @115200, 계산) |
| `initcall_blacklist` 2개 | −20~60 ms | 드라이버를 Kconfig에서 빼면 훨씬 큼 |
| `lpj=` | 0 (arm64는 이미 skipped) | arm64에서는 0 |
| `driver_async_probe=*` | 잡음 수준 | 느린 probe + 멀티코어에서 의미 있음 |
| initramfs xz → lz4 | −12.07 s (14.90 → 2.83) | 저장장치·CPU에 따라, 방향은 같다 |

---

## 6. 양산에서 쓰는 큰 기법들

부팅 시간이 정말 중요한 제품(차량 후방 카메라 2 s 규정, 드론 전원 투입 후 즉시 링크)은 위의 미세 조정보다 구조를 바꾼다.

- **U-Boot Falcon 모드(SPL OS boot)**: SPL(DDR 초기화를 하는 작은 1단 로더, C4)이 U-Boot 본체를 건너뛰고 **커널을 직접** 올린다(`CONFIG_SPL_OS_BOOT`). 미리 `spl export`로 만들어 둔 DTB·인자를 쓰고, 특정 키·GPIO가 눌려 있을 때만 U-Boot 본체로 간다. U-Boot 본체의 초기화·스캔 시간이 통째로 사라진다. arm64에서 TF-A를 쓰는 경우 BL31이 BL33으로 U-Boot 대신 커널을 바로 넘기는 구성도 같은 발상이다(C5).
- **콜드 부팅을 안 한다 — suspend-to-RAM**: "전원 버튼 = 부팅"이 아니라 "전원 버튼 = resume"으로 만든다. DRAM self-refresh로 상태를 유지하고 PSCI `SYSTEM_SUSPEND`(C5)로 들어갔다가, 깨어날 때 드라이버 `resume`만 돈다. 차량 인포테인먼트가 흔히 쓴다. 대가는 대기 전력과 드라이버들의 suspend/resume 품질(N 모듈).
- **스냅샷 부팅(hibernation 계열)**: 부팅이 끝난 메모리 이미지를 저장장치에 써 두고 다음 부팅 때 그 이미지를 복원한다. 저장장치가 빠르고 RAM이 작을수록 유리하다.
- **타깃에서 계속 잰다**: systemd 이미지면 `systemd-analyze`를 CI의 하드웨어 테스트 단계에서 돌려 회귀를 잡는다. BusyBox 이미지면 `/proc/uptime`과 `CNTVCT_EL0`를 READY 시점에 로그로 남긴다. 측정이 CI에 없으면 6개월 뒤 부팅 시간은 반드시 다시 늘어 있다.
- **"READY" GPIO**: 앱이 준비되는 순간 GPIO 하나를 올리게 하고, 리셋 핀과 함께 scope·로직 분석기로 찍는다. 펌웨어·커널·유저 공간 어디에도 의존하지 않는 유일한 측정이다.

---

## 7. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="300" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160.0" y="29.3" text-anchor="middle" font-size="12" font-weight="bold">kernel_init() — PID 1 커널 스레드</text>
  <line x1="160.0" y1="41.0" x2="160.0" y2="51.0" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="10" y="52" width="300" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160.0" y="71.3" text-anchor="middle" font-size="12">kernel_init_freeable()</text>
  <line x1="160.0" y1="83.0" x2="160.0" y2="93.0" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="10" y="94" width="300" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160.0" y="113.3" text-anchor="middle" font-size="12">do_basic_setup → initcall 전부</text>
  <line x1="160.0" y1="125.0" x2="160.0" y2="135.0" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="10" y="136" width="300" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160.0" y="155.3" text-anchor="middle" font-size="12">wait_for_initramfs()</text>
  <line x1="160.0" y1="167.0" x2="160.0" y2="177.0" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="10" y="178" width="300" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160.0" y="197.3" text-anchor="middle" font-size="12">init_eaccess("/init")</text>
  <line x1="160.0" y1="209.0" x2="160.0" y2="219.0" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="10" y="220" width="300" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160.0" y="239.3" text-anchor="middle" font-size="12">async_synchronize_full · free_initmem</text>
  <line x1="160.0" y1="251.0" x2="160.0" y2="261.0" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="10" y="262" width="300" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160.0" y="281.3" text-anchor="middle" font-size="12">run_init_process("/init")</text>
  <rect x="10" y="308" width="300" height="62" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="160.0" y="326.3" text-anchor="middle" font-size="12">실패하면 init= 또는 /sbin/init, /etc/init,</text>
  <text x="160.0" y="343.3" text-anchor="middle" font-size="12">/bin/init, /bin/sh 순서로 시도</text>
  <text x="160.0" y="360.3" text-anchor="middle" font-size="12">다 실패 → panic: No working init found</text>
  <line x1="160.0" y1="293.0" x2="160.0" y2="307.0" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="330" y="136" width="160" height="46" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="410.0" y="154.8" text-anchor="middle" font-size="12">/init 없음</text>
  <text x="410.0" y="171.8" text-anchor="middle" font-size="12">→ prepare_namespace()</text>
  <line x1="311.0" y1="191.0" x2="329.0" y2="165.0" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="500" y="136" width="170" height="46" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="585.0" y="154.8" text-anchor="middle" font-size="12">root= 마운트</text>
  <text x="585.0" y="171.8" text-anchor="middle" font-size="12">→ init= 실행</text>
  <line x1="491.0" y1="159.0" x2="499.0" y2="159.0" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="330" y="236" width="340" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="500.0" y="255.3" text-anchor="middle" font-size="12" font-weight="bold">PID 1 exit_group() → do_exit()</text>
  <line x1="500.0" y1="267.0" x2="500.0" y2="283.0" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="330" y="284" width="340" height="46" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="500.0" y="302.8" text-anchor="middle" font-size="12">is_global_init(tsk) 이면</text>
  <text x="500.0" y="319.8" text-anchor="middle" font-size="12">panic("Attempted to kill init!")</text>
  <rect x="330" y="342" width="340" height="46" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="500.0" y="360.8" text-anchor="middle" font-size="12">다른 프로세스 exit → find_child_reaper()</text>
  <text x="500.0" y="377.8" text-anchor="middle" font-size="12">고아의 부모를 PID 1(또는 subreaper)로</text>
  <text x="330.0" y="226.0" font-size="12" font-weight="bold">exit 경로 (kernel/exit.c)</text>
  <text x="330.0" y="126.0" font-size="12" font-weight="bold">initramfs가 없을 때 (init/do_mounts.c)</text>
</svg>
```
그림 8 — 커널이 PID 1을 만드는 경로(`init/main.c`)와 PID 1이 죽을 때의 경로(`kernel/exit.c`). 6.18.55 소스로 확인했다.

- `start_kernel()`(C6)의 끝에서 `rest_init()`이 `kernel_init`을 **PID 1 커널 스레드**로 만든다. 이 스레드는 `kernel_init_freeable()` 안에서 `do_basic_setup()` → 모든 initcall(드라이버 등록, probe)을 돌린다. §5.2의 `initcall_debug`가 재는 것이 바로 이 구간이다.
- `wait_for_initramfs()`: 비동기로 풀던 initramfs가 끝나기를 기다린다(§5.3의 "거의 일정한 0.9 s 차이"의 이유).
- `init_eaccess(ramdisk_execute_command)`: `/init`(또는 `rdinit=`)이 실행 가능한지 본다. 없으면 `prepare_namespace()`(`init/do_mounts.c`)가 `root=`를 마운트한다. 이때 `rootwait`, `rootfstype=`, `root=PARTUUID=` 같은 인자가 쓰인다.
- `async_synchronize_full()` → `free_initmem()`("Freeing unused kernel memory: 3264K" 메시지) → `run_init_process("/init")`. `run_init_process`는 커널 스레드 안에서 `kernel_execve()`를 불러, **같은 task(PID 1)가** 유저 프로그램으로 바뀐다. 그래서 PID 1의 부모는 0이다.
- 모두 실패하면 `init=` → `/sbin/init` → `/etc/init` → `/bin/init` → `/bin/sh` 순서로 시도하고, 다 안 되면 `panic("No working init found. ...")`.
- PID 1이 `exit_group`을 부르면 `do_exit()`에서 스레드 그룹의 마지막 스레드일 때 `is_global_init(tsk)`를 검사해 `panic("Attempted to kill init! exitcode=0x%08x")`. §1.3에서 본 콜 스택(`do_group_exit` → `make_task_dead`/`vpanic`)이 이것이다.
- 다른 프로세스가 죽으면 `exit_notify()` → `forget_original_parent()` → `find_child_reaper()`가 고아들의 새 부모를 고른다: 가장 가까운 subreaper, 없으면 그 PID 네임스페이스의 init(PID 1). §1.4의 `ppid=1`.
- 시그널: `signal_struct->flags`의 `SIGNAL_UNKILLABLE`(init에게 설정)이 있으면 `sig_task_ignored()`가 기본 동작(SIG_DFL)인 시그널을 버린다. §1.5의 `kill -9 1`이 무시된 이유.

드라이버를 쓸 때 다시 만나는 지점:

- 내 드라이버의 `module_init()`은 빌트인이면 이 initcall 목록의 한 줄이다(`device_initcall` 레벨). probe에서 수백 ms를 기다리면 `initcall_debug`에 그대로 찍히고 부팅 전체가 늦어진다. 느린 HW 초기화(펌웨어 다운로드, PHY 링크)는 `PROBE_PREFER_ASYNCHRONOUS`, 지연 작업(workqueue), 또는 `-EPROBE_DEFER`로 다룬다(F, G 모듈).
- rootfs가 있는 스토리지 드라이버를 모듈로 만들면 initramfs가 필수가 된다. 빌트인(`=y`)이면 initramfs 없이 `root=`로 바로 간다 — 부팅 시간과 이미지 구조를 결정하는 Kconfig 선택이다(K 모듈).
- 콘솔 드라이버가 nbcon(`write_atomic`/`write_thread`)인지에 따라 `printk`가 부팅을 막는 정도가 달라진다(M 모듈 printk).

---

## 8. Embedded 실무에서 다시 보기

**장면 1 — 센서 키트 게이트웨이(i.MX8, eMMC, systemd)가 12 s에 부팅한다.** 첫날 할 일은 측정이다: 리셋 핀과 앱의 READY GPIO를 scope로 찍어 12 s를 확인하고, U-Boot 로그의 타임스탬프(또는 `bootstage`), `dmesg`의 `Run /init` 시각, `systemd-analyze`로 세 구간을 나눈다. 전형적인 결과는 "U-Boot 3 s(bootdelay·스캔·USB) + 커널 3 s(defconfig, verbose UART) + 유저 공간 6 s(불필요한 서비스, 네트워크 대기)"다. §10의 면접 답이 이 순서를 따른다.

**장면 2 — 국방 로봇의 dm-verity + dm-crypt 루트.** 루트는 dm-verity로 무결성을 검증하고 데이터 파티션은 LUKS로 암호화한다. 해시 루트와 키 언실(unseal)은 유저 공간 도구(`veritysetup`, TPM/OP-TEE 클라이언트)가 해야 하므로 initramfs가 필수다. §3.3의 `/init`에서 `insmod dm-mod.ko` 다음 줄이 그 자리다. 보안 요구가 initramfs 구조와 부팅 시간(해시 검증 비용)을 같이 결정한다.

**장면 3 — 필드에서 "가끔 부팅 안 됨".** 콘솔 로그 끝이 `Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000100`. `exitcode`의 상위 바이트 1 → `/init` 스크립트가 `exit 1`로 끝났다. 원인은 USB 스토리지가 늦게 나타나는 날 `mount`가 실패하고 스크립트가 끝난 것. 고치는 법: 장치 대기 루프 + 실패 시 `exec sh`(또는 복구 모드) + 커널 인자 `panic=5`로 자동 재시도.

**장면 4 — BusyBox 센서 노드의 데몬이 죽으면 다시 살아나야 한다.** `::respawn:`으로 감시하고, 더 엄격하게는 데몬이 `/dev/watchdog`을 주기적으로 쓰게 해서 데몬이 멈추면(죽지는 않고 hang) HW 워치독이 보드를 리셋하게 한다. systemd라면 `WatchdogSec=` + `sd_notify("WATCHDOG=1")`이 SW 단계, `RuntimeWatchdogSec=`가 HW 워치독 연동이다(L1).

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `/init` 스크립트 끝에 `exec` 빠뜨림 | `Attempted to kill init! exitcode=0x00000000` | 스크립트가 끝나 PID 1 exit | 마지막 줄을 `exec switch_root …` 또는 `exec /sbin/init` |
| `/init`에 실행 권한 없음 / shebang 없음 | `check access for rdinit=/init failed: -13, ignoring` / `Failed to execute /init (error -8)` | `-EACCES`(-13) / `-ENOEXEC`(-8) | `chmod +x`, 첫 줄 `#!/bin/sh`, 셸이 initramfs 안에 있는지 확인 |
| initramfs에 동적 링크 바이너리만 넣음 | `/init` 실행 실패 error -2 | 로더 `ld-linux-aarch64.so.1`·libc가 없음 | 정적 링크(BusyBox static) 또는 라이브러리까지 복사 (B2) |
| 루트 장치를 기다리지 않음 | 가끔 `mount: mounting /dev/mmcblk0p2 failed` | probe가 아직 안 끝남 | 대기 루프, 커널만 쓰면 `rootwait` |
| switch_root 전에 `/proc`·`/dev`를 안 옮김 | 진짜 init이 콘솔을 못 엶, `ps` 비어 있음 | 새 루트에 마운트 없음 | `mount --move` 또는 진짜 rcS에서 다시 마운트 |
| PID 1 앱이 `wait()`를 안 함 | 시간이 갈수록 `Z` 프로세스 증가, 결국 fork 실패 | 고아의 좀비를 아무도 안 거둠 | 진짜 init(BusyBox/systemd/tini)을 PID 1로 |
| respawn 대상이 즉시 죽음 | 콘솔이 같은 에러로 뒤덮임 | 무한 재시작 | 먼저 once로 확인, systemd는 `StartLimitBurst=` |
| initramfs를 xz로 압축 (작게 하려고) | 부팅이 수 초 늘어남 | 해제가 CPU를 많이 씀 | lz4 또는 비압축으로 재고 비교 (§5.3) |
| `Wants=`만 쓰고 `After=`를 안 씀 | 서비스가 네트워크보다 먼저 떠서 가끔 실패 | Wants는 순서를 안 정함 | `Wants=` + `After=` 짝으로 |
| 부팅 시간을 `dmesg` 마지막 줄로 잼 | 펌웨어 시간이 빠진 짧은 숫자 | 커널 시계는 커널이 켠다 | 리셋 기준 시계(GPIO+scope, `CNTVCT_EL0`)로 |

---

## 10. 면접에서 이렇게 말한다

**Q.** What does PID 1 have to do, and what happens if it exits?

**A.** PID 1은 커널이 마지막에 실행하는 첫 유저 프로세스다. 가상 파일시스템을 마운트하고 서비스를 띄우고, 고아가 된 프로세스를 입양해 `wait()`로 좀비를 거둬야 한다. 핸들러가 없는 시그널은 커널이 버리므로 종료 요청도 핸들러로 받는다. PID 1이 exit하면 커널은 "Attempted to kill init!"으로 panic한다. 끄려면 exit가 아니라 sync 후 `reboot()` syscall을 쓴다.

> PID 1 is the first user process the kernel starts. It mounts /proc, /sys and /dev, starts and supervises services, and it inherits every orphaned process, so it must keep calling wait() or zombies pile up. The kernel drops signals PID 1 hasn't installed a handler for, so even SIGKILL is ignored. And it must never exit: if it does, do_exit() sees it's the global init and panics with "Attempted to kill init!". To shut down you sync and call reboot(), you don't exit.

**꼬리 질문**: "Why do Docker images use tini?" → 앱이 PID 1이 되면 좀비 회수와 시그널 전달을 안 하므로, 그 일만 하는 작은 init을 앞에 둔다.

**Q.** Why would you use an initramfs? When can you skip it?

**A.** 진짜 루트를 마운트하기 전에 유저 공간 도구가 필요할 때 쓴다: 스토리지 드라이버가 모듈일 때, dm-crypt 해독이나 dm-verity 검증, LVM, A/B 슬롯 선택, 복구 셸. 그 일을 마치면 `switch_root`로 진짜 루트의 init에 넘긴다. 스토리지 드라이버가 빌트인이고 암호화가 없으면 `root=PARTUUID=… rootwait`로 건너뛸 수 있고, 그게 부팅도 더 빠르다.

> An initramfs is early userspace: a cpio archive the kernel unpacks into a tmpfs and runs /init from. You need it when mounting the real root requires userspace work: loading storage modules, unlocking dm-crypt with a key from the TPM or OP-TEE, setting up dm-verity, picking an A/B slot, or offering a recovery shell. Then it mounts the real root and exec's switch_root, which deletes the initramfs contents to free the RAM, chroots, and execs the real init, still as PID 1. If the storage driver is built in and there's no encryption, I skip it and boot with root=PARTUUID and rootwait.

**꼬리 질문**: "Why switch_root and not pivot_root?" → initramfs의 rootfs는 unmount할 수 없는 특별한 마운트라 pivot_root가 안 되고, 내용을 지운 뒤 새 루트를 그 위로 move하고 chroot한다.

**Q.** BusyBox init or systemd for an embedded product?

**A.** 서비스가 몇 개뿐이고 rootfs를 수 MB로 줄여야 하면 BusyBox init과 inittab: 작고 동작이 뻔하다. 서비스가 많고 의존성·재시작·워치독·로그 관리가 필요하면 systemd: 의존성 그래프로 병렬 시작하고 `systemd-analyze`로 부팅을 해부할 수 있다. 대가는 크기와 학습 비용이다.

> For a small sensor node with a handful of daemons and a few-megabyte rootfs, BusyBox init with an inittab is enough: sysinit runs rcS, respawn supervises the daemons, shutdown actions clean up. For a gateway with dozens of services I'd use systemd: dependency-ordered parallel startup, Restart= and WatchdogSec= per service, journald, and systemd-analyze to profile boot. The cost is footprint and complexity, which on a Jetson or i.MX8-class board is usually fine.

**Q.** How would you cut boot time from 12 seconds to 3?

**A.** 먼저 "부팅 완료"를 정의하고(앱 READY GPIO), 리셋부터 그 GPIO까지 scope로 12 s를 확인한 뒤 펌웨어·커널·유저 공간으로 나눈다. 그다음 큰 것부터: U-Boot는 bootdelay 0, 스캔 대신 직접 부팅, 필요하면 Falcon 모드. 커널은 `quiet`, `initcall_debug`로 느린 드라이버를 찾아 Kconfig에서 빼고, 보드용 최소 설정으로 이미지를 줄인다. 유저 공간은 `systemd-analyze critical-chain`으로 READY까지의 경로만 남기고 나머지를 뒤로 미룬다. 매 단계 다시 재고, 측정을 CI에 넣어 회귀를 막는다. 그래도 안 되면 suspend-to-RAM으로 콜드 부팅 자체를 없앤다.

> First I'd define "booted" — say, the app raising a READY GPIO — and measure reset-to-GPIO on a scope, then split the 12 seconds into firmware, kernel and userspace using U-Boot timestamps, the dmesg time of "Run /init", and systemd-analyze. Then I attack the biggest chunk first. In U-Boot: bootdelay=0, boot the known image directly instead of scanning devices, and if needed Falcon mode so SPL loads the kernel. In the kernel: quiet, because a synchronous 115200 console alone can cost over a second; initcall_debug to find slow probes; and a board-specific defconfig instead of the generic one. In userspace: critical-chain to see what's actually on the path to the app, and move everything else after it. I re-measure after each change — in my QEMU lab, bootdelay alone was 2 seconds while lpj did nothing — and I put the measurement in CI. If 3 seconds still isn't reachable, suspend-to-RAM instead of cold boot.

**꼬리 질문**: "What did you measure that surprised you?" → 압축이 항상 이득이 아니라는 것: 같은 initramfs가 xz로는 14.9 s, lz4로는 2.8 s였다(QEMU icount). 저장장치 대역폭과 CPU 해제 속도를 같이 봐야 한다.

**Q.** How do you measure boot time accurately?

**A.** 하나의 기준 시계로 마커를 잡는다. 가장 확실한 건 리셋 핀과 READY GPIO를 scope로 찍는 것이다. 소프트웨어로는 리셋부터 도는 arm64 generic timer를 READY 시점에 읽으면 펌웨어 시간까지 포함된다. `dmesg` 타임스탬프나 `/proc/uptime`은 커널이 시계를 켠 뒤부터라 펌웨어가 빠진다. 같은 측정을 여러 번 하고 분포를 본다.

> I want one clock for all markers. The gold standard is a scope on the reset line and a READY GPIO. In software, on arm64 I read CNTVCT_EL0 at the READY point — the generic timer counts from reset, so it includes firmware time that dmesg and /proc/uptime miss. For the kernel part I use printk timestamps and initcall_debug, for userspace systemd-analyze. And I repeat runs and look at the spread; in emulation I even used QEMU's icount mode to make the timing deterministic.

**Q.** A board panics with "Attempted to kill init! exitcode=0x00000100" only sometimes. How do you debug it?

**A.** exitcode의 상위 바이트가 1이니 PID 1(보통 initramfs `/init` 스크립트)이 `exit 1`로 끝났다. "가끔"이면 타이밍 문제일 가능성이 높다 — 루트 장치가 늦게 나타나 mount가 실패하는 경우가 대표적이다. 스크립트에 `set -x`나 echo를 넣어 어디서 끝나는지 보고, 장치 대기 루프와 실패 시 `exec sh`를 넣는다.

> The exit code is in wait-status format, so 0x100 means PID 1 called exit(1) — no signal involved. Usually that's the initramfs /init script falling off the end after a failed command. "Sometimes" smells like a race: the root device or a USB stick appearing late. I'd add tracing to the script, add a wait loop for the device, never let the script exit — exec a rescue shell on failure — and set panic=5 so the field unit reboots instead of hanging.

**Q.** What's the difference between Wants=, Requires= and After= in systemd?

**A.** Wants와 Requires는 "같이 시작하라"는 끌어오기이고 순서는 정하지 않는다. Requires는 상대가 실패하거나 멈추면 나도 멈춘다. After는 순서만 정하고 끌어오지는 않는다. 그래서 보통 `Wants=` + `After=`를 짝으로 쓴다.

> Wants= and Requires= pull another unit into the transaction; Requires= also stops me if it fails or stops. Neither orders anything. After= only orders startup and doesn't pull anything in. So the normal pattern is Wants=network-online.target plus After=network-online.target. Getting this wrong gives you services that start "too early" only on some boots.

---

## 11. 직접 해보기

1. **개념**: `exitcode=0x0000000b`로 끝나는 "Attempted to kill init!"은 무슨 뜻인가? `0x00000300`과 어떻게 다른가?
   정답/힌트: 하위 7비트가 시그널 번호다. 0x0b = 11 = SIGSEGV — `/init`이 segfault로 죽었다. 0x300은 시그널 없이 `exit(3)`.

2. **코드**: §1.4의 `c7_pid1.c`에 `SIGCHLD` 핸들러를 달아, `sleep(2)` 후 한 번 거두는 대신 자식이 끝나는 즉시 회수하도록 바꿔라. 그리고 SIGTERM을 받으면 `reboot(RB_POWER_OFF)`하도록 하고, 셸에서 `kill -TERM 1`로 확인하라.
   정답/힌트: `sigaction(SIGCHLD, …)` 핸들러 안에서 `while (waitpid(-1, &st, WNOHANG) > 0);`. 핸들러를 달면 SIGTERM은 더 이상 무시되지 않는다(§1.5).

3. **initramfs**: §3.3의 `/init`을 `root=LABEL=c7root`로도 동작하게 고쳐라 (BusyBox `findfs` 사용).
   정답/힌트: `case $ROOT in LABEL=*|UUID=*) ROOT=$(findfs $ROOT);; esac`. `mkfs.ext4 -L c7root`로 라벨을 붙였다.

4. **측정**: `c7_bt.py`로 `initramfs_async=0`을 줬을 때와 기본값의 `/init` 도달 시간을 gzip 23 MB initramfs로 비교하라. 단일 코어 QEMU에서 차이가 클까 작을까?
   정답/힌트: 비동기는 해제를 다른 initcall과 겹치게 할 뿐 CPU 일을 줄이지 않는다. 코어가 하나면 차이가 작을 것으로 예상되지만, 직접 재서 확인하라. `-smp 4`로도 해 보라.

5. **systemd**: VM 호스트에서 `systemd-analyze critical-chain ssh.service`(또는 다른 서비스)를 돌려 그 서비스가 왜 그 시각에 시작됐는지 설명하라. `systemd-analyze plot`의 SVG에서 같은 사실을 찾아라.
   정답/힌트: `@`는 활성화 시각, `+`는 자체 소요. 체인에서 `+`가 큰 unit이 줄일 대상이다.

6. **/proc 관찰**: §2.2의 BusyBox 보드에서 `cat /proc/1/status`의 `SigCgt`를 읽어 BusyBox init이 어떤 시그널을 잡는지 비트로 해석하라.
   정답/힌트: SIGHUP(1), SIGINT(2), SIGQUIT(3), SIGUSR1(10), SIGUSR2(12), SIGTERM(15) 등이 켜져 있어야 한다. 비트 n-1이 시그널 n.

7. **U-Boot**: `c7_env_direct`에서 `Image` 대신 `Image.gz`를 쓰되 lz4로 압축한 커널(`Image.lz4`)을 만들어 U-Boot `booti`로 부팅해 보라(이 U-Boot는 `CONFIG_LZ4=y`). gzip보다 얼마나 빠른가?
   정답/힌트: §5.3의 legacy 형식이 아니라 U-Boot는 LZ4 frame 형식을 기대한다 — `lz4.frame`으로 만든다. `kernel_comp_addr_r`, `kernel_comp_size` 설정은 gz와 같다.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| PID 1 / init | 첫 유저 프로세스 | 커널이 `run_init_process()`로 실행, 절대 끝나면 안 됨 |
| zombie | 끝났지만 회수 안 된 프로세스 | 부모가 `wait()`해야 사라짐 |
| orphan / reparent | 부모가 먼저 죽은 프로세스 / 부모 교체 | 새 부모는 subreaper 또는 PID 1 |
| subreaper | 고아를 대신 받는 조상 | `prctl(PR_SET_CHILD_SUBREAPER, 1)` |
| `SIGNAL_UNKILLABLE` | init의 시그널 보호 플래그 | 핸들러 없는 시그널을 커널이 버림 |
| inittab | BusyBox init 설정 파일 | `<tty>::<action>:<process>` |
| respawn | 죽으면 다시 실행 | BusyBox init 액션 |
| unit / target | systemd 작업 단위 / 동기화 지점 | `.service`, `.target` 파일 |
| critical chain | 최종 target까지의 가장 긴 의존 경로 | 부팅 단축의 대상 |
| initramfs | 압축 cpio 임시 루트 | 커널이 tmpfs에 풀고 `/init` 실행 |
| early userspace | 진짜 루트 이전의 유저 공간 | 모듈 로드·해독·루트 찾기 |
| `switch_root` | initramfs → 진짜 루트 전환 | 내용 삭제 + move + chroot + exec |
| `rdinit=` / `init=` | init 경로 커널 인자 | initramfs용 / 진짜 루트용 |
| `rootwait` | 루트 장치가 나타날 때까지 대기 | 커널이 직접 루트를 마운트할 때 |
| initcall | 커널 초기화 함수 | 빌트인 드라이버의 `module_init` 포함 |
| `initcall_debug` | initcall별 소요 시간 출력 | `KERN_DEBUG`라 `dmesg`로 본다 |
| `initcall_blacklist=` | 지정 initcall 건너뛰기 | `CONFIG_KALLSYMS` 필요, 디버깅용 |
| `lpj=` | loops_per_jiffy 지정 | arm64에선 calibration이 이미 skipped |
| `driver_async_probe=` | 드라이버 probe 비동기화 | `*`이면 전부 |
| icount | QEMU 명령어 수 기반 가상 시간 | 호스트 부하와 무관한 재현성 |
| `CNTVCT_EL0` | arm64 가상 카운터 | 리셋부터의 시간, EL0에서 읽기 가능 |
| Falcon mode | SPL이 커널을 직접 부팅 | U-Boot 본체 생략 |
| nbcon | non-blocking console | printk 출력을 프린터 스레드로 넘김 |

---

## 13. 요약 & 체크리스트

커널은 드라이버를 올리고 rootfs를 준비한 뒤 PID 1 하나를 실행하고 물러난다. PID 1은 가상 FS를 마운트하고, 서비스를 띄우고 감시하며, 고아의 좀비를 거두고, 절대 exit하지 않는다(exit하면 커널이 "Attempted to kill init!"으로 panic한다 — 직접 봤다). 구현은 직접 쓴 `/init`, BusyBox init + inittab, systemd 중에서 장비 크기와 서비스 수에 맞춰 고른다. initramfs는 진짜 루트를 마운트하기 위한 early userspace이고, 할 일을 마치면 `switch_root`로 같은 PID 1을 진짜 init으로 바꾼다 — QEMU에서 virtio 디스크의 ext4로 직접 만들었다. 부팅 시간은 리셋부터 READY까지 하나의 시계로 재야 하고(`CNTVCT_EL0`, GPIO+scope), QEMU에서는 icount로 재현성을 확보했다. 측정해 보니 이 보드에서는 `bootdelay` 2 s, 커널 압축 4.8 s, initramfs 압축 선택 최대 36 s가 큰 덩어리였고, `quiet`·`initcall_blacklist`·`lpj`·async probe는 수십 ms 이하였다 — 그래서 "재고, 큰 것부터, 다시 잰다"가 유일한 규칙이다.

- [ ] PID 1의 여섯 가지 책임을 설명하고, exit하면 왜 panic인지 커널 함수 이름(`do_exit`, `is_global_init`)으로 말할 수 있다
- [ ] 좀비와 고아를 만들고 PID 1에서 `waitpid(-1, …, WNOHANG)`으로 거두는 C 코드를 짤 수 있다
- [ ] `kill -9 1`이 무시되는 이유(`SIGNAL_UNKILLABLE`)를 설명할 수 있다
- [ ] BusyBox inittab의 sysinit/wait/once/respawn/shutdown 순서와 시그널(SIGUSR1/SIGUSR2/SIGTERM/SIGHUP)을 설명할 수 있다
- [ ] systemd의 `Wants=`/`Requires=`/`After=` 차이와 target의 역할을 설명하고, unit 파일을 쓰고 `systemd-analyze verify`로 검사할 수 있다
- [ ] `systemd-analyze`, `blame`, `critical-chain`, `plot`의 숫자를 읽고 무엇을 줄일지 고를 수 있다
- [ ] initramfs의 네 가지 역할과 "없어도 되는 경우"를 말할 수 있다
- [ ] initramfs `/init` → 루트 대기 → mount → `mount --move` → `exec switch_root`를 직접 짜고 QEMU에서 부팅할 수 있다
- [ ] `switch_root`와 `pivot_root`의 차이를 설명할 수 있다
- [ ] 리셋부터 READY까지 구간을 나눠 재는 방법(GPIO+scope, `CNTVCT_EL0`, dmesg, `systemd-analyze`)을 설명할 수 있다
- [ ] bootdelay, 직접 부팅, `quiet`, `initcall_blacklist`, initramfs 압축의 효과를 측정값과 함께 말할 수 있다
- [ ] "12 s → 3 s" 질문에 측정 → 펌웨어 → 커널 → 유저 공간 → 구조 변경(Falcon, suspend) 순서로 답할 수 있다

---

## 참고 자료

- Linux 커널 문서 — Ramfs, rootfs and initramfs: https://docs.kernel.org/filesystems/ramfs-rootfs-initramfs.html
- Linux 커널 문서 — Using the initial RAM disk (initrd): https://docs.kernel.org/admin-guide/initrd.html
- Linux 커널 문서 — The kernel's command-line parameters (`rdinit=`, `initcall_blacklist=`, `initcall_debug`, `driver_async_probe=`, `lpj=`, `initramfs_async=`): https://docs.kernel.org/admin-guide/kernel-parameters.html
- 커널 소스(6.18.55): `init/main.c`(`kernel_init`, `kernel_init_freeable`, `initcall_blacklist`), `init/initramfs.c`(`wait_for_initramfs`), `init/do_mounts.c`(`prepare_namespace`), `kernel/exit.c`(`do_exit`, `find_child_reaper`), `drivers/base/dd.c`(`driver_async_probe`) — https://elixir.bootlin.com/linux/v6.18/source
- BusyBox 소스: `init/init.c`, `examples/inittab`, `util-linux/switch_root.c` — https://busybox.net
- man7.org: [switch_root(8)](https://man7.org/linux/man-pages/man8/switch_root.8.html), [pivot_root(2)](https://man7.org/linux/man-pages/man2/pivot_root.2.html), [prctl(2)](https://man7.org/linux/man-pages/man2/prctl.2.html) (`PR_SET_CHILD_SUBREAPER`), [wait(2)](https://man7.org/linux/man-pages/man2/wait.2.html), [reboot(2)](https://man7.org/linux/man-pages/man2/reboot.2.html), [systemd-analyze(1)](https://man7.org/linux/man-pages/man1/systemd-analyze.1.html)
- systemd 문서 — systemd.unit, systemd.service: https://www.freedesktop.org/software/systemd/man/latest/systemd.unit.html
- U-Boot 문서 — booti 명령(`kernel_comp_addr_r`): https://docs.u-boot.org/en/latest/usage/cmd/booti.html , Falcon 모드: https://docs.u-boot.org/en/latest/develop/falcon.html
- Bootlin — Embedded Linux boot time optimization 교육 자료: https://bootlin.com/doc/training/boot-time/
- eLinux.org — Boot Time: https://elinux.org/Boot_Time
- Michael Kerrisk, "The Linux Programming Interface" — 26장(Monitoring Child Processes: 좀비·고아·`SIGCHLD`)
- QEMU 문서 — TCG Instruction Counting (`-icount`): https://www.qemu.org/docs/master/devel/tcg-icount.html
- 연결 노트: A2(프로세스·wait), B2(정적 링크·libc), C3(U-Boot 환경), C5(TF-A·PSCI), C6(커널 시작·initcall), L1(systemd 깊이 보기)
