---
id: ELX-B12
time: 70분
updated: 2026-11-15
---
# ELX-B12 rootfs 구성: FHS, BusyBox, 동적 라이브러리

## 학습 목표

- 동적 링크 실행 파일 하나가 rootfs에 요구하는 파일을 **ELF만 읽어서** 전부 뽑는다: `PT_INTERP`(커널이 여는 로더) + `DT_NEEDED`의 전이 폐포(closure, 로더가 여는 라이브러리).
- glibc 로더의 탐색 순서(RPATH → RUNPATH → 기본 경로, `$ORIGIN`, RPATH 상속)를 코드로 옮기고, 실제 `ldd` 결과와 1:1로 맞춘다.
- `ldd` 를 rootfs 빌드에 쓰면 안 되는 이유(바이너리를 로더로 실행한다 → 크로스 타깃에서는 `not a dynamic executable`)를 실측으로 설명한다.
- 동적 BusyBox + glibc + FHS 디렉터리 + `/etc/inittab` + `/etc/fstab` 으로 rootfs를 손으로 만들고, `fakeroot` + `mkfs.ext4 -d` 로 root 소유 ext4 이미지를 sudo 없이 만들어 `root=/dev/vda` 로 부팅한다.
- 라이브러리나 로더가 빠졌을 때 커널 콘솔에 나오는 **두 가지 다른 panic** 을 읽고 원인을 바로 말한다.

## 1. 문제

보드 bring-up에서 "커널은 떴는데 `Run /sbin/init as init process` 다음에 panic"은 거의 항상 rootfs 문제다. 그래서 면접에서는 **"최소 rootfs에 무엇이 꼭 있어야 하나? 동적 바이너리라면 무엇을 더 넣나? 그걸 어떻게 찾나?"** 를 묻는다. 펌웨어로 치면 "링커 스크립트가 맞아도 벡터 테이블이 가리키는 주소에 코드가 없으면 리셋 직후 죽는다"와 같은 종류의 문제다. 이 레슨은 그 목록을 **도구로 계산**하고, 그 계산이 맞다는 것을 진짜 부팅으로 확인한다.

> 환경: 모든 실행은 Lima VM `elinux`(aarch64 Ubuntu 26.04, glibc 2.43) 안에서 했다. 게스트는 **QEMU TCG cortex-a57 `-smp 1` `-m 512M` 위의 Linux 6.18.55** (`/var/lab/out/Image`, defconfig: `VIRTIO_BLK=y`, `EXT4_FS=y`, `DEVTMPFS_MOUNT=y`). BusyBox 1.37.0은 `/var/lab/src` 사본에서 **동적 링크**로 직접 빌드했다. 콘솔의 타임스탬프는 TCG 에뮬레이션 시간이라 실제 보드 속도가 아니다.

K1·K2 노트(ELN `K_rootfs_build/`)에서 정적 BusyBox initramfs와 "로더를 손으로 하나씩 넣기"를 이미 해 봤다. 이번에는 그 손작업을 **코드로 바꾼다** — Buildroot·Yocto가 rootfs를 만들 때 하는 일의 핵심이다.

```svg
<svg viewBox="0 0 760 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">부팅 → init: 누가 어떤 파일을 여는가 (이 레슨의 rootfs)</text>
<rect x="10" y="35" width="170" height="44" class="f-mut" stroke="currentColor"/>
<text x="20" y="54" fill="currentColor">커널: root=/dev/vda</text>
<text x="20" y="71" fill="currentColor" font-size="12">ext4 마운트, devtmpfs</text>
<rect x="210" y="35" width="170" height="44" class="f-acc" stroke="currentColor"/>
<text x="220" y="54" fill="currentColor">exec /sbin/init</text>
<text x="220" y="71" fill="currentColor" font-size="12">→ ../bin/busybox 링크</text>
<rect x="410" y="35" width="170" height="44" class="f-acc" stroke="currentColor"/>
<text x="420" y="54" fill="currentColor">PT_INTERP 열기</text>
<text x="420" y="71" fill="currentColor" font-size="12">ld-linux-aarch64.so.1</text>
<rect x="600" y="35" width="150" height="44" class="f-acc" stroke="currentColor"/>
<text x="610" y="54" fill="currentColor">로더: DT_NEEDED</text>
<text x="610" y="71" fill="currentColor" font-size="12">libm, libresolv, libc</text>
<line x1="180" y1="57" x2="210" y2="57" stroke="currentColor"/>
<line x1="380" y1="57" x2="410" y2="57" stroke="currentColor"/>
<line x1="580" y1="57" x2="600" y2="57" stroke="currentColor"/>
<rect x="410" y="95" width="170" height="44" class="f-warn" stroke="currentColor"/>
<text x="420" y="113" fill="currentColor" font-size="12">로더 없음: execve -ENOENT</text>
<text x="420" y="130" fill="currentColor" font-size="12">→ No working init found</text>
<rect x="600" y="95" width="150" height="44" class="f-warn" stroke="currentColor"/>
<text x="610" y="113" fill="currentColor" font-size="12">lib 없음: exit 127</text>
<text x="610" y="130" fill="currentColor" font-size="12">→ exitcode=0x00007f00</text>
<line x1="495" y1="79" x2="495" y2="95" stroke="currentColor" stroke-dasharray="4 3"/>
<line x1="675" y1="79" x2="675" y2="95" stroke="currentColor" stroke-dasharray="4 3"/>
<rect x="10" y="165" width="170" height="44" class="f-ok" stroke="currentColor"/>
<text x="20" y="184" fill="currentColor">BusyBox init (PID 1)</text>
<text x="20" y="201" fill="currentColor" font-size="12">/etc/inittab 읽기</text>
<rect x="210" y="165" width="170" height="44" class="f-ok" stroke="currentColor"/>
<text x="220" y="184" fill="currentColor">::sysinit (기다림)</text>
<text x="220" y="201" fill="currentColor" font-size="12">echo, /etc/init.d/rcS</text>
<rect x="410" y="165" width="170" height="44" class="f-ok" stroke="currentColor"/>
<text x="420" y="184" fill="currentColor">rcS: proc → mount -a</text>
<text x="420" y="201" fill="currentColor" font-size="12">/etc/fstab 나머지 4개</text>
<rect x="600" y="165" width="150" height="44" class="f-ok" stroke="currentColor"/>
<text x="610" y="184" fill="currentColor">::once 테스트</text>
<text x="610" y="201" fill="currentColor" font-size="12">끝에 poweroff</text>
<line x1="180" y1="187" x2="210" y2="187" stroke="currentColor"/>
<line x1="380" y1="187" x2="410" y2="187" stroke="currentColor"/>
<line x1="580" y1="187" x2="600" y2="187" stroke="currentColor"/>
<line x1="675" y1="139" x2="675" y2="150" stroke="currentColor"/>
<line x1="675" y1="150" x2="95" y2="150" stroke="currentColor"/>
<line x1="95" y1="150" x2="95" y2="165" stroke="currentColor"/>
<text x="230" y="146" fill="currentColor" font-size="12">전부 찾으면 busybox main()</text>
<rect x="10" y="235" width="740" height="110" class="f-mut" stroke="currentColor"/>
<text x="20" y="256" fill="currentColor" font-weight="bold">rootfs.ext4 (32 MiB, fakeroot + mkfs.ext4 -d, 전부 0:0 소유)</text>
<text x="20" y="278" fill="currentColor" font-family="monospace" font-size="12">/bin/busybox 1,182,288 B + 402개 applet 링크   /sbin/init → ../bin/busybox</text>
<text x="20" y="298" fill="currentColor" font-family="monospace" font-size="12">/lib/ld-linux-aarch64.so.1 201,872   /lib/aarch64-linux-gnu/libc.so.6 1,788,240</text>
<text x="20" y="318" fill="currentColor" font-family="monospace" font-size="12">libm.so.6 723,144   libresolv.so.2 68,000   /usr/lib/sensor/libsensor.so.1 69,544</text>
<text x="20" y="338" fill="currentColor" font-family="monospace" font-size="12">/etc: inittab fstab init.d/rcS hostname passwd group   /dev: console(5,1) null(1,3)</text>
</svg>
```

