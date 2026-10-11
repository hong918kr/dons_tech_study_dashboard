# G4. 문자 디바이스 — major/minor, cdev, alloc_chrdev_region, file_operations, private_data

> **이 노트를 다 읽으면**: `dev_t` → `cdev_map` → `struct cdev` → `file_operations`로 이어지는 연결을 그림으로 그리고 코드로 짤 수 있다 · `alloc_chrdev_region` + `cdev_add` + `class`/`device_create`로 `/dev` 노드가 생기기까지(uevent, devtmpfs, udev/mdev)를 설명할 수 있다 · 여러 minor = 여러 장치 인스턴스를 `container_of(inode->i_cdev, ...)`와 `file->private_data`로 per-device / per-open 상태로 나눌 수 있다 · `read()`의 `f_pos`·short read·EOF·`-EAGAIN` 규칙과 `.owner = THIS_MODULE`의 의미를 실제 실행 결과로 말할 수 있다
> **왜 필요한가**: "화이트보드에 char driver를 짜 보세요"는 Embedded Linux 드라이버 면접의 단골이고, 실무에서도 센서·FPGA·DSP 같은 "서브시스템이 없는 장치"는 결국 char device로 유저에게 보인다
> **Don 기준 난이도**: 링버퍼·ISR과 태스크 사이의 락·바이너리 레코드 스트림은 펌웨어에서 매일 했다 / 새로 배울 부분은 "장치 번호 → cdev → fops" 등록 체계, `/dev` 노드가 생기는 경로, 그리고 open마다 따로 생기는 `struct file`과 그 안의 `f_pos`
> **선행 노트**: E3(커널 모듈), E6(`copy_to_user` 규약, `vfs_read` → `f_op->read`), F3(blocking read, wait queue), F4(락), F12(`chrdev_open`이 `f_op`를 바꿔 끼우는 과정) — G5(ioctl·mmap), G6(poll), G7(misc device), G9(uevent·udev)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

펌웨어에서는 센서 드라이버를 쓰면 상위 태스크가 `imu_read(&sample)` 같은 함수를 **직접 부른다**. Linux에서는 유저 프로세스가 커널 함수를 직접 부를 수 없다. 대신 커널은 장치에 **파일 이름 하나**(`/dev/g4imu0`)를 주고, 유저는 그 파일을 `open`/`read`/`write`/`close` 한다. 커널은 이 파일 연산을 드라이버가 등록해 둔 함수 테이블(`struct file_operations`)로 넘긴다. 이렇게 "바이트 스트림처럼 보이는 장치"가 **문자 디바이스(character device)** 다.

문제는 "파일 이름 → 드라이버 함수"를 어떻게 잇느냐다. Linux의 답은 **번호**다. 장치 파일의 inode는 데이터 대신 **장치 번호(major:minor)** 하나만 들고 있고, 커널은 그 번호로 드라이버가 등록한 `struct cdev`를 찾는다(F12 §4에서 `chrdev_open()` 소스로 봤다). 이 노트는 그 반대편 — **드라이버가 번호를 받고, cdev를 등록하고, `/dev` 노드를 만들고, fops를 구현하는 쪽**을 본다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="16" width="260" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="37" text-anchor="middle" font-size="13">유저: open("/dev/g4imu1")</text>
  <text x="150" y="56" text-anchor="middle" font-size="12">inode: S_IFCHR, i_rdev = 510:1</text>
  <line x1="150" y1="66" x2="150" y2="98" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="20" y="100" width="260" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="122" text-anchor="middle" font-size="13">cdev_map (kobj_map)</text>
  <text x="150" y="141" text-anchor="middle" font-size="12">major로 해시 → minor 범위 비교</text>
  <text x="150" y="159" text-anchor="middle" font-size="12">cdev_add()가 넣고 cdev_del()이 뺀다</text>
  <line x1="280" y1="135" x2="338" y2="135" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="340" y="100" width="320" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="500" y="122" text-anchor="middle" font-size="13">g4_dev[1] 안의 struct cdev</text>
  <text x="500" y="141" text-anchor="middle" font-size="12">cdev.dev = 510:1, cdev.count = 1</text>
  <text x="500" y="159" text-anchor="middle" font-size="12">cdev.ops = &amp;g4_fops, cdev.owner</text>
  <line x1="150" y1="170" x2="150" y2="208" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="20" y="210" width="640" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="232" text-anchor="middle" font-size="13">chrdev_open() (F12 §4): inode->i_cdev = 찾은 cdev 캐시</text>
  <text x="340" y="254" text-anchor="middle" font-size="13">file->f_op = fops_get(cdev->ops) → 드라이버 .open = g4_open()</text>
  <line x1="340" y1="266" x2="340" y2="294" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="20" y="296" width="640" height="70" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="318" text-anchor="middle" font-size="13">이후의 syscall은 file->f_op로 바로 간다</text>
  <text x="340" y="338" text-anchor="middle" font-size="12">read → g4_read · write → g4_write · lseek → g4_llseek · close → g4_release</text>
  <text x="340" y="356" text-anchor="middle" font-size="12">ioctl → .unlocked_ioctl (G5) · poll → .poll (G6) · mmap → .mmap (G5)</text>
</svg>
```
그림 1 — 장치 번호가 열쇠다. 노드의 `i_rdev`로 `cdev_map`에서 cdev를 찾고, open 순간 `f_op`가 드라이버 테이블로 바뀐다. 그 뒤로는 번호 조회 없이 함수 포인터로 바로 간다.

펌웨어 비유로 옮기면:

| Linux | 펌웨어에서 비슷한 것 |
|---|---|
| major 번호 | 드라이버 테이블의 인덱스 (예: `drivers[DRV_IMU]`) |
| minor 번호 | 같은 드라이버가 관리하는 인스턴스 번호 (IMU #0, #1, #2) |
| `struct cdev` + `cdev_add` | "이 번호 범위는 이 드라이버가 받는다"는 등록 레코드 |
| `file_operations` | HAL ops 테이블 `{ open, read, write, ioctl }` |
| `struct file` | 핸들 하나 = 세션 하나 (커서 위치 `f_pos` 포함) |
| `/dev/g4imu0` | 이름 → 번호 매핑. 이름 자체는 아무 의미가 없다 |

이 노트의 실습은 공용 랩의 **debug 커널**(`/var/lab/out/Image-dbg`, 6.18.55-dbg — lockdep·DEBUG_ATOMIC_SLEEP 포함)을 QEMU `virt`(cortex-a57, `-smp 2`, 512 MB, TCG)로 부팅해서 돌렸다. 처리량 측정(§9)만 lockdep 오버헤드를 빼려고 일반 커널(`out/Image`, 6.18.55)에서 돌렸다.

| 절 | 내용 | 실습 |
|---|---|---|
| §1 | 장치 번호 `dev_t`, static vs dynamic major, 옛 `register_chrdev` | 예제 2, 3 |
| §2 | `struct cdev`와 등록 순서 | (코드 읽기) |
| §3 | `/dev` 노드 — class, `device_create`, uevent, devtmpfs | 예제 4, 5, 6 |
| §4 | 실습 드라이버 `g4_sensorkit` (3 minor, hrtimer 샘플 생성) | 예제 1 (빌드) |
| §5 | `file_operations` 필수 항목, llseek 세 가지 | (표) |
| §6 | `read()`와 `f_pos` — 부분 읽기, short read, EOF, `-EAGAIN`, overrun | 예제 7 |
| §7 | 동시성 — 여러 opener, per-open vs per-device, 락, `-EBUSY` | 예제 8, 9 |
| §8 | 모듈 언로드와 `.owner` | 예제 10, 11 |
| §9 | 처리량 vs read 크기 | 예제 12 + 차트 |
| §10 | 화이트보드 버전 (40줄) | 예제 13 |

> 실행 환경: Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`)에서 모듈과 테스트 프로그램을 빌드했다. 모듈 출력은 전부 **게스트 커널 6.18.55-dbg / 6.18.55**(BusyBox initramfs)에서 실제로 돌린 결과다. 테스트 프로그램은 VM에서 `gcc -std=gnu11 -Wall -Wextra -O2 -static`으로 빌드했고 경고 0개다. TCG 에뮬레이션이라 **시간 값은 실제 보드보다 수십 배 느리다** — 시간은 상대 비교로만 읽는다. 주소·PID·타임스탬프·major 번호(동적)는 실행마다 다를 수 있다.

### 0.1 실습 준비 — 빌드와 게스트 부팅 스크립트

모듈 네 개를 한 디렉터리에서 빌드한다. `g4_noown.c`는 `#define G4_NO_OWNER` 한 줄 + `#include "g4_sensorkit.c"`인 "버그 버전"이다(§8).

```make
obj-m += g4_sensorkit.o g4_noown.o g4_wb.o g4_old.o
```

### 예제 1 — 빌드 (debug 커널 트리)

무엇을 확인하나: 모듈이 경고 없이 빌드되고, vermagic이 게스트 커널과 맞는지.

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/G4/mod_dbg && cp /Users/donh/.elinux_scratch/G4/{g4_sensorkit.[ch],g4_noown.c,g4_wb.c,g4_old.c,Kbuild} /var/tmp/G4/mod_dbg/ \
  && cd /var/tmp/G4/mod_dbg && make -j2 -C /var/lab/out/linux-build-dbg M=$PWD modules && modinfo ./g4_sensorkit.ko'
```

```text
  CC [M]  g4_sensorkit.o
  CC [M]  g4_noown.o
  CC [M]  g4_wb.o
  CC [M]  g4_old.o
  MODPOST Module.symvers
  CC [M]  g4_sensorkit.mod.o
  CC [M]  .module-common.o
  CC [M]  g4_noown.mod.o
  CC [M]  g4_wb.mod.o
  CC [M]  g4_old.mod.o
  LD [M]  g4_sensorkit.ko
  LD [M]  g4_noown.ko
  LD [M]  g4_wb.ko
  LD [M]  g4_old.ko
filename:       /var/tmp/G4/mod_dbg/./g4_sensorkit.ko
description:    G4: sensorkit IMU char device, 3 minors
license:        GPL
depends:        
name:           g4_sensorkit
vermagic:       6.18.55-dbg SMP preempt mod_unload aarch64
parm:           major:int
parm:           excl:bool
parm:           rate_hz:uint
```

출력에서 볼 것: `vermagic 6.18.55-dbg` — 이 `.ko`는 debug 커널 전용이다. 일반 커널용은 같은 소스를 `/var/lab/out/linux-build`로 한 번 더 빌드했다(`mod_std`). `parm:` 세 줄이 §1(major), §7(excl), §4(rate_hz)에서 쓰는 모듈 파라미터다. (첫 빌드 때 `g4_wb`·`g4_old`에 `MODULE_DESCRIPTION()`이 없다는 modpost 경고가 나서 넣었다 — 6.x에서는 이것도 경고 대상이다.)

게스트에서 셸 스크립트 하나를 실행하고 끄는 부팅 스크립트(`boot.sh`, VM에서 실행). 랩 initramfs의 `/init`은 proc·sysfs·devtmpfs(`/dev`)·debugfs를 mount 하고 `LAB_CMD`를 실행한 뒤 전원을 끈다. **udev도 mdev도 돌지 않는다** — §3에서 이 사실이 중요해진다.

```sh
#!/bin/bash
# usage: KERN=dbg|std boot.sh <guest-script.sh>  -> boots lab kernel with /g4/* and runs the script
set -e
G=$1; N=$(basename $G .sh); K=${KERN:-dbg}; W=/var/tmp/G4/run_$N
IMG=/var/lab/out/Image; [ $K = dbg ] && IMG=/var/lab/out/Image-dbg
rm -rf $W && mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
mkdir -p $W/r/g4
cp /var/tmp/G4/mod_$K/*.ko /var/tmp/G4/bin/* $W/r/g4/
cp /Users/donh/.elinux_scratch/G4/$G $W/r/g4/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m 512M -nographic -no-reboot \
  -kernel $IMG -initrd $W/i.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/g4/t.sh" > $W/out.txt 2>&1 || echo "qemu rc=$?"
tr -d '\r' < $W/out.txt > /var/tmp/G4/out_$N.txt
```

유저 테스트 프로그램(`g4_uev`, `g4_rd`, `g4_multi`, `g4_bench`)은 VM에서 static으로 빌드해 `/var/tmp/G4/bin/`에 두면 위 스크립트가 initramfs의 `/g4/`에 넣는다.

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/G4/bin && cd /Users/donh/.elinux_scratch/G4 && for p in g4_uev g4_rd g4_multi g4_bench; do gcc -std=gnu11 -Wall -Wextra -O2 -static -I. $p.c -o /var/tmp/G4/bin/$p; done'
```

---

## 1. 장치 번호 — `dev_t`, MAJOR / MINOR / MKDEV

### 1.1 직관 — 번호는 "드라이버 테이블 인덱스 + 인스턴스 번호"

`ls -l /dev`를 보면 장치 파일은 크기 자리에 숫자 두 개가 찍힌다.

```text
crw-rw-rw-    1 0        0         510,   0 Oct 11 01:44 /dev/g4imu0
crw-rw-rw-    1 0        0         510,   1 Oct 11 01:44 /dev/g4imu1
crw-rw-rw-    1 0        0         510,   2 Oct 11 01:44 /dev/g4imu2
```

첫 글자 `c`는 character device, `510`이 **major**, `0/1/2`가 **minor**다. 전통적인 의미는 "major = 어느 드라이버, minor = 그 드라이버 안의 몇 번째 장치"다. 지금도 대체로 맞지만, 정확히는 커널이 보는 단위는 **(major, minor 범위) → cdev** 매핑이다. 한 major 안에서 minor 범위를 나눠 서로 다른 드라이버가 가져갈 수도 있다(예: major 10 `misc`는 minor마다 다른 드라이버다 — G7).

### 1.2 정의 — 커널 안의 `dev_t`

커널 안에서 장치 번호는 `dev_t`(32비트 정수) 하나다. 랩 소스 `include/linux/kdev_t.h` 그대로:

```c
#define MINORBITS	20
#define MINORMASK	((1U << MINORBITS) - 1)
#define MAJOR(dev)	((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)	((unsigned int) ((dev) & MINORMASK))
#define MKDEV(ma,mi)	(((ma) << MINORBITS) | (mi))
```

```
 31            20 19                              0
+----------------+--------------------------------+
|  major (12b)   |          minor (20b)           |
+----------------+--------------------------------+
 MKDEV(510, 1) = (510 << 20) | 1 = 0x1FE00001
```

- 커널 안에서는 **반드시 매크로**를 쓴다. 비트 배치를 코드에 하드코딩하지 않는다.
- inode에서 꺼낼 때는 `imajor(inode)` / `iminor(inode)` (= `MAJOR(inode->i_rdev)` / `MINOR(inode->i_rdev)`).
- **유저에게 보이는 인코딩은 다르다.** `stat()`의 `st_rdev`는 커널이 `new_encode_dev()`로 바꿔서 준다(하위 8비트 minor, 그 위 12비트 major, 나머지 minor 상위 비트). 유저 코드는 `<sys/sysmacros.h>`의 `major()`/`minor()`/`makedev()`만 쓴다. 커널 `dev_t`를 날것 그대로 유저에게 넘기는 ioctl을 만들면 안 되는 이유다.
- major는 이론상 12비트(4095)지만 char device 등록 코드는 `CHRDEV_MAJOR_MAX 512`까지만 받는다(`include/linux/fs.h`).

### 1.3 static vs dynamic major

번호를 얻는 API는 두 개다. 둘 다 `fs/char_dev.c`이고, **번호 범위만 예약한다**(아직 장치와 연결되지 않는다 — 연결은 §2의 `cdev_add`).

| API | 쓰는 경우 | 실패하면 |
|---|---|---|
| `register_chrdev_region(dev_t from, unsigned count, const char *name)` | major를 **내가 정한다** (static). 문서화된 고정 번호가 있는 장치 | 이미 누가 쓰면 `-EBUSY` |
| `alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count, const char *name)` | 커널이 빈 major를 **골라 준다** (dynamic). 새 드라이버는 이것 | 빈 major가 없으면 `-EBUSY` |
| `unregister_chrdev_region(dev_t from, unsigned count)` | 둘 다 해제는 이것 | — |

동적 major는 어디서 고르나? 랩 소스의 `find_dynamic_major()`:

```c
	for (i = ARRAY_SIZE(chrdevs)-1; i >= CHRDEV_MAJOR_DYN_END; i--) {
		if (chrdevs[i] == NULL)
			return i;
	}

	for (i = CHRDEV_MAJOR_DYN_EXT_START;
	     i >= CHRDEV_MAJOR_DYN_EXT_END; i--) {
```

`chrdevs[]` 크기는 255(`CHRDEV_MAJOR_HASH_SIZE`)이므로 첫 루프는 **254 → 234**를 내려가며 빈 자리를 찾고, 다 차 있으면 **511 → 384** 확장 구간을 찾는다(`CHRDEV_MAJOR_DYN_END 234`, `CHRDEV_MAJOR_DYN_EXT_START 511`, `CHRDEV_MAJOR_DYN_EXT_END 384`).

### 예제 2 — `/proc/devices`와 동적 major가 510이 된 이유

무엇을 확인하나: 동적 할당이 실제로 어느 번호를 줬는지, 왜 그 번호인지. 게스트 스크립트 `t_reg.sh`의 앞부분이다.

```sh
cd /g4
./g4_uev 6 g4 > /tmp/uev.txt &        # §3 예제 5에서 본다
sleep 1
insmod g4_sensorkit.ko; dmesg | grep 'g4:'
sed -n '/Character/,/^$/p' /proc/devices | grep -E 'Character|^ *(1|4|5|10|2[0-9][0-9]) '
```

```text
[   12.929199] g4: major 510, minors 0..2, 100 Hz
Character devices:
  1 mem
  4 /dev/vc/0
  4 tty
  4 ttyS
  5 /dev/tty
  5 /dev/console
  5 /dev/ptmx
 10 misc
204 ttyAMA
207 ttymxc
234 vfio
235 megaraid_sas_ioctl
236 ttyTHS
237 ttySTM
238 ttyHS
239 ttyMSM
240 ttyOWL
241 ttyMSM
242 ttyLP
243 ttyLF
244 bsg
245 watchdog
246 tee
247 iio
248 ptp
249 pps
250 rtc
251 tpm
252 pwm
253 ttyMV
254 gpiochip
```

같은 커널을 모듈 없이 부팅해서 300번대 이상만 본 결과:

```sh
qemu-system-aarch64 ... -kernel /var/lab/out/Image-dbg -initrd /var/lab/out/initramfs.cpio.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=grep~-E~^[345][0-9][0-9]~/proc/devices"
```

```text
511 rpmb
```

출력에서 볼 것: 랩 커널은 arm64 `defconfig` 기반이라 수많은 SoC의 시리얼·서브시스템 드라이버가 빌트인으로 들어 있고, 이들이 부팅 중에 **234~254를 전부** 동적으로 가져갔다(`ttyOWL`, `ttyMSM`, `iio`, `gpiochip` …). 확장 구간의 511은 `rpmb`가 가져갔으니 우리 차례는 **510**이다. 실제 제품 커널(필요한 드라이버만 넣은 config)에서는 보통 250 근처가 나온다. **동적 major는 config·로드 순서에 따라 바뀐다** — 그래서 유저 코드에 major를 하드코딩하면 안 된다.

### 예제 3 — static major는 충돌한다

무엇을 확인하나: `register_chrdev_region`에 이미 쓰이는 major를 주면 어떻게 되는지. `Documentation/admin-guide/devices.txt`가 "local/experimental use"로 비워 둔 240~254를 써 보자. 게스트 스크립트 `t_static.sh` 앞부분:

```sh
cd /g4
insmod g4_sensorkit.ko major=240; echo "insmod rc=$?"
grep -E '^240 ' /proc/devices
insmod g4_sensorkit.ko major=300; echo "insmod rc=$?"
grep g4 /proc/devices; ls -l --color=never /dev/g4imu*; rmmod g4_sensorkit
insmod g4_sensorkit.ko major=4; echo "insmod rc=$?"
grep -E '^ *4 ' /proc/devices
```

```text
insmod: can't insert 'g4_sensorkit.ko': Device or resource busy
insmod rc=1
240 ttyOWL
insmod rc=0
300 g4_sensorkit
crw-rw-rw-    1 0        0         300,   0 Oct 11 01:42 /dev/g4imu0
crw-rw-rw-    1 0        0         300,   1 Oct 11 01:42 /dev/g4imu1
crw-rw-rw-    1 0        0         300,   2 Oct 11 01:42 /dev/g4imu2
insmod: can't insert 'g4_sensorkit.ko': Device or resource busy
insmod rc=1
  4 /dev/vc/0
  4 tty
  4 ttyS
```

출력에서 볼 것: "실험용으로 비워 둔" 240도 **동적 할당이 먼저 가져가면** 쓸 수 없다(`-EBUSY` → `insmod`의 "Device or resource busy"). major 4는 tty가 쓰고 있어 역시 실패한다. 동적 구간 밖의 300은 성공했다. 교훈: **새 드라이버는 `alloc_chrdev_region`**. static major는 `devices.txt`에 공식 할당된 오래된 장치(예: `mem` 1, `tty` 4, `misc` 10)에만 쓴다. "major를 고정해야 rootfs의 `/dev`를 미리 만들 수 있다"는 옛 이유는 devtmpfs(§3)로 사라졌다.

### 1.4 옛 API — `register_chrdev()`

LDD3 이전 세대 코드와 지금도 꽤 많은 벤더 드라이버가 쓰는 한 줄짜리 API다. 랩 소스:

```c
static inline int register_chrdev(unsigned int major, const char *name,
				  const struct file_operations *fops)
{
	return __register_chrdev(major, 0, 256, name, fops);
}
```

번호 예약 + cdev 할당 + `cdev_add`를 한 번에 하는데, **minor 0~255 전체(256개)를 한 fops로** 가져간다. 실습 모듈 `g4_old`(minor를 찍는 `.open`만 있다):

```c
// SPDX-License-Identifier: GPL-2.0
/* g4_old: legacy register_chrdev() grabs minors 0..255 with one fops */
#include <linux/module.h>
#include <linux/fs.h>

