# F7. 지연 작업 — softirq · tasklet · workqueue · BH workqueue · kthread, 언제 무엇을

> **이 노트를 다 읽으면**: "ISR에서 I2C 센서를 읽으면 안 되는 이유와 대신 어디서 읽는지"를 커널 소스 근거로 설명할 수 있다 · softirq/tasklet/BH workqueue/workqueue/kthread/threaded IRQ가 각각 어느 컨텍스트에서 돌고 잠들 수 있는지, 실제로 얼마나 늦게 도는지(직접 잰 숫자로) 말할 수 있다 · `alloc_workqueue` 플래그(`WQ_UNBOUND`/`WQ_HIGHPRI`/`WQ_MEM_RECLAIM`/`WQ_FREEZABLE`)와 `max_active`, ordered wq, `delayed_work`를 골라 쓸 수 있다 · rmmod 때 `cancel_work_sync`를 빼먹으면 어떻게 죽는지 보고, 올바른 teardown 순서를 코드로 짤 수 있다
> **왜 필요한가**: 면접에서 "Why can't you read an I2C sensor in the ISR, and where do you do it instead?"는 드라이버 직무의 단골 질문이고, 실무에서는 "센서 데이터가 가끔 수 ms 늦게 온다", "rmmod 하면 가끔 커널이 죽는다"가 전부 이 주제다
> **Don 기준 난이도**: ISR을 짧게 두고 나머지를 태스크로 넘기는 설계(플래그 + 메인 루프, PendSV, RTOS deferred interrupt handling)는 이미 몸에 배어 있다 / 새로 배울 것은 Linux가 그 "나머지"를 맡길 곳을 다섯 가지로 나눠 둔 이유, 각 곳의 컨텍스트 규칙, CMWQ의 워커 풀 구조, 그리고 모듈 언로드라는 bare-metal에 없던 수명 문제
> **선행 노트**: E4(커널 코드의 제약: 잠들면 안 되는 곳), F1(스케줄러·우선순위), F2(실행 컨텍스트), F3(잠자기와 깨우기·completion), F6(인터럽트 처리·threaded IRQ), D4(DT `interrupts`와 `irq_set_irqchip_state` 트리거 기법)

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어에서 NAND 완료 인터럽트를 처리하던 방식을 떠올려 보자. ISR은 상태 레지스터를 읽고, 완료 큐에 엔트리 하나를 넣고, 인터럽트를 ack 하고 끝냈다. ECC 결과 해석, 재시도 결정, 호스트 완료 큐 갱신 같은 "진짜 일"은 메인 루프나 RTOS 태스크가 했다. 이유는 두 가지였다.

- ISR이 길면 **다른 인터럽트가 늦어진다** (같은 우선순위 이하는 다 막힌다).
- ISR 안에서는 **기다릴 수 없다** (세마포어를 잡거나, 다른 버스 트랜잭션이 끝나길 기다리면 시스템 전체가 멈춘다).

Linux도 똑같은 이유로 인터럽트 처리를 둘로 쪼갠다. 인터럽트가 들어오자마자 도는 짧은 부분을 **top half**(= hardirq handler), 나중에 도는 나머지를 **bottom half**라고 부른다. bare-metal과 다른 점은 bottom half를 맡길 곳이 **여러 종류**라는 것이다. 맡기는 곳마다 "얼마나 빨리 도는가"와 "잠들 수 있는가"가 다르다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="20" font-size="12">빨리 실행 · 잠들 수 없음</text>
  <text x="660" y="20" text-anchor="end" font-size="12">늦을 수 있음 · 잠들 수 있음</text>
  <line x1="20" y1="32" x2="660" y2="32" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="12" y="46" width="124" height="130" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="74" y="68" text-anchor="middle" font-size="13">hardirq</text>
  <text x="74" y="88" text-anchor="middle" font-size="12">(top half)</text>
  <text x="74" y="112" text-anchor="middle" font-size="12">이 CPU IRQ 꺼짐</text>
  <text x="74" y="136" text-anchor="middle" font-size="12">sleep 불가</text>
  <text x="74" y="160" text-anchor="middle" font-size="12">수 µs로 끝낸다</text>
  <rect x="144" y="46" width="124" height="130" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="206" y="68" text-anchor="middle" font-size="13">softirq</text>
  <text x="206" y="88" text-anchor="middle" font-size="12">tasklet · BH wq</text>
  <text x="206" y="112" text-anchor="middle" font-size="12">irq_exit 직후</text>
  <text x="206" y="136" text-anchor="middle" font-size="12">sleep 불가</text>
  <text x="206" y="160" text-anchor="middle" font-size="12">CPU별 실행</text>
  <rect x="276" y="46" width="124" height="130" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="338" y="68" text-anchor="middle" font-size="13">threaded IRQ</text>
  <text x="338" y="88" text-anchor="middle" font-size="12">irq/NN-이름</text>
  <text x="338" y="112" text-anchor="middle" font-size="12">SCHED_FIFO 50</text>
  <text x="338" y="136" text-anchor="middle" font-size="12">sleep 가능</text>
  <text x="338" y="160" text-anchor="middle" font-size="12">IRQ 1개 전용</text>
  <rect x="408" y="46" width="124" height="130" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="470" y="68" text-anchor="middle" font-size="13">highpri wq</text>
  <text x="470" y="88" text-anchor="middle" font-size="12">kworker/N:MH</text>
  <text x="470" y="112" text-anchor="middle" font-size="12">nice -20</text>
  <text x="470" y="136" text-anchor="middle" font-size="12">sleep 가능</text>
  <text x="470" y="160" text-anchor="middle" font-size="12">공유 워커 풀</text>
  <rect x="540" y="46" width="124" height="130" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="602" y="68" text-anchor="middle" font-size="13">wq · kthread</text>
  <text x="602" y="88" text-anchor="middle" font-size="12">kworker/N:M</text>
  <text x="602" y="112" text-anchor="middle" font-size="12">nice 0</text>
  <text x="602" y="136" text-anchor="middle" font-size="12">sleep 가능</text>
  <text x="602" y="160" text-anchor="middle" font-size="12">일반 스레드</text>
  <text x="74" y="200" text-anchor="middle" font-size="12">(기준점 0)</text>
  <text x="206" y="200" text-anchor="middle" font-size="12">53 / 93 µs</text>
  <text x="338" y="200" text-anchor="middle" font-size="12">178 µs</text>
  <text x="470" y="200" text-anchor="middle" font-size="12">249 µs</text>
  <text x="602" y="200" text-anchor="middle" font-size="12">286 / 382 µs</text>
  <text x="340" y="226" text-anchor="middle" font-size="12">위 숫자: IRQ → bottom half 시작까지 p50, 유휴 상태, QEMU TCG 실측 (9절)</text>
  <line x1="12" y1="244" x2="268" y2="244" stroke="#d0564a" stroke-width="2"/>
  <text x="140" y="264" text-anchor="middle" font-size="12">atomic 컨텍스트 — in_interrupt() 참</text>
  <line x1="276" y1="244" x2="664" y2="244" stroke="#4a7bd0" stroke-width="2"/>
  <text x="470" y="264" text-anchor="middle" font-size="12">process 컨텍스트 — 스케줄러가 고르는 스레드</text>
  <text x="340" y="302" text-anchor="middle" font-size="12">bare-metal 비유: ISR → PendSV(최저 우선순위 예외) → RTOS 태스크</text>
</svg>
```
그림 1 — 미뤄 둔 일을 맡길 수 있는 곳의 스펙트럼. 왼쪽으로 갈수록 빨리 돌지만 잠들 수 없고, 오른쪽으로 갈수록 스케줄러를 거치느라 늦을 수 있지만 잠들 수 있다

이 노트의 핵심 질문은 하나다. **"이 일을 어디에 맡겨야 하나?"** 답을 고르는 축은 세 개다.

1. **잠들어야 하나?** I2C/SPI 전송, `mutex_lock`, `kmalloc(GFP_KERNEL)`, `msleep` 중 하나라도 하면 잠든다. 그러면 오른쪽 세 칸(threaded IRQ, workqueue, kthread)만 가능하다.
2. **얼마나 빨리 돌아야 하나?** 네트워크 패킷이나 타이머처럼 µs 단위로 쏟아지는 일은 softirq가 맡는다. 센서 한 샘플(수백 Hz)은 threaded IRQ나 highpri workqueue로 충분하다.
3. **어느 CPU에서 돌아야 하나?** 큐잉한 CPU에서 돌면 캐시가 따뜻하다(per-CPU). 아무 CPU나 상관없으면 스케줄러가 한가한 CPU에 보낸다(unbound).

### "ISR에서 I2C 센서를 읽으면 왜 안 되나" — 짧은 답

센서 키트의 IMU가 "데이터 준비됨" 인터럽트를 GPIO로 올린다고 하자. ISR에서 바로 `i2c_transfer()`로 14바이트를 읽고 싶어진다. 안 되는 이유는 커널 소스에 그대로 있다.

- `i2c_transfer()`는 먼저 버스 락을 잡는다. 이 락은 **rt_mutex**다(`drivers/i2c/i2c-core-base.c`의 `rt_mutex_lock_nested(&adapter->bus_lock, ...)`). mutex는 잠들 수 있는 락이라 hardirq에서 잡으면 안 된다(E4, F4).
- 락을 잡은 뒤 컨트롤러 드라이버는 전송을 시작하고 **완료 인터럽트를 기다리며 잠든다**(보통 `wait_for_completion_timeout`, F3). 400 kHz I2C로 14바이트면 바이트당 9클럭이니 전송 자체만 300 µs가 넘는다. 그동안 hardirq를 붙잡고 있으면 이 CPU의 다른 인터럽트가 전부 밀린다.
- 게다가 그 "완료 인터럽트"는 I2C 컨트롤러의 인터럽트다. 지금 IMU 인터럽트 핸들러 안에서 기다리면, 같은 CPU에서는 그 완료 인터럽트가 들어올 수조차 없다 → 교착.
- 커널에는 예외 경로(`i2c_in_atomic_xfer_mode()`)가 있지만 소스 주석이 "only allow atomic transfers for very late communication, e.g. to access a PMIC when powering down"이라고 못 박는다. `system_state > SYSTEM_RUNNING`(종료 중)일 때만 켜진다.

그래서 답은 "ISR(top half)에서는 타임스탬프만 찍고 IRQ를 ack 한 뒤, **threaded IRQ**(또는 workqueue)에서 I2C를 읽는다"이다. 이 노트는 그 답을 실제로 재고 부숴 보면서 확인한다.

---

## 1. 왜 미루나 — 두 가지 이유를 정확히

### 1.1 이유 1: hardirq는 짧아야 한다

hardirq handler가 도는 동안 무슨 일이 일어나는지 F6에서 봤다. arm64 Linux에서 핸들러는 **그 CPU의 IRQ가 꺼진 상태**(PSTATE.I = 1)로 돈다. 즉 핸들러가 100 µs 걸리면 그 CPU의 다른 모든 인터럽트(타이머 틱 포함)가 최대 100 µs 늦어진다. bare-metal에서 우선순위 중첩(nesting)으로 해결하던 문제를 Linux는 "핸들러를 짧게 만들고 나머지를 미룬다"로 해결한다.

### 1.2 이유 2: 잠들어야 하는 일이 있다

"잠든다"는 것은 `schedule()`을 불러 CPU를 다른 태스크에게 넘긴다는 뜻이다(F3). 그런데 hardirq와 softirq는 **태스크가 아니다**. 어떤 태스크를 잠깐 빌려서(그 태스크의 스택 대신 IRQ 스택에서) 돌고 있을 뿐이다. 거기서 `schedule()`을 부르면 "빌려 쓴 태스크"가 잠들어 버리고, 빌려 쓴 상태(softirq 처리 중 표시, 락 등)가 꼬인다. 5절에서 실제로 이렇게 커널을 죽여 본다.

### 1.3 컨텍스트 정리표

| 어디서 도나 | 누구의 몸을 빌리나 | 잠들기 | 선점 | 확인 함수 |
|---|---|---|---|---|
| hardirq (top half) | 아무 태스크 (IRQ 스택) | 불가 | 불가, 이 CPU IRQ 꺼짐 | `in_hardirq()` |
| softirq (tasklet, BH wq 포함) | 아무 태스크 또는 `ksoftirqd/N` | 불가 | 불가, IRQ는 켜짐 | `in_serving_softirq()` |
| threaded IRQ | 자기 스레드 `irq/NN-name` | 가능 | 가능 (SCHED_FIFO 50) | `in_task()` |
| workqueue | 공유 스레드 `kworker/...` | 가능 | 가능 | `in_task()` |
| kthread | 자기 스레드 | 가능 | 가능 | `in_task()` |

`in_interrupt()`는 hardirq·softirq·NMI 중 하나라도 해당하면 참이다. 다만 커널 문서는 드라이버가 이런 함수로 "지금 어디냐"를 분기하는 것을 권하지 않는다. 이 노트에서는 관찰용으로만 쓴다.

---

## 2. 실험 장치 — 같은 IRQ를 8가지 방법으로 미루는 모듈

이 노트의 실험은 전부 하나의 모듈 `f7_defer`로 한다. 아이디어는 D4에서 쓴 것과 같다. DT에 가짜 장치 노드를 하나 만들어 안 쓰는 GIC SPI를 배정하고, 모듈 안에서 `irq_set_irqchip_state(irq, IRQCHIP_STATE_PENDING, true)`로 GIC에 pending 비트를 직접 세워 진짜 인터럽트를 일으킨다. 그러면 하드웨어 없이도 "진짜 GIC → 진짜 hardirq 경로"를 탄다.

- **top half**: `ktime_get_ns()`로 시각을 찍고, 모드에 따라 bottom half를 예약한다.
- **bottom half**: 다시 시각을 찍어 차이(IRQ → bottom half 지연)를 기록하고, 첫 번째 실행 때 컨텍스트를 출력한다.
- **발생기** `f7_gen` kthread: pending을 세우고, bottom half가 끝났다는 completion을 기다리고, 2~3 ms 쉰 뒤 반복한다(기본 200번).

실행 환경은 공용 랩의 **디버그 커널**이다.

```sh
limactl shell elinux -- bash -lc 'uname -r; nproc'
```

```text
7.0.0-38-generic
6
```

VM 호스트 커널은 7.0이지만 모듈은 VM 안 QEMU 게스트(`/var/lab/out/Image-dbg`, Linux **6.18.55-dbg**, lockdep·`DEBUG_ATOMIC_SLEEP`·ftrace·`WQ_WATCHDOG` 켜짐)에서 돌린다. 게스트는 `-smp 2`, `CONFIG_HZ=250`, `CONFIG_PREEMPT=y`이다. QEMU는 KVM 없이 TCG(명령어 번역)로 돌기 때문에 **절대 시간은 실제 보드보다 수 배~수십 배 느리다**. 숫자는 방식끼리 비교하는 용도로만 읽는다.

### 2.1 예제 1 — `-smp 2`용 DT를 덤프하고 가짜 장치 노드 추가

무엇을 확인하나: QEMU가 만드는 DT는 CPU 개수에 따라 달라지므로, `-smp 2`로 덤프한 DT에 IRQ 하나짜리 노드를 붙인다.

```sh
cd /var/tmp/F7
qemu-system-aarch64 -machine virt,dumpdtb=f7_base.dtb -cpu cortex-a57 -smp 2 -m 512M -nographic
dtc -I dtb -O dts f7_base.dtb -o f7_base.dts 2>/dev/null
grep -c cpu@ f7_base.dts
sed '$d' f7_base.dts > f7.dts && cat >> f7.dts <<EOF

	f7_dev {
		compatible = "don,f7-defer";
		interrupts = <0x00 0x50 0x01>;	/* SPI 80, edge rising */
	};
};
EOF
dtc -I dts -O dtb -o f7.dtb f7.dts 2>&1 | head -3
```

```text
2
f7.dts:276.3-21: Warning (clocks_property): /pl061@9030000:clocks: cell 0 is not a phandle reference
f7.dts:302.3-21: Warning (clocks_property): /pl031@9010000:clocks: cell 0 is not a phandle reference
f7.dts:310.3-28: Warning (clocks_property): /pl011@9000000:clocks: cell 0 is not a phandle reference
```

출력에서 볼 것: `cpu@` 노드가 2개다. dtc 경고는 덤프된 DT를 역컴파일하면서 phandle이 숫자로 바뀐 기존 노드 때문이고(D2), 우리 노드와는 상관없다. `<0x00 0x50 0x01>`은 "SPI 80번, rising edge"이고, GIC 하드웨어 번호로는 80 + 32 = **112**가 된다(D4의 "+32" 함정). 8절의 `/proc/interrupts`에서 `GIC-0 112 Edge`로 확인된다.

### 2.2 예제 2 — 모듈 소스 (1/5): 공통 상태와 bottom half

무엇을 확인하나: 모든 bottom half가 같은 함수 `f7_bottom()`을 부르게 해서, "어디서 불렸는지"만 다르고 나머지는 동일하게 만든다.

```c
// SPDX-License-Identifier: GPL-2.0
/* f7_defer.c — 같은 IRQ를 8가지 방법으로 미뤄 처리하고 IRQ→bottom half 지연을 잰다 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/sort.h>
#include <linux/slab.h>

static char *mode = "wq";	/* tasklet bhwq wq hipri unbound kthread kwork thread */
module_param(mode, charp, 0444);
static int nsamp = 200;
module_param(nsamp, int, 0444);
static bool sleep_test;		/* bottom half 안에서 msleep(1) 해 보기 */
module_param(sleep_test, bool, 0444);
static bool raw;		/* 샘플 원본 출력 */
module_param(raw, bool, 0444);

struct f7 {
	struct device *dev;
	int irq, n;
	u64 t_irq;			/* top half가 찍은 시각 */
	u64 *lat;			/* IRQ → bottom half 지연(ns) */
	struct tasklet_struct tl;
	struct work_struct work;
	struct workqueue_struct *own_wq;	/* WQ_UNBOUND | WQ_SYSFS | WQ_MEM_RECLAIM */
	struct task_struct *kt, *gen;
	struct kthread_worker *kw;
	struct kthread_work kwork;
	atomic_t kt_pending;
	wait_queue_head_t kt_wq;
	struct completion done;
};
static struct f7 *g;

static void f7_bottom(struct f7 *p, const char *who)	/* 모든 bottom half 공통 */
{
	u64 now = ktime_get_ns();

	if (p->n < nsamp)
		p->lat[p->n] = now - READ_ONCE(p->t_irq);
	if (p->n == 0)
		dev_info(p->dev, "ctx[%s]: hardirq=%d serving_softirq=%d task=%d preemptible=%d irqs_off=%d comm=%s cpu=%d\n",
			 who, !!in_hardirq(), !!in_serving_softirq(), !!in_task(),
			 preemptible(), irqs_disabled(), current->comm, raw_smp_processor_id());
	if (sleep_test && p->n == 0) {
		dev_info(p->dev, "%s: msleep(1) 시도\n", who);
		msleep(1);
		dev_info(p->dev, "%s: msleep(1) 복귀\n", who);
	}
	p->n++;
	complete(&p->done);
}
```

볼 것: `complete()`는 어느 컨텍스트에서나 불러도 되는 몇 안 되는 "깨우기" 함수다(F3). 그래서 hardirq·softirq·스레드 어디서 불려도 발생기를 깨울 수 있다. `current->comm`은 "지금 누구의 몸을 빌려 돌고 있나"를 보여 준다. softirq에서는 이게 우연히 끼어든 아무 태스크다.

### 2.3 예제 2 (2/5): 각 bottom half와 top half

```c
static void f7_tasklet_fn(struct tasklet_struct *t) { f7_bottom(from_tasklet(g, t, tl), "tasklet"); }
static void f7_work_fn(struct work_struct *w) { f7_bottom(container_of(w, struct f7, work), mode); }
static void f7_kwork_fn(struct kthread_work *w) { f7_bottom(container_of(w, struct f7, kwork), "kwork"); }

static int f7_kthread_fn(void *data)
{
	struct f7 *p = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(p->kt_wq, atomic_xchg(&p->kt_pending, 0) ||
					 kthread_should_stop());
		if (!kthread_should_stop())
			f7_bottom(p, "kthread");
	}
	return 0;
}

static irqreturn_t f7_hardirq(int irq, void *data)	/* top half: 시각만 찍고 미룬다 */
{
	struct f7 *p = data;

	WRITE_ONCE(p->t_irq, ktime_get_ns());
	if (!strcmp(mode, "tasklet"))
		tasklet_schedule(&p->tl);
	else if (!strcmp(mode, "bhwq"))
		queue_work(system_bh_wq, &p->work);
	else if (!strcmp(mode, "wq"))
		schedule_work(&p->work);		/* = system_percpu_wq (구 system_wq) */
	else if (!strcmp(mode, "hipri"))
		queue_work(system_highpri_wq, &p->work);
	else if (!strcmp(mode, "unbound"))
		queue_work(p->own_wq, &p->work);
	else if (!strcmp(mode, "kthread")) {
		atomic_set(&p->kt_pending, 1);
		wake_up(&p->kt_wq);
	} else if (!strcmp(mode, "kwork"))
		kthread_queue_work(p->kw, &p->kwork);
	else if (!strcmp(mode, "thread"))
		return IRQ_WAKE_THREAD;			/* irq/NN-f7_defer 스레드를 깨운다 */
	return IRQ_HANDLED;
}

