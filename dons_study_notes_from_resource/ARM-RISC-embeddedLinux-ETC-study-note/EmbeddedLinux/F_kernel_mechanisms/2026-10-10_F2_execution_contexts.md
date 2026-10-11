# F2. 실행 컨텍스트 — process / hardirq / softirq / NMI, atomic 컨텍스트, "잠들어도 되나" 규칙

> **이 노트를 다 읽으면**: 드라이버 코드 한 줄을 보고 "이건 process·hardirq·softirq·NMI 중 어디서 도나, 지금 잠들어도 되나"를 근거와 함께 답할 수 있다 · `preempt_count` 값(예: `0x000101`, `0x010001`)을 비트 칸별로 풀어 읽고 `in_task()`·`in_hardirq()`·`in_serving_softirq()`·`in_atomic()`·`irqs_disabled()`가 각각 무엇을 보는지 설명할 수 있다 · "BUG: sleeping function called from invalid context"·"scheduling while atomic"·lockdep 경고를 읽고 원인 줄을 찾을 수 있다 · 컨텍스트별로 쓸 수 있는 API(mutex, `GFP_*`, spinlock 변형, `copy_to_user`, printk)를 표로 고를 수 있다
> **왜 필요한가**: "Why can't you sleep in an interrupt handler?"는 드라이버 면접 최빈출 질문이고, 실무 첫 크래시의 절반은 "atomic 컨텍스트에서 잠들 수 있는 함수 호출"이다. contents.md가 이 항목을 **가장 많이 틀리는 개념**으로 꼽은 이유다
> **Don 기준 난이도**: "ISR 안에서는 기다리지 않는다"는 펌웨어 감각은 이미 있다 / 새로 배울 부분은 ISR이 hardirq·softirq 두 층으로 나뉘고, 스핀락·`preempt_disable`·`local_irq_disable`·RCU 읽기 구간처럼 **ISR이 아닌데도 잠들면 안 되는 곳**이 많으며, 커널이 이 상태를 `preempt_count` 한 변수에 기록한다는 것
> **선행 노트**: E3(커널 모듈), E4(커널 프로그래밍 제약 — §5에서 "scheduling while atomic"을 처음 봤다). 이어지는 노트: F3(잠자기와 깨우기), F4(락), F6(인터럽트 처리), F7(지연 작업), F8(타이머), F9(GFP 플래그)

---

## 0. 큰 그림 — 이게 왜 필요한가

Don의 SSD 펌웨어를 떠올려 보자. 코드가 도는 장소는 두세 군데였다. **메인 루프(또는 RTOS 태스크)** 와 **ISR**, 그리고 Cortex-R이면 FIQ 정도. 규칙도 단순했다: ISR은 짧게, ISR 안에서 세마포어를 기다리지 않는다, 긴 일은 플래그를 세우고 태스크가 한다.

Linux 커널도 뼈대는 같다. 다만 장소가 더 잘게 나뉘어 있고, 각 장소에 이름이 있다.

- **process context**: 어떤 태스크(`task_struct`)를 대신해서 도는 코드. 유저가 부른 syscall(`read`, `ioctl`), 드라이버 `probe()`, 커널 스레드, workqueue 작업, threaded IRQ 핸들러. RTOS 태스크에 해당한다. **잠들 수 있다.**
- **hardirq context**: 하드웨어 인터럽트 핸들러(top half). Don이 알던 ISR 그 자체. **잠들 수 없다.**
- **softirq context**: hardirq가 끝나는 길목(`irq_exit`)이나 `ksoftirqd` 스레드에서 도는 "소프트웨어 인터럽트". timer 콜백, tasklet, 네트워크 수신 처리. 펌웨어로 치면 "ISR 끝에서 PendSV로 넘겨 처리하는 2단계 핸들러"다. **잠들 수 없다.**
- **NMI context**: 막을 수 없는 인터럽트. perf 샘플링·hard lockup 감지용. arm64에서는 GICv3 우선순위 마스킹으로 흉내 낸 pseudo-NMI. **거의 아무것도 할 수 없다.**

그리고 함정이 하나 더 있다. process context 안에서도 **스핀락을 잡거나, 선점을 끄거나, IRQ를 끄거나, RCU 읽기 구간에 들어가면** 그 구간은 잠들 수 없다. 이런 구간과 interrupt context를 합쳐 **atomic context**라고 부른다. 드라이버 버그 대부분은 이 "process인데 atomic인" 구간에서 나온다.

```svg
<svg viewBox="0 0 680 390" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">한 CPU 위의 시간축 — IRQ 하나가 컨텍스트 세 개를 지나간다</text>
<text x="12" y="56" text-anchor="start" font-size="13">process 컨텍스트</text>
<text x="12" y="72" text-anchor="start" font-size="12">(insmod)</text>
<line x1="170" y1="60" x2="670" y2="60" stroke="#888" stroke-dasharray="4 3"/>
<text x="12" y="116" text-anchor="start" font-size="13">hardirq</text>
<text x="12" y="132" text-anchor="start" font-size="12">(IRQ 스택, IRQ 꺼짐)</text>
<line x1="170" y1="120" x2="670" y2="120" stroke="#888" stroke-dasharray="4 3"/>
<text x="12" y="176" text-anchor="start" font-size="13">softirq</text>
<text x="12" y="192" text-anchor="start" font-size="12">(IRQ 켜짐)</text>
<line x1="170" y1="180" x2="670" y2="180" stroke="#888" stroke-dasharray="4 3"/>
<text x="12" y="236" text-anchor="start" font-size="13">irq 스레드</text>
<text x="12" y="252" text-anchor="start" font-size="12">(process 컨텍스트)</text>
<line x1="170" y1="240" x2="670" y2="240" stroke="#888" stroke-dasharray="4 3"/>
<rect x="170" y="45" width="100" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="220" y="65" text-anchor="middle" font-size="13">insmod</text>
<rect x="275" y="105" width="120" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="335" y="125" text-anchor="middle" font-size="13">f2_hard()</text>
<text x="335" y="152" text-anchor="middle" font-size="12">pc=0x010001</text>
<rect x="400" y="165" width="130" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="465" y="185" text-anchor="middle" font-size="12">TIMER · TASKLET</text>
<text x="465" y="212" text-anchor="middle" font-size="12">pc=0x000101</text>
<rect x="535" y="45" width="40" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="580" y="225" width="90" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="625" y="245" text-anchor="middle" font-size="12">f2_thread</text>
<text x="625" y="272" text-anchor="middle" font-size="12">pc=0x000000</text>
<line x1="270" y1="75" x2="278" y2="103" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="395" y1="135" x2="403" y2="163" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="530" y1="165" x2="538" y2="77" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="575" y1="75" x2="583" y2="223" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="262" y="98" text-anchor="middle" font-size="12">①</text>
<text x="388" y="158" text-anchor="middle" font-size="12">②</text>
<text x="548" y="112" text-anchor="middle" font-size="12">③</text>
<text x="20" y="305" text-anchor="start" font-size="12">① GIC가 IRQ 22를 올림 → el1_interrupt → irq_enter_rcu (HARDIRQ 칸 +1)</text>
<text x="20" y="327" text-anchor="start" font-size="12">② 핸들러가 tasklet 예약 · IRQ_WAKE_THREAD → irq_exit_rcu()가 softirq 실행</text>
<text x="20" y="349" text-anchor="start" font-size="12">③ softirq 끝 → 가로챘던 insmod로 복귀 → 스케줄러가 RT 스레드 irq/22-f2_ctx 실행</text>
<text x="20" y="371" text-anchor="start" font-size="12">current는 ①~③ 내내 insmod(가로채인 쪽)이고, 마지막 칸에서야 irq 스레드로 바뀐다</text>
</svg>
```

그림 1 — 이 노트의 실험 모듈이 실제로 만든 장면을 §6의 출력(`pc` 값)과 §8의 ftrace(순서·`insmod`)를 합쳐 그린 것. 같은 CPU에서 insmod가 돌다가 IRQ가 들어오면 hardirq → softirq → 원래 태스크 → irq 스레드 순으로 컨텍스트가 바뀐다. `pc`는 그 순간의 `preempt_count`다.

### 0.1 펌웨어 용어로 옮겨 보기

| Don이 아는 것 (bare-metal / RTOS) | Linux에서 대응하는 것 | 잠들기(블로킹) | 비고 |
|---|---|---|---|
| RTOS 태스크 | process context (유저 프로세스의 syscall, 커널 스레드) | 가능 | `current`가 바로 그 태스크 |
| ISR (NVIC 핸들러) | hardirq | 불가 | IRQ가 꺼진 채, CPU별 IRQ 스택에서 실행 |
| ISR 끝에서 PendSV로 미룬 처리 | softirq (timer·tasklet·NET_RX·RCU …) | 불가 | IRQ는 켜져 있다. hardirq가 끼어들 수 있다 |
| 우선순위 0 / NMI / FIQ | NMI, arm64 pseudo-NMI | 불가 (락도 거의 불가) | perf, watchdog |
| `__disable_irq()` 구간 | `local_irq_disable()` / `spin_lock_irqsave()` | 불가 | 펌웨어와 같다 |
| 스케줄러 잠금 (`vTaskSuspendAll`) | `preempt_disable()` / `spin_lock()` | 불가 | 펌웨어보다 훨씬 자주 쓴다 |
| "태스크에 알림 보내고 끝내기" | `wake_up`, `complete`, `queue_work`, `IRQ_WAKE_THREAD` | 어디서나 가능 | 잠드는 쪽이 아니라 깨우는 쪽 |

> 한 줄 요약: **"잠들 수 있나?"는 함수가 아니라 그 함수가 불린 문맥이 결정한다.** 같은 `kmalloc(GFP_KERNEL)`이 `probe()`에서는 맞고 타이머 콜백에서는 버그다.

---

## 1. 실습 준비 — 디버그 커널에서 모듈 하나 = QEMU 부팅 하나

### 1.1 예제 1 — 디버그 커널 설정 확인

무엇을 확인하나: 이 노트의 모든 실험은 공용 랩의 **디버그 커널**(`/var/lab/out/Image-dbg`, 6.18.55-dbg)에서 돌린다. 어떤 검사가 켜져 있는지 먼저 본다. VM 호스트 커널과 비교하려고 `uname -r`도 찍는다.

```sh
uname -r                                   # VM 호스트 (랩 모듈을 여기 넣지 않는다)
grep -E "^(# )?CONFIG_(PREEMPT|PREEMPT_RCU|DEBUG_ATOMIC_SLEEP|PROVE_LOCKING|ARM64_PSEUDO_NMI|IRQ_FORCED_THREADING|SOFTIRQ_ON_OWN_STACK|PREEMPT_RT|HZ)[= ]" \
    /var/lab/out/linux-build-dbg/.config
grep -E "^(# )?CONFIG_(DEBUG_ATOMIC_SLEEP|PROVE_LOCKING)[= ]" /var/lab/out/linux-build/.config
```

```text
7.0.0-38-generic
CONFIG_IRQ_FORCED_THREADING=y
CONFIG_PREEMPT=y
CONFIG_PREEMPT_RCU=y
CONFIG_HZ=250
# CONFIG_ARM64_PSEUDO_NMI is not set
CONFIG_SOFTIRQ_ON_OWN_STACK=y
CONFIG_PROVE_LOCKING=y
CONFIG_DEBUG_ATOMIC_SLEEP=y
# CONFIG_PROVE_LOCKING is not set
# CONFIG_DEBUG_ATOMIC_SLEEP is not set
```

출력에서 볼 것:

- 디버그 커널은 `PREEMPT=y`(완전 선점), `PREEMPT_RCU=y`, `DEBUG_ATOMIC_SLEEP=y`, `PROVE_LOCKING=y`(lockdep). 마지막 두 줄은 일반 랩 커널(`out/Image`)로, 두 검사가 **꺼져 있다**. E4 §5에서 봤듯 같은 버그가 일반 커널에서는 조용히 지나간다.
- `PREEMPT_RCU=y`라서 `rcu_read_lock()`이 `preempt_count`를 건드리지 않는다(§5에서 확인). `PREEMPT_RT` 줄은 아예 없다 — RT 커널이 아니다.
- `ARM64_PSEUDO_NMI`가 꺼져 있고, QEMU `virt`의 인터럽트 컨트롤러도 GICv2(`arm,cortex-a15-gic`)라 pseudo-NMI를 쓸 수 없다. 그래서 **이 노트의 NMI 관련 내용은 소스 읽기로만 확인했고 실행하지 않았다**(§2.4).

### 1.2 예제 2 — 게스트 실행기

무엇을 확인하나: `.ko`와 테스트 스크립트를 랩 initramfs 사본에 넣고, 커널 인자 `LAB_CMD=sh~/t.sh`로 스크립트를 한 번 돌린 뒤 꺼지는 실행기. E4의 실행기와 같고, 기본 커널만 `Image-dbg`, CPU는 2개(`-smp 2`)다.

```sh
#!/bin/bash
# usage: run_guest.sh <tag> <test.sh> [extra kernel args] ; env FILES, SMP, TMO, KIMG
set -e
tag=$1; t=$2; extra=$3
W=/var/tmp/F2/run_$tag
rm -rf $W && mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
for f in $FILES; do cp $f $W/r/; done
cp $t $W/r/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
KIMG=${KIMG:-/var/lab/out/Image-dbg}
timeout ${TMO:-150} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m 512M -nographic -no-reboot \
  -kernel $KIMG -initrd $W/i.gz -append "console=ttyAMA0 quiet $extra LAB_CMD=sh~/t.sh" 2>&1 | tr -d '\r'
```

모듈은 **디버그 커널의 빌드 트리**로 빌드한다. `vermagic`이 `6.18.55-dbg`라서 일반 랩 커널에는 로드되지 않는다(섞지 말 것).

```sh
mkdir -p /var/tmp/F2/f2_ctx && cd /var/tmp/F2/f2_ctx
echo "obj-m += f2_ctx.o" > Kbuild
make -C /var/lab/out/linux-build-dbg M=$PWD -j2 modules
modinfo f2_ctx.ko | grep -E "vermagic|parm"
```

```text
make: Entering directory '/var/lab/build/linux-dbg'
make[1]: Entering directory '/var/tmp/F2/f2_ctx'
  CC [M]  f2_ctx.o
  MODPOST Module.symvers
  CC [M]  f2_ctx.mod.o
  CC [M]  .module-common.o
  LD [M]  f2_ctx.ko
make[1]: Leaving directory '/var/tmp/F2/f2_ctx'
make: Leaving directory '/var/lab/build/linux-dbg'
vermagic:       6.18.55-dbg SMP preempt mod_unload aarch64
parm:           bug:int
parm:           nothread:bool
```

출력에서 볼 것: 경고 0개. 파라미터 두 개 — `bug`(0이면 관찰만, 1~4는 일부러 규칙을 어기는 모드, §10)와 `nothread`(IRQ를 스레드 없이 등록, §7).

---

## 2. 네 가지 실행 컨텍스트 — 정의

### 2.1 process context — "누군가를 대신해서" 도는 코드

**정의**: 커널 코드가 특정 태스크를 대신해 실행되는 상태. 그 태스크가 `current`(현재 CPU에서 도는 `task_struct`를 가리키는 매크로, arm64에서는 `sp_el0` 레지스터에 들어 있다)다.

세 가지 성질이 이 정의에서 나온다.

1. **`current`가 의미가 있다.** `current->pid`, `current->comm`, 시그널, 권한(`current_cred()`)이 "지금 이 일을 시킨 주체"다.
2. **잠들 수 있다.** 잠든다는 것은 `schedule()`을 불러 `current`를 대기 상태로 두고 CPU를 다른 태스크에 넘기는 것이다. 나중에 깨우면 같은 커널 스택에서 이어서 돈다. 재울 `task_struct`와 보관할 스택이 있으니 가능하다.
3. **유저 메모리에 접근할 수 있다(유저 프로세스라면).** `current->mm`이 그 프로세스의 주소 공간이므로 `copy_from_user()`가 그 공간에서 읽는다. 페이지가 없으면 page fault가 나고, 처리하려면 디스크에서 읽어야 할 수도 있으니 — 다시 2번, 잠들 수 있어야 한다.

**커널 스레드(kthread)** 도 process context다. `kthread_run()`으로 만든 스레드, workqueue를 처리하는 `kworker/N:M`, threaded IRQ의 `irq/NN-이름`, `ksoftirqd/N`이 다 커널 스레드다. 차이는 **유저 주소 공간이 없다**는 것(`current->mm == NULL`). 그래서 잠들 수는 있지만 `copy_to_user()`로 "누군가의 유저 메모리"에 쓸 수는 없다(§6에서 `mm=none`으로 확인).

### 2.2 hardirq context — top half

**정의**: CPU가 IRQ 예외를 받아 커널의 인터럽트 진입 코드(`el1_interrupt`/`el0_interrupt`)를 거쳐 `request_irq()`로 등록한 핸들러를 부르는 동안의 상태.

- **`current`는 "가로채인 태스크"** 다. hardirq는 아무 태스크나 끊고 들어온다. idle 태스크(`swapper/N`)일 수도, 아무 상관 없는 유저 프로세스일 수도 있다. 그러니 `current`를 보고 판단하는 코드는 hardirq에서 의미가 없다.
- **IRQ가 꺼진 채 돈다.** Linux는 hardirq 핸들러를 중첩시키지 않는다(같은 CPU에서 핸들러 실행 중 다른 IRQ가 끼어들지 않음). §6 출력의 `irqoff=1`이 그 증거다.
- **CPU별 IRQ 스택**에서 돈다(arm64 `IRQ_STACK_SIZE` = 16 KiB, E4 §4.2). 가로챈 태스크의 스택을 더 파먹지 않으려는 것이다.
- **잠들 수 없다.** 재울 "자기 태스크"가 없고, 잠들면 가로채인 무고한 태스크가 대신 잠들며, IRQ 스택은 CPU에 하나뿐이라 다른 곳으로 옮길 수도 없다. 자세한 이유는 §9.2.

### 2.3 softirq context — bottom half의 실행 엔진

**정의**: 커널이 정해 둔 10개의 "소프트웨어 인터럽트 벡터"(§7.3의 `/proc/softirqs` 출력의 행)의 핸들러가 도는 상태. hardirq 핸들러가 `raise_softirq()`로 "할 일 있음" 비트를 세워 두면, 나중에 커널이 그 비트를 보고 처리한다.

드라이버가 softirq 벡터를 직접 추가하지는 않는다. 드라이버가 만나는 softirq는 이런 모습이다.

| 드라이버가 쓰는 것 | 실제로 도는 softirq 벡터 | 비고 |
|---|---|---|
| `timer_list` 콜백 (`mod_timer`) | TIMER | F8 |
| `hrtimer`를 `HRTIMER_MODE_*_SOFT`로 시작 | HRTIMER | 기본 모드는 hardirq (§6) |
| tasklet (`tasklet_schedule`) | TASKLET (`tasklet_hi_schedule`은 HI) | 새 코드에서는 퇴출 중, F7 |
| BH workqueue (`system_bh_wq`, `WQ_BH`) | softirq 문맥 | tasklet의 후계 |
| NAPI 폴링 (네트워크 드라이버) | NET_RX / NET_TX | |
| RCU 콜백 (`call_rcu`) | RCU | F4 |

softirq는 **어디서 실행되나**가 헷갈리는 지점이다. 실행 장소가 둘이다.

1. **`irq_exit_rcu()`** — hardirq를 끝내고 나가는 길에, `in_interrupt()`가 거짓이고(즉 softirq나 BH 금지 구간을 가로챈 게 아니고) 대기 중인 softirq가 있으면 그 자리에서 `handle_softirqs()`를 돈다. 이때 `current`는 여전히 가로채인 태스크다.
2. **`ksoftirqd/N`** — CPU마다 하나 있는 커널 스레드. process context에서 `raise_softirq()`를 부르면 이 스레드를 깨운다. 또 `handle_softirqs()`가 2 ms(`MAX_SOFTIRQ_TIME`)나 10회 재시작(`MAX_SOFTIRQ_RESTART`)을 넘기면 나머지를 여기로 넘긴다(네트워크 폭주 때 유저 태스크가 굶지 않게).

