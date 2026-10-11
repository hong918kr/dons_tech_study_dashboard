# G7. misc device — 가장 짧은 문자 디바이스, 언제 쓰고 언제 쓰지 않나

> **이 노트를 다 읽으면**: `struct miscdevice`의 필드 9개가 각각 무엇을 하는지 말할 수 있다 · `misc_open()`이 `file->private_data`에 miscdevice 포인터를 넣고 fops를 바꾸는 과정을 6.18 소스와 ftrace로 설명할 수 있다 · DT 노드마다 `/dev/sensorkitN`을 하나씩 만드는 platform + misc 드라이버를 짤 수 있다 · misc와 cdev 중 무엇을 쓸지 근거를 대고 고를 수 있다
> **왜 필요한가**: "간단한 char 드라이버 하나 짜 보세요" 화이트보드 문제의 가장 짧은 정답이 misc이고, 실무에서도 "설정·진단용 노드 하나"짜리 드라이버는 대부분 misc로 끝난다. 반대로 "열린 채로 unbind 하면?" 같은 꼬리 질문에서 수명 관리를 모르면 바로 막힌다
> **Don 기준 난이도**: 함수 포인터 테이블과 `container_of`식 "멤버 주소 → 부모 구조체"는 펌웨어에서 늘 했다 / 새로 배울 부분은 major 10을 여러 드라이버가 나눠 쓰는 구조, `.mode`·`.groups`·`.nodename`이 devtmpfs·sysfs로 이어지는 경로, 그리고 deregister가 열린 fd를 기다리지 않는다는 점
> **선행 노트**: E3(커널 모듈), F12(VFS — `chrdev_open`과 `misc_open`의 f_op 두 번 교체), G3(platform driver), G4(cdev) — G8(sysfs attribute), G9(udev·devtmpfs 권한)으로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

G4에서 문자 디바이스(char device) 하나를 만들려면 할 일이 많았다. major/minor 번호를 받고(`alloc_chrdev_region`), `cdev`를 초기화해서 등록하고(`cdev_init` + `cdev_add`), 장치 노드를 만들 class를 만들고(`class_register`), 그 class 아래에 device를 만들고(`device_create`), 내릴 때는 전부 역순으로 해제한다. 노드 하나 만들자고 함수 다섯 개와 에러 되감기(unwind) 라벨 서너 개가 필요하다.

**misc device**는 이 일을 커널이 미리 해 둔 것이다. 부팅 때 misc 서브시스템(`drivers/char/misc.c`)이 **major 10 전체를 cdev 하나로 등록**해 두고, `misc` class도 하나 만들어 둔다. 드라이버는 `struct miscdevice`에 이름과 fops만 채워 `misc_register()` 한 번 부르면 끝이다. minor 번호 하나 할당, `/dev/<이름>` 노드, `/sys/class/misc/<이름>` 디렉터리, `/proc/misc` 항목이 한 번에 생긴다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="40" y="14" width="360" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="220" y="37" text-anchor="middle" font-size="13">유저: open("/dev/sensorkit0")</text>
  <line x1="220" y1="50" x2="220" y2="74" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="40" y="76" width="360" height="36" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="220" y="99" text-anchor="middle" font-size="12">VFS: chrdev_open() — inode->i_rdev = 10:258</text>
  <line x1="220" y1="112" x2="220" y2="136" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="40" y="138" width="360" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="220" y="158" text-anchor="middle" font-size="12">cdev_map: major 10 → misc의 cdev 하나</text>
  <text x="220" y="177" text-anchor="middle" font-size="12">f_op = misc_fops (.open = misc_open)</text>
  <line x1="220" y1="188" x2="220" y2="212" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="40" y="214" width="360" height="66" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="220" y="234" text-anchor="middle" font-size="13" font-weight="bold">misc 계층 (drivers/char/misc.c)</text>
  <text x="220" y="253" text-anchor="middle" font-size="12">misc_open(): misc_list에서 minor 258 찾기</text>
  <text x="220" y="271" text-anchor="middle" font-size="12">private_data = miscdevice, replace_fops</text>
  <line x1="220" y1="280" x2="220" y2="304" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="40" y="306" width="360" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="220" y="326" text-anchor="middle" font-size="13">g7_sensorkit 드라이버: sk_fops</text>
  <text x="220" y="345" text-anchor="middle" font-size="12">container_of(private_data) → struct sk_dev</text>
  <path d="M400,163 L414,163 L414,331 L404,331" fill="none" stroke="currentColor" stroke-dasharray="5,4" marker-end="url(#ah1)"/>
  <rect x="430" y="130" width="236" height="66" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="548" y="150" text-anchor="middle" font-size="12">부팅 때 misc_init()이</text>
  <text x="548" y="168" text-anchor="middle" font-size="12">major 10 전체(2^20 minor)를</text>
  <text x="548" y="186" text-anchor="middle" font-size="12">cdev 하나로 미리 등록</text>
  <rect x="430" y="214" width="236" height="66" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="548" y="234" text-anchor="middle" font-size="12">드라이버는 misc_register()</text>
  <text x="548" y="252" text-anchor="middle" font-size="12">한 번 → minor · /dev 노드 ·</text>
  <text x="548" y="270" text-anchor="middle" font-size="12">sysfs · /proc/misc 한꺼번에</text>
  <text x="548" y="320" text-anchor="middle" font-size="12">점선: 일반 cdev(G4) 경로</text>
  <text x="548" y="338" text-anchor="middle" font-size="12">— misc 단계 없이 바로 드라이버로</text>
  <text x="340" y="388" text-anchor="middle" font-size="12">misc = "major 10을 공유하는 cdev + 미리 만들어 둔 class" 위에 드라이버가 얹히는 구조</text>
</svg>
```
그림 1 — misc 계층은 chrdev 계층과 드라이버 사이에 끼는 얇은 층이다. VFS는 major 10만 보고 misc의 cdev로 보내고, misc가 minor로 실제 드라이버를 찾는다.

펌웨어 비유로 말하면, 인터럽트 벡터 하나를 여러 주변장치가 공유할 때 공통 ISR이 상태 레지스터를 보고 개별 핸들러로 분기하는 것과 같다. 벡터 = major 10, 공통 ISR = `misc_open()`, 상태 레지스터 = minor, 개별 핸들러 = 각 드라이버의 fops. 분기는 **open 때 한 번만** 일어나고, 그 뒤 read/write/ioctl은 `file->f_op`가 이미 드라이버 것으로 바뀌어 있어서 misc 계층을 거치지 않는다(F12 §4).

이 노트에서 다루는 것:

| 절 | 내용 | 실습 |
|---|---|---|
| §1 | `struct miscdevice`, major 10, 정적·동적 minor | 예제 1 (소스), 예제 2 (게스트의 `/proc/misc`) |
| §2 | 화이트보드 misc 드라이버 25줄 | 예제 3 (`g7_hello`) |
| §3 | `misc_open` 해부 — private_data 전달과 `container_of` | 예제 4 (ftrace) |
| §4 | DT 인스턴스마다 misc 하나 — `g7_sensorkit` | 예제 5·6·7 |
| §5 | 등록 실패 — 이름·minor 충돌 | 예제 8 |
| §6 | 열린 채로 deregister — use-after-free와 고치는 법 | 예제 9·10 |
| §7 | misc vs cdev — 같은 기능을 cdev로, 코드 크기 비교, 결정 흐름 | 예제 11·12·13 + 차트 |

> 실행 환경: Mac의 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안에서 공용 랩의 **debug 커널**(`/var/lab/out/Image-dbg`, `uname -r` = `6.18.55-dbg`, ftrace·lockdep 포함)을 QEMU `virt`(cortex-a57, 1 CPU, 512 MB, TCG)로 부팅해서 돌렸다. 커널 소스 인용은 랩의 `/var/lab/src/linux-6.18.55`. 모듈은 `/var/lab/out/linux-build-dbg`로 빌드했고(vermagic `6.18.55-dbg SMP preempt mod_unload aarch64`) **경고 0개**, 유저 프로그램은 VM에서 `gcc -std=gnu11 -Wall -Wextra -O2 -static`으로 빌드했고 경고 0개다. 모든 출력은 게스트에서 실제로 돌린 결과이고, 타임스탬프·주소·PID는 실행마다 다르다. TCG + lockdep이라 ftrace 시간 값은 실제 보드보다 수십 배 느리다.

### 0.1 실습 준비 — 모듈 빌드, DT, 부팅 스크립트

이 노트의 모듈 다섯 개를 한 Kbuild로 빌드한다.

```make
obj-m += g7_hello.o g7_sensorkit.o g7_cdev.o g7_dup.o g7_safe.o
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/G7/mod && cd /var/tmp/G7/mod \
  && cp /Users/donh/.elinux_scratch/G7/{g7_hello.c,g7_sensorkit.c,g7_cdev.c,g7_dup.c,g7_safe.c,Kbuild} . \
  && make -j2 -C /var/lab/out/linux-build-dbg M=$PWD modules 2>&1 | grep -E "CC \[M\]  g7_[a-z]+\.o|warning|error|LD"'
```

```text
  CC [M]  g7_hello.o
  CC [M]  g7_sensorkit.o
  CC [M]  g7_cdev.o
  CC [M]  g7_dup.o
  CC [M]  g7_safe.o
  LD [M]  g7_hello.ko
  LD [M]  g7_sensorkit.ko
  LD [M]  g7_cdev.ko
  LD [M]  g7_dup.ko
  LD [M]  g7_safe.ko
```

(`make clean` 직후 빌드다. `warning`/`error` 줄이 하나도 없다.)

센서 키트의 IMU 두 개(앞·뒤)를 DT 노드 두 개로 표현한다. 랩의 `virt.dts`를 복사하고 노드만 덧붙인 파일을 include 한다.

```dts
/ {
	g7-sensorkit-front {
		compatible = "g7,sensorkit";
		label = "imu-front";
		g7,odr-hz = <200>;
	};
	g7-sensorkit-rear {
		compatible = "g7,sensorkit";
		label = "imu-rear";
		g7,odr-hz = <100>;
	};
};
```

```sh
cd /var/tmp/G7/dt && cp /var/lab/out/virt.dts . \
  && printf '/include/ "virt.dts"\n/include/ "g7_sensorkit.dtsi"\n' > g7.dts \
  && dtc -I dts -O dtb -o g7.dtb g7.dts
```

(`dtc`는 `virt.dts`의 phandle 관련 경고 몇 줄을 낸다 — 디컴파일된 원본 DT에서 오는 것이고 우리 노드와는 상관없다.)

게스트 스크립트 하나를 initramfs의 `/g7/t.sh`로 넣고 부팅하는 `boot.sh`(VM에서 실행). 랩 initramfs의 `/init`이 `LAB_CMD`를 실행한 뒤 전원을 끈다.

```sh
#!/bin/bash
# usage: [EXTRA="..."] boot.sh <guest-script.sh>  -> dbg kernel + g7.dtb, /g7/* in initramfs
set -e
G=$1; N=$(basename $G .sh); W=/var/tmp/G7/run_$N
rm -rf $W && mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
mkdir -p $W/r/g7
cp /var/tmp/G7/mod/*.ko /var/tmp/G7/bin/* $W/r/g7/
cp /Users/donh/.elinux_scratch/G7/$G $W/r/g7/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -no-reboot \
  -kernel /var/lab/out/Image-dbg -initrd $W/i.gz -dtb /var/tmp/G7/dt/g7.dtb \
  -append "console=ttyAMA0 quiet $EXTRA LAB_CMD=sh~/g7/t.sh" > $W/out.txt 2>&1 || echo "qemu rc=$?"
tr -d '\r' < $W/out.txt > /var/tmp/G7/out_$N.txt
```

`-dtb`로 직접 만든 DT를 줄 때는 랩 README대로 DT를 뽑을 때와 같은 `-m 512M -cpu cortex-a57`, CPU 1개로 부팅한다.

---

## 1. `struct miscdevice`와 major 10

### 1.1 정의 — 9개 필드

6.18의 `include/linux/miscdevice.h`에서 구조체는 이렇게 생겼다.

```c
struct miscdevice {
	int minor;
	const char *name;
	const struct file_operations *fops;
	struct list_head list;
	struct device *parent;
	struct device *this_device;
	const struct attribute_group **groups;
	const char *nodename;
	umode_t mode;
};
```

| 필드 | 누가 채우나 | 뜻 | 실무에서 |
|---|---|---|---|
| `minor` | 드라이버 | `MISC_DYNAMIC_MINOR`(255)면 커널이 고른다. 255 미만이면 그 번호를 고정 요청 | 새 드라이버는 **항상 동적** |
| `name` | 드라이버 | `/dev/<name>`, `/sys/class/misc/<name>`, `/proc/misc`의 이름 | 시스템 전체에서 유일해야 한다(§5) |
| `fops` | 드라이버 | open 이후 쓰일 `file_operations` | `.owner = THIS_MODULE` 필수 |
| `list` | misc 코어 | `misc_list`에 연결하는 고리 | 건드리지 않는다 |
| `parent` | 드라이버(선택) | sysfs에서 이 노드의 부모 device | platform device를 넣으면 sysfs 트리가 하드웨어 아래로 붙는다 |
| `this_device` | misc 코어 | `misc_register`가 만든 `struct device` | `dev_info(misc.this_device, …)`, sysfs 콜백의 `dev` |
| `groups` | 드라이버(선택) | 노드 생성과 **동시에** 만들 sysfs attribute 그룹 | 레이스 없는 sysfs 노출(G8) |
| `nodename` | 드라이버(선택) | `/dev` 아래 경로를 바꾼다(예: `"net/tun"`) | 하위 디렉터리에 노드를 둘 때 |
| `mode` | 드라이버(선택) | devtmpfs가 만들 노드의 권한 | 0이면 기본값 0600 |

함수는 두 개다: `int misc_register(struct miscdevice *)`, `void misc_deregister(struct miscdevice *)`. 둘 다 `EXPORT_SYMBOL`(GPL 전용이 아님)이다. 그리고 모듈 init/exit에서 등록·해제만 하는 드라이버를 위한 매크로가 있다.

```c
#define module_misc_device(__misc_device) \
	module_driver(__misc_device, misc_register, misc_deregister)
```

`module_driver`는 `module_platform_driver`(G3)를 만드는 바로 그 매크로다 — init에서 `misc_register(&x)`, exit에서 `misc_deregister(&x)`를 부르는 함수 두 개를 생성한다.

### 1.2 예제 1 — 6.18 소스에서 확인하기

무엇을 확인하나: major 10이 어디서 등록되는지, 동적 minor가 어느 범위에서 나오는지, `misc_open`이 어디서 `private_data`를 채우는지 줄 번호로.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 && grep -n "MISC_MAJOR" include/uapi/linux/major.h; grep -n "MISC_DYNAMIC_MINOR\s*255" include/linux/miscdevice.h; grep -nE "^(static )?(int|void) (misc_register|misc_deregister)|^static int misc_open|file->private_data = c;|replace_fops\(file|__register_chrdev\(MISC_MAJOR|MISC_DYNAMIC_MINOR \+ 1" drivers/char/misc.c'
```

```text
26:#define MISC_MAJOR		10
83:#define MISC_DYNAMIC_MINOR	255
71:		ret = ida_alloc_range(&misc_minors_ida, MISC_DYNAMIC_MINOR + 1,
118:static int misc_open(struct inode *inode, struct file *file)
158:	file->private_data = c;
161:	replace_fops(file, new_fops);
209:int misc_register(struct miscdevice *misc)
284:void misc_deregister(struct miscdevice *misc)
306:	err = __register_chrdev(MISC_MAJOR, 0, MINORMASK + 1, "misc", &misc_fops);
```

출력에서 볼 것:

- 306행: 부팅 때 `misc_init()`이 `__register_chrdev(10, 0, MINORMASK + 1, …)`로 **major 10의 minor 0 ~ 2^20−1 전체**를 cdev 하나(`misc_fops`)로 잡는다. 개별 misc 드라이버는 cdev를 만들지 않는다.
- 71행: 동적 minor는 `ida_alloc_range(…, MISC_DYNAMIC_MINOR + 1, MINORMASK, …)` — 즉 **256 이상**에서 나온다. 헤더 주석도 "< 255 고정, == 255 동적 요청 표시, > 255 동적으로 할당된 값"이라고 적고 있다. 예전 커널은 255 아래(예: 63부터 거꾸로 내려가며)에서 동적 minor를 줬다 — 정확히 몇 버전에서 바뀌었는지는 이 노트에서 확인하지 않았다. 중요한 건 "동적 minor 값에 의존하지 말라"는 것이다.
- 158·161행: `misc_open`이 `private_data`를 채우고 fops를 바꾼다(§3).

### 1.3 정적 minor 목록 — 왜 새 드라이버는 동적을 쓰나

`miscdevice.h` 앞부분에는 역사적으로 배정된 고정 minor 목록이 있다(발췌, 6.18).

```c
#define WATCHDOG_MINOR		130	/* Watchdog timer     */
#define RTC_MINOR		135
#define HWRNG_MINOR		183
#define VFIO_MINOR		196
#define TUN_MINOR		200
#define UINPUT_MINOR		223
#define FUSE_MINOR		229
#define KVM_MINOR		232
#define LOOP_CTRL_MINOR		237
#define UHID_MINOR		239
#define MISC_DYNAMIC_MINOR	255
```

고정 minor가 존재하는 이유는 두 가지다.

1. **udev 이전의 정적 `/dev`**: 옛날 배포판은 `/dev`를 `mknod`로 미리 만들어 두었다. `/dev/fuse`가 항상 10:229여야 했던 이유다.
2. **요청 시 모듈 자동 로드**: `misc_open`은 열린 minor를 못 찾고 그 minor가 255 미만이면 `request_module("char-major-10-%d")`를 부른다(소스 136~148행). 모듈은 `MODULE_ALIAS_MISCDEV(FUSE_MINOR)`로 별칭 `char-major-10-229`를 선언해 두므로, 노드가 미리 있으면 **열기만 해도 fuse 모듈이 로드**된다. 동적 minor에는 이 경로가 없다.

새 드라이버는 둘 다 필요 없다. devtmpfs가 등록 순간 노드를 만들고, 모듈 자동 로드는 장치 매칭(`MODULE_DEVICE_TABLE`, G9)으로 한다. 그래서 커널 문서와 리뷰어가 요구하는 것은 하나다: **`MISC_DYNAMIC_MINOR`를 쓰고, 유저스페이스는 이름(`/dev/<name>`)으로 찾게 하라.** 고정 번호는 LANANA(현재는 커널 `Documentation/admin-guide/devices.txt`)에 등록해야 하고, 이미 다른 드라이버가 쓰면 `-EBUSY`로 실패한다(§5에서 직접 본다).

### 1.4 예제 2 — 게스트에서 본 misc 장치들

무엇을 확인하나: 아무 모듈도 넣지 않은 게스트에서 major 10이 `misc`로 등록돼 있는지, 어떤 장치가 있고 minor가 어떤 범위인지, `.mode`와 `.nodename`의 흔적.

```sh
# g7_base.sh (게스트 안)
echo "### uname"; uname -r
echo "### proc/devices (char 10)"; grep -w misc /proc/devices
echo "### proc/misc (stock)"; cat /proc/misc
echo "### ls -l /dev major 10"; ls -l /dev /dev/net /dev/vfio | grep " 10, "
echo "### loop-control sysfs"; ls --color=never /sys/class/misc/loop-control/; cat /sys/class/misc/loop-control/uevent
echo "### tun uevent"; cat /sys/class/misc/tun/uevent
echo "### kvm"; ls /dev/kvm; dmesg | grep -i kvm | head -3
```

```text
lab initramfs: 6.18.55-dbg up 8.41s
### uname
6.18.55-dbg
### proc/devices (char 10)
 10 misc
