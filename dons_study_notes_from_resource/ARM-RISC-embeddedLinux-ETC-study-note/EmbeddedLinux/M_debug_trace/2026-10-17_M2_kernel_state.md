# M2. 커널 상태 들여다보기 — /proc · /sys · debugfs로 하는 bring-up 진단 체크리스트

> **이 노트를 다 읽으면**: "장치가 안 보인다"를 DT 노드 → 장치 객체 → bind → defer → probe 에러 → 자원 → 인터럽트 순서로 좁혀 가는 진단 순서를 외워서 쓸 수 있다 · `devices_deferred`, driver 링크, `of_node` 링크, `/proc/interrupts`, `/proc/iomem`, `clk_summary`, `regulator_summary`, debugfs `gpio`, regmap `registers`가 각각 어떤 질문에 답하는지 말할 수 있다 · `/proc/meminfo`·`slabinfo`·`buddyinfo`·`vmallocinfo`로 드라이버 메모리 사용과 누수를 숫자로 잡아낼 수 있다 · `/proc/<pid>/stack`·`wchan`으로 "앱이 멈췄다"를 드라이버의 어느 줄에서 자는지로 바꿔 말할 수 있다
> **왜 필요한가**: 새 보드 bring-up 첫 주의 질문은 거의 다 "그 드라이버 probe 됐어?", "IRQ 들어와?", "클럭 켜졌어?", "메모리 새?"다. 면접에서도 "Board boots but /dev/xxx never appears — where do you look?"는 단골이고, 답의 질은 **어느 파일을 어떤 순서로 보느냐**로 갈린다
> **Don 기준 난이도**: 레지스터 덤프·인터럽트 카운터·메모리 맵을 보며 HW를 진단하는 감각은 이미 있다 / 새로 배울 부분은 Linux가 그 정보를 **어느 가상 파일에 어떤 형식으로** 내놓는지, 그리고 각 숫자의 정확한 뜻(MemTotal ≠ RAM, managed IRQ의 affinity 거부, `-ENODEV`는 조용히 실패 등)
> **선행 노트**: A1·A10(`/proc` 기본, 셸 도구), D3(드라이버 매칭·`devices_deferred` 첫 소개), D4(DT 자원), G2(디바이스 모델·sysfs 트리), G3(probe·`-EPROBE_DEFER`·fw_devlink), G8(sysfs·debugfs를 드라이버가 만드는 법), H2(regmap), H3(clk·regulator), H4(pinctrl·GPIO), H6(IRQ 컨트롤러), F6(인터럽트 처리), F9(커널 메모리 할당자). 이 노트는 그 기초를 다시 설명하지 않고 **"진단할 때 어디를 어떤 순서로 보나"** 관점만 다룬다

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어를 bring-up할 때를 떠올려 보자. 새 컨트롤러 샘플이 왔는데 NAND가 안 읽힌다. 그럴 때 하는 일은 정해져 있다. 먼저 **클럭·리셋 레지스터**를 읽어 블록이 켜졌는지 보고, **핀먹스 레지스터**로 패드가 NAND 기능으로 설정됐는지 보고, **인터럽트 pending/count**를 보고, 마지막으로 **펌웨어 상태 변수**를 JTAG로 덤프한다. 측정 지점이 정해져 있고, 순서가 정해져 있다.

Linux에서도 똑같다. 다만 JTAG로 메모리를 직접 읽는 대신, 커널이 **가상 파일**로 상태를 보여 준다. 그 창구가 세 개다.

- `/proc` — 원래 "프로세스 정보"용 파일시스템이었지만, 역사적으로 커널 전역 통계(`interrupts`, `iomem`, `meminfo`, `slabinfo`…)도 여기 쌓였다. 형식이 오래 유지된다.
- `/sys` (sysfs) — 디바이스 모델(G2) 그 자체. 버스·장치·드라이버·모듈이 디렉터리와 심볼릭 링크로 보인다. "값 하나 = 파일 하나"의 안정 ABI.
- `/sys/kernel/debug` (debugfs) — 개발자용. 형식 약속이 없다(ABI 아님). 대신 clk 트리, 레귤레이터, 핀먹스, GPIO, regmap 레지스터처럼 **bring-up에 제일 쓸모 있는 정보**가 여기 있다. 양산 이미지에서는 꺼 두거나 마운트하지 않는 경우가 많다.

### 0.1 bring-up 진단 순서 한 장

"부팅은 되는데 `/dev/sensor0`이 안 생긴다"는 증상을 받으면 아래 순서로 좁힌다. 각 칸의 오른쪽이 **답이 있는 곳**이다. 한 칸이 OK여야 다음 칸이 의미가 있다.

```svg
<svg viewBox="0 0 680 610" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" text-anchor="middle" font-size="14">증상: 부팅은 되는데 /dev/sensor0 이 안 생긴다</text>
  <rect x="20" y="40" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="70" text-anchor="middle" font-size="13">① DT 노드가 있고 okay인가?</text>
  <rect x="360" y="40" width="300" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="70" text-anchor="middle" font-size="12">/proc/device-tree/.../status</text>
  <line x1="320" y1="65" x2="358" y2="65" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="90" x2="170" y2="114" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="180" y="107" font-size="12">OK</text>
  <rect x="20" y="116" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="146" text-anchor="middle" font-size="13">② 장치 객체가 만들어졌나?</text>
  <rect x="360" y="116" width="300" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="146" text-anchor="middle" font-size="12">/sys/bus/&lt;bus&gt;/devices/&lt;name&gt;</text>
  <line x1="320" y1="141" x2="358" y2="141" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="166" x2="170" y2="190" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="180" y="183" font-size="12">OK</text>
  <rect x="20" y="192" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="222" text-anchor="middle" font-size="13">③ 드라이버가 bind 됐나?</text>
  <rect x="360" y="192" width="300" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="222" text-anchor="middle" font-size="12">&lt;dev&gt;/driver 링크 · drivers/&lt;drv&gt;/</text>
  <line x1="320" y1="217" x2="358" y2="217" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="242" x2="170" y2="266" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="180" y="259" font-size="12">아니오</text>
  <rect x="20" y="268" width="300" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="298" text-anchor="middle" font-size="13">④ defer 중인가?</text>
  <rect x="360" y="268" width="300" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="298" text-anchor="middle" font-size="12">debugfs devices_deferred (이유 문자열)</text>
  <line x1="320" y1="293" x2="358" y2="293" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="318" x2="170" y2="342" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="180" y="335" font-size="12">아니오</text>
  <rect x="20" y="344" width="300" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="374" text-anchor="middle" font-size="13">⑤ probe가 에러를 냈나?</text>
  <rect x="360" y="344" width="300" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="374" text-anchor="middle" font-size="12">dmesg: probe ... failed with error -N</text>
  <line x1="320" y1="369" x2="358" y2="369" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="394" x2="170" y2="418" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="180" y="411" font-size="12">bind는 됐는데 동작 안 함</text>
  <rect x="20" y="420" width="300" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="450" text-anchor="middle" font-size="13">⑥ 자원이 살아 있나?</text>
  <rect x="360" y="420" width="300" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="440" text-anchor="middle" font-size="12">clk_summary · regulator_summary</text>
  <text x="510" y="459" text-anchor="middle" font-size="12">pinmux-pins · debugfs gpio · regmap</text>
  <line x1="320" y1="445" x2="358" y2="445" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="470" x2="170" y2="494" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="180" y="487" font-size="12">OK</text>
  <rect x="20" y="496" width="300" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="526" text-anchor="middle" font-size="13">⑦ 인터럽트가 들어오나?</text>
  <rect x="360" y="496" width="300" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="526" text-anchor="middle" font-size="12">/proc/interrupts 카운트 · smp_affinity</text>
  <line x1="320" y1="521" x2="358" y2="521" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="340" y="580" text-anchor="middle" font-size="13">전부 OK인데 이상하다 → 드라이버 내부로: M1 dyndbg · M5 ftrace · M7 bpftrace</text>
</svg>
```
그림 1 — bring-up 진단 순서. 왼쪽 질문이 "아니오"면 그 칸의 오른쪽 파일이 원인을 말해 준다. ③에서 bind가 안 됐으면 ④(defer)와 ⑤(실패)를 본다.

### 0.2 세 창구 지도

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
  <rect x="10" y="10" width="210" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="32" text-anchor="middle" font-size="14">/proc</text>
  <text x="115" y="52" text-anchor="middle" font-size="12">프로세스 · 커널 전역 통계</text>
  <rect x="235" y="10" width="210" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="32" text-anchor="middle" font-size="14">/sys (sysfs)</text>
  <text x="340" y="52" text-anchor="middle" font-size="12">디바이스 모델 그 자체</text>
  <rect x="460" y="10" width="210" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="32" text-anchor="middle" font-size="14">/sys/kernel/debug</text>
  <text x="565" y="52" text-anchor="middle" font-size="12">debugfs · 개발자용</text>
  <rect x="10" y="70" width="210" height="320" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="94" font-size="12">&lt;pid&gt;/stack · wchan</text>
  <text x="20" y="118" font-size="12">&lt;pid&gt;/status · maps</text>
  <text x="20" y="142" font-size="12">&lt;pid&gt;/smaps_rollup</text>
  <text x="20" y="166" font-size="12">interrupts · softirqs</text>
  <text x="20" y="190" font-size="12">irq/N/smp_affinity</text>
  <text x="20" y="214" font-size="12">iomem · ioports</text>
  <text x="20" y="238" font-size="12">meminfo · zoneinfo</text>
  <text x="20" y="262" font-size="12">slabinfo · buddyinfo</text>
  <text x="20" y="286" font-size="12">vmallocinfo · modules</text>
  <text x="20" y="310" font-size="12">device-tree (링크)</text>
  <text x="20" y="334" font-size="12">sys/ (sysctl 손잡이)</text>
  <text x="20" y="358" font-size="12">timer_list · locks</text>
  <rect x="235" y="70" width="210" height="320" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="245" y="94" font-size="12">bus/*/devices · drivers</text>
  <text x="245" y="118" font-size="12">&lt;dev&gt;/driver 링크</text>
  <text x="245" y="142" font-size="12">&lt;dev&gt;/of_node 링크</text>
  <text x="245" y="166" font-size="12">&lt;dev&gt;/power/runtime_*</text>
  <text x="245" y="190" font-size="12">class/devlink</text>
  <text x="245" y="214" font-size="12">kernel/irq/N/</text>
  <text x="245" y="238" font-size="12">module/*/parameters</text>
  <text x="245" y="262" font-size="12">module/*/taint</text>
  <text x="245" y="286" font-size="12">kernel/slab/&lt;cache&gt;/</text>
  <text x="245" y="310" font-size="12">firmware/devicetree/base</text>
  <text x="245" y="334" font-size="12">drivers/&lt;drv&gt;/bind · unbind</text>
  <rect x="460" y="70" width="210" height="320" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="470" y="94" font-size="12">devices_deferred</text>
  <text x="470" y="118" font-size="12">clk/clk_summary</text>
  <text x="470" y="142" font-size="12">regulator/regulator_summary</text>
  <text x="470" y="166" font-size="12">pinctrl/*/pins · pinmux-pins</text>
  <text x="470" y="190" font-size="12">gpio</text>
  <text x="470" y="214" font-size="12">regmap/&lt;dev&gt;/registers</text>
  <text x="470" y="238" font-size="12">memblock/memory · reserved</text>
  <text x="470" y="262" font-size="12">wakeup_sources</text>
  <text x="470" y="286" font-size="12">dmaengine/summary</text>
  <text x="470" y="310" font-size="12">dma_buf/bufinfo</text>
  <text x="470" y="334" font-size="12">dynamic_debug/control</text>
  <text x="470" y="358" font-size="12">tracing (→ tracefs)</text>
  <text x="115" y="416" text-anchor="middle" font-size="12">형식이 오래 유지됨</text>
  <text x="340" y="416" text-anchor="middle" font-size="12">안정 ABI (Documentation/ABI)</text>
  <text x="565" y="416" text-anchor="middle" font-size="12">ABI 아님 · 언제든 바뀜</text>
</svg>
```
그림 2 — 세 창구에서 bring-up 때 실제로 여는 파일들. debugfs 쪽이 HW 자원(clk·전원·핀·레지스터) 정보를 제일 많이 갖고 있다.

### 0.3 이 노트의 실습 환경

실습은 공용 랩의 **trace 커널**과 systemd 게스트에서 했다.

- 커널: `/var/lab/out/Image-trace` (`6.18.55-trace`, Image-sec 설정 + ftrace·PM_ADVANCED_DEBUG·gpio-sim 등). 게스트 안 `uname -r` = `6.18.55-trace`. VM 호스트 커널은 `7.0.0-38-generic`(실습 모듈은 호스트에 넣지 않는다).
- rootfs: `/var/lab/out/rootfs-trace.ext4`의 사본(Ubuntu 24.04 + systemd 255 + libgpiod 1.6.3, slabtop, setpriv 등).
- QEMU: `-machine virt -cpu cortex-a57,pmu=on -smp 2 -m 1G`, virtio-blk(PCI) 디스크, virtio-net(PCI). DT는 QEMU가 만들어 준다(같은 인자로 `dumpdtb`해서 비교에 썼다).
- 실습 모듈 세 개: `m2_defer`(없는 GPIO 칩을 기다리며 defer하는 소비자 + GPIO IRQ), `m2_regs`(RAM 버퍼 위의 regmap-mmio + runtime PM + 실패 주입), `m2_slab`(전용 slab 캐시·vmalloc·일부러 새는 kmalloc·잠드는 read).
- 실행 방식: 실험 스크립트를 이미지에 넣고 systemd oneshot 서비스로 부팅 후 자동 실행 → 결과 파일을 `debugfs -R cat`으로 꺼냈다.

> 주의: QEMU가 KVM 없이 TCG로 돌고, VM을 17명이 동시에 쓰는 중이라 부팅에 10분 가까이 걸렸다(dmesg 타임스탬프가 600초대인 이유). 이 노트의 타임스탬프·시간 값은 **실제 HW 수치가 아니다**. 카운트·주소·크기 같은 값만 의미 있게 읽자.

#### 예제 1 — 실습 모듈 빌드

무엇을 확인하나: 모듈이 trace 커널 빌드 트리에 맞춰 경고 없이 빌드되고 vermagic이 `6.18.55-trace`인지.

```sh
cd /var/tmp/M2/src      # m2_defer.c m2_regs.c m2_slab.c + Kbuild(obj-m := m2_slab.o m2_regs.o m2_defer.o)
make -C /var/lab/out/linux-build-trace M=$PWD -j2 modules 2>&1 | grep -iE "warn|error|LD"
modinfo -F vermagic m2_regs.ko
```

```text
make: Entering directory '/var/lab/build/linux-trace'
  LD [M]  m2_slab.ko
  LD [M]  m2_defer.ko
  LD [M]  m2_regs.ko
make: Leaving directory '/var/lab/build/linux-trace'
6.18.55-trace SMP preempt mod_unload aarch64
```

출력에서 볼 것: 경고 줄이 하나도 없다(처음엔 `MODULE_DESCRIPTION()` 누락 경고가 나서 추가했다). vermagic이 다른 커널에 넣으면 `insmod`가 `Invalid module format`으로 거부한다(E3).

---

## 1. 질문 ①~⑤ — "드라이버가 probe 됐나?"

### 1.1 직관 — 세 개의 링크만 보면 된다

bare-metal에서 "드라이버 init이 돌았나?"는 init 함수에 브레이크포인트를 걸면 끝이다. Linux에서는 driver core(G2)가 장치와 드라이버를 짝지어 주고, 그 결과를 sysfs **심볼릭 링크**로 남긴다.

| 보고 싶은 것 | 파일 | 읽는 법 |
|---|---|---|
| 장치 객체가 있나 | `/sys/bus/<bus>/devices/<name>` | 없으면 DT 노드가 disabled이거나 부모 버스 컨트롤러가 probe 실패 |
| bind 됐나 | `/sys/bus/<bus>/devices/<name>/driver` | 링크가 있으면 bind 완료. 가리키는 곳이 드라이버 |
| 이 드라이버가 잡은 장치들 | `/sys/bus/<bus>/drivers/<drv>/` | 장치 이름의 링크들 + `bind`·`unbind` 파일 |
| 어떤 DT 노드에서 왔나 | `.../<name>/of_node` | `/sys/firmware/devicetree/base/...`를 가리킴 (§2) |
| 대기 중인가 | `/sys/kernel/debug/devices_deferred` | 장치 이름 + `dev_err_probe()`가 남긴 이유 |
| 왜 실패했나 | `dmesg` | `probe with driver X failed with error -N` |

### 1.2 예제 2 — 소비자 드라이버 `m2_defer.c`

무엇을 확인하는 코드인가: 센서 드라이버가 리셋 핀과 data-ready(DRDY) 핀을 GPIO 확장기에서 받는 흔한 구조를 흉내 낸다. 확장기(여기서는 gpio-sim 칩 `m2_sim`)가 아직 없으면 `devm_gpiod_get()`이 `-EPROBE_DEFER`를 돌려주고, `dev_err_probe()`가 그 이유를 기록한다. DT 대신 `gpiod_lookup_table`(보드 파일식 매핑)을 써서 오버레이 없이 재현했다.

```c
// m2_defer.c — 없는 GPIO 칩(m2_sim)을 기다리며 defer 하는 소비자 + GPIO IRQ
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/interrupt.h>

static struct platform_device *pdev_m2;
static struct gpiod_lookup_table m2_lookup = {      /* DT 대신 보드 파일식 매핑 */
	.dev_id = "m2_cons",
	.table = {
		GPIO_LOOKUP("m2_sim", 3, "reset", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("m2_sim", 5, "drdy", GPIO_ACTIVE_HIGH),
		{ }
	},
};
static irqreturn_t m2_drdy(int irq, void *data) { return IRQ_HANDLED; }

static int m2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_desc *rst, *drdy;
	int irq;

