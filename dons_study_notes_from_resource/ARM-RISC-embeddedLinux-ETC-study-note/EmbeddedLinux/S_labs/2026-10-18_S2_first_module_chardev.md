# S2. 첫 모듈 · char device — hello 모듈에서 blocking read·poll·mutex·selftest까지 8단계로 직접 만들기

> **이 노트를 다 읽으면**: 빈 디렉터리에서 시작해 git 태그 8개(m1~m8)로 쌓아 올린 char driver `s2chr`를 직접 다시 만들 수 있다 · 각 단계를 W=1 경고 0개로 빌드하고 Image-trace QEMU 게스트에서 돌려 실제 출력으로 검증할 수 있다 · 일부러 넣은 버그 10개(vermagic, class 누수, `*ppos` 누락, 커널 메모리 누출, 스택 누출, lost wakeup, 타이머 use-after-free Oops, 락 없는 race, goto 사다리 건너뛰기)의 증상을 보고 원인을 바로 말할 수 있다 · 이것을 GitHub 저장소 + 3분 데모로 면접에서 보여 줄 수 있다
> **왜 필요한가**: "char driver 하나 짜 봤나?"는 Embedded Linux 면접의 기본 질문이고, 입사 첫 달에 만나는 사고(모듈이 안 올라감, 재로드 실패, `cat`이 끝나지 않음, rmmod 후 커널 패닉)는 전부 이 8단계 안에 있다
> **Don 기준 난이도**: 링 버퍼·인터럽트 생산자/소비자·에러 경로 정리는 SSD 펌웨어에서 매일 하던 일이다 / 새로 손에 붙일 것은 Linux가 정한 계약(`read`의 반환값과 `f_pos`, `-ERESTARTSYS`, `-ENOTTY`, `.owner`, 등록 순서와 역순 해제)과 그것을 확인하는 도구(sysfs, debugfs, dynamic debug, TAP 테스트)다
> **선행 노트**: S1(QEMU 랩), E3(모듈), E6(유저/커널 경계), G4(char device), G5(ioctl), G6(poll), G8(sysfs/debugfs), F3(sleep/wakeup), F4(locking), M1(로그·dynamic debug) — 압축판은 R3 §2(misc 드라이버 화이트보드 풀이)

---

## 0. 큰 그림 — 무엇을 만들고, 왜 이 순서인가

이 노트는 **이론 노트가 아니라 작업 지시서**다. E·G 노트가 "왜"를 이미 설명했으니, 여기서는 Don이 키보드 앞에서 그대로 따라 치면 같은 결과가 나오는 순서만 쓴다. 이론이 필요한 곳은 노트 ID와 절 번호로만 연결한다.

최종 산출물은 다음과 같다.

- 드라이버 `s2chr.ko` 하나(479줄)가 장치 노드 두 개를 만든다. `/dev/s2chr0`은 seek 가능한 메모리 버퍼(read/write/llseek/ioctl, `O_APPEND`), `/dev/s2chr1`은 타이머가 "센서 샘플"을 밀어 넣는 stream FIFO(blocking read, `poll`)다.
- 사용자 도구 4개(정적 링크): `s2_rw`(read/write 계약), `s2_ioctl`(ioctl 번호와 errno), `s2_poll`(non-blocking·poll·blocking·EINTR), `s2_race`(두 프로세스 동시 append).
- TAP 13 형식 selftest `tests/s2chr_selftest.sh` 16개 항목, 그리고 일부러 망가뜨린 빌드 10개(`bugs/*.patch`)로 "테스트가 버그를 잡는다"를 보여 주는 회귀 실행.
- git 저장소 `~/.elinux_scratch/S2/s2-chardev/` — 마일스톤마다 커밋 1개 + 태그 1개. Don이 그대로 자기 GitHub에 올릴 수 있다.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" text-anchor="middle" font-size="14">s2chr 로드맵 — 태그 m1 ~ m8, 단계마다 커밋 1개</text>
  <rect x="20" y="40" width="150" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="68" text-anchor="middle" font-size="13">M1 hello module</text>
  <text x="95" y="90" text-anchor="middle" font-size="12">init/exit, printk</text>
  <rect x="186" y="40" width="150" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="261" y="68" text-anchor="middle" font-size="13">M2 parameters</text>
  <text x="261" y="90" text-anchor="middle" font-size="12">sysfs 0444/0644</text>
  <rect x="352" y="40" width="150" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="427" y="68" text-anchor="middle" font-size="13">M3 cdev + class</text>
  <text x="427" y="90" text-anchor="middle" font-size="12">/dev/s2chr0 생성</text>
  <rect x="518" y="40" width="150" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="593" y="68" text-anchor="middle" font-size="13">M4 read/write</text>
  <text x="593" y="90" text-anchor="middle" font-size="12">copy_to_user, f_pos</text>
  <rect x="20" y="150" width="150" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="95" y="178" text-anchor="middle" font-size="13">M5 ioctl</text>
  <text x="95" y="200" text-anchor="middle" font-size="12">_IOR/_IOW + UAPI</text>
  <rect x="186" y="150" width="150" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="261" y="178" text-anchor="middle" font-size="13">M6 block + poll</text>
  <text x="261" y="200" text-anchor="middle" font-size="12">wait queue, timer</text>
  <rect x="352" y="150" width="150" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="427" y="178" text-anchor="middle" font-size="13">M7 mutex</text>
  <text x="427" y="200" text-anchor="middle" font-size="12">2-process race</text>
  <rect x="518" y="150" width="150" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="593" y="178" text-anchor="middle" font-size="13">M8 debug + test</text>
  <text x="593" y="200" text-anchor="middle" font-size="12">sysfs, debugfs, TAP</text>
  <line x1="170" y1="75" x2="184" y2="75" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="336" y1="75" x2="350" y2="75" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="502" y1="75" x2="516" y2="75" stroke="currentColor" marker-end="url(#ah1)"/>
  <polyline points="593,110 593,130 95,130 95,148" fill="none" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="185" x2="184" y2="185" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="336" y1="185" x2="350" y2="185" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="502" y1="185" x2="516" y2="185" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="30" y="246" width="12" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="48" y="257" font-size="12">모듈 기초</text>
  <rect x="150" y="246" width="12" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="168" y="257" font-size="12">char device ABI</text>
  <rect x="310" y="246" width="12" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="328" y="257" font-size="12">잠과 동시성</text>
  <rect x="450" y="246" width="12" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="468" y="257" font-size="12">관측과 테스트</text>
</svg>
```
그림 1 — 8개 마일스톤. 색은 주제 묶음이다. 각 상자가 git 태그 하나이고, 앞 단계 코드를 지우지 않고 쌓는다. 드라이버 소스 증가량(`git diff --shortstat`): m2 +30, m3 +71, m4 +62, m5 +41, m6 +145, m7 +47, m8 +114줄.

### 0.1 이론은 어디에 있나 — 마일스톤별 연결표

| 마일스톤 | 손으로 하는 일 | 이론 노트 | 화이트보드 압축판 |
|---|---|---|---|
| M1 hello | Kbuild out-of-tree, printk 레벨, `modinfo`, taint | E3 §1–3, §9–10, M1 노트 §1 | R3 §2.3 뼈대 |
| M2 파라미터 | `module_param`, 권한, 범위 검사 | E3 §5 | — |
| M3 cdev + class | `alloc_chrdev_region` → `cdev_add` → `class_create` → `device_create` | G4 §1–3, G2 | R3 §2(misc는 이 단계를 한 줄로 줄인 것, G7) |
| M4 read/write | `copy_to_user`, `*ppos`, short read, EOF, ENOSPC | E6 §3, G4 §6 | R3 §2.5 |
| M5 ioctl | `_IOR/_IOW`, UAPI 헤더, `-ENOTTY` | E6 §6, G5 §2–3 | R3 §2.5 |
| M6 blocking + poll | wait queue, `-ERESTARTSYS`, `.poll`, 타이머 | F3 §2·§4, G6 §1, F8 | R3 §2.4 그림 |
| M7 동시성 | mutex, 여러 opener, 두 프로세스 race | F4 §5, G4 §7 | R3 §2.7 |
| M8 관측·테스트 | sysfs 속성, debugfs, dynamic debug, goto 사다리 vs devm | G8 §2·§9, M1 노트 §4, E3 §6, G3 | R3 §3.4(devm 순서) |

표기 규칙: 이 노트에서 맨 M1~M8은 **마일스톤**이고, 모듈 M(디버깅·트레이싱)의 노트는 "M1 노트"처럼 "노트"를 붙여 구분한다.

R3 §2의 misc 드라이버는 "45분 안에 칠판에 쓰는" 압축판이다. 이 노트는 같은 내용을 **실제 저장소로, 한 단계씩, 실패까지 재현하면서** 만든다. 면접에서는 R3로 말하고, "실제로 만들어 본 저장소가 있다"고 이 노트의 결과물을 보여 주는 조합이 가장 강하다.

---

## 1. 준비 — 랩, 저장소 레이아웃, 스크립트 4개

### 1.1 전제 조건

- Lima VM `elinux`(호스트 커널 `uname -r` = `7.0.0-38-generic`, gcc 15.2). **모듈은 VM 호스트에 절대 넣지 않는다.** 게스트 커널만 쓴다.
- 랩 커널 `/var/lab/out/Image-trace`(6.18.55-trace, ftrace·kprobes·BTF 포함)와 그 빌드 트리 `/var/lab/out/linux-build-trace`(`Module.symvers` 있음). 게스트는 BusyBox 1.37 initramfs(`/var/lab/build/rootfs`)를 복사해 우리 `.ko`와 정적 도구를 넣어 다시 묶는다. S1이 이 랩을 처음부터 만드는 과정이다.
- 이 커널 설정에서 결과에 영향을 주는 값: `CONFIG_DEVTMPFS=y`, `CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_DEBUG_FS=y`, `CONFIG_PREEMPT=y`, `CONFIG_HZ=250`, `CONFIG_INIT_STACK_ALL_ZERO=y`, `CONFIG_HARDENED_USERCOPY` 꺼짐, `CONFIG_PROVE_LOCKING` 꺼짐, `CONFIG_LOCALVERSION="-trace"`. 이 값들이 §6·§5의 버그 증상을 바꾼다(그때 다시 설명한다).
- QEMU는 KVM 없이 TCG로 돈다. **이 노트의 모든 시간 값은 에뮬레이션 수치이고 실제 보드 숫자가 아니다.**

### 1.2 저장소 레이아웃

Mac 쪽 `~/.elinux_scratch/S2/s2-chardev/`에 git 저장소를 만들고, VM에서는 읽기 전용으로 보이므로 빌드는 VM의 `/var/tmp/S2/build`에 복사해서 한다.

```
s2-chardev/
  Kbuild                 obj-m := s2chr.o
  Makefile               modules (W=1) + static tools
  s2chr.c                the driver (grows m1 -> m8)
  include/s2chr_ioctl.h  UAPI header shared by driver and tools   (m5)
  tools/s2_rw.c          read/write/llseek contract               (m4)
  tools/s2_ioctl.c       ioctl numbers + errno                    (m5)
  tools/s2_poll.c        O_NONBLOCK, poll, blocking, EINTR        (m6)
  tools/s2_race.c        N processes append records               (m7)
  tests/s2chr_selftest.sh  TAP 13, 16 checks                      (m8)
  guest/m1.sh .. m8.sh   one scripted scenario per milestone
  guest/regress.sh       selftest against a broken build
  bugs/*.patch           10 deliberately broken variants
  scripts/build.sh mkinitrd.sh run-qemu.sh build-bug.sh
```

### 1.3 빌드 파일 — Kbuild와 Makefile

무엇을 확인하나: out-of-tree 모듈은 커널 트리의 Makefile을 `-C`로 부르고 `M=`으로 우리 디렉터리를 알려 준다(E3 §1.3). 같은 Makefile에서 정적 사용자 도구도 만든다.

```make
# Kbuild
obj-m := s2chr.o
ccflags-y := -I$(src)/include
```

```make
# Makefile — KDIR = 모듈을 올릴 바로 그 커널의 빌드 트리
KDIR ?= /var/lab/out/linux-build-trace
TOOLS := $(patsubst %.c,%,$(wildcard tools/*.c))

all: modules tools

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) W=1 modules

tools: $(TOOLS)
tools/%: tools/%.c $(wildcard include/*.h)
	$(CC) -static -O2 -Wall -Wextra -Iinclude -o $@ $<

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f $(TOOLS)

.PHONY: all modules tools clean
```

`W=1`은 커널이 기본으로 끈 추가 경고(`-Wmissing-prototypes`, `-Wunused-but-set-variable` 등)를 켠다. 이 프로젝트의 규칙은 **모든 마일스톤에서 W=1 경고 0개**다. 도구를 `-static`으로 만드는 이유는 BusyBox initramfs에 glibc가 없기 때문이다.

### 1.4 스크립트 4개 — 빌드, initramfs, 부팅, 버그 빌드

```sh
#!/bin/sh
# scripts/build.sh — VM 안에서 실행. Mac 홈은 VM에서 읽기 전용이라 복사본에서 빌드
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-/var/tmp/S2/build}
rm -rf "$OUT" && mkdir -p "$OUT"
cp -a "$SRC"/. "$OUT"/ && rm -rf "$OUT/.git"
make -C "$OUT" KDIR="${1:-/var/lab/out/linux-build-trace}" all
```

```sh
#!/bin/sh
# scripts/mkinitrd.sh — 랩 BusyBox rootfs + .ko + 정적 도구 + 게스트 스크립트 -> initramfs
set -e
B=${OUT:-/var/tmp/S2/build}; W=/var/tmp/S2; R=$W/rootfs
rm -rf "$R" && mkdir -p "$R" && cp -a /var/lab/build/rootfs/. "$R"/
mkdir -p "$R/s2"
cp "$B"/*.ko "$R/s2/" 2>/dev/null || true
cp -r "$B/guest" "$B/tests" "$R/s2/"
for t in "$B"/tools/*; do [ -x "$t" ] && cp "$t" "$R/s2/"; done
ls "$W"/extra/* >/dev/null 2>&1 && cp "$W"/extra/* "$R/s2/"; true
(cd "$R" && find . | cpio -o -H newc --quiet | gzip) > "$W/i.gz"
ls -l "$W/i.gz" | awk '{print "initramfs:", $5, "bytes"}'
```

```sh
#!/bin/sh
# scripts/run-qemu.sh <guest/script.sh[~args]> [extra kernel args]
S=$1; shift
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G \
  -nographic -no-reboot -kernel /var/lab/out/Image-trace -initrd /var/tmp/S2/i.gz \
  -append "console=ttyAMA0 quiet panic=1 $* LAB_CMD=sh~/s2/$S" 2>&1 | tr -d '\r' \
  | sed -n '/lab initramfs/,$p' | grep -v 'reboot: Power down'
```

```sh
#!/bin/sh
# scripts/build-bug.sh bugs/<name>.patch  ->  /var/tmp/S2/extra/s2chr_<name>.ko
# 선택: BUG_KCFLAGS=... 로 망가진 빌드에만 컴파일러 플래그 추가
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd); P=$SRC/$1; N=$(basename "$1" .patch)
OUT=/var/tmp/S2/bug-$N; KDIR=${KDIR:-/var/lab/out/linux-build-trace}
rm -rf "$OUT" && mkdir -p "$OUT" /var/tmp/S2/extra
cp "$SRC"/Kbuild "$SRC"/s2chr.c "$OUT"/ && cp -r "$SRC"/include "$OUT"/
(cd "$OUT" && patch -s -p1 < "$P")
make -s -C "$KDIR" M="$OUT" KCFLAGS="$BUG_KCFLAGS" modules
cp "$OUT"/s2chr.ko /var/tmp/S2/extra/s2chr_"$N".ko
echo "built /var/tmp/S2/extra/s2chr_$N.ko"
```

게스트의 `/init`은 커널 인자 `LAB_CMD=`의 `~`를 공백으로 바꿔 명령 하나를 실행하고 `poweroff -f` 한다. 그래서 "부팅 1번 = 시나리오 1개"다. `-smp 2`는 M7의 race에 필요하고, `-no-reboot` + `panic=1`은 M6에서 일부러 낸 커널 패닉이 180초 타임아웃까지 매달리지 않고 1초 뒤 QEMU를 끝내게 한다. 모든 단계의 루프는 이것 하나다.

```sh
# Mac에서 (저장소 루트)
limactl shell elinux -- bash -lc 'cd /Users/donh/.elinux_scratch/S2/s2-chardev
  sh scripts/build.sh && sh scripts/mkinitrd.sh && sh scripts/run-qemu.sh guest/mN.sh'
git add -A && git commit -m "MN: ..." && git tag mN
```

> 체크포인트: `scripts/` 네 파일과 `Kbuild`, `Makefile`이 생겼고 `git init`이 끝났다. 아직 `s2chr.c`가 없으니 빌드는 실패한다.

---

## 2. M1 — hello 모듈 (init/exit, printk 레벨, modinfo)

### 2.1 코드

무엇을 확인하나: 가장 작은 모듈의 네 부품 — `module_init/exit`, `MODULE_LICENSE`, printk 레벨, `pr_fmt`. 이론은 E3 §1, 레벨과 콘솔은 M1 노트 §1–2.

```c
// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/utsname.h>

static int __init s2chr_init(void)
{
	pr_err("M1 err-level line (console shows it even with quiet)\n");
	pr_warn("M1 warn-level line\n");
	pr_info("hello from %s, kernel %s\n", KBUILD_MODNAME, init_utsname()->release);
	pr_debug("debug line: compiled in, off until dynamic debug enables it\n");
	return 0;
}

static void __exit s2chr_exit(void)
{
	pr_info("bye\n");
}

module_init(s2chr_init);
module_exit(s2chr_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Don H");
MODULE_DESCRIPTION("S2 hands-on char device (milestone build)");
MODULE_VERSION("0.1");
```

`pr_fmt`을 `#include`보다 먼저 정의해야 모든 `pr_*`에 `s2chr: ` 접두어가 붙는다. 커널 릴리스는 컴파일 시점 상수(`UTS_RELEASE`) 대신 실행 중인 커널 값(`init_utsname()->release`)을 찍었다 — 로드된 커널이 무엇인지가 궁금한 것이기 때문이다.

### 2.2 빌드 — W=1, 경고 0개

```sh
sh scripts/build.sh && modinfo /var/tmp/S2/build/s2chr.ko
```

```text
make: Entering directory '/var/tmp/S2/build'
make -C /var/lab/out/linux-build-trace M=/var/tmp/S2/build W=1 modules
make[1]: Entering directory '/var/lab/build/linux-trace'
make[2]: Entering directory '/var/tmp/S2/build'
  CC [M]  s2chr.o
  MODPOST Module.symvers
  CC [M]  s2chr.mod.o
  CC [M]  .module-common.o
  LD [M]  s2chr.ko
  BTF [M] s2chr.ko
make[2]: Leaving directory '/var/tmp/S2/build'
make[1]: Leaving directory '/var/lab/build/linux-trace'
make: Leaving directory '/var/tmp/S2/build'
filename:       /var/tmp/S2/build/s2chr.ko
version:        0.1
description:    S2 hands-on char device (milestone build)
author:         Don H
license:        GPL
srcversion:     AEDAFE0A22A0AA4AF11032F
depends:        
name:           s2chr
vermagic:       6.18.55-trace SMP preempt mod_unload aarch64
```

출력에서 볼 것: `BTF [M]`은 이 커널이 `CONFIG_DEBUG_INFO_BTF_MODULES`라서 모듈에도 BTF가 붙는다는 뜻이다(M7 노트의 bpftrace가 모듈 구조체를 읽을 수 있게 된다). `vermagic`의 `6.18.55-trace`가 **게스트 커널의 `uname -r`과 정확히 같아야** 한다.

### 2.3 게스트에서 실행

```sh
# guest/m1.sh (요지)
insmod s2chr.ko; dmesg | grep s2chr; grep s2chr /proc/modules
cat /sys/module/s2chr/{version,initstate,taint}; cat /proc/sys/kernel/tainted
rmmod s2chr; insmod s2chr.ko dyndbg=+p; dmesg | grep 'debug line'
```

```text
lab initramfs: 6.18.55-trace up 2.86s
== insmod
[    3.097306][   T77] s2chr: M1 err-level line (console shows it even with quiet)
== dmesg
[    3.057896][   T77] s2chr: loading out-of-tree module taints kernel.
[    3.058168][   T77] s2chr: module verification failed: signature and/or required key missing - tainting kernel
[    3.097306][   T77] s2chr: M1 err-level line (console shows it even with quiet)
[    3.097350][   T77] s2chr: M1 warn-level line
[    3.097363][   T77] s2chr: hello from s2chr, kernel 6.18.55-trace
== /proc/modules
s2chr 16384 0 - Live 0xffffa3e25d1b2000 (OE)
== /sys/module/s2chr
coresize holders initsize initstate notes refcnt sections srcversion taint uevent version 
version=0.1 initstate=live taint=OE
== kernel taint
12288
== rmmod
[    3.195791][   T87] s2chr: bye
== insmod with dyndbg=+p (pr_debug turned on at load time)
[    3.250872][   T90] s2chr: M1 err-level line (console shows it even with quiet)
[    3.250943][   T90] s2chr: debug line: compiled in, off until dynamic debug enables it
```

출력에서 볼 것:

- `quiet`로 부팅하면 콘솔 로그 레벨이 4라서 **`pr_err`(3)만 콘솔에 바로** 나오고, `pr_warn`(4)·`pr_info`(6)는 `dmesg`에만 있다. 첫 줄이 두 번 보이는 이유다.
- taint `OE` = out-of-tree(O, 4096) + unsigned(E, 8192) → `12288`. 이 랩은 `MODULE_SIG`가 강제가 아니라 경고만 낸다(E3 §9).
- `pr_debug`는 기본으로 꺼져 있다가, `insmod ... dyndbg=+p`로 **로드 시점에** 켜졌다. 마일스톤 M8에서 런타임에 켜는 법을 본다(M1 노트 §4).
- 주소 `0xffffa3e25d1b2000`은 KASLR 때문에 부팅마다 다르다.

> 체크포인트: `insmod` 후 `/sys/module/s2chr/initstate`가 `live`, `rmmod` 후 `bye`가 dmesg에 있어야 한다. 커밋하고 `git tag m1`.

### 2.4 FAIL-1 재현 — 다른 커널 트리로 빌드한 모듈

무엇을 확인하나: "같은 6.18.55인데 왜 안 올라가나"의 실제 모습. 같은 소스를 다른 랩 트리 두 개로 빌드해서 Image-trace 게스트에 넣었다.

```sh
OUT=/var/tmp/S2/build-wrong  sh scripts/build.sh /var/lab/out/linux-build      # defconfig 계열, 6.18.55
OUT=/var/tmp/S2/build-wrong2 sh scripts/build.sh /var/lab/out/linux-build-rt   # PREEMPT_RT, 6.18.55-rt
# 게스트: insmod s2chr_wrongkdir.ko; insmod s2chr_rtkdir.ko
```

```text
== FAIL-1: s2chr_wrongkdir.ko (built against another kernel tree)
insmod: can't insert 's2chr_wrongkdir.ko': invalid module format
[    3.310583][   T94] module s2chr: .gnu.linkonce.this_module section size must match the kernel's built struct module size at run time
insmod exit=1
== FAIL-1: s2chr_rtkdir.ko (built against another kernel tree)
insmod: can't insert 's2chr_rtkdir.ko': invalid module format
insmod exit=1
[    3.334023][   T97] s2chr: version magic '6.18.55-rt SMP preempt_rt mod_unload aarch64' should be '6.18.55-trace SMP preempt mod_unload aarch64'
```

출력에서 볼 것: 유저가 보는 말은 둘 다 `invalid module format`(-ENOEXEC)뿐이고, **진짜 이유는 dmesg에만** 있다. 그리고 이유가 두 가지로 갈렸다.

- defconfig 트리(`linux-build`, vermagic `6.18.55`)로 만든 모듈은 vermagic 검사까지 가지도 못했다. trace 설정은 ftrace·kprobes 등으로 `struct module`이 더 커서, 로더가 ELF 섹션 `.gnu.linkonce.this_module`의 크기를 실행 중인 커널의 `sizeof(struct module)`과 비교하는 **더 이른 검사**에서 거절했다. (`linux-build-sec`로 빌드한 것도 같은 메시지였다 — 확인함.)
- RT 트리(`6.18.55-rt`)는 `struct module` 크기가 우연히 같아서 그 검사를 통과했고, 고전적인 **version magic** 메시지가 나왔다(E3 §10.1과 같은 메시지).

고치는 법은 하나다: **장비에 올라간 그 커널의 빌드 트리(`.config` + `Module.symvers`)로 빌드한다.** BSP를 받으면 `uname -r`, `/proc/config.gz`, 벤더가 준 `Module.symvers`를 먼저 맞춘다.

---

## 3. M2 — 모듈 파라미터 (타입, 권한, 범위 검사)

### 3.1 코드 (M1에서 바뀐 부분)

무엇을 확인하나: `module_param(이름, 타입, 권한)`의 세 번째 인자가 `/sys/module/s2chr/parameters/` 파일의 권한이 된다는 것, 그리고 **값 검증은 드라이버 책임**이라는 것(E3 §5).

```c
#include <linux/moduleparam.h>

#define S2_BUF_MIN	64
#define S2_BUF_MAX	(1024 * 1024)

static unsigned int buf_size = 4096;		/* fixed after load: 0444 */
module_param(buf_size, uint, 0444);
MODULE_PARM_DESC(buf_size, "kernel buffer size in bytes (64..1048576)");

