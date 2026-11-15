---
id: ELX-B04
time: 70분
updated: 2026-11-15
---
# ELX-B04 QEMU virt에서 커널+initramfs 부팅

## 학습 목표

- BusyBox를 **정적(static)** 으로 빌드하고, `find | cpio -o -H newc` 로 initramfs를 만들어 QEMU `virt` 보드에서 부팅한다.
- 커널이 initramfs를 풀고 `/init` 을 **PID 1** 로 실행하기까지의 길(`init/main.c`)을 함수 이름으로 따라간다.
- PID 1이 해야 하는 일 — 가상 파일시스템 마운트, 콘솔 확보, **고아(orphan) 거두기**, 절대 `exit` 하지 않기 — 을 C로 직접 구현하고 게스트 안에서 자동 검증한다.
- 커널 명령줄이 init의 `envp`/`argv` 로 넘어가는 규칙(`key=val` → 환경 변수, `--` 뒤 → argv, 점 들어간 key → 버림)을 실측으로 설명한다.
- 대표적인 부팅 실패 4가지(`/init` 없음, 동적 `/init`, init 종료, `/dev/console` 없음)의 **실제 커널 메시지**를 보고 원인을 바로 말한다.

## 1. 문제

보드 bring-up에서 "커널은 떴는데 셸이 안 나온다"는 가장 흔한 첫 장애이고, 면접에서도 "What does PID 1 do? Why do we need an initramfs? What happens if init exits?"로 바로 나온다 — 커널과 유저 공간의 **경계 한 줄**을 정확히 아는지 보는 질문이다.

SSD 펌웨어로 치면 initramfs는 "ROM 부트로더가 RAM에 올려 주는 작은 런타임 이미지", `/init` 은 그 이미지의 `main()` 이다. 차이는 하나다: 펌웨어의 `main()` 은 무한 루프로 끝나지만, 리눅스의 PID 1은 **끝나는 순간 커널이 panic** 한다. 그래서 PID 1은 마지막에 `reboot(RB_POWER_OFF)` 같은 시스템 콜로 "정상 종료"를 해야 한다.

> 환경: Lima VM `elinux`(aarch64, gcc 15) 안에서 빌드하고, 게스트는 **QEMU TCG `-machine virt -cpu cortex-a57 -smp 1 -m 512M` + Linux 6.18.55 (`/var/lab/out/Image`, defconfig)** 이다. BusyBox 1.37.0은 `build_busybox.sh` 로 직접 정적 빌드했다(아래 5.0). 아래 커널 로그의 시간은 **TCG 에뮬레이션 시간** 이라 실제 보드 시간이 아니다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">QEMU -kernel/-initrd 부팅: initramfs 가 /init (PID 1) 이 되기까지</text>
<rect x="10" y="35" width="170" height="62" class="f-mut" stroke="currentColor"/>
<text x="20" y="55" fill="currentColor">QEMU (부트로더 역할)</text>
<text x="20" y="73" fill="currentColor" font-size="12" font-family="monospace">Image -&gt; 커널 진입</text>
<text x="20" y="89" fill="currentColor" font-size="12" font-family="monospace">i.gz @ 0x48000000</text>
<rect x="200" y="35" width="250" height="62" class="f-mut" stroke="currentColor"/>
<text x="210" y="55" fill="currentColor">DTB /chosen</text>
<text x="210" y="73" fill="currentColor" font-size="12" font-family="monospace">linux,initrd-start/-end</text>
<text x="210" y="89" fill="currentColor" font-size="12" font-family="monospace">bootargs = rdinit=/init ...</text>
<rect x="470" y="35" width="260" height="62" class="f-acc" stroke="currentColor"/>
<text x="480" y="55" fill="currentColor">populate_rootfs (0.30 s)</text>
<text x="480" y="73" fill="currentColor" font-size="12">① 내장 cpio (/dev/console 등)</text>
<text x="480" y="89" fill="currentColor" font-size="12">② 우리 cpio(newc+gzip)를 그 위에</text>
<line x1="180" y1="66" x2="200" y2="66" stroke="currentColor"/>
<line x1="450" y1="66" x2="470" y2="66" stroke="currentColor"/>
<rect x="470" y="120" width="260" height="62" class="f-acc" stroke="currentColor"/>
<text x="480" y="140" fill="currentColor">rootfs = tmpfs (root= 없음)</text>
<text x="480" y="158" fill="currentColor" font-size="12">console_on_rootfs(): /dev/console</text>
<text x="480" y="174" fill="currentColor" font-size="12">-&gt; fd 0, 1, 2</text>
<line x1="600" y1="97" x2="600" y2="120" stroke="currentColor"/>
<rect x="200" y="120" width="250" height="62" class="f-acc" stroke="currentColor"/>
<text x="210" y="140" fill="currentColor">init_eaccess("/init")</text>
<text x="210" y="158" fill="currentColor" font-size="12">있음 -&gt; run_init_process (0.75 s)</text>
<text x="210" y="174" fill="currentColor" font-size="12">없음 -&gt; prepare_namespace (root=)</text>
<line x1="470" y1="151" x2="450" y2="151" stroke="currentColor"/>
<rect x="10" y="120" width="170" height="62" class="f-ok" stroke="currentColor"/>
<text x="20" y="140" fill="currentColor">/init = PID 1</text>
<text x="20" y="158" fill="currentColor" font-size="12">우리 C 프로그램</text>
<text x="20" y="174" fill="currentColor" font-size="12">(gcc -static)</text>
<line x1="200" y1="151" x2="180" y2="151" stroke="currentColor"/>
<rect x="10" y="205" width="720" height="82" class="f-ok" stroke="currentColor"/>
<text x="20" y="223" fill="currentColor" font-size="12">PID 1 의 일 ① mount: proc /proc, sysfs /sys, devtmpfs /dev</text>
<text x="20" y="240" fill="currentColor" font-size="12">② fd 0/1/2 확인 (닫혀 있으면 /dev/console 다시 열기)</text>
<text x="20" y="257" fill="currentColor" font-size="12">③ /proc/cmdline 의 B04_SCRIPT= -&gt; fork + exec /bin/sh 스크립트  ④ waitpid(-1): 자식+고아</text>
<text x="20" y="274" fill="currentColor" font-size="12">⑤ sync() + reboot(RB_POWER_OFF) -&gt; "reboot: Power down" (1.93 s)</text>
<line x1="95" y1="182" x2="95" y2="205" stroke="currentColor"/>
<rect x="10" y="297" width="720" height="55" class="f-warn" stroke="currentColor"/>
<text x="20" y="317" fill="currentColor" font-size="12">PID 1 이 return/exit -&gt; Kernel panic: Attempted to kill init! exitcode=0x00000100</text>
<text x="20" y="339" fill="currentColor" font-size="12">/init 이 없음 -&gt; root= 를 찾다가 VFS: Unable to mount root fs on unknown-block(0,0)</text>
</svg>
```

그림 1 — 이 레슨의 실측 경로. 시간은 `-smp 1` TCG 로그(`Unpacking initramfs...` 0.303 s, `Run /init as init process` 0.754 s, `reboot: Power down` 1.929 s)에서 가져왔다.

### Part A — /init 의 기본기: 마운트와 콘솔

`init.c` 의 `mount_early()` 는 `proc→/proc`, `sysfs→/sys`, `devtmpfs→/dev` 를 마운트한다 (디렉터리가 없으면 `mkdir`, 이미 마운트돼 있으면 `EBUSY` 를 성공으로 친다). `fix_console()` 은 fd 0/1/2 중 하나라도 닫혀 있으면 `/dev/console` 을 다시 열어 `dup2` 한다. 자기 검사: PID==1, `/proc/mounts` 에 세 줄, stdout이 문자 장치 5:1(`/dev/console`).

### Part B — 커널 명령줄과 커널→init 인자

`cmdline_get(cmdline, key, val, n)` 으로 `/proc/cmdline` 에서 `B04_SCRIPT=` 값을 찾는다. 접두어 함정(`XB04_SCRIPT=`, `B04_SCRIPTX=`), 값 없는 플래그(`quiet`), 따옴표 값(`msg="hello world"`), 같은 key 반복(마지막이 이김), 버퍼 부족(-1 + 잘린 값)을 처리한다. 그리고 **커널이 같은 값을 init의 환경 변수로도 넘겼는지**(`getenv`), `--` 뒤의 단어가 `argv[1]` 로 왔는지 확인한다.

### Part C — 고아 거두기, 스크립트 실행, 끄기

- `reap_all()` — 블로킹 `waitpid(-1)` 를 `ECHILD` 까지 돌려 자식과 **입양된 고아** 를 모두 거둔다.
- `spawn_wait(argv, ctty, &orphans)` — `fork`+`execv` 로 BusyBox 스크립트를 비대화식으로 돌리고, 기다리는 동안 다른 자식이 먼저 끝나면 고아로 센다.
- `power_off()` — `sync()` + `reboot(RB_POWER_OFF)`. PID 1은 `return` 하면 안 된다.

`check.sh` 가 이 프로그램을 `gcc -static` 으로 빌드하고, BusyBox + 애플릿 링크 + 스크립트와 함께 cpio로 묶어 QEMU에서 부팅한 뒤, 콘솔 로그의 `PASS:`/`FAIL:` 줄 수와 `reboot: Power down` 으로 판정한다. `check.sh demo <이름>` 은 실패 4가지를 실제로 재현한다.

## 2. Problem (English)

> **Write a minimal PID 1 for an initramfs and boot it on QEMU `virt`.**
>
> Build a static BusyBox and a static C program installed as `/init` in a `newc` cpio archive. Boot `qemu-system-aarch64 -machine virt -kernel Image -initrd initramfs.cpio.gz -append "console=ttyAMA0 rdinit=/init B04_SCRIPT=/etc/selftest.sh -- b04arg"`. Implement:
>
> 1. `int mount_early(void)` — mount `proc`, `sysfs`, `devtmpfs` on `/proc`, `/sys`, `/dev` (create the directories; treat `EBUSY` as success). Return how many succeeded.
> 2. `int fix_console(void)` — if any of fds 0/1/2 is closed, open `/dev/console` and `dup2` it onto 0, 1, 2. Return 0 (fine), 1 (repaired), -1 (failed).
> 3. `int cmdline_get(const char *cmdline, const char *key, char *val, size_t n)` — whitespace-separated tokens, double quotes protect spaces and are stripped, exact key match only, last occurrence wins, a bare `key` yields `""`. Return 1/0, or -1 if `val` is too small (store the truncated, NUL-terminated value).
> 4. `int reap_all(int *codes, int max)` — block in `waitpid(-1, ...)` until `ECHILD`, return the count, record exit codes (`128+sig` for signals).
> 5. `int spawn_wait(char *const argv[], int ctty, int *orphans)` — fork/exec `argv[0]`; wait for *that* pid while counting any other reaped child as an orphan; with `ctty`, the child calls `setsid()` and `TIOCSCTTY`.
> 6. `void power_off(void)` — flush, `sync()`, `reboot(RB_POWER_OFF)`. PID 1 must never return.
>
> Constraints: no libc in the initramfs (static link), the kernel gives PID 1 no `PATH`, verdicts must come from counts and exact strings, never from timing.

## 3. 시작 코드

<!-- code: starter.c -->

게스트 안에서 `/init` 이 실행하는 BusyBox 스크립트와, 빌드·cpio·부팅·판정을 하는 스크립트:

<!-- code: selftest.sh -->

<!-- code: check.sh -->

Mac에서 한 줄로 (VM 안에서 빌드 → cpio → QEMU 부팅 → 판정, 약 3–5초):

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B04/check.sh starter
```