어느 쪽에서 돌든 **softirq 핸들러 입장에서는 같은 softirq context**다. `ksoftirqd` 안에서 돌아도 `preempt_count`의 softirq 칸이 서 있으므로 잠들 수 없다(§6에서 `ksoftirqd/0`인데 `serv=1 atom=1`).

softirq는 hardirq와 달리 **IRQ를 켠 채** 돈다. 그래서 softirq 처리 중에 hardirq가 끼어들 수 있다(§8 ftrace에서 확인). 같은 CPU에서 softirq끼리는 중첩되지 않는다.

### 2.4 NMI와 arm64 pseudo-NMI (실행하지 않음 — 소스 확인만)

**NMI**(Non-Maskable Interrupt)는 IRQ를 꺼도 들어오는 인터럽트다. x86은 하드웨어 NMI 핀이 있고, perf 샘플링과 hard lockup 감지(IRQ가 꺼진 채 멈춘 CPU 찾기)에 쓴다.

arm64에는 오랫동안 아키텍처 NMI가 없었다. 그래서 커널은 **GICv3의 우선순위 마스크(`ICC_PMR_EL1`)** 로 흉내 낸다: `local_irq_disable()`이 PSTATE.I 비트 대신 PMR을 "보통 우선순위만 막는" 값으로 쓰고, 높은 우선순위 인터럽트만 통과시킨다. 이게 pseudo-NMI이고 `CONFIG_ARM64_PSEUDO_NMI=y` + 부트 인자 `irqchip.gicv3_pseudo_nmi=1`이 필요하다. 랩 소스 `arch/arm64/kernel/entry-common.c`:

```c
static void noinstr el1_interrupt(struct pt_regs *regs,
				  void (*handler)(struct pt_regs *))
{
	write_sysreg(DAIF_PROCCTX_NOIRQ, daif);

	if (IS_ENABLED(CONFIG_ARM64_PSEUDO_NMI) && regs_irqs_disabled(regs))
		__el1_pnmi(regs, handler);
	else
		__el1_irq(regs, handler);
}
```

"IRQ가 꺼진 상태에서 들어온 인터럽트"는 pseudo-NMI밖에 없으므로 그 경우 `irqentry_nmi_enter()` 경로를 탄다. `include/linux/hardirq.h`의 `__nmi_enter()`는 `__preempt_count_add(NMI_OFFSET + HARDIRQ_OFFSET)`, 즉 `preempt_count`에 `0x110000`을 더한다 — NMI 안에서는 `in_nmi()`와 `in_hardirq()`가 둘 다 참이다.

NMI 핸들러는 **어떤 락도 기다릴 수 없다**(락 주인이 바로 그 CPU의, NMI에 가로채인 코드일 수 있다). 그래서 lockless 자료구조, `irq_work`로 나중에 처리하기, printk의 지연 출력 같은 특수 수단만 쓴다. 드라이버 작성자가 NMI 핸들러를 쓸 일은 거의 없다. 면접에서는 "가장 제약이 심한 문맥, lockless만 가능"이라고 말할 수 있으면 된다.

### 2.5 한 장 요약

| 컨텍스트 | 진입 | `current`의 의미 | IRQ 상태 | 스택 | 잠들기 | 대표 예 |
|---|---|---|---|---|---|---|
| process (유저 태스크) | syscall, 예외 | 일을 시킨 프로세스 | 켜짐 | 태스크 커널 스택 16 KiB | 가능 | `read`/`ioctl` 핸들러, `probe` |
| process (kthread) | 스케줄러 | 그 커널 스레드 (`mm` 없음) | 켜짐 | 태스크 커널 스택 | 가능 | `kworker`, `irq/NN-…`, `ksoftirqd` |
| softirq | `irq_exit`, `local_bh_enable`, `ksoftirqd` | 가로채인 태스크 또는 `ksoftirqd` | 켜짐 | CPU별 IRQ 스택 (`SOFTIRQ_ON_OWN_STACK`) 또는 ksoftirqd 스택 | 불가 | timer 콜백, tasklet, NAPI |
| hardirq | IRQ 예외 | 가로채인 태스크 | 꺼짐 | CPU별 IRQ 스택 | 불가 | `request_irq` 핸들러, hrtimer(기본) |
| NMI | NMI / pseudo-NMI | 가로채인 태스크 | 꺼짐 | IRQ 스택 | 불가, 락도 불가 | perf, hard lockup 감지 |

---

## 3. `preempt_count` — 컨텍스트를 기록하는 32비트

### 3.1 직관

커널은 "지금 어느 컨텍스트인가"를 매번 계산하지 않는다. **카운터 하나에 칸을 나눠서 기록**한다. hardirq에 들어가면 hardirq 칸을 +1, softirq를 처리하기 시작하면 softirq 칸을 +1, 스핀락을 잡으면 preempt 칸을 +1. 나갈 때 -1. 펌웨어에서 "ISR 중첩 깊이" 변수와 "스케줄러 잠금 카운트" 변수를 하나의 워드에 비트필드로 합쳐 둔 것과 같다. 이 카운터가 `preempt_count`이고, **0이면 선점해도 되고(잠들어도 되고), 0이 아니면 안 된다**가 기본 규칙이다.

### 3.2 예제 3 — 랩 소스에서 비트 배치 확인

무엇을 확인하나: 칸의 위치와 폭을 추측하지 않고 랩 커널 소스에서 직접 읽는다.

```sh
sed -n 13,53p /var/lab/src/linux-6.18.55/include/linux/preempt.h
```

```c
/*
 * We put the hardirq and softirq counter into the preemption
 * counter. The bitmask has the following meaning:
 *
 * - bits 0-7 are the preemption count (max preemption depth: 256)
 * - bits 8-15 are the softirq count (max # of softirqs: 256)
 *
 * The hardirq count could in theory be the same as the number of
 * interrupts in the system, but we run all interrupt handlers with
 * interrupts disabled, so we cannot have nesting interrupts. Though
 * there are a few palaeontologic drivers which reenable interrupts in
 * the handler, so we need more than one bit here.
 *
 *         PREEMPT_MASK:	0x000000ff
 *         SOFTIRQ_MASK:	0x0000ff00
 *         HARDIRQ_MASK:	0x000f0000
 *             NMI_MASK:	0x00f00000
 * PREEMPT_NEED_RESCHED:	0x80000000
 */
#define PREEMPT_BITS	8
#define SOFTIRQ_BITS	8
#define HARDIRQ_BITS	4
#define NMI_BITS	4

#define PREEMPT_SHIFT	0
#define SOFTIRQ_SHIFT	(PREEMPT_SHIFT + PREEMPT_BITS)
#define HARDIRQ_SHIFT	(SOFTIRQ_SHIFT + SOFTIRQ_BITS)
#define NMI_SHIFT	(HARDIRQ_SHIFT + HARDIRQ_BITS)

#define __IRQ_MASK(x)	((1UL << (x))-1)

#define PREEMPT_MASK	(__IRQ_MASK(PREEMPT_BITS) << PREEMPT_SHIFT)
#define SOFTIRQ_MASK	(__IRQ_MASK(SOFTIRQ_BITS) << SOFTIRQ_SHIFT)
#define HARDIRQ_MASK	(__IRQ_MASK(HARDIRQ_BITS) << HARDIRQ_SHIFT)
#define NMI_MASK	(__IRQ_MASK(NMI_BITS)     << NMI_SHIFT)

#define PREEMPT_OFFSET	(1UL << PREEMPT_SHIFT)
#define SOFTIRQ_OFFSET	(1UL << SOFTIRQ_SHIFT)
#define HARDIRQ_OFFSET	(1UL << HARDIRQ_SHIFT)
#define NMI_OFFSET	(1UL << NMI_SHIFT)

#define SOFTIRQ_DISABLE_OFFSET	(2 * SOFTIRQ_OFFSET)
```

출력에서 볼 것:

- 칸 네 개: **PREEMPT 0~7, SOFTIRQ 8~15, HARDIRQ 16~19, NMI 20~23**. 마스크는 `0xff`, `0xff00`, `0xf0000`, `0xf00000`.
- softirq 칸은 **두 가지 의미를 함께 센다**. softirq를 **처리 중**이면 `SOFTIRQ_OFFSET`(0x100)을, `local_bh_disable()`로 **금지만** 했으면 `SOFTIRQ_DISABLE_OFFSET`(0x200)을 더한다. 그래서 bit 8(0x100)이 서 있으면 "실행 중", 짝수 배수만 있으면 "금지 중"으로 구분한다. 이 트릭 때문에 `in_serving_softirq()`와 `in_softirq()`가 다르다.
- 주석의 `PREEMPT_NEED_RESCHED: 0x80000000`은 x86 방식이다. **arm64는 다르다**: `arch/arm64/include/asm/preempt.h`에 `#define PREEMPT_NEED_RESCHED BIT(32)` — `thread_info`의 64비트 필드 위쪽 32비트를 need_resched 플래그로 쓰고, **반전 논리**(0이면 "resched 필요")라서 "count == 0이고 resched 필요"를 64비트 값 하나가 0인지로 한 번에 검사한다. 또 arm64의 `preempt_count()`는 **태스크별**(`current_thread_info()->preempt.count`)이다.

### 3.3 판정 매크로 — 무엇을 보나

같은 파일(랩 소스 `include/linux/preempt.h` 113~186행)에서 정의를 그대로 옮기면:

| 매크로 | 정의 (non-RT) | 참이 되는 때 | 드라이버에서 써도 되나 |
|---|---|---|---|
| `in_nmi()` | `preempt_count() & NMI_MASK` | NMI 처리 중 | 디버그용 |
| `in_hardirq()` | `preempt_count() & HARDIRQ_MASK` | hardirq 핸들러 안 (NMI 안도 포함) | 디버그용 |
| `in_serving_softirq()` | `softirq_count() & SOFTIRQ_OFFSET` | softirq 핸들러를 **실행 중** | 디버그용 |
| `in_task()` | `!(preempt_count() & (NMI_MASK or HARDIRQ_MASK or SOFTIRQ_OFFSET))` | process context (BH 금지 구간도 task로 침) | 가끔 (예: 통계를 어디에 셀지) |
| `in_softirq()` | `softirq_count()` | softirq 실행 중 **또는 BH 금지 중** | 폐기 예정 (주석: deprecated) |
| `in_interrupt()` | `irq_count()` = NMI+HARDIRQ+SOFTIRQ 칸 | 위 셋 중 하나라도, **BH 금지도 포함** | 폐기 예정 |
| `in_atomic()` | `preempt_count() != 0` | 칸 중 하나라도 0이 아님 | **쓰지 말 것** (주석: "Do not use in_atomic() in driver code") |
| `irqs_disabled()` | (preempt_count와 별개) 로컬 CPU의 IRQ 마스크 | `local_irq_disable`, `spin_lock_irqsave` 구간, hardirq | 단언(assert)용 |
| `preemptible()` | `preempt_count() == 0 && !irqs_disabled()` | 선점 가능 | 디버그용 |

(표 셀 안에는 `|`를 쓸 수 없어서 원문의 `NMI_MASK | HARDIRQ_MASK | SOFTIRQ_OFFSET`을 "or"로 적었다.)

`in_atomic()`을 드라이버에서 쓰지 말라는 이유가 두 가지 있다. 첫째, **IRQ만 끈 구간은 `preempt_count`가 0**이라 `in_atomic()`이 거짓을 돌려준다(§5에서 실측). 둘째, `CONFIG_PREEMPT_COUNT`가 꺼진 구성에서는 `spin_lock()`이 카운트를 올리지 않을 수 있다. "지금 잠들어도 되나"를 런타임에 묻는 코드를 짜지 말고, **호출 문맥을 설계로 정해서 API 문서에 적는 것**이 커널의 방식이다(예: "이 함수는 process context에서만 부를 것, might sleep").

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">preempt_count 비트 배치 (include/linux/preempt.h) — 실측값 분해</text>
<text x="10" y="48" text-anchor="start" font-size="12">비트</text>
<text x="10" y="78" text-anchor="start" font-size="12">preempt_count</text>
<text x="10" y="108" text-anchor="start" font-size="12">마스크</text>
<text x="210.0" y="48" text-anchor="middle" font-size="12">31–24</text>
<rect x="180" y="56" width="60" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="210.0" y="78" text-anchor="middle" font-size="12" font-weight="bold">미사용</text>
<text x="210.0" y="108" text-anchor="middle" font-size="12">—</text>
<text x="275.0" y="48" text-anchor="middle" font-size="12">23–20</text>
<rect x="240" y="56" width="70" height="34" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="275.0" y="78" text-anchor="middle" font-size="12" font-weight="bold">NMI</text>
<text x="275.0" y="108" text-anchor="middle" font-size="12">0xf00000</text>
<text x="350.0" y="48" text-anchor="middle" font-size="12">19–16</text>
<rect x="310" y="56" width="80" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="350.0" y="78" text-anchor="middle" font-size="12" font-weight="bold">HARDIRQ</text>
<text x="350.0" y="108" text-anchor="middle" font-size="12">0x0f0000</text>
<text x="460.0" y="48" text-anchor="middle" font-size="12">15–8</text>
<rect x="390" y="56" width="140" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="460.0" y="78" text-anchor="middle" font-size="12" font-weight="bold">SOFTIRQ</text>
<text x="460.0" y="108" text-anchor="middle" font-size="12">0x00ff00</text>
<text x="600.0" y="48" text-anchor="middle" font-size="12">7–0</text>
<rect x="530" y="56" width="140" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="600.0" y="78" text-anchor="middle" font-size="12" font-weight="bold">PREEMPT</text>
<text x="600.0" y="108" text-anchor="middle" font-size="12">0x0000ff</text>
<line x1="10" y1="120" x2="670" y2="120" stroke="currentColor"/>
<text x="10" y="145" text-anchor="start" font-size="12">0x000000 init (insmod)</text>
<text x="210.0" y="145" text-anchor="middle" font-size="13">0</text>
<text x="275.0" y="145" text-anchor="middle" font-size="13">0</text>
<text x="350.0" y="145" text-anchor="middle" font-size="13">0</text>
<text x="460.0" y="145" text-anchor="middle" font-size="13">0</text>
<text x="600.0" y="145" text-anchor="middle" font-size="13">0</text>
<text x="10" y="177" text-anchor="start" font-size="12">0x000002 spin_lock 2개</text>
<text x="210.0" y="177" text-anchor="middle" font-size="13">0</text>
<text x="275.0" y="177" text-anchor="middle" font-size="13">0</text>
<text x="350.0" y="177" text-anchor="middle" font-size="13">0</text>
<text x="460.0" y="177" text-anchor="middle" font-size="13">0</text>
<text x="600.0" y="177" text-anchor="middle" font-size="13" font-weight="bold">2</text>
<text x="10" y="209" text-anchor="start" font-size="12">0x000201 spin_lock_bh</text>
<text x="210.0" y="209" text-anchor="middle" font-size="13">0</text>
<text x="275.0" y="209" text-anchor="middle" font-size="13">0</text>
<text x="350.0" y="209" text-anchor="middle" font-size="13">0</text>
<text x="460.0" y="209" text-anchor="middle" font-size="13" font-weight="bold">2</text>
<text x="600.0" y="209" text-anchor="middle" font-size="13" font-weight="bold">1</text>
<text x="10" y="241" text-anchor="start" font-size="12">0x000101 timer_list</text>
<text x="210.0" y="241" text-anchor="middle" font-size="13">0</text>
<text x="275.0" y="241" text-anchor="middle" font-size="13">0</text>
<text x="350.0" y="241" text-anchor="middle" font-size="13">0</text>
<text x="460.0" y="241" text-anchor="middle" font-size="13" font-weight="bold">1</text>
<text x="600.0" y="241" text-anchor="middle" font-size="13" font-weight="bold">1</text>
<text x="10" y="273" text-anchor="start" font-size="12">0x010001 hardirq</text>
<text x="210.0" y="273" text-anchor="middle" font-size="13">0</text>
<text x="275.0" y="273" text-anchor="middle" font-size="13">0</text>
<text x="350.0" y="273" text-anchor="middle" font-size="13" font-weight="bold">1</text>
<text x="460.0" y="273" text-anchor="middle" font-size="13">0</text>
<text x="600.0" y="273" text-anchor="middle" font-size="13" font-weight="bold">1</text>
<text x="10" y="305" text-anchor="start" font-size="12">0x000200 강제 스레드 IRQ</text>
<text x="210.0" y="305" text-anchor="middle" font-size="13">0</text>
<text x="275.0" y="305" text-anchor="middle" font-size="13">0</text>
<text x="350.0" y="305" text-anchor="middle" font-size="13">0</text>
<text x="460.0" y="305" text-anchor="middle" font-size="13" font-weight="bold">2</text>
<text x="600.0" y="305" text-anchor="middle" font-size="13">0</text>
<text x="10" y="337" text-anchor="start" font-size="12">0x110000 NMI (미실행)</text>
<text x="210.0" y="337" text-anchor="middle" font-size="13">0</text>
<text x="275.0" y="337" text-anchor="middle" font-size="13" font-weight="bold">1</text>
<text x="350.0" y="337" text-anchor="middle" font-size="13" font-weight="bold">1</text>
<text x="460.0" y="337" text-anchor="middle" font-size="13">0</text>
<text x="600.0" y="337" text-anchor="middle" font-size="13">0</text>
<text x="10" y="369" text-anchor="start" font-size="12">0xffffffff bug3 언더플로</text>
<text x="210.0" y="369" text-anchor="middle" font-size="13" font-weight="bold">ff</text>
<text x="275.0" y="369" text-anchor="middle" font-size="13" font-weight="bold">f</text>
<text x="350.0" y="369" text-anchor="middle" font-size="13" font-weight="bold">f</text>
<text x="460.0" y="369" text-anchor="middle" font-size="13" font-weight="bold">ff</text>
<text x="600.0" y="369" text-anchor="middle" font-size="13" font-weight="bold">ff</text>
<line x1="180" y1="124" x2="180" y2="380" stroke="#888"/>
<line x1="240" y1="124" x2="240" y2="380" stroke="#888"/>
<line x1="310" y1="124" x2="310" y2="380" stroke="#888"/>
<line x1="390" y1="124" x2="390" y2="380" stroke="#888"/>
<line x1="530" y1="124" x2="530" y2="380" stroke="#888"/>
<line x1="670" y1="124" x2="670" y2="380" stroke="#888"/>
<text x="10" y="402" text-anchor="start" font-size="12">SOFTIRQ 칸: 1 = softirq 실행 중 (SOFTIRQ_OFFSET 0x100), 2 = BH 금지 (SOFTIRQ_DISABLE_OFFSET 0x200)</text>
<text x="10" y="422" text-anchor="start" font-size="12">arm64: 64비트 preempt 필드의 bit 32 = need_resched 플래그 (반전 논리: 0이면 resched 필요)</text>
</svg>
```

그림 2 — `preempt_count`의 칸과, 이 노트에서 실제로 찍힌 값을 칸별로 분해한 것. NMI 행만 계산값(실행 안 함)이다. 마지막 행은 §10.3에서 카운트가 언더플로되어 모든 칸이 1로 찬 실제 값이다.

---

## 4. 실험 모듈 `f2_ctx` — 모든 컨텍스트에서 같은 함수를 부른다

### 4.1 설계

아이디어는 단순하다. `f2_show("이름")` 함수 하나가 `preempt_count`와 판정 매크로 결과, `current`의 이름·PID, 유저 주소 공간 유무, CPU 번호를 한 줄로 찍는다. 이 함수를 (1) `module_init`(process context) 안에서 여러 가지 락·금지 상태로, (2) 모든 지연 실행 수단(timer, hrtimer 두 모드, tasklet, BH workqueue, workqueue, kthread)의 콜백에서, (3) 진짜 하드웨어 IRQ의 hardirq 핸들러와 irq 스레드에서 부른다.

IRQ는 D4에서 쓴 방법을 빌린다. GIC의 비어 있는 SPI 80번에 **DT 수정 없이** 모듈 안에서 매핑을 만들고(`irq_create_fwspec_mapping`), `irq_set_irqchip_state(..., IRQCHIP_STATE_PENDING, true)`로 GIC의 pending 비트를 직접 세워 인터럽트를 만든다. 실제 장치 IRQ와 똑같이 GIC → `gic_handle_irq` → 핸들러 경로를 탄다.

출력 필드: `pc`=`preempt_count`, `task`=`in_task()`, `hard`=`in_hardirq()`, `serv`=`in_serving_softirq()`, `bh`=`in_softirq()`, `intr`=`in_interrupt()`, `nmi`=`in_nmi()`, `atom`=`in_atomic()`, `irqoff`=`irqs_disabled()`, `rcu`=`rcu_preempt_depth()`, `mm`=`current->mm` 유무, `[comm/pid cpuN]`.

### 4.2 조각 1 — 헤더, 파라미터, 출력 함수

```c
// SPDX-License-Identifier: GPL-2.0
/* f2_ctx: 실행 컨텍스트마다 preempt_count와 판정 매크로를 찍는다 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/irqdomain.h>

/* 0: 조사만, 1: timer에서 mutex, 2: hardirq에서 GFP_KERNEL,
 * 3: spin_lock_irqsave 안에서 msleep, 4: rcu_read_lock 안에서 msleep */
