# F8. 시간과 타이머 — jiffies, HZ, timer_list, hrtimer, ktime, clocksource/clockevent, delay 함수

> **이 노트를 다 읽으면**: arm64 generic timer(CNTVCT_EL0·CNTFRQ_EL0·CPU별 비교기)에서 clocksource/clockevent, tick, timer wheel/hrtimer로 올라가는 층을 그릴 수 있다 · jiffies wraparound 버그를 재현하고 `time_after()`로 고칠 수 있다 · `timer_list`와 hrtimer를 6.18 API(`timer_setup`·`timer_delete_sync`·`timer_container_of`·`hrtimer_setup`)로 쓰고, HZ=250에서 1 ms 타이머가 왜 4~8 ms 뒤에 울리는지 측정값으로 설명할 수 있다 · `udelay`/`usleep_range`/`msleep`/`fsleep` 중 무엇을 쓸지, 센서 타임스탬프에 어떤 시계를 쓸지 고를 수 있다
> **왜 필요한가**: "폴링 주기가 왜 들쭉날쭉한가", "msleep(1)이 왜 4 ms 넘게 자나", "ISR에서 mdelay 써도 되나", "센서 타임스탬프는 어떤 clock으로 찍나"는 드라이버 면접과 센서 bring-up에서 반드시 나온다
> **Don 기준 난이도**: SysTick·비교 일치 타이머·free-running 카운터·busy-wait 지연은 MCU에서 매일 쓰던 것 / 새로 배울 부분은 "타이머가 수만 개인 OS가 하드웨어 비교기 하나를 어떻게 나눠 쓰나"(timer wheel vs hrtimer), tick이 꺼지는 NO_HZ, 여러 종류의 시계(MONOTONIC·RAW·BOOTTIME·REALTIME), 그리고 "어떤 컨텍스트에서 어떤 기다림이 허용되나"
> **선행 노트**: E3(커널 모듈), E4(커널 제약 — atomic context), E5(커널 C 관용구 — `container_of`) · 함께 보면 좋은 노트: F2(실행 컨텍스트), F6(인터럽트), F7(지연 작업 — softirq), M5(ftrace), N1(실시간), Q3(멀티 센서 동기화·타임스탬프)

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 MCU 펌웨어에서 "10 ms마다 IMU를 읽는다"를 구현할 때는 이렇게 했을 것이다. SysTick을 1 kHz로 돌리고 ISR에서 카운터를 증가시키거나, 하드웨어 타이머(TIMx) 하나를 잡아서 비교 일치 인터럽트로 정확히 10 ms마다 깨운다. 타임스탬프는 DWT `CYCCNT` 같은 free-running 카운터를 읽는다. **타이머 하드웨어 하나 = 용도 하나**였다.

Linux 커널에는 하드웨어 타이머가 CPU마다 사실상 **하나**(arm64 generic timer의 비교기)뿐인데, 그걸로 해결해야 하는 "나중에 깨워 줘" 요청은 수천~수만 개다. 네트워크 재전송 타임아웃, 디스크 I/O 타임아웃, `nanosleep()` 중인 프로세스, 스케줄러의 시간 할당량, 그리고 내 드라이버의 폴링 타이머. 그래서 커널은 하드웨어를 **두 가지 역할**로 추상화하고(clocksource = "지금 몇 시?", clockevent = "이 시각에 깨워 줘"), 그 위에 **두 가지 소프트웨어 타이머**를 올린다.

- **timer wheel (`timer_list`)** — jiffies(= tick 횟수) 단위의 싸고 대충인 타이머. 대부분 만료 전에 취소되는 타임아웃용.
- **hrtimer** — 나노초 단위의 정확한 타이머. 만료 시각을 하드웨어 비교기에 바로 프로그램한다.

```svg
<svg viewBox="0 0 680 444" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="64" font-size="12">쓰는 쪽</text>
  <text x="10" y="138" font-size="12">시간 코어</text>
  <text x="10" y="236" font-size="12">장치 추상</text>
  <text x="10" y="320" font-size="12">하드웨어</text>
  <text x="10" y="396" font-size="12">MCU 비유</text>
  <rect x="100" y="36" width="134" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="167" y="55" text-anchor="middle" font-size="12">mod_timer()</text>
  <text x="167" y="72" text-anchor="middle" font-size="12">msleep()</text>
  <rect x="246" y="36" width="134" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="313" y="55" text-anchor="middle" font-size="12">hrtimer_start()</text>
  <text x="313" y="72" text-anchor="middle" font-size="12">usleep_range()</text>
  <rect x="392" y="36" width="134" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="459" y="55" text-anchor="middle" font-size="12">ktime_get()</text>
  <text x="459" y="72" text-anchor="middle" font-size="12">clock_gettime()</text>
  <rect x="538" y="36" width="134" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="605" y="55" text-anchor="middle" font-size="12">스케줄러 · RCU</text>
  <text x="605" y="72" text-anchor="middle" font-size="12">CPU 시간 통계</text>
  <rect x="100" y="110" width="134" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="167" y="129" text-anchor="middle" font-size="12">timer wheel</text>
  <text x="167" y="146" text-anchor="middle" font-size="12">jiffies 단위</text>
  <rect x="246" y="110" width="134" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="313" y="129" text-anchor="middle" font-size="12">hrtimer</text>
  <text x="313" y="146" text-anchor="middle" font-size="12">rbtree, ns 단위</text>
  <rect x="392" y="110" width="134" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="459" y="129" text-anchor="middle" font-size="12">timekeeping</text>
  <text x="459" y="146" text-anchor="middle" font-size="12">ktime = ns 시계</text>
  <rect x="538" y="110" width="134" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="605" y="129" text-anchor="middle" font-size="12">tick (HZ=250)</text>
  <text x="605" y="146" text-anchor="middle" font-size="12">4 ms 주기 hrtimer</text>
  <rect x="100" y="208" width="280" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="240" y="227" text-anchor="middle" font-size="12">clockevent: arch_sys_timer</text>
  <text x="240" y="244" text-anchor="middle" font-size="12">CPU마다 하나 · 다음 만료 시각을 쓴다</text>
  <rect x="392" y="208" width="280" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="532" y="227" text-anchor="middle" font-size="12">clocksource: arch_sys_counter</text>
  <text x="532" y="244" text-anchor="middle" font-size="12">시스템에 하나 · 읽기만 한다</text>
  <rect x="100" y="290" width="280" height="58" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="240" y="310" text-anchor="middle" font-size="12">CPU별 비교기 CNTV_CVAL_EL0</text>
  <text x="240" y="328" text-anchor="middle" font-size="12">카운터 ≥ CVAL → INTID 27 → 그 CPU</text>
  <rect x="392" y="290" width="280" height="58" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="532" y="310" text-anchor="middle" font-size="12">공용 64비트 카운터 CNTVCT_EL0</text>
  <text x="532" y="328" text-anchor="middle" font-size="12">CNTFRQ_EL0 = 62.5 MHz (QEMU)</text>
  <rect x="100" y="374" width="280" height="46" rx="5" fill="#888" fill-opacity="0.05" stroke="#888" stroke-dasharray="4 3"/>
  <text x="240" y="393" text-anchor="middle" font-size="12">SysTick · TIMx 비교 일치 인터럽트</text>
  <text x="240" y="410" text-anchor="middle" font-size="12">(다음에 깨울 시각을 레지스터에)</text>
  <rect x="392" y="374" width="280" height="46" rx="5" fill="#888" fill-opacity="0.05" stroke="#888" stroke-dasharray="4 3"/>
  <text x="532" y="393" text-anchor="middle" font-size="12">DWT CYCCNT 같은 free-running</text>
  <text x="532" y="410" text-anchor="middle" font-size="12">카운터 (타임스탬프용)</text>
  <line x1="167" y1="82" x2="167" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="313" y1="82" x2="313" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="459" y1="82" x2="459" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="605" y1="108" x2="605" y2="84" stroke="currentColor" marker-end="url(#ah1)"/>
  <path d="M605,156 L605,180 L167,180 L167,158" fill="none" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#ah1)"/>
  <text x="560" y="196" text-anchor="middle" font-size="12">jiffies++ · softirq</text>
  <line x1="313" y1="156" x2="313" y2="206" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="459" y1="206" x2="459" y2="158" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="220" y1="254" x2="220" y2="288" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="260" y1="288" x2="260" y2="256" stroke="#d0564a" marker-end="url(#ah1)"/>
  <text x="268" y="276" font-size="12">IRQ</text>
  <line x1="532" y1="288" x2="532" y2="256" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="240" y1="348" x2="240" y2="372" stroke="currentColor" stroke-dasharray="2 3"/>
  <line x1="532" y1="348" x2="532" y2="372" stroke="currentColor" stroke-dasharray="2 3"/>
  <line x1="392" y1="319" x2="380" y2="319" stroke="currentColor" stroke-dasharray="2 3"/>
</svg>
```

그림 1 — 시간 서브시스템의 층. 아래 두 줄(하드웨어·장치 추상)은 MCU의 SysTick/비교 일치 타이머와 free-running 카운터에 해당한다. 그 위에서 tick(4 ms 주기 hrtimer)이 jiffies를 올리고 timer wheel을 돌린다. 점선 = tick이 하는 일.

### 0.1 MCU 개념 ↔ Linux 개념 대응표

| MCU 펌웨어 (Don이 아는 것) | Linux 커널 | 절 |
|---|---|---|
| free-running 카운터 (DWT `CYCCNT`, TIMx `CNT`) | **clocksource** `arch_sys_counter` (CNTVCT_EL0 읽기) | §2, §3 |
| 비교 일치 레지스터 + 인터럽트 (TIMx `CCR`) | **clockevent** `arch_sys_timer` (CNTV_CVAL_EL0 + PPI) | §2, §3 |
| SysTick 1 kHz + `g_ticks++` | **tick** (HZ=250) + `jiffies++` | §4, §5 |
| `if ((int32_t)(g_ticks - deadline) >= 0)` | `time_after_eq(jiffies, deadline)` | §5 |
| 소프트웨어 타이머 리스트 (FreeRTOS timer task) | **timer wheel** (`timer_list`) | §6 |
| 하드웨어 타이머 하나를 정확한 주기로 | **hrtimer** + `hrtimer_forward_now()` | §7, §9 |
| `HAL_GetTick()` / `CYCCNT` 타임스탬프 | `ktime_get()` / `clock_gettime(CLOCK_MONOTONIC)` | §8 |
| `for (i...) __NOP();` 지연 | `udelay()` (busy-wait) | §10 |
| `vTaskDelay()` | `msleep()` / `usleep_range()` / `fsleep()` | §10 |

### 0.2 이 노트에서 직접 측정하는 것

| 질문 | 측정 결과 (QEMU, 아래 §1.3 조건) | 절 |
|---|---|---|
| HZ=250에서 1 ms `timer_list`는 언제 울리나 | **4.05 ~ 7.97 ms** (중앙값 5.97 ms) | §7.4 |
| 같은 1 ms를 hrtimer로 걸면 | **1.008 ms** (200회 중 199회) | §7.4 |
| `msleep(1)`은 실제로 얼마나 자나 | **4.1 ~ 8.0 ms** (평균 6.4 ms) | §10.3 |
| 1 kHz hrtimer 샘플링 1000주기의 늦음 | p50 7 us, 최대 13 us, overrun 0 | §9.3 |
| "now + 주기"로 재무장하면 1000주기 동안 쌓이는 드리프트 | **+8.9 ms** | §9.3 |

---

## 1. 실습 준비

### 1.1 실행 환경

이 노트의 커널 코드는 공용 랩의 QEMU 게스트(Linux 6.18.55 arm64, `virt` 머신, cortex-a57, `-smp 2`, 512 MB)에서 돌렸다. VM 호스트 커널(`uname -r` → `7.0.0-38-generic`)에는 랩 모듈을 넣지 않는다. 랩 커널은 두 개다.

| 이미지 | 용도 | 이 노트에서 |
|---|---|---|
| `/var/lab/out/Image` (6.18.55) | 기본 랩 커널 (defconfig 계열) | **모든 측정** (모듈은 `out/linux-build`로 빌드) |
| `/var/lab/out/Image-dbg` (6.18.55-dbg) | + ftrace, lockdep, DEBUG_ATOMIC_SLEEP 등 | ftrace 트레이스(§7.5), `/proc/timer_list` 덤프(§3) |

두 커널 모두 타이머 관련 설정은 같다.

```sh
grep -E "^(# )?CONFIG_(HZ|HZ_250|NO_HZ_IDLE|NO_HZ_FULL|HIGH_RES_TIMERS|PREEMPT|ARM_ARCH_TIMER)[= ]" /var/lab/out/linux-build-dbg/.config
```

```text
CONFIG_NO_HZ_IDLE=y
# CONFIG_NO_HZ_FULL is not set
CONFIG_HIGH_RES_TIMERS=y
CONFIG_PREEMPT=y
CONFIG_HZ_250=y
CONFIG_HZ=250
CONFIG_ARM_ARCH_TIMER=y
```

(위 출력은 실제 `grep` 결과에서 이 줄들만 남긴 것이다. 같은 설정 파일에 `CONFIG_TICK_ONESHOT=y`, `CONFIG_NO_HZ_COMMON=y`도 있다.) **HZ=250 → tick 하나 = 4 ms**. 이 숫자가 이 노트 전체의 "양자화 단위"다.

### 1.2 예제 1 — 게스트 실행 스크립트

무엇을 확인하나: `.ko`·바이너리와 테스트 스크립트를 랩 initramfs 복사본에 넣고, 커널 인자 `LAB_CMD=sh~/t.sh`로 스크립트 하나를 실행한 뒤 자동으로 꺼지는 실행기. E4 노트의 실행기에 **`ICOUNT` 옵션**을 더했다(§1.3).

```sh
#!/bin/bash
# usage: run_guest.sh <tag> <test.sh> [extra kernel args] ; env FILES, SMP, TMO, ICOUNT, KIMG
set -e
tag=$1; t=$2; extra=$3
W=/var/tmp/F8/run_$tag
rm -rf $W && mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
for f in $FILES; do cp $f $W/r/; done
cp $t $W/r/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
KIMG=${KIMG:-/var/lab/out/Image-dbg}
IC=""; [ -n "$ICOUNT" ] && IC="-icount shift=$ICOUNT"
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m 512M -nographic -no-reboot $IC \
  -kernel $KIMG -initrd $W/i.gz -append "console=ttyAMA0 quiet $extra LAB_CMD=sh~/t.sh" 2>&1 | tr -d '\r'
```

모듈 빌드는 노트 전체에서 같은 방식이다 (`Kbuild` 한 줄 + 랩 빌드 트리, 출력은 `/var/tmp/F8/`).

```sh
mkdir -p /var/tmp/F8/nd/f8_lat && cd /var/tmp/F8/nd/f8_lat     # f8_lat.c + Kbuild 복사
echo "obj-m += f8_lat.o" > Kbuild
make -C /var/lab/out/linux-build M=$PWD modules
```

```text
  CC [M]  f8_lat.o
  MODPOST Module.symvers
  CC [M]  f8_lat.mod.o
  CC [M]  .module-common.o
  LD [M]  f8_lat.ko
```

(`make: Entering/Leaving directory` 줄은 생략.) 출력에서 볼 것: 경고 0개. (처음 빌드 때 `WARNING: modpost: missing MODULE_DESCRIPTION()`이 나와서 모든 모듈에 `MODULE_DESCRIPTION`을 넣었다. 6.x modpost는 이걸 경고로 잡는다.)

### 1.3 QEMU에서 시간을 재는 법 — 무엇이 에뮬레이터 탓인가

이 노트는 "타이머가 **얼마나 늦게** 울리나"를 잰다. 그런데 QEMU TCG는 게스트 CPU를 호스트 스레드로 흉내 낸다. 측정하는 동안 같은 VM에서 다른 작업자 11명이 빌드·부팅을 돌리고 있었고(load average 14~15), 그러면 게스트 vCPU 스레드가 호스트 스케줄러에 밀려 **수 ms씩 멈춘다**. 게스트 입장에서는 "타이머가 수 ms 늦게 울렸다"로 보인다. 이건 커널의 성질이 아니라 **에뮬레이터 잡음**이다.

C7 노트에서 쓴 `-icount`로 이걸 분리한다. 세 가지 실행 모드를 비교했다.

| 모드 | QEMU 옵션 | 게스트 시계가 가는 방식 | 이 노트에서 쓰는 곳 |
|---|---|---|---|
| 실시간 TCG | (없음) | 호스트 실제 시간 | "현실적인 잡음" 예시 |
| icount | `-icount shift=3` | 명령어 1개 = 8 ns. 단, 게스트가 idle이면 실제 시간만큼 흐름 | 비교용 |
| **icount, sleep=off** | `-icount shift=3,sleep=off` | 명령어 1개 = 8 ns. idle이면 **다음 타이머 시각으로 즉시 점프** | **커널 메커니즘 측정 (주 데이터)** |

`sleep=off`가 핵심이다. 기본 icount(`sleep=on`)는 게스트가 WFI로 쉬는 동안 가상 시계를 실제 시간만큼 흘려 보내기 때문에, 그 사이 호스트가 밀리면 여전히 잡음이 들어온다(실제로 1 kHz 샘플링에서 overrun이 14~143회 나왔다). `sleep=off`면 게스트 시간은 **오직 실행한 명령어 수**로만 간다. 그러면 남는 지연은 (1) 커널 설계가 만든 양자화(tick 4 ms 등)와 (2) 125 MIPS짜리 가상 CPU가 인터럽트 경로 코드를 실행하는 시간뿐이다.

읽는 규칙:

- **tick 4 ms 단위의 양자화, "절대 일찍 울리지 않음", 드리프트 누적** → 커널 설계. 실제 보드에서도 똑같다.
- **hrtimer의 수 us 지연** → 125 MIPS 가상 CPU에서 인터럽트 진입·콜백 코드를 실행하는 시간. 실제 GHz급 코어에서는 다르다 (보드에서 다시 재야 한다).
- **실시간 TCG의 ms급 꼬리(tail)** → 호스트 부하 잡음. 실제 보드의 꼬리는 IRQ off 구간·선점 금지 구간·캐시·전력 상태가 만든다 (N1, M5).

> 디버그 커널(`Image-dbg`)로는 측정하지 않았다. 같은 `f8_jiffies` 모듈로 `ktime_get()` 1회 비용을 재 보니 기본 커널은 실시간 TCG에서 269 ns였고, 디버그 커널은 17,171 ns였다(§8.3). lockdep 같은 디버그 장치가 시간 읽기 경로까지 무겁게 만든다. **타이밍은 양산 설정 커널에서 잰다**는 것도 이 노트의 교훈 중 하나다.

---

## 2. 하드웨어 — arm64 generic timer

### 2.1 직관

ARMv8 코어에는 아키텍처가 정한 타이머가 들어 있다(**generic timer**). 구성은 MCU 타이머와 놀랄 만큼 비슷하다.

- **시스템 카운터 하나** — SoC 전체가 공유하는 64비트 free-running 카운터. 주파수는 `CNTFRQ_EL0` 레지스터에 적혀 있다(펌웨어가 부팅 때 써 넣는다). 모든 CPU가 같은 값을 본다.
- **CPU마다 비교기 몇 개** — physical(EL1용), virtual(게스트/커널용), hypervisor용. 각 비교기는 `CVAL`(비교값) 또는 `TVAL`(남은 tick 수)과 `CTL`(enable·mask·status)로 이루어진다. 카운터가 CVAL에 도달하면 **그 CPU에만** 가는 PPI(Private Peripheral Interrupt)가 올라간다.

MCU와 다른 점 두 가지:

1. 카운터가 **CPU 클럭과 무관한 고정 주파수**다(보통 수 MHz~수십 MHz, 이 랩의 QEMU는 62.5 MHz, Don의 M2 호스트는 24 MHz). DVFS로 CPU 주파수가 바뀌어도 시간이 흔들리지 않는다. Cortex-M의 `CYCCNT`는 코어 클럭을 세기 때문에 주파수를 바꾸면 환산이 깨진다는 걸 Don은 알고 있을 것이다.
2. **유저 공간(EL0)에서도 카운터를 읽을 수 있다** (`CNTKCTL_EL1`로 커널이 허용). 그래서 `clock_gettime()`이 syscall 없이 vDSO에서 끝난다.

### 2.2 예제 2 — EL0에서 CNTFRQ_EL0·CNTVCT_EL0 직접 읽기 + `clock_getres`

무엇을 확인하나: 유저 프로그램이 `mrs` 명령으로 카운터를 직접 읽을 수 있는지, 그 값이 `CLOCK_MONOTONIC`과 맞는지, 그리고 각 시계의 해상도.

```c
/* f8_clk: clock_getres + EL0에서 CNTFRQ_EL0/CNTVCT_EL0 직접 읽기 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

static inline uint64_t cntvct(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v) :: "memory");
	return v;
}
static inline uint64_t cntfrq(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}
static int64_t ns(clockid_t c)
{
	struct timespec ts;
	if (clock_gettime(c, &ts)) { perror("clock_gettime"); exit(1); }
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main(void)
{
	const struct { clockid_t id; const char *n; } c[] = {
		{ CLOCK_REALTIME, "REALTIME" }, { CLOCK_MONOTONIC, "MONOTONIC" },
		{ CLOCK_MONOTONIC_RAW, "MONOTONIC_RAW" }, { CLOCK_BOOTTIME, "BOOTTIME" },
		{ CLOCK_MONOTONIC_COARSE, "MONOTONIC_COARSE" }, { CLOCK_REALTIME_COARSE, "REALTIME_COARSE" } };
	for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
		struct timespec r;
		if (clock_getres(c[i].id, &r)) { perror("clock_getres"); return 1; }
		printf("%-17s res=%ld ns  now=%lld ns\n", c[i].n, r.tv_nsec, (long long)ns(c[i].id));
	}
	uint64_t f = cntfrq(), v0 = cntvct();
	int64_t m0 = ns(CLOCK_MONOTONIC);
	struct timespec d = { 0, 100 * 1000 * 1000 };
	nanosleep(&d, NULL);
	uint64_t v1 = cntvct();
	int64_t m1 = ns(CLOCK_MONOTONIC);
	printf("CNTFRQ_EL0=%llu Hz (1 tick = %.2f ns)\n", (unsigned long long)f, 1e9 / f);
	printf("100ms sleep: CNTVCT delta=%llu ticks = %.3f ms, CLOCK_MONOTONIC delta=%.3f ms\n",
	       (unsigned long long)(v1 - v0), (v1 - v0) * 1e3 / f, (m1 - m0) / 1e6);
	int64_t t0 = ns(CLOCK_MONOTONIC);
	for (int i = 0; i < 100000; i++) (void)cntvct();
	int64_t t1 = ns(CLOCK_MONOTONIC);
	for (int i = 0; i < 100000; i++) (void)ns(CLOCK_MONOTONIC);
	int64_t t2 = ns(CLOCK_MONOTONIC);
	printf("cost: mrs cntvct_el0 = %lld ns, clock_gettime(MONOTONIC) = %lld ns\n",
	       (long long)(t1 - t0) / 100000, (long long)(t2 - t1) / 100000);
	return 0;
}
```

빌드는 VM에서 `gcc -std=gnu11 -Wall -Wextra -O2 -static f8_clk.c -o f8_clk` (게스트 initramfs는 BusyBox static이라 libc가 없으므로 `-static`). 경고 0개. 먼저 **QEMU 게스트**(Linux 6.18.55, 실시간 TCG):

```text
== guest 6.18.55
REALTIME          res=1 ns  now=1791671058772833936 ns
MONOTONIC         res=1 ns  now=3707275504 ns
MONOTONIC_RAW     res=1 ns  now=3707756432 ns
BOOTTIME          res=1 ns  now=3714955056 ns
MONOTONIC_COARSE  res=4000000 ns  now=3710705744 ns
REALTIME_COARSE   res=4000000 ns  now=1791671058788000000 ns
CNTFRQ_EL0=62500000 Hz (1 tick = 16.00 ns)
100ms sleep: CNTVCT delta=6545576 ticks = 104.729 ms, CLOCK_MONOTONIC delta=104.730 ms
cost: mrs cntvct_el0 = 667 ns, clock_gettime(MONOTONIC) = 831 ns
```

같은 바이너리를 **VM 호스트**(Lima, Apple M2 위의 Linux 7.0, HZ=1000)에서:

```text
REALTIME          res=1 ns  now=1791671006812539577 ns
MONOTONIC         res=1 ns  now=19549371207576 ns
MONOTONIC_RAW     res=1 ns  now=19548668268821 ns
BOOTTIME          res=1 ns  now=19549371208367 ns
MONOTONIC_COARSE  res=1000000 ns  now=19549370948658 ns
REALTIME_COARSE   res=1000000 ns  now=1791671006812296368 ns
CNTFRQ_EL0=24000000 Hz (1 tick = 41.67 ns)
100ms sleep: CNTVCT delta=2419331 ticks = 100.805 ms, CLOCK_MONOTONIC delta=100.808 ms
cost: mrs cntvct_el0 = 14 ns, clock_gettime(MONOTONIC) = 29 ns
```

출력에서 볼 것 (`now=` 값은 실행마다 다르다):

- **`mrs cntvct_el0`가 EL0에서 그대로 동작한다.** 그리고 카운터 차이 ÷ CNTFRQ가 `CLOCK_MONOTONIC` 차이와 us 단위까지 같다 — `CLOCK_MONOTONIC`이 바로 이 카운터를 환산한 값이기 때문이다.
- `res=1 ns`는 "1 ns 정밀도"가 아니라 **hrtimer가 켜져 있다**(`hrtimer_resolution` = 1 ns)는 뜻이다. 실제 하드웨어 분해능은 카운터 1 tick(16 ns / 41.67 ns)이다.
- **`_COARSE` 시계의 해상도 = tick 길이**: 게스트는 4,000,000 ns(HZ=250), 호스트는 1,000,000 ns(HZ=1000). COARSE는 카운터를 읽지 않고 마지막 tick 때 저장해 둔 값을 돌려준다(§8).
- 비용: 실제 실리콘(M2)에서 카운터 읽기 14 ns, `clock_gettime` 29 ns — syscall 없이 vDSO에서 끝나기 때문에 이 정도다. QEMU TCG의 667 ns는 `mrs` 하나하나를 헬퍼 함수로 에뮬레이션하는 비용이라 **에뮬레이터 수치**다.
- 게스트의 "100 ms sleep"이 104.7 ms가 된 것은 실시간 TCG + 높은 호스트 부하 때문이다(`nanosleep` 자체는 hrtimer 기반이라 실제 보드라면 100.0x ms).

### 2.3 PPI와 /proc/interrupts

비교기 인터럽트는 PPI라서 **CPU마다 따로** 센다. 랩의 QEMU `virt` DT에서 timer 노드를 보면:

```sh
grep -n "timer {" -A4 /var/lab/out/virt.dts
```

```text
370:	timer {
371-		interrupts = <0x01 0x0d 0x104 0x01 0x0e 0x104 0x01 0x0b 0x104 0x01 0x0a 0x104>;
372-		always-on;
373-		compatible = "arm,armv8-timer", "arm,armv7-timer";
374-	};
```

