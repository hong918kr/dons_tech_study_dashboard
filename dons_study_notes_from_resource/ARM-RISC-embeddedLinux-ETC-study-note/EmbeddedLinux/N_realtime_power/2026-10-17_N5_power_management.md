# N5. 전원 관리 — system suspend/resume, runtime PM, cpufreq/cpuidle, devfreq, PM QoS

> **이 노트를 다 읽으면**: 전원 상태를 "시스템 sleep · CPU idle(C-state) · CPU 주파수(P-state) · 장치 runtime PM" 네 층으로 나눠 설명할 수 있다 · suspend/resume의 단계(freeze → prepare → suspend → late → noirq → 잠듦 → 역순)를 ftrace로 직접 찍고 어느 단계가 느린지 짚을 수 있다 · `pm_runtime_*` autosuspend를 쓰는 작은 드라이버를 만들어 sysfs로 상태 전이를 관찰할 수 있다 · wakeup source, `wakeup_count` 프로토콜, PM QoS가 무엇을 막고 무엇을 못 막는지 실험 결과로 말할 수 있다
> **왜 필요한가**: 배터리 장비(드론, 휴대 센서 키트, 카메라)에서는 "밤새 suspend 했는데 배터리가 다 닳았다", "resume 후 IMU가 죽었다", "NPU가 쉬는데도 전류가 안 내려간다"가 매주 나오는 버그다. 로보틱스·AI 가속기 면접에서는 "runtime PM과 system suspend 차이", "DVFS를 누가 결정하나"가 단골이다
> **Don 기준 난이도**: SSD 펌웨어의 PS0~PS4 전원 상태, APST, L1.2 같은 "idle이면 내려가고 명령 오면 올라오는 상태 기계"는 이미 몸으로 안다 / 그것이 Linux에서는 cpuidle·cpufreq·runtime PM·system sleep이라는 서로 다른 프레임워크로 쪼개져 있고, 각자 sysfs·DT·tracepoint로 드러난다는 것이 새로 배울 부분
> **선행 노트**: C5 §4(PSCI — `CPU_SUSPEND`, `SYSTEM_SUSPEND`, QEMU TF-A가 suspend를 지원하지 않는 이유), H3(clock·regulator — runtime PM 콜백이 실제로 끄는 대상), I4(thermal zone · cooling device · power_supply 클래스), G3(platform driver 수명), F1(스케줄러), M5(ftrace). 같은 모듈의 N2(PM QoS로 지연 고정), **N6(드라이버의 `dev_pm_ops` 구현 — 콜백 작성법은 거기서 깊게)**

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어를 생각하자. 호스트가 아무 명령도 안 보내면 컨트롤러는 코어 클럭을 낮추고(DVFS), 코어를 WFI로 재우고(idle), NAND 채널 PHY를 끄고(장치 단위 전원), 오래 놀면 PS4 같은 깊은 상태로 내려간다(시스템 단위). 이 넷은 서로 다른 타이머와 조건으로 움직이지만 한 펌웨어 안의 코드였다.

Linux SoC에서는 같은 네 가지가 **네 개의 프레임워크**로 나뉜다.

| 층 | 무엇을 아끼나 | Linux 프레임워크 | 누가 결정하나 | 눈에 보이는 곳 |
|---|---|---|---|---|
| 시스템 sleep | 보드 전체 (DDR self-refresh, SoC 대부분 off) | system suspend (`kernel/power/`) | 유저(`echo mem > /sys/power/state`, systemd) | `/sys/power/` |
| CPU idle (C-state) | 놀고 있는 코어의 누설·동적 전력 | cpuidle | idle 루프 + governor(menu/teo) | `/sys/devices/system/cpu/cpuN/cpuidle/` |
| CPU 성능 (P-state) | 일하는 코어의 V·f | cpufreq (+ EAS) | governor(schedutil) | `/sys/devices/system/cpu/cpufreq/` |
| 장치 | 놀고 있는 IP 블록의 clock·전원 | runtime PM + genpd | 드라이버(`pm_runtime_get/put`) | `/sys/devices/.../power/` |
| 가속기·버스 성능 | GPU·NPU·DDR의 V·f | devfreq | devfreq governor | `/sys/class/devfreq/` |
| 제약 | 지연·처리량 하한 | PM QoS | 유저·드라이버 요청 | `/dev/cpu_dma_latency`, `power/pm_qos_*` |

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 400">
<defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="22" font-size="14" text-anchor="middle">전원 상태 분류 — 위로 갈수록 깊고(전력↓) 깨어나는 데 오래 걸린다</text>
<rect x="10" y="40" width="210" height="300" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="60" font-size="13" text-anchor="middle">시스템 sleep 상태</text>
<rect x="25" y="72" width="180" height="36" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="115" y="95" font-size="12" text-anchor="middle">disk (hibernate)</text>
<rect x="25" y="116" width="180" height="36" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="115" y="139" font-size="12" text-anchor="middle">mem = deep (S2RAM)</text>
<rect x="25" y="160" width="180" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="115" y="183" font-size="12" text-anchor="middle">standby (shallow)</text>
<rect x="25" y="204" width="180" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="115" y="227" font-size="12" text-anchor="middle">freeze = s2idle</text>
<rect x="25" y="248" width="180" height="36" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="115" y="271" font-size="12" text-anchor="middle">running</text>
<text x="115" y="310" font-size="12" text-anchor="middle">/sys/power/state</text>
<text x="115" y="328" font-size="12" text-anchor="middle">유저가 고른다</text>
<rect x="235" y="40" width="210" height="300" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="60" font-size="13" text-anchor="middle">CPU 상태</text>
<rect x="250" y="72" width="180" height="36" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340" y="95" font-size="12" text-anchor="middle">C: cluster power-down</text>
<rect x="250" y="116" width="180" height="36" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340" y="139" font-size="12" text-anchor="middle">C: core power-down</text>
<rect x="250" y="160" width="180" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="183" font-size="12" text-anchor="middle">C: retention</text>
<rect x="250" y="204" width="180" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="227" font-size="12" text-anchor="middle">C: WFI (clock gate)</text>
<rect x="250" y="248" width="180" height="36" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="271" font-size="12" text-anchor="middle">P: OPP0 ~ OPPn (V, f)</text>
<text x="340" y="310" font-size="12" text-anchor="middle">cpuidle / cpufreq</text>
<text x="340" y="328" font-size="12" text-anchor="middle">커널 governor가 고른다</text>
<rect x="460" y="40" width="210" height="300" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565" y="60" font-size="13" text-anchor="middle">장치 상태</text>
<rect x="475" y="72" width="180" height="36" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="565" y="95" font-size="12" text-anchor="middle">power domain off (genpd)</text>
<rect x="475" y="116" width="180" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="139" font-size="12" text-anchor="middle">RPM_SUSPENDED</text>
<rect x="475" y="160" width="180" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="183" font-size="12" text-anchor="middle">active, 낮은 OPP (devfreq)</text>
<rect x="475" y="248" width="180" height="36" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565" y="271" font-size="12" text-anchor="middle">RPM_ACTIVE</text>
<text x="565" y="310" font-size="12" text-anchor="middle">runtime PM / devfreq</text>
<text x="565" y="328" font-size="12" text-anchor="middle">드라이버가 고른다</text>
<line x1="20" y1="380" x2="660" y2="380" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="20" y="372" font-size="12">짧은 지연 · 적은 절약</text>
<text x="660" y="372" font-size="12" text-anchor="end">system suspend는 세 열을 한꺼번에 가장 깊은 쪽으로 민다</text>
</svg>
```

그림 1 — 세 축의 전원 상태. 시스템 sleep은 유저가, CPU C/P-state는 커널 governor가, 장치 상태는 드라이버가 고른다. system suspend는 모든 장치를 suspend시키고 CPU를 가장 깊은 상태로 보내므로 세 열을 한 번에 위로 민다.

용어 하나씩 한 문장으로:

- **C-state**: 코어가 일을 안 할 때 들어가는 잠 상태. 번호가 클수록 깊다(ARM에서는 "idle state").
- **P-state / OPP(Operating Performance Point)**: 코어가 일할 때의 (전압, 주파수) 쌍. 주파수를 낮추면 전압도 낮출 수 있어 전력이 V²f로 준다.
- **runtime PM**: 시스템은 깨어 있는데 **장치 하나**가 놀면 그 장치만 끄는 것. SSD의 "채널 하나 PHY off"와 같다.
- **system suspend**: 보드 전체를 재우는 것. 노트북 뚜껑 닫기.
- **PM QoS**: "나는 100 us 안에 깨어나야 한다" 같은 제약을 걸어 governor가 너무 깊이 못 자게 하는 장치.

이 노트는 **시스템 관점**이다. 드라이버가 `dev_pm_ops` 콜백 안에서 레지스터를 저장·복원하고 clock을 끄는 구체적인 코드는 N6에서 다룬다. 여기서는 콜백이 "언제, 어떤 순서로, 누구 때문에" 불리는지를 실험으로 확인한다.

---

## 1. 실습 준비 — QEMU virt가 해 주는 것과 못 해 주는 것

### 1.1 커널과 rootfs

- 커널: 랩의 `Image-trace` (`6.18.55-trace`). 이 노트에 필요한 옵션이 다 켜져 있다: `CONFIG_PM_DEBUG`, `PM_ADVANCED_DEBUG`, `PM_SLEEP_DEBUG`(→ `/sys/power/pm_test`, `pm_print_times`, `pm_debug_messages`), `ARM_PSCI_CPUIDLE`, `CPU_FREQ_STAT`, `CPU_FREQ_GOV_SCHEDUTIL`, `PM_DEVFREQ`, `ENERGY_MODEL`, ftrace 전체. 일부 비교 실험은 `Image-sec`(`6.18.55-sec`, PM 디버그 옵션 없음)에서도 돌렸고 그때는 그렇다고 적는다.
- rootfs: 랩 BusyBox initramfs 복사본 + 이 노트의 `.ko`와 static 바이너리. 커널만 시험하는 실험이라 systemd rootfs보다 빠르고, suspend 중에 끼어드는 데몬이 없다.
- VM 호스트 커널은 `7.0.0-38-generic`(Lima Ubuntu)이다. 모듈은 호스트가 아니라 QEMU 게스트에만 넣는다.
- 모든 시간 수치는 **QEMU TCG(에뮬레이션) + 17명이 공유하는 VM**에서 잰 값이다. 순서와 비율은 의미가 있지만 절대값은 실보드와 다르다.

### 1.2 예제 1 — 부팅 스크립트

무엇을 확인하는 코드인지: 매 실험마다 initramfs를 새로 묶고 `LAB_CMD`로 시험 스크립트 하나를 돌린 뒤 꺼지게 하는 래퍼다.

```sh
#!/bin/bash
# run.sh <kernel> <moddir> <script> [dtb] [extra append]
K=$1; MD=$2; S=$3; DTB=$4; EX=$5
cd /var/tmp/N5 && rm -rf r && mkdir r && cp -a /var/lab/build/rootfs/. r/ \
  && cp /Users/donh/.elinux_scratch/N5/*.sh r/ && chmod +x r/*.sh \
  && cp $MD/*/*.ko r/ 2>/dev/null; cp /var/tmp/N5/bin/* r/ 2>/dev/null
(cd r && find . | cpio -o -H newc --quiet | gzip) > n5_i.gz
D=""; [ -n "$DTB" ] && D="-dtb $DTB"
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57,pmu=on -smp 2 -m 1G \
  -nographic -kernel $K -initrd n5_i.gz $D \
  -append "console=ttyAMA0 quiet LAB_CMD=/$S $EX"
```

```sh
limactl shell elinux -- bash /Users/donh/.elinux_scratch/N5/run.sh \
  /var/lab/out/Image-trace /var/tmp/N5/b-trace t8.sh /var/tmp/N5/n5_idle.dtb
```

출력에서 볼 것: 없다(래퍼). 모듈은 `make -C /var/lab/out/linux-build-trace M=$PWD modules`로 `Image-trace` 전용으로 빌드했다(vermagic `6.18.55-trace`).

### 1.3 예제 2 — QEMU virt의 전원 관리 지형 확인

무엇을 확인하는 코드인지: 기본 DT(QEMU가 만든 것) 그대로 `Image-sec`로 부팅해 `/sys/power`, cpuidle, cpufreq, devfreq, RTC wakeup이 각각 있는지 본다 (ls 색 코드는 지웠다).

```sh
#!/bin/sh
echo "== uname"; uname -r
for f in state mem_sleep disk; do printf "%s: " $f; cat /sys/power/$f; done
echo "== cpuidle"; cat /sys/devices/system/cpu/cpuidle/current_driver \
                       /sys/devices/system/cpu/cpuidle/current_governor
ls /sys/devices/system/cpu/cpu0/cpuidle
echo "== cpufreq"; ls /sys/devices/system/cpu/cpufreq
echo "== devfreq"; ls /sys/class/devfreq
echo "== rtc"; cat /sys/class/rtc/rtc0/name /sys/class/rtc/rtc0/device/power/wakeup
echo "== psci dmesg"; dmesg | grep -i -E "psci|cpuidle|suspend|PM:"
echo "== wakeup_sources"; cat /sys/kernel/debug/wakeup_sources
```

```text
== uname
6.18.55-sec
state: freeze mem disk
mem_sleep: [s2idle]
disk: [shutdown] reboot suspend test_resume
== cpuidle
none
menu
ls: /sys/devices/system/cpu/cpu0/cpuidle: No such file or directory
== cpufreq
== devfreq
== rtc
rtc-pl031 9010000.pl031
enabled
== psci dmesg
[    0.000000] psci: probing for conduit method from DT.
[    0.000000] psci: PSCIv1.1 detected in firmware.
[    0.000000] psci: Using standard PSCI v0.2 function IDs
[    0.000000] psci: Trusted OS migration not required
[    0.000000] psci: SMC Calling Convention v1.0
[    2.863534] cpuidle: using governor menu
[    9.133343] PM: genpd: Disabling unused power domains
== wakeup_sources
name		active_count	event_count	wakeup_count	expire_count	active_since	total_time	max_time	last_change	prevent_suspend_time
alarmtimer.0.auto	0		0		0		0		0		0		0		0		0
9010000.pl031	0		0		0		0		0		0		0		0		0
deleted     	0		0		0		0		0		0		0		0		0
```

출력에서 볼 것:

- `state: freeze mem disk`인데 `mem_sleep`에 `[s2idle]` 하나뿐이다. 즉 `mem`을 써도 실제로는 s2idle로 간다. `deep`이 없다는 것은 커널이 PSCI `SYSTEM_SUSPEND`를 못 찾았다는 뜻이다 — QEMU 내장 PSCI(이 부팅은 `-kernel`이라 EL3 펌웨어 없이 QEMU가 PSCI를 흉내 낸다, DT `method = "hvc"`)가 `PSCI_FEATURES(SYSTEM_SUSPEND)`를 지원한다고 답하지 않는다. TF-A 경로도 지원하지 않는다는 것은 C5 §4.7에서 소스(`qemu_pwr_domain_suspend()`가 `assert(0)`)로 확인했다.
- cpuidle 드라이버가 `none`이다. QEMU DT에 `idle-states`가 없어서 `psci_idle` 드라이버가 등록되지 않았다. 이때 idle 루프는 그냥 `cpu_do_idle()`(WFI)를 부른다.
- cpufreq, devfreq 디렉터리가 비어 있다. QEMU virt에는 주파수를 바꿀 clock·regulator가 없다.
- RTC(pl031)는 wakeup이 `enabled`이고 wakeup source(`9010000.pl031`)로 등록돼 있다. s2idle에서 깨우는 데 쓸 수 있다.

### 1.4 QEMU로 할 수 있는 것과 없는 것

| 항목 | QEMU virt에서 | 이 노트에서 한 일 |
|---|---|---|
| s2idle (freeze) | 된다. RTC 알람으로 깨울 수 있다 | 실제 단계별 시간 측정 (§5) |
| mem = deep (S2RAM) | 안 된다 (PSCI SYSTEM_SUSPEND 없음) | 흐름만 설명 (가이드) |
| disk (hibernate) | 커널은 지원, swap 없음 | 설명만 |
| `pm_test` | freezer·devices·platform 된다, processors·core는 s2idle에서 거부 | 실행 (§5.4) |
| cpuidle | DT에 `idle-states`를 넣으면 PSCI `CPU_SUSPEND` 상태가 생긴다 (QEMU는 WFI처럼 처리) | 직접 DT 수정 (§2) |
| cpufreq | 드라이버 없음 | 가짜 cpufreq 드라이버로 governor 관찰 (§3) |
| devfreq | 장치 없음 | DT·드라이버 골격 가이드 (§8) |
| runtime PM | 된다 (장치 모델은 진짜) | 직접 만든 드라이버 (§4) |
| wakeup source / wakeup_count | 된다 | 실행 (§6) |
| PM QoS | 된다 | 실행 (§7) |
| 전력 숫자 (mW, mA) | **없다** — 전력 모델이 없다 | 측정 방법 가이드 (§9) |
| DVFS의 실제 효과 | **없다** — 주파수가 바뀌어도 TCG 속도는 같다 | 설명만 |

---

## 2. CPU idle — cpuidle와 C-state

### 2.1 직관 — "쉬는 동안 얼마나 깊이 잘까"

Cortex-M에서 `__WFI()`는 한 가지 잠뿐이다. Cortex-A SoC에는 여러 단계가 있다.

| 깊이 | ARM 이름 | 꺼지는 것 | 깨어날 때 | 대표 지연 |
|---|---|---|---|---|
| 얕음 | WFI (standby) | 코어 clock gate | 바로 이어서 실행 | 1 us 미만 |
| 중간 | retention | 코어 clock + 전압을 유지 최소치로 | 상태 보존, 바로 이어서 | 수십 us |
| 깊음 | core power-down | 코어 전원 off (L1 flush) | 펌웨어(PSCI)가 리셋 벡터로 다시 시작, 커널이 레지스터 복원 | 수백 us |
| 더 깊음 | cluster power-down | 클러스터 전체 + L2 | L2 flush·복원 | 1 ms 이상 |

깊이 잘수록 아끼는 전력이 크지만, **들어가고 나오는 데도 에너지와 시간이 든다**. 그래서 "다음 일이 언제 올지"를 예측해서 그 예측 시간보다 짧은 `min-residency`(본전 뽑는 최소 체류 시간)를 가진 상태 중 가장 깊은 것을 고른다. 이 예측기가 **cpuidle governor**다.

### 2.2 정의 — 세 부품

- **cpuidle 드라이버**: 상태 목록과 "그 상태에 들어가는 법"을 제공한다. ARM64 DT 시스템이면 `cpuidle-psci`(`drivers/cpuidle/cpuidle-psci.c`): state0은 항상 WFI, state1부터는 DT `idle-states` 노드에서 읽어 PSCI `CPU_SUSPEND`(C5 §4.1의 0xC4000001)를 부른다.
- **cpuidle governor**: 상태를 고르는 정책. `menu`(다음 타이머 + 최근 idle 길이 패턴으로 예측 — 오래된 기본값), `teo`(Timer Events Oriented — 타이머로 깬 경우와 아닌 경우를 통계로 나눠 보정, tickless 시스템에서 권장되는 추세), `ladder`(한 단계씩 오르내림 — periodic tick 시스템용), `haltpoll`(가상 머신 게스트용, 잠깐 폴링 후 halt). 이 랩 커널에는 `menu`만 들어 있다.
- **idle 루프**: 스케줄러가 돌릴 태스크가 없으면 `do_idle()` → `cpuidle_idle_call()` → governor `select()` → 드라이버 `enter()`.

### 2.3 예제 3 — DT에 idle state 하나 추가

무엇을 확인하는 코드인지: QEMU가 만든 DT를 덤프해 `idle-states` 노드와 `cpu-idle-states`를 넣는다. 실제 SoC의 DTS에서 보는 모양 그대로다.

```sh
qemu-system-aarch64 -machine virt,dumpdtb=/var/tmp/N5/n5_virt.dtb \
  -cpu cortex-a57,pmu=on -smp 2 -m 1G -nographic
dtc -I dtb -O dts n5_virt.dtb -o n5_virt.dts
# (python으로 아래 조각을 cpus 노드 안에 삽입) 
dtc -I dts -O dtb -o n5_idle.dtb n5_idle.dts
```

```dts
	cpus {
		idle-states {
			entry-method = "psci";
			n5_ret: cpu-retention {
				compatible = "arm,idle-state";
				arm,psci-suspend-param = <0x0>;	/* standby type, StateID 0 */
				entry-latency-us = <40>;
				exit-latency-us = <100>;
				min-residency-us = <500>;
			};
		};
		cpu@0 {
			cpu-idle-states = <&n5_ret>;
			/* ... 나머지는 QEMU가 만든 그대로 ... */
		};
		cpu@1 {
			cpu-idle-states = <&n5_ret>;
		};
	};