static irqreturn_t f7_thread_fn(int irq, void *data)
{
	f7_bottom(data, "irq-thread");
	return IRQ_HANDLED;
}
```

볼 것: top half가 하는 일은 모드와 상관없이 "시각 찍기 + 예약 한 번"이 전부다. 예약 함수(`tasklet_schedule`, `queue_work`, `wake_up`, `kthread_queue_work`)는 전부 **hardirq에서 불러도 되는** 함수다. 내부에서 spinlock(IRQ 끔)만 쓰고 잠들지 않기 때문이다. 실제 드라이버에서는 `strcmp` 분기 대신 모드별로 핸들러를 따로 두지만, 여기서는 실험을 위해 한 핸들러에 모았다.

### 2.4 예제 2 (3/5): IRQ 발생기와 통계

```c
static int cmp_u64(const void *a, const void *b)
{
	u64 x = *(const u64 *)a, y = *(const u64 *)b;

	return x < y ? -1 : x > y;
}

static int f7_gen_fn(void *data)	/* IRQ 발생기: GIC에 pending을 nsamp번 세운다 */
{
	struct f7 *p = data;
	int i, miss = 0;

	for (i = 0; i < nsamp && !kthread_should_stop(); i++) {
		reinit_completion(&p->done);
		irq_set_irqchip_state(p->irq, IRQCHIP_STATE_PENDING, true);
		if (!wait_for_completion_timeout(&p->done, HZ))
			miss++;
		usleep_range(2000, 3000);
	}
	sort(p->lat, p->n < nsamp ? p->n : nsamp, sizeof(u64), cmp_u64, NULL);
	i = p->n < nsamp ? p->n : nsamp;
	if (i)
		dev_info(p->dev, "RESULT mode=%s n=%d miss=%d ns: min=%llu p50=%llu p90=%llu p99=%llu max=%llu\n",
			 mode, i, miss, p->lat[0], p->lat[i / 2],
			 p->lat[i * 9 / 10], p->lat[i * 99 / 100], p->lat[i - 1]);
	if (raw)
		for (int k = 0; k < i; k += 20) {
			char buf[256];
			int n = 0;

			for (int j = k; j < k + 20 && j < i; j++)
				n += scnprintf(buf + n, sizeof(buf) - n, " %llu", p->lat[j]);
			pr_info("f7raw %s%s\n", mode, buf);
		}
	while (!kthread_should_stop())
		msleep(50);
	return 0;
}
```

볼 것: 발생기는 다음 IRQ를 이전 bottom half가 끝난 뒤에만 일으킨다. 그래서 bottom half끼리 겹치지 않고, 측정값은 순수하게 "IRQ → bottom half 시작"이다. 끝의 `while (!kthread_should_stop())`는 kthread 함수가 `kthread_stop()` 전에 먼저 return 하지 않도록 기다리는 관용구다(7절).

### 2.5 예제 2 (4/5): probe — 다섯 종류의 "맡길 곳"을 다 만들어 둔다

```c
static int f7_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct f7 *p;
	int ret;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->lat = devm_kcalloc(dev, nsamp, sizeof(u64), GFP_KERNEL);
	if (!p->lat)
		return -ENOMEM;
	p->dev = dev;
	g = p;
	init_completion(&p->done);
	init_waitqueue_head(&p->kt_wq);
	tasklet_setup(&p->tl, f7_tasklet_fn);
	INIT_WORK(&p->work, f7_work_fn);
	kthread_init_work(&p->kwork, f7_kwork_fn);
	p->own_wq = alloc_workqueue("f7_wq", WQ_UNBOUND | WQ_SYSFS | WQ_MEM_RECLAIM, 0);
	if (!p->own_wq)
		return -ENOMEM;
	p->kt = kthread_run(f7_kthread_fn, p, "f7_kthread");
	p->kw = kthread_run_worker(0, "f7_kworker");
	if (IS_ERR(p->kt) || IS_ERR(p->kw)) {
		ret = -ENOMEM;
		goto err_wq;
	}
	p->irq = platform_get_irq(pdev, 0);
	if (p->irq < 0) {
		ret = p->irq;
		goto err_kt;
	}
	if (!strcmp(mode, "thread"))
		ret = request_threaded_irq(p->irq, f7_hardirq, f7_thread_fn, IRQF_ONESHOT,
					   "f7_defer", p);
	else
		ret = request_irq(p->irq, f7_hardirq, 0, "f7_defer", p);
	if (ret)
		goto err_kt;
	platform_set_drvdata(pdev, p);
	dev_info(dev, "mode=%s irq=%d nsamp=%d\n", mode, p->irq, nsamp);
	p->gen = kthread_run(f7_gen_fn, p, "f7_gen");
	return 0;
err_kt:
	kthread_stop(p->kt);
	kthread_destroy_worker(p->kw);
err_wq:
	destroy_workqueue(p->own_wq);
	return ret;
}
```

볼 것: `alloc_workqueue("f7_wq", WQ_UNBOUND | WQ_SYSFS | WQ_MEM_RECLAIM, 0)`는 전용 workqueue를 만든다. `WQ_SYSFS` 덕분에 `/sys/bus/workqueue/devices/f7_wq`가 생기고, `WQ_MEM_RECLAIM` 때문에 rescuer 스레드 `kworker/R-f7_wq`가 생긴다(6절). `kthread_run_worker()`는 6.18 기준 이름이다(예전 커널은 `kthread_create_worker()`를 직접 쓰고 `wake_up_process()`를 따로 불렀다). 에러 경로는 실험용으로 단순화했다 — `kt`와 `kw` 중 하나만 실패하면 다른 하나가 새는데, 실제 드라이버라면 하나씩 검사해서 되감아야 한다.

### 2.6 예제 2 (5/5): remove — 미룬 일을 정리하는 순서

```c
static void f7_remove(struct platform_device *pdev)	/* 순서: 소스 차단 → 미룬 일 취소 → 해제 */
{
	struct f7 *p = platform_get_drvdata(pdev);

	kthread_stop(p->gen);
	free_irq(p->irq, p);		/* 1. 새 IRQ(=새 큐잉)가 더 안 오게 */
	tasklet_kill(&p->tl);		/* 2. 이미 큐에 있는 것 취소/완료 대기 */
	cancel_work_sync(&p->work);
	kthread_cancel_work_sync(&p->kwork);
	kthread_stop(p->kt);
	kthread_destroy_worker(p->kw);
	destroy_workqueue(p->own_wq);	/* 3. 그 다음에 자원 해제 */
}

static const struct of_device_id f7_of[] = { { .compatible = "don,f7-defer" }, { } };
MODULE_DEVICE_TABLE(of, f7_of);
static struct platform_driver f7_drv = {
	.probe = f7_probe, .remove = f7_remove,
	.driver = { .name = "f7_defer", .of_match_table = f7_of },
};
module_platform_driver(f7_drv);
MODULE_DESCRIPTION("F7 deferred work demo");
MODULE_LICENSE("GPL");
```

볼 것: **순서가 핵심이다.** 먼저 일을 만들어 내는 쪽(IRQ, 발생기)을 끊고, 그다음 이미 예약된 일을 취소하거나 끝나길 기다리고, 마지막에 그 일이 쓰는 메모리를 푼다. 순서를 바꾸면 11절에서 보는 것처럼 커널이 죽는다. `devm_` 자원(`p`, `p->lat`)은 `.remove`가 끝난 **뒤에** 풀리므로 여기서 안전하다.

### 2.7 빌드와 게스트 실행 스크립트

Kbuild는 한 줄이다(11절의 `f7_teardown.c`도 같이 빌드).

```make
obj-m += f7_defer.o f7_teardown.o
```

```sh
cd /var/tmp/F7/mod && make -C /var/lab/out/linux-build-dbg M=$PWD -j2 modules
```

```text
make: Entering directory '/var/lab/build/linux-dbg'
make[1]: Entering directory '/var/tmp/F7/mod'
  CC [M]  f7_defer.o
  CC [M]  f7_teardown.o
  MODPOST Module.symvers
  CC [M]  f7_defer.mod.o
  CC [M]  .module-common.o
  CC [M]  f7_teardown.mod.o
  LD [M]  f7_defer.ko
  LD [M]  f7_teardown.ko
make[1]: Leaving directory '/var/tmp/F7/mod'
make: Leaving directory '/var/lab/build/linux-dbg'
```

경고 0개다. 부팅 스크립트는 랩 rootfs를 복사해 `.ko`와 게스트용 스크립트 `f7.sh`를 넣고, 디버그 커널을 `-smp 2`와 우리 DT로 띄운다. `LAB_CMD`가 `f7.sh <실험이름>`을 실행한 뒤 자동으로 poweroff 한다.

```sh
#!/bin/sh
# boot.sh "<f7.sh 인자>" — dbg 커널, -smp 2, f7.dtb
W=/var/tmp/F7
cd $W/mod && make -s -C /var/lab/out/linux-build-dbg M=$PWD -j2 modules >/dev/null 2>&1 || exit 1
rm -rf $W/r && mkdir $W/r && cp -a /var/lab/build/rootfs/. $W/r/
cp $W/mod/f7_defer.ko $W/mod/f7_teardown.ko /Users/donh/.elinux_scratch/F7/f7.sh $W/r/
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
ARGS=$(echo "$1" | tr ' ' '~')
timeout ${TMO:-300} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic \
  -kernel /var/lab/out/Image-dbg -initrd $W/i.gz -dtb $W/f7.dtb \
  -append "console=ttyAMA0 ${KARGS:-quiet} LAB_CMD=sh~/f7.sh~$ARGS"
```

게스트 쪽 `f7.sh`의 공통 부분은 "모드 하나로 insmod → RESULT 줄이 찍힐 때까지 기다림 → rmmod"이다. 실험별 부분은 각 절에서 보여 준다.

```sh
#!/bin/sh
# f7.sh <실험> — 게스트 안에서 실행
M="tasklet bhwq wq hipri unbound kthread kwork thread"
run() {	# run <mode> [추가 인자]
  C0=$(dmesg | grep -c "RESULT mode=$1 ")
  insmod /f7_defer.ko mode=$1 $2
  N=0; while [ $(dmesg | grep -c "RESULT mode=$1 ") -le $C0 ]; do sleep 1; N=$((N+1)); [ $N -gt 60 ] && break; done
  rmmod f7_defer; }
case "$1" in
ctx)
  for m in $M; do run $m nsamp=5; done
  dmesg | grep -E "ctx\[|RESULT" ;;
# lat) sleep) trace) ps) td) hog) 는 각 절에서
esac
```

---

## 3. softirq — 고정된 10개의 "소프트웨어 인터럽트"

### 3.1 직관

softirq는 "hardirq가 끝나는 순간, 같은 CPU에서, IRQ를 다시 켠 채로 이어서 도는 처리 루틴"이다. bare-metal로 치면 Cortex-M의 **PendSV**와 비슷하다. ISR이 PendSV 비트를 세워 두면, 더 높은 우선순위 ISR이 다 끝난 뒤 PendSV 핸들러가 돈다. softirq도 hardirq가 `raise_softirq()`로 비트를 세우고, hardirq 탈출 경로(`irq_exit`)에서 그 비트들을 처리한다.

### 3.2 정의 — 목록이 소스에 박혀 있다

softirq는 **동적으로 추가할 수 없다.** 커널 컴파일 시점에 정해진 enum이다. 랩 소스에서 직접 확인한다.

예제 3 — 무엇을 확인하나: 6.18의 softirq 목록, 이름 표, 그리고 한 번의 softirq 처리 예산.

```sh
cd /var/lab/src/linux-6.18.55
grep -n "_SOFTIRQ,\|NR_SOFTIRQS" include/linux/interrupt.h | head -20
grep -n "softirq_to_name\[" -A4 kernel/softirq.c | head -5
grep -n "MAX_SOFTIRQ_TIME\|MAX_SOFTIRQ_RESTART" kernel/softirq.c | head -4
```

```text
550:	TIMER_SOFTIRQ,
551:	NET_TX_SOFTIRQ,
552:	NET_RX_SOFTIRQ,
553:	BLOCK_SOFTIRQ,
554:	IRQ_POLL_SOFTIRQ,
555:	TASKLET_SOFTIRQ,
556:	SCHED_SOFTIRQ,
557:	HRTIMER_SOFTIRQ,
558:	RCU_SOFTIRQ,    /* Preferable RCU should always be the last softirq */
560:	NR_SOFTIRQS
581:extern const char * const softirq_to_name[NR_SOFTIRQS];
64:const char * const softirq_to_name[NR_SOFTIRQS] = {
65-	"HI", "TIMER", "NET_TX", "NET_RX", "BLOCK", "IRQ_POLL",
66-	"TASKLET", "SCHED", "HRTIMER", "RCU"
67-};
68-
531: * We restart softirq processing for at most MAX_SOFTIRQ_RESTART times,
533: * The MAX_SOFTIRQ_TIME provides a nice upper bound in most cases, but in
535: * increment and so we need the MAX_SOFTIRQ_RESTART limit as
543:#define MAX_SOFTIRQ_TIME  msecs_to_jiffies(2)
```

출력에서 볼 것: grep 패턴이 549행 `HI_SOFTIRQ=0,`은 놓쳤지만(`=0`이 붙어 있어 `_SOFTIRQ,`에 안 걸림) 이름 표에 `"HI"`가 첫 번째로 있다. `MAX_SOFTIRQ_RESTART`의 실제 값(10)은 544행 `#define`에 있는데 `head -4`에 잘렸다. 그래서 전부 **10개**: HI, TIMER, NET_TX, NET_RX, BLOCK, IRQ_POLL, TASKLET, SCHED, HRTIMER, RCU. 번호(vec)는 0부터 9까지이고, 10절 ftrace의 `vec=6 [action=TASKLET]`이 바로 이 번호다. 드라이버가 쓰는 tasklet과 BH workqueue는 **HI(0)와 TASKLET(6) 두 softirq 위에 얹혀 있다.**

| vec | 이름 | 누가 쓰나 |
|---|---|---|
| 0 | HI | `tasklet_hi_schedule`, `system_bh_highpri_wq` |
| 1 | TIMER | `timer_list` 만료 처리 (F8) |
| 2, 3 | NET_TX, NET_RX | 네트워크 송수신, NAPI 폴링 (O 모듈) |
| 4 | BLOCK | 블록 I/O 완료 |
| 5 | IRQ_POLL | 블록 장치용 irq_poll |
| 6 | TASKLET | `tasklet_schedule`, `system_bh_wq` |
| 7 | SCHED | 스케줄러 로드 밸런싱 |
| 8 | HRTIMER | softirq 모드 hrtimer (F8) |
| 9 | RCU | RCU 콜백 처리 (F4) |

### 3.3 언제 도나 — 세 군데

1. **hardirq 탈출 직후** (`irq_exit()` → `__irq_exit_rcu()` → `handle_softirqs()`): 가장 흔한 경로. 5절 BUG 콜스택에 `__irq_exit_rcu → do_softirq_own_stack → handle_softirqs`가 그대로 찍힌다.
2. **`local_bh_enable()` 시점**: 어떤 코드가 `spin_lock_bh()` 등으로 softirq를 막고 있다가 풀면, 그동안 쌓인 softirq를 그 자리에서 처리한다.
3. **`ksoftirqd/N` 스레드**: softirq가 너무 많이 쌓이면 무한정 처리하지 않는다. `handle_softirqs()`는 한 번에 최대 `MAX_SOFTIRQ_TIME`(2 ms)·`MAX_SOFTIRQ_RESTART`(10회)만 돌고, 남은 것은 CPU마다 하나씩 있는 nice 0 스레드 `ksoftirqd/N`에게 넘긴다. 이렇게 넘어가면 그 softirq는 **일반 스레드처럼 스케줄링되므로 늦어진다.** 네트워크 폭주 때 지연이 튀는 이유 중 하나다.

### 3.4 성질

- **per-CPU**: raise한 CPU에서 돈다. 같은 softirq 종류가 두 CPU에서 동시에 돌 수 있다(그래서 NET_RX 핸들러는 per-CPU 자료구조를 쓴다).
- **잠들 수 없다**: 태스크가 아니라 아무 태스크의 몸을 빌린 상태다. `preempt_count`에 "softirq 처리 중" 비트(`SOFTIRQ_OFFSET`, 0x100)가 서 있다.
- **IRQ는 켜져 있다**: softirq 처리 중에도 hardirq는 들어올 수 있다. 그래서 softirq와 hardirq가 공유하는 데이터는 `spin_lock_irqsave`로, softirq와 process 컨텍스트가 공유하는 데이터는 `spin_lock_bh`로 보호한다(F4).
- **드라이버가 새 softirq를 만들 일은 없다.** 새 vec를 추가하려면 커널 자체를 고쳐야 하고, 업스트림에서 받아 주지 않는다. 드라이버의 선택지는 그 위에 얹힌 tasklet과 BH workqueue다.

`/proc/softirqs`로 CPU별 누적 횟수를 볼 수 있다(6.6절 출력에 포함).

---

## 4. tasklet과 BH workqueue — softirq 위의 드라이버용 인터페이스

### 4.1 tasklet

tasklet은 TASKLET(또는 HI) softirq 위에서 도는 "드라이버가 동적으로 만들 수 있는 작은 bottom half"다. 성질은 이렇다.

- `tasklet_schedule(t)`를 부른 **그 CPU**에서 돈다.
- **같은 tasklet은 동시에 두 CPU에서 돌지 않는다**(서로 다른 tasklet은 동시에 돌 수 있다). 그래서 핸들러 안에서 자기 자신과의 경쟁은 걱정하지 않아도 된다.
- 이미 예약된 tasklet을 다시 `tasklet_schedule` 하면 **한 번만** 돈다(이벤트가 합쳐진다). bare-metal의 "pending 플래그"와 같다.
- softirq이므로 잠들 수 없다.

그리고 커널 소스가 tasklet을 이렇게 소개한다.

예제 4 — 무엇을 확인하나: 6.18에서 tasklet의 공식 상태와 BH workqueue의 존재.

```sh
cd /var/lab/src/linux-6.18.55
sed -n 664,668p include/linux/interrupt.h
grep -n "WQ_BH\|system_bh" include/linux/workqueue.h
grep -n "system_bh_wq = \|events_bh" kernel/workqueue.c
sed -n 950,960p kernel/softirq.c
```

```text

/* Tasklets --- multithreaded analogue of BHs.

   This API is deprecated. Please consider using threaded IRQs instead:
   https://lore.kernel.org/lkml/20200716081538.2sivhkj4hcyrusem@linutronix.de
371:	WQ_BH			= 1 << 0, /* execute in bottom half (softirq) context */
413:	__WQ_BH_ALLOWS		= WQ_BH | WQ_HIGHPRI | WQ_PERCPU,
457: * system_bh[_highpri]_wq are convenience interface to softirq. BH work items
469:extern struct workqueue_struct *system_bh_wq;
470:extern struct workqueue_struct *system_bh_highpri_wq;
7915:	system_bh_wq = alloc_workqueue("events_bh", WQ_BH | WQ_PERCPU, 0);
7916:	system_bh_highpri_wq = alloc_workqueue("events_bh_highpri",
static __latent_entropy void tasklet_action(void)
{
	workqueue_softirq_action(false);
	tasklet_action_common(this_cpu_ptr(&tasklet_vec), TASKLET_SOFTIRQ);
}

static __latent_entropy void tasklet_hi_action(void)
{
	workqueue_softirq_action(true);
	tasklet_action_common(this_cpu_ptr(&tasklet_hi_vec), HI_SOFTIRQ);
}
```

출력에서 볼 것: 헤더 주석이 tasklet API를 **"deprecated"**라고 적고 threaded IRQ를 권한다. 하지만 6.18에도 tasklet은 그대로 있고 많은 드라이버가 아직 쓴다. 그리고 `tasklet_action()`이 자기 tasklet 목록을 처리하기 **직전에** `workqueue_softirq_action()`을 부른다. 즉 BH workqueue는 TASKLET softirq에 얹혀 돈다. 이름은 workqueue지만 실행 컨텍스트는 tasklet과 똑같은 softirq다.

### 4.2 BH workqueue — tasklet의 후계자

BH workqueue(`WQ_BH`)는 **Linux 6.9**에 tasklet을 대체하려고 들어왔다(Tejun Heo의 "workqueue: Implement BH workqueues to eventually replace tasklets" 시리즈). 아이디어는 단순하다. "softirq에서 돌아야 하는 일"도 workqueue API(`work_struct`, `queue_work`, `cancel_work_sync`, `flush_work`)로 쓰게 해서, 드라이버가 API 하나만 익히면 되게 하고 tasklet 구현을 언젠가 지우겠다는 것이다. 6.10 무렵에는 tasklet의 `disable/enable`에 대응하는 `disable_work()`/`enable_work()`가 추가되었다(6.18 헤더에 `disable_work`, `disable_work_sync`, `enable_work`, `enable_and_queue_work`가 있는 것을 위에서 확인).

변환은 거의 기계적이다.

| tasklet | BH workqueue |
|---|---|
| `struct tasklet_struct t` | `struct work_struct w` |
| `tasklet_setup(&t, fn)` | `INIT_WORK(&w, fn)` |
| `from_tasklet(p, t, field)` | `from_work(p, w, field)` |
| `tasklet_schedule(&t)` | `queue_work(system_bh_wq, &w)` |
| `tasklet_hi_schedule(&t)` | `queue_work(system_bh_highpri_wq, &w)` |
| `tasklet_disable(&t)` | `disable_work_sync(&w)` |
| `tasklet_enable(&t)` | `enable_work(&w)` |
| `tasklet_kill(&t)` | `cancel_work_sync(&w)` |