static int maj;

static int old_open(struct inode *inode, struct file *f)
{
	pr_info("g4_old: open major=%u minor=%u\n", imajor(inode), iminor(inode));
	return 0;
}
static const struct file_operations old_fops = {
	.owner = THIS_MODULE, .open = old_open,
};

static int __init old_init(void)
{
	maj = register_chrdev(0, "g4_old", &old_fops);
	if (maj < 0)
		return maj;
	pr_info("g4_old: major %d\n", maj);
	return 0;
}
static void __exit old_exit(void)
{
	unregister_chrdev(maj, "g4_old");
}
module_init(old_init);
module_exit(old_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G4: legacy register_chrdev demo");
```

```sh
insmod g4_old.ko; M=$(awk '$2=="g4_old"{print $1}' /proc/devices); grep g4_old /proc/devices
mknod /tmp/o0 c $M 0; mknod /tmp/o200 c $M 200; mknod /tmp/o255 c $M 255; mknod /tmp/o256 c $M 256
for n in 0 200 255 256; do cat /tmp/o$n 2>&1; done
dmesg | grep g4_old
```

```text
510 g4_old
cat: read error: Invalid argument
cat: read error: Invalid argument
cat: read error: Invalid argument
cat: can't open '/tmp/o256': No such device or address
[    6.932673] g4_old: major 510
[    7.212825] g4_old: open major=510 minor=0
[    7.245281] g4_old: open major=510 minor=200
[    7.292809] g4_old: open major=510 minor=255
```

출력에서 볼 것: minor 0, 200, 255 — 드라이버가 만든 적도 없는 장치가 **전부 열린다**. open은 성공하고 `.read`가 없으니 read에서 `-EINVAL`("Invalid argument")이 났다(`vfs_read`는 `.read`도 `.read_iter`도 없으면 `-EINVAL`). 256은 범위 밖이라 `cdev_map`에 없어서 `-ENXIO`("No such device or address"). 그리고 `/dev` 노드가 생기지 않았다 — `register_chrdev`는 class/device를 만들지 않으므로 노드는 직접 `mknod` 해야 한다. **새 코드에는 쓰지 않는다**: minor를 필요한 만큼만 예약하는 `alloc_chrdev_region` + `cdev_add`를 쓴다. 면접에서 "register_chrdev와 뭐가 달라요?"가 나오면 이 출력이 답이다.

---

## 2. `struct cdev` — 번호와 fops를 잇는 끈

### 2.1 정의

`struct cdev`(`include/linux/cdev.h`)는 "major:minor 범위 → `file_operations`" 매핑 한 개다. 드라이버가 쓰는 필드는 사실상 세 개다.

| 필드 | 누가 채우나 | 의미 |
|---|---|---|
| `ops` | `cdev_init(cdev, fops)` | open 때 `file->f_op`로 복사될 테이블 |
| `owner` | 드라이버가 직접 (`cdev.owner = THIS_MODULE`) | open 동안 모듈을 잡아 둘 주인 (§8) |
| `dev`, `count` | `cdev_add(cdev, dev, count)` | 담당하는 첫 번호와 개수 |
| `kobj` | 커널 | 참조 카운트. 열린 file이 있으면 cdev가 살아 있다 |

API 네 개:

| 함수 | 하는 일 |
|---|---|
| `cdev_init(&c, &fops)` | 구조체 안에 박혀 있는(embedded) cdev를 초기화하고 `ops`를 넣는다 |
| `cdev_alloc()` | cdev를 동적 할당하는 버전. 구조체에 박아 넣을 수 없을 때만 |
| `cdev_add(&c, dev, count)` | `cdev_map`에 등록. **이 순간부터 open이 들어올 수 있다** |
| `cdev_del(&c)` | `cdev_map`에서 뺀다. 새 open은 막지만 **이미 열린 file은 계속 fops를 부를 수 있다** |

`cdev_add`의 본체는 짧다(랩 소스, 에러 처리 일부 생략 없이 그대로):

```c
int cdev_add(struct cdev *p, dev_t dev, unsigned count)
{
	int error;

	p->dev = dev;
	p->count = count;

	if (WARN_ON(dev == WHITEOUT_DEV)) {
		error = -EBUSY;
		goto err;
	}

	error = kobj_map(cdev_map, dev, count, NULL,
			 exact_match, exact_lock, p);
	if (error)
		goto err;

	kobject_get(p->kobj.parent);

	return 0;
```

`kobj_map(cdev_map, ...)`이 그림 1의 녹색 상자다. open 쪽의 `chrdev_open()` → `kobj_lookup(cdev_map, i_rdev)` → `container_of(kobj, struct cdev, kobj)` 경로는 F12 §4.2에서 소스와 ftrace로 봤으니 여기서는 반복하지 않는다.

### 2.2 `cdev_del`의 경고 — 소스 주석 그대로

```c
/**
 * cdev_del() - remove a cdev from the system
 * @p: the cdev structure to be removed
 *
 * cdev_del() removes @p from the system, possibly freeing the structure
 * itself.
 *
 * NOTE: This guarantees that cdev device will no longer be able to be
 * opened, however any cdevs already open will remain and their fops will
 * still be callable even after cdev_del returns.
 */
```

이 주석이 이 노트에서 가장 중요한 한 줄일 수 있다. **`cdev_del()`이 돌아와도 이미 열린 fd의 `read()`는 계속 들어온다.** 그래서 드라이버 상태 구조체를 `cdev_del()` 직후 `kfree()` 하면 use-after-free다. 모듈 언로드 경로에서는 `.owner`가 이것을 막아 준다(§8 — 열린 fd가 있으면 언로드 자체가 거부된다). 하지만 **USB 분리 같은 핫플러그 remove**는 모듈 언로드가 아니므로 `.owner`가 못 막는다. 그때는 상태 구조체에 참조 카운트(kref)를 두고 마지막 `release`에서 해제하거나, `cdev_device_add`로 cdev 수명을 `struct device`에 묶는다(아래 2.4, G3).

### 2.3 등록 순서 — "준비 다 하고 마지막에 공개"

```
init:  alloc_chrdev_region -> (state, ring, locks, timer init) -> cdev_init -> cdev_add -> device_create
exit:  device_destroy -> cdev_del -> (stop timer, free state) -> unregister_chrdev_region
```

- `cdev_add` 전에 **장치 상태를 전부 초기화**한다. `cdev_add`가 돌아오기 전에도 누군가 (`mknod`로 만든 노드로) open할 수 있다.
- `device_create`는 `cdev_add` **뒤**. 노드가 생겼는데(그리고 udev가 그걸 보고 데몬을 띄웠는데) open이 `-ENXIO`면 경쟁 상태 버그다.
- 해제는 정확히 역순. 위 규칙은 펌웨어에서 "ISR 등록은 버퍼·상태 초기화가 끝난 뒤, 인터럽트 enable은 맨 마지막"과 같은 원리다.

### 2.4 minor마다 cdev 하나 vs cdev 하나에 count=N

| 방식 | 코드 | 장점 | 단점 |
|---|---|---|---|
| 인스턴스마다 cdev 1개 (`cdev_add(&d->cdev, MKDEV(maj, i), 1)`) | 이 노트의 `g4_sensorkit` | `container_of(inode->i_cdev, ...)`로 바로 인스턴스를 찾는다. 인스턴스마다 수명 관리 가능 | cdev가 N개 |
| cdev 1개로 minor N개 (`cdev_add(&cdev, base, N)`) | 옛 코드, `register_chrdev` 내부 | 등록이 한 번 | open에서 `iminor(inode)`로 배열 인덱싱. 인스턴스 핫플러그에 약하다 |

현대 커널은 인스턴스마다 `struct device`가 있고, 그 device 안에 cdev를 박아 두고 `cdev_device_add()`로 둘을 한 번에 등록하는 패턴을 권장한다. 랩 소스의 주석: "This function should be used whenever the struct cdev and the struct device are members of the same structure whose lifetime is managed by the struct device." 모양은 이렇다 (**이 스니펫은 실행하지 않았다** — `g4_sensorkit`은 더 흔한 `cdev_add` + `device_create` 조합으로 짰다):

```c
/* sketch, not built in this note: cdev + device in one lifetime */
struct imu {
	struct device dev;	/* owns the lifetime (refcounted) */
	struct cdev cdev;
};

static void imu_release(struct device *dev)	/* last put_device() */
{
	kfree(container_of(dev, struct imu, dev));
}

static int imu_register(struct imu *m, dev_t devt, const struct class *cls)
{
	device_initialize(&m->dev);
	m->dev.devt = devt;
	m->dev.class = cls;
	m->dev.release = imu_release;
	dev_set_name(&m->dev, "imu%d", MINOR(devt));
	cdev_init(&m->cdev, &imu_fops);
	return cdev_device_add(&m->cdev, &m->dev);	/* cdev parent = dev */
}
/* remove: cdev_device_del(&m->cdev, &m->dev); put_device(&m->dev); */
```

`cdev_device_add()`는 `cdev_set_parent(cdev, &dev->kobj)`로 cdev가 device를 참조하게 만든다. 열린 file이 cdev를 잡고 있는 동안 device(그리고 그걸 품은 `struct imu`)가 해제되지 않는다. 핫플러그 장치 드라이버(USB, PCIe)를 쓸 때 다시 만난다.

---

## 3. `/dev` 노드 만들기 — class, `device_create`, uevent, devtmpfs

### 3.1 직관 — 노드는 "번호가 적힌 명함"이다

`cdev_add`까지 끝나도 유저는 아직 장치를 못 연다. **이름이 없기 때문이다.** `/dev/g4imu0`는 `(c, 510, 0)`을 담은 inode일 뿐이고 누군가 만들어 줘야 한다. 만드는 방법은 세 가지다.

| 방법 | 누가 만드나 | 언제 쓰나 |
|---|---|---|
| `mknod /dev/x c 510 0` | 사람/스크립트 | 디버깅. 옛 정적 `/dev` |
| devtmpfs | **커널이 직접** (`device_add()` → `devtmpfs_create_node()`) | 현대 시스템 기본. 랩 커널 `CONFIG_DEVTMPFS=y` |
| udev / mdev | 유저 데몬이 **uevent**를 받아서 | 권한·소유자·심볼릭 링크·이름 규칙, 펌웨어 로딩, 모듈 자동 로드 (G9) |

현대 시스템에서는 devtmpfs가 노드를 만들고, udev(systemd-udevd)나 mdev가 그 뒤에 **권한과 심볼릭 링크를 다듬는다**. devtmpfs가 없는 아주 작은 시스템에서만 mdev가 노드 자체를 `mknod` 한다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="16" width="250" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="145" y="37" text-anchor="middle" font-size="13">alloc_chrdev_region()</text>
  <text x="145" y="56" text-anchor="middle" font-size="12">번호 예약 (510:0~2)</text>
  <rect x="300" y="16" width="360" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="480" y="46" text-anchor="middle" font-size="12">/proc/devices에 "510 g4_sensorkit"</text>
  <line x1="270" y1="41" x2="298" y2="41" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="145" y1="66" x2="145" y2="80" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="20" y="82" width="250" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="145" y="103" text-anchor="middle" font-size="13">cdev_init() + cdev_add()</text>
  <text x="145" y="122" text-anchor="middle" font-size="12">번호 → cdev → fops 연결</text>
  <rect x="300" y="82" width="360" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="480" y="112" text-anchor="middle" font-size="12">cdev_map 등록: 이 순간부터 open 가능</text>
  <line x1="270" y1="107" x2="298" y2="107" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="145" y1="132" x2="145" y2="146" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="20" y="148" width="250" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="145" y="169" text-anchor="middle" font-size="13">class_register()</text>
  <text x="145" y="188" text-anchor="middle" font-size="12">또는 class_create("이름")</text>
  <rect x="300" y="148" width="360" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="480" y="178" text-anchor="middle" font-size="12">/sys/class/g4_sensorkit/ + uevent "add@/class/..."</text>
  <line x1="270" y1="173" x2="298" y2="173" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="145" y1="198" x2="145" y2="212" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="20" y="214" width="250" height="138" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="145" y="270" text-anchor="middle" font-size="13">device_create()</text>
  <text x="145" y="290" text-anchor="middle" font-size="12">→ device_add()</text>
  <text x="145" y="310" text-anchor="middle" font-size="12">인스턴스마다 한 번</text>
  <rect x="300" y="214" width="360" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="480" y="239" text-anchor="middle" font-size="12">① sysfs: /sys/devices/virtual/g4_sensorkit/g4imu0</text>
  <rect x="300" y="263" width="360" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="480" y="288" text-anchor="middle" font-size="12">② devtmpfs: 커널이 /dev/g4imu0 직접 생성</text>
  <rect x="300" y="312" width="360" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="480" y="337" text-anchor="middle" font-size="12">③ kobject_uevent(ADD) → netlink 브로드캐스트</text>
  <line x1="270" y1="234" x2="298" y2="234" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="270" y1="283" x2="298" y2="283" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="270" y1="332" x2="298" y2="332" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="480" y1="352" x2="480" y2="370" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="160" y="372" width="500" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="410" y="390" text-anchor="middle" font-size="12">udev / mdev (유저 데몬): 권한 · 소유자 · 심볼릭 링크 · rule</text>
  <text x="410" y="408" text-anchor="middle" font-size="12">devtmpfs가 없으면 노드 자체를 mknod 한다 (G9)</text>
</svg>
```
그림 2 — 번호 예약과 cdev 등록만으로는 노드가 생기지 않는다. `device_create()`가 sysfs 디렉터리를 만들고, devtmpfs에 노드를 만들고, uevent를 쏜다. udev/mdev는 마지막 단계의 "다듬기" 담당이다.

### 3.2 class — 6.4부터 바뀐 API

class는 "같은 종류 장치의 묶음"이다(`/sys/class/tty`, `/sys/class/input` …). 노드를 devtmpfs·udev가 만들려면 `struct device`가 필요하고, `device_create()`는 class를 인자로 받는다. 버전 차이가 면접·실무 코드에서 자주 헷갈린다.

| 버전 | 시그니처 | 비고 |
|---|---|---|
| ~6.3 | `class_create(THIS_MODULE, "name")` | 옛 책·블로그 코드. 6.4+에서는 컴파일 에러 |
| 6.4+ | `struct class *class_create(const char *name)` | 랩 6.18 `include/linux/device/class.h:226` 확인 |
| 권장 | `static const struct class x = { .name = ..., ... };` + `class_register(&x)` | class를 정적 const로. 랩 6.18의 `class_register(const struct class *cls)` 확인 |

`g4_sensorkit`은 권장 방식(정적 const + `class_register`)을, 화이트보드 버전(§10)은 짧은 `class_create("g4_wb")`를 쓴다. 정적 class에는 `.devnode` 콜백을 넣을 수 있다 — devtmpfs 노드의 이름·권한을 정한다:

```c
static char *g4_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;		/* what a udev rule would otherwise do */
	return NULL;			/* keep the default name */
}
static const struct class g4_class = { .name = "g4_sensorkit", .devnode = g4_devnode };
```

실무에서는 권한을 드라이버에 박지 않고 **udev rule**로 준다(`KERNEL=="g4imu*", GROUP="sensor", MODE="0660"`). 여기서는 udev가 없는 initramfs라 `.devnode`로 대신했다. 차이는 §10 예제 13에서 보인다(`.devnode`가 없는 `g4_wb`는 `crw-------`).

`device_create(cls, parent, devt, drvdata, fmt, ...)`의 인자:

- `parent`: 이 장치의 부모 `struct device`. 실제 드라이버라면 platform/I2C device(`&pdev->dev`)를 준다 → sysfs에서 `/sys/devices/platform/.../g4_sensorkit/g4imu0`처럼 HW 계층 아래 붙는다. `NULL`이면 `/sys/devices/virtual/` 아래로 간다.
- `drvdata`: `dev_get_drvdata()`로 꺼낼 포인터. sysfs 속성(G8)에서 인스턴스를 찾을 때 쓴다.
- `fmt`: 노드·sysfs 이름. `"g4imu%d"`.

### 예제 4 — sysfs와 `/dev`에서 확인하기

무엇을 확인하나: `device_create()`가 만든 것들 — 노드, class 링크, `dev`·`uevent` 속성, `/sys/dev/char` 역방향 링크. (`t_reg.sh` 계속)

```sh
ls -l --color=never /dev/g4imu*
ls -l --color=never /sys/class/g4_sensorkit/
ls --color=never /sys/class/g4_sensorkit/g4imu1/
cat /sys/class/g4_sensorkit/g4imu1/dev; cat /sys/class/g4_sensorkit/g4imu1/uevent
ls -l --color=never /sys/dev/char/ | grep g4
```

```text
crw-rw-rw-    1 0        0         510,   0 Oct 11 01:44 /dev/g4imu0
crw-rw-rw-    1 0        0         510,   1 Oct 11 01:44 /dev/g4imu1
crw-rw-rw-    1 0        0         510,   2 Oct 11 01:44 /dev/g4imu2
total 0
lrwxrwxrwx    1 0        0                0 Oct 11 01:44 g4imu0 -> ../../devices/virtual/g4_sensorkit/g4imu0
lrwxrwxrwx    1 0        0                0 Oct 11 01:44 g4imu1 -> ../../devices/virtual/g4_sensorkit/g4imu1
lrwxrwxrwx    1 0        0                0 Oct 11 01:44 g4imu2 -> ../../devices/virtual/g4_sensorkit/g4imu2
dev        power      subsystem  uevent
510:1
MAJOR=510
MINOR=1
DEVNAME=g4imu1
DEVMODE=0666
lrwxrwxrwx    1 0        0                0 Oct 11 01:44 510:0 -> ../../devices/virtual/g4_sensorkit/g4imu0
lrwxrwxrwx    1 0        0                0 Oct 11 01:44 510:1 -> ../../devices/virtual/g4_sensorkit/g4imu1
lrwxrwxrwx    1 0        0                0 Oct 11 01:44 510:2 -> ../../devices/virtual/g4_sensorkit/g4imu2
```

출력에서 볼 것:

- 이 initramfs에는 **udev도 mdev도 돌지 않는데** 노드가 생겼다. devtmpfs가 커널 안에서 만든 것이다(랩 소스 `drivers/base/core.c:3731`의 `devtmpfs_create_node(dev)` 호출이 `device_add()` 안에 있다). 모드 `crw-rw-rw-`는 `.devnode`가 준 0666이다.
- `/sys/class/g4_sensorkit/g4imu1`은 실체가 아니라 `/sys/devices/virtual/...`로 가는 **심볼릭 링크**다. sysfs의 실체는 항상 `/sys/devices` 트리 하나이고 class·bus 디렉터리는 링크 모음이다(G2).
- `dev` 파일 `510:1`과 `uevent` 파일의 `MAJOR`/`MINOR`/`DEVNAME`이 udev가 노드를 만들 때 쓰는 정보 전부다. `/sys/dev/char/510:1`은 "번호 → 장치" 역방향 조회용이다(`libudev`가 `stat()`의 `st_rdev`로 장치를 찾을 때 쓴다).

### 예제 5 — uevent를 직접 받아 보기 (udev가 받는 것)

무엇을 확인하나: `device_create()`가 쏘는 uevent의 실제 내용. udev/mdev는 `NETLINK_KOBJECT_UEVENT` 소켓으로 이것을 받는다. 같은 소켓을 열어 `insmod` 동안 들어오는 메시지를 찍는다.

```c
/* g4_uev: listen to kernel uevents (what udev/mdev receive) for N seconds */
#include <linux/netlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

int main(int argc, char **argv)
{
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	int secs = argc > 1 ? atoi(argv[1]) : 5;
	const char *filt = argc > 2 ? argv[2] : "g4";
	char buf[4096];
	int s = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);

	if (s < 0 || bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		perror("netlink");
		exit(1);
	}
	printf("uev: listening %d s\n", secs);
	fflush(stdout);
	struct pollfd p = { .fd = s, .events = POLLIN };
	while (poll(&p, 1, secs * 1000) > 0) {
		ssize_t n = recv(s, buf, sizeof(buf) - 1, 0);
		if (n <= 0)
			break;
		buf[n] = '\0';
		if (!strstr(buf, filt))		/* header "ACTION@DEVPATH" */
			continue;
		printf("== %s\n", buf);
		for (char *k = buf + strlen(buf) + 1; k < buf + n; k += strlen(k) + 1)
			if (!strncmp(k, "MAJOR", 5) || !strncmp(k, "MINOR", 5) ||
			    !strncmp(k, "DEVNAME", 7) || !strncmp(k, "SUBSYSTEM", 9) ||
			    !strncmp(k, "DEVMODE", 7))
				printf("   %s\n", k);
	}
	return 0;
}
```

(`t_reg.sh` 첫 줄에서 백그라운드로 띄워 두고 1초 뒤 `insmod` 했다.)

```text
uev: listening 6 s
== add@/class/g4_sensorkit
   SUBSYSTEM=class
== add@/devices/virtual/g4_sensorkit/g4imu0
   SUBSYSTEM=g4_sensorkit
   MAJOR=510
   MINOR=0
   DEVNAME=g4imu0
   DEVMODE=0666
== add@/devices/virtual/g4_sensorkit/g4imu1
   SUBSYSTEM=g4_sensorkit
   MAJOR=510
   MINOR=1
   DEVNAME=g4imu1
   DEVMODE=0666
== add@/devices/virtual/g4_sensorkit/g4imu2
   SUBSYSTEM=g4_sensorkit
   MAJOR=510
   MINOR=2
   DEVNAME=g4imu2
   DEVMODE=0666
== add@/module/g4_sensorkit
   SUBSYSTEM=module
```

출력에서 볼 것: 메시지는 `ACTION@DEVPATH` 헤더 + `KEY=VALUE` 문자열들(NUL 구분)이다. 순서가 등록 순서 그대로다 — class 먼저, 인스턴스 셋, 그리고 **module add는 맨 마지막**(`module_init`이 성공적으로 돌아온 뒤에 쏜다). udev rule의 `SUBSYSTEM=="g4_sensorkit"`, `KERNEL=="g4imu*"`가 매칭하는 값이 바로 여기 있는 것들이다. 실제 시스템에서 같은 것을 보려면 `udevadm monitor --kernel --property`(G9).

### 예제 6 — `mknod`로 직접 만든 노드, 없는 minor

무엇을 확인하나: 노드의 이름·위치는 상관없고 **(c, major, minor)만** 의미가 있다는 것. 등록 안 된 minor는 `-ENXIO`.

```sh
M=$(awk '$2=="g4_sensorkit"{print $1}' /proc/devices)
mknod /tmp/myimu2 c $M 2; mknod /tmp/myimu3 c $M 3; ls -l --color=never /tmp/myimu*
./g4_rd /tmp/myimu2 | head -3
cat /tmp/myimu3; echo "cat rc=$?"
```

```text
crw-r--r--    1 0        0         510,   2 Oct 11 01:44 /tmp/myimu2
crw-r--r--    1 0        0         510,   3 Oct 11 01:44 /tmp/myimu3
sizeof(struct g4_sample) = 32, f_pos after open = 19360
1) 10 B (mid-sample)         read(   10) =    10    f_pos=19370 (seq 605 + 10 B)
2) 54 B                      read(   54) =    22    f_pos=19392 (seq 606 + 0 B)
cat: can't open '/tmp/myimu3': No such device or address
cat rc=1
```

출력에서 볼 것: `/tmp/myimu2`(tmpfs 위의 노드, 권한도 기본값 `crw-r--r--`)로도 `/dev/g4imu2`와 똑같이 드라이버에 닿는다. minor 3은 `cdev_add`로 등록한 범위(0~2) 밖이라 `chrdev_open()`의 `kobj_lookup`이 실패해 `-ENXIO`. 이것이 "Permission denied도 아니고 No such file도 아닌" ENXIO의 정체다 — **노드는 있는데 그 번호를 받는 드라이버가 없다**. 보드에서 "`/dev/foo`는 있는데 열면 ENXIO"라면 드라이버가 로드되지 않았거나 probe가 실패한 것이다.

---

## 4. 실습 드라이버 `g4_sensorkit` — IMU 3개, minor 3개

### 4.1 설계

모듈 G의 공용 예제인 가상 센서 키트 "sensorkit"의 IMU 3개를 char device 하나(major 1개, minor 0~2)로 노출한다.

| 항목 | 결정 | 이유 |
|---|---|---|
| 인스턴스 | minor마다 `struct g4_dev` 하나 (cdev 내장) | `container_of(inode->i_cdev, ...)`로 인스턴스를 찾는다 |
| 데이터 소스 | 인스턴스마다 hrtimer(기본 100 Hz)가 샘플 생성 | 실제 IMU의 data-ready 인터럽트 대역. hrtimer 콜백은 hardirq 문맥(F8) |
| 버퍼 | 인스턴스마다 링 1024 샘플(32 KiB), **덮어쓰기** | 센서 데이터는 "최신이 중요". 느린 리더는 오래된 샘플을 잃는다(overrun) |
| 레코드 | `struct g4_sample` 32바이트 바이너리 | 텍스트 변환 비용 없음. ABI 헤더를 유저와 공유 |
| `f_pos` | 샘플 스트림의 **바이트 오프셋** (`seq × 32 + 바이트`) | 부분 읽기·`lseek`가 자연스럽다. 여러 리더가 서로 데이터를 뺏지 않는다(브로드캐스트) |
| `write` | 텍스트 명령 `start` / `stop` / `rate N` | 설정 경로. 정식 설계는 ioctl(G5)이나 sysfs(G8) |
| EOF | `stop` 상태에서 따라잡으면 `read() = 0` | "더 이상 데이터가 오지 않는다"는 약속이 있을 때만 EOF |

공유 ABI 헤더(드라이버와 유저가 같이 include):

```c
/* g4_sensorkit.h — ABI shared by the driver and userspace */
#ifndef G4_SENSORKIT_H
#define G4_SENSORKIT_H
#include <linux/types.h>

struct g4_sample {		/* one IMU reading = 32 bytes */
	__u64 t_ns;		/* ktime_get_ns() when sampled */
	__u32 seq;		/* per-instance sample number */
	__u16 dev;		/* minor number = instance id */
	__u16 flags;
	__s16 acc[3];		/* accel, mg */
	__s16 gyr[3];		/* gyro, raw */
	__u32 rsv;
};
#endif
```

`__u64` 같은 `linux/types.h` 타입을 쓰고, 큰 필드부터 놓아 패딩이 없게 했다(8+4+2+2+6+6+4 = 32). 32/64비트 유저가 같은 레이아웃을 보게 하는 규칙은 E6 §6.5에서 봤다.

### 4.2 코드 (1/4) — 인스턴스 상태와 샘플 생성기

```c
// SPDX-License-Identifier: GPL-2.0
/* g4_sensorkit (1/4): per-instance state + hrtimer sample generator */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include "g4_sensorkit.h"

#ifdef G4_NO_OWNER
#define G4_OWNER NULL			/* the bug we demonstrate in 8.3 */
#else
#define G4_OWNER THIS_MODULE
#endif
#define G4_NDEV	3
#define G4_RING	1024			/* samples per instance, power of 2 */
#define G4_SZ	((u64)sizeof(struct g4_sample))

static int major;			/* 0 = dynamic */
module_param(major, int, 0444);
static bool excl;			/* single open per minor */
module_param(excl, bool, 0644);
static unsigned int rate_hz = 100;
module_param(rate_hz, uint, 0444);

struct g4_dev {				/* one per minor = per IMU instance */
	struct cdev cdev;		/* chrdev_open() finds us through this */
	int id;
	spinlock_t ring_lock;		/* ring + head: hrtimer(hardirq) vs read */
	struct g4_sample *ring;
	u64 head;			/* samples produced so far */
	wait_queue_head_t wq;
	struct mutex cfg_lock;		/* running/period: write() vs write() */
	bool running;
	ktime_t period;
	struct hrtimer timer;
	unsigned long busy;		/* bit 0: held open (excl mode) */
};

struct g4_open {			/* one per open() = file->private_data */
	struct g4_dev *d;
	bool held;
	u64 bytes, lost;
};

static dev_t g4_base;
static struct g4_dev *g4_devs[G4_NDEV];

static enum hrtimer_restart g4_tick(struct hrtimer *t)
{
	struct g4_dev *d = container_of(t, struct g4_dev, timer);
	struct g4_sample *s;
	u32 n;

	spin_lock(&d->ring_lock);	/* hardirq: IRQs already off */
	n = (u32)d->head;
	s = &d->ring[d->head & (G4_RING - 1)];
	*s = (struct g4_sample){ .t_ns = ktime_get_ns(), .seq = n, .dev = d->id,
		.acc = { (s16)(n % 200) - 100, d->id * 10, 1000 },
		.gyr = { (s16)(n % 31) - 15, 0, 3 } };
	WRITE_ONCE(d->head, d->head + 1);
	spin_unlock(&d->ring_lock);
	wake_up_interruptible(&d->wq);
	hrtimer_forward_now(t, READ_ONCE(d->period));
	return HRTIMER_RESTART;
}
```

포인트:

- **상태를 두 구조체로 나눴다.** `g4_dev`는 장치당 하나(링, 타이머, 락), `g4_open`은 open당 하나(통계, excl 소유 여부). 이 분리가 §7의 주제다.
- `struct cdev`를 `g4_dev` **안에 박았다**(포인터가 아니라 값). 그래야 `container_of()`로 cdev 주소에서 `g4_dev` 주소를 역산할 수 있다 — Don이 펌웨어 링크드 리스트에서 쓰던 "멤버 주소 − offsetof = 컨테이너 주소" 그대로다(E5).
- hrtimer 콜백은 hardirq 문맥이라 잠들 수 없다 → 링은 **spinlock**으로 보호한다. mutex로는 안 된다(§7.3).
- `head`는 지금까지 만든 샘플 수(64비트, 단조 증가). 링 인덱스는 `head & (G4_RING - 1)`. 펌웨어 링버퍼에서 쓰는 "free-running 카운터 + 마스크" 기법이다.
- `hrtimer_setup()`은 6.13 무렵 들어온 새 초기화 API다(옛 `hrtimer_init()` + `timer.function = ...` 대체). 랩 6.18 `include/linux/hrtimer.h:230`에서 시그니처를 확인했다.

### 4.3 코드 (2/4) — open / release / llseek

```c
/* g4_sensorkit (2/4): open / release / llseek */
static int g4_open(struct inode *inode, struct file *f)
{
	struct g4_dev *d = container_of(inode->i_cdev, struct g4_dev, cdev);
	struct g4_open *o;
	bool held = false;

	if (READ_ONCE(excl)) {
		if (test_and_set_bit(0, &d->busy))
			return -EBUSY;
		held = true;
	}
	o = kzalloc(sizeof(*o), GFP_KERNEL);
	if (!o) {
		if (held)
			clear_bit(0, &d->busy);
		return -ENOMEM;
	}
	o->d = d;
	o->held = held;
	f->private_data = o;
	f->f_pos = READ_ONCE(d->head) * G4_SZ;	/* start "live" */
	pr_info("g4: open  imu%d minor=%u pos=%lld excl=%d\n",
		d->id, iminor(inode), f->f_pos, held);
	return 0;
}

static int g4_release(struct inode *inode, struct file *f)
{
	struct g4_open *o = f->private_data;

	pr_info("g4: close imu%d bytes=%llu lost=%llu\n",
		o->d->id, o->bytes, o->lost);
	if (o->held)
		clear_bit(0, &o->d->busy);
	kfree(o);
	return 0;
}

static loff_t g4_llseek(struct file *f, loff_t off, int whence)
{
	struct g4_open *o = f->private_data;

	/* valid range is [0, bytes produced so far]; SEEK_END = "now" */
	return fixed_size_llseek(f, off, whence, READ_ONCE(o->d->head) * G4_SZ);
}
```

포인트:

- **open의 첫 줄이 이 노트의 핵심 관용구다**: `container_of(inode->i_cdev, struct g4_dev, cdev)`. `chrdev_open()`이 `inode->i_cdev`에 cdev 포인터를 캐시해 뒀으므로(F12 §4.2), 드라이버는 minor 번호로 배열을 뒤질 필요 없이 바로 인스턴스를 얻는다.
- 그다음 open당 구조체를 할당해 **`file->private_data`에 건다.** 이후의 모든 fops(`read`, `write`, `llseek`, `release`)는 `struct inode *`를 받지 않거나(read/write) 받더라도 `file`만 보면 되게 만든다. `private_data`는 VFS가 건드리지 않는 드라이버 전용 슬롯이다. (misc device는 `misc_open()`이 여기에 `struct miscdevice *`를 미리 넣어 준다 — F12 §5, G7.)
- `f->f_pos`를 open에서 직접 정했다. 기본값은 0인데, 그러면 처음 read가 "아주 오래된 샘플"부터 요청하게 돼 바로 overrun이 난다. "열면 지금부터"가 센서 스트림에 맞는 의미다. 과거 샘플이 필요하면 `lseek(fd, -N × 32, SEEK_END)`.
- `fixed_size_llseek(f, off, whence, size)`는 `SEEK_SET`/`SEEK_CUR`/`SEEK_END`를 처리하고 결과가 `[0, size]` 밖이면 `-EINVAL`을 주는 헬퍼다(랩 `fs/read_write.c:262`). 여기서 size = "지금까지 만든 바이트 수"라서 **미래로는 seek할 수 없다.**
- excl 모드의 `test_and_set_bit`는 원자적 "검사 + 설정"이다. `if (busy) return -EBUSY; busy = 1;`로 쓰면 두 open이 동시에 통과하는 경쟁이 생긴다.
- `release`는 **`close()`마다가 아니라 `struct file`의 마지막 참조가 사라질 때** 한 번 불린다. `dup()`·`fork()`로 fd가 복제돼 있으면 마지막 close에서만 온다(F12 §5).

### 4.4 코드 (3/4) — read

```c
/* g4_sensorkit (3/4): read — byte-granular f_pos over the sample stream */
static ssize_t g4_read(struct file *f, char __user *buf, size_t cnt, loff_t *ppos)
{
	struct g4_open *o = f->private_data;
	struct g4_dev *d = o->d;
	struct g4_sample tmp[8];
	size_t done = 0;

	while ((u64)*ppos >= READ_ONCE(d->head) * G4_SZ) {	/* no data yet */
		if (!READ_ONCE(d->running))
			return 0;				/* stopped: EOF */
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(d->wq,
				(u64)*ppos < READ_ONCE(d->head) * G4_SZ ||
				!READ_ONCE(d->running)))
			return -ERESTARTSYS;
	}
	while (done < cnt) {
		u64 seq = (u64)*ppos / G4_SZ, off = (u64)*ppos % G4_SZ, n, k, len;
		u64 oldest;

		spin_lock_irq(&d->ring_lock);
		oldest = d->head > G4_RING ? d->head - G4_RING : 0;
		if (seq < oldest) {		/* overwritten: skip ahead */
			o->lost += oldest - seq;
			seq = oldest;
			off = 0;
		}
		n = min_t(u64, d->head - seq, ARRAY_SIZE(tmp));
		for (k = 0; k < n; k++)
			tmp[k] = d->ring[(seq + k) & (G4_RING - 1)];
		spin_unlock_irq(&d->ring_lock);
		if (!n)
			break;			/* caught up: short read */
		len = min_t(u64, n * G4_SZ - off, cnt - done);
		if (copy_to_user(buf + done, (char *)tmp + off, len))
			return done ? done : -EFAULT;
		done += len;
		*ppos = seq * G4_SZ + off + len;
	}
	o->bytes += done;
	return done;
}
```

읽는 순서대로:

1. **데이터가 하나도 없으면** 세 갈래: 정지 상태면 `0`(EOF), `O_NONBLOCK`이면 `-EAGAIN`, 아니면 wait queue에서 잔다. 시그널이면 `-ERESTARTSYS`. 이 모양은 F3 §2.6의 정석 패턴과 같다(조건은 락 없이 보고, 깨어나면 다시 확인).
2. **데이터가 조금이라도 있으면 절대 잠들지 않는다.** 있는 만큼만 주고 돌아온다 → short read. 이것이 `read()` 규약이다: "요청한 만큼 다 채울 때까지 기다린다"가 아니라 "최소 1바이트가 생기면 돌려준다"(파이프·소켓·tty와 같다).
3. 링에서 최대 8샘플(256 B)을 **spinlock 아래서 스택 버퍼로 복사**하고, 락을 푼 다음 `copy_to_user()` 한다. `copy_to_user()`는 page fault로 잠들 수 있어서 spinlock 안에서 부르면 안 된다(debug 커널이면 `DEBUG_ATOMIC_SLEEP`이 "BUG: sleeping function called from invalid context"를 찍는다).
4. 요청 `f_pos`가 이미 덮어써진 샘플이면 가장 오래된 샘플로 건너뛰고 `lost`를 센다.
5. `*ppos`는 **실제로 복사한 바이트만큼만** 전진한다. 샘플 중간에서 끝나도 된다 — 다음 read가 그 샘플의 나머지 바이트부터 이어 준다.
6. `copy_to_user()` 실패 시: 이미 일부 복사했다면 그 바이트 수를, 아니면 `-EFAULT`. E6 §3.3의 "부분 성공 우선" 규약이다.

`*ppos`와 `f->f_pos`의 관계: VFS(`ksys_read`)가 `file->f_pos`를 지역 변수에 복사해 `ppos`로 넘기고, 드라이버가 돌아오면 다시 `file->f_pos`에 써 준다(E6 §2.1). 그래서 드라이버는 **`f->f_pos`가 아니라 `*ppos`를 읽고 쓴다.** `pread()`는 `f_pos`와 무관한 지역 오프셋을 `ppos`로 넘기므로, `*ppos`만 쓰면 `pread()`도 공짜로 맞게 동작한다.

### 4.5 코드 (4/4) — write와 등록

```c
/* g4_sensorkit (4/4): write commands + registration */
static ssize_t g4_write(struct file *f, const char __user *ubuf, size_t cnt,
			loff_t *ppos)
{
	struct g4_dev *d = ((struct g4_open *)f->private_data)->d;
	char buf[24], *c;
	unsigned int hz;
	ssize_t ret = cnt;

	if (cnt == 0 || cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = '\0';
	c = strim(buf);
	if (mutex_lock_interruptible(&d->cfg_lock))
		return -ERESTARTSYS;
	if (!strcmp(c, "stop")) {
		hrtimer_cancel(&d->timer);
		WRITE_ONCE(d->running, false);
		wake_up_interruptible(&d->wq);		/* sleepers see EOF */
	} else if (!strcmp(c, "start")) {
		if (!d->running) {
			WRITE_ONCE(d->running, true);
			hrtimer_start(&d->timer, d->period, HRTIMER_MODE_REL);
		}
	} else if (sscanf(c, "rate %u", &hz) == 1 && hz >= 1 && hz <= 20000) {
		WRITE_ONCE(d->period, ns_to_ktime(NSEC_PER_SEC / hz));
	} else {
		ret = -EINVAL;
	}
	mutex_unlock(&d->cfg_lock);
	return ret;
}

static const struct file_operations g4_fops = {
	.owner = G4_OWNER, .open = g4_open, .release = g4_release,
	.read = g4_read, .write = g4_write, .llseek = g4_llseek,
};
```

(§3.2의 `g4_devnode`·`g4_class` 정의가 여기 온다.)

```c
static void g4_destroy(int i)
{
	struct g4_dev *d = g4_devs[i];

	device_destroy(&g4_class, d->cdev.dev);	/* node + sysfs go first */
	cdev_del(&d->cdev);			/* no new opens after this */
	hrtimer_cancel(&d->timer);
	kfree(d->ring);
	kfree(d);
}

static int g4_create(int i)
{
	struct g4_dev *d = kzalloc(sizeof(*d), GFP_KERNEL);
	struct device *dev;
	int ret;

	if (!d)
		return -ENOMEM;
	d->ring = kcalloc(G4_RING, sizeof(*d->ring), GFP_KERNEL);
	if (!d->ring) {
		kfree(d);
		return -ENOMEM;
	}
	d->id = i;
	spin_lock_init(&d->ring_lock);
	init_waitqueue_head(&d->wq);
	mutex_init(&d->cfg_lock);
	d->period = ns_to_ktime(NSEC_PER_SEC / (rate_hz ? rate_hz : 1));
	hrtimer_setup(&d->timer, g4_tick, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cdev_init(&d->cdev, &g4_fops);
	d->cdev.owner = G4_OWNER;
	ret = cdev_add(&d->cdev, MKDEV(MAJOR(g4_base), MINOR(g4_base) + i), 1);
	if (ret)
		goto err_free;
	dev = device_create(&g4_class, NULL, d->cdev.dev, d, "g4imu%d", i);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		cdev_del(&d->cdev);
		goto err_free;
	}
	g4_devs[i] = d;
	d->running = true;
	hrtimer_start(&d->timer, d->period, HRTIMER_MODE_REL);
	return 0;
err_free:
	kfree(d->ring);
	kfree(d);
	return ret;
}
```

```c
static int __init g4_init(void)
{
	int i, ret;

	if (major) {
		g4_base = MKDEV(major, 0);
		ret = register_chrdev_region(g4_base, G4_NDEV, "g4_sensorkit");
	} else {
		ret = alloc_chrdev_region(&g4_base, 0, G4_NDEV, "g4_sensorkit");
	}
	if (ret)
		return ret;
	ret = class_register(&g4_class);
	if (ret)
		goto err_region;
	for (i = 0; i < G4_NDEV; i++) {
		ret = g4_create(i);
		if (ret)
			goto err_devs;
	}
	pr_info("g4: major %d, minors %d..%d, %u Hz\n", MAJOR(g4_base),
		MINOR(g4_base), MINOR(g4_base) + G4_NDEV - 1, rate_hz);
	return 0;
err_devs:
	while (--i >= 0)
		g4_destroy(i);
	class_unregister(&g4_class);
err_region:
	unregister_chrdev_region(g4_base, G4_NDEV);
	return ret;
}

static void __exit g4_exit(void)
{
	int i;

	for (i = 0; i < G4_NDEV; i++)
		g4_destroy(i);
	class_unregister(&g4_class);
	unregister_chrdev_region(g4_base, G4_NDEV);
	pr_info("g4: unloaded\n");
}
module_init(g4_init);
module_exit(g4_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G4: sensorkit IMU char device, 3 minors");
```

포인트:

- `g4_create`의 순서가 §2.3 규칙 그대로다: 상태 초기화 → `cdev_add` → `device_create` → 타이머 시작.
- `write`는 `*ppos`를 무시한다. 명령 채널에 위치 개념은 없다. 반환값은 **소비한 바이트 수**(`cnt`) — 0을 돌려주면 유저 `write()` 루프가 무한히 다시 쓴다.
- `rate` 변경은 `period`만 바꾸고 타이머는 건드리지 않는다. 콜백이 다음 `hrtimer_forward_now()`에서 새 주기를 읽는다.
- 실무 드라이버라면 이 init/exit 대신 platform driver의 `probe`/`remove`(G3) 안에서 같은 일을 하고, `device_create`의 `parent`에 `&pdev->dev`를 준다.

---

## 5. `file_operations` — 무엇을 채우고 무엇을 비우나

랩 6.18의 `struct file_operations`는 40개 가까운 멤버가 있다. char device 드라이버가 실제로 채우는 것은 이 정도다.

| 멤버 | 비우면 (NULL) | 언제 채우나 |
|---|---|---|
| `owner` | 모듈 언로드 보호 없음 (§8) | **항상** `THIS_MODULE` |
| `open` | open은 그냥 성공 | 인스턴스 찾기, per-open 할당, 단일 open 정책, HW 켜기 |
| `release` | close 때 아무것도 안 함 | open에서 할당한 것 해제, HW 끄기 |
| `read` / `write` | `-EINVAL` | 바이트 스트림 데이터 경로 |
| `read_iter` / `write_iter` | — | `readv`/AIO/io_uring 효율이 필요할 때. 일반 char 드라이버는 `read`/`write`로 충분 |
| `llseek` | `lseek()`가 `-ESPIPE` | 아래 표 |
| `unlocked_ioctl` / `compat_ioctl` | `-ENOTTY` | 설정·제어 명령 (G5) |
| `poll` | 항상 "읽기·쓰기 가능"으로 보임 | `select`/`poll`/`epoll` 지원 (G6) |
| `mmap` | `-ENODEV` | DMA 버퍼·레지스터 창을 유저에 매핑 (G5) |
| `fasync` | — | `SIGIO` 통지 (G6) |
| `flush` | — | close마다 불림(드물게 씀). `release`와 헷갈리지 말 것 |

### 5.1 llseek 세 가지 (6.18 기준 — 직접 확인)

예전 코드에서 흔히 보던 `no_llseek`는 6.18 헤더에 **없다**(랩 `include/linux/fs.h`에 `default_llseek`, `noop_llseek`, `generic_file_llseek`, `fixed_size_llseek`만 있음을 grep으로 확인했다. 6.12 무렵 제거된 것으로 기억하는데 정확한 버전은 확인하지 않았다). 대신 `.llseek`를 NULL로 두면 seek 불가가 된다. 랩 `fs/open.c`의 `do_dentry_open()`:

```c
	f->f_mode |= FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE;
...
	if ((f->f_mode & FMODE_LSEEK) && !f->f_op->llseek)
		f->f_mode &= ~FMODE_LSEEK;
```

그리고 `vfs_llseek()`:

```c
loff_t vfs_llseek(struct file *file, loff_t offset, int whence)
{
	if (!(file->f_mode & FMODE_LSEEK))
		return -ESPIPE;
	return file->f_op->llseek(file, offset, whence);
}
```

| 선택 | 동작 | 맞는 장치 |
|---|---|---|
| `.llseek = NULL` (또는 open에서 `nonseekable_open()`) | `lseek()`가 `-ESPIPE` | 순수 스트림 (UART, 소켓 같은 것) |
| `.llseek = noop_llseek` | 성공하지만 위치는 안 바뀜 | seek를 시도하는 유저 프로그램을 깨뜨리지 않아야 할 때 |
| `.llseek = default_llseek` | `f_pos`를 그대로 바꿔 줌 (범위 검사 거의 없음) | 화이트보드 버전처럼 단순한 고정 버퍼 |
| `fixed_size_llseek` / `generic_file_llseek_size` 감싸기 | 범위 검사 + `SEEK_END` | `g4_sensorkit`처럼 크기가 정의되는 장치 |
| `stream_open()` (open에서 호출) | seek 불가 + `ppos`가 NULL로 옴 + `f_pos` 잠금 생략 | 진짜 스트림. FUSE·일부 tty 계열 |

`nonseekable_open()`은 `FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE`를 지워서 `pread`/`pwrite`까지 `-ESPIPE`로 만든다(랩 `fs/open.c:1638`).

---

## 6. `read()`와 `f_pos` — 부분 읽기, short read, EOF, `-EAGAIN`, overrun

### 6.1 규칙 한 장

| 상황 | 드라이버가 돌려줄 것 | 유저가 보는 것 |
|---|---|---|
| 요청보다 데이터가 많음 | `cnt` | 꽉 찬 read |
| 데이터가 1바이트 이상, 요청보다 적음 | 있는 만큼 (**short read**) | `n < cnt` — 오류 아님, 다시 부르면 됨 |
| 데이터 0, 앞으로도 안 옴 | `0` | EOF |
| 데이터 0, 곧 올 수 있음, 블로킹 | 잠들었다가 데이터가 오면 위 규칙 | 기다림 |
| 데이터 0, 곧 올 수 있음, `O_NONBLOCK` | `-EAGAIN` | `-1`, `errno = EAGAIN` |
| 잠든 중 시그널 | `-ERESTARTSYS` | `EINTR` 또는 자동 재시작 (F3 §4) |
| 유저 버퍼 주소 불량 | 일부 복사했으면 그 수, 아니면 `-EFAULT` | E6 §3.5 |

**0은 EOF다.** "지금은 데이터가 없다"는 뜻으로 0을 돌려주면 `cat`과 대부분의 프로그램이 거기서 끝내 버린다. 데이터가 없으면 잠들거나 `-EAGAIN`이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="60" y="30" text-anchor="middle" font-size="12">224</text>
  <text x="140" y="30" text-anchor="middle" font-size="12">256</text>
  <text x="220" y="30" text-anchor="middle" font-size="12">288</text>
  <text x="300" y="30" text-anchor="middle" font-size="12">320</text>
  <text x="380" y="30" text-anchor="middle" font-size="12">352</text>
  <text x="460" y="30" text-anchor="middle" font-size="12">384</text>
  <text x="540" y="30" text-anchor="middle" font-size="12">416</text>
  <text x="620" y="30" text-anchor="middle" font-size="12">448</text>
  <rect x="60" y="38" width="80" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="60" text-anchor="middle" font-size="12">seq 7</text>
  <rect x="140" y="38" width="80" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="60" text-anchor="middle" font-size="12">seq 8</text>
  <rect x="220" y="38" width="80" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="260" y="60" text-anchor="middle" font-size="12">seq 9</text>
  <rect x="300" y="38" width="80" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="60" text-anchor="middle" font-size="12">seq 10</text>
  <rect x="380" y="38" width="80" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="420" y="60" text-anchor="middle" font-size="12">seq 11</text>
  <rect x="460" y="38" width="80" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="60" text-anchor="middle" font-size="12">seq 12</text>
  <rect x="540" y="38" width="80" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="580" y="60" text-anchor="middle" font-size="12">seq 13</text>
  <line x1="60" y1="100" x2="85" y2="100" stroke="#3f9a6b" stroke-width="3" marker-end="url(#ah3)"/>
  <text x="95" y="104" font-size="12">① read(10) = 10 → f_pos 234 (seq 7의 10바이트째)</text>
  <line x1="85" y1="132" x2="140" y2="132" stroke="#3f9a6b" stroke-width="3" marker-end="url(#ah3)"/>
  <text x="150" y="136" font-size="12">② read(54) = 22 — seq 8이 아직 없어서 seq 7 나머지만</text>
  <line x1="140" y1="172" x2="540" y2="172" stroke="#3f9a6b" stroke-width="3" marker-end="url(#ah3)"/>
  <text x="340" y="164" text-anchor="middle" font-size="12">③ 50 ms 뒤 read(4096) = 160 — 5개뿐이라 short read</text>
  <circle cx="540" cy="206" r="5" fill="#d0564a"/>
  <text x="528" y="210" text-anchor="end" font-size="12">④ O_NONBLOCK, 새 데이터 없음 → -EAGAIN</text>
  <line x1="540" y1="240" x2="620" y2="240" stroke="#e08a3c" stroke-width="3" marker-end="url(#ah3)"/>
  <text x="528" y="244" text-anchor="end" font-size="12">⑤ stop 뒤: 남은 32 B를 주고 (seq 13)</text>
  <circle cx="620" cy="274" r="5" fill="#888"/>
  <text x="608" y="278" text-anchor="end" font-size="12">⑥ 그다음 read는 0 = EOF (정지 + 따라잡음)</text>
</svg>
```
그림 3 — 예제 7의 실제 실행을 바이트 축 위에 그린 것. 위 숫자는 바이트 오프셋(`f_pos`)이고 샘플 하나가 32바이트다. `f_pos`는 샘플 경계와 무관하게 "복사한 바이트만큼" 움직인다.

### 예제 7 — 한 minor에서 `f_pos`의 모든 경우

무엇을 확인하나: 그림 3의 ①~⑥, `SEEK_END` 기준 과거 읽기, 범위 밖 seek, 잘못된 명령, 그리고 링이 한 바퀴 넘게 돌았을 때의 overrun.

```c
/* g4_rd: f_pos, partial reads, short read, EAGAIN, EOF, llseek on one minor */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "g4_sensorkit.h"
#define SZ (long)sizeof(struct g4_sample)

static int fd;
static unsigned char raw[64 * 1024];
static void rd(const char *what, size_t n)
{
	ssize_t r = read(fd, raw, n);
	off_t pos = lseek(fd, 0, SEEK_CUR);
	printf("%-28s read(%5zu) = %5zd  %s  f_pos=%ld (seq %ld + %ld B)\n", what, n,
	       r, r < 0 ? strerror(errno) : "", (long)pos, pos / SZ, pos % SZ);
}
static void cmd(const char *c)
{
	if (write(fd, c, strlen(c)) < 0)
		printf("write(\"%s\") -> %s\n", c, strerror(errno));
}
int main(int argc, char **argv)
{
	fd = open(argc > 1 ? argv[1] : "/dev/g4imu0", O_RDWR);
	if (fd < 0) { perror("open"); exit(1); }
	printf("sizeof(struct g4_sample) = %ld, f_pos after open = %ld\n",
	       SZ, (long)lseek(fd, 0, SEEK_CUR));
	rd("1) 10 B (mid-sample)", 10);
	rd("2) 54 B", 54);
	usleep(50000);
	rd("3) 4096 B (short read)", 4096);
	fcntl(fd, F_SETFL, O_NONBLOCK);
	rd("4) O_NONBLOCK, caught up", 4096);
	fcntl(fd, F_SETFL, 0);
	cmd("stop\n");
	rd("5) after stop", 4096);
	rd("6) again", 4096);
	lseek(fd, -3 * SZ, SEEK_END);
	rd("7) SEEK_END-96, 3 samples", 4096);
	struct g4_sample *s = (struct g4_sample *)raw;
	for (int i = 0; i < 3; i++)
		printf("   seq=%u dev=%u acc=(%d,%d,%d) t=%llu\n", s[i].seq, s[i].dev,
		       s[i].acc[0], s[i].acc[1], s[i].acc[2], (unsigned long long)s[i].t_ns);
	off_t e = lseek(fd, 1, SEEK_END);
	printf("8) lseek(+1, SEEK_END) = %ld (%s)\n", (long)e, e < 0 ? strerror(errno) : "ok");
	cmd("bogus");
	cmd("start\n");
	rd("9) after start (blocks)", 4096);
	cmd("rate 5000");
	sleep(1);				/* > 1024 samples: ring wraps */
	rd("10) 1 s later at 5 kHz", 4096);
	cmd("rate 100");
	close(fd);
	return 0;
}
```

게스트 스크립트 `t_io.sh` 앞부분: `insmod g4_sensorkit.ko` 후 `./g4_rd /dev/g4imu0`.

```text
sizeof(struct g4_sample) = 32, f_pos after open = 224
1) 10 B (mid-sample)         read(   10) =    10    f_pos=234 (seq 7 + 10 B)
2) 54 B                      read(   54) =    22    f_pos=256 (seq 8 + 0 B)
3) 4096 B (short read)       read( 4096) =   160    f_pos=416 (seq 13 + 0 B)
4) O_NONBLOCK, caught up     read( 4096) =    -1  Resource temporarily unavailable  f_pos=416 (seq 13 + 0 B)
5) after stop                read( 4096) =    32    f_pos=448 (seq 14 + 0 B)
6) again                     read( 4096) =     0    f_pos=448 (seq 14 + 0 B)
7) SEEK_END-96, 3 samples    read( 4096) =    96    f_pos=448 (seq 14 + 0 B)
   seq=11 dev=0 acc=(-89,0,1000) t=25402063664
   seq=12 dev=0 acc=(-88,0,1000) t=25411470560
   seq=13 dev=0 acc=(-87,0,1000) t=25422039872
