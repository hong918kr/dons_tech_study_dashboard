# I1. 프레임워크 원칙 — 직접 char device를 만들지 말고 서브시스템에 등록하라, ops 구조체 패턴

> **이 노트를 다 읽으면**: Linux 서브시스템(프레임워크)의 네 부품 — core, ops 구조체, 등록 API, class/sysfs + 표준 uAPI — 을 그림 한 장으로 설명할 수 있다 · RTC·LED·input·watchdog 네 프레임워크의 "할당 → ops 채우기 → 등록 → core가 콜백" 흐름을 6.18 소스 줄 번호와 함께 비교할 수 있다 · "core가 내 op를 어떤 context에서 부르나"를 알고 `brightness_set`에서 잠들면 커널이 죽는 이유를 실측으로 보여 줄 수 있다 · 같은 온도계를 misc+ioctl과 hwmon으로 두 번 짜 보고 "왜 char device를 직접 만들면 안 되나"를 숫자로 답할 수 있다
> **왜 필요한가**: 드라이버 설계 면접의 단골 질문이 "Why not just write a char device with an ioctl?"이고, 실무에서 첫 패치를 올렸을 때 메인테이너가 가장 먼저 다는 리뷰 코멘트도 "This should be an IIO/hwmon/LED driver"다
> **Don 기준 난이도**: 함수 포인터 테이블로 HAL을 추상화하는 것(CMSIS-Driver의 `ARM_DRIVER_USART`, 펌웨어의 NAND/PHY 드라이버 vtable)은 이미 안다 / 새로 배울 부분은 Linux core가 그 vtable 위에 **sysfs·uevent·PM·락·표준 툴**까지 묶어 준다는 점, 그리고 **core가 정한 호출 context 규칙**
> **선행 노트**: G1(드라이버 종류와 프레임워크 결정표), G2(디바이스 모델), G3(platform driver), G4(char device), G7(misc device), G8(sysfs) — 이후 I2(IIO), I3(input), I4(hwmon), I9(LED·RTC·watchdog)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

G1에서 "드라이버 대부분은 char/block/net 위에 올라간 **서브시스템 드라이버**"라는 결론과, "내 장치는 어느 프레임워크?" 결정표를 이미 봤다. G4에서는 `cdev`와 `file_operations`로 char device를 **직접** 만드는 법을 배웠다. 이 노트는 그 사이의 질문에 답한다.

- 프레임워크는 **어떻게 설계되어 있나** — 모든 프레임워크에 공통인 뼈대는 무엇인가.
- 드라이버는 프레임워크에 **무엇을 넘기고 무엇을 돌려받나**.
- core는 내 콜백을 **언제, 어떤 context에서, 어떤 락을 잡고** 부르나.
- 그리고 G4처럼 직접 만든 char device가 왜 대부분의 경우 **틀린 답**인가.

Don의 펌웨어 경험으로 번역하면 이렇다. SSD 펌웨어에서 NAND 벤더가 바뀌면 `nand_ops` 같은 함수 포인터 테이블만 새로 채우고, FTL·에러 처리·스케줄러는 그대로 썼을 것이다. ARM의 CMSIS-Driver도 같다. `ARM_DRIVER_USART`는 `Initialize`, `Send`, `Receive`, `Control` 같은 함수 포인터를 담은 구조체이고, 미들웨어는 칩이 무엇이든 그 구조체만 부른다. Linux 프레임워크는 **이 vtable 패턴에 운영체제 기능을 덧붙인 것**이다. 드라이버가 `rtc_class_ops` 같은 테이블을 채워 등록하면, core는

1. `/dev/rtcN` char device와 `/sys/class/rtc/rtcN/` sysfs 디렉터리를 만들고,
2. uevent를 보내 udev/mdev가 노드를 만들게 하고,
3. 유저가 `ioctl(RTC_RD_TIME)`을 하면 **자기 mutex를 잡은 뒤** 내 `read_time`을 부르고,
4. suspend/resume 때 알아서 시간 보정을 하고,
5. `hwclock` 같은 **이미 존재하는 유저 툴**이 내 칩에서 바로 돌게 해 준다.

드라이버가 쓰는 코드는 "레지스터를 읽어 `struct rtc_time`을 채우는 함수" 하나뿐이다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g font-size="12">
    <text x="115" y="24" text-anchor="middle" font-size="13">드라이버 (내가 쓰는 것)</text>
    <text x="340" y="24" text-anchor="middle" font-size="13">프레임워크 core</text>
    <text x="567" y="24" text-anchor="middle" font-size="13">유저 공간</text>
    <rect x="10" y="40" width="210" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="115" y="62" text-anchor="middle">① 할당</text><text x="115" y="82" text-anchor="middle">devm_*_allocate_device()</text>
    <rect x="10" y="108" width="210" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="115" y="130" text-anchor="middle">② ops · capability 채우기</text><text x="115" y="150" text-anchor="middle">rtc->ops = &amp;my_ops</text>
    <rect x="10" y="176" width="210" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="115" y="198" text-anchor="middle">③ 등록</text><text x="115" y="218" text-anchor="middle">devm_*_register_device()</text>
    <rect x="10" y="244" width="210" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="115" y="266" text-anchor="middle">④ 콜백 구현 (칩 고유 부분)</text><text x="115" y="286" text-anchor="middle">read_time() · ping() …</text>
    <rect x="245" y="40" width="190" height="260" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="340" y="62" text-anchor="middle">등록 때 core가 하는 일</text>
    <text x="340" y="88" text-anchor="middle">class 장치 + sysfs 속성</text>
    <text x="340" y="112" text-anchor="middle">char dev (cdev) 생성</text>
    <text x="340" y="136" text-anchor="middle">uevent → udev / mdev</text>
    <text x="340" y="166" text-anchor="middle">호출 때 core가 하는 일</text>
    <text x="340" y="192" text-anchor="middle">락 (ops_lock, hwdev->lock)</text>
    <text x="340" y="216" text-anchor="middle">입력 검증 · 포맷 변환</text>
    <text x="340" y="240" text-anchor="middle">PM: suspend / resume 훅</text>
    <text x="340" y="264" text-anchor="middle">타이머 · 버퍼 · 트리거</text>
    <text x="340" y="288" text-anchor="middle">tracepoint</text>
    <rect x="465" y="40" width="205" height="260" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="567" y="62" text-anchor="middle">표준 uAPI</text>
    <text x="567" y="86" text-anchor="middle">/dev/rtcN + RTC_RD_TIME</text>
    <text x="567" y="108" text-anchor="middle">…/hwmonN/temp1_input</text>
    <text x="567" y="130" text-anchor="middle">/dev/input/eventN</text>
    <text x="567" y="152" text-anchor="middle">/dev/watchdogN + WDIOC_*</text>
    <text x="567" y="186" text-anchor="middle">표준 툴 (그대로 동작)</text>
    <text x="567" y="210" text-anchor="middle">hwclock · evtest · sensors</text>
    <text x="567" y="234" text-anchor="middle">v4l2-ctl · ip · candump</text>
    <text x="567" y="258" text-anchor="middle">iio_readdev · aplay</text>
    <text x="567" y="282" text-anchor="middle">modetest · watchdog 데몬</text>
  </g>
  <g stroke="currentColor" fill="none">
    <line x1="115" y1="96" x2="115" y2="106" marker-end="url(#ah1)"/>
    <line x1="115" y1="164" x2="115" y2="174" marker-end="url(#ah1)"/>
    <line x1="220" y1="204" x2="243" y2="204" marker-end="url(#ah1)"/>
    <line x1="435" y1="120" x2="463" y2="120" marker-end="url(#ah1)"/>
    <line x1="463" y1="200" x2="437" y2="200" marker-end="url(#ah1)"/>
    <line x1="243" y1="272" x2="222" y2="272" marker-end="url(#ah1)" stroke-dasharray="4 3"/>
  </g>
  <g font-size="12">
    <rect x="10" y="322" width="660" height="84" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="340" y="346" text-anchor="middle">실선 →: 등록(드라이버 → core), uAPI 노출(core → 유저), syscall(유저 → core)</text>
    <text x="340" y="370" text-anchor="middle">점선 →: core가 자기 락을 잡은 채 내 콜백을 부른다 (제어의 역전, "Don't call us, we'll call you")</text>
    <text x="340" y="394" text-anchor="middle">펌웨어 비유: CMSIS-Driver 구조체 = ②, 미들웨어 = core. Linux는 여기에 sysfs·uevent·PM·락을 얹었다</text>
  </g>
</svg>
```
그림 1 — 프레임워크의 해부. 드라이버는 왼쪽 네 칸만 쓴다. 가운데 core와 오른쪽 uAPI·툴은 커널과 생태계가 이미 갖고 있다.

### 0.1 이 노트의 실습 환경

- VM 호스트: Lima `elinux` (`uname -r` = `7.0.0-38-generic`). 모듈은 여기서 빌드만 한다.
- 게스트: 공용 랩의 **서브시스템 커널** `/var/lab/out/Image-sub` (`6.18.55-sub`). 일반 랩 config에 가상·테스트 드라이버(`rtc-test`, `softdog`, `ledtrig-heartbeat`, `vivid`, `vcan` …)를 모듈로 더한 커널이고, 설치된 모듈은 `/var/lab/out/modules-sub/lib/modules/6.18.55-sub`에 있다. 우리 모듈은 `make -C /var/lab/out/linux-build-sub M=$PWD`로 빌드한다(vermagic `6.18.55-sub`).
- 이 커널의 관련 config(실제 `.config`에서 확인): `CONFIG_RTC_CLASS=y`, `CONFIG_RTC_DRV_PL031=y`, `CONFIG_RTC_DRV_TEST=m`, `CONFIG_LEDS_CLASS=y`, `CONFIG_LEDS_TRIGGER_TIMER=y`, `CONFIG_LEDS_TRIGGER_HEARTBEAT=m`, `CONFIG_HWMON=y`, `CONFIG_WATCHDOG_CORE=y`, `CONFIG_SOFT_WATCHDOG=m`, `CONFIG_INPUT_EVDEV=y`, `CONFIG_PM_SLEEP=y`. `CONFIG_WATCHDOG_SYSFS`는 **꺼져 있다**(§7에서 이 때문에 sysfs 대신 ioctl로 본다).

이 노트에서 만드는 모듈 네 개는 모두 같은 platform 뼈대를 쓴다. "가짜 장치"를 `platform_device_register_simple()`로 하나 만들고, 같은 이름의 `platform_driver`가 probe 되게 하는 G3의 패턴이다. 실제 보드라면 이 부분은 Device Tree 노드가 대신한다. 뼈대는 한 번만 보이고, 이후 코드에서는 "공통 뼈대"라고만 적는다.

```c
/* 공통 뼈대 — 네 모듈 모두 이름(XXX)만 다르고 똑같다 (실제 i1_rtc.c 뒷부분) */
static struct platform_driver i1_rtc_drv = {
	.probe = i1_rtc_probe,
	.driver = { .name = "i1_rtc" },
};
static struct platform_device *pdev;

static int __init i1_init(void)
{
	int ret = platform_driver_register(&i1_rtc_drv);

	if (ret)
		return ret;
	pdev = platform_device_register_simple("i1_rtc", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&i1_rtc_drv);
		return PTR_ERR(pdev);
	}
	return 0;
}

static void __exit i1_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&i1_rtc_drv);
}
module_init(i1_init);
module_exit(i1_exit);
MODULE_LICENSE("GPL");
```

게스트 부팅 스크립트(`boot.sh`, VM에서 실행). 랩 initramfs를 복사하고, 우리 `.ko`·유저 프로그램과 함께 **modules-sub에서 필요한 모듈 세 개만**(`rtc-test`, `ledtrig-heartbeat`, `softdog`) `modules.dep`와 같이 넣어 BusyBox `modprobe`가 동작하게 한다. modules-sub 전체는 86 MB라 512 MB 게스트의 initramfs로는 무겁다.

```sh
#!/bin/bash
# usage: [KIMG=Image-sub] boot.sh <guest-script.sh> -> boots lab kernel with /i1/* + selected modules-sub, runs script
set -e
G=$1; N=$(basename $G .sh); W=/var/tmp/I1/run_$N; K=${KIMG:-Image-sub}
MS=/var/lab/out/modules-sub/lib/modules/6.18.55-sub
rm -rf $W && mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
mkdir -p $W/r/i1 $W/r/lib/modules/6.18.55-sub
cp $MS/modules.* $W/r/lib/modules/6.18.55-sub/
(cd $MS && for k in drivers/rtc/rtc-test.ko drivers/leds/trigger/ledtrig-heartbeat.ko drivers/watchdog/softdog.ko; do
   f=$(ls kernel/$k* 2>/dev/null | head -1); mkdir -p $W/r/lib/modules/6.18.55-sub/$(dirname $f); cp $f $W/r/lib/modules/6.18.55-sub/$f; done)
cp /var/tmp/I1/mod/*.ko /var/tmp/I1/bin/* $W/r/i1/
cp /Users/donh/.elinux_scratch/I1/$G $W/r/i1/t.sh; cp /Users/donh/.elinux_scratch/I1/s3_led.sh $W/r/i1/s3.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-120} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic -no-reboot \
  -kernel /var/lab/out/$K -initrd $W/i.gz \
  -append "console=ttyAMA0 quiet ${EXTRA} LAB_CMD=sh~/i1/t.sh" > $W/out.txt 2>&1 || echo "qemu rc=$?"
tr -d "\r" < $W/out.txt | sed "s/\x1b\[[0-9;]*m//g" > /var/tmp/I1/out_$N.txt
```

#### 예제 1 — 네 모듈과 유저 프로그램 빌드

무엇을 확인하나: 이 노트의 모듈 네 개(`i1_thermo_misc`, `i1_thermo_hwmon`, `i1_rtc`, `i1_led`)가 서브시스템 커널 빌드 트리로 경고 없이 빌드되는지, 유저 프로그램 세 개(`thermo_cli`, `uev`, `wdt_info`)가 `-Wall -Wextra`로 경고 0개인지.

```make
obj-m += i1_thermo_misc.o i1_thermo_hwmon.o i1_rtc.o i1_led.o
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/I1/mod && make -j2 -C /var/lab/out/linux-build-sub M=$PWD modules \
  && for m in i1_*.ko; do modinfo $m | grep -E "^(name|vermagic|parm)"; done \
  && cd /var/tmp/I1/bin && for p in thermo_cli uev wdt_info; do \
     gcc -std=gnu11 -Wall -Wextra -O2 -static -I/Users/donh/.elinux_scratch/I1 /Users/donh/.elinux_scratch/I1/$p.c -o $p; done'
```

```text
  CC [M]  i1_thermo_misc.o
  CC [M]  i1_thermo_hwmon.o
  CC [M]  i1_rtc.o
  CC [M]  i1_led.o
  MODPOST Module.symvers
  CC [M]  i1_thermo_misc.mod.o
  CC [M]  .module-common.o
  CC [M]  i1_thermo_hwmon.mod.o
  CC [M]  i1_rtc.mod.o
  CC [M]  i1_led.mod.o
  LD [M]  i1_thermo_misc.ko
  LD [M]  i1_thermo_hwmon.ko
  LD [M]  i1_rtc.ko
  LD [M]  i1_led.ko
name:           i1_led
vermagic:       6.18.55-sub SMP preempt mod_unload aarch64
parm:           blocking:bool
parm:           sleepbug:bool
name:           i1_rtc
vermagic:       6.18.55-sub SMP preempt mod_unload aarch64
name:           i1_thermo_hwmon
vermagic:       6.18.55-sub SMP preempt mod_unload aarch64
name:           i1_thermo_misc
vermagic:       6.18.55-sub SMP preempt mod_unload aarch64
```

출력에서 볼 것: 경고 0개(유저 프로그램 gcc도 아무것도 출력하지 않았다). vermagic이 `6.18.55-sub`이므로 **`Image-sub`로만** 부팅해야 한다. 기본 `Image`(`6.18.55`)에 넣으면 `insmod`가 vermagic 불일치로 거부한다. (처음 빌드 때는 `i1_rtc`, `i1_led`에 `MODULE_DESCRIPTION()`이 없어서 modpost가 `WARNING: modpost: missing MODULE_DESCRIPTION()`을 냈다. 6.x modpost는 이것도 경고로 친다. 추가한 뒤의 클린 빌드 출력이다.)

---

## 1. 프레임워크의 해부 — 네 부품

### 1.1 직관 — "드라이버는 플러그인, core가 프로그램"

G4에서 만든 char device는 **드라이버가 프로그램**이었다. `open`부터 `read`·`ioctl`·락·`f_pos`·노드 이름·권한까지 드라이버가 전부 정했다. 프레임워크에서는 반대다. **core가 프로그램이고 드라이버는 플러그인**이다. 유저와 대화하는 쪽(syscall, sysfs, 이벤트 포맷)은 core가 독점하고, 드라이버는 "칩에 실제로 접근하는 함수"만 플러그인으로 꽂는다. 소프트웨어 공학 용어로 **제어의 역전**(inversion of control)이다.

### 1.2 정의 — 모든 프레임워크에 있는 네 부품

| 부품 | 무엇인가 | RTC 예 | LED 예 | 드라이버가 하는 일 |
|---|---|---|---|---|
| core | 서브시스템 공통 코드. 유저 인터페이스·락·PM·공통 로직 | `drivers/rtc/class.c`, `interface.c`, `dev.c`, `sysfs.c` | `drivers/leds/led-class.c`, `led-core.c`, `led-triggers.c` | 없음 (읽기만) |
| 장치 객체 | 프레임워크별 "장치" 구조체. 안에 `struct device`가 들어 있다 | `struct rtc_device` | `struct led_classdev` | 할당하거나 정적으로 선언 |
| ops 구조체 | core가 부를 콜백 테이블. 대부분 선택, 몇 개만 필수 | `struct rtc_class_ops` | `led_classdev` 안의 `brightness_set` 등 | 칩 고유 함수로 채운다 |
| 등록 API | 장치 객체를 core에 넘기고 uAPI를 공개하는 함수 | `devm_rtc_register_device()` | `devm_led_classdev_register()` | probe 마지막에 부른다 |
| class + uAPI | `/sys/class/<이름>/`, `/dev/` 노드, ioctl·이벤트 포맷 | `/sys/class/rtc/rtcN`, `/dev/rtcN`, `RTC_RD_TIME` | `/sys/class/leds/NAME/brightness`, `trigger` | 없음 (core가 만든다) |

표의 "드라이버가 하는 일" 열이 핵심이다. 드라이버는 위에서 두 번째·세 번째·네 번째 행만 손댄다. 그리고 그것도 **정해진 순서**가 있다.

### 1.3 드라이버의 생애 — 할당 → 채우기 → 등록 → 콜백

모든 프레임워크 드라이버의 probe는 거의 같은 모양이다.

```c
static int foo_probe(struct platform_device *pdev)   /* 또는 i2c/spi probe */
{
	struct foo_dev *obj;

	/* ① 할당: 프레임워크 객체 (devm_ = probe 실패·remove 때 자동 해제) */
	obj = devm_foo_allocate_device(&pdev->dev);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	/* ② 채우기: ops 테이블과 capability (core가 이것을 보고 uAPI 모양을 정한다) */
	obj->ops = &foo_ops;
	obj->range_max = U32_MAX;          /* 예: RTC가 표현할 수 있는 범위 */

	/* ③ 등록: 이 순간부터 유저가 볼 수 있고, core가 콜백을 부를 수 있다 */
	return devm_foo_register_device(obj);
}
/* ④ 이후로는 core가 foo_ops의 함수들을 부른다 — 드라이버가 먼저 부르지 않는다 */
```

(위 코드는 패턴을 보이는 의사 코드다. `foo_`라는 API는 없다. 실제 API 이름은 §2 표에 있다.)

세 가지 규칙이 따라 나온다.

1. **등록은 맨 마지막.** ③이 끝나는 순간 유저 공간이 `/dev/rtcN`을 열 수 있고 core가 ④를 부를 수 있다. 그 전에 하드웨어 초기화·IRQ 등록·private data 설정이 끝나 있어야 한다. G4의 "준비 다 하고 마지막에 공개(`cdev_add`)"와 같은 원칙이다.
2. **ops 구조체는 `const`이고 정적이다.** 장치가 여러 개여도 ops는 하나를 공유하고, 장치별 상태는 `dev_get_drvdata()`나 `container_of()`로 찾는다. RTC ops는 `struct device *`(부모 장치)를 받으므로 `dev_get_drvdata(dev)`로, LED ops는 `struct led_classdev *`를 받으므로 `container_of(cdev, struct my_led, cdev)`로 찾는다.
3. **NULL인 op = 그 기능 없음.** core는 op가 NULL이면 그 기능을 끄거나 uAPI에서 숨긴다. 예를 들어 RTC core는 등록 시 `set_alarm`이 없으면 알람 기능 비트를 지운다(`drivers/rtc/class.c:403`의 `clear_bit(RTC_FEATURE_ALARM, rtc->features)`). 그러면 sysfs `wakealarm` 속성도 나타나지 않는다(§5의 실측).

### 1.4 core가 op를 부르는 모습 — `__rtc_read_time()` 소스 그대로

랩 소스 `drivers/rtc/interface.c:84-123` (6.18.55):

```c
static int __rtc_read_time(struct rtc_device *rtc, struct rtc_time *tm)
{
	int err;

	if (!rtc->ops) {
		err = -ENODEV;
	} else if (!rtc->ops->read_time) {
		err = -EINVAL;
	} else {
		memset(tm, 0, sizeof(struct rtc_time));
		err = rtc->ops->read_time(rtc->dev.parent, tm);
		if (err < 0) {
			dev_dbg(&rtc->dev, "read_time: fail to read: %d\n",
				err);
			return err;
		}

		rtc_add_offset(rtc, tm);

		err = rtc_valid_tm(tm);
		if (err < 0)
			dev_dbg(&rtc->dev, "read_time: rtc_time isn't valid\n");
	}
	return err;
}

