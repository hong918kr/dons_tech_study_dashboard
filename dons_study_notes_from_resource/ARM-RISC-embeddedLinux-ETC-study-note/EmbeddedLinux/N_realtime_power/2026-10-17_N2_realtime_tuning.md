# N2. 실시간 튜닝 — SCHED_FIFO/DEADLINE, 우선순위 설계, CPU 격리(isolcpus·nohz_full·cpuset), IRQ affinity, mlockall

> **이 노트를 다 읽으면**: 1 kHz 제어 루프를 `clock_nanosleep(TIMER_ABSTIME)` + `mlockall` + prefault + `SCHED_FIFO` + CPU 고정으로 정석대로 짜고 자기 지터 히스토그램을 찍을 수 있다 · 로봇 컴퓨터의 스레드/IRQ 스레드/커널 스레드 우선순위 지도를 그리고 `chrt`로 실제로 적용할 수 있다 · `isolcpus`·`nohz_full`·`rcu_nocbs`·`irqaffinity`와 cgroup v2 cpuset `isolated` 파티션의 차이를 말하고, 격리가 "실제로 먹혔는지"를 `/sys`·`/proc/interrupts`로 증명할 수 있다 · SCHED_DEADLINE의 admission control, PM QoS(`/dev/cpu_dma_latency`), "printf 대신 SPSC 링" 같은 실무 규칙을 측정 결과와 함께 설명할 수 있다
> **왜 필요한가**: 로보틱스·드론·국방 회사 면접에서 "Design the thread/priority layout for a robot control computer", "isolcpus vs nohz_full vs cpuset?"는 단골이고, 실무에서는 "센서 루프가 가끔 3 ms 늦어요"를 재현·수정해야 한다
> **Don 기준 난이도**: 고정 우선순위 선점, ISR → 태스크 분리, 링버퍼, 정적 할당은 RTOS·SSD 펌웨어로 이미 안다 / 새로 배울 것은 "범용 OS가 같은 CPU에 몰래 넣는 일(tick, RCU, kworker, IRQ)"을 걷어내는 손잡이들, 그리고 커널 스레드까지 포함한 우선순위 지도
> **선행 노트**: F1(스케줄러 — 클래스·EEVDF·RT throttling·fair_server·DEADLINE 기초), F8(시간·타이머 — hrtimer, ABSTIME vs 상대 sleep, timer slack), F6/F7(IRQ·threaded IRQ), A2(nice·affinity), L8(cgroup v2), N1(Linux는 실시간인가 — PREEMPT_RT). 측정 도구 자체(cyclictest, rtla)는 N3, cpufreq/cpuidle 자체는 N5.

---

## 0. 큰 그림 — "빠르게"가 아니라 "늘 같은 시각에"

bare-metal에서 1 kHz 제어 루프는 쉽다. 타이머 인터럽트 하나, ISR에서 플래그, 메인 루프에서 처리. 그 CPU에는 Don이 넣은 코드 말고는 아무것도 없다. 지터(jitter: 매 주기 시작 시각의 흔들림)는 인터럽트 지연과 캐시 정도가 전부다.

Linux에서는 같은 CPU에 Don이 넣지 않은 것들이 계속 끼어든다: 다른 프로세스, 주기 tick(HZ=1000이면 1 ms마다), RCU callback, kworker, 네트워크·디스크 IRQ, page fault, 깊은 idle에서의 복귀, 주파수 변경. **실시간 튜닝은 이 "남이 넣은 일"을 하나씩 다른 CPU로 내보내거나, 내 루프보다 낮은 우선순위로 내리는 작업**이다. 평균을 빠르게 하는 일이 아니라 **최악값(worst case)과 꼬리(p99.9)를 줄이는 일**이다.

```svg
<svg viewBox="0 0 640 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="140" y="24" text-anchor="middle" font-size="14">지터 원인</text>
  <text x="480" y="24" text-anchor="middle" font-size="14">손잡이 (이 노트의 절)</text>
  <rect x="20" y="40" width="240" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="140" y="63" text-anchor="middle" font-size="12">다른 태스크가 CPU를 먼저 씀</text>
  <rect x="340" y="40" width="280" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="480" y="63" text-anchor="middle" font-size="12">SCHED_FIFO·DEADLINE, 우선순위 (§2–4)</text>
  <rect x="20" y="86" width="240" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="140" y="109" text-anchor="middle" font-size="12">같은 CPU의 IRQ·kthread·tick</text>
  <rect x="340" y="86" width="280" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="480" y="109" text-anchor="middle" font-size="12">CPU 격리·cpuset·IRQ affinity (§6–8)</text>
  <rect x="20" y="132" width="240" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="140" y="155" text-anchor="middle" font-size="12">page fault (첫 접근·malloc)</text>
  <rect x="340" y="132" width="280" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="480" y="155" text-anchor="middle" font-size="12">mlockall·prefault·사전 할당 (§5, §9)</text>
  <rect x="20" y="178" width="240" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="140" y="201" text-anchor="middle" font-size="12">상대 sleep의 누적 드리프트</text>
  <rect x="340" y="178" width="280" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="480" y="201" text-anchor="middle" font-size="12">clock_nanosleep TIMER_ABSTIME (§5)</text>
  <rect x="20" y="224" width="240" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="140" y="247" text-anchor="middle" font-size="12">루프 안 printf·write·lock</text>
  <rect x="340" y="224" width="280" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="480" y="247" text-anchor="middle" font-size="12">SPSC 링 + 로거 스레드 (§5.5)</text>
  <rect x="20" y="270" width="240" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="140" y="293" text-anchor="middle" font-size="12">깊은 idle 복귀·클럭 변화</text>
  <rect x="340" y="270" width="280" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="480" y="293" text-anchor="middle" font-size="12">PM QoS·performance governor (§10)</text>
  <line x1="262" y1="58" x2="336" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="104" x2="336" y2="104" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="150" x2="336" y2="150" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="196" x2="336" y2="196" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="242" x2="336" y2="242" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="288" x2="336" y2="288" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="320" y="328" text-anchor="middle" font-size="12">전제: PREEMPT_RT 커널(N1) — 커널 코드 자체가 선점 가능해야 손잡이가 의미 있다</text>
</svg>
```

그림 1 — 지터 원인과 손잡이. 왼쪽 하나를 고치면 다음 원인이 꼬리에 드러난다. 그래서 "하나씩 켜고 측정" 순서로 진행한다(§11의 결과 표).

Don의 경험과 연결하면: SSD 펌웨어에서 호스트 I/O 지연 꼬리(QoS p99.99)를 줄이려고 GC·wear leveling을 별도 코어나 유휴 구간으로 밀고, 핫패스에서 동적 할당과 로그 출력을 빼고, 인터럽트를 특정 코어에 고정했을 것이다. **이 노트의 손잡이는 그 작업의 Linux 판이다.** 차이는 "다른 일"을 커널이 하고 있어서, 그것을 옮기는 인터페이스(부트 파라미터, `/proc/irq`, cgroup)를 알아야 한다는 것뿐이다.

### 0.1 이 노트의 지도

| 하고 싶은 일 | 손잡이 | 절 |
|---|---|---|
| 내 루프가 다른 일보다 먼저 돌게 | `SCHED_FIFO` 우선순위, `chrt`, `sched_setscheduler` | §2, §4 |
| "주기마다 x ms" 예약 | `SCHED_DEADLINE`, `sched_setattr`, admission control | §3 |
| 전체 스택의 우선순위 순서 | 우선순위 지도: IRQ 스레드 vs 제어 vs 통신 vs 로그 | §4 |
| 정확한 주기 | `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` | §5 |
| page fault·syscall 제거 | `mlockall`, prefault, SPSC 링 | §5, §9 |
| 같은 CPU의 잡음 제거 | `isolcpus=`, `nohz_full=`, `rcu_nocbs=`, `irqaffinity=` | §6 |
| 런타임에 바꿀 수 있는 격리 | cgroup v2 `cpuset.cpus.partition = isolated` | §7 |
| IRQ를 다른 CPU로 | `/proc/irq/N/smp_affinity_list`, irqbalance 끄기 | §8 |
| idle·주파수 잡음 | `/dev/cpu_dma_latency`, performance governor | §10 |

---

## 1. 실습 환경 — 그리고 "에뮬레이터 숫자"를 어떻게 읽나

### 1.1 세 가지 실행 환경

| 환경 | 커널 | 무엇에 썼나 |
|---|---|---|
| QEMU TCG 게스트 `-smp 4` | `6.18.55-rt` (`/var/lab/out/Image-rt`, PREEMPT_RT, NO_HZ_FULL, CPU_ISOLATION, RCU_NOCB_CPU, HZ=1000) | 격리 부트 파라미터, IRQ 스레드 우선순위, cpuset 파티션, DEADLINE, PM QoS — **구조적 증거**(sysfs, `/proc/interrupts`, 에러 코드) |
| QEMU TCG 게스트 `-smp 4` | `6.18.55-trace` (`Image-trace`, PREEMPT_RT 아님 — `CONFIG_PREEMPT`, HZ=250) | 비교용 비RT 커널 |
| Lima VM 호스트 (Apple 가상화, 하드웨어 지원) | `7.0.0-38-generic` (`PREEMPT_DYNAMIC`, lazy) | 1 kHz 지터 숫자 — TCG보다 훨씬 현실적인 타이밍 |

게스트 rootfs는 랩 BusyBox initramfs에 이 노트의 정적 바이너리(`n2_rt`, `n2_bad`, `n2_spsc`, `n2_dl`, `n2_qos`)와 스크립트를 넣어 다시 묶은 것이다. 부팅 명령의 뼈대는 다음과 같다(디스크와 NIC은 IRQ 부하를 만들려고 붙였다).

```sh
qemu-system-aarch64 -machine virt -cpu cortex-a57,pmu=on -smp 4 -m 1G -nographic \
  -kernel /var/lab/out/Image-rt -initrd n2_i.gz \
  -drive file=n2_disk.img,if=virtio,format=raw \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -append "console=ttyAMA0 quiet <격리 파라미터> LAB_CMD=/n2_run.sh~jitter~500"
```

### 1.2 TCG 숫자의 함정 — 먼저 읽고 넘어간다

QEMU TCG는 ARM 명령을 호스트에서 번역해 실행한다. 게스트의 "시간"(arch timer)은 호스트 실시간을 따라가지만, 게스트 CPU가 그 시간 동안 실행하는 명령 수는 호스트 부하에 따라 들쭉날쭉하다. 이 노트를 쓰는 동안 Lima VM(6 vCPU)에는 17명의 작성자가 QEMU와 커널 빌드를 돌려 load average가 9~25였다. 그래서:

- **1 kHz 루프를 TCG 게스트에서 부하와 함께 돌리면 루프가 아예 따라오지 못했다.** 실제로 `Image-trace`에서 CPU hog 2개만 켜도 2000주기(이론 2 s) 루프가 4~7 s 늦게 끝났다(`drift=4168150 us` 같은 값). 이건 Linux의 문제가 아니라 "게스트가 1 ms 안에 타이머 인터럽트 + 깨우기 + 문맥 전환을 끝낼 만큼 명령을 실행하지 못한 것"이다.
- 그래서 TCG 게스트의 지터 실험은 **100 Hz(10 ms 주기)** 로 낮춰서 했고, 숫자 자체보다 **설정 간 상대 비교와 구조적 증거**(격리 sysfs 값, CPU별 인터럽트 카운트, tick 수)를 본다.
- 1 kHz 숫자는 하드웨어 가상화로 도는 **Lima VM 호스트**에서 쟀다. 여기도 Mac 호스트가 vCPU를 가끔 수 ms씩 재우기 때문에(다른 작성자의 부하) max는 크다. 그래도 p50·p99는 실제 보드와 비슷한 감각을 준다.
- **실제 보드의 숫자는 이 노트의 숫자가 아니다.** i.MX8M이나 AM62 같은 보드에서 PREEMPT_RT + 격리면 `cyclictest` max 수십 µs가 흔한 목표치다(N3에서 측정법).

---

## 2. 스케줄링 클래스와 정책 — 어느 줄에 설 것인가

F1에서 run queue와 클래스를 자세히 봤다. 여기서는 **튜닝하는 사람 관점에서** 다시 정리한다. 커널은 CPU마다 클래스 순서대로 "실행할 태스크가 있나?"를 묻는다. 위 클래스에 실행 가능한 태스크가 하나라도 있으면 아래 클래스는 돌지 못한다.

```svg
<svg viewBox="0 0 650 305" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="4" refY="7" orient="auto"><path d="M0,0 L8,0 L4,8 z" fill="currentColor"/></marker></defs>
  <text x="20" y="14" text-anchor="middle" font-size="12">높음</text>
  <line x1="20" y1="22" x2="20" y2="280" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="20" y="298" text-anchor="middle" font-size="12">낮음</text>
  <rect x="40" y="20" width="160" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="47" text-anchor="middle" font-size="13">stop</text>
  <rect x="210" y="20" width="420" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="220" y="39" font-size="12">migration/N, stop_machine — 무엇이든 선점</text>
  <text x="220" y="57" font-size="12">유저가 고를 수 없는 클래스</text>
  <rect x="40" y="74" width="160" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="101" text-anchor="middle" font-size="13">deadline</text>
  <rect x="210" y="74" width="420" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="220" y="93" font-size="12">SCHED_DEADLINE: runtime / deadline / period, EDF + CBS</text>
  <text x="220" y="111" font-size="12">+ fair_server (6.12+): RT가 fair를 완전히 굶기지 않게</text>
  <rect x="40" y="128" width="160" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="120" y="155" text-anchor="middle" font-size="13">rt</text>
  <rect x="210" y="128" width="420" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="220" y="147" font-size="12">SCHED_FIFO / SCHED_RR, rtprio 1–99 (99가 최고)</text>
  <text x="220" y="165" font-size="12">IRQ 스레드(기본 50), 제어 루프, 통신 스레드</text>
  <rect x="40" y="182" width="160" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="209" text-anchor="middle" font-size="13">fair</text>
  <rect x="210" y="182" width="420" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="220" y="201" font-size="12">SCHED_OTHER (nice −20..19), SCHED_BATCH, SCHED_IDLE</text>
  <text x="220" y="219" font-size="12">EEVDF — 비율로 나눈다. 로거·ROS 노드·셸 대부분</text>
  <rect x="40" y="236" width="160" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="263" text-anchor="middle" font-size="13">idle</text>
  <rect x="210" y="236" width="420" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="220" y="255" font-size="12">swapper/N (idle 태스크) — 할 일이 없을 때만</text>
  <text x="220" y="273" font-size="12">SCHED_IDLE "정책"과는 다른 것 (그건 fair 안의 최저 weight)</text>
</svg>
```

그림 2 — 스케줄링 클래스 계층. 튜닝에서 고르는 것은 deadline·rt·fair 세 줄이다. stop과 idle은 커널 전용이다.

### 2.1 정책 한눈에

| 정책 | 클래스 | 파라미터 | 언제 쓰나 (로봇 예) |
|---|---|---|---|
| `SCHED_OTHER` (=`SCHED_NORMAL`) | fair | nice −20..19 (weight 비율) | 대부분: ROS 노드, 상태 머신, 웹 UI |
| `SCHED_BATCH` | fair | nice | CPU를 오래 쓰는 일괄 작업. wakeup 선점에서 불리하게 취급 → 대화형 작업을 덜 방해 |
| `SCHED_IDLE` | fair | 없음 (weight 3, nice 19의 15보다 작다) | 로그 압축·업로드, 맵 후처리 — "남는 CPU만" |
| `SCHED_FIFO` | rt | 1..99 | 제어 루프, 센서 IRQ 스레드, 모터 명령 |
| `SCHED_RR` | rt | 1..99 + 같은 우선순위끼리 time slice(기본 100 ms) | 같은 우선순위 RT 작업이 여럿이고 공평해야 할 때(드묾) |
| `SCHED_DEADLINE` | deadline | runtime ≤ deadline ≤ period | 주기·예산이 명확한 작업, 예산 초과 시 강제로 멈춰야 할 때 |

`chrt -m`이 정책별 범위를 보여 준다(Lima VM, `7.0.0-38-generic`):

```text
SCHED_OTHER min/max priority	: 0/0
SCHED_FIFO min/max priority	: 1/99
SCHED_RR min/max priority	: 1/99
SCHED_BATCH min/max priority	: 0/0
SCHED_IDLE min/max priority	: 0/0
SCHED_DEADLINE min/max priority	: 0/0
```

### 2.2 세 가지 도구 — chrt, taskset, sched_setattr

```sh
chrt -f 80 ./ctrl              # SCHED_FIFO 80으로 실행
chrt -f -p 85 <pid>            # 이미 도는 태스크(예: IRQ 스레드)의 정책 변경
chrt -p <pid>                  # 현재 정책/우선순위 보기
chrt -d -T 2000000 -D 10000000 -P 10000000 0 ./job   # DEADLINE: runtime/deadline/period (ns)
chrt -i 0 ./compress           # SCHED_IDLE
taskset -c 3 ./ctrl            # CPU3에서만 실행 (affinity)
taskset -pc 0-2 <pid>          # 실행 중 태스크의 affinity 변경
```

C에서는 `sched_setscheduler()`(FIFO/RR/OTHER/BATCH/IDLE), `pthread_setschedparam()`(스레드 단위), `sched_setattr()`(DEADLINE과 모든 정책 — glibc 2.41부터 래퍼가 있지만 오래된 glibc를 생각하면 `syscall(SYS_sched_setattr, ...)`이 안전하다), `sched_setaffinity()`/`pthread_setaffinity_np()`를 쓴다. 권한: RT·DEADLINE 정책은 `CAP_SYS_NICE`가 필요하다(또는 `RLIMIT_RTPRIO` > 0). systemd 서비스라면 `CPUSchedulingPolicy=fifo`, `CPUSchedulingPriority=80`, `CPUAffinity=3`, `LimitMEMLOCK=infinity`로 같은 일을 선언적으로 한다(L1).

### 2.3 함정 — pthread의 "inherit-sched"

`pthread_attr_setschedpolicy()`로 새 스레드의 정책을 정해도, attr의 기본값이 `PTHREAD_INHERIT_SCHED`이면 **무시되고 부모 정책을 물려받는다.** `pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)`를 같이 해야 한다. 이 노트의 `n2_spsc`처럼 스레드를 만든 뒤 그 스레드 안에서 `pthread_setschedparam(pthread_self(), ...)`를 부르는 방법이 실수가 적다.

---

## 3. SCHED_DEADLINE — "10 ms마다 2 ms" 예약과 그 규칙들

### 3.1 직관

SCHED_FIFO는 "누가 더 중요한가"만 말한다. 중요한 태스크가 폭주하면 아래는 전부 굶는다. SCHED_DEADLINE은 **"이 태스크는 period마다 runtime만큼 CPU가 필요하고, 그 일은 deadline 안에 끝나야 한다"** 를 커널에 계약으로 건다. 커널은 두 가지를 해 준다.

- **EDF(Earliest Deadline First)**: deadline이 가장 가까운 태스크부터 돌린다. 단일 CPU에서 이용률 합 ≤ 100%면 모든 deadline을 지키는 것이 이론적으로 보장되는 스케줄링이다.
- **CBS(Constant Bandwidth Server)**: runtime을 다 쓰면 다음 period까지 **강제로 멈춘다(throttle)**. 폭주해도 남의 몫을 못 먹는다. RTOS에서 "태스크별 실행 시간 예산 + 예산 초과 감시"를 직접 짜던 것을 커널이 해 준다.

정의(F1 §6): `sched_runtime ≤ sched_deadline ≤ sched_period`, 단위 ns. 우선순위 숫자는 없다. 모든 RT 태스크보다 위다.

