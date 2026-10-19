# N1. Linux는 실시간인가 — soft vs hard RT, 지연의 출처, PREEMPT_RT가 실제로 바꾸는 것

> **이 노트를 다 읽으면**: latency·jitter·WCET·hard/firm/soft RT를 RTOS 용어로 정확히 구분할 수 있다 · IRQ부터 유저 태스크까지의 지연 사슬과 각 단계에 끼어드는 지연 원인(IRQ off, preempt off, softirq, 역전, page fault, 캐시, cpuidle, SMI/EL3)을 그림으로 설명할 수 있다 · PREEMPT_RT가 락·IRQ·softirq·printk를 어떻게 바꾸는지(그리고 무엇은 못 바꾸는지) 6.18 소스와 QEMU 실험으로 보여 줄 수 있다 · "우리 1 kHz 루프가 가끔 놓쳐요"에 순서 있게 답할 수 있다
> **왜 필요한가**: 로보틱스·드론·국방 회사 Embedded Linux 면접의 단골 질문이 "Is Linux real-time?"이다. "아니요"도 "예"도 틀린 답이고, "무엇을 보장하고 무엇을 측정해야 하는지"로 답해야 통과한다.
> **Don 기준 난이도**: ISR latency, critical section, priority inversion, WCET는 이미 RTOS·SSD 펌웨어에서 안다 / Linux의 preemption 모델, PREEMPT_RT의 락 변환, threaded IRQ, mlockall, RT throttling, "측정은 왜 부하 상태에서 해야 하나"를 새로 배운다
> **선행 노트**: F1(스케줄러·preemption 모델·RT throttling), F2(실행 문맥), F4(락), F6(인터럽트), F8(타이머). 이어지는 노트: N2(튜닝), N3(측정 도구), N4(Linux vs RTOS vs MCU 분담), L8(cgroup과 RT)

---

## 0. 큰 그림 — "실시간"은 빠르다는 뜻이 아니다

Don이 FreeRTOS로 모터 제어 태스크를 짰다면 이렇게 생각했을 것이다: "이 ISR은 최악 몇 사이클, 이 태스크는 우선순위 최상위, 다른 태스크가 잡는 critical section은 최대 몇 µs — 그러니 최악 지연은 X µs 이하다." 이 문장의 핵심은 **최악(worst case)** 과 **이하(bound)** 다. 평균이 얼마나 빠른지는 관심 밖이다.

실시간(real-time) 시스템의 정의도 같다: **결과가 맞는 것만이 아니라 "제때" 나와야 맞는 시스템**. 그래서 질문은 "Linux가 빠르냐"가 아니라 "Linux에서 내 태스크가 깨어나기까지 걸리는 시간에 **상한이 있느냐, 그 상한이 내 deadline보다 작으냐**"다.

```svg
<svg viewBox="0 0 660 270" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="110" y="22" text-anchor="middle" font-size="14">hard RT</text>
  <text x="330" y="22" text-anchor="middle" font-size="14">firm RT</text>
  <text x="550" y="22" text-anchor="middle" font-size="14">soft RT</text>
  <g stroke="currentColor" stroke-width="1.2" fill="none">
    <line x1="20" y1="170" x2="205" y2="170" marker-end="url(#ah1)"/><line x1="20" y1="175" x2="20" y2="40" marker-end="url(#ah1)"/>
    <line x1="240" y1="170" x2="425" y2="170" marker-end="url(#ah1)"/><line x1="240" y1="175" x2="240" y2="40" marker-end="url(#ah1)"/>
    <line x1="460" y1="170" x2="645" y2="170" marker-end="url(#ah1)"/><line x1="460" y1="175" x2="460" y2="40" marker-end="url(#ah1)"/>
  </g>
  <g stroke="#888" stroke-dasharray="4 3"><line x1="110" y1="45" x2="110" y2="235"/><line x1="330" y1="45" x2="330" y2="170"/><line x1="550" y1="45" x2="550" y2="170"/></g>
  <polyline points="20,70 110,70 110,230 200,230" fill="none" stroke="#d0564a" stroke-width="2.5"/>
  <polyline points="240,70 330,70 330,170 420,170" fill="none" stroke="#e08a3c" stroke-width="2.5"/>
  <path d="M460,70 L550,70 C580,100 600,150 640,165" fill="none" stroke="#3f9a6b" stroke-width="2.5"/>
  <text x="110" y="187" text-anchor="middle" font-size="12">deadline</text>
  <text x="330" y="187" text-anchor="middle" font-size="12">deadline</text>
  <text x="550" y="187" text-anchor="middle" font-size="12">deadline</text>
  <text x="122" y="222" font-size="12">음수 = 사고</text>
  <text x="342" y="160" font-size="12">0 = 버림</text>
  <text x="560" y="110" font-size="12">점점 감소</text>
  <text x="28" y="62" font-size="12">결과의 가치</text>
  <text x="20" y="255" font-size="12">예: 모터 전류 루프, 에어백</text>
  <text x="240" y="255" font-size="12">예: 영상 프레임, 레이더 스캔</text>
  <text x="460" y="255" font-size="12">예: UI, 로그, 텔레메트리</text>
</svg>
```

그림 1 — deadline을 넘긴 결과의 가치. hard RT는 늦으면 사고(음수 가치), firm RT는 늦은 결과를 버린다(0), soft RT는 늦을수록 품질이 떨어진다.

일반 Linux(PREEMPT_NONE/VOLUNTARY 서버·데스크톱 커널)는 **처리량(throughput)** 을 위해 설계된 OS다. 평균은 매우 빠르지만, 드물게 수 ms~수십 ms짜리 "꼬리"가 생긴다. PREEMPT_RT는 그 꼬리를 잘라 **최악값을 작고 예측 가능하게** 만드는 대신 평균 처리량을 조금 내준다. 이 노트는 그 꼬리가 **어디서 생기는지**(§3), PREEMPT_RT가 **무엇을 바꿔서 자르는지**(§4~§5), 그리고 **그래도 남는 것**(§10)을 차례로 본다.

bare-metal 비유로 한 줄 요약하면:

- 일반 Linux 커널 = "critical section이 어디에 얼마나 긴지 아무도 다 모르는, 수천 명이 쓴 펌웨어". 각 드라이버가 `spin_lock_irqsave()`로 IRQ를 끄는 구간이 곧 FreeRTOS의 `taskENTER_CRITICAL()` 구간이다.
- PREEMPT_RT = "그 critical section 대부분을 우선순위 상속 뮤텍스로 바꾸고, ISR 본문을 전부 태스크로 옮겨서, 정말로 인터럽트를 끄는 구간을 아주 작게 줄인 펌웨어". FreeRTOS로 치면 "ISR은 `xTaskNotifyFromISR()`만 하고 본문은 deferred task에서" 패턴을 커널 전체에 강제한 것이다.

### 0.1 이 노트의 지도

| 절 | 내용 | 실행 예제 |
|---|---|---|
| §1 | 정의: latency, jitter, throughput, hard/firm/soft, WCET | — |
| §2 | 실습 준비 + 두 커널 정체 확인 | 예제 1, 2 |
| §3 | 지연 사슬과 지연 원인 전부 | — |
| §4 | preemption 모델 사다리 | 예제 3(소스 확인) |
| §5 | PREEMPT_RT가 바꾸는 것 + spinlock_t vs raw 실험 | 예제 4 |
| §6 | cyclictest 헤드라인 비교 + TCG의 한계 | 예제 5, 6 |
| §7 | 우선순위 역전 (실측 타임라인) | 예제 7 |
| §8 | page fault와 mlockall | 예제 8 |
| §9 | RT throttling | 예제 9 |
| §10 | RT가 주지 않는 것, 언제 무엇을 쓰나 | — |

---

## 1. 정의 — RTOS에서 쓰던 말을 Linux에 맞춰 다시 정리

### 1.1 latency, jitter, throughput

- **latency(지연)**: 사건이 일어난 시각부터 그 사건을 처리할 코드가 실제로 돌기 시작하는 시각까지. RT에서 가장 많이 쓰는 것은 **scheduling latency(wakeup latency)** — "타이머가 만료되거나 IRQ가 들어와서 RT 태스크가 깨어나야 하는 순간"부터 "그 태스크가 CPU에서 첫 명령을 실행하는 순간"까지다. cyclictest가 재는 것이 이것이다.
- **jitter(지터)**: latency가 매번 얼마나 흔들리는가. 1 kHz 제어 루프에서는 주기 오차가 곧 jitter다. 평균 latency가 50 µs로 일정하면 보정할 수 있지만, 5 µs ~ 900 µs를 오가면 보정할 수 없다.
- **throughput(처리량)**: 단위 시간당 해낸 일의 양. 일반 커널이 최적화하는 대상이다. 선점 지점을 늘리고 락을 잠드는 락으로 바꾸면 context switch가 늘어 처리량이 떨어진다 — §6에서 hackbench로 실제로 본다.

### 1.2 hard / firm / soft RT

| 구분 | deadline을 놓치면 | 예시 | 보통 어디서 도나 |
|---|---|---|---|
| hard RT | 시스템 실패·위험 | 모터 전류 루프(20 kHz), 브레이크·에어백, 안전 정지 | MCU·RTOS, FPGA, 또는 검증된 RT 코어 |
| firm RT | 그 결과는 쓸모없어 버린다. 가끔은 견딘다 | 영상 프레임, 레이더 스캔, 오디오 버퍼 | Linux(+RT), DSP |
| soft RT | 품질이 서서히 떨어진다 | UI 반응, 텔레메트리, 로깅 | 일반 Linux |

함정: 면접에서 "hard RT = 아주 빠름"이라고 말하면 감점이다. hard는 **보장의 강도**지 속도가 아니다. 100 ms deadline이라도 놓치면 사고인 시스템은 hard RT다.

### 1.3 WCET, 결정론(deterministic), "빠르다"

- **WCET(Worst-Case Execution Time)**: 코드 조각이 어떤 입력·어떤 캐시 상태에서도 넘지 않는 실행 시간 상한. MCU에서는 정적 분석 도구(aiT 같은)나 사이클 카운트로 구할 수 있다. Linux 커널 전체의 WCET은 **누구도 증명하지 못한다** — 코드가 수천만 줄이고 드라이버마다 다르고, 캐시·DRAM·인터커넥트가 공유되기 때문이다. 그래서 Linux RT 세계에서는 증명 대신 **측정된 최악값(observed worst case)** 을 오래, 부하를 걸고 잰다(N3).
- **deterministic(결정론적)**: 같은 입력에 같은 시간. RT가 원하는 것은 "빠름"이 아니라 이것이다. 평균 5 µs·최악 2 ms보다 평균 20 µs·최악 60 µs가 RT로는 더 좋은 시스템이다.

### 1.4 RTOS 개념 ↔ Linux 대응표

| Don이 아는 RTOS 개념 | Linux에서의 이름 | 이 노트 |
|---|---|---|
| ISR latency | IRQ latency (HW IRQ → handler 첫 명령) | §3 |
| task wakeup latency | scheduling / wakeup latency | §3, §6 |
| `taskENTER_CRITICAL()` (BASEPRI·PRIMASK) | `local_irq_save()`, `spin_lock_irqsave()` | §3.2 |
| `vTaskSuspendAll()` (스케줄러 잠금) | `preempt_disable()`, non-RT `spin_lock()` | §3.3 |
| deferred interrupt handling task | threaded IRQ `irq/N-name` | §5 |
| mutex with priority inheritance | `rt_mutex`, `PTHREAD_PRIO_INHERIT`, RT의 `spinlock_t` | §5, §7 |
| 정적 메모리 할당 (heap 금지) | `mlockall()` + 미리 할당·prefault | §8 |
| idle hook / tickless idle | cpuidle, `NO_HZ_IDLE`/`NO_HZ_FULL` | §3.6, N2 |
| 우선순위 0~(configMAX_PRIORITIES-1) | SCHED_FIFO/RR 1~99 (99가 최고) | F1 |

---

## 2. 실습 준비 — 같은 소스, 두 커널

### 2.1 실행 환경

- VM 호스트: Lima Ubuntu VM `elinux` (aarch64, 커널 7.0). 정적 바이너리와 모듈을 여기서 빌드한다.
- 게스트: QEMU `virt`, `-cpu cortex-a57,pmu=on -smp 2 -m 1G`, TCG(에뮬레이션). 커널 두 개:
  - `/var/lab/out/Image-trace` = **6.18.55-trace**, `CONFIG_PREEMPT=y`(full), `HZ=250`, ftrace 전부(irqsoff/preemptoff 포함), `RT_GROUP_SCHED=y`
  - `/var/lab/out/Image-rt` = **6.18.55-rt**, 같은 설정 + `CONFIG_PREEMPT_RT=y`, `HZ=1000`, `NO_HZ_FULL`, `RCU_NOCB_CPU`
- 루트 파일시스템: `rootfs-trace.ext4`(Ubuntu Base 24.04 + rt-tests, stress-ng, gcc …)의 내 사본. systemd로 부팅하면 1~4분 걸려서, **`init=/n1/run.sh`** 로 systemd 없이 내 스크립트를 PID 1로 띄우고, 실험이 끝나면 `sysrq o`로 끈다. 커널 스레드(`irq/…`, `ksoftirqd/…`)는 init과 무관하게 다 생긴다.

```sh
# VM 호스트에서: 사본에 실험 파일 넣기 (마운트 없이 debugfs로 씀)
# (실제로는 이 일을 파일 여러 개에 반복하는 작은 스크립트로 실행했다)
cp --sparse=always /var/lab/out/rootfs-trace.ext4 /var/tmp/N1/n1_root.ext4
printf 'mkdir n1\nwrite n1_pi n1/n1_pi\nsif n1/n1_pi mode 0100755\n' \
  | /usr/sbin/debugfs -w -f - /var/tmp/N1/n1_root.ext4
# 부팅 (커널만 바꿔 가며 같은 rootfs 사용)
qemu-system-aarch64 -machine virt -cpu cortex-a57,pmu=on -smp 2 -m 1G -nographic \
  -kernel /var/lab/out/Image-rt -drive file=/var/tmp/N1/n1_root.ext4,if=virtio,format=raw \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait init=/n1/run.sh n1steps=id,cyc quiet"
```

`run.sh`의 핵심(실험별 명령은 각 절에 그대로 적는다):

```sh
#!/bin/bash
export PATH=/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
mount -t proc proc /proc; mount -t sysfs sys /sys; mount -t devtmpfs dev /dev
mount -t tmpfs tmp /tmp; mkdir -p /dev/shm; mount -t tmpfs shm /dev/shm
mount -t debugfs none /sys/kernel/debug; mount -t tracefs none /sys/kernel/tracing
STEPS=$(sed -n 's/.*n1steps=\([^ ]*\).*/\1/p' /proc/cmdline)
for s in ${STEPS//,/ }; do echo "@@@ STEP $s"; case $s in ... esac; done
sync; mount -o remount,ro /; echo o > /proc/sysrq-trigger
```

함정 두 개를 실제로 밟았다: (1) `/dev/shm`이 없으면 `cyclictest -m`이 `ERROR: shm_open No such file or directory`로 바로 끝난다 — systemd가 평소 해 주던 마운트다. (2) `sysrq o`로 끄기 전에 `remount,ro`를 안 하면 다음 debugfs 쓰기 때 `EXT4-fs error ... bad block bitmap checksum`이 났다. `e2fsck -fy`로 복구했다.

### 2.2 예제 1·2 — 두 커널의 정체 확인

무엇을 확인하나: 두 커널이 실제로 어떤 preemption 모델인지, RT 커널에서 IRQ handler와 softirq가 스레드로 바뀌었는지.

```sh
uname -r; uname -v; ls /sys/kernel/realtime
zcat /proc/config.gz | grep -E "^CONFIG_(PREEMPT[A-Z_]*|HZ|NO_HZ_FULL|RT_GROUP_SCHED|IRQ_FORCED_THREADING)="
ps -eo pid,cls,rtprio,comm | grep -E "PID|irq/|ksoftirq|ktimer|rcuc|rcub|rcu_preempt|pr/"
ps -eo comm | grep -c "^irq/"
```

예제 1 — Image-trace (PREEMPT full):

```text
6.18.55-trace
#1 SMP PREEMPT Sat Oct 17 23:37:47 PDT 2026
ls: cannot access '/sys/kernel/realtime': No such file or directory
CONFIG_IRQ_FORCED_THREADING=y
CONFIG_PREEMPT_BUILD=y
CONFIG_PREEMPT=y
CONFIG_PREEMPT_COUNT=y
CONFIG_PREEMPTION=y
CONFIG_PREEMPT_RCU=y
CONFIG_RT_GROUP_SCHED=y
CONFIG_HZ=250
CONFIG_PREEMPT_NOTIFIERS=y
CONFIG_PREEMPTIRQ_TRACEPOINTS=y
CONFIG_PREEMPT_TRACER=y
cat: /sys/kernel/debug/sched/preempt: No such file or directory
  PID CLS RTPRIO COMMAND
   14  TS      - ksoftirqd/0
   15  TS      - rcu_preempt
   22  TS      - ksoftirqd/1
   31  TS      - pr/ttyAMA-1
0
```

