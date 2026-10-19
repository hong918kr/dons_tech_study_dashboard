# M1. 로그 기반 디버깅 — printk 레벨, `pr_*`/`dev_*`, dynamic debug, dmesg, earlycon, loglevel

> **이 노트를 다 읽으면**: printk 한 줄이 링버퍼 → 콘솔/`dmesg`로 가는 경로(lockless ringbuffer, nbcon vs legacy 콘솔)를 그림으로 설명할 수 있다 · 로그 레벨과 `console_loglevel`·`loglevel=`·`quiet`·`ignore_loglevel`로 "어디까지 보이나"를 제어할 수 있다 · dynamic debug로 재빌드 없이 파일·함수·줄 단위 `pr_debug`를 켜고, 부팅 인자로도 켤 수 있다 · earlycon으로 콘솔 이전의 크래시를 잡고, printk storm 비용과 `%p` 해싱·`kptr_restrict`를 실측으로 설명할 수 있다
> **왜 필요한가**: 드라이버 bring-up과 필드 버그의 첫 번째 도구는 언제나 로그다. 면접에서는 "콘솔이 뜨기 전에 죽으면 어떻게 디버그하나?", "printk를 넣으면 버그가 사라진다 — 무슨 뜻인가?", "dev_dbg를 재빌드 없이 켜는 법은?"으로 나온다
> **Don 기준 난이도**: UART 로그 링버퍼, raw putc, 로그 레벨, "로그가 타이밍을 바꾼다"는 펌웨어에서 이미 몸으로 안다 / 새로 배울 부분은 printk 링버퍼의 소비자 구조(콘솔 vs 리더), nbcon 콘솔 kthread, dynamic debug 쿼리 문법, `%p` 해싱과 `kptr_restrict`, `/dev/kmsg` 레코드 형식
> **선행 노트**: E3(커널 모듈, `pr_fmt`·dyndbg 첫 맛), E4(커널 제약, Oops 첫 읽기), E5(`%pe`·`ERR_PTR`), C6(§5 earlycon vs console, tty0 함정, `quiet`), G3(플랫폼 드라이버, `dev_err_probe`) — L1(journald), L7(pstore/netconsole), M3(Oops 해독), M5(ftrace·`trace_printk`)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don의 SSD 펌웨어를 떠올려 보자. 아마 이런 구조였을 것이다: `LOG(lvl, fmt, ...)` 매크로 → RAM 링버퍼에 기록 → 백그라운드 태스크가 UART로 천천히 흘려보냄 → 크래시가 나면 링버퍼를 덤프(혹은 다음 부팅에 NOR에서 회수). 그리고 경험으로 아는 규칙도 있다: "ISR 안에서 UART로 직접 찍으면 타이밍이 망가진다", "로그 레벨을 올리면 버그가 사라진다".

Linux 커널의 `printk`도 **정확히 같은 구조**다. 차이는 규모와 표준화다:

- **생산자**: `printk()`와 그 래퍼(`pr_info`, `dev_err`, `netdev_warn` …). 어떤 컨텍스트(프로세스, softirq, hardirq, NMI)에서든 부를 수 있다.
- **저장소**: 커널 링버퍼(`printk_ringbuffer`, 5.10부터 lockless). 크기는 `CONFIG_LOG_BUF_SHIFT`(랩 커널 17 = 128 KiB) 또는 부팅 인자 `log_buf_len=`.
- **소비자 1 — 콘솔**: UART·netconsole·VT 같은 "출력 장치". 레벨 필터 `console_loglevel`을 통과한 줄만 내보낸다. 6.x에서는 콘솔이 **legacy**(호출자가 직접 찍음)와 **nbcon**(전용 kthread가 찍음) 두 종류다.
- **소비자 2 — 리더**: `/dev/kmsg`(→ `dmesg`, systemd-journald), `syslog(2)`. **레벨과 무관하게 전부** 읽는다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">printk 한 줄의 여행 — 생산자 · 링버퍼 · 두 종류의 소비자</text>
  <rect x="14" y="44" width="170" height="112" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="99" y="64" text-anchor="middle" font-size="13" font-weight="bold">생산자</text>
  <text x="99" y="84" text-anchor="middle" font-size="12">pr_info / dev_err</text>
  <text x="99" y="102" text-anchor="middle" font-size="12">netdev_warn / printk</text>
  <text x="99" y="120" text-anchor="middle" font-size="12">어느 컨텍스트든 OK</text>
  <text x="99" y="138" text-anchor="middle" font-size="12">(task, IRQ, NMI)</text>
  <rect x="14" y="196" width="170" height="64" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="99" y="218" text-anchor="middle" font-size="12">유저 공간 쓰기</text>
  <text x="99" y="238" text-anchor="middle" font-size="12">echo ... &gt; /dev/kmsg</text>
  <rect x="244" y="80" width="190" height="130" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="339" y="102" text-anchor="middle" font-size="13" font-weight="bold">printk_ringbuffer</text>
  <text x="339" y="122" text-anchor="middle" font-size="12">lockless (5.10+)</text>
  <text x="339" y="142" text-anchor="middle" font-size="12">레코드 = 레벨 + 시각</text>
  <text x="339" y="160" text-anchor="middle" font-size="12">+ caller + 텍스트</text>
  <text x="339" y="182" text-anchor="middle" font-size="12">128 KiB, 가득 차면</text>
  <text x="339" y="200" text-anchor="middle" font-size="12">오래된 것부터 덮어씀</text>
  <line x1="184" y1="110" x2="242" y2="130" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="184" y1="226" x2="242" y2="180" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="490" y="44" width="176" height="120" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="578" y="64" text-anchor="middle" font-size="13" font-weight="bold">콘솔 (출력 장치)</text>
  <text x="578" y="84" text-anchor="middle" font-size="12">레벨 &lt; console_loglevel</text>
  <text x="578" y="104" text-anchor="middle" font-size="12">nbcon: pr/ttyAMA kthread</text>
  <text x="578" y="122" text-anchor="middle" font-size="12">legacy: 호출자가 직접</text>
  <text x="578" y="142" text-anchor="middle" font-size="12">UART, netconsole,</text>
  <text x="578" y="158" text-anchor="middle" font-size="12">earlycon(bootcon)</text>
  <rect x="490" y="190" width="176" height="96" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="578" y="210" text-anchor="middle" font-size="13" font-weight="bold">리더 (전부 읽음)</text>
  <text x="578" y="230" text-anchor="middle" font-size="12">/dev/kmsg → dmesg</text>
  <text x="578" y="248" text-anchor="middle" font-size="12">systemd-journald (L1)</text>
  <text x="578" y="266" text-anchor="middle" font-size="12">syslog(2), pstore (L7)</text>
  <line x1="434" y1="120" x2="488" y2="104" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="434" y1="180" x2="488" y2="236" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="340" y="310" text-anchor="middle" font-size="12">콘솔에 안 보였다고 기록이 없는 것이 아니다 — dmesg에는 있다 (예제 1)</text>
</svg>
```
그림 1 — printk의 생산자·저장소·소비자. 콘솔은 레벨 필터를 거치고, 리더는 레벨과 무관하게 전부 읽는다. 링버퍼가 가득 차면 오래된 레코드를 덮어쓴다(§6의 "messages dropped").

Embedded Linux 엔지니어가 이 그림에서 매일 쓰는 손잡이는 다섯 개다.

| 손잡이 | 무엇을 바꾸나 | 이 노트 |
|---|---|---|
| 로그 레벨 (`pr_err` vs `pr_info`) | 메시지의 중요도 | §1 |
| `console_loglevel`, `loglevel=`, `quiet` | 콘솔로 무엇이 나가나 | §2 |
| dynamic debug | 컴파일돼 있지만 꺼진 `pr_debug`/`dev_dbg`를 켬 | §4 |
| `earlycon`, `console=`, `keep_bootcon` | 언제부터, 어디로 나가나 | §5 |
| ratelimit, devkmsg, 레벨 정책 | 로그 폭주가 시스템을 망치지 않게 | §6, §9 |

랩 환경: Mac 위 Lima VM `elinux`(Ubuntu 26.04, 호스트 커널 `7.0.0-38-generic`) 안에서 QEMU `virt`(TCG, `-cpu cortex-a57,pmu=on -smp 2 -m 1G`)로 **Linux 6.18.55-trace**(`/var/lab/out/Image-trace`, `CONFIG_PRINTK_CALLER=y`, `CONFIG_DYNAMIC_DEBUG=y`)를 부팅했다. 게스트 userspace는 BusyBox initramfs이고, util-linux `dmesg`·kmod `modprobe`가 필요한 예제는 `rootfs-trace.ext4`(Ubuntu 24.04)를 **읽기 전용으로** 붙여 chroot해서 썼다. 모든 실행은 17명이 VM을 공유하는 부하 상태의 TCG라 **시간 수치는 실제 하드웨어 값이 아니다** — 상대 비교로만 읽는다.

모듈은 세 개를 만들었다 (Kbuild 한 줄: `obj-m += m1_mod.o m1_fmt.o m1_storm.o`). 빌드는 `-Wall`이 기본인 Kbuild로 경고 0개.

```sh
make -C /var/lab/out/linux-build-trace M=$PWD -j2 modules      # vermagic 6.18.55-trace
# run.sh: BusyBox initramfs 복사 + .ko + 게스트 스크립트를 넣고 LAB_CMD=/m1/t.sh 로 부팅
qemu-system-aarch64 -machine virt -cpu cortex-a57,pmu=on -smp 2 -m 1G -nographic \
  -kernel /var/lab/out/Image-trace -initrd i.gz -append "console=ttyAMA0 quiet LAB_CMD=/m1/t.sh" \
  [-drive file=/var/lab/out/rootfs-trace.ext4,if=virtio,format=raw,readonly=on]
```

---

## 1. 로그 레벨과 `pr_*` / `dev_*` / `netdev_*`

### 1.1 직관 — 메시지마다 "중요도 바이트"가 붙는다

`printk("<3>foo")`처럼 문자열 맨 앞에 레벨을 붙이는 것이 원래 방식이었다. 지금은 `KERN_ERR` 같은 매크로가 `"\001" "3"`(SOH 문자 + 숫자 문자)로 펼쳐지고, printk는 이 두 바이트를 떼어 레코드의 `level` 필드에 넣는다. 펌웨어의 `LOG(LVL_ERR, ...)`와 같은데, 레벨이 **포맷 문자열 안에** 들어 있다는 점이 다르다.

| 레벨 | 매크로 | `pr_*` | 쓰는 곳 |
|---|---|---|---|
| 0 | `KERN_EMERG` | `pr_emerg` | 시스템을 못 쓴다 (panic 직전) |
| 1 | `KERN_ALERT` | `pr_alert` | 즉시 조치 필요 |
| 2 | `KERN_CRIT` | `pr_crit` | 치명적 HW/SW 상태 |
| 3 | `KERN_ERR` | `pr_err` | 드라이버 에러 (probe 실패, I/O 에러) |
| 4 | `KERN_WARNING` | `pr_warn` | 이상하지만 계속 동작 |
| 5 | `KERN_NOTICE` | `pr_notice` | 정상이지만 눈여겨볼 일 |
| 6 | `KERN_INFO` | `pr_info` | 정보 (장치 발견, 버전) |
| 7 | `KERN_DEBUG` | `pr_debug` | 디버그. **기본은 컴파일되지 않거나 꺼져 있다** |
| (없음) | `KERN_DEFAULT` | `printk("...")` | `default_message_loglevel`(기본 4)로 기록 |
| — | `KERN_CONT` | `pr_cont` | 직전 줄에 이어 붙임 |

`pr_debug`만 특별하다. `pr_info`까지는 `printk(KERN_INFO pr_fmt(fmt), ...)`로 바로 펼쳐지지만, `pr_debug`는 (1) `CONFIG_DYNAMIC_DEBUG=y`면 dynamic debug 사이트(기본 꺼짐), (2) 아니고 파일에 `#define DEBUG`가 있으면 `printk(KERN_DEBUG ...)`, (3) 둘 다 아니면 `no_printk()`(컴파일은 되지만 코드가 사라짐)이 된다(§4).

### 1.2 `pr_*` vs `dev_*` vs `netdev_*` — "누가" 말했나를 붙여 주는 래퍼

| 계열 | 접두어 | 언제 |
|---|---|---|
| `pr_info(fmt, ...)` | `pr_fmt(fmt)` 결과 (보통 `KBUILD_MODNAME ": "`) | `struct device`가 없을 때: 모듈 init, 코어 코드 |
| `dev_info(dev, fmt, ...)` | `"<드라이버명> <장치명>: "` | 드라이버 안 — **항상 이쪽을 우선** |
| `netdev_info(ndev, fmt, ...)` | `"<드라이버> <버스장치> <ifname>: "` | 네트워크 드라이버 |
| `dev_err_probe(dev, err, fmt, ...)` | `dev_*`와 같음 | probe의 에러 반환 경로. `-EPROBE_DEFER`면 dbg로 낮추고 이유를 기록 |

`dev_*`는 접두어만 붙이는 게 아니다. 레코드에 **dev_printk_info**(subsystem, device 이름)를 붙여 `/dev/kmsg`에 키 줄로 내보낸다. 아래 예제 1의 모듈을 넣은 뒤 `/dev/kmsg` 원시 레코드를 읽은 결과다(`grep -A2 "probe: dev_info"`):

```text
6,314,5783066,-,caller=T76;m1_dev m1_dev.0: probe: dev_info carries driver+device name
 SUBSYSTEM=platform
 DEVICE=+platform:m1_dev.0
```

journald가 이 키로 장치별 필터(`journalctl -k _KERNEL_SUBSYSTEM=platform` 등)를 한다(L1). `pr_*`로 찍으면 이 키가 없다. `pr_fmt`는 E3에서 봤듯이 파일 맨 위, `#include`보다 **먼저** 정의해야 한다. `dev_*` 계열에는 `dev_fmt`라는 짝이 있다(접두어 뒤에 붙일 문자열).

### 1.3 예제 1 — 모든 레벨, `dev_*`, ratelimit, once를 한 모듈에서

무엇을 확인하나: 레벨 0~7과 레벨 없는 printk, `pr_cont`, 플랫폼 장치 위의 `dev_*`, `pr_info_ratelimited`와 `pr_info_once`를 찍고, `console_loglevel`을 8 → 4 → 1로 바꾸며 콘솔에 무엇이 나오는지 본다.

```c
// m1_mod.c - every log level, dev_* on a platform device, ratelimit/once, pr_debug
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/platform_device.h>

static struct platform_device *pdev;
static noinline void m1_tick(int i)
{
	pr_debug("tick %d (pr_debug: dyndbg site)\n", i);
	pr_info_ratelimited("ratelimited line %d\n", i);
	pr_info_once("once line (i=%d)\n", i);
}

static int m1_probe(struct platform_device *p)
{
	dev_info(&p->dev, "probe: dev_info carries driver+device name\n");
	dev_warn(&p->dev, "probe: dev_warn\n");
	dev_dbg(&p->dev, "probe: dev_dbg (dyndbg site)\n");
	dev_err_probe(&p->dev, -EPROBE_DEFER, "dev_err_probe demo (deferral is dbg)\n");
	return 0;
}

static struct platform_driver m1_drv = {
	.probe = m1_probe, .driver = { .name = "m1_dev" },
};

static int __init m1_init(void)
{
	int i, ret;
	pr_emerg("level 0 EMERG\n");
	pr_alert("level 1 ALERT\n");
	pr_crit("level 2 CRIT\n");
	pr_err("level 3 ERR\n");
	pr_warn("level 4 WARNING\n");
	pr_notice("level 5 NOTICE\n");
	pr_info("level 6 INFO\n");
	pr_debug("level 7 DEBUG (only if DEBUG or dyndbg)\n");
	printk("no level -> default_message_loglevel");
	pr_cont(" + pr_cont\n");
	ret = platform_driver_register(&m1_drv);
	if (ret)
		return ret;
	pdev = platform_device_register_simple("m1_dev", 0, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&m1_drv);
		return PTR_ERR(pdev);
	}
	for (i = 0; i < 30; i++)
		m1_tick(i);
	return 0;
}
static void __exit m1_exit(void)
{
	m1_tick(99);	/* >5 s later: flushes the "callbacks suppressed" count */
	platform_device_unregister(pdev);
	platform_driver_unregister(&m1_drv);
}
module_init(m1_init); module_exit(m1_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("M1 log level demo");
```