8) lseek(+1, SEEK_END) = -1 (Invalid argument)
write("bogus") -> Invalid argument
9) after start (blocks)      read( 4096) =    32    f_pos=480 (seq 15 + 0 B)
10) 1 s later at 5 kHz       read( 4096) =  4096    f_pos=93984 (seq 2937 + 0 B)
```

같은 실행의 dmesg에서 이 open의 release 줄:

```text
[   30.439212] g4: close imu0 bytes=4448 lost=2794
```

출력에서 볼 것:

- **open 직후 `f_pos = 224`** = 모듈 로드 후 이미 7개 샘플이 만들어졌다(7 × 32). open이 "지금"으로 맞춰 둔 것이다.
- **①②**: 10바이트를 읽으면 `f_pos`가 샘플 중간(seq 7 + 10 B)에 선다. 54바이트를 요청했지만 22바이트만 왔다 — seq 7의 나머지 22바이트만 있고 seq 8은 아직 안 만들어졌기 때문이다(100 Hz = 10 ms마다 하나). **데이터가 조금이라도 있으면 기다리지 않는다**는 규칙이 그대로 보인다.
- **③**: 50 ms 쉬고 4096바이트를 요청하자 160바이트(5샘플)만 왔다. 이 short read는 정상이다. 유저 코드는 반드시 반환값만큼만 처리해야 한다.
- **④**: 따라잡은 상태에서 `O_NONBLOCK`이면 `EAGAIN`, `f_pos`는 그대로.
- **⑤⑥**: `stop` 직전에 샘플 하나가 더 생겨 있어서 먼저 32바이트를 주고, 그다음에야 `0`(EOF). EOF는 "정지 + 다 읽음"일 때만 나온다. 다른 실행에서는 `stop` 전 새 샘플이 없어 ⑤가 바로 0이었다 — 타이밍에 따라 달라진다.
- **⑦**: `lseek(fd, -96, SEEK_END)`로 마지막 3샘플(seq 11~13)을 다시 읽었다. 같은 데이터를 두 번 읽을 수 있다 — 소비형 FIFO(F3의 kfifo)와 다른 "히스토리 파일" 의미다. `t_ns` 간격이 약 10 ms(10,000,000 ns 근처)다.
- **⑧**: 미래로의 seek는 `fixed_size_llseek`가 `-EINVAL`로 막는다.
- **⑩**: 5 kHz로 1초 → 링(1024샘플)보다 훨씬 많이 쌓였다. `f_pos`가 가리키던 샘플은 이미 덮어써져서 드라이버가 가장 오래된 샘플로 건너뛰었고, release 로그의 `lost=2794`가 잃은 샘플 수다. 4096바이트 = 128샘플을 읽은 뒤의 `f_pos`(seq 2937)가 "oldest + 128"이다.

### 6.2 `f_pos` 설계의 선택지

`g4_sensorkit`은 byte-granular `f_pos`를 골랐지만, 실제 커널에는 다른 선택도 많다.

| 설계 | 예 | read 규칙 |
|---|---|---|
| 바이트 스트림 + 위치 의미 있음 | `g4_sensorkit`, `/dev/mem` | `*ppos` 기준으로 복사, `llseek` 지원 |
| 레코드 단위 + 위치 없음 | `evdev`(`/dev/input/eventN`) | `cnt < sizeof(struct input_event)`면 `-EINVAL`, 항상 레코드 경계로만 반환. 클라이언트마다 자기 큐(per-open 버퍼) |
| 소비형 FIFO + 위치 없음 | F3의 `f3_queue`, 대부분의 UART | 읽으면 사라진다. `nonseekable_open()`/`stream_open()` |
| 고정 크기 내용 | 화이트보드 `g4_wb`, sysfs 속성 | `simple_read_from_buffer()` 한 줄 |

인터뷰에서 "센서 데이터를 char device로 내보낸다면 read 의미를 어떻게 정의하겠나"는 좋은 꼬리 질문이다. 정답은 하나가 아니고 **레코드 경계, 여러 리더, overrun 처리, seek 가능 여부**를 의식적으로 정했는지가 중요하다. 실제 IMU라면 IIO 서브시스템(I 모듈)이 이 모든 결정을 표준화해 준다.

---

## 7. 동시성 — 여러 opener, per-open vs per-device, 락

### 7.1 그림으로 — 무엇이 누구 것인가

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="200" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="35" text-anchor="middle" font-size="13">g4_open #1</text>
  <text x="120" y="54" text-anchor="middle" font-size="12">bytes · lost · held</text>
  <rect x="240" y="14" width="200" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="35" text-anchor="middle" font-size="13">g4_open #2</text>
  <text x="340" y="54" text-anchor="middle" font-size="12">bytes · lost · held</text>
  <rect x="460" y="14" width="200" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="35" text-anchor="middle" font-size="13">g4_open #3</text>
  <text x="560" y="54" text-anchor="middle" font-size="12">bytes · lost · held</text>
  <rect x="20" y="100" width="200" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="122" text-anchor="middle" font-size="13">struct file (A의 fd 3)</text>
  <text x="120" y="141" text-anchor="middle" font-size="12">f_pos = 123456</text>
  <text x="120" y="159" text-anchor="middle" font-size="12">f_flags, f_op</text>
  <rect x="240" y="100" width="200" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="122" text-anchor="middle" font-size="13">struct file (A의 fd 4)</text>
  <text x="340" y="141" text-anchor="middle" font-size="12">f_pos = 123392 (따로)</text>
  <text x="340" y="159" text-anchor="middle" font-size="12">f_flags, f_op</text>
  <rect x="460" y="100" width="200" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="560" y="122" text-anchor="middle" font-size="13">struct file (A의 fd 5)</text>
  <text x="560" y="141" text-anchor="middle" font-size="12">f_pos = 3968</text>
  <text x="560" y="159" text-anchor="middle" font-size="12">f_flags, f_op</text>
  <line x1="120" y1="100" x2="120" y2="66" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="340" y1="100" x2="340" y2="66" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="560" y1="100" x2="560" y2="66" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="128" y="88" font-size="12">private_data</text>
  <line x1="120" y1="170" x2="200" y2="218" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="340" y1="170" x2="260" y2="218" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="560" y1="170" x2="560" y2="218" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="270" y="196" font-size="12">f_inode</text>
  <rect x="20" y="220" width="420" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="230" y="241" text-anchor="middle" font-size="13">inode /dev/g4imu0 (i_rdev 510:0)</text>
  <text x="230" y="260" text-anchor="middle" font-size="12">i_cdev → &amp;g4_dev[0].cdev (chrdev_open이 캐시)</text>
  <rect x="460" y="220" width="200" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="560" y="241" text-anchor="middle" font-size="13">inode /dev/g4imu1</text>
  <text x="560" y="260" text-anchor="middle" font-size="12">i_rdev 510:1</text>
  <line x1="230" y1="270" x2="230" y2="318" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="560" y1="270" x2="560" y2="318" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="240" y="298" font-size="12">container_of(i_cdev, struct g4_dev, cdev)</text>
  <rect x="20" y="320" width="420" height="84" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="230" y="342" text-anchor="middle" font-size="13">struct g4_dev[0] — 장치당 하나, 모두가 공유</text>
  <text x="230" y="362" text-anchor="middle" font-size="12">cdev · ring[1024] · head · wq · hrtimer</text>
  <text x="230" y="380" text-anchor="middle" font-size="12">ring_lock (spinlock) · cfg_lock (mutex) · busy 비트</text>
  <text x="230" y="397" text-anchor="middle" font-size="12">→ 락이 필요한 것은 여기뿐</text>
  <rect x="460" y="320" width="200" height="84" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560" y="342" text-anchor="middle" font-size="13">struct g4_dev[1]</text>
  <text x="560" y="362" text-anchor="middle" font-size="12">자기 링 · 자기 타이머</text>
  <text x="560" y="380" text-anchor="middle" font-size="12">imu0과 상태 공유 없음</text>
</svg>
```
그림 4 — 같은 minor를 두 번 열면 `struct file`(그리고 그 안의 `f_pos`)과 `g4_open`은 둘, inode와 `g4_dev`는 하나다. 다른 minor는 다른 `g4_dev`다. 숫자는 예제 8의 실제 값.