예제 2 — Image-rt (PREEMPT_RT):

```text
6.18.55-rt
#1 SMP PREEMPT_RT Sun Oct 18 00:43:53 PDT 2026
ls: cannot access '/sys/kernel/realtime': No such file or directory
CONFIG_IRQ_FORCED_THREADING=y
CONFIG_NO_HZ_FULL=y
CONFIG_PREEMPT_BUILD=y
CONFIG_PREEMPT=y
CONFIG_PREEMPT_RT=y
CONFIG_PREEMPT_COUNT=y
CONFIG_PREEMPTION=y
CONFIG_PREEMPT_RCU=y
CONFIG_HZ=1000
CONFIG_PREEMPT_NOTIFIERS=y
cat: /sys/kernel/debug/sched/preempt: No such file or directory
  PID CLS RTPRIO COMMAND
   14  TS      - ksoftirqd/0
   15  FF      1 ktimers/0
   16  FF      1 rcu_preempt
   17  FF      1 rcub/0
   19  FF      1 rcuc/0
   27  FF      1 rcuc/1
   28  FF      1 ktimers/1
   29  TS      - ksoftirqd/1
   40  TS      - pr/ttyAMA-1
   68  FF     50 irq/17-virtio1-config
   69  FF     50 irq/18-virtio1-req.0
   70  FF     50 irq/19-virtio1-req.1
   73  FF     50 irq/20-virtio0-config
   74  FF     50 irq/21-virtio0-input.0
   75  FF     50 irq/22-virtio0-output.0
   77  FF     50 irq/23-rtc-pl031
   80  FF     50 irq/25-GPIO Key Poweroff
   82  FF     50 irq/13-uart-pl011
9
```

출력에서 볼 것:

- `uname -v`가 `SMP PREEMPT` 대 `SMP PREEMPT_RT`로 갈린다. 가장 빠른 판별법이다. 모듈 vermagic도 `6.18.55-rt SMP preempt_rt`로 다르다(§5.3).
- **`/sys/kernel/realtime`은 두 커널 모두 없다.** 예전 out-of-tree RT 패치가 만들던 파일이라 많은 블로그·스크립트가 RT 판별에 쓰지만, mainline 6.18에는 없다(소스 `kernel/ksysfs.c`에도 없다). mainline RT 판별은 `uname -v`나 `/proc/config.gz`의 `CONFIG_PREEMPT_RT=y`로 한다.
- RT 커널에도 `CONFIG_PREEMPT=y`가 같이 보인다 — `PREEMPT_RT`가 full preemption 위에 얹히는 구조(`select PREEMPTION`)라서다. RT 쪽에는 `RT_GROUP_SCHED`가 없다(RT 커널 설정에서는 꺼짐 — §9의 throttling 실험은 trace 커널에서 했다).
- **스레드 목록이 결정적 차이다.** PREEMPT에는 `irq/` 스레드가 **0개**다 — 모든 handler가 hardirq 문맥에서 돈다. PREEMPT_RT에는 **9개**: virtio 블록·네트워크, PL031 RTC, PL011 UART, GPIO 전원 키 IRQ가 전부 `irq/N-name` 이름의 **SCHED_FIFO(FF) 50** 스레드가 되었다. 드라이버는 하나도 안 바꿨다 — `irq_setup_forced_threading()`이 한 일이다.
- RT에만 `ktimers/0`, `ktimers/1`(FIFO 1, timer softirq 전용)과 `rcuc/N`·`rcub/0`(FIFO 1, RCU 콜백·부스트 스레드)이 있다. `rcu_preempt`도 TS(SCHED_OTHER)에서 FIFO 1로 바뀌었다.
- 두 커널 모두 `pr/ttyAMA-1`(SCHED_OTHER)이 있다 — nbcon printk 출력 스레드다. PL011 드라이버가 nbcon으로 바뀌어서 non-RT에서도 콘솔 출력이 스레드로 나간다.
- `ksoftirqd/N`은 두 커널 모두 SCHED_OTHER다. RT에서도 softirq 대부분은 그것을 올린 스레드가 처리하고, `ksoftirqd`는 넘친 일만 받는다.

### 2.3 주의 — QEMU TCG에서 지연을 재면 무엇이 틀어지나 (정직한 경고)

이 노트의 모든 시간 측정은 **에뮬레이터 안**이다. 결과를 읽기 전에 왜 절대값이 의미 없는지 알아야 한다:

- 게스트 vCPU 하나 = 호스트의 **스레드 하나**다. 호스트(VM, 그 위의 macOS)가 그 스레드를 언제든 재운다. 측정 당시 VM에는 다른 노트 작성자 16명의 QEMU가 함께 돌아 load average가 8~15였다. 게스트 커널이 아무리 RT여도 **호스트가 vCPU를 뺏은 시간은 게스트 지연으로 보인다** — §3.6의 "hypervisor" 항목을 실험 환경 자체가 재현하고 있는 셈이다.
- 게스트 타이머(arch timer)도 호스트 타이머로 에뮬레이션된다. 만료 처리가 호스트 스케줄링에 묶인다.
- TCG는 명령을 번역해서 돌리므로 같은 코드가 실제 Cortex-A57보다 10~100배 느리다. 커널 경로 하나가 µs가 아니라 수십~수백 µs가 된다.

그래서 이 노트는 **절대값이 아니라 구조적 차이**(같은 조건에서 A와 B의 비교: spinlock_t vs raw, PI 없음 vs 있음, fault vs mlock)만 결론으로 쓴다. 실제 보드에서 이 실험들을 다시 하면 숫자는 100배쯤 작아지고, 구조는 같다. N3가 실제 측정 방법론을 다룬다.

---

## 3. 지연 사슬 — 사건부터 유저 태스크까지, 어디서 시간이 새나

### 3.1 직관 — 릴레이 경주

센서 키트의 IMU가 1 kHz로 DRDY(data ready) 인터럽트를 올리고, 유저 공간 제어 스레드(SCHED_FIFO 80)가 그 데이터를 읽어 모터 명령을 계산한다고 하자. 이 스레드가 데이터를 손에 쥐기까지는 여덟 명이 바통을 넘긴다. 각 주자는 평소엔 빠르지만, **누군가 트랙을 막고 있으면 기다린다.** 최악 지연 = 각 구간 최악값의 합(에 가깝다).

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="140" y="20" text-anchor="middle" font-size="13">지연 사슬 (위 → 아래)</text>
  <text x="500" y="20" text-anchor="middle" font-size="13">그 단계에 끼어드는 지연 원인</text>
  <rect x="10" y="32" width="280" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="54" font-size="13">① HW 이벤트 (센서 DRDY, timer)</text>
  <rect x="330" y="32" width="340" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="54" font-size="12">버스·DMA 경합, 장치 쪽 지연 (FIFO watermark)</text>
  <line x1="292" y1="49" x2="328" y2="49" stroke="#d0564a" stroke-dasharray="3 2"/>
  <line x1="150" y1="67" x2="150" y2="80" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="82" width="280" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="104" font-size="13">② GIC → CPU에 IRQ 신호</text>
  <rect x="330" y="82" width="340" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="104" font-size="12">IRQ off 구간 (local_irq_save), 다른 IRQ 처리 중</text>
  <line x1="292" y1="99" x2="328" y2="99" stroke="#d0564a" stroke-dasharray="3 2"/>
  <line x1="150" y1="117" x2="150" y2="130" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="132" width="280" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="154" font-size="13">③ exception entry (el1h_64_irq)</text>
  <rect x="330" y="132" width="340" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="154" font-size="12">cpuidle exit, SMI(x86), EL3/TEE, hypervisor</text>
  <line x1="292" y1="149" x2="328" y2="149" stroke="#d0564a" stroke-dasharray="3 2"/>
  <line x1="150" y1="167" x2="150" y2="180" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="182" width="280" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="204" font-size="13">④ top half (hardirq handler)</text>
  <rect x="330" y="182" width="340" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="204" font-size="12">같은 CPU의 다른 handler, DVFS 낮은 클럭</text>
  <line x1="292" y1="199" x2="328" y2="199" stroke="#d0564a" stroke-dasharray="3 2"/>
  <line x1="150" y1="217" x2="150" y2="230" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="232" width="280" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="254" font-size="13">⑤ wakeup (try_to_wake_up)</text>
  <rect x="330" y="232" width="340" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="254" font-size="12">irq_exit의 softirq (non-RT), 긴 handler</text>
  <line x1="292" y1="249" x2="328" y2="249" stroke="#d0564a" stroke-dasharray="3 2"/>
  <line x1="150" y1="267" x2="150" y2="280" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="282" width="280" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="304" font-size="13">⑥ scheduler (__schedule)</text>
  <rect x="330" y="282" width="340" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="304" font-size="12">preempt off 구간 (non-RT spinlock, 긴 경로)</text>
  <line x1="292" y1="299" x2="328" y2="299" stroke="#d0564a" stroke-dasharray="3 2"/>
  <line x1="150" y1="317" x2="150" y2="330" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="332" width="280" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="354" font-size="13">⑦ context switch</text>
  <rect x="330" y="332" width="340" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="354" font-size="12">더 높은 RT 태스크, 우선순위 역전, throttling</text>
  <line x1="292" y1="349" x2="328" y2="349" stroke="#d0564a" stroke-dasharray="3 2"/>
  <line x1="150" y1="367" x2="150" y2="380" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="382" width="280" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="404" font-size="13">⑧ user task가 다시 돈다</text>
  <rect x="330" y="382" width="340" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="404" font-size="12">cache·TLB miss, page fault, glibc 내부 lock</text>
  <line x1="292" y1="399" x2="328" y2="399" stroke="#d0564a" stroke-dasharray="3 2"/>
  <text x="10" y="440" font-size="12">latency = ①부터 ⑧까지 걸린 시간. jitter = 그 값이 매번 얼마나 흔들리나. RT는 최댓값 싸움이다.</text>
</svg>
```

그림 2 — 지연 사슬(파랑)과 각 구간에 끼어드는 지연 원인(빨강). cyclictest는 타이머 IRQ로 시작해 ⑧까지를 한 번에 잰다. 원인이 어디인지 가르려면 ftrace의 irqsoff/preemptoff/wakeup tracer나 rtla timerlat(N3, M5)이 필요하다.

F6에서 ②~④(GIC → `el1h_64_irq` → `handle_domain_irq` → handler)를, F1 §10에서 ⑥~⑦(`__schedule()` → `context_switch()` → `cpu_switch_to`)을 이미 따라갔다. 이 절은 그 경로 위에 **"무엇이 막을 수 있나"** 를 겹친다.

### 3.2 IRQ가 꺼진 구간 — 가장 직접적인 원인

CPU의 인터럽트 마스크(arm64 `DAIF.I`)가 켜져 있으면 GIC가 IRQ를 올려도 CPU는 받지 않는다. FreeRTOS의 `taskENTER_CRITICAL()`이 BASEPRI를 올리는 것과 같다. Linux에서 IRQ를 끄는 코드:

- `local_irq_disable()`/`local_irq_save()` 직접 호출
- `spin_lock_irqsave()`/`spin_lock_irq()` — non-RT 커널에서는 진짜로 IRQ를 끈다
- hardirq handler 실행 중 (arm64 Linux는 handler 동안 IRQ를 다시 켜지 않는다 — 중첩 IRQ 없음, F6)
- 커널 진입·복귀 경로, 스케줄러 내부의 일부(`rq->lock`은 raw spinlock)

이 구간이 100 µs면 그 CPU의 IRQ latency 최악값은 최소 100 µs다. ftrace `irqsoff` tracer가 "가장 긴 IRQ off 구간과 그 콜스택"을 기록한다(M5). 랩의 Image-trace에 켜져 있다.

### 3.3 선점이 꺼진 구간 — IRQ는 받았지만 스케줄러가 못 바꾼다

IRQ handler가 RT 태스크를 깨웠다(`try_to_wake_up()` → `TIF_NEED_RESCHED`). 그런데 그 CPU에서 돌던 코드가 `preempt_count != 0`이면 `preempt_enable()`이 불릴 때까지 전환하지 못한다. FreeRTOS의 `vTaskSuspendAll()` 구간과 같다. 원인:

- non-RT 커널의 `spin_lock()` — 락을 쥐는 동안 선점 금지 (F4)
- `preempt_disable()`, `get_cpu()`, `local_bh_disable()`, per-CPU 변수 접근 구간
- softirq 처리 중 (non-RT에서는 `irq_exit()`에서 hardirq 직후 바로 softirq를 돌린다 — NET_RX 같은 것이 길면 ms 단위가 된다. 너무 길면 `ksoftirqd`로 넘기지만 그 전까지는 막힌다, F7)
- non-`PREEMPT_RCU` 커널의 `rcu_read_lock()` 구간 (PREEMPT·RT 커널은 `PREEMPT_RCU`라 이 구간도 선점된다)
- PREEMPT_NONE/VOLUNTARY 커널이라면 **커널 코드 전체**가 사실상 선점 불가 (§4)

### 3.4 우선순위 역전 — 기다리는 대상이 밀린다

높은 우선순위 태스크가 낮은 우선순위 태스크가 쥔 락을 기다리는데, 중간 우선순위 태스크가 그 낮은 태스크를 밀어내면, 높은 태스크가 **중간 태스크보다 늦게** 돈다. Mars Pathfinder(VxWorks)의 그 버그다. 유저 공간에서는 `PTHREAD_PRIO_INHERIT`로, 커널에서는 `rt_mutex`(PREEMPT_RT에서는 `spinlock_t`·`mutex`까지)로 막는다. §7에서 실측한다.

### 3.5 메모리 — page fault, 캐시, TLB

- **page fault**: Linux는 `malloc()`·스택·`mmap()` 메모리를 처음 만질 때 물리 페이지를 붙인다(demand paging, F11). 첫 접근마다 예외 → 페이지 할당 → 0으로 채움 → 페이지 테이블 갱신. RTOS에는 없는 비용이다. 처방은 `mlockall(MCL_CURRENT|MCL_FUTURE)` + 시작 때 미리 다 만져 두기(prefault). §8에서 실측.
- **major fault**: 파일 백업 페이지(실행 파일 코드 자체 포함)가 밀려났다가 다시 읽히면 디스크 I/O — ms 단위. `mlockall`이 이것도 막는다.
- **캐시·TLB·분기 예측기**: 다른 태스크·커널이 돌고 나면 RT 태스크의 working set이 L1/L2에서 쫓겨난다. 같은 코드가 "따뜻할 때 3 µs, 차가울 때 30 µs"가 된다. 공유 L3·DRAM 대역을 GPU·ISP·NPU가 같이 쓰는 SoC에서는 더 심하다(DMA bus contention). 처방: CPU isolation, 코어 분리, 캐시 파티셔닝(일부 SoC의 MPAM·cache coloring) — N2.

### 3.6 하드웨어·펌웨어가 OS 몰래 가져가는 시간

- **cpuidle exit latency**: idle CPU가 깊은 C-state(arm64 PSCI의 CPU/cluster power-down)에 있으면 깨어나는 데 수십 µs~ms. `/sys/devices/system/cpu/cpu0/cpuidle/stateN/latency`에 각 상태의 exit latency가 있다. 처방: PM QoS(`/dev/cpu_dma_latency`에 0 쓰기) — cyclictest가 기본으로 하는 일이다(N3, N5).
- **DVFS**: 클럭이 낮은 상태에서 이벤트가 오면 같은 코드가 느리게 돌고, 주파수 전환 자체가 PLL·전압 안정화로 수십 µs 멈출 수 있다(SoC마다 다름). 처방: RT 코어는 `performance` governor.
- **SMI (x86)**: System Management Interrupt — BIOS가 OS 몰래 CPU를 잡아 SMM에서 수십~수백 µs 쓴다. OS는 볼 수도 막을 수도 없다. ftrace `hwlat` tracer(osnoise 계열)가 "OS가 아무것도 안 했는데 사라진 시간"으로 잡는다.
- **ARM의 대응물**: EL3 펌웨어(TF-A BL31)의 SMC 처리, **secure world(OP-TEE)** 의 실행, PSCI 호출, SDEI, 그리고 EL2 하이퍼바이저. Normal world Linux가 보기엔 똑같이 "사라진 시간"이다. Don이 C5에서 본 BL31이 런타임에도 상주하며 이 시간을 쓸 수 있다.
- **hypervisor**: VM 안의 vCPU는 호스트 스케줄러가 언제든 뺏는다. **이 노트의 QEMU 실험이 정확히 이 상황이다**(§2.3).
- **thermal throttling**: 과열 시 SoC가 클럭을 강제로 내린다 — 같은 루프가 갑자기 느려진다. 센서 키트처럼 밀폐된 장비에서 여름에만 나는 버그의 단골.

### 3.7 타이머와 tick

- `HZ`(Image-trace 250, Image-rt 1000)는 주기 tick 빈도다. hrtimer(F8)는 tick과 무관하게 ns 해상도로 만료되므로 `clock_nanosleep()`의 정확도는 HZ에 묶이지 않는다. 다만 tick 자체가 매 1~4 ms마다 RT 태스크를 잠깐 끊는다(수 µs).
- `NO_HZ_IDLE`: idle일 때 tick을 끈다(전력). `NO_HZ_FULL`: 태스크 하나만 도는 CPU에서 tick을 끈다 — 끊김을 줄이지만 커널 진입·복귀 비용이 늘어난다(N2).
- PREEMPT_RT에서 hrtimer 콜백은 `HRTIMER_MODE_HARD`가 아니면 softirq(`ktimers/N` 스레드)로 옮겨진다(F2 §6). 그래서 RT 커널에서 타이머 기반 깨움은 hardirq에서 직접, 나머지는 스레드에서 돈다.

### 3.8 정리 표 — 원인, 크기, 보는 도구, 처방

| 원인 | 전형적 크기 (일반적 수치, 기억 기반) | 보는 도구 | 처방 |
|---|---|---|---|
| IRQ off 구간 | µs ~ 수백 µs (나쁜 드라이버는 ms) | ftrace irqsoff | PREEMPT_RT, 드라이버 수정 |
| preempt off 구간 | µs ~ ms | ftrace preemptoff, wakeup_rt | PREEMPT/RT 모델, 드라이버 수정 |
| softirq (NET_RX, BLOCK) | 수십 µs ~ ms | `/proc/softirqs`, timerlat | RT(스레드화), IRQ affinity |
| 우선순위 역전 | 무한대까지 가능 | `/proc/<pid>/sched`, trace | PI 뮤텍스, 우선순위 설계 |
| page fault | 수 µs/페이지 (major는 ms) | `getrusage`, perf `page-faults` | mlockall + prefault |
| 캐시·TLB·DRAM 경합 | 수 µs ~ 수십 µs | perf 카운터 | isolation, 코어 분리 |
| cpuidle exit | 수십 µs ~ ms | cpuidle sysfs | PM QoS, `cpuidle.off=1` |
| SMI / EL3 / TEE / hypervisor | 수십 ~ 수백 µs | hwlat, osnoise | BIOS 설정, 펌웨어 협상, 베어메탈 |
| RT throttling | 1초마다 50 ms | 루프 갭 측정 (§9) | 폭주 방지 장치로 이해하고 설계 |

---

## 4. preemption 모델 — 커널 코드를 "어디서" 끊을 수 있나

유저 코드는 모든 모델에서 언제든 선점된다. 모델이 정하는 것은 **커널 코드(syscall 처리, 드라이버, kthread) 실행 중에 더 높은 우선순위 태스크가 깨어났을 때 얼마나 빨리 바꿀 수 있나**다. F1 §7.2에서 Kconfig 다섯 가지와 시간축 그림을 봤다. 여기서는 RT 관점의 사다리로만 다시 정리한다.

```svg
<svg viewBox="0 0 680 335" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13">모델</text>
  <text x="190" y="22" font-size="13">커널 코드 중 선점 가능한 부분 (막대 = 개념적 비율, 측정값 아님)</text>
  <text x="20" y="60" font-size="13">PREEMPT_NONE</text>
  <rect x="190" y="44" width="470" height="24" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <rect x="190" y="44" width="70" height="24" rx="4" fill="#888" fill-opacity="0.18" stroke="#888"/>
  <text x="268" y="61" font-size="12">유저 복귀 · 스스로 잠들 때만</text>
  <text x="20" y="112" font-size="13">PREEMPT_VOLUNTARY</text>
  <rect x="190" y="96" width="470" height="24" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <rect x="190" y="96" width="141" height="24" rx="4" fill="#888" fill-opacity="0.18" stroke="#888"/>
  <text x="339" y="113" font-size="12">+ cond_resched() 지점</text>
  <text x="20" y="164" font-size="13">PREEMPT (full)</text>
  <rect x="190" y="148" width="470" height="24" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <rect x="190" y="148" width="291" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/>
  <text x="198" y="165" font-size="12">+ preempt_count == 0 인 어디서나</text>
  <text x="20" y="216" font-size="13">PREEMPT_LAZY</text>
  <rect x="190" y="200" width="470" height="24" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <rect x="190" y="200" width="291" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/>
  <text x="198" y="217" font-size="12">full과 같되 fair끼리는 tick까지 미룸</text>
  <text x="20" y="268" font-size="13">PREEMPT_RT</text>
  <rect x="190" y="252" width="470" height="24" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <rect x="190" y="252" width="432" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/>
  <text x="198" y="269" font-size="12">+ spinlock_t 안 · IRQ/softirq 핸들러 안</text>
  <text x="20" y="306" font-size="12">PREEMPT_RT도 100%는 아니다: raw_spinlock_t, preempt_disable(),</text>
  <text x="20" y="322" font-size="12">entry·scheduler 코드, 진짜 hardirq 구간은 그대로 남는다.</text>