int rtc_read_time(struct rtc_device *rtc, struct rtc_time *tm)
{
	int err;

	err = mutex_lock_interruptible(&rtc->ops_lock);
	if (err)
		return err;

	err = __rtc_read_time(rtc, tm);
	mutex_unlock(&rtc->ops_lock);

	trace_rtc_read_time(rtc_tm_to_time64(tm), err);
	return err;
}
```

드라이버의 `read_time` 한 줄 주위에서 core가 해 주는 일을 세어 보자: ops NULL 검사, 버퍼 0으로 초기화, **mutex로 직렬화**(드라이버는 `read_time`과 `set_time`이 동시에 불리지 않는다고 가정해도 된다), 오프셋 보정(`rtc_add_offset`), 결과 검증(`rtc_valid_tm`), tracepoint(`trace_rtc_read_time`). G4 방식이라면 이 전부를 드라이버마다 다시 썼을 것이다.

### 1.5 capability → uAPI: core는 "내가 채운 것"을 보고 모양을 정한다

프레임워크의 두 번째 설계 원칙은 **드라이버가 능력을 선언하면 core가 uAPI를 그에 맞춰 만든다**는 것이다.

| 프레임워크 | 드라이버가 선언하는 것 | core가 그것을 보고 하는 일 |
|---|---|---|
| RTC | `ops->set_alarm` 유무, `range_min/range_max`, `device_init_wakeup()` | `wakealarm`·`range`·`offset` sysfs 속성을 보이거나 숨김 (`drivers/rtc/sysfs.c`의 `rtc_attr_is_visible`) |
| hwmon | `HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT … )` 비트, `is_visible()` 반환 권한 | `temp1_input`, `temp1_max` 같은 파일을 만들고 권한(0444/0644)을 붙임 |
| input | `evbit`, `keybit`, `absbit` 비트맵 (`input_set_capability()`) | 지원 안 하는 이벤트는 `input_event()`에서 버림, `capabilities/` sysfs와 evdev ioctl `EVIOCGBIT`로 공개 |
| watchdog | `watchdog_info.options` (`WDIOF_SETTIMEOUT`, `WDIOF_MAGICCLOSE` …), `ops->get_timeleft` 유무 | `WDIOC_SETTIMEOUT`·`WDIOC_GETTIMELEFT` 허용 여부, magic close 동작 |
| IIO (I2) | `iio_chan_spec` 배열 (채널 타입, `info_mask`) | `in_accel_x_raw`, `in_accel_scale` 같은 속성 이름 자동 생성 |

펌웨어 비유: 디바이스가 디스크립터(USB descriptor, NVMe Identify 데이터)로 능력을 알리면 호스트 스택이 거기에 맞춰 동작하는 것과 같다. 여기서는 **드라이버가 디스크립터를 쓰고 core가 호스트 역할**을 한다.

### 1.6 예제 2 — 랩 커널 소스에서 프레임워크별 드라이버 수 세기

무엇을 확인하나: "직접 char device를 만들지 말라"가 실제 커널에서 어느 정도 지켜지는지. `drivers/` 아래 `.c` 파일 중 각 등록 API를 부르는 파일 수를 센다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55; for p in "devm_rtc_register_device|devm_rtc_device_register" "led_classdev_register" "input_register_device" "watchdog_register_device" "hwmon_device_register_with_info" "iio_device_register" "misc_register" "cdev_add|cdev_device_add"; do n=$(grep -rlE "$p" drivers --include=*.c | wc -l); printf "%-55s %5d\n" "$p" $n; done; grep -rlE "misc_register" drivers --include=*.c | cut -d/ -f2 | sort | uniq -c | sort -rn | head -6'
```

```text
devm_rtc_register_device|devm_rtc_device_register         182
led_classdev_register                                     201
input_register_device                                     416
watchdog_register_device                                  141
hwmon_device_register_with_info                           170
iio_device_register                                       486
misc_register                                             193
cdev_add|cdev_device_add                                   97
     49 watchdog
     25 char
     14 platform
     14 misc
      9 s390
      7 xen
```

출력에서 볼 것: 프레임워크 등록 API를 부르는 파일이 압도적으로 많다. 그리고 `cdev_add`를 부르는 97개 중 상당수는 **프레임워크 core 자신**이다(예: `drivers/rtc/class.c:418`의 `cdev_device_add`, `drivers/input/evdev.c:1390`). 재미있는 것은 `misc_register`의 최다 사용처가 `drivers/watchdog`(49개)라는 점이다. watchdog core가 생기기 전에 쓰인 **옛날 방식 드라이버**들이다. §2.5에서 이 둘을 비교해 "프레임워크가 없으면 무슨 일이 생기나"를 숫자로 본다. (`grep` 패턴 매칭이라 주석·매크로 정의에 걸린 파일도 몇 개 섞여 있다. 크기 감각용이다.)

---

## 2. 네 프레임워크를 나란히 — RTC, LED, input, watchdog

### 2.1 한 장으로 — 같은 네 단계, 다른 이름

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g font-size="12">
    <text x="158" y="30" text-anchor="middle" font-size="13">① 할당</text>
    <text x="306" y="30" text-anchor="middle" font-size="13">② 채우는 것</text>
    <text x="454" y="30" text-anchor="middle" font-size="13">③ 등록</text>
    <text x="602" y="30" text-anchor="middle" font-size="13">core가 만드는 것</text>
    <text x="44" y="88" text-anchor="middle" font-size="13">RTC</text>
    <text x="44" y="172" text-anchor="middle" font-size="13">LED</text>
    <text x="44" y="256" text-anchor="middle" font-size="13">input</text>
    <text x="44" y="340" text-anchor="middle" font-size="13">watchdog</text>
    <rect x="88" y="46" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="158" y="76" text-anchor="middle">devm_rtc_</text><text x="158" y="94" text-anchor="middle">allocate_device()</text>
    <rect x="236" y="46" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="306" y="68" text-anchor="middle">rtc_class_ops</text><text x="306" y="86" text-anchor="middle">read_time, set_time</text><text x="306" y="104" text-anchor="middle">range_max</text>
    <rect x="384" y="46" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="454" y="76" text-anchor="middle">devm_rtc_</text><text x="454" y="94" text-anchor="middle">register_device()</text>
    <rect x="532" y="46" width="140" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="602" y="68" text-anchor="middle">/dev/rtcN (cdev)</text><text x="602" y="86" text-anchor="middle">class/rtc/rtcN</text><text x="602" y="104" text-anchor="middle">rtc0: procfs 항목</text>
    <rect x="88" y="130" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="158" y="160" text-anchor="middle">struct led_classdev</text><text x="158" y="178" text-anchor="middle">(정적 또는 devm)</text>
    <rect x="236" y="130" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="306" y="152" text-anchor="middle">brightness_set</text><text x="306" y="170" text-anchor="middle">또는 _set_blocking</text><text x="306" y="188" text-anchor="middle">max_brightness</text>
    <rect x="384" y="130" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="454" y="160" text-anchor="middle">devm_led_</text><text x="454" y="178" text-anchor="middle">classdev_register()</text>
    <rect x="532" y="130" width="140" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="602" y="152" text-anchor="middle">class/leds/NAME</text><text x="602" y="170" text-anchor="middle">brightness, trigger</text><text x="602" y="188" text-anchor="middle">(char dev 없음)</text>
    <rect x="88" y="214" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="158" y="244" text-anchor="middle">devm_input_</text><text x="158" y="262" text-anchor="middle">allocate_device()</text>
    <rect x="236" y="214" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="306" y="236" text-anchor="middle">name, evbit/keybit</text><text x="306" y="254" text-anchor="middle">open/close (선택)</text><text x="306" y="272" text-anchor="middle">event (LED 등)</text>
    <rect x="384" y="214" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="454" y="244" text-anchor="middle">input_register_</text><text x="454" y="262" text-anchor="middle">device()</text>
    <rect x="532" y="214" width="140" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="602" y="236" text-anchor="middle">class/input/inputN</text><text x="602" y="254" text-anchor="middle">evdev 핸들러가</text><text x="602" y="272" text-anchor="middle">/dev/input/eventN</text>
    <rect x="88" y="298" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="158" y="328" text-anchor="middle">struct</text><text x="158" y="346" text-anchor="middle">watchdog_device</text>
    <rect x="236" y="298" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="306" y="320" text-anchor="middle">watchdog_ops</text><text x="306" y="338" text-anchor="middle">start, stop, ping</text><text x="306" y="356" text-anchor="middle">info, timeout</text>
    <rect x="384" y="298" width="140" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="454" y="328" text-anchor="middle">devm_watchdog_</text><text x="454" y="346" text-anchor="middle">register_device()</text>
    <rect x="532" y="298" width="140" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="602" y="320" text-anchor="middle">/dev/watchdogN</text><text x="602" y="338" text-anchor="middle">+ /dev/watchdog</text><text x="602" y="356" text-anchor="middle">watchdogd 워커</text>
  </g>
  <g stroke="currentColor">
    <line x1="228" y1="81" x2="234" y2="81" marker-end="url(#ah2)"/><line x1="376" y1="81" x2="382" y2="81" marker-end="url(#ah2)"/><line x1="524" y1="81" x2="530" y2="81" marker-end="url(#ah2)"/>
    <line x1="228" y1="165" x2="234" y2="165" marker-end="url(#ah2)"/><line x1="376" y1="165" x2="382" y2="165" marker-end="url(#ah2)"/><line x1="524" y1="165" x2="530" y2="165" marker-end="url(#ah2)"/>
    <line x1="228" y1="249" x2="234" y2="249" marker-end="url(#ah2)"/><line x1="376" y1="249" x2="382" y2="249" marker-end="url(#ah2)"/><line x1="524" y1="249" x2="530" y2="249" marker-end="url(#ah2)"/>
    <line x1="228" y1="333" x2="234" y2="333" marker-end="url(#ah2)"/><line x1="376" y1="333" x2="382" y2="333" marker-end="url(#ah2)"/><line x1="524" y1="333" x2="530" y2="333" marker-end="url(#ah2)"/>
  </g>
  <g font-size="12">
    <rect x="10" y="386" width="660" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="340" y="407" text-anchor="middle">네 줄 모두 같은 문장이다: 객체를 얻고, 콜백과 능력을 채우고, 등록하면 core가 노드·sysfs·uevent를 만든다</text>
    <text x="340" y="427" text-anchor="middle">input만 예외: 핸들러(evdev, kbd)가 노드를 만든다 — 유저 인터페이스가 한 단계 더 분리</text>
  </g>
</svg>
```
그림 2 — 네 프레임워크의 등록 흐름을 나란히. 행이 프레임워크, 열이 단계다. 파란 칸만 드라이버가 쓴다.

### 2.2 RTC — 가장 작은 프레임워크

`include/linux/rtc.h:59-71` (6.18.55):

```c
struct rtc_class_ops {
	int (*ioctl)(struct device *, unsigned int, unsigned long);
	int (*read_time)(struct device *, struct rtc_time *);
	int (*set_time)(struct device *, struct rtc_time *);
	int (*read_alarm)(struct device *, struct rtc_wkalrm *);
	int (*set_alarm)(struct device *, struct rtc_wkalrm *);
	int (*proc)(struct device *, struct seq_file *);
	int (*alarm_irq_enable)(struct device *, unsigned int enabled);
	int (*read_offset)(struct device *, long *offset);
	int (*set_offset)(struct device *, long offset);
	int (*param_get)(struct device *, struct rtc_param *param);
	int (*param_set)(struct device *, struct rtc_param *param);
};
```

- **할당**: `devm_rtc_allocate_device()` (`drivers/rtc/class.c:363`). ID를 받고(`rtc%d`), `rtc->dev.parent = dev`, devm 해제 액션을 건다.
- **등록**: `__devm_rtc_register_device()` (`drivers/rtc/class.c:392`, 매크로 `devm_rtc_register_device()`가 `THIS_MODULE`을 붙여 부른다). 이 안에서 ops 검사(`ops`가 없으면 `-EINVAL`), 기능 비트 정리(`:403`), 이미 걸린 알람 읽기, `rtc_dev_prepare()`(`:416` → `drivers/rtc/dev.c:539`에서 `cdev_init(&rtc->char_dev, &rtc_dev_fops)`), **`cdev_device_add()`로 char device와 class 장치를 한 번에 추가**(`:418`), `/proc/driver/rtc` 추가(hctosys 장치일 때만, `drivers/rtc/proc.c:81-85`), 그리고 이 장치가 `CONFIG_RTC_HCTOSYS_DEVICE`(`"rtc0"`)이면 부팅 시 시스템 시간을 RTC에서 읽어 설정(`rtc_hctosys`, `:435`)까지 한다.
- **콜백 경로**: 유저 `ioctl(fd, RTC_RD_TIME)` → `rtc_dev_ioctl()` (`drivers/rtc/dev.c:203`, `case RTC_RD_TIME`은 `:320`) → `rtc_read_time()` (`interface.c:110`, `ops_lock` mutex) → `ops->read_time`.
- **PM**: class 자체에 PM ops가 있다(`drivers/rtc/class.c:195` `SIMPLE_DEV_PM_OPS(rtc_class_dev_pm_ops, rtc_suspend, rtc_resume)`, `:203` `.pm = RTC_CLASS_DEV_PM_OPS`). 단 `rtc_suspend()`는 hctosys 장치(`rtc0`)에서만 suspend 중 흐른 시간을 시스템 시계에 보정한다(`:110`의 `strcmp` 검사).

정리하면 RTC 드라이버의 최소 요건은 `read_time` 하나(시간을 쓸 수 있게 하려면 `set_time`도)다. 나머지는 전부 core 몫이다.

### 2.3 LED — 콜백이 두 종류인 이유 (context 규칙)

`include/linux/leds.h:127-140` (6.18.55, 주석 그대로):

```c
	/* Set LED brightness level
	 * Must not sleep. Use brightness_set_blocking for drivers
	 * that can sleep while setting brightness.
	 */
	void		(*brightness_set)(struct led_classdev *led_cdev,
					  enum led_brightness brightness);
	/*
	 * Set LED brightness level immediately - it can block the caller for
	 * the time required for accessing a LED device register.
	 */
	int (*brightness_set_blocking)(struct led_classdev *led_cdev,
				       enum led_brightness brightness);