정리하면:

| 상태 | 어디에 두나 | 수명 | 락 |
|---|---|---|---|
| 읽기 위치 | `file->f_pos` (VFS가 관리) | open ~ 마지막 close | 같은 file을 두 스레드가 동시에 read하면 경쟁 (아래 7.4) |
| open별 통계·소유권 | `file->private_data` → `g4_open` | open ~ release | 보통 불필요 |
| 장치 데이터·HW 상태 | `g4_dev` (`container_of`로 찾음) | probe ~ remove | **필요** |
| 드라이버 전역 (번호, class) | 전역 변수 | 모듈 로드 ~ 언로드 | init/exit에서만 바뀌면 불필요 |

펌웨어로 옮기면: `g4_dev`는 "IMU 드라이버 인스턴스 구조체", `g4_open`은 "이 IMU를 구독한 태스크마다의 구독 핸들"이다. 여러 태스크가 같은 링을 읽되 각자 자기 read 인덱스를 갖는 **멀티 컨슈머 링버퍼**와 같다.

### 예제 8 — 같은 minor 두 번, 다른 minor, 잠든 리더 둘

무엇을 확인하나: per-open `f_pos`가 독립인지, 두 리더가 같은 샘플을 받는지(브로드캐스트), 다른 minor는 다른 인스턴스인지, 같은 minor에서 잠든 프로세스 둘이 한 샘플에 같이 깨는지.

