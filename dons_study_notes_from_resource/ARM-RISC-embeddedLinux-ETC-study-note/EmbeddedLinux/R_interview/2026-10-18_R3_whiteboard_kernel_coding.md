# R3. 화이트보드 코딩 (커널 스타일) — 라이브 코딩 7문제를 "말하면서 45분 안에" 쓰기

> **이 노트를 다 읽으면**: misc char driver(read/write/ioctl/poll + wait queue)를 백지에서 쓸 수 있다 · platform driver + DT + `devm_*` + IRQ를 순서와 이유까지 말할 수 있다 · SPSC 링 버퍼를 acquire/release로 짜고 왜 틀린 버전이 실제로 깨지는지 보여 줄 수 있다 · `container_of`·비트 필드·레지스터 구조체 질문에서 함정을 먼저 짚을 수 있다
> **왜 필요한가**: Embedded Linux 면접의 코딩 라운드는 LeetCode가 아니라 "드라이버 뼈대를 칠판에 써 봐", "ISR과 스레드가 공유하는 링 버퍼를 짜 봐"다. Scout AI·Anduril·Neros·Rivian 모두 이 형태가 나온다
> **Don 기준 난이도**: 링 버퍼·비트 조작·레지스터는 이미 손에 익었다(Anduril 문제 은행) / 새로 몸에 붙일 것은 **커널 API 이름, 컨텍스트 규칙(잠들 수 있나), devm 순서, 말로 설명하는 습관**
> **선행 노트**: E5, F3, F4, F5, F7, F8, G3, G4, G6, G7, H1, H2, H5 (이 노트는 그 노트들을 "시험장 버전"으로 압축한다)

---

## 0. 큰 그림 — 커널 스타일 라이브 코딩에서 면접관이 보는 것

라이브 코딩 라운드에서 면접관은 "컴파일되는 완벽한 코드"보다 **세 가지**를 본다.

1. **컨텍스트 감각** — 이 코드가 hardirq에서 도나, 프로세스 컨텍스트에서 도나? 잠들어도 되나? 어떤 락을 써야 하나? (F2, F4)
2. **자원 수명** — probe가 중간에 실패하면 무엇이 풀리나? remove와 IRQ가 겹치면? (G3 §3, §8)
3. **유저 계약** — `read()`가 0, 음수, 짧은 값을 언제 돌려주나? `O_NONBLOCK`이면? 시그널이 오면? (G4 §6, F3 §4)

Don은 bare-metal에서 링 버퍼·레지스터·ISR을 수백 번 짰다. 그 지식은 그대로 쓴다. 다른 점은 **Linux에는 "잠들 수 있는 곳"과 "없는 곳"이 나뉘어 있고, 자원을 커널 객체 수명에 묶어야 한다**는 것뿐이다. 이 노트의 7문제는 전부 그 두 가지를 시험한다.

| # | 문제 | 시험하는 것 | 실행 검증 | 원본 노트 |
|---|---|---|---|---|
| 1 | misc char driver: read/write/ioctl/poll | wait queue, `-ERESTARTSYS`, `O_NONBLOCK`, `copy_to_user` | QEMU 게스트 + 정적 테스트 바이너리 | G4, G5, G6, G7 |
| 2 | platform driver + DT + IRQ + regmap | probe 순서, devm 역순 해제, ISR 규칙 | QEMU 게스트, 가짜 IRQ 주입 | D3, D8, G3, H2 |
| 3 | lock-free SPSC 링 버퍼 + IRQ-safe 변형 | acquire/release 짝, 2의 거듭제곱, kfifo 비교 | 커널 kthread 2개 + 유저 2스레드 스트레스 | F4, F5 |
| 4 | `container_of` + `list_head` | 오프셋 계산, 타입 검사, `_safe` 순회 | 유저 | E5 §1–2 |
| 5 | 비트 조작 + RMW 경쟁 | GENMASK/FIELD_PREP, 필드 넘침, 공유 레지스터 | 유저 + 커널 빌드 에러 | E5 §5, H1 §6 |
| 6 | 레지스터 구조체 vs 오프셋 | 패딩, packed, volatile의 한계, `readl` | 유저 + objdump | H1 §3, §8, F5 §5 |
| 7 | 엑스트라: 디바운스/hrtimer, I2C client | delayed work, hrtimer 컨텍스트, SMBus 헬퍼 | QEMU 게스트 + i2c-stub | F7, F8, H5 |

### 0.1 Anduril 문제 은행과의 관계 — 중복하지 않는다

Don의 C 문제 은행(`job_interview_prep_research/anduril_firmware_engineer/`, 111문제)에 이미 **순수 C 버전**이 있는 것은 여기서 다시 풀지 않고 번호로 가리킨다. 이 노트는 그 위에 **커널 API와 컨텍스트 규칙**을 얹는다.

| 은행에 이미 있는 것 (순수 C) | 위치 | 이 노트에서 더하는 것 |
|---|---|---|
| set/clear/toggle/test bit, range mask, set_field | `problems/01_bit_basics.c` 1–4, 11–17 | `BIT()`이 `unsigned long`이라는 함정, `FIELD_PREP`의 컴파일 타임 검사 (§6) |
| popcount, swap_endian32, get/set_field | `problems/02_bit_advanced.c` 12, 21, 22 | `swab32`/`cpu_to_be32`, `get_unaligned_be16` (§6, §8.2) |
| volatile 폴링, 구조체 패딩 | `problems/04_memory.c` 49, 54 | `__iomem` + `readl`, packed가 만드는 바이트 단위 접근 (§7) |
| SPSC 무락 링 (C11), MPMC mutex+condvar | `problems/05_circular_buffer.c` Q64, Q65 | `smp_load_acquire`/`smp_store_release`, kfifo, hardirq 생산자 (§4) |
| 정적 노드 풀 연결 리스트 | `problems/06_linked_list.c` Q66–Q75 | intrusive `list_head` + `container_of` (§5) |
| MMIO + volatile, ISR 공유 자원 | `problems/09_rtos_embedded.c` Q98, Q101 | `spin_lock_irqsave`, regmap (§3, §4.6) |
| reg_set_field, UART ISR 링, GPIO 디바운스 | `problems/10_anduril_signature.c` Q4, Q5, Q8 | 커널의 delayed work 디바운스 (§8.1) |

> 연습 순서 추천: 은행의 C 버전을 10분 안에 맨손으로 쓸 수 있으면, 이 노트의 같은 주제(커널 버전)로 넘어온다. 커널 버전은 "C 버전 + 컨텍스트 한 줄 설명"이 핵심이다.

---

## 1. 실습 환경 — 모든 코드는 실제로 빌드하고 돌렸다

- 빌드: VM(`uname -r` = `7.0.0-38-generic`, gcc 15.2) 안에서 랩 트리 `/var/lab/out/linux-build-trace`(Linux **6.18.55-trace**)에 대해 `W=1`로 빌드. 경고 0.
- 실행: `Image-trace` + BusyBox initramfs, QEMU `virt` `-smp 2 -m 1G`, TCG(에뮬레이션). **TCG 시간은 실제 하드웨어 수치가 아니다.**
- 유저 공간 코드: VM에서 `gcc -std=gnu11 -Wall -Wextra -O2`. VM은 Apple M2 위의 Lima라서 **유저 공간 스레드 실험은 진짜 arm64 코어**에서 돈다(메모리 순서 실험이 의미 있다).

### 1.1 DT 조각 — 빈 virtio-mmio 슬롯을 "센서"로 빌린다

QEMU `virt`에는 우리가 원하는 센서가 없다. G3 §2.1과 같은 방법을 쓴다: 비어 있는 virtio-mmio 슬롯(0x0a003e00, SPI 47)을 DT에서 지우고 그 자리에 우리 노드를 넣는다. 슬롯의 0번 레지스터는 항상 `0x74726976`("virt")이라 **칩 ID**로 쓸 수 있다. DT는 `-m 1G -smp 2`로 다시 덤프했다(`-dtb`를 줄 때 메모리·CPU 수가 같아야 한다 — 랩 README).

```dts
/* r3_nodes.dtsi — virt DT 끝에 #include: 가짜 센서(빈 virtio-mmio 슬롯을 빌린다) */
#include <dt-bindings/interrupt-controller/arm-gic.h>

/ {
	/delete-node/ virtio_mmio@a003e00;

	r3-sensor@a003e00 {
		compatible = "don,r3-sensor";
		reg = <0x0 0x0a003e00 0x0 0x200>;
		interrupts = <GIC_SPI 47 IRQ_TYPE_EDGE_RISING>;
		don,debounce-ms = <20>;
	};
};
```

```sh
#!/bin/sh
set -e
W=/var/tmp/R3; K=/var/lab/src/linux-6.18.55/include
cp $W/virt1g.dts $W/r3_virt.dts
echo '#include "/Users/donh/.elinux_scratch/R3/r3_nodes.dtsi"' >> $W/r3_virt.dts
cpp -nostdinc -I $K -undef -x assembler-with-cpp $W/r3_virt.dts > $W/r3.pp.dts
dtc -q -I dts -O dtb -o $W/r3.dtb $W/r3.pp.dts
fdtget -t s $W/r3.dtb /r3-sensor@a003e00 compatible
fdtget $W/r3.dtb /r3-sensor@a003e00 interrupts
```

```text
don,r3-sensor
0 47 1
```

출력에서 볼 것: 컴파일된 dtb에서 노드를 다시 읽었다. `interrupts`가 `<0 47 1>` = GIC_SPI 47, 상승 엣지다. `don,`은 등록되지 않은 vendor prefix라서 실제 업스트림이라면 `dt_binding_check`가 거부한다(D7).

### 1.2 빌드·부팅 스크립트

무엇을 확인하나: 모듈 5개(+업스트림 `i2c-stub.c`를 out-of-tree로 빌드)를 initramfs에 넣고, `LAB_CMD`로 시나리오 하나를 돌린 뒤 자동 종료한다.

```sh
#!/bin/bash
# run.sh <case> [smp] — rebuild initramfs, boot Image-trace with r3.dtb
set -e
W=/var/tmp/R3; S=/Users/donh/.elinux_scratch/R3; C=$1; SMP=${2:-2}
rm -rf $W/rootfs; cp -a /var/lab/build/rootfs $W/rootfs; mkdir -p $W/rootfs/r3
cp $W/mod/*.ko $W/stub/i2c-stub.ko $W/bin/* $W/rootfs/r3/ 2>/dev/null || true
cp $S/guest.sh $W/rootfs/r3/; chmod +x $W/rootfs/r3/guest.sh
(cd $W/rootfs && find . | cpio -o -H newc --quiet | gzip) > $W/r3_initrd.gz
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp $SMP -m 1G -nographic \
  -kernel /var/lab/out/Image-trace -initrd $W/r3_initrd.gz -dtb $W/r3.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=/r3/guest.sh~$C" 2>&1 | tr -d '\r' | grep -v "^\[ *[0-9.]*\] reboot\|^$"
```

```sh
#!/bin/sh
# guest.sh <case> — runs inside the BusyBox initramfs guest
cd /r3
case "$1" in
chr)
	insmod r3_chr.ko && ls -l --color=never /dev/r3_chr && grep r3_chr /proc/misc
	./r3_chr_test
	rmmod r3_chr && echo "rmmod ok" ;;
plat)
	insmod r3_plat.ko
	D=/sys/bus/platform/devices/a003e00.r3-sensor
	ls -l --color=never $D/driver; grep r3 /proc/iomem; grep r3 /proc/interrupts
	echo 5 > $D/fire; sleep 1; cat $D/stats
	echo 1 > $D/fire; sleep 1; cat $D/stats
	grep r3 /proc/interrupts
	echo a003e00.r3-sensor > /sys/bus/platform/drivers/r3_sensor/unbind
	grep r3 /proc/iomem /proc/interrupts || echo "unbind: iomem+irq released" ;;
spsc)
	insmod r3_spsc.ko mode=0; rmmod r3_spsc
	insmod r3_spsc.ko mode=1; rmmod r3_spsc ;;
tmr)
	insmod r3_tmr.ko ;;
i2c)
	insmod i2c-stub.ko chip_addr=0x68
	ls --color=never /sys/bus/i2c/devices/
	i2cset -y 0 0x68 0x75 0x68; i2cset -y 0 0x68 0x6b 0x40
	i2cset -y 0 0x68 0x3b 0x01 0x00 0xff 0x38 0x40 0x00 i
	insmod r3_i2c.ko
	echo r3-imu 0x68 > /sys/bus/i2c/devices/i2c-0/new_device
	i2cget -y -f 0 0x68 0x6b
	ls -l --color=never /sys/bus/i2c/devices/0-0068/driver ;;
esac
dmesg | grep -E "r3|i2c" | grep -viE "command line|LAB_CMD|taint|i2c_dev" | tail -14
```

```sh
cd /var/tmp/R3/mod && make -C /var/lab/out/linux-build-trace M=$PWD W=1 modules > build.log 2>&1
grep -E "CC \[M\]  r3_[a-z0-9]*\.o" build.log; echo "warnings: $(grep -ci warning build.log)"
```

```text
  CC [M]  r3_chr.o
  CC [M]  r3_plat.o
  CC [M]  r3_spsc.o
  CC [M]  r3_tmr.o
  CC [M]  r3_i2c.o
warnings: 0
```

출력에서 볼 것: `W=1`(추가 경고 세트)로도 경고 0. `checkpatch.pl -f`도 돌렸다: 에러 0, 경고는 "SPDX 줄 없음"과 "파일 이름 주석"뿐이다(노트의 블록 표시용 주석이라 남겼다). 실제 패치라면 첫 줄은 `// SPDX-License-Identifier: GPL-2.0`이어야 한다(P7).

---

## 2. 문제 1 — misc char driver: read/write/ioctl + poll + wait queue

### 2.1 면접관은 이렇게 말한다

> "Write a minimal character driver. Userspace writes bytes into it, another process reads them back. Reads should block when there is no data, support non-blocking mode and poll/select. Add an ioctl that returns how many bytes are buffered and one that clears the buffer."

### 2.2 먼저 물어볼 것 (30초)

| 질문 | 왜 묻나 | 이 풀이의 가정 |
|---|---|---|
| 장치는 하나인가, 인스턴스가 여러 개인가? | 정적 구조체 vs probe에서 할당 | 하나 → 정적 `struct r3_dev` |
| 여러 프로세스가 동시에 읽고 쓰나? | 락 종류와 범위 | 그렇다 → mutex 하나 |
| 데이터 생산자가 IRQ인가? | IRQ면 mutex 불가, spinlock + kfifo | 아니다(유저 write). IRQ 버전은 §4.6 |
| 버퍼가 가득 차면 write는 막나, 버리나? | 블로킹 정책 | 막는다(`O_NONBLOCK`이면 `-EAGAIN`) |
| 메시지 경계가 있나(패킷)? | 바이트 스트림 vs 레코드 | 바이트 스트림 |

### 2.3 처음 5분에 칠판에 쓸 뼈대

먼저 이것만 쓰고 "이제 read부터 채우겠다"고 말한다. 뼈대가 있으면 시간이 모자라도 구조 점수는 받는다.

```c
struct r3_dev {
	struct miscdevice misc;		/* container_of로 되찾을 멤버 */
	struct mutex lock;		/* fifo 보호, 프로세스 컨텍스트 전용 */
	DECLARE_KFIFO(fifo, u8, 64);
	wait_queue_head_t rq, wq;	/* 데이터 기다림 / 공간 기다림 */
};
static ssize_t r3_read(struct file *f, char __user *buf, size_t n, loff_t *pos);
static ssize_t r3_write(struct file *f, const char __user *buf, size_t n, loff_t *pos);
static __poll_t r3_poll(struct file *f, poll_table *pt);
static long r3_ioctl(struct file *f, unsigned int cmd, unsigned long arg);
static const struct file_operations r3_fops = { .owner = THIS_MODULE, /* ... */ };
/* init: mutex_init, INIT_KFIFO, init_waitqueue_head x2, misc_register (마지막) */
```

### 2.4 그림 — 블로킹 read가 깨어나는 순서

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="30" y="10" width="130" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="30" font-size="13" text-anchor="middle">Reader 스레드</text>
  <rect x="200" y="10" width="130" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="265" y="30" font-size="13" text-anchor="middle">r3_read()</text>
  <rect x="370" y="10" width="130" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="435" y="30" font-size="13" text-anchor="middle">r3_write()</text>
  <rect x="535" y="10" width="130" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="600" y="30" font-size="13" text-anchor="middle">Writer 스레드</text>
  <line x1="95" y1="40" x2="95" y2="345" stroke="currentColor" stroke-dasharray="4 4" opacity="0.5"/>
  <line x1="265" y1="40" x2="265" y2="345" stroke="currentColor" stroke-dasharray="4 4" opacity="0.5"/>
  <line x1="435" y1="40" x2="435" y2="345" stroke="currentColor" stroke-dasharray="4 4" opacity="0.5"/>
  <line x1="600" y1="40" x2="600" y2="345" stroke="currentColor" stroke-dasharray="4 4" opacity="0.5"/>
  <line x1="95" y1="70" x2="263" y2="70" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="180" y="63" font-size="12" text-anchor="middle">read(fd, buf, 31)</text>
  <text x="273" y="100" font-size="12">FIFO 비었음 → unlock</text>
  <rect x="257" y="115" width="16" height="125" fill="#888" fill-opacity="0.3" stroke="#888"/>
  <text x="281" y="135" font-size="12">wait_event(rq)</text>
  <text x="281" y="152" font-size="12">잠듦: INTERRUPTIBLE</text>
  <line x1="600" y1="180" x2="437" y2="180" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="517" y="173" font-size="12" text-anchor="middle">write(fd, "hello", 5)</text>
  <text x="443" y="205" font-size="12">kfifo_from_user 5 B</text>
  <line x1="435" y1="232" x2="275" y2="232" stroke="#d0564a" marker-end="url(#ah1)"/>
  <text x="355" y="225" font-size="12" text-anchor="middle">wake_up_interruptible(rq)</text>
  <line x1="435" y1="255" x2="598" y2="255" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="517" y="249" font-size="12" text-anchor="middle">return 5</text>
  <text x="273" y="272" font-size="12">다시 lock → kfifo_to_user</text>
  <line x1="265" y1="300" x2="97" y2="300" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="180" y="293" font-size="12" text-anchor="middle">return 5</text>
  <text x="20" y="332" font-size="12">실측: reader는 204 ms 잠들었다가 "hello"를 받았다 (§2.6)</text>