### 3.2 admission control — 넘치면 아예 거절한다

커널은 새 예약을 받을 때 **root domain**(load balancing을 함께 하는 CPU 집합, 기본은 전체 CPU) 안의 예약 합을 검사한다.

```text
sum(runtime_i / period_i)  <=  (sched_rt_runtime_us / sched_rt_period_us) x (root domain의 CPU 수)
```

기본값 950000/1000000 = 0.95이므로 4 CPU면 3.8. F1 §6.3은 2 CPU에서 90% 예약 3개로 이걸 봤다. 여기서는 4 CPU에 0.5짜리 예약 8개를 차례로 요청한다. 예약을 잡고 3초 잠드는 버전이라 셸이 굶지 않는다.

### 3.2.1 예제 1 — admission control과 DEADLINE의 제약 `n2_dl`

무엇을 확인하나: `n2_dl` — DEADLINE을 거는 세 가지 시나리오(hold: 예약 후 sleep, pin: DEADLINE 후 affinity·fork, pin2: affinity 후 DEADLINE).

```c
/* n2_dl: SCHED_DEADLINE helper.
 *   n2_dl hold <runtime_us> <period_us> <hold_s>    reserve, sleep (admission test)
 *   n2_dl pin|pin2 <runtime_us> <period_us> <cpu>   DL then affinity (+fork) / affinity then DL */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/sched/types.h>               /* struct sched_attr */
static int set_dl(long rt_us, long per_us) {
	struct sched_attr a = { .size = sizeof a, .sched_policy = SCHED_DEADLINE,
		.sched_runtime = rt_us * 1000, .sched_deadline = per_us * 1000, .sched_period = per_us * 1000 };
	return syscall(SYS_sched_setattr, 0, &a, 0) ? -errno : 0;
}
static int pin(int cpu) { cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s); return sched_setaffinity(0, sizeof s, &s) ? -errno : 0; }
int main(int argc, char **argv) {
	if (argc < 5) { fprintf(stderr, "usage: see source\n"); return 1; }
	long rt = atol(argv[2]), per = atol(argv[3]), x = atol(argv[4]); int r;
	if (!strcmp(argv[1], "hold")) {
		r = set_dl(rt, per);
		printf("pid %d DL %ld/%ld us: %s\n", getpid(), rt, per, r ? strerror(-r) : "admitted");
		fflush(stdout); if (!r) sleep((unsigned)x); return r ? 1 : 0;
	}
	if (!strcmp(argv[1], "pin")) {
		r = set_dl(rt, per); printf("1) set DL (all CPUs allowed): %s\n", r ? strerror(-r) : "ok");
		r = pin((int)x);    printf("2) then sched_setaffinity(cpu %ld): %s\n", x, r ? strerror(-r) : "ok");
		pid_t c = fork();                     /* a DL task may not fork (unless RESET_ON_FORK) */
		printf("3) fork() from the DL task: %s\n", c < 0 ? strerror(errno) : "ok");
		return 0;
	}
	if (!strcmp(argv[1], "pin2")) {
		r = pin((int)x);    printf("1) sched_setaffinity(cpu %ld) first: %s\n", x, r ? strerror(-r) : "ok");
		r = set_dl(rt, per); printf("2) then set DL: %s\n", r ? strerror(-r) : "ok");
		return 0;
	}
	return 1;
}
```

빌드: `gcc -static -std=gnu11 -Wall -Wextra -O2 n2_dl.c -o n2_dl` (경고 0개).

게스트 스크립트(`n2_dl.sh`):

```sh
echo "-- sched_rt_runtime_us=$(cat /proc/sys/kernel/sched_rt_runtime_us) period=$(cat /proc/sys/kernel/sched_rt_period_us), CPUs=$(nproc)"
echo "-- admission: eight 500 ms / 1000 ms reservations (0.5 CPU each) on 4 CPUs"
for i in 1 2 3 4 5 6 7 8; do /n2_dl hold 500000 1000000 3 & sleep 0.3; done; wait
echo "-- DL + affinity (root domain = all 4 CPUs)"; /n2_dl pin 1000 10000 3; /n2_dl pin2 1000 10000 3
```

```text
lab initramfs: 6.18.55-rt up 7.82s
== kernel 6.18.55-rt  cmdline: console=ttyAMA0 quiet  
   /sys/devices/system/cpu/isolated = ''
   /sys/devices/system/cpu/nohz_full = '(null)'
-- sched_rt_runtime_us=950000 period=1000000, CPUs=4
-- admission: eight 500 ms / 1000 ms reservations (0.5 CPU each) on 4 CPUs
pid 127 DL 500000/1000000 us: admitted
pid 129 DL 500000/1000000 us: admitted
pid 131 DL 500000/1000000 us: admitted
pid 133 DL 500000/1000000 us: admitted
pid 135 DL 500000/1000000 us: admitted
pid 137 DL 500000/1000000 us: admitted
pid 139 DL 500000/1000000 us: admitted
pid 141 DL 500000/1000000 us: Device or resource busy
-- DL + affinity (root domain = all 4 CPUs)
1) set DL (all CPUs allowed): ok
2) then sched_setaffinity(cpu 3): Device or resource busy
3) fork() from the DL task: Resource temporarily unavailable
1) sched_setaffinity(cpu 3) first: ok
2) then set DL: Operation not permitted
[   15.266701][  T145] reboot: Power down
```

출력에서 볼 것:

- **7개까지 승인, 8번째는 `EBUSY`.** 7 × 0.5 = 3.5 ≤ 3.8인데 8 × 0.5 = 4.0 > 3.8. 여기에 6.12+의 fair_server가 CPU마다 0.05씩 이미 예약해 두었다(F1 §5.2) — 3.5 + 0.2 = 3.7 ≤ 3.8이라 7번째가 들어갔다.
- **DEADLINE 태스크는 root domain 전체를 affinity로 가져야 한다.** DEADLINE을 건 뒤 CPU3 하나로 좁히려 하면 `sched_setaffinity`가 `EBUSY`, 먼저 CPU3에 묶고 DEADLINE을 걸면 `sched_setattr`가 `EPERM`. 이유: admission control이 "root domain 전체 대역폭"으로 계산하므로, 한 CPU에 묶인 태스크를 허용하면 계산이 틀어진다(global EDF 가정이 깨진다).
- **DEADLINE 태스크는 fork할 수 없다** (`EAGAIN`). 자식이 같은 예약을 물려받으면 대역폭이 두 배가 되기 때문이다. `sched_flags`에 `SCHED_FLAG_RESET_ON_FORK`를 주면 자식은 SCHED_OTHER로 시작한다.

### 3.3 "DEADLINE을 한 CPU에만" — cpuset 파티션으로 root domain을 쪼갠다

그럼 격리된 CPU3에서만 DEADLINE을 돌리고 싶으면? **cpuset 파티션으로 CPU3을 별도 root domain으로 만든다**(§7). 그 안에서는 root domain = {CPU3}이므로 affinity 규칙이 자동으로 만족되고, admission 한계도 1 × 0.95가 된다. §7에서 실제로 0.9는 승인, 0.96은 거절되는 것을 본다.

### 3.4 FIFO냐 DEADLINE이냐 — 실무 판단

| 질문 | SCHED_FIFO | SCHED_DEADLINE |
|---|---|---|
| 설정 | 숫자 하나 | runtime/deadline/period 세 개, WCET를 알아야 함 |
| 폭주 시 | 아래 우선순위 전부 굶음 (fair_server 50 ms/1 s만 예외) | 자기 runtime에서 강제 정지 — 격리가 보장됨 |
| 다른 RT와의 관계 | 숫자로 명시적 순서 | 모든 FIFO보다 위 — IRQ 스레드(FIFO 50)보다도 위라는 점 주의 |
| 생태계 | ROS 2 executor, 대부분 RT 앱, cyclictest | 일부 산업용·연구용, `chrt -d` |
| 함정 | 우선순위 역전 (PI mutex로 해결) | fork 금지, affinity 제약, 측정한 WCET가 틀리면 throttle로 deadline miss |

실무에서 대부분의 로봇 스택은 **FIFO + 격리**로 간다. DEADLINE은 "이 작업이 폭주해도 시스템이 살아야 한다"(예: 서드파티 인식 모듈에 CPU 예산을 씌울 때)에 의미가 크다. 단, **DEADLINE 태스크는 IRQ 스레드보다 위에서 돈다** — 그 태스크가 데이터를 기다리는 IRQ 스레드를 runtime 동안 막을 수 있다.

---

## 4. 우선순위 지도 — 로봇 컴퓨터의 스레드를 어떻게 줄 세우나

### 4.1 원칙 — "데이터 흐름 순서 = 우선순위 순서", 그리고 빈자리

RTOS에서 태스크 우선순위를 정할 때처럼 Rate Monotonic(주기가 짧을수록 높게)이 출발점이다. Linux에서는 여기에 두 가지가 더해진다.

1. **IRQ 스레드도 태스크다.** PREEMPT_RT에서는 거의 모든 디바이스 IRQ 핸들러가 `irq/<번호>-<이름>` 커널 스레드(FIFO 50)로 돈다. 제어 루프(FIFO 80)가 SPI IMU 데이터를 기다린다면, 그 데이터를 가져오는 `irq/NN-spi...` 스레드가 50이면 80보다 낮다 — 다른 FIFO 51~79 작업이 데이터 경로를 막을 수 있다. **생산자는 소비자보다 위**에 둔다.
2. **커널 스레드에도 RT가 있다.** `ktimers/N`, `rcuc/N` 같은 스레드는 FIFO 1이다. 내 RT 루프가 어떤 CPU를 100% 쓰면 그 CPU의 timer softirq와 RCU callback이 굶는다. 이게 "RT 루프는 반드시 잠들어야 한다", "RT CPU에는 `rcu_nocbs`"의 이유다.

그리고 **99는 비워 둔다.** 문제가 생겼을 때 `chrt -f 99`로 디버그 셸을 띄울 자리, 그리고 나중에 더 급한 무언가가 생길 자리다. 반대로 "중요하니까 99"로 시작하면 우선순위 공간이 계획 없이 위로 몰린다.

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="20" font-size="13">로봇 컴퓨터 우선순위 지도 (PREEMPT_RT) — 회색 = 커널 기본, 색 = 우리가 정함</text>
  <rect x="20" y="34" width="100" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="70" y="52" text-anchor="middle" font-size="12">FIFO 99</text>
  <rect x="130" y="34" width="510" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="140" y="52" font-size="12">migration/N — stop 클래스 (stat에는 FIFO 99로 보임)</text>
  <rect x="20" y="65" width="100" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="70" y="83" text-anchor="middle" font-size="12">FIFO 90</text>
  <rect x="130" y="65" width="510" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="140" y="83" font-size="12">IMU SPI IRQ 스레드 — 제어 루프가 기다리는 데이터 (데모: 85)</text>
  <rect x="20" y="96" width="100" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="70" y="114" text-anchor="middle" font-size="12">FIFO 80</text>
  <rect x="130" y="96" width="510" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="140" y="114" font-size="12">1 kHz 제어 루프 (CPU3 격리, mlockall)</text>
  <rect x="20" y="127" width="100" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="70" y="145" text-anchor="middle" font-size="12">FIFO 70</text>
  <rect x="130" y="127" width="510" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="140" y="145" font-size="12">CAN 모터 IRQ 스레드 · 모터 명령 TX</text>
  <rect x="20" y="158" width="100" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="70" y="176" text-anchor="middle" font-size="12">FIFO 60</text>
  <rect x="130" y="158" width="510" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="140" y="176" font-size="12">통신: DDS 수신·안전 heartbeat (데모: sleep)</text>
  <rect x="20" y="189" width="100" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="70" y="207" text-anchor="middle" font-size="12">FIFO 50</text>
  <rect x="130" y="189" width="510" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="140" y="207" font-size="12">나머지 irq/N-* 스레드 · watchdogd (커널 기본값)</text>
  <rect x="20" y="220" width="100" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="70" y="238" text-anchor="middle" font-size="12">FIFO 1</text>
  <rect x="130" y="220" width="510" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="140" y="238" font-size="12">ktimers/N · rcuc/N · rcu_preempt · rcub · irq_work/N</text>
  <rect x="20" y="251" width="100" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="70" y="269" text-anchor="middle" font-size="12">nice 0</text>
  <rect x="130" y="251" width="510" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="140" y="269" font-size="12">ROS 노드 · systemd · 셸 (SCHED_OTHER)</text>
  <rect x="20" y="282" width="100" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="70" y="300" text-anchor="middle" font-size="12">nice 10</text>
  <rect x="130" y="282" width="510" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="140" y="300" font-size="12">로거 스레드 (SPSC 링 비우기)</text>
  <rect x="20" y="313" width="100" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="70" y="331" text-anchor="middle" font-size="12">IDLE</text>
  <rect x="130" y="313" width="510" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="140" y="331" font-size="12">로그 압축·업로드 (SCHED_IDLE, 남는 CPU만)</text>
</svg>
```

그림 3 — 로봇 컴퓨터 우선순위 지도. 회색 줄의 숫자는 아래 예제 2에서 Image-rt가 실제로 보여 준 커널 기본값이다. 색 줄은 설계 권장값이고, 데모에서는 virtio-net RX IRQ 스레드를 "센서 IRQ" 대역(85)으로, `chrt -f 60 sleep`을 통신 스레드 대역으로 썼다.

### 4.2 예제 2 — PREEMPT_RT의 실제 우선순위 목록과 로봇 스택 적용

무엇을 확인하나: Image-rt 게스트에서 SCHED_OTHER가 아닌 모든 스레드를 정책·rtprio·nice와 함께 나열하고(proc(5) 기준 `/proc/<pid>/task/<tid>/stat`의 41번째 필드 = policy, 40번째 = rt_priority, 19번째 = nice. comm과 그 앞을 `sub()`로 떼어 내면 2씩 당겨져 awk에서는 `$39`, `$38`, `$17`이 된다), 로봇 스택을 띄운 뒤 센서 IRQ 스레드를 `chrt -f -p 85`로 올린다. BusyBox `ps`는 rtprio 열이 없어서 awk로 직접 읽는다(procps가 있으면 `ps -eLo pid,cls,rtprio,ni,psr,comm`).

```sh
# priority map: every task with policy != OTHER, plus our robot-stack tasks
plist() { for s in /proc/[0-9]*/task/[0-9]*/stat; do
	awk '{ sub(/^[^(]*\(/, ""); c = $0; sub(/\).*/, "", c); sub(/^.*\) /, "");
	       pol = $39; rp = $38; n = $17; if (pol != 0 || c ~ /^n2_|^sleep/) printf "%-6s rtprio=%-3s nice=%-3s %s\n",
	       (pol==1?"FIFO":pol==2?"RR":pol==3?"BATCH":pol==5?"IDLE":pol==6?"DEADL":"OTHER"), rp, n, c }' $s 2>/dev/null
	done | sort | uniq -c | sort -k3,3nr -k2; }
echo "-- before: non-OTHER tasks (count policy rtprio nice comm)"; plist
net; ping -A -q 10.0.2.2 > /dev/null 2>&1 &
/n2_rt 80 3 1 20000 > /dev/null &                         # control loop
chrt -f 60 sleep 30 &                                     # comms (stand-in)
nice -n 10 sleep 30 &                                     # logger (stand-in)
chrt -i 0 sleep 30 &                                      # housekeeping / SCHED_IDLE
IRQT=$(grep -l 'virtio0-input' /proc/[0-9]*/stat | head -1 | cut -d/ -f3)
echo "-- sensor RX irq thread pid $IRQT: $(chrt -p $IRQT | tr '\n' ' ')"
chrt -f -p 85 $IRQT; echo "   after chrt -f -p 85: $(chrt -p $IRQT | tail -1)"
sleep 1; echo "-- after: robot stack"; plist
```

출력(“after” 부분, 앞의 “before” 목록은 우리 태스크 4줄이 없는 것 말고 같다):

```text
-- sensor RX irq thread pid 98: pid 98's current scheduling policy: SCHED_FIFO pid 98's current scheduling priority: 50 
pid 98's current scheduling policy: SCHED_FIFO
pid 98's current scheduling priority: 50
pid 98's new scheduling policy: SCHED_FIFO
pid 98's new scheduling priority: 85
   after chrt -f -p 85: pid 98's current scheduling priority: 85
-- after: robot stack
      1 FIFO   rtprio=1   nice=0   irq_work/0
      ...      (irq_work/1..3 같음)
      1 FIFO   rtprio=1   nice=0   ktimers/0
      ...      (ktimers/1..3 같음)
      1 FIFO   rtprio=1   nice=0   rcu_preempt
      1 FIFO   rtprio=1   nice=0   rcub/0
      1 FIFO   rtprio=1   nice=0   rcuc/0
      ...      (rcuc/1..3 같음)
      1 FIFO   rtprio=50  nice=0   irq/13-uart-pl011
      1 FIFO   rtprio=50  nice=0   irq/17-virtio1-config
      1 FIFO   rtprio=50  nice=0   irq/18-virtio1-req.0
      1 FIFO   rtprio=50  nice=0   irq/19-virtio1-req.1
      1 FIFO   rtprio=50  nice=0   irq/20-virtio1-req.2
      1 FIFO   rtprio=50  nice=0   irq/21-virtio1-req.3
      1 FIFO   rtprio=50  nice=0   irq/22-virtio0-config
      1 FIFO   rtprio=50  nice=0   irq/24-virtio0-output.0
      1 FIFO   rtprio=50  nice=0   irq/25-rtc-pl031
      1 FIFO   rtprio=50  nice=0   irq/27-GPIO Key Poweroff
      1 FIFO   rtprio=50  nice=0   watchdogd
      1 FIFO   rtprio=60  nice=0   sleep
      1 FIFO   rtprio=80  nice=0   n2_rt
      1 FIFO   rtprio=85  nice=0   irq/23-virtio0-input.0
      1 FIFO   rtprio=99  nice=0   migration/0
      ...      (migration/1..3 같음)
      1 IDLE   rtprio=0   nice=0   sleep
      2 OTHER  rtprio=0   nice=0   n2_run.sh
      1 OTHER  rtprio=0   nice=10  sleep