</svg>
```

그림 3 — preemption 모델 사다리. 위로 갈수록 처리량 중심, 아래로 갈수록 지연 중심. 막대는 개념적 비율이다.

| 모델 | 최악 지연을 정하는 것 | RT 용도 |
|---|---|---|
| `PREEMPT_NONE` | 가장 긴 syscall·커널 경로 전체 | 안 됨 (서버) |
| `PREEMPT_VOLUNTARY` | 다음 `cond_resched()`까지 | 안 됨 (오래된 데스크톱) |
| `PREEMPT` (full) | 가장 긴 spinlock·preempt off·IRQ off·softirq 구간 | soft RT (ms 단위). arm64 defconfig, Image-trace |
| `PREEMPT_LAZY` (6.13+) | full과 같다(RT·DL 깨움은 즉시). fair 태스크끼리만 미룸 | full과 같은 RT 특성 + 처리량 회복 |
| `PREEMPT_RT` | raw spinlock·진짜 hardirq·preempt_disable 구간만 | firm RT, 잘 고른 HW에서 hard RT에 가깝게 |

포인트: PREEMPT(full)와 PREEMPT_RT의 차이는 **"spinlock과 IRQ handler와 softirq 안에서도 끊을 수 있느냐"** 하나로 요약된다. 드라이버 수천 개가 쓰는 `spin_lock()` 구간이 non-RT에서는 선점 불가 구간이고, 그 최악값은 아무도 관리하지 않는다. PREEMPT_RT는 그 구간을 "선점 가능하고 우선순위 상속이 되는 구간"으로 바꿔 **최악값을 소수의 raw 구간으로 줄인다.**

### 4.1 예제 3 — mainline 6.18 소스에서 PREEMPT_RT를 확인

무엇을 확인하나: "PREEMPT_RT가 mainline에 들어왔다"는 말을 랩 소스 트리(6.18.55)로 직접 확인한다 — 어느 아키텍처가 지원하는지, `spinlock_t`가 무엇으로 바뀌는지.

```sh
cd /var/lab/src/linux-6.18.55
grep -n "select ARCH_SUPPORTS_RT" arch/*/Kconfig
sed -n "/^config PREEMPT_RT$/,/select PREEMPTION/p" kernel/Kconfig.preempt
sed -n "45,56p" include/linux/spinlock_types.h
```

```text
arch/arm64/Kconfig:110:	select ARCH_SUPPORTS_RT
arch/loongarch/Kconfig:74:	select ARCH_SUPPORTS_RT
arch/riscv/Kconfig:75:	select ARCH_SUPPORTS_RT
arch/x86/Kconfig:133:	select ARCH_SUPPORTS_RT
config PREEMPT_RT
	bool "Fully Preemptible Kernel (Real-Time)"
	depends on EXPERT && ARCH_SUPPORTS_RT && !COMPILE_TEST
	select PREEMPTION
#else /* !CONFIG_PREEMPT_RT */

/* PREEMPT_RT kernels map spinlock to rt_mutex */
#include <linux/rtmutex.h>

typedef struct spinlock {
	struct rt_mutex_base	lock;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
} spinlock_t;
```

출력에서 볼 것:

- **arm64는 6.18에서 무조건 `ARCH_SUPPORTS_RT`** 다. 32-bit ARM(`arch/arm`)은 이 목록에 없다 — i.MX6 같은 Cortex-A9/A7 보드에서 RT를 쓰려면 아직 out-of-tree RT 패치가 필요하다(6.18.55 소스 기준).
- `depends on EXPERT` — menuconfig에서 `General setup → Configure standard kernel features (expert users)`를 켜야 보인다. 숨겨 둔 이유는 "알고 켜라"다.
- RT 커널의 `spinlock_t` 안에는 `raw_spinlock_t`가 아니라 **`struct rt_mutex_base`** 가 들어 있다. 같은 이름의 타입이 커널 설정에 따라 완전히 다른 물건이 된다.

역사(기억 기반, 날짜는 공개 발표 기준): RT 패치(`-rt`)는 2004년경부터 Ingo Molnar·Thomas Gleixner 등이 out-of-tree로 유지했고, 그동안 hrtimer, threaded IRQ, `rt_mutex`/PI futex, `PREEMPT_RCU`, lockdep 일부 같은 조각이 하나씩 mainline에 먼저 들어갔다. 마지막 장애물은 **printk**였다 — 어떤 문맥에서도 불리고 콘솔 드라이버를 직접 돌리는 printk가 RT 지연을 깨뜨렸기 때문이다. nbcon(non-BKL console) 구조가 완성되자 **2024년 9월 6.12 merge window에서 `PREEMPT_RT`를 켜는 마지막 커밋이 머지**되었고, 6.12(2024년 11월 릴리스)부터 x86·arm64·riscv에서 mainline 설정만으로 RT 커널을 만들 수 있다. 위 6.18 출력의 loongarch는 그 뒤에 추가된 것이다.

---

## 5. PREEMPT_RT가 실제로 바꾸는 것

### 5.1 한 장 요약

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="150" y="22" text-anchor="middle" font-size="14">non-RT (PREEMPT full)</text>
  <text x="520" y="22" text-anchor="middle" font-size="14">PREEMPT_RT</text>
  <rect x="10" y="36" width="285" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="59" font-size="12">spinlock_t: busy-wait + preempt off</text>
  <line x1="297" y1="54" x2="363" y2="54" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="365" y="36" width="305" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="375" y="59" font-size="12">rt_mutex 기반 sleeping lock + PI</text>
  <rect x="10" y="88" width="285" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="111" font-size="12">rwlock_t</text>
  <line x1="297" y1="106" x2="363" y2="106" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="365" y="88" width="305" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="375" y="111" font-size="12">rwbase_rt (잠드는 reader/writer lock)</text>
  <rect x="10" y="140" width="285" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="163" font-size="12">local_lock_t: preempt_disable()</text>
  <line x1="297" y1="158" x2="363" y2="158" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="365" y="140" width="305" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="375" y="163" font-size="12">per-CPU spinlock_t (선점 가능)</text>
  <rect x="10" y="192" width="285" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="215" font-size="12">raw_spinlock_t</text>
  <line x1="297" y1="210" x2="363" y2="210" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="365" y="192" width="305" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="375" y="215" font-size="12">그대로 busy-wait (변하지 않음)</text>
  <rect x="10" y="244" width="285" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="267" font-size="12">request_irq() handler: hardirq</text>
  <line x1="297" y1="262" x2="363" y2="262" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="365" y="244" width="305" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="375" y="267" font-size="12">irq/N-name 스레드 (FIFO 50)</text>
  <rect x="10" y="296" width="285" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="319" font-size="12">softirq: irq_exit()에서 바로</text>
  <line x1="297" y1="314" x2="363" y2="314" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="365" y="296" width="305" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="375" y="319" font-size="12">스레드 안에서 (timer는 ktimers/N)</text>
  <rect x="10" y="348" width="285" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="371" font-size="12">hrtimer 콜백: hardirq 기본</text>
  <line x1="297" y1="366" x2="363" y2="366" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="365" y="348" width="305" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="375" y="371" font-size="12">softirq로 (HRTIMER_MODE_HARD만 예외)</text>
  <rect x="10" y="400" width="285" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="423" font-size="12">printk legacy 콘솔: 호출자가 출력</text>
  <line x1="297" y1="418" x2="363" y2="418" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="365" y="400" width="305" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="375" y="423" font-size="12">pr/legacy 스레드로 (nbcon은 pr/이름)</text>
  <text x="10" y="460" font-size="12">같은 소스 코드, 다른 동작. 드라이버가 spin_lock()을 부르면 RT 커널에서는 잠들 수 있는 락이 된다.</text>
</svg>
```

그림 4 — PREEMPT_RT가 바꾸는 것(초록)과 바꾸지 않는 것(회색). 드라이버 소스는 그대로인데 동작이 바뀐다.

하나씩 보면:

1. **`spinlock_t` → rt_mutex 기반 sleeping lock.** 락이 이미 잡혀 있으면 돌지 않고 잠든다. 소유자에게 **우선순위 상속(PI)** 이 걸린다. 쥔 동안 선점은 가능하지만 **migration은 금지**(`migrate_disable()`)하고 `rcu_read_lock()`을 잡는다(아래 소스). per-CPU 데이터를 spinlock으로 보호하던 코드가 그래서 여전히 맞게 돈다.
2. **`spin_lock_irqsave()`가 IRQ를 끄지 않는다.** RT에서는 `flags = 0; spin_lock(lock);`일 뿐이다(아래 소스). IRQ handler가 어차피 스레드에서 돌기 때문에 IRQ를 끌 필요가 없다. 이것이 IRQ off 구간을 극적으로 줄이는 핵심이다.
3. **`rwlock_t` → `rwbase_rt`**, `local_lock_t` → per-CPU `spinlock_t`(non-RT에서는 그냥 `preempt_disable()`).
4. **`raw_spinlock_t`는 그대로 진짜 spinlock.** 스케줄러 `rq->lock`, irqchip, 타이머 하드웨어, 일부 아주 짧은 구간용이다. 드라이버가 함부로 쓰면 RT를 망친다.
5. **IRQ handler 강제 스레드화**: `request_irq()`로 등록한 handler가 `irq/N-name` 커널 스레드(SCHED_FIFO 50)에서 돈다. 진짜 hardirq 문맥에서는 "스레드를 깨우는" 기본 handler만 돈다. `kernel/irq/manage.c`의 `irq_setup_forced_threading()`을 보면 예외는 세 플래그다: `IRQF_NO_THREAD`(타이머·IPI·cascade irqchip처럼 스레드로 돌면 안 되는 것), `IRQF_PERCPU`(arch timer 같은 per-CPU IRQ), `IRQF_ONESHOT`(드라이버가 이미 ONESHOT으로 등록한 것). 이미 `request_threaded_irq()`로 등록한 thread_fn은 원래대로 스레드에서 돈다(F2 §7, F6).
6. **softirq도 스레드 문맥**: hardirq 직후 바로 돌리지 않고, softirq를 올린 스레드(예: `irq/N` 스레드)가 `local_bh_enable()` 시점에 자기 문맥에서 처리하거나 `ksoftirqd/N`이 처리한다. timer·hrtimer softirq는 전용 `ktimers/N` 스레드가 맡는다. `local_bh_disable()`은 RT에서 per-CPU 락이 되어 선점 가능하다.
7. **hrtimer 콜백**: `HRTIMER_MODE_HARD`가 아니면 softirq로 옮겨진다(F2 §6).
8. **printk**: 메시지를 링버퍼에 넣는 것과 콘솔에 찍는 것을 분리했다. **nbcon 콘솔**(랩의 PL011 드라이버 `amba-pl011.c`에 `pl011_console_write_atomic`/`pl011_console_write_thread`가 있다)은 커널 설정과 무관하게 전용 스레드 `pr/<콘솔이름>`에서 찍고, panic 같은 비상시에만 atomic 경로를 쓴다 — 예제 1·2에서 두 커널 모두 `pr/ttyAMA-1` 스레드가 보인다. 아직 nbcon으로 안 바뀐 **legacy 콘솔**은 non-RT에서는 printk를 부른 쪽이 직접 찍지만, RT에서는 `force_legacy_kthread()`가 참이라 `pr/legacy` 스레드로 미룬다(`kernel/printk/internal.h`). "printk 한 줄이 UART 115200 baud로 ms 동안 IRQ를 끄는" 고전 문제가 이렇게 사라졌고, 이것이 RT 편입의 마지막 조각이었다.

### 5.2 소스로 확인 — RT의 `spin_lock()`과 `spin_lock_irqsave()`

```sh
cd /var/lab/src/linux-6.18.55
sed -n 46,52p kernel/locking/spinlock_rt.c
sed -n "/define spin_lock_irqsave(lock, flags)/,+6p" include/linux/spinlock_rt.h
```