static bool verbose;				/* root may flip it: 0644 */
module_param(verbose, bool, 0644);
MODULE_PARM_DESC(verbose, "log every read/write at info level");

static char *tag = "s2";			/* string: charp, 0444 */
module_param(tag, charp, 0444);
MODULE_PARM_DESC(tag, "label printed in messages");

static int secret = 42;				/* perm 0: no sysfs file */
module_param(secret, int, 0);
MODULE_PARM_DESC(secret, "load-time only, invisible in sysfs");

static int __init s2chr_init(void)
{
	if (buf_size < S2_BUF_MIN || buf_size > S2_BUF_MAX) {
		pr_err("buf_size=%u out of range [%u..%u]\n",
		       buf_size, S2_BUF_MIN, S2_BUF_MAX);
		return -EINVAL;
	}
	pr_info("[%s] hello, kernel %s, buf_size=%u verbose=%d secret=%d\n",
		tag, init_utsname()->release, buf_size, verbose, secret);
	return 0;
}
```

`secret`(권한 0)은 sysfs 파일이 없다는 것을 보여 주는 M2 전용 예시라서 M3에서 지웠다. `buf_size`는 M4에서 버퍼 크기가 되므로 **로드 후 바뀌면 안 된다** → 0444.

### 3.2 실행

```text
== insmod with parameters
[    2.447404][   T77] s2chr: [imu0] hello, kernel 6.18.55-trace, buf_size=8192 verbose=1 secret=7
== /sys/module/s2chr/parameters
-r--r--r-- buf_size
-r--r--r-- tag
-rw-r--r-- verbose
buf_size = 8192
tag = imu0
verbose = Y
== root writes verbose (0644)
N
== root writes buf_size (0444)
/s2/guest/m2.sh: line 8: can't create /sys/module/s2chr/parameters/buf_size: Permission denied
exit=1
[    2.580724][   T89] s2chr: [imu0] bye (verbose=0 at exit)
== bad values
[    2.655592][   T92] s2chr: buf_size=10 out of range [64..1048576]
insmod: can't insert 's2chr.ko': invalid parameter
exit=1
[    2.738088][   T95] s2chr: `maybe' invalid for parameter `verbose'
insmod: can't insert 's2chr.ko': invalid parameter
exit=1
exit=0
[    2.824365][   T98] s2chr: unknown parameter 'no_such' ignored
[    2.824932][   T98] s2chr: [s2] hello, kernel 6.18.55-trace, buf_size=4096 verbose=0 secret=42
```

(bad values 부분은 `pr_err`가 콘솔과 dmesg에 두 번 찍힌 줄을 하나씩 지웠다.)

출력에서 볼 것:

- `secret`은 `parameters/`에 없다(권한 0). bool은 `Y`/`N`으로 보인다.
- **root도 0444 파일에는 못 쓴다.** 일반 파일이라면 root는 `CAP_DAC_OVERRIDE`로 통과하지만, sysfs(kernfs)는 open 시점에 "쓰기 권한 비트나 store가 없으면 거절"을 따로 검사해서 `EACCES`를 낸다.
- 범위 밖 값은 우리 init이 `-EINVAL`을 돌려 거절했고, `verbose=maybe`는 커널 파서(`param_set_bool`)가 우리 코드에 오기도 전에 거절했다. 둘 다 유저에게는 `invalid parameter`다.
- **모르는 파라미터는 실패가 아니다**: 경고 한 줄 남기고 로드된다(exit=0). 오타 난 파라미터가 조용히 무시되는 사고가 여기서 난다 — 항상 dmesg를 본다.

> 체크포인트: `parameters/`에 파일 3개, `buf_size=10`이면 로드 실패. `git tag m2`.

---

## 4. M3 — char device: `alloc_chrdev_region` + `cdev` + class/device → `/dev/s2chr0`

### 4.1 무엇을 만드나 — 객체 그래프

장치 노드 하나가 생기려면 네 가지가 필요하다: (1) 번호 `dev_t`를 빌리고, (2) 그 번호에 `file_operations`를 묶는 `cdev`를 등록하고, (3) sysfs에 class와 device를 만들어 uevent를 내고, (4) devtmpfs가 그 uevent로 `/dev` 노드를 만든다. 이론은 G4 §1–3이다. 아래 그림은 M6까지 끝난 최종 모습이다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14">open("/dev/s2chr1") 이후 커널 객체가 서로 가리키는 모습</text>
  <rect x="20" y="36" width="200" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="56" text-anchor="middle" font-size="13">user: fd = 3</text>
  <text x="120" y="73" text-anchor="middle" font-size="12">read(3, buf, n)</text>
  <rect x="20" y="120" width="200" height="100" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="140" font-size="13">struct file</text>
  <text x="30" y="162" font-size="12">f_op → s2_fifo_fops</text>
  <text x="30" y="182" font-size="12">private_data → s2_dev</text>
  <text x="30" y="202" font-size="12">f_pos (unused: stream)</text>
  <rect x="250" y="120" width="190" height="100" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="260" y="140" font-size="13">struct inode</text>
  <text x="260" y="162" font-size="12">i_rdev = 510:1</text>
  <text x="260" y="182" font-size="12">i_cdev → fifo_cdev</text>
  <text x="260" y="202" font-size="12">(devtmpfs node)</text>
  <rect x="470" y="36" width="190" height="270" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="480" y="56" font-size="13">struct s2_dev s2</text>
  <text x="480" y="80" font-size="12">cdev (minor 0)</text>
  <rect x="478" y="92" width="174" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="486" y="110" font-size="12">fifo_cdev (minor 1)</text>
  <text x="486" y="128" font-size="12">.ops = s2_fifo_fops</text>
  <text x="480" y="158" font-size="12">kfifo + fifo_lock</text>
  <text x="480" y="180" font-size="12">wait_queue_head rq</text>
  <text x="480" y="202" font-size="12">timer_list timer</text>
  <text x="480" y="224" font-size="12">mutex lock, buf, len</text>
  <text x="480" y="246" font-size="12">dev[0], dev[1]</text>
  <text x="480" y="268" font-size="12">atomic open_count</text>
  <text x="480" y="290" font-size="12">dentry dbg</text>
  <rect x="20" y="300" width="200" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="324" text-anchor="middle" font-size="13">chrdev region 510:0-1</text>
  <text x="120" y="346" text-anchor="middle" font-size="12">kobj_map: devt → cdev</text>
  <rect x="250" y="300" width="190" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="345" y="324" text-anchor="middle" font-size="12">/sys/class/s2chr/s2chr1</text>
  <text x="345" y="346" text-anchor="middle" font-size="12">uevent → devtmpfs node</text>
  <line x1="120" y1="80" x2="120" y2="118" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="220" y1="150" x2="248" y2="150" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="234" y="112" text-anchor="middle" font-size="12">f_inode</text>
  <line x1="440" y1="178" x2="476" y2="118" stroke="currentColor" marker-end="url(#ah2)"/>
  <path d="M 200 178 C 300 250, 420 250, 470 230" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
  <text x="335" y="262" text-anchor="middle" font-size="12">container_of</text>
  <line x1="220" y1="330" x2="248" y2="330" stroke="#888" stroke-dasharray="4 3"/>
  <line x1="120" y1="300" x2="300" y2="222" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
  <text x="140" y="286" font-size="12">chrdev_open()</text>
  <line x1="345" y1="300" x2="345" y2="222" stroke="#d0564a" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
  <text x="20" y="390" font-size="12">open → chrdev_open이 kobj_map에서 cdev를 찾아 i_cdev에 캐시 → f_op = cdev->ops → 우리 .open</text>
</svg>
```
그림 2 — char device 객체 그래프. 사용자 fd → `struct file`(열 때마다 하나) → `struct inode`(노드 하나에 하나) → `cdev` → 우리 `s2_dev`. 드라이버는 `container_of(inode->i_cdev, struct s2_dev, fifo_cdev)`로 자기 구조체를 찾고 `file->private_data`에 저장해 이후 read/write/ioctl이 쓴다. 왼쪽 아래의 번호 지도와 오른쪽 아래의 class/device는 등록 시점에만 쓰인다.

### 4.2 코드 (M2에서 바뀐 부분)

무엇을 확인하나: 등록 4단계와, 실패하면 **거꾸로** 정리하는 goto 사다리(E3 §6). 아직 `.read`가 없으므로 open/release만 있다.

```c
#define S2_NR_MINORS	1

struct s2_dev {
	dev_t devt;			/* first major:minor we own */
	struct cdev cdev;		/* links devt -> our fops */
	const struct class *class;	/* /sys/class/s2chr */
	struct device *dev;		/* /sys/class/s2chr/s2chr0 -> /dev/s2chr0 */
};
static struct s2_dev s2;

static int s2_open(struct inode *inode, struct file *filp)
{
	struct s2_dev *d = container_of(inode->i_cdev, struct s2_dev, cdev);

	filp->private_data = d;
	pr_info("[%s] open minor=%u by %s(%d)\n", tag, iminor(inode),
		current->comm, task_pid_nr(current));
	return 0;
}
```

