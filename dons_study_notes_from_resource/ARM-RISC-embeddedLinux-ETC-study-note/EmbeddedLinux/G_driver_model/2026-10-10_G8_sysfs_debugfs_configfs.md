# G8. sysfs · debugfs · configfs — 드라이버가 무엇을 어디에 노출하나

> **이 노트를 다 읽으면**: sysfs 속성을 `DEVICE_ATTR_RW/RO/WO` + `sysfs_emit`로 올바르게 만들고, 잘못된 입력에 `-EINVAL`을 돌려 셸에서 어떻게 보이는지 설명할 수 있다 · attribute group, `is_visible`, binary attribute, `sysfs_notify` + `poll(POLLPRI)`을 한 드라이버에 넣고 `.dev_groups`가 왜 uevent 경쟁을 막는지 실측으로 보여 줄 수 있다 · debugfs에 `debugfs_create_regset32`로 레지스터 덤프, u32 knob, seq_file 통계를 붙일 수 있다 · configfs가 "유저가 mkdir로 커널 객체를 만드는" 인터페이스라는 것과 procfs에 새 장치 파일을 만들면 안 되는 이유를 말할 수 있다
> **왜 필요한가**: 센서 드라이버를 쓰면 "sample rate를 런타임에 바꾸게 해 주세요", "레지스터를 덤프할 방법이 필요해요", "임계값 넘으면 앱이 알게 해 주세요"라는 요청이 반드시 온다. 면접에서는 "sysfs와 debugfs 차이?", "sysfs 파일 규칙은?", "sysfs 속성을 probe에서 `sysfs_create_group`으로 만들면 뭐가 문제?", "ioctl 대신 sysfs를 쓰는 기준은?"으로 나온다
> **Don 기준 난이도**: 레지스터 맵, 디버그 셸 명령, 펌웨어의 "vendor-specific 로그 페이지"·"디버그 커맨드"는 이미 익숙하다 / 새로 배울 부분은 kernfs 위의 sysfs 규칙(값 하나 = 파일 하나, ABI), driver core가 속성을 만드는 시점, debugfs·configfs 헬퍼 API
> **선행 노트**: G2(디바이스 모델·kobject·sysfs 트리), G3(platform driver·`devm_*`), G4(문자 디바이스), G5(ioctl — 언제 sysfs가 나은가), G6(poll), D8(드라이버에서 DT 읽기)

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어 시절을 떠올려 보자. 호스트가 드라이브와 대화하는 길은 여러 갈래였다. 정식 NVMe 명령(Get/Set Features)은 **스펙에 박힌 약속**이라 함부로 바꿀 수 없다. 반면 사내 디버그 툴이 쓰는 vendor-specific 명령이나 UART 디버그 셸은 **팀 안에서만 쓰는 도구**라 펌웨어 버전마다 형식이 바뀌어도 아무도 뭐라 하지 않는다. 그리고 "네임스페이스 만들기"처럼 **호스트가 드라이브 안에 새 객체를 생성**하는 명령이 따로 있다.

Linux 드라이버도 똑같이 세 갈래(+ 레거시 하나)를 갖고 있고, 각각 별도의 가상 파일시스템이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="660" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="33" text-anchor="middle" font-size="13">유저스페이스: 셸 스크립트 · udev · 서비스 데몬 · 개발자</text>
  <line x1="87" y1="46" x2="87" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="252" y1="46" x2="252" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="417" y1="46" x2="417" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="582" y1="46" x2="582" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="10" y="70" width="155" height="150" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="87" y="94" text-anchor="middle" font-size="14">sysfs</text>
  <text x="87" y="116" text-anchor="middle" font-size="12">/sys</text>
  <text x="87" y="138" text-anchor="middle" font-size="12">커널이 객체를 만든다</text>
  <text x="87" y="160" text-anchor="middle" font-size="12">값 하나 = 파일 하나</text>
  <text x="87" y="182" text-anchor="middle" font-size="12">설정 · 상태 · 토폴로지</text>
  <text x="87" y="204" text-anchor="middle" font-size="12">ABI 안정 · 문서 필수</text>
  <rect x="175" y="70" width="155" height="150" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="252" y="94" text-anchor="middle" font-size="14">debugfs</text>
  <text x="252" y="116" text-anchor="middle" font-size="12">/sys/kernel/debug</text>
  <text x="252" y="138" text-anchor="middle" font-size="12">개발자 디버그 전용</text>
  <text x="252" y="160" text-anchor="middle" font-size="12">형식 자유 · seq_file</text>
  <text x="252" y="182" text-anchor="middle" font-size="12">레지스터 덤프 · 통계</text>
  <text x="252" y="204" text-anchor="middle" font-size="12">ABI 없음 · 수시 변경</text>
  <rect x="340" y="70" width="155" height="150" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="417" y="94" text-anchor="middle" font-size="14">configfs</text>
  <text x="417" y="116" text-anchor="middle" font-size="12">/sys/kernel/config</text>
  <text x="417" y="138" text-anchor="middle" font-size="12">mkdir로 객체 생성</text>
  <text x="417" y="160" text-anchor="middle" font-size="12">유저가 만들고 설정</text>
  <text x="417" y="182" text-anchor="middle" font-size="12">USB gadget · nvmet</text>
  <text x="417" y="204" text-anchor="middle" font-size="12">ABI 안정 · 문서 필수</text>
  <rect x="505" y="70" width="155" height="150" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
  <text x="582" y="94" text-anchor="middle" font-size="14">procfs</text>
  <text x="582" y="116" text-anchor="middle" font-size="12">/proc</text>
  <text x="582" y="138" text-anchor="middle" font-size="12">프로세스 정보용</text>
  <text x="582" y="160" text-anchor="middle" font-size="12">옛 장치 파일이 남음</text>
  <text x="582" y="182" text-anchor="middle" font-size="12">새 장치 파일 금지</text>
  <text x="582" y="204" text-anchor="middle" font-size="12">레거시 · 유지만</text>
  <line x1="87" y1="220" x2="87" y2="242" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="252" y1="220" x2="252" y2="242" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="417" y1="220" x2="417" y2="242" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="582" y1="220" x2="582" y2="242" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="10" y="244" width="660" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="267" text-anchor="middle" font-size="13">커널: struct device · driver · 서브시스템 객체 (kobject, kernfs 노드)</text>
</svg>
```
그림 1 — 드라이버가 유저에게 무언가를 보여 주는 네 개의 가상 파일시스템. 핵심 차이는 "누가 객체를 만드나"와 "ABI로 약속하나"다. sysfs·configfs 파일은 한 번 내보내면 사실상 영원히 지켜야 하는 약속이고, debugfs는 언제 바꿔도 되는 개발자 도구다.

Don의 펌웨어 경험에 대응시키면 이렇다.

| Linux | SSD 펌웨어 비유 | 바꿔도 되나 |
|---|---|---|
| sysfs | NVMe Get/Set Features, Identify 필드 | 안 된다 (스펙 = ABI) |
| debugfs | UART 디버그 셸, vendor-specific 로그 페이지 | 언제든 |
| configfs | Namespace Management (호스트가 객체 생성) | 안 된다 |
| procfs | 오래전 만든 레거시 명령 — 고객이 쓰니 지우지는 못함 | 새로 만들지 않는다 |
| 문자 디바이스 `read`/`ioctl`/`mmap` (G4·G5) | 데이터 경로 (I/O 큐, DMA) | 안 된다 |

이 노트는 가상의 IMU "sensorkit" 드라이버 `g8_sensorkit` 하나에 이 모든 것을 붙여 가며 진행한다.

1. sysfs 규칙과 ABI 문서 (§1) → 속성 매크로와 show/store (§2) → `sysfs_emit` (§3) → 에러가 셸에서 어떻게 보이나 (§4) → 권한 (§5)
2. attribute group과 `is_visible`, binary attribute (§6) → `sysfs_notify` + poll (§7) → **속성은 uevent 전에 있어야 한다** (§8)
3. debugfs: regset32 레지스터 덤프, knob, seq_file (§9)
4. configfs: mkdir로 채널 만들기, 진짜 사용자 USB gadget (§10) → procfs (§11)
5. sysfs vs `read()` vs `ioctl` 비용 측정 (§12)

> 실행 환경: 커널 모듈은 Lima VM `elinux`(호스트 커널 `uname -r` = `7.0.0-38-generic`) 안에서 공용 랩의 **Linux 6.18.55** 빌드 트리로 컴파일하고, QEMU `virt`(cortex-a57, 512 MB, TCG, CPU 1개) 게스트에서 insmod해서 확인했다. 유저 프로그램은 VM에서 `gcc -static -Wall -Wextra -O2`로 빌드해 initramfs에 넣었다. 아래 모든 출력은 실제 실행 결과이고, dmesg 타임스탬프·주소는 실행마다 다르다. 모듈 빌드·유저 프로그램 빌드 모두 **경고 0개**다 (일부러 실패시키는 §5.2 예제 제외).

---

## 1. sysfs의 규칙 — "값 하나 = 텍스트 파일 하나, 그리고 그것은 ABI다"

### 1.1 직관

sysfs(`/sys`)는 G2에서 본 것처럼 커널의 객체 트리(kobject)를 디렉터리로 보여 주는 파일시스템이다. 디렉터리 = 객체(디바이스, 드라이버, 버스), 파일 = 그 객체의 **속성(attribute)** 하나. 드라이버가 할 일은 자기 디바이스 디렉터리에 속성 파일을 몇 개 더 붙이는 것이다.

펌웨어로 치면 "레지스터 맵을 유저에게 노출하는데, 레지스터 대신 **이름 붙은 텍스트 값**으로 노출한다"고 보면 된다. `odr` 파일에 `104`가 들어 있고, `416`을 쓰면 ODR이 바뀐다.

### 1.2 규칙 (Documentation/filesystems/sysfs.rst의 요지)

| 규칙 | 뜻 | 어기면 |
|---|---|---|
| 값 하나 = 파일 하나 | 한 파일에 한 값. 같은 타입의 배열(예: 선택 가능한 값 목록)만 공백으로 나열 허용 | 유저가 파싱해야 하고, 필드 추가가 ABI 깨기가 된다 |
| ASCII 텍스트 | 숫자는 10진수 텍스트 + 줄바꿈. 바이너리는 binary attribute로 분리 | `cat`으로 못 보고, 엔디안·구조체 패딩이 ABI가 된다 |
| PAGE_SIZE 이하 | show 버퍼는 한 페이지(arm64 기본 4 KiB) | 잘린다 (§3) |
| 문서화 | 새 속성은 `Documentation/ABI/`에 항목 추가 | 리뷰에서 거절된다 |
| 영원히 유지 | 한 번 나간 이름·형식·단위는 바꾸지 않는다 | 유저 스크립트가 깨진다 ("We don't break userspace") |

"값 하나" 규칙이 왜 이렇게 강조될까? `/proc/meminfo`처럼 여러 값을 한 파일에 넣으면, 다음 커널에서 줄 하나만 추가해도 파싱 코드를 대충 짠 유저 프로그램이 깨진다. 파일 하나에 값 하나면 새 값은 **새 파일**로 추가하면 되므로 기존 유저는 영향이 없다.

### 1.3 예제 1 — 진짜 ABI 문서 하나 읽기

무엇을 확인하나: 랩 커널 소스의 `Documentation/ABI/testing/sysfs-bus-iio`에서 IIO 센서의 `sampling_frequency` 항목을 본다. 우리 `odr` 속성의 "정식 버전"이다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55; grep -n "sampling_frequency$" -A16 Documentation/ABI/testing/sysfs-bus-iio | head -22'
```

```text
94:What:		/sys/bus/iio/devices/iio:deviceX/sampling_frequency
95:What:		/sys/bus/iio/devices/iio:deviceX/in_intensity_sampling_frequency
96:What:		/sys/bus/iio/devices/iio:deviceX/buffer/sampling_frequency
97:What:		/sys/bus/iio/devices/iio:deviceX/events/sampling_frequency
98:What:		/sys/bus/iio/devices/triggerX/sampling_frequency
99-KernelVersion:	2.6.35
100-Contact:	linux-iio@vger.kernel.org
101-Description:
102-		Some devices have internal clocks.  This parameter sets the
103-		resulting sampling frequency.  In many devices this
104-		parameter has an effect on input filters etc. rather than
105-		simply controlling when the input is sampled.  As this
106-		affects data ready triggers, hardware buffers and the sysfs
107-		direct access interfaces, it may be found in any of the
108-		relevant directories.  If it affects all of the above
109-		then it is to be found in the base device directory.
110-
111-		The stm32-timer-trigger has the additional characteristic that
112-		a sampling_frequency of 0 is defined to stop sampling.
113-
114-What:		/sys/bus/iio/devices/iio:deviceX/sampling_frequency_available
```

출력에서 볼 것:

- 항목 형식은 `What:`(경로) / `KernelVersion:`(처음 들어간 버전) / `Contact:` / `Description:`이다. 2.6.35(2010년)에 들어간 파일이 지금도 같은 이름·의미로 있다. 이것이 "sysfs는 ABI"의 실체다.
- 바로 다음 항목이 `sampling_frequency_available`이다. "설정 가능한 값의 목록은 `_available` 접미사 파일로 따로 준다"는 관례다. 우리 드라이버도 `odr_available`을 만든다.
- `Documentation/ABI/` 아래는 `stable/`, `testing/`, `obsolete/`, `removed/`로 나뉜다. 새 드라이버 속성은 보통 `testing/`에 들어간다 (`testing`이라도 유저가 쓰기 시작하면 깨면 안 된다).

우리 sensorkit 드라이버를 업스트림에 낸다면 이런 파일을 같이 낸다 (형식 예시 — 실행 결과 아님).

```text
What:		/sys/bus/platform/devices/<dev>/odr
Date:		October 2026
KernelVersion:	6.19
Contact:	Don <don@example.com>
Description:
		(RW) Output data rate of the sensorkit IMU in Hz.
		Valid values are listed in odr_available. Writing any
		other value fails with -EINVAL.
```

> 실무 판단: 사실 진짜 IMU라면 sysfs 속성을 직접 만들지 않고 **IIO 서브시스템**(모듈 I)에 등록한다. 그러면 `sampling_frequency`, `in_accel_scale`, 버퍼·트리거가 표준 이름으로 공짜로 생긴다. "커스텀 sysfs 속성은 서브시스템이 표현 못 하는 것만" — 이게 리뷰어들이 기대하는 감각이다. 이 노트는 메커니즘을 보려고 platform driver에 직접 붙인다.

---

## 2. 속성 만들기 — `DEVICE_ATTR_RW/RO/WO`와 show/store

### 2.1 정의

디바이스 속성은 `struct device_attribute`다.

```c
/* include/linux/device.h (6.18) */
struct device_attribute {
	struct attribute	attr;		/* .name, .mode */
	ssize_t (*show)(struct device *dev, struct device_attribute *attr,
			char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);
};
```

- `show`: 유저가 `read()`하면 불린다. `buf`는 **커널이 준비한 PAGE_SIZE 버퍼**(페이지 정렬). 텍스트를 쓰고 **쓴 바이트 수**를 돌려준다.
- `store`: 유저가 `write()`하면 불린다. `buf`는 유저 데이터 복사본(NUL로 끝남 보장), `count`는 길이. 성공하면 **`count`를 돌려준다** (다 소비했다는 뜻). 실패하면 음수 errno.

매크로 세 개가 이름 규칙으로 함수를 연결해 준다.

| 매크로 | 만드는 변수 | 요구하는 함수 | 모드 |
|---|---|---|---|
| `DEVICE_ATTR_RW(odr)` | `dev_attr_odr` | `odr_show`, `odr_store` | 0644 |
| `DEVICE_ATTR_RO(temperature)` | `dev_attr_temperature` | `temperature_show` | 0444 |
| `DEVICE_ATTR_WO(self_test)` | `dev_attr_self_test` | `self_test_store` | 0200 |
| `DEVICE_ATTR_ADMIN_RW(x)` | `dev_attr_x` | `x_show`, `x_store` | 0600 |
| `DEVICE_ATTR(x, mode, show, store)` | `dev_attr_x` | 임의 이름 | 직접 지정 |

### 2.2 실습 대상 — sensorkit 드라이버와 DT

실습용 DT 노드 두 개를 랩의 `virt.dts`에 붙인다. `a`는 온도 센서가 있는 변형(variant), `b`는 없는 변형이다. 이 차이를 §6의 `is_visible`이 사용한다.

```dts

	g8-sensorkit-a {
		compatible = "g8,sensorkit";
		label = "imu-left";
		g8,has-temp-sensor;
		g8,temp-threshold-mC = <40000>;
	};

	g8-sensorkit-b {
		compatible = "g8,sensorkit";
		label = "imu-right";
	};
```

DTB 만들기와 부팅 스크립트 (D8과 같은 방식: 랩 rootfs 복사 → `.ko`와 유저 바이너리, 게스트 스크립트 `/g8.sh` 추가 → `-dtb`로 부팅 → `LAB_CMD`로 스크립트 실행 후 자동 poweroff).

```sh
#!/bin/sh
# mkdtb.sh: /var/tmp/G8/g8.dtb = lab virt.dts + two sensorkit nodes before the root's closing brace
set -e
mkdir -p /var/tmp/G8 && cd /var/tmp/G8
head -n -1 /var/lab/out/virt.dts > g8.dts
cat /Users/donh/.elinux_scratch/G8/g8_nodes.dtsi >> g8.dts
echo "};" >> g8.dts
dtc -q -I dts -O dtb -o g8.dtb g8.dts
ls -l g8.dtb
```

```sh
#!/bin/sh
# boot.sh guest_script.sh -> boots lab kernel with g8.dtb, runs /g8.sh in the guest, prints console
set -e
W=/var/tmp/G8
rm -rf $W/r && mkdir -p $W/r && cp -a /var/lab/build/rootfs/. $W/r/
cp $W/mod/*.ko $W/r/ 2>/dev/null || true
cp $W/bin/* $W/r/bin/ 2>/dev/null || true
cp "$1" $W/r/g8.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd $W/i.gz -dtb $W/g8.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/g8.sh" | tr -d '\r' | sed 's/\x1b\[[0-9;]*m//g'
```