### proc/misc (stock)
257 cpu_dma_latency
196 vfio
200 tun
237 loop-control
235 autofs
231 snapshot
183 hw_random
256 vga_arbiter
### ls -l /dev major 10
crw-r--r--    1 0        0          10, 235 Jan  1  1970 autofs
crw-------    1 0        0          10, 257 Oct 11 01:35 cpu_dma_latency
crw-------    1 0        0          10, 183 Jan  1  1970 hwrng
crw-------    1 0        0          10, 237 Jan  1  1970 loop-control
crw-------    1 0        0          10, 231 Jan  1  1970 snapshot
crw-------    1 0        0          10, 256 Jan  1  1970 vga_arbiter
crw-------    1 0        0          10, 200 Jan  1  1970 tun
crw-rw-rw-    1 0        0          10, 196 Jan  1  1970 vfio
### loop-control sysfs
dev        power      subsystem  uevent
MAJOR=10
MINOR=237
DEVNAME=loop-control
### tun uevent
MAJOR=10
MINOR=200
DEVNAME=net/tun
### kvm
ls: /dev/kvm: No such file or directory
[    5.171283] kvm [1]: HYP mode not available
[   10.476494] reboot: Power down
```

출력에서 볼 것:

- `/proc/devices`의 char major 10 이름이 `misc`다. `/proc/misc`는 "minor 이름" 두 열이다(`misc_seq_show`가 `"%3i %s\n"`로 찍는다).
- 고정 minor(183, 196, 200, 231, 235, 237)와 **동적 minor(256, 257)**가 섞여 있다. `vga_arbiter`와 `cpu_dma_latency`(PM QoS)는 동적으로 등록해서 256, 257을 받았다.
- `/proc/misc`의 `hw_random`이 `/dev/hwrng`로 나온다 — hw_random 코어가 `.nodename = "hwrng"`을 줬기 때문이다. `tun`은 `.nodename = "net/tun"`이라 `/dev/net/tun`에 있고(`uevent`의 `DEVNAME=net/tun`), `vfio`는 `.nodename = "vfio/vfio"`라 `/dev/vfio/vfio`에 있다. 위 `ls` 출력의 마지막 두 줄이 그 두 디렉터리에서 나온 것이다(`grep` 때문에 디렉터리 제목 줄이 빠졌다).
- `vfio`만 `crw-rw-rw-`다. 드라이버가 `.mode = S_IRUGO | S_IWUGO`를 줬기 때문이다. autofs는 소스에 `.mode = 0644`가 있어서 `crw-r--r--`이고, 나머지는 `.mode`가 0이라 devtmpfs 기본값 0600이다(`drivers/base/devtmpfs.c`).
- `/dev/kvm`이 없다. KVM 코드는 커널에 들어 있지만(`CONFIG_KVM=y`) TCG로 에뮬레이션되는 게스트 CPU에 EL2(하이퍼바이저 모드)가 없어서 초기화가 실패했고(`HYP mode not available`), 그래서 `misc_register(&kvm_dev)`까지 가지 않았다. **"misc 장치는 드라이버가 등록에 성공했을 때만 생긴다"**는 당연한 사실을 실제로 본 것이다. fuse는 이 커널에서 모듈(`CONFIG_FUSE_FS=m`)이라 로드 전에는 없다.

---

## 2. 화이트보드 misc 드라이버 — 25줄

### 2.1 예제 3 — `g7_hello`

무엇을 확인하나: misc 드라이버의 최소 형태. `open`도 `init`/`exit`도 직접 쓰지 않는다.

```c
// g7_hello: 화이트보드용 최소 misc 드라이버
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

static ssize_t hello_read(struct file *f, char __user *buf,
			  size_t n, loff_t *pos)
{
	static const char msg[] = "hello from misc\n";

	return simple_read_from_buffer(buf, n, pos, msg, sizeof(msg) - 1);
}

static const struct file_operations hello_fops = {
	.owner = THIS_MODULE,
	.read  = hello_read,
};

static struct miscdevice hello_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "g7_hello",
	.fops  = &hello_fops,
	.mode  = 0444,
};
module_misc_device(hello_misc);
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (g7_cmp.sh 앞부분)
insmod g7_hello.ko; ls -l --color=never /dev/g7_hello; grep g7_hello /proc/misc; cat /dev/g7_hello
echo hi > /dev/g7_hello; echo "write rc=$?"
```

```text
### hello
cr--r--r--    1 0        0          10, 258 Oct 11 01:39 /dev/g7_hello
258 g7_hello
hello from misc
sh: write error: Invalid argument
write rc=1
```

출력에서 볼 것:

- 노드가 major 10, 동적 minor 258로 생겼고 권한이 `.mode = 0444`대로 `cr--r--r--`다. udev가 없는 BusyBox initramfs인데도 노드가 있다 — devtmpfs가 커널 안에서 만든다.
- root는 0444 노드도 쓰기용으로 열 수 있다(권한 검사 우회). 그런데 fops에 `.write`가 없으므로 `vfs_write`가 `-EINVAL`을 돌려준다. "권한"과 "기능"은 별개다.

### 2.2 화이트보드에서 말로 덧붙일 것

25줄을 쓰고 나서 면접관에게 이 네 가지를 말하면 "써 본 사람"으로 보인다.

1. `simple_read_from_buffer`가 `*pos`를 갱신하고 `copy_to_user`를 해 준다. 그래서 `cat`이 두 번째 read에서 0(EOF)을 받고 끝난다.
2. `.owner = THIS_MODULE` 덕분에 열려 있는 동안 `rmmod`가 거부된다(§6에서 확인).
3. `.open`이 없어도 된다. `misc_open`이 `file->private_data = &hello_misc`를 넣어 준다(§3).
4. 실제 장치가 있다면 이 정적 `miscdevice`가 아니라 probe에서 장치 구조체 안에 `miscdevice`를 넣고 `container_of`로 꺼낸다(§4).

---

## 3. `misc_open` 해부 — private_data 전달과 `container_of`

### 3.1 소스 — 6.18 `drivers/char/misc.c`

F12에서 본 함수를 드라이버 작성자 관점으로 다시 읽는다.

```c
static int misc_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct miscdevice *c = NULL, *iter;
	int err = -ENODEV;
	const struct file_operations *new_fops = NULL;

	mutex_lock(&misc_mtx);

	list_for_each_entry(iter, &misc_list, list) {
		if (iter->minor != minor)
			continue;
		c = iter;
		new_fops = fops_get(iter->fops);
		break;
	}
	/* ... 고정 minor면 request_module() 후 한 번 더 검색 (생략) ... */
	if (!new_fops)
		goto fail;

	file->private_data = c;

	err = 0;
	replace_fops(file, new_fops);
	if (file->f_op->open)
		err = file->f_op->open(inode, file);
fail:
	mutex_unlock(&misc_mtx);
	return err;
}
```

(136~148행의 `request_module` 부분만 주석으로 줄였다.)

한 줄씩 의미:

1. `iminor(inode)` — devtmpfs 노드의 `i_rdev`에서 minor를 꺼낸다.
2. `misc_list`를 **선형 검색**한다. 장치 수십 개 수준이라 open 때 한 번이면 문제없다.
3. `fops_get(iter->fops)` — 드라이버 fops의 `.owner` 모듈 참조를 올린다(`try_module_get`). 이 참조가 "열려 있는 동안 rmmod 불가"를 만든다.
4. `file->private_data = c` — **이 장치의 `struct miscdevice` 주소**를 넣는다. 드라이버가 자기 장치 구조체를 찾는 유일한 단서다.
5. `replace_fops(file, new_fops)` — `file->f_op`를 드라이버 fops로 바꾸고, 옛 `misc_fops`의 참조는 놓는다.
6. 드라이버에 `.open`이 있으면 **`misc_mtx`를 잡은 채로** 부른다. 즉 드라이버의 `.open`은 모든 misc 장치의 open·register·deregister와 직렬화된다. 장점: open과 deregister가 경쟁하지 않는다(§6.3에서 이 성질을 이용한다). 단점: `.open`에서 오래 자면 시스템의 다른 misc open이 전부 멈춘다.

### 3.2 `container_of` — miscdevice 포인터에서 내 장치 구조체로

드라이버는 `struct miscdevice`를 **포인터가 아니라 멤버로 포함(embed)**한다.

```c
struct sk_dev {
	struct miscdevice misc;		/* 포함(embed) — 포인터가 아니다 */
	const char *label;
	u32 odr_hz;
	u32 seq;
	int id;
};
```

그러면 `private_data`(= `&sk->misc`)에서 멤버 오프셋을 빼서 `sk`를 얻을 수 있다.

```c
struct sk_dev *sk = container_of(file->private_data, struct sk_dev, misc);
```

같은 포인터가 sysfs 쪽에서도 온다. `misc_register`는 `device_create_with_groups(…, misc /* drvdata */, …)`로 miscdevice 자신을 `this_device`의 drvdata로 넣는다. 그래서 sysfs `show`/`store` 콜백에서 `dev_get_drvdata(dev)`를 하면 역시 `struct miscdevice *`가 나오고, 같은 `container_of`가 통한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="180" y="28" text-anchor="middle" font-size="13" font-weight="bold">struct sk_dev (probe에서 할당)</text>
  <rect x="60" y="40" width="240" height="150" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="62" text-anchor="middle" font-size="13">misc (struct miscdevice)</text>
  <text x="180" y="84" text-anchor="middle" font-size="12">offset 0</text>
  <text x="180" y="110" text-anchor="middle" font-size="12">minor · name · fops · list</text>
  <text x="180" y="130" text-anchor="middle" font-size="12">parent · this_device</text>
  <text x="180" y="150" text-anchor="middle" font-size="12">groups · nodename · mode</text>
  <rect x="60" y="190" width="240" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="180" y="210" text-anchor="middle" font-size="12">label</text>
  <rect x="60" y="220" width="240" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="180" y="240" text-anchor="middle" font-size="12">odr_hz</text>
  <rect x="60" y="250" width="240" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="180" y="270" text-anchor="middle" font-size="12">seq · id</text>
  <rect x="380" y="40" width="280" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="60" text-anchor="middle" font-size="12">file->private_data</text>
  <text x="520" y="79" text-anchor="middle" font-size="12">misc_open()이 넣음 (open마다)</text>
  <rect x="380" y="110" width="280" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="130" text-anchor="middle" font-size="12">dev_get_drvdata(this_device)</text>
  <text x="520" y="149" text-anchor="middle" font-size="12">misc_register가 drvdata로 넣음</text>
  <rect x="380" y="200" width="280" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="520" y="222" text-anchor="middle" font-size="12">container_of(p, struct sk_dev, misc)</text>
  <text x="520" y="243" text-anchor="middle" font-size="12">= p 주소 − offsetof(sk_dev, misc)</text>
  <line x1="380" y1="65" x2="302" y2="70" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="380" y1="135" x2="302" y2="100" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="520" y1="90" x2="520" y2="108" stroke="currentColor" stroke-dasharray="4,3"/>
  <line x1="520" y1="160" x2="520" y2="198" stroke="currentColor" stroke-dasharray="4,3" marker-end="url(#ah2)"/>
  <path d="M380,240 L340,240 L340,44 L302,44" fill="none" stroke="#3f9a6b" marker-end="url(#ah2)"/>
  <text x="340" y="306" text-anchor="middle" font-size="12">misc가 첫 멤버라 오프셋이 0이지만, 어느 위치에 두어도 container_of가 정확히 부모를 찾는다</text>
</svg>
```
그림 2 — open 경로(`private_data`)와 sysfs 경로(`dev_get_drvdata`)가 둘 다 `struct miscdevice *`를 주고, 드라이버는 `container_of` 하나로 자기 장치 구조체를 찾는다.