`WQ_BH` 워크큐에 허용되는 플래그는 `WQ_BH | WQ_HIGHPRI | WQ_PERCPU`뿐이다(`__WQ_BH_ALLOWS`). unbound BH나 `max_active` 같은 개념은 없다. 실행 순서는 "큐잉한 CPU에서, 큐잉한 순서대로"(헤더 주석 "executed in the queueing CPU's BH context in the queueing order")다.

**그럼 새 드라이버는?** 순서대로:

1. 잠들어야 하면(I2C·SPI 센서 등) → **threaded IRQ** (8절). 커널 주석이 권하는 것이 이것이다.
2. 정말 softirq 수준의 지연이 필요하고 잠들 필요가 없으면 → **BH workqueue**.
3. tasklet은 기존 코드를 유지보수할 때만.

---

## 5. 컨텍스트 실측 — 누구의 몸을 빌려 도는가, 잠들면 어떻게 되는가

### 5.1 예제 5 — 8가지 bottom half의 컨텍스트

무엇을 확인하나: 각 모드의 bottom half가 처음 돌 때 `in_serving_softirq()`, `in_task()`, `preemptible()`, `current->comm`이 무엇인지.

```sh
sh /Users/donh/.elinux_scratch/F7/boot.sh ctx 2>&1 | tail -40
```

```text
lab initramfs: 6.18.55-dbg up 8.68s
[    9.577069] f7_defer f7_dev: ctx[tasklet]: hardirq=0 serving_softirq=1 task=0 preemptible=0 irqs_off=0 comm=rcu_preempt cpu=0
[    9.630685] f7_defer f7_dev: RESULT mode=tasklet n=5 miss=0 ns: min=39536 p50=48672 p90=409264 p99=409264 max=409264
[   10.083913] f7_defer f7_dev: ctx[bhwq]: hardirq=0 serving_softirq=1 task=0 preemptible=0 irqs_off=0 comm=f7_gen cpu=0
[   10.105426] f7_defer f7_dev: RESULT mode=bhwq n=5 miss=0 ns: min=52208 p50=61568 p90=472592 p99=472592 max=472592
[   10.647328] f7_defer f7_dev: ctx[wq]: hardirq=0 serving_softirq=0 task=1 preemptible=1 irqs_off=0 comm=kworker/0:0 cpu=0
[   10.685842] f7_defer f7_dev: RESULT mode=wq n=5 miss=0 ns: min=153680 p50=253424 p90=4547280 p99=4547280 max=4547280
[   11.129323] f7_defer f7_dev: ctx[hipri]: hardirq=0 serving_softirq=0 task=1 preemptible=1 irqs_off=0 comm=kworker/0:0H cpu=0
[   11.194394] f7_defer f7_dev: RESULT mode=hipri n=5 miss=0 ns: min=166128 p50=328256 p90=3353600 p99=3353600 max=3353600
[   12.275886] f7_defer f7_dev: ctx[unbound]: hardirq=0 serving_softirq=0 task=1 preemptible=1 irqs_off=0 comm=kworker/u8:1 cpu=0
[   12.317272] f7_defer f7_dev: RESULT mode=unbound n=5 miss=0 ns: min=160576 p50=451968 p90=9675728 p99=9675728 max=9675728
[   13.141087] f7_defer f7_dev: ctx[kthread]: hardirq=0 serving_softirq=0 task=1 preemptible=1 irqs_off=0 comm=f7_kthread cpu=0
[   13.179996] f7_defer f7_dev: RESULT mode=kthread n=5 miss=0 ns: min=144384 p50=329712 p90=937840 p99=937840 max=937840
[   14.300851] f7_defer f7_dev: ctx[kwork]: hardirq=0 serving_softirq=0 task=1 preemptible=1 irqs_off=0 comm=f7_kworker cpu=0
[   14.399555] f7_defer f7_dev: RESULT mode=kwork n=5 miss=0 ns: min=120960 p50=489744 p90=7668048 p99=7668048 max=7668048
[   15.212575] f7_defer f7_dev: ctx[irq-thread]: hardirq=0 serving_softirq=0 task=1 preemptible=1 irqs_off=0 comm=irq/22-f7_defer cpu=0
[   15.243755] f7_defer f7_dev: RESULT mode=thread n=5 miss=0 ns: min=89216 p50=153840 p90=34665600 p99=34665600 max=34665600
[   16.723433] reboot: Power down
```

출력에서 볼 것 (타임스탬프는 실행마다 다르다):

- **tasklet, bhwq**: `serving_softirq=1 task=0 preemptible=0`. 그리고 `comm`이 각각 `rcu_preempt`, `f7_gen`이다. 즉 tasklet은 "IRQ가 들어왔을 때 마침 CPU0에서 돌던 아무 태스크"의 몸을 빌려 돌았다. 이 태스크들은 우리 드라이버와 아무 관계가 없다. 그래서 여기서 잠들면 **남의 태스크를 재우는 것**이 된다.
- **wq, hipri, unbound, kthread, kwork, irq-thread**: 전부 `task=1 preemptible=1`. 각자 자기 스레드다. 이름이 곧 정체다: `kworker/0:0`(CPU0 일반 풀), `kworker/0:0H`(CPU0 highpri 풀, H = nice -20), `kworker/u8:1`(unbound 풀), `f7_kthread`·`f7_kworker`(우리가 만든 스레드), `irq/22-f7_defer`(Linux IRQ 22번의 스레드).
- 모든 bottom half에서 `irqs_off=0`이다. softirq도 IRQ를 켠 채로 돈다는 3.4절 내용 그대로다.
- 이 실행은 5개 샘플만 봤으므로 RESULT 숫자는 무시한다(9절에서 200개로 잰다).

### 5.2 예제 6 — softirq에서 잠들면 (tasklet, BH workqueue)

무엇을 확인하나: `sleep_test=1`로 bottom half 첫 실행에서 `msleep(1)`을 부른다. 디버그 커널(`DEBUG_ATOMIC_SLEEP`)이므로 즉시 잡힌다. 일회용 QEMU 게스트에서만 한다.

```sh
# f7.sh의 sleep 실험: run $2 "nsamp=3 sleep_test=1" 후 dmesg
KARGS="loglevel=7" TMO=100 sh /Users/donh/.elinux_scratch/F7/boot.sh "sleep tasklet" 2>&1 | sed -n "/f7_defer f7_dev: mode/,\$p"
```

```text
[   15.643439] f7_defer f7_dev: mode=tasklet irq=22 nsamp=3
[   15.666992] f7_defer f7_dev: ctx[tasklet]: hardirq=0 serving_softirq=1 task=0 preemptible=0 irqs_off=0 comm=insmod cpu=0
[   15.667161] f7_defer f7_dev: tasklet: msleep(1) 시도
[   15.669840] BUG: scheduling while atomic: insmod/76/0x00000102
[   15.670007] 1 lock held by insmod/76:
[   15.670090]  #0: ffffda151080dc88 (cgroup_threadgroup_rwsem){.+.+}-{0:0}, at: do_exit+0xd0/0xa88
[   15.677109] Modules linked in: f7_defer(O)
[   15.678096] CPU: 0 UID: 0 PID: 76 Comm: insmod Tainted: G           O        6.18.55-dbg #1 PREEMPT 
...
[   15.679453]  show_stack+0x30/0x90 (C)
[   15.679637]  dump_stack_lvl+0x70/0x98
[   15.679665]  dump_stack+0x18/0x24
[   15.679687]  __schedule_bug+0x74/0xa0
[   15.679718]  __schedule+0xdac/0x1088
[   15.679777]  schedule+0x48/0x140
[   15.679801]  schedule_timeout+0x94/0x140
[   15.679840]  msleep+0x50/0x80
[   15.679872]  f7_bottom+0x114/0x140 [f7_defer]
[   15.680236]  f7_tasklet_fn+0x24/0x40 [f7_defer]
[   15.680268]  tasklet_action_common+0x140/0x1c0
[   15.680290]  tasklet_action+0x38/0x50
[   15.680311]  handle_softirqs+0x12c/0x4b0
[   15.680332]  __do_softirq+0x1c/0x28
[   15.680353]  ____do_softirq+0x18/0x30
[   15.680376]  call_on_irq_stack+0x30/0x48
[   15.680400]  do_softirq_own_stack+0x24/0x50
[   15.680423]  __irq_exit_rcu+0x16c/0x1b8
[   15.680444]  irq_exit_rcu+0x18/0x48
[   15.680464]  el1_interrupt+0x3c/0x58
[   15.680485]  el1h_64_irq_handler+0x18/0x30
[   15.680507]  el1h_64_irq+0x6c/0x70
[   15.680604]  _raw_spin_unlock_irq+0x3c/0x80 (P)
[   15.680631]  exit_signals+0x180/0x488
[   15.680682]  do_exit+0xd0/0xa88
[   15.680704]  do_group_exit+0x38/0xb8
[   15.680726]  __arm64_sys_exit_group+0x20/0x28
...
[   15.690140] Unable to handle kernel write to read-only memory at virtual address ffffda150ddfca84
...
[   15.691788] Internal error: Oops: 000000009600004f [#1]  SMP
[   15.700686] Modules linked in: f7_defer(O)
[   15.701208] CPU: 0 UID: 0 PID: 0 Comm: swapper/0 Tainted: G        W  O        6.18.55-dbg #1 PREEMPT 
...
[   15.703321] pc : __run_timer_base+0x294/0x3e0
...
[   15.710539]  __run_timer_base+0x294/0x3e0 (P)
[   15.710777]  run_timer_softirq+0x3c/0x70
[   15.710993]  handle_softirqs+0x12c/0x4b0
```

(`...`는 레지스터 덤프·Mem abort 상세 등을 생략한 곳)

출력에서 볼 것: 콜스택을 **아래에서 위로** 읽는다. `insmod` 프로세스가 종료하려고 `exit_group` syscall → `do_exit`에서 IRQ를 켜는 순간(`_raw_spin_unlock_irq`) 우리 IRQ가 들어왔고 → `el1_interrupt` → `irq_exit_rcu` → softirq 처리 → `tasklet_action` → 우리 `f7_tasklet_fn` → `msleep` → `schedule()` → **"BUG: scheduling while atomic"**. 끝에 붙은 `0x00000102`는 그 순간의 `preempt_count`이고, `0x100` 비트가 "softirq 처리 중"(`SOFTIRQ_OFFSET`)이다. 즉 커널이 "softirq 안에서 schedule을 불렀다"고 정확히 말해 준다. 피해자는 **종료 중이던 남의 프로세스 insmod**다. 그리고 몇 ms 뒤 타이머 softirq(`__run_timer_base`)에서 Oops가 났다. softirq 처리 도중에 CPU를 넘겨 버려 내부 상태가 꼬인 결과로 보인다(정확한 연쇄는 여기서 추적하지 않았다). **softirq에서 잠드는 버그는 "경고만 찍고 넘어가는" 버그가 아니다.**

같은 실험을 `quiet`로 한 번 더 했을 때는 IRQ가 idle 태스크 위에 떨어졌고 이렇게 끝났다.

```text
[   30.422952] BUG: scheduling while atomic: swapper/0/0/0x00000102
[   30.444356] bad: scheduling from the idle thread!
[   30.457607] Kernel panic - not syncing: stack-protector: Kernel stack is corrupted in: lock_acquired+0x4a8/0x4a8
```

빌린 몸이 idle 태스크(`swapper/0`)였으니 "idle 스레드에서 스케줄링"이라는 더 나쁜 상황이 되었고 바로 panic이다. **어느 태스크의 몸을 빌리느냐는 운**이라 증상이 매번 다르다. 현장에서 "가끔, 서로 다른 콜스택으로 죽는다"는 버그의 전형이다.

BH workqueue도 똑같다.

```text
[    8.141206] f7_defer f7_dev: bhwq: msleep(1) 시도
[    8.141386] BUG: scheduling while atomic: f7_gen/81/0x00000102
[    8.141450] 2 locks held by f7_gen/81:
[    8.141496]  #0: ffff000004411758 ((wq_completion)events_bh){+.-.}-{0:0}, at: process_one_work+0x19c/0x668
[    8.142897]  #1: ffff800080003e08 ((work_completion)(&p->work)){+.-.}-{0:0}, at: process_one_work+0x1c4/0x668
...
[    8.148508]  f7_bottom+0x114/0x140 [f7_defer]
[    8.150057]  f7_work_fn+0x24/0x40 [f7_defer]
[    8.150473]  process_one_work+0x21c/0x668
[    8.151326]  bh_worker+0x1c0/0x230
[    8.151360]  workqueue_softirq_action+0x84/0xa0
[    8.151965]  tasklet_action+0x1c/0x50
[    8.152032]  handle_softirqs+0x12c/0x4b0
...
[    8.152497]  irq_set_irqchip_state+0xa4/0x128
[    8.152544]  f7_gen_fn+0x74/0x280 [f7_defer]
...
[    8.160699] Internal error: Oops: 000000009600004f [#1]  SMP
```

출력에서 볼 것: `process_one_work` → `bh_worker` → `workqueue_softirq_action` → `tasklet_action`. 4.1절에서 소스로 본 "BH workqueue는 TASKLET softirq에 얹혀 돈다"가 콜스택으로 확인된다. lockdep이 잡은 락 이름 `(wq_completion)events_bh`의 `events_bh`가 `system_bh_wq`의 이름이다. 이번에 빌린 몸은 우리 발생기 `f7_gen`이었다. 발생기가 `irq_set_irqchip_state()`에서 락을 풀며 IRQ를 켜는 순간 IRQ가 바로 들어온 것이다.

### 5.3 예제 7 — workqueue에서 잠들면

```sh
TMO=120 sh /Users/donh/.elinux_scratch/F7/boot.sh "sleep wq" 2>&1 | tail -8
```

```text
lab initramfs: 6.18.55-dbg up 28.63s
[   31.876996] f7_defer f7_dev: mode=wq irq=22 nsamp=3
[   31.888744] f7_defer f7_dev: ctx[wq]: hardirq=0 serving_softirq=0 task=1 preemptible=1 irqs_off=0 comm=kworker/0:2 cpu=0
[   31.889137] f7_defer f7_dev: wq: msleep(1) 시도
[   31.901859] f7_defer f7_dev: wq: msleep(1) 복귀
[   31.935341] f7_defer f7_dev: RESULT mode=wq n=3 miss=0 ns: min=357040 p50=391088 p90=8146224 p99=8146224 max=8146224
[   33.804903] reboot: Power down
```

출력에서 볼 것: 같은 코드가 kworker에서는 아무 문제 없이 잠들었다 깬다. `msleep(1)`이 실제로 약 12.7 ms 걸린 것(31.889 → 31.902)은 `HZ=250`이라 1 ms가 jiffy 단위(4 ms)로 올림되고 타이머 처리가 겹친 결과다(F8). **"잠들 수 있는 곳"은 스레드뿐**이라는 것이 이 두 실험의 결론이다.

---

## 6. workqueue — CMWQ, 드라이버의 기본 선택지

### 6.1 직관

workqueue는 "커널이 관리하는 스레드 풀에 함수 포인터를 던지는 것"이다. RTOS로 치면 "공용 워커 태스크 몇 개가 메시지 큐를 보고 있다가, 받은 함수 포인터를 실행해 주는 구조"다. 드라이버는 스레드를 만들 필요 없이 `struct work_struct`에 함수를 묶어 `queue_work()`만 하면 된다. 스레드 컨텍스트이므로 잠들 수 있다.

### 6.2 CMWQ — 워커는 공유하고, 개수는 커널이 정한다

옛날(2.6.36 이전) workqueue는 워크큐 하나마다 CPU별 전용 스레드를 만들었다. 워크큐가 수백 개면 스레드가 수천 개였다. 지금 구조는 **CMWQ**(Concurrency Managed Workqueue)다.

- 드라이버가 보는 **workqueue**는 "정책(속성) + 이름표"일 뿐, 자기 스레드를 갖지 않는다.
- 실제 스레드(`kworker`)는 **worker_pool**에 속하고, 여러 workqueue가 같은 pool을 **공유**한다. CPU마다 normal pool(nice 0)과 highpri pool(nice -20)이 있고, unbound pool은 속성(cpumask, nice)이 같은 것끼리 공유한다.
- workqueue와 pool을 잇는 연결 고리가 **pool_workqueue**(pwq)다. `max_active` 같은 "이 워크큐에서 동시에 몇 개까지" 제한이 여기서 적용된다.
- **concurrency management**: per-CPU pool은 "지금 이 CPU에서 실제로 돌고 있는(잠들지 않은) 워커"를 센다. 돌고 있는 워커가 잠들면(I/O 대기 등) 그 순간 pool이 다른 워커를 깨워 다음 work를 이어서 실행한다. 반대로 돌고 있는 워커가 있으면 새 워커를 깨우지 않는다. 그래서 스레드 수는 필요한 만큼만 생기고, CPU는 놀지 않는다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="24" font-size="13">workqueue — 드라이버가 queue_work() 하는 대상 (정책 + 이름)</text>
  <rect x="10" y="34" width="125" height="48" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="72" y="54" text-anchor="middle" font-size="12">system_wq</text>
  <text x="72" y="72" text-anchor="middle" font-size="12">"events"</text>
  <rect x="145" y="34" width="125" height="48" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="207" y="54" text-anchor="middle" font-size="12">highpri_wq</text>
  <text x="207" y="72" text-anchor="middle" font-size="12">"events_highpri"</text>
  <rect x="280" y="34" width="125" height="48" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="342" y="54" text-anchor="middle" font-size="12">unbound_wq</text>
  <text x="342" y="72" text-anchor="middle" font-size="12">"events_unbound"</text>
  <rect x="415" y="34" width="125" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="477" y="54" text-anchor="middle" font-size="12">f7_wq (우리 것)</text>
  <text x="477" y="72" text-anchor="middle" font-size="12">UNBOUND+RECLAIM</text>
  <rect x="550" y="34" width="125" height="48" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="612" y="54" text-anchor="middle" font-size="12">system_bh_wq</text>
  <text x="612" y="72" text-anchor="middle" font-size="12">"events_bh"</text>
  <line x1="72" y1="82" x2="72" y2="146" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="207" y1="82" x2="207" y2="146" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="342" y1="82" x2="342" y2="146" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="460" y1="82" x2="370" y2="146" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="490" y1="82" x2="490" y2="146" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <line x1="612" y1="82" x2="612" y2="146" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="100" y="118" font-size="12">pwq</text>
  <text x="235" y="118" font-size="12">pwq</text>
  <text x="498" y="118" font-size="12">비상시</text>
  <text x="10" y="142" font-size="13">worker_pool — 실제 스레드 묶음 (공유)</text>
  <rect x="10" y="150" width="125" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="72" y="172" text-anchor="middle" font-size="12">normal pool</text>
  <text x="72" y="192" text-anchor="middle" font-size="12">CPU별 · nice 0</text>
  <rect x="145" y="150" width="125" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="207" y="172" text-anchor="middle" font-size="12">highpri pool</text>
  <text x="207" y="192" text-anchor="middle" font-size="12">CPU별 · nice -20</text>
  <rect x="280" y="150" width="125" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="342" y="172" text-anchor="middle" font-size="12">unbound pool</text>
  <text x="342" y="192" text-anchor="middle" font-size="12">속성별 공유</text>
  <rect x="415" y="150" width="125" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="477" y="172" text-anchor="middle" font-size="12">rescuer</text>
  <text x="477" y="192" text-anchor="middle" font-size="12">MEM_RECLAIM 전용</text>
  <rect x="550" y="150" width="125" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="612" y="172" text-anchor="middle" font-size="12">BH pool</text>
  <text x="612" y="192" text-anchor="middle" font-size="12">CPU별 · 2개</text>
  <line x1="72" y1="206" x2="72" y2="252" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="207" y1="206" x2="207" y2="252" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="342" y1="206" x2="342" y2="252" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="477" y1="206" x2="477" y2="252" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="612" y1="206" x2="612" y2="252" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="10" y="248" font-size="13">워커 (ps에 보이는 이름)</text>
  <rect x="10" y="256" width="125" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="72" y="278" text-anchor="middle" font-size="12">kworker/0:1</text>
  <text x="72" y="298" text-anchor="middle" font-size="12">kworker/1:2</text>
  <rect x="145" y="256" width="125" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="207" y="278" text-anchor="middle" font-size="12">kworker/0:1H</text>
  <text x="207" y="298" text-anchor="middle" font-size="12">kworker/1:1H</text>
  <rect x="280" y="256" width="125" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="342" y="278" text-anchor="middle" font-size="12">kworker/u8:1</text>
  <text x="342" y="298" text-anchor="middle" font-size="12">kworker/u8:3</text>
  <rect x="415" y="256" width="125" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="477" y="278" text-anchor="middle" font-size="12">kworker/R-f7_wq</text>
  <text x="477" y="298" text-anchor="middle" font-size="12">평소엔 잠듦</text>
  <rect x="550" y="256" width="125" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="612" y="278" text-anchor="middle" font-size="12">스레드 없음</text>
  <text x="612" y="298" text-anchor="middle" font-size="12">softirq에서 실행</text>
  <text x="340" y="342" text-anchor="middle" font-size="12">CMWQ: 돌던 워커가 잠들면 pool이 다른 워커를 깨워 같은 CPU의 다음 work를 이어 간다</text>
  <text x="340" y="364" text-anchor="middle" font-size="12">돌던 워커가 잠들지 않고 오래 돌면? → 6.7절 (CPU_INTENSIVE 자동 판정, WQ_WATCHDOG)</text>