starter 실측 출력 (TODO가 비어 있어 마운트·파싱·reaping이 실패하고, `power_off()` 가 아무것도 안 하므로 `main` 이 return → panic):

```text
B04 init: pid=1 mounts=0 console=ok argc=2
PASS: A1 running as PID 1
FAIL: A2 mount_early() mounted proc+sysfs+devtmpfs  (/tmp/ELX-B04_work/starter.597636/starter.c:175)
FAIL: A3 /proc/mounts lists proc, sysfs, devtmpfs  (/tmp/ELX-B04_work/starter.597636/starter.c:176)
PASS: A4 stdout is /dev/console (char 5:1)
…
FAIL: C3 no zombies, ECHILD  (/tmp/ELX-B04_work/starter.597636/starter.c:209)
B04 init: no B04_SCRIPT= -> interactive shell, 'exit' powers off
6/16 passed
B04 init: power off
[    0.857743] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000100
no clean 'reboot: Power down'
kernel panicked
6/27 passed (FAIL lines: 10)
RESULT: FAIL (starter) log: /tmp/ELX-B04_work/starter.597636/console.log
```

`/proc` 이 마운트되지 않아 `/proc/cmdline` 을 못 읽으니 스크립트 경로도 못 찾고 "interactive shell" 분기로 빠진 것이다. A4가 PASS인 이유는 5절 Part A에서 설명한다 (커널 내장 cpio의 `/dev/console`). 목표: `27/27 passed`, `RESULT: PASS (solution)`.

## 4. 힌트

<details>
<summary>힌트 1 — mount(2) 인자 순서</summary>

`mount(source, target, fstype, flags, data)`. 가상 FS는 source가 의미 없으므로 관례상 fstype 이름을 넣는다: `mount("proc", "/proc", "proc", MS_NOSUID, NULL)`. 마운트 지점 디렉터리는 먼저 있어야 한다 → `mkdir(path, 0755)` 후 `EEXIST` 는 무시.

</details>

<details>
<summary>힌트 2 — fd가 열려 있는지 묻는 가장 싼 방법</summary>

`fcntl(fd, F_GETFD)` 가 -1 이고 `errno == EBADF` 면 닫힌 fd다. `open()` 은 **가장 작은 빈 번호**를 주므로, fd 0이 비어 있으면 `/dev/console` 이 0번으로 열린다. 그래서 `if (fd != i) dup2(fd, i)` 로 자기 자신을 dup하지 않게 한다.

</details>

<details>
<summary>힌트 3 — 토큰부터 자르고, key는 "정확히" 비교</summary>

공백으로 토큰을 자르되 `"` 안의 공백은 건너뛴다. 토큰이 `key` 로 시작해도 바로 다음 글자가 `=` 이거나 토큰 끝이어야 같은 key다 (`strncmp` 만 쓰면 `B04_SCRIPTX=` 도 맞아 버린다). 반복되면 덮어쓰기 → 마지막이 이긴다.

</details>

<details>
<summary>힌트 4 — 고아는 "누가" 거두나</summary>

부모가 먼저 죽은 프로세스는 커널이 PID 1의 자식으로 붙인다(reparent). 그러니 PID 1의 `waitpid(-1, …)` 에 **내가 fork 하지 않은 pid** 가 돌아올 수 있다. `spawn_wait` 는 반환 pid가 내 자식이 아니면 고아로 세고 계속 기다린다. `EINTR` 은 다시 시도.

</details>

<details>
<summary>힌트 5 — 끝내는 법</summary>

