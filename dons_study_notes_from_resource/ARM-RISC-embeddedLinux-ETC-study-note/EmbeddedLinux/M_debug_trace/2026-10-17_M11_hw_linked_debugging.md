# M11. HW 연계 디버깅 — scope·LA로 커널 타이밍을 "핀으로" 증명하고, 버스 캡처와 커널 로그를 한 시간축에 맞추기

> **이 노트를 다 읽으면**: 커널 각 단계(hardirq·threaded IRQ·workqueue·유저 복귀)에 디버그 GPIO 엣지를 심어 지연을 핀으로 잴 수 있다 · 커널 timestamp와 핀 엣지가 왜·얼마나 어긋나는지 설명할 수 있다 · ftrace 시계와 LA 시계를 sync 엣지로 맞추고 오차를 수치로 말할 수 있다 · LA의 I2C/SPI 디코드와 i2c/spi/regmap tracepoint를 내용과 시각으로 짝지을 수 있다
> **왜 필요한가**: "센서 데이터가 가끔 늦다", "지터가 200 µs 튄다" 같은 문제에서 커널 로그만 보면 범인을 못 잡는다. 핀과 버스는 거짓말을 하지 않는다 — 로보틱스·드론 면접에서 "어떻게 증명하나?"가 반드시 나온다
> **Don 기준 난이도**: scope·LA·트리거·프로토콜 디코드는 이미 Don의 무기 / 그걸 Linux의 어느 지점(hardirq, irq thread, workqueue, wakeup)에 꽂는지, ftrace 시계와 어떻게 맞추는지, gpiolib의 can_sleep 같은 Linux 쪽 제약이 새로 배울 부분
> **선행 노트**: H4(gpio-sim·gpiod API·edge 이벤트 지연), H5(i2c tracepoint·가짜 컨트롤러), H7(SPI), H2(regmap), F6(IRQ·threaded IRQ), F7(workqueue), F8(hrtimer·clocksource), M5(ftrace), N3(cyclictest·rtla로 지연 측정)

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 커널 timestamp는 "핀이 움직인 시각"이 아니다

Don이 RF 칩 bring-up에서 늘 하던 일을 떠올려 보자. 펌웨어 로그에 "TX start @ 1234 us"라고 찍혀 있어도, 정말 PA enable 핀이 그 시각에 올라갔는지는 scope로 봐야 믿는다. 로그는 **코드가 그 줄을 지나간 시각**이고, 핀은 **하드웨어가 실제로 바뀐 시각**이다. 둘 사이에는 버스 쓰기 버퍼, 인터럽트, 선점, 캐시 미스, (가상 머신이라면) 호스트 스케줄링까지 끼어든다.

Linux에서도 똑같다. ftrace의 `gpio_value` 이벤트는 gpiolib이 드라이버의 `.set` 콜백을 **부르기 직전에** 찍힌다. 그 사이에 선점이 일어나면, 로그상 시각과 핀 엣지는 수 ms까지 벌어질 수 있다 — 이 노트 §5에서 실제로 2.5 ms, 14.8 ms 차이를 잡는다.

그래서 HW 연계 디버깅의 원칙은 단순하다.

1. **지연·지터의 ground truth는 핀이다.** 외부 신호(센서 DRDY)와 소프트웨어 단계마다 토글한 디버그 핀을 같은 LA/scope 채널에 넣으면, 같은 하드웨어 시계(LA의 샘플 클럭)로 모든 단계를 잰다.
2. **커널 트레이스는 "왜"를 알려 준다.** 핀이 늦었다는 사실은 LA가, 그 사이에 누가 CPU를 잡고 있었는지는 ftrace(`sched_switch`, `irq_handler_entry`)가 알려 준다.
3. **둘을 같은 시간축에 올리는 기술**이 이 노트의 핵심이다 — sync 엣지 하나가 아니라 여러 엣지로, 평균이 아니라 중앙값으로.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 300">
<defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="290" height="120" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="165" y="42" font-size="14" text-anchor="middle">소프트웨어 시간축 (커널 시계)</text>
<text x="34" y="66" font-size="12">ftrace: irq_handler_entry, sched_switch</text>
<text x="34" y="86" font-size="12">tracepoint: gpio_value, i2c_write ...</text>
<text x="34" y="106" font-size="12">trace_marker: 유저 로그</text>
<text x="34" y="126" font-size="12">= 코드가 그 줄을 지난 시각</text>
<rect x="350" y="20" width="290" height="120" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="495" y="42" font-size="14" text-anchor="middle">하드웨어 시간축 (LA 샘플 클럭)</text>
<text x="364" y="66" font-size="12">센서 DRDY 핀, 디버그 GPIO 엣지</text>
<text x="364" y="86" font-size="12">I2C/SPI/CAN 파형 + 프로토콜 디코드</text>
<text x="364" y="106" font-size="12">전원 레일 전류 (current probe)</text>
<text x="364" y="126" font-size="12">= 실제로 전기가 바뀐 시각</text>
<rect x="170" y="190" width="320" height="90" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="330" y="214" font-size="14" text-anchor="middle">하나의 타임라인</text>
<text x="184" y="238" font-size="12">sync 엣지: 같은 사건을 양쪽에서 기록</text>
<text x="184" y="258" font-size="12">offset = median(LA 시각 − trace 시각)</text>
<text x="184" y="276" font-size="12">잔차(residual) = 커널이 "속인" 양</text>
<line x1="165" y1="140" x2="280" y2="188" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="495" y1="140" x2="380" y2="188" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```

그림 1 — 두 시간축. 소프트웨어 트레이스는 "무엇이 일어났나·왜"를, 핀과 버스는 "정말 언제"를 준다. 둘을 같은 사건(sync 엣지)으로 묶어 한 타임라인으로 만든다.

### 0.2 bare-metal과 무엇이 다른가

| 항목 | bare-metal / RTOS (Don의 경험) | Linux |
|---|---|---|
| 디버그 핀 토글 | ISR 첫 줄에 `GPIO->BSRR = PIN` 한 줄, 수십 ns | gpiolib 경유(`gpiod_set_value`) 또는 MMIO 직접 쓰기. 컨트롤러가 I2C 확장기면 hardirq에서 **못 씀**(can_sleep) |
| ISR이 몇 단계인가 | 보통 1단계(ISR → 플래그 → 메인 루프) | hardirq → threaded IRQ → workqueue/softirq → wakeup → 스케줄러 → 유저 복귀, 각 단계가 지연원 |
| 지연을 일으키는 것 | 우선순위 높은 ISR, critical section | 위 + 선점 비활성 구간, 다른 프로세스, 캐시, CPU idle 진입/복귀, cpufreq, (VM이면) 호스트 |
| 소프트웨어 타임스탬프 | DWT CYCCNT 같은 단일 카운터 | 시계가 여러 개: `local`, `global`, `mono`, `boot`… ftrace `trace_clock`으로 고른다 |
| 버스 로그 | 직접 넣은 printf | i2c/smbus/spi/regmap tracepoint가 이미 있다 |

### 0.3 이 랩에서 진짜와 가짜 — 먼저 분명히 해 두기

이 노트의 실습 환경에는 **진짜 보드도, 진짜 scope도 없다.** QEMU `virt` 머신(TCG 에뮬레이션, 호스트는 Apple M2 위의 Lima VM, 17명이 동시에 쓰는 공용 VM — 측정 당시 load average 15~20)에서 돌린다. 그래서 다음처럼 대응시킨다.

| 진짜 벤치 | 이 랩의 대역 | 얼마나 진짜인가 |
|---|---|---|
| SoC의 GPIO 컨트롤러 (MMIO) | QEMU `virt`의 **PL061** GPIO (0x09030000) | 진짜 Linux 드라이버(`gpio-pl061`)가 진짜 MMIO 레지스터를 쓴다. 핀만 밖으로 안 나간다 |
| 외부 센서의 DRDY 핀 → GPIO IRQ | 커널 모듈의 **hrtimer**가 "외부 신호"를 흉내 내고, **gpio-sim** 라인의 IRQ를 pending으로 만든다 | IRQ 이후 경로(handler, thread, workqueue, wakeup)는 진짜. "핀 → GIC" 구간은 irq_work IPI로 대체 |
| 로직 분석기 (외부 관찰자) | QEMU 자체 trace 이벤트 `pl061_set_output` — 게스트가 PL061 레지스터를 쓰는 **순간** 호스트가 µs 단위로 기록 | 게스트 커널 밖에서 찍힌다는 점에서 진짜 "외부 관찰자". 해상도 1 µs, 시계는 호스트 UTC |
| 가상 LA (소프트웨어) | ftrace `gpio:gpio_value` tracepoint | 진짜 커널 기능. 실제 보드에서도 그대로 쓴다 |
| I2C 센서 + LA 디코드 | 가짜 I2C 컨트롤러 `m11_i2c`(0x68 IMU 흉내) + **합성(mock)** 디코드 그림 | tracepoint는 진짜. LA 디코드 파형은 바이트로부터 100 kHz 모델로 그린 것 |
| scope 설정, sigrok-cli, Saleae, current probe | — | §7은 **가이드(실행 안 함)** |

TCG에서는 명령 하나하나를 호스트가 번역·실행하므로 **절대 수치는 실보드보다 10~100배 느리고 들쭉날쭉하다.** 이 노트의 숫자는 "방법이 맞는지, 상대 비교가 어떻게 나오는지"를 보여 주는 용도다. 실보드 기대치를 말할 때는 따로 "일반적 경험치"라고 밝힌다.

---

## 1. 실습 판 깔기 — 디버그 핀 5개, IRQ 소스 1개, 관찰자 2명

### 1.1 핀 배치

| 신호 | 칩 · 라인 | 전역 GPIO 번호 | 누가 토글하나 |
|---|---|---|---|
| EXT | PL061 line 0 | 512 | hrtimer 콜백 ("외부 센서 DRDY") |
| HARD | PL061 line 1 | 513 | hardirq 핸들러 (top half) |
| THREAD | PL061 line 2 | 514 | threaded IRQ 핸들러 (`irq/23-m11_lat` 커널 스레드) |
| (사용 중) | PL061 line 3 | 515 | gpio-keys의 전원 버튼 — 건드리지 않는다 |
| WORK | PL061 line 4 | 516 | workqueue 작업 함수 (`kworker`) |
| USER | PL061 line 5 | 517 | 유저 프로그램 `m11_user` (uAPI v2 ioctl) |
| RATE-U / RATE-K | PL061 line 6 / 7 | 518 / 519 | 토글 속도 실험 (§2) |
| src | gpio-sim `m11_sim` line 0 | (동적) | IRQ 소스. hrtimer가 pending을 주입 |

PL061을 디버그 핀으로 고른 이유가 중요하다. gpio-sim은 내부 상태를 mutex로 보호해서 `can_sleep = true`로 등록된다(H4 §4.1). 잠들 수 있는 칩의 핀은 **hardirq에서 토글할 수 없다** — `gpiod_set_value()`가 `WARN_ON(desc->gdev->can_sleep)`을 친다(H4 §5.5에서 재현). 실보드에서 "디버그 핀을 I2C GPIO 확장기에 물렸더니 ISR에서 못 쓴다"와 같은 상황이다. 디버그 핀은 **반드시 SoC 내장 GPIO(MMIO)**에 둔다.

### 1.2 QEMU 실행 스크립트 — "외부 LA"를 켜는 한 줄

무엇을 확인하나: QEMU의 `-trace pl061_set_output`과 `-msg timestamp=on`으로 게스트 밖에서 핀 변화를 µs 타임스탬프로 기록하고, 게스트의 ftrace 버퍼는 두 번째 virtio 디스크에 통째로 쏟아 받는다.

```sh
#!/bin/sh
# q.sh <kernel> <tag> "<LAB_CMD>" : initramfs 부팅, pl061 QEMU trace -> <tag>.qlog, 게스트 ftrace -> <tag>.trace
K=$1; TAG=$2; CMD=$3; O=/var/tmp/M11
truncate -s 64M $O/$TAG.disk
timeout ${TMO:-300} qemu-system-aarch64 -machine virt -cpu cortex-a57,pmu=on -smp 2 -m 1G -nographic \
  -kernel $K -initrd $O/init.gz \
  -drive file=$O/$TAG.disk,if=none,format=raw,id=d1 -device virtio-blk-device,drive=d1 \
  -msg timestamp=on -trace "pl061_set_output" -D $O/$TAG.qlog \
  -append "console=ttyAMA0 quiet LAB_CMD=$CMD" > $O/$TAG.con 2>&1
tr -d '\000' < $O/$TAG.disk > $O/$TAG.trace; rm -f $O/$TAG.disk
```

- `-trace pl061_set_output`: QEMU의 PL061 모델이 출력 비트가 **바뀔 때마다** 한 줄 남긴다(바뀌지 않는 쓰기는 기록 안 됨 — 진짜 LA도 엣지만 본다).
- `-D`: trace 로그 파일. `-msg timestamp=on`이 각 줄 앞에 호스트 UTC 시각(µs)을 붙인다.
- 커널은 `/var/lab/out/Image-trace`(6.18.55-trace, ftrace 전부 켜진 랩 커널). VM 호스트 커널은 `7.0.0-38-generic`이고, 랩 모듈은 호스트 커널이 아니라 QEMU 게스트에만 넣는다.

### 1.3 게스트 스크립트 — gpio-sim 칩, ftrace, 모듈, 유저 프로그램

무엇을 확인하나: configfs로 IRQ 소스용 gpio-sim 칩(`m11_sim`)을 만들고, ftrace를 `mono` 시계로 맞춘 뒤 `gpio_value`와 IRQ·스케줄러 이벤트를 켜고 측정을 돌린다.

```sh
#!/bin/sh
# g_lat.sh (게스트) : configfs gpio-sim 칩 + tracing + m11_lat
T=/sys/kernel/tracing
mount -t tracefs none $T 2>/dev/null
mount -t configfs none /sys/kernel/config
C=/sys/kernel/config/gpio-sim/m11
mkdir -p $C/bank0 && echo m11_sim > $C/bank0/label && echo 4 > $C/bank0/num_lines
echo 1 > $C/live
echo 0 > $T/tracing_on; echo ${CLK:-mono} > $T/trace_clock
echo 8192 > $T/buffer_size_kb
echo 1 > $T/events/gpio/gpio_value/enable
for e in irq/irq_handler_entry irq/irq_handler_exit sched/sched_wakeup sched/sched_switch \
         workqueue/workqueue_execute_start timer/hrtimer_expire_entry; do echo 1 > $T/events/$e/enable; done
[ -n "$LOAD" ] && for i in 1 2; do (while :; do dd if=/dev/zero of=/dev/null bs=64k count=2000 2>/dev/null; done) & done
insmod /m11_lat.ko period_us=${PER:-3000} count=${N:-40}
dmesg | grep "m11_lat:"; grep -A9 "9030000.pl061" /sys/kernel/debug/gpio; grep -A5 "m11_sim" /sys/kernel/debug/gpio
PL=/dev/$(ls /sys/bus/amba/devices/9030000.pl061/ | grep gpiochip)
echo 1 > $T/tracing_on
/m11_user lat $PL 5 ${N:-40}
echo 0 > $T/tracing_on
cat /proc/interrupts | grep -E "m11|CPU"
cat $T/trace > /dev/vda 2>/dev/null || cat $T/trace | tail -${SHOW:-30}
echo "lines=$(wc -l < $T/trace)"
```

### 1.4 예제 1 — 핀이 제대로 잡혔는지 확인

무엇을 확인하나: 모듈이 PL061 라인 0·1·2·4를 `m11_dbg`라는 이름으로, gpio-sim 라인 0을 `src`로 잡았는지, IRQ 번호가 무엇인지.

```sh
limactl shell elinux -- bash -c 'S=/Users/donh/.elinux_scratch/M11; sh $S/mkinit.sh; TMO=200 sh $S/q.sh /var/lab/out/Image-trace r_info "N=5~sh~/g_lat.sh"; grep -v "^\s*$" /var/tmp/M11/r_info.con'
```

```text
lab initramfs: 6.18.55-trace up 21.37s
gpiochip0: 8 GPIOs, parent: amba/9030000.pl061, 9030000.pl061:
gpiochip1: 4 GPIOs, parent: platform/gpio-sim.0, m11_sim, can sleep:
[   27.420003][   T82] m11_lat: loading out-of-tree module taints kernel.
[   27.428362][   T82] m11_lat: module verification failed: signature and/or required key missing - tainting kernel
[   28.601148][   T82] m11_lat: irq=23 gpio ext=512 hard=513 thread=514 work=516 period=3000us
gpiochip0: 8 GPIOs, parent: amba/9030000.pl061, 9030000.pl061:
 gpio-0   (                    |m11_dbg             ) out lo 
 gpio-1   (                    |m11_dbg             ) out lo 
 gpio-2   (                    |m11_dbg             ) out lo 
 gpio-3   (                    |GPIO Key Poweroff   ) in  lo IRQ 
 gpio-4   (                    |m11_dbg             ) out lo 
gpiochip1: 4 GPIOs, parent: platform/gpio-sim.0, m11_sim, can sleep:
 gpio-0   (src) input,pull-down
 gpio-1   (<unused>) input,pull-down
...
user wakeups=2/5
           CPU0       CPU1       
 23:          0          5  irq_sim   0 Edge      m11_lat
lines=502
```

출력에서 볼 것: PL061 칩 머리에는 `can sleep`이 없고 gpio-sim 칩에는 있다 — 이것이 디버그 핀을 PL061에 둔 이유다. IRQ 23은 `irq_sim` 도메인(gpio-sim의 소프트웨어 IRQ 칩)에서 왔고 5번 다 들어왔지만, 유저는 2번만 깨어났다(`wakeups=2/5`). 나머지 3번은 어디서 사라졌을까? 바로 이 질문에 LA가 답한다(§4.4). (`...`는 gpio-sim 덤프가 두 번 찍힌 중복 줄을 생략한 것이다.)

---

## 2. GPIO 토글 기법 — 어디서, 무엇으로 토글하나

### 2.1 세 가지 방법과 비용

| 방법 | 코드 | 컨텍스트 | 오버헤드 | ftrace에 보이나 |
|---|---|---|---|---|
| 유저 공간 (libgpiod / uAPI v2) | `ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, ...)` | 프로세스 | syscall + gpiolib + 드라이버. 스케줄링 지터가 그대로 들어온다 | 보인다 (`gpio_value`) |
| 커널 gpiod API | `gpiod_set_value()` / `gpiod_set_value_cansleep()` | 전자는 어디서나(칩이 안 잠들면), 후자는 프로세스 컨텍스트만 | gpiolib 검사(SRCU 가드) + tracepoint + 드라이버 콜백(PL061은 `writeb` 한 번, 주소 마스크라 잠금도 없다) | 보인다 |
| MMIO 직접 쓰기 | `writel(BIT(n), base + SET_REG)` | 어디서나 | 레지스터 쓰기 1번 | **안 보인다** — scope만 본다 |

`gpiod_set_value()` vs `_cansleep()`의 차이는 "핀이 어떤 칩에 있나"다. SoC 내장 GPIO(MMIO)는 `can_sleep = false`라 hardirq에서도 `gpiod_set_value()`가 된다. I2C/SPI GPIO 확장기 뒤의 핀은 버스 전송이 필요하므로 `can_sleep = true`이고, `_cansleep()`만 써야 하며 **hardirq·spinlock 안에서는 아예 못 쓴다.** 디버그 핀을 고를 때 첫 번째 조건이다.

### 2.2 예제 2 — 유저 공간 토글 프로그램 (uAPI v2)

무엇을 확인하나: libgpiod가 내부에서 하는 일(라인 요청 → 값 설정 ioctl)을 직접 써서, (lat) 커널이 깨울 때마다 USER 핀에 펄스를 내고, (rate) 최대 속도로 토글하며 간격을 잰다.

```c
/* m11_user: (lat) 커널 단계 체인의 마지막 핀  (rate) 유저 공간 토글 속도·지터 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

static long long now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000LL + t.tv_nsec;
}
static int line_out(const char *chip, unsigned int off)
{
	struct gpio_v2_line_request r = { .num_lines = 1, .offsets = { off } };
	int c = open(chip, O_RDONLY);
	if (c < 0) { perror(chip); exit(1); }
	r.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
	strcpy(r.consumer, "m11_user");
	if (ioctl(c, GPIO_V2_GET_LINE_IOCTL, &r) < 0) { perror("GET_LINE"); exit(1); }
	close(c);
	return r.fd;
}
static void set(int fd, int v)
{
	struct gpio_v2_line_values lv = { .bits = (__u64)v, .mask = 1 };
	if (ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &lv) < 0) { perror("SET"); exit(1); }
}
static int cmp(const void *a, const void *b)
{
	long long x = *(const long long *)a, y = *(const long long *)b;
	return x > y ? 1 : x < y ? -1 : 0;
}
int main(int argc, char **argv)
{
	if (argc < 5) { fprintf(stderr, "usage: lat/rate chip line n\n"); return 2; }
	int fd = line_out(argv[2], atoi(argv[3])), n = atoi(argv[4]);
	if (!strcmp(argv[1], "lat")) {	/* 커널이 깨워 줄 때마다 USER 핀 펄스 */
		int d = open("/dev/m11_lat", O_RDWR);
		if (d < 0 || write(d, "go", 2) != 2) { perror("m11_lat"); return 1; }
		int got = 0;
		while (got < n && read(d, NULL, 0) == 0) { set(fd, 1); set(fd, 0); got++; }
		printf("user wakeups=%d/%d\n", got, n);
		return 0;
	}
	long long *t = malloc(sizeof(*t) * (n + 1));	/* rate: 최대 속도로 n번 토글 */
	if (!t) { perror("malloc"); return 1; }
	t[0] = now();
	for (int i = 1; i <= n; i++) { set(fd, i & 1); t[i] = now(); }
	for (int i = 0; i < n; i++) t[i] = t[i + 1] - t[i];
	qsort(t, n, sizeof(*t), cmp);
	printf("user toggle n=%d p50=%lldns p99=%lldns max=%lldns\n", n, t[n / 2], t[n * 99 / 100], t[n - 1]);
	return 0;
}
```

VM 호스트에서 `gcc -static -std=gnu11 -Wall -Wextra -O2 m11_user.c -o /var/tmp/M11/m11_user`로 빌드했다(경고 0개, static이라 BusyBox initramfs에서 그대로 돈다).

### 2.3 예제 3 — 커널 토글: gpiod API vs 레지스터 직접 쓰기

무엇을 확인하나: 같은 PL061 line 7을 (1) `gpiod_set_value()`로 2000번, (2) PL061 `GPIODATA` 레지스터에 `writeb`로 2000번 토글하고 각각 토글당 비용을 잰다. PL061의 GPIODATA는 **주소 비트 [9:2]가 쓰기 마스크**라서 `base + (BIT(7) << 2)`에 쓰면 bit 7만 바뀐다 — read-modify-write가 필요 없는, 디버그 핀에 딱 좋은 하드웨어다(STM32의 BSRR과 같은 발상).

```c
/* m11_rate.c — 같은 핀(PL061 line 7)을 gpiod API vs 레지스터 직접 쓰기로 토글 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/irqflags.h>

#define N 2000
static struct gpiod_lookup_table lt = {
	.dev_id = "m11_rate",
	.table = { GPIO_LOOKUP("9030000.pl061", 7, "rate", GPIO_ACTIVE_HIGH), { } },
};
static struct platform_device *pdev;

static int __init m11_rate_init(void)
{
	struct gpio_desc *d;
	void __iomem *base;
	unsigned long flags;
	u64 t0, t1, t2;
	int i, ret = 0;

	gpiod_add_lookup_table(&lt);
	pdev = platform_device_register_simple("m11_rate", -1, NULL, 0);
	if (IS_ERR(pdev)) { gpiod_remove_lookup_table(&lt); return PTR_ERR(pdev); }
	d = gpiod_get(&pdev->dev, "rate", GPIOD_OUT_LOW);	/* 방향 설정은 드라이버에 맡김 */
	if (IS_ERR(d)) { ret = PTR_ERR(d); goto out; }
	base = ioremap(0x09030000, 0x1000);	/* 디버그 전용 꼼수: pl061 영역을 한 번 더 맵 */
	if (!base) { ret = -ENOMEM; goto put; }

	local_irq_save(flags);
	t0 = ktime_get_ns();
	for (i = 0; i < N; i++)
		gpiod_set_value(d, i & 1);		/* gpiolib → pl061_set_value → writeb */
	t1 = ktime_get_ns();
	for (i = 0; i < N; i++)
		writeb((i & 1) << 7, base + (BIT(7) << 2));	/* GPIODATA 주소 마스크: bit7만 */
	t2 = ktime_get_ns();
	local_irq_restore(flags);

	pr_info("m11_rate: gpiod_set_value %llu ns/toggle, raw writeb %llu ns/toggle (n=%d)\n",
		(t1 - t0) / N, (t2 - t1) / N, N);
	iounmap(base);
