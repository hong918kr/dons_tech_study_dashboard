# N3. 지연 측정 — cyclictest, rtla(osnoise/timerlat), hwlat, 히스토그램 해석

> **이 노트를 다 읽으면**: cyclictest를 올바른 옵션·부하·시간으로 돌려 히스토그램과 백분위를 낼 수 있다 · rtla timerlat으로 지연을 "IRQ 단계 vs thread 단계"로 가르고 `--auto`로 첫 스파이크의 범인을 잡을 수 있다 · osnoise/hwlat으로 "OS 안의 소음"과 "OS 밖의 소음"을 구분할 수 있다 · 두 커널·두 설정을 공정하게 비교한 결과를 설계 리뷰용 한 장으로 보고할 수 있다
> **왜 필요한가**: 로보틱스·드론·차량 면접에서 "PREEMPT_RT 써 봤나요?" 다음 질문은 거의 항상 "그래서 지연이 얼마였고, 어떻게 쟀나요?"다. 숫자 없이 "RT 패치를 넣었더니 좋아졌다"는 답은 통과하지 못한다.
> **Don 기준 난이도**: scope로 IRQ→GPIO 응답을 재 본 경험, "최악값이 스펙이다"라는 감각은 이미 있다 / 새로 배울 것은 Linux에서 소프트웨어로 지연을 재는 도구들, 각 도구가 "어디서부터 어디까지"를 재는지, 히스토그램·백분위·측정 시간의 통계적 의미
> **선행 노트**: N1(Linux는 실시간인가 — 지연 원인), N2(실시간 튜닝), F1(스케줄러 — 직접 만든 깨어남 지연 루프 §8, `wakeup_rt` tracer §9), F8(시간·타이머·hrtimer), M5(ftrace 기초·irqsoff/preemptoff tracer). 하드웨어 측정은 M11.

---

## 0. 큰 그림 — "지연이 얼마냐"는 질문의 정답 형식

SSD 펌웨어 시절을 떠올려 보자. "이 커맨드의 latency는 몇 µs입니까?"라는 질문에 평균 하나로 답한 적은 없을 것이다. QoS 스펙은 "4K random read, QD1, 99.99 percentile ≤ X µs, 측정 시간 Y 분, preconditioning 후"처럼 **분포·조건·시간**이 함께 붙는다. Linux 실시간 지연도 똑같다. 정답 형식은 이렇다:

```text
"<하드웨어>에서 <커널/설정>으로, <부하>를 걸고 <시간> 동안 <도구/옵션>으로 쟀더니
 p99 = a us, p99.99 = b us, max = c us 였다. 히스토그램은 이렇다."
```