```text
static __always_inline void __rt_spin_lock(spinlock_t *lock)
{
	rtlock_might_resched();
	rtlock_lock(&lock->lock);
	rcu_read_lock();
	migrate_disable();
}
#define spin_lock_irqsave(lock, flags)			 \
	do {						 \
		typecheck(unsigned long, flags);	 \
		flags = 0;				 \
		spin_lock(lock);			 \
	} while (0)
```

출력에서 볼 것: `preempt_disable()`이 **없다**. 대신 `migrate_disable()` + `rcu_read_lock()`. 그리고 `spin_lock_irqsave()`가 IRQ 상태를 저장하지도 끄지도 않는다(`flags = 0`). 드라이버 개발자 입장의 결론: **RT 커널에서 `spin_lock()` 구간 안에서는 잠들 수 있는 락을 쥔 것과 같다** — 그래서 hardirq 문맥(진짜 `IRQF_NO_THREAD` handler, raw spinlock 안)에서 `spin_lock()`을 부르면 "sleeping function called from invalid context"가 난다(F2 §13.4, M8의 `DEBUG_ATOMIC_SLEEP`).

### 5.3 예제 4 — spinlock_t는 RT에서 선점되고, raw_spinlock_t는 안 된다

무엇을 확인하나: CPU1에 묶인 FIFO 1 커널 스레드가 락을 쥔 채 20 ms 바쁘게 기다린다(`mdelay`), 이것을 100 ms마다 20번. 같은 CPU1에서 FIFO 80 cyclictest(1 ms 주기)가 돈다. 락 종류(`spinlock_t`/`raw_spinlock_t`) × 커널(PREEMPT/PREEMPT_RT) 네 조합에서 cyclictest가 막히나.

```c
// SPDX-License-Identifier: GPL-2.0
/* n1_lock: a FIFO-1 kthread on CPU1 holds spinlock_t or raw_spinlock_t for hold_ms */
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
static int raw;			/* 0 = spinlock_t, 1 = raw_spinlock_t */
static int hold_ms = 20, reps = 20, gap_ms = 80;
module_param(raw, int, 0444);
module_param(hold_ms, int, 0444);
module_param(reps, int, 0444);
module_param(gap_ms, int, 0444);

static DEFINE_SPINLOCK(n1_spin);
static DEFINE_RAW_SPINLOCK(n1_raw);
static struct task_struct *th;
static int n1_fn(void *unused)
{
	int i;

	sched_set_fifo_low(current);		/* SCHED_FIFO prio 1 */
	for (i = 0; i < reps && !kthread_should_stop(); i++) {
		if (raw) {
			raw_spin_lock(&n1_raw);
			mdelay(hold_ms);	/* busy-wait, preemption OFF on every kernel */
			raw_spin_unlock(&n1_raw);
		} else {
			spin_lock(&n1_spin);
			mdelay(hold_ms);	/* RT: rt_mutex held, still preemptible */
			spin_unlock(&n1_spin);
		}
		msleep(gap_ms);
	}
	pr_info("n1_lock: %d x %d ms in %s done (preempt_model %s)\n", i, hold_ms,
		raw ? "raw_spinlock_t" : "spinlock_t",
		IS_ENABLED(CONFIG_PREEMPT_RT) ? "RT" : "non-RT");
	while (!kthread_should_stop())
		msleep(50);
	return 0;
}

static int __init n1_init(void)
{
	th = kthread_create(n1_fn, NULL, "n1_lock");
	if (IS_ERR(th))
		return PTR_ERR(th);
	kthread_bind(th, 1);
	wake_up_process(th);
	return 0;
}
static void __exit n1_exit(void)
{
	kthread_stop(th);
}
module_init(n1_init);
module_exit(n1_exit);
MODULE_DESCRIPTION("N1: spinlock_t vs raw_spinlock_t hold on PREEMPT_RT");
MODULE_LICENSE("GPL");
```

```make
obj-m += n1_lock.o
```

```sh
# VM 호스트: 커널마다 따로 빌드 (vermagic이 달라야 한다)
make -C /var/lab/out/linux-build-trace M=$PWD modules   # → n1_lock_6.18.55-trace.ko
make -C /var/lab/out/linux-build-rt    M=$PWD modules   # → n1_lock_6.18.55-rt.ko (vermagic "6.18.55-rt SMP preempt_rt")
# 게스트: 모듈 없음 1회 + (spinlock_t, raw) 2회씩
for r in none 0 1 0 1; do
  cyclictest -m -t1 -a1 -p80 -i1000 -D 6 -q -h 40000 > /tmp/l.txt 2>&1 &
  C=$!; sleep 1
  [ $r = none ] && echo "baseline (no module)" || insmod /n1/n1_lock_$(uname -r).ko raw=$r hold_ms=20 reps=20 gap_ms=80
  wait $C; [ $r = none ] || { dmesg | grep n1_lock | tail -1 | cut -c16-; rmmod n1_lock; }
  grep -E "^# (Total|Max Lat|Histogram Overflows)" /tmp/l.txt
  grep -v "^#" /tmp/l.txt | awk '{a+=$2; if ($1>=5000) b+=$2; if ($1>=15000) c+=$2} END {print "samples", a, " >=5ms:", b+0, " >=15ms:", c+0}'
done
```

PREEMPT (Image-trace):

```text
baseline (no module)
# Total: 000004813
# Max Latencies: 34033
# Histogram Overflows: 00000
samples 4813  >=5ms: 36  >=15ms: 4
n1_lock: 20 x 20 ms in spinlock_t done (preempt_model non-RT)
# Total: 000004376
# Max Latencies: 57266
# Histogram Overflows: 00005
samples 4376  >=5ms: 43  >=15ms: 19
n1_lock: 20 x 20 ms in raw_spinlock_t done (preempt_model non-RT)
# Total: 000004361
# Max Latencies: 49182
# Histogram Overflows: 00002
samples 4361  >=5ms: 40  >=15ms: 21
n1_lock: 20 x 20 ms in spinlock_t done (preempt_model non-RT)
# Total: 000004392
# Max Latencies: 43118
# Histogram Overflows: 00003
samples 4392  >=5ms: 31  >=15ms: 20
n1_lock: 20 x 20 ms in raw_spinlock_t done (preempt_model non-RT)
# Total: 000004027
# Max Latencies: 77945
# Histogram Overflows: 00007
samples 4027  >=5ms: 48  >=15ms: 18
```

PREEMPT_RT (Image-rt):

```text
baseline (no module)
# Total: 000004165
# Max Latencies: 97978
# Histogram Overflows: 00004
samples 4165  >=5ms: 70  >=15ms: 6
n1_lock: 20 x 20 ms in spinlock_t done (preempt_model RT)
# Total: 000004578
# Max Latencies: 25480
# Histogram Overflows: 00000
samples 4578  >=5ms: 64  >=15ms: 3
n1_lock: 20 x 20 ms in raw_spinlock_t done (preempt_model RT)
# Total: 000004039
# Max Latencies: 59007
# Histogram Overflows: 00009
samples 4039  >=5ms: 42  >=15ms: 16
n1_lock: 20 x 20 ms in spinlock_t done (preempt_model RT)
# Total: 000004683
# Max Latencies: 46916
# Histogram Overflows: 00001
samples 4683  >=5ms: 49  >=15ms: 2
n1_lock: 20 x 20 ms in raw_spinlock_t done (preempt_model RT)
# Total: 000004508
# Max Latencies: 42431
# Histogram Overflows: 00004
samples 4508  >=5ms: 39  >=15ms: 17
```

`-h 40000`은 1 µs 칸 40000개짜리 히스토그램이고 40 ms 넘는 표본은 `Histogram Overflows`로 따로 센다. "15 ms 이상 늦은 표본 수"(히스토그램 15~40 ms 칸 + overflow)를 모으면:

| 커널 | 모듈 없음 | `spinlock_t` 1회차 / 2회차 | `raw_spinlock_t` 1회차 / 2회차 |
|---|---|---|---|
| PREEMPT (trace) | 4 | 24 / 23 | 23 / 25 |
| PREEMPT_RT | 10 | **3 / 3** | 25 / 21 |

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <line x1="60" y1="250.0" x2="660" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
  <text x="54" y="254.0" text-anchor="end" font-size="12">0</text>
  <line x1="60" y1="183.3" x2="660" y2="183.3" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
  <text x="54" y="187.3" text-anchor="end" font-size="12">10</text>
  <line x1="60" y1="116.7" x2="660" y2="116.7" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
  <text x="54" y="120.7" text-anchor="end" font-size="12">20</text>
  <line x1="60" y1="50.0" x2="660" y2="50.0" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
  <text x="54" y="54.0" text-anchor="end" font-size="12">30</text>
  <line x1="60" y1="116.7" x2="660" y2="116.7" stroke="#d0564a" stroke-width="1"/>
  <text x="64" y="111.7" font-size="12">20 = 락 보유 횟수</text>
  <rect x="90.0" y="223.3" width="58.0" height="26.7" fill="#888" fill-opacity="0.55" stroke="#888"/>
  <text x="120.0" y="218.3" text-anchor="middle" font-size="12">4</text>
  <text x="120.0" y="268" text-anchor="middle" font-size="12">모듈 없음</text>
  <rect x="175.0" y="90.0" width="28.0" height="160.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <rect x="205.0" y="96.7" width="28.0" height="153.3" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <text x="205.0" y="85.0" text-anchor="middle" font-size="12">24 / 23</text>
  <text x="205.0" y="268" text-anchor="middle" font-size="12">spinlock_t</text>
  <rect x="260.0" y="96.7" width="28.0" height="153.3" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
  <rect x="290.0" y="83.3" width="28.0" height="166.7" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
  <text x="290.0" y="78.3" text-anchor="middle" font-size="12">23 / 25</text>
  <text x="290.0" y="268" text-anchor="middle" font-size="12">raw_spinlock_t</text>
  <text x="205" y="290" text-anchor="middle" font-size="13">PREEMPT (Image-trace)</text>
  <rect x="390.0" y="183.3" width="58.0" height="66.7" fill="#888" fill-opacity="0.55" stroke="#888"/>
  <text x="420.0" y="178.3" text-anchor="middle" font-size="12">10</text>
  <text x="420.0" y="268" text-anchor="middle" font-size="12">모듈 없음</text>
  <rect x="475.0" y="230.0" width="28.0" height="20.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <rect x="505.0" y="230.0" width="28.0" height="20.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
  <text x="505.0" y="225.0" text-anchor="middle" font-size="12">3 / 3</text>
  <text x="505.0" y="268" text-anchor="middle" font-size="12">spinlock_t</text>
  <rect x="560.0" y="83.3" width="28.0" height="166.7" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
  <rect x="590.0" y="110.0" width="28.0" height="140.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
  <text x="590.0" y="78.3" text-anchor="middle" font-size="12">25 / 21</text>
  <text x="590.0" y="268" text-anchor="middle" font-size="12">raw_spinlock_t</text>
  <text x="505" y="290" text-anchor="middle" font-size="13">PREEMPT_RT (Image-rt)</text>
  <line x1="60" y1="250" x2="660" y2="250" stroke="currentColor"/>
  <text x="10" y="25" font-size="13">FIFO 80 cyclictest(CPU1)가 15 ms 이상 늦은 횟수 — FIFO 1 kthread가 20 ms x 20회 락 보유</text>
  <text x="10" y="328" font-size="12">막대 두 개 = 두 번 실행. 모듈 없음 = TCG 잡음 기준선. 실측값 (QEMU TCG, smp 2).</text>
</svg>
```

그림 5 — 락 20회 보유가 cyclictest를 몇 번 막았나(실측). 빨간 선 20 = 락을 쥔 횟수. PREEMPT_RT + `spinlock_t`만 기준선 수준으로 떨어진다.

출력에서 볼 것:

- **PREEMPT + spinlock_t**: 약 20번 막혔다. non-RT의 `spin_lock()`은 `preempt_disable()`이라 FIFO 1 kthread가 쥔 동안 FIFO 80 cyclictest가 CPU1에서 못 돈다. 우선순위가 아무 의미 없다.
- **PREEMPT + raw**: 똑같다. non-RT에서는 둘이 같은 물건이다(`spinlock_t`가 `raw_spinlock`을 감싼 것뿐).
- **PREEMPT_RT + spinlock_t**: **3번** — 모듈이 없을 때(10번)보다도 적은, TCG 잡음 수준이다. kthread가 락을 쥔 채로 FIFO 80에게 **선점당했다**. 이것이 §5.2의 `migrate_disable()` + `rcu_read_lock()`(선점은 허용)의 실제 효과다.
- **PREEMPT_RT + raw**: 다시 약 20번. raw spinlock은 RT에서도 `preempt_disable()`이다. **RT 커널의 최악 지연은 가장 긴 raw 구간이 정한다** — 드라이버가 raw 락을 함부로 쓰면 RT 커널을 깐 의미가 사라진다.
- `Max Latencies`만 보면 결론을 못 낸다: 모듈이 없는 RT 기준선의 Max가 97978 µs로 가장 컸다. 호스트가 vCPU를 한 번 오래 뺏으면 Max는 그 값이 된다(§2.3). **"사건 수"처럼 실험 설계와 맞물린 지표를 골라야 잡음 속에서 구조가 보인다** — 실제 보드에서도 같은 원칙이다(N3).

---

## 6. 헤드라인 비교 — cyclictest idle vs 부하, PREEMPT vs PREEMPT_RT

cyclictest의 옵션·히스토그램 해석·rtla timerlat은 N3에서 자세히 다룬다. 여기서는 "두 커널을 같은 조건에서 돌리면 무엇이 보이나"만 본다.

### 6.1 예제 5·6 — 측정

무엇을 확인하나: 두 vCPU 각각에 FIFO 90 측정 스레드를 두고(`-S`), 1 ms 주기로 20초 동안 깨어남 지연을 잰다. idle 한 번, `stress-ng --cpu 2`(CPU 부하) + `hackbench` 반복(소켓·스케줄러·커널 경로 부하)을 건 채로 한 번.

```sh
cyclictest -m -S -p 90 -i 1000 -D 20 -q -h 20000 > /tmp/c_idle.txt 2>&1
grep -E "^# (Min|Avg|Max) Lat|^# Histogram Overflows|^# Total" /tmp/c_idle.txt
stress-ng --cpu 2 --timeout 25 -q &
(while :; do hackbench -l 50 -g 2 >/dev/null 2>&1; done) & HB=$!
sleep 2
cyclictest -m -S -p 90 -i 1000 -D 20 -q -h 20000 > /tmp/c_load.txt 2>&1
grep -E "^# (Min|Avg|Max) Lat|^# Histogram Overflows|^# Total" /tmp/c_load.txt
```

예제 5 — PREEMPT (Image-trace), 값 단위 µs, 열 = CPU0 CPU1:

```text
-- idle
# Total: 000015301 000014941
# Min Latencies: 00072 00068
# Avg Latencies: 00720 00723
# Max Latencies: 44237 55049
# Histogram Overflows: 00009 00011
-- load (stress-ng --cpu 2 + hackbench loop)
# Total: 000011402 000011091
# Min Latencies: 00084 00080
# Avg Latencies: 01189 01021
# Max Latencies: 1035839 1025501
# Histogram Overflows: 00020 00016
```

예제 6 — PREEMPT_RT (Image-rt):

```text
-- idle
# Total: 000014837 000015321
# Min Latencies: 00045 00039
# Avg Latencies: 00621 00561
# Max Latencies: 94808 91668
# Histogram Overflows: 00003 00005
-- load (stress-ng --cpu 2 + hackbench loop)
# Total: 000008377 000008073
# Min Latencies: 00055 00045
# Avg Latencies: 01698 01611
# Max Latencies: 58293 53928
# Histogram Overflows: 00023 00026
```

히스토그램(두 CPU 합산, overflow 제외)에서 계산한 백분위수:

| 조건 | 표본 | p50 | p99 | p99.9 | 최댓값 (두 CPU 중 큰 쪽) |
|---|---|---|---|---|---|
| PREEMPT idle | 30242 | 421 µs | 4212 µs | 10028 µs | 55049 µs |
| PREEMPT_RT idle | 30158 | 182 µs | 4278 µs | 9117 µs | 94808 µs |
| PREEMPT load | 22493 | 414 µs | 5016 µs | 9937 µs | 1035839 µs |
| PREEMPT_RT load | 16450 | 529 µs | 9888 µs | 17488 µs | 58293 µs |

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <line x1="80" y1="280.0" x2="660" y2="280.0" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
  <text x="74" y="284.0" text-anchor="end" font-size="12">100 µs</text>
  <line x1="80" y1="224.2" x2="660" y2="224.2" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
  <text x="74" y="228.2" text-anchor="end" font-size="12">1 ms</text>
  <line x1="80" y1="168.4" x2="660" y2="168.4" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
  <text x="74" y="172.4" text-anchor="end" font-size="12">10 ms</text>
  <line x1="80" y1="112.6" x2="660" y2="112.6" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
  <text x="74" y="116.6" text-anchor="end" font-size="12">100 ms</text>
  <line x1="80" y1="56.8" x2="660" y2="56.8" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
  <text x="74" y="60.8" text-anchor="end" font-size="12">1 s</text>
  <line x1="80" y1="40" x2="80" y2="280" stroke="currentColor"/>
  <line x1="80" y1="280" x2="660" y2="280" stroke="currentColor"/>
  <rect x="95" y="245.2" width="30" height="34.8" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="110" y="241.2" text-anchor="middle" font-size="12">421µs</text>
  <rect x="131" y="189.4" width="30" height="90.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="146" y="203.4" text-anchor="middle" font-size="10">4.2ms</text>
  <rect x="167" y="127.1" width="30" height="152.9" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="182" y="123.1" text-anchor="middle" font-size="12">55.0ms</text>
  <text x="146" y="298" text-anchor="middle" font-size="12">PREEMPT idle</text>
  <rect x="235" y="265.5" width="30" height="14.5" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="250" y="261.5" text-anchor="middle" font-size="12">182µs</text>
  <rect x="271" y="189.0" width="30" height="91.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="286" y="203.0" text-anchor="middle" font-size="10">4.3ms</text>
  <rect x="307" y="113.9" width="30" height="166.1" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="322" y="109.9" text-anchor="middle" font-size="12">94.8ms</text>
  <text x="286" y="298" text-anchor="middle" font-size="12">RT idle</text>
  <rect x="375" y="245.6" width="30" height="34.4" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="390" y="241.6" text-anchor="middle" font-size="12">414µs</text>
  <rect x="411" y="185.1" width="30" height="94.9" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="426" y="199.1" text-anchor="middle" font-size="10">5.0ms</text>
  <rect x="447" y="55.9" width="30" height="224.1" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="462" y="51.9" text-anchor="middle" font-size="12">1036ms</text>
  <text x="426" y="298" text-anchor="middle" font-size="12">PREEMPT load</text>
  <rect x="515" y="239.6" width="30" height="40.4" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="530" y="235.6" text-anchor="middle" font-size="12">529µs</text>
  <rect x="551" y="168.7" width="30" height="111.3" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="566" y="182.7" text-anchor="middle" font-size="10">9.9ms</text>
  <rect x="587" y="125.7" width="30" height="154.3" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="602" y="121.7" text-anchor="middle" font-size="12">58.3ms</text>
  <text x="566" y="298" text-anchor="middle" font-size="12">RT load</text>
  <rect x="90" y="312" width="14" height="12" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="110" y="322" font-size="12">p50</text>
  <rect x="170" y="312" width="14" height="12" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="190" y="322" font-size="12">p99</text>
  <rect x="250" y="312" width="14" height="12" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="270" y="322" font-size="12">max</text>
  <text x="10" y="20" font-size="13">cyclictest wakeup latency (로그 축) — QEMU TCG, 공유 VM 부하 속 20초 측정</text>
  <text x="10" y="348" font-size="12">TCG에서는 RT가 더 좋아 보이지 않는다: 호스트가 vCPU를 뺏는 잡음이 커널 차이보다 크다 (§6.2).</text>
</svg>
```