static int bug;
module_param(bug, int, 0444);
static bool nothread;		/* 1이면 thread_fn 없이 request_irq() */
module_param(nothread, bool, 0444);

static void f2_show(const char *where)
{
	unsigned int pc = preempt_count();

	pr_info("%-13s pc=0x%06x task=%d hard=%d serv=%d bh=%d intr=%d nmi=%d atom=%d irqoff=%d rcu=%d mm=%s [%s/%d cpu%d]\n",
		where, pc, !!in_task(), !!in_hardirq(), !!in_serving_softirq(),
		!!in_softirq(), !!in_interrupt(), !!in_nmi(), !!in_atomic(),
		!!irqs_disabled(), rcu_preempt_depth(),
		current->mm ? "user" : "none", current->comm,
		current->pid, raw_smp_processor_id());
}

static DEFINE_SPINLOCK(f2_lock);
static DEFINE_SPINLOCK(f2_lock2);
static DEFINE_MUTEX(f2_mutex);
```

`raw_smp_processor_id()`를 쓴 이유: process context에서 선점이 켜진 채 `smp_processor_id()`를 부르면 `CONFIG_DEBUG_PREEMPT` 커널이 경고한다(값이 다음 순간 틀릴 수 있으므로). 여기서는 "찍는 순간의 CPU"만 알면 되니 raw 버전이 맞다.

### 4.3 조각 2 — process context 안의 여러 "atomic 만들기"

```c
static void f2_survey_process(void)
{
	unsigned long flags;

	f2_show("init");
	spin_lock(&f2_lock);
	f2_show("spin_lock");
	spin_lock(&f2_lock2);
	f2_show("spin_lock x2");
	spin_unlock(&f2_lock2);
	spin_unlock(&f2_lock);
	spin_lock_irqsave(&f2_lock, flags);
	f2_show("lock_irqsave");
	spin_unlock_irqrestore(&f2_lock, flags);
	spin_lock_bh(&f2_lock);
	f2_show("spin_lock_bh");
	spin_unlock_bh(&f2_lock);
	preempt_disable();
	f2_show("preempt_off");
	preempt_enable();
	local_irq_disable();
	f2_show("irq_disable");
	local_irq_enable();
	local_bh_disable();
	f2_show("bh_disable");
	local_bh_enable();
	rcu_read_lock();
	f2_show("rcu_read_lock");
	rcu_read_unlock();
	mutex_lock(&f2_mutex);
	f2_show("mutex_lock");
	mutex_unlock(&f2_mutex);
}
```

### 4.4 조각 3 — 지연 실행 수단들의 콜백

```c
/* ---- 지연 실행 수단들 ---- */
static DECLARE_COMPLETION(f2_done);
static struct timer_list f2_timer;
static struct hrtimer f2_hrt_hard, f2_hrt_soft;
static struct tasklet_struct f2_tl;
static struct work_struct f2_work, f2_bh_work;
static int f2_virq;

static void f2_timer_fn(struct timer_list *t)
{
	f2_show("timer_list");
	if (bug == 1) {
		mutex_lock(&f2_mutex);		/* 금지: 잠들 수 있는 락 */
		mutex_unlock(&f2_mutex);
	}
	complete(&f2_done);
}

static enum hrtimer_restart f2_hrt_fn(struct hrtimer *h)
{
	f2_show(h == &f2_hrt_hard ? "hrtimer" : "hrtimer_soft");
	complete(&f2_done);
	return HRTIMER_NORESTART;
}

static void f2_tl_fn(struct tasklet_struct *t)
{
	f2_show("tasklet");
	complete(&f2_done);
}

static void f2_work_fn(struct work_struct *w)
{
	f2_show(w == &f2_work ? "workqueue" : "bh_workqueue");
	if (w == &f2_work)
		msleep(1);			/* process context: OK */
	complete(&f2_done);
}

static int f2_kthread_fn(void *arg)
{
	f2_show("kthread");
	complete(&f2_done);
	return 0;
}
```

### 4.5 조각 4 — IRQ 핸들러 두 개, GIC 매핑, 정리 함수

```c
static irqreturn_t f2_hard(int irq, void *dev)
{
	f2_show("hardirq");
	if (bug == 2) {
		void *p = kmalloc(64, GFP_KERNEL);	/* 금지 */

		kfree(p);
	}
	if (nothread) {
		complete(&f2_done);
		return IRQ_HANDLED;
	}
	tasklet_schedule(&f2_tl);		/* hardirq에서 softirq 예약 */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t f2_thread(int irq, void *dev)
{
	f2_show("irq_thread");
	msleep(1);				/* irq thread는 잠들 수 있다 */
	complete(&f2_done);
	return IRQ_HANDLED;
}

static int f2_map_irq(void)
{
	struct irq_fwspec fs = {
		.param_count = 3,
		.param = { 0 /* GIC_SPI */, 80, IRQ_TYPE_EDGE_RISING },
	};
	struct device_node *gic;
	int virq;

	gic = of_find_compatible_node(NULL, NULL, "arm,cortex-a15-gic");
	if (!gic)
		return -ENODEV;
	fs.fwnode = of_fwnode_handle(gic);
	virq = irq_create_fwspec_mapping(&fs);
	of_node_put(gic);
	return virq ? virq : -EINVAL;
}
```

```c
static void __exit f2_exit(void)
{
	free_irq(f2_virq, NULL);
	irq_dispose_mapping(f2_virq);
	tasklet_kill(&f2_tl);
	timer_delete_sync(&f2_timer);
	hrtimer_cancel(&f2_hrt_hard);
	hrtimer_cancel(&f2_hrt_soft);
	cancel_work_sync(&f2_work);
	cancel_work_sync(&f2_bh_work);
}
module_init(f2_init);
module_exit(f2_exit);
MODULE_DESCRIPTION("F2: execution contexts and preempt_count");
MODULE_LICENSE("GPL");
```

`f2_hard`가 `IRQ_WAKE_THREAD`를 돌려주면 커널이 이 IRQ의 스레드(`irq/22-f2_ctx`)를 깨워 `f2_thread`를 부른다. `IRQF_ONESHOT`은 "스레드 핸들러가 끝날 때까지 이 IRQ 줄을 마스크해 둬라"라는 뜻으로, 레벨 트리거 장치에서 핸들러가 원인을 지우기 전에 IRQ가 다시 들어오는 폭주를 막는다(F6). 데모라서 init 실패 경로의 정리(타이머·매핑 해제)는 생략했다.

### 4.6 조각 5 — init: 차례로 실행하고 기다리기

```c
#define F2_WAIT(n) do { int i; for (i = 0; i < (n); i++) \
	if (!wait_for_completion_timeout(&f2_done, HZ)) pr_err("timeout\n"); } while (0)