(마지막 `sed`는 BusyBox `ls`의 색 코드를 지운다.)

### 2.3 드라이버 코드 (1/9) — 상태와 가짜 레지스터 뱅크

드라이버 `g8_sensorkit.c`는 길어서 아홉 조각으로 나눠 싣는다. 조각을 순서대로 이으면 실제로 빌드한 파일 그대로다. 실제 하드웨어가 없으니 `u32 regs[6]` 배열을 "MMIO 레지스터 뱅크"로 쓰고 `readl`/`writel`로 접근한다 (arm64에서 일반 메모리에 `readl`을 써도 동작한다. 실제 드라이버라면 `devm_platform_ioremap_resource()`로 얻은 주소다).

```c
// SPDX-License-Identifier: GPL-2.0
/* g8_sensorkit.c - fictional "sensorkit" IMU: sysfs + debugfs + misc demo (part 1: state) */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/sysfs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define REG_WHO_AM_I 0x00	/* fixed 0x47 */
#define REG_CTRL1    0x04	/* [3:0] odr code, [7] enable */
#define REG_CTRL2    0x08	/* [1:0] range code */
#define REG_STATUS   0x0c	/* [0] drdy, [1] temp alarm, [2] self-test pass */
#define REG_TEMP     0x10	/* temperature, milli-degC */
#define REG_ST_CNT   0x14	/* self-test count */
#define NREGS 6
#define G8_IOC_GET_TEMP _IOR('g', 1, int)

static const unsigned int odr_hz[] = { 12, 26, 52, 104, 208, 416, 833, 1666 };
static const char * const range_str[] = { "2g", "4g", "8g", "16g" };

struct g8_stats { u32 show, store, store_err, notify, self_test; };

struct g8_priv {
	struct device *dev;
	struct mutex lock;		/* protects regs + stats */
	u32 regs[NREGS];		/* fake register bank ("MMIO") */
	void __iomem *base;		/* = (void __iomem *)regs */
	u8 calib[32];			/* calibration blob (bin attr) */
	bool has_temp;			/* DT: g8,has-temp-sensor */
	u32 threshold_mc;		/* DT: g8,temp-threshold-mC */
	u32 temp_offset_mc;		/* debugfs knob: injected heat */
	bool alarm;
	struct g8_stats st;
	struct delayed_work poll_work;	/* "sensor sampling" every 50 ms */
	struct dentry *dbg;
	struct debugfs_regset32 regset;
	struct miscdevice misc;
};

static struct dentry *g8_dbg_root;

static u32 g8_rd(struct g8_priv *p, u32 off) { return readl(p->base + off); }
static void g8_wr(struct g8_priv *p, u32 off, u32 v) { writel(v, p->base + off); }
static void g8_rmw(struct g8_priv *p, u32 off, u32 mask, u32 v)
{
	g8_wr(p, off, (g8_rd(p, off) & ~mask) | (v & mask));
}
```

포인트:

- 모든 상태는 디바이스마다 하나인 `struct g8_priv`에 있다. 전역 변수는 debugfs 루트 디렉터리 하나뿐이다. 장치가 두 개(`a`, `b`)여도 섞이지 않는다.
- `lock`(mutex)이 레지스터와 통계를 보호한다. sysfs show/store는 **프로세스 컨텍스트**에서 불리고 여러 프로세스가 동시에 같은 파일을 읽고 쓸 수 있으므로, 공유 상태를 건드리면 락이 필요하다. 펌웨어로 치면 "디버그 셸 태스크와 센서 태스크가 같은 레지스터 미러를 만지는 상황"이다.

### 2.4 드라이버 코드 (2/9) — `odr`: 검증하는 RW 속성

```c
/* ---- part 2: text attributes ---- */
static ssize_t odr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct g8_priv *p = dev_get_drvdata(dev);
	u32 code;

	mutex_lock(&p->lock);
	code = g8_rd(p, REG_CTRL1) & 0xf;
	p->st.show++;
	mutex_unlock(&p->lock);
	return sysfs_emit(buf, "%u\n", odr_hz[code]);
}

static ssize_t odr_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct g8_priv *p = dev_get_drvdata(dev);
	unsigned int hz, i;
	int ret = kstrtouint(buf, 0, &hz);	/* accepts "104\n" */

	for (i = 0; !ret && i < ARRAY_SIZE(odr_hz); i++)
		if (odr_hz[i] == hz)
			break;
	mutex_lock(&p->lock);
	if (ret || i == ARRAY_SIZE(odr_hz)) {
		p->st.store_err++;
		mutex_unlock(&p->lock);
		return ret ? ret : -EINVAL;
	}
	g8_rmw(p, REG_CTRL1, 0x8f, 0x80 | i);
	p->st.store++;
	mutex_unlock(&p->lock);
	return count;		/* consumed everything */
}
static DEVICE_ATTR_RW(odr);

static ssize_t odr_available_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(odr_hz); i++)
		len += sysfs_emit_at(buf, len, "%u ", odr_hz[i]);
	buf[len - 1] = '\n';	/* replace trailing space */
	return len;
}
static DEVICE_ATTR_RO(odr_available);
```

`odr_store`의 흐름이 store 함수의 교과서 형태다.

1. 파싱: `kstrtouint(buf, 0, &hz)` — 끝의 `\n` 하나는 허용한다 (`echo`가 붙이니까). 숫자가 아니면 `-EINVAL`, 범위를 넘으면 `-ERANGE`를 돌려준다.
2. 검증: 하드웨어가 지원하는 값인지 표에서 찾는다. 없으면 `-EINVAL`. **가장 가까운 값으로 반올림하지 않는다** — 그건 유저가 모르는 사이 다른 값이 설정되는 것이라, 이 드라이버에서는 거부를 택했다 (IIO 드라이버 중에는 반올림하는 것도 있다. 어느 쪽이든 ABI 문서에 적는다).
3. 적용: 락 잡고 레지스터 read-modify-write.
4. `return count;` — "다 썼다". 0이나 `count`보다 작은 값을 돌려주면 유저 `write()`가 남은 바이트로 다시 store를 부르는 등 이상하게 동작한다.

### 2.5 드라이버 코드 (3/9) — `range`(enum), `temperature`(RO), `alarm`, `self_test`(WO)

```c
static ssize_t range_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct g8_priv *p = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", range_str[g8_rd(p, REG_CTRL2) & 3]);
}

static ssize_t range_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct g8_priv *p = dev_get_drvdata(dev);
	int i = sysfs_match_string(range_str, buf);	/* ignores trailing \n */

	if (i < 0)
		return i;	/* -EINVAL */
	mutex_lock(&p->lock);
	g8_rmw(p, REG_CTRL2, 3, i);
	mutex_unlock(&p->lock);
	return count;
}
static DEVICE_ATTR_RW(range);

static ssize_t temperature_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct g8_priv *p = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", (int)g8_rd(p, REG_TEMP));	/* milli-degC */
}
static DEVICE_ATTR_RO(temperature);

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct g8_priv *p = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", p->alarm);
}
static DEVICE_ATTR_RO(alarm);

static ssize_t self_test_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct g8_priv *p = dev_get_drvdata(dev);
	bool go;
	int ret = kstrtobool(buf, &go);

	if (ret)
		return ret;
	if (!go)
		return -EINVAL;		/* only "1" triggers */
	mutex_lock(&p->lock);
	g8_rmw(p, REG_STATUS, BIT(2), BIT(2));	/* pretend: pass */
	g8_wr(p, REG_ST_CNT, g8_rd(p, REG_ST_CNT) + 1);
	p->st.self_test++;
	mutex_unlock(&p->lock);
	return count;
}
static DEVICE_ATTR_WO(self_test);
```

- `range_store`의 `sysfs_match_string(range_str, buf)`는 문자열 배열에서 일치하는 인덱스를 찾는다. **끝의 `\n`을 무시**해 주므로 `echo 8g > range`가 바로 된다. 직접 `strcmp`하면 `"8g\n"`이 안 맞아서 초보자가 반드시 한 번 막히는 부분이다.
- `self_test`는 **쓰기 전용 트리거**다. 모드 0200 — root도 읽을 수 없다 (§5). "1"만 받고 "0"은 `-EINVAL`. `kstrtobool`은 `1/0/y/n/on/off`를 받는다.
- `temperature`는 milli-degC 정수다. sysfs 관례: **소수점 대신 정수 + 단위가 파일 이름이나 ABI 문서에 있다** (hwmon의 `temp1_input`도 milli-degC).

### 2.6 예제 2 — 빌드하고 sysfs 디렉터리 보기

무엇을 확인하나: 모듈을 랩 커널 트리로 빌드하고, 게스트에서 insmod한 뒤 장치 `a`와 `b`의 디렉터리를 비교한다. Kbuild 파일은 `obj-m := g8_sensorkit.o g8_race.o g8_cfs.o g8_emit.o` 한 줄이다 (나머지 모듈은 뒤 절에서 쓴다).

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/G8/mod && cp /Users/donh/.elinux_scratch/G8/m/* /var/tmp/G8/mod/ && cd /var/tmp/G8/mod && make -j2 -C /var/lab/out/linux-build M=$PWD modules'
```

```text
make: Entering directory '/var/lab/build/linux'
make[1]: Entering directory '/var/tmp/G8/mod'
  CC [M]  g8_sensorkit.o
  MODPOST Module.symvers
  CC [M]  g8_sensorkit.mod.o
  CC [M]  .module-common.o
  LD [M]  g8_sensorkit.ko
make[1]: Leaving directory '/var/tmp/G8/mod'
make: Leaving directory '/var/lab/build/linux'
```

(첫 빌드에서는 `#include <linux/mod_devicetable.h>`를 빠뜨려 `array type has incomplete element type 'struct of_device_id'` 에러가 났다. `of_device_id`는 그 헤더에 있다.)

게스트 스크립트:

```sh
insmod /g8_sensorkit.ko
cd /sys/bus/platform/devices/g8-sensorkit-a && ls -l
echo ---b; ls /sys/bus/platform/devices/g8-sensorkit-b/
echo ---; ls /sys/kernel/debug/g8_sensorkit/ /sys/kernel/debug/g8_sensorkit/g8-sensorkit-a; ls -l /dev/g8*
```

```text
lab initramfs: 6.18.55 up 1.01s
total 0
-r--r--r--    1 0        0             4096 Oct 11 01:34 alarm
-rw-------    1 0        0               32 Oct 11 01:34 calibration
lrwxrwxrwx    1 0        0                0 Oct 11 01:34 driver -> ../../../bus/platform/drivers/g8_sensorkit
-rw-r--r--    1 0        0             4096 Oct 11 01:34 driver_override
-r--r--r--    1 0        0             4096 Oct 11 01:34 modalias
-rw-r--r--    1 0        0             4096 Oct 11 01:34 odr
-r--r--r--    1 0        0             4096 Oct 11 01:34 odr_available
lrwxrwxrwx    1 0        0                0 Oct 11 01:34 of_node -> ../../../firmware/devicetree/base/g8-sensorkit-a
drwxr-xr-x    2 0        0                0 Oct 11 01:34 power
-rw-r--r--    1 0        0             4096 Oct 11 01:34 range
--w-------    1 0        0             4096 Oct 11 01:34 self_test
lrwxrwxrwx    1 0        0                0 Oct 11 01:34 subsystem -> ../../../bus/platform
-r--r--r--    1 0        0             4096 Oct 11 01:34 temperature
-rw-r--r--    1 0        0             4096 Oct 11 01:34 uevent
---b
calibration      modalias         of_node          self_test
driver           odr              power            subsystem
driver_override  odr_available    range            uevent
---
/sys/kernel/debug/g8_sensorkit/:
g8-sensorkit-a  g8-sensorkit-b

/sys/kernel/debug/g8_sensorkit/g8-sensorkit-a:
regs            stats           temp_offset_mC  threshold_mC
crw-------    1 0        0          10, 258 Oct 11 01:34 /dev/g8-sensorkit-a
crw-------    1 0        0          10, 259 Oct 11 01:34 /dev/g8-sensorkit-b
```

출력에서 볼 것:

- 모드가 매크로대로다: `odr`/`range` 0644, `temperature`/`alarm`/`odr_available` 0444, `self_test` 0200, `calibration` 0600.
- 텍스트 속성의 크기는 항상 **4096**(PAGE_SIZE)으로 보인다. 실제 내용 길이가 아니다 — sysfs는 읽기 전에는 길이를 모른다. 그래서 `stat`의 크기로 판단하는 유저 코드는 틀린다. 반면 binary attribute `calibration`은 우리가 선언한 **32**가 그대로 크기다.
- `driver_override`, `modalias`, `uevent`, `driver`, `of_node`, `power`는 driver core·platform 버스가 만든 것이다. 우리가 만든 건 나머지 7개.
- 장치 `b`에는 `temperature`와 `alarm`이 **아예 없다**. DT에 `g8,has-temp-sensor`가 없어서 `is_visible`이 0을 돌려줬기 때문이다 (§6).

---

## 3. `sysfs_emit` / `sysfs_emit_at` — 왜 `sprintf`가 아닌가

### 3.1 문제

옛 드라이버는 show에서 `return sprintf(buf, "%d\n", val);`을 썼다. `buf`가 PAGE_SIZE라는 건 **암묵적 약속**일 뿐 `sprintf`는 크기를 모른다. 리스트를 출력하다 길어지면 페이지 밖을 덮어쓴다(커널 메모리 오염). `snprintf(buf, PAGE_SIZE, ...)`로 바꿔도 함정이 있다: `snprintf`는 **"잘리지 않았다면 썼을" 길이**를 돌려줘서, `len += snprintf(buf + len, PAGE_SIZE - len, ...)`를 반복하면 `len`이 PAGE_SIZE를 넘고 다음 호출의 크기 인자가 음수(→ 거대한 size_t)가 된다.

### 3.2 해결 — Linux 5.10부터

```c
int sysfs_emit(char *buf, const char *fmt, ...);          /* buf는 show가 받은 그 버퍼 */
int sysfs_emit_at(char *buf, int at, const char *fmt, ...); /* buf + at 위치에 이어 쓰기 */
```

랩 소스 `fs/sysfs/file.c`의 구현은 짧다.

```c
int sysfs_emit(char *buf, const char *fmt, ...)
{
	va_list args;
	int len;

	if (WARN(!buf || offset_in_page(buf),
		 "invalid sysfs_emit: buf:%p\n", buf))
		return 0;

	va_start(args, fmt);
	len = vscnprintf(buf, PAGE_SIZE, fmt, args);
	va_end(args);

	return len;
}
```

- `vscnprintf`는 **실제로 쓴 길이**를 돌려준다 (`snprintf`와 다름). 그래서 `len += sysfs_emit_at(...)`를 반복해도 PAGE_SIZE - 1을 넘지 않는다.
- `buf`가 **페이지 시작이 아니면 WARN**하고 0을 돌려준다. "sysfs show 버퍼에만 써라"를 런타임에 강제하는 장치다. `checkpatch.pl`도 show 함수 안의 `sprintf`/`snprintf`에 경고를 낸다.

### 3.3 예제 3 — 잘못 쓴 `sysfs_emit`과 PAGE_SIZE 잘림

무엇을 확인하나: 이어 쓰기에 `sysfs_emit(buf + len, ...)`을 쓰는 흔한 실수, 올바른 `sysfs_emit_at`, 그리고 5000바이트를 출력하려는 show를 비교한다. 장치는 class 디바이스(`/sys/class/g8emit/g8emit0`)로 만든다.

```c
// SPDX-License-Identifier: GPL-2.0
/* g8_emit.c: sysfs_emit() guards - misuse at buf+len, and PAGE_SIZE truncation */
#include <linux/module.h>
#include <linux/device.h>

static ssize_t bad_show(struct device *dev, struct device_attribute *a, char *buf)
{
	int len = sysfs_emit(buf, "2g ");
	len += sysfs_emit(buf + len, "4g\n");	/* BUG: must be sysfs_emit_at(buf, len, ..) */
	return len;
}
static DEVICE_ATTR_RO(bad);

static ssize_t good_show(struct device *dev, struct device_attribute *a, char *buf)
{
	int len = sysfs_emit(buf, "2g ");
	len += sysfs_emit_at(buf, len, "4g\n");
	return len;
}
static DEVICE_ATTR_RO(good);

static ssize_t big_show(struct device *dev, struct device_attribute *a, char *buf)
{
	int i, len = 0;

	for (i = 0; i < 1000; i++)		/* wants 5000 bytes, page is 4096 */
		len += sysfs_emit_at(buf, len, "%04d ", i);
	return len;
}
static DEVICE_ATTR_RO(big);

static struct attribute *g8e_attrs[] = {
	&dev_attr_bad.attr, &dev_attr_good.attr, &dev_attr_big.attr, NULL
};
ATTRIBUTE_GROUPS(g8e);
/* ... module init: class_create("g8emit") + device_create_with_groups(cls, NULL, 0, NULL,
 *     g8e_groups, "g8emit0") - same shape as g8_race.c in section 8 (full file: 60 lines) */