```

`arm,psci-suspend-param`은 PSCI `CPU_SUSPEND`의 `power_state` 인자 그대로다. 원래(original) 형식에서 bit 16이 "power-down이냐 standby냐"를 정한다. 0x0은 **standby(=retention)** 이라 커널이 레지스터를 저장하지 않고 SMC/HVC만 부른다. QEMU 내장 PSCI는 `CPU_SUSPEND`를 받으면 깊이와 상관없이 WFI처럼 처리하고 0을 돌려준다. power-down(0x10000) 상태를 넣으면 커널은 "펌웨어가 리셋 벡터로 다시 들어올 것"을 기대하는데 QEMU는 그냥 리턴하므로 맞지 않는다. 그래서 retention만 넣었다. 실보드 DTS의 예(가이드, 실행 안 함 — 숫자는 SoC마다 다르다):

```dts
	CPU_SLEEP: cpu-sleep {
		compatible = "arm,idle-state";
		arm,psci-suspend-param = <0x0010000>;	/* power-down */
		local-timer-stop;			/* 코어 타이머도 꺼짐 → broadcast timer 필요 */
		entry-latency-us = <300>;
		exit-latency-us = <1200>;
		min-residency-us = <2000>;
	};
	CLUSTER_SLEEP: cluster-sleep {
		compatible = "arm,idle-state";
		arm,psci-suspend-param = <0x1010000>;	/* affinity level 1 = cluster */
		local-timer-stop;
		entry-latency-us = <400>;
		exit-latency-us = <1500>;
		min-residency-us = <5000>;
	};
```

`local-timer-stop`은 "이 상태에서는 코어의 arch timer도 꺼진다"는 표시다. 그러면 커널은 그 코어의 다음 타이머를 **broadcast timer**(항상 켜진 SoC 타이머)에 맡기고 잔다. broadcast 타이머가 DT에 없으면 이 상태는 쓰이지 않는다 — bring-up 때 "idle state가 usage 0"인 흔한 원인이다.

### 2.4 예제 4 — 상태 목록과 통계 읽기

무엇을 확인하는 코드인지: 수정한 DT로 `Image-trace`를 부팅해 `/sys/power`의 디버그 파일, cpuidle 드라이버·governor, state1의 속성, 그리고 나머지 PM 프레임워크(cpufreq, devfreq, thermal, genpd, energy model)의 유무를 한 번에 본다.

```sh
#!/bin/sh
echo "== uname: $(uname -r)"
echo "== /sys/power"; ls -1 /sys/power | tr '\n' ' '; echo
for f in state mem_sleep disk pm_test; do echo "$f: $(cat /sys/power/$f)"; done
C=/sys/devices/system/cpu
echo "cpuidle driver=$(cat $C/cpuidle/current_driver) governor=$(cat $C/cpuidle/current_governor) available=$(cat $C/cpuidle/available_governors)"
for f in name desc latency residency disable above below; do
  echo "state1/$f=$(cat $C/cpu0/cpuidle/state1/$f)"; done
echo "cpufreq dir: [$(ls $C/cpufreq)]  devfreq: [$(ls /sys/class/devfreq)]  thermal: [$(ls /sys/class/thermal)]  powercap: [$(ls /sys/class/powercap 2>&1)]"
echo "== genpd"; cat /sys/kernel/debug/pm_genpd/pm_genpd_summary | head
echo "== energy model"; ls /sys/kernel/debug/energy_model
```

```text
== uname: 6.18.55-trace
== /sys/power
disk freeze_filesystems image_size mem_sleep pm_async pm_debug_messages pm_freeze_timeout pm_print_times pm_test pm_wakeup_irq reserved_size resume resume_offset state suspend_stats sync_on_suspend wakeup_count 
state: freeze mem disk
mem_sleep: [s2idle]
disk: [shutdown] reboot suspend test_resume
pm_test: [none] core processors platform devices freezer
cpuidle driver=psci_idle governor=menu available=menu 
state1/name=cpu-retention
state1/desc=cpu-retention
state1/latency=140
state1/residency=500
state1/disable=0
state1/above=39
state1/below=0
cpufreq dir: []  devfreq: []  thermal: []  powercap: [ls: /sys/class/powercap: No such file or directory]
== genpd
domain                          status          children        performance
    /device                         runtime status                  managed by
------------------------------------------------------------------------------
== energy model
```

출력에서 볼 것: `Image-sec`(예제 2)에는 없던 `pm_test`, `pm_print_times`, `pm_debug_messages`, `pm_wakeup_irq`가 `PM_DEBUG`/`PM_SLEEP_DEBUG` 덕분에 생겼다. 드라이버가 `none`에서 `psci_idle`로 바뀌었다. genpd 표는 머리글뿐(등록된 power domain 없음), energy model 디렉터리도 비어 있다. `latency=140`은 DT의 entry 40 + exit 100이다(커널은 exit latency를 둘의 합으로 잡는다). `above`는 "이 상태로 들어갔는데 실제로 잔 시간이 너무 짧아서 더 얕은 상태가 나았던" 횟수, `below`는 "더 깊이 잤어도 됐던" 횟수다. governor 예측이 맞는지 보는 첫 번째 지표다. `disable`에 1을 쓰면 그 상태를 금지할 수 있다(디버그에 유용 — "deep idle 때문에 IRQ 지연이 생기나?"를 1초 만에 확인).

### 2.5 예제 5 — 쉴 때와 바쁠 때, 어느 state에 얼마나 있었나

무엇을 확인하는 코드인지: `stateN/usage`(진입 횟수)와 `stateN/time`(누적 us)을 3초 창 앞뒤로 읽어 차이를 낸다. 바쁠 때는 셸 busy loop 두 개를 돌린다. 뒤의 세 줄은 §7의 PM QoS 실험이다.

```sh
#!/bin/sh
C=/sys/devices/system/cpu
snap() { for s in 0 1; do eval "u$s=\$(cat $C/cpu0/cpuidle/state$s/usage) t$s=\$(cat $C/cpu0/cpuidle/state$s/time)"; done; }
delta() { a0=$u0; b0=$t0; a1=$u1; b1=$t1; snap; echo "$1: WFI usage+$((u0-a0)) time+$((t0-b0))us  retention usage+$((u1-a1)) time+$((t1-b1))us"; }
burn() { end=$(( $(date +%s) + $1 )); while [ $(date +%s) -lt $end ]; do :; done; }
sleep 2
snap; sleep 3; delta "idle 3s       "
snap; burn 3 & burn 3 & wait; delta "busy x2 3s    "
snap; /n5_qos 0 3 >/dev/null; delta "qos=0us 3s    "
snap; /n5_qos 100 3 >/dev/null; delta "qos=100us 3s  "
snap; /n5_qos 200 3 >/dev/null; delta "qos=200us 3s  "
```

```text
idle 3s       : WFI usage+2 time+1462us  retention usage+50 time+3022648us
busy x2 3s    : WFI usage+23 time+13730us  retention usage+238 time+539968us
qos=0us 3s    : WFI usage+12 time+2968176us  retention usage+20 time+45654us
qos=100us 3s  : WFI usage+21 time+2995973us  retention usage+13 time+24440us
qos=200us 3s  : WFI usage+5 time+2302us  retention usage+51 time+3040638us
```

(이 실행은 `Image-sec`, 같은 DT. 실제 스크립트에서는 `burn`을 서브셸 루프로 썼다.)

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 284">
<text x="10" y="20" font-size="14">cpu0의 3초 창 동안 idle state별 체류 시간 (sysfs stateN/time 차이)</text>
<line x1="130" y1="38" x2="130" y2="214" stroke="currentColor" stroke-opacity="0.25"/>
<text x="130" y="230" font-size="12" text-anchor="middle">0 s</text>
<line x1="250" y1="38" x2="250" y2="214" stroke="currentColor" stroke-opacity="0.25"/>
<text x="250" y="230" font-size="12" text-anchor="middle">1 s</text>
<line x1="370" y1="38" x2="370" y2="214" stroke="currentColor" stroke-opacity="0.25"/>
<text x="370" y="230" font-size="12" text-anchor="middle">2 s</text>
<line x1="490" y1="38" x2="490" y2="214" stroke="currentColor" stroke-opacity="0.25"/>
<text x="490" y="230" font-size="12" text-anchor="middle">3 s</text>
<text x="122" y="61" font-size="12" text-anchor="end">idle 3 s</text>
<rect x="130" y="49" width="1.5" height="18" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="131.5" y="49" width="362.8" height="18" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="502.3" y="61" font-size="12">WFI 0.00 / ret 3.02</text>
<text x="122" y="95" font-size="12" text-anchor="end">busy x2 3 s</text>
<rect x="130" y="83" width="1.6" height="18" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="131.6" y="83" width="64.8" height="18" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="204.4" y="95" font-size="12">WFI 0.01 / ret 0.54</text>
<text x="122" y="129" font-size="12" text-anchor="end">QoS 0 us</text>
<rect x="130" y="117" width="356.2" height="18" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="486.2" y="117" width="5.5" height="18" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="499.7" y="129" font-size="12">WFI 2.97 / ret 0.05</text>
<text x="122" y="163" font-size="12" text-anchor="end">QoS 100 us</text>
<rect x="130" y="151" width="359.5" height="18" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="489.5" y="151" width="2.9" height="18" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="500.4" y="163" font-size="12">WFI 3.00 / ret 0.02</text>
<text x="122" y="197" font-size="12" text-anchor="end">QoS 200 us</text>
<rect x="130" y="185" width="1.5" height="18" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="131.5" y="185" width="364.9" height="18" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="504.4" y="197" font-size="12">WFI 0.00 / ret 3.04</text>
<rect x="20" y="243" width="14" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="40" y="254" font-size="12">state0 WFI (지연 1 us)</text>
<rect x="220" y="243" width="14" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="240" y="254" font-size="12">state1 cpu-retention (지연 140 us)</text>
</svg>
```

그림 2 — 위 출력을 그린 것. 놀 때는 cpu0이 3초 거의 전부를 retention에서 보낸다. busy loop를 돌리면 idle 시간 자체가 0.55초로 준다(가끔 `date` fork 사이에 쉰다). PM QoS 0 us 또는 100 us를 걸면 retention(지연 140 us)이 금지돼 WFI로 바뀐다. 200 us는 140 us보다 크니 retention이 허용된다.

출력에서 볼 것: 진입 횟수(usage)보다 **시간(time)** 이 전력과 직결된다. 놀 때 retention 진입이 3초에 50번뿐인 이유는 tickless(`NO_HZ_IDLE`)라서 다음 타이머까지 길게 자기 때문이다. QoS 0일 때도 retention에 20번, 45 ms가 남는 것은 `n5_qos`가 실행돼 실제로 요청을 쓰기 전과 닫은 뒤의 구간이 3초 창에 섞였기 때문으로 본다(TCG에서는 exec가 수십 ms 걸린다).

### 2.6 예제 6 — `power:cpu_idle` tracepoint로 체류 시간 계산

무엇을 확인하는 코드인지: sysfs 카운터 대신 tracepoint로 "언제 어느 상태에 들어가고 나왔는지"를 기록하고, awk로 상태별 체류 시간을 더한다. 실보드에서 "왜 deep idle에 안 들어가나"를 볼 때 쓰는 방법이다 (`Image-trace`).

```sh
#!/bin/sh
T=/sys/kernel/tracing; mount -t tracefs none $T
burn() { end=$(( $(date +%s) + $1 )); while [ $(date +%s) -lt $end ]; do :; done; }
res() { grep -v "^#" $T/trace | awk '{ts=$4+0; split($6,a,"="); split($7,b,"="); s=a[2]; c=b[2];
  if (s=="4294967295") { if (c in st) { sum[c" s"st[c]]+=ts-t0[c]; n[c" s"st[c]]++; delete st[c] } }
  else { st[c]=s; t0[c]=ts } }
  END { for (k in sum) printf "  cpu%s: %d entries, %.3f s resident\n", k, n[k], sum[k] }' | sort; }
echo 1 > $T/events/power/cpu_idle/enable
sleep 1; echo > $T/trace; sleep 2; echo "== idle 2s"; res
echo > $T/trace; burn 2 & burn 2 & wait; echo "== busy x2 2s"; res
/n5_qos 0 3 > /dev/null & sleep 0.5; echo > $T/trace; sleep 2; echo "== qos 0us (2 s window inside the hold)"; res; wait
```

```text
== idle 2s
  cpu0 s0: 2 entries, 0.002 s resident
  cpu0 s1: 16 entries, 1.872 s resident
  cpu1 s0: 2 entries, 0.003 s resident
  cpu1 s1: 11 entries, 1.982 s resident
== busy x2 2s
  cpu0 s0: 4 entries, 0.012 s resident
  cpu0 s1: 31 entries, 0.166 s resident
  cpu1 s0: 1 entries, 0.002 s resident
  cpu1 s1: 8 entries, 0.051 s resident
== qos 0us (2 s window inside the hold)
  cpu0 s0: 18 entries, 2.459 s resident
  cpu0 s1: 13 entries, 0.263 s resident
  cpu1 s0: 14 entries, 1.760 s resident
  cpu1 s1: 9 entries, 0.287 s resident
```

raw 이벤트는 이렇게 생겼다 (`state=4294967295`는 `PWR_EVENT_EXIT` = (u32)-1, "idle에서 나옴"):

```text
          <idle>-0       [000] d..1.    52.767795: cpu_idle: state=4294967295 cpu_id=0
          <idle>-0       [000] d..1.    52.768307: cpu_idle: state=1 cpu_id=0
          <idle>-0       [000] d..1.    52.771512: cpu_idle: state=4294967295 cpu_id=0
          <idle>-0       [000] d..1.    52.771831: cpu_idle: state=1 cpu_id=0
```

출력에서 볼 것: 같은 결론(놀 때 s1, 바쁠 때 idle 자체가 적음, QoS 0이면 s0 위주)이 tracepoint로도 나온다. QoS 창에서 s1이 0.26초 남고 cpu0 합계가 2초를 넘는 것은 TCG에서 `echo > trace`·`sleep`·awk 처리 자체가 느려 창 경계가 흐려진 탓으로 보인다 — 정확한 원인을 확정하지는 못했다. 실보드라면 `trace-cmd record -e power:cpu_idle` 후 KernelShark로 보는 것이 편하다(M5).

### 2.7 함정

- **"idle state가 있는데 usage가 0"**: DT `cpu-idle-states` 누락, PSCI 펌웨어가 그 `power_state`를 거부(부팅 로그 `psci: ... invalid`), `local-timer-stop`인데 broadcast timer 없음, 누군가 PM QoS 0을 걸어 둠(§7 — `/dev/cpu_dma_latency`를 read하면 현재 유효 값이 s32 이진값으로 나온다, `od -An -td4 /dev/cpu_dma_latency`).
- **실시간 루프 지터**: deep idle에서 깨어나는 1~2 ms가 센서 루프 지터로 보인다. N2에서 PM QoS·`cpuidle.off=1`·state disable로 다룬다.
- **governor 기대와 다른 동작**: 주기적 인터럽트(예: 1 kHz IMU)가 있으면 menu는 그 주기를 학습해서 얕은 상태를 고른다. 주기를 늘리거나 FIFO 배치로 묶으면 deep idle 비율이 확 오른다 — 전력 최적화의 실제 레버다.

---

## 3. CPU 성능 — cpufreq, OPP, governor, EAS

### 3.1 직관 — 동적 전력은 V²f

CMOS 동적 전력은 대략 `P = C·V²·f`다. 주파수를 반으로 낮추면 같은 전압에서 전력은 반이지만, 낮은 주파수에서는 전압도 낮출 수 있어 V²만큼 더 준다. 그래서 주파수와 전압은 짝(OPP)으로 움직인다. 올릴 때는 **전압 먼저, 주파수 나중**, 내릴 때는 **주파수 먼저, 전압 나중** — 순서를 틀리면 낮은 전압에서 높은 클럭이 돌아 타이밍 위반으로 코어가 죽는다. Don이 SSD 컨트롤러 bring-up에서 PMIC 레일과 PLL 순서를 맞췄던 바로 그 규칙이다.

### 3.2 정의 — 부품

- **OPP 테이블 (DT `operating-points-v2`)**: 이 SoC가 허용하는 (주파수, 전압) 목록. 커널 OPP 라이브러리(`drivers/opp/`)가 읽는다.
- **cpufreq 드라이버**: 실제로 주파수를 바꾸는 코드. 일반적인 ARM SoC는 **`cpufreq-dt`** 하나로 끝난다 — CPU 노드의 `clocks`, `cpu-supply`(regulator), `operating-points-v2`만 있으면 `dev_pm_opp_set_rate()`가 위의 V/f 순서를 지켜 clock과 regulator를 바꾼다(H3). SCMI(`scmi-cpufreq`, 펌웨어가 DVFS를 대행), Qualcomm `qcom-cpufreq-hw`, NVIDIA `tegra194-cpufreq` 같은 SoC 전용 드라이버도 있다.
- **policy**: 같은 clock을 공유하는 CPU 묶음(보통 클러스터). `related_cpus`로 보인다.
- **governor**: 주파수를 고르는 정책.