### Part A — ELF에서 의존성 읽기

`libclosure` 의 아래쪽 절반을 만든다. 헤더와 프로그램 헤더 파서(`read_elf`)는 주어진다.

- A1. `parse_dynamic(e, data, phdrs)` — `PT_DYNAMIC` 을 `(d_tag, d_val)` 배열로 읽고 `DT_NEEDED`, `DT_SONAME`, `DT_RPATH`, `DT_RUNPATH` 문자열을 채운다. 문자열 테이블 `DT_STRTAB` 은 **가상 주소** 이므로 `vaddr_to_offset()` 으로 파일 오프셋으로 바꿔야 한다. ELF64(`<qQ`, 16 B)와 ELF32(`<iI`, 8 B) 둘 다.
- 검증: 픽스처(직접 빌드한 `app`, `libfoo`, armhf `hello`, 정적 바이너리, ELF가 아닌 파일, 잘린 파일)와 VM의 진짜 `qemu-system-aarch64`(46개 라이브러리), `mkfs.ext4`, `gpioinfo` 의 결과를 **실제 `ldd` 와 이름→경로 단위로** 비교한다.

### Part B — 로더처럼 찾기: 탐색 순서와 경계 사례

- B1. `search_dirs(chain, default_dirs)` — glibc 로더의 순서를 옮긴다: 요구한 객체에 RUNPATH가 **없을 때만** 로드 사슬(요구자 → 그것을 로드시킨 객체 → … → 실행 파일)의 RPATH, 그다음 요구자의 RUNPATH, 그다음 기본 경로. `$ORIGIN` 은 그 항목을 가진 객체의 디렉터리.
- B2. `closure(bins, roots, default_dirs)` — BFS. 이미 로드된 이름/SONAME은 건너뛴다(순환 의존 OK), 인터프리터의 SONAME은 처음부터 로드된 것으로 친다, class/machine이 다른 후보(x86-64 `.so`)는 건너뛰고 다음 디렉터리를 본다, 못 찾은 이름은 `missing`.
- 경계 사례(전부 실제 `ldd` 와 비교): 정적 바이너리, 없는 라이브러리, 순환 의존, 앞 디렉터리의 잘못된 아키텍처 라이브러리, **RPATH는 상속되고 RUNPATH는 안 되는** 실행 파일 한 쌍, armhf 크로스 바이너리(`ldd` 는 실패 → `qemu-arm` 로 로더 `--list` 와 비교).

### Part C — rootfs를 만들고 부팅해서 증명하기

- C1. `stage(res, root, roots)` — closure의 인터프리터와 라이브러리를 `root + 타깃 경로` 로 복사한다. 심볼릭 링크는 따라가서 **내용**을 복사한다.
- `check.sh` 가 나머지를 한다: 동적 BusyBox 빌드 → FHS 뼈대 + `/etc` → 보드 팀의 `sensor_app` 패키지(라이브러리는 staging에, RUNPATH `$ORIGIN/../lib/sensor`) → **여러분의 `stage`** 로 closure 배치 → `fakeroot` 안에서 `chown 0:0` + `mknod` + `mkfs.ext4 -d` → QEMU `root=/dev/vda`.
- 게스트가 증명할 것: PID 1이 BusyBox init, inittab의 sysinit이 rcS를 실행, `/proc` + fstab의 마운트 4개(`/sys /tmp /run /dev/pts`), 게스트 안의 `ld.so --list /bin/busybox` 에 `not found` 없음, `sensor_app` 실행, 파일 소유자 0:0, poweroff 때 `::shutdown` 액션 실행.

## 2. Problem (English)

> You are bringing up a board whose rootfs you assemble by hand. Write `libclosure`, a tool that computes which files a set of dynamically-linked binaries needs at run time **without executing them** (no `ldd`), then copies those files into a staging rootfs at their on-target paths.
>
> ```python
> def parse_dynamic(e: Elf, data: bytes, phdrs: list) -> None    # fill e.needed / soname / rpath / runpath
> def search_dirs(chain: list, default_dirs: list) -> list        # glibc order for one DT_NEEDED lookup
> def closure(bins: list, roots: list, default_dirs=DEFAULT_DIRS) -> dict
>     # {"interps": [...], "libs": {needed_name: target_path}, "missing": [...], "parent": {needed_name: requester}}
> def stage(res: dict, root: str, roots: list) -> list           # sorted target paths actually copied
> ```
>
> `roots` is an ordered list of host directories (e.g. `[rootfs, staging, sysroot]`); a target path `p` resolves to the first existing `root + p`. Support ELF32/ELF64 little-endian, `$ORIGIN`, DT_RPATH vs DT_RUNPATH precedence (RPATH ignored when RUNPATH exists; RPATH of the loading chain inherited; RUNPATH not inherited), cycles, missing libraries, wrong-class/machine candidates (skip and keep searching). Raise `ValueError` for non-ELF or truncated input.
>
> Example (`/bin/busybox` + `/usr/bin/sensor_app`):
> ```text
>   [interp] /lib/ld-linux-aarch64.so.1
>   libm.so.6 => /lib/aarch64-linux-gnu/libm.so.6   (needed by /bin/busybox)
>   libresolv.so.2 => /lib/aarch64-linux-gnu/libresolv.so.2   (needed by /bin/busybox)
>   libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6   (needed by /bin/busybox)
>   libsensor.so.1 => /usr/lib/sensor/libsensor.so.1   (needed by /usr/bin/sensor_app)
> ```
>
> Then build an ext4 rootfs (dynamic BusyBox, FHS dirs, `/etc/inittab`, `/etc/fstab`), boot it on QEMU `virt` with `root=/dev/vda`, and show that sysinit and the fstab mounts ran. Follow-ups: why is `ldd` the wrong tool for a cross rootfs? What does the kernel print when the interpreter vs a library is missing?

## 3. 시작 코드

파일: `starter.py`(TODO 4개 + 테스트 하네스), `check.sh`(VM에서 전체 흐름), 게스트 쪽 `b12-test.sh`, `/etc` 파일(`inittab`, `fstab`, `rcS`, `hostname`), Part C 패키지 `libsensor.c` + `sensor_app.c`. Python 3 표준 라이브러리만 쓴다(VM은 3.14). 테스트는 gcc로 픽스처를 만들고 **VM의 실제 `ldd`** 와 비교하므로 VM에서 돈다.

```bash
limactl shell elinux -- python3 -I /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B12/starter.py test
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B12/check.sh starter
```

첫 줄은 단위 테스트만(`7/40 passed`, exit 1), 둘째 줄은 전체 흐름이다. starter의 `closure` 가 빈 결과를 돌려주므로 로더가 rootfs에 들어가지 않고, 게스트는 §5 C3의 `No working init found` 로 끝난다(`9/49 passed`, exit 1, 약 2초).

<!-- code: starter.py -->

<!-- code: check.sh -->

<!-- code: b12-test.sh -->

<!-- code: sensor_app.c -->

`/etc` 파일 세 개(레슨 폴더의 `inittab`, `fstab`, `rcS` 그대로):

```text
# ELX-B12 /etc/inittab (BusyBox init)   <tty>::<action>:<process>   (runlevel field is ignored)
::sysinit:/bin/echo B12: inittab sysinit 1 of 2
::sysinit:/etc/init.d/rcS
::once:/etc/b12-test.sh
::ctrlaltdel:/sbin/reboot
::shutdown:/bin/echo B12: inittab shutdown action
::shutdown:/bin/umount -a -r

# ELX-B12 /etc/fstab   <dev> <mountpoint> <type> <options> <dump> <pass>
/dev/root  /         ext4    rw,noatime              0 1
proc       /proc     proc    defaults                0 0
sysfs      /sys      sysfs   defaults                0 0
tmpfs      /tmp      tmpfs   mode=1777,size=16m      0 0
tmpfs      /run      tmpfs   mode=0755,nosuid,nodev  0 0
devpts     /dev/pts  devpts  gid=5,mode=620          0 0

#!/bin/sh
# ELX-B12 /etc/init.d/rcS: run by inittab ::sysinit (init waits for it)
echo "B12: rcS start, pid $$"
mount -t proc proc /proc     # first: BusyBox mount -a reads /proc/mounts to skip what is already mounted
mkdir -p /dev/pts
mount -a
echo "B12: rcS mount -a rc=$?"
hostname -F /etc/hostname
touch /run/b12-rcS.done
```