이 노트는 이 한 문장을 만들고 방어하는 법을 다룬다. 도구는 네 개다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="14">지연을 재는 도구는 "어디서부터 어디까지"가 다르다</text>
<rect x="20" y="40" width="190" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30" y="60" font-size="13">HW (scope·LA, M11)</text>
<text x="30" y="80" font-size="12">핀 이벤트 → GPIO 토글</text>
<rect x="20" y="100" width="190" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="120" font-size="13">hwlat / hwnoise</text>
<text x="30" y="140" font-size="12">OS 밖(펌웨어·EL3·HV)</text>
<rect x="20" y="160" width="190" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="180" font-size="13">osnoise</text>
<text x="30" y="200" font-size="12">CPU를 뺏긴 시간 합</text>
<rect x="20" y="220" width="190" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="240" font-size="13">timerlat / cyclictest</text>
<text x="30" y="260" font-size="12">타이머 → 깨어남 지연</text>
<rect x="240" y="40" width="400" height="230" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="255" y="62" font-size="13">무엇을 답하나</text>
<text x="255" y="90" font-size="12">HW: 실제 핀에서 반응까지 — 고객이 보는 진짜 숫자</text>
<text x="255" y="120" font-size="12">hwlat: IRQ까지 끈 루프에서도 시간이 사라지나?</text>
<text x="255" y="140" font-size="12">→ 사라지면 OS 탓이 아니다 (SMI, TF-A, 호스트)</text>
<text x="255" y="180" font-size="12">osnoise: 내 스레드가 돌 때 누가 끼어드나?</text>
<text x="255" y="200" font-size="12">→ IRQ, softirq, NMI, 다른 thread별로 센다</text>
<text x="255" y="240" font-size="12">timerlat: 깨어나야 할 때 얼마나 늦나?</text>
<text x="255" y="260" font-size="12">→ IRQ 단계 vs thread 단계로 나눠 준다</text>
</svg>
```

그림 1 — 네 가지 측정 도구가 각각 답하는 질문. 아래로 갈수록 "OS 안쪽"의 증상, 위로 갈수록 "OS 밖·실제 핀"에 가깝다.

bare-metal 비유로 정리하면:

| Linux 도구 | Don이 아는 bare-metal 대응 | 무엇을 재나 |
|---|---|---|
| cyclictest | 주기 타이머 ISR에서 "기대 시각 vs 실제 진입 시각"을 기록하는 자체 측정 루프 | 유저 RT 스레드의 타이머 깨어남 지연(전 구간 합) |
| timerlat | 같은 측정을 ISR 진입 시점과 태스크 진입 시점 두 곳에서 동시에 | IRQ latency와 thread latency를 분리 |
| osnoise | 바쁜 루프를 돌리며 "루프 한 바퀴가 비정상적으로 길어진 순간"을 찾는 것 | 워크로드가 빼앗긴 시간과 그 원인 분류 |
| hwlat | 인터럽트를 모두 끄고 TSC/카운터를 연속으로 읽어 "구멍"을 찾는 것 | OS가 통제 못 하는 정지(SMI, 펌웨어, 하이퍼바이저) |
| scope + GPIO (M11) | 그대로 같다 | 외부 관찰자 기준 실제 응답 시간 |

### 0.1 이 노트의 실험 환경과 꼭 기억할 경고

모든 실행은 Mac의 Lima VM(`elinux`, 호스트 커널 `7.0.0-38-generic`, aarch64) 안에서 QEMU TCG로 띄운 게스트에서 했다. 게스트 커널은 랩의 `Image-trace`(6.18.55-trace, `PREEMPT`, ftrace 전부)와 `Image-rt`(6.18.55-rt, `PREEMPT_RT`), rootfs는 `rootfs-trace.ext4`(Ubuntu 24.04 + rt-tests 2.50 + stress-ng + rtla 6.18.55)다.

> **TCG 경고 (이 노트 전체에 적용)**: 이 게스트에는 진짜 하드웨어가 없다. CPU는 소프트웨어 에뮬레이션이고, VM 호스트는 다른 작성자 16명이 동시에 쓰고 있었다(측정 중 `uptime` load average 10~23, 코어 6개). 그래서 **절대 숫자는 RT 성능이 아니라 에뮬레이터와 호스트 부하를 반영한다.** 이 노트에서 배울 것은 (1) 측정 방법, (2) 상대 비교와 분포의 모양, (3) 도구가 원인을 가리키는 방식이다. 실제 i.MX8·AM62·Orin 보드라면 같은 명령으로 수 µs~수십 µs 단위가 나온다.

두 가지 시간 모드로 실험했다. 이 구분 자체가 이 노트의 중요한 교훈이 된다.

| 모드 | QEMU 옵션 | 게스트 시간의 의미 | 무엇을 보여 주나 |
|---|---|---|---|
| 실시간 TCG | (기본, multi-thread TCG) | 호스트 실제 시간. 호스트가 vCPU 스레드를 재우면 게스트 시간은 계속 흐른다 | "OS 밖의 정지"가 지연을 지배할 때 어떻게 보이나 — SMI·하이퍼바이저와 같은 상황 |
| icount | `-icount shift=3,sleep=off` | 명령어 1개 = 8 ns인 가상 시간. 호스트가 느려도 게스트 시간이 같이 멈춘다 | 게스트 안의 원인(IRQ off, preempt off, 부하)이 깨끗하게 드러난다 |

icount 모드는 "명령어 수로 정해지는 장난감 시계"다. 캐시 미스·버스 대기·DRAM 지연은 모델링되지 않고, vCPU 두 개가 한 호스트 스레드에서 번갈아(round-robin) 돈다. 그러니 이 모드의 숫자도 실제 HW 숫자가 아니다. 다만 호스트 부하에 덜 흔들려서 **같은 실험을 반복했을 때 모양이 재현된다.**

---

## 1. 실습 준비 — systemd 없이 바로 측정 스크립트를 PID 1로

systemd 게스트는 부하 걸린 호스트에서 부팅에 수 분이 걸리고, 백그라운드 서비스가 측정에 소음을 더한다. 그래서 커널 커맨드라인 `init=/root/n3/init.sh`로 **측정 스크립트를 PID 1로** 띄웠다. 커널은 이해 못 하는 `이름=값` 인자를 init의 환경 변수로 넘겨 주므로(`N3D=30` 같은), 스크립트 매개변수를 커맨드라인으로 바꿀 수 있다.

### 1.1 예제 1 — PID 1 측정 스크립트와 QEMU 실행

무엇을 확인하는 코드인가: 최소한의 마운트(proc, sys, tracefs, debugfs)만 하고, 커맨드라인의 `n3phase=`로 고른 실험을 차례로 돌린 뒤 결과를 rootfs에 남기고 전원을 끈다.

```sh
#!/bin/bash
# /root/n3/init.sh — PID 1 for N3 experiments: no systemd, run phases, power off.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
mount -t proc proc /proc; mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null; mkdir -p /dev/pts /dev/shm
mount -t devpts pts /dev/pts; mount -t tmpfs tmp /tmp; mount -t tmpfs shm /dev/shm
mount -t tracefs none /sys/kernel/tracing; mount -t debugfs none /sys/kernel/debug
mount -t cgroup2 none /sys/fs/cgroup 2>/dev/null
hostname n3
PHASE=$(sed -n 's/.*n3phase=\([^ ]*\).*/\1/p' /proc/cmdline)
OUT=/root/n3/out/$(uname -r)_$PHASE$N3TAG; rm -rf $OUT; mkdir -p $OUT
echo "N3START $(uname -r) phase=$PHASE"
for p in ${PHASE//,/ }; do
  mkdir -p $OUT/$p
  bash /root/n3/phase_$p.sh $OUT/$p 2>&1 | tee $OUT/$p/log.txt; sync
done
echo N3END; sync; sleep 1
echo o > /proc/sysrq-trigger; sleep 60
```

VM 쪽에서는 이렇게 띄웠다 (`QX`에 icount 옵션을 넣거나 비운다).

```sh
timeout 5400 qemu-system-aarch64 -no-reboot -machine virt -cpu cortex-a57,pmu=on -smp 2 -m 1G \
  -nographic $QX -kernel /var/lab/out/Image-$K \
  -drive file=/var/tmp/N3/n3_root.ext4,if=virtio,format=raw \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait panic=1 init=/root/n3/init.sh n3phase=$P $X"
```

출력에서 볼 것: 부팅 로그 끝의 `Run /root/n3/init.sh as init process` 다음 줄이 곧바로 `N3START`다. 결과 파일은 QEMU가 끝난 뒤 VM에서 `debugfs -R "rdump /root/n3/out <dir>" n3_root.ext4`로 꺼냈다(이미지를 mount하지 않으므로 sudo도 필요 없다).

함정 두 개를 실제로 밟았다.

- `echo o > /proc/sysrq-trigger`는 전원 끄기를 **작업 큐에 넣고 바로 돌아온다.** 곧바로 PID 1이 끝나면 `Kernel panic - not syncing: Attempted to kill init!`이 먼저 난다. 그래서 `sleep 60`을 붙이고, 그래도 끝나지 않을 때를 대비해 `panic=1` + QEMU `-no-reboot`로 "panic이면 QEMU 종료"를 만들었다.
- QEMU를 `kill`로 죽였더니 게스트 page cache에만 있던 결과 디렉터리가 통째로 사라졌다(`e2fsck`가 고친 뒤에도 비어 있었다). 그래서 실험 하나가 끝날 때마다 `sync`한다. 측정 장비에서 결과를 잃는 가장 흔한 이유가 이것이다.

### 1.2 예제 2 — 일부러 지연을 만드는 모듈 `n3_spike`

원인을 아는 지연을 넣어야 도구가 그것을 제대로 가리키는지 확인할 수 있다. 오실로스코프를 믿기 전에 기준 신호를 넣어 보는 것과 같다.

무엇을 확인하는 코드인가: 지정한 CPU에 묶인 kthread가 `period_ms`마다 `us` 마이크로초 동안 **IRQ를 끄고**(mode=0) 또는 **선점만 끄고**(mode=1) 바쁘게 돈다.

```c
/* n3_spike: a kthread bound to one CPU that, every period_ms, burns `us`
 * microseconds with IRQs off (mode=0) or with only preemption off (mode=1).
 * It is a deliberate latency source for cyclictest / rtla experiments. */
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/irqflags.h>
#include <linux/preempt.h>

static int cpu = 1, us = 300, period_ms = 50, mode;
module_param(cpu, int, 0444);
module_param(us, int, 0444);
module_param(period_ms, int, 0444);
module_param(mode, int, 0444);

static struct task_struct *n3_task;
static unsigned long n3_spikes;
static noinline void n3_burn(void)
{
	unsigned long flags;
	if (mode == 0) {
		local_irq_save(flags);
		udelay(us);
		local_irq_restore(flags);
	} else {
		preempt_disable();
		udelay(us);
		preempt_enable();
	}
	n3_spikes++;
}

static int n3_fn(void *unused)
{
	while (!kthread_should_stop()) {
		msleep_interruptible(period_ms);
		n3_burn();
	}
	return 0;
}
static int __init n3_init(void)
{
	n3_task = kthread_create(n3_fn, NULL, "n3_spike/%d", cpu);
	if (IS_ERR(n3_task))
		return PTR_ERR(n3_task);
	kthread_bind(n3_task, cpu);
	wake_up_process(n3_task);
	pr_info("n3_spike: cpu=%d us=%d period=%dms mode=%s\n", cpu, us,
		period_ms, mode ? "preempt_off" : "irq_off");
	return 0;
}
static void __exit n3_exit(void)
{
	kthread_stop(n3_task);
	pr_info("n3_spike: %lu spikes injected\n", n3_spikes);
}
module_init(n3_init);
module_exit(n3_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("N3 latency spike injector");
```

빌드는 커널마다 따로 한다(vermagic이 다르다). Kbuild는 `obj-m += n3_spike.o` 한 줄이다.

```sh
make -C /var/lab/out/linux-build-trace M=$PWD modules   # -> vermagic 6.18.55-trace
make -C /var/lab/out/linux-build-rt    M=$PWD modules   # -> vermagic 6.18.55-rt
modinfo -F vermagic n3_spike.ko
```

```text
6.18.55-trace SMP preempt mod_unload aarch64
6.18.55-rt SMP preempt_rt mod_unload aarch64
```

출력에서 볼 것: 같은 소스라도 `preempt` 대 `preempt_rt`로 vermagic이 갈린다. 두 커널 모두 경고 0개로 빌드됐다(처음엔 `missing MODULE_DESCRIPTION()` modpost 경고가 나서 한 줄 추가했다).

> PREEMPT_RT에서도 `local_irq_save()`는 진짜로 CPU의 IRQ를 끄고, `preempt_disable()`도 진짜로 선점을 막는다. RT 커널이 "잠들 수 있게" 바꾸는 것은 `spinlock_t`이지 이 두 API가 아니다(N1). 그래서 이 모듈은 두 커널 모두에서 같은 종류의 지연을 만든다.

---

## 2. cyclictest — 무엇을 재는가

### 2.1 직관 — "알람을 7시에 맞췄는데 7시 몇 분에 일어났나"

cyclictest는 SCHED_FIFO 스레드 하나(또는 CPU마다 하나)를 만들고, 그 스레드가 아래 루프를 끝없이 돈다. F1 §8.1에서 직접 만든 `f1_lat`이 바로 이 루프였다.

```text
next = now + interval
loop:
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, next)   # 절대 시각까지 잔다
    now = clock_gettime(CLOCK_MONOTONIC)
    latency = now - next                                     # 늦은 만큼
    record(latency)  -> min / avg / max / histogram[latency]
    next += interval
```

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14">cyclictest 한 주기 (interval = 1000 us)</text>
<line x1="40" y1="200" x2="640" y2="200" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="600" y="220" font-size="12">시간</text>
<line x1="120" y1="60" x2="120" y2="210" stroke="#4a7bd0" stroke-dasharray="4 3"/>
<text x="70" y="52" font-size="12">expected (next)</text>
<rect x="120" y="150" width="80" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="128" y="167" font-size="12">IRQ 지연</text>
<text x="128" y="183" font-size="12">(irq off)</text>
<rect x="200" y="150" width="70" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="208" y="167" font-size="12">hrtimer</text>
<text x="208" y="183" font-size="12">handler</text>
<rect x="270" y="150" width="170" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="278" y="167" font-size="12">wakeup → 스케줄</text>
<text x="278" y="183" font-size="12">(preempt off, RT 경쟁)</text>
<rect x="440" y="150" width="100" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="448" y="167" font-size="12">ctx switch</text>
<text x="448" y="183" font-size="12">+ 유저 복귀</text>
<line x1="540" y1="60" x2="540" y2="210" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="500" y="52" font-size="12">actual (now)</text>
<line x1="124" y1="90" x2="536" y2="90" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="536" y1="90" x2="124" y2="90" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="230" y="82" font-size="13">latency = now − next (cyclictest가 기록)</text>
<text x="20" y="245" font-size="12">루프: clock_nanosleep(ABSTIME, next) → clock_gettime(now) → 차이 기록 → next += interval</text>
<text x="20" y="265" font-size="12">주황·초록 = 커널 IRQ 경로 (timerlat의 IRQ latency),  빨강·회색까지 = thread latency</text>
<text x="20" y="285" font-size="12">cyclictest 값은 네 구간의 합이다 — 어디서 늦었는지는 알려 주지 않는다</text>
</svg>
```

그림 2 — cyclictest의 측정 모델. 기대 시각(파란 점선)과 스레드가 실제로 시계를 읽은 시각(빨간 점선)의 차이가 "latency" 한 숫자로 기록된다.

### 2.2 정의 — 네 구간의 합

cyclictest가 보고하는 한 샘플은 다음의 합이다.

| 구간 | 무엇이 늘리나 | 커널 쪽 이름 |
|---|---|---|
| ① 타이머 만료 → IRQ 진입 | IRQ off 구간(`local_irq_save`, 긴 hardirq), 깊은 idle 상태 탈출, 펌웨어·하이퍼바이저 정지 | arch timer PPI → GIC → `el1_interrupt` |
| ② hrtimer 처리 | 같은 시각에 만료된 다른 hrtimer들 | `hrtimer_interrupt` → `__hrtimer_run_queues` → `hrtimer_wakeup` |
| ③ wakeup → 실제 선택 | preempt off 구간, 더 높은 우선순위 RT 태스크, (비 RT 커널의) 긴 softirq | `try_to_wake_up` → `need_resched` → `schedule()` |
| ④ context switch + 유저 복귀 | 캐시·TLB 상태, 유저 쪽 page fault(그래서 `-m`) | `__schedule` → `cpu_switch_to` → `ret_to_user` |

중요한 점: **cyclictest는 합만 준다.** "max가 2 ms"는 알려 주지만 ①인지 ③인지는 모른다. 그걸 가르는 도구가 7절의 timerlat이다.

### 2.3 옵션 — 업계 표준 커맨드라인 한 줄 해부

RT 커뮤니티(예: OSADL 장기 측정)에서 가장 흔히 보는 형태는 이렇다.

```sh
cyclictest -m -S -p 95 -i 1000 -h 400 -q -D 24h --histfile=/var/log/ct.hist
```

| 옵션 | 뜻 | 왜 쓰나 |
|---|---|---|
| `-m` (`--mlockall`) | 현재·미래 메모리 전부 잠금 | 측정 루프의 page fault를 없앤다(A5). 빼면 첫 몇 샘플·메모리 압박 시 튄다 |
| `-S` (`--smp`) | `-a -t` + 모든 스레드 같은 우선순위 | CPU마다 측정 스레드 하나씩, 각자 자기 CPU에 고정 |
| `-p 95` | SCHED_FIFO 우선순위 95 | 측정 스레드가 다른 RT 작업에 밀리지 않게. 관례적으로 99는 비워 둔다(시스템의 최우선 RT 작업용). 제품의 실제 RT 스레드와 같은 우선순위로 재는 것도 좋다 |
| `-i 1000` | 주기 1000 µs | 기본값. 제어 루프 주기에 맞춰 바꾸기도 한다 |
| `-h 400` | 0~400 µs를 1 µs 버킷으로 히스토그램 | 400 µs를 넘는 샘플은 "Histogram Overflows"로 따로 센다 |
| `-q` | 끝날 때 요약만 출력 | 화면 갱신 자체가 부하가 되는 것을 막는다 |
| `-D 24h` | 측정 시간 (`m`, `h`, `d` 접미사) | 5절 — 짧은 측정은 거짓말한다 |
| `--histfile=` | 히스토그램을 파일로 | 나중에 백분위·그림을 만든다 |
| `--policy=fifo` | 정책 지정(other, batch, idle, fifo, rr) | SCHED_OTHER로 돌려 "RT가 아니면 어떤가" 비교 |
| `-a 1 -t 1` | 스레드 1개를 CPU1에 | 격리한 CPU(N2의 `isolcpus`) 하나만 볼 때 |
| `--mainaffinity=0` | 측정 스레드가 아닌 메인(출력) 스레드를 CPU0에 | 메인 스레드가 측정 CPU를 건드리지 않게 |
| `-b 300` + `--tracemark` | 300 µs 넘으면 ftrace를 멈추고(trace marker도 기록) 종료 | 9절 — 스파이크 순간의 트레이스 보존 |
| `--json=` | 결과를 JSON으로 | CI에서 자동 비교 |
| `--spike=200` | 200 µs 넘는 샘플을 하나하나 기록 | 언제 튀었는지 시각 확인 |

> rt-tests 2.x에서 cyclictest의 내장 tracer 옵션(옛 `-f`, `-w` 등)은 없어졌다. `-b`는 이제 "tracing_on을 0으로 쓰고 끝내기"만 한다. 무엇을 기록할지는 우리가 ftrace(M5)나 trace-cmd로 미리 켜 둬야 한다. 아래 출력은 게스트 rootfs의 cyclictest 2.50 `--help`에서 확인한 것이다.

```text
cyclictest V 2.50
-b USEC  --breaktrace=USEC send break trace command when latency > USEC
-h       --histogram=US    dump a latency histogram to stdout after the run
                           US is the max latency time to be tracked in microseconds
			   This option runs all threads at the same priority.
	 --histfile=<path> dump the latency histogram to <path> instead of stdout
-S       --smp             Standard SMP testing: options -a -t and same priority
                           of all threads
         --tracemark       write a trace mark when -b latency is exceeded
...
```

(긴 도움말에서 몇 줄만 남겼다.) `/dev/cpu_dma_latency`에 0을 써서 CPU가 깊은 idle에 들어가지 않게 하는 것도 cyclictest의 기본 동작이다(`--default-system`이면 하지 않는다). 그래서 출력 첫 줄에 `# /dev/cpu_dma_latency set to 0us`가 찍힌다. **이것 자체가 시스템 튜닝이다** — 제품 설정에 PM QoS 요청이 없다면, cyclictest 결과는 제품보다 좋게 나온다. 공정하게 비교하려면 이 점을 보고서에 적는다.

---

## 3. 부하 걸기 — idle 시스템의 숫자는 아무 의미가 없다

### 3.1 왜 부하가 필수인가

idle 시스템의 cyclictest는 "아무도 방해하지 않을 때"의 숫자다. 제품은 그렇게 돌지 않는다. 센서 키트라면 카메라 DMA가 메모리 대역폭을 먹고, LiDAR UDP가 NET_RX softirq를 일으키고, 로깅이 eMMC에 쓰고, 다른 프로세스가 `fork()`/`exec()`를 한다. 지연의 최악값은 **이런 일들과 타이머가 겹칠 때** 나온다. 그래서 측정에는 "최악의 현실적인 부하"를 일부러 건다.

| 부하 도구 | 무엇을 흔드나 | 예 |
|---|---|---|
| `stress-ng --cpu N` | CPU 연산, 스케줄러 경쟁 | `stress-ng --cpu 4 --timeout 1h` |
| `stress-ng --vm N --vm-bytes 256M` | 메모리 할당·page fault·캐시/TLB | 메모리 대역폭 압박 |
| `stress-ng --io N` / `--hdd N` | `sync()` 호출, 블록 I/O, 파일시스템 락, writeback | 로깅·녹화가 있는 장비 |
| `hackbench -l 100000 -g 4` | 소켓/파이프 메시지 폭풍 → wakeup·context switch 폭주 | 스케줄러 경로 스트레스 |
| `ping -f <peer>` (root) 또는 `iperf3` | NIC IRQ, NET_RX softirq | 네트워크 장비, LiDAR |
| `make -j$(nproc)` 커널 빌드 | 모든 것(fork/exec, 파일 I/O, 메모리) | "현실적인 혼합 부하" 고전 |
| 제품 자체 워크로드 | 카메라 스트리밍, 추론, 로깅 | **최종 측정은 반드시 이것으로** |

OSADL 같은 장기 측정 랩은 보통 여러 개를 섞어 건다. 이 노트의 "loaded" 부하는 `stress-ng --cpu 2 --vm 1 --vm-bytes 64M --io 1 --hdd 1` + `hackbench -s 512 -g 2 -P`다(icount 실험에서는 I/O 계열을 빼고 CPU·메모리·hackbench만 — I/O 에뮬레이션이 icount에서 극단적으로 느려져 실험이 끝나지 않았다). 네트워크 부하는 이 랩의 QEMU user-mode 네트워크(slirp)로는 의미 있는 IRQ 폭풍을 만들 수 없어 **가이드(실행 안 함)**로만 둔다.

### 3.2 부하를 걸 때의 함정

- **부하가 측정 스레드보다 높은 우선순위면 안 된다.** stress-ng는 기본 SCHED_OTHER다. 부하를 `chrt -f 99`로 돌리면 "측정"이 아니라 "굶기기"가 된다.
- **부하가 정말 돌고 있는지 확인한다.** `stress-ng`가 권한 문제로 일부 stressor를 건너뛰는 일이 흔하다. `top`이나 `mpstat 1`로 CPU가 실제로 100%인지 본다.
- **부하와 측정의 길이를 맞춘다.** 부하가 측정보다 먼저 끝나면 후반부는 idle 측정이다. 이 노트의 스크립트는 부하를 측정 시간 + 5초로 걸었다.
- **열·주파수.** 실제 보드에서는 오래 돌리면 thermal throttling으로 클럭이 떨어진다. 그것도 현실이니 숨기지 말고 `cpufreq` 상태(N5)를 함께 기록한다.

---

## 4. 히스토그램과 백분위 — 평균은 거의 아무것도 말하지 않는다

### 4.1 예제 3 — cyclictest idle vs loaded (실시간 TCG, Image-trace)

무엇을 확인하나: 같은 커널·같은 게스트에서 부하 없음과 부하 있음을 60초씩 잰다. 그리고 **VM 호스트가 바쁠 때와 조용할 때** 두 번 반복했다.

```sh
cyclictest -m -S -p 95 -i 1000 -h 20000 -q -D 60 --histfile=$O/idle.hist
stress-ng --cpu 2 --vm 1 --vm-bytes 64M --io 1 --hdd 1 --hdd-bytes 16M --timeout 65s -q &
hackbench -s 512 -l 100000000 -g 2 -P >/dev/null 2>&1 &
cyclictest -m -S -p 95 -i 1000 -h 20000 -q -D 60 --histfile=$O/load.hist
```

호스트가 조용할 때(load average 약 4)의 idle 요약(`--histfile` 끝부분):

```text
# /dev/cpu_dma_latency set to 0us
# Min Latencies: 00052 00044
# Avg Latencies: 00201 00370
# Max Latencies: 22072 22509
# Histogram Overflows: 00002 00003
```

같은 조건의 loaded:

```text
# Min Latencies: 00057 00079
# Avg Latencies: 00494 00508
# Max Latencies: 25248 26670
# Histogram Overflows: 00006 00007
```

숫자 두 개씩은 스레드 0(CPU0), 스레드 1(CPU1)이다. 출력에서 볼 것: 평균은 200~500 µs인데 max는 22~27 ms다. **max가 평균의 50~100배**다. 평균만 보고 "0.5 ms 지연 시스템"이라고 말하면 안 되는 이유가 이것이다. `-h 20000`이라 20 ms를 넘은 샘플은 Overflows로 따로 셌다.

### 4.2 예제 4 — 백분위를 직접 계산하는 스크립트

cyclictest는 min/avg/max만 준다. p99.9 같은 백분위는 히스토그램에서 우리가 계산한다. 이 스크립트를 VM에서 결과 파일에 돌렸다.

무엇을 확인하는 코드인가: 1 µs 버킷 히스토그램을 누적해서 p50/p99/p99.9/p99.99를 구한다. 오버플로 샘플도 전체 개수에는 넣고, 백분위가 오버플로 구간에 떨어지면 `>20000`처럼 "히스토그램 범위 밖"이라고 정직하게 표시한다.

```python
#!/usr/bin/env python3
"""n3_pct.py: percentiles from a cyclictest --histfile (1 us buckets).
usage: n3_pct.py file.hist [label]  -> one line per thread + merged"""
import sys

def load(path):
    rows, meta = [], {}
    for line in open(path):
        if line.startswith('#'):
            if ':' in line:
                k, v = line[1:].split(':', 1)
                meta[k.strip()] = v.split()
            continue
        f = line.split()
        if f:
            rows.append([int(x) for x in f])
    return rows, meta

def pct(counts, over, q):
    """smallest bucket b with cum(b) >= q * total; None if inside overflow"""
    total = sum(counts) + over
    need, cum = q * total, 0
    for b, c in enumerate(counts):
        cum += c
        if cum >= need:
            return b
    return None

rows, meta = load(sys.argv[1])
nthr = len(rows[0]) - 1
over = [int(x) for x in meta['Histogram Overflows']]
mx = [int(x) for x in meta['Max Latencies']]
H = len(rows)
qs = [0.5, 0.99, 0.999, 0.9999]
print(f"{'thr':>5} {'n':>7} {'p50':>6} {'p99':>7} {'p99.9':>7} {'p99.99':>7} {'max':>8}  over")
series = [[r[t + 1] for r in rows] for t in range(nthr)]
series.append([sum(r[1:]) for r in rows])
over.append(sum(over)); mx.append(max(mx))
for t, s in enumerate(series):
    name = f"T{t}" if t < nthr else "all"
    ps = [pct(s, over[t], q) for q in qs]
    txt = [f"{p:>7}" if p is not None else f"{'>' + str(H):>7}" for p in ps]
    print(f"{name:>5} {sum(s) + over[t]:>7} " + " ".join(txt) + f" {mx[t]:>8}  {over[t]}")
```

```sh
python3 -I n3_pct.py idle.hist; python3 -I n3_pct.py load.hist
```

호스트가 조용할 때(위 4.1의 두 파일):

```text
  thr       n    p50     p99   p99.9  p99.99      max  over
   T0   59430     188     390    3538   14657    22072  2
   T1   59404     354     669    3559   15786    22509  3
  all  118834     270     581    3559   14940    22509  5
  thr       n    p50     p99   p99.9  p99.99      max  over
   T0   58944     463     833    5399  >20000    25248  6
   T1   57074     480     807    5480  >20000    26670  7
  all  116018     471     820    5414  >20000    26670  13
```

호스트가 바쁠 때(load average 15~23, 같은 명령):

```text
  thr       n    p50     p99   p99.9  p99.99      max  over
   T0   39944     410    7015  >20000  >20000   176108  97
   T1   39136     403    7167  >20000  >20000   174260  97
  all   79080     407    7085  >20000  >20000   176108  194
  thr       n    p50     p99   p99.9  p99.99      max  over
   T0   28252     524   10051  >20000  >20000   890987  105
   T1   25394     583   11348  >20000  >20000   891499  116
  all   53646     548   10670  >20000  >20000   891499  221
```

출력에서 볼 것:

- **n(샘플 수)**: 60초 × 1000 Hz = 스레드당 60000이어야 한다. 조용할 때는 59000대, 바쁠 때는 25000~40000이다. 주기를 통째로 놓친 것이다. 샘플 수 확인은 측정의 "건전성 검사"다.
- **p50은 거의 안 변했다**(270 → 407 µs). 하지만 p99는 581 → 7085 µs로 12배, max는 22 ms → 176 ms로 8배. 게스트·커널·명령이 모두 같고 **바뀐 것은 측정 환경(호스트 부하)뿐**이다. 10절의 "공정 비교" 규칙이 왜 필요한지 보여 주는 실험이다.
- 부하를 걸면 p50이 오른다(270 → 471 µs). 캐시·TLB가 부하에 오염되고 wakeup 경로가 더 길어지기 때문이다. 실제 보드에서도 같은 방향이다.

### 4.3 히스토그램 그림 — 로그 축으로 그린다

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="14">cyclictest 60 s, Image-trace, 실시간 TCG — 호스트 부하에 따라</text>
<line x1="60" y1="280" x2="590" y2="280" stroke="currentColor"/>
<line x1="60" y1="56" x2="60" y2="280" stroke="currentColor"/>
<line x1="60.0" y1="280" x2="60.0" y2="284" stroke="currentColor"/>
<text x="60.0" y="298" font-size="12" text-anchor="middle">10us</text>
<line x1="192.5" y1="280" x2="192.5" y2="284" stroke="currentColor"/>
<text x="192.5" y="298" font-size="12" text-anchor="middle">100us</text>
<line x1="325.0" y1="280" x2="325.0" y2="284" stroke="currentColor"/>
<text x="325.0" y="298" font-size="12" text-anchor="middle">1ms</text>
<line x1="457.5" y1="280" x2="457.5" y2="284" stroke="currentColor"/>
<text x="457.5" y="298" font-size="12" text-anchor="middle">10ms</text>
<line x1="590.0" y1="280" x2="590.0" y2="284" stroke="currentColor"/>
<text x="590.0" y="298" font-size="12" text-anchor="middle">100ms</text>
<line x1="56" y1="262.8" x2="60" y2="262.8" stroke="currentColor"/>
<text x="52" y="266.8" font-size="12" text-anchor="end">1e0</text>
<line x1="56" y1="193.8" x2="60" y2="193.8" stroke="currentColor"/>
<text x="52" y="197.8" font-size="12" text-anchor="end">1e2</text>
<line x1="56" y1="124.9" x2="60" y2="124.9" stroke="currentColor"/>
<text x="52" y="128.9" font-size="12" text-anchor="end">1e4</text>
<line x1="56" y1="56.0" x2="60" y2="56.0" stroke="currentColor"/>
<text x="52" y="60.0" font-size="12" text-anchor="end">1e6</text>
<text x="325.0" y="316" font-size="12" text-anchor="middle">latency (log)</text>
<text x="14" y="44" font-size="12">count (log)</text>
<polyline points="60.0,280.0 73.3,280.0 73.3,280.0 86.5,280.0 86.5,280.0 99.8,280.0 99.8,280.0 113.0,280.0 113.0,280.0 126.2,280.0 126.2,280.0 139.5,280.0 139.5,215.8 152.8,215.8 152.8,209.1 166.0,209.1 166.0,191.4 179.2,191.4 179.2,172.9 192.5,172.9 192.5,144.8 205.8,144.8 205.8,135.0 219.0,135.0 219.0,104.1 232.2,104.1 232.2,125.8 245.5,125.8 245.5,132.5 258.8,132.5 258.8,103.5 272.0,103.5 272.0,123.3 285.2,123.3 285.2,159.9 298.5,159.9 298.5,169.1 311.8,169.1 311.8,188.1 325.0,188.1 325.0,206.5 338.2,206.5 338.2,214.6 351.5,214.6 351.5,221.3 364.8,221.3 364.8,217.9 378.0,217.9 378.0,214.6 391.2,214.6 391.2,213.4 404.5,213.4 404.5,210.0 417.8,210.0 417.8,217.9 431.0,217.9 431.0,233.6 444.2,233.6 444.2,238.7 457.5,238.7 457.5,221.3 470.7,221.3 470.7,223.3 484.0,223.3 484.0,242.0 497.2,242.0 497.2,280.0 510.5,280.0 510.5,280.0 523.8,280.0 523.8,280.0 537.0,280.0 537.0,280.0 550.2,280.0 550.2,280.0 563.5,280.0 563.5,280.0 576.8,280.0 576.8,280.0 590.0,280.0" fill="none" stroke="#4a7bd0" stroke-width="1.6"/>
<rect x="602" y="238.7" width="10" height="41.3" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<line x1="504.2" y1="56" x2="504.2" y2="280" stroke="#4a7bd0" stroke-dasharray="3 3"/>
<line x1="340" y1="58.0" x2="360" y2="58.0" stroke="#4a7bd0" stroke-width="2"/>
<text x="366" y="62.0" font-size="12">조용 idle (max 22.5ms)</text>
<polyline points="60.0,280.0 73.3,280.0 73.3,280.0 86.5,280.0 86.5,280.0 99.8,280.0 99.8,280.0 113.0,280.0 113.0,280.0 126.2,280.0 126.2,280.0 139.5,280.0 139.5,280.0 152.8,280.0 152.8,262.8 166.0,262.8 166.0,252.4 179.2,252.4 179.2,207.2 192.5,207.2 192.5,197.4 205.8,197.4 205.8,180.2 219.0,180.2 219.0,170.6 232.2,170.6 232.2,153.3 245.5,153.3 245.5,133.2 258.8,133.2 258.8,115.3 272.0,115.3 272.0,103.1 285.2,103.1 285.2,108.6 298.5,108.6 298.5,119.3 311.8,119.3 311.8,159.9 325.0,159.9 325.0,192.0 338.2,192.0 338.2,199.2 351.5,199.2 351.5,204.2 364.8,204.2 364.8,213.4 378.0,213.4 378.0,205.8 391.2,205.8 391.2,217.9 404.5,217.9 404.5,204.5 417.8,204.5 417.8,213.4 431.0,213.4 431.0,217.2 444.2,217.2 444.2,219.5 457.5,219.5 457.5,219.5 470.7,219.5 470.7,217.2 484.0,217.2 484.0,225.6 497.2,225.6 497.2,280.0 510.5,280.0 510.5,280.0 523.8,280.0 523.8,280.0 537.0,280.0 537.0,280.0 550.2,280.0 550.2,280.0 563.5,280.0 563.5,280.0 576.8,280.0 576.8,280.0 590.0,280.0" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<rect x="616" y="224.4" width="10" height="55.6" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<line x1="513.9" y1="56" x2="513.9" y2="280" stroke="#3f9a6b" stroke-dasharray="3 3"/>
<line x1="340" y1="76.0" x2="360" y2="76.0" stroke="#3f9a6b" stroke-width="2"/>
<text x="366" y="80.0" font-size="12">조용 loaded (max 26.7ms)</text>
<polyline points="60.0,280.0 73.3,280.0 73.3,280.0 86.5,280.0 86.5,280.0 99.8,280.0 99.8,280.0 113.0,280.0 113.0,280.0 126.2,280.0 126.2,280.0 139.5,280.0 139.5,280.0 152.8,280.0 152.8,280.0 166.0,280.0 166.0,242.0 179.2,242.0 179.2,199.0 192.5,199.0 192.5,162.5 205.8,162.5 205.8,125.6 219.0,125.6 219.0,124.9 232.2,124.9 232.2,130.8 245.5,130.8 245.5,130.4 258.8,130.4 258.8,135.9 272.0,135.9 272.0,129.6 285.2,129.6 285.2,131.9 298.5,131.9 298.5,134.0 311.8,134.0 311.8,134.7 325.0,134.7 325.0,138.0 338.2,138.0 338.2,143.4 351.5,143.4 351.5,146.5 364.8,146.5 364.8,151.3 378.0,151.3 378.0,152.9 391.2,152.9 391.2,157.4 404.5,157.4 404.5,166.2 417.8,166.2 417.8,172.0 431.0,172.0 431.0,178.4 444.2,178.4 444.2,186.9 457.5,186.9 457.5,188.0 470.7,188.0 470.7,193.7 484.0,193.7 484.0,201.0 497.2,201.0 497.2,262.8 510.5,262.8 510.5,280.0 523.8,280.0 523.8,280.0 537.0,280.0 537.0,280.0 550.2,280.0 550.2,280.0 563.5,280.0 563.5,280.0 576.8,280.0 576.8,280.0 590.0,280.0" fill="none" stroke="#e08a3c" stroke-width="1.6"/>
<rect x="630" y="183.9" width="10" height="96.1" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<line x1="340" y1="94.0" x2="360" y2="94.0" stroke="#e08a3c" stroke-width="2"/>
<text x="366" y="98.0" font-size="12">바쁨 idle (max 176.1ms)</text>
<polyline points="60.0,280.0 73.3,280.0 73.3,280.0 86.5,280.0 86.5,280.0 99.8,280.0 99.8,280.0 113.0,280.0 113.0,280.0 126.2,280.0 126.2,280.0 139.5,280.0 139.5,280.0 152.8,280.0 152.8,262.8 166.0,262.8 166.0,220.4 179.2,220.4 179.2,180.4 192.5,180.4 192.5,173.6 205.8,173.6 205.8,170.0 219.0,170.0 219.0,151.2 232.2,151.2 232.2,130.7 245.5,130.7 245.5,135.9 258.8,135.9 258.8,132.8 272.0,132.8 272.0,135.3 285.2,135.3 285.2,140.2 298.5,140.2 298.5,144.6 311.8,144.6 311.8,140.9 325.0,140.9 325.0,143.4 338.2,143.4 338.2,143.0 351.5,143.0 351.5,147.8 364.8,147.8 364.8,145.9 378.0,145.9 378.0,147.7 391.2,147.7 391.2,147.6 404.5,147.6 404.5,155.1 417.8,155.1 417.8,162.7 431.0,162.7 431.0,167.0 444.2,167.0 444.2,177.5 457.5,177.5 457.5,185.4 470.7,185.4 470.7,189.5 484.0,189.5 484.0,199.2 497.2,199.2 497.2,280.0 510.5,280.0 510.5,280.0 523.8,280.0 523.8,280.0 537.0,280.0 537.0,280.0 550.2,280.0 550.2,280.0 563.5,280.0 563.5,280.0 576.8,280.0 576.8,280.0 590.0,280.0" fill="none" stroke="#d0564a" stroke-width="1.6"/>
<rect x="644" y="182.0" width="10" height="98.0" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<line x1="340" y1="112.0" x2="360" y2="112.0" stroke="#d0564a" stroke-width="2"/>
<text x="366" y="116.0" font-size="12">바쁨 loaded (max 891.5ms)</text>
<text x="598" y="316" font-size="12">&gt;20ms</text>
</svg>
```

그림 3 — 실제 측정 히스토그램(Image-trace, 실시간 TCG, 60초, 두 CPU 합산). x축·y축 모두 로그. 오른쪽 막대는 20 ms를 넘은 오버플로 샘플 수, 점선은 max. 조용한 호스트의 분포(파랑·초록)는 수백 µs에 뾰족한 봉우리와 수 ms까지 얇은 꼬리, 바쁜 호스트의 분포(주황·빨강)는 꼬리가 두꺼운 고원이다.

왜 로그 y축인가: 1 µs 버킷 하나에 수만 개가 몰리고 꼬리 버킷에는 1~2개가 있다. 선형 y축이면 꼬리가 0처럼 보여 사라진다. RT 세계의 히스토그램(OSADL 등)은 거의 항상 **로그 y축**이다. 이 노트는 범위가 µs~수백 ms로 넓어서 x축도 로그(10구간/decade로 다시 묶음)로 그렸다. 실제 보드의 결과(수~수백 µs)라면 x축은 선형 1 µs 버킷으로 그리는 것이 보통이다.

그림 생성 스크립트는 같은 히스토그램 파일을 읽어 10구간/decade로 다시 묶고 좌표를 Python으로 계산해 SVG를 쓴다(이 노트의 히스토그램 그림 3장이 모두 같은 스크립트 출력이다. 50줄이 넘어 본문에는 핵심 부분만).

```python
edges = [10.0 * 10 ** (i / 10) for i in range(41)]          # 10 us .. 100 ms, 10 bins/decade
def xp(v): return L + (math.log10(v) - 1) / 4 * (R - L)      # log x
def yp(c): return B - ((math.log10(c) + 0.5) / 6.5) * (B - T) if c > 0 else B   # log y
for us, c in enumerate(counts):                               # counts[us] = 1 us bucket
    if c: bins[min(39, int(10 * (math.log10(max(us, 10)) - 1)))] += c
```

### 4.4 icount 모드에서 같은 측정 — 두 봉우리(bimodal)

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="14">cyclictest 30 s, Image-trace, icount 모드</text>
<line x1="60" y1="280" x2="590" y2="280" stroke="currentColor"/>
<line x1="60" y1="56" x2="60" y2="280" stroke="currentColor"/>
<line x1="60.0" y1="280" x2="60.0" y2="284" stroke="currentColor"/>
<text x="60.0" y="298" font-size="12" text-anchor="middle">10us</text>
<line x1="192.5" y1="280" x2="192.5" y2="284" stroke="currentColor"/>
<text x="192.5" y="298" font-size="12" text-anchor="middle">100us</text>
<line x1="325.0" y1="280" x2="325.0" y2="284" stroke="currentColor"/>
<text x="325.0" y="298" font-size="12" text-anchor="middle">1ms</text>
<line x1="457.5" y1="280" x2="457.5" y2="284" stroke="currentColor"/>
<text x="457.5" y="298" font-size="12" text-anchor="middle">10ms</text>
<line x1="590.0" y1="280" x2="590.0" y2="284" stroke="currentColor"/>
<text x="590.0" y="298" font-size="12" text-anchor="middle">100ms</text>
<line x1="56" y1="262.8" x2="60" y2="262.8" stroke="currentColor"/>
<text x="52" y="266.8" font-size="12" text-anchor="end">1e0</text>
<line x1="56" y1="193.8" x2="60" y2="193.8" stroke="currentColor"/>
<text x="52" y="197.8" font-size="12" text-anchor="end">1e2</text>
<line x1="56" y1="124.9" x2="60" y2="124.9" stroke="currentColor"/>
<text x="52" y="128.9" font-size="12" text-anchor="end">1e4</text>
<line x1="56" y1="56.0" x2="60" y2="56.0" stroke="currentColor"/>
<text x="52" y="60.0" font-size="12" text-anchor="end">1e6</text>
<text x="325.0" y="316" font-size="12" text-anchor="middle">latency (log)</text>
<text x="14" y="44" font-size="12">count (log)</text>
<polyline points="60.0,280.0 73.3,280.0 73.3,280.0 86.5,280.0 86.5,280.0 99.8,280.0 99.8,280.0 113.0,280.0 113.0,280.0 126.2,280.0 126.2,280.0 139.5,280.0 139.5,280.0 152.8,280.0 152.8,280.0 166.0,280.0 166.0,280.0 179.2,280.0 179.2,280.0 192.5,280.0 192.5,103.4 205.8,103.4 205.8,117.3 219.0,117.3 219.0,167.2 232.2,167.2 232.2,198.0 245.5,198.0 245.5,252.4 258.8,252.4 258.8,205.5 272.0,205.5 272.0,175.2 285.2,175.2 285.2,220.4 298.5,220.4 298.5,280.0 311.8,280.0 311.8,262.8 325.0,262.8 325.0,280.0 338.2,280.0 338.2,280.0 351.5,280.0 351.5,262.8 364.8,262.8 364.8,280.0 378.0,280.0 378.0,280.0 391.2,280.0 391.2,280.0 404.5,280.0 404.5,280.0 417.8,280.0 417.8,280.0 431.0,280.0 431.0,280.0 444.2,280.0 444.2,280.0 457.5,280.0 457.5,280.0 470.7,280.0 470.7,280.0 484.0,280.0 484.0,280.0 497.2,280.0 497.2,280.0 510.5,280.0 510.5,280.0 523.8,280.0 523.8,280.0 537.0,280.0 537.0,280.0 550.2,280.0 550.2,280.0 563.5,280.0 563.5,280.0 576.8,280.0 576.8,280.0 590.0,280.0" fill="none" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="360.2" y1="56" x2="360.2" y2="280" stroke="#4a7bd0" stroke-dasharray="3 3"/>
<line x1="340" y1="58.0" x2="360" y2="58.0" stroke="#4a7bd0" stroke-width="2"/>
<text x="366" y="62.0" font-size="12">idle (max 1.8ms)</text>
<polyline points="60.0,280.0 73.3,280.0 73.3,280.0 86.5,280.0 86.5,280.0 99.8,280.0 99.8,280.0 113.0,280.0 113.0,280.0 126.2,280.0 126.2,280.0 139.5,280.0 139.5,280.0 152.8,280.0 152.8,280.0 166.0,280.0 166.0,280.0 179.2,280.0 179.2,280.0 192.5,280.0 192.5,147.6 205.8,147.6 205.8,129.3 219.0,129.3 219.0,111.1 232.2,111.1 232.2,125.1 245.5,125.1 245.5,222.2 258.8,222.2 258.8,177.8 272.0,177.8 272.0,135.6 285.2,135.6 285.2,175.9 298.5,175.9 298.5,202.8 311.8,202.8 311.8,252.4 325.0,252.4 325.0,229.9 338.2,229.9 338.2,252.4 351.5,252.4 351.5,280.0 364.8,280.0 364.8,280.0 378.0,280.0 378.0,280.0 391.2,280.0 391.2,280.0 404.5,280.0 404.5,280.0 417.8,280.0 417.8,280.0 431.0,280.0 431.0,280.0 444.2,280.0 444.2,280.0 457.5,280.0 457.5,280.0 470.7,280.0 470.7,280.0 484.0,280.0 484.0,280.0 497.2,280.0 497.2,280.0 510.5,280.0 510.5,280.0 523.8,280.0 523.8,280.0 537.0,280.0 537.0,280.0 550.2,280.0 550.2,280.0 563.5,280.0 563.5,280.0 576.8,280.0 576.8,280.0 590.0,280.0" fill="none" stroke="#d0564a" stroke-width="1.6"/>
<line x1="349.7" y1="56" x2="349.7" y2="280" stroke="#d0564a" stroke-dasharray="3 3"/>
<line x1="340" y1="76.0" x2="360" y2="76.0" stroke="#d0564a" stroke-width="2"/>
<text x="366" y="80.0" font-size="12">loaded (max 1.5ms)</text>
</svg>
```

그림 4 — icount 모드(호스트 소음 제거)의 cyclictest 30초. 100~200 µs의 주 봉우리 옆에 300~600 µs의 두 번째 봉우리가 있다.

```text
  thr       n    p50     p99   p99.9  p99.99      max  over
   T0   29999     121     150     233     397     1842  0
   T1   29824     121     436     467     570      591  0
  all   59823     121     193     465     565     1842  0
  thr       n    p50     p99   p99.9  p99.99      max  over
   T0   29990     193     221     245    1072     1536  0
   T1   20308     192     597     678     808     1191  0
  all   50298     193     451     656    1069     1536  0
```

(위 두 블록이 idle, 아래가 loaded.) 출력에서 볼 것: 스레드별로 나눠 보면 두 번째 봉우리는 **거의 CPU1(T1)에서만** 나온다(T0 p99 = 150 µs, T1 p99 = 436 µs). icount 모드는 두 vCPU를 한 호스트 스레드에서 번갈아 실행하는데, 그 교대 구조가 CPU1에 고정된 추가 지연을 만든 것으로 보인다. 이 해석은 에뮬레이터 내부 동작에 대한 추정이다. 일반 원칙은 확실하다:

### 4.5 히스토그램 모양 읽기

| 모양 | 뜻 | 실제 보드에서 흔한 원인 |
|---|---|---|
| 뾰족한 봉우리 하나 + 얇은 꼬리가 금방 끊김 | 건강함. 꼬리 끝이 max | 잘 튜닝된 PREEMPT_RT |
| 봉우리 + 꼬리가 길게 고원처럼 이어짐 | 원인이 여러 개, 빈도도 다양 | 비 RT 커널 + 부하, 공유 자원 경쟁(메모리 대역폭, 락) |
| 두 봉우리(bimodal) | **서로 다른 경로 두 개**가 섞였다 | idle 탈출 vs 이미 깨어 있음, CPU별 차이(격리 CPU vs 아닌 CPU), 주기적 작업과 겹친 경우 |
| 주 봉우리에서 떨어진 작은 섬 | 드물지만 **일정한 크기**의 지연원 | 주기적으로 IRQ를 끄는 드라이버, 일정한 길이의 펌웨어 호출(SMC), 타이머 tick 작업 |
| 오버플로가 0이 아님 | 히스토그램 범위가 모자랐다 | `-h`를 늘리거나 max를 따로 보고 |

bimodal을 보면 **먼저 스레드(CPU)별로 쪼개 본다** — 위 icount 예처럼 한 CPU의 문제인 경우가 많다. 그다음 "작은 섬"의 크기를 재고, 그 크기와 같은 길이의 작업(예: 10 ms마다 400 µs IRQ off 하는 드라이버)을 찾는다. 섬의 위치 = 범인의 길이, 섬의 높이 = 범인의 빈도 × 측정 주기와 겹칠 확률이다.

---

## 5. 얼마나 오래 재야 하나 — 짧은 측정은 거짓말한다

### 5.1 직관 — 드문 사건을 만날 확률

최악의 지연은 "드문 일 두 개가 겹칠 때" 생긴다(타이머 만료 순간에 마침 긴 IRQ off 구간이 돌고 있다 등). 어떤 스파이크가 평균 한 시간에 한 번 생긴다고 하자(포아송 가정). T 동안 측정해서 그것을 한 번이라도 볼 확률은 `1 - exp(-T / 1h)`다.

| 측정 시간 | 한 시간에 한 번 생기는 스파이크를 볼 확률 |
|---|---|
| 1분 | 1.6% |
| 10분 | 15% |
| 1시간 | 63% |
| 3시간 | 95% |
| 24시간 | 거의 100% |

(값은 Python으로 계산했다: `1 - math.exp(-T/3600)`.) 95%를 원하면 "평균 간격의 3배"를 재야 한다(`-ln 0.05 ≈ 3`). 반대로 말하면 "N개 샘플에서 한 번도 안 나왔다"는 결과는 그 사건의 확률이 대략 `3/N`보다 작다는 것만 말해 준다(통계의 "rule of three"). 1 kHz로 1시간이면 N = 360만 → p99.9999 근처까지만 근거가 생긴다.

그래서 업계의 관행은 개발 중 수십 분, 릴리스 판정 수 시간~24시간, 장기 품질 판정 며칠이다(OSADL QA Farm은 보드마다 장기간 반복 측정한 히스토그램을 공개한다).

### 5.2 예제 5 — max vs 측정 시간 (Image-rt, 부하, CPU1)

무엇을 확인하나: 같은 부하를 계속 걸어 둔 채 cyclictest를 1초(3번), 10초(2번), 60초, 300초 돌리고 각 결과의 p99/p99.9/max를 비교한다.

```sh
load_on 900; sleep 3          # stress-ng cpu/vm/io/hdd + hackbench (3.1절)
for D in 1 1 1 10 10 60 300; do
  cyclictest -m -p 95 -a 1 -t 1 -i 1000 -h 20000 -q -D $D --histfile=$O/d$D.$RANDOM.hist
done
for f in d1.9111 d1.4274 d1.29837 d10.14835 d10.32069 d60.22571 d300.3455; do
  echo "== $f"; python3 -I n3_pct.py $f.hist | sed -n 2p; done
```

```text
== d1.9111
   T0    1000     194     333     364     374      374  0
== d1.4274
   T0     990     138     602    3493    3493     3493  0
== d1.29837
   T0     982     115     961    4935    4935     4935  0
== d10.14835
   T0    9877     165     414    4692   14533    14533  0
== d10.32069
   T0    9895     275     365    4069   18677    18677  0
== d60.22571
   T0   59379     208     369    4008   16679    21409  1
== d300.3455
   T0  297018     148     249    3712   16524    39182  13
```

(열: 스레드, n, p50, p99, p99.9, p99.99, max, overflow. 파일 이름 뒤 숫자는 `$RANDOM`이라 실행 순서와 무관하다.)

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="14">측정 시간을 늘리면 max만 자란다 (Image-rt, 부하, CPU1, 실시간 TCG)</text>
<line x1="70" y1="270" x2="600" y2="270" stroke="currentColor"/>
<line x1="70" y1="50" x2="70" y2="270" stroke="currentColor"/>
<line x1="121.7" y1="270" x2="121.7" y2="274" stroke="currentColor"/>
<text x="121.7" y="288" font-size="12" text-anchor="middle">1 s</text>
<line x1="293.9" y1="270" x2="293.9" y2="274" stroke="currentColor"/>
<text x="293.9" y="288" font-size="12" text-anchor="middle">10 s</text>
<line x1="427.9" y1="270" x2="427.9" y2="274" stroke="currentColor"/>
<text x="427.9" y="288" font-size="12" text-anchor="middle">60 s</text>
<line x1="548.3" y1="270" x2="548.3" y2="274" stroke="currentColor"/>
<text x="548.3" y="288" font-size="12" text-anchor="middle">300 s</text>
<line x1="66" y1="270.0" x2="600" y2="270.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="274.0" font-size="12" text-anchor="end">100us</text>
<line x1="66" y1="196.7" x2="600" y2="196.7" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="200.7" font-size="12" text-anchor="end">1ms</text>
<line x1="66" y1="123.3" x2="600" y2="123.3" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="127.3" font-size="12" text-anchor="end">10ms</text>
<line x1="66" y1="50.0" x2="600" y2="50.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="54.0" font-size="12" text-anchor="end">100ms</text>
<text x="335.0" y="306" font-size="12" text-anchor="middle">측정 시간 (log)</text>
<circle cx="121.7" cy="228.0" r="4" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<circle cx="121.7" cy="156.8" r="4" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<circle cx="121.7" cy="145.8" r="4" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<circle cx="293.9" cy="111.4" r="4" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<circle cx="293.9" cy="103.4" r="4" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<circle cx="427.9" cy="99.1" r="4" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<circle cx="548.3" cy="79.8" r="4" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<polyline points="121.7,145.8 293.9,103.4 427.9,99.1 548.3,79.8" fill="none" stroke="#d0564a" stroke-width="1.5"/>
<circle cx="121.7" cy="228.9" r="4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<circle cx="121.7" cy="156.8" r="4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<circle cx="121.7" cy="145.8" r="4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<circle cx="293.9" cy="147.4" r="4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<circle cx="293.9" cy="152.0" r="4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<circle cx="427.9" cy="152.5" r="4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<circle cx="548.3" cy="154.9" r="4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<polyline points="121.7,145.8 293.9,147.4 427.9,152.5 548.3,154.9" fill="none" stroke="#e08a3c" stroke-width="1.5"/>
<circle cx="121.7" cy="231.7" r="4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<circle cx="121.7" cy="212.8" r="4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<circle cx="121.7" cy="197.9" r="4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<circle cx="293.9" cy="224.8" r="4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<circle cx="293.9" cy="228.8" r="4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<circle cx="427.9" cy="228.4" r="4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<circle cx="548.3" cy="240.9" r="4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<polyline points="121.7,197.9 293.9,224.8 427.9,228.4 548.3,240.9" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<circle cx="110" cy="56" r="4" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="120" y="60" font-size="12">max</text>
<circle cx="110" cy="74" r="4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<text x="120" y="78" font-size="12">p99.9</text>
<circle cx="110" cy="92" r="4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="120" y="96" font-size="12">p99</text>
<text x="76" y="322" font-size="12">1 s를 3번, 10 s를 2번 반복 — 같은 1 s 측정의 max가 374 us ~ 4935 us로 13배 흔들린다</text>
</svg>
```

그림 5 — 위 실행 결과를 그린 것. 빨강 max는 측정 시간에 따라 0.37 ms → 39 ms로 계속 자라고, 주황 p99.9는 10초 이후 3.7~4.7 ms에서 거의 변하지 않는다.

출력에서 볼 것:

- **같은 1초 측정 세 번의 max가 374 µs, 3493 µs, 4935 µs** — 13배 차이. 1초짜리 측정 하나로 "max 374 µs"라고 보고했다면 운이 좋았을 뿐이다.
- **max는 측정 시간과 함께 단조롭게 커진다**(10 s: 14.5~18.7 ms, 60 s: 21.4 ms, 300 s: 39.2 ms). 측정을 더 늘리면 더 커질 가능성이 높다. "max가 더 이상 자라지 않는 시간"을 찾는 것이 측정 시간 결정의 기준이다.
- **p99.9는 10초 이후 안정**(4.7 → 4.0 → 3.7 ms). 백분위는 짧은 측정에서도 비교적 안정적으로 추정되지만, 제어 루프가 깨지는 것은 max다. 그래서 보고서에는 둘 다 쓴다: "p99.9는 분포의 몸통, max는 측정 시간 동안 관측된 최악".
- 300초 측정에서도 n = 297018로 기대값(300000)보다 약 3000 적다. 부하 아래서 주기를 놓친 샘플이다.

---

## 6. osnoise — 바쁜 워크로드는 무엇에게 시간을 빼앗기나

### 6.1 직관과 정의

cyclictest·timerlat은 "자다가 깨는" 워크로드를 흉내 낸다. 그런데 폴링 드라이버, DPDK식 패킷 처리, 추론 루프처럼 **CPU를 계속 쥐고 도는** 워크로드도 있다. 그런 워크로드에게 중요한 질문은 "얼마나 늦게 깨나"가 아니라 "돌고 있는 동안 얼마나 자주, 얼마나 오래 끊기나"다.

osnoise tracer는 CPU마다 kthread 하나를 띄워 `runtime` 동안 시계를 연속으로 읽는다. 두 읽기 사이 간격이 임계값(`-T`, 기본 1 µs)을 넘으면 "noise"로 세고, 그 사이에 발생한 IRQ·softirq·NMI·다른 thread를 tracepoint로 추적해 원인별 카운트를 붙인다. 아무 OS 이벤트도 없었는데 시간이 사라졌으면 **HW** 칸으로 간다(12.2절).

`rtla osnoise top` 열의 뜻:

| 열 | 뜻 |
|---|---|
| Period / Runtime | 주기 수, 측정 스레드가 돈 시간 합(µs) |
| Noise | 빼앗긴 시간 합(µs) |
| % CPU Aval | 워크로드가 실제로 쓸 수 있었던 비율 = (Runtime − Noise) / Runtime |
| Max Noise | 한 주기 안 노이즈 합의 최대 |
| Max Single | 노이즈 한 번의 최대 길이 |
| HW / NMI / IRQ / Softirq / Thread | 원인별 발생 **횟수** |

### 6.2 예제 6 — 함정: 임계값이 플랫폼보다 작으면 전부 노이즈가 된다

기본 임계값(1 µs)으로 TCG 게스트에서 돌린 결과다.

```sh
rtla osnoise top -d 20 -q
```

```text
                                          Operating System Noise                                                                          
duration:   0 00:00:21 | time is in us
CPU Period       Runtime        Noise  % CPU Aval   Max Noise   Max Single          HW          NMI          IRQ      Softirq       Thread
  0 #19         19000146     19000063     0.00043     1000129        17196     5086764            0         4730          593          120
  1 #20         20000072     19999838     0.00116     1000037        23630     5328975            0         5000          449          119
```

출력에서 볼 것: CPU 가용률 0.0004% — 측정 시간 전부가 노이즈다. HW 노이즈가 20초에 500만 번. 이 게스트는 에뮬레이션이라 시계를 두 번 읽는 루프 한 바퀴가 이미 1 µs를 자주 넘는다. **측정 도구의 해상도·임계값이 플랫폼에 맞지 않으면 숫자가 의미를 잃는다.** 실제 보드라면 기본값 1 µs가 적절하지만, 느린 코어나 VM에서는 `-T`로 올린다.

### 6.3 예제 7 — `-T 50`으로 다시: 조용할 때, 시끄러운 이웃이 있을 때, RT 우선순위를 줬을 때

무엇을 확인하나: 임계값 50 µs로 (a) 조용한 시스템, (b) CPU1에 `stress-ng --cpu 1`(SCHED_OTHER 계산 부하) + n3_spike(100 ms마다 2 ms IRQ off), (c) CPU1에 stress-ng만 두고 osnoise 스레드를 SCHED_FIFO 95로(`-P f:95`).

```sh
rtla osnoise top -T 50 -d 20 -q
stress-ng --cpu 1 --taskset 1 --timeout 50s -q &
insmod /root/n3/n3_spike-trace.ko cpu=1 us=2000 period_ms=100 mode=0
rtla osnoise top -T 50 -d 20 -q
rmmod n3_spike; stress-ng --cpu 1 --taskset 1 --timeout 25s -q &
rtla osnoise top -T 50 -P f:95 -d 20 -q
```

```text
== osnoise top -T 50, quiet
CPU Period       Runtime        Noise  % CPU Aval   Max Noise   Max Single          HW          NMI          IRQ      Softirq       Thread
  0 #19         19000093      1427495    92.48690      116191        33075        1718            0         4741          333          156
  1 #19         19006039      1314005    93.08638      110103        32108        1831            0         4754          551          100
== osnoise top -T 50, noisy: stress-ng CPU1 + n3_spike irq-off 2000us/100ms CPU1
  0 #19         19000479      1692751    91.09100      127642        36051        2185            0         4761          334          192
  1 #19         19015877     10545106    44.54578      579619        52234         421            0         4708          731         2235
== osnoise top -T 50 -P f:95, stress-ng on CPU1
[   99.991462][    C0] sched: RT throttling activated
  0 #19         19001349      1941406    89.78279      132163        53271        2222            0         4818          538          263
  1 #19         19009653      2282806    87.99133      149570        55462        2202            0         4748          618           97
```

(제목 줄 생략.) 출력에서 볼 것:

- **IRQ 횟수 약 4750 = 19초 × 250 Hz**. Image-trace는 `HZ=250`이라 scheduler tick이 초당 250번이다. 조용한 시스템의 IRQ 노이즈는 거의 다 tick이다. (Image-rt는 `HZ=1000` + `NO_HZ_FULL` — 6.4에서 비교.)
- **조용할 때 가용률 92~93%**. 남은 7%는 HW 노이즈(약 1700~1800회) — 8절에서 보듯 에뮬레이터/호스트가 vCPU를 멈춘 시간이다. 실제 보드라면 99.9% 이상을 기대한다.
- **시끄러운 이웃**: CPU1의 가용률이 44.5%로 반토막. Thread 칸이 100 → 2235회로 뛰었다. stress-ng와 n3_spike kthread가 osnoise 스레드(SCHED_OTHER)와 CPU를 나눠 썼기 때문이다. CPU0은 거의 그대로(91%) — 노이즈가 CPU별로 분리돼 보이는 것이 osnoise의 장점이다.
- **`-P f:95`**: osnoise 스레드를 SCHED_FIFO 95로 올리자 같은 stress-ng가 있어도 CPU1 가용률이 88%로 회복, Thread 칸은 97회. 일반 스레드는 RT 스레드를 끊지 못한다. 대신 커널이 `sched: RT throttling activated`를 찍었다 — RT 태스크가 1초 중 0.95초를 넘게 쓰려 하자 RT throttling(F1 §5.2, N2)이 끼어들어 나머지 시간을 일반 태스크에 준 것이고, 그 시간이 다시 노이즈로 잡힌다. **바쁜 루프를 RT로 돌리는 제품이라면 이 throttling이 주기적 노이즈원이 된다.**

같은 측정을 **Image-rt**(PREEMPT_RT, `HZ=1000`)에서 하면 열의 모양이 달라진다:

```text
== osnoise top -T 50, quiet   (6.18.55-rt)
CPU Period       Runtime        Noise  % CPU Aval   Max Noise   Max Single          HW          NMI          IRQ      Softirq       Thread
  0 #19         19000100      2552407    86.56634      194174        56286        3459            0        18727            0          540
  1 #19         19002263      2722554    85.67247      200719        70301        2957            0        18688            0          691
== osnoise top -T 50, noisy: stress-ng CPU1 + n3_spike irq-off 2000us/100ms CPU1
  0 #18         18003372      2388539    86.73282      193630        19378        3639            0        17695            0          601
  1 #18         18002380     10780406    40.11677      627046        29281        1031            0        17535            0         5290
```

- IRQ 횟수가 19초에 약 18700 — `HZ=1000`이라 tick이 4배다. (`NO_HZ_FULL`이 켜져 있어도 `nohz_full=` 부트 인자로 CPU를 지정하지 않았으니 tick은 멈추지 않는다 — N2.)
- **Softirq 칸이 0**이다. PREEMPT_RT에서는 softirq가 IRQ 출구가 아니라 스레드 문맥에서 돌기 때문에 osnoise는 그것을 Thread 노이즈로 센다. 같은 도구라도 커널 설정에 따라 "어느 칸에 들어가나"가 바뀐다는 것을 알고 읽어야 한다.

> 이 RT 게스트에서 `rtla osnoise top -P f:95`(바쁜 루프를 SCHED_FIFO 95로 100% 돌림)를 실행하자 게스트 전체가 멈춰 20분 동안 돌아오지 않았다(콘솔 메시지도 없어 QEMU를 강제 종료했다). 같은 명령이 Image-trace에서는 RT throttling 메시지와 함께 끝났다. **원인은 확인하지 못했다.** 가장 그럴듯한 가설: PREEMPT_RT에서는 타이머 softirq 처리가 낮은 RT 우선순위의 커널 스레드(`ktimers/N`)에서 도는데, FIFO 95 루프가 CPU를 독점하면 그 스레드가 굶어 rtla의 종료 타이머조차 처리되지 않는다. 6.12 이후 RT throttling 자리를 대신하는 fair server(deadline server)는 SCHED_OTHER 태스크에게 시간을 보장할 뿐 더 낮은 RT 스레드를 구해 주지 않는다. 교훈은 확실하다: **RT 커널에서 100% 바쁜 RT 루프가 커널의 RT 스레드(타이머, softirq, IRQ 스레드)보다 우선순위가 높으면 시스템이 멈출 수 있다.** RT 우선순위로 osnoise를 돌릴 때는 `-r`(runtime)을 `-p`(period)보다 작게 줘 여유를 둔다.

### 6.4 예제 8 — `rtla osnoise hist`와 `-s` 정지 + 트레이스

```sh
rtla osnoise hist -T 50 -d 20 -b 100 -E 100 > noisy.hist   # 100 us 버킷 100개
```

시끄러운 이웃이 있을 때 결과의 끝부분(CPU0, CPU1 두 열):

```text
7700          0         6 
7800          0        14 
7900          0        15 
8000          0        11 
8100          0         3 
...
9800          1         0 
over:         4        17 
count:     6988      4733 
min:          0         0 
avg:     192.93   1478.77 
max:      25000     90000 
```

(0인 줄과 중간 일부 생략.) 출력에서 볼 것: CPU1에는 7.7~8.1 ms 근처에 노이즈 샘플이 몰려 있다 — 2 ms IRQ off와 stress-ng의 CFS time slice가 겹친 고정 크기 덩어리로 보인다(정확한 분해는 하지 않았다). CPU1 평균 노이즈 1478 µs 대 CPU0 193 µs.

`-s 5000 -t`로 "5 ms 넘는 단일 노이즈에서 멈추고 트레이스 저장"을 걸었다(n3_spike는 1초마다 8 ms IRQ off).

```sh
rtla osnoise top -s 5000 -T 50 -t -q -d 60; echo "rc=$?"
```

```text
  Saving trace to osnoise_trace.txt
osnoise hit stop tracing
rc=2
```

저장된 트레이스(778줄)의 마지막 줄:

```text
osnoise/0-119     [000] ....1   131.551631: sample_threshold: start 131.541696352 duration 9678400 ns interference 0
```

그리고 그 직전 다른 줄들:

```text
rtla-118     [000] d..3.   131.119376: thread_noise:     rtla:118 start 131.115486688 duration 3794096 ns
osnoise/0-119     [000] ....1   131.119408: sample_threshold: start 131.115297808 duration 4108736 ns interference 3
```

출력에서 볼 것: 정지를 일으킨 것은 n3_spike가 있는 CPU1이 아니라 **CPU0의 9.68 ms짜리 노이즈, `interference 0`**이었다 — OS 이벤트가 하나도 없었는데 시간이 사라졌다, 즉 HW(여기서는 호스트) 노이즈다. 두 번째로 볼 것은 CPU0에서 **rtla 자신**(`rtla:118`)이 3.8 ms 노이즈를 만들었다는 점이다. 측정 도구의 수집 스레드도 CPU를 쓴다. 그래서 rtla에는 `-H <cpus>`(housekeeping: rtla 자신을 이 CPU들에만)와 `-c <cpus>`(측정할 CPU) 옵션이 있다. 실제 측정에서는 `-c 2-3 -H 0`처럼 측정 대상과 수집기를 분리한다.

---

## 7. timerlat — 지연을 "IRQ 단계"와 "thread 단계"로 가른다

### 7.1 직관

cyclictest의 한 숫자를 두 개로 쪼갠다. 타이머가 만료되면 **timerlat의 hrtimer 콜백(IRQ 문맥)**이 먼저 "지금 몇 µs 늦었나"를 기록하고(IRQ latency), 측정 스레드를 깨운다. 스레드가 실제로 돌기 시작하면 다시 기록한다(Thread latency). 사용자 스레드 모드에서는 유저 공간으로 돌아온 시각도 잰다(Ret user).

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14">timerlat: 같은 타이머로 두 지점을 잰다</text>
<line x1="40" y1="250" x2="640" y2="250" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="90" y1="50" x2="90" y2="260" stroke="#4a7bd0" stroke-dasharray="4 3"/>
<text x="50" y="275" font-size="12">타이머 만료 시각</text>
<rect x="90" y="190" width="120" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="98" y="215" font-size="12">IRQ 못 받음</text>
<line x1="210" y1="50" x2="210" y2="260" stroke="#e08a3c"/>
<text x="168" y="275" font-size="12">timerlat IRQ 진입</text>
<rect x="210" y="190" width="50" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="216" y="215" font-size="12">IRQ</text>
<rect x="260" y="190" width="200" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="268" y="207" font-size="12">thread 깨움 → 아직 못 돎</text>
<text x="268" y="223" font-size="12">(preempt off, 다른 RT 등)</text>
<line x1="460" y1="50" x2="460" y2="260" stroke="#d0564a"/>
<text x="410" y="275" font-size="12">timerlat thread 실행</text>
<line x1="94" y1="80" x2="206" y2="80" stroke="#e08a3c" marker-end="url(#ah3)"/>
<text x="98" y="72" font-size="13">IRQ latency</text>
<line x1="94" y1="130" x2="456" y2="130" stroke="#d0564a" marker-end="url(#ah3)"/>
<text x="220" y="122" font-size="13">Thread latency (IRQ latency 포함)</text>
<text x="20" y="300" font-size="12">IRQ latency 큼 → 누군가 IRQ를 막았다 (local_irq_save, 긴 hardirq, 펌웨어/하이퍼바이저)</text>
<text x="20" y="320" font-size="12">IRQ 작고 Thread만 큼 → 스케줄링 문제 (preempt off, 우선순위, 긴 softirq·thread)</text>
</svg>
```

그림 6 — timerlat의 두 측정점. 두 값의 **차이**가 진단의 핵심이다.

판별 규칙은 표 하나로 외운다.

| IRQ latency | Thread latency | 의심할 곳 |
|---|---|---|
| 작다 | 작다 | 정상 |
| 크다 | 크다(IRQ와 비슷) | IRQ가 막혔다: `local_irq_save`/`spin_lock_irqsave` 긴 구간, 긴 hardirq 핸들러, idle 탈출, OS 밖 정지 |
| 작다 | 크다 | IRQ는 제때 왔는데 스레드가 못 돌았다: `preempt_disable`/`spin_lock`(비 RT) 구간, 더 높은 RT 태스크, 긴 softirq |

### 7.2 예제 9 — `rtla timerlat top`: 조용할 때, IRQ off 스파이크, preempt off 스파이크

무엇을 확인하나: 같은 측정을 세 번 한다. n3_spike로 CPU1에 500 ms마다 20 ms짜리 (a) IRQ off, (b) preempt off 구간을 넣었을 때 IRQ 열과 Thread 열이 어떻게 다르게 반응하는가. **icount 모드**(호스트 소음 제거)에서 실행했다.

```sh
rtla timerlat top -d 20 -q
insmod /root/n3/n3_spike-trace.ko cpu=1 us=20000 period_ms=500 mode=0
rtla timerlat top -d 20 -q; rmmod n3_spike
insmod /root/n3/n3_spike-trace.ko cpu=1 us=20000 period_ms=500 mode=1
rtla timerlat top -d 20 -q; rmmod n3_spike
```

```text
== timerlat top quiet
CPU COUNT      |      cur       min       avg       max |      cur       min       avg       max |      cur       min       avg       max
  0 #19967     |       25        17        22        50 |       95        86        95       345 |      202       193       201       529
  1 #19946     |       84        17       182       686 |      365        87       349      1347 |      679       179       682      1665
== timerlat top with n3_spike mode=0 (20000us every 500ms on CPU1)
  0 #19921     |       17         8        22       196 |      307        87       277     52737 |      414       192       425     52951
  1 #19247     |       79        17        76     20008 |      160       130       192     20448 |      250       180       304     20536
[  279.735430][  T211] n3_spike: 83 spikes injected
== timerlat top with n3_spike mode=1 (20000us every 500ms on CPU1)
  0 #19988     |       20        16        22       211 |       87        85       104       738 |      177       176       216       854
  1 #19255     |      194        23        74       397 |      274       103       223     19970 |      365       175       326     20061
[  324.846365][  T218] n3_spike: 83 spikes injected
```

(헤더 두 줄과 ALL 합계 줄은 생략했다. 세 열 묶음은 왼쪽부터 IRQ, Thread, Ret user, 단위 µs.)

출력에서 볼 것:

- **IRQ off(mode=0)**: CPU1의 IRQ max가 20008 µs — 주입한 20 ms가 거의 그대로 IRQ 단계에 찍혔다. Thread max(20448)는 IRQ max + 수백 µs. "IRQ 크고 Thread도 비슷" → IRQ가 막혔다.
- **preempt off(mode=1)**: CPU1의 IRQ max는 397 µs로 조용할 때(686)와 같은 수준이다. 그런데 Thread max는 19970 µs. "IRQ 작고 Thread만 큼" → 스케줄링 문제. timerlat이 아니라 cyclictest였다면 두 경우 모두 "max 20 ms" 한 숫자였다.
- IRQ off 실험에서 CPU0의 Thread max가 52737 µs로 튀었다. CPU0에는 스파이크를 넣지 않았다. icount 모드는 vCPU 두 개를 한 호스트 스레드에서 번갈아 돌리므로, CPU1이 IRQ를 끈 채 오래 돌면 CPU0의 실행 기회 배분에도 영향이 있을 수 있다 — **이 노트에서는 원인을 확정하지 못했다**. 실제 보드라면 CPU 간 의존(IPI 응답 대기, 같은 락, RCU)부터 의심하고 7.4의 `--auto`로 CPU0을 따로 잡는다.
- 조용할 때도 CPU1의 평균(182 µs)이 CPU0(22 µs)보다 훨씬 크다. 4.4절의 icount 히스토그램에서 본 "두 번째 봉우리"와 같은 현상(에뮬레이터의 vCPU 교대)이다.

### 7.3 예제 10 — `rtla timerlat hist`

`top`이 요약이라면 `hist`는 분포다. 옵션은 cyclictest의 `-h`와 같은 역할이다.

```sh
rtla timerlat hist -d 30 -b 100 -E 200 > tl_load.hist     # 100 us 버킷 200개 = 0~20 ms
```

```text
# RTLA timerlat histogram
# Time unit is microseconds (us)
# Duration:   0 00:00:37
Index   IRQ-000   Thr-000   Usr-000   IRQ-001   Thr-001   Usr-001
0          6754         4         0      6942         0         0 
over:        27        37        47        26        37        41 
count:    13771     13771     13771     16380     16380     16380 
min:         16        74       131        13       100       157 
avg:        892      1264      1592       597       920      1144 
max:    1003663   1006335   1006609    530378    573537    573788 
```

(실시간 TCG 모드, 호스트가 바쁠 때(load average 약 23) 부하를 걸고 잰 결과에서 버킷 0과 요약 줄만 남겼다.) 출력에서 볼 것: `-d 30`을 줬는데 `Duration`이 37초이고, count가 30초 × 1000 Hz = 30000에 한참 못 미친다(13771, 16380). 측정 스레드가 주기를 놓친 것이다. max가 1초(1003663 µs)다 — 게스트 vCPU가 호스트에서 통째로 1초 동안 실행되지 못한 것으로 보인다. 같은 구간 커널 로그에는 `rcu: INFO: rcu_preempt detected stalls`(khugepaged가 RCU read 구간에서 선점된 채 오래 못 돌았다)도 찍혔다. **count가 기대값과 다르면 그 자체가 결과다** — 측정이 측정 대상을 따라가지 못했다는 뜻이니 max 숫자보다 먼저 보고한다.

### 7.4 예제 11 — `rtla timerlat top -a <us>`: 첫 스파이크에서 멈추고 자동 분석

`-a 8000`(`--auto`)은 다음을 한 번에 켠다(6.18.55 `timerlat_top.c` 소스에서 확인): IRQ·thread 어느 쪽이든 8000 µs를 넘으면 트레이싱 정지, IRQ에서 스택 트레이스 저장(`print_stack`), 트레이스를 `timerlat_trace.txt`로 저장, 그리고 멈춘 지점의 **auto-analysis** 출력.

무엇을 확인하나: IRQ off 10 ms 스파이크(1초마다)를 넣고 8 ms에서 멈추게 했다. 실시간 TCG 모드, 호스트가 비교적 조용할 때.

```sh
insmod /root/n3/n3_spike-trace.ko cpu=1 us=10000 period_ms=1000 mode=0
rtla timerlat top -a 8000 -q -d 60; echo "rc=$?"
```

```text
  Saving trace to timerlat_trace.txt
...
  1 #20        |     9879       102       666      9879 |      472       249       389      1120 |      659       388       561      1260
...
timerlat hit stop tracing
## CPU 1 hit stop tracing, analyzing it ##
  IRQ handler delay:                                        75.07 us (0.76 %)
  IRQ latency:                                            9879.82 us
  Blocking thread:
                       swapper/1:228      
    Blocking thread stack trace
                -> timerlat_irq
                -> __hrtimer_run_queues
                -> hrtimer_interrupt
                -> arch_timer_handler_virt
                -> handle_percpu_devid_irq
                -> handle_irq_desc
                -> generic_handle_domain_irq
                -> gic_handle_irq
                -> call_on_irq_stack
                -> do_interrupt_handler
                -> el1_interrupt
                -> el1h_64_irq_handler
                -> el1h_64_irq
                -> n3_burn
                -> n3_fn
                -> kthread
                -> ret_from_fork
------------------------------------------------------------------------
     IRQ latency:                                         9879.82 us (100%)

Max timerlat IRQ latency from idle: 404.66 us in cpu 0
rc=2
```

출력에서 볼 것:

- 시작 20회 만에(`#20`) CPU1의 **IRQ latency 9879 µs**에서 멈췄다. thread 단계까지 가지도 않았다 — IRQ 단계에서 이미 임계값을 넘었다.
- 스택 트레이스가 결정적 증거다. 타이머 IRQ(`el1h_64_irq`)가 **`n3_burn` 안에서** 들어왔다. 즉 IRQ가 늦게 들어온 이유는 `n3_burn`이 IRQ를 끄고 있었기 때문이고, `local_irq_restore()` 순간 대기 중이던 타이머 IRQ가 바로 그 자리에서 처리된 것이다. ("Blocking thread: swapper/1:228"의 이름 표기는 믿기 어렵다 — PID 228은 `insmod`(T227) 직후에 생긴 kthread, 즉 `n3_spike/1`일 가능성이 높다. IRQ off 경우에는 분석기가 "누가 돌고 있었나"를 IRQ 진입 직전 이벤트로 추정하는데 그 정보가 부족했던 것으로 보인다. **이름보다 스택을 믿는다.**)
- `rc=2`는 "임계값에 걸려 멈췄다"는 종료 코드다(`utils.h`의 `enum result`: PASSED=0, ERROR=1, FAILED=2). CI에서 자동 판정에 그대로 쓴다.

같은 방법으로 **preempt off** 스파이크를 잡은 결과(호스트가 바쁠 때의 실행, 20 ms 스파이크, 임계값 15 ms):

```text
## CPU 1 hit stop tracing, analyzing it ##
  IRQ handler delay:                                      3218.80 us (15.75 %)
  IRQ latency:                                            3244.91 us
  Timerlat IRQ duration:                                    67.79 us (0.33 %)
  Blocking thread:                                       15208.00 us (74.42 %)
                      n3_spike/1:327                     15208.00 us
    Blocking thread stack trace
                -> timerlat_irq
                ...
                -> el1h_64_irq
                -> __delay_cycles
                -> __delay
                -> __udelay
                -> n3_burn
                -> n3_fn
                -> kthread
                -> ret_from_fork
  IRQ interference                                        1859.68 us (9.10 %)
                arch_timer:11                              599.04 us
                arch_timer:11                              238.22 us
                arch_timer:11                              150.78 us
                arch_timer:11                              114.88 us
                arch_timer:11                              756.75 us
  Softirq interference                                      18.13 us (0.09 %)
                     SCHED:7                                18.13 us
------------------------------------------------------------------------
  Thread latency:                                        20435.86 us (100%)
```

(스택 중간 GIC 경로 10줄은 위와 같아서 `...`로 생략.) 이번에는 **Thread latency 20435 µs 중 74%(15208 µs)가 "Blocking thread: n3_spike/1"**이라고 이름까지 정확히 짚었다. IRQ는 3.2 ms 늦었는데(호스트 소음), 그 뒤 15 ms를 n3_spike가 선점 불가 상태로 CPU를 쥐고 있었다. 나머지는 다른 타이머 IRQ 간섭(9%) 등이다. 합이 100%가 되도록 나눠 주는 것이 auto-analysis의 가치다.

### 7.5 auto-analysis가 "범인 없음"을 말할 때

같은 preempt off 실험을 호스트가 조용할 때 다시 돌렸더니, 이번에는 n3_spike가 아닌 다른 이벤트에서 먼저 멈췄다.

```text
## CPU 0 hit stop tracing, analyzing it ##
  IRQ handler delay:                                        97.50 us (1.11 %)
  IRQ latency:                                            8775.28 us
  Blocking thread:
                       swapper/0:0        
    Blocking thread stack trace
                -> timerlat_irq
                ...
                -> el1h_64_irq
                -> default_idle_call
                -> do_idle
                -> cpu_startup_entry
                -> rest_init
                -> start_kernel
                -> __primary_switched
------------------------------------------------------------------------
     IRQ latency:                                         8775.28 us (100%)
```

CPU0이 **idle 상태(`default_idle_call`)**에서 IRQ를 8.7 ms 늦게 받았다. 게스트 OS 안에는 IRQ를 막은 코드가 없다. idle의 CPU가 타이머 IRQ에 늦게 반응하는 경우는 (1) 깊은 idle 상태의 탈출 지연(실제 보드에서는 수십~수백 µs. `--dma-latency 0`이나 `--deepest-idle-state`로 확인), (2) CPU가 아예 멈춰 있었음(펌웨어·하이퍼바이저)이다. 이 게스트에서는 (2) — QEMU의 vCPU 스레드가 호스트에서 실행되지 못한 시간이다. 이런 "OS 안에 범인이 없는" 스파이크를 직접 증명하는 도구가 8절의 hwlat이다. 이 경우 auto-analysis 출력이 "IRQ handler delay: (exit from idle)"로 표시되기도 한다(바쁜 호스트에서 잡은 IRQ off 실험의 출력 첫 줄이 `IRQ handler delay: (exit from idle) 1762.06 us`였다).

---

## 8. hwlat과 hwnoise — "OS 밖"에서 사라진 시간을 찾는다

### 8.1 직관

IRQ를 끄고 시계만 계속 읽는 루프를 생각하자. OS는 이 루프를 방해할 방법이 없다(IRQ off, 선점 불가). 그런데도 연속된 두 읽기 사이가 10 µs 벌어졌다면? CPU가 그 사이 **OS가 모르는 일**을 한 것이다. x86의 고전적 범인은 SMI(BIOS가 System Management Mode로 CPU를 빼앗음)이고, arm64에서는 EL3의 secure monitor(TF-A BL31이 SMC·PSCI·SCMI·RAS 처리), EL2 하이퍼바이저, 그리고 가상 머신이라면 호스트 스케줄러다. Don의 경험으로 말하면 "디버거가 halt를 건 시간"이나 "SSD 컨트롤러에서 다른 코어가 버스를 잡아 내 코어가 stall된 시간"을 소프트웨어만으로 찾아내는 방법이다.

| 도구 | 방식 | 출력 |
|---|---|---|
| `hwlat` tracer | `window`(기본 1 s)마다 `width`(기본 0.5 s) 동안 IRQ off로 `trace_clock_local()` 연속 읽기. CPU를 돌아가며(round-robin) | 창마다 최대 간격: inner(두 연속 읽기 사이)/outer(루프 반복 사이), 초과 횟수 count |
| `rtla hwnoise` | osnoise를 IRQ off로 돌림(IRQ·softirq·thread 소음이 원천 차단됨) | CPU별 남은 노이즈 = HW 노이즈, 가용률 % |

### 8.2 예제 12 — hwlat tracer를 tracefs로 직접 (실시간 TCG)

```sh
T=/sys/kernel/tracing
echo 10 > $T/tracing_thresh            # us
echo hwlat > $T/current_tracer
grep . $T/hwlat_detector/width $T/hwlat_detector/window $T/hwlat_detector/mode
sleep 30; echo 0 > $T/tracing_on
cat $T/trace
```

```text
/sys/kernel/tracing/hwlat_detector/width:500000
/sys/kernel/tracing/hwlat_detector/window:1000000
/sys/kernel/tracing/hwlat_detector/mode:none [round-robin] per-cpu
# tracer: hwlat
#
# entries-in-buffer/entries-written: 30/30   #P:2
...
           <...>-383     [001] dn...  1106.401782: #1     inner/outer(us):  703/1702  ts:1792313350.520562352 count:1631
           <...>-383     [000] d....  1107.425285: #2     inner/outer(us): 5499/7059  ts:1792313351.544254304 count:307
           <...>-383     [001] d....  1108.450574: #3     inner/outer(us):  101/694   ts:1792313352.569479296 count:217
           <...>-383     [000] d....  1109.474081: #4     inner/outer(us): 1565/2516  ts:1792313353.594244992 count:384
           <...>-383     [001] d....  1110.497114: #5     inner/outer(us):   44/30    ts:1792313354.616247616 count:142
           <...>-383     [000] d....  1111.521709: #6     inner/outer(us):   66/35    ts:1792313355.640923200 count:180
...
           <...>-383     [000] d....  1117.667590: #12    inner/outer(us): 23802/2366  ts:1792313361.787478064 count:1287
...
           <...>-383     [000] d....  1121.761270: #16    inner/outer(us): 4310/1492  ts:1792313365.880248112 count:166
```

(30개 중 일부. 헤더 주석 줄 일부 생략.) 출력에서 볼 것:

- **30초에 30개 샘플** — 1초 창마다 빠짐없이 임계값(10 µs)을 넘었다. 실제 보드에서 정상이라면 이 트레이스는 거의 비어 있어야 한다.
- `[001]`, `[000]`이 번갈아 나온다 — round-robin 모드로 CPU를 옮겨 다닌다.
- 최대 23802 µs = **IRQ를 끈 CPU에서 24 ms가 사라졌다.** 게스트 OS는 그 순간 아무것도 하지 않았다. QEMU의 vCPU 스레드가 호스트에서 실행되지 못했거나(다른 작성자들의 QEMU·빌드와 경쟁), TCG가 번역·I/O 처리로 vCPU 실행을 멈춘 시간이다. `count:1631`은 0.5초 창 안에서 10 µs 초과 간격이 1631번 있었다는 뜻이다.
- 이 숫자는 7.5절에서 auto-analysis가 "idle 중 IRQ가 8.7 ms 늦었는데 OS 안에 범인 없음"이라고 한 것을 정확히 설명한다. 4.2절 cyclictest의 max(22 ms)와 크기도 같다. **즉 이 게스트에서 cyclictest max를 결정한 것은 커널이 아니라 에뮬레이터·호스트다.**

### 8.3 예제 13 — `rtla hwnoise`

```sh
rtla hwnoise -d 20 -q
```

```text
                                          Hardware-related Noise                                   
duration:   0 00:00:20 | time is in us
CPU Period       Runtime        Noise  % CPU Aval   Max Noise   Max Single          HW          NMI
  0 #19         14250011      4707156    66.96735      336020        23625     2919368            0 
  1 #20         15000003      4783160    68.11227      307692        21361     2964471            0 
```

출력에서 볼 것: 열은 주기 수(`#19`), 실행 시간 합, 노이즈 합, CPU 가용률, 한 주기 최대 노이즈, 단일 최대 노이즈, HW 노이즈 횟수, NMI 횟수다. IRQ를 끈 상태에서 **실행 시간의 약 33%가 사라졌다**(가용률 67~68%). 단일 최대 23.6 ms는 hwlat의 23.8 ms와 일치한다. 실행 시간이 14.25초인 것은 hwnoise가 기본으로 주기의 75%만 IRQ off로 돌기 때문으로 보인다(19주기 × 0.75 s ≈ 14.25 s). 실제 arm64 보드라면 가용률은 99.99% 이상이어야 하고, 그렇지 않으면 TF-A(EL3) 쪽 호출 빈도·길이를 펌웨어 팀과 확인한다.

> hwlat/hwnoise는 **측정하는 동안 CPU를 IRQ off로 오래 붙잡는다.** 제품이 돌고 있는 시스템에서 켜면 그 자체가 지연원이 된다. 양산 전 검증이나 격리된 CPU에서만 쓴다.

---

## 9. 스파이크의 원인 추적 — 넓게 재고, 좁혀서 잡는다

```svg
<svg viewBox="0 0 660 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14">스파이크 원인 추적 순서 — 넓게 재고, 좁혀서 잡는다</text>
<rect x="20" y="40" width="300" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="60" font-size="13">1. cyclictest -h 부하+장시간</text>
<text x="30" y="80" font-size="12">분포·p99.9·max 확보 — "문제가 있다"</text>
<rect x="20" y="110" width="300" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="130" font-size="13">2. rtla timerlat top/hist</text>
<text x="30" y="150" font-size="12">IRQ 단계냐 thread 단계냐 가른다</text>
<rect x="20" y="180" width="300" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="200" font-size="13">3. rtla timerlat top -a 임계값</text>
<text x="30" y="220" font-size="12">첫 스파이크에서 멈춤 + auto-analysis</text>
<rect x="20" y="250" width="300" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="270" font-size="13">4. 남은 의심 → 전용 트레이서</text>
<text x="30" y="290" font-size="12">osnoise -s, cyclictest -b, irqsoff(M5)</text>
<rect x="20" y="320" width="300" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="340" font-size="13">5. 고치고 1번을 같은 조건으로 재측정</text>
<text x="30" y="360" font-size="12">전/후 히스토그램을 한 그림에</text>
<line x1="170" y1="90" x2="170" y2="108" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="170" y1="160" x2="170" y2="178" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="170" y1="230" x2="170" y2="248" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="170" y1="300" x2="170" y2="318" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="350" y="110" width="290" height="120" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="362" y="132" font-size="13">auto-analysis가 말해 주는 것</text>
<text x="362" y="156" font-size="12">IRQ handler delay 큼 → IRQ off 구간</text>
<text x="362" y="176" font-size="12">(exit from idle) → idle 탈출 비용</text>
<text x="362" y="196" font-size="12">Blocking thread: 이름 + 스택</text>
<text x="362" y="216" font-size="12">IRQ/Softirq interference: 누가 끼었나</text>
<rect x="350" y="250" width="290" height="70" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="362" y="272" font-size="13">트레이스에 아무것도 없으면?</text>
<text x="362" y="292" font-size="12">OS 밖 → hwlat/hwnoise로 확인</text>
<text x="362" y="310" font-size="12">(SMI, EL3 펌웨어, 하이퍼바이저)</text>
</svg>
```

그림 7 — 지연 스파이크 원인 추적 워크플로. 1~2단계는 트레이싱을 끈 "측정", 3~4단계는 트레이싱을 켠 "분석"이다. 분석 단계의 숫자는 트레이싱 오버헤드가 섞이므로 보고용 숫자로 쓰지 않는다.

timerlat `--auto`(7.4절)가 대부분의 경우 3단계에서 끝내 준다. 하지만 "내 제품 스레드(cyclictest 같은 유저 RT 스레드)가 늦은 바로 그 순간"의 전체 문맥을 보고 싶을 때는 cyclictest `-b`와 ftrace를 같이 쓴다.

### 9.1 예제 14 — cyclictest `-b` + ftrace 이벤트로 스파이크 순간 잡기

무엇을 확인하나: sched·irq·timer 이벤트를 켠 상태에서 cyclictest를 CPU1에 하나 돌리고, 8 ms를 넘는 순간 트레이싱을 멈추게 한다. CPU1에는 n3_spike가 1초마다 10 ms IRQ off를 한다.

```sh
T=/sys/kernel/tracing
echo 0 > $T/tracing_on; echo > $T/trace; echo 8192 > $T/buffer_size_kb
for e in sched/sched_switch sched/sched_wakeup sched/sched_waking irq/irq_handler_entry \
         irq/irq_handler_exit timer/hrtimer_expire_entry timer/hrtimer_expire_exit; do
  echo 1 > $T/events/$e/enable; done
echo 1 > $T/tracing_on
insmod /root/n3/n3_spike-trace.ko cpu=1 us=10000 period_ms=1000 mode=0
cyclictest -m -p 95 -a 1 -t 1 -i 1000 -q -D 30 -b 8000 --tracemark; echo "rc=$?"
rmmod n3_spike; cat $T/trace > trace.txt
```

```text
INFO: debugfs mountpoint: /sys/kernel/debug/tracing/
[ 1163.551552][  T402] NOTICE: Automounting of tracing to debugfs is deprecated and will be removed in 2030
# /dev/cpu_dma_latency set to 0us
T: 0 (  403) P:95 I:1000 C:    955 Min:     79 Act: 9724 Avg:  337 Max:    9724
# Thread Ids: 00403
# Break thread: 403
# Break value: 9724
rc=0
[ 1164.639977][  T404] n3_spike: 2 spikes injected
```

출력에서 볼 것: 955번째 주기에서 9724 µs가 나와 멈췄다(`Break value`). n3_spike는 그동안 2번 실행됐다 — 두 번째 스파이크에서 잡혔다. cyclictest는 아직 옛 경로 `/sys/kernel/debug/tracing`을 쓰고, 그 때문에 커널이 "debugfs 아래 tracing 자동 마운트는 deprecated"라고 경고했다. 보드의 cyclictest가 오래된 버전이면 tracefs 마운트 위치를 확인할 것.

트레이스 끝부분(CPU1 위주로 발췌, 긴 줄은 잘랐다):

```text
<idle>-0       [001] d.s3.  1164.523660: sched_waking: comm=n3_spike/1 pid=401 prio=120 target_cpu=001
<idle>-0       [001] d..2.  1164.523753: sched_switch: prev_comm=swapper/1 ... ==> next_comm=n3_spike/1 next_pid=401
n3_spike/1-401 [001] d.h..  1164.523854: irq_handler_entry: irq=11 name=arch_timer
n3_spike/1-401 [001] d.h..  1164.523859: hrtimer_expire_entry: ... function=hrtimer_wakeup now=1164448344848
n3_spike/1-401 [001] dNh3.  1164.523870: sched_wakeup: comm=cyclictest pid=403 prio=4 target_cpu=001
n3_spike/1-401 [001] d..2.  1164.523908: sched_switch: prev_comm=n3_spike/1 ... ==> next_comm=cyclictest next_pid=403
cyclictest-403 [001] d..2.  1164.524005: sched_switch: prev_comm=cyclictest ... prev_state=S ==> next_comm=n3_spike/1
n3_spike/1-401 [001] d.h..  1164.534126: irq_handler_entry: irq=11 name=arch_timer
n3_spike/1-401 [001] d.h..  1164.534139: hrtimer_expire_entry: ... function=hrtimer_wakeup now=1164458621664
n3_spike/1-401 [001] dNh..  1164.534191: hrtimer_expire_entry: hrtimer=0000000067357890 function=hrtimer_wakeup ...
n3_spike/1-401 [001] dNh3.  1164.534201: sched_wakeup: comm=cyclictest pid=403 prio=4 target_cpu=001
n3_spike/1-401 [001] dNh..  1164.534206: hrtimer_expire_entry: ... function=tick_nohz_handler ...
n3_spike/1-401 [001] dNh..  1164.534370: irq_handler_exit: irq=11 ret=handled
n3_spike/1-401 [001] d..2.  1164.534505: sched_switch: prev_comm=n3_spike/1 ... ==> next_comm=cyclictest next_pid=403
cyclictest-403 [001] ...1.  1164.535030: tracing_mark_write: hit latency threshold (9724 > 8000)
```

읽는 법(타임라인으로):

1. 523.753 ms: softirq(`d.s3.`, 타이머 softirq)가 msleep에서 깬 `n3_spike/1`이 CPU1에 올라온다.
2. 523.854 ms: n3_spike가 아직 IRQ를 끄기 전, 타이머 IRQ가 들어와 cyclictest(prio 4로 보이는 것은 커널 내부 prio 값 = 99−95)를 깨우고, cyclictest가 한 주기를 처리한 뒤 524.005 ms에 다시 잔다(`prev_state=S`). CPU는 n3_spike에게 돌아간다.
3. 524.005 ms ~ 534.126 ms: **CPU1에 이벤트가 하나도 없다.** 그 사이 cyclictest의 다음 만료(약 524.9 ms)가 있었는데 IRQ가 안 들어왔다. n3_spike가 `local_irq_save()` 후 10 ms `udelay`를 도는 구간이다. IRQ off 구간 안에서는 어떤 이벤트도 발생할 수 없으므로 "빈 구간" 자체가 증거다.
4. 534.126 ms: IRQ가 풀리자마자 밀려 있던 타이머 IRQ가 n3_spike 문맥에서 처리된다. 한 번의 IRQ에서 **hrtimer 세 개**(cyclictest 메인 스레드 pid 402, 측정 스레드 403, tick)가 한꺼번에 만료된다 — 늦은 IRQ는 쌓였던 일을 한꺼번에 처리하므로 IRQ 처리 자체도 길어진다(534.126 → 534.370, 약 240 µs).
5. 534.505 ms: cyclictest로 전환, 535.030 ms에 임계값 초과를 확인하고 trace marker를 쓴 뒤 트레이싱을 멈췄다.

이벤트 트레이스는 "IRQ off 구간 안"을 보여 주지 못한다. **누가** IRQ를 껐는지 함수 단위로 보려면 M5의 `irqsoff` tracer(Image-trace에 있음, Image-rt에는 없음)나 timerlat `-a`의 IRQ 스택(7.4절, `n3_burn`이 찍힘)을 쓴다. 9.1의 방식이 강한 곳은 "스파이크 전후 수 ms 동안 시스템 전체에서 무슨 일이 있었나"라는 문맥이다.

### 9.2 osnoise로 잡기 — `-s`(단일 노이즈 임계값) + `-t`(트레이스 저장)

워크로드가 주기적 스레드가 아니라 "계속 도는 바쁜 루프"(폴링 드라이버, DPDK, 추론 루프)라면 같은 일을 osnoise로 한다. `rtla osnoise top -s 5000 -T 50 -t`는 단일 노이즈가 5000 µs를 넘으면 멈추고 트레이스를 `osnoise_trace.txt`로 저장한다. 실행 결과는 6.4절에 있다.

---

## 10. 커널·설정을 공정하게 비교하기

### 10.1 규칙

| 같게 맞출 것 | 이유 |
|---|---|
| 하드웨어(가능하면 같은 개체), 펌웨어(TF-A, U-Boot) 버전 | 개체 차이·펌웨어 SMC 경로가 OS 밖 소음을 바꾼다 |
| 부하 스크립트·시작 순서 | 부하가 다르면 비교가 아니다 |
| 측정 시간·주기·우선순위·CPU 배치 | max는 시간에 따라 자란다(5절) |
| 부트 인자(`isolcpus`, `nohz_full`, `irqaffinity`, cpuidle), cpufreq governor/고정 주파수 | 튜닝 차이를 커널 차이로 착각하지 않게 |
| 온도·전원(배터리/어댑터) | thermal throttling |
| 도구 버전(rt-tests, rtla) | 측정 방식·기본값이 바뀐다 |

그리고 **한 번씩이 아니라 번갈아 여러 번**(A-B-A-B) 돌린다. 측정 환경 자체가 얼마나 흔들리는지를 같이 재야 차이가 "진짜"인지 알 수 있다.

### 10.2 예제 15 — Image-trace vs Image-rt, 그리고 같은 커널의 재측정

호스트가 비교적 조용한 같은 시간대(load average 2~4)에 Image-trace → Image-rt → Image-trace 순으로, 같은 스크립트(4.1절 명령, 60초, 두 CPU 합산)로 돌렸다. 아래 숫자는 모두 4.2의 스크립트 출력에서 `all` 줄만 모은 것이다(단위 µs).

| 실행 | 상태 | n | p50 | p99 | p99.9 | p99.99 | max | over |
|---|---|---|---|---|---|---|---|---|
| ① trace (01:31) | idle | 118834 | 270 | 581 | 3559 | 14940 | 22509 | 5 |
| ① trace | loaded | 116018 | 471 | 820 | 5414 | >20000 | 26670 | 13 |
| ② rt (01:56) | idle | 117640 | 262 | 804 | 4735 | 19759 | 24134 | 11 |
| ② rt | loaded | 117509 | 342 | 759 | 5329 | >20000 | 33879 | 15 |
| ③ trace 재측정 (02:26) | idle | 118003 | 478 | 966 | 3233 | 13936 | 27166 | 3 |
| ③ trace 재측정 | loaded | 117720 | 297 | 490 | 3630 | 15288 | 28885 | 5 |

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="14">cyclictest 60 s — Image-trace vs Image-rt (실시간 TCG)</text>
<line x1="60" y1="280" x2="590" y2="280" stroke="currentColor"/>
<line x1="60" y1="56" x2="60" y2="280" stroke="currentColor"/>
<line x1="60.0" y1="280" x2="60.0" y2="284" stroke="currentColor"/>
<text x="60.0" y="298" font-size="12" text-anchor="middle">10us</text>
<line x1="192.5" y1="280" x2="192.5" y2="284" stroke="currentColor"/>
<text x="192.5" y="298" font-size="12" text-anchor="middle">100us</text>
<line x1="325.0" y1="280" x2="325.0" y2="284" stroke="currentColor"/>
<text x="325.0" y="298" font-size="12" text-anchor="middle">1ms</text>
<line x1="457.5" y1="280" x2="457.5" y2="284" stroke="currentColor"/>
<text x="457.5" y="298" font-size="12" text-anchor="middle">10ms</text>
<line x1="590.0" y1="280" x2="590.0" y2="284" stroke="currentColor"/>
<text x="590.0" y="298" font-size="12" text-anchor="middle">100ms</text>
<line x1="56" y1="262.8" x2="60" y2="262.8" stroke="currentColor"/>
<text x="52" y="266.8" font-size="12" text-anchor="end">1e0</text>
<line x1="56" y1="193.8" x2="60" y2="193.8" stroke="currentColor"/>
<text x="52" y="197.8" font-size="12" text-anchor="end">1e2</text>
<line x1="56" y1="124.9" x2="60" y2="124.9" stroke="currentColor"/>
<text x="52" y="128.9" font-size="12" text-anchor="end">1e4</text>
<line x1="56" y1="56.0" x2="60" y2="56.0" stroke="currentColor"/>
<text x="52" y="60.0" font-size="12" text-anchor="end">1e6</text>
<text x="325.0" y="316" font-size="12" text-anchor="middle">latency (log)</text>
<text x="14" y="44" font-size="12">count (log)</text>
<polyline points="60.0,280.0 73.3,280.0 73.3,280.0 86.5,280.0 86.5,280.0 99.8,280.0 99.8,280.0 113.0,280.0 113.0,280.0 126.2,280.0 126.2,280.0 139.5,280.0 139.5,215.8 152.8,215.8 152.8,209.1 166.0,209.1 166.0,191.4 179.2,191.4 179.2,172.9 192.5,172.9 192.5,144.8 205.8,144.8 205.8,135.0 219.0,135.0 219.0,104.1 232.2,104.1 232.2,125.8 245.5,125.8 245.5,132.5 258.8,132.5 258.8,103.5 272.0,103.5 272.0,123.3 285.2,123.3 285.2,159.9 298.5,159.9 298.5,169.1 311.8,169.1 311.8,188.1 325.0,188.1 325.0,206.5 338.2,206.5 338.2,214.6 351.5,214.6 351.5,221.3 364.8,221.3 364.8,217.9 378.0,217.9 378.0,214.6 391.2,214.6 391.2,213.4 404.5,213.4 404.5,210.0 417.8,210.0 417.8,217.9 431.0,217.9 431.0,233.6 444.2,233.6 444.2,238.7 457.5,238.7 457.5,221.3 470.7,221.3 470.7,223.3 484.0,223.3 484.0,242.0 497.2,242.0 497.2,280.0 510.5,280.0 510.5,280.0 523.8,280.0 523.8,280.0 537.0,280.0 537.0,280.0 550.2,280.0 550.2,280.0 563.5,280.0 563.5,280.0 576.8,280.0 576.8,280.0 590.0,280.0" fill="none" stroke="#4a7bd0" stroke-width="1.6"/>
<rect x="602" y="238.7" width="10" height="41.3" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<line x1="504.2" y1="56" x2="504.2" y2="280" stroke="#4a7bd0" stroke-dasharray="3 3"/>
<line x1="340" y1="58.0" x2="360" y2="58.0" stroke="#4a7bd0" stroke-width="2"/>
<text x="366" y="62.0" font-size="12">trace idle (max 22.5ms)</text>
<polyline points="60.0,280.0 73.3,280.0 73.3,280.0 86.5,280.0 86.5,280.0 99.8,280.0 99.8,280.0 113.0,280.0 113.0,280.0 126.2,280.0 126.2,280.0 139.5,280.0 139.5,280.0 152.8,280.0 152.8,262.8 166.0,262.8 166.0,252.4 179.2,252.4 179.2,207.2 192.5,207.2 192.5,197.4 205.8,197.4 205.8,180.2 219.0,180.2 219.0,170.6 232.2,170.6 232.2,153.3 245.5,153.3 245.5,133.2 258.8,133.2 258.8,115.3 272.0,115.3 272.0,103.1 285.2,103.1 285.2,108.6 298.5,108.6 298.5,119.3 311.8,119.3 311.8,159.9 325.0,159.9 325.0,192.0 338.2,192.0 338.2,199.2 351.5,199.2 351.5,204.2 364.8,204.2 364.8,213.4 378.0,213.4 378.0,205.8 391.2,205.8 391.2,217.9 404.5,217.9 404.5,204.5 417.8,204.5 417.8,213.4 431.0,213.4 431.0,217.2 444.2,217.2 444.2,219.5 457.5,219.5 457.5,219.5 470.7,219.5 470.7,217.2 484.0,217.2 484.0,225.6 497.2,225.6 497.2,280.0 510.5,280.0 510.5,280.0 523.8,280.0 523.8,280.0 537.0,280.0 537.0,280.0 550.2,280.0 550.2,280.0 563.5,280.0 563.5,280.0 576.8,280.0 576.8,280.0 590.0,280.0" fill="none" stroke="#e08a3c" stroke-width="1.6"/>
<rect x="616" y="224.4" width="10" height="55.6" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<line x1="513.9" y1="56" x2="513.9" y2="280" stroke="#e08a3c" stroke-dasharray="3 3"/>
<line x1="340" y1="76.0" x2="360" y2="76.0" stroke="#e08a3c" stroke-width="2"/>
<text x="366" y="80.0" font-size="12">trace loaded (max 26.7ms)</text>
<polyline points="60.0,280.0 73.3,280.0 73.3,280.0 86.5,280.0 86.5,280.0 99.8,280.0 99.8,280.0 113.0,280.0 113.0,280.0 126.2,280.0 126.2,206.8 139.5,206.8 139.5,190.5 152.8,190.5 152.8,182.2 166.0,182.2 166.0,170.5 179.2,170.5 179.2,160.5 192.5,160.5 192.5,150.9 205.8,150.9 205.8,129.5 219.0,129.5 219.0,118.7 232.2,118.7 232.2,110.5 245.5,110.5 245.5,103.6 258.8,103.6 258.8,115.9 272.0,115.9 272.0,143.4 285.2,143.4 285.2,162.9 298.5,162.9 298.5,168.0 311.8,168.0 311.8,175.8 325.0,175.8 325.0,184.9 338.2,184.9 338.2,190.3 351.5,190.3 351.5,192.2 364.8,192.2 364.8,192.0 378.0,192.0 378.0,190.2 391.2,190.2 391.2,206.8 404.5,206.8 404.5,205.1 417.8,205.1 417.8,210.4 431.0,210.4 431.0,221.3 444.2,221.3 444.2,228.3 457.5,228.3 457.5,221.3 470.7,221.3 470.7,222.2 484.0,222.2 484.0,229.9 497.2,229.9 497.2,280.0 510.5,280.0 510.5,280.0 523.8,280.0 523.8,280.0 537.0,280.0 537.0,280.0 550.2,280.0 550.2,280.0 563.5,280.0 563.5,280.0 576.8,280.0 576.8,280.0 590.0,280.0" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<rect x="630" y="226.9" width="10" height="53.1" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<line x1="508.2" y1="56" x2="508.2" y2="280" stroke="#3f9a6b" stroke-dasharray="3 3"/>
<line x1="340" y1="94.0" x2="360" y2="94.0" stroke="#3f9a6b" stroke-width="2"/>
<text x="366" y="98.0" font-size="12">rt idle (max 24.1ms)</text>
<polyline points="60.0,280.0 73.3,280.0 73.3,280.0 86.5,280.0 86.5,280.0 99.8,280.0 99.8,280.0 113.0,280.0 113.0,280.0 126.2,280.0 126.2,280.0 139.5,280.0 139.5,280.0 152.8,280.0 152.8,246.3 166.0,246.3 166.0,231.6 179.2,231.6 179.2,186.4 192.5,186.4 192.5,158.3 205.8,158.3 205.8,142.2 219.0,142.2 219.0,143.3 232.2,143.3 232.2,121.2 245.5,121.2 245.5,109.5 258.8,109.5 258.8,109.3 272.0,109.3 272.0,120.8 285.2,120.8 285.2,112.6 298.5,112.6 298.5,138.9 311.8,138.9 311.8,175.6 325.0,175.6 325.0,188.8 338.2,188.8 338.2,188.6 351.5,188.6 351.5,194.8 364.8,194.8 364.8,199.6 378.0,199.6 378.0,209.1 391.2,209.1 391.2,208.7 404.5,208.7 404.5,203.6 417.8,203.6 417.8,207.9 431.0,207.9 431.0,217.9 444.2,217.9 444.2,220.4 457.5,220.4 457.5,238.7 470.7,238.7 470.7,222.2 484.0,222.2 484.0,221.3 497.2,221.3 497.2,280.0 510.5,280.0 510.5,280.0 523.8,280.0 523.8,280.0 537.0,280.0 537.0,280.0 550.2,280.0 550.2,280.0 563.5,280.0 563.5,280.0 576.8,280.0 576.8,280.0 590.0,280.0" fill="none" stroke="#d0564a" stroke-width="1.6"/>
<rect x="644" y="222.2" width="10" height="57.8" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<line x1="527.7" y1="56" x2="527.7" y2="280" stroke="#d0564a" stroke-dasharray="3 3"/>
<line x1="340" y1="112.0" x2="360" y2="112.0" stroke="#d0564a" stroke-width="2"/>
<text x="366" y="116.0" font-size="12">rt loaded (max 33.9ms)</text>
<text x="598" y="316" font-size="12">&gt;20ms</text>
</svg>
```

그림 8 — ①(trace)과 ②(rt)의 히스토그램을 같은 축에 겹쳐 그렸다. 몸통의 위치는 조금씩 다르지만, 1 ms 이상의 꼬리와 20 ms 근처 max는 네 곡선이 거의 같다.

출력에서 볼 것:

- ②의 rt loaded p50(342)이 ①의 trace loaded p50(471)보다 낮다. 여기서 멈추면 "RT가 부하 아래 몸통을 줄였다"고 말하고 싶어진다.
- 그런데 **③ 같은 trace 커널을 다시 재니 loaded p50이 297**로, rt보다 더 낮게 나왔다. idle p50은 반대로 270 → 478로 올랐다. 같은 커널, 같은 스크립트인데 실행 간 차이가 커널 간 차이보다 크다.
- max는 22~34 ms로 모두 비슷하다 — 8절에서 본 호스트의 vCPU 정지(최대 약 24 ms)가 상한을 정한다. PREEMPT_RT는 OS 안의 비선점 구간을 줄이는 기술이지, OS 밖 정지를 줄이지 못한다.
- **결론: 이 환경에서는 두 커널의 차이를 주장할 수 없다.** 이것이 공정 비교의 핵심이다 — 측정 환경의 흔들림(③ − ①)을 먼저 재고, 커널 차이(② − ①)가 그보다 확실히 클 때만 차이를 말한다. 실제 보드에서라면 PREEMPT_RT의 효과(max가 ms → 수십 µs)가 이 흔들림보다 훨씬 커서 분명하게 보인다(N1).

---

## 11. 설계 리뷰용 보고 — 한 장으로 말하기

### 11.1 무엇을 반드시 넣나

리뷰어가 숫자를 믿으려면 "재현 가능한 조건"과 "분포"가 있어야 한다. Don이 SSD QoS 리포트에 preconditioning 상태, QD, 측정 시간을 적었던 것과 같다.

1. 목적과 요구사항: "1 kHz 제어 루프, 스레드 깨어남 지연 p99.99 ≤ 100 µs, max ≤ 200 µs"
2. 조건: HW·펌웨어·커널(config 해시)·부트 인자·도구 버전·부하·시간·온도
3. 결과 표: p50/p99/p99.9/p99.99/max/overflow/n, CPU별
4. 히스토그램: 로그 y축, 비교 대상은 같은 축에 겹쳐서
5. 최악값 분석: timerlat auto-analysis 요약(IRQ인가 thread인가, 범인 함수)
6. 한계: 측정 시간, 측정 방식(자체 측정인지 GPIO+scope인지), 알려진 소음
7. 결론과 다음 행동

### 11.2 템플릿 (이 노트의 실험으로 채운 예)

```text
Latency Report — N3 lab, 2026-10-18
Requirement : (lab exercise) periodic wakeup latency of a SCHED_FIFO 95 thread, 1 kHz
Hardware    : QEMU 'virt' TCG (cortex-a57 model), -smp 2, -m 1G, inside Lima VM on Apple M2
              host load average 2-4 during runs (shared with 16 other jobs)  <-- dominant noise
Kernels     : 6.18.55-trace (PREEMPT, HZ=250)  vs  6.18.55-rt (PREEMPT_RT, HZ=1000)
Boot args   : none for isolation (no isolcpus/nohz_full), init=/root/n3/init.sh (no systemd)
Tools       : cyclictest 2.50 (-m -S -p 95 -i 1000 -h 20000 -q -D 60), rtla 6.18.55
Load        : stress-ng --cpu 2 --vm 1 --io 1 --hdd 1 + hackbench -g 2 -P
Duration    : 60 s per run (too short for release; see max-vs-duration test)
Results (us, both CPUs merged):
  run              p50   p99  p99.9  p99.99    max   n
  trace loaded #1  471   820   5414  >20000  26670  116018
  rt    loaded     342   759   5329  >20000  33879  117509
  trace loaded #2  297   490   3630   15288  28885  117720
Worst case  : hwlat/hwnoise show 20-24 ms gaps with IRQs off -> outside the guest OS
Conclusion  : run-to-run variance > kernel difference; no claim possible on this platform.
Next        : repeat on target HW (i.MX8/AM62) for 24 h, add GPIO+scope cross-check (M11)
```

영문으로 쓴 이유: 실제 리뷰 문서는 대부분 영어다. 이 템플릿을 그대로 채우면 면접에서 "어떻게 측정하고 보고했나"에 대한 답이 된다.

---

## 12. 커널 안에서는 무슨 일이 일어나나

### 12.1 cyclictest의 한 샘플이 지나가는 길

```text
user  : clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next)
kernel: __arm64_sys_clock_nanosleep -> common_nsleep -> hrtimer_nanosleep
        -> do_nanosleep: hrtimer_sleeper 시작, set_current_state(TASK_INTERRUPTIBLE), schedule()
  ...   (CPU runs something else, or idles)