```

```sh
insmod /g8_emit.ko
cd /sys/class/g8emit/g8emit0
echo "== good"; cat good
echo "== bad"; cat bad; echo "(bytes: $(cat bad | wc -c))"
dmesg | grep -A6 "invalid sysfs_emit" | head -8
echo "== big"; cat big | wc -c; cat big | tail -c 20; echo
```

```text
== good
2g 4g
== bad
2g (bytes: 3)
[    1.414341] invalid sysfs_emit: buf:00000000c1ec95c1
[    1.414780] WARNING: CPU: 0 PID: 65 at fs/sysfs/file.c:759 sysfs_emit+0x58/0xd0
[    1.415941] Modules linked in: g8_emit(O)
[    1.416750] CPU: 0 UID: 0 PID: 65 Comm: cat Tainted: G           O        6.18.55 #1 PREEMPT 
[    1.416847] Tainted: [O]=OOT_MODULE
[    1.416867] Hardware name: linux,dummy-virt (DT)
[    1.416990] pstate: 60000005 (nZCv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
--
== big
4095
0815 0816 0817 0818 
```

출력에서 볼 것:

- `bad`는 `"2g "` 3바이트만 나왔다. 두 번째 `sysfs_emit(buf + 3, ...)`은 페이지 정렬이 아니라서 **WARN + 0 반환**. 커널 로그에 스택 트레이스가 남는다 (위는 앞부분만). 버그를 조용히 넘기지 않고 시끄럽게 알려 준다.
- `big`은 5000바이트를 원했지만 **4095바이트**(PAGE_SIZE - 1, 마지막은 NUL 자리)에서 잘렸다. 메모리 오염은 없다. 하지만 유저는 잘린 걸 모른다 — "PAGE_SIZE를 넘을 수 있는 데이터는 sysfs에 두지 말라"는 규칙의 이유다. 큰 데이터는 binary attribute, debugfs seq_file, 또는 문자 디바이스로 간다.
- (참고: 포인터 `%p`는 해시돼서 `00000000c1ec95c1`처럼 가짜 값이 찍힌다. 커널 주소 노출 방지.)

---

## 4. 에러 처리 — store가 돌려준 errno가 셸에서 보이는 모습

### 4.1 예제 4 — 정상 쓰기와 잘못된 쓰기

무엇을 확인하나: 정상 값과 잘못된 값을 써서 `echo`의 종료 코드와 에러 메시지를 본다. 마지막에 debugfs 통계로 store 호출 횟수를 확인한다.

```sh
insmod /g8_sensorkit.ko
cd /sys/bus/platform/devices/g8-sensorkit-a
echo "== reads"; for f in odr odr_available range temperature alarm; do echo "$f: $(cat $f)"; done
echo "== valid writes"; echo 416 > odr; echo "rc=$?"; echo 8g > range; echo "rc=$?"; cat odr range
echo "== invalid writes"
echo 100 > odr; echo "rc=$?"
echo abc > odr; echo "rc=$?"
echo 99999999999 > odr; echo "rc=$?"
echo 3g > range; echo "rc=$?"
echo 0 > self_test; echo "rc=$?"
echo 1 > self_test; echo "rc=$?"
echo 1 > temperature; echo "rc=$?"
cat self_test; echo "rc=$?"
```

```text
lab initramfs: 6.18.55 up 0.74s
== reads
odr: 104
odr_available: 12 26 52 104 208 416 833 1666
range: 2g
temperature: 25000
alarm: 0
== valid writes
rc=0
rc=0
416
8g
== invalid writes
sh: write error: Invalid argument
rc=1
sh: write error: Invalid argument
rc=1
sh: write error: Numerical result out of range
rc=1
sh: write error: Invalid argument
rc=1
sh: write error: Invalid argument
rc=1
rc=0
/g8.sh: line 12: can't create temperature: Permission denied
rc=1
cat: can't open 'self_test': Permission denied
rc=1
```

출력에서 볼 것:

- store가 돌려준 `-EINVAL`은 유저의 `write()`가 -1 + `errno = EINVAL`로 받고, 셸은 `write error: Invalid argument`를 찍고 종료 코드 1을 낸다. **유저 스크립트는 `$?`로 실패를 알 수 있다.** store에서 에러를 삼키고 `count`를 돌려주면 이 정보가 사라진다 — 가장 흔한 리뷰 지적 중 하나다.
- `99999999999`는 `kstrtouint`가 `-ERANGE`를 돌려줘서 메시지가 다르다 (`Numerical result out of range`). 드라이버는 파싱 함수의 에러를 그대로 전달했다.
- `temperature`(0444)에 쓰기, `self_test`(0200) 읽기는 store/show까지 가지도 않고 **open 단계에서 `EACCES`**다. 여기서는 root인데도 거부됐다 — 일반 파일이라면 root는 `CAP_DAC_OVERRIDE`로 통과하지만, kernfs는 open 시점에 "쓰기 모드로 열었는데 모드에 쓰기 비트(`S_IWUGO`)가 없거나 store가 없으면 `-EACCES`"를 따로 검사한다 (`fs/kernfs/file.c`의 `kernfs_fop_open()`).
- `echo 0 > self_test`(유효한 bool이지만 의미 없는 값)도 `-EINVAL`로 거부했다.

### 4.2 errno 고르기

| 상황 | errno | 셸 메시지 |
|---|---|---|
| 파싱 실패, 지원하지 않는 값 | `-EINVAL` | Invalid argument |
| 숫자가 타입 범위 밖 | `-ERANGE` (kstrto가 줌) | Numerical result out of range |
| 장치가 지금 바쁨 (측정 중) | `-EBUSY` | Resource busy |
| 장치가 꺼져 있음 (runtime PM 실패) | 하위 함수의 에러 그대로 | 상황별 |
| 버스 통신 실패 | `-EIO` 또는 하위 에러 | I/O error |
| 권한 (드라이버가 추가 검사) | `-EPERM` | Operation not permitted |

---

## 5. 권한과 소유권

### 5.1 예제 5 — 일반 유저로 접근하기

무엇을 확인하나: uid 1000으로 sysfs·debugfs에 접근해 보고, root가 `chown`으로 특정 속성만 넘겨주면 어떻게 되는지 본다. BusyBox rootfs에는 `/etc/passwd`가 없어서 uid를 직접 바꾸는 작은 헬퍼를 쓴다.

```c
/* g8_asuser.c: drop to uid/gid 1000 and exec a command (busybox has no passwd here) */
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s cmd [args]\n", argv[0]); exit(1); }
	if (setgroups(0, NULL) || setgid(1000) || setuid(1000)) { perror("drop"); exit(1); }
	execvp(argv[1], argv + 1);
	perror("execvp");
	return 127;
}
```

```sh
insmod /g8_sensorkit.ko
cd /sys/bus/platform/devices/g8-sensorkit-a
U=g8_asuser
echo "== as uid 1000"; $U id 2>&1
$U cat odr; $U sh -c 'echo 52 > odr'; echo "rc=$?"
$U cat calibration; echo "rc=$?"
$U ls /sys/kernel/debug/; echo "rc=$?"
ls -ld /sys/kernel/debug
echo "== root hands odr to uid 1000 (what a udev rule would do)"
chown 1000:1000 odr; ls -l odr
$U sh -c 'echo 52 > odr'; echo "rc=$?"; cat odr
```

```text
lab initramfs: 6.18.55 up 1.43s
== as uid 1000
uid=1000 gid=1000
104
sh: can't create odr: Permission denied
rc=1
cat: can't open 'calibration': Permission denied
rc=1
ls: can't open '/sys/kernel/debug/': Permission denied
rc=1
drwx------   41 0        0                0 Jan  1  1970 /sys/kernel/debug
== root hands odr to uid 1000 (what a udev rule would do)
-rw-r--r--    1 1000     1000          4096 Oct 11 01:38 odr
rc=0
52
```

출력에서 볼 것:

- 일반 유저는 0644 `odr`를 읽을 수 있지만 쓸 수 없고, 0600 `calibration`은 읽지도 못한다. 보정 데이터처럼 민감한 것은 `_ADMIN_` 변형(0600/0400)으로 만든다.
- debugfs 마운트 지점 자체가 **0700**이다 (debugfs 기본 마운트 모드). 일반 유저는 디렉터리에 들어갈 수조차 없다.
- sysfs 속성은 `chown`/`chmod`가 된다 (kernfs가 inode 속성을 기억한다). 실제 제품에서는 udev 규칙이 이 일을 한다: 예를 들어 `ACTION=="bind", SUBSYSTEM=="platform", DRIVER=="g8_sensorkit", RUN+="/bin/chgrp sensors /sys%p/odr"` 같은 식 (규칙 문법은 G9, 이 규칙 자체는 실행해 보지 않았다). 그래서 §8의 "uevent 시점에 속성이 존재해야 한다"가 중요해진다.

### 5.2 예제 6 — world-writable 속성은 컴파일이 안 된다

무엇을 확인하나: 모드 0666으로 속성을 선언하면 무슨 일이 생기는지. 별도 디렉터리 `/var/tmp/G8/perm`에서 빌드했다.

```c
// SPDX-License-Identifier: GPL-2.0
/* g8_perm.c: world-writable sysfs attribute -> rejected at compile time */
#include <linux/module.h>
#include <linux/device.h>

static ssize_t odr_show(struct device *d, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "104\n");
}
static ssize_t odr_store(struct device *d, struct device_attribute *a,
			 const char *buf, size_t n)
{
	return n;
}
static DEVICE_ATTR(odr, 0666, odr_show, odr_store);	/* anyone may write?! */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G8 permission check demo");
```

```text
  CC [M]  g8_perm.o
In file included from /var/lab/src/linux-6.18.55/include/linux/build_bug.h:5,
                 from /var/lab/src/linux-6.18.55/include/linux/container_of.h:5,
                 from /var/lab/src/linux-6.18.55/include/linux/list.h:5,
                 from /var/lab/src/linux-6.18.55/include/linux/module.h:12,
                 from g8_perm.c:3:
/var/lab/src/linux-6.18.55/include/linux/compiler.h:197:67: error: static assertion failed: "(0666) & 2 is true"
  197 | #define __BUILD_BUG_ON_ZERO_MSG(e, msg, ...) ((int)sizeof(struct {_Static_assert(!(e), msg);}))
      |                                                                   ^~~~~~~~~~~~~~
...
/var/lab/src/linux-6.18.55/include/linux/kernel.h:210:10: note: in expansion of macro ‘BUILD_BUG_ON_ZERO’
  210 |          BUILD_BUG_ON_ZERO((perms) & 2) +                                       \
      |          ^~~~~~~~~~~~~~~~~
/var/lab/src/linux-6.18.55/include/linux/sysfs.h:229:26: note: in expansion of macro ‘VERIFY_OCTAL_PERMISSIONS’
  229 |                  .mode = VERIFY_OCTAL_PERMISSIONS(_mode) },             \
```

출력에서 볼 것: `__ATTR` 매크로가 `VERIFY_OCTAL_PERMISSIONS`로 모드를 **컴파일 시간에** 검사한다. "others 쓰기 비트(`& 2`)가 켜져 있으면 빌드 실패". 같은 매크로가 "user 읽기 ≥ group 읽기 ≥ other 읽기" 순서도 검사한다. 누구나 쓸 수 있는 장치 설정은 보안 구멍이므로 커널이 아예 못 만들게 한다. 꼭 필요하면 udev가 런타임에 그룹 권한을 준다 (예제 5).

---

## 6. attribute group, `is_visible`, binary attribute

### 6.1 직관

속성을 하나씩 `device_create_file()`로 만들면 에러 처리·제거가 속성 수만큼 늘어난다. 그래서 커널은 **속성 배열 + 옵션을 묶은 `struct attribute_group`**을 한 번에 등록·제거하게 한다. 펌웨어로 치면 "레지스터 하나씩 초기화하지 말고 `{addr, value}` 테이블을 루프로 적용"하는 것과 같다.

```c
/* include/linux/sysfs.h (6.18) */
struct attribute_group {
	const char		*name;		/* 있으면 하위 디렉터리 */
	umode_t			(*is_visible)(struct kobject *, struct attribute *, int);
	umode_t			(*is_bin_visible)(struct kobject *, const struct bin_attribute *, int);
	size_t			(*bin_size)(struct kobject *, const struct bin_attribute *, int);
	struct attribute	**attrs;
	const struct bin_attribute	*const *bin_attrs;
};
```

- `is_visible`: 그룹을 만들 때 **속성마다 한 번** 불린다. 0을 돌려주면 그 파일은 만들어지지 않고, 모드를 돌려주면 그 모드로 만든다 (모드를 바꿔 RO로 만드는 것도 가능). "이 변형 칩에는 온도 센서가 없다", "이 펌웨어 버전은 이 기능이 없다"를 표현하는 표준 방법이다.
- 6.18에서 `bin_attrs`는 `const struct bin_attribute *const *` 타입이다. bin attribute 콜백의 세 번째 인자도 `const struct bin_attribute *`다. 이 const화는 6.13~6.16 무렵 진행된 변경이라 **옛 커널 예제의 시그니처(비 const)와 다르다** — 블로그 코드를 복사하면 경고가 난다.

### 6.2 드라이버 코드 (4/9) — binary attribute + group + `is_visible`

```c
/* ---- part 3: binary attribute + group with is_visible ---- */
static ssize_t calibration_read(struct file *f, struct kobject *kobj,
				const struct bin_attribute *attr, char *buf,
				loff_t off, size_t count)
{
	struct g8_priv *p = dev_get_drvdata(kobj_to_dev(kobj));

	/* sysfs already clipped off/count to attr->size (32) */
	mutex_lock(&p->lock);
	memcpy(buf, p->calib + off, count);
	mutex_unlock(&p->lock);
	return count;
}

static ssize_t calibration_write(struct file *f, struct kobject *kobj,
				 const struct bin_attribute *attr, char *buf,
				 loff_t off, size_t count)
{
	struct g8_priv *p = dev_get_drvdata(kobj_to_dev(kobj));

	mutex_lock(&p->lock);
	memcpy(p->calib + off, buf, count);
	mutex_unlock(&p->lock);
	return count;
}
static const BIN_ATTR_ADMIN_RW(calibration, 32);	/* mode 0600 */

static struct attribute *g8_attrs[] = {
	&dev_attr_odr.attr,
	&dev_attr_odr_available.attr,
	&dev_attr_range.attr,
	&dev_attr_temperature.attr,
	&dev_attr_alarm.attr,
	&dev_attr_self_test.attr,
	NULL,
};

static const struct bin_attribute *const g8_bin_attrs[] = {
	&bin_attr_calibration,
	NULL,
};

static umode_t g8_is_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	bool has_temp = device_property_read_bool(dev, "g8,has-temp-sensor");

	if ((a == &dev_attr_temperature.attr || a == &dev_attr_alarm.attr) && !has_temp)
		return 0;		/* file is not created at all */
	return a->mode;			/* keep the declared mode */
}

static const struct attribute_group g8_group = {
	.attrs = g8_attrs,
	.bin_attrs = g8_bin_attrs,
	.is_visible = g8_is_visible,
};
__ATTRIBUTE_GROUPS(g8);		/* -> g8_groups[] = { &g8_group, NULL } */
```

- `BIN_ATTR_ADMIN_RW(calibration, 32)`: `calibration_read`/`calibration_write`를 연결하고 **크기 32**, 모드 0600. 크기를 주면 sysfs가 `off`/`count`를 미리 잘라서 넘겨준다 — read/write 콜백에서 범위 검사를 다시 할 필요가 없다.
- binary attribute는 sysfs의 "값 하나 = 텍스트" 규칙의 **공식 예외**다. 펌웨어 blob, EEPROM 내용(`at24` 드라이버의 `eeprom` 파일), PCI config space(`/sys/bus/pci/devices/.../config`), 보정 테이블처럼 "커널이 해석하지 않고 그대로 전달하는 바이트"에만 쓴다.
- `is_visible`은 DT 속성을 직접 읽었다. `probe()`에서 채운 `p->has_temp`를 쓰지 않은 이유는 §8에서 나온다: `.dev_groups`는 probe가 끝난 뒤 만들어지므로 drvdata를 써도 되지만, 같은 그룹을 `dev->groups`(device_add 시점)로 옮기면 그때는 drvdata가 아직 없다. 펌웨어 속성(`device_property_*`, D8)은 언제나 읽을 수 있으므로 안전하다.
- `__ATTRIBUTE_GROUPS(g8)`는 `g8_groups[] = { &g8_group, NULL }`을 만든다. 속성만 있다면 `ATTRIBUTE_GROUPS(g8)` 한 줄이 그룹 정의까지 해 주지만, bin_attrs와 is_visible을 넣으려고 그룹은 직접 정의했다.

### 6.3 예제 7 — binary attribute 읽고 쓰기, 크기 넘기기

무엇을 확인하나: 32바이트 보정 blob을 읽고, 일부를 쓰고, 40바이트를 써서 크기 제한이 어떻게 걸리는지 본다.

```sh
cd /sys/bus/platform/devices/g8-sensorkit-a
hexdump -C calibration
printf 'SKCAL\001\002\003' > calibration; echo "rc=$?"
hexdump -C calibration
dd if=/dev/zero bs=40 count=1 2>/dev/null | tr '\0' 'Z' > /tmp/z40; ls -l /tmp/z40
dd if=/tmp/z40 of=calibration bs=40 count=1; echo "rc=$?"
dd if=/tmp/z40 of=calibration bs=8 seek=4 count=1; echo "rc=$?"
wc -c calibration
```

```text
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000020
rc=0
00000000  53 4b 43 41 4c 01 02 03  00 00 00 00 00 00 00 00  |SKCAL...........|
00000010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00000020
-rw-r--r--    1 0        0               40 Oct 11 01:35 /tmp/z40
dd: error writing 'calibration': File too large
1+0 records in
0+0 records out
32 bytes (32B) copied, 0.000999 seconds, 31.3KB/s
rc=1
dd: error writing 'calibration': File too large
1+0 records in
0+0 records out
0 bytes (0B) copied, 0.001210 seconds, 0B/s
rc=1
32 calibration
```

출력에서 볼 것:

- 바이너리(`\001\002\003`)가 그대로 저장·반환된다. 텍스트 변환 없음.
- 40바이트 쓰기: sysfs가 `count`를 32로 잘라 store에 넘겼고(32 bytes copied), 나머지 8바이트를 쓰려는 두 번째 `write()`에서 오프셋 32 = 크기 → **`-EFBIG`(File too large)**. 오프셋 32부터 쓰기(`seek=4`, bs=8)는 처음부터 `-EFBIG`. 커널 `sysfs_kf_bin_write()`의 `if (size <= pos) return -EFBIG;` 그대로다.
- 그래서 binary attribute에서는 드라이버가 범위 검사를 안 해도 버퍼 오버플로가 없다. 단, 크기 0으로 선언하면(가변 크기) 이 보호가 꺼지므로 직접 검사해야 한다.

---

## 7. 변화 알리기 — `sysfs_notify` + `poll(POLLPRI)`

### 7.1 직관

"온도가 임계값을 넘으면 앱이 알게 해 주세요." 앱이 `alarm` 파일을 100 ms마다 읽는 폴링은 CPU를 낭비하고 반응이 느리다. 펌웨어라면 "상태 레지스터를 폴링하지 말고 인터럽트를 받아라"다. sysfs에도 그런 장치가 있다: 드라이버가 `sysfs_notify()`를 부르면, 그 파일을 `poll()`로 기다리던 프로세스가 깨어난다.

규칙(랩 소스 `fs/kernfs/file.c`의 주석 요지):

1. 유저는 먼저 파일을 **한 번 읽는다** (그때의 이벤트 번호가 기록된다).
2. `poll()`의 events에 `POLLPRI`(또는 `POLLERR`)을 넣고 기다린다. sysfs 파일은 늘 "읽을 수 있음"이라 `POLLIN`은 의미가 없다.
3. 깨어나면 `revents`에 `POLLERR|POLLPRI`가 켜져 있다. **lseek(0) 후 다시 읽거나**(`pread(fd, .., 0)`), 닫고 다시 연다.
4. 이 메커니즘은 드라이버가 `sysfs_notify`를 부르는 속성에서만 동작한다. 지원하지 않는 속성을 poll하면 영원히 안 깨어난다 (에러도 없다) — 타임아웃을 꼭 건다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="40" y="10" width="140" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="30" text-anchor="middle" font-size="13">g8_poll (유저)</text>
  <rect x="270" y="10" width="140" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="30" text-anchor="middle" font-size="13">kernfs (sysfs)</text>
  <rect x="500" y="10" width="140" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="570" y="30" text-anchor="middle" font-size="13">드라이버 work</text>
  <line x1="110" y1="40" x2="110" y2="280" stroke="currentColor" stroke-dasharray="3 3"/>
  <line x1="340" y1="40" x2="340" y2="280" stroke="currentColor" stroke-dasharray="3 3"/>
  <line x1="570" y1="40" x2="570" y2="280" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="225" y="66" text-anchor="middle" font-size="12">① pread(): 현재 event 기억</text>
  <line x1="110" y1="74" x2="338" y2="74" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="225" y="100" text-anchor="middle" font-size="12">② poll(POLLPRI): 잠듦</text>
  <line x1="110" y1="108" x2="338" y2="108" stroke="#4a7bd0" marker-end="url(#ah2)"/>
  <rect x="490" y="124" width="160" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570" y="143" text-anchor="middle" font-size="12">③ temp ≥ threshold</text>
  <text x="455" y="176" text-anchor="middle" font-size="12">④ sysfs_notify(.., "alarm")</text>
  <line x1="570" y1="184" x2="342" y2="184" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="225" y="210" text-anchor="middle" font-size="12">⑤ event++ → wake_up</text>
  <line x1="340" y1="218" x2="112" y2="218" stroke="#d0564a" marker-end="url(#ah2)"/>
  <text x="225" y="236" text-anchor="middle" font-size="12">revents = POLLERR|POLLPRI</text>
  <text x="225" y="262" text-anchor="middle" font-size="12">⑥ pread(fd, 0): 새 값 1</text>
  <line x1="110" y1="270" x2="338" y2="270" stroke="currentColor" marker-end="url(#ah2)"/>
</svg>
```
그림 2 — sysfs 속성 변화 통지 순서. kernfs는 열린 파일마다 "마지막으로 본 event 번호"를 기억하고, `kernfs_notify()`가 노드의 event 번호를 올리고 대기 큐를 깨운다. 번호가 다르면 poll이 `POLLERR|POLLPRI`를 돌려준다.