## 4. 힌트

<details><summary>힌트 1 — DT_STRTAB 은 왜 그냥 오프셋이 아닌가</summary>

동적 섹션은 **로더가 메모리에 올린 뒤** 읽으라고 만든 것이라 주소가 전부 가상 주소다. 파일에서 읽으려면 그 주소를 덮는 `PT_LOAD` 를 찾아 `p_offset + (vaddr - p_vaddr)` 로 바꾼다(`vaddr_to_offset` 이 이미 있다). `DT_NEEDED` 의 `d_val` 은 그 문자열 테이블 안의 **오프셋**이다. 문자열은 NUL에서 끝난다.
</details>

<details><summary>힌트 2 — 동적 엔트리 한 개의 크기</summary>

ELF64는 `Elf64_Dyn { Elf64_Sxword d_tag; Elf64_Xword d_val; }` = 16바이트(`"<qQ"`), ELF32는 8바이트(`"<iI"`). `DT_NULL`(0)에서 멈춘다. `PT_DYNAMIC` 이 없으면 정적 바이너리이므로 그냥 돌아간다.
</details>

<details><summary>힌트 3 — closure의 큐에 무엇을 넣나</summary>

`(타깃 경로, Elf)` 쌍을 넣고, 각 객체마다 "누가 나를 처음 로드시켰나" 사슬(`chain_of[target]`)을 기억한다. RPATH 상속이 이 사슬을 따라간다. 방문 표시는 **NEEDED 이름**과 **SONAME** 둘 다로 한다 — glibc도 이미 로드된 객체를 이름이나 SONAME으로 찾는다. 인터프리터의 SONAME(`ld-linux-aarch64.so.1`)을 처음에 넣어 두면 libc와 busybox가 요구하는 `ld-linux-aarch64.so.1` 이 따로 잡히지 않는다.
</details>

<details><summary>힌트 4 — 후보가 있는데도 건너뛰어야 할 때</summary>

`roots + 디렉터리 + 이름` 이 존재해도, 그 파일의 `(bits, machine)` 이 실행 파일과 다르면 로더는 무시하고 다음 디렉터리를 본다. 그래서 x86-64 `libbar.so.1` 이 RUNPATH 앞쪽에 있어도 aarch64 것이 선택된다.
</details>

<details><summary>힌트 5 — stage 에서 symlink</summary>

VM의 `/lib/ld-linux-aarch64.so.1` 은 `aarch64-linux-gnu/ld-linux-aarch64.so.1` 을 가리키는 **상대 심볼릭 링크**다. 링크 자체를 복사하면 rootfs에는 그 대상이 없을 수 있다. `shutil.copy2(src, dst)` 는 기본으로 링크를 따라가 내용을 복사한다.
</details>

## 5. 풀이

### A1 — 동적 섹션 걷기

`PT_DYNAMIC` 세그먼트 = `(d_tag, d_val)` 의 배열. 이 레슨이 쓰는 태그는 다섯 개다.

| 태그 | 값 | `d_val` 의 뜻 | 누가 쓰나 |
|---|---:|---|---|
| `DT_NEEDED` | 1 | strtab 안의 오프셋 → `"libc.so.6"` | 로더: 이 이름을 찾아 로드 |
| `DT_STRTAB` | 5 | 문자열 테이블의 **가상 주소** | 나머지 태그의 기준 |
| `DT_SONAME` | 14 | 이 라이브러리의 "공식 이름" | 링커가 NEEDED에 적는 이름 |
| `DT_RPATH` | 15 | `':'` 로 나뉜 디렉터리 목록 | 로더 (옛 방식, 상속됨) |
| `DT_RUNPATH` | 29 | 같은 형식 | 로더 (새 방식, 상속 안 됨) |

실제 BusyBox를 `readelf` 로 보면 이렇다(VM, 이 레슨이 빌드한 것):

```text
bin/busybox: ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, ...
-rwxr-xr-x 1 donh donh 1182288 Nov 15 15:13 bin/busybox
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libresolv.so.2]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-aarch64.so.1]
```

마지막 줄을 보자. BusyBox는 **로더 자신을 NEEDED로** 가진다. 그런데 `ldd` 출력에는 `ld-linux-aarch64.so.1 => …` 줄이 없고 맨 끝에 `/lib/ld-linux-aarch64.so.1 (0x…)` 만 있다. 로더는 실행 시작 때 이미 메모리에 있고, 자기 SONAME으로 "이미 로드됨"이 되기 때문이다. 그래서 `closure` 는 인터프리터의 SONAME을 미리 `loaded` 에 넣는다. 안 넣으면 같은 파일이 `/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1` 로 한 번 더 잡힌다.

또 하나: 픽스처 `libfoo.so.1` 은 `libc.so.6` 을 NEEDED로 갖지 **않는다**(`foo()` 가 libc 함수를 안 쓴다). Ubuntu gcc는 링커에 `--as-needed` 를 기본으로 넘기기 때문이다(`gcc -dumpspecs` 에 `as-needed` 가 있다). 처음 쓴 테스트가 `["libbar.so.1", "libc.so.6"]` 을 기대했다가 FAIL이 났고, 그래서 기대값을 실제 ELF에 맞췄다. **NEEDED는 소스가 아니라 링크 옵션이 정한다.**

### B1 — 탐색 순서: RPATH, RUNPATH, `$ORIGIN`

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">NEEDED 한 개를 찾는 순서 (glibc 로더 → search_dirs)</text>
<rect x="10" y="35" width="230" height="40" class="f-mut" stroke="currentColor"/>
<text x="20" y="60" fill="currentColor">요구자에 RUNPATH 있나?</text>
<rect x="290" y="35" width="460" height="40" class="f-acc" stroke="currentColor"/>
<text x="300" y="52" fill="currentColor">없음: RPATH 를 사슬 따라 — 요구자 → 로드시킨 객체 → … → 실행 파일</text>
<text x="300" y="69" fill="currentColor" font-size="12">(RUNPATH 있는 객체의 RPATH 는 무시됨)</text>
<line x1="240" y1="55" x2="290" y2="55" stroke="currentColor"/>
<rect x="290" y="90" width="460" height="34" class="f-mut" stroke="currentColor"/>
<text x="300" y="112" fill="currentColor">LD_LIBRARY_PATH — rootfs 빌드 도구는 쓰지 않음</text>
<rect x="290" y="139" width="460" height="34" class="f-acc" stroke="currentColor"/>
<text x="300" y="161" fill="currentColor">요구자 자신의 RUNPATH 만 (상속 안 됨)</text>
<rect x="290" y="188" width="460" height="34" class="f-mut" stroke="currentColor"/>
<text x="300" y="210" fill="currentColor">/etc/ld.so.cache — 손 rootfs 에는 없음, 건너뜀</text>
<rect x="290" y="237" width="460" height="50" class="f-ok" stroke="currentColor"/>
<text x="300" y="256" fill="currentColor" font-family="monospace" font-size="12">/lib/aarch64-linux-gnu  /usr/lib/aarch64-linux-gnu</text>
<text x="300" y="276" fill="currentColor" font-family="monospace" font-size="12">/lib  /usr/lib   (ld.so --help 의 system search path)</text>
<line x1="520" y1="75" x2="520" y2="90" stroke="currentColor"/>
<line x1="520" y1="124" x2="520" y2="139" stroke="currentColor"/>
<line x1="520" y1="173" x2="520" y2="188" stroke="currentColor"/>
<line x1="520" y1="222" x2="520" y2="237" stroke="currentColor"/>
<rect x="10" y="139" width="250" height="148" class="f-warn" stroke="currentColor"/>
<text x="20" y="160" fill="currentColor">각 후보 파일마다:</text>
<text x="20" y="182" fill="currentColor" font-size="12">roots 앞에서부터 root+경로 존재?</text>
<text x="20" y="202" fill="currentColor" font-size="12">(bits, machine) == 실행 파일?</text>
<text x="20" y="222" fill="currentColor" font-size="12">다르면 건너뛰고 다음 후보</text>
<text x="20" y="246" fill="currentColor" font-size="12">$ORIGIN = 그 RPATH/RUNPATH 를</text>
<text x="20" y="264" fill="currentColor" font-size="12">가진 객체의 디렉터리(타깃 경로)</text>
</svg>
```

```python
dirs = []
req_target, req = chain[0]
if not req.runpath:
    for target, obj in chain:
        if not obj.runpath:
            dirs += [expand_origin(d, target) for d in obj.rpath if d]