	rst = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "reset gpio not ready\n");
	drdy = devm_gpiod_get(dev, "drdy", GPIOD_IN);
	if (IS_ERR(drdy))
		return dev_err_probe(dev, PTR_ERR(drdy), "drdy gpio\n");
	irq = gpiod_to_irq(drdy);
	if (irq < 0)
		return dev_err_probe(dev, irq, "drdy irq\n");
	/* gpio-sim은 can_sleep 칩 → threaded handler + ONESHOT */
	irq = devm_request_threaded_irq(dev, irq, NULL, m2_drdy,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "m2_drdy", NULL);
	if (irq)
		return dev_err_probe(dev, irq, "request irq\n");
	dev_info(dev, "probed: reset+drdy ready\n");
	return 0;
}
static struct platform_driver m2_drv = { .probe = m2_probe, .driver = { .name = "m2_cons" } };

static int __init m2_init(void)
{
	gpiod_add_lookup_table(&m2_lookup);
	platform_driver_register(&m2_drv);
	pdev_m2 = platform_device_register_simple("m2_cons", -1, NULL, 0);
	return PTR_ERR_OR_ZERO(pdev_m2);
}
static void __exit m2_exit(void)
{
	platform_device_unregister(pdev_m2);
	platform_driver_unregister(&m2_drv);
	gpiod_remove_lookup_table(&m2_lookup);
}
module_init(m2_init); module_exit(m2_exit);
MODULE_LICENSE("GPL"); MODULE_DESCRIPTION("M2 m2_defer demo");
```

출력에서 볼 것: 코드 자체는 평범한 probe다. 진단에 중요한 줄은 `dev_err_probe()` 세 개 — 에러가 `-EPROBE_DEFER`면 로그 대신 **이유 문자열을 저장**하고, 그 밖의 에러면 `dev_err`로 찍는다(G3 §9).

### 1.3 예제 3 — defer 전후를 sysfs·debugfs로 보기

무엇을 확인하나: 공급자가 없을 때 `devices_deferred`에 무엇이 보이는지, 공급자(gpio-sim 칩)를 configfs로 만든 뒤 자동 재시도로 bind되는지. dd.c의 dynamic debug를 켜서 driver core의 판단도 같이 본다(M1).

```sh
echo 'file drivers/base/dd.c +p' > /proc/dynamic_debug/control
cat /sys/kernel/debug/devices_deferred; echo "[end]"          # 로드 전
insmod /root/m2/m2_defer.ko; sleep 1
cat /sys/kernel/debug/devices_deferred; echo "[end]"
ls -l /sys/bus/platform/devices/m2_cons/ ; ls /sys/bus/platform/drivers/m2_cons/
dmesg | grep -E "m2_cons|m2_sim" | tail -8
# 공급자 등장: gpio-sim 칩을 configfs로 만든다 (라벨 m2_sim, 8라인)
C=/sys/kernel/config/gpio-sim/m2_chip
mkdir $C $C/bank0; echo 8 > $C/bank0/num_lines; echo m2_sim > $C/bank0/label
mkdir $C/bank0/line0; echo m2_led > $C/bank0/line0/name
echo 1 > $C/live; sleep 1; cat $C/dev_name $C/bank0/chip_name
cat /sys/kernel/debug/devices_deferred; echo "[end]"
ls -l /sys/bus/platform/devices/m2_cons/driver
dmesg | grep -E "m2_cons|m2_sim|gpio-sim" | tail -10
```

```text
[end]
m2_cons	m2_cons: reset gpio not ready
[end]
total 0
-rw-r--r-- 1 root root 4096 Oct 18 06:53 driver_override
-r--r--r-- 1 root root 4096 Oct 18 06:53 modalias
drwxr-xr-x 2 root root    0 Oct 18 06:53 power
lrwxrwxrwx 1 root root    0 Oct 18 06:53 subsystem -> ../../../bus/platform
-rw-r--r-- 1 root root 4096 Oct 18 06:53 uevent
bind
module
uevent
unbind
[  607.529889] platform m2_cons: bus: 'platform': __driver_probe_device: matched device with driver m2_cons
[  607.530090] platform m2_cons: bus: 'platform': really_probe: probing driver m2_cons with device
[  607.551709] m2_cons m2_cons: cannot find GPIO chip m2_sim, deferring
[  607.552088] m2_cons m2_cons: Driver m2_cons requests probe deferral
[  607.552350] platform m2_cons: Added to deferred list
gpio-sim.0
gpiochip1
[end]
lrwxrwxrwx 1 root root 0 Oct 18 06:54 /sys/bus/platform/devices/m2_cons/driver -> ../../../bus/platform/drivers/m2_cons
[  625.661482] platform gpio-sim.0: bus: 'platform': __driver_probe_device: matched device with driver gpio-sim
[  625.661654] platform gpio-sim.0: bus: 'platform': really_probe: probing driver gpio-sim with device
[  625.834774] platform m2_cons: Retrying from deferred list
[  625.975594] platform m2_cons: bus: 'platform': __driver_probe_device: matched device with driver m2_cons
[  625.975695] platform m2_cons: bus: 'platform': really_probe: probing driver m2_cons with device
[  625.981924] gpio-sim gpio-sim.0: driver: 'gpio-sim': driver_bound: bound to device
[  626.195712] gpio-sim gpio-sim.0: bus: 'platform': really_probe: bound device to driver gpio-sim
[  627.819644] m2_cons m2_cons: probed: reset+drdy ready
[  627.819741] m2_cons m2_cons: driver: 'm2_cons': driver_bound: bound to device
[  627.820058] m2_cons m2_cons: bus: 'platform': really_probe: bound device to driver m2_cons
```

출력에서 볼 것:

- insmod 직후 `devices_deferred`에 `m2_cons	m2_cons: reset gpio not ready`가 있다. 형식은 "장치 이름 TAB 이유". 이유 문자열은 우리가 `dev_err_probe()`에 준 메시지 그대로다 — **드라이버가 `dev_err_probe`를 안 쓰면 이유 칸이 비어 있다**. 그래서 리뷰에서 `return -EPROBE_DEFER;` 대신 `return dev_err_probe(...)`를 요구한다.
- 장치 디렉터리에 `driver` 링크가 **없다**. 드라이버 디렉터리에는 `bind`·`unbind`·`module`만 있고 장치 링크가 없다. 장치도 있고 드라이버도 있는데 짝이 안 된 상태 = defer.
- gpiolib이 `cannot find GPIO chip m2_sim, deferring`을 dev_warn으로 찍었다. 이건 gpiolib의 lookup 테이블 경로가 남기는 줄이고, driver core 자체는 dyndbg를 켜야만 `requests probe deferral`·`Added to deferred list`를 보여 준다. **즉 기본 설정에서는 defer가 거의 조용하다.**
- gpio-sim이 bind되자마자(`gpio-sim.0 ... probing`) `Retrying from deferred list`가 나온다. 아무 드라이버나 bind에 성공하면 driver core가 deferred 목록 전체를 다시 시도하기 때문이다(`driver_deferred_probe_trigger()`).
- 재시도 후 `devices_deferred`는 비고, `driver -> ../../../bus/platform/drivers/m2_cons` 링크가 생겼다.

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="150" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="32" text-anchor="middle" font-size="13">유저 (insmod)</text>
  <rect x="175" y="10" width="150" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="32" text-anchor="middle" font-size="13">driver core (dd.c)</text>
  <rect x="340" y="10" width="150" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="415" y="32" text-anchor="middle" font-size="13">gpiolib</text>
  <rect x="510" y="10" width="150" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="585" y="32" text-anchor="middle" font-size="13">gpio-sim (configfs)</text>
  <line x1="85" y1="44" x2="85" y2="440" stroke="#888" stroke-dasharray="4 4"/>
  <line x1="250" y1="44" x2="250" y2="440" stroke="#888" stroke-dasharray="4 4"/>
  <line x1="415" y1="44" x2="415" y2="440" stroke="#888" stroke-dasharray="4 4"/>
  <line x1="585" y1="44" x2="585" y2="440" stroke="#888" stroke-dasharray="4 4"/>
  <text x="90" y="66" font-size="12">insmod m2_defer.ko</text>
  <line x1="85" y1="74" x2="248" y2="74" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="258" y="100" font-size="12">really_probe → m2_probe()</text>
  <line x1="250" y1="108" x2="413" y2="108" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="420" y="104" font-size="12">devm_gpiod_get("reset")</text>
  <line x1="415" y1="140" x2="252" y2="140" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="420" y="144" font-size="12">-EPROBE_DEFER</text>
  <rect x="130" y="156" width="240" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="250" y="176" text-anchor="middle" font-size="12">deferred 목록 + 이유 문자열 저장</text>
  <text x="10" y="176" font-size="12">607.55 s</text>
  <text x="90" y="216" font-size="12">echo 1 &gt; live</text>
  <line x1="85" y1="224" x2="583" y2="224" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="260" y="254" font-size="12">gpio-sim bind 성공 → deferred trigger</text>
  <line x1="585" y1="262" x2="252" y2="262" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="10" y="262" font-size="12">625.83 s</text>
  <rect x="130" y="278" width="240" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="298" text-anchor="middle" font-size="12">Retrying from deferred list</text>
  <line x1="250" y1="330" x2="413" y2="330" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="420" y="334" font-size="12">gpiod_get 재시도 → 성공</text>
  <rect x="100" y="352" width="300" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="250" y="372" text-anchor="middle" font-size="12">driver_bound · driver 링크 생성 · 목록에서 제거</text>
  <text x="10" y="372" font-size="12">627.82 s</text>
  <text x="250" y="420" text-anchor="middle" font-size="12">시각은 TCG·과부하 VM의 dmesg 값 (실제 HW 아님)</text>
</svg>
```
그림 3 — 예제 3의 deferred probe 순서. 공급자가 생기면 "누군가 bind 성공" 이벤트가 목록 전체 재시도를 일으킨다.

### 1.4 `deferred_probe_timeout` — 영원히 기다리지 않는 장치

defer가 끝나지 않으면 어떻게 될까. 6.18 `drivers/base/dd.c`를 보면:

```c
#ifdef CONFIG_MODULES
static int driver_deferred_probe_timeout = 10;
#else
static int driver_deferred_probe_timeout;
#endif
...
static void deferred_probe_timeout_work_func(struct work_struct *work)
{
	...
	list_for_each_entry(p, &deferred_probe_pending_list, deferred_probe)
		dev_warn(p->device, "deferred probe pending: %s", p->deferred_probe_reason ?: "(reason unknown)\n");
	...
}
```

- 부팅 때 initcall이 다 끝난 뒤 이 타이머(기본 10초, 커널 인자 `deferred_probe_timeout=`로 변경)가 돈다. 만료되면 아직 대기 중인 장치마다 **`deferred probe pending: <이유>`** 경고를 한 번 찍는다. 부팅 로그에서 이 줄을 grep하는 것이 "부팅 후 안 붙은 장치" 목록을 얻는 가장 빠른 방법이다.
- 만료 후에는 `driver_deferred_probe_check_state()`를 쓰는 서브시스템(일부 pinctrl·IOMMU·power domain 경로)이 "선택적 공급자"를 포기하고 `-ETIMEDOUT`으로 진행한다(`deferred probe timeout, ignoring dependency`).
- 실습에서는 이 메시지를 재현하지 않았다. 타이머가 부팅 직후 한 번만 돌고, 우리 모듈은 그 뒤에 insmod했기 때문이다. 위 코드는 랩 소스 `/var/lab/src/linux-6.18.55/drivers/base/dd.c` 255–330행에서 확인한 것이다.

### 1.5 예제 4 — probe 실패는 에러 코드에 따라 다르게 찍힌다

무엇을 확인하나: `m2_regs`의 `fail` 파라미터로 probe가 `-EIO`(-5), `-ENODEV`(-19), `-ETIMEDOUT`(-110)을 돌려주게 하고, sysfs `bind`에 쓴 쪽과 dmesg에 무엇이 보이는지.

```sh
D=/sys/bus/platform/drivers/m2_regs; echo m2_regs > $D/unbind
for e in -5 -19 -110; do echo $e > /sys/module/m2_regs/parameters/fail; echo m2_regs > $D/bind; echo "bind($e) rc=$?"; done
dmesg | grep -E "m2_regs.*(failed|rejects)" | tail -3
echo 0 > /sys/module/m2_regs/parameters/fail; echo m2_regs > $D/bind; ls -l /sys/bus/platform/devices/m2_regs/driver
```

```text
/root/m2/m2_run2.sh: line 8: echo: write error: Input/output error
bind(-5) rc=1
/root/m2/m2_run2.sh: line 8: echo: write error: No such device
bind(-19) rc=1
/root/m2/m2_run2.sh: line 8: echo: write error: Connection timed out
bind(-110) rc=1
[  758.076775] m2_regs m2_regs: probe with driver m2_regs failed with error -5
[  758.102924] m2_regs m2_regs: probe with driver m2_regs rejects match -19
[  758.128143] m2_regs m2_regs: probe with driver m2_regs failed with error -110
lrwxrwxrwx 1 root root 0 Oct 18 06:56 /sys/bus/platform/devices/m2_regs/driver -> ../../../bus/platform/drivers/m2_regs
```

출력에서 볼 것:

- `bind`에 쓴 `echo`가 probe의 에러 코드를 그대로 errno로 받는다(`Input/output error`, `No such device`, `Connection timed out`). **수동 bind는 probe를 디버깅하는 가장 빠른 루프**다 — 모듈을 내렸다 올릴 필요가 없다.
- `-5`와 `-110`은 `dev_err`로 `failed with error`가 찍힌다. **`-19`(ENODEV)와 `-6`(ENXIO)은 "이 장치는 내 것이 아니다"라는 뜻으로 취급돼 `dev_dbg`(`rejects match`)로만 찍힌다** — 여기서는 dd.c dyndbg를 켜 뒀기 때문에 보였다. 기본 설정이면 dmesg에 아무것도 없고 장치는 조용히 unbound로 남는다. 칩 ID가 안 맞을 때 `-ENODEV`를 돌려주는 드라이버가 많으니, "로그 없이 안 붙는다"면 이 경우를 의심한다.
- `fail=0`으로 되돌리고 bind하면 정상 bind된다. 설정 파라미터가 `0644`라서 런타임에 바꿀 수 있었다(§7).

| probe 반환값 | driver core 동작 | 기본 로그 |
|---|---|---|
| 0 | bind, `driver` 링크 생성 | 없음 (드라이버가 찍는 것만) |
| `-EPROBE_DEFER` (-517) | deferred 목록, 나중에 재시도 | 없음 (`devices_deferred`에 이유) |
| `-ENODEV` (-19) · `-ENXIO` (-6) | 매치 거부, 다음 드라이버 시도 | 없음 (dbg만) |
| 그 밖의 음수 | 실패, unbound로 남음 | `probe with driver X failed with error -N` |

> 참고: `-517`이라는 숫자가 dmesg에 직접 보이는 경우는 `initcall_debug`나 일부 서브시스템이 반환값을 그대로 찍을 때다. 예전 커널(5.x 초반 이전)에서는 `dev_err(..., "... %d", ret)`로 -517을 찍는 드라이버가 흔했고, 그래서 "error -517"이 검색어로 유명하다. 지금은 `dev_err_probe()` 덕에 대부분 사라졌다.

---

## 2. 질문 ① — "어떤 DT 노드가 쓰였나?"

### 2.1 세 이름은 같은 트리다

- `/proc/device-tree` → `/sys/firmware/devicetree/base`로 가는 심볼릭 링크다. 커널이 **실제로 받아서 해석한** DT(부트로더가 고친 것, 오버레이 적용 후)를 보여 준다. 빌드한 `.dts`가 아니라 이것이 진실이다(D1, D6).
- 각 장치의 `of_node` 링크가 그 트리의 한 노드를 가리킨다. "이 장치는 어느 노드에서 만들어졌나"의 답.
- 속성 파일은 바이너리다. 문자열 목록은 NUL로 구분되고, 숫자는 big-endian 32비트 셀이다. `tr '\0' ' '`와 `od -An -tx1`로 읽는다.

### 2.2 예제 5 — PL011 UART 장치에서 DT 노드까지

무엇을 확인하나: amba 버스의 `9000000.pl011` 장치가 어느 노드에서 왔고 어떤 드라이버가 잡았는지, `reg` 셀이 `/proc/iomem`(§4)과 맞는지.

```sh
cat /proc/device-tree/compatible | tr '\0' ' '; echo
ls -l /sys/bus/amba/devices/9000000.pl011/of_node /sys/bus/amba/devices/9000000.pl011/driver
ls /sys/bus/amba/devices/9000000.pl011/of_node/
cat /sys/bus/amba/devices/9000000.pl011/of_node/compatible | tr "\0" " "; echo
od -An -tx1 /sys/bus/amba/devices/9000000.pl011/of_node/reg
cat /sys/kernel/debug/devices_deferred; echo "[end]"
```

```text
linux,dummy-virt 
lrwxrwxrwx 1 root root 0 Oct 18 06:56 /sys/bus/amba/devices/9000000.pl011/driver -> ../../../bus/amba/drivers/uart-pl011
lrwxrwxrwx 1 root root 0 Oct 18 06:56 /sys/bus/amba/devices/9000000.pl011/of_node -> ../../../firmware/devicetree/base/pl011@9000000
clock-names
clocks
compatible
interrupts
name
reg
arm,pl011 arm,primecell 
 00 00 00 00 09 00 00 00 00 00 00 00 00 00 10 00
[end]
```

출력에서 볼 것:

- 루트 `compatible`이 `linux,dummy-virt`. `of_node`는 `../../../firmware/devicetree/base/pl011@9000000`을, `driver`는 `amba/drivers/uart-pl011`을 가리킨다. 처음에 `/sys/bus/platform/devices/9000000.pl011`을 찾다가 "No such file"을 봤다 — PL011은 **amba 버스** 장치라서다. "장치가 없다"고 결론 내리기 전에 `ls /sys/bus/`의 다른 버스도 본다.
- `reg`의 16바이트 = `<0x0 0x09000000 0x0 0x1000>` (address-cells=2, size-cells=2). §4의 `/proc/iomem` 줄 `09000000-09000fff : 9000000.pl011`과 정확히 맞는다.
- `devices_deferred`가 비어 있다(`[end]`만). 부팅 후 안 붙은 장치가 없다는 뜻이다.

함정: `status = "disabled"`인 노드는 `/proc/device-tree`에는 **있지만** 장치 객체가 만들어지지 않는다. 노드가 보인다고 장치가 있는 게 아니다 — 그림 1의 ①과 ②는 다른 질문이다. `cat .../status`가 없으면 기본값 okay다.

---

## 3. 질문 ⑦ — "인터럽트가 들어오나?"