```c
static const struct file_operations s2_fops = {
	.owner		= THIS_MODULE,	/* open 중 rmmod 방지 (G4 §8) */
	.open		= s2_open,
	.release	= s2_release,	/* pr_info만 하는 대칭 함수 */
};

static int __init s2chr_init(void)
{
	int ret;	/* (buf_size 검사는 M2와 같음) */

	ret = alloc_chrdev_region(&s2.devt, 0, S2_NR_MINORS, "s2chr");
	if (ret)
		return ret;
	cdev_init(&s2.cdev, &s2_fops);
	ret = cdev_add(&s2.cdev, s2.devt, S2_NR_MINORS);
	if (ret)
		goto err_region;
	s2.class = class_create("s2chr");	/* 6.4+: owner 인자 없음 */
	if (IS_ERR(s2.class)) {
		ret = PTR_ERR(s2.class);
		goto err_cdev;
	}
	s2.dev = device_create(s2.class, NULL, s2.devt, NULL, "s2chr0");
	if (IS_ERR(s2.dev)) {
		ret = PTR_ERR(s2.dev);
		goto err_class;
	}
	return 0;
err_class:
	class_destroy(s2.class);
err_cdev:
	cdev_del(&s2.cdev);
err_region:
	unregister_chrdev_region(s2.devt, S2_NR_MINORS);
	return ret;
}

static void __exit s2chr_exit(void)
{
	device_destroy(s2.class, s2.devt);
	class_destroy(s2.class);
	cdev_del(&s2.cdev);
	unregister_chrdev_region(s2.devt, S2_NR_MINORS);
}
```

`class_create()`는 6.4부터 `owner` 인자가 없어졌다(G4 §3.2). 예전 블로그의 `class_create(THIS_MODULE, "x")`를 그대로 쓰면 6.18에서 컴파일 에러다. 그리고 순서: `cdev_add`가 성공하는 순간 같은 번호로 노드를 가진 사람은 open할 수 있다 — 그래서 실제 버퍼 같은 상태는 `cdev_add` **전에** 준비한다(M4에서 `kzalloc`을 맨 앞에 둔 이유, G4 §2.3).

### 4.3 실행

```text
== insmod
[    5.806945][   T77] s2chr: [s2] registered 510:0 buf_size=4096
== /proc/devices
510 s2chr
== /dev node (made by devtmpfs)
crw-------    1 0        0         510,   0 Oct 18 23:31 /dev/s2chr0
== sysfs
s2chr0
MAJOR=510
MINOR=0
DEVNAME=s2chr0
../../devices/virtual/s2chr/s2chr0
== open + close with no .read
cat: read error: Invalid argument
cat exit=1
[    5.997763][   T85] s2chr: [s2] open minor=0 by cat(85)
[    5.999402][   T85] s2chr: [s2] release minor=0
== rmmod -> node gone, insmod again works
ls: /dev/s2chr0: No such file or directory
0
re-insmod ok: [    6.267386][   T92] s2chr: [s2] registered 510:0 buf_size=4096
```

출력에서 볼 것:

- 동적 major가 510이다. 이 랩 커널은 빌트인 드라이버들이 234~254를 다 가져가고 확장 구간의 511은 `rpmb`가 가져가서 510이 나온다(G4 예제 2). config와 로드 순서에 따라 바뀌므로 하드코딩하지 말 것.
- 노드는 `mknod` 없이 생겼다: `device_create` → uevent(`MAJOR=510 MINOR=0 DEVNAME=s2chr0`) → devtmpfs. 부모가 없으므로 `/sys/devices/virtual/s2chr/` 아래에 붙었다.
- 권한이 `crw-------`(root만)인 이유: devtmpfs 기본값 0600. 바꾸려면 class의 `devnode` 콜백이나 udev 규칙(G7 §4.5).
- `.read`가 없으면 VFS가 `-EINVAL`을 돌려준다. open/release는 정상으로 불렸다.

> 체크포인트: `/dev/s2chr0`이 `c 510,0`으로 생기고, rmmod 후 사라지고, 다시 insmod가 된다. `git tag m3`.

### 4.4 FAIL-2 재현 — exit에서 `class_destroy`를 빼먹으면

무엇을 확인하나: "처음엔 되는데 rmmod 후 다시 insmod가 안 된다"의 정체. `bugs/m3-leak-class.patch`는 exit의 `class_destroy` 한 줄을 지운다.

```sh
sh scripts/build-bug.sh bugs/m3-leak-class.patch
# 게스트: insmod s2chr_m3-leak-class.ko; rmmod s2chr; ls /sys/class; insmod s2chr.ko
```

```text
== FAIL-2: exit() forgets class_destroy
after rmmod: /sys/class/s2chr still there
insmod: can't insert 's2chr.ko': File exists
insmod exit=1
sysfs: cannot create duplicate filename '/class/s2chr'
CPU: 1 UID: 0 PID: 102 Comm: insmod Tainted: G           OE       6.18.55-trace #1 PREEMPT 
Call trace:
 ...
 sysfs_warn_dup+0x6c/0x90
 sysfs_create_dir_ns+0xf4/0x120
 kobject_add_internal+0x9c/0x2e0
 kset_register+0x64/0xc8
 class_register+0x138/0x1a0
 class_create+0x4c/0xa0
 s2chr_init+0xb0/0xf70 [s2chr]
 do_one_initcall+0x5c/0x280
 do_init_module+0x5c/0x270
 load_module+0x1ba0/0x2258
 ...
kobject: kobject_add_internal failed for s2chr with -EEXIST, don't try to register things with the same name in the same directory.
```

(타임스탬프 칸을 sed로 지웠고, 스택의 앞뒤 몇 줄을 `...`로 줄였다.)

출력에서 볼 것: rmmod는 **성공**했다. 모듈 코드는 사라졌는데 `class_create`가 할당한 class 객체와 `/sys/class/s2chr` 디렉터리는 남았다. 다음 `class_create("s2chr")`가 같은 이름의 kobject를 만들려다 `-EEXIST` → `insmod: File exists`. 스택은 init 경로를 그대로 보여 준다: `s2chr_init` → `class_create` → `class_register` → `kset_register` → `sysfs_create_dir_ns`. 이 상태는 **재부팅 전까지 고칠 수 없다**(그 class를 가리키는 포인터를 가진 코드가 이제 없다). 그래서 M8의 selftest에는 "rmmod 후 아무것도 남지 않았나" 검사가 들어간다.

---

## 5. M4 — read/write: 커널 버퍼, `copy_to_user`/`copy_from_user`, `*ppos`

### 5.1 코드 (M3에서 바뀐 부분)

무엇을 확인하나: `read`/`write`의 계약 — **반환값은 실제로 옮긴 바이트 수, 0은 EOF, 음수는 -errno**, 그리고 `*ppos`를 직접 전진시킨다(E6 §3, G4 §6). 버퍼는 `buf_size` 파라미터 크기로 `kzalloc`한다.

```c
static ssize_t s2_read(struct file *filp, char __user *ubuf, size_t count,
		       loff_t *ppos)
{
	struct s2_dev *d = filp->private_data;
	size_t n;

	if (*ppos >= d->len)
		return 0;				/* EOF */
	n = min_t(size_t, count, d->len - *ppos);	/* short read is fine */
	if (copy_to_user(ubuf, d->buf + *ppos, n))
		return -EFAULT;
	*ppos += n;
	return n;
}

static ssize_t s2_write(struct file *filp, const char __user *ubuf,
			size_t count, loff_t *ppos)
{
	struct s2_dev *d = filp->private_data;
	size_t n;

	if (*ppos >= buf_size)
		return -ENOSPC;
	n = min_t(size_t, count, buf_size - *ppos);	/* short write */
	if (copy_from_user(d->buf + *ppos, ubuf, n))
		return -EFAULT;
	*ppos += n;
	d->len = max_t(size_t, d->len, *ppos);
	return n;
}

static loff_t s2_llseek(struct file *filp, loff_t off, int whence)
{
	struct s2_dev *d = filp->private_data;

	/* SEEK_END is relative to the valid data, pos is capped at buf_size */
	return generic_file_llseek_size(filp, off, whence, buf_size, d->len);
}

/* s2_open()에 추가: 쓰기 + O_TRUNC 이면 내용을 비운다 ("echo x >" 가 덮어쓰기) */
	if ((filp->f_flags & O_ACCMODE) != O_RDONLY && (filp->f_flags & O_TRUNC))
		d->len = 0;

/* init: s2.buf = kzalloc(buf_size, GFP_KERNEL) 을 맨 앞에, err_buf: kfree() 를 맨 끝에 */
```

`copy_to_user`는 **못 옮긴 바이트 수**를 돌려준다(0이면 성공). 그래서 `if (copy_to_user(...)) return -EFAULT;`가 정석이다. 이 단계는 아직 락이 없다 — 동시에 두 명이 쓰면 깨진다는 것을 M7에서 실제로 보여 준다.

### 5.2 계약 테스트 도구 `s2_rw`

무엇을 확인하나: 셸 명령으로는 보기 어려운 경계값 — short read, EOF, SEEK_END, pread, short write, ENOSPC, 범위 밖 seek, 나쁜 사용자 포인터.

```c
/* tools/s2_rw.c — check the read/write/llseek contract of /dev/s2chr0 (M4) */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SHOW(call) do { ssize_t r_ = (call); \
	printf("%-44s -> %zd%s%s\n", #call, r_, r_ < 0 ? " " : "", \
	       r_ < 0 ? strerror(errno) : ""); } while (0)

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/s2chr0";
	long size = argc > 2 ? atol(argv[2]) : 4096;
	char b[8] = {0};
	char *volatile bad = (char *)1;     /* not a valid user address */
	int fd = open(path, O_RDWR | O_TRUNC);

	if (fd < 0) { perror("open"); exit(1); }
	SHOW(write(fd, "hello world\n", 12));
	SHOW(lseek(fd, 0, SEEK_SET));
	SHOW(read(fd, b, 5));            printf("   b=\"%.5s\"\n", b);
	SHOW(read(fd, b, 5));            printf("   b=\"%.5s\"\n", b);
	SHOW(read(fd, b, 5));            /* short read: only 2 left */
	SHOW(read(fd, b, 5));            /* EOF */
	SHOW(lseek(fd, 0, SEEK_END));
	SHOW(pread(fd, b, 5, 6));        printf("   b=\"%.5s\"\n", b);
	SHOW(lseek(fd, size - 3, SEEK_SET));
	SHOW(write(fd, "ABCDEF", 6));    /* short write: 3 fit */
	SHOW(write(fd, "ABCDEF", 6));    /* full */
	SHOW(lseek(fd, size + 1, SEEK_SET));
	SHOW(read(fd, bad, 4));          /* bad pointer at EOF -> 0 */
	SHOW(lseek(fd, 0, SEEK_SET));
	SHOW(read(fd, bad, 4));          /* bad pointer with data */
	close(fd);
	return 0;
}
```

`char *volatile bad`인 이유: 처음에 `read(fd, (char *)1, 4)`라고 썼더니 glibc `_FORTIFY_SOURCE`가 "주소 0 근처에 4바이트 쓰기"를 컴파일 시점에 알아채고 `-Wstringop-overflow` 경고 4개를 냈다. 경고 0개 규칙 때문에 컴파일러가 값을 추적하지 못하게 `volatile`로 막았다.

### 5.3 실행

```text
== shell: echo / cat / dd
imu0 accel x=12 y=-3 z=981
imu0 acc
== s2_rw contract test
write(fd, "hello world\n", 12)               -> 12
lseek(fd, 0, SEEK_SET)                       -> 0
read(fd, b, 5)                               -> 5
   b="hello"
read(fd, b, 5)                               -> 5
   b=" worl"
read(fd, b, 5)                               -> 2
read(fd, b, 5)                               -> 0
lseek(fd, 0, SEEK_END)                       -> 12
pread(fd, b, 5, 6)                           -> 5
   b="world"
lseek(fd, size - 3, SEEK_SET)                -> 4093
write(fd, "ABCDEF", 6)                       -> 3
write(fd, "ABCDEF", 6)                       -> -1 No space left on device
lseek(fd, size + 1, SEEK_SET)                -> -1 Invalid argument
read(fd, bad, 4)                             -> 0
lseek(fd, 0, SEEK_SET)                       -> 0
read(fd, bad, 4)                             -> -1 Bad address
```

출력에서 볼 것: 5·5·2·0 순서가 "short read 후 EOF"다. `cat`은 0이 올 때까지 읽으므로 정확히 한 번 출력하고 끝났다. 나쁜 포인터는 **데이터가 없으면 0**(복사를 시도하지 않음), 데이터가 있으면 `EFAULT`다 — 커널이 Oops 내지 않고 예외 테이블로 복구한 것(E6 §4). `buf_size - 3` 위치의 6바이트 쓰기는 3바이트만 들어갔다(short write). 셸의 `>` 리다이렉션은 short write를 보면 나머지를 다시 쓰므로 계약을 지키면 셸 도구가 알아서 동작한다.

> 체크포인트: `echo ... > /dev/s2chr0; cat /dev/s2chr0`가 같은 문자열을 한 번만 출력한다. `git tag m4`.

### 5.4 FAIL-3/4/5 재현 — read 하나에서 나는 세 가지 사고

무엇을 확인하나: 면접관이 기다리는 read 실수 세 개를 실제로 만든다. 각 패치는 한두 줄이다.

```text
# bugs/m4-no-ppos.patch
-	*ppos += n;
+	/* BUG: forgot *ppos += n; */
# bugs/m4-return-zero.patch
-	if (copy_to_user(ubuf, d->buf + *ppos, n))
+	ret = copy_to_user(ubuf, d->buf + *ppos, n);
+	if (ret)
 		return -EFAULT;
 ...
-	return n;
+	return ret;	/* BUG: returns copy_to_user() result (0) */
# bugs/m4-wrong-len.patch
-	n = min_t(size_t, count, d->len - *ppos);	/* short read is fine */
+	n = count;	/* BUG: trusts the user length */
```

```text
== FAIL: s2chr_m4-no-ppos.ko
Terminated
timeout 2 cat -> bytes: 85164
== FAIL: s2chr_m4-return-zero.ko
cat -> bytes: 0
== FAIL: s2chr_m4-wrong-len.ko
00000000  68 65 6c 6c 6f 0a 00 00  00 00 00 00 00 00 00 00  |hello...........|
00000010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000040  78 5d ba fd aa b4 ff ff  88 42 18 05 00 00 ff ff  |x].......B......|
00000050  88 42 18 05 00 00 ff ff  50 01 29 a1 aa b4 ff ff  |.B......P.).....|
00000060  00 00 00 00 00 00 00 00  f0 be 69 fd aa b4 ff ff  |..........i.....|
00000070  48 0d 0d 05 00 00 ff ff  01 00 00 00 03 00 00 00  |H...............|
00000080  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
```

출력에서 볼 것 (세 경우 모두 `buf_size=64`, 내용은 `hello\n` 6바이트):

- **`*ppos` 누락**: read가 매번 같은 6바이트를 돌려주고 0(EOF)을 영영 안 준다. `cat`은 끝나지 않고 2초 동안 85164바이트를 뱉었다(`timeout`이 죽임). 실제 장비에서는 "`cat /dev/xxx`가 멈추지 않는다", 로그 수집 스크립트가 디스크를 채운다로 나타난다.
- **반환값 0**: `copy_to_user`의 결과(성공 = 0)를 그대로 돌려줬다. 데이터는 이미 복사됐는데 커널은 "EOF"라고 말한 셈이라 `cat`이 0바이트를 출력했다.
- **길이를 믿음**: `dd bs=256`이 256바이트를 요청하자 64바이트짜리 `kmalloc` 객체 뒤의 **이웃 slab 객체**가 그대로 나왔다. 0x40 이후의 `ff ff b4 aa ...`는 little-endian으로 읽으면 `0xffffb4aa...` 꼴의 커널 가상 주소다 — KASLR을 깨는 정보 누출이다. 이 커널은 `CONFIG_HARDENED_USERCOPY`가 꺼져 있어 조용히 통과했다. 켜진 커널(배포판 대부분)이라면 `usercopy: Kernel memory exposure attempt detected`로 BUG가 났을 것이고, `Image-kasan`이라면 KASAN이 slab-out-of-bounds로 잡는다(M8 노트).

---

## 6. M5 — ioctl: `_IOR`/`_IOW` 번호와 공유 UAPI 헤더

### 6.1 UAPI 헤더

무엇을 확인하나: ioctl 번호 안에 **방향·타입·번호·인자 크기**가 들어간다는 것, 그리고 이 헤더가 사용자 프로그램과 커널이 함께 include하는 ABI라는 것(E6 §6, G5 §2).

```c
/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* include/s2chr_ioctl.h — shared by the driver and user tools. Never reorder. */
#ifndef _UAPI_S2CHR_IOCTL_H
#define _UAPI_S2CHR_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define S2CHR_ABI_VERSION	1
#define S2CHR_IOC_MAGIC		'S'	/* check ioctl-number.rst before upstreaming */

struct s2chr_info {
	__u32 version;		/* S2CHR_ABI_VERSION */
	__u32 buf_size;		/* bytes */
	__u32 len;		/* valid bytes (mem) or queued bytes (fifo) */
	__u32 open_count;	/* current openers */
	__u32 period_ms;	/* producer period, 0 = off */
	__u32 reserved[3];	/* zero; room to grow without a new number */
};

#define S2CHR_IOC_GET_INFO	_IOR(S2CHR_IOC_MAGIC, 1, struct s2chr_info)
#define S2CHR_IOC_CLEAR		_IO(S2CHR_IOC_MAGIC, 2)
#define S2CHR_IOC_SET_PERIOD	_IOW(S2CHR_IOC_MAGIC, 3, __u32)

#endif
```

고정 폭 `__u32`만 쓰고 포인터·`long`을 넣지 않았다 → 32비트 사용자 프로그램과 구조체 배치가 같으므로 `.compat_ioctl = compat_ptr_ioctl` 한 줄로 끝난다(G5 §3.1). `period_ms`는 M6의 타이머가 쓴다(지금은 저장만).

### 6.2 드라이버 쪽 `s2_ioctl`