게스트 스크립트(BusyBox `sh`):

```sh
#!/bin/sh
echo "== printk sysctl: $(cat /proc/sys/kernel/printk)"
for L in 8 4 1; do
  echo "== console_loglevel=$L : insmod (console output follows)"
  echo $L > /proc/sys/kernel/printk
  insmod /m1/m1_mod.ko; sleep 1
  echo 8 > /proc/sys/kernel/printk; echo "-- rmmod"; sleep 5; rmmod m1_mod
done
echo 1 > /proc/sys/kernel/printk
echo "== dmesg (buffer has everything)"; dmesg | grep -E "m1_|platform" | tail -20
```

실행 결과 (부팅 인자 `console=ttyAMA0 quiet`, 콘솔 출력 그대로. 가운데 반복은 그대로 두었다):

```text
lab initramfs: 6.18.55-trace up 14.27s
== printk sysctl: 4	4	1	7
== console_loglevel=8 : insmod (console output follows)
[   16.123039][   T78] m1_mod: loading out-of-tree module taints kernel.
[   16.123350][   T78] m1_mod: module verification failed: signature and/or required key missing - tainting kernel
[   16.241884][   T78] m1_mod: level 0 EMERG
[   16.241959][   T78] m1_mod: level 1 ALERT
[   16.241980][   T78] m1_mod: level 2 CRIT
[   16.241993][   T78] m1_mod: level 3 ERR
[   16.242006][   T78] m1_mod: level 4 WARNING
[   16.242019][   T78] m1_mod: level 5 NOTICE
[   16.242031][   T78] m1_mod: level 6 INFO
[   16.242047][   T78] no level -> default_message_loglevel + pr_cont
[   16.295180][   T78] m1_dev m1_dev.0: probe: dev_info carries driver+device name
[   16.295279][   T78] m1_dev m1_dev.0: probe: dev_warn
[   16.301219][   T78] m1_mod: ratelimited line 0
[   16.301279][   T78] m1_mod: once line (i=0)
[   16.301355][   T78] m1_mod: ratelimited line 1
[   16.301410][   T78] m1_mod: ratelimited line 2
[   16.301458][   T78] m1_mod: ratelimited line 3
[   16.301483][   T78] m1_mod: ratelimited line 4
[   16.305378][   T78] m1_mod: ratelimited line 5
[   16.305457][   T78] m1_mod: ratelimited line 6
[   16.305487][   T78] m1_mod: ratelimited line 7
[   16.305512][   T78] m1_mod: ratelimited line 8
[   16.305537][   T78] m1_mod: ratelimited line 9
-- rmmod
[   23.361669][   T81] m1_tick: 20 callbacks suppressed
[   23.361937][   T81] m1_mod: ratelimited line 99
== console_loglevel=4 : insmod (console output follows)
[   24.287823][   T82] m1_mod: level 0 EMERG
[   24.287979][   T82] m1_mod: level 1 ALERT
[   24.288037][   T82] m1_mod: level 2 CRIT
[   24.288079][   T82] m1_mod: level 3 ERR
-- rmmod
[   31.655577][   T85] m1_tick: 20 callbacks suppressed
[   31.655811][   T85] m1_mod: ratelimited line 99
== console_loglevel=1 : insmod (console output follows)
[   32.061478][   T86] m1_mod: level 0 EMERG
-- rmmod
[   38.528004][   T89] m1_tick: 20 callbacks suppressed
[   38.528211][   T89] m1_mod: ratelimited line 99
== dmesg (buffer has everything)
[   32.061620][   T86] m1_mod: level 2 CRIT
[   32.061649][   T86] m1_mod: level 3 ERR
[   32.061683][   T86] m1_mod: level 4 WARNING
[   32.061714][   T86] m1_mod: level 5 NOTICE
[   32.061739][   T86] m1_mod: level 6 INFO
[   32.103287][   T86] m1_dev m1_dev.0: probe: dev_info carries driver+device name
[   32.103383][   T86] m1_dev m1_dev.0: probe: dev_warn
[   32.103877][   T86] m1_mod: ratelimited line 0
[   32.103951][   T86] m1_mod: once line (i=0)
...  (ratelimited line 1~9)
[   38.528004][   T89] m1_tick: 20 callbacks suppressed
[   38.528211][   T89] m1_mod: ratelimited line 99
[   39.016399][   T93] reboot: Power down
```

출력에서 볼 것:

- `/proc/sys/kernel/printk`의 네 숫자 `4 4 1 7` = **console_loglevel, default_message_loglevel, minimum_console_loglevel, default_console_loglevel**. `quiet`로 부팅해서 첫 값이 4다. 규칙은 **"레벨 숫자 < console_loglevel이면 콘솔로"**. 그래서 4일 때 0~3(ERR 이상)만, 1일 때 0(EMERG)만 나왔다. 8이면 7(DEBUG)까지 전부.
- 레벨 없는 `printk`는 default 4(WARNING)로 기록됐다. console_loglevel=4에서는 안 보인다. 레벨을 빼먹으면 "의도와 다른 레벨"이 된다 — checkpatch가 경고하는 이유(E4).
- `pr_cont`가 같은 줄에 이어졌다. 다만 다른 CPU의 printk가 끼어들면 줄이 쪼개질 수 있어서, 한 줄은 **한 번의 printk로** 만드는 게 원칙이다.
- `dev_*` 줄은 `m1_dev m1_dev.0:`(드라이버 이름 + 장치 이름)으로 시작한다. 같은 드라이버가 장치 4개를 붙잡고 있을 때 어느 장치의 로그인지 바로 안다.
- `pr_debug`·`dev_dbg`는 console_loglevel=8이어도 **안 나왔다**. dyndbg 사이트가 꺼져 있기 때문이다(§4).
- `pr_info_ratelimited`는 30번 중 **10번만** 찍고, 5초 뒤 다음 호출 때 `m1_tick: 20 callbacks suppressed`를 남겼다(기본 `DEFAULT_RATELIMIT_INTERVAL` 5초, `BURST` 10). 억제된 개수는 **다음에 허용될 때** 찍히므로, 폭주가 멈춘 뒤에야 "몇 개 버려졌다"가 보인다.
- `pr_info_once`는 정적 변수라 모듈을 다시 넣으면 다시 한 번 찍힌다.
- 마지막 `dmesg`: console_loglevel=1로 콘솔에 EMERG만 나갔던 세 번째 insmod의 CRIT~INFO가 **버퍼에는 다 있다**.
- `[   T78]`은 `CONFIG_PRINTK_CALLER=y`가 붙여 주는 호출자 ID다. `T<pid>`는 태스크 컨텍스트, `C<cpu>`는 인터럽트 컨텍스트(예제 9). 여러 CPU의 줄이 섞일 때 누가 찍었는지 가른다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
  <text x="330" y="20" text-anchor="middle" font-size="14" font-weight="bold">레벨 vs console_loglevel — 예제 1 실측</text>
  <text x="60" y="48" text-anchor="middle" font-size="12">레벨</text>
  <text x="300" y="48" text-anchor="middle" font-size="12">cl=8</text>
  <text x="430" y="48" text-anchor="middle" font-size="12">cl=4 (quiet)</text>
  <text x="560" y="48" text-anchor="middle" font-size="12">cl=1</text>
  <g font-size="12">
    <text x="20" y="74">0 EMERG</text><text x="20" y="98">1 ALERT</text><text x="20" y="122">2 CRIT</text><text x="20" y="146">3 ERR</text>
    <text x="20" y="170">4 WARNING</text><text x="20" y="194">5 NOTICE</text><text x="20" y="218">6 INFO</text><text x="20" y="242">7 DEBUG</text>
  </g>
  <rect x="250" y="60" width="100" height="168" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="300" y="148" text-anchor="middle" font-size="12">콘솔 출력</text>
  <rect x="250" y="230" width="100" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="300" y="245" text-anchor="middle" font-size="12">dyndbg 꺼짐</text>
  <rect x="380" y="60" width="100" height="96" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="430" y="112" text-anchor="middle" font-size="12">콘솔 출력</text>
  <rect x="380" y="158" width="100" height="92" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="430" y="208" text-anchor="middle" font-size="12">dmesg에만</text>
  <rect x="510" y="60" width="100" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="77" text-anchor="middle" font-size="12">콘솔</text>
  <rect x="510" y="86" width="100" height="164" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="560" y="172" text-anchor="middle" font-size="12">dmesg에만</text>
  <line x1="130" y1="156" x2="640" y2="156" stroke="#d0564a" stroke-dasharray="4 3"/>
  <text x="20" y="272" font-size="12">규칙: 메시지 레벨 &lt; console_loglevel 이면 콘솔로.</text>
  <text x="20" y="290" font-size="12">링버퍼(dmesg)는 레벨과 무관하게 전부 저장한다.</text>
</svg>
```
그림 2 — 예제 1의 결과를 표로 그렸다. 빨간 점선은 `quiet`(cl=4)의 경계. 레벨 7(DEBUG)은 콘솔 레벨이 아니라 dynamic debug가 막고 있다.

### 1.4 흔한 함정

- `pr_err`를 "눈에 잘 띄라고" 정상 경로에 쓰면, 제품에서 `quiet`로 부팅해도 콘솔에 나와 115200 UART 시간을 잡아먹고(§6), 필드 로그 분석에서 진짜 에러를 묻는다.
- `dev_err(NULL, ...)`는 크래시는 안 나지만 `(NULL device *):` 접두어가 붙는다. 장치가 없으면 `pr_*`를 쓴다.
- 줄 끝 `\n`을 빼먹으면 다음 printk가 `pr_cont`처럼 붙을 수 있다(커널은 다른 호출자·레벨이면 끊지만 보장은 없다).

---

## 2. 콘솔 레벨을 정하는 손잡이 — sysctl, `loglevel=`, `quiet`, `ignore_loglevel`

### 2.1 정의

| 손잡이 | 어디서 | 효과 |
|---|---|---|
| `/proc/sys/kernel/printk` | 런타임 | 4개 값. 첫 값만 바꾸는 일이 대부분 (`echo 8 > ...`) |
| `dmesg -n 8` | 런타임 | 같은 것을 `syslog(2)` `SYSLOG_ACTION_CONSOLE_LEVEL`로 |
| `loglevel=N` | 부팅 인자 (`early_param`) | console_loglevel = N |
| `quiet` | 부팅 인자 | console_loglevel = `CONFIG_CONSOLE_LOGLEVEL_QUIET`(랩 4) |
| `debug` | 부팅 인자 | console_loglevel = 10 (전부) |
| `ignore_loglevel` | 부팅 인자, 또는 `/sys/module/printk/parameters/ignore_loglevel` | 레벨 필터 자체를 무시 (KERN_DEBUG까지 콘솔로) |
| `printk.time=0/1` | 부팅 인자, `/sys/module/printk/parameters/time` | 콘솔·syslog 출력의 타임스탬프 표시 (레코드에는 항상 시각이 있다) |
| `CONFIG_CONSOLE_LOGLEVEL_DEFAULT` | 빌드 | 인자가 없을 때 기본 (랩 7) |
| `CONFIG_MESSAGE_LOGLEVEL_DEFAULT` | 빌드 | 레벨 없는 printk의 레벨 (랩 4) |

### 2.2 예제 2 — 같은 부팅, 인자만 바꿔 콘솔 줄 수 세기

무엇을 확인하나: 같은 Image-trace + 기본 initramfs를 `LAB_CMD=true`(즉시 poweroff)로 부팅하면서 인자만 바꾸고, 호스트에서 콘솔 줄 수를 센다.

```sh
q() { local n=$1; shift; timeout 60 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G \
  -nographic -no-reboot -kernel /var/lab/out/Image-trace -initrd /var/lab/out/initramfs.cpio.gz \
  -append "$*" > e_$n.txt 2>&1; echo "$n rc=$? lines=$(grep -c . e_$n.txt)"; }