### 3.1 숫자 세 개가 같은 인터럽트를 가리킨다

SSD 컨트롤러에서는 인터럽트 번호가 하나였다 — 벡터 테이블의 인덱스. Linux에는 번호가 세 겹이다(H6).

- **hwirq**: 인터럽트 컨트롤러 안의 번호. GIC라면 SPI 1 = hwirq 33(32 + 1). gpio-sim이라면 라인 번호.
- **irq_domain**: 컨트롤러 하나마다 하나. hwirq → Linux 번호 변환표.
- **virq (Linux IRQ 번호)**: `request_irq()`에 넘기는 번호, `/proc/interrupts` 맨 앞 칸, `/proc/irq/N/`의 N. 부팅마다 바뀔 수 있다 — **보드 문서에 virq를 적지 말 것.**

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="105" y="20" text-anchor="middle" font-size="13">HW 신호</text>
  <text x="340" y="20" text-anchor="middle" font-size="13">irq_domain (hwirq)</text>
  <text x="590" y="20" text-anchor="middle" font-size="13">Linux virq</text>
  <rect x="10" y="34" width="190" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="59" text-anchor="middle" font-size="12">PL011 UART (DT: SPI 1)</text>
  <rect x="240" y="34" width="200" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="59" text-anchor="middle" font-size="12">GIC-0 · hwirq 33</text>
  <rect x="540" y="34" width="100" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="590" y="59" text-anchor="middle" font-size="12">13</text>
  <line x1="200" y1="54" x2="238" y2="54" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="440" y1="54" x2="538" y2="54" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="90" width="190" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="115" text-anchor="middle" font-size="12">virtio-net MSI-X 벡터 1</text>
  <rect x="240" y="90" width="200" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="115" text-anchor="middle" font-size="12">GICv2m-PCI-MSIX · hwirq 1</text>
  <rect x="540" y="90" width="100" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="590" y="115" text-anchor="middle" font-size="12">21</text>
  <line x1="200" y1="110" x2="238" y2="110" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="440" y1="110" x2="538" y2="110" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="146" width="190" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="171" text-anchor="middle" font-size="12">gpio-sim 라인 5 (DRDY)</text>
  <rect x="240" y="146" width="200" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="171" text-anchor="middle" font-size="12">irq_sim · hwirq 5</text>
  <rect x="540" y="146" width="100" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="590" y="171" text-anchor="middle" font-size="12">26</text>
  <line x1="200" y1="166" x2="238" y2="166" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="440" y1="166" x2="538" y2="166" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="210" width="660" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="232" text-anchor="middle" font-size="12">/proc/interrupts 한 행: virq · CPU별 카운트 · chip 이름 · hwirq · 트리거 · action 이름</text>
  <rect x="10" y="252" width="660" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="274" text-anchor="middle" font-size="12">/proc/irq/N/: smp_affinity(_list) · effective_affinity(_list) · spurious · action 이름 디렉터리</text>
  <rect x="10" y="294" width="660" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="316" text-anchor="middle" font-size="12">/sys/kernel/irq/N/: chip_name · hwirq · type · actions · per_cpu_count · wakeup</text>
</svg>
```
그림 4 — 실습 게스트에서 실제로 본 세 인터럽트의 번호 변환. 오른쪽 virq가 모든 진단 파일의 키다.

### 3.2 예제 6 — `/proc/interrupts` 한 장 읽기

무엇을 확인하나: 2 CPU 게스트의 인터럽트 표 전체. 어떤 열이 무엇인지 확인한다.

```sh
cat /proc/interrupts
```

```text
           CPU0       CPU1       
 11:      63874      63903 GIC-0  27 Level     arch_timer
 13:          0          0 GIC-0  33 Level     uart-pl011
 17:          0          0 GICv2m-PCI-MSIX-0000:00:02.0   0 Edge      virtio1-config
 18:       1047          0 GICv2m-PCI-MSIX-0000:00:02.0   1 Edge      virtio1-req.0
 19:          0       1097 GICv2m-PCI-MSIX-0000:00:02.0   2 Edge      virtio1-req.1
 20:          0          0 GICv2m-PCI-MSIX-0000:00:01.0   0 Edge      virtio0-config
 21:          0          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
 22:          0          0 GICv2m-PCI-MSIX-0000:00:01.0   2 Edge      virtio0-output.0
 23:          0          0 GIC-0  34 Level     rtc-pl031
 24:          0          0 GIC-0  23 Level     arm-pmu
 25:          0          0 9030000.pl061   3 Edge      GPIO Key Poweroff
 26:          3          0  irq_sim   5 Edge      m2_drdy
IPI0:      4658       4226       Rescheduling interrupts
IPI1:      2308       2204       Function call interrupts
IPI2:         0          0       CPU stop interrupts
IPI3:         0          0       CPU stop NMIs
IPI4:         0          0       Timer broadcast interrupts
IPI5:         3          0       IRQ work interrupts
IPI6:         0          0       CPU backtrace interrupts
IPI7:         0          0       KGDB roundup interrupts
Err:          0
```

출력에서 볼 것 (열 순서: virq, CPU0 카운트, CPU1 카운트, irq chip/domain 이름, hwirq, 트리거, action 이름):

- `11: arch_timer` (GIC PPI 27) — 양쪽 CPU에서 거의 같이 증가한다. 각 CPU의 로컬 타이머라 per-CPU 인터럽트다. 이 수가 **멈춰 있다면** 그 CPU의 tick이 죽은 것이다(NO_HZ idle이면 느리게 는다 — N 모듈).
- `13: uart-pl011` 카운트 0 — 콘솔이 polling/FIFO로 쓰는 경우가 많고, 이 게스트는 출력을 systemd 서비스 파일로 돌렸다. "UART IRQ가 0이면 고장"이라고 단정하지 말 것.
- `18/19: virtio1-req.0/1` — virtio-blk의 큐 두 개가 CPU0·CPU1에 **하나씩 고정**돼 있다(managed affinity, §3.4).
- `25: 9030000.pl061 3 Edge GPIO Key Poweroff` — **GPIO 컨트롤러가 다시 인터럽트 컨트롤러**가 된 경우. chip 칸에 GIC 대신 GPIO 장치 이름이 나온다.
- `26: irq_sim 5 Edge m2_drdy` — 우리 DRDY 핀.
- `IPI0..7` — CPU 간 인터럽트(Rescheduling, Function call 등). 숫자가 비정상적으로 크면 스케줄러가 CPU 사이에서 일을 계속 옮기고 있다는 신호(F1). `Err:`는 컨트롤러가 보고한 가짜/잘못된 인터럽트 수.

### 3.3 예제 7 — 내 IRQ가 실제로 오는지: before/after

무엇을 확인하나: DRDY 라인(gpio-sim 라인 5)의 입력을 상승 에지 3번 만들고, 카운트가 3 증가하는지. `/sys/kernel/irq/N/`의 상세 정보.

```sh
grep -E "CPU0|m2_drdy" /proc/interrupts
P=/sys/devices/platform/gpio-sim.0/gpiochip1/sim_gpio5
for i in 1 2 3; do echo pull-up > $P/pull; echo pull-down > $P/pull; done
grep m2_drdy /proc/interrupts
IRQ=26
for f in chip_name hwirq name type actions per_cpu_count wakeup; do echo "$f: $(cat /sys/kernel/irq/$IRQ/$f)"; done
```

```text
           CPU0       CPU1       
 26:          0          0  irq_sim   5 Edge      m2_drdy
 26:          3          0  irq_sim   5 Edge      m2_drdy
chip_name: irq_sim
hwirq: 5
name: 
type: edge
actions: m2_drdy
per_cpu_count: 3,0
wakeup: disabled
```

출력에서 볼 것: 0 → 3. 상승 에지만 세도록(`IRQF_TRIGGER_RISING`) 요청했으니 pull-up 세 번 = 3회다. `per_cpu_count: 3,0`은 전부 CPU0에서 처리됐다는 뜻. `type: edge`는 실제로 컨트롤러에 설정된 트리거다 — DT의 `interrupts` 플래그와 드라이버의 `IRQF_TRIGGER_*`가 어긋나면 여기서 드러난다.

bring-up에서는 이 패턴을 그대로 쓴다: **센서를 한 번 흔들고(혹은 DRDY를 scope로 확인하고) 카운트가 움직이는지 본다.** 안 움직이면 ⑥ 자원(핀먹스·트리거·컨트롤러) 쪽, 미친 듯이 오르면 레벨 인터럽트를 안 지우는 핸들러(인터럽트 폭풍)다.

### 3.4 예제 8 — affinity 바꾸기와 "managed" IRQ의 거부

무엇을 확인하나: virtio IRQ들의 현재 affinity, 그리고 블록 큐 IRQ(18)와 네트워크 RX IRQ(21)를 CPU1로 옮겨 보기. 네트워크는 ping 20번으로 트래픽을 만든다.

```sh
for n in 17 18 19 20 21 22; do echo "irq $n $(grep -E "^ *$n:" /proc/interrupts | awk '{print $NF}') aff=$(cat /proc/irq/$n/smp_affinity) list=$(cat /proc/irq/$n/smp_affinity_list) eff=$(cat /proc/irq/$n/effective_affinity_list)"; done
ls /proc/irq/26/; cat /proc/irq/default_smp_affinity
BLK=18; NET=21; echo "BLK=$BLK NET=$NET"     # 실제 스크립트는 grep으로 번호를 뽑았다
echo 1 > /proc/irq/18/smp_affinity_list; echo "blk set rc=$?"
grep -E "^ *21:" /proc/interrupts; echo 1 > /proc/irq/21/smp_affinity_list; echo "net set rc=$?"
cat /proc/irq/21/effective_affinity_list
ip -br a; ping -c 20 -i 0.2 -q 10.0.2.2 | tail -2; grep -E "^ *21:" /proc/interrupts
cat /proc/softirqs; cat /proc/irq/21/spurious
```

```text
irq 17 virtio1-config aff=3 list=0-1 eff=0
irq 18 virtio1-req.0 aff=1 list=0 eff=0
irq 19 virtio1-req.1 aff=2 list=1 eff=1
irq 20 virtio0-config aff=3 list=0-1 eff=0
irq 21 virtio0-input.0 aff=3 list=0-1 eff=0
irq 22 virtio0-output.0 aff=3 list=0-1 eff=0
affinity_hint
effective_affinity
effective_affinity_list
m2_drdy
node
smp_affinity
smp_affinity_list
spurious
3
BLK=18 NET=21
/root/m2/m2_run.sh: line 39: echo: write error: Operation not permitted
blk set rc=1
 21:          0          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
net set rc=0
1
lo               UNKNOWN        127.0.0.1/8 
enp0s1           UP             10.0.2.15/24 metric 1024 
20 packets transmitted, 20 received, 0% packet loss, time 3918ms
rtt min/avg/max/mdev = 0.475/2.208/6.037/1.806 ms
 21:          0         27 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
                    CPU0       CPU1       
          HI:          0          0
       TIMER:      19134      17446
      NET_TX:          1          2
      NET_RX:          7         31
       BLOCK:          0          0
    IRQ_POLL:          0          0
     TASKLET:          1          0
       SCHED:      15800      16096
     HRTIMER:          0          0
         RCU:      19112      18833
count 0
unhandled 0
last_unhandled 0 ms
```

출력에서 볼 것:

- `smp_affinity`는 CPU 비트마스크(16진), `_list`는 사람이 읽는 목록. **`effective_affinity`가 실제 배달 대상**이다. GIC는 SPI 하나를 한 CPU에만 보내므로 허용 목록이 `0-1`이어도 effective는 `0` 하나다. "affinity를 0-1로 줬는데 왜 CPU0만 받냐"의 답.
- IRQ 18(virtio-blk 큐)은 `write error: Operation not permitted`(앞의 `line 39`는 스크립트 파일로 실행해서 bash가 붙인 접두어). 블록 multiqueue 드라이버는 큐마다 CPU를 고정하는 **managed IRQ**(`IRQD_AFFINITY_MANAGED`)를 쓰고, 커널이 유저의 변경을 거부한다. NVMe도 똑같다 — SSD 시절 "큐 하나 = 코어 하나" 설계가 Linux에서는 이렇게 보인다.
- IRQ 21(virtio-net RX)은 변경 성공 → ping 20번 뒤 카운트가 **CPU1 칸에만** 27 늘었다. `NET_RX` softirq도 CPU1 쪽이 31로 많다 — 하드 IRQ를 받은 CPU에서 softirq(NAPI)가 돌기 때문이다(F7).
- `spurious`의 `unhandled 0` — 핸들러가 전부 `IRQ_NONE`을 돌려주면 이 수가 오르고, 10만 번 중 9만 9900번 이상이면 커널이 `irq N: nobody cared`를 찍고 그 IRQ를 끈다. 공유 IRQ 핸들러에서 자기 장치인지 확인 안 하고 `IRQ_HANDLED`/`IRQ_NONE`을 잘못 돌려줄 때 보는 파일이다.
- `/proc/irq/26/` 안의 `m2_drdy` 디렉터리는 action 이름이다. 실습 커널에는 `CONFIG_GENERIC_IRQ_DEBUGFS`가 없어서 `/sys/kernel/debug/irq/`(irq_domain·상태 비트 덤프)는 확인하지 못했다. 그 옵션이 켜진 커널이면 `/sys/kernel/debug/irq/irqs/N`에서 domain·hwirq·상태 플래그(`IRQD_*`)까지 보인다.

---

## 4. 질문 ⑥의 전제 — "주소 맵이 맞나?" (`/proc/iomem`, `/proc/ioports`, memblock)

### 4.1 직관

`/proc/iomem`은 커널의 `struct resource` 트리(`iomem_resource`)를 그대로 출력한다. 들여쓰기가 트리 깊이다. **바깥 줄 = DT/버스가 선언한 영역, 안쪽 줄 = 드라이버가 `request_mem_region()`(`devm_ioremap_resource`가 내부에서 호출)으로 실제 점유한 영역.** 그래서 안쪽 줄이 없으면 "장치는 있는데 드라이버가 레지스터를 잡지 않았다"는 뜻이다. 두 드라이버가 같은 영역을 잡으려 하면 두 번째가 `-EBUSY`로 probe에 실패하고, 그 충돌도 여기서 보인다.

### 4.2 예제 9 — root vs 일반 사용자

무엇을 확인하나: root로 본 iomem, `nobody`(uid 65534)로 본 iomem, ioports.

```sh
cat /proc/iomem
setpriv --reuid=65534 --regid=65534 --clear-groups cat /proc/iomem | head -12
cat /proc/ioports
```

```text
00000000-03ffffff : 0.flash flash@0
04000000-07ffffff : 0.flash flash@0
09000000-09000fff : pl011@9000000
  09000000-09000fff : 9000000.pl011 pl011@9000000
09010000-09010fff : pl031@9010000
  09010000-09010fff : rtc-pl031
09030000-09030fff : pl061@9030000
  09030000-09030fff : 9030000.pl061 pl061@9030000
10000000-3efeffff : pcie@10000000
  10000000-1007ffff : 0000:00:01.0
  10080000-10080fff : 0000:00:01.0
  10081000-10081fff : 0000:00:02.0
40000000-7fffffff : System RAM
  40200000-4356ffff : Kernel code
  43570000-4398ffff : reserved
  43990000-4419ffff : Kernel data
  48000000-480fffff : reserved
  7cb00000-7fbfffff : reserved
  7fc29000-7fc2bfff : reserved
  7fc2c000-7fdacfff : reserved
  7fdad000-7fdf2fff : reserved
  7fdf5000-7fdf5fff : reserved
  7fdf6000-7fdf8fff : reserved
  7fdf9000-7fe0bfff : reserved
  7fe0c000-7fe16fff : reserved
  7fe17000-7fffffff : reserved
4010000000-401fffffff : PCI ECAM
8000000000-ffffffffff : pcie@10000000
  8000000000-8000003fff : 0000:00:01.0
    8000000000-8000003fff : virtio-pci-modern
  8000004000-8000007fff : 0000:00:02.0
    8000004000-8000007fff : virtio-pci-modern
--- (nobody) ---
00000000-00000000 : 0.flash flash@0
00000000-00000000 : 0.flash flash@0
00000000-00000000 : pl011@9000000
  00000000-00000000 : 9000000.pl011 pl011@9000000
00000000-00000000 : pl031@9010000
  00000000-00000000 : rtc-pl031
00000000-00000000 : pl061@9030000
  00000000-00000000 : 9030000.pl061 pl061@9030000
00000000-00000000 : pcie@10000000
  00000000-00000000 : 0000:00:01.0
  00000000-00000000 : 0000:00:01.0
  00000000-00000000 : 0000:00:02.0
--- ioports ---
00000000-0000ffff : pcie@10000000
  00001000-0000107f : 0000:00:02.0
  00001080-0000109f : 0000:00:01.0