```

왜 두 개인가? LED 밝기는 **아무 데서나** 바뀐다. 유저가 sysfs에 쓰기도 하지만, **트리거**(timer, heartbeat, disk-activity, netdev …)가 타이머 콜백(softirq)이나 다른 드라이버의 IRQ 경로에서 바꾸기도 한다. GPIO 레지스터 한 번 쓰는 LED는 어느 context에서든 괜찮지만, I2C LED 컨트롤러(PCA9532 등)는 버스 전송 중 잠든다. 그래서 core는 두 길을 둔다. `drivers/leds/led-core.c:332-359`:

```c
void led_set_brightness_nopm(struct led_classdev *led_cdev, unsigned int value)
{
	/* Use brightness_set op if available, it is guaranteed not to sleep */
	if (!__led_set_brightness(led_cdev, value))
		return;

	/*
	 * Brightness setting can sleep, delegate it to a work queue task.
	 ...
	 */
	led_cdev->delayed_set_value = value;
	...
	queue_work(led_cdev->wq, &led_cdev->set_brightness_work);
}
```

(가운데 work 플래그 처리 줄은 `...`로 생략했다.) `brightness_set`이 있으면 **호출자의 context에서 바로** 부르고, 없으면(= `brightness_set_blocking`만 있으면) 워크큐에 넘겨 **kworker 스레드에서** 부른다. 그 워크큐는 등록 시 `led_cdev->wq = leds_wq`(`drivers/leds/led-class.c:568`)로 붙고, `leds_wq`는 `alloc_ordered_workqueue("leds", 0)`(`:693`)로 만든 **순서 보장 워크큐**다. 그래서 blocking 드라이버도 밝기 변경 순서가 뒤바뀌지 않는다.

- **할당/등록**: `struct led_classdev`는 드라이버가 정적 또는 `devm_kzalloc`으로 갖고, `devm_led_classdev_register()` → `led_classdev_register_ext()` (`led-class.c:484`)가 `device_create_with_groups(&leds_class, …)`(`:537`)로 class 장치를 만들고 기본 트리거를 건다(`:578`). **char device는 없다.** LED의 uAPI는 sysfs뿐이다.
- **sysfs 쓰기 경로**: `brightness_store()` (`led-class.c:44`)는 `led_cdev->led_access` mutex를 잡고 문자열을 `kstrtoul`로 파싱한 뒤 `led_set_brightness()`를 부른다. 0을 쓰면 트리거도 해제한다.
- **PM**: `leds_class_dev_pm_ops` (`led-class.c:226`, `.pm`은 `:248`). 드라이버가 `flags`에 `LED_CORE_SUSPENDRESUME`을 넣으면 suspend 때 LED를 끄고 resume 때 원래 밝기로 되돌린다(`led_suspend`/`led_resume`, `:204-221`). §6에서 실측한다.

§3에서 이 두 길이 실제로 어떤 context로 나타나는지, 그리고 `brightness_set`에서 잠들면 무엇이 터지는지 직접 본다.

### 2.4 input — 드라이버가 core를 부르는 방향이 하나 더 있다

`struct input_dev` (`include/linux/input.h:137`)에서 드라이버가 채우는 것은 이름·ID, **지원 이벤트 비트맵**(`evbit` `:145`, `keybit` `:146` …), 그리고 선택 콜백 `open`/`close`/`flush`/`event` (`:185-188`)다.

- **할당**: `devm_input_allocate_device()` (`drivers/input/input.c:1969`, devm 아닌 판은 `:1885`).
- **등록**: `input_register_device()` (`input.c:2312`). `device_add(&dev->dev)`(`:2363`)로 `/sys/class/input/inputN`을 만든 다음, 등록된 모든 **핸들러**에 붙여 본다(`:2377-2378`의 `input_attach_handler`). `/dev/input/eventN`은 드라이버도 input core도 아니라 **evdev 핸들러**가 `evdev_connect()`(`drivers/input/evdev.c:1341`)에서 `cdev_device_add()`(`:1390`)로 만든다. 그래서 같은 버튼이 `kbd` 핸들러(콘솔 키보드)와 `evdev`(유저 공간)에 동시에 붙는다.
- **방향이 두 개**: 다른 프레임워크는 "core → 드라이버 콜백"이 주된 방향이지만 input은 반대 방향이 데이터 경로다. 드라이버가 IRQ 핸들러 등에서 `input_report_key()` → `input_event()`(`input.c:391`)를 **부른다**. `input_event()`는 `guard(spinlock_irqsave)(&dev->event_lock)`로 보호되므로 **hardirq에서 불러도 된다.** 반면 core → 드라이버 방향의 `open`/`close`는 `dev->mutex`를 잡고(`input_open_device`, `:578`) 프로세스 context에서 불린다. "첫 번째 유저가 열 때 IRQ를 켜고, 마지막 유저가 닫을 때 끈다"는 전원 절약 패턴이 여기서 나온다.
- **PM**: `input_dev_pm_ops` (`input.c:1850`). suspend/resume 때 눌린 키를 "뗌"으로 정리한다(`input_dev_release_keys`, `:674`). 드라이버가 할 일이 없다.

### 2.5 watchdog — 프레임워크가 없던 시절과 비교할 수 있는 유일한 곳

`include/linux/watchdog.h:47-60`:

```c
struct watchdog_ops {
	struct module *owner;
	/* mandatory operations */
	int (*start)(struct watchdog_device *);
	/* optional operations */
	int (*stop)(struct watchdog_device *);
	int (*ping)(struct watchdog_device *);
	unsigned int (*status)(struct watchdog_device *);
	int (*set_timeout)(struct watchdog_device *, unsigned int);
	int (*set_pretimeout)(struct watchdog_device *, unsigned int);
	unsigned int (*get_timeleft)(struct watchdog_device *);
	int (*restart)(struct watchdog_device *, unsigned long, void *);
	long (*ioctl)(struct watchdog_device *, unsigned int, unsigned long);
};
```

필수는 `start` 하나다(`ping`이 없으면 core가 `start`로 대신 킥한다). core(`drivers/watchdog/watchdog_dev.c`)가 하는 일:

- `/dev/watchdogN` cdev(`:1079` `cdev_device_add`)와, 첫 번째 장치에 대해 옛 이름 `/dev/watchdog`(misc minor 130, `:1061` `misc_register`)까지 만든다.
- `open` = 시작(`watchdog_open` `:864` → `watchdog_start` `:894`), `write`/`WDIOC_KEEPALIVE` = 킥, **magic close**: `'V'`를 쓰고 닫으면 멈춤(`:720`의 `set_bit(_WDOG_ALLOW_RELEASE …)`, release에서 `:962` 검사), `WDIOC_GETSUPPORT`(`:773`) 등 ioctl 전부.
- 모든 ops 호출을 `wd_data->lock` mutex로 직렬화한다(`write` 경로 `:726`, `ioctl` `:760`).
- HW 최대 타임아웃이 유저가 원하는 타임아웃보다 짧으면(`max_hw_heartbeat_ms`), core가 **hrtimer + `watchdogd` kthread 워커**로 대신 킥해 준다(`watchdog_timer_expired` `:229` → `kthread_queue_work`, `watchdog_ping_work` `:217`에서 mutex 잡고 `__watchdog_ping`). 이 kthread는 `sched_set_fifo()`(`:1237`)로 RT 우선순위를 받는다.
- reboot notifier, restart handler(`watchdog_core.c:308`, `:322`), 부트로더가 켜 둔 워치독 처리(`CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED=y`)까지.

#### 예제 3 — 같은 디렉터리의 옛 드라이버 47개 vs 프레임워크 드라이버 130개

무엇을 확인하나: `drivers/watchdog/`에서 `misc_register`로 직접 char device를 만드는 옛 드라이버와 watchdog core를 쓰는 드라이버를 나눠, (1) 파일 길이 중앙값, (2) `WDIOC_GETSUPPORT` ioctl을 스스로 구현한 비율, (3) magic close 상태 변수 `expect_close`를 스스로 구현한 비율을 잰다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/drivers/watchdog; a=$(grep -lE "misc_register" *.c | sort); b=$(grep -lE "watchdog_register_device|watchdog_init_timeout" *.c | sort); L=$(comm -23 <(echo "$a") <(echo "$b") | grep -v watchdog_dev.c); F=$(echo "$b" | grep -v "watchdog_core.c\|watchdog_dev.c"); echo "legacy n=$(echo "$L"|wc -l) median_lines=$(for f in $L; do wc -l <$f; done | sort -n | awk "{a[NR]=\$1} END{print a[int((NR+1)/2)]}") expect_close_impl=$(grep -l expect_close $L | wc -l) WDIOC_GETSUPPORT_impl=$(grep -l WDIOC_GETSUPPORT $L | wc -l)"; echo "framework n=$(echo "$F"|wc -l) median_lines=$(for f in $F; do wc -l <$f; done | sort -n | awk "{a[NR]=\$1} END{print a[int((NR+1)/2)]}") expect_close_impl=$(grep -l expect_close $F | wc -l) WDIOC_GETSUPPORT_impl=$(grep -l WDIOC_GETSUPPORT $F | wc -l)"; wc -l softdog.c acquirewdt.c'
```

```text
legacy n=47 median_lines=383 expect_close_impl=30 WDIOC_GETSUPPORT_impl=46
framework n=130 median_lines=258 expect_close_impl=0 WDIOC_GETSUPPORT_impl=0
  222 softdog.c
  328 acquirewdt.c
  550 total
```

출력에서 볼 것: 옛 드라이버 47개 중 46개가 `WDIOC_GETSUPPORT`를, 30개가 magic close를 **각자 다시 구현**했다. 프레임워크 드라이버 130개는 0개다. 같은 ioctl 의미를 47번 복붙했으니 버그도 47벌이다(예: magic close를 잊은 드라이버, `nowayout` 처리가 다른 드라이버). 파일 길이 중앙값도 383줄 → 258줄로 짧다. 예를 들어 `acquirewdt.c`(328줄)는 `expect_close` 변수, `case WDIOC_GETSUPPORT`, `misc_register(&acq_miscdev)`를 손으로 다 갖고 있고(같은 파일 78·156·255행), `softdog.c`(222줄)는 `softdog_ops`에 `.start`/`.stop`만 채운 뒤 `watchdog_register_device()`를 부른다(168-171행, 201행).

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<g font-size="12">
<text x="10" y="20" font-size="13">drivers/watchdog/ (6.18.55): misc 직접 등록 47개 vs watchdog core 사용 130개</text>
<text x="242" y="67" text-anchor="end">파일 길이 중앙값 (줄)</text>
<rect x="250" y="49" width="335.1" height="16" fill="#d0564a" fill-opacity="0.75"/><text x="589.1" y="61">383</text>
<rect x="250" y="69" width="225.8" height="16" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="479.8" y="81">258</text>
<text x="242" y="125" text-anchor="end">WDIOC_GETSUPPORT 직접 구현</text>
<rect x="250" y="107" width="343.0" height="16" fill="#d0564a" fill-opacity="0.75"/><text x="597.0" y="119">98%</text>
<text x="254.0" y="139">0%</text>
<text x="242" y="183" text-anchor="end">magic close 직접 구현</text>
<rect x="250" y="165" width="224.0" height="16" fill="#d0564a" fill-opacity="0.75"/><text x="478.0" y="177">64%</text>
<text x="254.0" y="197">0%</text>
<line x1="250" y1="45" x2="250" y2="219" stroke="currentColor"/>
<rect x="250" y="226" width="12" height="12" fill="#d0564a" fill-opacity="0.75"/><text x="267" y="236">legacy: misc_register + 자체 ioctl</text>
<rect x="480" y="226" width="12" height="12" fill="#3f9a6b" fill-opacity="0.75"/><text x="497" y="236">watchdog_ops 등록</text>
</g>
</svg>
```
그림 3 — `drivers/watchdog/`(6.18.55) 실측 (magic close = `expect_close` 변수 보유): misc 직접 등록 드라이버(빨강) vs watchdog core 드라이버(초록). 예제 3의 숫자를 그대로 그렸다. 프레임워크가 생기면 "모든 드라이버가 똑같이 해야 하는 일"이 core로 올라가 0이 된다.

### 2.6 네 프레임워크 비교표

| 항목 | RTC | LED | input | watchdog |
|---|---|---|---|---|
| 장치 객체 | `struct rtc_device` (core가 할당) | `struct led_classdev` (드라이버가 소유) | `struct input_dev` (core가 할당) | `struct watchdog_device` (드라이버가 소유) |
| ops | `rtc_class_ops` (const, 별도 구조체) | 객체 안 함수 포인터 | 객체 안 함수 포인터 (`open`/`close`/`event`) | `watchdog_ops` (const, 별도 구조체) |
| 필수 op | `read_time` | `brightness_set` 또는 `_blocking` 중 하나 | 없음 (데이터는 드라이버가 `input_event()`로 밀어 넣음) | `start` |
| 등록 | `devm_rtc_register_device()` | `devm_led_classdev_register()` | `input_register_device()` (devm 할당이면 자동 해제) | `devm_watchdog_register_device()` |
| core가 잡는 락 | `rtc->ops_lock` mutex | sysfs 경로는 `led_access` mutex, 트리거 경로는 락 없이 직접 호출 | `dev->mutex`(open/close), `event_lock` spinlock(이벤트) | `wd_data->lock` mutex |
| uAPI | `/dev/rtcN` + `RTC_*` ioctl, sysfs, procfs | sysfs만 | `/dev/input/eventN` (`struct input_event`) | `/dev/watchdogN` + `WDIOC_*`, magic close |
| PM | class PM ops (rtc0 시간 보정) | `LED_CORE_SUSPENDRESUME`이면 끄고 복원 | 눌린 키 해제 | 드라이버 PM + `watchdog_stop_on_reboot` 등 |
| 표준 툴 | `hwclock`, `rtcwake`, `timedatectl` | `echo` + 트리거 (`heartbeat`, `netdev`) | `evtest`, libinput, `getevent` | `watchdog` 데몬, systemd `RuntimeWatchdogSec` |
| 상세 노트 | I9 | I9 | I3 | I9 |

---

## 3. "core는 내 op를 어떤 context에서 부르나"

### 3.1 직관 — 콜백의 계약서에는 "언제 불릴지"가 적혀 있다

펌웨어에서 콜백을 등록할 때(예: DMA 완료 콜백, 타이머 콜백) Don은 "이건 ISR 안에서 불리니까 블로킹 API를 쓰면 안 된다"를 습관처럼 확인했을 것이다. Linux 프레임워크도 똑같다. 단, **누가 부르는지가 core 안에 숨어 있어서** 드라이버 코드만 보면 모른다. 그래서 각 ops 구조체의 주석과 core 소스가 "계약서"다. 이 계약을 어기면 테스트에서는 안 터지다가 특정 트리거·특정 타이밍에서만 커널이 죽는다.

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g font-size="12">
    <text x="110" y="24" text-anchor="middle" font-size="13">부르는 쪽</text>
    <text x="345" y="24" text-anchor="middle" font-size="13">불리는 함수</text>
    <text x="575" y="24" text-anchor="middle" font-size="13">context · core가 잡은 락</text>
    <rect x="10" y="38" width="200" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="110" y="62" text-anchor="middle">LED 트리거 타이머 · sysfs</text>
    <rect x="240" y="38" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="345" y="62" text-anchor="middle">brightness_set</text>
    <rect x="480" y="38" width="190" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="575" y="62" text-anchor="middle">아무 context (softirq 포함)</text>
    <rect x="10" y="90" width="200" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="110" y="114" text-anchor="middle">LED: leds_wq kworker</text>
    <rect x="240" y="90" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="345" y="114" text-anchor="middle">brightness_set_blocking</text>
    <rect x="480" y="90" width="190" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="575" y="114" text-anchor="middle">process · ordered wq</text>
    <rect x="10" y="142" width="200" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="110" y="166" text-anchor="middle">RTC ioctl · sysfs · hctosys</text>
    <rect x="240" y="142" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="345" y="166" text-anchor="middle">read_time · set_time</text>
    <rect x="480" y="142" width="190" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="575" y="166" text-anchor="middle">process · ops_lock</text>
    <rect x="10" y="194" width="200" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="110" y="218" text-anchor="middle">WDT write · ioctl · kthread</text>
    <rect x="240" y="194" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="345" y="218" text-anchor="middle">start · stop · ping</text>
    <rect x="480" y="194" width="190" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="575" y="218" text-anchor="middle">process · wd_data->lock</text>
    <rect x="10" y="246" width="200" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="110" y="270" text-anchor="middle">hwmon sysfs read · write</text>
    <rect x="240" y="246" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="345" y="270" text-anchor="middle">hwmon_ops read · write</text>
    <rect x="480" y="246" width="190" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="575" y="270" text-anchor="middle">process · hwdev->lock</text>
    <rect x="10" y="298" width="200" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="110" y="322" text-anchor="middle">open(/dev/input/eventN)</text>
    <rect x="240" y="298" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="345" y="322" text-anchor="middle">input_dev open · close</text>
    <rect x="480" y="298" width="190" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="575" y="322" text-anchor="middle">process · dev->mutex</text>
    <rect x="10" y="350" width="200" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="110" y="374" text-anchor="middle">내 드라이버의 IRQ 핸들러</text>
    <rect x="240" y="350" width="210" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="345" y="374" text-anchor="middle">input_event() (core API)</text>
    <rect x="480" y="350" width="190" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="575" y="374" text-anchor="middle">hardirq OK · spin_irqsave</text>
    <text x="340" y="416" text-anchor="middle">빨강 = 잠들면 안 됨 (mutex, msleep, I2C/SPI 동기 전송 금지) · 초록 = 잠들어도 됨</text>
    <text x="340" y="434" text-anchor="middle">주황 = 방향이 반대: 드라이버가 core를 부른다 (core가 IRQ-safe 하게 만들어 둠)</text>
  </g>
  <g stroke="currentColor">
    <line x1="210" y1="58" x2="238" y2="58" marker-end="url(#ah4)"/><line x1="450" y1="58" x2="478" y2="58" marker-end="url(#ah4)"/>
    <line x1="210" y1="110" x2="238" y2="110" marker-end="url(#ah4)"/><line x1="450" y1="110" x2="478" y2="110" marker-end="url(#ah4)"/>
    <line x1="210" y1="162" x2="238" y2="162" marker-end="url(#ah4)"/><line x1="450" y1="162" x2="478" y2="162" marker-end="url(#ah4)"/>
    <line x1="210" y1="214" x2="238" y2="214" marker-end="url(#ah4)"/><line x1="450" y1="214" x2="478" y2="214" marker-end="url(#ah4)"/>
    <line x1="210" y1="266" x2="238" y2="266" marker-end="url(#ah4)"/><line x1="450" y1="266" x2="478" y2="266" marker-end="url(#ah4)"/>
    <line x1="210" y1="318" x2="238" y2="318" marker-end="url(#ah4)"/><line x1="450" y1="318" x2="478" y2="318" marker-end="url(#ah4)"/>
    <line x1="210" y1="370" x2="238" y2="370" marker-end="url(#ah4)"/><line x1="450" y1="370" x2="478" y2="370" marker-end="url(#ah4)"/>
  </g>
</svg>
```
그림 4 — "core가 내 op를 어떤 context에서 부르나" 지도 (6.18.55 소스 기준). 한 op가 여러 곳에서 불리면 **가장 나쁜 context**가 그 op의 규칙이 된다. `brightness_set`이 sysfs에서는 task context로 불려도, 트리거에서 softirq로 불릴 수 있으므로 "잠들기 금지"다.

### 3.2 표 — 누가 무엇을 어떤 context에서 부르나

| 프레임워크 | op | 부르는 쪽 (core 함수, 파일:줄) | context | core가 잡은 락 | 그래서 op 안에서 |
|---|---|---|---|---|---|
| LED | `brightness_set` | `led_set_brightness_nopm()` (`led-core.c:332`) ← sysfs `brightness_store` (`led-class.c:44`), 트리거 타이머 `led_timer_function` (`led-core.c:65`), heartbeat `led_heartbeat_function` (`ledtrig-heartbeat.c:33`) | 아무 context (task, softirq, 트리거에 따라 hardirq) | 없음 (sysfs 경로만 `led_access` mutex) | GPIO 레지스터 쓰기만. 잠들기 금지 |
| LED | `brightness_set_blocking` | `set_brightness_delayed()` (`led-core.c:146`, `leds_wq` 워크) 또는 `led_set_brightness_sync()` (`:373`) | process (kworker 또는 호출 task) | 없음 | I2C/SPI 전송, `msleep` 가능 |
| RTC | `read_time`, `set_time` | `rtc_read_time()`/`rtc_set_time()` (`interface.c:110`, `:126`) ← `rtc_dev_ioctl()` (`dev.c:203`), sysfs, hctosys | process | `rtc->ops_lock` mutex | I2C 전송 가능. 같은 장치의 다른 op와 동시 실행 없음 |
| watchdog | `start`, `stop`, `ping` | `watchdog_write`/`watchdog_ioctl` (`watchdog_dev.c:697`, `:750`), `watchdog_ping_work` (`:217`, `watchdogd` kthread) | process (유저 task 또는 FIFO kthread) | `wd_data->lock` mutex | 잠들 수 있지만 짧게 (킥이 늦으면 리셋) |
| hwmon | `read`, `write`, `read_string` | `hwmon_attr_show`/`_store` (`hwmon.c:426`, `:474`) | process (sysfs) | `hwdev->lock` mutex (`guard(mutex)`, `:435`, `:487`) | I2C 전송 가능 |
| input | `open`, `close` | `input_open_device()` (`input.c:578`) | process | `dev->mutex` | IRQ 켜기·끄기, 전원 켜기 |
| input (반대 방향) | `input_event()` | 드라이버가 부름 (`input.c:391`) | 아무 context | core가 `event_lock` spin_irqsave | IRQ 핸들러에서 바로 보고해도 됨 |

(hwmon core의 이 락은 커널 버전에 따라 다를 수 있다. 기존 hwmon 드라이버 중에는 자기 mutex를 따로 가진 것이 많다. 6.18.55에서는 `hwmon_attr_show()`가 `guard(mutex)(&hwdev->lock)`을 잡는 것을 소스로 직접 확인했다.)

### 3.3 예제 4 — `brightness_set`은 실제로 어디서 불리나