Don의 펌웨어 경험으로는, 리스트 노드를 구조체 안에 넣어 두고 노드 주소에서 소유 구조체를 역산하는 "intrusive list" 패턴과 같다. 전역 배열 `g_imu[2]`로 찾는 대신 포인터 산술로 찾으므로 **인스턴스 수에 제한이 없다**.

G4의 cdev 드라이버와의 차이: cdev는 `container_of(inode->i_cdev, struct my_dev, cdev)`로 inode에서 찾고 `.open`에서 `private_data`를 직접 채운다. misc는 `misc_open`이 그 일을 대신 해 주므로 `.open` 자체가 필요 없다.

### 3.3 예제 4 — ftrace로 본 `misc_open`

무엇을 확인하나: `misc_open` 안에서 실제로 무엇이 불리는지. `.open`이 없는 `g7_hello`와 `.open`이 있는 `g7_safe`(§6.3)를 비교한다. debug 커널의 function_graph 트레이서를 `misc_open`에만 건다.

```sh
# g7_ftrace.sh (게스트 안)
cd /g7
insmod g7_hello.ko; insmod g7_safe.ko
mount -t tracefs none /sys/kernel/tracing; cd /sys/kernel/tracing
echo misc_open > set_graph_function; echo 3 > max_graph_depth
echo function_graph > current_tracer
for d in g7_hello sensorkit0; do
  echo > trace; echo 1 > tracing_on; cat /dev/$d > /dev/null; echo 0 > tracing_on
  echo "### misc_open for /dev/$d"; grep -v "^#\|^ *$" trace
done
```

```text
### misc_open for /dev/g7_hello
 0)               |  misc_open() {
 0)               |    mutex_lock_nested() {
 0) ! 136.704 us  |      __mutex_lock();
 0) ! 354.128 us  |    }
 0) + 61.424 us   |    try_module_get();
 0)   2.000 us    |    module_put();
 0)               |    mutex_unlock() {
 0)   3.920 us    |      __mutex_unlock_slowpath();
 0)   8.512 us    |    }
 0) # 1163.216 us |  }
### misc_open for /dev/sensorkit0
 0)               |  misc_open() {
 0)               |    mutex_lock_nested() {
 0) + 13.712 us   |      __mutex_lock();
 0) + 24.208 us   |    }
 0)   4.000 us    |    try_module_get();
 0)   1.632 us    |    module_put();
 0) + 45.664 us   |    sk_open [g7_safe]();
 0)               |    mutex_unlock() {
 0)   3.920 us    |      __mutex_unlock_slowpath();
 0)   8.672 us    |    }
 0) ! 160.336 us  |  }
```

출력에서 볼 것:

- `try_module_get`은 `fops_get(iter->fops)`(드라이버 모듈 참조 획득), `module_put`은 `replace_fops`가 옛 `misc_fops`를 놓는 것이다.
- `g7_hello`는 `.open`이 없어서 `misc_open` 안에 드라이버 함수가 하나도 없다. `g7_safe`는 `sk_open [g7_safe]`가 **`mutex_lock`과 `mutex_unlock` 사이**에서 불린다 — §3.1의 6번 "misc_mtx를 잡은 채로"가 그대로 보인다.
- `list_for_each_entry` 검색은 인라인 코드라 트레이스에 함수로 나타나지 않는다.
- 첫 번째 `misc_open`이 1 ms 넘게 걸린 것은 TCG + lockdep 환경에서 첫 실행의 비용이다. 시간은 비교용으로만 본다.

---

## 4. DT 인스턴스마다 misc 하나 — `g7_sensorkit`

### 4.1 직관 — 장치 구조체마다 miscdevice를 하나씩

센서 키트에 IMU가 두 개 있으면 노드도 두 개(`/dev/sensorkit0`, `/dev/sensorkit1`)여야 하고, 각 노드는 자기 IMU의 상태만 봐야 한다. 정적 전역 `struct miscdevice` 하나로는 안 된다. 그래서:

1. platform driver(G3)로 DT 노드마다 probe가 한 번씩 불리게 하고,
2. probe마다 `struct sk_dev`를 할당하고 그 안의 `misc`를 채워 `misc_register`하며,
3. 이름은 `ida`로 받은 번호로 `"sensorkit%d"`를 만들고,
4. `.parent = &pdev->dev`로 sysfs 트리를 하드웨어 아래에 붙인다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="85" y="22" text-anchor="middle" font-size="13" font-weight="bold">Device Tree</text>
  <text x="250" y="22" text-anchor="middle" font-size="13" font-weight="bold">platform bus</text>
  <text x="397" y="22" text-anchor="middle" font-size="13" font-weight="bold">드라이버</text>
  <text x="570" y="22" text-anchor="middle" font-size="13" font-weight="bold">유저에게 보이는 것</text>
  <rect x="10" y="40" width="150" height="80" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="62" text-anchor="middle" font-size="12">g7-sensorkit-front</text>
  <text x="85" y="84" text-anchor="middle" font-size="12">label=imu-front</text>
  <text x="85" y="104" text-anchor="middle" font-size="12">odr-hz=200</text>
  <rect x="175" y="40" width="150" height="80" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="66" text-anchor="middle" font-size="12">platform_device</text>
  <text x="250" y="88" text-anchor="middle" font-size="12">g7-sensorkit-front</text>
  <rect x="340" y="40" width="115" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="397" y="62" text-anchor="middle" font-size="12">sk_probe()</text>
  <text x="397" y="84" text-anchor="middle" font-size="12">sk_dev #0</text>
  <text x="397" y="104" text-anchor="middle" font-size="12">ida → 0</text>
  <rect x="470" y="40" width="200" height="80" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="62" text-anchor="middle" font-size="12">/dev/sensorkit0  10:258</text>
  <text x="570" y="84" text-anchor="middle" font-size="12">/sys/class/misc/sensorkit0</text>
  <text x="570" y="104" text-anchor="middle" font-size="12">odr_hz (groups)</text>
  <rect x="10" y="190" width="150" height="80" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="212" text-anchor="middle" font-size="12">g7-sensorkit-rear</text>
  <text x="85" y="234" text-anchor="middle" font-size="12">label=imu-rear</text>
  <text x="85" y="254" text-anchor="middle" font-size="12">odr-hz=100</text>
  <rect x="175" y="190" width="150" height="80" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="216" text-anchor="middle" font-size="12">platform_device</text>
  <text x="250" y="238" text-anchor="middle" font-size="12">g7-sensorkit-rear</text>
  <rect x="340" y="190" width="115" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="397" y="212" text-anchor="middle" font-size="12">sk_probe()</text>
  <text x="397" y="234" text-anchor="middle" font-size="12">sk_dev #1</text>
  <text x="397" y="254" text-anchor="middle" font-size="12">ida → 1</text>
  <rect x="470" y="190" width="200" height="80" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="212" text-anchor="middle" font-size="12">/dev/sensorkit1  10:259</text>
  <text x="570" y="234" text-anchor="middle" font-size="12">/sys/class/misc/sensorkit1</text>
  <text x="570" y="254" text-anchor="middle" font-size="12">odr_hz (groups)</text>
  <line x1="160" y1="80" x2="173" y2="80" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="325" y1="80" x2="338" y2="80" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="455" y1="80" x2="468" y2="80" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="160" y1="230" x2="173" y2="230" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="325" y1="230" x2="338" y2="230" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="455" y1="230" x2="468" y2="230" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="250" y="150" text-anchor="middle" font-size="12">of_platform_populate</text>
  <text x="397" y="150" text-anchor="middle" font-size="12">compatible 매칭</text>
  <text x="570" y="150" text-anchor="middle" font-size="12">misc_register</text>
  <rect x="10" y="300" width="660" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="322" text-anchor="middle" font-size="12">공유: sk_fops 하나 · sk_groups 하나 · sk_ida 하나 · 드라이버 코드 하나</text>
  <text x="340" y="343" text-anchor="middle" font-size="12">인스턴스별: struct sk_dev (그 안의 miscdevice 포함) — 상태는 전부 여기에</text>
</svg>
```
그림 3 — DT 노드 두 개 → platform device 두 개 → probe 두 번 → `sk_dev` 두 개 → misc 노드 두 개. 코드와 fops 테이블은 하나, 상태는 인스턴스마다.

### 4.2 예제 5 — 드라이버 코드

무엇을 확인하나: 위 그림을 그대로 코드로. 파일이 96줄이라 두 블록으로 나눠 보인다. 앞부분 — 장치 구조체, fops, sysfs attribute:

```c
// g7_sensorkit: DT 노드 하나당 misc device 하나 (/dev/sensorkitN)
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/mod_devicetable.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/idr.h>

struct sk_dev {
	struct miscdevice misc;		/* 포함(embed) — 포인터가 아니다 */
	const char *label;
	u32 odr_hz;
	u32 seq;
	int id;
};
static DEFINE_IDA(sk_ida);

static ssize_t sk_read(struct file *f, char __user *buf, size_t n, loff_t *pos)
{
	struct sk_dev *sk = container_of(f->private_data, struct sk_dev, misc);
	char line[80];
	int len = scnprintf(line, sizeof(line), "%s label=%s odr=%u seq=%u\n",
			    sk->misc.name, sk->label, sk->odr_hz, sk->seq);
	if (*pos == 0)
		sk->seq++;
	return simple_read_from_buffer(buf, n, pos, line, len);
}
static const struct file_operations sk_fops = {
	.owner = THIS_MODULE,
	.read  = sk_read,		/* .open 없음 — misc_open이 private_data를 채운다 */
};

static ssize_t odr_hz_show(struct device *d, struct device_attribute *a, char *buf)
{
	struct sk_dev *sk = container_of(dev_get_drvdata(d), struct sk_dev, misc);
	return sysfs_emit(buf, "%u\n", sk->odr_hz);
}
static ssize_t odr_hz_store(struct device *d, struct device_attribute *a,
			    const char *buf, size_t n)
{
	struct sk_dev *sk = container_of(dev_get_drvdata(d), struct sk_dev, misc);
	return kstrtou32(buf, 0, &sk->odr_hz) ? -EINVAL : n;
}
static DEVICE_ATTR_RW(odr_hz);
static struct attribute *sk_attrs[] = { &dev_attr_odr_hz.attr, NULL };
ATTRIBUTE_GROUPS(sk);
```

뒷부분 — probe/remove와 드라이버 등록:

```c
static int sk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sk_dev *sk = devm_kzalloc(dev, sizeof(*sk), GFP_KERNEL);
	int ret;

	if (!sk)
		return -ENOMEM;
	if (device_property_read_string(dev, "label", &sk->label))
		sk->label = "none";
	if (device_property_read_u32(dev, "g7,odr-hz", &sk->odr_hz))
		sk->odr_hz = 100;
	sk->id = ida_alloc(&sk_ida, GFP_KERNEL);
	if (sk->id < 0)
		return sk->id;
	sk->misc.minor  = MISC_DYNAMIC_MINOR;
	sk->misc.name   = devm_kasprintf(dev, GFP_KERNEL, "sensorkit%d", sk->id);
	sk->misc.fops   = &sk_fops;
	sk->misc.parent = dev;
	sk->misc.mode   = 0666;
	sk->misc.groups = sk_groups;
	ret = sk->misc.name ? misc_register(&sk->misc) : -ENOMEM;
	if (ret) {
		ida_free(&sk_ida, sk->id);
		return ret;
	}
	platform_set_drvdata(pdev, sk);
	dev_info(dev, "/dev/%s minor=%d label=%s\n", sk->misc.name, sk->misc.minor, sk->label);
	return 0;
}

static void sk_remove(struct platform_device *pdev)
{
	struct sk_dev *sk = platform_get_drvdata(pdev);

	misc_deregister(&sk->misc);
	ida_free(&sk_ida, sk->id);
}

static const struct of_device_id sk_of[] = { { .compatible = "g7,sensorkit" }, { } };
MODULE_DEVICE_TABLE(of, sk_of);
static struct platform_driver sk_drv = {
	.probe = sk_probe, .remove = sk_remove,
	.driver = { .name = "g7_sensorkit", .of_match_table = sk_of },
};
module_platform_driver(sk_drv);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G7: one misc device per DT instance");
```

코드에서 볼 것:

- `sk_read`와 `odr_hz_show/store`가 **같은 `container_of`**를 쓴다. 하나는 `file->private_data`, 하나는 `dev_get_drvdata(d)`에서 출발한다(그림 2).
- `.groups`는 `misc_register` **전에** 채운다. `misc_register`가 `device_create_with_groups`에 넘기므로 attribute 파일이 device와 동시에 생기고, uevent가 나가는 시점에 이미 `odr_hz`가 있다. `misc_register` 뒤에 `device_create_file`로 붙이면 udev 규칙이 그 파일을 못 볼 수 있는 레이스가 생긴다(G8).
- 이름은 `ida_alloc`으로 받은 번호를 쓴다. probe 순서가 바뀌면 번호도 바뀔 수 있다. 번호를 하드웨어에 고정하려면 DT `aliases`(`of_alias_get_id`)를 쓰거나, 유저스페이스가 sysfs의 `device` 링크(어느 DT 노드인지)로 찾게 한다.
- `.remove`가 `void`를 돌려준다. 예전 커널은 `int`를 돌려주는 `.remove`와 `void`형 `.remove_new`가 공존했는데, 6.x 후반에 정리되어 6.18의 `include/linux/platform_device.h`에는 `void (*remove)(struct platform_device *)` 하나만 있다(정확한 전환 버전은 확인하지 않았다).
- 이 코드에는 §6에서 드러날 **수명 버그**가 하나 있다. 실무 드라이버에서도 흔한 형태라 일부러 그대로 둔다.

### 4.3 예제 6 — 두 인스턴스 실행: `/proc/misc`, `/sys/class/misc`, `ls -l /dev`

무엇을 확인하나: DT 노드 두 개 → 노드 두 개가 major 10·동적 minor로 생기는지, sysfs 트리가 platform device 아래에 붙는지, 두 노드가 각자의 상태를 보는지, sysfs attribute가 동작하는지, rmmod 하면 다 사라지는지.

```sh
# g7_main.sh (게스트 안)
cd /g7
echo "### DT nodes"; ls -d --color=never /sys/firmware/devicetree/base/g7-*
echo "### insmod"; insmod g7_sensorkit.ko; dmesg | grep g7-sensorkit
echo "### proc/misc"; cat /proc/misc | head -3
echo "### ls -l /dev/sensorkit*"; ls -l --color=never /dev/sensorkit*
echo "### sysfs class/misc/sensorkit0"; ls --color=never /sys/class/misc/sensorkit0/
cat /sys/class/misc/sensorkit0/uevent
echo "### links"; readlink /sys/class/misc/sensorkit0; readlink /sys/class/misc/sensorkit1
readlink /sys/class/misc/sensorkit0/device
echo "### read both twice"; cat /dev/sensorkit0 /dev/sensorkit1 /dev/sensorkit0 /dev/sensorkit1
echo "### sysfs attr"; cat /sys/class/misc/sensorkit1/odr_hz
echo 400 > /sys/class/misc/sensorkit1/odr_hz; cat /dev/sensorkit1
echo "### non-root"; ./g7_test /dev/sensorkit0 /dev/sensorkit1 /dev/loop-control
echo "### rmmod"; rmmod g7_sensorkit; cat /proc/misc | head -3; ls /dev/sensorkit* 2>&1
```

```text
lab initramfs: 6.18.55-dbg up 6.62s
### DT nodes
/sys/firmware/devicetree/base/g7-sensorkit-front
/sys/firmware/devicetree/base/g7-sensorkit-rear
### insmod
[    7.207357] g7_sensorkit g7-sensorkit-front: /dev/sensorkit0 minor=258 label=imu-front
[    7.212399] g7_sensorkit g7-sensorkit-rear: /dev/sensorkit1 minor=259 label=imu-rear
### proc/misc
259 sensorkit1
258 sensorkit0
257 cpu_dma_latency
### ls -l /dev/sensorkit*
crw-rw-rw-    1 0        0          10, 258 Oct 11 01:36 /dev/sensorkit0
crw-rw-rw-    1 0        0          10, 259 Oct 11 01:36 /dev/sensorkit1
### sysfs class/misc/sensorkit0
dev        device     odr_hz     power      subsystem  uevent
MAJOR=10
MINOR=258
DEVNAME=sensorkit0
DEVMODE=0666
### links
../../devices/platform/g7-sensorkit-front/misc/sensorkit0
../../devices/platform/g7-sensorkit-rear/misc/sensorkit1
../../../g7-sensorkit-front
### read both twice
sensorkit0 label=imu-front odr=200 seq=0
sensorkit1 label=imu-rear odr=100 seq=0
sensorkit0 label=imu-front odr=200 seq=1
sensorkit1 label=imu-rear odr=100 seq=1
### sysfs attr
100
sensorkit1 label=imu-rear odr=400 seq=2
### non-root
uid=0
  /dev/sensorkit0 -> sensorkit0 label=imu-front odr=200 seq=2
  /dev/sensorkit1 -> sensorkit1 label=imu-rear odr=400 seq=3
  read(/dev/loop-control): Invalid argument