HW    : CNTV 비교값 도달 -> arch timer PPI(IRQ 11 on QEMU virt) -> GIC
kernel: el1_interrupt / el0_interrupt -> gic_handle_irq -> arch_timer_handler_virt
        -> hrtimer_interrupt -> __hrtimer_run_queues -> hrtimer_wakeup -> wake_up_process
        -> (RT 태스크가 더 높으면) need_resched -> IRQ 출구에서 preempt_schedule_irq / schedule
        -> __schedule -> context_switch -> cpu_switch_to
user  : clock_nanosleep 리턴 -> clock_gettime(vDSO, syscall 없이 CNTVCT 읽기) -> now - next
```

같은 경로가 timerlat의 auto-analysis 스택 트레이스(7.4절)에 그대로 찍혀 나온다: `arch_timer_handler_virt → hrtimer_interrupt → __hrtimer_run_queues → timerlat_irq`. F6(인터럽트 처리), F8(hrtimer), F1(스케줄러 §7 preemption)의 내용이 한 줄로 이어지는 지점이다. 드라이버를 쓸 때는 "내 ISR이 오래 IRQ를 끄면 ①이 늘고, 내 `spin_lock`·`preempt_disable` 구간이 길면 ③이 는다"로 다시 만난다.

### 12.2 osnoise / timerlat tracer — `kernel/trace/trace_osnoise.c`

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14">rtla timerlat 한 주기 — 유저 도구, tracefs, 커널 tracer</text>
<rect x="20" y="40" width="200" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="62" font-size="13">rtla (유저)</text>
<text x="30" y="82" font-size="12">설정 쓰기 + 결과 읽기</text>
<rect x="250" y="40" width="390" height="60" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="260" y="62" font-size="13">/sys/kernel/tracing/osnoise/</text>
<text x="260" y="82" font-size="12">timerlat_period_us, stop_tracing_us, print_stack, cpus</text>
<rect x="20" y="140" width="200" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="162" font-size="13">timerlat_irq()</text>
<text x="30" y="182" font-size="12">hrtimer 콜백, IRQ 문맥</text>
<rect x="250" y="140" width="190" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="260" y="162" font-size="13">측정 스레드</text>
<text x="260" y="182" font-size="12">timerlatu/N (유저)</text>
<rect x="470" y="140" width="170" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="480" y="162" font-size="13">ring buffer</text>
<text x="480" y="182" font-size="12">timerlat_sample</text>
<line x1="220" y1="70" x2="246" y2="70" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="220" y1="170" x2="246" y2="170" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="120" y1="200" x2="120" y2="240" stroke="currentColor"/>
<line x1="120" y1="240" x2="550" y2="240" stroke="currentColor"/>
<line x1="550" y1="240" x2="550" y2="204" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="345" y1="200" x2="345" y2="240" stroke="currentColor"/>
<text x="130" y="258" font-size="12">context=0 (IRQ)</text>
<text x="355" y="258" font-size="12">context=1, 2 (thread, ret user)</text>
<line x1="550" y1="140" x2="550" y2="110" stroke="currentColor"/>
<line x1="550" y1="110" x2="120" y2="110" stroke="currentColor"/>
<line x1="120" y1="110" x2="120" y2="104" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="300" y="126" font-size="12">rtla가 읽어 통계·hist·auto-analysis</text>
<text x="20" y="290" font-size="12">측정 스레드는 timerlat_fd를 read()하며 잠들고, timerlat_irq가 깨운다 (-k면 커널 스레드)</text>
<text x="20" y="310" font-size="12">stop_tracing_us를 넘으면 커널이 트레이싱을 멈추고, 그 순간의 버퍼가 분석 대상이 된다</text>
</svg>
```