| governor | 동작 | 언제 |
|---|---|---|
| `schedutil` | 스케줄러의 CPU 사용률(PELT util)에서 바로 주파수 계산, 스케줄러 이벤트마다 갱신 | 현재 기본값, EAS와 짝 |
| `performance` | 항상 최대 | 벤치마크, 지연 민감 |
| `powersave` | 항상 최소 | 열·배터리 극한 |
| `ondemand` | 주기적으로 부하 샘플링, 임계값 넘으면 최대로 | 옛 기본값 |
| `conservative` | ondemand처럼 하되 한 단계씩 | 배터리 장비의 옛 선택 |
| `userspace` | `scaling_setspeed`에 쓴 값 | 실험, 특수 장비 |

- **EAS(Energy Aware Scheduling)**: big.LITTLE처럼 코어 종류가 다를 때 스케줄러가 **에너지 모델(EM)**(OPP별 전력 비용, DT `dynamic-power-coefficient` 또는 SCMI로 등록)을 보고 "이 태스크를 어느 코어에 두면 전체 에너지가 가장 적은가"를 계산해 배치한다. 조건: 비대칭 CPU 용량(`capacity-dmips-mhz`), EM 등록, schedutil, SMT 없음. `/sys/kernel/debug/energy_model/`에 보인다(이 랩에서는 EM을 등록한 드라이버가 없어 비어 있다 — 예제 4의 `== energy model` 줄).

### 3.3 실보드 DT (가이드, 실행 안 함)

```dts
	cpu0: cpu@0 {
		compatible = "arm,cortex-a55";
		reg = <0x0>;
		enable-method = "psci";
		clocks = <&scmi_clk 0>;		/* 또는 SoC CCF clock */
		cpu-supply = <&vdd_cpu>;	/* PMIC buck */
		operating-points-v2 = <&cpu_opp_table>;
		#cooling-cells = <2>;		/* thermal이 cooling device로 쓸 수 있게 (I4) */
		dynamic-power-coefficient = <100>;	/* EM용, uW/MHz/V^2 */
		capacity-dmips-mhz = <1024>;
	};
	cpu_opp_table: opp-table {
		compatible = "operating-points-v2";
		opp-shared;			/* 클러스터가 clock 공유 → 하나의 policy */
		opp-400000000  { opp-hz = /bits/ 64 <400000000>;  opp-microvolt = <750000>; };
		opp-1200000000 { opp-hz = /bits/ 64 <1200000000>; opp-microvolt = <850000>; };
		opp-1800000000 { opp-hz = /bits/ 64 <1800000000>; opp-microvolt = <950000>;
				 turbo-mode; };
	};
```

### 3.4 예제 7 — 가짜 cpufreq 드라이버로 governor 보기

QEMU에는 바꿀 clock이 없다. 그래서 "주파수 값을 변수에 저장만 하는" cpufreq 드라이버를 만들었다. 실제 전력·속도는 그대로지만, **cpufreq 코어·governor·통계·tracepoint는 진짜**라서 schedutil이 어떻게 반응하는지 볼 수 있다.

무엇을 확인하는 코드인지: 최소한의 cpufreq 드라이버 = 주파수 표 + `init` + `verify` + `target_index` + `get`.

```c
// n5_cpufreq: fake cpufreq driver (no real clock) to watch governors (N5)
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>

static struct cpufreq_frequency_table n5_tbl[] = {	/* "OPP table", kHz */
	{ .frequency = 400000 }, { .frequency = 800000 },
	{ .frequency = 1200000 }, { .frequency = 1800000 },
	{ .frequency = CPUFREQ_TABLE_END },
};
static unsigned int n5_cur = 400000;

static int n5_target_index(struct cpufreq_policy *p, unsigned int idx)
{
	n5_cur = n5_tbl[idx].frequency;	/* real driver: regulator + clk_set_rate */
	return 0;
}
static unsigned int n5_get(unsigned int cpu) { return n5_cur; }

static int n5_cpu_init(struct cpufreq_policy *p)
{
	p->freq_table = n5_tbl;
	p->cpuinfo.transition_latency = 100 * NSEC_PER_USEC;
	cpumask_copy(p->cpus, cpu_possible_mask);	/* one cluster = one policy */
	p->cur = n5_cur;
	return 0;
}
static struct cpufreq_driver n5_drv = {
	.name = "n5-fake", .init = n5_cpu_init,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = n5_target_index, .get = n5_get,
};
static int __init n5_init(void) { return cpufreq_register_driver(&n5_drv); }
static void __exit n5_exit(void) { cpufreq_unregister_driver(&n5_drv); }
module_init(n5_init);
module_exit(n5_exit);
MODULE_LICENSE("GPL");
```

빌드 메모: 예전 커널 예제에 흔한 `.attr = cpufreq_generic_attr`는 6.18에 없다(빌드 에러 `'cpufreq_generic_attr' undeclared`). 지금은 cpufreq 코어가 주파수 표가 있는 드라이버에 `scaling_available_frequencies`를 자동으로 붙인다(`drivers/cpufreq/cpufreq.c`). 이런 API 변화는 out-of-tree 드라이버를 새 커널로 옮길 때 자주 만난다(E7).

```sh
insmod /n5_cpufreq.ko; ls /sys/devices/system/cpu/cpufreq /sys/devices/system/cpu/cpufreq/policy0
cd /sys/devices/system/cpu/cpufreq/policy0
for f in scaling_driver scaling_governor scaling_available_governors \
         scaling_available_frequencies scaling_cur_freq related_cpus; do echo "$f=$(cat $f)"; done
```

```text
/sys/devices/system/cpu/cpufreq:
policy0    schedutil
/sys/devices/system/cpu/cpufreq/policy0:
affected_cpus                  scaling_cur_freq
cpuinfo_cur_freq               scaling_driver
cpuinfo_max_freq               scaling_governor
cpuinfo_min_freq               scaling_max_freq
cpuinfo_transition_latency     scaling_min_freq
related_cpus                   scaling_setspeed
scaling_available_frequencies  stats
scaling_available_governors
scaling_driver=n5-fake
scaling_governor=schedutil
scaling_available_governors=ondemand userspace performance schedutil 
scaling_available_frequencies=400000 800000 1200000 1800000 
scaling_cur_freq=1800000
related_cpus=0 1
```

(`Image-sec`, ls 출력은 색 코드를 지우고 두 디렉터리 이름을 붙였다.) 출력에서 볼 것: CPU 두 개가 한 policy(`related_cpus=0 1`)다. `powersave`·`conservative`는 이 커널에서 모듈(`=m`)이라 목록에 없다. `schedutil` 디렉터리는 governor의 튜닝 노브(`rate_limit_us`)다.

### 3.5 예제 8 — schedutil의 반응과 `stats`

무엇을 확인하는 코드인지: 쉴 때 3초, busy loop 3초 동안의 `time_in_state`(단위 10 ms)와 전이 표, 그리고 다른 governor로 바꿨을 때의 값.

```sh
P=/sys/devices/system/cpu/cpufreq/policy0
echo 1 > $P/stats/reset; sleep 3
echo "idle 3s : cur=$(cat $P/scaling_cur_freq)"; cat $P/stats/time_in_state
echo 1 > $P/stats/reset
burn 3 & sleep 1; echo "busy 1s : cur=$(cat $P/scaling_cur_freq)"; wait
sleep 1; echo "after   : cur=$(cat $P/scaling_cur_freq)"; cat $P/stats/time_in_state
echo "total_trans=$(cat $P/stats/total_trans)"; cat $P/stats/trans_table
echo performance > $P/scaling_governor; echo "performance: $(cat $P/scaling_cur_freq)"
echo userspace > $P/scaling_governor; echo 800000 > $P/scaling_setspeed
echo "userspace 800000: $(cat $P/scaling_cur_freq)"
echo schedutil > $P/scaling_governor; echo 1200000 > $P/scaling_max_freq
echo "schedutil max=1.2G: max=$(cat $P/scaling_max_freq)"
```

```text
idle 3s : cur=1800000
400000 275
800000 2
1200000 16
1800000 13
busy 1s : cur=1800000
after   : cur=1800000
400000 66
800000 70
1200000 72
1800000 191
total_trans=258
   From  :    To
         :    400000    800000   1200000   1800000 
   400000:         0         3         7         1 
   800000:         1         0        45        28 
  1200000:         8        37         0        50 
  1800000:         2        34        43         0 
performance: 1800000
userspace 800000: 800000
schedutil max=1.2G: max=1200000
```

출력에서 볼 것:

- 쉴 때 3초 중 2.75초(275 × 10 ms)를 400 MHz에 있었다. 그런데 `cur=1800000`이 찍힌 것은 **측정 자체가 부하**이기 때문이다 — `cat`을 fork하는 순간 셸이 CPU를 쓰고 schedutil이 바로 올린다. sysfs를 한 번 읽어서 "지금 주파수"를 판단하면 안 되는 이유다.
- 바쁜 구간에서는 1.8 GHz가 가장 길지만 4초에 258번이나 전이했다. TCG에서는 셸 busy loop도 `date`를 계속 fork·대기해서 사용률이 들쭉날쭉하기 때문이다. 실보드에서도 "주파수가 너무 자주 바뀐다"는 것은 `rate_limit_us`를 볼 신호다.
- `scaling_max_freq`에 쓰는 것은 governor와 상관없는 **상한 제약**이다. thermal도 이 경로(freq QoS)로 상한을 누른다(§8.1).

### 3.6 예제 9 — `power:cpu_frequency` tracepoint

무엇을 확인하는 코드인지: governor가 실제로 언제 주파수를 바꿨는지 tracepoint로 본다 (`Image-trace`).

```sh
insmod /n5_cpufreq.ko; sleep 1
echo > $T/trace; echo 1 > $T/events/power/cpu_frequency/enable
sleep 1; burn 1; sleep 1
echo 0 > $T/events/power/cpu_frequency/enable; grep -v "^#" $T/trace | head -12
echo "total events: $(grep -c cpu_frequency $T/trace)"
```

```text
         sugov:0-156     [001] .....    63.018115: cpu_frequency: state=800000 cpu_id=0
         sugov:0-156     [001] .....    63.018174: cpu_frequency: state=800000 cpu_id=1
         sugov:0-156     [001] .....    63.522173: cpu_frequency: state=1800000 cpu_id=0
         sugov:0-156     [001] .....    63.522193: cpu_frequency: state=1800000 cpu_id=1
         sugov:0-156     [001] .....    63.545983: cpu_frequency: state=400000 cpu_id=0
         sugov:0-156     [001] .....    63.545999: cpu_frequency: state=400000 cpu_id=1
         sugov:0-156     [001] .....    63.581060: cpu_frequency: state=1800000 cpu_id=0
         sugov:0-156     [001] .....    63.581090: cpu_frequency: state=1800000 cpu_id=1
         sugov:0-156     [001] .....    63.615892: cpu_frequency: state=1200000 cpu_id=0
         sugov:0-156     [001] .....    63.615907: cpu_frequency: state=1200000 cpu_id=1
         sugov:0-156     [001] .....    63.616139: cpu_frequency: state=1800000 cpu_id=0
         sugov:0-156     [001] .....    63.616145: cpu_frequency: state=1800000 cpu_id=1
total events: 34
```

출력에서 볼 것: 주파수를 바꾸는 주체는 **`sugov:0`** 커널 스레드(schedutil governor의 worker, SCHED_DEADLINE으로 돈다)다. 이 드라이버는 `fast_switch`(스케줄러 경로에서 바로 레지스터를 쓰는 콜백)가 없어서 `target_index`가 잠들 수 있는 스레드 문맥에서 불린다. policy 하나에 CPU 둘이라 이벤트가 CPU마다 한 줄씩 짝으로 찍힌다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 430">
<defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">누가 CPU의 주파수와 잠 깊이를 정하나</text>
<rect x="250" y="36" width="180" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="55" font-size="13" text-anchor="middle">스케줄러 (CFS/EEVDF)</text>
<text x="340" y="72" font-size="12" text-anchor="middle">PELT util, EAS 배치</text>
<rect x="20" y="36" width="180" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="110" y="55" font-size="13" text-anchor="middle">Energy Model</text>
<text x="110" y="72" font-size="12" text-anchor="middle">OPP별 전력 비용</text>
<line x1="200" y1="58" x2="248" y2="58" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="60" y="120" width="200" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="160" y="139" font-size="13" text-anchor="middle">cpufreq governor</text>
<text x="160" y="156" font-size="12" text-anchor="middle">schedutil (sugov)</text>
<rect x="420" y="120" width="200" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="520" y="139" font-size="13" text-anchor="middle">cpuidle governor</text>
<text x="520" y="156" font-size="12" text-anchor="middle">menu / teo</text>
<line x1="300" y1="80" x2="200" y2="118" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="150" y="102" font-size="12">util 변화</text>
<line x1="380" y1="80" x2="480" y2="118" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="440" y="102" font-size="12">돌릴 태스크 없음</text>
<rect x="60" y="210" width="200" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="160" y="229" font-size="13" text-anchor="middle">cpufreq 코어 + policy</text>
<text x="160" y="246" font-size="12" text-anchor="middle">min / max (freq QoS)</text>
<line x1="160" y1="164" x2="160" y2="208" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="420" y="210" width="200" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="520" y="229" font-size="13" text-anchor="middle">cpuidle-psci 드라이버</text>
<text x="520" y="246" font-size="12" text-anchor="middle">state0 WFI, state1.. DT</text>
<line x1="520" y1="164" x2="520" y2="208" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="60" y="300" width="200" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="160" y="319" font-size="13" text-anchor="middle">cpufreq-dt / scmi</text>
<text x="160" y="336" font-size="12" text-anchor="middle">OPP → regulator + clk</text>
<line x1="160" y1="254" x2="160" y2="298" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="420" y="300" width="200" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="520" y="319" font-size="13" text-anchor="middle">PSCI CPU_SUSPEND</text>
<text x="520" y="336" font-size="12" text-anchor="middle">EL3 펌웨어 (TF-A)</text>
<line x1="520" y1="254" x2="520" y2="298" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="280" y="210" width="120" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340" y="229" font-size="13" text-anchor="middle">thermal</text>
<text x="340" y="246" font-size="12" text-anchor="middle">cooling device</text>
<line x1="280" y1="232" x2="262" y2="232" stroke="#d0564a" marker-end="url(#ah3)"/>
<rect x="280" y="370" width="120" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="389" font-size="13" text-anchor="middle">PM QoS</text>
<text x="340" y="406" font-size="12" text-anchor="middle">cpu_dma_latency</text>
<line x1="400" y1="380" x2="520" y2="166" stroke="#4a7bd0" marker-end="url(#ah3)"/>
<text x="470" y="380" font-size="12">지연 상한 → 깊은 state 금지</text>
<text x="20" y="390" font-size="12">상한: thermal·유저가</text>
<text x="20" y="406" font-size="12">scaling_max_freq를 누른다</text>
</svg>
```

그림 3 — cpufreq·cpuidle·thermal·PM QoS의 관계. 스케줄러가 두 governor 모두의 입력이다(일이 있으면 주파수, 없으면 잠 깊이). thermal은 cpufreq policy의 상한(freq QoS)을 누르고, PM QoS의 CPU latency 요청은 cpuidle governor가 고를 수 있는 가장 깊은 상태를 제한한다.

---

## 4. 장치 — runtime PM

### 4.1 직관 — "장치마다 참조 카운트 달린 전원 스위치"

센서 키트의 SPI IMU 컨트롤러는 100 Hz로 샘플을 읽을 때만 일한다. 앱이 장치를 닫으면 SPI 블록의 clock을 끄고, 전원 도메인을 내리고 싶다. 하지만 같은 버스의 다른 장치가 쓰는 중이면 안 된다. 이 "누가 쓰고 있나"를 커널이 세어 주는 것이 runtime PM이다.

- **usage counter**: `pm_runtime_get*()`가 +1, `pm_runtime_put*()`가 −1. 0이 되면 suspend 후보.
- **autosuspend**: 0이 된 뒤 바로 끄지 않고 `autosuspend_delay_ms`만큼 기다린다. 짧은 간격의 접근마다 껐다 켰다(그 자체가 시간·전력 낭비)하지 않기 위해서다. SSD의 APST idle 타이머와 같다.
- **부모·자식 규칙**: 자식이 active면 부모(버스 컨트롤러)는 suspend할 수 없다(`runtime_active_kids`). 자식을 resume하려면 부모가 먼저 resume된다.
- **genpd(generic power domain)**: 여러 장치가 공유하는 전원 섬(power island). 도메인 안의 장치가 전부 runtime suspended이면 genpd가 도메인 전원을 끈다. DT `power-domains = <&pd 3>;`로 연결된다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 330">
<defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="330" y="20" font-size="14" text-anchor="middle">runtime PM 상태 기계 (dev-&gt;power.runtime_status)</text>
<rect x="40" y="60" width="150" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="115" y="82" font-size="13" text-anchor="middle">RPM_ACTIVE</text>
<text x="115" y="100" font-size="12" text-anchor="middle">clock on, 일함</text>
<rect x="470" y="60" width="150" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="545" y="82" font-size="13" text-anchor="middle">RPM_SUSPENDING</text>
<text x="545" y="100" font-size="12" text-anchor="middle">runtime_suspend()</text>
<rect x="470" y="220" width="150" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="545" y="242" font-size="13" text-anchor="middle">RPM_SUSPENDED</text>
<text x="545" y="260" font-size="12" text-anchor="middle">clock·도메인 off</text>
<rect x="40" y="220" width="150" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="115" y="242" font-size="13" text-anchor="middle">RPM_RESUMING</text>
<text x="115" y="260" font-size="12" text-anchor="middle">runtime_resume()</text>
<line x1="190" y1="78" x2="468" y2="78" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="330" y="56" font-size="12" text-anchor="middle">usage 0 + autosuspend 타이머 만료</text>
<text x="330" y="72" font-size="12" text-anchor="middle">(rpm_suspend, kworker에서)</text>
<line x1="545" y1="110" x2="545" y2="218" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="555" y="160" font-size="12">0 리턴</text>
<text x="538" y="176" font-size="12" text-anchor="end">(-EBUSY면 ACTIVE로)</text>
<line x1="468" y1="250" x2="192" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="330" y="240" font-size="12" text-anchor="middle">pm_runtime_get / resume_and_get</text>
<text x="330" y="290" font-size="12" text-anchor="middle">부모가 먼저 resume된다</text>
<line x1="115" y1="218" x2="115" y2="112" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="125" y="160" font-size="12">0 리턴</text>
<rect x="210" y="120" width="240" height="74" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="330" y="140" font-size="12" text-anchor="middle">usage_count: get +1 / put -1</text>
<text x="330" y="158" font-size="12" text-anchor="middle">active_kids: 깨어 있는 자식 수</text>
<text x="330" y="176" font-size="12" text-anchor="middle">control=on 이면 usage +1 고정</text>
</svg>
```

그림 4 — runtime PM 상태 기계. 네 상태가 `runtime_status` sysfs와 `rpm_status` tracepoint에 그대로 보인다. 콜백이 에러를 돌려주면 이전 상태로 되돌아간다.