uid=1000 (setuid 후)
  /dev/sensorkit0 -> sensorkit0 label=imu-front odr=200 seq=3
  /dev/sensorkit1 -> sensorkit1 label=imu-rear odr=400 seq=4
  open(/dev/loop-control): Permission denied
### rmmod
257 cpu_dma_latency
196 vfio
200 tun
ls: /dev/sensorkit*: No such file or directory
[    8.085310] reboot: Power down
```

출력에서 볼 것:

- **probe가 두 번** 불렸고(dmesg 두 줄, 앞의 `g7-sensorkit-front`는 platform device 이름 = DT 노드 이름) 동적 minor 258·259를 받았다. `/proc/misc`는 `misc_register`가 `list_add`(맨 앞에 추가)를 하기 때문에 **최근 등록 순서**로 나온다.
- `ls -l`: major 10, 권한 `crw-rw-rw-`. udev 없이 `.mode = 0666`만으로 됐다. `uevent`의 `DEVMODE=0666`은 udev·mdev에게도 같은 값을 알려 주는 줄이다.
- sysfs의 실제 위치는 `/sys/devices/platform/g7-sensorkit-front/misc/sensorkit0` — **`.parent`를 줬기 때문에 하드웨어 아래**에 붙었다. `.parent`가 없으면 `/sys/devices/virtual/misc/<name>`이 된다(§5의 dup 모듈이 그 경우다). `device` 링크를 따라가면 어느 DT 노드의 인스턴스인지 알 수 있다.
- read 결과에서 `label`·`odr`·`seq`가 노드별로 따로 간다 — `container_of`가 인스턴스마다 다른 `sk_dev`를 찾았다는 증거다.
- `odr_hz`에 400을 쓰면 `/dev/sensorkit1`에서 바로 보인다. sysfs 쪽 `container_of(dev_get_drvdata(d), …)`가 같은 구조체를 가리킨다.
- rmmod → platform driver 해제 → 두 인스턴스의 `sk_remove` → `misc_deregister` → 노드·sysfs·`/proc/misc` 항목이 모두 사라졌다.

### 4.4 예제 7 — `.mode` 권한을 uid 1000으로 확인하기

무엇을 확인하나: `.mode = 0666`인 노드는 일반 사용자도 열 수 있고, `.mode`를 안 준 노드(`/dev/loop-control`, 0600)는 못 여는지. 위 예제 6의 `### non-root` 부분이 이 프로그램의 출력이다.

```c
// g7_test: 두 인스턴스를 읽고, uid 1000으로 권한을 확인한다
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void cat_once(const char *path)
{
	char buf[128];
	int fd = open(path, O_RDONLY);
	ssize_t n;

	if (fd < 0) {
		printf("  open(%s): %s\n", path, strerror(errno));
		return;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0) {
		printf("  read(%s): %s\n", path, strerror(errno));
		close(fd);
		return;
	}
	buf[n] = '\0';
	printf("  %s -> %s", path, buf);
	close(fd);
}

int main(int argc, char **argv)
{
	printf("uid=%d\n", getuid());
	for (int i = 1; i < argc; i++)
		cat_once(argv[i]);
	if (getuid() == 0) {
		if (setgid(1000) || setuid(1000)) {
			perror("setuid");
			exit(1);
		}
		printf("uid=%d (setuid 후)\n", getuid());
		for (int i = 1; i < argc; i++)
			cat_once(argv[i]);
	}
	return 0;
}
```

출력에서 볼 것(예제 6의 `### non-root`):

- uid 0: `/dev/loop-control`은 열리지만 read는 `EINVAL`(loop-control은 ioctl 전용이라 `.read`가 없다).
- uid 1000: `sensorkit0/1`은 그대로 읽히고, `/dev/loop-control`은 **open 단계에서 `EACCES`**. 권한 검사는 VFS의 `may_open`이 inode 모드로 하므로 드라이버 코드까지 오지도 않는다.

### 4.5 `.mode` vs udev 규칙 — 무엇을 어디서 정하나

| 방법 | 어디서 정하나 | 장점 | 단점 |
|---|---|---|---|
| `miscdevice.mode` | 드라이버 코드 | udev 없는 initramfs·BusyBox에서도 동작. 노드가 생기는 순간 권한이 맞다 | 정책이 커널에 박힌다. 그룹(`plugdev` 등)은 못 정한다(소유자는 항상 root:root) |
| udev 규칙 (`MODE=`, `GROUP=`) | `/etc/udev/rules.d/` | 배포·제품마다 정책을 바꿀 수 있다. 그룹·심볼릭 링크 이름까지 | udev(또는 mdev) 데몬이 있어야 한다. uevent 처리 전 잠깐 동안은 커널 기본 권한 |
| `nodename` | 드라이버 코드 | `/dev/<디렉터리>/<이름>` 구조 | 이름 정책도 커널에 박힌다 |

원칙: **보안에 민감한 노드는 0600으로 두고 권한은 udev 규칙으로 연다**. `.mode = 0666`은 "누가 열어도 안전한" 노드에만 준다(`/dev/vfio/vfio`처럼 열기만으로는 아무것도 못 하는 컨테이너 노드, 또는 개발용 보드). 센서 키트 제품이라면 `.mode`는 비워 두고 `SUBSYSTEM=="misc", KERNEL=="sensorkit*", GROUP="sensors", MODE="0660"` 같은 규칙을 rootfs에 넣는 쪽이 정석이다(G9).

---

## 5. 등록 실패 — 이름 충돌, minor 충돌, 잘못된 minor

### 5.1 예제 8 — `g7_dup`로 일부러 실패시키기

무엇을 확인하나: `misc_register`가 실패하는 세 경로와 반환값. 모듈 파라미터로 이름과 minor를 바꿔 가며 등록한다.

```c
// g7_dup: misc_register 실패 경우 3가지를 일부러 만든다
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

static char *name = "g7_dup";
static int minor = MISC_DYNAMIC_MINOR;
module_param(name, charp, 0444);
module_param(minor, int, 0444);

static const struct file_operations dup_fops = { .owner = THIS_MODULE };
static struct miscdevice dup_misc = { .fops = &dup_fops };

static int __init dup_init(void)
{
	int ret;

	dup_misc.name = name;
	dup_misc.minor = minor;
	ret = misc_register(&dup_misc);
	pr_info("g7_dup: misc_register(name=%s, minor=%d) = %d, minor now %d\n",
		name, minor, ret, dup_misc.minor);
	return ret;
}
static void __exit dup_exit(void)
{
	misc_deregister(&dup_misc);
}
module_init(dup_init);
module_exit(dup_exit);
MODULE_LICENSE("GPL");
```

```sh
# g7_dup.sh (게스트 안) — g7_sensorkit을 먼저 넣어 sensorkit0을 만들어 둔다
cd /g7
insmod g7_sensorkit.ko
echo "### 1) name collision: name=sensorkit0"; insmod g7_dup.ko name=sensorkit0; echo "insmod rc=$?"
dmesg | sed -n '/g7_dup\|duplicate/,$p' | grep -v "^\[.*\]  *\(lr\|sp\|x[0-9]\)" | head -40
echo "### 2) fixed minor collision: minor=237 (loop-control)"; insmod g7_dup.ko name=g7_dup minor=237; echo "insmod rc=$?"
echo "### 3) invalid fixed minor: minor=300"; insmod g7_dup.ko name=g7_dup minor=300; echo "insmod rc=$?"
echo "### 4) free fixed minor: minor=130 (WATCHDOG_MINOR)"; insmod g7_dup.ko name=g7_dup minor=130; echo "insmod rc=$?"
ls -l --color=never /dev/g7_dup; rmmod g7_dup
dmesg | grep "g7_dup:\|Invalid fixed"
```

```text
lab initramfs: 6.18.55-dbg up 9.15s
### 1) name collision: name=sensorkit0
insmod: can't insert 'g7_dup.ko': File exists
insmod rc=1
[   10.011329] sysfs: cannot create duplicate filename '/class/misc/sensorkit0'
[   10.011812] CPU: 0 UID: 0 PID: 67 Comm: insmod Tainted: G           O        6.18.55-dbg #1 PREEMPT 
[   10.011938] Tainted: [O]=OOT_MODULE
[   10.011958] Hardware name: linux,dummy-virt (DT)
[   10.012096] Call trace:
[   10.012247]  show_stack+0x30/0x90 (C)
[   10.012701]  dump_stack_lvl+0x70/0x98
[   10.012722]  dump_stack+0x18/0x24
[   10.012738]  sysfs_warn_dup+0x6c/0x90
[   10.012764]  sysfs_do_create_link_sd+0xf8/0x108
[   10.012799]  sysfs_create_link+0x28/0x50
[   10.012818]  device_add+0x2d8/0x8c8
[   10.012844]  device_create_groups_vargs+0xec/0x150
[   10.012859]  device_create_with_groups+0x5c/0x90
[   10.012874]  misc_register+0x170/0x1f8
[   10.012898]  dup_init+0x38/0xfc8 [g7_dup]
[   10.013177]  do_one_initcall+0x84/0x380
[   10.013193]  do_init_module+0x5c/0x268
[   10.013219]  load_module+0x1a30/0x2080
[   10.013232]  __do_sys_init_module+0x1a8/0x268
[   10.013246]  __arm64_sys_init_module+0x20/0x40
[   10.013260]  invoke_syscall.constprop.0+0x48/0xf0
[   10.013274]  el0_svc_common.constprop.0+0x40/0xf0
[   10.013288]  do_el0_svc+0x24/0x40
[   10.013302]  el0_svc+0x50/0x2f8
[   10.013330]  el0t_64_sync_handler+0xa0/0xe8
[   10.013345]  el0t_64_sync+0x198/0x1a0
[   10.013983] g7_dup: misc_register(name=sensorkit0, minor=255) = -17, minor now 255
### 2) fixed minor collision: minor=237 (loop-control)
insmod: can't insert 'g7_dup.ko': Device or resource busy
insmod rc=1
### 3) invalid fixed minor: minor=300
[   10.428060] Invalid fixed minor 300 for miscdevice 'g7_dup'
insmod: can't insert 'g7_dup.ko': invalid parameter
insmod rc=1
### 4) free fixed minor: minor=130 (WATCHDOG_MINOR)
insmod rc=0
crw-------    1 0        0          10, 130 Oct 11 01:36 /dev/g7_dup
[   10.013983] g7_dup: misc_register(name=sensorkit0, minor=255) = -17, minor now 255
[   10.342274] g7_dup: misc_register(name=g7_dup, minor=237) = -16, minor now 237
[   10.428060] Invalid fixed minor 300 for miscdevice 'g7_dup'
[   10.428175] g7_dup: misc_register(name=g7_dup, minor=300) = -22, minor now 300
[   10.527627] g7_dup: misc_register(name=g7_dup, minor=130) = 0, minor now 130
[   10.761202] reboot: Power down
```

(1번의 레지스터 덤프 줄은 `grep -v`로 걸렀다.)

출력에서 볼 것:

| 경우 | 반환값 | 어디서 막혔나 | 뒷정리 |
|---|---|---|---|
| 이름 충돌 (`sensorkit0`) | `-EEXIST`(-17) | `device_add` → class 디렉터리에 심볼릭 링크 `/sys/class/misc/sensorkit0` 생성 실패 + 경고 스택 | 이미 받은 동적 minor를 반납하고 `minor`를 255로 되돌림 ("minor now 255") |
| 고정 minor 충돌 (237) | `-EBUSY`(-16) | `misc_list` 검색에서 같은 minor 발견 | 경고 없이 조용히 실패 |
| 잘못된 고정 minor (300) | `-EINVAL`(-22) | 함수 첫 줄 검사 (`minor > MISC_DYNAMIC_MINOR`) | `Invalid fixed minor` 한 줄 |
| 빈 고정 minor (130) | 0 | — | 이 게스트에 watchdog 장치가 없어서 130이 비어 있었다 |

- **이름 충돌이 sysfs에서 잡힌다**는 점이 중요하다. `misc_register`는 이름 중복을 스스로 검사하지 않는다. 실제 device 디렉터리는 `.parent`가 다르면(`/sys/devices/platform/…/misc/sensorkit0` vs `/sys/devices/virtual/misc/sensorkit0`) 겹치지 않지만, class 디렉터리 `/sys/class/misc/`의 링크 이름이 겹쳐서 실패했다. devtmpfs 노드 `/dev/sensorkit0`도 같은 이름이니 어차피 공존할 수 없다.
- 실무 교훈: 인스턴스 이름은 **드라이버 안에서 유일하게**(ida), **시스템 안에서도 유일하게**(드라이버 고유 접두어) 만든다. "imu0" 같은 흔한 이름은 다른 드라이버와 부딪힐 수 있다.
- 4번처럼 "빈 고정 minor"는 등록이 된다. 하지만 나중에 watchdog 드라이버가 로드되면 그쪽이 `-EBUSY`로 실패한다(watchdog 코어가 실제로 "a legacy watchdog module is probably present"라는 메시지를 갖고 있다 — `drivers/watchdog/watchdog_dev.c`). 남의 고정 minor를 빌려 쓰지 말아야 하는 이유다.

---

## 6. 열린 채로 deregister — 노드는 사라지지만 fd는 살아 있다

### 6.1 직관