static int __init f2_init(void)
{
	unsigned long flags;
	int ret;

	f2_survey_process();
	timer_setup(&f2_timer, f2_timer_fn, 0);
	hrtimer_setup(&f2_hrt_hard, f2_hrt_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_setup(&f2_hrt_soft, f2_hrt_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
	tasklet_setup(&f2_tl, f2_tl_fn);
	INIT_WORK(&f2_work, f2_work_fn);
	INIT_WORK(&f2_bh_work, f2_work_fn);

	if (bug == 3) {
		spin_lock_irqsave(&f2_lock, flags);
		msleep(5);				/* 금지 */
		spin_unlock_irqrestore(&f2_lock, flags);
	}
	if (bug == 4) {
		rcu_read_lock();
		msleep(5);				/* 금지 */
		rcu_read_unlock();
	}

	mod_timer(&f2_timer, jiffies + 1);			F2_WAIT(1);
	hrtimer_start(&f2_hrt_hard, ms_to_ktime(1), HRTIMER_MODE_REL);	F2_WAIT(1);
	hrtimer_start(&f2_hrt_soft, ms_to_ktime(1), HRTIMER_MODE_REL_SOFT); F2_WAIT(1);
	tasklet_schedule(&f2_tl);				F2_WAIT(1);
	queue_work(system_bh_wq, &f2_bh_work);			F2_WAIT(1);
	schedule_work(&f2_work);				F2_WAIT(1);
	kthread_run(f2_kthread_fn, NULL, "f2_kthread");		F2_WAIT(1);

	f2_virq = f2_map_irq();
	if (f2_virq < 0)
		return f2_virq;
	if (nothread)
		ret = request_irq(f2_virq, f2_hard, 0, "f2_ctx", NULL);
	else
		ret = request_threaded_irq(f2_virq, f2_hard, f2_thread,
					   IRQF_ONESHOT, "f2_ctx", NULL);
	if (ret)
		return ret;
	pr_info("virq=%d (GIC SPI 80) -> set PENDING\n", f2_virq);
	irq_set_irqchip_state(f2_virq, IRQCHIP_STATE_PENDING, true);
	F2_WAIT(nothread ? 1 : 2);		/* (tasklet +) irq_thread */
	return 0;
}
```

`F2_WAIT`는 각 콜백이 `complete()`를 부를 때까지 기다린다. **기다리는 쪽(init, process)은 잠들고, 깨우는 쪽(timer·hardirq 등, atomic)은 `complete()`만 부른다** — 이 노트 전체의 패턴이 코드 한 줄에 들어 있다. `complete()`는 잠들지 않으므로 어느 문맥에서나 부를 수 있다.

---

## 5. 예제 4 — 무엇이 process context를 atomic으로 만드나

무엇을 확인하나: 같은 `insmod`(process context) 안에서 락·금지 API 하나씩만 바꿨을 때 `preempt_count`와 판정 매크로가 어떻게 바뀌는지.

```sh
# 게스트 안 (t.sh)
uname -r
insmod /f2_ctx.ko
dmesg | grep "f2_ctx:"
grep f2_ctx /proc/interrupts
ps | grep -E "irq/.*f2|ksoftirqd|f2_k" | grep -v grep
rmmod f2_ctx
```

```text
lab initramfs: 6.18.55-dbg up 11.95s
6.18.55-dbg
[   13.344735] f2_ctx: loading out-of-tree module taints kernel.
[   13.361681] f2_ctx: init          pc=0x000000 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=0 irqoff=0 rcu=0 mm=user [insmod/75 cpu0]
[   13.362167] f2_ctx: spin_lock     pc=0x000001 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=1 irqoff=0 rcu=0 mm=user [insmod/75 cpu0]
[   13.362308] f2_ctx: spin_lock x2  pc=0x000002 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=1 irqoff=0 rcu=0 mm=user [insmod/75 cpu0]
[   13.362383] f2_ctx: lock_irqsave  pc=0x000001 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=1 irqoff=1 rcu=0 mm=user [insmod/75 cpu0]
[   13.362439] f2_ctx: spin_lock_bh  pc=0x000201 task=1 hard=0 serv=0 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=user [insmod/75 cpu0]
[   13.362496] f2_ctx: preempt_off   pc=0x000001 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=1 irqoff=0 rcu=0 mm=user [insmod/75 cpu0]
[   13.362556] f2_ctx: irq_disable   pc=0x000000 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=0 irqoff=1 rcu=0 mm=user [insmod/75 cpu0]
[   13.362604] f2_ctx: bh_disable    pc=0x000200 task=1 hard=0 serv=0 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=user [insmod/75 cpu0]
[   13.362706] f2_ctx: rcu_read_lock pc=0x000000 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=0 irqoff=0 rcu=1 mm=user [insmod/75 cpu0]
[   13.362881] f2_ctx: mutex_lock    pc=0x000000 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=0 irqoff=0 rcu=0 mm=user [insmod/75 cpu0]
```

(전체 출력의 앞부분. 나머지는 §6에서 본다. 타임스탬프·PID는 실행마다 다르다.)

출력에서 볼 것 — 한 줄씩:

| 상태 | `pc` | 무엇이 올렸나 | `atom` | `irqoff` | 잠들어도 되나 |
|---|---|---|---|---|---|
| `init` | `0x000000` | 없음 | 0 | 0 | **된다** |
| `spin_lock` | `0x000001` | 스핀락이 선점을 끈다 (+1) | 1 | 0 | 안 된다 |
| `spin_lock x2` | `0x000002` | 락 두 개 = 2. 중첩 깊이를 센다 | 1 | 0 | 안 된다 |
| `lock_irqsave` | `0x000001` | 선점 끔 +1, **IRQ 꺼짐**은 `pc`가 아니라 `irqoff`에 | 1 | 1 | 안 된다 |
| `spin_lock_bh` | `0x000201` | BH 금지(0x200) + 선점 끔(1) | 1 | 0 | 안 된다 |
| `preempt_off` | `0x000001` | `preempt_disable()` | 1 | 0 | 안 된다 |
| `irq_disable` | `0x000000` | **`pc`는 그대로 0** | **0** | 1 | 안 된다 |
| `bh_disable` | `0x000200` | `local_bh_disable()` | 1 | 0 | 안 된다 |
| `rcu_read_lock` | `0x000000` | **`pc`는 0**, `rcu=1` | **0** | 0 | 안 된다 |
| `mutex_lock` | `0x000000` | mutex는 `pc`를 건드리지 않는다 | 0 | 0 | **된다** |

여기서 꼭 가져갈 세 가지:

1. **`in_atomic()`은 두 군데를 놓친다.** `local_irq_disable()` 구간(`irq_disable` 줄)과 `PREEMPT_RCU` 커널의 `rcu_read_lock()` 구간(`rcu_read_lock` 줄)에서 `atom=0`이다. 하지만 둘 다 잠들면 안 된다. IRQ를 끈 채 잠들면 그 CPU는 타이머 인터럽트를 못 받고, RCU 읽기 구간에서 잠들면 grace period가 끝나지 않는다. `might_sleep()`의 검사(§9.3)는 그래서 `preempt_count`만이 아니라 `irqs_disabled()`와 RCU 깊이도 따로 본다.
2. **`PREEMPT_RCU`가 아닌 커널(예: `PREEMPT_NONE`/`VOLUNTARY` 서버 커널)에서는** `rcu_read_lock()`이 그냥 `preempt_disable()`이다. 랩 소스 `include/linux/rcupdate.h` 91~94행: `static inline void __rcu_read_lock(void) { preempt_disable(); }`. 같은 코드라도 커널 구성에 따라 `pc`가 0 또는 1이 된다 — "pc만 보고 판단하지 말라"의 또 다른 이유다.
3. **`mutex_lock`을 잡은 상태는 atomic이 아니다.** mutex를 잡은 채 잠드는 것(예: mutex 안에서 `msleep`, I2C 전송)은 합법이다. mutex는 "잠드는 락"이다. 반대로 spinlock을 잡은 채 잠드는 것은 금지다. 이 대비가 F4의 "spinlock vs mutex" 질문의 핵심이다.

`bh` 열도 보자. `spin_lock_bh`와 `bh_disable` 줄에서 `bh=1`, `intr=1`인데 `serv=0`, `task=1`이다. **softirq를 금지했을 뿐 softirq 안에 있는 것은 아니므로** `in_task()`는 참이고 `in_interrupt()`는 참이다. `in_interrupt()`라는 이름이 오해를 부르는 대표 사례이고, 그래서 폐기 예정이다.

> 함정: `in_atomic()`이나 `in_interrupt()`로 "atomic이면 `GFP_ATOMIC`, 아니면 `GFP_KERNEL`"을 고르는 코드는 위의 두 구멍 때문에 틀린다. 호출자가 문맥을 알고 있으니 **호출자가 gfp 플래그를 인자로 넘겨 주는 것**이 커널 API의 관례다(`kmalloc(size, gfp)`, `skb_copy(skb, gfp)` 등).

---

## 6. 예제 5 — 컨텍스트별 실측 표

무엇을 확인하나: 같은 `f2_show()`를 timer·hrtimer·tasklet·BH workqueue·workqueue·kthread·hardirq·irq 스레드에서 불렀을 때의 값. 같은 실행(§5)의 뒷부분이다.

```text
[   13.366270] f2_ctx: timer_list    pc=0x000101 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [swapper/0/0 cpu0]
[   13.368181] f2_ctx: hrtimer       pc=0x010001 task=0 hard=1 serv=0 bh=0 intr=1 nmi=0 atom=1 irqoff=1 rcu=0 mm=none [swapper/0/0 cpu0]
[   13.370631] f2_ctx: hrtimer_soft  pc=0x000101 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [swapper/0/0 cpu0]
[   13.371087] f2_ctx: tasklet       pc=0x000100 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [ksoftirqd/0/14 cpu0]
[   13.372445] f2_ctx: bh_workqueue  pc=0x000100 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [ksoftirqd/1/22 cpu1]
[   13.373033] f2_ctx: workqueue     pc=0x000000 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=0 irqoff=0 rcu=0 mm=none [kworker/1:2/57 cpu1]
[   13.385541] f2_ctx: kthread       pc=0x000000 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=0 irqoff=0 rcu=0 mm=none [f2_kthread/76 cpu1]
[   13.393278] f2_ctx: virq=22 (GIC SPI 80) -> set PENDING
[   13.393934] f2_ctx: hardirq       pc=0x010001 task=0 hard=1 serv=0 bh=0 intr=1 nmi=0 atom=1 irqoff=1 rcu=0 mm=none [swapper/0/0 cpu0]
[   13.395044] f2_ctx: tasklet       pc=0x000101 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [swapper/0/0 cpu0]
[   13.395441] f2_ctx: irq_thread    pc=0x000000 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=0 irqoff=0 rcu=0 mm=none [irq/22-f2_ctx/77 cpu0]
 22:          1          0 GIC-0 112 Edge      f2_ctx
   14 0         0:00 [ksoftirqd/0]
   22 0         0:00 [ksoftirqd/1]
   77 0         0:00 [irq/22-f2_ctx]
[   14.336497] reboot: Power down
```

출력에서 볼 것 — 정리하면:

| 콜백 | `pc` | 판정 | `current` | `mm` | 해석 |
|---|---|---|---|---|---|
| `timer_list` | `0x000101` | softirq 실행 중 | `swapper/0` (idle) | none | TIMER softirq가 idle을 가로챘다. 하위 1은 idle 태스크 자신의 선점 금지 |
| `hrtimer` (`HRTIMER_MODE_REL`) | `0x010001` | **hardirq** | `swapper/0` | none | 기본 hrtimer는 타이머 IRQ 안에서 바로 돈다. `irqoff=1` |
| `hrtimer_soft` (`..._REL_SOFT`) | `0x000101` | softirq 실행 중 | `swapper/0` | none | HRTIMER softirq로 미뤄서 돈다 |
| `tasklet` (process에서 예약) | `0x000100` | softirq 실행 중 | `ksoftirqd/0` | none | process에서 raise → ksoftirqd가 처리. 가로챈 태스크가 없으니 하위 0 |
| `bh_workqueue` | `0x000100` | softirq 실행 중 | `ksoftirqd/1` | none | `system_bh_wq` 작업도 softirq 문맥 |
| `workqueue` | `0x000000` | process | `kworker/1:2` | none | 잠들 수 있다 (`msleep(1)` 성공) |
| `kthread` | `0x000000` | process | `f2_kthread` | none | 잠들 수 있다 |
| `hardirq` (`f2_hard`) | `0x010001` | hardirq | `swapper/0` | none | IRQ 22, `irqoff=1` |
| `tasklet` (hardirq에서 예약) | `0x000101` | softirq 실행 중 | `swapper/0` | none | `irq_exit`에서 바로 실행됐다 |
| `irq_thread` (`f2_thread`) | `0x000000` | process | `irq/22-f2_ctx` | none | 잠들 수 있다 (`msleep(1)` 성공) |

해석 포인트 다섯 가지.

**(1) `pc`의 위쪽 칸은 "지금 내가 어디 있나", 아래쪽 칸은 "가로채인 쪽이 무엇을 쥐고 있었나"다.** hardirq의 `0x010001`에서 HARDIRQ 칸 1은 우리 핸들러, PREEMPT 칸 1은 가로채인 idle 태스크의 몫이다(idle 태스크는 선점 금지 상태로 돈다 — `init_idle_preempt_count()`가 `PREEMPT_DISABLED`로 설정). 이 노트를 쓰는 중 다른 실행에서는 hardirq가 `insmod`의 `spin_unlock_irqrestore()` 직후를 가로채 `0x010001 … mm=user [insmod/74 cpu0]`이 찍혔다 — `spin_unlock_irqrestore`는 IRQ를 먼저 복원하고 선점 카운트를 나중에 내리기 때문에 그 틈에 들어온 것이다. 그래서 IRQ 안에서 본 `pc`의 하위 칸 값은 실행마다 0일 수도 1일 수도 있다.

**(2) `current`는 hardirq·softirq에서 "희생자"다.** 같은 hardirq 핸들러가 이번 실행에서는 `swapper/0`, 다른 실행(§10.4의 출력)에서는 `insmod/73`으로 찍혔다. `mm` 열도 그에 따라 none/user로 바뀐다. 즉 hardirq 안에서 `current->mm`이 있다고 `copy_to_user()`를 하면, **우연히 가로챈 아무 프로세스의 메모리에** 쓰게 된다. 그래서 "interrupt context에서는 유저 메모리 접근 불가"가 규칙이다.

**(3) 같은 tasklet이 두 곳에서 돌았다.** process(insmod)에서 `tasklet_schedule()` → `raise_softirq`가 "지금 interrupt 안이 아니니" `ksoftirqd/0`을 깨웠고, 거기서 돌았다(`pc=0x000100`). hardirq에서 예약한 tasklet은 그 hardirq의 `irq_exit_rcu()`에서 곧바로 돌았다(`pc=0x000101`, `current`=가로채인 idle). 어느 쪽이 될지는 타이밍에 달려 있다 — 이 노트를 쓰는 중 다른 실행에서는 process에서 예약한 tasklet이 `ksoftirqd`가 아니라 `insmod` 위에서(`pc=0x000100 … [insmod/73 cpu1]`) 돌았다. ksoftirqd가 스케줄되기 전에 같은 CPU의 다른 출구(IRQ 복귀 등)가 먼저 처리한 것이다. **드라이버는 softirq가 어느 태스크 위에서 돌지 가정하면 안 된다.**

**(4) hrtimer는 기본이 hardirq다.** 과제 설명처럼 "6.x hrtimer는 softirq에서 돌 수도 있다"는 말은 반만 맞다. 랩 소스 `kernel/time/hrtimer.c`의 `__hrtimer_setup()`:

```c
	bool softtimer = !!(mode & HRTIMER_MODE_SOFT);
	...
	if (IS_ENABLED(CONFIG_PREEMPT_RT) && !(mode & HRTIMER_MODE_HARD))
		softtimer = true;
```

non-RT 커널에서는 `HRTIMER_MODE_*_SOFT`를 줄 때만 softirq, 아니면 hardirq다. **PREEMPT_RT 커널에서는 반대로 `HRTIMER_MODE_*_HARD`를 명시하지 않으면 전부 softirq로 옮겨진다.** 그래서 "hrtimer 콜백에서 무엇을 해도 되나"는 RT 여부에 따라 달라지고, 안전한 답은 "hardirq 규칙을 따른다"다.

**(5) `ps`의 마지막 세 줄**: `ksoftirqd/0`, `ksoftirqd/1`(CPU마다 하나)과 `irq/22-f2_ctx`(우리 IRQ의 스레드). `/proc/interrupts`의 `GIC-0 112 Edge`는 hwirq 112 = SPI 80 + 32(D4의 "+32 함정")다.

---

## 7. threaded IRQ와 강제 스레드화 — 같은 핸들러, 다른 문맥

### 7.1 threaded IRQ는 process context다

§6의 `irq_thread` 줄이 보여 주듯 `request_threaded_irq()`의 `thread_fn`은 커널 스레드 `irq/NN-이름` 안에서 돈다. `pc=0`, `task=1`, `irqoff=0` — 완전한 process context이고, 잠들어도 된다. 그래서 **I2C/SPI 센서처럼 "레지스터를 읽으려면 버스 전송(잠듦)이 필요한" 장치는 threaded IRQ가 정답**이다. hardirq 쪽(primary handler)은 "내 IRQ가 맞나" 확인과 `IRQ_WAKE_THREAD` 반환만 한다. 많은 I2C 드라이버는 primary handler를 `NULL`로 두고 `IRQF_ONESHOT`만 준다.

irq 스레드는 실시간 스케줄링 클래스(SCHED_FIFO)로 만들어져서 보통 유저 태스크보다 먼저 돈다. 이 노트를 쓰는 중 돌린 ftrace(이전 실행, depth 9)에서 `__irq_wake_thread()` 아래에 `enqueue_task_rt()`가 찍힌 것으로 RT 큐에 들어가는 것을 확인했다(그 출력은 길어서 싣지 않았다).

### 7.2 예제 6 — `threadirqs` 부트 인자: hardirq 핸들러가 스레드로 쫓겨난다

무엇을 확인하나: `nothread=1`로 IRQ를 **스레드 없이** `request_irq()`만 해서 등록한 뒤, 일반 부팅과 `threadirqs` 부팅에서 같은 핸들러의 문맥을 비교한다. `CONFIG_IRQ_FORCED_THREADING=y` 커널은 `threadirqs`를 주면 대부분의 hardirq 핸들러를 강제로 스레드로 옮긴다(PREEMPT_RT 커널은 기본이 이 동작이다).

```sh
# 게스트 안 (t.sh) — 커널 인자만 바꿔 두 번 부팅
echo "cmdline: $(grep -o threadirqs /proc/cmdline)"
insmod /f2_ctx.ko nothread=1
dmesg | grep "f2_ctx: \(hardirq\|virq\)"
grep f2_ctx /proc/interrupts
ps | grep "irq/22" | grep -v grep
```

```text
lab initramfs: 6.18.55-dbg up 7.24s
cmdline: 
[    7.829084] f2_ctx: virq=22 (GIC SPI 80) -> set PENDING
[    7.829614] f2_ctx: hardirq       pc=0x010001 task=0 hard=1 serv=0 bh=0 intr=1 nmi=0 atom=1 irqoff=1 rcu=0 mm=none [swapper/0/0 cpu0]
 22:          1          0 GIC-0 112 Edge      f2_ctx
[    8.121339] reboot: Power down
```

```text
lab initramfs: 6.18.55-dbg up 7.65s
cmdline: threadirqs
[    8.718514] f2_ctx: virq=22 (GIC SPI 80) -> set PENDING
[    8.720252] f2_ctx: hardirq       pc=0x000200 task=1 hard=0 serv=0 bh=1 intr=1 nmi=0 atom=1 irqoff=1 rcu=0 mm=none [irq/22-f2_ctx/83 cpu0]
 22:          1          0 GIC-0 112 Edge      f2_ctx
   83 0         0:00 [irq/22-f2_ctx]
[    9.914462] reboot: Power down
```

출력에서 볼 것:

- 일반 부팅: `f2_hard`가 hardirq(`pc=0x010001`, `hard=1`), `irq/22` 스레드는 없다.
- `threadirqs` 부팅: **같은 `f2_hard`가** `irq/22-f2_ctx` 스레드 안에서 돌았다. `task=1`, `hard=0` — 이제 hardirq가 아니다. 그런데 `pc=0x000200`, `bh=1`, `irqoff=1`, `atom=1`이다. 강제 스레드화된 핸들러는 랩 소스 `kernel/irq/manage.c`의 `irq_forced_thread_fn()`에서 이렇게 불린다:

```c
static irqreturn_t irq_forced_thread_fn(struct irq_desc *desc, struct irqaction *action)
{
	irqreturn_t ret;

	local_bh_disable();
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_irq_disable();
	ret = irq_thread_fn(desc, action);
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_irq_enable();
	local_bh_enable();
	return ret;
}
```

  즉 hardirq용으로 쓴 코드가 스레드에서도 **hardirq와 같은 전제(BH 금지, IRQ 꺼짐)** 로 돌게 해서 기존 드라이버를 깨뜨리지 않는다. 핸들러 입장에서는 여전히 잠들면 안 된다. 또 `intr=1`(BH 금지 때문)인데 `task=1`인 이상한 조합이 나온다 — `in_interrupt()`를 믿으면 안 되는 또 하나의 예.
- `IRQF_ONESHOT`, `IRQF_NO_THREAD`, `IRQF_PERCPU`가 붙은 IRQ는 강제 스레드화에서 빠진다(`irq_setup_forced_threading()` 1311행). §6의 기본 모드(`IRQF_ONESHOT` + thread_fn)는 그래서 `threadirqs`에서도 primary handler가 hardirq로 남는다(아래 예제 7).

### 7.3 예제 7 — `threadirqs`에서 softirq는 어디로 가나

무엇을 확인하나: `threadirqs` 부팅에서 기본 모드(`bug=0`) 모듈을 넣고 `/proc/softirqs`, 커널 스레드 목록, 콜백 문맥을 본다.

```sh
cat /proc/softirqs
ps | grep -E "ksoftirqd|irq/|kworker/0|migration|rcu_preempt" | grep -v grep
insmod /f2_ctx.ko
dmesg | grep "f2_ctx: \(hardirq\|tasklet\|irq_thread\|virq\|timer_list\|hrtimer\)"
ps | grep "irq/22" | grep -v grep
```

```text
lab initramfs: 6.18.55-dbg up 10.05s
                    CPU0       CPU1       
          HI:          0          0
       TIMER:        131        307
      NET_TX:          0          0
      NET_RX:          0          0
       BLOCK:          0          0
    IRQ_POLL:          0          0
     TASKLET:          0          1
       SCHED:       1025       1276
     HRTIMER:          0          0
         RCU:        645        478
== kernel threads (ps)
    9 0         0:00 [kworker/0:0-rcu]
   10 0         0:00 [kworker/0:0H-ev]
   11 0         0:00 [kworker/0:1-dev]
   14 0         0:00 [ksoftirqd/0]
   16 0         0:00 [rcu_preempt]
   19 0         0:00 [migration/0]
   22 0         0:00 [migration/1]
   24 0         0:00 [ksoftirqd/1]
   59 0         0:00 [kworker/0:1H]
   62 0         0:00 [irq/16-virtio0-]
   63 0         0:00 [irq/17-virtio0-]
   64 0         0:00 [irq/18-virtio0-]
   66 0         0:00 [irq/19-rtc-pl03]
   67 0         0:00 [irq/21-GPIO Key]
   68 0         0:00 [kworker/0:2-eve]
   69 0         0:00 [irq/13-uart-pl0]
== insmod (threadirqs=threadirqs)
[   11.582320] f2_ctx: timer_list    pc=0x000100 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [ktimers/1/23 cpu1]
[   11.584668] f2_ctx: hrtimer       pc=0x010001 task=0 hard=1 serv=0 bh=0 intr=1 nmi=0 atom=1 irqoff=1 rcu=0 mm=none [swapper/0/0 cpu0]
[   11.586511] f2_ctx: hrtimer_soft  pc=0x000100 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [ktimers/0/15 cpu0]
[   11.587327] f2_ctx: tasklet       pc=0x000100 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [ksoftirqd/1/24 cpu1]
[   11.623368] f2_ctx: virq=22 (GIC SPI 80) -> set PENDING
[   11.626732] f2_ctx: hardirq       pc=0x010001 task=0 hard=1 serv=0 bh=0 intr=1 nmi=0 atom=1 irqoff=1 rcu=0 mm=none [swapper/0/0 cpu0]
[   11.627552] f2_ctx: irq_thread    pc=0x000000 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=0 irqoff=0 rcu=0 mm=none [irq/22-f2_ctx/88 cpu0]
[   11.627673] f2_ctx: tasklet       pc=0x000100 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [ksoftirqd/0/14 cpu0]
   88 0         0:00 [irq/22-f2_ctx]
[   12.287896] reboot: Power down
```

(부팅 인자에 `threadirqs`. 위쪽 "== insmod" 줄의 `threadirqs=threadirqs`가 그 확인이다.)

출력에서 볼 것:

- `/proc/softirqs`의 10개 행이 softirq 벡터 전부다(HI=0 … RCU=9). TIMER·SCHED·RCU가 부팅만으로 수백~수천 번 돌았다. TASKLET 1은 부팅 중 누군가 쓴 것이다.
- `threadirqs`라서 virtio·RTC·GPIO 키·UART의 IRQ가 전부 `irq/NN-이름` 스레드를 가졌다.
- `timer_list`와 `hrtimer_soft` 콜백이 `ktimers/N` 스레드에서 돌았다(`pc=0x000100`, 가로챈 태스크 없음). 강제 스레드화 모드에서는 타이머 softirq를 전용 스레드 `ktimers/N`이 맡는다(랩 소스 `kernel/softirq.c`의 `.thread_comm = "ktimers/%u"`). 기본 hrtimer(`hrtimer` 줄)는 여전히 hardirq다.
- hardirq에서 예약한 tasklet이 `irq_exit`이 아니라 `ksoftirqd/0`에서 돌았다. `invoke_softirq()`가 `force_irqthreads()`이면 그 자리에서 처리하지 않고 `wakeup_softirqd()`만 하기 때문이다(`kernel/softirq.c` 487~507행).
- 결론: **같은 드라이버 코드라도 커널 구성(`threadirqs`, PREEMPT_RT)에 따라 어느 스레드 위에서 도는지가 바뀐다.** 바뀌지 않는 것은 "그 콜백 종류의 규칙"(timer 콜백은 잠들면 안 된다)이다. 규칙을 콜백 종류에 묶어서 외우고, 스레드 이름에 묶어서 외우지 말 것.

---

## 8. 예제 8 — ftrace로 중첩을 직접 보기

무엇을 확인하나: hardirq 안에서 softirq를 예약하고(`softirq_raise`), hardirq가 끝나는 `irq_exit_rcu()`에서 그 softirq가 실행되는(`softirq_entry`) 순서를 함수 그래프로 본다. `function_graph` 추적기를 우리 모듈 함수와 IRQ·softirq 핵심 함수로만 제한하고, `irq` 이벤트 5개를 함께 켠다(이벤트는 그래프 안에 `/* … */` 주석으로 끼어든다).

```sh
mount -t tracefs none /sys/kernel/tracing
cd /sys/kernel/tracing
for f in handle_irq_event __handle_irq_event_percpu __irq_wake_thread __tasklet_schedule irq_exit_rcu do_softirq_own_stack __do_softirq handle_softirqs tasklet_action ":mod:f2_ctx"; do
  echo "$f" >> set_ftrace_filter
done
echo 1 > options/funcgraph-proc
echo function_graph > current_tracer
for e in irq_handler_entry irq_handler_exit softirq_raise softirq_entry softirq_exit; do echo 1 > events/irq/$e/enable; done
echo > trace
insmod /f2_ctx.ko
echo 0 > tracing_on
cat trace > /tmp/tr.txt
# f2_hard가 찍힌 CPU의 줄만 골라 그 주변을 출력 (나머지 필터 스크립트는 생략)
```

```text
 0)    <idle>-0    =>   insmod-74   
 0)   insmod-74    |               |    handle_irq_event() {
 0)   insmod-74    |               |      __handle_irq_event_percpu() {
 0)   insmod-74    |               |        /* irq_handler_entry: irq=22 name=f2_ctx */
 0)   insmod-74    |               |        f2_hard [f2_ctx]() {
 0)   insmod-74    | + 43.584 us   |          f2_show [f2_ctx]();
 0)   insmod-74    |               |          __tasklet_schedule() {
 0)   insmod-74    |               |            /* softirq_raise: vec=6 [action=TASKLET] */
 0)   insmod-74    | + 28.512 us   |          }
 0)   insmod-74    | ! 150.624 us  |        }
 0)   insmod-74    |               |        /* irq_handler_exit: irq=22 ret=handled */
 0)   insmod-74    | + 64.336 us   |        __irq_wake_thread();
 0)   insmod-74    | ! 480.672 us  |      }
 0)   insmod-74    | ! 853.920 us  |    }
 0)   insmod-74    |               |  /* irq_handler_entry: irq=1 name=IPI */
 0)   insmod-74    |               |  /* irq_handler_exit: irq=1 ret=handled */
 0)   insmod-74    |               |  /* irq_handler_entry: irq=11 name=arch_timer */
 0)   insmod-74    |               |  /* softirq_raise: vec=1 [action=TIMER] */
 0)   insmod-74    |               |  /* softirq_raise: vec=9 [action=RCU] */
 0)   insmod-74    |               |  /* softirq_raise: vec=7 [action=SCHED] */
 0)   insmod-74    |               |  /* irq_handler_exit: irq=11 ret=handled */
 0)   insmod-74    |               |    irq_exit_rcu() {
 0)   insmod-74    |               |      do_softirq_own_stack() {
 0)   insmod-74    |               |        __do_softirq() {
 0)   insmod-74    |               |          handle_softirqs() {
 0)   insmod-74    |               |            /* softirq_entry: vec=1 [action=TIMER] */
 0)   insmod-74    |               |            /* softirq_exit: vec=1 [action=TIMER] */
 0)   insmod-74    |               |            /* softirq_entry: vec=6 [action=TASKLET] */
 0)   insmod-74    |               |            tasklet_action() {
 0)   insmod-74    |               |              f2_tl_fn [f2_ctx]() {
 0)   insmod-74    | + 46.544 us   |                f2_show [f2_ctx]();
 0)   insmod-74    | + 65.792 us   |              }
 0)   insmod-74    | ! 127.840 us  |            }
 0)   insmod-74    |               |            /* softirq_exit: vec=6 [action=TASKLET] */
 0)   insmod-74    |               |            /* softirq_entry: vec=7 [action=SCHED] */
 0)   insmod-74    |               |            /* softirq_exit: vec=7 [action=SCHED] */
 0)   insmod-74    |               |            /* softirq_entry: vec=9 [action=RCU] */
 0)   insmod-74    |               |            /* softirq_exit: vec=9 [action=RCU] */
 0)   insmod-74    | ! 516.048 us  |          }
 0)   insmod-74    | ! 523.056 us  |        }
 0)   insmod-74    | ! 531.616 us  |      }
 0)   insmod-74    | ! 547.216 us  |    }
 0)   insmod-74    =>   irq/22--76  
 0)   irq/22--76   |               |  f2_thread [f2_ctx]() {
 0)   irq/22--76   | + 37.968 us   |    f2_show [f2_ctx]();
== tasklet from process context
 1)   insmod-74    | ! 110.464 us  |    }
 ------------------------------------------
 1)   insmod-74    =>   ksoftir-22  
 ------------------------------------------

 1)   ksoftir-22   |               |  handle_softirqs() {
 1)   ksoftir-22   |               |  /* softirq_entry: vec=6 [action=TASKLET] */
 1)   ksoftir-22   |               |    tasklet_action() {
 1)   ksoftir-22   |               |      f2_tl_fn [f2_ctx]() {
 1)   ksoftir-22   | + 68.000 us   |        f2_show [f2_ctx]();
 1)   ksoftir-22   | ! 113.552 us  |      }
 1)   ksoftir-22   | ! 168.960 us  |    }
 1)   ksoftir-22   |               |  /* softirq_exit: vec=6 [action=TASKLET] */
 1)   ksoftir-22   | ! 216.832 us  |  }
 ------------------------------------------
 1)   ksoftir-22   =>   insmod-74   
 ------------------------------------------