put:
	gpiod_put(d);
out:
	platform_device_unregister(pdev);
	gpiod_remove_lookup_table(&lt);
	return ret ? ret : -EAGAIN;	/* 측정만 하고 남지 않는다 */
}
module_init(m11_rate_init);
MODULE_DESCRIPTION("M11: toggle cost gpiod vs MMIO");
MODULE_LICENSE("GPL");
```

- DT 없이 consumer가 핀을 얻는 방법: `gpiod_lookup_table`에 "칩 라벨 `9030000.pl061`의 7번 라인을 장치 `m11_rate`의 `rate` 핀으로"라고 등록하고, 같은 이름의 platform device를 만든 뒤 `gpiod_get()`. 실제 드라이버라면 DT의 `debug-gpios = <&gpio 7 GPIO_ACTIVE_HIGH>;`가 이 표 역할을 한다(D4, H4).
- `ioremap`으로 이미 드라이버가 잡은 영역을 한 번 더 맵하는 것은 **디버그 전용 꼼수**다. 같은 레지스터를 드라이버와 동시에 쓰면 경쟁이 생긴다. PL061의 주소 마스크 덕분에 다른 비트는 안 건드리지만, 일반 SoC에서는 SET/CLR 레지스터가 있는 컨트롤러에서만 안전하다.
- `return -EAGAIN`: 측정만 하고 모듈이 메모리에 남지 않게 init에서 실패를 돌려준다. insmod는 에러를 출력한다(아래 출력에서 보인다).

빌드는 `make -C /var/lab/out/linux-build-trace M=$PWD`(vermagic `6.18.55-trace SMP preempt mod_unload aarch64`), 경고 0개.

### 2.4 예제 4 — 측정: 안에서 잰 비용과 "LA"가 본 간격

무엇을 확인하나: 유저 토글 3회 + tracepoint를 켠 유저 토글 1회, 커널 토글 3회. 게스트 안 측정(`clock_gettime`, `ktime_get_ns`)과 QEMU 로그의 엣지 간격(외부 관찰자)을 둘 다 본다.

```sh
#!/bin/sh
# g_rate.sh (게스트)
T=/sys/kernel/tracing
mount -t tracefs none $T 2>/dev/null
PL=/dev/$(ls /sys/bus/amba/devices/9030000.pl061/ | grep gpiochip)
for r in 1 2 3; do /m11_user rate $PL 6 2000; done
echo 1 > $T/events/gpio/gpio_value/enable
echo "with gpio_value tracepoint on:"; /m11_user rate $PL 6 2000
echo 0 > $T/events/gpio/gpio_value/enable
for r in 1 2 3; do insmod /m11_rate.ko; done 2>&1
dmesg | grep m11_rate
```

게스트 콘솔 출력(실제):

```text
lab initramfs: 6.18.55-trace up 24.07s
user toggle n=2000 p50=81904ns p99=2039584ns max=22533600ns
user toggle n=2000 p50=69872ns p99=604112ns max=2984720ns
user toggle n=2000 p50=76048ns p99=1173296ns max=4916544ns
with gpio_value tracepoint on:
user toggle n=2000 p50=109200ns p99=10588560ns max=257737136ns
insmod: can't insert '/m11_rate.ko': Resource temporarily unavailable
insmod: can't insert '/m11_rate.ko': Resource temporarily unavailable
insmod: can't insert '/m11_rate.ko': Resource temporarily unavailable
[   35.006609][   T85] m11_rate: loading out-of-tree module taints kernel.
[   35.007083][   T85] m11_rate: module verification failed: signature and/or required key missing - tainting kernel
[   35.201338][   T85] m11_rate: gpiod_set_value 11040 ns/toggle, raw writeb 5895 ns/toggle (n=2000)
[   35.587776][   T86] m11_rate: gpiod_set_value 7111 ns/toggle, raw writeb 6145 ns/toggle (n=2000)
[   36.062520][   T87] m11_rate: gpiod_set_value 4982 ns/toggle, raw writeb 3539 ns/toggle (n=2000)
```

같은 실행의 QEMU 로그를 `rate.py`(QEMU 로그에서 라인 6·7의 엣지 간격 분포를 뽑는 30줄짜리 스크립트)로 분석한 "LA 시점" 결과:

```text
user run1                  edges= 2000 p50=  82.0us p99=  1977.9us max=  22493.1us
user run2                  edges= 2000 p50=  69.1us p99=   593.9us max=   3017.9us
user run3                  edges= 2000 p50=  75.1us p99=  1138.9us max=   3444.0us
user run4 (tracepoint on)  edges= 2000 p50= 111.1us p99= 10644.9us max= 257042.2us
kernel gpiod run1          edges= 1999 p50=   3.1us p99=    49.1us max=   4929.1us
kernel raw writeb run1     edges= 2000 p50=   2.9us p99=    59.8us max=    981.1us
kernel gpiod run2          edges= 1999 p50=   3.1us p99=    17.9us max=   5986.0us
kernel raw writeb run2     edges= 2001 p50=   2.9us p99=    36.0us max=   2888.9us
kernel gpiod run3          edges= 1999 p50=   3.1us p99=    37.0us max=    325.0us
kernel raw writeb run3     edges= 2001 p50=   2.9us p99=    18.8us max=    240.1us
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 270">
<line x1="190.0" y1="20" x2="190.0" y2="215" stroke="currentColor" stroke-opacity="0.25"/>
<text x="190.0" y="232" font-size="12" text-anchor="middle">1µs</text>
<line x1="280.0" y1="20" x2="280.0" y2="215" stroke="currentColor" stroke-opacity="0.25"/>
<text x="280.0" y="232" font-size="12" text-anchor="middle">10µs</text>
<line x1="370.0" y1="20" x2="370.0" y2="215" stroke="currentColor" stroke-opacity="0.25"/>
<text x="370.0" y="232" font-size="12" text-anchor="middle">100µs</text>
<line x1="460.0" y1="20" x2="460.0" y2="215" stroke="currentColor" stroke-opacity="0.25"/>
<text x="460.0" y="232" font-size="12" text-anchor="middle">1ms</text>
<line x1="550.0" y1="20" x2="550.0" y2="215" stroke="currentColor" stroke-opacity="0.25"/>
<text x="550.0" y="232" font-size="12" text-anchor="middle">10ms</text>
<line x1="640.0" y1="20" x2="640.0" y2="215" stroke="currentColor" stroke-opacity="0.25"/>
<text x="640.0" y="232" font-size="12" text-anchor="middle">100ms</text>
<text x="10" y="42" font-size="12">user uAPI v2</text>
<rect x="190" y="30" width="168.8" height="16" fill="#888" fill-opacity="0.75"/>
<line x1="358.8" y1="38" x2="465.1" y2="38" stroke="#888" stroke-width="2"/>
<line x1="465.1" y1="32" x2="465.1" y2="44" stroke="#888" stroke-width="2"/>
<text x="471.1" y="43" font-size="12">75 / 1139</text>
<text x="10" y="88" font-size="12">user + tracepoint</text>
<rect x="190" y="76" width="184.1" height="16" fill="#d0564a" fill-opacity="0.75"/>
<line x1="374.1" y1="84" x2="552.4" y2="84" stroke="#d0564a" stroke-width="2"/>
<line x1="552.4" y1="78" x2="552.4" y2="90" stroke="#d0564a" stroke-width="2"/>
<text x="558.4" y="89" font-size="12">111 / 10645</text>
<text x="10" y="134" font-size="12">kernel gpiod_set_value</text>
<rect x="190" y="122" width="44.2" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
<line x1="234.2" y1="130" x2="331.1" y2="130" stroke="#4a7bd0" stroke-width="2"/>
<line x1="331.1" y1="124" x2="331.1" y2="136" stroke="#4a7bd0" stroke-width="2"/>
<text x="337.1" y="135" font-size="12">3.1 / 37</text>
<text x="10" y="180" font-size="12">kernel raw writeb</text>
<rect x="190" y="168" width="41.6" height="16" fill="#3f9a6b" fill-opacity="0.75"/>
<line x1="231.6" y1="176" x2="330.1" y2="176" stroke="#3f9a6b" stroke-width="2"/>
<line x1="330.1" y1="170" x2="330.1" y2="182" stroke="#3f9a6b" stroke-width="2"/>
<text x="336.1" y="181" font-size="12">2.9 / 36</text>
<text x="10" y="258" font-size="12">edge-to-edge interval seen by the "LA" (QEMU log): bar = p50, whisker = p99, µs</text>
</svg>
```

그림 2 — 같은 핀을 네 가지 방법으로 토글했을 때 "LA"(QEMU 로그)가 본 엣지 간격. 로그 눈금. 유저 공간은 중앙값부터 커널보다 20배 이상 느리고, p99가 ms 단위로 튄다. tracepoint를 켜면 유저 쪽 p99가 10배 나빠졌다.

출력에서 볼 것:

- **유저 vs 커널**: 중앙값 약 75 µs vs 3 µs. 유저 토글은 매번 syscall(SVC 예외 → 커널 진입)과 gpiolib-cdev 경로를 지난다. 그리고 p99가 0.6~2 ms, max가 22 ms — 유저 프로세스는 언제든 선점될 수 있다. **유저 공간 GPIO 토글은 "대략 언제"를 보는 용도지, µs 지연 측정 도구가 아니다.**
- **gpiod vs writeb가 거의 같다(3.1 vs 2.9 µs)**: TCG에서는 MMIO 쓰기 한 번이 "게스트 → QEMU 디바이스 모델 호출"이라 그 자체가 µs 단위로 비싸서, gpiolib 오버헤드(수백 ns)가 묻힌다. **TCG 함정이다.** 실보드에서는 MMIO 쓰기가 버스 지연(수십~수백 ns)이고 gpiolib 경로가 그와 비슷하거나 더 커서 차이가 드러난다 — 실보드에서 직접 재 볼 숙제다(§12).
- **게스트 안 측정과 LA 측정이 다르다**: 커널 안에서 잰 값(gpiod 5~11 µs/토글)은 LA가 본 중앙값(3.1 µs)보다 크다. 안에서 잰 값은 **평균**이라 가끔 있는 긴 간격(TCG 번역, 호스트 선점 — p99/max 참고)이 다 들어가고, LA 중앙값은 그걸 빼고 본다. "평균만 보고하지 말고 분포를 봐라"의 실례다.
- **관찰자 효과**: tracepoint를 켜자 유저 토글의 중앙값이 75 → 111 µs, p99가 1.1 → 10.6 ms로 나빠졌다. 트레이싱 자체가 시스템을 바꾼다. 디버그 핀(MMIO 한 번)은 트레이스보다 훨씬 가벼운 관측 수단이다.
- `local_irq_save()` 안에서도 max가 수 ms인 구간이 있다(커널 gpiod run2 max 5986 µs). 게스트 IRQ를 꺼도 **호스트가 vCPU 스레드를 재우면** 게스트는 알 수 없다. 실보드의 SMI(System Management Interrupt)나 하이퍼바이저 steal time과 같은 종류 — 커널 트레이스로는 안 보이고 핀으로만 보인다(N1).

### 2.5 예제 5 — MMIO 직접 쓰기는 ftrace에 안 보인다 (그리고 hist trigger)

무엇을 확인하나: `gpio_value` 이벤트를 켜고 `m11_rate`를 한 번 돌린 뒤, ftrace가 본 line 7 이벤트 수와 QEMU가 본 엣지 수를 비교한다. 덤으로 hist trigger로 이벤트를 (gpio, value)별로 센다.

```sh
#!/bin/sh
# g_vis.sh (게스트)
T=/sys/kernel/tracing
mount -t tracefs none $T 2>/dev/null
echo 'hist:keys=gpio,value' > $T/events/gpio/gpio_value/trigger
echo 1 > $T/events/gpio/gpio_value/enable
insmod /m11_rate.ko 2>/dev/null; dmesg | grep "m11_rate: gpiod"
echo "gpio_value events for gpio 519 (line 7): $(grep -c 'gpio_value: 519 set' $T/trace)"
grep -v "^#" $T/events/gpio/gpio_value/hist | grep -v "^$"
echo "--- bus/regmap event groups"
for g in i2c smbus spi regmap gpio irq; do printf "%-7s " $g; ls $T/events/$g 2>/dev/null | grep -v "enable\|filter" | tr "\n" " "; echo; done
```

```text
[    9.994456][   T78] m11_rate: gpiod_set_value 15079 ns/toggle, raw writeb 3172 ns/toggle (n=2000)
gpio_value events for gpio 519 (line 7): 2001
{ gpio:        519, value:          1 } hitcount:       1000
{ gpio:        519, value:          0 } hitcount:       1001
Totals:
    Hits: 2001
    Entries: 2
    Dropped: 0
--- bus/regmap event groups
i2c     i2c_read i2c_reply i2c_result i2c_write 
smbus   smbus_read smbus_reply smbus_result smbus_write 
spi     spi_controller_busy spi_controller_idle spi_message_done spi_message_start spi_message_submit spi_set_cs spi_setup spi_transfer_start spi_transfer_stop 
regmap  regcache_drop_region regcache_sync regmap_async_complete_done regmap_async_complete_start regmap_async_io_complete regmap_async_write_start regmap_bulk_read regmap_bulk_write regmap_cache_bypass regmap_cache_only regmap_hw_read_done regmap_hw
gpio    gpio_direction gpio_value 
irq     irq_handler_entry irq_handler_exit softirq_entry softirq_exit softirq_raise tasklet_entry tasklet_exit 
```

같은 실행의 QEMU 로그에서 line 7 엣지 수를 세면:

```sh
grep -c "output 7" /var/tmp/M11/r_vis.qlog
```

```text
4000
```

출력에서 볼 것:

- ftrace는 2001건(= `GPIOD_OUT_LOW`로 방향 잡을 때 1건 + gpiod 루프 2000건), "LA"는 4000건(초기화 0 쓰기는 값이 안 바뀌어 기록 안 됨 → gpiod 1999 + writeb 2000 + 마지막 정리 1). **writeb 2000번은 커널 트레이스에 흔적이 없다.** 반대로 말하면, 드라이버가 레지스터를 직접 치는 경로(regmap 없이 `writel`)는 핀이나 MMIO 트레이스(x86의 mmiotrace — arm64 랩 커널엔 없음)로만 보인다.
- tracepoint를 켜자 gpiod 토글 비용이 15079 ns로, raw(3172 ns)의 약 5배가 됐다. 예제 4의 tracepoint 꺼진 상태(5~11 µs)보다 크다 — 이벤트 하나를 링 버퍼에 쓰는 비용 + hist trigger 집계 비용.
- hist trigger(`hist:keys=gpio,value`)는 이벤트를 버퍼에 쌓지 않고 커널 안에서 바로 집계한다. 장시간 "핀이 몇 번 토글됐나"를 셀 때 쓸 수 있다(M5).
- regmap 줄은 출력 폭 때문에 `regmap_hw`에서 잘렸다(원래 출력에서 `cut -c1-250`).

---

## 3. "디버그 GPIO" 디자인 패턴 — 핀 몇 개로 상태 머신을 LA에 띄우기

### 3.1 하드웨어 쪽 준비 (보드 설계 단계에서)

- **스페어 핀 + 테스트 포인트**: SoC의 남는 GPIO 2~4개를 TP(테스트 패드)나 헤더로 빼 둔다. 리뷰에서 "디버그 핀은 왜?"라고 물으면 "양산 후 현장 지연 문제를 핀으로 증명하려고"라고 답한다. Don이 RF 보드에서 하던 그 습관이다.
- **SoC 내장 GPIO에 둔다**: I2C/SPI 확장기 핀은 hardirq에서 못 쓰고, 토글 자체가 버스 트랜잭션이라 수십~수백 µs가 걸린다.
- **같은 전압 도메인·같은 GPIO 뱅크**: 여러 핀을 한 레지스터 쓰기로 동시에 바꿀 수 있어야 "병렬 상태 버스"가 된다(아래 3.2 ③).
- **pinctrl에서 GPIO 기능으로 먹스되어 있는지** DT에서 확인(H4 §2). 다른 기능(UART TX 등)과 공유된 패드면 부팅 중 글리치가 난다.

### 3.2 소프트웨어 쪽 인코딩

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 380">
<text x="10" y="22" font-size="13">① 펄스 폭 = 구간 길이 (진입 시 1, 종료 시 0)</text>
<text x="10" y="56" font-size="12">DBG0</text>
<path d="M60,60 L120,60 L120,36 L300,36 L300,60 L380,60 L380,36 L440,36 L440,60 L640,60" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<text x="210" y="76" font-size="12" text-anchor="middle">handler 180 µs</text>
<text x="410" y="76" font-size="12" text-anchor="middle">60 µs</text>
<text x="10" y="110" font-size="13">② 펄스 개수 = 상태 코드 (에러 3 = 펄스 3개)</text>
<text x="10" y="144" font-size="12">DBG1</text>
<path d="M60,148 L120,148 L120,124 L135,124 L135,148 L150,148 L150,124 L165,124 L165,148 L180,148 L180,124 L195,124 L195,148 L400,148 L400,124 L415,124 L415,148 L640,148" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="157" y="164" font-size="12" text-anchor="middle">state 3</text>
<text x="407" y="164" font-size="12" text-anchor="middle">state 1</text>
<text x="10" y="198" font-size="13">③ 병렬 상태 버스 (핀 3개 = 상태 8개, 한 레지스터 쓰기로 동시 변경)</text>
<text x="10" y="232" font-size="12">S0</text>
<path d="M60,236 L160,236 L160,216 L260,216 L260,236 L360,236 L360,216 L640,216" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<text x="10" y="266" font-size="12">S1</text>
<path d="M60,270 L260,270 L260,250 L460,250 L460,270 L640,270" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<text x="10" y="300" font-size="12">S2</text>
<path d="M60,304 L460,304 L460,284 L640,284" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<text x="110" y="326" font-size="12" text-anchor="middle">IDLE 0</text>
<text x="210" y="326" font-size="12" text-anchor="middle">IRQ 1</text>
<text x="310" y="326" font-size="12" text-anchor="middle">XFER 2</text>
<text x="410" y="326" font-size="12" text-anchor="middle">PROC 3</text>
<text x="550" y="326" font-size="12" text-anchor="middle">USER 5</text>
<text x="10" y="360" font-size="12">LA에서 S2..S0을 "bus"로 묶으면 상태 번호가 숫자로 보인다 (Saleae, PulseView)</text>
</svg>
```