`misc_deregister()`가 하는 일은 `misc_list`에서 빼고, device(노드·sysfs)를 지우고, minor를 반납하는 것뿐이다. **이미 열린 `struct file`은 건드리지 않는다.** 그 file의 `f_op`는 open 때 드라이버 fops로 바뀌었고, `private_data`는 장치 구조체를 가리키고 있다. 그러니 deregister 뒤에도 그 fd로 `read()`를 하면 드라이버의 `sk_read`가 그대로 불린다. 그 순간 `sk_dev`가 이미 해제됐다면 use-after-free다.

모듈 언로드는 이 문제가 없다. `fops_get`이 모듈 참조를 잡고 있어서 열린 동안 `rmmod`가 거부된다. 문제는 **모듈은 그대로인데 장치만 사라지는 경우**다 — sysfs `unbind`, DT overlay 제거, USB·PCIe 핫플러그, deferred probe 재시도. platform driver + `devm_kzalloc` 조합이 정확히 이 경우에 걸린다. devres는 `remove()`가 끝나면 바로 메모리를 푼다.

### 6.2 예제 9 — 직접 일으켜 보기 (slab poison으로 증거 남기기)

무엇을 확인하나: (1) 열린 동안 rmmod가 거부되는지, (2) 열린 채로 unbind 하면 노드는 사라지지만 fd로 read가 드라이버까지 가는지, (3) 그때 해제된 메모리를 읽는지. 커널 인자 `slab_debug=FZP`로 해제된 slab 객체를 `0x6b`(POISON_FREE)로 채워, 해제된 포인터를 쓰면 티가 나게 한다.

```c
// g7_hold: 장치를 연 채로 드라이버를 unbind 하고, 그 fd로 다시 읽는다
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FREED "dmesg | grep -q 'g7_safe: free' && echo freed=yes || echo freed=no"

static void rd(int fd, const char *tag)
{
	char buf[128];
	ssize_t n = pread(fd, buf, sizeof(buf) - 1, 0);

	if (n < 0) {
		printf("[%s] read: %m\n", tag);
		return;
	}
	buf[n] = '\0';
	printf("[%s] read %zd bytes: %s", tag, n, buf);
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s /dev/node platform-dev-name\n", argv[0]);
		exit(1);
	}
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("open");
		exit(1);
	}
	rd(fd, "before");
	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		 "echo %s > /sys/bus/platform/drivers/g7_sensorkit/unbind", argv[2]);
	printf("$ %s\n", cmd);
	if (system(cmd) != 0)
		printf("unbind failed\n");
	printf("node still exists? %s\n", access(argv[1], F_OK) ? "no" : "yes");
	rd(fd, "after");
	if (system(FREED) < 0)
		perror("system");
	close(fd);
	printf("closed\n");
	if (system(FREED) < 0)
		perror("system");
	return 0;
}
```

```sh
# g7_uaf.sh (게스트 안), boot.sh를 EXTRA="slab_debug=FZP"로 실행
cd /g7
insmod ${MOD:-g7_sensorkit}.ko
echo "### rmmod while open"
( exec 3</dev/sensorkit0; rmmod ${MOD:-g7_sensorkit}; grep "^${MOD:-g7_sensorkit} " /proc/modules )
echo "### unbind while open"
./g7_hold /dev/sensorkit1 g7-sensorkit-rear
echo "### after"; cat /proc/misc | head -2; dmesg | grep -v "^\[.*\] *x[0-9]" | sed -n "/g7_hold\|Unable to handle/,\$p" | head -40
```

```text
lab initramfs: 6.18.55-dbg up 7.46s
### rmmod while open
rmmod: remove 'g7_sensorkit': Resource temporarily unavailable
g7_sensorkit 12288 1 - Live 0xffffaa18cf360000 (O)
### unbind while open
[before] read 40 bytes: sensorkit1 label=imu-rear odr=100 seq=0
$ echo g7-sensorkit-rear > /sys/bus/platform/drivers/g7_sensorkit/unbind
node still exists? no
[    8.274087] Unable to handle kernel paging request at virtual address 006b6b6b6b6b6b6b
[    8.274171] Mem abort info:
[    8.274191]   ESR = 0x0000000096000004
[    8.274224]   EC = 0x25: DABT (current EL), IL = 32 bits
...
[    8.274401] [006b6b6b6b6b6b6b] address between user and kernel address ranges
[    8.274763] Internal error: Oops: 0000000096000004 [#1]  SMP
[    8.276893] Modules linked in: g7_sensorkit(O)
[    8.277845] CPU: 0 UID: 0 PID: 69 Comm: g7_hold Tainted: G           O        6.18.55-dbg #1 PREEMPT 
...
[    8.278501] pc : string+0x50/0x140
[    8.279003] lr : vsnprintf+0x1f8/0x638
...
[    8.280666] x2 : 6b6b6b6b6b6b6b6b x1 : ffff80008083bcf8 x0 : ffff80008083bca8
[    8.280919] Call trace:
[    8.281122]  string+0x50/0x140 (P)
[    8.281325]  vsnprintf+0x1f8/0x638
[    8.281395]  scnprintf+0x70/0xc0
[    8.281462]  sk_read+0x70/0xd8 [g7_sensorkit]
[    8.281777]  vfs_read+0xcc/0x358
[    8.281860]  __arm64_sys_pread64+0xb0/0xf0
[    8.281932]  invoke_syscall.constprop.0+0x48/0xf0
[    8.282031]  el0_svc_common.constprop.0+0x40/0xf0
[    8.282105]  do_el0_svc+0x24/0x40
[    8.282162]  el0_svc+0x50/0x2f8
[    8.282223]  el0t_64_sync_handler+0xa0/0xe8
[    8.282303]  el0t_64_sync+0x198/0x1a0
[    8.282516] Code: 91000400 eb0600bf 540005a0 aa0503e4 (38646845) 
[    8.282769] ---[ end trace 0000000000000000 ]---
Segmentation fault
```

(Oops 블록의 Data abort 세부·나머지 레지스터 줄은 `...`로 생략했다. 그 뒤 `### after` 부분은 같은 Oops를 dmesg에서 다시 찍은 것이라 뺐다. `g7_hold`는 Oops로 죽어서 `freed=` 줄까지 가지 못했다.)

출력에서 볼 것:

- **rmmod while open**: BusyBox `rmmod`가 `Resource temporarily unavailable`(EAGAIN — 비차단 delete_module이 참조가 남아 있어 거부)로 실패했고, `/proc/modules`의 참조 카운트가 1이다. fd 3이 잡은 `fops_get` 참조다.
- **unbind while open**: unbind 직후 `/dev/sensorkit1`은 사라졌다("node still exists? no"). 그런데 같은 fd로 `pread`를 하자 `vfs_read` → **`sk_read [g7_sensorkit]`**까지 갔다 — `file->f_op`가 여전히 드라이버 fops라는 증거다.
- `sk_read`가 `scnprintf("%s", sk->misc.name)`을 하는데, `sk`와 `name`은 devres가 이미 해제했다. 해제된 `sk` 안의 `name` 포인터 자리가 poison 값 `0x6b6b6b6b6b6b6b6b`로 채워져 있었고(`x2`), `string()`이 그 주소를 읽다가 data abort가 났다. 펌웨어로 치면 **해제된 DMA 디스크립터의 주소 필드를 따라간 버스 에러**다.
- `slab_debug` poison이 없었다면 해제된 메모리에 옛 값이 그대로 남아 있어 **조용히 이상한 값을 읽거나, 다른 객체가 그 메모리를 재사용한 뒤 그걸 망가뜨렸을 것**이다. 그래서 개발 중에는 `slab_debug`나 KASAN을 켠 커널로 unbind/rebind 테스트를 한다. (fault 주소의 맨 위 바이트가 `00`으로 찍힌 것은 arm64 fault 보고 방식 때문으로 보이며, 이 노트에서는 더 확인하지 않았다.)

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="60" y="76" text-anchor="middle" font-size="13" font-weight="bold">devm 버전</text>
  <text x="60" y="94" text-anchor="middle" font-size="12">(예제 9)</text>
  <text x="60" y="176" text-anchor="middle" font-size="13" font-weight="bold">kref 버전</text>
  <text x="60" y="194" text-anchor="middle" font-size="12">(예제 10)</text>
  <rect x="110" y="50" width="140" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="180" y="76" text-anchor="middle" font-size="12">open</text>
  <text x="180" y="94" text-anchor="middle" font-size="12">private_data=misc</text>
  <rect x="258" y="50" width="165" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="76" text-anchor="middle" font-size="12">unbind → deregister</text>
  <text x="340" y="94" text-anchor="middle" font-size="12">devres가 sk 해제</text>
  <rect x="431" y="50" width="125" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="493" y="76" text-anchor="middle" font-size="12">read → 해제된 sk</text>
  <text x="493" y="94" text-anchor="middle" font-size="12">Oops (0x6b…)</text>
  <rect x="564" y="50" width="110" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="619" y="76" text-anchor="middle" font-size="12">close</text>
  <text x="619" y="94" text-anchor="middle" font-size="12">(도달 못 함)</text>
  <rect x="110" y="150" width="140" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="180" y="176" text-anchor="middle" font-size="12">open</text>
  <text x="180" y="194" text-anchor="middle" font-size="12">kref_get (ref=2)</text>
  <rect x="258" y="150" width="165" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="176" text-anchor="middle" font-size="12">unbind: deregister</text>
  <text x="340" y="194" text-anchor="middle" font-size="12">gone=true, ref=1</text>
  <rect x="431" y="150" width="125" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="493" y="176" text-anchor="middle" font-size="12">read</text>
  <text x="493" y="194" text-anchor="middle" font-size="12">→ -ENODEV</text>
  <rect x="564" y="150" width="110" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="619" y="176" text-anchor="middle" font-size="12">close: ref=0</text>
  <text x="619" y="194" text-anchor="middle" font-size="12">여기서 kfree</text>
  <line x1="110" y1="240" x2="668" y2="240" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="640" y="232" text-anchor="end" font-size="12">시간</text>
  <text x="340" y="276" text-anchor="middle" font-size="12">misc_deregister는 열린 fd를 기다리지 않는다 — 노드만 사라지고 file->f_op는 그대로 남는다</text>
</svg>
```
그림 4 — 같은 사건 순서(open → unbind → read → close)에서 장치 구조체의 수명을 누가 쥐고 있느냐에 따라 결과가 갈린다.

### 6.3 예제 10 — 고치는 법: kref + gone 플래그

무엇을 확인하나: 장치 구조체를 devres가 아니라 **참조 카운트**로 관리하면, unbind 뒤의 read는 깔끔한 `-ENODEV`가 되고 메모리는 마지막 close에서 풀리는지.

바뀌는 부분만 보인다(`g7_safe.c` 전체는 `g7_sensorkit.c`에 아래 변경을 한 145줄. 플랫폼 드라이버 이름은 같은 `g7_sensorkit`).

```c
struct sk_dev {				/* 필드 추가 */
	struct miscdevice misc;
	const char *label;
	u32 odr_hz, seq;
	int id;
	struct kref ref;
	struct mutex lock;
	bool gone;
};
static void sk_free(struct kref *ref)
{
	struct sk_dev *sk = container_of(ref, struct sk_dev, ref);

	pr_info("g7_safe: free %s\n", sk->misc.name);
	kfree(sk->misc.name);
	kfree(sk);
}
static int sk_open(struct inode *inode, struct file *f)
{
	struct sk_dev *sk = container_of(f->private_data, struct sk_dev, misc);

	kref_get(&sk->ref);	/* misc_open이 misc_mtx를 잡은 채 부른다 → deregister와 경쟁 없음 */
	f->private_data = sk;
	return 0;
}
static int sk_release(struct inode *inode, struct file *f)
{
	struct sk_dev *sk = f->private_data;

	kref_put(&sk->ref, sk_free);
	return 0;
}
/* sk_read: mutex_lock(&sk->lock); if (sk->gone) → mutex_unlock 후 -ENODEV; 나머지는 같음 */
/* sk_probe: devm_kzalloc → kzalloc, devm_kasprintf → kasprintf, kref_init, mutex_init */
static void sk_remove(struct platform_device *pdev)
{
	struct sk_dev *sk = platform_get_drvdata(pdev);

	misc_deregister(&sk->misc);	/* 이후 새 open 불가 */
	mutex_lock(&sk->lock);
	sk->gone = true;		/* 이미 열린 fd의 read는 -ENODEV */
	mutex_unlock(&sk->lock);
	ida_free(&sk_ida, sk->id);
	kref_put(&sk->ref, sk_free);	/* 마지막 close가 실제 free */
}
```

(구조체 정의는 지면상 `u32 odr_hz, seq;`로 줄여 적었다. 실제 파일은 예제 5와 같은 필드에 `ref`·`lock`·`gone` 세 줄을 더한 것이다.)

```sh
# g7_safe.sh = g7_uaf.sh에서 모듈 이름만 g7_safe로 바꾼 것, EXTRA="slab_debug=FZP"
```

```text
lab initramfs: 6.18.55-dbg up 15.63s
### rmmod while open
rmmod: remove 'g7_safe': Resource temporarily unavailable
g7_safe 12288 1 - Live 0xffffaa18c8304000 (O)
### unbind while open
[before] read 40 bytes: sensorkit1 label=imu-rear odr=100 seq=0
$ echo g7-sensorkit-rear > /sys/bus/platform/drivers/g7_sensorkit/unbind
node still exists? no
[after] read: No such device
freed=no
closed
freed=yes
### after
258 sensorkit0
257 cpu_dma_latency
[   17.177056] reboot: Power down
```

출력에서 볼 것:

- unbind 뒤 read가 Oops 대신 **`ENODEV`**("No such device")를 돌려준다. 유저 프로그램은 "장치가 사라졌다"를 정상 에러로 처리할 수 있다.
- unbind 직후에는 `freed=no`, **close 뒤에야 `freed=yes`** — 마지막 참조(열린 fd)가 사라질 때 `sk_free`가 불렸다.
- `sk_open`의 `kref_get`이 안전한 이유는 §3.1의 6번이다. `misc_open`이 `misc_mtx`를 잡은 채 `.open`을 부르고, `misc_deregister`도 같은 뮤텍스를 잡는다. 그래서 "list에서 찾았는데 그 사이 deregister + free" 같은 틈이 없다. cdev(G4)에는 이런 공짜 직렬화가 없어서 `cdev_device_add`/`cdev_device_del`과 `.release` 쪽 참조 관리가 더 까다롭다.
- 실무에서는 이 패턴을 더 일반화한 것이 있다: 서브시스템 코어가 "unregister 후 fops가 -ENODEV를 내게" 해 주는 경우(input, IIO, gpio cdev 등). 서브시스템을 쓰면 이 문제를 대신 풀어 준다 — §7의 "먼저 서브시스템을 찾아라"의 또 다른 이유다.

---

## 7. misc vs cdev — 같은 기능을 cdev로 짜 보면

### 7.1 예제 11 — `g7_cdev`: 기능은 같고 뼈대만 다르다

무엇을 확인하나: `g7_sensorkit`과 똑같은 기능(DT 인스턴스마다 노드, `odr_hz` sysfs attribute, 0666)을 `alloc_chrdev_region` + `cdev` + 전용 class로 만들면 무엇이 더 필요한지. fops·sysfs 콜백은 거의 같아서 **새로 필요한 부분만** 보인다(전체 147줄은 빌드·실행했다).

```c
static dev_t sk_base;
static char *sk_devnode(const struct device *d, umode_t *mode)
{
	if (mode)
		*mode = 0666;		/* misc의 .mode = 0666에 해당 */
	return NULL;
}
static const struct class sk_class = { .name = "g7sk", .devnode = sk_devnode };