</svg>
```
그림 2 — CMWQ 구조. workqueue는 이름표이고, 스레드는 pool에 있으며 여러 workqueue가 pool을 공유한다. `f7_wq`는 unbound pool을 쓰고(실선), 메모리가 부족해 새 워커를 못 만들 때만 전용 rescuer를 쓴다(점선)

### 6.3 시스템 워크큐 — 6.18 기준 이름

부팅 때 커널이 미리 만들어 두는 공용 워크큐들이다(랩 소스 `kernel/workqueue.c` 7902~7917행).

| 변수 | 이름 | 플래그 | 언제 |
|---|---|---|---|
| `system_wq` (= `system_percpu_wq`) | events | `WQ_PERCPU` | `schedule_work()`의 기본. 짧은 일 |
| `system_highpri_wq` | events_highpri | `WQ_HIGHPRI`, `WQ_PERCPU` | 지연에 민감한 짧은 일 |
| `system_long_wq` | events_long | `WQ_PERCPU` | 오래 걸릴 수 있는 일 (flush가 오래 걸려도 되는) |
| `system_unbound_wq` (= `system_dfl_wq`) | events_unbound | `WQ_UNBOUND`, max_active 2048 | CPU 무관, 길거나 CPU를 많이 쓰는 일 |
| `system_freezable_wq` | events_freezable | `WQ_FREEZABLE` | suspend 중 멈춰야 하는 일 |
| `system_power_efficient_wq` | events_power_efficient | `WQ_POWER_EFFICIENT` | 전력 우선 (커널 파라미터로 unbound 전환) |
| `system_bh_wq` / `system_bh_highpri_wq` | events_bh / events_bh_highpri | `WQ_BH` | softirq에서 실행 (4.2절) |

**버전 주의**: `system_percpu_wq`, `system_dfl_wq`, `WQ_PERCPU`는 6.17 무렵 새로 들어온 이름이다(이 랩 6.18 소스에 있음을 확인). 헤더에 `system_wq; /* use system_percpu_wq, this will be removed */`라고 적혀 있다. 업스트림은 장기적으로 "플래그를 안 주면 unbound"를 기본값으로 바꾸려 하고, 그래서 per-CPU가 필요하면 `WQ_PERCPU`를 명시하는 쪽으로 옮겨 가는 중이다. 면접에서는 `system_wq`라고 말해도 통하지만, 최신 코드를 읽을 때 새 이름을 알아보면 된다. 6.6 같은 예전 LTS에는 새 이름이 없다.

### 6.4 `alloc_workqueue` — 전용 워크큐가 필요할 때

```c
struct workqueue_struct *alloc_workqueue(const char *fmt, unsigned int flags, int max_active, ...);
```

| 플래그 | 뜻 | 언제 쓰나 |
|---|---|---|
| `WQ_UNBOUND` | 특정 CPU에 묶이지 않음. 스케줄러가 CPU 선택. concurrency management 없음 | 긴 작업, CPU를 오래 쓰는 작업, 큐잉한 CPU를 깨우고 싶지 않을 때(전력) |
| `WQ_HIGHPRI` | highpri pool(nice -20) 사용 | 지연에 민감한 짧은 일 |
| `WQ_MEM_RECLAIM` | rescuer 스레드를 미리 하나 만들어 둠 | 메모리 회수 경로(블록·네트워크 스토리지 드라이버)에서 쓰일 수 있는 일 |
| `WQ_FREEZABLE` | 시스템 suspend 때 멈췄다가 resume 때 재개 | suspend 도중 장치 접근하면 안 되는 일 |
| `WQ_CPU_INTENSIVE` | concurrency management에서 제외 (per-CPU 유지) | CPU를 오래 쓰는 per-CPU 작업 |
| `WQ_SYSFS` | `/sys/bus/workqueue/devices/<이름>` 노출 | 사용자가 cpumask·nice 조정 (unbound만 의미) |
| `WQ_PERCPU` | per-CPU임을 명시 (6.17 무렵 추가) | per-CPU 워크큐를 만들 때 |

- **`max_active`**: 이 워크큐의 work가 (per-CPU 워크큐는 CPU당, unbound는 시스템 전체 기준) 동시에 몇 개까지 실행될 수 있나. `0`을 주면 기본값 `WQ_DFL_ACTIVE`(1024)이고 최대는 `WQ_MAX_ACTIVE`(2048)이다. 8절 출력에서 `f7_wq`의 `max_active 1024`가 이것이다.
- **ordered workqueue**: `alloc_ordered_workqueue(name, flags)`는 "한 번에 하나씩, 넣은 순서대로" 실행을 보장한다(내부적으로 `WQ_UNBOUND | __WQ_ORDERED`, max_active 1). 펌웨어 다운로드 → 설정 → 시작처럼 순서가 중요한 명령 시퀀스를 처리할 때 쓴다. 예전 코드의 `create_singlethread_workqueue()`도 이것으로 매핑된다.
- **rescuer가 왜 필요한가**: 메모리가 바닥나면 새 kworker를 만들 수 없다(스레드 생성도 메모리 할당이다). 그런데 메모리를 풀려면 블록 I/O 완료 처리 같은 work가 돌아야 한다면? 교착이다. `WQ_MEM_RECLAIM` 워크큐는 미리 만들어 둔 rescuer(`kworker/R-<이름>`)가 이런 상황에서 work를 대신 실행해 "앞으로 나아감(forward progress)"을 보장한다. 센서 드라이버라면 보통 필요 없다.

### 6.5 work의 성질과 `delayed_work`

- **같은 work는 동시에 두 번 큐에 들어가지 않는다.** pending 상태에서 다시 `queue_work()`하면 `false`를 돌려주고 아무 일도 하지 않는다(tasklet과 같은 "합쳐짐"). 단, 이미 **실행 중인** work를 다시 큐잉하는 것은 된다. 그러면 실행이 끝난 뒤 한 번 더 돈다.
- per-CPU 워크큐에서는 work 하나가 동시에 두 CPU에서 돌지 않는다(non-reentrant). 그래서 work 함수 안에서 자기 자신과의 경쟁은 걱정하지 않아도 된다.
- **`delayed_work`** = `work_struct` + `timer_list`. `queue_delayed_work(wq, &dw, msecs_to_jiffies(100))`은 타이머를 걸어 두고, 타이머가 만료되면(TIMER softirq) 그때 work를 큐에 넣는다. 폴링 루프("100 ms마다 배터리 게이지 읽기")에 딱 맞다. 이미 걸린 타이머 시간을 바꾸려면 `mod_delayed_work()`.
- **취소와 대기**:

| 함수 | 하는 일 | 돌려주는 값 |
|---|---|---|
| `flush_work(&w)` | 지금 pending이거나 실행 중인 그 work가 **끝날 때까지 기다림** (취소 안 함) | 기다렸으면 true |
| `cancel_work_sync(&w)` | pending이면 **취소**, 실행 중이면 **끝날 때까지 기다림**. 스스로 다시 큐잉하는 work도 막는다 | pending이던 걸 취소했으면 true |
| `cancel_delayed_work_sync(&dw)` | 위와 같고, 타이머까지 제거 | 같음 |
| `cancel_work(&w)` | 취소만 하고 기다리지 않음 (실행 중일 수 있음) | 같음 |
| `disable_work_sync(&w)` | 취소 + 이후 `enable_work()` 전까지 큐잉 자체를 거부 | 같음 |
| `drain_workqueue(wq)` | 그 워크큐가 빌 때까지 반복 flush (체인 큐잉 포함) | 없음 |
| `destroy_workqueue(wq)` | drain 후 워크큐 해제 | 없음 |

`_sync`가 붙은 함수는 **잠든다** → process 컨텍스트에서만 부를 수 있다. 그리고 work 함수 안에서 자기 자신에게 `cancel_work_sync`를 부르면 자기가 끝나길 기다리는 교착이 된다.

### 6.6 예제 8 — `ps`로 보는 kworker 이름, `/sys/bus/workqueue/devices`

무엇을 확인하나: 모듈을 올린 상태에서 스레드 이름과 workqueue sysfs, `/proc/interrupts`, `/proc/softirqs`를 본다.

```sh
# f7.sh의 ps 실험
insmod /f7_defer.ko mode=thread nsamp=1; sleep 1
ps -o pid,comm 2>/dev/null || ps
echo "=== /sys/bus/workqueue/devices ==="; ls /sys/bus/workqueue/devices
echo "=== f7_wq attrs ==="; cd /sys/bus/workqueue/devices/f7_wq && for f in *; do [ -f $f ] && printf "%-14s %s\n" $f "$(cat $f 2>&1)"; done
echo "=== writeback attrs ==="; cd /sys/bus/workqueue/devices/writeback && for f in *; do [ -f $f ] && printf "%-14s %s\n" $f "$(cat $f 2>&1)"; done
echo "=== interrupts ==="; grep -E "CPU0|f7_defer" /proc/interrupts
echo "=== softirqs ==="; cat /proc/softirqs
```

```text
PID   COMMAND
    1 init
    2 kthreadd
    3 pool_workqueue_
    4 kworker/R-rcu_g
    5 kworker/R-sync_
...
    9 kworker/0:0-pm
   10 kworker/0:1-rcu
   11 kworker/0:0H-ev
   12 kworker/u8:0-ev
   13 kworker/R-mm_pe
   14 ksoftirqd/0
   15 rcu_preempt
...
   22 ksoftirqd/1
   23 kworker/1:0-pm
   24 kworker/1:0H-ev
...
   34 kworker/u8:1-ev
   35 kworker/R-write
...
   46 kworker/u8:2
   47 kworker/u8:3-ev
   48 kworker/1:1-pm
...
   50 kworker/0:1H
...
   55 kworker/u9:0
...
   60 kworker/0:2
   61 kworker/1:1H
   72 sh
   74 kworker/R-f7_wq
   75 f7_kthread
   76 f7_kworker
   77 irq/22-f7_defer
   78 f7_gen
   80 ps
=== /sys/bus/workqueue/devices ===
blkcg_punt_bio  f7_wq           writeback
=== f7_wq attrs ===
affinity_scope default (cache)
affinity_strict 0
cpumask        3
max_active     1024
nice           0
per_cpu        0
uevent         
=== interrupts ===
           CPU0       CPU1       
 22:          1          0 GIC-0 112 Edge      f7_defer
=== softirqs ===
                    CPU0       CPU1       
          HI:          0          0
       TIMER:        184        211
      NET_TX:          0          0
      NET_RX:          0          0
       BLOCK:          0          0
    IRQ_POLL:          0          0
     TASKLET:          1          0
       SCHED:        616        550
     HRTIMER:          0          0
         RCU:        418        418
```

(`...`는 kworker가 아닌 커널 스레드 줄을 생략한 곳. `writeback attrs` 출력은 `f7_wq`와 값이 똑같아 생략했다. `ls`의 색 escape 코드는 지웠다. PID는 실행마다 다르다)

출력에서 볼 것:

- **이름 규칙** (소스 `kernel/workqueue.c`의 `"kworker/%d:%d%s"`, `"kworker/u%d:%d"`, `"kworker/R-%s"`): `kworker/<CPU>:<워커번호>` = per-CPU normal pool, 뒤에 `H`가 붙으면 highpri pool(nice < 0), `kworker/u<pool id>:<워커번호>` = unbound pool, `kworker/R-<워크큐 이름>` = rescuer.
- `-pm`, `-ev`, `-rcu` 꼬리는 그 워커가 **마지막으로 처리한 워크큐 이름**이다(`wq_worker_comm()`: 지금 실행 중이면 `+`, 끝났으면 `-`. `events` → `ev`는 busybox `ps`의 15자 표시 제한으로 잘린 것). "어떤 kworker가 CPU를 먹는다"는 문제를 볼 때 이 꼬리가 첫 단서다.
- `kworker/R-f7_wq`: 우리 `WQ_MEM_RECLAIM` 때문에 생긴 rescuer. `irq/22-f7_defer`: threaded IRQ의 스레드(8절). 이 둘과 `f7_kthread`, `f7_kworker`는 모듈을 내리면 사라진다.
- `/sys/bus/workqueue/devices`에는 **`WQ_SYSFS`를 준 unbound 워크큐만** 나온다(`writeback`, `blkcg_punt_bio`, 그리고 우리 `f7_wq`). `cpumask 3`(CPU0·1), `max_active 1024`(`0`을 줬을 때의 기본값), `per_cpu 0`(unbound). 여기에 `echo 1 > cpumask`를 쓰면 이 워크큐의 일을 CPU0에만 보낼 수 있다(코어 격리 튜닝).
- `GIC-0 112 Edge`: DT의 SPI 80 + 32 = hwirq 112, rising edge. Linux IRQ 번호(virq)는 22.
- `TASKLET: 1`: 부팅 이후 TASKLET softirq가 CPU0에서 한 번 돌았다. 이 커널에서 tasklet/BH work를 쓰는 곳이 거의 없다는 뜻이다.

### 6.7 예제 9 — 잠들지 않고 오래 도는 work: CPU_INTENSIVE 자동 판정과 `WQ_WATCHDOG`

6.2절의 concurrency management에는 함정이 있다. "돌고 있는 워커가 있으면 새 워커를 안 깨운다"는 규칙 때문에, **잠들지 않고 오래 도는 work 하나가 같은 CPU pool의 다른 work를 전부 막을 수 있다.** 이걸 확인하기 위해 `f7_teardown.c`에 `hog_s` 파라미터를 넣었다(11절에서 같은 파일을 teardown 실험에도 쓴다).

```c
static int hog_s;			/* >0이면 per-CPU 워커에서 hog_s초 동안 잠들지 않고 돈다 */
module_param(hog_s, int, 0444);

static void f7_victim_fn(struct work_struct *w)
{
	pr_info("f7_td: victim 실행 cpu=%d (jiffies=%lu)\n", raw_smp_processor_id(), jiffies);
}
static DECLARE_WORK(f7_victim, f7_victim_fn);

static void f7_hog_fn(struct work_struct *w)
{
	unsigned long end = jiffies + hog_s * HZ;

	pr_info("f7_td: hog 시작 cpu=%d\n", raw_smp_processor_id());
	queue_work_on(raw_smp_processor_id(), system_percpu_wq, &f7_victim);	/* 같은 pool 뒤에 줄 세움 */
	while (time_before(jiffies, end))
		mdelay(10);			/* 잠들지 않음 → CMWQ는 이 워커가 "일하는 중"이라고 본다 */
	pr_info("f7_td: hog 끝 (jiffies=%lu)\n", jiffies);
}
static DECLARE_WORK(f7_hog, f7_hog_fn);
/* init: if (hog_s) queue_work_on(0, system_percpu_wq, &f7_hog);
   exit: if (hog_s) { cancel_work_sync(&f7_hog); cancel_work_sync(&f7_victim); } */