그림 3 — 디버그 GPIO 인코딩 세 가지. ① 구간 길이를 펄스 폭으로, ② 상태·에러 코드를 펄스 개수로, ③ 핀 2~4개를 묶어 상태 번호를 병렬로 내보낸다.

| 패턴 | 장점 | 단점 / 주의 |
|---|---|---|
| ① 진입 1 / 종료 0 | 구간 길이를 scope의 pulse width 측정으로 바로 통계(mean, max, 히스토그램) | 중첩·재진입하면 폭이 엉킨다. 핀 하나에 구간 하나 |
| 짧은 펄스(진입 시 1→0) | 이 노트의 m11_lat 방식. 여러 단계를 각자 핀으로 | 폭이 너무 좁으면 저속 LA가 놓친다 → 샘플률 ≥ 펄스 폭의 역수 x 4 |
| ② 펄스 개수 | 핀 하나로 에러 코드·분기 표시 | 펄스를 내는 시간이 실행 시간을 늘린다(관찰자 효과) |
| ③ 병렬 버스 | 상태 머신 전이를 한 화면에. 엣지 하나 = 전이 하나 | 같은 뱅크·같은 레지스터 한 번 쓰기로 바꿔야 중간 상태(글리치)가 안 보인다 |
| 토글(반전)만 | 이벤트마다 엣지 1개 — 가장 싸다 | 이벤트 사이 간격만 보이고 길이는 안 보인다 |

커널에서 병렬 버스는 `gpiod_set_array_value()`로 여러 핀을 한 번에 쓸 수 있다. gpiolib은 같은 칩의 핀이면 드라이버의 `.set_multiple` 콜백으로 **한 번의 레지스터 쓰기**로 묶어 준다 — 단, 칩 드라이버가 `set_multiple`을 구현했을 때만이다. 랩의 PL061 드라이버(`drivers/gpio/gpio-pl061.c`)는 `.set`만 채우고 `set_multiple`은 없어서, gpiolib이 핀마다 `.set`을 따로 부른다(핀 사이에 수십 ns~µs 틈 = 중간 상태 글리치). 확실히 하려면 MMIO 직접 쓰기다.

### 3.3 코드 위생

- 디버그 핀 코드는 `#ifdef CONFIG_<드라이버>_DEBUG_GPIO` 또는 모듈 파라미터로 켜고 끈다. DT에 `debug-gpios`가 없으면 `devm_gpiod_get_optional()`이 NULL을 주고, `gpiod_set_value(NULL, ...)`은 아무것도 안 한다 — 옵션 핀 처리가 공짜다.
- hardirq·spinlock 안에서는 `gpiod_set_value()`(non-sleeping 칩 전제), 프로세스 컨텍스트에서는 `_cansleep()`이 정석이다(H4 §5.5).
- 디버그 핀을 양산 펌웨어에 남긴다면 "기본 OFF"로. 핀 하나가 수십 ns~µs를 더한다.

---

## 4. IRQ 지연을 단계별로 재기 — 외부 신호에서 유저 복귀까지

### 4.1 직관 — 단계마다 핀 하나

센서가 DRDY를 올린 순간부터 유저 앱이 데이터를 손에 쥘 때까지 Linux에서는 이런 계단을 내려온다(F6, F7, F3).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 312">
<defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="10" y="30" width="100" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="60" y="54" font-size="13" text-anchor="middle">외부 신호</text>
<text x="60" y="74" font-size="12" text-anchor="middle">DRDY 엣지</text>
<rect x="140" y="30" width="110" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="195" y="54" font-size="13" text-anchor="middle">hardirq</text>
<text x="195" y="74" font-size="12" text-anchor="middle">top half</text>
<rect x="280" y="30" width="110" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="335" y="54" font-size="13" text-anchor="middle">irq thread</text>
<text x="335" y="74" font-size="12" text-anchor="middle">SCHED_FIFO 50</text>
<rect x="420" y="30" width="110" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="475" y="54" font-size="13" text-anchor="middle">workqueue</text>
<text x="475" y="74" font-size="12" text-anchor="middle">kworker</text>
<rect x="560" y="30" width="110" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="615" y="54" font-size="13" text-anchor="middle">유저 read()</text>
<text x="615" y="74" font-size="12" text-anchor="middle">복귀</text>
<line x1="110" y1="58" x2="138" y2="58" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="250" y1="58" x2="278" y2="58" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="390" y1="58" x2="418" y2="58" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="530" y1="58" x2="558" y2="58" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="10" y="120" font-size="12">지연원:</text>
<text x="124" y="120" font-size="12">GIC 전달, IRQ off 구간</text>
<text x="280" y="140" font-size="12">wakeup + 스케줄링</text>
<text x="420" y="120" font-size="12">kworker 깨우기</text>
<text x="560" y="140" font-size="12">CFS 선점, 캐시</text>
<text x="10" y="180" font-size="12">EXT</text>
<path d="M60,186 L70,186 L70,168 L80,168 L80,186 L660,186" fill="none" stroke="#d0564a" stroke-width="2"/>
<text x="10" y="210" font-size="12">HARD</text>
<path d="M60,216 L180,216 L180,198 L186,198 L186,216 L660,216" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="10" y="240" font-size="12">THREAD</text>
<path d="M60,246 L330,246 L330,228 L345,228 L345,246 L660,246" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<text x="10" y="270" font-size="12">WORK</text>
<path d="M60,276 L470,276 L470,258 L485,258 L485,276 L660,276" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<text x="10" y="296" font-size="12">USER</text>
<path d="M60,300 L610,300 L610,284 L625,284 L625,300 L660,300" fill="none" stroke="#888" stroke-width="2"/>
</svg>
```

그림 4 — 지연 단계 사슬. 각 단계의 첫 줄에서 자기 핀에 짧은 펄스를 내면, LA 화면에서 엣지 사이 간격이 곧 단계별 지연이다.

bare-metal에서는 "ISR 첫 줄 핀 토글 → 메인 루프에서 처리 시작 핀 토글" 두 개면 됐다. Linux에서는 단계가 4~5개이고 각각 다른 스케줄링 규칙을 따른다(hardirq는 즉시, irq thread는 SCHED_FIFO 50, kworker는 보통 SCHED_NORMAL — highpri 풀은 nice -20, 유저 앱은 CFS). 핀 하나로는 "어느 단계가 범인인지" 모른다.

### 4.2 예제 6 — 측정 모듈 `m11_lat` (세 조각)

무엇을 확인하나: hrtimer가 "외부 신호"를 흉내 내 EXT 핀에 펄스를 내고 gpio-sim IRQ를 pending으로 만든다. 그 IRQ의 top half, thread, workqueue, 그리고 깨어난 유저가 각자 핀에 펄스를 낸다.

```c
/* m11_lat.c (1/3) — 디버그 핀 4개 + "외부 신호" hrtimer */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/workqueue.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/fs.h>

static unsigned int period_us = 3000, count = 40;
module_param(period_us, uint, 0444);
module_param(count, uint, 0444);

enum { P_EXT, P_HARD, P_THREAD, P_WORK, NPIN };
static struct gpio_desc *pin[NPIN], *src;	/* pin[]: PL061 출력, src: gpio-sim 입력 */
static struct platform_device *pdev;
static struct hrtimer tmr;
static struct work_struct wk;
static DECLARE_WAIT_QUEUE_HEAD(wq);
static atomic_t ready = ATOMIC_INIT(0);
static unsigned int fired;
static int irq;