static int sk_open(struct inode *inode, struct file *f)
{
	f->private_data = container_of(inode->i_cdev, struct sk_dev, cdev);
	return 0;
}
/* --- probe 안에서 misc_register 한 줄 대신 --- */
	cdev_init(&sk->cdev, &sk_fops);
	sk->cdev.owner = THIS_MODULE;
	ret = cdev_add(&sk->cdev, MKDEV(MAJOR(sk_base), sk->id), 1);
	if (ret)
		goto err_ida;
	sk->cls_dev = device_create_with_groups(&sk_class, dev, sk->cdev.dev, sk,
						sk_groups, "g7sk%d", sk->id);
	if (IS_ERR(sk->cls_dev)) {
		ret = PTR_ERR(sk->cls_dev);
		goto err_cdev;
	}
/* --- remove: misc_deregister 한 줄 대신 --- */
	device_destroy(&sk_class, sk->cdev.dev);
	cdev_del(&sk->cdev);
/* --- module_platform_driver 한 줄 대신 --- */
static int __init sk_init(void)
{
	int ret = alloc_chrdev_region(&sk_base, 0, SK_MAX, "g7sk");

	if (ret)
		return ret;
	ret = class_register(&sk_class);
	if (ret)
		goto err_region;
	ret = platform_driver_register(&sk_drv);
	if (ret)
		goto err_class;
	return 0;
err_class:
	class_unregister(&sk_class);
err_region:
	unregister_chrdev_region(sk_base, SK_MAX);
	return ret;
}
static void __exit sk_exit(void)
{
	platform_driver_unregister(&sk_drv);
	class_unregister(&sk_class);
	unregister_chrdev_region(sk_base, SK_MAX);
}
```

```sh
# g7_cmp.sh 뒷부분 (게스트 안)
insmod g7_cdev.ko; dmesg | grep "g7_cdev g7-"
grep g7sk /proc/devices; ls -l --color=never /dev/g7sk*; ls --color=never /sys/class/g7sk/; ls --color=never /sys/class/g7sk/g7sk0/
cat /dev/g7sk0 /dev/g7sk1; echo 50 > /sys/class/g7sk/g7sk0/odr_hz; ./g7_test /dev/g7sk0
```

```text
### cdev version
[   15.106304] g7_cdev g7-sensorkit-front: /dev/g7sk0 dev=510:0 label=imu-front
[   15.116171] g7_cdev g7-sensorkit-rear: /dev/g7sk1 dev=510:1 label=imu-rear
510 g7sk
crw-rw-rw-    1 0        0         510,   0 Oct 11 01:39 /dev/g7sk0
crw-rw-rw-    1 0        0         510,   1 Oct 11 01:39 /dev/g7sk1
g7sk0  g7sk1
dev        device     odr_hz     power      subsystem  uevent
g7sk0 label=imu-front odr=200 seq=0
g7sk1 label=imu-rear odr=100 seq=0
uid=0
  /dev/g7sk0 -> g7sk0 label=imu-front odr=50 seq=1
uid=1000 (setuid 후)
  /dev/g7sk0 -> g7sk0 label=imu-front odr=50 seq=2
```

출력에서 볼 것:

- 동작은 misc 버전과 같다. 다른 점은 **자기 major(510, 동적으로 받은 값)**, 자기 class(`/sys/class/g7sk/`), 그리고 minor가 0, 1로 깔끔하다는 것. `/proc/devices`에 `510 g7sk` 줄이 따로 생긴다.
- 0666 권한을 위해 class의 `.devnode` 콜백을 직접 써야 했다. misc는 `misc_devnode()`가 `miscdevice.mode`를 읽어 같은 일을 해 준다(6.18 `misc.c` 169~178행).
- `.open`이 필요해졌다. `private_data`를 채워 줄 사람이 없기 때문이다.

### 7.2 예제 12 — 줄 수와 코드 크기 비교

무엇을 확인하나: 같은 기능의 misc 버전과 cdev 버전이 실제로 얼마나 차이 나는지.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/G7/mod && for f in g7_hello.c g7_sensorkit.c g7_cdev.c g7_safe.c; do printf "%-16s total=%3d  code=%3d\n" $f $(wc -l < $f) $(grep -cvE "^\s*$|^\s*//|^\s*/\*.*\*/\s*$" $f); done; size g7_hello.o g7_sensorkit.o g7_cdev.o g7_safe.o'
```

```text
g7_hello.c       total= 26  code= 21
g7_sensorkit.c   total= 96  code= 87
g7_cdev.c        total=147  code=136
g7_safe.c        total=145  code=132
   text	   data	    bss	    dec	    hex	filename
    505	    112	      0	    617	    269	g7_hello.o
   1875	    440	      0	   2315	    90b	g7_sensorkit.o
   2251	    456	      4	   2711	    a97	g7_cdev.o
   2814	    464	     16	   3294	    cde	g7_safe.o
```

(`code`는 빈 줄과 주석만 있는 줄을 뺀 수다.)

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">코드 줄 수 (빈 줄·주석 줄 제외) — 예제 12 측정값</text>
  <line x1="205" y1="36" x2="205" y2="222" stroke="currentColor"/>
  <line x1="205" y1="222" x2="565" y2="222" stroke="currentColor"/>
  <line x1="325" y1="218" x2="325" y2="226" stroke="currentColor"/>
  <line x1="445" y1="218" x2="445" y2="226" stroke="currentColor"/>
  <line x1="565" y1="218" x2="565" y2="226" stroke="currentColor"/>
  <text x="205" y="240" text-anchor="middle" font-size="12">0</text>
  <text x="325" y="240" text-anchor="middle" font-size="12">50</text>
  <text x="445" y="240" text-anchor="middle" font-size="12">100</text>
  <text x="565" y="240" text-anchor="middle" font-size="12">150</text>
  <text x="195" y="64" text-anchor="end" font-size="12">g7_hello (misc, 정적)</text>
  <rect x="205" y="46" width="50.4" height="28" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="263" y="64" font-size="12">21줄 · .text 505 B</text>
  <text x="195" y="110" text-anchor="end" font-size="12">g7_sensorkit (misc)</text>
  <rect x="205" y="92" width="208.8" height="28" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="421" y="110" font-size="12">87줄 · .text 1875 B</text>
  <text x="195" y="156" text-anchor="end" font-size="12">g7_safe (misc + kref)</text>
  <rect x="205" y="138" width="316.8" height="28" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="529" y="156" font-size="12">132줄 · .text 2814 B</text>
  <text x="195" y="202" text-anchor="end" font-size="12">g7_cdev (cdev + class)</text>
  <rect x="205" y="184" width="326.4" height="28" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="539" y="202" font-size="12">136줄 · .text 2251 B</text>
</svg>
```
그림 5 — 같은 기능에서 misc 버전(87줄)과 cdev 버전(136줄)의 차이 49줄은 거의 전부 등록·해제·에러 되감기 코드다. 수명 관리를 제대로 넣은 g7_safe(132줄)는 cdev 버전과 비슷한 크기가 된다.

읽는 법:

- misc → cdev로 바꾸면 **약 1.6배**(87 → 136줄)가 된다. 늘어난 49줄은 `alloc_chrdev_region`/`unregister_chrdev_region`, `class_register`/`class_unregister`, `.devnode`, `cdev_init`/`cdev_add`/`cdev_del`, `device_create`/`device_destroy`, `.open`, 그리고 각 단계의 `goto` 되감기다. 기능은 한 줄도 늘지 않았다.
- `.text`는 1875 → 2251 바이트(약 +20%). 바이너리 크기 차이는 작다 — misc의 이점은 크기가 아니라 **실수할 자리가 적다**는 것이다.
- `g7_safe`가 132줄로 cdev 버전만큼 커진 것은 교훈적이다. "핫 언플러그에 안전한 드라이버"의 비용은 misc냐 cdev냐보다 크다. misc가 줄여 주는 것은 등록 뼈대뿐이고, 수명 관리는 어느 쪽이든 직접 해야 한다.

### 7.3 언제 misc, 언제 cdev — 결정 흐름

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="30" y="16" width="370" height="54" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="215" y="38" text-anchor="middle" font-size="13">1. 맞는 커널 서브시스템이 있나?</text>
  <text x="215" y="58" text-anchor="middle" font-size="12">(input, IIO, hwmon, rtc, watchdog, tty, gpio …)</text>
  <rect x="450" y="16" width="210" height="54" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="555" y="38" text-anchor="middle" font-size="13">서브시스템에 등록</text>
  <text x="555" y="58" text-anchor="middle" font-size="12">노드·ABI는 서브시스템 몫</text>
  <line x1="400" y1="43" x2="448" y2="43" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="424" y="36" text-anchor="middle" font-size="12">예</text>
  <line x1="215" y1="70" x2="215" y2="106" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="240" y="93" text-anchor="middle" font-size="12">아니오</text>
  <rect x="30" y="108" width="370" height="54" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="215" y="130" text-anchor="middle" font-size="13">2. 장치(인스턴스)당 노드 하나면 되나?</text>
  <text x="215" y="150" text-anchor="middle" font-size="12">(채널별 노드·minor 범위가 필요 없다)</text>
  <rect x="450" y="108" width="210" height="54" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="555" y="130" text-anchor="middle" font-size="13">cdev</text>
  <text x="555" y="150" text-anchor="middle" font-size="12">alloc_chrdev_region(N)</text>
  <line x1="400" y1="135" x2="448" y2="135" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="424" y="128" text-anchor="middle" font-size="12">아니오</text>
  <line x1="215" y1="162" x2="215" y2="198" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="230" y="185" text-anchor="middle" font-size="12">예</text>
  <rect x="30" y="200" width="370" height="54" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="215" y="222" text-anchor="middle" font-size="13">3. 전용 major나 전용 class 이름이</text>
  <text x="215" y="242" text-anchor="middle" font-size="12">기존 ABI로 이미 정해져 있나?</text>
  <rect x="450" y="200" width="210" height="54" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="555" y="222" text-anchor="middle" font-size="13">cdev + 전용 class</text>
  <text x="555" y="242" text-anchor="middle" font-size="12">/sys/class/&lt;이름&gt;</text>
  <line x1="400" y1="227" x2="448" y2="227" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="424" y="220" text-anchor="middle" font-size="12">예</text>
  <line x1="215" y1="254" x2="215" y2="290" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="240" y="277" text-anchor="middle" font-size="12">아니오</text>
  <rect x="30" y="292" width="370" height="58" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="215" y="316" text-anchor="middle" font-size="14" font-weight="bold">misc device</text>
  <text x="215" y="338" text-anchor="middle" font-size="12">MISC_DYNAMIC_MINOR + misc_register</text>
  <rect x="450" y="292" width="210" height="58" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="555" y="314" text-anchor="middle" font-size="12">예: 설정·진단 노드,</text>
  <text x="555" y="334" text-anchor="middle" font-size="12">컨트롤 노드, 단일 장치</text>
  <text x="340" y="384" text-anchor="middle" font-size="12">misc = cdev 하나 + class(misc) + device_create를 미리 묶어 둔 것</text>
  <text x="340" y="404" text-anchor="middle" font-size="12">할 수 있는 일(read/write/ioctl/mmap/poll)은 cdev와 같다</text>
</svg>
```
그림 6 — misc를 고르기 전에 먼저 서브시스템을 찾고, 그다음 "노드 수"와 "이름·번호 ABI"를 본다.

비교표:

| 기준 | misc | cdev (+ 자기 class) |
|---|---|---|
| major | 10 공유 | 자기 것 (동적 또는 고정) |
| minor | 장치당 1개, 동적이면 256 이상 | 원하는 범위 (`alloc_chrdev_region(&base, 0, N, …)`) |
| 노드 수 | `misc_register` 한 번 = 노드 1개 | 장치 하나에 노드 여러 개 가능 (채널별, 제어/데이터 분리) |
| `/sys/class/` | 항상 `misc` | 자기 class 이름 (`/sys/class/g7sk/`) — udev 규칙에서 `SUBSYSTEM==`로 구분하기 쉽다 |
| `private_data` | `misc_open`이 miscdevice로 채움 | 드라이버 `.open`에서 `inode->i_cdev`로 직접 |
| 등록 코드 | 1 호출 | 4~5 호출 + 되감기 |
| open 직렬화 | `misc_mtx` 아래에서 `.open` 호출 (전역) | 없음 (드라이버 책임) |
| 모듈 자동 로드 | 고정 minor면 `char-major-10-N` 별칭 | 고정 major면 `char-major-M-*` 별칭 |
| 성능 | open 때 리스트 검색 1회 추가, read/write 경로는 동일 | 동일 |

misc를 **쓰지 말아야** 하는 경우:

1. 한 하드웨어에 노드가 여러 개 필요할 때 — 예: 8채널 ADC에서 채널마다 `/dev/adc0ch0..7`. misc를 8번 등록할 수도 있지만 `/sys/class/misc` 아래에 섞여 버리고 이름 충돌 위험도 커진다. cdev로 minor 범위를 받는 게 자연스럽다(그 전에 IIO를 먼저 고려).
2. 유저스페이스 ABI가 이미 major/class 이름을 기대할 때 — 예: 기존 제품의 udev 규칙이 `SUBSYSTEM=="g7sk"`를 쓰고 있다.
3. 맞는 서브시스템이 있을 때 — 이것이 가장 흔한 "misc 오용"이다. IMU라면 IIO(`/sys/bus/iio/…`, 버퍼·트리거·타임스탬프 공짜), 버튼이라면 input, 온도라면 hwmon. 업스트림 리뷰에서 "왜 misc입니까? IIO를 쓰세요"가 단골 코멘트다.

### 7.4 예제 13 — 트리에서 misc를 쓰는 곳 찾기

무엇을 확인하나: 실제 커널에서 misc가 얼마나, 어디서 쓰이는지. 읽어 볼 만한 사용자를 고른다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 && grep -rlE "misc_register\(|module_misc_device\(" --include=*.c . | wc -l; grep -rlE "cdev_add\(|cdev_device_add\(" --include=*.c . | wc -l; grep -rlE "misc_register\(|module_misc_device\(" --include=*.c . | cut -d/ -f2-3 | sort | uniq -c | sort -rn | head -8'
```

```text
244
108
     48 drivers/watchdog
     24 drivers/char
     15 drivers/misc
     14 drivers/platform
      9 drivers/s390
      9 arch/powerpc
      7 drivers/xen
      6 drivers/virt