```

게스트 스크립트는 현재 파라미터 값을 찍고, 모듈을 올리고, hog가 끝날 때까지 기다린 뒤 dmesg를 거른다.

```sh
# f7.sh의 hog 실험
echo "cpu_intensive_thresh_us=$(cat /sys/module/workqueue/parameters/cpu_intensive_thresh_us) watchdog_thresh=$(cat /sys/module/workqueue/parameters/watchdog_thresh)"
insmod /f7_teardown.ko hog_s=$2; sleep $(($2+2)); rmmod f7_teardown; dmesg | grep -E "f7_td|workqueue|BUG|pool|in-flight|pending|hogged"
```

먼저 기본 설정으로(워치독 임계값만 3초로 낮춤):

```sh
KARGS="quiet workqueue.watchdog_thresh=3" TMO=90 sh /Users/donh/.elinux_scratch/F7/boot.sh "hog 8" 2>&1 | grep -v "DMA: prealloc" | tail -20
```

```text
lab initramfs: 6.18.55-dbg up 5.96s
cpu_intensive_thresh_us=1000000 watchdog_thresh=3
[    0.000000] Kernel command line: console=ttyAMA0 quiet workqueue.watchdog_thresh=3 LAB_CMD=sh~/f7.sh~hog~8
[    6.400719] f7_td: hog 시작 cpu=0
[    7.447154] f7_td: victim 실행 cpu=0 (jiffies=4294894113)
[   14.407127] f7_td: hog 끝 (jiffies=4294895853)
```

victim이 hog가 끝나길(8초) 기다리지 않고 **약 1초 뒤** 돌았다. 6.5 무렵부터 커널은 `wq_worker_tick()`에서 "잠들지 않고 `cpu_intensive_thresh_us` 이상 돈 워커"를 자동으로 CPU_INTENSIVE로 표시하고 concurrency management에서 빼낸 뒤, pool이 다른 워커를 깨우게 한다. 기본값은 10 ms인데, 소스(`wq_cpu_intensive_thresh_init`)가 BogoMIPS 4000 미만인 느린 CPU에서는 최대 1초까지 늘린다. QEMU TCG 게스트는 BogoMIPS가 낮아서 **1000000 µs(1초)**가 찍혔고, 그래서 victim이 약 1초 뒤에 돌았다.

이번에는 자동 판정을 끄고(`cpu_intensive_thresh_us=0`) 같은 실험을 한다.

```sh
KARGS="quiet workqueue.watchdog_thresh=3 workqueue.cpu_intensive_thresh_us=0" TMO=90 sh /Users/donh/.elinux_scratch/F7/boot.sh "hog 8" 2>&1 | grep -v "DMA: prealloc" | tail -60
```

```text
lab initramfs: 6.18.55-dbg up 3.83s
cpu_intensive_thresh_us=0 watchdog_thresh=3
[    9.655055] BUG: workqueue lockup - pool cpus=0 node=0 flags=0x0 nice=0 stuck for 5s!
[    0.000000] Kernel command line: console=ttyAMA0 quiet workqueue.watchdog_thresh=3 workqueue.cpu_intensive_thresh_us=0 LAB_CMD=sh~/f7.sh~hog~8
[    4.214116] f7_td: hog 시작 cpu=0
[    9.655055] BUG: workqueue lockup - pool cpus=0 node=0 flags=0x0 nice=0 stuck for 5s!
[    9.655850] Showing busy workqueues and worker pools:
[    9.655939] workqueue events: flags=0x100
[    9.656195]     in-flight: 60:f7_hog_fn [f7_teardown]
[    9.656851]     pending: f7_victim_fn [f7_teardown], vmstat_shepherd
[    9.657336] workqueue mm_percpu_wq: flags=0x8
[    9.657409]     pending: vmstat_update
[    9.657454] workqueue pm: flags=0x4
[    9.657530]     pending: pm_runtime_work
[    9.657708] pool 2: cpus=0 node=0 flags=0x0 nice=0 hung=5s workers=3 idle: 10 9
[    9.657926] Showing backtraces of running workers in stalled CPU-bound worker pools:
[    9.658012] pool 2:
[   12.213163] f7_td: hog 끝 (jiffies=4294895295)
[   12.213445] f7_td: victim 실행 cpu=0 (jiffies=4294895295)
```

출력에서 볼 것 (첫 BUG 줄은 콘솔에 바로 찍힌 것이고, 그 아래는 dmesg에서 다시 거른 것이다):

- **`BUG: workqueue lockup - pool cpus=0 ... stuck for 5s!`**: `CONFIG_WQ_WATCHDOG`가 "CPU0 normal pool이 5초 동안 앞으로 나아가지 못했다"고 잡았다.
- **`in-flight: 60:f7_hog_fn`**: 범인. PID 60 워커가 실행 중.
- **`pending: f7_victim_fn [f7_teardown], vmstat_shepherd`**: 피해자들. 우리 victim뿐 아니라 **커널의 `vmstat_shepherd`, `vmstat_update`, `pm_runtime_work`까지** 같은 CPU의 시스템 work가 줄줄이 막혔다. 드라이버 하나가 `system_wq`에서 바쁜 대기를 하면 시스템 전체가 영향을 받는다는 증거다.
- `idle: 10 9`: 놀고 있는 워커가 두 개 있는데도 깨우지 않았다. concurrency management가 "돌고 있는 워커가 있으니 충분하다"고 판단했기 때문이다.
- victim은 hog가 끝난 **같은 jiffy**에 바로 돌았다.

교훈: workqueue 함수 안에서 오래 걸리는 일은 **잠들면서**(`msleep`, `wait_event`, `usleep_range`) 하거나, CPU를 오래 쓰는 일이면 `WQ_UNBOUND`(또는 `WQ_CPU_INTENSIVE`) 워크큐로 보낸다. `mdelay` 같은 바쁜 대기는 workqueue에서도 나쁘다. `CONFIG_WQ_CPU_INTENSIVE_REPORT`를 켜면 "`<함수> hogged CPU for >Nus N times, consider switching to WQ_UNBOUND`" 경고로 범인을 알려 주는데, 이 랩 dbg 커널은 그 옵션이 꺼져 있어 확인하지 못했다.

---

## 7. kthread와 kthread_worker — 내 스레드가 필요할 때

### 7.1 언제 workqueue 대신 kthread인가

workqueue는 공유 스레드라 "내 일만을 위한 스케줄링 정책"을 줄 수 없다. 다음이 필요하면 자기 스레드를 만든다.

- **RT 우선순위**: 모터 제어 루프, 오디오 버퍼 공급처럼 `SCHED_FIFO`가 필요한 일. `sched_set_fifo(task)`(우선순위 50) 또는 `sched_set_fifo_low(task)`.
- **CPU 고정**: `kthread_bind()`나 `set_cpus_allowed_ptr()`로 특정 코어에 묶기(예: 격리한 코어에서 센서 융합).
- **계속 도는 루프**: "데이터가 오면 처리하고 다시 기다린다"를 영원히 반복하는 일. 매번 work를 큐잉하는 것보다 자연스럽다.

### 7.2 kthread 기본형 — 2.3절 `f7_kthread_fn`

- `kthread_run(fn, data, "이름")`: 스레드를 만들고 바로 깨운다. 실패하면 `ERR_PTR`을 돌려주므로 `IS_ERR()`로 검사한다(NULL이 아니다).
- 스레드 함수는 `while (!kthread_should_stop())` 루프를 돈다. 할 일이 없으면 `wait_event_interruptible()`로 잠들고, 깨우는 쪽(top half)은 플래그를 세우고 `wake_up()`한다. 대기 조건에 `kthread_should_stop()`을 **반드시 같이 넣는다.** 안 넣으면 `kthread_stop()`이 깨워도 조건이 거짓이라 다시 잠들고, `kthread_stop()`은 영원히 기다린다.
- `kthread_stop(task)`: 스레드에 "멈춰" 표시를 하고 깨운 뒤, 스레드 함수가 return 할 때까지 **기다린다**. 그래서 스레드 함수가 먼저 return 해 버리면 안 된다(2.4절 발생기의 마지막 `while` 루프가 그 이유). 먼저 끝날 수 있는 스레드라면 `get_task_struct()`로 참조를 잡아 두어야 한다.
- 2.3절 코드의 `atomic_xchg(&p->kt_pending, 0)`는 "이벤트 플래그를 읽으면서 지운다"를 원자적으로 한다. RTOS의 event flag clear-on-read와 같다. 이벤트 두 개가 처리 전에 겹치면 하나로 합쳐진다(tasklet·work와 같은 성질).

### 7.3 kthread_worker — "내 전용 workqueue"

kthread를 직접 쓰면 대기·깨우기·종료 코드를 매번 짜야 한다. `kthread_worker`는 그 보일러플레이트를 없앤 것이다. **스레드 하나 + work 목록**이고, API가 workqueue와 거의 같다.

| workqueue | kthread_worker |
|---|---|
| `alloc_workqueue(...)` | `kthread_run_worker(0, "이름")` (6.18) |
| `INIT_WORK(&w, fn)` | `kthread_init_work(&kw, fn)` |
| `queue_work(wq, &w)` | `kthread_queue_work(worker, &kw)` |
| `queue_delayed_work(...)` | `kthread_queue_delayed_work(...)` |
| `cancel_work_sync(&w)` | `kthread_cancel_work_sync(&kw)` |
| `destroy_workqueue(wq)` | `kthread_destroy_worker(worker)` |

스레드가 하나이므로 work는 항상 **순서대로 하나씩** 실행된다(ordered). 그리고 스레드 하나가 `worker->task`로 노출되므로 `sched_set_fifo(worker->task)`로 RT 우선순위를 줄 수 있다. 커널의 SPI 코어가 메시지 펌프에 이것을 쓴다(`spi_controller`의 `rt` 옵션이 켜지면 그 워커를 RT로 올린다). 5.1절 출력의 `comm=f7_kworker`가 우리 kthread_worker 스레드다.

---

## 8. threaded IRQ — "ISR이 잠들어야 한다"의 현대적인 답

자세한 메커니즘은 F6에서 다룬다. 여기서는 deferred work 관점에서만 본다.

```c
ret = request_threaded_irq(irq, f7_hardirq, f7_thread_fn, IRQF_ONESHOT, "f7_defer", p);
```

- `f7_hardirq`(top half)가 `IRQ_WAKE_THREAD`를 돌려주면, 커널이 IRQ 전용 스레드 `irq/<virq>-<이름>`을 깨워 `f7_thread_fn`을 실행한다. top half를 `NULL`로 주면 커널 기본 핸들러가 무조건 스레드를 깨운다.
- 스레드는 `SCHED_FIFO` 우선순위 50으로 만들어진다(`kernel/irq/manage.c`의 `sched_set_fifo(current)`, `MAX_RT_PRIO / 2`). 10절 ftrace에 `prio=49`로 찍히는 것이 이것이다(커널 내부 prio = 99 − 50). 그래서 일반 스레드(nice 0의 kworker, kthread)보다 **항상 먼저** 돈다.
- **`IRQF_ONESHOT`**: 스레드 함수가 끝날 때까지 IRQ 라인을 **마스크한 채로** 둔다. 레벨 트리거 센서 인터럽트(장치가 상태 레지스터를 읽어야 라인을 내림)에서 필수다. 이게 없으면 top half가 끝나자마자 같은 레벨 IRQ가 다시 들어와 무한 반복한다. top half를 `NULL`로 주면 커널이 `IRQF_ONESHOT`을 요구한다.
- **왜 workqueue보다 나은가**: (1) IRQ 하나에 스레드 하나가 1:1로 붙어 있어 다른 드라이버의 work에 밀리지 않는다. (2) RT 우선순위다. (3) ONESHOT으로 IRQ 마스킹을 커널이 대신 관리한다. (4) `/proc/irq/<n>/smp_affinity`를 바꾸면 스레드도 따라 움직인다. 9절 측정에서도 부하 중 p90이 workqueue보다 확실히 낮다.
- I2C/SPI 센서 드라이버(IIO의 많은 IMU 드라이버)는 이 패턴을 쓴다: top half에서 `iio_get_time_ns()` 같은 타임스탬프만 찍고, 스레드에서 `regmap_bulk_read()`로 FIFO를 읽는다.
- 부팅 인자 `threadirqs`를 주면 `IRQF_NO_THREAD`가 아닌 거의 모든 IRQ 핸들러가 강제로 스레드화된다(`CONFIG_IRQ_FORCED_THREADING`, 이 dbg 커널은 `=y`). PREEMPT_RT 커널은 이게 기본이고, 그 경우 softirq도 대부분 스레드 컨텍스트로 옮겨 간다.

---

## 9. 지연 측정 — 각 방식은 실제로 얼마나 늦게 도나

### 9.1 실험 방법

`f7.sh lat` 실험은 8개 모드를 각각 200샘플씩 유휴 상태에서 재고, 셸 busy loop 2개(`-smp 2`이므로 두 CPU를 모두 100% 점유, nice 0)를 띄운 뒤 다시 잰다.

```sh
# f7.sh의 lat 실험
for m in $M; do run $m raw=1; done
for i in 1 2; do (while :; do :; done) & done
sleep 1; echo "=== load on ==="
for m in $M; do run $m raw=1; done
kill %1 %2 2>/dev/null
dmesg | grep -E "RESULT|f7raw|ctx\["
```

예제 10 — 무엇을 확인하나: 8가지 bottom half의 IRQ → 시작 지연 분포, 유휴 vs CPU 부하.

```sh
sh /Users/donh/.elinux_scratch/F7/boot.sh lat > /var/tmp/F7/lat.log 2>&1
grep -E "load on|RESULT" /var/tmp/F7/lat.log
```

```text
=== load on ===
[   33.930469] f7_defer f7_dev: RESULT mode=tasklet n=200 miss=0 ns: min=27840 p50=53120 p90=90800 p99=1880304 max=5757376
[   36.530530] f7_defer f7_dev: RESULT mode=bhwq n=200 miss=0 ns: min=50576 p50=92960 p90=191376 p99=1791760 max=1811344
[   39.425129] f7_defer f7_dev: RESULT mode=wq n=200 miss=0 ns: min=122416 p50=286128 p90=869376 p99=9764192 max=61019328
[   42.905466] f7_defer f7_dev: RESULT mode=hipri n=200 miss=0 ns: min=121248 p50=249008 p90=881632 p99=7511680 max=25125792
[   45.686286] f7_defer f7_dev: RESULT mode=unbound n=200 miss=0 ns: min=130464 p50=429136 p90=2022304 p99=10820448 max=16012864
[   49.014731] f7_defer f7_dev: RESULT mode=kthread n=200 miss=0 ns: min=85008 p50=382000 p90=2850944 p99=13959088 max=28705440
[   51.852123] f7_defer f7_dev: RESULT mode=kwork n=200 miss=0 ns: min=121088 p50=417168 p90=2732304 p99=10833072 max=17479824
[   54.847453] f7_defer f7_dev: RESULT mode=thread n=200 miss=0 ns: min=81456 p50=177872 p90=945792 p99=14217840 max=15463440
[   65.577485] f7_defer f7_dev: RESULT mode=tasklet n=200 miss=0 ns: min=34672 p50=63712 p90=132032 p99=4482416 max=5664704
[   72.479796] f7_defer f7_dev: RESULT mode=bhwq n=200 miss=0 ns: min=71552 p50=125072 p90=293088 p99=10845104 max=85125584
[   85.151848] f7_defer f7_dev: RESULT mode=wq n=200 miss=0 ns: min=151088 p50=430336 p90=12710464 p99=99150336 max=144121728
[   93.950203] f7_defer f7_dev: RESULT mode=hipri n=200 miss=0 ns: min=133296 p50=256704 p90=1355216 p99=13103216 max=20679760
[  100.072923] f7_defer f7_dev: RESULT mode=unbound n=200 miss=0 ns: min=159008 p50=333328 p90=5202192 p99=30596960 max=97743536
[  105.711843] f7_defer f7_dev: RESULT mode=kthread n=200 miss=0 ns: min=122752 p50=401168 p90=4316528 p99=26082688 max=100752176
[  109.527332] f7_defer f7_dev: RESULT mode=kwork n=200 miss=0 ns: min=121280 p50=215456 p90=2168848 p99=8907264 max=13608096
[  114.511503] f7_defer f7_dev: RESULT mode=thread n=200 miss=0 ns: min=100912 p50=169248 p90=565056 p99=5107120 max=13302336
```

출력 읽는 법: `=== load on ===`은 셸이 바로 찍은 줄이고 RESULT는 마지막 `dmesg`에서 나왔기 때문에 순서가 뒤집혀 보인다. **위 8줄이 유휴, 아래 8줄(65초 이후)이 부하**다. 단위는 ns. `miss=0`은 200번 모두 1초 안에 bottom half가 돌았다는 뜻이다. 원본 샘플(`f7raw` 160줄)은 생략했고, 아래 차트는 그 원본에서 Python으로 백분위를 계산해 그렸다.

### 9.2 분포 차트

```svg
<svg viewBox="0 0 680 476" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="18" text-anchor="middle" font-size="14">IRQ → bottom half 지연 분포 (n=200, 실행 1, 로그 축)</text>
<line x1="170" y1="38" x2="200" y2="38" stroke="#4a7bd0" stroke-width="6" stroke-opacity="0.6"/><text x="206" y="42" font-size="12">유휴</text>
<line x1="260" y1="38" x2="290" y2="38" stroke="#e08a3c" stroke-width="6" stroke-opacity="0.6"/><text x="296" y="42" font-size="12">CPU 부하 (busy loop 2개)</text>
<text x="170" y="60" font-size="12">막대 = p10~p90 · 점 = p50 · 짧은 세로선 = p99</text>
<line x1="170.0" y1="70" x2="170.0" y2="426" stroke="currentColor" stroke-opacity="0.15"/>
<text x="170.0" y="444" text-anchor="middle" font-size="12">10 µs</text>
<line x1="281.6" y1="70" x2="281.6" y2="426" stroke="currentColor" stroke-opacity="0.15"/>
<text x="281.6" y="444" text-anchor="middle" font-size="12">100 µs</text>
<line x1="393.2" y1="70" x2="393.2" y2="426" stroke="currentColor" stroke-opacity="0.15"/>
<text x="393.2" y="444" text-anchor="middle" font-size="12">1 ms</text>
<line x1="504.8" y1="70" x2="504.8" y2="426" stroke="currentColor" stroke-opacity="0.15"/>
<text x="504.8" y="444" text-anchor="middle" font-size="12">10 ms</text>
<line x1="616.4" y1="70" x2="616.4" y2="426" stroke="currentColor" stroke-opacity="0.15"/>
<text x="616.4" y="444" text-anchor="middle" font-size="12">100 ms</text>
<line x1="170" y1="426" x2="650" y2="426" stroke="currentColor"/>
<text x="410" y="464" text-anchor="middle" font-size="12">top half가 찍은 시각 → bottom half 시작 시각 (로그 축)</text>
<text x="160" y="96" text-anchor="end" font-size="13">tasklet</text>
<line x1="227.8" y1="86" x2="276.9" y2="86" stroke="#4a7bd0" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="250.9" cy="86" r="4" fill="#4a7bd0"/>
<line x1="423.8" y1="81" x2="423.8" y2="91" stroke="#4a7bd0" stroke-width="2"/>
<line x1="243.9" y1="100" x2="295.1" y2="100" stroke="#e08a3c" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="259.8" cy="100" r="4" fill="#e08a3c"/>
<line x1="465.9" y1="95" x2="465.9" y2="105" stroke="#e08a3c" stroke-width="2"/>
<text x="160" y="140" text-anchor="end" font-size="13">BH wq</text>
<line x1="256.8" y1="130" x2="313.1" y2="130" stroke="#4a7bd0" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="278.1" cy="130" r="4" fill="#4a7bd0"/>
<line x1="421.5" y1="125" x2="421.5" y2="135" stroke="#4a7bd0" stroke-width="2"/>
<line x1="275.9" y1="144" x2="333.7" y2="144" stroke="#e08a3c" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="292.4" cy="144" r="4" fill="#e08a3c"/>
<line x1="508.7" y1="139" x2="508.7" y2="149" stroke="#e08a3c" stroke-width="2"/>
<text x="160" y="184" text-anchor="end" font-size="13">system_wq</text>
<line x1="302.6" y1="174" x2="386.4" y2="174" stroke="#4a7bd0" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="332.6" cy="174" r="4" fill="#4a7bd0"/>
<line x1="503.6" y1="169" x2="503.6" y2="179" stroke="#4a7bd0" stroke-width="2"/>
<line x1="323.0" y1="188" x2="516.4" y2="188" stroke="#e08a3c" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="352.3" cy="188" r="4" fill="#e08a3c"/>
<line x1="616.0" y1="183" x2="616.0" y2="193" stroke="#e08a3c" stroke-width="2"/>
<text x="160" y="228" text-anchor="end" font-size="13">highpri wq</text>
<line x1="301.2" y1="218" x2="387.1" y2="218" stroke="#4a7bd0" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="325.8" cy="218" r="4" fill="#4a7bd0"/>
<line x1="490.9" y1="213" x2="490.9" y2="223" stroke="#4a7bd0" stroke-width="2"/>
<line x1="311.6" y1="232" x2="407.9" y2="232" stroke="#e08a3c" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="327.3" cy="232" r="4" fill="#e08a3c"/>
<line x1="517.9" y1="227" x2="517.9" y2="237" stroke="#e08a3c" stroke-width="2"/>
<text x="160" y="272" text-anchor="end" font-size="13">unbound wq</text>
<line x1="315.4" y1="262" x2="427.3" y2="262" stroke="#4a7bd0" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="352.2" cy="262" r="4" fill="#4a7bd0"/>
<line x1="508.6" y1="257" x2="508.6" y2="267" stroke="#4a7bd0" stroke-width="2"/>
<line x1="317.9" y1="276" x2="473.1" y2="276" stroke="#e08a3c" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="340.0" cy="276" r="4" fill="#e08a3c"/>
<line x1="559.0" y1="271" x2="559.0" y2="281" stroke="#e08a3c" stroke-width="2"/>
<text x="160" y="316" text-anchor="end" font-size="13">kthread</text>
<line x1="297.7" y1="306" x2="444.0" y2="306" stroke="#4a7bd0" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="346.6" cy="306" r="4" fill="#4a7bd0"/>
<line x1="521.0" y1="301" x2="521.0" y2="311" stroke="#4a7bd0" stroke-width="2"/>
<line x1="305.9" y1="320" x2="464.1" y2="320" stroke="#e08a3c" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="348.9" cy="320" r="4" fill="#e08a3c"/>
<line x1="551.3" y1="315" x2="551.3" y2="325" stroke="#e08a3c" stroke-width="2"/>
<text x="160" y="360" text-anchor="end" font-size="13">kthread_worker</text>
<line x1="329.7" y1="350" x2="441.9" y2="350" stroke="#4a7bd0" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="350.8" cy="350" r="4" fill="#4a7bd0"/>
<line x1="508.7" y1="345" x2="508.7" y2="355" stroke="#4a7bd0" stroke-width="2"/>
<line x1="302.4" y1="364" x2="430.7" y2="364" stroke="#e08a3c" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="318.8" cy="364" r="4" fill="#e08a3c"/>
<line x1="499.2" y1="359" x2="499.2" y2="369" stroke="#e08a3c" stroke-width="2"/>
<text x="160" y="404" text-anchor="end" font-size="13">threaded IRQ</text>
<line x1="282.2" y1="394" x2="390.5" y2="394" stroke="#4a7bd0" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="309.5" cy="394" r="4" fill="#4a7bd0"/>
<line x1="521.9" y1="389" x2="521.9" y2="399" stroke="#4a7bd0" stroke-width="2"/>
<line x1="288.0" y1="408" x2="365.5" y2="408" stroke="#e08a3c" stroke-width="6" stroke-opacity="0.6"/>
<circle cx="307.1" cy="408" r="4" fill="#e08a3c"/>
<line x1="472.2" y1="403" x2="472.2" y2="413" stroke="#e08a3c" stroke-width="2"/>
</svg>
```
그림 3 — 방식별 IRQ → bottom half 지연 분포 (실행 1). 파랑 = 유휴, 주황 = 두 CPU 모두 busy loop. 막대가 p10~p90, 점이 중앙값, 짧은 세로선이 p99

TCG 에뮬레이션과 다른 노트 작성자들이 같은 VM을 동시에 쓰는 환경이라 꼬리(p99, max)는 잡음이 크다. 그래서 같은 실험을 한 번 더 돌려 **부하 중 p90**만 비교했다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="18" text-anchor="middle" font-size="14">CPU 부하 중 p90 지연 — 같은 실험 두 번 (로그 축)</text>
<rect x="250" y="28" width="12" height="12" fill="#e08a3c" fill-opacity="0.75"/><text x="268" y="39" font-size="12">실행 1</text>
<rect x="340" y="28" width="12" height="12" fill="#d0564a" fill-opacity="0.75"/><text x="358" y="39" font-size="12">실행 2</text>
<line x1="80" y1="270.0" x2="660" y2="270.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="74" y="274.0" text-anchor="end" font-size="12">10 µs</text>
<line x1="80" y1="215.0" x2="660" y2="215.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="74" y="219.0" text-anchor="end" font-size="12">100 µs</text>
<line x1="80" y1="160.0" x2="660" y2="160.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="74" y="164.0" text-anchor="end" font-size="12">1 ms</text>
<line x1="80" y1="105.0" x2="660" y2="105.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="74" y="109.0" text-anchor="end" font-size="12">10 ms</text>
<line x1="80" y1="50.0" x2="660" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="74" y="54.0" text-anchor="end" font-size="12">100 ms</text>
<line x1="80" y1="270" x2="660" y2="270" stroke="currentColor"/>
<rect x="94.2" y="208.4" width="20" height="61.6" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="118.2" y="210.6" width="20" height="59.4" fill="#d0564a" fill-opacity="0.75"/>
<text x="116.2" y="288" text-anchor="middle" font-size="12">tasklet</text>
<text x="116.2" y="304" text-anchor="middle" font-size="12">132µs</text>
<text x="116.2" y="320" text-anchor="middle" font-size="12">120µs</text>
<rect x="166.8" y="189.3" width="20" height="80.7" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="190.8" y="183.8" width="20" height="86.2" fill="#d0564a" fill-opacity="0.75"/>
<text x="188.8" y="288" text-anchor="middle" font-size="12">BH wq</text>
<text x="188.8" y="304" text-anchor="middle" font-size="12">293µs</text>
<text x="188.8" y="320" text-anchor="middle" font-size="12">370µs</text>
<rect x="239.2" y="99.3" width="20" height="170.7" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="263.2" y="105.0" width="20" height="165.0" fill="#d0564a" fill-opacity="0.75"/>
<text x="261.2" y="288" text-anchor="middle" font-size="12">system_wq</text>
<text x="261.2" y="304" text-anchor="middle" font-size="12">12.7ms</text>
<text x="261.2" y="320" text-anchor="middle" font-size="12">10.0ms</text>
<rect x="311.8" y="152.7" width="20" height="117.3" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="335.8" y="128.6" width="20" height="141.4" fill="#d0564a" fill-opacity="0.75"/>
<text x="333.8" y="288" text-anchor="middle" font-size="12">highpri</text>
<text x="333.8" y="304" text-anchor="middle" font-size="12">1.4ms</text>
<text x="333.8" y="320" text-anchor="middle" font-size="12">3.7ms</text>
<rect x="384.2" y="120.6" width="20" height="149.4" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="408.2" y="91.7" width="20" height="178.3" fill="#d0564a" fill-opacity="0.75"/>
<text x="406.2" y="288" text-anchor="middle" font-size="12">unbound</text>
<text x="406.2" y="304" text-anchor="middle" font-size="12">5.2ms</text>
<text x="406.2" y="320" text-anchor="middle" font-size="12">17.4ms</text>
<rect x="456.8" y="125.1" width="20" height="144.9" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="480.8" y="122.3" width="20" height="147.7" fill="#d0564a" fill-opacity="0.75"/>
<text x="478.8" y="288" text-anchor="middle" font-size="12">kthread</text>
<text x="478.8" y="304" text-anchor="middle" font-size="12">4.3ms</text>
<text x="478.8" y="320" text-anchor="middle" font-size="12">4.8ms</text>
<rect x="529.2" y="141.5" width="20" height="128.5" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="553.2" y="162.3" width="20" height="107.7" fill="#d0564a" fill-opacity="0.75"/>
<text x="551.2" y="288" text-anchor="middle" font-size="12">kt_worker</text>
<text x="551.2" y="304" text-anchor="middle" font-size="12">2.2ms</text>
<text x="551.2" y="320" text-anchor="middle" font-size="12">909µs</text>
<rect x="601.8" y="173.6" width="20" height="96.4" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="625.8" y="177.5" width="20" height="92.5" fill="#d0564a" fill-opacity="0.75"/>
<text x="623.8" y="288" text-anchor="middle" font-size="12">threaded</text>
<text x="623.8" y="304" text-anchor="middle" font-size="12">565µs</text>
<text x="623.8" y="320" text-anchor="middle" font-size="12">480µs</text>
</svg>
```
그림 4 — CPU 부하 중 p90 지연을 두 번 측정한 결과. 막대 아래 숫자는 위가 실행 1, 아래가 실행 2