```c
static long s2_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct s2_dev *d = filp->private_data;
	void __user *uarg = (void __user *)arg;
	struct s2chr_info info;
	u32 val;

	switch (cmd) {
	case S2CHR_IOC_GET_INFO:
		memset(&info, 0, sizeof(info));	/* no stack bytes leak out */
		info.version = S2CHR_ABI_VERSION;
		info.buf_size = buf_size;
		info.len = d->len;
		info.period_ms = d->period_ms;
		return copy_to_user(uarg, &info, sizeof(info)) ? -EFAULT : 0;
	case S2CHR_IOC_CLEAR:
		if (!(filp->f_mode & FMODE_WRITE))
			return -EBADF;
		memset(d->buf, 0, buf_size);
		d->len = 0;
		return 0;
	case S2CHR_IOC_SET_PERIOD:
		if (get_user(val, (u32 __user *)uarg))
			return -EFAULT;
		if (val > S2_PERIOD_MAX)
			return -EINVAL;
		d->period_ms = val;
		return 0;
	default:
		return -ENOTTY;			/* "not a typewriter" = unknown cmd */
	}
}
/* s2_fops 에 추가:  .unlocked_ioctl = s2_ioctl, .compat_ioctl = compat_ptr_ioctl, */
```

### 6.3 사용자 도구 `s2_ioctl`과 실행

```c
/* tools/s2_ioctl.c (요지) — 번호를 풀어 보고, 각 명령의 errno를 확인 */
struct s2chr_info_v0 { __u32 version, buf_size, len; };	/* 옛 헤더를 흉내 */
#define S2CHR_IOC_GET_INFO_V0 _IOR(S2CHR_IOC_MAGIC, 1, struct s2chr_info_v0)

static void decode(const char *name, unsigned long nr)
{
	printf("%-22s 0x%08lx dir=%lu type='%c' nr=%lu size=%lu\n", name, nr,
	       _IOC_DIR(nr), (int)_IOC_TYPE(nr), _IOC_NR(nr), _IOC_SIZE(nr));
}
/* main: decode() 4번 → SET_PERIOD 250 / 99999 → GET_INFO → CLEAR(RO fd, RW fd)
 *       → GET_INFO(bad ptr 8) → GET_INFO_V0 → TCGETS(0x5401), 각 결과를 rc()로 출력 */
```

```text
S2CHR_IOC_GET_INFO     0x80205301 dir=2 type='S' nr=1 size=32
S2CHR_IOC_CLEAR        0x00005302 dir=0 type='S' nr=2 size=0
S2CHR_IOC_SET_PERIOD   0x40045303 dir=1 type='S' nr=3 size=4
S2CHR_IOC_GET_INFO_V0  0x800c5301 dir=2 type='S' nr=1 size=12
SET_PERIOD 250                     -> 0
SET_PERIOD 99999                   -> -1 Invalid argument
GET_INFO                           -> 0
   version=1 buf_size=4096 len=21 period_ms=250
   open_count=0 reserved=00000000 00000000 00000000
CLEAR on O_RDONLY fd               -> -1 Bad file descriptor
CLEAR on O_RDWR fd                 -> 0
GET_INFO with bad pointer          -> -1 Bad address
old-header GET_INFO (size 12)      -> -1 Inappropriate ioctl for device
TCGETS (a tty ioctl)               -> -1 Inappropriate ioctl for device
after CLEAR: cat -> 0 bytes
```

출력에서 볼 것:

- `0x80205301` = dir 2(`_IOC_READ`, 커널→유저) · size 0x20(32바이트) · type `'S'`(0x53) · nr 1. 비트 배치는 E6 §6.1.
- **옛 헤더로 컴파일된 앱은 `ENOTTY`를 받는다**: 구조체가 12바이트였던 시절의 번호는 `0x800c5301`이라 `switch`의 어느 case와도 맞지 않는다. 크기가 번호에 들어가는 덕분에 "몰래 다른 배치로 복사"하는 사고 대신 명확한 거절이 된다. 확장할 여지는 `reserved[]`와 `version`으로 남겼다(G5 §2.3의 `copy_struct_from_user` 패턴이 더 정교한 방법).
- 모르는 명령(TCGETS)도 `-ENOTTY`("Inappropriate ioctl for device")다. `-EINVAL`을 돌려주면 `isatty()` 같은 라이브러리 판단이 틀어진다.
- 읽기 전용 fd로 상태를 바꾸는 CLEAR는 `-EBADF`로 막았다. 권한이 필요한 명령이면 `capable(CAP_SYS_ADMIN)` → `-EPERM`(G5 §3.2).

> 체크포인트: 네 번호가 위와 같고, 옛 헤더와 TCGETS가 둘 다 `ENOTTY`. `git tag m5`.

### 6.4 FAIL-6 재현 — GET_INFO에서 `memset`을 빼면 (그리고 왜 처음엔 재현이 안 됐나)

무엇을 확인하나: 구조체를 스택에 만들어 일부 필드만 채우고 통째로 `copy_to_user`하면 나머지 필드에 **이전 함수 호출이 남긴 스택 바이트**가 나간다. `bugs/m5-no-memset.patch`는 `memset` 한 줄을 지운다.

처음 빌드에서는 재현되지 않았다: `open_count=0 reserved=00000000 00000000 00000000`. 이 커널이 `CONFIG_INIT_STACK_ALL_ZERO=y`(GCC `-ftrivial-auto-var-init=zero`)라서 컴파일러가 모든 지역 변수를 0으로 채워 주기 때문이다. 이 설정이 없는 커널(오래된 BSP 커널 다수)을 흉내 내려고 망가진 빌드에만 플래그를 덮어썼다.

```sh
BUG_KCFLAGS=-ftrivial-auto-var-init=uninitialized sh scripts/build-bug.sh bugs/m5-no-memset.patch
```

```text
== FAIL: GET_INFO without memset (built with auto-var-init disabled)
   version=1 buf_size=4096 len=0 period_ms=250
   open_count=4294934528 reserved=2664dec8 804fbdf0 ffff8000
```

출력에서 볼 것: 채우지 않은 `open_count`와 `reserved[]`에 쓰레기가 나왔고, `804fbdf0 ffff8000`을 붙이면 `0xffff8000804fbdf0` — 커널 스택 주소(arm64 vmalloc 영역)다. 교훈은 두 개다. (1) 유저로 나가는 구조체는 `memset` 또는 `= {}` 초기화 후 채운다. (2) **"내 커널에서 안 터진다"는 증거가 아니다** — 하드닝 옵션이 버그를 가렸을 뿐이다.

---

## 7. M6 — `/dev/s2chr1`: blocking read + `poll`, wait queue, 타이머 생산자

### 7.1 설계 — 왜 minor를 하나 더 만드나

M4의 `/dev/s2chr0`은 파일처럼 seek하는 버퍼라 "기다린다"는 개념이 없다. 센서 데이터처럼 **흘러가는 stream**은 다른 계약이 필요하다: 데이터가 없으면 잠들고(blocking), `O_NONBLOCK`이면 `-EAGAIN`, 시그널이 오면 `-ERESTARTSYS`, `poll`로 "읽을 게 생겼나"를 물을 수 있어야 한다(F3 §2, G6 §1). 그래서 minor 1에 **cdev를 따로** 붙이고 `file_operations`도 따로 줬다. 생산자는 `timer_list`가 맡는다 — 실제 드라이버의 "IMU FIFO watermark 인터럽트"를 흉내 낸 것이다.

| 항목 | minor 0 `s2chr0` (mem) | minor 1 `s2chr1` (fifo) |
|---|---|---|
| 데이터 모델 | 파일처럼 오프셋으로 접근 | 한 번 읽으면 사라지는 바이트 stream |
| `f_pos` | 사용(`llseek` 지원) | `stream_open()`으로 끔 |
| 데이터 없을 때 | read는 0(EOF) | 잠든다 / `-EAGAIN` |
| 보호 | M7에서 mutex | spinlock(타이머는 softirq라 잠들 수 없다) |
| 생산자 | 유저의 write | 타이머 + 유저의 write |

### 7.2 코드 (1/2) — 생산자: 타이머와 `s2_push`

```c
/* struct s2_dev 에 추가 */
	struct cdev fifo_cdev;		/* minor 1 -> s2_fifo_fops */
	struct device *dev[S2_NR_MINORS];	/* -> /dev/s2chr0, /dev/s2chr1 */
	u32 seq;			/* tick counter */
	DECLARE_KFIFO(fifo, char, S2_FIFO_SIZE);
	spinlock_t fifo_lock;		/* timer (softirq) vs read/write */
	wait_queue_head_t rq;		/* readers sleep here */
	struct timer_list timer;	/* fake "sensor IRQ" */

static void s2_push(struct s2_dev *d, const char *p, unsigned int n)
{
	kfifo_in_spinlocked(&d->fifo, p, n, &d->fifo_lock);	/* drops if full */
	wake_up_interruptible(&d->rq);
}

static void s2_tick(struct timer_list *t)		/* softirq context */
{
	struct s2_dev *d = timer_container_of(d, t, timer);
	u32 period = READ_ONCE(d->period_ms);
	char line[48];
	int n;

	n = scnprintf(line, sizeof(line), "tick %u t_us=%llu\n", ++d->seq,
		      ktime_get_ns() / 1000);
	s2_push(d, line, n);
	if (period)
		mod_timer(&d->timer, jiffies + msecs_to_jiffies(period));
}

/* SET_PERIOD ioctl: 저장 + 타이머 무장/해제 */
		WRITE_ONCE(d->period_ms, val);
		if (val)
			mod_timer(&d->timer, jiffies + msecs_to_jiffies(val));
		else
			timer_delete_sync(&d->timer);
```

6.18의 `include/linux/timer.h`에는 옛 이름 `from_timer()`와 `del_timer_sync()`가 더 이상 없다(grep으로 확인). 각각 `timer_container_of()`, `timer_delete_sync()`로 바뀌었으므로 블로그 예제의 옛 이름은 이 커널에서 컴파일되지 않는다. exit에서는 `timer_shutdown_sync()`를 쓴다 — 지우는 동시에 "다시는 무장되지 않음"을 보장한다(F8).

### 7.3 코드 (2/2) — 소비자: blocking read와 `.poll`

```c
static int s2_fifo_open(struct inode *inode, struct file *filp)
{
	filp->private_data = container_of(inode->i_cdev, struct s2_dev, fifo_cdev);
	return stream_open(inode, filp);		/* no f_pos, no lseek */
}

static ssize_t s2_fifo_read(struct file *filp, char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	struct s2_dev *d = filp->private_data;
	char tmp[256];
	unsigned int n;
	int ret;

	count = min(count, sizeof(tmp));
	while (!(n = kfifo_out_spinlocked(&d->fifo, tmp, count, &d->fifo_lock))) {
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(d->rq, !kfifo_is_empty(&d->fifo));
		if (ret)
			return ret;			/* -ERESTARTSYS */
	}
	if (copy_to_user(ubuf, tmp, n))			/* never under the spinlock */
		return -EFAULT;
	return n;
}

static __poll_t s2_fifo_poll(struct file *filp, poll_table *wait)
{
	struct s2_dev *d = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &d->rq, wait);			/* 1) register */
	if (!kfifo_is_empty(&d->fifo))			/* 2) report */
		mask |= EPOLLIN | EPOLLRDNORM;
	if (!kfifo_is_full(&d->fifo))
		mask |= EPOLLOUT | EPOLLWRNORM;
	return mask;
}
/* s2_fifo_write: copy_from_user(tmp) → kfifo_avail 부족하면 -ENOSPC → s2_push() */
/* init: INIT_KFIFO, spin_lock_init, init_waitqueue_head, timer_setup(.., s2_tick, 0),
 *       cdev_add(&fifo_cdev, devt + 1, 1), device_create(.."s2chr%d", i) 두 번 */
```

핵심 규칙 세 개:

- `copy_to_user`는 page fault로 **잠들 수 있다**. 그래서 spinlock 안에서 부르지 않는다: 락 안에서는 `kfifo_out_spinlocked`로 스택 버퍼에 꺼내기만 하고, 락을 푼 뒤 복사한다(F2, F4 §3.4). 대가: 복사가 `EFAULT`로 실패하면 꺼낸 데이터는 사라진다. 사용자 포인터를 먼저 검사하고 싶으면 `kfifo_to_user`를 mutex 아래에서 쓰는 설계로 바꾼다.
- `wait_event_interruptible`은 **조건을 다시 검사**하고 잔다. 조건과 깨우기 사이의 lost-wakeup 경쟁을 매크로가 막아 준다(F3 §5). 깨어났다고 데이터가 있다는 보장은 없으므로 `while`로 다시 꺼낸다.
- `.poll`은 잠들지 않는다. "이 wait queue에 나를 등록해 두라"(poll_wait)와 "지금 상태"(mask) 두 가지만 한다(G6 §1.3).

### 7.4 시퀀스 그림 — 잠든 리더가 깨어나는 순서

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="120" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="80" y="42" text-anchor="middle" font-size="13">user (cat)</text>
  <rect x="180" y="20" width="140" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="42" text-anchor="middle" font-size="13">s2_fifo_read</text>
  <rect x="360" y="20" width="120" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="420" y="42" text-anchor="middle" font-size="13">wait queue rq</text>
  <rect x="510" y="20" width="150" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="585" y="42" text-anchor="middle" font-size="13">timer / writer</text>
  <line x1="80" y1="54" x2="80" y2="350" stroke="currentColor" stroke-dasharray="3 4"/>
  <line x1="250" y1="54" x2="250" y2="350" stroke="currentColor" stroke-dasharray="3 4"/>
  <line x1="420" y1="54" x2="420" y2="350" stroke="currentColor" stroke-dasharray="3 4"/>
  <line x1="585" y1="54" x2="585" y2="350" stroke="currentColor" stroke-dasharray="3 4"/>
  <line x1="80" y1="80" x2="248" y2="80" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="165" y="74" text-anchor="middle" font-size="12">read(fd)</text>
  <text x="258" y="106" font-size="12">kfifo 비어 있음</text>
  <line x1="250" y1="130" x2="418" y2="130" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="335" y="124" text-anchor="middle" font-size="12">enqueue, sleep</text>
  <rect x="244" y="138" width="12" height="96" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="140" y="190" text-anchor="middle" font-size="12">S 상태 (잠)</text>
  <text x="140" y="208" text-anchor="middle" font-size="12">wchan=s2_fifo_read</text>
  <text x="438" y="166" font-size="12">kfifo_in (spinlock)</text>
  <line x1="585" y1="196" x2="422" y2="196" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="503" y="190" text-anchor="middle" font-size="12">wake_up()</text>
  <line x1="420" y1="236" x2="252" y2="236" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="335" y="230" text-anchor="middle" font-size="12">runnable, 조건 재검사</text>
  <text x="258" y="266" font-size="12">kfifo_out → 락 해제</text>
  <text x="258" y="286" font-size="12">copy_to_user</text>
  <line x1="248" y1="314" x2="82" y2="314" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="165" y="308" text-anchor="middle" font-size="12">return n</text>
  <text x="20" y="372" font-size="12">시그널이 먼저 오면: -ERESTARTSYS → 유저는 EINTR (SA_RESTART면 재시작)</text>
</svg>
```
그림 3 — blocking read 시퀀스. 리더는 큐에 자신을 걸고 `TASK_INTERRUPTIBLE`로 잔다(그동안 CPU를 쓰지 않는다). 생산자는 데이터를 넣고 `wake_up_interruptible`을 부른다. 깨어난 리더는 조건을 다시 보고 꺼낸다. 펌웨어로 말하면 "RTOS 세마포어 pend/post"이고, 차이는 시그널로 잠을 깰 수 있다는 점이다.

### 7.5 테스트 도구 `s2_poll` (두 블록)

```c
/* tools/s2_poll.c (1/2) — non-blocking, poll(), blocking read, EINTR (M6) */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "s2chr_ioctl.h"