```

(트레이스 앞뒤를 잘랐다. `!`·`#`는 실행 시간이 100 us·1 ms를 넘었다는 표시이고, TCG 에뮬레이션 + lockdep이라 실기보다 수십 배 느리다. `ksoftir-22`는 `ksoftirqd/1`의 잘린 이름이다.)

출력에서 볼 것:

- **hardirq 구간**: `handle_irq_event()` → `irq_handler_entry: irq=22 name=f2_ctx` → `f2_hard()` 안에서 `__tasklet_schedule()` → `softirq_raise: vec=6 [action=TASKLET]` → `irq_handler_exit` → `__irq_wake_thread()`(irq 스레드 깨우기). 핸들러는 softirq를 **예약만** 하고 실행하지 않는다.
- 그 뒤 같은 예외 진입 안에서 GIC가 대기 중이던 IPI(irq=1)와 타이머(irq=11)를 연달아 처리했다 — `gic_handle_irq()`가 IAR 레지스터를 1020 이상이 나올 때까지 루프로 읽기 때문이다(`drivers/irqchip/irq-gic.c` 340행). 타이머 핸들러가 TIMER·RCU·SCHED softirq를 raise했다.
- **softirq 구간**: `irq_exit_rcu()` → `do_softirq_own_stack()`(CPU별 스택으로 전환, `SOFTIRQ_ON_OWN_STACK=y`) → `handle_softirqs()`가 벡터 번호 순서로 TIMER(1) → TASKLET(6, 여기서 `f2_tl_fn`) → SCHED(7) → RCU(9)를 처리했다. 이 전체가 여전히 `insmod-74`라는 이름 아래에 있다 — `current`는 바뀌지 않았다.
- **process 구간**: 마지막에 `insmod-74 => irq/22--76` 컨텍스트 스위치, `f2_thread()`가 스레드에서 돈다.
- 아래쪽 `== tasklet from process context` 부분: process에서 예약한 tasklet은 CPU 1에서 `insmod-74 => ksoftir-22` 스위치 후 `ksoftirqd/1`의 `handle_softirqs()`에서 돌았다. 처리 후 다시 `insmod`로 돌아간다.

softirq가 IRQ를 켠 채 돈다는 것은 같은 스크립트의 **앞선 실행**에서 우연히 잡혔다. RCU softirq를 처리하는 도중에 타이머 hardirq가 끼어들었고, 그 hardirq의 `irq_exit_rcu()`는 이미 softirq 처리 중(`in_interrupt()` 참)이라 softirq를 다시 돌리지 않고 5.664 us 만에 빠져나왔다:

```text
 0)   insmod-75    |               |            /* softirq_entry: vec=9 [action=RCU] */
 0)   insmod-75    |               |            /* irq_handler_entry: irq=11 name=arch_timer */
 0)   insmod-75    |               |            /* irq_handler_exit: irq=11 ret=handled */
 0)   insmod-75    |   5.664 us    |            irq_exit_rcu();
 0)   insmod-75    |               |            /* softirq_exit: vec=9 [action=RCU] */
```

이게 그림 1의 "softirq 도중에 hardirq가 끼어들 수 있다"의 실물이다. 반대(hardirq 도중 softirq)는 일어나지 않는다.

---

## 9. "잠들어도 되나" 규칙

### 9.1 잠든다 = `schedule()`을 부른다

커널에서 "잠든다(sleep, block)"는 정확히 **현재 태스크를 대기 상태로 바꾸고 `schedule()`을 불러 CPU를 넘기는 것**이다. 시간이 걸리는 것(busy-wait)과는 다르다: `udelay(100)`은 100 us 동안 CPU를 붙잡고 돌 뿐 잠들지 않으므로 atomic에서도 합법이다(길면 나쁠 뿐이다). 반대로 `kmalloc(GFP_KERNEL)`은 보통 1 us도 안 걸리지만 **메모리가 부족하면 회수(reclaim)하느라 잠들 수 있으므로** atomic에서 불법이다. 기준은 "오래 걸리나"가 아니라 "`schedule()`에 닿을 수 있나"다.

잠들 수 있는 대표 API와 atomic에서 쓰는 대안:

| 잠들 수 있는 것 | 왜 잠드나 | atomic 문맥의 대안 |
|---|---|---|
| `msleep`, `usleep_range`, `ssleep` | 타이머를 걸고 `schedule()` | `udelay`/`ndelay` (짧게), 아니면 hrtimer·workqueue로 나눔 |
| `mutex_lock`, `down` (semaphore) | 경합하면 대기 큐에서 잠듦 | `spin_lock` 계열 |
| `wait_event*`, `wait_for_completion*` | 조건이 될 때까지 잠듦 | 기다리지 말고 `wake_up`/`complete`로 상대를 깨우기만 |
| `kmalloc`/`kzalloc`(`GFP_KERNEL`), `vmalloc` | 메모리 회수·I/O | `GFP_ATOMIC`/`GFP_NOWAIT` (실패 가능성 처리), 미리 할당한 풀 |
| `copy_from_user`/`copy_to_user`/`get_user` | 유저 페이지 fault 처리 (디스크 I/O 가능) | process context에서 미리 복사해 둠 |
| `i2c_transfer`, `spi_sync`, `regmap`(버스 기반) | 버스 완료를 기다림 | threaded IRQ·workqueue로 옮김, `spi_async` |
| `request_firmware`, `flush_work`, `synchronize_rcu` | 다른 작업 완료를 기다림 | `call_rcu`, 비동기 API |

### 9.2 왜 각 문맥에서 잠들 수 없나

**hardirq** — 면접 최빈출 질문의 답:

1. **재울 대상이 없다.** hardirq는 태스크가 아니다. `current`는 가로채인 무고한 태스크라서, 여기서 `schedule()`하면 그 태스크가 이유 없이 잠들고 핸들러의 나머지는 그 태스크가 깨어날 때까지 멈춘다.
2. **IRQ 스택은 CPU에 하나다.** 핸들러는 CPU별 IRQ 스택 위에 프레임을 쌓고 있다. 다른 태스크로 넘어가면 그 CPU에서 다음 IRQ가 같은 스택을 덮어쓴다. 태스크 스택처럼 "잠든 동안 보관"할 수 없다.
3. **IRQ가 꺼져 있고, 인터럽트 줄이 끝나지 않았다.** 핸들러가 끝나야 EOI를 쓰고 IRQ를 다시 켠다. 잠들면 이 CPU는 타이머 tick조차 못 받고, 그 IRQ 줄(레벨 트리거라면 계속 assert)이 막힌다.
4. **가로챈 코드가 락을 쥐고 있을 수 있다.** 가로채인 태스크가 잡고 있던 락을 핸들러가 기다리면 영원히 안 풀린다(주인은 핸들러 밑에 깔려 있다).
5. **스케줄러가 금지한다.** `preempt_count`의 HARDIRQ 칸이 0이 아니면 `schedule()` 안의 `schedule_debug()`가 "scheduling while atomic"을 낸다.

**softirq** — 1, 2(`SOFTIRQ_ON_OWN_STACK`일 때), 4, 5가 그대로 적용된다. 게다가 softirq는 "지금 이 CPU의 나머지 softirq 처리 전체"를 대표하므로 잠들면 그 CPU의 네트워크·타이머·RCU 처리가 다 멈춘다. `ksoftirqd` 안에서 돌 때도 softirq 칸이 서 있으므로 같은 규칙이다.

**스핀락을 잡은 process 문맥** — 스핀락은 "기다리는 쪽이 돌면서(spin) 기다린다"는 전제의 락이다. 주인이 잠들면, 같은 CPU에 새로 스케줄된 태스크나 다른 CPU들이 그 락을 기다리며 CPU를 태운다. 단일 CPU에서 같은 락을 다시 잡으려 하면 영원히 돈다. 또 `spin_lock()`은 선점을 끄므로(`pc` +1) 잠드는 순간 `schedule_debug()`에 걸린다.

**`local_irq_disable()`/`spin_lock_irqsave()` 구간** — IRQ를 끈 채 잠들면 깨워 줄 타이머 IRQ도 못 받는다. `preempt_count`가 0이어도 금지다.

**RCU 읽기 구간** — RCU writer는 "모든 CPU가 읽기 구간을 한 번씩 벗어날 때까지"(grace period) 기다렸다가 옛 데이터를 해제한다. 읽기 구간에서 잠들면 grace period가 끝나지 않아 메모리 해제가 쌓인다. 잠들어야 하면 SRCU(`srcu_read_lock`)를 쓴다(F4).

### 9.3 `might_sleep()`과 `DEBUG_ATOMIC_SLEEP` — 사고 전에 잡는 장치

E4 §5.3에서 봤듯 잠들 **수도** 있는 함수의 입구에는 `might_sleep()` 주석이 박혀 있다. 랩 소스에서 실제 위치 몇 개:

```sh
grep -n "might_sleep();" /var/lab/src/linux-6.18.55/kernel/locking/mutex.c | head -2
sed -n 113,116p /var/lab/src/linux-6.18.55/kernel/sched/completion.c
grep -n "might_fault();" /var/lab/src/linux-6.18.55/include/linux/uaccess.h | head -2
```

```text
271:	might_sleep();
575:	might_sleep();
__wait_for_common(struct completion *x,
		  long (*action)(long), long timeout, int state)
{
	might_sleep();
106:	might_fault();
142:	might_fault();
```

`mutex.c` 271행은 lockdep이 꺼진 구성의 `mutex_lock()` 입구, 575행은 lockdep 구성(`mutex_lock_nested` → `__mutex_lock_common`)의 입구다. 디버그 커널은 후자를 타므로 §10.1의 경고가 575행을 가리킨다. `copy_from_user` 계열은 `might_fault()` → `__might_fault()` → `__might_sleep()`으로 이어진다(`mm/memory.c` 7125행).

`CONFIG_DEBUG_ATOMIC_SLEEP=y`이면 `__might_sleep()` → `__might_resched()`(`kernel/sched/core.c` 8882행)가 검사한다:

```c
	/* WARN_ON_ONCE() by default, no rate limit required: */
	rcu_sleep_check();

	if ((resched_offsets_ok(offsets) && !irqs_disabled() &&
	     !is_idle_task(current) && !current->non_block_count) ||
	    system_state == SYSTEM_BOOTING || system_state > SYSTEM_RUNNING ||
	    oops_in_progress)
		return;

	if (time_before(jiffies, prev_jiffy + HZ) && prev_jiffy)
		return;
	prev_jiffy = jiffies;
```

읽는 법:

- 통과 조건은 "`preempt_count`가 기대값과 같고(`resched_offsets_ok`) **그리고** IRQ가 켜져 있고 **그리고** idle 태스크가 아님". 셋 중 하나라도 어긋나면 `BUG: sleeping function called from invalid context`를 찍는다. §5에서 본 `in_atomic()`의 구멍(IRQ만 끈 구간)을 `irqs_disabled()`로 메운다.
- `rcu_sleep_check()`가 RCU 읽기 구간을 따로 검사한다(`PREEMPT_RCU`에서 `pc`에 안 보이는 구멍을 메움).
- **부팅 중(`SYSTEM_BOOTING`)에는 검사하지 않는다.** 그래서 built-in 드라이버의 probe 버그는 모듈로 넣었을 때에야 보이기도 한다.
- **1초(HZ)에 한 번만** 찍는다. 첫 경고만 믿고, 같은 초에 생긴 두 번째 위반은 안 보일 수 있다는 것을 기억하자.

두 메시지의 차이(E4 §5.4에서 정리했다): **"sleeping function called from invalid context" = 잠들 수 있는 함수를 불렀다(사고 예고, 디버그 옵션 필요)**, **"scheduling while atomic" = 실제로 `schedule()`에 들어갔다(사고 발생, 옵션 없이도 항상 검사)**.

```svg
<svg viewBox="0 0 680 520" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="170" y="10" width="340" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="32" text-anchor="middle" font-size="13" font-weight="bold">이 줄의 코드가 지금 잠들어도 되나?</text>
<line x1="220" y1="44" x2="220" y2="68" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="20" y="70" width="400" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="220" y="91" text-anchor="middle" font-size="13" font-weight="bold">① 인터럽트 컨텍스트인가?</text>
<text x="220" y="111" text-anchor="middle" font-size="12">in_task() == 0 → hardirq · softirq · NMI</text>
<line x1="420" y1="96" x2="438" y2="96" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="429" y="88" text-anchor="middle" font-size="12">예</text>
<line x1="220" y1="122" x2="220" y2="148" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="250" y="140" text-anchor="middle" font-size="12">아니오</text>
<rect x="20" y="150" width="400" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="220" y="171" text-anchor="middle" font-size="13" font-weight="bold">② preempt_count ≠ 0 인가?</text>
<text x="220" y="191" text-anchor="middle" font-size="12">spin_lock · preempt_disable · local_bh_disable</text>
<line x1="420" y1="176" x2="438" y2="176" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="429" y="168" text-anchor="middle" font-size="12">예</text>
<line x1="220" y1="202" x2="220" y2="228" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="250" y="220" text-anchor="middle" font-size="12">아니오</text>
<rect x="20" y="230" width="400" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="220" y="251" text-anchor="middle" font-size="13" font-weight="bold">③ IRQ가 꺼져 있나?</text>
<text x="220" y="271" text-anchor="middle" font-size="12">local_irq_disable · spin_lock_irqsave → irqs_disabled()</text>
<line x1="420" y1="256" x2="438" y2="256" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="429" y="248" text-anchor="middle" font-size="12">예</text>
<line x1="220" y1="282" x2="220" y2="308" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="250" y="300" text-anchor="middle" font-size="12">아니오</text>
<rect x="20" y="310" width="400" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="220" y="331" text-anchor="middle" font-size="13" font-weight="bold">④ RCU 읽기 구간 안인가?</text>
<text x="220" y="351" text-anchor="middle" font-size="12">rcu_read_lock() — PREEMPT_RCU면 pc에 안 보임</text>
<line x1="420" y1="336" x2="438" y2="336" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="429" y="328" text-anchor="middle" font-size="12">예</text>
<line x1="220" y1="362" x2="220" y2="398" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="250" y="380" text-anchor="middle" font-size="12">아니오</text>
<rect x="440" y="70" width="230" height="292" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="555" y="100" text-anchor="middle" font-size="13" font-weight="bold">잠들면 안 된다 (atomic)</text>
<text x="458" y="135" text-anchor="start" font-size="12">· mutex · msleep 금지</text>
<text x="458" y="162" text-anchor="start" font-size="12">· wait_event · schedule 금지</text>
<text x="458" y="189" text-anchor="start" font-size="12">· copy_to_user 금지</text>
<text x="458" y="216" text-anchor="start" font-size="12">· kmalloc(GFP_ATOMIC)</text>
<text x="458" y="243" text-anchor="start" font-size="12">· 락은 spinlock 계열만</text>
<text x="458" y="270" text-anchor="start" font-size="12">· 긴 일은 workqueue로</text>
<text x="458" y="297" text-anchor="start" font-size="12">· 또는 irq 스레드로 넘김</text>
<text x="458" y="324" text-anchor="start" font-size="12">· 할당 실패(NULL) 처리</text>
<rect x="20" y="400" width="400" height="60" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="220" y="424" text-anchor="middle" font-size="13" font-weight="bold">잠들어도 된다 (process 컨텍스트)</text>
<text x="220" y="446" text-anchor="middle" font-size="12">mutex · msleep · wait_event · GFP_KERNEL · copy_from_user</text>
<text x="340" y="500" text-anchor="middle" font-size="12">DEBUG_ATOMIC_SLEEP 커널은 ①~③을 might_sleep()에서, ④는 RCU 검사로 자동으로 잡는다</text>
</svg>
```

그림 3 — 코드 한 줄 앞에서 던질 질문 네 개. ①은 "누가 불렀나"(콜백 종류), ②~④는 "그 사이에 무엇을 쥐었나"(락·금지 상태)다. 하나라도 "예"면 오른쪽 규칙을 따른다.

---

## 10. 일부러 어기기 — 실측 로그 네 개

네 실험 모두 같은 `f2_ctx.ko`에 `bug=N`만 바꿔 각각 새로 부팅한 게스트에서 돌렸다(rate limit과 lockdep의 "첫 보고 후 꺼짐"을 피하려고 한 부팅에 한 위반). 각 테스트 스크립트는 `insmod /f2_ctx.ko bug=N` 후 `dmesg`를 거른다.

### 10.1 예제 9 — timer 콜백(softirq)에서 `mutex_lock` (`bug=1`)

무엇을 확인하나: timer 콜백에서 경합 없는 mutex를 잡는다. 실제로 잠들지는 않지만(경합이 없으니 즉시 획득) 디버그 커널이 잡는지.

```text
[   10.396008] f2_ctx: loading out-of-tree module taints kernel.
[   10.429196] f2_ctx: timer_list    pc=0x000101 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [swapper/1/0 cpu1]
[   10.429412] BUG: sleeping function called from invalid context at kernel/locking/mutex.c:575
[   10.429476] in_atomic(): 1, irqs_disabled(): 0, non_block: 0, pid: 0, name: swapper/1
[   10.429516] preempt_count: 101, expected: 0
[   10.429547] RCU nest depth: 0, expected: 0
[   10.429651] 1 lock held by swapper/1/0:
[   10.429763]  #0: ffff80008000bdc8 ((&f2_timer)){+.-.}-{0:0}, at: call_timer_fn+0x8/0x320
[   10.434651] CPU: 1 UID: 0 PID: 0 Comm: swapper/1 Tainted: G           O        6.18.55-dbg #1 PREEMPT 
[   10.436297] Tainted: [O]=OOT_MODULE
[   10.436330] Hardware name: linux,dummy-virt (DT)
[   10.436535] Call trace:
[   10.436977]  show_stack+0x30/0x90 (C)
[   10.437173]  dump_stack_lvl+0x70/0x98
[   10.437203]  dump_stack+0x18/0x24
[   10.437226]  __might_resched+0x174/0x248
[   10.437259]  __might_sleep+0x4c/0x98
[   10.437281]  __mutex_lock+0x74/0x820
[   10.437335]  mutex_lock_nested+0x2c/0x40
[   10.437357]  f2_timer_fn+0x5c/0x88 [f2_ctx]
[   10.438819]  call_timer_fn+0xb8/0x320
[   10.438853]  __run_timer_base+0x2c4/0x3e0
[   10.438930]  run_timer_softirq+0x3c/0x70
[   10.438966]  handle_softirqs+0x12c/0x4b0
[   10.439009]  __do_softirq+0x1c/0x28
[   10.439038]  ____do_softirq+0x18/0x30
[   10.439061]  call_on_irq_stack+0x30/0x48
[   10.439095]  do_softirq_own_stack+0x24/0x50
[   10.439127]  __irq_exit_rcu+0x16c/0x1b8
[   10.439148]  irq_exit_rcu+0x18/0x48
[   10.439183]  el1_interrupt+0x3c/0x58
[   10.439222]  el1h_64_irq_handler+0x18/0x30
[   10.439257]  el1h_64_irq+0x6c/0x70
[   10.439653]  default_idle_call+0xbc/0x2e8 (P)
[   10.439787]  do_idle+0x284/0x2e0
[   10.439830]  cpu_startup_entry+0x40/0x50
[   10.439869]  secondary_start_kernel+0x13c/0x168
[   10.439906]  __secondary_switched+0xc0/0xc8
...
[   10.453645] =============================
[   10.453792] [ BUG: Invalid wait context ]
[   10.453903] 6.18.55-dbg #1 Tainted: G        W  O       
[   10.454033] -----------------------------
[   10.454108] swapper/1/0 is trying to lock:
[   10.454186] ffffc129b9c4e118 (f2_mutex){+.+.}-{4:4}, at: f2_timer_fn+0x5c/0x88 [f2_ctx]
[   10.454473] other info that might help us debug this:
[   10.454594] context-{3:3}
[   10.454657] 1 lock held by swapper/1/0:
[   10.454752]  #0: ffff80008000bdc8 ((&f2_timer)){+.-.}-{0:0}, at: call_timer_fn+0x8/0x320
[   10.455072] stack backtrace:
...
[   10.458899]  __lock_acquire+0x96c/0x15d8
[   10.458996]  lock_acquire+0x22c/0x378
[   10.459076]  __mutex_lock+0xb8/0x820
[   10.459156]  mutex_lock_nested+0x2c/0x40
[   10.459240]  f2_timer_fn+0x5c/0x88 [f2_ctx]
...
```