```

출력에서 볼 것:

- misc를 등록하는 `.c` 파일이 244개, `cdev_add`/`cdev_device_add`를 직접 부르는 파일은 108개다. "char 드라이버를 직접 짠다"면 misc가 더 흔한 선택이라는 뜻이다(물론 대부분의 드라이버는 둘 다 안 쓰고 서브시스템을 쓴다 — G1).
- `drivers/watchdog`이 48개로 1위인 것은 watchdog 코어 이전의 **레거시 watchdog 드라이버**들이 `WATCHDOG_MINOR`로 직접 등록하기 때문이다. 새 watchdog 드라이버는 watchdog 코어(`watchdog_register_device`)를 쓴다 — 서브시스템이 생기면 misc 사용자가 그쪽으로 옮겨 간다는 예다.

읽어 볼 만한 in-tree misc 사용자 (6.18 소스에서 위치 확인):

| 노드 | 파일 | 볼 점 |
|---|---|---|
| `/dev/fuse` | `fs/fuse/dev.c` (2753행 `fuse_miscdevice`) | 고정 minor `FUSE_MINOR` + `MODULE_ALIAS_MISCDEV` → 열면 모듈 자동 로드 |
| `/dev/kvm` | `virt/kvm/kvm_main.c` (5586행 `kvm_dev`) | 위치 초기화자 `{ KVM_MINOR, "kvm", &kvm_chardev_ops }` — 노드 하나에 ioctl로 VM fd를 계속 만들어 내는 "팩토리 노드" |
| `/dev/vfio/vfio` | `drivers/vfio/container.c` (408행 `vfio_dev`) | `.nodename = "vfio/vfio"`, `.mode`는 `S_IRUGO`와 `S_IWUGO`를 OR 한 값 (예제 2에서 0666 확인) |
| `/dev/loop-control` | `drivers/block/loop.c` (2231행 `loop_misc`) | 블록 드라이버가 **제어용 노드 하나**를 misc로 둔다 (ioctl 전용) |
| `/dev/uinput` | `drivers/input/misc/uinput.c` (1159행) | `module_misc_device(uinput_misc)` 한 줄 등록 |
| `/dev/hwrng` | `drivers/char/hw_random/core.c` (330행 `rng_miscdev`) | `.nodename = "hwrng"`, `.groups = rng_dev_groups` — 노드에 sysfs 속성(`rng_current` 등)을 붙이는 실제 예 |
| `/dev/watchdog` | `drivers/watchdog/watchdog_dev.c` (1001행) | **misc + cdev 혼용**: 첫 watchdog만 레거시 `/dev/watchdog`(misc 10:130), 모든 watchdog은 `/dev/watchdogN`(자기 cdev) |
| `/dev/net/tun` | `drivers/net/tun.c` (3495행 `tun_miscdev`) | `.nodename = "net/tun"` + 고정 minor 별칭 |

(vfio 행: 소스의 실제 줄은 `.mode = S_IRUGO | S_IWUGO,`이다.)

---

## 8. 커널 안에서는 무슨 일이 일어나나

`misc_register()` 한 번이 내부에서 하는 일을 6.18 소스 순서대로 그리면 이렇다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="16" width="330" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="185" y="36" text-anchor="middle" font-size="12">misc_register(&amp;sk->misc)</text>
  <line x1="185" y1="46" x2="185" y2="62" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="64" width="330" height="30" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="185" y="84" text-anchor="middle" font-size="12">mutex_lock(&amp;misc_mtx)</text>
  <line x1="185" y1="94" x2="185" y2="110" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="112" width="330" height="30" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="185" y="132" text-anchor="middle" font-size="12">ida_alloc_range(256 .. 2^20−1) → minor</text>
  <line x1="185" y1="142" x2="185" y2="158" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="160" width="330" height="66" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="185" y="180" text-anchor="middle" font-size="12">device_create_with_groups(</text>
  <text x="185" y="199" text-anchor="middle" font-size="12">misc_class, parent, MKDEV(10, minor),</text>
  <text x="185" y="218" text-anchor="middle" font-size="12">drvdata = misc, groups, name)</text>
  <line x1="185" y1="226" x2="185" y2="242" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="244" width="330" height="30" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="185" y="264" text-anchor="middle" font-size="12">list_add(&amp;misc->list, &amp;misc_list) — 맨 앞</text>
  <line x1="185" y1="274" x2="185" y2="290" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="292" width="330" height="30" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="185" y="312" text-anchor="middle" font-size="12">mutex_unlock → 이제 misc_open이 찾는다</text>
  <rect x="380" y="112" width="285" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="522" y="131" text-anchor="middle" font-size="12">sysfs: device 디렉터리 + groups 속성</text>
  <text x="522" y="148" text-anchor="middle" font-size="12">/sys/class/misc/&lt;name&gt; 링크</text>
  <rect x="380" y="170" width="285" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="522" y="189" text-anchor="middle" font-size="12">devtmpfs: misc_devnode()</text>
  <text x="522" y="206" text-anchor="middle" font-size="12">→ /dev/&lt;nodename 또는 name&gt;, mode</text>
  <rect x="380" y="228" width="285" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="522" y="247" text-anchor="middle" font-size="12">uevent: MAJOR, MINOR, DEVNAME, DEVMODE</text>
  <text x="522" y="264" text-anchor="middle" font-size="12">→ udev · mdev (G9)</text>
  <line x1="350" y1="185" x2="378" y2="134" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="350" y1="193" x2="378" y2="192" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="350" y1="201" x2="378" y2="250" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="340" y="360" text-anchor="middle" font-size="12">실패하면: minor 반납 · 동적이면 minor를 255로 되돌림 · -EEXIST / -EBUSY / -EINVAL 반환 (§5)</text>
</svg>
```
그림 7 — `misc_register`의 실제 일은 `device_create_with_groups` 한 번이고, 그 아래에서 driver core(G2)가 sysfs·devtmpfs·uevent를 한꺼번에 처리한다. cdev는 만들지 않는다 — major 10의 cdev는 부팅 때 이미 있다.

전체 경로를 유저 → 커널 → 드라이버로 이으면:

| 유저 동작 | 커널 경로 | 드라이버에서 다시 만나는 곳 |
|---|---|---|
| `insmod` / probe | `misc_register` → `ida_alloc_range` → `device_create_with_groups` → `device_add` → devtmpfs·sysfs·uevent | G2(driver core, `device_add`), G8(attribute group), G9(uevent·udev) |
| `open("/dev/sensorkit0")` | `do_sys_openat2` → `vfs_open` → `chrdev_open`(major 10 → misc cdev) → `misc_open` → 드라이버 `.open` | F12 §4, G4 |
| `read(fd)` | `ksys_read` → `vfs_read` → `file->f_op->read` = `sk_read` (misc 계층 없음) | E6, G4 |
| sysfs `cat odr_hz` | kernfs → `dev_attr_show` → `odr_hz_show(this_device)` | G8 |
| `close(fd)` (마지막) | `fput` → `__fput` → `.release` → `fops_put`(모듈 참조 해제) | F12 §5, G4 |
| unbind | `device_release_driver` → `sk_remove` → `misc_deregister` → `device_destroy` → devres 해제 | G3(devres 순서), §6 |

`misc_deregister`는 `list_del_init` → `device_destroy` → `misc_minor_free` 순서이고, 동적 minor였다면 `minor`를 다시 255로 돌려놓는다(그래서 같은 `miscdevice`를 다시 `misc_register`해도 동작한다).

---

## 9. Embedded 실무에서 다시 보기

### 9.1 센서 키트의 "제어 노드" — misc가 딱 맞는 자리

센서 키트에서 IMU 데이터 자체는 IIO 드라이버(H 모듈)로 내보내는 게 정석이다. 그런데 제품에는 IIO에 맞지 않는 작은 인터페이스가 꼭 생긴다. 예를 들어:

- 센서 동기화 FPGA에 "모든 센서 트리거를 동시에 리셋" 명령을 보내는 노드 하나
- 공장 보정 데이터를 EEPROM에 쓰는 노드 하나(쓰기는 공장 툴만)
- 시간 동기화(PPS ↔ IMU 타임스탬프) 상태를 `poll()`로 기다리는 노드 하나

전부 "장치당 노드 하나, ioctl 몇 개" 형태다. 이런 것은 misc + `.groups`(상태는 sysfs로) + `.mode = 0`(권한은 udev 규칙으로)으로 만든다. 드라이버가 100줄 안쪽이고, 리뷰어도 금방 읽는다.

### 9.2 `/dev` 이름이 부팅마다 바뀐다

`ida` 번호는 probe 순서대로 나간다. deferred probe(G3)로 앞 IMU가 늦게 뜨면 `sensorkit0`과 `sensorkit1`이 뒤바뀐다. 해결:

1. DT `aliases { sensorkit0 = &imu_front; };` + `of_alias_get_id(np, "sensorkit")`로 번호를 고정하거나,
2. 이름은 그대로 두고 udev 규칙에서 sysfs의 부모 DT 노드(예제 6의 `device` 링크)를 보고 `/dev/imu-front` 같은 심볼릭 링크를 만든다.

카메라·시리얼 장치에서 늘 나오는 문제와 같다. 유저스페이스 앱이 `/dev/sensorkit0`을 하드코딩하지 않게 하는 것이 핵심이다.

### 9.3 유지보수 모드에서 unbind/rebind — 수명 버그가 드러나는 순간

현장 장비에서 "센서가 멈추면 드라이버를 unbind/bind 해서 살린다"는 복구 스크립트가 흔하다. 이때 데이터 수집 데몬이 노드를 열어 두고 있으면 정확히 예제 9가 재현된다. 증상은 Oops가 아니라 **며칠 뒤 무관한 곳의 메모리 손상**일 수도 있다. 그래서 드라이버를 리뷰할 때 확인할 것:

1. `devm_kzalloc`한 구조체를 `file->private_data`가 가리키는가? → 열린 fd가 있으면 remove 뒤에 쓰인다.
2. `.release`에서 그 구조체를 만지는가?
3. 테스트에 "fd를 연 채로 unbind → read → close"가 있는가? (`slab_debug=FZP` 또는 KASAN 커널로)

### 9.4 Don의 경험과 연결

SSD 펌웨어에서 NVMe 큐를 삭제(Delete I/O SQ)할 때, 이미 큐에 들어온 커맨드가 다 끝날 때까지 큐 메모리를 풀면 안 됐다. "삭제 요청 → 새 커맨드 거부(gone) → 진행 중인 커맨드 완료 대기 → 마지막에 메모리 해제"가 `g7_safe`의 `gone` + `kref`와 같은 구조다. 커널에서는 "진행 중인 커맨드" 대신 "열린 file"이 참조를 쥔다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `.owner = THIS_MODULE` 누락 | 열린 동안 rmmod가 성공하고, 이후 read/close에서 Oops | `fops_get`이 잡을 모듈 참조가 없음 | fops에 항상 `.owner = THIS_MODULE` |
| 전역 `static struct miscdevice` 하나로 여러 인스턴스 처리 | 두 번째 probe에서 `-EEXIST` 또는 상태 섞임 | 장치마다 miscdevice가 하나씩 있어야 함 | 장치 구조체에 embed + `container_of` (§4) |
| `struct miscdevice *misc` 포인터로 선언 | `container_of`가 엉뚱한 주소 | `container_of`는 **embed된 멤버**에만 쓴다 | `struct miscdevice misc;` (포인터 아님) |
| 흔한 이름 (`"imu"`, `"sensor"`) | 다른 드라이버와 `sysfs: cannot create duplicate filename` + `-EEXIST` | `/sys/class/misc/` 안에서 이름이 겹침 (예제 8) | 드라이버 고유 접두어 + 인스턴스 번호 |
| 남의 고정 minor 사용 | 나중에 진짜 드라이버가 `-EBUSY`로 실패 | 정적 minor는 공유 자원 | 항상 `MISC_DYNAMIC_MINOR` |
| 고정 minor로 256 이상 지정 | `Invalid fixed minor` + `-EINVAL` | 255 초과는 동적 영역 | 동적 사용 |
| `devm_kzalloc` 구조체 + 열린 fd + unbind | unbind 뒤 read에서 Oops 또는 조용한 메모리 손상 | `misc_deregister`는 열린 file을 기다리지 않음 (예제 9) | kref + gone 플래그 (예제 10) 또는 서브시스템 사용 |
| `misc_register` 뒤에 `device_create_file`로 속성 추가 | udev 규칙이 가끔 속성을 못 봄 (레이스) | uevent가 속성 생성보다 먼저 나감 | `.groups`를 등록 전에 채운다 |
| `.open`에서 오래 sleep (펌웨어 로드·I2C 재시도) | 다른 misc 장치 open·등록이 전부 멈춤 | `misc_open`이 `misc_mtx`를 잡은 채 `.open`을 부름 | 느린 초기화는 probe나 첫 read/ioctl로 옮긴다 |
| 보안 노드에 `.mode = 0666` | 일반 사용자가 위험한 ioctl 호출 | 권한 정책을 커널에 박음 | `.mode` 비우고 udev 규칙으로 그룹 권한 |

---

## 11. 면접에서 이렇게 말한다

**Q.** What is a misc device and why would you use one instead of a regular character device?

**A.** misc는 major 10을 공유하는 char device다. misc 코어가 부팅 때 major 10 전체를 cdev 하나로 잡아 두고, 드라이버는 `struct miscdevice`에 이름·fops를 채워 `misc_register` 한 번만 부른다. 그러면 동적 minor, `/dev` 노드, `/sys/class/misc` 항목이 한 번에 생긴다. 장치당 노드 하나면 충분하고 맞는 서브시스템이 없을 때 쓴다. 실제로 같은 드라이버를 cdev로 짜 보니 87줄이 136줄이 됐고, 늘어난 건 전부 등록·되감기 코드였다.

> A misc device is a character device that shares major number 10. The misc core registers one cdev for the whole major at boot, so a driver just fills a struct miscdevice with a name and fops and calls misc_register. That one call allocates a dynamic minor, creates the /dev node through devtmpfs and the /sys/class/misc entry. I use it when I need a single node per device and no existing subsystem fits. In a side-by-side test, the cdev version of the same driver was about 1.6 times longer, and all of the extra lines were registration and error unwinding.

**꼬리 질문**: "그럼 항상 misc 쓰면 되나요?" → 아니다. 노드가 여러 개(채널별) 필요하거나, 기존 ABI가 major/class 이름을 기대하거나, IIO·input·hwmon 같은 서브시스템이 있으면 그쪽이다.

**Q.** In a misc driver, how do you get from the struct file back to your device structure?

**A.** `misc_open`이 `file->private_data`에 그 장치의 `struct miscdevice` 주소를 넣고 나서 드라이버의 `.open`을 부른다. 드라이버는 `miscdevice`를 장치 구조체에 embed해 두고 `container_of(file->private_data, struct my_dev, misc)`로 꺼낸다. 그래서 `.open`이 없어도 된다. sysfs 콜백에서는 `dev_get_drvdata(dev)`가 같은 miscdevice 포인터를 주니까 똑같이 `container_of`를 쓴다.

> misc_open looks up the miscdevice by minor, stores its address in file->private_data, swaps in the driver's fops, and then calls the driver's open if there is one. I embed the miscdevice inside my device struct, so container_of(file->private_data, struct my_dev, misc) gives me the device. For sysfs callbacks the same pointer comes from dev_get_drvdata on the misc device, because misc_register passes the miscdevice as drvdata.

**꼬리 질문**: "cdev면요?" → `.open`에서 `container_of(inode->i_cdev, struct my_dev, cdev)`로 찾아 `private_data`에 직접 넣는다.

**Q.** What happens if the device is unregistered while a process still has it open?

**A.** `misc_deregister`는 노드와 sysfs를 지우고 리스트에서 빼지만, 이미 열린 file은 건드리지 않는다. 그 file의 `f_op`는 여전히 드라이버 fops라서 read를 하면 드라이버 코드가 불린다. 장치 구조체를 devm으로 잡았다면 이미 해제됐으니 use-after-free다. 실제로 `slab_debug` poison을 켜고 unbind 후 read를 했더니 `0x6b6b…` 주소에서 Oops가 났다. 고치는 법은 구조체를 kref로 관리해서 open이 참조를 잡고, remove는 gone 플래그를 세운 뒤 참조를 놓고, 마지막 close에서 해제하는 것이다. 모듈 언로드는 `.owner` 참조 때문에 열린 동안 막힌다.