```

출력에서 볼 것:

- `09000000-09000fff : pl011@9000000` 바깥 줄 아래 `9000000.pl011` 안쪽 줄 = PL011 드라이버가 4 KiB를 점유했다. 예제 5의 `reg`와 일치. `pl031`(RTC), `pl061`(GPIO)도 같은 모양.
- `10000000-3efeffff : pcie@10000000`, `4010000000-401fffffff : PCI ECAM`, `8000000000-ffffffffff`(64비트 prefetch 창) — QEMU가 만든 DT의 PCIe `ranges`·`reg`와 같다(`ranges = <... 0x2000000 0x00 0x10000000 ...>`, `reg = <0x40 0x10000000 ...>`, `dumpdtb`로 확인). 그 안에 virtio-pci BAR가 `virtio-pci-modern`으로 잡혀 있다.
- `40000000-7fffffff : System RAM` 안에 `Kernel code`, `Kernel data`, 여러 `reserved`가 보인다. 이 reserved 합이 §5의 "MemTotal이 RAM보다 작은 이유"다. `7cb00000-7fbfffff`(48 MiB)는 CMA·DMA 풀·page 구조체 등 부팅 초기 memblock 예약이다.
- **일반 사용자에게는 주소가 전부 0**이다. 이름과 트리 모양은 보이지만 숫자는 가린다(`kernel/resource.c`의 `r_show()`가 `CAP_SYS_ADMIN`이 없으면 start/end를 0으로 찍는다). 버그 리포트에 "iomem이 다 0이에요"가 붙어 오면 sudo 없이 본 것이다.
- `/proc/ioports`는 x86의 I/O 포트 공간용이지만 arm64에서는 PCIe의 I/O 창(`0x3eff0000`에 매핑된 64 KiB)을 포트 번호로 보여 준다. virtio-pci 장치의 레거시 I/O BAR가 여기 있다.

### 4.3 예제 10 — memblock: 커널이 부팅 초기에 알던 메모리

무엇을 확인하나: `ARCH_KEEP_MEMBLOCK`가 켜진 arm64 커널에서 debugfs `memblock/memory`·`reserved`. DT에 `reserved-memory` 노드가 있는지도 본다.

```sh
cat /sys/kernel/debug/memblock/memory; cat /sys/kernel/debug/memblock/reserved | head -20
ls /proc/device-tree/reserved-memory
```

```text
   0: 0x0000000040000000..0x000000007fffffff    0 NONE
   0: 0x0000000040200000..0x000000004356ffff    0 NONE
   1: 0x0000000043990000..0x000000004419ffff    0 NONE
   2: 0x0000000048000000..0x00000000480fffff    0 NONE
   3: 0x000000007cb00000..0x000000007fbfffff    0 RSV_KERN
   4: 0x000000007fc29000..0x000000007fc2bfff    0 RSV_KERN
   5: 0x000000007fc2cbc0..0x000000007fdacfc7    0 RSV_KERN
   6: 0x000000007fdad000..0x000000007fdf2fff    0 RSV_KERN
   7: 0x000000007fdf5200..0x000000007fdf521f    0 RSV_KERN
   8: 0x000000007fdf5240..0x000000007fdf5807    0 RSV_KERN
   9: 0x000000007fdf5840..0x000000007fdf589f    0 RSV_KERN
  10: 0x000000007fdf58c0..0x000000007fdf5a47    0 RSV_KERN
...
ls: cannot access '/proc/device-tree/reserved-memory': No such file or directory
```

(memory는 첫 한 줄, 나머지는 reserved — 20줄 중 앞 11줄만 남겼다.)

출력에서 볼 것: `memory`는 `0x40000000..0x7fffffff` 한 줄(DT `memory@40000000`의 `reg`). `reserved`의 첫 세 줄이 iomem의 `Kernel code`·`Kernel data`·`48000000-480fffff`(DT blob을 복사해 둔 영역)와 같은 범위다. QEMU virt DT에는 `reserved-memory` 노드가 없어서 `ls`가 실패한다. 실제 SoC(i.MX8, AM62, Jetson)에서는 여기에 코프로세서 펌웨어·DSP·카메라용 carve-out 노드가 여럿 있고, 각각 iomem에 `reserved`로, 드라이버가 `memory-region`으로 참조하면 `of_reserved_mem` 로그로 나타난다(F10 참조 — 이 부분은 랩에서 재현 안 함).

---

## 5. "메모리가 어디 갔나?" — meminfo · slabinfo · buddyinfo · vmallocinfo · zoneinfo

### 5.1 `/proc/meminfo` 필드의 정확한 뜻

| 필드 | 뜻 | bring-up에서 보는 이유 |
|---|---|---|
| `MemTotal` | 버디 할당자가 관리하는 페이지 총합(= zoneinfo `managed` 합) | **물리 RAM이 아니다.** 커널 이미지·memblock 예약·carve-out을 뺀 값. DT `memory` 크기가 맞는지 확인할 때 차이를 계산한다 |
| `MemFree` | 아무도 안 쓰는 페이지 | 낮다고 문제가 아니다. 캐시가 차지하는 게 정상 |
| `MemAvailable` | 회수 가능한 캐시까지 포함한 "새로 쓸 수 있는 양" 추정 | 앱 입장의 여유 메모리. OOM 위험은 이걸로 본다 |
| `Buffers` · `Cached` | 블록 장치 메타데이터 · 파일 페이지 캐시 | 압박 시 회수됨 |
| `Slab` = `SReclaimable` + `SUnreclaim` | slab 할당자(kmalloc·kmem_cache) 사용량 | **드라이버 누수는 거의 SUnreclaim으로 나타난다** |
| `KernelStack` | 커널 스레드·태스크 스택 (arm64는 16 KiB씩) | 스레드 폭증 감지 |
| `PageTables` | 페이지 테이블 | 큰 mmap·많은 프로세스 |
| `VmallocUsed` | vmalloc 영역에 매핑된 페이지 | 큰 버퍼를 vmalloc하는 드라이버·모듈 코드 |
| `Percpu` | per-CPU 할당 | CPU 수 × 구조체 |
| `CmaTotal` · `CmaFree` | CMA 영역 크기와 그중 비어 있는 양 | 카메라·디스플레이 DMA 버퍼 할당 실패 디버깅(F10) |

`alloc_pages()`로 직접 페이지를 가져가는 드라이버(DMA 링, 일부 네트워크 드라이버)는 **어느 필드에도 따로 안 잡힌다** — MemFree가 줄어들 뿐이다. 이런 사용은 `/proc/pagetypeinfo`나 `page_owner`(M8)로 추적한다.

### 5.2 예제 11 — 실험 모듈 `m2_slab.c`

무엇을 확인하는 코드인가: 192바이트 객체 2만 개를 **전용 slab 캐시** `m2_obj`에서 할당하고(SLUB가 다른 캐시와 합치지 않게 `SLAB_NO_MERGE`), 포인터 배열은 vmalloc으로 잡는다. debugfs `m2/leak`에 쓰면 해제하지 않는 `kmalloc(512)`를 1000개 만들고, `m2/block`을 읽으면 누군가 쓸 때까지 잠든다(§8).

```c
// m2_slab.c — 전용 slab 캐시 + vmalloc + 누수 + 잠드는 read (진단 실험용)
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <linux/wait.h>
static int count = 20000;
module_param(count, int, 0444);
static struct kmem_cache *cache;
static void **objs;
static struct dentry *dir;
static DECLARE_WAIT_QUEUE_HEAD(m2_wq);
static bool m2_go;
static ssize_t blk_read(struct file *f, char __user *u, size_t n, loff_t *p)
{	/* 누가 write 할 때까지 잠든다 → /proc/<pid>/stack 관찰용 */
	if (wait_event_interruptible(m2_wq, READ_ONCE(m2_go)))
		return -ERESTARTSYS;
	return 0;
}
static ssize_t blk_write(struct file *f, const char __user *u, size_t n, loff_t *p)
{
	WRITE_ONCE(m2_go, true);
	wake_up_interruptible(&m2_wq);
	return n;
}
static ssize_t leak_write(struct file *f, const char __user *u, size_t n, loff_t *p)
{	/* 일부러 해제하지 않는 kmalloc(512) 1000개 — 누수 흉내 */
	for (int i = 0; i < 1000; i++)
		if (!kmalloc(512, GFP_KERNEL))
			return -ENOMEM;
	return n;
}
static const struct file_operations blk_fops = { .read = blk_read, .write = blk_write };
static const struct file_operations leak_fops = { .write = leak_write };
static void m2_free_all(void)
{
	for (int i = 0; objs && i < count && objs[i]; i++)
		kmem_cache_free(cache, objs[i]);
	vfree(objs);
	kmem_cache_destroy(cache);
}
static int __init m2_init(void)
{
	cache = kmem_cache_create("m2_obj", 192, 0, SLAB_NO_MERGE, NULL);
	objs = vzalloc(array_size(count, sizeof(void *)));   /* vmallocinfo에 보임 */
	for (int i = 0; cache && objs && i < count; i++)
		objs[i] = kmem_cache_alloc(cache, GFP_KERNEL);
	if (!cache || !objs || !objs[count - 1]) {
		m2_free_all();
		return -ENOMEM;
	}
	dir = debugfs_create_dir("m2", NULL);
	debugfs_create_file("block", 0600, dir, NULL, &blk_fops);
	debugfs_create_file("leak", 0200, dir, NULL, &leak_fops);
	pr_info("m2_slab: %d objs x 192B in m2_obj\n", count);
	return 0;
}
static void __exit m2_exit(void) { debugfs_remove_recursive(dir); m2_free_all(); }
module_init(m2_init); module_exit(m2_exit);
MODULE_LICENSE("GPL"); MODULE_DESCRIPTION("M2 m2_slab demo");
```

출력에서 볼 것: `SLAB_NO_MERGE`가 없으면 SLUB가 같은 크기·플래그의 기존 캐시(예: 192바이트 kmalloc 캐시)와 **합쳐 버려서** `/proc/slabinfo`에 `m2_obj` 이름이 안 나올 수 있다. 진단용으로 이름 붙은 캐시를 원하면 이 플래그(6.5+)를 쓰거나 부팅 인자 `slab_nomerge`를 준다.

### 5.3 예제 12 — 로드 전후 메모리 숫자

무엇을 확인하나: `insmod m2_slab.ko count=20000` 전후의 meminfo·slabinfo·buddyinfo, 그리고 `/sys/kernel/slab/m2_obj/`의 캐시 정보.

```sh
grep -E "^(MemTotal|MemFree|MemAvailable|Buffers|Cached|Slab|SReclaimable|SUnreclaim|KernelStack|PageTables|VmallocUsed|Percpu|CmaTotal|CmaFree)" /proc/meminfo
head -2 /proc/slabinfo; grep -E "^kmalloc-512 " /proc/slabinfo; cat /proc/buddyinfo
insmod /root/m2/m2_slab.ko count=20000; sleep 1
grep -E "^(MemFree|Slab|SUnreclaim|VmallocUsed)" /proc/meminfo; grep -E "^(m2_obj|kmalloc-512) " /proc/slabinfo
grep m2_slab /proc/vmallocinfo
ls /sys/kernel/slab/m2_obj/ | tr '\n' ' '
cat /sys/kernel/slab/m2_obj/objs_per_slab /sys/kernel/slab/m2_obj/order /sys/kernel/slab/m2_obj/slab_size
cat /proc/buddyinfo
```

```text
--- 로드 전 ---
MemTotal:         965292 kB
MemFree:          570592 kB
MemAvailable:     821664 kB
Buffers:            7008 kB
Cached:           300656 kB
Slab:              35072 kB
SReclaimable:      12092 kB
SUnreclaim:        22980 kB
KernelStack:        1400 kB
PageTables:         1332 kB
VmallocUsed:        4692 kB
Percpu:              568 kB
CmaTotal:          32768 kB
CmaFree:           32384 kB
slabinfo - version: 2.1
# name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab> : tunables <limit> <batchcount> <sharedfactor> : slabdata <active_slabs> <num_slabs> <sharedavail>
kmalloc-512         1011   1024    512   16    2 : tunables    0    0    0 : slabdata     64     64      0
Node 0, zone      DMA    215    138     40     20      5      4      2      2      1      7    134 
--- 로드 후 ---
MemFree:          565348 kB
Slab:              38908 kB
SUnreclaim:        26808 kB
VmallocUsed:        4864 kB
m2_obj             20013  20013    192   21    1 : tunables    0    0    0 : slabdata    953    953      0
kmalloc-512         1011   1024    512   16    2 : tunables    0    0    0 : slabdata     64     64      0
aliases align cache_dma cpu_partial cpu_slabs ctor destroy_by_rcu hwcache_align min_partial object_size objects objects_partial objs_per_slab order partial poison reclaim_account red_zone remote_node_defrag_ratio sanity_checks sheaf_capacity shrink slab_size slabs slabs_cpu_partial store_user total_objects trace validate 
21
0
192
Node 0, zone      DMA      1     90     15     15      3      4      2      3      2      7    133 
```

출력에서 볼 것:

- `m2_obj  20013 20013 192 21 1 ... slabdata 953 953` — 활성 객체 20013(우리 2만 + 슬랩 끝자투리), 객체 크기 192, 슬랩 하나(1페이지 = order 0)에 21개, 슬랩 953개. 953 × 4 KiB = 3812 KiB.
- `Slab`이 35072 → 38908 kB(+3836), `SUnreclaim`이 22980 → 26808 kB(+3828). **slabinfo의 3812 KiB와 meminfo의 증가분이 거의 정확히 맞는다.** 드라이버 메모리를 이렇게 "두 군데에서 맞춰 보는" 습관이 누수 판정의 기본이다.
- `VmallocUsed`가 4692 → 4864 kB(+172). 그중 160 KiB(포인터 20000 × 8 B = 40 페이지)가 우리 배열이고, 나머지는 같은 시간에 다른 곳(모듈 코드 매핑 등)이 쓴 것이다.
- `grep m2_slab /proc/vmallocinfo`가 **아무것도 못 찾았다.** 아래 예제 13 참고.
- `buddyinfo`의 열은 order 0, 1, 2 … 10의 **빈 블록 개수**(order n = 2^n 페이지). 로드 후 order 0 블록이 215 → 1로 거의 다 소진됐다 — 953개의 order-0 슬랩을 잡느라 작은 블록부터 썼기 때문. 오른쪽 끝(order 10, 4 MiB 블록)이 134 → 133으로 줄어든 것도 보인다. **오른쪽 열이 0에 가까우면 큰 연속 메모리(DMA 버퍼, 고차 할당)가 실패하기 시작한다** — 단편화 진단의 첫 화면이다.
- `/sys/kernel/slab/m2_obj/`에는 `validate`, `trace`, `red_zone`, `poison`, `store_user` 같은 SLUB 디버그 손잡이가 있다(M8에서 `slub_debug`와 같이 다룬다).

### 5.4 예제 13 — vmallocinfo에서 모듈 init이 할당한 버퍼 찾기

무엇을 확인하나: 예제 12에서 `grep m2_slab`가 실패한 이유. BusyBox initramfs로 trace 커널을 다시 부팅해 같은 모듈을 넣고, 40페이지짜리 vmalloc을 직접 찾았다.

```sh
# LAB_CMD 로 실행 (initramfs, root, kptr_restrict=0)
insmod /m2_slab.ko && grep -E "pages=40 |m2_slab" /proc/vmallocinfo ; grep -c m2_slab /proc/kallsyms
```

```text
lab initramfs: 6.18.55-trace up 50.89s
0x0000000048748908-0x0000000046261985  167936 0xffffc78d4384710c pages=40 vmalloc N0=40
37
```

출력에서 볼 것:

- 크기 167936 = 41 페이지(40 + guard page 1), `pages=40 vmalloc`. 우리 배열이 맞다.
- 호출자 칸이 심볼이 아니라 **`0xffffc78d4384710c` 같은 맨 주소**다. `vzalloc()`을 부른 곳이 `__init` 함수(`m2_init`)이고, 모듈 로드가 끝나면 init 섹션은 해제되어 심볼 조회가 실패하기 때문이다. 모듈의 다른 심볼 37개는 kallsyms에 있지만 init 심볼은 없다. 실무에서 "vmallocinfo에서 내 드라이버를 못 찾겠다"면 크기·`pages=`로 찾거나, 할당을 probe(비-init 함수)에서 하게 한다.
- 이 initramfs에는 sysctl 설정이 없어 커널 기본값 `kptr_restrict=0`이고, 그때 `%pK`는 주소를 **해시값**으로 찍는다(시작 주소가 끝 주소보다 큰 이상한 범위가 그 증거). 진짜 가상 주소를 보려면 `kptr_restrict=1` + root(CAP_SYSLOG)여야 한다(§9의 sysctl 표).

### 5.5 예제 14 — 누수를 slabinfo로 잡기

무엇을 확인하나: `leak`에 다섯 번 써서 `kmalloc(512)` 5000개를 흘리고, rmmod 뒤에도 `kmalloc-512`가 줄지 않는지. 덤으로 `slabtop` 한 화면.

```sh
for i in 1 2 3 4 5; do echo x > /sys/kernel/debug/m2/leak; grep -E "^kmalloc-512 " /proc/slabinfo; done
slabtop -o -s c | head -14
rmmod m2_slab; sleep 1
grep -E "^(Slab|SUnreclaim)" /proc/meminfo; grep -E "^(m2_obj|kmalloc-512) " /proc/slabinfo; echo "[m2_obj gone?]"
```

```text
kmalloc-512         2000   2000    512   16    2 : tunables    0    0    0 : slabdata    125    125      0
kmalloc-512         2992   2992    512   16    2 : tunables    0    0    0 : slabdata    187    187      0
kmalloc-512         4000   4000    512   16    2 : tunables    0    0    0 : slabdata    250    250      0
kmalloc-512         4992   4992    512   16    2 : tunables    0    0    0 : slabdata    312    312      0
kmalloc-512         6000   6000    512   16    2 : tunables    0    0    0 : slabdata    375    375      0
--- slabtop ---
 Active / Total Objects (% used)    : 140306 / 143922 (97.5%)
 Active / Total Slabs (% used)      : 5347 / 5347 (100.0%)
 Active / Total Caches (% used)     : 112 / 166 (67.5%)
 Active / Total Size (% used)       : 33806.81K / 34603.10K (97.7%)
 Minimum / Average / Maximum Object : 0.01K / 0.24K / 8.75K
  OBJS ACTIVE  USE OBJ SIZE  SLABS OBJ/SLAB CACHE SIZE NAME                   
  6864   6864 100%    0.60K    528       13      4224K inode_cache            
 20013  20013 100%    0.19K    953       21      3812K m2_obj                 
  6000   6000 100%    0.50K    375       16      3000K kmalloc-512            
 21450  21450 100%    0.13K    715       30      2860K kernfs_node_cache      
 13482  13415  99%    0.19K    642       21      2568K dentry                 
  1918   1918 100%    1.10K    137       14      2192K ext4_inode_cache       
   704    686  97%    2.00K     44       16      1408K kmalloc-2k             