dirs += [expand_origin(d, req_target) for d in req.runpath if d]
dirs += default_dirs
```

출처를 밝혀 둔다. 기본 경로 4개는 VM에서 `ld-linux-aarch64.so.1 --help` 로 직접 봤다. 순서 규칙(RPATH 사슬 → `LD_LIBRARY_PATH` → 요구자의 RUNPATH → `ld.so.cache` → 기본 경로, RUNPATH가 있으면 RPATH 무시)은 glibc `elf/dl-load.c` 의 `_dl_map_object()` 를 **기억으로** 옮긴 것이다(glibc 소스는 랩에 없다). 그래서 이 규칙을 테스트가 실제 `ldd` 로 확인하게 만들었다. 픽스처 한 쌍은 같은 소스를 `-Wl,--disable-new-dtags`(→ `DT_RPATH`)와 `--enable-new-dtags`(→ `DT_RUNPATH`)로만 다르게 링크했다:

| 픽스처 | 실행 파일의 태그 | `libp.so.1` → `libq.so.1` (libp는 RPATH 없음) | 실제 `ldd` | 우리 closure |
|---|---|---|---|---|
| `app_rpath` | `RPATH $ORIGIN/../lib` | 실행 파일의 RPATH가 **상속**되어 찾음 | `libq.so.1 => …/opt/rp/lib/libq.so.1` | 같음 (PASS) |
| `app_runpath` | `RUNPATH $ORIGIN/../lib` | RUNPATH는 libp에 상속 안 됨 → 기본 경로에 없음 | `libq.so.1 => not found` | `missing == ["libq.so.1"]` (PASS) |

실무 결론: **RUNPATH(현재 기본값)를 쓰면 라이브러리의 라이브러리에도 각자 RUNPATH(`$ORIGIN`)를 줘야 한다.** 픽스처 `libfoo.so.1` 이 `RUNPATH $ORIGIN` 을 가진 이유다.

### B2 — closure와 경계 사례: 실제 `ldd` 와의 비교 결과

`python3 -I solution.py test` 의 실측(`40/40 passed`, 픽스처 빌드 포함 1초 안쪽). 비교는 "이름 → `realpath`" 단위다. `ldd` 는 `ld.so.cache` 덕분에 `/usr/lib/aarch64-linux-gnu/…` 를, 우리는 기본 경로 첫 번째인 `/lib/aarch64-linux-gnu/…` 를 말하지만, VM은 usrmerge(`/lib → usr/lib`)라 같은 파일이다.

| 대상 | 기대 | 결과 |
|---|---|---|
| VM `qemu-system-aarch64` | 46개 이름→경로가 `ldd` 와 같음 | PASS |
| VM `mkfs.ext4`, `gpioinfo` | 6개, 2개 | PASS |
| `app` (`libfoo` → `libbar`) | 3개, `parent[libbar] == libfoo` | PASS |
| `app_static` (`gcc -static`) | `PT_INTERP`/`PT_DYNAMIC` 없음 → 빈 closure. `ldd`: `not a dynamic executable` | PASS |
| `app_missing` | `libgone.so.1 => not found` 와 `missing` 일치 | PASS |
| `app_cyc` (`libcyca` ↔ `libcycb`) | 무한 루프 없이 3개 | PASS |
| `app_wrong` (RUNPATH 앞에 x86-64 `libbar`) | 건너뛰고 `/usr/lib/foo/libbar.so.1` | PASS |
| armhf `hello` (ELF32, machine 40) | roots `[…, /usr/arm-linux-gnueabihf]` → `libc.so.6 => /lib/libc.so.6` | PASS |

마지막 행이 **`ldd` 를 쓰면 안 되는 이유**다. VM에서 직접 돌린 결과:

```text
$ ldd ./arm            # arm-linux-gnueabihf-gcc 로 만든 hello
	not a dynamic executable
$ qemu-arm -L /usr/arm-linux-gnueabihf /usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3 --list ./arm
	linux-vdso.so.1 (0x40801000)
	libc.so.6 => /lib/libc.so.6 (0x40840000)
	/lib/ld-linux-armhf.so.3 => /usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3 (0x00400000)
```

`/usr/bin/ldd` 는 셸 스크립트다. `RTLDLIST=/lib/ld-linux-aarch64.so.1` 의 로더로 `--verify` 를 돌리고, 통과하면 `LD_TRACE_LOADED_OBJECTS=1` 로 **그 로더가 바이너리를 실제로 로드**하게 해서 결과를 찍는다. aarch64 로더는 ARM32 파일을 거부하므로 `not a dynamic executable` 이 된다. 같은 질문을 armhf 로더에게 qemu-user로 물으면 답이 나오고, 그 답이 우리 closure와 같다. `ldd(1)` 맨 페이지의 Security 절도 "인터프리터가 ld-linux가 아니면 ldd가 프로그램을 직접 실행할 수 있다"고 경고한다(VM의 `/usr/share/man/man1/ldd.1.gz` 에서 확인). 그리고 `ldd` 는 **지금 이 호스트, 이 위치** 기준의 답이다. `sensor_app` 을 빌드 디렉터리에서 `ldd` 하면 `libsensor.so.1 => not found` 가 나왔다 — RUNPATH `$ORIGIN/../lib/sensor` 가 타깃 배치 기준이기 때문이다.

### C1 — stage, 그리고 rootfs 조립

`stage` 는 closure의 `interps` + `libs` 값(타깃 경로)을 정렬해서 `find(t, roots)` 로 원본을 찾고 `root + t` 로 `copy2` 한다. `check.sh` 의 실제 호출과 출력:

```text
python3 -I solution.py stage $R --from $ST --from / /bin/busybox /usr/bin/sensor_app
  [interp] /lib/ld-linux-aarch64.so.1
  libm.so.6 => /lib/aarch64-linux-gnu/libm.so.6   (needed by /bin/busybox)
  libresolv.so.2 => /lib/aarch64-linux-gnu/libresolv.so.2   (needed by /bin/busybox)
  libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6   (needed by /bin/busybox)
  libsensor.so.1 => /usr/lib/sensor/libsensor.so.1   (needed by /usr/bin/sensor_app)
  staged /lib/aarch64-linux-gnu/libc.so.6
  staged /lib/aarch64-linux-gnu/libm.so.6
  staged /lib/aarch64-linux-gnu/libresolv.so.2
  staged /lib/ld-linux-aarch64.so.1
  staged /usr/lib/sensor/libsensor.so.1