static double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void on_alarm(int sig) { (void)sig; }
```

```c
/* tools/s2_poll.c (2/2) — main() */
	int fd = open(path, O_RDWR | O_NONBLOCK), i, r;	/* (선언부 일부 생략) */

	if (fd < 0) { perror("open"); exit(1); }
	n = read(fd, buf, sizeof(buf));
	printf("1) empty + O_NONBLOCK read -> %zd (%s)\n", n, strerror(errno));

	ioctl(fd, S2CHR_IOC_SET_PERIOD, &period);			/* 100 ms */
	pfd = (struct pollfd){ .fd = fd, .events = POLLIN };
	for (i = 0, t0 = now_ms(); i < 4; i++) {
		r = poll(&pfd, 1, 2000);
		n = read(fd, buf, sizeof(buf) - 1);
		buf[n > 0 ? n : 0] = 0;
		printf("2) poll=%d revents=0x%x after %6.1f ms: %s", r, pfd.revents,
		       now_ms() - t0, buf);
	}
	ioctl(fd, S2CHR_IOC_SET_PERIOD, &off);
	while (read(fd, buf, sizeof(buf)) > 0)
		;					/* drain */
	r = poll(&pfd, 1, 300);
	printf("3) producer off: poll(300 ms) -> %d (timeout)\n", r);

	fcntl(fd, F_SETFL, 0);				/* now blocking */
	if (fork() == 0) {				/* writer process */
		usleep(200 * 1000);
		if (write(fd, "hello from writer\n", 18) < 0) perror("write");
		_exit(0);
	}
	t0 = now_ms();
	n = read(fd, buf, sizeof(buf) - 1);
	buf[n > 0 ? n : 0] = 0;
	printf("4) blocking read woke after %.1f ms: %s", now_ms() - t0, buf);
	wait(NULL);

	sigaction(SIGALRM, &sa, NULL);			/* sa_flags = 0: no SA_RESTART */
	alarm(1);
	t0 = now_ms();
	n = read(fd, buf, sizeof(buf));
	printf("5) blocking read + SIGALRM -> %zd (%s) after %.0f ms\n", n,
	       strerror(errno), now_ms() - t0);
	close(fd);
	return 0;
}
```

### 7.6 실행

```text
crw-------    1 0        0         510,   0 Oct 18 23:41 /dev/s2chr0
crw-------    1 0        0         510,   1 Oct 18 23:41 /dev/s2chr1
== s2_poll
1) empty + O_NONBLOCK read -> -1 (Resource temporarily unavailable)
2) poll=1 revents=0x1 after  102.9 ms: tick 1 t_us=4864681
2) poll=1 revents=0x1 after  205.3 ms: tick 2 t_us=4968253
2) poll=1 revents=0x1 after  309.5 ms: tick 3 t_us=5072365
2) poll=1 revents=0x1 after  414.2 ms: tick 4 t_us=5177234
3) producer off: poll(300 ms) -> 0 (timeout)
4) blocking read woke after 210.2 ms: hello from writer
5) blocking read + SIGALRM -> -1 (Interrupted system call) after 1002 ms
== a sleeping reader seen from /proc
state: State:	S (sleeping)  wchan: s2_fifo_read
[<0>] s2_fifo_read+0xdc/0x1d8 [s2chr]
[<0>] vfs_read+0xcc/0x350
[<0>] ksys_read+0xec/0x110
[<0>] __arm64_sys_read+0x20/0x40
[<0>] invoke_syscall.constprop.0+0x48/0xf0
[<0>] el0_svc_common.constprop.0+0x40/0xf0
reader got: imu0 sample
== rmmod while the producer timer runs
rmmod ok: [    9.149818][   T90] s2chr: [s2] bye
```

출력에서 볼 것:

- 1) → 5)가 blocking 계약 다섯 가지를 모두 보여 준다: `EAGAIN`, poll로 깨어남(`revents=0x1`=POLLIN), 생산자가 없으면 poll 타임아웃(0), 다른 프로세스의 write가 잠든 read를 210 ms 뒤 깨움(writer가 200 ms 잤다), 시그널이 오면 `-ERESTARTSYS`가 `EINTR`로 바뀌어 나옴(`SA_RESTART`가 없으니까, E6 §5.4).
- 간격이 100이 아니라 약 102~105 ms이고 조금씩 밀린다. `HZ=250`이라 jiffy가 4 ms이고, 콜백 안에서 "지금 jiffies + 25"로 다시 무장하므로 콜백이 늦게 돈 만큼 매번 누적된다. 드리프트 없는 주기가 필요하면 `d->timer.expires + period`로 무장하거나 hrtimer를 쓴다(§13 확장 과제). TCG 수치라 실제 보드와는 다르다.
- `/proc/<pid>/wchan`과 `/proc/<pid>/stack`이 "어디서 자고 있나"를 정확히 보여 준다: `s2_fifo_read` ← `vfs_read` ← `ksys_read` ← syscall 진입(E6 §2). 현장에서 "앱이 멈췄다"를 분류하는 첫 명령이다(F3 §3).

> 체크포인트: `/dev/s2chr1`이 생기고, 1)~5)가 위와 같은 의미로 나오고, 타이머가 도는 중에 rmmod해도 커널이 살아 있다. `git tag m6`.

### 7.7 FAIL-7 재현 — 생산자가 `wake_up`을 잊으면

```text
 	kfifo_in_spinlocked(&d->fifo, p, n, &d->fifo_lock);	/* drops if full */
-	wake_up_interruptible(&d->rq);
+	/* BUG: forgot wake_up_interruptible(&d->rq); */
```

```text
== FAIL: producer forgets wake_up
reader state: State:	S (sleeping)
queued in fifo while reader sleeps: len=12
Terminated
cat exit=143 got=''
```

출력에서 볼 것: 데이터 12바이트(`imu0 sample\n`)가 FIFO에 **들어 있는데** 리더는 계속 잔다. 3초 뒤 `timeout`의 SIGTERM으로 끝났다(143 = 128 + 15). 증상만 보면 "가끔 데이터가 늦게 온다/안 온다"이고, 다른 이벤트(다음 tick의 다른 경로, 시그널)가 우연히 깨워 주면 동작하는 것처럼 보여서 찾기 어렵다. 처음 실험에서는 GET_INFO를 `timeout`이 끝난 **뒤**에 불렀더니 `len=0`이었다: SIGTERM이 리더를 깨우자 `wait_event`가 조건을 다시 보고 데이터를 꺼내 갔고, 프로세스는 그것을 출력하기 전에 죽었다. 그래서 위 스크립트는 리더가 잠든 동안 측정하도록 고쳤다.

### 7.8 FAIL-8 재현 — exit에서 타이머를 멈추지 않으면 (커널 패닉)

```text
-	timer_shutdown_sync(&s2.timer);		/* no tick may run after this */
+	/* BUG: forgot timer_shutdown_sync(&s2.timer); */
```

```text
== FAIL: exit() forgets to stop the timer
rmmod exit=0
[   12.856498][    C0] Unable to handle kernel paging request at virtual address ffffc20c824bf1d8
[   12.856736][    C0]   ESR = 0x0000000096000047
[   12.856775][    C0]   EC = 0x25: DABT (current EL), IL = 32 bits
[   12.856838][    C0]   FSC = 0x07: level 3 translation fault
[   12.856902][    C0]   CM = 0, WnR = 1, TnD = 0, TagAccess = 0
[   12.858038][    C0] Internal error: Oops: 0000000096000047 [#1]  SMP
[   12.871253][    C0] Modules linked in: [last unloaded: s2chr(OE)]
[   12.872180][    C0] CPU: 0 UID: 0 PID: 0 Comm: swapper/0 Tainted: G           OE       6.18.55-trace #1 PREEMPT 
[   12.873331][    C0] pc : __run_timers+0x130/0x318
[   12.875978][    C0] lr : __run_timers+0x3c/0x318
...
 __run_timers+0x130/0x318 (P)
 timer_expire_remote+0x54/0x70
 tmigr_handle_remote_up+0x260/0x3c0
 ...
 run_timer_softirq+0x40/0x50
 handle_softirqs+0x194/0x470
 ...
 default_idle_call+0x48/0x148 (P)
 do_idle+0x28c/0x2f0
Kernel panic - not syncing: Oops: Fatal exception in interrupt
Rebooting in 1 seconds..
```

(Mem abort 상세와 레지스터 덤프, 스택 일부를 생략했다. 앞 실행에서 `C0]` 접두어를 지운 스택 줄을 합쳤다.)

출력에서 볼 것: **rmmod 자체는 성공**했다(`rmmod exit=0`). 문제는 그 뒤 250 ms 안에 일어났다. 해제된 `.bss`의 `struct s2_dev s2` 안에 있던 `timer_list`가 아직 타이머 휠에 걸려 있었고, softirq가 그 항목을 리스트에서 떼어 내려고 **쓰기**(WnR=1)를 하다 이미 unmap된 모듈 메모리(level 3 translation fault)를 건드렸다. `Comm: swapper/0`(idle 태스크)과 `Fatal exception in interrupt` — 어떤 프로세스와도 상관없는 곳에서 죽기 때문에 현장에서는 "가끔 idle에서 패닉"으로 보고된다. `[last unloaded: s2chr(OE)]`가 가장 중요한 단서다. 해독법은 M3 노트, 고치는 법은 exit에서 `timer_shutdown_sync`, workqueue라면 `cancel_work_sync`, IRQ라면 `free_irq` — **모든 비동기 실행 경로를 끄는 것이 exit의 첫 번째 일**이다.

---

## 8. M7 — 동시성: mutex, 여러 opener, 두 프로세스 테스트

### 8.1 코드 (M6에서 바뀐 부분)

무엇을 확인하나: mem 버퍼의 `buf`/`len`은 프로세스 컨텍스트에서만 만지므로 **잠들 수 있는 mutex**가 맞다(`copy_from_user`가 락 안에서 잠들 수 있어야 한다, F4 §5). `O_APPEND`는 "len을 읽고 → 그 위치에 쓰고 → len을 갱신"하는 read-modify-write라서 락 없이는 깨진다.

```c
/* struct s2_dev 에 추가 */
	struct mutex lock;		/* protects buf + len (process context only) */
	atomic_t open_count;		/* both minors */

static ssize_t s2_write(struct file *filp, const char __user *ubuf,
			size_t count, loff_t *ppos)
{
	struct s2_dev *d = filp->private_data;
	ssize_t ret;
	size_t n;

	if (mutex_lock_interruptible(&d->lock))
		return -ERESTARTSYS;
	if (filp->f_flags & O_APPEND)
		*ppos = d->len;			/* read-modify-write of len: needs lock */
	ret = -ENOSPC;
	if (*ppos >= buf_size)
		goto out;
	n = min_t(size_t, count, buf_size - *ppos);	/* short write */
	ret = -EFAULT;
	if (copy_from_user(d->buf + *ppos, ubuf, n))
		goto out;
	*ppos += n;
	d->len = max_t(size_t, d->len, *ppos);
	ret = n;
out:
	mutex_unlock(&d->lock);
	return ret;
}
/* s2_read: 같은 모양 (mutex_lock_interruptible → ... → out: mutex_unlock)
 * s2_open: O_TRUNC 처리를 mutex 안으로, atomic_inc(&d->open_count)
 * s2_release: atomic_dec,  ioctl CLEAR: mutex 안에서 memset
 * GET_INFO: info.open_count = atomic_read(&d->open_count)  */
```

함수 안에 return이 여러 개면 unlock을 빼먹기 쉬우므로 `ret`에 값을 넣고 `goto out` 한 곳에서 푸는 모양으로 바꿨다. `mutex_lock_interruptible`은 락을 기다리다 시그널을 받으면 `-EINTR`을 주는데, 드라이버는 이것을 `-ERESTARTSYS`로 바꿔 돌려준다(시스템 콜 재시작 규칙, F3 §4). VFS는 char device에 `O_APPEND`를 자동으로 해 주지 않는다 — 일반 파일에서는 `generic_write_checks()`가 하지만 우리 `.write`는 직접 처리해야 한다.

### 8.2 race 테스트 도구 `s2_race`

```c
/* tools/s2_race.c (요지) — N processes append fixed 16-byte records (M7) */
	for (p = 0; p < np; p++) {
		if (fork() == 0) {
			fd = open(path, O_WRONLY | O_APPEND);
			if (fd < 0) { perror("open"); _exit(1); }
			for (i = 0; i < nrec; i++) {
				snprintf(rec, sizeof(rec), "w%d:%012d\n", p, i);
				if (write(fd, rec, REC) != REC) { perror("write"); _exit(1); }
			}
			_exit(0);
		}
	}
	while (wait(NULL) > 0)
		;
	fd = open(path, O_RDONLY);
	if (fd < 0 || ioctl(fd, S2CHR_IOC_GET_INFO, &info)) { perror("info"); exit(1); }
	all = malloc(info.len + 1);
	n = read(fd, all, info.len);
	for (i = 0; i + REC <= n; i += REC) {	/* count well-formed records per writer */
		int w, k;

		if (sscanf(all + i, "w%d:%12d", &w, &k) == 2 && w >= 0 && w < np &&
		    all[i + REC - 1] == '\n') { seen[w]++; good++; } else bad++;
	}
	printf("procs=%d records/proc=%d expected_len=%d len=%u\n", np, nrec,
	       np * nrec * REC, info.len);
	printf("good=%d bad=%d lost=%d", good, bad, np * nrec - good);
```

### 8.3 실행 — mutex 있음 vs 없음 (`bugs/m7-no-lock.patch`)

게스트는 `-smp 2`, 모듈은 `buf_size=1048576`, 프로세스 2개 × 20000 레코드 × 16바이트 = 640000바이트가 기대값이다.

```text
== open_count with two other openers
   open_count=4 reserved=00000000 00000000 00000000
== race run 1 (mutex)
procs=2 records/proc=20000 expected_len=640000 len=640000
good=40000 bad=0 lost=0 w0=20000 w1=20000
real	0m 14.95s
== race run 2 (mutex)
procs=2 records/proc=20000 expected_len=640000 len=640000
good=40000 bad=0 lost=0 w0=20000 w1=20000
real	0m 14.93s
== race run 3 (mutex)
procs=2 records/proc=20000 expected_len=640000 len=640000
good=40000 bad=0 lost=0 w0=20000 w1=20000
real	0m 18.14s
== FAIL race run 1 (no lock)
procs=2 records/proc=20000 expected_len=640000 len=626848
good=39178 bad=0 lost=822 w0=19598 w1=19580
real	0m 17.69s
== FAIL race run 2 (no lock)
procs=2 records/proc=20000 expected_len=640000 len=623504
good=38969 bad=0 lost=1031 w0=19431 w1=19538
real	0m 17.82s
== FAIL race run 3 (no lock)
procs=2 records/proc=20000 expected_len=640000 len=622288
good=38893 bad=0 lost=1107 w0=19470 w1=19423
real	0m 16.75s
```

(`time`의 user/sys 줄은 생략했다.)

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
  <text x="320" y="22" text-anchor="middle" font-size="14">잃어버린 레코드 수 (2 프로세스 × 20000, -smp 2, TCG)</text>
  <line x1="70" y1="240" x2="610" y2="240" stroke="currentColor"/>
  <line x1="70" y1="40" x2="70" y2="240" stroke="currentColor"/>
  <text x="62" y="244" text-anchor="end" font-size="12">0</text>
  <text x="62" y="161" text-anchor="end" font-size="12">500</text>
  <text x="62" y="77" text-anchor="end" font-size="12">1000</text>
  <line x1="66" y1="157" x2="610" y2="157" stroke="#888" stroke-dasharray="2 4"/>
  <line x1="66" y1="73" x2="610" y2="73" stroke="#888" stroke-dasharray="2 4"/>
  <rect x="95" y="238" width="60" height="2" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="180" y="238" width="60" height="2" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="265" y="238" width="60" height="2" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="365" y="102" width="60" height="138" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <rect x="450" y="67" width="60" height="173" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <rect x="535" y="54" width="60" height="186" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="125" y="230" text-anchor="middle" font-size="12">0</text>
  <text x="210" y="230" text-anchor="middle" font-size="12">0</text>
  <text x="295" y="230" text-anchor="middle" font-size="12">0</text>
  <text x="395" y="96" text-anchor="middle" font-size="12">822</text>
  <text x="480" y="61" text-anchor="middle" font-size="12">1031</text>
  <text x="565" y="48" text-anchor="middle" font-size="12">1107</text>
  <text x="125" y="258" text-anchor="middle" font-size="12">run 1</text>
  <text x="210" y="258" text-anchor="middle" font-size="12">run 2</text>
  <text x="295" y="258" text-anchor="middle" font-size="12">run 3</text>
  <text x="395" y="258" text-anchor="middle" font-size="12">run 1</text>
  <text x="480" y="258" text-anchor="middle" font-size="12">run 2</text>
  <text x="565" y="258" text-anchor="middle" font-size="12">run 3</text>
  <text x="210" y="282" text-anchor="middle" font-size="13">mutex 있음</text>
  <text x="480" y="282" text-anchor="middle" font-size="13">락 없음 (m7-no-lock)</text>
</svg>
```
그림 4 — 같은 테스트를 세 번씩. 락이 있으면 매번 0, 없으면 매번 약 2~3%(822~1107개)를 잃었다. 막대 높이는 Python으로 `lost × 0.168 px`로 계산했다.

출력에서 볼 것:

- **`bad=0`인데 `lost>0`** — 레코드가 찢어지지 않고 통째로 사라졌다. 두 writer가 같은 `len`을 읽고 같은 위치에 각자 16바이트를 쓴 뒤 둘 다 `len += 16`을 했으니, 하나가 다른 하나를 정확히 덮어쓴 것이다(lost update). 펌웨어의 "ISR과 메인 루프가 같은 head 인덱스를 갱신"과 똑같다.
- 손실은 2~3%로 작고 실행마다 다르다. 실제 SoC에서는 더 드물게 날 수 있고, 그래서 "테스트에서 한 번도 안 났다"가 증거가 되지 못한다 — lockdep(`Image-dbg`)이나 KCSAN 같은 도구, 그리고 이런 스트레스 테스트를 CI에 넣는 이유다.
- 실행 시간 15~18 s는 TCG 에뮬레이션 비용이 지배한다. 락 유무로 시간이 거의 같다는 것도 이 환경에서는 의미 있는 비교가 아니다(실제 보드에서 다시 재야 한다).
- `open_count=4` = 백그라운드 `sleep` 두 개(각 minor) + `s2_ioctl`이 연 fd 두 개. 두 minor를 합산하는 카운터다.

> 체크포인트: mutex 빌드는 세 번 다 `lost=0`, 락 없는 빌드는 `lost>0`. `git tag m7`.

---

## 9. M8 — sysfs 속성, debugfs, dynamic debug, goto 사다리 증명, selftest

### 9.1 sysfs 속성 — "값 하나 = 파일 하나"

무엇을 확인하나: 사용자가 셸로 바꿀 손잡이(`period_ms`)와 읽기 전용 상태(`len`)를 sysfs 속성으로 낸다. `device_create_with_groups`로 넘기면 **uevent보다 먼저** 속성이 생긴다(G8 §8 — udev가 속성을 읽는 경쟁을 피함).