`exit()` 도 `return` 도 안 된다. `<sys/reboot.h>` 의 `reboot(RB_POWER_OFF)` 는 PID 1에게 허락된 정상 종료다. 그 전에 `fflush(stdout)` 와 `sync()`. QEMU는 `-no-reboot` 이 없으면 리부트 요청에 다시 부팅하므로 check.sh는 `-no-reboot panic=-1` 을 쓴다 — panic도 즉시 "리부트 → QEMU 종료"가 되어 starter가 멈추지 않는다.

</details>

## 5. 풀이

### 5.0 BusyBox 정적 빌드 (실측)

<!-- code: build_busybox.sh -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B04/build_busybox.sh
```

```text
CONFIG_STATIC=y
# CONFIG_SHA1_HWACCEL is not set
# CONFIG_SHA256_HWACCEL is not set
# CONFIG_TC is not set
build time: 32 s (-j2, shared VM)
busybox: ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=183bf8e8…, for GNU/Linux 3.7.0, stripped
-rwxrwxr-x 1 donh donh 2242744 Nov 15 15:17 busybox
applets: 402
```

- 크기 2,242,744 바이트는 랩 빌드(`/var/lab/build/busybox/busybox`)와 같다 — 랩 스크립트 `/var/lab/build_lab.sh` 도 "defconfig + STATIC + 이 세 개 끔"이기 때문이다. `check.sh` 는 이 빌드가 있으면 그것을, 없으면 랩 바이너리를 쓴다.
- `SHA*_HWACCEL` 을 켠 채 arm64에서 빌드하면 실제로 이렇게 깨진다 (같은 빌드 디렉터리에서 켜 보고 확인): `libbb/hash_md5_sha.c:1316:35: error: 'sha1_process_block64_shaNI' undeclared`. SHA-NI 구현이 x86 전용 `.S` 파일(`hash_sha1_hwaccel_x86-64.S` 등)에만 있기 때문이다.
- 왜 static인가: initramfs에는 `/lib/ld-linux-aarch64.so.1` 도 `libc.so.6` 도 없다. 동적 바이너리는 커널이 ELF의 `PT_INTERP` 를 열다가 `ENOENT` 로 실패한다 (아래 실패 재현 ②, ELX-B02 참고).

### Part A — 커널이 /init 을 부르기까지, 그리고 마운트·콘솔

`/var/lab/src/linux-6.18.55/init/main.c` 를 따라가면:

| 순서 | 함수 (파일) | 하는 일 |
|---|---|---|
| 1 | `populate_rootfs` → `do_populate_rootfs` (`init/initramfs.c`) | **커널 이미지에 내장된 cpio** 를 먼저 풀고, 부트로더가 준 initrd(cpio)를 그 위에 푼다 |
| 2 | `init_rootfs` (`init/do_mounts.c`) | `root=` 가 없고 `CONFIG_TMPFS=y` 면 rootfs = **tmpfs**, 아니면 ramfs |
| 3 | `console_on_rootfs` (`init/main.c:1510`) | `/dev/console` 을 열어 fd 0/1/2로. 실패하면 `Warning: unable to open an initial console.` |
| 4 | `init_eaccess(ramdisk_execute_command)` | 기본값 `"/init"` (`rdinit=` 로 변경). 없으면 `prepare_namespace()` = `root=` 마운트 경로 |
| 5 | `run_init_process("/init")` | `kernel_execve` — 성공하면 이 커널 스레드가 **유저 프로세스 PID 1** 이 된다 |
| 6 | 실패 시 | `init=` → `CONFIG_DEFAULT_INIT` → `/sbin/init`, `/etc/init`, `/bin/init`, `/bin/sh` → `No working init found` panic |

**우리 cpio에는 `/dev/console` 이 없는데 왜 A4가 PASS인가?** 랩 커널은 `CONFIG_INITRAMFS_SOURCE=""` 로 빌드됐고, 이때 `usr/default_cpio_list` 가 커널 안에 들어간다. 실제 내용:

```text
dir /dev 0755 0 0
nod /dev/console 0600 0 0 c 5 1
dir /root 0700 0 0
```

이 내장 cpio가 1번에서 먼저 풀리므로, 일반 사용자 권한으로 만든 cpio(장치 노드를 `mknod` 할 수 없음)라도 콘솔이 있다. 장치 노드를 직접 넣고 싶으면 커널 트리의 `usr/gen_init_cpio` 를 쓴다 — 리스트 파일로 cpio를 만들기 때문에 root 권한이 필요 없다. 실측 (uid 501로 실행):

```text
$ /var/lab/out/linux-build/usr/gen_init_cpio list.txt > g.cpio && cpio -itv < g.cpio
drwxr-xr-x   2 root     root            0 Nov 15 15:14 dev
crw-------   1 root     root       5,   1 Nov 15 15:14 dev/console
-rwxr-xr-x   1 root     root       856856 Nov 15 15:14 init
```

**devtmpfs는 왜 직접 마운트하나?** `CONFIG_DEVTMPFS_MOUNT=y` 인데도, 자동 마운트(`devtmpfs_mount()`)는 `prepare_namespace()` 안(`init/do_mounts.c:493`), 즉 **`root=` 경로에서만** 불린다. initramfs 경로에서는 PID 1이 해야 한다. 마운트 후 `/dev/ttyAMA0`, `/dev/null` 이 생긴 것을 스크립트 S8이 확인한다.

`mount_early()` 의 포인트는 세 개뿐이다: ① `mkdir` 먼저(`EEXIST` 무시), ② `MS_NOSUID` 정도의 플래그, ③ `EBUSY`(이미 마운트)는 성공. `fix_console()` 은 `fcntl(i, F_GETFD)` 로 0/1/2를 검사하고, 하나라도 `EBADF` 면 `/dev/console` 을 `O_RDWR|O_NOCTTY` 로 열어 `dup2` 한다. 순서가 중요하다: **devtmpfs 마운트 → 콘솔 수리 → 첫 printf**. 콘솔이 깨진 보드에서 printf부터 하면 그 출력은 그냥 사라진다 (실패 재현 ④).

### Part B — 명령줄은 세 갈래로 나뉜다

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">커널 명령줄 -&gt; 커널 / init envp / init argv / 버림 (practice 실측)</text>
<rect x="10" y="32" width="720" height="34" class="f-mut" stroke="currentColor"/>
<text x="20" y="54" fill="currentColor" font-size="12" font-family="monospace">console=ttyAMA0 quiet panic=-1 rdinit=/init B04_SCRIPT=/etc/practice.sh b04.dot=1 -- b04arg</text>
<rect x="10" y="95" width="170" height="80" class="f-acc" stroke="currentColor"/>
<text x="20" y="115" fill="currentColor">커널이 소비</text>
<text x="20" y="135" fill="currentColor" font-size="12" font-family="monospace">console= quiet</text>
<text x="20" y="152" fill="currentColor" font-size="12" font-family="monospace">panic= rdinit=</text>
<text x="20" y="168" fill="currentColor" font-size="12">(__setup / early_param)</text>
<rect x="195" y="95" width="175" height="80" class="f-ok" stroke="currentColor"/>
<text x="205" y="115" fill="currentColor">init envp</text>
<text x="205" y="135" fill="currentColor" font-size="12" font-family="monospace">HOME=/  TERM=linux</text>
<text x="205" y="152" fill="currentColor" font-size="12" font-family="monospace">B04_SCRIPT=...</text>
<text x="205" y="168" fill="currentColor" font-size="12">모르는 key=val</text>
<rect x="385" y="95" width="160" height="80" class="f-ok" stroke="currentColor"/>
<text x="395" y="115" fill="currentColor">init argv</text>
<text x="395" y="135" fill="currentColor" font-size="12" font-family="monospace">argv[0]=/init</text>
<text x="395" y="152" fill="currentColor" font-size="12" font-family="monospace">argv[1]=b04arg</text>
<text x="395" y="168" fill="currentColor" font-size="12">'--' 뒤 / '=' 없는 단어</text>
<rect x="560" y="95" width="170" height="80" class="f-warn" stroke="currentColor"/>
<text x="570" y="115" fill="currentColor">버림 (env 아님)</text>
<text x="570" y="135" fill="currentColor" font-size="12" font-family="monospace">b04.dot=1</text>
<text x="570" y="152" fill="currentColor" font-size="12">'.' = 모듈 파라미터</text>
<text x="570" y="168" fill="currentColor" font-size="12">/proc/cmdline 엔 남음</text>
<line x1="95" y1="66" x2="95" y2="95" stroke="currentColor"/>
<line x1="282" y1="66" x2="282" y2="95" stroke="currentColor"/>
<line x1="465" y1="66" x2="465" y2="95" stroke="currentColor"/>
<line x1="645" y1="66" x2="645" y2="95" stroke="currentColor"/>
<text x="10" y="205" fill="currentColor">unknown_bootoption(): '.' 있으면 return 0 (버림) / val 있으면 envp_init[] / 없으면 argv_init[]</text>
<text x="10" y="228" fill="currentColor">practice 검사 P4b: 커널이 준 env 는 정확히 3개 (HOME, TERM, B04_SCRIPT). PATH 는 없다.</text>
</svg>
```