```

`roots = [rootfs, staging, /]` 의 세 층은 Buildroot의 `target/`, `staging/`, 툴체인 sysroot와 같은 역할이다. `sensor_app` 은 이미 rootfs에 있고(그래서 `find` 가 rootfs에서 찾는다), `libsensor.so.1` 은 staging에서, glibc는 VM(= 이 경우 sysroot)에서 왔다. `sensor_app` 이 요구한 `libc.so.6` 은 busybox가 이미 끌어왔으므로 한 번만 복사된다.

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">closure (BFS) — 이 레슨 rootfs 의 실제 결과, 크기는 바이트</text>
<rect x="10" y="40" width="200" height="44" class="f-acc" stroke="currentColor"/>
<text x="20" y="59" fill="currentColor">/bin/busybox</text>
<text x="20" y="76" fill="currentColor" font-size="12">1,182,288 (rootfs)</text>
<rect x="10" y="150" width="200" height="44" class="f-acc" stroke="currentColor"/>
<text x="20" y="169" fill="currentColor">/usr/bin/sensor_app</text>
<text x="20" y="186" fill="currentColor" font-size="12">RUNPATH $ORIGIN/../lib/sensor</text>
<rect x="290" y="30" width="230" height="30" class="f-ok" stroke="currentColor"/>
<text x="300" y="50" fill="currentColor" font-size="12">libm.so.6  723,144 (sysroot)</text>
<rect x="290" y="68" width="230" height="30" class="f-ok" stroke="currentColor"/>
<text x="300" y="88" fill="currentColor" font-size="12">libresolv.so.2  68,000 (sysroot)</text>
<rect x="290" y="106" width="230" height="30" class="f-ok" stroke="currentColor"/>
<text x="300" y="126" fill="currentColor" font-size="12">libc.so.6  1,788,240 (sysroot)</text>
<rect x="290" y="166" width="230" height="30" class="f-ok" stroke="currentColor"/>
<text x="300" y="186" fill="currentColor" font-size="12">libsensor.so.1  69,544 (staging)</text>
<rect x="560" y="68" width="190" height="44" class="f-mut" stroke="currentColor"/>
<text x="570" y="87" fill="currentColor" font-size="12">ld-linux-aarch64.so.1</text>
<text x="570" y="104" fill="currentColor" font-size="12">201,872 = PT_INTERP</text>
<line x1="210" y1="55" x2="290" y2="45" stroke="currentColor"/>
<line x1="210" y1="62" x2="290" y2="83" stroke="currentColor"/>
<line x1="210" y1="70" x2="290" y2="121" stroke="currentColor"/>
<line x1="210" y1="172" x2="290" y2="181" stroke="currentColor"/>
<line x1="210" y1="165" x2="290" y2="128" stroke="currentColor" stroke-dasharray="4 3"/>
<line x1="520" y1="121" x2="560" y2="100" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="10" y="222" fill="currentColor" font-size="12">점선 = 이미 로드된 이름이라 건너뜀: sensor_app→libc, libc→ld-linux, busybox→ld-linux (인터프리터 SONAME 미리 등록)</text>
<text x="10" y="242" fill="currentColor" font-size="12">게스트 du -sk: /bin 1140 KiB, /lib 2688 KiB, /usr 48 KiB — 동적 busybox + glibc 4개 ≈ 3.9 MB</text>
</svg>
```

조립의 나머지(`check.sh` 4~6단계):

1. **FHS 뼈대**: `/bin /sbin /usr/bin /usr/sbin`(BusyBox `make install` 이 만든 402개 링크), `/lib`, `/dev /proc /sys /run /tmp`(빈 마운트 지점, `/tmp` 는 1777), `/etc /etc/init.d`, `/root /mnt /var/log`. 가상 파일시스템 디렉터리는 **이미지에는 비어 있고** 부팅 때 마운트된다.
2. **BusyBox 동적 빌드**: `/var/lab/src/busybox-1.37.0` 을 `/var/tmp/ELX-B12_cache/src` 로 복사 → `defconfig`(기본이 동적, `# CONFIG_STATIC is not set`) → `TC`, `SHA1_HWACCEL`, `SHA256_HWACCEL` 끔(랩 빌드 스크립트와 같은 이유) → `make -j3 EXTRA_CFLAGS=-std=gnu11` → `make … CONFIG_PREFIX=… install`. 6 vCPU가 한가할 때 9초 걸렸다. **install에도 같은 `EXTRA_CFLAGS` 를 준다**(K2 §3.4: 다르면 `install` 이 다시 링크한다). 한 번 빌드하면 재사용한다.
3. **ext4 이미지, sudo 없이**: `fakeroot -- sh -c "chown -R 0:0 R && mknod R/dev/console c 5 1 && mknod R/dev/null c 1 3 && mkfs.ext4 -q -F -L b12root -d R rootfs.ext4"`. `fakeroot` 안에서는 `chown`/`mknod` 가 "성공한 것처럼" 기록되고, 같은 세션의 `mkfs.ext4 -d` 가 `stat()` 으로 그 가짜 값을 읽어 이미지에 적는다. 결과를 `debugfs` 로 확인:

```text
$ debugfs -R "ls -l /dev" rootfs.ext4
    108   20600 (3)      0      0       0 15-Nov-2026 15:16 console
    109   20666 (3)      0      0       0 15-Nov-2026 15:16 null
$ debugfs -R "stat /bin/busybox" rootfs.ext4 | grep User
User:     0   Group:     0   Project:     0   Size: 1182288
```

`fakeroot` 를 빼면 소유자가 빌드한 사람(VM의 uid 501)이 된다 — K1 §7.4에서 `501 k1user` 로 실제로 본 문제다.

### C2 — 부팅: 커널은 root를 붙이고 `/sbin/init` 을 찾는다

`check.sh demo ok` 의 실제 콘솔(Linux 6.18.55, QEMU TCG cortex-a57 -smp 1, 앞부분 생략):

```text
[    0.700058] check access for rdinit=/init failed: -2, ignoring
[    0.741546] EXT4-fs (vda): mounted filesystem d8b43972-b1bf-4608-92e5-7847ca795ed1 r/w with ordered data mode. Quota mode: none.
[    0.741812] VFS: Mounted root (ext4 filesystem) on device 254:0.
[    0.742818] devtmpfs: mounted
[    0.781685] Freeing unused kernel memory: 3264K
[    0.782165] Run /sbin/init as init process
B12: inittab sysinit 1 of 2
B12: rcS start, pid 55
B12: rcS mount -a rc=0
PASS: guest PID 1 is BusyBox init (/proc/1/exe)
…
PASS: guest files are root-owned (fakeroot)
LIST: 	 (0x0000ffff83df0000)
LIST: 	linux-vdso.so.1 (0x0000ffff83f79000)
LIST: 	libm.so.6 => /lib/aarch64-linux-gnu/libm.so.6 (0x0000ffff83d20000)
LIST: 	libresolv.so.2 => /lib/aarch64-linux-gnu/libresolv.so.2 (0x0000ffff83cf0000)
LIST: 	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x0000ffff83b20000)
LIST: 	/lib/ld-linux-aarch64.so.1 (0x0000ffff83f30000)
MOUNTS: /dev/root / ext4;devtmpfs /dev devtmpfs;proc /proc proc;sysfs /sys sysfs;tmpfs /tmp tmpfs;tmpfs /run tmpfs;devpts /dev/pts devpts;
GUEST_DONE
B12: inittab shutdown action
[    1.410920] EXT4-fs (vda): re-mounted d8b43972-b1bf-4608-92e5-7847ca795ed1 ro.
The system is going down NOW!
Sent SIGTERM to all processes
Sent SIGKILL to all processes
Requesting system poweroff
[    3.422885] reboot: Power down
```

커널 소스(`/var/lab/src/linux-6.18.55`)로 읽는 순서:

| 콘솔 줄 | 커널 코드 | 뜻 |
|---|---|---|
| `check access for rdinit=/init failed: -2` | `init/main.c` `kernel_init_freeable()` | 내장 initramfs에 `/init` 이 없다 → `prepare_namespace()` 로 진짜 root를 붙이러 간다 |
| `VFS: Mounted root (ext4 …) on device 254:0` | `init/do_mounts.c` `do_mount_root()` | `root=/dev/vda` 를 ext4로 마운트(`rw` 인자 때문에 r/w) |
| `devtmpfs: mounted` | `prepare_namespace()` 끝의 `devtmpfs_mount()` | `DEVTMPFS_MOUNT=y` → 이미지의 빈 `/dev` 위에 devtmpfs |
| `Run /sbin/init as init process` | `kernel_init()` → `try_to_run_init_process("/sbin/init")` | `init=` 이 없으면 `/sbin/init`, `/etc/init`, `/bin/init`, `/bin/sh` 순 |