### 9.3 읽는 법 — 숫자가 말하는 것

1. **softirq 계열(tasklet, BH wq)이 가장 빠르고, 부하에 거의 흔들리지 않는다.** 중앙값 53~125 µs, 부하 중 p90도 0.1~0.4 ms. hardirq 탈출 경로에서 바로 돌기 때문에 스케줄러를 거치지 않는다. 부하가 있든 없든 "IRQ가 들어온 그 순간 CPU를 쓰던 태스크"의 몸을 빌리면 그만이다. BH wq가 tasklet보다 수십 µs 느린 것은 workqueue 기계장치(`process_one_work`, lockdep 주석 등)를 한 겹 더 거치기 때문으로 보인다(dbg 커널은 lockdep이 켜져 있어 이 차이가 실제보다 클 수 있다).
2. **nice 0 스레드(system_wq, unbound, kthread)는 부하가 걸리면 꼬리가 무너진다.** `system_wq`의 부하 중 p90이 두 번 다 10 ms가 넘었다(12.7 ms, 10.0 ms). busy loop도 nice 0이므로 kworker는 EEVDF 스케줄러에서 "공평한 몫"을 기다려야 하고, `HZ=250`이면 틱이 4 ms라 몇 틱 밀리면 바로 10 ms다. 센서 키트에서 "부하가 걸리면 IMU 샘플이 가끔 10 ms씩 늦는다"는 증상의 정체가 이것이다.
3. **highpri(nice −20)는 중간**: 부하 중 p90 1.4 ms / 3.7 ms. nice −20은 가중치가 커서 busy loop보다 훨씬 자주 CPU를 받지만, 여전히 CFS/EEVDF 클래스라 RT처럼 "즉시"는 아니다.
4. **threaded IRQ(SCHED_FIFO 50)는 부하에도 p90이 0.5~0.6 ms로 유지된다.** RT 클래스는 nice 0 태스크를 깨어나는 즉시 선점한다. 잠들 수 있으면서 지연도 일정한, **"I2C 센서 읽기"에 가장 맞는 자리**라는 것을 숫자로 확인했다.
5. **kthread_worker(nice 0)**가 kthread보다 나은 것은 우연에 가깝다. 둘 다 nice 0이고, 우선순위를 올리지 않으면 `system_wq`와 같은 부류다. 전용 스레드의 장점은 `sched_set_fifo()`로 올릴 **수 있다**는 것이지, 만들기만 하면 빨라지는 것이 아니다.
6. **절대값은 믿지 말 것**: QEMU TCG에서 잰 숫자다. 실제 Cortex-A53/A72 보드라면 tasklet 지연은 수 µs, threaded IRQ는 수십 µs 수준이 일반적이다(이 노트에서 재지 않음 — 실제 보드에서 같은 모듈로 재 보는 것이 17절 연습문제다). 비교해야 할 것은 **순서와 부하에 대한 민감도**다.

---

## 10. ftrace로 보기 — workqueue와 softirq 이벤트

### 10.1 예제 11 — 트레이스 켜기

무엇을 확인하나: IRQ 진입부터 bottom half 실행까지를 tracepoint로 따라간다. `irq:irq_handler_entry/exit`, `irq:softirq_raise/entry/exit`, `workqueue:workqueue_queue_work/activate_work/execute_start/execute_end`, `sched:sched_wakeup/switch`를 켜고, 잡음을 줄이려고 필터를 건다.

```sh
# f7.sh의 trace 실험 (게스트 안)
mount -t tracefs none /sys/kernel/tracing; T=/sys/kernel/tracing
echo 0 > $T/tracing_on; echo > $T/trace
for e in irq/irq_handler_entry irq/irq_handler_exit irq/softirq_raise irq/softirq_entry irq/softirq_exit \
         workqueue/workqueue_queue_work workqueue/workqueue_activate_work workqueue/workqueue_execute_start workqueue/workqueue_execute_end \
         sched/sched_wakeup sched/sched_switch; do echo 1 > $T/events/$e/enable; done
echo "irq==22" > $T/events/irq/irq_handler_entry/filter      # f7_dev의 virq (ctx 실험에서 확인)
echo "irq==22" > $T/events/irq/irq_handler_exit/filter
for e in softirq_raise softirq_entry softirq_exit; do echo "vec==6" > $T/events/irq/$e/filter; done   # TASKLET
echo 'comm ~ "kworker*" || comm ~ "irq/*" || comm ~ "f7_*"' > $T/events/sched/sched_wakeup/filter
echo 'next_comm ~ "kworker*" || next_comm ~ "irq/*" || next_comm ~ "f7_*"' > $T/events/sched/sched_switch/filter
echo 'function ~ "f7_*"' > $T/events/workqueue/workqueue_execute_start/filter
echo 1 > $T/tracing_on
insmod /f7_defer.ko mode=$2 nsamp=3; sleep 1; echo 0 > $T/tracing_on
rmmod f7_defer
cat $T/trace
```

마지막 필터(`function ~ "f7_*"`)는 `sh: write error: Invalid argument`로 거부되었다. `function` 필드는 문자열이 아니라 함수 포인터라 glob 필터가 안 된다. 그래서 workqueue 이벤트는 걸러지지 않고 다른 work까지 다 찍혔다. 아래는 `mode=wq` 트레이스의 두 번째 샘플 부분이다(앞뒤 줄 생략).

```text
          f7_gen-78      [000] d.h1.    12.382377: irq_handler_entry: irq=22 name=f7_defer
          f7_gen-78      [000] d.h2.    12.382407: workqueue_queue_work: work struct=000000006fe1edb7 function=0xffffda14b73f3708 workqueue=events req_cpu=512 cpu=0
          f7_gen-78      [000] d.h2.    12.382415: workqueue_activate_work: work struct 000000006fe1edb7 function=0xffffda14b73f3708 
          f7_gen-78      [000] d.h5.    12.382453: sched_wakeup: comm=kworker/0:2 pid=61 prio=120 target_cpu=000
          f7_gen-78      [000] d.h1.    12.382468: irq_handler_exit: irq=22 ret=handled
          f7_gen-78      [000] d..2.    12.382571: sched_switch: prev_comm=f7_gen prev_pid=78 prev_prio=120 prev_state=D ==> next_comm=kworker/0:2 next_pid=61 next_prio=120
     kworker/0:2-61      [000] .....    12.382617: workqueue_execute_start: work struct 000000006fe1edb7: function 0xffffda14b73f3708
     kworker/0:2-61      [000] d..4.    12.382675: sched_wakeup: comm=f7_gen pid=78 prio=120 target_cpu=000
     kworker/0:2-61      [000] .....    12.382690: workqueue_execute_end: work struct 000000006fe1edb7: function 0xffffda14b73f3708
     kworker/0:2-61      [000] d..2.    12.382742: sched_switch: prev_comm=kworker/0:2 prev_pid=61 prev_prio=120 prev_state=I ==> next_comm=f7_gen next_pid=78 next_prio=120
```

출력에서 볼 것:

- 플래그 열 `d.h1.`: `d` = IRQ 꺼짐, `h` = hardirq 안. 그 안에서 `workqueue_queue_work`(workqueue=events, 즉 `system_wq`, `cpu=0` = 큐잉한 CPU의 pool)와 `sched_wakeup kworker/0:2`가 일어난다. **top half는 work를 큐에 넣고 kworker를 깨우기만** 한다.
- `sched_switch ... ==> next_comm=kworker/0:2`: IRQ에서 돌아온 뒤 스케줄러가 kworker로 전환. 이 사이(194 µs)가 9절에서 잰 지연의 대부분이다.
- `kworker/0:2` 줄의 플래그 `.....`: IRQ 켜짐, hardirq/softirq 아님, preempt-depth 0 → 완전한 process 컨텍스트.
- `function=0xffffda14b73f3708`: 모듈 함수 이름이 주소로만 나왔다. 트레이스를 읽는 시점에 모듈이 이미 `rmmod`되어 심볼을 풀 수 없었기 때문이다(커널 자체 함수 `vmstat_update`, `do_free_init` 등은 이름으로 나온다). 모듈을 내리기 전에 `cat trace` 하면 이름이 보인다.
- 플래그 `prev_state=D`: 발생기 `f7_gen`은 `wait_for_completion_timeout()`으로 잠들어 있었다(uninterruptible).

`mode=tasklet` 트레이스의 두 번째 샘플:

```text
          f7_gen-78      [000] d.h1.     9.851400: irq_handler_entry: irq=22 name=f7_defer
          f7_gen-78      [000] d.h1.     9.851413: softirq_raise: vec=6 [action=TASKLET]
          f7_gen-78      [000] d.h1.     9.851417: irq_handler_exit: irq=22 ret=handled
          f7_gen-78      [000] ..s1.     9.851451: softirq_entry: vec=6 [action=TASKLET]
          f7_gen-78      [000] ..s1.     9.851513: softirq_exit: vec=6 [action=TASKLET]
```

같은 실행의 세 번째 샘플은 IRQ가 셸(`sh-72`) 위에 떨어졌다.

```text
              sh-72      [000] d.h1.     9.855794: irq_handler_entry: irq=22 name=f7_defer
              sh-72      [000] d.h1.     9.855806: softirq_raise: vec=6 [action=TASKLET]
              sh-72      [000] d.h1.     9.855811: irq_handler_exit: irq=22 ret=handled
              sh-72      [000] ..s1.     9.855833: softirq_entry: vec=6 [action=TASKLET]
              sh-72      [000] d.s5.     9.856044: sched_wakeup: comm=f7_gen pid=78 prio=120 target_cpu=001
              sh-72      [000] ..s1.     9.856062: softirq_exit: vec=6 [action=TASKLET]
```

출력에서 볼 것: `softirq_raise vec=6`이 hardirq 안(`h`)에서, `softirq_entry`가 hardirq가 끝난 **바로 뒤** 같은 태스크 이름으로(`s` = softirq 안) 찍힌다. 스케줄러 전환(`sched_switch`)이 **없다**. 이것이 tasklet이 빠른 이유이자, 잠들 수 없는 이유다. 셋째 샘플에서 `TASK-PID`가 `sh-72`인 것은 "그 순간 CPU0에서 돌던 셸의 몸을 빌렸다"는 뜻이다. 발생기는 CPU1에 있었는데 IRQ는 CPU0으로 왔다(SPI의 기본 affinity가 CPU0).

`mode=thread` 트레이스의 두 번째 샘플:

```text
          insmod-74      [000] d.h..    10.596287: irq_handler_entry: irq=22 name=f7_defer
          insmod-74      [000] d.h..    10.596313: irq_handler_exit: irq=22 ret=handled
          insmod-74      [000] dNh3.    10.596388: sched_wakeup: comm=irq/22-f7_defer pid=78 prio=49 target_cpu=000
          insmod-74      [000] d..2.    10.596796: sched_switch: prev_comm=insmod prev_pid=74 prev_prio=120 prev_state=R+ ==> next_comm=irq/22-f7_defer next_pid=78 next_prio=49
 irq/22-f7_defer-78      [000] d..4.    10.596947: sched_wakeup: comm=f7_gen pid=79 prio=120 target_cpu=000
```

출력에서 볼 것: `prio=49` = SCHED_FIFO 50(8절). 깨운 뒤 플래그에 `N`(need-resched)이 서고, IRQ에서 돌아오는 즉시 `insmod`(prio 120)를 **선점**해(`prev_state=R+` = 실행 가능한 채로 쫓겨남) IRQ 스레드로 전환한다. 이 샘플은 그 전환에 약 0.5 ms가 걸렸는데, 트레이스 오버헤드와 TCG 탓이 크다(트레이스 없는 9절 측정의 p50은 0.17 ms).

### 10.2 타임라인으로 다시 그리기

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="18" text-anchor="middle" font-size="13">ftrace 실측 타임라인 — irq_handler_entry를 0 µs로 맞춤</text>
  <text x="10" y="74" font-size="13">tasklet</text>
  <text x="10" y="154" font-size="13">system_wq</text>
  <text x="10" y="234" font-size="13">threaded IRQ</text>
  <line x1="150" y1="70" x2="660" y2="70" stroke="currentColor" stroke-opacity="0.25"/>
  <line x1="150" y1="150" x2="660" y2="150" stroke="currentColor" stroke-opacity="0.25"/>
  <line x1="150" y1="230" x2="660" y2="230" stroke="currentColor" stroke-opacity="0.25"/>
  <rect x="150" y="62" width="15" height="16" fill="#d0564a" fill-opacity="0.7"/>
  <rect x="196" y="62" width="56" height="16" fill="#e08a3c" fill-opacity="0.7"/>
  <text x="150" y="98" font-size="12">hardirq 17 µs (raise vec=6)</text>
  <text x="196" y="54" font-size="12">softirq_entry 51 µs → exit 113 µs (같은 태스크, 전환 없음)</text>
  <rect x="150" y="142" width="82" height="16" fill="#d0564a" fill-opacity="0.7"/>
  <line x1="232" y1="150" x2="323" y2="150" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <rect x="366" y="142" width="66" height="16" fill="#4a7bd0" fill-opacity="0.7"/>
  <line x1="325" y1="138" x2="325" y2="162" stroke="#4a7bd0" stroke-width="2"/>
  <text x="150" y="178" font-size="12">hardirq 91 µs (queue_work + kworker 깨우기)</text>
  <text x="325" y="132" font-size="12">kworker 전환 194 µs → 실행 240~313 µs</text>
  <rect x="150" y="222" width="23" height="16" fill="#d0564a" fill-opacity="0.7"/>
  <line x1="173" y1="230" x2="604" y2="230" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <rect x="608" y="222" width="22" height="16" fill="#3f9a6b" fill-opacity="0.7"/>
  <text x="150" y="258" font-size="12">hardirq 26 µs, irq/22 깨움(prio 49) 101 µs</text>
  <text x="630" y="214" text-anchor="end" font-size="12">insmod 선점 후 IRQ 스레드 시작 509 µs</text>
  <line x1="150" y1="276" x2="660" y2="276" stroke="currentColor"/>
  <line x1="150" y1="272" x2="150" y2="280" stroke="currentColor"/>
  <text x="150" y="294" text-anchor="middle" font-size="12">0</text>
  <line x1="240" y1="272" x2="240" y2="280" stroke="currentColor"/>
  <text x="240" y="294" text-anchor="middle" font-size="12">100</text>
  <line x1="330" y1="272" x2="330" y2="280" stroke="currentColor"/>
  <text x="330" y="294" text-anchor="middle" font-size="12">200</text>
  <line x1="420" y1="272" x2="420" y2="280" stroke="currentColor"/>
  <text x="420" y="294" text-anchor="middle" font-size="12">300</text>
  <line x1="510" y1="272" x2="510" y2="280" stroke="currentColor"/>
  <text x="510" y="294" text-anchor="middle" font-size="12">400</text>
  <line x1="600" y1="272" x2="600" y2="280" stroke="currentColor"/>
  <text x="600" y="294" text-anchor="middle" font-size="12">500 µs</text>
</svg>
```
그림 5 — 위 세 트레이스(각 방식의 두 번째 샘플)를 시간축에 그린 것. 빨강 = hardirq, 주황 = softirq, 파랑 = kworker, 초록 = IRQ 스레드. 점선 화살표 구간이 "스케줄러를 기다리는 시간"이다. 트레이스를 켠 상태라 9절의 트레이스 없는 측정보다 느리다

---

## 11. teardown — 미룬 일이 모듈보다 오래 살면

### 11.1 bare-metal에는 없던 문제

펌웨어에서는 코드가 ROM/플래시에 영원히 있었다. Linux 모듈은 `rmmod`하면 **코드와 전역 데이터가 메모리에서 사라진다.** 그런데 예약해 둔 work, 타이머, tasklet은 커널 쪽 목록에 "이 주소의 함수를 나중에 불러 줘"라는 포인터로 남아 있다. 모듈이 내려간 뒤 그 시점이 오면 커널은 사라진 주소로 뛴다. 장치 unbind(`.remove`)도 똑같다. 모듈 코드는 남아 있어도 `devm_kzalloc`으로 잡은 private 구조체가 풀린 뒤 work가 그걸 만지면 use-after-free다.

### 11.2 예제 12 — `cancel_delayed_work_sync` 없이 rmmod

```c
// SPDX-License-Identifier: GPL-2.0
/* f7_teardown.c — pending 상태의 delayed_work를 두고 rmmod 하면? (hog 부분은 6.7절) */
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

static bool fix;			/* 1이면 exit에서 cancel_delayed_work_sync */
module_param(fix, bool, 0444);

static void f7_td_fn(struct work_struct *w)
{
	pr_info("f7_td: work 실행 (jiffies=%lu)\n", jiffies);
}
static DECLARE_DELAYED_WORK(f7_td_work, f7_td_fn);

static int __init f7_td_init(void)
{
	/* (hog_s 분기 생략) */
	schedule_delayed_work(&f7_td_work, msecs_to_jiffies(2000));
	pr_info("f7_td: 2초 뒤 실행 예약 (fix=%d)\n", fix);
	return 0;
}