그림 6 — cyclictest 헤드라인 비교(로그 축, 실측). 실제 하드웨어라면 PREEMPT_RT의 max가 PREEMPT보다 한두 자릿수 작아야 하지만, TCG에서는 섞인 결과가 나온다.

### 6.2 결과 읽기 — 정직하게

- **절대값은 전부 의미 없다.** 실제 arm64 보드라면 idle p50은 수~수십 µs다. 여기서 p50이 200~500 µs인 것은 TCG가 커널 경로를 수십 배 느리게 돌리기 때문이다. 20초 × 1000 Hz × 2 CPU = 40000이어야 할 표본이 15000~30000개뿐인 것도, 측정 스레드가 제때 못 깨어난 주기가 그만큼 많았다는 뜻이다(cyclictest는 놓친 주기를 건너뛴다).
- **RT가 이기는 곳**: 부하 상태 최댓값. PREEMPT는 **1.04초**짜리 지연이 났고 RT는 58 ms였다. 이 한 칸이 RT가 겨냥하는 "꼬리"다. 단, 이것도 한 번의 실행이라 TCG 잡음일 가능성을 배제할 수 없다.
- **RT가 지는 곳**: 부하 상태 p99·p99.9가 RT 쪽이 두 배쯤 나쁘다. 이유: (1) RT에서는 모든 IRQ handler가 스레드라 IRQ마다 context switch가 두 번 더 생기고, TCG에서 context switch 하나가 매우 비싸다. (2) hackbench의 소켓 락이 잠드는 락이 되어 전환이 폭증한다. (3) 두 커널의 설정도 다르다(HZ 250 vs 1000, NO_HZ_FULL, RCU_NOCB). (4) 무엇보다 **호스트가 vCPU 스레드를 뺏는 시간**은 게스트 커널이 어떤 모델이든 똑같이 들어온다 — 공유 VM의 load average가 15~24였다.
- 결론: **"RT 커널이 더 낫다"를 VM 안에서 증명하려 하지 말 것.** 면접에서 "VM에서 cyclictest를 돌려 봤더니 RT가 별로였다"고 말하면 측정 환경을 모른다는 고백이다. 올바른 말은 "vCPU는 호스트 스레드라서 hypervisor가 지연 사슬의 맨 앞에 끼어든다. RT 측정은 베어메탈 타깃에서, 실제 워크로드 + stress 부하로, 몇 시간 단위로 한다"이다.
- 그럼에도 §5.3(락 종류), §7(PI), §8(mlockall)처럼 **같은 조건에서 한 변수만 바꾼 실험**은 TCG에서도 구조가 선명하게 보였다. 잡음이 큰 환경일수록 실험을 "차이"로 설계한다.

### 6.3 처리량 비용 — hackbench

무엇을 확인하나: 같은 hackbench(`-l 200 -g 2`: 2그룹 × 20 sender/receiver, 소켓으로 메시지 왕복)를 두 커널에서 3번. 숫자는 걸린 초.

```sh
for i in 1 2 3; do hackbench -l 200 -g 2 2>&1 | grep Time; done
```

PREEMPT (Image-trace):

```text
Time: 37.427
Time: 39.321
Time: 37.227
```

PREEMPT_RT (Image-rt):

```text
Time: 134.790
Time: 122.405
Time: 109.390
```

출력에서 볼 것: RT가 약 3배 느리다. 실제 하드웨어에서는 이 정도까지 벌어지지 않는다(TCG가 context switch·락 경합 비용을 부풀린다). 하지만 방향은 진짜다: **PREEMPT_RT는 처리량을 내주고 최악값을 산다.** 락 경합이 많은 워크로드(네트워크·스토리지·IPC가 많은)일수록 비용이 커진다. 실무에서는 "RT 코어 몇 개 + 나머지 코어는 처리량 작업"으로 나눠 이 비용을 관리한다(N2의 CPU isolation).

---

## 7. 우선순위 역전 — 실측 타임라인

### 7.1 직관

Don이 FreeRTOS에서 `xSemaphoreCreateMutex()`(PI 있음)와 `xSemaphoreCreateBinary()`(PI 없음)의 차이를 알고 있다면 이미 이 절의 결론을 안다. Linux 유저 공간의 기본 `pthread_mutex`는 **PI가 없는 쪽**이다(`PTHREAD_PRIO_NONE`). PI를 원하면 속성으로 명시해야 한다.

### 7.2 예제 7 — 세 스레드, 한 CPU, 뮤텍스 하나

무엇을 확인하나: CPU1에 L(FIFO 10)·M(FIFO 20)·H(FIFO 30)를 올린다. L이 뮤텍스를 쥐고, H가 그 뮤텍스를 기다리고, M이 CPU를 40 ms 태운다. PI 없음/있음에서 H가 얼마나 기다리나. 각 스레드는 실제로 CPU에서 돈 구간을 기록한다(호스트 부하에 덜 흔들리게 "벽시계 20 ms"가 아니라 **CPU 시간 20 ms**를 태운다). 시작은 시각이 아니라 이벤트로 맞춘다 — TCG에서 스레드 생성이 수십 ms씩 걸려서 시각 기반 시작은 순서가 뒤집혔다(실제로 첫 시도에서 L이 H가 오기도 전에 끝났다).

```c
/* n1_pi.c - priority inversion on CPU1: L(FIFO10) holds mutex, H(30) waits, M(20) burns */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

static pthread_mutex_t mtx; static double t0;
static volatile int locked, wanting, mmade;	/* event-driven start (TCG is slow) */
static char logb[4096][48]; static int nlog;	/* printed after the run: no stdio lock */
#define LOG(...) do { int i_ = __atomic_fetch_add(&nlog, 1, __ATOMIC_RELAXED); \
	if (i_ < 4096) snprintf(logb[i_], 48, __VA_ARGS__); } while (0)
static double now_ms(clockid_t c) { struct timespec ts; clock_gettime(c, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
static double rel(void) { return now_ms(CLOCK_MONOTONIC) - t0; }
/* burn 'cpu' ms of CPU time; log the wall-clock segments where we actually ran */
static void burn(const char *who, double cpu) {
	double c0 = now_ms(CLOCK_THREAD_CPUTIME_ID), seg = rel(), last = seg, t;
	while (now_ms(CLOCK_THREAD_CPUTIME_ID) - c0 < cpu) {
		t = rel(); if (t - last > 0.2) { LOG("RUN %s %.2f %.2f", who, seg, last); seg = t; }
		last = t;
	}
	LOG("RUN %s %.2f %.2f", who, seg, last);
}
static void *low(void *a) { pthread_mutex_lock(&mtx); LOG("EV L lock %.2f", rel()); locked = 1;
	while (!mmade) ;	/* hold the lock until H waits and M exists */
	burn("L", 20); LOG("EV L unlock %.2f", rel()); pthread_mutex_unlock(&mtx); return a; }
static void *med(void *a) { LOG("EV M start %.2f", rel()); burn("M", 40);
	LOG("EV M done %.2f", rel()); return a; }
static void *high(void *a) { double w = rel(); LOG("EV H want %.2f", w); wanting = 1;
	pthread_mutex_lock(&mtx); double g = rel(); pthread_mutex_unlock(&mtx);
	LOG("EV H got %.2f", g); LOG("H blocked %.2f ms", g - w); return a; }
static pthread_t start(void *(*fn)(void *), int prio) {	/* SCHED_FIFO thread on CPU1 */
	pthread_attr_t at; struct sched_param sp = { .sched_priority = prio };
	cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(1, &cs); pthread_t t;
	pthread_attr_init(&at); pthread_attr_setinheritsched(&at, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&at, SCHED_FIFO); pthread_attr_setschedparam(&at, &sp);
	pthread_attr_setaffinity_np(&at, sizeof(cs), &cs);
	int e = pthread_create(&t, &at, fn, NULL);
	if (e) { fprintf(stderr, "pthread_create: %s\n", strerror(e)); exit(1); }
	return t;
}
int main(int argc, char **argv) {
	int pi = argc > 1 && !strcmp(argv[1], "pi");
	pthread_mutexattr_t ma; pthread_mutexattr_init(&ma);
	if (pi) pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT);
	pthread_mutex_init(&mtx, &ma);
	cpu_set_t c0; CPU_ZERO(&c0); CPU_SET(0, &c0);	/* main stays on CPU0 */
	if (sched_setaffinity(0, sizeof(c0), &c0) || mlockall(MCL_CURRENT | MCL_FUTURE)) { perror("setup"); exit(1); }
	printf("mode=%s\n", pi ? "PTHREAD_PRIO_INHERIT" : "PTHREAD_PRIO_NONE"); t0 = now_ms(CLOCK_MONOTONIC);
	pthread_t l = start(low, 10);	while (!locked) usleep(100);
	pthread_t h = start(high, 30);	while (!wanting) usleep(100);	/* H blocks on mtx */
	pthread_t m = start(med, 20); LOG("EV M created %.2f", rel()); mmade = 1;	/* M > L, but M < L boosted to 30 */
	pthread_join(l, NULL); pthread_join(h, NULL); pthread_join(m, NULL);
	for (int i = 0; i < nlog && i < 4096; i++) puts(logb[i]);
	return 0;
}
```

```sh
# 게스트(Image-trace) 안에서, 3회씩
for i in 1 2 3; do /n1/n1_pi none; /n1/n1_pi pi; done
```

세 번째 쌍의 출력(`RUN` 줄은 일부만 남기고 `...`로 줄였다):

```text
mode=PTHREAD_PRIO_NONE
EV L lock 64.10
EV H want 117.05
EV M created 169.52
EV M start 170.03
RUN M 170.22 171.47
RUN M 171.96 175.47
...
EV M done 212.40
RUN L 213.37 215.49
RUN L 215.85 219.50
...
EV L unlock 234.23
EV H got 234.48
H blocked 117.43 ms
mode=PTHREAD_PRIO_INHERIT
EV L lock 53.87
EV H want 108.85
EV M created 162.20
RUN L 162.37 164.02
RUN L 164.34 164.34
...
EV L unlock 183.71
EV H got 184.08
H blocked 75.23 ms
EV M start 185.55
RUN M 185.67 189.18
RUN M 189.58 189.58
...
EV M done 230.25
```

여섯 번의 결과를 "M이 생성된 시점부터 H가 락을 얻기까지"로 정리하면(H가 락을 요청한 시점부터 재면 TCG에서 main 스레드가 M을 만드는 시간까지 섞이므로 이 기준을 쓴다):

| 회차 | PREEMPT, PI 없음 | PREEMPT, PI | PREEMPT_RT, PI 없음 | PREEMPT_RT, PI |
|---|---|---|---|---|
| 1 | 72.87 ms | 23.78 ms | 99.01 ms | 23.42 ms |
| 2 | 65.37 ms | 22.20 ms | 72.68 ms | 32.90 ms |
| 3 | 64.96 ms | 21.88 ms | 96.77 ms | 27.88 ms |

(PREEMPT_RT 열은 같은 바이너리를 Image-rt 게스트에서 돌린 것이다. 유저 공간 PI는 `FUTEX_LOCK_PI`라는 mainline 기능이라 **두 커널에서 똑같이 동작한다** — RT 커널이 필요한 것은 커널 안 락의 PI다.)

```svg
<svg viewBox="0 0 680 415" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="30" font-size="13">PTHREAD_PRIO_NONE (PI 없음)</text>
  <text x="10" y="63" font-size="12">H (FIFO 30)</text>
  <line x1="150" y1="73" x2="650" y2="73" stroke="#888" stroke-width="0.5"/>
  <rect x="150.0" y="53" width="406.0" height="16" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="3 2"/>
  <text x="562.0" y="66" font-size="12">락 획득 +65.0 ms</text>
  <text x="10" y="101" font-size="12">M (FIFO 20)</text>
  <line x1="150" y1="111" x2="650" y2="111" stroke="#888" stroke-width="0.5"/>
  <rect x="154.4" y="91" width="262.9" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="none"/>
  <text x="10" y="139" font-size="12">L (FIFO 10)</text>
  <line x1="150" y1="149" x2="650" y2="149" stroke="#888" stroke-width="0.5"/>
  <rect x="424.1" y="129" width="129.9" height="16" fill="#3f9a6b" fill-opacity="0.5" stroke="none"/>
  <line x1="150" y1="169" x2="650" y2="169" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="150.0" y1="169" x2="150.0" y2="173" stroke="currentColor"/>
  <text x="150.0" y="185" text-anchor="middle" font-size="12">0</text>
  <line x1="275.0" y1="169" x2="275.0" y2="173" stroke="currentColor"/>
  <text x="275.0" y="185" text-anchor="middle" font-size="12">20</text>
  <line x1="400.0" y1="169" x2="400.0" y2="173" stroke="currentColor"/>
  <text x="400.0" y="185" text-anchor="middle" font-size="12">40</text>
  <line x1="525.0" y1="169" x2="525.0" y2="173" stroke="currentColor"/>
  <text x="525.0" y="185" text-anchor="middle" font-size="12">60</text>
  <line x1="650.0" y1="169" x2="650.0" y2="173" stroke="currentColor"/>
  <text x="650.0" y="185" text-anchor="middle" font-size="12">80</text>
  <text x="10" y="210" font-size="13">PTHREAD_PRIO_INHERIT (PI)</text>
  <text x="10" y="243" font-size="12">H (FIFO 30)</text>
  <line x1="150" y1="253" x2="650" y2="253" stroke="#888" stroke-width="0.5"/>
  <rect x="150.0" y="233" width="136.8" height="16" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="3 2"/>
  <text x="292.8" y="246" font-size="12">락 획득 +21.9 ms</text>
  <text x="10" y="281" font-size="12">M (FIFO 20)</text>
  <line x1="150" y1="291" x2="650" y2="291" stroke="#888" stroke-width="0.5"/>
  <rect x="296.7" y="271" width="88.4" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="none"/>
  <rect x="407.0" y="271" width="10.6" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="none"/>
  <rect x="431.9" y="271" width="11.0" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="none"/>
  <rect x="446.8" y="271" width="71.6" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="none"/>
  <rect x="523.4" y="271" width="51.2" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="none"/>
  <text x="10" y="319" font-size="12">L (FIFO 10)</text>
  <line x1="150" y1="329" x2="650" y2="329" stroke="#888" stroke-width="0.5"/>
  <rect x="151.1" y="309" width="17.0" height="16" fill="#3f9a6b" fill-opacity="0.5" stroke="none"/>
  <rect x="171.9" y="309" width="94.6" height="16" fill="#3f9a6b" fill-opacity="0.5" stroke="none"/>
  <rect x="281.1" y="309" width="2.6" height="16" fill="#3f9a6b" fill-opacity="0.5" stroke="none"/>
  <line x1="150" y1="349" x2="650" y2="349" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="150.0" y1="349" x2="150.0" y2="353" stroke="currentColor"/>
  <text x="150.0" y="365" text-anchor="middle" font-size="12">0</text>
  <line x1="275.0" y1="349" x2="275.0" y2="353" stroke="currentColor"/>
  <text x="275.0" y="365" text-anchor="middle" font-size="12">20</text>
  <line x1="400.0" y1="349" x2="400.0" y2="353" stroke="currentColor"/>
  <text x="400.0" y="365" text-anchor="middle" font-size="12">40</text>
  <line x1="525.0" y1="349" x2="525.0" y2="353" stroke="currentColor"/>
  <text x="525.0" y="365" text-anchor="middle" font-size="12">60</text>
  <line x1="650.0" y1="349" x2="650.0" y2="353" stroke="currentColor"/>
  <text x="650.0" y="365" text-anchor="middle" font-size="12">80</text>
  <text x="10" y="390" font-size="12">시간축 0 = M 생성 시점 (ms). 막대 = 실제로 CPU1에서 돈 구간 (Image-trace, TCG 실측).</text>
  <text x="10" y="406" font-size="12">빨간 점선 = H가 락을 기다린 구간.</text>
</svg>
```