그림 9 — rtla timerlat의 구성. rtla는 tracefs 설정 파일을 쓰고, 커널의 hrtimer 콜백(`timerlat_irq`)이 IRQ latency를 기록한 뒤 측정 스레드를 깨우며, 스레드가 thread·ret-user latency를 기록한다. 결과는 링 버퍼를 거쳐 rtla가 읽는다.

두 tracer는 한 파일에 산다(6.18.55 기준).

| 함수 | 역할 |
|---|---|
| `osnoise_main()` → `run_osnoise()` | CPU마다 kthread. `runtime` 동안 `time_get()`(= `trace_clock_local()`)을 연속으로 읽으며 두 읽기 사이 간격이 `tracing_thresh`(기본 1 µs)를 넘으면 noise로 친다 |
| `osnoise_trace_irq_entry()` 등 tracepoint 콜백 | IRQ·softirq·NMI·thread 진입/퇴장 시각을 기록해 noise를 원인별로 분류(`irq_noise`, `softirq_noise`, `nmi_noise`, `thread_noise` 이벤트) |
| `run_osnoise()`의 `hw_count` | 간격이 벌어졌는데 그 사이 IRQ·NMI 카운트가 하나도 늘지 않았다 → "HW noise" (OS가 본 적 없는 정지) |
| `timerlat_irq()` | hrtimer 콜백. `now - abs_period`를 IRQ latency로 기록(`context=0`)하고 측정 스레드를 깨운다 |
| `timerlat_main()` | 깨어난 스레드가 다시 시각을 읽어 thread latency(`context=1`) 기록, 다음 주기 hrtimer를 건다 |
| 사용자 스레드 모드(`-u`, rtla 6.x 기본) | rtla가 만든 유저 스레드가 `/sys/kernel/tracing/osnoise/per_cpu/cpuN/timerlat_fd`를 read하며 잔다. 유저로 돌아온 시각(`context=2`, "Ret user")까지 잰다 |