`/dev/console` 은 조금 특별하다. 커널은 root를 붙이기 **전에** `console_on_rootfs()` 로 메모리 rootfs의 `/dev/console` 을 열어 fd 0/1/2로 쓴다. 그래서 이미지에 넣은 `console(5,1)` 노드는 이 부팅에선 devtmpfs에 가려 실제로 쓰이지 않는다. `devtmpfs.mount=0` 이거나 `DEVTMPFS` 가 없는 커널일 때의 보험이다(K1 §7.5에서 `devtmpfs.mount=0` 이면 `/dev/null` 이 일반 파일로 만들어지는 사고를 봤다).

`LIST:` 첫 줄의 이름 없는 `(0x…)` 는 VM에서 `ld.so --list busybox` 를 해도 똑같이 나왔고, `/usr/bin/ls` 에서는 나오지 않았다. BusyBox가 로더를 NEEDED로 직접 가진 것과 관련 있어 보이지만 원인은 확인하지 않았다(`ldd` 출력에는 없다).

### C3 — 일부러 빼고 부팅: panic 두 종류

`check.sh demo nolib`(staging 뒤 `libm.so.6` 삭제)와 `demo nointerp`(로더 삭제)의 실제 출력:

```text
# demo nolib
[    0.770358] Run /sbin/init as init process
/sbin/init: error while loading shared libraries: libm.so.6: cannot open shared object file: No such file or directory
[    0.813585] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00007f00

# demo nointerp  (starter 도 같은 결과)
[    0.614433] Run /sbin/init as init process
[    0.629115] Run /etc/init as init process
[    0.629979] Run /bin/init as init process
[    0.630036] Run /bin/sh as init process
[    0.630696] Kernel panic - not syncing: No working init found.  Try passing init= option to kernel. See Linux Documentation/admin-guide/init.rst for guidance.
```

| | 라이브러리 없음 | 로더(PT_INTERP) 없음 |
|---|---|---|
| 실패하는 곳 | **유저 공간** 로더가 이미 실행 중 | **커널** `fs/binfmt_elf.c` 가 `PT_INTERP` 문자열을 `open_exec()` → `-ENOENT` |
| 메시지 | 로더가 직접 `error while loading shared libraries: libm.so.6` | 없음. `execve` 가 `-ENOENT` 라 "파일이 없다"와 구별 불가 |
| 커널 반응 | init이 `exit(127)` → `kernel/exit.c` 의 `Attempted to kill init! exitcode=0x00007f00` (0x7f = 127) | `try_to_run_init_process()` 는 `-ENOENT` 면 경고 없이 다음 후보로. 넷 다 실패 → `No working init found` |
| 읽는 법 | `0x7f00` 이면 "로더까지는 됐다, 라이브러리 문제" | `/sbin/init` 은 있는데 `Run /etc/init …` 로 넘어가면 "인터프리터 경로 확인" |

`panic=-1` + QEMU `-no-reboot` 이라 panic 즉시 QEMU가 끝난다. 그래서 starter의 `check.sh` 도 매달리지 않고 약 2초에 FAIL로 끝난다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B12/check.sh solution
```

실측: `62/62 passed (python 40/40, guest 13/13, boot 9/9; qemu exit 0)`, `RESULT: PASS (solution)`, 연속 3회 모두 PASS, 회당 4~5초(BusyBox 캐시 있음. 첫 실행은 BusyBox 빌드 포함 약 1분). `check.sh demo ok|nolib|nointerp` 로 콘솔을 직접 볼 수 있고, `KEEP=1` 을 앞에 붙이면 `rootfs/`, `rootfs.ext4`, `console.log` 가 남는다. 이번 실행의 로그 사본: `log_check_solution.txt`, `log_demo_ok.txt`, `log_demo_nolib.txt`, `log_demo_nointerp.txt`, `log_mount_a_before_proc.txt`.

## 7. 자주 하는 실수

**① `/proc` 없이 `mount -a` — 이 레슨을 쓰다 실제로 당했다.** 처음 rcS는 `mkdir -p /dev/pts; mount -a` 였고 결과는 이랬다(`log_mount_a_before_proc.txt`):

```text
B12: rcS start, pid 55
[    1.843973] /dev/root: Can't lookup blockdev
mount: mounting /dev/root on / failed: No such file or directory
B12: rcS mount -a rc=1
```

BusyBox `mount -a`(`util-linux/mount.c`)는 fstab 항목마다 `find_mount_point()` 로 "이미 마운트돼 있나"를 보고 건너뛴다. 그런데 그 정보를 읽는 파일이 `bb_path_mtab_file` 이고, 이 빌드(`# CONFIG_FEATURE_MTAB_SUPPORT is not set`)에서는 `include/libbb.h` 의 정의상 **`/proc/mounts`** 다. fstab 첫 줄 `/` 를 처리할 때 `/proc` 이 아직 없으니 "아무것도 안 마운트됨" → `/dev/root` 를 마운트하려다 실패(`/dev/root` 는 `/proc/mounts` 에만 보이는 이름이고 노드가 없다). 나머지 줄은 마운트됐지만 `rc=1` 이 남는다. 고침: rcS에서 `mount -t proc proc /proc` 을 먼저(위 rcS), 또는 fstab에서 `/` 줄을 빼거나 `proc` 을 첫 줄로. K1의 rcS가 무사했던 것도 `proc` 을 먼저 마운트했기 때문이다.

**② 로더를 symlink째 복사.** `cp -a /lib/ld-linux-aarch64.so.1 R/lib/` 는 `aarch64-linux-gnu/ld-linux-aarch64.so.1` 을 가리키는 링크만 복사한다. 그 대상이 rootfs에 없으면 C3의 `No working init found` 와 똑같다. 고침: 링크를 따라 내용 복사(`cp -L`, `shutil.copy2`), 또는 대상까지 같이 넣기.

**③ 호스트 `ldd` 로 타깃 목록 만들기.** 크로스 바이너리는 `not a dynamic executable`(§5 B2). 같은 아키텍처라도 호스트의 `ld.so.cache`·`LD_LIBRARY_PATH`·설치 위치 기준의 답이다. 고침: `readelf -d`/`-l` 또는 이 레슨의 closure를 sysroot 기준으로.

**④ RUNPATH가 상속된다고 가정.** 실행 파일에만 `-Wl,-rpath,'$ORIGIN/../lib'` 를 주고 라이브러리끼리의 의존은 잊는다 → 보드에서 `libq.so.1: cannot open shared object file`. 고침: 각 라이브러리에도 `-Wl,-rpath,'$ORIGIN'` 을 주거나, 기본 경로(`/usr/lib`)에 설치한다.

**⑤ `PT_INTERP` 경로를 "비슷한 곳"에 두기.** 커널은 `PT_INTERP` 문자열 그대로(`/lib/ld-linux-aarch64.so.1`)를 연다. 검색 경로가 없다. `/lib/aarch64-linux-gnu/` 에만 두면 ⑤번은 ②번과 같은 panic이다.

**⑥ fakeroot 세션을 나누기.** `fakeroot chown -R 0:0 R` 과 `fakeroot mkfs.ext4 -d R …` 를 따로 실행하면 첫 세션의 가짜 소유권 기록이 사라져 이미지에 uid 501이 들어간다. 고침: 한 `fakeroot -- sh -c "…"` 안에서(또는 `-s/-i` 로 상태 파일 저장).

## 8. 면접 Q&A

<details><summary>Q1. 최소 rootfs에 "반드시" 있어야 하는 것은?</summary>

커널 쪽 요구는 실행할 수 있는 init 하나다(`/sbin/init` 등 또는 `init=`). 동적 init이면 여기에 **`PT_INTERP` 경로의 로더 + NEEDED closure** 가 붙는다. 그 다음부터는 init이 요구하는 것: BusyBox init이면 `/etc/inittab`(없으면 기본값: sysinit `/etc/init.d/rcS`, tty들에 askfirst 셸 등), 마운트 지점 `/proc /sys /dev /tmp /run`, 콘솔. `/dev/console` 은 커널이 root 마운트 전에 연 것을 물려받으므로 devtmpfs가 있으면 이미지에 없어도 된다.