```c
/* g4_multi: several openers — per-open f_pos, shared samples, -EBUSY */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "g4_sensorkit.h"
#define SZ (long)sizeof(struct g4_sample)

static int op(const char *tag, const char *path)
{
	int fd = open(path, O_RDONLY);
	printf("%-3s open(%s) = %d %s\n", tag, path, fd, fd < 0 ? strerror(errno) : "");
	return fd;
}
static void one(const char *tag, int fd)	/* blocking read of 1 sample */
{
	struct g4_sample s;
	struct timespec a, b;
	clock_gettime(CLOCK_MONOTONIC, &a);
	ssize_t n = read(fd, &s, sizeof(s));
	clock_gettime(CLOCK_MONOTONIC, &b);
	long us = (b.tv_sec - a.tv_sec) * 1000000 + (b.tv_nsec - a.tv_nsec) / 1000;
	printf("%-3s pid %d read=%zd dev=%u seq=%u waited %ld us, f_pos=%ld\n", tag,
	       getpid(), n, s.dev, s.seq, us, (long)lseek(fd, 0, SEEK_CUR));
}
int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "excl")) {
		int a = op("A", "/dev/g4imu0"), b = op("B", "/dev/g4imu0");
		int c = op("C", "/dev/g4imu1");
		close(a);
		int d = op("D", "/dev/g4imu0");
		close(b); close(c); close(d);
		return 0;
	}
	int a = op("A", "/dev/g4imu0"), b = op("B", "/dev/g4imu0");
	int c = op("C", "/dev/g4imu1");
	if (a < 0 || b < 0 || c < 0)
		exit(1);
	one("A", a); one("A", a); one("A", a);
	one("B", b);					/* B's f_pos is its own */
	one("C", c);
	for (int i = 0; i < 2; i++)
		if (fork() == 0) {			/* 2 sleepers on one minor */
			int fd = op("K", "/dev/g4imu0");
			one("K", fd);
			_exit(0);
		}
	while (wait(NULL) > 0)
		;
	return 0;
}
```

(`t_io.sh`에서 예제 7 바로 다음에 `./g4_multi` — 그래서 imu0의 seq가 예제 7의 5 kHz 구간만큼 크다.)

```text
A   open(/dev/g4imu0) = 3 
B   open(/dev/g4imu0) = 4 
C   open(/dev/g4imu1) = 5 
A   pid 75 read=32 dev=0 seq=3855 waited 164 us, f_pos=123392
A   pid 75 read=32 dev=0 seq=3856 waited 10230 us, f_pos=123424
A   pid 75 read=32 dev=0 seq=3857 waited 8999 us, f_pos=123456
B   pid 75 read=32 dev=0 seq=3855 waited 100 us, f_pos=123392
C   pid 75 read=32 dev=1 seq=123 waited 81 us, f_pos=3968
K   open(/dev/g4imu0) = 6 
K   open(/dev/g4imu0) = 6 
K   pid 76 read=32 dev=0 seq=3859 waited 5820 us, f_pos=123520
K   pid 77 read=32 dev=0 seq=3859 waited 1920 us, f_pos=123520
```

같은 실행의 dmesg:

```text
[   30.511636] g4: open  imu0 minor=0 pos=123360 excl=0
[   30.516698] g4: open  imu0 minor=0 pos=123360 excl=0
[   30.517589] g4: open  imu1 minor=1 pos=3936 excl=0
[   30.550371] g4: open  imu0 minor=0 pos=123488 excl=0
[   30.554509] g4: open  imu0 minor=0 pos=123488 excl=0
[   30.563679] g4: close imu0 bytes=32 lost=0
[   30.563680] g4: close imu0 bytes=32 lost=0
[   30.570092] g4: close imu1 bytes=32 lost=0
[   30.570196] g4: close imu0 bytes=32 lost=0
[   30.570234] g4: close imu0 bytes=96 lost=0
```

출력에서 볼 것:

- A와 B는 같은 `/dev/g4imu0`이지만 `f_pos`가 따로다. A가 세 번 읽어 seq 3857까지 갔어도 B의 첫 read는 **seq 3855** — A가 이미 읽은 샘플을 B도 받는다(브로드캐스트). 소비형 FIFO였다면 B는 3858을 받았을 것이다.
- A의 2·3번째 read는 약 10 ms(10230 µs, 8999 µs) 기다렸다 — 100 Hz 샘플 하나를 기다린 blocking read다. 첫 read는 이미 샘플이 있어 기다리지 않았다.
- C(`/dev/g4imu1`)는 `dev=1`, seq 123 — 자기 링, 자기 카운터다. imu1은 예제 7의 `rate 5000`을 받지 않았으므로 100 Hz로 쭉 123개쯤 만들었다.
- fork한 K 둘은 **같은 seq 3859를 받았다.** 둘 다 같은 wait queue에서 잤고 `wake_up_interruptible()`이 non-exclusive 대기자를 모두 깨웠다(F3 §6). 브로드캐스트 의미에서는 이게 맞다.
- dmesg의 close 줄 다섯 개 — `release`는 open마다 한 번씩, 각자의 `g4_open` 통계(`bytes=96`은 A)를 찍고 해제했다.

### 예제 9 — 단일 open 정책 (`-EBUSY`)

무엇을 확인하나: 모듈 파라미터 `excl`을 런타임에 켜고(권한 0644라 sysfs로 쓸 수 있다) 같은 minor 두 번째 open이 거부되는지, 다른 minor는 영향이 없는지, close 후 다시 열리는지.

```sh
echo 1 > /sys/module/g4_sensorkit/parameters/excl; ./g4_multi excl
```

```text
A   open(/dev/g4imu0) = 3 
B   open(/dev/g4imu0) = -1 Device or resource busy
C   open(/dev/g4imu1) = 4 
D   open(/dev/g4imu0) = 3 
```

```text
[   30.613689] g4: open  imu0 minor=0 pos=123680 excl=1
[   30.620955] g4: open  imu1 minor=1 pos=4256 excl=1
[   30.621520] g4: close imu0 bytes=0 lost=0
[   30.621797] g4: open  imu0 minor=0 pos=123680 excl=1
[   30.622358] g4: close imu1 bytes=0 lost=0
[   30.622478] g4: close imu0 bytes=0 lost=0
```

출력에서 볼 것: B의 거부는 dmesg에 open 줄이 없다 — `g4_open`이 `test_and_set_bit`에서 바로 `-EBUSY`를 돌려줬기 때문이다. imu1은 별개 비트라 열린다. A를 닫자(`release`에서 `clear_bit`) D가 다시 연다.

단일 open이 필요한 경우: HW가 "하나의 설정"만 가질 수 있는 장치(카메라 센서의 모드, 모터 컨트롤러의 제어권). 주의할 점이 둘 있다.

- **open을 막아도 fd 공유는 못 막는다.** `fork()`나 `dup()`, `SCM_RIGHTS`로 넘긴 fd는 같은 `struct file`이라 open이 다시 불리지 않는다. "한 프로세스만"이 아니라 "한 open file description만"이다.
- 블로킹 대안: `-EBUSY` 대신 open에서 잠들어 기다리게 할 수도 있다(`O_NONBLOCK`이면 `-EAGAIN`). LDD3의 `scullwuid`가 그 예다. 대부분은 `-EBUSY`가 더 예측 가능하다.