기본값: osnoise `period = runtime = 1 s`(`DEFAULT_SAMPLE_PERIOD/RUNTIME`), timerlat `period = 1000 µs`(`DEFAULT_TIMERLAT_PERIOD`). 설정 파일은 `/sys/kernel/tracing/osnoise/`(`period_us`, `runtime_us`, `stop_tracing_us`, `stop_tracing_total_us`, `timerlat_period_us`, `print_stack`, `cpus`, `options`)에 있다. rtla는 이 파일들을 써 주는 "자동 설정 + 통계 + 분석기"다 — tracefs를 손으로 만질 줄 알면 rtla 없는 보드에서도 같은 측정을 할 수 있다.

### 12.3 hwlat — `kernel/trace/trace_hwlat.c`

`kthread_fn()`이 `window`(기본 1 s)마다 한 번, `local_irq_disable()` 후 `get_sample()`을 `width`(기본 0.5 s) 동안 돈다. `get_sample()`은 `trace_clock_local()`을 연속으로 두 번 읽어 그 차이(inner)와 루프 사이 차이(outer)를 보고, `tracing_thresh`(hwlat에서는 기본 10 µs)를 넘으면 샘플로 남긴다. IRQ를 껐으니 OS가 끼어들 수 없다. 그런데도 시간이 사라졌다면 **OS 밖**이다: x86에서는 SMI(System Management Interrupt), arm64에서는 EL3 secure monitor(TF-A의 SMC 처리, RAS 펌웨어 처리)나 EL2 하이퍼바이저, 그리고 가상 머신이라면 호스트 스케줄러. NMI는 OS가 볼 수 있으므로 `nmi-total`/`nmi-count`로 따로 표시해 준다(arm64에서는 pseudo-NMI를 켠 경우만 의미가 있다).