```

(같은 모양의 CPU별 줄 3개씩을 `...`로 줄였다.)

출력에서 볼 것:

- **모든 디바이스 IRQ가 FIFO 50 스레드다** — uart, virtio-blk 큐 4개, virtio-net, rtc, GPIO 키까지. PREEMPT_RT가 강제 스레드화한 결과이고, 우선순위 50은 `sched_set_fifo()`(`MAX_RT_PRIO / 2`)가 정한 값이다. 비RT 커널에서도 `threadirqs` 부트 파라미터를 주면 같은 모양이 된다.
- **FIFO 1 무리**: `ktimers/N`(PREEMPT_RT에서 timer·hrtimer softirq를 처리), `rcuc/N`(CPU별 RCU callback), `rcu_preempt`(grace period 커널 스레드), `rcub/0`(RCU priority boost), `irq_work/N`. 가장 낮은 RT라 일반 태스크보다는 위, 내 RT 태스크보다는 아래다.
- **`migration/N`은 FIFO 99로 표시되지만 실제로는 stop 클래스**에서 돈다(`sched_set_stop_task()`가 표시용으로 FIFO 99를 넣는다). 어떤 RT 태스크도 이걸 막지 못한다.
- `watchdogd`(FIFO 50)는 `/dev/watchdog` 하드웨어 watchdog 커널 피더다. 제어 루프가 이보다 높으면, 제어 루프가 폭주할 때 watchdog이 리셋해 준다 — 로봇에서는 의도한 동작일 수 있다(N4·L7과 연결).
- `chrt -f -p 85`로 센서 RX IRQ 스레드(`irq/23-virtio0-input.0`)가 제어 루프(80) 위로 올라갔다. **IRQ 번호는 부팅마다 바뀔 수 있으므로** 실제 제품에서는 이름으로 PID를 찾는 스크립트를 systemd oneshot이나 udev 규칙으로 돌린다. 장치 IRQ 스레드 PID는 `/proc/irq/N/`에는 없고 `pgrep -f "irq/.*spi"`나 위처럼 `/proc/*/stat` 이름으로 찾는다.

### 4.3 RT throttling 다시 보기 — 950 ms/1 s와 fair_server

`/proc/sys/kernel/sched_rt_runtime_us`(950000) / `sched_rt_period_us`(1000000)는 고전적으로 "RT는 1초 중 0.95초까지"였다. F1 §5.2에서 본 것처럼 6.12+에서 `RT_GROUP_SCHED`가 없는 커널은 대신 **fair_server**(CPU마다 50 ms/1 s의 deadline 서버)가 굶는 fair 태스크를 구한다. 이 랩의 Image-rt는 `.config`를 보면 `CONFIG_RT_GROUP_SCHED`가 켜져 있지 않다 — 그래서 fair_server만 일한다. 튜닝 관점의 결론은 같다:

- 정상적인 RT 루프는 **매 주기 잠들어서** throttling에 걸릴 일이 없어야 한다. 걸린다면 루프가 폴링 중이거나 폭주한 것이다.
- `sched_rt_runtime_us = -1`로 끄는 것은 "RT 폭주 = 시스템 정지"를 받아들이는 선택이다. 격리된 CPU에서 폴링 루프를 의도적으로 돌릴 때만(예: DPDK식 busy-poll) 고려하고, 그때도 fair_server가 같은 CPU의 kworker를 위해 50 ms를 빼앗을 수 있다는 점(F1 §13.2)을 기억한다.


---

## 5. 정석 주기 루프 — clock_nanosleep ABSTIME + mlockall + prefault + FIFO + 고정

### 5.1 직관 — 상대 sleep은 오차가 쌓이고, 절대 시각은 안 쌓인다

F8 §9.4에서 상대 `nanosleep` 1 kHz 루프가 1초에 수십 ms씩 밀리는 것을 봤다. 이유는 간단하다. "지금부터 1 ms 뒤"를 매번 요청하면 (루프 본문 시간 + 깨어남 지연)이 매 주기 더해진다. 절대 시각(`TIMER_ABSTIME`)으로 "t0 + k × 1 ms에 깨워 달라"고 하면 한 번 늦어도 다음 목표는 그대로다. bare-metal의 "타이머 compare 레지스터에 다음 값 = 이전 값 + period"와 같은 원리다(현재 카운터 + period로 쓰면 드리프트가 생긴다는 것, Don은 이미 안다).

```svg
<svg viewBox="0 0 640 250" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="30" font-size="13">상대 sleep: usleep(1000) — 매 주기 (처리 시간 + 깨어남 지연)이 더해진다</text>
  <line x1="50" y1="80" x2="625" y2="80" stroke="currentColor"/>
  <text x="20" y="150" font-size="13">절대 시각: clock_nanosleep(TIMER_ABSTIME) — 목표 시각은 k × T로 고정</text>
  <line x1="50" y1="200" x2="625" y2="200" stroke="currentColor"/>
  <line x1="60" y1="70" x2="60" y2="90" stroke="#888"/>
  <line x1="60" y1="190" x2="60" y2="210" stroke="#888"/>
  <text x="60" y="106" text-anchor="middle" font-size="12">0T</text>
  <text x="60" y="226" text-anchor="middle" font-size="12">0T</text>
  <circle cx="60" cy="62" r="4" fill="#d0564a"/>
  <circle cx="60" cy="182" r="4" fill="#3f9a6b"/>
  <line x1="110" y1="70" x2="110" y2="90" stroke="#888"/>
  <line x1="110" y1="190" x2="110" y2="210" stroke="#888"/>
  <circle cx="115" cy="62" r="4" fill="#d0564a"/>
  <circle cx="114" cy="182" r="4" fill="#3f9a6b"/>
  <line x1="160" y1="70" x2="160" y2="90" stroke="#888"/>
  <line x1="160" y1="190" x2="160" y2="210" stroke="#888"/>
  <text x="160" y="106" text-anchor="middle" font-size="12">2T</text>
  <text x="160" y="226" text-anchor="middle" font-size="12">2T</text>
  <circle cx="170" cy="62" r="4" fill="#d0564a"/>
  <circle cx="162" cy="182" r="4" fill="#3f9a6b"/>
  <line x1="210" y1="70" x2="210" y2="90" stroke="#888"/>
  <line x1="210" y1="190" x2="210" y2="210" stroke="#888"/>
  <circle cx="225" cy="62" r="4" fill="#d0564a"/>
  <circle cx="217" cy="182" r="4" fill="#3f9a6b"/>
  <line x1="260" y1="70" x2="260" y2="90" stroke="#888"/>
  <line x1="260" y1="190" x2="260" y2="210" stroke="#888"/>
  <text x="260" y="106" text-anchor="middle" font-size="12">4T</text>
  <text x="260" y="226" text-anchor="middle" font-size="12">4T</text>
  <circle cx="280" cy="62" r="4" fill="#d0564a"/>
  <circle cx="263" cy="182" r="4" fill="#3f9a6b"/>
  <line x1="310" y1="70" x2="310" y2="90" stroke="#888"/>
  <line x1="310" y1="190" x2="310" y2="210" stroke="#888"/>
  <circle cx="335" cy="62" r="4" fill="#d0564a"/>
  <circle cx="315" cy="182" r="4" fill="#3f9a6b"/>
  <line x1="360" y1="70" x2="360" y2="90" stroke="#888"/>
  <line x1="360" y1="190" x2="360" y2="210" stroke="#888"/>
  <text x="360" y="106" text-anchor="middle" font-size="12">6T</text>
  <text x="360" y="226" text-anchor="middle" font-size="12">6T</text>
  <circle cx="390" cy="62" r="4" fill="#d0564a"/>
  <circle cx="362" cy="182" r="4" fill="#3f9a6b"/>
  <line x1="410" y1="70" x2="410" y2="90" stroke="#888"/>
  <line x1="410" y1="190" x2="410" y2="210" stroke="#888"/>
  <circle cx="445" cy="62" r="4" fill="#d0564a"/>
  <circle cx="416" cy="182" r="4" fill="#3f9a6b"/>
  <line x1="460" y1="70" x2="460" y2="90" stroke="#888"/>
  <line x1="460" y1="190" x2="460" y2="210" stroke="#888"/>
  <text x="460" y="106" text-anchor="middle" font-size="12">8T</text>
  <text x="460" y="226" text-anchor="middle" font-size="12">8T</text>
  <circle cx="500" cy="62" r="4" fill="#d0564a"/>
  <circle cx="463" cy="182" r="4" fill="#3f9a6b"/>
  <line x1="510" y1="70" x2="510" y2="90" stroke="#888"/>
  <line x1="510" y1="190" x2="510" y2="210" stroke="#888"/>
  <circle cx="555" cy="62" r="4" fill="#d0564a"/>
  <circle cx="514" cy="182" r="4" fill="#3f9a6b"/>
  <line x1="560" y1="70" x2="560" y2="90" stroke="#888"/>
  <line x1="560" y1="190" x2="560" y2="210" stroke="#888"/>
  <text x="560" y="106" text-anchor="middle" font-size="12">10T</text>
  <text x="560" y="226" text-anchor="middle" font-size="12">10T</text>
  <circle cx="610" cy="62" r="4" fill="#d0564a"/>
  <circle cx="562" cy="182" r="4" fill="#3f9a6b"/>
  <line x1="560" y1="50" x2="610" y2="50" stroke="#d0564a"/>
  <text x="585" y="44" text-anchor="middle" font-size="12">누적 10ε</text>
  <text x="20" y="128" font-size="12">회색 = 이상적인 시각, 빨강 = 실제로 깨어난 시각 (오차가 쌓여 오른쪽으로 밀린다)</text>
  <text x="20" y="244" font-size="12">초록 = 실제로 깨어난 시각 (늦어도 다음 목표는 그대로 → 드리프트 0, 지터만 남는다)</text>
</svg>
```

그림 4 — 상대 sleep(위)과 절대 시각 sleep(아래). 위는 오차 ε가 주기마다 쌓여 드리프트가 되고, 아래는 각 주기의 지터만 남는다.

### 5.2 정석 루프의 다섯 요소

| 요소 | 코드 | 없으면 생기는 일 |
|---|---|---|
| 절대 시각 sleep | `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL)` | 드리프트 (§5.4에서 0.56 s / 5 s) |
| `CLOCK_MONOTONIC` | `CLOCK_REALTIME` 쓰지 말 것 | NTP·`date` 설정으로 시계가 점프하면 루프가 멈추거나 폭주 |
| 메모리 잠금 + prefault | `mlockall(MCL_CURRENT or MCL_FUTURE)` + 스택·힙 미리 건드리기 | 첫 접근 page fault, 메모리 압박 시 major fault (§9) |
| RT 정책 | `sched_setscheduler(0, SCHED_FIFO, ...)` 80 | 다른 SCHED_OTHER 태스크와 시간 나눔 → ms~s 지연 |
| CPU 고정 | `sched_setaffinity` (+ §6 격리) | migration, 캐시 냉각, 다른 CPU의 잡음까지 흡수 |

순서도 중요하다. **잠금·prefault·스레드 생성·파일 열기 같은 "느린 준비"를 모두 끝낸 뒤** RT 정책으로 바꾸고 루프에 들어간다. RT 우선순위로 수백 ms짜리 초기화를 하면 그동안 같은 CPU의 다른 일이 다 멈춘다.

### 5.3 예제 3 — 자기 지터를 재는 정석 루프 `n2_rt`

무엇을 확인하나: 인자로 정책(0이면 SCHED_OTHER, 아니면 FIFO 우선순위), CPU 고정, `mlockall`+prefault를 켜고 끌 수 있는 1 kHz 루프. 매 주기 "목표 시각보다 몇 µs 늦게 깨어났나"를 기록해 p50/p99/p99.9/max, 5구간 히스토그램, 총 드리프트, 루프 중 minor fault와 비자발 문맥 전환 수를 찍는다. 루프 본문은 4 MiB 버퍼를 페이지 단위로 하나씩 건드린다 — "처음 쓰는 버퍼"(로그·맵 버퍼)를 흉내 낸 것이다.

```c
/* n2_rt: 1 kHz periodic loop (clock_nanosleep TIMER_ABSTIME) measuring its own wakeup latency.
 * usage: n2_rt <fifo_prio 0=OTHER> <cpu -1=any> <lock 0/1> <iterations> [period_us=1000] */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/resource.h>
#define NS 1000000000L
static long ns(struct timespec t) { return t.tv_sec * NS + t.tv_nsec; }
static char work[4 << 20];                    /* 4 MiB buffer the loop touches page by page */
static int lat[200000];
static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static void prefault_stack(void) { volatile char s[512 << 10]; memset((char *)s, 0, sizeof s); }
int main(int argc, char **argv)
{
	if (argc < 5) { fprintf(stderr, "usage\n"); exit(1); }
	int prio = atoi(argv[1]), cpu = atoi(argv[2]), lock = atoi(argv[3]);
	long n = atol(argv[4]), max = 0, PERIOD = argc > 5 ? atol(argv[5]) * 1000 : 1000000;
	if (cpu >= 0) {
		cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
		if (sched_setaffinity(0, sizeof s, &s)) { perror("sched_setaffinity"); exit(1); }
	}
	if (lock) {                               /* lock + prefault everything before the loop */
		if (mlockall(MCL_CURRENT | MCL_FUTURE)) { perror("mlockall"); exit(1); }
		prefault_stack();
	}
	if (prio > 0) {
		struct sched_param sp = { .sched_priority = prio };
		if (sched_setscheduler(0, SCHED_FIFO, &sp)) { perror("sched_setscheduler"); exit(1); }
	}
	struct rusage r0, r1; getrusage(RUSAGE_SELF, &r0);
	struct timespec next, now; clock_gettime(CLOCK_MONOTONIC, &next);
	long t0 = ns(next);
	for (long i = 0; i < n; i++) {
		next.tv_nsec += PERIOD;
		if (next.tv_nsec >= NS) { next.tv_nsec -= NS; next.tv_sec++; }
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
		clock_gettime(CLOCK_MONOTONIC, &now);
		lat[i] = (int)((ns(now) - ns(next)) / 1000);   /* us late */
		if (lat[i] > max) max = lat[i];
		work[(i * 4096) % sizeof work]++;     /* "control work": touches a new page each cycle */
	}
	clock_gettime(CLOCK_MONOTONIC, &now); getrusage(RUSAGE_SELF, &r1);
	long hist[6] = {0}, edge[5] = {50, 100, 500, 1000, 5000};
	for (long i = 0; i < n; i++) { int b = 0; while (b < 5 && lat[i] >= edge[b]) b++; hist[b]++; }
	qsort(lat, n, sizeof lat[0], cmp);
	printf("prio=%d cpu=%d lock=%d n=%ld per=%ldus p50=%d p99=%d p99.9=%d max=%ld us drift=%ld us minflt=%ld nivcsw=%ld\n",
	       prio, cpu, lock, n, PERIOD / 1000, lat[n / 2], lat[n * 99 / 100], lat[n * 999 / 1000], max,
	       (ns(now) - t0 - n * PERIOD) / 1000, r1.ru_minflt - r0.ru_minflt, r1.ru_nivcsw - r0.ru_nivcsw);
	printf("  hist <50:%ld 50-100:%ld 100-500:%ld 0.5-1ms:%ld 1-5ms:%ld >=5ms:%ld\n",
	       hist[0], hist[1], hist[2], hist[3], hist[4], hist[5]);
	return 0;
}
```

빌드와 실행(Lima VM 호스트, 6 vCPU, RT 정책에는 root가 필요해 `sudo`):

```sh
gcc -static -std=gnu11 -Wall -Wextra -O2 n2_rt.c -o n2_rt
for c in "0 -1 0" "0 5 0" "80 -1 0" "80 5 0" "80 5 1"; do sudo ./n2_rt $c 5000; done
```

배경 부하: 이 VM에는 다른 작성자 16명의 QEMU·커널 빌드가 돌고 있었다(load average 23, 6 CPU). CPU5에는 `nice 19` hog 하나를 추가로 묶었다.

```text
prio=0 cpu=-1 lock=0 n=5000 per=1000us p50=67 p99=21426 p99.9=35586 max=37482 us drift=8890 us minflt=2054 nivcsw=2
  hist <50:5 50-100:3867 100-500:225 0.5-1ms:213 1-5ms:437 >=5ms:253
prio=0 cpu=5 lock=0 n=5000 per=1000us p50=69 p99=903954 p99.9=935984 max=938941 us drift=97829 us minflt=2054 nivcsw=42
  hist <50:5 50-100:2837 100-500:170 0.5-1ms:217 1-5ms:401 >=5ms:1370
prio=80 cpu=-1 lock=0 n=5000 per=1000us p50=15 p99=3134 p99.9=16481 max=20432 us drift=16 us minflt=2054 nivcsw=0
  hist <50:4708 50-100:81 100-500:78 0.5-1ms:34 1-5ms:66 >=5ms:33
prio=80 cpu=5 lock=0 n=5000 per=1000us p50=13 p99=2521 p99.9=11467 max=15164 us drift=13 us minflt=2054 nivcsw=0
  hist <50:4611 50-100:94 100-500:114 0.5-1ms:57 1-5ms:102 >=5ms:22
prio=80 cpu=5 lock=1 n=5000 per=1000us p50=15 p99=2370 p99.9=20688 max=24265 us drift=21 us minflt=1 nivcsw=0
  hist <50:4770 50-100:66 100-500:64 0.5-1ms:23 1-5ms:48 >=5ms:29
```

출력에서 볼 것:

- **SCHED_OTHER의 p50이 65~69 µs, FIFO는 13~15 µs.** 이 50 µs 차이는 스케줄링 때문이 아니라 **timer slack** 때문이다. 일반 태스크의 hrtimer sleep에는 기본 50 µs의 여유(slack)가 붙어 커널이 근처 타이머와 묶어 깨운다(전력 절약). RT·DEADLINE 정책으로 바꾸면 커널이 `timer_slack_ns`를 0으로 만든다(6.18 `kernel/sched/syscalls.c`: "rt-policy tasks do not have a timerslack"). 직접 확인: `cat /proc/self/timerslack_ns` → `50000`, `sudo chrt -f 80 cat /proc/self/timerslack_ns` → `0` (F8 §9.4).
- **SCHED_OTHER + CPU5 고정(같은 CPU에 다른 일)이 최악이다**: p99가 0.9 s. 같은 CPU에 runnable 태스크가 여럿이면 fair 클래스는 "비율"로 나눌 뿐 "제때"를 약속하지 않는다. 고정은 RT 정책 없이 하면 오히려 해롭다 — 도망갈 CPU가 없어진다. hog 없이 다시 재도 CPU5의 OTHER 루프는 p99 = 259 ms였다(다른 작성자들의 QEMU 스레드가 그 CPU에 있었다).
- **FIFO로 바꾸는 순간 p99가 3 ms 이하**, 히스토그램의 94%가 50 µs 안. 남은 꼬리(수 ms~20 ms)는 Mac 호스트가 이 VM의 vCPU 자체를 재운 시간이라 게스트 안의 어떤 손잡이로도 안 없어진다 — 실제 보드에서는 이 자리가 SMI·펌웨어·하이퍼바이저 잡음이다(N1).
- **`minflt=2054` → `minflt=1`.** 잠금+prefault 없이 루프 안에서 4 MiB 버퍼의 페이지 1024개를 처음 건드리며 page fault가 2천 번 났다. `mlockall(MCL_CURRENT)`은 이미 매핑된 영역(bss의 `work[]`, `lat[]` 포함)을 그 자리에서 전부 채운다(populate). 그래서 루프 안 fault가 0이 된다. 이 VM에서는 fault 하나가 수 µs라 지터 차이가 작게 보이지만, 저가 SoC의 느린 DRAM·메모리 압박 상황에서는 fault 하나가 수십 µs~수 ms(major fault = 플래시 읽기)가 된다.
- **`drift`는 모두 수십 µs 이하**(OTHER 고정은 98 ms — 너무 늦어서 몇 주기는 따라잡지 못한 채 끝났다). ABSTIME 루프는 늦게 깨면 다음 sleep이 즉시 반환되며 "따라잡는다". 그래서 큰 정지 한 번이 연속된 여러 개의 큰 latency로 기록된다 — 히스토그램 꼬리가 두꺼운 이유다. 실무에서는 "몇 주기를 놓쳤나(overrun)"를 따로 세서 보고한다.

### 5.4 예제 4 — "처음 짜는" 나쁜 루프 `n2_bad`

무엇을 확인하나: 흔히 처음 짜는 모양 — 상대 `usleep(1000)`, 매 주기 256 KiB `malloc`+`memset`+`free`, 매 주기 `fprintf`+`fflush`. 같은 조건에서 드리프트와 주기 범위를 잰다.

```c
/* n2_bad: the "typical first try" 1 kHz loop - relative usleep, malloc+memset, fprintf+fflush.
 * usage: n2_bad <fifo_prio 0=OTHER> <cpu -1=any> <iterations> <logfile> */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#define NS 1000000000L