### 4.2 API 한 장

| API | 하는 일 | 쓰는 곳 |
|---|---|---|
| `devm_pm_runtime_enable(dev)` | runtime PM 켜기 (remove 때 자동 disable) | probe |
| `pm_runtime_set_active(dev)` | "지금 HW가 켜져 있다"고 알림 (enable 전에) | probe에서 HW를 켜 둔 경우 |
| `pm_runtime_use_autosuspend` / `set_autosuspend_delay` | autosuspend 사용, 지연(ms) | probe |
| `pm_runtime_resume_and_get(dev)` | usage+1, 동기 resume, 실패 시 usage 원복 | 장치를 쓰기 직전 |
| `pm_runtime_get_sync(dev)` | 같지만 실패해도 usage가 올라간 채 남음 (함정) | 옛 코드 |
| `pm_runtime_put_autosuspend(dev)` | (6.18) last_busy 갱신 + usage−1, 타이머 예약 | 다 쓴 직후 |
| `pm_runtime_put_sync(dev)` | usage−1, 0이면 바로 동기 suspend | 즉시 꺼야 할 때 |
| `pm_runtime_force_suspend/resume` | system sleep에서 runtime 콜백 재사용 | `dev_pm_ops`의 system sleep 칸 |

6.18의 `include/linux/pm_runtime.h`를 보면 `pm_runtime_put_autosuspend()`가 이제 안에서 `pm_runtime_mark_last_busy()`를 부른다. 옛 드라이버에 흔한 "`mark_last_busy` 다음 `put_autosuspend`" 두 줄은 지금은 한 줄로 충분하다(남아 있어도 해롭지는 않다).

### 4.3 예제 10 — `n5_rpm`: autosuspend + wakeup source 드라이버 (1/2)

무엇을 확인하는 코드인지: misc 장치를 열면 resume, 닫으면 500 ms 뒤 autosuspend하는 플랫폼 드라이버. `write`로 wakeup source를 잡고 놓는 기능(§6)도 넣었다. 콜백은 메시지만 찍는다 — 실제 clock·레지스터 처리는 N6.

```c
// n5_rpm: runtime PM autosuspend + wakeup source demo (N5)
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/fs.h>

static struct platform_device *n5_pdev;
static struct device *n5_dev;

static int n5_open(struct inode *i, struct file *f)
{
	return pm_runtime_resume_and_get(n5_dev);	/* usage++ , resume if needed */
}
static int n5_release(struct inode *i, struct file *f)
{
	pm_runtime_put_autosuspend(n5_dev);	/* mark_last_busy + usage-- */
	return 0;
}
static ssize_t n5_write(struct file *f, const char __user *b, size_t n, loff_t *o)
{
	char c;
	if (n < 1 || get_user(c, b))
		return -EFAULT;
	if (c == 'h') pm_stay_awake(n5_dev);		/* hold wakeup source */
	if (c == 'r') pm_relax(n5_dev);			/* release it */
	if (c == 'e') pm_wakeup_event(n5_dev, 0);	/* one-shot event */
	return n;
}
static const struct file_operations n5_fops = {
	.owner = THIS_MODULE, .open = n5_open,
	.release = n5_release, .write = n5_write,
};
static struct miscdevice n5_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "n5_rpm", .fops = &n5_fops,
};

static int n5_rt_suspend(struct device *d) { dev_info(d, "runtime_suspend: clock off\n"); return 0; }
static int n5_rt_resume(struct device *d)  { dev_info(d, "runtime_resume: clock on\n");  return 0; }
static const struct dev_pm_ops n5_pm = {
	RUNTIME_PM_OPS(n5_rt_suspend, n5_rt_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};
```

### 4.4 예제 10 — `n5_rpm` (2/2): probe와 장치 등록

```c
static int n5_probe(struct platform_device *pdev)
{
	n5_dev = &pdev->dev;
	pm_runtime_set_autosuspend_delay(n5_dev, 500);	/* ms */
	pm_runtime_use_autosuspend(n5_dev);
	devm_pm_runtime_enable(n5_dev);			/* starts "suspended" */
	device_init_wakeup(n5_dev, true);		/* creates wakeup source */
	n5_misc.parent = n5_dev;
	return misc_register(&n5_misc);
}
static void n5_remove(struct platform_device *pdev)
{
	misc_deregister(&n5_misc);
	device_init_wakeup(n5_dev, false);
}
static struct platform_driver n5_drv = {
	.probe = n5_probe, .remove = n5_remove,
	.driver = { .name = "n5_rpm", .pm = pm_ptr(&n5_pm) },
};
static int __init n5_init(void)
{
	int ret = platform_driver_register(&n5_drv);
	if (ret)
		return ret;
	n5_pdev = platform_device_register_simple("n5_rpm", -1, NULL, 0);
	if (IS_ERR(n5_pdev)) {
		platform_driver_unregister(&n5_drv);
		return PTR_ERR(n5_pdev);
	}
	return 0;
}
static void __exit n5_exit(void)
{
	platform_device_unregister(n5_pdev);
	platform_driver_unregister(&n5_drv);
}
module_init(n5_init);
module_exit(n5_exit);
MODULE_LICENSE("GPL");
```

```make
obj-m += n5_rpm.o
```

DT 노드 없이 모듈이 스스로 `platform_device`를 만든다(G3). `pm_runtime_set_active()`를 부르지 않았으므로 장치는 "suspended" 상태로 시작한다 — HW를 probe에서 켜 두는 실제 드라이버라면 enable 전에 `set_active`를 불러야 상태가 맞는다. `SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)`는 "system suspend 때는 runtime suspend 콜백을 재사용하라"는 흔한 한 줄이다. 이 조합의 함정과 `prepare`/`suspend_late`/`noirq` 콜백을 언제 쓰는지는 N6.

### 4.5 예제 11 — sysfs로 상태 전이 보기

무엇을 확인하는 코드인지: 장치의 `power/` 디렉터리를 읽고, fd를 열고 닫으면서 `runtime_status`·`runtime_usage`가 어떻게 바뀌는지 본다. 먼저 `Image-sec`에서 처음 상태:

```sh
insmod /n5_rpm.ko; P=/sys/devices/platform/n5_rpm/power
for f in control runtime_status autosuspend_delay_ms runtime_active_time runtime_suspended_time wakeup; do echo "$f=$(cat $P/$f)"; done
exec 3>/dev/n5_rpm; echo "after open: $(cat $P/runtime_status)"
exec 3>&-; echo "after close: $(cat $P/runtime_status)"; sleep 1; echo "after 1s: $(cat $P/runtime_status)"
dmesg | tail -3
```

```text
control=auto
runtime_status=suspended
autosuspend_delay_ms=500
runtime_active_time=0
runtime_suspended_time=427
wakeup=enabled
after open: active
after close: active
after 1s: suspended
[   28.566415] n5_rpm: module verification failed: signature and/or required key missing - tainting kernel
[   29.201859] n5_rpm n5_rpm: runtime_resume: clock on
[   30.069926] n5_rpm n5_rpm: runtime_suspend: clock off
```

(이 커널에는 `PM_ADVANCED_DEBUG`가 없어서 `runtime_usage` 파일이 없다 — `cat` 에러 줄은 뺐다.) `Image-trace`에서는 usage와 자식 수까지 보인다:

```sh
P=/sys/devices/platform/n5_rpm/power
echo "runtime_usage(open fd)=$(cat $P/runtime_usage) status=$(cat $P/runtime_status) active_kids=$(cat $P/runtime_active_kids)"
exec 3>&-; echo "closed: usage=$(cat $P/runtime_usage) status=$(cat $P/runtime_status)"
sleep 1; echo "1s later: $(cat $P/runtime_status) active_time=$(cat $P/runtime_active_time) suspended_time=$(cat $P/runtime_suspended_time)"
echo on > $P/control; echo "control=on: $(cat $P/runtime_status) usage=$(cat $P/runtime_usage)"
echo auto > $P/control; sleep 1; echo "control=auto: $(cat $P/runtime_status)"
```

```text
runtime_usage(open fd)=1 status=active active_kids=0
closed: usage=0 status=active
1s later: suspended active_time=6347 suspended_time=2165
control=on: active usage=1
control=auto: suspended
```

출력에서 볼 것:

- 닫은 직후 usage는 0인데 status는 아직 `active`다 — autosuspend 500 ms 대기 중. 1초 뒤 `suspended`. dmesg의 resume(29.20) → suspend(30.07) 간격이 0.87초인 것은 셸이 `sleep 1`까지 가는 TCG 지연 + 500 ms 타이머다.
- `control`에 `on`을 쓰면 유저가 usage를 하나 잡은 것과 같아서 장치가 바로 깨고 다시는 안 잔다. 실보드에서 "runtime PM 때문에 버그가 나나?"를 1초 만에 가르는 스위치다(전원 버그 bisect의 첫 수). powertop의 "Tunables" 탭이 바꾸는 것도 이 파일이다.
- `runtime_active_time`/`runtime_suspended_time`(ms)은 "이 장치가 얼마나 자주 깨어 있었나"를 장비 전체에서 훑을 때 쓴다:

```sh
grep -H . /sys/devices/platform/*/power/runtime_active_time 2>/dev/null | sort -t: -k2 -n | tail
```

### 4.6 예제 12 — rpm tracepoint로 전이 과정 보기

무엇을 확인하는 코드인지: `rpm:*` 이벤트를 `n5_rpm`으로 필터해서 open → close → autosuspend를 커널 안쪽 순서로 본다 (`Image-trace`).

```sh
echo > $T/trace
echo 'name == "n5_rpm"' > $T/events/rpm/filter; echo 1 > $T/events/rpm/enable
exec 3>/dev/n5_rpm; sleep 0.2; exec 3>&-; sleep 1
echo 0 > $T/events/rpm/enable; grep -v "^#" $T/trace
```

```text
           t6.sh-76      [000] d..1.    67.634839: rpm_resume: n5_rpm flags-4 cnt-1  dep-0  auto-1 p-0 irq-0 child-0
           t6.sh-76      [000] d..1.    67.636424: rpm_status: n5_rpm status=RPM_RESUMING
           t6.sh-76      [000] d..1.    67.636721: rpm_status: n5_rpm status=RPM_ACTIVE
           t6.sh-76      [000] d..1.    67.636790: rpm_idle: n5_rpm flags-1 cnt-1  dep-0  auto-1 p-0 irq-0 child-0
           t6.sh-76      [000] d..1.    67.636937: rpm_return_int: rpm_idle+0xdc/0x3c0:n5_rpm ret=-11
           t6.sh-76      [000] d..1.    67.637133: rpm_return_int: rpm_resume+0x140/0x650:n5_rpm ret=0
           t6.sh-76      [000] d..1.    67.935607: rpm_suspend: n5_rpm flags-d cnt-0  dep-0  auto-1 p-0 irq-0 child-0
           t6.sh-76      [000] d..1.    67.935794: rpm_return_int: rpm_suspend+0x168/0x590:n5_rpm ret=0
          <idle>-0       [000] d.h2.    68.559648: rpm_suspend: n5_rpm flags-9 cnt-0  dep-0  auto-1 p-0 irq-0 child-0
          <idle>-0       [000] dNh2.    68.559766: rpm_return_int: rpm_suspend+0x168/0x590:n5_rpm ret=0
     kworker/0:0-9       [000] d..1.    68.560108: rpm_suspend: n5_rpm flags-a cnt-0  dep-0  auto-1 p-0 irq-0 child-0
     kworker/0:0-9       [000] d..1.    68.560121: rpm_status: n5_rpm status=RPM_SUSPENDING
     kworker/0:0-9       [000] d..1.    68.569909: rpm_status: n5_rpm status=RPM_SUSPENDED
     kworker/0:0-9       [000] d..1.    68.569962: rpm_return_int: rpm_suspend+0x168/0x590:n5_rpm ret=0
```

출력에서 볼 것: (1) open이 `rpm_resume`(cnt-1) → RESUMING → ACTIVE를 **셸 태스크 문맥에서 동기로** 했다. 바로 뒤 `rpm_idle`이 `-11`(EAGAIN)인 것은 usage가 1이라 idle 처리를 거절한 것이다. (2) close(67.9356)의 `rpm_suspend`는 autosuspend 타이머만 걸고 돌아온다(ret=0). (3) 약 0.62초 뒤(TCG라 500 ms보다 늦다) 타이머가 **hardirq 문맥**(`d.h2.`, `<idle>-0`)에서 만료되어 비동기 요청만 넣고(`flags-9` = `RPM_ASYNC` + `RPM_AUTO`), 실제 suspend는 **kworker**가 `pm_runtime_work()`에서 한다(`flags-a` = `RPM_NOWAIT` + `RPM_AUTO`). flags 비트는 `include/linux/pm_runtime.h`: ASYNC 0x1, NOWAIT 0x2, GET_PUT 0x4, AUTO 0x8 — close 때의 `flags-d`는 ASYNC + GET_PUT + AUTO다. 그래서 runtime_suspend 콜백은 잠들 수 있다(I2C로 센서를 sleep시키는 명령을 보내도 된다). 반대로 `pm_runtime_irq_safe()`를 쓴 장치는 원자 문맥에서 콜백이 불리므로 잠들면 안 된다(N6).

---

## 5. 시스템 sleep — suspend/resume

### 5.1 상태 네 가지

| `/sys/power/state`에 쓰는 값 | `mem_sleep` 이름 | 무엇을 하나 | 깨우는 데 | ARM에서 누가 |
|---|---|---|---|---|
| `freeze` | `s2idle` | 태스크 동결 + 장치 suspend + 모든 CPU를 cpuidle의 가장 깊은 상태로. CPU는 꺼지지 않는다 | 빠르다 (ms) | 순수 커널. 펌웨어 지원 불필요 |
| `standby` | `shallow` | s2idle + non-boot CPU offline + 플랫폼 저전력 | 중간 | 플랫폼 `suspend_ops` (ARM에서는 드묾) |
| `mem` | `deep` (또는 기본값이 s2idle이면 그것) | DDR self-refresh, SoC 대부분 off, 깨면 펌웨어 경유 재시작 | 수백 ms~ | PSCI `SYSTEM_SUSPEND` (TF-A가 PMIC·DDR 처리) |
| `disk` | — | 메모리 이미지를 swap에 쓰고 전원 off | 초 단위 | 커널 hibernation (임베디드에서는 드묾) |

`mem`이 실제로 무엇을 뜻하는지는 `/sys/power/mem_sleep`이 정한다. 대괄호가 현재 선택이다. 실보드(i.MX8M, Jetson 등)에서는 `s2idle [deep]`처럼 보이고, 예제 2의 QEMU는 `[s2idle]`뿐이다. 커널 인자 `mem_sleep_default=deep`으로 기본값을 바꿀 수 있다.

s2idle와 deep의 차이를 Don의 말로 하면: s2idle은 "모든 코어를 WFI/retention으로 재우고 인터럽트를 기다리는 펌웨어 idle 루프"이고, deep은 "컨트롤러 전원을 내리고 always-on 도메인의 wake 로직만 살려 두는 PS4"다. s2idle이 덜 아끼는 대신 플랫폼 펌웨어 없이도 되고 깨는 것이 빠르다.

### 5.2 단계 — 진입은 위에서 아래, 복귀는 아래에서 위

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 470">
<defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">suspend 사다리 — 진입 순서의 정확한 역순으로 복귀한다</text>
<g font-size="12">
<rect x="20" y="36" width="290" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="30" y="56">sync + freeze_processes (태스크 동결)</text>
<rect x="20" y="72" width="290" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="30" y="92">dpm_prepare — -&gt;prepare(), 새 자식 금지</text>
<rect x="20" y="108" width="290" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="30" y="128">dpm_suspend — -&gt;suspend() 대부분 여기</text>
<rect x="20" y="144" width="290" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="30" y="164">dpm_suspend_late — runtime PM 끔</text>
<rect x="20" y="180" width="290" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="30" y="200">dpm_suspend_noirq — 장치 IRQ 꺼진 채</text>
<rect x="20" y="230" width="135" height="58" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="250">s2idle_loop</text><text x="30" y="266">CPU 모두 idle</text><text x="30" y="282">타임키핑 동결</text>
<rect x="165" y="230" width="145" height="140" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="175" y="250">deep 경로만:</text><text x="175" y="268">non-boot CPU off</text><text x="175" y="286">IRQ off</text><text x="175" y="304">syscore_suspend</text><text x="175" y="322">(GIC, 타이머)</text><text x="175" y="340">suspend_ops-&gt;enter</text><text x="170" y="358" font-size="11">PSCI SYSTEM_SUSPEND</text>
<rect x="370" y="36" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="380" y="56">thaw_processes — 유저 재개</text>
<rect x="370" y="72" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="380" y="92">dpm_complete — -&gt;complete()</text>
<rect x="370" y="108" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="380" y="128">dpm_resume — -&gt;resume()</text>
<rect x="370" y="144" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="380" y="164">dpm_resume_early — runtime PM 다시 켬</text>
<rect x="370" y="180" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="380" y="200">dpm_resume_noirq</text>
<rect x="370" y="230" width="290" height="58" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="380" y="250">wakeup IRQ (RTC, GPIO, UART ...)</text><text x="380" y="266">deep: 펌웨어 → cpu_resume → syscore</text><text x="380" y="282">→ CPU 다시 online</text>
</g>
<line x1="165" y1="66" x2="165" y2="70" stroke="currentColor"/>
<line x1="315" y1="259" x2="366" y2="259" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="515" y1="228" x2="515" y2="212" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="10" y1="40" x2="10" y2="226" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="668" y1="226" x2="668" y2="40" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="20" y="400" font-size="12">각 dpm_* 단계는 모든 장치에 대해 끝나야 다음 단계로 간다 (장치 목록 순서 = 등록 순서,</text>
<text x="20" y="418" font-size="12">자식이 부모보다 먼저 suspend). async 허용 장치는 같은 단계 안에서 병렬로 돈다 (pm_async=1).</text>
<text x="20" y="436" font-size="12">어느 콜백이든 에러를 돌려주면 그 지점에서 역방향으로 resume하고 suspend는 실패한다.</text>
<text x="20" y="454" font-size="12">s2idle에는 syscore 단계가 없다 — 커널 소스 suspend_enter()의 분기 (아래 발췌).</text>
</svg>
```

그림 5 — suspend/resume 사다리. 진입은 왼쪽 위에서 아래로, 복귀는 오른쪽 아래에서 위로. s2idle은 noirq 단계 다음에 `s2idle_loop()`로 들어가 CPU를 cpuidle로 재우고, deep은 CPU를 offline하고 syscore를 거쳐 PSCI `SYSTEM_SUSPEND`로 펌웨어에 넘긴다.

커널 소스 발췌 (`kernel/power/suspend.c` 6.18.55, `suspend_enter()`, 에러 경로 생략):

```c
	error = dpm_suspend_late(PMSG_SUSPEND);
	...
	error = dpm_suspend_noirq(PMSG_SUSPEND);
	...
	if (suspend_test(TEST_PLATFORM))
		goto Platform_wake;

	if (state == PM_SUSPEND_TO_IDLE) {
		s2idle_loop();
		goto Platform_wake;
	}

	error = pm_sleep_disable_secondary_cpus();
	if (error || suspend_test(TEST_CPUS))
		goto Enable_cpus;

	arch_suspend_disable_irqs();
	...
	error = syscore_suspend();
	if (!error) {
		*wakeup = pm_wakeup_pending();
		if (!(suspend_test(TEST_CORE) || *wakeup)) {
			trace_suspend_resume(TPS("machine_suspend"), state, true);
			error = suspend_ops->enter(state);	/* PSCI SYSTEM_SUSPEND */
			trace_suspend_resume(TPS("machine_suspend"), state, false);
		} else if (*wakeup) {
			error = -EBUSY;
		}
		syscore_resume();
	}