static void __exit f7_td_exit(void)
{
	/* (hog_s 분기 생략) */
	if (fix) {
		bool was = cancel_delayed_work_sync(&f7_td_work);

		pr_info("f7_td: cancel_delayed_work_sync -> %d (1 = pending이던 걸 취소)\n", was);
	}
	pr_info("f7_td: 모듈 내려감\n");
}
module_init(f7_td_init);
module_exit(f7_td_exit);
MODULE_DESCRIPTION("F7 deferred work demo");
MODULE_LICENSE("GPL");
```

게스트에서는 모듈을 올리고 0.5초 뒤 내리고, 3초 기다린다.

```sh
# f7.sh의 td 실험
insmod /f7_teardown.ko fix=$2; sleep 0.5; rmmod f7_teardown; echo "rmmod 끝, 3초 대기"; sleep 3; echo "살아 있음"; dmesg | grep -A40 f7_td
```

```sh
KARGS="loglevel=7" TMO=60 sh /Users/donh/.elinux_scratch/F7/boot.sh "td 0" 2>&1 | sed -n "/f7_td/,\$p" | head -70
```

```text
[    7.807123] f7_td: 2초 뒤 실행 예약 (fix=0)
[    8.396502] f7_td: 모듈 내려감
rmmod 끝, 3초 대기
[    9.816264] Unable to handle kernel paging request at virtual address ffffda14dfb7e068
[    9.816394] Mem abort info:
[    9.816410]   ESR = 0x0000000096000047
[    9.816439]   EC = 0x25: DABT (current EL), IL = 32 bits
...
[    9.816511]   FSC = 0x07: level 3 translation fault
...
[    9.816644] [ffffda14dfb7e068] pgd=0000000000000000, p4d=1000000044073003, pud=1000000046353403, pmd=1000000046354403, pte=0000000000000000
[    9.817773] Internal error: Oops: 0000000096000047 [#1]  SMP
[    9.819769] Modules linked in: [last unloaded: f7_teardown(O)]
[    9.820418] CPU: 1 UID: 0 PID: 0 Comm: swapper/1 Tainted: G           O        6.18.55-dbg #1 PREEMPT 
...
[    9.821184] pc : __run_timer_base+0x178/0x3e0
...
[    9.823699] Call trace:
[    9.823928]  __run_timer_base+0x178/0x3e0 (P)
[    9.824179]  run_timer_softirq+0x3c/0x70
[    9.824266]  handle_softirqs+0x12c/0x4b0
[    9.824365]  __do_softirq+0x1c/0x28
...
[    9.824855]  irq_exit_rcu+0x18/0x48
[    9.825025]  el1_interrupt+0x3c/0x58
...
[    9.825461]  default_idle_call+0xbc/0x2e8 (P)
...
[    9.826632] ---[ end trace 0000000000000000 ]---
[    9.826969] Kernel panic - not syncing: Oops: Fatal exception in interrupt
```

(`...`는 레지스터 덤프 등 생략. "살아 있음"은 끝내 찍히지 않았다)

출력에서 볼 것:

- 예약 7.807초 + 2초 = **9.807초**, Oops 9.816초. 정확히 타이머가 만료되는 순간 죽었다.
- 죽은 곳은 우리 work 함수가 아니라 **`__run_timer_base`**(TIMER softirq)다. `delayed_work`의 `timer_list`가 모듈의 전역 변수(`DECLARE_DELAYED_WORK` = 모듈 `.data`)에 들어 있었고, 커널 타이머 휠은 그 주소를 연결 리스트 노드로 들고 있었다. rmmod로 그 페이지가 매핑 해제되자(`pte=0`, `level 3 translation fault`) 타이머 휠이 리스트를 따라가다 죽었다. work 함수는 **실행되기도 전**이다.
- `Modules linked in: [last unloaded: f7_teardown(O)]`: 크래시 리포트가 "마지막으로 내린 모듈"을 알려 준다. 현장에서 이 줄이 보이면 teardown 버그를 1순위로 의심한다.
- `Fatal exception in interrupt` → panic. softirq(인터럽트 컨텍스트)에서 난 Oops는 그 프로세스만 죽이고 넘어갈 수 없으므로 커널 전체가 멈춘다.
- 죽은 CPU는 1번, 빌린 몸은 `swapper/1`(idle). 우리 모듈과 무관한 곳에서 무관한 시간에 죽는다는 것이 이 버그를 찾기 어렵게 만든다.

### 11.3 고친 버전 — `fix=1`

```sh
KARGS="loglevel=7" TMO=60 sh /Users/donh/.elinux_scratch/F7/boot.sh "td 1" 2>&1 | sed -n "/f7_td/,\$p" | head -70
```

```text
[    7.807526] f7_td: 2초 뒤 실행 예약 (fix=1)
[    8.387552] f7_td: cancel_delayed_work_sync -> 1 (1 = pending이던 걸 취소)
[    8.387622] f7_td: 모듈 내려감
rmmod 끝, 3초 대기
살아 있음
[    7.807526] f7_td: 2초 뒤 실행 예약 (fix=1)
[    8.387552] f7_td: cancel_delayed_work_sync -> 1 (1 = pending이던 걸 취소)
[    8.387622] f7_td: 모듈 내려감
[   11.588756] Flash device refused suspend due to active operation (state 20)
[   11.588949] Flash device refused suspend due to active operation (state 20)
```

출력에서 볼 것: `cancel_delayed_work_sync`가 `1`을 돌려줬다 = "pending이던 것을 취소했다". 타이머가 휠에서 빠졌으므로 2초 시점에 아무 일도 없고 "살아 있음"이 찍혔다. (f7_td 줄이 두 번 나온 것은 콘솔 출력과 `dmesg` 출력이 겹친 것이고, Flash 줄은 poweroff 중 QEMU pflash 드라이버 메시지로 이 실험과 무관하다.)

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" font-size="13">fix=0 (버그)</text>
  <line x1="60" y1="70" x2="650" y2="70" stroke="currentColor" marker-end="url(#ah5)"/>
  <circle cx="60" cy="70" r="6" fill="#4a7bd0"/>
  <text x="60" y="52" font-size="12">insmod: 2초 타이머 예약</text>
  <text x="60" y="92" font-size="12">0 s</text>
  <circle cx="172" cy="70" r="6" fill="#e08a3c"/>
  <text x="172" y="92" font-size="12">rmmod 0.59 s: 모듈 메모리 해제</text>
  <text x="172" y="108" font-size="12">(타이머는 여전히 휠에 걸려 있음)</text>
  <line x1="440" y1="58" x2="440" y2="82" stroke="#d0564a" stroke-width="3"/>
  <text x="440" y="52" text-anchor="middle" font-size="12">2.01 s 타이머 만료</text>
  <text x="440" y="100" text-anchor="middle" font-size="12">해제된 timer_list 접근</text>
  <text x="440" y="116" text-anchor="middle" font-size="12">→ Oops → panic</text>
  <text x="10" y="150" font-size="13">fix=1 (수정)</text>
  <line x1="60" y1="196" x2="650" y2="196" stroke="currentColor" marker-end="url(#ah5)"/>
  <circle cx="60" cy="196" r="6" fill="#4a7bd0"/>
  <text x="60" y="178" font-size="12">insmod: 2초 타이머 예약</text>
  <circle cx="170" cy="196" r="6" fill="#3f9a6b"/>
  <text x="170" y="218" font-size="12">rmmod 0.58 s: cancel_delayed_work_sync → 1</text>
  <text x="170" y="234" font-size="12">(타이머 제거, 그다음에 메모리 해제)</text>
  <text x="440" y="178" text-anchor="middle" font-size="12">2 s: 아무 일 없음</text>
</svg>
```
그림 6 — teardown 경쟁. 예약된 일은 커널 목록에 "포인터"로 남는다. 모듈 메모리를 풀기 전에 그 포인터를 목록에서 빼야 한다

### 11.4 올바른 teardown 순서 — 세 단계

2.6절 `f7_remove()`의 순서를 일반화하면 이렇다.

1. **새 일이 생기지 않게 막는다.** IRQ를 끈다(`free_irq`, `disable_irq`), 장치 인터럽트 마스크를 쓴다, 다른 work나 타이머가 이 work를 다시 큐잉하지 않게 플래그를 세운다.
2. **이미 예약된 일을 취소하고, 도는 중이면 끝나길 기다린다.** `cancel_work_sync`, `cancel_delayed_work_sync`, `tasklet_kill`, `kthread_stop`, `kthread_cancel_work_sync`, `timer_delete_sync`(F8).
3. **그다음에** 그 일이 쓰던 메모리·자원을 푼다. `destroy_workqueue`, `kfree`, `devm_` 해제.

함정들:

- **순서를 1 → 2로 지킨다.** `cancel_work_sync` 다음에 `free_irq`를 하면, 그 사이에 IRQ가 와서 work를 다시 큐잉할 수 있다.
- **`devm_request_irq` + 수동 `cancel_work_sync` 조합**: `devm_` IRQ는 `.remove`가 끝난 **뒤** 풀린다. 그러면 `.remove`에서 `cancel_work_sync`를 해도 그 뒤 IRQ가 와서 다시 큐잉할 수 있다. 해결책은 `devm_add_action_or_reset()`으로 "work 취소"도 devm 체인에 넣어서, IRQ 해제 → work 취소가 역순으로 자동 진행되게 하는 것이다. 또는 IRQ를 `devm_`으로 받지 않는다.
- **`flush_work`는 취소가 아니다.** 지금 있는 것이 끝나길 기다릴 뿐이고, 그 사이 다시 큐잉되면 소용없다. teardown에는 `cancel_*_sync`.
- **스스로 다시 큐잉하는 work**(폴링 루프): `cancel_delayed_work_sync`는 이것도 처리한다(취소 중 표시를 해 두고 재큐잉을 막는다). 하지만 그 work가 **다른** work를 큐잉한다면 그쪽도 따로 취소한다.
- **`destroy_workqueue`는 남은 work를 실행한 뒤 해제한다**(drain). 전용 워크큐라면 이것으로 충분할 때가 많지만, 공용 `system_wq`에 넣은 work는 destroy할 수 없으니 반드시 개별 `cancel_*_sync`.
- **suspend**: suspend 콜백에서도 같은 원리로 work를 멈춘다(`cancel_delayed_work_sync` 후 resume에서 다시 큐잉). 또는 `WQ_FREEZABLE` 워크큐를 쓴다.

---

## 12. 결정표 — 언제 무엇을

### 12.1 한 장 요약

| 방식 | 컨텍스트 | 잠들기 | 지연 (9절, 부하 중 p90) | CPU | 동시 실행 | 쓸 때 |
|---|---|---|---|---|---|---|
| hardirq | IRQ 꺼짐 | 불가 | 0 (기준) | IRQ 받은 CPU | — | 레지스터 읽기, ack, 타임스탬프 |
| softirq (직접) | softirq | 불가 | — | raise한 CPU | 같은 종류도 CPU별로 동시 | 커널 코어만 (net, timer, block) |
| tasklet | softirq | 불가 | 0.13 ms / 0.12 ms | 스케줄한 CPU | 같은 tasklet은 동시 불가 | 기존 코드 유지보수만 |
| BH workqueue | softirq | 불가 | 0.29 / 0.37 ms | 큐잉한 CPU | 같은 work는 동시 불가 | tasklet 대체, 잠들 필요 없는 빠른 처리 |
| threaded IRQ | 스레드 FIFO 50 | 가능 | 0.57 / 0.48 ms | IRQ affinity 따라감 | IRQ당 1개 | **I2C/SPI 센서 IRQ의 기본값** |
| highpri wq | kworker nice −20 | 가능 | 1.4 / 3.7 ms | 큐잉한 CPU | `max_active` | 짧고 지연 민감, IRQ와 1:1 아님 |
| system_wq | kworker nice 0 | 가능 | 12.7 / 10.0 ms | 큐잉한 CPU | `max_active` | 일반적인 "나중에 해 줘" |
| unbound wq | kworker nice 0 | 가능 | 5.2 / 17.4 ms | 아무 CPU (cpumask) | `max_active` | 길거나 CPU를 많이 쓰는 일, 전력 |
| ordered wq | kworker | 가능 | — | 아무 CPU | 1개, 순서 보장 | 명령 시퀀스 직렬화 |
| kthread / kthread_worker | 자기 스레드 | 가능 | 4.3 / 2.2 ms (nice 0일 때) | 지정 가능 | 1개 | RT 우선순위·CPU 고정·영구 루프 |

### 12.2 결정 흐름

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="170" y="14" width="330" height="44" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="335" y="41" text-anchor="middle" font-size="13">할 일이 레지스터 몇 개 + ack뿐인가?</text>
  <line x1="500" y1="36" x2="528" y2="36" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="514" y="28" text-anchor="middle" font-size="12">예</text>
  <rect x="530" y="14" width="140" height="44" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="600" y="41" text-anchor="middle" font-size="13">hardirq에서 끝</text>
  <line x1="335" y1="58" x2="335" y2="92" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="345" y="80" font-size="12">아니오</text>
  <rect x="170" y="94" width="330" height="44" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="335" y="121" text-anchor="middle" font-size="13">잠들어야 하나? (I2C·SPI, mutex, msleep)</text>
  <line x1="200" y1="138" x2="120" y2="182" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="140" y="156" text-anchor="middle" font-size="12">아니오</text>
  <line x1="470" y1="138" x2="500" y2="182" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="500" y="156" font-size="12">예</text>
  <rect x="10" y="184" width="220" height="44" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="211" text-anchor="middle" font-size="13">새로 쓰는 코드인가?</text>
  <line x1="120" y1="228" x2="120" y2="266" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="130" y="252" font-size="12">예</text>
  <rect x="10" y="268" width="220" height="56" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120" y="291" text-anchor="middle" font-size="13">BH workqueue</text>
  <text x="120" y="311" text-anchor="middle" font-size="12">queue_work(system_bh_wq)</text>
  <line x1="60" y1="228" x2="60" y2="352" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah6)"/>
  <text x="22" y="252" font-size="12">아니오</text>
  <rect x="10" y="354" width="220" height="56" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120" y="377" text-anchor="middle" font-size="13">tasklet 유지</text>
  <text x="120" y="397" text-anchor="middle" font-size="12">고칠 때 BH wq로 변환</text>
  <rect x="330" y="184" width="340" height="44" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="500" y="211" text-anchor="middle" font-size="13">IRQ 하나에 1:1로 붙은 처리인가?</text>
  <line x1="400" y1="228" x2="360" y2="266" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="395" y="252" text-anchor="end" font-size="12">예</text>
  <rect x="250" y="268" width="200" height="56" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="350" y="291" text-anchor="middle" font-size="13">threaded IRQ</text>
  <text x="350" y="311" text-anchor="middle" font-size="12">request_threaded_irq</text>
  <line x1="600" y1="228" x2="580" y2="266" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="610" y="252" font-size="12">아니오</text>
  <rect x="470" y="268" width="200" height="56" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="570" y="291" text-anchor="middle" font-size="13">RT 우선순위나</text>
  <text x="570" y="311" text-anchor="middle" font-size="13">영구 루프가 필요?</text>
  <line x1="610" y1="324" x2="610" y2="364" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="620" y="348" font-size="12">예</text>
  <rect x="490" y="366" width="180" height="56" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="580" y="389" text-anchor="middle" font-size="13">kthread_worker</text>
  <text x="580" y="409" text-anchor="middle" font-size="12">+ sched_set_fifo</text>
  <line x1="510" y1="324" x2="420" y2="362" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="455" y="336" font-size="12">아니오</text>
  <rect x="250" y="364" width="220" height="96" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="360" y="386" text-anchor="middle" font-size="13">workqueue</text>
  <text x="360" y="406" text-anchor="middle" font-size="12">급함: WQ_HIGHPRI</text>
  <text x="360" y="424" text-anchor="middle" font-size="12">길다/CPU 무관: UNBOUND</text>
  <text x="360" y="442" text-anchor="middle" font-size="12">회수 경로: MEM_RECLAIM</text>
</svg>
```
그림 7 — "이 일을 어디에 맡길까" 결정 흐름. 새 softirq를 만드는 선택지는 없다(커널 코어 전용)

---

## 13. 커널 안에서는 무슨 일이 일어나나

각 API가 실제로 무엇을 건드리는지 한 번에 정리한다.

- **`raise_softirq(nr)`** → per-CPU 변수 `irq_stat.__softirq_pending`의 비트 `nr`을 세운다. hardirq 탈출 경로 `irq_exit_rcu()` → `__irq_exit_rcu()`가 `in_interrupt()`가 아니고 pending이 있으면 `invoke_softirq()` → `handle_softirqs()`가 비트를 하나씩 지우며 `softirq_vec[nr].action`을 부른다. 예산(2 ms·10회)을 넘으면 `wakeup_softirqd()`.
- **`tasklet_schedule(t)`** → `TASKLET_STATE_SCHED` 비트를 test-and-set(이미 서 있으면 아무것도 안 함) → per-CPU `tasklet_vec` 리스트 끝에 붙이고 `raise_softirq_irqoff(TASKLET_SOFTIRQ)`. `tasklet_action()`이 리스트를 떼어 내 각 tasklet을 `TASKLET_STATE_RUN`으로 잠그고(다른 CPU에서 이미 돌면 다시 뒤로 미룸) 콜백을 부른다.
- **`queue_work(wq, w)`** → `WORK_STRUCT_PENDING` 비트를 test-and-set → 큐잉할 pwq를 고름(per-CPU면 현재 CPU, unbound면 노드/affinity scope) → `max_active` 여유가 있으면 pool의 `worklist`에 붙이고 `kick_pool()`로 idle 워커 하나를 깨움(없으면 manager가 새 워커 생성). 워커 스레드는 `worker_thread()` → `process_one_work()`에서 PENDING을 지우고 함수를 부른다. ftrace의 `workqueue_queue_work → activate_work → execute_start → execute_end`가 이 경로의 tracepoint다.
- **BH workqueue**는 같은 `queue_work`지만 pool이 `bh_worker_pools`(per-CPU)이고, `kick_pool()`이 스레드를 깨우는 대신 `kick_bh_pool()`로 TASKLET softirq(highpri면 HI)를 raise 한다. 그러면 `tasklet_action()` 첫 줄의 `workqueue_softirq_action()` → `bh_worker()`가 work를 실행한다(5.2절 콜스택).
- **concurrency management**의 실체: 워커가 잠들 때 스케줄러가 `wq_worker_sleeping()`을 불러 pool의 `nr_running`을 줄이고, 0이 되면 다른 워커를 깨운다. 깨어날 때는 `wq_worker_running()`. 틱마다 `wq_worker_tick()`이 CPU_INTENSIVE 판정을 한다(6.7절).
- **`queue_delayed_work`** → `timer_list`(work 안에 내장)를 `add_timer`. 만료 시 TIMER softirq의 `delayed_work_timer_fn()`이 진짜 `queue_work`를 한다. 11.2절 크래시가 TIMER softirq에서 난 이유.
- **`cancel_work_sync`** → PENDING 비트를 훔쳐(steal) 리스트에서 빼고, 이미 실행 중이면 `flush_work`처럼 그 실행이 끝나기를 completion으로 기다린다. 그동안 work에 "취소 중" 표시를 해 재큐잉을 막는다.
- **threaded IRQ**: `__setup_irq()`가 `kthread_create(irq_thread, ...)`로 `irq/<n>-<name>` 스레드를 만들고 `sched_set_fifo()`. top half가 `IRQ_WAKE_THREAD`를 돌려주면 `__irq_wake_thread()`가 `IRQTF_RUNTHREAD`를 세우고 `wake_up_process()`. ONESHOT이면 스레드 함수가 끝난 뒤 `irq_finalize_oneshot()`이 라인을 unmask.
- **`kthread_run`** → `kthreadd`(PID 2)에게 생성을 요청하고 깨움. `kthread_stop()`은 `KTHREAD_SHOULD_STOP` 비트를 세우고 깨운 뒤 `wait_for_completion(&kthread->exited)`.

드라이버 노트와의 연결: 이 패턴들은 G 모듈(char device: 데이터가 오면 `wake_up_interruptible`로 `read()` 깨우기), H 모듈(I2C/SPI 클라이언트 드라이버의 threaded IRQ), H6(인터럽트 + regmap 센서 드라이버), O 모듈(NAPI = NET_RX softirq), F8(timer_list·hrtimer가 softirq에서 도는 것)에서 다시 나온다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="160" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="90" y="43" text-anchor="middle" font-size="13">top half</text>
  <text x="90" y="63" text-anchor="middle" font-size="12">f7_hardirq()</text>
  <line x1="170" y1="36" x2="248" y2="36" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="208" y="28" text-anchor="middle" font-size="12">raise</text>
  <rect x="250" y="14" width="200" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="350" y="41" text-anchor="middle" font-size="12">__softirq_pending 비트 6</text>
  <line x1="450" y1="36" x2="498" y2="36" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="500" y="14" width="170" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="585" y="34" text-anchor="middle" font-size="12">irq_exit → tasklet_action</text>
  <text x="585" y="50" text-anchor="middle" font-size="12">tasklet · BH work 실행</text>
  <line x1="170" y1="62" x2="248" y2="112" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="196" y="104" text-anchor="middle" font-size="12">queue</text>
  <rect x="250" y="94" width="200" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="350" y="121" text-anchor="middle" font-size="12">pool->worklist + kick_pool</text>
  <line x1="450" y1="116" x2="498" y2="116" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="500" y="94" width="170" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="585" y="114" text-anchor="middle" font-size="12">스케줄러 → kworker</text>
  <text x="585" y="130" text-anchor="middle" font-size="12">process_one_work</text>
  <line x1="90" y1="76" x2="248" y2="190" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="140" y="160" text-anchor="middle" font-size="12">WAKE_THREAD</text>
  <rect x="250" y="174" width="200" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="350" y="201" text-anchor="middle" font-size="12">IRQTF_RUNTHREAD + wake</text>
  <line x1="450" y1="196" x2="498" y2="196" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="500" y="174" width="170" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="585" y="194" text-anchor="middle" font-size="12">FIFO 선점 → irq_thread</text>
  <text x="585" y="210" text-anchor="middle" font-size="12">thread_fn, unmask</text>
  <line x1="50" y1="76" x2="248" y2="262" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="104" y="232" text-anchor="middle" font-size="12">wake_up</text>
  <rect x="250" y="250" width="200" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="350" y="277" text-anchor="middle" font-size="12">플래그 + wait queue</text>
  <line x1="450" y1="272" x2="498" y2="272" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="500" y="250" width="170" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="585" y="277" text-anchor="middle" font-size="12">스케줄러 → 내 kthread</text>
</svg>
```
그림 8 — top half에서 각 bottom half까지 커널 내부 경로. 위의 한 줄(softirq)만 스케줄러를 거치지 않는다

---

## 14. Embedded 실무에서 다시 보기

### 14.1 센서 키트의 I2C IMU — 정석 구조

IMU의 data-ready 핀이 GPIO 인터럽트로 들어오는 경우의 뼈대다(컴파일·실행하지 않은 스케치 — H 모듈에서 실제 I2C 드라이버로 다룬다).

```c
static irqreturn_t imu_hardirq(int irq, void *d)
{
	struct imu *st = d;

	st->ts = ktime_get_boottime_ns();	/* 타임스탬프는 top half에서: 가장 정확 */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t imu_thread(int irq, void *d)
{
	struct imu *st = d;
	int ret;

	ret = regmap_bulk_read(st->map, IMU_REG_FIFO, st->buf, 14);	/* I2C: 잠든다 */
	if (ret)
		return IRQ_NONE;
	imu_push_sample(st, st->buf, st->ts);
	return IRQ_HANDLED;
}

/* probe */
ret = devm_request_threaded_irq(dev, irq, imu_hardirq, imu_thread,
				IRQF_ONESHOT, "imu", st);
```

포인트: 타임스탬프는 **top half**에서 찍는다(bottom half에서 찍으면 9절의 지연과 지터가 그대로 섞인다). 레벨 트리거면 `IRQF_ONESHOT`이 라인을 마스크해 준다. 스레드는 FIFO 50이라 nice 0 앱이 CPU를 다 써도 9절 숫자처럼 버틴다. 더 높은 우선순위가 필요하면 user space에서 `chrt -f -p 60 <irq 스레드 PID>`로 올린다.

### 14.2 "가끔 샘플이 10 ms 늦는다"

증상: 평소엔 괜찮은데 카메라 파이프라인이나 빌드가 돌면 IMU 타임스탬프 간격이 튄다. 원인 후보 1순위는 bottom half가 `schedule_work()`(nice 0 kworker)라는 것 — 9절에서 부하 중 p90이 10 ms를 넘는 것을 봤다. 확인은 10절처럼 `workqueue_queue_work`와 `workqueue_execute_start`의 시간차를 ftrace로 보고, 고치는 법은 threaded IRQ로 옮기거나 최소한 `system_highpri_wq`/전용 `WQ_HIGHPRI` 워크큐로 바꾸는 것이다.