무엇을 확인하나: 같은 LED op가 ① `echo 1 > brightness`, ② `timer` 트리거, ③ `heartbeat` 트리거에서 각각 어떤 context로 불리는지. 드라이버(`i1_led`, 코드는 §6)는 콜백 안에서 `in_hardirq()`/`in_serving_softirq()`와 `current->comm`을 찍는다.

게스트 스크립트 `s3_led.sh` (`LEDARG`를 바꿔 예제 5·6에서도 그대로 쓴다):

```sh
insmod /i1/i1_led.ko $LEDARG
L=/sys/class/leds/i1:green:status
echo "### class tree"; ls /sys/class/leds; readlink -f $L; ls $L
echo "### triggers"; cat $L/trigger
echo "phase1: echo 1 > brightness" > /dev/kmsg; echo 1 > $L/brightness; usleep 100000
echo "phase2: timer trigger" > /dev/kmsg; echo timer > $L/trigger; ls $L; cat $L/delay_on $L/delay_off; sleep 1
echo none > $L/trigger
modprobe ledtrig-heartbeat
echo "phase3: heartbeat trigger" > /dev/kmsg; echo heartbeat > $L/trigger; grep -o "\[heartbeat\]" $L/trigger; sleep 2
echo none > $L/trigger
echo "### dmesg"; dmesg | grep -E "i1_led|phase|BUG|scheduling while|Call trace|^\[ *[0-9.]+\]  [a-z_]+\+0x" | grep -v taints | head -40
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.elinux_scratch/I1/boot.sh s3_led.sh; cat /var/tmp/I1/out_s3_led.txt'
```

```text
lab initramfs: 6.18.55-sub up 0.85s
### class tree
i1:green:status
/sys/devices/platform/i1_led/leds/i1:green:status
brightness      max_brightness  subsystem       uevent
device          power           trigger
### triggers
[none] kbd-scrolllock kbd-numlock kbd-capslock kbd-kanalock kbd-shiftlock kbd-altgrlock kbd-ctrllock kbd-altlock kbd-shiftllock kbd-shiftrlock kbd-ctrlllock kbd-ctrlrlock timer disk-activity disk-read disk-write cpu cpu0 cpu1 default-on panic
brightness      device          subsystem
delay_off       max_brightness  trigger
delay_on        power           uevent
500
500
[heartbeat]
### dmesg
[    1.013351] phase1: echo 1 > brightness
[    1.014509] i1_led: #1 brightness_set(1) ctx=task comm=sh
[    1.128143] phase2: timer trigger
[    1.164077] i1_led: #2 brightness_set(0) ctx=softirq comm=swapper/0
[    1.669289] i1_led: #3 brightness_set(1) ctx=softirq comm=swapper/1
[    2.184840] i1_led: #4 brightness_set(0) ctx=softirq comm=swapper/0
[    2.192400] i1_led: #5 brightness_set(0) ctx=task comm=sh
[    2.192439] i1_led: #6 brightness_set(0) ctx=task comm=sh
[    2.203779] phase3: heartbeat trigger
[    2.228933] i1_led: #7 brightness_set(1) ctx=task comm=sh
[    2.304660] i1_led: #8 brightness_set(0) ctx=softirq comm=swapper/1
[    2.549324] i1_led: #9 brightness_set(1) ctx=softirq comm=swapper/1
[    2.625898] i1_led: #10 brightness_set(0) ctx=softirq comm=swapper/1
[    3.525807] i1_led: #11 brightness_set(1) ctx=softirq comm=swapper/1
[    3.599435] i1_led: #12 brightness_set(0) ctx=softirq comm=swapper/1
[    3.843406] i1_led: #13 brightness_set(1) ctx=softirq comm=swapper/1
[    3.919363] i1_led: #14 brightness_set(0) ctx=softirq comm=swapper/1
[    4.276107] i1_led: #15 brightness_set(0) ctx=task comm=sh
[    4.314027] reboot: Power down
```

(dmesg 필터에 걸린 무관한 줄 `rcu: Max phase no-delay instances is 1000.` 한 줄은 지웠다.)

출력에서 볼 것:

- `#1`: sysfs 쓰기는 **task context**(`comm=sh`). 셸이 `write()`를 했고 그 스레드에서 바로 불렸다.
- `#2~#4`: timer 트리거는 500 ms마다(`delay_on`/`delay_off` = 500) **softirq**에서 불렸다. `comm=swapper/0`·`swapper/1`은 "CPU가 idle이던 중 타이머 인터럽트 끝에서 softirq가 돌았다"는 뜻이다. 즉 **아무 프로세스의 것도 아닌 시간**이다.
- `#8~#14`: heartbeat는 짧게 두 번 깜빡이고(on 약 76 ms, 간격 약 245 ms) 쉬는 패턴이다. 역시 softirq.
- 트리거를 켜고 끄는 순간(`#5`, `#6`, `#7`, `#15`)은 셸의 task context다. **같은 op가 두 context에서 불린다.** 그러니 규칙은 나쁜 쪽(softirq)에 맞춰야 한다.
- 트리거 목록 `[none] kbd-… timer disk-activity … cpu cpu0 cpu1 default-on panic`은 드라이버가 한 줄도 쓰지 않았는데 생겼다. `heartbeat`는 `modprobe ledtrig-heartbeat` 뒤에 목록에 추가되어 바로 선택되었다.

### 3.4 예제 5 — `brightness_set_blocking`만 주면 kworker로 옮겨진다

무엇을 확인하나: 같은 시나리오를 `blocking=1`(드라이버가 `brightness_set_blocking`만 채움)로 돌리면 core가 호출을 어디로 옮기는지.

```sh
# s4_ledblk.sh 내용:  LEDARG=blocking=1 . /i1/s3.sh
limactl shell elinux -- bash -lc '/Users/donh/.elinux_scratch/I1/boot.sh s4_ledblk.sh; sed -n "/### dmesg/,\$p" /var/tmp/I1/out_s4_ledblk.txt'
```

```text
### dmesg
[    1.015731] phase1: echo 1 > brightness
[    1.017136] i1_led: #1 brightness_set_blocking(1) ctx=task comm=kworker/u8:0
[    1.127650] phase2: timer trigger
[    1.152602] i1_led: #2 brightness_set_blocking(0) ctx=task comm=kworker/u8:0
[    1.660968] i1_led: #3 brightness_set_blocking(1) ctx=task comm=kworker/u8:0
[    2.172448] i1_led: #4 brightness_set_blocking(0) ctx=task comm=kworker/u8:0
[    2.204829] i1_led: #5 brightness_set_blocking(0) ctx=task comm=kworker/u8:0
[    2.216782] phase3: heartbeat trigger
[    2.228748] i1_led: #6 brightness_set_blocking(1) ctx=task comm=kworker/u8:0
[    2.305185] i1_led: #7 brightness_set_blocking(0) ctx=task comm=kworker/u8:0
[    2.549076] i1_led: #8 brightness_set_blocking(1) ctx=task comm=kworker/u8:0
[    2.624367] i1_led: #9 brightness_set_blocking(0) ctx=task comm=kworker/u8:0
[    3.517354] i1_led: #10 brightness_set_blocking(1) ctx=task comm=kworker/u8:0
[    3.592713] i1_led: #11 brightness_set_blocking(0) ctx=task comm=kworker/u8:0
[    3.837560] i1_led: #12 brightness_set_blocking(1) ctx=task comm=kworker/u8:0
[    3.912605] i1_led: #13 brightness_set_blocking(0) ctx=task comm=kworker/u8:0
[    4.266433] i1_led: #14 brightness_set_blocking(0) ctx=task comm=kworker/u8:1
[    4.402952] reboot: Power down
```

(무관한 `rcu:` 줄 한 줄 생략.)

출력에서 볼 것: **모든** 호출이 `kworker/u8:N`(unbound 워크큐의 워커 스레드)에서 왔다. 셸이 `echo 1`을 한 경우(`#1`)조차 core는 직접 부르지 않고 워크큐에 넘겼다(`led_set_brightness_nopm()`의 `queue_work`). 이 콜백 안에서는 `msleep(1)`을 매번 했는데 아무 문제가 없다. 대가는 **지연**이다. 밝기가 실제로 바뀌는 시점이 워커가 스케줄되는 시점으로 밀린다. LED에는 문제없지만 "정확한 시각에 핀을 토글"해야 하는 용도라면 LED 프레임워크가 아니라 PWM이나 HW 타이머를 쓴다.

### 3.5 예제 6 — 계약을 어기면: `brightness_set`에서 `msleep()`

무엇을 확인하나: `sleepbug=1`이면 드라이버가 `brightness_set` 안에서 `msleep(1)`을 한다(I2C LED 컨트롤러를 `brightness_set`으로 잘못 등록한 상황의 재현). sysfs 쓰기(task)에서는 멀쩡하다가 timer 트리거(softirq)에서 무슨 일이 생기는지 본다. 커널이 죽을 수 있으므로 `TMO=40`, 콘솔 로그를 다 보려고 `loglevel=8`을 붙였다.

```sh
# s5_ledbug.sh 내용:  LEDARG=sleepbug=1 . /i1/s3.sh
limactl shell elinux -- bash -lc 'EXTRA=loglevel=8 TMO=40 /Users/donh/.elinux_scratch/I1/boot.sh s5_ledbug.sh; awk "/phase1/,0" /var/tmp/I1/out_s5_ledbug.txt | head -48'
```

```text
qemu rc=124
[    1.420101] phase1: echo 1 > brightness
[    1.421196] i1_led: #1 brightness_set(1) ctx=task comm=sh
[    1.538412] phase2: timer trigger
brightness      device          subsystem
delay_off       max_brightness  trigger
delay_on        power           uevent
[    1.553071] i1_led: #2 brightness_set(0) ctx=softirq comm=swapper/0
[    1.553183] BUG: scheduling while atomic: swapper/0/0/0x00000102
[    1.553211] Modules linked in: i1_led(O)
[    1.554101] CPU: 0 UID: 0 PID: 0 Comm: swapper/0 Tainted: G           O        6.18.55-sub #2 PREEMPT 
[    1.554186] Tainted: [O]=OOT_MODULE
[    1.554200] Hardware name: linux,dummy-virt (DT)
[    1.554303] Call trace:
[    1.554408]  show_stack+0x28/0x7c (C)
[    1.555453]  dump_stack_lvl+0x60/0x80
[    1.555476]  dump_stack+0x18/0x24
[    1.555485]  __schedule_bug+0x50/0x68
[    1.555495]  __schedule+0x75c/0x968
[    1.555506]  schedule+0x34/0xd4
[    1.555514]  schedule_timeout+0x80/0x104
[    1.555522]  msleep+0x28/0x40
[    1.555530]  i1_set+0x38/0x44 [i1_led]
[    1.555759]  led_timer_function+0x7c/0x1fc
[    1.555770]  call_timer_fn.isra.0+0x24/0x80
[    1.555780]  __run_timers+0x22c/0x280
[    1.555788]  run_timer_softirq+0x8c/0x100
[    1.555796]  handle_softirqs+0xfc/0x240
[    1.555805]  __do_softirq+0x14/0x20
[    1.555812]  ____do_softirq+0x10/0x20
[    1.555820]  call_on_irq_stack+0x30/0x48
[    1.555829]  do_softirq_own_stack+0x1c/0x40
[    1.555837]  __irq_exit_rcu+0xd4/0x10c
[    1.555845]  irq_exit_rcu+0x10/0x1c
[    1.555865]  el1_interrupt+0x3c/0x60
[    1.555876]  el1h_64_irq_handler+0x18/0x24
[    1.555886]  el1h_64_irq+0x6c/0x70
[    1.555928]  default_idle_call+0x28/0x60 (P)
[    1.555937]  do_idle+0x268/0x2c0
[    1.555946]  cpu_startup_entry+0x34/0x40
[    1.555954]  rest_init+0xd8/0xe0
[    1.555962]  console_on_rootfs+0x0/0x6c
[    1.555971]  __primary_switched+0x88/0x90
[    1.556060] bad: scheduling from the idle thread!
```

같은 실행의 끝부분(약 40줄 생략):

```text
[    1.558468] Unable to handle kernel execute from non-executable memory at virtual address ffff00001fec5c80
...
[    1.568695] Kernel panic - not syncing: Attempted to kill the idle task!
[    1.568899] SMP: stopping secondary CPUs
...
[    1.569692] ---[ end Kernel panic - not syncing: Attempted to kill the idle task! ]---
qemu-system-aarch64: terminating on signal 15 from pid 1033279 (timeout)
```

(`quiet`만 붙인 첫 실행에서는 같은 BUG 뒤에 `Unable to handle kernel paging request at virtual address 0040041ff96962a0`라는 **다른** Oops가 났고 마지막 panic 문장은 같았다. 잠들면 안 되는 곳에서 잠든 뒤의 증상은 실행마다 다르다. 첫 줄 `BUG: scheduling while atomic`이 진짜 원인이다.)

출력에서 볼 것:

- `#1`(sysfs, task context)에서는 `msleep`이 **아무 문제 없이 지나갔다.** 그래서 이런 버그는 "echo로 테스트했는데 잘 되던데요"로 리뷰를 통과한다.
- 트리거가 켜지자마자 첫 softirq 호출(`#2`)에서 `BUG: scheduling while atomic`. 스택을 아래에서 위로 읽으면 Don이 아는 그림 그대로다: idle 루프(`default_idle_call`) → **IRQ 예외 진입**(`el1h_64_irq`) → IRQ 끝에서 softirq 처리(`__irq_exit_rcu` → `handle_softirqs` → `run_timer_softirq`) → LED core의 `led_timer_function` → 내 `i1_set` → `msleep` → `schedule`. 펌웨어로 치면 **타이머 ISR 안에서 RTOS `vTaskDelay()`를 부른 것**이다.
- `0x00000102`는 그 순간의 `preempt_count`다. `0x100`이 "softirq 처리 중"(SOFTIRQ_OFFSET) 비트, `0x2`는 선점 비활성 카운트다. 0이 아니므로 "atomic".
- 결국 idle 태스크(`swapper`, PID 0)가 잠들려다 `Attempted to kill the idle task!` panic. 랩 커널은 `DEBUG_ATOMIC_SLEEP`이 꺼진 일반 커널이라 `might_sleep()` 경고 없이 바로 이 지경이 됐다. 디버그 커널(`Image-dbg`, `CONFIG_DEBUG_ATOMIC_SLEEP=y`)이라면 `#1`처럼 문제가 없어 보이는 경로에서도 잘못된 context를 더 일찍 잡아 준다(M 모듈).

교훈: **op의 context 규칙은 core의 주석과 소스가 정한다.** 드라이버 작성자는 "내 테스트에서 어떤 context로 불렸나"가 아니라 "core가 어떤 context에서 부를 수 있나"로 판단해야 한다.

---

## 4. 같은 온도계를 두 번 — (a) misc + ioctl vs (b) hwmon

### 4.1 시나리오

센서 키트 보드에 I2C 온도 센서(예: TMP102 계열)가 하나 있다. 요구 사항은 "현재 온도 읽기, 경고 온도(max) 읽고 쓰기, 위험 온도(crit) 읽기, 라벨(board)". G4를 막 배운 사람이 짤 법한 답 (a)와, 메인테이너가 받아 주는 답 (b)를 **같은 칩 접근 함수**로 짠다. 칩 접근은 `chip_read_temp()` 하나로 흉내 낸다(42.000 °C에서 10초 주기로 0.125 °C씩 오르는 값).

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g font-size="12">
    <text x="170" y="22" text-anchor="middle" font-size="13">(a) misc char device + 자체 ioctl</text>
    <text x="510" y="22" text-anchor="middle" font-size="13">(b) hwmon 프레임워크</text>
    <rect x="10" y="36" width="320" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
    <text x="170" y="55" text-anchor="middle">유저: thermo_cli 21줄 + i1_thermo.h 12줄</text><text x="170" y="73" text-anchor="middle">모든 소비자가 헤더를 복사해야 함</text>
    <rect x="10" y="100" width="320" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
    <text x="170" y="124" text-anchor="middle">ioctl(fd, I1_THERMO_GET, &amp;info)</text>
    <rect x="10" y="158" width="320" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
    <text x="170" y="182" text-anchor="middle">/dev/i1_thermo (10:258, crw------- root)</text>
    <rect x="10" y="216" width="320" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="170" y="240" text-anchor="middle">misc core: fops를 그대로 넘겨줄 뿐</text>
    <rect x="10" y="274" width="320" height="58" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
    <text x="170" y="296" text-anchor="middle">thermo_ioctl(): 자체 mutex, copy_to/from_user</text><text x="170" y="316" text-anchor="middle">범위 검사, -ENOTTY, compat_ioctl</text>
    <rect x="350" y="36" width="320" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="510" y="55" text-anchor="middle">유저: cat · 셸 루프 · lm-sensors</text><text x="510" y="73" text-anchor="middle">collectd/telegraf · thermal zone</text>
    <rect x="350" y="100" width="320" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="510" y="124" text-anchor="middle">read("…/hwmon0/temp1_input")</text>
    <rect x="350" y="158" width="320" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="510" y="182" text-anchor="middle">temp1_input 0444 · temp1_max 0644</text>
    <rect x="350" y="216" width="320" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="510" y="240" text-anchor="middle">hwmon core: mutex, sprintf, kstrtol, 권한</text>
    <rect x="350" y="274" width="320" height="58" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="510" y="296" text-anchor="middle">th_read(hwmon_temp, hwmon_temp_input)</text><text x="510" y="316" text-anchor="middle">값 하나만 돌려준다 (long, 밀리°C)</text>
    <rect x="180" y="352" width="320" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="340" y="376" text-anchor="middle">chip_read_temp() — 두 드라이버가 똑같이 공유</text>
    <rect x="10" y="410" width="660" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="340" y="431" text-anchor="middle">커널 코드 양은 거의 같다 (88줄 vs 85줄). 다른 것은 위쪽 절반:</text>
    <text x="340" y="450" text-anchor="middle">(a)는 유저 쪽 코드와 ABI 문서를 새로 만들어야 하고, (b)는 이미 있는 ABI와 툴을 그대로 쓴다</text>
  </g>
  <g stroke="currentColor">
    <line x1="170" y1="82" x2="170" y2="98" marker-end="url(#ah3)"/><line x1="170" y1="140" x2="170" y2="156" marker-end="url(#ah3)"/>
    <line x1="170" y1="198" x2="170" y2="214" marker-end="url(#ah3)"/><line x1="170" y1="256" x2="170" y2="272" marker-end="url(#ah3)"/>
    <line x1="170" y1="332" x2="250" y2="350" marker-end="url(#ah3)"/>
    <line x1="510" y1="82" x2="510" y2="98" marker-end="url(#ah3)"/><line x1="510" y1="140" x2="510" y2="156" marker-end="url(#ah3)"/>
    <line x1="510" y1="198" x2="510" y2="214" marker-end="url(#ah3)"/><line x1="510" y1="256" x2="510" y2="272" marker-end="url(#ah3)"/>
    <line x1="510" y1="332" x2="430" y2="350" marker-end="url(#ah3)"/>
  </g>
</svg>
```
그림 5 — 같은 온도계, 두 가지 설계. 빨강은 드라이버·유저가 새로 만든 것, 초록은 표준을 따른 것, 주황은 core가 대신 해 주는 것.

### 4.2 (a) misc + ioctl — 코드 (1/2): private uAPI 헤더와 ioctl

유저와 커널이 공유해야 하는 헤더부터 생긴다. 이것이 **새 ABI**다. 한 번 배포하면 구조체 레이아웃·ioctl 번호를 영원히 못 바꾼다.

```c
/* i1_thermo.h - the private uAPI the misc variant forces on user space */
#ifndef I1_THERMO_H
#define I1_THERMO_H
#include <linux/ioctl.h>
#include <linux/types.h>
struct i1_thermo_info {
	__s32 temp_mc;		/* milli-degC */
	__s32 max_mc;
	__s32 crit_mc;
};
#define I1_THERMO_GET		_IOR('T', 1, struct i1_thermo_info)
#define I1_THERMO_SET_MAX	_IOW('T', 2, __s32)
#endif
```

```c
// (a) anti-pattern: thermometer as misc char device + private ioctl
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/timekeeping.h>
#include "i1_thermo.h"