#define PL "9030000.pl061"
static struct gpiod_lookup_table lt = {
	.dev_id = "m11_lat",
	.table = {
		GPIO_LOOKUP_IDX(PL, 0, "dbg", P_EXT, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(PL, 1, "dbg", P_HARD, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(PL, 2, "dbg", P_THREAD, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(PL, 4, "dbg", P_WORK, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("m11_sim", 0, "src", GPIO_ACTIVE_HIGH),
		{ }
	},
};

/* 외부 센서의 DRDY 핀 역할: 엣지를 내고, gpio-sim IRQ를 pending으로 만든다 */
static enum hrtimer_restart tmr_fn(struct hrtimer *t)
{
	gpiod_set_value(pin[P_EXT], 1);
	irq_set_irqchip_state(irq, IRQCHIP_STATE_PENDING, true);
	gpiod_set_value(pin[P_EXT], 0);
	if (++fired >= count)
		return HRTIMER_NORESTART;
	hrtimer_forward_now(t, us_to_ktime(period_us));
	return HRTIMER_RESTART;
}
```

- `irq_set_irqchip_state(irq, IRQCHIP_STATE_PENDING, true)`: IRQ 칩에게 "이 IRQ가 pending이다"라고 알리는 범용 API. gpio-sim이 `pull` 파일에 쓰일 때 내부에서 부르는 것과 같은 함수다(`gpio_sim_apply_pull()`). irq_sim은 이걸 받아 `irq_work_queue()` → (arm64에서는 자기 CPU에 IPI) → hardirq 문맥에서 핸들러를 부른다. 실보드의 "핀 → GPIO 컨트롤러 → GIC → CPU" 구간을 "irq_work IPI"가 대신하는 셈이다.
- 랩 소스(`kernel/irq/irq_sim.c`)를 보면 `irq_sim_set_irqchip_state()`는 IRQ가 **enabled일 때만** pending 비트를 세운다. 마스크된 동안 온 엣지는 그냥 버려진다 — §4.4의 유실 원인 중 하나.

```c
/* m11_lat.c (2/3) — 단계마다 핀 하나: hardirq → threaded → workqueue → 유저 */
static irqreturn_t top(int irq, void *d)
{
	gpiod_set_value(pin[P_HARD], 1);	/* PL061 = MMIO, 안 잠듦 → hardirq OK */
	gpiod_set_value(pin[P_HARD], 0);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t thread_fn(int irq, void *d)
{
	gpiod_set_value(pin[P_THREAD], 1);
	queue_work(system_highpri_wq, &wk);
	gpiod_set_value(pin[P_THREAD], 0);
	return IRQ_HANDLED;
}

static void wk_fn(struct work_struct *w)
{
	gpiod_set_value(pin[P_WORK], 1);
	atomic_set(&ready, 1);
	wake_up_interruptible(&wq);
	gpiod_set_value(pin[P_WORK], 0);
}

/* read(): 다음 샘플까지 잠든다(최대 1초).  write(): 측정 시작 */
static ssize_t m11_read(struct file *f, char __user *b, size_t n, loff_t *o)
{
	long r = wait_event_interruptible_timeout(wq, atomic_xchg(&ready, 0), HZ);

	if (r < 0)
		return r;
	return r ? 0 : -ETIMEDOUT;	/* 1초 동안 샘플 없음 = 측정 끝 (유실 샘플이 있어도 안 멈춤) */
}

static ssize_t m11_write(struct file *f, const char __user *b, size_t n, loff_t *o)
{
	fired = 0;
	hrtimer_start(&tmr, us_to_ktime(period_us), HRTIMER_MODE_REL);
	return n;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE, .read = m11_read, .write = m11_write,
};
static struct miscdevice mdev = {
	.minor = MISC_DYNAMIC_MINOR, .name = "m11_lat", .fops = &fops,
};
```

```c
/* m11_lat.c (3/3) — 핀 얻기, IRQ 요청, 정리 */
static int __init m11_init(void)
{
	int i, ret;

	gpiod_add_lookup_table(&lt);
	pdev = platform_device_register_simple("m11_lat", -1, NULL, 0);
	if (IS_ERR(pdev)) { ret = PTR_ERR(pdev); goto err_lt; }
	for (i = 0; i < NPIN; i++) {
		pin[i] = devm_gpiod_get_index(&pdev->dev, "dbg", i, GPIOD_OUT_LOW);
		if (IS_ERR(pin[i])) { ret = PTR_ERR(pin[i]); goto err_pd; }
		gpiod_set_consumer_name(pin[i], "m11_dbg");
	}
	src = devm_gpiod_get(&pdev->dev, "src", GPIOD_IN);
	if (IS_ERR(src)) { ret = PTR_ERR(src); goto err_pd; }
	irq = gpiod_to_irq(src);
	if (irq < 0) { ret = irq; goto err_pd; }
	ret = request_threaded_irq(irq, top, thread_fn, IRQF_ONESHOT | IRQF_TRIGGER_RISING,
				   "m11_lat", NULL);
	if (ret) goto err_pd;
	INIT_WORK(&wk, wk_fn);
	hrtimer_setup(&tmr, tmr_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ret = misc_register(&mdev);
	if (ret) goto err_irq;
	pr_info("m11_lat: irq=%d gpio ext=%d hard=%d thread=%d work=%d period=%uus\n", irq,
		desc_to_gpio(pin[0]), desc_to_gpio(pin[1]), desc_to_gpio(pin[2]),
		desc_to_gpio(pin[3]), period_us);
	return 0;
err_irq:
	free_irq(irq, NULL);
err_pd:
	platform_device_unregister(pdev);
err_lt:
	gpiod_remove_lookup_table(&lt);
	return ret;
}

static void __exit m11_exit(void)
{
	misc_deregister(&mdev);
	hrtimer_cancel(&tmr);
	free_irq(irq, NULL);
	cancel_work_sync(&wk);
	platform_device_unregister(pdev);
	gpiod_remove_lookup_table(&lt);
}
module_init(m11_init);
module_exit(m11_exit);
MODULE_DESCRIPTION("M11: debug-GPIO latency stages");
MODULE_LICENSE("GPL");
```

- `hrtimer_setup()`은 최근 커널에 들어온 API다(예전 `hrtimer_init()` + `.function` 대입을 한 번에 한다). 랩 6.18 커널에는 `hrtimer_init()`이 더 이상 없어서 이것을 썼다 — 오래된 BSP 커널(5.x)에서는 `hrtimer_init()`을 쓴다.
- `devm_gpiod_get_index()`의 devm 자원은 platform device가 **해제될 때** 풀린다. 드라이버 없이 만든 장치라 `platform_device_unregister()`가 그 시점이다.
- `desc_to_gpio()`는 전역 GPIO 번호를 주는 레거시 함수다. 여기서는 ftrace `gpio_value` 이벤트가 전역 번호(512 + 라인)로 찍히기 때문에 대응표를 남기려고 썼다.

빌드: 세 조각을 이어 붙여 `m11_lat.c`로 만들고 `make -C /var/lab/out/linux-build-trace M=$PWD` — 경고 0개. 실행: idle 상태(`N=200 PER=5000`)와 부하 상태(`LOAD=1`, 백그라운드 `dd` 루프 2개)를 각각 한 번.

```sh
TMO=280 sh q.sh /var/lab/out/Image-trace lat_idle "N=200~PER=5000~sh~/g_lat.sh"
TMO=280 sh q.sh /var/lab/out/Image-trace lat_load "LOAD=1~N=200~PER=5000~sh~/g_lat.sh"
```

### 4.3 예제 7 — 샘플 하나를 ftrace와 "LA"로 동시에 보기

무엇을 확인하나: idle 실행에서 EXT→USER가 가장 짧았던 샘플 하나. 게스트 ftrace(가상 LA + 원인 정보)와 QEMU 로그(외부 LA)를 나란히 놓는다.

```sh
awk '$4+0>=28.4070 && $4+0<=28.4090' /var/tmp/M11/lat_idle.trace | cut -c1-150
```

```text
          <idle>-0       [001] d.h1.    28.407428: irq_handler_entry: irq=11 name=arch_timer
          <idle>-0       [001] d.h1.    28.407443: hrtimer_expire_entry: hrtimer=000000007f02e364 function=tmr_fn [m11_lat] now=28407436448
          <idle>-0       [001] d.h1.    28.407449: gpio_value: 512 set 1
          <idle>-0       [001] d.h1.    28.407482: gpio_value: 512 set 0
          <idle>-0       [001] d.h1.    28.407512: irq_handler_exit: irq=11 ret=handled
          <idle>-0       [001] d.h1.    28.407523: irq_handler_entry: irq=6 name=IPI
          <idle>-0       [001] d.h1.    28.407535: irq_handler_entry: irq=23 name=m11_lat
          <idle>-0       [001] d.h1.    28.407538: gpio_value: 513 set 1
          <idle>-0       [001] d.h1.    28.407545: gpio_value: 513 set 0
          <idle>-0       [001] d.h1.    28.407549: irq_handler_exit: irq=23 ret=handled
          <idle>-0       [001] d.h1.    28.407582: irq_handler_exit: irq=6 ret=handled
          <idle>-0       [000] d.h1.    28.407685: irq_handler_entry: irq=2 name=IPI
          <idle>-0       [000] dNh2.    28.407722: sched_wakeup: comm=irq/23-m11_lat pid=83 prio=49 target_cpu=000
          <idle>-0       [000] dNh1.    28.407730: irq_handler_exit: irq=2 ret=handled
          <idle>-0       [000] d..2.    28.407798: sched_switch: prev_comm=swapper/0 prev_pid=0 prev_prio=120 prev_state=R ==> next_comm=irq/23-m11_la
  irq/23-m11_lat-83      [000] .....    28.407821: gpio_value: 514 set 1
  irq/23-m11_lat-83      [000] d..4.    28.407875: sched_wakeup: comm=kworker/0:1H pid=59 prio=100 target_cpu=000
  irq/23-m11_lat-83      [000] .....    28.407886: gpio_value: 514 set 0
  irq/23-m11_lat-83      [000] d..2.    28.407951: sched_switch: prev_comm=irq/23-m11_lat prev_pid=83 prev_prio=49 prev_state=S ==> next_comm=kworker/
    kworker/0:1H-59      [000] .....    28.407977: workqueue_execute_start: work struct 00000000c66a9098: function wk_fn [m11_lat]
    kworker/0:1H-59      [000] .....    28.407980: gpio_value: 516 set 1
    kworker/0:1H-59      [000] d..4.    28.408018: sched_wakeup: comm=m11_user pid=87 prio=120 target_cpu=000
    kworker/0:1H-59      [000] .....    28.408026: gpio_value: 516 set 0
    kworker/0:1H-59      [000] d..2.    28.408065: sched_switch: prev_comm=kworker/0:1H prev_pid=59 prev_prio=100 prev_state=I ==> next_comm=m11_user 
        m11_user-87      [000] .....    28.408185: gpio_value: 517 set 1
        m11_user-87      [000] .....    28.408267: gpio_value: 517 set 0
        m11_user-87      [000] d..2.    28.408496: sched_switch: prev_comm=m11_user prev_pid=87 prev_prio=120 prev_state=S ==> next_comm=swapper/0 nex
```

같은 사건을 QEMU 로그(외부 관찰자)에서:

```sh
sed -n 1253,1262p /var/tmp/M11/lat_idle.qlog
```

```text
2026-10-18T06:42:34.192179Z pl061_set_output /machine/unattached/device[8] setting output 0 to 1
2026-10-18T06:42:34.192215Z pl061_set_output /machine/unattached/device[8] setting output 0 to 0
2026-10-18T06:42:34.192266Z pl061_set_output /machine/unattached/device[8] setting output 1 to 1
2026-10-18T06:42:34.192272Z pl061_set_output /machine/unattached/device[8] setting output 1 to 0
2026-10-18T06:42:34.192553Z pl061_set_output /machine/unattached/device[8] setting output 2 to 1
2026-10-18T06:42:34.192614Z pl061_set_output /machine/unattached/device[8] setting output 2 to 0
2026-10-18T06:42:34.192709Z pl061_set_output /machine/unattached/device[8] setting output 4 to 1
2026-10-18T06:42:34.192755Z pl061_set_output /machine/unattached/device[8] setting output 4 to 0
2026-10-18T06:42:34.192922Z pl061_set_output /machine/unattached/device[8] setting output 5 to 1
2026-10-18T06:42:34.193000Z pl061_set_output /machine/unattached/device[8] setting output 5 to 0
```

출력에서 볼 것 — ftrace가 "왜"를 채워 준다:

- EXT(512)는 CPU1의 타이머 IRQ(irq 11) 안 hrtimer 콜백에서 올랐다. 바로 뒤 irq 6(IPI) 안에서 irq 23(`m11_lat`)이 돈다 — irq_work를 위한 자기 자신 IPI다. EXT → HARD = 89 µs(ftrace) / 87 µs(LA).
- hardirq는 CPU1에서 돌았지만 irq thread `irq/23-m11_lat`(prio=49 = SCHED_FIFO 50)는 **CPU0에서** 깨어났다. CPU0의 idle을 깨우는 IPI(irq 2)가 필요했다. 이 cross-CPU wakeup이 HARD → THREAD 283 µs의 큰 몫이다. 실보드라면 IRQ affinity와 irq thread affinity를 같은 CPU로 묶는 것이 첫 튜닝 포인트다(N2).
- thread → kworker(`kworker/0:1H`, H = highpri 풀, prio=100 = nice -20) → `m11_user`(prio=120) 순서가 `sched_wakeup`/`sched_switch`로 그대로 보인다.
- ftrace의 EXT→USER = 28.408185 − 28.407449 = 736 µs, LA = 0.192922 − 0.192179 = 743 µs. 같은 사건을 두 관찰자가 7 µs 차이로 본다. 이 차이가 무엇인지가 §5의 주제다.

### 4.4 예제 8 — 분포와 유실: 핀 개수를 세면 어디서 사라졌는지 보인다

무엇을 확인하나: 200개 샘플 전체를 파싱해 단계별 지연 분포를 구한다. 매칭 규칙은 "EXT 엣지 i 다음에 오는 첫 HARD(다음 EXT 이전), 그 뒤 첫 THREAD, …" 이고, 같은 THREAD/WORK/USER 엣지를 두 샘플이 공유하면(= 합쳐짐) 그 샘플은 통계에서 뺀다. 스크립트는 `ana.py`(약 80줄 Python, ftrace와 QEMU 로그 둘 다 같은 규칙으로 처리).

```sh
python3 -I ana.py lat_idle ; python3 -I ana.py lat_load
```

```text
#### lat_idle
counts ftrace {'EXT': 200, 'HARD': 198, 'THREAD': 174, 'WORK': 161, 'USER': 147}
counts qemu  {'EXT': 200, 'HARD': 198, 'THREAD': 174, 'WORK': 161, 'USER': 147}
ftrace complete chains = 139
  EXT->HARD     p50=  226.0 p90= 1537.0 p99=36510.0 max=39529.0 us
  HARD->THREAD  p50=  689.0 p90= 5166.0 p99=25768.0 max=49574.0 us
  THREAD->WORK  p50=  486.0 p90= 3814.0 p99=29910.0 max=31725.0 us
  WORK->USER    p50=  859.0 p90= 8684.0 p99=45165.0 max=104423.0 us
  EXT->USER     p50= 4302.0 p90=22867.0 p99=66248.0 max=107476.0 us
qemu complete chains = 137
  EXT->HARD     p50=  216.0 p90= 1505.1 p99=34049.0 max=39528.8 us
  HARD->THREAD  p50=  694.0 p90= 5174.9 p99=25775.9 max=49584.2 us
  THREAD->WORK  p50=  479.9 p90= 3821.1 p99=29901.0 max=31724.9 us
  WORK->USER    p50=  949.1 p90= 8747.1 p99=45193.9 max=104430.0 us
  EXT->USER     p50= 4540.2 p90=22879.1 p99=66264.9 max=107479.1 us
#### lat_load
counts ftrace {'EXT': 200, 'HARD': 198, 'THREAD': 156, 'WORK': 144, 'USER': 108}
counts qemu  {'EXT': 200, 'HARD': 198, 'THREAD': 156, 'WORK': 144, 'USER': 108}
ftrace complete chains = 103
  EXT->HARD     p50=  265.0 p90= 3674.0 p99= 7331.0 max= 9379.0 us
  HARD->THREAD  p50= 2343.0 p90= 5229.0 p99=20293.0 max=21205.0 us
  THREAD->WORK  p50=  371.0 p90= 4313.0 p99=13181.0 max=16209.0 us
  WORK->USER    p50= 2884.0 p90=14101.0 p99=56798.0 max=62876.0 us
  EXT->USER     p50= 7557.0 p90=28918.0 p99=58726.0 max=72675.0 us
qemu complete chains = 103
  EXT->HARD     p50=  267.0 p90= 3686.0 p99= 7262.0 max= 9371.0 us
  HARD->THREAD  p50= 2350.8 p90= 5245.0 p99=20304.9 max=21226.9 us
  THREAD->WORK  p50=  372.2 p90= 4841.1 p99=13184.1 max=16207.0 us
  WORK->USER    p50= 2805.9 p90=13595.1 p99=58574.9 max=62896.0 us
  EXT->USER     p50= 7561.0 p90=28936.9 p99=61035.9 max=72697.9 us
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 490">
<text x="10" y="22" font-size="13">(a) fastest sample: EXT→USER = 743 µs (QEMU pl061 log = "LA")</text>
<text x="10" y="46" font-size="12">EXT</text>
<path d="M90,50 L124.1,50 L124.1,32 L144.6,32 L144.6,50 L660,50" fill="none" stroke="#d0564a" stroke-width="2"/>
<path d="M128.1,52 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<text x="10" y="80" font-size="12">HARD</text>
<path d="M90,84 L173.6,84 L173.6,66 L177.0,66 L177.0,84 L660,84" fill="none" stroke="#e08a3c" stroke-width="2"/>
<path d="M178.6,86 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<text x="10" y="114" font-size="12">THREAD</text>
<path d="M90,118 L336.7,118 L336.7,100 L371.3,100 L371.3,118 L660,118" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<path d="M339.4,120 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<text x="10" y="148" font-size="12">WORK</text>
<path d="M90,152 L425.3,152 L425.3,134 L451.4,134 L451.4,152 L660,152" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<path d="M429.7,154 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<text x="10" y="182" font-size="12">USER</text>
<path d="M90,186 L546.4,186 L546.4,168 L590.6,168 L590.6,186 L660,186" fill="none" stroke="#888" stroke-width="2"/>
<path d="M546.2,188 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<line x1="90" y1="212" x2="660" y2="212" stroke="currentColor"/>
<line x1="124.1" y1="212" x2="124.1" y2="217" stroke="currentColor"/>
<text x="124.1" y="230" font-size="12" text-anchor="middle">0</text>
<line x1="180.9" y1="212" x2="180.9" y2="217" stroke="currentColor"/>
<text x="180.9" y="230" font-size="12" text-anchor="middle">100</text>
<line x1="237.8" y1="212" x2="237.8" y2="217" stroke="currentColor"/>
<text x="237.8" y="230" font-size="12" text-anchor="middle">200</text>
<line x1="294.5" y1="212" x2="294.5" y2="217" stroke="currentColor"/>
<text x="294.5" y="230" font-size="12" text-anchor="middle">300</text>
<line x1="351.4" y1="212" x2="351.4" y2="217" stroke="currentColor"/>
<text x="351.4" y="230" font-size="12" text-anchor="middle">400</text>
<line x1="408.2" y1="212" x2="408.2" y2="217" stroke="currentColor"/>
<text x="408.2" y="230" font-size="12" text-anchor="middle">500</text>
<line x1="465.1" y1="212" x2="465.1" y2="217" stroke="currentColor"/>
<text x="465.1" y="230" font-size="12" text-anchor="middle">600</text>
<line x1="521.8" y1="212" x2="521.8" y2="217" stroke="currentColor"/>
<text x="521.8" y="230" font-size="12" text-anchor="middle">700</text>
<line x1="578.6" y1="212" x2="578.6" y2="217" stroke="currentColor"/>
<text x="578.6" y="230" font-size="12" text-anchor="middle">800</text>
<line x1="635.5" y1="212" x2="635.5" y2="217" stroke="currentColor"/>
<text x="635.5" y="230" font-size="12" text-anchor="middle">900</text>
<text x="660" y="246" font-size="12" text-anchor="end">µs from EXT edge</text>
<text x="10" y="247" font-size="13">(b) median sample: EXT→USER = 4540 µs (QEMU pl061 log = "LA")</text>
<text x="10" y="271" font-size="12">EXT</text>
<path d="M90,275 L97.1,275 L97.1,257 L123.7,257 L123.7,275 L499.3,275 L499.3,257 L515.8,257 L515.8,275 L660,275" fill="none" stroke="#d0564a" stroke-width="2"/>
<path d="M96.2,277 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<path d="M497.8,277 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<text x="10" y="305" font-size="12">HARD</text>
<path d="M90,309 L154.6,309 L154.6,291 L156.8,291 L156.8,309 L534.2,309 L534.2,291 L540.5,291 L540.5,309 L660,309" fill="none" stroke="#e08a3c" stroke-width="2"/>
<path d="M155.3,311 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<path d="M534.9,311 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<text x="10" y="339" font-size="12">THREAD</text>
<path d="M90,343 L261.8,343 L261.8,325 L277.4,325 L277.4,343 L584.2,343 L584.2,325 L596.0,325 L596.0,343 L660,343" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<path d="M262.1,345 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<path d="M584.7,345 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<text x="10" y="373" font-size="12">WORK</text>
<path d="M90,377 L309.9,377 L309.9,359 L316.5,359 L316.5,377 L660,377" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<path d="M310.6,379 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<text x="10" y="407" font-size="12">USER</text>
<path d="M90,411 L636.2,411 L636.2,393 L657.5,393 L657.5,411 L660,411" fill="none" stroke="#888" stroke-width="2"/>
<path d="M636.3,413 l-4,7 l8,0 z" fill="currentColor" fill-opacity="0.6"/>
<line x1="90" y1="437" x2="660" y2="437" stroke="currentColor"/>
<line x1="97.1" y1="437" x2="97.1" y2="442" stroke="currentColor"/>
<text x="97.1" y="455" font-size="12" text-anchor="middle">0</text>
<line x1="215.9" y1="437" x2="215.9" y2="442" stroke="currentColor"/>
<text x="215.9" y="455" font-size="12" text-anchor="middle">1000</text>
<line x1="334.6" y1="437" x2="334.6" y2="442" stroke="currentColor"/>
<text x="334.6" y="455" font-size="12" text-anchor="middle">2000</text>
<line x1="453.4" y1="437" x2="453.4" y2="442" stroke="currentColor"/>
<text x="453.4" y="455" font-size="12" text-anchor="middle">3000</text>
<line x1="572.1" y1="437" x2="572.1" y2="442" stroke="currentColor"/>
<text x="572.1" y="455" font-size="12" text-anchor="middle">4000</text>
<text x="660" y="471" font-size="12" text-anchor="end">µs from EXT edge</text>
</svg>
```

그림 5 — 실제 측정 데이터로 그린 LA 화면. 파형은 QEMU 로그(외부 관찰자)의 엣지, 각 채널 아래 삼각형은 같은 엣지의 ftrace `gpio_value` 시각(§5의 median offset으로 매핑). (a) 가장 빠른 샘플 743 µs, (b) 중앙값 샘플 4540 µs — (b)에서는 THREAD와 USER 사이에 **다음 EXT가 이미 와 버렸다**. 이전 샘플을 처리하는 동안 새 샘플이 도착하는 overrun 상황이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 380">
<line x1="150.0" y1="30" x2="150.0" y2="330" stroke="currentColor" stroke-opacity="0.25"/>
<text x="150.0" y="348" font-size="12" text-anchor="middle">100µs</text>
<line x1="310.0" y1="30" x2="310.0" y2="330" stroke="currentColor" stroke-opacity="0.25"/>
<text x="310.0" y="348" font-size="12" text-anchor="middle">1ms</text>
<line x1="470.0" y1="30" x2="470.0" y2="330" stroke="currentColor" stroke-opacity="0.25"/>
<text x="470.0" y="348" font-size="12" text-anchor="middle">10ms</text>
<line x1="630.0" y1="30" x2="630.0" y2="330" stroke="currentColor" stroke-opacity="0.25"/>
<text x="630.0" y="348" font-size="12" text-anchor="middle">100ms</text>
<text x="10" y="60" font-size="12">EXT→HARD</text>
<rect x="150" y="40" width="53.5" height="14" fill="#4a7bd0" fill-opacity="0.75"/>
<line x1="203.5" y1="47" x2="338.4" y2="47" stroke="#4a7bd0" stroke-width="2"/>
<line x1="338.4" y1="41" x2="338.4" y2="53" stroke="#4a7bd0" stroke-width="2"/>
<text x="344.4" y="52" font-size="12">216 / 1505</text>
<rect x="150" y="60" width="68.2" height="14" fill="#e08a3c" fill-opacity="0.75"/>
<line x1="218.2" y1="67" x2="400.6" y2="67" stroke="#e08a3c" stroke-width="2"/>
<line x1="400.6" y1="61" x2="400.6" y2="73" stroke="#e08a3c" stroke-width="2"/>
<text x="406.6" y="72" font-size="12">267 / 3686</text>
<text x="10" y="118" font-size="12">HARD→THREAD</text>
<rect x="150" y="98" width="134.6" height="14" fill="#4a7bd0" fill-opacity="0.75"/>
<line x1="284.6" y1="105" x2="424.2" y2="105" stroke="#4a7bd0" stroke-width="2"/>
<line x1="424.2" y1="99" x2="424.2" y2="111" stroke="#4a7bd0" stroke-width="2"/>
<text x="430.2" y="110" font-size="12">694 / 5175</text>
<rect x="150" y="118" width="219.4" height="14" fill="#e08a3c" fill-opacity="0.75"/>
<line x1="369.4" y1="125" x2="425.2" y2="125" stroke="#e08a3c" stroke-width="2"/>
<line x1="425.2" y1="119" x2="425.2" y2="131" stroke="#e08a3c" stroke-width="2"/>
<text x="431.2" y="130" font-size="12">2351 / 5245</text>
<text x="10" y="176" font-size="12">THREAD→WORK</text>
<rect x="150" y="156" width="109.0" height="14" fill="#4a7bd0" fill-opacity="0.75"/>
<line x1="259.0" y1="163" x2="403.2" y2="163" stroke="#4a7bd0" stroke-width="2"/>
<line x1="403.2" y1="157" x2="403.2" y2="169" stroke="#4a7bd0" stroke-width="2"/>
<text x="409.2" y="168" font-size="12">480 / 3821</text>
<rect x="150" y="176" width="91.3" height="14" fill="#e08a3c" fill-opacity="0.75"/>
<line x1="241.3" y1="183" x2="419.6" y2="183" stroke="#e08a3c" stroke-width="2"/>
<line x1="419.6" y1="177" x2="419.6" y2="189" stroke="#e08a3c" stroke-width="2"/>
<text x="425.6" y="188" font-size="12">372 / 4841</text>
<text x="10" y="234" font-size="12">WORK→USER</text>
<rect x="150" y="214" width="156.4" height="14" fill="#4a7bd0" fill-opacity="0.75"/>
<line x1="306.4" y1="221" x2="460.7" y2="221" stroke="#4a7bd0" stroke-width="2"/>
<line x1="460.7" y1="215" x2="460.7" y2="227" stroke="#4a7bd0" stroke-width="2"/>
<text x="466.7" y="226" font-size="12">949 / 8747</text>
<rect x="150" y="234" width="231.7" height="14" fill="#e08a3c" fill-opacity="0.75"/>
<line x1="381.7" y1="241" x2="491.3" y2="241" stroke="#e08a3c" stroke-width="2"/>
<line x1="491.3" y1="235" x2="491.3" y2="247" stroke="#e08a3c" stroke-width="2"/>
<text x="497.3" y="246" font-size="12">2806 / 13595</text>
<text x="10" y="292" font-size="12">EXT→USER</text>
<rect x="150" y="272" width="265.1" height="14" fill="#4a7bd0" fill-opacity="0.75"/>
<line x1="415.1" y1="279" x2="527.5" y2="279" stroke="#4a7bd0" stroke-width="2"/>
<line x1="527.5" y1="273" x2="527.5" y2="285" stroke="#4a7bd0" stroke-width="2"/>
<text x="533.5" y="284" font-size="12">4540 / 22879</text>
<rect x="150" y="292" width="300.6" height="14" fill="#e08a3c" fill-opacity="0.75"/>
<line x1="450.6" y1="299" x2="543.8" y2="299" stroke="#e08a3c" stroke-width="2"/>
<line x1="543.8" y1="293" x2="543.8" y2="305" stroke="#e08a3c" stroke-width="2"/>
<text x="549.8" y="304" font-size="12">7561 / 28937</text>
<rect x="150" y="358" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75"/><text x="168" y="369" font-size="12">idle (n=137)</text>
<rect x="270" y="358" width="12" height="12" fill="#e08a3c" fill-opacity="0.75"/><text x="288" y="369" font-size="12">2x dd load (n=103)</text>
<text x="430" y="369" font-size="12">bar = p50, whisker = p90 (µs)</text>
</svg>
```

그림 6 — 단계별 지연(p50 막대, p90 수염), idle vs 부하. 로그 눈금, 단위 µs. 부하를 걸면 HARD→THREAD와 WORK→USER(= 스케줄러가 개입하는 구간)가 3배 늘고, EXT→HARD(= hardirq까지)는 거의 그대로다.

출력에서 볼 것:

- **채널별 엣지 개수가 곧 유실 지도다.** idle: EXT 200 → HARD 198 → THREAD 174 → WORK 161 → USER 147. 각 단계의 "합쳐짐(coalescing)"이 원인이다.
  - EXT → HARD(−2): irq_sim은 IRQ가 마스크된 동안의 pending을 버린다. `IRQF_ONESHOT`이라 thread가 끝날 때까지 마스크 상태다.
  - HARD → THREAD(−24): thread가 아직 실행 중일 때 `IRQ_WAKE_THREAD`가 다시 오면 "한 번 더 돌아라" 플래그만 서고 합쳐진다.
  - THREAD → WORK(−13): `queue_work()`는 이미 대기 중인 work를 다시 넣지 않는다(`WORK_STRUCT_PENDING` 비트).
  - WORK → USER(−14): `ready` 플래그 하나라 유저가 읽기 전에 두 번 set되면 하나로 합쳐진다.
  - 실보드의 "가끔 샘플이 빠진다" 버그가 정확히 이 구조다. LA로 채널별 엣지를 세면 **어느 단계에서 빠지는지** 1분 안에 보인다. 커널 로그로는 "안 일어난 일"이 로그에 안 남으므로 찾기 어렵다.
- **ftrace와 LA의 개수·분포가 거의 같다**: 이 랩에서는 둘 다 같은 PL061 쓰기를 보기 때문이다. 중앙값 차이는 수 µs~수십 µs, 하지만 몇몇 샘플은 ms 단위로 다르다(§5.3).
- **절대값은 TCG + 공용 VM(load 15~20) 수치다.** 실보드(예: Cortex-A53/A72, PREEMPT_RT 아님) 일반적 경험치는 EXT→HARD 수 µs~수십 µs, EXT→유저 복귀 수십~수백 µs 수준이다. 숫자 대신 **방법과 상대 비교**를 가져가자.

### 4.5 "커널이 200 µs 지터를 더한다"를 어떻게 증명하나

면접 단골 질문의 답이 위 실험 구조 그대로다.

1. **센서 쪽 기준 확보**: DRDY 핀 자체(EXT)를 LA 채널 0에 물리고, DRDY 주기의 지터를 먼저 잰다(scope의 period 통계 또는 LA의 엣지 간격 히스토그램). 센서 지터가 수 µs면 센서는 무죄.
2. **단계별 핀**: HARD, THREAD, 유저 복귀에 핀을 하나씩. EXT 기준으로 각 엣지까지의 지연 분포(히스토그램)를 본다. "EXT→HARD는 p99 15 µs로 안정, HARD→USER가 p99 220 µs" 같은 식으로 **어느 구간이 지터를 만드는지** 숫자가 나온다.
3. **원인 짚기**: 지터가 큰 구간의 시간대에 ftrace(`sched_switch`, `irq_handler_entry`, `irqsoff`/`wakeup_rt` tracer, `osnoise`)를 켜서 누가 CPU를 잡고 있었는지 본다(M5, N3). 트레이스와 LA를 §5 방법으로 같은 축에 올린다.
4. **수정 후 재측정**: irq thread를 SCHED_FIFO 우선순위 조정·CPU 고정, `isolcpus`/`nohz_full`, PREEMPT_RT(N1, N2) 후 같은 히스토그램을 다시 뽑아 비교한다.

핵심 문장: "센서 핀과 내 코드의 핀을 **같은 LA 시계**로 재면, 센서와 커널의 기여를 분리할 수 있다. 커널 timestamp만으로는 커널이 자기 자신의 지연을 재는 꼴이다."

---

## 5. ftrace 시계와 LA 시계 맞추기

### 5.1 ftrace의 시계 고르기 — `trace_clock`

ftrace 타임스탬프는 `/sys/kernel/tracing/trace_clock`에서 고른 시계로 찍힌다. 랩 소스 `kernel/trace/trace.c`의 표(`trace_clocks[]`)에 있는 것들이다.

| 이름 | 소스 | 특징 | 언제 쓰나 |
|---|---|---|---|
| `local` (기본) | `sched_clock()` 계열, CPU별 | 가장 빠름. CPU 간 단조 보장 없음. 유저 시계와 0점이 다르다 | 한 CPU 안의 미세 순서 |
| `global` | CPU 간 동기화된 local | 느리지만 CPU 간 순서 보장 | 멀티코어 이벤트 순서 |
| `counter` | 그냥 증가하는 카운터 | 시간 아님, 순서만 | 순서만 필요할 때 |
| `uptime` | jiffies | 해상도 1/HZ | 거의 안 씀 |
| `perf` | perf와 같은 시계 | perf 데이터와 합칠 때 | M6과 같이 볼 때 |
| `mono` | `ktime_get_mono_fast_ns()` | **CLOCK_MONOTONIC과 같은 축** | 유저 로그·다른 도구와 맞출 때 |
| `mono_raw` | `ktime_get_raw_fast_ns()` | NTP 보정 없는 원시 클럭 | 하드웨어 클럭 드리프트를 볼 때 |
| `boot` | `ktime_get_boot_fast_ns()` | MONOTONIC + suspend 시간 | suspend/resume을 넘는 기록(N5) |
| `tai` | `ktime_get_tai_fast_ns()` | 국제원자시, 절대 시각 | PTP로 맞춘 여러 장비와 비교 |

x86에는 `x86-tsc`가 추가로 있고(`ARCH_TRACE_CLOCKS`), arm64 랩 커널에는 위 9개가 전부다.

### 5.2 예제 9 — 시계마다 trace_marker를 찍어 유저 시계와 비교

무엇을 확인하나: 유저 프로그램이 `CLOCK_MONOTONIC`, `CLOCK_MONOTONIC_RAW`, `CLOCK_BOOTTIME`, `CLOCK_REALTIME`을 읽어 그 값을 `trace_marker`에 쓴다. trace_clock을 바꿔 가며 ftrace 타임스탬프가 어느 유저 시계와 같은 축인지 본다.

```c
/* m11_mark: 유저 시계 값을 trace_marker에 써서 ftrace 시각과 나란히 놓는다 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

static long long ns(clockid_t c)
{
	struct timespec t;
	clock_gettime(c, &t);
	return t.tv_sec * 1000000000LL + t.tv_nsec;
}
int main(int argc, char **argv)
{
	char buf[160];
	int fd = open("/sys/kernel/tracing/trace_marker", O_WRONLY);
	if (fd < 0) { perror("trace_marker"); return 1; }
	long long m = ns(CLOCK_MONOTONIC), r = ns(CLOCK_MONOTONIC_RAW);
	long long b = ns(CLOCK_BOOTTIME), w = ns(CLOCK_REALTIME);
	int n = snprintf(buf, sizeof(buf), "M11 %s mono=%lld.%06lld raw=%lld.%06lld boot=%lld.%06lld real=%lld.%06lld",
			 argc > 1 ? argv[1] : "-", m / 1000000000, m / 1000 % 1000000, r / 1000000000,
			 r / 1000 % 1000000, b / 1000000000, b / 1000 % 1000000, w / 1000000000,
			 w / 1000 % 1000000);
	if (write(fd, buf, n) != n) { perror("write"); return 1; }
	return 0;
}
```

```sh
#!/bin/sh
# g_clk.sh (게스트)
T=/sys/kernel/tracing
mount -t tracefs none $T 2>/dev/null
cat $T/trace_clock
for c in local global counter uptime perf mono mono_raw boot tai; do
	echo $c > $T/trace_clock || continue
	echo > $T/trace
	/m11_mark $c
	grep M11 $T/trace | sed 's/^ *//'
done
```

```text
lab initramfs: 6.18.55-trace up 22.87s
[local] global counter uptime perf mono mono_raw boot tai
<...>-80      [000] ...1.    26.217800: tracing_mark_write: M11 local mono=25.070281 raw=25.070415 boot=25.070421 real=1792305749.995599
<...>-83      [000] ...1.    26.485204: tracing_mark_write: M11 global mono=25.341914 raw=25.341919 boot=25.341920 real=1792305750.267094
<...>-86      [001] ...1.            4: tracing_mark_write: M11 counter mono=25.583374 raw=25.583378 boot=25.583379 real=1792305750.508553
<...>-89      [001] ...1.         2578: tracing_mark_write: M11 uptime mono=25.781218 raw=25.781223 boot=25.781224 real=1792305750.706398
<...>-92      [001] ...1.    27.123022: tracing_mark_write: M11 perf mono=25.979737 raw=25.979742 boot=25.979743 real=1792305750.904917
<...>-95      [000] ...1.    26.277182: tracing_mark_write: M11 mono mono=26.277038 raw=26.277046 boot=26.277047 real=1792305751.202221
<...>-98      [001] ...1.    26.673463: tracing_mark_write: M11 mono_raw mono=26.673338 raw=26.673343 boot=26.673344 real=1792305751.598518
<...>-101     [000] ...1.    26.943403: tracing_mark_write: M11 boot mono=26.943088 raw=26.943098 boot=26.943099 real=1792305751.868273
<...>-104     [001] ...1. 1792305752.123436: tracing_mark_write: M11 tai mono=27.198048 raw=27.198056 boot=27.198057 real=1792305752.123231
```

출력에서 볼 것:

- `local`, `global`, `perf`는 유저 `mono`보다 **약 1.14 s 앞서** 있다(26.2178 vs 25.0703). 이 시계들은 `sched_clock()` 기반이라 0점이 CLOCK_MONOTONIC과 다르다. 기본값(`local`)으로 찍은 트레이스를 유저 로그와 그대로 비교하면 1초 넘게 틀린다.
- `mono`·`mono_raw`·`boot`는 유저 값과 0.1~0.3 ms 차이 — 유저가 시계를 읽은 뒤 `write()` syscall이 marker를 기록하기까지 걸린 시간(TCG라 길다)이다. 축은 같다.
- `tai`는 REALTIME 축(1792305752 s ≈ 2026년)이다. TAI는 UTC보다 37초 앞서야 하지만 여기서는 REALTIME과 같은 값이 나왔다 — 게스트에서 TAI offset을 설정하는 데몬(chrony/ptp4l)이 없어서다.
- `counter`와 `uptime`은 시간 단위가 아니다(카운터 값, jiffies).
- **규칙: 다른 것과 맞출 트레이스는 `echo mono > trace_clock`으로 찍는다.** 이 노트의 모든 실험이 그렇게 했다. suspend를 넘는다면 `boot`.

### 5.3 예제 10 — sync 엣지 하나 vs 여러 엣지의 중앙값

LA와 커널은 서로 다른 시계(LA 샘플 클럭 vs SoC 타이머)를 쓴다. 맞추는 방법은 bare-metal에서 Don이 했던 것과 같다: **양쪽이 모두 기록하는 사건**(sync 엣지)을 만든다. 이 노트에서는 EXT 핀이 그 역할이다 — ftrace `gpio_value: 512 set 1`과 QEMU `setting output 0 to 1`이 같은 사건이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 260">
<defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="10" y="40" font-size="12">ftrace (mono)</text>
<line x1="120" y1="36" x2="640" y2="36" stroke="#4a7bd0" stroke-width="2"/>
<text x="10" y="150" font-size="12">LA / QEMU (UTC)</text>
<line x1="120" y1="146" x2="640" y2="146" stroke="#e08a3c" stroke-width="2"/>
<line x1="160" y1="28" x2="160" y2="44" stroke="#4a7bd0" stroke-width="2"/>
<line x1="300" y1="28" x2="300" y2="44" stroke="#4a7bd0" stroke-width="2"/>
<line x1="440" y1="28" x2="440" y2="44" stroke="#4a7bd0" stroke-width="2"/>
<line x1="580" y1="28" x2="580" y2="44" stroke="#4a7bd0" stroke-width="2"/>
<line x1="175" y1="138" x2="175" y2="154" stroke="#e08a3c" stroke-width="2"/>
<line x1="306" y1="138" x2="306" y2="154" stroke="#e08a3c" stroke-width="2"/>
<line x1="446" y1="138" x2="446" y2="154" stroke="#e08a3c" stroke-width="2"/>
<line x1="586" y1="138" x2="586" y2="154" stroke="#e08a3c" stroke-width="2"/>
<line x1="160" y1="46" x2="174" y2="134" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
<line x1="300" y1="46" x2="305" y2="134" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
<line x1="440" y1="46" x2="445" y2="134" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
<line x1="580" y1="46" x2="585" y2="134" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
<text x="180" y="92" font-size="12">첫 엣지만 늦게 찍힘(이상치)</text>
<text x="330" y="74" font-size="12">나머지는 일정한 offset</text>
<text x="10" y="196" font-size="12">첫 엣지 하나로 맞추면: offset = 이상치 → 나머지 전부가 일정하게 틀어진다</text>
<text x="10" y="218" font-size="12">모든 엣지의 (LA − ftrace) 중앙값으로 맞추면: 이상치에 끌려가지 않는다</text>
<text x="10" y="240" font-size="12">드리프트(ppm)는 앞 절반과 뒤 절반의 중앙값 offset 차이 ÷ 시간으로 본다</text>
</svg>
```

그림 7 — sync 엣지로 두 시간축을 맞추는 법. 엣지 하나는 그 엣지 자체가 이상치일 수 있다. 여러 엣지의 중앙값이 안전하다.

`ana.py`의 정렬 부분(발췌):

```python
# clock alignment (a) one sync edge = first EXT edge  (b) all EXT edges: least-squares q = f*(1+a) + b
f0, q0 = ft[0][0], qe[0][0]
n = min(len(ft[0]), len(qe[0]))
F = [ft[0][i] for i in range(n)]; Q = [qe[0][i] for i in range(n)]
res1 = [((Q[i] - q0) - (F[i] - f0)) * 1e6 for i in range(n)]
mf = st.mean(F); dq = [Q[i] - F[i] for i in range(n)]; md = st.mean(dq)
a_ = sum((F[i] - mf) * (dq[i] - md) for i in range(n)) / sum((F[i] - mf) ** 2 for i in range(n))
b_ = md - a_ * mf
res_lsq = [(dq[i] - (a_ * F[i] + b_)) * 1e6 for i in range(n)]
om = st.median(dq)                                  # robust: median offset over all edges
res2 = [(x - om) * 1e6 for x in dq]
h = n // 2                                          # robust drift: median offset of 2nd half vs 1st half
drift = (st.median(dq[h:]) - st.median(dq[:h])) / (st.median(F[h:]) - st.median(F[:h]))
```

```text
#### lat_idle
align one_edge n=200 span=1.522s residual p50=  -87.1 min=  -95.1 max= 2411.0 us
align lsq      n=200 span=1.522s residual p50=  -16.2 min=  -47.4 max= 2465.2 us
align median   n=200 span=1.522s residual p50=    0.0 min=   -7.9 max= 2498.1 us
drift: lsq 31.8 ppm, robust(half medians) 1.3 ppm
  pin EXT    qemu - fit(ftrace): p50=   0.0 min=  -7.9 max=2498.1 us
  pin HARD   qemu - fit(ftrace): p50=  -6.9 min=  -9.1 max=  84.2 us
  pin THREAD qemu - fit(ftrace): p50=  -2.9 min=  -8.8 max= 102.0 us
  pin WORK   qemu - fit(ftrace): p50=  -6.0 min=  -9.1 max= 937.2 us
  pin USER   qemu - fit(ftrace): p50=   6.0 min=  -4.8 max=14784.1 us
#### lat_load
align one_edge n=200 span=1.137s residual p50= -375.9 min= -382.9 max= 2649.2 us
align lsq      n=200 span=1.137s residual p50=  -16.0 min=  -72.2 max= 2956.9 us
align median   n=200 span=1.137s residual p50=    0.0 min=   -7.2 max= 3024.8 us
drift: lsq -86.0 ppm, robust(half medians) 0.0 ppm
  pin EXT    qemu - fit(ftrace): p50=   0.0 min=  -7.2 max=3024.8 us
  pin HARD   qemu - fit(ftrace): p50=  -4.3 min=  -8.3 max=2484.8 us
  pin THREAD qemu - fit(ftrace): p50=   1.7 min=  -7.2 max= 273.0 us
  pin WORK   qemu - fit(ftrace): p50=  -5.0 min=  -8.3 max=4579.8 us
  pin USER   qemu - fit(ftrace): p50=   6.9 min=  -1.2 max=2309.8 us
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 330">
<line x1="70" y1="280.0" x2="640" y2="280.0" stroke="currentColor" stroke-opacity="0.2"/>
<text x="62" y="284.0" font-size="12" text-anchor="end">-150</text>
<line x1="70" y1="240.0" x2="640" y2="240.0" stroke="currentColor" stroke-opacity="0.2"/>
<text x="62" y="244.0" font-size="12" text-anchor="end">-100</text>
<line x1="70" y1="200.0" x2="640" y2="200.0" stroke="currentColor" stroke-opacity="0.2"/>
<text x="62" y="204.0" font-size="12" text-anchor="end">-50</text>
<line x1="70" y1="160.0" x2="640" y2="160.0" stroke="currentColor" stroke-opacity="0.6"/>
<text x="62" y="164.0" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="120.0" x2="640" y2="120.0" stroke="currentColor" stroke-opacity="0.2"/>
<text x="62" y="124.0" font-size="12" text-anchor="end">50</text>
<line x1="70" y1="80.0" x2="640" y2="80.0" stroke="currentColor" stroke-opacity="0.2"/>
<text x="62" y="84.0" font-size="12" text-anchor="end">100</text>
<line x1="70" y1="40.0" x2="640" y2="40.0" stroke="currentColor" stroke-opacity="0.2"/>
<text x="62" y="44.0" font-size="12" text-anchor="end">150</text>
<text x="10" y="20" font-size="12">residual µs = QEMU edge time − mapped ftrace time (EXT pin, 200 edges)</text>
<circle cx="70.0" cy="160.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="71.8" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="73.8" cy="224.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="76.3" cy="223.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="77.6" cy="219.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="79.5" cy="225.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="81.2" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="83.1" cy="232.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="85.8" cy="227.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="87.0" cy="232.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="88.7" cy="230.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="90.6" cy="234.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="92.7" cy="232.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="96.1" cy="230.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="99.4" cy="225.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="103.2" cy="220.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="127.3" cy="231.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="132.5" cy="226.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="136.5" cy="160.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="145.9" cy="233.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="148.5" cy="93.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="151.0" cy="226.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="152.5" cy="234.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="154.8" cy="228.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="156.2" cy="235.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="158.0" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="160.8" cy="228.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="162.8" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="165.7" cy="235.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="183.9" cy="228.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="187.1" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="191.0" cy="228.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="191.6" cy="235.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="193.5" cy="232.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="195.7" cy="222.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="197.2" cy="232.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="199.3" cy="230.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="203.1" cy="223.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="204.7" cy="230.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="208.6" cy="225.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/>
<circle cx="210.4" cy="234.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="212.5" cy="231.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="214.3" cy="226.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="216.2" cy="227.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="218.8" cy="229.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="219.8" cy="230.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="221.6" cy="234.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="223.6" cy="236.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="225.7" cy="231.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="227.4" cy="233.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="229.2" cy="234.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="231.0" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="234.9" cy="233.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="236.6" cy="233.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="239.5" cy="229.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="240.5" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="242.2" cy="231.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="245.2" cy="227.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="246.1" cy="231.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="248.4" cy="228.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="252.3" cy="227.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="257.4" cy="232.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="259.3" cy="224.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="268.3" cy="226.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="272.6" cy="227.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="274.2" cy="233.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="276.2" cy="236.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="277.8" cy="226.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="279.6" cy="229.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="281.5" cy="232.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="283.4" cy="228.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="285.2" cy="232.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="287.1" cy="232.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="289.0" cy="223.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="290.9" cy="232.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="292.8" cy="225.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="297.4" cy="232.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="299.6" cy="232.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="300.2" cy="117.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="302.3" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/>
<circle cx="304.0" cy="235.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="305.8" cy="225.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="308.8" cy="228.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="309.6" cy="229.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="311.4" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="329.7" cy="224.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="330.8" cy="232.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="332.8" cy="236.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="333.9" cy="228.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="336.1" cy="232.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="337.7" cy="229.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="342.1" cy="227.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="361.8" cy="227.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="365.7" cy="226.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="368.2" cy="229.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="369.8" cy="224.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="373.6" cy="229.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="375.5" cy="229.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="377.0" cy="232.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="378.9" cy="227.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="380.8" cy="229.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="384.2" cy="226.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="396.0" cy="229.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="397.6" cy="232.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="400.5" cy="40.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="401.3" cy="235.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="403.2" cy="224.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="405.0" cy="234.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="407.0" cy="217.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="410.0" cy="224.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="411.2" cy="235.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="412.6" cy="235.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="414.9" cy="229.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="417.1" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="418.3" cy="233.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="420.0" cy="234.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="421.9" cy="226.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="423.9" cy="227.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="425.7" cy="227.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="427.5" cy="232.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/>
<circle cx="429.6" cy="217.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="431.8" cy="221.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="433.2" cy="230.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="435.2" cy="228.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="437.0" cy="102.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="438.9" cy="227.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="440.6" cy="231.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="442.5" cy="227.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="444.4" cy="230.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="446.3" cy="228.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="448.1" cy="229.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="450.1" cy="229.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="451.9" cy="229.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="455.6" cy="224.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="457.5" cy="230.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="459.4" cy="231.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="461.3" cy="227.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="463.1" cy="235.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="471.0" cy="228.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="472.5" cy="235.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="474.3" cy="230.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="476.3" cy="229.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="481.6" cy="232.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="481.8" cy="235.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="483.7" cy="231.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="485.6" cy="231.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="489.4" cy="209.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="491.2" cy="236.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="493.1" cy="229.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="494.9" cy="229.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="496.8" cy="232.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="498.7" cy="233.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="500.6" cy="228.9" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="502.4" cy="234.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="505.5" cy="227.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="506.2" cy="226.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="508.4" cy="231.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="510.1" cy="221.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="511.8" cy="231.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="513.7" cy="228.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/>
<circle cx="515.6" cy="228.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="518.7" cy="230.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="519.3" cy="233.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="521.1" cy="229.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="523.8" cy="227.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="528.2" cy="223.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="528.7" cy="234.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="530.8" cy="228.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="532.4" cy="40.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="548.8" cy="226.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="550.0" cy="213.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="551.3" cy="226.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="553.2" cy="228.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="554.9" cy="223.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="557.2" cy="227.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="559.1" cy="145.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="560.9" cy="229.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="562.5" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="565.7" cy="221.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="566.3" cy="234.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="568.4" cy="189.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="572.2" cy="233.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="573.5" cy="208.0" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="583.7" cy="224.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="588.1" cy="216.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="594.6" cy="231.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="596.0" cy="234.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="600.7" cy="230.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="601.6" cy="230.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="603.6" cy="231.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="605.4" cy="235.3" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="610.8" cy="224.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="611.5" cy="236.1" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="615.4" cy="172.8" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="616.7" cy="233.7" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="619.2" cy="223.2" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="624.7" cy="218.4" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="626.2" cy="234.5" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="638.8" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/><circle cx="640.0" cy="233.6" r="2.5" fill="#d0564a" fill-opacity="0.8"/>
<circle cx="70.0" cy="90.2" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="71.8" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="73.8" cy="155.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="76.3" cy="153.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="77.6" cy="149.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="79.5" cy="156.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="81.2" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="83.1" cy="163.1" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="85.8" cy="157.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="87.0" cy="162.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="88.7" cy="160.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="90.6" cy="164.6" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="92.7" cy="163.1" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="96.1" cy="160.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="99.4" cy="155.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="103.2" cy="151.2" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="127.3" cy="161.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="132.5" cy="156.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="136.5" cy="91.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="145.9" cy="164.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="148.5" cy="40.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="151.0" cy="156.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="152.5" cy="164.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="154.8" cy="159.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="156.2" cy="165.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="158.0" cy="164.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="160.8" cy="158.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="162.8" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="165.7" cy="165.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="183.9" cy="158.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="187.1" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="191.0" cy="159.2" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="191.6" cy="165.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="193.5" cy="162.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="195.7" cy="152.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="197.2" cy="163.2" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="199.3" cy="160.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="203.1" cy="153.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="204.7" cy="160.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="208.6" cy="155.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/>
<circle cx="210.4" cy="164.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="212.5" cy="161.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="214.3" cy="156.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="216.2" cy="157.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="218.8" cy="159.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="219.8" cy="160.6" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="221.6" cy="164.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="223.6" cy="166.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="225.7" cy="161.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="227.4" cy="164.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="229.2" cy="164.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="231.0" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="234.9" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="236.6" cy="164.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="239.5" cy="160.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="240.5" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="242.2" cy="162.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="245.2" cy="157.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="246.1" cy="161.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="248.4" cy="158.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="252.3" cy="157.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="257.4" cy="162.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="259.3" cy="155.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="268.3" cy="156.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="272.6" cy="157.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="274.2" cy="164.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="276.2" cy="166.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="277.8" cy="156.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="279.6" cy="159.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="281.5" cy="163.1" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="283.4" cy="158.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="285.2" cy="163.1" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="287.1" cy="163.2" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="289.0" cy="153.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="290.9" cy="162.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="292.8" cy="155.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="297.4" cy="162.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="299.6" cy="163.1" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="300.2" cy="47.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="302.3" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/>
<circle cx="304.0" cy="165.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="305.8" cy="156.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="308.8" cy="158.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="309.6" cy="159.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="311.4" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="329.7" cy="154.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="330.8" cy="162.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="332.8" cy="166.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="333.9" cy="158.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="336.1" cy="162.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="337.7" cy="159.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="342.1" cy="158.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="361.8" cy="157.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="365.7" cy="156.6" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="368.2" cy="159.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="369.8" cy="154.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="373.6" cy="160.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="375.5" cy="160.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="377.0" cy="163.2" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="378.9" cy="157.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="380.8" cy="159.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="384.2" cy="156.6" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="396.0" cy="159.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="397.6" cy="162.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="400.5" cy="40.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="401.3" cy="165.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="403.2" cy="154.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="405.0" cy="164.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="407.0" cy="148.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="410.0" cy="154.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="411.2" cy="165.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="412.6" cy="165.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="414.9" cy="160.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="417.1" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="418.3" cy="164.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="420.0" cy="164.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="421.9" cy="156.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="423.9" cy="157.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="425.7" cy="157.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="427.5" cy="163.1" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/>
<circle cx="429.6" cy="148.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="431.8" cy="152.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="433.2" cy="160.6" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="435.2" cy="158.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="437.0" cy="40.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="438.9" cy="157.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="440.6" cy="161.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="442.5" cy="157.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="444.4" cy="160.6" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="446.3" cy="158.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="448.1" cy="160.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="450.1" cy="160.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="451.9" cy="159.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="455.6" cy="155.2" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="457.5" cy="160.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="459.4" cy="161.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="461.3" cy="157.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="463.1" cy="165.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="471.0" cy="159.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="472.5" cy="165.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="474.3" cy="160.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="476.3" cy="159.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="481.6" cy="163.1" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="481.8" cy="165.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="483.7" cy="161.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="485.6" cy="161.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="489.4" cy="140.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="491.2" cy="166.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="493.1" cy="160.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="494.9" cy="159.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="496.8" cy="163.1" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="498.7" cy="164.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="500.6" cy="159.2" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="502.4" cy="164.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="505.5" cy="157.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="506.2" cy="156.6" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="508.4" cy="161.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="510.1" cy="152.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="511.8" cy="161.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="513.7" cy="159.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/>
<circle cx="515.6" cy="158.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="518.7" cy="160.6" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="519.3" cy="164.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="521.1" cy="160.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="523.8" cy="157.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="528.2" cy="153.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="528.7" cy="164.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="530.8" cy="159.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="532.4" cy="40.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="548.8" cy="156.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="550.0" cy="144.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="551.3" cy="156.6" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="553.2" cy="158.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="554.9" cy="153.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="557.2" cy="157.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="559.1" cy="75.9" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="560.9" cy="159.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="562.5" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="565.7" cy="151.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="566.3" cy="164.6" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="568.4" cy="119.9" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="572.2" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="573.5" cy="138.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="583.7" cy="155.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="588.1" cy="147.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="594.6" cy="161.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="596.0" cy="164.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="600.7" cy="160.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="601.6" cy="160.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="603.6" cy="161.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="605.4" cy="165.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="610.8" cy="154.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="611.5" cy="166.3" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="615.4" cy="103.2" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="616.7" cy="164.0" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="619.2" cy="153.5" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="624.7" cy="148.7" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="626.2" cy="164.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="638.8" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="640.0" cy="163.8" r="2.5" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="70.0" y="300" font-size="12" text-anchor="middle">0</text>
<text x="163.6" y="300" font-size="12" text-anchor="middle">250</text>
<text x="257.2" y="300" font-size="12" text-anchor="middle">500</text>
<text x="350.8" y="300" font-size="12" text-anchor="middle">750</text>
<text x="444.4" y="300" font-size="12" text-anchor="middle">1000</text>
<text x="538.0" y="300" font-size="12" text-anchor="middle">1250</text>
<text x="631.6" y="300" font-size="12" text-anchor="middle">1500</text>
<text x="640" y="318" font-size="12" text-anchor="end">ms since first EXT edge</text>
<circle cx="80" cy="314" r="4" fill="#d0564a"/><text x="90" y="318" font-size="12">sync on 1 edge</text>
<circle cx="210" cy="314" r="4" fill="#4a7bd0"/><text x="220" y="318" font-size="12">median offset, all edges</text>
<text x="640" y="20" font-size="12" text-anchor="end">clipped at ±150: 2 / 4 pts</text>
</svg>
```

그림 8 — idle 실행의 EXT 엣지 200개 잔차. 빨강: 첫 엣지 하나로 맞췄을 때 — 전부 약 −90 µs로 일정하게 틀어졌다(첫 엣지가 이상치였다). 파랑: 모든 엣지의 중앙값 offset — 0 근처에 ±10 µs로 모인다. 위로 튄 점은 "커널이 찍은 시각보다 핀이 ms 늦게 움직인" 사건.

출력에서 볼 것:

- **첫 엣지 sync의 함정**: idle에서 −87 µs, load에서 −376 µs만큼 모든 샘플이 틀어졌다. 첫 엣지 때 ftrace 기록과 실제 PL061 쓰기 사이에 무언가(첫 실행 코드의 TCG 번역, 캐시 미스에 해당)가 끼었기 때문이다. 실보드에서도 첫 번째 이벤트는 cold cache·TLB miss로 느린 경우가 흔하다. **sync 엣지는 여러 개 내고 중앙값을 쓴다.**
- **최소제곱(lsq)도 이상치에 끌려간다**: 드리프트를 31.8 ppm, −86 ppm으로 엉뚱하게 추정했다. 중앙값 기반 드리프트는 1.3 ppm, 0.0 ppm. 이 랩은 게스트 타이머와 호스트 시계가 같은 호스트 클럭에서 나오므로 드리프트가 0에 가까운 것이 정답이다. 실보드에서는 LA의 수정 발진기와 SoC의 수정이 다르므로 **수십 ppm 드리프트가 실제로 있다** — 1초 캡처면 수십 µs가 벌어진다. 긴 캡처는 주기적인 sync 엣지(예: 1초마다 펄스)로 구간별 보정한다.
- **잔차가 커널이 "속인" 양이다**: median 정렬 후 대부분의 엣지는 ±10 µs 안에서 맞는다. 그러나 최댓값이 EXT 2.5 ms, WORK 0.9~4.6 ms, USER **14.8 ms**다. ftrace는 "핀을 set했다"고 기록했는데 실제 레지스터 쓰기는 그만큼 뒤였다는 뜻이다. `gpiod_set_raw_value_commit()`은 `trace_gpio_value()`를 **먼저** 부르고 그다음 `gpiochip_set()`을 부른다(랩 소스 `drivers/gpio/gpiolib.c`). 그 사이에 선점(유저·kworker 문맥)이나 vCPU가 호스트에서 밀려나는 일이 생기면 로그 시각과 핀 시각이 벌어진다. HARD(hardirq 문맥, 선점 불가)의 잔차가 idle에서 최대 84 µs로 가장 작은 것도 이 설명과 맞는다.
- 실보드 교훈: **"tracepoint 시각 = 사건 시각"이 아니다.** 같은 이유로 `i2c_write` tracepoint 시각은 버스에 START가 나간 시각이 아니다(§6).

### 5.4 실보드에서 sync를 만드는 방법들

| 방법 | 어떻게 | 정확도 |
|---|---|---|
| 디버그 GPIO 펄스 + trace_marker | 유저/커널이 펄스를 내면서 같은 순간 `trace_marker`나 `gpio_value`가 찍힌다 | 수 µs (위 잔차만큼) |
| 커널 모듈이 hardirq·IRQ off 상태에서 펄스 + `trace_printk` | 선점 불가 구간이라 잔차가 작다 | 1 µs 이하 (실보드) |
| PPS 입력 | GNSS PPS를 SoC GPIO와 LA에 동시에. 커널 `pps` 서브시스템이 timestamp | 수십 ns~µs |
| PTP 하드웨어 타임스탬프 | 이더넷 PHY/MAC이 패킷 timestamp, `tai` 시계로 트레이스 | 수십 ns (장비 간) |
| 주기 펄스 | 1초마다 sync 펄스 → 구간별 offset·드리프트 보정 | 긴 캡처용 |

---

## 6. 버스 트레이스와 커널 로그 맞추기

### 6.1 커널 쪽에 이미 있는 "소프트웨어 프로토콜 분석기"

예제 5의 마지막 출력이 랩 커널의 버스 관련 tracepoint 목록이다. 정리하면:

| 그룹 | 이벤트 | 언제 찍히나 | LA에서 대응되는 것 |
|---|---|---|---|
| `i2c` | `i2c_write`, `i2c_read` | `__i2c_transfer()`가 어댑터 `.xfer`를 부르기 **직전**, 메시지마다 | 각 메시지의 START/Sr + 주소 바이트 |
| `i2c` | `i2c_reply` | `.xfer` 반환 **후**, 읽기 메시지의 데이터 | 읽기 데이터 바이트들 (이미 끝난 과거) |
| `i2c` | `i2c_result` | 전송 전체 반환값 | STOP 이후. 음수면 NACK·타임아웃 |
| `smbus` | `smbus_write/read/reply/result` | `i2c_smbus_xfer()` 경로 | SMBus 명령 단위 |
| `spi` | `spi_message_submit/start/done` | 메시지 큐 진입·전송 시작·완료 | CS 구간 전체 |
| `spi` | `spi_set_cs` | CS 핀 토글 직전 | **CS 엣지 — 가장 좋은 sync 후보** |
| `spi` | `spi_transfer_start/stop` | transfer 하나의 시작·끝 (데이터 포함) | CS 안의 바이트 묶음 |
| `regmap` | `regmap_reg_write/read`, `regmap_hw_read_start/done`, `regmap_bulk_read` | 드라이버의 레지스터 접근 단위 | 레지스터 주소 바이트 + 값 |
| `gpio` | `gpio_value`, `gpio_direction` | gpiolib 경유 핀 변경 직전 | 디지털 채널 엣지 |

CAN은 `can_*` tracepoint 대신 SocketCAN 소켓의 하드웨어/소프트웨어 timestamp(`SO_TIMESTAMPING`)와 `candump -t a`를 쓰고(I10), UART는 tracepoint가 없어 tty 레이어를 `function` tracer로 보거나 디버그 핀을 쓴다(H8).

### 6.2 예제 11 — 가짜 I2C 컨트롤러로 트레이스 만들기

QEMU `virt`에는 I2C 컨트롤러가 없고 랩 커널은 `CONFIG_I2C_STUB`도 꺼져 있다(H5와 같은 상황). H5의 `h5_fakebus`는 DT 노드가 필요한 platform 드라이버라서, 여기서는 DT 없이 바로 어댑터를 등록하는 50줄짜리 컨트롤러를 새로 쓴다. 0x68에 IMU가 있는 척하고(WHO_AM_I=0x68, 0x3b~0x40 가속도 데이터), 전송하는 동안 **PL061 line 0을 High로** 둔다 — 실제 컨트롤러라면 SCL/SDA를 LA에 물렸을 자리다. 100 kHz 버스 시간을 흉내 내려고 바이트당 90 µs(9클럭) `udelay`를 넣었다.

```c
/* m11_i2c.c (1/2) — 가짜 I2C 컨트롤러: 0x68에 IMU가 있는 척 + LA 트리거 핀 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>

static u8 reg[256] = { [0x75] = 0x68, [0x3b] = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
static u8 ptr;
static struct gpio_desc *trig;
static struct platform_device *pdev;
static struct i2c_client *cl;
static struct gpiod_lookup_table lt = {
	.dev_id = "m11_i2c",
	.table = { GPIO_LOOKUP("9030000.pl061", 0, "trig", GPIO_ACTIVE_HIGH), { } },
};

static int m11_xfer(struct i2c_adapter *a, struct i2c_msg *m, int n)
{
	int i, j, ret = n;

	gpiod_set_value(trig, 1);		/* "컨트롤러가 START를 낸다" = LA 트리거 */
	for (i = 0; i < n; i++, m++) {
		if (m->addr != 0x68) { ret = -ENXIO; break; }	/* 주소 NACK */
		for (j = 0; j < m->len; j++) {
			if (m->flags & I2C_M_RD)
				m->buf[j] = reg[ptr++];
			else if (j == 0)
				ptr = m->buf[0];	/* 첫 바이트 = 레지스터 주소 */
			else
				reg[ptr++] = m->buf[j];
		}
		udelay(90 * (m->len + 1));	/* 100 kHz 흉내: (주소+데이터) 바이트당 9클럭 */
	}
	gpiod_set_value(trig, 0);
	return ret;
}
static u32 m11_func(struct i2c_adapter *a) { return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL; }
static const struct i2c_algorithm algo = { .xfer = m11_xfer, .functionality = m11_func };
static struct i2c_adapter adap = { .owner = THIS_MODULE, .algo = &algo, .name = "m11-fake" };
static const struct regmap_config rc = { .reg_bits = 8, .val_bits = 8, .max_register = 0xff };
```

```c
/* m11_i2c.c (2/2) — 어댑터 등록, regmap으로 한 번 읽기, 정리 */
static int __init m11_i2c_init(void)
{
	struct regmap *map;
	u8 v[6];
	int ret;

	gpiod_add_lookup_table(&lt);
	pdev = platform_device_register_simple("m11_i2c", -1, NULL, 0);
	if (IS_ERR(pdev)) { ret = PTR_ERR(pdev); goto e_lt; }
	trig = gpiod_get(&pdev->dev, "trig", GPIOD_OUT_LOW);
	if (IS_ERR(trig)) { ret = PTR_ERR(trig); goto e_pd; }
	ret = i2c_add_adapter(&adap);
	if (ret) goto e_gp;
	cl = i2c_new_dummy_device(&adap, 0x68);
	if (IS_ERR(cl)) { ret = PTR_ERR(cl); goto e_ad; }
	map = regmap_init_i2c(cl, &rc);
	if (IS_ERR(map)) { ret = PTR_ERR(map); goto e_cl; }
	ret = regmap_bulk_read(map, 0x3b, v, 6);	/* regmap → i2c 두 층의 tracepoint */
	pr_info("m11_i2c: bus=%d regmap 0x3b..0x40 = %*ph (ret=%d)\n", adap.nr, 6, v, ret);
	regmap_exit(map);
	return 0;
e_cl:	i2c_unregister_device(cl);
e_ad:	i2c_del_adapter(&adap);
e_gp:	gpiod_put(trig);
e_pd:	platform_device_unregister(pdev);
e_lt:	gpiod_remove_lookup_table(&lt);
	return ret;
}

static void __exit m11_i2c_exit(void)
{
	i2c_unregister_device(cl);
	i2c_del_adapter(&adap);
	gpiod_put(trig);
	platform_device_unregister(pdev);
	gpiod_remove_lookup_table(&lt);
}
module_init(m11_i2c_init);
module_exit(m11_i2c_exit);
MODULE_DESCRIPTION("M11: fake I2C controller for LA/ftrace correlation");
MODULE_LICENSE("GPL");
```

유저 공간에서는 i2c-dev의 `I2C_RDWR`로 write-then-read를 두 번 한다(A3, H5 §6).

```c
/* m11_i2cx: i2c-dev로 WHO_AM_I(0x75) 1바이트, 가속도(0x3b) 6바이트 읽기 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

static void rd(int fd, unsigned char r, unsigned char *buf, int len)
{
	struct i2c_msg m[2] = {
		{ .addr = 0x68, .flags = 0, .len = 1, .buf = &r },
		{ .addr = 0x68, .flags = I2C_M_RD, .len = len, .buf = buf },
	};
	struct i2c_rdwr_ioctl_data d = { .msgs = m, .nmsgs = 2 };
	if (ioctl(fd, I2C_RDWR, &d) < 0) { perror("I2C_RDWR"); exit(1); }
}
int main(int argc, char **argv)
{
	unsigned char b[6];
	int fd = open(argc > 1 ? argv[1] : "/dev/i2c-0", O_RDWR);
	if (fd < 0) { perror("open"); return 1; }
	rd(fd, 0x75, b, 1);
	printf("WHO_AM_I=0x%02x\n", b[0]);
	rd(fd, 0x3b, b, 6);
	printf("ACCEL=%02x %02x %02x %02x %02x %02x\n", b[0], b[1], b[2], b[3], b[4], b[5]);
	return 0;
}
```

```sh
#!/bin/sh
# g_i2c.sh (게스트)
T=/sys/kernel/tracing
mount -t tracefs none $T 2>/dev/null
echo 0 > $T/tracing_on; echo mono > $T/trace_clock
for e in i2c smbus regmap gpio/gpio_value; do echo 1 > $T/events/$e/enable; done
echo 1 > $T/tracing_on
insmod /m11_i2c.ko
B=$(grep -l m11-fake /sys/bus/i2c/devices/i2c-*/name | head -1); B=${B%/name}; B=/dev/${B##*/}
/m11_i2cx $B
echo 0 > $T/tracing_on
dmesg | grep m11_i2c
grep -v "^#" $T/trace | sed 's/^ *//'
```

```text
lab initramfs: 6.18.55-trace up 20.34s
WHO_AM_I=0x68
ACCEL=01 02 03 04 05 06
[   21.430588][   T78] m11_i2c: loading out-of-tree module taints kernel.
[   21.431173][   T78] m11_i2c: module verification failed: signature and/or required key missing - tainting kernel
[   21.743060][   T78] m11_i2c: bus=0 regmap 0x3b..0x40 = 01 02 03 04 05 06 (ret=0)
insmod-78      [000] .....    21.276532: gpio_value: 512 set 0
insmod-78      [000] .....    21.330566: regmap_hw_read_start: 0-0068 reg=3b count=6
insmod-78      [000] .....    21.331023: i2c_write: i2c-0 #0 a=068 f=0000 l=1 [3b]
insmod-78      [000] .....    21.331187: i2c_read: i2c-0 #1 a=068 f=0001 l=6
insmod-78      [000] .....    21.331407: gpio_value: 512 set 1
insmod-78      [000] .....    21.332930: gpio_value: 512 set 0
insmod-78      [000] .....    21.333200: i2c_reply: i2c-0 #1 a=068 f=0001 l=6 [01-02-03-04-05-06]
insmod-78      [000] .....    21.333390: i2c_result: i2c-0 n=2 ret=2
insmod-78      [000] .....    21.338724: regmap_hw_read_done: 0-0068 reg=3b count=6
insmod-78      [000] .....    21.339079: regmap_bulk_read: 0-0068 reg=3b val=01 02 03 04 05 06
m11_i2cx-82      [000] .....    21.833058: i2c_write: i2c-0 #0 a=068 f=0200 l=1 [75]
m11_i2cx-82      [000] .....    21.833177: i2c_read: i2c-0 #1 a=068 f=0201 l=1
m11_i2cx-82      [000] .....    21.833211: gpio_value: 512 set 1
m11_i2cx-82      [000] .....    21.833723: gpio_value: 512 set 0
m11_i2cx-82      [000] .....    21.833761: i2c_reply: i2c-0 #1 a=068 f=0201 l=1 [68]
m11_i2cx-82      [000] .....    21.833768: i2c_result: i2c-0 n=2 ret=2
m11_i2cx-82      [000] .....    21.842595: i2c_write: i2c-0 #0 a=068 f=0200 l=1 [3b]
m11_i2cx-82      [000] .....    21.842624: i2c_read: i2c-0 #1 a=068 f=0201 l=6
m11_i2cx-82      [000] .....    21.842645: gpio_value: 512 set 1
m11_i2cx-82      [000] .....    21.843619: gpio_value: 512 set 0
m11_i2cx-82      [000] .....    21.843910: i2c_reply: i2c-0 #1 a=068 f=0201 l=6 [01-02-03-04-05-06]
m11_i2cx-82      [000] .....    21.843923: i2c_result: i2c-0 n=2 ret=2
```

같은 실행의 QEMU 로그(트리거 핀, 외부 관찰자):

```text
2026-10-18T06:42:23.696183Z pl061_set_output /machine/unattached/device[8] setting output 0 to 1
2026-10-18T06:42:23.697647Z pl061_set_output /machine/unattached/device[8] setting output 0 to 0
2026-10-18T06:42:24.197928Z pl061_set_output /machine/unattached/device[8] setting output 0 to 1
2026-10-18T06:42:24.198418Z pl061_set_output /machine/unattached/device[8] setting output 0 to 0
2026-10-18T06:42:24.207345Z pl061_set_output /machine/unattached/device[8] setting output 0 to 1
2026-10-18T06:42:24.208321Z pl061_set_output /machine/unattached/device[8] setting output 0 to 0
```

출력에서 볼 것:

- **층이 보인다**: `regmap_hw_read_start` → `i2c_write`/`i2c_read` → (버스) → `i2c_reply` → `i2c_result` → `regmap_hw_read_done` → `regmap_bulk_read`. 드라이버가 regmap을 쓰면 "어느 레지스터를 읽으려 했나"(regmap)와 "버스에 무엇이 나갔나"(i2c)를 같이 본다(H2).
- **i2c-dev 경유는 `f=0200`**: flags 0x0200 = `I2C_M_DMA_SAFE`. 유저 버퍼를 커널이 DMA-safe 버퍼로 복사해 넘겼다는 뜻이다. 커널 내부 regmap 경로는 `f=0000`/`f=0001`(`I2C_M_RD`)이다.
- **"요청" 이벤트는 버스 활동 전, "응답" 이벤트는 버스 활동 후**: 세 번째 전송에서 `i2c_write`(21.842595)는 트리거 핀 상승(21.842645)보다 50 µs 앞이고, `i2c_reply`(21.843910)는 트리거 하강(21.843619)보다 291 µs 뒤다. 즉 로그의 `i2c_reply [01-02-...]` 줄 시각은 **데이터가 버스에 있던 시각이 아니다.**
- 세 상승 엣지의 (QEMU − ftrace) 차이: 2.364776, 2.364717, 2.364700 s(+ epoch). 첫 엣지(insmod의 regmap 읽기)가 가장 크게(약 60 µs) 벗어난다 — §5.3의 "첫 엣지 이상치"가 여기서도 재현됐다. 그래서 아래 그림은 `m11_i2cx`의 두 엣지 중앙값으로 offset을 잡았다.

### 6.3 예제 12 — 같은 시간축에 올리기 (LA 디코드는 합성)

무엇을 확인하나: 세 번째 전송(0x3b에서 6바이트)의 ftrace 이벤트를 median offset으로 LA 시간축에 매핑하고, 트리거 핀 파형과 **합성한** I2C 디코드(바이트를 100 kHz, 바이트당 9클럭으로 다시 그린 것)를 겹친다. `gen_svg.py`가 실제 트레이스·QEMU 로그를 읽어 그린다.

```text
i2c offset 1792330922.3647084 [('i2c_write', -41), ('i2c_read', -13), ('gpio_value', 8), ('gpio_value', 983), ('i2c_reply', 1273), ('i2c_result', 1287)] trig 0.0 976.0856628417969
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 350">
<text x="10" y="20" font-size="12">time 0 = trigger pin rising edge seen by the "LA" (QEMU log); ftrace mapped by median offset (2 edges)</text>
<text x="10" y="58" font-size="12">ftrace</text>
<line x1="83.4" y1="70" x2="83.4" y2="96" stroke="currentColor" stroke-width="2"/>
<text x="83.4" y="64" font-size="12" text-anchor="middle">a</text>
<line x1="94.9" y1="70" x2="94.9" y2="96" stroke="currentColor" stroke-width="2"/>
<text x="94.9" y="50" font-size="12" text-anchor="middle">b</text>
<line x1="103.3" y1="70" x2="103.3" y2="96" stroke="currentColor" stroke-width="2"/>
<text x="103.3" y="64" font-size="12" text-anchor="middle">c</text>
<line x1="493.0" y1="70" x2="493.0" y2="96" stroke="currentColor" stroke-width="2"/>
<text x="493.0" y="50" font-size="12" text-anchor="middle">d</text>
<line x1="609.4" y1="70" x2="609.4" y2="96" stroke="currentColor" stroke-width="2"/>
<text x="609.4" y="64" font-size="12" text-anchor="middle">e</text>
<line x1="614.6" y1="70" x2="614.6" y2="96" stroke="currentColor" stroke-width="2"/>
<text x="614.6" y="50" font-size="12" text-anchor="middle">f</text>
<text x="10" y="300" font-size="12">a=i2c_write -41, b=i2c_read -13, c=gpio 1 +8</text>
<text x="10" y="316" font-size="12">d=gpio 0 +983, e=i2c_reply +1273, f=i2c_result +1287 (µs)</text>
<text x="10" y="134" font-size="12">TRIG</text>
<path d="M60.0,140 L100.0,140 L100.0,116 L490.4,116 L490.4,140 L660.0,140" fill="none" stroke="#d0564a" stroke-width="2"/>
<text x="295.2" y="110" font-size="12" text-anchor="middle">width 976 µs</text>
<text x="10" y="186" font-size="12">SDA</text><text x="10" y="200" font-size="12">mock</text>
<rect x="100.0" y="172" width="36.0" height="26" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="118.0" y="190" font-size="12" text-anchor="middle">68 W</text>
<rect x="136.0" y="172" width="36.0" height="26" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<text x="154.0" y="190" font-size="12" text-anchor="middle">3B</text>
<rect x="172.0" y="172" width="36.0" height="26" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="190.0" y="190" font-size="12" text-anchor="middle">68 R</text>
<rect x="208.0" y="172" width="36.0" height="26" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="226.0" y="190" font-size="12" text-anchor="middle">01</text>
<rect x="244.0" y="172" width="36.0" height="26" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="262.0" y="190" font-size="12" text-anchor="middle">02</text>
<rect x="280.0" y="172" width="36.0" height="26" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="298.0" y="190" font-size="12" text-anchor="middle">03</text>
<rect x="316.0" y="172" width="36.0" height="26" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="334.0" y="190" font-size="12" text-anchor="middle">04</text>
<rect x="352.0" y="172" width="36.0" height="26" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="370.0" y="190" font-size="12" text-anchor="middle">05</text>
<rect x="388.0" y="172" width="36.0" height="26" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="406.0" y="190" font-size="12" text-anchor="middle">06</text>
<text x="100.0" y="216" font-size="12" text-anchor="middle">S</text>
<text x="172.0" y="216" font-size="12" text-anchor="middle">Sr</text>
<text x="424.0" y="216" font-size="12" text-anchor="middle">P</text>
<line x1="60.0" y1="250" x2="660.0" y2="250" stroke="currentColor"/>
<line x1="60.0" y1="250" x2="60.0" y2="255" stroke="currentColor"/><text x="60.0" y="270" font-size="12" text-anchor="middle">-100</text>
<line x1="140.0" y1="250" x2="140.0" y2="255" stroke="currentColor"/><text x="140.0" y="270" font-size="12" text-anchor="middle">100</text>
<line x1="220.0" y1="250" x2="220.0" y2="255" stroke="currentColor"/><text x="220.0" y="270" font-size="12" text-anchor="middle">300</text>
<line x1="300.0" y1="250" x2="300.0" y2="255" stroke="currentColor"/><text x="300.0" y="270" font-size="12" text-anchor="middle">500</text>
<line x1="380.0" y1="250" x2="380.0" y2="255" stroke="currentColor"/><text x="380.0" y="270" font-size="12" text-anchor="middle">700</text>
<line x1="460.0" y1="250" x2="460.0" y2="255" stroke="currentColor"/><text x="460.0" y="270" font-size="12" text-anchor="middle">900</text>
<line x1="540.0" y1="250" x2="540.0" y2="255" stroke="currentColor"/><text x="540.0" y="270" font-size="12" text-anchor="middle">1100</text>
<line x1="620.0" y1="250" x2="620.0" y2="255" stroke="currentColor"/><text x="620.0" y="270" font-size="12" text-anchor="middle">1300</text>
<text x="660" y="284" font-size="12" text-anchor="end">µs</text>
<text x="10" y="340" font-size="12">mock = bytes from i2c_reply re-drawn at 100 kHz (9 clocks/byte); a real LA decodes these from SCL/SDA</text>
</svg>
```

그림 9 — 트리거 핀 상승을 0으로 둔 한 시간축. 위: ftrace 이벤트(a~f, median offset으로 매핑). 가운데: 트리거 핀(QEMU 로그 = 외부 LA). 아래: 합성 디코드(mock) — 실제 LA라면 SCL/SDA에서 디코더가 뽑아 줄 프레임. `i2c_reply`(e)는 마지막 바이트가 버스를 떠난 지 0.4 ms 이상 지나서야 찍힌다.

이 그림을 표로 바꾸면 현장에서 쓰는 "대조표"가 된다.

| LA 시각 (µs) | LA 디코드 (실보드라면) | 커널 이벤트 (매핑 시각 µs) | 판정 |
|---|---|---|---|
| −41 | (버스 조용) | a `i2c_write [3b]` (−41) | 요청 로그가 버스보다 먼저 |
| 0 | START, `68 W`, ACK | c `gpio 1` (+8) | 트리거 = 컨트롤러 진입 |
| 90 | `3B`, ACK | — | 레지스터 주소 일치 (트레이스의 `[3b]`) |
| 180 | Sr, `68 R`, ACK | b `i2c_read l=6` (−13, 이미 지남) | 읽기 길이 6 일치 |
| 270~810 | `01`..`06`, 마지막 NACK, STOP | — | 데이터 일치 (트레이스 `i2c_reply`의 `[01-02-03-04-05-06]`) |
| 976 | (트리거 하강) | d `gpio 0` (+983) | 컨트롤러 탈출 |
| 1273 | (버스 조용) | e `i2c_reply [01..06]` (+1273) | 응답 로그는 한참 뒤 |
| 1287 | — | f `i2c_result ret=2` (+1287) | 성공 |

(트리거 폭이 976 µs로 모델 810 µs보다 긴 것은 TCG 위의 `udelay`·코드 실행 오버헤드다. 실제 컨트롤러라면 클럭 스트레칭이나 FIFO 처리 간격이 이 자리에 나타난다.)

### 6.4 실무 절차 — 내용으로 짝짓고, 시각으로 확인

1. **양쪽에 같은 sync를 만든다**: 가장 좋은 것은 버스 자체의 신호다. SPI는 CS 하강(`spi_set_cs` 이벤트), I2C는 START, 또는 드라이버 진입 시 디버그 핀 펄스. 여러 번 내서 중앙값 offset.
2. **내용으로 먼저 짝짓는다**: 주소·레지스터·데이터 바이트가 같은 전송끼리 묶는다. 시각만으로 짝지으면 수백 µs 어긋남(위 표) 때문에 옆 전송과 헷갈린다. 반복되는 같은 내용이면 "n번째 발생"으로 센다.
3. **차이를 해석한다**:
   - 커널에는 있는데 LA에는 없다 → 컨트롤러가 실제로 안 보냈다(FIFO 설정, 클럭 꺼짐, pinctrl이 다른 기능).
   - LA에는 있는데 커널에는 없다 → 다른 마스터(멀티 마스터, 부트로더/펌웨어, 다른 프로세서 코어), 혹은 regmap 캐시 히트로 버스 접근이 생략되었는데 LA에는 다른 접근이 보이는 경우.
   - 둘 다 있는데 데이터가 다르다 → 바이트 순서·DMA 캐시 일관성(H9)·드라이버의 파싱 버그.
   - 둘 다 있는데 커널의 `i2c_result`가 −110(ETIMEDOUT) → LA에서 SCL이 Low로 붙어 있는지(클럭 스트레칭, 버스 hang) 확인(H5 §7).
4. **트리거를 커널 사건에 건다**: LA를 "디버그 핀 상승"에 트리거하고 버스는 프리트리거로 함께 캡처하면, 드문 사건(가끔 나는 NACK 등)만 골라 잡을 수 있다. 드라이버의 에러 경로에 `gpiod_set_value(dbg, 1)` 한 줄을 넣는 것이 가장 강력한 트리거다.

### 6.5 scope에서만 보이는 것 — DMA, 클럭 게이팅, 전원

| 현상 | scope/LA에 보이는 모습 | 커널 쪽에서 확인할 것 |
|---|---|---|
| DMA 전송 (SPI/UART) | 바이트 사이 간격이 0에 가깝고 일정, CPU 핀 토글과 무관하게 계속 | `spi_transfer_start/stop` 사이 시간, DMA 채널 할당(H9) |
| PIO 전송 | 바이트(또는 FIFO 깊이)마다 틈, IRQ 핀과 같이 움직임 | IRQ 개수(`/proc/interrupts`) |
| 클럭 게이팅 / runtime PM | 첫 전송 앞에 수십~수백 µs 공백, 그 뒤 정상. 오래 쉬었다 오면 반복 | `rpm` tracepoint, `pm_runtime` autosuspend 지연(N5, N6) |
| CPU idle 깊은 상태 복귀 | IRQ 지연이 "쉬다 온 직후"에만 길다 | `cpu_idle` tracepoint, `/sys/devices/system/cpu/cpu0/cpuidle/` |
| cpufreq 변경 | 같은 코드 구간의 핀 펄스 폭이 계단식으로 바뀐다 | `cpu_frequency` tracepoint |
| 전원 레일 | current probe 파형에 버스 활동·CPU 깨어남이 겹쳐 보인다 | 위 이벤트들과 같은 sync로 정렬 (N5) |

---

## 7. 실제 벤치 절차 (가이드 — 이 랩에서는 실행하지 않음)

### 7.1 벤치 구성

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 330">
<defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="20" y="30" width="250" height="200" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="145" y="54" font-size="14" text-anchor="middle">SoC 보드 (i.MX8 / AM62 / Orin)</text>
<rect x="40" y="70" width="100" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="90" y="95" font-size="12" text-anchor="middle">IMU (I2C/SPI)</text>
<rect x="160" y="70" width="90" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="205" y="95" font-size="12" text-anchor="middle">SoC GPIO</text>
<text x="40" y="136" font-size="12">TP1 DRDY  TP2 SCL  TP3 SDA</text>
<text x="40" y="156" font-size="12">TP4..TP6 디버그 GPIO</text>
<text x="40" y="176" font-size="12">TP7 VDD_CPU 션트/전류</text>
<text x="40" y="196" font-size="12">GND 핀 여러 개 (스프링 그라운드)</text>
<text x="40" y="216" font-size="12">UART 콘솔 → 호스트 PC</text>
<rect x="400" y="30" width="260" height="80" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="530" y="54" font-size="14" text-anchor="middle">오실로스코프 4ch</text>
<text x="414" y="76" font-size="12">CH1 DRDY, CH2 디버그(HARD)</text>
<text x="414" y="96" font-size="12">CH3 디버그(USER), CH4 전류</text>
<rect x="400" y="140" width="260" height="80" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="530" y="164" font-size="14" text-anchor="middle">로직 분석기 8~16ch</text>
<text x="414" y="186" font-size="12">D0 DRDY, D1-D4 디버그 핀</text>
<text x="414" y="206" font-size="12">D5 SCL, D6 SDA (+ I2C 디코더)</text>
<rect x="400" y="250" width="260" height="60" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="530" y="274" font-size="14" text-anchor="middle">호스트 PC</text>
<text x="414" y="296" font-size="12">PulseView / Logic 2, ssh, trace-cmd</text>
<line x1="270" y1="80" x2="398" y2="70" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="270" y1="160" x2="398" y2="180" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="530" y1="220" x2="530" y2="248" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="145" y1="230" x2="398" y2="290" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
<text x="20" y="262" font-size="12">EXT TRIG: 디버그 핀 하나를 scope의</text>
<text x="20" y="280" font-size="12">외부 트리거 또는 LA 트리거로 공유하면</text>
<text x="20" y="298" font-size="12">두 장비의 시간축이 같은 사건으로 묶인다</text>
</svg>
```

그림 10 — 실보드 벤치. 아날로그 품질·전류는 scope, 많은 채널·프로토콜 디코드는 LA. 같은 디버그 핀을 양쪽에 넣거나 트리거를 공유해 두 장비를 묶고, 커널 트레이스는 같은 핀의 `gpio_value`/`trace_marker`로 묶는다.

### 7.2 프로빙과 scope 설정 체크리스트 (Rigol/Keysight 공통)

- [ ] **그라운드를 짧게**: 15 cm 집게 그라운드는 수십 MHz 이상에서 링잉을 만든다. 디지털 타이밍만 볼 거면 괜찮지만 엣지 품질·크로스토크를 볼 거면 스프링 그라운드. LA도 채널 묶음마다 GND 선을 꼭 연결.
- [ ] **임계 전압**: 1.8 V I/O면 scope 트리거 레벨 0.9 V, LA 임계값 0.9 V(많은 저가 LA는 고정 1.4 V라 1.8 V 로직에서 아슬아슬하다 — 데이터시트 확인).
- [ ] **샘플률**: 측정할 최소 펄스 폭의 4배 이상 샘플. 1 µs 펄스면 ≥ 4 MS/s, 지연 분해능 100 ns가 필요하면 ≥ 10 MS/s. I2C 400 kHz 디코드는 ≥ 4 MS/s, SPI 10 MHz는 ≥ 50 MS/s.
- [ ] **트리거**: CH1(DRDY) 상승 엣지, Normal 모드(Auto 아님). 지연을 보려면 트리거 위치를 화면 왼쪽 10%에.
- [ ] **지연 통계**: Measure → Delay(CH1 상승 → CH2 상승)를 켜고 통계(mean, min, max, std, count)를 수천 회 누적. 이것이 "p99"의 scope 판이다.
- [ ] **무한 잔상(infinite persistence)**: Display → Persistence = Infinite. CH2 엣지가 흩어진 폭이 곧 지터 범위다. 몇 분 돌려 두고 가장 늦은 엣지가 어디까지 가는지 본다.
- [ ] **히스토그램 모드**: Keysight InfiniiVision의 Histogram(측정값 또는 파형 영역), Rigol MSO5000/HDO 계열의 Histogram — 지연 분포를 그대로 그린다. 장비에 없으면 LA로 캡처해 엣지 간격을 스크립트로 히스토그램.
- [ ] **세그먼트 메모리**: 드문 사건을 트리거마다 짧게 수천 개 저장(Keysight Segmented, Rigol "Record/UltraAcquire"). 긴 캡처보다 메모리 효율적이다.
- [ ] **시리얼 디코드**: scope의 I2C/SPI/UART/CAN 디코드 옵션을 켜고, 트리거를 "I2C 주소 0x68 + NACK" 같은 프로토콜 트리거로 걸면 에러만 잡는다.

### 7.3 저가 LA + sigrok (가이드, 명령은 sigrok-cli 문서 기준)

fx2lafw 펌웨어로 도는 Cypress FX2 기반 8채널 LA(24 MHz급, 저가 클론 다수)는 sigrok/PulseView에서 바로 쓴다. 이 VM에는 sigrok-cli가 설치되어 있지 않아 **실행하지 않았다.**

```sh
# 장치 확인
sigrok-cli --scan
# 디버그 핀 4개 + I2C 2선, 4 MHz로 2초 캡처, D0 상승 엣지 트리거
sigrok-cli -d fx2lafw --config samplerate=4m --channels D0,D1,D2,D3,D4,D5 \
  --triggers D0=r --time 2s -o cap.sr
# I2C 디코드 (D4=SCL, D5=SDA), 주석만 출력
sigrok-cli -i cap.sr -P i2c:scl=D4:sda=D5 -A i2c=address-read:address-write:data-read:data-write
# 엣지 시각을 텍스트로 뽑아 스크립트로 분석 (ftrace와 정렬용)
sigrok-cli -i cap.sr -O csv > cap.csv
```

- Saleae Logic 2: 같은 일을 GUI로. "Timing Marker"로 두 채널 간 지연을 재고, 내보내기(Export → CSV)로 엣지 시각을 뽑아 위 `ana.py` 같은 스크립트에 넣는다. Logic 2에는 자동화 API(Python)도 있다.
- 정렬 스크립트는 이 노트의 `ana.py`와 같은 구조다: LA CSV의 디버그 핀 엣지 vs `trace-cmd report`의 `gpio_value` 줄 → median offset → 잔차.

### 7.4 보드에서 커널 쪽 준비 (가이드)

```sh
# 보드에서: mono 시계로, 필요한 이벤트만, 큰 버퍼
cd /sys/kernel/tracing
echo mono > trace_clock
echo 16384 > buffer_size_kb
echo 1 > events/gpio/gpio_value/enable
echo 1 > events/i2c/enable
echo 1 > events/irq/irq_handler_entry/enable
echo 1 > events/sched/sched_wakeup/enable
echo 1 > events/sched/sched_switch/enable
# 또는 trace-cmd 한 줄로 (호스트로 가져가 KernelShark에서 보기)
trace-cmd record -C mono -e gpio:gpio_value -e i2c -e irq:irq_handler_entry -e sched:sched_switch -- ./sensor_app
```

- `trace-cmd record -C mono`가 `trace_clock` 설정과 같다(M5).
- 캡처 순서: LA를 먼저 ARM(트리거 대기) → 트레이스 시작 → 동작 → 트레이스 정지 → LA 정지. 둘 다 sync 펄스를 5개 이상 포함하게.

### 7.5 전원 레일 + current probe와 cpuidle/cpufreq (N5 미리보기)

- current probe(또는 션트 + 차동 프로브)를 VDD_CPU나 보드 입력에 걸고, CH2에 디버그 핀. 전류 파형의 "계단"이 CPU idle 진입·복귀, cpufreq 변경과 맞는지 `power:cpu_idle`, `power:cpu_frequency` tracepoint와 같은 sync로 맞춰 본다.
- "IRQ 지연이 가끔 길다" + "그때마다 전류가 바닥에서 올라오는 중" → 깊은 idle 상태 복귀 지연. `cpuidle` 상태를 제한(PM QoS, `cpu_dma_latency`)해서 재측정(N2, N5).

---

## 8. 커널 안에서는 무슨 일이 일어나나

`gpiod_set_value()` 한 줄이 핀까지 가는 길과, tracepoint가 찍히는 위치(랩 6.18.55 소스 기준):

```
gpiod_set_value(desc, 1)                      drivers/gpio/gpiolib.c
  VALIDATE_DESC(desc)                         NULL -> return 0 (optional pin)
  WARN_ON(desc->gdev->can_sleep)              warns on sleeping chips
  gpiod_set_value_nocheck()
    active-low invert, open-drain/source
    gpiod_set_raw_value_commit()
      trace_gpio_value(gpio, 0, value)        <-- ftrace timestamp taken here
      gpiochip_set(gc, hwgpio, value)
        gc->set()  = pl061_set_value()        drivers/gpio/gpio-pl061.c
          writeb(!!value << off, base + BIT(off + 2))   <-- the pin (= LA) changes here
```

```
IRQ path in this note's experiment:
hrtimer (arch_timer IRQ 11)
  tmr_fn -> irq_set_irqchip_state(PENDING) -> irq_sim_set_irqchip_state()
     -> irq_work_queue() -> self IPI (irq 6)
        irq_sim_handle_irq() -> handle_simple_irq()
           top()                  [hardirq]  returns IRQ_WAKE_THREAD
           __irq_wake_thread()    -> sched_wakeup irq/23-m11_lat (SCHED_FIFO 50)
irq/23-m11_lat: irq_thread() -> thread_fn() -> queue_work(system_highpri_wq)
kworker/N:xH:   process_one_work() -> wk_fn() -> wake_up_interruptible(&wq)
m11_user:       read() returns -> ioctl(GPIO_V2_LINE_SET_VALUES) -> gpiolib-cdev -> gpiod_set_array_value_complex -> pl061
```

드라이버를 쓸 때 다시 만나는 지점:

- `request_threaded_irq()` + `IRQF_ONESHOT`의 마스크 구간이 곧 "엣지 유실 창"이다(F6, H4 §8.1). 고속 DRDY는 hardirq에서 타임스탬프만 찍고(`iio_get_time_ns()` 같은 패턴, I2) 나머지를 thread로.
- `queue_work()`의 PENDING 합쳐짐, 플래그 하나로 유저를 깨우는 구조의 합쳐짐 — 유실 없이 넘기려면 kfifo/링버퍼(A4, I2의 IIO buffer)로 샘플마다 저장한다.
- GPIO 컨트롤러 드라이버(provider)를 쓴다면, `.set`을 MMIO 한 번으로, 가능하면 `.set_multiple`도 구현해 디버그 버스를 원자적으로 만든다(H4 §4).

---

## 9. Embedded 실무에서 다시 보기

### 9.1 IMU 샘플이 가끔 늦는다 — 센서인가 커널인가

센서 키트의 IMU(SPI, 1 kHz DRDY)가 "가끔 2 ms 늦게 온다"는 보고. LA에 DRDY, SPI CS, 그리고 드라이버의 hardirq·thread에 넣은 디버그 핀 2개. 결과가 "DRDY 주기는 1000.0 ± 0.3 µs, DRDY→HARD는 항상 8 µs, HARD→THREAD가 가끔 1.9 ms"라면 범인은 irq thread 스케줄링이다. 그 시각대의 `sched_switch`를 보면 같은 CPU에서 SCHED_FIFO 우선순위가 더 높은 다른 스레드(예: 카메라 파이프라인)가 돌고 있다. 해결: irq thread 우선순위/affinity 조정(N2).

### 9.2 "I2C가 가끔 -110을 뱉는다"

`i2c_result` tracepoint에 `ret < 0` 필터를 걸고(H5 §7), 드라이버 에러 경로에 디버그 핀 펄스 한 줄. LA를 그 핀에 트리거하고 SCL/SDA는 프리트리거 10 ms. 캡처를 보니 에러 직전 SCL이 3 ms 동안 Low — 센서의 클럭 스트레칭이 컨트롤러 타임아웃보다 길다. 커널 로그만으로는 "타임아웃"까지만 알 수 있었다.

### 9.3 부팅 시간: 커널 타임스탬프 0초는 언제인가

M10의 `initcall_debug`나 printk 타임스탬프의 0은 "커널 타이머가 시작된 시점"이지 전원 인가 시점이 아니다. 전원 레일 상승(scope CH1)과 U-Boot·커널의 특정 지점에 넣은 GPIO 펄스(CH2)를 같이 찍으면, ROM·SPL·U-Boot·커널·유저 각 구간을 **전원 기준 절대 시간**으로 잴 수 있다. "카메라 첫 프레임까지 2초" 같은 요구사항은 이 방법으로 증명한다.

### 9.4 DMA로 바꿨더니 더 느려졌다

SPI를 DMA로 바꿨는데 짧은 전송(4바이트)이 오히려 느려졌다. scope에 CS와 SCLK를 띄우면 CS 하강 → 첫 SCLK 사이 공백이 PIO 때보다 40 µs 길다 — DMA 디스크립터 준비·캐시 flush 비용. `spi_transfer_start` 시각과 CS 엣지의 차이로 확인하고, 드라이버의 DMA 임계 길이(많은 SPI 컨트롤러 드라이버의 `can_dma` 콜백이 길이로 판단)를 확인한다(H7, H9).

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 디버그 핀을 I2C GPIO 확장기에 배치 | ISR에서 쓰면 WARN, 펄스가 수백 µs 늦거나 아예 안 나옴 | `can_sleep = true` 칩 | SoC 내장 GPIO(MMIO)에 배치 |
| ftrace 기본 시계(`local`)로 찍고 유저 로그와 비교 | 1초 넘게 어긋남 | local은 CLOCK_MONOTONIC과 0점이 다름 | `echo mono > trace_clock` 또는 `trace-cmd -C mono` |
| sync 엣지 하나로 정렬 | 모든 이벤트가 일정하게 수십~수백 µs 틀어짐 | 첫 엣지가 cold cache 등으로 이상치 | 엣지 여러 개 + 중앙값 offset |
| tracepoint 시각을 "사건 시각"으로 믿음 | 로그상 순서와 핀 순서가 다름, 가끔 ms 차이 | tracepoint는 하드웨어 동작 전·후에 찍힘, 그 사이 선점 | 핀으로 확인. hardirq 문맥 펄스는 잔차가 작다 |
| `i2c_reply` 시각을 데이터 수신 시각으로 사용 | 센서 타임스탬프가 수백 µs 늦음 | reply는 전송 완료 후 기록 | DRDY IRQ에서 timestamp(IIO 패턴) |
| LA 샘플률 부족 | 짧은 펄스가 사라지거나 폭이 들쭉날쭉 | 샘플 간격 > 펄스 폭 | 샘플률 ≥ 최소 펄스 폭 역수의 4배 |
| LA 그라운드 미연결·임계값 불일치 | 디코드 에러, 가짜 엣지 | 1.8 V 로직에 고정 1.4 V 임계 등 | GND 연결, 임계값 설정, 레벨 시프터 |
| 평균만 보고 | "평균 300 µs라 OK"인데 현장에서 데드라인 미스 | 꼬리(p99, max)를 안 봄 | 히스토그램·persistence·p99/max 보고 |
| 트레이스를 잔뜩 켜고 지연 측정 | 측정할 때만 느림(관찰자 효과) | 이벤트 기록 비용 | 필요한 이벤트만, 지연 측정 자체는 핀으로 |
| 단계별 엣지 개수를 안 셈 | "가끔 샘플 유실"의 위치를 못 찾음 | 합쳐짐(ONESHOT 마스크, work PENDING, 플래그) | 채널별 엣지 수 비교 → 유실 단계 특정 |

---

## 11. 면접에서 이렇게 말한다

**Q.** How do you prove the kernel adds 200 µs of jitter and not the sensor?

**A.** 센서의 DRDY 핀과, 드라이버의 hardirq·irq thread·유저 복귀 지점에 넣은 디버그 GPIO를 같은 LA에 물린다. 같은 하드웨어 시계로 DRDY 주기 지터(센서 몫)와 DRDY→각 단계 지연 분포(커널 몫)를 따로 잰다. 지터가 생기는 구간을 특정한 뒤, 그 시각대의 ftrace(sched_switch, irqsoff/wakeup tracer)로 원인을 찾고, 같은 sync 엣지로 두 기록을 정렬한다.

> I put the sensor's data-ready line and debug GPIOs from my hardirq handler, the IRQ thread and the user-space wakeup on the same logic analyzer, so everything is measured with one hardware clock. The DRDY period jitter is the sensor's share; the DRDY-to-stage delays are the kernel's. If DRDY is stable to a few microseconds and the HARD-to-THREAD delay has a 200 µs tail, it's scheduling. Then I trace that window with sched_switch and the wakeup tracer, aligned to the GPIO edges, to name the task that held the CPU.

**꼬리 질문**: "디버그 핀 토글 자체가 지연을 바꾸지 않나?" → MMIO 쓰기 한 번(수십~수백 ns)이라 트레이스보다 훨씬 가볍고, 핀 유무로 A/B 측정해 영향을 확인한다.

**Q.** How do you correlate a logic analyzer capture with kernel logs?

**A.** 양쪽이 모두 기록하는 사건을 만든다 — 커널이 토글하는 GPIO(ftrace `gpio_value`나 trace_marker와 같은 순간)나 SPI CS. ftrace는 `mono` 시계로 찍는다. 여러 sync 엣지의 (LA − trace) 중앙값으로 offset을 잡고, 긴 캡처면 드리프트도 보정한다. 그다음 버스 트랜잭션은 내용(주소·레지스터·데이터)으로 짝짓고 시각으로 확인한다. i2c_write는 버스 전, i2c_reply는 버스 후에 찍힌다는 점을 감안한다.

> I create a common event: a GPIO the kernel toggles, which shows up both as an edge on the analyzer and as a gpio_value event or trace_marker in ftrace, with trace_clock set to mono. I use several sync edges and take the median offset, because a single edge can be an outlier, and for long captures I correct drift with periodic pulses. Then I pair bus transactions by content — address, register, data — and only use time to confirm, knowing that i2c_write is logged before the bus activity and i2c_reply after it.

**Q.** Why can't you toggle a GPIO on an I2C expander from an interrupt handler?

**A.** 그 칩은 핀을 바꾸려면 I2C 전송이 필요하고 I2C 전송은 잠들 수 있다. gpiolib은 그런 칩을 `can_sleep`으로 표시하고, `gpiod_set_value()`를 그런 칩에 부르면 WARN을 낸다. 프로세스 컨텍스트에서 `gpiod_set_value_cansleep()`을 쓰거나, 디버그 핀은 처음부터 SoC 내장 GPIO에 둔다.

> Setting that pin means an I2C transfer, which can sleep, so gpiolib marks the chip can_sleep and gpiod_set_value() warns if you call it on such a chip. In process context you use the _cansleep variant; for debug pins I always pick SoC GPIOs that are plain MMIO, so they're usable in hardirq.

**Q.** Your kernel trace says the GPIO went high at t, but the scope shows it 2 ms later. How is that possible?

**A.** tracepoint는 드라이버 콜백을 부르기 **직전**에 찍힌다. 그 사이 선점이나 IRQ, 가상화 환경이면 vCPU 선점, 실보드면 SMI·하이퍼바이저·버스 쓰기 버퍼가 끼면 벌어진다. 랩에서도 유저 문맥 핀은 최대 14.8 ms, hardirq 문맥 핀은 최대 84 µs 벌어졌다. 그래서 타이밍의 ground truth는 핀이다.

> The gpio_value tracepoint fires right before gpiolib calls the chip's set callback. If the task is preempted in between — or the vCPU, or an SMI on x86 — the log time and the pin time diverge. In my QEMU lab, pins set from process context diverged by up to 14.8 ms, pins set in hardirq by under 0.1 ms. That's why I treat the pin as ground truth.

**Q.** Which ftrace clock do you use and why?

**A.** 다른 것과 맞출 거면 `mono`. 기본 `local`은 빠르지만 CLOCK_MONOTONIC과 0점이 달라 랩에서 1.1초 어긋났다. suspend를 넘으면 `boot`, PTP로 동기화된 여러 장비면 `tai`. 순서만 필요하면 `counter`.

> For anything I'll correlate, mono — it's the same timeline as CLOCK_MONOTONIC in user space. The default local clock is faster but has a different zero; in my lab it was off by over a second. Across suspend I use boot, across PTP-synced machines tai.

**Q.** Interrupts are being lost at high rates. How do you find where?

**A.** 단계마다 핀을 두고 채널별 엣지 수를 센다. 랩에서 200개 외부 엣지가 HARD 198 → THREAD 174 → WORK 161 → USER 147로 줄었다 — ONESHOT 마스크 중 유실, thread 재실행 합쳐짐, work PENDING 합쳐짐, 플래그 합쳐짐. 수정은 hardirq에서 timestamp만 찍고 FIFO에 넣는 것.

> I put a pin on each stage and count edges per channel. In my lab 200 external edges became 198 hardirqs, 174 thread runs, 161 work items and 147 user wakeups — each stage coalesces: the line is masked during a oneshot thread, a pending work item isn't queued twice, a single flag merges wakeups. The fix is to timestamp in hardirq and push each sample into a FIFO.

**Q.** What's the difference between a scope and a logic analyzer for this work?

**A.** scope는 채널이 적지만 아날로그 품질(링잉, 레벨, 전류)과 지연 통계·persistence·히스토그램이 강하다. LA는 채널이 많고 긴 캡처와 프로토콜 디코드가 강하다. 지연 분포는 scope, 상태 머신·버스 상관은 LA, 둘을 트리거 공유로 묶는다.

> A scope gives me analog truth — edges, levels, current — plus built-in delay statistics, infinite persistence and histograms, but only four channels. A logic analyzer gives many channels, long captures and protocol decoders. I use the scope for latency distributions and power, the LA for state machines and bus correlation, and I tie them together with a shared trigger.

---

## 12. 직접 해보기

1. **개념**: `gpio_value` tracepoint 시각과 실제 핀 엣지가 어긋날 수 있는 이유 세 가지를 쓰고, 어느 문맥(hardirq vs 프로세스)에서 어긋남이 더 큰지 설명하라.
   정답/힌트: tracepoint는 `gpiochip_set()` 호출 전. 선점, IRQ, vCPU/SMI, 버스 쓰기 버퍼. hardirq는 선점 불가라 작다(§5.3 표: HARD 최대 84 µs vs USER 14.8 ms).
2. **코드**: `m11_lat`에 "SOFTIRQ 단계"를 추가하라 — thread 대신 tasklet 또는 `BH` workqueue(`system_bh_wq`)로 처리하는 경로에 PL061 line 6 펄스를 넣고 HARD→BH 지연을 비교.
   정답/힌트: `queue_work(system_bh_wq, ...)`는 softirq 문맥에서 돈다(F7). 스케줄러를 안 거치므로 threaded보다 짧고 지터가 작을 것.
3. **코드**: `m11_user`를 SCHED_FIFO 60으로 올리고(`sched_setscheduler`) `mlockall` 후 WORK→USER 분포가 어떻게 바뀌는지 재라.
   정답/힌트: N2. 부하(`LOAD=1`) 상태에서 차이가 커야 한다.
4. **관찰**: `trace_clock`을 `local`로 두고 §5.3 정렬을 다시 하라. offset이 어떻게 바뀌고, 드리프트가 0으로 나오는가?
   정답/힌트: offset만 바뀌고(약 1.1 s 차이) 드리프트·잔차는 비슷해야 한다 — 0점이 다를 뿐 같은 하드웨어 카운터다.
5. **관찰**: `g_vis.sh`에 `echo 'hist:keys=common_pid.execname:vals=hitcount' > events/gpio/gpio_value/trigger`를 추가해 누가 핀을 가장 많이 토글했는지 보라.
   정답/힌트: m11_lat 실험이면 `irq/23-m11_lat`, `kworker/...H`, `m11_user`, `<idle>`(hardirq 문맥)이 나온다.
6. **설계**: 3핀 병렬 상태 버스로 IMU 드라이버의 상태(IDLE, IRQ, SPI_XFER, PARSE, PUSH)를 내보내는 코드를 쓰고, PL061에 `set_multiple`이 없을 때 생기는 중간 상태 글리치를 MMIO 한 번 쓰기로 피하라.
   정답/힌트: PL061 GPIODATA의 주소 마스크 `base + ((BIT(a)|BIT(b)|BIT(c)) << 2)`에 한 번 `writeb`.
7. **실보드 숙제**: 실제 보드에서 §2.4를 반복해 `gpiod_set_value` vs `writel` 비용을 재라. TCG 결과(차이 없음)와 비교해 설명하라.
   정답/힌트: 실보드에서는 MMIO 쓰기 자체가 짧아 gpiolib 경로(검증·SRCU 가드·active-low 처리·tracepoint 분기)가 상대적으로 드러난다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| ground truth | 기준이 되는 참값 | 지연·순서는 핀/버스의 전기 신호가 참값 |
| 디버그 GPIO | 측정용 핀 | 코드 지점마다 토글해 LA/scope로 시각을 읽는다 |
| sync 엣지 | 동기 사건 | 두 기록(LA, 트레이스)에 모두 남는 같은 사건 |
| offset | 두 시계의 차 | median(LA 시각 − 트레이스 시각) |
| drift | 시계 속도 차 | ppm 단위, 긴 캡처에서 offset이 변하는 정도 |
| residual (잔차) | 정렬 후 남는 차이 | 커널 timestamp와 실제 핀 시각의 어긋남 |
| `trace_clock` | ftrace 시계 선택 | local/global/mono/boot/tai 등 |
| `trace_marker` | 유저→ftrace 기록 | 유저가 쓴 문자열이 트레이스에 이벤트로 들어간다 |
| `gpio_value` | gpiolib tracepoint | 핀 set/get 직전 기록, "가상 LA" |
| `can_sleep` | gpio_chip 속성 | 핀 접근이 잠들 수 있음 → hardirq에서 사용 불가 |
| irq_sim | 소프트웨어 IRQ 칩 | gpio-sim이 쓰는 IRQ 도메인, irq_work로 핸들러 호출 |
| `IRQF_ONESHOT` | IRQ 플래그 | thread가 끝날 때까지 IRQ를 마스크 |
| coalescing (합쳐짐) | 여러 사건 → 한 번 처리 | 마스크·PENDING 비트·플래그 때문에 사건이 사라진다 |
| infinite persistence | scope 표시 모드 | 모든 트리거의 파형을 겹쳐 지터 범위를 보여 준다 |
| segmented memory | scope 기록 모드 | 트리거마다 짧은 구간을 수천 개 저장 |
| protocol decoder | LA 기능 | SCL/SDA 등 파형을 바이트·프레임으로 해석 |
| fx2lafw | 오픈 LA 펌웨어 | Cypress FX2 기반 저가 LA를 sigrok에서 쓰게 해 준다 |
| TCG | QEMU 에뮬레이션 방식 | 명령을 번역 실행 — 절대 시간 측정은 믿을 수 없다 |

---

## 14. 요약 & 체크리스트

커널 timestamp는 "코드가 그 줄을 지난 시각"이고 핀은 "전기가 바뀐 시각"이다. 지연·지터·유실을 증명하려면 외부 신호와 각 커널 단계(hardirq, irq thread, workqueue, 유저 복귀)에 디버그 GPIO를 두고 같은 LA/scope 시계로 잰다. 이 랩에서는 PL061 MMIO GPIO를 디버그 핀으로, QEMU의 `pl061_set_output` trace를 외부 LA로, ftrace `gpio_value`를 가상 LA로 써서 단계별 지연 분포와 유실 지점(200 → 198 → 174 → 161 → 147)을 핀 개수로 찾았다. 두 시간축은 여러 sync 엣지의 중앙값 offset으로 맞추고(첫 엣지 하나로 맞추면 87~376 µs 틀어졌다), 정렬 후 남는 잔차가 커널 timestamp의 "거짓말"(hardirq 문맥 최대 84 µs, 프로세스 문맥 최대 14.8 ms)이다. 버스는 i2c/spi/regmap tracepoint와 LA 디코드를 내용으로 짝짓고 시각으로 확인하되, 요청 이벤트는 버스 전, 응답 이벤트는 버스 후에 찍힌다는 것을 기억한다. 절대 수치는 TCG라 실보드와 다르다 — 가져갈 것은 방법이다.

- [ ] 커널 timestamp와 핀 엣지가 어긋나는 이유를 gpiolib 코드 경로로 설명할 수 있다
- [ ] 디버그 핀을 SoC MMIO GPIO에 둬야 하는 이유(can_sleep)를 말할 수 있다
- [ ] hardirq·threaded IRQ·workqueue·유저 복귀에 핀을 심는 모듈을 짤 수 있다
- [ ] 채널별 엣지 수로 유실 단계를 찾고, 각 단계의 합쳐짐 원인을 말할 수 있다
- [ ] 펄스 폭·펄스 개수·병렬 상태 버스 인코딩을 골라 쓸 수 있다
- [ ] `trace_clock`의 종류와 `mono`를 쓰는 이유를 설명할 수 있다
- [ ] trace_marker로 유저 시계와 ftrace 시계를 비교할 수 있다
- [ ] 여러 sync 엣지의 중앙값으로 offset·드리프트를 구하고 잔차를 해석할 수 있다
- [ ] i2c/spi/regmap tracepoint가 버스 활동의 전·후 어디에 찍히는지 말할 수 있다
- [ ] LA 디코드와 커널 트레이스를 대조표로 만들 수 있다
- [ ] scope의 delay 통계·persistence·히스토그램·세그먼트 메모리로 지연 분포를 잴 수 있다
- [ ] "커널이 200 µs 지터를 더한다"를 센서와 분리해 증명하는 절차를 영어로 설명할 수 있다

## 참고 자료

- docs.kernel.org — GPIO Descriptor Consumer Interface: https://docs.kernel.org/driver-api/gpio/consumer.html
- docs.kernel.org — GPIO Testing Driver (gpio-sim): https://docs.kernel.org/admin-guide/gpio/gpio-sim.html
- docs.kernel.org — ftrace (trace_clock, trace_marker): https://docs.kernel.org/trace/ftrace.html
- docs.kernel.org — Event Histograms (hist triggers): https://docs.kernel.org/trace/histogram.html
- docs.kernel.org — Event Tracing: https://docs.kernel.org/trace/events.html
- elixir.bootlin.com — `drivers/gpio/gpiolib.c` (`gpiod_set_raw_value_commit`), `drivers/gpio/gpio-pl061.c`, `kernel/irq/irq_sim.c`, `include/trace/events/i2c.h`, `include/trace/events/spi.h`, `kernel/trace/trace.c` (`trace_clocks[]`): https://elixir.bootlin.com/linux/v6.18/source
- QEMU 문서 — Tracing (`-trace`, `-D`): https://www.qemu.org/docs/master/devel/tracing.html
- sigrok — sigrok-cli 문서와 fx2lafw: https://sigrok.org/wiki/Sigrok-cli , https://sigrok.org/wiki/Fx2lafw
- ARM — PrimeCell GPIO (PL061) Technical Reference Manual (GPIODATA 주소 마스크)
- 관련 노트: H4(gpio-sim, edge 이벤트 지연), H5(i2c tracepoint), H7(SPI), H2(regmap), H9(DMA), F6/F7(IRQ·deferred work), M5(ftrace), M10(부팅 분석), N1~N3(실시간·지연 측정), N5(전원 관리)