</svg>
```

그림 1 — 빈 FIFO에서 `read()`는 락을 놓고 `rq`에서 잠든다. `write()`가 데이터를 넣고 `rq`를 깨우면, 깨어난 reader는 **락을 다시 잡고 조건을 다시 확인한 뒤** 복사한다.

### 2.5 풀이 — 커널 스타일 전체 코드

UAPI 헤더는 커널과 유저가 같이 include한다(G5 §2: 고정 폭 타입 `__u32`, 매직 문자, 방향 매크로).

```c
/* r3_chr.h — 커널과 유저가 같이 include 하는 ioctl 번호 */
#ifndef R3_CHR_H
#define R3_CHR_H
#include <linux/ioctl.h>
#include <linux/types.h>

#define R3_IOC_MAGIC     'r'
#define R3_IOC_GET_LEVEL _IOR(R3_IOC_MAGIC, 1, __u32)  /* bytes in FIFO */
#define R3_IOC_RESET     _IO(R3_IOC_MAGIC, 2)          /* drop all data */
#endif
```

```c
// r3_chr.c (1/3) — misc char device: write->FIFO, blocking read, poll, ioctl
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include "r3_chr.h"

#define R3_FIFO_SZ 64				/* must be a power of 2 */

struct r3_dev {
	struct miscdevice misc;
	struct mutex lock;			/* protects fifo; process ctx only */
	DECLARE_KFIFO(fifo, u8, R3_FIFO_SZ);
	wait_queue_head_t rq;			/* readers wait for data */
	wait_queue_head_t wq;			/* writers wait for space */
};
static struct r3_dev r3;

static inline struct r3_dev *to_r3(struct file *f)
{	/* misc_open() put &misc into private_data */
	return container_of(f->private_data, struct r3_dev, misc);
}

static ssize_t r3_read(struct file *f, char __user *ubuf, size_t n, loff_t *ppos)
{
	struct r3_dev *d = to_r3(f);
	unsigned int copied;
	int ret;

	if (mutex_lock_interruptible(&d->lock))
		return -ERESTARTSYS;
	while (kfifo_is_empty(&d->fifo)) {
		mutex_unlock(&d->lock);		/* never sleep holding the lock */
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(d->rq, !kfifo_is_empty(&d->fifo)))
			return -ERESTARTSYS;	/* signal: let VFS restart */
		if (mutex_lock_interruptible(&d->lock))
			return -ERESTARTSYS;
	}					/* re-check under lock: loop */
	ret = kfifo_to_user(&d->fifo, ubuf, n, &copied);
	mutex_unlock(&d->lock);
	if (ret)
		return ret;			/* -EFAULT */
	wake_up_interruptible(&d->wq);		/* space freed */
	return copied;
}
```

```c
// r3_chr.c (2/3) — write, poll
static ssize_t r3_write(struct file *f, const char __user *ubuf, size_t n,
			loff_t *ppos)
{
	struct r3_dev *d = to_r3(f);
	unsigned int copied;
	int ret;

	if (mutex_lock_interruptible(&d->lock))
		return -ERESTARTSYS;
	while (kfifo_is_full(&d->fifo)) {
		mutex_unlock(&d->lock);
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(d->wq, !kfifo_is_full(&d->fifo)))
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(&d->lock))
			return -ERESTARTSYS;
	}
	ret = kfifo_from_user(&d->fifo, ubuf, n, &copied);	/* short write ok */
	mutex_unlock(&d->lock);
	if (ret)
		return ret;
	wake_up_interruptible(&d->rq);
	return copied;
}

static __poll_t r3_poll(struct file *f, poll_table *pt)
{
	struct r3_dev *d = to_r3(f);
	__poll_t mask = 0;

	poll_wait(f, &d->rq, pt);		/* 1) register, never sleeps */
	poll_wait(f, &d->wq, pt);
	mutex_lock(&d->lock);			/* 2) report current state */
	if (!kfifo_is_empty(&d->fifo))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (!kfifo_is_full(&d->fifo))
		mask |= EPOLLOUT | EPOLLWRNORM;
	mutex_unlock(&d->lock);
	return mask;
}
```

```c
// r3_chr.c (3/3) — ioctl, registration
static long r3_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct r3_dev *d = to_r3(f);
	u32 level;

	switch (cmd) {
	case R3_IOC_GET_LEVEL:
		mutex_lock(&d->lock);
		level = kfifo_len(&d->fifo);
		mutex_unlock(&d->lock);
		return put_user(level, (u32 __user *)arg);
	case R3_IOC_RESET:
		mutex_lock(&d->lock);
		kfifo_reset(&d->fifo);
		mutex_unlock(&d->lock);
		wake_up_interruptible(&d->wq);
		return 0;
	}
	return -ENOTTY;				/* unknown cmd: not -EINVAL */
}
static const struct file_operations r3_fops = {
	.owner		= THIS_MODULE,
	.read		= r3_read,
	.write		= r3_write,
	.poll		= r3_poll,
	.unlocked_ioctl	= r3_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= noop_llseek,
};

static int __init r3_init(void)
{
	mutex_init(&r3.lock);
	INIT_KFIFO(r3.fifo);
	init_waitqueue_head(&r3.rq);
	init_waitqueue_head(&r3.wq);
	r3.misc.minor = MISC_DYNAMIC_MINOR;
	r3.misc.name  = "r3_chr";
	r3.misc.fops  = &r3_fops;
	r3.misc.mode  = 0666;
	return misc_register(&r3.misc);		/* publish LAST */
}

static void __exit r3_exit(void)
{
	misc_deregister(&r3.misc);
}
module_init(r3_init);
module_exit(r3_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("R3 whiteboard misc char device");
```

### 2.6 실행 — 정적 테스트 바이너리로 계약을 하나씩 확인

무엇을 확인하나: 빈 상태 non-blocking read, poll 마스크, 블로킹 read가 write에 깨어나는지, 짧은 write, 가득 찼을 때, ioctl 세 경우.

```c
// r3_chr_test.c — userspace test for /dev/r3_chr (static binary in guest)
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "r3_chr.h"

#define DEV "/dev/r3_chr"
static double now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static void *reader(void *arg)
{
	char buf[32] = {0};
	int fd = open(DEV, O_RDONLY);		/* blocking fd */
	double t0 = now_ms();
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	printf("[reader] blocked %.0f ms, read %zd bytes: \"%s\"\n",
	       now_ms() - t0, n, buf);
	close(fd);
	return arg;
}
int main(void)
{
	int fd = open(DEV, O_RDWR | O_NONBLOCK);
	struct pollfd p = { .fd = fd, .events = POLLIN | POLLOUT };
	char big[100], buf[8];
	pthread_t th;
	__u32 lvl;

	if (fd < 0) { perror("open"); exit(1); }
	ssize_t n = read(fd, buf, 8);		/* errno read AFTER the call */
	printf("1) empty read: %zd (%s)\n", n, strerror(errno));
	poll(&p, 1, 0);
	printf("2) poll empty: IN=%d OUT=%d\n", !!(p.revents & POLLIN), !!(p.revents & POLLOUT));
	pthread_create(&th, NULL, reader, NULL);
	usleep(200 * 1000);			/* let the reader sleep */
	if (write(fd, "hello", 5) != 5) perror("write");
	pthread_join(th, NULL);
	memset(big, 'x', sizeof(big));
	printf("3) write 100 -> %zd (FIFO is 64)\n", write(fd, big, sizeof(big)));
	n = write(fd, big, 1);  printf("4) write when full -> %zd (%s)\n", n, strerror(errno));
	poll(&p, 1, 0);
	printf("5) poll full: IN=%d OUT=%d\n", !!(p.revents & POLLIN), !!(p.revents & POLLOUT));
	ioctl(fd, R3_IOC_GET_LEVEL, &lvl);  printf("6) GET_LEVEL = %u\n", lvl);
	ioctl(fd, R3_IOC_RESET);  ioctl(fd, R3_IOC_GET_LEVEL, &lvl);  printf("7) after RESET = %u\n", lvl);
	int rc = ioctl(fd, _IO('r', 99));  printf("8) bad ioctl -> %d (%s)\n", rc, strerror(errno));
	close(fd);
	return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static -pthread -I. r3_chr_test.c -o /var/tmp/R3/bin/r3_chr_test
bash run.sh chr
```

```text
lab initramfs: 6.18.55-trace up 1.93s
crw-rw-rw-    1 0        0          10, 258 Oct 18 22:44 /dev/r3_chr
258 r3_chr
1) empty read: -1 (Resource temporarily unavailable)
2) poll empty: IN=0 OUT=1
[reader] blocked 204 ms, read 5 bytes: "hello"
3) write 100 -> 64 (FIFO is 64)
4) write when full -> -1 (Resource temporarily unavailable)
5) poll full: IN=1 OUT=0
6) GET_LEVEL = 64
7) after RESET = 0
8) bad ioctl -> -1 (Inappropriate ioctl for device)
rmmod ok
```

출력에서 볼 것:

- `/dev/r3_chr`가 major 10, 동적 minor 258로 생겼다(misc, G7 §1). `.mode = 0666`이 그대로 권한이 됐다.
- 1)과 4)는 `O_NONBLOCK` 경로의 `-EAGAIN`, 3)은 **짧은 write**(100을 요청했는데 64) — 유저는 반환값을 보고 나머지를 다시 써야 한다.
- reader가 200 ms `usleep` 동안 잠들어 있다가 write 직후 깨어났다(204 ms). 바쁜 대기였다면 CPU를 태웠을 것이다.
- 모르는 ioctl은 `-ENOTTY`("Inappropriate ioctl for device"). `-EINVAL`이 아니다(G5 §3.2).
- 테스트 코드 자체의 함정 하나: 처음엔 `printf("%zd %s", read(...), strerror(errno))`로 썼다. C에서 **함수 인자 평가 순서는 정해져 있지 않으므로** `errno`를 `read()`보다 먼저 읽을 수 있다. 반환값을 변수에 받은 뒤 `errno`를 읽도록 고쳤다.

### 2.7 토론 — 복잡도, 락, 컨텍스트

| 주제 | 말할 내용 |
|---|---|
| 복잡도 | read/write는 O(n) 복사, poll은 O(1). kfifo는 크기가 2의 거듭제곱이라 인덱스가 `& mask` |
| 왜 mutex인가 | `kfifo_to_user`는 `copy_to_user`를 부르고, 이것은 **page fault로 잠들 수 있다**. spinlock을 잡은 채 잠들면 안 된다(F4 §5). 생산자가 IRQ였다면 mutex를 못 쓰니 구조를 바꿔야 한다 → §4.6 |
| 락을 놓고 잔다 | `wait_event_interruptible` 전에 `mutex_unlock`. 락을 쥔 채 자면 writer가 들어올 수 없어 영원히 안 깨어난다 |
| 조건 재확인 `while` | 깨어났다고 데이터가 있다는 보장은 없다. 다른 reader가 먼저 가져갔을 수 있다(헛깨움, G6 §5). 그래서 `if`가 아니라 `while` |
| lost wakeup | `wait_event`는 "상태를 TASK_INTERRUPTIBLE로 → 조건 확인 → schedule" 순서라서, 조건 확인과 잠들기 사이에 온 `wake_up`도 놓치지 않는다(F3 §5) |
| `-ERESTARTSYS` | 시그널로 깨면 이 값을 돌려준다. 유저에게는 보이지 않고, VFS가 `SA_RESTART`면 다시 호출하거나 `-EINTR`로 바꾼다(F3 §4) |
| poll 두 단계 | `poll_wait`는 **등록만** 하고 절대 잠들지 않는다. 그 다음 현재 상태를 마스크로 보고한다. 잠드는 것은 VFS(`do_poll`)다(G6 §1.3) |
| 왜 misc인가 | major/minor 할당, cdev, class, device_create를 `misc_register` 한 번이 해 준다. 장치 하나짜리 드라이버의 표준 선택(G7) |

### 2.8 면접관이 기다리는 실수

- `copy_to_user` 반환값을 무시하거나 그대로 반환한다. 이 함수는 **복사 못 한 바이트 수**(양수)를 돌려준다. 실패면 `-EFAULT`로 바꿔야 한다. (`kfifo_to_user`는 이미 `-EFAULT`로 바꿔서 준다.)
- `__user` 포인터를 커널에서 직접 역참조(`*ubuf = x`). ARMv8.1+ 코어에서 PAN이 켜져 있으면 바로 oops다(E6). 랩의 cortex-a57(v8.0)처럼 PAN이 없으면 조용히 "동작"해서 더 위험하다 — 유저가 커널 주소를 넘기면 그대로 보안 구멍이다.
- spinlock을 잡고 `copy_to_user` 또는 `wait_event`.
- `wake_up`을 빠뜨려 poll/epoll 사용자가 영원히 안 깨어남(G6 §4의 ET 멈춤 버그와 같은 뿌리).
- `misc_register`를 초기화(`mutex_init`, `INIT_KFIFO`)보다 먼저 호출. 등록하는 순간 유저가 open할 수 있다 — **"다 준비하고 마지막에 공개"**(G4 §2.3).
- 모르는 ioctl에 `-EINVAL`. 정답은 `-ENOTTY`.

### 2.9 꼬리 문제 (follow-up)

| 꼬리 질문 | 답의 방향 |
|---|---|
| "인스턴스가 3개면?" | 정적 구조체를 버리고 probe에서 `devm_kzalloc` + `miscdevice`를 장치마다 하나. `container_of`는 그대로 동작(G7 §4) |
| "생산자가 IRQ면?" | mutex 불가. `spin_lock_irqsave` + kfifo 또는 lock-free SPSC(§4), read 쪽은 락 밖에서 `copy_to_user` |
| "메시지 경계를 지키려면?" | 레코드 단위 kfifo(`DECLARE_KFIFO(f, u8, N)` 대신 `struct kfifo_rec_ptr_1`) 또는 고정 크기 레코드 |
| "epoll edge-triggered 사용자가 멈춘다" | 상태가 바뀔 때마다 깨우고 있는지, 비어 있을 때만 깨우는 최적화를 했는지 확인(G6 §4.2) |
| "fasync/SIGIO도?" | `.fasync` + `fasync_helper` + `kill_fasync(&q, SIGIO, POLL_IN)`(G6 §7) |
| "mmap으로 복사를 없애라" | `.mmap` + `remap_pfn_range` 또는 `vm_insert_page`, 공유 링의 head/tail 동기화가 새 문제(G5 §4) |

---

## 3. 문제 2 — platform driver + DT binding + IRQ + regmap

### 3.1 면접관은 이렇게 말한다

> "There's a memory-mapped sensor block at 0x0a003e00 with an interrupt line. Write the device tree node and a platform driver that maps the registers, checks the chip ID, and handles the interrupt. The line bounces, so report an event only after it has been quiet for 20 ms. Use managed resources."

### 3.2 먼저 물어볼 것

| 질문 | 왜 |
|---|---|
| 레지스터 폭과 정렬은? 32비트 접근만 허용하나? | `regmap_config.val_bits`, `reg_stride`, `readl` vs `readw` (H1 §3.5) |
| 인터럽트는 엣지인가 레벨인가? 누가 지우나? | 레벨이면 ISR이 장치 쪽 원인을 지워야 한다. 안 지우면 IRQ 폭주 |
| ISR에서 장치를 읽어야 하나? 버스가 I2C면? | MMIO는 hardirq에서 OK, I2C는 잠드므로 threaded IRQ(F7 §8) |
| 클럭·리셋·전원이 필요한가? | `devm_clk_get_enabled`, `devm_reset_control_get`, regulator(H3) |
| 이벤트를 유저에게 어떻게 알리나? | sysfs, misc+poll, input, IIO 중 선택(I1) |

### 3.3 처음 5분 뼈대

```c
struct r3s { struct device *dev; void __iomem *base; struct regmap *map;
	     int irq; struct delayed_work deb_work; /* ... */ };