> misc_deregister removes the node, the sysfs entry and the list entry, but it doesn't wait for open files. Those files still point at the driver's fops, so a later read goes straight into driver code. If the device struct was devm-allocated, it is already freed, so that's a use-after-free. I reproduced it with slab poisoning: unbind, then read on the old fd, oopsed on a 0x6b6b pointer. The fix is to refcount the struct with a kref: open takes a reference, remove sets a gone flag and drops its reference, file operations return -ENODEV when gone, and the last release frees the memory. Module unload is a separate case, and the fops owner reference already blocks it.

**꼬리 질문**: "open과 deregister가 경쟁하면?" → misc는 `misc_open`이 `misc_mtx`를 잡은 채 `.open`을 부르고 `misc_deregister`도 같은 뮤텍스를 잡으므로, `.open` 안의 `kref_get`은 안전하다.

**Q.** Should you use a fixed minor number or MISC_DYNAMIC_MINOR?

**A.** 새 드라이버는 동적이다. 고정 minor는 정적 `/dev`와 `char-major-10-N` 별칭으로 모듈을 자동 로드하던 시절의 것이고, 공유 자원이라 남의 번호를 쓰면 나중에 그 드라이버가 `-EBUSY`로 실패한다. 6.18에서는 동적 minor가 256 이상에서 나오고, 255 초과를 고정으로 요청하면 `-EINVAL`이다. 유저스페이스는 번호가 아니라 이름으로 찾게 한다.

> Dynamic, for anything new. Fixed minors exist for legacy reasons: static /dev and on-demand module loading through the char-major-10-N alias. They're a shared namespace, so if you borrow one, the real owner fails with -EBUSY later. On 6.18 dynamic minors are allocated above 255, and asking for a fixed minor above 255 returns -EINVAL. User space should find the node by name and never by number.

**Q.** How do you control the permissions of a misc device node?

**A.** 두 가지다. `miscdevice.mode`를 주면 devtmpfs가 노드를 만들 때 그 권한을 쓰고, uevent에도 `DEVMODE`로 실린다 — udev 없는 BusyBox에서도 된다. 제품에서는 보통 `.mode`를 비워 0600으로 두고 udev 규칙으로 그룹과 모드를 정한다. 정책을 커널에 박지 않으려고, 그리고 `.mode`로는 그룹을 못 정하니까.

> Either set miscdevice.mode, which devtmpfs applies when it creates the node and which also appears as DEVMODE in the uevent, or leave it at the 0600 default and let a udev rule set the group and mode. `.mode` works even in a BusyBox initramfs with no udev, but it hard-codes policy in the kernel and can't set a group. For product images I prefer udev rules, and I keep `.mode` for nodes that are harmless to open.

**Q.** You need to expose a few tunables alongside the misc node. How?

**A.** `.groups`에 attribute group을 넣고 `misc_register`를 부른다. 그러면 `device_create_with_groups`가 device와 속성 파일을 같이 만들어서 uevent가 나갈 때 이미 속성이 있다. 콜백에서는 `dev_get_drvdata`가 miscdevice를 주니까 `container_of`로 장치를 찾는다. 등록 뒤에 `device_create_file`을 하면 udev와 레이스가 생긴다.

> Put an attribute group in miscdevice.groups before calling misc_register. It goes to device_create_with_groups, so the attributes exist before the uevent fires and udev rules can rely on them. In show and store, dev_get_drvdata returns the miscdevice, so container_of gets me back to my device struct. Adding files after registration with device_create_file races with udev.

**Q.** Write a minimal misc driver on the whiteboard.

**A.** §2의 25줄을 쓴다. `simple_read_from_buffer`를 쓰는 `.read`, `.owner = THIS_MODULE`이 들어간 fops, `MISC_DYNAMIC_MINOR`·이름·fops를 채운 `struct miscdevice`, 그리고 `module_misc_device()` 한 줄. 다 쓰고 나서 "실제 장치라면 probe에서 장치 구조체에 embed하고 `container_of`를 쓴다"를 덧붙인다.

> I'd write a read handler using simple_read_from_buffer, a file_operations with owner set to THIS_MODULE, a static miscdevice with MISC_DYNAMIC_MINOR, a name and the fops, and module_misc_device to generate init and exit. Then I'd say that for real hardware the miscdevice lives inside a per-device struct allocated in probe, and the handlers recover it with container_of.

---

## 12. 직접 해보기

1. **개념**: `misc_open`이 고정 minor일 때만 `request_module`을 부르는 이유는 무엇인가? 동적 minor 장치의 모듈은 어떻게 자동 로드되나?
   정답/힌트: 동적 minor는 등록 전에는 번호가 정해지지 않아서 `char-major-10-N` 별칭을 미리 선언할 수 없다. 동적 minor 드라이버의 자동 로드는 장치 매칭(`MODULE_DEVICE_TABLE` → modalias → udev `modprobe`, G9)으로 한다.

2. **코드**: `g7_sensorkit`에 `.nodename`을 추가해서 노드가 `/dev/sensorkit/imu-front`처럼 DT `label`로 생기게 바꿔 보라. `/proc/misc`와 `/sys/class/misc`의 이름은 무엇으로 남나?
   정답/힌트: `sk->misc.nodename = devm_kasprintf(dev, GFP_KERNEL, "sensorkit/%s", sk->label)`. `misc_devnode`가 nodename을 쓰므로 `/dev` 경로만 바뀌고, `/proc/misc`와 `/sys/class/misc/`는 여전히 `name`(`sensorkit0`)이다. `uevent`의 `DEVNAME`도 확인하라.

3. **코드**: DT에 `aliases { sensorkit5 = "/g7-sensorkit-rear"; };`를 넣고, probe에서 `of_alias_get_id(dev->of_node, "sensorkit")`가 0 이상이면 그 번호를 이름에 쓰도록 바꿔 보라.
   정답/힌트: 이름이 `sensorkit5`로 고정된다. 이 경우 ida와 번호가 겹치지 않게 `ida_alloc_range(&sk_ida, id, id, …)`로 그 번호를 예약해야 한다.

4. **관찰**: 예제 9를 `slab_debug=FZP` 없이 돌려 보라. Oops가 나는가? 나지 않는다면 출력의 `label=`·`seq=` 값은 무엇인가?
   정답/힌트: poison이 없으면 해제된 메모리에 옛 값이 남아 있을 수 있어서 "정상처럼 보이는" 출력이 나올 수 있다. 이것이 UAF가 무서운 이유다. 결과는 실행마다 다를 수 있다(이 노트에서는 돌리지 않았다).

5. **관찰 (ftrace)**: `set_graph_function`에 `misc_register`를 걸고 `insmod g7_sensorkit.ko`를 해서, `device_create_with_groups` 아래에서 `devtmpfs_create_node`와 `kobject_uevent`가 불리는지 확인하라.
   정답/힌트: `max_graph_depth`를 6~8 정도로 올려야 보인다. 그림 7의 오른쪽 세 상자가 그 함수들이다.

6. **설계**: 8채널 ADC 보드에 채널마다 노드를 주고 싶다. misc로 8번 등록하는 방안과 cdev로 minor 8개를 받는 방안을 비교하고, 그 전에 확인해야 할 서브시스템을 말하라.
   정답/힌트: misc 8번은 `/sys/class/misc`에 섞이고 이름 충돌 위험, 채널 묶음 표현이 어렵다. cdev는 minor 범위와 자기 class로 깔끔하다. 하지만 먼저 IIO(채널·스케일·버퍼·트리거가 이미 있다)를 검토해야 한다.

7. **코드**: `g7_safe`에 `.poll`을 추가해서 unbind 되면 `EPOLLHUP | EPOLLERR`를 돌려주게 하라(G6 연결). 대기 중인 프로세스를 깨우려면 remove에서 무엇을 해야 하나?
   정답/힌트: `sk`에 `wait_queue_head_t`를 두고, remove에서 `gone = true` 후 `wake_up_interruptible(&sk->wq)`. `.poll`은 `poll_wait` 후 `gone`이면 hangup 마스크를 돌려준다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| misc device | 잡다한 문자 디바이스 | major 10을 공유하는 char device. `misc_register` 한 번으로 노드까지 |
| `struct miscdevice` | misc 장치 기술자 | minor·name·fops·parent·groups·nodename·mode를 담는 구조체 |
| `MISC_MAJOR` | 10 | misc 서브시스템이 부팅 때 통째로 잡는 major |
| `MISC_DYNAMIC_MINOR` | 255 | "minor를 알아서 골라 달라"는 표시. 6.18에서 실제 값은 256 이상 |
| 고정 minor | 정적 배정 번호 | `miscdevice.h`에 정의된 역사적 번호(예: FUSE 229). 새 드라이버는 쓰지 않는다 |
| `misc_open` | misc 공통 open | minor로 `misc_list`를 찾아 `private_data`를 채우고 fops를 바꾼 뒤 드라이버 `.open` 호출 |
| `misc_mtx` | misc 전역 뮤텍스 | 리스트·등록·해제·open을 직렬화. 드라이버 `.open`도 이 아래서 불린다 |
| `replace_fops` | fops 교체 매크로 | `file->f_op`를 새 fops로 바꾸고 옛 fops 모듈 참조를 놓는다 |
| `container_of` | 부모 구조체 역산 | 멤버 주소에서 그 멤버의 오프셋을 빼서 부모 구조체 주소를 얻는다 |
| `module_misc_device` | 등록 매크로 | init/exit에서 `misc_register`/`misc_deregister`만 하는 모듈의 보일러플레이트 제거 |
| `.groups` | sysfs 속성 그룹 | 노드와 동시에 만들 attribute 목록. uevent 레이스 방지 |
| `.nodename` | `/dev` 경로 | `"net/tun"`처럼 하위 디렉터리에 노드를 둘 때 |
| devtmpfs | 커널이 채우는 `/dev` | `device_add` 때 커널이 직접 노드를 만드는 파일시스템. udev 없이도 노드가 생긴다 |
| ida | ID 할당기 | 작은 정수 ID를 할당·반납하는 커널 자료구조. 인스턴스 번호와 misc minor에 쓰인다 |
| `slab_debug` | slab 디버그 부팅 인자 | `F`(sanity) `Z`(red zone) `P`(poison) 등. 해제된 객체를 0x6b로 채운다 |
| use-after-free | 해제 후 사용 | 이미 해제된 메모리를 계속 쓰는 버그. 열린 fd + unbind가 전형적 원인 |
| kref | 참조 카운트 | `kref_get`/`kref_put` + 해제 콜백. 마지막 사용자가 객체를 푼다 |

---

## 14. 요약 & 체크리스트

misc device는 "major 10을 공유하는 cdev + 미리 만들어 둔 `misc` class"다. 부팅 때 `misc_init()`이 major 10 전체를 `misc_fops` 하나로 잡아 두고, 드라이버는 `struct miscdevice`를 채워 `misc_register()`만 부르면 ida에서 동적 minor(6.18에서는 256 이상)를 받고 `device_create_with_groups`로 `/dev` 노드·sysfs·uevent가 한 번에 생긴다. open 때 `misc_open()`이 `misc_mtx` 아래에서 minor로 리스트를 찾아 `file->private_data = miscdevice`를 넣고 fops를 드라이버 것으로 바꾼다. 그래서 드라이버는 `.open` 없이도 `container_of(file->private_data, struct my_dev, misc)`로 장치를 찾고, sysfs 콜백에서도 `dev_get_drvdata`로 같은 포인터를 받는다. 장치마다 miscdevice를 embed하면 DT 인스턴스마다 노드를 하나씩 만들 수 있다(`g7_sensorkit`: 10:258, 10:259, 0666, `odr_hz`). 이름 충돌은 sysfs에서 `-EEXIST`, 고정 minor 충돌은 `-EBUSY`, 255 초과 고정 minor는 `-EINVAL`이다. `misc_deregister`는 열린 fd를 기다리지 않으므로 devm 구조체 + 열린 fd + unbind는 use-after-free가 되고(실제 Oops 확인), kref + gone 플래그로 고친다. 같은 기능을 cdev로 짜면 87줄이 136줄이 되지만, 노드가 여러 개 필요하거나 자기 class·major가 ABI이면 cdev, 맞는 서브시스템이 있으면 둘 다 아니다.

- [ ] misc가 major 10 하나를 여러 드라이버가 나눠 쓰는 구조를 그림으로 설명할 수 있다
- [ ] `struct miscdevice`의 `minor`·`name`·`fops`·`parent`·`groups`·`nodename`·`mode`가 각각 어디에 반영되는지 말할 수 있다
- [ ] `misc_open`이 하는 일 네 가지(검색, `fops_get`, `private_data`, `replace_fops` + `.open` 호출)를 순서대로 말할 수 있다
- [ ] `container_of(file->private_data, …)`와 `container_of(dev_get_drvdata(dev), …)`가 같은 구조체를 찾는 이유를 설명할 수 있다
- [ ] 25줄 화이트보드 misc 드라이버를 보지 않고 쓸 수 있다
- [ ] DT 인스턴스마다 misc 노드를 하나씩 만드는 platform 드라이버를 짤 수 있다
- [ ] 동적 minor를 써야 하는 이유와 고정 minor의 역사적 용도(정적 `/dev`, `char-major-10-N` 자동 로드)를 말할 수 있다
- [ ] 이름 충돌·minor 충돌·잘못된 minor 때 각각의 에러 코드를 말할 수 있다
- [ ] 열린 채로 unbind 할 때 왜 UAF가 나는지, kref + gone으로 어떻게 고치는지 설명할 수 있다
- [ ] `.mode`와 udev 규칙 중 무엇을 언제 쓰는지 말할 수 있다
- [ ] misc vs cdev vs 서브시스템을 결정 흐름(그림 6)으로 고를 수 있다
- [ ] fuse, kvm, loop-control, hwrng, watchdog 중 두 개 이상의 misc 사용 방식을 소스에서 짚을 수 있다

## 참고 자료

- 랩 소스 (elixir.bootlin.com의 v6.18에서도 같은 경로): `drivers/char/misc.c`(`misc_open`, `misc_register`, `misc_deregister`, `misc_devnode`, `misc_init`), `include/linux/miscdevice.h`(정적 minor 목록, `MISC_DYNAMIC_MINOR`, `module_misc_device`, `MODULE_ALIAS_MISCDEV`), `include/uapi/linux/major.h`(`MISC_MAJOR`), `fs/char_dev.c`(`chrdev_open`), `include/linux/fs.h`(`replace_fops`, `fops_get`)
- 읽어 볼 misc 사용자: `fs/fuse/dev.c`, `virt/kvm/kvm_main.c`, `drivers/vfio/container.c`, `drivers/block/loop.c`, `drivers/input/misc/uinput.c`, `drivers/char/hw_random/core.c`, `drivers/watchdog/watchdog_dev.c`, `drivers/net/tun.c`
- 커널 문서: [Linux allocated devices](https://docs.kernel.org/admin-guide/devices.html) (major 10 misc minor 배정표), [Driver Model — Device Drivers](https://docs.kernel.org/driver-api/driver-model/driver.html), [SLUB debugging (slab_debug)](https://docs.kernel.org/mm/slub.html), [Kernel objects / kref](https://docs.kernel.org/core-api/kref.html)
- Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman, "Linux Device Drivers, 3rd Edition" 3장(Char Drivers) — misc는 직접 다루지 않지만 cdev·`private_data`·`container_of` 설명의 원전 (API 일부는 현재 커널과 다르다)
- Bootlin "Linux kernel and driver development" 교육 슬라이드 — misc driver 실습 절
- 이 노트와 이어지는 노트: F12(VFS — `chrdev_open`·`misc_open` ftrace), G3(platform driver·devres), G4(cdev), G6(poll), G8(sysfs attribute), G9(udev·devtmpfs 권한)