q def   "console=ttyAMA0 LAB_CMD=true"
q quiet "console=ttyAMA0 quiet LAB_CMD=true"
q ll1   "console=ttyAMA0 loglevel=1 LAB_CMD=true"
q ign   "console=ttyAMA0 quiet ignore_loglevel LAB_CMD=true"
q time0 "console=ttyAMA0 printk.time=0 quiet LAB_CMD=dmesg~-r~|~head~-3"
```

```text
def rc=0 lines=288
quiet rc=0 lines=2
ll1 rc=0 lines=2
ign rc=0 lines=300
time0 rc=0 lines=5
```

`quiet`·`loglevel=1`의 두 줄은 이것뿐이다:

```text
lab initramfs: 6.18.55-trace up 18.73s
[   22.325449][   T77] reboot: Power down
```

`ign`이 `def`보다 더 보여 준 줄 일부(`diff`로 뽑음, 타임스탬프 제거):

```text
> printk: debug: ignoring loglevel setting.
> pcpu-alloc: s104728 r8192 d30440 u143360 alloc=35*4096
> Kernel command line: console=ttyAMA0 quiet ignore_loglevel LAB_CMD=true
> audit: type=2000 audit(0.912:1): state=initialized audit_enabled=0 res=1
> libata version 3.00 loaded.
> rtc-pl031 9010000.pl031: setting system clock to 2026-10-18T06:52:20 UTC (1792306340)
> hrtimer: interrupt took 52604528 ns
```

`printk.time=0`일 때 `dmesg -r`(BusyBox, 원시 레벨 표시):

```text
<6>[    T0] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
<5>[    T0] Linux version 6.18.55-trace (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP PREEMPT Sat Oct 17 23:37:47 PDT 2026
<6>[    T0] KASLR enabled
```

출력에서 볼 것:

- 기본(cl=7)은 288줄, `quiet`는 2줄. 커널 자체 부팅 로그는 0줄이 되고 init의 echo와 `reboot: Power down`(EMERG)만 남는다. 115200 UART라면 280여 줄 × 수 ms가 부팅 시간에서 빠진다(§6에서 계산).
- `ignore_loglevel`은 `quiet`와 함께 줘도 이긴다 — 레벨 비교를 아예 건너뛰기 때문. 그래서 300줄로 `def`보다 12줄 많다. 늘어난 줄은 KERN_DEBUG(레벨 7)로 기록되는 것들이다: `mm/percpu.c`의 `pcpu-alloc`은 `printk("%spcpu-alloc: ...", lvl)`에 KERN_DEBUG를 넘기고, `with arguments:`·`/init`은 `init/main.c`의 `pr_debug`(이 파일은 기본 켜짐 — 예제 5 끝)다. (`Kernel command line` 줄은 인자 문자열이 달라서 diff에 잡힌 것이다.)
- `hrtimer: interrupt took 52604528 ns`는 공유 VM 위 TCG가 52 ms 동안 멈췄다는 뜻이다. 실제 보드에서 이 줄이 보이면 진짜 문제(타이머 인터럽트 지연)다.
- `printk.time=0`은 **출력 표시만** 끈다. 레코드에는 여전히 시각이 있다(`dmesg`·`/dev/kmsg`는 그대로 시각을 준다).

### 2.3 흔한 함정

- `loglevel=8`을 주고도 `pr_debug`가 안 보인다 → 레벨 필터가 아니라 dyndbg가 꺼져 있다(§4).
- 제품 빌드에서 `quiet`만 넣고 끝내면, 운영 중 `echo 8 > /proc/sys/kernel/printk`를 한 디버그 스크립트가 그대로 남아 콘솔 폭주로 실시간 루프가 깨질 수 있다(§6).

---

## 3. 콘솔이란 무엇인가 — `console=`, preferred console, legacy vs nbcon

### 3.1 정의

**콘솔(`struct console`)**은 "printk 레코드를 받아 어딘가로 내보내는 드라이버 쪽 콜백 묶음"이다. UART 드라이버(`amba-pl011.c`), netconsole, VT, pstore console(L7)이 각자 하나씩 등록한다. 등록은 `register_console()`.

- `console=ttyAMA0,115200` — 여러 번 줄 수 있고 **전부** 활성화된다. **마지막 것**이 `/dev/console`(PID 1의 stdin/stdout)이 된다 — "preferred console".
- `console=`이 없으면 DT의 `/chosen/stdout-path`를 쓰고, 그것도 없으면 처음 등록된 콘솔(VT `tty0`일 수 있다 — C6 예제 10의 함정).
- **bootconsole**(`CON_BOOT`): earlycon처럼 정식 드라이버 전에 쓰는 임시 콘솔. 정식 콘솔이 등록되면 해제된다(§5).

### 3.2 6.x의 큰 변화 — nbcon

옛 printk는 `printk()` 호출자가 `console_lock`을 잡고 **그 자리에서** UART에 찍었다(legacy). 그래서 printk 한 줄이 115200에서 수 ms 걸리고, 인터럽트를 끈 채 찍으면 그만큼 지연이 생겼다. PREEMPT_RT 작업의 일부로 **nbcon**(non-blocking console)이 6.x 동안 단계적으로 들어왔다. nbcon 콘솔은:

- `write_thread()` 콜백 — 콘솔 전용 kthread(`pr/<name>`)가 평소에 찍는다. printk 호출자는 링버퍼에 넣고 kthread를 깨우기만 한다.
- `write_atomic()` 콜백 — panic·Oops 같은 비상시, 또는 kthread가 아직 없는 이른 부팅 때 호출자 컨텍스트에서 직접 찍는다.

랩 소스에서 PL011이 이미 nbcon인지 확인했다:

```sh
cd /var/lab/src/linux-6.18.55
grep -n "write_atomic\|write_thread\|CON_NBCON" drivers/tty/serial/amba-pl011.c
grep -rln "CON_NBCON" drivers/tty
grep -n "\"pr/%s%d\"\|pr/legacy" kernel/printk/*.c
```

```text
drivers/tty/serial/amba-pl011.c:2548:pl011_console_write_atomic(struct console *co, struct nbcon_write_context *wctxt)
drivers/tty/serial/amba-pl011.c:2576:pl011_console_write_thread(struct console *co, struct nbcon_write_context *wctxt)
drivers/tty/serial/amba-pl011.c:2634:	.write_atomic	= pl011_console_write_atomic,
drivers/tty/serial/amba-pl011.c:2635:	.write_thread	= pl011_console_write_thread,
drivers/tty/serial/amba-pl011.c:2638:	.flags		= CON_PRINTBUFFER | CON_ANYTIME | CON_NBCON,
drivers/tty/serial/amba-pl011.c
drivers/tty/serial/imx.c
drivers/tty/serial/tegra-utc.c
drivers/tty/serial/sifive.c
drivers/tty/tty_io.c
kernel/printk/nbcon.c:1342:	kt = kthread_run(nbcon_kthread_func, con, "pr/%s%d", con->name, con->index);
kernel/printk/printk.c:3691:	kt = kthread_run(legacy_kthread_func, NULL, "pr/legacy");
```

출력에서 볼 것: 6.18에서 nbcon으로 바뀐 UART 드라이버는 PL011, i.MX(`imx.c` — i.MX8 보드!), Tegra UTC, SiFive뿐이다. 8250(TI AM62 등)은 이 버전에서 아직 legacy다. 즉 **보드마다 printk의 비용 모델이 다르다**. `pr/legacy`는 PREEMPT_RT 커널에서 legacy 콘솔까지 kthread로 미는 스레드다(N1).

부팅 로그의 문구도 이걸 드러낸다: earlycon은 `printk: legacy bootconsole [pl11] enabled`, 정식 PL011은 `printk: console [ttyAMA0] enabled`(legacy 콘솔이면 `legacy console`이라고 찍는 것으로 보인다 — 이 랩에서는 legacy 정식 콘솔을 띄워 보지 않았다). 런타임에는 `/proc/consoles`의 플래그 문자(`fs/proc/consoles.c`: E=enabled, C=preferred, B=boot, N=nbcon, p=printbuffer, a=anytime)로 본다 — 예제 8.

---

## 4. dynamic debug — 재빌드 없이 `pr_debug`/`dev_dbg` 켜기

### 4.1 직관

펌웨어에서 디버그 로그를 켜려면 `-DDEBUG_NAND`로 다시 빌드해서 다시 플래시했을 것이다. dynamic debug는 **모든 `pr_debug`/`dev_dbg` 호출 지점(site)을 커널에 컴파일해 두고, 각 사이트에 on/off 스위치**를 단다. 스위치는 `static_key`(jump label)라서 꺼져 있을 때 비용은 NOP 한 개다. 켜는 창구는 `/proc/dynamic_debug/control`(debugfs가 있으면 `/sys/kernel/debug/dynamic_debug/control`도).

### 4.2 정의 — 사이트 디스크립터와 쿼리 문법

`CONFIG_DYNAMIC_DEBUG=y`이면 `pr_debug(fmt, ...)`는 대략 이렇게 펼쳐진다: `static struct _ddebug __section("__dyndbg") d = { modname, function, filename, format, lineno, flags };` + `if (static_branch_unlikely(&d.key)) __dynamic_pr_debug(&d, fmt, ...)`. 모든 디스크립터가 `__dyndbg` 섹션에 모이고, 커널은 부팅·모듈 로드 때 이 표를 control 파일로 보여 준다.

control 파일에 쓰는 쿼리: **`<매치 조건들> <플래그 연산>`**. 조건끼리는 AND, 여러 쿼리는 `;`로 구분.

| 매치 키워드 | 예 | 뜻 |
|---|---|---|
| `module` | `module m1_mod` | 모듈 이름 (빌트인은 파일 기준 모듈명) |
| `file` | `file drivers/base/dd.c`, `file m1_mod.c` | 소스 경로(뒷부분 일치) |
| `func` | `func really_probe` | 함수 이름 (glob 가능) |
| `line` | `line 9`, `line 100-200` | 줄 번호(범위) |
| `format` | `format "probing"` | 포맷 문자열 일부 |
| `class` | `class DRM_UT_CORE` | DRM 같은 클래스 기반 사이트 |

| 플래그 | 뜻 |
|---|---|
| `p` | 출력 켜기 (printk로 보냄) |
| `f` | 함수 이름 붙이기 |
| `l` | 줄 번호 붙이기 |
| `m` | 모듈 이름 붙이기 |
| `t` | 스레드 ID 붙이기 |
| `s` | 소스 파일 이름 붙이기 (6.x) |
| `_` | 플래그 없음 (control 표시용) |

연산자는 `+`(추가), `-`(제거), `=`(그대로 설정). 예: `echo 'file drivers/i2c/* +p' > control`.

켜는 방법은 세 가지다.

| 방법 | 예 | 적용 시점 |
|---|---|---|
| control 파일 | `echo 'module m1_mod +p' > /proc/dynamic_debug/control` | 즉시. 이미 지나간 코드(init)는 못 본다 |
| 모듈 파라미터 | `insmod m1_mod.ko dyndbg=+pflmt` / `modprobe m1_mod dyndbg=+p` | 모듈 로드 순간 → init·probe의 dbg도 보인다 |
| 부팅 인자 | `dyndbg="file drivers/base/dd.c +p"` (빌트인), `m1_mod.dyndbg=+p` (모듈) | 빌트인은 부팅 때. 모듈용은 **modprobe가** `/proc/cmdline`에서 읽어 넘긴다 (예제 5) |

### 4.3 예제 3 — 모듈 파라미터, 파일+줄, 함수 쿼리

무엇을 확인하나: (a) `insmod ... dyndbg=+pflmt`로 init·probe의 dbg까지 보기, (b) 평범하게 넣은 뒤 **한 줄만** 켜기, (c) func 쿼리와 매치 없는 쿼리, (d) 커널 전체 사이트 수.

```sh
#!/bin/sh
echo 4 > /proc/sys/kernel/printk; C=/proc/dynamic_debug/control
echo "== (a) insmod with module param dyndbg=+pflmt"
insmod /m1/m1_mod.ko dyndbg=+pflmt; dmesg | grep -E "tick 0 |DEBUG|dev_dbg|deferral"
echo "== control lines for module m1_mod"; grep m1_mod $C
rmmod m1_mod; dmesg -c >/dev/null
echo "== (b) plain insmod, then enable ONE line (file+line) with +pt"
insmod /m1/m1_mod.ko; echo 'file m1_mod.c line 9 +pt' > $C
grep "m1_mod.c:9" $C; sleep 6; rmmod m1_mod; dmesg | grep -E "tick|suppressed"
echo "== (c) query by func, flags -p, and an unknown-match query"
insmod /m1/m1_mod.ko; echo 'func m1_probe +p' > $C; echo 'func m1_probe -p' > $C
echo 'module nosuch +p' > $C; echo "write rc=$?"; rmmod m1_mod
echo "== (d) count of dyndbg sites in this kernel and how many are on"
wc -l < $C; grep -c " =[^_ ]" $C
```

```text
== (a) insmod with module param dyndbg=+pflmt
[   10.655781][   T77] m1_mod: level 0 EMERG
[   10.655852][   T77] m1_mod: level 1 ALERT
[   10.655870][   T77] m1_mod: level 2 CRIT
[   10.655904][   T77] m1_mod: level 3 ERR
[   10.656203][   T77] [77] m1_mod:m1_init:37: m1_mod: level 7 DEBUG (only if DEBUG or dyndbg)
[   10.688617][   T77] [77] m1_mod:m1_probe:18: m1_dev m1_dev.0: probe: dev_dbg (dyndbg site)
[   10.690558][   T77] [77] m1_mod:m1_tick:9: m1_mod: tick 0 (pr_debug: dyndbg site)
== control lines for module m1_mod
m1_mod.c:37 [m1_mod]m1_init =pmflt "level 7 DEBUG (only if DEBUG or dyndbg)\n"
m1_mod.c:9 [m1_mod]m1_tick =pmflt "tick %d (pr_debug: dyndbg site)\n"
m1_mod.c:18 [m1_mod]m1_probe =pmflt "probe: dev_dbg (dyndbg site)\n"
== (b) plain insmod, then enable ONE line (file+line) with +pt
[   11.816230][   T83] m1_mod: level 0 EMERG
[   11.816309][   T83] m1_mod: level 1 ALERT
[   11.816328][   T83] m1_mod: level 2 CRIT
[   11.816341][   T83] m1_mod: level 3 ERR
m1_mod.c:9 [m1_mod]m1_tick =pt "tick %d (pr_debug: dyndbg site)\n"
[   18.480338][   T86] [86] m1_mod: tick 99 (pr_debug: dyndbg site)
[   18.480698][   T86] m1_tick: 20 callbacks suppressed
== (c) query by func, flags -p, and an unknown-match query
[   18.817054][   T89] m1_mod: level 0 EMERG
[   18.817122][   T89] m1_mod: level 1 ALERT
[   18.817149][   T89] m1_mod: level 2 CRIT
[   18.817167][   T89] m1_mod: level 3 ERR
write rc=0
== (d) count of dyndbg sites in this kernel and how many are on
6226
94
```

출력에서 볼 것:

- (a) 모듈 파라미터 `dyndbg=`는 로드 **순간** 적용되므로 `m1_init`의 줄(37)과 probe의 `dev_dbg`(18)까지 보인다. control 파일로는 이미 끝난 init을 다시 볼 방법이 없다. probe 디버깅은 이 방법으로 한다.
- `+pflmt` 접두어: `[77]`(t, 스레드 ID) `m1_mod:`(m) `m1_init:`(f) `37:`(l). `pr_fmt`의 `m1_mod:`가 한 번 더 붙은 건 m 플래그와 겹친 것이다.
- (b) `=pt`로 **딱 한 줄**(m1_tick의 9행)만 켰고, rmmod 때의 `tick 99`만 나왔다. 수천 개 사이트 중 문제 함수 하나만 켜서 로그를 최소화하는 게 실무 사용법이다.
- (b)의 `[86] m1_mod:` 앞 `[86]`은 t 플래그, 뒤의 `[   T86]`는 PRINTK_CALLER다. 둘은 별개 기능.
- (c) `module nosuch +p`는 매치가 하나도 없는데 쓰기가 **성공(rc=0)**했다. 오타를 내도 에러가 안 난다 — 쓴 다음에 `grep` 으로 `=p`가 실제 붙었는지 반드시 확인하라.
- (d) 이 커널에는 dyndbg 사이트가 6226개 있고 94개가 기본으로 켜져 있다. 켜진 것은 소스에 `#define DEBUG`가 있는 파일들이다(예제 4 끝).

### 4.4 예제 4 — 부팅 인자 `dyndbg=`로 빌트인 driver core 켜기 + `dev_err_probe`의 숨은 줄

무엇을 확인하나: 빌트인 코드(`drivers/base/dd.c`의 probe 경로, `__dev_probe_failed`)를 **부팅 때부터** 켜고, m1_dev가 매칭·probe·bind되는 과정과 `dev_err_probe(-EPROBE_DEFER)`가 남기는 dbg 줄을 본다.

```sh
#!/bin/sh
echo 4 > /proc/sys/kernel/printk
echo "== cmdline: $(cat /proc/cmdline)"
insmod /m1/m1_mod.ko
dmesg | grep -E "m1_" | grep -vE "ratelimited line|level [0-6]|tick [1-9]"
echo "== builtin sites enabled at boot: $(grep -c 'drivers/base/dd.c.* =p' /proc/dynamic_debug/control)"
```

부팅 인자: `console=ttyAMA0 quiet dyndbg="func __dev_probe_failed +p; file drivers/base/dd.c +p" m1_mod.dyndbg=+pmf`

```text
== cmdline: console=ttyAMA0 quiet dyndbg="func __dev_probe_failed +p; file drivers/base/dd.c +p" m1_mod.dyndbg=+pmf LAB_CMD=/m1/t.sh
[    7.694332][   T78] m1_mod: level 0 EMERG
[    7.694398][   T78] m1_mod: level 1 ALERT
[    7.694414][   T78] m1_mod: level 2 CRIT
[    7.694427][   T78] m1_mod: level 3 ERR
[    0.000000][    T0] Kernel command line: console=ttyAMA0 quiet dyndbg="func __dev_probe_failed +p; file drivers/base/dd.c +p" m1_mod.dyndbg=+pmf LAB_CMD=/m1/t.sh
[    7.617390][   T78] m1_mod: loading out-of-tree module taints kernel.
[    7.617790][   T78] m1_mod: module verification failed: signature and/or required key missing - tainting kernel
[    7.714951][   T78] platform m1_dev.0: bus: 'platform': __driver_probe_device: matched device with driver m1_dev
[    7.715072][   T78] platform m1_dev.0: bus: 'platform': really_probe: probing driver m1_dev with device
[    7.715470][   T78] m1_dev m1_dev.0: probe: dev_info carries driver+device name
[    7.715516][   T78] m1_dev m1_dev.0: probe: dev_warn
[    7.715718][   T78] m1_dev m1_dev.0: error -EPROBE_DEFER: dev_err_probe demo (deferral is dbg)
[    7.715947][   T78] m1_dev m1_dev.0: driver: 'm1_dev': driver_bound: bound to device
[    7.716217][   T78] m1_dev m1_dev.0: bus: 'platform': really_probe: bound device to driver m1_dev
[    7.716465][   T78] m1_mod: once line (i=0)
== builtin sites enabled at boot: 20
```

출력에서 볼 것:

- driver core의 dbg가 "matched → probing → (드라이버 자신의 로그) → bound" 순서를 보여 준다. "probe가 왜 안 불리나?"(D3, G3)를 디버그할 때 첫 수다: `dyndbg="file drivers/base/dd.c +p"`. 매칭조차 안 되면 이 줄들이 아예 없다.
- `dev_err_probe(-EPROBE_DEFER)`는 `dev_err`가 아니라 **dev_dbg**로 `error -EPROBE_DEFER: ...`를 찍는다. 랩 소스 `drivers/base/core.c`의 `__dev_probe_failed()`가 `-EPROBE_DEFER`면 `device_set_deferred_probe_reason()` + `dev_dbg`, `-ENOMEM`이면 침묵, 나머지는 `dev_err`/`dev_warn`이다. 그래서 deferral 이유는 평소엔 안 보이고 `/sys/kernel/debug/devices_deferred`나 이 dyndbg로 본다.
- **그런데 `m1_mod.dyndbg=+pmf`는 효과가 없었다** — m1_mod의 `pr_debug` 줄이 하나도 없다. 다음 예제에서 이유를 본다.

### 4.5 예제 5 — `<모듈>.dyndbg=` 부팅 인자는 누가 적용하나

무엇을 확인하나: 같은 부팅 인자 `m1_mod.dyndbg=+pmf`에서 BusyBox `insmod`와 kmod `modprobe`의 차이. rootfs-trace를 읽기 전용으로 붙이고 tmpfs에 모듈 디렉터리를 만들어 `modprobe -d`로 로드한다.

```sh
#!/bin/sh
echo 4 > /proc/sys/kernel/printk
mkdir -p /mnt/r && mount -o ro,noload /dev/vda /mnt/r && mount -t devtmpfs d /mnt/r/dev
mount -t proc p /mnt/r/proc && mount -t tmpfs t /mnt/r/tmp && mount -t sysfs s /mnt/r/sys
V=$(uname -r); M=/mnt/r/tmp/mr/lib/modules/$V; mkdir -p $M/extra; cp /m1/m1_mod.ko $M/extra/
chroot /mnt/r /usr/sbin/depmod -b /tmp/mr $V
echo "== cmdline: $(cat /proc/cmdline)"
echo "== (1) busybox insmod: cmdline m1_mod.dyndbg is NOT applied"
insmod /m1/m1_mod.ko; dmesg | grep -c "m1_mod:m1_"; rmmod m1_mod
echo "== (2) kmod modprobe: reads /proc/cmdline and passes m1_mod.dyndbg"
chroot /mnt/r /usr/sbin/modprobe -d /tmp/mr m1_mod; dmesg | grep "m1_mod:m1_"
echo "== default-on sites (compiled with DEBUG):"; grep " =p " /proc/dynamic_debug/control | head -4
grep " =p " /proc/dynamic_debug/control | cut -d: -f1 | sort | uniq -c | sort -rn | head -5
```

실행 결과 (depmod의 modules.order 없음 경고 3줄과 modprobe 쪽 tick 1~29 줄은 생략):

```text
== cmdline: console=ttyAMA0 quiet m1_mod.dyndbg=+pmf LAB_CMD=/m1/t.sh
== (1) busybox insmod: cmdline m1_mod.dyndbg is NOT applied
[    7.545963][   T89] m1_mod: level 0 EMERG
[    7.546061][   T89] m1_mod: level 1 ALERT
[    7.546086][   T89] m1_mod: level 2 CRIT
[    7.546106][   T89] m1_mod: level 3 ERR
0
== (2) kmod modprobe: reads /proc/cmdline and passes m1_mod.dyndbg
[    7.814584][   T93] m1_mod: level 0 EMERG
[    7.814648][   T93] m1_mod: level 1 ALERT
[    7.814665][   T93] m1_mod: level 2 CRIT
[    7.814680][   T93] m1_mod: level 3 ERR
[    7.814864][   T93] m1_mod:m1_init: m1_mod: level 7 DEBUG (only if DEBUG or dyndbg)
[    7.830275][   T93] m1_mod:m1_probe: m1_dev m1_dev.0: probe: dev_dbg (dyndbg site)
[    7.830908][   T93] m1_mod:m1_tick: m1_mod: tick 0 (pr_debug: dyndbg site)
...
== default-on sites (compiled with DEBUG):
init/main.c:1149 [main]initcall_blacklisted =p "initcall %s blacklisted\n"
init/main.c:1116 [main]initcall_blacklist =p "blacklisting initcall %s\n"
init/main.c:1363 [main]run_init_process =p "    %s\n"
init/main.c:1361 [main]run_init_process =p "  with environment:\n"
     11 drivers/scsi/libsas/sas_expander.c
      8 drivers/scsi/libsas/sas_scsi_host.c
      6 kernel/power/swap.c
      6 init/main.c
      5 drivers/scsi/libsas/sas_discover.c
```

출력에서 볼 것:

- BusyBox `insmod`는 0줄, kmod `modprobe`는 `m1_init`·`m1_probe`·`m1_tick`의 dbg가 전부 나왔다(+pmf라 `m1_mod:m1_init:` 접두어). **커널은 `<mod>.dyndbg=` 부팅 인자를 모듈에 자동 적용하지 않는다.** kmod의 modprobe가 `/proc/cmdline`에서 `m1_mod.`로 시작하는 인자를 찾아 모듈 파라미터로 넘겨 주는 것이다. 최소 BusyBox 이미지(insmod만 있는 initramfs)에서 "부팅 인자로 dyndbg를 줬는데 안 켜진다"는 장면이 정확히 이것이다. 해법: modprobe를 쓰거나, `insmod x.ko dyndbg=+p`, 또는 빌트인처럼 `dyndbg="module m1_mod +p"`(모듈 로드 시 커널이 보류된 쿼리를 적용하는지는 커널 문서 `dynamic-debug-howto`의 "Debug messages at module initialization time" 절을 확인 — 이 노트에서는 실행하지 않았다).
- 기본으로 켜진 94개는 `init/main.c` 같은 파일이 `#define DEBUG`를 갖고 있어서다. 그래서 `run_init_process`의 "with arguments" 줄이 매 부팅 KERN_DEBUG로 남는다(예제 2의 `ign` 출력에 보였다).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">dynamic debug — 사이트 표와 세 개의 스위치</text>
  <rect x="14" y="40" width="200" height="104" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="114" y="60" text-anchor="middle" font-size="12">pr_debug("tick %d")</text>
  <text x="114" y="80" text-anchor="middle" font-size="12">컴파일 시 _ddebug 생성</text>
  <text x="114" y="100" text-anchor="middle" font-size="12">mod, func, file, line</text>
  <text x="114" y="120" text-anchor="middle" font-size="12">format, flags, key</text>
  <text x="114" y="138" text-anchor="middle" font-size="12">→ __dyndbg 섹션</text>
  <rect x="260" y="40" width="190" height="104" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="355" y="62" text-anchor="middle" font-size="13" font-weight="bold">사이트 표 (6226개)</text>
  <text x="355" y="84" text-anchor="middle" font-size="12">m1_mod.c:9 =_</text>
  <text x="355" y="104" text-anchor="middle" font-size="12">dd.c:… =p</text>
  <text x="355" y="124" text-anchor="middle" font-size="12">꺼짐 = static key NOP</text>
  <line x1="214" y1="92" x2="258" y2="92" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="490" y="40" width="176" height="104" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="578" y="62" text-anchor="middle" font-size="13" font-weight="bold">켜진 사이트</text>
  <text x="578" y="84" text-anchor="middle" font-size="12">__dynamic_pr_debug()</text>
  <text x="578" y="104" text-anchor="middle" font-size="12">+ f l m t 접두어</text>
  <text x="578" y="124" text-anchor="middle" font-size="12">→ printk KERN_DEBUG</text>
  <line x1="450" y1="92" x2="488" y2="92" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="14" y="190" width="200" height="80" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="114" y="212" text-anchor="middle" font-size="12">control 파일 쓰기</text>
  <text x="114" y="232" text-anchor="middle" font-size="12">'file x.c line 9 +p'</text>
  <text x="114" y="252" text-anchor="middle" font-size="12">즉시, 지난 init은 못 봄</text>
  <rect x="240" y="190" width="200" height="80" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="212" text-anchor="middle" font-size="12">모듈 파라미터</text>
  <text x="340" y="232" text-anchor="middle" font-size="12">insmod x.ko dyndbg=+p</text>
  <text x="340" y="252" text-anchor="middle" font-size="12">로드 순간, init·probe 포함</text>
  <rect x="466" y="190" width="200" height="80" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="566" y="212" text-anchor="middle" font-size="12">부팅 인자</text>
  <text x="566" y="232" text-anchor="middle" font-size="12">dyndbg="..." (빌트인)</text>
  <text x="566" y="252" text-anchor="middle" font-size="12">mod.dyndbg= (modprobe만)</text>
  <line x1="114" y1="190" x2="320" y2="146" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="190" x2="350" y2="146" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="566" y1="190" x2="400" y2="146" stroke="currentColor" marker-end="url(#ah2)"/>
</svg>
```
그림 3 — dynamic debug의 구조. 사이트는 컴파일 때 다 만들어지고, 스위치 세 개가 각 사이트의 flags를 바꾼다. 꺼진 사이트의 비용은 static key NOP 하나다.

### 4.6 `DEBUG` define, `CONFIG_DYNAMIC_DEBUG_CORE`, 그리고 제품 빌드

- 드라이버 폴더 Makefile의 `ccflags-$(CONFIG_I2C_DEBUG_CORE) := -DDEBUG` 같은 패턴은 **그 파일들의 pr_debug를 기본 켬(=p)**으로 만든다(dyndbg가 있으면 켜진 사이트, 없으면 그냥 printk).
- `CONFIG_DYNAMIC_DEBUG=y`는 커널 이미지를 수백 KB 키운다(사이트 디스크립터 + 포맷 문자열). 작은 제품은 `CONFIG_DYNAMIC_DEBUG_CORE=y`만 켜고 원하는 모듈만 `ccflags-y += -DDYNAMIC_DEBUG_MODULE`로 사이트를 만든다.
- dyndbg가 아예 없는 커널에서 `pr_debug`는 `no_printk`가 되어 사라진다. 이 경우 유일한 방법은 `#define DEBUG` 후 재빌드.
- 제품 커널에서도 dyndbg는 켜 두는 편이 낫다. 필드에서 재현되는 버그를 재빌드 없이 볼 수 있는 유일한 창구이기 때문이다(§8).

---

## 5. earlycon — 콘솔이 뜨기 전의 크래시 잡기

C6 §5가 earlycon과 정식 console의 인계, tty0 함정, `initcall_blacklist=pl011_init`으로 "콘솔 드라이버가 끝내 안 뜰 때"를 이미 다뤘다. 여기서는 **콘솔 이전의 진짜 크래시**와 `keep_bootcon`을 실측한다.

### 5.1 정의 복습

- `earlycon=pl011,0x9000000` (드라이버명, MMIO 물리 주소) 또는 인자 없는 `earlycon`(DT `/chosen/stdout-path`의 UART 사용). 드라이버는 `OF_EARLYCON_DECLARE`/`EARLYCON_DECLARE`로 등록된 "레지스터 몇 개만 쓰는 putc"다. arm64는 `setup_arch()`의 `parse_early_param()`에서 켠다 — 메모리 할당기도 없을 때다.
- `earlyprintk=`는 옛 아키텍처별 방식(x86 `earlyprintk=serial,ttyS0` 등). arm64는 earlycon만 쓴다.
- `keep_bootcon`: 정식 콘솔이 등록돼도 bootconsole을 해제하지 않는다. "콘솔 인계 순간에 멈춘다"를 의심할 때 쓴다.
- 등록 순간 이미 쌓인 버퍼를 다시 찍는다(`CON_PRINTBUFFER`) — 그래서 earlycon을 켜면 0초의 첫 줄부터 보인다.

### 5.2 예제 6 — 부팅 0초의 크래시: earlycon 없이 0줄, 있으면 Oops 전체

무엇을 확인하나: `mem=24M`으로 메모리를 일부러 너무 적게 주면 이 커널은 `setup_arch()` 안에서 NULL 역참조로 죽는다(정식 콘솔 등록 훨씬 전). earlycon 유무만 바꾼다.

```sh
q crash_noec "console=ttyAMA0 mem=24M"                         # timeout 40 s
q crash_ec   "console=ttyAMA0 earlycon=pl011,0x9000000 mem=24M"
cat e_crash_noec.txt; sed -n "5,8p;19,22p;36,40p;51,62p" e_crash_ec.txt
```

```text
crash_noec rc=124 lines=1
crash_ec rc=124 lines=63
qemu-system-aarch64: terminating on signal 15 from pid 1957265 (timeout)
[    0.000000][    T0] Machine model: linux,dummy-virt
[    0.000000][    T0] earlycon: pl11 at MMIO 0x0000000009000000 (options '')
[    0.000000][    T0] printk: legacy bootconsole [pl11] enabled
[    0.000000][    T0] Memory limited to 24MB
[    0.000000][    T0] Early memory node ranges
[    0.000000][    T0]   node   0: [mem 0x0000000040000000-0x000000004419ffff]
[    0.000000][    T0] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000010
[    0.000000][    T0] Mem abort info:
[    0.000000][    T0] Hardware name: linux,dummy-virt (DT)
[    0.000000][    T0] pstate: 800000c5 (Nzcv daIF -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[    0.000000][    T0] pc : __bitmap_set+0x6c/0x90
[    0.000000][    T0] lr : subsection_mask_set+0x34/0x48
[    0.000000][    T0] sp : ffffa481ad993be0
[    0.000000][    T0] Call trace:
[    0.000000][    T0]  __bitmap_set+0x6c/0x90 (P)
[    0.000000][    T0]  subsection_map_init+0x88/0xf0
[    0.000000][    T0]  free_area_init+0x6c4/0xe80
[    0.000000][    T0]  bootmem_init+0x120/0x1b8
[    0.000000][    T0]  setup_arch+0x280/0x5d0
[    0.000000][    T0]  start_kernel+0x88/0x788
[    0.000000][    T0]  __primary_switched+0x88/0x98
[    0.000000][    T0] Code: 92800001 4b0203e2 9ac22421 8a050021 (f9400002) 
[    0.000000][    T0] ---[ end trace 0000000000000000 ]---
[    0.000000][    T0] Kernel panic - not syncing: Attempted to kill the idle task!
[    0.000000][    T0] ---[ end Kernel panic - not syncing: Attempted to kill the idle task! ]---
```

출력에서 볼 것:

- earlycon 없이: QEMU의 timeout 메시지 1줄뿐. 커널은 0초에 panic했지만 Oops는 RAM 속 링버퍼에만 있다. 보드라면 "U-Boot의 `Starting kernel ...` 뒤 침묵"이다.
- earlycon과 함께: `setup_arch → bootmem_init → free_area_init → subsection_map_init → __bitmap_set`의 콜스택, PC, Code 바이트까지 다 보인다. 이걸 M3의 `decode_stacktrace.sh`/`faddr2line`에 넣으면 소스 줄이 나온다. 모든 시각이 `0.000000`인 것도 단서다 — 타이머 초기화(`time_init`) 이전이다.
- `printk: legacy bootconsole [pl11] enabled` — earlycon은 legacy 콘솔이다. 이 시점엔 kthread도 스케줄러도 없으니 당연히 호출자가 직접 찍는다.
- `(P)`는 6.x arm64 콜스택에서 "이 프레임 주소는 PC에서 왔다"는 표시다(LR에서 온 프레임과 구분).

> earlycon으로도 아무것도 안 나오면? 커널이 `setup_arch`에 도달하지 못한 것이다. (1) 부트로더가 DTB 주소·Image 로드 주소를 제대로 넘겼나(C6 §2), (2) earlycon의 드라이버명·주소(물리 주소!)가 맞나, (3) UART 클록·핀먹스를 부트로더가 켜 뒀나(earlycon은 클록을 켜지 않는다), (4) 그래도 안 되면 JTAG로 PC를 보고(M4), 링버퍼를 메모리 덤프로 읽는다(`__log_buf` 심볼 — `vmlinux`에서 주소 확인, 또는 L7의 pstore/ramoops로 다음 부팅에 회수).

### 5.3 예제 7 — 콘솔 인계와 `keep_bootcon`

무엇을 확인하나: 정상 부팅에서 earlycon → ttyAMA0 인계 줄을 찾고, `keep_bootcon`이면 무엇이 달라지는지 줄 수로 본다.

```sh
q ec    "console=ttyAMA0 earlycon=pl011,0x9000000 LAB_CMD=true"
q eckb  "console=ttyAMA0 earlycon=pl011,0x9000000 keep_bootcon LAB_CMD=true"
grep -n -E "console|bootcon|earlycon" e_ec.txt; grep -n -E "console|bootcon|earlycon" e_eckb.txt
sort e_eckb.txt | uniq -d | wc -l
```

```text
ec rc=0 lines=293
eckb rc=0 lines=460
6:[    0.000000][    T0] earlycon: pl11 at MMIO 0x0000000009000000 (options '')
7:[    0.000000][    T0] printk: legacy bootconsole [pl11] enabled
38:[    0.000000][    T0] Kernel command line: console=ttyAMA0 earlycon=pl011,0x9000000 LAB_CMD=true
119:[    5.742452][    T1] printk: console [ttyAMA0] enabled
120:[    5.742452][    T1] printk: console [ttyAMA0] enabled
121:[    5.744515][    T1] printk: legacy bootconsole [pl11] disabled
122:[    5.744515][    T1] printk: legacy bootconsole [pl11] disabled
284:[   30.457833][    T1] netconsole: network logging started
6:[    0.000000][    T0] earlycon: pl11 at MMIO 0x0000000009000000 (options '')
7:[    0.000000][    T0] printk: legacy bootconsole [pl11] enabled
8:[    0.000000][    T0] printk: debug: skip boot console de-registration.
39:[    0.000000][    T0] Kernel command line: console=ttyAMA0 earlycon=pl011,0x9000000 keep_bootcon LAB_CMD=true
120:[    4.360367][    T1] printk: console [ttyAMA0] enabled
121:[    4.360367][    T1] printk: console [ttyAMA0] enabled
442:[   27.742315][    T1] netconsole: network logging started
443:[   27.742315][    T1] netconsole: network logging started
170
```

출력에서 볼 것:

- 인계 순간 두 줄씩 찍혔다(119~122행): 같은 UART에 earlycon(pl11)과 ttyAMA0이 동시에 쓰는 짧은 구간. 인계가 끝나면 `legacy bootconsole [pl11] disabled`.
- `keep_bootcon`이면 `skip boot console de-registration` 후 **이후 모든 줄이 두 번** 나온다(중복 줄 170개, 293 → 460줄). 같은 UART를 두 콘솔이 공유하기 때문이다. 실무 용도: 정식 UART 드라이버 probe(클록·핀먹스·baud 재설정) 순간 출력이 끊기면 `keep_bootcon`으로 "커널은 살아 있는데 정식 콘솔만 망가졌나"를 가른다. 단 정식 드라이버가 UART를 리셋·클록 변경하면 earlycon 쪽 출력도 깨질 수 있다.
- 시간 수치(ttyAMA0 등록 4.36~5.74 s)는 공유 VM 부하 탓에 C6의 실측(0.47 s)보다 훨씬 느리다. 실제 HW 수치가 아니다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">부팅 타임라인 — 어느 창구로 로그가 나오나 (예제 6, 7)</text>
  <line x1="40" y1="230" x2="650" y2="230" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="40" y="250" font-size="12">0 s</text>
  <text x="230" y="250" font-size="12">setup_arch</text>
  <text x="420" y="250" font-size="12">ttyAMA0 등록</text>
  <text x="580" y="250" font-size="12">/init</text>
  <rect x="40" y="40" width="610" height="32" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="345" y="61" text-anchor="middle" font-size="12">링버퍼: 처음부터 전부 기록 (dmesg로 나중에 볼 수 있음)</text>
  <rect x="150" y="86" width="290" height="32" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="295" y="107" text-anchor="middle" font-size="12">earlycon (legacy bootconsole)</text>
  <rect x="440" y="86" width="210" height="32" fill="#e08a3c" fill-opacity="0.05" stroke="#e08a3c" stroke-dasharray="4 3"/>
  <text x="545" y="107" text-anchor="middle" font-size="12">keep_bootcon: 계속 (중복)</text>
  <rect x="440" y="132" width="210" height="32" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="545" y="153" text-anchor="middle" font-size="12">정식 콘솔 (nbcon)</text>
  <line x1="200" y1="178" x2="200" y2="228" stroke="#d0564a"/>
  <text x="208" y="196" font-size="12">mem=24M 크래시 (0 s)</text>
  <text x="208" y="214" font-size="12">earlycon 없으면 출력 0줄</text>
  <text x="40" y="107" font-size="12">이전</text>
  <text x="40" y="153" font-size="12">침묵</text>
</svg>
```
그림 4 — 링버퍼는 0초부터 기록하지만, 화면으로 나가는 창구는 earlycon(setup_arch부터)과 정식 콘솔(드라이버 probe 후)뿐이다. 그 사이에 죽으면 earlycon만이 증거를 보여 준다.

---

## 6. printk의 비용 — storm, 링버퍼 overflow, ratelimit

### 6.1 직관 — 115200 baud는 생각보다 느리다

115200 baud, 8N1이면 한 바이트에 10비트 → **초당 11,520바이트**. 예제 8의 storm 한 줄은 콘솔에서 `[    9.999787][   T77] m1_storm: storm line 00000 ` + `x` 39개 + `\r\n` = **91바이트**다. 한 줄 = 910비트 / 115200 = **7.9 ms**. 2000줄이면 **15.8초**. legacy 콘솔이면 이 시간 동안 printk를 부른 쪽이(인터럽트를 끈 채일 수도 있다) 묶인다. 센서 루프가 1 kHz라면 한 줄에 8주기를 놓친다.

### 6.2 예제 8 — storm 비용 실측: 필터됨 vs nbcon vs legacy bootconsole

무엇을 확인하나: 2000줄을 (A) 콘솔 레벨에 걸려 필터될 때, (B/C) nbcon PL011 콘솔로 나갈 때, 그리고 `earlycon keep_bootcon`으로 legacy 콘솔을 하나 더 살려 둔 경우를 비교한다. 호출자 쪽 시간은 모듈이 `ktime_get_ns()`로 재고, 콘솔에 실제로 나온 줄 수와 시간 폭은 호스트의 Python이 QEMU stdout을 읽으며 잰다.

```c
// m1_storm.c - cost of N printk lines to the caller, at a chosen level
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/ktime.h>

static int n = 1000;
module_param(n, int, 0644);

static int m1_run(const char *val, const struct kernel_param *kp)
{
	int lvl, i;
	u64 t0, dt;

	if (kstrtoint(val, 0, &lvl) || lvl < 0 || lvl > 7)
		return -EINVAL;
	t0 = ktime_get_ns();
	for (i = 0; i < n; i++)
		printk("%c%c" KBUILD_MODNAME ": storm line %05d xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",
		       KERN_SOH_ASCII, '0' + lvl, i);
	dt = ktime_get_ns() - t0;
	pr_emerg("RESULT level=%d n=%d caller_time=%llu us (%llu ns/line)\n",
		 lvl, n, dt / 1000, dt / n);
	return 0;
}
static const struct kernel_param_ops m1_run_ops = { .set = m1_run };
module_param_cb(run, &m1_run_ops, NULL, 0200);

static int __init m1_storm_init(void) { return 0; }
static void __exit m1_storm_exit(void) { }
module_init(m1_storm_init);
module_exit(m1_storm_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("M1 printk storm cost");
```

`"%c%c"` + `KERN_SOH_ASCII`는 레벨을 런타임 값으로 고르는 요령이다(`KERN_<LEVEL>`은 문자열 리터럴이라 변수로 못 바꾼다).

게스트 스크립트와 호스트 측 측정기:

```sh
#!/bin/sh
insmod /m1/m1_storm.ko n=2000
echo "== A: level 7, console_loglevel=4 (filtered: ringbuffer only)"
echo 4 > /proc/sys/kernel/printk; echo 7 > /sys/module/m1_storm/parameters/run; sleep 3
echo "== B: level 6, console_loglevel=8 (to console)"
echo 8 > /proc/sys/kernel/printk; echo 6 > /sys/module/m1_storm/parameters/run; sleep 8
echo "== C: same as B again"
echo 6 > /sys/module/m1_storm/parameters/run; sleep 8
echo "== consoles:"; cat /proc/consoles
echo "m1: printer threads: $(ps | grep -E '\[pr/' | grep -v grep | tr -s ' ' | cut -d' ' -f5- | tr '\n' ' ')"
```

```python
import sys, time
first = last = None; n = 0; t0 = time.monotonic(); show = False
def summary():
    global first, n
    if first:
        print(f"   host: {n} storm lines on serial, first->last {1000*(last-first):.1f} ms")
    first = None; n = 0
for raw in sys.stdin.buffer:
    t = time.monotonic(); line = raw.decode(errors='replace').rstrip()
    if 'storm line' in line:
        n += 1; first = first or t; last = t
        continue
    if line.startswith('=='):
        summary(); show = 'consoles' in line
    if show or line.startswith('==') or 'RESULT' in line or 'dropped' in line or 'm1:' in line or 'bootcon' in line:
        print(f"{t - t0:8.3f}s  {line}")
summary()
```

nbcon 콘솔만 있는 부팅(`quiet`), 두 번 실행 중 첫 번째. `** N printk messages dropped **` 줄은 개수만 셌다:

```text
   8.865s  == A: level 7, console_loglevel=4 (filtered: ringbuffer only)
   8.938s  [    6.812470][   T77] m1_storm: RESULT level=7 n=2000 caller_time=63048 us (31524 ns/line)
  11.982s  == B: level 6, console_loglevel=8 (to console)
  13.055s  [    9.999787][   T77] m1_storm: RESULT level=6 n=2000 caller_time=140245 us (70122 ns/line)
   host: 1760 storm lines on serial, first->last 1067.1 ms
  20.274s  == C: same as B again
  21.785s  [   18.259783][   T77] m1_storm: RESULT level=6 n=2000 caller_time=106185 us (53092 ns/line)
   host: 1765 storm lines on serial, first->last 1499.7 ms
  28.489s  == consoles:
  28.534s  ttyAMA0              -W- (EC Np a)  204:64
  28.984s  m1: printer threads: [pr/ttyAMA-1]
dropped lines: 8 sum: 475
```

두 번째 실행: A 67180 us, B 93825 us(콘솔 1725줄), C 100638 us(1718줄), dropped 합계 557.

`earlycon=pl011,0x9000000 keep_bootcon`을 더한 부팅(legacy bootconsole이 살아 있음):

```text
   8.519s  == A: level 7, console_loglevel=4 (filtered: ringbuffer only)
   8.659s  [    7.154290][   T75] m1_storm: RESULT level=7 n=2000 caller_time=127100 us (63550 ns/line)
   8.659s  [    7.154290][   T75] m1_storm: RESULT level=7 n=2000 caller_time=127100 us (63550 ns/line)
  11.706s  == B: level 6, console_loglevel=8 (to console)
  14.020s  [   12.516052][   T75] m1_storm: RESULT level=6 n=2000 caller_time=2313768 us (1156884 ns/line)
  14.021s  [   12.516052][   T75] m1_storm: RESULT level=6 n=2000 caller_time=2313768 us (1156884 ns/line)
  22.079s  == C: same as B again
  23.912s  [   22.408125][   T75] m1_storm: RESULT level=6 n=2000 caller_time=1836786 us (918393 ns/line)
  23.912s  [   22.408125][   T75] m1_storm: RESULT level=6 n=2000 caller_time=1836786 us (918393 ns/line)
   host: 3998 storm lines on serial, first->last 1830.4 ms
  31.953s  == consoles:
  31.996s  ttyAMA0              -W- (EC N  a)  204:64
  31.996s  pl11                 RW- (E B p  )
  32.326s  m1: printer threads:
dropped lines: 0 sum:
```

(B 구간의 호스트 집계는 같은 줄이 두 콘솔로 섞여 나와 4002줄·10.4 s로 찍혀 신뢰할 수 없어서 뺐다. C 구간의 3998줄 ≈ 2000줄 × 2.)

출력에서 볼 것:

- **필터됨(A)**: 콘솔로 안 나가도 공짜가 아니다. vsnprintf + 링버퍼 저장만으로 한 줄 31~34 µs(TCG). 실제 Cortex-A 하드웨어에서는 이보다 수십 배 빠르다(이 랩에서 실측하지 않았다). 그래도 "꺼진 로그"의 진짜 공짜는 dyndbg가 꺼진 `pr_debug`(static key NOP)뿐이다.
- **nbcon(B/C)**: 호출자는 94~140 ms만 쓰고 바로 돌아왔다. 실제 출력은 `pr/ttyAMA-1` kthread가 뒤에서 했다(이름 끝 `-1`은 kthread 생성 시점의 index 값이 그대로 박힌 것으로 보인다). 대신 **콘솔이 못 따라가서** 링버퍼(128 KiB)가 한 바퀴 돌며 475~557줄이 화면에서 사라졌다(`** N printk messages dropped **`). 링버퍼에서 덮어써졌으니 dmesg에서도 사라진 것이다. 1760 + (일부 dropped) ≈ 2000.
- **legacy bootconsole이 살아 있으면(keep_bootcon)**: 호출자 시간이 **1.8~2.3초**(한 줄 0.9~1.2 ms)로 15~20배 뛰었다. legacy 콘솔은 호출자가 `console_unlock()` 안에서 직접 찍기 때문이다. printer kthread도 사라졌다(legacy 콘솔이 하나라도 있으면 그 경로가 nbcon까지 같이 찍는다). 대신 dropped는 0 — 호출자가 기다려 주니 버퍼가 넘치지 않는다. **지연 vs 유실의 맞교환**이다.
- `/proc/consoles` 플래그: ttyAMA0은 `N`(nbcon), pl11은 `B`(boot) — §3.2 표대로.
- QEMU의 PL011은 baud 속도를 흉내 내지 않는다(115200의 7.9 ms/줄보다 훨씬 빠른 0.5 ms/줄로 나갔다). 그래서 아래 그림의 115200 막대는 **계산값**이다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">printk 2000줄 — 호출자가 묶인 시간 (로그 스케일)</text>
  <g stroke="currentColor" stroke-opacity="0.3">
    <line x1="230" y1="44" x2="230" y2="232"/><line x1="330" y1="44" x2="330" y2="232"/>
    <line x1="430" y1="44" x2="430" y2="232"/><line x1="530" y1="44" x2="530" y2="232"/>
    <line x1="630" y1="44" x2="630" y2="232"/>
  </g>
  <line x1="230" y1="232" x2="630" y2="232" stroke="currentColor"/>
  <g font-size="12" text-anchor="middle">
    <text x="230" y="250">10 ms</text><text x="330" y="250">100 ms</text><text x="430" y="250">1 s</text>
    <text x="530" y="250">10 s</text><text x="630" y="250">100 s</text>
  </g>
  <g font-size="12" text-anchor="end">
    <text x="220" y="78">필터됨 (레벨 7, cl=4)</text>
    <text x="220" y="122">nbcon 콘솔 (kthread)</text>
    <text x="220" y="166">legacy bootcon 동기</text>
    <text x="220" y="210">115200 동기 (계산)</text>
  </g>
  <rect x="230" y="60" width="81.3" height="26" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <text x="318" y="78" font-size="12">65 ms</text>
  <rect x="230" y="104" width="104.1" height="26" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="341" y="122" font-size="12">110 ms (+ 475~557줄 유실)</text>
  <rect x="230" y="148" width="231.7" height="26" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
  <text x="468" y="166" font-size="12">2.1 s (유실 0)</text>
  <rect x="230" y="192" width="319.9" height="26" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c" stroke-dasharray="4 3"/>
  <text x="556" y="210" font-size="12">15.8 s</text>
  <text x="340" y="276" text-anchor="middle" font-size="12">실측 막대 = 2회 평균, 공유 VM의 TCG(실제 HW 아님). 점선 = 91바이트 × 2000줄 ÷ 11520 B/s</text>
</svg>
```
그림 5 — 예제 8의 호출자 시간. 콘솔을 kthread로 미루면(nbcon) 호출자는 빨리 돌아오지만 콘솔이 못 따라가 줄을 잃고, 동기(legacy) 콘솔은 잃지 않는 대신 호출자를 묶는다. 실제 115200 UART의 동기 출력이라면 15.8초다.

### 6.3 ratelimit 도구 상자

| 도구 | 효과 | 예제 |
|---|---|---|
| `pr_info_ratelimited`, `dev_err_ratelimited` | 호출 지점마다 5초에 10번. 나머지는 "N callbacks suppressed" | 예제 1 |
| `printk_ratelimited()` | 위와 같은 계열 | — |
| `DEFINE_RATELIMIT_STATE(rs, HZ, 3)` + `__ratelimit(&rs)` | 간격·버스트를 직접 지정 | 연습 4 |
| `pr_info_once`, `dev_warn_once`, `WARN_ONCE` | 부팅(또는 모듈 로드)당 한 번 | 예제 1 |
| `printk.devkmsg=on/off/ratelimit` | 유저 공간의 `/dev/kmsg` 쓰기 제한 (기본 ratelimit) | 예제 10 |
| `/proc/sys/kernel/printk_ratelimit`, `printk_ratelimit_burst` | 옛 전역 `printk_ratelimit()`용 | — |

인터럽트 핸들러의 에러 경로(`IRQ_HANDLED` 전에 "FIFO overflow" 같은 것)는 **반드시** ratelimited로 쓴다. 장치가 고장 나 인터럽트가 폭주하면 printk가 시스템을 멈춘다.

### 6.4 IRQ/NMI 안의 printk

- 5.10 이후 링버퍼 저장은 lockless라 **어느 컨텍스트에서든** 안전하다(옛날의 `logbuf_lock` 데드락, NMI용 per-CPU 버퍼 `printk_safe`는 대부분 정리됐다).
- 출력은 다르다. NMI·스케줄러 내부처럼 콘솔을 직접 못 만지는 곳에서는 `irq_work`로 미룬다(`printk_deferred()`는 그 강제 버전, 스케줄러·타이머 코어 내부 전용).
- nbcon 콘솔은 평소 kthread가 찍고, panic·Oops·`WARN` 같은 비상시에는 `write_atomic()`으로 호출자가 바로 찍는다 — 비상 메시지가 kthread를 기다리다 사라지지 않게.
- 그래도 hardirq에서 printk를 많이 하면: 포맷팅 비용(예제 8의 A) + legacy 콘솔이면 UART 대기가 인터럽트 지연에 그대로 들어간다(N1, N3에서 지연 측정).

---

## 7. printk 포맷 확장 — `%pS`, `%pe`, `%*ph`, `%pOF`, `%p` 해싱과 `kptr_restrict`

### 7.1 정의

커널 `vsnprintf`(`lib/vsprintf.c`)는 `%p` 뒤 글자로 포인터를 해석하는 확장이 많다. 자주 쓰는 것만:

| 포맷 | 출력 | 용도 |
|---|---|---|
| `%p` | 해시된 값 (`000000005160a5a7`) | 같은 포인터인지 비교만 가능, 주소 노출 방지 |
| `%px` | 진짜 주소 | 정말 필요할 때만 (리뷰에서 이유를 묻는다) |
| `%pK` | `kptr_restrict`에 따라 해시/진짜/0 | `/proc`·sysfs로 내보내는 주소 |
| `%pS` / `%ps` | `m1_show+0x0/0x120 [m1_fmt]` / `m1_show [m1_fmt]` | 함수 포인터·콜백·리턴 주소 |
| `%pB` | 콜스택용 (리턴 주소 보정) | backtrace |
| `%pe` | `-ETIMEDOUT` | `ERR_PTR` 이름 (E5) |
| `%*ph` | `de ad be ef 01 02 03 04` | 짧은 버퍼 hex 덤프 (64바이트까지) |
| `%pOF` / `%pOFn` | `/pl011@9000000` / `pl011` | DT 노드 (D8) |
| `%pR` / `%pr` | 자원 범위 + 플래그 / 범위만 | `struct resource` |
| `%pa` | `phys_addr_t`·`dma_addr_t` 값 | `%pa`는 포인터를 넘긴다 (`&addr`) |
| `%pV` | 중첩 `struct va_format` | 래퍼 함수 (`dev_err_probe` 내부) |

### 7.2 예제 9 — 포맷 전부 + `kptr_restrict` 0/1/2 + 프로세스 vs softirq

무엇을 확인하나: 같은 포맷 묶음을 쓰기 전용 모듈 파라미터 `kick`(프로세스 컨텍스트)과 그 직후 1 jiffy 타이머(softirq)에서 찍고, `kptr_restrict`를 0/1/2로 바꾼다.

```c
// m1_fmt.c - printk pointer formats; "kick" param re-prints from process ctx + timer(softirq)
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/of.h>
#include <linux/timer.h>
#include <linux/err.h>

static int m1_obj = 42;
static struct timer_list m1_timer;
static const u8 m1_buf[8] = { 0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03, 0x04 };

static noinline void m1_show(const char *ctx)
{
	struct device_node *np = of_find_node_by_path("/pl011@9000000");

	pr_info("[%s] %%p  = %p\n", ctx, &m1_obj);
	pr_info("[%s] %%px = %px\n", ctx, &m1_obj);
	pr_info("[%s] %%pK = %pK\n", ctx, &m1_obj);
	pr_info("[%s] %%pS = %pS  %%ps = %ps\n", ctx, m1_show, m1_show);
	pr_info("[%s] caller %%pS = %pS\n", ctx, __builtin_return_address(0));
	pr_info("[%s] %%pe = %pe, %pe\n", ctx, ERR_PTR(-ETIMEDOUT), ERR_PTR(-EPROBE_DEFER));
	pr_info("[%s] %%*ph = %*ph\n", ctx, (int)sizeof(m1_buf), m1_buf);
	pr_info("[%s] %%pOF = %pOF  %%pOFn = %pOFn\n", ctx, np, np);
	of_node_put(np);
}

static void m1_timer_fn(struct timer_list *t)
{
	m1_show("softirq");
}

static int m1_kick(const char *val, const struct kernel_param *kp)
{
	m1_show("process");
	mod_timer(&m1_timer, jiffies + 1);
	return 0;
}
static const struct kernel_param_ops m1_kick_ops = { .set = m1_kick };
module_param_cb(kick, &m1_kick_ops, NULL, 0200);

static int __init m1_fmt_init(void)
{
	timer_setup(&m1_timer, m1_timer_fn, 0);
	return 0;
}

static void __exit m1_fmt_exit(void)
{
	timer_shutdown_sync(&m1_timer);
}
module_init(m1_fmt_init);
module_exit(m1_fmt_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("M1 printk format demo");
```

```sh
#!/bin/sh
echo 8 > /proc/sys/kernel/printk
insmod /m1/m1_fmt.ko
for K in 0 1 2; do
  echo $K > /proc/sys/kernel/kptr_restrict; echo "== kptr_restrict=$K"
  echo 1 > /sys/module/m1_fmt/parameters/kick; sleep 1
done
echo "== /proc/kallsyms m1_show (root, kptr_restrict=2 then 1)"
grep -w m1_show /proc/kallsyms; echo 1 > /proc/sys/kernel/kptr_restrict; grep -w m1_show /proc/kallsyms
```

실행 결과 (주소·해시는 부팅마다 다르다. kptr_restrict=0은 전체, 1과 2는 `%p`/`%px`/`%pK` 줄만 남기고 나머지는 0과 같아 생략했다):

```text
== kptr_restrict=0
[    7.892902][   T76] m1_fmt: [process] %p  = 000000005160a5a7
[    7.893197][   T76] m1_fmt: [process] %px = ffffbab248bac020
[    7.893245][   T76] m1_fmt: [process] %pK = 000000005160a5a7
[    7.893290][   T76] m1_fmt: [process] %pS = m1_show+0x0/0x120 [m1_fmt]  %ps = m1_show [m1_fmt]
[    7.894088][   T76] m1_fmt: [process] caller %pS = m1_kick+0x20/0x50 [m1_fmt]
[    7.894120][   T76] m1_fmt: [process] %pe = -ETIMEDOUT, -EPROBE_DEFER
[    7.894294][   T76] m1_fmt: [process] %*ph = de ad be ef 01 02 03 04
[    7.894364][   T76] m1_fmt: [process] %pOF = /pl011@9000000  %pOFn = pl011
[    7.904403][    C0] m1_fmt: [softirq] %p  = 000000005160a5a7
[    7.904487][    C0] m1_fmt: [softirq] %px = ffffbab248bac020
[    7.904500][    C0] m1_fmt: [softirq] %pK = 000000005160a5a7
[    7.904512][    C0] m1_fmt: [softirq] %pS = m1_show+0x0/0x120 [m1_fmt]  %ps = m1_show [m1_fmt]
[    7.904575][    C0] m1_fmt: [softirq] caller %pS = m1_timer_fn+0x20/0x30 [m1_fmt]
[    7.904592][    C0] m1_fmt: [softirq] %pe = -ETIMEDOUT, -EPROBE_DEFER
[    7.904606][    C0] m1_fmt: [softirq] %*ph = de ad be ef 01 02 03 04
[    7.904620][    C0] m1_fmt: [softirq] %pOF = /pl011@9000000  %pOFn = pl011
== kptr_restrict=1
[    8.923968][   T76] m1_fmt: [process] %p  = 000000005160a5a7
[    8.924384][   T76] m1_fmt: [process] %px = ffffbab248bac020
[    8.924415][   T76] m1_fmt: [process] %pK = ffffbab248bac020
...
[    8.928255][    C0] m1_fmt: [softirq] %p  = 000000005160a5a7
[    8.928306][    C0] m1_fmt: [softirq] %px = ffffbab248bac020
[    8.928318][    C0] m1_fmt: [softirq] %pK =         pK-error
...
== kptr_restrict=2
[    9.963820][   T76] m1_fmt: [process] %p  = 000000005160a5a7
[    9.963899][   T76] m1_fmt: [process] %px = ffffbab248bac020
[    9.963915][   T76] m1_fmt: [process] %pK = 0000000000000000
...
[    9.977717][    C1] m1_fmt: [softirq] %p  = 000000005160a5a7
[    9.977845][    C1] m1_fmt: [softirq] %px = ffffbab248bac020
[    9.977876][    C1] m1_fmt: [softirq] %pK = 0000000000000000
...
== /proc/kallsyms m1_show (root, kptr_restrict=2 then 1)
[   14.944861][    C1] hrtimer: interrupt took 62571120 ns
0000000000000000 t m1_show	[m1_fmt]
ffffbab248baa068 t m1_show	[m1_fmt]
```

출력에서 볼 것:

- `%p`는 **부팅 내내 같은 해시**(`000000005160a5a7`)다. 같은 객체인지 비교는 되지만 주소는 알 수 없다. 4.15부터 이렇게 바뀌었다(커널 주소 유출 → KASLR 무력화를 막기 위해). 디버그 중 진짜 주소가 필요하면 `%px`를 쓰거나 부팅 인자 `no_hash_pointers`(커널이 큰 경고 배너를 찍는다 — 이 노트에서 실행하지 않음).
- `%pK`: 0이면 `%p`와 같은 해시, 1이면 CAP_SYSLOG가 있는 root 프로세스에서 진짜 주소, **softirq·hardirq에서는 `pK-error`**(권한을 물어볼 "현재 프로세스"가 의미 없으므로 — `lib/vsprintf.c`의 `restricted_pointer()`), 2면 항상 0. `%pK`는 원래 `/proc`·sysfs 파일로 내보낼 때 쓰라고 만든 것이고, printk에서는 이렇게 컨텍스트에 따라 결과가 달라진다.
- `/proc/kallsyms`도 같은 규칙이다(root인데도 2면 0).
- `%pS`의 `+0x20/0x50`은 "함수 시작에서 오프셋 / 함수 크기". `__builtin_return_address(0)`를 `%pS`로 찍으면 **누가 불렀는지**가 나온다 — `m1_kick` vs `m1_timer_fn`. 콜백이 어디서 등록됐는지 모를 때 `pr_info("cb=%pS\n", ops->fn)`이 강력하다.
- `[   T76]` vs `[    C0]`/`[    C1]`: PRINTK_CALLER가 프로세스 컨텍스트는 PID, 인터럽트(softirq 포함) 컨텍스트는 CPU 번호로 표시했다. 타이머가 CPU0에서도 CPU1에서도 돌았다.
- `%*ph`는 길이를 `int`로 넘겨야 한다(`sizeof`는 `size_t`라 캐스트). 64바이트보다 길면 `print_hex_dump()`를 쓴다.

```svg
<svg viewBox="0 0 660 260" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="330" y="20" text-anchor="middle" font-size="14" font-weight="bold">%pK 결정 경로 (lib/vsprintf.c restricted_pointer, 예제 9 실측)</text>
  <rect x="250" y="36" width="160" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="330" y="56" text-anchor="middle" font-size="12">kptr_restrict = ?</text>
  <line x1="290" y1="66" x2="110" y2="104" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="330" y1="66" x2="330" y2="104" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="370" y1="66" x2="550" y2="104" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="180" y="82" font-size="12">0</text><text x="338" y="88" font-size="12">1</text><text x="470" y="82" font-size="12">2</text>
  <rect x="30" y="106" width="160" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="110" y="126" text-anchor="middle" font-size="12">%p처럼 해시</text>
  <text x="110" y="142" text-anchor="middle" font-size="12">000000005160a5a7</text>
  <rect x="250" y="106" width="160" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="330" y="126" text-anchor="middle" font-size="12">IRQ/softirq/NMI</text>
  <text x="330" y="142" text-anchor="middle" font-size="12">컨텍스트인가?</text>
  <rect x="470" y="106" width="160" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="550" y="126" text-anchor="middle" font-size="12">항상 0</text>
  <text x="550" y="142" text-anchor="middle" font-size="12">0000000000000000</text>
  <line x1="290" y1="150" x2="190" y2="186" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="370" y1="150" x2="460" y2="186" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="220" y="166" font-size="12">예</text><text x="420" y="166" font-size="12">아니오</text>
  <rect x="100" y="188" width="180" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="190" y="208" text-anchor="middle" font-size="12">pK-error</text>
  <text x="190" y="224" text-anchor="middle" font-size="12">(timer softirq에서 실측)</text>
  <rect x="370" y="188" width="200" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="470" y="208" text-anchor="middle" font-size="12">CAP_SYSLOG + uid=euid</text>
  <text x="470" y="224" text-anchor="middle" font-size="12">이면 진짜 주소, 아니면 0</text>
</svg>
```
그림 6 — `%pK`의 세 갈래. 같은 printk라도 어디서(프로세스 vs softirq), 누가(root vs 일반) 부르냐에 따라 결과가 다르다. printk 디버그에는 `%px`나 `%p`를, 파일 인터페이스에는 `%pK`를 쓴다.

---

## 8. 리더 쪽 — `/dev/kmsg`, `dmesg`, `dmesg_restrict`

### 8.1 정의

- **`/dev/kmsg` 읽기**: 한 번의 `read()`가 레코드 하나. 형식은 `<prefix>,<seq>,<timestamp_us>,<flags>[,caller=..];<message>` + (`dev_*` 메시지면) `\n SUBSYSTEM=...` 같은 키 줄. seq가 건너뛰면 그 사이가 덮어써진 것(`read()`가 `-EPIPE`). `prefix = facility × 8 + level`. 커널 메시지는 facility 0(kern).
- **`/dev/kmsg` 쓰기**: 유저 공간이 링버퍼에 한 줄 넣는다. `<N>` 접두어로 레벨(과 facility)을 준다. 커널 facility(0)로는 못 쓴다 — 0이면 user(1)로 바뀐다. systemd가 부팅 초기에 이걸로 로그를 남기고, 테스트 스크립트가 "여기서부터 테스트 시작" 마커를 찍는 데 쓴다.
- **`printk.devkmsg=`** (`/proc/sys/kernel/printk_devkmsg`): `on`/`off`/`ratelimit`(기본). ratelimit은 **열린 파일마다** 5초에 10줄이다.
- **`dmesg`**(util-linux): `-w`/`--follow`(새 메시지 대기), `-W`(새 것만), `-x`(facility:level 해석), `-T`(벽시계 — 부팅 후 suspend가 있었으면 틀린다, "may be inaccurate!"), `-l err,warn`(레벨 필터), `-k`/`-u`(커널/유저 facility만), `-H`(사람용), `-c`(읽고 지움), `-n`(콘솔 레벨). BusyBox `dmesg`는 `-c -n -s -r`만 있다.
- **`kernel.dmesg_restrict`** (`CONFIG_SECURITY_DMESG_RESTRICT` 기본값): 1이면 CAP_SYSLOG 없는 사용자는 못 읽는다.

### 8.2 예제 10 — 유저 공간에서 쓰기, 원시 레코드, dmesg 옵션, devkmsg ratelimit

무엇을 확인하나: 레벨 접두어를 바꿔 `/dev/kmsg`에 쓰고, 원시 레코드와 `dmesg -x -l -T`로 읽고, 한 fd로 30줄을 쏟아 ratelimit을 본 다음, `dmesg_restrict=1`에서 nobody로 읽어 본다.

```sh
#!/bin/sh
mkdir -p /mnt/r && mount -o ro,noload /dev/vda /mnt/r && mount -t devtmpfs d /mnt/r/dev && mount -t proc p /mnt/r/proc
X="chroot /mnt/r"
echo "== devkmsg mode: $(cat /proc/sys/kernel/printk_devkmsg)"
echo "<3>m1_user: an error written from userspace" > /dev/kmsg
echo "<12>m1_user: user.warning (facility 1, level 4)" > /dev/kmsg
echo "m1_user: no prefix -> user.info" > /dev/kmsg
exec 3>/dev/kmsg; i=0; while [ $i -lt 30 ]; do echo "m1_user: flood $i" >&3; i=$((i+1)); done
sleep 6; echo "m1_user: after 6 s" >&3; exec 3>&-
echo "== raw /dev/kmsg records (pri,seq,ts_us,flags;msg)"; $X timeout 1 cat /dev/kmsg > /tmp/k.txt; grep -E "m1_user: (an|user|no)" /tmp/k.txt
echo "== dmesg -x (facility:level)"; $X dmesg -x | grep -E "m1_user|suppressed" | head -8; echo ...
$X dmesg -x | grep -E "m1_user|suppressed" | tail -3
echo "== dmesg -l err,warn -x"; $X dmesg -l err,warn -x | tail -3
echo "== dmesg -T (wall clock), -r (raw)"; $X dmesg -T | tail -2; $X dmesg -r | tail -2
echo "== dmesg_restrict=1, as uid 65534"; echo 1 > /proc/sys/kernel/dmesg_restrict
$X setpriv --reuid=65534 --regid=65534 --clear-groups dmesg | head -2; echo "rc=$?"
```

```text
== devkmsg mode: ratelimit
[   11.557156][   T76] m1_user: an error written from userspace
== raw /dev/kmsg records (pri,seq,ts_us,flags;msg)
11,306,11557156,-,caller=T76;m1_user: an error written from userspace
12,307,11558146,-,caller=T76;m1_user: user.warning (facility 1, level 4)
12,308,11560125,-,caller=T76;m1_user: no prefix -> user.info
== dmesg -x (facility:level)
user  :err   : [   11.557156] m1_user: an error written from userspace
user  :warn  : [   11.558146] m1_user: user.warning (facility 1, level 4)
user  :warn  : [   11.560125] m1_user: no prefix -> user.info
user  :warn  : [   11.562396] m1_user: flood 0
user  :warn  : [   11.574488] m1_user: flood 1
user  :warn  : [   11.582152] m1_user: flood 2
user  :warn  : [   11.584421] m1_user: flood 3
user  :warn  : [   11.584969] m1_user: flood 4
...
user  :warn  : [   11.592201] m1_user: flood 9
user  :warn  : [   17.777715] m1_user: after 6 s
kern  :warn  : [   17.778654] printk: t.sh: 20 output lines suppressed due to ratelimiting
== dmesg -l err,warn -x
user  :warn  : [   11.592201] m1_user: flood 9
user  :warn  : [   17.777715] m1_user: after 6 s
kern  :warn  : [   17.778654] printk: t.sh: 20 output lines suppressed due to ratelimiting
== dmesg -T (wall clock), -r (raw)
[Sun Oct 18 06:44:17 2026] m1_user: after 6 s
[Sun Oct 18 06:44:17 2026] printk: t.sh: 20 output lines suppressed due to ratelimiting
<5>[   17.777715] m1_user: after 6 s
<4>[   17.778654] printk: t.sh: 20 output lines suppressed due to ratelimiting
== dmesg_restrict=1, as uid 65534
dmesg: read kernel buffer failed: Operation not permitted
rc=0
```

출력에서 볼 것:

- 원시 레코드의 첫 필드: `<3>` → **11**(= user 1 × 8 + err 3). 유저는 kern facility로 위장할 수 없다. 접두어 없음 → 12(user.warning) — 기본이 info가 아니라 `default_message_loglevel`(4)이었다(메시지 문구의 "user.info"는 틀린 기대였다). `caller=T76`은 PRINTK_CALLER가 `/dev/kmsg`에 주는 필드.
- `<3>` 줄만 콘솔에 바로 찍혔다(quiet, cl=4 → err만). `/dev/kmsg` 쓰기도 커널 메시지와 같은 콘솔 규칙을 탄다.
- 같은 fd로 30줄 → 10줄만 남고, 다음 쓰기(또는 close) 때 `printk: t.sh: 20 output lines suppressed due to ratelimiting`. 매번 `echo > /dev/kmsg`처럼 새로 열면 파일마다 새 ratelimit 상태라 **제한에 안 걸린다**(이 노트를 쓰며 Image-sec에서 처음 시도했을 때 그렇게 30줄이 다 들어갔다). 제한을 풀려면 부팅 인자 `printk.devkmsg=on`.
- `dmesg -r`의 `<5>`는 원시 레코드의 12와 맞지 않는다. util-linux가 `-r` 출력에서 숫자를 다시 계산하는 방식 때문으로 보이지만 확인하지 않았다 — 정확한 prefix는 `/dev/kmsg`에서 직접 본다.
- `-T`의 시각은 부팅 시각 + 모노토닉 타임스탬프로 계산한 추정치다. suspend가 끼면 틀린다. 필드 로그는 journald의 실제 wall clock(L1)을 쓴다.
- `dmesg_restrict=1` + 일반 사용자 → `Operation not permitted`. 보안 강화 이미지(L6)에서 기본으로 켠다.

---

## 9. 커널 안에서는 무슨 일이 일어나나

`pr_info("x=%d\n", x)` 한 줄의 경로(6.18 기준, `kernel/printk/`):

1. `pr_info` → `printk(KERN_INFO pr_fmt(fmt), ...)` → `_printk()` → `vprintk()` → `vprintk_default()` → **`vprintk_emit()`**.
2. `vprintk_store()`: 레벨 접두어 파싱, `vsnprintf()`로 텍스트 생성(여기서 `%pS`면 kallsyms 조회, `%p`면 siphash), 시각(`local_clock()`), caller ID, `dev_printk_info`를 채운다.
3. **`prb_reserve()` → 쓰기 → `prb_commit()`/`prb_final_commit()`**: `printk_ringbuffer.c`의 lockless 링버퍼. 디스크립터 링(상태 + seq) + 텍스트 데이터 링 두 개로 되어 있고, cmpxchg로 자리를 잡아 여러 CPU·NMI가 동시에 써도 안전하다. Don의 펌웨어 로그 링버퍼와 같은 발상인데, "쓰는 중" 상태를 디스크립터에 두어 리더가 미완성 레코드를 건너뛰게 한 것이 핵심이다.
4. 콘솔 처리: `printk_get_console_flush_type()`이 지금 컨텍스트에서 무엇을 할지 고른다 — nbcon이면 kthread 깨우기(`nbcon_kthreads_wake()`) 또는 비상시 `nbcon_atomic_flush_pending()`, legacy면 `console_trylock_spinning()` → `console_unlock()`이 **호출자 안에서** `console_flush_all()`로 모든 legacy 콘솔의 `->write()`를 부른다(예제 8의 2초가 여기).
5. 각 콘솔 출력 직전 `suppress_message_printing(level)` = `level >= console_loglevel && !ignore_loglevel`이면 건너뛴다(§2의 규칙). 리더 쪽(`devkmsg_read()`, `syslog_print()`)에는 이 필터가 없다.
6. 링버퍼가 차면 가장 오래된 레코드가 덮어써지고, 아직 그걸 못 찍은 콘솔은 seq 차이로 "N printk messages dropped"를 찍는다.

`pr_debug`의 경로는 1번에서 갈라진다: `dynamic_pr_debug()` → `static_branch_unlikely(&descriptor.key.dd_key_false)`. 꺼져 있으면 여기서 끝(NOP). 켜져 있으면 `__dynamic_pr_debug()`가 플래그 접두어를 붙여 `printk(KERN_DEBUG ...)`.

| 드라이버를 쓸 때 다시 만나는 곳 | 노트 |
|---|---|
| `dev_err_probe()`로 probe 에러 경로 정리, deferral 이유 기록 | G3, D3 |
| UART 드라이버의 콘솔 구현 (`struct console`, `write_atomic`/`write_thread`, `OF_EARLYCON_DECLARE`) | I3 (UART), C6 |
| 인터럽트 핸들러 안의 ratelimited 로그 | F2, H 모듈 |
| netconsole·pstore console — 콘솔의 다른 구현 | L7 |
| `trace_printk()` — 콘솔·링버퍼 대신 ftrace 버퍼로, 수십 배 싸고 순서가 정확 | M5 |
| Oops/WARN 출력이 nbcon `write_atomic`으로 바로 나가는 경로 | M3 |

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">vprintk_emit() 안의 갈림길 (kernel/printk/)</text>
  <rect x="230" y="34" width="220" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="54" text-anchor="middle" font-size="12">vprintk_store: vsnprintf + 시각</text>
  <rect x="230" y="86" width="220" height="30" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="106" text-anchor="middle" font-size="12">prb_reserve → prb_commit</text>
  <line x1="340" y1="64" x2="340" y2="84" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="230" y="138" width="220" height="30" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="158" text-anchor="middle" font-size="12">flush type 결정</text>
  <line x1="340" y1="116" x2="340" y2="136" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="14" y="200" width="200" height="74" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="114" y="220" text-anchor="middle" font-size="12">nbcon 평소</text>
  <text x="114" y="240" text-anchor="middle" font-size="12">pr/ttyAMA kthread 깨움</text>
  <text x="114" y="260" text-anchor="middle" font-size="12">호출자 즉시 복귀</text>
  <rect x="240" y="200" width="200" height="74" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="220" text-anchor="middle" font-size="12">nbcon 비상 (panic/Oops)</text>
  <text x="340" y="240" text-anchor="middle" font-size="12">write_atomic</text>
  <text x="340" y="260" text-anchor="middle" font-size="12">호출자가 직접</text>
  <rect x="466" y="200" width="200" height="74" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="566" y="220" text-anchor="middle" font-size="12">legacy 콘솔</text>
  <text x="566" y="240" text-anchor="middle" font-size="12">console_unlock 안에서</text>
  <text x="566" y="260" text-anchor="middle" font-size="12">호출자가 직접 (느림)</text>
  <line x1="290" y1="168" x2="130" y2="198" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="340" y1="168" x2="340" y2="198" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="390" y1="168" x2="550" y2="198" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="470" y="106" font-size="12">리더(/dev/kmsg)는</text>
  <text x="470" y="124" font-size="12">여기서 바로 읽는다</text>
</svg>
```
그림 7 — 저장은 한 길, 출력은 세 길. 예제 8의 "110 ms vs 2.1 s"는 왼쪽 길과 오른쪽 길의 차이다.

---

## 10. Embedded 실무에서 다시 보기

### 10.1 센서 키트 bring-up — IMU 드라이버가 probe되지 않는다

i.MX8 보드에 SPI IMU를 붙였는데 `/sys/bus/iio/devices`가 비어 있다. 순서:

```sh
dmesg -l err,warn                                         # 1) 에러부터
cat /sys/kernel/debug/devices_deferred                    # 2) deferral 이유 (dev_err_probe가 기록)
echo 'file drivers/base/dd.c +p' > /proc/dynamic_debug/control
echo 'module bmi270 +pfl' > /proc/dynamic_debug/control   # 3) 드라이버 자신의 dev_dbg
echo 1 > /sys/bus/spi/drivers_probe 2>/dev/null; modprobe -r bmi270_spi; modprobe bmi270_spi dyndbg=+pfl
dmesg -w                                                  # 4) 실시간으로 따라가기
```

(가이드, 실행 안 함 — 드라이버 이름은 예시.) "matched" 줄이 없으면 DT compatible(D3), "probing"은 있는데 실패면 드라이버 로그, deferral이면 공급자(regulator·clock·gpio)가 아직 안 뜬 것이다.

### 10.2 실시간 루프 옆의 콘솔 로그

1 kHz 모터 제어 스레드가 가끔 2~3 ms 늦는다. 원인 후보 1순위: **legacy 콘솔(예: 8250 UART)에 누군가 printk storm**. 제품 부팅 인자에 `quiet`(또는 `loglevel=3`)을 넣고, 드라이버 에러 경로는 ratelimited로, 디버그는 dyndbg로. nbcon 콘솔(PL011, i.MX)로 바뀐 커널이면 호출자 지연은 줄지만 대신 "messages dropped"로 로그를 잃는다(예제 8). PREEMPT_RT 커널은 legacy 콘솔도 `pr/legacy` kthread로 민다(N1, N2).

### 10.3 콘솔 UART가 없는 양산 보드

양산 보드는 디버그 UART 핀이 막혀 있다. 대안: netconsole(UDP로 콘솔, L7), pstore console/ramoops(다음 부팅에 회수, L7), journald가 `/dev/kmsg`를 읽어 저장(L1). 셋 다 "콘솔"이거나 "리더"다 — 그림 1의 어느 쪽인지 구분하면 레벨 필터가 걸리는지(콘솔) 안 걸리는지(리더) 바로 안다.

### 10.4 테스트 자동화의 마커

HIL 테스트에서 "여기서부터 케이스 17"을 커널 로그에 같이 남기면 dmesg만으로 시간 축이 맞춰진다:

```sh
echo "<5>hil: === case 17 start (motor ramp) ===" > /dev/kmsg
```

(가이드 — 예제 10에서 같은 메커니즘을 확인했다.) M11의 "버스 트레이스와 커널 로그 맞추기"에서 GPIO 토글과 함께 쓴다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `loglevel=8`만 주고 `pr_debug`를 기다림 | 디버그 줄이 안 나옴 | dyndbg 사이트가 꺼져 있음 | `dyndbg=` / control 파일로 `+p` |
| BusyBox initramfs에서 `mod.dyndbg=+p` 부팅 인자 | 효과 없음 | 그 인자는 kmod modprobe가 적용 | `insmod x.ko dyndbg=+p` 또는 modprobe |
| control 쿼리 오타 | 아무 일도 없음, 에러도 없음 | 매치 0개도 성공으로 반환 | 쓴 뒤 `grep` 으로 `=p` 확인 |
| earlycon 없이 bring-up | `Starting kernel ...` 후 침묵 | 정식 콘솔 전 크래시 | `earlycon`(+ 주소), 그다음 M3 |
| earlycon만 넣고 `console=` 없음 | 수십 줄 후 멈춘 듯 | tty0로 인계(C6) | `console=ttyXXX` 함께 |
| ISR 에러 경로에 plain `dev_err` | 장치 고장 시 시스템 정지 | printk storm | `dev_err_ratelimited` |
| 디버그용 `echo 8 > /proc/sys/kernel/printk`가 제품에 남음 | 실시간 지터, 부팅 지연 | 115200 콘솔 출력 비용 | 제품은 `quiet`, 레벨 정책(§12 Q5) |
| `%p`로 주소를 찍고 비교 | 해시값이라 실제 주소와 안 맞음 | 4.15+ 해싱 | 디버그는 `%px`(임시), 심볼은 `%pS` |
| softirq에서 `%pK` | `pK-error` | kptr_restrict=1의 권한 검사 | printk에는 `%p`/`%px` |
| `/dev/kmsg`에 매번 새로 열어 대량 쓰기 | ratelimit이 안 걸려 버퍼 오염 | 상태가 열린 파일마다 | 한 fd 유지 또는 journald 사용 |

---

## 12. 면접에서 이렇게 말한다

**Q.** A driver works when you add printk debugging but fails without it. What does that tell you?

**A.** printk가 바꾸는 건 **타이밍**이다. 레이스·순서 문제(배리어 누락, 락 없는 공유 변수, 하드웨어 ready 비트를 안 기다림)일 가능성이 가장 크다. printk는 수 µs~ms의 지연, 락(legacy 콘솔), 캐시·배리어 효과를 넣어 숨은 순서를 바꾼다. 그래서 printk를 빼고 `trace_printk`나 ftrace 이벤트처럼 가벼운 계측으로 바꾸고, lockdep·KCSAN을 켜고(M8), 레지스터 접근 사이의 지연·폴링 조건을 데이터시트와 대조한다. 내 펌웨어 경험으로는 NAND ready/busy를 안 보고 고정 지연에 의존하던 코드가 로그 레벨을 올리면 고쳐지는 경우가 전형이었다.

> It tells me the bug is almost certainly timing-related — a race or a missing ordering guarantee. printk adds microseconds to milliseconds of delay, and with a legacy console it also takes locks and can disable interrupts, so it silently serializes things. My next step is to replace printk with low-overhead instrumentation like trace_printk or tracepoints, enable lockdep and KCSAN, and check every hardware handshake: am I polling a ready bit, or relying on a delay that the printk was accidentally providing? I've seen exactly this on SSD firmware where a NAND busy wait was missing and higher log levels "fixed" it.

**꼬리 질문**: "Why trace_printk instead of printk?" → per-CPU ftrace 링버퍼에 기록만 하고 콘솔로 안 나간다. 포맷팅도 지연 가능(`trace_bprintk`), 순서와 타임스탬프가 정확(M5).

**Q.** How do you debug a crash that happens before the console is up?

**A.** 1단계는 `earlycon`이다. DT에 stdout-path가 있으면 인자 없이, 없으면 `earlycon=<드라이버>,<물리주소>`. arm64는 `setup_arch`에서 켜지므로 그 뒤의 크래시는 Oops 전체가 보인다(예제 6: `mem=24M`로 0초 크래시 → earlycon 없이 0줄, 있으면 콜스택 전체). earlycon으로도 아무것도 안 나오면 커널 진입 전후 문제라 부트로더 핸드오프(DTB 주소, 로드 주소)와 UART 클록·핀먹스를 본다. 그다음은 JTAG로 PC와 `__log_buf` 메모리를 읽거나, ramoops로 다음 부팅에 회수한다.

> First, earlycon: either bare "earlycon" if the device tree has a stdout-path, or earlycon=pl011 plus the UART physical address. On arm64 it comes up in setup_arch, before the memory allocator, so anything after that point prints a full oops — I reproduced this with a mem= value that crashes at time zero, and got nothing without earlycon and a full call trace with it. If even earlycon is silent, the kernel probably never reached setup_arch, so I check the bootloader handoff — DTB pointer, image load address — and whether the UART clock and pinmux are still on. After that, JTAG: halt, read the PC, and dump the printk buffer from memory using the symbol address from vmlinux, or use ramoops to recover the log on the next boot.

**Q.** How do you enable dev_dbg output without rebuilding the kernel?

**A.** `CONFIG_DYNAMIC_DEBUG`가 있으면 모든 `pr_debug`/`dev_dbg`가 꺼진 채 컴파일돼 있다. `/proc/dynamic_debug/control`에 `module foo +p`, `file drivers/i2c/x.c line 120 +pfl`처럼 쓴다. probe 시점을 보려면 로드 순간 적용되는 `modprobe foo dyndbg=+p`, 빌트인은 부팅 인자 `dyndbg="file ... +p"`. 꺼진 사이트는 static key NOP라 비용이 거의 없다. 주의: 매치가 없어도 에러가 안 나니 control을 grep해서 확인하고, `foo.dyndbg=` 부팅 인자는 modprobe가 적용한다.

> With CONFIG_DYNAMIC_DEBUG, every pr_debug and dev_dbg call site is compiled in but disabled behind a static key. I write queries to /proc/dynamic_debug/control — by module, file, function, line or format — with flags like +p to print and +flmt to add function, line, module and thread ID. For probe-time messages I pass dyndbg=+p as a module parameter so it applies at load, and for built-in code I use the dyndbg= boot parameter. Two gotchas: a query that matches nothing still succeeds, so I grep the control file to confirm, and the per-module boot parameter is applied by modprobe, not by insmod.

**Q.** What's the difference between console_loglevel and what dmesg shows?

**A.** 링버퍼에는 레벨과 무관하게 전부 들어간다. `console_loglevel`은 **콘솔(UART 등)로 내보낼 때만** 거는 필터다(레벨 < console_loglevel). dmesg·journald는 리더라서 전부 본다. 그래서 `quiet` 부팅에서도 dmesg에는 전부 있다(예제 1, 2).

> The ring buffer stores every message regardless of level. console_loglevel only filters what gets written to console devices like the serial port: a message goes out if its level number is below the console level. Readers like dmesg and journald see everything. That's why quiet makes the serial console almost silent but dmesg still has the full boot log.

**Q.** Can you call printk from an interrupt handler? What's the cost?

**A.** 가능하다. 5.10 이후 링버퍼는 lockless라 hardirq·NMI에서도 저장은 안전하다. 비용은 포맷팅 + 저장(수 µs)과, legacy 콘솔이면 그 자리에서 UART로 찍는 시간(115200에서 한 줄 약 8 ms). 6.x의 nbcon 콘솔은 kthread로 미루므로 호출자 비용은 줄지만, 폭주하면 링버퍼가 넘쳐 메시지를 잃는다. 그래서 ISR에서는 ratelimited 변형만, 고빈도 계측은 tracepoint로.

> Yes — since 5.10 the printk ring buffer is lockless, so storing a record is safe from any context including NMI. The cost is formatting plus the store, and with a legacy console, the actual serial output happens synchronously in the caller: at 115200 baud a typical 90-byte line is about 8 ms. Newer nbcon consoles hand printing to a per-console kthread, so the caller returns quickly, but under a storm the console falls behind and the ring buffer overwrites messages — I measured both effects in QEMU. In an ISR I only use the ratelimited variants, and use tracepoints for anything high-frequency.

**Q.** What log-level policy would you set for a production embedded product?

**A.** (1) 부팅 인자 `quiet` 또는 `loglevel=3`으로 콘솔은 에러만, (2) dmesg/journald에는 info까지 남겨 필드 분석용으로, (3) 드라이버는 `dev_*`만 쓰고, 정상 경로는 `dev_dbg`(dyndbg로 현장에서 켤 수 있게), 반복 가능한 에러는 ratelimited, (4) `CONFIG_DYNAMIC_DEBUG`는 유지, (5) `dmesg_restrict=1`, `kptr_restrict=1`, `%px` 금지, (6) 크래시 로그는 pstore/ramoops로 회수(L7).

> Console at error level only — quiet or loglevel=3 — because serial output costs boot time and real-time latency. The ring buffer and journald still keep info level for field diagnostics. Drivers use the dev_ helpers: normal flow at dev_dbg so we can enable it in the field with dynamic debug, real errors at dev_err, and anything that can repeat is ratelimited. I keep dynamic debug compiled in, set dmesg_restrict and kptr_restrict, ban %px in review, and capture panics with ramoops so they survive the reboot.

**Q.** Why does `%p` print a weird number instead of the address?

**A.** 4.15부터 `%p`는 부팅마다 랜덤 키로 siphash한 값을 찍는다. 커널 주소가 로그로 새면 KASLR이 무력화되기 때문이다. 같은 포인터는 같은 해시라 비교는 된다. 진짜 주소가 필요하면 `%px`(리뷰 대상), 디버그 부팅에 `no_hash_pointers`, 함수면 `%pS`가 더 낫다.

> Since 4.15, plain %p prints a hashed value, keyed randomly per boot, so kernel addresses don't leak into logs and defeat KASLR. The same pointer always hashes to the same value, so you can still correlate. If I really need the address I use %px with a justification, or boot with no_hash_pointers on a debug build; for code pointers %pS is better anyway because it gives symbol plus offset.

---

## 13. 직접 해보기

1. (개념) `/proc/sys/kernel/printk`가 `7 4 1 7`일 때 `printk("hello\n")`는 콘솔에 나오나? `quiet`로 부팅했다면? 정답/힌트: default_message_loglevel 4 → 4 < 7이라 나온다. quiet(cl=4)면 4 < 4가 거짓이라 안 나온다(예제 1의 "no level" 줄).
2. (dyndbg) 예제 3의 모듈에서 `format "tick"` 쿼리로 tick 사이트만 켜 보고, `=_`로 다시 꺼서 control 표시를 확인하라. 정답/힌트: `echo 'module m1_mod format "tick" +p' > /proc/dynamic_debug/control` → `m1_mod.c:9 ... =p`.
3. (부팅 인자) `dyndbg="file drivers/base/dd.c +p"`로 부팅해서 PL011(`9000000.pl011`)이 어떤 버스에서 어떤 드라이버와 bind되는지 찾아라. 정답/힌트: Image-sec로 미리 돌려 본 출력에 `amba 9000000.pl011: bus: 'amba': really_probe: probing driver uart-pl011`이 있었다.
4. (코드) `DEFINE_RATELIMIT_STATE(rs, HZ, 3)`을 써서 1초에 3줄만 찍는 `m1_tick`을 만들고, 억제 메시지가 어떤 함수 이름으로 나오는지 보라. 정답/힌트: `__ratelimit(&rs)`가 false면 건너뛴다. 억제 개수는 `ratelimit_state_exit()`나 다음 성공 때 `"%s: %d callbacks suppressed"`(`lib/ratelimit.c`)로 함수 이름과 함께 찍힌다.
5. (측정) 예제 8을 `n=500`으로 줄여 nbcon에서 "messages dropped"가 사라지는 n을 찾아라. 그리고 `log_buf_len=1M` 부팅 인자로 n=2000에서 dropped가 줄어드는지 확인하라. 정답/힌트: 128 KiB ÷ 레코드 크기(텍스트 ~70 B + 메타데이터)로 대략 계산되는 개수 부근에서 시작된다. 버퍼를 키우면 콘솔이 따라잡을 시간이 생긴다.
6. (관찰) `cat /dev/kmsg`를 백그라운드로 띄워 두고 `dmesg -C`를 한 뒤에도 `/dev/kmsg` 리더가 계속 새 레코드를 받는지, `dmesg`(syslog 방식 `-S`)와 무엇이 다른지 보라. 정답/힌트: `-C`는 syslog 읽기 위치만 옮긴다. `/dev/kmsg` 리더는 각자 seq를 들고 있다.
7. (earlycon) Image-trace를 `earlycon` 없이, `console=ttyAMA0 initcall_blacklist=pl011_init`로 부팅해서 아무것도 안 나오는 걸 본 뒤, 같은 부팅을 `earlycon`만 더해 끝까지 보라. 마지막 쪽 "Warning: unable to open an initial console."의 의미는? 정답/힌트: C6 예제 11. bootconsole이 끝까지 살아 있지만 `/dev/console`을 열 정식 콘솔이 없다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| printk | 커널 로그 함수 | 레벨 접두어 + 포맷 → 링버퍼 + 콘솔 |
| log level | 0(EMERG)~7(DEBUG) | 숫자가 작을수록 중요 |
| console_loglevel | 콘솔 출력 문턱 | 레벨 < 이 값이면 콘솔로 |
| printk_ringbuffer | 커널 로그 저장소 | 5.10+ lockless, 기본 `2^LOG_BUF_SHIFT` |
| console | 출력 장치 콜백 | UART, netconsole, VT, pstore |
| bootconsole | 임시 콘솔 (`CON_BOOT`) | earlycon. 정식 콘솔 등록 시 해제 |
| earlycon | 이른 UART 출력 | `setup_arch` 시점부터, 레지스터만 사용 |
| keep_bootcon | 부팅 인자 | bootconsole 해제를 막음 (중복 출력) |
| preferred console | `/dev/console`이 될 콘솔 | 마지막 `console=` |
| nbcon | non-blocking console | 6.x, kthread 출력 + 비상시 atomic 출력 |
| legacy console | 옛 콘솔 | 호출자가 `console_unlock()`에서 직접 출력 |
| pr_fmt / dev_fmt | 접두어 매크로 | 파일 전체 로그 앞에 붙는 문자열 |
| dev_printk_info | 레코드 메타데이터 | SUBSYSTEM/DEVICE 키 (journald 필터) |
| dynamic debug | `pr_debug` 런타임 스위치 | control 파일, `dyndbg=` |
| `_ddebug` | dyndbg 사이트 디스크립터 | `__dyndbg` 섹션, static key 포함 |
| ratelimit | 출력 빈도 제한 | 기본 5초당 10회 |
| PRINTK_CALLER | 호출자 ID | `T<pid>` 또는 `C<cpu>` |
| `/dev/kmsg` | 링버퍼 리더/라이터 | 레코드 단위, seq로 유실 감지 |
| devkmsg | `/dev/kmsg` 쓰기 정책 | on/off/ratelimit |
| dmesg_restrict | 읽기 권한 | 1이면 CAP_SYSLOG 필요 |
| kptr_restrict | `%pK`·kallsyms 정책 | 0 해시, 1 권한 검사, 2 항상 0 |
| `%p` hashing | 포인터 해시 출력 | 4.15+, KASLR 보호 |
| `trace_printk` | ftrace 버퍼로 쓰는 printk | 디버그 전용, 훨씬 가벼움 (M5) |

---

## 15. 요약 & 체크리스트

printk는 Don이 펌웨어에서 만들던 "레벨 붙은 로그 → RAM 링버퍼 → UART" 구조의 표준판이다. 레코드는 레벨과 무관하게 lockless 링버퍼에 전부 저장되고, 콘솔만 `console_loglevel`로 거른다(`quiet`=4). `pr_debug`/`dev_dbg`는 dynamic debug 사이트로 컴파일돼 꺼져 있으니, control 파일·모듈 파라미터·부팅 인자로 켠다(부팅 인자의 모듈용은 modprobe가 적용). 콘솔은 6.x에서 nbcon(kthread, 호출자는 빠르지만 폭주 시 유실)과 legacy(호출자가 직접, 느리지만 유실 없음)로 나뉘고, 이 차이를 실측으로 15~20배 확인했다. 115200 UART의 동기 출력은 줄당 약 8 ms다. earlycon은 `setup_arch`부터 동작해 정식 콘솔 전의 크래시를 보여 주는 유일한 창구다. `%p`는 해시, `%px`는 진짜, `%pK`는 정책·컨텍스트 의존, `%pS`·`%pe`·`%*ph`·`%pOF`는 드라이버 로그의 표준 도구다.

- [ ] printk 레코드가 링버퍼 → 콘솔/리더로 가는 경로를 그림으로 그릴 수 있다
- [ ] `/proc/sys/kernel/printk`의 네 값과 `loglevel=`·`quiet`·`ignore_loglevel`의 효과를 설명할 수 있다
- [ ] `pr_*` vs `dev_*` vs `netdev_*`, `pr_fmt`, `dev_err_probe`를 언제 쓰는지 말할 수 있다
- [ ] dynamic debug 쿼리(`module/file/func/line/format` + `+pflmt`)를 쓸 수 있다
- [ ] probe 시점 dbg를 보기 위해 `dyndbg=` 모듈 파라미터를 쓰고, `mod.dyndbg=` 부팅 인자의 함정을 설명할 수 있다
- [ ] earlycon 문법과, earlycon으로도 침묵할 때의 다음 단계를 말할 수 있다
- [ ] `keep_bootcon`이 무엇을 보여 주는지 설명할 수 있다
- [ ] nbcon과 legacy 콘솔의 비용·유실 차이를 수치로 말할 수 있다
- [ ] 115200 baud에서 한 줄 비용을 계산할 수 있다
- [ ] ratelimited/once 변형과 "callbacks suppressed"의 의미를 안다
- [ ] `%p`·`%px`·`%pK`·`%pS`·`%pe`·`%*ph`·`%pOF`의 출력을 예측할 수 있다
- [ ] `/dev/kmsg` 레코드 형식과 `dmesg -x -T -l -w`를 쓸 수 있다

## 참고 자료

- docs.kernel.org — [Message logging with printk](https://docs.kernel.org/core-api/printk-basics.html)
- docs.kernel.org — [How to get printk format specifiers right](https://docs.kernel.org/core-api/printk-formats.html)
- docs.kernel.org — [Dynamic debug](https://docs.kernel.org/admin-guide/dynamic-debug-howto.html)
- docs.kernel.org — [The kernel's command-line parameters](https://docs.kernel.org/admin-guide/kernel-parameters.html) (`earlycon`, `console`, `keep_bootcon`, `loglevel`, `ignore_loglevel`, `printk.devkmsg`, `no_hash_pointers`, `log_buf_len`)
- docs.kernel.org — [/dev/kmsg ABI](https://docs.kernel.org/admin-guide/abi-testing.html) (Documentation/ABI/testing/dev-kmsg)
- docs.kernel.org — [Documentation for /proc/sys/kernel/](https://docs.kernel.org/admin-guide/sysctl/kernel.html) (`printk`, `printk_devkmsg`, `dmesg_restrict`, `kptr_restrict`)
- man7.org — [dmesg(1)](https://man7.org/linux/man-pages/man1/dmesg.1.html), [syslog(2)](https://man7.org/linux/man-pages/man2/syslog.2.html)
- 랩 소스 (6.18.55): `kernel/printk/printk.c`, `kernel/printk/nbcon.c`, `kernel/printk/printk_ringbuffer.c`, `lib/vsprintf.c`(`restricted_pointer`), `lib/dynamic_debug.c`, `lib/ratelimit.c`, `drivers/base/core.c`(`__dev_probe_failed`), `drivers/tty/serial/amba-pl011.c`, `drivers/tty/serial/earlycon.c`, `fs/proc/consoles.c` — elixir.bootlin.com에서 같은 경로로 볼 수 있다
- 선행·연결 노트: C6 §5(earlycon), E3(모듈·dyndbg), E5(`%pe`), G3(`dev_err_probe`), L1(journald), L7(pstore/netconsole), M3(Oops), M5(ftrace)