### 7.3 왜 mutex 하나로 안 되나 — 락 두 개의 이유

`g4_dev`에는 락이 두 개다.

| 락 | 보호 대상 | 경쟁 상대 | 왜 이 종류 |
|---|---|---|---|
| `ring_lock` (spinlock) | `ring[]`, `head` | hrtimer 콜백(**hardirq**) ↔ `read()` | hardirq에서는 잠들 수 없다 → mutex 불가. 프로세스 쪽은 `spin_lock_irq`로 로컬 IRQ를 끄고 잡는다(같은 CPU에서 콜백이 끼어들어 데드락 나는 것 방지) |
| `cfg_lock` (mutex) | `running`, 타이머 시작/정지 | `write()` ↔ `write()` | 둘 다 프로세스 문맥이고 `hrtimer_cancel()`은 콜백이 끝날 때까지 기다릴 수 있다 → 잠들 수 있는 락 |

그리고 "spinlock 안에서 `copy_to_user()` 금지" 때문에 read는 **락 안에서는 스택 버퍼로 복사, 락 밖에서 유저로 복사**하는 2단계를 쓴다. 이 모양이 면접 화이트보드에서 "ISR이 채우는 링버퍼를 read로 내보내라"의 모범 답안이다. 락이 맞는지는 debug 커널이 확인해 줬다 — 예제 7~9 전체를 lockdep + `DEBUG_ATOMIC_SLEEP` 커널에서 돌린 뒤의 검사:

```sh
dmesg | grep -E 'BUG|WARNING|lockdep|circular'
```

```text
[    0.000000] rcu: 	RCU lockdep checking is enabled.
```

부팅 배너 한 줄 외에 경고가 없다.

### 7.4 같은 `struct file`을 두 스레드가 동시에 read하면

VFS는 **일반 파일과 디렉터리에만** `FMODE_ATOMIC_POS`를 켠다(랩 `fs/open.c:933`: `if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)) f->f_mode |= FMODE_ATOMIC_POS;`). char device는 해당 없음 → 두 스레드가 같은 fd로 동시에 `read()` 하면 둘 다 같은 `f_pos`에서 시작해 같은 데이터를 받고 `f_pos`를 덮어쓸 수 있다. `g4_sensorkit`은 이 경우를 막지 않았다(같은 fd를 여러 스레드가 읽는 설계 자체가 드물다). 막으려면 per-open mutex를 `g4_open`에 두고 read 전체를 감싸면 된다. 면접에서 "f_pos는 누가 보호하나"가 나오면 이 사실이 답이다.

---

## 8. 모듈 언로드와 `.owner = THIS_MODULE`

### 8.1 무엇이 문제인가

열린 fd의 `file->f_op`는 **모듈의 메모리(.rodata)에 있는 `g4_fops`를 가리킨다.** 모듈이 언로드되면 그 메모리가 해제된다. 그 뒤 그 fd로 `read()`나 심지어 `close()`를 하면 커널이 해제된 주소에서 함수 포인터를 읽는다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="16" width="200" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="43" text-anchor="middle" font-size="13">open("/dev/g4imu0")</text>
  <line x1="220" y1="38" x2="258" y2="38" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="260" y="16" width="400" height="84" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="460" y="37" text-anchor="middle" font-size="13">chrdev_open()</text>
  <text x="460" y="58" text-anchor="middle" font-size="12">cdev_get(): try_module_get(cdev.owner) → +1</text>
  <text x="460" y="78" text-anchor="middle" font-size="12">fops_get(): try_module_get(fops.owner) → +1</text>
  <text x="460" y="94" text-anchor="middle" font-size="12">마지막 fput()에서 둘 다 module_put()</text>
  <line x1="460" y1="100" x2="460" y2="128" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="260" y="130" width="400" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="460" y="157" text-anchor="middle" font-size="13">lsmod: g4_sensorkit ... Used by 2</text>
  <rect x="20" y="130" width="200" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="157" text-anchor="middle" font-size="13">rmmod g4_sensorkit</text>
  <line x1="120" y1="174" x2="120" y2="198" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="20" y="200" width="640" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="227" text-anchor="middle" font-size="13">delete_module → try_stop_module: refcnt ≠ 0 → -EWOULDBLOCK (언로드 거부)</text>
  <rect x="20" y="262" width="640" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="284" text-anchor="middle" font-size="13">.owner = NULL이면: refcnt 0 → 언로드 성공 → file->f_op가 해제된 메모리</text>
  <text x="340" y="305" text-anchor="middle" font-size="12">그 fd로 read/close → 커널이 해제된 fops에서 함수 포인터를 읽다가 Oops</text>