```

### 5.3 예제 13 — RTC 알람으로 깨는 s2idle을 처음부터 끝까지 찍기

무엇을 확인하는 코드인지: `n5_rpm`을 연 상태(장치 active)에서 RTC 알람 3초를 걸고 `freeze`한다. `pm_print_times`로 장치별 콜백 시간을, ftrace `power:suspend_resume`·`device_pm_callback_*`·`wakeup_source_*`·`rpm_*`로 단계 경계를 기록한다. 트레이스 시계는 `boot`(suspend 시간을 포함하는 `CLOCK_BOOTTIME`)로 바꿨다 — 기본 `local` 시계는 s2idle 동안 멈춘다.

```sh
#!/bin/sh
T=/sys/kernel/tracing; mount -t tracefs none $T
echo 1 > /sys/power/pm_print_times; echo 1 > /sys/power/pm_debug_messages
insmod /n5_rpm.ko; exec 3>/dev/n5_rpm
echo boot > $T/trace_clock; echo 8192 > $T/buffer_size_kb
for e in power/suspend_resume power/device_pm_callback_start power/device_pm_callback_end \
         rpm/rpm_suspend rpm/rpm_resume power/wakeup_source_activate power/wakeup_source_deactivate; do
  echo 1 > $T/events/$e/enable; done
echo "== full freeze, RTC +3 s, n5_rpm open (active)"
echo +3 > /sys/class/rtc/rtc0/wakealarm
echo freeze > /sys/power/state; echo "rc=$?"
echo 0 > $T/tracing_on
echo "=== DMESG"; dmesg | sed -n '/PM: suspend entry/,$p'
echo "=== TRACE"; grep -v "^#" $T/trace
```

dmesg의 요약 줄 (장치별 `calling`/`returned` 수백 줄 중 단계 경계만 남겼다, `grep -v "calling\|returned"`):

```text
[   26.709773][   T76] PM: suspend entry (s2idle)
[   27.064558][   T76] PM: suspend of devices complete after 222.740 msecs
[   27.064883][   T76] PM: start suspend of devices complete after 249.249 msecs
[   27.106240][   T76] PM: late suspend of devices complete after 41.222 msecs
[   27.216527][   T76] PM: noirq suspend of devices complete after 108.035 msecs
[   27.217720][   T76] PM: suspend-to-idle
[   27.224058][    T0] Timekeeping suspended for 2.481 seconds
[   27.225127][    C0] PM: Triggering wakeup from IRQ 19
[   27.239931][   T76] PM: resume from suspend-to-idle
[   27.262639][   T76] PM: noirq resume of devices complete after 22.569 msecs
[   27.288286][   T76] PM: early resume of devices complete after 24.748 msecs
[   27.362005][   T76] PM: resume of devices complete after 71.859 msecs
[   27.401593][   T76] Restarting tasks: Starting
[   27.405166][   T76] Restarting tasks: Done
[   27.407539][   T76] PM: suspend exit
```

장치별 줄의 예 (`pm_print_times`, 같은 실행에서 몇 줄):

```text
[   26.857178][   T76] n5_rpm n5_rpm: PM: calling platform_pm_suspend @ 76, parent: platform
[   26.861955][   T76] n5_rpm n5_rpm: runtime_suspend: clock off
[   26.868530][   T76] n5_rpm n5_rpm: PM: platform_pm_suspend returned 0 after 6981 usecs
[   26.939900][   T76] uart-pl011 9000000.pl011: PM: calling pl011_suspend @ 76, parent: platform
[   26.965600][   T76] uart-pl011 9000000.pl011: PM: pl011_suspend returned 0 after 25562 usecs
[   26.906532][   T12] virtio-pci 0000:00:01.0: PM: calling pci_pm_suspend @ 12, parent: pci0000:00
[   26.996896][   T12] virtio-pci 0000:00:01.0: PM: pci_pm_suspend returned 0 after 90185 usecs
```

ftrace의 단계 경계 (`suspend_resume`, `wakeup_source_*`, `n5_rpm` 관련 줄만, 나머지 rpm 이벤트 생략):

```text
           t4.sh-76      [000] .....    23.275255: suspend_resume: suspend_enter[1] begin
           t4.sh-76      [000] .....    23.275445: suspend_resume: sync_filesystems[0] begin
           t4.sh-76      [000] .....    23.285112: suspend_resume: sync_filesystems[0] end
           t4.sh-76      [001] .....    23.331936: suspend_resume: freeze_processes[0] begin
           t4.sh-76      [001] .....    23.366502: suspend_resume: freeze_processes[0] end
           t4.sh-76      [001] .....    23.366553: suspend_resume: suspend_enter[1] end
           t4.sh-76      [001] .....    23.380862: suspend_resume: dpm_prepare[2] begin
           t4.sh-76      [001] .....    23.405406: suspend_resume: dpm_prepare[2] end
           t4.sh-76      [001] .....    23.406736: suspend_resume: dpm_suspend[2] begin
           t4.sh-76      [001] .....    23.426667: device_pm_callback_start: n5_rpm n5_rpm, parent: platform, bus [suspend]
           t4.sh-76      [001] .....    23.433558: device_pm_callback_end: n5_rpm n5_rpm, err=0
           t4.sh-76      [000] .....    23.629850: suspend_resume: dpm_suspend[2] end
           t4.sh-76      [000] .....    23.630087: suspend_resume: dpm_suspend_late[2] begin
           t4.sh-76      [001] .....    23.671404: suspend_resume: dpm_suspend_late[2] end
           t4.sh-76      [001] .....    23.673480: suspend_resume: dpm_suspend_noirq[2] begin
           t4.sh-76      [001] .....    23.781734: suspend_resume: dpm_suspend_noirq[2] end
           t4.sh-76      [001] .....    23.782919: suspend_resume: machine_suspend[1] begin
          <idle>-0       [001] d..2.    23.788924: suspend_resume: timekeeping_freeze[1] begin
          <idle>-0       [000] dN.2.    26.274582: suspend_resume: timekeeping_freeze[0] end
           t4.sh-76      [001] .....    26.289878: suspend_resume: machine_suspend[1] end
           t4.sh-76      [001] .....    26.290475: suspend_resume: dpm_resume_noirq[16] begin
           t4.sh-76      [001] .....    26.313114: suspend_resume: dpm_resume_noirq[16] end
          <idle>-0       [000] d.h3.    26.314430: wakeup_source_activate: 9010000.pl031 state=0x1
           t4.sh-76      [001] .....    26.314764: suspend_resume: dpm_resume_early[16] begin
     kworker/0:0-9       [000] d..2.    26.317390: wakeup_source_deactivate: 9010000.pl031 state=0x10000
           t4.sh-76      [000] .....    26.338798: suspend_resume: dpm_resume_early[16] end
           t4.sh-76      [001] .....    26.340531: suspend_resume: dpm_resume[16] begin
           t4.sh-76      [001] .....    26.411880: device_pm_callback_start: n5_rpm n5_rpm, parent: platform, bus [resume]
           t4.sh-76      [001] .....    26.412046: device_pm_callback_end: n5_rpm n5_rpm, err=0
           t4.sh-76      [001] .....    26.412573: suspend_resume: dpm_resume[16] end
           t4.sh-76      [001] .....    26.412821: suspend_resume: dpm_complete[16] begin
           t4.sh-76      [001] .....    26.429862: suspend_resume: dpm_complete[16] end
           t4.sh-76      [001] .....    26.430245: suspend_resume: console_resume_all[1] begin
           t4.sh-76      [001] .....    26.448004: suspend_resume: console_resume_all[1] end
           t4.sh-76      [001] .....    26.448252: suspend_resume: thaw_processes[0] begin
           t4.sh-76      [001] .....    26.455612: suspend_resume: thaw_processes[0] end
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 422">
<text x="10" y="20" font-size="14">s2idle 1회의 단계별 실제 시간 (ftrace suspend_resume, trace_clock=boot)</text>
<text x="222" y="55" font-size="12" text-anchor="end">sync_filesystems</text>
<rect x="230" y="43" width="14.5" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="252.6" y="55" font-size="12">9.7 ms</text>
<text x="222" y="79" font-size="12" text-anchor="end">freeze_processes</text>
<rect x="230" y="67" width="51.9" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="289.9" y="79" font-size="12">34.6 ms</text>
<text x="222" y="103" font-size="12" text-anchor="end">dpm_prepare</text>
<rect x="230" y="91" width="36.8" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="274.8" y="103" font-size="12">24.5 ms</text>
<text x="222" y="127" font-size="12" text-anchor="end">dpm_suspend</text>
<rect x="230" y="115" width="334.6" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="572.6" y="127" font-size="12">223.1 ms</text>
<text x="222" y="151" font-size="12" text-anchor="end">dpm_suspend_late</text>
<rect x="230" y="139" width="61.9" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="299.9" y="151" font-size="12">41.3 ms</text>
<text x="222" y="175" font-size="12" text-anchor="end">dpm_suspend_noirq</text>
<rect x="230" y="163" width="162.4" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="400.4" y="175" font-size="12">108.3 ms</text>
<text x="222" y="199" font-size="12" text-anchor="end">machine_suspend (s2idle)</text>
<rect x="230" y="187" width="330" height="16" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="395.0" y="199" font-size="12" text-anchor="middle">축 생략 — RTC 알람 대기</text>
<text x="568" y="199" font-size="12">2507 ms</text>
<text x="222" y="223" font-size="12" text-anchor="end">dpm_resume_noirq</text>
<rect x="230" y="211" width="33.9" height="16" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="271.9" y="223" font-size="12">22.6 ms</text>
<text x="222" y="247" font-size="12" text-anchor="end">dpm_resume_early</text>
<rect x="230" y="235" width="36.0" height="16" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="274.0" y="247" font-size="12">24.0 ms</text>
<text x="222" y="271" font-size="12" text-anchor="end">dpm_resume</text>
<rect x="230" y="259" width="108.0" height="16" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="346.0" y="271" font-size="12">72.0 ms</text>
<text x="222" y="295" font-size="12" text-anchor="end">dpm_complete</text>
<rect x="230" y="283" width="25.5" height="16" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="263.5" y="295" font-size="12">17.0 ms</text>
<text x="222" y="319" font-size="12" text-anchor="end">console_resume_all</text>
<rect x="230" y="307" width="26.7" height="16" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="264.7" y="319" font-size="12">17.8 ms</text>
<text x="222" y="343" font-size="12" text-anchor="end">thaw_processes</text>
<rect x="230" y="331" width="11.1" height="16" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="249.1" y="343" font-size="12">7.4 ms</text>
<rect x="20" y="359" width="14" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="40" y="370" font-size="12">freeze 단계</text>
<rect x="185" y="359" width="14" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="205" y="370" font-size="12">장치 suspend 단계</text>
<rect x="350" y="359" width="14" height="12" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="370" y="370" font-size="12">잠듦</text>
<rect x="515" y="359" width="14" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="535" y="370" font-size="12">resume 단계</text>
<text x="20" y="392" font-size="12">합계 3.18 s = 진입 508 ms + 잠듦 2507 ms + 복귀 166 ms (단계 사이 간격 포함)</text>
<text x="20" y="410" font-size="12">QEMU TCG + 공유 VM 부하에서 잰 값이다 — 실보드 수치가 아니다</text>
</svg>
```

그림 6 — 위 ftrace 경계에서 계산한 단계별 시간. 장치 suspend 단계(`dpm_suspend` 223 ms)가 진입의 절반 가까이를 차지하고, 그 안에서는 `virtio-pci`(90 ms)와 `pl011`(25.6 ms)이 가장 길다. 잠든 시간 2.5초는 RTC 알람 3초에서 진입 시간을 뺀 값과 맞는다.

출력에서 볼 것:

- **숫자 뒤의 `[1]`, `[2]`, `[16]`** 는 `suspend_resume` 이벤트의 `val`이다. `suspend_enter[1]`·`machine_suspend[1]`은 목표 상태(`PM_SUSPEND_TO_IDLE` = 1), `dpm_*[2]`는 PM 이벤트 `PM_EVENT_SUSPEND`(0x2), `[16]`은 `PM_EVENT_RESUME`(0x10)이다.
- **dmesg 시계와 트레이스 시계가 다르다.** dmesg는 26.7 → 27.4초로 suspend 전체가 0.7초인 것처럼 보이지만, 트레이스(boot 시계)는 23.28 → 26.46초 = 3.18초다. dmesg가 쓰는 `local_clock`은 s2idle 동안 멈추기 때문이다. 그래서 커널이 친절하게 `Timekeeping suspended for 2.481 seconds`를 찍어 준다. "dmesg로 보니 suspend가 0.7초밖에 안 됐다"는 오판의 함정이다.
- **누가 깨웠나**: `PM: Triggering wakeup from IRQ 19`(`pm_debug_messages` 덕분), 그리고 resume 도중 `wakeup_source_activate: 9010000.pl031`. 실보드에서 "무엇이 자꾸 깨우나"는 `/sys/power/pm_wakeup_irq`와 이 줄로 찾는다.
- **`n5_rpm`은 `dpm_suspend` 단계에서** `platform_pm_suspend` → `pm_runtime_force_suspend` → 우리 `runtime_suspend` 콜백 순서로 불렸다(fd가 열려 있어 active였기 때문). resume은 `dpm_resume` 단계다. 콜백 하나가 6.98 ms인 것은 `dev_info` 출력 비용(TCG에서 콘솔은 느리다)이다.
- **장치 콜백은 두 스레드(T76, T12, T31)에서 돈다.** `pm_async=1`이면 async 허용 장치(PCI 등)는 kworker에서 병렬로 처리된다. 순서 의존이 있는 장치끼리는 `device_link`로 묶어야 한다(N6).

### 5.4 예제 14 — `pm_test`: 사다리를 어디까지 내려갔다 올라올지 정하기

무엇을 확인하는 코드인지: `pm_test`에 단계 이름을 쓰면 그 단계까지만 진입하고 `pm_test_delay`초 기다렸다가 복귀한다. 깨울 장치가 없어도 suspend 경로를 시험할 수 있어서 bring-up 초기에 "어느 단계에서 죽나"를 이분 탐색하는 표준 도구다.

```sh
#!/bin/sh
echo 1 > /sys/power/pm_debug_messages
echo 1 > /sys/module/suspend/parameters/pm_test_delay
echo "pm_test: $(cat /sys/power/pm_test)"
for t in freezer devices platform processors core; do
  echo $t > /sys/power/pm_test; s=$(cut -d' ' -f1 /proc/uptime)
  echo freeze > /sys/power/state; rc=$?; e=$(cut -d' ' -f1 /proc/uptime)
  echo "pm_test=$t rc=$rc uptime $s -> $e"
  dmesg | grep -E "suspend debug|Unsupported" | tail -1
done
echo none > /sys/power/pm_test
```

```text
pm_test: [none] core processors platform devices freezer
pm_test=freezer rc=0 uptime 30.19 -> 38.40
[   36.169453][   T77] PM: suspend debug: Waiting for 1 second(s).
pm_test=devices rc=0 uptime 40.10 -> 49.25
[   47.627417][   T77] PM: suspend debug: Waiting for 1 second(s).
pm_test=platform rc=0 uptime 51.33 -> 57.30
[   57.610118][   T77] PM: suspend debug: Waiting for 1 second(s).
sh: write error: Resource temporarily unavailable
pm_test=processors rc=1 uptime 57.73 -> 57.95
[   63.462228][   T77] PM: Unsupported test mode for suspend to idle, please choose none/freezer/devices/platform.
sh: write error: Resource temporarily unavailable
pm_test=core rc=1 uptime 58.53 -> 58.75
[   64.252366][   T77] PM: Unsupported test mode for suspend to idle, please choose none/freezer/devices/platform.
```

출력에서 볼 것:

| 레벨 | 어디서 멈추고 돌아오나 | 걸리는 버그 |
|---|---|---|
| `freezer` | 태스크 동결 직후 | 얼지 않는 커널 스레드, 유저 프로세스의 D 상태 |
| `devices` | `dpm_suspend` 끝 (late 전) | 대부분의 드라이버 `suspend` 콜백 |
| `platform` | `noirq` 끝, 플랫폼 hook 직전 | late/noirq 콜백, IRQ 처리 |
| `processors` | non-boot CPU offline 뒤 | CPU hotplug 경로 (deep 전용) |
| `core` | syscore까지, 펌웨어 호출 직전 | syscore(GIC·타이머 저장) (deep 전용) |

- s2idle은 CPU offline·syscore 단계가 없으므로 `processors`·`core`를 거부하고 `-EAGAIN`(write error: Resource temporarily unavailable)을 돌려준다. 소스의 그 분기(`suspend.c:576`)가 그대로 보인다.
- `pm_test_delay`를 1초로 줄였는데 한 번에 6~9초가 걸렸다(`/proc/uptime`은 suspend 중에도 흐른다). 기다림 1초를 뺀 나머지가 TCG에서 장치 수십 개를 suspend/resume하는 비용이다.
- 실보드에서의 순서: `freezer` → `devices` → `platform` → `processors` → `core` → `none`(진짜). 어느 단계에서 처음 죽는지가 범인의 층을 알려 준다. 장치 하나를 찾으려면 그 단계에서 `pm_print_times`의 마지막 `calling` 줄을 본다(돌아오지 않은 콜백). 콘솔이 죽으면 `no_console_suspend` 커널 인자로 suspend 중에도 UART를 살려 둔다.

---

## 6. wakeup source — "지금 자면 안 된다"를 커널에 알리는 법

### 6.1 정의

- **wakeup source**: 커널 객체(`struct wakeup_source`) 하나. 드라이버가 "처리 중인 깨움 이벤트가 있다"를 표시한다. `device_init_wakeup(dev, true)`가 장치용 wakeup source를 만들고, `pm_stay_awake(dev)`/`pm_relax(dev)`로 잡고 놓는다. `pm_wakeup_event(dev, ms)`는 ms 동안 잡았다가 자동으로 놓는다. Android의 wakelock이 이것이 됐다.
- **wake IRQ**: suspend 중에도 켜 두는 인터럽트(`enable_irq_wake()`, `dev_pm_set_wake_irq()`). pl031 드라이버가 probe에서 `devm_pm_set_wake_irq()`를 부르는 것을 소스(`drivers/rtc/rtc-pl031.c`)에서 볼 수 있다.
- **`/sys/devices/.../power/wakeup`**: 이 장치가 시스템을 깨워도 되는지(enabled/disabled). 유저 정책이다.
- **`/sys/kernel/debug/wakeup_sources`**: 모든 wakeup source의 통계. `active_count`(잡은 횟수), `event_count`, `wakeup_count`(suspend를 중단시킨 횟수), `total_time`·`max_time`(ms), `prevent_suspend_time`.

### 6.2 예제 15 — 잡고 있어도 suspend가 된다? (Image-sec)

무엇을 확인하는 코드인지: `n5_rpm`의 wakeup source를 잡은(`h`) 채로 `echo freeze`를 하면 어떻게 되는가.

```sh
insmod /n5_rpm.ko; exec 3>/dev/n5_rpm
echo h >&3; grep -E "^name|n5_rpm" /sys/kernel/debug/wakeup_sources
echo +3 > /sys/class/rtc/rtc0/wakealarm
echo freeze > /sys/power/state; echo "rc=$?"
dmesg | tail -12
```

```text
name		active_count	event_count	wakeup_count	expire_count	active_since	total_time	max_time	last_change	prevent_suspend_time
n5_rpm      	1		1		0		0		32		32		32		3587		0
rc=0
[    3.842702] Freezing user space processes completed (elapsed 0.006 seconds)
[    3.843068] OOM killer disabled.
[    3.843214] Freezing remaining freezable tasks
[    3.855348] Freezing remaining freezable tasks completed (elapsed 0.012 seconds)
[    3.855557] printk: Suspending console(s) (use no_console_suspend to debug)
[    3.873508] n5_rpm n5_rpm: runtime_suspend: clock off
[    3.967560] n5_rpm n5_rpm: runtime_resume: clock on
[    3.984910] OOM killer enabled.
[    3.985011] Restarting tasks: Starting
[    4.000327] Restarting tasks: Done
[    4.000573] random: crng reseeded on system resumption
[    4.001046] PM: suspend exit
```

출력에서 볼 것: **suspend가 그냥 성공했다(rc=0).** wakeup source를 잡고 있었는데도. 많은 사람이 "wakeup source를 잡으면 suspend가 막힌다"고 알고 있지만, 커널 코드는 이렇다 (`drivers/base/power/wakeup.c`, `pm_wakeup_pending()`):

```c
	raw_spin_lock_irqsave(&events_lock, flags);
	if (events_check_enabled) {
		unsigned int cnt, inpr;

		split_counters(&cnt, &inpr);
		ret = (cnt != saved_count || inpr > 0);
		events_check_enabled = !ret;
	}
	raw_spin_unlock_irqrestore(&events_lock, flags);

	if (ret) {
		pm_pr_dbg("Wakeup pending, aborting suspend\n");
		pm_print_active_wakeup_sources();
	}

	return ret || atomic_read(&pm_abort_suspend) > 0;