세 칸씩 끊어 읽는다: `0x01` = PPI, 그다음이 PPI 번호, `0x104` = CPU 마스크 1 + level-high. 바인딩이 정한 순서는 secure phys(PPI 13) / non-secure phys(PPI 14) / **virtual(PPI 11)** / hypervisor(PPI 10)다. PPI 번호에 16을 더하면 GIC INTID다. Linux는 EL1로 부팅했으므로 **virtual timer**(PPI 11 = INTID 27)를 쓴다(§3의 `arch_timer_set_next_event_virt`가 증거). 이 IRQ가 `/proc/interrupts`에 `arch_timer`로 보인다. MCU로 치면 DT의 이 노드가 "SysTick IRQ 번호와 우선순위"를 적어 둔 벡터 테이블 항목이다.

```text
           CPU0       CPU1       
 11:        777        719 GIC-0  27 Level     arch_timer
IPI0:        23         42       Rescheduling interrupts
IPI1:       265        328       Function call interrupts
...
IPI4:         0          0       Timer broadcast interrupts
```

(디버그 커널 부팅 3.7초 시점, 일부 IPI 줄은 `...`로 생략.) 왼쪽 `11`은 Linux 내부 IRQ 번호(virq), `GIC-0  27`이 하드웨어 INTID다. SysTick이 코어마다 있는 것처럼 **CPU마다 자기 타이머 인터럽트**를 받는다. `Timer broadcast interrupts`가 0인 것은, 깊은 idle에서 로컬 타이머가 꺼지는 SoC라면 다른 타이머가 대신 깨워 줘야 하는데(broadcast) QEMU에서는 그럴 일이 없기 때문이다.

---

## 3. clocksource와 clockevent — 하드웨어를 두 역할로 나눈다

### 3.1 정의

| | clocksource | clockevent device |
|---|---|---|
| 질문 | "지금 몇 시인가?" | "이 시각에 나를 깨워 줘" |
| 하드웨어 | free-running 카운터 (읽기 전용) | 비교기 + 인터럽트 |
| 개수 | 시스템에 여러 개 등록 가능, **하나를 골라** 씀 | **CPU마다** 하나씩 |
| arm64 이름 | `arch_sys_counter` (rating 400) | `arch_sys_timer` (rating 450) |
| 핵심 콜백 | `.read()` → 카운터 값 | `.set_next_event(delta)`, `.set_state_oneshot()` |
| 쓰는 쪽 | timekeeping (`ktime_get()`) | tick, hrtimer |
| MCU 비유 | `CYCCNT` | `TIMx->CCR1` + 인터럽트 |

`rating`은 "여러 후보 중 무엇을 고를까"의 점수다. x86이라면 TSC·HPET·acpi_pm이 경쟁하지만, arm64는 대개 generic timer 하나로 끝난다.

### 3.2 예제 3 — sysfs와 `/proc/timer_list`로 들여다보기

무엇을 확인하나: 현재 clocksource/clockevent가 무엇인지, clockevent가 어떤 모드로 무엇을 부르는지.

```sh
cat /sys/devices/system/clocksource/clocksource0/available_clocksource
cat /sys/devices/system/clocksource/clocksource0/current_clocksource
ls /sys/devices/system/clockevents/
cat /sys/devices/system/clockevents/clockevent0/current_device
cat /proc/timer_list
```

```text
arch_sys_counter 
arch_sys_counter
broadcast    clockevent0  clockevent1  power        uevent
arch_sys_timer
Timer List Version: v0.10
HRTIMER_MAX_CLOCK_BASES: 8
now at 3750957184 nsecs

cpu: 0
 clock 0:
  .base:       000000000d018b27
  .index:      0
  .resolution: 1 nsecs
  .offset:     0 nsecs
active timers:
 #0: <000000007c0dcb99>, tick_nohz_handler, S:01
 # expires at 3752000000-3752000000 nsecs [in 1042816 to 1042816 nsecs]
 #1: <00000000d511ab31>, pm_suspend_timer_fn, S:01
 # expires at 3926473168-4051473168 nsecs [in 175515984 to 300515984 nsecs]
 #2: <00000000759bcde0>, watchdog_timer_fn, S:01
 # expires at 4056000000-4056000000 nsecs [in 305042816 to 305042816 nsecs]
...
  .nohz           : 1
  .highres        : 1
  .tick_stopped   : 0
...
jiffies: 4294893233
...
Tick Device: mode:     1
Per CPU device: 0
Clock Event Device: arch_sys_timer
 max_delta_ns:   2199023255551
 min_delta_ns:   1000
 mult:           8388608
 shift:          27
 mode:           3
 next_event:     3764000000 nsecs
 set_next_event: arch_timer_set_next_event_virt
 shutdown:       arch_timer_shutdown_virt
 oneshot stopped: arch_timer_shutdown_virt
 event_handler:  hrtimer_interrupt
```

(디버그 커널, `-smp 2`. 244줄 중 핵심만 남기고 `...`로 생략. 포인터 값과 시각은 실행마다 다르다.)

출력에서 볼 것:

- **`tick_nohz_handler`가 hrtimer 목록에 있다.** tick은 특별한 하드웨어 인터럽트가 아니라 **4 ms마다 만료되는 hrtimer 하나**다(`expires at 3752000000` — 정확히 4 ms 배수). high-res 모드(`.highres: 1`)에서는 이렇게 동작한다.
- `pm_suspend_timer_fn`의 `expires at A-B`: hrtimer는 만료 시각이 **범위(soft~hard)**일 수 있다. 이 범위 안 아무 때나 울려도 된다는 뜻이고, 이게 `usleep_range()`의 정체다(§10.2).
- clockevent `event_handler: hrtimer_interrupt` — 비교기 인터럽트가 오면 hrtimer 코어로 바로 간다. `set_next_event: arch_timer_set_next_event_virt` — virtual 비교기(CNTV)에 다음 만료를 쓴다.
- `mult: 8388608`, `shift: 27`: ns를 카운터 tick으로 바꾸는 고정소수점 계수. `cycles = (ns × mult) >> shift` = ns × 8388608 / 2^27 = **ns / 16** → 1 tick = 16 ns = 62.5 MHz. 커널은 나눗셈 대신 곱셈+시프트를 쓴다(MCU에서 Don도 하던 최적화).
- `min_delta_ns: 1000`: 비교기에 "1 us보다 가까운 미래"는 프로그램하지 않는다.
- `jiffies: 4294893233` — 부팅 3.75초 만에 벌써 42억이다. 이유는 §5.

### 3.3 함정

- clocksource가 불안정하다고 판정되면(watchdog이 다른 clocksource와 비교) 커널이 **다른 clocksource로 바꾼다**. x86에서 `clocksource: Marking clocksource 'tsc' as unstable` 로그가 유명하다. 실제 보드에서 SoC 전용 타이머 드라이버를 쓴다면 `current_clocksource`를 한 번은 확인하자.
- 깊은 idle에서 CPU 로컬 타이머가 멈추는 SoC(`CLOCK_EVT_FEAT_C3STOP`)는 **broadcast clockevent**가 필요하다. 이게 DT에 빠지면 "idle에 들어간 CPU가 타이머를 놓치고 한참 뒤에 깬다"는 증상이 나온다.

---

## 4. tick, HZ, NO_HZ

### 4.1 tick이 하는 일

tick은 HZ번/초 울리는 주기 인터럽트(지금은 주기 hrtimer)다. 하는 일:

1. `jiffies_64++` (한 CPU만, `tick_do_timer_cpu` 담당)
2. 현재 태스크의 CPU 시간 계산, 스케줄러 틱(`sched_tick`) — 타임 슬라이스 소진 판정
3. timer wheel에 만료된 타이머가 있으면 `TIMER_SOFTIRQ`를 올림
4. RCU 진행, 로드 평균, 프로파일링 등

**HZ**는 tick 주파수다. Kconfig에서 100/250/300/1000 중 고른다. 높을수록 `timer_list`·`msleep`의 해상도가 좋아지지만 인터럽트 오버헤드가 늘고 idle 전력이 나빠진다. Ubuntu 호스트는 1000, 이 랩(arm64 defconfig)은 250이다.

### 4.2 NO_HZ — tick을 끄는 모드

| 설정 | 뜻 | 쓰는 곳 |
|---|---|---|
| `CONFIG_HZ_PERIODIC` | 항상 tick | 옛날 방식 |
| `CONFIG_NO_HZ_IDLE` | **idle CPU는 tick을 멈춘다** (다음 타이머까지 한 번에 잠) | 이 랩, 대부분의 임베디드 |
| `CONFIG_NO_HZ_FULL` | 태스크가 하나만 도는 CPU도 tick을 멈춘다 (부팅 인자 `nohz_full=` 필요) | 실시간·HPC 코어 격리 |

MCU 비유: tickless idle FreeRTOS(`configUSE_TICKLESS_IDLE`)와 같은 발상이다. 잘 때는 SysTick을 멈추고 "다음 할 일 시각"에 맞춰 저전력 타이머 하나만 건다.

### 4.3 예제 4 — idle일 때와 바쁠 때 tick 인터럽트 수

무엇을 확인하나: NO_HZ_IDLE에서 idle 2초와 busy 2초 동안 CPU별 `arch_timer` 인터럽트가 몇 번 오는지.

```sh
grep arch_timer /proc/interrupts; sleep 2; grep arch_timer /proc/interrupts
grep arch_timer /proc/interrupts; timeout 2 taskset 1 sh -c 'while :; do :; done'; grep arch_timer /proc/interrupts
```

```text
== NO_HZ_IDLE: arch_timer IRQs during 'sleep 2' (idle)
 11:       4052       2368 GIC-0  27 Level     arch_timer
 11:       4083       2405 GIC-0  27 Level     arch_timer
== busy CPU0 for 2s
 11:       4094       2415 GIC-0  27 Level     arch_timer
Terminated
 11:       4580       2467 GIC-0  27 Level     arch_timer
```

출력에서 볼 것:

- idle 2초: CPU0 **+31**, CPU1 **+37**. HZ=250이 계속 돌았다면 각각 +500이어야 한다. tick이 멈춰 있었다.
- CPU0을 2초 바쁘게 돌리면: CPU0 **+486** (≈ 250 × 2), CPU1은 +52. 바쁜 CPU에서는 tick이 다시 돈다.
- 실무 의미: idle이 긴 배터리 장비에서 전력이 크게 줄고, `/proc/interrupts`의 타이머 카운트로 "이 CPU가 정말 쉬고 있나"를 볼 수 있다.

VM 호스트(Ubuntu 7.0)는 `CONFIG_NO_HZ_FULL=y`로 빌드됐지만 실제로는 꺼져 있다.

```sh
cat /sys/devices/system/cpu/nohz_full; cat /proc/cmdline
```

```text

BOOT_IMAGE=/vmlinuz-7.0.0-38-generic root=UUID=e8c86924-872a-4501-b66c-34b21fe8c63f ro console=tty1 console=ttyAMA0
```

`nohz_full` 파일이 비어 있다 — **커널 설정은 "가능"일 뿐이고, 켜려면 `nohz_full=2-5` 같은 부팅 인자를 줘야 한다**. 로봇의 제어 루프 전용 코어를 만들 때 `isolcpus=`·`nohz_full=`·`rcu_nocbs=`를 함께 쓰는 이유가 이것이다(N1·N3에서 다시).

---

## 5. jiffies — tick 카운터와 wraparound

### 5.1 정의

`jiffies`는 부팅 이후 tick 수를 세는 전역 변수다(`unsigned long`). 64비트 진짜 값은 `jiffies_64`이고, 64비트 커널에서는 둘이 같은 메모리다(링커 스크립트에서 alias). 32비트 커널에서 `jiffies`는 하위 32비트이고, 64비트 값을 안전하게 읽으려면 `get_jiffies_64()`를 쓴다.

```c
// include/linux/jiffies.h (6.18)
#define INITIAL_JIFFIES ((unsigned long)(unsigned int) (-300*HZ))
```

**`INITIAL_JIFFIES`는 0이 아니다.** 부팅 5분 뒤에 32비트 값이 한 바퀴 돌도록(wrap) 일부러 −300×HZ에서 시작한다. wraparound를 잘못 다루는 코드를 **개발 중에 5분 만에** 터뜨리기 위한 장치다. 0에서 시작하면 HZ=250에서 첫 wrap이 약 199일 뒤라서 현장에서야 터진다.

### 5.2 예제 5 — 커널 안에서 jiffies·HZ·시계 값 찍기 (`f8_jiffies`)

무엇을 확인하나: HZ, INITIAL_JIFFIES, 현재 jiffies, `msecs_to_jiffies` 반올림, `time_after32`, 여러 ktime 시계, 카운터 주파수, 그리고 각 시계 읽기 비용.

```c
// SPDX-License-Identifier: GPL-2.0
/* f8_jiffies: jiffies/HZ 값, wraparound 비교, ktime_get 변종들 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/timekeeping.h>
#include <linux/sched/clock.h>
#include <asm/arch_timer.h>

#define COST(expr) ({ u64 _t0 = ktime_get_ns(); int _i;			\
	for (_i = 0; _i < 1000; _i++) (void)(expr);				\
	(ktime_get_ns() - _t0) / 1000; })

static int __init f8_jiffies_init(void)
{
	u32 before = 0xfffffff0u, after = before + 0x20;	/* 32비트 wrap 직후 */

	pr_info("HZ=%d TICK_NSEC=%lu INITIAL_JIFFIES=%lu (0x%lx)\n",
		HZ, TICK_NSEC, INITIAL_JIFFIES, INITIAL_JIFFIES);
	pr_info("jiffies=%lu jiffies_64=%llu since_boot=%lu jiffies = %u ms\n",
		jiffies, get_jiffies_64(), jiffies - INITIAL_JIFFIES,
		jiffies_to_msecs(jiffies - INITIAL_JIFFIES));
	pr_info("msecs_to_jiffies: 1ms->%lu 4ms->%lu 5ms->%lu 10ms->%lu\n",
		msecs_to_jiffies(1), msecs_to_jiffies(4), msecs_to_jiffies(5),
		msecs_to_jiffies(10));
	pr_info("wrap: after(0x%x) > before(0x%x)? naive=%d time_after32=%d\n",
		after, before, after > before, time_after32(after, before));
	pr_info("mono=%lld raw=%lld boot=%lld real=%lld coarse=%lld local_clock=%llu\n",
		ktime_get(), ktime_get_raw(), ktime_get_boottime(),
		ktime_get_real(), ktime_get_coarse(), local_clock());
	pr_info("arch_timer rate=%u Hz counter=%llu\n",
		arch_timer_get_cntfrq(), __arch_counter_get_cntvct());
	pr_info("cost(ns): ktime_get=%llu raw=%llu real=%llu coarse=%llu jiffies=%llu\n",
		COST(ktime_get()), COST(ktime_get_raw()), COST(ktime_get_real()),
		COST(ktime_get_coarse()), COST(READ_ONCE(jiffies)));
	return 0;
}

static void __exit f8_jiffies_exit(void) { }
module_init(f8_jiffies_init);
module_exit(f8_jiffies_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F8: jiffies, HZ and ktime variants");
```

(처음에는 카운터 주파수를 `arch_timer_get_rate()`로 읽으려 했는데 `ERROR: modpost: "arch_timer_get_rate" [f8_jiffies.ko] undefined!` — 모듈에 export되지 않은 함수다. `<asm/arch_timer.h>`의 인라인 `arch_timer_get_cntfrq()`로 바꿨다.)

테스트 스크립트는 `insmod /f8_jiffies.ko; sleep 1; dmesg | grep f8_jiffies; cat /proc/uptime`. 기본 커널, 실시간 TCG:

```text
lab initramfs: 6.18.55 up 2.14s
f8_jiffies: HZ=250 TICK_NSEC=4000000 INITIAL_JIFFIES=4294892296 (0xfffedb08)
f8_jiffies: jiffies=4294892873 jiffies_64=4294892873 since_boot=577 jiffies = 2308 ms
f8_jiffies: msecs_to_jiffies: 1ms->1 4ms->1 5ms->2 10ms->3
f8_jiffies: wrap: after(0x10) > before(0xfffffff0)? naive=0 time_after32=1
f8_jiffies: mono=2310413424 raw=2310441968 boot=2310495632 real=1791670938205157248 coarse=2305347520 local_clock=2466744592
f8_jiffies: arch_timer rate=62500000 Hz counter=165384118
f8_jiffies: cost(ns): ktime_get=269 raw=298 real=274 coarse=158 jiffies=54
3.44 3.48
```

출력에서 볼 것:

- `INITIAL_JIFFIES = 4294892296 = 0xFFFEDB08` = 2^32 − 75000 (= 300 s × 250). 부팅 2.3초에 jiffies는 `INITIAL_JIFFIES + 577`이다. `since_boot` = 577 jiffies = 2308 ms로 `mono=2.310 s`와 맞는다.
- 64비트 커널이라 `jiffies == jiffies_64`이고 **32비트 경계(4294967296)를 넘어도 wrap하지 않는다** — 75,000 tick(5분) 뒤에 그냥 4294967296 이상으로 올라간다. 그래도 `time_after()`를 써야 하는 이유: 같은 드라이버가 32비트 ARM(Cortex-A7 같은 SoC)에서도 빌드되고, 그때는 진짜로 5분 만에 wrap한다.
- **`msecs_to_jiffies`는 올림이다**: 1 ms → 1, 5 ms → 2, 10 ms → 3 jiffies(= 12 ms). 4 ms 미만은 표현할 수 없다.
- `time_after32(0x10, 0xfffffff0)` = 1(참), 단순 비교 `0x10 > 0xfffffff0` = 0(거짓). 다음 예제에서 이게 왜 중요한지 본다.
- `local_clock()`(스케줄러용 `sched_clock`)이 `ktime_get()`보다 0.15 s 크다 — 기준점이 다른 별개의 시계다. 비교에 섞어 쓰면 안 된다.
- 비용(실시간 TCG 수치라 절대값은 참고만): jiffies 읽기는 메모리 읽기 하나라 가장 싸고, coarse < mono ≈ raw ≈ real 순이다.

### 5.3 예제 6 — wraparound 버그 재현 (`f8_wrap`, 유저 공간 시뮬레이션)

무엇을 확인하나: 32비트 jiffies에서 부팅 299초에 건 2초 타임아웃이 단순 비교로는 즉시 만료되고, `time_after()` 원리로는 정확히 2초 뒤 만료되는지. (커널 매크로를 그대로 쓸 수 없으니 같은 원리를 `uint32_t`로 재현한다.)

```c
/* f8_wrap: 32비트 jiffies wraparound — 단순 비교 vs time_after */
#include <stdio.h>
#include <stdint.h>

#define HZ 250
#define INITIAL_JIFFIES ((uint32_t)(-300 * HZ))	/* 부팅 5분 뒤 wrap */
#define time_after(a, b) ((int32_t)((b) - (a)) < 0)	/* 커널 매크로와 같은 원리 */

int main(void)
{
	uint32_t jiffies = INITIAL_JIFFIES;
	uint32_t start_s[] = { 10, 299 };	/* 부팅 후 10초, 299초에 2초 타임아웃 */

	printf("INITIAL_JIFFIES = %u (0x%08x), wraps after %u s\n",
	       INITIAL_JIFFIES, INITIAL_JIFFIES, (uint32_t)(0u - INITIAL_JIFFIES) / HZ);
	for (int k = 0; k < 2; k++) {
		uint32_t now = jiffies + start_s[k] * HZ;
		uint32_t timeout = now + 2 * HZ;
		int naive_at = -1, safe_at = -1;

		for (uint32_t t = 0; t <= 3 * HZ; t++) {	/* 3초 동안 tick 진행 */
			uint32_t j = now + t;
			if (naive_at < 0 && j > timeout)
				naive_at = (int)t;
			if (safe_at < 0 && time_after(j, timeout))
				safe_at = (int)t;
		}
		printf("start=%3us now=0x%08x timeout=0x%08x -> naive expired after %4d ticks, "
		       "time_after after %4d ticks\n", start_s[k], now, timeout, naive_at, safe_at);
	}
	return 0;
}
```

```text
INITIAL_JIFFIES = 4294892296 (0xfffedb08), wraps after 300 s
start= 10s now=0xfffee4cc timeout=0xfffee6c0 -> naive expired after  501 ticks, time_after after  501 ticks
start=299s now=0xffffff06 timeout=0x000000fa -> naive expired after    0 ticks, time_after after  501 ticks
```

출력에서 볼 것: 부팅 10초에 건 타임아웃은 둘 다 501 tick(2초 + 1 tick, "넘었나"라서 +1) 뒤 만료. 하지만 **부팅 299초에 건 타임아웃은 `timeout`이 wrap해서 `0x000000fa`가 되고, 단순 비교는 0 tick 만에 "이미 지났다"**고 판단한다. 센서 드라이버라면 "부팅 5분쯤에 I2C 타임아웃 에러가 한 번 난다"는, 재현하기 끔찍한 버그가 된다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="190" y="20" text-anchor="middle" font-size="13">32비트 jiffies = 시계판 (비율 무시)</text>
  <circle cx="190" cy="185" r="115" fill="none" stroke="currentColor" stroke-width="2"/>
  <circle cx="190" cy="70" r="5" fill="#888"/>
  <text x="190" y="46" text-anchor="middle" font-size="12">0x00000000</text>
  <circle cx="111" cy="101" r="5" fill="#888"/>
  <text x="106" y="88" text-anchor="end" font-size="12">INITIAL_JIFFIES</text>
  <text x="106" y="103" text-anchor="end" font-size="12">0xFFFEDB08</text>
  <circle cx="151" cy="77" r="6" fill="#4a7bd0"/>
  <text x="142" y="68" text-anchor="end" font-size="12">now</text>
  <circle cx="229" cy="77" r="6" fill="#e08a3c"/>
  <text x="238" y="68" font-size="12">timeout</text>
  <path d="M157,70 Q190,58 223,70" fill="none" stroke="#3f9a6b" stroke-width="2" marker-end="url(#ah2)"/>
  <text x="190" y="110" text-anchor="middle" font-size="12">+500 tick (2 s)</text>
  <text x="190" y="180" text-anchor="middle" font-size="12">시계 방향 = 시간 진행</text>
  <text x="190" y="198" text-anchor="middle" font-size="12">한 바퀴 = 2^32 tick</text>
  <text x="190" y="216" text-anchor="middle" font-size="12">(HZ=250이면 약 199일)</text>
  <rect x="330" y="40" width="340" height="86" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="500" y="62" text-anchor="middle" font-size="13">단순 비교: jiffies &gt; timeout</text>
  <text x="500" y="84" text-anchor="middle" font-size="12">0xFFFFFF06 &gt; 0x000000FA → 참</text>
  <text x="500" y="106" text-anchor="middle" font-size="12">→ 기다리지 않고 즉시 만료 처리 (버그)</text>
  <rect x="330" y="140" width="340" height="86" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="500" y="162" text-anchor="middle" font-size="13">time_after(jiffies, timeout)</text>
  <text x="500" y="184" text-anchor="middle" font-size="12">(s32)(timeout - jiffies) = +500 &lt; 0 ? → 거짓</text>
  <text x="500" y="206" text-anchor="middle" font-size="12">→ 아직 안 됨 (정답)</text>
  <rect x="330" y="240" width="340" height="86" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="500" y="262" text-anchor="middle" font-size="13">규칙: 차이를 부호 있는 수로 본다</text>
  <text x="500" y="284" text-anchor="middle" font-size="12">두 시각 차이가 반 바퀴(2^31 tick) 미만이면</text>
  <text x="500" y="306" text-anchor="middle" font-size="12">wrap을 지나도 항상 맞다 (HZ=250: 약 99일)</text>
  <text x="340" y="350" text-anchor="middle" font-size="12">64비트 커널에서도 32비트 값(u32 타임스탬프, 레지스터 카운터)에는 같은 함정이 있다</text>
</svg>
```

그림 2 — jiffies를 시계판으로 보면 wraparound가 직관적이다. 단순 비교는 숫자 크기를 보고, `time_after`는 "시계 방향으로 반 바퀴 이내에 있나"를 본다.

### 5.4 jiffies 비교 매크로 정리

| 매크로 | 뜻 | 비고 |
|---|---|---|
| `time_after(a, b)` | a가 b보다 나중 | `(long)((b) - (a)) < 0` |
| `time_before(a, b)` | a가 b보다 먼저 | `time_after(b, a)` |
| `time_after_eq(a, b)` / `time_before_eq` | 같음 포함 | 타임아웃 루프에 흔히 씀 |
| `time_in_range(a, b, c)` | b ≤ a ≤ c | |
| `time_after32` / `time_before32` | `u32` 값용 | 하드웨어 32비트 타임스탬프 비교에도 쓸 수 있다 |
| `time_is_after_jiffies(a)` | a가 지금보다 나중 (아직 안 옴) | `time_before(jiffies, a)` |

전형적인 폴링 루프:

```c
unsigned long timeout = jiffies + msecs_to_jiffies(100);

while (!(readl(base + STATUS) & READY)) {
	if (time_after(jiffies, timeout))
		return -ETIMEDOUT;
	usleep_range(50, 100);
}
```

(위 코드는 패턴 설명용 조각이라 실행하지 않았다. 실제 드라이버라면 이 루프를 직접 쓰기보다 `readl_poll_timeout()`(`<linux/iopoll.h>`)을 쓴다 — 내부가 정확히 이 구조다. 단, 그 매크로는 jiffies가 아니라 `ktime`으로 타임아웃을 잰다.)

함정: `jiffies`는 `volatile`이 아니라 컴파일러가 루프 밖으로 끌어낼 수 있다고 걱정할 수 있는데, 커널은 `jiffies`를 `volatile`로 선언해 두었다(`include/linux/jiffies.h`). 대신 **`unsigned long`에 담아야 한다** — `int`나 `u32`에 담으면 64비트에서도 wrap 버그가 다시 생긴다.

---

## 6. `timer_list` — jiffies 단위의 싼 타이머

### 6.1 API (6.18 기준)

```c
#include <linux/timer.h>