</svg>
```
그림 5 — `.owner`는 "이 fops 테이블의 주인 모듈"이다. open 경로가 주인의 참조 카운트를 올려 두기 때문에 열린 fd가 있는 동안 모듈이 사라지지 않는다.

랩 소스로 확인: `fs/char_dev.c`의 `cdev_get()`이 `try_module_get(p->owner)`를, `include/linux/fs.h`의 `fops_get()` 매크로가 `try_module_get((_fops)->owner)`를 한다. `kernel/module/main.c`의 `try_stop_module()`은 참조가 남아 있으면 `-EWOULDBLOCK`을 돌려준다.

### 예제 10 — 열린 fd가 있으면 rmmod가 거부된다

무엇을 확인하나: refcount 값과 rmmod 결과. 셸의 `exec 3< /dev/g4imu0`로 fd 3을 열어 둔다(게스트 스크립트 `t_unload.sh`).

```sh
cd /g4
insmod g4_sensorkit.ko
exec 3< /dev/g4imu0
lsmod | grep g4
rmmod g4_sensorkit; echo "rmmod rc=$?"
exec 3<&-
lsmod | grep g4
rmmod g4_sensorkit; echo "rmmod rc=$?"; ls /dev/g4imu* 2>&1; grep g4 /proc/devices; dmesg | grep 'g4:' | tail -3
```

```text
g4_sensorkit 12288 2 - Live 0xffffbe54bdda9000 (O)
rmmod: remove 'g4_sensorkit': Resource temporarily unavailable
rmmod rc=0
g4_sensorkit 12288 0 - Live 0xffffbe54bdda9000 (O)
rmmod rc=0
ls: /dev/g4imu*: No such file or directory
[   14.136546] g4: open  imu0 minor=0 pos=96 excl=0
[   14.257878] g4: close imu0 bytes=0 lost=0
[   14.478800] g4: unloaded
```

출력에서 볼 것:

- fd 하나를 열었는데 **refcount가 2**다. 그림 5대로 `cdev_get()`(cdev.owner)과 `fops_get()`(fops.owner)이 각각 하나씩 올렸다. 그래서 `cdev.owner`와 `fops.owner`를 둘 다 `THIS_MODULE`로 두는 게 정석이다.
- `rmmod`는 "Resource temporarily unavailable" = `EWOULDBLOCK`(=`EAGAIN`)으로 거부됐다. **이 BusyBox `rmmod`는 실패해도 종료 코드 0을 돌려줬다** — 스크립트에서 성공 여부는 `lsmod`나 `/proc/modules`로 확인하는 편이 안전하다.
- fd를 닫자 refcount 0, 그다음 rmmod 성공, `/dev/g4imu*` 노드와 `/proc/devices` 항목이 사라졌다(`device_destroy`가 devtmpfs 노드도 지운다).

### 예제 11 — `.owner = NULL`이면 (의도적인 버그)

무엇을 확인하나: 같은 드라이버를 `G4_NO_OWNER`로 빌드한 `g4_noown.ko`에서 같은 시나리오. (게스트를 망가뜨리는 실험이라 TCG 게스트에서만 한다.)

```sh
insmod g4_noown.ko
exec 3< /dev/g4imu0
lsmod | grep g4
rmmod g4_noown 3<&-; echo "rmmod rc=$?"
```

```text
g4_noown 12288 0 - Live 0xffffc648ce8f9000 (O)
[   16.242958] Unable to handle kernel paging request at virtual address ffffc648ce8fd228
[   16.243170] Mem abort info:
[   16.243215]   ESR = 0x0000000096000007
[   16.243280]   EC = 0x25: DABT (current EL), IL = 32 bits
...
[   16.245517] Internal error: Oops: 0000000096000007 [#1]  SMP
[   16.251817] Modules linked in: [last unloaded: g4_noown(O)]
[   16.252755] CPU: 0 UID: 0 PID: 72 Comm: sh Tainted: G           O        6.18.55-dbg #1 PREEMPT 
...
[   16.253600] pc : filp_flush+0x3c/0xc8
[   16.254697] lr : __arm64_sys_close+0x34/0x98
...
[   16.256573] Call trace:
[   16.256847]  filp_flush+0x3c/0xc8 (P)
[   16.257048]  __arm64_sys_close+0x34/0x98
[   16.257132]  invoke_syscall.constprop.0+0x48/0xf0
[   16.257247]  el0_svc_common.constprop.0+0x40/0xf0
[   16.257336]  do_el0_svc+0x24/0x40
[   16.257684]  el0_svc+0x50/0x2f8
[   16.257923]  el0t_64_sync_handler+0xa0/0xe8
[   16.258150]  el0t_64_sync+0x198/0x1a0
```

(예제 10과는 다른 부팅에서 실행한 것이라 모듈 주소가 다르다. Oops는 레지스터 덤프 등 일부를 `...`로 생략했다. 이 뒤로 같은 Oops가 한 번 더 나고 게스트가 멈춰서 `timeout`이 QEMU를 끝냈다.)

출력에서 볼 것:

- fd가 열려 있는데 `lsmod`의 refcount가 **0**이다. 그래서 `rmmod`가 바로 성공했다(`Modules linked in: [last unloaded: g4_noown(O)]`).
- 그다음 셸이 `3<&-` 리다이렉션으로 fd 3을 닫자(`Comm: sh`, `__arm64_sys_close`) `filp_flush()`가 `file->f_op->flush`를 읽으려다 **해제된 모듈 주소**(`0xffffc648ce8fd228` = 모듈 베이스 `0xffffc648ce8f9000` + `0x4228`, 모듈 안의 `g4_fops`)에서 data abort(`EC = 0x25: DABT`)가 났다. 드라이버 코드를 한 줄도 실행하지 않고, **fops 테이블을 읽는 것만으로** 죽었다.
- 처음 시도에서는 `3<&-` 없이 `rmmod g4_noown`을 했는데, 그때는 fd 3을 물려받은 **rmmod 프로세스 자신**이 언로드 성공 후 종료하면서 fd를 닫다가 똑같이 `filp_flush`에서 Oops가 났다(`Comm: rmmod`, `exit_files` → `put_files_struct` → `filp_close`).

이 실험이 `.owner` 한 줄의 존재 이유다. 언로드 레이스를 드라이버가 직접 막을 방법은 없다(드라이버 코드가 사라지는 것이 문제니까). 그래서 **커널이 "열려 있는 동안 주인 모듈을 잡아 둔다"를 해 주고, 드라이버는 주인이 누구인지만 알려 준다.**

---

## 9. 처리량 — read 크기에 따라 얼마나 달라지나

### 예제 12 — 정지된 32 KiB 링을 다양한 크기로 다시 읽기

무엇을 확인하나: 같은 데이터를 `read()` 크기만 바꿔 읽을 때 처리량. 비교 기준으로 같은 크기로 `/dev/zero`를 읽는다. 링을 5 kHz로 1초 채운 뒤 `stop`하고, `lseek(-32 KiB, SEEK_END)` → EOF까지 읽기를 16 MiB가 될 때까지 반복한다. lockdep 오버헤드를 빼려고 **일반 커널(`KERN=std`)** 에서 돌렸다.

```c
/* g4_bench: read throughput vs read() size over a 32 KiB stopped ring */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "g4_sensorkit.h"
#define RING_B (1024L * (long)sizeof(struct g4_sample))

static char buf[65536];
static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}
int main(void)
{
	int fd = open("/dev/g4imu0", O_RDWR), z = open("/dev/zero", O_RDONLY);
	if (fd < 0 || z < 0) { perror("open"); exit(1); }
	if (write(fd, "rate 5000", 9) < 0) { perror("rate"); exit(1); }
	sleep(1);					/* fill the ring */
	if (write(fd, "stop", 4) < 0) { perror("stop"); exit(1); }
	if (lseek(fd, 0, SEEK_END) < RING_B) { puts("ring not full"); exit(1); }
	printf("%6s %10s %10s %12s %12s\n", "size", "calls", "ns/call", "g4 MB/s", "zero MB/s");
	for (long sz = 32; sz <= 32768; sz *= 2) {
		long calls = 0, bytes = 0;
		double t0 = now();
		while (bytes < 16L * 1024 * 1024) {	/* 16 MiB per size */
			lseek(fd, -RING_B, SEEK_END);
			ssize_t n;
			while ((n = read(fd, buf, sz)) > 0)
				bytes += n, calls++;
			calls++;			/* the read() that hit EOF */
		}
		double t1 = now(), zb = 0, t2;
		while (zb < 16.0 * 1024 * 1024)
			zb += read(z, buf, sz);
		t2 = now();
		printf("%6ld %10ld %10.0f %12.1f %12.1f\n", sz, calls,
		       (t1 - t0) * 1e9 / calls, bytes / (t1 - t0) / 1e6, zb / (t2 - t1) / 1e6);
	}
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.elinux_scratch/G4 && KERN=std SMP=2 TMO=300 ./boot.sh t_bench.sh; cat /var/tmp/G4/out_t_bench.txt'
```

```text
lab initramfs: 6.18.55 up 1.38s
  size      calls    ns/call      g4 MB/s    zero MB/s
    32     524800      34718          0.9          1.2
    64     262656      27363          2.3          2.5
   128     131584      23555          5.4          6.0
   256      66048      25117         10.1          9.9
   512      33280      29272         17.2         21.9
  1024      16896      28404         35.0         42.6
  2048       8704      32256         59.8         64.2
  4096       4608      35085        103.8        159.4
  8192       2560      43734        149.9        284.6
 16384       1536      50259        217.3        511.1
 32768       1024      62172        263.5        833.7
```

```svg
<svg viewBox="0 0 640 360" xmlns="http://www.w3.org/2000/svg">
<line x1="80" y1="290" x2="600" y2="290" stroke="currentColor"/>
<line x1="80" y1="290" x2="80" y2="40" stroke="currentColor"/>
<line x1="80" y1="267.2" x2="600" y2="267.2" stroke="#888" stroke-opacity="0.35"/>
<text x="72" y="271.2" text-anchor="end" font-size="12">1</text>
<line x1="80" y1="191.5" x2="600" y2="191.5" stroke="#888" stroke-opacity="0.35"/>
<text x="72" y="195.5" text-anchor="end" font-size="12">10</text>
<line x1="80" y1="115.7" x2="600" y2="115.7" stroke="#888" stroke-opacity="0.35"/>
<text x="72" y="119.7" text-anchor="end" font-size="12">100</text>
<line x1="80" y1="40.0" x2="600" y2="40.0" stroke="#888" stroke-opacity="0.35"/>
<text x="72" y="44.0" text-anchor="end" font-size="12">1000</text>
<line x1="80.0" y1="290" x2="80.0" y2="295" stroke="currentColor"/>
<text x="80.0" y="309" text-anchor="middle" font-size="12">32</text>
<line x1="132.0" y1="290" x2="132.0" y2="295" stroke="currentColor"/>
<text x="132.0" y="309" text-anchor="middle" font-size="12">64</text>
<line x1="184.0" y1="290" x2="184.0" y2="295" stroke="currentColor"/>
<text x="184.0" y="309" text-anchor="middle" font-size="12">128</text>
<line x1="236.0" y1="290" x2="236.0" y2="295" stroke="currentColor"/>
<text x="236.0" y="309" text-anchor="middle" font-size="12">256</text>
<line x1="288.0" y1="290" x2="288.0" y2="295" stroke="currentColor"/>
<text x="288.0" y="309" text-anchor="middle" font-size="12">512</text>
<line x1="340.0" y1="290" x2="340.0" y2="295" stroke="currentColor"/>
<text x="340.0" y="309" text-anchor="middle" font-size="12">1K</text>
<line x1="392.0" y1="290" x2="392.0" y2="295" stroke="currentColor"/>
<text x="392.0" y="309" text-anchor="middle" font-size="12">2K</text>
<line x1="444.0" y1="290" x2="444.0" y2="295" stroke="currentColor"/>
<text x="444.0" y="309" text-anchor="middle" font-size="12">4K</text>
<line x1="496.0" y1="290" x2="496.0" y2="295" stroke="currentColor"/>
<text x="496.0" y="309" text-anchor="middle" font-size="12">8K</text>
<line x1="548.0" y1="290" x2="548.0" y2="295" stroke="currentColor"/>
<text x="548.0" y="309" text-anchor="middle" font-size="12">16K</text>
<line x1="600.0" y1="290" x2="600.0" y2="295" stroke="currentColor"/>
<text x="600.0" y="309" text-anchor="middle" font-size="12">32K</text>
<polyline points="80.0,261.2 132.0,237.1 184.0,208.3 236.0,191.8 288.0,165.7 340.0,143.8 392.0,130.3 444.0,100.4 496.0,81.3 548.0,62.1 600.0,46.0" fill="none" stroke="#888" stroke-width="2"/>
<circle cx="80.0" cy="261.2" r="3.5" fill="#888"/>
<circle cx="132.0" cy="237.1" r="3.5" fill="#888"/>
<circle cx="184.0" cy="208.3" r="3.5" fill="#888"/>
<circle cx="236.0" cy="191.8" r="3.5" fill="#888"/>
<circle cx="288.0" cy="165.7" r="3.5" fill="#888"/>
<circle cx="340.0" cy="143.8" r="3.5" fill="#888"/>
<circle cx="392.0" cy="130.3" r="3.5" fill="#888"/>
<circle cx="444.0" cy="100.4" r="3.5" fill="#888"/>
<circle cx="496.0" cy="81.3" r="3.5" fill="#888"/>
<circle cx="548.0" cy="62.1" r="3.5" fill="#888"/>
<circle cx="600.0" cy="46.0" r="3.5" fill="#888"/>
<polyline points="80.0,270.7 132.0,239.8 184.0,211.7 236.0,191.1 288.0,173.6 340.0,150.3 392.0,132.6 444.0,114.5 496.0,102.4 548.0,90.2 600.0,83.9" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="80.0" cy="270.7" r="3.5" fill="#4a7bd0"/>
<circle cx="132.0" cy="239.8" r="3.5" fill="#4a7bd0"/>
<circle cx="184.0" cy="211.7" r="3.5" fill="#4a7bd0"/>
<circle cx="236.0" cy="191.1" r="3.5" fill="#4a7bd0"/>
<circle cx="288.0" cy="173.6" r="3.5" fill="#4a7bd0"/>
<circle cx="340.0" cy="150.3" r="3.5" fill="#4a7bd0"/>
<circle cx="392.0" cy="132.6" r="3.5" fill="#4a7bd0"/>
<circle cx="444.0" cy="114.5" r="3.5" fill="#4a7bd0"/>
<circle cx="496.0" cy="102.4" r="3.5" fill="#4a7bd0"/>
<circle cx="548.0" cy="90.2" r="3.5" fill="#4a7bd0"/>
<circle cx="600.0" cy="83.9" r="3.5" fill="#4a7bd0"/>
<text x="86.0" y="286.7" font-size="12">0.9</text>
<text x="450.0" y="130.5" font-size="12">103.8</text>
<text x="594.0" y="101.9" text-anchor="end" font-size="12">263.5</text>
<text x="594.0" y="38.0" text-anchor="end" font-size="12">833.7</text>
<text x="340" y="330" text-anchor="middle" font-size="13">read() 한 번에 요청한 크기 (bytes, log2)</text>
<text x="20" y="165" text-anchor="middle" font-size="13" transform="rotate(-90 20 165)">MB/s (log10)</text>
<rect x="100" y="48" width="14" height="4" fill="#4a7bd0"/><text x="120" y="54" font-size="12">/dev/g4imu0 (정지된 32 KiB 링)</text>
<rect x="100" y="66" width="14" height="4" fill="#888"/><text x="120" y="72" font-size="12">/dev/zero (기준선)</text>
</svg>
```
그림 6 — 예제 12의 측정값(TCG 게스트, 일반 커널 6.18.55). 두 축 모두 로그. 절대값은 실제 보드보다 훨씬 느리고 기울기만 의미가 있다.

숫자 읽는 법:

- **32 ~ 1024 B 구간은 직선(기울기 1)** 이다. ns/call이 23~35 µs로 거의 일정하니 처리량 = 크기 / 고정 비용. 이 구간의 비용은 **syscall 진입·복귀**(E6 §7)가 지배한다. TCG에서는 syscall 하나가 수십 µs라 절대값이 크다. 실제 Cortex-A 보드라면 수백 ns 단위일 것이다(이 노트에서 측정하지 않았다).
- 32바이트(샘플 하나)씩 읽으면 0.9 MB/s, 32 KiB씩 읽으면 263 MB/s — **약 290배.** 센서 데몬이 샘플 하나마다 `read()` 하는 코드는 고주파 IMU(수 kHz)에서 CPU를 쓸데없이 먹는다. 한 번에 여러 샘플을 읽고(버퍼 크기 = 샘플 크기 × N), 데이터 도착은 `poll`(G6)로 기다리는 게 정석이다.
- **4 KiB 이상에서 g4가 `/dev/zero`보다 확실히 느려진다.** `g4_read`는 8샘플(256 B)마다 spinlock을 잡고 스택에 복사한 뒤 `copy_to_user`를 한 번 하므로, 큰 read 한 번 안에서도 256 B마다 고정 비용이 붙는다. `/dev/zero`는 페이지 단위로 한 번에 처리한다. 더 빠르게 하려면 청크를 키우거나(스택 대신 per-open 버퍼), 근본적으로는 `mmap`으로 링을 유저에 직접 보이게 한다(G5) — 그때는 `read()` 복사가 아예 없다.

---

## 10. 화이트보드 버전 — 40줄 char driver와 모범 답안

### 10.1 코드 (include 포함 40줄, 실제 빌드·실행함)

```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
static dev_t devt; static struct cdev wb_cdev; static struct class *cls;
static const char msg[] = "hello from g4_wb\n";
static ssize_t wb_read(struct file *f, char __user *buf, size_t n, loff_t *pos)
{
	if (*pos >= sizeof(msg) - 1)
		return 0;					/* EOF */
	n = min(n, (size_t)(sizeof(msg) - 1 - *pos));		/* short read */
	if (copy_to_user(buf, msg + *pos, n))
		return -EFAULT;
	*pos += n;
	return n;
}
static const struct file_operations wb_fops = {
	.owner = THIS_MODULE, .read = wb_read, .llseek = default_llseek,
};
static int __init wb_init(void)
{
	int ret = alloc_chrdev_region(&devt, 0, 1, "g4_wb");	/* 1. number */
	if (ret) return ret;
	cdev_init(&wb_cdev, &wb_fops);				/* 2. cdev */
	if ((ret = cdev_add(&wb_cdev, devt, 1))) goto e1;
	cls = class_create("g4_wb");				/* 3. node */
	if (IS_ERR(cls)) { ret = PTR_ERR(cls); goto e2; }
	if (!IS_ERR(device_create(cls, NULL, devt, NULL, "g4_wb"))) return 0;
	ret = -ENODEV; class_destroy(cls);
e2:	cdev_del(&wb_cdev);
e1:	unregister_chrdev_region(devt, 1);
	return ret;
}
static void __exit wb_exit(void)
{
	device_destroy(cls, devt); class_destroy(cls);
	cdev_del(&wb_cdev); unregister_chrdev_region(devt, 1);
}
module_init(wb_init); module_exit(wb_exit); MODULE_LICENSE("GPL"); MODULE_DESCRIPTION("G4 whiteboard");
```

(화이트보드에서는 줄 수를 맞추려고 한 줄에 여러 문장을 썼다. 실제 코드에서는 kernel coding style대로 나눈다. `wb_read` 다섯 줄은 `return simple_read_from_buffer(buf, n, pos, msg, sizeof(msg) - 1);` 한 줄로 바꿔도 된다 — 면접에서는 먼저 직접 쓰고 "헬퍼가 있다"고 덧붙이는 게 좋다.)

### 예제 13 — 화이트보드 버전 실행

```sh
insmod g4_wb.ko; ls -l --color=never /dev/g4_wb; cat /dev/g4_wb; dd if=/dev/g4_wb bs=5 2>/dev/null; echo
grep g4_wb /proc/devices; ls --color=never /sys/class/g4_wb/
rmmod g4_wb; echo "rmmod rc=$?"; ls /dev/g4_wb 2>&1
```

```text
crw-------    1 0        0         509,   0 Oct 11 01:42 /dev/g4_wb
hello from g4_wb
hello from g4_wb

509 g4_wb
g4_wb
rmmod rc=0
ls: /dev/g4_wb: No such file or directory
```

출력에서 볼 것: `dd bs=5`는 5바이트씩 3번 + 2바이트(short read) + 0(EOF), 합계 17바이트으로 같은 문자열을 만들었다 — `*pos` 처리가 맞다는 증거다. 모드가 `crw-------`인 것은 `.devnode`가 없어서 devtmpfs 기본값(0600)이 쓰였기 때문이다(`g4_sensorkit`의 0666과 비교). major 509는 같은 부팅에서 `g4_old`가 510을 먼저 가져간 뒤 할당된 번호다.

### 10.2 모범 답안 — 말로 설명하는 순서

화이트보드에 쓰면서 이 순서로 말한다.

1. "먼저 장치 번호를 받습니다. 새 드라이버라 major를 하드코딩하지 않고 `alloc_chrdev_region`으로 동적으로 받습니다."
2. "`cdev_init`으로 fops를 묶고 `cdev_add`로 등록합니다. 이 시점부터 open이 들어올 수 있으니 장치 상태 초기화는 이 전에 끝냅니다."
3. "`/dev` 노드는 class와 `device_create`로 만듭니다. 그러면 devtmpfs가 노드를 만들고 uevent가 나가서 udev가 권한을 정합니다. 6.4부터 `class_create`는 이름만 받습니다."
4. "read는 `*ppos`를 기준으로 남은 만큼만 `copy_to_user` 하고, 실제 복사한 만큼 `*ppos`를 올리고, 그 바이트 수를 반환합니다. 끝이면 0, 주소가 나쁘면 `-EFAULT`."
5. "fops의 `.owner = THIS_MODULE`은 열린 동안 모듈 언로드를 막습니다."
6. "에러 경로는 역순 goto로 풀고, exit은 init의 정확한 역순입니다."
7. (시간이 있으면) "장치가 여러 개면 per-device 구조체에 cdev를 박고 open에서 `container_of(inode->i_cdev, ...)`로 찾아 `file->private_data`에 둡니다. 공유 버퍼는 ISR과 공유하면 spinlock, 아니면 mutex, 그리고 spinlock 안에서는 `copy_to_user`를 하지 않습니다."

면접관이 자주 찌르는 곳: "`cdev_add`와 `device_create` 순서를 바꾸면?", "`copy_to_user` 반환값은 무엇인가?"(못 복사한 바이트 수 — E6), "`read`가 0을 반환하면?", "`.owner`를 빼면?"(예제 11), "`register_chrdev`와 차이는?"(예제 2의 1.4절).

---

## 11. 커널 안에서는 무슨 일이 일어나나

등록부터 read까지 한 장으로:

```
insmod
  g4_init
    alloc_chrdev_region -> __register_chrdev_region -> chrdevs[] (/proc/devices)
    class_register      -> /sys/class/g4_sensorkit, uevent add@/class/...
    g4_create(i)
      cdev_init / cdev_add -> kobj_map(cdev_map, 510:i)
      device_create -> device_add -> sysfs dir + "dev" attr
                                  -> devtmpfs_create_node -> /dev/g4imu<i>
                                  -> kobject_uevent(KOBJ_ADD) -> netlink -> udev/mdev
open("/dev/g4imu0")
  do_sys_openat2 -> path_openat -> do_dentry_open: f_op = def_chr_fops
    chrdev_open: kobj_lookup(cdev_map, i_rdev) -> inode->i_cdev = cdev
                 cdev_get (module ref +1), fops_get (module ref +1), replace_fops
      g4_open: container_of(i_cdev) -> g4_dev; kzalloc g4_open -> private_data
read(fd, buf, n)
  ksys_read -> vfs_read -> file->f_op->read == g4_read(file, buf, n, &pos)
    wait_event_interruptible(d->wq ...)  <- g4_tick (hrtimer, hardirq) wake_up
    spin_lock_irq / copy to stack / spin_unlock_irq / copy_to_user
  ksys_read writes pos back to file->f_pos
close(fd) (last reference)
  fput -> __fput -> f_op->release == g4_release -> fops_put/cdev_put (module ref -1, -1)
```

- open 경로의 VFS 부분(`do_sys_openat2` → `chrdev_open`)은 F12 §2, §4에서 ftrace로 봤다.
- read 경로의 `ksys_read` → `vfs_read` → `f_op->read`와 `*ppos` 왕복은 E6 §2.1.
- `wait_event_interruptible` / `wake_up_interruptible`의 내부와 lost-wakeup 안전성은 F3 §5.
- 드라이버를 쓸 때 다시 만나는 곳: **G5**(`unlocked_ioctl`, `mmap` → `remap_pfn_range`로 링을 유저에 매핑), **G6**(`.poll` + `poll_wait(file, &d->wq, ...)` — 지금의 `wq`를 그대로 쓴다), **G7**(misc device — 위 등록 단계 1~3을 `misc_register` 하나로), **G8**(per-device 설정을 sysfs 속성으로, `device_create`의 `drvdata` 사용), **G9**(uevent·udev rule).

---

## 12. Embedded 실무에서 다시 보기

### 12.1 "IIO로 할까, char device로 할까"

예를 들어 센서 키트의 IMU가 표준 부품(BMI088, ICM-42688 같은)이라면 **IIO 드라이버**(I 모듈)가 정답이다 — 버퍼·트리거·타임스탬프·sysfs 채널이 표준이고 유저 라이브러리(libiio)가 있다. 사내 FPGA가 여러 센서를 묶어 독자 패킷을 DMA로 올려 주는 경우처럼 **표준 서브시스템에 안 맞는 장치**가 char device(또는 misc device)로 간다. 면접에서 "char device를 직접 만들겠다"고 하기 전에 "맞는 서브시스템이 있는지 먼저 본다"는 말을 붙이면 좋다.

### 12.2 장치 이름이 부팅마다 바뀐다

`/dev/g4imu0`이 어느 물리 IMU인지는 probe 순서에 달렸다. 실무에서는 udev rule로 **안정된 심볼릭 링크**를 만든다(`/dev/sensorkit/imu-front` 같은). rule이 매칭할 수 있도록 드라이버는 sysfs에 구분 정보(DT 노드 이름, 버스 주소, 시리얼 번호)를 남긴다 — `device_create`의 `parent`를 실제 HW device로 주면 udev가 `DEVPATH`로 이를 볼 수 있다(G9).

### 12.3 데몬이 죽었다 살아나면

센서 데몬이 크래시 후 재시작하면 새로 open한다. `g4_sensorkit`처럼 open이 "지금부터"로 시작하면 크래시 동안의 데이터는 버려진다. 블랙박스 로깅처럼 잃으면 안 되는 경우는 링을 크게 잡고 `lseek(SEEK_END - N)`으로 되감거나, 데몬과 별개로 커널이 오래 들고 있는 버퍼(예: 별도 minor의 소비형 FIFO)를 둔다. **"open 시 위치를 어디에 둘지"는 제품 요구사항이다.**

### 12.4 단일 open과 systemd 재시작 레이스

`excl` 정책 장치를 쓰는 데몬을 systemd가 재시작할 때, 이전 프로세스의 fd가 아직 닫히지 않았으면(좀비 자식이 fd를 물려받았거나 `ExecStop`이 느릴 때) 새 프로세스의 open이 `-EBUSY`로 실패한다. 예제 9의 "fd 공유는 막지 못한다"와 같은 뿌리다. 데몬 쪽에서 `O_CLOEXEC`로 열고 재시도 백오프를 둔다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `read`에서 데이터 없을 때 0 반환 | `cat`이 바로 끝남, 데몬이 "EOF" 로그 후 종료 | 0은 EOF 의미 | 블로킹이면 잠들고, `O_NONBLOCK`이면 `-EAGAIN` |
| `*ppos`를 안 올림 | `cat`이 같은 내용을 무한 반복 | 다음 read가 같은 위치에서 다시 시작 | 복사한 바이트만큼 `*ppos += n` |
| `f->f_pos`를 직접 읽고 씀 | `pread()`가 이상하게 동작, 위치가 가끔 되돌아감 | VFS가 `*ppos`를 `f_pos`에 다시 써 덮음 | 항상 `*ppos`만 사용 |
| spinlock 쥔 채 `copy_to_user` | debug 커널 "sleeping function called from invalid context", 드물게 데드락 | page fault로 잠들 수 있음 | 락 안에서 커널 버퍼로 복사, 락 밖에서 유저로 |
| ISR/hrtimer와 공유하는 데이터를 mutex로 보호 | "scheduling while atomic" BUG | hardirq에서 잠들 수 없음 | spinlock + 프로세스 쪽 `spin_lock_irq`(irqsave) |
| `.owner` 누락 | 열린 상태에서 rmmod 성공 후 Oops (예제 11) | 모듈 refcount가 안 올라감 | `fops.owner = THIS_MODULE`, `cdev.owner`도 |
| `device_create` 후에 `cdev_add` | 부팅 직후 udev가 띄운 데몬의 open이 가끔 `ENXIO` | 노드가 cdev 등록보다 먼저 보임 | 상태 초기화 → `cdev_add` → `device_create` 순서 |
| `cdev_del` 직후 상태 `kfree` (핫플러그) | 장치 분리 후 열린 fd로 read 시 use-after-free | `cdev_del`은 이미 열린 file을 막지 않음 | kref로 마지막 release에서 해제, 또는 `cdev_device_add` + device release |
| major 하드코딩 (`register_chrdev_region(MKDEV(240,0), ...)`) | 다른 config/보드에서 `insmod` `-EBUSY` (예제 3) | 동적 할당이 먼저 가져감 | `alloc_chrdev_region` |
| 6.4+ 커널에 `class_create(THIS_MODULE, "x")` | 컴파일 에러 "too many arguments" | 6.4에서 시그니처 변경 | `class_create("x")` 또는 정적 const class + `class_register` |

---

## 14. 면접에서 이렇게 말한다

**Q.** What are major and minor numbers, and how should a new driver get them?

**A.** 커널 안에서 장치 번호는 `dev_t` 하나고, 상위 12비트가 major, 하위 20비트가 minor다(`MAJOR`/`MINOR`/`MKDEV` 매크로로만 다룬다). 전통적으로 major는 드라이버, minor는 그 드라이버의 인스턴스다. 새 드라이버는 `alloc_chrdev_region`으로 동적으로 받는다. 정적 major는 다른 드라이버와 충돌한다 — 랩 커널에서 "실험용" 240조차 이미 `ttyOWL`이 가져가 있어서 `-EBUSY`였다. 유저 코드는 major를 하드코딩하지 않고 노드 이름이나 sysfs로 찾는다.

> A dev_t packs a 12-bit major and a 20-bit minor; you only touch it through MAJOR, MINOR and MKDEV. Classically the major selects the driver and the minor selects an instance. A new driver should call alloc_chrdev_region and let the kernel pick a free major — static majors collide; on my test kernel even 240, the "experimental" range, was already taken by a serial driver. Userspace should find the device by name or through sysfs, never by a hardcoded major.

**꼬리 질문**: "register_chrdev는?" → minor 256개를 한 fops로 다 가져가고 노드도 안 만든다. mknod로 만든 아무 minor나 열린다.

**Q.** Walk me through a minimal character driver.

**A.** 세 단계다. 번호 받기(`alloc_chrdev_region`), cdev 등록(`cdev_init` + `cdev_add` — 이 순간부터 open 가능하니 상태 초기화는 그 전에), 노드 만들기(`class_create` + `device_create` — devtmpfs가 노드를 만들고 uevent로 udev가 권한을 다듬는다). fops에는 `.owner = THIS_MODULE`과 `read`를 넣는다. read는 `*ppos` 기준으로 남은 만큼 `copy_to_user`, 복사한 만큼 `*ppos` 증가, 끝이면 0. exit은 정확히 역순.

> Three steps: reserve a number with alloc_chrdev_region; init the cdev with my file_operations and cdev_add it — from that moment opens can arrive, so all state must be ready before; then create the node with a class and device_create, which makes devtmpfs create /dev/foo and emits a uevent so udev can apply permissions. The fops has owner = THIS_MODULE and a read that copies from the position in ppos, advances that position by what was copied, returns that count, and returns 0 at end of file. Teardown is the exact reverse order.

**Q.** Your driver exposes three identical sensors. How does read() know which one it's talking to, and where do you keep state?

**A.** 인스턴스마다 구조체를 두고 그 안에 `struct cdev`를 박아 minor 하나씩 `cdev_add` 한다. open에서 `container_of(inode->i_cdev, struct my_dev, cdev)`로 인스턴스를 찾는다 — `chrdev_open`이 `i_cdev`를 캐시해 둔다. open마다 다른 상태(읽기 통계, 소유권)는 따로 할당해 `file->private_data`에 걸고, read/write/release는 거기서 시작한다. 읽기 위치는 VFS가 `struct file`마다 따로 관리하는 `f_pos`다. 정리하면 per-device는 `container_of`, per-open은 `private_data`, 락이 필요한 건 per-device 쪽뿐.

> One struct per instance with an embedded struct cdev, one cdev_add per minor. In open I do container_of(inode->i_cdev, struct my_dev, cdev) — chrdev_open has already cached the cdev pointer in the inode. Anything per-open, like statistics or an exclusive-ownership flag, I allocate and hang on file->private_data, and every other fop starts from there. The read position itself is f_pos, which the VFS keeps per struct file. Per-device state is shared and needs locking; per-open state usually doesn't.

**Q.** What should read() return when there is less data than requested? When there is none?

**A.** 1바이트라도 있으면 있는 만큼 돌려준다 — short read는 정상이고 유저가 다시 부른다. 없으면 블로킹 fd는 wait queue에서 잠들고(`wait_event_interruptible`, 시그널이면 `-ERESTARTSYS`), `O_NONBLOCK`이면 `-EAGAIN`. 0은 "앞으로도 데이터가 없다"는 EOF일 때만. 우리 IMU 드라이버에서는 정지 상태에서 다 읽었을 때만 0을 줬다.

> If at least one byte is available, return what you have — a short read is normal and the caller loops. If nothing is available: a blocking file sleeps on a wait queue and returns -ERESTARTSYS on a signal; an O_NONBLOCK file gets -EAGAIN. Zero means end of file, so I only return it when no more data will ever come — in my IMU driver that's when sampling is stopped and the reader has caught up.

**Q.** What does `.owner = THIS_MODULE` do? What happens without it?

**A.** open 경로(`cdev_get`, `fops_get`)가 그 모듈의 참조를 잡아 두게 한다. 그래서 열린 fd가 있으면 `rmmod`가 `EWOULDBLOCK`으로 거부된다. 실제로 fd 하나에 `lsmod`가 2를 보였다(cdev와 fops 각각). 빼고 실험하면 refcount 0이라 언로드가 성공하고, 그 fd를 닫는 순간 `filp_flush`가 해제된 모듈 메모리의 fops를 읽다가 Oops가 났다. 드라이버 코드가 사라지는 문제라 드라이버가 스스로 막을 수 없고, 커널이 주인 모듈을 잡아 주는 구조다.

> It tells the open path who owns the function table, so chrdev_open takes a module reference — actually two, one via cdev_get and one via fops_get; I saw "used by 2" with one open fd — and rmmod fails with EWOULDBLOCK while the file is open. I tested removing it: the refcount stayed at zero, rmmod succeeded with the fd still open, and the very next close() oopsed in filp_flush reading f_op from freed module memory. The driver can't protect its own code from being unloaded, so the kernel pins the owner module instead.

**꼬리 질문**: "그럼 USB 분리처럼 모듈은 남고 장치만 사라지면?" → `.owner`는 소용없다. `cdev_del`은 열린 file을 막지 않으므로 kref·`cdev_device_add`로 상태 수명을 마지막 release까지 늘리고, 'removed' 플래그로 이후 read를 `-ENODEV`로 돌린다.

**Q.** Data is produced in an interrupt handler into a ring buffer and consumed by read(). How do you lock it?

**A.** ISR(우리는 hrtimer 콜백, hardirq)은 잠들 수 없으니 링은 spinlock이다. 프로세스 쪽은 `spin_lock_irq`/`irqsave`로 잡아야 같은 CPU에서 ISR이 끼어들어 자기 락을 기다리는 데드락이 없다. 그리고 `copy_to_user`는 page fault로 잠들 수 있어서 spinlock 안에서 못 부른다 — 락 안에서 커널 임시 버퍼로 복사하고 락을 푼 다음 유저로 복사한다. 잠든 리더는 ISR이 데이터를 넣고 락을 푼 뒤 `wake_up`으로 깨운다. 설정 경로(write로 start/stop)는 둘 다 프로세스 문맥이라 mutex. debug 커널(lockdep, DEBUG_ATOMIC_SLEEP)에서 돌려 경고가 없는 걸 확인했다.

> The producer runs in hardirq context, so the ring is protected by a spinlock, and the process side takes it with spin_lock_irq or irqsave so the interrupt can't deadlock against it on the same CPU. copy_to_user may fault and sleep, so it can never run under that spinlock: I copy a chunk into a kernel bounce buffer under the lock, drop it, then copy to user. The ISR updates the ring, unlocks, then calls wake_up for sleeping readers. Configuration changes from write() are process context only, so they use a mutex. I validated it on a lockdep and DEBUG_ATOMIC_SLEEP kernel with no warnings.

**Q.** How does /dev/foo get created when the driver loads?

**A.** `device_create()` → `device_add()`가 세 가지를 한다: sysfs에 장치 디렉터리와 `dev` 속성(major:minor), devtmpfs에 노드 직접 생성, 그리고 uevent(`ACTION=add`, `MAJOR`, `MINOR`, `DEVNAME`) 브로드캐스트. udev나 mdev는 uevent를 받아 권한·그룹·심볼릭 링크를 rule대로 정한다. 실습 initramfs에는 udev가 없었는데도 노드가 생겼다 — devtmpfs가 만든 것이다. 권한은 udev rule이 정석이고, class의 `.devnode` 콜백으로 드라이버가 정할 수도 있다.

> device_add does three things: creates the sysfs directory with a "dev" attribute holding major:minor, asks devtmpfs to create the node directly in the kernel, and broadcasts a uevent with ACTION, MAJOR, MINOR and DEVNAME over netlink. udev or mdev listen to that and apply ownership, mode and symlinks from rules. In my test initramfs there was no udev at all and the node still appeared — that's devtmpfs. Permissions belong in a udev rule; a class devnode callback can set them from the driver if needed.

**Q.** A user does `lseek()` on your device and gets ESPIPE. Why, and what are the options?

**A.** `.llseek`가 NULL이면 `do_dentry_open`이 `FMODE_LSEEK`를 지우고 `vfs_llseek`가 `-ESPIPE`를 준다. 스트림 장치면 그게 맞다(`nonseekable_open`/`stream_open`으로 명시). 위치가 의미 있으면 `default_llseek`나, 크기가 있으면 `fixed_size_llseek`로 범위 검사를 붙인다. seek를 시도하는 프로그램을 깨지 않으려면 `noop_llseek`. 옛 `no_llseek`는 6.18에는 없다.

> If .llseek is NULL, do_dentry_open clears FMODE_LSEEK and vfs_llseek returns -ESPIPE — correct for a pure stream, and you can make it explicit with nonseekable_open or stream_open. If position has meaning, use default_llseek, or fixed_size_llseek to bound it like I did for the sample history. noop_llseek accepts the call without moving, for tools that insist on seeking. The old no_llseek helper is gone in current kernels.

---

## 15. 직접 해보기

1. (개념) `MKDEV(510, 1)`의 값은? `stat()`으로 본 `/dev/g4imu1`의 `st_rdev`도 같은 숫자인가?
   정답/힌트: 커널 `dev_t`는 `0x1FE00001`. `st_rdev`는 `new_encode_dev()` 인코딩이라 다르다(minor 하위 8비트 | major << 8 | minor 상위 비트 << 12 → `0x1FE01`). 유저는 `major()`/`minor()`로만 푼다.
2. (코드) `g4_sensorkit`에 `.poll`을 추가해 `poll()`로 데이터 도착을 기다리게 하라. `poll_wait(f, &d->wq, pt)` 후 `*ppos < head × 32`이면 `EPOLLIN | EPOLLRDNORM`, 정지 상태로 따라잡았으면 `EPOLLHUP`.
   정답/힌트: wait queue는 이미 있다. G6에서 자세히. 처리량 측정(예제 12)을 "poll + 큰 read"로 바꿔 샘플당 CPU 비용을 비교해 보라.
3. (코드) 같은 `struct file`을 두 스레드가 동시에 read할 때 같은 샘플을 두 번 받는 것을 재현하고(§7.4), `g4_open`에 mutex를 넣어 고쳐라.
   정답/힌트: `pthread` 두 개가 같은 fd로 `read(fd, buf, 32)`를 반복하며 seq를 기록 → 중복 seq가 나오는지. 고친 뒤에는 중복이 없어야 한다.
4. (관찰) `/proc/<pid>/fdinfo/3`과 `/proc/<pid>/fd/3`으로 `exec 3< /dev/g4imu0` 상태의 셸에서 `pos:`와 flags를 확인하라. `dd bs=10 count=1 <&3` 후 `pos`가 어떻게 바뀌나?
   정답/힌트: `pos`는 `f_pos` 그대로다. 같은 fd를 공유하므로 셸과 dd가 같은 `f_pos`를 본다.
5. (코드) `cdev_add` + `device_create` 대신 `cdev_device_add` + `device_initialize`로 `g4_create`를 다시 써라(§2.4 스케치). `g4_dev`를 device의 release 콜백에서 해제하도록 바꾸고, 모듈 대신 "장치 제거" 시뮬레이션(debugfs 파일에 쓰면 `g4_destroy(i)` 호출)으로 열린 fd가 있을 때 use-after-free가 없는지 KASAN 없이도 debug 커널에서 확인해 보라.
   정답/힌트: remove 경로에서 `removed = true` + `wake_up` → read는 `-ENODEV`. 마지막 `put_device`가 release에서 `kfree`.
6. (관찰) `g4_noown`으로 예제 11을 반복하되 이번엔 close가 아니라 read에서 죽게 만들어라(셸 리다이렉션이 fd를 닫지 않게). Oops의 `pc`는 어디인가?
   정답/힌트: `rmmod` 대신 별도 C 프로그램이 `delete_module(2)`를 부르게 하거나, fd를 연 C 프로그램이 rmmod 후 read. `vfs_read`가 `file->f_op->read`를 읽는 지점에서 data abort가 예상된다(이 노트에서 실행하지 않았다).
7. (개념) `g4_sensorkit`의 `excl`을 켜도 두 프로세스가 같은 장치를 동시에 읽을 수 있는 방법 두 가지는?
   정답/힌트: `fork()` 후 자식이 fd 상속, `SCM_RIGHTS`로 fd 전달. 둘 다 같은 `struct file`이라 `.open`이 다시 불리지 않는다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| character device | 문자 장치 | 바이트 스트림으로 읽고 쓰는 장치. `ls -l`의 `c` |
| `dev_t` | 장치 번호 | 커널 안에서는 major 12비트 + minor 20비트 |
| major / minor | 주 / 부 번호 | 대체로 드라이버 / 인스턴스. 실제 매핑 단위는 (major, minor 범위) → cdev |
| `alloc_chrdev_region` | 동적 번호 예약 | 빈 major를 골라 minor 범위를 예약 |
| `register_chrdev_region` | 정적 번호 예약 | 내가 정한 major. 충돌하면 `-EBUSY` |
| `register_chrdev` | 옛 일체형 API | minor 256개 + cdev를 한 번에. 노드는 안 만든다 |
| `struct cdev` | char device 등록 레코드 | 번호 범위 → fops. `cdev_add`로 `cdev_map`에 들어간다 |
| `cdev_map` | 번호 → cdev 조회 표 | `kobj_map`. `chrdev_open`이 여기서 찾는다 |
| `cdev_device_add` | cdev + device 한 번에 등록 | cdev 수명을 `struct device`에 묶는다 |
| class | 장치 종류 묶음 | `/sys/class/<이름>`. `device_create`가 필요로 한다 |
| `device_create` | 장치 객체 생성 | sysfs + devtmpfs 노드 + uevent |
| devtmpfs | 커널이 채우는 `/dev` | 장치 등록 시 커널이 노드를 직접 만든다 |
| uevent | 커널 → 유저 장치 이벤트 | netlink로 `ACTION`, `DEVPATH`, `MAJOR`, `MINOR` 등을 보낸다 |
| udev / mdev | uevent 처리 데몬 | 권한·심볼릭 링크·모듈 로드. mdev는 BusyBox 판 |
| `file_operations` | fops | 드라이버가 채우는 함수 포인터 테이블 |
| `struct file` | 열린 파일 | open마다 하나. `f_pos`, `f_flags`, `f_op`, `private_data` |
| `private_data` | 드라이버 전용 슬롯 | `struct file`에 per-open 상태를 거는 곳 |
| `i_cdev` | inode의 cdev 캐시 | `container_of`로 per-device 구조체를 찾는 출발점 |
| `f_pos` / `ppos` | 파일 위치 | 드라이버는 `*ppos`를 읽고 쓰고, VFS가 `f_pos`에 반영 |
| short read | 부분 읽기 | 요청보다 적게 반환. 오류 아님 |
| overrun | 덮어쓰기 손실 | 링이 리더보다 빨리 돌아 오래된 데이터를 잃음 |
| `THIS_MODULE` | 현재 모듈 포인터 | `.owner`에 넣어 열린 동안 언로드를 막는다 |
| `-ENXIO` | No such device or address | 노드는 있는데 그 번호를 받는 드라이버가 없음 |
| `-ESPIPE` | Illegal seek | seek 불가 파일에 `lseek` |

---

## 17. 요약 & 체크리스트

문자 디바이스는 "장치 번호 → cdev → `file_operations`" 세 고리로 이어진다. 드라이버는 `alloc_chrdev_region`으로 번호를 받고, 장치 상태를 다 준비한 뒤 `cdev_add`로 번호와 fops를 묶고, `class` + `device_create`로 sysfs·devtmpfs 노드·uevent를 만든다(udev/mdev는 권한을 다듬는다). open은 `container_of(inode->i_cdev, ...)`로 장치 인스턴스를 찾고 per-open 상태를 `file->private_data`에 건다. 읽기 위치 `f_pos`는 open마다 따로이고, 드라이버는 `*ppos`만 다룬다. `read()`는 있는 만큼 주고(short read), 없으면 잠들거나 `-EAGAIN`, 0은 진짜 끝일 때만이다. ISR과 공유하는 버퍼는 spinlock + "락 밖에서 `copy_to_user`", 프로세스끼리의 설정은 mutex. `.owner = THIS_MODULE`이 없으면 열린 fd가 남은 채로 모듈이 사라져 close 한 번에 Oops가 난다. 그리고 `cdev_del`은 이미 열린 file을 막지 않는다 — 핫플러그 장치는 상태 수명을 따로 관리해야 한다.

- [ ] `dev_t`의 비트 배치와 `MAJOR`/`MINOR`/`MKDEV`, 그리고 유저의 `st_rdev` 인코딩이 다르다는 것을 설명할 수 있다
- [ ] `alloc_chrdev_region`과 `register_chrdev_region`, `register_chrdev`의 차이를 실제 출력(`-EBUSY`, minor 256개)으로 설명할 수 있다
- [ ] `cdev_init`/`cdev_add`/`cdev_del`의 의미와 "cdev_add 순간부터 open 가능", "cdev_del은 열린 file을 막지 않음"을 말할 수 있다
- [ ] `class_create`(6.4+ 시그니처) 또는 정적 class + `class_register`, `device_create`로 노드를 만들 수 있다
- [ ] `device_add`가 하는 세 가지(sysfs, devtmpfs, uevent)와 udev/mdev의 역할을 구분할 수 있다
- [ ] 여러 minor를 per-device 구조체 + `container_of(inode->i_cdev, ...)`로 구현할 수 있다
- [ ] per-open 상태를 `file->private_data`에 두고 `release`에서 해제할 수 있다
- [ ] `read()`의 short read · EOF · `-EAGAIN` · `-ERESTARTSYS` · `-EFAULT` 규칙대로 코드를 짤 수 있다
- [ ] `*ppos`와 `f_pos`의 관계, llseek 선택지(NULL → `-ESPIPE`, `noop_llseek`, `default_llseek`, `fixed_size_llseek`)를 설명할 수 있다
- [ ] hardirq 생산자 + read 소비자의 락 설계(spinlock_irq + 락 밖 `copy_to_user`)를 화이트보드에 쓸 수 있다
- [ ] 단일 open 정책을 `test_and_set_bit`로 구현하고 그 한계(fd 공유)를 말할 수 있다
- [ ] `.owner = THIS_MODULE`이 refcount를 어떻게 올리는지, 없으면 무슨 일이 나는지 설명할 수 있다
- [ ] 40줄 화이트보드 char driver를 보지 않고 쓸 수 있다

## 참고 자료

- docs.kernel.org — Linux Device Drivers 문서의 char device 관련 항목과 "The Linux driver implementer's API guide": [https://docs.kernel.org/driver-api/index.html](https://docs.kernel.org/driver-api/index.html)
- Documentation/admin-guide/devices.txt — 공식 major/minor 할당표: [https://docs.kernel.org/admin-guide/devices.html](https://docs.kernel.org/admin-guide/devices.html)
- Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman, "Linux Device Drivers, 3rd Edition" 3장 Char Drivers (scull) — API 일부(`class_create` 시그니처 등)는 옛 버전이니 위 표와 비교하며 읽는다: [https://lwn.net/Kernel/LDD3/](https://lwn.net/Kernel/LDD3/)
- 소스(랩 트리 `/var/lab/src/linux-6.18.55`, 또는 elixir.bootlin.com v6.18): `fs/char_dev.c`(`alloc_chrdev_region`, `cdev_add`, `cdev_del`, `chrdev_open`, `find_dynamic_major`), `include/linux/kdev_t.h`, `include/linux/fs.h`(`struct file_operations`, `register_chrdev`, `fops_get`), `fs/open.c`(`do_dentry_open`, `nonseekable_open`, `stream_open`), `fs/read_write.c`(`vfs_llseek`, `fixed_size_llseek`), `drivers/base/core.c`(`device_create`, `device_add`, `devtmpfs_create_node` 호출), `drivers/base/class.c`, `kernel/module/main.c`(`delete_module`, `try_stop_module`)
- Bootlin, "Linux kernel and driver development training" 슬라이드의 character driver 장: [https://bootlin.com/doc/training/linux-kernel/](https://bootlin.com/doc/training/linux-kernel/)
- man7.org: [read(2)](https://man7.org/linux/man-pages/man2/read.2.html), [lseek(2)](https://man7.org/linux/man-pages/man2/lseek.2.html), [mknod(2)](https://man7.org/linux/man-pages/man2/mknod.2.html), [makedev(3)](https://man7.org/linux/man-pages/man3/makedev.3.html), [netlink(7)](https://man7.org/linux/man-pages/man7/netlink.7.html)
- Michael Kerrisk, "The Linux Programming Interface" 4장(파일 I/O)·14장(파일 시스템, 장치 파일)
- 이 저장소의 선행 노트: E6(유저 메모리 복사·에러 규약), F3(wait queue), F4(락), F12(VFS 객체와 `chrdev_open`)