static irqreturn_t r3s_isr(int irq, void *data);	/* hardirq: 짧게 */
static void r3s_deb_work(struct work_struct *w);	/* 잠들어도 됨 */
static int r3s_probe(struct platform_device *pdev)
{
	/* 1 kzalloc 2 ioremap 3 regmap 4 DT 속성 5 ID 확인
	 * 6 work 준비 7 drvdata 8 IRQ 요청 (마지막: 이 순간부터 ISR이 돈다) */
}
static const struct of_device_id r3s_of_match[] = { { .compatible = "don,r3-sensor" }, { } };
MODULE_DEVICE_TABLE(of, r3s_of_match);
static struct platform_driver r3s_driver = { .probe = r3s_probe, .driver = { /* name, of_match_table */ } };
module_platform_driver(r3s_driver);
```

### 3.4 그림 — probe 순서, devres 역순 해제, 런타임 경로

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="235" height="110" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="22" y="42" font-size="12">r3-sensor@a003e00 {</text>
  <text x="22" y="60" font-size="12">compatible = "don,r3-sensor"</text>
  <text x="22" y="78" font-size="12">reg = &lt;0xa003e00 0x200&gt;</text>
  <text x="22" y="96" font-size="12">interrupts = &lt;SPI 47 EDGE&gt;</text>
  <text x="22" y="114" font-size="12">don,debounce-ms = &lt;20&gt;</text>
  <line x1="127" y1="130" x2="127" y2="163" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="135" y="152" font-size="12">of_match_table 일치</text>
  <rect x="10" y="165" width="235" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="127" y="185" font-size="13" text-anchor="middle">r3s_probe(pdev)</text>
  <line x1="245" y1="180" x2="268" y2="180" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="270" y="20" width="255" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="280" y="39" font-size="12">1 devm_kzalloc</text>
  <rect x="270" y="58" width="255" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="280" y="77" font-size="12">2 devm_platform_ioremap_resource</text>
  <rect x="270" y="96" width="255" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="280" y="115" font-size="12">3 devm_regmap_init_mmio</text>
  <rect x="270" y="134" width="255" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="280" y="153" font-size="12">4 device_property_read_u32</text>
  <rect x="270" y="172" width="255" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="280" y="191" font-size="12">5 regmap_read → magic 확인</text>
  <rect x="270" y="210" width="255" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="280" y="229" font-size="12">6 devm_delayed_work_autocancel</text>
  <rect x="270" y="248" width="255" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="280" y="267" font-size="12">7 platform_set_drvdata</text>
  <rect x="270" y="286" width="255" height="28" rx="3" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="280" y="305" font-size="12">8 devm_request_irq (마지막)</text>
  <line x1="545" y1="314" x2="545" y2="22" stroke="#d0564a" marker-end="url(#ah2)"/>
  <text x="555" y="50" font-size="12">devres 해제</text>
  <text x="555" y="68" font-size="12">(쌓은 역순)</text>
  <text x="555" y="105" font-size="12">8 IRQ 해제</text>
  <text x="555" y="125" font-size="12">6 work 취소</text>
  <text x="555" y="145" font-size="12">3 regmap 해제</text>
  <text x="555" y="165" font-size="12">2 iounmap</text>
  <text x="555" y="185" font-size="12">1 kfree</text>
  <text x="10" y="345" font-size="12">런타임: 엣지 5번 → 이벤트 1번 (실측 raw_irqs=5 events=1)</text>
  <rect x="10" y="355" width="150" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="85" y="376" font-size="12" text-anchor="middle">GIC SPI 47</text>
  <text x="85" y="394" font-size="12" text-anchor="middle">엣지 (fire)</text>
  <rect x="180" y="355" width="150" height="50" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="255" y="376" font-size="12" text-anchor="middle">r3s_isr</text>
  <text x="255" y="394" font-size="12" text-anchor="middle">hardirq, 짧게</text>
  <rect x="350" y="355" width="150" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="425" y="376" font-size="12" text-anchor="middle">mod_delayed_work</text>
  <text x="425" y="394" font-size="12" text-anchor="middle">마감 +20 ms</text>
  <rect x="520" y="355" width="150" height="50" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595" y="376" font-size="12" text-anchor="middle">r3s_deb_work</text>
  <text x="595" y="394" font-size="12" text-anchor="middle">프로세스 ctx</text>
  <line x1="160" y1="380" x2="178" y2="380" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="330" y1="380" x2="348" y2="380" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="500" y1="380" x2="518" y2="380" stroke="currentColor" marker-end="url(#ah2)"/>
</svg>
```

그림 2 — probe는 bring-up 순서 그대로 자원을 쌓고, IRQ를 **마지막에** 켠다. devres는 unbind나 probe 실패 때 쌓은 역순으로 푼다: IRQ가 먼저 사라진 뒤 work가 취소되므로 "ISR이 이미 취소된 work를 다시 거는" 틈이 없다.

### 3.5 풀이

regmap을 쓰는 이유: MMIO와 I2C/SPI 드라이버가 같은 API(`regmap_read`/`regmap_update_bits`)를 쓰고, `update_bits`가 내부 락으로 **RMW를 원자적으로** 해 준다(H2 §1, §4). MMIO regmap은 `fast_io`(spinlock)라서 ISR에서도 부를 수 있다(`regmap-mmio.c`에 `.fast_io = true`).

```c
// r3_plat.c (1/4) — platform driver: DT match, regmap-MMIO, IRQ, debounce
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/regmap.h>
#include <linux/io.h>
#include <linux/bitfield.h>
#include <linux/workqueue.h>
#include <linux/devm-helpers.h>
#include <linux/delay.h>

#define R3_MAGIC	0x000	/* "virt" = 0x74726976 */
#define R3_VERSION	0x004
#define R3_DEVID	0x008
#define R3_VENDOR	0x00c	/* "QEMU" = 0x554d4551 */
#define R3_VENDOR_B0	GENMASK(7, 0)
#define R3_VENDOR_B3	GENMASK(31, 24)

struct r3_regs {		/* struct overlay of the same window */
	__le32 magic;		/* 0x00 */
	__le32 version;		/* 0x04 */
	__le32 devid;		/* 0x08 */
	__le32 vendor;		/* 0x0c */
};
static_assert(offsetof(struct r3_regs, vendor) == R3_VENDOR);

struct r3s {
	struct device *dev;
	void __iomem *base;
	struct regmap *map;
	int irq;
	u32 debounce_ms;
	atomic_t raw_irqs;		/* touched in hardirq */
	unsigned int events;		/* touched only in the work */
	struct delayed_work deb_work;
};

static const struct regmap_config r3s_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x1fc,
};

static irqreturn_t r3s_isr(int irq, void *data)	/* hardirq: no sleep */
{
	struct r3s *s = data;

	atomic_inc(&s->raw_irqs);
	/* every edge pushes the deadline out: fires once after quiet time */
	mod_delayed_work(system_wq, &s->deb_work,
			 msecs_to_jiffies(s->debounce_ms));
	return IRQ_HANDLED;
}
```

```c
// r3_plat.c (2/4) — debounce work + sysfs test knobs
static void r3s_deb_work(struct work_struct *w)	/* process ctx: may sleep */
{
	struct r3s *s = container_of(to_delayed_work(w), struct r3s, deb_work);
	unsigned int magic;

	regmap_read(s->map, R3_MAGIC, &magic);	/* "read status" after settle */
	s->events++;
	dev_info(s->dev, "debounced event #%u (raw irqs so far %d) magic=%#x\n",
		 s->events, atomic_read(&s->raw_irqs), magic);
}

/* test only: make the GIC see an edge, as if the sensor raised its line */
static ssize_t fire_store(struct device *dev, struct device_attribute *a,
			  const char *buf, size_t len)
{
	struct r3s *s = dev_get_drvdata(dev);
	unsigned int i, n;
	int ret;

	ret = kstrtouint(buf, 0, &n);
	if (ret)
		return ret;
	for (i = 0; i < n; i++) {
		ret = irq_set_irqchip_state(s->irq, IRQCHIP_STATE_PENDING, true);
		if (ret)
			return ret;
		usleep_range(2000, 3000);	/* 2-3 ms bounce spacing */
	}
	return len;
}
static DEVICE_ATTR_WO(fire);

static ssize_t stats_show(struct device *dev, struct device_attribute *a,
			  char *buf)
{
	struct r3s *s = dev_get_drvdata(dev);

	return sysfs_emit(buf, "raw_irqs=%d events=%u\n",
			  atomic_read(&s->raw_irqs), s->events);
}
static DEVICE_ATTR_RO(stats);

static struct attribute *r3s_attrs[] = {
	&dev_attr_fire.attr, &dev_attr_stats.attr, NULL
};
ATTRIBUTE_GROUPS(r3s);
```

```c
// r3_plat.c (3/4) — probe: every resource is devm_*, so no remove()
static int r3s_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct r3_regs __iomem *regs;
	unsigned int magic, ver, vendor;
	struct r3s *s;
	int ret;

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->dev = dev;
	s->base = devm_platform_ioremap_resource(pdev, 0);	/* request+map */
	if (IS_ERR(s->base))
		return PTR_ERR(s->base);
	s->map = devm_regmap_init_mmio(dev, s->base, &r3s_regmap_cfg);
	if (IS_ERR(s->map))
		return dev_err_probe(dev, PTR_ERR(s->map), "regmap\n");
	if (device_property_read_u32(dev, "don,debounce-ms", &s->debounce_ms))
		s->debounce_ms = 10;			/* optional property */

	ret = regmap_read(s->map, R3_MAGIC, &magic) ?:
	      regmap_read(s->map, R3_VERSION, &ver) ?:
	      regmap_read(s->map, R3_VENDOR, &vendor);
	if (ret)
		return ret;
	if (magic != 0x74726976)
		return dev_err_probe(dev, -ENODEV, "bad magic %#x\n", magic);
	regs = s->base;					/* overlay view */
	dev_info(dev, "offset vendor=%#x overlay vendor=%#x bytes '%c'..'%c'\n",
		 vendor, readl(&regs->vendor),
		 (char)FIELD_GET(R3_VENDOR_B0, vendor),
		 (char)FIELD_GET(R3_VENDOR_B3, vendor));

	ret = devm_delayed_work_autocancel(dev, &s->deb_work, r3s_deb_work);
	if (ret)
		return ret;
	atomic_set(&s->raw_irqs, 0);
	platform_set_drvdata(pdev, s);			/* before IRQ can fire */
	s->irq = platform_get_irq(pdev, 0);
	if (s->irq < 0)
		return s->irq;
	ret = devm_request_irq(dev, s->irq, r3s_isr, 0, dev_name(dev), s);
	if (ret)
		return dev_err_probe(dev, ret, "irq %d\n", s->irq);
	dev_info(dev, "probed: version=%u irq=%d debounce=%ums\n",
		 ver, s->irq, s->debounce_ms);
	return 0;
}
```

```c
// r3_plat.c (4/4) — DT match table and driver registration
static const struct of_device_id r3s_of_match[] = {
	{ .compatible = "don,r3-sensor" },
	{ }
};
MODULE_DEVICE_TABLE(of, r3s_of_match);

static struct platform_driver r3s_driver = {
	.probe = r3s_probe,
	.driver = {
		.name		= "r3_sensor",
		.of_match_table	= r3s_of_match,
		.dev_groups	= r3s_groups,
	},
};
module_platform_driver(r3s_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("R3 whiteboard: r3_plat");
```

### 3.6 실행 — 바인딩, 자원, 디바운스, unbind

무엇을 확인하나: DT 매칭으로 probe가 됐는지, `/proc/iomem`과 `/proc/interrupts`에 자원이 보이는지, 튀는 엣지 5번이 이벤트 1번이 되는지, unbind 때 devres가 다 푸는지. 엣지는 `irq_set_irqchip_state(irq, IRQCHIP_STATE_PENDING, true)`로 GIC에 pending을 세워 만든다(G3 §8은 GICD 레지스터에 직접 썼다. 이 API가 더 깔끔하다 — `kernel/irq/manage.c`에서 `EXPORT_SYMBOL_GPL` 확인).

```sh
bash run.sh plat
```

```text
lab initramfs: 6.18.55-trace up 2.14s
lrwxrwxrwx    1 0        0                0 Oct 18 22:44 /sys/bus/platform/devices/a003e00.r3-sensor/driver -> ../../../bus/platform/drivers/r3_sensor
0a003e00-0a003fff : a003e00.r3-sensor r3-sensor@a003e00
 22:          0          0 GIC-0  79 Edge      a003e00.r3-sensor
raw_irqs=5 events=1
raw_irqs=6 events=2
 22:          6          0 GIC-0  79 Edge      a003e00.r3-sensor
unbind: iomem+irq released
[    2.294398][   T76] r3_sensor a003e00.r3-sensor: offset vendor=0x554d4551 overlay vendor=0x554d4551 bytes 'Q'..'U'
[    2.295172][   T76] r3_sensor a003e00.r3-sensor: probed: version=1 irq=22 debounce=20ms
[    2.380002][   T11] r3_sensor a003e00.r3-sensor: debounced event #1 (raw irqs so far 5) magic=0x74726976
[    3.407631][   T11] r3_sensor a003e00.r3-sensor: debounced event #2 (raw irqs so far 6) magic=0x74726976
```

출력에서 볼 것:

- 장치 이름 `a003e00.r3-sensor`는 DT 노드 이름에서 왔고(D3), `driver` 링크가 `r3_sensor`를 가리킨다.
- `/proc/iomem`에 0x200 창이 우리 이름으로 잡혔다 — `devm_platform_ioremap_resource`는 `request_mem_region` + `ioremap`이다(H1 §1.4). 다른 드라이버가 같은 창을 잡으면 `-EBUSY`.
- **Linux IRQ 번호 22 ≠ 하드웨어 번호**. GIC hwirq는 79(= SPI 47 + 32). 드라이버는 `platform_get_irq`가 준 virq만 쓴다(H6).
- 2–3 ms 간격 엣지 5번 → 마지막 엣지 후 20 ms 조용해진 다음 이벤트 1번. 다음 단발 엣지 하나 → 이벤트 2번. 디바운스가 동작한다.
- work는 `T11`(kworker)에서, probe는 `T76`(insmod)에서 돌았다 — 컨텍스트가 다르다.
- unbind 후 `/proc/iomem`과 `/proc/interrupts`에서 사라졌다. `.remove` 함수가 없는데도 전부 풀렸다.
- 오프셋 방식(`regmap_read(R3_VENDOR)`)과 구조체 오버레이(`readl(&regs->vendor)`)가 같은 값을 읽었다. `FIELD_GET`으로 뽑은 바이트 0은 `'Q'`(0x51), 바이트 3은 `'U'`(0x55) — 리틀 엔디안으로 "QEMU"다(§6, §7).

### 3.7 토론

| 주제 | 말할 내용 |
|---|---|
| 왜 drvdata를 IRQ 전에 | `devm_request_irq`가 돌아오기 전에도 ISR이 돌 수 있다(이미 pending이면 즉시). ISR이나 sysfs가 쓰는 모든 것은 그 전에 준비(G3 §3.3) |
| 왜 `.remove`가 없나 | 모든 자원이 devm. 해제 순서는 획득 역순이 자동 보장. 단, devm이 아닌 자원(예: `request_irq` + `kfree` 섞기)이 있으면 순서 버그(G3 §8) |
| ISR에서 한 일 | 카운터 `atomic_inc` + `mod_delayed_work`. `mod_delayed_work`는 "IRQ 핸들러 포함 어느 컨텍스트에서도 안전"(`kernel/workqueue.c` 주석) |
| `events`는 왜 atomic이 아닌가 | work 하나에서만 쓴다. 같은 work는 동시에 두 번 돌지 않는다(같은 워크큐 non-reentrant). sysfs 읽기는 경쟁이 있지만 통계용이라 허용 — 이렇게 **"왜 괜찮은지"를 말하는 것**이 점수다 |
| 엣지 vs 레벨 | 레벨 IRQ였다면 ISR이 장치의 status를 읽고 지워야 한다. 지우는 동작이 I2C면 `devm_request_threaded_irq(..., NULL, thread_fn, IRQF_ONESHOT, ...)` |
| 에러 출력 | `dev_err_probe`: `-EPROBE_DEFER`면 조용히 기록만, 아니면 에러 출력(G3 §9) |

### 3.8 면접관이 기다리는 실수

- `devm_request_irq`를 probe 맨 앞에 둔다 → ISR이 아직 NULL인 drvdata를 만진다.
- `devm_request_irq` + 수동 `kfree(s)`를 remove에서 → remove가 끝나고 devres가 IRQ를 풀기 전 틈에 ISR이 해제된 메모리를 만진다(G3 §8.2에서 실제로 재현).
- `ioremap` 결과를 `NULL`로 검사한다. `devm_platform_ioremap_resource`는 **`ERR_PTR`**을 돌려준다 → `IS_ERR`(E5 §3).
- `__iomem` 포인터를 `*(u32 *)p`로 직접 역참조(H1 §8 — arm64에서는 우연히 돌아가서 더 위험).
- `MODULE_DEVICE_TABLE`을 빠뜨린다 → 빌트인/수동 insmod는 되지만 udev 자동 로딩(modalias)이 안 된다(G9).
- `of_match_table` 마지막에 빈 항목 `{ }`이 없다.
- ISR에서 `msleep`, `mutex_lock`, I2C 읽기.

### 3.9 꼬리 문제

| 꼬리 질문 | 답의 방향 |
|---|---|
| "클럭과 리셋이 있다면?" | `devm_clk_get_enabled(dev, NULL)`, `devm_reset_control_get_exclusive` + deassert, 순서는 전원 → 클럭 → 리셋 해제 → ID 읽기(G3 §3, H3) |
| "공급자가 아직 없으면?" | `-EPROBE_DEFER`를 그대로 반환, 드라이버 코어가 나중에 재시도(G3 §9) |
| "suspend/resume?" | `DEFINE_SIMPLE_DEV_PM_OPS` + `pm_sleep_ptr`, resume에서 regmap cache sync(N6, H2 §5) |
| "디바운스를 hrtimer로?" | `hrtimer_start(&t, ms_to_ktime(20), HRTIMER_MODE_REL)`을 ISR마다 재시작. 콜백은 hardirq에서 돌므로 잠드는 일은 다시 work로(§8.1) |
| "DT 바인딩 문서는?" | `Documentation/devicetree/bindings/.../don,r3-sensor.yaml`, `make dt_binding_check`(D7) |
| "ACPI에서도 돌게?" | `of_property_read_u32` 대신 이미 쓴 `device_property_read_u32`가 정답 — 펌웨어 종류와 무관(D8) |

---

## 4. 문제 3 — lock-free SPSC 링 버퍼 (그리고 IRQ-safe 변형, kfifo 비교)

### 4.1 면접관은 이렇게 말한다

> "Implement a ring buffer between one producer and one consumer running on different CPUs, without locks. Explain every memory barrier. Then: what changes if the producer is an interrupt handler on the same CPU as the consumer?"

### 4.2 먼저 물어볼 것

| 질문 | 왜 |
|---|---|
| 생산자·소비자가 정확히 하나씩인가? | 하나씩이면 lock-free 가능. 여럿이면 CAS 또는 락 |
| 가득 차면 덮어쓰나, 버리나, 기다리나? | 센서는 보통 "버리고 카운트" (drop counter) |
| 크기를 2의 거듭제곱으로 정해도 되나? | `% size` 대신 `& mask`, 인덱스 wrap 처리가 단순 |
| 같은 CPU에서 IRQ와 스레드인가, 다른 CPU인가? | 같은 CPU면 컴파일러 배리어만으로도 되는 경우가 있지만, 일반 코드는 SMP 배리어로 쓴다 |