### 7.2 드라이버 코드 (5/9) — 주기적 샘플링과 임계값 통지

진짜 센서라면 data-ready 인터럽트의 threaded handler에서 할 일을, 여기서는 50 ms 주기 delayed work로 흉내 낸다. 온도 = 25.000 °C + debugfs knob으로 주입한 값(§9).

```c
/* ---- part 4: periodic "sampling" + sysfs_notify on threshold crossing ---- */
static void g8_poll_work(struct work_struct *w)
{
	struct g8_priv *p = container_of(to_delayed_work(w), struct g8_priv, poll_work);
	u32 temp = 25000 + READ_ONCE(p->temp_offset_mc);	/* fake 25.000 C + heat */
	bool alarm = temp >= p->threshold_mc;
	bool changed;

	mutex_lock(&p->lock);
	g8_wr(p, REG_TEMP, temp);
	g8_rmw(p, REG_STATUS, BIT(1) | BIT(0), (alarm ? BIT(1) : 0) | BIT(0));
	changed = alarm != p->alarm;
	p->alarm = alarm;
	if (changed)
		p->st.notify++;
	mutex_unlock(&p->lock);

	if (changed) {
		/* process context: sysfs_notify may sleep (kernfs lookup) */
		sysfs_notify(&p->dev->kobj, NULL, "alarm");
		dev_info(p->dev, "alarm %d (temp %u mC, threshold %u)\n",
			 alarm, temp, p->threshold_mc);
	}
	schedule_delayed_work(&p->poll_work, msecs_to_jiffies(50));
}
```

- 통지는 **상태가 바뀔 때만**(0→1, 1→0) 보낸다. 매 샘플마다 보내면 유저가 쓸데없이 깨어난다.
- `sysfs_notify()`는 내부에서 `kernfs_find_and_get()`으로 이름 검색을 하고 이는 잠들 수 있는 락을 쓴다. 그래서 **hard IRQ 핸들러에서 부르면 안 된다**. IRQ에서 알려야 하면 probe 때 `sysfs_get_dirent()`로 kernfs 노드를 미리 잡아 두고 `sysfs_notify_dirent()`(= `kernfs_notify`, IRQ 안전)를 쓰거나, threaded IRQ·workqueue로 넘긴다. (이 노트는 workqueue를 썼다.)
- 락 밖에서 notify한다. notify가 깨운 유저가 바로 `alarm_show`를 부를 수 있으므로, 락을 잡은 채 부를 이유가 없다.

### 7.3 예제 8 — poller 프로그램

무엇을 확인하나: `alarm` 파일을 `poll(POLLPRI)`로 기다리는 정적 링크 프로그램. 처음 값을 읽고, 두 번의 변화를 기다린다.

```c
/* g8_poll.c: wait for sysfs_notify() on an attribute with poll(POLLPRI) */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	char buf[16];
	int n, events = argc > 2 ? atoi(argv[2]) : 2;
	int fd = open(argv[1], O_RDONLY);

	if (argc < 2 || fd < 0) { perror("open"); exit(1); }
	/* 1) must read once first: arms the "last seen event" counter */
	n = pread(fd, buf, sizeof(buf) - 1, 0);
	if (n < 0) { perror("pread"); exit(1); }
	buf[n] = 0;
	printf("[%.3f] initial value: %s", now(), buf);
	fflush(stdout);
	while (events-- > 0) {
		struct pollfd pfd = { .fd = fd, .events = POLLPRI | POLLERR };
		int r = poll(&pfd, 1, 10000);		/* 10 s timeout */

		if (r < 0) { perror("poll"); exit(1); }
		if (r == 0) { printf("timeout\n"); exit(2); }
		/* 2) re-read from offset 0 (seek+read) to get the new value */
		n = pread(fd, buf, sizeof(buf) - 1, 0);
		if (n < 0) { perror("pread"); exit(1); }
		buf[n] = 0;
		printf("[%.3f] revents=0x%x -> new value: %s", now(), pfd.revents, buf);
		fflush(stdout);
	}
	close(fd);
	return 0;
}
```

```sh
insmod /g8_sensorkit.ko
cd /sys/bus/platform/devices/g8-sensorkit-a
D=/sys/kernel/debug/g8_sensorkit/g8-sensorkit-a
g8_poll alarm 2 &
sleep 1
echo "[shell] heat +20 C"; echo 20000 > $D/temp_offset_mC
sleep 1
echo "[shell] cool down"; echo 0 > $D/temp_offset_mC
wait
dmesg | grep g8
cat $D/stats | grep notif
```

```text
lab initramfs: 6.18.55 up 0.80s
[0.901] initial value: 0
[shell] heat +20 C
[1.906] revents=0xa -> new value: 1
[shell] cool down
[2.949] revents=0xa -> new value: 0
[    0.000000] Kernel command line: console=ttyAMA0 quiet LAB_CMD=sh~/g8.sh
[    0.000000] Unknown kernel command line parameters "LAB_CMD=sh~/g8.sh", will be passed to user space.
[    0.786906]     LAB_CMD=sh~/g8.sh
[    0.918213] g8_sensorkit: loading out-of-tree module taints kernel.
[    0.923006] g8_sensorkit g8-sensorkit-a: probed (has_temp=1)
[    0.923645] g8_sensorkit g8-sensorkit-b: probed (has_temp=0)
[    1.957447] g8_sensorkit g8-sensorkit-a: alarm 1 (temp 45000 mC, threshold 40000)
[    3.001103] g8_sensorkit g8-sensorkit-a: alarm 0 (temp 25000 mC, threshold 40000)
notifies:     2
```

출력에서 볼 것:

- `revents=0xa` = `POLLERR`(0x8) | `POLLPRI`(0x2). kernfs가 돌려주는 값 그대로다.
- poller는 CPU를 쓰지 않고 잠들어 있다가 셸이 온도를 올린 지 약 1 work 주기(최대 50 ms) 안에 깨어났다. (poller의 시각은 `CLOCK_MONOTONIC`, dmesg는 printk 시계라 두 열을 ms 단위로 직접 비교하지는 말자.)
- 장치 `b`는 `has_temp=0`이라 work를 아예 시작하지 않았다. 속성도 숨겨져 있으니 유저 쪽에서 실수로 기다릴 일도 없다.
- 실제로는 이런 이벤트를 IIO events(모듈 I)나 input 이벤트로 내보내는 경우가 많다. sysfs_notify는 "가끔 바뀌는 상태 하나"(전원 상태, 알람, 링크 상태)에 맞다. 고속 데이터 스트림에는 쓰지 않는다.

---

## 8. 속성은 uevent 전에 존재해야 한다 — `.dev_groups`가 이기는 이유

### 8.1 문제

udev(또는 mdev, systemd-udevd)는 커널 uevent를 받는 즉시 규칙을 실행한다. 규칙은 흔히 sysfs 속성을 읽어 판단하거나(`ATTR{model}=="..."`), 권한을 바꾼다(예제 5). 그런데 드라이버가 **uevent를 보낸 다음에** 속성을 만들면, udev가 먼저 달려가서 "파일이 없다"를 본다. 타이밍에 따라 되기도 하고 안 되기도 하는, 가장 나쁜 종류의 버그다. Greg Kroah-Hartman이 "sysfs 파일을 올바르게 만드는 법"으로 여러 번 강조한 문제다.

언제 uevent가 나가는지 driver core 코드(랩 소스)를 보면:

- `device_add()` (`drivers/base/core.c`): `device_add_attrs(dev)` — 여기서 **class·type·`dev->groups`의 그룹을 만들고** → … → `kobject_uevent(&dev->kobj, KOBJ_ADD)` → `bus_probe_device(dev)`.
- `really_probe()` (`drivers/base/dd.c`): `driver_sysfs_add()` → `call_driver_probe()`(우리 `probe()`) → **`device_add_groups(dev, drv->dev_groups)`** → … → `driver_bound()` 안에서 `kobject_uevent(&dev->kobj, KOBJ_BIND)`.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" font-size="13">A. 잘못: device_create() 후 sysfs_create_group()</text>
  <rect x="10" y="30" width="150" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="52" text-anchor="middle" font-size="12">device_create()</text>
  <rect x="178" y="30" width="150" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="253" y="52" text-anchor="middle" font-size="12">KOBJ_ADD uevent</text>
  <rect x="346" y="30" width="150" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="421" y="52" text-anchor="middle" font-size="12">udev: gain 없음!</text>
  <rect x="514" y="30" width="156" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="592" y="52" text-anchor="middle" font-size="12">sysfs_create_group()</text>
  <line x1="160" y1="47" x2="176" y2="47" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="328" y1="47" x2="344" y2="47" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="496" y1="47" x2="512" y2="47" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="10" y="96" font-size="13">B. 올바름: device_create_with_groups() 또는 dev->groups, class->dev_groups</text>
  <rect x="10" y="106" width="200" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="128" text-anchor="middle" font-size="12">device_add(): 그룹 먼저 생성</text>
  <rect x="236" y="106" width="150" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="311" y="128" text-anchor="middle" font-size="12">KOBJ_ADD uevent</text>
  <rect x="412" y="106" width="150" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="487" y="128" text-anchor="middle" font-size="12">udev: gain 있음</text>
  <line x1="210" y1="123" x2="234" y2="123" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="386" y1="123" x2="410" y2="123" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="10" y="172" font-size="13">C. 드라이버 속성: platform 장치 + driver.dev_groups</text>
  <rect x="10" y="182" width="116" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="68" y="204" text-anchor="middle" font-size="12">device_add()</text>
  <rect x="146" y="182" width="116" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="204" y="204" text-anchor="middle" font-size="12">KOBJ_ADD</text>
  <rect x="282" y="182" width="116" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="204" text-anchor="middle" font-size="12">probe()</text>
  <rect x="418" y="182" width="116" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="476" y="204" text-anchor="middle" font-size="12">dev_groups 생성</text>
  <rect x="554" y="182" width="116" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="612" y="204" text-anchor="middle" font-size="12">KOBJ_BIND</text>
  <line x1="126" y1="199" x2="144" y2="199" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="262" y1="199" x2="280" y2="199" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="398" y1="199" x2="416" y2="199" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="534" y1="199" x2="552" y2="199" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="68" y="236" text-anchor="middle" font-size="12">DT populate</text>
  <text x="204" y="236" text-anchor="middle" font-size="12">드라이버 없음</text>
  <text x="340" y="236" text-anchor="middle" font-size="12">자원 · HW 초기화</text>
  <text x="476" y="236" text-anchor="middle" font-size="12">속성 7개 + bin</text>
  <text x="612" y="236" text-anchor="middle" font-size="12">udev bind 규칙</text>
</svg>
```
그림 3 — 속성이 만들어지는 시점과 uevent의 순서. A는 uevent가 속성보다 먼저 나가는 경쟁(race), B는 `device_add()` 안에서 그룹을 먼저 만드는 올바른 방법, C는 드라이버가 붙이는 속성이 `KOBJ_BIND` 전에 생기는 흐름이다.

정리하면:

- **장치를 직접 만드는 코드**(class 디바이스, `device_create`, 서브시스템 코어)는 속성을 `device_create_with_groups()`, `dev->groups`, `class->dev_groups`, `device_type->groups`로 넘겨서 `device_add()` 안에서 만든다. → `KOBJ_ADD` 전에 존재.
- **드라이버**(platform/i2c/spi driver)는 장치를 만들지 않는다. 장치는 DT populate 때 이미 `KOBJ_ADD`를 보냈다. 드라이버 속성은 `driver.dev_groups`로 넘긴다. → driver core가 probe 성공 후, `KOBJ_BIND` 전에 만들고, unbind 때 자동으로 지운다. udev 규칙은 `ACTION=="bind"`로 건다.
- probe 안에서 `sysfs_create_group()`/`device_add_group()`을 부르면 순서는 C와 비슷하지만 **제거를 직접 해야 하고**, 에러 경로·remove에서 빼먹기 쉽다. 그리고 probe가 그룹을 만든 뒤 나중 단계에서 실패하면 "속성이 잠깐 보였다가 사라지는" 상태가 유저에게 노출된다. `.dev_groups`는 probe가 **성공한 뒤에만** 만든다. 그래서 새 코드는 `.dev_groups`를 쓰라는 것이 리뷰 표준이다 (`devm_device_add_group()`은 차선책).

### 8.2 예제 9 — 미니 udev로 경쟁을 눈으로 보기

무엇을 확인하나: netlink(`NETLINK_KOBJECT_UEVENT`)로 커널 uevent를 직접 받는 작은 프로그램이 이벤트를 받는 **즉시** 속성 파일이 있는지 검사한다. udev가 하는 일의 최소 버전이다.

```c
/* g8_uspy.c: mini-udev. On each kernel uevent, immediately check if an attribute exists */
#include <linux/netlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	char buf[4096], path[512];
	int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	int max = argc > 2 ? atoi(argv[2]) : 4;

	if (argc < 2) { fprintf(stderr, "usage: %s attr [n]\n", argv[0]); exit(1); }
	if (fd < 0 || bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		perror("netlink"); exit(1);
	}
	while (max-- > 0) {
		ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
		const char *action = "?", *devpath = "?";

		if (n < 0) { perror("recv"); exit(1); }
		buf[n] = 0;
		/* payload: "action@devpath\0KEY=VAL\0KEY=VAL\0..." */
		for (char *p = buf + strlen(buf) + 1; p < buf + n; p += strlen(p) + 1) {
			if (!strncmp(p, "ACTION=", 7)) action = p + 7;
			if (!strncmp(p, "DEVPATH=", 8)) devpath = p + 8;
		}
		snprintf(path, sizeof(path), "/sys%s/%s", devpath, argv[1]);
		printf("uevent %-6s %-45s %s: %s\n", action, devpath, argv[1],
		       access(path, F_OK) == 0 ? "PRESENT" : "MISSING");
		fflush(stdout);
	}
	return 0;
}
```

대조군 모듈 `g8_race.ko`: `good=0`이면 `device_create()` 뒤에 `sysfs_create_group()`(경쟁 창을 눈에 보이게 하려고 사이에 `msleep(200)`을 넣었다 — 실제 코드에서는 창이 마이크로초 단위라 "가끔" 실패한다), `good=1`이면 `device_create_with_groups()`.

```c
// SPDX-License-Identifier: GPL-2.0
/* g8_race.c: attribute created AFTER device_add (racy) vs. passed as groups (correct) */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>