```

잡혀 있는 wakeup source(`inpr > 0`)를 보는 것은 **`events_check_enabled`가 켜졌을 때만**이다. 그리고 그것을 켜는 방법은 유저가 `/sys/power/wakeup_count`에 값을 써 넣는 것(또는 `CONFIG_PM_AUTOSLEEP`)뿐이다. 즉 wakeup source는 **wakeup_count 프로토콜을 쓰는 suspend 관리자(Android의 suspend 서비스가 대표적)와 짝을 이룰 때만** suspend를 막는다. 단순히 `echo mem > /sys/power/state`하는 스크립트 앞에서는 무력하다. (예외: suspend 도중 wake IRQ가 들어오면 `pm_system_wakeup()`이 `pm_abort_suspend`를 올려서 언제나 중단된다.)

### 6.3 wakeup_count 프로토콜 — race 없는 suspend

문제: 데몬이 "할 일 없음 → suspend" 결정을 내린 순간과 실제로 `echo mem`을 쓰는 순간 사이에 버튼 인터럽트가 들어오면? 이벤트는 처리되지 않은 채로 잠든다. 해결은 SSD 펌웨어의 "sequence number 비교 후 sleep"과 같다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 320">
<defs><marker id="ah6" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="330" y="20" font-size="14" text-anchor="middle">wakeup_count 프로토콜 — 읽은 값을 그대로 다시 써야 잘 수 있다</text>
<line x1="120" y1="40" x2="120" y2="300" stroke="currentColor" stroke-dasharray="4 3"/>
<line x1="500" y1="40" x2="500" y2="300" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="120" y="50" font-size="13" text-anchor="middle">suspend 데몬</text>
<text x="500" y="50" font-size="13" text-anchor="middle">커널 (wakeup 카운터)</text>
<line x1="120" y1="80" x2="496" y2="80" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="300" y="74" font-size="12" text-anchor="middle">1. read wakeup_count (잡힌 source가 있으면 블록)</text>
<line x1="496" y1="104" x2="124" y2="104" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="300" y="98" font-size="12" text-anchor="middle">N</text>
<rect x="20" y="116" width="200" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="138" font-size="12" text-anchor="middle">2. 유저 공간 할 일 정리</text>
<line x1="120" y1="174" x2="496" y2="174" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="300" y="168" font-size="12" text-anchor="middle">3. write N → 그사이 이벤트 있었으면 EINVAL</text>
<rect x="400" y="186" width="200" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="500" y="208" font-size="12" text-anchor="middle">events_check_enabled = 1</text>
<line x1="120" y1="240" x2="496" y2="240" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="300" y="234" font-size="12" text-anchor="middle">4. write mem/freeze → state</text>
<rect x="380" y="252" width="240" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="500" y="268" font-size="12" text-anchor="middle">동결·각 단계에서 카운터 재확인</text>
<text x="500" y="285" font-size="12" text-anchor="middle">바뀌었거나 잡혀 있으면 -EBUSY</text>
</svg>
```

그림 7 — wakeup_count 프로토콜. 데몬이 읽은 카운터 값을 그대로 써 넣으면 커널은 "이 값 이후에 이벤트가 생기면 suspend를 중단하라"는 감시를 켠다. 3번에서 실패하면 데몬은 이벤트를 처리하고 처음부터 다시 한다.

### 6.4 예제 16 — 프로토콜의 세 가지 결말

무엇을 확인하는 코드인지: (가) 읽은 뒤 이벤트가 생기면 write가 실패, (나) 감시를 켠 뒤 wakeup source를 잡으면 freeze가 중단, (다) 잡혀 있는 동안 read가 블록. (가)는 `Image-sec`, (나)(다)는 `Image-trace`에서 돌렸다.

```sh
W=/sys/power/wakeup_count
echo r >&3; c=$(cat $W); echo "read wakeup_count=$c"
echo e >&3; echo "n5_rpm fired pm_wakeup_event()"
echo $c > $W; echo "write back rc=$? (stale count)"
c=$(cat $W); echo $c > $W; echo "re-read $c, write back rc=$?"
```

```text
read wakeup_count=2
n5_rpm fired pm_wakeup_event()
sh: write error: Invalid argument
write back rc=1 (stale count)
re-read 3, write back rc=0
```

```sh
echo 1 > /sys/power/pm_debug_messages
insmod /n5_rpm.ko; exec 3>/dev/n5_rpm
c=$(cat /sys/power/wakeup_count); echo $c > /sys/power/wakeup_count && echo "wakeup_count $c accepted"
echo h >&3; echo "n5_rpm: pm_stay_awake()"
echo freeze > /sys/power/state; echo "freeze rc=$?"
dmesg | grep -E -A2 "Wakeup pending|PM: suspend entry"
(cat /sys/power/wakeup_count > /tmp/wc; echo "read returned $(cat /tmp/wc) at $(cut -d' ' -f1 /proc/uptime)") &
sleep 2; echo "after 2 s at $(cut -d' ' -f1 /proc/uptime): reader still blocked"
grep -E "^name|n5_rpm" /sys/kernel/debug/wakeup_sources
echo r >&3; echo "n5_rpm: pm_relax()"; wait
```

```text
wakeup_count 0 accepted
n5_rpm: pm_stay_awake()
[   47.714818][   T76] Freezing user space processes aborted after 0.001 seconds (1 tasks refusing to freeze, wq_busy=0):
sh: write error: Device or resource busy
freeze rc=1
[   47.591681][   T76] PM: suspend entry (s2idle)
[   47.609621][   T76] Filesystems sync: 0.017 seconds
[   47.713056][   T76] Freezing user space processes
after 2 s at 45.10: reader still blocked
name		active_count	event_count	wakeup_count	expire_count	active_since	total_time	max_time	last_change	prevent_suspend_time
n5_rpm      	1		1		1		0		3873		3873		3873		41581		0
n5_rpm: pm_relax()
read returned 1 at 46.11
```

출력에서 볼 것:

- (가) 읽은 값 2를 쓰려는데 그사이 이벤트로 3이 되어 `EINVAL`. 다시 읽은 3은 받아들여진다.
- (나) 감시가 켜진 상태에서 잡힌 source가 있으니 **동결 단계**에서 중단됐다(`try_to_freeze_tasks()`가 루프마다 `pm_wakeup_pending()`을 본다). `write error: Device or resource busy` = `-EBUSY`. 커널 메시지 "1 tasks refusing to freeze"는 중단 순간 아직 덜 얼었던 태스크 수일 뿐 진짜 범인이 아니다 — 이 줄만 보고 "얼지 않는 태스크"를 찾아 헤매는 함정이 있다. `n5_rpm`의 `wakeup_count` 열이 1로 올라간 것이 진짜 증거다(이번에는 감시가 켜져 있었으므로 카운트됐다. 예제 15에서는 0이었다). `Wakeup pending, aborting suspend` 디버그 줄은 찍히지 않았다. 이 메시지는 `pm_pr_dbg()`라서 `pm_debug_messages_should_print()` → `pm_sleep_transition_in_progress()`가 참일 때만 나오는데, `pm_suspend_target_state`는 동결이 끝난 뒤 `suspend_devices_and_enter()`(`suspend.c:505`)에서야 설정된다. 동결 단계의 중단은 디버그 메시지 없이 지나간다 — `wakeup_sources`를 직접 봐야 하는 이유다.
- (다) source가 잡혀 있는 동안 `wakeup_count` read는 **블록**한다(`pm_get_wakeup_count(..., true)`가 in-progress 이벤트가 0이 될 때까지 기다린다). `pm_relax()` 뒤에 풀렸다. 데몬은 이 블록 덕분에 바쁘게 폴링할 필요가 없다. (dmesg 시계와 `/proc/uptime`은 서로 다른 시계라 숫자를 비교하지 말 것.)

### 6.5 함정 — wakeup storm

어떤 장치가 suspend 직후마다 깨운다면 장비는 "자려다 깨고, 자려다 깨는" 상태로 배터리를 태운다. 증거는 `/sys/power/suspend_stats/`(`success`, `fail`, `last_failed_dev`, `last_failed_step`), `wakeup_sources`의 `wakeup_count`·`event_count` 증가 속도, `/sys/power/pm_wakeup_irq`. 예제 15·16(가)를 돌린 같은 부팅에서, 다시 읽은 카운트를 써 넣고 RTC 알람으로 한 번 더 freeze한 뒤의 통계다(Image-sec, 성공한 suspend 2회 — 예제 15와 이번):

```text
n5_rpm      	2		2		0		0		0		299		299		3908		0
9010000.pl031	2		2		1		0		0		2		1		4034		0
success: 2
fail: 0
failed_freeze: 0
failed_prepare: 0
failed_suspend: 0
last_failed_dev: 
last_failed_errno: 0
last_failed_step: 
```

---

## 7. PM QoS — "이만큼은 빨리 깨어나 달라"

### 7.1 정의

| 종류 | 인터페이스 | 무엇을 제한하나 |
|---|---|---|
| CPU latency QoS (전역) | 유저: `/dev/cpu_dma_latency` 열고 s32 쓰기 · 커널: `cpu_latency_qos_add_request()` | 모든 CPU의 cpuidle state exit latency 상한 |
| 장치 resume latency | 유저: `/sys/devices/.../power/pm_qos_resume_latency_us` · 커널: `dev_pm_qos_add_request()` | 그 장치(CPU 장치 포함)가 runtime suspend·깊은 idle에 들어가는 것 |
| 주파수 QoS | `freq_qos_add_request()` (thermal, 유저 `scaling_min/max_freq`) | cpufreq policy의 min/max |
| devfreq QoS | `dev_pm_qos` `DEV_PM_QOS_MIN/MAX_FREQUENCY`, sysfs `min_freq`/`max_freq` | devfreq 장치 주파수 범위 |

규칙: 요청은 **fd를 열고 있는 동안만** 유효하다(닫으면 자동 제거). 여러 요청이 있으면 가장 엄격한 값(최솟값)이 이긴다. 그래서 프로세스가 죽어도 제약이 남지 않는다.

### 7.2 예제 17 — `/dev/cpu_dma_latency`를 잡는 프로그램

무엇을 확인하는 코드인지: 지정한 지연(us)을 지정한 초 동안 잡는다. 예제 5의 QoS 세 줄이 이 프로그램이다. VM에서 `gcc -static -O2 -Wall -Wextra`로 빌드, 경고 0.

```c
// n5_qos: hold a CPU latency PM QoS request via /dev/cpu_dma_latency
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s <latency_us> <seconds>\n", argv[0]);
		return 1;
	}
	int32_t us = atoi(argv[1]);		/* binary s32, or ASCII hex */
	int fd = open("/dev/cpu_dma_latency", O_WRONLY);
	if (fd < 0) { perror("open"); exit(1); }
	if (write(fd, &us, sizeof(us)) != sizeof(us)) { perror("write"); exit(1); }
	printf("n5_qos: holding cpu_dma_latency=%d us for %s s\n", us, argv[2]);
	fflush(stdout);
	sleep(atoi(argv[2]));
	close(fd);				/* request is dropped on close */
	return 0;
}
```

결과는 예제 5의 마지막 세 줄과 그림 2다: 0 us와 100 us는 retention(exit latency 140 us)을 막아 WFI만 남기고, 200 us는 retention을 허용한다. menu governor 소스(`drivers/cpuidle/governors/menu.c`)는 `latency_req`보다 exit latency가 큰 상태를 후보에서 뺀다. 그리고 CPU 장치별 값은 `/sys/devices/system/cpu/cpu0/power/pm_qos_resume_latency_us`(예제 실행에서 `0` = 제약 없음)로 따로 걸 수 있다.

실무 메모: 오디오·센서 루프·CAN 제어 루프가 cpu_dma_latency를 잡는 것은 정당하지만, **잡은 채로 잊으면 장비 전체가 deep idle에 영영 못 들어간다**. 배터리 버그 조사 때 `lsof /dev/cpu_dma_latency`(누가 열고 있나)가 첫 줄에 들어가는 이유다. 실시간 튜닝에서 이것을 어떻게 쓰는지는 N2.

---

## 8. thermal ↔ cpufreq, 그리고 devfreq (GPU · DDR · NPU)

### 8.1 thermal이 주파수를 누르는 경로

I4에서 thermal zone · trip · cooling device · governor를 만들어 봤다. CPU에 `#cooling-cells = <2>`가 있고 cpufreq 드라이버가 `CPUFREQ_IS_COOLING_DEV` 플래그를 주거나(`cpufreq-dt`는 준다) 직접 `of_cpufreq_cooling_register()`를 부르면, 그 policy가 **cooling device**가 된다. cooling state k = "위에서 k번째 OPP까지만 허용"이다. thermal governor(step_wise, power_allocator)가 state를 올리면 cpufreq cooling이 **freq QoS의 max 요청**을 갱신하고, 그것이 예제 8의 `scaling_max_freq`를 누른 것과 같은 효과를 낸다. governor(schedutil)는 그 상한 안에서만 고른다.

이 랩의 가짜 드라이버는 cooling device로 등록하지 않았고, QEMU virt에는 thermal zone도 없다(`thermal: []`, 예제 4). 실보드에서 확인할 곳:

```sh
grep . /sys/class/thermal/cooling_device*/type     # cpufreq-cpu0, devfreq-... 가 보이면 연결됨
cat /sys/class/thermal/cooling_device0/cur_state /sys/class/thermal/cooling_device0/max_state
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq   # thermal이 누르면 내려간다
```

(가이드, 실행 안 함.)

### 8.2 devfreq — CPU 말고 나머지의 DVFS

devfreq는 "cpufreq의 일반화"다. GPU, NPU, DDR 컨트롤러(메모리 버스), 인터커넥트처럼 **자기 부하를 스스로 보고할 수 있는 장치**의 주파수를 바꾼다. AI 가속기 회사의 Linux 드라이버에서 가장 직접적으로 만나는 전원 프레임워크다.