static DEFINE_MUTEX(lock);		/* we must invent our own locking */
static s32 max_mc = 80000;

static s32 chip_read_temp(void)		/* pretend I2C register read */
{
	return 42000 + (s32)(ktime_get_real_seconds() % 10) * 125;
}

static long thermo_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct i1_thermo_info info;
	s32 v;
	long ret = 0;

	mutex_lock(&lock);
	switch (cmd) {
	case I1_THERMO_GET:
		info.temp_mc = chip_read_temp();
		info.max_mc = max_mc;
		info.crit_mc = 95000;
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			ret = -EFAULT;
		break;
	case I1_THERMO_SET_MAX:
		if (copy_from_user(&v, (void __user *)arg, sizeof(v)))
			ret = -EFAULT;
		else if (v < 0 || v > 125000)
			ret = -EINVAL;
		else
			max_mc = v;
		break;
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&lock);
	return ret;
}
```

### 4.3 (a) misc + ioctl — 코드 (2/2): fops, miscdevice, probe

```c
static const struct file_operations thermo_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = thermo_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static struct miscdevice thermo_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "i1_thermo",
	.fops = &thermo_fops,
};

static int thermo_probe(struct platform_device *pdev)
{
	thermo_misc.parent = &pdev->dev;
	return misc_register(&thermo_misc);
}

static void thermo_remove(struct platform_device *pdev)
{
	misc_deregister(&thermo_misc);
}
/* + 공통 뼈대 (platform_driver에 .remove = thermo_remove 추가) */
```

그리고 유저 쪽. **이 프로그램이 없으면 온도를 읽을 방법이 없다.** `cat`도, 셸 스크립트도, 기존 모니터링 툴도 못 쓴다.

```c
/* user space for the misc variant: needs the private header + ioctl */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "i1_thermo.h"

int main(int argc, char **argv)
{
	struct i1_thermo_info info;
	int fd = open("/dev/i1_thermo", O_RDONLY);

	if (fd < 0) { perror("open"); exit(1); }
	if (argc > 1) {
		__s32 v = atoi(argv[1]);
		if (ioctl(fd, I1_THERMO_SET_MAX, &v) < 0) { perror("SET_MAX"); exit(1); }
	}
	if (ioctl(fd, I1_THERMO_GET, &info) < 0) { perror("GET"); exit(1); }
	printf("temp=%d max=%d crit=%d (milli-degC)\n",
	       info.temp_mc, info.max_mc, info.crit_mc);
	close(fd);
	return 0;
}
```

### 4.4 (b) hwmon — 코드 (공통 뼈대 제외 전부)

```c
// (b) framework: the same thermometer as a hwmon driver
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/timekeeping.h>

static s32 max_mc = 80000;		/* hwmon core serializes ops */

static s32 chip_read_temp(void)		/* pretend I2C register read */
{
	return 42000 + (s32)(ktime_get_real_seconds() % 10) * 125;
}

static umode_t th_visible(const void *data, enum hwmon_sensor_types type,
			  u32 attr, int ch)
{
	return attr == hwmon_temp_max ? 0644 : 0444;
}

static int th_read(struct device *dev, enum hwmon_sensor_types type,
		   u32 attr, int ch, long *val)
{
	switch (attr) {
	case hwmon_temp_input: *val = chip_read_temp(); return 0;
	case hwmon_temp_max:   *val = max_mc; return 0;
	case hwmon_temp_crit:  *val = 95000; return 0;
	}
	return -EOPNOTSUPP;
}

static int th_read_string(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int ch, const char **str)
{
	*str = "board";
	return 0;
}

static int th_write(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int ch, long val)
{
	if (val < 0 || val > 125000)
		return -EINVAL;
	max_mc = val;
	return 0;
}

static const struct hwmon_ops th_ops = {
	.is_visible = th_visible,
	.read = th_read,
	.read_string = th_read_string,
	.write = th_write,
};

static const struct hwmon_channel_info * const th_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_LABEL),
	NULL
};

static const struct hwmon_chip_info th_chip = { .ops = &th_ops, .info = th_info };