(콜 트레이스 일부와 lockdep 두 번째 보고의 나머지를 생략했다.)

출력에서 볼 것 — 위에서부터 읽는 법:

- `at kernel/locking/mutex.c:575` — `__mutex_lock_common()`의 `might_sleep()` 줄(§9.3의 grep 결과와 같은 줄 번호).
- `in_atomic(): 1, irqs_disabled(): 0, … name: swapper/1` — atomic이지만 IRQ는 켜져 있다 → hardirq가 아니다. `current`가 idle(`swapper/1`)이다 → interrupt 문맥이 idle을 가로챘다.
- **`preempt_count: 101`** — 그림 2로 분해하면 SOFTIRQ 칸 1(softirq 실행 중) + PREEMPT 칸 1(idle 태스크 몫). "softirq 안에서 잠들 수 있는 함수를 불렀다"가 숫자 하나로 확정된다.
- `1 lock held: ((&f2_timer))` — lockdep이 timer 콜백 실행을 가짜 락으로 기록한다. 어떤 타이머인지 이름으로 알려 준다.
- 콜 트레이스를 **아래에서 위로** 읽으면 문맥이 보인다: `default_idle_call` (idle) → `el1h_64_irq` (IRQ 예외) → `irq_exit_rcu` → `do_softirq_own_stack` → `handle_softirqs` → `run_timer_softirq` → `call_timer_fn` → `f2_timer_fn` → `mutex_lock_nested` → `__might_sleep`. §8의 ftrace와 같은 경로다.
- 이어서 lockdep이 **`[ BUG: Invalid wait context ]`** 를 따로 냈다. lockdep은 락마다 "대기 종류"를 매긴다(`include/linux/lockdep_types.h`: 0 INV, 1 FREE, 2 SPIN, 3 CONFIG, 4 SLEEP). `f2_mutex`는 `{4:4}` = 잠드는 락, 지금 문맥 `context-{3:3}`은 softirq가 허용하는 최대치 3(`spinlock_t` 수준). 4 > 3이라 위반이다. 이 검사는 `DEBUG_ATOMIC_SLEEP`과 독립이라 둘 중 하나만 켜져 있어도 잡힌다.

고치는 법: 콜백에서 하려던 일을 `schedule_work()`로 넘기고 work 함수에서 mutex를 잡는다(§6의 `workqueue` 줄이 그 문맥). 또는 공유 데이터를 `spin_lock_bh()`로 보호하도록 바꾼다(F4).

### 10.2 예제 10 — hardirq 핸들러에서 `kmalloc(GFP_KERNEL)` (`bug=2`)

무엇을 확인하나: IRQ 핸들러에서 `GFP_KERNEL` 할당. 메모리가 넉넉하므로 실제로 잠들지 않고 성공한다 — 일반 커널에서는 아무 일 없이 지나가는 **잠복 버그**다.

```text
[   12.516520] f2_ctx: hardirq       pc=0x010001 task=0 hard=1 serv=0 bh=0 intr=1 nmi=0 atom=1 irqoff=1 rcu=0 mm=none [swapper/0/0 cpu0]
...
[   12.521266] WARNING: inconsistent lock state
[   12.521921] 6.18.55-dbg #1 Tainted: G           O       
[   12.522468] --------------------------------
[   12.522847] inconsistent {HARDIRQ-ON-W} -> {IN-HARDIRQ-W} usage.
[   12.523395] swapper/0/0 [HC1[1]:SC0[0]:HE0:SE1] takes:
[   12.523889] ffffb89536039228 (fs_reclaim){?.+.}-{0:0}, at: __kmalloc_cache_noprof+0x64/0x608
[   12.526571] {HARDIRQ-ON-W} state was registered at:
[   12.527280]   lock_acquire+0x22c/0x378
[   12.528119]   fs_reclaim_acquire+0xd0/0xf0
[   12.528677]   mem_cgroup_css_alloc+0x74/0x6c0
[   12.529221]   cgroup_init_subsys+0xb8/0x218
[   12.529710]   cgroup_init+0x1f4/0x468
[   12.530224]   start_kernel+0x768/0x780
[   12.530738]   __primary_switched+0x88/0x98
...
[   12.534205]  Possible unsafe locking scenario:
[   12.534205] 
[   12.534462]        CPU0
[   12.534587]        ----
[   12.534803]   lock(fs_reclaim);
[   12.535090]   <Interrupt>
[   12.535278]     lock(fs_reclaim);
[   12.535868] 
[   12.535868]  *** DEADLOCK ***
...
[   12.546671]  fs_reclaim_acquire+0xd0/0xf0
[   12.547154]  __kmalloc_cache_noprof+0x64/0x608
[   12.547457]  f2_hard+0xb4/0xd0 [f2_ctx]
...
[   12.554013] BUG: sleeping function called from invalid context at include/linux/sched/mm.h:321
[   12.554141] in_atomic(): 1, irqs_disabled(): 1, non_block: 0, pid: 0, name: swapper/0
[   12.554178] preempt_count: 10001, expected: 0
[   12.554194] RCU nest depth: 0, expected: 0
[   12.554239] INFO: lockdep is turned off.
...
[   12.559127]  f2_hard+0xb4/0xd0 [f2_ctx]
[   12.559158]  __handle_irq_event_percpu+0xb0/0x3a0
[   12.559176]  handle_irq_event+0x4c/0xc0
[   12.559194]  handle_fasteoi_irq+0xfc/0x1a8
[   12.559209]  handle_irq_desc+0x3c/0x68
[   12.559226]  generic_handle_domain_irq+0x24/0x40
[   12.559244]  gic_handle_irq+0x40/0xe0
[   12.559257]  call_on_irq_stack+0x30/0x48
[   12.559274]  do_interrupt_handler+0x88/0x98
[   12.559292]  el1_interrupt+0x38/0x58
...
```

(lockdep 보고의 콜 트레이스와 두 번째 보고의 앞부분 일부를 생략했다.)

출력에서 볼 것:

- **lockdep이 먼저** 잡았다: `inconsistent {HARDIRQ-ON-W} -> {IN-HARDIRQ-W} usage`, 대상은 `fs_reclaim`. `fs_reclaim`은 진짜 락이 아니라 "이 할당은 메모리 회수에 들어갈 수 있다"를 표시하는 lockdep용 가짜 락이다. 부팅 때 IRQ가 켜진 상태에서 쓰였는데(`HARDIRQ-ON-W`, `mem_cgroup_css_alloc`에서 등록) 이번엔 hardirq 안에서 쓰였다(`IN-HARDIRQ-W`). lockdep의 시나리오 그림 — `lock(fs_reclaim); <Interrupt> lock(fs_reclaim); *** DEADLOCK ***` — 은 "회수 중인 코드를 IRQ가 끊고 들어와 또 회수하면 교착"이라는 뜻이다.
- `swapper/0/0 [HC1[1]:SC0[0]:HE0:SE1]` — lockdep의 문맥 표기. HC=hardirq 문맥 1, SC=softirq 문맥 0, HE=hardirq 켜짐 0(꺼짐), SE=softirq 켜짐 1. `preempt_count`와 같은 정보를 다른 형식으로 보여 준다.
- 이어서 `__might_resched`: **`preempt_count: 10001`** = HARDIRQ 칸 1 + PREEMPT 칸 1(idle). **`irqs_disabled(): 1`** — hardirq는 IRQ가 꺼진 채 돈다는 §2.2의 실물. 위치 `include/linux/sched/mm.h:321`은 `might_alloc()`의 `might_sleep_if(gfpflags_allow_blocking(gfp_mask))` 줄이다.
- `INFO: lockdep is turned off.` — lockdep은 첫 보고 후 스스로 꺼진다. **lockdep 경고는 부팅당 첫 번째만 믿을 수 있다.** 그래서 이 노트는 위반 하나마다 새로 부팅했다.
- 그 뒤 tasklet·irq 스레드는 정상적으로 돌았다(할당은 성공했으므로). 증상 없는 버그라는 점이 무섭다.

고치는 법: `GFP_ATOMIC`으로 바꾸고 NULL을 처리하거나, 할당 자체를 probe 때 미리 해 둔다. 센서 드라이버라면 FIFO 버퍼를 probe에서 `devm_kzalloc`으로 잡아 두고 IRQ에서는 쓰기만 하는 것이 정석이다.

### 10.3 예제 11 — `spin_lock_irqsave` 안에서 `msleep` (`bug=3`)

무엇을 확인하나: E4 §5.2는 `spin_lock` + `msleep`이었다. 여기서는 IRQ까지 끈 상태에서 잠들면 무슨 일이 연쇄적으로 생기는지 본다.

```text
lab initramfs: 6.18.55-dbg up 11.47s
[   12.482772] BUG: scheduling while atomic: insmod/72/0x00000002
[   12.498404] BUG: spinlock wrong CPU on CPU#0, insmod/72
[   12.498632]  lock: f2_lock+0x0/0x48 [f2_ctx], .magic: dead4ead, .owner: insmod/72, .owner_cpu: 1
...
[   12.499059]  do_raw_spin_unlock+0x10c/0x120
[   12.499114]  _raw_spin_unlock_irqrestore+0x38/0xa0
[   12.499148]  f2_init+0x2c8/0xde8 [f2_ctx]
...
[   12.503504] BUG: sleeping function called from invalid context at kernel/sched/completion.c:116
[   12.503589] in_atomic(): 1, irqs_disabled(): 0, non_block: 0, pid: 72, name: insmod
[   12.503623] preempt_count: ffffffff, expected: 0
...
[   12.504592] ------------[ cut here ]------------
[   12.504665] WARNING: CPU: 0 PID: 72 at kernel/irq/irqdesc.c:666 handle_irq_desc+0x4c/0x68
...
[   12.510278] pc : handle_irq_desc+0x4c/0x68
...
[   12.513225] Call trace:
[   12.513311]  handle_irq_desc+0x4c/0x68 (P)
[   12.513414]  gic_handle_irq+0x40/0xe0
[   12.513497]  call_on_irq_stack+0x30/0x48
[   12.513586]  do_interrupt_handler+0x88/0x98
[   12.513677]  el1_interrupt+0x38/0x58
[   12.513765]  el1h_64_irq_handler+0x18/0x30
[   12.513853]  el1h_64_irq+0x6c/0x70
[   12.513991]  __might_resched+0x154/0x248 (P)
[   12.514106]  __might_sleep+0x4c/0x98
[   12.514187]  wait_for_completion_timeout+0x44/0x1c0
[   12.514292]  f2_init+0x350/0xde8 [f2_ctx]
[   12.514438]  do_one_initcall+0x84/0x380
...
[   12.522248] BUG: scheduling while atomic: insmod/72/0x00000000
...
qemu-system-aarch64: terminating on signal 15 from pid 758622 (timeout)
```

(콘솔 출력. 게스트가 멈춰서 `dmesg`를 찍는 줄까지 가지 못했고, 실행기의 `timeout`이 QEMU를 끝냈다. 중간의 레지스터 덤프와 콜 트레이스 일부를 생략했다.)

출력에서 볼 것 — 버그 하나가 줄줄이 사고를 낸다:

1. `scheduling while atomic: insmod/72/0x00000002` — `msleep()`은 입구에 `might_sleep()`이 없어서(랩 소스 `kernel/time/sleep_timeout.c` 313~319행: 루프 안에서 바로 `schedule_timeout_uninterruptible()`) 사고 예고 없이 **실제 `schedule()`에서** 걸렸다. `0x2` = 스핀락 1 + `schedule()` 진입 때의 선점 끔 1.
2. 스케줄러는 경고 후 `preempt_count`를 강제로 바로잡고 태스크를 재운다. 깨어난 `insmod`는 **CPU 1에서 잡은 스핀락을 CPU 0에서 풀었다**: `BUG: spinlock wrong CPU on CPU#0 … .owner_cpu: 1`. 스핀락을 잡은 채 잠들면 다른 CPU로 옮겨질 수 있다는 것이 그대로 드러났다(`CONFIG_DEBUG_SPINLOCK`의 검사, lockdep이 켜면 함께 켜진다).
3. 바로잡힌 카운트에서 `spin_unlock_irqrestore()`가 1을 또 빼서 **언더플로**: 다음 `wait_for_completion_timeout()`의 `might_sleep()`이 `preempt_count: ffffffff`를 찍었다(그림 2 마지막 행).
4. 그다음 IRQ가 들어오자 `handle_irq_desc()`의 `WARN_ON_ONCE(!in_hardirq() && …)`가 터졌다(`kernel/irq/irqdesc.c:666`). `0xffffffff + 0x10000`(HARDIRQ_OFFSET)은 32비트에서 `0x0000ffff`가 되어 HARDIRQ 칸이 0 — **커널이 자기가 hardirq 안에 있다는 사실조차 모르게 됐다.** 이 IRQ는 `-EPERM`으로 버려진다.
5. `scheduling while atomic: insmod/72/0x00000000` — 이번엔 카운트가 0인데도 걸렸다. `__schedule()` 안에서는 정상이면 1이어야 하는데(`in_atomic_preempt_off()`는 `!= 1`을 검사) 언더플로 때문에 0이 됐기 때문이다. 그 뒤 게스트는 응답하지 않았고 `timeout`이 끝냈다.

이 노트를 쓰는 중 앞선 `bug=3` 실행(같은 코드 경로)에서는 1·3·5번 메시지만 나오고 게스트가 끝까지 살아남았다. **같은 버그가 실행마다 다른 증상을 낸다** — 현장의 "가끔 멈춘다" 버그가 대개 이런 모습이다. 첫 번째 BUG 줄만 원인이고 나머지는 전부 2차 피해라는 것을 기억하자: **로그는 위에서부터, 첫 BUG만 고친다.**

### 10.4 예제 12 — `rcu_read_lock` 안에서 `msleep` (`bug=4`)

무엇을 확인하나: `preempt_count`가 0인 "숨은 atomic"(§5의 `rcu_read_lock` 줄)에서 잠들면 잡히는지.

```text
[   13.913216] f2_ctx: loading out-of-tree module taints kernel.
[   13.933259] ------------[ cut here ]------------
[   13.933286] Voluntary context switch within RCU read-side critical section!
[   13.933717] WARNING: CPU: 0 PID: 73 at kernel/rcu/tree_plugin.h:332 rcu_note_context_switch+0x7a4/0x7f8
[   13.935226] Modules linked in: f2_ctx(O+)
[   13.936372] CPU: 0 UID: 0 PID: 73 Comm: insmod Tainted: G           O        6.18.55-dbg #1 PREEMPT 
[   13.936503] Tainted: [O]=OOT_MODULE
[   13.936533] Hardware name: linux,dummy-virt (DT)
[   13.936697] pstate: 600000c5 (nZCv daIF -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[   13.936743] pc : rcu_note_context_switch+0x7a4/0x7f8
[   13.936788] lr : rcu_note_context_switch+0x7a4/0x7f8
[   13.936825] sp : ffff800080893880
...
[   13.937461] Call trace:
[   13.937653]  rcu_note_context_switch+0x7a4/0x7f8 (P)
[   13.937805]  __schedule+0x104/0x1088
[   13.937882]  schedule+0x48/0x140
[   13.937908]  schedule_timeout+0x94/0x140
[   13.937931]  msleep+0x50/0x80
[   13.937957]  f2_init+0x32c/0xde8 [f2_ctx]
...
[   13.939003] ---[ end trace 0000000000000000 ]---
[   13.955951] f2_ctx: timer_list    pc=0x000101 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [swapper/0/0 cpu0]
[   13.958248] f2_ctx: hrtimer       pc=0x010001 task=0 hard=1 serv=0 bh=0 intr=1 nmi=0 atom=1 irqoff=1 rcu=0 mm=none [swapper/0/0 cpu0]
[   13.961148] f2_ctx: hrtimer_soft  pc=0x000101 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [swapper/0/0 cpu0]
[   13.961907] f2_ctx: tasklet       pc=0x000100 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [ksoftirqd/0/14 cpu0]
[   13.963226] f2_ctx: bh_workqueue  pc=0x000100 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=none [ksoftirqd/0/14 cpu0]
[   13.964123] f2_ctx: workqueue     pc=0x000000 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=0 irqoff=0 rcu=0 mm=none [kworker/1:1/32 cpu1]
[   13.977579] f2_ctx: kthread       pc=0x000000 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=0 irqoff=0 rcu=0 mm=none [f2_kthread/74 cpu1]
[   13.987070] f2_ctx: virq=22 (GIC SPI 80) -> set PENDING
[   13.987575] f2_ctx: hardirq       pc=0x010001 task=0 hard=1 serv=0 bh=0 intr=1 nmi=0 atom=1 irqoff=1 rcu=0 mm=user [insmod/73 cpu0]
[   13.988334] f2_ctx: tasklet       pc=0x000101 task=0 hard=0 serv=1 bh=1 intr=1 nmi=0 atom=1 irqoff=0 rcu=0 mm=user [insmod/73 cpu0]
[   13.988554] f2_ctx: irq_thread    pc=0x000000 task=1 hard=0 serv=0 bh=0 intr=0 nmi=0 atom=0 irqoff=0 rcu=0 mm=none [irq/22-f2_ctx/75 cpu0]
```

(레지스터 덤프와 콜 트레이스 아래쪽을 생략했다.)

출력에서 볼 것:

- `Voluntary context switch within RCU read-side critical section!` — `kernel/rcu/tree_plugin.h:332`의 `WARN_ONCE(!preempt && rcu_preempt_depth() > 0, …)`. `PREEMPT_RCU`에서는 읽기 구간 안에서 **선점당하는 것(involuntary)은 허용**되지만(RCU가 그 태스크를 추적해서 grace period를 미룬다), **스스로 잠드는 것(voluntary)은 금지**다. 이 검사는 스케줄러의 `rcu_note_context_switch()`에 있어 디버그 옵션 없이도 동작한다(`WARN_ONCE`라 부팅당 한 번).
- `preempt_count` 기반 검사는 아무 말도 하지 않았다(카운트가 0이므로). `msleep` 대신 `mutex_lock`이었다면 `__might_resched()`의 `rcu_sleep_check()`와 `RCU nest depth: 1, expected: 0` 줄이 함께 나왔을 것이다(이 조합은 실행하지 않았다).
- 같은 실행의 `hardirq` 줄이 `mm=user [insmod/73 cpu0]`이다. §6 (2)에서 말한 "hardirq의 `current`는 아무나"의 실례.

### 10.5 로그 메시지 → 의미 빠른 표

| 메시지 | 누가 내나 | 뜻 | 먼저 볼 것 |
|---|---|---|---|
| `BUG: sleeping function called from invalid context at 파일:줄` | `__might_resched` (`DEBUG_ATOMIC_SLEEP`) | 잠들 수 있는 함수를 atomic에서 불렀다 (아직 안 잠들었을 수도) | `preempt_count:` 값을 칸별로 분해, `irqs_disabled():` |
| `BUG: scheduling while atomic: 이름/PID/0x…` | `schedule_debug` (항상) | 실제로 `schedule()`에 들어갔다 | 끝의 hex = `preempt_count` (보통 +1 된 값) |
| `[ BUG: Invalid wait context ]` | lockdep | 문맥이 허용하지 않는 종류의 락 (mutex in softirq 등) | `context-{a:b}`와 락의 `{x:y}` |
| `inconsistent {HARDIRQ-ON-W} -> {IN-HARDIRQ-W} usage` | lockdep | 같은 락을 IRQ 켠 채로도, hardirq 안에서도 잡는다 → 교착 가능 | 그 락을 process 쪽에서 `_irqsave`로 잡고 있는지 |
| `Voluntary context switch within RCU read-side critical section!` | RCU | RCU 읽기 구간에서 잠들었다 | `rcu_read_lock` 범위, SRCU 필요 여부 |
| `BUG: spinlock wrong CPU` / `preempt_count: ffffffff` | spinlock debug / might_sleep | 앞선 버그의 2차 피해 | **무시하고 첫 BUG로 올라간다** |

---