```c
static ssize_t period_ms_show(struct device *dev, struct device_attribute *a,
			      char *buf)
{
	struct s2_dev *d = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", READ_ONCE(d->period_ms));
}

static ssize_t period_ms_store(struct device *dev, struct device_attribute *a,
			       const char *buf, size_t count)
{
	u32 val;
	int ret = kstrtou32(buf, 0, &val);

	if (ret)
		return ret;
	ret = s2_set_period(dev_get_drvdata(dev), val);	/* ioctl과 같은 함수 */
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(period_ms);

static ssize_t len_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct s2_dev *d = dev_get_drvdata(dev);

	return sysfs_emit(buf, "mem=%zu fifo=%u\n", READ_ONCE(d->len),
			  kfifo_len(&d->fifo));
}
static DEVICE_ATTR_RO(len);

static struct attribute *s2_attrs[] = {
	&dev_attr_period_ms.attr,
	&dev_attr_len.attr,
	NULL,
};
ATTRIBUTE_GROUPS(s2);		/* -> s2_groups */

/* init: device_create_with_groups(s2.class, NULL, s2.devt + i, &s2, s2_groups, "s2chr%d", i) */
```

ioctl의 SET_PERIOD와 sysfs store가 같은 검증 함수 `s2_set_period()`를 부른다 — 인터페이스가 두 개여도 규칙은 하나여야 한다. `len_show`는 엄밀히는 "값 하나" 규칙을 어긴다(두 값). 학습용으로 둔 것이고, 업스트림이라면 `mem_len`과 `fifo_len`으로 나눈다(G8 §1.2).

### 9.2 debugfs — 약속 없는 개발자 창문

```c
static int s2_state_show(struct seq_file *m, void *unused)
{
	struct s2_dev *d = m->private;

	seq_printf(m, "devt=%d:%d buf_size=%u len=%zu\n", MAJOR(d->devt),
		   MINOR(d->devt), buf_size, READ_ONCE(d->len));
	seq_printf(m, "fifo=%u/%u period_ms=%u seq=%u openers=%d\n",
		   kfifo_len(&d->fifo), kfifo_size(&d->fifo),
		   READ_ONCE(d->period_ms), READ_ONCE(d->seq),
		   atomic_read(&d->open_count));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(s2_state);

static void s2_debugfs_init(struct s2_dev *d)	/* failures are not fatal */
{
	d->dbg = debugfs_create_dir("s2chr", NULL);
	debugfs_create_file("state", 0444, d->dbg, d, &s2_state_fops);
	debugfs_create_atomic_t("reads", 0444, d->dbg, &d->reads);
	debugfs_create_atomic_t("writes", 0444, d->dbg, &d->writes);
	debugfs_create_atomic_t("ticks", 0444, d->dbg, &d->ticks);
	debugfs_create_atomic_t("drops", 0644, d->dbg, &d->drops);
}
/* exit 첫 줄: debugfs_remove_recursive(s2.dbg); — 등록의 역순 */
```

debugfs 함수의 반환값은 **검사하지 않는 것이 커널 규칙**이다(debugfs가 없거나 실패해도 드라이버는 동작해야 하고, 실패한 dentry를 다음 호출에 넘겨도 안전하게 무시된다). `drops`만 0644인 것은 테스트 중 카운터를 0으로 되돌리기 위해서다.

### 9.3 dynamic debug — 재빌드 없이 디버그 줄 켜기

M3에서 넣은 `pr_info` open/release 로그를 `dev_dbg`로 바꿨다. `dev_dbg`는 장치 이름을 자동으로 붙이고, `CONFIG_DYNAMIC_DEBUG=y` 커널에서는 **기본 꺼짐 + 런타임 스위치**다(M1 노트 §4).

```c
	dev_dbg(d->dev[0], "open by %s(%d) flags=0x%x\n", current->comm,
		task_pid_nr(current), filp->f_flags);			/* s2_open */
	dev_dbg(d->dev[iminor(inode) - MINOR(d->devt)], "release\n");	/* s2_release */
	dev_dbg(d->dev[1], "fifo read %u bytes, %u left\n", n, kfifo_len(&d->fifo));
```

### 9.4 goto 사다리를 "증명"하기 — `fail_at` 결함 주입

무엇을 확인하나: init의 7단계 각각에서 실패했을 때 그 앞 단계까지만 정확히 거꾸로 풀리는지. 손으로 리뷰하는 대신 **모듈 파라미터로 실패를 주입**하고 남은 흔적을 센다.

```c
static int fail_at;				/* test hook: 0 = never */
module_param(fail_at, int, 0444);
MODULE_PARM_DESC(fail_at, "inject -ENOMEM at init step N (1..7)");

static int s2_inject(int step)
{
	if (fail_at != step)
		return 0;
	pr_warn("fault injected at init step %d\n", step);
	return -ENOMEM;
}

	s2.buf = s2_inject(1) ? NULL : kzalloc(buf_size, GFP_KERNEL);
	if (!s2.buf)
		return -ENOMEM;
	ret = s2_inject(2) ?: alloc_chrdev_region(&s2.devt, 0, S2_NR_MINORS, "s2chr");
	if (ret)
		goto err_buf;
	cdev_init(&s2.cdev, &s2_fops);
	ret = s2_inject(3) ?: cdev_add(&s2.cdev, s2.devt, 1);
	if (ret)
		goto err_region;
	cdev_init(&s2.fifo_cdev, &s2_fifo_fops);
	ret = s2_inject(4) ?: cdev_add(&s2.fifo_cdev, s2.devt + 1, 1);
	if (ret)
		goto err_cdev;
	s2.class = s2_inject(5) ? ERR_PTR(-ENOMEM) : class_create("s2chr");
	if (IS_ERR(s2.class)) {
		ret = PTR_ERR(s2.class);
		goto err_fifo_cdev;
	}
	for (i = 0; i < S2_NR_MINORS; i++) {
		s2.dev[i] = s2_inject(6 + i) ? ERR_PTR(-ENOMEM) :
			device_create_with_groups(s2.class, NULL, s2.devt + i,
						  &s2, s2_groups, "s2chr%d", i);
		if (IS_ERR(s2.dev[i])) {
			ret = PTR_ERR(s2.dev[i]);
			goto err_devices;
		}
	}
	s2_debugfs_init(&s2);
	return 0;

err_devices:
	while (--i >= 0)
		device_destroy(s2.class, s2.devt + i);
	class_destroy(s2.class);
err_fifo_cdev:
	cdev_del(&s2.fifo_cdev);
err_cdev:
	cdev_del(&s2.cdev);
err_region:
	unregister_chrdev_region(s2.devt, S2_NR_MINORS);
err_buf:
	kfree(s2.buf);
	return ret;
```

`a ?: b`는 GCC 확장(커널 전체에서 쓰는 관용구, E5)으로 "a가 0이 아니면 a, 아니면 b"다. 실제 함수 호출 앞에 주입을 끼워 넣어 원래 코드 모양을 거의 바꾸지 않았다. 커널에는 범용 결함 주입(`CONFIG_FAULT_INJECTION`, `failslab`, J 모듈에서 사용)도 있지만, 단계 번호로 정확히 찌르는 데는 이 방식이 단순하다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="155" y="22" text-anchor="middle" font-size="14">init 순서 (위 → 아래)</text>
  <text x="525" y="22" text-anchor="middle" font-size="14">실패 시 정리 (아래 → 위)</text>
  <rect x="30" y="36" width="250" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="40" y="56" font-size="12">1 kzalloc(buf)</text>
  <rect x="30" y="74" width="250" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="40" y="94" font-size="12">2 alloc_chrdev_region</text>
  <rect x="30" y="112" width="250" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="40" y="132" font-size="12">3 cdev_add(mem)</text>
  <rect x="30" y="150" width="250" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="40" y="170" font-size="12">4 cdev_add(fifo)</text>
  <rect x="30" y="188" width="250" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="40" y="208" font-size="12">5 class_create</text>
  <rect x="30" y="226" width="250" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="40" y="246" font-size="12">6 device_create(s2chr0)</text>
  <rect x="30" y="264" width="250" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="40" y="284" font-size="12">7 device_create(s2chr1)</text>
  <text x="390" y="56" font-size="12">return -ENOMEM (정리할 것 없음)</text>
  <rect x="390" y="74" width="270" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="400" y="94" font-size="12">err_buf: kfree(buf)</text>
  <rect x="390" y="112" width="270" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="400" y="132" font-size="12">err_region: unregister region</text>
  <rect x="390" y="150" width="270" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="400" y="170" font-size="12">err_cdev: cdev_del(mem)</text>
  <rect x="390" y="188" width="270" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="400" y="208" font-size="12">err_fifo_cdev: cdev_del(fifo)</text>
  <rect x="390" y="245" width="270" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="400" y="265" font-size="12">err_devices: 장치·class 해제</text>
  <line x1="280" y1="51" x2="386" y2="51" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="280" y1="89" x2="388" y2="89" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="280" y1="127" x2="388" y2="127" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="280" y1="165" x2="388" y2="165" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="280" y1="203" x2="388" y2="203" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="280" y1="241" x2="388" y2="256" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="280" y1="279" x2="388" y2="264" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="525" y1="245" x2="525" y2="220" stroke="#3f9a6b" marker-end="url(#ah5)"/>
  <line x1="525" y1="188" x2="525" y2="182" stroke="#3f9a6b" marker-end="url(#ah5)"/>
  <line x1="525" y1="150" x2="525" y2="144" stroke="#3f9a6b" marker-end="url(#ah5)"/>
  <line x1="525" y1="112" x2="525" y2="106" stroke="#3f9a6b" marker-end="url(#ah5)"/>
  <text x="30" y="322" font-size="12">단계 k가 실패하면 k-1까지 만든 것만, 만든 순서의 반대로 푼다. 라벨은 위로 fall-through 한다.</text>
</svg>
```
그림 5 — goto 사다리. 왼쪽 단계가 실패하면 화살표의 라벨로 점프하고, 거기서부터 오른쪽 열을 위로 타고 올라가며(코드에서는 아래로 fall-through) 앞 단계를 푼다. exit 함수는 7단계 전부를 같은 역순으로 푼다(+ 맨 앞에 debugfs, 중간에 타이머).

### 9.5 실행 — 속성, debugfs, dynamic debug, 결함 주입

```text
== sysfs attributes (created before the uevent by device_create_with_groups)
dev        len        period_ms  power      subsystem  uevent
100
sh: write error: Invalid argument
store 99999 exit=1
mem=6 fifo=201
== debugfs
drops   reads   state   ticks   writes
devt=510:0 buf_size=4096 len=6
fifo=201/4096 period_ms=0 seq=10 openers=0
reads=0
writes=1
ticks=10
drops=0
== dynamic debug
s2chr.c:285 [s2chr]s2_fifo_read =_ "fifo read %u bytes, %u left\n"
s2chr.c:102 [s2chr]s2_open =_ "open by %s(%d) flags=0x%x\n"
s2chr.c:112 [s2chr]s2_release =_ "release\n"
tick 1 t_us=4900283
[    6.444816][   T89] s2_open:102: s2chr s2chr0: open by cat(89) flags=0x20000
[    6.445447][   T89] s2_release:112: s2chr s2chr0: release
[    6.479750][   T90] s2_fifo_read:285: s2chr s2chr1: fifo read 201 bytes, 0 left
[    6.481921][   T90] s2_release:112: s2chr s2chr1: release
[    6.677547][   T76] s2_release:112: s2chr s2chr1: release
== fault injection walks the goto ladder
fail_at=1 insmod=1 leftovers(devices,class+debugfs)=0 0
fail_at=2 insmod=1 leftovers(devices,class+debugfs)=0 0
fail_at=3 insmod=1 leftovers(devices,class+debugfs)=0 0
fail_at=4 insmod=1 leftovers(devices,class+debugfs)=0 0
fail_at=5 insmod=1 leftovers(devices,class+debugfs)=0 0
fail_at=6 insmod=1 leftovers(devices,class+debugfs)=0 0
fail_at=7 insmod=1 leftovers(devices,class+debugfs)=0 0
clean insmod after 7 failures: ok
```

출력에서 볼 것:

- 범위 밖 store는 우리 `-EINVAL`이 그대로 셸의 `write error: Invalid argument`가 됐다. 1초 동안 `period_ms=100`이 tick 10개(`seq=10`, 201바이트)를 만들었다.
- dynamic debug 제어 파일의 `=_`는 "꺼짐", 켠 뒤 `+pfl`의 f(함수 이름)·l(줄 번호)이 접두어 `s2_open:102:`로 붙었다. 끄는 것은 `-p`. 제품 빌드에 디버그 줄을 남겨 두고 현장에서 켜는 방식이다(M1 노트 §4.6).
- `flags=0x20000` = `O_LARGEFILE`(arm64 64비트 프로세스에서 VFS가 붙임). `O_RDONLY`는 0이다.
- 7단계 모두에서 insmod가 실패했고(1) **남은 것이 0**이었으며, 그 뒤 정상 insmod가 됐다. 이것이 "goto 사다리가 맞다"의 증거다. 리뷰어에게 "init의 모든 실패 경로를 테스트했다"고 말할 수 있다.

### 9.6 goto 사다리 vs `devm_*` — 왜 여기서는 goto인가

| 항목 | goto 사다리 (이 드라이버) | `devm_*` (platform/I2C/SPI 드라이버) |
|---|---|---|
| 해제 시점 | init 실패 시 직접, exit에서 직접 | probe 실패 시 또는 remove/unbind 후 devres가 역순으로 |
| 필요 조건 | 없음 (어떤 모듈이든) | **bus가 probe한 `struct device`**가 있어야 함 |
| 흔한 실수 | 라벨 순서 틀림, 하나 건너뜀 | devm 자원을 수동으로 또 해제(double free), IRQ가 다른 자원보다 늦게 풀려 해제된 데이터 접근 |
| 이 노트에서 | M8 그림 5, `fail_at` 7개 | G3 §(probe/remove), R3 §3.4 그림 |

`s2chr`은 bus에 붙은 장치가 아니라 "모듈 = 장치" 구조라서 devres를 매달 probe된 `struct device`가 없다. `device_create`가 만드는 device에 devm을 매다는 것은 가능은 하지만, 그 device는 우리가 `device_destroy`로 지우는 대상이라 순서가 뒤엉킨다. 실제 센서 드라이버(S4)는 platform/I2C driver로 쓰고 `devm_kzalloc`, `devm_request_irq`, `devm_regmap_init_i2c`, `misc`/`cdev`는 `devm_add_action_or_reset`로 묶는다 — 그러면 이 goto 사다리 대부분이 사라진다. **면접에서는 둘 다 말할 수 있어야 한다**: "모듈 init이면 goto, probe면 devm, 섞으면 순서를 반드시 그림으로 그려 본다."

### 9.7 selftest — kselftest 스타일 TAP 스크립트

무엇을 확인하나: 지금까지의 확인을 사람이 눈으로 보는 대신 기계가 판정하게 만든다. kselftest의 출력 형식(TAP 13: `ok N - 설명`, `not ok N`, 마지막 `# Totals`)을 따르므로 CI가 그대로 파싱할 수 있다. 이 커널은 `CONFIG_KUNIT`이 꺼져 있어(KUnit은 `Image-dom`에만 있다) 사용자 공간 end-to-end 테스트로 했다.

```sh
#!/bin/sh
# tests/s2chr_selftest.sh — kselftest-style (TAP 13). usage: s2chr_selftest.sh [ko]
KO=${1:-/s2/s2chr.ko}; BIN=$(dirname "$KO"); N=0; FAIL=0
ok()  { N=$((N+1)); echo "ok $N - $1"; }
nok() { N=$((N+1)); FAIL=$((FAIL+1)); echo "not ok $N - $1"; }
t()   { d=$1; shift; if eval "$@" >/dev/null 2>&1; then ok "$d"; else nok "$d"; fi; }
clean() { ! grep -q s2chr /proc/devices && [ ! -e /sys/class/s2chr ] &&
          [ ! -e /dev/s2chr0 ] && [ ! -e /sys/kernel/debug/s2chr ]; }

echo "TAP version 13"; echo "1..16"
t "insmod"                   insmod $KO buf_size=1048576
t "char nodes via devtmpfs"  "[ -c /dev/s2chr0 ] && [ -c /dev/s2chr1 ]"
t "write/read round trip"    "echo imu0 > /dev/s2chr0 && [ \"\$(cat /dev/s2chr0)\" = imu0 ]"
t "rw contract (short/ENOSPC/EFAULT)" \
  "$BIN/s2_rw /dev/s2chr0 1048576 > /tmp/rw.out && grep -q 'No space left' /tmp/rw.out && grep -q 'Bad address' /tmp/rw.out"
t "ioctl: unknown cmd -> ENOTTY" "$BIN/s2_ioctl | grep -q 'TCGETS.*Inappropriate ioctl'"
t "ioctl: old ABI size -> ENOTTY" "$BIN/s2_ioctl | grep -q 'old-header.*Inappropriate ioctl'"
t "poll + blocking read + EINTR" \
  "$BIN/s2_poll > /tmp/p.out && grep -q '^4) blocking read woke' /tmp/p.out && grep -q 'Interrupted' /tmp/p.out"
t "2-process append: no lost records" "$BIN/s2_race /dev/s2chr0 2 2000 | grep -q 'lost=0 '"
t "sysfs period_ms rejects 99999" "! echo 99999 > /sys/class/s2chr/s2chr1/period_ms"
t "sysfs period_ms=50 makes ticks" \
  "echo 50 > /sys/class/s2chr/s2chr1/period_ms && sleep 0.5 && echo 0 > /sys/class/s2chr/s2chr1/period_ms && [ \$(cat /sys/kernel/debug/s2chr/ticks) -gt 3 ]"
t "debugfs state readable"  "grep -q 'buf_size=' /sys/kernel/debug/s2chr/state"
t "rmmod"                   rmmod s2chr
t "nothing left after rmmod" clean
L=0; for s in 1 2 3 4 5 6 7; do insmod $KO fail_at=$s 2>/dev/null && L=1 && rmmod s2chr; clean || L=1; done
t "fail_at=1..7: insmod fails, nothing leaks" "[ $L = 0 ]"
t "re-insmod after injected failures" insmod $KO
t "final rmmod + clean"     "rmmod s2chr && clean"
echo "# Totals: pass:$((N-FAIL)) fail:$FAIL"
[ $FAIL = 0 ]
```