그림 7 — 실측 타임라인(세 번째 쌍, 0.6 ms 미만의 틈은 합쳐 그렸다). PI 없음: M(주황)이 L(초록)을 밀어내고 40 ms CPU를 다 쓴 뒤에야 L이 남은 20 ms를 하고 H가 락을 얻는다. PI: L이 H의 우선순위 30으로 부스트되어 M보다 먼저 끝까지 돌고, M은 H가 끝난 뒤에야 시작한다.

출력에서 볼 것:

- **PI 없음**: 순서가 `M start → M done → L unlock → H got`이다. 가장 높은 H가 **M의 40 ms 전체를 기다렸다**. M이 400 ms를 태우면 H도 400 ms를 기다린다 — 역전의 길이는 무관한 중간 태스크가 정한다. 이것이 "unbounded priority inversion"이다.
- **PI**: 순서가 `L unlock → H got → M start`다. M은 생성됐지만(`EV M created`) L이 FIFO 30으로 부스트된 동안 CPU1에서 한 번도 못 돌았다. H의 대기 = L의 남은 임계 구역(CPU 20 ms + TCG 잡음)만큼으로 **유계(bounded)** 가 되었다.
- 막대 사이의 틈(0.2 ms 이상 끊긴 구간)은 tick·커널 스레드·**호스트가 vCPU를 뺏은 시간**이다(§2.3). 절대값은 실제 하드웨어보다 크지만 순서는 결정적으로 같았다(6회 모두).

### 7.3 커널 안에서 — PI futex

`PTHREAD_PRIO_INHERIT` 뮤텍스가 경합하면 glibc는 `futex(FUTEX_LOCK_PI)`를 부른다. 커널은 `futex_lock_pi()` → `rt_mutex_start_proxy_lock()` 계열로 **커널 rt_mutex를 뮤텍스 소유자 대신 세운다.** 소유자 `task_struct`의 `pi_waiters` 트리에 H가 들어가고, `rt_mutex_adjust_prio_chain()`이 소유자의 실효 우선순위(`prio`)를 가장 높은 대기자 수준으로 올린다. 소유자가 다른 PI 락을 또 기다리면 체인을 따라 전파한다. F1 §12에서 본 `prio`·`normal_prio` 구분이 바로 이것이다. PREEMPT_RT 커널에서는 **커널 안의 `spinlock_t`·`mutex`도 같은 rt_mutex** 이므로, 드라이버 락에서도 같은 부스팅이 일어난다.

함정: PI는 **같은 CPU 위의 중간 태스크** 문제를 푼다. 락을 너무 오래 쥐는 설계, 중첩 락 순서, PI 없는 다른 동기화(조건 변수 대기, 세마포어, 파이프)는 PI로 해결되지 않는다. `sem_t`에는 PI가 없다.

---

## 8. 메모리 — page fault와 mlockall

### 8.1 예제 8 — 루프 안에서 처음 만지는 버퍼

무엇을 확인하나: 64 MiB 버퍼를 "루프 한 번에 256 KiB씩" 처음 써 나갈 때 반복당 시간과 page fault 수, 그리고 `mlockall(MCL_CURRENT|MCL_FUTURE)` 뒤 같은 일을 할 때.

```c
/* n1_mlock.c - cost of first-touch page faults inside a "control loop" vs mlockall */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/resource.h>

#define BUF   (64UL << 20)		/* 64 MiB working buffer */
#define CHUNK (256UL << 10)		/* each loop iteration writes 256 KiB */

static double us(void) {
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
static long minflt(void) {
	struct rusage ru; getrusage(RUSAGE_SELF, &ru); return ru.ru_minflt;
}
static void run(const char *name) {
	long f0 = minflt();
	double t0 = us();
	char *p = mmap(NULL, BUF, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) { perror("mmap"); exit(1); }
	madvise(p, BUF, MADV_NOHUGEPAGE);	/* 4 KiB faults, not 2 MiB THP */
	double tmap = us() - t0, max = 0, sum = 0;
	long f1 = minflt();
	int n = BUF / CHUNK;
	for (int i = 0; i < n; i++) {		/* one "iteration" of the loop */
		double a = us();
		memset(p + i * CHUNK, i, CHUNK);
		double d = us() - a;
		sum += d; if (d > max) max = d;
	}
	long f2 = minflt();
	printf("%-9s mmap %8.0f us (faults %6ld) | loop: avg %7.1f us  max %7.1f us  faults %6ld\n",
	       name, tmap, f1 - f0, sum / n, max, f2 - f1);
	munmap(p, BUF);
}
int main(void) {
	run("no-mlock");
	if (mlockall(MCL_CURRENT | MCL_FUTURE)) { perror("mlockall"); exit(1); }
	run("mlockall");	/* MCL_FUTURE: mmap() itself populates + locks the pages */
	return 0;
}
```

Image-trace 게스트, 두 번 실행:

```text
no-mlock  mmap      570 us (faults      1) | loop: avg  8854.6 us  max 233198.9 us  faults  16384
mlockall  mmap   191635 us (faults     32) | loop: avg    67.6 us  max   386.4 us  faults      0
no-mlock  mmap      304 us (faults      1) | loop: avg  9468.6 us  max 68422.8 us  faults  16384
mlockall  mmap    75256 us (faults     32) | loop: avg    82.8 us  max  1195.8 us  faults      0
```

출력에서 볼 것:

- **no-mlock**: `mmap()` 자체는 수백 µs로 끝난다(가상 주소만 예약). 대신 루프에서 **16384번 fault**(64 MiB / 4 KiB)가 났고, 반복 하나(256 KiB = 64페이지)에 평균 8.9~9.5 ms, 최악 68~233 ms가 걸렸다. 페이지마다 예외 → 페이지 할당 → 0으로 채움 → PTE 설정을 했기 때문이다.
- **mlockall**: `MCL_FUTURE` 때문에 `mmap()`이 그 자리에서 페이지를 다 붙이고 잠근다 — 비용이 **시작 시점으로 옮겨졌다**(75~192 ms, fault 32번). 루프 안 fault는 **0**, 반복당 평균 68~83 µs.
- mlockall 쪽 `faults 32`는 4 KiB가 아니라 2 MiB THP 단위로 채워졌다는 뜻이다(`madvise(MADV_NOHUGEPAGE)`를 `mmap()` 뒤에 불렀지만 MCL_FUTURE는 `mmap()` 안에서 이미 채운다). 반대로 `MADV_NOHUGEPAGE`를 안 넣은 첫 시험(Image-sec)에서는 no-mlock 쪽 fault가 32번뿐이었고 대신 한 번에 2 MiB를 0으로 채우느라 반복 하나가 최대 34 ms였다. **THP는 fault 횟수를 줄이지만 한 번의 비용을 키운다** — RT 루프에서는 둘 다 피해야 한다.
- 이 반복 시간(수 ms)은 TCG라서 크다. 실제 Cortex-A53/A72에서는 4 KiB fault 하나가 대략 1~수 µs(기억 기반 일반적 수치)이지만, 64페이지면 여전히 수십~수백 µs — 1 kHz 루프의 예산을 다 먹는다.

### 8.2 RT 앱의 메모리 체크리스트

1. 시작할 때 `mlockall(MCL_CURRENT | MCL_FUTURE)` (root 또는 `CAP_IPC_LOCK`, `ulimit -l` 확인 — VM 호스트에서 일반 사용자로 돌렸을 때 실제로 `mmap: Resource temporarily unavailable`이 났다. `RLIMIT_MEMLOCK` 때문이다).
2. 버퍼는 루프 전에 할당하고 한 번씩 써 둔다(prefault). 루프 안에서 `malloc`/`free` 금지 — glibc malloc은 내부 락과 `brk`/`mmap` syscall을 탈 수 있다.
3. 스택도 미리 만진다: RT 스레드 시작 때 큰 지역 배열을 `memset`해서 스택 페이지를 붙여 둔다. 스레드 스택 크기는 `pthread_attr_setstacksize`로 정한다.
4. 파일 매핑(코드 자체 포함)이 밀려나지 않게 하는 것도 `mlockall`이다. 메모리 압박이 있는 장비에서는 major fault가 ms 단위다.

---

## 9. RT throttling — SCHED_FIFO 무한 루프는 CPU를 100% 쥘 수 없다

### 9.1 예제 9 — busy loop가 실제로 받은 몫

무엇을 확인하나: CPU1에서 SCHED_FIFO 50 busy loop가 1초마다 실제로 몇 %를 돌았는지 — 혼자일 때, 같은 CPU에 SCHED_OTHER 루프가 있을 때, sysctl을 바꿨을 때. F1 §5.2는 `RT_GROUP_SCHED`가 꺼진 커널(fair_server만 있음)에서 SCHED_OTHER 쪽 몫을 쟀다. 여기 Image-trace는 **`CONFIG_RT_GROUP_SCHED=y`** 라서 고전적인 RT bandwidth 제한도 살아 있다.

```c
/* n1_rthog.c - SCHED_FIFO busy loop on CPU1: how much of each second do we really get? */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double ms(void) {
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
int main(int argc, char **argv) {
	int secs = argc > 1 ? atoi(argv[1]) : 3;
	cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(1, &cs);
	struct sched_param sp = { .sched_priority = 50 };
	if (sched_setaffinity(0, sizeof(cs), &cs)) { perror("affinity"); exit(1); }
	if (sched_setscheduler(0, SCHED_FIFO, &sp)) { perror("sched_setscheduler"); exit(1); }
	double start = ms(), last = start, sec0 = start, stolen = 0, maxgap = 0;
	int s = 0;
	while (s < secs) {
		double t = ms(), gap = t - last;
		if (gap > 0.5) { stolen += gap; if (gap > maxgap) maxgap = gap; }	/* we were off-CPU */
		last = t;
		if (t - sec0 >= 1000) {
			printf("sec %d: ran %.1f%%  off-CPU %.1f ms  longest gap %.1f ms\n",
			       s, 100.0 * (t - sec0 - stolen) / (t - sec0), stolen, maxgap);
			sec0 = t; stolen = 0; maxgap = 0; s++;
		}
	}
	return 0;
}
```

```sh
cat /proc/sys/kernel/sched_rt_period_us /proc/sys/kernel/sched_rt_runtime_us
/n1/n1_rthog 3                                        # A: 혼자
taskset -c 1 bash -c 'while :; do :; done' &          # SCHED_OTHER 경쟁자
/n1/n1_rthog 3                                        # B
echo 500000 > /proc/sys/kernel/sched_rt_runtime_us;  /n1/n1_rthog 3   # C
echo -1     > /proc/sys/kernel/sched_rt_runtime_us;  /n1/n1_rthog 3   # D
```

```text
1000000
950000
-- A: alone
sec 0: ran 95.6%  off-CPU 43.6 ms  longest gap 3.4 ms
sec 1: ran 94.3%  off-CPU 56.8 ms  longest gap 5.2 ms
sec 2: ran 92.3%  off-CPU 77.0 ms  longest gap 27.3 ms
-- B: + SCHED_OTHER loop on CPU1
sec 0: ran 86.7%  off-CPU 135.2 ms  longest gap 68.0 ms
sec 1: ran 66.7%  off-CPU 333.7 ms  longest gap 52.1 ms
sec 2: ran 68.6%  off-CPU 313.9 ms  longest gap 56.8 ms
/n1/run.sh: line 42: echo: write error: Invalid argument
-- C: runtime 500000 + loop
sec 0: ran 72.2%  off-CPU 278.5 ms  longest gap 54.3 ms
sec 1: ran 83.6%  off-CPU 164.6 ms  longest gap 57.9 ms
sec 2: ran 88.0%  off-CPU 119.6 ms  longest gap 54.5 ms
-- D: runtime -1 + loop
sec 0: ran 87.7%  off-CPU 122.7 ms  longest gap 54.9 ms
sec 1: ran 84.7%  off-CPU 153.3 ms  longest gap 60.8 ms
sec 2: ran 89.3%  off-CPU 107.0 ms  longest gap 50.7 ms
```

출력에서 볼 것:

- **A(혼자)**: 매초 92~96%만 돌았다. CPU1에 다른 할 일이 없는데도 1초 중 약 5%(950000/1000000의 나머지)를 **쉬었다** — RT_GROUP_SCHED의 고전 throttling은 "남에게 양보"가 아니라 "RT 그룹의 예산 소진 → 그 CPU의 RT 큐를 멈춤"이기 때문이다. 3초째의 27 ms 갭은 호스트 쪽 잡음이다.
- **B(경쟁자 있음)**: 67~87%로 더 떨어지고, 50~68 ms짜리 큰 갭이 생겼다. throttling 5%에 더해 6.12+의 **fair_server**(1초당 50 ms를 굶는 fair 태스크에게, F1 §5.2)가 같이 일하고, 남은 차이는 부하 걸린 TCG의 잡음이다(실행마다 크게 흔들렸다).
- **C**: `echo 500000`이 `Invalid argument`로 **거부**되었다. 6.18 소스를 보면 `sched_rt_global_constraints()`가 RT_GROUP_SCHED에서 `__rt_schedulable()`로 루트 태스크 그룹의 기존 예산(부팅 때 950000으로 잡힘)이 새 전역 값보다 크면 거부한다. 그리고 `sched_rt_do_global()`은 **빈 함수**다 — 이 버전에서 sysctl은 루트 그룹 예산을 다시 쓰지 않는다.
- **D**: `-1`은 받아들여졌지만 결과가 B와 거의 같았다(85~89%). 위와 같은 이유로 루트 그룹 예산은 그대로이고, fair_server도 그대로다. **"throttling 끄기 = `echo -1`"이라는 오래된 튜닝 가이드는 6.12+ 커널에서 그대로 믿으면 안 된다**(F1 §5.2와 같은 결론을, RT_GROUP_SCHED 켜진 커널에서도 확인).

실무 번역: RT 루프는 "1초에 한 번 최대 수십 ms 끊길 수 있다"를 전제로 설계하거나, 커널 설정(RT_GROUP_SCHED 여부, `/sys/kernel/debug/sched/fair_server/cpuN/runtime`)을 의도적으로 정하고 문서화한다. 반대로 이 장치 덕분에 RT 스레드가 무한 루프에 빠져도 ssh로 들어가 죽일 수 있다. 대부분의 제품에서는 끄지 말고 "RT 스레드는 절대 1초에 950 ms 이상 돌지 않는다"를 설계 규칙으로 삼는다. 컨테이너 안 RT(cgroup별 `cpu.rt_runtime_us`)는 L8.

---

## 10. PREEMPT_RT가 주지 않는 것 — 그리고 언제 무엇을 쓰나

### 10.1 RT 커널을 켜도 남는 것