static int th_probe(struct platform_device *pdev)
{
	struct device *hw;

	hw = devm_hwmon_device_register_with_info(&pdev->dev, "i1_thermo",
						  NULL, &th_chip, NULL);
	return PTR_ERR_OR_ZERO(hw);
}
/* + 공통 뼈대 (.remove 불필요 — devm이 해제) */
```

(노트에 맞추려고 `th_chip` 초기화를 한 줄로 붙였다. 실제 파일은 4줄로 펼쳐져 있다. 줄 수 측정은 실제 파일 기준이다.)

코드를 읽는 법:

- `HWMON_CHANNEL_INFO(temp, …)` 한 줄이 "온도 채널 1개, 속성 4개"라는 **능력 선언**이다. core는 이것을 보고 `temp1_input`, `temp1_max`, `temp1_crit`, `temp1_label` 파일 이름을 만든다. 이름은 `Documentation/hwmon/sysfs-interface.rst`에 정해진 표준 이름이라 드라이버가 고를 수 없다(고를 수 없다는 것이 장점이다).
- 단위도 표준이다: 온도는 **밀리 °C 정수**. 그래서 `th_read`는 `long` 하나만 돌려주고, 문자열 변환(`sprintf`)과 파싱(`kstrtol`)은 core가 한다.
- 락이 없다. 6.18.55의 hwmon core가 `hwdev->lock`으로 모든 `read`/`write`를 직렬화한다(§3.2 표).
- `.remove`도 없다. `devm_` 등록이라 장치가 사라질 때 자동 해제된다. (a)는 `misc_deregister`를 잊으면 모듈 언로드 후 dangling fops가 남는다.

### 4.5 예제 7 — 두 드라이버를 같은 게스트에 넣고 유저가 보는 것 비교

무엇을 확인하나: 두 모듈을 차례로 넣을 때 나오는 uevent, 그리고 각각 유저 공간에서 무엇으로·어떤 권한으로 접근하는지. uevent는 직접 짠 netlink 리스너(`uev.c`, 아래)로 받는다. udev/mdev가 받는 바로 그 메시지다(G4·G9).

```c
/* print kernel uevents (what udev/mdev receive) for N seconds */
#include <linux/netlink.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	int secs = argc > 1 ? atoi(argv[1]) : 3;
	int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	char buf[4096];

	if (fd < 0 || bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		perror("netlink"); exit(1);
	}
	struct pollfd p = { .fd = fd, .events = POLLIN };
	while (poll(&p, 1, secs * 1000) > 0) {
		ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
		if (n <= 0) break;
		buf[n] = 0;
		const char *act = "", *path = "", *sub = "";
		for (char *s = buf + strlen(buf) + 1; s < buf + n; s += strlen(s) + 1) {
			if (!strncmp(s, "ACTION=", 7)) act = s + 7;
			else if (!strncmp(s, "DEVPATH=", 8)) path = s + 8;
			else if (!strncmp(s, "SUBSYSTEM=", 10)) sub = s + 10;
		}
		printf("%-6s %-9s %s\n", act, sub, path);
		fflush(stdout);
	}
	return 0;
}
```

게스트 스크립트 `s2_thermo.sh`:

```sh
echo "### uevents during insmod (misc, then hwmon)"
/i1/uev 2 > /tmp/u & sleep 0.3
insmod /i1/i1_thermo_misc.ko
insmod /i1/i1_thermo_hwmon.ko
wait; cat /tmp/u
echo "### (a) misc: what user space sees"
ls -l /dev/i1_thermo
ls /sys/class/misc/i1_thermo/
cat /dev/i1_thermo; echo "cat rc=$?"
/i1/thermo_cli
/i1/thermo_cli 70000
echo "### (b) hwmon: what user space sees"
H=$(dirname $(grep -l i1_thermo /sys/class/hwmon/*/name))
echo "H=$H -> $(readlink -f $H)"
ls -l $H/ | grep -v "^total"
for f in name temp1_label temp1_input temp1_max temp1_crit; do echo "$f=$(cat $H/$f)"; done
echo 70000 > $H/temp1_max; echo "after write: temp1_max=$(cat $H/temp1_max)"
echo 200000 > $H/temp1_max; echo "write 200000 rc=$?"
echo "### sensors-style loop over every hwmon device (no lm-sensors needed)"
for h in /sys/class/hwmon/hwmon*; do
  for t in $h/temp*_input; do
    p=${t%_input}; l=$(cat ${p}_label 2>/dev/null)
    echo "$(cat $h/name) ${l:-$(basename $p)}: $(( $(cat $t) / 1000 )).$(( $(cat $t) % 1000 / 100 )) C (max $(( $(cat ${p}_max) / 1000 )) C, crit $(( $(cat ${p}_crit) / 1000 )) C)"
  done
done
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.elinux_scratch/I1/boot.sh s2_thermo.sh; cat /var/tmp/I1/out_s2_thermo.txt'
```

```text
lab initramfs: 6.18.55-sub up 0.76s
### uevents during insmod (misc, then hwmon)
add    drivers   /bus/platform/drivers/i1_thermo_misc
add    platform  /devices/platform/i1_thermo_misc
add    misc      /devices/platform/i1_thermo_misc/misc/i1_thermo
bind   platform  /devices/platform/i1_thermo_misc
add    module    /module/i1_thermo_misc
add    drivers   /bus/platform/drivers/i1_thermo_hwmon
add    platform  /devices/platform/i1_thermo_hwmon
add    hwmon     /devices/platform/i1_thermo_hwmon/hwmon/hwmon0
bind   platform  /devices/platform/i1_thermo_hwmon
add    module    /module/i1_thermo_hwmon
### (a) misc: what user space sees
crw-------    1 0        0          10, 258 Oct 11 23:12 /dev/i1_thermo
dev        device     power      subsystem  uevent
cat: read error: Invalid argument
cat rc=1
temp=43125 max=80000 crit=95000 (milli-degC)
temp=43125 max=70000 crit=95000 (milli-degC)
### (b) hwmon: what user space sees
H=/sys/class/hwmon/hwmon0 -> /sys/devices/platform/i1_thermo_hwmon/hwmon/hwmon0
lrwxrwxrwx    1 0        0                0 Oct 11 23:12 device -> ../../../i1_thermo_hwmon
-r--r--r--    1 0        0             4096 Oct 11 23:12 name
drwxr-xr-x    2 0        0                0 Oct 11 23:12 power
lrwxrwxrwx    1 0        0                0 Oct 11 23:12 subsystem -> ../../../../../class/hwmon
-r--r--r--    1 0        0             4096 Oct 11 23:12 temp1_crit
-r--r--r--    1 0        0             4096 Oct 11 23:12 temp1_input
-r--r--r--    1 0        0             4096 Oct 11 23:12 temp1_label
-rw-r--r--    1 0        0             4096 Oct 11 23:12 temp1_max
-rw-r--r--    1 0        0             4096 Oct 11 23:12 uevent
name=i1_thermo
temp1_label=board
temp1_input=43125
temp1_max=80000
temp1_crit=95000
after write: temp1_max=70000
sh: write error: Invalid argument
write 200000 rc=1
### sensors-style loop over every hwmon device (no lm-sensors needed)
i1_thermo board: 43.1 C (max 70 C, crit 95 C)
[    3.410392] reboot: Power down
```

출력에서 볼 것:

- **uevent는 둘 다 나온다**(`add misc …`, `add hwmon …`). 둘 다 `struct device`를 class에 등록했기 때문이다. 차이는 uevent의 **의미**다. `SUBSYSTEM=hwmon`이면 모니터링 데몬·udev 규칙이 "온도 센서가 생겼다"를 안다. `SUBSYSTEM=misc`는 "뭔가 장치 노드가 생겼다"만 알려 준다.
- (a)의 `/dev/i1_thermo`는 `crw-------` root 전용이다. 일반 사용자 데몬이 읽게 하려면 udev 규칙을 따로 써야 한다. sysfs 디렉터리에는 `dev`·`uevent`·`power`밖에 없어서 **sysfs로 알 수 있는 것이 0개**다. `cat`은 `.read`가 없으니 `-EINVAL`.
- (b)는 `temp1_*`가 `0444`(누구나 읽기), `temp1_max`만 `0644`. 권한은 `th_visible()`의 반환값 그대로다. 값은 표준 단위(밀리 °C) 정수.
- (b)에 범위 밖 값(`200000`)을 쓰면 드라이버의 `th_write`가 돌려준 `-EINVAL`이 셸의 `write error: Invalid argument`로 그대로 나타난다. 에러 의미도 표준 errno로 통일된다.
- 마지막 셸 루프는 `/sys/class/hwmon/hwmon*/temp*_input` 규칙만 아는 **범용** 코드다. 우리 드라이버 이름을 하나도 모르는데 동작했다. lm-sensors의 `sensors`가 하는 일이 본질적으로 이것이다(BusyBox 게스트에는 lm-sensors가 없어서 실행하지 않았다).

### 4.6 숫자로 비교 — 줄 수와 유저가 받는 것

#### 예제 8 — 코드 줄 수 세기

무엇을 확인하나: 각 파일의 전체 줄 수와, 빈 줄·주석 줄을 뺀 "코드 줄" 수. 그리고 공통 platform 뼈대를 뺀 "프레임워크 부분" 줄 수.

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.elinux_scratch/I1; printf "%-20s %6s %6s\n" file total code; for f in i1_thermo_misc.c i1_thermo.h thermo_cli.c i1_thermo_hwmon.c i1_rtc.c i1_led.c wdt_info.c; do printf "%-20s %6s %6s\n" $f $(wc -l < $f) $(grep -cvE "^\s*($|//|/\*|\*)" $f); done; for f in i1_thermo_misc.c i1_thermo_hwmon.c i1_rtc.c i1_led.c; do n=$(awk "/^static struct platform_driver/{exit} {print}" $f | grep -cvE "^\s*($|//|/\*|\*)"); echo "$f framework-part=$n"; done'
```

```text
file                  total   code
i1_thermo_misc.c        102     88
i1_thermo.h              13     12
thermo_cli.c             24     21
i1_thermo_hwmon.c       103     85
i1_rtc.c                 67     56
i1_led.c                 81     69
wdt_info.c               35     32
i1_thermo_misc.c framework-part=61
i1_thermo_hwmon.c framework-part=59
i1_rtc.c framework-part=30
i1_led.c framework-part=43
```

(`grep` 정규식이 `/*`로 시작하는 줄을 주석으로 빼므로 코드 끝에 붙은 주석 줄은 코드로 센다. 두 온도계에 같은 규칙을 적용했다.)

출력에서 볼 것: **커널 쪽 줄 수는 거의 같다**(88 vs 85, 프레임워크 부분 61 vs 59). "프레임워크를 쓰면 코드가 확 줄어든다"는 이 작은 예에서는 사실이 아니다. hwmon 쪽은 라벨(`read_string`)과 권한(`is_visible`)을 추가로 구현했는데도 같은 길이라는 정도다. 진짜 차이는 커널 밖에 있다. (a)는 유저 쪽에 **헤더 12줄 + CLI 21줄 = 33줄**이 더 필요하고, 그 33줄은 이 온도계를 쓰는 **모든** 프로그램(모니터링 데몬, 공장 테스트, 웹 UI …)에 복제된다. (b)는 0줄이다. 그리고 RTC는 프레임워크 부분 30줄로 `/dev/rtcN`·sysfs 7개 속성·`hwclock` 호환을 얻는다(§5).

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<g font-size="12">
<text x="10" y="20" font-size="13">왼쪽: 내가 쓰는 코드 줄 수 (공백·주석 제외)</text>
<text x="400" y="20" font-size="13">오른쪽: 표준 sysfs 속성 수</text>
<text x="122" y="77" text-anchor="end">(a) misc 온도계</text>
<rect x="130" y="64" width="140.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="270.8" y="64" width="52.8" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<text x="327.6" y="77">88 + 33</text>
<text x="404.0" y="77">0 (dev 노드만)</text>
<text x="122" y="121" text-anchor="end">(b) hwmon 온도계</text>
<rect x="130" y="108" width="136.0" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="270.0" y="121">85</text>
<rect x="400" y="108" width="156.2" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="560.2" y="121">5</text>
<text x="122" y="165" text-anchor="end">i1_rtc</text>
<rect x="130" y="152" width="89.6" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="223.6" y="165">56</text>
<rect x="400" y="152" width="218.8" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="622.8" y="165">7</text>
<text x="122" y="209" text-anchor="end">i1_led</text>
<rect x="130" y="196" width="110.4" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="244.4" y="209">69</text>
<rect x="400" y="196" width="93.8" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="497.8" y="209">3</text>
<line x1="130" y1="60" x2="130" y2="240" stroke="currentColor"/>
<line x1="400" y1="60" x2="400" y2="240" stroke="currentColor"/>
<line x1="130.0" y1="240" x2="130.0" y2="244" stroke="currentColor"/><text x="130.0" y="256" text-anchor="middle">0</text>
<line x1="210.0" y1="240" x2="210.0" y2="244" stroke="currentColor"/><text x="210.0" y="256" text-anchor="middle">50</text>
<line x1="290.0" y1="240" x2="290.0" y2="244" stroke="currentColor"/><text x="290.0" y="256" text-anchor="middle">100</text>
<line x1="400.0" y1="240" x2="400.0" y2="244" stroke="currentColor"/><text x="400.0" y="256" text-anchor="middle">0</text>
<line x1="525.0" y1="240" x2="525.0" y2="244" stroke="currentColor"/><text x="525.0" y="256" text-anchor="middle">4</text>
<line x1="650.0" y1="240" x2="650.0" y2="244" stroke="currentColor"/><text x="650.0" y="256" text-anchor="middle">8</text>
<rect x="130" y="266" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75"/><text x="147" y="276">커널 모듈</text>
<rect x="220" y="266" width="12" height="12" fill="#e08a3c" fill-opacity="0.75"/><text x="237" y="276">유저가 따로 써야 하는 코드 (헤더 + CLI)</text>
<rect x="490" y="266" width="12" height="12" fill="#3f9a6b" fill-opacity="0.75"/><text x="507" y="276">name, temp1_input …</text>
</g>
</svg>
```
그림 6 — 이 노트에서 만든 네 드라이버의 실측. 왼쪽은 예제 8의 코드 줄 수(파랑 = 커널 모듈, 주황 = 유저가 따로 짜야 하는 코드), 오른쪽은 예제 4·7·9에서 `ls`로 센 **표준 sysfs 속성 수**(`dev`·`uevent`·`power`·`subsystem`·`device` 같은 모든 장치 공통 항목은 뺐다).

#### 유저가 받는 것 — 표

| 항목 | (a) misc + ioctl | (b) hwmon | 확인 방법 |
|---|---|---|---|
| 커널 코드 줄 | 88 | 85 | 예제 8 |
| 유저가 따로 쓸 코드 | 헤더 12 + CLI 21 (소비자마다 복제) | 0 | 예제 8 |
| 읽는 방법 | `ioctl(I1_THERMO_GET)` 전용 프로그램 | `cat temp1_input`, 셸, 모든 hwmon 툴 | 예제 7 |
| 표준 sysfs 속성 | 0 | 5 (`name`, `temp1_input/max/crit/label`) | 예제 7 |
| 접근 권한 | `0600` root (udev 규칙 별도) | 읽기 `0444`, 쓰기 속성만 `0644` | 예제 7 |
| 동시성 | 드라이버가 mutex 직접 | core `hwdev->lock` | `hwmon.c:435`, `:487` |
| 단위·포맷 | 드라이버가 정의 (문서화 필요) | 표준 (밀리 °C, 10진 정수) | `sysfs-interface.rst` |
| 알람 이벤트 | 직접 poll/fasync 구현 필요 | `hwmon_notify_event()`가 `sysfs_notify` + uevent | `hwmon.c:772-795` |
| thermal 연동 | 불가 (직접 thermal zone 등록) | `HWMON_C_REGISTER_TZ` + DT로 thermal zone 자동 | `hwmon.c:229` (이 노트에서 실행 안 함, DT 노드 필요) |
| 32/64비트 호환 | `compat_ioctl` 직접 (구조체 레이아웃 주의) | 문제 없음 (텍스트) | 코드 |
| ABI 부담 | 새 ioctl ABI를 영원히 유지 | 기존 ABI 재사용 | — |
| 업스트림 리뷰 | "use hwmon" 리젝 거의 확실 | 정상 경로 | — |

---

## 5. RTC provider — `rtc-test`와 30줄짜리 내 RTC

### 5.1 내 RTC 드라이버 (공통 뼈대 제외 전부)

"하드웨어"는 시스템 시계에 대한 오프셋 하나다. 실제 RTC 칩이라면 `i1_read_time`이 I2C로 BCD 레지스터 7개를 읽어 `struct rtc_time`을 채우는 자리다(`rtc-ds1307.c`, `rtc-pcf8563.c`가 그 모양이다).

```c
// minimal RTC provider: "hardware" = offset from the system clock
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/timekeeping.h>

static time64_t offset;			/* what a real chip keeps in its counter */

static int i1_read_time(struct device *dev, struct rtc_time *tm)
{
	rtc_time64_to_tm(ktime_get_real_seconds() + offset, tm);
	return 0;
}

static int i1_set_time(struct device *dev, struct rtc_time *tm)
{
	offset = rtc_tm_to_time64(tm) - ktime_get_real_seconds();
	dev_info(dev, "set_time %ptR (offset %lld s)\n", tm, offset);
	return 0;
}

static const struct rtc_class_ops i1_rtc_ops = {
	.read_time = i1_read_time,
	.set_time = i1_set_time,
};

static int i1_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc = devm_rtc_allocate_device(&pdev->dev);

	if (IS_ERR(rtc))
		return PTR_ERR(rtc);
	rtc->ops = &i1_rtc_ops;
	rtc->range_max = U32_MAX;
	offset = 3600;			/* start 1 h ahead so we can tell it apart */
	return devm_rtc_register_device(rtc);
}
/* + 공통 뼈대 (§0.1), MODULE_DESCRIPTION */
```

코드를 읽는 법: probe는 §1.3의 네 단계 그대로다. `range_max`는 "이 칩이 표현할 수 있는 최대 시각(초)"이다. core는 이 범위 밖의 `set_time`을 드라이버에 넘기기 전에 거부한다. 그리고 범위가 0이 아니면 sysfs `range` 속성을 보여 준다(§1.5). 오프셋을 3600으로 시작한 것은 다른 RTC와 구분하려는 장치일 뿐이다.

비교 대상인 커널의 `drivers/rtc/rtc-test.c`(201줄)는 같은 구조에 알람 타이머를 더한 것이다. 모듈 init에서 platform 장치 3개를 만들고(`:160-167`), probe에서 `id == 0`이면 알람 없는 ops(`test_rtc_ops_noalm`), 나머지는 알람 있는 ops를 주고 `device_init_wakeup()`을 부른다(`:129-136`). 이 차이가 sysfs에 어떻게 드러나는지 보자.

### 5.2 예제 9 — `rtc-test` 3개 + `i1_rtc`, 그리고 손대지 않은 `hwclock`

무엇을 확인하나: ① 등록 때 나오는 uevent, ② `/sys/class/rtc`와 `/dev/rtcN`이 core에 의해 같은 major(`250 rtc`)로 만들어지는지, ③ 알람 유무에 따라 sysfs 속성이 달라지는지, ④ BusyBox `hwclock`이 수정 없이 모든 RTC에서 동작하는지, ⑤ `hwclock -w`(시스템 시간 → RTC)가 내 `set_time`까지 내려오는지.

게스트 스크립트 `s1_rtc.sh`:

```sh
echo "### uevents during modprobe rtc-test + insmod i1_rtc"
/i1/uev 2 > /tmp/u & sleep 0.3
modprobe rtc-test
insmod /i1/i1_rtc.ko
wait; cat /tmp/u
echo "### class"
for r in /sys/class/rtc/rtc*; do echo "$(basename $r): name='$(cat $r/name)' dev=$(cat $r/dev) $(cat $r/date) $(cat $r/time)"; done
ls -l /dev/rtc*
grep rtc /proc/devices
I=$(grep -l i1_rtc /sys/class/rtc/rtc*/name | xargs dirname); T=/sys/class/rtc/rtc2
echo "### attrs i1 ($I)"; ls $I
echo "### attrs rtc-test ($T)"; ls $T
echo "### hwclock (unmodified BusyBox) on every rtc"
date -u
for d in /dev/rtc*; do echo "$d: $(hwclock -r -u -f $d)"; done
D=/dev/$(basename $I)
echo "### hwclock -w -u -f $D (systohc into our driver)"
hwclock -w -u -f $D && hwclock -r -u -f $D
dmesg | grep -E "i1_rtc|rtc-test" | tail -6
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.elinux_scratch/I1/boot.sh s1_rtc.sh; cat /var/tmp/I1/out_s1_rtc.txt'
```

```text
lab initramfs: 6.18.55-sub up 0.91s
### uevents during modprobe rtc-test + insmod i1_rtc
add    drivers   /bus/platform/drivers/rtc-test
add    platform  /devices/platform/rtc-test.0
add    rtc       /devices/platform/rtc-test.0/rtc/rtc1
bind   platform  /devices/platform/rtc-test.0
add    platform  /devices/platform/rtc-test.1
change platform  /devices/platform/rtc-test.1
add    wakeup    /devices/platform/rtc-test.1/wakeup/wakeup2
add    rtc       /devices/platform/rtc-test.1/rtc/rtc2
bind   platform  /devices/platform/rtc-test.1
add    platform  /devices/platform/rtc-test.2
change platform  /devices/platform/rtc-test.2
add    wakeup    /devices/platform/rtc-test.2/wakeup/wakeup3
add    rtc       /devices/platform/rtc-test.2/rtc/rtc3
bind   platform  /devices/platform/rtc-test.2
add    module    /module/rtc_test
add    drivers   /bus/platform/drivers/i1_rtc
add    platform  /devices/platform/i1_rtc
add    rtc       /devices/platform/i1_rtc/rtc/rtc4
bind   platform  /devices/platform/i1_rtc
add    module    /module/i1_rtc
### class
rtc0: name='rtc-pl031 9010000.pl031' dev=250:0 2026-10-11 23:15:45
rtc1: name='rtc-test rtc-test.0' dev=250:1 2026-10-11 23:15:45
rtc2: name='rtc-test rtc-test.1' dev=250:2 2026-10-11 23:15:45
rtc3: name='rtc-test rtc-test.2' dev=250:3 2026-10-11 23:15:45
rtc4: name='i1_rtc i1_rtc' dev=250:4 2026-10-12 00:15:45
crw-------    1 0        0         250,   0 Jan  1  1970 /dev/rtc0
crw-------    1 0        0         250,   1 Oct 11 23:15 /dev/rtc1
crw-------    1 0        0         250,   2 Oct 11 23:15 /dev/rtc2
crw-------    1 0        0         250,   3 Oct 11 23:15 /dev/rtc3
crw-------    1 0        0         250,   4 Oct 11 23:15 /dev/rtc4
250 rtc
### attrs i1 (/sys/class/rtc/rtc4)
date           hctosys        power          subsystem
dev            max_user_freq  range          time
device         name           since_epoch    uevent
### attrs rtc-test (/sys/class/rtc/rtc2)
date           hctosys        power          time
dev            max_user_freq  since_epoch    uevent
device         name           subsystem      wakealarm
### hwclock (unmodified BusyBox) on every rtc
Sun Oct 11 23:15:45 UTC 2026
/dev/rtc0: Sun Oct 11 23:15:45 2026  0.000000 seconds
/dev/rtc1: Sun Oct 11 23:15:45 2026  0.000000 seconds
/dev/rtc2: Sun Oct 11 23:15:45 2026  0.000000 seconds
/dev/rtc3: Sun Oct 11 23:15:45 2026  0.000000 seconds
/dev/rtc4: Mon Oct 12 00:15:45 2026  0.000000 seconds
### hwclock -w -u -f /dev/rtc4 (systohc into our driver)
Sun Oct 11 23:15:45 2026  0.000000 seconds
[    1.378728] rtc-test rtc-test.0: registered as rtc1
[    1.381868] rtc-test rtc-test.1: registered as rtc2
[    1.384270] rtc-test rtc-test.2: registered as rtc3
[    1.396258] i1_rtc: loading out-of-tree module taints kernel.
[    1.399065] i1_rtc i1_rtc: registered as rtc4
[    4.090398] i1_rtc i1_rtc: set_time 2026-10-11T23:15:45 (offset 0 s)
[    4.176708] reboot: Power down
```

(시각은 실행마다 다르다. 게스트 시계는 QEMU pl031 RTC(`rtc0`)에서 부팅 때 읽은 값이다.)

출력에서 볼 것:

- **uevent**: `add rtc …/rtc/rtcN`이 RTC마다 하나씩. `rtc-test.1`, `.2`에는 `change platform`과 `add wakeup …`이 더 있다. probe의 `device_init_wakeup(&plat_dev->dev, true)`가 부모 장치를 "깨울 수 있는 장치"로 표시하면서 wakeup source 객체가 생긴 것이다. `rtc-test.0`과 `i1_rtc`에는 없다.
- **같은 major 250, minor = rtc 번호**. 다섯 RTC가 전부 core가 만든 `250 rtc` 아래에 있다. 드라이버는 `alloc_chrdev_region`을 한 번도 부르지 않았다. `name` 속성은 core가 `"<드라이버 이름> <부모 장치 이름>"`으로 채운다.
- **capability → 속성**: `i1_rtc`(rtc4)에는 `range`가 있고 `wakealarm`이 없다(`range_max`를 줬고 `set_alarm`이 없음). `rtc-test.1`(rtc2)은 반대로 `wakealarm`이 있고 `range`가 없다(알람 + wakeup, 범위 미지정). §1.5의 `rtc_attr_is_visible()` 그대로다. 공통 항목을 뺀 표준 속성은 rtc4가 7개(`date`, `time`, `since_epoch`, `name`, `hctosys`, `max_user_freq`, `range`)다.
- **`hwclock`은 수정 없이** 다섯 RTC 모두에서 동작했다. rtc4만 1시간 앞서 있는 것이 우리 드라이버의 오프셋 3600이다. `hwclock -w`는 `ioctl(RTC_SET_TIME)` → `rtc_set_time()` → 내 `i1_set_time()`으로 내려와 오프셋을 0으로 맞췄고(dmesg의 `set_time … (offset 0 s)`), 다시 읽은 값이 시스템 시간과 같아졌다.
- **함정 하나(실제로 밟았다)**: 처음에는 `dev_info`에 `%ptRr`을 썼는데 출력이 `set_time 0126-09-11T23:12:18`처럼 나왔다. `r` 플래그는 `struct rtc_time`을 **변환 없이 raw**(년 − 1900, 월 0부터)로 찍는다. 보통은 `%ptR`이 맞다(`Documentation/core-api/printk-formats.rst`).

---

## 6. LED provider — 트리거와 PM을 공짜로

### 6.1 `i1_led` 코드 (공통 뼈대 제외 전부)

§3의 예제 4~6에서 쓴 드라이버다. 모듈 파라미터로 두 op 중 하나만 등록한다.

```c
// minimal LED class provider that reports the context the core calls it in
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/sched.h>

static bool blocking;			/* 0: brightness_set, 1: _blocking */
module_param(blocking, bool, 0444);
static bool sleepbug;			/* 1: msleep() inside brightness_set */
module_param(sleepbug, bool, 0444);
static atomic_t calls = ATOMIC_INIT(0);

static void report(const char *op, enum led_brightness b)
{
	int n = atomic_inc_return(&calls);

	if (n <= 16)
		pr_info("i1_led: #%d %s(%d) ctx=%s comm=%s\n", n, op, b,
			in_hardirq() ? "hardirq" : in_serving_softirq() ?
			"softirq" : "task", current->comm);
}

static void i1_set(struct led_classdev *cdev, enum led_brightness b)
{
	report("brightness_set", b);
	if (sleepbug)
		msleep(1);		/* BUG: this op must not sleep */
}

static int i1_set_blocking(struct led_classdev *cdev, enum led_brightness b)
{
	report("brightness_set_blocking", b);
	msleep(1);			/* fine: e.g. an I2C transfer */
	return 0;
}

static struct led_classdev i1_led = {
	.name = "i1:green:status",
	.max_brightness = 1,
	.flags = LED_CORE_SUSPENDRESUME,
};

static int i1_led_probe(struct platform_device *pdev)
{
	if (blocking)
		i1_led.brightness_set_blocking = i1_set_blocking;
	else
		i1_led.brightness_set = i1_set;
	return devm_led_classdev_register(&pdev->dev, &i1_led);
}
/* + 공통 뼈대 (§0.1), MODULE_DESCRIPTION */
```

코드를 읽는 법:

- LED 이름 `i1:green:status`는 `장치:색:기능` 관례다(`Documentation/leds/leds-class.rst`). 실제 드라이버는 DT의 `color`·`function` 속성에서 `led_classdev_register_ext()`가 이름을 만들게 한다.
- `.flags = LED_CORE_SUSPENDRESUME` 한 줄이 "suspend 때 꺼 주고 resume 때 복원해 달라"는 요청이다. 드라이버는 PM 코드를 한 줄도 쓰지 않는다.
- 보고용 `report()`와 파라미터를 빼면 실제 LED 드라이버의 본질은 `i1_set` 한 함수(실제 칩이면 `gpiod_set_value()` 한 줄)다.

예제 4의 출력에서 이 드라이버가 받은 것: `/sys/class/leds/i1:green:status/`에 `brightness`, `max_brightness`, `trigger` 3개(timer 트리거를 켜면 `delay_on`, `delay_off`가 추가), 트리거 20개 + 모듈로 추가된 `heartbeat`. **char device는 없다**. LED는 sysfs만으로 충분한 장치다.

### 6.2 예제 10 — suspend/resume을 core가 대신 처리한다

무엇을 확인하나: LED를 켠 상태로 `rtcwake -m freeze -s 3`(suspend-to-idle 3초, pl031 RTC 알람으로 깨어남)을 하면, `LED_CORE_SUSPENDRESUME`을 준 LED 드라이버가 PM 코드 없이 꺼졌다가 원래 밝기로 돌아오는지. `rtcwake`도 RTC 프레임워크의 표준 uAPI(`wakealarm`/`RTC_WKALM_SET`)를 쓰는 **기존 툴**이다.

게스트 스크립트 `s6_pm.sh`:

```sh
insmod /i1/i1_led.ko
insmod /i1/i1_thermo_misc.ko
L=/sys/class/leds/i1:green:status
echo 1 > $L/brightness
echo "### /sys/power/state: $(cat /sys/power/state)"
echo "### i1_led power/: $(ls $L/power | tr '\n' ' ')"
echo "phase: rtcwake freeze" > /dev/kmsg
rtcwake -d rtc0 -m freeze -s 3; echo "rtcwake rc=$?"
echo "brightness after resume: $(cat $L/brightness)"
dmesg | grep -E "i1_led|phase|PM:|rtcwake|Freezing|Restarting|suspend|resume" | grep -v taints
```

```sh
limactl shell elinux -- bash -lc 'TMO=60 /Users/donh/.elinux_scratch/I1/boot.sh s6_pm.sh; cat /var/tmp/I1/out_s6_pm.txt'
```

```text
lab initramfs: 6.18.55-sub up 0.74s
### /sys/power/state: freeze mem disk
### i1_led power/: autosuspend_delay_ms control runtime_active_time runtime_status runtime_suspended_time 
wakeup from "freeze" at Sun Oct 11 23:15:11 2026
rtcwake rc=0
brightness after resume: 1
[    0.039771] rcu: 	Max phase no-delay instances is 1000.
[    0.665567] PM: genpd: Disabling unused power domains
[    0.851777] i1_led: #1 brightness_set(1) ctx=task comm=sh
[    0.874653] phase: rtcwake freeze
[    0.896988] PM: suspend entry (s2idle)
[    0.901317] Freezing user space processes
[    0.903168] Freezing user space processes completed (elapsed 0.001 seconds)
[    0.903326] Freezing remaining freezable tasks
[    0.904616] Freezing remaining freezable tasks completed (elapsed 0.001 seconds)
[    0.904678] printk: Suspending console(s) (use no_console_suspend to debug)
[    0.918333] i1_led: #2 brightness_set(0) ctx=task comm=rtcwake
[    4.894114] i1_led: #3 brightness_set(1) ctx=task comm=rtcwake
[    4.905482] Restarting tasks: Starting
[    4.905927] Restarting tasks: Done
[    4.906384] PM: suspend exit
[    4.958262] reboot: Power down
```

출력에서 볼 것:

- `PM: suspend entry (s2idle)` 뒤 `#2 brightness_set(0)`: LED class의 `led_suspend()` → `led_classdev_suspend()`가 꺼 줬다. 약 4초 뒤 `#3 brightness_set(1)`: `led_resume()`이 **원래 밝기 1로 복원**했다. resume 후 `brightness`도 1이다.
- 두 호출 모두 `comm=rtcwake`, task context다. 시스템 sleep 콜백은 `echo freeze > /sys/power/state`를 한 프로세스(여기서는 `rtcwake`)의 context에서 순서대로 돈다.
- 같이 넣은 `i1_thermo_misc`는 아무것도 하지 않았다. misc class(`drivers/char/misc.c:180`)에는 `.pm`이 없다. 그 장치가 실제 센서라면 "suspend 전에 변환 중지, resume 후 레지스터 재설정"을 드라이버가 `dev_pm_ops`로 직접 써야 한다. (hwmon class도 `.pm`이 없다. hwmon 드라이버는 PM을 스스로 구현한다. "프레임워크 = PM 공짜"는 프레임워크마다 다르니 class 정의의 `.pm`을 확인하자.)
- `i1_led`의 `power/` 디렉터리에는 runtime PM 속성만 있다. 이것은 driver core가 모든 장치에 주는 것이고 프레임워크와 무관하다.

---

## 7. watchdog와 input — 유저 쪽에서 본 "표준 uAPI"

### 7.1 예제 11 — `softdog`과 표준 watchdog ioctl, `gpio-keys`의 input 장치

무엇을 확인하나: ① 랩 virt 머신의 `gpio-keys`(전원 버튼)가 input 프레임워크로 어떻게 보이는지, ② `softdog`(소프트웨어 워치독, watchdog core 사용)을 **어느 워치독에도 쓸 수 있는 표준 ioctl 프로그램**으로 조작하는 모습. 이 커널은 `CONFIG_WATCHDOG_SYSFS`가 꺼져 있어 `/sys/class/watchdog/watchdog0/`에 `timeout`·`state` 같은 속성이 없다. 그래서 ioctl로 본다.

```c
/* the standard watchdog uAPI: same code for softdog, i6300esb, imx2_wdt ... */
#include <fcntl.h>
#include <linux/watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/watchdog0";
	struct watchdog_info wi;
	int t, fd = open(dev, O_WRONLY);	/* open = start (watchdog_start) */

	if (fd < 0) { perror("open"); exit(1); }
	if (ioctl(fd, WDIOC_GETSUPPORT, &wi) < 0) { perror("GETSUPPORT"); exit(1); }
	printf("identity='%s' options=0x%x%s%s%s\n", (char *)wi.identity, wi.options,
	       wi.options & WDIOF_SETTIMEOUT ? " SETTIMEOUT" : "",
	       wi.options & WDIOF_KEEPALIVEPING ? " KEEPALIVEPING" : "",
	       wi.options & WDIOF_MAGICCLOSE ? " MAGICCLOSE" : "");
	if (ioctl(fd, WDIOC_GETTIMEOUT, &t) == 0) printf("timeout=%d s\n", t);
	t = 7;
	if (ioctl(fd, WDIOC_SETTIMEOUT, &t) == 0) printf("set timeout -> %d s\n", t);
	if (ioctl(fd, WDIOC_GETTIMELEFT, &t) == 0) printf("timeleft=%d s\n", t);
	else perror("GETTIMELEFT (optional op not implemented)");
	for (int i = 0; i < 3; i++) {
		if (ioctl(fd, WDIOC_KEEPALIVE, 0) < 0) { perror("KEEPALIVE"); exit(1); }
		printf("ping %d\n", i);
		sleep(1);
	}
	if (write(fd, "V", 1) != 1) perror("magic V");	/* magic close: stop on close */
	close(fd);
	printf("closed with magic 'V' -> watchdog stopped\n");
	return 0;
}
```

게스트 스크립트 `s7_wdt_input.sh`:

```sh
echo "### input (gpio-keys from virt DT, base kernel driver)"
cat /proc/bus/input/devices
ls /sys/class/input/input0/
echo "### watchdog (softdog)"
modprobe softdog soft_margin=5
ls /sys/class/watchdog/watchdog0
ls -l /dev/watchdog*
/i1/wdt_info /dev/watchdog0
dmesg | grep -i -E "softdog|watchdog" | tail -4
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.elinux_scratch/I1/boot.sh s7_wdt_input.sh; cat /var/tmp/I1/out_s7_wdt_input.txt'
```

```text
lab initramfs: 6.18.55-sub up 0.83s
### input (gpio-keys from virt DT, base kernel driver)
I: Bus=0019 Vendor=0001 Product=0001 Version=0100
N: Name="gpio-keys"
P: Phys=gpio-keys/input0
S: Sysfs=/devices/platform/gpio-keys/input/input0
U: Uniq=
H: Handlers=kbd event0 
B: PROP=0
B: EV=3
B: KEY=10000000000000 0

capabilities  id            name          properties    uniq
device        inhibited     phys          subsystem
event0        modalias      power         uevent
### watchdog (softdog)
dev        power      subsystem  uevent
crw-------    1 0        0          10, 130 Oct 11 23:15 /dev/watchdog
crw-------    1 0        0         245,   0 Oct 11 23:15 /dev/watchdog0
identity='Software Watchdog' options=0x8180 SETTIMEOUT KEEPALIVEPING MAGICCLOSE
timeout=5 s
set timeout -> 7 s
GETTIMELEFT (optional op not implemented): Operation not supported
ping 0
ping 1
ping 2
closed with magic 'V' -> watchdog stopped
[    0.942292] softdog: initialized. soft_noboot=0 soft_margin=5 sec soft_panic=0 (nowayout=0)
[    0.942328] softdog:              soft_reboot_cmd=<not set> soft_active_on_boot=0
[    4.010750] reboot: Power down
```

출력에서 볼 것:

- **input**: `H: Handlers=kbd event0` — 하나의 `input_dev`에 핸들러 두 개(콘솔 키보드 `kbd`, 유저용 `evdev` → `event0`)가 붙었다(§2.4). `B: EV=3`은 `EV_SYN`(비트 0)과 `EV_KEY`(비트 1), `B: KEY=10000000000000 0`은 `keybit` 비트맵에서 비트 116(`KEY_POWER`) 하나다. 드라이버(`gpio-keys`)가 선언한 capability가 그대로 공개된다. `inhibited` 속성은 "이 장치의 이벤트를 잠시 무시하라"는 표준 스위치로, 드라이버가 구현하지 않아도 core가 준다.
- **watchdog 노드 두 개**: `/dev/watchdog`(misc, `10:130`)와 `/dev/watchdog0`(watchdog class cdev, `245:0`). 둘 다 core가 만들었다(`watchdog_dev.c:1061`, `:1079`). 옛 프로그램이 쓰는 `/dev/watchdog` 이름까지 호환으로 챙겨 준다.
- `options=0x8180` = `WDIOF_KEEPALIVEPING`(0x8000) + `WDIOF_MAGICCLOSE`(0x100) + `WDIOF_SETTIMEOUT`(0x80). softdog이 `watchdog_info`에 선언한 능력이다.
- `WDIOC_GETTIMELEFT`가 `EOPNOTSUPP`: softdog은 선택 op `get_timeleft`를 채우지 않았다. core가 NULL op를 보고 표준 에러를 돌려준다(§1.3 규칙 3). 유저 프로그램은 이것을 보고 기능 없음으로 처리하면 된다.
- magic close: `'V'`를 쓰고 닫았으므로 워치독이 멈췄고 게스트가 리셋되지 않았다. `'V'` 없이 닫았다면 softdog이 7초 뒤 리부트를 걸었을 것이다(이 노트에서는 실행하지 않았다).
- 이 `wdt_info` 프로그램은 softdog 전용 코드가 한 줄도 없다. i.MX의 `imx2_wdt`, TI의 `rti_wdt`, PC의 `i6300esb`에서도 그대로 돈다. systemd의 `RuntimeWatchdogSec=`, BusyBox `watchdog` 데몬도 같은 ioctl을 쓴다.

---

## 8. 안티패턴의 비용 — "그냥 char device + ioctl로 하면 안 되나요?"

§4·§2.5의 실측을 바탕으로, 프레임워크가 있는 장치를 커스텀 char device로 만들 때 치르는 비용을 정리한다.

| 비용 | 무슨 일이 생기나 | 이 노트의 증거 |
|---|---|---|
| 유저 공간 툴 재발명 | `hwclock`, `sensors`, `evtest`, `v4l2-ctl`, `candump`, `iio_readdev`가 안 돈다. 소비자마다 전용 헤더와 CLI를 복제 | 예제 7 (`cat` 실패, `thermo_cli` 필요) |
| ABI 부담 | ioctl 번호·구조체 레이아웃은 한 번 배포하면 못 바꾼다. 32/64비트 compat, 패딩, 엔디언까지 직접 책임 | `i1_thermo.h`, `compat_ptr_ioctl` |
| 공통 로직 복제 | 락, 입력 검증, magic close, 타임아웃 관리, 버퍼링을 드라이버마다 다시 짠다 → 버그도 드라이버마다 따로 | 예제 3 (47개 중 46개가 `WDIOC_GETSUPPORT` 재구현) |
| PM 미통합 | 프레임워크가 해 주던 suspend 정리(LED 끄기, 눌린 키 해제, RTC 시간 보정)를 직접 해야 하고, 보통 잊는다 | 예제 10 (misc 쪽은 아무 일도 없음) |
| 생태계 연동 없음 | thermal zone, LED 트리거, IIO 버퍼·트리거, udev 규칙의 `SUBSYSTEM==` 매칭, systemd 워치독 | 예제 4 트리거 목록, 예제 7 uevent |
| 권한·보안 | 노드가 `0600` root로 생긴다. 읽기 전용과 쓰기 속성을 나눌 수 없고 ioctl 하나로 다 연다 | 예제 7 (`crw-------` vs `0444`/`0644`) |
| 업스트림 리뷰 | "Please use the hwmon/IIO/LED subsystem" 코멘트로 반려. 벤더 커널에만 남아 유지 비용이 계속 든다 | (경험칙 — 실행 근거 없음) |

#### 그럼 char device(misc)는 언제 쓰나

- **맞는 프레임워크가 정말 없을 때**: FPGA 커스텀 가속기, 특정 보안 칩의 독자 프로토콜, 펌웨어 메일박스처럼 "표준 의미"가 없는 장치. 이때도 설정·상태는 sysfs 속성으로 빼고, ioctl은 `Documentation/userspace-api/ioctl/ioctl-number.rst`에 번호를 등록하고, `Documentation/ABI/`에 문서를 쓴다.
- **프레임워크가 있지만 일부만 맞을 때**: 먼저 프레임워크에 넣고, 빠진 기능은 그 프레임워크의 확장 지점(RTC `param_get/param_set`, hwmon `extra_groups`, IIO 확장 속성, V4L2 컨트롤)을 쓴다. 정말 새 기능이면 프레임워크 자체를 확장하는 패치를 낸다.
- **UIO/VFIO**: 유저 공간 드라이버가 정답인 경우(DPDK류 고속 NIC, 단순 FPGA 레지스터 맵)는 char device를 직접 짜지 않고 UIO/VFIO 프레임워크를 쓴다. 이것도 결국 "프레임워크에 등록"이다.

---

## 9. 커널 안에서는 무슨 일이 일어나나

### 9.1 두 경로를 커널 함수로 따라가기

예제 9의 `hwclock -r -f /dev/rtc4`와 예제 7의 `cat temp1_input`이 내 콜백에 도착하기까지:

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g font-size="12">
    <text x="170" y="22" text-anchor="middle" font-size="13">hwclock -r -f /dev/rtc4</text>
    <text x="510" y="22" text-anchor="middle" font-size="13">cat …/hwmon0/temp1_input</text>
    <rect x="10" y="36" width="320" height="38" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="170" y="60" text-anchor="middle">ioctl(fd, RTC_RD_TIME, &amp;tm)</text>
    <rect x="350" y="36" width="320" height="38" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="510" y="60" text-anchor="middle">read(fd, buf, 4096)</text>
    <line x1="10" y1="88" x2="670" y2="88" stroke="currentColor" stroke-dasharray="5 4"/>
    <text x="664" y="102" text-anchor="end">syscall 경계 (SVC → EL1)</text>
    <rect x="10" y="110" width="320" height="38" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="170" y="134" text-anchor="middle">VFS → cdev 250:4 → rtc_dev_fops</text>
    <rect x="350" y="110" width="320" height="38" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="510" y="134" text-anchor="middle">VFS → kernfs → sysfs dev_attr_show</text>
    <rect x="10" y="166" width="320" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="170" y="190" text-anchor="middle">rtc_dev_ioctl() dev.c:203, :320</text>
    <rect x="350" y="166" width="320" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510" y="190" text-anchor="middle">hwmon_attr_show() hwmon.c:426</text>
    <rect x="10" y="222" width="320" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="170" y="246" text-anchor="middle">rtc_read_time() — ops_lock 잡음</text>
    <rect x="350" y="222" width="320" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510" y="246" text-anchor="middle">guard(mutex)(&amp;hwdev->lock)</text>
    <rect x="10" y="278" width="320" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="170" y="302" text-anchor="middle">__rtc_read_time(): NULL 검사, memset</text>
    <rect x="350" y="278" width="320" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510" y="302" text-anchor="middle">hattr->ops->read(dev, type, attr, ch)</text>
    <rect x="10" y="334" width="320" height="38" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170" y="358" text-anchor="middle">ops->read_time = i1_read_time()</text>
    <rect x="350" y="334" width="320" height="38" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="510" y="358" text-anchor="middle">th_read() → chip_read_temp()</text>
    <rect x="10" y="390" width="320" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="170" y="410" text-anchor="middle">돌아오며: offset 보정, rtc_valid_tm</text><text x="170" y="428" text-anchor="middle">tracepoint, copy_to_user</text>
    <rect x="350" y="390" width="320" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510" y="410" text-anchor="middle">돌아오며: tracepoint</text><text x="510" y="428" text-anchor="middle">sprintf("%lld\n") → 유저 버퍼</text>
  </g>
  <g stroke="currentColor">
    <line x1="170" y1="74" x2="170" y2="108" marker-end="url(#ah6)"/><line x1="170" y1="148" x2="170" y2="164" marker-end="url(#ah6)"/>
    <line x1="170" y1="204" x2="170" y2="220" marker-end="url(#ah6)"/><line x1="170" y1="260" x2="170" y2="276" marker-end="url(#ah6)"/>
    <line x1="170" y1="316" x2="170" y2="332" marker-end="url(#ah6)"/><line x1="170" y1="372" x2="170" y2="388" marker-end="url(#ah6)"/>
    <line x1="510" y1="74" x2="510" y2="108" marker-end="url(#ah6)"/><line x1="510" y1="148" x2="510" y2="164" marker-end="url(#ah6)"/>
    <line x1="510" y1="204" x2="510" y2="220" marker-end="url(#ah6)"/><line x1="510" y1="260" x2="510" y2="276" marker-end="url(#ah6)"/>
    <line x1="510" y1="316" x2="510" y2="332" marker-end="url(#ah6)"/><line x1="510" y1="372" x2="510" y2="388" marker-end="url(#ah6)"/>
  </g>
</svg>
```
그림 7 — 두 표준 uAPI 호출이 드라이버 콜백(파랑)까지 내려가는 길. 주황은 전부 프레임워크 core 코드다. 드라이버가 쓴 것은 파랑 한 칸뿐이다.

### 9.2 구조체 관계 — 프레임워크 객체 안의 `struct device`

모든 프레임워크 객체는 안에 `struct device`를 **품고** 있다(`struct rtc_device`의 `dev`, `struct input_dev`의 `dev`, `watchdog_core_data`의 `dev`, LED는 `led_cdev->dev` 포인터). 그래서 G2의 디바이스 모델 기능 — sysfs 디렉터리, uevent, class 링크, 부모-자식 관계, PM 콜백 순서 — 을 프레임워크가 자동으로 상속한다. 예제 7·9의 경로가 이것을 보여 준다.

```
/sys/devices/platform/i1_rtc/               <- platform_device (G3)  : driver binds here
/sys/devices/platform/i1_rtc/rtc/rtc4       <- rtc_device.dev        : class "rtc", devt 250:4
/sys/class/rtc/rtc4                         -> symlink to the class device above

/sys/devices/platform/i1_thermo_hwmon/hwmon/hwmon0   <- hwmon_device.dev : class "hwmon"
/sys/devices/platform/i1_led/leds/i1:green:status    <- led_cdev->dev    : class "leds"
```

ops 콜백이 `struct device *`를 받는 프레임워크(RTC, hwmon)는 **부모**(platform/I2C 장치)를 넘겨준다. `__rtc_read_time()`의 `rtc->ops->read_time(rtc->dev.parent, tm)`가 그 증거다. 그래서 드라이버는 probe에서 `dev_set_drvdata(&client->dev, priv)` 해 둔 private data를 콜백에서 `dev_get_drvdata(dev)`로 바로 꺼낸다.

### 9.3 드라이버를 쓸 때 다시 만나는 지점

| 이 노트의 개념 | 다시 나오는 곳 |
|---|---|
| 능력 선언 → core가 속성 생성 | I2 IIO `iio_chan_spec`, I5 V4L2 `v4l2_ctrl`, I6 `net_device->features` |
| op 호출 context 규칙 | I2 IIO 트리거 핸들러(threaded IRQ), I6 `ndo_start_xmit`(softirq, 잠들기 금지), I10 CAN |
| core가 만드는 char device | I2 `/dev/iio:deviceN`, I5 `/dev/videoN`, I7 `/dev/snd/*` |
| 같은 칩을 여러 프레임워크로 쪼개기 | P 모듈 PMIC(MFD → regulator + rtc + watchdog + input) |
| `devm_` 등록과 해제 순서 | G3, H 모듈 전반 |

---

## 10. Embedded 실무에서 다시 보기

### 10.1 장면 1 — "IMU 데이터를 앱에 주는 char 드라이버 하나 짜 주세요"

센서 키트 프로젝트에서 앱 팀이 이렇게 요청한다. G1 §12.1에서 결론(IIO)은 냈다. 이 노트의 관점으로 **설득하는 말**을 준비하자. "char + ioctl로 하면 앱 팀이 전용 라이브러리를 유지해야 하고, ROS 드라이버·libiio·`iio_readdev`를 못 씁니다. IIO로 하면 타임스탬프·FIFO·트리거·버퍼 poll이 core에서 공짜고, 앱은 `/dev/iio:deviceN`에서 표준 포맷으로 읽습니다. 드라이버 코드는 오히려 줄어듭니다(레지스터 → `read_raw` + 버퍼 push만)." 예제 7의 숫자(커널 코드는 같고 유저 코드 33줄이 0줄)가 근거다.

### 10.2 장면 2 — I2C LED 드라이버가 가끔 커널을 죽인다

보드에 I2C LED 드라이버 칩(PCA9532 계열)을 붙였고 `heartbeat` 트리거를 켜면 가끔 `BUG: scheduling while atomic`이 난다. 원인은 예제 6 그대로다. 누군가 I2C 전송(잠드는 함수)을 `brightness_set`에 넣었다. 고치는 법은 그 함수를 `brightness_set_blocking`으로 옮기는 것이다(예제 5처럼 core가 kworker로 옮겨 준다). 리뷰 체크리스트에 "ops 콜백 안에서 잠드는 함수(`i2c_*`, `regmap_*` on I2C/SPI, `mutex_lock`, `msleep`)를 부르나? 그 op의 context 규칙은?"을 넣자.

```c
/* 고친 모양 (실행 안 함 — 모양만): regmap은 I2C에서 잠든다 */
static int pca_led_set(struct led_classdev *cdev, enum led_brightness b)
{
	struct pca_led *led = container_of(cdev, struct pca_led, cdev);

	return regmap_update_bits(led->map, LS_REG(led->id),
				  LS_MASK(led->id), b ? LS_ON(led->id) : 0);
}
	/* probe: */
	led->cdev.brightness_set_blocking = pca_led_set;   /* NOT .brightness_set */
```

(`LS_REG` 등은 설명용 매크로 이름이다. 실제 `leds-pca9532.c`의 구현과는 다르다.)

### 10.3 장면 3 — PMIC 하나가 프레임워크 네 개

i.MX8·AM62 보드의 PMIC는 레귤레이터 + RTC + 워치독 + 전원 버튼을 한 칩에 갖는다. 드라이버도 한 덩어리 char device가 아니라 **MFD 코어 드라이버가 자식 platform 장치들을 만들고**, 각 자식이 regulator, `rtc_class_ops`, `watchdog_ops`, `input_dev`로 해당 프레임워크에 등록한다(예: `rtc-rk808.ko`, `rtc-da9063.ko`, `rtc-tps6594.ko`가 modules-sub에 있다). 그러면 `hwclock`, systemd 워치독, `evtest`가 PMIC 기능을 각각 표준으로 쓴다. "칩 하나 = 드라이버 하나 = char device 하나"라는 펌웨어식 사고를 버려야 하는 지점이다.

### 10.4 장면 4 — 하드웨어 워치독과 systemd

생산 장비는 HW 워치독으로 행(hang)을 잡는다. 드라이버는 `watchdog_ops`의 `start`/`stop`/`ping`과 `max_hw_heartbeat_ms`만 준다. 유저 공간은 systemd `RuntimeWatchdogSec=30s` 한 줄로 `/dev/watchdog0`을 열고 킥한다. HW 최대 타임아웃이 2초밖에 안 되는 칩이어도 core의 `watchdogd` FIFO 워커가 그 사이를 메워 준다(§2.5). 예제 3의 옛 드라이버처럼 misc로 짰다면 이 "짧은 HW 타임아웃 보정"을 칩마다 다시 짜야 했다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 프레임워크가 있는 장치를 misc+ioctl로 구현 | 리뷰 반려, 앱 팀이 전용 라이브러리 유지, 표준 툴 불가 | 프레임워크 결정 생략 | G1 결정표 → 해당 프레임워크 ops로 재작성 (예제 7) |
| `brightness_set`에서 I2C/`msleep`/mutex | 트리거를 켜면 `BUG: scheduling while atomic`, panic | op의 context 규칙(아무 context) 위반 | `brightness_set_blocking`으로 옮김 (예제 5·6) |
| 등록을 probe 중간에 함 | 부팅 직후 가끔 NULL 역참조, 초기화 안 된 레지스터 읽기 | 등록 순간 core가 콜백을 부를 수 있음 (hctosys, 트리거 기본값) | 하드웨어·private data 준비 후 마지막에 등록 |
| core가 이미 잡는 락을 콜백 안에서 또 잡으려 함 (같은 락) | deadlock, `hung_task` 경고 | core 락(`ops_lock`, `wd_data->lock`)을 모름 | §3.2 표 확인, 드라이버 락은 HW 공유 자원에만 |
| `rtc_time`을 raw로 다룸 (`tm_year`에 2026, `tm_mon`에 10) | `hwclock`이 엉뚱한 날짜, `-EINVAL` | `tm_year`는 1900 기준, `tm_mon`은 0부터 | `rtc_time64_to_tm()`/`rtc_tm_to_time64()` 사용, 로그는 `%ptR` (예제 9의 `%ptRr` 함정) |
| hwmon 값을 °C·V 단위로 돌려줌 | `sensors`가 0.042 °C로 표시 | 표준 단위는 밀리 °C, 밀리 V | `sysfs-interface.rst` 단위 확인 |
| 필수 op 누락 (RTC `read_time`, watchdog `start`) | 등록 실패 `-EINVAL` 또는 ioctl이 `-EINVAL`/`-EOPNOTSUPP` | core가 NULL 검사 | ops 구조체 주석의 "mandatory" 확인 |
| `devm_` 등록과 수동 해제 혼용 | rmmod 때 double free, use-after-free | devm이 이미 해제 | 등록이 devm이면 remove에서 unregister 하지 않음 |
| input에서 `input_sync()` 누락 | evtest에 이벤트가 늦게/묶여서 보임 | `EV_SYN`이 "한 프레임 끝" 신호 | 보고 묶음마다 `input_sync()` |
| WDT 드라이버가 magic close를 직접 구현 | core와 이중 처리, 닫아도 안 멈춤 | 옛 드라이버를 베낌 | watchdog core에 맡기고 `WDIOF_MAGICCLOSE`만 선언 (예제 3) |

---

## 12. 면접에서 이렇게 말한다

**Q.** "Why shouldn't I just write a char device with an ioctl for my temperature sensor?"

**A.** char device를 직접 만들면 커널 코드는 비슷하지만 나머지 전부를 새로 만들어야 한다. 전용 ioctl ABI(영원히 유지), 전용 유저 툴, 락, 권한, PM, 문서. hwmon에 등록하면 `temp1_input` 같은 표준 sysfs, `sensors`·thermal 연동, core 락을 공짜로 받는다. 실제로 같은 온도계를 둘 다 짜 보니 커널 쪽은 88줄 대 85줄로 같았고, 유저 쪽은 33줄 대 0줄이었다.

> A framework gives you the user-space ABI, tooling, locking and power management for free. If I write a misc device with a private ioctl, the kernel code is about the same size — I measured 88 versus 85 lines for the same thermometer — but every consumer needs my header and my CLI, I own a new ABI forever, and the patch would be rejected upstream with "please use hwmon". With hwmon, `cat temp1_input` and lm-sensors just work.

**꼬리 질문**: "그럼 char device는 언제 쓰나?" → 맞는 프레임워크가 없을 때(FPGA 커스텀 가속기 등). 그때도 misc + sysfs + `Documentation/ABI` 문서, ioctl 번호 등록.

**Q.** "What does a typical subsystem driver look like?"

**A.** 네 단계다. 프레임워크 객체 할당(`devm_rtc_allocate_device`), ops 테이블과 능력 채우기, probe 마지막에 등록, 그 뒤로는 core가 내 콜백을 부른다. core는 등록 때 class 장치·sysfs·cdev·uevent를 만들고, 호출 때 락을 잡고 입력을 검증한다.

> Allocate the framework object, fill in a const ops table and capability fields, register at the very end of probe, and from then on the core calls you. Registration creates the class device, sysfs attributes, the char device if there is one, and the uevent. On every call the core takes its own lock, validates input and then invokes my op — for RTC that's `ops_lock` around `read_time`.

**Q.** "In which context is an LED `brightness_set` callback called? What if your LED is on I2C?"

**A.** `brightness_set`은 아무 context에서나 불린다. 트리거 타이머에서는 softirq다. I2C 전송은 잠들기 때문에 `brightness_set_blocking`을 써야 하고, 그러면 core가 호출을 ordered 워크큐의 kworker로 옮긴다. 실제로 `brightness_set`에서 `msleep`을 했더니 timer 트리거 첫 호출에서 `scheduling while atomic`이 나고 panic이 났다.

> `brightness_set` must not sleep because triggers call it from timer softirq — I saw `swapper/0` in softirq context in my test. For an I2C LED you implement `brightness_set_blocking` instead; the LED core then defers the call to its ordered `leds` workqueue, so it runs in a kworker. Sleeping in the non-blocking op gave me "BUG: scheduling while atomic" with `led_timer_function` on the stack.

**꼬리 질문**: "어떻게 확인했나?" → 콜백 안에서 `in_serving_softirq()`와 `current->comm`을 찍었다. 디버그 커널이라면 `CONFIG_DEBUG_ATOMIC_SLEEP`이 `might_sleep()`으로 미리 잡는다.

**Q.** "What locking does the framework give you, and what do you still need?"

**A.** core가 장치 단위로 op 호출을 직렬화하는 경우가 많다. RTC는 `ops_lock`, watchdog은 `wd_data->lock`, hwmon(6.18)은 `hwdev->lock`. 그래서 같은 장치의 op끼리는 겹치지 않는다. 드라이버 락이 필요한 것은 IRQ 핸들러와 공유하는 상태나, 한 칩을 여러 프레임워크가 공유할 때(MFD)다.

> Most small frameworks serialize ops per device — RTC holds `ops_lock`, watchdog holds `wd_data->lock`, hwmon holds its device mutex. I still need my own lock for state shared with my IRQ handler, or for a register bus shared between MFD children; regmap usually provides that.

**Q.** "How does user space find and use an RTC you wrote, without any code from you?"

**A.** core가 `/dev/rtcN`(major는 rtc class 하나)과 `/sys/class/rtc/rtcN`을 만들고 uevent를 보낸다. `hwclock`은 표준 `RTC_RD_TIME`/`RTC_SET_TIME` ioctl을 쓰므로 그대로 동작한다. 30줄짜리 드라이버로 확인했다.

> The RTC core creates `/dev/rtcN` and the sysfs directory and emits a uevent; udev or mdev creates the node. `hwclock` uses the standard `RTC_RD_TIME` and `RTC_SET_TIME` ioctls, which the core translates into my `read_time` and `set_time`. I verified `hwclock -w` landing in my `set_time` with a thirty-line provider.

**Q.** "What is the watchdog core actually doing for you?"

**A.** `/dev/watchdogN`와 호환용 `/dev/watchdog`를 만들고, open = 시작, write/ioctl = 킥, magic close, `WDIOC_*` ioctl 전부, nowayout, HW 타임아웃이 짧을 때 hrtimer + FIFO kthread로 대신 킥하기까지 한다. 6.18 소스에서 옛 misc 방식 워치독 47개 중 46개가 `WDIOC_GETSUPPORT`를 직접 구현했고 core를 쓰는 130개는 0개다.

> It owns the whole user ABI — open starts it, writes and `WDIOC_KEEPALIVE` ping it, magic close, nowayout, timeouts — and if the hardware's max timeout is shorter than what user space asked for, it keeps the hardware alive with an hrtimer and a SCHED_FIFO kthread. In 6.18, 46 of the 47 legacy misc-based watchdog drivers re-implement `WDIOC_GETSUPPORT`; none of the 130 core-based ones do.

**Q.** "Why is `/dev/input/event0` not created by the input driver?"

**A.** input은 드라이버와 유저 인터페이스 사이에 핸들러 계층이 있다. 드라이버는 `input_dev`만 등록하고, evdev 핸들러가 붙으면서 eventN char device를 만든다. 그래서 한 버튼이 콘솔(kbd)과 evdev에 동시에 연결된다.

> Input has a handler layer: the driver registers an `input_dev`, and handlers such as evdev, kbd or joydev attach to it. evdev's connect callback creates the `eventN` char device, so the same button can feed the console keyboard handler and user space at the same time.

---

## 13. 직접 해보기

1. **개념**: §1.2의 네 부품을 IIO(I2)에 대응시켜 보라. 장치 객체, ops 구조체, 등록 API, 유저가 보는 경로. 정답/힌트: `struct iio_dev` + `struct iio_info`(`read_raw`) + `devm_iio_device_register()` + `/sys/bus/iio/devices/iio:deviceN/`, `/dev/iio:deviceN`.
2. **코드**: `i1_thermo_hwmon`에 두 번째 채널(`in0_input`, 보드 전압 밀리볼트)을 추가하라. 바꿔야 하는 것은 `HWMON_CHANNEL_INFO` 한 줄과 `th_read`의 `type` 분기뿐인지 확인하라. 정답/힌트: `HWMON_CHANNEL_INFO(in, HWMON_I_INPUT)`, `th_read`에서 `type == hwmon_in` 분기. 게스트에서 `ls` 하면 `in0_input`이 생긴다.
3. **코드**: `i1_rtc`에 `read_offset`/`set_offset`을 추가하고 sysfs `offset` 속성이 나타나는지 보라. 정답/힌트: `rtc_attr_is_visible()`이 `ops->set_offset` 유무로 `offset`을 보이거나 숨긴다(§1.5).
4. **관찰**: 예제 4를 `echo disk-activity > trigger`로 바꾸고 `dd if=/dev/vda …`(virtio 디스크를 붙여서) 하면 `brightness_set`이 어떤 context에서 불리는지 찍어 보라. 정답/힌트: 블록 요청 완료 경로라서 softirq 또는 hardirq가 나올 수 있다. 그래서 "아무 context"가 규칙이다.
5. **관찰**: 예제 6을 디버그 커널(`Image-dbg`, `linux-build-dbg`로 다시 빌드)에서 `sleepbug=1`로 돌리되, 트리거 없이 `echo 1 > brightness`만 하라. 경고가 나오나? 정답/힌트: sysfs 경로는 task context라 `might_sleep()` 경고는 안 나온다. 디버그 옵션도 "그 순간의 context"만 본다. op 규칙은 사람이 지켜야 한다.
6. **소스 읽기**: `drivers/watchdog/acquirewdt.c`와 `softdog.c`를 나란히 열고, 옛 드라이버에만 있는 코드(`expect_close`, `case WDIOC_*`, `misc_register`)를 표로 정리하라. 정답/힌트: 그 줄들이 전부 `watchdog_dev.c`로 옮겨 갔다(§2.5의 줄 번호).
7. **설계**: 센서 키트의 "FPGA 기반 타임스탬프 카운터"(레지스터 4개, 이벤트 IRQ)는 어느 프레임워크에 넣어야 하나? 맞는 게 없다면 무엇을 쓰나? 정답/힌트: PTP 하드웨어 클럭이면 `ptp_clock_info`(PTP 프레임워크, `/dev/ptpN`). 이벤트 카운터면 counter 서브시스템(`drivers/counter`). 그래도 안 맞으면 misc + sysfs + ABI 문서.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| 서브시스템 / 프레임워크 | subsystem, framework | 한 종류 장치의 공통 코드(core)와 표준 uAPI 묶음. RTC, LED, input, hwmon, IIO … |
| core | 프레임워크 공통 코드 | 유저 인터페이스, 락, PM, 공통 로직을 갖고 드라이버 콜백을 부르는 쪽 |
| ops 구조체 | operations table | core가 부를 함수 포인터 테이블. `rtc_class_ops`, `watchdog_ops`, `hwmon_ops` |
| provider 드라이버 | 프레임워크에 장치를 공급하는 드라이버 | 칩 고유 부분만 구현하고 등록하는 쪽 (consumer의 반대) |
| class | `struct class` | `/sys/class/<이름>/` 아래 같은 종류 장치를 모으는 디바이스 모델 개념 (G2) |
| uAPI | user-space API | 유저가 쓰는 커널 인터페이스: 노드 이름, ioctl, sysfs 속성, 이벤트 포맷 |
| ABI | application binary interface | uAPI의 바이너리 형태(구조체 레이아웃, ioctl 번호). 한 번 공개하면 못 깬다 |
| capability 선언 | 능력 비트·필드 | `evbit`, `HWMON_T_*`, `WDIOF_*`, `range_max` 등 core가 uAPI 모양을 정하는 근거 |
| 제어의 역전 | inversion of control | 드라이버가 core를 부르는 게 아니라 core가 드라이버를 부르는 구조 |
| atomic context | 잠들 수 없는 실행 문맥 | hardirq, softirq, spinlock 보유 중. `preempt_count` ≠ 0 |
| softirq | 소프트웨어 인터럽트 | IRQ 끝에서 도는 지연 처리. 타이머 콜백이 여기서 돈다 |
| kworker | 워크큐 워커 스레드 | 워크큐 작업을 process context에서 실행하는 커널 스레드 |
| LED 트리거 | LED trigger | LED 밝기를 커널 이벤트(타이머, 디스크, 네트워크)에 연결하는 LED core 기능 |
| magic close | 워치독 닫기 규약 | `'V'`를 쓰고 닫아야 워치독이 멈춤. 데몬이 죽어서 닫힌 것과 구분 |
| hctosys | HW clock to system | 부팅 시 RTC 시간을 시스템 시계로 복사하는 RTC core 기능 |
| devm_ | device-managed | probe 실패·장치 제거 때 자동 해제되는 자원·등록 API |
| MFD | multi-function device | 한 칩(PMIC 등)을 기능별 자식 장치로 쪼개 각 프레임워크에 등록하는 구조 |

---

## 15. 요약 & 체크리스트

Linux 프레임워크는 펌웨어의 vtable HAL에 운영체제 기능을 얹은 것이다. 드라이버는 프레임워크 객체를 할당하고, const ops 테이블과 능력을 채우고, probe 마지막에 등록한다. 그 순간 core가 class 장치·sysfs 속성·char device·uevent를 만들고, 이후 유저의 표준 uAPI 호출을 자기 락을 잡은 채 내 콜백으로 번역한다. 콜백이 어떤 context에서 불리는지는 core가 정하며, LED `brightness_set`처럼 softirq에서 불리는 op에서 잠들면 커널이 죽는다(예제 6). 같은 온도계를 misc+ioctl과 hwmon으로 짜 보면 커널 코드는 같지만 유저가 받는 것은 0 대 표준 속성 5개, 유저 코드 33줄 대 0줄이다. watchdog 트리의 옛 드라이버 47개가 같은 ioctl을 46번 다시 구현한 것이 프레임워크가 없을 때의 비용이다. char device를 직접 만드는 것은 맞는 프레임워크가 없을 때의 마지막 수단이다.

- [ ] 프레임워크의 네 부품(core, 장치 객체 + ops, 등록 API, class + uAPI)을 그림으로 설명할 수 있다
- [ ] "할당 → 채우기 → 등록 → 콜백" 순서와, 등록이 마지막이어야 하는 이유를 말할 수 있다
- [ ] RTC·LED·input·watchdog의 ops 구조체와 등록 함수 이름을 말할 수 있다
- [ ] NULL op와 capability 선언이 uAPI(sysfs 속성, ioctl 지원 여부)를 바꾸는 예를 두 개 들 수 있다
- [ ] `brightness_set`과 `brightness_set_blocking`의 차이와 각각의 호출 context를 설명할 수 있다
- [ ] "scheduling while atomic" 스택을 읽고 어느 op가 규칙을 어겼는지 찾을 수 있다
- [ ] RTC·watchdog·hwmon core가 op 호출 때 잡는 락을 말할 수 있다
- [ ] 30줄짜리 RTC provider와 hwmon 드라이버를 짜서 `hwclock`·`cat temp1_input`으로 확인할 수 있다
- [ ] misc+ioctl 설계의 비용(ABI, 툴, PM, 락, 리뷰)을 숫자와 함께 설명할 수 있다
- [ ] input에서 `/dev/input/eventN`을 누가 만드는지(evdev 핸들러) 설명할 수 있다
- [ ] 어떤 class가 PM을 대신해 주고(RTC, LED, input) 어떤 class는 안 해 주는지(misc, hwmon) 소스에서 확인할 수 있다
- [ ] "Why not just write a char device?"에 30초 영어 답변을 할 수 있다

---

## 참고 자료

- 랩 커널 소스 6.18.55 (`/var/lab/src/linux-6.18.55`): `include/linux/rtc.h`, `drivers/rtc/class.c`, `interface.c`, `dev.c`, `sysfs.c`, `rtc-test.c` · `include/linux/leds.h`, `drivers/leds/led-class.c`, `led-core.c`, `trigger/ledtrig-heartbeat.c` · `include/linux/input.h`, `drivers/input/input.c`, `evdev.c` · `include/linux/watchdog.h`, `drivers/watchdog/watchdog_core.c`, `watchdog_dev.c`, `softdog.c`, `acquirewdt.c` · `include/linux/hwmon.h`, `drivers/hwmon/hwmon.c` · `drivers/char/misc.c`. 같은 파일을 온라인에서 보려면 [elixir.bootlin.com](https://elixir.bootlin.com/linux/v6.18/source)
- 커널 문서(랩 트리에 존재 확인): `Documentation/hwmon/hwmon-kernel-api.rst`, `Documentation/hwmon/sysfs-interface.rst`, `Documentation/leds/leds-class.rst`, `Documentation/watchdog/watchdog-kernel-api.rst`, `Documentation/input/input-programming.rst`, `Documentation/admin-guide/rtc.rst`, `Documentation/core-api/printk-formats.rst`(`%ptR`), `Documentation/userspace-api/ioctl/ioctl-number.rst`. 온라인: [docs.kernel.org](https://docs.kernel.org/)
- [The Linux Kernel Driver Model — docs.kernel.org](https://docs.kernel.org/driver-api/driver-model/index.html)
- Bootlin, "Linux kernel and driver development" 교육 슬라이드 — "Kernel frameworks for device drivers" 장 ([bootlin.com/training/kernel](https://bootlin.com/training/kernel/))
- Corbet, Rubini, Kroah-Hartman, "Linux Device Drivers, 3rd ed." — 14장 The Linux Device Model (프레임워크 API 이름은 오래됐지만 class/kobject 개념은 유효)
- 이 노트의 선행·후속: G1(프레임워크 결정표), G2(디바이스 모델), G4(char device), G7(misc), G8(sysfs), I2(IIO), I3(input), I4(hwmon), I9(LED·RTC·watchdog)