꼬리: "rootfs 크기를 줄이려면?" → 정적 vs 동적 판단(프로그램 1개면 정적이 작다: 랩의 정적 busybox 2,242,744 B vs 이 레슨의 동적 busybox 1,182,288 + glibc 4개 2,781,256 B), musl, strip, 필요한 applet만, closure로 실제 필요한 `.so` 만.
</details>

<details><summary>Q2. 파일이 분명히 있는데 "No such file or directory" 가 나온다. 왜?</summary>

ELF의 `PT_INTERP` 가 가리키는 로더가 없다. 커널 `fs/binfmt_elf.c` 가 그 경로를 `open_exec()` 하다 `-ENOENT` 를 받고, `execve` 는 그 errno를 그대로 돌려준다. 셸은 "명령이 없다"고 말한다. 확인: `readelf -l prog | grep interpreter`.

꼬리: "PID 1이면?" → `try_to_run_init_process()` 는 `-ENOENT` 면 아무 말 없이 다음 후보로 가므로 `No working init found` 만 남는다(§5 C3). `init=/sbin/init` 을 명시하면 대신 `Requested init /sbin/init failed (error -2).` panic이다(`init/main.c` 의 문자열, 이 레슨에서는 실행 안 함).
</details>

<details><summary>Q3. ldd를 rootfs 빌드 스크립트에 쓰면 왜 안 되나?</summary>

`ldd` 는 호스트 로더에게 `LD_TRACE_LOADED_OBJECTS=1` 로 그 바이너리를 **로드시키는** 스크립트다. (1) 호스트 로더가 다른 아키텍처를 거부 → `not a dynamic executable`, (2) 결과가 호스트의 `ld.so.cache`·`LD_LIBRARY_PATH`·파일 위치에 의존, (3) 신뢰할 수 없는 바이너리에 위험(맨 페이지 Security 절). 대안: `readelf -d`, `objdump -p`, 이 레슨처럼 ELF를 읽는 도구(`lddtree` 류), 또는 타깃 로더를 qemu-user로 `--list`.

꼬리: "그래도 같은 아키텍처 개발 PC에서 빠르게 보고 싶다면?" → 타깃 sysroot의 로더를 `--library-path` 와 함께 직접 실행하는 방법이 있지만, 그래도 바이너리를 로드한다는 점은 같다.
</details>

<details><summary>Q4. RPATH와 RUNPATH의 차이는?</summary>

둘 다 바이너리에 박힌 라이브러리 탐색 경로다. RPATH는 `LD_LIBRARY_PATH` **보다 먼저** 보고, 로드 사슬을 따라 **상속**된다(실행 파일의 RPATH가 그 라이브러리들의 의존성 탐색에도 쓰인다). RUNPATH는 `LD_LIBRARY_PATH` **다음**에 보고, 그 객체 자신의 NEEDED에만 쓰인다. 객체에 RUNPATH가 있으면 그 객체의 RPATH는 무시된다. 지금 GNU ld 기본(`--enable-new-dtags`)은 RUNPATH다. 이 레슨에서 `app_rpath`/`app_runpath` 한 쌍으로 실제 `ldd` 가 그렇게 동작함을 확인했다.

꼬리: "`$ORIGIN` 은 왜 쓰나?" → 설치 위치에 상관없이 "나와 같이 배포된 라이브러리"를 찾게 한다. 빌드 PC 절대 경로(`/home/…/build/lib`)가 RPATH에 새는 것은 흔한 버그다(추가 연습 P2). 보안 관련: setuid 프로그램에서는 로더가 `$ORIGIN` 과 `LD_LIBRARY_PATH` 를 제한한다 — glibc secure mode 동작을 기억으로 말한 것이고 이 레슨에서는 실행해 보지 않았다.
</details>

<details><summary>Q5. 커널은 root=/dev/vda 를 받아서 init까지 무엇을 하나?</summary>

`kernel_init_freeable()`: `console_on_rootfs()` 로 메모리 rootfs의 `/dev/console` 을 fd 0/1/2로 → `rdinit` 대상(`/init`) 확인, 없으면(`check access for rdinit=/init failed: -2`) `prepare_namespace()`: 장치 probe를 기다리고(`rootwait` 면 장치가 나타날 때까지) → `mount_root()`(→ `VFS: Mounted root (ext4 filesystem)`) → `devtmpfs_mount()` → `MS_MOVE` 로 `/` 교체 → `chroot`. 그다음 `kernel_init()` 이 `/sbin/init` 부터 실행을 시도한다. 소스는 `init/main.c`, `init/do_mounts.c`.

꼬리: "`rootwait` 는 언제 필요?" → USB·SD처럼 늦게 나타나는 장치. virtio-blk는 built-in이고 probe가 끝난 뒤라 이 레슨은 없이 됐다. 없는데 장치가 늦으면 root 마운트 실패 panic이 난다(`VFS: Unable to mount root fs on …` — 기억으로 쓴 문구, 이 레슨에서 재현 안 함).
</details>

<details><summary>Q6. BusyBox inittab의 sysinit / wait / once / respawn / shutdown 차이는?</summary>

sysinit → wait → once 순서로 실행하고, sysinit/wait는 끝날 때까지 기다린다(`init/init.c` `run_actions()` 가 이 액션들만 `waitfor()`). once는 안 기다린다. respawn은 죽으면 다시 띄운다. shutdown은 poweroff/reboot 직전에 줄 순서대로 실행한다. 이 레슨의 콘솔에서 `B12: inittab sysinit 1 of 2` → rcS → `::once` 테스트 → `poweroff` → `B12: inittab shutdown action` → `umount -a -r`(`EXT4-fs (vda): re-mounted … ro`) → `The system is going down NOW!` → `Requesting system poweroff` 순서를 그대로 봤다. 첫 칸(id)은 BusyBox에서 tty 이름이고 runlevel 칸은 무시된다.

꼬리: "inittab이 없으면?" → `parse_inittab()` 이 기본 동작(rcS + 콘솔·tty2~4 askfirst 셸 등)을 넣는다. QEMU virt에는 tty2~4가 없어 에러가 반복될 수 있다(K2 §5.1).
</details>

<details><summary>Q7. sudo 없이 root 소유 ext4 이미지를 만드는 방법은? 장치 노드는?</summary>

`mkfs.ext4 -d <dir>` 은 디렉터리를 복사하면서 파일시스템을 만들고 `stat()` 결과(소유자, 모드, 장치 번호)를 그대로 적는다. `fakeroot` 는 `LD_PRELOAD` 로 `chown`/`mknod`/`stat` 을 가로채 "가짜 root" 상태를 기억하므로, 같은 세션 안에서 `chown -R 0:0`, `mknod console c 5 1` 을 한 뒤 `mkfs.ext4 -d` 를 돌리면 이미지에 0:0과 문자 장치 `(5,1)` 이 들어간다(§5 C1의 `debugfs` 출력). Buildroot는 같은 원리로 `makedevs` 장치 테이블 + fakeroot를 쓴다.

꼬리: "그럼 `/dev` 는 이미지에 다 넣어야 하나?" → 보통은 devtmpfs(커널이 노드 생성) + mdev/udev(권한·이름). 정적 노드는 `console`, `null` 정도만 보험으로.
</details>

<details><summary>Q8. 보드에서 "-sh: ./sensor_app: not found" 가 나왔다. 어떻게 좁히나?</summary>

1) `ls -l` 로 파일 존재·실행 비트 확인. 2) `readelf -l` 의 interpreter 경로가 보드에 있는지 — 없으면 Q2(가장 흔함: musl/glibc 혼동, armhf vs aarch64 로더 이름 차이). 3) 있으면 보드에서 `/lib/ld-linux-*.so.* --list ./sensor_app` 으로 `not found` 라이브러리를 찾는다(이 레슨 게스트의 `ld.so --list` 처럼). 4) RUNPATH/`$ORIGIN` 이 실제 배치와 맞는지(`readelf -d`). 5) 아키텍처 확인(`file`): 다른 아키텍처의 ELF면 커널이 `-ENOEXEC` 를 돌려주고 셸 메시지는 셸마다 다르다(이 레슨에서 실행해 보지 않음).