### 4.3 그림 — free-running 인덱스와 acquire/release 짝

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="230" y="40" font-size="13" text-anchor="middle">head = 0x00000002</text>
  <text x="230" y="58" font-size="12" text-anchor="middle">head &amp; 7 = 2 (다음 쓰기)</text>
  <line x1="230" y1="66" x2="230" y2="106" stroke="#e08a3c" marker-end="url(#ah3)"/>
  <text x="510" y="40" font-size="13" text-anchor="middle">tail = 0xFFFFFFFE</text>
  <text x="510" y="58" font-size="12" text-anchor="middle">tail &amp; 7 = 6 (다음 읽기)</text>
  <line x1="510" y1="66" x2="510" y2="106" stroke="#4a7bd0" marker-end="url(#ah3)"/>
  <rect x="60" y="110" width="60" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="90" y="137" font-size="13" text-anchor="middle">102</text>
  <rect x="130" y="110" width="60" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="137" font-size="13" text-anchor="middle">103</text>
  <rect x="200" y="110" width="60" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="270" y="110" width="60" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="340" y="110" width="60" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="410" y="110" width="60" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="480" y="110" width="60" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="137" font-size="13" text-anchor="middle">100</text>
  <rect x="550" y="110" width="60" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="580" y="137" font-size="13" text-anchor="middle">101</text>
  <text x="90" y="172" font-size="12" text-anchor="middle">[0]</text>
  <text x="160" y="172" font-size="12" text-anchor="middle">[1]</text>
  <text x="230" y="172" font-size="12" text-anchor="middle">[2]</text>
  <text x="300" y="172" font-size="12" text-anchor="middle">[3]</text>
  <text x="370" y="172" font-size="12" text-anchor="middle">[4]</text>
  <text x="440" y="172" font-size="12" text-anchor="middle">[5]</text>
  <text x="510" y="172" font-size="12" text-anchor="middle">[6]</text>
  <text x="580" y="172" font-size="12" text-anchor="middle">[7]</text>
  <text x="60" y="200" font-size="13">used = head - tail = 0x2 - 0xFFFFFFFE = 4 (u32 뺄셈은 mod 2^32)</text>
  <text x="60" y="220" font-size="13">empty: head == tail · full: head - tail == 8 · 슬롯 = index &amp; 7</text>
  <rect x="40" y="240" width="270" height="76" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="52" y="262" font-size="13">생산자 (head만 쓴다)</text>
  <text x="52" y="282" font-size="13">① buf[head &amp; mask] = v</text>
  <text x="52" y="302" font-size="13">② smp_store_release(&amp;head)</text>
  <rect x="370" y="240" width="270" height="76" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="382" y="262" font-size="13">소비자 (tail만 쓴다)</text>
  <text x="382" y="282" font-size="13">③ smp_load_acquire(&amp;head)</text>
  <text x="382" y="302" font-size="13">④ v = buf[tail &amp; mask]</text>
  <line x1="310" y1="297" x2="368" y2="279" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="339" y="270" font-size="12" text-anchor="middle">짝</text>
</svg>
```

그림 3 — 인덱스는 슬롯 번호가 아니라 **계속 증가하는 u32 카운터**다. 슬롯은 `& mask`로 구한다. `head - tail`은 wrap을 넘어가도 정확한 개수를 준다(그림은 tail이 막 wrap 직전, head는 wrap 직후). 생산자의 release(②)를 소비자의 acquire(③)가 보면, ① 데이터도 반드시 보인다.

### 4.4 풀이 — 커널 버전

```c
/* r3_spsc.h — lock-free SPSC ring: one producer CPU, one consumer CPU */
#include <linux/types.h>
#include <asm/barrier.h>

struct spsc {
	u32 head;		/* next write slot; written ONLY by producer */
	u32 tail;		/* next read slot;  written ONLY by consumer */
	u32 mask;		/* size - 1, size is a power of 2 */
	u32 *buf;
};

/* free-running u32 indices: used = head - tail, correct across wrap */
static inline bool spsc_push(struct spsc *r, u32 v)
{
	u32 head = r->head;				/* own index */
	u32 tail = smp_load_acquire(&r->tail);		/* (A) pairs with (D) */

	if (head - tail > r->mask)			/* full: size used */
		return false;
	r->buf[head & r->mask] = v;			/* write data ... */
	smp_store_release(&r->head, head + 1);		/* (B) ... then publish */
	return true;
}

static inline bool spsc_pop(struct spsc *r, u32 *v)
{
	u32 tail = r->tail;				/* own index */
	u32 head = smp_load_acquire(&r->head);		/* (C) pairs with (B) */

	if (head == tail)				/* empty */
		return false;
	*v = r->buf[tail & r->mask];			/* read data ... */
	smp_store_release(&r->tail, tail + 1);		/* (D) ... then free slot */
	return true;
}
```

왜 이 네 개인가 (그대로 말하면 된다):

- **(B) release on head**: "데이터를 다 쓴 다음에 인덱스를 공개한다." 이게 없으면 CPU가 인덱스 store를 데이터 store보다 먼저 보이게 할 수 있다.
- **(C) acquire on head**: "인덱스를 본 다음에 데이터를 읽는다." 이게 없으면 데이터 load가 인덱스 load보다 먼저 실행돼 **옛 값**을 읽을 수 있다. (B)와 (C)가 F5 §3.2의 MP(message passing) 패턴이다.
- **(D) release on tail / (A) acquire on tail**: 반대 방향. "슬롯을 다 읽은 다음에 비었다고 알린다." 이게 없으면 생산자가 아직 읽지 않은 슬롯을 덮어쓸 수 있다.
- 자기 인덱스는 자기만 쓰므로 평범한 load로 충분하다.

커널 테스트: 두 kthread를 CPU0·CPU1에 묶고 0..N-1을 흘려 순서를 검사한다. `mode=1`은 같은 테스트를 kfifo로 한다. kfifo 헤더 주석(6.18 `include/linux/kfifo.h` 30행): "reader 하나, writer 하나이고 `kfifo_reset()`을 부르지 않으면 락이 필요 없다."

```c
// r3_spsc.c (1/2) — 2 kthreads on 2 CPUs stream N values; mode 0=spsc 1=kfifo
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/kfifo.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include "r3_spsc.h"

static unsigned int nitems = 1000000, mode;
module_param(nitems, uint, 0444);
module_param(mode, uint, 0444);

#define RSZ 1024
static struct spsc ring;
static DEFINE_KFIFO(kf, u32, RSZ);	/* kfifo is lockless for 1 reader + 1 writer */
static DECLARE_COMPLETION(done_p);
static DECLARE_COMPLETION(done_c);
static unsigned long errors, full_spins;

static int prod_fn(void *arg)
{
	u32 i;

	for (i = 0; i < nitems; i++) {
		while (!(mode ? kfifo_put(&kf, i) : spsc_push(&ring, i))) {
			full_spins++;
			cond_resched();		/* full: let the consumer run */
		}
	}
	complete(&done_p);
	return 0;
}