static long now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * NS + t.tv_nsec; }
int main(int argc, char **argv)
{
	if (argc < 5) { fprintf(stderr, "usage\n"); exit(1); }
	int prio = atoi(argv[1]), cpu = atoi(argv[2]); long n = atol(argv[3]);
	FILE *f = fopen(argv[4], "w"); if (!f) { perror("fopen"); exit(1); }
	if (cpu >= 0) {
		cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
		if (sched_setaffinity(0, sizeof s, &s)) { perror("sched_setaffinity"); exit(1); }
	}
	if (prio > 0) {
		struct sched_param sp = { .sched_priority = prio };
		if (sched_setscheduler(0, SCHED_FIFO, &sp)) { perror("sched_setscheduler"); exit(1); }
	}
	struct rusage r0, r1; getrusage(RUSAGE_SELF, &r0);
	long t0 = now_ns(), prev = t0, pmin = NS, pmax = 0, late_max = 0;
	for (long i = 1; i <= n; i++) {
		usleep(1000);                              /* BAD: relative sleep, error accumulates */
		long t = now_ns(), p = t - prev; prev = t;
		if (p < pmin) pmin = p;
		if (p > pmax) pmax = p;
		long late = t - (t0 + i * 1000000L);       /* lateness vs the ideal absolute schedule */
		if (late > late_max) late_max = late;
		char *buf = malloc(256 << 10);             /* BAD: 256 KiB > mmap threshold -> mmap+faults */
		if (!buf) { perror("malloc"); exit(1); }
		memset(buf, (int)i, 256 << 10);
		fprintf(f, "i=%ld t=%ld period=%ld\n", i, t, p);   /* BAD: stdio + syscall in RT path */
		fflush(f);
		free(buf);                                 /* munmap -> next malloc faults again */
	}
	long total = now_ns() - t0; getrusage(RUSAGE_SELF, &r1);
	printf("bad prio=%d cpu=%d n=%ld total=%.1f ms (ideal %ld) drift=%ld us period min/avg/max=%ld/%ld/%ld us"
	       " late_max=%ld us minflt=%ld\n", prio, cpu, n, total / 1e6, n, (total - n * 1000000L) / 1000,
	       pmin / 1000, total / n / 1000, pmax / 1000, late_max / 1000, r1.ru_minflt - r0.ru_minflt);
	fclose(f);
	return 0;
}
```

```sh
sudo ./n2_bad 0 -1 5000 /var/tmp/N2/b.log      # SCHED_OTHER
sudo ./n2_bad 80 5 5000 /var/tmp/N2/b.log      # FIFO 80 + CPU5
```

```text
bad prio=0 cpu=-1 n=5000 total=5558.7 ms (ideal 5000) drift=558730 us period min/avg/max=1013/1111/21854 us late_max=558719 us minflt=2
bad prio=80 cpu=5 n=5000 total=5205.1 ms (ideal 5000) drift=205148 us period min/avg/max=1011/1041/20632 us late_max=205142 us minflt=2
```

출력에서 볼 것:

- **5초 동안 0.56 s(OTHER) / 0.21 s(FIFO) 밀렸다.** 주기가 최소 1011 µs — 한 번도 1 ms를 지키지 못했다. FIFO로 올려도 드리프트는 남는다. **정책은 나쁜 루프 구조를 고쳐 주지 않는다.** 1 kHz 제어에서 평균 주기 1041 µs면 제어기의 이산화 시간 dt가 4% 틀린 것이다(PID의 적분·미분항이 그만큼 틀린다).
- **`minflt=2`**: 256 KiB는 glibc의 기본 mmap 임계값(128 KiB)보다 커서 처음엔 `mmap`으로 받지만, `free()` 때 glibc가 **임계값을 그 크기로 동적으로 올려서** 두 번째부터는 heap에서 재사용했다. "malloc in loop가 늘 fault를 낸다"는 틀렸고, "언제 fault·syscall을 낼지 **모른다**"가 맞다. 그래서 RT 경로에서는 malloc 자체를 빼고, 꼭 써야 하면 `mallopt(M_MMAP_MAX, 0)`, `mallopt(M_TRIM_THRESHOLD, -1)`로 glibc가 메모리를 OS에 돌려주지 못하게 한다(§9).
- `fprintf`+`fflush`는 매 주기 `write()` syscall이다. 파일시스템·블록 계층에서 lock을 잡거나 writeback에 막히면 그 시간이 그대로 루프에 들어간다(`period max=21854 us`).

### 5.5 예제 5 — printf 대신 SPSC 링 + 로거 스레드 `n2_spsc`

```svg
<svg viewBox="0 0 660 230" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="50" width="150" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="85" y="76" text-anchor="middle" font-size="13">제어 루프</text>
  <text x="85" y="96" text-anchor="middle" font-size="12">FIFO 80 · CPU3</text>
  <text x="85" y="112" text-anchor="middle" font-size="12">producer</text>
  <rect x="440" y="50" width="200" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="540" y="76" text-anchor="middle" font-size="13">로거 스레드</text>
  <text x="540" y="96" text-anchor="middle" font-size="12">SCHED_OTHER · CPU0</text>
  <text x="540" y="112" text-anchor="middle" font-size="12">10 ms마다 비움</text>
  <rect x="200" y="65" width="26" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="228" y="65" width="26" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="256" y="65" width="26" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="284" y="65" width="26" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="312" y="65" width="26" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="340" y="65" width="26" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="368" y="65" width="26" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="396" y="65" width="26" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="312" y="55" text-anchor="middle" font-size="12">ring[1024] (정적, mlock됨)</text>
  <text x="263" y="124" text-anchor="middle" font-size="12">tail</text>
  <text x="368" y="124" text-anchor="middle" font-size="12">head</text>
  <line x1="162" y1="85" x2="196" y2="85" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="426" y1="85" x2="436" y2="85" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="540" y1="122" x2="540" y2="150" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="455" y="152" width="170" height="30" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="540" y="172" text-anchor="middle" font-size="12">fprintf · fflush · write</text>
  <text x="10" y="160" font-size="12">RT 경로: 슬롯 쓰기 + release store 하나 — syscall 0개</text>
  <text x="10" y="180" font-size="12">꽉 차면 버리고 dropped++ — 절대 기다리지 않는다</text>
  <text x="10" y="200" font-size="12">head는 producer만, tail은 consumer만 쓴다 → lock 불필요</text>
  <text x="10" y="24" font-size="13">printf를 RT 루프 밖으로: 단일 생산자·단일 소비자(SPSC) 링</text>
</svg>
```

그림 5 — RT 루프는 링에 쓰기만 하고, I/O는 낮은 우선순위의 로거 스레드가 다른 CPU에서 한다. SSD 펌웨어의 "호스트 큐 → 백엔드 큐" 분리와 같은 구조다.

무엇을 확인하나: FIFO 80·CPU3의 루프가 매 주기 기록을 lock-free SPSC 링에 넣고, CPU0의 SCHED_OTHER 로거가 10 ms마다 비워 파일에 쓴다. 링이 가득 차면 루프는 기다리지 않고 버린다.

```c
/* n2_spsc: RT loop (FIFO 80, CPU3) hands log records to a SCHED_OTHER logger thread (CPU0)
 * through a lock-free single-producer/single-consumer ring. The RT path never calls stdio. */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#define NS 1000000000L
#define RING 1024                                   /* power of two */
struct rec { long i, lat_ns; };
static struct rec ring[RING];
static _Atomic unsigned long head, tail;            /* head: producer only, tail: consumer only */
static _Atomic int done; static long dropped, written, maxlat;
static int push(struct rec r) {
	unsigned long h = atomic_load_explicit(&head, memory_order_relaxed);
	if (h - atomic_load_explicit(&tail, memory_order_acquire) == RING) return 0;   /* full */
	ring[h & (RING - 1)] = r;
	atomic_store_explicit(&head, h + 1, memory_order_release); return 1;
}
static void pin(int cpu) { cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s); pthread_setaffinity_np(pthread_self(), sizeof s, &s); }
static void *logger(void *arg) {
	FILE *f = arg; pin(0);
	struct timespec ten = { 0, 10000000 };
	for (;;) {
		unsigned long t = atomic_load_explicit(&tail, memory_order_relaxed);
		unsigned long h = atomic_load_explicit(&head, memory_order_acquire);
		for (; t != h; t++, written++) fprintf(f, "%ld %ld\n", ring[t & (RING - 1)].i, ring[t & (RING - 1)].lat_ns);
		atomic_store_explicit(&tail, t, memory_order_release); fflush(f);
		if (atomic_load(&done) && t == atomic_load(&head)) return NULL;
		nanosleep(&ten, NULL);                       /* drain every 10 ms */
	}
}
int main(int argc, char **argv) {
	long n = argc > 1 ? atol(argv[1]) : 5000;
	FILE *f = fopen(argc > 2 ? argv[2] : "/tmp/n2_spsc.log", "w"); if (!f) { perror("fopen"); exit(1); }
	if (mlockall(MCL_CURRENT | MCL_FUTURE)) { perror("mlockall"); exit(1); }
	pthread_t lt; if (pthread_create(&lt, NULL, logger, f)) { perror("pthread_create"); exit(1); }
	pin(3); struct sched_param sp = { .sched_priority = 80 };
	if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp)) { perror("setschedparam"); exit(1); }
	struct timespec next, now; clock_gettime(CLOCK_MONOTONIC, &next);
	for (long i = 0; i < n; i++) {
		next.tv_nsec += 1000000; if (next.tv_nsec >= NS) { next.tv_nsec -= NS; next.tv_sec++; }
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
		clock_gettime(CLOCK_MONOTONIC, &now);
		long lat = (now.tv_sec - next.tv_sec) * NS + now.tv_nsec - next.tv_nsec;
		if (lat > maxlat) maxlat = lat;
		if (!push((struct rec){ i, lat })) dropped++;          /* never block the RT loop */
	}
	atomic_store(&done, 1); pthread_join(lt, NULL); fclose(f);
	printf("spsc n=%ld written=%ld dropped=%ld rt_max_lat=%ld us\n", n, written, dropped, maxlat / 1000);
	return 0;
}
```

```sh
gcc -static -std=gnu11 -Wall -Wextra -O2 -pthread n2_spsc.c -o n2_spsc
sudo ./n2_spsc 5000 /var/tmp/N2/s.log
```

```text
spsc n=5000 written=5000 dropped=0 rt_max_lat=18972 us
```

출력에서 볼 것: 5000개 기록이 하나도 버려지지 않고 파일에 갔고, RT 루프는 syscall 없이 돌았다(max 19 ms는 위와 같은 호스트 vCPU 정지). 메모리 순서: producer는 슬롯을 쓴 뒤 `head`를 **release**로, consumer는 `head`를 **acquire**로 읽고 나서 슬롯을 읽는다 — F5의 메모리 배리어 짝 그대로다. `head - tail == RING`으로 가득 참을 판정하려고 인덱스를 계속 증가시키고 `& (RING - 1)`로 접는다(링 크기는 2의 거듭제곱). 링 크기는 "로거가 가장 오래 멈출 수 있는 시간 × 생산 속도"로 정한다 — 10 ms 주기 로거가 100 ms 막혀도 1 kHz면 100개, 1024면 여유가 있다.

함정: 로거 스레드를 만든 **뒤에** 메인 스레드를 FIFO로 바꿨다. 반대로 하면 `pthread_create`가 FIFO를 물려받아 로거까지 RT가 된다(§2.3).
---

## 6. CPU 격리 — 같은 CPU에 몰래 들어오는 일을 내보낸다

### 6.1 직관 — 코어 하나를 "bare-metal 코어"로 만든다

FIFO 80으로 올려도 그 CPU에는 여전히 tick 인터럽트(HZ=1000이면 1 ms마다), 그 CPU로 라우팅된 디바이스 IRQ, RCU callback, 그 CPU에 붙은 kworker, 다른 태스크의 migration이 들어온다. 우선순위가 높으니 대부분 금방 끝나지만, **그 시간이 그대로 내 루프의 지터**다. 격리는 이 일들을 housekeeping CPU로 몰아서, 격리된 CPU에는 내 루프와 어쩔 수 없는 per-CPU 커널 스레드만 남게 하는 것이다. Don의 SSD 펌웨어로 치면 "호스트 명령 처리 코어에는 GC·로그·온도 감시 태스크를 절대 배치하지 않는다"와 같다.

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
  <rect x="20" y="40" width="440" height="210" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="240" y="28" text-anchor="middle" font-size="13">housekeeping CPU0 · CPU1 · CPU2 — "잡일은 전부 여기"</text>
  <text x="130" y="90" text-anchor="middle" font-size="12">IRQ (virtio, uart, SPI …)</text>
  <text x="350" y="90" text-anchor="middle" font-size="12">tick 1000 Hz</text>
  <text x="130" y="140" text-anchor="middle" font-size="12">RCU callback (rcuo 스레드)</text>
  <text x="350" y="140" text-anchor="middle" font-size="12">unbound kworker · workqueue</text>
  <text x="130" y="190" text-anchor="middle" font-size="12">systemd · ROS 노드 · 셸</text>
  <text x="350" y="190" text-anchor="middle" font-size="12">로거 · 통신 · 카메라 파이프라인</text>
  <rect x="475" y="40" width="170" height="210" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="560" y="28" text-anchor="middle" font-size="13">isolated CPU3</text>
  <text x="560" y="70" text-anchor="middle" font-size="12">제어 루프 FIFO 80 하나</text>
  <text x="560" y="98" text-anchor="middle" font-size="12">tick 정지 (nohz_full)</text>
  <text x="560" y="126" text-anchor="middle" font-size="12">RCU cb 없음 (rcu_nocbs)</text>
  <text x="560" y="154" text-anchor="middle" font-size="12">load balancing 없음</text>
  <text x="560" y="182" text-anchor="middle" font-size="12">기본 IRQ 없음</text>
  <text x="560" y="210" text-anchor="middle" font-size="12">per-CPU kthread만 대기</text>
  <text x="560" y="242" text-anchor="middle" font-size="12">ksoftirqd/3 · ktimers/3</text>
  <text x="20" y="278" font-size="12">boot: isolcpus=domain,managed_irq,3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2</text>
  <text x="20" y="300" font-size="12">또는 런타임: cgroup v2 cpuset.cpus.partition = isolated (CPU3) + IRQ affinity 수동</text>
  <text x="20" y="322" font-size="12">CPU0은 housekeeping에 남긴다 (nohz_full은 부트 CPU를 받지 않는다)</text>
</svg>
```

그림 6 — housekeeping CPU와 격리 CPU의 분업. 부트 파라미터 네 개가 각각 어떤 줄을 오른쪽에서 왼쪽으로 옮기는지 아래 표에 정리했다.

### 6.2 손잡이 하나씩

| 파라미터 | 옮기는 것 | 필요한 config | 6.18에서의 주의 |
|---|---|---|---|
| `isolcpus=domain,3` (플래그 생략 시 domain) | scheduler load balancing에서 CPU3 제외 → 명시적으로 고정한 태스크만 CPU3에 온다. unbound workqueue 기본 마스크에서도 빠진다 | `CPU_ISOLATION` | 커널 문서에 "[Deprecated - use cpusets instead]". 런타임에 되돌릴 수 없다 |
| `isolcpus=managed_irq,3` | managed IRQ(멀티큐 장치)의 affinity에 housekeeping CPU가 섞여 있으면 housekeeping 쪽으로 | 〃 | 큐 마스크가 CPU3뿐이면 효과 없음 — 대신 CPU3에서 I/O를 안 하면 안 온다 (§8) |
| `isolcpus=nohz,3` = `nohz_full=3` | 태스크가 하나만 runnable이면 tick 정지(adaptive-tick), RCU callback offload | `NO_HZ_FULL` | 부트 CPU는 못 넣는다. 잔여 1 Hz tick이 남을 수 있다(문서: workqueue로 offload) |
| `rcu_nocbs=3` | CPU3의 RCU callback을 `rcuo` 커널 스레드(housekeeping에서 실행)로 | `RCU_NOCB_CPU` | `nohz_full`이 이미 포함하지만 명시하는 관례 |
| `irqaffinity=0-2` | 새로 등록되는 IRQ의 기본 affinity | — | managed IRQ에는 적용 안 됨. 이미 등록된 IRQ는 `/proc/irq`로 |
| (`kthread_cpus=`) | — | — | **6.18의 kernel-parameters.txt에 없다.** unbound kthread의 배치는 housekeeping 마스크(`HK_TYPE_KERNEL_NOISE`/`HK_TYPE_DOMAIN`)를 따르고, 필요하면 `taskset -pc`로 옮긴다 |

`kernel/sched/isolation.c`가 이 파라미터들을 housekeeping 마스크로 바꾸고, 타이머·workqueue·kthread·IRQ 코드가 `housekeeping_cpumask(HK_TYPE_...)`를 참조한다. 6.18 `include/linux/sched/isolation.h`에서는 `HK_TYPE_DOMAIN`, `HK_TYPE_MANAGED_IRQ`, `HK_TYPE_KERNEL_NOISE` 세 개로 정리됐고 예전 이름(`HK_TYPE_TICK`, `HK_TYPE_RCU`, `HK_TYPE_WQ`, `HK_TYPE_KTHREAD` …)은 모두 `HK_TYPE_KERNEL_NOISE`의 별칭이다.

### 6.3 예제 6 — 격리가 "먹혔는지" 증명하기

무엇을 확인하나: 같은 Image-rt를 (A) 파라미터 없이, (B) `isolcpus=domain,managed_irq,3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2`로 부팅해 sysfs 값, dmesg, workqueue 마스크, IRQ effective affinity, CPU3에 마지막으로 있었던 태스크, 그리고 **CPU3에 busy 태스크 하나만 둘 때의 tick 수**를 비교한다.

```sh
echo "-- dmesg lines about isolation"
dmesg | grep -iE "nohz|housekeep|offload|nocb|isolcpus" | grep -v "command line" | sed 's/^\[[^]]*\] *//' | head -12
echo "-- workqueue cpumask: $(cat /sys/devices/virtual/workqueue/cpumask)"
echo "-- default irq affinity: $(cat /proc/irq/default_smp_affinity)"
echo "-- effective affinity of device IRQs:"
for i in /proc/irq/[0-9]*; do a=$(cat $i/effective_affinity_list 2>/dev/null); n=$(ls $i | grep -vE "smp|affin|node|spurious"); [ -n "$n" ] && echo "   irq $(basename $i) eff=$a $n"; done
echo "-- tasks whose last CPU is 3:"
for s in /proc/[0-9]*/stat; do awk '$39==3 { sub(/^[^(]*\(/, ""); sub(/\).*/, ""); print }' $s 2>/dev/null; done | sort | tr '\n' ' '; echo
echo "-- tick test: one busy task alone on CPU3 for 5 s (arch_timer per CPU)"
taskset -c 3 sh -c 'while :; do :; done' & sleep 1
grep arch_timer /proc/interrupts; sleep 5; grep arch_timer /proc/interrupts; kill $!
```

(B) 격리 부팅:

```text
lab initramfs: 6.18.55-rt up 7.49s
== kernel 6.18.55-rt  cmdline: console=ttyAMA0 quiet isolcpus=domain,managed_irq,3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2 
   /sys/devices/system/cpu/isolated = '3'
   /sys/devices/system/cpu/nohz_full = '3'
-- dmesg lines about isolation
[    T0] rcu: 	Offload RCU callbacks from CPUs: 3.
[    T1] watchdog: Disabling watchdog on nohz_full cores by default
-- workqueue cpumask: 7
-- default irq affinity: 7
-- effective affinity of device IRQs:
   irq 13 eff=0 uart-pl011
   irq 17 eff=0 virtio1-config
   irq 18 eff=0 virtio1-req.0
   irq 19 eff=1 virtio1-req.1
   irq 20 eff=2 virtio1-req.2
   irq 21 eff=3 virtio1-req.3
   irq 22 eff=0 virtio0-config
   irq 23 eff=0 virtio0-input.0
   irq 24 eff=0 virtio0-output.0
   irq 25 eff=0 rtc-pl031
   irq 26 eff= arm-pmu
   irq 27 eff= GPIO Key Poweroff
-- tasks whose last CPU is 3:
backlog_napi/3 cpuhp/3 irq/21-virtio1-req.3 irq_work/3 ksoftirqd/3 ktimers/3 kworker/3:0-events kworker/3:1-events migration/3 rcuc/3 
-- tick test: one busy task alone on CPU3 for 5 s (arch_timer per CPU)
 11:      13712      10537      10118        547 GIC-0  27 Level     arch_timer
 11:      18071      10705      10266        547 GIC-0  27 Level     arch_timer
[   28.491688][  T363] reboot: Power down
```

(A) 파라미터 없이 (비교용, 같은 스크립트):

```text
lab initramfs: 6.18.55-rt up 7.39s
== kernel 6.18.55-rt  cmdline: console=ttyAMA0 quiet  
   /sys/devices/system/cpu/isolated = ''
   /sys/devices/system/cpu/nohz_full = '(null)'
-- dmesg lines about isolation
-- workqueue cpumask: f
-- default irq affinity: f
-- effective affinity of device IRQs:
   irq 13 eff=0 uart-pl011
   irq 17 eff=0 virtio1-config
   irq 18 eff=0 virtio1-req.0
   irq 19 eff=1 virtio1-req.1
   irq 20 eff=2 virtio1-req.2
   irq 21 eff=3 virtio1-req.3
   irq 22 eff=0 virtio0-config
   irq 23 eff=0 virtio0-input.0
   irq 24 eff=0 virtio0-output.0
   irq 25 eff=0 rtc-pl031
   irq 26 eff= arm-pmu
   irq 27 eff= GPIO Key Poweroff
-- tasks whose last CPU is 3:
backlog_napi/3 cpuhp/3 irq/21-virtio1-req.3 irq_work/3 kauditd ksoftirqd/3 ktimers/3 kworker/3:0-mm_percpu_wq kworker/3:0H-events_highpri kworker/3:1 kworker/3:1H kworker/u16:0-events_unbound migration/3 rcu_tasks_kthread rcu_tasks_trace_kthread rcuc/3 
-- tick test: one busy task alone on CPU3 for 5 s (arch_timer per CPU)
 11:       8834       8308       8096       8383 GIC-0  27 Level     arch_timer
 11:       9213       8430       8207      10931 GIC-0  27 Level     arch_timer
[   28.172884][  T352] reboot: Power down
```

출력에서 볼 것:

- `/sys/devices/system/cpu/isolated = '3'`, `nohz_full = '3'`, dmesg의 `Offload RCU callbacks from CPUs: 3.` — 파라미터가 받아들여졌다. 격리 없는 부팅에서 `nohz_full`이 `(null)`로 읽히는 것은 이 커널의 실제 출력이다(마스크가 할당되지 않음). 덤으로 `watchdog: Disabling watchdog on nohz_full cores by default` — soft/hard lockup detector도 tick을 쓰기 때문에 격리 CPU에서 끈다.
- **workqueue cpumask `7`(= CPU0~2) vs `f`** — unbound workqueue가 CPU3을 피한다.
- **tick 테스트**: CPU3에 busy 태스크 하나를 두고 5초 — 격리하면 `arch_timer` CPU3 카운트가 `547 → 547`, **5초 동안 tick 0번.** 격리 없으면 `8383 → 10931`(5초에 2548번, TCG라 1000 Hz를 다 못 채웠다). 이것이 `nohz_full`의 실체다: runnable 태스크가 하나뿐이면 스케줄러가 시간을 나눌 필요가 없으니 tick을 멈춘다. 태스크가 둘이 되면 tick이 돌아온다(§16 문제 3).
- **CPU3에 있었던 태스크**: 격리 시 per-CPU 커널 스레드(`migration/3`, `ksoftirqd/3`, `ktimers/3`, `rcuc/3`, `cpuhp/3`, `irq_work/3`, `backlog_napi/3`, `kworker/3:*`)와 `irq/21-virtio1-req.3`뿐. 격리 없으면 `kauditd`, `kworker/u16:0-events_unbound`, `rcu_tasks_kthread` 같은 unbound 일꾼이 섞여 있다.
- **`virtio1-req.3`의 effective affinity는 격리해도 3이다.** virtio-blk가 CPU마다 큐 하나씩(4개)을 만들고 큐 3의 마스크가 {CPU3}뿐이라 `managed_irq`가 손댈 수 없다(문서의 "If a queue's affinity mask contains only isolated CPUs then this parameter has no effect"). 대신 **CPU3에서 I/O를 제출하지 않으면 그 IRQ는 오지 않는다** — §6.4 결과에서 격리 후 `virtio1-req.3` 카운트가 0이었다.

### 6.4 예제 7 — 같은 부하, 격리 전후의 지터와 CPU3 인터럽트

무엇을 확인하나: `n2_run.sh jitter` — 부하(unpinned CPU hog 2개 + 50 ms마다 64 KiB × 200 direct read + 200 Hz ping)를 켠 채 `n2_rt`의 네 설정을 100 Hz × 500주기로 돌리고 끝에 `/proc/interrupts`를 찍는다. 같은 Image-rt를 격리 없이/격리해서 부팅했다.

```sh
load() {   # ${HOGS:-1} unpinned CPU hog(s) + block I/O loop + 200 Hz ping (network RX IRQs)
	for i in $(seq ${HOGS:-1}); do (while :; do :; done) & done
	[ -n "$NOIO" ] || (while :; do dd if=/dev/vda of=/dev/null bs=64k count=200 iflag=direct 2>/dev/null; sleep 0.05; done) &
	net; [ -n "$NOIO" ] || ping -i 0.005 -q 10.0.2.2 > /dev/null 2>&1 &
	sleep 1
}
# jitter)
load; /n2_rt 0 -1 0 $N $P; /n2_rt 80 -1 0 $N $P; /n2_rt 80 3 0 $N $P; /n2_rt 80 3 1 $N $P
grep -E "CPU0|arch_timer|virtio0-input|virtio1-req|Resched|Function" /proc/interrupts
```

(A) Image-rt, 격리 없음 (`HOGS=2 P=10000`은 커널 cmdline으로 넘긴 환경 변수):

```text
lab initramfs: 6.18.55-rt up 6.41s
== kernel 6.18.55-rt  cmdline: console=ttyAMA0 quiet HOGS=2 P=10000 
   /sys/devices/system/cpu/isolated = ''
   /sys/devices/system/cpu/nohz_full = '(null)'
prio=0 cpu=-1 lock=0 n=500 per=10000us p50=42659 p99=319094 p99.9=332055 max=332055 us drift=49706 us minflt=1002 nivcsw=456
  hist <50:0 50-100:0 100-500:16 0.5-1ms:7 1-5ms:50 >=5ms:427
prio=80 cpu=-1 lock=0 n=500 per=10000us p50=4037 p99=79122 p99.9=86556 max=86556 us drift=2783 us minflt=1002 nivcsw=1
  hist <50:0 50-100:1 100-500:73 0.5-1ms:27 1-5ms:178 >=5ms:221
prio=80 cpu=3 lock=0 n=500 per=10000us p50=2550 p99=39272 p99.9=57805 max=57805 us drift=57306 us minflt=1002 nivcsw=2
  hist <50:0 50-100:2 100-500:118 0.5-1ms:34 1-5ms:166 >=5ms:180
prio=80 cpu=3 lock=1 n=500 per=10000us p50=1638 p99=20619 p99.9=40203 max=40203 us drift=144 us minflt=1 nivcsw=0
  hist <50:0 50-100:7 100-500:89 0.5-1ms:105 1-5ms:228 >=5ms:71
-- /proc/interrupts after the runs:
           CPU0       CPU1       CPU2       CPU3       
 11:      10568       9021      10481       9780 GIC-0  27 Level     arch_timer
 18:       1432          0          0          0 GICv2m-PCI-MSIX-0000:00:02.0   1 Edge      virtio1-req.0
 19:          0        318          0          0 GICv2m-PCI-MSIX-0000:00:02.0   2 Edge      virtio1-req.1
 20:          0          0       1010          0 GICv2m-PCI-MSIX-0000:00:02.0   3 Edge      virtio1-req.2
 21:          0          0          0       1020 GICv2m-PCI-MSIX-0000:00:02.0   4 Edge      virtio1-req.3
 23:       1857          0          0          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
IPI0:       703        643        590        839       Rescheduling interrupts
IPI1:      1663        738        616       1258       Function call interrupts
[   31.784905][  T177] reboot: Power down
```

(B) Image-rt + `isolcpus=domain,managed_irq,3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2`:

```text
lab initramfs: 6.18.55-rt up 6.84s
== kernel 6.18.55-rt  cmdline: console=ttyAMA0 quiet HOGS=2 P=10000 isolcpus=domain,managed_irq,3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2 
   /sys/devices/system/cpu/isolated = '3'
   /sys/devices/system/cpu/nohz_full = '3'
prio=0 cpu=-1 lock=0 n=500 per=10000us p50=11820 p99=197208 p99.9=209831 max=209831 us drift=82308 us minflt=1002 nivcsw=357
  hist <50:0 50-100:0 100-500:39 0.5-1ms:27 1-5ms:106 >=5ms:328
prio=80 cpu=-1 lock=0 n=500 per=10000us p50=2595 p99=82801 p99.9=107360 max=107360 us drift=7613 us minflt=1002 nivcsw=2
  hist <50:0 50-100:1 100-500:57 0.5-1ms:67 1-5ms:240 >=5ms:135
prio=80 cpu=3 lock=0 n=500 per=10000us p50=3845 p99=43991 p99.9=64075 max=64075 us drift=1606 us minflt=1002 nivcsw=0
  hist <50:0 50-100:0 100-500:27 0.5-1ms:29 1-5ms:255 >=5ms:189
prio=80 cpu=3 lock=1 n=500 per=10000us p50=3097 p99=17229 p99.9=19856 max=19856 us drift=10024 us minflt=1 nivcsw=0
  hist <50:0 50-100:0 100-500:27 0.5-1ms:39 1-5ms:291 >=5ms:143
-- /proc/interrupts after the runs:
           CPU0       CPU1       CPU2       CPU3       
 11:      15169      12747      13074        654 GIC-0  27 Level     arch_timer
 18:       1568          0          0          0 GICv2m-PCI-MSIX-0000:00:02.0   1 Edge      virtio1-req.0
 19:          0       1468          0          0 GICv2m-PCI-MSIX-0000:00:02.0   2 Edge      virtio1-req.1
 20:          0          0        602          0 GICv2m-PCI-MSIX-0000:00:02.0   3 Edge      virtio1-req.2
 21:          0          0          0          0 GICv2m-PCI-MSIX-0000:00:02.0   4 Edge      virtio1-req.3
 23:       1896          0          0          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
IPI0:       675        721        655        869       Rescheduling interrupts
IPI1:       329       2284        940         15       Function call interrupts
[   31.941386][  T181] reboot: Power down
```

출력에서 볼 것:

- **인터럽트 숫자가 격리를 증명한다.** CPU3 열: `arch_timer` 9780 → 654, `virtio1-req.3` 1020 → 0, `Function call interrupts`(IPI) 1258 → 15. 디스크 I/O를 하는 `dd`가 CPU3에 오지 않으니(domain 격리) 큐 3 IRQ가 사라졌고, tick도 대부분 멈췄다. `Rescheduling interrupts`는 여전히 869 — 다른 CPU가 CPU3의 태스크를 깨우는 IPI(우리 루프가 hrtimer로 깨지만 셸·부모와의 상호작용 등)라 0이 되지 않는다.
- **지터 숫자는 방향만 본다.** 가장 좋은 설정(FIFO+CPU3+mlock)의 p99는 20.6 → 17.2 ms, max 40.2 → 19.9 ms로 줄었다. 하지만 10 ms 주기에 p50이 1.6~3.1 ms라는 것 자체가 TCG의 한계다(§1.2) — 실제 보드라면 이 칸은 수~수십 µs다. 같은 실험 안에서도 SCHED_OTHER(p99 197~319 ms) → FIFO(79~83 ms) → 고정·잠금(17~21 ms) 순서는 일관되다.
- 격리 부팅의 첫 줄(SCHED_OTHER, 고정 없음)은 CPU3을 쓸 수 없어 0~2에서 hog들과 경쟁했다. **isolcpus=domain이면 "고정하지 않은 태스크는 격리 CPU에 절대 안 간다"** — 반대로 말하면 RT 루프는 반드시 명시적으로 고정해야 그 CPU를 쓴다.

---

## 7. cgroup v2 cpuset 파티션 — 런타임에 바꿀 수 있는 격리

### 7.1 정의

cgroup v2의 cpuset 컨트롤러에서 `cpuset.cpus.partition`을 `root` 또는 `isolated`로 쓰면 그 cgroup의 CPU가 부모에게서 **독점적으로** 떨어져 나간다(부모의 `cpuset.cpus.effective`에서 빠진다).

- `root`: 별도 scheduling domain을 가진 파티션 — 그 안에서는 load balancing을 한다.
- `isolated`: load balancing 없는 파티션 — `isolcpus=domain`의 런타임 판. 6.x에서는 unbound workqueue 마스크에서도 빠진다(`kernel/cgroup/cpuset.c`의 `workqueue_unbound_exclude_cpumask()`).
- 루트 cgroup의 `cpuset.cpus.isolated`(읽기 전용)가 현재 격리된 CPU 전체를 보여 준다.

tick 정지(`nohz_full`)와 RCU offload는 cpuset으로 못 한다 — 그건 부트 파라미터다. 그래서 실무 조합은 "**`nohz_full=3 rcu_nocbs=3 irqaffinity=0-2`는 부팅 때, domain 격리는 cpuset 파티션**"이다. 커널 문서가 isolcpus 대신 cpuset을 권하는 이유는 되돌릴 수 있고, 컨테이너·systemd slice와 같은 인터페이스라서다(systemd는 `AllowedCPUs=`가 이 파일을 쓴다).

### 7.2 예제 8 — isolated 파티션 만들고, 그 안에서 루프와 DEADLINE 돌리기

무엇을 확인하나: Image-rt를 파라미터 없이 부팅하고 cgroup2를 마운트해 `rt` cgroup을 CPU3 `isolated` 파티션으로 만든다. hog 4개가 어디로 가는지, 그 cgroup에 들어간 셸의 affinity, 그 안에서의 `n2_rt`와 DEADLINE admission, 다시 `member`로 되돌렸을 때를 본다.

```sh
mount -t cgroup2 none /sys/fs/cgroup
cd /sys/fs/cgroup; echo +cpuset > cgroup.subtree_control; mkdir rt
echo 3 > rt/cpuset.cpus; echo isolated > rt/cpuset.cpus.partition
echo "-- rt/cpuset.cpus.partition = $(cat rt/cpuset.cpus.partition)"
echo "-- root cpuset.cpus.effective = $(cat cpuset.cpus.effective)  rt effective = $(cat rt/cpuset.cpus.effective)"
echo "-- root cpuset.cpus.isolated = $(cat cpuset.cpus.isolated 2>/dev/null)"
echo "-- workqueue cpumask = $(cat /sys/devices/virtual/workqueue/cpumask)"
echo "-- /sys/devices/system/cpu/isolated = '$(cat /sys/devices/system/cpu/isolated)'"
for i in 1 2 3 4; do (while :; do :; done) & done; sleep 1
echo "-- where did 4 unpinned hogs land? (field 39 = last CPU)"
for s in /proc/[0-9]*/stat; do awk '$3=="R" {print $2, "on cpu", $39}' $s; done 2>/dev/null
echo "-- move a shell into rt, then run the loop there (no taskset needed: cpuset gives CPU3)"
sh -c 'echo $$ > /sys/fs/cgroup/rt/cgroup.procs; grep Cpus_allowed_list /proc/self/status; /n2_rt 80 -1 1 '$N' '$P
echo "-- DL inside the isolated partition (its own root domain = CPU3 only)"
sh -c 'echo $$ > /sys/fs/cgroup/rt/cgroup.procs; /n2_dl hold 500000 1000000 0; /n2_dl hold 900000 1000000 0; /n2_dl hold 960000 1000000 0'
echo "-- partition back to member"; echo member > /sys/fs/cgroup/rt/cpuset.cpus.partition
echo "   partition=$(cat /sys/fs/cgroup/rt/cpuset.cpus.partition) root isolated='$(cat /sys/fs/cgroup/cpuset.cpus.isolated)' wq cpumask=$(cat /sys/devices/virtual/workqueue/cpumask)"
```

```text
lab initramfs: 6.18.55-rt up 6.43s
== kernel 6.18.55-rt  cmdline: console=ttyAMA0 quiet P=10000 
   /sys/devices/system/cpu/isolated = ''
   /sys/devices/system/cpu/nohz_full = '(null)'
-- rt/cpuset.cpus.partition = isolated
-- root cpuset.cpus.effective = 0-2  rt effective = 3
-- root cpuset.cpus.isolated = 3
-- workqueue cpumask = 7
-- /sys/devices/system/cpu/isolated = ''
-- where did 4 unpinned hogs land? (field 39 = last CPU)
(n2_run.sh) on cpu 1
(n2_run.sh) on cpu 2
(n2_run.sh) on cpu 0
(n2_run.sh) on cpu 2
-- move a shell into rt, then run the loop there (no taskset needed: cpuset gives CPU3)
Cpus_allowed_list:	3
prio=80 cpu=-1 lock=1 n=500 per=10000us p50=358 p99=22617 p99.9=52470 max=52470 us drift=2658 us minflt=1 nivcsw=0
  hist <50:0 50-100:0 100-500:340 0.5-1ms:34 1-5ms:99 >=5ms:27
-- DL inside the isolated partition (its own root domain = CPU3 only)
pid 249 DL 500000/1000000 us: admitted
pid 250 DL 900000/1000000 us: admitted
pid 248 DL 960000/1000000 us: Device or resource busy
-- partition back to member
   partition=member root isolated='' wq cpumask=f
[   35.496363][  T257] reboot: Power down
```

출력에서 볼 것:

- 루트 `cpuset.cpus.effective`가 `0-2`로 줄고, `cpuset.cpus.isolated = 3`, workqueue 마스크 `7`. **`/sys/devices/system/cpu/isolated`는 여전히 빈 값** — 그 파일은 부트 파라미터 `isolcpus=`만 보여 준다. cpuset 격리를 확인할 때는 `cpuset.cpus.isolated`를 본다(면접에서 "isolated 파일이 비어 있는데 격리 안 된 거 아니냐"는 함정 질문이 가능하다).
- 고정하지 않은 hog 4개가 전부 CPU0~2에 있다.
- `rt` cgroup에 들어간 셸의 `Cpus_allowed_list`가 `3` — `taskset` 없이도 CPU3에서만 돈다. 루프 `drift`는 2.7 ms, minflt=1.
- **DEADLINE이 CPU3 하나에서 승인된다**: 파티션이 별도 root domain이라 §3.2의 "affinity = root domain 전체" 규칙이 자동으로 만족된다. 한계는 1 × 0.95 — 0.5와 0.9는 승인, 0.96은 `EBUSY`. (각 `hold`는 sleep 0이라 바로 끝나므로 예약이 쌓이지 않고 하나씩 검사된다. 마지막 명령은 BusyBox `sh -c`가 `exec`로 실행해서 PID가 셸과 같은 248이다.)
- `member`로 되돌리자 `cpuset.cpus.isolated`가 비고 workqueue 마스크가 `f`로 돌아왔다. **재부팅 없이** 격리를 켜고 끌 수 있다 — isolcpus로는 불가능하다.