```text
TAP version 13
1..16
ok 1 - insmod
ok 2 - char nodes via devtmpfs
ok 3 - write/read round trip
ok 4 - rw contract (short/ENOSPC/EFAULT)
ok 5 - ioctl: unknown cmd -> ENOTTY
ok 6 - ioctl: old ABI size -> ENOTTY
ok 7 - poll + blocking read + EINTR
ok 8 - 2-process append: no lost records
ok 9 - sysfs period_ms rejects 99999
ok 10 - sysfs period_ms=50 makes ticks
ok 11 - debugfs state readable
ok 12 - rmmod
ok 13 - nothing left after rmmod
ok 14 - fail_at=1..7: insmod fails, nothing leaks
ok 15 - re-insmod after injected failures
ok 16 - final rmmod + clean
# Totals: pass:16 fail:0
selftest exit=0
```

처음 돌렸을 때는 `not ok 8`과 `not ok 4`가 나왔다 — 테스트 스크립트 쪽 버그였다(기본 `buf_size=4096`에 64000바이트를 쓰려 해서 ENOSPC, `s2_rw`에 버퍼 크기를 안 넘김). 테스트도 코드다. 실패하면 "드라이버가 틀렸나, 테스트가 틀렸나"부터 가른다.

### 9.8 회귀 실행 — selftest가 망가진 빌드를 잡는가

무엇을 확인하나: 테스트의 가치는 "버그가 있을 때 빨간불이 켜지는가"다. M8 소스에 버그 패치 3개를 각각 적용해 빌드하고, 버그마다 새로 부팅해 selftest를 돌렸다(class 누수는 게스트를 더럽히므로 한 부팅에 하나).

```sh
for b in m7-no-lock m8-leak-class m8-ladder-skip; do sh scripts/build-bug.sh bugs/$b.patch; done
sh scripts/mkinitrd.sh
for k in s2chr_m7-no-lock.ko s2chr_m8-leak-class.ko s2chr_m8-ladder-skip.ko; do
  sh scripts/run-qemu.sh "guest/regress.sh~$k"; done
```

```text
== selftest on s2chr_m7-no-lock.ko
not ok 8 - 2-process append: no lost records
# Totals: pass:15 fail:1
== selftest on s2chr_m8-leak-class.ko
not ok 13 - nothing left after rmmod
not ok 14 - fail_at=1..7: insmod fails, nothing leaks
not ok 15 - re-insmod after injected failures
not ok 16 - final rmmod + clean
# Totals: pass:12 fail:4
== selftest on s2chr_m8-ladder-skip.ko
not ok 14 - fail_at=1..7: insmod fails, nothing leaks
not ok 15 - re-insmod after injected failures
not ok 16 - final rmmod + clean
# Totals: pass:13 fail:3
```

(각 부팅 사이의 `lab initramfs` 줄과 콘솔에 섞여 나온 `kobject_add_internal failed ... -EEXIST` 줄을 지웠다.)

출력에서 볼 것: 세 버그가 모두 다른 항목에서 잡혔다. `m8-ladder-skip`은 step 6·7 실패 시 `goto err_devices` 대신 `goto err_fifo_cdev`로 뛰는 버그다 — 정상 경로(1~13번)는 전부 통과하고 **결함 주입(14번)만 잡는다.** 에러 경로 버그는 정상 사용으로는 영영 안 보인다는 것, 그래서 `fail_at` 같은 장치가 필요하다는 것을 숫자로 보여 준다. 덤으로, 이 버그 빌드는 컴파일러가 이미 경고했다:

```text
s2chr.c: In function ‘s2chr_init’:
s2chr.c:442:1: warning: label ‘err_devices’ defined but not used [-Wunused-label]
```

**경고 0개 규칙**이 왜 중요한지 보여 주는 장면이다 — 경고가 수십 개인 빌드에서는 이 한 줄이 묻힌다.

> 체크포인트: selftest가 `pass:16 fail:0`, 버그 빌드 세 개는 각각 `fail>0`. `git tag m8`.

### 9.9 git 기록

```sh
cd ~/.elinux_scratch/S2/s2-chardev && git log --oneline && git tag
```

```text
ea1e240 M8: sysfs attrs, debugfs stats, dev_dbg, fault-injected init, TAP selftest, bug zoo
b91d646 M7: mutex around the mem buffer, O_APPEND, open counting, 2-process race test
06127f2 M6: /dev/s2chr1 stream FIFO with blocking read, poll, wait queue, timer producer
f3beaff M5: ioctl (GET_INFO/_IOR, CLEAR/_IO, SET_PERIOD/_IOW) + shared UAPI header
4d6fc2b M4: read/write/llseek on a kernel buffer with copy_{to,from}_user
adfd1f0 M3: char device (alloc_chrdev_region + cdev + class/device, devtmpfs node)
3fc1648 M2: module parameters (uint/bool/charp, 0444/0644/0, range check)
1c4a2f6 M1: hello module with printk levels and out-of-tree Kbuild
m1
m2
m3
m4
m5
m6
m7
m8
```

최종 `.ko`: `size` 기준 text 7808 · data 1808 · bss 4528바이트(파일은 BTF·디버그 정보 때문에 456816바이트). 각 마일스톤 빌드 로그의 `warning` 개수는 m1~m8 모두 0이었다(`grep -ci warning`으로 확인). 버그 패치는 해당 태그의 소스 기준이다(`m3-leak-class`는 M8 소스에 적용되지 않아 `m8-leak-class`를 따로 만들었다).

---

## 10. 커널 안에서는 무슨 일이 일어나나 — 한 장 요약

| 사용자 동작 | 커널 경로 (6.18) | 우리 코드 | 더 깊이 |
|---|---|---|---|
| `insmod s2chr.ko` | `finit_module` → `load_module`(ELF·`struct module` 크기·vermagic 검사, 재배치, 파라미터 파싱) → `do_init_module` → `s2chr_init` | init의 7단계 | E3 §3.3, §10 |
| `open("/dev/s2chr1")` | `do_sys_open` → `path_openat` → `chrdev_open`: `kobj_lookup(devt)`로 cdev 찾기 → `inode->i_cdev` 캐시 → `replace_fops(filp, cdev->ops)` → `.open` | `s2_fifo_open`: `container_of`, `stream_open` | G4 §2, F12 |
| `read(fd)` | `ksys_read` → `vfs_read` → `file->f_op->read` | `s2_fifo_read` → `wait_event_interruptible` → `schedule()` | E6 §2, F3 §1 |
| 타이머 만료 | hardirq(arch timer) → `run_timer_softirq` → `__run_timers` → `s2_tick` | `s2_push` → `wake_up_interruptible` → `try_to_wake_up` | F7, F8 |
| `poll(fd)` | `do_sys_poll` → `vfs_poll` → `.poll`(poll_wait로 rq에 등록) → 잠 → wake_up이 poll 엔트리 콜백 호출 | `s2_fifo_poll` | G6 §1.4 |
| `ioctl(fd, cmd)` | `__arm64_sys_ioctl` → `do_vfs_ioctl`(공통 cmd 먼저: FIONBIO 등) → `.unlocked_ioctl` | `s2_ioctl` switch | E6 §6, G5 §3 |
| `echo 50 > period_ms` | `kernfs_fop_write_iter` → `sysfs_kf_write` → `dev_attr_store` | `period_ms_store` | G8 §2 |
| `rmmod` | `delete_module` → refcount 0 확인(`.owner`) → `s2chr_exit` → 모듈 메모리 해제 | exit: debugfs → device → class → cdev → timer → region → kfree | E3 §8 |

M6의 패닉은 마지막 줄의 "모듈 메모리 해제" 뒤에 타이머 휠이 해제된 메모리를 가리키고 있었기 때문이고, M3의 재로드 실패는 class kobject가 해제되지 않아 sysfs 이름이 남았기 때문이다. 두 사고 모두 "exit는 init의 정확한 역순이어야 하고, 비동기 경로를 먼저 끈다"로 정리된다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트의 IMU 데이터 경로

로봇 센서 키트에서 IMU가 FIFO watermark 인터럽트를 주는 경우, `s2chr1`의 구조가 그대로 쓰인다: 타이머 자리에 threaded IRQ 핸들러(I2C/SPI로 FIFO를 읽으니 잠들 수 있어야 함, F6·F7), kfifo에 타임스탬프 붙은 샘플, 유저는 `poll` + `read`. 다만 업스트림이라면 직접 char device를 만들지 않고 IIO buffer(`/dev/iio:deviceN`)를 쓴다 — IIO가 이 노트의 M6를 프레임워크로 만들어 둔 것이다. S4가 그 다음 단계다.

### 11.2 "가끔 데이터가 안 온다"

현장 증상 → 첫 명령: `cat /proc/<pid>/wchan`, `cat /proc/<pid>/stack`(M6 출력), debugfs의 큐 길이. 리더가 잔다 + 큐에 데이터가 있다 = wake_up 누락(FAIL-7). 리더가 잔다 + 큐가 비었다 + `ticks`가 안 늘어난다 = 생산자(IRQ)가 죽음. 카운터를 debugfs에 미리 넣어 두는 습관이 이 분류를 30초로 만든다.

### 11.3 벤더 BSP 모듈 업데이트

커널을 패치해서 다시 빌드하면 `-dirty`·git 해시가 `uname -r`에 붙고, out-of-tree 모듈이 FAIL-1처럼 전부 안 올라간다. 빌드 시스템(Yocto의 `module.bbclass`, S6)이 모듈을 커널과 함께 빌드하게 만드는 이유다. 그리고 `CONFIG_*`가 달라 `struct module` 크기가 바뀌면 vermagic보다 앞의 검사에서 막힌다는 것도 이번에 직접 봤다.

### 11.4 제품 펌웨어의 디버그 손잡이

`dev_dbg`는 빌드에 남기고 기본은 꺼 둔다. 현장에서는 `echo 'module s2chr +p' > /proc/dynamic_debug/control`, 부팅 문제라면 `s2chr.dyndbg=+p` 커널 인자. debugfs는 양산 이미지에서 마운트하지 않는 경우가 많으므로(보안), **꼭 필요한 상태는 sysfs**, 개발용은 debugfs로 나눈다.

---

## 12. 흔한 실수와 증상 — 이 노트에서 실제로 재현한 것

| 실수 | 증상 (이 노트의 실제 출력) | 원인 | 고치는 법 |
|---|---|---|---|
| 다른 커널 트리로 빌드 | `invalid module format` + dmesg `struct module size` 또는 `version magic` | `.config`/릴리스 문자열이 다른 트리 | 장비 커널의 빌드 트리로 빌드, dmesg부터 본다 |
| exit에서 `class_destroy` 누락 | rmmod 성공 → 다음 insmod `File exists`, `duplicate filename '/class/s2chr'` | class kobject가 sysfs에 남음 | init 역순 exit, selftest의 "nothing left" 검사 |
| read에서 `*ppos` 미전진 | `cat`이 끝나지 않음 (2초에 85164바이트) | 영원히 EOF가 안 옴 | `*ppos += n`, EOF에서 0 |
| `copy_to_user` 결과를 반환 | `cat`이 0바이트 출력 | 성공(0)을 EOF로 돌려줌 | 옮긴 바이트 수 `n`을 반환 |
| 사용자 길이를 믿음 | 64바이트 버퍼 뒤 slab 내용·커널 주소 누출 | `min(count, len - pos)` 생략 | 항상 길이 제한, HARDENED_USERCOPY·KASAN으로 검출 |
| ioctl 구조체 `memset` 누락 | `reserved`에 `ffff8000...` 스택 주소 (auto-init을 끈 빌드에서) | 채우지 않은 필드 = 이전 스택 | `memset`/`= {}`; "내 커널에선 안 터짐"은 증거 아님 |
| 생산자의 `wake_up` 누락 | 큐에 12바이트가 있는데 리더가 S 상태로 계속 잠 | 대기 큐를 아무도 깨우지 않음 | 데이터를 넣은 직후 `wake_up_interruptible` |
| exit에서 타이머 미정지 | rmmod 성공 후 `__run_timers` Oops → `Fatal exception in interrupt` 패닉 | 해제된 모듈 메모리의 `timer_list`가 휠에 남음 | `timer_shutdown_sync`를 exit 앞쪽에 |
| `O_APPEND` 경로에 락 없음 | 40000개 중 822~1107개 레코드 손실 | `len` read-modify-write 경쟁 | mutex(프로세스 컨텍스트), 스트레스 테스트 |
| goto 라벨 건너뜀 | 정상 테스트 통과, `fail_at=7`에서만 class 누수 + `-Wunused-label` 경고 | 실패 경로가 다른 라벨로 점프 | 결함 주입 테스트, 경고 0개 유지 |
| `class_create(THIS_MODULE, ...)` (옛 API) | 6.4+에서 컴파일 에러 (재현 안 함, 소스 확인) | 6.4에서 시그니처 변경 | `class_create("name")` |

---

## 13. 확장 과제 — 포트폴리오를 한 단계 더

1. **드리프트 없는 생산자**: `mod_timer(&d->timer, d->timer.expires + msecs_to_jiffies(period))`로 바꾸고 §7.6의 간격(102.9 → 414.2 ms 누적 밀림)이 어떻게 바뀌는지 측정한다. 이어서 `hrtimer`로 1 ms 주기를 만들고 `cyclictest`식 지터 히스토그램을 그린다(S5로 연결).
2. **blocking write**: FIFO가 가득 차면 `-ENOSPC` 대신 두 번째 wait queue `wq`에서 잔다. `.poll`의 `EPOLLOUT`이 비로소 의미가 생긴다(G6 §5의 wait queue 두 개).
3. **`.mmap`**: mem 버퍼를 `remap_vmalloc_range`로 유저에 매핑하고 `read()` 대비 처리량을 잰다(G5 §4–5).
4. **lockdep과 KASAN으로 돌리기**: `linux-build-dbg`/`linux-build-kasan`으로 빌드해 `Image-dbg`/`Image-kasan`에서 selftest를 돌린다. `m4-wrong-len`이 KASAN `slab-out-of-bounds`로, spinlock 안 `copy_to_user` 변형이 `DEBUG_ATOMIC_SLEEP`으로 잡히는지 확인한다(M8 노트).
5. **KUnit 테스트**: `Image-dom`(KUnit 포함)용으로 `s2_set_period()` 범위 검사 같은 순수 함수를 KUnit suite로 분리한다.
6. **misc device로 다시 쓰기**: M3의 4단계를 `misc_register` 하나로 바꾸고 줄 수 차이를 README에 적는다(G7, R3 §2).
7. **platform driver화**: DT 노드(`compatible = "don,s2chr"`)로 probe되게 바꾸고 goto 사다리를 `devm_*`으로 대체한다(G3, D 모듈). 그러면 S4 IIO 드라이버의 뼈대가 된다.
8. **CI**: GitHub Actions에서 x86_64 커널 헤더로 `make W=1`만이라도 돌리고(실행은 로컬 QEMU), `checkpatch.pl --strict`를 0경고로 만든다.

---

## 14. GitHub와 면접에서 보여 주기

### 14.1 README 개요 (그대로 섹션 제목으로 쓴다)

1. 한 줄 소개 — "Linux 6.18 char driver built in 8 tagged milestones: blocking read, poll, ioctl ABI, mutex, sysfs/debugfs, fault-injected init, TAP selftest, and a bug zoo of 10 reproduced failures."
2. 데모 GIF 또는 asciinema — `s2_poll` 1)~5)와 selftest 16/16.
3. 아키텍처 그림 — 이 노트의 그림 2(객체 그래프)와 그림 3(blocking read).
4. 마일스톤 표 — 태그, 배운 것, 핵심 API, 커밋 링크(그림 1).
5. Build & Run — `scripts/build.sh`, `mkinitrd.sh`, `run-qemu.sh` 세 줄, 필요한 것(커널 빌드 트리, QEMU, 정적 glibc).
6. User ABI — `include/s2chr_ioctl.h` 표(번호, 방향, 크기, errno).
7. Testing — selftest 16항목 목록, 회귀 결과(버그 3개가 각각 어떤 항목에서 잡히는가).
8. Bug zoo — 10개 패치 표: 증상, 원인, 고친 줄. **이 표가 면접관이 가장 오래 보는 부분이다.**
9. Numbers — race 손실 0 vs 822~1107(그림 4), poll 깨움 210 ms(writer 200 ms 지연), 타이머 주기 드리프트, `.ko` text 7808바이트. "TCG 에뮬레이션 수치"라고 명시.
10. Design notes — 왜 minor 두 개인가, 왜 FIFO는 spinlock이고 mem은 mutex인가, 왜 goto이고 devm이 아닌가.
11. Next — §13의 확장 목록(특히 IIO/platform driver로 가는 길, S4 링크).

### 14.2 3분 데모 스크립트