| 부품 | 역할 |
|---|---|
| `struct devfreq_dev_profile` | 드라이버가 채운다: `target()`(주파수 바꾸기), `get_dev_status()`(busy_time/total_time 보고), `polling_ms`, `get_cur_freq()` |
| governor | `simple_ondemand`(busy 비율이 임계값 넘으면 올림), `performance`, `powersave`, `userspace`, `passive`(다른 devfreq/cpufreq를 따라감 — 예: CPU가 빠르면 DDR도 올림) |
| OPP 테이블 | cpufreq와 같은 DT `operating-points-v2` |
| devfreq-event | DDR 대역폭 카운터 같은 HW 모니터 (예: `rockchip-dfi`) |
| devfreq cooling | `devfreq_cooling_em_register()` — thermal이 GPU/NPU 주파수를 누를 수 있게 |

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 390">
<defs><marker id="ah8" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">devfreq — NPU · GPU · DDR의 DVFS 루프</text>
<rect x="20" y="40" width="190" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="64" font-size="13" text-anchor="middle">NPU 드라이버</text>
<text x="115" y="84" font-size="12" text-anchor="middle">busy / total 카운터</text>
<rect x="245" y="40" width="190" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="64" font-size="13" text-anchor="middle">devfreq 코어</text>
<text x="340" y="84" font-size="12" text-anchor="middle">polling_ms 마다 평가</text>
<rect x="470" y="40" width="190" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565" y="64" font-size="13" text-anchor="middle">governor</text>
<text x="565" y="84" font-size="12" text-anchor="middle">simple_ondemand</text>
<line x1="210" y1="70" x2="243" y2="70" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="435" y1="62" x2="468" y2="62" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="468" y1="82" x2="437" y2="82" stroke="currentColor" marker-end="url(#ah8)"/>
<text x="452" y="118" font-size="12" text-anchor="middle">목표 주파수</text>
<rect x="245" y="140" width="190" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="164" font-size="13" text-anchor="middle">OPP + QoS 범위</text>
<text x="340" y="184" font-size="12" text-anchor="middle">min_freq / max_freq</text>
<line x1="340" y1="100" x2="340" y2="138" stroke="currentColor" marker-end="url(#ah8)"/>
<rect x="245" y="240" width="190" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="264" font-size="13" text-anchor="middle">profile-&gt;target()</text>
<text x="340" y="284" font-size="12" text-anchor="middle">regulator + clk (H3)</text>
<line x1="340" y1="200" x2="340" y2="238" stroke="currentColor" marker-end="url(#ah8)"/>
<rect x="470" y="140" width="190" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="565" y="164" font-size="13" text-anchor="middle">thermal</text>
<text x="565" y="184" font-size="12" text-anchor="middle">devfreq cooling</text>
<line x1="468" y1="170" x2="437" y2="170" stroke="#d0564a" marker-end="url(#ah8)"/>
<rect x="20" y="140" width="190" height="60" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="115" y="164" font-size="13" text-anchor="middle">DDR devfreq</text>
<text x="115" y="184" font-size="12" text-anchor="middle">passive governor</text>
<line x1="210" y1="170" x2="243" y2="170" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah8)"/>
<text x="20" y="225" font-size="12">NPU가 빨라지면 DDR도</text>
<text x="20" y="241" font-size="12">따라 올린다 (부모 = NPU)</text>
<rect x="20" y="300" width="640" height="76" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="320" font-size="12" text-anchor="middle">runtime PM과 겹친다: 일이 없으면 runtime_suspend로 아예 끄고 (clock·power domain off),</text>
<text x="340" y="340" font-size="12" text-anchor="middle">일이 있으면 devfreq가 그 안에서 V·f를 고른다.</text>
<text x="340" y="360" font-size="12" text-anchor="middle">system suspend 중에는 devfreq_suspend_device()로 모니터링을 멈춘다.</text>
</svg>
```

그림 8 — devfreq의 제어 루프. 드라이버는 "얼마나 바빴나"만 보고하고, governor가 목표 주파수를, OPP·QoS·thermal이 허용 범위를 정한다. DDR처럼 스스로 부하를 알기 어려운 장치는 passive governor로 다른 장치를 따라간다.

DT와 드라이버 골격 (가이드, 실행 안 함 — QEMU virt에는 devfreq 장치가 없고 `/sys/class/devfreq`가 비어 있다, 예제 2):

```dts
	npu: npu@1e000000 {
		compatible = "vendor,example-npu";
		reg = <0x1e000000 0x100000>;
		clocks = <&cru CLK_NPU>;
		npu-supply = <&vdd_npu>;
		power-domains = <&pd NPU_PD>;		/* genpd: runtime PM이 끈다 */
		operating-points-v2 = <&npu_opp>;
		#cooling-cells = <2>;
	};
	npu_opp: opp-table-npu {
		compatible = "operating-points-v2";
		opp-300000000 { opp-hz = /bits/ 64 <300000000>; opp-microvolt = <700000>; };
		opp-800000000 { opp-hz = /bits/ 64 <800000000>; opp-microvolt = <850000>; };
	};
```

```c
static int npu_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct dev_pm_opp *opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);
	dev_pm_opp_put(opp);
	return dev_pm_opp_set_rate(dev, *freq);		/* V/f 순서를 OPP 코어가 지킨다 */
}
static int npu_status(struct device *dev, struct devfreq_dev_status *s)
{
	struct npu *n = dev_get_drvdata(dev);
	s->busy_time  = npu_read_busy_cycles(n);	/* HW 카운터, 가상의 함수 */
	s->total_time = npu_read_total_cycles(n);
	s->current_frequency = clk_get_rate(n->clk);
	return 0;
}
static struct devfreq_dev_profile npu_profile = {
	.polling_ms = 50, .target = npu_target, .get_dev_status = npu_status,
};
/* probe: devm_pm_opp_set_regulators(), devm_pm_opp_of_add_table(dev),
 *        devm_devfreq_add_device(dev, &npu_profile, DEVFREQ_GOV_SIMPLE_ONDEMAND, NULL) */
```

`/sys/class/devfreq/<dev>/`에서 `cur_freq`, `available_frequencies`, `governor`, `trans_stat`(cpufreq stats와 같은 전이 표), `load`(최근 busy 비율)를 볼 수 있다. Jetson Orin의 GPU(`17000000.gpu`)와 EMC(메모리)가 이 클래스로 보이고, Rockchip RK3588 NPU(`fdab0000.npu`)도 devfreq로 등록된다. 이 이름들은 커널·BSP 버전마다 다를 수 있으니 장비에서 `ls /sys/class/devfreq`로 확인한다.

---

## 9. 전력을 실제로 재기 — QEMU가 못 하는 부분

QEMU는 전력 모델이 없다. 지금까지의 실험은 "커널이 무엇을 하라고 시켰나"까지만 보여 준다. "그래서 몇 mW 줄었나"는 실보드에서 재야 한다. Don의 scope·로직 분석기 경험이 가장 직접적으로 쓰이는 곳이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 360">
<defs><marker id="ah9" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">전력 측정 벤치 — 세 가지 방법을 함께 쓴다</text>
<rect x="20" y="50" width="130" height="60" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="85" y="76" font-size="13" text-anchor="middle">배터리 / PSU</text>
<text x="85" y="94" font-size="12" text-anchor="middle">예: 12 V</text>
<rect x="190" y="50" width="150" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="265" y="76" font-size="13" text-anchor="middle">Joulescope / Otii</text>
<text x="265" y="94" font-size="12" text-anchor="middle">nA~A, 수 MHz 샘플</text>
<rect x="380" y="40" width="280" height="200" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="520" y="60" font-size="13" text-anchor="middle">보드 (SoC + PMIC)</text>
<rect x="395" y="75" width="120" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="455" y="100" font-size="12" text-anchor="middle">shunt 10 mΩ</text>
<rect x="525" y="75" width="120" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="585" y="100" font-size="12" text-anchor="middle">VDD_CPU 레일</text>
<rect x="395" y="130" width="120" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="455" y="155" font-size="12" text-anchor="middle">INA226/3221</text>
<rect x="525" y="130" width="120" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="585" y="155" font-size="12" text-anchor="middle">SoC (Linux)</text>
<line x1="515" y1="150" x2="523" y2="150" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="520" y="188" font-size="12" text-anchor="middle">I2C → hwmon (I4)</text>
<rect x="525" y="196" width="120" height="34" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="585" y="218" font-size="12" text-anchor="middle">GPIO 마커</text>
<line x1="150" y1="80" x2="188" y2="80" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="340" y1="80" x2="378" y2="80" stroke="currentColor" marker-end="url(#ah9)"/>
<rect x="190" y="270" width="150" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="265" y="296" font-size="13" text-anchor="middle">scope / LA</text>
<text x="265" y="314" font-size="12" text-anchor="middle">전류 파형 + 마커</text>
<line x1="585" y1="230" x2="342" y2="300" stroke="#d0564a" marker-end="url(#ah9)"/>
<line x1="265" y1="110" x2="265" y2="268" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah9)"/>
<text x="20" y="150" font-size="12">1) 인라인 계측기: 보드 전체,</text>
<text x="20" y="166" font-size="12">suspend 전류(uA)까지</text>
<text x="20" y="196" font-size="12">2) 온보드 INA: 레일별,</text>
<text x="20" y="212" font-size="12">Linux에서 직접 읽음</text>
<text x="20" y="242" font-size="12">3) GPIO 마커로 커널 이벤트와</text>
<text x="20" y="258" font-size="12">전류 파형을 정렬 (M11)</text>
</svg>
```

그림 9 — 측정 벤치. 인라인 계측기는 보드 전체(특히 suspend 중 uA 단위)를, 온보드 전류 센서는 레일별 값을 Linux 안에서, GPIO 마커는 "이 전류 스파이크가 어느 커널 이벤트냐"를 맞춰 준다.

| 도구 | 무엇 | Linux에서 보는 곳 | 주의 |
|---|---|---|---|
| INA226 / INA219 (shunt + ADC) | 레일 하나의 V, I, P | `ina2xx` hwmon: `/sys/class/hwmon/hwmonN/curr1_input`(mA), `power1_input`(uW) | DT `shunt-resistor`(uΩ) 값이 틀리면 전부 틀림. 샘플 주기·평균 설정 |
| INA3221 (3채널) | 레일 셋 | `ina3221` hwmon: `in1_input`, `curr1_input`... | Jetson 개발 키트가 이것으로 VDD_IN·CPU_GPU_CV·SOC 레일을 보고한다 |
| `tegrastats` (Jetson) | CPU/GPU 부하·주파수·온도·레일 전력 요약 | 위 hwmon과 devfreq를 읽는 NVIDIA 도구 | 출력 형식이 JetPack 버전마다 다름 |
| Joulescope, Otii Arc, Nordic PPK2 | 인라인 고속 전류계 | 장비 밖 (PC 소프트웨어) | suspend 전류 같은 uA 측정은 이쪽만 정확 |
| `powertop` | wakeup 원인 순위, 장치별 runtime PM 상태, 튜닝 제안 | 커널 tracepoint + sysfs | x86 중심 기능이 많다. ARM에서는 "Idle stats"·"Device stats"·"Tunables"가 유용 |
| power_supply 클래스 | 배터리 fuel gauge의 전압·전류·잔량 | `/sys/class/power_supply/battery/` (I4 §10) | gauge의 `current_now`는 평균 창이 길다 |

실무 절차(밤새 배터리가 닳는 문제의 예):

1. 인라인 계측기로 suspend 중 전류를 잰다. 기대값(데이터시트의 DDR self-refresh + always-on 도메인)보다 크면 어떤 레일이 안 꺼졌는지 레일별 측정으로 좁힌다.
2. 전류 파형에 주기적인 스파이크가 있으면 → suspend가 깨졌다 잤다 반복(wakeup storm, §6.5). `suspend_stats`, `wakeup_sources`.
3. 평평하게 높으면 → 꺼졌어야 할 장치·레일이 켜져 있다. resume 직후 `/sys/kernel/debug/clk/clk_summary`의 `enable_count`, `/sys/kernel/debug/regulator/regulator_summary`, `pm_genpd_summary`를 suspend 직전 값과 비교한다. 외부 칩(센서, PHY, 카메라 센서)은 Linux가 모르는 전력을 먹으니 드라이버 suspend 콜백이 칩을 sleep 명령으로 보냈는지 버스 트레이스로 확인한다.

---

## 10. 커널 안에서는 무슨 일이 일어나나

```text
echo freeze > /sys/power/state
  state_store()                         kernel/power/main.c
   pm_suspend(PM_SUSPEND_TO_IDLE)       kernel/power/suspend.c
    enter_state()
     suspend_prepare()  → pm_notifier(PM_SUSPEND_PREPARE) → suspend_freeze_processes()
     suspend_devices_and_enter()
      pm_suspend_target_state = state
      dpm_suspend_start()  → dpm_prepare() + dpm_suspend()      drivers/base/power/main.c
      suspend_enter()
       dpm_suspend_late() → dpm_suspend_noirq() (suspend_device_irqs())
       s2idle_loop() → s2idle_enter() → wake_up_all_idle_cpus()
          각 CPU: do_idle() → cpuidle_enter_s2idle() → 가장 깊은 state + tick_freeze()
          wake IRQ → pm_system_irq_wakeup() → s2idle_wake()
       (deep이면) pm_sleep_disable_secondary_cpus() → syscore_suspend()
                  → suspend_ops->enter() = psci_system_suspend_enter()
                  → cpu_suspend(0, psci_system_suspend) → SMC SYSTEM_SUSPEND
      dpm_resume_noirq → dpm_resume_early → dpm_resume_end
     suspend_finish() → thaw_processes() → pm_notifier(PM_POST_SUSPEND)

open("/dev/n5_rpm")
  misc_open → n5_open → pm_runtime_resume_and_get()
    __pm_runtime_resume(dev, RPM_GET_PUT) → rpm_resume()        drivers/base/power/runtime.c
      parent 먼저 rpm_resume() → callback = RPM_GET_CALLBACK(dev, runtime_resume)
      (genpd 도메인이면 genpd_runtime_resume()가 도메인 전원을 먼저 켬)

idle CPU
  do_idle() → cpuidle_idle_call()                               kernel/sched/idle.c
    cpuidle_select() → menu_select()   (cpu_latency_qos_limit(), next timer)
    cpuidle_enter() → psci_enter_idle_state() → psci_cpu_suspend_enter()
       retention: invoke_psci_fn(CPU_SUSPEND)   power-down: cpu_suspend() + 컨텍스트 저장
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<defs><marker id="ah10" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">드라이버가 PM 프레임워크와 만나는 네 지점</text>
<rect x="250" y="120" width="180" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="146" font-size="13" text-anchor="middle">struct device</text>
<text x="340" y="164" font-size="12" text-anchor="middle">dev-&gt;power, dev-&gt;pm_domain</text>
<rect x="20" y="40" width="200" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="120" y="64" font-size="13" text-anchor="middle">runtime PM</text>
<text x="120" y="84" font-size="12" text-anchor="middle">get/put, runtime_* 콜백</text>
<rect x="460" y="40" width="200" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="560" y="64" font-size="13" text-anchor="middle">system sleep</text>
<text x="560" y="84" font-size="12" text-anchor="middle">dev_pm_ops (N6)</text>
<rect x="20" y="200" width="200" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="120" y="224" font-size="13" text-anchor="middle">wakeup</text>
<text x="120" y="244" font-size="12" text-anchor="middle">wakeup source, wake IRQ</text>
<rect x="460" y="200" width="200" height="60" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="560" y="224" font-size="13" text-anchor="middle">QoS · OPP · devfreq</text>
<text x="560" y="244" font-size="12" text-anchor="middle">dev_pm_qos, dev_pm_opp</text>
<line x1="220" y1="80" x2="248" y2="130" stroke="currentColor" marker-end="url(#ah10)"/>
<line x1="460" y1="80" x2="432" y2="130" stroke="currentColor" marker-end="url(#ah10)"/>
<line x1="220" y1="220" x2="248" y2="172" stroke="currentColor" marker-end="url(#ah10)"/>
<line x1="460" y1="220" x2="432" y2="172" stroke="currentColor" marker-end="url(#ah10)"/>
<text x="340" y="290" font-size="12" text-anchor="middle">모두 struct dev_pm_info (dev-&gt;power) 한 구조체 안의 필드로 이어진다</text>
</svg>
```

그림 10 — 드라이버 관점의 네 접점. `struct dev_pm_info`(`include/linux/pm.h`)에 runtime 상태(`runtime_status`, `usage_count`, `child_count`, `autosuspend_delay`), wakeup(`wakeup`, `wakeirq`), QoS(`qos`)가 한데 들어 있다. `dev->pm_domain`(genpd)이 있으면 버스·클래스·드라이버 콜백보다 **먼저** 선택된다.

콜백 선택 우선순위(`drivers/base/power/main.c`의 `device_suspend()`·`device_suspend_late()`·`device_suspend_noirq()` 안의 if-else 사슬, runtime은 `rpm_callback()`의 `RPM_GET_CALLBACK`): **pm_domain → type → class → bus → driver**. 예제 13에서 `n5_rpm`의 콜백 이름이 `platform_pm_suspend`(bus)로 찍히고 그 안에서 드라이버의 `pm_runtime_force_suspend`가 불린 것이 이 구조다. 콜백 작성 규칙(어느 단계에서 무엇을 하나, `noirq`에서 IRQ를 어떻게 다루나, 실패 시 되돌리기)은 N6에서 이어진다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트 — IMU와 카메라를 쓰지 않을 때 끄기

SPI IMU 드라이버(H7)는 `read()`·버퍼 활성화 때 `pm_runtime_resume_and_get()`, 끝나면 `pm_runtime_put_autosuspend()`를 부르고 delay를 샘플 주기보다 길게(예: 2초) 둔다. 그래야 100 Hz 읽기마다 껐다 켜지 않는다. IIO 프레임워크(I2)를 쓰면 `buffer_postenable`/`predisable` 훅이 자리다. 카메라 센서(V4L2, I5)는 스트리밍 시작·종료가 자연스러운 get/put 지점이다. MIPI CSI 수신기·ISP가 같은 power domain에 있으면 genpd가 전부 suspended일 때만 도메인을 내린다.

### 11.2 드론 — suspend 대신 "깊은 idle + 주변장치 off"

비행 중에는 suspend를 쓸 수 없다. 대신 비행 제어 MCU(N4)가 실시간 루프를 맡고, Linux 쪽은 cpuidle의 깊은 상태와 장치별 runtime PM으로 전력을 줄인다. 착륙 후 대기 모드에서만 s2idle/deep을 쓰고, 깨우는 것은 MCU가 GPIO로 한다(DT `wakeup-source` 속성 + gpio-keys 또는 전용 드라이버의 `enable_irq_wake`).

### 11.3 AI 가속기 SoC — NPU의 세 겹 전원 관리

추론 요청이 없으면 runtime PM으로 NPU power domain을 끈다. 요청이 오면 resume(펌웨어 재로드가 필요하면 수 ms~수십 ms — G10). 일하는 동안에는 devfreq가 NPU 부하로 V/f를 고르고 passive governor로 DDR 대역폭을 맞춘다. 온도가 오르면 devfreq cooling이 상한을 누른다. 면접에서 "NPU가 쉬는데도 전류가 안 내려간다"를 받으면 이 세 겹을 위에서부터 확인한다: runtime_status가 suspended인가 → genpd 도메인이 off인가 → 클럭 enable_count가 0인가.

### 11.4 bring-up 체크리스트 — "suspend가 안 된다"

```sh
cat /sys/power/state /sys/power/mem_sleep          # deep이 보이나 (PSCI SYSTEM_SUSPEND)
echo 1 > /sys/power/pm_print_times; echo 1 > /sys/power/pm_debug_messages
for t in freezer devices platform processors core; do
  echo $t > /sys/power/pm_test; echo mem > /sys/power/state || echo "FAIL at $t"; done
echo none > /sys/power/pm_test
cat /sys/power/suspend_stats/last_failed_dev /sys/power/suspend_stats/last_failed_step
```