## 11. 결정표 — "Y 컨텍스트에서 X 해도 되나"

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
<text x="219.5" y="30" text-anchor="middle" font-size="12" font-weight="bold">process</text>
<text x="219.5" y="47" text-anchor="middle" font-size="12">ioctl·probe</text>
<text x="302.5" y="30" text-anchor="middle" font-size="12" font-weight="bold">kthread·wq</text>
<text x="302.5" y="47" text-anchor="middle" font-size="12">irq 스레드</text>
<text x="385.5" y="30" text-anchor="middle" font-size="12" font-weight="bold">spinlock</text>
<text x="385.5" y="47" text-anchor="middle" font-size="12">잡은 채</text>
<text x="468.5" y="30" text-anchor="middle" font-size="12" font-weight="bold">softirq</text>
<text x="468.5" y="47" text-anchor="middle" font-size="12">timer 등</text>
<text x="551.5" y="30" text-anchor="middle" font-size="12" font-weight="bold">hardirq</text>
<text x="551.5" y="47" text-anchor="middle" font-size="12">top half</text>
<text x="634.5" y="30" text-anchor="middle" font-size="12" font-weight="bold">NMI</text>
<text x="634.5" y="47" text-anchor="middle" font-size="12">(perf 등)</text>
<text x="10" y="81" text-anchor="start" font-size="12">msleep · wait_event</text>
<rect x="182" y="62" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="219.5" y="81" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="265" y="62" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="302.5" y="81" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="348" y="62" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="385.5" y="81" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="431" y="62" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="468.5" y="81" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="514" y="62" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="551.5" y="81" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="597" y="62" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="634.5" y="81" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<text x="10" y="115" text-anchor="start" font-size="12">mutex_lock</text>
<rect x="182" y="96" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="219.5" y="115" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="265" y="96" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="302.5" y="115" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="348" y="96" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="385.5" y="115" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="431" y="96" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="468.5" y="115" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="514" y="96" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="551.5" y="115" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="597" y="96" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="634.5" y="115" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<text x="10" y="149" text-anchor="start" font-size="12">kmalloc(GFP_KERNEL)</text>
<rect x="182" y="130" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="219.5" y="149" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="265" y="130" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="302.5" y="149" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="348" y="130" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="385.5" y="149" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="431" y="130" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="468.5" y="149" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="514" y="130" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="551.5" y="149" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="597" y="130" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="634.5" y="149" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<text x="10" y="183" text-anchor="start" font-size="12">kmalloc(GFP_ATOMIC)</text>
<rect x="182" y="164" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="219.5" y="183" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="265" y="164" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="302.5" y="183" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="348" y="164" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="385.5" y="183" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="431" y="164" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="468.5" y="183" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="514" y="164" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="551.5" y="183" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="597" y="164" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="634.5" y="183" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<text x="10" y="217" text-anchor="start" font-size="12">spin_lock</text>
<rect x="182" y="198" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="219.5" y="217" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="265" y="198" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="302.5" y="217" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="348" y="198" width="75" height="28" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="385.5" y="217" text-anchor="middle" font-size="13" font-weight="bold">△</text>
<rect x="431" y="198" width="75" height="28" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="468.5" y="217" text-anchor="middle" font-size="13" font-weight="bold">△</text>
<rect x="514" y="198" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="551.5" y="217" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="597" y="198" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="634.5" y="217" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<text x="10" y="251" text-anchor="start" font-size="12">spin_lock_irqsave</text>
<rect x="182" y="232" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="219.5" y="251" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="265" y="232" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="302.5" y="251" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="348" y="232" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="385.5" y="251" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="431" y="232" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="468.5" y="251" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="514" y="232" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="551.5" y="251" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="597" y="232" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="634.5" y="251" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<text x="10" y="285" text-anchor="start" font-size="12">copy_to_user</text>
<rect x="182" y="266" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="219.5" y="285" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="265" y="266" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="302.5" y="285" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="348" y="266" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="385.5" y="285" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="431" y="266" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="468.5" y="285" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="514" y="266" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="551.5" y="285" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<rect x="597" y="266" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="634.5" y="285" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<text x="10" y="319" text-anchor="start" font-size="12">printk · pr_info</text>
<rect x="182" y="300" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="219.5" y="319" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="265" y="300" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="302.5" y="319" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="348" y="300" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="385.5" y="319" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="431" y="300" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="468.5" y="319" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="514" y="300" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="551.5" y="319" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="597" y="300" width="75" height="28" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="634.5" y="319" text-anchor="middle" font-size="13" font-weight="bold">△</text>
<text x="10" y="353" text-anchor="start" font-size="12">wake_up · queue_work</text>
<rect x="182" y="334" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="219.5" y="353" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="265" y="334" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="302.5" y="353" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="348" y="334" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="385.5" y="353" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="431" y="334" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="468.5" y="353" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="514" y="334" width="75" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="551.5" y="353" text-anchor="middle" font-size="13" font-weight="bold">O</text>
<rect x="597" y="334" width="75" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="634.5" y="353" text-anchor="middle" font-size="13" font-weight="bold">X</text>
<text x="10" y="392" text-anchor="start" font-size="12">O 가능   △ 조건부 (아래 결정표의 메모 참고)   X 금지</text>
<text x="10" y="414" text-anchor="start" font-size="12">X의 이유: 잠들 수 있다 (mutex · msleep · GFP_KERNEL · page fault), 또는 그 문맥에 없는 것을 쓴다</text>
<text x="10" y="436" text-anchor="start" font-size="12">(copy_to_user는 current의 유저 주소 공간이 '내 호출자'일 때만 의미가 있다)</text>
</svg>
```

그림 4 — API × 컨텍스트 행렬. "spinlock 잡은 채" 열은 process 문맥이라도 스핀락(또는 `preempt_disable`, `local_irq_disable`, RCU 읽기)을 쥔 구간이다.

같은 내용을 메모와 함께 표로:

| 하고 싶은 것 | process (syscall·probe) | kthread · workqueue · irq 스레드 | spinlock 등 쥔 구간 | softirq (timer·tasklet) | hardirq | NMI |
|---|---|---|---|---|---|---|
| 잠들기 (`msleep`, `wait_event`) | O | O | X | X | X | X |
| `mutex_lock` | O | O | X | X (lockdep `Invalid wait context`) | X | X |
| `kmalloc(GFP_KERNEL)` | O | O | X | X | X | X |
| `kmalloc(GFP_ATOMIC)` | O (불필요하게 비관적) | O | O | O | O | X |
| `spin_lock` | O | O | △ 락 순서 규칙 지킬 것 | △ 같은 데이터를 hardirq도 만지면 `_irqsave` | O | X (`raw_spin_trylock` 정도만) |
| `spin_lock_bh` | O | O | △ | 불필요 (이미 BH 문맥) | X (hardirq에서 BH enable 금지) | X |
| `spin_lock_irqsave` | O | O | O | O | O | X |
| `copy_to_user`/`copy_from_user` | O (호출한 프로세스의 메모리) | X (`mm` 없음) | X (fault 시 잠듦) | X | X | X |
| `printk`/`pr_info`/`dev_info` | O | O | O | O | O | △ 저장만 되고 출력은 나중 |
| `wake_up`, `complete`, `queue_work`, `tasklet_schedule` | O | O | O | O | O | X (`irq_work`로 우회) |
| `udelay`(짧게) | O (가능하면 `usleep_range`) | O | O | O | O (수 us 이내로) | O |

(△ 칸의 조건은 F4에서 자세히 다룬다. NMI 열은 소스·문서 기반이고 실행하지 않았다.)

외우는 요령: **잠드는 것(행 1~3, `copy_*_user`)은 맨 왼쪽 두 열에서만.** 깨우는 것·`GFP_ATOMIC`·`_irqsave` 락은 NMI만 빼고 어디서나.

---

## 12. 커널 안에서는 무슨 일이 일어나나

hardirq 하나가 들어와 softirq까지 처리되는 경로를 `preempt_count` 변화와 함께 따라가면 이렇다. 함수 이름은 랩 소스(6.18.55)의 `arch/arm64/kernel/entry-common.c`, `kernel/softirq.c`, `drivers/irqchip/irq-gic.c`, `kernel/irq/`에서 확인했고, §8 ftrace·§10.1 콜 트레이스에 실제로 찍힌 이름들이다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="210" y="20" text-anchor="middle" font-size="13" font-weight="bold">호출 경로 (arm64, 6.18)</text>
<text x="545" y="20" text-anchor="middle" font-size="13" font-weight="bold">preempt_count · 상태</text>
<rect x="20" y="34" width="380" height="28" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="210" y="53" text-anchor="middle" font-size="12">el1h_64_irq (예외 벡터, entry.S)</text>
<text x="420" y="53" text-anchor="start" font-size="12">가로챈 코드의 pc 그대로</text>
<line x1="210" y1="62" x2="210" y2="68" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="20" y="70" width="380" height="28" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="210" y="89" text-anchor="middle" font-size="12">el1h_64_irq_handler → el1_interrupt</text>
<text x="420" y="89" text-anchor="start" font-size="12">DAIF로 IRQ 마스크</text>
<line x1="210" y1="98" x2="210" y2="104" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="20" y="106" width="380" height="28" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="210" y="125" text-anchor="middle" font-size="12">__el1_irq: irq_enter_rcu()</text>
<text x="420" y="125" text-anchor="start" font-size="12">pc += 0x10000 (HARDIRQ)</text>
<line x1="210" y1="134" x2="210" y2="140" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="20" y="142" width="380" height="28" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="210" y="161" text-anchor="middle" font-size="12">do_interrupt_handler → call_on_irq_stack</text>
<text x="420" y="161" text-anchor="start" font-size="12">CPU별 IRQ 스택으로 전환</text>
<line x1="210" y1="170" x2="210" y2="176" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="20" y="178" width="380" height="28" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="210" y="197" text-anchor="middle" font-size="12">gic_handle_irq → handle_fasteoi_irq</text>
<text x="420" y="197" text-anchor="start" font-size="12">IAR 읽기, 루프로 여러 IRQ</text>
<line x1="210" y1="206" x2="210" y2="212" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="20" y="214" width="380" height="28" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="210" y="233" text-anchor="middle" font-size="12">handle_irq_event → f2_hard()</text>
<text x="420" y="233" text-anchor="start" font-size="12">in_hardirq()=1, irqs_disabled()=1</text>
<line x1="210" y1="242" x2="210" y2="248" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="20" y="250" width="380" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="210" y="269" text-anchor="middle" font-size="12">irq_exit_rcu()</text>
<text x="420" y="269" text-anchor="start" font-size="12">pc -= 0x10000</text>
<line x1="210" y1="278" x2="210" y2="284" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="20" y="286" width="380" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="210" y="305" text-anchor="middle" font-size="12">invoke_softirq → handle_softirqs</text>
<text x="420" y="305" text-anchor="start" font-size="12">pc += 0x100, IRQ 다시 켬</text>
<line x1="210" y1="314" x2="210" y2="320" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="20" y="322" width="380" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="210" y="341" text-anchor="middle" font-size="12">tasklet_action → f2_tl_fn()</text>
<text x="420" y="341" text-anchor="start" font-size="12">in_serving_softirq()=1</text>
<line x1="210" y1="350" x2="210" y2="356" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="20" y="358" width="380" height="28" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="210" y="377" text-anchor="middle" font-size="12">exit_to_kernel_mode</text>
<text x="420" y="377" text-anchor="start" font-size="12">pc==0이고 need_resched면 선점</text>
<text x="20" y="412" text-anchor="start" font-size="12">irq_exit_rcu에서 in_interrupt()가 참이면(softirq·BH 금지 구간을 가로챈 경우) softirq를 미룬다.</text>
<text x="20" y="434" text-anchor="start" font-size="12">handle_softirqs가 2 ms 또는 10회 재시작을 넘기거나 need_resched면 나머지는 ksoftirqd/N로 넘긴다.</text>
<text x="20" y="456" text-anchor="start" font-size="12">process 컨텍스트에서 raise된 softirq는 ksoftirqd를 깨우거나 local_bh_enable() 때 처리된다.</text>
</svg>
```

그림 5 — arm64에서 EL1(커널 실행 중)에 IRQ가 들어왔을 때의 경로. 유저 모드(EL0)를 가로챘다면 `el0t_64_irq` → `el0_interrupt`로 들어오고 나머지는 같다.

핵심 함수 세 개를 소스로 확인해 두자. `kernel/softirq.c`의 `__irq_exit_rcu()`:

```c
static inline void __irq_exit_rcu(void)
{
#ifndef __ARCH_IRQ_EXIT_IRQS_DISABLED
	local_irq_disable();
#else
	lockdep_assert_irqs_disabled();
#endif
	account_hardirq_exit(current);
	preempt_count_sub(HARDIRQ_OFFSET);
	if (!in_interrupt() && local_softirq_pending())
		invoke_softirq();
	...
```

hardirq 칸을 먼저 내리고, **그래도 `in_interrupt()`가 거짓일 때만**(즉 가로챈 코드가 softirq 처리 중이거나 BH 금지 중이 아닐 때만) softirq를 돈다. §8의 "RCU softirq 안에 끼어든 타이머 IRQ가 softirq를 다시 돌리지 않은" 장면이 이 `if`다.

드라이버 작성 때 이 지식이 다시 나오는 곳:

- **F3 (잠자기와 깨우기)**: `wait_event_interruptible()`은 process 쪽, `wake_up()`은 IRQ 쪽 — §4.6의 `complete()` 패턴의 일반형.
- **F4 (락)**: "데이터를 누가 만지나"(process만? softirq도? hardirq도?)에 따라 `spin_lock` / `spin_lock_bh` / `spin_lock_irqsave`를 고르는 규칙이 이 노트의 컨텍스트 구분에서 바로 나온다.
- **F6 (인터럽트 처리)**: `request_threaded_irq`, `IRQF_ONESHOT`, IRQ affinity.
- **F7 (지연 작업)**: softirq·tasklet·BH workqueue·workqueue·kthread 중 무엇을 고를지 = "잠들어야 하나"가 첫 질문.
- **F8 (타이머)**: `timer_list`는 softirq, `hrtimer`는 모드에 따라 hardirq/softirq.
- **F9 (메모리)**: `GFP_KERNEL`/`GFP_ATOMIC`/`GFP_NOWAIT` 선택.
- **H 모듈 (I2C/SPI 드라이버)**: 버스 전송은 잠드므로 IRQ에서 바로 못 한다 → threaded IRQ.

---

## 13. Embedded 실무에서 다시 보기

### 13.1 장면 1 — IMU의 data-ready IRQ에서 I2C로 FIFO 읽기

예를 들어 센서 키트의 IMU(BMI270 같은 I2C 6축 센서)가 1.6 kHz로 data-ready 핀을 올린다. 첫 구현에서 흔히 `request_irq()` 핸들러 안에서 `i2c_smbus_read_i2c_block_data()`를 부른다. `i2c_transfer()`는 버스 완료를 `wait_for_completion`으로 기다리므로 **hardirq에서 잠드는** 버그다. 디버그 커널이면 §10.2와 같은 "sleeping function called" + `preempt_count: 10001`이 바로 찍힌다. 고친 모양(개념 스케치 — 이 노트에서 컴파일·실행하지 않았다):

```c
/* hardirq는 타임스탬프만, 버스 접근은 irq 스레드에서 */
static irqreturn_t imu_hard(int irq, void *p)
{
	struct imu *st = p;

	st->ts = ktime_get_boottime_ns();	/* IRQ 시각은 여기서 잡아야 정확 */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t imu_thread(int irq, void *p)
{
	struct imu *st = p;
	u8 buf[12];

	mutex_lock(&st->lock);			/* process context: OK */
	if (i2c_smbus_read_i2c_block_data(st->client, 0x0c, sizeof(buf), buf) == sizeof(buf))
		imu_push(st, buf, st->ts);
	mutex_unlock(&st->lock);
	return IRQ_HANDLED;
}
/* probe: devm_request_threaded_irq(dev, irq, imu_hard, imu_thread, IRQF_ONESHOT, "imu", st); */
```

IIO 서브시스템에서는 이 패턴이 `iio_triggered_buffer_setup()`의 "pollfunc"(top half는 `iio_pollfunc_store_time`, bottom half는 스레드)로 표준화되어 있다(I2).

### 13.2 장면 2 — CAN 모터 컨트롤러 제어 루프를 1 kHz hrtimer로

hrtimer 콜백(기본 hardirq)에서 SPI CAN 컨트롤러(MCP2518FD 같은)로 프레임을 보내고 싶다. `spi_sync()`는 잠들므로 불가. 선택지: (1) 콜백에서는 `wake_up_process()`로 SCHED_FIFO kthread를 깨우고 kthread가 `spi_sync()`, (2) `spi_async()`로 비동기 전송(완료 콜백도 atomic 문맥), (3) 아예 kthread에서 `hrtimer_sleeper`/`clock_nanosleep` 기반 주기 루프. 실무에서는 (1)이나 (3)이 지터·디버깅 면에서 낫다(M5·N1의 지연 측정과 연결).

### 13.3 장면 3 — 현장 로그의 `preempt_count`로 원인 범위 좁히기

고객 장비의 `dmesg`에 이런 줄이 있다고 하자: `in_atomic(): 1, irqs_disabled(): 0 … preempt_count: 201`. 그림 2로 분해하면 SOFTIRQ 칸 2 = **BH 금지**, PREEMPT 칸 1 = 스핀락 하나. 즉 "`spin_lock_bh()`를 잡은 process 코드에서 잠들 수 있는 함수를 불렀다"까지 콜 트레이스를 보기 전에 좁혀진다. `preempt_count: 10000`에 `irqs_disabled(): 1`이면 hardirq 핸들러, `100`이면 ksoftirqd에서 돈 softirq/tasklet이다. 이 해석을 30초 안에 하는 것이 Embedded Linux 엔지니어의 기본기다.

### 13.4 장면 4 — PREEMPT_RT 커널로 옮겼더니 드라이버가 깨졌다

로봇 제어용으로 PREEMPT_RT 커널을 쓰면: `spinlock_t`가 잠드는 rt_mutex로 바뀌고, hardirq 핸들러가 기본으로 스레드화되며(§7.2와 비슷), non-HARD hrtimer가 softirq로 옮겨진다(§6 (4)). 그래서 "진짜 hardirq에서 돌아야 하는" 코드(IRQ 컨트롤러 드라이버, `IRQF_NO_THREAD` 핸들러, `HRTIMER_MODE_HARD` 콜백)에서 `spinlock_t`를 잡으면 RT에서는 "잠드는 락을 hardirq에서 잡는" 버그가 된다. 그런 곳은 `raw_spinlock_t`를 쓴다. 이 노트의 lockdep `Invalid wait context` 검사(`{3:3}`의 CONFIG 등급)가 바로 RT에서 깨질 코드를 non-RT 커널에서 미리 잡아 주는 장치다. (이 노트의 랩에는 RT 커널이 없어 실행하지 않았다.)

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| IRQ 핸들러에서 I2C/SPI 동기 전송 | 디버그 커널: "sleeping function called", `preempt_count: 10001`. 일반 커널: 가끔 "scheduling while atomic"·멈춤 | 버스 전송은 완료를 기다리며 잠듦 | threaded IRQ (`request_threaded_irq` + `IRQF_ONESHOT`) |
| timer 콜백에서 `mutex_lock` | "sleeping function called", `preempt_count: 101` 또는 `100`, lockdep `Invalid wait context` | timer 콜백은 softirq | `spin_lock_bh`로 바꾸거나 일을 workqueue로 |
| hardirq에서 `GFP_KERNEL` | 평소엔 정상, 메모리 부족 때만 크래시. lockdep `fs_reclaim` inconsistent | 할당이 회수로 들어가면 잠듦 | `GFP_ATOMIC` + NULL 처리, 또는 probe에서 미리 할당 |
| 스핀락 안에서 `msleep`·`copy_from_user` | "scheduling while atomic", 2차로 spinlock wrong CPU·카운트 언더플로·hang | 스핀락은 선점을 끈다 | 락 밖에서 복사·대기 후 짧게 락, 또는 mutex로 |
| `in_atomic()`/`in_interrupt()`로 gfp 고르기 | IRQ만 끈 구간·RCU 구간에서 `GFP_KERNEL`이 선택됨 | 두 매크로의 구멍 (§5) | 호출자가 gfp를 인자로 넘김 |
| `rcu_read_lock` 안에서 잠드는 함수 호출 | `Voluntary context switch within RCU read-side critical section!`, non-PREEMPT_RCU 커널에선 "scheduling while atomic" | RCU 읽기 구간은 atomic | 구간 밖으로 빼거나 SRCU |
| hardirq에서 `current`나 `current->mm` 사용 | 엉뚱한 프로세스 이름 로그, 엉뚱한 메모리에 쓰기 | `current`는 가로채인 태스크 | 필요한 정보는 process 문맥에서 미리 저장 |
| process와 hardirq가 공유하는 데이터를 process에서 `spin_lock`만으로 보호 | 드물게 완전 교착 (lockdep: `{HARDIRQ-ON-W} -> {IN-HARDIRQ-W}`) | 락을 쥔 사이 같은 CPU에서 IRQ가 와서 같은 락을 기다림 | process 쪽은 `spin_lock_irqsave` |
| 첫 BUG 뒤에 나온 메시지를 원인으로 오인 | 엉뚱한 곳(완료 대기, IRQ 처리)을 고치게 됨 | 2차 피해 (§10.3) | 로그는 위에서부터, 첫 BUG만 |
| 양산 커널에서만 테스트 | 잠복 버그가 현장에서 터짐 | `DEBUG_ATOMIC_SLEEP`·lockdep 꺼짐 | 개발·CI 커널에 둘 다 켜기 |