- **WCET 보장은 없다.** RT 커널은 "선점 불가 구간을 짧게 만드는" 도구다. 최악값이 얼마인지는 여전히 **그 보드에서 측정**해야 한다(N3). 인증(IEC 61508, ISO 26262, DO-178C) 수준의 hard RT 증명은 Linux 커널로는 현실적으로 불가능하다고 보는 것이 업계의 일반적인 입장이다.
- **드라이버의 긴 raw 구간.** 벤더 BSP 드라이버가 `raw_spin_lock_irqsave()` 안에서 레지스터 폴링을 1 ms 하면 RT 커널에서도 그 CPU는 1 ms 막힌다. §5.3에서 정확히 이 현상을 만들어 봤다. GPU·NPU·Wi-Fi 같은 큰 out-of-tree 드라이버가 단골이다.
- **`preempt_disable()`/`local_irq_disable()` 직접 호출.** 변환 대상이 아니다.
- **펌웨어와 하드웨어.** SMI, EL3/TEE, cpuidle, DVFS, thermal, DRAM refresh·경합 — 커널 밖의 일이다(§3.6).
- **유저 공간 설계 실수.** page fault, 우선순위 역전(PI 없는 뮤텍스), 로깅을 RT 스레드에서 `printf`로 하는 것, `malloc` — RT 커널이 대신 고쳐 주지 않는다(§7, §8).
- **처리량 비용.** 락이 잠드는 락이 되고 IRQ마다 스레드 전환이 생긴다. §6.3 hackbench에서 실측했다(TCG에서 약 3배). 네트워크·스토리지가 바쁜 장비에서는 체감될 수 있다.

### 10.2 선택지 비교

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <line x1="20" y1="30" x2="660" y2="30" stroke="currentColor" marker-end="url(#ah8)"/>
  <text x="20" y="20" font-size="12">요구가 느슨함</text>
  <text x="660" y="20" text-anchor="end" font-size="12">더 짧고 더 확실한 deadline</text>
  <rect x="10" y="45" width="158" height="250" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="89" y="70" text-anchor="middle" font-size="13">stock Linux</text>
  <text x="89" y="90" text-anchor="middle" font-size="12">PREEMPT/LAZY + 튜닝</text>
  <line x1="20" y1="102" x2="158" y2="102" stroke="#888"/>
  <text x="20" y="128" font-size="12">최악 지연 수 ms 허용</text>
  <text x="20" y="168" font-size="12">가끔 놓쳐도 품질만 저하</text>
  <text x="20" y="208" font-size="12">UI, 영상 녹화, 로깅</text>
  <text x="20" y="248" font-size="12">예: 30 fps 카메라 저장</text>
  <rect x="178" y="45" width="158" height="250" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="257" y="70" text-anchor="middle" font-size="13">Linux PREEMPT_RT</text>
  <text x="257" y="90" text-anchor="middle" font-size="12">+ N2 튜닝 + N3 측정</text>
  <line x1="188" y1="102" x2="326" y2="102" stroke="#3f9a6b"/>
  <text x="188" y="128" font-size="12">최악 ~50–200 µs 목표</text>
  <text x="188" y="168" font-size="12">부하 상태로 몇 시간 측정</text>
  <text x="188" y="208" font-size="12">1 kHz 이하 제어 루프</text>
  <text x="188" y="248" font-size="12">예: EtherCAT 마스터</text>
  <rect x="346" y="45" width="158" height="250" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="425" y="70" text-anchor="middle" font-size="13">dual-kernel</text>
  <text x="425" y="90" text-anchor="middle" font-size="12">Xenomai 3 / EVL</text>
  <line x1="356" y1="102" x2="494" y2="102" stroke="#e08a3c"/>
  <text x="356" y="128" font-size="12">최악 수~수십 µs</text>
  <text x="356" y="168" font-size="12">별도 API, 드라이버 이식</text>
  <text x="356" y="208" font-size="12">레거시 산업 장비</text>
  <text x="356" y="248" font-size="12">유지보수 비용 큼</text>
  <rect x="514" y="45" width="158" height="250" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="593" y="70" text-anchor="middle" font-size="13">MCU / RTOS</text>
  <text x="593" y="90" text-anchor="middle" font-size="12">FreeRTOS · Zephyr</text>
  <line x1="524" y1="102" x2="662" y2="102" stroke="#4a7bd0"/>
  <text x="524" y="128" font-size="12">µs 단위, 사이클 계산</text>
  <text x="524" y="168" font-size="12">WCET 분석·인증 필요</text>
  <text x="524" y="208" font-size="12">모터 FOC, 안전 정지</text>
  <text x="524" y="248" font-size="12">Linux는 명령만 (N4)</text>
  <text x="10" y="318" font-size="12">수치는 흔히 인용되는 경험적 범위(기억 기반). 실제 경계는 SoC·펌웨어·드라이버가 정한다 — 측정할 것.</text>