struct timer_list t;
timer_setup(&t, my_fn, 0);                      /* 콜백 등록 (flags: 0, TIMER_DEFERRABLE, TIMER_PINNED, TIMER_IRQSAFE) */
mod_timer(&t, jiffies + msecs_to_jiffies(100)); /* (재)무장: 만료 시각은 '절대 jiffies' */
timer_delete(&t);                               /* 취소 (콜백이 다른 CPU에서 도는 중이면 기다리지 않음) */
timer_delete_sync(&t);                          /* 취소 + 실행 중인 콜백이 끝날 때까지 기다림 */
timer_shutdown_sync(&t);                        /* 취소 + 이후 mod_timer도 무시 (해제 직전에) */
```

(API 목록이라 실행 코드가 아니다. 실제로 돌린 것은 §6.3 예제 7.)

콜백 시그니처는 `void fn(struct timer_list *t)`이고, 타이머를 품은 구조체는 `timer_container_of()`로 찾는다(E5의 `container_of` 그대로).

- **콜백은 softirq(`TIMER_SOFTIRQ`) 컨텍스트**에서 돈다 → atomic. `mutex_lock`, `msleep`, I2C/SPI 전송, `GFP_KERNEL` 할당 **금지** (F2, E4 §5).
- `mod_timer()`는 이미 무장된 타이머의 만료 시각만 바꾼다. 콜백 안에서 자기 자신을 다시 무장하면 주기 타이머가 된다.
- `TIMER_DEFERRABLE`: idle CPU를 이 타이머 때문에 깨우지 않는다(다음에 깰 때 같이 처리). 배터리 장비의 "대충 1초마다 통계" 같은 용도.

### 6.2 6.18에서 바뀐 이름들 — 옛 튜토리얼을 그대로 따라 하면 빌드가 안 된다

| 옛 이름 (LDD3·블로그) | 6.18 이름 | 비고 |
|---|---|---|
| `init_timer()` + `.function`/`.data` | `timer_setup()` | 4.15에서 콜백 시그니처가 `unsigned long` → `struct timer_list *`로 바뀌며 교체 |
| `from_timer(var, t, field)` | `timer_container_of(var, t, field)` | 6.16에서 이름 변경 (E5에서 확인) |
| `del_timer()` / `del_timer_sync()` | `timer_delete()` / `timer_delete_sync()` | 새 이름이 6.2에 생겼고 옛 이름은 이후 제거됨 — 6.18에는 없다 (아래 예제 8) |
| (없음) | `timer_shutdown_sync()` | 6.2 도입. 해제 경로용 |
| `hrtimer_init()` + `timer.function = fn` | `hrtimer_setup(timer, fn, clock, mode)` | 6.13에 도입, `hrtimer_init()`은 6.15에서 제거 — 6.18에는 없다 |

"6.18에 없다"는 것은 아래 예제 8로 확인했다. `hrtimer_setup()`은 6.13에 들어왔고 `hrtimer_init()`은 6.15에서 제거됐다(외부 드라이버들이 6.15를 기준으로 분기하는 호환 코드로 확인). `from_timer` → `timer_container_of` 이름 변경은 6.16이다. `del_timer*`가 정확히 어느 릴리스에서 빠졌는지는 확인하지 못했으니 필요하면 `git log -S del_timer_sync -- include/linux/timer.h`로 확인한다.

### 6.3 예제 7 — 드라이버식 주기 폴링 타이머 (`f8_poll`)

무엇을 확인하나: 장치 구조체에 `timer_list`를 내장하고, 콜백에서 `timer_container_of`로 장치를 찾고, 100 ms마다 드리프트 없이 재무장하고, 모듈 해제 때 안전하게 멈추는 표준 패턴. 콜백이 정말 softirq에서 도는지도 찍는다.

```c
// SPDX-License-Identifier: GPL-2.0
/* f8_poll: 드라이버식 timer_list — 구조체에 내장, 100 ms 폴링, 안전한 해제 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/jiffies.h>

struct f8_dev {
	struct timer_list poll_timer;	/* 구조체 안에 내장 */
	unsigned long last;		/* 지난 콜백의 jiffies */
	int count;
};
static struct f8_dev *dev;

static void f8_poll_fn(struct timer_list *t)
{
	struct f8_dev *d = timer_container_of(d, t, poll_timer);	/* 6.16+ */
	unsigned long now = jiffies;

	pr_info("poll #%d jiffies=%lu (+%lu) in_softirq=%d\n", d->count, now,
		d->count ? now - d->last : 0, !!in_serving_softirq());
	d->count++;
	d->last = now;
	/* 기준을 '이전 만료'로 잡아 드리프트를 막는다 */
	mod_timer(&d->poll_timer, d->poll_timer.expires + msecs_to_jiffies(100));
}

static int __init f8_poll_init(void)
{
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	timer_setup(&dev->poll_timer, f8_poll_fn, 0);
	mod_timer(&dev->poll_timer, jiffies + msecs_to_jiffies(100));
	return 0;
}