---

## 15. 면접에서 이렇게 말한다

**Q.** "Why can't you sleep in an interrupt handler?"

**A.** 핸들러는 태스크가 아니라서 재울 대상이 없다. `current`는 가로채인 남의 태스크이고, 핸들러는 CPU당 하나뿐인 IRQ 스택 위에서 IRQ가 꺼진 채 돈다. 잠들면 무고한 태스크가 멈추고, 스택은 덮어써지고, 그 CPU는 타이머조차 못 받는다. 가로챈 코드가 쥔 락을 기다리면 교착이다. 그래서 잠들어야 하는 일은 threaded IRQ나 workqueue로 넘긴다.

> An interrupt handler doesn't run on behalf of any task — `current` is just whatever task got interrupted, and the handler runs on the per-CPU IRQ stack with interrupts disabled. If it called schedule(), you'd put an unrelated task to sleep, leave the IRQ stack in use while another interrupt could reuse it, and stall that CPU's timer tick; and if the interrupted code held a lock you need, you'd deadlock. The kernel enforces this with the hardirq bits in preempt_count — you get "scheduling while atomic". So anything that can block, like an I2C transfer, goes to a threaded IRQ handler or a workqueue.

**꼬리 질문**: "softirq에서는요?" → 같은 이유로 안 된다. ksoftirqd에서 돌 때도 softirq 칸이 서 있어 마찬가지.

**Q.** "What's the difference between process context and interrupt context?"

**A.** process context는 어떤 태스크를 대신해 도는 코드라 `current`가 의미 있고, 잠들 수 있고, 유저 프로세스라면 그 메모리에 접근할 수 있다. interrupt context(hardirq·softirq·NMI)는 비동기로 끼어든 코드라 셋 다 불가다. 판별은 `preempt_count`의 hardirq·softirq·NMI 칸으로 하고 `in_task()`가 그 요약이다.

> Process context means the kernel is executing on behalf of a specific task — a syscall, a probe, a kernel thread — so `current` is meaningful, you can sleep, and for user tasks you can access user memory. Interrupt context — hardirq, softirq, NMI — is asynchronous: `current` is incidental, you can't sleep, and you can't touch user memory. The kernel tracks it in the preempt_count bit fields, and in_task() summarizes it.

**Q.** "What is atomic context? Is holding a spinlock in process context atomic?"

**A.** 잠들면 안 되는 모든 구간이다. interrupt context 전부 + process context에서 스핀락을 쥐었거나, `preempt_disable`, `local_bh_disable`, `local_irq_disable`, RCU 읽기 구간 안일 때. 그러니 그렇다, 스핀락을 쥔 process 코드는 atomic이다. 단 `in_atomic()`은 IRQ만 끈 구간과 PREEMPT_RCU의 RCU 구간을 못 본다.

> Atomic context is anywhere you must not sleep: all interrupt contexts, plus process context while holding a spinlock, with preemption or bottom halves disabled, with local interrupts disabled, or inside an RCU read-side section. So yes, a spinlock makes process context atomic — spin_lock() bumps preempt_count. One caveat: in_atomic() misses the irqs-disabled-only case and preemptible-RCU read sections, which is why drivers shouldn't use it to pick GFP flags.

**Q.** "Explain preempt_count."

**A.** 태스크별 32비트 카운터로, 칸이 넷이다: bit 0~7 선점 금지 깊이, 8~15 softirq(실행 중이면 0x100, BH 금지면 0x200씩), 16~19 hardirq, 20~23 NMI. 0이면 선점 가능. 예를 들어 `0x101`은 softirq 실행 중 + 스핀락 하나, `0x10001`은 hardirq 안. 경고 로그의 이 숫자만 봐도 문맥을 안다.

> It's a per-task counter split into fields: bits 0–7 count preempt-disable nesting, 8–15 softirq — 0x100 while serving a softirq, 0x200 per local_bh_disable — 16–19 hardirq, 20–23 NMI. Zero means preemptible. So "preempt_count: 101" in a splat means "inside a softirq with one more preempt-disable", and 0x10001 means hardirq. On arm64 it lives in thread_info and need_resched is folded into the upper 32 bits, inverted, so a single 64-bit compare decides whether to preempt.

**Q.** "When would you use a threaded IRQ instead of a regular one?"

**A.** 인터럽트 처리에 잠드는 일이 필요할 때 — I2C/SPI 센서처럼 원인 레지스터를 읽는 데 버스 전송이 필요한 경우가 대표적이다. hardirq 쪽은 타임스탬프만 잡고 `IRQ_WAKE_THREAD`, 스레드(SCHED_FIFO)에서 버스 전송. `IRQF_ONESHOT`으로 스레드가 끝날 때까지 줄을 마스크한다.

> Whenever handling the interrupt requires something that sleeps — the classic case is an I2C or SPI sensor where even reading the status register is a bus transaction. The hard handler just timestamps and returns IRQ_WAKE_THREAD; the thread_fn runs in an irq/NN kthread with SCHED_FIFO priority, in process context, so it can take mutexes and do i2c_transfer. IRQF_ONESHOT keeps the line masked until the thread finishes, which matters for level-triggered sources.

**Q.** "Where do softirqs run? What is ksoftirqd?"

**A.** 두 곳이다. 보통은 hardirq가 끝나는 `irq_exit` 길목에서, 가로챈 태스크 위에서 바로 돈다. process 문맥에서 raise됐거나, 한 번에 2 ms·10회를 넘기면 CPU별 `ksoftirqd/N` 스레드가 처리한다 — 네트워크 폭주 때 유저 태스크가 굶지 않게. 어디서 돌든 softirq 문맥이라 잠들 수 없다.

> Mostly on the way out of a hard interrupt — irq_exit checks pending bits and runs handle_softirqs right there, on top of whatever task was interrupted. If they're raised from process context, or if processing exceeds about 2 ms or 10 restarts, the rest is deferred to the per-CPU ksoftirqd thread so a packet flood can't starve user tasks. Either way it's softirq context: interrupts enabled, but no sleeping.

**Q.** "How would you find a sleep-in-atomic bug before it ships?"

**A.** 개발·CI 커널에 `CONFIG_DEBUG_ATOMIC_SLEEP`과 lockdep(`PROVE_LOCKING`)을 켠다. 그러면 실제로 잠들지 않은 잠복 버그(메모리 넉넉할 때의 `GFP_KERNEL` 등)도 `might_sleep()` 지점에서 잡힌다. 경고는 1초에 한 번, lockdep은 첫 보고 후 꺼지므로 하나씩 고치고 다시 돌린다.

> Run development and CI kernels with CONFIG_DEBUG_ATOMIC_SLEEP and lockdep. Then every might_sleep() annotation — in mutex_lock, GFP_KERNEL allocations, copy_from_user — checks preempt_count, irqs_disabled and RCU depth, so you catch the latent bugs that only actually sleep under memory pressure. Lockdep adds wait-context checks, like a mutex in a timer callback. Note it's rate-limited and lockdep disables itself after the first splat, so fix them one at a time.

**꼬리 질문**: "hrtimer 콜백은 어느 문맥이에요?" → non-RT에서 기본은 hardirq, `HRTIMER_MODE_*_SOFT`면 softirq. PREEMPT_RT에서는 `_HARD`를 명시하지 않으면 softirq로 옮겨진다.

---

## 16. 직접 해보기

1. **개념**: `preempt_count: 202`, `irqs_disabled(): 0`인 경고가 떴다. 어떤 상황인가? — 정답/힌트: SOFTIRQ 칸 2(BH 금지 한 번), PREEMPT 칸 2(스핀락 두 개 또는 스핀락 + preempt_disable). `spin_lock_bh` 안에서 또 `spin_lock`을 잡은 process 코드.
2. **개념**: `local_irq_disable()` 직후 `in_atomic()`은 무엇을 돌려주나? 그 상태에서 `mutex_lock`을 부르면 디버그 커널은 잡는가? — 정답/힌트: 0을 돌려준다(§5). 그래도 `__might_resched`의 `!irqs_disabled()` 조건 때문에 잡힌다. 실제로 `bug=5` 모드를 추가해 확인해 보라.
3. **코드**: `f2_ctx`에 `bug=5`(hrtimer HARD 콜백에서 `kmalloc(GFP_KERNEL)`)와 `bug=6`(`HRTIMER_MODE_REL_SOFT` 콜백에서 같은 일)을 추가하고, 두 경고의 `preempt_count`와 `irqs_disabled()` 값을 비교하라. — 정답/힌트: 예상은 `10001`(또는 `10000`)·1 대 `101`(또는 `100`)·0. 하위 칸은 가로챈 태스크에 따라 바뀐다.
4. **코드**: §10.1의 버그를 두 가지로 고쳐 보라: (a) timer 콜백에서 `schedule_work()`로 넘기고 work에서 mutex, (b) mutex를 spinlock으로 바꾸고 process 쪽은 `spin_lock_bh`. 둘 다 디버그 커널에서 경고 0개인지 확인. — 정답/힌트: (b)에서 process 쪽을 그냥 `spin_lock`으로 두면 lockdep이 `{SOFTIRQ-ON-W} -> {IN-SOFTIRQ-W}` 형태의 inconsistent 보고를 낼 것으로 예상된다(§10.2의 hardirq판과 같은 구조, 이 노트에서는 실행하지 않았다).
5. **관찰**: `threadirqs` 부팅에서 `cat /proc/softirqs`를 1초 간격으로 두 번 찍고, `ps`로 `ktimers/N`과 `ksoftirqd/N`이 있는지 본 뒤, 일반 부팅과 비교하라. — 정답/힌트: `ktimers/N`은 강제 스레드화 모드에서만 생긴다(§7.3).
6. **ftrace**: §8 스크립트에서 `set_ftrace_filter`에 `run_timer_softirq`와 `call_timer_fn`, 모듈의 `f2_timer_fn`을 넣어 timer 콜백이 `irq_exit_rcu` 아래에서 도는지 `ksoftirqd` 아래에서 도는지 확인하라. — 정답/힌트: 대부분 idle을 가로챈 `irq_exit_rcu` 아래(`<idle>-0`)로 찍힌다.
7. **읽기**: 랩 소스 `kernel/softirq.c`에서 `local_bh_enable()` → `__local_bh_enable_ip()`를 찾아 "BH를 다시 켤 때 대기 중인 softirq를 그 자리에서 처리한다"는 부분을 찾아라. softirq의 세 번째 실행 장소다. — 정답/힌트: 랩 소스 445행 `if (unlikely(!in_interrupt() && local_softirq_pending()))` 아래의 `do_softirq();`. 같은 함수 첫 줄의 `WARN_ON_ONCE(in_hardirq())`는 hardirq에서 `spin_unlock_bh`를 부르면 안 되는 이유이기도 하다.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| process context | 프로세스 문맥 | 어떤 태스크를 대신해 도는 커널 코드. `current`가 의미 있고 잠들 수 있다 |
| interrupt context | 인터럽트 문맥 | hardirq·softirq·NMI의 총칭. 잠들 수 없다 |
| hardirq | 하드 인터럽트 | `request_irq` 핸들러(top half). IRQ 꺼진 채 CPU별 IRQ 스택에서 실행 |
| softirq | 소프트 인터럽트 | 10개 고정 벡터(TIMER, NET_RX, TASKLET, RCU …)의 지연 처리. IRQ 켜진 채 실행 |
| bottom half (BH) | 하반부 | hardirq 뒤로 미룬 처리. 커널 API에서 "BH"는 보통 softirq를 뜻함(`local_bh_disable`) |
| ksoftirqd/N | softirq 스레드 | CPU별 커널 스레드. 넘친 softirq와 process에서 raise된 softirq를 처리 |
| tasklet | 태스크릿 | softirq 위의 동적 작업 단위. 퇴출 중이며 BH workqueue가 후계 |
| threaded IRQ | 스레드형 IRQ | `thread_fn`이 `irq/NN-이름` 커널 스레드(SCHED_FIFO)에서 도는 IRQ. process context |
| forced threading | 강제 스레드화 | `threadirqs`·PREEMPT_RT에서 일반 IRQ 핸들러를 스레드로 옮기는 것 |
| NMI / pseudo-NMI | 마스크 불가 인터럽트 | IRQ를 꺼도 들어옴. arm64는 GICv3 우선순위로 흉내 냄 |
| atomic context | 원자 문맥 | 잠들면 안 되는 모든 구간 (interrupt context + 락·금지 구간) |
| `preempt_count` | 선점 카운트 | 선점 금지 깊이·softirq·hardirq·NMI 칸을 담은 태스크별 카운터 |
| `current` | 현재 태스크 | 이 CPU에서 지금 도는 `task_struct`. interrupt context에서는 가로채인 태스크 |
| `might_sleep()` | 잠들 수도 있음 표시 | 잠들 수 있는 함수 입구의 주석. 디버그 커널에서 문맥을 검사 |
| `DEBUG_ATOMIC_SLEEP` | 디버그 옵션 | `might_sleep()` 검사를 켜는 Kconfig |
| lockdep | 락 검증기 | 락 순서·IRQ 안전성·대기 종류를 런타임에 검증 (`PROVE_LOCKING`) |
| grace period | 유예 기간 | 모든 CPU가 RCU 읽기 구간을 한 번 벗어날 때까지의 시간 |
| `GFP_ATOMIC` | 원자 할당 플래그 | 잠들지 않는 할당. 비상 예비분을 쓰며 실패할 수 있다 |

---

## 18. 요약 & 체크리스트

커널 코드는 네 문맥 중 하나에서 돈다: 태스크를 대신하는 **process**(잠들 수 있음), IRQ 핸들러인 **hardirq**, 그 뒤로 미룬 **softirq**(timer·tasklet·NAPI), 그리고 **NMI**. 이 상태는 태스크별 `preempt_count`의 칸(PREEMPT 0~7, SOFTIRQ 8~15, HARDIRQ 16~19, NMI 20~23)에 기록되고, `in_task()`·`in_hardirq()`·`in_serving_softirq()`가 그 칸을 읽는다. process 문맥이라도 스핀락·`preempt_disable`·`local_bh_disable`·`local_irq_disable`·RCU 읽기 구간 안이면 **atomic**이라 잠들 수 없다 — 그리고 IRQ만 끈 구간과 PREEMPT_RCU의 RCU 구간은 `preempt_count`에 안 보인다. 잠든다는 것은 `schedule()`에 닿는 것이고, mutex·`msleep`·`wait_event`·`GFP_KERNEL`·`copy_*_user`·I2C/SPI 동기 전송이 그렇다. 잠들어야 하는 일은 threaded IRQ·workqueue·kthread로 넘기고, atomic 쪽은 `GFP_ATOMIC`·spinlock 계열·`wake_up`/`complete`/`queue_work`만 쓴다. 개발 커널에는 `DEBUG_ATOMIC_SLEEP`과 lockdep을 켜서 잠복 버그를 `might_sleep()` 지점에서 잡고, 로그의 `preempt_count` 숫자를 칸별로 풀어 문맥을 읽는다.

- [ ] process / hardirq / softirq / NMI 문맥을 `current`, IRQ 상태, 스택, 잠들기 가능 여부로 구분해 설명할 수 있다
- [ ] "왜 IRQ 핸들러에서 잠들 수 없나"를 다섯 가지 이유로 영어로 답할 수 있다
- [ ] `preempt_count`의 네 칸과 마스크를 그리고, `0x101`·`0x201`·`0x10001`을 분해할 수 있다
- [ ] `in_serving_softirq()`와 `in_softirq()`, `in_task()`와 `in_interrupt()`의 차이를 BH 금지 예로 설명할 수 있다
- [ ] `in_atomic()`이 놓치는 두 경우를 말하고, 왜 드라이버가 그걸로 gfp를 고르면 안 되는지 설명할 수 있다
- [ ] softirq가 `irq_exit`·`ksoftirqd`·`local_bh_enable`에서 실행된다는 것과 각각의 조건을 설명할 수 있다
- [ ] hrtimer 기본 모드가 hardirq이고 `_SOFT`·PREEMPT_RT에서 바뀐다는 것을 설명할 수 있다
- [ ] threaded IRQ를 `request_threaded_irq` + `IRQF_ONESHOT`으로 짜고, primary/thread 각각에서 할 일을 나눌 수 있다
- [ ] "sleeping function called from invalid context"와 "scheduling while atomic"의 차이를 말하고, 로그에서 원인 줄을 찾을 수 있다
- [ ] lockdep의 `Invalid wait context`와 `{HARDIRQ-ON-W} -> {IN-HARDIRQ-W}` 보고가 무엇을 뜻하는지 읽을 수 있다
- [ ] 결정표(그림 4)를 보지 않고 mutex·`GFP_*`·spinlock 변형·`copy_to_user`가 어느 문맥에서 되는지 말할 수 있다
- [ ] ftrace `function_graph` + `irq` 이벤트로 hardirq → softirq 중첩을 직접 보여 줄 수 있다

## 참고 자료

- docs.kernel.org — Unreliable Guide To Hacking The Linux Kernel (문맥별 규칙의 원조 문서): https://docs.kernel.org/kernel-hacking/hacking.html
- docs.kernel.org — Unreliable Guide To Locking (문맥별 락 선택): https://docs.kernel.org/kernel-hacking/locking.html
- docs.kernel.org — Workqueue (BH workqueue 포함): https://docs.kernel.org/core-api/workqueue.html
- docs.kernel.org — ftrace: https://docs.kernel.org/trace/ftrace.html
- docs.kernel.org — Runtime locking correctness validator (lockdep): https://docs.kernel.org/locking/lockdep-design.html
- docs.kernel.org — What is RCU?: https://docs.kernel.org/RCU/whatisRCU.html
- 랩 소스 (Linux 6.18.55, `/var/lab/src/linux-6.18.55`): `include/linux/preempt.h`, `arch/arm64/include/asm/preempt.h`, `include/linux/hardirq.h`, `kernel/softirq.c`(`__irq_exit_rcu`, `handle_softirqs`, `invoke_softirq`), `kernel/sched/core.c`(`__might_resched`), `kernel/irq/manage.c`(`irq_setup_forced_threading`, `irq_forced_thread_fn`), `kernel/time/hrtimer.c`(`__hrtimer_setup`), `arch/arm64/kernel/entry-common.c`(`el1_interrupt`)
- elixir.bootlin.com에서 같은 파일 보기: https://elixir.bootlin.com/linux/latest/source/include/linux/preempt.h
- Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman, "Linux Device Drivers, 3rd ed." 10장 Interrupt Handling, 7장 Time, Delays, and Deferred Work (API는 오래됐지만 문맥 개념은 그대로)
- Robert Love, "Linux Kernel Development, 3rd ed." 7장 Interrupts and Interrupt Handlers, 8장 Bottom Halves and Deferring Work
- 이 스터디의 E4(§5 atomic context 첫 소개), D4(GIC SPI 번호와 pending 주입), F4·F6·F7·F8·F9 노트