### 12.4 cyclictest `-b`가 하는 일

rt-tests 2.x의 `-b USEC`는 지연이 USEC를 넘는 순간 `/sys/kernel/tracing/tracing_on`에 0을 쓰고(`--tracemark`면 그 직전 `trace_marker`에 "hit latency threshold" 문자열을 쓴다) 측정을 끝낸다. ftrace의 링 버퍼는 덮어쓰기 모드라, 멈춘 순간 버퍼에는 "스파이크 직전 수 ms~수 초"가 남는다. bare-metal에서 trace buffer를 순환 기록하다가 assert에서 트리거를 걸어 멈추는 것과 똑같다.

---

## 13. Embedded 실무에서 다시 보기

### 13.1 장면 1 — "벤더가 max 20 µs라고 합니다"

i.MX8M Plus BSP를 받았는데 벤더 문서에 "PREEMPT_RT, cyclictest max 20 µs"라고 적혀 있다. 확인 순서:

1. 벤더에게 **측정 조건 전체**를 받는다: 커맨드라인, 측정 시간, 부하, 커널 config, 부트 인자(`isolcpus`, `nohz_full`, cpuidle 설정), 보드 리비전, CPU 주파수 고정 여부.
2. 같은 조건으로 재현한다. 재현이 안 되면 거기서 멈추고 차이를 찾는다.
3. **우리 조건**으로 다시 잰다: 제품 부하(카메라 스트리밍 + 추론 + 로깅 + 네트워크), 제품 cpuidle 설정(cyclictest가 `/dev/cpu_dma_latency`=0을 걸어 idle을 막는 효과를 빼려면 `--default-system` 또는 제품과 같은 PM QoS), 최소 수 시간~24시간.
4. 최악값이 나오면 timerlat `--auto`로 원인을 잡는다. 벤더 숫자와 우리 숫자의 차이가 "어느 드라이버/기능을 켰기 때문인지"까지 말할 수 있으면 협상이 된다.