static int cons_fn(void *arg)
{
	u32 v, expect = 0;

	while (expect < nitems) {
		if (!(mode ? kfifo_get(&kf, &v) : spsc_pop(&ring, &v))) {
			cond_resched();
			continue;
		}
		if (v != expect)
			errors++;		/* lost / reordered / stale */
		expect = v + 1;
	}
	complete(&done_c);
	return 0;
}
```

```c
// r3_spsc.c (2/2) — start both threads, wait, report
static int __init r3_spsc_init(void)
{
	struct task_struct *p, *c;
	ktime_t t0;

	ring.mask = RSZ - 1;
	ring.buf = kcalloc(RSZ, sizeof(u32), GFP_KERNEL);
	if (!ring.buf)
		return -ENOMEM;
	p = kthread_create(prod_fn, NULL, "r3_prod");
	c = kthread_create(cons_fn, NULL, "r3_cons");
	if (IS_ERR(p) || IS_ERR(c)) {
		if (!IS_ERR(p))
			kthread_stop(p);	/* never woken: fn not run */
		if (!IS_ERR(c))
			kthread_stop(c);
		kfree(ring.buf);
		return -ENOMEM;
	}
	kthread_bind(p, 0);
	kthread_bind(c, 1);
	t0 = ktime_get();
	wake_up_process(c);
	wake_up_process(p);
	wait_for_completion(&done_p);
	wait_for_completion(&done_c);
	pr_info("r3_spsc: mode=%s n=%u errors=%lu full_spins=%lu time=%lld ms (TCG)\n",
		mode ? "kfifo" : "spsc", nitems, errors, full_spins,
		ktime_ms_delta(ktime_get(), t0));
	kfree(ring.buf);
	return 0;
}
static void __exit r3_spsc_exit(void) { }
module_init(r3_spsc_init);
module_exit(r3_spsc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("R3 whiteboard: r3_spsc");
```

```sh
bash run.sh spsc      # -smp 2
```

```text
lab initramfs: 6.18.55-trace up 2.10s
[    2.302706][   T76] r3_spsc: mode=spsc n=1000000 errors=0 full_spins=39484 time=61 ms (TCG)
[    2.412880][   T80] r3_spsc: mode=kfifo n=1000000 errors=0 full_spins=60069 time=52 ms (TCG)
```

출력에서 볼 것: 두 구현 모두 100만 개 중 순서 오류 0. 시간은 TCG라서 실제 하드웨어 비교값이 아니다(F5 §4.2: "QEMU는 메모리 순서 검증 도구가 아니다" — TCG가 게스트 배리어를 호스트에서 어떻게 번역하는지에 달렸다). 그래서 **메모리 순서는 아래 유저 공간 실험으로 진짜 M2 코어에서 검증**한다.

### 4.5 유저 공간 버전 — 단위 테스트 + 2스레드 스트레스, 그리고 틀린 버전이 실제로 깨진다

C11 `memory_order_acquire`/`release`는 arm64에서 커널 `smp_load_acquire`/`smp_store_release`와 같은 `ldar`/`stlr` 명령이 된다(아래 objdump로 확인). `-DBROKEN`이면 둘 다 `relaxed`로 바꾼다.

```c
// ring_user.c (1/3) — userspace SPSC ring (C11 atomics = smp_load_acquire/store_release)
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef BROKEN			/* wrong: relaxed = no ordering between data and index */
#define ACQ memory_order_relaxed
#define REL memory_order_relaxed
#else
#define ACQ memory_order_acquire
#define REL memory_order_release
#endif

struct spsc {
	_Alignas(64) _Atomic uint32_t head;	/* own cache line: no false sharing */
	_Alignas(64) _Atomic uint32_t tail;
	_Alignas(64) uint32_t mask;
	uint64_t *buf;
};

static int push(struct spsc *r, uint64_t v)
{
	uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
	uint32_t t = atomic_load_explicit(&r->tail, ACQ);

	if (h - t > r->mask)
		return 0;				/* full */
	r->buf[h & r->mask] = v;
	atomic_store_explicit(&r->head, h + 1, REL);
	return 1;
}

static int pop(struct spsc *r, uint64_t *v)
{
	uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
	uint32_t h = atomic_load_explicit(&r->head, ACQ);

	if (h == t)
		return 0;				/* empty */
	*v = r->buf[t & r->mask];
	atomic_store_explicit(&r->tail, t + 1, REL);
	return 1;
}
```

```c
// ring_user.c (2/3) — unit tests: empty, full, slot reuse, u32 wrap
static void unit_tests(void)
{
	uint64_t b[4], v;
	struct spsc r = { .mask = 3, .buf = b };

	assert(!pop(&r, &v));				/* empty */
	for (int i = 0; i < 4; i++)
		assert(push(&r, i));
	assert(!push(&r, 9));				/* full at exactly 4 */
	assert(pop(&r, &v) && v == 0 && push(&r, 4));	/* slot reuse */
	atomic_store(&r.head, 0xfffffffeu);		/* indices about to wrap */
	atomic_store(&r.tail, 0xfffffffeu);
	for (int i = 0; i < 4; i++)
		assert(push(&r, 100 + i));		/* head wraps to 2 */
	assert(atomic_load(&r.head) == 2 && !push(&r, 0));
	for (int i = 0; i < 4; i++)
		assert(pop(&r, &v) && v == 100u + i);
	puts("unit tests: empty/full/reuse/u32-wrap OK");
}
```

```c
// ring_user.c (3/3) — 2-thread stress: producer writes seq, consumer checks
static struct spsc R;
static uint64_t N;

static void *producer(void *a)
{
	for (uint64_t i = 1; i <= N; i++)
		while (!push(&R, i))
			;				/* spin when full */
	return a;
}

static void *consumer(void *a)
{
	uint64_t v, expect = 1, bad = 0;

	while (expect <= N) {
		if (!pop(&R, &v))
			continue;
		if (v != expect)
			bad++;				/* stale/reordered slot */
		expect++;
	}
	*(uint64_t *)a = bad;
	return a;
}

int main(int argc, char **argv)
{
	unsigned size = argc > 2 ? strtoul(argv[2], NULL, 0) : 1024;
	uint64_t bad = 0;
	struct timespec t0, t1;
	pthread_t p, c;

	N = argc > 1 ? strtoull(argv[1], NULL, 0) : 10000000;
	unit_tests();
	assert(size && !(size & (size - 1)));		/* power of 2 */
	R.mask = size - 1;
	R.buf = calloc(size, sizeof(uint64_t));
	clock_gettime(CLOCK_MONOTONIC, &t0);
	pthread_create(&c, NULL, consumer, &bad);
	pthread_create(&p, NULL, producer, NULL);
	pthread_join(p, NULL);
	pthread_join(c, NULL);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double s = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
	printf("%s size=%u N=%llu bad=%llu  %.1f M items/s\n",
#ifdef BROKEN
	       "BROKEN(relaxed)",
#else
	       "acq/rel",
#endif
	       size, (unsigned long long)N, (unsigned long long)bad, N / s / 1e6);
	free(R.buf);
	return bad != 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -pthread ring_user.c -o ring
gcc -std=gnu11 -Wall -Wextra -O2 -pthread -DBROKEN ring_user.c -o ring_broken
./ring 10000000 1024; ./ring 10000000 16
for i in 1 2 3; do ./ring_broken 10000000 16; echo "exit=$?"; done
```

```text
unit tests: empty/full/reuse/u32-wrap OK
acq/rel size=1024 N=10000000 bad=0  19.7 M items/s
unit tests: empty/full/reuse/u32-wrap OK
acq/rel size=16 N=10000000 bad=0  21.4 M items/s
unit tests: empty/full/reuse/u32-wrap OK
BROKEN(relaxed) size=16 N=10000000 bad=366200  143.3 M items/s
exit=1
unit tests: empty/full/reuse/u32-wrap OK
BROKEN(relaxed) size=16 N=10000000 bad=357258  140.6 M items/s
exit=1
unit tests: empty/full/reuse/u32-wrap OK
BROKEN(relaxed) size=16 N=10000000 bad=367503  144.2 M items/s
exit=1
```

출력에서 볼 것:

- 단위 테스트가 경계 4개(빈, 정확히 가득, 슬롯 재사용, **u32 wrap** — `0xfffffffe`에서 시작해 head가 2로 넘어감)를 통과했다.
- acquire/release 버전은 1천만 개 중 오류 0.
- **relaxed 버전은 1천만 개 중 약 36만 개(3.6%)가 틀린 값**이었다. 세 번 다 재현. 빠른 것(143 M/s)은 의미 없다 — 틀린 답을 빨리 낸 것이다.

정말 CPU 재배치 때문인가? 컴파일러가 명령 순서를 바꾼 것은 아닌지 objdump로 확인했다.

```sh
gcc -std=gnu11 -O2 -pthread -c ring_user.c -o a.o              # acq/rel
gcc -std=gnu11 -O2 -pthread -DBROKEN -c ring_user.c -o b.o     # relaxed
for o in a.o b.o; do echo "== $o"; objdump -d --no-show-raw-insn $o | sed -n "/<pop>:/,/^$/p"; done
```

```text
== a.o
0000000000000090 <pop>:
  90:	add	x3, x0, #0x40
  94:	ldr	w2, [x3]
  98:	ldar	w4, [x0]
  9c:	cmp	w2, w4
  a0:	b.eq	c8 <pop+0x38>  // b.none
  a4:	ldr	w4, [x0, #128]
  a8:	ldr	x0, [x0, #136]
  ac:	and	w4, w2, w4
  b0:	add	w2, w2, #0x1
  b4:	ldr	x0, [x0, x4, lsl #3]
  b8:	str	x0, [x1]
  bc:	stlr	w2, [x3]
  c0:	mov	w0, #0x1                   	// #1
  c4:	ret
  c8:	mov	w0, #0x0                   	// #0
  cc:	ret

== b.o
0000000000000090 <pop>:
  90:	add	x3, x0, #0x40
  94:	ldr	w2, [x3]
  98:	ldr	w4, [x0]
  9c:	cmp	w2, w4
  a0:	b.eq	c8 <pop+0x38>  // b.none
  a4:	ldr	w4, [x0, #128]
  a8:	ldr	x0, [x0, #136]
  ac:	and	w4, w2, w4
  b0:	add	w2, w2, #0x1
  b4:	ldr	x0, [x0, x4, lsl #3]
  b8:	str	x0, [x1]
  bc:	str	w2, [x3]
  c0:	mov	w0, #0x1                   	// #1
  c4:	ret
  c8:	mov	w0, #0x0                   	// #0
  cc:	ret
```

push도 같은 모양이다: 데이터 `str` 다음 head에 `stlr`(a.o) / `str`(b.o).

출력에서 볼 것: 명령 **순서는 완전히 같고**, 다른 것은 `ldar`→`ldr`, `stlr`→`str` 두 글자뿐이다. 그런데 결과가 3.6% 틀린다. 즉 깨진 원인은 컴파일러가 아니라 **M2 코어가 실제로 load/store 순서를 바꿔 보이게 한 것**이다. 소비자의 데이터 load(`b4`)는 slot 주소가 자기 `tail`에만 의존하므로 `head` load(`98`)를 기다리지 않고 먼저 실행될 수 있다. Don이 SSD FW에서 "DMA 디스크립터 owner 비트 쓰기 전에 `dmb`"를 넣던 것과 같은 이야기다(F5 §6.1).

### 4.6 IRQ-safe 변형 — 생산자가 hardirq일 때 (spin_lock_irqsave)

생산자가 hrtimer 콜백(hardirq)이고 소비자가 프로세스 컨텍스트면, 가장 단순하고 안전한 답은 spinlock이다. 핵심은 **프로세스 쪽에서 `_irqsave`**: 소비자가 락을 쥔 순간 같은 CPU에 타이머 IRQ가 오면, ISR이 같은 락에서 영원히 돈다(F4 §4.3에서 실제 self-deadlock 재현).

```c
// r3_tmr.c (1/2) — IRQ-safe ring: hrtimer (hardirq) produces, process ctx consumes
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/delay.h>

#define RSZ 128				/* power of 2 */
struct irq_ring {
	spinlock_t lock;		/* producer is in hardirq -> irqsave */
	u32 head, tail;
	u64 buf[RSZ];
	unsigned long drops;
};
static struct irq_ring rg;
static struct hrtimer tmr;
static u64 seq;

static enum hrtimer_restart tmr_fn(struct hrtimer *t)	/* hardirq ctx */
{
	spin_lock(&rg.lock);		/* IRQs already off here */
	if (rg.head - rg.tail < RSZ)
		rg.buf[rg.head++ & (RSZ - 1)] = seq;
	else
		rg.drops++;		/* never block in IRQ: count and drop */
	seq++;
	spin_unlock(&rg.lock);
	hrtimer_forward_now(t, us_to_ktime(200));	/* 5 kHz "sensor" */
	return HRTIMER_RESTART;
}
```

```c
// r3_tmr.c (2/2) — consumer with spin_lock_irqsave, test driver
static unsigned int drain(u64 *last)	/* process ctx */
{
	unsigned long flags;
	unsigned int n = 0;

	spin_lock_irqsave(&rg.lock, flags);	/* else timer IRQ on this CPU
						 * spins on our lock: deadlock */
	while (rg.tail != rg.head) {
		*last = rg.buf[rg.tail++ & (RSZ - 1)];
		n++;
	}
	spin_unlock_irqrestore(&rg.lock, flags);
	return n;
}

static int __init r3_tmr_init(void)
{
	u64 last = 0, got = 0;
	int i;

	spin_lock_init(&rg.lock);
	hrtimer_setup(&tmr, tmr_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_start(&tmr, us_to_ktime(200), HRTIMER_MODE_REL);
	for (i = 0; i < 20; i++) {		/* 20 x 10 ms = 200 ms */
		msleep(i == 10 ? 30 : 10);	/* one slow pass -> overflow */
		got += drain(&last);
	}
	hrtimer_cancel(&tmr);			/* waits if callback running */
	got += drain(&last);
	pr_info("r3_tmr: produced=%llu consumed=%llu drops=%lu last_seq=%llu\n",
		seq, got, rg.drops, last);
	return 0;
}
static void __exit r3_tmr_exit(void) { }
module_init(r3_tmr_init);
module_exit(r3_tmr_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("R3 whiteboard: r3_tmr");
```

```sh
bash run.sh tmr
```

```text
lab initramfs: 6.18.55-trace up 2.04s
[    2.543494][   T76] r3_tmr: produced=1694 consumed=1642 drops=52 last_seq=1693
```

출력에서 볼 것: `produced = consumed + drops`(1694 = 1642 + 52)로 샘플이 하나도 "조용히" 사라지지 않았다. drop은 일부러 30 ms 늦게 비운 한 번에서만 났다(5 kHz × 약 34 ms ≈ 170개 > 128칸). `last_seq=1693`은 마지막 샘플까지 받았다는 뜻이다. 200 ms를 의도했는데 1694개(약 340 ms 분량)가 나온 것은 `msleep(10)`이 HZ=250에서 jiffy 단위로 올림되어 실제로는 더 길게 자기 때문이다(F8 §6, TCG 영향도 있음).

`hrtimer_setup`은 6.13+의 이름이고 옛 `hrtimer_init` + `t.function = fn`은 6.18에서 빌드되지 않는다(F8 §6.2). PREEMPT_RT에서는 `HRTIMER_MODE_REL`이 softirq 컨텍스트로 옮겨지고, 진짜 hardirq가 필요하면 `HRTIMER_MODE_REL_HARD`를 쓴다(`include/linux/hrtimer.h` 32행 주석).

### 4.7 비교 — 내 SPSC vs kfifo vs spinlock 링 vs mutex 링

꼬리 질문 "더 빠르게 할 수 있나?"의 답은 **상대 인덱스를 캐시**하는 것이다. 생산자는 "가득 찬 것처럼 보일 때만" `tail`을 다시 읽고, 소비자는 "빈 것처럼 보일 때만" `head`를 다시 읽는다. 공유 캐시 라인 왕복이 줄어든다. 아래는 그 비교 벤치(`ring_bench.c`, 75줄)의 생산자 부분 발췌다.

```c
	uint32_t h = 0, t_cache = 0;
	for (uint64_t i = 1; i <= N; i++) {
		for (;;) {
			/* ... mode 0 (mutex) 생략 ... */
			if (h - t_cache >= SZ || mode == 1)	/* reload only if looks full */
				t_cache = atomic_load_explicit(&tail, memory_order_acquire);
			if (h - t_cache < SZ) break;
		}
		if (mode) { buf[h % SZ] = i; atomic_store_explicit(&head, ++h, memory_order_release); }
	}
```

```text
mutex           bad=0    4.7 M items/s
acq/rel         bad=0   23.7 M items/s
acq/rel+cached  bad=0   56.3 M items/s
mutex           bad=0    4.7 M items/s
acq/rel         bad=0   22.5 M items/s
acq/rel+cached  bad=0   66.6 M items/s
mutex           bad=0    4.4 M items/s
acq/rel         bad=0   25.7 M items/s
acq/rel+cached  bad=0   57.8 M items/s
```

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
  <line x1="180" y1="28" x2="180" y2="170" stroke="currentColor"/>
  <text x="170" y="60" font-size="13" text-anchor="end">mutex 링</text>
  <rect x="180" y="40" width="33" height="30" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <text x="221" y="60" font-size="12">4.7 M/s</text>
  <text x="170" y="105" font-size="13" text-anchor="end">acq/rel</text>
  <rect x="180" y="85" width="166" height="30" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="354" y="105" font-size="12">23.7 M/s</text>
  <text x="170" y="150" font-size="13" text-anchor="end">acq/rel + 캐시</text>
  <rect x="180" y="130" width="405" height="30" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <text x="593" y="150" font-size="12">57.8 M/s</text>
  <text x="20" y="195" font-size="12">relaxed(틀린 코드)는 143 M/s지만 1천만 개 중 약 36만 개가 틀린 값 — 속도 비교 대상이 아니다</text>
  <text x="20" y="215" font-size="12">Lima VM (Apple M2) 유저 공간 실측, 3회 중앙값, N = 1천만, 링 1024칸, 2스레드</text>
</svg>
```

그림 4 — 같은 SPSC라도 락이 없으면 5배, 상대 인덱스를 캐시하면 다시 2.4배 빠르다. 숫자는 이 VM에서의 값이고 SoC마다 다르다.

| 선택지 | 언제 | 장점 | 주의 |
|---|---|---|---|
| 직접 짠 SPSC (acquire/release) | 정확히 1:1, 다른 CPU, 핫패스 | 락 없음, 가장 빠름 | 1:1이 깨지면 즉시 버그. 리뷰어가 배리어를 이해해야 함 |
| `kfifo` | 1:1이면 락 없이, 아니면 `kfifo_in_spinlocked` | 커널 표준, `kfifo_to_user` 제공, 리뷰 쉬움 | 크기 2의 거듭제곱으로 올림. `kfifo_reset`은 락 필요 |
| spinlock 링 (`_irqsave`) | 생산자가 IRQ, 소비자가 프로세스, 같은 CPU 가능 | 생각할 것이 적음, 다중 생산자도 OK | 락 안에서 잠들기 금지, `copy_to_user` 금지 |
| mutex + wait queue | 둘 다 프로세스 컨텍스트, 블로킹 필요 | 문제 1처럼 블로킹 API를 쉽게 | IRQ에서 못 씀 |

### 4.8 면접관이 기다리는 실수

- `count` 필드를 양쪽이 같이 갱신한다(SPSC가 아니게 된다 — 은행 Q56~62의 count 버전은 단일 컨텍스트용).
- `volatile`로 해결하려 한다. `volatile`은 컴파일러만 막고 CPU 재배치는 못 막는다(위 실험: `ldr`/`str`도 `volatile` 접근과 같은 명령이다).
- 가득 참 판정을 `(head + 1) % size == tail`로 하면서 크기 N 링에 N개를 넣을 수 있다고 말한다. 그 방식은 한 칸을 비워 둔다(N-1개). free-running 방식은 N개 다 쓴다. 어느 쪽이든 **일관되게** 말하면 된다.
- `% size`를 쓰면서 size가 2의 거듭제곱이 아님 → u32 wrap에서 인덱스가 튄다(0xFFFFFFFF % 6 = 3, 0 % 6 = 0 — 연속이 아님).
- IRQ 생산자에 프로세스 쪽 `spin_lock`(irqsave 없이).

### 4.9 꼬리 문제

| 꼬리 질문 | 답의 방향 |
|---|---|
| "생산자가 둘이면?" | head에 CAS(`cmpxchg`) 또는 생산자 쪽만 spinlock. 아니면 per-CPU 링 여러 개 |
| "false sharing?" | head와 tail을 다른 캐시 라인에(`____cacheline_aligned_in_smp`, 유저는 `_Alignas(64)`) |
| "가득 차면 덮어쓰기(최신 우선)?" | 생산자가 tail을 밀면 tail을 두 쪽이 쓰게 됨 → SPSC가 깨짐. seqlock 또는 락 필요 |
| "유저와 커널이 mmap으로 링을 공유하면?" | 같은 acquire/release 규칙 + 인덱스 검증(유저가 아무 값이나 쓸 수 있다). io_uring이 이 구조 |
| "DMA 링이면?" | CPU 간 배리어가 아니라 `dma_wmb()` + doorbell `writel`(F5 §6) |

---

## 5. 문제 4 — `container_of` 구현과 `list_head`

### 5.1 면접관은 이렇게 말한다

> "Implement container_of. Why does the kernel embed list nodes inside structures instead of having nodes point to data? Show me how you'd iterate and delete safely."

### 5.2 먼저 물어볼 것

- "GNU 확장(`typeof`, statement expression)을 써도 되나?" — 커널 스타일이면 된다. 표준 C만이면 타입 검사 없는 버전을 쓴다.
- "순회 중 삭제가 필요한가?" — 그렇다면 `_safe` 변형.

### 5.3 그림 — 멤버 포인터에서 구조체 시작 주소로

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="40" font-size="13">s = 0xc8c2ca246020</text>
  <line x1="80" y1="48" x2="80" y2="86" stroke="#4a7bd0" marker-end="url(#ah4)"/>
  <text x="336" y="40" font-size="13" text-anchor="middle">ptr = &amp;s-&gt;node = 0xc8c2ca246030</text>
  <line x1="336" y1="48" x2="336" y2="86" stroke="#3f9a6b" marker-end="url(#ah4)"/>
  <rect x="80" y="90" width="64" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="112" y="117" font-size="13" text-anchor="middle">seq</text>
  <rect x="144" y="90" width="96" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="192" y="117" font-size="13" text-anchor="middle">accel[3]</text>
  <rect x="240" y="90" width="96" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="288" y="117" font-size="13" text-anchor="middle">pad</text>
  <rect x="336" y="90" width="128" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="400" y="117" font-size="13" text-anchor="middle">node.next</text>
  <rect x="464" y="90" width="128" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="528" y="117" font-size="13" text-anchor="middle">node.prev</text>
  <text x="80" y="152" font-size="12" text-anchor="middle">0</text>
  <text x="144" y="152" font-size="12" text-anchor="middle">4</text>
  <text x="240" y="152" font-size="12" text-anchor="middle">10</text>
  <text x="336" y="152" font-size="12" text-anchor="middle">16</text>
  <text x="464" y="152" font-size="12" text-anchor="middle">24</text>
  <text x="592" y="152" font-size="12" text-anchor="middle">32</text>
  <line x1="336" y1="170" x2="82" y2="170" stroke="#d0564a" marker-end="url(#ah4)"/>
  <text x="208" y="190" font-size="12" text-anchor="middle">container_of(ptr, struct sample, node) = ptr - 16</text>
  <rect x="20" y="228" width="90" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="65" y="253" font-size="12" text-anchor="middle">q (head)</text>
  <rect x="170" y="223" width="180" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="170" y="223" width="80" height="50" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="210" y="253" font-size="12" text-anchor="middle">node</text>
  <text x="300" y="253" font-size="12" text-anchor="middle">seq=1</text>
  <rect x="420" y="223" width="180" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="420" y="223" width="80" height="50" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="460" y="253" font-size="12" text-anchor="middle">node</text>
  <text x="550" y="253" font-size="12" text-anchor="middle">seq=3</text>
  <line x1="110" y1="248" x2="168" y2="248" stroke="currentColor" marker-end="url(#ah4)"/>
  <path d="M210,223 L210,210 L460,210 L460,221" fill="none" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="335" y="205" font-size="12" text-anchor="middle">next</text>
  <path d="M460,273 L460,290 L65,290 L65,270" fill="none" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="560" y="288" font-size="12" text-anchor="middle">next (원형)</text>
</svg>
```

그림 5 — 위: 실행에서 찍힌 실제 주소. 리스트가 넘겨주는 것은 `node`의 주소(…030)이고, 오프셋 16을 빼면 객체 시작(…020)이다. 아래: 리스트는 `node`끼리만 잇는다(prev 화살표는 생략). 짝수 seq를 지운 뒤 남은 1, 3.

### 5.4 풀이 — 화이트보드 버전 (유저 공간에서 실행)

E5 §1.3, §2.3에 같은 주제의 더 긴 버전과 커널 모듈 실습이 있다. 여기서는 **면접장에서 쓸 최소 버전**만.

```c
// cof.c — whiteboard container_of + minimal list_head (kernel semantics)
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define container_of(ptr, type, member) ({				\
	const typeof(((type *)0)->member) *__mptr = (ptr);		\
	(type *)((char *)__mptr - offsetof(type, member)); })

struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(n) { &(n), &(n) }
static void list_add_tail(struct list_head *n, struct list_head *h)
{	/* insert between h->prev and h */
	n->prev = h->prev; n->next = h;
	h->prev->next = n; h->prev = n;
}
static void list_del(struct list_head *e)
{
	e->prev->next = e->next; e->next->prev = e->prev;
	e->next = e->prev = NULL;		/* kernel poisons instead */
}
#define list_entry(p, t, m) container_of(p, t, m)
#define list_for_each_safe(p, n, h) \
	for (p = (h)->next, n = p->next; p != (h); p = n, n = p->next)

struct sample {				/* the link lives INSIDE the object */
	unsigned int seq;
	short accel[3];
	struct list_head node;
};

int main(void)
{
	struct list_head q = LIST_HEAD_INIT(q), *p, *n;
	struct sample *s;

	printf("offsetof(node)=%zu sizeof(sample)=%zu\n",
	       offsetof(struct sample, node), sizeof(struct sample));
	for (unsigned int i = 0; i < 4; i++) {
		s = calloc(1, sizeof(*s));
		if (!s) { perror("calloc"); exit(1); }
		s->seq = i;
		list_add_tail(&s->node, &q);
	}
	s = list_entry(q.next, struct sample, node);
	printf("first: obj=%p &node=%p diff=%td seq=%u\n", (void *)s,
	       (void *)&s->node, (char *)&s->node - (char *)s, s->seq);
	list_for_each_safe(p, n, &q) {		/* _safe: we free while walking */
		s = list_entry(p, struct sample, node);
		if (s->seq % 2 == 0) { list_del(p); free(s); }
	}
	list_for_each_safe(p, n, &q)
		printf("left seq=%u\n", list_entry(p, struct sample, node)->seq);
	list_for_each_safe(p, n, &q) { list_del(p); free(list_entry(p, struct sample, node)); }
	printf("empty: %s\n", q.next == &q ? "yes" : "no");
	/* type check: container_of(&s->seq, struct sample, node) -> warning */
	return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 cof.c -o cof && ./cof
```

```text
offsetof(node)=16 sizeof(sample)=32
first: obj=0xc8c2ca246020 &node=0xc8c2ca246030 diff=16 seq=0
left seq=1
left seq=3
empty: yes
```

출력에서 볼 것: `seq`(4) + `accel[3]`(6) = 10바이트 뒤에 포인터 정렬(8)을 맞추려고 6바이트 패딩이 들어가 `node`가 16에 있다. 주소 차이도 정확히 16. 주소 값은 실행마다 다르다.

타입 검사가 왜 중요한지 — 마지막 주석처럼 엉뚱한 멤버(`seq`)를 넘기면:

```sh
gcc -std=gnu11 -Wall -Wextra -c bad.c -o /dev/null   # container_of(&s->seq, struct sample, node)
```

```text
bad.c:2:88: error: initialization of ‘const struct list_head *’ from incompatible pointer type ‘unsigned int *’ [-Wincompatible-pointer-types]
```

출력에서 볼 것: `__mptr` 임시 변수에 멤버 타입을 붙여 두었기 때문에 잘못된 포인터가 컴파일 단계에서 걸린다(gcc 15에서는 경고가 아니라 에러). 6.18 커널의 진짜 정의는 `static_assert(__same_type(*(ptr), ((type *)0)->member) ...)`로 같은 검사를 한다(E5 §1.5).

### 5.5 토론과 함정

| 주제 | 말할 내용 |
|---|---|
| 비용 | 상수 빼기 한 번. 런타임 비용 사실상 0(E5 §1.4) |
| intrusive의 이점 | 노드 할당이 따로 없다(malloc 한 번), 한 객체가 여러 리스트에 동시에(멤버 여러 개), 캐시 친화 |
| `offsetof(type, member)` 구현 | `((size_t)&((type *)0)->member)`가 고전 구현. 현대 컴파일러는 `__builtin_offsetof` — "NULL 역참조 같은데 왜 안전하냐"는 꼬리 질문에 "주소 계산만 하고 load가 없다, 그래도 표준상 UB라서 builtin을 쓴다" |
| `list_for_each` 중 삭제 | 지운 노드의 `next`를 따라가면 use-after-free. `_safe`는 다음 포인터를 미리 저장(E5 §2.5에서 oops 재현) |
| 커널 `list_del` | NULL이 아니라 `LIST_POISON1/2`로 채워서 잘못된 재사용이 즉시 oops로 보이게 한다 |
| 락 | `list_head` 자체는 락이 없다. 리스트 하나에 락 하나, 또는 RCU(`list_for_each_entry_rcu`) |

꼬리 문제: "`hlist`는 왜 있나?"(해시 버킷: head가 포인터 하나라 메모리 절반, E5 §2.6) · "`kref`로 수명 관리는?"(E5 §2.7) · "`container_of`가 `const`를 버리는 문제"(`container_of_const`: 6.18 `include/linux/container_of.h` 주석이 "새 코드에서는 항상 이것을 쓰라"고 한다).

---

## 6. 문제 5 — 비트 조작 세트와 RMW 경쟁

### 6.1 면접관은 이렇게 말한다

> "Here is a control register: bit 31 enable, bits 6:4 output data rate, bits 1:0 range. Write helpers to set and read a field, then enable the device. Two drivers share this register — what can go wrong?"

### 6.2 먼저 물어볼 것

- "레지스터 폭은 32비트인가? 쓰기 전용 비트나 write-1-to-clear 비트가 있나?" — W1C가 섞이면 RMW가 **다른 비트를 지워 버린다**.
- "리셋 값은?" — 처음 읽은 값을 기준으로 RMW할지, 알려진 값을 통째로 쓸지.
- "다른 주체(다른 드라이버, 펌웨어, 다른 코어)가 같은 레지스터를 만지나?"

### 6.3 풀이 — 커널 매크로를 유저 공간에 다시 만들기

set/clear/toggle/popcount/swap은 은행 01·02번에 이미 있으므로 짧게, **커널 매크로 이름과 함정**에 집중한다.

```c
// bits.c — kernel-style bit helpers re-implemented for a whiteboard + tests
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define BIT(n)		(1UL << (n))
#define GENMASK(h, l)	(((~0UL) << (l)) & (~0UL >> (63 - (h))))   /* 64-bit long */
#define FIELD_SHIFT(m)	__builtin_ctzl(m)
#define FIELD_PREP(m, v) (((unsigned long)(v) << FIELD_SHIFT(m)) & (m))
#define FIELD_GET(m, r)	(((r) & (m)) >> FIELD_SHIFT(m))

/* datasheet: CTRL @0x10 = [31]EN [6:4]ODR [1:0]RANGE */
#define CTRL_EN		BIT(31)
#define CTRL_ODR	GENMASK(6, 4)
#define CTRL_RANGE	GENMASK(1, 0)

static uint32_t update_field(uint32_t reg, uint32_t mask, uint32_t val)
{
	return (reg & ~mask) | FIELD_PREP(mask, val);	/* clear, then set */
}
static int popcount32(uint32_t x)
{	/* Kernighan: each step clears the lowest set bit */
	int n = 0;
	for (; x; x &= x - 1)
		n++;
	return n;
}
static uint32_t swab32(uint32_t x)
{
	return (x >> 24) | ((x >> 8) & 0xff00) | ((x << 8) & 0xff0000) | (x << 24);
}

int main(void)
{
	uint32_t r = 0x0000008e;		/* reset value from datasheet */

	assert(GENMASK(6, 4) == 0x70 && GENMASK(31, 0) == 0xffffffffUL);
	r = update_field(r, CTRL_ODR, 5);		/* ODR=5 */
	r = update_field(r, CTRL_RANGE, 2);
	r |= CTRL_EN;					/* set */
	printf("CTRL=%#010x ODR=%lu RANGE=%lu EN=%d\n", r,
	       FIELD_GET(CTRL_ODR, r), FIELD_GET(CTRL_RANGE, r), !!(r & CTRL_EN));
	r &= ~CTRL_EN;					/* clear */
	r ^= BIT(3);					/* toggle */
	printf("after clear EN, toggle b3: %#010x\n", r);
	assert(FIELD_PREP(CTRL_ODR, 9) == FIELD_PREP(CTRL_ODR, 1)); /* 9 truncated! */
	assert(popcount32(0xf0f0) == 8 && popcount32(0) == 0);
	assert(popcount32(0xdeadbeef) == __builtin_popcount(0xdeadbeef));
	assert(swab32(0x11223344) == 0x44332211 && swab32(0x11223344) == __builtin_bswap32(0x11223344));
	printf("~CTRL_EN as u32 = %#x, sizeof(BIT(0)) = %zu\n", (uint32_t)~CTRL_EN, sizeof(BIT(0)));
	puts("bit tests OK");
	return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 bits.c -o bits && ./bits
```

```text
CTRL=0x800000de ODR=5 RANGE=2 EN=1
after clear EN, toggle b3: 0x000000d6
~CTRL_EN as u32 = 0x7fffffff, sizeof(BIT(0)) = 8
bit tests OK
```

출력에서 볼 것:

- 리셋 값 0x8e(ODR 필드에 0, RANGE에 2, 다른 비트 1·2·3·7 세트)에서 ODR만 5로 바꾸고 나머지 비트를 보존했다: 0x8e → 0xde.
- `sizeof(BIT(0)) = 8` — `BIT()`는 **`unsigned long`**(arm64에서 64비트)이다. 이것 때문에 실제로 커널 빌드 경고가 났다(§8.2의 I2C 드라이버 첫 빌드):

```text
r3_i2c.c:38:23: warning: format ‘%x’ expects argument of type ‘unsigned int’, but argument 5 has type ‘long unsigned int’ [-Wformat=]
```

`pwr & ~R3_SLEEP`(`s32 & unsigned long`)의 결과가 `unsigned long`이 되어 `%x`와 안 맞았다. 8비트 레지스터 값을 `u8` 변수에 받아서 고쳤다. 32비트 레지스터에 `~BIT(n)`을 쓸 때 상위 32비트가 1로 채워지는 것도 같은 뿌리다(값은 잘려서 맞지만, 비교·시프트에서 놀란다).

- 내 `FIELD_PREP(CTRL_ODR, 9)`는 9를 조용히 1로 잘랐다(assert 통과). 커널 `FIELD_PREP`은 상수 값이 필드를 넘치면 **컴파일 에러**다:

```c
#include <linux/module.h>
#include <linux/bitfield.h>
#define CTRL_ODR GENMASK(6, 4)
static int __init f_init(void)
{
	pr_info("%lu\n", FIELD_PREP(CTRL_ODR, 9));	/* 9 does not fit in 3 bits */
	return 0;
}
module_init(f_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("x");
```

```text
/var/lab/src/linux-6.18.55/include/linux/compiler_types.h:610:45: error: call to ‘__compiletime_assert_415’ declared with attribute error: FIELD_PREP: value too large for the field
```

출력에서 볼 것: 이게 커널 매크로를 쓰는 이유다. 마스크와 시프트를 손으로 쓰면 이 검사가 없다. (런타임 값이면 검사는 안 되고 마스크로 잘린다 — 범위 검사는 호출자 책임.)

### 6.4 공유 레지스터의 RMW 경쟁 — 실제로 업데이트가 사라진다

두 "드라이버"가 한 레지스터의 서로 다른 16비트 필드를 각자 5만 번 올린다. 필드가 겹치지 않으니 안전해 보이지만, read-modify-write 사이에 상대가 쓴 값을 덮어쓴다. H1 §6.2는 같은 실험을 커널 모듈(`h1_race`)로 했다.

```c
// rmw_race.c — two "drivers" update different fields of ONE register
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define N 50000				/* < 65536: a 16-bit field can hold it */
static volatile uint32_t reg;		/* stands in for an MMIO register */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int use_lock;

static void bump(int shift)
{	/* read-modify-write of a 16-bit field */
	if (use_lock)
		pthread_mutex_lock(&lock);
	uint32_t v = reg;				/* read */
	uint32_t f = ((v >> shift) & 0xffff) + 1;	/* modify */
	reg = (v & ~(0xffffu << shift)) | (f << shift);	/* write */
	if (use_lock)
		pthread_mutex_unlock(&lock);
}
static void *drv_a(void *a) { for (int i = 0; i < N; i++) bump(0);  return a; }
static void *drv_b(void *a) { for (int i = 0; i < N; i++) bump(16); return a; }

int main(int argc, char **argv)
{
	pthread_t a, b;

	use_lock = argc > 1 && !strcmp(argv[1], "lock");
	reg = 0;
	pthread_create(&a, NULL, drv_a, NULL);
	pthread_create(&b, NULL, drv_b, NULL);
	pthread_join(a, NULL);
	pthread_join(b, NULL);
	printf("%-7s fieldA=%u fieldB=%u (expect %d each) lost=%u\n",
	       use_lock ? "lock" : "no-lock", reg & 0xffff, reg >> 16, N,
	       2 * N - (reg & 0xffff) - (reg >> 16));
	return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -pthread rmw_race.c -o rmw
for i in 1 2 3; do ./rmw; done; ./rmw lock; ./rmw lock
```

```text
no-lock fieldA=10259 fieldB=50000 (expect 50000 each) lost=39741
no-lock fieldA=15923 fieldB=50000 (expect 50000 each) lost=34077
no-lock fieldA=15005 fieldB=50000 (expect 50000 each) lost=34995
lock    fieldA=50000 fieldB=50000 (expect 50000 each) lost=0
lock    fieldA=50000 fieldB=50000 (expect 50000 each) lost=0
```

출력에서 볼 것: 락 없이 10만 번 중 3만4천~4만 번이 사라졌다. 어느 쪽이 잃는지는 타이밍이 정한다(이 세 번은 모두 A 쪽이 잃었다 — 스레드 시작 순서와 스케줄링에 따라 달라진다). `volatile`은 전혀 도움이 안 된다 — 접근마다 진짜 load/store를 하게 할 뿐, 세 단계를 묶어 주지 않는다.

커널에서의 정답:

| 상황 | 도구 |
|---|---|
| 같은 드라이버 안에서 여러 컨텍스트 | `spin_lock_irqsave`로 RMW를 감싼다(H1 §6.3) |
| regmap을 쓰는 드라이버 | `regmap_update_bits()` — regmap 내부 락이 RMW를 묶는다 |
| 여러 드라이버가 한 레지스터를 공유(클럭 게이트, GPIO 뱅크) | 공유 락을 프레임워크가 제공(`clk-gate`의 `spinlock_t *lock`, gpiochip의 락), 또는 syscon + regmap |
| 하드웨어가 SET/CLR 별칭 레지스터를 준다 | RMW 없이 `writel(BIT(n), base + SET)` — 가장 좋다(i.MX의 `_SET`/`_CLR` 오프셋 등) |
| 메모리 위의 비트맵 | `set_bit`/`clear_bit`(원자), 이미 락 안이면 `__set_bit`(비원자) |

### 6.5 엔디안 — 한 줄씩

- 장치가 빅 엔디안으로 보내는 16비트 값: `be16_to_cpu()` 또는 바이트 버퍼면 `get_unaligned_be16(p)`(§8.2에서 사용).
- 레지스터는 `readl`/`writel`이 **리틀 엔디안 장치**로 가정한다. 빅 엔디안 장치면 `ioread32be`/`iowrite32be`, regmap이면 `.val_format_endian = REGMAP_ENDIAN_BIG`.
- 구조체 필드에 `__le32`/`__be32`를 붙이면 sparse가 변환 누락을 잡는다(`make C=1`, E5 §5.4).

---

## 7. 문제 6 — 레지스터 블록: 구조체 오버레이 vs 오프셋, volatile, packed

### 7.1 면접관은 이렇게 말한다

> "You have a UART register map from a datasheet. Some people overlay a struct on the base address, others use offset macros with readl/writel. Which do you prefer in Linux and why? What are the pitfalls of volatile and packed here?"

### 7.2 30초 답의 골격

Linux 드라이버는 **오프셋 매크로 + `readl`/`writel`**(또는 regmap)이 관례다. 이유 세 가지: (1) 접근 폭과 순서를 접근자가 명시적으로 정한다(`readl`은 32비트 한 번 + 배리어, F5 §5.2) (2) 구조체는 컴파일러 패딩·정렬 규칙에 오프셋을 맡긴다 (3) `volatile` 구조체 필드는 `|=`가 RMW라는 사실을 숨긴다. 구조체 오버레이도 `static_assert(offsetof(...))`로 오프셋을 못 박고 `readl(&regs->fr)`처럼 **접근자를 통해서** 쓰면 괜찮다(§3.6에서 두 방식이 같은 값을 읽었다).

### 7.3 실행 — 패딩 버그, 명시적 reserved, packed가 만드는 코드

```c
// regs.c — register block: struct overlay vs offsets, padding, packed
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct uart_regs_bad {			/* datasheet: DR@0 FR@0x18 IBRD@0x24 */
	uint32_t dr;
	uint8_t  rsr;			/* "only 8 bits used" ... */
	uint32_t fr;			/* compiler pads: lands at 0x08, not 0x18 */
};
struct uart_regs {			/* PL011 subset, every gap is explicit */
	uint32_t dr;			/* 0x00 */
	uint32_t rsr;			/* 0x04 */
	uint32_t reserved0[4];		/* 0x08-0x14 */
	uint32_t fr;			/* 0x18 */
	uint32_t reserved1[2];		/* 0x1c-0x20 */
	uint32_t ibrd;			/* 0x24 */
};
static_assert(offsetof(struct uart_regs, fr) == 0x18, "FR offset");
static_assert(offsetof(struct uart_regs, ibrd) == 0x24, "IBRD offset");

#define UART_DR   0x00			/* offset style (Linux drivers) */
#define UART_FR   0x18
#define UART_IBRD 0x24

struct __attribute__((packed)) pkt {	/* wire format, NOT a register map */
	uint8_t  type;
	uint32_t value;			/* offset 1: unaligned */
};

uint32_t rd_overlay(volatile struct uart_regs *u) { return u->fr; }
uint32_t rd_offset(volatile uint8_t *base) { return *(volatile uint32_t *)(base + UART_FR); }
uint32_t rd_packed(struct pkt *p) { return p->value; }
void set_bit_overlay(volatile struct uart_regs *u) { u->ibrd |= 1u << 3; }

int main(void)
{
	static struct uart_regs fake = { .fr = 0x90, .ibrd = 0x10 };

	printf("bad: offsetof(fr)=%#zx  good: offsetof(fr)=%#zx sizeof=%#zx\n",
	       offsetof(struct uart_regs_bad, fr), offsetof(struct uart_regs, fr),
	       sizeof(struct uart_regs));
	printf("packed: sizeof(pkt)=%zu offsetof(value)=%zu\n",
	       sizeof(struct pkt), offsetof(struct pkt, value));
	assert(rd_overlay(&fake) == rd_offset((volatile uint8_t *)&fake));
	set_bit_overlay(&fake);
	printf("FR overlay=%#x offset=%#x  IBRD after |=: %#x\n", rd_overlay(&fake),
	       rd_offset((volatile uint8_t *)&fake), fake.ibrd);
	return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 regs.c -o regs && ./regs
```

```text
bad: offsetof(fr)=0x8  good: offsetof(fr)=0x18 sizeof=0x28
packed: sizeof(pkt)=5 offsetof(value)=1
FR overlay=0x90 offset=0x90  IBRD after |=: 0x18
```

출력에서 볼 것: "8비트만 쓰는 레지스터니까 `uint8_t`"로 쓰면 다음 필드가 0x08로 밀린다(0x18이어야 함). 데이터시트의 빈칸을 `reserved[]`로 전부 채우고 `static_assert`로 확인해야 한다 — 틀리면 **컴파일이 실패**한다.

```sh
gcc -std=gnu11 -O2 -c regs.c -o regs.o && objdump -d --no-show-raw-insn regs.o | sed -n "/<rd_overlay>:/,/<main>:/p"
```

```text
0000000000000000 <rd_overlay>:
   0:	ldr	w0, [x0, #24]
   4:	ret

0000000000000008 <rd_offset>:
   8:	ldr	w0, [x0, #24]
   c:	ret

0000000000000010 <rd_packed>:
  10:	ldur	w0, [x0, #1]
  14:	ret
  18:	nop
  1c:	nop

0000000000000020 <set_bit_overlay>:
  20:	ldr	w1, [x0, #36]
  24:	orr	w1, w1, #0x8
  28:	str	w1, [x0, #36]
  2c:	ret

Disassembly of section .text.startup:

0000000000000000 <main>:
```

같은 파일을 `-mstrict-align`(정렬 안 된 접근을 쓰지 말라는 옵션)으로 컴파일하면 `rd_packed`가 바뀐다.

```sh
gcc -std=gnu11 -O2 -mstrict-align -c regs.c -o regs_sa.o && objdump -d --no-show-raw-insn regs_sa.o | sed -n "/<rd_packed>:/,/<set_bit/p"
```

```text
0000000000000010 <rd_packed>:
  10:	ldrb	w1, [x0, #1]
  14:	ldrb	w2, [x0, #2]
  18:	orr	x2, x1, x2, lsl #8
  1c:	ldrb	w1, [x0, #3]
  20:	ldrb	w0, [x0, #4]
  24:	orr	x1, x2, x1, lsl #16
  28:	orr	w0, w1, w0, lsl #24
  2c:	ret

0000000000000030 <set_bit_overlay>:
```

출력에서 볼 것:

- 오버레이와 오프셋은 **같은 명령**(`ldr w0, [x0, #24]`)이 된다. 차이는 코드 생성이 아니라 유지보수와 접근 폭 통제다.
- `u->ibrd |= 1u << 3`은 `ldr`/`orr`/`str` 세 명령 — `volatile`이 붙어 있어도 원자적이지 않다(§6.4의 경쟁이 그대로 생긴다).
- packed 구조체의 정렬 안 된 32비트는 arm64 기본 설정에서 `ldur`(비정렬 load) 하나, `-mstrict-align`(커널 일부 아키텍처·Cortex-M 같은 환경)에서는 **바이트 load 4개**로 쪼개진다. Device 메모리(MMIO)에서 비정렬 접근은 arm64에서 alignment fault이고, 바이트 4번 접근은 레지스터가 32비트 접근만 받는다면 잘못된 동작이다(H1 §3.5에서 폭이 다른 접근 실험). 결론: **packed는 와이어 포맷(패킷, 디스크 구조)용이지 레지스터 맵용이 아니다.**

### 7.4 volatile 함정 — 한 표로

| 오해 | 사실 | 커널의 답 |
|---|---|---|
| "volatile이면 MMIO 순서가 보장된다" | 컴파일러 순서만. CPU·버스의 순서, 메모리와 MMIO 사이 순서는 별개(F5 §2, §5) | `readl`/`writel`(배리어 포함), `_relaxed`는 순서가 필요 없을 때만 |
| "volatile이면 스레드 간 공유가 안전하다" | 원자성도 순서도 없다(§4.5, §6.4 실측) | `READ_ONCE`/`WRITE_ONCE` + 락 또는 acquire/release |
| "레지스터 포인터는 `volatile u32 *`" | 커널에서는 `void __iomem *` + 접근자. 직접 역참조는 sparse 경고, 일부 아키텍처에서 오동작(H1 §8) | `readl(base + OFF)` |
| "폴링 루프는 volatile로 충분" | 타임아웃이 없다. 장치가 죽으면 커널이 멈춘다 | `readl_poll_timeout(addr, val, cond, sleep_us, timeout_us)`(H1 §5) |

---

## 8. 문제 7 — 엑스트라 두 개: 디바운스/타이머, I2C client

### 8.1 디바운스: delayed work vs hrtimer vs timer_list

문제 2의 `mod_delayed_work`가 디바운스의 표준 답이다. "매 엣지마다 마감을 뒤로 민다 → 마지막 엣지 후 N ms 조용하면 한 번 실행." 실측: 엣지 5번 → 이벤트 1번(§3.6). 선택지 비교:

| 도구 | 콜백 컨텍스트 | 해상도 | 디바운스에 쓰면 |
|---|---|---|---|
| `delayed_work` (`mod_delayed_work`) | 프로세스(kworker) — 잠들 수 있음 | jiffy (HZ=250이면 4 ms) | 기본 선택. 콜백에서 I2C로 상태를 다시 읽을 수 있다 |
| `timer_list` (`mod_timer`) | softirq — 잠들 수 없음 | jiffy | 가볍지만 콜백에서 버스 접근 불가, 결국 work를 또 건다 |
| `hrtimer` | hardirq(기본), RT에선 softirq | ns 요청(실제는 clockevent 해상도) | 1 ms 미만 정밀 디바운스가 필요할 때. 콜백은 아주 짧게(F8 §7) |
| threaded IRQ + `msleep` | IRQ 스레드 | — | 간단하지만 sleep 동안 같은 IRQ가 막힌다(ONESHOT) |

GPIO 버튼이라면 직접 짜지 말고 `gpio-keys`의 `debounce-interval` DT 속성이나 `gpiod_set_debounce()`(하드웨어 디바운스가 있으면 그걸 쓴다)를 먼저 말한다 — "프레임워크가 이미 하는 것을 다시 만들지 않는다"는 것이 시니어 답이다(I1, I3).

### 8.2 I2C client — `i2c_smbus_*`로 레지스터 읽기

> "Write the probe function of an I2C driver for an IMU at address 0x68: verify WHO_AM_I, take it out of sleep, read the 3-axis accelerometer burst."

먼저 물어볼 것: "레지스터 주소는 8비트인가? 멀티바이트 값은 빅 엔디안인가? 버스트 읽기에서 주소가 자동 증가하나? 어댑터가 SMBus I2C block을 지원하나?" — 마지막 질문이 `i2c_check_functionality`로 이어진다(H5 §4.4).

```c
// r3_i2c.c (1/2) — I2C client: WHO_AM_I + 6-byte burst via i2c_smbus_* helpers
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/unaligned.h>

#define R3_WHO_AM_I	0x75	/* MPU-6050 style map */
#define R3_PWR_MGMT_1	0x6b
#define R3_ACCEL_XOUT_H	0x3b	/* 6 bytes: X_H X_L Y_H Y_L Z_H Z_L */
#define R3_SLEEP	BIT(6)

static int r3i_probe(struct i2c_client *c)
{
	struct device *dev = &c->dev;
	u8 raw[6];
	s32 who, pwr;
	u8 newpwr;
	int ret;

	if (!i2c_check_functionality(c->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
						 I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EOPNOTSUPP;
	who = i2c_smbus_read_byte_data(c, R3_WHO_AM_I);	/* <0 = -errno */
	if (who < 0)
		return dev_err_probe(dev, who, "WHO_AM_I\n");
	if (who != 0x68)
		return dev_err_probe(dev, -ENODEV, "unknown id %#x\n", who);

	pwr = i2c_smbus_read_byte_data(c, R3_PWR_MGMT_1);	/* RMW over bus */
	if (pwr < 0)
		return pwr;
	newpwr = pwr & ~R3_SLEEP;	/* BIT() is unsigned long! */
	ret = i2c_smbus_write_byte_data(c, R3_PWR_MGMT_1, newpwr);
	if (ret)
		return ret;

	ret = i2c_smbus_read_i2c_block_data(c, R3_ACCEL_XOUT_H, 6, raw);
	if (ret != 6)
		return ret < 0 ? ret : -EIO;	/* short read is an error */
	dev_info(dev, "who=%#x pwr %#x->%#x accel x=%d y=%d z=%d\n",
		 who, pwr, newpwr,
		 (s16)get_unaligned_be16(&raw[0]),
		 (s16)get_unaligned_be16(&raw[2]),
		 (s16)get_unaligned_be16(&raw[4]));
	return 0;
}
```

```c
// r3_i2c.c (2/2) — match tables: id_table for new_device, of for DT
static const struct i2c_device_id r3i_id[] = {
	{ "r3-imu" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, r3i_id);

static const struct of_device_id r3i_of[] = {
	{ .compatible = "don,r3-imu" },
	{ }
};
MODULE_DEVICE_TABLE(of, r3i_of);

static struct i2c_driver r3i_driver = {
	.driver = { .name = "r3_imu", .of_match_table = r3i_of },
	.probe = r3i_probe,
	.id_table = r3i_id,
};
module_i2c_driver(r3i_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("R3 whiteboard: r3_i2c");
```

실행: 업스트림 `drivers/i2c/i2c-stub.c`(Image-trace에는 꺼져 있다)를 out-of-tree로 같은 트리에 빌드(W=1 경고 0)하고, `i2cset`으로 가짜 칩의 레지스터를 채운 뒤 `new_device`로 client를 만든다(H5 §2.3).

```sh
bash run.sh i2c
```

```text
lab initramfs: 6.18.55-trace up 2.00s
i2c-0
0x00
lrwxrwxrwx    1 0        0                0 Oct 18 22:44 /sys/bus/i2c/devices/0-0068/driver -> ../../../bus/i2c/drivers/r3_imu
[    2.160755][   T76] i2c-stub: Virtual chip at 0x68
[    2.236363][   T75] r3_imu 0-0068: who=0x68 pwr 0x40->0x0 accel x=256 y=-200 z=16384
[    2.236905][   T75] i2c i2c-0: new_device: Instantiated device r3-imu at 0x68
```

출력에서 볼 것:

- `new_device`에 쓴 이름 `r3-imu`가 `id_table`과 매칭돼 probe됐다(DT가 없으므로 OF 매칭이 아니라 id_table, H5 §2.5).
- PWR_MGMT_1이 0x40(SLEEP) → 0x00으로 바뀌었고, 그 뒤 `i2cget -f`가 0x00을 읽었다. `-f`는 커널 드라이버가 잡고 있는 주소에 강제로 접근하는 옵션이다(없으면 `Device or resource busy`).
- 버스트 6바이트 `01 00 ff 38 40 00`이 빅 엔디안 s16으로 256, -200, 16384가 됐다. 16384는 ±2g 범위에서 1g다. `(s16)` 캐스트를 빼면 -200이 65336이 된다.
- 반환값 규칙: `i2c_smbus_read_byte_data`는 **값 또는 음수 errno**를 `s32`로 준다 — `u8`에 바로 받으면 에러를 놓친다. block read는 **읽은 바이트 수**를 주므로 6과 비교한다.

꼬리 문제: "SPI 버전은?"(`spi_write_then_read`, H7) · "regmap으로 바꾸면?"(`devm_regmap_init_i2c` + `regmap_bulk_read`, H2) · "인터럽트 핀이 있다면?"(`devm_request_threaded_irq(..., NULL, fn, IRQF_ONESHOT, ...)` — 스레드에서 I2C 읽기, F7 §8) · "IIO로 노출하라"(I2).

---

## 9. 커널 안에서는 무슨 일이 일어나나

이 노트의 코드가 커널의 어떤 경로에 걸리는지 한 표로 묶는다. R2("무슨 일이 일어나나" 시리즈)가 각 경로를 처음부터 끝까지 따라간다.

| 유저/HW 사건 | 커널 경로 | 우리 코드 | 깊은 설명 |
|---|---|---|---|
| `open("/dev/r3_chr")` | `chrdev_open` → `misc_open` → `file->private_data = &misc` | `to_r3()`의 `container_of` | G7 §3 |
| `read()` 빈 FIFO | `vfs_read` → `.read` → `wait_event_interruptible` → `schedule()` | `r3_read` | F3 §1, §5 |
| `poll()`/`epoll_wait()` | `do_sys_poll`/`ep_poll` → `.poll` → `poll_wait`로 wait queue에 엔트리 추가 | `r3_poll` | G6 §1 |
| `ioctl()` | `vfs_ioctl` → `.unlocked_ioctl`, 모르는 cmd는 `-ENOTTY` | `r3_ioctl` | G5 §3 |
| `insmod`/DT 노드 | `of_platform_populate`가 만든 `platform_device` ↔ `of_match_table` → `really_probe` | `r3s_probe` | D3, G2, G3 |
| unbind / probe 실패 | `device_unbind_cleanup` → `devres_release_all`(역순) | `.remove` 없음 | G3 §7 |
| 엣지 IRQ | GIC → `handle_fasteoi_irq`/`handle_edge_irq` → `r3s_isr` → `mod_delayed_work` → kworker | `r3s_isr`, `r3s_deb_work` | F6, F7 §6 |
| hrtimer 만료 | `hrtimer_interrupt`(hardirq) → `tmr_fn` | `r3_tmr` | F8 §7 |
| I2C 읽기 | `i2c_smbus_xfer` → adapter `smbus_xfer`(i2c-stub) 또는 `i2c_transfer` 에뮬레이션 | `r3i_probe` | H5 §4 |

---

## 10. Embedded 실무에서 다시 보기

- **센서 키트의 IMU 드라이버**: 문제 2 + 문제 7이 합쳐진 모양이 실무 첫 드라이버다. DRDY 인터럽트 → threaded IRQ → regmap bulk read → 링 버퍼(문제 3) → IIO buffer 또는 misc+poll(문제 1)로 유저에게. 처음부터 직접 짜기보다 IIO의 triggered buffer를 쓰는 것이 보통 정답이다(I2).
- **보조 MCU와의 링**: Linux와 Cortex-M이 공유 메모리 링을 쓰면 문제 3의 acquire/release가 그대로 나오고, 캐시·배리어가 `dma_wmb`/`dma_rmb`로 바뀐다(O3, H10).
- **필드 버그 "가끔 샘플이 깨진다"**: §4.5의 relaxed 실험은 3.6%였지만 실제 장비에서는 0.001%일 수도 있다. 테스트로는 안 잡히고 리뷰로 잡아야 한다 — 그래서 면접관이 배리어 설명을 요구한다(R4의 "가끔 데이터가 깨진다" 시나리오).
- **공유 레지스터**: 클럭 게이트·GPIO 출력 레지스터를 두 드라이버가 같이 만지는 보드에서 §6.4가 실제로 일어난다. 증상은 "가끔 다른 장치가 꺼진다".

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 락을 쥔 채 `wait_event` | writer가 영원히 블록, hung task 경고 | 깨워 줄 쪽이 락을 못 잡음 | 락 놓고 자고, 깨면 다시 잡고 조건 재확인 |
| spinlock 안에서 `copy_to_user` | 가끔 "BUG: sleeping function called from invalid context" | page fault로 잠듦 | mutex, 또는 로컬 버퍼에 복사 후 락 밖에서 `copy_to_user` |
| 프로세스 쪽 `spin_lock`(irqsave 없음), IRQ도 같은 락 | 가끔 CPU 하나가 영원히 멈춤, soft lockup | 같은 CPU에서 ISR이 자기 락을 기다림 | `spin_lock_irqsave` (F4 §4.3) |
| IRQ를 probe 앞부분에 요청 | 부팅 시 가끔 NULL 역참조 oops | drvdata·work가 준비 전 ISR 실행 | IRQ 요청을 맨 마지막에 |
| devm IRQ + 수동 kfree | unbind 직후 use-after-free | devres가 remove 뒤에 IRQ를 품 | 메모리도 devm, 또는 remove에서 `disable_irq`/`free_irq` 먼저 |
| SPSC를 `volatile`로만 | 드물게 옛 값·중복 샘플 | CPU 재배치(§4.5에서 3.6%) | `smp_store_release`/`smp_load_acquire` 또는 kfifo |
| 링 크기 비 2의 거듭제곱 + `% size` + u32 wrap | 오래 돌린 뒤 순서 뒤섞임 | wrap에서 인덱스 불연속 | 크기 2^n + `& mask`, 또는 인덱스를 `size`로 wrap |
| 공유 레지스터 RMW에 락 없음 | 다른 기능이 가끔 꺼짐/켜짐 | lost update(§6.4에서 35~40%) | 공유 락, `regmap_update_bits`, SET/CLR 레지스터 |
| 레지스터 구조체 패딩 착오 | 엉뚱한 레지스터를 읽음 | 컴파일러 정렬 | `reserved[]` + `static_assert(offsetof)` |
| `i2c_smbus_read_byte_data` 결과를 `u8`에 | NACK인데 0xEA 같은 쓰레기 값 사용 | 음수 errno가 잘림 | `s32`로 받고 `< 0` 검사 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "Walk me through the blocking read path of your driver. What happens if a signal arrives?"

**A.** (30초) 빈 FIFO면 mutex를 놓고 `wait_event_interruptible`로 wait queue에서 잔다. writer가 데이터를 넣고 `wake_up`하면 깨어나 락을 다시 잡고 조건을 `while`로 재확인한 뒤 `copy_to_user`한다. 시그널이 오면 `-ERESTARTSYS`를 돌려주고, VFS가 `SA_RESTART`에 따라 재시작하거나 `-EINTR`로 바꾼다.

(2분) 세 가지를 덧붙인다. 첫째, lost wakeup이 없는 이유 — `wait_event`는 태스크 상태를 먼저 INTERRUPTIBLE로 바꾸고 조건을 확인하므로, 그 사이의 `wake_up`은 상태를 RUNNING으로 되돌려 `schedule()`이 바로 돌아온다. 둘째, mutex인 이유 — `copy_to_user`가 잠들 수 있다. 셋째, `O_NONBLOCK`이면 잠들지 않고 `-EAGAIN`, poll은 `poll_wait`로 등록만 하고 현재 마스크를 보고한다. 실제로 QEMU에서 reader가 204 ms 잠들었다가 write 직후 깨어나는 것을 확인했다.

> "If the FIFO is empty I drop the mutex and sleep in wait_event_interruptible on the read wait queue. The writer pushes data and calls wake_up; the reader wakes, re-takes the lock and re-checks the condition in a loop, because another reader may have drained it. Then kfifo_to_user copies out — that can fault and sleep, which is why this is a mutex, not a spinlock. If a signal arrives, wait_event returns -ERESTARTSYS and the VFS either restarts the syscall or turns it into EINTR. With O_NONBLOCK I return EAGAIN instead of sleeping, and poll just registers on both queues and reports the current mask."

**꼬리 질문**: "Why a while loop and not an if?" → 헛깨움과 경쟁하는 reader. "Who actually sleeps in poll?" → 드라이버가 아니라 `do_poll`.

**Q.** "In your platform driver's probe, why is request_irq last, and what does devm change about teardown?"

**A.** (30초) IRQ를 요청하는 순간 ISR이 돌 수 있으므로, ISR이 만지는 drvdata·regmap·work를 전부 먼저 준비한다. devm은 자원을 획득 역순으로 자동 해제하므로 IRQ가 가장 먼저 풀리고, 그 다음 work 취소, iounmap, kfree 순이 된다. 그래서 `.remove`가 필요 없다.

(2분) 위험은 devm과 수동 해제를 섞을 때다. remove에서 `kfree`를 하면 remove가 끝난 뒤에야 devres가 IRQ를 풀기 때문에, 그 사이에 IRQ가 오면 해제된 메모리를 만진다. G3에서 실제로 재현했다. 또 하나는 `-EPROBE_DEFER` — 클럭이나 regulator 공급자가 아직 없으면 그대로 반환하고, `dev_err_probe`로 로그를 조용히 남긴다.

> "Once request_irq returns — or even before, if the line is already pending — the handler can run, so everything it touches must exist first: driver data, the regmap, the delayed work. With devm, resources are released in reverse order of acquisition, so on unbind the IRQ goes first, then the work is cancelled, then the mapping and the memory. That's why this driver has no remove callback. The classic bug is mixing devm IRQs with a manual kfree in remove: there's a window after remove where the IRQ is still live."

**꼬리 질문**: "What if the interrupt is level-triggered and clearing it needs an I2C write?" → threaded IRQ + `IRQF_ONESHOT`.

**Q.** "Explain every barrier in your lock-free ring buffer. Would volatile work?"

**A.** (30초) 생산자는 데이터를 쓴 다음 `smp_store_release`로 head를 공개하고, 소비자는 `smp_load_acquire`로 head를 읽은 다음 데이터를 읽는다 — 이 짝이 "데이터가 인덱스보다 먼저 보인다"를 보장한다. 반대 방향으로 tail에도 같은 짝이 있어 아직 안 읽은 슬롯을 덮어쓰지 않는다. volatile은 컴파일러만 막으므로 안 된다.

(2분) M2 코어에서 실제로 보여 줄 수 있다. 같은 코드를 relaxed로 바꾸면 objdump상 명령 순서는 똑같고 `ldar`/`stlr`이 `ldr`/`str`로 바뀔 뿐인데, 1천만 개 중 36만 개가 틀린 값이었다. 소비자의 데이터 load는 주소가 자기 tail에만 의존해서 head load보다 먼저 실행될 수 있기 때문이다. 인덱스는 free-running u32이고 크기는 2의 거듭제곱이라 `head - tail`이 wrap을 넘어도 정확하다. 더 빠르게 하려면 상대 인덱스를 캐시해 캐시 라인 왕복을 줄인다(같은 VM에서 23.7 → 57.8 M items/s).

> "It's the message-passing pattern twice. The producer writes the slot, then publishes head with a store-release; the consumer reads head with a load-acquire, then reads the slot — so if it sees the new index it must see the data. The same pair on tail stops the producer from overwriting a slot the consumer hasn't read yet. Volatile only constrains the compiler; on arm64 the CPU can still reorder. I actually measured it on an M2: the relaxed version had identical instruction order but about 3.6 percent corrupted items."

**꼬리 질문**: "What if the producer is an ISR on the same CPU?" → spinlock + 프로세스 쪽 irqsave, 또는 SPSC 그대로(배리어는 남겨 둔다). "kfifo?" → 1:1이면 락 없이 안전, `kfifo_reset` 주의.

**Q.** "Implement container_of. Why is it safe, and why does the kernel use intrusive lists?"

**A.** (30초) 멤버 주소에서 `offsetof(type, member)`를 빼면 구조체 시작 주소다. 커널 버전은 임시 변수(또는 `static_assert`)로 포인터 타입이 멤버 타입과 같은지 검사한다. intrusive 리스트는 링크가 객체 안에 있어 노드 할당이 따로 없고, 한 객체가 여러 리스트에 들어갈 수 있다.

(2분) 비용은 상수 빼기 한 번이다. 순회 중 삭제는 `list_for_each_entry_safe`로 다음 포인터를 미리 저장해야 하고, 커널 `list_del`은 포인터를 poison 값으로 채워 재사용 버그를 즉시 oops로 드러낸다. 드라이버에서는 `file->private_data`의 miscdevice, work_struct, kobject에서 내 구조체로 돌아오는 데 매일 쓴다.

> "container_of subtracts the member's offset from the member pointer. The kernel version also type-checks the pointer against the member, so passing the wrong field is a compile error. Lists are intrusive because the link lives in the object: no separate node allocation, better locality, and one object can sit on several lists. In drivers it's how you get from a work_struct or a miscdevice back to your private struct."

**Q.** "Two drivers update different fields of the same register. Is that safe?"

**A.** (30초) 아니다. 필드가 겹치지 않아도 read-modify-write 세 단계 사이에 상대가 쓴 값을 낡은 값으로 덮어쓴다. 실험에서 10만 번 중 3만5천 번이 사라졌다. 공유 spinlock, `regmap_update_bits`, 또는 하드웨어 SET/CLR 레지스터로 해결한다.

> "No. Even with disjoint fields, a read-modify-write is three steps, and the other writer's update can be overwritten with a stale copy. In a quick two-thread test I lost about a third of the updates. The fix is a shared lock around the RMW — regmap_update_bits gives you that — or, best, hardware set/clear alias registers so no RMW is needed."

**Q.** "Struct overlay or offset macros for registers?"

**A.** (30초) Linux에서는 오프셋 + `readl`/`writel` 또는 regmap. 접근 폭과 순서를 접근자가 정하고, 패딩 착오가 없다. 구조체를 쓰면 `static_assert(offsetof)`로 오프셋을 고정하고 그래도 접근은 `readl(&regs->x)`로 한다. packed는 레지스터 맵에 쓰지 않는다 — 비정렬 또는 바이트 단위 접근이 생성된다.

> "Offsets plus readl/writel, or regmap. The accessor fixes the access width and gives the ordering guarantees; a plain volatile struct field gives neither, and a compound assignment hides a non-atomic read-modify-write. If a team prefers a struct, I'd pin every offset with static_assert and still go through the accessors. Never packed: it can turn a 32-bit register read into four byte loads."

**Q.** "Debounce a noisy interrupt line in a driver."

**A.** (30초) ISR에서 `mod_delayed_work(wq, &w, msecs_to_jiffies(20))` — 엣지마다 마감을 뒤로 밀어서 마지막 엣지 후 20 ms 조용하면 work가 한 번 돈다. work는 프로세스 컨텍스트라 버스를 다시 읽어 안정 상태를 확인할 수 있다. 1 ms 미만 정밀도가 필요하면 hrtimer, GPIO 버튼이면 gpio-keys의 `debounce-interval`을 먼저 쓴다.

> "In the hard IRQ handler I just push a delayed work's deadline out with mod_delayed_work — it's safe from any context. The work runs once the line has been quiet for the debounce time, in process context, so it can re-read the status over I2C. On QEMU five bounces 2 ms apart produced exactly one event. For sub-millisecond precision I'd use an hrtimer; for a GPIO button I'd use gpio-keys' debounce-interval or hardware debounce instead of writing it."

---

## 13. 인터뷰 당일 — 말하면서 코딩하기

### 13.1 체크리스트 (코딩 중 소리 내어 말할 것)

- [ ] 문제를 내 말로 다시 말한다: "생산자 하나, 소비자 하나, 다른 CPU, 가득 차면 버린다 — 맞나요?"
- [ ] 먼저 질문 2~3개(§2.2, §3.2, §4.2 표에서 고른다). 가정은 칠판 구석에 적어 둔다
- [ ] 5분 안에 **뼈대**(구조체, 함수 시그니처, 등록)부터 쓰고 "이제 핵심 경로를 채우겠다"고 말한다
- [ ] 함수를 쓸 때마다 **컨텍스트를 선언**한다: "이건 hardirq라 잠들 수 없다", "이건 프로세스 컨텍스트라 mutex OK"
- [ ] 락을 잡을 때 **무엇을 보호하는지** 한 단어로 말한다("fifo 보호")
- [ ] 에러 경로를 빠뜨리지 않는다: `IS_ERR`/`PTR_ERR`, `dev_err_probe`, `-EFAULT`, `-ENOTTY`, `-EAGAIN`
- [ ] 반환값 규칙을 말한다: "read는 0이면 EOF, 짧은 값 가능", "smbus 읽기는 음수면 errno"
- [ ] 배리어를 쓰면 **짝**을 손가락으로 가리킨다(release ↔ acquire)
- [ ] 다 쓰고 나서 직접 테스트 케이스 3개를 돌려 본다(빈, 가득, wrap / 정상, probe 실패, unbind)
- [ ] 복잡도와 확장(인스턴스 N개, IRQ 생산자, RT 커널)을 1분 안에 말한다
- [ ] 모르는 API 이름은 지어내지 않는다: "정확한 이름은 기억이 안 나지만, 하는 일은 X다"
- [ ] 끝에 "실무에서는 이걸 직접 안 짜고 kfifo/regmap/IIO를 쓴다"를 한 줄 덧붙인다

### 13.2 45분 연습 계획 (한 세트)

| 시간 | 할 일 | 기준 |
|---|---|---|
| 0–3분 | 문제 읽고 질문 3개를 소리 내어 | 가정이 칠판에 적혀 있다 |
| 3–8분 | 뼈대(구조체 + 시그니처 + 등록) | 컴파일 가능한 모양 |
| 8–25분 | 핵심 경로(read/write 또는 probe 또는 push/pop) | 컨텍스트·락·에러를 말하며 |
| 25–30분 | 손으로 테스트(경계 3개) | 버그 하나는 스스로 찾는다 |
| 30–40분 | 꼬리 문제 2개(§2.9, §3.9, §4.9 표에서 무작위) | 각 2분 답 |
| 40–45분 | 이 노트의 풀이와 diff, 놓친 것 기록 | 다음 세트에 반영 |

추천 순환(하루 한 세트): ① 문제 1 → ② 문제 3 → ③ 문제 2 → ④ 문제 5+6 묶음 → ⑤ 문제 4 + 7(I2C) → ⑥ 모의: 문제 하나를 무작위로, 노트 없이. 은행의 `make prob N=05_circular_buffer`로 C 버전을 10분 안에 먼저 풀고 시작하면 좋다. 실제로 돌려 보려면 §1.2의 `run.sh <case>`.

---

## 14. 직접 해보기

1. **개념**: §2의 `r3_read`에서 `while`을 `if`로 바꾸면 어떤 상황에서 틀리나? reader 둘, 바이트 하나로 시나리오를 써 보라. 정답/힌트: 둘 다 깨어나고 하나가 먼저 비우면, 다른 하나는 빈 FIFO에서 `kfifo_to_user`로 0을 돌려준다 — 유저는 EOF로 오해한다.
2. **코드**: `r3_chr`를 platform 드라이버로 바꾸고 DT 인스턴스 2개를 만들어 `/dev/r3_chr0`, `/dev/r3_chr1`을 띄워라. 정답/힌트: `devm_kzalloc` 안에 miscdevice, 이름은 `devm_kasprintf`, G7 §4.
3. **코드**: `ring_user.c`의 consumer 쪽만 relaxed로 바꾸면(생산자는 release 유지) 오류가 나는가? 정답/힌트: 난다(acquire 쪽이 없으면 데이터 load가 앞당겨진다) — 실험으로 확인하라. 짝은 둘 다 있어야 한다.
4. **관찰**: `run.sh plat`에서 `fire` 간격을 `usleep_range(30000, 31000)`(30 ms)로 바꾸면 events는 몇이 되나? 정답/힌트: 디바운스 20 ms보다 간격이 길므로 엣지마다 이벤트 — 5.
5. **관찰**: BusyBox initramfs에는 `trace-cmd`가 없으니 tracefs로 `irq_handler_entry`와 `workqueue_execute_start`를 켜고 `fire`를 써서 ISR → kworker 지연을 재 보라. 정답/힌트: `mount -t tracefs none /sys/kernel/tracing`, `events/irq/irq_handler_entry/enable`, M5 §5.
6. **코드**: §6의 `update_field`를 `regmap_update_bits(map, CTRL, CTRL_ODR, FIELD_PREP(CTRL_ODR, 5))`로 옮기고 왜 락이 필요 없는지 설명하라. 정답/힌트: regmap 내부 lock(MMIO는 spinlock, I2C는 mutex).
7. **면접 모의**: §13.2 표대로 45분 타이머를 맞추고 문제 3을 백지에서. 끝나면 §4.8 실수 목록과 대조.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| misc device | major 10 문자 장치 | `misc_register` 한 번으로 cdev + /dev 노드 생성 |
| wait queue | 잠든 태스크 목록 | `wait_event`로 들어가고 `wake_up`으로 깨운다 |
| `-ERESTARTSYS` | 시그널로 중단된 syscall 표시 | 유저에게는 안 보이고 VFS가 재시작/EINTR 결정 |
| `__poll_t` / EPOLLIN | poll 마스크 타입·비트 | `.poll`이 현재 상태를 비트로 보고 |
| devm / devres | 장치 수명에 묶인 자원 | unbind·probe 실패 때 획득 역순 자동 해제 |
| `of_match_table` | DT compatible 매칭 표 | 마지막은 빈 항목, `MODULE_DEVICE_TABLE`로 modalias |
| regmap | 레지스터 접근 추상화 | MMIO/I2C/SPI 공통 API, 캐시, 원자적 `update_bits` |
| `mod_delayed_work` | 지연 작업의 마감 재설정 | IRQ에서 호출 가능, 디바운스의 표준 도구 |
| SPSC | single-producer single-consumer | 양쪽이 자기 인덱스만 쓰면 락 없이 가능 |
| acquire / release | 단방향 메모리 배리어 의미 | release 전 쓰기는 acquire 후 읽기에 보인다 |
| `ldar` / `stlr` | arm64 load-acquire / store-release 명령 | `smp_load_acquire`/`smp_store_release`가 되는 명령 |
| kfifo | 커널 표준 FIFO | 1:1이면 락 불필요, 크기 2의 거듭제곱 |
| `container_of` | 멤버 포인터 → 바깥 구조체 | `ptr - offsetof(type, member)` + 타입 검사 |
| intrusive list | 링크를 객체 안에 넣은 리스트 | `struct list_head`가 객체 멤버 |
| `GENMASK(h, l)` | 비트 h..l이 1인 마스크 | `unsigned long`. 32비트 전용은 `GENMASK_U32`(6.18 `include/linux/bits.h`에 있음) |
| `FIELD_PREP` / `FIELD_GET` | 마스크 기반 필드 쓰기/읽기 | 상수 값 넘침은 컴파일 에러 |
| RMW | read-modify-write | 세 단계라 원자적이지 않다 |
| `__iomem` | MMIO 주소 표시 | sparse만 아는 주석, 접근은 `readl`/`writel` |
| SMBus | I2C 위의 정형 트랜잭션 | `i2c_smbus_read_byte_data` 등, 음수면 errno |

---

## 16. 요약 & 체크리스트

커널 스타일 라이브 코딩은 결국 **세 질문**의 반복이다: 이 코드는 어떤 컨텍스트에서 도는가(잠들 수 있나), 무엇을 어떤 락으로 보호하는가, 자원은 언제 생기고 어떤 순서로 사라지는가. misc 드라이버는 wait queue와 `-ERESTARTSYS`, platform 드라이버는 probe 순서와 devm 역순, 링 버퍼는 acquire/release 짝, 비트·레지스터 문제는 RMW와 접근 폭을 시험한다. 이 노트의 모든 코드는 6.18.55 트리에 W=1 경고 0으로 빌드됐고 QEMU 게스트와 M2 코어에서 실제로 돌았다 — 특히 relaxed 링 버퍼가 3.6% 깨지는 것과 RMW가 3분의 1을 잃는 것은 면접에서 "직접 재 봤다"고 말할 수 있는 숫자다.

- [ ] misc char driver(read/write/poll/ioctl)를 15분 안에 백지에서 쓸 수 있다
- [ ] `wait_event_interruptible` 전에 락을 놓는 이유와 `while` 재확인 이유를 말할 수 있다
- [ ] `-ERESTARTSYS`, `-EAGAIN`, `-ENOTTY`, `-EFAULT`를 언제 돌려주는지 말할 수 있다
- [ ] platform driver probe의 8단계 순서와 "IRQ는 마지막"의 이유를 말할 수 있다
- [ ] devm 해제 순서와 devm+수동 kfree 혼용 버그를 설명할 수 있다
- [ ] SPSC 링의 배리어 4개를 각각 왜 필요한지 설명하고 코드로 짤 수 있다
- [ ] free-running u32 인덱스가 wrap을 넘어도 맞는 이유를 증명할 수 있다
- [ ] hardirq 생산자 + 프로세스 소비자에서 `spin_lock_irqsave`가 필요한 이유를 말할 수 있다
- [ ] kfifo가 락 없이 안전한 조건을 말할 수 있다
- [ ] `container_of`를 타입 검사 포함으로 구현할 수 있다
- [ ] `BIT()`이 `unsigned long`이라 생기는 함정과 `FIELD_PREP`의 컴파일 타임 검사를 말할 수 있다
- [ ] 레지스터 구조체 오버레이의 위험(패딩, packed, volatile RMW)과 `readl`/regmap을 쓰는 이유를 말할 수 있다

## 참고 자료

- Linux 6.18 소스(랩 `/var/lab/src/linux-6.18.55`): `include/linux/kfifo.h`(30행 locking 주석), `include/asm-generic/barrier.h`, `include/linux/bitfield.h`, `include/linux/devm-helpers.h`, `include/linux/hrtimer.h`, `drivers/char/misc.c`, `drivers/base/regmap/regmap-mmio.c`, `drivers/i2c/i2c-stub.c`, `kernel/irq/manage.c`(`irq_set_irqchip_state`)
- docs.kernel.org: "Driver Basics"·"Device drivers infrastructure"(driver-api), "Linux Kernel Memory Barriers"(`Documentation/memory-barriers.txt`), "Circular Buffers"(`Documentation/core-api/circular-buffers.rst`), "Devres - Managed Device Resource"(`Documentation/driver-api/driver-model/devres.rst`), "Implementing I2C device drivers"(`Documentation/i2c/writing-clients.rst`)
- Corbet, Rubini, Kroah-Hartman, "Linux Device Drivers, 3rd ed." 3장(char drivers), 6장(blocking I/O) — API 일부는 낡았다(LDD3는 2.6.10 기준)
- Bootlin "Linux kernel and driver development" 슬라이드(무료 PDF)
- elixir.bootlin.com — `container_of`, `list_head`, `kfifo` 정의를 버전별로 확인
- 이 저장소: E5(관용구), F3(sleep/wakeup), F4(락), F5(배리어), F7(deferred work), F8(타이머), G3(platform lifecycle), G4(char device), G6(poll), G7(misc), H1(MMIO), H2(regmap), H5(I2C) · 같이 쓰는 R 노트: R1(개념 Top 50), R2(무슨 일이 일어나나), R4(디버깅 시나리오)
- Don의 C 문제 은행: `job_interview_prep_research/anduril_firmware_engineer/`(01, 02, 04, 05, 06, 09, 10번 세트)