근거는 `init/main.c:505` 의 `unknown_bootoption()` 이다. 커널이 모르는 `B04_SCRIPT=…` 는 부팅 로그에 이렇게 찍힌 뒤 init의 환경 변수가 된다 (실측):

```text
[    0.000000] Kernel command line: console=ttyAMA0 panic=-1 rdinit=/init B04_SCRIPT=/etc/selftest.sh -- b04arg
[    0.000000] Unknown kernel command line parameters "B04_SCRIPT=/etc/selftest.sh", will be passed to user space.
```

그럼 `getenv()` 만 쓰면 되지 왜 `/proc/cmdline` 을 파싱하나? ① 점이 들어간 key(`b04.dot=1`, `systemd.unit=…` 류)는 env로 안 온다(practice P4a 실측), ② `rdinit=`/`init=` 은 **그 앞에 있던 argv 단어를 지운다**(`rdinit_setup` 의 "auto" 주석), ③ initramfs 스크립트들이 `root=`, `rescue` 같은 커널이 *소비한* 인자도 읽어야 하기 때문이다. 그래서 실제 initramfs(`dracut`, BusyBox `/init` 스크립트)는 `for a in $(cat /proc/cmdline)` 패턴을 쓴다.

`cmdline_get` 의 토큰 규칙과 테스트:

| 테스트 | 입력 | 기대 | 지키는 규칙 |
|---|---|---|---|
| B1 | `console=ttyAMA0 B04_SCRIPT=/etc/s.sh quiet\n` | `/etc/s.sh` | 끝의 `\n` 도 공백 |
| B2 | `XB04_SCRIPT=a B04_SCRIPTX=b` | 0 (없음) | key 뒤는 `=` 또는 토큰 끝 |
| B3 | `ro quiet panic=-1` 에서 `quiet` | 1, `""` | 플래그 |
| B4 | `a=1 msg="hello world" b=2` | `hello world` | 따옴표 안 공백 유지, 따옴표 제거 |
| B5 | `root=/dev/vda root=/dev/vdb` | `/dev/vdb` | 마지막이 이김 (커널 `__setup` 도 두 번 불려 덮어씀) |
| B6 | n=4 | -1, `/et` | 넘치지 않고 NUL 종료 |

한계(일부러 안 함): 토큰 전체를 감싼 `"msg=hello world"` 형태는 커널 `next_arg()` 는 받아 주지만 이 함수는 key 비교에서 실패한다.