### 13.2 장면 2 — 1 kHz 모터 제어 루프의 회귀 테스트(CI)

커널을 올릴 때마다 지연이 나빠지지 않았는지 자동으로 본다. `rtla timerlat`의 종료 코드(0 = 통과, 2 = 임계값 초과)가 그대로 CI 판정이 된다.

```sh
# CI 단계 예시 (가이드 — 이 노트의 랩에서 CI로 돌리지는 않았다)
rtla timerlat hist -c 2-3 -d 1h -T 100 --no-aa -b 1 -E 200 > tl_$(uname -r).hist
rc=$?
[ $rc -eq 2 ] && { echo "FAIL: thread latency > 100 us"; exit 1; }
cyclictest -m -a 2-3 -t 2 -p 95 -i 1000 -h 400 -q -D 1h --json=ct_$(uname -r).json
```

결과 JSON·히스토그램은 아티팩트로 남기고 4.2의 스크립트로 이전 빌드와 p99.9·max를 비교한다.

### 13.3 장면 3 — 센서 키트에서 "가끔 IMU 샘플이 늦다"

1. 유저 앱의 타임스탬프로 증상을 수치화한다(늦은 샘플의 빈도·크기).
2. 같은 CPU에서 `rtla timerlat top -c <cpu> -a <임계값>`. IRQ 단계면 IRQ off 구간(드라이버 `spin_lock_irqsave`), thread 단계면 우선순위·preempt off.
3. auto-analysis가 범인을 못 가리키면(7.5절처럼 idle에서 늦음) `rtla hwnoise`로 OS 밖 정지를 확인한다. arm64 SoC에서 흔한 OS 밖 원인: TF-A(EL3)에서 처리하는 PSCI/SCMI 호출, 보안 모니터, DDR 주파수 전환(DVFS), RAS 에러 처리 펌웨어.
4. 최종 확인은 M11처럼 GPIO + scope로 "IMU DRDY 핀 → 앱 처리 완료 GPIO"를 직접 잰다. 소프트웨어 자체 측정은 자기 자신이 멈춘 시간을 못 잴 수 있기 때문이다(아래 13.4).