---

## 8. IRQ affinity — 인터럽트를 어느 CPU가 받나

### 8.1 인터페이스

```sh
cat /proc/interrupts                       # IRQ별·CPU별 카운트
cat /proc/irq/23/smp_affinity_list         # 허용 CPU (예: 0-3)
cat /proc/irq/23/effective_affinity_list   # 실제로 라우팅된 CPU (GIC는 하나를 고른다)
echo 0-2 > /proc/irq/23/smp_affinity_list  # 바꾸기 (root)
cat /proc/irq/default_smp_affinity         # 새 IRQ 기본값 (irqaffinity=로 설정, 16진 마스크)
```

GICv2/v3의 SPI는 한 번에 CPU 하나로 라우팅되므로 `smp_affinity_list`가 `0-3`이어도 effective는 하나다(H6). PREEMPT_RT에서는 IRQ의 하드 부분(GIC ack + 스레드 깨우기)이 그 CPU에서 일어나고, `irq/N-name` 스레드도 기본적으로 IRQ affinity를 따라간다.

### 8.2 예제 9 — managed IRQ는 옮길 수 없다

무엇을 확인하나: `Image-sec` 게스트(`-smp 4`, virtio-blk 1개 + 기본 virtio-net)에서 모든 virtio IRQ를 CPU3으로 옮겨 본다.

```sh
for n in $(grep -E 'virtio' /proc/interrupts | cut -d: -f1); do
	echo "try irq $n -> 3"; echo 3 > /proc/irq/$n/smp_affinity_list; echo " now $(cat /proc/irq/$n/smp_affinity_list)"
done
```

```text
try irq 17 -> 3
 now 3
try irq 18 -> 3
sh: write error: Operation not permitted
 now 0
try irq 19 -> 3
sh: write error: Operation not permitted
 now 1
try irq 20 -> 3
sh: write error: Operation not permitted
 now 2
try irq 21 -> 3
sh: write error: Operation not permitted
 now 3
try irq 22 -> 3
 now 3
try irq 23 -> 3
 now 3
try irq 24 -> 3
 now 3
```

출력에서 볼 것: 17(virtio1-config)과 22~24(virtio-net config/input/output)는 옮겨졌고, **18~21(virtio-blk의 큐 IRQ `virtio1-req.0..3`)은 `EPERM`**. 이것이 managed IRQ다. 드라이버가 `pci_alloc_irq_vectors_affinity(..., PCI_IRQ_AFFINITY, ...)`로 "큐마다 CPU 하나씩" 커널에 맡긴 IRQ는 커널이 affinity를 관리하고 유저가 못 바꾼다. NVMe, 멀티큐 NIC, virtio-blk/net(멀티큐) 모두 해당한다. 대처: `isolcpus=managed_irq`(효과 조건은 §6.3), 장치 큐 수를 줄이기(`ethtool -L`, 드라이버 파라미터), RT CPU에서 I/O를 하지 않기.

### 8.3 예제 10 — 네트워크 IRQ를 일부러 CPU3에 보내면

무엇을 확인하나: 격리 부팅 상태에서 같은 부하를 걸고, virtio-net IRQ를 (1) CPU0, (2) CPU3으로 옮긴 뒤 각각 최선 설정(FIFO 80 + CPU3 + mlock) 루프를 돌린다. 각 블록의 첫 `virtio0-input` 줄이 실행 후, 둘째 줄이 실행 전 카운트다.

```sh
for c in 0 3; do
	for n in $(grep virtio0 /proc/interrupts | cut -d: -f1); do echo $c > /proc/irq/$n/smp_affinity_list; done
	echo "-- virtio-net IRQs -> CPU$c"; grep virtio0-input /proc/interrupts > /tmp/a
	/n2_rt 80 3 1 $N $P; grep virtio0-input /proc/interrupts; cat /tmp/a
done
```

```text
lab initramfs: 6.18.55-rt up 6.90s
== kernel 6.18.55-rt  cmdline: console=ttyAMA0 quiet HOGS=2 P=10000 isolcpus=domain,managed_irq,3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2 
   /sys/devices/system/cpu/isolated = '3'
   /sys/devices/system/cpu/nohz_full = '3'
-- virtio-net IRQs -> CPU0
prio=80 cpu=3 lock=1 n=500 per=10000us p50=3437 p99=19653 p99.9=35169 max=35169 us drift=7669 us minflt=1 nivcsw=0
  hist <50:0 50-100:0 100-500:37 0.5-1ms:42 1-5ms:245 >=5ms:176
 23:        576          0          0          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
 23:        118          0          0          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
-- virtio-net IRQs -> CPU3
prio=80 cpu=3 lock=1 n=500 per=10000us p50=3517 p99=23368 p99.9=46960 max=46960 us drift=1832 us minflt=1 nivcsw=0
  hist <50:0 50-100:0 100-500:35 0.5-1ms:43 1-5ms:256 >=5ms:166
 23:        625          0          0        482 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
 23:        625          0          0         15 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
[   22.890132][  T176] reboot: Power down
```

출력에서 볼 것: CPU3으로 보내자 루프가 도는 5초 동안 CPU3이 RX IRQ를 467번(482 − 15) 받았고, p99가 19.7 → 23.4 ms, max 35.2 → 47.0 ms로 나빠졌다. TCG 잡음 속이라 차이는 작지만 방향은 예상대로다. 실제 보드에서는 IRQ 하나의 하드 부분(수 µs) + 같은 CPU에서 깨어나는 `irq/23-...` 스레드(FIFO 50이라 루프보다 낮아 루프를 선점하지는 않지만 캐시를 더럽힌다)가 지터로 나타난다. **우선순위가 낮아도 같은 CPU의 IRQ는 해롭다** — 하드 IRQ 부분은 우선순위와 무관하게 루프를 끊는다.

### 8.4 irqbalance

`irqbalance` 데몬은 주기적으로 IRQ 부하를 보고 `/proc/irq/*/smp_affinity`를 다시 쓴다. 수동으로 맞춘 affinity가 몇 분 뒤 바뀌는 흔한 원인이다. RT 시스템에서는 (1) 끄거나(`systemctl disable --now irqbalance`), (2) 설정 파일(`/etc/default/irqbalance` 등)의 `IRQBALANCE_BANNED_CPULIST`로 RT CPU를 제외한다. 최근 irqbalance는 `isolcpus`·`nohz_full` CPU를 스스로 피한다고 알려져 있지만, 버전마다 다르니 `/proc/interrupts`로 확인하는 것이 원칙이다. (이 랩의 게스트에는 irqbalance가 없어 실행해 보지 않았다.)

### 8.5 tuned의 realtime 프로파일

RHEL·Fedora 계열의 `tuned`는 `realtime` 프로파일(`tuned-profiles-realtime`)로 위의 일들 — `isolated_cores=` 변수 하나로 isolcpus/nohz_full/rcu_nocbs 부트 파라미터, IRQ affinity, 워크큐 마스크, governor, 일부 sysctl — 을 묶어 적용한다. 개념적으로는 이 노트의 체크리스트를 자동화한 것이다. Yocto·Ubuntu 기반 임베디드 이미지에는 보통 없어서, 같은 내용을 커널 cmdline + systemd 유닛으로 직접 넣는다(가이드, 실행 안 함).
---

## 9. 메모리 — RT 경로에서 page fault를 0으로

### 9.1 무엇이 fault를 내나

Linux는 메모리를 **게으르게** 준다(A5). `malloc`이나 큰 static 배열은 가상 주소만 잡고, 처음 건드릴 때 page fault로 물리 페이지를 붙인다. 코드 페이지도 처음 실행할 때 fault로 읽어 온다. 메모리가 부족하면 커널은 깨끗한 페이지(실행 파일의 코드!)를 버렸다가 다시 읽는다 — 이것이 **major fault**이고 eMMC에서 수 ms다.

| 원인 | 언제 | 대책 |
|---|---|---|
| 처음 쓰는 heap·bss·스택 페이지 | 첫 접근 | `mlockall(MCL_CURRENT)` (기존 매핑을 채움) + 스택 prefault |
| 나중에 생기는 매핑 (malloc, 새 스레드 스택, mmap) | 할당 시점 | `MCL_FUTURE` (할당 시 바로 채워지고 잠김) — 단 할당 자체가 루프 밖이어야 |
| 스택 성장 | 깊은 호출 | 큰 지역 배열을 한 번 건드려 미리 늘려 둔다 (`prefault_stack()`) |
| 코드·라이브러리 페이지 회수 | 메모리 압박 | `mlockall`이 코드 페이지도 잠근다 |
| glibc가 메모리를 OS에 반환 후 다시 받음 | free/malloc 반복 | `mallopt(M_TRIM_THRESHOLD, -1)`, `mallopt(M_MMAP_MAX, 0)`, 또는 루프 전 사전 할당 + 자체 풀 |
| THP 병합·compaction | khugepaged, 할당 시 | `transparent_hugepage=madvise` 또는 `never`, 필요한 버퍼만 `madvise(MADV_HUGEPAGE)` |

### 9.2 mlockall의 세부

- 플래그: `MCL_CURRENT`(지금 매핑된 것 전부), `MCL_FUTURE`(앞으로 생길 매핑), `MCL_ONFAULT`(4.4+: 잠그되 채우지는 않음 — RT에는 보통 안 맞는다).
- 한도: 일반 사용자는 `RLIMIT_MEMLOCK`(`ulimit -l`)까지. Lima VM의 기본은 `8192`(KiB)였다. `n2_rt`(약 6 MiB)는 일반 사용자로도 잠겼지만 큰 앱은 실패한다. systemd 서비스는 `LimitMEMLOCK=infinity`, root는 `CAP_IPC_LOCK`.
- `mlockall` 뒤에 생기는 스레드: glibc 기본 스레드 스택은 8 MiB(`ulimit -s`)라 `MCL_FUTURE`면 스레드 하나마다 8 MiB가 실제로 잠긴다. RT 앱은 `pthread_attr_setstacksize()`로 스택을 작게 잡는다.
- 측정 결과(§5.3): 잠금 없이 루프 안 `minflt=2054`, 잠금 후 `minflt=1`. 남은 1은 루프 끝의 `getrusage` 주변 같은 사소한 것이다.

### 9.3 hugepages — 언제 도움이 되나

큰 맵·텐서 버퍼를 4 KiB 페이지로 쓰면 TLB miss가 많다. 미리 예약한 hugetlbfs 페이지(`vm.nr_hugepages`, `MAP_HUGETLB`)는 부팅 때 확보되고 THP처럼 백그라운드 병합(khugepaged) 지연이 없어서 RT에 더 예측 가능하다. "TLB miss를 줄인다"와 "khugepaged·compaction 잡음을 없앤다"는 다른 이야기다 — 후자 때문에 RT 시스템에서는 THP를 `madvise`로 두는 경우가 많다.

---

## 10. 전원·주파수 — idle 깊이와 클럭이 만드는 지터

### 10.1 직관

CPU가 할 일이 없으면 cpuidle이 깊은 idle state(코어 power gating, 클러스터 off)로 보낸다. 깊을수록 전력은 줄고 **깨어나는 데 걸리는 시간(exit latency)** 은 길어진다 — 수십 µs~ms. 1 kHz 루프는 매 주기 sleep → idle → 타이머로 깨어남을 반복하므로, 깊은 idle의 exit latency가 매 주기 지터에 더해진다. 주파수도 마찬가지다. schedutil·ondemand governor가 부하에 따라 클럭을 내렸다 올리면 같은 코드의 실행 시간이 바뀐다.

### 10.2 PM QoS — "이 이상 늦게 깨는 idle state는 쓰지 마"

`/dev/cpu_dma_latency`는 CPU 지연 PM QoS 요청 인터페이스다. s32 값(µs)을 써 넣고 **fd를 열어 둔 동안만** 요청이 유지된다. 여러 요청 중 가장 작은 값이 적용되고, cpuidle governor(menu·teo)는 exit latency가 그 값보다 큰 state를 고르지 않는다. 0을 쓰면 사실상 가장 얕은 state(arm64에서는 WFI)만 쓴다. CPU별로는 `/sys/devices/system/cpu/cpuN/power/pm_qos_resume_latency_us`가 있다.

### 10.3 예제 11 — PM QoS 요청 걸고 읽기 `n2_qos`

무엇을 확인하나: 현재 합산값을 읽고, 요청을 쓰고, 다시 읽는다. fd를 닫으면 요청이 사라진다.

```c
/* n2_qos: hold a PM QoS CPU latency request through /dev/cpu_dma_latency for <s> seconds. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv) {
	int32_t us = argc > 1 ? atoi(argv[1]) : 0; int secs = argc > 2 ? atoi(argv[2]) : 5, cur;
	int fd = open("/dev/cpu_dma_latency", O_RDWR);
	if (fd < 0) { perror("open /dev/cpu_dma_latency"); return 1; }
	if (read(fd, &cur, sizeof cur) != sizeof cur) { perror("read"); return 1; }
	printf("before: aggregated cpu latency limit = %d us\n", cur);
	if (write(fd, &us, sizeof us) != sizeof us) { perror("write"); return 1; }
	if (pread(fd, &cur, sizeof cur, 0) != sizeof cur) { perror("pread"); return 1; }
	printf("holding request %d us for %d s (fd open) -> aggregated now %d us\n", us, secs, cur);
	fflush(stdout); sleep((unsigned)secs);
	close(fd);                                  /* closing the fd drops the request */
	return 0;
}
```

게스트(`6.18.55-rt`)에서 `/n2_qos 0 3`, 끝난 뒤 `/n2_qos 20 1` (`n2_qos.sh`의 해당 줄만):

```text
-- cpuidle: driver=none governor=menu
-- cpufreq: ls: /sys/devices/system/cpu/cpu0/cpufreq: No such file or directory 
before: aggregated cpu latency limit = 2000000000 us
holding request 0 us for 3 s (fd open) -> aggregated now 0 us
before: aggregated cpu latency limit = 2000000000 us
holding request 20 us for 1 s (fd open) -> aggregated now 20 us
```

출력에서 볼 것:

- 기본값 `2000000000` µs(= `PM_QOS_CPU_LATENCY_DEFAULT_VALUE`, 사실상 "제한 없음"). 요청 0을 쓰자 합산값이 0. 첫 프로세스가 끝나 fd가 닫힌 뒤 두 번째 실행의 "before"가 다시 기본값 — **요청은 fd 수명에 묶여 있다.** 그래서 이 파일을 쓰는 데몬은 계속 살아 있어야 한다(`cyclictest`도 실행 중 이 파일을 열어 둔다 — N3).
- QEMU virt 게스트는 cpuidle state가 없고(`driver=none`) cpufreq도 없다. 그래서 효과를 볼 수는 없다. **실제 보드에서는** `/sys/devices/system/cpu/cpu0/cpuidle/state*/{name,latency,usage}`로 각 state의 exit latency를 보고, 요청을 걸기 전후 깊은 state의 `usage`가 더 이상 늘지 않는 것을 확인한다.

### 10.4 주파수와 그 밖의 하드웨어 자원

- **cpufreq**: RT CPU는 `performance` governor로 최고 클럭 고정(또는 `scaling_min_freq = scaling_max_freq`)이 기본 처방이다. 열·전력 예산과의 타협은 N5.
- **thermal throttling**: 고정 클럭이어도 온도가 오르면 펌웨어·커널이 클럭을 내린다. 로봇 컴퓨터의 RT 검증은 최악 온도에서 해야 한다(N5).
- **캐시·메모리 대역폭 분할**: 다른 코어가 큰 메모리 복사를 하면 공유 L2/L3와 DRAM 대역폭 경쟁으로 RT 코어의 실행 시간이 늘어난다. CPU 격리로는 못 막는다. Intel은 RDT(CAT/MBA)를 `resctrl` 파일시스템으로, Arm은 **MPAM**(Memory System Resource Partitioning and Monitoring)으로 캐시 way·대역폭을 파티션별로 나눈다. MPAM의 Linux `resctrl` 지원은 최근 몇 년에 걸쳐 upstream에 들어오는 중이고 SoC가 MPAM을 구현해야 쓸 수 있다 — 이 랩에서는 확인하지 못했다(개념만). 대안: 큰 메모리 작업을 RT 루프와 다른 클러스터에 두기, 캐시 lock 기능이 있는 SoC의 TCM/SRAM 쓰기.

---

## 11. 결과 종합 — 하나씩 켜면서 무엇이 바뀌었나

### 11.1 1 kHz, Lima VM (하드웨어 가상화, `7.0.0-38-generic`, 배경 load 23)

| 설정 | p50 µs | p99 µs | p99.9 µs | max µs | 루프 중 minflt |
|---|---|---|---|---|---|
| SCHED_OTHER, 고정 없음 | 67 | 21426 | 35586 | 37482 | 2054 |
| SCHED_OTHER + CPU5 (같은 CPU에 hog) | 69 | 903954 | 935984 | 938941 | 2054 |
| (a) SCHED_FIFO 80 | 15 | 3134 | 16481 | 20432 | 2054 |
| (b) FIFO 80 + CPU5 고정 | 13 | 2521 | 11467 | 15164 | 2054 |
| (e) FIFO 80 + CPU5 + mlockall/prefault | 15 | 2370 | 20688 | 24265 | 1 |
| 나쁜 루프 (usleep·malloc·fprintf), OTHER | 주기 평균 1111 | — | — | 주기 max 21854 | 2 |
| 나쁜 루프, FIFO 80 + CPU5 | 주기 평균 1041 | — | — | 주기 max 20632 | 2 |

```svg
<svg viewBox="0 0 660 320" xmlns="http://www.w3.org/2000/svg">
  <text x="70" y="22" font-size="13">Lima VM 1 kHz 루프 (ms, 로그 축) — 배경 load average 23</text>
  <line x1="70" y1="250.0" x2="640" y2="250.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="254.0" text-anchor="end" font-size="12">0.01</text>
  <line x1="70" y1="208.0" x2="640" y2="208.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="212.0" text-anchor="end" font-size="12">0.1</text>
  <line x1="70" y1="166.0" x2="640" y2="166.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="170.0" text-anchor="end" font-size="12">1</text>
  <line x1="70" y1="124.0" x2="640" y2="124.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="128.0" text-anchor="end" font-size="12">10</text>
  <line x1="70" y1="82.0" x2="640" y2="82.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="86.0" text-anchor="end" font-size="12">100</text>
  <line x1="70" y1="40.0" x2="640" y2="40.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="44.0" text-anchor="end" font-size="12">1000</text>
  <line x1="70" y1="40" x2="70" y2="250" stroke="currentColor"/>
  <line x1="70" y1="250" x2="640" y2="250" stroke="currentColor"/>
  <rect x="80" y="215.3" width="28" height="34.7" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="111" y="110.1" width="28" height="139.9" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <rect x="142" y="99.9" width="28" height="150.1" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="125" y="268" text-anchor="middle" font-size="12">OTHER</text>
  <rect x="192" y="214.8" width="28" height="35.2" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="223" y="41.8" width="28" height="208.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <rect x="254" y="41.1" width="28" height="208.9" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="237" y="268" text-anchor="middle" font-size="12">OTHER+CPU5</text>
  <rect x="304" y="242.6" width="28" height="7.4" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="335" y="145.2" width="28" height="104.8" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <rect x="366" y="111.0" width="28" height="139.0" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="349" y="268" text-anchor="middle" font-size="12">FIFO 80</text>
  <rect x="416" y="245.2" width="28" height="4.8" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="447" y="149.1" width="28" height="100.9" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <rect x="478" y="116.4" width="28" height="133.6" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="461" y="268" text-anchor="middle" font-size="12">FIFO+CPU5</text>
  <rect x="528" y="242.6" width="28" height="7.4" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="559" y="150.3" width="28" height="99.7" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <rect x="590" y="107.8" width="28" height="142.2" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="573" y="268" text-anchor="middle" font-size="12">FIFO+CPU5+mlock</text>
  <rect x="80" y="284" width="12" height="12" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="98" y="294" font-size="12">p50</text>
  <rect x="170" y="284" width="12" height="12" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="188" y="294" font-size="12">p99</text>
  <rect x="260" y="284" width="12" height="12" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="278" y="294" font-size="12">max</text>
  <text x="350" y="294" font-size="12">p50: OTHER 0.067 → FIFO 0.013~0.015</text>
</svg>
```