| 시간 | 화면 | 말할 것 (요지) |
|---|---|---|
| 0:00–0:20 | `git log --oneline` | "8단계, 단계마다 커밋. hello에서 시작해 blocking I/O와 테스트까지." |
| 0:20–0:50 | `insmod`, `ls -l /dev/s2chr*`, `/sys/class/s2chr/` | "번호 할당 → cdev → class/device → devtmpfs. 노드 두 개: seek 버퍼와 stream FIFO." |
| 0:50–1:30 | `./s2_poll` 출력 1)~5) | "EAGAIN, poll 깨움, 타임아웃, 다른 프로세스의 write가 잠든 read를 깨움, 시그널이면 EINTR. wait queue + `-ERESTARTSYS`." |
| 1:30–1:50 | `cat /proc/<pid>/stack` | "멈춘 리더가 어디서 자는지 바로 보인다 — 현장 디버깅 첫 명령." |
| 1:50–2:20 | race 결과 0 vs 822~1107 | "`O_APPEND`의 read-modify-write. 락을 빼면 2~3% 손실, 레코드가 찢어지지 않고 통째로 사라진다." |
| 2:20–2:45 | selftest 16/16 → ladder-skip 빌드에서 `not ok 14` | "에러 경로는 정상 사용으로 안 보인다. 결함 주입으로 7단계 실패를 전부 테스트했다. 컴파일러 `-Wunused-label`도 같은 버그를 잡았다." |
| 2:45–3:00 | bug zoo 표 | "vermagic, class 누수, 타이머 UAF 패닉 등 10개를 일부러 재현해 증상-원인을 정리했다. 실무에서 BSP 모듈 문제를 볼 때 이 표를 쓴다." |

### 14.3 숫자를 말할 때의 원칙

- 모든 시간은 TCG 에뮬레이션이라고 먼저 말한다. "실제 i.MX8에서 재면 다를 것이고, 다음 단계로 보드에서 잴 계획"이 좋은 답이다.
- 손실 개수 같은 race 결과는 "3번 중 3번"처럼 반복 횟수를 같이 말한다.
- 숫자보다 **판정 기준**(lost=0, leftovers=0, 경고 0개)을 강조한다 — CI가 쓸 수 있는 형태라는 점이 실무 감각을 보여 준다.

---

## 15. 면접에서 이렇게 말한다

**Q.** Walk me through what happens between `insmod` and the device node showing up in `/dev`.

**A.** 로더가 ELF를 검사하고(구조체 크기, vermagic) 재배치·파라미터 파싱 후 init을 부른다. init은 `alloc_chrdev_region`으로 번호를 빌리고, `cdev_add`로 번호와 `file_operations`를 묶고, `class_create`·`device_create`로 sysfs 객체를 만들어 uevent를 낸다. devtmpfs가 그 uevent의 MAJOR/MINOR/DEVNAME으로 노드를 만들고, udev가 있으면 권한·심볼릭 링크를 덧붙인다.

> The loader validates the ELF — struct module size and vermagic — relocates it, parses parameters, and calls init. Init reserves a dev_t range, binds it to my file_operations with cdev_add, then creates a class and a device, which emits a uevent. devtmpfs creates the node from MAJOR, MINOR and DEVNAME; udev, if present, only adjusts permissions and links. I order it so everything the fops touch is initialized before cdev_add, because the device is live the moment it is added.

**꼬리 질문**: 동적 major는 왜 쓰나? → 충돌 방지, `/proc/devices`나 sysfs `dev` 파일에서 읽는다(G4 §1.3).

**Q.** Your read handler returns a value. What are the rules?

**A.** 옮긴 바이트 수(짧아도 됨), 0은 EOF, 음수는 -errno. `*ppos`를 직접 전진시킨다. `copy_to_user`는 못 옮긴 바이트 수를 주므로 0이 아니면 `-EFAULT`. 실제로 `*ppos`를 빼먹으면 `cat`이 끝나지 않고, `copy_to_user` 결과를 돌려주면 0바이트가 나오는 것을 재현해 봤다.

> Return the number of bytes copied — a short count is fine — zero for end of file, and a negative errno on error. I advance `*ppos` myself. copy_to_user returns the bytes it could not copy, so non-zero means -EFAULT. I reproduced the two classic bugs: forgetting to advance the position makes cat loop forever, and returning copy_to_user's result returns zero, which userspace reads as EOF.

**Q.** How does your blocking read work, and what happens on a signal?

**A.** 비어 있고 `O_NONBLOCK`이면 `-EAGAIN`, 아니면 `wait_event_interruptible`로 조건을 다시 검사하며 잔다. 생산자(타이머 softirq)는 spinlock으로 kfifo에 넣고 `wake_up_interruptible`. 시그널이 오면 `-ERESTARTSYS`를 돌려주고 커널이 `SA_RESTART`에 따라 재시작하거나 `EINTR`로 바꾼다. 복사는 spinlock 밖에서 한다 — `copy_to_user`는 잠들 수 있으니까.

> If the FIFO is empty I return -EAGAIN for non-blocking files, otherwise I sleep in wait_event_interruptible, which re-checks the condition to avoid lost wakeups. The producer, a timer in softirq context, pushes under a spinlock and calls wake_up_interruptible. On a signal I return -ERESTARTSYS and the kernel either restarts the syscall or returns EINTR depending on SA_RESTART. I pop into a stack buffer under the lock and do copy_to_user after releasing it, since it can fault and sleep.

**꼬리 질문**: `.poll`은 무엇을 하나? → `poll_wait`로 등록, 현재 마스크 반환, 절대 잠들지 않음(G6 §1).

**Q.** Why a mutex for one buffer and a spinlock for the other?

**A.** mem 버퍼는 프로세스 컨텍스트에서만 만지고, 락을 쥔 채 `copy_from_user`(잠들 수 있음)를 해야 하므로 mutex. FIFO는 타이머 softirq도 만지므로 잠들 수 없는 spinlock(irqsave/bh)이고, 대신 락 안에서는 사용자 메모리를 건드리지 않는다. 락을 빼면 두 프로세스 append에서 2~3% 레코드를 잃는 것을 측정했다.

> The memory buffer is only touched from process context and I need to hold the lock across copy_from_user, which may sleep, so it's a mutex. The FIFO is also touched from a timer in softirq context, which cannot sleep, so it's a spinlock, and I never touch user memory while holding it. Without the mutex, two processes appending with O_APPEND lost two to three percent of records in my test — lost updates on the length field.

**Q.** How do you make sure your error paths are correct?

**A.** goto 사다리를 init 순서의 정확한 역순으로 쓰고, `fail_at` 파라미터로 7단계 각각에서 실패를 주입한 뒤 sysfs·`/proc/devices`·debugfs에 남은 것이 없는지 selftest로 확인한다. 라벨 하나를 잘못 건너뛴 빌드는 정상 테스트는 모두 통과하고 결함 주입 항목에서만 잡혔다. probe 기반 드라이버라면 `devm_*`으로 대부분을 없앤다.

> I write the unwind ladder in exact reverse order of init, and I prove it: a fail_at module parameter injects -ENOMEM at each of the seven steps, and the selftest checks that nothing is left in sysfs, /proc/devices or debugfs and that a clean insmod still works. A build where one goto jumped to the wrong label passed every functional test and failed only the injection test. In a probe-based driver I'd use devm to remove most of the ladder.

**Q.** A customer reports the kernel panics in the idle task a few hundred milliseconds after an update script. Where do you look?

**A.** `Comm: swapper`, 인터럽트/softirq 스택(`__run_timers`), `[last unloaded: xxx]`를 본다. 모듈 언로드 후 남은 타이머·work·IRQ가 해제된 메모리를 실행하는 전형이다. exit에서 `timer_shutdown_sync`, `cancel_work_sync`, `free_irq`를 했는지 확인한다. 실제로 타이머 정지를 빼서 똑같은 Oops를 재현했다.

> The idle task plus a softirq backtrace in __run_timers plus "last unloaded: s2chr" points to a timer, work item or IRQ that outlived its module. I check that exit stops every asynchronous path first — timer_shutdown_sync, cancel_work_sync, free_irq — before freeing anything. I reproduced exactly this: removing timer_shutdown_sync gives a level-3 translation fault on a write inside __run_timers and "Fatal exception in interrupt".

**Q.** Why should unknown ioctl commands return -ENOTTY, and how do you version an ioctl struct?

**A.** 커널 관례이고 libc·`isatty` 같은 판단이 `ENOTTY`에 의존한다. 구조체 크기가 번호에 들어가므로 크기가 바뀐 옛 앱은 자동으로 `ENOTTY`를 받는다. 고정 폭 타입, 예약 필드, version 필드, 그리고 반드시 `memset` 후 채운다 — 빼먹으면 스택 주소가 새는 것을 auto-init을 끈 빌드에서 확인했다.

> -ENOTTY is the convention that libc and callers like isatty rely on. Because _IOR/_IOW encode the argument size, an old binary built with a 12-byte struct sends a different number and gets ENOTTY instead of silently corrupting memory. I use fixed-width types, a version field and reserved words, and I memset the struct before filling it — with stack auto-init disabled, skipping the memset leaked a kernel stack address in the reserved words.

---

## 16. 직접 해보기

1. (개념) `s2_fifo_read`에서 `wait_event_interruptible` 대신 "`if (kfifo_is_empty()) schedule();`"로 쓰면 어떤 경쟁이 생기나? 정답/힌트: 검사와 잠 사이에 wake_up이 오면 영원히 잔다(lost wakeup). F3 §5.1.
2. (코드) FIFO가 가득 찼을 때 잠드는 blocking write를 추가하고, `s2_poll`에 `POLLOUT` 대기 케이스를 넣어라. 정답/힌트: `wq` 대기 큐를 하나 더, 리더가 꺼낸 뒤 `wake_up_interruptible(&d->wq)`.
3. (관찰) M6 시나리오에서 잠든 `cat`의 `/proc/<pid>/status`의 `voluntary_ctxt_switches`를 tick 전후로 읽어라. 정답/힌트: 깨어날 때마다 1씩 는다 — 잠은 자발적 문맥 전환이다.
4. (버그) `s2_fifo_read`의 `copy_to_user`를 spinlock 안으로 옮긴 패치를 만들고 `linux-build-dbg`로 빌드해 `Image-dbg`에서 돌려라. 정답/힌트: `DEBUG_ATOMIC_SLEEP`이 "BUG: sleeping function called from invalid context"를 낼 수 있다(page fault가 날 때). 페이지가 이미 매핑돼 있으면 조용하다는 점도 관찰하라.
5. (코드) `S2CHR_IOC_GET_INFO`에 필드를 하나 추가하되 옛 앱(32바이트 구조체)이 계속 동작하게 하라. 정답/힌트: `reserved[0]`을 새 필드로 쓰고 `version`을 2로 — 크기가 같으니 번호가 그대로다.
6. (관찰) `strace -e trace=openat,read,ioctl ./s2_ioctl`을 게스트 대신 rootfs-trace 게스트(strace 있음)에서 돌려 ioctl 번호가 `_IOC(_IOC_READ, 0x53, 0x1, 0x20)`로 디코딩되는지 보라. 정답/힌트: strace는 모르는 ioctl을 `_IOC(...)` 형태로 푼다.
7. (테스트) selftest에 "`O_NONBLOCK` read가 빈 FIFO에서 EAGAIN"과 "모듈 사용 중 rmmod가 실패(`.owner`)" 항목을 추가하라. 정답/힌트: `sleep 2 < /dev/s2chr1 &` 후 `! rmmod s2chr`.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| out-of-tree module | 커널 소스 밖에서 빌드한 모듈 | `make -C <KDIR> M=$PWD`, taint O |
| vermagic | 모듈에 새긴 커널 릴리스·옵션 문자열 | 로드 시 실행 커널과 비교 |
| `dev_t` | major:minor 번호 | 12비트 major + 20비트 minor |
| `cdev` | 번호 범위 ↔ fops 연결 객체 | `cdev_add` 순간부터 open 가능 |
| devtmpfs | 커널이 직접 채우는 `/dev` | uevent의 DEVNAME으로 노드 생성 |
| `*ppos` / `f_pos` | 파일 오프셋 | 드라이버가 read/write에서 직접 전진 |
| short read/write | 요청보다 적게 옮김 | 정상 동작, 호출자가 반복 |
| UAPI | 유저에게 노출된 ABI 헤더 | 고정 폭 타입, 순서 변경 금지 |
| `_IOR`/`_IOW` | ioctl 번호 생성 매크로 | 방향·타입·번호·크기 인코딩 |
| `-ENOTTY` | 모르는 ioctl | "Inappropriate ioctl for device" |
| wait queue | 잠든 태스크 목록 | `wait_event_*` / `wake_up_*` |
| `-ERESTARTSYS` | 시그널로 깬 잠 | 재시작 또는 EINTR로 변환 |
| stream_open | seek 없는 stream 파일로 표시 | `f_pos` 무시, `FMODE_STREAM` |
| kfifo | 커널의 2의 거듭제곱 링 버퍼 | `_spinlocked` 변형은 락 포함 |
| dynamic debug | 런타임에 켜는 `pr_debug`/`dev_dbg` | `/proc/dynamic_debug/control` |
| debugfs | 개발용 가상 FS | ABI 약속 없음, 반환값 검사 안 함 |
| goto ladder | 실패 시 역순 정리 라벨 | init의 거울상 |
| devm / devres | device 수명에 묶인 자원 | probe된 device가 있어야 씀 |
| TAP | Test Anything Protocol | `ok N - ...`, kselftest 출력 형식 |
| lost update | 동시 read-modify-write로 갱신 손실 | M7의 822~1107개 |

---

## 18. 요약 & 체크리스트

`s2chr`은 hello 모듈에서 출발해 번호·cdev·class/device로 노드를 만들고(M3), read/write의 반환값과 `*ppos` 계약(M4), 크기가 번호에 들어간 ioctl ABI(M5), wait queue로 잠들고 깨는 stream과 `poll`(M6), 컨텍스트에 맞는 락(M7), 그리고 sysfs/debugfs/dynamic debug 관측과 결함 주입으로 증명한 에러 경로·TAP selftest(M8)까지 8개 태그로 쌓였다. 과정에서 10개의 실패를 실제로 만들었고, 그중 셋(class 누수, 락 없음, 사다리 건너뜀)은 selftest가 서로 다른 항목에서 잡는다는 것을 확인했다. 이 저장소와 bug zoo 표가 GitHub 포트폴리오의 첫 장이고, 다음 장은 같은 구조를 IIO 센서 드라이버로 옮기는 S4다.

- [ ] 빈 디렉터리에서 Kbuild/Makefile을 쓰고 랩 트리로 W=1 경고 0개 빌드를 할 수 있다
- [ ] `invalid module format`을 보면 dmesg에서 struct 크기/vermagic 중 무엇인지 읽고 원인을 말할 수 있다
- [ ] `module_param` 권한 0/0444/0644가 sysfs에서 어떻게 보이는지, 검증이 누구 책임인지 설명할 수 있다
- [ ] `alloc_chrdev_region` → `cdev_add` → `class_create` → `device_create` 순서와 그 역순 exit를 백지에 쓸 수 있다
- [ ] read/write의 반환값 규칙과 `*ppos`를 지키는 코드를 쓰고, 세 가지 read 버그의 증상을 말할 수 있다
- [ ] `_IOR/_IOW` 번호를 손으로 풀고, `-ENOTTY`와 `memset`의 이유를 설명할 수 있다
- [ ] blocking read(`EAGAIN`, `wait_event_interruptible`, `-ERESTARTSYS`)와 `.poll`을 쓸 수 있다
- [ ] spinlock 안에서 `copy_to_user`를 하면 안 되는 이유와 우회 패턴을 말할 수 있다
- [ ] exit에서 타이머를 안 끄면 왜 idle에서 패닉이 나는지 Oops를 보고 설명할 수 있다
- [ ] mutex vs spinlock을 컨텍스트로 고르고, race를 재현하는 테스트를 짤 수 있다
- [ ] sysfs 속성·debugfs 카운터·dynamic debug를 붙이고 각각의 용도를 구분할 수 있다
- [ ] 결함 주입으로 goto 사다리를 검증하고, goto와 devm 중 언제 무엇을 쓰는지 말할 수 있다

## 참고 자료

- Linux 6.18 소스(랩 `/var/lab/src/linux-6.18.55`, 웹은 elixir.bootlin.com): `fs/char_dev.c`(`chrdev_open`, `cdev_add`), `drivers/base/class.c`(`class_create`), `include/linux/timer.h`(`timer_container_of`, `timer_shutdown_sync`), `include/linux/kfifo.h`, `include/linux/wait.h`, `kernel/module/main.c`(로더 검사), `fs/kernfs/file.c`
- docs.kernel.org: Driver API — "Char devices"(`driver-api/basics`), "The Linux Kernel Device Model", `userspace-api/ioctl/ioctl-number.rst`, `admin-guide/dynamic-debug-howto.rst`, `filesystems/debugfs.rst`, `filesystems/sysfs.rst`, `dev-tools/kselftest.rst`, `dev-tools/ktap.rst`, `kbuild/modules.rst`
- Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman, "Linux Device Drivers, 3rd ed." 3장(char drivers)·5장(concurrency)·6장(advanced char driver operations) — API 이름은 옛것이니 이 노트의 6.18 이름으로 바꿔 읽는다
- Michael Kerrisk, "The Linux Programming Interface" 21장(시그널과 시스템 콜 재시작), 63장(I/O multiplexing)
- Bootlin, "Linux kernel and driver development training" 슬라이드(char drivers, sleeping, locking 장)
- 이 저장소의 모든 소스: `~/.elinux_scratch/S2/s2-chardev/` (태그 m1~m8, `bugs/` 10개)