꼬리: "빌드 단계에서 미리 막으려면?" → 이 레슨의 closure를 이미지 빌드 끝에 돌려 `missing` 이 비어 있지 않으면 빌드 실패로(`check.sh` 의 `stage` 종료 코드 검사가 바로 그것).
</details>

## 9. 추가 연습

`practice.py` = `solution.py` + 아래 네 함수 + 테스트 9개.

**P1. `why(res, name)`** — "이 라이브러리는 누가 끌고 왔나". `parent` 를 거꾸로 따라 `[이름, 요구자, 요구자의 요구자, …, 실행 파일]` 을 돌려준다. 순환(`libcyca` ↔ `libcycb`)에서도 끝나야 한다.

<details><summary>P1 답</summary>

`parent[name]` 은 요구자의 **타깃 경로**이므로 경로 → 이름 역사전(`{t: n for n, t in res["libs"].items()}`)으로 다시 이름을 얻어 반복한다. 실행 파일은 `libs` 에 없으므로 거기서 멈춘다. BFS의 첫 요구자만 기록하므로 순환이 있어도 사슬은 실행 파일로 끝난다. 실측: `why(res, "libcycb.so.1") == ["libcycb.so.1", "/usr/lib/cyc/libcyca.so.1", "/usr/bin/app_cyc"]`. Yocto의 "이 패키지는 왜 이미지에 들어왔나"(`bitbake -g`) 질문의 축소판이다.
</details>

**P2. `rpath_lint(elf, target, roots)`** — RPATH/RUNPATH 항목 중 문제 있는 것을 찾는다: 빈 항목(= 현재 디렉터리), `$ORIGIN` 없는 상대 경로, 빌드 PC 경로 유출, 타깃에 없는 디렉터리.

<details><summary>P2 답</summary>

`-Wl,-rpath,'/var/tmp/build/lib::lib:$ORIGIN:$ORIGIN/nope'` 로 만든 라이브러리에 대한 실측:

```text
[('/var/tmp/build/lib', 'RPATH: build-host path leaked'), ('', 'RPATH: empty entry = current directory'),
 ('lib', 'RPATH: relative path without $ORIGIN'), ('$ORIGIN/nope', 'RPATH: directory not in target')]
```

다섯 항목 중 `$ORIGIN` 만 깨끗하다. 빈 항목과 상대 경로는 "프로그램을 실행한 현재 디렉터리" 기준이라 공격자가 같은 이름의 `.so` 를 심을 수 있는 보안 문제다.
</details>

**P3. `size_budget(res, roots)`** — 스테이징될 파일을 크기순으로, 합계와 함께.

<details><summary>P3 답</summary>

`os.path.getsize(find(t, roots))` 는 링크를 따라간 실제 파일 크기다. `app` + `app_cyc` closure의 실측: 6개, 합계 2,267,712 B, 1위 `libc.so.6` 1,788,240 B. 로더 201,872 B를 빼면 픽스처 라이브러리 4개가 각각 약 69 KB다. 코드 몇 줄짜리가 이렇게 큰 이유는 세그먼트 정렬이다: `libsensor.so` 를 `readelf -lW` 로 보면 두 번째 `LOAD` 가 파일 오프셋 `0x00fe20` 에서 시작하고 `Align 0x10000`(64 KiB)이다. 이 aarch64 툴체인이 64 KiB 페이지 커널도 지원하려고 RW 세그먼트를 64 KiB 경계 근처로 미는 것이다. 작은 `.so` 가 많은 rootfs에서는 이 패딩이 눈에 띄는 비용이다.
</details>

**P4. `soname_links(libdir)`** — `ldconfig -n` 처럼 SONAME과 파일 이름이 다른 라이브러리마다 `SONAME → 파일` 심볼릭 링크를 만든다. 결과를 **실제 `ldconfig -n -N`** 과 비교한다.

<details><summary>P4 답</summary>

파일마다 `read_elf(p).soname` 을 읽고, `libbar.so.1.2.3`(SONAME `libbar.so.1`)이면 `libbar.so.1 -> libbar.so.1.2.3` 을 만든다. 이미 맞는 링크면 건너뛰므로 두 번째 실행은 아무것도 안 만든다. 같은 입력을 가진 다른 디렉터리에 VM의 `/usr/sbin/ldconfig -n -N` 을 돌린 결과와 링크 집합이 같았다(non-root로 실행 가능). `stage` 는 내용만 복사하므로 rootfs에 개발용 `libfoo.so` 링크나 버전 파일명이 필요하면 이 단계를 붙인다.
</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B12/check.sh practice
```

실측: `71/71 passed (python 49/49, guest 13/13, boot 9/9; qemu exit 0)`, `RESULT: PASS (practice)`.

## 10. 핵심 정리

- 동적 실행 파일의 rootfs 요구 = **`PT_INTERP` 로더(커널이 경로 그대로 연다) + `DT_NEEDED` 전이 폐포(로더가 검색해서 연다)**.
- `DT_STRTAB` 은 가상 주소 → `PT_LOAD` 로 파일 오프셋 변환. NEEDED는 링크 옵션(`--as-needed`)이 정한다.
- 탐색 순서: (요구자에 RUNPATH 없으면) RPATH 사슬 → `LD_LIBRARY_PATH` → 요구자 RUNPATH → `ld.so.cache` → 기본 경로. RPATH는 상속, RUNPATH는 안 됨. 잘못된 class/machine 후보는 건너뜀.
- `ldd` 는 바이너리를 로더로 로드한다 → 크로스 타깃에서 `not a dynamic executable`. rootfs 빌드는 ELF를 읽어서.
- 로더 없음 = `-ENOENT` → `No working init found`. 라이브러리 없음 = 로더 메시지 + `Attempted to kill init! exitcode=0x00007f00`.
- `fakeroot` 한 세션 안에서 `chown 0:0` + `mknod` + `mkfs.ext4 -d` = sudo 없는 root 소유 이미지.
- BusyBox `mount -a` 는 `/proc/mounts` 로 "이미 마운트됨"을 판단한다 → `/proc` 을 먼저.
- inittab: sysinit/wait는 기다림, once는 안 기다림, shutdown은 poweroff 직전에 순서대로.

## 참고자료

- Linux 6.18.55 소스(`/var/lab/src/linux-6.18.55`): `init/main.c`(`kernel_init_freeable`, `try_to_run_init_process`, `console_on_rootfs`), `init/do_mounts.c`(`prepare_namespace`, `do_mount_root`), `fs/binfmt_elf.c`(`PT_INTERP` → `open_exec`), `kernel/exit.c`(`Attempted to kill init!`), `Documentation/admin-guide/init.rst`
- BusyBox 1.37.0 소스: `init/init.c`(`parse_inittab`, `run_actions`), `util-linux/mount.c`(`mount -a` 의 `find_mount_point`), `include/libbb.h`(`bb_path_mtab_file`)
- System V ABI / ELF gABI — Dynamic Section(`DT_NEEDED`, `DT_RPATH`, `DT_RUNPATH`, `DT_SONAME`): https://refspecs.linuxfoundation.org/elf/gabi4+/ch5.dynamic.html
- `ld.so(8)`, `ldd(1)`, `ldconfig(8)` 맨 페이지 (VM의 `/usr/share/man`), glibc `elf/dl-load.c` `_dl_map_object()`
- Filesystem Hierarchy Standard 3.0: https://refspecs.linuxfoundation.org/FHS_3.0/fhs/index.html
- e2fsprogs `mke2fs(8)` `-d root-directory`, `fakeroot(1)`
- 이 사이트: ELX-B04(initramfs와 PID 1), ELX-B02(크로스 툴체인·ELF 읽기 C판), ELX-M13(Buildroot), ELN `K_rootfs_build/` K1·K2, `B_toolchain/` B3