그림 7 — Lima VM의 1 kHz 결과(로그 축). FIFO가 p50을 timer slack만큼(약 50 µs) 줄이고 p99를 한 자릿수 ms로 끌어내린다. max는 Mac 호스트가 vCPU를 재우는 시간이라 게스트 설정과 무관하게 15~25 ms에 머문다.

### 11.2 100 Hz, QEMU TCG `-smp 4` (같은 부하: hog 2개 + 디스크 + ping)

| 커널 / 부팅 | OTHER p99 ms | FIFO 80 p99 | FIFO + CPU3 p99 | FIFO + CPU3 + mlock p99 / max | CPU3 arch_timer (실험 전체) |
|---|---|---|---|---|---|
| 6.18.55-trace (CONFIG_PREEMPT, HZ=250) | 287.6 | 58.5 | 110.2 | 32.8 / 50.0 | 5844 |
| 6.18.55-rt | 319.1 | 79.1 | 39.3 | 20.6 / 40.2 | 9780 |
| (c)+(d) 6.18.55-rt + isolcpus·nohz_full·rcu_nocbs·irqaffinity | 197.2 | 82.8 | 44.0 | 17.2 / 19.9 | 654 |
| (d') 위 + virtio-net IRQ를 CPU3으로 강제 | — | — | — | 23.4 / 47.0 | (RX IRQ 467개 추가) |
| 6.18.55-rt + cpuset isolated 파티션 (부트 파라미터 없음) | — | — | — | 22.6 / 52.5 | — |

```svg
<svg viewBox="0 0 660 320" xmlns="http://www.w3.org/2000/svg">
  <text x="70" y="22" font-size="13">TCG 게스트 100 Hz 루프의 p99 지연 (ms, 로그 축) — HOGS=2 + 디스크·ping 부하</text>
  <line x1="70" y1="250.0" x2="640" y2="250.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="254.0" text-anchor="end" font-size="12">10</text>
  <line x1="70" y1="199.9" x2="640" y2="199.9" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="203.9" text-anchor="end" font-size="12">30</text>
  <line x1="70" y1="145.0" x2="640" y2="145.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="149.0" text-anchor="end" font-size="12">100</text>
  <line x1="70" y1="94.9" x2="640" y2="94.9" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="98.9" text-anchor="end" font-size="12">300</text>
  <line x1="70" y1="40.0" x2="640" y2="40.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="44.0" text-anchor="end" font-size="12">1000</text>
  <line x1="70" y1="40" x2="70" y2="250" stroke="currentColor"/>
  <line x1="70" y1="250" x2="640" y2="250" stroke="currentColor"/>
  <rect x="90" y="96.8" width="34" height="153.2" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="107.0" y="92.8" text-anchor="middle" font-size="12">288</text>
  <rect x="128" y="92.1" width="34" height="157.9" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="145.0" y="88.1" text-anchor="middle" font-size="12">319</text>
  <rect x="166" y="114.0" width="34" height="136.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="183.0" y="110.0" text-anchor="middle" font-size="12">197</text>
  <text x="145" y="268" text-anchor="middle" font-size="12">OTHER</text>
  <rect x="230" y="169.4" width="34" height="80.6" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="247.0" y="165.4" text-anchor="middle" font-size="12">58</text>
  <rect x="268" y="155.7" width="34" height="94.3" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="285.0" y="151.7" text-anchor="middle" font-size="12">79</text>
  <rect x="306" y="153.6" width="34" height="96.4" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="323.0" y="149.6" text-anchor="middle" font-size="12">83</text>
  <text x="285" y="268" text-anchor="middle" font-size="12">FIFO 80</text>
  <rect x="370" y="140.6" width="34" height="109.4" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="387.0" y="136.6" text-anchor="middle" font-size="12">110</text>
  <rect x="408" y="187.6" width="34" height="62.4" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="425.0" y="183.6" text-anchor="middle" font-size="12">39</text>
  <rect x="446" y="182.4" width="34" height="67.6" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="463.0" y="178.4" text-anchor="middle" font-size="12">44</text>
  <text x="425" y="268" text-anchor="middle" font-size="12">FIFO+CPU3</text>
  <rect x="510" y="195.8" width="34" height="54.2" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="527.0" y="191.8" text-anchor="middle" font-size="12">33</text>
  <rect x="548" y="217.0" width="34" height="33.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="565.0" y="213.0" text-anchor="middle" font-size="12">21</text>
  <rect x="586" y="225.3" width="34" height="24.7" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="603.0" y="221.3" text-anchor="middle" font-size="12">17</text>
  <text x="565" y="268" text-anchor="middle" font-size="12">FIFO+CPU3+mlock</text>
  <rect x="80" y="284" width="12" height="12" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="98" y="294" font-size="12">6.18-trace (비RT)</text>
  <rect x="260" y="284" width="12" height="12" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="278" y="294" font-size="12">6.18-rt</text>
  <rect x="440" y="284" width="12" height="12" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="458" y="294" font-size="12">6.18-rt + 격리</text>
</svg>
```

그림 8 — TCG 게스트의 p99(로그 축). 설정 순서(OTHER → FIFO → 고정·잠금 → 격리)에 따라 꼬리가 줄어드는 방향은 세 커널에서 일관되다. 절대값은 TCG와 호스트 부하 때문에 실제 보드의 100~1000배다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13">격리가 먹혔다는 증거 — Image-rt, CPU3 카운트 (격리 없음 vs 격리)</text>
  <text x="20" y="50" font-size="12">CPU3 tick, busy 태스크 하나 5 s</text>
  <rect x="20" y="56" width="400.0" height="16" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="426.0" y="69" font-size="12">2548 (격리 없음)</text>
  <rect x="20" y="76" width="1.0" height="16" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="27.0" y="89" font-size="12">0 (격리)</text>
  <text x="20" y="110" font-size="12">CPU3 virtio-blk IRQ (실험 전체)</text>
  <rect x="20" y="116" width="400.0" height="16" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="426.0" y="129" font-size="12">1020 (격리 없음)</text>
  <rect x="20" y="136" width="1.0" height="16" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="27.0" y="149" font-size="12">0 (격리)</text>
  <text x="20" y="170" font-size="12">CPU3 Function call IPI</text>
  <rect x="20" y="176" width="400.0" height="16" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="426.0" y="189" font-size="12">1258 (격리 없음)</text>
  <rect x="20" y="196" width="4.8" height="16" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="30.8" y="209" font-size="12">15 (격리)</text>
  <text x="20" y="230" font-size="12">CPU3 arch_timer (실험 전체 누적)</text>
  <rect x="20" y="236" width="400.0" height="16" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="426.0" y="249" font-size="12">9780 (격리 없음)</text>
  <rect x="20" y="256" width="26.7" height="16" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="52.7" y="269" font-size="12">654 (격리)</text>
  <text x="20" y="290" font-size="12">격리 = isolcpus=domain,managed_irq,3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2</text>
</svg>
```

그림 9 — 격리의 구조적 증거(Image-rt, CPU3). 지터 숫자는 잡음이 커도, 이 카운트들은 격리가 실제로 CPU3에서 tick·디스크 IRQ·IPI를 걷어냈다는 것을 분명히 보여 준다.

### 11.3 읽는 법

- **(a) SCHED_FIFO가 가장 큰 한 걸음이다.** 두 환경 모두 OTHER → FIFO에서 p99가 한 자릿수 배 이상 줄었다. 부하가 fair 태스크라면 정책 하나로 대부분 해결된다.
- **(b) 고정은 RT 정책과 함께일 때만 이득.** OTHER + 고정은 최악(Lima p99 0.9 s). FIFO + 고정은 비RT 커널(trace)에서 오히려 p99가 늘었다(58.5 → 110.2) — TCG 잡음 범위 안이지만, "고정 = 그 CPU의 잡음을 다 맞는다"는 뜻이라 격리 없이 고정만 하면 좋아진다는 보장은 없다.
- **(e) mlockall은 fault 수를 확실히 0으로 만든다**(2054 → 1). 지터 차이는 이 환경에서는 작다. fault 비용이 큰 실제 보드(느린 DRAM, 메모리 압박, eMMC의 major fault)에서 효과가 커진다 — "측정에서 차이가 작았다"는 "필요 없다"가 아니다.
- **(c) 격리**는 지터 숫자보다 인터럽트 카운트로 확실하게 보인다(그림 9). 실제 보드에서는 이 카운트 감소가 그대로 µs 단위 꼬리 감소로 나타난다.
- **(d) IRQ affinity**: 네트워크 IRQ를 격리 CPU로 되돌리자 p99·max가 다시 나빠졌다.
- **나쁜 루프는 FIFO로 올려도 나쁘다**: 5초에 0.2 s 드리프트(§5.4).
- PREEMPT_RT(6.18.55-rt) vs 비RT(6.18.55-trace)의 차이는 TCG에서 거의 안 보인다. 그 차이는 "커널 안에서 오래 선점 불가능한 구간"이 있을 때 나타나는데(N1), 이 부하(hog·dd·ping)는 그런 구간을 크게 만들지 않는다. 실제 비교는 N3의 `cyclictest` + `stress-ng`/`hackbench` 부하로 한다.


---

## 12. 커널 안에서는 무슨 일이 일어나나

| 유저가 한 일 | 커널 경로 (6.18) | 드라이버를 쓸 때 다시 만나는 곳 |
|---|---|---|
| `sched_setscheduler(FIFO 80)` | `__sched_setscheduler()` → `__setscheduler_params()` (여기서 `timer_slack_ns = 0`) → `p->sched_class = &rt_sched_class`, `p->prio = 99 - 80` | 커널 스레드는 `sched_set_fifo()` (= rtprio 50, `MAX_RT_PRIO / 2`) — threaded IRQ의 `irq_thread()`가 이걸 부른다 |
| `sched_setattr(DEADLINE)` | `sched_dl_overflow()`가 root domain의 `dl_bw`로 admission → 실패 시 `-EBUSY`; `dl_task_check_affinity()`가 affinity 검사 | 드라이버가 직접 쓸 일은 드묾. 단 `sugov`(schedutil 커널 스레드)는 특별 플래그 DEADLINE 태스크다 |
| `clock_nanosleep(ABSTIME)` | `hrtimer_nanosleep()` → `do_nanosleep()` → `schedule()`; 만료는 hrtimer 인터럽트 → `hrtimer_wakeup()` → `try_to_wake_up()` | F8: 드라이버의 `hrtimer`. PREEMPT_RT에서 RT 태스크의 sleep 타이머는 `HRTIMER_MODE_HARD`로 하드 IRQ에서 만료된다(`__hrtimer_setup_sleeper()`), 일반 태스크는 soft 모드 → `ktimers/N` 스레드 |
| `mlockall()` | `apply_mlockall_flags()` → VMA에 `VM_LOCKED` → `mm_populate()` → `populate_vma_page_range()`로 페이지 채움 | 드라이버의 DMA 버퍼는 애초에 커널이 잡은 메모리라 fault가 없다(F9/F10). 유저 버퍼를 DMA하면 `pin_user_pages()` |
| `isolcpus=` / `nohz_full=` | `kernel/sched/isolation.c`의 housekeeping 마스크(`HK_TYPE_DOMAIN`, `HK_TYPE_MANAGED_IRQ`, `HK_TYPE_KERNEL_NOISE` 등) → scheduler domain 구성, 타이머·workqueue·kthread 배치가 `housekeeping_cpumask()`를 참조 | 드라이버의 `queue_work()`(unbound wq는 housekeeping CPU), `irq_set_affinity_hint()`, managed IRQ(`pci_alloc_irq_vectors_affinity()`) |
| cpuset `isolated` 파티션 | `kernel/cgroup/cpuset.c`가 sched domain을 다시 만들고(`rebuild_sched_domains`) 별도 root domain을 생성, unbound workqueue 마스크에서 제외 | — |
| `/proc/irq/N/smp_affinity_list` 쓰기 | `irq_set_affinity()` → irqchip의 `irq_set_affinity` 콜백(GIC는 라우팅 레지스터 갱신). managed IRQ는 거부 | H6: GIC `GICD_ITARGETSR`/`IROUTER`, MSI는 ITS/v2m |
| `/dev/cpu_dma_latency` 쓰기 | `kernel/power/qos.c` `cpu_latency_qos_add_request()` → cpuidle governor가 `cpu_latency_qos_limit()`을 읽음 | N6: 드라이버의 `dev_pm_qos_*`, `cpu_latency_qos_add_request()` (예: 오디오·네트워크 드라이버가 DMA 지연 때문에 건다 — 이름이 "dma_latency"인 이유) |

핵심 연결 두 가지:

1. **threaded IRQ의 우선순위는 드라이버가 아니라 커널이 정한다** — `request_threaded_irq()`로 만든 `irq/N-name` 스레드는 `sched_set_fifo()`로 FIFO 50이 된다. PREEMPT_RT에서는 거의 모든 IRQ 핸들러가 강제로 스레드화된다(`IRQF_NO_THREAD`·`IRQF_TIMER` 등 예외). 그래서 시스템 통합자가 `chrt`로 다시 정한다(§4).
2. **드라이버가 RT 경로를 망치는 방법**: IRQ 핸들러에서 `udelay(500)`, 긴 `spin_lock_irqsave` 구간(RT에서는 raw spinlock만 해당), `preempt_disable()` 구간, 하드 IRQ 문맥의 긴 작업. 이건 우선순위나 격리로 못 고친다 — `osnoise`/`timerlat`(N3)과 `irqsoff`/`preemptoff` tracer(M5)로 찾는다.

---

## 13. Embedded 실무에서 다시 보기

### 13.1 장면 1 — Jetson·i.MX8 로봇 컴퓨터 초기 설정 체크리스트

1. PREEMPT_RT 커널 (`uname -v`에 `PREEMPT_RT`). 벤더 BSP의 RT 패치 버전 확인.
2. 부트 파라미터: `isolcpus=domain,managed_irq,3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2` (또는 cpuset 파티션) — 클러스터가 둘이면 RT 코어를 한 클러스터에 모은다.
3. irqbalance 끄기(`systemctl disable --now irqbalance`) 또는 banned CPU 설정, 센서 IRQ를 RT 코어 **근처**(같은 클러스터의 housekeeping 코어)로.
4. 우선순위 지도(§4)대로 IRQ 스레드 `chrt` — 부팅 때 udev 규칙이나 systemd oneshot 서비스로 (IRQ 번호는 바뀔 수 있으니 이름으로 찾는다).
5. 제어 앱: `mlockall`, prefault, `CPUSchedulingPolicy=fifo`, `CPUAffinity=3`, `LimitMEMLOCK=infinity`, `LimitRTPRIO=99`.
6. PM QoS 데몬 또는 앱이 `/dev/cpu_dma_latency`에 0을 들고 있기, RT 코어 `performance` governor.
7. 검증: 최악 부하(카메라 + 네트워크 + 디스크 + stress-ng) 상태에서 `cyclictest` / `rtla timerlat` 12시간 이상 (N3), 최고 온도에서.

### 13.2 장면 2 — "격리했는데 CPU3에 가끔 50 µs 튐"

`/proc/interrupts`에서 CPU3 열이 늘어나는 줄을 찾는다. 흔한 범인: (a) managed IRQ — NVMe·virtio·네트워크 멀티큐 장치는 CPU마다 큐를 만들고 CPU3 큐의 IRQ를 CPU3에 둔다. `managed_irq` 플래그는 "CPU3 큐에 CPU3 말고 다른 CPU도 있을 때"만 효과가 있다(§8). CPU3에서 I/O를 하지 않으면 그 IRQ는 오지 않는다. (b) `Function call interrupts`(IPI) — 다른 CPU가 TLB flush 등을 요청. RT 프로세스가 `mmap`/`munmap`을 반복하면 같은 프로세스의 다른 스레드가 있는 CPU에 IPI가 간다 — 메모리 레이아웃을 루프 밖에서 고정하는 또 하나의 이유. (c) 남은 1 Hz tick — nohz_full의 잔여 tick.

### 13.3 장면 3 — ROS 2 노드를 RT로

ROS 2의 executor 스레드 하나에 RT 콜백과 일반 콜백을 섞으면 일반 콜백(로그, 파라미터 서비스)이 RT 콜백을 막는다. 처방은 이 노트와 같다: RT 콜백만 가진 executor를 별도 스레드로 만들고 그 스레드에 FIFO + affinity를 준다. DDS 미들웨어의 수신 스레드 우선순위도 지도에 넣는다(벤더 설정 파일에서 지정).

### 13.4 장면 4 — 컨테이너 안의 RT 스레드

L8에서 본 것처럼 cgroup v2에서 RT 태스크를 하위 cgroup에 넣으려면 `CONFIG_RT_GROUP_SCHED`가 있는 커널은 그 cgroup에 `cpu.rt_runtime_us`가 필요하다(대부분의 배포 커널은 이 옵션을 끈다). `cpu.max`는 fair 태스크만 throttle한다. 컨테이너에 RT 코어를 주는 방법은 cpuset(`--cpuset-cpus=3`)이고, 격리는 호스트 쪽 파티션으로 한다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 상대 `usleep(1000)` 루프 | 1 kHz인데 실제 900~960 Hz, 드리프트 | 본문 시간 + 지연이 매 주기 누적 (§5.4: 5 s에 0.56 s) | `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` |
| RT 정책 없이 CPU 고정만 | 고정 전보다 p99가 더 나빠짐 | 같은 CPU의 다른 fair 태스크와 비율로 나눔 (§5.3: p99 0.9 s) | FIFO + 고정 + 격리를 함께 |
| 제어 루프를 FIFO 99로 | 디버그 셸·watchdog 피더가 못 끼어듦, 다른 RT를 넣을 자리 없음 | 우선순위 공간을 계획하지 않음 | 99는 비우고 지도(§4)대로 |
| 센서 IRQ 스레드(50)가 제어(80)보다 낮음 | 제어 루프가 데이터를 기다리며 늦음 | 데이터 생산자가 소비자보다 낮은 우선순위 | 생산자 IRQ 스레드를 `chrt -f -p 85` |
| `mlockall` 없이 RT | 첫 몇 초·메모리 압박 때 spike | 첫 접근 fault, 코드 페이지 회수 | `mlockall(MCL_CURRENT or MCL_FUTURE)` + prefault |
| 루프 안 `printf`/`syslog` | 가끔 수 ms~수십 ms 지연 | write syscall, 콘솔·파일시스템 lock | SPSC 링 + 로거 스레드 (§5.5) |
| `nohz_full=0-3` 처럼 전 CPU | 부트 CPU가 빠지고 경고, tick 정지 효과 없음 | timekeeping을 할 housekeeping CPU가 필요 | 최소 1개(보통 CPU0) housekeeping |
| irqbalance가 켜진 채 수동 affinity | 몇 분 뒤 IRQ가 다시 RT 코어로 | irqbalance가 주기적으로 다시 배치 | irqbalance 끄기 또는 banned CPU 설정 |
| DEADLINE 태스크에서 `fork`/`taskset` | `EAGAIN`, `EBUSY`, `EPERM` | fork 금지, affinity는 root domain 전체여야 함 (§3) | `SCHED_FLAG_RESET_ON_FORK`, cpuset 파티션 |
| `/proc/irq/N/smp_affinity_list` 쓰기 실패 | `write error: Operation not permitted` | managed IRQ (멀티큐 장치) | `isolcpus=managed_irq`, 장치 큐 수 줄이기, RT CPU에서 I/O 안 하기 |

---

## 15. 면접에서 이렇게 말한다

**Q.** Design the thread/priority layout for a robot control computer.

**A.** 먼저 CPU를 나눈다: CPU0~2는 housekeeping(모든 IRQ, kworker, systemd, ROS 노드, 로거), CPU3은 격리해서 1 kHz 제어 루프만. 우선순위는 "데이터 흐름 순서 = 우선순위 순서": 제어 루프가 기다리는 센서 IRQ 스레드(SPI IMU) 90, 제어 루프 80, 모터 CAN TX/RX 70, 통신(DDS 수신) 60, 나머지 IRQ 스레드 기본 50, 일반 노드 SCHED_OTHER, 로그 압축은 SCHED_IDLE. 99는 비운다. 제어 루프는 mlockall·prefault·ABSTIME sleep·syscall 없는 경로, 로그는 SPSC 링으로 CPU0 로거에 넘긴다. 마지막으로 최악 부하에서 cyclictest·timerlat으로 검증한다.

> I'd start by partitioning CPUs: one or two isolated cores for the hard control loop, the rest as housekeeping for IRQs, kernel threads, ROS nodes and logging. Priorities follow the data path: the IMU's threaded IRQ at FIFO 90, the 1 kHz control loop at 80, motor CAN at 70, middleware receive threads at 60, other IRQ threads stay at the default 50, everything else SCHED_OTHER, and bulk work like log compression at SCHED_IDLE. I leave 99 free. The control loop locks memory, prefaults, sleeps with clock_nanosleep TIMER_ABSTIME on CLOCK_MONOTONIC, and never does I/O — it pushes records into a lock-free SPSC ring drained by a logger on a housekeeping core. Then I validate with cyclictest or rtla timerlat under worst-case load and temperature.

**꼬리 질문**: "Why is the IRQ thread above the control loop?" → 루프가 그 데이터를 기다리기 때문. 반대면 루프가 깨어 있어도 데이터가 안 온다(사실상 우선순위 역전).

**Q.** isolcpus vs nohz_full vs cpuset — which and why?

**A.** 세 개는 다른 것을 끈다. `isolcpus=domain`은 load balancing에서 CPU를 빼서 "명시적으로 고정한 태스크만" 오게 하고, `nohz_full`은 태스크가 하나뿐일 때 tick을 멈추고 RCU callback을 내보낸다. cpuset `isolated` 파티션은 domain 격리를 런타임에 바꿀 수 있게 한 현대적 방법이다(커널 문서도 isolcpus를 deprecated로 표시하고 cpuset을 권한다). tick 정지는 부트 파라미터 `nohz_full`로만 된다. 그래서 실무 조합은 "nohz_full + rcu_nocbs + irqaffinity는 부팅 때, domain 격리는 cpuset 파티션 또는 isolcpus".

> They remove different noise. isolcpus=domain takes the CPU out of scheduler load balancing so only explicitly pinned tasks run there; managed_irq keeps managed device interrupts off it where possible. nohz_full stops the periodic tick when a single task is running and offloads RCU callbacks — that's boot-time only. A cgroup v2 cpuset partition set to "isolated" gives the same domain isolation at runtime, can be changed without reboot, and is what the kernel docs now recommend over isolcpus. In practice I combine nohz_full, rcu_nocbs and irqaffinity on the command line with a cpuset isolated partition, and I verify it through /sys/devices/system/cpu/nohz_full, cpuset.cpus.isolated and per-CPU counts in /proc/interrupts.

**Q.** How do you write a periodic real-time loop in Linux?

**A.** `CLOCK_MONOTONIC` 절대 시각으로 `clock_nanosleep(TIMER_ABSTIME)`, 다음 목표 = 이전 목표 + period. 루프 전에 mlockall·prefault·할당·파일 열기를 끝내고 마지막에 SCHED_FIFO로 바꾼다. 루프 안에서는 syscall·malloc·lock 없이. overrun(목표를 이미 지난 경우)을 세서 보고한다.

> clock_nanosleep with TIMER_ABSTIME on CLOCK_MONOTONIC, where the next deadline is the previous deadline plus the period, so errors don't accumulate. All setup — mlockall, stack and heap prefault, allocations, opening files — happens before switching to SCHED_FIFO, and the loop body has no syscalls, allocation or blocking locks. I count overruns explicitly.

**Q.** What does mlockall actually protect you from?

**A.** 첫 접근 minor fault와 회수 후 major fault. 측정해 보면 잠금 없는 루프에서 5000주기 동안 fault 2054번, 잠근 뒤 1번. 하지만 malloc의 syscall, IPI, 캐시 미스는 못 막는다. RLIMIT_MEMLOCK과 스레드 스택 크기도 같이 신경 쓴다.

> It removes page faults from the RT path — first-touch minor faults and major faults after reclaim, including code pages. It doesn't stop allocator syscalls, TLB shootdown IPIs or cache misses. You also need RLIMIT_MEMLOCK raised and small explicit thread stacks, because MCL_FUTURE locks every new thread's whole stack.

**Q.** What are the default priorities of kernel threads on PREEMPT_RT, and what do you change?

**A.** 예제 2의 실제 목록: 디바이스 IRQ 스레드 `irq/N-name`은 FIFO 50(`sched_set_fifo()`), `ktimers/N`·`rcuc/N`·`rcu_preempt`·`rcub`·`irq_work/N`은 FIFO 1, `migration/N`은 FIFO 99로 보이지만 stop 클래스, 나머지 kworker는 SCHED_OTHER. 내가 바꾸는 건 (1) 제어 루프가 기다리는 센서 IRQ 스레드를 루프보다 위로, (2) 상관없는 IRQ는 50 그대로 두되 housekeeping CPU로, (3) RT CPU의 FIFO 1 스레드가 굶지 않게 루프가 반드시 잠들게 하고 RCU callback은 `rcu_nocbs`로 내보낸다.

> On PREEMPT_RT, device interrupt handlers run as irq/N-name threads at SCHED_FIFO 50, set by sched_set_fifo(). The softirq-ish work — ktimers/N for timers, rcuc/N for RCU callbacks, irq_work/N — runs at FIFO 1, and the migration threads show up as FIFO 99 but actually live in the stop class. I raise the IRQ thread that feeds the control loop above the loop, move unrelated IRQs and their threads to housekeeping cores, and make sure the RT loop always blocks so the FIFO 1 threads on that core don't starve — plus rcu_nocbs to move RCU callbacks off it.

**Q.** When would you use SCHED_DEADLINE instead of SCHED_FIFO?

**A.** 작업마다 CPU 예산을 강제하고 싶을 때 — 폭주해도 남을 못 굶긴다. 대신 WCET를 알아야 하고, fork 금지, affinity가 root domain 전체여야 한다는 제약이 있다. 한 CPU에 두려면 cpuset 파티션으로 root domain을 쪼갠다. 그리고 DEADLINE은 모든 FIFO(IRQ 스레드 포함)보다 위라는 점.

> When I need temporal isolation — a guaranteed budget per period with enforcement, so a misbehaving task can't starve others. The cost is that I must know the WCET, DEADLINE tasks can't fork, admission control can reject the request with EBUSY, and the affinity must span the root domain — to pin one I create a cpuset partition. Also remember it runs above every FIFO task, including IRQ threads.

**Q.** A pinned SCHED_FIFO loop still shows rare 100 µs spikes on an isolated core. How do you debug it?

**A.** `/proc/interrupts`의 그 CPU 열을 시간 차로 비교(managed IRQ, IPI, 잔여 tick), `rtla timerlat`/`osnoise`로 IRQ·softirq·thread 잡음 분리(N3), `trace-cmd`로 spike 순간의 이벤트(M5), 그리고 하드웨어 요인(idle exit, 주파수, 온도, 공유 캐시) 확인.

> First I diff /proc/interrupts for that CPU to see which interrupts still land there — managed queue IRQs, IPIs from TLB shootdowns, the residual tick. Then rtla timerlat or osnoise to split the noise into IRQ, softirq and thread contributions, and a trace around the spike. If the kernel is quiet, it's hardware: idle exit latency, frequency changes, thermal throttling, or shared cache and DRAM contention from other cores.

---

## 16. 직접 해보기

1. (개념) 2 CPU 시스템에서 SCHED_DEADLINE 예약 0.6, 0.6, 0.6을 차례로 요청하면 몇 번째가 거절되나? fair_server 포함해서 계산하라.
정답/힌트: 한계 2 × 0.95 = 1.9, fair_server 2 × 0.05 = 0.1. 0.1 + 0.6 × 3 = 1.9 → 정확히 경계다. 커널은 2^20 = 1.0 고정소수점으로 계산한다(0.95 → 996147, 0.05 → 52428, 0.6 → 629145, 내림): 한계 1992294 vs 합계 104856 + 3 × 629145 = 1992291이라 세 번째도 승인된다(계산상). 0.65로 바꾸면 세 번째가 `EBUSY`. 2 CPU 게스트(`-smp 2`)에서 `n2_dl hold`로 직접 확인하라.
2. (코드) `n2_rt`에 overrun 카운터를 넣어라: `clock_nanosleep` 직후 `now`가 이미 `next + period`를 지났으면 놓친 주기 수를 세고, 정책을 "따라잡기" 대신 "건너뛰기"(next를 now 이후 첫 주기로 옮김)로 바꾸는 옵션을 추가하라.
정답/힌트: `missed = (ns(now) - ns(next)) / PERIOD; next += missed × PERIOD`. 제어기는 보통 건너뛰기가 맞다(오래된 주기를 몰아서 실행하면 안 된다).
3. (관찰) Image-rt를 `nohz_full=3` 없이/있이 부팅해 CPU3에 busy 태스크 하나를 두고 5초간 `arch_timer` 증가량을 비교하라. 태스크를 두 개로 늘리면?
정답/힌트: 두 개면 스케줄러가 시간 분할을 해야 하므로 tick이 다시 돈다(1000/s). nohz_full은 "태스크 하나"일 때만 효과.
4. (관찰) cpuset `isolated` 파티션을 만든 뒤 `/sys/devices/virtual/workqueue/cpumask`와 루트 `cpuset.cpus.isolated`를 확인하고, 파티션을 `member`로 되돌렸을 때 값이 어떻게 바뀌나 보라.
정답/힌트: isolated 파티션의 CPU가 unbound workqueue 마스크에서 빠졌다가 되돌리면 복귀한다(§7 출력과 비교).
5. (코드) `n2_spsc`의 로거 스레드 sleep을 10 ms → 500 ms로 바꾸고 `RING`을 256으로 줄여 `dropped`가 생기게 하라. 링 크기 공식(로거 최대 정지 × 생산 속도)과 맞는지 확인.
정답/힌트: 1 kHz × 0.5 s = 500 > 256 → 주기마다 약 244개 버림.
6. (설계) 카메라 30 fps 인코딩 + 1 kHz IMU 융합 + 500 Hz 모터 제어 + DDS 통신이 있는 4코어 SoC의 CPU 배치와 우선순위 지도를 그려라.
정답/힌트: 카메라·인코더·DDS·로거는 housekeeping, IMU 융합과 모터 제어는 격리 코어(같은 코어면 IMU 융합 > 모터 제어 또는 하나의 루프로 합침), IMU SPI IRQ 스레드와 CAN IRQ 스레드를 각 소비자보다 높게.
7. (관찰) `/proc/<pid>/timerslack_ns`를 SCHED_OTHER와 FIFO에서 비교하고, `prctl(PR_SET_TIMERSLACK, 1)`을 한 SCHED_OTHER 루프의 p50이 FIFO와 비슷해지는지 Lima VM에서 재 보라.
정답/힌트: OTHER 50000 → FIFO 0. slack을 1 ns로 줄이면 p50의 50 µs 차이가 대부분 사라진다(F8 §9.4).

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| jitter | 주기 시작 시각의 흔들림 | 평균이 아니라 분포의 폭과 꼬리 |
| drift | 누적 오차 | 상대 sleep에서 주기마다 쌓이는 시간 |
| SCHED_FIFO / RR | RT 정책 | 고정 우선순위 1–99, RR은 같은 우선순위끼리 교대 |
| SCHED_DEADLINE | 예약 정책 | runtime/deadline/period, EDF + CBS |
| admission control | 승인 검사 | 예약 합이 한계를 넘으면 `EBUSY` |
| root domain | 스케줄링 도메인 묶음 | DEADLINE 대역폭과 RT push/pull의 범위 |
| housekeeping CPU | 잡일 담당 CPU | IRQ·kthread·tick·RCU를 몰아 두는 CPU |
| isolcpus | 부트 파라미터 | `domain`, `managed_irq`, `nohz` 플래그로 격리 |
| nohz_full | adaptive-tick | 태스크가 하나일 때 tick 정지 |
| rcu_nocbs | RCU callback offload | callback을 `rcuo` 스레드로 다른 CPU에서 실행 |
| cpuset partition | cgroup v2 격리 | `cpuset.cpus.partition = isolated`로 런타임 격리 |
| managed IRQ | 커널 관리 affinity | 멀티큐 장치의 큐별 IRQ, `/proc/irq`로 못 바꿈 |
| IRQ affinity | IRQ가 갈 CPU | `/proc/irq/N/smp_affinity_list` |
| threaded IRQ | IRQ 스레드 | `irq/N-name`, 기본 FIFO 50 |
| mlockall | 메모리 잠금 | 페이지를 RAM에 고정하고 미리 채움 |
| prefault | 미리 건드리기 | 루프 전에 스택·버퍼를 써서 fault를 앞당김 |
| timer slack | 타이머 여유 | 일반 태스크 50 µs, RT는 0 |
| PM QoS | 전원 QoS 요청 | `/dev/cpu_dma_latency`로 깊은 idle 금지 |
| SPSC ring | 단일 생산자·소비자 링 | lock 없이 RT 경로에서 데이터를 넘김 |
| MPAM | Arm 메모리 자원 분할 | 캐시·대역폭을 파티션별로 할당 |

---

## 18. 요약 & 체크리스트

실시간 튜닝은 "내 루프가 늘 같은 시각에 돈다"를 위해 남의 일을 치우는 작업이다. 순서는 (1) 루프 구조를 정석으로(ABSTIME, mlockall, prefault, syscall 없는 경로), (2) 정책과 우선순위(FIFO, 데이터 흐름 순서의 지도, IRQ 스레드 포함), (3) 자리(격리: nohz_full·rcu_nocbs·irqaffinity + cpuset 파티션 또는 isolcpus, IRQ affinity, irqbalance), (4) 하드웨어(PM QoS, 고정 클럭, 공유 캐시). 각 단계에서 측정하고, 격리가 실제로 먹혔는지는 숫자가 아니라 `/sys/devices/system/cpu/{isolated,nohz_full}`, `cpuset.cpus.isolated`, `/proc/interrupts` CPU별 카운트, tick 수로 증명한다. 에뮬레이터 숫자는 상대 비교로만 읽는다.

- [ ] SCHED_OTHER/BATCH/IDLE/FIFO/RR/DEADLINE이 어느 클래스에 속하고 언제 쓰는지 설명할 수 있다
- [ ] `chrt`, `taskset`, `sched_setattr`로 정책·affinity·DEADLINE을 걸 수 있다
- [ ] DEADLINE admission control을 계산하고 `EBUSY`/`EPERM`/`EAGAIN`이 왜 나는지 말할 수 있다
- [ ] 로봇 스택의 우선순위 지도(IRQ 스레드 포함)를 그리고 `chrt -f -p`로 적용할 수 있다
- [ ] PREEMPT_RT의 커널 스레드 기본 우선순위(irq/N 50, ktimers, rcuc)를 말할 수 있다
- [ ] `clock_nanosleep(TIMER_ABSTIME)` 정석 루프를 mlockall·prefault와 함께 짤 수 있다
- [ ] 상대 sleep 루프의 드리프트와 timer slack의 효과를 수치로 설명할 수 있다
- [ ] SPSC 링으로 RT 경로에서 I/O를 빼는 코드를 짤 수 있다
- [ ] isolcpus의 세 플래그, nohz_full, rcu_nocbs, irqaffinity의 역할을 구분할 수 있다
- [ ] cgroup v2 cpuset `isolated` 파티션을 만들고 효과를 확인할 수 있다
- [ ] managed IRQ가 왜 `/proc/irq`로 안 옮겨지는지 설명할 수 있다
- [ ] `/dev/cpu_dma_latency`의 의미와 "fd를 열어 둬야 한다"는 규칙을 말할 수 있다

---

## 참고 자료

- man7.org: [sched(7)](https://man7.org/linux/man-pages/man7/sched.7.html), [sched_setattr(2)](https://man7.org/linux/man-pages/man2/sched_setattr.2.html), [clock_nanosleep(2)](https://man7.org/linux/man-pages/man2/clock_nanosleep.2.html), [mlock(2)](https://man7.org/linux/man-pages/man2/mlock.2.html), [chrt(1)](https://man7.org/linux/man-pages/man1/chrt.1.html), [taskset(1)](https://man7.org/linux/man-pages/man1/taskset.1.html), [mallopt(3)](https://man7.org/linux/man-pages/man3/mallopt.3.html)
- docs.kernel.org: [Deadline Task Scheduling](https://docs.kernel.org/scheduler/sched-deadline.html), [Real-Time group scheduling](https://docs.kernel.org/scheduler/sched-rt-group.html), [NO_HZ: Reducing Scheduling-Clock Ticks](https://docs.kernel.org/timers/no_hz.html), [Control Group v2 (cpuset 파티션)](https://docs.kernel.org/admin-guide/cgroup-v2.html), [The kernel's command-line parameters](https://docs.kernel.org/admin-guide/kernel-parameters.html), [PM Quality Of Service Interface](https://docs.kernel.org/power/pm_qos_interface.html), [CPU Isolation 관련 kernel/sched/isolation.c](https://elixir.bootlin.com/linux/v6.18/source/kernel/sched/isolation.c)
- Linux Foundation Real-Time Linux 위키: [HOWTO build a simple RT application](https://wiki.linuxfoundation.org/realtime/documentation/howto/applications/application_base) (mlockall, prefault, 정석 루프)
- Michael Kerrisk, "The Linux Programming Interface" 35장(프로세스 우선순위와 스케줄링), 23장(타이머와 sleep), 50장(메모리 잠금)
- 소스(6.18.55, 랩 `/var/lab/src/linux-6.18.55`): `kernel/sched/syscalls.c`(`__setscheduler_params`, `sched_set_fifo`), `kernel/sched/deadline.c`(`sched_dl_overflow`), `kernel/time/hrtimer.c`(`__hrtimer_setup_sleeper`의 PREEMPT_RT 하드 모드), `kernel/irq/manage.c`(`irq_thread`), `kernel/power/qos.c`, `kernel/cgroup/cpuset.c`
- 관련 노트: F1, F5, F6, F7, F8, A2, A5, L1, L8, M5, N1, N3, N5, N6