static int good;
module_param(good, int, 0444);

static ssize_t gain_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "1\n");
}
static DEVICE_ATTR_RO(gain);
static struct attribute *g8r_attrs[] = { &dev_attr_gain.attr, NULL };
ATTRIBUTE_GROUPS(g8r);			/* -> g8r_groups */

static struct class *cls;
static struct device *d;

static int __init g8r_init(void)
{
	cls = class_create("g8race");
	if (IS_ERR(cls))
		return PTR_ERR(cls);
	if (good) {
		/* groups are added inside device_add(), BEFORE KOBJ_ADD uevent */
		d = device_create_with_groups(cls, NULL, 0, NULL, g8r_groups, "g8race0");
	} else {
		d = device_create(cls, NULL, 0, NULL, "g8race0");	/* uevent sent here */
		if (!IS_ERR(d)) {
			msleep(200);	/* widen the window so the race is visible */
			if (sysfs_create_group(&d->kobj, &g8r_group))
				pr_warn("g8_race: group failed\n");
		}
	}
	if (IS_ERR(d)) {
		class_destroy(cls);
		return PTR_ERR(d);
	}
	return 0;
}

static void __exit g8r_exit(void)
{
	if (!good)
		sysfs_remove_group(&d->kobj, &g8r_group);
	device_destroy(cls, 0);
	class_destroy(cls);
}
module_init(g8r_init);
module_exit(g8r_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G8 sysfs-vs-uevent race demo");
```

```sh
echo "== bad: device_create() then sysfs_create_group()"
g8_uspy gain 10 & P=$!; sleep 0.3
insmod /g8_race.ko good=0; sleep 0.5; ls /sys/class/g8race/g8race0/
rmmod g8_race; sleep 0.3; kill $P
echo "== good: device_create_with_groups()"
g8_uspy gain 10 & P=$!; sleep 0.3
insmod /g8_race.ko good=1; sleep 0.5
rmmod g8_race; sleep 0.3; kill $P
echo "== platform driver .dev_groups: unbind / bind"
insmod /g8_sensorkit.ko
g8_uspy odr 10 & P=$!; sleep 0.3
echo g8-sensorkit-a > /sys/bus/platform/drivers/g8_sensorkit/unbind
echo g8-sensorkit-a > /sys/bus/platform/drivers/g8_sensorkit/bind
sleep 0.5; kill $P
```

```text
lab initramfs: 6.18.55 up 1.58s
== bad: device_create() then sysfs_create_group()
uevent add    /class/g8race                                 gain: MISSING
uevent add    /devices/virtual/g8race/g8race0               gain: MISSING
uevent add    /module/g8_race                               gain: MISSING
gain       power      subsystem  uevent
uevent remove /devices/virtual/g8race/g8race0               gain: MISSING
uevent remove /class/g8race                                 gain: MISSING
uevent remove /module/g8_race                               gain: MISSING
== good: device_create_with_groups()
uevent add    /class/g8race                                 gain: MISSING
uevent add    /devices/virtual/g8race/g8race0               gain: PRESENT
uevent add    /module/g8_race                               gain: MISSING
uevent remove /devices/virtual/g8race/g8race0               gain: MISSING
uevent remove /class/g8race                                 gain: MISSING
uevent remove /module/g8_race                               gain: MISSING
== platform driver .dev_groups: unbind / bind
uevent remove /devices/virtual/misc/g8-sensorkit-a          odr: MISSING
uevent unbind /devices/platform/g8-sensorkit-a              odr: MISSING
uevent add    /devices/virtual/misc/g8-sensorkit-a          odr: MISSING
uevent bind   /devices/platform/g8-sensorkit-a              odr: PRESENT
```

출력에서 볼 것:

- 핵심 줄은 `uevent add /devices/virtual/g8race/g8race0`이다. 잘못된 버전은 **MISSING**, 올바른 버전은 **PRESENT**. 잘못된 버전에서도 0.5초 뒤 `ls`에는 `gain`이 보인다 — "나중에 보면 멀쩡한데 udev 규칙만 가끔 안 먹는" 전형적인 증상이다.
- `/class/g8race`, `/module/g8_race`에 대해서도 uevent가 나간다(클래스·모듈도 kobject). 거기에는 `gain`이 원래 없으니 MISSING이 정상이다.
- platform 드라이버: `unbind` 이벤트 시점엔 `odr`가 이미 지워졌고(driver core가 `.dev_groups`를 자동 제거), `bind` 이벤트 시점엔 **PRESENT**. `.dev_groups`를 쓰면 bind 규칙이 언제나 속성을 본다.
- 덤: probe에서 만든 misc 장치(`/devices/virtual/misc/g8-sensorkit-a`)도 자기 add/remove uevent를 보낸다. `/dev` 노드는 이 이벤트로 생긴다 (G7, G9).

---

## 9. debugfs — "개발자만 쓰는, 약속 없는 창문"

### 9.1 직관과 규칙

debugfs(`/sys/kernel/debug`)는 **규칙이 없는 것이 규칙**이다. 값 여러 개, 표, 레지스터 덤프, 바이너리 — 뭐든 된다. 대신:

- **ABI가 아니다.** 다음 커널에서 파일 이름·형식이 바뀌어도 아무도 보장하지 않는다. 그러니 **제품의 유저스페이스 서비스가 debugfs에 의존하면 안 된다.**
- 기본적으로 root만 접근한다(마운트 디렉터리 0700, 예제 5).
- 보안: 커널 lockdown(`lockdown=integrity` / `confidentiality`, Secure Boot 시스템에서 흔함)이 켜지면 debugfs 접근이 막힌다 (`LOCKDOWN_DEBUGFS`). `CONFIG_DEBUG_FS_ALLOW_NONE`이나 커널 인자 `debugfs=off`로 아예 끈 양산 커널도 많다. 랩 커널은 `CONFIG_DEBUG_FS_ALLOW_ALL=y`, lockdown LSM 없음이다. **"양산 이미지에서 debugfs가 없을 수 있다"를 전제로 설계한다.**
- 그래서 debugfs 생성 함수의 반환값은 **검사하지 않는 것이 커널 관례**다. debugfs가 실패해도 드라이버는 정상 동작해야 하고, 실패한 dentry(`ERR_PTR`)를 다른 debugfs 함수에 넘겨도 안전하게 무시된다.

SSD 비유: 펌웨어 디버그 UART 셸. 양산 펌웨어에서 꺼지고, 명령 형식은 팀 마음대로 바꾼다. 그 대신 bring-up 때 가장 많이 쓰는 도구다.

### 9.2 헬퍼 함수들

| 함수 | 만드는 것 | 쓰는 곳 |
|---|---|---|
| `debugfs_create_dir(name, parent)` | 디렉터리 | 드라이버/장치별 폴더 |
| `debugfs_create_u32(name, mode, parent, &var)` | 10진 u32 읽기/쓰기 knob | 튜닝 값, 결함 주입 |
| `debugfs_create_x32(...)` | 16진(`0x...`) 표시 u32 | 레지스터 값·마스크 |
| `debugfs_create_bool`, `_u8/_u16/_u64`, `_size_t`, `_atomic_t` | 타입별 knob | |
| `debugfs_create_regset32(name, mode, parent, &regset)` | **레지스터 맵 덤프** | bring-up, 현장 디버그 |
| `debugfs_create_file(name, mode, parent, data, &fops)` | 임의 파일 (보통 seq_file) | 통계, 표, 상태 머신 |
| `debugfs_create_blob(...)` | 메모리 영역 읽기 | 펌웨어 로그 버퍼 |
| `debugfs_remove(dentry)` | 디렉터리째 재귀 삭제 | remove/exit |

### 9.3 regset32 — 레지스터 맵을 이름과 함께 덤프

Don이 보드 bring-up 때 JTAG로 레지스터 창을 띄워 보던 것을, 드라이버가 스스로 파일 하나로 제공하는 기능이다. 드라이버는 `{이름, 오프셋}` 표와 `base` 주소만 주면 된다. 읽을 때마다 커널이 `readl(base + offset)`을 하고 `이름 = 0x%08x` 형식으로 출력한다 (`debugfs_print_regs32()`). `regset.dev`를 채우면 읽는 동안 runtime PM으로 장치를 깨워 준다 — 전원이 꺼진 블록의 레지스터를 읽다가 bus fault 나는 사고를 막는다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="140" y="10" width="400" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="32" text-anchor="middle" font-size="13">debugfs_regset32 = { regs[], nregs, base, dev }</text>
  <text x="145" y="66" text-anchor="middle" font-size="12">레지스터 뱅크 (base + offset)</text>
  <text x="525" y="66" text-anchor="middle" font-size="12">cat regs 출력 (readl 결과)</text>
  <rect x="10" y="76" width="270" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="94" font-size="12">+0x00 WHO_AM_I  고정 0x47</text>
  <rect x="10" y="106" width="270" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="124" font-size="12">+0x04 CTRL1  [3:0] odr, [7] en</text>
  <rect x="10" y="136" width="270" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="154" font-size="12">+0x08 CTRL2  [1:0] range</text>
  <rect x="10" y="166" width="270" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="184" font-size="12">+0x0c STATUS  drdy, alarm, st</text>
  <rect x="10" y="196" width="270" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="214" font-size="12">+0x10 TEMP  milli-degC</text>
  <rect x="10" y="226" width="270" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="244" font-size="12">+0x14 ST_CNT  self-test 횟수</text>
  <rect x="390" y="76" width="270" height="176" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="405" y="94" font-size="12">WHO_AM_I = 0x00000047</text>
  <text x="405" y="124" font-size="12">CTRL1 = 0x00000085</text>
  <text x="405" y="154" font-size="12">CTRL2 = 0x00000002</text>
  <text x="405" y="184" font-size="12">STATUS = 0x00000005</text>
  <text x="405" y="214" font-size="12">TEMP = 0x000061a8</text>
  <text x="405" y="244" font-size="12">ST_CNT = 0x00000001</text>
  <line x1="282" y1="89" x2="388" y2="89" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="282" y1="119" x2="388" y2="119" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="282" y1="149" x2="388" y2="149" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="282" y1="179" x2="388" y2="179" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="282" y1="209" x2="388" y2="209" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="282" y1="239" x2="388" y2="239" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="340" y="280" text-anchor="middle" font-size="12">sysfs odr=416 → CTRL1[3:0]=5 · range=8g → CTRL2=2 · self_test → STATUS[2], ST_CNT</text>
</svg>
```
그림 4 — `debugfs_create_regset32`의 구조. 드라이버는 이름·오프셋 표와 base만 준다. 오른쪽 값은 예제 10에서 sysfs로 odr=416, range=8g, self_test=1을 쓴 뒤 실제로 읽은 값이다.

### 9.4 드라이버 코드 (6/9) — debugfs 디렉터리, regset32, knob, seq_file

```c
/* ---- part 5: debugfs ---- */
static const struct debugfs_reg32 g8_regs[] = {
	{ "WHO_AM_I", REG_WHO_AM_I }, { "CTRL1", REG_CTRL1 },
	{ "CTRL2", REG_CTRL2 }, { "STATUS", REG_STATUS },
	{ "TEMP", REG_TEMP }, { "ST_CNT", REG_ST_CNT },
};

static int g8_stats_show(struct seq_file *s, void *unused)
{
	struct g8_priv *p = s->private;		/* data passed to debugfs_create_file */

	mutex_lock(&p->lock);
	seq_printf(s, "show_calls:   %u\n", p->st.show);
	seq_printf(s, "store_ok:     %u\n", p->st.store);
	seq_printf(s, "store_einval: %u\n", p->st.store_err);
	seq_printf(s, "notifies:     %u\n", p->st.notify);
	seq_printf(s, "self_tests:   %u\n", p->st.self_test);
	mutex_unlock(&p->lock);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(g8_stats);	/* -> g8_stats_fops (single_open) */

static void g8_debugfs_init(struct g8_priv *p)
{
	/* debugfs calls return ERR_PTR on failure; by design callers ignore it */
	p->dbg = debugfs_create_dir(dev_name(p->dev), g8_dbg_root);
	p->regset.regs = g8_regs;
	p->regset.nregs = ARRAY_SIZE(g8_regs);
	p->regset.base = p->base;
	debugfs_create_regset32("regs", 0400, p->dbg, &p->regset);
	debugfs_create_u32("temp_offset_mC", 0600, p->dbg, &p->temp_offset_mc);
	debugfs_create_x32("threshold_mC", 0400, p->dbg, &p->threshold_mc);
	debugfs_create_file("stats", 0400, p->dbg, p, &g8_stats_fops);
}
```

- `DEFINE_SHOW_ATTRIBUTE(g8_stats)`는 `g8_stats_show(struct seq_file *, void *)`를 받아 `g8_stats_open`(= `single_open`)과 `g8_stats_fops`를 만들어 준다. `debugfs_create_file`에 넘긴 `p`는 inode의 `i_private` → `single_open`의 data → `s->private`로 전달된다. seq_file은 출력이 PAGE_SIZE를 넘어도 알아서 버퍼를 키우므로 sysfs보다 자유롭다.
- `temp_offset_mC`는 0600 u32 knob이다. 테스트 코드가 "가짜 열"을 주입하는 **결함 주입(fault injection) 훅**이다 — 이런 건 절대 sysfs(ABI)에 두지 않는다.
- `threshold_mC`는 `x32`라 16진수로 보인다(`0x00009c40` = 40000). 데모용으로 일부러 x32를 썼다. 사람이 읽을 값은 u32가 낫다.
- `debugfs_remove(p->dbg)`(remove에서 호출)는 디렉터리를 **재귀로** 지운다. 옛 이름 `debugfs_remove_recursive`는 이제 같은 함수의 별칭이다.

### 9.5 예제 10 — 레지스터 덤프와 통계

무엇을 확인하나: 예제 4의 sysfs 쓰기(odr=416, range=8g, self_test=1, 잘못된 쓰기 3번)가 레지스터와 통계에 어떻게 반영됐는지 같은 부팅에서 이어서 본다.

```sh
D=/sys/kernel/debug/g8_sensorkit/g8-sensorkit-a
cat $D/regs; cat $D/threshold_mC; cat $D/temp_offset_mC; cat $D/stats
```

```text
== debugfs
WHO_AM_I = 0x00000047
CTRL1 = 0x00000085
CTRL2 = 0x00000002
STATUS = 0x00000005
TEMP = 0x000061a8
ST_CNT = 0x00000001
0x00009c40
0
show_calls:   2
store_ok:     1
store_einval: 3
notifies:     0
self_tests:   1
```

출력에서 볼 것:

- `CTRL1 = 0x85`: bit 7(enable) + odr 코드 5(= `odr_hz[5]` = 416). `CTRL2 = 2` = `range_str[2]` = "8g". sysfs의 "사람 단위"(Hz, g)가 레지스터 비트로 어떻게 번역됐는지 한눈에 보인다. bring-up에서 "sysfs에 썼는데 HW가 안 바뀐다"를 디버그할 때 바로 이 대조를 한다.
- `STATUS = 0x5`: bit 0(drdy, work가 매 샘플 세움) + bit 2(self-test pass). `TEMP = 0x61a8` = 25000.
- `store_einval: 3` — odr에 대한 잘못된 쓰기 3번(100, abc, 99999999999)을 셌다. `odr_store`만 통계를 세게 만들었기 때문이다. `store_ok: 1`은 `echo 416 > odr` 한 번.

### 9.6 장치 b의 레지스터와 정리 확인 (예제 11)

무엇을 확인하나: 온도 센서가 없는 장치 `b`의 레지스터, `/proc/misc`(레거시 procfs), DT 속성, 그리고 rmmod 후 debugfs·sysfs 정리.

```sh
insmod /g8_sensorkit.ko
echo "== procfs (legacy)"; grep g8 /proc/misc
echo "== DT props"; ls /sys/bus/platform/devices/g8-sensorkit-a/of_node/ /sys/bus/platform/devices/g8-sensorkit-b/of_node/
echo "== regs b"; cat /sys/kernel/debug/g8_sensorkit/g8-sensorkit-b/regs
echo "== rmmod removes everything"; rmmod g8_sensorkit; ls /sys/kernel/debug/g8_sensorkit 2>&1; ls /sys/bus/platform/devices/g8-sensorkit-a/
```

```text
lab initramfs: 6.18.55 up 10.82s
== procfs (legacy)
259 g8-sensorkit-b
258 g8-sensorkit-a
== DT props
/sys/bus/platform/devices/g8-sensorkit-a/of_node/:
compatible            g8,temp-threshold-mC  name
g8,has-temp-sensor    label

/sys/bus/platform/devices/g8-sensorkit-b/of_node/:
compatible  label       name
== regs b
WHO_AM_I = 0x00000047
CTRL1 = 0x00000083
CTRL2 = 0x00000000
STATUS = 0x00000000
TEMP = 0x00000000
ST_CNT = 0x00000000
== rmmod removes everything
ls: /sys/kernel/debug/g8_sensorkit: No such file or directory
driver_override  of_node          subsystem
modalias         power            uevent
```

출력에서 볼 것:

- `b`는 샘플링 work가 없어서 `STATUS`·`TEMP`가 0이다. 장치별 디렉터리가 따로라 `a`와 섞이지 않는다.
- rmmod 후 debugfs 디렉터리가 사라졌고, platform 장치 디렉터리에는 버스가 만든 파일만 남았다. 우리 속성 7개 + `calibration`은 driver core가 unbind 때 `.dev_groups`를 지우면서 자동으로 사라졌다 — remove에 sysfs 정리 코드가 한 줄도 없다.
- `/proc/misc`는 misc 장치의 minor 번호 목록이다. procfs에 남아 있는 "장치 관련 레거시 파일"의 예 (§11).

---

## 10. configfs — 유저가 커널 객체를 만드는 곳

### 10.1 직관

sysfs의 디렉터리는 **커널이** 만든다. 유저는 `mkdir /sys/...`를 할 수 없다. 그런데 "유저가 원하는 만큼 객체를 만들고 설정한 뒤 활성화"해야 하는 경우가 있다.

- **USB gadget**: 보드가 USB 장치 역할을 할 때 어떤 기능(ACM 시리얼, 이더넷 NCM, mass storage)을 몇 개 묶을지 유저가 조립한다. 드론·로봇 컨트롤러가 USB로 PC에 "시리얼 + 네트워크" 복합 장치로 보이게 하는 게 이것이다.
- **nvmet**(NVMe over Fabrics target): 서브시스템, 네임스페이스, 포트를 mkdir로 만든다. Don이 SSD에서 본 NVMe 개념(subsystem/namespace/port)이 그대로 디렉터리가 된다.
- **iSCSI/LIO target**, **IIO software trigger**(`configfs-iio`), **netconsole** 대상 등.
- **DT overlay 적용**: 업스트림 커널에는 configfs로 overlay를 올리는 인터페이스가 **없다**. Raspberry Pi 등 다운스트림 커널에 `/sys/kernel/config/device-tree/overlays/<name>/dtbo`에 blob을 써서 적용하는 패치가 있다 (D6 참조). 업스트림에서 이 방식은 보안·수명 문제로 받아들여지지 않았다.

configfs의 모델: `mkdir` → 커널의 `make_item()`/`make_group()` 호출 → 새 객체 + 속성 파일 생성. 속성에 값을 쓰고, 마지막에 "활성화" 속성(예: gadget의 `UDC`)에 쓰면 커널이 그 설정으로 동작한다. `rmdir` → `drop_item()` → 참조가 0이 되면 `release()`. 객체 생명주기를 **유저가** 결정한다는 점이 sysfs와 정반대다.

SSD 비유: NVMe Namespace Management(Create) + Attachment. 호스트가 "이 크기로 namespace 하나 만들어" → 펌웨어가 객체 생성 → Attach해서 활성화.

### 10.2 예제 12 — sensorkit 채널을 mkdir로 만드는 configfs 서브시스템

무엇을 확인하나: 랩 커널은 `CONFIG_CONFIGFS_FS=y`다. `/sys/kernel/config/g8_sensorkit/` 아래에서 mkdir하면 "채널" 객체가 생기고 `axis`, `enable` 속성이 붙는 모듈을 만든다. 코드는 두 조각이다.

```c
// SPDX-License-Identifier: GPL-2.0
/* g8_cfs.c: configfs subsystem - userspace mkdir creates a sensor "channel" object */
#include <linux/module.h>
#include <linux/configfs.h>
#include <linux/slab.h>

struct g8_chan {
	struct config_item item;
	unsigned int axis;		/* 0=x 1=y 2=z */
	bool enable;
};
#define to_chan(i) container_of(i, struct g8_chan, item)

static ssize_t g8_chan_axis_show(struct config_item *i, char *page)
{
	return sysfs_emit(page, "%u\n", to_chan(i)->axis);	/* page = one zeroed page */
}
static ssize_t g8_chan_axis_store(struct config_item *i, const char *page, size_t len)
{
	unsigned int v;
	int ret = kstrtouint(page, 0, &v);

	if (ret)
		return ret;
	if (v > 2)
		return -EINVAL;
	to_chan(i)->axis = v;
	return len;
}
static ssize_t g8_chan_enable_show(struct config_item *i, char *page)
{
	return sysfs_emit(page, "%d\n", to_chan(i)->enable);
}
static ssize_t g8_chan_enable_store(struct config_item *i, const char *page, size_t len)
{
	int ret = kstrtobool(page, &to_chan(i)->enable);

	if (!ret)
		pr_info("g8_cfs: channel %s axis %u %s\n", config_item_name(i),
			to_chan(i)->axis, to_chan(i)->enable ? "ENABLED" : "disabled");
	return ret ? ret : len;
}
CONFIGFS_ATTR(g8_chan_, axis);
CONFIGFS_ATTR(g8_chan_, enable);
static struct configfs_attribute *g8_chan_attrs[] = {
	&g8_chan_attr_axis, &g8_chan_attr_enable, NULL,
};

static void g8_chan_release(struct config_item *i)
{
	pr_info("g8_cfs: release (last ref dropped), axis was %u\n", to_chan(i)->axis);
	kfree(to_chan(i));
}
static struct configfs_item_operations g8_chan_item_ops = { .release = g8_chan_release };
static const struct config_item_type g8_chan_type = {
	.ct_item_ops = &g8_chan_item_ops,
	.ct_attrs = g8_chan_attrs,
	.ct_owner = THIS_MODULE,
};
```

```c
static struct config_item *g8_make_chan(struct config_group *g, const char *name)
{
	struct g8_chan *c = kzalloc(sizeof(*c), GFP_KERNEL);

	if (!c)
		return ERR_PTR(-ENOMEM);
	config_item_init_type_name(&c->item, name, &g8_chan_type);
	pr_info("g8_cfs: mkdir -> new channel '%s'\n", name);
	return &c->item;
}

static void g8_drop_chan(struct config_group *g, struct config_item *i)
{
	pr_info("g8_cfs: rmdir -> drop channel '%s'\n", config_item_name(i));
	config_item_put(i);		/* -> release() when refcount hits 0 */
}

static struct configfs_group_operations g8_root_ops = {
	.make_item = g8_make_chan,
	.drop_item = g8_drop_chan,
};
static const struct config_item_type g8_root_type = {
	.ct_group_ops = &g8_root_ops,
	.ct_owner = THIS_MODULE,
};

static struct configfs_subsystem g8_subsys = {
	.su_group.cg_item = {
		.ci_namebuf = "g8_sensorkit",	/* /sys/kernel/config/g8_sensorkit */
		.ci_type = &g8_root_type,
	},
};

static int __init g8_cfs_init(void)
{
	config_group_init(&g8_subsys.su_group);
	mutex_init(&g8_subsys.su_mutex);
	return configfs_register_subsystem(&g8_subsys);
}
static void __exit g8_cfs_exit(void)
{
	configfs_unregister_subsystem(&g8_subsys);
}
module_init(g8_cfs_init);
module_exit(g8_cfs_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G8 configfs channel demo");
```

- configfs 속성 show/store는 `struct config_item *`를 받는다 (sysfs는 `struct device *`). `CONFIGFS_ATTR(g8_chan_, axis)`는 `g8_chan_axis_show/_store`를 연결해 `g8_chan_attr_axis`를 만든다. configfs의 `page`도 `get_zeroed_page()`로 받은 페이지 하나라서(`fs/configfs/file.c`) `sysfs_emit`을 그대로 쓸 수 있다. 업스트림 USB gadget configfs 코드도 `sysfs_emit`을 쓴다.
- `make_item`이 `config_item_init_type_name()`으로 객체를 초기화하면, configfs가 디렉터리와 속성 파일을 만든다.
- `drop_item`에서 `config_item_put()` → 참조가 0이 되면 `release()`에서 `kfree`. 처음에는 `release()`에서 `config_item_name()`을 찍었더니 `(null)`이 나왔다 — release 시점에는 이름이 이미 해제돼 있다. 이름이 필요한 로그는 `drop_item`에서 남긴다.

```sh
mount -t configfs none /sys/kernel/config
insmod /g8_cfs.ko
cd /sys/kernel/config/g8_sensorkit
mkdir accel_x gyro_z; ls -l; ls -l accel_x
echo 2 > gyro_z/axis; echo 1 > gyro_z/enable
echo 7 > accel_x/axis; echo "rc=$?"
cat gyro_z/axis gyro_z/enable
rmdir accel_x; ls
cd /; rmmod g8_cfs; lsmod | grep g8_cfs
rmdir /sys/kernel/config/g8_sensorkit/gyro_z; rmmod g8_cfs; lsmod | grep -c g8_cfs
dmesg | grep g8_cfs
```

```text
lab initramfs: 6.18.55 up 2.37s
total 0
drwxr-xr-x    2 0        0                0 Oct 11 01:48 accel_x
drwxr-xr-x    2 0        0                0 Oct 11 01:48 gyro_z
total 0
-rw-r--r--    1 0        0             4096 Oct 11 01:48 axis
-rw-r--r--    1 0        0             4096 Oct 11 01:48 enable
sh: write error: Invalid argument
rc=1
2
1
gyro_z
rmmod: remove 'g8_cfs': Resource temporarily unavailable
g8_cfs 12288 2 - Live 0xffffaa18f3109000 (O)
0
[    2.756359] g8_cfs: loading out-of-tree module taints kernel.
[    2.779104] g8_cfs: mkdir -> new channel 'accel_x'
[    2.780212] g8_cfs: mkdir -> new channel 'gyro_z'
[    2.822169] g8_cfs: channel gyro_z axis 2 ENABLED
[    2.841709] g8_cfs: rmdir -> drop channel 'accel_x'
[    2.841811] g8_cfs: release (last ref dropped), axis was 0
[    2.894286] g8_cfs: rmdir -> drop channel 'gyro_z'
[    2.894329] g8_cfs: release (last ref dropped), axis was 2
```

출력에서 볼 것:

- 랩 initramfs는 configfs를 자동 마운트하지 않는다(`/init`은 proc, sysfs, devtmpfs, debugfs만 마운트). 일반 배포판에서는 systemd가 `/sys/kernel/config`에 마운트한다.
- `mkdir accel_x` 한 번에 `make_item`이 불리고 `axis`, `enable` 파일이 생겼다. **파일 이름(=객체 이름)을 유저가 정했다.**
- 잘못된 값(`axis`에 7)은 sysfs와 똑같이 `-EINVAL` → `write error`.
- 채널 `gyro_z`가 살아 있는 동안 `rmmod`는 **실패**한다(참조 카운트 2 — 채널 객체가 모듈을 붙잡고 있다). 객체를 rmdir한 뒤에야 내려간다. (BusyBox `rmmod`는 이때도 종료 코드 0을 돌려줘서 `lsmod`로 확인했다.)

### 10.3 예제 13 — 진짜 사용자: USB gadget을 configfs로 조립

무엇을 확인하나: 랩 빌드 트리의 `libcomposite.ko`, `u_serial.ko`, `usb_f_acm.ko`(모두 `=m`)를 넣고 USB gadget을 mkdir로 조립해 본다. QEMU `virt`에는 USB 장치 컨트롤러(UDC)가 없어서 마지막 활성화(`UDC` 파일에 컨트롤러 이름 쓰기)는 할 수 없다.

```sh
insmod /libcomposite.ko; insmod /u_serial.ko; insmod /usb_f_acm.ko
cd /sys/kernel/config/usb_gadget && mkdir g8g && cd g8g
ls
echo 0x1d6b > idVendor; mkdir functions/acm.gs0 configs/c.1
ln -s functions/acm.gs0 configs/c.1/; ls -l configs/c.1
ls /sys/class/udc; echo "UDC list empty -> nothing to bind (no UDC in QEMU virt)"
```

```text
== real user: USB gadget
UDC              bMaxPacketSize0  functions        os_desc
bDeviceClass     bcdDevice        idProduct        strings
bDeviceProtocol  bcdUSB           idVendor         webusb
bDeviceSubClass  configs          max_speed
total 0
-rw-r--r--    1 0        0             4096 Oct 11 01:37 MaxPower
lrwxrwxrwx    1 0        0                0 Oct 11 01:37 acm.gs0 -> ../../../../usb_gadget/g8g/functions/acm.gs0
-rw-r--r--    1 0        0             4096 Oct 11 01:37 bmAttributes
drwxr-xr-x    2 0        0                0 Oct 11 01:37 strings
UDC list empty -> nothing to bind (no UDC in QEMU virt)
```

출력에서 볼 것:

- `mkdir g8g` 한 번에 USB 장치 디스크립터 필드(`idVendor`, `bcdUSB`, `bDeviceClass` …)가 파일로 생겼다. USB 스펙의 Device Descriptor가 그대로 디렉터리다.
- `ln -s functions/acm.gs0 configs/c.1/` — configfs는 **심볼릭 링크로 객체를 연결**한다(`allow_link` 콜백). "이 configuration에 이 function을 넣는다"는 관계 표현이다. sysfs에는 없는 기능이다.
- 실제 보드(i.MX8, AM62 등 OTG 포트)에서는 마지막에 `echo ci_hdrc.0 > UDC`처럼 UDC 이름을 쓰면 PC에 `/dev/ttyACM0`이 생긴다 (이 노트에서 실행하지 않았다 — UDC가 없음).

---

## 11. procfs — 장치 파일을 새로 만들지 않는다

`/proc`은 원래 이름 그대로 **프로세스** 정보(`/proc/<pid>/maps`, `status`, `fd/`)를 위한 곳이다. 리눅스 초기에 sysfs가 없어서 드라이버들이 `/proc/driver/...`, `/proc/<무언가>`에 장치 정보를 넣었고, 그 흔적이 `/proc/interrupts`, `/proc/devices`, `/proc/misc`(예제 11), `/proc/bus/...`, `/proc/scsi/...`로 남아 있다. 이미 유저가 쓰는 것이라 지우지는 못하지만:

- **새 드라이버가 procfs 파일을 추가하는 패치는 리뷰에서 거절된다.** 장치 설정·상태는 sysfs, 디버그는 debugfs로 간다.
- 시스템 전역 튜닝은 `/proc/sys`(sysctl)이 맞는 자리지만, 이것도 드라이버 개별 설정에는 쓰지 않는다 (모듈 파라미터 `/sys/module/<mod>/parameters/`나 장치 속성이 맞다).
- procfs에는 형식 규칙이 없어서 "값 여러 개를 한 파일에" 넣은 레거시 파일이 많다. 그걸 파싱하는 유저 코드 때문에 형식을 못 바꾸는 고통이, 바로 sysfs가 "값 하나 = 파일 하나"를 고집하는 이유다.

### 11.1 무엇을 어디에 — 결정표

| 노출하려는 것 | 자리 | 예 (sensorkit) | 이유 |
|---|---|---|---|
| 장치 설정 (런타임 변경) | sysfs (가능하면 서브시스템 표준 이름) | `odr`, `range` | ABI, 사람이 읽음, udev가 다룸 |
| 장치 상태·정보 (가끔 읽음) | sysfs RO | `temperature`, `alarm` | 값 하나, poll 가능 |
| 동작 트리거 | sysfs WO | `self_test` | 단순, 권한 제어 쉬움 |
| 작은 바이너리 blob | sysfs binary attribute | `calibration` | 크기 제한·오프셋 자동 |
| 고속 데이터 스트림 | 문자 디바이스 `read`/`mmap` 또는 IIO 버퍼 | 가속도 샘플 1 kHz | sysfs는 값 하나당 syscall 여러 번 (§12) |
| 원자적 다중 필드 명령 | `ioctl` (G5) | "ODR+range+FIFO 동시 설정" | sysfs는 파일마다 따로라 원자성 없음 |
| 레지스터 덤프, 내부 통계, 결함 주입 | debugfs | `regs`, `stats`, `temp_offset_mC` | ABI 아님, root 전용 |
| 유저가 개수를 정하는 객체 | configfs | 채널 정의 | mkdir/rmdir 생명주기 |
| 전역 커널 튜닝 | `/proc/sys` (sysctl) | — | 드라이버 개별 설정에는 아님 |
| 새 장치 정보 파일 | procfs | **금지** | 레거시 |
| 모듈 로드 시 설정 | 모듈 파라미터 (`/sys/module/.../parameters`) | 디버그 레벨 | 장치별 설정에는 부적합 |

---

## 12. 측정 — 값 하나 읽는 비용: sysfs vs `read()` vs `ioctl`

### 12.1 드라이버 코드 (7/9) — 비교용 문자 디바이스

공정한 비교를 위해 같은 드라이버에 misc 장치(G7)를 붙였다. `read()`는 온도를 4바이트 바이너리로, `ioctl(G8_IOC_GET_TEMP)`는 `put_user`로 돌려준다.

```c
/* ---- part 6: char interface for the benchmark (read() and ioctl) ---- */
static ssize_t g8_misc_read(struct file *f, char __user *ubuf, size_t len, loff_t *ppos)
{
	struct g8_priv *p = container_of(f->private_data, struct g8_priv, misc);
	int t = (int)g8_rd(p, REG_TEMP);

	if (len < sizeof(t))
		return -EINVAL;
	return copy_to_user(ubuf, &t, sizeof(t)) ? -EFAULT : sizeof(t);
}

static long g8_misc_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct g8_priv *p = container_of(f->private_data, struct g8_priv, misc);
	int t = (int)g8_rd(p, REG_TEMP);

	if (cmd != G8_IOC_GET_TEMP)
		return -ENOTTY;
	return put_user(t, (int __user *)arg);
}

static const struct file_operations g8_misc_fops = {
	.owner = THIS_MODULE,
	.read = g8_misc_read,
	.unlocked_ioctl = g8_misc_ioctl,
};
```

### 12.2 드라이버 코드 (8/9, 9/9) — probe, remove, 등록

```c
/* ---- part 7: probe / remove / module init ---- */
static int g8_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct g8_priv *p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	int ret;

	if (!p)
		return -ENOMEM;
	p->dev = dev;
	mutex_init(&p->lock);
	p->base = (void __force __iomem *)p->regs;	/* real HW: devm_platform_ioremap_resource() */
	g8_wr(p, REG_WHO_AM_I, 0x47);
	g8_wr(p, REG_CTRL1, 0x80 | 3);			/* enabled, 104 Hz */
	p->has_temp = device_property_read_bool(dev, "g8,has-temp-sensor");
	p->threshold_mc = 40000;
	device_property_read_u32(dev, "g8,temp-threshold-mC", &p->threshold_mc);
	platform_set_drvdata(pdev, p);

	p->misc.minor = MISC_DYNAMIC_MINOR;
	p->misc.name = dev_name(dev);			/* /dev/g8-sensorkit-a */
	p->misc.fops = &g8_misc_fops;
	ret = misc_register(&p->misc);
	if (ret)
		return ret;
	g8_debugfs_init(p);
	INIT_DELAYED_WORK(&p->poll_work, g8_poll_work);
	if (p->has_temp)
		schedule_delayed_work(&p->poll_work, 0);
	dev_info(dev, "probed (has_temp=%d)\n", p->has_temp);
	return 0;	/* driver core adds .dev_groups AFTER this returns */
}

static void g8_remove(struct platform_device *pdev)
{
	struct g8_priv *p = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&p->poll_work);
	debugfs_remove(p->dbg);		/* recursive */
	misc_deregister(&p->misc);
}
```

```c
static const struct of_device_id g8_of_match[] = {
	{ .compatible = "g8,sensorkit" },
	{ }
};
MODULE_DEVICE_TABLE(of, g8_of_match);

static struct platform_driver g8_driver = {
	.probe = g8_probe,
	.remove = g8_remove,
	.driver = {
		.name = "g8_sensorkit",
		.of_match_table = g8_of_match,
		.dev_groups = g8_groups,	/* created before KOBJ_BIND uevent */
	},
};

static int __init g8_init(void)
{
	int ret;

	g8_dbg_root = debugfs_create_dir("g8_sensorkit", NULL);
	ret = platform_driver_register(&g8_driver);
	if (ret)
		debugfs_remove(g8_dbg_root);
	return ret;
}

static void __exit g8_exit(void)
{
	platform_driver_unregister(&g8_driver);
	debugfs_remove(g8_dbg_root);
}
module_init(g8_init);
module_exit(g8_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G8 sensorkit: sysfs/debugfs/misc demo");
```

- probe에는 sysfs 코드가 **한 줄도 없다**. 속성은 `.driver.dev_groups = g8_groups` 한 줄로 끝났다.
- `misc.name = dev_name(dev)`로 장치마다 다른 `/dev` 이름을 만들었다 (`/dev/g8-sensorkit-a`, `-b`).
- remove 순서: 샘플링 work 정지(`cancel_delayed_work_sync`) → debugfs → misc. work가 돌면서 `sysfs_notify`를 부르는 중에 장치가 사라지지 않게 work부터 멈춘다. sysfs 쪽 순서는 driver core가 보장한다: 랩 소스 `drivers/base/dd.c`의 `device_remove()`는 **먼저 `device_remove_groups(dev, drv->dev_groups)`로 속성을 지우고**(kernfs가 실행 중인 show/store가 끝날 때까지 기다린다) 그다음 `drv->remove()`를 부르며, devm 자원(`p`)은 그 뒤 `device_unbind_cleanup()`에서 해제된다. 그래서 remove 안에서는 더 이상 show/store가 불리지 않는다.

### 12.3 예제 14 — 벤치마크 프로그램

무엇을 확인하나: 온도 값 하나를 얻는 네 가지 방법의 호출당 시간.

```c
/* g8_bench.c: cost of getting ONE value: sysfs (open/read/close), sysfs pread, char read(), ioctl */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define G8_IOC_GET_TEMP _IOR('g', 1, int)
#define SYS "/sys/bus/platform/devices/g8-sensorkit-a/temperature"
#define DEV "/dev/g8-sensorkit-a"

static long long ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
static void die(const char *m) { perror(m); exit(1); }

int main(int argc, char **argv)
{
	int i, n = argc > 1 ? atoi(argv[1]) : 20000, v = 0;
	char buf[32];
	int sfd = open(SYS, O_RDONLY), dfd = open(DEV, O_RDONLY);
	long long t;

	if (sfd < 0) die(SYS);
	if (dfd < 0) die(DEV);

	t = ns();
	for (i = 0; i < n; i++) {		/* what `cat` or a naive script does */
		int fd = open(SYS, O_RDONLY);
		if (fd < 0 || read(fd, buf, sizeof(buf)) <= 0) die("sysfs open/read");
		close(fd);
	}
	printf("sysfs open+read+close : %6lld ns/op\n", (ns() - t) / n);

	t = ns();
	for (i = 0; i < n; i++)			/* keep fd open, re-read from 0 */
		if (pread(sfd, buf, sizeof(buf), 0) <= 0) die("sysfs pread");
	printf("sysfs pread (fd kept) : %6lld ns/op  (%.*s)\n", (ns() - t) / n, 5, buf);

	t = ns();
	for (i = 0; i < n; i++)
		if (read(dfd, &v, sizeof(v)) != sizeof(v)) die("char read");
	printf("char dev read() 4B    : %6lld ns/op  (%d)\n", (ns() - t) / n, v);

	t = ns();
	for (i = 0; i < n; i++)
		if (ioctl(dfd, G8_IOC_GET_TEMP, &v) < 0) die("ioctl");
	printf("ioctl(GET_TEMP)       : %6lld ns/op  (%d)\n", (ns() - t) / n, v);
	return 0;
}
```

```sh
insmod /g8_sensorkit.ko
for k in 1 2 3; do echo "-- run $k"; g8_bench 20000; done
```

```text
lab initramfs: 6.18.55 up 2.72s
-- run 1
sysfs open+read+close : 260143 ns/op
sysfs pread (fd kept) :  35342 ns/op  (25000)
char dev read() 4B    :  20864 ns/op  (25000)
ioctl(GET_TEMP)       :  19718 ns/op  (25000)
-- run 2
sysfs open+read+close : 210092 ns/op
sysfs pread (fd kept) :  42467 ns/op  (25000)
char dev read() 4B    :  30133 ns/op  (25000)
ioctl(GET_TEMP)       :  20209 ns/op  (25000)
-- run 3
sysfs open+read+close : 186715 ns/op
sysfs pread (fd kept) :  31209 ns/op  (25000)
char dev read() 4B    :  18454 ns/op  (25000)
ioctl(GET_TEMP)       :  15377 ns/op  (25000)
```

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="13">값 하나 읽기 비용 (µs/op, 3회 중앙값, QEMU TCG 게스트)</text>
  <text x="190" y="57" text-anchor="end" font-size="12">sysfs open+read+close</text>
  <rect x="200" y="40" width="382" height="26" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="588" y="57" font-size="12">210.1</text>
  <text x="190" y="97" text-anchor="end" font-size="12">sysfs pread (fd 유지)</text>
  <rect x="200" y="80" width="64" height="26" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="270" y="97" font-size="12">35.3</text>
  <text x="190" y="137" text-anchor="end" font-size="12">char read() 4B</text>
  <rect x="200" y="120" width="38" height="26" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="244" y="137" font-size="12">20.9</text>
  <text x="190" y="177" text-anchor="end" font-size="12">ioctl(GET_TEMP)</text>
  <rect x="200" y="160" width="36" height="26" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="242" y="177" font-size="12">19.7</text>
  <line x1="200" y1="196" x2="620" y2="196" stroke="currentColor"/>
  <line x1="200" y1="196" x2="200" y2="201" stroke="currentColor"/>
  <line x1="291" y1="196" x2="291" y2="201" stroke="currentColor"/>
  <line x1="382" y1="196" x2="382" y2="201" stroke="currentColor"/>
  <line x1="473" y1="196" x2="473" y2="201" stroke="currentColor"/>
  <line x1="564" y1="196" x2="564" y2="201" stroke="currentColor"/>
  <text x="200" y="215" text-anchor="middle" font-size="12">0</text>
  <text x="291" y="215" text-anchor="middle" font-size="12">50</text>
  <text x="382" y="215" text-anchor="middle" font-size="12">100</text>
  <text x="473" y="215" text-anchor="middle" font-size="12">150</text>
  <text x="564" y="215" text-anchor="middle" font-size="12">200</text>
  <text x="410" y="238" text-anchor="middle" font-size="12">µs per value (낮을수록 좋음)</text>
</svg>
```
그림 5 — 예제 14의 세 번 실행 중앙값. 절대값은 KVM 없는 TCG 에뮬레이션이라 실제 하드웨어보다 수십 배 크다. 봐야 할 것은 비율이다.

출력에서 볼 것:

- **`cat`처럼 매번 open/close하는 방식이 압도적으로 비싸다** (중앙값 210 µs, 나머지의 6~10배). 경로 이름 해석(`/sys/bus/platform/devices/...` 디렉터리 7단계 탐색), kernfs open 파일 구조 할당, seq_file 준비가 매번 일어난다. 셸 스크립트로 1 kHz 센서를 읽는 설계가 왜 안 되는지 보여 준다.
- fd를 열어 두고 `pread(fd, buf, n, 0)`로 다시 읽으면 35 µs로 6배 빨라진다. sysfs를 자주 읽어야 하는 데몬은 이렇게 한다 (예제 8의 poller도 같은 방식).
- 그래도 `read()` 4바이트(21 µs)와 `ioctl`(20 µs)보다 느리다. sysfs 경로는 seq_file 버퍼 준비 + PAGE 0 채우기 + `sprintf`류 포맷 + 유저 쪽 파싱이 더 붙는다. `read()`와 `ioctl`은 거의 같다 — 둘 다 syscall 1번 + `copy_to_user` 4바이트다.
- 실행마다 20~40% 흔들린다 (같은 VM에서 다른 작업이 돌고, TCG는 호스트 부하에 민감). 결론(순서와 대략의 비율)은 세 번 모두 같다.
- 정리: **설정·가끔 읽는 상태 → sysfs**(비용 무관, 사람·스크립트 친화), **고속 반복 데이터 → 문자 디바이스 read/mmap 또는 IIO 버퍼**. 이 측정은 G5의 "ioctl vs sysfs" 판단의 숫자 근거다.

---

## 13. 커널 안에서는 무슨 일이 일어나나

`echo 416 > odr`와 `cat odr`이 우리 함수에 도달하는 경로다 (랩 소스 6.18.55 기준 함수 이름).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="160" y="22" text-anchor="middle" font-size="13">쓰기: echo 416 &gt; odr</text>
  <text x="510" y="22" text-anchor="middle" font-size="13">읽기: cat odr</text>
  <rect x="10" y="34" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="160" y="53" text-anchor="middle" font-size="13">write(fd, "416\n", 4)</text>
  <text x="160" y="70" text-anchor="middle" font-size="12">유저 공간</text>
  <rect x="360" y="34" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="53" text-anchor="middle" font-size="13">read(fd, buf, 4096)</text>
  <text x="510" y="70" text-anchor="middle" font-size="12">유저 공간</text>
  <line x1="10" y1="85" x2="670" y2="85" stroke="currentColor" stroke-dasharray="5 4"/>
  <rect x="10" y="92" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="111" text-anchor="middle" font-size="13">vfs_write → kernfs_fop_write_iter()</text>
  <text x="160" y="128" text-anchor="middle" font-size="12">커널 페이지로 복사, ≤ PAGE_SIZE</text>
  <rect x="360" y="92" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="111" text-anchor="middle" font-size="13">vfs_read → kernfs_fop_read_iter()</text>
  <text x="510" y="128" text-anchor="middle" font-size="12">seq_file로 PAGE 버퍼 준비</text>
  <rect x="10" y="148" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="167" text-anchor="middle" font-size="13">sysfs_kf_write()</text>
  <text x="160" y="184" text-anchor="middle" font-size="12">kobject의 sysfs_ops 선택</text>
  <rect x="360" y="148" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="167" text-anchor="middle" font-size="13">sysfs_kf_seq_show()</text>
  <text x="510" y="184" text-anchor="middle" font-size="12">버퍼를 0으로 채우고 show 호출</text>
  <rect x="10" y="204" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="223" text-anchor="middle" font-size="13">dev_attr_store()</text>
  <text x="160" y="240" text-anchor="middle" font-size="12">container_of → device_attribute</text>
  <rect x="360" y="204" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="223" text-anchor="middle" font-size="13">dev_attr_show()</text>
  <text x="510" y="240" text-anchor="middle" font-size="12">container_of → device_attribute</text>
  <rect x="10" y="260" width="300" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="279" text-anchor="middle" font-size="13">odr_store(dev, attr, buf, count)</text>
  <text x="160" y="296" text-anchor="middle" font-size="12">검증 → 레지스터 쓰기 → count</text>
  <rect x="360" y="260" width="300" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="279" text-anchor="middle" font-size="13">odr_show(dev, attr, buf)</text>
  <text x="510" y="296" text-anchor="middle" font-size="12">sysfs_emit(buf, ...) → 길이</text>
  <line x1="160" y1="78" x2="160" y2="90" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="136" x2="160" y2="146" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="192" x2="160" y2="202" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="248" x2="160" y2="258" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="78" x2="510" y2="90" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="136" x2="510" y2="146" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="192" x2="510" y2="202" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="248" x2="510" y2="258" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="340" y="322" text-anchor="middle" font-size="12">kernfs(파일시스템) → sysfs(kobject 연결) → driver core(device) → 드라이버</text>
</svg>
```
그림 6 — sysfs 속성 read/write의 커널 경로. 점선 위가 유저 공간이다. 아래로 갈수록 범용(VFS, kernfs)에서 구체(우리 드라이버)로 내려간다.

층별로 정리하면:

1. **kernfs** (`fs/kernfs/`): sysfs·cgroupfs가 공유하는 "커널 객체를 위한 가상 파일시스템" 엔진. 디렉터리·파일 노드(`struct kernfs_node`)와 열린 파일(`struct kernfs_open_file`)을 관리하고, poll 대기 큐·event 번호(§7)도 여기 있다. **active reference**로 "show/store 실행 중에는 노드 제거를 기다리게" 해서, 속성 제거와 동시 접근을 안전하게 만든다.
2. **sysfs** (`fs/sysfs/file.c`): kernfs 노드를 kobject와 `struct attribute`에 연결한다. `sysfs_kf_seq_show()`는 PAGE_SIZE 버퍼를 0으로 채운 뒤 `kobj->ktype->sysfs_ops->show`를 부르고, 결과가 PAGE_SIZE 이상이면 경고 후 자른다. 쓰기는 `sysfs_kf_write()` → `ops->store`.
3. **driver core** (`drivers/base/core.c`): device kobject의 `sysfs_ops`가 `dev_sysfs_ops = { dev_attr_show, dev_attr_store }`다. `container_of`로 `struct attribute` → `struct device_attribute`를 얻어 우리 `odr_show/odr_store`를 부른다.
4. binary attribute는 `sysfs_kf_bin_read/write`(크기 클리핑, §6.3), configfs는 kernfs가 아닌 자체 구현(`fs/configfs/`), debugfs도 자체 구현(`fs/debugfs/`)이다. debugfs는 파일 연산 앞뒤로 lockdown 검사와 "파일 제거 중이면 대기" 보호(`debugfs_file_get/put`)를 끼운다.

**드라이버 작성 때 다시 만나는 곳**

- G2(디바이스 모델): `kobject` → kernfs 노드 → `/sys` 디렉터리. `device_add()` 순서가 §8의 근거.
- G3(platform driver): `.dev_groups`, devm 자원과 속성 수명.
- G5(ioctl과 mmap): §11.1 결정표와 §12 측정.
- G6(poll): sysfs poll은 kernfs가 `.poll`을 대신 구현한 것. 드라이버 `.poll` + `poll_wait`와 같은 대기 큐 패턴.
- G9(udev): `bind`/`add` 규칙이 속성을 읽는 시점.
- 모듈 I(IIO), hwmon, LED, power_supply 같은 **서브시스템**은 표준 sysfs 이름을 만들어 준다 — 커스텀 속성은 마지막 수단.

---

## 14. Embedded 실무에서 다시 보기

### 14.1 센서 키트 bring-up 첫 주

새 보드에 IMU를 올렸는데 데이터가 이상하다. 순서:

```sh
cat /sys/kernel/debug/g8_sensorkit/g8-sensorkit-a/regs     # 레지스터가 의도대로인가
echo 833 > /sys/bus/platform/devices/g8-sensorkit-a/odr      # 설정 변경
cat /sys/kernel/debug/g8_sensorkit/g8-sensorkit-a/regs     # CTRL1이 바뀌었나
cat /sys/kernel/debug/g8_sensorkit/g8-sensorkit-a/stats    # 에러 카운트는?
```

JTAG를 붙이지 않고도 "sysfs 의도 → 레지스터 실제값"을 대조할 수 있다. 실제 I2C/SPI 센서라면 regmap(모듈 H)이 `/sys/kernel/debug/regmap/<dev>/registers`를 **공짜로** 만들어 주므로, regset32는 MMIO 블록(SoC 내장 IP)에서 주로 쓴다. 이 regs 덤프는 현장 장애 시 고객 지원 스크립트가 수집하는 항목이 되기도 한다 — 단, debugfs가 꺼진 양산 이미지에서는 동작하지 않으니 별도 경로(예: devcoredump)가 필요하다.

### 14.2 보정값 저장 서비스

공장에서 측정한 IMU 보정 32바이트를 `/var/lib/sensorkit/cal.bin`에 저장하고, 부팅 때 systemd 서비스가 넣는다.

```sh
# boot-time restore (not executed in this note)
cat /var/lib/sensorkit/cal.bin > /sys/bus/platform/devices/g8-sensorkit-a/calibration
```

binary attribute라 바이트 그대로, 크기 제한은 커널이 검사(§6.3). EEPROM에 보정값이 있다면 `at24` 드라이버의 `eeprom`(또는 nvmem) binary 파일에서 읽어 올 수 있다.

### 14.3 과열 감시 데몬

로봇 메인 컴퓨터의 감시 데몬이 `alarm` 파일을 `poll(POLLPRI)`로 기다리다(예제 8) 알람이 오면 모터 출력을 제한한다. CPU 소모 0, 반응 지연은 드라이버 샘플링 주기만큼. 여러 센서면 fd 여러 개를 `epoll`에 `EPOLLPRI`로 등록한다 (A 모듈 epoll 노트와 같은 패턴). 실제 온도 센서는 hwmon 서브시스템의 `temp1_input`/`temp1_max_alarm`을 쓰면 표준 도구(`sensors`)도 그대로 동작한다.

### 14.4 USB로 PC에 연결되는 컨트롤러

드론 비행 컨트롤러 컴패니언 보드가 USB로 정비용 PC에 꽂히면 "시리얼 콘솔 + 네트워크"로 보이게 하고 싶다. 부팅 스크립트가 configfs로 gadget을 조립한다(예제 13 + `functions/ncm.usb0` + `echo <udc> > UDC`). 이 스크립트는 제품 펌웨어의 일부이므로 configfs가 ABI여야 하는 이유가 여기 있다.

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| probe에서 `sysfs_create_group()` 후 remove에서 안 지움 | rmmod 후 재 insmod 시 `sysfs: cannot create duplicate filename` | 수동 생성·제거 짝 불일치 | `.driver.dev_groups` 사용 |
| `device_create()` 후 `sysfs_create_group()` | udev 규칙이 가끔 속성을 못 읽음 | 속성이 KOBJ_ADD 뒤에 생김 (예제 9) | `device_create_with_groups()`, `dev->groups` |
| store가 에러를 삼키고 `count` 반환 | 잘못된 값을 써도 `rc=0`, 설정은 안 바뀜 | 에러 미전달 | 음수 errno 반환 (예제 4) |
| store가 0 반환 | `echo`가 멈추거나 반복 호출 | "0바이트 소비" 의미 | 성공 시 `count` 반환 |
| `strcmp(buf, "8g")` | `echo 8g`가 항상 실패 | `echo`가 붙인 `\n` | `sysfs_streq()`, `sysfs_match_string()` |
| show에서 `sprintf`, 이어 쓰기에 `buf + len` | 긴 출력에서 메모리 오염 / WARN | 크기·정렬 무시 (예제 3) | `sysfs_emit`, `sysfs_emit_at` |
| 한 파일에 여러 값 (`"x=1 y=2 z=3"`) | 리뷰 거절, 필드 추가 시 유저 깨짐 | 값 하나 규칙 위반 | 파일 여러 개 또는 group 하위 디렉터리 |
| 제품 데몬이 debugfs 파일을 읽음 | 양산 이미지·커널 업데이트에서 고장 | debugfs는 ABI 아님, lockdown | sysfs/서브시스템 인터페이스로 승격 |
| debugfs 반환값 검사 후 probe 실패 처리 | debugfs 꺼진 커널에서 드라이버 전체가 안 뜸 | 관례 위반 | 반환값 무시 |
| hard IRQ에서 `sysfs_notify()` | "sleeping function called from invalid context" | 내부에서 잠들 수 있는 락 | threaded IRQ/workqueue, 또는 미리 잡은 dirent + `sysfs_notify_dirent()` |

---

## 16. 면접에서 이렇게 말한다

**Q.** What's the difference between sysfs, debugfs, and configfs? When would you use each?

**A.** sysfs는 커널이 만든 객체(장치·드라이버)의 속성을 값 하나당 파일 하나로 보여 주고, 문서화된 ABI라 유지 의무가 있다. debugfs는 ABI가 없는 개발자용 창문이라 레지스터 덤프, 통계, 결함 주입에 쓰고 양산에서 꺼질 수 있다. configfs는 반대로 유저가 mkdir로 커널 객체를 만들고 설정하는 곳이고 USB gadget, nvmet이 대표다.

> sysfs exposes attributes of kernel-created objects — one value per file, text, and it's a stable ABI that must be documented under Documentation/ABI. debugfs is a no-rules debug interface with no ABI guarantee: register dumps, stats, fault injection; it may be disabled or locked down in production, so nothing in the product should depend on it. configfs is the inverse of sysfs: userspace creates kernel objects with mkdir and configures them — USB gadget composition and the NVMe target are the classic users. And procfs is legacy for device stuff; new drivers shouldn't add files there.

**꼬리 질문**: "그럼 센서 데이터 스트림은?" → sysfs가 아니라 문자 디바이스 read/mmap 또는 IIO 버퍼. 값 하나 open/read/close가 실측으로 수십 배 비싸다 (§12).

**Q.** What are the rules for a sysfs attribute's show and store functions?

**A.** show는 페이지 정렬된 PAGE_SIZE 버퍼에 `sysfs_emit`으로 쓰고 길이를 돌려준다. store는 `kstrto*`나 `sysfs_match_string`으로 파싱·검증하고, 실패하면 음수 errno, 성공하면 `count`를 돌려준다. 공유 상태는 락으로 보호한다 — 여러 프로세스가 동시에 부를 수 있다.

> show gets a page-sized, page-aligned buffer; use sysfs_emit or sysfs_emit_at, never sprintf, and return the length. store gets a NUL-terminated copy of the user data, possibly with a trailing newline; parse it with kstrtouint or sysfs_match_string, validate against what the hardware supports, return -EINVAL or the parse error on failure — that shows up as a write error and non-zero exit code in the shell — and return count on success. Both run in process context concurrently, so take a lock around hardware state.

**Q.** Why should you use the driver's dev_groups instead of calling sysfs_create_group in probe?

**A.** 속성이 생기는 시점과 정리 때문이다. `.dev_groups`는 driver core가 probe 성공 후, KOBJ_BIND uevent 전에 만들고 unbind 때 자동으로 지운다. 수동 생성은 에러 경로·remove에서 빼먹기 쉽고, 장치를 직접 만드는 코드에서 device_add 뒤에 만들면 udev가 KOBJ_ADD를 받는 순간 파일이 없다. 미니 udev로 재현하면 잘못된 방법은 MISSING, 올바른 방법은 PRESENT가 나온다.

> Timing and lifetime. With driver.dev_groups the driver core creates the attributes right after probe succeeds and before the KOBJ_BIND uevent, and removes them automatically on unbind — no cleanup code. If you create a device yourself and add attributes after device_add, the KOBJ_ADD uevent has already gone out and udev may run its rules before the files exist — a race that only fails sometimes. For devices you create, pass the groups in: device_create_with_groups, dev->groups, or class->dev_groups. I reproduced it with a netlink uevent listener: attribute missing at the add event in the racy version, present in the correct one.

**Q.** How can userspace get notified when a sysfs value changes?

**A.** 드라이버가 값이 바뀔 때 `sysfs_notify(&dev->kobj, NULL, "attr")`를 부르고, 유저는 파일을 한 번 읽은 다음 `poll`을 `POLLPRI`로 기다린다. 깨어나면 `POLLERR|POLLPRI`가 오고, 오프셋 0부터 다시 읽는다. `sysfs_notify`는 잠들 수 있어서 hard IRQ에서 부르면 안 된다.

> The driver calls sysfs_notify on the attribute when the value changes. Userspace opens the file, reads it once to arm it, then polls with POLLPRI; on change poll returns POLLERR|POLLPRI and you pread from offset zero to get the new value. It only works for attributes whose driver actually calls sysfs_notify, so always use a timeout. sysfs_notify can sleep, so from an IRQ you either defer to a thread or work item, or cache the kernfs node and use sysfs_notify_dirent. For high-rate events I'd use a proper subsystem event interface like IIO events instead.

**Q.** How would you expose a register dump of your device for debugging?

**A.** debugfs의 `debugfs_create_regset32`를 쓴다. 이름·오프셋 표와 ioremap한 base만 주면 읽을 때마다 readl해서 `NAME = 0x%08x`로 출력한다. `regset.dev`를 채우면 runtime PM으로 장치를 깨워서 읽는다. I2C/SPI 장치라면 regmap이 debugfs에 registers 파일을 자동으로 만든다. 절대 sysfs에 두지 않는다 — ABI가 돼 버린다.

> debugfs, with debugfs_create_regset32: a table of name/offset pairs plus the ioremapped base, and every read does readl and prints NAME = 0x%08x. Set regset.dev and it takes a runtime PM reference so you don't read a powered-off block. For I2C or SPI devices regmap already gives you a registers file in debugfs. I'd never put a raw register dump in sysfs, because then the register layout becomes ABI. In bring-up this is how I compare what I wrote through the user interface with what's actually in the hardware.

**Q.** When would you use a binary sysfs attribute?

**A.** 커널이 해석하지 않는 바이트 덩어리일 때 — 보정 blob, EEPROM 내용, PCI config space. `BIN_ATTR_RW(name, size)`로 크기를 주면 sysfs가 offset·count를 잘라 주고, 크기를 넘는 쓰기는 `-EFBIG`가 된다. 텍스트 값을 바이너리로 내보내는 데 쓰면 안 된다.

> Only for opaque byte blobs the kernel doesn't interpret — calibration data, EEPROM contents, PCI config space, firmware-related blobs. You declare it with BIN_ATTR_RW or the ADMIN variant with a size, and sysfs clips offset and count for you; writes past the end get -EFBIG. Since 6.13-ish the callbacks and the group's bin_attrs take const struct bin_attribute pointers. It's not a way to dodge the one-value-per-file rule for normal settings.

**Q.** Can you add a /proc file for your driver's status?

**A.** 새 드라이버에는 안 된다. procfs는 프로세스 정보용이고 장치 관련 파일은 레거시로 유지만 한다. 상태는 sysfs, 디버그 정보는 debugfs로 간다.

> Not in a new driver — it would be rejected upstream. procfs is meant for process information; the device-related files there are legacy kept for compatibility. Device state goes into sysfs, ideally via a subsystem's standard attributes, and anything debug-only goes into debugfs.

---

## 17. 직접 해보기

1. (개념) `odr_store`가 `count` 대신 `count - 1`을 돌려주면 `echo 416 > odr`에서 무슨 일이 생길까? 정답/힌트: `write()`가 덜 쓴 것으로 보고 남은 1바이트(`\n`)로 store를 다시 부른다 → `kstrtouint("\n")`이 `-EINVAL` → 셸은 실패로 본다. 직접 바꿔서 확인해 보자.
2. (코드) `range_available` 속성을 `sysfs_emit_at`으로 추가하고 `ATTRIBUTE_GROUPS` 대신 이름 있는 group(`.name = "config"`)으로 옮겨 하위 디렉터리를 만들어 보라. 정답/힌트: `/sys/.../g8-sensorkit-a/config/odr`처럼 경로가 바뀐다 — 경로도 ABI라는 점을 함께 생각하자.
3. (코드) `is_visible`에서 0 대신 `0444`를 돌려 장치 `b`의 `range`를 읽기 전용으로 만들어 보라. 정답/힌트: `ls -l`에서 모드가 바뀌고, root도 쓰기가 `Permission denied`.
4. (관찰) 예제 8의 poller를 `alarm` 대신 `odr`에 걸고 `echo 52 > odr`을 해 보라. 무슨 일이 생기나? 정답/힌트: `odr_store`는 `sysfs_notify`를 부르지 않으므로 poller는 타임아웃(10 s)까지 깨어나지 않는다 — §7 규칙 4.
5. (코드) debugfs에 `debugfs_create_bool("force_alarm", ...)`을 추가하고 work에서 그 값이 참이면 알람을 강제로 켜라. 정답/힌트: 결함 주입 훅의 전형. sysfs가 아닌 debugfs에 두는 이유를 한 문장으로 적어 보자.
6. (관찰) 예제 9의 `g8_uspy`를 `odr` 대상으로 띄워 두고 `insmod`/`rmmod g8_sensorkit`을 해 보라. `bind` 이벤트가 몇 개, 각각 PRESENT인가? 정답/힌트: 장치 `a`, `b` 각각 bind 1개씩, 둘 다 PRESENT. misc 장치 add도 함께 보인다.
7. (configfs) `g8_cfs`의 채널에 `enable`이 1인 동안 `axis`를 바꾸지 못하게(`-EBUSY`) 만들어 보라. 정답/힌트: `g8_chan_axis_store`에서 `to_chan(i)->enable`을 검사. USB gadget이 `UDC`에 바인딩된 동안 설정 변경을 막는 것과 같은 패턴이다.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| sysfs | `/sys` 가상 FS | kobject 트리를 디렉터리로, 속성을 파일로 보여 줌. ABI |
| kernfs | 커널 객체 FS 엔진 | sysfs·cgroupfs가 공유하는 노드·열린 파일·poll 구현 |
| attribute | 속성 | sysfs 파일 하나. `struct attribute` (이름 + 모드) |
| `device_attribute` | 장치 속성 | attribute + `show`/`store` 콜백 |
| attribute group | 속성 묶음 | 속성 배열 + `is_visible` + 선택적 하위 디렉터리 이름 |
| `is_visible` | 가시성 콜백 | 속성별로 0(숨김) 또는 모드를 돌려줌 |
| binary attribute | 바이너리 속성 | 크기 있는 바이트 blob 파일, offset/count 자동 클리핑 |
| `.dev_groups` | 드라이버 속성 그룹 | probe 성공 후 driver core가 만들고 unbind 때 지움 |
| uevent | 커널 이벤트 | add/remove/bind/unbind 등을 netlink로 udev에 알림 |
| `sysfs_emit` | show용 출력 함수 | 페이지 정렬 검사 + PAGE_SIZE 경계 보장 |
| `sysfs_notify` | 변화 통지 | 해당 속성을 poll하는 프로세스를 깨움 (POLLPRI) |
| ABI | Application Binary Interface | 유저가 의존하는 인터페이스. 바꾸면 안 됨 |
| debugfs | `/sys/kernel/debug` | 규칙·ABI 없는 디버그 FS, root 전용, lockdown 대상 |
| regset32 | 레지스터 집합 | `{이름, 오프셋}` 표 + base → 레지스터 덤프 파일 |
| seq_file | 순차 출력 헬퍼 | 버퍼 크기를 신경 쓰지 않고 `seq_printf`로 출력 |
| configfs | `/sys/kernel/config` | 유저가 mkdir로 커널 객체를 만들고 설정하는 FS |
| config_item | configfs 객체 | mkdir 하나 = item 하나, refcount로 수명 관리 |
| lockdown | 커널 잠금 LSM | Secure Boot 환경에서 debugfs 등 커널 내부 접근 차단 |

---

## 19. 요약 & 체크리스트

드라이버가 유저에게 무언가를 보여 주는 자리는 네 곳이다. **sysfs**는 커널이 만든 객체의 속성을 "값 하나 = 텍스트 파일 하나"로 보여 주는 ABI이고, 드라이버는 `DEVICE_ATTR_RW/RO/WO` + `sysfs_emit`으로 속성을 만들어 `.driver.dev_groups`에 넘긴다. 그러면 driver core가 probe 성공 후 `KOBJ_BIND` 전에 만들고 unbind 때 지운다. 변형별 속성은 `is_visible`, 바이트 blob은 binary attribute, 변화 통지는 `sysfs_notify` + `poll(POLLPRI)`. **debugfs**는 ABI 없는 개발자 도구라 레지스터 덤프(`debugfs_create_regset32`), knob, seq_file 통계를 마음껏 두되, 제품은 절대 의존하지 않는다. **configfs**는 유저가 mkdir로 객체를 만드는 곳(USB gadget, nvmet)이고, **procfs**에는 새 장치 파일을 만들지 않는다. 그리고 sysfs는 설정·상태용이지 데이터 경로가 아니다 — 값 하나 open/read/close는 `ioctl`의 10배 비용이었다.

- [ ] sysfs, debugfs, configfs, procfs의 차이를 "누가 객체를 만드나 / ABI인가"로 설명할 수 있다
- [ ] sysfs "값 하나 = 파일 하나" 규칙과 `Documentation/ABI` 항목 형식을 말할 수 있다
- [ ] `DEVICE_ATTR_RW` + `odr_show`/`odr_store`를 검증·락·에러 반환까지 포함해 코드로 짤 수 있다
- [ ] `sysfs_emit`/`sysfs_emit_at`이 `sprintf`/`snprintf`보다 안전한 이유를 설명할 수 있다
- [ ] store의 음수 errno가 셸에서 `write error` + 종료 코드 1로 보인다는 것을 보여 줄 수 있다
- [ ] attribute group, `is_visible`, `__ATTRIBUTE_GROUPS`로 변형별 속성을 숨길 수 있다
- [ ] `BIN_ATTR_RW`로 크기 있는 binary attribute를 만들고 `-EFBIG` 동작을 설명할 수 있다
- [ ] `sysfs_notify` + `poll(POLLPRI)` 유저 프로그램을 짜고, IRQ 컨텍스트 제약을 말할 수 있다
- [ ] `.dev_groups`와 `device_create_with_groups`가 uevent 경쟁을 막는 이유를 driver core 순서로 설명할 수 있다
- [ ] debugfs에 regset32, u32 knob, `DEFINE_SHOW_ATTRIBUTE` seq_file을 붙이고, 반환값을 검사하지 않는 이유를 말할 수 있다
- [ ] configfs `make_item`/`drop_item`/`release` 생명주기와 USB gadget 예를 설명할 수 있다
- [ ] 값 하나를 읽는 비용(sysfs open/read/close vs pread vs read vs ioctl)을 측정해 인터페이스 선택 근거로 쓸 수 있다

---

## 참고 자료

- docs.kernel.org — sysfs: https://docs.kernel.org/filesystems/sysfs.html
- docs.kernel.org — "Rules on how to access information in sysfs": https://docs.kernel.org/admin-guide/sysfs-rules.html
- docs.kernel.org — ABI 문서: https://docs.kernel.org/admin-guide/abi.html (소스는 `Documentation/ABI/`)
- docs.kernel.org — debugfs: https://docs.kernel.org/filesystems/debugfs.html
- docs.kernel.org — configfs: https://docs.kernel.org/filesystems/configfs.html
- docs.kernel.org — USB gadget configfs: https://docs.kernel.org/usb/gadget_configfs.html
- docs.kernel.org — Driver Model: https://docs.kernel.org/driver-api/driver-model/index.html
- 랩 소스(6.18.55): `fs/sysfs/file.c`(`sysfs_emit`, `sysfs_notify`, `sysfs_kf_*`), `fs/kernfs/file.c`(poll), `drivers/base/core.c`(`device_add`, `dev_attr_show`), `drivers/base/dd.c`(`really_probe`의 `dev_groups`), `fs/debugfs/file.c`(`debugfs_print_regs32`), `include/linux/sysfs.h`, `include/linux/configfs.h`, `samples/configfs/configfs_sample.c`
- elixir.bootlin.com에서 위 파일 검색: https://elixir.bootlin.com/linux/latest/source
- Michael Kerrisk, "The Linux Programming Interface" — 63장(poll, select), 12장(/proc)
- Bootlin, "Linux kernel and driver development training" 슬라이드 — Device Model / sysfs 부분: https://bootlin.com/training/kernel/