static void __exit f8_poll_exit(void)
{
	/* 콜백이 스스로 재무장하므로 delete가 아니라 shutdown: 이후 재무장도 막는다 */
	timer_shutdown_sync(&dev->poll_timer);
	pr_info("stopped after %d polls\n", dev->count);
	kfree(dev);
}
module_init(f8_poll_init);
module_exit(f8_poll_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F8: periodic timer_list polling pattern");
```

테스트: `insmod /f8_poll.ko; sleep 1; rmmod f8_poll; dmesg | grep f8_poll`. 기본 커널:

```text
f8_poll: poll #0 jiffies=4294892632 (+0) in_softirq=1
f8_poll: poll #1 jiffies=4294892657 (+25) in_softirq=1
f8_poll: poll #2 jiffies=4294892682 (+25) in_softirq=1
f8_poll: poll #3 jiffies=4294892707 (+25) in_softirq=1
f8_poll: poll #4 jiffies=4294892732 (+25) in_softirq=1
f8_poll: poll #5 jiffies=4294892757 (+25) in_softirq=1
f8_poll: poll #6 jiffies=4294892782 (+25) in_softirq=1
f8_poll: poll #7 jiffies=4294892807 (+25) in_softirq=1
f8_poll: poll #8 jiffies=4294892832 (+25) in_softirq=1
f8_poll: poll #9 jiffies=4294892857 (+25) in_softirq=1
f8_poll: stopped after 10 polls
```

출력에서 볼 것:

- 간격이 정확히 **25 jiffies = 100 ms**. 콜백이 조금 늦게 돌아도 다음 만료를 `expires + 25`로 잡았기 때문에 오차가 쌓이지 않는다. `jiffies + 25`로 잡았다면 늦은 만큼 매번 밀린다(§9에서 hrtimer로 측정).
- `in_softirq=1` — 콜백은 softirq 컨텍스트다.
- `rmmod`가 깔끔하다. 처음 버전은 `pr_info(..., d->count++, ..., d->count > 1 ? ...)`로 썼다가 gcc가 `warning: operation on 'd->count' may be undefined [-Wsequence-point]`를 냈다 — 인자 평가 순서는 정해져 있지 않다. 위 코드는 고친 버전이다.

**왜 `timer_shutdown_sync`인가**: 콜백이 스스로 `mod_timer`를 하는 타이머는 `timer_delete_sync`로 지운 직후에도 "마침 실행 중이던 콜백"이 다시 무장할 수 있다는 고전적인 경쟁이 있다. `timer_delete_sync`는 실행 중인 콜백이 끝나길 기다려 주므로 이 경우도 처리하지만(콜백이 끝난 뒤 다시 지운다), 다른 경로(예: IRQ 핸들러)가 `mod_timer`를 부르는 드라이버라면 지운 뒤에도 다시 살아날 수 있다. `timer_shutdown_sync`는 타이머를 "종료됨"으로 표시해서 **이후의 모든 `mod_timer`를 조용히 무시**한다. 해제(`kfree`) 직전에는 이걸 쓴다.

### 6.4 예제 8 — 옛 API로 빌드하면

무엇을 확인하나: 인터넷 튜토리얼에 흔한 `from_timer`·`hrtimer_init`·`del_timer_sync`가 6.18에서 정말 없는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* f8_old: 옛 튜토리얼 API를 6.18에서 빌드하면? */
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>

struct foo { struct timer_list t; struct hrtimer h; };
static struct foo f;

static void old_fn(struct timer_list *t)
{
	struct foo *p = from_timer(p, t, t);		/* 6.16에서 이름 변경 */

	(void)p;
}

static int __init f8_old_init(void)
{
	timer_setup(&f.t, old_fn, 0);
	hrtimer_init(&f.h, CLOCK_MONOTONIC, HRTIMER_MODE_REL);	/* 제거됨 */
	del_timer_sync(&f.t);					/* 제거됨 */
	return 0;
}
module_init(f8_old_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F8: removed timer APIs");
```

```text
f8_old.c:12:25: error: implicit declaration of function ‘from_timer’; did you mean ‘mod_timer’? [-Wimplicit-function-declaration]
f8_old.c:12:25: error: initialization of ‘struct foo *’ from ‘int’ makes pointer from integer without a cast [-Wint-conversion]
f8_old.c:20:9: error: implicit declaration of function ‘hrtimer_init’; did you mean ‘hrtimers_init’? [-Wimplicit-function-declaration]
f8_old.c:21:9: error: implicit declaration of function ‘del_timer_sync’ [-Wimplicit-function-declaration]
make[3]: *** [/var/lab/src/linux-6.18.55/scripts/Makefile.build:287: f8_old.o] Error 1
```

출력에서 볼 것: 세 함수 모두 **선언 자체가 없다**. E4에서 본 `-Werror=implicit-function-declaration` 덕분에 링크까지 가지 않고 컴파일에서 멈춘다. `hrtimers_init`을 권하는 gcc 제안은 엉뚱하다(부팅 때 한 번 도는 내부 초기화 함수). 다른 커널 버전용 드라이버를 포팅할 때는 `include/linux/timer.h`·`hrtimer.h`를 먼저 열어 보자.

### 6.5 timer wheel — 왜 "대충"인가

`timer_list`는 **timer wheel**에 들어간다. 바퀴는 레벨 9개(HZ>100일 때), 레벨마다 버킷 64개다. 만료까지 남은 시간이 길수록 높은 레벨에 들어가고, 레벨이 오를 때마다 간격이 8배 커진다. `kernel/time/timer.c` 주석의 HZ=250 표:

```text
 * HZ  250
 * Level Offset  Granularity            Range
 *  0	   0         4 ms                0 ms -        255 ms
 *  1	  64        32 ms              256 ms -       2047 ms (256ms - ~2s)
 *  2	 128       256 ms             2048 ms -      16383 ms (~2s - ~16s)
 *  3	 192      2048 ms (~2s)      16384 ms -     131071 ms (~16s - ~2m)
 *  4	 256     16384 ms (~16s)    131072 ms -    1048575 ms (~2m - ~17m)
 *  5	 320    131072 ms (~2m)    1048576 ms -    8388607 ms (~17m - ~2h)
 *  6	 384   1048576 ms (~17m)   8388608 ms -   67108863 ms (~2h - ~18h)
 *  7	 448   8388608 ms (~2h)   67108864 ms -  536870911 ms (~18h - ~6d)
 *  8    512  67108864 ms (~18h) 536870912 ms - 4294967288 ms (~6d - ~49d)
```

(소스 주석 그대로. `sed -n 128,139p kernel/time/timer.c`.) 설계 의도도 같은 주석에 있다: "대부분의 타임아웃(네트워크, 디스크 I/O)은 **만료 전에 취소된다**. 만료됐다면 이미 정상 동작이 깨진 것이니 조금 늦어도 상관없다." 그래서 wheel은 **넣기·빼기가 O(1)**이고(해시 버킷), 대신 정밀도를 포기한다. 예를 들어 5초 타임아웃은 레벨 2에 들어가서 256 ms 단위로 반올림된다(최대 12.5% 늦음).

**절대 일찍 울리지 않게** 하는 규칙이 하나 더 있다.

```c
// kernel/time/timer.c (6.18)
static inline unsigned calc_index(unsigned long expires, unsigned lvl,
				  unsigned long *bucket_expiry)
{
	/*
	 * The timer wheel has to guarantee that a timer does not fire
	 * early. Early expiry can happen due to:
	 * - Timer is armed at the edge of a tick
	 * - Truncation of the expiry time in the outer wheel levels
	 *
	 * Round up with level granularity to prevent this.
	 */
	expires = (expires >> LVL_SHIFT(lvl)) + 1;
	*bucket_expiry = expires << LVL_SHIFT(lvl);
	return LVL_OFFS(lvl) + (expires & LVL_MASK);
}
```

레벨 0에서도 `+1`이다. 즉 `mod_timer(t, jiffies + 1)`의 실제 버킷은 `jiffies + 2`다. 이유: 지금이 tick J의 **끝자락**(J+1 직전)일 수 있다. J+1 tick에 울리면 경과 시간이 0에 가까울 수 있으니, 한 칸 더 올려서 "최소한 요청한 tick 수만큼 온전히" 지나게 한다.

```svg
<svg viewBox="0 0 680 412" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="13">같은 "1 ms 뒤에 불러 줘" — HZ=250에서 두 타이머가 실제로 울리는 시각</text>
  <line x1="60" y1="120" x2="650" y2="120" stroke="currentColor" stroke-width="2" marker-end="url(#ah3)"/>
  <line x1="60" y1="110" x2="60" y2="130" stroke="currentColor" stroke-width="2"/>
  <line x1="253" y1="110" x2="253" y2="130" stroke="currentColor" stroke-width="2"/>
  <line x1="447" y1="110" x2="447" y2="130" stroke="currentColor" stroke-width="2"/>
  <line x1="640" y1="110" x2="640" y2="130" stroke="currentColor" stroke-width="2"/>
  <text x="60" y="146" text-anchor="middle" font-size="12">0 ms (J)</text>
  <text x="253" y="146" text-anchor="middle" font-size="12">4 ms (J+1)</text>
  <text x="447" y="146" text-anchor="middle" font-size="12">8 ms (J+2)</text>
  <text x="625" y="146" text-anchor="middle" font-size="12">12 ms (J+3)</text>
  <line x1="123" y1="60" x2="123" y2="114" stroke="#888" stroke-dasharray="3 3"/>
  <text x="118" y="52" font-size="12">t=1.3 ms: mod_timer(jiffies+1) / hrtimer_start(1 ms)</text>
  <circle cx="171" cy="120" r="7" fill="#4a7bd0"/>
  <text x="140" y="168" font-size="12">↑ hrtimer 콜백 2.3 ms</text>
  <line x1="253" y1="70" x2="253" y2="110" stroke="#e08a3c" stroke-dasharray="4 3"/>
  <text x="258" y="82" font-size="12">expires = J+1 (여기서 울리면</text>
  <text x="258" y="97" font-size="12">요청보다 빠를 수 있다)</text>
  <circle cx="447" cy="120" r="7" fill="#e08a3c"/>
  <text x="455" y="100" font-size="12">timer_list 콜백 8 ms</text>
  <line x1="123" y1="204" x2="445" y2="204" stroke="#e08a3c" marker-end="url(#ah3)"/>
  <text x="284" y="222" text-anchor="middle" font-size="12">6.7 ms 경과 — 무장 위치에 따라 4~8 ms</text>
  <line x1="123" y1="196" x2="169" y2="196" stroke="#4a7bd0" marker-end="url(#ah3)"/>
  <rect x="20" y="248" width="190" height="66" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="115" y="270" text-anchor="middle" font-size="12">calc_index(): 버킷 = J+2</text>
  <text x="115" y="288" text-anchor="middle" font-size="12">레벨 간격만큼 올림 →</text>
  <text x="115" y="306" text-anchor="middle" font-size="12">절대 일찍 울리지 않는다</text>
  <rect x="222" y="248" width="250" height="66" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="347" y="270" text-anchor="middle" font-size="12">wheel 레벨 (HZ=250)</text>
  <text x="347" y="288" text-anchor="middle" font-size="12">L0 4 ms 간격: ~255 ms</text>
  <text x="347" y="306" text-anchor="middle" font-size="12">L1 32 ms: ~2 s · L2 256 ms: ~16 s</text>
  <rect x="484" y="248" width="180" height="66" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="574" y="270" text-anchor="middle" font-size="12">hrtimer: rbtree에 ns로</text>
  <text x="574" y="288" text-anchor="middle" font-size="12">가장 이른 만료를 바로</text>
  <text x="574" y="306" text-anchor="middle" font-size="12">clockevent에 프로그램</text>
  <text x="340" y="346" text-anchor="middle" font-size="12">timer_list = "대략 이쯤, 늦어도 괜찮은" 타임아웃용 (취소가 대부분, 싸다)</text>
  <text x="340" y="368" text-anchor="middle" font-size="12">hrtimer = "정확히 그때" 용 (주기 샘플링, nanosleep) — 대신 비싸다</text>
  <text x="340" y="396" text-anchor="middle" font-size="12">측정: 1 ms 요청 → timer_list 4.05~7.97 ms, hrtimer 1.008 ms (§7.4, icount)</text>
</svg>
```

그림 3 — HZ=250에서 "1 ms 뒤"를 두 타이머로 걸었을 때. `timer_list`는 J+2 tick까지 기다리므로 무장 위치에 따라 4~8 ms, hrtimer는 1 ms 뒤 바로 울린다.

그래서 `timer_list`로 "P ms 뒤"를 걸면, `J = msecs_to_jiffies(P)`(올림)라 할 때 실제 경과는 **(J × 4 ms, (J+1) × 4 ms]** 구간이다(+ softirq 처리 시간).

| 요청 | `msecs_to_jiffies` | 버킷 | 예상 경과 |
|---|---|---|---|
| 1 ms | 1 | J+2 | 4 ~ 8 ms |
| 2 ms | 1 | J+2 | 4 ~ 8 ms |
| 5 ms | 2 | J+3 | 8 ~ 12 ms |
| 10 ms | 3 | J+4 | 12 ~ 16 ms |

§7.4에서 실제로 잰다.

---

## 7. hrtimer — 나노초 단위의 정확한 타이머

### 7.1 직관과 정의

hrtimer(high-resolution timer)는 만료 시각을 **`ktime_t`(s64 나노초)**로 들고, CPU별 **red-black tree**에 정렬해 둔다. 트리의 가장 왼쪽(가장 이른 만료)을 clockevent 비교기에 바로 프로그램한다. 인터럽트가 오면 만료된 것들을 실행하고, 다음으로 이른 만료를 다시 프로그램한다. MCU에서 "하드웨어 타이머 하나로 여러 소프트웨어 타이머를 정확히 돌리기 위해 정렬 리스트를 만들고 맨 앞 것을 CCR에 쓰던" 구조와 같다.

- 넣기/빼기 O(log n) — wheel보다 비싸다. 그래서 "대부분 취소될 타임아웃"이 아니라 **정말 그 시각에 일어나야 하는 일**에 쓴다: `nanosleep`, `usleep_range`, POSIX 타이머, 스케줄러 hrtick, tick 자체, 오디오/센서 주기 샘플링, 네트워크 패킷 페이싱.
- `CONFIG_HIGH_RES_TIMERS=y`이고 clockevent가 one-shot 모드를 지원해야 high-res 모드가 켜진다(`/proc/timer_list`의 `.highres: 1`).

### 7.2 API (6.18)

```c
#include <linux/hrtimer.h>

static struct hrtimer h;
static enum hrtimer_restart my_fn(struct hrtimer *t)
{
	/* atomic 컨텍스트 (HARD면 hardirq, SOFT면 softirq) */
	hrtimer_forward_now(t, ms_to_ktime(1));    /* 주기면 다음 만료를 계산 */
	return HRTIMER_RESTART;                    /* 또는 HRTIMER_NORESTART */
}

hrtimer_setup(&h, my_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL_HARD);
hrtimer_start(&h, ms_to_ktime(1), HRTIMER_MODE_REL_HARD);
hrtimer_cancel(&h);    /* 취소 + 실행 중인 콜백 대기 (= timer_delete_sync) */
```

(API 요약 조각. 실제로 돌린 코드는 예제 9·11.)

모드 플래그(`include/linux/hrtimer.h`에서 확인):

| 플래그 | 뜻 |
|---|---|
| `HRTIMER_MODE_REL` | 시각을 "지금부터 얼마 뒤"로 준다 |
| `HRTIMER_MODE_ABS` | 시각을 절대값(그 clock 기준 ns)으로 준다 — 주기 샘플링에 유리 |
| `HRTIMER_MODE_PINNED` | 무장한 CPU에 고정 (다른 CPU로 옮기지 않음) |
| `HRTIMER_MODE_SOFT` | 콜백을 softirq(`HRTIMER_SOFTIRQ`)에서 실행 |
| `HRTIMER_MODE_HARD` | 콜백을 hardirq에서 실행 — **PREEMPT_RT에서도** hardirq |
| 조합 | `HRTIMER_MODE_REL_HARD`, `HRTIMER_MODE_ABS_SOFT`, `HRTIMER_MODE_REL_PINNED_SOFT` … |

`_SOFT`/`_HARD`를 안 쓰면: 일반 커널에서는 hardirq에서 돌고, PREEMPT_RT 커널에서는 softirq 쪽으로 옮겨진다. 그래서 **"정말 hardirq여야 하는가"를 명시하는 습관**이 좋다 — 콜백이 RT 커널의 sleeping spinlock을 잡는다면 `_HARD`면 안 되고, 반대로 지터가 중요하면 `_HARD`를 쓴다.

clock은 `CLOCK_MONOTONIC`(기본), `CLOCK_BOOTTIME`(suspend 시간 포함), `CLOCK_REALTIME`(벽시계, 바뀔 수 있음), `CLOCK_TAI` 중 고른다.

### 7.3 예제 9 — 요청 대비 실제 만료 측정 모듈 (`f8_lat`)

무엇을 확인하나: 1/2/5/10 ms를 `timer_list`(`mod_timer`)와 hrtimer(`HRTIMER_MODE_REL_HARD`)로 각 200회 걸고, "무장 직전 `ktime_get()`"부터 "콜백 안 `ktime_get()`"까지를 us로 잰다. tick 경계에 대한 무장 위치가 매번 같으면 결과가 한쪽으로 치우치므로, 무장 전에 0~4 ms 무작위로 잠들어 **tick 위상을 섞는다**.

```c
// SPDX-License-Identifier: GPL-2.0
/* f8_lat: 요청한 시간 vs 실제 만료 — timer_list와 hrtimer 비교 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/random.h>

static int n = 200;
module_param(n, int, 0444);
static const unsigned int periods[] = { 1, 2, 5, 10 };	/* ms */
static struct timer_list tl;
static struct hrtimer hrt;
static DECLARE_COMPLETION(done);
static ktime_t t_fire;

static void f8_tl_fn(struct timer_list *t)
{
	t_fire = ktime_get();
	complete(&done);
}

static enum hrtimer_restart f8_hrt_fn(struct hrtimer *h)
{
	t_fire = ktime_get();
	complete(&done);
	return HRTIMER_NORESTART;
}

static s64 one_shot(int kind, unsigned int ms)
{
	unsigned int ph = get_random_u32_below(4000);	/* tick 위상 무작위화 */
	ktime_t t0;

	usleep_range(ph, ph + 10);
	reinit_completion(&done);
	t0 = ktime_get();
	if (kind == 0)
		mod_timer(&tl, jiffies + msecs_to_jiffies(ms));
	else
		hrtimer_start(&hrt, ms_to_ktime(ms), HRTIMER_MODE_REL_HARD);
	wait_for_completion(&done);
	return ktime_us_delta(t_fire, t0);
}
```

(이어서 init/exit — 한 파일이지만 60줄 규칙 때문에 둘로 나눠 보였다.)

```c
static int __init f8_lat_init(void)
{
	int k, p, i;
	char line[160];

	timer_setup(&tl, f8_tl_fn, 0);
	hrtimer_setup(&hrt, f8_hrt_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL_HARD);
	for (k = 0; k < 2; k++)
		for (p = 0; p < ARRAY_SIZE(periods); p++)
			for (i = 0; i < n; i++) {
				int len = (i % 16) ? strlen(line) : 0;

				snprintf(line + len, sizeof(line) - len, " %lld",
					 one_shot(k, periods[p]));
				if (i % 16 == 15 || i == n - 1)
					pr_info("%s P=%u:%s\n", k ? "hrt" : "tl", periods[p], line);
			}
	return 0;
}

static void __exit f8_lat_exit(void)
{
	timer_delete_sync(&tl);
	hrtimer_cancel(&hrt);
}
module_init(f8_lat_init);
module_exit(f8_lat_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F8: timer_list vs hrtimer expiry latency");
```

`complete()`는 atomic 컨텍스트에서 불러도 되는 깨우기 함수다(F3). 측정값은 콜백에서 찍었으므로 **"타이머가 울린 시각"**이고, 그 뒤 프로세스가 깨어나 스케줄되는 지연은 포함하지 않는다.

### 7.4 결과 — tick 양자화가 그대로 보인다

테스트 스크립트는 `insmod /f8_lat.ko n=200; dmesg | grep f8_lat`. 먼저 **icount sleep=off**(메커니즘만 남긴 모드)의 원본 출력 일부:

```text
lab initramfs: 6.18.55 up 2.33s
f8_lat: loading out-of-tree module taints kernel.
f8_lat: tl P=1: 5323 5976 5848 7263 5968 6213 5105 7854 6391 6137 7584 4620 6874 5927 6062 7917
f8_lat: tl P=1: 5151 7426 5032 6199 7268 5311 4892 5643 5574 4048 6287 6875 5983 7342 4909 5175
...
f8_lat: tl P=10: 14315 13334 12797 13694 12712 14315 15868 14785 15958 15123 13949 15117 13851 14369 13836 13783
...
f8_lat: hrt P=1: 1027 1008 1008 1008 1008 1008 1008 1008 1008 1008 1008 1008 1008 1008 1008 1008
...
f8_lat: hrt P=10: 10008 10014 10014 10014 10014 10014 10014 10014 10014 10014 10014 10014 10014 10014 10014 10014
```

(104줄 중 일부, 단위 us.) 이 원본을 Mac에서 작은 Python 스크립트(`stats.py`: 줄을 파싱해 정렬 후 분위수 계산)로 집계했다.

```sh
python3 stats.py res/nd_ns_lat.txt res/nd_rt_lat.txt
```

```text
res/nd_ns_lat.txt
  tl            P= 1 n=200 min=4048 p25=5028 p50=5968 p75=7114 p95=7866 max=7966
  tl            P= 2 n=200 min=4038 p25=4985 p50=5890 p75=6784 p95=7803 max=7957
  tl            P= 5 n=200 min=8087 p25=9047 p50=10100 p75=11170 p95=11889 max=11967
  tl            P=10 n=200 min=12079 p25=13150 p50=13932 p75=15117 p95=15868 max=15962
  hrt           P= 1 n=200 min=1008 p25=1008 p50=1008 p75=1008 p95=1008 max=1054
  hrt           P= 2 n=200 min=2006 p25=2008 p50=2008 p75=2008 p95=2008 max=2023
  hrt           P= 5 n=200 min=5008 p25=5008 p50=5008 p75=5008 p95=5014 max=5420
  hrt           P=10 n=200 min=10008 p25=10013 p50=10014 p75=10014 p95=10014 max=10015
res/nd_rt_lat.txt
  tl            P= 1 n=200 min=4093 p25=5569 p50=6684 p75=7706 p95=9594 max=47228
  tl            P= 2 n=200 min=4154 p25=5668 p50=6806 p75=7806 p95=10708 max=50443
  tl            P= 5 n=200 min=6993 p25=9852 p50=10850 p75=11740 p95=13497 max=22501
  tl            P=10 n=200 min=12126 p25=13780 p50=14729 p75=15665 p95=16760 max=19291
  hrt           P= 1 n=200 min=1033 p25=1053 p50=1078 p75=1202 p95=2693 max=21898
  hrt           P= 2 n=200 min=2015 p25=2061 p50=2082 p75=2124 p95=4164 max=6245
  hrt           P= 5 n=200 min=5040 p25=5082 p50=5124 p75=5497 p95=8710 max=32483
  hrt           P=10 n=200 min=10065 p25=10103 p50=10149 p75=10636 p95=12873 max=29878
```

```svg
<svg viewBox="0 0 640 340" xmlns="http://www.w3.org/2000/svg">
<text x="320.0" y="20" text-anchor="middle" font-size="13">요청 시간 대비 실제 만료 (QEMU icount sleep=off, 각 200회)</text>
<line x1="60" y1="290.0" x2="620" y2="290.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="294.0" text-anchor="end" font-size="12">0</text>
<line x1="60" y1="260.6" x2="620" y2="260.6" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="264.6" text-anchor="end" font-size="12">2</text>
<line x1="60" y1="231.2" x2="620" y2="231.2" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="235.2" text-anchor="end" font-size="12">4</text>
<line x1="60" y1="201.8" x2="620" y2="201.8" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="205.8" text-anchor="end" font-size="12">6</text>
<line x1="60" y1="172.4" x2="620" y2="172.4" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="176.4" text-anchor="end" font-size="12">8</text>
<line x1="60" y1="142.9" x2="620" y2="142.9" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="146.9" text-anchor="end" font-size="12">10</text>
<line x1="60" y1="113.5" x2="620" y2="113.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="117.5" text-anchor="end" font-size="12">12</text>
<line x1="60" y1="84.1" x2="620" y2="84.1" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="88.1" text-anchor="end" font-size="12">14</text>
<line x1="60" y1="54.7" x2="620" y2="54.7" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="58.7" text-anchor="end" font-size="12">16</text>
<line x1="60" y1="40" x2="60" y2="290" stroke="currentColor"/><line x1="60" y1="290" x2="620" y2="290" stroke="currentColor"/>
<text x="16" y="165.0" font-size="12" text-anchor="middle" transform="rotate(-90 16 165.0)">실제 경과 (ms)</text>
<text x="130.0" y="310" text-anchor="middle" font-size="13">요청 1 ms</text>
<line x1="71.2" y1="275.3" x2="188.8" y2="275.3" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<line x1="102.0" y1="230.5" x2="102.0" y2="172.9" stroke="#e08a3c"/>
<line x1="92.9" y1="230.5" x2="111.1" y2="230.5" stroke="#e08a3c"/>
<line x1="92.9" y1="172.9" x2="111.1" y2="172.9" stroke="#e08a3c"/>
<rect x="83.8" y="185.4" width="36.4" height="30.7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<line x1="83.8" y1="202.2" x2="120.2" y2="202.2" stroke="#e08a3c" stroke-width="2"/>
<line x1="158.0" y1="275.2" x2="158.0" y2="274.5" stroke="#4a7bd0"/>
<line x1="148.9" y1="275.2" x2="167.1" y2="275.2" stroke="#4a7bd0"/>
<line x1="148.9" y1="274.5" x2="167.1" y2="274.5" stroke="#4a7bd0"/>
<rect x="139.8" y="275.2" width="36.4" height="2.0" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<line x1="139.8" y1="275.2" x2="176.2" y2="275.2" stroke="#4a7bd0" stroke-width="2"/>
<text x="270.0" y="310" text-anchor="middle" font-size="13">요청 2 ms</text>
<line x1="211.2" y1="260.6" x2="328.8" y2="260.6" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<line x1="242.0" y1="230.6" x2="242.0" y2="173.0" stroke="#e08a3c"/>
<line x1="232.9" y1="230.6" x2="251.1" y2="230.6" stroke="#e08a3c"/>
<line x1="232.9" y1="173.0" x2="251.1" y2="173.0" stroke="#e08a3c"/>
<rect x="223.8" y="190.2" width="36.4" height="26.5" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<line x1="223.8" y1="203.4" x2="260.2" y2="203.4" stroke="#e08a3c" stroke-width="2"/>
<line x1="298.0" y1="260.5" x2="298.0" y2="260.2" stroke="#4a7bd0"/>
<line x1="288.9" y1="260.5" x2="307.1" y2="260.5" stroke="#4a7bd0"/>
<line x1="288.9" y1="260.2" x2="307.1" y2="260.2" stroke="#4a7bd0"/>
<rect x="279.8" y="260.5" width="36.4" height="2.0" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<line x1="279.8" y1="260.5" x2="316.2" y2="260.5" stroke="#4a7bd0" stroke-width="2"/>
<text x="410.0" y="310" text-anchor="middle" font-size="13">요청 5 ms</text>
<line x1="351.2" y1="216.5" x2="468.8" y2="216.5" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<line x1="382.0" y1="171.1" x2="382.0" y2="114.0" stroke="#e08a3c"/>
<line x1="372.9" y1="171.1" x2="391.1" y2="171.1" stroke="#e08a3c"/>
<line x1="372.9" y1="114.0" x2="391.1" y2="114.0" stroke="#e08a3c"/>
<rect x="363.8" y="125.7" width="36.4" height="31.2" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<line x1="363.8" y1="141.5" x2="400.2" y2="141.5" stroke="#e08a3c" stroke-width="2"/>
<line x1="438.0" y1="216.4" x2="438.0" y2="210.3" stroke="#4a7bd0"/>
<line x1="428.9" y1="216.4" x2="447.1" y2="216.4" stroke="#4a7bd0"/>
<line x1="428.9" y1="210.3" x2="447.1" y2="210.3" stroke="#4a7bd0"/>
<rect x="419.8" y="216.4" width="36.4" height="2.0" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<line x1="419.8" y1="216.4" x2="456.2" y2="216.4" stroke="#4a7bd0" stroke-width="2"/>
<text x="550.0" y="310" text-anchor="middle" font-size="13">요청 10 ms</text>
<line x1="491.2" y1="142.9" x2="608.8" y2="142.9" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<line x1="522.0" y1="112.4" x2="522.0" y2="55.3" stroke="#e08a3c"/>
<line x1="512.9" y1="112.4" x2="531.1" y2="112.4" stroke="#e08a3c"/>
<line x1="512.9" y1="55.3" x2="531.1" y2="55.3" stroke="#e08a3c"/>
<rect x="503.8" y="67.7" width="36.4" height="28.9" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<line x1="503.8" y1="85.1" x2="540.2" y2="85.1" stroke="#e08a3c" stroke-width="2"/>
<line x1="578.0" y1="142.8" x2="578.0" y2="142.7" stroke="#4a7bd0"/>
<line x1="568.9" y1="142.8" x2="587.1" y2="142.8" stroke="#4a7bd0"/>
<line x1="568.9" y1="142.7" x2="587.1" y2="142.7" stroke="#4a7bd0"/>
<rect x="559.8" y="142.7" width="36.4" height="2.0" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<line x1="559.8" y1="142.7" x2="596.2" y2="142.7" stroke="#4a7bd0" stroke-width="2"/>
<rect x="70" y="42" width="14" height="10" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/><text x="90" y="51" font-size="12">timer_list (jiffies)</text>
<rect x="230" y="42" width="14" height="10" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/><text x="250" y="51" font-size="12">hrtimer (REL_HARD)</text>
<line x1="390" y1="47" x2="410" y2="47" stroke="#3f9a6b" stroke-dasharray="4 3"/><text x="416" y="51" font-size="12">요청값</text>
<text x="320.0" y="332" text-anchor="middle" font-size="12">상자 = p25~p75, 굵은 선 = 중앙값, 수염 = 최소~최대 · HZ=250 (tick 4 ms)</text>
</svg>
```

그림 4 — icount sleep=off에서 잰 실제 만료 시각(각 200회). `timer_list`는 4 ms 폭의 구간 안에 고르게 퍼지고, hrtimer는 요청값에 거의 붙어 있다.

출력에서 볼 것:

- **`timer_list`는 §6.5 표의 구간에 정확히 들어간다.** 1 ms·2 ms 요청: 4.05~7.97 ms. 5 ms: 8.09~11.97 ms. 10 ms: 12.08~15.96 ms. 최소값이 구간 하한(4/8/12 ms)보다 조금 큰 건 "절대 일찍 울리지 않는다"(`calc_index`의 `+1`), 최대값이 상한보다 조금 작은 건 tick 직후에 무장한 경우다. 분포가 구간 안에 고르게(uniform) 퍼진 건 무장 위상을 무작위로 섞었기 때문이다.
- **1 ms와 2 ms가 같은 분포다** — 둘 다 1 jiffy로 올림되기 때문이다. HZ=250 커널에서 `timer_list`로는 4 ms보다 짧은 간격을 구분할 수 없다.
- **hrtimer는 요청값 + 8 us**(1008, 2008, 5008, 10014). 이 8 us는 125 MIPS 가상 CPU가 인터럽트 진입부터 콜백까지 약 1000개 명령어를 실행한 시간이다 — **에뮬레이터 수치**이고 실제 코어는 다르다. 가끔 있는 1054, 5420 같은 값은 그 순간 같은 CPU에서 tick 처리 같은 다른 일이 겹친 경우다.
- **실시간 TCG(아래 블록)는 같은 구조 위에 잡음이 얹혀 있다.** hrtimer p50은 1.078 ms로 여전히 정확하지만 최대 21.9 ms 같은 꼬리가 생긴다 — 호스트 부하로 vCPU 스레드가 멈춘 시간이다. 그리고 `tl P=5`의 최소값 6.99 ms는 **구간 하한 8 ms보다 작다**. 다음 절에서 이유를 본다.

#### 실시간 TCG에서 timer_list가 "일찍" 울린 이유 — 에뮬레이터 아티팩트

디버그 커널 + 실시간 TCG로 처음 돌렸을 때는 이런 값도 나왔다(`f8_delay`에서 `fsleep(20ms)` = 20 ms 요청).

```text
f8_delay: fsleep(20ms) min=12473 avg=25013 max=42825 us
```

20 ms를 요청했는데 12.5 ms 만에 깼다. 커널은 wheel에서 일찍 울리지 않게 올림까지 하는데 어떻게 가능할까? `timer_list`의 기준은 `ktime`이 아니라 **jiffies**다. NO_HZ에서 jiffies는 "jiffies 담당 CPU"(`tick_do_timer_cpu`)의 tick이 올린다. 그 CPU의 vCPU 스레드가 호스트에서 수 ms 멈추면 **jiffies가 실제 시간보다 뒤처진다.** 그 상태에서 `jiffies + 5`로 무장하면 기준이 이미 과거라서, 나중에 jiffies가 따라잡는 순간 타이머가 한꺼번에 만료된다. 커널도 이 상황을 알고 있다.

```c
// kernel/time/tick-sched.c (6.18) — tick_sched_do_timer()
	/*
	 * If the jiffies update stalled for too long (timekeeper in stop_machine()
	 * or VMEXIT'ed for several msecs), force an update.
	 */
	if (ts->last_tick_jiffies != jiffies) {
		ts->stalled_jiffies = 0;
		ts->last_tick_jiffies = READ_ONCE(jiffies);
	} else {
		if (++ts->stalled_jiffies == MAX_STALLED_JIFFIES) {
			tick_do_update_jiffies64(now);
```

(`MAX_STALLED_JIFFIES`는 5.) 주석이 바로 "VMEXIT로 몇 ms 멈춘 경우"다 — **가상화 환경의 아티팩트**이고, icount sleep=off에서는 한 번도 나오지 않았다(모든 최소값이 구간 하한 이상). 실제 보드에서는 `stop_machine()` 같은 드문 경우가 아니면 보기 어렵다. 교훈: **`timer_list`/`msleep`의 기준은 jiffies이고, jiffies는 ktime보다 거칠 뿐 아니라 늦게 갱신될 수도 있다.** 정확한 시간 간격이 필요하면 hrtimer와 ktime을 쓴다.

### 7.5 예제 10 — ftrace로 버킷과 만료를 직접 보기

무엇을 확인하나: `timer_start` 이벤트에 찍히는 `expires`와 `bucket_expiry`가 §6.5의 `+1` 규칙대로인지, hrtimer는 요청 시각 그대로 무장되는지. ftrace가 있는 **디버그 커널**에서 `f8_lat n=2`를 돌렸다(실시간 TCG라 시각 간격에는 잡음이 있다).

```sh
mount -t tracefs none /sys/kernel/tracing
cd /sys/kernel/tracing
for e in timer_start timer_expire_entry hrtimer_start hrtimer_expire_entry; do echo 1 > events/timer/$e/enable; done
echo 1 > tracing_on
insmod /f8_lat.ko n=2
echo 0 > tracing_on
grep -E "f8_(tl|hrt)_fn" trace | head -40
```

```text
          insmod-73      [000] d..1.    18.866430: timer_start: timer=000000009a7a6199 function=f8_tl_fn [f8_lat] expires=4294896599 [timeout=1] bucket_expiry=4294896600 cpu=0 idx=24 flags=
          <idle>-0       [000] .Ns1.    18.871367: timer_expire_entry: timer=000000009a7a6199 function=f8_tl_fn [f8_lat] now=4294896600 baseclk=4294896600
          insmod-73      [001] d..1.    18.873524: timer_start: timer=000000009a7a6199 function=f8_tl_fn [f8_lat] expires=4294896601 [timeout=1] bucket_expiry=4294896602 cpu=1 idx=26 flags=
          <idle>-0       [000] ..s1.    18.879455: timer_expire_entry: timer=000000009a7a6199 function=f8_tl_fn [f8_lat] now=4294896602 baseclk=4294896602
...
          insmod-73      [001] d..1.    18.926343: timer_start: timer=000000009a7a6199 function=f8_tl_fn [f8_lat] expires=4294896616 [timeout=3] bucket_expiry=4294896617 cpu=1 idx=41 flags=
          <idle>-0       [001] ..s1.    18.939173: timer_expire_entry: timer=000000009a7a6199 function=f8_tl_fn [f8_lat] now=4294896617 baseclk=4294896617
          insmod-73      [000] d..1.    18.964267: hrtimer_start: hrtimer=00000000ac427973 function=f8_hrt_fn [f8_lat] expires=17310329584 softexpires=17310329584 mode=REL|HARD was_armed=0
          <idle>-0       [000] d.h1.    18.965336: hrtimer_expire_entry: hrtimer=00000000ac427973 function=f8_hrt_fn [f8_lat] now=17310444704
...
```

(32줄 중 일부. 포인터 값은 실행마다 다르다.)

출력에서 볼 것:

- `timer_start ... expires=4294896599 [timeout=1] bucket_expiry=4294896600` — **요청 1 jiffy, 버킷은 +1**. `timer_expire_entry ... now=4294896600`: 정확히 버킷 jiffy에 실행됐다. `[timeout=3]`(10 ms 요청)도 `expires=...616 → bucket=...617`.
- 컨텍스트 플래그 열: `timer_expire_entry`는 `..s1.` — **`s` = softirq**. `hrtimer_expire_entry`는 `d.h1.` — **`d` = IRQ 꺼짐, `h` = hardirq**. 콜백이 어디서 도는지 트레이스가 증명한다.
- 두 번째 `timer_start`는 CPU1에서 무장했는데 만료는 CPU0에서 실행됐다(`[000]`). 6.9부터 들어간 timer migration 계층(`kernel/time/timer_migration.c` — idle CPU의 전역 타이머를 깨어 있는 CPU가 대신 처리)이 한 일로 보인다. 특정 CPU에서 돌아야 하면 `TIMER_PINNED`.
- hrtimer는 `expires=17310329584` ns를 그대로 들고 있고, `now=17310444704`에 실행됐다(디버그 커널 + 실시간 TCG라 115 us 늦음).

---

## 8. ktime과 시계 고르기 — 센서 타임스탬프는 무엇으로 찍나

### 8.1 커널 시계 종류

`ktime_t`는 그냥 `s64` 나노초다(예전에는 32비트용 union이었지만 지금은 단순 정수). 커널은 같은 clocksource에서 여러 "관점"의 시계를 만든다.

| 커널 함수 | 유저 공간 clockid | 기준·특징 | 언제 쓰나 |
|---|---|---|---|
| `ktime_get()` | `CLOCK_MONOTONIC` | 부팅부터, 절대 뒤로 안 감. NTP가 **속도**(주파수)는 보정함. suspend 동안 멈춤 | **기본값.** 타임아웃, 간격 측정, 센서 타임스탬프 |
| `ktime_get_raw()` | `CLOCK_MONOTONIC_RAW` | 카운터 원본 환산, NTP 보정 없음 | 하드웨어 클럭 자체의 드리프트를 볼 때 |
| `ktime_get_boottime()` | `CLOCK_BOOTTIME` | MONOTONIC + suspend 시간 | suspend하는 장비에서 "실제 경과" |
| `ktime_get_real()` | `CLOCK_REALTIME` | 벽시계(1970년 기준). 사용자가·NTP가 **점프**시킬 수 있음 | 로그에 사람이 읽는 시각. 간격 측정 금지 |
| `ktime_get_clocktai()` | `CLOCK_TAI` | 윤초 없는 원자시 | PTP·TSN (Q3) |
| `ktime_get_coarse()` 등 `_coarse` | `CLOCK_MONOTONIC_COARSE` | 마지막 tick 때 값 (카운터 안 읽음) | 아주 자주 읽고 tick 해상도면 충분할 때 |
| `ktime_get_mono_fast_ns()` | — | NMI에서도 안전 (seqlock 대기 없음) | 트레이싱, NMI 핸들러 |
| `jiffies` | — | tick 수 | `timer_list`용 만료 계산 |
| `local_clock()` / `sched_clock()` | — | 스케줄러용, CPU별, 빠름 | 커널 내부 통계. 드라이버 타임스탬프로 쓰지 말 것 |

### 8.2 센서 타임스탬프는 `CLOCK_MONOTONIC` (Q3 연결)

예를 들어 IMU 드라이버가 FIFO 워터마크 인터럽트를 받으면 **hardirq 핸들러 맨 앞에서** `ktime_get()`(또는 IIO라면 `iio_get_time_ns()`)으로 시각을 찍고, 샘플 개수와 ODR로 각 샘플 시각을 거꾸로 계산한다. 센서 데이터의 시간 축은 `CLOCK_MONOTONIC`이 기본 선택이다. V4L2 카메라 버퍼는 `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`으로 MONOTONIC을 쓴다. 반면 **IIO와 evdev는 역사적인 이유로 기본이 REALTIME**이라서, IIO는 sysfs `current_timestamp_clock`에 `monotonic`을 써 주고, evdev는 `EVIOCSCLOCKID` ioctl로 바꿔 줘야 한다 — 센서 키트 bring-up 체크리스트에 넣어 둘 항목이다(6.18 소스로 확인: IIO의 `clock_id`와 evdev 클라이언트의 `clk_type`은 0으로 할당되는데, 0이 각각 `CLOCK_REALTIME`, `INPUT_CLK_REAL`이다). MONOTONIC이어야 하는 이유:

- REALTIME은 NTP·GNSS 시각 동기·사용자의 `date -s`로 **점프한다**. 퓨전 알고리즘이 dt < 0을 보면 폭발한다.
- MONOTONIC은 모든 프로세스·드라이버가 공유하는 하나의 축이라, IMU·카메라·LiDAR 타임스탬프를 같은 축에서 비교할 수 있다.
- 장비가 suspend한다면 BOOTTIME을 고려한다(Android 센서 HAL이 이 방식). 로봇처럼 suspend하지 않는 장비는 MONOTONIC.
- 여러 장비 사이(LiDAR와 SoC)를 맞추는 건 별개 문제다 — PTP 하드웨어 클럭(PHC)과 `phc2sys`로 시스템 시계를 맞춘다(Q3).

### 8.3 시계 읽기 비용 — 그리고 디버그 커널의 함정

예제 5(`f8_jiffies`)의 마지막 줄을 커널·모드별로 모으면:

| 커널 / 모드 | `ktime_get` | `_raw` | `_real` | `_coarse` | `jiffies` |
|---|---|---|---|---|---|
| 기본 6.18.55, 실시간 TCG | 269 ns | 298 | 274 | 158 | 54 |
| 기본 6.18.55, icount | 472 ns | 448 | 603 | 584 | 24 |
| 디버그 6.18.55-dbg, 실시간 TCG | 17,171 ns | 22,853 | 14,148 | 18,906 | 48 |
| 디버그 6.18.55-dbg, icount | 26,262 ns | 25,703 | 25,687 | 24,778 | 47 |

(각 칸은 실제 `cost(ns):` 출력 줄의 값. 1000회 평균.)

출력에서 볼 것: 디버그 커널에서는 `ktime_get()` 한 번이 **수십 us**다 — 기본 커널의 약 60배. 디버그 커널에서 hrtimer 지연을 재면 측정 도구(시계 읽기) 자체가 결과를 오염시킨다. 실제로 같은 `f8_lat`을 같은 모드(icount, sleep=on)에서 돌리면 hrtimer 1 ms의 중앙값이 기본 커널 1.035 ms, 디버그 커널 1.110 ms로 75 us 차이가 났다. icount 줄에서 coarse가 오히려 비싸게 나온 것처럼, 수백 ns 수준의 차이는 에뮬레이터에서 의미가 없다. 실제 SoC에서 vDSO `clock_gettime`이 수십 ns라는 건 예제 2의 M2 수치(29 ns)가 보여 준다.

---

## 9. 주기 샘플링 — `hrtimer_forward_now()`와 드리프트

### 9.1 직관

"1 kHz로 센서를 읽는다"를 구현하는 두 가지 방법이 있다.

- **상대 재무장**: 콜백에서 "지금부터 1 ms 뒤"로 다시 건다. 콜백이 매번 d만큼 늦게 돌면, 다음 주기도 d만큼 밀린다. 오차가 **누적**된다(드리프트). 1000주기 뒤에는 1000 × d만큼 늦다.
- **절대 재무장**: "이전 만료 시각 + 1 ms"로 건다. 콜백이 늦어도 다음 만료 시각은 원래 격자 위에 있다. 오차가 누적되지 않는다(지터만 있다). MCU에서 비교 일치 타이머를 `CCR += period`로 다시 걸던 것과 같다.

`hrtimer_forward_now(timer, interval)`이 두 번째 방법이다. 만료 시각에 interval을 **지금보다 미래가 될 때까지** 더하고, 몇 번 더했는지(= 1 + 놓친 주기 수)를 돌려준다. 콜백이 한 주기 넘게 늦으면 놓친 주기를 건너뛰고(overrun), 반환값으로 그 사실을 알려 준다 — 샘플 드롭 카운터로 쓰기 좋다.

### 9.2 예제 11 — 1 kHz 샘플링 1000주기 (`f8_periodic`)

무엇을 확인하나: 1 ms 주기 hrtimer를 세 방식으로 1000번씩 돌리고, 매 콜백의 "예정 만료 대비 늦음"과 1000번에 걸린 총 시간, overrun 수를 잰다. (1) `forward` + HARD, (2) `forward` + SOFT, (3) "now + 1 ms" 상대 재무장 + HARD.

```c
// SPDX-License-Identifier: GPL-2.0
/* f8_periodic: 1 kHz 센서 샘플링 타이머 — forward(절대) vs 상대 재무장 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/sort.h>

static int n = 1000, period_us = 1000;
module_param(n, int, 0444);
module_param(period_us, int, 0444);
static struct hrtimer hrt;
static DECLARE_COMPLETION(done);
static s64 *late;			/* ns: 실제 콜백 시각 - 예정 만료 시각 */
static int idx, mode, overruns;		/* mode 0: forward, 1: now+period */

static enum hrtimer_restart f8_sample_fn(struct hrtimer *h)
{
	ktime_t now = ktime_get(), period = us_to_ktime(period_us);

	late[idx] = ktime_to_ns(ktime_sub(now, hrtimer_get_expires(h)));
	/* 여기서 센서 레지스터를 읽는다고 가정 (I2C면 안 됨 — atomic) */
	if (++idx == n) {
		complete(&done);
		return HRTIMER_NORESTART;
	}
	if (mode == 0)
		overruns += hrtimer_forward_now(h, period) - 1; /* 놓친 주기 */
	else
		hrtimer_set_expires(h, ktime_add(now, period));	/* 드리프트 */
	return HRTIMER_RESTART;
}

static int cmp(const void *a, const void *b)
{
	return *(const s64 *)a < *(const s64 *)b ? -1 : *(const s64 *)a > *(const s64 *)b;
}
```

(이어서 init — 같은 파일.)

```c
static int __init f8_periodic_init(void)
{
	static const enum hrtimer_mode hm[] = { HRTIMER_MODE_ABS_HARD,
		HRTIMER_MODE_ABS_SOFT, HRTIMER_MODE_ABS_HARD };
	static const char * const nm[] = { "forward/HARD", "forward/SOFT", "now+P/HARD" };
	int r;

	late = kmalloc_array(n, sizeof(*late), GFP_KERNEL);
	if (!late)
		return -ENOMEM;
	for (r = 0; r < 3; r++) {
		ktime_t t0;

		mode = (r == 2); idx = 0; overruns = 0;
		reinit_completion(&done);
		hrtimer_setup(&hrt, f8_sample_fn, CLOCK_MONOTONIC, hm[r]);
		t0 = ktime_get();
		hrtimer_start(&hrt, ktime_add_us(t0, period_us), hm[r]);
		wait_for_completion(&done);
		pr_info("%s: %d periods took %lld us (ideal %lld) overruns=%d\n", nm[r], n,
			ktime_us_delta(ktime_get(), t0), (s64)n * period_us, overruns);
		for (idx = 0; r == 0 && idx < n; idx++)	/* 원자료(us) 덤프 */
			pr_info("raw %d %lld\n", idx, div_s64(late[idx], 1000));
		sort(late, n, sizeof(*late), cmp, NULL);
		pr_info("%s late(us): min=%lld p50=%lld p99=%lld max=%lld\n", nm[r],
			div_s64(late[0], 1000), div_s64(late[n / 2], 1000),
			div_s64(late[n * 99 / 100], 1000), div_s64(late[n - 1], 1000));
	}
	kfree(late);
	return 0;
}
module_init(f8_periodic_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F8: periodic hrtimer sampling jitter");
```

모드 (3)에서 `hrtimer_set_expires()`로 만료를 "now + P"로 바꾸고 `HRTIMER_RESTART`를 돌려주는 것은 hrtimer 코어가 허용하는 방식이다(콜백 반환 후 새 만료로 다시 큐에 넣는다). 단지 드리프트가 생길 뿐이다.

### 9.3 결과

테스트: `insmod /f8_periodic.ko; dmesg | grep f8_periodic` (`raw` 1000줄은 차트용으로만 쓰고 아래에서는 뺐다). **icount sleep=off**:

```text
lab initramfs: 6.18.55 up 2.34s
f8_periodic: forward/HARD: 1000 periods took 1000040 us (ideal 1000000) overruns=0
f8_periodic: forward/HARD late(us): min=7 p50=7 p99=12 max=13
f8_periodic: forward/SOFT: 1000 periods took 1000044 us (ideal 1000000) overruns=0
f8_periodic: forward/SOFT late(us): min=13 p50=13 p99=18 max=525
f8_periodic: now+P/HARD: 1000 periods took 1008879 us (ideal 1000000) overruns=0
f8_periodic: now+P/HARD late(us): min=7 p50=7 p99=12 max=66
```

**실시간 TCG** (호스트 load 14~15):

```text
lab initramfs: 6.18.55 up 2.57s
f8_periodic: forward/HARD: 1000 periods took 1157245 us (ideal 1000000) overruns=157
f8_periodic: forward/HARD late(us): min=27 p50=82 p99=2869 max=10811
f8_periodic: forward/SOFT: 1000 periods took 1323207 us (ideal 1000000) overruns=323
f8_periodic: forward/SOFT late(us): min=33 p50=153 p99=5831 max=34775
f8_periodic: now+P/HARD: 1000 periods took 1444206 us (ideal 1000000) overruns=0
f8_periodic: now+P/HARD late(us): min=32 p50=172 p99=3668 max=20580
```

```svg
<svg viewBox="0 0 640 350" xmlns="http://www.w3.org/2000/svg">
<text x="320.0" y="20" text-anchor="middle" font-size="13">1 kHz hrtimer 샘플링 1000주기 — 예정 시각 대비 늦음 (로그 축)</text>
<line x1="60" y1="270.0" x2="620" y2="270.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="274.0" text-anchor="end" font-size="12">1 us</text>
<line x1="60" y1="224.0" x2="620" y2="224.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="228.0" text-anchor="end" font-size="12">10 us</text>
<line x1="60" y1="178.0" x2="620" y2="178.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="182.0" text-anchor="end" font-size="12">100 us</text>
<line x1="60" y1="132.0" x2="620" y2="132.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="136.0" text-anchor="end" font-size="12">1 ms</text>
<line x1="60" y1="86.0" x2="620" y2="86.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="90.0" text-anchor="end" font-size="12">10 ms</text>
<line x1="60" y1="40.0" x2="620" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="44.0" text-anchor="end" font-size="12">100 ms</text>
<line x1="60" y1="40" x2="60" y2="270" stroke="currentColor"/><line x1="60" y1="270" x2="620" y2="270" stroke="currentColor"/>
<text x="60.0" y="286" text-anchor="middle" font-size="12">0</text>
<text x="200.1" y="286" text-anchor="middle" font-size="12">250</text>
<text x="340.3" y="286" text-anchor="middle" font-size="12">500</text>
<text x="480.4" y="286" text-anchor="middle" font-size="12">750</text>
<text x="620.0" y="286" text-anchor="middle" font-size="12">1000</text>
<text x="340.0" y="304" text-anchor="middle" font-size="12">샘플 번호</text>
<line x1="60" y1="132.0" x2="620" y2="132.0" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="616" y="127.0" text-anchor="end" font-size="12">1주기 (이보다 늦으면 overrun)</text>
<polyline points="60.0,154.7 60.6,180.6 61.1,136.2 61.7,124.0 62.2,177.8 62.8,175.4 63.4,156.0 63.9,139.5 64.5,172.9 65.0,170.6 65.6,185.1 66.2,167.3 66.7,156.7 67.3,169.6 67.8,174.5 68.4,188.9 69.0,158.8 69.5,183.0 70.1,185.1 70.7,130.7 71.2,176.8 71.8,129.5 72.3,188.5 72.9,183.5 73.5,191.8 74.0,175.4 74.6,133.1 75.1,200.1 75.7,157.1 76.3,188.2 76.8,190.7 77.4,172.3 77.9,169.8 78.5,148.4 79.1,183.5 79.6,187.5 80.2,192.3 80.7,174.9 81.3,182.0 81.9,173.7 82.4,151.3 83.0,136.9 83.5,150.0 84.1,146.7 84.7,177.2 85.2,185.4 85.8,172.5 86.3,126.8 86.9,148.4 87.5,181.5 88.0,169.2 88.6,169.1 89.1,155.0 89.7,181.0 90.3,167.2 90.8,166.6 91.4,184.3 92.0,179.9 92.5,172.8 93.1,173.7 93.6,114.9 94.2,177.0 94.8,192.3 95.3,159.7 95.9,173.2 96.4,187.5 97.0,194.0 97.6,194.9 98.1,186.3 98.7,175.6 99.2,140.9 99.8,154.8 100.4,181.7 100.9,177.0 101.5,180.6 102.0,180.8 102.6,177.2 103.2,189.9 103.7,171.4 104.3,145.5 104.8,113.2 105.4,146.7 106.0,147.0 106.5,186.0 107.1,125.9 107.6,144.9 108.2,189.6 108.8,171.4 109.3,117.0 109.9,130.6 110.5,142.6 111.0,188.2 111.6,131.0 112.1,142.2 112.7,129.1 113.3,140.5 113.8,130.5 114.4,130.3 114.9,176.3 115.5,138.5 116.1,130.4 116.6,140.9 117.2,172.8 117.7,129.8 118.3,142.6 118.9,131.0 119.4,130.9 120.0,167.2 120.5,131.0 121.1,161.0 121.7,152.6 122.2,189.6 122.8,131.1 123.3,117.2 123.9,130.0 124.5,130.1 125.0,135.6 125.6,141.5 126.1,130.3 126.7,168.4 127.3,179.4 127.8,151.5 128.4,173.9 128.9,137.4 129.5,145.6 130.1,116.5 130.6,116.4 131.2,106.0 131.8,170.4 132.3,150.9 132.9,183.2 133.4,176.1 134.0,151.0 134.6,174.9 135.1,147.9 135.7,133.6 136.2,176.5 136.8,175.2 137.4,102.3 137.9,132.3 138.5,168.1 139.0,182.0 139.6,138.6 140.2,140.1 140.7,175.7 141.3,168.2 141.8,182.0 142.4,172.3 143.0,102.8 143.5,180.8 144.1,171.9 144.6,180.8 145.2,186.0 145.8,190.7 146.3,193.1 146.9,194.4 147.4,181.5 148.0,187.9 148.6,185.4 149.1,190.3 149.7,190.3 150.3,170.6 150.8,180.1 151.4,188.9 151.9,189.9 152.5,194.0 153.1,191.8 153.6,193.5 154.2,189.2 154.7,90.7 155.3,181.2 155.9,194.4 156.4,115.3 157.0,108.9 157.5,127.6 158.1,124.8 158.7,180.6 159.2,179.7 159.8,179.0 160.3,178.0 160.9,181.2 161.5,172.0 162.0,174.7 162.6,171.0 163.1,175.6 163.7,183.5 164.3,175.7 164.8,185.7 165.4,188.5 165.9,194.4 166.5,128.0 167.1,191.1 167.6,171.6 168.2,176.8 168.7,190.7 169.3,199.6 169.9,187.5 170.4,196.3 171.0,165.9 171.6,175.6 172.1,181.5 172.7,185.4 173.2,192.3 173.8,180.3 174.4,179.9 174.9,182.0 175.5,180.6 176.0,196.3 176.6,200.1 177.2,193.1 177.7,198.4 178.3,199.0 178.8,198.4 179.4,194.4 180.0,171.9 180.5,183.0 181.1,197.3 181.6,194.9 182.2,201.4 182.8,200.8 183.3,200.1 183.9,196.8 184.4,201.4 185.0,201.4 185.6,202.1 186.1,173.1 186.7,180.1 187.2,186.6 187.8,187.2 188.4,180.8 188.9,179.7 189.5,103.7 190.1,197.9 190.6,199.6 191.2,198.4 191.7,186.0 192.3,197.3 192.9,174.2 193.4,181.5 194.0,189.2 194.5,183.7 195.1,168.9 195.7,190.3 196.2,199.6 196.8,191.5 197.3,202.1 197.9,200.1 198.5,197.9 199.0,201.4 199.6,199.6 200.1,204.2 200.7,203.4 201.3,200.1 201.8,200.8 202.4,202.1 202.9,202.1 203.5,202.7 204.1,197.9 204.6,194.4 205.2,201.4 205.7,203.4 206.3,200.8 206.9,202.1 207.4,200.1 208.0,201.4 208.5,198.4 209.1,183.0 209.7,192.7 210.2,186.0 210.8,193.5 211.4,192.3 211.9,195.8 212.5,175.4 213.0,186.9 213.6,181.2 214.2,186.9 214.7,192.7 215.3,191.5 215.8,192.3 216.4,193.1 217.0,184.0 217.5,186.9 218.1,186.0 218.6,186.6 219.2,195.8 219.8,193.1 220.3,186.0 220.9,193.1 221.4,193.5 222.0,195.8 222.6,200.8 223.1,202.7 223.7,175.7 224.2,113.4 224.8,161.4 225.4,181.5 225.9,190.3 226.5,181.5 227.0,183.7 227.6,189.9 228.2,197.9 228.7,192.7 229.3,186.9 229.8,193.1 230.4,200.8 231.0,194.4 231.5,177.8 232.1,169.8 232.7,133.3 233.2,197.3 233.8,131.5 234.3,148.4 234.9,136.0 235.5,131.2 236.0,163.6 236.6,147.3 237.1,131.1 237.7,174.5 238.3,131.3 238.8,144.2 239.4,147.0 239.9,117.8 240.5,141.6 241.1,171.0 241.6,113.2 242.2,130.6 242.7,130.4 243.3,130.7 243.9,173.2 244.4,141.4 245.0,176.8 245.5,132.4 246.1,191.1 246.7,188.9 247.2,131.1 247.8,149.5 248.3,148.8 248.9,177.0 249.5,134.1 250.0,130.2 250.6,130.8 251.2,130.0 251.7,124.3 252.3,153.8 252.8,138.9 253.4,131.3 254.0,149.8 254.5,130.7 255.1,184.0 255.6,130.0 256.2,145.6 256.8,141.4 257.3,191.5 257.9,185.1 258.4,154.4 259.0,139.5 259.6,131.1 260.1,149.0 260.7,150.3 261.2,145.3 261.8,143.9 262.4,149.7 262.9,137.4 263.5,132.4 264.0,180.6 264.6,146.7 265.2,174.9 265.7,131.7 266.3,131.2 266.8,109.6 267.4,130.9 268.0,134.1 268.5,131.0 269.1,135.6 269.6,131.3 270.2,130.8 270.8,130.3 271.3,130.3 271.9,157.9 272.5,130.4 273.0,172.8 273.6,130.5 274.1,181.2 274.7,130.6 275.3,130.4 275.8,130.9 276.4,155.5 276.9,136.2 277.5,118.0 278.1,144.3 278.6,147.0 279.2,144.2 279.7,165.7 280.3,132.7 280.9,192.3 281.4,150.4 282.0,132.4 282.5,187.5 283.1,120.4 283.7,160.3 284.2,157.6 284.8,150.8 285.3,131.0 285.9,130.1 286.5,163.7 287.0,137.4 287.6,131.1 288.1,130.6 288.7,139.2 289.3,131.2 289.8,158.7 290.4,158.3 291.0,130.4 291.5,119.2 292.1,138.5 292.6,131.5 293.2,157.6 293.8,153.3 294.3,134.8 294.9,131.1 295.4,191.8 296.0,191.5 296.6,193.5 297.1,186.3 297.7,190.7 298.2,190.7 298.8,174.9 299.4,177.4 299.9,182.0 300.5,181.7 301.0,185.4 301.6,170.4 302.2,183.7 302.7,182.0 303.3,182.5 303.8,165.3 304.4,183.0 305.0,189.6 305.5,188.2 306.1,167.4 306.6,181.0 307.2,186.3 307.8,179.4 308.3,134.3 308.9,192.3 309.4,131.3 310.0,176.6 310.6,164.4 311.1,182.7 311.7,180.8 312.3,145.3 312.8,175.9 313.4,179.9 313.9,178.8 314.5,183.5 315.1,182.5 315.6,191.5 316.2,191.8 316.7,189.6 317.3,186.3 317.9,172.5 318.4,182.5 319.0,175.6 319.5,179.0 320.1,183.0 320.7,184.0 321.2,179.7 321.8,174.4 322.3,176.1 322.9,179.2 323.5,185.1 324.0,190.3 324.6,186.6 325.1,190.7 325.7,167.2 326.3,180.1 326.8,129.5 327.4,178.4 327.9,184.3 328.5,192.7 329.1,179.7 329.6,183.0 330.2,187.2 330.8,192.7 331.3,176.6 331.9,184.6 332.4,186.9 333.0,173.2 333.6,178.4 334.1,155.1 334.7,174.7 335.2,176.6 335.8,177.8 336.4,177.6 336.9,181.5 337.5,186.9 338.0,183.7 338.6,177.4 339.2,138.8 339.7,128.8 340.3,154.5 340.8,181.2 341.4,176.8 342.0,130.1 342.5,130.4 343.1,116.7 343.6,138.1 344.2,130.8 344.8,116.9 345.3,149.1 345.9,135.8 346.4,131.4 347.0,133.5 347.6,146.0 348.1,192.3 348.7,181.7 349.2,191.5 349.8,192.3 350.4,175.6 350.9,182.2 351.5,179.4 352.1,184.3 352.6,181.5 353.2,177.6 353.7,152.0 354.3,179.4 354.9,152.2 355.4,176.6 356.0,174.2 356.5,174.7 357.1,179.0 357.7,185.1 358.2,126.5 358.8,188.9 359.3,147.3 359.9,165.2 360.5,171.1 361.0,170.4 361.6,181.0 362.1,175.0 362.7,191.5 363.3,178.0 363.8,121.8 364.4,190.3 364.9,188.9 365.5,160.4 366.1,173.7 366.6,180.8 367.2,184.6 367.7,169.8 368.3,166.6 368.9,173.5 369.4,175.9 370.0,165.2 370.6,171.7 371.1,160.9 371.7,190.3 372.2,188.2 372.8,187.5 373.4,189.2 373.9,194.9 374.5,194.9 375.0,84.4 375.6,189.9 376.2,156.3 376.7,175.0 377.3,182.7 377.8,174.2 378.4,177.8 379.0,137.7 379.5,186.3 380.1,179.7 380.6,177.6 381.2,185.7 381.8,188.5 382.3,187.9 382.9,178.0 383.4,191.5 384.0,188.9 384.6,166.6 385.1,170.4 385.7,184.3 386.2,184.3 386.8,181.2 387.4,179.2 387.9,153.1 388.5,183.5 389.0,193.1 389.6,189.9 390.2,195.3 390.7,196.8 391.3,197.3 391.9,182.2 392.4,124.0 393.0,194.0 393.5,185.1 394.1,191.5 394.7,170.9 395.2,182.0 395.8,184.0 396.3,185.1 396.9,186.0 397.5,172.2 398.0,181.7 398.6,183.0 399.1,183.0 399.7,186.0 400.3,185.7 400.8,191.5 401.4,188.2 401.9,185.1 402.5,187.9 403.1,176.5 403.6,182.5 404.2,184.0 404.7,182.7 405.3,168.7 405.9,168.0 406.4,185.7 407.0,110.9 407.5,187.2 408.1,172.8 408.7,157.1 409.2,177.6 409.8,112.9 410.4,194.0 410.9,185.4 411.5,178.8 412.0,164.0 412.6,133.9 413.2,194.4 413.7,117.3 414.3,188.2 414.8,189.2 415.4,194.9 416.0,194.0 416.5,194.9 417.1,188.9 417.6,194.0 418.2,193.1 418.8,191.8 419.3,191.8 419.9,195.3 420.4,196.3 421.0,187.9 421.6,190.3 422.1,194.4 422.7,197.9 423.2,191.8 423.8,172.9 424.4,187.5 424.9,194.0 425.5,187.2 426.0,186.0 426.6,192.7 427.2,191.1 427.7,190.3 428.3,191.5 428.8,191.1 429.4,187.5 430.0,193.1 430.5,191.1 431.1,158.9 431.7,195.8 432.2,195.8 432.8,194.4 433.3,142.9 433.9,182.7 434.5,166.4 435.0,171.4 435.6,193.5 436.1,190.3 436.7,195.8 437.3,190.7 437.8,191.1 438.4,195.3 438.9,195.3 439.5,189.6 440.1,196.8 440.6,172.5 441.2,165.3 441.7,169.9 442.3,153.0 442.9,147.3 443.4,190.3 444.0,184.0 444.5,170.2 445.1,177.4 445.7,179.2 446.2,176.8 446.8,176.5 447.3,180.6 447.9,178.0 448.5,179.4 449.0,183.0 449.6,182.7 450.2,183.2 450.7,181.7 451.3,188.2 451.8,187.2 452.4,184.6 453.0,180.6 453.5,184.3 454.1,181.5 454.6,160.8 455.2,129.6 455.8,162.0 456.3,177.8 456.9,185.7 457.4,188.5 458.0,194.9 458.6,195.3 459.1,190.7 459.7,193.1 460.2,144.9 460.8,184.0 461.4,190.3 461.9,188.5 462.5,194.9 463.0,167.8 463.6,184.8 464.2,181.0 464.7,185.1 465.3,181.0 465.8,186.9 466.4,194.9 467.0,189.6 467.5,174.9 468.1,181.0 468.6,188.5 469.2,191.1 469.8,194.0 470.3,183.5 470.9,186.0 471.5,191.8 472.0,182.7 472.6,182.0 473.1,171.1 473.7,130.2 474.3,172.5 474.8,167.6 475.4,174.2 475.9,185.7 476.5,189.2 477.1,193.5 477.6,197.9 478.2,197.9 478.7,191.5 479.3,174.2 479.9,188.5 480.4,176.3 481.0,183.7 481.5,189.9 482.1,191.1 482.7,188.2 483.2,186.6 483.8,188.5 484.3,189.6 484.9,174.0 485.5,184.0 486.0,192.3 486.6,193.5 487.1,196.3 487.7,189.9 488.3,195.8 488.8,187.5 489.4,186.0 489.9,191.1 490.5,191.8 491.1,194.4 491.6,195.3 492.2,191.8 492.8,196.3 493.3,195.8 493.9,165.7 494.4,191.5 495.0,194.0 495.6,189.9 496.1,195.3 496.7,192.7 497.2,197.9 497.8,189.2 498.4,194.9 498.9,194.4 499.5,191.8 500.0,194.4 500.6,194.0 501.2,163.4 501.7,173.7 502.3,148.3 502.8,176.1 503.4,185.7 504.0,192.7 504.5,179.7 505.1,187.5 505.6,189.9 506.2,195.3 506.8,185.7 507.3,188.9 507.9,191.1 508.4,196.3 509.0,195.3 509.6,194.4 510.1,192.7 510.7,195.3 511.3,195.3 511.8,187.2 512.4,191.8 512.9,194.4 513.5,185.1 514.1,189.2 514.6,114.5 515.2,172.8 515.7,186.6 516.3,186.3 516.9,188.5 517.4,187.2 518.0,189.9 518.5,194.0 519.1,195.8 519.7,185.7 520.2,189.6 520.8,194.4 521.3,188.5 521.9,188.5 522.5,191.5 523.0,181.0 523.6,186.3 524.1,194.0 524.7,189.6 525.3,194.9 525.8,189.6 526.4,194.9 526.9,193.5 527.5,193.1 528.1,193.1 528.6,195.8 529.2,193.1 529.7,196.3 530.3,195.8 530.9,197.3 531.4,194.0 532.0,167.8 532.6,111.6 533.1,115.9 533.7,167.5 534.2,184.8 534.8,177.2 535.4,178.6 535.9,184.3 536.5,193.1 537.0,193.5 537.6,173.5 538.2,165.7 538.7,142.8 539.3,192.3 539.8,188.9 540.4,170.6 541.0,188.2 541.5,190.3 542.1,177.4 542.6,179.4 543.2,172.6 543.8,181.2 544.3,106.1 544.9,191.5 545.4,190.3 546.0,172.3 546.6,186.9 547.1,189.9 547.7,186.3 548.2,196.3 548.8,173.9 549.4,192.7 549.9,190.3 550.5,187.2 551.1,189.6 551.6,188.5 552.2,189.2 552.7,194.0 553.3,186.9 553.9,189.6 554.4,187.2 555.0,187.2 555.5,175.9 556.1,174.4 556.7,177.6 557.2,181.5 557.8,184.8 558.3,185.4 558.9,188.2 559.5,194.0 560.0,195.8 560.6,197.9 561.1,190.7 561.7,195.8 562.3,197.3 562.8,195.3 563.4,194.0 563.9,174.9 564.5,191.8 565.1,197.9 565.6,191.8 566.2,197.9 566.7,199.0 567.3,140.0 567.9,184.8 568.4,158.5 569.0,189.2 569.5,194.0 570.1,189.2 570.7,196.3 571.2,194.9 571.8,171.7 572.4,130.2 572.9,182.5 573.5,181.2 574.0,176.1 574.6,179.2 575.2,189.6 575.7,186.9 576.3,182.0 576.8,142.9 577.4,194.4 578.0,193.5 578.5,184.3 579.1,194.0 579.6,167.4 580.2,173.1 580.8,180.3 581.3,185.7 581.9,193.5 582.4,177.4 583.0,181.0 583.6,168.9 584.1,191.5 584.7,192.7 585.2,189.9 585.8,182.2 586.4,141.4 586.9,191.1 587.5,187.5 588.0,193.1 588.6,191.8 589.2,191.1 589.7,171.9 590.3,178.6 590.9,178.0 591.4,168.9 592.0,183.7 592.5,189.9 593.1,187.9 593.7,186.6 594.2,181.7 594.8,175.6 595.3,183.7 595.9,179.9 596.5,184.0 597.0,177.2 597.6,181.2 598.1,185.4 598.7,183.2 599.3,170.0 599.8,194.4 600.4,188.5 600.9,178.2 601.5,189.6 602.1,187.9 602.6,166.4 603.2,184.3 603.7,177.8 604.3,194.9 604.9,111.1 605.4,197.9 606.0,193.1 606.5,184.0 607.1,189.2 607.7,190.7 608.2,190.3 608.8,187.5 609.3,157.6 609.9,172.0 610.5,167.5 611.0,175.9 611.6,189.6 612.2,184.6 612.7,189.2 613.3,189.9 613.8,190.3 614.4,189.9 615.0,116.9 615.5,190.7 616.1,186.3 616.6,190.3 617.2,191.5 617.8,194.0 618.3,187.5 618.9,194.9 619.4,197.3 620.0,197.9" fill="none" stroke="#d0564a" stroke-width="1"/>
<polyline points="60.0,231.1 60.6,231.1 61.1,231.1 61.7,231.1 62.2,231.1 62.8,231.1 63.4,231.1 63.9,231.1 64.5,231.1 65.0,231.1 65.6,231.1 66.2,231.1 66.7,231.1 67.3,231.1 67.8,231.1 68.4,231.1 69.0,231.1 69.5,231.1 70.1,231.1 70.7,231.1 71.2,231.1 71.8,231.1 72.3,231.1 72.9,231.1 73.5,231.1 74.0,231.1 74.6,231.1 75.1,231.1 75.7,231.1 76.3,231.1 76.8,231.1 77.4,231.1 77.9,231.1 78.5,231.1 79.1,231.1 79.6,231.1 80.2,231.1 80.7,231.1 81.3,231.1 81.9,231.1 82.4,231.1 83.0,231.1 83.5,231.1 84.1,231.1 84.7,231.1 85.2,231.1 85.8,231.1 86.3,231.1 86.9,231.1 87.5,231.1 88.0,231.1 88.6,231.1 89.1,231.1 89.7,231.1 90.3,231.1 90.8,231.1 91.4,231.1 92.0,231.1 92.5,231.1 93.1,231.1 93.6,231.1 94.2,231.1 94.8,231.1 95.3,231.1 95.9,231.1 96.4,231.1 97.0,231.1 97.6,231.1 98.1,231.1 98.7,231.1 99.2,231.1 99.8,231.1 100.4,231.1 100.9,231.1 101.5,231.1 102.0,231.1 102.6,231.1 103.2,231.1 103.7,231.1 104.3,231.1 104.8,231.1 105.4,231.1 106.0,231.1 106.5,231.1 107.1,231.1 107.6,231.1 108.2,220.4 108.8,231.1 109.3,231.1 109.9,231.1 110.5,220.4 111.0,231.1 111.6,231.1 112.1,231.1 112.7,220.4 113.3,231.1 113.8,231.1 114.4,231.1 114.9,220.4 115.5,231.1 116.1,231.1 116.6,231.1 117.2,220.4 117.7,231.1 118.3,231.1 118.9,231.1 119.4,220.4 120.0,231.1 120.5,231.1 121.1,231.1 121.7,220.4 122.2,231.1 122.8,231.1 123.3,231.1 123.9,220.4 124.5,231.1 125.0,231.1 125.6,231.1 126.1,220.4 126.7,231.1 127.3,231.1 127.8,231.1 128.4,220.4 128.9,231.1 129.5,231.1 130.1,231.1 130.6,220.4 131.2,231.1 131.8,231.1 132.3,231.1 132.9,220.4 133.4,231.1 134.0,231.1 134.6,231.1 135.1,220.4 135.7,231.1 136.2,231.1 136.8,231.1 137.4,220.4 137.9,231.1 138.5,231.1 139.0,231.1 139.6,220.4 140.2,231.1 140.7,231.1 141.3,231.1 141.8,220.4 142.4,231.1 143.0,231.1 143.5,231.1 144.1,220.4 144.6,231.1 145.2,231.1 145.8,231.1 146.3,220.4 146.9,231.1 147.4,231.1 148.0,231.1 148.6,220.4 149.1,231.1 149.7,231.1 150.3,231.1 150.8,220.4 151.4,231.1 151.9,231.1 152.5,231.1 153.1,220.4 153.6,231.1 154.2,231.1 154.7,231.1 155.3,220.4 155.9,231.1 156.4,231.1 157.0,231.1 157.5,220.4 158.1,231.1 158.7,231.1 159.2,231.1 159.8,220.4 160.3,231.1 160.9,231.1 161.5,231.1 162.0,220.4 162.6,231.1 163.1,231.1 163.7,231.1 164.3,220.4 164.8,231.1 165.4,231.1 165.9,231.1 166.5,220.4 167.1,231.1 167.6,231.1 168.2,231.1 168.7,220.4 169.3,231.1 169.9,231.1 170.4,231.1 171.0,220.4 171.6,231.1 172.1,231.1 172.7,231.1 173.2,220.4 173.8,231.1 174.4,231.1 174.9,231.1 175.5,220.4 176.0,231.1 176.6,231.1 177.2,231.1 177.7,220.4 178.3,231.1 178.8,231.1 179.4,231.1 180.0,220.4 180.5,231.1 181.1,231.1 181.6,231.1 182.2,220.4 182.8,231.1 183.3,231.1 183.9,231.1 184.4,220.4 185.0,231.1 185.6,231.1 186.1,231.1 186.7,220.4 187.2,231.1 187.8,231.1 188.4,231.1 188.9,220.4 189.5,231.1 190.1,231.1 190.6,231.1 191.2,220.4 191.7,231.1 192.3,231.1 192.9,231.1 193.4,220.4 194.0,231.1 194.5,231.1 195.1,231.1 195.7,220.4 196.2,231.1 196.8,231.1 197.3,231.1 197.9,220.4 198.5,231.1 199.0,231.1 199.6,231.1 200.1,220.4 200.7,231.1 201.3,231.1 201.8,231.1 202.4,220.4 202.9,231.1 203.5,231.1 204.1,231.1 204.6,220.4 205.2,231.1 205.7,231.1 206.3,231.1 206.9,220.4 207.4,231.1 208.0,231.1 208.5,231.1 209.1,220.4 209.7,231.1 210.2,231.1 210.8,231.1 211.4,220.4 211.9,231.1 212.5,231.1 213.0,231.1 213.6,220.4 214.2,231.1 214.7,231.1 215.3,231.1 215.8,220.4 216.4,231.1 217.0,231.1 217.5,231.1 218.1,220.4 218.6,231.1 219.2,231.1 219.8,231.1 220.3,220.4 220.9,231.1 221.4,231.1 222.0,231.1 222.6,220.4 223.1,231.1 223.7,231.1 224.2,231.1 224.8,220.4 225.4,231.1 225.9,231.1 226.5,231.1 227.0,220.4 227.6,231.1 228.2,231.1 228.7,231.1 229.3,220.4 229.8,231.1 230.4,231.1 231.0,231.1 231.5,220.4 232.1,231.1 232.7,231.1 233.2,231.1 233.8,220.4 234.3,231.1 234.9,231.1 235.5,231.1 236.0,220.4 236.6,231.1 237.1,231.1 237.7,231.1 238.3,220.4 238.8,231.1 239.4,231.1 239.9,231.1 240.5,220.4 241.1,231.1 241.6,231.1 242.2,231.1 242.7,220.4 243.3,231.1 243.9,231.1 244.4,231.1 245.0,220.4 245.5,231.1 246.1,231.1 246.7,231.1 247.2,220.4 247.8,231.1 248.3,231.1 248.9,231.1 249.5,220.4 250.0,231.1 250.6,231.1 251.2,231.1 251.7,220.4 252.3,231.1 252.8,231.1 253.4,231.1 254.0,220.4 254.5,231.1 255.1,231.1 255.6,231.1 256.2,220.4 256.8,231.1 257.3,231.1 257.9,231.1 258.4,220.4 259.0,231.1 259.6,231.1 260.1,231.1 260.7,220.4 261.2,231.1 261.8,231.1 262.4,231.1 262.9,220.4 263.5,231.1 264.0,231.1 264.6,231.1 265.2,220.4 265.7,231.1 266.3,231.1 266.8,231.1 267.4,220.4 268.0,231.1 268.5,231.1 269.1,231.1 269.6,220.4 270.2,231.1 270.8,231.1 271.3,231.1 271.9,220.4 272.5,231.1 273.0,231.1 273.6,231.1 274.1,220.4 274.7,231.1 275.3,231.1 275.8,231.1 276.4,220.4 276.9,231.1 277.5,231.1 278.1,231.1 278.6,220.4 279.2,231.1 279.7,231.1 280.3,231.1 280.9,220.4 281.4,231.1 282.0,231.1 282.5,231.1 283.1,220.4 283.7,231.1 284.2,231.1 284.8,231.1 285.3,220.4 285.9,231.1 286.5,231.1 287.0,231.1 287.6,220.4 288.1,231.1 288.7,231.1 289.3,231.1 289.8,220.4 290.4,231.1 291.0,231.1 291.5,231.1 292.1,220.4 292.6,231.1 293.2,231.1 293.8,231.1 294.3,220.4 294.9,231.1 295.4,231.1 296.0,231.1 296.6,220.4 297.1,231.1 297.7,231.1 298.2,231.1 298.8,231.1 299.4,231.1 299.9,231.1 300.5,231.1 301.0,231.1 301.6,231.1 302.2,231.1 302.7,231.1 303.3,220.4 303.8,231.1 304.4,231.1 305.0,231.1 305.5,220.4 306.1,231.1 306.6,231.1 307.2,231.1 307.8,220.4 308.3,231.1 308.9,231.1 309.4,231.1 310.0,220.4 310.6,231.1 311.1,231.1 311.7,231.1 312.3,220.4 312.8,231.1 313.4,231.1 313.9,231.1 314.5,220.4 315.1,231.1 315.6,231.1 316.2,231.1 316.7,220.4 317.3,231.1 317.9,231.1 318.4,231.1 319.0,220.4 319.5,231.1 320.1,231.1 320.7,231.1 321.2,220.4 321.8,231.1 322.3,231.1 322.9,231.1 323.5,220.4 324.0,231.1 324.6,231.1 325.1,231.1 325.7,220.4 326.3,231.1 326.8,231.1 327.4,231.1 327.9,220.4 328.5,231.1 329.1,231.1 329.6,231.1 330.2,220.4 330.8,231.1 331.3,231.1 331.9,231.1 332.4,220.4 333.0,231.1 333.6,231.1 334.1,231.1 334.7,220.4 335.2,231.1 335.8,231.1 336.4,231.1 336.9,220.4 337.5,231.1 338.0,231.1 338.6,231.1 339.2,220.4 339.7,231.1 340.3,231.1 340.8,231.1 341.4,220.4 342.0,231.1 342.5,231.1 343.1,231.1 343.6,220.4 344.2,231.1 344.8,231.1 345.3,231.1 345.9,220.4 346.4,231.1 347.0,231.1 347.6,231.1 348.1,220.4 348.7,231.1 349.2,231.1 349.8,231.1 350.4,220.4 350.9,231.1 351.5,231.1 352.1,231.1 352.6,220.4 353.2,231.1 353.7,231.1 354.3,231.1 354.9,220.4 355.4,231.1 356.0,231.1 356.5,231.1 357.1,220.4 357.7,231.1 358.2,231.1 358.8,231.1 359.3,220.4 359.9,231.1 360.5,231.1 361.0,231.1 361.6,220.4 362.1,231.1 362.7,231.1 363.3,231.1 363.8,220.4 364.4,231.1 364.9,231.1 365.5,231.1 366.1,220.4 366.6,231.1 367.2,231.1 367.7,231.1 368.3,220.4 368.9,231.1 369.4,231.1 370.0,231.1 370.6,220.4 371.1,231.1 371.7,231.1 372.2,231.1 372.8,220.4 373.4,231.1 373.9,231.1 374.5,231.1 375.0,220.4 375.6,231.1 376.2,231.1 376.7,231.1 377.3,220.4 377.8,231.1 378.4,231.1 379.0,231.1 379.5,220.4 380.1,231.1 380.6,231.1 381.2,231.1 381.8,220.4 382.3,231.1 382.9,231.1 383.4,231.1 384.0,220.4 384.6,231.1 385.1,231.1 385.7,231.1 386.2,231.1 386.8,231.1 387.4,231.1 387.9,231.1 388.5,231.1 389.0,231.1 389.6,231.1 390.2,231.1 390.7,220.4 391.3,231.1 391.9,231.1 392.4,231.1 393.0,220.4 393.5,231.1 394.1,231.1 394.7,231.1 395.2,220.4 395.8,231.1 396.3,231.1 396.9,231.1 397.5,220.4 398.0,231.1 398.6,231.1 399.1,231.1 399.7,220.4 400.3,231.1 400.8,231.1 401.4,231.1 401.9,220.4 402.5,231.1 403.1,231.1 403.6,231.1 404.2,218.8 404.7,231.1 405.3,231.1 405.9,231.1 406.4,220.4 407.0,231.1 407.5,231.1 408.1,231.1 408.7,220.4 409.2,231.1 409.8,231.1 410.4,231.1 410.9,220.4 411.5,231.1 412.0,231.1 412.6,231.1 413.2,220.4 413.7,231.1 414.3,231.1 414.8,231.1 415.4,220.4 416.0,231.1 416.5,231.1 417.1,231.1 417.6,220.4 418.2,231.1 418.8,231.1 419.3,231.1 419.9,220.4 420.4,231.1 421.0,231.1 421.6,231.1 422.1,220.4 422.7,231.1 423.2,231.1 423.8,231.1 424.4,220.4 424.9,231.1 425.5,231.1 426.0,231.1 426.6,220.4 427.2,231.1 427.7,231.1 428.3,231.1 428.8,220.4 429.4,231.1 430.0,231.1 430.5,231.1 431.1,220.4 431.7,231.1 432.2,231.1 432.8,231.1 433.3,220.4 433.9,231.1 434.5,231.1 435.0,231.1 435.6,220.4 436.1,231.1 436.7,231.1 437.3,231.1 437.8,220.4 438.4,231.1 438.9,231.1 439.5,231.1 440.1,220.4 440.6,231.1 441.2,231.1 441.7,231.1 442.3,231.1 442.9,231.1 443.4,231.1 444.0,231.1 444.5,231.1 445.1,231.1 445.7,231.1 446.2,231.1 446.8,220.4 447.3,231.1 447.9,231.1 448.5,231.1 449.0,220.4 449.6,231.1 450.2,231.1 450.7,231.1 451.3,220.4 451.8,231.1 452.4,231.1 453.0,231.1 453.5,220.4 454.1,231.1 454.6,231.1 455.2,231.1 455.8,220.4 456.3,231.1 456.9,231.1 457.4,231.1 458.0,220.4 458.6,231.1 459.1,231.1 459.7,231.1 460.2,220.4 460.8,231.1 461.4,231.1 461.9,231.1 462.5,220.4 463.0,231.1 463.6,231.1 464.2,231.1 464.7,220.4 465.3,231.1 465.8,231.1 466.4,231.1 467.0,220.4 467.5,231.1 468.1,231.1 468.6,231.1 469.2,220.4 469.8,231.1 470.3,231.1 470.9,231.1 471.5,220.4 472.0,231.1 472.6,231.1 473.1,231.1 473.7,220.4 474.3,231.1 474.8,231.1 475.4,231.1 475.9,220.4 476.5,231.1 477.1,231.1 477.6,231.1 478.2,220.4 478.7,231.1 479.3,231.1 479.9,231.1 480.4,220.4 481.0,231.1 481.5,231.1 482.1,231.1 482.7,220.4 483.2,231.1 483.8,231.1 484.3,231.1 484.9,220.4 485.5,231.1 486.0,231.1 486.6,231.1 487.1,220.4 487.7,231.1 488.3,231.1 488.8,231.1 489.4,220.4 489.9,231.1 490.5,231.1 491.1,231.1 491.6,220.4 492.2,231.1 492.8,231.1 493.3,231.1 493.9,220.4 494.4,231.1 495.0,231.1 495.6,231.1 496.1,220.4 496.7,231.1 497.2,231.1 497.8,231.1 498.4,220.4 498.9,231.1 499.5,231.1 500.0,231.1 500.6,220.4 501.2,231.1 501.7,231.1 502.3,231.1 502.8,220.4 503.4,231.1 504.0,231.1 504.5,231.1 505.1,220.4 505.6,231.1 506.2,231.1 506.8,231.1 507.3,220.4 507.9,231.1 508.4,231.1 509.0,231.1 509.6,220.4 510.1,231.1 510.7,231.1 511.3,231.1 511.8,220.4 512.4,231.1 512.9,231.1 513.5,231.1 514.1,220.4 514.6,231.1 515.2,231.1 515.7,231.1 516.3,220.4 516.9,231.1 517.4,231.1 518.0,231.1 518.5,220.4 519.1,231.1 519.7,231.1 520.2,231.1 520.8,220.4 521.3,231.1 521.9,231.1 522.5,231.1 523.0,220.4 523.6,231.1 524.1,231.1 524.7,231.1 525.3,220.4 525.8,231.1 526.4,231.1 526.9,231.1 527.5,220.4 528.1,231.1 528.6,231.1 529.2,231.1 529.7,220.4 530.3,231.1 530.9,231.1 531.4,231.1 532.0,220.4 532.6,231.1 533.1,231.1 533.7,231.1 534.2,220.4 534.8,231.1 535.4,231.1 535.9,231.1 536.5,220.4 537.0,231.1 537.6,231.1 538.2,231.1 538.7,220.4 539.3,231.1 539.8,231.1 540.4,231.1 541.0,220.4 541.5,231.1 542.1,231.1 542.6,231.1 543.2,220.4 543.8,231.1 544.3,231.1 544.9,231.1 545.4,220.4 546.0,231.1 546.6,231.1 547.1,231.1 547.7,220.4 548.2,231.1 548.8,231.1 549.4,231.1 549.9,220.4 550.5,231.1 551.1,231.1 551.6,231.1 552.2,220.4 552.7,231.1 553.3,231.1 553.9,231.1 554.4,220.4 555.0,231.1 555.5,231.1 556.1,231.1 556.7,220.4 557.2,231.1 557.8,231.1 558.3,231.1 558.9,220.4 559.5,231.1 560.0,231.1 560.6,231.1 561.1,220.4 561.7,231.1 562.3,231.1 562.8,231.1 563.4,220.4 563.9,231.1 564.5,231.1 565.1,231.1 565.6,220.4 566.2,231.1 566.7,231.1 567.3,231.1 567.9,220.4 568.4,231.1 569.0,231.1 569.5,231.1 570.1,220.4 570.7,231.1 571.2,231.1 571.8,231.1 572.4,220.4 572.9,231.1 573.5,231.1 574.0,231.1 574.6,220.4 575.2,231.1 575.7,231.1 576.3,231.1 576.8,220.4 577.4,231.1 578.0,231.1 578.5,231.1 579.1,220.4 579.6,231.1 580.2,231.1 580.8,231.1 581.3,220.4 581.9,231.1 582.4,231.1 583.0,231.1 583.6,220.4 584.1,231.1 584.7,231.1 585.2,231.1 585.8,231.1 586.4,231.1 586.9,231.1 587.5,231.1 588.0,231.1 588.6,231.1 589.2,231.1 589.7,231.1 590.3,220.4 590.9,231.1 591.4,231.1 592.0,231.1 592.5,220.4 593.1,231.1 593.7,231.1 594.2,231.1 594.8,220.4 595.3,231.1 595.9,231.1 596.5,231.1 597.0,220.4 597.6,231.1 598.1,231.1 598.7,231.1 599.3,220.4 599.8,231.1 600.4,231.1 600.9,231.1 601.5,220.4 602.1,231.1 602.6,231.1 603.2,231.1 603.7,220.4 604.3,231.1 604.9,231.1 605.4,231.1 606.0,220.4 606.5,231.1 607.1,231.1 607.7,231.1 608.2,231.1 608.8,231.1 609.3,231.1 609.9,231.1 610.5,231.1 611.0,231.1 611.6,231.1 612.2,231.1 612.7,220.4 613.3,231.1 613.8,231.1 614.4,231.1 615.0,220.4 615.5,231.1 616.1,231.1 616.6,231.1 617.2,220.4 617.8,231.1 618.3,231.1 618.9,231.1 619.4,220.4 620.0,231.1" fill="none" stroke="#4a7bd0" stroke-width="1"/>
<line x1="70" y1="338" x2="90" y2="338" stroke="#d0564a"/><text x="96" y="342" font-size="12">실시간 TCG (호스트 부하 높음): p50 82 us, p99 2.9 ms, overrun 157</text>
<line x1="70" y1="320" x2="90" y2="320" stroke="#4a7bd0"/><text x="96" y="324" font-size="12">icount sleep=off (결정적): p50 7 us, 최대 13 us, overrun 0</text>
</svg>
```

그림 5 — 모드 (1) forward/HARD의 1000개 원자료. 파랑(icount sleep=off)은 7 us에 붙어 있고 4개마다 12 us로 튄다. 빨강(실시간 TCG)은 호스트 부하 잡음으로 수백 us~10 ms까지 흔들린다.

출력에서 볼 것:

- **드리프트**: icount에서 forward는 1000주기에 1000.040 ms(마지막 콜백의 늦음 40 us만큼), 상대 재무장은 **1008.879 ms** — 매 주기 약 8.9 us씩 밀린 것이 그대로 쌓였다. 실시간 TCG에서는 상대 재무장이 1444 ms, 즉 **44% 느린 샘플링**이 됐다. 1 kHz라고 생각한 IMU 데이터가 실제로는 692 Hz였다는 뜻이다. overrun이 0인데도 그렇다 — 상대 재무장은 "놓친 주기"라는 개념 자체가 없어서 문제를 숨긴다.
- **forward는 늦어도 격자를 지킨다**: 실시간 TCG에서 forward/HARD는 1157 ms에 1000번 — 1000 + 157(overrun) = 1157주기. 즉 **시간 축은 정확히 유지하고, 못 지킨 주기는 overrun으로 셌다**. 센서 드라이버라면 이 157을 "dropped samples" 카운터로 노출하면 된다.
- **HARD vs SOFT**: SOFT는 hardirq에서 softirq를 올리고, IRQ 출구에서 그걸 처리하는 단계가 더 있어서 icount에서 6 us 더 늦다(7 → 13 us). 최대값 525 us는 softirq가 다른 softirq 처리 뒤에 줄을 선 경우다. 실시간 TCG에서 SOFT의 overrun이 HARD의 두 배(323 vs 157)인 것도 같은 이유다. PREEMPT_RT에서는 softirq가 스레드로 돌기 때문에 차이가 더 커진다.
- **4개마다 12 us**: icount 원자료에서 7 us가 781번, 12 us가 218번 나왔고, 12 us는 샘플 86번부터 **정확히 4개 간격**(86, 90, 94, …)으로 나온다. 4 ms마다 tick hrtimer(`tick_nohz_handler`)가 같은 인터럽트에서 먼저 처리되는 비용으로 보인다. 처음 86개에 없는 것은 그동안 이 CPU의 tick이 멈춰 있었기 때문일 것이다(NO_HZ_IDLE — 콜백 사이에 CPU가 idle이다). **tick 자체가 지터 원인**이라는 것, 그리고 NO_HZ_FULL로 tick을 없애려는 이유를 숫자로 보여 준다.
- 실제 보드의 지터는 이 두 그래프 중 어디에도 해당하지 않는다. IRQ를 오래 끄는 드라이버, 캐시 미스, CPU idle 상태에서 깨어나는 시간(exit latency)이 꼬리를 만든다. 그건 M5(ftrace `irqsoff`·`timerlat`)와 N1에서 실제 측정 방법을 다룬다.

### 9.4 예제 12 — 유저 공간 버전: `clock_nanosleep(TIMER_ABSTIME)` (`f8_nsleep`)

무엇을 확인하나: 유저 공간에서도 같은 차이(상대 vs 절대)가 있는지, 그리고 프로세스별 timer slack(기본 50 us)이 무엇을 바꾸는지.

```c
/* f8_nsleep: 유저 공간 1 kHz 루프 — 상대 nanosleep vs TIMER_ABSTIME */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/prctl.h>

#define N 1000
#define P_NS 1000000LL
static long long late[N];

static long long now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
static int cmp(const void *a, const void *b)
{
	return *(const long long *)a < *(const long long *)b ? -1 : *(const long long *)a > *(const long long *)b;
}

static void run(int absolute)
{
	long long t0 = now_ns(), next = t0;
	for (int i = 0; i < N; i++) {
		next += P_NS;
		struct timespec ts = { next / 1000000000LL, next % 1000000000LL };
		struct timespec rel = { 0, P_NS };
		int r = absolute ? clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL)
				 : clock_nanosleep(CLOCK_MONOTONIC, 0, &rel, NULL);
		if (r) { fprintf(stderr, "clock_nanosleep: %d\n", r); exit(1); }
		long long t = now_ns();
		late[i] = t - next;		/* 이상적인 i번째 샘플 시각 대비 */
		if (!absolute)
			next = t;		/* 상대 모드: 기준이 매번 밀린다 */
	}
	long long total = now_ns() - t0;
	qsort(late, N, sizeof(late[0]), cmp);
	printf("%-8s total=%7.2f ms (ideal %d)  late us: p50=%lld p99=%lld max=%lld\n",
	       absolute ? "ABSTIME" : "relative", total / 1e6, N, late[N / 2] / 1000,
	       late[N * 99 / 100] / 1000, late[N - 1] / 1000);
}

int main(void)
{
	printf("timer slack = %d ns\n", prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0));
	run(0);
	run(1);
	prctl(PR_SET_TIMERSLACK, 1, 0, 0, 0);
	printf("timer slack = %d ns\n", prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0));
	run(0);
	run(1);
	return 0;
}
```

VM 호스트(Linux 7.0, Apple M2 위 가상화 — 실제 실리콘 타이머를 쓴다):

```text
timer slack = 50000 ns
relative total=1095.42 ms (ideal 1000)  late us: p50=66 p99=1013 max=2887
ABSTIME  total=1000.10 ms (ideal 1000)  late us: p50=66 p99=2732 max=4138
timer slack = 1 ns
relative total=1069.38 ms (ideal 1000)  late us: p50=15 p99=1865 max=4323
ABSTIME  total=1000.02 ms (ideal 1000)  late us: p50=15 p99=2976 max=7917
```

QEMU 게스트(Linux 6.18.55, 실시간 TCG):

```text
timer slack = 50000 ns
relative total=1664.12 ms (ideal 1000)  late us: p50=329 p99=4956 max=19418
ABSTIME  total=1000.50 ms (ideal 1000)  late us: p50=347 p99=30651 max=34889
timer slack = 1 ns
relative total=1517.10 ms (ideal 1000)  late us: p50=228 p99=4734 max=10075
ABSTIME  total=1000.11 ms (ideal 1000)  late us: p50=195 p99=16030 max=22381
```

출력에서 볼 것:

- **ABSTIME은 총 시간이 1000 ms에 붙어 있다**(1000.02~1000.50). 상대 모드는 호스트에서 7~10%, 게스트에서 50~66% 길어졌다. 커널의 `hrtimer_forward_now`와 같은 결론이다.
- **timer slack**: 유저 프로세스의 `nanosleep`은 기본으로 50 us의 여유(slack)를 받는다 — hrtimer의 soft~hard 범위로 들어가서, 근처의 다른 타이머와 묶어 깨울 수 있게 한다(전력 절약). 그래서 p50 늦음이 66 us였다. `PR_SET_TIMERSLACK`으로 1 ns로 줄이면 p50이 15 us가 된다. RT 스케줄링 정책(`SCHED_FIFO`) 스레드는 slack이 0으로 취급된다(N2).
- ABSTIME 쪽 p99가 오히려 큰 것은, 한 번 늦으면 다음 몇 샘플도 "격자 대비" 늦게 기록되기 때문이다(따라잡는 중). 상대 모드는 늦음이 매번 리셋되는 대신 총 시간이 늘어난다. **지터 숫자만 보고 상대 모드가 낫다고 판단하면 안 되는 이유**다.
- 호스트 VM도 가상화(HVF) 위라서 p99 1~3 ms는 실제 보드 수치가 아니다. 구조적 결론(ABSTIME은 드리프트 0, slack이 p50을 정한다)만 가져간다.

---

## 10. 지연 함수 — busy-wait vs sleep

### 10.1 두 종류

| 종류 | 함수 | 어떻게 기다리나 | atomic 컨텍스트 | 정확도 |
|---|---|---|---|---|
| busy-wait | `ndelay`, `udelay`, `mdelay` | CPU가 카운터를 보며 루프 (arm64는 `WFE`/`WFIT`로 전력 절약하며 카운터 비교) | **가능** | 짧은 구간에 정확 |
| hrtimer sleep | `usleep_range(min, max)`, `usleep_range_idle` | 태스크를 재우고 hrtimer로 깨움 | 불가 | 정확, 보통 max에 깸 |
| jiffies sleep | `msleep`, `msleep_interruptible`, `ssleep` | `schedule_timeout()` → `timer_list` | 불가 | tick 단위, 4 ms 양자화 |
| 자동 | `fsleep(us)` | 크기에 따라 위 셋 중 하나 | 불가 | 무난 |

6.18의 `Documentation/timers/delay_sleep_functions.rst`(존재 확인함)가 같은 내용을 표로 정리하고 이렇게 권한다:

```text
A generic advice for non atomic contexts could be:

#. Use `fsleep()` whenever unsure (as it combines all the advantages of the
   others)
#. Use `*sleep()` whenever possible
#. Use `usleep_range*()` whenever accuracy of `*sleep()` is not sufficient
#. Use `*delay()` for very, very short delays
```

(문서 원문 그대로.) 같은 문서의 표에서 `*sleep()`의 "long intervals" 정확도는 "max 12.5% slack", `*delay()`는 "Do not use!"다.

### 10.2 6.18의 구현 — 세 함수를 직접 읽기

```c
// kernel/time/sleep_timeout.c (6.18)
void msleep(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs);

	while (timeout)
		timeout = schedule_timeout_uninterruptible(timeout);
}

void __sched usleep_range_state(unsigned long min, unsigned long max, unsigned int state)
{
	ktime_t exp = ktime_add_us(ktime_get(), min);
	u64 delta = (u64)(max - min) * NSEC_PER_USEC;
	...
	for (;;) {
		__set_current_state(state);
		/* Do not return before the requested sleep time has elapsed */
		if (!schedule_hrtimeout_range(&exp, delta, HRTIMER_MODE_ABS))
			break;
	}
}
```

```c
// include/linux/delay.h (6.18)
static const unsigned int max_slack_shift = 2;
#define USLEEP_RANGE_UPPER_BOUND	((TICK_NSEC << max_slack_shift) / NSEC_PER_USEC)

static inline void fsleep(unsigned long usecs)
{
	if (usecs <= 10)
		udelay(usecs);
	else if (usecs < USLEEP_RANGE_UPPER_BOUND)
		usleep_range(usecs, usecs + (usecs >> max_slack_shift));
	else
		msleep(DIV_ROUND_UP(usecs, USEC_PER_MSEC));
}
```

(`usleep_range_state`의 WARN 줄은 `...`로 생략. 나머지는 소스 그대로.)

읽을 것:

- `msleep(1)` = `msecs_to_jiffies(1)` = 1 jiffy → `schedule_timeout(1)` → `timer_list` 1 jiffy → §6.5에 의해 **4~8 ms**. 예전 커널은 여기에 `+ 1`이 더 있었는데(그래서 예전 문서에는 "msleep(1)은 최소 2 jiffies"라고 쓰여 있다), 6.18 코드에는 없다 — wheel의 `calc_index`가 이미 올림을 하기 때문이다. (제거된 정확한 버전은 확인하지 않았다.)
- `usleep_range(min, max)`: hrtimer의 **soft 만료 = min, hard 만료 = max**. 하드웨어 비교기에는 **max**를 프로그램한다. 그 사이에 다른 인터럽트가 와서 hrtimer 코어가 돌면 그때 같이 깨워 준다(묶어 깨우기 → 인터럽트 수 절약). 다른 일이 없으면 **max에 깬다**.
- `fsleep`: HZ=250에서 `USLEEP_RANGE_UPPER_BOUND` = 4 ms × 4 = **16,000 us**. 10 us 이하는 `udelay`, 16 ms 미만은 `usleep_range(us, us × 1.25)`, 그 이상은 `msleep`(이 크기면 tick 양자화 오차가 25% 이하라서).
- `msleep`의 주석은 정확도를 계산하는 공식까지 준다: 레벨 0에서 slack = tick 길이 / 요청 시간. "HZ=250에서 slack을 25% 이하로 하려면 16 ms 이상, 12.5% 이하로 하려면 32 ms 이상이어야 한다."

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="240" y="14" width="200" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="36" text-anchor="middle" font-size="13">드라이버가 기다려야 한다</text>
  <rect x="150" y="74" width="380" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="96" text-anchor="middle" font-size="13">지금 잠들어도(sleep) 되는 컨텍스트인가?</text>
  <text x="340" y="115" text-anchor="middle" font-size="12">spinlock 안 · IRQ · softirq · 타이머 콜백 = 안 됨</text>
  <line x1="340" y1="48" x2="340" y2="72" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="166" width="220" height="80" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="120" y="188" text-anchor="middle" font-size="13">ndelay / udelay</text>
  <text x="120" y="208" text-anchor="middle" font-size="12">CPU가 카운터를 보며 busy-wait</text>
  <text x="120" y="226" text-anchor="middle" font-size="12">수 us까지만 · mdelay는 최후 수단</text>
  <path d="M150,100 L120,100 L120,164" fill="none" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="128" y="152" font-size="12">아니오 (atomic)</text>
  <rect x="380" y="166" width="200" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="480" y="191" text-anchor="middle" font-size="13">얼마나 기다리나?</text>
  <path d="M530,100 L560,100 L560,140 L480,140 L480,164" fill="none" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="568" y="94" font-size="12">예</text>
  <rect x="246" y="250" width="128" height="80" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="310" y="272" text-anchor="middle" font-size="13">10 us 이하</text>
  <text x="310" y="292" text-anchor="middle" font-size="12">udelay</text>
  <text x="310" y="310" text-anchor="middle" font-size="12">잠드는 비용이 더 큼</text>
  <rect x="382" y="250" width="160" height="80" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="462" y="272" text-anchor="middle" font-size="13">10 us ~ 16 ms</text>
  <text x="462" y="292" text-anchor="middle" font-size="12">usleep_range(lo, hi)</text>
  <text x="462" y="310" text-anchor="middle" font-size="12">hrtimer · 보통 hi에 깸</text>
  <rect x="550" y="250" width="122" height="80" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="611" y="272" text-anchor="middle" font-size="13">16 ms 이상</text>
  <text x="611" y="292" text-anchor="middle" font-size="12">msleep</text>
  <text x="611" y="310" text-anchor="middle" font-size="12">timer wheel</text>
  <line x1="440" y1="206" x2="316" y2="248" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="480" y1="206" x2="464" y2="248" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="520" y1="206" x2="606" y2="248" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="250" y="350" width="422" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="461" y="372" text-anchor="middle" font-size="13">모르겠으면 fsleep(us) — 위 세 갈래를 자동 선택</text>
  <text x="461" y="392" text-anchor="middle" font-size="12">경계: 10 us / 16 ms(HZ=250) · usleep_range 상한 = 1.25×하한</text>
  <rect x="10" y="270" width="220" height="80" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
  <text x="120" y="292" text-anchor="middle" font-size="13">정확한 "주기"가 필요하면</text>
  <text x="120" y="312" text-anchor="middle" font-size="12">sleep 반복 대신 hrtimer +</text>
  <text x="120" y="330" text-anchor="middle" font-size="12">hrtimer_forward_now (§9)</text>
</svg>
```

그림 6 — 드라이버에서 기다릴 때 고르는 순서. 첫 질문은 항상 "지금 잠들어도 되나"다.

### 10.3 예제 13 — 네 함수의 실제 소요 시간 (`f8_delay`)

무엇을 확인하나: 1/2/5/10/20 ms를 `msleep`, `usleep_range(us, us×1.25)`, `fsleep`, `mdelay`로 각 40회 기다렸을 때 실제 소요 시간. 매번 tick 위상을 무작위로 섞는다.

```c
// SPDX-License-Identifier: GPL-2.0
/* f8_delay: msleep / usleep_range / fsleep / mdelay 의 실제 소요 시간 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/random.h>

static int n = 40;
module_param(n, int, 0444);
static const unsigned int ms_list[] = { 1, 2, 5, 10, 20 };

static s64 run(int how, unsigned int ms)
{
	unsigned int us = ms * 1000, ph = get_random_u32_below(4000);
	ktime_t t0;

	usleep_range(ph, ph + 10);		/* tick 위상 무작위화 */
	t0 = ktime_get();
	switch (how) {
	case 0: msleep(ms); break;			/* timer_list(jiffies) */
	case 1: usleep_range(us, us + us / 4); break;	/* hrtimer + slack */
	case 2: fsleep(us); break;			/* 크기에 따라 자동 선택 */
	case 3: mdelay(ms); break;			/* busy-wait */
	}
	return ktime_us_delta(ktime_get(), t0);
}