### 13.4 장면 4 — 소프트웨어 측정 vs 하드웨어 측정

| 항목 | cyclictest/timerlat (자체 측정) | GPIO + scope/LA (M11) |
|---|---|---|
| 기준 시계 | 같은 CPU의 시계(CNTVCT) | 외부 장비의 독립 시계 |
| 재는 구간 | 타이머 만료 → 스레드 실행 | 실제 외부 이벤트(핀) → 실제 응답(핀) |
| 놓치는 것 | 타이머 하드웨어 자체의 지연, 외부 IRQ 경로(GPIO 컨트롤러·GIC 라우팅), 측정 코드도 같이 멈춘 경우 | 내부 원인(어느 함수인지) |
| 장점 | 원인 분석(트레이스·스택)과 바로 연결, 대량 샘플, CI 자동화 | 고객이 보는 진짜 숫자, 측정이 측정 대상에 영향 거의 없음 |
| 결론 | 원인 찾기와 회귀 테스트용 | 스펙 검증·최종 보고용 |

둘은 경쟁 관계가 아니다. SSD 시절 "펌웨어 내부 타임스탬프 로그"와 "PCIe 분석기 트레이스"를 같이 쓴 것과 같다. 시간 축 맞추기는 M11에서 다룬다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| idle 시스템에서 10초 측정 | "max 15 µs!" → 현장에서 수 ms 지연 | 드문 이벤트를 만날 확률이 0에 가깝다 | 제품 부하 + 수 시간 이상, max와 p99.99 함께 보고 |
| 평균만 보고 | "평균 8 µs라 충분" | 제어 루프는 최악값에 깨진다 | p99/p99.9/p99.99/max + 히스토그램 |
| `-m` 생략 | 측정 시작 직후나 메모리 압박 때 수백 µs 튐 | 측정 스레드의 page fault | `-m` (제품 앱도 `mlockall`, A5·N2) |
| 우선순위 너무 낮게/정책 없이 | SCHED_OTHER 수준의 ms 지연 | `-p` 생략 시 기본 0 = SCHED_OTHER | `-p 95` 이상, 제품 RT 스레드와 같은 우선순위로 |
| `/dev/cpu_dma_latency` 효과를 모름 | 측정은 좋은데 제품은 나쁨 | cyclictest가 idle을 막아 idle 탈출 지연이 숨겨짐 | 제품과 같은 PM QoS로 측정, 또는 `--default-system`으로 비교 |
| 히스토그램 범위가 작음 | Overflows가 0이 아닌데 그림에 안 보임 | `-h 400` 밖 샘플 | 오버플로 개수·max를 따로 보고, `-h`를 키움 |
| 측정 도구 자체의 부하를 무시 | `rtla ... -t`/function tracer를 켜면 지연이 커짐 | 트레이싱 오버헤드 | 측정 단계(트레이싱 끔)와 원인 분석 단계(트레이싱 켬)를 분리 |
| VM·에뮬레이터 숫자를 RT 성능으로 보고 | 수 ms 지연, 재현 안 됨 | 호스트 스케줄링이 게스트 시간을 훔친다 | hwlat/hwnoise로 OS 밖 소음 확인, 실제 보드에서 측정 |
| 비교 실험의 조건 불일치 | "새 커널이 2배 좋아짐" → 재현 안 됨 | 부하·시간·보드 온도·주파수가 달랐다 | 같은 HW·부하·시간·config, 번갈아 여러 번 |
| 샘플 수 확인 안 함 | 60초 측정인데 샘플이 기대값보다 훨씬 적음 | 측정 스레드가 주기를 놓침(이 노트 4.2의 바쁜 호스트) | `n`을 기대값(시간 × 주파수 × 스레드)과 비교 |

---

## 15. 면접에서 이렇게 말한다

**Q.** Your vendor claims 20 µs max latency on this SoC. How do you verify it?

**A.** 먼저 측정 조건 전체(cyclictest 커맨드라인, 시간, 부하, 커널 config, 부트 인자, cpuidle·주파수 설정)를 받아 그대로 재현한다. 재현되면 우리 조건 — 제품 부하, 제품 PM 설정, 최소 수 시간~24시간 — 으로 다시 잰다. 결과는 max 하나가 아니라 p99/p99.9/p99.99/max와 로그 스케일 히스토그램으로 보고한다. 최악값은 timerlat `--auto`로 원인을 확인하고, 최종 수치는 GPIO와 scope로 외부에서 한 번 교차 검증한다.

> First I'd ask for the exact conditions: cyclictest command line, duration, load, kernel config and boot arguments, cpuidle and frequency settings. I'd reproduce their number under those conditions. Then I'd measure under our conditions — our real workload, our power-management settings, and hours, ideally 24 hours, not minutes — because a max from a short idle run is meaningless. I'd report p99, p99.9, p99.99 and max with a log-scale histogram, use rtla timerlat with auto-analysis to explain the worst case, and cross-check the final number externally with a GPIO toggle on a scope.

**꼬리 질문**: "cyclictest가 `/dev/cpu_dma_latency`에 0을 쓴다는 게 왜 중요한가?" → idle 진입을 막아 idle 탈출 지연이 결과에서 빠진다. 제품이 같은 PM QoS 요청을 하지 않으면 결과가 실제보다 좋게 나온다.

**Q.** Average latency is 8 µs but the max is 2 ms. What do you do next?

**A.** 평균은 무시하고 꼬리를 본다. 히스토그램으로 2 ms가 한 번인지, 일정한 크기의 섬인지 확인하고, CPU별로 나눈다. 그다음 `rtla timerlat top -a 1000`으로 첫 스파이크에서 멈춰 auto-analysis를 본다. IRQ latency가 크면 IRQ off 구간(드라이버의 `spin_lock_irqsave`, 긴 hardirq), thread만 크면 preempt off·우선순위·softirq다. 스택 트레이스가 함수까지 가리킨다. 트레이스에 아무것도 없으면 `rtla hwnoise`로 펌웨어·하이퍼바이저를 의심한다.

> The average doesn't matter; the tail does. I'd look at the histogram to see whether the 2 ms is a one-off or a recurring island, and split it per CPU. Then I'd run rtla timerlat top with --auto at, say, 1 ms so it stops at the first spike and prints the analysis. If the IRQ latency is large, something kept interrupts disabled — a driver holding a spinlock with irqsave or a long hardirq handler. If only the thread latency is large, it's preemption-disabled code, a higher-priority task or softirq work. The saved stack usually points at the function. If the OS trace shows nothing, I'd run rtla hwnoise to check for firmware or hypervisor stalls.

**Q.** What exactly does cyclictest measure?

**A.** SCHED_FIFO 스레드가 `clock_nanosleep(TIMER_ABSTIME)`로 다음 절대 시각까지 자고, 깨어나서 `clock_gettime`으로 읽은 시각과 기대 시각의 차이다. 타이머 IRQ 지연, hrtimer 처리, wakeup에서 스케줄링까지, context switch와 유저 복귀가 모두 합쳐진 한 숫자다. 구간을 나누려면 timerlat을 쓴다.

> A SCHED_FIFO thread sleeps with clock_nanosleep on an absolute deadline, wakes up, reads the clock and records how late it is. That one number includes the timer interrupt latency, the hrtimer processing, the wakeup-to-schedule delay and the context switch back to user space. It tells you how late, not why — for the breakdown I use timerlat, which records the latency both in the timer IRQ and in the thread.

**Q.** What's the difference between osnoise and timerlat?

**A.** osnoise는 "바쁜 워크로드가 빼앗긴 시간"을 잰다 — 처리량 중심의 HPC·DPDK형 작업에 맞다. 루프를 돌며 시간 구멍을 찾아 IRQ·softirq·NMI·thread·HW로 분류한다. timerlat은 "깨어나야 할 때 얼마나 늦나"를 잰다 — 주기적 제어 루프에 맞다. 둘 다 같은 커널 파일(`trace_osnoise.c`)의 tracer이고 rtla가 프런트엔드다.

> osnoise measures how much time a busy workload loses to interference, and classifies it into IRQ, softirq, NMI, thread and hardware noise — that fits polling or HPC-style workloads. timerlat measures how late a periodic thread wakes up, split into IRQ and thread latency — that fits control loops. Both are in-kernel tracers in trace_osnoise.c, and rtla is the user-space front end that configures them and analyzes the results.

**Q.** How long should a latency test run, and why?

**A.** 드문 이벤트를 만날 확률 때문에 길어야 한다. 한 시간에 한 번 생기는 스파이크를 1분 측정이 잡을 확률은 1.6%, 1시간이면 63%, 3시간이면 95%다. 그래서 개발 중에는 수십 분, 릴리스 판정은 수 시간~24시간 이상, 장기 안정성은 며칠 돌린다. 측정 시간을 늘리면 max는 거의 항상 커진다 — 그 증가가 멈추는지를 본다.

> Long enough to meet the rare events. If a spike happens once an hour, a one-minute run catches it with about 1.6 percent probability, one hour about 63 percent, three hours about 95 percent. So minutes during development, many hours or 24 hours for a release decision, days for long-term qualification. The max almost always grows with duration; what you want to see is that it stops growing.

**Q.** Your trace shows the timer IRQ arrived 3 ms late but nothing in the OS had interrupts disabled. What could it be?

**A.** OS 밖이다. x86이면 SMI, arm64면 EL3 secure monitor(TF-A의 SMC 처리, PSCI/SCMI, RAS 펌웨어), 가상화 환경이면 하이퍼바이저·호스트 스케줄링. idle 탈출이 깊은 경우도 있다. `hwlat` tracer나 `rtla hwnoise`로 IRQ를 끈 상태에서도 시간이 사라지는지 확인하고, 펌웨어 팀과 함께 SMC 호출 경로를 본다. 이 노트의 QEMU 실험에서는 hwlat이 호스트가 vCPU를 멈춘 시간을 그대로 잡았다.

> Then it's outside the OS. On x86 that's classically SMIs; on arm64 it's EL3 — the secure monitor handling SMCs, PSCI or SCMI calls, RAS firmware — or a hypervisor if we're virtualized. Deep idle exit is another candidate. I'd run the hwlat tracer or rtla hwnoise, which spin with interrupts disabled and report time gaps; if gaps show up there, the OS can't be the cause. In my QEMU lab, hwlat caught exactly that — host scheduling stalls of the vCPU thread.

**Q.** How do you compare two kernels fairly?

**A.** 같은 보드(가능하면 같은 개체), 같은 부하, 같은 시간, 같은 부트 인자·주파수·온도 조건, 같은 도구 버전. 한 번씩이 아니라 번갈아 여러 번(A-B-A-B) 돌려 측정 환경의 흔들림을 본다. 보고는 p50/p99/p99.9/p99.99/max 표 + 같은 축의 겹친 히스토그램.

> Same board, ideally the same unit; same workload, same duration, same boot arguments, frequency and thermal conditions, same tool versions. I alternate runs, A-B-A-B, so I can see how much the environment itself drifts. In my lab the same kernel's max changed eightfold just because the host got busier — that's the variance you have to rule out before claiming a kernel is better. The report is a percentile table plus overlaid histograms on the same axes.

---

## 16. 직접 해보기

1. (개념) cyclictest의 한 샘플을 네 구간으로 나눠 말하고, 각 구간을 늘리는 원인을 하나씩 들어라. 정답/힌트: 그림 2와 2.2절의 표.
2. (계산) 30분에 한 번 생기는 2 ms 스파이크가 있다. 10분, 1시간, 3시간 측정이 그것을 한 번이라도 볼 확률은? 정답/힌트: `1 - exp(-T/30min)` → 28%, 86%, 99.8%.
3. (코드) 4.2의 `n3_pct.py`를 고쳐 `--json` 출력(cyclictest `--json=`)에서도 같은 표를 만들어라. 정답/힌트: JSON에 스레드별 histogram 딕셔너리가 들어 있다 — 먼저 실제 파일을 열어 구조를 확인한다.
4. (실험) `n3_spike`를 mode=1(preempt off)로 넣고 cyclictest와 `rtla timerlat top`을 각각 돌려 결과를 비교하라. cyclictest만으로는 IRQ off와 구분이 되는가? 정답/힌트: 안 된다. timerlat의 IRQ 열이 조용한지가 구분점(7.2절).
5. (실험) `rtla osnoise top`을 `-P f:95` 없이/있이 돌려 Thread 노이즈 열이 어떻게 바뀌는지 보라. 정답/힌트: 6.3절. osnoise 스레드가 RT면 일반 스레드는 끼어들지 못하고 IRQ·softirq만 남는다.
6. (관찰) `/sys/kernel/tracing/osnoise/` 디렉터리의 파일을 손으로 설정해 rtla 없이 timerlat을 돌려 보라(`echo timerlat > current_tracer`, `cat trace`). 정답/힌트: `timerlat_period_us`, `stop_tracing_us`, `print_stack`. rtla가 하는 일이 이 파일 쓰기라는 것을 확인.
7. (보고) 이 노트의 Image-trace 결과와 Image-rt 결과로 11.2절의 보고서 템플릿을 채워라. 정답/힌트: 조건 표에 "QEMU TCG, 호스트 load average"를 반드시 적는다.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| latency (wakeup latency) | 깨어남 지연 | 기대 시각과 실제 실행 시각의 차이 |
| jitter | 지터 | 지연의 흔들림. 보통 max−min 또는 분포 폭 |
| cyclictest | rt-tests의 측정 도구 | 주기적으로 자고 깨어나며 지연 히스토그램을 만든다 |
| rtla | Real-Time Linux Analysis | 커널 `tools/tracing/rtla`. osnoise/timerlat/hwnoise 프런트엔드 |
| osnoise | OS noise tracer | 바쁜 워크로드가 빼앗긴 시간을 원인별로 센다 |
| timerlat | timer latency tracer | IRQ latency와 thread latency를 분리해 잰다 |
| hwlat | hardware latency detector | IRQ를 끄고 시간 구멍을 찾아 OS 밖 정지를 탐지 |
| hwnoise | rtla 하위 명령 | IRQ를 끈 osnoise. OS 밖 소음만 남긴다 |
| auto-analysis | rtla timerlat 자동 분석 | 멈춘 샘플의 지연을 구성 요소별 %로 분해 |
| percentile (p99.9) | 백분위 | 샘플의 99.9%가 이 값 이하 |
| histogram overflow | 히스토그램 범위 초과 | `-h` 상한을 넘은 샘플 수 |
| SMI | System Management Interrupt | x86 펌웨어가 OS 몰래 CPU를 쓰는 인터럽트 |
| EL3 / secure monitor | arm64 최고 예외 레벨 | TF-A BL31이 SMC·PSCI를 처리하는 곳. OS 밖 정지의 arm64 대응물 |
| PM QoS (`/dev/cpu_dma_latency`) | 전원 관리 지연 요구 | 열어 둔 동안 idle 상태 깊이를 제한 |
| icount | QEMU 명령어 계수 모드 | 명령어 수로 게스트 시간을 진행시키는 결정적 시계 |

---

## 18. 요약 & 체크리스트

지연은 한 숫자가 아니라 **조건이 붙은 분포**다. cyclictest는 SCHED_FIFO 스레드의 타이머 깨어남 지연을 전 구간 합으로 재고, `-m -S -p 95 -i 1000 -h N -q -D 시간 --histfile`로 돌려 백분위와 로그 히스토그램을 만든다. 측정에는 제품 수준의 부하와 긴 시간이 필수다. 짧은 측정의 max는 거의 항상 실제 최악값보다 작다. 원인 분석은 rtla timerlat으로 IRQ 단계와 thread 단계를 가르고, `--auto`로 첫 스파이크에서 멈춰 스택까지 확인한다. osnoise는 바쁜 워크로드의 소음을 원인별로, hwlat/hwnoise는 OS 밖 정지를 보여 준다. 비교는 같은 HW·부하·시간에서 번갈아 여러 번 하고, 최종 수치는 HW(GPIO + scope)로 교차 검증한다. 이 노트의 숫자는 QEMU TCG라 절대값은 의미가 없고, 방법과 모양만 가져간다.

- [ ] cyclictest가 재는 구간을 그림으로 그리고 네 부분으로 나눠 설명할 수 있다
- [ ] 표준 cyclictest 커맨드라인의 각 옵션(`-m -S -p -i -h -q -D --histfile -b`)의 이유를 말할 수 있다
- [ ] 부하 도구(stress-ng, hackbench, ping -f, make -j)로 현실적인 부하를 설계할 수 있다
- [ ] 히스토그램 파일에서 p99/p99.9/p99.99를 계산하는 스크립트를 짤 수 있다
- [ ] 로그 y축 히스토그램의 모양(꼬리, bimodal, 섬)을 원인 후보와 연결할 수 있다
- [ ] 측정 시간과 드문 이벤트를 잡을 확률의 관계를 계산할 수 있다
- [ ] `rtla timerlat top/hist`로 IRQ latency와 thread latency를 가르고 해석할 수 있다
- [ ] `rtla timerlat top -a`의 auto-analysis 출력에서 범인 함수를 찾을 수 있다
- [ ] `rtla osnoise top/hist`의 열(IRQ, Softirq, Thread, HW, NMI)을 설명할 수 있다
- [ ] hwlat/hwnoise 결과가 무엇을 뜻하는지, arm64에서 OS 밖 원인 후보를 말할 수 있다
- [ ] cyclictest `-b` + ftrace로 스파이크 순간의 트레이스를 남길 수 있다
- [ ] 두 커널을 공정하게 비교하고 설계 리뷰용 보고서를 쓸 수 있다

---

## 참고 자료

- Linux 커널 문서 — RTLA: https://docs.kernel.org/tools/rtla/index.html (`rtla-timerlat-top`, `rtla-osnoise-top`, `rtla-hwnoise` 등 하위 페이지)
- Linux 커널 문서 — OSNOISE Tracer: https://docs.kernel.org/trace/osnoise-tracer.html
- Linux 커널 문서 — Timerlat tracer: https://docs.kernel.org/trace/timerlat-tracer.html
- Linux 커널 문서 — Hardware Latency Detector: https://docs.kernel.org/trace/hwlat_detector.html
- rt-tests 소스(cyclictest): https://git.kernel.org/pub/scm/utils/rt-tests/rt-tests.git
- Linux Foundation Realtime wiki — cyclictest 사용법: https://wiki.linuxfoundation.org/realtime/documentation/howto/tools/cyclictest/start
- OSADL Realtime QA Farm(장기 latency 측정·히스토그램 예): https://www.osadl.org/ (QA Farm Realtime 메뉴)
- 커널 소스(6.18.55): `kernel/trace/trace_osnoise.c`, `kernel/trace/trace_hwlat.c`, `tools/tracing/rtla/src/timerlat_top.c`, `tools/tracing/rtla/src/timerlat_aa.c` — https://elixir.bootlin.com/linux/v6.18/source
- Daniel Bristot de Oliveira, "Demystifying the real-time Linux scheduling latency" (ECRTS 2020) — timerlat/osnoise의 이론적 배경
- 관련 노트: N1, N2, F1 §8~9, F6, F8, M5, M11, A5