### Part C — 좀비와 고아: PID 1만 할 수 있는 일

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">make_orphan_pair(): 자식 exit(7) -&gt; 손자는 PID 1 에 입양 -&gt; exit(42)</text>
<line x1="120" y1="45" x2="120" y2="280" stroke="currentColor" stroke-dasharray="4 3"/>
<line x1="330" y1="45" x2="330" y2="280" stroke="currentColor" stroke-dasharray="4 3"/>
<line x1="560" y1="45" x2="560" y2="280" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="80" y="42" fill="currentColor">PID 1 (init)</text>
<text x="300" y="42" fill="currentColor">자식 c</text>
<text x="530" y="42" fill="currentColor">손자 g</text>
<rect x="60" y="55" width="120" height="26" class="f-mut" stroke="currentColor"/>
<text x="70" y="73" fill="currentColor" font-size="12">fork() -&gt; c</text>
<rect x="270" y="90" width="120" height="26" class="f-mut" stroke="currentColor"/>
<text x="280" y="108" fill="currentColor" font-size="12">fork() -&gt; g</text>
<rect x="270" y="125" width="120" height="26" class="f-warn" stroke="currentColor"/>
<text x="280" y="143" fill="currentColor" font-size="12">_exit(7) = Z</text>
<rect x="490" y="125" width="140" height="44" class="f-mut" stroke="currentColor"/>
<text x="500" y="143" fill="currentColor" font-size="12">getppid()==1 까지</text>
<text x="500" y="160" fill="currentColor" font-size="12">대기 (입양 확인)</text>
<rect x="30" y="160" width="190" height="44" class="f-acc" stroke="currentColor"/>
<text x="40" y="178" fill="currentColor" font-size="12">waitid(WNOWAIT): c 종료 확인</text>
<text x="40" y="195" fill="currentColor" font-size="12">C1: 좀비 수 &gt;= 1</text>
<rect x="490" y="185" width="140" height="26" class="f-warn" stroke="currentColor"/>
<text x="500" y="203" fill="currentColor" font-size="12">_exit(42) = Z</text>
<rect x="30" y="220" width="190" height="52" class="f-ok" stroke="currentColor"/>
<text x="40" y="238" fill="currentColor" font-size="12">reap_all(): waitpid(-1) x2</text>
<text x="40" y="254" fill="currentColor" font-size="12">C2: n=2, codes {7, 42}</text>
<text x="40" y="268" fill="currentColor" font-size="12">C3: 좀비 0, ECHILD</text>
<line x1="390" y1="138" x2="490" y2="147" stroke="currentColor"/>
<text x="400" y="128" fill="currentColor" font-size="12">reparent -&gt; 1</text>
<line x1="490" y1="198" x2="220" y2="245" stroke="currentColor"/>
<line x1="270" y1="138" x2="220" y2="182" stroke="currentColor"/>
</svg>
```

- **좀비(zombie)**: 끝났지만 부모가 `wait` 하지 않아 `task_struct` 와 종료 코드만 남은 프로세스. `/proc/<pid>/stat` 의 상태가 `Z`. 테스트 C1은 `waitid(…, WEXITED|WNOWAIT)` 로 "끝난 건 확인하되 거두지는 않은" 순간을 만들어 좀비가 실제로 있는 것을 센다 — 타이밍이 아니라 커널 상태로 판정한다.
- **고아(orphan)**: 부모가 먼저 죽은 프로세스. `exit_notify()` 가 PID 1(또는 subreaper)에게 붙인다. 손자는 `getppid()==1` 이 될 때까지 기다렸다가 끝나므로 순서가 결정적이다.
- 그래서 **PID 1의 `waitpid(-1)` 에는 자기가 fork 하지 않은 pid가 온다**. `spawn_wait()` 는 그것을 `orphans` 로 세고, 스크립트 pid가 올 때까지 계속 기다린다. 스크립트가 남긴 `( sleep 1; exit 5 ) &` 는 스크립트보다 늦게 끝나므로 이후 `reap_all()` 이 거둔다 (실측 `reaped 1 late orphan(s), first code 5`).

PID 1이 끝나면? `kernel/exit.c:941` — 마지막 스레드가 죽은 프로세스가 global init이면 `panic("Attempted to kill init! exitcode=0x%08x\n", …)`. starter의 `exitcode=0x00000100` 은 **wait status 인코딩** 이다: `main` 이 1을 반환 → `exit(1)` → `1 << 8 = 0x100`. `/bin/true` 를 init으로 주면 `0x00000000` 이 나온다 (실패 재현 ③). 정상 종료는 `reboot(RB_POWER_OFF)` 뿐이고, 그러면 커널이 `reboot: Power down` 을 찍고 QEMU가 끝난다.

### 5.4 cpio 만들기와 부팅 (check.sh가 하는 일)

| 단계 | 명령 (check.sh) | 왜 |
|---|---|---|
| 빌드 | `gcc -std=c11 -Wall -Wextra -Werror -O2 -static X.c -o init` | libc 없는 initramfs. 결과 856,856 B static ELF |
| rootfs | `bin/busybox` + `busybox --list` 의 402개 애플릿을 `bin/` 심볼릭 링크, `sbin -> bin`, `etc/*.sh`, `/init` | 셸·`grep`·`awk`·`sleep` 등을 BusyBox 하나로 |
| cpio | `find . \| cpio -o -H newc -R 0:0 --quiet \| gzip -9` | 커널은 **newc(070701)** 만 푼다. `-R 0:0` = 게스트에서 root 소유 |
| 부팅 | `qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic -no-reboot -kernel Image -initrd i.gz -append "console=ttyAMA0 quiet panic=-1 rdinit=/init B04_SCRIPT=/etc/selftest.sh -- b04arg"` | `panic=-1` + `-no-reboot` = panic 즉시 QEMU 종료 |
| 판정 | `PASS:` 27개, `FAIL:` 0, `INIT_DONE`, `reboot: Power down`, `Kernel panic` 없음 | 개수·문자열만, 시간은 안 봄 |

실측 숫자: cpio에 415개 항목, `i.gz` 1,560,239 B. 게스트에서 `/proc/device-tree/chosen` 을 읽어 보니 QEMU가 initrd를 `linux,initrd-start = 0x48000000`, `linux,initrd-end = 0x4817cf56` 에 놓았다 (그때 cpio 1,560,406 B = 0x17cf56 — 정확히 파일 크기). 커널은 풀고 나서 `Freeing initrd memory: 1520K` 로 그 RAM을 돌려준다. 풀린 rootfs(tmpfs)는 `/proc/meminfo` 의 `Shmem: 3044 kB` 로 보인다 — initramfs의 대가는 **RAM** 이다. 커널이 받는 압축 형식은 `.config` 의 `CONFIG_RD_GZIP/BZIP2/LZMA/XZ/LZO/LZ4/ZSTD=y` 로 정해진다.

solution 실측 (요약):

```text
B04 init: pid=1 mounts=3 console=ok argc=2
PASS: A1 running as PID 1
…
PASS: C3 no zombies, ECHILD
SCRIPT: pid=54 ppid=1 kernel=6.18.55 rootfs=rootfs
PASS: S1 parent is PID 1
…
SCRIPT: left orphan pid 63 behind, exiting 3
B04 init: /etc/selftest.sh exited 3, orphans reaped meanwhile 0
PASS: C4 script ran to the end (exit 3)
B04 init: reaped 1 late orphan(s), first code 5
PASS: C5 script's orphan (exit 5) reaped
PASS: C6 no zombies after script
19/19 passed
B04 init: power off
27/27 passed (FAIL lines: 0)
RESULT: PASS (solution)
```

check.sh 한 번에 3–5초(벽시계, TCG `-smp 1`). solution 3회 연속 PASS.

### 5.5 실패 4가지 재현 (실측, `check.sh demo <이름>`)

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B04/check.sh demo noinit
```

| demo | 만든 상황 | 핵심 커널 메시지 (6.18.55 실측) |
|---|---|---|
| `noinit` | cpio에 `/init` 없음 | `check access for rdinit=/init failed: -2, ignoring` → `VFS: Cannot open root device "" or unknown-block(0,0): error -6` → `Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0)` |
| `dyninit` | `/init` = 동적 ELF, 다른 파일 없음 | `Failed to execute /init (error -2)` → `/sbin/init`, `/etc/init`, `/bin/init`, `/bin/sh` 차례로 시도 → `Kernel panic - not syncing: No working init found.  Try passing init= option to kernel. See Linux Documentation/admin-guide/init.rst for guidance.` |
| `initexit` | `rdinit=/bin/true` | `Run /bin/true as init process` → `Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000000` (콜 스택 `do_group_exit` ← `__arm64_sys_exit_group` ← `el0_svc_common`) |
| `noconsole` | cpio의 `dev/console` = 깨진 심볼릭 링크 | `Warning: unable to open an initial console.` → solution은 `console=repaired` 로 살아남아 27개 전부 PASS |
| `noconsole_starter` | 같은 상황 + starter | 같은 Warning 뒤 **init 출력이 한 줄도 없이** `Attempted to kill init! exitcode=0x00000100` |

읽는 법:

- **noinit 과 dyninit 의 차이** 가 면접 포인트다. `/init` 이 *없으면* `init_eaccess` 단계에서 이미 버려지고 커널은 `root=` 로 진짜 디스크를 찾다가 VFS panic을 낸다. `/init` 이 *있는데 실행이 안 되면* (`-2` = `ENOENT`: 파일은 있는데 인터프리터 `/lib/ld-linux-aarch64.so.1` 이 없음) fallback 목록을 다 돌고 `No working init found` 다. "VFS panic이 났는데 initramfs를 줬다"면 → cpio가 안 풀렸거나 `/init` 경로가 틀렸다.
- dyninit 로그에는 평소엔 안 보이는 `with arguments: /sbin/init b04arg` / `with environment: HOME=/ TERM=linux B04_SCRIPT=/etc/selftest.sh` 줄도 찍혔다. `run_init_process()` 의 `pr_debug` 줄이다. `init/main.c` 13행에 `#define DEBUG` 가 있어 이 줄은 항상 KERN_DEBUG로 기록되고(정상 부팅이면 `dmesg` 로 보인다), 평소 콘솔 레벨에선 걸러진다. panic이 콘솔 레벨을 올린 뒤 아직 콘솔로 안 나간 기록이 함께 찍힌 것으로 보인다(추정). Part B의 envp/argv 규칙을 커널이 직접 보여 준 셈이다.
- noconsole: `init/initramfs.c` 의 `clean_path()` 는 같은 이름이 **다른 타입** 으로 오면 기존 항목을 지운다. 그래서 내장 cpio의 `/dev/console`(char 5:1)이 우리 cpio의 심볼릭 링크로 바뀌었고, `console_on_rootfs()` 의 `filp_open` 이 실패했다. 실제 보드에서는 "cpio를 만들 때 `/dev` 를 통째로 덮었다", "`console=` 이 없는 UART를 가리킨다" 같은 실수로 생긴다.
- cpio 형식 실수도 한 번 확인했다: `-H newc` 를 빼면(GNU cpio 기본 = 옛 binary 형식, 첫 바이트 `c7 71`) `Initramfs unpacking failed: no cpio magic` → `/init` 없음 → 위의 VFS panic.

### 5.6 대화형 셸 (수동 실행)

`B04_SCRIPT=` 를 빼면 `/init` 이 콘솔에서 `/bin/sh` 를 띄운다. Don이 직접 쳐 볼 명령 (QEMU 종료는 셸에서 `exit`, 또는 `Ctrl-a x`):

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B04/check.sh shell
```

expect로 같은 명령을 돌린 실측:

```text
B04 init: no B04_SCRIPT= -> interactive shell, 'exit' powers off
~ # ls -l /proc/self/fd/ /dev/tty /dev/console; cut -d' ' -f5-8 /proc/self/stat; ls -l /proc/1/fd
crw-------    1 0        0           5,   1 Jan  1  1970 /dev/console
crw-rw-rw-    1 0        0           5,   0 Nov 15 23:12 /dev/tty
…
56 54 52288 56
~ # sleep 30 &
~ # jobs
[1]+  Running                    sleep 30
~ # exit
16/16 passed
INIT_DONE
B04 init: power off
[   31.022514] reboot: Power down
```

- `/proc/self/stat` 5–8번 필드 = pgrp 56, session 54, **tty_nr 52288 = 204×256+64 = ttyAMA0**, tpgid 56. `spawn_wait(…, ctty=1)` 의 `setsid()` + `ioctl(0, TIOCSCTTY, 1)` 덕에 셸이 제어 터미널을 가져서 `jobs` 가 동작한다. `/dev/console` 을 그냥 열면 커널이 ctty로 잡아 주지 않는다 (`drivers/tty/tty_io.c` 의 `noctty = … || device == MKDEV(TTYAUX_MAJOR, 1)`). BusyBox의 `cttyhack` 애플릿이 같은 문제를 푼다.
- 마지막 줄 시간 **31.0 s** 를 보라. 셸을 `exit` 한 뒤 `reap_all()` 이 고아가 된 `sleep 30` 이 끝날 때까지 블로킹했다. 진짜 init은 끄기 전에 `kill(-1, SIGTERM)` 으로 남은 프로세스를 정리한다 → 추가 연습 P2.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B04/check.sh solution
# ... 27/27 passed (FAIL lines: 0)
# RESULT: PASS (solution)
```

## 7. 자주 하는 실수

**1. `/init` 스크립트·프로그램의 끝에서 그냥 끝난다.** 셸 `/init` 마지막 줄의 `exec /bin/sh` 를 빠뜨리거나, C의 `main` 이 `return` 한다 → `Attempted to kill init!`. `set -e` 를 쓴 셸 `/init` 은 명령 하나 실패로 끝나서 같은 panic이다. 고침: 마지막은 `exec switch_root …`, `exec /bin/sh`, 또는 `reboot(RB_POWER_OFF)` (`poweroff -f`).

**2. `/init` 을 동적으로 링크했다.** 호스트에선 잘 도는데 게스트에선 `Failed to execute /init (error -2)`. 파일은 있는데 `-2` 라서 헷갈린다 — 없는 것은 `PT_INTERP` 의 `/lib/ld-linux-aarch64.so.1` 이다. 고침: `gcc -static`, 확인은 `file init` 에 `statically linked`.

**3. `cpio -o` 에 `-H newc` 를 안 줬다.** `Initramfs unpacking failed: no cpio magic` 한 줄이 묻히고 결과는 VFS panic이다. 고침: `find . | cpio -o -H newc -R 0:0`. `find .` 는 반드시 rootfs **안에서** (`cd r && find .`) — 밖에서 하면 경로가 `r/init` 이 되어 `/init` 이 없다.

**4. 자식을 거두지 않는다 / WNOHANG 한 번만 돈다.** 좀비가 쌓이고 pid가 고갈된다. `waitpid(-1, &st, WNOHANG)` 를 한 번 부르면 그 순간 끝난 하나만 거둔다. 고침: PID 1은 `waitpid(-1)` 를 **루프** 로, `EINTR` 은 재시도, `ECHILD` 에서 멈춘다. 이벤트 루프가 있으면 SIGCHLD/`signalfd`(OS-M18)로 깨어나 WNOHANG 루프를 ECHILD까지 돈다.

**5. `spawn_wait` 가 `waitpid(pid)` 만 한다.** 내 스크립트만 기다리면 그동안 끝난 고아는 좀비로 남는다(PID 1이 아니면 문제 없지만 PID 1에서는 버그). 고침: `waitpid(-1)` 로 받고 pid가 다르면 고아로 센다.

**6. key를 `strncmp(tok, key, strlen(key))` 로만 비교.** `B04_SCRIPTX=evil` 도 맞는다. 고침: 다음 글자가 `=` 이거나 토큰 끝인지 확인 (B2 테스트).

**7. (P1) 시그널을 막아 둔 채 fork/exec.** 막힌 시그널 마스크는 `fork`·`execve` 를 지나 **상속** 된다. PID 1이 SIGTERM을 막은 채 스크립트를 띄우면 그 스크립트가 남긴 `sleep 1000` 도 SIGTERM이 막혀 있어 `kill(-1, SIGTERM)` 이 안 먹는다. 실측: practice.c에서 자식의 `sigprocmask(SIG_SETMASK, &g_oldmask, NULL)` 한 줄을 지우면 `P1 … PASS` 까지 가고 끄기 청소에서 영원히 기다려 `no INIT_DONE (qemu rc=124)` (timeout)로 끝났다. 고침: 자식에서 exec 전에 원래 마스크로 되돌린다.

## 8. 면접 Q&A

<details>
<summary>Q1. initramfs와 옛 initrd는 뭐가 다른가? 왜 쓰나?</summary>

initrd는 **블록 장치 이미지**(ext2 등)를 RAM 디스크로 붙이고 파일시스템 드라이버로 마운트한 뒤 `/linuxrc` → `pivot_root` 했다. initramfs는 **cpio(newc) 아카이브** 를 커널이 직접 tmpfs/ramfs(rootfs)에 풀 뿐이라 FS 드라이버도 블록 장치도 필요 없다. 쓰는 이유: 진짜 루트를 마운트하기 전에 필요한 일 — 스토리지/암호화 모듈 로드, dm-verity/dm-crypt 설정, A/B 슬롯 선택, 복구 모드 — 을 유저 공간에서 하려고. 작은 장비는 initramfs 자체가 최종 rootfs다(이 레슨처럼). 대가는 RAM(여기선 `Shmem` 3,044 kB)과 매 부팅 압축 해제 시간.

꼬리: *커널에 내장할 수도 있나?* `CONFIG_INITRAMFS_SOURCE` 에 디렉터리나 리스트 파일을 주면 `usr/gen_init_cpio` 로 만들어 vmlinux에 넣는다. 비워 두면 `/dev`, `/dev/console`, `/root` 만 있는 기본 cpio가 들어가고, 부트로더가 준 cpio는 그 위에 풀린다.
</details>

<details>
<summary>Q2. PID 1이 꼭 해야 하는 일은?</summary>

① `/proc`, `/sys`, `/dev`(devtmpfs) 마운트 — 커널은 initramfs 경로에서 devtmpfs를 자동 마운트하지 않는다. ② stdin/stdout/stderr 확보(`/dev/console`). ③ **고아 거두기** — 모든 고아가 PID 1에 붙으므로 `waitpid(-1)` 루프. ④ 서비스 시작·감시(respawn), ⑤ 종료 시 남은 프로세스에 SIGTERM→SIGKILL, `sync`, `reboot(2)`. ⑥ 절대 exit 하지 않기.

꼬리: *컨테이너에서 앱을 PID 1로 띄우면?* 같은 문제가 생긴다 — 좀비를 안 거두고, 핸들러 없는 SIGTERM이 무시되어 `docker stop` 이 10초 뒤 SIGKILL로 끝난다. 그래서 `tini`/`dumb-init` 같은 작은 init을 쓴다.
</details>

<details>
<summary>Q3. init이 죽으면 정확히 무슨 일이 일어나나? exitcode=0x00000100 은 무슨 뜻인가?</summary>

`do_exit()` 에서 스레드 그룹의 마지막 스레드가 죽고 그게 global init이면 `panic("Attempted to kill init! exitcode=0x%08x")` (`kernel/exit.c`). 숫자는 wait status 형식이라 `0x100` = `exit(1)`, `0x000` = `exit(0)`, 시그널로 죽으면 하위 7비트가 시그널 번호다. 제품에서는 `panic=N`(N초 뒤 리부트)이나 HW 워치독으로 회복한다. 이 레슨은 `panic=-1` + QEMU `-no-reboot` 으로 "즉시 리부트 = QEMU 종료"를 만들어 테스트가 멈추지 않게 했다.

꼬리: *정상적으로 끄려면?* `reboot(RB_POWER_OFF)` / `RB_AUTOBOOT`. BusyBox `poweroff`, `systemctl poweroff` 도 결국 이 시스템 콜이다.
</details>

<details>
<summary>Q4. 커널은 init을 어떻게 찾나? initramfs를 줬는데 VFS panic이 났다면?</summary>

initramfs를 푼 뒤 `rdinit=`(기본 `/init`)을 `init_eaccess` 로 확인한다. 있으면 실행, 실패하면 `init=` → `CONFIG_DEFAULT_INIT` → `/sbin/init` `/etc/init` `/bin/init` `/bin/sh` → `No working init found`. **`/init` 접근 자체가 실패하면** `prepare_namespace()` 로 가서 `root=` 디스크를 찾는다 → `root=` 가 없으면 `VFS: Unable to mount root fs on unknown-block(0,0)`. 그래서 VFS panic이면 ① cpio가 안 풀렸다(`no cpio magic`, 압축 형식 미지원 `RD_*`, 부트로더가 initrd 주소/크기를 잘못 넘김) ② `/init` 경로·권한(실행 비트)이 틀렸다 를 먼저 본다. `Unpacking initramfs...` 다음 줄을 확인하는 게 첫 단계다.

꼬리: *U-Boot에서는 initrd를 어떻게 넘기나?* `booti ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} ${fdt_addr_r}` — U-Boot가 DTB `/chosen` 의 `linux,initrd-start/-end` 를 채운다. QEMU `-initrd` 도 같은 속성을 쓴다(실측 0x48000000–0x4817cf56). 자세한 건 ELX-M12.
</details>

<details>
<summary>Q5. 커널 명령줄의 값은 init에 어떻게 전달되나?</summary>

커널이 아는 파라미터(`console=`, `root=`, `quiet` …)는 커널이 쓰고, 모르는 것은 `unknown_bootoption()` 이 나눈다: `key=val` → init의 **환경 변수**, `=` 없는 단어 → **argv**, 이름에 `.` 이 있으면 모듈 파라미터로 보고 **버린다**. `--` 뒤는 전부 argv. 기본 env는 `HOME=/`, `TERM=linux` 뿐이고 **PATH는 없다** — 그래서 init은 `execvp` 전에 PATH를 정하거나 절대 경로를 쓴다. 전부 `/proc/cmdline` 에는 원문 그대로 남는다.

꼬리: *왜 `rdinit=` 은 명령줄 끝 쪽에 쓰나?* `rdinit_setup()`/`init_setup()` 이 그 앞에 모인 argv 단어를 지운다(LILO의 `auto` 대응).
</details>

<details>
<summary>Q6. 셸에서 kill -9 1 하면 init이 죽나?</summary>

안 죽는다. `kernel/signal.c` 의 `sig_task_ignored()`: global init에는 SIGKILL/SIGSTOP이 아예 전달되지 않고(`sig_kernel_only`), `SIGNAL_UNKILLABLE` 프로세스는 **핸들러가 SIG_DFL인 시그널도 무시** 한다. practice S9에서 `kill -KILL 1; kill -STOP 1; kill -INT 1` 후 PID 1 상태가 `S` 인 것을 실측했다. 그래서 init이 시그널로 명령을 받으려면 **핸들러를 설치** 하거나 막아 두고 `sigwaitinfo` 로 받아야 한다 (BusyBox: USR1 halt, USR2 poweroff, TERM reboot — `init/init.c` 의 `halt_reboot_pwoff`).

꼬리: *커널이 강제로 보내는 SIGKILL(OOM 등)은?* 6.18의 `sig_task_ignored()` 는 global init에 대해 `force` 와 상관없이 KILL/STOP을 버리고, OOM killer도 `is_global_init()` 이면 후보에서 뺀다(`mm/oom_kill.c`). `force` 예외는 **컨테이너 안의 PID 1**(`SIGNAL_UNKILLABLE` 만 가진 namespace init) 용이다 — 부모 namespace에서는 죽일 수 있어야 하니까.
</details>

<details>
<summary>Q7. 콘솔이 안 나온다. 커널 로그는 보이는데 init 출력이 없다면?</summary>

`Warning: unable to open an initial console.` 가 있는지 본다. 있으면 `/dev/console` 노드가 없거나 다른 타입으로 덮였다(cpio에 `/dev` 를 잘못 넣음) → init의 fd 0/1/2가 닫힌 채 시작한다. init이 devtmpfs를 마운트하고 `/dev/console` 을 다시 열면 회복된다(demo noconsole). 없으면 `console=` 이 가리키는 UART가 맞는지(`ttyAMA0` vs `ttyS0`, `stdout-path`), `earlycon=pl011,0x9000000` 로 초기 로그를 보는지, init이 정적 바이너리인지(실행 실패면 위의 panic이 보여야 함)를 순서대로 본다.

꼬리: *셸에서 Ctrl-C가 안 먹고 "job control turned off" 가 나오면?* `/dev/console` 은 제어 터미널이 되지 않는다(`noctty`). `setsid()` + `TIOCSCTTY`, 또는 BusyBox `setsid cttyhack sh`.
</details>

<details>
<summary>Q8. initramfs 안의 BusyBox는 왜 static으로 빌드하나? 항상 맞나?</summary>

initramfs에 `ld-linux` 와 `libc.so` 가 없으면 동적 바이너리는 하나도 실행되지 않는다. static이면 파일 하나로 끝난다(2,242,744 B). 하지만 진짜 rootfs에 이미 glibc를 쓰는 앱이 많으면 동적 BusyBox가 이득이다(공유) — ELX-B12. static glibc의 함정: `getaddrinfo` 등 NSS 함수는 실행 중 `libnss_*.so` 를 `dlopen` 하므로 DNS가 조용히 실패할 수 있다(링크 경고로 나옴). musl static이 대안이다.

꼬리: *빌드 중 `SHA1_HWACCEL` 오류는?* x86 SHA-NI 전용 코드라 arm64에서 `sha1_process_block64_shaNI undeclared` — 끄면 된다(실측).
</details>

## 9. 추가 연습

practice.c는 solution.c에 아래 다섯 가지를 더한 것이고, 게스트 스크립트는 `practice.sh` (`B04_SCRIPT=/etc/practice.sh b04.dot=1 -- b04arg`)다.

**P1. 경쟁 없는 끄기 요청.** 스크립트가 끝나기 직전 `kill -USR2 1` 을 보낸다(BusyBox 약속: USR2 = poweroff). init이 스크립트를 띄우기 *전에* 시그널을 받을 준비를 해야 한다. 어떻게?

<details>
<summary>답</summary>

`sigprocmask(SIG_BLOCK, {USR2, TERM})` 로 미리 막아 둔다 → 스크립트가 언제 보내든 **pending** 으로 남는다 → 스크립트 종료 후 `sigwaitinfo()` 가 바로 돌려준다 (`got signal 12 from pid 54`, `si_pid` = 스크립트). 핸들러+플래그 방식은 "확인 후 `pause()`" 사이에 시그널이 오면 놓친다(OS-B07의 race). 그리고 막힌 마스크는 상속되므로 **자식에서 exec 전에 원래 마스크로 복원** (7번 실수).
</details>

**P2. 끄기 전 청소.** 스크립트가 `sleep 1000 &` 데몬을 남기고 끝난다. 블로킹 `reap_all()` 만 하면 영원히 기다린다. 해결은?

<details>
<summary>답</summary>

`kill(-1, SIGTERM)` — PID 1 자신과 커널 스레드를 뺀 모든 프로세스에 보낸다 → `reap_all()` 이 `143`(=128+15)을 거둔다 (실측 `shutdown sweep reaped 1: 143`). 진짜 init은 SIGTERM 후 잠깐 기다렸다가 `kill(-1, SIGKILL)` 을 한 번 더 한다(BusyBox `init/init.c` 의 `run_shutdown_and_kill_processes`: `kill(-1, SIGTERM)` → `sync()` → `sleep1()` → `kill(-1, SIGKILL)`). 이 레슨의 C5는 그래서 practice에서 "데몬이 아직 살아 있음"(`kill(-1, 0) == 0`)으로 바뀐다.
</details>

**P3. 지금 루트는 무엇인가?** `/proc/mounts` 에서 `/` 의 타입을 읽고, `statfs("/")` 의 `f_type` 이 무엇인지 확인하라.

<details>
<summary>답</summary>

`/proc/mounts` 에는 `rootfs` 로 보이지만, `root=` 가 없고 `CONFIG_TMPFS=y` 라 실체는 tmpfs다 → `f_type == TMPFS_MAGIC (0x01021994)` (`init/do_mounts.c` `init_rootfs()`). tmpfs라 크기 제한·swap 가능, ramfs면 제한 없이 RAM을 먹는다. 또 rootfs는 `pivot_root` 로 떼어낼 수 없어서 `switch_root` 는 "파일 삭제 → `mount --move` → `chroot` → `exec init`" 을 한다.
</details>

**P4. 커널이 준 것만 세기.** `b04.dot=1` 은 env에 없고 `/proc/cmdline` 에는 있는지, 커널이 준 환경 변수가 정확히 몇 개인지, `argv[0]` 이 무엇인지 확인하라.

<details>
<summary>답</summary>

env는 우리가 넣은 PATH를 빼면 `HOME=/`, `TERM=linux`, `B04_SCRIPT=/etc/practice.sh` 3개. 점이 든 key는 `unknown_bootoption()` 에서 `return 0` 으로 버려진다. `argv[0]` 은 `run_init_process()` 가 넣은 `"/init"` (rdinit 경로) — 실측 P4a/P4b/P4c PASS.
</details>

**P5. PID 1에게 kill -KILL / -STOP / -INT.** 스크립트에서 보내고 PID 1이 살아 있는지 `/proc/1/status` 의 `State:` 로 확인하라.

<details>
<summary>답</summary>

셋 다 무시된다: KILL/STOP은 global init에 전달 자체가 안 되고, INT는 SIG_DFL이라 `SIGNAL_UNKILLABLE` 때문에 버려진다. 실측 `after kill -KILL/-STOP/-INT 1: PID 1 state=S`. 반대로 P1의 USR2는 **막아 두고 기다렸기 때문에** 전달되었다 — "핸들러 또는 sigwait 없이는 init에 시그널이 안 간다".
</details>

### 한 번에 검증

<!-- code: practice.c -->

<!-- code: practice.sh -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B04/check.sh practice
# ... B04 init: got signal 12 from pid 54
# ... B04 init: shutdown sweep reaped 1: 143 -1 -1
# 36/36 passed (FAIL lines: 0)
# RESULT: PASS (practice)
```

## 10. 핵심 정리

- initramfs = **newc cpio(+gzip)**. 커널은 내장 cpio(`/dev/console` 포함)를 먼저 풀고 부트로더가 준 것을 덮어 푼다. rootfs는 `root=` 가 없으면 tmpfs.
- 커널은 `rdinit=`(기본 `/init`)을 PID 1로 실행한다. `/init` 이 없으면 `root=` 경로 → VFS panic, 있는데 실행 실패면 fallback 후 `No working init found`.
- `/init` 은 **static** 으로. 동적이면 `Failed to execute /init (error -2)` — 없는 건 ld-linux다.
- PID 1의 일: proc/sysfs/devtmpfs 마운트(initramfs에선 devtmpfs 자동 마운트 없음), fd 0/1/2 확보, `waitpid(-1)` 루프로 고아까지 거두기, 끄기 전 `kill(-1, SIGTERM)`.
- PID 1은 return/exit 금지 → `Attempted to kill init! exitcode=0x…`(wait status). 정상 종료는 `sync()` + `reboot(RB_POWER_OFF)`.
- 명령줄: 모르는 `key=val` → init env, 단어/`--` 뒤 → argv, 점 든 key → 버림. PATH는 없다. 원문은 `/proc/cmdline`.
- PID 1은 KILL/STOP을 못 받고 SIG_DFL 시그널은 무시된다 → 명령은 핸들러나 막아 둔 뒤 `sigwaitinfo`, 자식에겐 마스크 복원.
- 테스트는 `panic=-1` + `-no-reboot` 으로 "panic = QEMU 종료", 판정은 PASS 줄 수와 `reboot: Power down` 문자열.

## 참고자료

- Linux 6.18.55 소스 (`/var/lab/src/linux-6.18.55`): `init/main.c` (`kernel_init`, `kernel_init_freeable`, `console_on_rootfs`, `unknown_bootoption`, `rdinit_setup`), `init/initramfs.c` (`do_populate_rootfs`, `clean_path`), `init/do_mounts.c` (`init_rootfs`, `prepare_namespace`), `kernel/exit.c` (`Attempted to kill init!`), `kernel/signal.c` (`sig_task_ignored`), `usr/default_cpio_list`, `usr/gen_init_cpio.c`, `drivers/tty/tty_io.c`
- 커널 문서: [ramfs, rootfs and initramfs](https://docs.kernel.org/filesystems/ramfs-rootfs-initramfs.html), [Early userspace support](https://docs.kernel.org/driver-api/early-userspace/early_userspace_support.html), [Explaining the "No working init found." boot hang message](https://docs.kernel.org/admin-guide/init.html), [The kernel's command-line parameters](https://docs.kernel.org/admin-guide/kernel-parameters.html)
- BusyBox 1.37.0 소스 (`/var/lab/src/busybox-1.37.0`): `init/init.c` (`halt_reboot_pwoff`), `libbb/hash_md5_sha.c`, `shell/ash.c`
- 이 사이트의 배경 노트: EmbeddedLinux `C_boot/2026-10-10_C7_init_boot_time.md` (PID 1, switch_root), `K_rootfs_build/2026-10-17_K2_busybox.md` (BusyBox 빌드·static/dynamic)
- 관련 레슨: ELX-B01 (부트 단계 지도), ELX-B02 (PT_INTERP·sysroot), ELX-B12 (rootfs·inittab·switch_root 대상), ELX-M12 (U-Boot `booti`), ELX-A08 (부트 시간), OS-B01 (fork/exec/wait), OS-B02 (dup2), OS-B07 (시그널·race), OS-M18 (signalfd/pidfd)
- `man 2 reboot`, `man 2 waitpid`, `man 2 sigwaitinfo`, `man 1 cpio`, `man 2 ioctl_tty` (TIOCSCTTY)