static int __init f8_delay_init(void)
{
	static const char * const name[] = { "msleep", "usleep_range", "fsleep", "mdelay" };
	int h, m, i;

	for (h = 0; h < 4; h++)
		for (m = 0; m < ARRAY_SIZE(ms_list); m++) {
			s64 v, mn = S64_MAX, mx = 0, sum = 0;
			char line[400];
			int len = 0;

			for (i = 0; i < n; i++) {
				v = run(h, ms_list[m]);
				mn = min(mn, v);
				mx = max(mx, v);
				sum += v;
				len += scnprintf(line + len, sizeof(line) - len, " %lld", v);
			}
			pr_info("%s(%ums) min=%lld avg=%lld max=%lld us\n", name[h],
				ms_list[m], mn, div_s64(sum, n), mx);
			pr_info("raw %s %u:%s\n", name[h], ms_list[m], line);
		}
	return 0;
}
module_init(f8_delay_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F8: sleep and delay function durations");
```

**icount sleep=off** (`raw` 줄 생략):

```text
f8_delay: msleep(1ms) min=4095 avg=6438 max=7956 us
f8_delay: msleep(2ms) min=4125 avg=6104 max=7811 us
f8_delay: msleep(5ms) min=8232 avg=9976 max=11877 us
f8_delay: msleep(10ms) min=12134 avg=13893 max=15873 us
f8_delay: msleep(20ms) min=20145 avg=22003 max=24007 us
f8_delay: usleep_range(1ms) min=1106 avg=1276 max=1289 us
f8_delay: usleep_range(2ms) min=2101 avg=2523 max=2564 us
f8_delay: usleep_range(5ms) min=6280 avg=6291 max=6295 us
f8_delay: usleep_range(10ms) min=11127 avg=12505 max=12546 us
f8_delay: usleep_range(20ms) min=23301 avg=25002 max=25046 us
f8_delay: fsleep(1ms) min=1280 avg=1283 max=1344 us
f8_delay: fsleep(2ms) min=2264 avg=2523 max=2539 us
f8_delay: fsleep(5ms) min=6287 avg=6291 max=6296 us
f8_delay: fsleep(10ms) min=12543 avg=12545 max=12546 us
f8_delay: fsleep(20ms) min=20194 avg=21795 max=24009 us
f8_delay: mdelay(1ms) min=1001 avg=1001 max=1001 us
f8_delay: mdelay(2ms) min=2001 avg=2002 max=2027 us
f8_delay: mdelay(5ms) min=5003 avg=5005 max=5042 us
f8_delay: mdelay(10ms) min=10007 avg=10009 max=10044 us
f8_delay: mdelay(20ms) min=20013 avg=20026 max=20337 us
```

**실시간 TCG** (비교용):

```text
f8_delay: msleep(1ms) min=4492 avg=7729 max=19537 us
f8_delay: msleep(2ms) min=4561 avg=6595 max=8130 us
f8_delay: msleep(5ms) min=8296 avg=10661 max=12272 us
f8_delay: msleep(10ms) min=12580 avg=15124 max=20707 us
f8_delay: msleep(20ms) min=20852 avg=23386 max=29467 us
f8_delay: usleep_range(1ms) min=1263 avg=1371 max=1554 us
f8_delay: usleep_range(2ms) min=2362 avg=2897 max=7229 us
f8_delay: usleep_range(5ms) min=6355 avg=6687 max=10508 us
f8_delay: usleep_range(10ms) min=11402 avg=15051 max=52783 us
f8_delay: usleep_range(20ms) min=20656 avg=25694 max=30870 us
f8_delay: fsleep(1ms) min=1322 avg=1696 max=3018 us
f8_delay: fsleep(2ms) min=2161 avg=3089 max=7636 us
f8_delay: fsleep(5ms) min=5829 avg=6668 max=8917 us
f8_delay: fsleep(10ms) min=12696 avg=13160 max=16532 us
f8_delay: fsleep(20ms) min=20395 avg=24107 max=91910 us
f8_delay: mdelay(1ms) min=1001 avg=1319 max=6247 us
f8_delay: mdelay(2ms) min=2002 avg=3140 max=12652 us
f8_delay: mdelay(5ms) min=5006 avg=8705 max=17102 us
f8_delay: mdelay(10ms) min=10247 avg=23827 max=48290 us
f8_delay: mdelay(20ms) min=20065 avg=42452 max=61183 us
```

```svg
<svg viewBox="0 0 640 340" xmlns="http://www.w3.org/2000/svg">
<text x="320.0" y="20" text-anchor="middle" font-size="13">실제 소요 ÷ 요청 시간 (평균 막대, 최소~최대 수염, 각 40회, icount sleep=off)</text>
<line x1="50" y1="280.0" x2="620" y2="280.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="44" y="284.0" text-anchor="end" font-size="12">0×</text>
<line x1="50" y1="250.8" x2="620" y2="250.8" stroke="currentColor" stroke-opacity="0.15"/>
<text x="44" y="254.8" text-anchor="end" font-size="12">1×</text>
<line x1="50" y1="221.5" x2="620" y2="221.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="44" y="225.5" text-anchor="end" font-size="12">2×</text>
<line x1="50" y1="192.2" x2="620" y2="192.2" stroke="currentColor" stroke-opacity="0.15"/>
<text x="44" y="196.2" text-anchor="end" font-size="12">3×</text>
<line x1="50" y1="163.0" x2="620" y2="163.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="44" y="167.0" text-anchor="end" font-size="12">4×</text>
<line x1="50" y1="133.8" x2="620" y2="133.8" stroke="currentColor" stroke-opacity="0.15"/>
<text x="44" y="137.8" text-anchor="end" font-size="12">5×</text>
<line x1="50" y1="104.5" x2="620" y2="104.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="44" y="108.5" text-anchor="end" font-size="12">6×</text>
<line x1="50" y1="75.2" x2="620" y2="75.2" stroke="currentColor" stroke-opacity="0.15"/>
<text x="44" y="79.2" text-anchor="end" font-size="12">7×</text>
<line x1="50" y1="46.0" x2="620" y2="46.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="44" y="50.0" text-anchor="end" font-size="12">8×</text>
<line x1="50" y1="250.8" x2="620" y2="250.8" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<line x1="50" y1="46" x2="50" y2="280" stroke="currentColor"/><line x1="50" y1="280" x2="620" y2="280" stroke="currentColor"/>
<text x="107.0" y="298" text-anchor="middle" font-size="13">1 ms</text>
<rect x="62.9" y="91.7" width="20.5" height="188.3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<line x1="73.1" y1="160.2" x2="73.1" y2="47.3" stroke="currentColor"/>
<rect x="85.5" y="242.7" width="20.5" height="37.3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="95.7" y1="247.6" x2="95.7" y2="242.3" stroke="currentColor"/>
<rect x="108.0" y="242.5" width="20.5" height="37.5" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="118.3" y1="242.6" x2="118.3" y2="240.7" stroke="currentColor"/>
<rect x="130.6" y="250.7" width="20.5" height="29.3" fill="#888" fill-opacity="0.35" stroke="#888"/>
<line x1="140.9" y1="250.7" x2="140.9" y2="250.7" stroke="currentColor"/>
<text x="221.0" y="298" text-anchor="middle" font-size="13">2 ms</text>
<rect x="176.9" y="190.7" width="20.5" height="89.3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<line x1="187.1" y1="219.7" x2="187.1" y2="165.8" stroke="currentColor"/>
<rect x="199.5" y="243.1" width="20.5" height="36.9" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="209.7" y1="249.3" x2="209.7" y2="242.5" stroke="currentColor"/>
<rect x="222.0" y="243.1" width="20.5" height="36.9" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="232.3" y1="246.9" x2="232.3" y2="242.9" stroke="currentColor"/>
<rect x="244.6" y="250.7" width="20.5" height="29.3" fill="#888" fill-opacity="0.35" stroke="#888"/>
<line x1="254.9" y1="250.7" x2="254.9" y2="250.4" stroke="currentColor"/>
<text x="335.0" y="298" text-anchor="middle" font-size="13">5 ms</text>
<rect x="290.9" y="221.6" width="20.5" height="58.4" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<line x1="301.1" y1="231.8" x2="301.1" y2="210.5" stroke="currentColor"/>
<rect x="313.5" y="243.2" width="20.5" height="36.8" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="323.7" y1="243.3" x2="323.7" y2="243.2" stroke="currentColor"/>
<rect x="336.0" y="243.2" width="20.5" height="36.8" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="346.3" y1="243.2" x2="346.3" y2="243.2" stroke="currentColor"/>
<rect x="358.6" y="250.7" width="20.5" height="29.3" fill="#888" fill-opacity="0.35" stroke="#888"/>
<line x1="368.9" y1="250.7" x2="368.9" y2="250.5" stroke="currentColor"/>
<text x="449.0" y="298" text-anchor="middle" font-size="13">10 ms</text>
<rect x="404.9" y="239.4" width="20.5" height="40.6" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<line x1="415.1" y1="244.5" x2="415.1" y2="233.6" stroke="currentColor"/>
<rect x="427.5" y="243.4" width="20.5" height="36.6" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="437.7" y1="247.5" x2="437.7" y2="243.3" stroke="currentColor"/>
<rect x="450.0" y="243.3" width="20.5" height="36.7" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="460.3" y1="243.3" x2="460.3" y2="243.3" stroke="currentColor"/>
<rect x="472.6" y="250.7" width="20.5" height="29.3" fill="#888" fill-opacity="0.35" stroke="#888"/>
<line x1="482.9" y1="250.7" x2="482.9" y2="250.6" stroke="currentColor"/>
<text x="563.0" y="298" text-anchor="middle" font-size="13">20 ms</text>
<rect x="518.9" y="247.8" width="20.5" height="32.2" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<line x1="529.1" y1="250.5" x2="529.1" y2="244.9" stroke="currentColor"/>
<rect x="541.5" y="243.4" width="20.5" height="36.6" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="551.7" y1="245.9" x2="551.7" y2="243.4" stroke="currentColor"/>
<rect x="564.0" y="248.1" width="20.5" height="31.9" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="574.3" y1="250.5" x2="574.3" y2="244.9" stroke="currentColor"/>
<rect x="586.6" y="250.7" width="20.5" height="29.3" fill="#888" fill-opacity="0.35" stroke="#888"/>
<line x1="596.9" y1="250.7" x2="596.9" y2="250.3" stroke="currentColor"/>
<rect x="70" y="310" width="14" height="10" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="90" y="319" font-size="12">msleep</text>
<rect x="165.0" y="310" width="14" height="10" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><text x="185.0" y="319" font-size="12">usleep_range</text>
<rect x="305.0" y="310" width="14" height="10" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="325.0" y="319" font-size="12">fsleep</text>
<rect x="400.0" y="310" width="14" height="10" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="420.0" y="319" font-size="12">mdelay</text>
<line x1="495.0" y1="315" x2="515.0" y2="315" stroke="#3f9a6b" stroke-dasharray="4 3"/><text x="521.0" y="319" font-size="12">요청과 같음 (1×)</text>
</svg>
```

그림 7 — icount sleep=off 결과를 "실제 ÷ 요청" 배수로 그렸다. `msleep(1)`은 평균 6.4배를 잔다. `usleep_range`와 `fsleep`은 슬랙 상한인 1.25배 근처, `mdelay`는 정확히 1배.

출력에서 볼 것:

- **`msleep(1)`은 4.1~8.0 ms(평균 6.4 ms)를 잔다.** `msleep(2)`도 같은 분포다(둘 다 1 jiffy). HZ=250 커널에서 "1 ms 기다려"를 `msleep(1)`로 쓰면 평균 6배 넘게 기다린다는 뜻이고, 이걸 루프 안에서 100번 돌면 0.1초가 아니라 0.64초가 된다. 펌웨어 업데이트나 센서 초기화 시퀀스가 "생각보다 몇 배 느린" 흔한 이유다.
- **`usleep_range(us, us×1.25)`는 거의 항상 상한(1.25배)에 깬다**: 5 ms → 6.28~6.30 ms, 10 ms → 12.5 ms, 20 ms → 25.0 ms. §10.2에서 본 대로 비교기에 max를 쓰기 때문이다. 1 ms·2 ms·10 ms의 최소값(1.106, 2.101, 11.127 ms)은 그 사이 다른 인터럽트(tick)가 와서 soft 만료(min) 이후 일찍 깨워 준 경우다. **정확히 X us를 원하면 범위를 좁게 준다** (`usleep_range(1000, 1050)`).
- **`fsleep(20ms)`은 `msleep(20)`과, `fsleep(1~10ms)`은 `usleep_range`와 같은 숫자**다. 16 ms 경계 그대로다.
- **`mdelay`는 icount에서 정확히 1.000배.** 하지만 실시간 TCG에서는 `mdelay(10)`이 평균 23.8 ms, 최대 48 ms가 됐다 — busy-wait 중인 vCPU가 호스트에서 밀린 만큼 그대로 늘어난다. 실제 보드에서도 `mdelay` 도중 인터럽트가 오면 그만큼 늘어난다(선점은 안 돼도 IRQ는 막지 않는다). 그리고 그동안 **그 CPU는 아무것도 못 한다** — 10 ms `mdelay`는 다른 태스크에게 10 ms짜리 지연 스파이크다.
- 실시간 TCG의 `usleep_range(10ms)` 최대 52.8 ms, `fsleep(20ms)` 최대 91.9 ms 같은 꼬리는 호스트 잡음이다.

### 10.4 함정 정리

- **atomic 컨텍스트에서 `msleep`/`usleep_range`** → `BUG: scheduling while atomic` (E4 §5, F2). 디버그 커널(`CONFIG_DEBUG_ATOMIC_SLEEP`)이면 `might_sleep()` 경고가 먼저 난다.
- **긴 `mdelay`를 probe에** → 부팅 시간이 그대로 늘고(C7) 다른 태스크가 굶는다. 데이터시트의 "전원 인가 후 50 ms 대기"는 `msleep(50)`이나 `fsleep(50000)`.
- **`udelay`에 큰 값** → 6.18의 `include/asm-generic/delay.h`(arm64가 그대로 씀)는 상수 인자가 `DELAY_CONST_MAX`(20000 us) 이상이면 존재하지 않는 함수 `__bad_udelay()`를 불러서 **링크 에러**를 낸다(소스로 확인, 빌드해 보지는 않았다). 같은 파일 주석은 "`MAX_UDELAY_MS`보다 길면 `mdelay()`를 쓰라"고 하고, `*delay()`가 **일찍 반환할 수도 있다**(loops_per_jiffy 보정 오차·캐시·CPU 클럭 변화)고 경고한다. arm64는 카운터를 보며 기다리므로 그 위험이 작지만, 데이터시트의 최소 대기 시간에는 여유를 더 준다.
- **`usleep_range(x, x)`** — 범위 0이면 묶어 깨우기를 못 해서 인터럽트가 늘어난다. 정말 필요한 경우가 아니면 여유를 준다.

---

## 11. 커널 안에서는 무슨 일이 일어나나

`msleep(5)`, `usleep_range(1000, 1250)`, `mod_timer()`, `hrtimer_start()`가 실제로 무엇을 거쳐 돌아오는지 한 장으로 모으면 이렇다.

```svg
<svg viewBox="0 0 680 490" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="120" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="70" y="30" text-anchor="middle" font-size="13">하드웨어</text>
  <rect x="140" y="10" width="190" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="235" y="30" text-anchor="middle" font-size="13">hardirq (IRQ 꺼짐)</text>
  <rect x="340" y="10" width="180" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="430" y="30" text-anchor="middle" font-size="13">softirq</text>
  <rect x="530" y="10" width="140" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="600" y="30" text-anchor="middle" font-size="13">프로세스</text>
  <line x1="335" y1="44" x2="335" y2="390" stroke="currentColor" stroke-opacity="0.2"/>
  <line x1="135" y1="44" x2="135" y2="390" stroke="currentColor" stroke-opacity="0.2"/>
  <line x1="525" y1="44" x2="525" y2="390" stroke="currentColor" stroke-opacity="0.2"/>
  <rect x="10" y="60" width="120" height="48" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="70" y="80" text-anchor="middle" font-size="12">CNTVCT ≥ CVAL</text>
  <text x="70" y="98" text-anchor="middle" font-size="12">INTID 27 → GIC</text>
  <rect x="140" y="60" width="190" height="48" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="235" y="80" text-anchor="middle" font-size="12">arch_timer_handler_virt</text>
  <text x="235" y="98" text-anchor="middle" font-size="12">→ evt->event_handler</text>
  <line x1="130" y1="84" x2="138" y2="84" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="140" y="124" width="190" height="48" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="235" y="144" text-anchor="middle" font-size="12">hrtimer_interrupt()</text>
  <text x="235" y="162" text-anchor="middle" font-size="12">만료된 hrtimer를 순서대로</text>
  <line x1="235" y1="108" x2="235" y2="122" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="140" y="188" width="190" height="48" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="235" y="208" text-anchor="middle" font-size="12">HARD hrtimer 콜백</text>
  <text x="235" y="226" text-anchor="middle" font-size="12">f8_hrt_fn, hrtimer_wakeup</text>
  <line x1="235" y1="172" x2="235" y2="186" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="140" y="252" width="190" height="64" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="235" y="272" text-anchor="middle" font-size="12">tick_nohz_handler (tick)</text>
  <text x="235" y="290" text-anchor="middle" font-size="12">jiffies 갱신 · 시간 통계</text>
  <text x="235" y="308" text-anchor="middle" font-size="12">TIMER_SOFTIRQ 올림</text>
  <line x1="235" y1="236" x2="235" y2="250" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="140" y="332" width="190" height="48" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="235" y="352" text-anchor="middle" font-size="12">다음 가장 이른 만료로</text>
  <text x="235" y="370" text-anchor="middle" font-size="12">CVAL 재프로그램</text>
  <line x1="235" y1="316" x2="235" y2="330" stroke="currentColor" marker-end="url(#ah5)"/>
  <path d="M140,356 L70,356 L70,110" fill="none" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#ah5)"/>
  <rect x="340" y="252" width="180" height="64" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="430" y="272" text-anchor="middle" font-size="12">run_timer_softirq</text>
  <text x="430" y="290" text-anchor="middle" font-size="12">→ expire_timers</text>
  <text x="430" y="308" text-anchor="middle" font-size="12">timer_list 콜백</text>
  <line x1="330" y1="300" x2="338" y2="300" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="340" y="332" width="180" height="48" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="430" y="352" text-anchor="middle" font-size="12">hrtimer_run_softirq</text>
  <text x="430" y="370" text-anchor="middle" font-size="12">SOFT hrtimer 콜백</text>
  <rect x="530" y="188" width="140" height="48" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="600" y="208" text-anchor="middle" font-size="12">wake_up_process</text>
  <text x="600" y="226" text-anchor="middle" font-size="12">usleep_range 복귀</text>
  <line x1="330" y1="212" x2="528" y2="212" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#ah5)"/>
  <rect x="530" y="252" width="140" height="64" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="600" y="272" text-anchor="middle" font-size="12">process_timeout</text>
  <text x="600" y="290" text-anchor="middle" font-size="12">→ wake_up_process</text>
  <text x="600" y="308" text-anchor="middle" font-size="12">msleep 복귀</text>
  <line x1="520" y1="284" x2="528" y2="284" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#ah5)"/>
  <text x="340" y="412" text-anchor="middle" font-size="12">softirq는 IRQ 출구(irq_exit)에서 실행되고, 밀리면 ksoftirqd 스레드로 넘어간다</text>
  <text x="340" y="432" text-anchor="middle" font-size="12">→ timer_list 콜백도, SOFT hrtimer 콜백도 "잠들 수 없는" atomic 컨텍스트</text>
  <text x="340" y="470" text-anchor="middle" font-size="12">점선 화살표 = 깨우기 · 재프로그램 (다음 인터럽트를 예약)</text>
</svg>
```

그림 8 — 타이머 인터럽트 한 번이 처리되는 경로. hardirq 단계에서 hrtimer들(tick 포함)이 돌고, tick이 올린 `TIMER_SOFTIRQ`에서 `timer_list` 콜백이 돈다. 잠든 태스크는 그 콜백이 깨운다.

단계별로:

1. **무장**: `mod_timer()`는 per-CPU `timer_base`의 wheel 버킷에 넣는다(`calc_wheel_index`). `hrtimer_start()`는 per-CPU `hrtimer_cpu_base`의 rbtree에 넣고, 이게 가장 이른 만료면 **바로 clockevent를 재프로그램**한다(`tick_program_event` → `arch_timer_set_next_event_virt` → `CNTV_CVAL_EL0` 쓰기).
2. **인터럽트**: 카운터가 CVAL에 도달 → INTID 27 → GIC → `arch_timer_handler_virt()` → `evt->event_handler`, high-res 모드에서는 `hrtimer_interrupt()`.
3. **hardirq에서 hrtimer 실행**: `__hrtimer_run_queues()`가 만료된 HARD hrtimer 콜백을 순서대로 부른다. 여기에 `tick_nohz_handler()`(tick), `hrtimer_wakeup()`(`usleep_range`·`nanosleep`으로 잠든 태스크 깨우기), 내 `f8_hrt_fn`이 있다. SOFT hrtimer는 `HRTIMER_SOFTIRQ`만 올린다.
4. **tick**: `tick_nohz_handler()` → `tick_sched_do_timer()`(jiffies 담당 CPU면 `jiffies_64` 증가) → `tick_sched_handle()` → `update_process_times()` → `run_local_timers()` → 만료된 wheel 버킷이 있으면 `TIMER_SOFTIRQ`를 올린다. 스케줄러 `sched_tick()`도 여기서 불린다.
5. **재프로그램**: `hrtimer_interrupt()`는 트리에서 다음으로 이른 만료를 찾아 비교기를 다시 쓴다. NO_HZ idle이면 다음 tick 대신 "다음 할 일"까지 한 번에 건다.
6. **softirq**: IRQ 출구(`irq_exit`)에서 `run_timer_softirq()` → `__run_timers()` → `expire_timers()` → `timer_list` 콜백. `msleep`이 쓰는 콜백은 `process_timeout()`이고, 6.18에서는 이렇게 생겼다 — `timer_container_of`의 실사용 예다.

```c
// kernel/time/sleep_timeout.c (6.18)
static void process_timeout(struct timer_list *t)
{
	struct process_timer *timeout = timer_container_of(timeout, t, timer);

	wake_up_process(timeout->task);
}
```

7. **깨어남**: 깨워진 태스크는 런큐에 들어가고, 스케줄러가 고르면 `schedule()`에서 돌아와 `msleep()`/`usleep_range()`가 반환된다. **콜백 시각과 태스크가 실제로 다시 도는 시각 사이**에는 스케줄링 지연이 더 있다(이 노트의 측정은 콜백 시각까지만 잰 것이고, `f8_delay`는 태스크 복귀까지 잰 것이다).

드라이버를 쓸 때 다시 만나는 지점:

| 지점 | 다시 나오는 곳 |
|---|---|
| 타이머 콜백 = atomic | F2(실행 컨텍스트), F4(콜백과 공유하는 데이터는 `spin_lock_bh`/`spin_lock_irqsave`) |
| 콜백에서 I2C를 읽고 싶다 | F7 — 콜백에서는 `schedule_work()`만 하고 workqueue에서 I2C |
| `wake_up_process`·`complete` | F3(잠자기와 깨우기) |
| 비교기 인터럽트·GIC·PPI | F6, H6 |
| `readl_poll_timeout` | H1(MMIO 접근) |
| tick·NO_HZ_FULL·`timerlat` | N1(실시간), M5(ftrace) |
| IIO 버퍼 타임스탬프, V4L2 timestamp | I(서브시스템), Q3(동기화) |

---

## 12. Embedded 실무에서 다시 보기

### 12.1 장면 1 — IMU를 1 kHz로 폴링해야 하는데 데이터가 700 Hz로 들어온다

예를 들어 센서 키트의 SPI IMU가 FIFO도 인터럽트도 없는 저가형이라 폴링해야 한다고 하자. 첫 구현은 보통 이렇다: 커널 스레드에서 `while (!kthread_should_stop()) { read_imu(); usleep_range(1000, 1000); }`. 결과는 §9.3의 "now+P" 모드 — 매 주기가 SPI 전송 시간 + 깨어나는 지연만큼 밀려서 1 kHz가 아니라 수백 Hz가 된다. 고치는 순서:

1. **절대 시각 기준**: hrtimer `HRTIMER_MODE_ABS_HARD` + `hrtimer_forward_now()`로 1 ms 격자를 만들고, 콜백에서는 SPI를 직접 하지 않고(atomic) 스레드를 깨운다. 스레드는 깨워진 시각(= 격자 시각)을 타임스탬프로 쓰고 SPI 읽기를 한다. forward의 반환값 − 1을 드롭 카운터로 노출한다.
2. 스레드에 `SCHED_FIFO` 우선순위를 주고(N2), 필요하면 전용 코어(`isolcpus`, `nohz_full`)를 준다(N3).
3. 더 근본적으로는 **센서의 FIFO + 워터마크 인터럽트**를 쓰고 각 샘플 시각은 ODR로 역산한다. 1 kHz 폴링을 Linux에 시키는 것보다 센서 자체 타이밍이 훨씬 정확하다(N4 — 무엇을 MCU/센서에 내릴지).

### 12.2 장면 2 — "부팅이 2초 느려졌다" → probe의 `msleep(1)` 루프

예를 들어 카메라 센서 드라이버가 레지스터 테이블 300개를 쓰면서 항목마다 `msleep(1)`을 넣었다(데이터시트의 "write 후 1 ms 대기"). 개발 보드 커널은 HZ=1000이라 300 × 1~2 ms = 0.3~0.6초였는데, 양산 커널을 HZ=250으로 바꾸자 300 × 평균 6.4 ms = **1.9초**가 됐다(§10.3의 측정값). `fsleep(1000)`이나 `usleep_range(1000, 1200)`으로 바꾸면 300 × 1.2 ms = 0.36초. 같은 이유로 **HZ를 바꾸면 드라이버 타이밍이 바뀐다** — `msleep` 숫자를 "정확한 시간"으로 믿는 코드는 HZ 변경에 약하다.

### 12.3 장면 3 — 센서 퓨전 팀이 "IMU 타임스탬프가 가끔 뒤로 간다"고 한다

원인 후보 1순위: 타임스탬프를 `CLOCK_REALTIME`으로 찍고 있다(§8.2). 장비에 GNSS나 NTP가 붙어서 시각이 맞춰지는 순간 값이 점프한다. IIO 장치라면 `echo monotonic > /sys/bus/iio/devices/iio:device0/current_timestamp_clock`, 직접 짠 드라이버라면 `ktime_get()`으로 바꾼다. 2순위: 인터럽트가 아니라 **읽은 시각**(스레드에서 늦게)으로 찍어서 순서가 섞인다 — hardirq 핸들러 첫 줄에서 찍는다(`request_threaded_irq`의 primary handler, F6).

### 12.4 장면 4 — 32비트 SoC에서 "부팅 5분 뒤에 한 번" 나는 I2C 타임아웃

§5.3 그대로다. 예를 들어 Cortex-A7 기반 32비트 게이트웨이에서 `if (jiffies > timeout)`으로 쓴 코드가 부팅 300초 근처에 걸리면 즉시 타임아웃이 난다. 64비트 개발 보드에서는 재현이 안 된다. 코드 리뷰에서 `jiffies`가 `>`·`<`와 함께 쓰인 줄을 찾아 `time_after()`로 바꾸는 것이 처방이고, `checkpatch.pl`도 일부 패턴을 잡아 준다(확인하지 않았다 — `scripts/checkpatch.pl`에서 "time_after" 검색).

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `if (jiffies > timeout)` | 32비트에서 부팅 5분쯤 오동작 1회 | wraparound (§5.3) | `time_after(jiffies, timeout)`, 변수는 `unsigned long` |
| HZ=250에서 `msleep(1)` 루프 | 초기화·펌웨어 다운로드가 몇 배 느림 | 1 jiffy + 올림 → 4~8 ms (§10.3) | `fsleep(1000)` 또는 `usleep_range(1000, 1200)` |
| 타이머 콜백에서 `mutex_lock`·I2C·`msleep` | `BUG: scheduling while atomic` | 콜백은 softirq/hardirq (§7.5 트레이스) | 콜백은 `schedule_work()`만, 실제 일은 workqueue (F7) |
| 모듈 해제 때 `timer_delete()`만 | 언로드 후 Oops (콜백이 해제된 코드·메모리 실행) | 다른 CPU에서 콜백 실행 중 | `timer_shutdown_sync()` 또는 `timer_delete_sync()`, hrtimer는 `hrtimer_cancel()` |
| 주기 타이머를 `jiffies + P`·`now + P`로 재무장 | 샘플레이트가 목표보다 낮고 부하에 따라 변함 | 콜백 지연이 누적 (§9.3: 1000주기에 +44%) | `expires + P` 또는 `hrtimer_forward_now()` |
| 정밀 주기를 `timer_list`로 | 1 ms 주기가 4 ms 이상, 들쭉날쭉 | wheel은 tick 단위 (§7.4) | hrtimer |
| 센서 타임스탬프를 `ktime_get_real()` | 타임스탬프가 뒤로 점프 | NTP·GNSS·`date`가 REALTIME을 바꿈 | `ktime_get()`(MONOTONIC), IIO `current_timestamp_clock` |
| probe에 `mdelay(50)` | 부팅 지연, 그 CPU의 다른 태스크 50 ms 정지 | busy-wait | `msleep(50)`/`fsleep(50000)` |
| 옛 튜토리얼의 `from_timer`·`del_timer_sync`·`hrtimer_init` | 6.18 빌드 실패 (예제 8) | 이름 변경·제거 | `timer_container_of`·`timer_delete_sync`·`hrtimer_setup` |
| 디버그 커널에서 타이밍 측정 | hrtimer가 수십~수백 us 늦게 보임 | lockdep 등으로 `ktime_get` 자체가 느림 (§8.3) | 양산 설정 커널에서 측정 |

---

## 14. 면접에서 이렇게 말한다

**Q.** What is the difference between jiffies-based timers and hrtimers? When would you use each?

**A.** `timer_list`는 jiffies(tick) 단위로 timer wheel에 들어가서 넣고 빼기가 O(1)이고 싸지만, HZ=250이면 4 ms 단위로 반올림되고 절대 일찍 울리지 않도록 올림까지 해서 1 ms를 걸면 4~8 ms 뒤에 울린다. 대부분 취소되는 타임아웃(통신 응답 대기, 워치독)에 쓴다. hrtimer는 ns 단위 만료를 rbtree에 넣고 하드웨어 비교기에 바로 프로그램해서 정확하다. 주기 샘플링, 짧은 sleep(`usleep_range`), 정확한 시각이 필요한 곳에 쓴다.

> timer_list timers live in the timer wheel and are jiffy-based — cheap O(1) insert and remove, but quantized to the tick. With HZ=250, a 1 ms timer actually fires 4 to 8 ms later, because the wheel rounds up so it never fires early. I measured exactly that range in QEMU. They're ideal for timeouts that usually get cancelled. hrtimers keep a nanosecond expiry in a per-CPU rbtree and program the clock event device directly, so a 1 ms hrtimer fires at 1 ms plus interrupt latency. I use them for periodic sampling and short precise sleeps.

**꼬리 질문**: "timer_list 콜백은 어떤 컨텍스트에서 도나?" → softirq(TIMER_SOFTIRQ). hrtimer는 기본 hardirq, `_SOFT`면 softirq. 둘 다 잠들 수 없다.

**Q.** Why does `msleep(1)` sleep much longer than 1 ms, and what should you use instead?

**A.** `msleep`은 jiffies 기반 `schedule_timeout`이라 1 ms가 1 jiffy로 올림되고, wheel이 한 칸 더 올려서 HZ=250이면 4~8 ms, 평균 6 ms 넘게 잔다. 측정해 보니 평균 6.4 ms였다. 짧은 지연에는 hrtimer 기반 `usleep_range(min, max)`를 쓰고, 모르겠으면 `fsleep()`이 크기에 따라 `udelay`/`usleep_range`/`msleep`을 골라 준다.

> msleep is jiffy-based: one millisecond rounds up to one jiffy, and the timer wheel adds another tick so it never wakes early. At HZ=250 that's 4 to 8 ms — I measured an average of 6.4 ms. For short delays I use usleep_range, which is hrtimer-based, or fsleep, which picks udelay, usleep_range or msleep depending on the duration, as recommended in the kernel's delay and sleep documentation.

**Q.** Can you call `mdelay()` or `msleep()` in an interrupt handler?

**A.** `msleep`은 안 된다 — 스케줄러를 부르니 atomic 컨텍스트에서 "scheduling while atomic"이 난다. `mdelay`/`udelay`는 busy-wait라 기술적으로는 되지만, IRQ 핸들러에서 ms 단위로 도는 건 그 CPU의 모든 인터럽트와 태스크를 막는 지연 스파이크라서 수 us까지만 쓴다. 긴 대기가 필요하면 threaded IRQ나 workqueue로 옮긴다.

> msleep, no — it schedules, so in atomic context you get "scheduling while atomic". udelay and mdelay are busy-waits, so they're legal, but spinning for milliseconds in a hard IRQ stalls that CPU for everyone, so I keep it to a few microseconds. If the hardware needs a longer wait, I move that part into a threaded IRQ handler or a workqueue where I can sleep.

**Q.** How do you implement a precise periodic task in the kernel, say sampling a sensor at 1 kHz?

**A.** hrtimer를 `HRTIMER_MODE_ABS_HARD`로 걸고, 콜백에서 `hrtimer_forward_now(timer, period)`로 다음 만료를 "이전 만료 + 주기"로 잡은 뒤 `HRTIMER_RESTART`를 돌려준다. 이러면 콜백이 늦어도 드리프트가 쌓이지 않고, 반환값으로 놓친 주기를 셀 수 있다. "now + 주기"로 재무장하면 1000주기에 지연이 누적된다 — QEMU에서 재 보니 부하가 있을 때 44% 느려졌다. 콜백은 atomic이니 SPI/I2C는 스레드에서 한다.

> I'd use an hrtimer in absolute hard mode and, in the callback, call hrtimer_forward_now with the period and return HRTIMER_RESTART. That keeps the expiry on a fixed grid, so latency doesn't accumulate, and the return value tells me how many periods I missed, which I expose as a dropped-sample counter. Re-arming relative to "now" drifts: in my test, a thousand 1 ms periods took 44 percent longer under load. Since the callback is atomic, the actual SPI transfer happens in a high-priority thread the callback wakes.

**꼬리 질문**: "그래도 1 kHz가 안정적이지 않다면?" → PREEMPT_RT, SCHED_FIFO 스레드, 코어 격리, 아니면 센서 FIFO + 워터마크 인터럽트로 타이밍을 센서에 맡긴다.

**Q.** What is jiffies wraparound and how do you handle it?

**A.** jiffies는 `unsigned long` 카운터라 32비트에서는 2^32 tick 뒤에 0으로 돈다. 커널은 이걸 일부러 부팅 5분 뒤에 일어나도록 `INITIAL_JIFFIES`를 −300×HZ로 시작한다. `jiffies > timeout`처럼 직접 비교하면 wrap 근처에서 틀리고, `time_after()`는 차이를 부호 있는 정수로 보기 때문에 반 바퀴 이내면 항상 맞다.

> jiffies is an unsigned long, so on 32-bit it wraps after 2^32 ticks, and the kernel deliberately starts it at minus 300 seconds' worth so the first wrap happens five minutes after boot and buggy code fails early. A direct comparison like jiffies greater than timeout breaks across the wrap; time_after does the subtraction and checks the sign, so it's correct as long as the two values are within half the range.

**Q.** Explain clocksource vs clock event device.

**A.** clocksource는 "지금 몇 시인가"를 주는 free-running 카운터이고 읽기만 한다 — arm64에서는 generic timer의 CNTVCT_EL0(`arch_sys_counter`). timekeeping이 이걸 ns로 환산해서 `ktime_get()`을 만든다. clockevent는 "이 시각에 인터럽트를 달라"는 CPU별 비교기다 — `arch_sys_timer`, CNTV_CVAL_EL0와 PPI. hrtimer와 tick이 다음 만료를 여기에 프로그램한다. MCU로 치면 free-running 카운터와 비교 일치 인터럽트다.

> A clocksource is a free-running counter you only read — on arm64 that's the generic timer's virtual count register, registered as arch_sys_counter — and timekeeping converts it to nanoseconds for ktime_get. A clock event device is a per-CPU comparator that raises an interrupt at a programmed time — arch_sys_timer, which uses the virtual compare register and a private peripheral interrupt. The hrtimer core, including the tick, programs the next expiry into it. In MCU terms it's the free-running counter versus the compare-match interrupt.

**Q.** Which clock should you use for sensor timestamps, and why?

**A.** `CLOCK_MONOTONIC`(`ktime_get()`)이다. REALTIME은 NTP·GNSS·사용자 설정으로 점프하니 퓨전 알고리즘이 음수 dt를 볼 수 있다. MONOTONIC은 시스템 전체가 공유하는 하나의 축이라 카메라·IMU·LiDAR를 같은 축에서 맞출 수 있다. suspend하는 장비면 BOOTTIME도 고려한다. 그리고 hardirq 핸들러 맨 앞에서 찍는다. IIO와 evdev는 기본이 REALTIME이라 설정을 바꿔야 한다.

> CLOCK_MONOTONIC, via ktime_get in the kernel. Realtime can jump when NTP or GNSS sets the clock, and a fusion filter that sees a negative delta-t breaks. Monotonic is one shared axis for the camera, IMU and LiDAR timestamps. If the device suspends, I'd consider BOOTTIME. I capture the timestamp at the very top of the hard IRQ handler, and I check the subsystem defaults — IIO and evdev default to realtime, so I switch them to monotonic.

**Q.** What does NO_HZ do, and why would a robotics system care?

**A.** NO_HZ_IDLE는 idle CPU의 tick을 멈춰서 전력을 아끼고(QEMU에서 idle 2초 동안 tick 인터럽트가 500번이 아니라 30여 번), NO_HZ_FULL은 태스크 하나만 도는 CPU의 tick까지 멈춘다. 1 kHz 샘플링을 재 보면 4 ms마다 tick 처리 때문에 지연이 튀는 게 보이는데, 제어 루프 전용 코어에서 `nohz_full`·`isolcpus`로 이런 간섭을 없앤다. 다만 `nohz_full`은 커널 설정만으론 안 켜지고 부팅 인자가 필요하다.

> NO_HZ_IDLE stops the periodic tick on idle CPUs, which saves power — in my test an idle CPU took about 30 timer interrupts in two seconds instead of 500. NO_HZ_FULL also stops it on CPUs running a single task. In a 1 kHz sampling test I could see a small latency bump every fourth sample, exactly when the 4 ms tick was processed in the same interrupt. For a control loop you isolate a core with isolcpus and nohz_full to remove that kind of interference — and nohz_full needs the boot parameter, not just the config option.

---

## 15. 직접 해보기

1. **개념**: HZ=1000 커널에서 `mod_timer(t, jiffies + msecs_to_jiffies(3))`는 몇 ms 뒤에 울릴 수 있나? 같은 걸 HZ=100에서는? — 정답/힌트: `calc_index`의 +1 규칙 — HZ=1000: 3 jiffies + 1 → 3~4 ms. HZ=100: `msecs_to_jiffies(3)` = 1 → 버킷 J+2 → 10~20 ms.
2. **코드**: `f8_poll`을 hrtimer 버전으로 바꿔라(`HRTIMER_MODE_ABS_SOFT`, `hrtimer_forward_now`, 100 ms). 콜백 컨텍스트가 `in_serving_softirq()=1`인지, `_HARD`로 바꾸면 `in_hardirq()`가 1인지 확인하라. — 정답/힌트: `hrtimer_setup(&d->h, fn, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_SOFT)`, 해제는 `hrtimer_cancel()`. 구조체에서 찾기는 `container_of(h, struct f8_dev, h)`.
3. **측정**: `f8_lat`에 `HRTIMER_MODE_REL_SOFT`를 추가해 HARD와 비교하라. icount sleep=off에서 몇 us 차이가 나나? §9.3의 7 → 13 us와 비교하라. — 정답/힌트: `periods` 루프에 kind 2를 추가하고 `hrtimer_setup`을 kind별로 다시 부른다.
4. **/proc 관찰**: 게스트에서 `sleep 10 &` 후 `cat /proc/timer_list`를 하면 그 `sleep`의 hrtimer가 어떤 함수 이름으로 보이나? `expires at A-B`의 범위 폭은 얼마인가? — 정답/힌트: `hrtimer_wakeup`, 범위 폭은 timer slack 50 us(= 50000 ns). §9.4의 `PR_SET_TIMERSLACK`과 연결된다.
5. **ftrace**: `f8_delay`를 돌리면서 `timer:timer_start`를 켜고 `function=process_timeout` 줄의 `timeout=` 값을 모아라. `msleep(1)`, `msleep(5)`, `msleep(20)`에서 각각 몇인가? — 정답/힌트: 1, 2, 5 (= `msecs_to_jiffies`). `bucket_expiry - expires`는 레벨 0이라 1.
6. **wrap**: 예제 6의 `f8_wrap`을 `INITIAL_JIFFIES` 대신 0에서 시작하게 바꾸면 299초 케이스가 어떻게 되나? 그럼 왜 커널은 0에서 시작하지 않나? — 정답/힌트: 둘 다 정상 — 버그가 숨는다. 199일 뒤 현장에서 터지게 하지 않으려고 5분에 터지게 만든다.
7. **설계**: 센서 키트에 1 kHz IMU(폴링), 30 fps 카메라(V4L2), 10 Hz GNSS(UART)가 있다. 각각의 타임스탬프를 어디서 무엇으로 찍을지 표로 적어라. — 정답/힌트: 모두 MONOTONIC. IMU = hrtimer 격자 시각 또는 워터마크 IRQ 시각 + ODR 역산, 카메라 = 드라이버가 프레임 시작(SOF) IRQ에서, GNSS = PPS 신호 IRQ(`pps-gpio`)와 NMEA 시각을 짝지어서 (Q3).

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| generic timer | ARMv8 아키텍처 타이머 | 공용 64비트 카운터 + CPU별 비교기 |
| CNTVCT_EL0 | virtual count register | 카운터 현재값. EL0에서도 읽힘 |
| CNTFRQ_EL0 | counter frequency register | 카운터 주파수(Hz). 펌웨어가 씀 |
| CNTV_CVAL_EL0 | virtual compare value | 이 값에 도달하면 PPI 발생 |
| PPI | Private Peripheral Interrupt | CPU 하나에만 가는 인터럽트 (INTID 16~31) |
| clocksource | 시간 읽기 장치 추상 | "지금 몇 시" — `arch_sys_counter` |
| clockevent | 타이머 인터럽트 장치 추상 | "이때 깨워 줘" — `arch_sys_timer`, CPU별 |
| tick | 주기 인터럽트 | HZ번/초. jiffies 증가, 스케줄러 틱, wheel 구동 |
| HZ | tick 주파수 | 이 랩 250 → 4 ms |
| NO_HZ_IDLE | tickless idle | idle CPU의 tick 정지 |
| NO_HZ_FULL | 완전 tickless | 단일 태스크 CPU의 tick 정지, `nohz_full=` 필요 |
| jiffies | tick 카운터 | `unsigned long`, `INITIAL_JIFFIES`에서 시작 |
| `INITIAL_JIFFIES` | jiffies 시작값 | −300×HZ → 32비트에서 5분 뒤 wrap |
| `time_after` | wrap 안전 비교 | 차이를 부호 있는 수로 판단 |
| `timer_list` | jiffies 타이머 | timer wheel, softirq 콜백 |
| timer wheel | 계층형 해시 버킷 | 레벨마다 간격 8배, O(1), 올림 |
| `bucket_expiry` | 실제 실행 jiffy | `calc_index`가 레벨 간격으로 올린 값 |
| hrtimer | 고해상도 타이머 | ns 만료, rbtree, 비교기에 직접 |
| `ktime_t` | 커널 시간 타입 | `s64` 나노초 |
| `hrtimer_forward_now` | 주기 재무장 | 만료를 격자 위로 전진, 놓친 주기 수 반환 |
| `HRTIMER_MODE_HARD`/`_SOFT` | 콜백 컨텍스트 | hardirq / softirq |
| timer slack | 깨우기 여유 | 유저 기본 50 us, hrtimer soft~hard 범위 |
| `CLOCK_MONOTONIC` | 단조 시계 | 뒤로 안 감, 센서 타임스탬프 기본 |
| `CLOCK_BOOTTIME` | suspend 포함 단조 시계 | Android 센서 |
| `CLOCK_REALTIME` | 벽시계 | 점프 가능, 간격 측정 금지 |
| busy-wait | 바쁜 대기 | `udelay` — CPU가 계속 돎, atomic에서 가능 |
| `fsleep` | 자동 지연 | ≤10 us udelay, <4 tick usleep_range, 그 이상 msleep |
| icount | QEMU 명령어 수 기반 시계 | `shift=3` → 1 명령 8 ns, `sleep=off`면 결정적 |

---

## 17. 요약 & 체크리스트

arm64 generic timer는 공용 64비트 카운터(CNTVCT_EL0, 이 랩 62.5 MHz)와 CPU별 비교기로 이루어지고, 커널은 이를 clocksource(`arch_sys_counter`, 시간 읽기)와 clockevent(`arch_sys_timer`, 인터럽트 예약)로 추상화한다. 그 위에서 hrtimer가 ns 단위 만료를 rbtree에 들고 비교기를 직접 프로그램하며, tick도 4 ms(HZ=250)짜리 hrtimer 하나다. tick은 jiffies를 올리고 timer wheel을 돌리는데, wheel은 싼 대신 tick 단위로 올림해서 1 ms `timer_list`가 4~8 ms 뒤에 울린다(측정 4.05~7.97 ms). `msleep`은 그 wheel을 쓰고, `usleep_range`는 hrtimer를 쓰며 보통 상한에 깨고, `fsleep`은 크기에 따라 고른다. atomic 컨텍스트에서는 `udelay` 같은 busy-wait만 가능하다. 주기 작업은 `hrtimer_forward_now`로 격자를 지켜야 드리프트가 없고(상대 재무장은 1000주기에 +0.9~44%), 센서 타임스탬프는 `CLOCK_MONOTONIC`으로 IRQ 첫 줄에서 찍는다. jiffies 비교는 항상 `time_after` 계열이다. 그리고 QEMU에서 시간을 잴 때는 `-icount shift=3,sleep=off`로 에뮬레이터 잡음을 분리하고, 디버그 커널로 타이밍을 재지 않는다.

- [ ] generic timer의 카운터·비교기·PPI 구조를 MCU 타이머와 비교해 설명할 수 있다
- [ ] clocksource와 clockevent의 차이를 `/sys`와 `/proc/timer_list`로 보여 줄 수 있다
- [ ] HZ, tick, NO_HZ_IDLE/NO_HZ_FULL을 설명하고 `/proc/interrupts`로 tick 정지를 확인할 수 있다
- [ ] `INITIAL_JIFFIES`가 왜 −300×HZ인지, `time_after`가 왜 wrap에 안전한지 설명할 수 있다
- [ ] `timer_setup`·`mod_timer`·`timer_delete_sync`·`timer_shutdown_sync`·`timer_container_of`로 드리프트 없는 폴링 타이머를 짤 수 있다
- [ ] HZ=250에서 1 ms `timer_list`가 4~8 ms 뒤에 울리는 이유를 `calc_index`로 설명할 수 있다
- [ ] hrtimer 모드(REL/ABS, HARD/SOFT, PINNED)를 고르고 `hrtimer_forward_now`로 주기 타이머를 짤 수 있다
- [ ] `udelay`/`usleep_range`/`msleep`/`fsleep`을 컨텍스트와 길이에 따라 고를 수 있다
- [ ] 센서 타임스탬프에 `CLOCK_MONOTONIC`을 쓰는 이유와 IIO·evdev의 기본값 함정을 설명할 수 있다
- [ ] ftrace `timer:*` 이벤트로 타이머의 무장·만료·컨텍스트를 확인할 수 있다
- [ ] QEMU 측정에서 커널 메커니즘과 에뮬레이터 아티팩트를 구분할 수 있다 (icount sleep=off, MAX_STALLED_JIFFIES)

---

## 참고 자료

- 커널 소스(6.18.55, 랩 `/var/lab/src/linux-6.18.55`): `kernel/time/timer.c`(timer wheel, `calc_index`), `kernel/time/hrtimer.c`(`hrtimer_interrupt`, `__hrtimer_run_queues`), `kernel/time/tick-sched.c`(`tick_nohz_handler`, `MAX_STALLED_JIFFIES`), `kernel/time/sleep_timeout.c`(`msleep`, `usleep_range_state`, `process_timeout`), `include/linux/delay.h`(`fsleep`), `include/linux/jiffies.h`, `include/linux/hrtimer.h`, `drivers/clocksource/arm_arch_timer.c` — 온라인은 [elixir.bootlin.com](https://elixir.bootlin.com/linux/v6.18/source/kernel/time/timer.c)
- docs.kernel.org: [Delay and sleep mechanisms](https://docs.kernel.org/timers/delay_sleep_functions.html), [hrtimers](https://docs.kernel.org/timers/hrtimers.html), [High resolution timers and dynamic ticks design notes](https://docs.kernel.org/timers/highres.html), [NO_HZ: Reducing Scheduling-Clock Ticks](https://docs.kernel.org/timers/no_hz.html), [Clock sources, Clock events, sched_clock() and delay timers](https://docs.kernel.org/timers/timekeeping.html)
- man7.org: [clock_gettime(2)](https://man7.org/linux/man-pages/man2/clock_gettime.2.html), [clock_nanosleep(2)](https://man7.org/linux/man-pages/man2/clock_nanosleep.2.html), [time(7)](https://man7.org/linux/man-pages/man7/time.7.html), [prctl(2)](https://man7.org/linux/man-pages/man2/prctl.2.html) (`PR_SET_TIMERSLACK`)
- Michael Kerrisk, "The Linux Programming Interface", 23장 Timers and Sleeping
- Arm Architecture Reference Manual for A-profile, "The Generic Timer in AArch64 state" 장 (CNTVCT_EL0, CNTV_CVAL_EL0, CNTKCTL_EL1)
- LWN.net: [A new approach to kernel timers](https://lwn.net/Articles/152436/) (hrtimer 도입 배경), [Reinventing the timer wheel](https://lwn.net/Articles/646950/) (현재 wheel 설계)
- QEMU 문서: [-icount 옵션](https://www.qemu.org/docs/master/system/invocation.html) (`shift`, `sleep=off`)
- 관련 노트: E4(atomic context), E5(`container_of`), F2·F3·F6·F7, M5(ftrace), N1·N2·N3(실시간), Q3(멀티 센서 동기화), C7(icount)