--- rmmod 후 ---
Slab:              37916 kB
SUnreclaim:        25532 kB
kmalloc-512         6000   6000    512   16    2 : tunables    0    0    0 : slabdata    375    375      0
[m2_obj gone?]
```

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <line x1="70" y1="230" x2="640" y2="230" stroke="currentColor"/>
  <line x1="70" y1="30" x2="70" y2="230" stroke="currentColor"/>
  <line x1="66" y1="230.0" x2="70" y2="230.0" stroke="currentColor"/>
  <text x="62" y="234.0" text-anchor="end" font-size="12">0</text>
  <line x1="66" y1="201.4" x2="70" y2="201.4" stroke="currentColor"/>
  <text x="62" y="205.4" text-anchor="end" font-size="12">1000</text>
  <line x1="66" y1="172.9" x2="70" y2="172.9" stroke="currentColor"/>
  <text x="62" y="176.9" text-anchor="end" font-size="12">2000</text>
  <line x1="66" y1="144.3" x2="70" y2="144.3" stroke="currentColor"/>
  <text x="62" y="148.3" text-anchor="end" font-size="12">3000</text>
  <line x1="66" y1="115.7" x2="70" y2="115.7" stroke="currentColor"/>
  <text x="62" y="119.7" text-anchor="end" font-size="12">4000</text>
  <line x1="66" y1="87.1" x2="70" y2="87.1" stroke="currentColor"/>
  <text x="62" y="91.1" text-anchor="end" font-size="12">5000</text>
  <line x1="66" y1="58.6" x2="70" y2="58.6" stroke="currentColor"/>
  <text x="62" y="62.6" text-anchor="end" font-size="12">6000</text>
  <line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/>
  <text x="62" y="34.0" text-anchor="end" font-size="12">7000</text>
  <text x="70.0" y="248" text-anchor="middle" font-size="12">로드 전</text>
  <text x="165.0" y="248" text-anchor="middle" font-size="12">leak 1</text>
  <text x="260.0" y="248" text-anchor="middle" font-size="12">leak 2</text>
  <text x="355.0" y="248" text-anchor="middle" font-size="12">leak 3</text>
  <text x="450.0" y="248" text-anchor="middle" font-size="12">leak 4</text>
  <text x="545.0" y="248" text-anchor="middle" font-size="12">leak 5</text>
  <text x="640.0" y="248" text-anchor="middle" font-size="12">rmmod 후</text>
  <polyline points="70.0,201.1 165.0,172.9 260.0,144.5 355.0,115.7 450.0,87.4 545.0,58.6 640.0,58.6" fill="none" stroke="#d0564a" stroke-width="2"/>
  <circle cx="70.0" cy="201.1" r="4" fill="#d0564a"/>
  <text x="70.0" y="191.1" text-anchor="middle" font-size="12">1011</text>
  <circle cx="165.0" cy="172.9" r="4" fill="#d0564a"/>
  <text x="165.0" y="162.9" text-anchor="middle" font-size="12">2000</text>
  <circle cx="260.0" cy="144.5" r="4" fill="#d0564a"/>
  <text x="260.0" y="134.5" text-anchor="middle" font-size="12">2992</text>
  <circle cx="355.0" cy="115.7" r="4" fill="#d0564a"/>
  <text x="355.0" y="105.7" text-anchor="middle" font-size="12">4000</text>
  <circle cx="450.0" cy="87.4" r="4" fill="#d0564a"/>
  <text x="450.0" y="77.4" text-anchor="middle" font-size="12">4992</text>
  <circle cx="545.0" cy="58.6" r="4" fill="#d0564a"/>
  <text x="545.0" y="48.6" text-anchor="middle" font-size="12">6000</text>
  <circle cx="640.0" cy="58.6" r="4" fill="#d0564a"/>
  <text x="640.0" y="48.6" text-anchor="middle" font-size="12">6000</text>
  <text x="80" y="34" font-size="13">kmalloc-512 active_objs (/proc/slabinfo)</text>
  <text x="355" y="274" text-anchor="middle" font-size="12">write 1회 = kmalloc(512) 1000개 · rmmod 해도 줄지 않는다 = 누수 신호</text>
</svg>
```
그림 5 — 예제 14의 실측. 쓸 때마다 정확히 약 1000개씩 늘고, 모듈을 내려도 그대로다.

출력에서 볼 것:

- `kmalloc-512` 활성 객체가 1011 → 2000 → … → 6000. 한 번에 약 1000개씩 정확히 늘었다.
- `slabtop -s c`(캐시 크기 순)에서 `m2_obj`(3812K)와 `kmalloc-512`(3000K)가 2·3위로 올라왔다. 필드에서 "메모리가 점점 줄어요"라는 보고를 받으면 **slabinfo를 주기적으로 찍어 diff**하는 것이 첫 수다.
- rmmod 후 `m2_obj` 줄은 사라졌다(캐시 파괴 = 전용 캐시는 정리됨). 하지만 `kmalloc-512`는 6000 그대로, `Slab`도 로드 전보다 약 2.8 MB 많다(37916 vs 35072 kB). 일반 kmalloc 캐시로 샌 메모리는 **모듈이 사라져도 주인 없이 남는다.** 어느 코드가 샜는지는 slabinfo로는 모른다 — 그다음 단계는 kmemleak(Image-kasan 커널, M8)이나 `slub_debug=U` + `/sys/kernel/slab/kmalloc-512/alloc_calls`다.

### 5.6 MemTotal 한 장으로 정리

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13">① 물리 RAM 1 GiB = 1,048,576 kB (DT memory@40000000, reg 크기 0x40000000)</text>
  <rect x="20" y="32" width="640" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="52" text-anchor="middle" font-size="12">/proc/iomem "System RAM 40000000-7fffffff"</text>
  <text x="20" y="92" font-size="13">② MemTotal 965,292 kB = RAM − (커널 코드·데이터 + memblock 예약)</text>
  <rect x="20" y="102" width="589.2" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="609.2" y="102" width="50.8" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="314.6" y="122" text-anchor="middle" font-size="12">버디 할당자가 관리 (zoneinfo managed)</text>
  <text x="634.6" y="122" text-anchor="middle" font-size="12">83 MB</text>
  <text x="20" y="162" font-size="13">③ MemTotal 안을 누가 쓰나 (모듈 로드 전 실측, 같은 축척)</text>
  <rect x="20.0" y="172" width="348.3" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="194.1" y="192" text-anchor="middle" font-size="12">MemFree 570,592</text>
  <rect x="368.3" y="172" width="187.8" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="462.2" y="192" text-anchor="middle" font-size="12">Cached+Buffers 307,664</text>
  <rect x="556.0" y="172" width="21.4" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <rect x="577.5" y="172" width="31.7" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <line x1="566.7" y1="232" x2="566.7" y2="206" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="560.7" y="248" text-anchor="end" font-size="12">Slab 35,072 kB = SReclaimable 12,092 + SUnreclaim 22,980</text>
  <line x1="593.3" y1="268" x2="593.3" y2="206" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="587.3" y="284" text-anchor="end" font-size="12">나머지 ≈ 52 MB: AnonPages · KernelStack 1,400 · PageTables 1,332 · 드라이버 alloc_pages …</text>
  <text x="20" y="312" font-size="12">VmallocUsed 4,692 kB와 CmaTotal 32,768 kB는 위 칸들과 겹쳐 세어지는 "다른 관점"의 숫자다</text>
</svg>
```
그림 6 — 예제 12의 로드 전 숫자로 그린 메모리 회계. RAM 1 GiB 중 83 MB는 MemTotal에 들어가지도 않는다(iomem의 Kernel code/data + reserved).

### 5.7 예제 15 — zoneinfo의 watermark

무엇을 확인하나: 존별 free 페이지와 min/low/high watermark.

```sh
grep -E "^Node|pages free|  min |  low |  high |managed" /proc/zoneinfo | head -24
cat /proc/sys/vm/min_free_kbytes
```

```text
Node 0, zone      DMA
  pages free     141889
        min      5632
        low      7040
        high     8448
        managed  241323
...
vm/min_free_kbytes = 22528
```

(DMA32·Normal·Movable 존은 managed 0이라 생략했다.)

출력에서 볼 것:

- RAM이 전부 `0x40000000..0x7fffffff`(4 GiB 아래)라서 arm64에서는 **전부 DMA 존**에 들어간다. `managed 241323` 페이지 × 4 KiB = 965292 kB = `MemTotal`과 정확히 같다.
- `min 5632` 페이지 × 4 KiB = 22528 kB = `vm.min_free_kbytes`. free가 `low` 아래로 내려가면 kswapd가 깨어나고, `min` 아래면 할당하는 쪽이 직접 회수(direct reclaim)한다 — 인터럽트 문맥의 `GFP_ATOMIC` 할당이 실패하기 시작하는 지점이다. 네트워크 드라이버가 "page allocation failure: order:2"를 찍는 보드에서는 이 값과 buddyinfo를 같이 본다.

---

## 6. 질문 ⑥ — "자원이 살아 있나?" (clk · regulator · pinctrl · GPIO · regmap · devlink · power · DMA)

bind는 됐는데 장치가 응답하지 않는다. bare-metal이라면 "클럭 게이트 열렸나, 전원 레일 올라왔나, 핀먹스 맞나, 레지스터 값 맞나"를 JTAG로 본다. Linux에서는 각 프레임워크(H2·H3·H4)가 debugfs에 **요약표**를 내놓는다. 이 표들은 "드라이버가 요청한 상태"를 보여 준다 — 실제 HW 레지스터와 다를 수 있다는 점은 §6.6에서 다시 말한다.

### 6.1 예제 16 — `clk_summary`와 `regulator_summary`

무엇을 확인하나: QEMU virt의 클럭 트리와 레귤레이터 목록.

```sh
cat /sys/kernel/debug/clk/clk_summary
cat /sys/kernel/debug/regulator/regulator_summary
```

```text
                                 enable  prepare  protect                                duty  hardware                            connection
   clock                          count    count    count        rate   accuracy phase  cycle    enable   consumer                         id
---------------------------------------------------------------------------------------------------------------------------------------------
 clk24mhz                            5       5        0        24000000    0          0     50000      Y   9010000.pl031                   apb_pclk                 
                                                                                                           9030000.pl061                   apb_pclk                 
                                                                                                           9000000.pl011                   no_connection_id         
                                                                                                           9000000.pl011                   apb_pclk                 
                                                                                                           deviceless                      no_connection_id         

 regulator                      use open bypass  opmode voltage current     min     max
---------------------------------------------------------------------------------------
 regulator-dummy                  1    0      0 unknown     0mV     0mA     0mV     0mV 
```

출력에서 볼 것:

- virt에는 클럭이 `clk24mhz`(DT `apb-pclk` fixed-clock, 24 MHz) **하나뿐**이고, consumer 칸에 pl031·pl061·pl011이 connection id(`apb_pclk`, `no_connection_id`)와 함께 나온다. `enable count 5` = 소비자 다섯이 `clk_prepare_enable()`을 했다. `hardware enable Y`는 클럭 드라이버의 `is_enabled` 콜백 결과다.
- 레귤레이터는 `regulator-dummy` 하나. DT에 공급자가 없는 `*-supply`를 요청하면 regulator core가 이 dummy를 준다(부팅 로그 `supply vdd not found, using dummy regulator`).
- 실제 SoC에서는 (실행 안 함, 구조 설명) 이 표가 수백 줄이다: PLL → divider → gate로 들여쓰기된 트리가 나오고, 각 줄의 `rate`로 "I2C 컨트롤러 입력 클럭이 정말 66 MHz인가", `enable count`로 "누가 이 게이트를 잡고 있어서 절전이 안 되나"를 본다. `regulator_summary`도 PMIC의 buck/LDO 아래 소비자 장치가 들여쓰기되고, `use`·`open`·`voltage`·소비자별 요청 전압(min/max)이 나온다. 센서 레일이 `0mV`나 `use 0`이면 전원부터 의심한다.
- 부팅 로그의 `clk: Disabling unused clocks`(이 게스트에서도 29.44초에 찍혔다)는 late_initcall에서 **아무도 enable 안 한 클럭을 끄는** 단계다. 부트로더가 켜 둔 클럭에 기대던 장치가 이 줄 직후 죽으면, 드라이버가 클럭을 요청하지 않은 것이다. 임시 확인용 커널 인자는 `clk_ignore_unused`, 같은 성격의 레귤레이터 쪽은 `regulator_ignore_unused`.

### 6.2 예제 17 — pinctrl, devlink, wakeup source, DMA

무엇을 확인하나: virt 게스트에서 다른 자원 요약 파일이 무엇을 보여 주는지(거의 비어 있다는 사실 자체가 정보다).

```sh
ls /sys/kernel/debug/pinctrl/
ls /sys/class/devlink/ | wc -l; ls /sys/bus/platform/devices/gpio-keys/ | tr "\n" " "
head -5 /sys/kernel/debug/wakeup_sources
cat /sys/kernel/debug/dmaengine/summary; ls /sys/kernel/debug/swiotlb
cat /sys/kernel/debug/swiotlb/io_tlb_nslabs /sys/kernel/debug/swiotlb/io_tlb_used
```

```text
pinctrl-devices
pinctrl-handles
pinctrl-maps
0
disabled_keys disabled_switches driver driver_override input keys modalias of_node power subsystem switches uevent 
name		active_count	event_count	wakeup_count	expire_count	active_since	total_time	max_time	last_change	prevent_suspend_time
alarmtimer.0.auto	0		0		0		0		0		0		0		0		0
9010000.pl031	1		1		0		0		0		0		0		738079		0
deleted     	0		0		0		0		0		0		0		0		0
summary
io_tlb_nslabs
io_tlb_used
io_tlb_used_hiwater
512
0
```

(DMA 부분은 `ls /sys/kernel/debug` 전체 목록을 빼고 결과만 남겼다. `dmaengine/` 안에는 `summary` 파일만 있고 내용은 비어 있다.)

출력에서 볼 것과 실제 SoC와의 차이:

- **pinctrl**: `pinctrl-devices`·`pinctrl-handles`·`pinctrl-maps` 세 파일만 있고 컨트롤러 디렉터리가 없다 — virt에는 핀 컨트롤러가 없다. 실제 SoC(실행 안 함)라면 `/sys/kernel/debug/pinctrl/<컨트롤러>/` 아래 `pins`(패드 번호·이름·현재 레지스터 값), `pinmux-pins`(각 핀을 **누가** 어떤 function으로 잡았나 — "pin 42 (UART2_TX): device 30890000.serial function uart2 group uart2grp"), `pinconf-pins`(pull·drive strength)가 있다. 두 드라이버가 한 핀을 다투면 두 번째 probe가 `pin X already requested by Y`로 실패하고 `pinmux-pins`에 첫 주인이 보인다(H4).
- **devlink**: `/sys/class/devlink/`에 링크가 0개. gpio-keys는 pl061의 GPIO를 쓰지만 GPIO가 자식 노드(`poweroff { gpios = ... }`)에 있어서인지 장치 링크가 만들어지지 않았다(원인은 확인하지 않았다 — 관찰 사실만 적는다). 실제 SoC에서는 fw_devlink(기본 `fw_devlink=on`)가 DT의 `clocks`·`*-supply`·`*-gpios`·`interrupts`·`pinctrl-0` 참조마다 `<공급자>--<소비자>` 링크를 만들고, 각 링크의 `status`(`available`, `consumer probing`, `active`, `supplier unbinding`…)와 `sync_state_only`가 보인다. 공급자가 bind되기 전에는 소비자의 probe를 아예 시작하지 않으므로 `devices_deferred`에도 안 나오는 경우가 있다 — 그때는 `/sys/kernel/debug/devices_deferred` 대신 이 링크들과 부팅 로그의 `fw_devlink` 경고를 본다(G3 §9, D3).
- **wakeup_sources**: RTC(`9010000.pl031`)와 alarmtimer만. 시스템이 suspend에 안 들어가면(N 모듈) `active_count`·`prevent_suspend_time`이 늘어나는 소스가 범인이다.
- **DMA**: virt에는 DMA 엔진 컨트롤러가 없어서 `dmaengine/summary`가 빈 파일이다. 실제 SoC라면 채널별로 "dma0chan3 (uart2-rx): in-use"처럼 누가 어떤 채널을 잡았는지가 나온다. `swiotlb`는 DMA 주소 제한이 있는 장치용 바운스 버퍼 풀이다. `io_tlb_used`가 0이 아니면 **어떤 장치가 바운스 복사를 하고 있다는 뜻**이라 성능 문제의 원인이 될 수 있다(H9). 이 게스트는 512 슬랩 중 0개 사용.

### 6.3 예제 18 — `m2_regs.c`: regmap debugfs와 runtime PM 상태

무엇을 확인하는 코드인가: H2의 regmap을 진단 관점에서 본다. RAM 버퍼 32바이트를 "레지스터 8개짜리 칩"처럼 regmap-mmio로 감싸고, 런타임 PM을 켠다. 실제 SoC라면 `base`가 `devm_platform_ioremap_resource()`의 결과다.

```c
// m2_regs.c — RAM 버퍼를 "레지스터"로 쓰는 regmap-mmio + runtime PM 플랫폼 드라이버
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

static struct platform_device *pdev_m2;
static int fail;                          /* 예: fail=-5 → probe 실패 흉내 */
module_param(fail, int, 0644);
static const struct regmap_config m2_cfg = {
	.reg_bits = 32, .val_bits = 32, .reg_stride = 4,
	.max_register = 0x1c,          /* 레지스터 8개: 0x00..0x1c */
};