</svg>
```

그림 8 — 요구 수준에 따른 선택. 오른쪽으로 갈수록 보장은 강해지고 개발·유지 비용이 커진다.

| 선택지 | 구조 | 장점 | 단점 |
|---|---|---|---|
| stock Linux (PREEMPT/LAZY) | 그대로 | 생태계 전부, 처리량 | 최악값 관리 안 됨 |
| Linux PREEMPT_RT | 같은 커널, 설정만 다름 (6.12+ mainline) | 같은 API(POSIX), 같은 드라이버. 최악값이 작고 측정 가능 | WCET 증명 불가, 드라이버 품질에 좌우 |
| dual-kernel (Xenomai 3 Cobalt, EVL, 과거 RTAI) | Linux 아래에 작은 RT 코어(I-pipe/Dovetail)가 인터럽트를 먼저 받는다 | 수 µs 단위 최악값 | 별도 API·RT 드라이버(RTDM) 필요, 커널 버전 추종 부담, 커뮤니티 작음 |
| 이종 코어 (Cortex-A + M/R) | 같은 SoC의 MCU 코어에서 RTOS, Linux와 rpmsg (O1, O2) | 진짜 hard RT + Linux 생태계 | 두 소프트웨어 스택, 통신 설계 |
| 외부 MCU | 별도 칩에 RTOS, UART/SPI/CAN으로 연결 (O3) | 격리 최고, 안전 인증 가능 | BOM, 펌웨어 업데이트 경로 |

### 10.3 경험적 경계 (수치는 일반적으로 인용되는 값, 기억 기반 — 반드시 자기 보드에서 측정)

- 잘 고른 x86·arm64 SoC + PREEMPT_RT + 기본 튜닝(N2): cyclictest 최악값 **수십 µs ~ 100 µs대**가 흔히 보고된다. 나쁜 드라이버·펌웨어가 있으면 수백 µs ~ ms.
- stock PREEMPT 커널: 평균은 비슷하게 수 µs ~ 수십 µs지만 부하 상태 최악값은 **수백 µs ~ 수 ms**.
- 그래서 실무 감각: **주기 1 ms(1 kHz) 이하이고, 가끔의 수백 µs 지연을 견딜 수 있는 루프 = Linux RT 가능. 10 kHz 이상 전류 루프, 놓치면 하드웨어가 타는 루프 = MCU.** 로봇이라면 "상위 제어·경로 계획·센서 융합(100 Hz ~ 1 kHz)은 Linux RT, 모터 FOC(10~40 kHz)와 안전 정지는 MCU"가 표준 분업이다(N4).

---

## 11. 커널 안에서는 무슨 일이 일어나나

RT 태스크 하나가 타이머로 깨어나는 경로를 non-RT와 RT로 나란히 놓으면:

| 단계 | PREEMPT (full) | PREEMPT_RT |
|---|---|---|
| 타이머 IRQ | arch timer PPI → `el1h_64_irq` → `hrtimer_interrupt()` (hardirq) | 같음 (per-CPU 타이머는 스레드화 예외) |
| 콜백 | `hrtimer_wakeup()`가 hardirq에서 `wake_up_process()` | `__hrtimer_setup_sleeper()`가 호출자가 RT·DL 정책이면(`rt_or_dl_task_policy(current)`) sleeper를 HARD 모드로 바꾼다 → hardirq에서 바로 깨움. SCHED_OTHER의 sleep과 다른 hrtimer는 `ktimers/N`에서 |
| 깨운 뒤 | `TIF_NEED_RESCHED` → IRQ 복귀 시 `preempt_schedule_irq()`. 단 그 순간 preempt count ≠ 0이면 `preempt_enable()`까지 대기 | 같은 경로지만 preempt count가 0이 아닌 구간이 훨씬 짧다 (spinlock_t 구간이 선점 가능) |
| 다른 IRQ 처리 중이었다면 | handler 끝 + softirq 끝까지 대기 | handler는 `irq/N` 스레드(FIFO 50)라 FIFO 80 태스크가 즉시 선점 |
| 락 경합 | spinlock은 busy-wait, mutex는 PI 없음(`mutex`는 RT에서만 rt_mutex 기반) | `spinlock_t`·`mutex`·`rwsem` 모두 PI. 소유자가 깨어난 RT 태스크 우선순위로 부스트 |

관련 구조체·함수: `struct rt_mutex_base`(`kernel/locking/rtmutex.c`), `task_struct::pi_waiters`·`pi_blocked_on`(PI 체인), `rt_mutex_adjust_prio_chain()`(PI 전파), `irq_thread()`/`irq_setup_forced_threading()`(`kernel/irq/manage.c`), `softirq_ctrl`·`ktimers`(`kernel/softirq.c`), `migrate_disable()`(`kernel/sched/core.c`), nbcon(`kernel/printk/nbcon.c`).

드라이버를 쓸 때 다시 만나는 지점:

- **G·H 모듈 드라이버**: 락을 고를 때 "RT 커널에서 이 `spin_lock()`이 잠들어도 되나?"를 묻는다. hardirq 문맥에서 꼭 돌아야 하는 짧은 구간만 `raw_spinlock_t`. 나머지는 `spinlock_t` 또는 `mutex`.
- **IRQ handler**: `request_threaded_irq()`로 처음부터 나눠 두면 RT·non-RT에서 동작이 같다. 센서 드라이버는 I2C/SPI 전송 때문에 어차피 threaded다(F6).
- **스레드 우선순위**: RT 커널에서 `irq/N` 스레드의 우선순위가 시스템 설계의 일부가 된다. IMU IRQ 스레드는 제어 스레드보다 높게, Wi-Fi·USB IRQ 스레드는 낮게(`chrt -f -p`, N2).
- **M8 lockdep**: `CONFIG_PROVE_RAW_LOCK_NESTING`이 non-RT 커널에서도 "raw 안에서 spinlock_t를 잡는" RT 위반을 잡아 준다 — RT 커널을 안 쓰더라도 켜 볼 가치가 있다.

---

## 12. Embedded 실무에서 다시 보기

### 12.1 장면 1 — "1 kHz 제어 루프가 가끔 놓쳐요" 어디서부터 보나

순서가 답의 품질이다. 측정 없이 RT 커널부터 깔지 않는다.

1. **놓침을 정의하고 측정한다**: 루프 안에서 `clock_gettime(CLOCK_MONOTONIC)`으로 매 주기 wakeup 지연과 실행 시간을 기록(히스토그램). "깨어남이 늦었나(스케줄링)"와 "깨어났지만 계산이 길었나(실행 시간·캐시·fault)"를 가른다.
2. **기본 위생**: SCHED_FIFO인가, `mlockall` + prefault 했나(§8), PI 뮤텍스인가(§7), 루프 안에 `printf`·`malloc`·파일 I/O·blocking 호출이 없나.
3. **플랫폼 기준선**: 같은 보드에서 cyclictest를 **부하를 걸고** 돌려(N3) 커널+HW가 주는 최악값을 잰다. 이 값이 이미 deadline에 가까우면 앱이 아니라 플랫폼 문제다.
4. **원인 찾기**: `rtla timerlat`(IRQ 지연과 스레드 지연을 분리, 원인 스택 자동 수집), ftrace `irqsoff`/`preemptoff`/`wakeup_rt`(M5), `osnoise`/`hwlat`(펌웨어·HW가 가져간 시간).
5. **처방**: 커널 모델(PREEMPT → PREEMPT_RT), CPU isolation·IRQ affinity·`nohz_full`·governor·cpuidle(N2), 문제 드라이버 수정, 그래도 안 되면 MCU로 분리(N4).

### 12.2 장면 2 — 벤더 BSP를 RT로 바꿨더니 부팅 중 BUG

`BUG: sleeping function called from invalid context`가 어떤 드라이버의 hardirq handler(`IRQF_NO_THREAD`) 또는 `raw_spin_lock` 안의 `spin_lock()`에서 난다. RT에서는 그 `spin_lock()`이 잠들 수 있는 락이기 때문이다. 고치는 법은 F4·F2 §13.4: 그 락을 정말 hardirq에서 잡아야 하면 `raw_spinlock_t`로(구간을 짧게), 아니면 handler를 threaded로.

### 12.3 장면 3 — RT 커널인데 cyclictest 최악값이 1초마다 한 번 튄다

1초 주기라면 RT throttling(§9)이나 주기적인 펌웨어 작업(SMI·thermal 폴링)을 의심한다. `sched_rt_runtime_us`, `/sys/kernel/debug/sched/fair_server`, `hwlat` tracer 순으로 본다. 10 ms·4 ms 주기라면 tick이나 특정 드라이버 타이머.

### 12.4 장면 4 — 로그 스레드 하나가 제어 루프를 망친다

제어 스레드(FIFO 80)와 로그 스레드(SCHED_OTHER)가 같은 링버퍼를 일반 `pthread_mutex`로 공유. 로그 스레드가 락을 쥔 채 선점되면 역전(§7). 처방: PI 뮤텍스, 또는 lock-free SPSC 링버퍼(Don이 SSD 펌웨어에서 쓰던 그 구조)로 바꿔 락 자체를 없앤다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 평균 latency로 RT 여부를 판단 | 데모는 되는데 필드에서 가끔 놓침 | RT는 최악값 문제 | 부하 상태로 장시간 max·히스토그램 측정 (N3) |
| idle 상태에서만 cyclictest | 최악값이 너무 좋게 나옴 | 부하가 없으면 경합 경로가 안 탄다 | stress-ng·hackbench·실제 워크로드와 함께 |
| PI 없는 `pthread_mutex`를 RT 스레드와 공유 | 높은 스레드가 수십 ms 멈춤 | 우선순위 역전 (§7) | `PTHREAD_PRIO_INHERIT` 또는 lock-free |
| `mlockall` 없이 큰 버퍼를 루프에서 처음 사용 | 첫 몇 주기만 크게 늦음, 메모리 압박 시 재발 | demand paging (§8) | `mlockall(MCL_CURRENT or MCL_FUTURE)` + prefault + 스택 미리 터치 |
| 드라이버에서 `raw_spinlock_t` 남용 | RT 커널인데 ms 단위 튐 | raw 구간은 RT에서도 선점 불가 (§5.3) | `spinlock_t`·mutex로, raw는 µs 구간만 |
| RT 루프를 FIFO 99로 | 커널 watchdog·migration 스레드와 경쟁, 디버그 불가 | 99는 커널용 관례 | 앱은 FIFO 1~89 범위에서 설계 |
| `sched_rt_runtime_us=-1`로 throttling을 끄면 끝이라 생각 | 6.12+에서 효과가 없거나, 폭주 시 시스템 먹통 | fair_server, RT_GROUP_SCHED 여부 (§9, F1 §5.2) | 커널 설정을 확인하고 의도적으로 결정 |
| VM·QEMU에서 잰 지연을 보고 | 숫자가 무의미 | vCPU가 호스트 스레드 (§2.3) | 실제 보드에서 측정 |
| RT 커널이면 끝이라 생각 | 여전히 튐 | 펌웨어·드라이버·cpuidle·DVFS (§3.6) | 원인 분리 후 N2 튜닝 |

---

## 14. 면접에서 이렇게 말한다

**Q.** "Is Linux real-time?"

**A.** "기본 커널은 soft RT다. 평균은 빠르지만 최악 지연에 상한이 관리되지 않는다. PREEMPT_RT(6.12부터 mainline)를 켜면 spinlock이 PI 뮤텍스로, IRQ handler와 softirq가 스레드로 바뀌어 선점 불가 구간이 아주 작아지고, 좋은 하드웨어에서 최악 지연이 수십~100 µs대로 측정된다. 하지만 WCET 증명은 안 되므로 '측정된 최악값이 deadline보다 충분히 작은가'로 판단하고, 진짜 hard RT·안전 기능은 MCU에 둔다."

> Out of the box, Linux is soft real-time: great average latency, but no managed bound on the worst case. With PREEMPT_RT, which is in mainline since 6.12, spinlocks become priority-inheriting sleeping locks and interrupt handlers and softirqs run in threads, so the non-preemptible regions shrink to a small set of raw spinlocks and low-level code. On decent hardware you typically measure worst-case wakeup latencies in the tens of microseconds to around a hundred. It's still not provable WCET, so I treat it as firm real-time: I measure the worst case under load for hours, and anything truly hard real-time or safety-critical goes on an MCU.

**꼬리 질문**: "What's the difference between hard and soft real-time?" → 보장의 강도. hard는 놓치면 실패, soft는 품질 저하. 속도와는 별개.

**Q.** "What does PREEMPT_RT actually change?"

**A.** "네 가지다. 첫째 `spinlock_t`·`rwlock_t`가 rt_mutex 기반의 잠드는 락이 되고 우선순위 상속이 붙는다. 쥔 동안에도 선점되고 migration만 막는다. `spin_lock_irqsave`는 IRQ를 끄지 않는다. 둘째 IRQ handler가 강제로 `irq/N` 스레드에서 돈다. 셋째 softirq도 스레드 문맥에서 처리되고 timer는 `ktimers` 스레드로 간다. 넷째 printk의 콘솔 출력이 전용 스레드로 옮겨져(nbcon 콘솔은 `pr/<이름>`, legacy 콘솔은 RT에서 `pr/legacy`) 지연을 깨지 않는다. `raw_spinlock_t`만 진짜 spinlock으로 남는다. 실제로 QEMU에서 FIFO 1 kthread가 spinlock_t를 20 ms 쥐어도 FIFO 80 태스크는 바로 돌았고, raw_spinlock_t로 바꾸면 20 ms 막혔다."

> It converts spinlock_t and rwlock_t into rt_mutex-based sleeping locks with priority inheritance, so a lock holder can be preempted — it only disables migration — and spin_lock_irqsave no longer disables interrupts. It force-threads interrupt handlers, so they run as SCHED_FIFO kernel threads you can prioritize. Softirqs run in thread context, timers in ktimers threads. And printk moved to the nbcon console model so printing doesn't stall the system. raw_spinlock_t stays a real spinning lock for the few paths that must never sleep, like the scheduler and irqchip code. That's also the trap: a driver holding a raw spinlock for a millisecond still blocks that CPU for a millisecond.

**꼬리 질문**: "What breaks when you move a driver to RT?" → hardirq 문맥이나 raw lock 안에서 `spin_lock()`·`kmalloc`을 부르는 코드가 "sleeping function called from invalid context"로 터진다.

**Q.** "Our 1 kHz control loop sometimes misses its deadline. Where do you start?"

**A.** "먼저 측정한다 — 루프 안에서 매 주기 깨어남 지연과 실행 시간을 히스토그램으로 남겨 스케줄링 문제인지 실행 시간 문제인지 가른다. 그다음 기본 위생: SCHED_FIFO, mlockall과 prefault, PI 뮤텍스, 루프 안에 printf·malloc·blocking I/O가 없는지. 그다음 같은 보드에서 부하를 걸고 cyclictest·rtla timerlat으로 플랫폼 최악값을 잰다. 플랫폼이 문제면 ftrace irqsoff/wakeup_rt·osnoise로 원인 드라이버나 펌웨어를 찾고, PREEMPT_RT·CPU isolation·IRQ affinity·cpuidle 제한을 적용한다. 그래도 안 되면 그 루프는 MCU 몫이다."

> First I'd instrument the loop itself — per-cycle wakeup latency and execution time into a histogram — to separate 'woke up late' from 'ran too long'. Then the basics: SCHED_FIFO priority, mlockall plus prefaulting, priority-inheritance mutexes, and no printf, malloc or blocking I/O inside the loop. Next I'd measure the platform baseline with cyclictest and rtla timerlat under realistic load. If the platform's worst case is the problem, I'd use the irqsoff and wakeup tracers or osnoise to find the culprit — a driver, a firmware call, idle states — and then apply PREEMPT_RT, CPU isolation, IRQ affinity and PM QoS. If it still can't hold the bound with margin, that loop belongs on a microcontroller.

**Q.** "Explain priority inversion and how Linux handles it."

**A.** "낮은 태스크가 쥔 락을 높은 태스크가 기다리는 동안 중간 태스크가 낮은 태스크를 밀어내면, 높은 태스크가 중간 태스크만큼 추가로 늦어진다. 유저 공간에서는 `PTHREAD_PRIO_INHERIT`가 `FUTEX_LOCK_PI`로 커널 rt_mutex를 써서 락 소유자를 기다리는 쪽 우선순위로 부스트한다. QEMU 실측에서 PI 없이 65~99 ms 막히던 높은 스레드가 PI를 켜면 22~33 ms, 즉 낮은 스레드의 남은 임계 구역만큼만 기다렸다(PREEMPT·RT 커널 모두 같은 경향 — 유저 공간 PI futex는 RT 전용 기능이 아니다). PREEMPT_RT 커널은 커널 안의 spinlock·mutex에도 같은 PI를 건다."

> Priority inversion is when a high-priority task waits for a lock held by a low-priority task, and a medium-priority task preempts the holder, so the high task effectively waits for the medium one. On Linux you fix it in user space with PTHREAD_PRIO_INHERIT mutexes, which use PI futexes backed by the kernel's rt_mutex, so the holder is boosted to the waiter's priority. Under PREEMPT_RT the kernel's own spinlocks and mutexes get the same treatment.

**꼬리 질문**: "PI의 단점은?" → 체인 전파 비용, 중첩 락에서 복잡해짐, 우선순위 ceiling(`PTHREAD_PRIO_PROTECT`)은 대안.

**Q.** "Why mlockall in a real-time application?"

**A.** "Linux는 메모리를 처음 만질 때 page fault로 페이지를 붙이고, 메모리가 부족하면 파일 백업 페이지를 내쫓았다가 다시 읽는다. RT 루프 안에서 그게 일어나면 µs~ms 지연이다. `mlockall(MCL_CURRENT|MCL_FUTURE)`로 모든 매핑을 RAM에 고정하고 시작할 때 버퍼와 스택을 미리 만져 둔다. 실측에서 64 MiB를 루프 안에서 처음 쓰면 16384번 fault가 났고, mlockall 후에는 0번이었다."

> Linux allocates pages lazily on first touch and can evict file-backed pages under memory pressure, so a page fault in the control loop costs microseconds to milliseconds. mlockall with MCL_CURRENT and MCL_FUTURE pins and pre-populates every mapping; I also pre-touch the stack and preallocate buffers at startup so the loop never faults.

**Q.** "What is RT throttling?"

**A.** "RT 태스크가 폭주해서 CPU를 100% 쥐는 것을 막는 안전장치다. 고전적으로는 `sched_rt_period_us`/`sched_rt_runtime_us`가 1초 중 0.95초만 RT에게 준다. 6.12부터는 RT_GROUP_SCHED가 없는 커널에서 fair_server라는 deadline 엔티티가 굶는 일반 태스크에게 1초당 50 ms를 준다. 그래서 RT 설계에서는 'RT 태스크가 1초에 한 번 최대 50 ms 끊길 수 있다'는 사실을 알고 있어야 한다."

> It's a safety net so a runaway SCHED_FIFO task can't starve the rest of the system. Classically, sched_rt_runtime_us of 950,000 per 1-second period leaves 5% for normal tasks. Since 6.12 the fair deadline server does that job unless RT group scheduling is configured, so on modern kernels the knob you change is the fair_server, not just the sysctl. Either way, an RT loop must tolerate or explicitly configure that 50 ms window.

**Q.** "When would you not use Linux for real-time?"

**A.** "deadline이 수십 µs 이하이거나, 놓치면 안전 문제가 생기거나, 인증이 필요하면 MCU·RTOS다. 모터 FOC, 안전 정지, 전원 시퀀싱. Linux는 그 위에서 경로 계획·센서 융합·통신을 하고 MCU에 명령을 보낸다."

> When the deadline is in the tens of microseconds, when a miss is a safety hazard, or when the function needs certification. Motor current loops, emergency stop, power sequencing go on an MCU or a Cortex-R/M core with an RTOS; Linux does planning, fusion and comms and sends setpoints down.

---

## 15. 직접 해보기

1. (개념) "평균 8 µs·최악 2 ms" 시스템과 "평균 30 µs·최악 80 µs" 시스템 중 500 µs deadline 제어 루프에 맞는 것은? 정답/힌트: 후자. RT는 최댓값과 여유(margin)로 판단한다.
2. (코드) 예제 7(`n1_pi`)에서 Medium의 burn을 40 ms → 200 ms로 늘리면 PI 없는 경우 High의 대기 시간은? PI 켠 경우는? 정답/힌트: 없으면 약 200 ms + L 잔여, 있으면 L 잔여만. 직접 돌려 확인.
3. (코드) `n1_pi`에서 High만 CPU0에 두면(`start()`에 CPU 인자를 추가) PI 없는 경우가 나아지나? 정답/힌트: 나아지지 않는다. High가 기다리는 것은 CPU가 아니라 L이 쥔 락이고, L은 여전히 CPU1에서 M에게 밀린다. 역전은 "락 + 같은 CPU의 중간 태스크"의 문제다. PI를 켜면 L이 FIFO 30으로 부스트되어 M을 이긴다.
4. (관찰) RT 커널 게스트에서 `ps -eo pid,cls,rtprio,comm` 으로 `irq/` 스레드를 찾고, `/proc/interrupts`의 같은 번호와 짝지어 보라. virtio-blk IRQ 스레드를 `chrt -f -p 10 <pid>`로 낮추면 무엇이 달라질까? 정답/힌트: 디스크 완료 처리가 FIFO 10보다 높은 RT 태스크에 밀린다 — I/O 지연이 늘어난다.
5. (코드) 예제 4의 모듈에서 `raw=1`일 때 `hold_ms`를 1, 5, 20으로 바꿔 cyclictest Max가 그대로 따라오는지 확인하라. 정답/힌트: raw 구간 길이 ≈ 최악 지연. 이것이 "RT의 최악값 = 가장 긴 raw 구간"의 직접 증거다.
6. (관찰) 두 커널에서 `cat /proc/softirqs`를 부하 전후로 찍고, RT 커널에서 `ktimers/N`·`ksoftirqd/N`의 CPU 시간(`ps -o time`)이 어떻게 늘어나는지 보라. 정답/힌트: RT에서는 timer softirq가 `ktimers`의 몫으로 잡힌다.
7. (설계) 1 kHz IMU 융합 + 20 kHz 모터 FOC + 30 fps 카메라 녹화 장비를 Linux RT / MCU / stock Linux로 나눠 보라. 정답/힌트: FOC → MCU, IMU 융합·상위 제어 → Linux RT(FIFO, 격리 CPU), 카메라 → 일반 스레드(N4).

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| latency | 지연 | 사건부터 처리 코드 시작까지 걸린 시간 |
| scheduling (wakeup) latency | 스케줄링 지연 | 태스크가 깨어나야 할 시각부터 실제로 도는 시각까지 |
| jitter | 지터 | latency·주기의 흔들림 |
| WCET | 최악 실행 시간 | 어떤 조건에서도 넘지 않는 실행 시간 상한 |
| hard / firm / soft RT | 경성 / 준경성 / 연성 실시간 | deadline 위반의 결과가 실패 / 버림 / 품질 저하 |
| deterministic | 결정론적 | 같은 입력에 같은 시간. RT의 목표 |
| PREEMPT_RT | 완전 선점 RT 커널 | spinlock→rt_mutex, IRQ·softirq 스레드화. 6.12 mainline |
| rt_mutex | RT 뮤텍스 | 우선순위 상속을 지원하는 커널 뮤텍스 |
| priority inheritance (PI) | 우선순위 상속 | 락 소유자를 기다리는 쪽 우선순위로 일시 상향 |
| priority inversion | 우선순위 역전 | 높은 태스크가 낮은 태스크 때문에 중간 태스크보다 늦게 도는 현상 |
| raw_spinlock_t | 진짜 spinlock | RT에서도 busy-wait + 선점 금지로 남는 락 |
| threaded IRQ | 스레드형 IRQ | handler 본문이 `irq/N-name` 커널 스레드에서 도는 것 |
| ktimers | 타이머 softirq 스레드 | RT에서 timer/hrtimer softirq를 처리하는 per-CPU 스레드 |
| nbcon | non-BKL console | printk 출력 스레드 + atomic 경로. RT 편입의 마지막 조각 |
| migrate_disable | 이동 금지 | 선점은 허용하되 다른 CPU로 옮기지 않음 |
| mlockall | 메모리 고정 | 프로세스 매핑 전체를 RAM에 고정, page fault 방지 |
| RT throttling | RT 제한 | RT 태스크가 CPU를 100% 쥐지 못하게 하는 안전장치 |
| fair_server | fair 서버 | 6.12+에서 굶는 fair 태스크에 1초당 50 ms를 주는 deadline 엔티티 |
| SMI | 시스템 관리 인터럽트 | x86 펌웨어가 OS 몰래 CPU를 쓰는 인터럽트 |
| dual-kernel | 이중 커널 | Linux 아래 작은 RT 코어를 두는 방식 (Xenomai, EVL) |
| cyclictest | 주기 지연 측정기 | 주기적으로 잠들었다 깨며 wakeup latency를 재는 rt-tests 도구 |

---

## 17. 요약 & 체크리스트

실시간은 "빠름"이 아니라 "최악값에 상한이 있고 그 상한이 deadline보다 작음"이다. Linux의 지연은 IRQ부터 유저 태스크까지의 사슬 각 구간에서 새고 — IRQ off, preempt off(non-RT spinlock), softirq, 역전, page fault, 캐시, cpuidle, SMI/EL3/TEE, hypervisor — 일반 커널은 그 최악값을 관리하지 않는다. PREEMPT_RT(6.12 mainline, arm64 지원)는 `spinlock_t`를 PI가 있는 잠드는 락으로, IRQ handler와 softirq를 스레드로, printk를 nbcon으로 바꿔 선점 불가 구간을 raw spinlock 등 소수로 줄인다. 그래도 WCET 증명, raw 구간이 긴 드라이버, 펌웨어 지연, 유저 공간 설계 실수는 남는다. 그래서 측정(부하 상태, 장시간, 실제 보드)으로 판단하고, 진짜 hard RT는 MCU에 둔다. QEMU TCG에서는 vCPU가 호스트 스레드라 절대 수치가 무의미하지만, **구조적 효과**(spinlock_t vs raw, PI, mlockall, throttling)는 분명히 재현된다.

- [ ] latency, jitter, throughput, WCET를 구분해 설명할 수 있다
- [ ] hard/firm/soft RT를 예시와 함께 설명할 수 있다 ("빠름"과 혼동하지 않는다)
- [ ] 지연 사슬 8단계와 각 단계의 지연 원인을 그릴 수 있다
- [ ] preemption 모델 5개의 차이를 "커널 코드를 어디서 끊을 수 있나"로 말할 수 있다
- [ ] PREEMPT_RT가 바꾸는 것 4가지(락, IRQ, softirq, printk)와 안 바꾸는 것(raw lock, 펌웨어)을 말할 수 있다
- [ ] RT 커널에서 `spin_lock_irqsave()`가 IRQ를 끄지 않는 이유를 설명할 수 있다
- [ ] PI 뮤텍스로 우선순위 역전을 막는 코드를 짤 수 있다
- [ ] `mlockall` + prefault가 왜 필요한지 수치로 말할 수 있다
- [ ] RT throttling과 6.12+ fair_server의 차이를 설명할 수 있다
- [ ] cyclictest를 idle·부하에서 돌려 max를 비교하고, VM 측정의 한계를 설명할 수 있다
- [ ] "1 kHz 루프가 가끔 놓친다"에 측정 → 위생 → 플랫폼 기준선 → 원인 → 처방 순서로 답할 수 있다
- [ ] Linux RT / dual-kernel / MCU 선택 기준을 말할 수 있다

## 참고 자료

- docs.kernel.org — Real-time preemption: https://docs.kernel.org/core-api/real-time/index.html (6.12+ 문서. "Differences with non-RT kernels" 절)
- docs.kernel.org — Lock types and their rules: https://docs.kernel.org/locking/locktypes.html (spinlock_t·raw_spinlock_t·local_lock의 RT 의미)
- docs.kernel.org — Real-Time group scheduling: https://docs.kernel.org/scheduler/sched-rt-group.html
- docs.kernel.org — RT-mutex subsystem with PI support: https://docs.kernel.org/locking/rt-mutex.html
- Linux Foundation Real-Time Linux wiki: https://wiki.linuxfoundation.org/realtime/start (cyclictest, HOWTO, 역사)
- rt-tests (cyclictest, hackbench): https://git.kernel.org/pub/scm/utils/rt-tests/rt-tests.git
- man7.org: `mlockall(2)`, `sched(7)`, `pthread_mutexattr_setprotocol(3p)`, `clock_nanosleep(2)`
- 소스 (elixir.bootlin.com, v6.18): `kernel/Kconfig.preempt`, `include/linux/spinlock_types.h`, `include/linux/spinlock_rt.h`, `kernel/locking/spinlock_rt.c`, `kernel/irq/manage.c` (`irq_setup_forced_threading`), `kernel/softirq.c` (ktimers), `kernel/printk/nbcon.c`, `drivers/tty/serial/amba-pl011.c`
- Xenomai: https://xenomai.org , EVL: https://evlproject.org
- 이 사이트의 F1(§5.2 RT throttling, §7.2 preemption 모델), F2(§6·§7), F4(§4.4), F6, F8, N2, N3, N4