(가이드 — deep이 없는 이 랩에서는 `mem`이 s2idle로 가고 processors·core가 거부된다, 예제 14.) 커널 인자 `no_console_suspend`(콘솔 유지), `initcall_debug`(부팅·resume 콜백 시간, M10), `pm_debug_messages`(커널 인자로도 된다)가 도구 상자다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `pm_runtime_get_sync()` 실패 후 put 안 함 | 장치가 영영 suspend 안 됨, 전류 높음 | 실패해도 usage가 올라간 채 남는다 | `pm_runtime_resume_and_get()` 사용 |
| probe에서 HW를 켜 두고 `set_active` 생략 | 첫 get에서 resume 콜백이 또 불려 clock enable 카운트 꼬임 | 코어는 장치가 suspended라고 믿음 | enable 전에 `pm_runtime_set_active()` |
| suspend 콜백이 `-EBUSY` 등 에러 리턴 | `PM: Some devices failed to suspend`, suspend 즉시 복귀 | 진행 중 전송, 버그 | `last_failed_dev` 확인, 전송 완료를 기다리거나 runtime PM으로 이미 꺼 두기 (N6) |
| wakeup source를 잡고 놓지 않음 | Android류 시스템에서 suspend 안 됨, `prevent_suspend_time` 증가 | 에러 경로에서 `pm_relax` 누락 | `pm_wakeup_event(dev, ms)` 같은 시간 제한형 사용, 경로 점검 |
| wake IRQ가 레벨인데 원인 해제 안 함 | wakeup storm — 자자마자 깸 | resume에서 인터럽트 원인 clear 누락 | `pm_wakeup_irq`·`suspend_stats`로 확인, 드라이버 수정 |
| 외부 칩을 sleep시키지 않음 | suspend 전류가 mA 단위로 높음 | SoC는 꺼졌지만 센서·PHY가 깨어 있음 | suspend 콜백에서 칩 sleep 명령, 레일 off, 버스 트레이스로 확인 |
| cpu_dma_latency를 열고 잊음 | deep idle 0%, 배터리 수명 반토막 | 오디오·RT 앱이 fd를 계속 들고 있음 | `lsof /dev/cpu_dma_latency`, 필요할 때만 잡기 |
| dmesg 시간으로 suspend 길이 판단 | "0.7초만 잤다"는 오판 | `local_clock`은 s2idle 동안 멈춤 | `Timekeeping suspended for` 줄, `trace_clock=boot` |
| resume 후 장치 초기화 누락 | resume 뒤 IMU 무응답, DMA 에러 | 전원이 꺼져 레지스터가 리셋됐는데 복원 안 함 | resume 콜백에서 재초기화, `regcache_sync()`(H2) |
| idle state에 `local-timer-stop`인데 broadcast timer 없음 | 그 state의 usage가 0 | 깨울 타이머가 없다 | SoC 타이머 노드 활성화 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "What's the difference between runtime PM and system suspend?"

**A.** runtime PM은 시스템이 깨어 있는 동안 장치 하나가 놀면 그 장치만 끄는 것이고, 드라이버의 usage counter(get/put)와 autosuspend 타이머가 결정한다. system suspend는 유저가 요청해서 모든 태스크를 얼리고 모든 장치를 정해진 단계 순서로 suspend한 뒤 CPU까지 재우는 것이다. 둘은 콜백을 공유할 수 있고(`pm_runtime_force_suspend`), 이미 runtime suspended인 장치는 system suspend에서 건너뛸 수도 있다.

> Runtime PM is per-device and happens while the system is running: the driver brackets hardware access with pm_runtime_get and put, and when the usage count drops to zero and the autosuspend delay expires, the core calls runtime_suspend so the driver can gate clocks and let the power domain turn off. System suspend is global and user-initiated: tasks are frozen, every device goes through prepare, suspend, suspend_late and suspend_noirq in dependency order, then the CPUs go to the deepest state — s2idle or PSCI SYSTEM_SUSPEND on ARM. A common pattern is to reuse the runtime callbacks for system sleep with pm_runtime_force_suspend.

**꼬리 질문**: "Why autosuspend instead of suspending immediately?" → 껐다 켜는 비용(시간·에너지, 펌웨어 재로드)이 짧은 idle 동안 아끼는 것보다 크기 때문. delay는 접근 패턴보다 길게.

**Q.** "A device drains its battery overnight while suspended. How do you debug it?"

**A.** 먼저 정말 잠들어 있었는지와 얼마나 자주 깼는지를 가른다. `suspend_stats`와 `wakeup_sources`, `pm_wakeup_irq`로 wakeup storm을 보고, 인라인 전류계로 suspend 전류 파형을 본다. 스파이크가 주기적이면 깨우는 원인을 찾고, 평평하게 높으면 레일별 측정과 clk/regulator/genpd summary로 안 꺼진 것을 찾는다. 마지막으로 외부 칩이 sleep 명령을 받았는지 버스 트레이스로 확인한다.

> I'd split it into "is it waking up" versus "is it sleeping shallow". First check /sys/power/suspend_stats, the wakeup_sources counters in debugfs and pm_wakeup_irq to see whether something keeps waking the system — a level-triggered wake IRQ that isn't cleared, or an RTC alarm. Then put an inline meter like a Joulescope on the battery: periodic spikes mean wakeups, a flat but high floor means something stays powered. For the floor, compare the clock, regulator and genpd summaries before suspend, measure per rail, and check that external chips — sensors, PHYs, camera modules — were actually put into their low-power mode, which I'd confirm on the bus with a logic analyzer. Also check mem_sleep: if it's s2idle instead of deep, the floor is expected to be higher.

**Q.** "Who decides the CPU frequency in Linux?"

**A.** cpufreq governor다. 지금 기본인 schedutil은 스케줄러가 계산하는 CPU 사용률로 바로 다음 주파수를 정하고, cpufreq 드라이버(대개 cpufreq-dt나 SCMI)가 OPP 테이블에 따라 regulator와 clock을 바꾼다. thermal과 유저는 freq QoS로 상한·하한을 누른다. big.LITTLE에서는 EAS가 에너지 모델로 태스크를 어느 클러스터에 둘지도 정한다.

> The cpufreq governor, and on modern kernels that's schedutil: it takes the scheduler's PELT utilization for the policy and maps it to a frequency, so decisions happen on scheduler events rather than on a sampling timer like ondemand. The driver — usually cpufreq-dt with an OPP table, or SCMI where firmware does the DVFS — applies the voltage-then-frequency sequence. Thermal cooling devices and user space clamp the range through frequency QoS, and on heterogeneous systems Energy Aware Scheduling uses the energy model to place tasks on the cluster that minimizes total energy.

**Q.** "How does the kernel choose an idle state, and how can an application prevent deep idle?"

**A.** idle 루프가 cpuidle governor(menu나 teo)에게 묻는다. governor는 다음 타이머까지의 시간과 최근 패턴으로 idle 길이를 예측하고, min-residency가 그보다 짧고 exit latency가 PM QoS 한계 안인 상태 중 가장 깊은 것을 고른다. 앱은 `/dev/cpu_dma_latency`를 열어 지연 상한을 쓰면 되고, fd를 닫으면 제약이 사라진다.

> When a CPU has nothing to run, the idle loop asks the cpuidle governor — menu or teo — to predict how long it will stay idle, from the next timer event and recent history. It picks the deepest state whose target residency fits that prediction and whose exit latency is within the current CPU latency QoS limit; the driver, cpuidle-psci on ARM, then calls PSCI CPU_SUSPEND with the power_state from the device tree. An application can cap the exit latency by opening /dev/cpu_dma_latency and writing a value; the request lives as long as the file descriptor is open.

**Q.** "Walk me through the suspend sequence."

**A.** 파일시스템 sync, 유저·커널 스레드 동결, 그다음 장치 콜백 네 단계(prepare, suspend, suspend_late, suspend_noirq — 자식부터), s2idle이면 CPU를 idle로 재우고, deep이면 non-boot CPU를 끄고 syscore 후 PSCI SYSTEM_SUSPEND. resume은 정확히 역순이다. 각 단계 시간은 `pm_print_times`와 `suspend_resume` tracepoint로 잴 수 있다.

> Sync, freeze user space and freezable kernel threads, then four device phases — prepare, suspend, suspend_late with runtime PM disabled, and suspend_noirq with device interrupts off — always children before parents. For suspend-to-idle the CPUs then just enter their deepest idle state with timekeeping frozen; for deep, non-boot CPUs are taken offline, syscore ops save the interrupt controller and timers, and the last CPU calls PSCI SYSTEM_SUSPEND. Resume runs the mirror image. On a QEMU lab I traced it with the suspend_resume tracepoint on the boot clock and the device suspend phase dominated, with one PCI device taking about 90 ms.

**꼬리 질문**: "Why is there a noirq phase?" → IRQ 핸들러가 이미 suspended된 장치를 건드리지 못하게 장치 IRQ를 끈 뒤에 마지막 저장을 하는 단계. 반대로 resume에서는 IRQ를 켜기 전에 장치를 먼저 살린다.

**Q.** "What is a wakeup source, and does holding one block suspend?"

**A.** 처리 중인 깨움 이벤트를 표시하는 커널 객체다. 잡혀 있으면 `wakeup_count` read가 블록하고, 데몬이 그 값을 써서 감시를 켠 뒤에는 suspend가 -EBUSY로 중단된다. 하지만 감시 없이 바로 `state`에 쓰면 잡혀 있어도 suspend된다 — 실험으로 확인했다.

> A wakeup source marks a wakeup event that is still being processed. It only blocks suspend when the suspend path checks for it: if user space used the wakeup_count protocol — read the count, which blocks while sources are active, then write it back — the kernel aborts with EBUSY if any source becomes active. A script that just writes mem to /sys/power/state bypasses that check, so the source alone doesn't stop it, which surprises people. A wake-enabled interrupt arriving during suspend aborts it regardless.

**Q.** "What is devfreq and where would you use it on an AI accelerator SoC?"

**A.** CPU 외 장치(GPU, NPU, DDR)의 DVFS 프레임워크다. 드라이버는 busy/total 시간을 보고하고 target으로 OPP를 적용하며, governor가 주파수를 고른다. NPU에는 simple_ondemand, DDR은 passive로 NPU를 따라가게, thermal은 devfreq cooling으로 상한을 누른다. 일이 없을 때는 runtime PM으로 아예 끈다.

> Devfreq is the generic DVFS framework for non-CPU devices. The driver supplies get_dev_status with busy and total time from hardware counters and a target callback that applies an OPP; a governor like simple_ondemand picks the frequency, and the passive governor lets the DDR or interconnect follow the NPU. Thermal can register it as a cooling device. It sits inside runtime PM: when there are no inference jobs the NPU is runtime-suspended and its power domain is off; devfreq only decides V and f while it is active.

---

## 14. 직접 해보기

1. (개념) 같은 장비에서 s2idle과 deep의 suspend 전류가 각각 무엇으로 정해지는지, 그리고 resume 지연이 왜 다른지 세 줄로 써라. 정답/힌트: s2idle은 SoC 전원이 그대로 켜진 채 CPU만 깊은 idle, deep은 DDR self-refresh + always-on만 남음, deep은 펌웨어 경유 재시작 + CPU online 비용.
2. (코드) `n5_rpm`에 `autosuspend_delay_ms`를 module param으로 받게 하고, delay 0·500·2000에서 open/close를 10번 반복할 때 `runtime_suspended_time`과 dmesg의 콜백 횟수를 비교하라. 정답/힌트: delay 0이면 close마다 콜백 한 쌍이 생긴다. `-1`을 쓰면 autosuspend가 사실상 꺼진다.
3. (관찰) 예제 3의 DT에 exit-latency 1000 us짜리 두 번째 retention state를 추가하고, `/n5_qos 500 3`에서 state별 time 차이를 보라. 정답/힌트: 500 us 한계면 140 us state는 허용, 1000 us state는 금지된다.
4. (관찰) 예제 13을 `pm_async`를 0으로 바꿔 다시 찍고 `dpm_suspend` 단계 시간과 콜백 스레드(T번호)를 비교하라. 정답/힌트: `echo 0 > /sys/power/pm_async` — 모든 콜백이 한 스레드(셸)에서 순서대로 돈다.
5. (코드) `n5_rpm`의 `runtime_suspend`가 `-EBUSY`를 돌려주게 바꾸고 rpm tracepoint에서 상태가 어떻게 되돌아가는지 보라. 정답/힌트: `rpm_return_int ... ret=-16` 뒤 `RPM_ACTIVE`, autosuspend가 다시 예약된다.
6. (관찰) 가짜 cpufreq 드라이버에서 governor를 `userspace`로 두고 `scaling_setspeed`를 바꾸며 `power:cpu_frequency` 이벤트를 찍어라. 그다음 `scaling_max_freq`를 800000으로 두고 `performance`로 바꿔 cur가 얼마인지 확인하라. 정답/힌트: 상한이 governor보다 우선 — 800000.
7. (설계) 1 kHz IMU를 읽는 센서 키트에서 deep idle 비율을 높이는 방법 세 가지를 써라. 정답/힌트: IMU FIFO + watermark IRQ로 깨우는 빈도 줄이기, 처리 스레드를 한 코어에 모으기(나머지는 깊게), 필요할 때만 PM QoS.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| s2idle (freeze) | suspend-to-idle | 장치 suspend + CPU를 가장 깊은 idle로, 펌웨어 지원 불필요 |
| deep (mem) | suspend-to-RAM | DDR self-refresh, PSCI `SYSTEM_SUSPEND` |
| hibernate (disk) | suspend-to-disk | 메모리 이미지를 swap에 저장 후 전원 off |
| C-state / idle state | 코어 잠 상태 | WFI, retention, power-down, cluster off |
| P-state / OPP | 동작점 | (주파수, 전압) 쌍, DT `operating-points-v2` |
| cpuidle governor | idle 상태 선택기 | menu, teo, ladder, haltpoll |
| cpufreq governor | 주파수 선택기 | schedutil, performance, powersave, ondemand, userspace |
| EAS | Energy Aware Scheduling | 에너지 모델로 태스크 배치를 고르는 스케줄러 기능 |
| Energy Model (EM) | 에너지 모델 | OPP별 전력 비용 표 |
| runtime PM | 장치별 동적 전원 관리 | usage counter + autosuspend |
| autosuspend | 지연 suspend | usage 0 뒤 delay가 지나야 suspend |
| genpd | generic power domain | 여러 장치가 공유하는 전원 섬을 관리 |
| `dev_pm_ops` | PM 콜백 표 | system sleep 단계별 + runtime 콜백 (N6) |
| noirq 단계 | 장치 IRQ를 끈 뒤의 단계 | 마지막 저장, 처음 복원 |
| syscore | 코어 장치 PM | GIC·타이머 같은 장치 모델 밖의 저장/복원 (deep 전용) |
| wakeup source | 깨움 이벤트 표시 | `pm_stay_awake`/`pm_relax`, debugfs 통계 |
| wake IRQ | suspend 중 살려 둔 IRQ | `enable_irq_wake`, `dev_pm_set_wake_irq` |
| wakeup_count | 경쟁 없는 suspend 프로토콜 | 읽은 값을 다시 써야 잘 수 있다 |
| `pm_test` | suspend 부분 시험 | freezer, devices, platform, processors, core |
| PM QoS | 성능 제약 | cpu latency, device resume latency, freq QoS |
| devfreq | 장치 DVFS | GPU·NPU·DDR 주파수 |
| cooling device | thermal의 액추에이터 | cpufreq·devfreq·팬 (I4) |
| PSCI | 전원 펌웨어 인터페이스 | `CPU_SUSPEND`, `SYSTEM_SUSPEND` (C5) |

---

## 16. 요약 & 체크리스트

Linux 전원 관리는 네 층이다. 시스템 sleep(유저가 고르는 s2idle·deep·hibernate), CPU idle(cpuidle governor가 고르는 C-state, ARM에서는 DT `idle-states` + PSCI `CPU_SUSPEND`), CPU 성능(schedutil이 고르는 OPP, cpufreq-dt가 V/f 순서를 지켜 적용, EAS가 배치), 장치(드라이버의 get/put이 움직이는 runtime PM과 genpd, 일하는 동안은 devfreq). PM QoS와 thermal은 이들 위에 "이보다 깊게 자지 말라", "이보다 빠르게 돌지 말라"는 제약을 얹는다. QEMU 실험으로 s2idle 한 번이 진입 508 ms · 잠듦 2.5 s · 복귀 166 ms로 나뉘고 장치 suspend 단계가 가장 길다는 것, wakeup source는 wakeup_count 프로토콜과 함께일 때만 suspend를 막는다는 것, PM QoS 100 us가 140 us짜리 idle state를 실제로 금지한다는 것을 확인했다. 전력 숫자 자체는 QEMU가 줄 수 없으니 실보드에서 인라인 계측기·INA 레일 센서·GPIO 마커로 잰다.

- [ ] 시스템 sleep · C-state · P-state · runtime PM · devfreq를 한 장 그림으로 설명할 수 있다
- [ ] `mem_sleep`을 보고 그 보드의 `mem`이 s2idle인지 deep인지, 그 이유(PSCI SYSTEM_SUSPEND)를 말할 수 있다
- [ ] suspend 단계 순서와 noirq·syscore 단계의 존재 이유를 설명할 수 있다
- [ ] `pm_print_times`·`suspend_resume` tracepoint·`trace_clock=boot`로 단계별 시간을 잴 수 있다
- [ ] `pm_test` 레벨로 suspend 실패 지점을 이분 탐색할 수 있다
- [ ] DT `idle-states`를 읽고 latency·residency가 governor 선택에 어떻게 쓰이는지 말할 수 있다
- [ ] cpufreq policy·governor·stats를 sysfs로 읽고 `scaling_max_freq`가 상한이라는 것을 설명할 수 있다
- [ ] `pm_runtime_resume_and_get`/`put_autosuspend`로 autosuspend 드라이버를 짤 수 있다
- [ ] runtime PM 상태 기계와 콜백이 어느 문맥(동기·kworker·irq_safe)에서 불리는지 말할 수 있다
- [ ] wakeup source가 suspend를 막는 조건(wakeup_count 프로토콜)과 막지 못하는 경우를 실험으로 설명할 수 있다
- [ ] `/dev/cpu_dma_latency`로 deep idle을 막고 그 부작용을 설명할 수 있다
- [ ] "밤새 배터리가 닳는다"를 wakeup 문제와 바닥 전류 문제로 나눠 디버그 절차를 말할 수 있다

## 참고 자료

- docs.kernel.org — Power Management: [System Sleep States](https://docs.kernel.org/admin-guide/pm/sleep-states.html), [Runtime Power Management Framework for I/O Devices](https://docs.kernel.org/power/runtime_pm.html), [Device Power Management Basics](https://docs.kernel.org/driver-api/pm/devices.html), [CPU Idle Time Management](https://docs.kernel.org/admin-guide/pm/cpuidle.html), [CPU Performance Scaling](https://docs.kernel.org/admin-guide/pm/cpufreq.html), [Energy Aware Scheduling](https://docs.kernel.org/scheduler/sched-energy.html), [PM Quality Of Service Interface](https://docs.kernel.org/power/pm_qos_interface.html), [Debugging hibernation and suspend](https://docs.kernel.org/power/basic-pm-debugging.html)
- DT 바인딩: `Documentation/devicetree/bindings/cpu/idle-states.yaml`, `Documentation/devicetree/bindings/opp/opp-v2.yaml`, `Documentation/devicetree/bindings/power/power-domain.yaml`
- 커널 소스 (6.18.55, elixir.bootlin.com): `kernel/power/suspend.c`, `kernel/power/main.c`, `drivers/base/power/main.c`, `drivers/base/power/runtime.c`, `drivers/base/power/wakeup.c`, `drivers/cpuidle/cpuidle-psci.c`, `drivers/cpuidle/governors/menu.c`, `drivers/cpufreq/cpufreq-dt.c`, `kernel/sched/cpufreq_schedutil.c`, `drivers/devfreq/devfreq.c`, `include/trace/events/power.h`, `include/trace/events/rpm.h`
- Arm, "Arm Power State Coordination Interface Platform Design Document" (DEN0022)
- Bootlin, "Embedded Linux kernel and driver development" 교육 자료의 power management 장