static int m2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *base = (void __force __iomem *)devm_kzalloc(dev, 0x20, GFP_KERNEL);
	struct regmap *map;

	if (fail)
		return fail;                  /* driver core가 어떻게 찍는지 본다 */
	if (!base)
		return -ENOMEM;
	map = devm_regmap_init_mmio(dev, base, &m2_cfg);   /* 진짜 SoC라면 ioremap 주소 */
	if (IS_ERR(map))
		return dev_err_probe(dev, PTR_ERR(map), "regmap\n");
	regmap_write(map, 0x00, 0x4d320001);   /* CHIP_ID 흉내 */
	regmap_write(map, 0x08, 0x000000ff);   /* CTRL */
	regmap_update_bits(map, 0x0c, 0xf0, 0x30);
	devm_pm_runtime_enable(dev);           /* power/runtime_status 관찰용 */
	dev_info(dev, "probed, regmap at %px\n", base);
	return 0;
}
static struct platform_driver m2_drv = {
	.probe = m2_probe,
	.driver = { .name = "m2_regs" },
};
static int __init m2_init(void)
{
	int ret = platform_driver_register(&m2_drv);
	if (ret)
		return ret;
	pdev_m2 = platform_device_register_simple("m2_regs", -1, NULL, 0);
	if (IS_ERR(pdev_m2)) {
		platform_driver_unregister(&m2_drv);
		return PTR_ERR(pdev_m2);
	}
	return 0;
}
static void __exit m2_exit(void)
{
	platform_device_unregister(pdev_m2);
	platform_driver_unregister(&m2_drv);
}
module_init(m2_init); module_exit(m2_exit);
MODULE_LICENSE("GPL"); MODULE_DESCRIPTION("M2 m2_regs demo");
```

```sh
insmod /root/m2/m2_regs.ko; sleep 1; ls /sys/kernel/debug/regmap/
R=/sys/kernel/debug/regmap/m2_regs; ls $R; cat $R/registers; cat $R/range
ls -l /sys/bus/platform/devices/m2_regs/driver
cd /sys/bus/platform/devices/m2_regs/power
for f in control runtime_status runtime_usage runtime_enabled runtime_active_time runtime_suspended_time; do echo "$f: $(cat $f 2>&1)"; done
echo m2_regs > /sys/bus/platform/drivers/m2_regs/unbind; ls /sys/kernel/debug/regmap/ | grep -c m2_regs
echo m2_regs > /sys/bus/platform/drivers/m2_regs/bind; ls /sys/kernel/debug/regmap/ | grep m2
```

```text
m2_regs
access
name
range
registers
00: 4d320001
04: 00000000
08: 000000ff
0c: 00000030
10: 00000000
14: 00000000
18: 00000000
1c: 00000000
0-1c
lrwxrwxrwx 1 root root 0 Oct 18 06:56 /sys/bus/platform/devices/m2_regs/driver -> ../../../bus/platform/drivers/m2_regs
control: auto
runtime_status: suspended
runtime_usage: 0
runtime_enabled: enabled
runtime_active_time: 0
runtime_suspended_time: 4611
0
m2_regs
```

(첫 `cat runtime_status` 한 줄 중복은 뺐다.)

출력에서 볼 것:

- `/sys/kernel/debug/regmap/m2_regs/registers`가 **레지스터 덤프 그 자체**다. 0x00 = `4d320001`(가짜 CHIP_ID), 0x08 = `ff`, 0x0c = `30`(`regmap_update_bits`로 0xf0 마스크에 0x30). I2C/SPI 센서도 regmap을 쓰면 똑같이 나온다 — bring-up 때 "드라이버가 칩에 무엇을 써 놨나"를 JTAG 없이 본다. `range`는 유효 레지스터 범위, `access`는 레지스터별 읽기/쓰기/volatile/precious 속성이다.
- 캐시가 있는 regmap(`REGCACHE_RBTREE`/`MAPLE`)이면 `registers`는 **캐시 값**을 보여 줄 수 있다. 칩이 리셋돼서 실제 값이 바뀌었는데 캐시는 그대로인 상황이 흔하다 — volatile 레지스터 지정과 `cache_bypass` 파일을 기억하자(H2).
- 기본적으로 debugfs regmap은 **읽기 전용**이다. 쓰기는 커널 소스의 `REGMAP_ALLOW_WRITE_DEBUGFS`를 직접 켜고 빌드해야 한다(위험하니까 일부러 막아 둠).
- unbind하면 regmap 디렉터리가 사라지고(`grep -c` = 0), bind하면 다시 생긴다. devm으로 만든 자원은 bind 단위로 산다(G3).
- runtime PM: `control: auto`, `runtime_status: suspended`, `runtime_usage: 0`. 콜백 없이 `pm_runtime_enable()`만 한 장치는 처음부터 suspended 상태다. `runtime_usage`(사용 카운트)는 `CONFIG_PM_ADVANCED_DEBUG`가 있어야 보이는 파일인데 trace 커널에 켜져 있다(Image-sec로 한 시험 부팅에서는 이 파일이 없었다). **센서가 응답 없음 → `runtime_status`가 `suspended`인데 드라이버가 `pm_runtime_get_sync()` 없이 레지스터를 만진다**는 것이 흔한 버그다. `control`에 `on`을 쓰면 런타임 PM을 강제로 끄고 계속 켜 둔다 — 진단용으로 유용하다.

### 6.4 예제 19 — debugfs `gpio`와 libgpiod로 본 GPIO

무엇을 확인하나: 예제 3 뒤에 gpio-sim 칩의 라인 상태. 커널 소비자(`reset`, `drdy`)가 잡은 라인과 유저스페이스가 잡은 라인이 어떻게 보이는지. 랩 rootfs의 libgpiod는 1.6.3이라 v1 문법을 쓴다(v2는 `gpioinfo -c gpiochip1` 형식).

```sh
cat /sys/kernel/debug/gpio
gpioinfo gpiochip1; gpiodetect
gpioset --mode=signal gpiochip1 0=1 & GS=$!; sleep 2
grep -A2 "gpiochip1:" /sys/kernel/debug/gpio; gpioinfo gpiochip1 | head -3; kill $GS; wait $GS
gpioget gpiochip1 5; echo "rc=$?"; gpioget gpiochip1 1; echo "rc=$?"
```

```text
gpiochip0: 8 GPIOs, parent: amba/9030000.pl061, 9030000.pl061:
 gpio-3   (                    |GPIO Key Poweroff   ) in  lo IRQ 
gpiochip1: 8 GPIOs, parent: platform/gpio-sim.0, m2_sim, can sleep:
 gpio-0   (<unused>) input,pull-down
 gpio-1   (<unused>) input,pull-down
 gpio-2   (<unused>) input,pull-down
 gpio-3   (reset) output-high,pull-down
 gpio-4   (<unused>) input,pull-down
 gpio-5   (drdy) input,pull-down
 gpio-6   (<unused>) input,pull-down
 gpio-7   (<unused>) input,pull-down
gpiochip1 - 8 lines:
	line   0:     "m2_led"       unused   input  active-high 
	line   1:      unnamed       unused   input  active-high 
	line   2:      unnamed       unused   input  active-high 
	line   3:      unnamed      "reset"  output   active-low [used]
	line   4:      unnamed       unused   input  active-high 
	line   5:      unnamed       "drdy"   input  active-high [used]
	line   6:      unnamed       unused   input  active-high 
	line   7:      unnamed       unused   input  active-high 
gpiochip0 [9030000.pl061] (8 lines)
gpiochip1 [m2_sim] (8 lines)
--- gpioset 실행 중 ---
gpiochip1: 8 GPIOs, parent: platform/gpio-sim.0, m2_sim, can sleep:
 gpio-0   (gpioset) output-high,pull-down
 gpio-1   (<unused>) input,pull-down
gpiochip1 - 8 lines:
	line   0:     "m2_led"    "gpioset"  output  active-high [used]
	line   1:      unnamed       unused   input  active-high 