### 14.3 폴링 장치 — `delayed_work`로 주기 작업

인터럽트 핀이 없는 배터리 게이지나 온도 센서는 `delayed_work`가 스스로 다시 큐잉하는 패턴을 쓴다.

```c
static void gauge_poll(struct work_struct *w)
{
	struct gauge *g = container_of(to_delayed_work(w), struct gauge, poll);

	gauge_read_and_report(g);			/* I2C: 잠들어도 됨 */
	queue_delayed_work(system_freezable_wq, &g->poll, msecs_to_jiffies(1000));
}
/* remove: cancel_delayed_work_sync(&g->poll);  ← 재큐잉까지 막는다 */
```

`system_freezable_wq`를 쓰면 suspend 중에는 폴링이 멈춘다. (스케치, 실행하지 않음)

### 14.4 커널 로그에서 이 노트의 흔적 찾기

- `BUG: scheduling while atomic` / `BUG: sleeping function called from invalid context` → softirq·tasklet·spinlock 안에서 잠듦 (5.2절).
- `BUG: workqueue lockup - pool cpus=N ... stuck for Ns!` → work 하나가 바쁜 대기 (6.7절). `in-flight:`가 범인, `pending:`이 피해자.
- `Modules linked in: [last unloaded: xxx]` + 타이머/워크큐 경로 Oops → teardown 누락 (11절).
- `top`에서 `ksoftirqd/N`이 CPU를 많이 먹음 → softirq 폭주(보통 네트워크). `kworker/...+<wq이름>`이 CPU를 먹으면 꼬리의 워크큐 이름으로 범인 드라이버를 찾는다.

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| ISR(hardirq)에서 `i2c_transfer`/`regmap_read` | `BUG: sleeping function called from invalid context`, 교착 | I2C 버스 락이 rt_mutex, 전송 완료를 잠들어 기다림 | threaded IRQ의 thread_fn에서 읽기 |
| tasklet·BH work에서 `msleep`, `mutex_lock`, `GFP_KERNEL` | `scheduling while atomic` 후 엉뚱한 곳에서 Oops·panic | softirq는 남의 태스크 몸을 빌려 돈다 | 스레드(threaded IRQ, workqueue)로 옮기거나 `GFP_ATOMIC`, spinlock 사용 |
| rmmod·remove에서 `cancel_work_sync` 누락 | 수 초 뒤 timer/kworker 경로에서 Oops, `last unloaded:` | 커널 목록에 해제된 work·timer 포인터가 남음 | `cancel_delayed_work_sync` 등으로 취소 후 해제 |
| IRQ 해제 전에 work 취소 | 드물게 같은 UAF | 취소와 해제 사이에 IRQ가 다시 큐잉 | 소스 차단(free_irq) → 취소 → 해제 순서, devm 순서 주의 |
| `flush_work`를 teardown에 사용 | 가끔 UAF | flush는 취소가 아님, 재큐잉을 못 막음 | `cancel_work_sync` |
| work 안에서 `mdelay`로 오래 대기 | 같은 CPU의 다른 work 지연, `workqueue lockup` | concurrency management가 새 워커를 안 깨움 | `msleep`/`usleep_range`, 또는 `WQ_UNBOUND` |
| 지연 민감한 처리를 `schedule_work` | 부하 시 수~수십 ms 지터 | nice 0 kworker가 CFS/EEVDF 경쟁 | threaded IRQ(FIFO) 또는 `WQ_HIGHPRI` |
| `kthread` 대기 조건에 `kthread_should_stop()` 누락 | rmmod가 영원히 멈춤 | `kthread_stop`이 깨워도 다시 잠듦 | 대기 조건에 `kthread_should_stop()` 포함 |
| `kthread_run` 반환값을 NULL로 검사 | 실패 시 ERR_PTR 역참조 Oops | 실패 시 `ERR_PTR` 반환 | `IS_ERR()`/`PTR_ERR()` |
| threaded IRQ(handler=NULL)에 `IRQF_ONESHOT` 누락 | `request_threaded_irq`가 `-EINVAL`, 또는 레벨 IRQ 폭주 | 스레드가 돌기 전에 라인이 unmask됨 | `IRQF_ONESHOT` |

---

## 16. 면접에서 이렇게 말한다

**Q.** "Why can't you read an I2C sensor in the ISR, and where do you do it instead?"

**A.** I2C 전송은 잠든다. 버스 락이 rt_mutex이고, 컨트롤러 드라이버는 전송 완료를 completion으로 기다린다. hardirq는 태스크가 아니라 잠들 수 없고, 400 kHz에서 14바이트면 300 µs 넘게 그 CPU의 인터럽트를 막는다. 그래서 top half에서는 타임스탬프만 찍고 `IRQ_WAKE_THREAD`를 돌려주고, threaded IRQ의 스레드 함수에서 읽는다. `IRQF_ONESHOT`으로 그동안 라인을 마스크한다.

> An I2C transfer sleeps: the adapter bus lock is an rt_mutex and the controller driver waits for completion of the transfer, and a hard IRQ handler isn't a task, so it can't sleep. It would also block every interrupt on that CPU for hundreds of microseconds. So the top half only timestamps and returns IRQ_WAKE_THREAD, and I read the sensor in the threaded handler registered with request_threaded_irq and IRQF_ONESHOT, which keeps the line masked until the thread finishes. The thread is SCHED_FIFO, so latency stays low even under load.

**꼬리 질문**: "Why not a workqueue?" → 공유 nice 0 kworker라 부하 시 지연이 튄다(직접 재 보니 p90이 10 ms 이상). threaded IRQ는 IRQ 전용 RT 스레드다.

**Q.** "What's the difference between softirq, tasklet and workqueue?"

**A.** softirq는 컴파일 시점에 고정된 10개의 bottom half로, hardirq 탈출 직후 같은 CPU에서 IRQ를 켠 채 돈다. 잠들 수 없고 같은 종류가 여러 CPU에서 동시에 돈다. tasklet은 TASKLET/HI softirq 위에서 동적으로 만드는 bottom half이고, 같은 tasklet은 동시에 두 CPU에서 돌지 않는다. workqueue는 kworker 스레드에서 도는 process 컨텍스트라 잠들 수 있다.

> Softirqs are a fixed, compile-time set of ten vectors that run right after the hard IRQ exits, on the same CPU, with interrupts enabled; they can't sleep and the same vector can run on several CPUs at once. Tasklets are dynamically created on top of the TASKLET and HI softirqs and are serialized against themselves. Workqueues run in kworker threads, in process context, so they can sleep. Tasklets are deprecated; new code uses threaded IRQs or BH workqueues.

**Q.** "Tasklets are deprecated — what replaces them?"

**A.** 잠들어야 하면 threaded IRQ, softirq 수준의 지연이 정말 필요하면 6.9에 들어온 BH workqueue(`system_bh_wq`, `WQ_BH`)다. BH workqueue는 TASKLET softirq에서 도는 workqueue라 `queue_work`·`cancel_work_sync`·`disable_work` 같은 workqueue API를 그대로 쓴다.

> Threaded IRQs if the work can sleep, and for true softirq-level latency the BH workqueue added in 6.9: system_bh_wq or a WQ_BH queue. It executes in softirq context on the queueing CPU, but uses the normal workqueue API, so tasklet_schedule becomes queue_work and tasklet_kill becomes cancel_work_sync.

**Q.** "Explain CMWQ. What do WQ_UNBOUND, WQ_HIGHPRI and WQ_MEM_RECLAIM do?"

**A.** workqueue는 정책일 뿐이고 실제 스레드는 CPU별·속성별 worker pool에 있어 공유된다. pool은 돌고 있는 워커 수를 세서, 워커가 잠들면 다른 워커를 깨워 CPU를 계속 쓴다. UNBOUND는 CPU에 묶이지 않는 pool, HIGHPRI는 nice −20 pool, MEM_RECLAIM은 메모리 부족에도 진행을 보장하는 rescuer 스레드를 하나 둔다.

> In CMWQ a workqueue is just a policy front-end; the kworker threads belong to shared worker pools, per-CPU normal and high-priority pools plus unbound pools keyed by attributes. A per-CPU pool tracks how many workers are running and wakes another one when a worker blocks, so the CPU stays busy without one thread per queue. WQ_UNBOUND lets the scheduler pick the CPU, WQ_HIGHPRI uses the nice -20 pool, and WQ_MEM_RECLAIM pre-allocates a rescuer thread so the queue makes forward progress under memory pressure.

**꼬리 질문**: "What's max_active?" → 워크큐당 동시에 실행될 수 있는 work 수 상한(기본 1024, ordered는 1).

**Q.** "Your driver crashes a few seconds after rmmod. What do you look for?"

**A.** 예약해 둔 work·delayed work·timer·tasklet이 모듈보다 오래 산 것이다. Oops가 타이머 softirq나 kworker에서 나고 `last unloaded`에 내 모듈이 찍힌다. remove에서 IRQ를 먼저 끄고, `cancel_delayed_work_sync`로 취소·대기한 뒤 메모리를 푼다. devm IRQ는 remove 뒤에 풀리니 순서에 주의한다.

> Deferred work outliving the module: a pending delayed work's timer or a queued work still points into freed module memory, so the oops shows up later in the timer softirq or a kworker, with "last unloaded" naming my module. The fix is ordering in remove: stop the source first (free_irq), then cancel_work_sync or cancel_delayed_work_sync, which also waits for a running instance and blocks requeueing, and only then free the data. I watch out for devm-managed IRQs being released after remove.

**Q.** "flush_work vs cancel_work_sync?"

**A.** flush는 지금 있는 것이 끝나길 기다릴 뿐 취소하지 않는다. cancel_work_sync는 pending이면 취소하고, 실행 중이면 끝나길 기다리며, 재큐잉도 막는다. 둘 다 잠들므로 process 컨텍스트에서만, 그리고 그 work 자신 안에서는 부르면 안 된다.

> flush_work waits for the currently queued or running instance to finish but doesn't cancel anything, so it can be requeued right after. cancel_work_sync removes it if pending, waits if it's running, and prevents self-requeueing during the cancel. Both sleep, so process context only, and never from inside the work item itself.

**Q.** "When would you use a dedicated kthread instead of a workqueue?"

**A.** RT 우선순위, CPU 고정, 영구 루프가 필요할 때. 다만 직접 kthread를 짜기보다 kthread_worker를 쓰면 workqueue 같은 API에 전용 스레드를 얻고 `sched_set_fifo`로 올릴 수 있다. SPI 코어의 메시지 펌프가 그렇게 한다.

> When I need my own scheduling policy, like SCHED_FIFO, CPU pinning, or a long-lived loop. I'd usually use a kthread_worker rather than a hand-rolled loop: it gives a workqueue-like API on a dedicated thread that I can raise with sched_set_fifo, which is what the SPI core does for its message pump. And a raw kthread must include kthread_should_stop in its wait condition.

**Q.** "A work item spins for seconds. What happens?"

**A.** 같은 CPU pool의 다른 work가 밀린다. 최근 커널은 일정 시간 이상 잠들지 않은 워커를 CPU_INTENSIVE로 바꿔 다른 워커를 깨우고, 그래도 막히면 WQ_WATCHDOG가 "workqueue lockup"을 찍는다. 바쁜 대기 대신 잠들거나 unbound 워크큐를 쓴다.

> It can starve other work on that CPU's pool, because concurrency management sees a running worker and doesn't wake another. Recent kernels auto-mark such a worker CPU_INTENSIVE after a threshold, and if the pool still stalls the workqueue watchdog reports "BUG: workqueue lockup" with the in-flight and pending items. The fix is to sleep instead of busy-waiting, or move CPU-heavy work to an unbound queue.

---

## 17. 직접 해보기

**문제 1.** **개념**: tasklet은 왜 "같은 tasklet은 동시에 두 CPU에서 돌지 않는다"를 보장하고, softirq는 왜 보장하지 않을까? 각각 어떤 장단점이 있나?

정답/힌트: tasklet은 `TASKLET_STATE_RUN` 비트로 자기 자신을 직렬화(쓰기 쉬움). softirq는 per-CPU 병렬 처리(NET_RX 확장성)를 위해 보장하지 않는다.

**문제 2.** **코드**: `f7_defer`에 `mode=bhhi`를 추가해 `system_bh_highpri_wq`로 큐잉하고, ftrace로 `softirq_raise vec=0 [action=HI]`가 찍히는지 확인하라.

정답/힌트: softirq 필터를 `vec==0 || vec==6`으로. `kick_bh_pool()`이 highpri면 HI를 raise 한다.

**문제 3.** **코드**: `f7_kthread`에 `sched_set_fifo(p->kt)`를 추가하고 9절 lat 실험을 다시 돌려라. kthread의 부하 중 p90이 threaded IRQ 수준으로 내려오는가?

정답/힌트: 내려와야 한다 — 차이는 스레드 종류가 아니라 스케줄링 클래스다.

**문제 4.** **관찰**: `/sys/bus/workqueue/devices/f7_wq/cpumask`에 `2`를 써서 CPU1로만 보내고, `mode=unbound`의 ctx 출력에서 `cpu=`가 바뀌는지 확인하라.

정답/힌트: unbound 워크큐만 sysfs로 cpumask를 바꿀 수 있다.

**문제 5.** **버그 재현**: `f7_remove()`에서 `cancel_work_sync(&p->work)`를 지우고 `mode=wq nsamp=1000`으로 올린 직후 rmmod 하라. 어떤 증상이 나오나? (일회용 게스트에서만)

정답/힌트: 발생기를 먼저 멈추므로 대개 무사하지만, work가 큐에 있을 때 `devm` 해제가 일어나면 kworker에서 UAF Oops. 타이밍 의존 버그임을 체험하는 것이 목표.

**문제 6.** **관찰**: 부팅 인자 `threadirqs`를 주고 부팅해 `ps`에서 `irq/..` 스레드가 얼마나 늘었는지 세어 보라.

정답/힌트: `IRQF_NO_THREAD`가 아닌 IRQ(UART 등)가 강제 스레드화되어 `irq/NN-...`가 생긴다.

**문제 7.** **실보드**: 같은 모듈을 실제 arm64 보드(예: Raspberry Pi 4, i.MX8)에서 돌려 9절 표를 다시 만들고 QEMU 숫자와 비교하라.

정답/힌트: 절대값은 수~수십 배 작아지지만 "softirq < FIFO 스레드 < highpri < nice 0" 순서와 부하 민감도는 유지되는지 확인.


---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| top half | 상반부 | hardirq handler. 시각 찍기, ack, 예약만 |
| bottom half | 하반부 | 나중에 도는 나머지 처리 (softirq·tasklet·wq·thread) |
| softirq | 소프트웨어 인터럽트 | 고정 10개 vec, hardirq 탈출 직후 같은 CPU에서 실행 |
| ksoftirqd/N | softirq 스레드 | 예산(2 ms·10회) 초과분을 처리하는 CPU별 nice 0 스레드 |
| tasklet | 태스클릿 | TASKLET/HI softirq 위의 동적 bottom half, deprecated |
| BH workqueue | BH 워크큐 | `WQ_BH`, softirq에서 도는 workqueue (6.9~), tasklet 대체 |
| CMWQ | Concurrency Managed Workqueue | 워크큐는 정책, 스레드는 공유 pool, 돌고 있는 워커 수로 관리 |
| worker_pool | 워커 풀 | 실제 kworker 묶음. CPU별 normal/highpri, unbound, BH |
| pwq | pool_workqueue | 워크큐와 pool을 잇는 연결, `max_active` 적용 지점 |
| kworker | 커널 워커 스레드 | `kworker/0:1`, `kworker/0:1H`, `kworker/u8:1`, `kworker/R-x` |
| rescuer | 구조 스레드 | `WQ_MEM_RECLAIM` 워크큐 전용, 메모리 부족 시 진행 보장 |
| max_active | 최대 동시 실행 | 워크큐당 동시에 실행 가능한 work 수 (기본 1024) |
| ordered workqueue | 순서 워크큐 | 한 번에 하나씩 넣은 순서대로 실행 |
| delayed_work | 지연 work | work + timer_list, 만료 시 큐잉 |
| CPU_INTENSIVE | CPU 집약 표시 | 오래 도는 워커를 concurrency 관리에서 제외 |
| WQ_WATCHDOG | 워크큐 워치독 | pool이 일정 시간 진행 못 하면 `workqueue lockup` 보고 |
| threaded IRQ | 스레드 IRQ | `irq/N-name` SCHED_FIFO 50 스레드에서 도는 핸들러 |
| IRQF_ONESHOT | 원샷 플래그 | 스레드 함수가 끝날 때까지 IRQ 라인 마스크 |
| kthread_worker | 커널 스레드 워커 | 전용 스레드 하나 + work 목록, RT 우선순위 부여 가능 |
| teardown | 정리 | 소스 차단 → 미룬 일 취소 → 자원 해제 순서 |

---

## 19. 요약 & 체크리스트

hardirq는 짧아야 하고 잠들 수 없으므로 나머지 일은 미룬다. 미룰 곳은 softirq 계열(고정 softirq, 그 위의 tasklet과 6.9의 BH workqueue — 빠르지만 잠들 수 없음)과 스레드 계열(threaded IRQ — FIFO 50, workqueue — 공유 kworker, kthread/kthread_worker — 전용 스레드 — 잠들 수 있지만 스케줄러를 거침)로 나뉜다. QEMU 실측에서 softirq 계열이 가장 빨랐고(중앙값 50~120 µs), 부하가 걸리면 nice 0 kworker의 p90이 10 ms를 넘은 반면 threaded IRQ는 0.5 ms 안팎을 지켰다. 그래서 "ISR에서 I2C를 읽으면 안 된다 → threaded IRQ에서 읽는다"가 정답이다. workqueue는 CMWQ 구조(공유 pool, concurrency management)를 이해하고 `WQ_UNBOUND`·`WQ_HIGHPRI`·`WQ_MEM_RECLAIM`·`max_active`·ordered를 골라 쓰며, 바쁜 대기는 `workqueue lockup`을 부른다. 마지막으로 미룬 일은 모듈·장치보다 오래 살 수 있으므로 "소스 차단 → `cancel_*_sync` → 해제" 순서를 지키지 않으면 수 초 뒤 엉뚱한 곳에서 커널이 죽는다.

- [ ] "ISR에서 I2C를 못 읽는 이유"를 rt_mutex·completion·IRQ 마스킹으로 설명할 수 있다
- [ ] softirq 10개의 이름과 실행 시점 3곳(irq_exit, local_bh_enable, ksoftirqd)을 말할 수 있다
- [ ] tasklet이 deprecated인 것과 BH workqueue(6.9)로의 변환표를 말할 수 있다
- [ ] softirq에서 잠들면 왜 "남의 태스크"가 피해를 보는지 콜스택으로 설명할 수 있다
- [ ] CMWQ의 workqueue / pwq / worker_pool / kworker 관계를 그릴 수 있다
- [ ] `kworker/0:1H`, `kworker/u8:1`, `kworker/R-x` 이름을 보고 pool 종류를 말할 수 있다
- [ ] `alloc_workqueue` 플래그와 `max_active`, ordered wq를 상황에 맞게 고를 수 있다
- [ ] `flush_work`와 `cancel_work_sync`의 차이, teardown 3단계 순서를 코드로 짤 수 있다
- [ ] `request_threaded_irq` + `IRQF_ONESHOT`으로 센서 IRQ 핸들러를 짤 수 있다
- [ ] kthread 루프를 `kthread_should_stop()`과 함께 바르게 짜고, kthread_worker를 쓸 수 있다
- [ ] ftrace의 workqueue·softirq 이벤트로 IRQ → bottom half 지연을 직접 잴 수 있다
- [ ] `workqueue lockup`, `scheduling while atomic`, `last unloaded` Oops를 보고 원인을 짚을 수 있다

## 참고 자료

- docs.kernel.org — Workqueue (Documentation/core-api/workqueue.rst): https://docs.kernel.org/core-api/workqueue.html
- docs.kernel.org — Kernel API: `request_threaded_irq` 등 Generic IRQ handling: https://docs.kernel.org/core-api/genericirq.html
- 랩 소스(Linux 6.18.55): `include/linux/interrupt.h`(softirq 목록, tasklet), `kernel/softirq.c`(`handle_softirqs`, `tasklet_action`), `include/linux/workqueue.h`·`kernel/workqueue.c`(플래그, 시스템 워크큐, `wq_worker_tick`, `wq_worker_comm`), `kernel/irq/manage.c`(threaded IRQ, `sched_set_fifo`), `drivers/i2c/i2c-core.h`·`i2c-core-base.c`(버스 락, atomic xfer)
- elixir.bootlin.com에서 위 파일들을 버전별로 비교: https://elixir.bootlin.com/linux/v6.18/source/kernel/workqueue.c
- Robert Love, "Linux Kernel Development" 3판, 8장 Bottom Halves and Deferring Work (tasklet 시절 설명이므로 CMWQ·BH wq는 이 노트 기준으로 보정)
- Bootlin, "Linux kernel and driver development" 교육 슬라이드 — interrupts and deferred work 부분: https://bootlin.com/training/kernel/
- 관련 노트: E4(커널 제약), F2(실행 컨텍스트), F3(wait queue·completion), F4(락), F6(인터럽트·threaded IRQ), F8(타이머), H6(센서 드라이버의 IRQ)