gpioget: error reading GPIO values: Device or resource busy
rc=1
0
rc=0
```

출력에서 볼 것:

- debugfs `gpio`는 칩마다 부모 장치(`platform/gpio-sim.0`), 라벨(`m2_sim`), **`can sleep`**(I2C 확장기처럼 잠들 수 있는 칩 → 하드 IRQ 문맥에서 `gpiod_set_value()` 금지, H4)을 보여 준다. 라인별로 **소비자 이름**(`reset`, `drdy`, `GPIO Key Poweroff`), 방향, 값, `IRQ` 표시가 나온다.
- `reset`이 `output-high`로 보이는 점에 주의. 우리는 `GPIOD_OUT_LOW`(논리 0)로 요청했지만 lookup에 `GPIO_ACTIVE_LOW`를 줬다. **debugfs는 물리 레벨**, `gpioinfo`는 `active-low` 플래그를 따로 보여 준다. "리셋을 풀었는데 칩이 계속 리셋 상태"인 버그의 절반은 이 극성 혼동이다.
- `gpioget gpiochip1 5`는 `Device or resource busy` — 커널 드라이버가 잡은 라인은 유저가 못 연다. 빈 라인 1은 읽힌다(`0`).
- `gpioset`이 도는 동안 라인 0의 소비자가 `gpioset`으로 바뀐다. 이 표 하나로 "누가 이 핀을 잡고 있나"가 끝난다.

### 6.5 진단 파일과 프레임워크 짝 (요약)

| 질문 | 파일 | 프레임워크 노트 |
|---|---|---|
| 클럭 rate·enable 카운트·소비자 | `/sys/kernel/debug/clk/clk_summary` (트리별 `clk/<이름>/`) | H3 |
| 레일 전압·소비자·사용 카운트 | `/sys/kernel/debug/regulator/regulator_summary` | H3 |
| 핀 소유자·function | `/sys/kernel/debug/pinctrl/<ctrl>/pinmux-pins` · `pins` · `pinconf-pins` | H4 |
| GPIO 라인 소유자·방향·값 | `/sys/kernel/debug/gpio`, `gpioinfo` | H4 |
| 칩 레지스터 | `/sys/kernel/debug/regmap/<dev>/registers` | H2 |
| 공급자-소비자 관계 | `/sys/class/devlink/*/status`, `<dev>/supplier:*`·`consumer:*` 링크 | G3 |
| 런타임 전원 상태 | `<dev>/power/runtime_status` · `runtime_usage` · `control` | N 모듈 |
| power domain | `/sys/kernel/debug/pm_genpd/pm_genpd_summary` | N 모듈 |
| DMA 채널·바운스 버퍼 | `/sys/kernel/debug/dmaengine/summary`, `swiotlb/` | H9 |

### 6.6 함정 — 요약표는 "커널이 믿는 상태"다

`clk_summary`의 rate는 클럭 드라이버가 **계산한** 값이고, `regulator_summary`의 voltage는 PMIC 드라이버가 **설정했다고 믿는** 값이다. 클럭 드라이버의 divider 테이블이 틀렸거나 PMIC 레지스터 쓰기가 I2C 오류로 조용히 실패하면 표는 정상인데 HW는 다르다. Don이 잘하는 영역이 여기다 — 표가 정상인데 동작이 이상하면 **scope로 클럭 핀·레일을 직접 재고**(M11), `devmem`/`/dev/mem` 또는 regmap 덤프로 실제 레지스터를 읽어 비교한다.

---

## 7. 모듈 — `lsmod`, `/proc/modules`, `/sys/module`, taint

### 7.1 예제 20 — 같은 정보, 다른 창구

무엇을 확인하나: `lsmod`와 `/proc/modules`(root와 nobody), `/sys/module/<mod>/`, 그리고 out-of-tree·서명 없는 모듈이 남긴 taint.

```sh
lsmod | head -5; grep m2_ /proc/modules
setpriv --reuid=65534 --regid=65534 --clear-groups grep m2_ /proc/modules
ls /sys/module/m2_slab/
cat /sys/module/m2_slab/parameters/count /sys/module/m2_slab/taint /sys/module/m2_slab/refcnt /sys/module/m2_slab/coresize
cat /proc/sys/kernel/tainted; dmesg | grep -iE "taint|loading out-of-tree" | head -3
```

```text
Module                  Size  Used by
m2_slab                12288  0
m2_regs                12288  0
m2_defer               12288  0
m2_slab 12288 0 - Live 0xffffdab7d499e000 (OE)
m2_regs 12288 0 - Live 0xffffdab7d4998000 (OE)
m2_defer 12288 0 - Live 0xffffdab7d4992000 (OE)
m2_slab 12288 0 - Live 0x0000000000000000 (OE)
m2_regs 12288 0 - Live 0x0000000000000000 (OE)
m2_defer 12288 0 - Live 0x0000000000000000 (OE)
coresize
holders
initsize
initstate
notes
parameters
refcnt
sections
taint
uevent
20000
OE
0
12288
12288
[  605.808345] m2_defer: loading out-of-tree module taints kernel.
[  605.808624] m2_defer: module verification failed: signature and/or required key missing - tainting kernel
```

출력에서 볼 것:

- `lsmod`는 `/proc/modules`를 예쁘게 찍는 도구일 뿐이다. `/proc/modules`에는 상태(`Live`/`Loading`/`Unloading`), 로드 주소, taint 플래그(`(OE)`)가 더 있다. **nobody에게는 주소가 0**이다(`kptr_restrict=1` → CAP_SYSLOG 없으면 가림). oops의 PC를 모듈 오프셋으로 바꿀 때(M3) 이 주소가 필요하니 root로 본다.
- `/sys/module/m2_slab/` 아래 값 다섯 개는 순서대로 `parameters/count`=20000, `taint`=`OE`, `refcnt`=0, `coresize`=12288, 그리고 마지막 줄 `/proc/sys/kernel/tainted`=12288이다(우연히 coresize와 같은 숫자다).
- tainted 12288 = 4096 + 8192 = bit 12(`O`, out-of-tree 모듈) + bit 13(`E`, 서명 안 된 모듈). Image-sec 계열은 `MODULE_SIG`가 켜져 있지만 강제가 아니라서 로드는 되고 `E`가 붙는다. 이 taint는 **이후 모든 oops 헤더에 `Tainted: G OE`로 찍힌다** — 커뮤니티에 버그를 보고할 때 "out-of-tree 모듈 없이 재현되나?"를 먼저 묻는 이유다.
- `refcnt`가 0이 아니면 rmmod가 `Module is in use`로 거부한다. `holders/`에 누가 이 모듈을 쓰는지 있다.

| taint 글자 | 비트 | 뜻 |
|---|---|---|
| `P` | 0 | 독점(non-GPL) 모듈 |
| `W` | 9 | WARN이 한 번이라도 발생 |
| `O` | 12 | out-of-tree 모듈 |
| `E` | 13 | 서명 없는 모듈 (서명 검사 켜진 커널) |
| `D` | 7 | oops/BUG 발생 |
| `L` | 14 | soft lockup 발생 |

> 참고: 비트 전체 목록은 docs.kernel.org의 "Tainted kernels" 문서와 `tools/debugging/kernel-chktaint` 스크립트에 있다. bring-up 중 `W`가 붙어 있다면 dmesg 어딘가에 WARN 스택이 있다는 뜻이니 꼭 찾아본다.

---

## 8. "앱이 멈췄다" — `/proc/<pid>/stack`, `wchan`, `status`, `syscall`

### 8.1 직관

RTOS에서 태스크가 멈추면 태스크 리스트에서 상태(Blocked)와 "무엇을 기다리나(semaphore X)"를 본다. Linux에서는 `/proc/<pid>/` 아래가 그 태스크 리스트다(A2). 특히 **커널 스택**(`stack`)은 "이 프로세스가 커널 안 어느 함수에서 자고 있나"를 바로 보여 준다. 센서 앱이 `read()`에서 안 돌아오면 → 드라이버의 wait_event 줄이 보인다.

### 8.2 예제 21 — 드라이버 안에서 잠든 태스크 찾기

무엇을 확인하나: `m2_slab`의 `block` 파일을 읽는 `cat`이 잠든 상태에서 `/proc/<pid>/`의 여러 파일. 그리고 nobody가 남의 stack을 볼 수 있는지.

```sh
setpriv --reuid=65534 --regid=65534 --clear-groups cat /proc/1/stack; echo "nobody stack rc=$?"
cat /sys/kernel/debug/m2/block & BP=$!; sleep 1
echo "pid=$BP"; grep -E "^(State|voluntary_ctxt_switches|nonvoluntary_ctxt_switches)" /proc/$BP/status
echo "wchan=$(cat /proc/$BP/wchan)"; cat /proc/$BP/stack
ps -o pid,stat,wchan:32,comm -p $BP; cat /proc/$BP/syscall
echo go > /sys/kernel/debug/m2/block; wait $BP; echo "reader exited rc=$?"
```

```text
cat: /proc/1/stack: Permission denied
nobody stack rc=1
pid=796
State:	S (sleeping)
voluntary_ctxt_switches:	1
nonvoluntary_ctxt_switches:	17
wchan=blk_read
[<0>] blk_read+0x94/0xe0 [m2_slab]
[<0>] full_proxy_read+0x6c/0xb0
[<0>] vfs_read+0xcc/0x350
[<0>] ksys_read+0x6c/0x110
[<0>] __arm64_sys_read+0x20/0x40
[<0>] invoke_syscall.constprop.0+0x48/0xf0
[<0>] el0_svc_common.constprop.0+0x40/0xf0
[<0>] do_el0_svc+0x24/0x40
[<0>] el0_svc+0x40/0x180
[<0>] el0t_64_sync_handler+0xa0/0xf0
[<0>] el0t_64_sync+0x198/0x1a0
    PID STAT WCHAN                            COMMAND
    796 S    blk_read                         cat
63 0x3 0xffffbe906000 0x20000 0x1 0x0 0xffffbeb838f0 0xffffe161ae10 0xffffbea61208
reader exited rc=0
```

출력에서 볼 것:

- `State: S (sleeping)` = `TASK_INTERRUPTIBLE`(시그널로 깨울 수 있다, F3). `D (disk sleep)`이면 `TASK_UNINTERRUPTIBLE` — kill도 안 먹고, 120초(`hung_task_timeout_secs`)가 넘으면 hung task 경고가 뜬다.
- `wchan=blk_read` — 잠든 함수 이름 한 단어. `ps -o wchan`도 같은 파일을 읽는다.
- `stack`은 위에서부터 `blk_read+0x94/0xe0 [m2_slab]` ← `full_proxy_read`(debugfs의 래퍼) ← `vfs_read` ← `ksys_read` ← `__arm64_sys_read` ← … ← `el0t_64_sync`(유저 모드에서 SVC 예외 진입, E6). **맨 위 줄의 `[모듈명]`과 오프셋**이 드라이버의 정확한 위치다. `+0x94/0xe0`은 "함수 시작에서 0x94바이트, 함수 길이 0xe0"이고, `scripts/faddr2line m2_slab.ko blk_read+0x94/0xe0`으로 소스 줄로 바꿀 수 있다(M3).
- `syscall`의 첫 숫자 63은 arm64의 `read` syscall 번호, 둘째 0x3은 fd 3이다. 그 뒤는 나머지 인자·스택 포인터·PC.
- nobody는 `/proc/1/stack`에 `Permission denied`. `stack` 파일은 root 전용(0400 + ptrace 권한 검사)이다.
- `voluntary_ctxt_switches: 1` — 스스로 한 번 잠들었다. 이 수가 초당 수천씩 오르는 스레드는 짧게 자고 깨기를 반복하는 것이다(폴링 루프, 지나치게 잦은 IRQ thread).

### 8.3 예제 22 — 메모리 쪽 `/proc/<pid>/`

무엇을 확인하나: systemd-journald의 `smaps_rollup`·`status`·`maps` 앞부분.

```sh
J=$(pidof systemd-journald); cat /proc/$J/smaps_rollup
grep -E "^(Name|State|VmRSS|VmHWM|Threads|Cpus_allowed_list|voluntary)" /proc/$J/status; head -5 /proc/$J/maps
```

```text
aaaac3ce0000-ffffde56d000 ---p 00000000 00:00 0                          [rollup]
Rss:                9580 kB
Pss:                4197 kB
Pss_Dirty:          1708 kB
Pss_Anon:           1028 kB
Pss_File:           3161 kB
Pss_Shmem:             8 kB
Shared_Clean:       6116 kB
Shared_Dirty:          8 kB
Private_Clean:      1752 kB
Private_Dirty:      1704 kB
Referenced:         9580 kB
Anonymous:          1028 kB
AnonHugePages:         0 kB
Swap:                  0 kB
Name:	systemd-journal
State:	S (sleeping)
VmHWM:	    9580 kB
VmRSS:	    9580 kB
Threads:	1
Cpus_allowed_list:	0-1
voluntary_ctxt_switches:	558
aaaac3ce0000-aaaac3d0b000 r-xp 00000000 fe:00 13653                      /usr/lib/systemd/systemd-journald
aaaac3d1e000-aaaac3d20000 r--p 0002e000 fe:00 13653                      /usr/lib/systemd/systemd-journald
aaaac3d20000-aaaac3d21000 rw-p 00030000 fe:00 13653                      /usr/lib/systemd/systemd-journald
aaaac841d000-aaaac8460000 rw-p 00000000 00:00 0                          [heap]
ffffbabf0000-ffffbac00000 ---p 00000000 00:00 0 
```

(`smaps_rollup`에서 0인 줄 몇 개는 뺐다.)

출력에서 볼 것: `Rss` 9580 kB 중 `Pss`는 4197 kB. 공유 라이브러리 페이지를 공유자 수로 나눈 값이 PSS라서, **"이 프로세스를 죽이면 실제로 얼마가 돌아오나"는 RSS가 아니라 PSS(그중에서도 Private)** 쪽이 가깝다. 임베디드에서 "어떤 데몬이 RAM을 먹나"는 `smaps_rollup`의 `Pss_Anon`과 `Private_Dirty`로 비교한다. `maps`는 A5에서 본 주소 공간 레이아웃이다.

---

## 9. 그 밖의 손잡이와 표 — `/proc/sys`, `timer_list`, `softirqs`, `locks`, workqueue

### 9.1 예제 23 — 진단에 자주 쓰는 sysctl 값 읽기

무엇을 확인하나: 이 게스트의 printk·kptr·panic·hung task·VM 관련 값.

```sh
for k in kernel/printk kernel/kptr_restrict kernel/dmesg_restrict kernel/panic kernel/panic_on_oops \
         kernel/hung_task_timeout_secs vm/overcommit_memory vm/min_free_kbytes kernel/perf_event_paranoid; do
  echo "$k = $(cat /proc/sys/$k)"; done
```

```text
kernel/printk = 4	4	1	7
kernel/kptr_restrict = 1
kernel/dmesg_restrict = 0
kernel/panic = 0
kernel/panic_on_oops = 0
kernel/hung_task_timeout_secs = 120
vm/overcommit_memory = 0
vm/min_free_kbytes = 22528
kernel/perf_event_paranoid = 2
```

| 값 | 뜻 | bring-up 때 |
|---|---|---|
| `kernel/printk = 4 4 1 7` | 콘솔 레벨, 기본 메시지 레벨, 최소, 부팅 기본 | 콘솔에 `pr_info`를 보고 싶으면 첫 값을 8로(M1) |
| `kptr_restrict = 1` | 커널 주소를 CAP_SYSLOG 있는 사람에게만 | `/proc/modules`·`kallsyms`가 0으로 보이는 이유 |
| `dmesg_restrict = 0` | 일반 사용자도 dmesg 가능 | 양산 이미지는 보통 1 |
| `panic = 0` · `panic_on_oops = 0` | panic 후 재부팅 안 함 · oops를 panic으로 안 올림 | 필드 장비는 `panic=10`(10초 후 재부팅) + `panic_on_oops=1`이 흔하다(L7) |
| `hung_task_timeout_secs = 120` | D 상태 120초면 경고 | 드라이버 deadlock 탐지(M8). Image-sec 첫 시험 부팅에는 파일이 없었다(옵션 차이) |
| `min_free_kbytes = 22528` | 예약 여유 메모리 | §5.7 watermark |
| `perf_event_paranoid = 2` | 일반 사용자 perf 제한 | M6 |

### 9.2 `timer_list`, `softirqs`, `locks`, workqueue

- `/proc/timer_list`: CPU별 hrtimer 큐와 **tick device**. 이 게스트에서는 각 CPU가 `arch_sys_timer`(`set_next_event: arch_timer_set_next_event_virt`, `event_handler: hrtimer_interrupt`)를 쓰고, 별도로 broadcast 장치 `bc_hrtimer`가 있다. CPU idle에서 로컬 타이머가 멈추는 SoC에서는 broadcast 장치가 진짜 HW 타이머여야 한다 — "idle 들어가면 시스템이 멈춰요"의 단서가 여기 있다(F8, N 모듈).

```text
now at 776492779021 nsecs
cpu: 0
cpu: 1
Tick Device: mode:     1
Clock Event Device: bc_hrtimer
 max_delta_ns:   9223372036854775807
 set_next_event: 0x0
 event_handler:  tick_handle_oneshot_broadcast
Tick Device: mode:     1
Clock Event Device: arch_sys_timer
 max_delta_ns:   2199023255551
 set_next_event: arch_timer_set_next_event_virt
```

- `/proc/softirqs`: §3.4에서 본 것처럼 softirq 종류별·CPU별 카운트. `NET_RX`가 한 CPU에만 몰리면 RPS/IRQ affinity를 본다. `TIMER`·`SCHED`·`RCU`는 늘 움직이는 배경 소음이다.
- `/proc/locks`: 유저스페이스 파일 잠금(flock/fcntl/OFD) 목록. 이 게스트에서는 그 순간 아무도 잠금을 안 잡고 있어서 비어 있었다. "앱 두 개가 같은 설정 파일에서 서로 기다린다"를 볼 때 쓴다. **커널 내부 lock(mutex·spinlock)은 여기 안 나온다** — 그건 lockdep `/proc/lockdep*`(M8)과 `/proc/<pid>/stack`이다.
- workqueue: `/sys/devices/virtual/workqueue/`에는 `WQ_SYSFS` 플래그로 만든 큐만 보인다(이 게스트: `blkcg_punt_bio`, `writeback`, 그리고 unbound 큐 기본 `cpumask` = 3 → CPU 0,1). 바쁜 워크큐 전체 상태는 SysRq `t`(`echo t > /proc/sysrq-trigger`, M4)가 태스크 목록과 함께 "Showing busy workqueues and worker pools"로 찍어 주고, 커널 소스의 `tools/workqueue/wq_dump.py`(drgn)도 있다. workqueue 지연 측정은 ftrace의 `workqueue:*` tracepoint(M5)로 한다.

```text
blkcg_punt_bio
cpumask
cpumask_isolated
cpumask_requested
power
uevent
writeback
3
```

---

## 10. 커널 안에서는 무슨 일이 일어나나

### 10.1 모든 진단 파일은 "읽을 때 계산되는" show 함수다

이 파일들은 디스크에 저장된 게 아니다. `cat`이 `read()`를 부르면 VFS가 procfs/sysfs/debugfs의 `file_operations`로 들어가고, 대부분 **seq_file**(`seq_printf`로 한 줄씩 만드는 헬퍼)의 `show` 콜백이 그 순간의 커널 자료구조를 훑어 텍스트를 만든다. 그래서 값은 항상 "읽은 순간"의 스냅샷이고, 큰 표(`slabinfo`, `clk_summary`)를 읽는 동안 내부 lock을 잠깐 잡는다.

```svg
<svg viewBox="0 0 680 322" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="150" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="45" text-anchor="middle" font-size="13">cat /proc/xxx</text>
  <rect x="190" y="20" width="150" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="265" y="45" text-anchor="middle" font-size="13">vfs_read()</text>
  <rect x="370" y="20" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="520" y="45" text-anchor="middle" font-size="13">procfs · sysfs(kernfs) · debugfs</text>
  <line x1="160" y1="40" x2="188" y2="40" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="340" y1="40" x2="368" y2="40" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="520" y1="60" x2="520" y2="88" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="370" y="90" width="300" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="520" y="112" text-anchor="middle" font-size="12">seq_file show 콜백 → seq_printf()</text>
  <line x1="520" y1="124" x2="520" y2="150" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="10" y="152" width="660" height="160" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="174" font-size="12">interrupts → show_interrupts() : irq_desc 배열 + desc→kstat_irqs (per-CPU 카운트)</text>
  <text x="20" y="196" font-size="12">iomem → r_show() : iomem_resource 트리 (CAP_SYS_ADMIN 없으면 0으로)</text>
  <text x="20" y="218" font-size="12">meminfo → meminfo_proc_show() : vm_stat 카운터 · totalram_pages()</text>
  <text x="20" y="240" font-size="12">slabinfo → slab_show() : slab_caches 리스트 · devices_deferred → deferred_devs_show()</text>
  <text x="20" y="262" font-size="12">clk_summary → clk_summary_show() · gpio → gpiolib_seq_show()</text>
  <text x="20" y="284" font-size="12">regmap/&lt;dev&gt;/registers → regmap_map_read_file()</text>
  <text x="20" y="306" font-size="12">&lt;pid&gt;/stack → proc_pid_stack() : stack_trace_save_tsk() (root 전용)</text>
</svg>
```
그림 7 — 진단 파일 하나를 읽을 때의 경로와, 이 노트에서 쓴 파일들의 show 함수(6.18 소스에서 확인한 이름).

| 파일 | show 함수 (6.18) | 읽는 자료구조 | 드라이버가 영향을 주는 지점 |
|---|---|---|---|
| `/proc/interrupts` | `show_interrupts()` (`kernel/irq/proc.c`) | `irq_desc`, `irq_desc->action->name` | `request_irq()`의 이름 인자가 마지막 칸 |
| `/proc/iomem` | `r_show()` (`kernel/resource.c`) | `iomem_resource` 트리 | `devm_ioremap_resource()` → `request_mem_region()` → 안쪽 줄 |
| `/proc/meminfo` | `meminfo_proc_show()` (`fs/proc/meminfo.c`) | 전역 vm 통계 카운터 | slab·vmalloc·page 할당이 각 카운터를 움직임 |
| `/proc/slabinfo` | `slab_show()` (`mm/slab_common.c`) | `slab_caches` 리스트 | `kmem_cache_create()`의 이름이 첫 칸 |
| `devices_deferred` | `deferred_devs_show()` (`drivers/base/dd.c`) | `deferred_probe_pending_list` | `dev_err_probe()`가 `deferred_probe_reason`을 채움 |
| `clk_summary` | `clk_summary_show()` (`drivers/clk/clk.c`) | clk_core 트리 | `devm_clk_get()`의 con_id가 consumer id |
| `regulator_summary` | `regulator_summary_show()` (`drivers/regulator/core.c`) | regulator_dev + 소비자 | `devm_regulator_get()` |
| `pinmux-pins` | `pinmux_pins_show()` (`drivers/pinctrl/pinmux.c`) | pin_desc의 mux_owner | DT `pinctrl-0` (probe 직전 driver core가 적용) |
| debugfs `gpio` | `gpiolib_seq_show()` (`drivers/gpio/gpiolib.c`) | gpio_device · gpio_desc 라벨 | `gpiod_get()`의 con_id가 라벨 |
| regmap `registers` | `regmap_map_read_file()` (`drivers/base/regmap/regmap-debugfs.c`) | regmap(캐시 또는 버스 읽기) | `regmap_config.name`·`max_register` |
| `/proc/<pid>/stack` | `proc_pid_stack()` (`fs/proc/base.c`) | 잠든 태스크의 저장된 레지스터로 unwind | 드라이버의 `wait_event*` 위치 |

### 10.2 드라이버를 쓸 때 이 지식이 다시 나오는 곳

- **이름을 잘 붙여라.** `/proc/interrupts`의 action 이름(`devm_request_irq`의 `devname`), slab 캐시 이름, regmap 이름, GPIO con_id, clk con_id가 모두 진단 화면의 "누가"가 된다. `"irq"` 같은 이름을 주면 현장에서 아무도 못 찾는다. 장치가 여러 개면 `dev_name(dev)`를 쓰자.
- **`dev_err_probe()`를 써라.** defer 이유가 `devices_deferred`에 남고, 나머지 에러는 장치 이름과 함께 찍힌다(G3).
- **`-ENODEV`는 "내 장치 아님"일 때만.** 칩 ID 불일치처럼 진짜 HW 문제면 `-EIO`나 `-ENXIO` 대신 `dev_err`를 직접 찍고 반환하자 — 안 그러면 §1.5처럼 아무 로그 없이 사라진다. (커뮤니티 관행은 드라이버마다 다르니 리뷰어 의견을 따른다.)
- **진단 정보는 debugfs에.** 레지스터 덤프·통계는 `debugfs_create_regset32()`나 regmap debugfs로(G8). `/proc`에 새 파일을 만드는 건 금지에 가깝다.
- **잠드는 곳은 interruptible로.** 예제 21처럼 `S` 상태면 앱을 Ctrl-C로 뺄 수 있다. `D`로 자는 경로는 hung task 경고의 원인이 된다(F3).

---

## 11. Embedded 실무에서 다시 보기

### 11.1 장면 1 — "IMU가 `/dev/iio:device0`으로 안 나와요" (센서 키트)

그림 1 순서를 그대로 밟는다. 예를 들어 센서 키트의 BMI088이 I2C2 0x68에 붙어 있다면:

```sh
cat /proc/device-tree/soc/i2c@30a30000/imu@68/status 2>/dev/null; echo     # ① 없으면 okay
ls /sys/bus/i2c/devices/ | grep 0068                                       # ② 2-0068 이 있나
ls -l /sys/bus/i2c/devices/2-0068/driver                                   # ③ bind?
cat /sys/kernel/debug/devices_deferred                                     # ④ 이유는?
dmesg | grep -iE "2-0068|bmi088"                                           # ⑤ probe 에러
grep -A3 "i2c2" /sys/kernel/debug/clk/clk_summary                          # ⑥ 클럭
cat /sys/kernel/debug/regulator/regulator_summary | grep -B1 -A2 2-0068    # ⑥ VDD/VDDIO
grep -E "i2c2|imu" /sys/kernel/debug/pinctrl/*/pinmux-pins                 # ⑥ SCL/SDA 핀
grep -i bmi /proc/interrupts                                               # ⑦ DRDY IRQ 카운트
```

(가이드 — 실행 안 함. 경로·주소는 i.MX8MP 스타일의 예시다.) ②에서 `2-0068`이 없으면 I2C 컨트롤러(`30a30000.i2c`) 자체가 bind됐는지로 한 단계 올라간다. ⑤에서 `-121`(EREMOTEIO) 같은 I2C 에러가 보이면 NACK — 주소·전원·핀먹스 문제다. 컨트롤러 드라이버에 따라 NACK가 `-ENXIO`(-6)로 올라오기도 하는데, 센서 드라이버가 그 값을 probe에서 그대로 반환하면 §1.5처럼 **로그 없이** 사라진다. 그다음은 Don의 LA로 SCL/SDA를 본다(H5, M11).

### 11.2 장면 2 — 필드에서 "며칠 지나면 메모리가 줄어요"

예제 14의 방법을 자동화한다. 장비에 아래 스크립트를 cron/systemd timer로 돌려 1시간마다 저장하고, 두 스냅샷을 diff한다. 증가하는 캐시가 보이면 그 이름(드라이버가 붙인 이름 또는 `kmalloc-N`)이 첫 단서다.

```sh
#!/bin/sh
# memsnap.sh — 메모리 스냅샷 (가이드: 랩에서는 개별 명령만 실행했다)
D=/var/log/memsnap; mkdir -p $D; T=$(date +%Y%m%d-%H%M)
{ echo "== meminfo"; cat /proc/meminfo
  echo "== slabinfo"; tail -n +3 /proc/slabinfo | sort -k3 -n -r | head -40
  echo "== buddyinfo"; cat /proc/buddyinfo
  echo "== vmalloc"; awk '{s[$3]+=$2} END {for (k in s) print s[k], k}' /proc/vmallocinfo | sort -n -r | head -15
} > $D/$T.txt
```

`kmalloc-N`이 범인이면 주인을 모르니, 디버그 이미지에서 kmemleak나 `slub_debug=U,kmalloc-512`로 할당 호출 위치(`/sys/kernel/slab/kmalloc-512/alloc_calls`)를 본다(M8).

### 11.3 장면 3 — "카메라를 켜면 가끔 프레임이 튄다"

V4L2 캡처 IRQ와 네트워크 RX IRQ가 같은 CPU에 몰려 있으면 지연이 생긴다. `/proc/interrupts`를 1초 간격으로 두 번 찍어 CPU별 증가량을 비교하고, 카메라 IRQ를 다른 코어로 옮긴다(`smp_affinity_list`). managed IRQ(NVMe·virtio-blk 큐)는 예제 8처럼 거부되니 옮길 대상에서 뺀다. 그 뒤 지연 측정은 ftrace `irqsoff`/`timerlat`(M5, N 모듈).

### 11.4 장면 4 — bring-up 보고서에 붙일 "보드 상태 한 장"

새 보드 첫 부팅에서 아래를 한 파일로 떠 두면, 이후 커널·DT를 바꿀 때마다 diff로 회귀를 잡을 수 있다: `uname -a`, `/proc/cmdline`, `dtc -I fs /proc/device-tree`(dtc가 있으면), `/proc/iomem`(root), `/proc/interrupts`, `devices_deferred`, `clk_summary`, `regulator_summary`, `pinmux-pins`, debugfs `gpio`, `lsmod`, `/proc/sys/kernel/tainted`, `dmesg`. SSD 시절 "샘플별 레지스터 덤프 기록"과 같은 습관이다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `/proc/iomem`을 일반 사용자로 봄 | 주소가 전부 `00000000-00000000` | `r_show()`가 CAP_SYS_ADMIN 없으면 가림 | sudo로 본다 |
| debugfs가 안 보임 | `/sys/kernel/debug`가 빈 디렉터리 | 마운트 안 됨 또는 `CONFIG_DEBUG_FS=n` | `mount -t debugfs none /sys/kernel/debug`, 양산 이미지면 디버그 빌드 사용 |
| `devices_deferred`의 이유 칸이 빔 | 장치 이름만 있음 | 드라이버가 `return -EPROBE_DEFER`를 그대로 씀 | `dev_err_probe()`로 교체 |
| probe가 `-ENODEV`를 반환 | 로그 없이 unbound | driver core가 dbg로만 찍음 | dd.c dyndbg 켜기, 드라이버에 `dev_err` 추가 |
| managed IRQ affinity 변경 | `write error: Operation not permitted` | 블록 mq·NVMe 큐 IRQ는 커널이 관리 | 다른 IRQ를 옮기거나 `irqaffinity=`·isolcpus 설계 |
| smp_affinity를 0-1로 주고 분산 기대 | CPU0만 카운트 증가 | GIC는 한 CPU에만 배달(effective_affinity) | `effective_affinity_list` 확인, 필요하면 irqbalance |
| debugfs `gpio`의 값과 코드의 값이 반대 | 리셋이 안 풀림 | debugfs는 물리 레벨, 코드는 논리값(active-low) | DT/lookup의 `GPIO_ACTIVE_LOW`와 `gpioinfo`의 active-low 확인 |
| 전용 slab 캐시가 slabinfo에 없음 | 이름으로 grep 실패 | SLUB 캐시 병합 | `SLAB_NO_MERGE` 또는 `slab_nomerge` |
| vmallocinfo에서 내 모듈 이름을 못 찾음 | 호출자 칸이 맨 주소 | `__init` 함수에서 할당 → init 심볼 해제 | 크기·`pages=`로 찾기, probe에서 할당 |
| 요약표만 믿음 | 표는 정상인데 HW 무응답 | 표는 "드라이버가 믿는 상태" | scope·LA·레지스터 직접 읽기로 교차 확인 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "The board boots, but /dev/xxx never appears. Where do you look?"

**A.** 체인을 순서대로 끊어 본다. DT 노드가 있고 okay인지(`/proc/device-tree`), 버스 아래 장치 객체가 생겼는지(`/sys/bus/*/devices`), `driver` 링크가 있는지. 없으면 `devices_deferred`에 이유가 있는지, dmesg에 `probe ... failed with error`가 있는지. bind는 됐는데 노드가 없으면 클래스 등록이나 udev 쪽이고, 동작이 이상하면 clk·regulator·pinmux 요약과 `/proc/interrupts`를 본다. `-ENODEV`는 조용히 실패하니 dd.c dynamic debug도 켠다.

> I walk the chain in order. Is the DT node present and okay under /proc/device-tree? Did the bus create a device object under /sys/bus/BUS/devices? Is there a driver symlink? If not, I check debugfs devices_deferred for a recorded reason, and dmesg for "probe with driver X failed with error N". If it's bound but there's no device node, it's the class registration or udev. If it's bound but misbehaving, I go to clk_summary, regulator_summary, pinmux-pins, and the interrupt counts. And since -ENODEV from probe is silent by default, I turn on dynamic debug for drivers/base/dd.c.

**꼬리 질문**: "What does the driver symlink tell you that dmesg doesn't?" → 현재 bind 상태 그 자체(로그는 과거 기록, 링크는 지금 상태).

**Q.** "What is -EPROBE_DEFER and how do you debug a device stuck in deferred probe?"

**A.** probe에 필요한 공급자(clk·regulator·GPIO·PHY)가 아직 없다는 뜻이고, driver core가 목록에 넣었다가 다른 드라이버가 bind될 때마다 다시 시도한다. `devices_deferred`에 `dev_err_probe()`가 남긴 이유가 나오고, 부팅 후 timeout(기본 10초)이 지나면 `deferred probe pending:` 경고가 찍힌다. 공급자 드라이버가 빠졌는지, 공급자 자체가 defer 중인지(연쇄)를 따라간다. fw_devlink가 있으면 `/sys/class/devlink`에서 공급자-소비자 링크 상태도 본다.

> It means a supplier the probe needs isn't there yet, so the driver core parks the device and retries every time some other driver binds. debugfs devices_deferred shows the reason recorded by dev_err_probe, and after the deferred probe timeout — ten seconds by default — the kernel prints "deferred probe pending" for whatever is still waiting. Then I follow the chain: is the supplier's driver missing from the build, or is the supplier itself deferred? With fw_devlink I also look at the device links in /sys/class/devlink.

**Q.** "How do you confirm that an interrupt is actually arriving?"

**A.** `/proc/interrupts`에서 그 action 이름의 줄을 찾아, 장치를 한 번 자극하기 전후로 카운트를 비교한다. 0에서 안 움직이면 핀먹스·트리거 타입·컨트롤러 설정, 폭주하면 레벨 인터럽트를 안 지우는 핸들러다. `/sys/kernel/irq/N/type`으로 실제 트리거를, `effective_affinity`로 실제 받는 CPU를, `spurious`로 처리 안 된 수를 본다. 마지막엔 scope로 핀을 직접 본다.

> I find the line with the handler's name in /proc/interrupts and compare the counts before and after poking the device. If it stays at zero, it's pinmux, trigger type, or the controller setup; if it explodes, it's usually a level interrupt the handler never clears. /sys/kernel/irq/N/type shows the trigger actually programmed, effective_affinity shows which CPU really receives it, and /proc/irq/N/spurious counts unhandled ones. If the counters disagree with my expectation, I put a scope on the pin.

**Q.** "Why is MemTotal smaller than the RAM on the board?"

**A.** MemTotal는 버디 할당자가 관리하는 페이지 합이다. 커널 코드·데이터, memblock 예약, `no-map` carve-out은 빠진다. 실습에서는 1 GiB 중 83 MB가 빠졌고, `zoneinfo`의 managed 페이지 × 4 KiB가 MemTotal과 정확히 같았다. 차이는 `/proc/iomem`의 reserved 줄과 debugfs memblock으로 설명한다.

> MemTotal is the number of pages the buddy allocator manages, so the kernel image, memblock reservations and no-map carve-outs are excluded. On my QEMU board, 83 MB of 1 GiB was missing, and zoneinfo's managed pages times four kilobytes matched MemTotal exactly. The reserved entries in /proc/iomem and debugfs memblock account for the difference.

**Q.** "How would you detect a memory leak in a kernel driver on a deployed device?"

**A.** 먼저 `/proc/meminfo`의 SUnreclaim과 `/proc/slabinfo`를 주기적으로 찍어 diff한다. 늘어나는 캐시 이름이 단서다 — 드라이버 전용 캐시면 바로 범인이고, `kmalloc-N`이면 디버그 빌드에서 kmemleak나 `slub_debug=U`의 alloc_calls로 호출 위치를 찾는다. 실습에서 kmalloc-512가 1000개씩 늘고 모듈을 내려도 그대로인 걸 확인했다.

> First, periodic snapshots of SUnreclaim in /proc/meminfo and of /proc/slabinfo, then diff them. The growing cache's name is the clue: if it's the driver's own cache, that's the culprit; if it's a generic kmalloc-N cache, I reproduce on a debug build with kmemleak or slub_debug=U and read alloc_calls to get the call site. In my lab, kmalloc-512 grew by exactly a thousand objects per trigger and stayed there after the module was unloaded.

**Q.** "An application hangs in read() on your device. How do you find where?"

**A.** `/proc/<pid>/status`의 State(S냐 D냐), `wchan`, 그리고 root로 `/proc/<pid>/stack`을 본다. 맨 위 줄이 `[모듈명]`이 붙은 드라이버 함수와 오프셋이라 `faddr2line`으로 소스 줄까지 간다. 그 wait 조건을 깨우는 쪽(IRQ 핸들러가 `wake_up`을 안 부르는지)을 `/proc/interrupts`와 ftrace로 확인한다.

> I check State in /proc/PID/status — S or D — then wchan, and as root /proc/PID/stack. The top frame is the driver function with the module name and an offset, so faddr2line gets me the source line. Then I check the waker side — does the IRQ handler ever call wake_up? — using /proc/interrupts and ftrace.

**꼬리 질문**: "S vs D?" → S는 시그널로 깰 수 있고 D는 못 깬다. D가 120초 넘으면 hung task 경고.

**Q.** "debugfs vs sysfs vs procfs — which do you use for what?"

**A.** sysfs는 디바이스 모델과 안정 ABI(값 하나 = 파일 하나), procfs는 프로세스와 오래된 전역 통계, debugfs는 개발자용으로 형식 약속이 없다. bring-up 때 가장 유용한 clk·regulator·pinctrl·gpio·regmap 정보는 debugfs에 있어서, 양산 이미지에서도 디버그 빌드로 쉽게 바꿀 수 있게 만들어 둔다.

> sysfs is the device model and a stable ABI, one value per file. procfs is per-process info plus legacy global statistics. debugfs is for developers with no format guarantees. The most useful bring-up views — clocks, regulators, pinctrl, GPIO, regmap — live in debugfs, so I make sure we can easily boot a debug build on production hardware.

---

## 14. 직접 해보기

1. (개념) `/proc/iomem`에서 어떤 장치의 바깥 줄은 있는데 안쪽 줄이 없다. 무슨 뜻인가?
   정답/힌트: DT·버스가 영역을 선언했지만 드라이버가 `request_mem_region()`(=`devm_ioremap_resource`)으로 잡지 않았다 — 드라이버가 bind 안 됐거나 `ioremap`만 직접 썼다.
2. (관찰) 예제 3에서 gpio-sim 칩을 만들기 전에 `echo m2_cons > /sys/bus/platform/drivers/m2_cons/bind`를 하면 어떤 오류 문자열이 나오나?
   정답/힌트: 직접 확인할 것(이 노트에서는 실행하지 않았다). `-517`(EPROBE_DEFER)은 유저 errno 표에 없는 커널 내부 값이라 셸에서 `Unknown error 517`처럼 보일 수 있다는 점을 생각해 본다.
3. (코드) `m2_defer`의 `GPIO_ACTIVE_LOW`를 `GPIO_ACTIVE_HIGH`로 바꾸고 다시 빌드해, debugfs `gpio`의 `reset` 줄이 `output-low`로 바뀌는지 확인하라.
   정답/힌트: 논리 0 요청 + active-high = 물리 low. debugfs는 물리 레벨을 보여 준다.
4. (관찰) 예제 7의 `IRQF_TRIGGER_RISING`을 `IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING`으로 바꾸면 pull-up/pull-down 세 번에 카운트가 몇이 되나? `/sys/kernel/irq/N/type`은?
   정답/힌트: 6, `type: edge`(양 에지도 edge로 표시).
5. (코드) `m2_slab`의 할당을 `module_init`이 아닌 debugfs 쓰기 핸들러로 옮기고 `/proc/vmallocinfo`의 호출자 칸이 심볼로 보이는지 확인하라.
   정답/힌트: 비-init 함수면 `leak_write+0x.../0x... [m2_slab]` 형태로 나온다.
6. (관찰) 같은 게스트를 `slab_nomerge` 없이, `SLAB_NO_MERGE`도 빼고 빌드해서 `m2_obj`가 slabinfo에 나오는지, `/sys/kernel/slab/m2_obj`가 다른 캐시를 가리키는 링크인지 확인하라.
   정답/힌트: 병합되면 `/sys/kernel/slab/m2_obj`는 `:0000192`나 `:a-0000192` 같은 이름의 공용 캐시로 가는 심볼릭 링크가 된다(`aliases` 파일 값도 확인).
7. (스크립트) §11.4의 "보드 상태 한 장" 스크립트를 짜서 trace 게스트에서 두 번(모듈 로드 전/후) 찍고 diff하라. 어느 파일이 바뀌었나?
   정답/힌트: interrupts(카운트·새 줄), slabinfo, meminfo, debugfs gpio, lsmod, tainted, iomem(변화 없음 — 우리 모듈은 MMIO를 잡지 않음).

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| deferred probe | 지연 probe | 공급자가 없어 `-EPROBE_DEFER`를 낸 장치를 나중에 다시 시도하는 driver core 기능 |
| `devices_deferred` | debugfs 파일 | 지금 대기 중인 장치와 이유 문자열 |
| `dev_err_probe()` | probe 에러 헬퍼 | defer면 이유 저장, 아니면 에러 출력 후 같은 값 반환 |
| fw_devlink | firmware device link | DT 참조로 공급자-소비자 device link를 자동 생성 |
| `of_node` | DT 노드 링크 | 장치가 만들어진 DT 노드를 가리키는 sysfs 링크 |
| hwirq · virq | 하드웨어/리눅스 IRQ 번호 | 컨트롤러 내부 번호와 `/proc/interrupts`의 번호 |
| effective affinity | 실제 배달 CPU | 허용 마스크 중 컨트롤러가 실제로 보내는 CPU |
| managed IRQ | 커널 관리 IRQ | 큐별로 CPU가 고정되어 유저가 affinity를 못 바꾸는 IRQ |
| resource tree | 자원 트리 | `/proc/iomem`·`ioports`의 원본, `struct resource` 계층 |
| memblock | 초기 메모리 관리자 | 버디 할당자 이전에 쓰는 메모리·예약 영역 목록 |
| MemTotal | 관리 메모리 총량 | 버디 할당자가 관리하는 페이지 합(물리 RAM보다 작음) |
| SUnreclaim | 회수 불가 slab | 커널 객체 메모리, 드라이버 누수가 여기 쌓임 |
| slab cache | 객체 캐시 | 같은 크기 객체를 모아 두는 할당자 단위 (`kmalloc-512`, `m2_obj`) |
| buddyinfo | 버디 빈 블록 표 | order별 빈 연속 블록 수 — 단편화 지표 |
| watermark | 여유 메모리 기준선 | zone의 min/low/high, kswapd·direct reclaim 동작 기준 |
| regmap debugfs | 레지스터 덤프 | `/sys/kernel/debug/regmap/<dev>/registers` |
| runtime PM | 런타임 전원 관리 | 장치 단위 suspend/resume, `power/runtime_status` |
| taint | 오염 플래그 | 커널 신뢰성에 영향을 준 사건 기록 (O·E·W·D…) |
| wchan | wait channel | 태스크가 잠든 커널 함수 이름 |
| seq_file | 순차 파일 헬퍼 | proc/debugfs 텍스트 파일을 show 콜백으로 만드는 커널 API |

---

## 16. 요약 & 체크리스트

bring-up 진단은 "어디를 볼지"를 아는 싸움이다. `/sys`의 장치·드라이버 링크로 bind 상태를, debugfs `devices_deferred`와 dmesg의 probe 에러로 실패 이유를, `of_node`와 `/proc/device-tree`로 DT 출처를 확인한다. 그다음 debugfs의 clk·regulator·pinctrl·gpio·regmap 요약으로 자원을, `/proc/interrupts`와 `/proc/irq`·`/sys/kernel/irq`로 인터럽트를 본다. 메모리는 `meminfo`의 SUnreclaim과 `slabinfo`를 맞춰 보고, `buddyinfo`·`zoneinfo`로 단편화와 여유를, `vmallocinfo`로 큰 버퍼를 본다. 멈춘 앱은 `/proc/<pid>/stack`이 드라이버의 잠든 줄을 알려 준다. 모든 표는 커널이 믿는 상태라서, 이상하면 HW를 직접 재서 교차 확인한다.

- [ ] "부팅은 되는데 장치 노드가 없다"를 7단계(DT → 장치 → bind → defer → 에러 → 자원 → IRQ)로 설명할 수 있다
- [ ] `devices_deferred`의 형식과, 이유 칸이 비는 경우를 설명할 수 있다
- [ ] probe가 `-ENODEV`를 돌려주면 왜 로그가 없는지 말하고 dd.c dyndbg로 확인할 수 있다
- [ ] sysfs `bind`/`unbind`로 probe를 반복 시험할 수 있다
- [ ] `of_node` 링크와 `reg` 바이트를 `/proc/iomem`과 맞춰 볼 수 있다
- [ ] `/proc/interrupts`의 각 열과 hwirq·virq·irq_domain 관계를 설명할 수 있다
- [ ] smp_affinity·effective_affinity·managed IRQ 차이를 실측 예로 말할 수 있다
- [ ] `/proc/iomem`이 일반 사용자에게 0으로 보이는 이유를 말할 수 있다
- [ ] MemTotal과 물리 RAM의 차이를 zoneinfo·memblock으로 설명할 수 있다
- [ ] slabinfo·meminfo로 드라이버 누수를 숫자로 보여 줄 수 있다
- [ ] clk_summary·regulator_summary·pinmux-pins·debugfs gpio·regmap registers가 각각 무엇을 답하는지 말할 수 있다
- [ ] `/proc/<pid>/stack`·`wchan`·`status`로 드라이버에서 잠든 태스크의 위치를 찾을 수 있다

## 참고 자료

- docs.kernel.org — "The /proc Filesystem" (`Documentation/filesystems/proc.rst`): meminfo·interrupts·iomem·smaps 필드 정의
- docs.kernel.org — "Tainted kernels" (`Documentation/admin-guide/tainted-kernels.rst`)
- docs.kernel.org — "Driver Basics"·"Device links" (`Documentation/driver-api/device_link.rst`), `fw_devlink`·`deferred_probe_timeout`는 `Documentation/admin-guide/kernel-parameters.txt`
- docs.kernel.org — "SMP IRQ affinity" (`Documentation/core-api/irq/irq-affinity.rst`)
- docs.kernel.org — "Short users guide for SLUB" (`Documentation/mm/slub.rst`)
- docs.kernel.org — "GPIO Testing Driver" (gpio-sim, `Documentation/admin-guide/gpio/gpio-sim.rst`)
- 소스 (elixir.bootlin.com, v6.18): `drivers/base/dd.c`(deferred probe, really_probe), `kernel/irq/proc.c`, `kernel/resource.c`, `fs/proc/meminfo.c`, `fs/proc/base.c`, `mm/slab_common.c`, `drivers/clk/clk.c`, `drivers/gpio/gpiolib.c`, `drivers/base/regmap/regmap-debugfs.c`, `drivers/pinctrl/pinmux.c`
- man7.org — `proc(5)`, `slabtop(1)`, `setpriv(1)`
- Bootlin — "Linux kernel and driver development" 교육 슬라이드(디버깅·디바이스 모델 장)
