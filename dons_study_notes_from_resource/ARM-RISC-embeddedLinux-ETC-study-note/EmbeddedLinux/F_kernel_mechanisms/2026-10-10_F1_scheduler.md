# F1. 프로세스·스케줄러 (커널 관점) — task_struct, EEVDF, 스케줄링 클래스, preemption

> **이 노트를 다 읽으면**: `task_struct`에서 스케줄러가 보는 필드(state, prio 3종, `se`/`rt`/`dl`, `mm`, `stack`)를 모듈로 직접 찍어 설명할 수 있다 · CPU별 run queue와 클래스 순서(stop > dl > rt > fair > idle), CFS에서 EEVDF로 바뀐 점(lag·eligibility·virtual deadline·slice)을 그림으로 설명할 수 있다 · nice→weight, SCHED_FIFO/RR, RT throttling(6.12+의 fair_server), SCHED_DEADLINE을 실측 숫자로 말할 수 있다 · preemption 모델과 "schedule()이 언제 불리나", context switch 경로를 ftrace로 보여 줄 수 있다
> **왜 필요한가**: "How does the Linux scheduler work?", "SCHED_FIFO 스레드가 CPU를 다 먹으면?", "센서 루프 지터를 어떻게 줄이나?", "priority inversion"은 로보틱스·드론 Embedded Linux 면접의 단골이고, 실무에서는 "RT 스레드를 줬는데 왜 가끔 늦지?"라는 버그로 돌아온다
> **Don 기준 난이도**: 고정 우선순위 선점형 스케줄러·ready list·문맥 전환(PendSV)·우선순위 역전은 RTOS로 이미 안다 / 새로 배울 부분은 "fair 클래스(EEVDF)가 weight 비율로 CPU를 나누는 방식", "RT 위에 또 있는 deadline 클래스와 fair_server", "커널 코드 자체가 선점되는지(preemption 모델)", SMP의 CPU별 run queue
> **선행 노트**: A4(프로세스·스레드), E1(커널 아키텍처), E3(커널 모듈), E4(커널 제약 — atomic context) — F2(실행 컨텍스트)·F3(잠자기와 깨우기)·F4(락)·N1~N3(실시간·지연 측정)으로 이어진다

---

## 0. 큰 그림 — RTOS 스케줄러를 알면 절반은 안다

Don이 SSD 펌웨어에서 쓰던 RTOS 스케줄러를 떠올려 보자. 태스크마다 고정 우선순위가 있고, ready 상태인 태스크 중 **가장 높은 우선순위**가 돈다. 같은 우선순위끼리는 tick마다 돌아가며(round-robin) 돈다. ISR에서 높은 우선순위 태스크를 깨우면 ISR이 끝나는 순간(Cortex-M이라면 PendSV에서) 문맥 전환이 일어난다. 이게 전부다.

Linux의 **RT 클래스(SCHED_FIFO/SCHED_RR)** 는 정확히 이 모델이다. 다른 점은 세 가지다.

1. **RT 아래에 "fair" 클래스가 있다.** 대부분의 프로세스(셸, 데몬, 내 앱)는 RT가 아니라 `SCHED_OTHER`(커널 이름 `SCHED_NORMAL`)다. 이들은 우선순위로 줄 세우지 않고 **weight 비율로 CPU 시간을 나눠 갖는다**. 6.6부터 이 알고리즘이 CFS에서 **EEVDF**로 바뀌었다.
2. **RT 위에 "deadline" 클래스가 있다.** "10 ms마다 2 ms 보장" 같은 예약(reservation)을 하는 `SCHED_DEADLINE`. 그리고 RT가 CPU를 독점해도 fair 태스크가 완전히 굶지 않도록 커널이 숨겨 둔 **fair_server**(deadline 클래스의 서버)가 있다.
3. **CPU가 여러 개고, 커널 코드도 선점될 수 있다.** CPU마다 run queue가 따로 있고, 커널 안에서 실행 중인 코드를 언제 끊을 수 있는지는 **preemption 모델**(none / voluntary / full / lazy / RT) 설정에 달렸다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="24" text-anchor="middle" font-size="14" font-weight="bold">RTOS 스케줄러 (Don의 FW)</text>
  <text x="510" y="24" text-anchor="middle" font-size="14" font-weight="bold">Linux 스케줄러</text>
  <rect x="20" y="40" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="65" text-anchor="middle" font-size="13">우선순위별 ready list</text>
  <rect x="20" y="92" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="117" text-anchor="middle" font-size="13">가장 높은 우선순위 ready 태스크 실행</text>
  <rect x="20" y="144" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="169" text-anchor="middle" font-size="13">같은 우선순위: tick마다 round-robin</text>
  <rect x="20" y="196" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="221" text-anchor="middle" font-size="13">ISR 끝 → PendSV에서 문맥 전환</text>
  <rect x="20" y="248" width="300" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="273" text-anchor="middle" font-size="13">CPU 1개 · 주소 공간 1개</text>
  <rect x="360" y="40" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="65" text-anchor="middle" font-size="13">CPU마다 run queue (struct rq)</text>
  <rect x="360" y="92" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="117" text-anchor="middle" font-size="13">클래스 순서 stop > dl > rt > fair > idle</text>
  <rect x="360" y="144" width="300" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="169" text-anchor="middle" font-size="13">RT(FIFO/RR): RTOS와 같은 고정 우선순위</text>
  <rect x="360" y="196" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="221" text-anchor="middle" font-size="13">fair(EEVDF): weight 비율로 CPU 나누기</text>
  <rect x="360" y="248" width="300" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="273" text-anchor="middle" font-size="13">커널 코드 선점 여부 = preemption 모델</text>
  <line x1="320" y1="112" x2="358" y2="160" stroke="#3f9a6b" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
  <text x="340" y="314" text-anchor="middle" font-size="13">초록 점선: RTOS의 규칙은 Linux RT 클래스에 거의 그대로 옮겨 온다</text>
</svg>
```

그림 1 — RTOS 스케줄러와 Linux 스케줄러. Linux RT 클래스는 Don이 아는 RTOS 규칙과 같다. 새로 배울 것은 그 위(deadline), 그 아래(fair/EEVDF), 그리고 SMP·커널 선점이다.

### 0.1 이 노트의 지도

| 질문 | 짧은 답 | 절 |
|---|---|---|
| 커널은 "태스크"를 무엇으로 표현하나 | 스레드 하나 = `struct task_struct` 하나. 프로세스는 같은 `mm`을 공유하는 task 묶음(`tgid`) | §2 |
| 다음에 누구를 돌리나 | CPU별 `struct rq`에서 클래스 순서대로 물어보고 처음 나온 태스크 | §3 |
| 보통 프로세스끼리는 | EEVDF: 덜 받은(eligible) 태스크 중 가상 deadline이 가장 이른 것 | §4 |
| nice는 무엇을 바꾸나 | weight. nice 1 차이 ≈ CPU 몫 10% 차이(weight 1.25배) | §4.2 |
| RT 태스크가 CPU를 독점하면 | 6.12+ 기본 설정에선 fair_server가 1초마다 50 ms를 fair에게 돌려준다 | §5 |
| "주기마다 x ms 보장"은 | SCHED_DEADLINE (runtime/deadline/period + admission control) | §6 |
| 언제 실제로 바뀌나 | `TIF_NEED_RESCHED`가 서고 → IRQ 리턴·유저 복귀·`preempt_enable`·`cond_resched`에서 `schedule()` | §7 |
| 얼마나 늦게 깨어나나 | 측정: 부하 아래 SCHED_OTHER 최대 120 ms, SCHED_FIFO 최대 0.84 ms (QEMU) | §8 |
| 전환 순간 무슨 일이 | `context_switch()` → `switch_mm_irqs_off()` → `cpu_switch_to()` (arm64) | §10 |

---

## 1. 실습 준비 — QEMU 디버그 커널과 정적 테스트 프로그램

### 1.1 실행 환경

이 노트의 게스트 실험은 공용 랩의 **디버그 커널** `out/Image-dbg`(Linux 6.18.55-dbg, ftrace·kprobes·lockdep·SCHEDSTATS 켜짐)에서 `-smp 2`로 돌렸다. 유저 프로그램은 Lima VM(호스트 커널 `uname -r` → `7.0.0-38-generic`, aarch64)에서 `gcc -static`으로 빌드해 initramfs에 넣었다. BusyBox initramfs에는 glibc가 없으므로 **정적 링크**가 필수다.

예제 1 — 랩 디버그 커널의 스케줄러 관련 설정 확인 (VM에서).

```sh
uname -r
grep -E "^(# )?CONFIG_(HZ|PREEMPT|PREEMPT_LAZY|PREEMPT_DYNAMIC|PREEMPT_RT|FAIR_GROUP_SCHED|RT_GROUP_SCHED|SCHED_CLASS_EXT|SCHEDSTATS|SCHED_TRACER|NO_HZ_IDLE)[= ]" \
    /var/lab/out/linux-build-dbg/.config
```

```text
7.0.0-38-generic
CONFIG_NO_HZ_IDLE=y
CONFIG_PREEMPT=y
# CONFIG_PREEMPT_LAZY is not set
# CONFIG_PREEMPT_DYNAMIC is not set
CONFIG_FAIR_GROUP_SCHED=y
# CONFIG_RT_GROUP_SCHED is not set
CONFIG_HZ=250
CONFIG_SCHEDSTATS=y
CONFIG_SCHED_TRACER=y
```

출력에서 볼 것: `CONFIG_PREEMPT=y`(full preemption), `HZ=250`(tick 4 ms), **`RT_GROUP_SCHED`가 꺼져 있다**는 점. 마지막이 §5의 RT throttling 동작을 결정한다. `PREEMPT_RT`와 `SCHED_CLASS_EXT`(sched_ext)는 줄 자체가 없다 — 의존 조건(EXPERT, BPF 등)이 안 맞아 선택지에 나타나지도 않았다는 뜻이다.

### 1.2 게스트 실행 스크립트

E4에서 쓴 실행기와 같은 방식이다. 바이너리와 테스트 스크립트를 랩 rootfs 복사본에 넣고, `LAB_CMD=sh~/t.sh`로 스크립트 하나를 돌린 뒤 꺼진다. 작업 디렉터리는 `/var/tmp/F1/`.

```sh
#!/bin/bash
# usage: run_guest.sh <tag> <test.sh> [extra kernel args] ; env KIMG, FILES, SMP, TMO, QEXTRA
set -e
tag=$1; t=$2; extra=$3
W=/var/tmp/F1/run_$tag
rm -rf $W && mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
for f in $FILES; do cp $f $W/r/; done
cp $t $W/r/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
KIMG=${KIMG:-/var/lab/out/Image-dbg}
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m 512M -nographic -no-reboot $QEXTRA \
  -kernel $KIMG -initrd $W/i.gz -append "console=ttyAMA0 quiet $extra LAB_CMD=sh~/t.sh" 2>&1 | tr -d '\r'
```

```sh
# 유저 프로그램 빌드 (경고 0개 확인)
gcc -static -std=gnu11 -Wall -Wextra -O2 ~/.elinux_scratch/F1/f1_share.c -o /var/tmp/F1/f1_share
# 게스트 실행 (시간 측정 실험은 -icount로)
QEXTRA="-icount shift=3" FILES="/var/tmp/F1/f1_lat /var/tmp/F1/f1_spin" ./run_guest.sh t3c t3c.sh
```

### 1.3 주의 — 에뮬레이터에서 시간을 재면 무엇이 틀어지나

QEMU는 KVM 없이 TCG(명령어 번역)로 돌고, 같은 VM에서 다른 노트 작성자 10여 명의 QEMU가 동시에 돌았다(호스트 load average 17). 이 상태에서 처음 지연 측정을 했더니 **게스트의 vCPU 스레드 자체가 호스트에서 밀려나** 수십 ms 지연이 SCHED_FIFO에도 찍혔다 — Linux 스케줄러가 아니라 에뮬레이터의 잡음이다.

그래서 시간에 민감한 실험(§5, §8, §9, §11)은 `-icount shift=3`으로 돌렸다. icount 모드에서는 게스트 시계가 **실행한 명령어 수**로 흐른다(명령어 1개 = 2^3 ns). 호스트가 vCPU를 쉬게 해도 게스트 시간은 멈춰 있으므로 잡음이 사라진다. 대가는 두 가지다: (1) 모든 것이 느리다 — 게스트 CPU가 약 125 MIPS인 셈이라 절대 지연값(수백 µs)은 실제 Cortex-A 보드(수십 µs)보다 한 자릿수 크다. (2) vCPU들이 한 호스트 스레드에서 번갈아 돈다. **이 노트의 절대 숫자는 "상대 비교"용**이다. FIFO와 OTHER의 차이, weight 비율, fair_server의 50 ms 같은 **구조적인 결과**만 실제 보드에 그대로 옮겨 말하자. §4의 weight 실험은 비율만 보면 되므로 icount 없이 돌렸다.

---

## 2. task_struct — 커널이 보는 "태스크"

### 2.1 직관

RTOS의 TCB(Task Control Block)를 떠올리면 된다. FreeRTOS `TCB_t`에는 스택 포인터, 우선순위, 상태 리스트 노드, 이름이 있다. Linux의 `struct task_struct`도 같은 역할인데, 6.18에서 `include/linux/sched.h` 기준 **수백 개 필드**를 가진 큰 구조체다. 프로세스의 주소 공간(`mm`), 열린 파일(`files`), 시그널, 자격 증명(`cred`)까지 전부 여기서 포인터로 이어진다.

핵심 규칙 두 개:

- **스레드 하나 = task_struct 하나.** Linux 커널에는 "프로세스" 구조체가 따로 없다. `pthread_create`로 만든 스레드도, `fork`로 만든 프로세스도, 커널 스레드(kthread)도 전부 task_struct다. 같은 프로세스의 스레드들은 같은 `mm`(주소 공간)과 같은 `tgid`(thread group id)를 공유한다. 유저가 `getpid()`로 보는 값이 `tgid`, `gettid()`로 보는 값이 `pid`다.
- **스케줄러는 task_struct 단위로 스케줄한다.** 그래서 스레드 4개짜리 프로세스는 fair 클래스에서 기본적으로 스레드 1개짜리 프로세스보다 CPU를 4배 받을 수 있다(cgroup으로 묶지 않는 한).

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="175" y="22" text-anchor="middle" font-size="14" font-weight="bold">struct task_struct (스케줄러가 보는 부분)</text>
  <rect x="15" y="32" width="320" height="380" rx="6" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <rect x="28" y="44" width="294" height="62" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="40" y="63" font-size="13" font-weight="bold">식별</text>
  <text x="40" y="81" font-size="12">pid · tgid · comm[16]</text>
  <text x="40" y="98" font-size="12">real_parent · children · tasks 리스트</text>
  <rect x="28" y="114" width="294" height="62" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="40" y="133" font-size="13" font-weight="bold">상태</text>
  <text x="40" y="151" font-size="12">__state (RUNNING, INTERRUPTIBLE ...)</text>
  <text x="40" y="168" font-size="12">on_rq · on_cpu · flags (PF_KTHREAD)</text>
  <rect x="28" y="184" width="294" height="80" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="40" y="203" font-size="13" font-weight="bold">스케줄링</text>
  <text x="40" y="221" font-size="12">sched_class · policy · cpus_ptr</text>
  <text x="40" y="238" font-size="12">prio · static_prio · normal_prio</text>
  <text x="40" y="255" font-size="12">se · rt · dl · rt_priority</text>
  <rect x="28" y="272" width="294" height="62" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="40" y="291" font-size="13" font-weight="bold">자원</text>
  <text x="40" y="309" font-size="12">mm · active_mm (주소 공간)</text>
  <text x="40" y="326" font-size="12">files (fd 테이블) · fs · signal · cred</text>
  <rect x="28" y="342" width="294" height="62" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="40" y="361" font-size="13" font-weight="bold">실행 문맥</text>
  <text x="40" y="379" font-size="12">thread_info.flags (NEED_RESCHED)</text>
  <text x="40" y="396" font-size="12">stack (16 KiB) · thread.cpu_context</text>
  <rect x="400" y="50" width="265" height="56" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-dasharray="4 3"/>
  <text x="532" y="73" text-anchor="middle" font-size="12">current → sp_el0 레지스터 (arm64)</text>
  <text x="532" y="92" text-anchor="middle" font-size="12">cpu_switch_to가 전환 때 바꿔 넣는다</text>
  <rect x="400" y="170" width="265" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="412" y="190" font-size="12" font-weight="bold">struct sched_entity se (fair)</text>
  <text x="412" y="208" font-size="12">load.weight · vruntime · vlag</text>
  <text x="412" y="222" font-size="12">deadline · slice · run_node</text>
  <rect x="400" y="234" width="265" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="412" y="254" font-size="12">rt: run_list (우선순위별 큐)</text>
  <text x="412" y="274" font-size="12">dl: runtime · deadline · period</text>
  <rect x="400" y="298" width="265" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="412" y="318" font-size="12" font-weight="bold">struct mm_struct</text>
  <text x="412" y="336" font-size="12">pgd (페이지 테이블) · VMA 트리</text>
  <rect x="400" y="356" width="265" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="412" y="376" font-size="12" font-weight="bold">커널 스택 16 KiB</text>
  <text x="412" y="394" font-size="12">pt_regs (유저 레지스터 저장)</text>
  <line x1="322" y1="250" x2="398" y2="200" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="322" y1="252" x2="398" y2="262" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="322" y1="305" x2="398" y2="322" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="322" y1="392" x2="398" y2="381" stroke="currentColor" marker-end="url(#ah2)"/>
</svg>
```

그림 2 — `task_struct`에서 스케줄러와 관련된 필드 지도. `se`·`rt`·`dl`은 포인터가 아니라 **구조체가 그대로 박혀 있다**(embedded). 클래스가 바뀌어도 모든 엔티티가 늘 존재하고, `sched_class` 포인터가 어느 쪽을 쓸지 정한다.

### 2.2 필드 정의 (6.18.55 `include/linux/sched.h`에서 확인)

| 필드 | 타입 | 뜻 |
|---|---|---|
| `__state` | `unsigned int` | `TASK_RUNNING`(0x0, 실행 중이거나 run queue에 있음), `TASK_INTERRUPTIBLE`(0x1, 시그널로 깨울 수 있는 잠), `TASK_UNINTERRUPTIBLE`(0x2, `D` 상태) 등. 직접 읽지 말고 `READ_ONCE()`·`task_is_running()` |
| `on_rq` / `on_cpu` | `int` | run queue에 올라가 있나 / 지금 어떤 CPU에서 실제로 돌고 있나 |
| `pid` / `tgid` | `pid_t` | 스레드 id / 스레드 그룹(프로세스) id |
| `prio` | `int` | **실효 우선순위**(0이 가장 높음). PI(priority inheritance)로 잠시 올라갈 수 있다 |
| `static_prio` | `int` | nice에서 온 값: `120 + nice` (100~139) |
| `normal_prio` | `int` | 정책 기준 우선순위: RT면 `99 - rt_priority`, fair면 `static_prio`, deadline이면 -1 |
| `rt_priority` | `unsigned int` | 유저가 준 RT 우선순위 1~99 (클수록 높음) |
| `policy` | `unsigned int` | `SCHED_NORMAL`(0) `FIFO`(1) `RR`(2) `BATCH`(3) `IDLE`(5) `DEADLINE`(6) |
| `sched_class` | `const struct sched_class *` | 이 태스크를 다루는 클래스의 함수 테이블 |
| `se` / `rt` / `dl` | `sched_entity` / `sched_rt_entity` / `sched_dl_entity` | 클래스별 스케줄링 상태 |
| `cpus_ptr` | `const cpumask_t *` | 돌 수 있는 CPU 집합 (`taskset`, `sched_setaffinity`) |
| `mm` / `active_mm` | `struct mm_struct *` | 주소 공간. kthread는 `mm == NULL`이고 직전 태스크의 것을 `active_mm`으로 빌려 쓴다 |
| `files` | `struct files_struct *` | fd 테이블 (A3의 그 테이블) |
| `stack` | `void *` | 커널 스택 (arm64 16 KiB, E4 §4) |
| `nvcsw` / `nivcsw` | `unsigned long` | 자발적(잠듦) / 비자발적(선점됨) context switch 횟수 |

우선순위 숫자가 세 겹이라 헷갈린다. 한 줄로 정리하면 **커널 내부 `prio`는 0~139 한 줄 위에 있고 작을수록 높다**:

| 범위 | 누구 | 유저가 보는 값 |
|---|---|---|
| -1 | SCHED_DEADLINE | (우선순위 없음, deadline으로 경쟁) |
| 0 ~ 98 | SCHED_FIFO/RR | `rt_priority` 99 ~ 1 (`prio = 99 - rt_priority`) |
| 100 ~ 139 | SCHED_NORMAL/BATCH | nice -20 ~ +19 (`prio = 120 + nice`) |

> 주의: `ps -o pri`, `top`의 PR 열, `/proc/<pid>/stat`의 priority 필드는 각자 다른 변환식을 쓴다. 면접에서 숫자를 말할 때는 "커널 내부 prio" 인지 "rt_priority" 인지 먼저 밝히자.

### 2.3 예제 2 — 모듈로 `current`의 스케줄링 필드를 찍고, 전체 프로세스를 RCU로 순회

무엇을 확인하나: `insmod`를 실행하는 태스크(`current`)의 필드를 그대로 찍는다. 같은 모듈을 그냥 / `nice -n 5`로 / `chrt -f 30`으로 로드해서 prio 3종과 weight가 어떻게 바뀌는지 본다. 그다음 `for_each_process()`로 모든 프로세스를 돈다. 태스크 리스트는 **RCU로 보호**되므로 `rcu_read_lock()` 안에서 읽기만 한다(F4에서 자세히).

```c
// SPDX-License-Identifier: GPL-2.0
/* f1_task: dump scheduler fields of current, then walk all processes under RCU */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/rcupdate.h>
#include <linux/fdtable.h>

static void f1_dump(struct task_struct *t)
{
	pr_info("f1: comm=%s pid=%d tgid=%d cpu=%u __state=0x%x on_cpu=%d on_rq=%d\n",
		t->comm, t->pid, t->tgid, task_cpu(t), READ_ONCE(t->__state), t->on_cpu, t->on_rq);
	pr_info("f1: policy=%u prio=%d static_prio=%d normal_prio=%d rt_priority=%u class=%ps\n",
		t->policy, t->prio, t->static_prio, t->normal_prio, t->rt_priority, t->sched_class);
	/* 64-bit kernels keep load weights scaled by 1024 (SCHED_FIXEDPOINT_SHIFT) */
	pr_info("f1: se.weight=%lu se.slice=%llu se.vruntime=%llu se.deadline=%llu se.sum_exec=%llu\n",
		t->se.load.weight >> SCHED_FIXEDPOINT_SHIFT, t->se.slice, t->se.vruntime,
		t->se.deadline, t->se.sum_exec_runtime);
	pr_info("f1: nvcsw=%lu nivcsw=%lu mm=%p active_mm=%p files=%p stack=%p\n",
		t->nvcsw, t->nivcsw, t->mm, t->active_mm, t->files, t->stack);
}

static int __init f1_init(void)
{
	struct task_struct *p, *t;
	int procs = 0, threads = 0, kthreads = 0, rt = 0;

	f1_dump(current);

	rcu_read_lock();                        /* the task list is RCU-protected */
	for_each_process(p) {
		procs++;
		if (p->flags & PF_KTHREAD)
			kthreads++;
		if (p->policy == SCHED_FIFO || p->policy == SCHED_RR) {
			rt++;
			pr_info("f1: rt task pid=%-3d %-16s policy=%u rt_priority=%u prio=%d\n",
				p->pid, p->comm, p->policy, p->rt_priority, p->prio);
		} else if (!(p->flags & PF_KTHREAD)) {
			pr_info("f1: user proc pid=%-3d %-16s threads=%d nice=%d mm=%p\n",
				p->pid, p->comm, get_nr_threads(p), task_nice(p), p->mm);
		}
	}
	for_each_process_thread(p, t)
		threads++;
	rcu_read_unlock();

	pr_info("f1: processes=%d (kthreads=%d, rt=%d) threads=%d\n", procs, kthreads, rt, threads);
	return 0;
}

static void __exit f1_exit(void) { }

module_init(f1_init);
module_exit(f1_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F1: task_struct scheduler fields + for_each_process");
```

빌드는 **디버그 커널의 빌드 트리**로 한다(`vermagic`이 `6.18.55-dbg`여야 `Image-dbg`에 들어간다).

```sh
cd /var/tmp/F1/mod && echo "obj-m += f1_task.o" > Kbuild
make -j2 -C /var/lab/out/linux-build-dbg M=$PWD modules
modinfo f1_task.ko | grep vermagic
```

```text
  CC [M]  f1_task.o
  CC [M]  f1_task.mod.o
  CC [M]  .module-common.o
  LD [M]  f1_task.ko
vermagic:       6.18.55-dbg SMP preempt mod_unload aarch64
```

게스트 스크립트:

```sh
echo "== insmod (plain)"; insmod /f1_task.ko; dmesg | grep "f1:" | head -4; rmmod f1_task
dmesg | grep "f1:" | tail -n +5 > /walk.txt
echo "== nice -n 5 insmod"; dmesg -c > /dev/null; nice -n 5 /sbin/insmod /f1_task.ko; dmesg | grep "f1:" | sed -n 2,3p; rmmod f1_task
echo "== chrt -f 30 insmod"; dmesg -c > /dev/null; chrt -f 30 /sbin/insmod /f1_task.ko; dmesg | grep "f1:" | sed -n 2,3p; rmmod f1_task
echo "== walk (plain run)"; cat /walk.txt
```

```text
lab initramfs: 6.18.55-dbg up 27.68s
== insmod (plain)
[   28.363807] f1: comm=insmod pid=74 tgid=74 cpu=1 __state=0x0 on_cpu=1 on_rq=1
[   28.363884] f1: policy=0 prio=120 static_prio=120 normal_prio=120 rt_priority=0 class=fair_sched_class
[   28.364013] f1: se.weight=1024 se.slice=1400000 se.vruntime=19459677956 se.deadline=19461077956 se.sum_exec=62227968
[   28.364088] f1: nvcsw=0 nivcsw=1 mm=000000004bcf0220 active_mm=000000004bcf0220 files=00000000b16882cd stack=0000000019134122
== nice -n 5 insmod
[   29.221360] f1: policy=0 prio=125 static_prio=125 normal_prio=125 rt_priority=0 class=fair_sched_class
[   29.221489] f1: se.weight=335 se.slice=1400000 se.vruntime=20031845840 se.deadline=20036125243 se.sum_exec=84505440
== chrt -f 30 insmod
[   29.765276] f1: policy=1 prio=69 static_prio=120 normal_prio=69 rt_priority=30 class=rt_sched_class
[   29.765404] f1: se.weight=1024 se.slice=1400000 se.vruntime=9061862284 se.deadline=758656 se.sum_exec=79928368
== walk (plain run)
[   28.364183] f1: user proc pid=1   init             threads=1 nice=0 mm=000000005bfd7a51
[   28.364257] f1: rt task pid=18  migration/0      policy=1 rt_priority=99 prio=0
[   28.364326] f1: rt task pid=21  migration/1      policy=1 rt_priority=99 prio=0
[   28.364398] f1: rt task pid=47  watchdogd        policy=1 rt_priority=50 prio=49
[   28.364468] f1: user proc pid=72  sh               threads=1 nice=0 mm=00000000248ede19
[   28.364541] f1: user proc pid=74  insmod           threads=1 nice=0 mm=000000004bcf0220
[   28.364625] f1: processes=60 (kthreads=57, rt=3) threads=60
```

출력에서 볼 것 (포인터 값은 `%p` 해시라 실행마다 다르다):

- **`__state=0x0 on_cpu=1 on_rq=1`**: 지금 이 코드를 실행하는 태스크는 당연히 RUNNING이고 CPU 위에 있다.
- **prio 3종**: 그냥 실행 → 120/120/120. `nice -n 5` → 125/125/125. `chrt -f 30` → `normal_prio = 99 - 30 = 69`, `prio = 69`, 그런데 **`static_prio`는 120 그대로**다. RT 정책은 nice를 쓰지 않을 뿐 지우지 않는다 — 나중에 SCHED_OTHER로 돌아가면 원래 nice가 살아난다.
- **`class=%ps`가 심볼 이름을 찍었다**: `fair_sched_class` → `rt_sched_class`. `sched_class`는 그냥 함수 포인터 테이블(구조체 상수) 주소다. 드라이버의 `file_operations`와 똑같은 패턴이다(G·I 모듈).
- **weight**: nice 0 = 1024, nice 5 = 335. 아래 §4.2의 표 그대로다.
- **EEVDF의 virtual deadline 공식이 눈에 보인다**: nice 0에서 `deadline - vruntime = 19461077956 - 19459677956 = 1,400,000` = slice. nice 5에서는 `20036125243 - 20031845840 = 4,279,403` = `1,400,000 × 1024 / 335`. 가벼운(weight 작은) 태스크일수록 가상 deadline이 멀리 잡힌다(§4.4).
- **walk**: 프로세스 60개 중 57개가 kthread다(BusyBox 최소 시스템이라 유저 프로세스는 init·sh·insmod뿐). `migration/N`은 RT 99로 보이지만 실제로는 **stop 클래스**다 — `sched_set_stop_task()`가 "유저가 헷갈리지 않게 SCHED_FIFO처럼 보이게" 정책을 FIFO 99로 적고 `sched_class`만 `stop_sched_class`로 바꾼다(`kernel/sched/core.c` 주석 그대로). `watchdogd`는 `sched_set_fifo()`(커널 kthread용 표준 RT 우선순위 `MAX_RT_PRIO/2` = 50)를 쓴 예다. threaded IRQ 핸들러 스레드(`irq/NN-이름`)도 같은 함수로 FIFO 50을 받는다(F6).

### 2.4 `current`는 어떻게 찾나 — arm64는 레지스터 한 개

Cortex-M RTOS에서 `pxCurrentTCB`는 전역 변수다. Linux는 CPU마다 현재 태스크가 다르니 전역 하나로는 안 된다. arm64는 **`sp_el0` 레지스터에 현재 `task_struct` 주소를 넣어 둔다**(커널 모드에서는 EL0 스택 포인터가 비어 있으므로 재활용). context switch의 마지막 어셈블리(`cpu_switch_to`)에 `msr sp_el0, x1`이 있다(§10). 그래서 `current`는 메모리 접근 없이 `mrs` 명령 하나다. `thread_info`도 `task_struct` 맨 앞에 박혀 있어서(`CONFIG_THREAD_INFO_IN_TASK=y`) 같은 포인터로 찾는다.

---

## 3. run queue와 스케줄링 클래스

### 3.1 직관 — CPU마다 대기실 하나, 대기실 안에 줄이 다섯

RTOS에는 ready list가 하나다. Linux는 **CPU마다 `struct rq`가 하나씩** 있고(per-CPU 변수 `runqueues`), 그 안에 클래스별 하위 큐가 있다: `rq->dl`, `rq->rt`, `rq->cfs`, 그리고 특별한 태스크 포인터 `rq->stop`, `rq->idle`. 한 태스크는 한 시점에 한 CPU의 rq에만 있고, CPU 사이 이동은 load balancer(fair)와 push/pull(RT, deadline)이 맡는다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">struct rq (CPU 하나당 하나) — pick_next_task는 위에서부터 묻는다</text>
  <rect x="20" y="40" width="140" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="90" y="58" text-anchor="middle" font-size="13" font-weight="bold">stop</text>
  <text x="90" y="76" text-anchor="middle" font-size="12">(유저 정책 없음)</text>
  <rect x="175" y="40" width="360" height="44" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a"/>
  <text x="187" y="67" font-size="12">rq->stop = migration/N (stop_machine)</text>
  <rect x="20" y="94" width="140" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="90" y="112" text-anchor="middle" font-size="13" font-weight="bold">dl</text>
  <text x="90" y="130" text-anchor="middle" font-size="12">SCHED_DEADLINE</text>
  <rect x="175" y="94" width="360" height="44" rx="5" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c"/>
  <text x="187" y="121" font-size="12">dl_rq: 절대 deadline 순 rbtree (+ fair_server)</text>
  <rect x="20" y="148" width="140" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="90" y="166" text-anchor="middle" font-size="13" font-weight="bold">rt</text>
  <text x="90" y="184" text-anchor="middle" font-size="12">SCHED_FIFO / RR</text>
  <rect x="175" y="148" width="360" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b"/>
  <text x="187" y="175" font-size="12">rt_rq: 우선순위 100단계 큐 + 비트맵 → O(1)</text>
  <rect x="20" y="202" width="140" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="90" y="220" text-anchor="middle" font-size="13" font-weight="bold">fair</text>
  <text x="90" y="238" text-anchor="middle" font-size="12">OTHER/BATCH/IDLE</text>
  <rect x="175" y="202" width="360" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
  <text x="187" y="229" font-size="12">cfs_rq: rbtree, EEVDF로 하나 고르기</text>
  <rect x="20" y="256" width="140" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="90" y="274" text-anchor="middle" font-size="13" font-weight="bold">idle</text>
  <text x="90" y="292" text-anchor="middle" font-size="12">(유저 정책 없음)</text>
  <rect x="175" y="256" width="360" height="44" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="187" y="283" font-size="12">rq->idle = swapper/N (할 일 없을 때 WFI)</text>
  <line x1="600" y1="44" x2="600" y2="296" stroke="currentColor" stroke-width="2" marker-end="url(#ah3)"/>
  <text x="612" y="60" font-size="12">높음</text>
  <text x="612" y="296" font-size="12">낮음</text>
  <rect x="20" y="320" width="300" height="60" rx="5" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="170" y="345" text-anchor="middle" font-size="12">CPU0의 rq · CPU1의 rq · CPU2의 rq ...</text>
  <text x="170" y="365" text-anchor="middle" font-size="12">태스크는 한 번에 한 rq에만 있다</text>
  <rect x="360" y="320" width="300" height="60" rx="5" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="510" y="345" text-anchor="middle" font-size="12">CPU 사이 이동: fair = load balance</text>
  <text x="510" y="365" text-anchor="middle" font-size="12">rt · dl = push / pull</text>
</svg>
```

그림 3 — 한 CPU의 run queue와 클래스 순서. 위 클래스에 실행 가능한 태스크가 하나라도 있으면 아래 클래스는 기회가 없다(예외는 §5의 fair_server).

### 3.2 클래스 순서는 링커가 정한다

클래스 순서는 if문이 아니라 **링커 스크립트의 섹션 배치**로 정해진다. 각 클래스는 `DEFINE_SCHED_CLASS(name)`으로 자기 섹션에 들어가고, `include/asm-generic/vmlinux.lds.h`가 순서대로 붙인다:

```c
/* include/asm-generic/vmlinux.lds.h (6.18.55) */
#define SCHED_DATA				\
	STRUCT_ALIGN();				\
	__sched_class_highest = .;		\
	*(__stop_sched_class)			\
	*(__dl_sched_class)			\
	*(__rt_sched_class)			\
	*(__fair_sched_class)			\
	*(__ext_sched_class)			\
	*(__idle_sched_class)			\
	__sched_class_lowest = .;
```

그래서 `kernel/sched/sched.h`의 비교는 그냥 주소 비교다: `#define sched_class_above(_a, _b) ((_a) < (_b))`. `ext`는 sched_ext(BPF로 스케줄러를 갈아 끼우는 클래스, 6.12+)이고 랩 커널에는 빌드되지 않았다.

`__pick_next_task()`(`kernel/sched/core.c`)의 실제 모양:

```c
	/*
	 * Optimization: we know that if all tasks are in the fair class we can
	 * call that function directly, ...
	 */
	if (likely(!sched_class_above(rq->donor->sched_class, &fair_sched_class) &&
		   rq->nr_running == rq->cfs.h_nr_queued)) {
		p = pick_next_task_fair(rq, rq->donor, rf);
		...
	}
restart:
	prev_balance(rq, rf);
	for_each_active_class(class) {
		...
			p = class->pick_task(rq);
			if (p) { put_prev_set_next_task(rq, rq->donor, p); return p; }
	}
```

읽는 법: **대부분의 시간에 rq에는 fair 태스크만 있다** → 바로 fair에게 묻는 fast path. 아니면 클래스를 위에서부터 돌며 처음으로 태스크를 내놓는 클래스가 이긴다. `rq->donor`는 proxy execution(락을 쥔 태스크가 기다리는 태스크의 "스케줄링 문맥"을 빌려 도는 기능) 준비로 최근 추가된 필드다(`struct rq`의 주석: "Scheduling context"). 이 기능이 꺼진 랩 커널에서는 `rq->curr`와 같다고 읽어도 된다.

### 3.3 `struct sched_class` — 스케줄러의 `file_operations`

각 클래스는 같은 함수 테이블을 채운다 (`kernel/sched/sched.h`에서 발췌):

| 콜백 | 언제 불리나 |
|---|---|
| `enqueue_task` / `dequeue_task` | 깨어나 rq에 들어올 때 / 잠들어 빠질 때 |
| `wakeup_preempt` | 새로 깨어난 태스크가 지금 도는 태스크를 밀어낼지 판단 (옛 이름 `check_preempt_curr`) |
| `pick_task` / `pick_next_task` | 다음에 돌 태스크 고르기 |
| `put_prev_task` / `set_next_task` | 전환 전후 회계 처리 |
| `task_tick` | 매 tick(`sched_tick()`)에서 현재 태스크에 대해 |
| `select_task_rq` | 깨울 때 어느 CPU의 rq에 넣을지 |
| `task_fork` / `task_dead` | 생성·소멸 |
| `prio_changed` / `switched_to` | `nice`·`chrt`로 바뀔 때 |

드라이버를 써 본 사람에게 익숙한 구조다: 공통 코어(`kernel/sched/core.c`)가 정책별 구현(`fair.c`, `rt.c`, `deadline.c`, `idle.c`, `stop_task.c`)을 함수 포인터로 부른다.

### 3.4 예제 3 — BusyBox `chrt`/`taskset`으로 정책과 affinity 보기

무엇을 확인하나: 랩 initramfs의 BusyBox에 `chrt`, `taskset`, `nice`, `renice`가 있다. 정책별 우선순위 범위와 실행 중인 프로세스의 정책·affinity를 본다.

```sh
chrt -m
taskset -c 1 /f1_spin 40 & HOG=$!
sleep 2
taskset -p $HOG; chrt -p $HOG
```

```text
SCHED_OTHER min/max priority	: 0/0
SCHED_FIFO min/max priority	: 1/99
SCHED_RR min/max priority	: 1/99
SCHED_BATCH min/max priority	: 0/0
SCHED_IDLE min/max priority	: 0/0
pid 73's current affinity mask: 2
pid 73's current scheduling policy: SCHED_OTHER
pid 73's current scheduling priority: 0
```

출력에서 볼 것: RT 정책만 1~99의 우선순위를 가진다. affinity mask `2` = 비트 1 = CPU1만. BusyBox `chrt`는 `SCHED_DEADLINE`을 설정하지 못한다(옵션 목록에 `-d`가 없다) — 그래서 §6은 C로 `sched_setattr()`을 직접 부른다. util-linux `chrt`는 `--deadline --sched-runtime ...`을 지원한다(랩에는 없음, 실행하지 않았다).

C에서는 이렇게 한다(모든 예제에서 쓰는 패턴):

```c
cpu_set_t s;
CPU_ZERO(&s); CPU_SET(1, &s);
sched_setaffinity(0, sizeof(s), &s);                 /* taskset -c 1 */
struct sched_param sp = { .sched_priority = 80 };
sched_setscheduler(0, SCHED_FIFO, &sp);              /* chrt -f 80 */
setpriority(PRIO_PROCESS, 0, 5);                     /* renice / nice -n 5 */
```

---

## 4. fair 클래스 — CFS에서 EEVDF로

### 4.1 직관 — "공평"을 가상 시간으로 잰다

RTOS에서 같은 우선순위 태스크 3개는 tick마다 돌아가며 돈다. 결과적으로 각자 1/3씩 받는다. fair 클래스는 이것을 **비율까지 조절 가능하게** 일반화한 것이다: 태스크마다 weight가 있고, 각자 `weight / Σweight` 만큼 CPU를 받는 것이 목표다.

이를 위해 **가상 실행 시간(vruntime)** 을 쓴다. 태스크가 실제로 `Δt` 동안 돌면 vruntime은 `Δt × 1024 / weight`만큼 늘어난다(`calc_delta_fair()`). nice 0(weight 1024)은 실제 시간 그대로, 무거운 태스크(weight 큼)는 느리게, 가벼운 태스크는 빠르게 늘어난다. **모두의 vruntime을 비슷하게 유지하면 자동으로 weight 비율대로 CPU를 나눈 셈**이 된다.

- **CFS(2.6.23 ~ 6.5)**: "vruntime이 가장 작은 태스크를 돌린다". rbtree의 가장 왼쪽 노드. 얼마나 오래 돌릴지는 `sched_latency`/`sched_min_granularity` 같은 휴리스틱으로 정했다. 지연(latency) 요구를 표현할 방법이 nice밖에 없어서, 깨어난 태스크를 얼마나 빨리 돌릴지에 휴리스틱이 계속 덧붙었다.
- **EEVDF(6.6부터, 6.12 무렵 완성)**: "**받아야 할 몫보다 덜 받은(eligible) 태스크 중, 가상 deadline이 가장 이른 것**을 돌린다". 1995년 Stoica 등의 논문 알고리즘을 Peter Zijlstra가 커널에 맞게 구현했다(`Documentation/scheduler/sched-eevdf.rst`). 공평함(lag)과 지연(slice → deadline)이 분리되어, 짧은 slice를 요청한 태스크가 더 자주(더 빨리) 뽑힌다.

### 4.2 nice → weight

`kernel/sched/core.c`의 표다(6.18.55에서 복사):

```c
const int sched_prio_to_weight[40] = {
 /* -20 */     88761,     71755,     56483,     46273,     36291,
 /* -15 */     29154,     23254,     18705,     14949,     11916,
 /* -10 */      9548,      7620,      6100,      4904,      3906,
 /*  -5 */      3121,      2501,      1991,      1586,      1277,
 /*   0 */      1024,       820,       655,       526,       423,
 /*   5 */       335,       272,       215,       172,       137,
 /*  10 */       110,        87,        70,        56,        45,
 /*  15 */        36,        29,        23,        18,        15,
};
```

규칙은 하나다: **nice가 1 오를 때마다 weight는 약 1/1.25배**. 두 태스크가 nice 0과 1이면 1024 : 820 → 55.5% : 44.5%. 즉 "nice 1 차이 = CPU 약 10% 차이"가 설계 의도다(`Documentation/scheduler/sched-nice-design.rst`). 비율 기반이라 nice 0 vs 5와 nice 10 vs 15는 같은 비율(약 3:1)이 된다.

> 함정: nice는 **같은 CPU에서 경쟁할 때만** 의미가 있다. CPU가 남으면 nice 19도 100%를 쓴다. 그리고 nice는 RT 태스크에 아무 영향이 없다.

### 4.3 예제 4 — 같은 CPU의 두 hog, nice만 다르게

무엇을 확인하나: CPU1에 고정한 busy loop 프로세스 여러 개에 서로 다른 nice를 주고 3초 동안 각각 받은 CPU 시간을 잰다. 이론값 `w_i / Σw`와 비교한다.

```c
/* f1_share: N CPU hogs pinned to CPU 1, each with its own nice value.
 * usage: f1_share <secs> <nice0> <nice1> ...   -> CPU time share per hog */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>

static double now(clockid_t c)
{
	struct timespec ts;
	clock_gettime(c, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s secs nice...\n", argv[0]); exit(1); }
	double secs = atof(argv[1]);
	int n = argc - 2, pfd[2];
	if (pipe(pfd) < 0) { perror("pipe"); exit(1); }
	double start = now(CLOCK_MONOTONIC) + 0.2;      /* common start line */
	for (int i = 0; i < n; i++) {
		if (fork() == 0) {
			cpu_set_t set;
			CPU_ZERO(&set); CPU_SET(1, &set);
			if (sched_setaffinity(0, sizeof(set), &set)) { perror("affinity"); _exit(1); }
			if (setpriority(PRIO_PROCESS, 0, atoi(argv[2 + i]))) { perror("nice"); _exit(1); }
			while (now(CLOCK_MONOTONIC) < start) ;
			double c0 = now(CLOCK_PROCESS_CPUTIME_ID);
			while (now(CLOCK_MONOTONIC) < start + secs) ;
			double r[2] = { i, now(CLOCK_PROCESS_CPUTIME_ID) - c0 };
			if (write(pfd[1], r, sizeof(r)) != sizeof(r)) _exit(1);
			_exit(0);
		}
	}
	double cpu[64] = { 0 }, tot = 0, r[2];
	for (int i = 0; i < n; i++) {
		if (read(pfd[0], r, sizeof(r)) != sizeof(r)) { perror("read"); exit(1); }
		cpu[(int)r[0]] = r[1]; tot += r[1];
	}
	while (wait(NULL) > 0) ;
	for (int i = 0; i < n; i++)
		printf("hog%d nice=%3s cpu=%6.3fs share=%5.1f%%\n", i, argv[2 + i], cpu[i], 100 * cpu[i] / tot);
	return 0;
}
```

```sh
for p in "0 0" "0 1" "0 5" "0 10" "0 19"; do echo "== nice $p"; /f1_share 3 $p; done
echo "== nice 0 5 10"; /f1_share 3 0 5 10
```

```text
lab initramfs: 6.18.55-dbg up 6.05s
== nice 0 0
hog0 nice=  0 cpu= 1.384s share= 50.0%
hog1 nice=  0 cpu= 1.384s share= 50.0%
== nice 0 1
hog0 nice=  0 cpu= 1.423s share= 55.7%
hog1 nice=  1 cpu= 1.130s share= 44.3%
== nice 0 5
hog0 nice=  0 cpu= 1.839s share= 75.3%
hog1 nice=  5 cpu= 0.602s share= 24.7%
== nice 0 10
hog0 nice=  0 cpu= 2.004s share= 89.4%
hog1 nice= 10 cpu= 0.237s share= 10.6%
== nice 0 19
hog0 nice=  0 cpu= 2.438s share= 98.5%
hog1 nice= 19 cpu= 0.037s share=  1.5%
```

```text
== nice 0 5 10
hog0 nice=  0 cpu= 1.671s share= 69.5%
hog1 nice=  5 cpu= 0.550s share= 22.9%
hog2 nice= 10 cpu= 0.182s share=  7.6%
```

(두 번째 블록은 별도 부팅에서 실행했다. 첫 부팅에서는 마지막 줄들이 poweroff 전에 콘솔로 다 나가지 못해 잘렸다 — 이후 모든 스크립트 끝에 `sleep 1`을 넣었다.)

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
<text x="320.0" y="22" text-anchor="middle" font-size="14" font-weight="bold">nice 0 hog의 CPU 점유율: 측정 vs weight 이론값</text>
<line x1="60" y1="270.0" x2="620" y2="270.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="274.0" text-anchor="end" font-size="12">0%</text>
<line x1="60" y1="212.5" x2="620" y2="212.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="216.5" text-anchor="end" font-size="12">25%</text>
<line x1="60" y1="155.0" x2="620" y2="155.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="159.0" text-anchor="end" font-size="12">50%</text>
<line x1="60" y1="97.5" x2="620" y2="97.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="101.5" text-anchor="end" font-size="12">75%</text>
<line x1="60" y1="40.0" x2="620" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="44.0" text-anchor="end" font-size="12">100%</text>
<line x1="60" y1="270" x2="620" y2="270" stroke="currentColor"/>
<line x1="60" y1="40" x2="60" y2="270" stroke="currentColor"/>
<rect x="82.0" y="155.0" width="32" height="115.0" fill="#888" fill-opacity="0.75"/>
<text x="98.0" y="150.0" text-anchor="middle" font-size="12">50.0</text>
<rect x="118.0" y="155.0" width="32" height="115.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="134.0" y="150.0" text-anchor="middle" font-size="12">50.0</text>
<text x="116.0" y="288" text-anchor="middle" font-size="13">nice 0 vs 0</text>
<rect x="194.0" y="142.3" width="32" height="127.7" fill="#888" fill-opacity="0.75"/>
<text x="210.0" y="137.3" text-anchor="middle" font-size="12">55.5</text>
<rect x="230.0" y="141.9" width="32" height="128.1" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="246.0" y="136.9" text-anchor="middle" font-size="12">55.7</text>
<text x="228.0" y="288" text-anchor="middle" font-size="13">nice 0 vs 1</text>
<rect x="306.0" y="96.7" width="32" height="173.3" fill="#888" fill-opacity="0.75"/>
<text x="322.0" y="91.7" text-anchor="middle" font-size="12">75.3</text>
<rect x="342.0" y="96.8" width="32" height="173.2" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="358.0" y="91.8" text-anchor="middle" font-size="12">75.3</text>
<text x="340.0" y="288" text-anchor="middle" font-size="13">nice 0 vs 5</text>
<rect x="418.0" y="62.3" width="32" height="207.7" fill="#888" fill-opacity="0.75"/>
<text x="434.0" y="57.3" text-anchor="middle" font-size="12">90.3</text>
<rect x="454.0" y="64.4" width="32" height="205.6" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="470.0" y="59.4" text-anchor="middle" font-size="12">89.4</text>
<text x="452.0" y="288" text-anchor="middle" font-size="13">nice 0 vs 10</text>
<rect x="530.0" y="43.3" width="32" height="226.7" fill="#888" fill-opacity="0.75"/>
<text x="546.0" y="38.3" text-anchor="middle" font-size="12">98.6</text>
<rect x="566.0" y="43.4" width="32" height="226.6" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="582.0" y="38.4" text-anchor="middle" font-size="12">98.5</text>
<text x="564.0" y="288" text-anchor="middle" font-size="13">nice 0 vs 19</text>
<rect x="200" y="300" width="14" height="14" fill="#888" fill-opacity="0.75"/><text x="220" y="312" font-size="13">이론 1024/(1024+w)</text>
<rect x="380" y="300" width="14" height="14" fill="#4a7bd0" fill-opacity="0.75"/><text x="400" y="312" font-size="13">QEMU 게스트 측정</text>
</svg>
```

그림 4 — 측정한 nice 0 쪽 점유율과 이론값 `1024 / (1024 + w)`. 이론값은 50.0, 55.5, 75.3, 90.3, 98.6%. 3개짜리(0/5/10)도 이론 69.7 / 22.8 / 7.5%에 대해 측정 69.5 / 22.9 / 7.6%.

출력에서 볼 것: **weight 표가 그대로 CPU 시간 비율이 된다.** 오차는 1%p 이내다. 합계 CPU 시간이 3초가 아니라 약 2.4~2.8초인 것은 TCG 에뮬레이션에서 타이머 IRQ·기타 kthread가 쓴 몫과 시작 동기화 오차다(비율에는 영향 없음).

### 4.4 EEVDF — lag, eligibility, virtual deadline, slice

EEVDF의 용어 다섯 개를 `kernel/sched/fair.c`의 주석·코드 이름 그대로 정리한다.

| 용어 | 코드 | 정의 |
|---|---|---|
| vruntime `v_i` | `se->vruntime` | 받은 서비스를 weight로 나눈 가상 시간 |
| V (가상 시간 평균) | `avg_vruntime(cfs_rq)` | `Σ w_i·v_i / Σ w_i`. "이상적인 공평 스케줄러라면 모두가 여기 있어야 한다" |
| lag | `se->vlag`, `entity_lag()` | `lag_i = w_i·(V − v_i)`. 양수면 "덜 받았다(빚을 받을 쪽)", 음수면 "더 받았다" |
| eligible | `entity_eligible()` | `lag_i ≥ 0`, 즉 `v_i ≤ V`. 이 태스크만 뽑힐 자격이 있다 |
| virtual deadline `vd_i` | `se->deadline`, `update_deadline()` | `vd_i = v_i + r_i / w_i` (r_i = 요청 slice) |
| slice `r_i` | `se->slice` | 한 번에 요청하는 실행량. 기본 `sysctl_sched_base_slice` |

선택 규칙(`pick_eevdf()`의 주석 그대로): **"1) the task must be eligible (must be owed service) 2) from those tasks that meet 1), we select the one with the earliest virtual deadline."**

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">EEVDF: vruntime 축 위의 네 태스크</text>
  <rect x="80" y="40" width="300" height="220" fill="#3f9a6b" fill-opacity="0.07"/>
  <text x="230" y="56" text-anchor="middle" font-size="12">eligible 영역 (v ≤ V, lag ≥ 0)</text>
  <line x1="380" y1="40" x2="380" y2="268" stroke="#d0564a" stroke-width="2" stroke-dasharray="6 4"/>
  <text x="380" y="284" text-anchor="middle" font-size="13" font-weight="bold">V = avg_vruntime</text>
  <text x="20" y="94" font-size="12">A nice 0</text>
  <circle cx="260" cy="90" r="5" fill="#4a7bd0"/>
  <line x1="260" y1="90" x2="500" y2="90" stroke="#4a7bd0" stroke-width="3"/>
  <line x1="500" y1="82" x2="500" y2="98" stroke="#4a7bd0" stroke-width="2"/>
  <text x="508" y="94" font-size="12">vd_A</text>
  <text x="20" y="134" font-size="12">B nice 1</text>
  <circle cx="320" cy="130" r="5" fill="#4a7bd0"/>
  <line x1="320" y1="130" x2="620" y2="130" stroke="#4a7bd0" stroke-width="3"/>
  <line x1="620" y1="122" x2="620" y2="138" stroke="#4a7bd0" stroke-width="2"/>
  <text x="588" y="118" font-size="12">vd_B</text>
  <text x="20" y="174" font-size="12">C 짧은 slice</text>
  <circle cx="410" cy="170" r="5" fill="#888"/>
  <line x1="410" y1="170" x2="460" y2="170" stroke="#888" stroke-width="3"/>
  <line x1="460" y1="162" x2="460" y2="178" stroke="#888" stroke-width="2"/>
  <text x="468" y="174" font-size="12">vd_C: 가장 이르지만 제외</text>
  <text x="20" y="214" font-size="12">D nice -3</text>
  <circle cx="360" cy="210" r="5" fill="#3f9a6b"/>
  <line x1="360" y1="210" x2="480" y2="210" stroke="#3f9a6b" stroke-width="3"/>
  <line x1="480" y1="202" x2="480" y2="218" stroke="#3f9a6b" stroke-width="2"/>
  <text x="488" y="214" font-size="12" font-weight="bold">vd_D → 선택!</text>
  <line x1="80" y1="250" x2="650" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="600" y="268" font-size="12">vruntime</text>
  <text x="30" y="312" font-size="12">점 = 현재 v_i, 막대 끝 = vd_i = v_i + slice / w_i (weight가 클수록 막대가 짧다)</text>
  <text x="30" y="334" font-size="12">A, B, D는 V 왼쪽 → eligible.  C는 V 오른쪽 → 이미 더 받았으므로 제외</text>
  <text x="30" y="356" font-size="12">eligible 중 vd가 가장 이른 D를 고른다.  C의 차례는 V가 C를 지나간 뒤</text>
  <text x="30" y="378" font-size="12">CFS였다면 v가 가장 작은 A를 골랐을 것이다</text>
</svg>
```

그림 5 — EEVDF의 선택. 가로축은 vruntime. 막대 길이는 `slice/weight`라서 무거운 태스크(D)는 deadline이 가깝고, 짧은 slice를 요청한 태스크(C)도 가깝다. 단 C처럼 이미 몫보다 많이 받은 태스크는 eligible이 아니어서 기다린다.

`fair.c`의 실제 코드 두 조각:

```c
/* kernel/sched/fair.c (6.18.55) — slice를 다 쓰면 deadline을 다시 잡는다 */
static bool update_deadline(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (vruntime_cmp(se->vruntime, "<", se->deadline))
		return false;
	...
	if (!se->custom_slice)
		se->slice = sysctl_sched_base_slice;

	/*
	 * EEVDF: vd_i = ve_i + r_i / w_i
	 */
	se->deadline = se->vruntime + calc_delta_fair(se->slice, se);
	avg_vruntime(cfs_rq);

	/*
	 * The task has consumed its request, reschedule.
	 */
	return true;
}
```

```c
/* pick_eevdf() 일부 — 증강(augmented) rbtree로 O(log n) */
	/* Pick the leftmost entity if it's eligible */
	if (se && entity_eligible(cfs_rq, se)) {
		best = se;
		goto found;
	}
	/* Heap search for the EEVD entity */
	while (node) {
		struct rb_node *left = node->rb_left;
		if (left && vruntime_eligible(cfs_rq, __node_2_se(left)->min_vruntime)) {
			node = left;
			continue;
		}
		...
```

rbtree는 **deadline 순으로 정렬**되어 있고, 각 노드는 서브트리의 최소 vruntime(`se->min_vruntime`)을 들고 있다. 그래서 "왼쪽 서브트리에 eligible한 게 있으면 왼쪽으로" 내려가는 힙 탐색이 된다.

세부 동작 몇 가지(전부 `kernel/sched/features.h`의 기본값 확인):

- **slice 기본값**: `sysctl_sched_base_slice = 0.70 ms × (1 + ilog2(ncpus))`. 랩 게스트(2 CPU)에서 `/sys/kernel/debug/sched/base_slice_ns` = **1400000**(1.4 ms)이었다 — 예제 2의 `se.slice=1400000`과 같다.
- **`RUN_TO_PARITY`**(true): 현재 태스크가 eligible이면 slice를 다 쓸 때까지 지켜 준다(6.18에서는 `se->vprot`라는 "protected" deadline으로 구현). 매 tick마다 바꾸지 않아서 캐시를 덜 버린다.
- **`PREEMPT_SHORT`**(true): 깨어난 태스크가 slice가 더 짧아 deadline이 이르면 현재 태스크의 보호를 깨고 선점한다.
- **`PLACE_LAG`**(true): 잠들 때 lag를 저장(`se->vlag`)했다가 깨어날 때 되살린다. "잠깐 자고 나와서 빚을 탕감받는" 꼼수를 막는다.
- **`DELAY_DEQUEUE`**(true): lag가 음수(더 받은)인 태스크가 잠들면 바로 빼지 않고 lag가 0이 될 때까지 rq에 "지연 dequeue" 상태로 둔다(`se->sched_delayed`). 문서의 "deferred dequeue"가 이것이다.
- 6.18에서 cfs_rq의 기준점 이름이 `min_vruntime`에서 **`zero_vruntime`** 으로 바뀌었다(아래 §11의 debugfs 출력에 `.zero_vruntime`이 보인다). 오래된 블로그의 "min_vruntime" 설명은 CFS 시절 것이다.

### 4.5 slice를 직접 요청하기 (6.12+)

EEVDF는 지연 요구를 표현할 손잡이를 처음으로 줬다: `sched_setattr()`에서 `SCHED_OTHER`인데 `sched_runtime`을 채우면 그 값이 **custom slice**가 된다. 커널은 0.1 ms ~ 100 ms로 자른다(`__setparam_fair()`):

```c
	if (attr->sched_runtime) {
		se->custom_slice = 1;
		se->slice = clamp_t(u64, attr->sched_runtime,
				      NSEC_PER_MSEC/10,   /* HZ=1000 * 10 */
				      NSEC_PER_MSEC*100); /* HZ=100  / 10 */
	}
```

짧은 slice → 가까운 virtual deadline → 깨어났을 때 더 빨리 뽑힌다. 대신 한 번에 짧게 돌고 자주 바뀐다. weight(공평한 몫)는 바뀌지 않는다. §8에서 측정해 본다(효과는 RT만큼 극적이지 않았다).

---

## 5. RT 클래스 — SCHED_FIFO, SCHED_RR, 그리고 throttling

### 5.1 정의

- **SCHED_FIFO**: 우선순위 1~99. 더 높은 RT 태스크가 오거나 스스로 잠들거나 `sched_yield()`할 때까지 **무한히** 돈다. RTOS 태스크와 같다.
- **SCHED_RR**: FIFO + 같은 우선순위끼리 time slice(기본 `sched_rr_timeslice_ms` = **100 ms**, `RR_TIMESLICE = 100 × HZ / 1000`)마다 교대.
- 자료구조: `rt_rq`에 우선순위 100개짜리 리스트 배열 + 비트맵. 가장 높은 비트를 찾아(`sched_find_first_bit`) 그 리스트의 맨 앞. RTOS의 ready list 그대로다.
- SMP: RT는 "가장 높은 우선순위 N개가 CPU N개에서 돈다"를 지키려고 **push/pull**을 한다(높은 RT가 깨어났는데 이 CPU가 더 높은 RT를 돌리고 있으면 다른 CPU로 민다).

### 5.2 RT throttling — 고전 설명과 6.12+의 실제

고전 설명(교재·블로그 대부분): `/proc/sys/kernel/sched_rt_period_us`(1,000,000)와 `sched_rt_runtime_us`(950,000) — **RT 태스크는 1초 중 0.95초까지만** 쓸 수 있고 나머지 0.05초는 비RT에게 남긴다. 폭주한 RT 루프가 시스템을 완전히 얼리는 것을 막는 안전장치다. `-1`을 쓰면 끈다.

6.18 소스를 보면 이야기가 바뀌었다. `kernel/sched/rt.c`에서 RT 태스크를 실제로 멈추는 코드(`sched_rt_runtime_exceeded()`, `rt_throttled`)는 **`#ifdef CONFIG_RT_GROUP_SCHED` 안에만** 있다. 랩 커널처럼 `RT_GROUP_SCHED`가 꺼진 커널(대부분의 배포판·임베디드 기본 설정)에서는 대신 **fair_server**가 일한다:

```c
/* kernel/sched/deadline.c — sched_init_dl_servers() */
	for_each_online_cpu(cpu) {
		u64 runtime =  50 * NSEC_PER_MSEC;
		u64 period = 1000 * NSEC_PER_MSEC;
		...
		dl_se = &rq->fair_server;
		...
		dl_server_apply_params(dl_se, runtime, period, 1);
		dl_se->dl_server = 1;
		dl_se->dl_defer = 1;
```

CPU마다 **deadline 클래스의 가짜 엔티티**(`rq->fair_server`)가 있어서, fair 태스크가 굶고 있으면 1초마다 50 ms를 deadline 클래스 권한으로(즉 RT보다 높게) fair 태스크에게 준다. `dl_defer = 1`은 "바로 주지 말고 **최대한 미뤘다가**(zero-laxity) 준다"는 뜻이다 — RT 태스크가 잠깐씩 도는 정상 상황에서는 RT 지연을 해치지 않으려는 설계다. 6.12에 들어왔다. `Documentation/scheduler/sched-rt-group.rst`도 이제 "`sched_rt_runtime_us/sched_rt_period_us > 0.05` 이어야 fair dl_server 몫이 보존된다"고 적고 있다.

조절 손잡이는 debugfs에 있다: `/sys/kernel/debug/sched/fair_server/cpuN/{runtime,period}`(ns).

### 5.3 예제 5 — SCHED_FIFO hog가 같은 CPU의 SCHED_OTHER를 굶기면

무엇을 확인하나: CPU1에 SCHED_OTHER 일꾼과 SCHED_FIFO 50 hog를 둔다. hog는 0.5 ~ 2.5초 동안 busy loop. 일꾼은 50 ms 구간마다 자기가 받은 CPU 시간(`CLOCK_THREAD_CPUTIME_ID`)을 기록한다. 네 가지 설정으로 돌린다: A 기본, B `sched_rt_runtime_us=-1`(고전적인 "throttling 끄기"), C fair_server runtime 0, D fair_server runtime 200 ms.

```c
/* f1_rtstarve: SCHED_FIFO hog (t=0.5..2.5 s) vs SCHED_OTHER worker, both on CPU 1.
 * The worker records how much CPU it got in each 50 ms bin of wall time. */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

static double now(clockid_t c)
{
	struct timespec ts;
	clock_gettime(c, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void pin1(void)
{
	cpu_set_t s;
	CPU_ZERO(&s); CPU_SET(1, &s);
	if (sched_setaffinity(0, sizeof(s), &s)) { perror("affinity"); _exit(1); }
}

int main(void)
{
	double t0 = now(CLOCK_MONOTONIC) + 0.1;
	if (fork() == 0) {                         /* the RT hog */
		struct sched_param sp = { .sched_priority = 50 };
		pin1();
		while (now(CLOCK_MONOTONIC) < t0 + 0.5) usleep(1000);
		if (sched_setscheduler(0, SCHED_FIFO, &sp)) { perror("FIFO"); _exit(1); }
		while (now(CLOCK_MONOTONIC) < t0 + 2.5) ;
		_exit(0);
	}
	pin1();                                     /* the SCHED_OTHER worker */
	double bin[60] = { 0 }, c_prev, t;
	while (now(CLOCK_MONOTONIC) < t0) ;
	c_prev = now(CLOCK_THREAD_CPUTIME_ID);
	while ((t = now(CLOCK_MONOTONIC) - t0) < 3.0) {
		double c = now(CLOCK_THREAD_CPUTIME_ID);
		bin[(int)(t / 0.05)] += c - c_prev;     /* credit CPU time to this 50 ms bin */
		c_prev = c;
	}
	wait(NULL);
	double during = 0;
	for (int i = 10; i < 50; i++) during += bin[i];
	printf("OTHER CPU during FIFO window (0.5-2.5 s): %.1f ms = %.1f%%\n",
	       during * 1e3, during / 2.0 * 100);
	for (int i = 0; i < 60; i++)
		printf("%s%3.0f", i % 10 ? " " : (i ? "\n" : ""), bin[i] / 0.05 * 100);
	printf("   (%% of each 50 ms bin, 10 bins per line)\n");
	return 0;
}
```

```sh
echo "== rt sysctls"; cat /proc/sys/kernel/sched_rt_period_us /proc/sys/kernel/sched_rt_runtime_us
echo "== fair_server cpu1 (ns)"; cat /sys/kernel/debug/sched/fair_server/cpu1/runtime /sys/kernel/debug/sched/fair_server/cpu1/period
echo "== A: default"; /f1_rtstarve
echo "== B: sched_rt_runtime_us=-1"; echo -1 > /proc/sys/kernel/sched_rt_runtime_us; /f1_rtstarve
echo 950000 > /proc/sys/kernel/sched_rt_runtime_us
echo "== C: fair_server runtime=0"; echo 0 > /sys/kernel/debug/sched/fair_server/cpu1/runtime; /f1_rtstarve
echo "== D: fair_server runtime=200ms"; echo 200000000 > /sys/kernel/debug/sched/fair_server/cpu1/runtime; /f1_rtstarve
```

```text
lab initramfs: 6.18.55-dbg up 27.79s
== rt sysctls
1000000
950000
== fair_server cpu1 (ns)
50000000
1000000000
== A: default
OTHER CPU during FIFO window (0.5-2.5 s): 97.5 ms = 4.9%
 73  73  74  73  72  73  73  74  74  71
  1   0   0   0   0   0   0   0   0   0
  0   0   0   0   0   0   0   0   0  94
  6   0   0   0   0   0   0   0   0   0
  0   0   0   0   0   0   0   0   0  94
 85  95  95  95  95  94  95  95  95  95   (% of each 50 ms bin, 10 bins per line)
== B: sched_rt_runtime_us=-1
OTHER CPU during FIFO window (0.5-2.5 s): 94.1 ms = 4.7%
 73  74  71  73  73  74  74  75  74  73
  0   0   0   0   0   0   0   0   0   0
  0   0   0   0   0   0   0   0   0  95
  5   0   0   0   0   0   0   0   0   0
  0   0   0   0   0   0   0   0   0  89
 86  95  95  95  94  96  94  95  94  95   (% of each 50 ms bin, 10 bins per line)
== C: fair_server runtime=0
OTHER CPU during FIFO window (0.5-2.5 s): 0.2 ms = 0.0%
 70  72  74  74  73  74  74  74  73  73
  0   0   0   0   0   0   0   0   0   0
  0   0   0   0   0   0   0   0   0   0
  0   0   0   0   0   0   0   0   0   0
  0   0   0   0   0   0   0   0   0   0
 82  95  94  95  95  95  94  95  94  95   (% of each 50 ms bin, 10 bins per line)
== D: fair_server runtime=200ms
OTHER CPU during FIFO window (0.5-2.5 s): 399.0 ms = 20.0%
 73  75  75  74  73  74  74  74  73  71
  1   0   0   0   0   0   0   0   0   0
  0   0   0   0   0  40  96  95  95  74
  0   0   0   0   0   0   0   0   0   0
  0   0   0   0   0  18  95  95  95  94
 86  95  95  94  95  95  95  95  94  95   (% of each 50 ms bin, 10 bins per line)
```

(`-icount shift=3`로 실행. 줄 하나 = 0.5초, 숫자 하나 = 50 ms 구간에서 일꾼이 받은 CPU %.)

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<text x="330.0" y="22" text-anchor="middle" font-size="14" font-weight="bold">CPU1의 SCHED_OTHER 작업이 받은 CPU (50 ms 구간별)</text>
<rect x="156.7" y="50" width="386.7" height="210" fill="#d0564a" fill-opacity="0.08" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="350.0" y="66" text-anchor="middle" font-size="12">SCHED_FIFO hog 실행 구간 (0.5 ~ 2.5 s)</text>
<line x1="60" y1="260.0" x2="640" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="264.0" text-anchor="end" font-size="12">0%</text>
<line x1="60" y1="155.0" x2="640" y2="155.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="159.0" text-anchor="end" font-size="12">50%</text>
<line x1="60" y1="50.0" x2="640" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="54.0" text-anchor="end" font-size="12">100%</text>
<text x="60.0" y="278" text-anchor="middle" font-size="12">0 s</text>
<text x="156.7" y="278" text-anchor="middle" font-size="12">0.5 s</text>
<text x="253.3" y="278" text-anchor="middle" font-size="12">1 s</text>
<text x="350.0" y="278" text-anchor="middle" font-size="12">1.5 s</text>
<text x="446.7" y="278" text-anchor="middle" font-size="12">2 s</text>
<text x="543.3" y="278" text-anchor="middle" font-size="12">2.5 s</text>
<text x="640.0" y="278" text-anchor="middle" font-size="12">3 s</text>
<line x1="60" y1="260" x2="640" y2="260" stroke="currentColor"/>
<line x1="60" y1="50" x2="60" y2="260" stroke="currentColor"/>
<polyline points="64.8,106.7 74.5,102.5 84.2,102.5 93.8,104.6 103.5,106.7 113.2,104.6 122.8,104.6 132.5,104.6 142.2,106.7 151.8,110.9 161.5,257.9 171.2,260.0 180.8,260.0 190.5,260.0 200.2,260.0 209.8,260.0 219.5,260.0 229.2,260.0 238.8,260.0 248.5,260.0 258.2,260.0 267.8,260.0 277.5,260.0 287.2,260.0 296.8,260.0 306.5,176.0 316.2,58.4 325.8,60.5 335.5,60.5 345.2,104.6 354.8,260.0 364.5,260.0 374.2,260.0 383.8,260.0 393.5,260.0 403.2,260.0 412.8,260.0 422.5,260.0 432.2,260.0 441.8,260.0 451.5,260.0 461.2,260.0 470.8,260.0 480.5,260.0 490.2,260.0 499.8,222.2 509.5,60.5 519.2,60.5 528.8,60.5 538.5,62.6 548.2,79.4 557.8,60.5 567.5,60.5 577.2,62.6 586.8,60.5 596.5,60.5 606.2,60.5 615.8,60.5 625.5,62.6 635.2,60.5" fill="none" stroke="#e08a3c" stroke-width="2"/>
<polyline points="64.8,106.7 74.5,106.7 84.2,104.6 93.8,106.7 103.5,108.8 113.2,106.7 122.8,106.7 132.5,104.6 142.2,104.6 151.8,110.9 161.5,257.9 171.2,260.0 180.8,260.0 190.5,260.0 200.2,260.0 209.8,260.0 219.5,260.0 229.2,260.0 238.8,260.0 248.5,260.0 258.2,260.0 267.8,260.0 277.5,260.0 287.2,260.0 296.8,260.0 306.5,260.0 316.2,260.0 325.8,260.0 335.5,260.0 345.2,62.6 354.8,247.4 364.5,260.0 374.2,260.0 383.8,260.0 393.5,260.0 403.2,260.0 412.8,260.0 422.5,260.0 432.2,260.0 441.8,260.0 451.5,260.0 461.2,260.0 470.8,260.0 480.5,260.0 490.2,260.0 499.8,260.0 509.5,260.0 519.2,260.0 528.8,260.0 538.5,62.6 548.2,81.5 557.8,60.5 567.5,60.5 577.2,60.5 586.8,60.5 596.5,62.6 606.2,60.5 615.8,60.5 625.5,60.5 635.2,60.5" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<line x1="60" y1="300" x2="85" y2="300" stroke="#4a7bd0" stroke-width="2"/><text x="92" y="304" font-size="13">기본 fair_server 50 ms / 1 s → 4.9%</text>
<line x1="400" y1="300" x2="425" y2="300" stroke="#e08a3c" stroke-width="2"/><text x="432" y="304" font-size="13">runtime 200 ms → 20.0%</text>
</svg>
```

그림 6 — A(기본)와 D(fair_server 200 ms)의 시간축. 빨간 점선 구간에서 FIFO hog가 CPU1을 잡고 있다.

출력에서 볼 것:

- **A: 2초 동안 일꾼이 받은 CPU는 97.5 ms = 4.9%** — 1초에 50 ms. 그런데 그 50 ms가 **FIFO가 시작된 지 약 0.95초 뒤에 한 덩어리로** 온다(1.45 ~ 1.5초, 2.45 ~ 2.5초 구간의 94%). 이것이 `dl_defer`: 굶기 시작한 시점부터 period 1초를 세고, 끝에서 runtime 50 ms만큼 남은 "더는 못 미루는" 순간(zero-laxity)에 몰아서 준다. **고전적인 "매초 마지막 50 ms" 설명과 결과는 비슷하지만 메커니즘이 다르다.**
- **B: `sched_rt_runtime_us=-1`을 써도 결과가 같다(4.7%).** `RT_GROUP_SCHED`가 없는 6.12+ 커널에서 이 sysctl은 deadline 대역폭 검사(admission)에만 쓰이고 fair_server를 끄지 않는다. 오래된 RT 튜닝 가이드의 "throttling 끄기 = `echo -1`"은 이 커널에서 효과가 없다.
- **C: fair_server runtime을 0으로 하면 정말로 0.0%** — RT 폭주 시 셸·sshd·워치독 데몬까지 완전히 굶는다. 실험용이 아니면 하지 말 것.
- **D: runtime 200 ms → 정확히 20.0%**, 덩어리는 FIFO 시작 약 0.8초 뒤(= 1000 − 200 ms)에 온다.
- 첫 0.5초가 100%가 아니라 73%인 것은 hog가 시작을 기다리며 `usleep(1000)`으로 1 ms마다 깨어나 일꾼을 선점하고, TCG에서 그 깨어남 처리 비용이 크기 때문이다. 마지막 구간의 95%는 timer IRQ 처리 시간이 빠진 것이다.

> 실무 번역: 센서 루프를 SCHED_FIFO로 올렸는데 무한 루프 버그가 나면, 6.12+ 기본 설정에서는 **다른 프로세스가 매초 50 ms씩만** 숨을 쉰다 — ssh 로그인은 되지만 극도로 느리다. 반대로 RT 루프가 50 ms 이상 "실제로 필요"한 시스템이라면 1초마다 한 번 fair 태스크가 끼어들 수 있다는 것을 알고 설계해야 한다.

---

## 6. SCHED_DEADLINE — "주기마다 이만큼" 예약

### 6.1 정의

RTOS의 고정 우선순위는 "누가 더 중요한가"만 말한다. SCHED_DEADLINE은 **"period마다 runtime만큼, 매 주기 시작 후 deadline 안에"** 를 요청한다. 커널은 EDF(Earliest Deadline First, 절대 deadline이 가장 이른 태스크를 먼저)로 돌리고, CBS(Constant Bandwidth Server)로 각 태스크가 예약을 넘어 쓰지 못하게 막는다(runtime을 다 쓰면 다음 주기까지 throttle).

- 파라미터: `sched_runtime ≤ sched_deadline ≤ sched_period` (ns). `struct sched_attr`로 `sched_setattr()`.
- **admission control**: 새 예약이 들어올 때 root domain 전체의 `Σ runtime/period`가 허용 대역폭(기본 CPU당 95% × CPU 수)을 넘으면 **`EBUSY`로 거부**한다. RTOS에서 Don이 손으로 하던 "CPU 사용률 합 < 한계" 계산(RMA)을 커널이 대신 해 주는 셈이다.
- deadline 태스크는 affinity를 root domain보다 좁힐 수 없다(`sched_setaffinity`가 `EBUSY`). 대역폭 검사가 root domain 단위라서다. CPU를 나누려면 cpuset으로 root domain 자체를 나눈다.
- deadline 태스크가 `fork()`하면 **`EAGAIN`으로 실패**한다(`sched_fork()`의 `if (dl_prio(p->prio)) return -EAGAIN;`). `SCHED_FLAG_RESET_ON_FORK`를 주면 자식은 SCHED_NORMAL로 시작한다. 예약은 언제나 명시적으로 잡는다.

### 6.2 예제 6 — 2 ms / 10 ms 예약은 부하 속에서도 20%를 받나

무엇을 확인하나: 두 CPU 모두에 SCHED_OTHER hog 2개씩을 돌리면서, 2 ms/10 ms 예약을 한 greedy 루프가 실제로 받은 CPU를 잰다. `secs = 0`이면 예약만 잡고 2초 잠든다(admission 실험용). glibc 버전에 따라 `<sched.h>`에 `struct sched_attr`가 있을 수도 없을 수도 있어서, 같은 레이아웃의 구조체를 다른 이름으로 직접 정의하고 `syscall()`로 부른다.

```c
/* f1_dl: SCHED_DEADLINE (runtime/deadline/period in us) under CPU load.
 * usage: f1_dl <runtime_us> <period_us> <secs>  -> CPU share actually received
 *        secs = 0: just hold the reservation (sleep 2 s) - for admission tests */
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

#ifndef SCHED_DEADLINE
#define SCHED_DEADLINE 6
#endif
struct f1_attr { uint32_t size, policy; uint64_t flags; int32_t nice; uint32_t prio;
		 uint64_t runtime, deadline, period; };      /* = struct sched_attr (v1) */

static double now(clockid_t c)
{
	struct timespec ts;
	clock_gettime(c, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: %s runtime_us period_us secs\n", argv[0]); exit(1); }
	struct f1_attr a = { .size = sizeof(a), .policy = SCHED_DEADLINE,
			     .runtime  = strtoull(argv[1], NULL, 0) * 1000,
			     .deadline = strtoull(argv[2], NULL, 0) * 1000,
			     .period   = strtoull(argv[2], NULL, 0) * 1000 };
	if (syscall(SYS_sched_setattr, 0, &a, 0)) {
		printf("sched_setattr(DEADLINE %s/%s us) failed: %s\n", argv[1], argv[2], strerror(errno));
		return 1;
	}
	struct f1_attr g;
	if (syscall(SYS_sched_getattr, 0, &g, sizeof(g), 0)) { perror("getattr"); exit(1); }
	printf("policy=%u runtime=%llu deadline=%llu period=%llu ns\n", g.policy,
	       (unsigned long long)g.runtime, (unsigned long long)g.deadline,
	       (unsigned long long)g.period);
	if (atof(argv[3]) == 0) { sleep(2); return 0; }   /* sleeping keeps the bandwidth */
	double secs = atof(argv[3]), t0 = now(CLOCK_MONOTONIC), c0 = now(CLOCK_THREAD_CPUTIME_ID);
	while (now(CLOCK_MONOTONIC) < t0 + secs) ;     /* greedy: wants 100% */
	double cpu = now(CLOCK_THREAD_CPUTIME_ID) - c0;
	printf("got %.3f s CPU in %.1f s = %.1f%% (reserved %.1f%%)\n", cpu, secs,
	       100 * cpu / secs, 100.0 * a.runtime / a.period);
	return 0;
}
```

```sh
for i in 1 2 3 4; do /f1_spin 30 & done
sleep 0.5
/f1_dl 2000 10000 2
```

```text
== SCHED_DEADLINE 2ms/10ms with 2 hogs per CPU
policy=6 runtime=2000000 deadline=10000000 period=10000000 ns
got 0.402 s CPU in 2.0 s = 20.1% (reserved 20.0%)
```

출력에서 볼 것: 예약 20%, 받은 것 20.1%. **busy loop인데도 20%에서 멈춘다** — CBS가 runtime 2 ms를 다 쓰면 다음 주기까지 throttle하기 때문이다. 동시에 hog들이 아무리 많아도 20%는 보장된다(deadline 클래스가 fair보다 위).

`f1_spin`은 그냥 지정한 초 동안 도는 SCHED_OTHER 부하다:

```c
/* f1_spin: burn CPU for <secs> seconds (a plain SCHED_OTHER hog) */
#include <stdlib.h>
#include <time.h>
int main(int argc, char **argv)
{
	struct timespec a, b;
	double secs = argc > 1 ? atof(argv[1]) : 5;
	clock_gettime(CLOCK_MONOTONIC, &a);
	do clock_gettime(CLOCK_MONOTONIC, &b);
	while ((b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9 < secs);
	return 0;
}
```

### 6.3 예제 7 — admission control: 90% 예약 세 개

무엇을 확인하나: 900 µs / 1000 µs(90%) 예약을 세 프로세스가 차례로 요청한다(예약을 잡은 뒤 잠들어서 CPU는 쓰지 않는다). 2 CPU × 95% = 1.9인데 fair_server가 이미 CPU당 5%를 예약해 두었다.

```sh
/f1_dl 900 1000 0 & sleep 0.2; /f1_dl 900 1000 0 & sleep 0.2; /f1_dl 900 1000 0; wait
/f1_dl 900 1000 0 & /f1_dl 900 1000 0 & sleep 0.5; grep -A3 "^dl_rq\[0\]" /sys/kernel/debug/sched/debug; wait
```

```text
== admission control: three tasks asking 900us/1000us each (held 2 s)
policy=6 runtime=900000 deadline=1000000 period=1000000 ns
policy=6 runtime=900000 deadline=1000000 period=1000000 ns
sched_setattr(DEADLINE 900/1000 us) failed: Device or resource busy
== while two reservations are held
policy=6 runtime=900000 deadline=1000000 period=1000000 ns
policy=6 runtime=900000 deadline=1000000 period=1000000 ns
dl_rq[0]:
  .dl_nr_running                 : 0
  .dl_bw->bw                     : 996147
  .dl_bw->total_bw               : 1992292
```

출력에서 볼 것: 세 번째는 **`EBUSY`**. 대역폭은 2^20 = 1,048,576을 1.0으로 하는 고정소수점이다. `bw = 996147` = 0.95(CPU당 한계). `total_bw = 1992292` = fair_server 2개(0.05 × 2 = 104,856) + 예약 2개(0.9 × 2 = 1,887,436). 세 번째 0.9를 더하면 2.8 > 1.9 → 거부. (참고: 이 실험을 처음엔 예약 태스크가 busy loop를 돌게 했더니, 두 예약이 CPU 두 개를 90%씩 먹는 바람에 셸이 `sleep 0.2`에서 깨어나지 못하고 첫 태스크가 끝난 뒤에야 세 번째를 실행해서 세 개 다 성공한 것처럼 보였다. 예약을 잡고 잠드는 버전으로 바꾼 이유다.)

---

## 7. 언제 `schedule()`이 불리나 — need_resched와 preemption 모델

### 7.1 직관 — "바꿔야 한다"와 "지금 바꾼다"는 다른 일

RTOS에서 ISR이 높은 우선순위 태스크를 깨우면 `portYIELD_FROM_ISR()` → PendSV 펜딩 → ISR이 끝나는 순간 전환. Linux도 두 단계로 나뉜다:

1. **표시(mark)**: 누군가 "지금 도는 태스크를 바꿔야 한다"고 판단하면 `resched_curr(rq)` → 현재 태스크의 `thread_info.flags`에 **`TIF_NEED_RESCHED`** 비트를 세운다(다른 CPU라면 IPI를 보낸다). 아무것도 바로 바뀌지 않는다. PendSV를 펜딩한 것과 같다.
2. **실행(act)**: 커널이 정해진 **선점 지점(preemption point)** 을 지날 때 그 비트를 보고 `schedule()`을 부른다.

`kernel/sched/core.c`의 `__schedule()` 위 주석이 정리해 둔 목록:

| 경로 | 무엇 | 예 |
|---|---|---|
| 명시적 blocking | 태스크 스스로 잠듦 → `schedule()` | mutex, `wait_event`, `msleep`, 블로킹 `read()` |
| tick | `sched_tick()` → `task_tick` 콜백이 slice 소진·RR 만료를 보고 `resched_curr()` | fair의 `update_deadline()`이 true |
| wakeup | `try_to_wake_up()` → `wakeup_preempt()`가 깨어난 쪽이 더 급하면 `resched_curr()` | RT 깨움, EEVDF의 PREEMPT_SHORT |
| 유저로 돌아갈 때 | `exit_to_user_mode_loop()`에서 `_TIF_NEED_RESCHED` 확인 → `schedule()` | syscall 리턴, IRQ 리턴(유저 모드였을 때) |
| 커널 안에서 IRQ 리턴 | `irqentry_exit()` → `preempt_schedule_irq()` (**CONFIG_PREEMPTION일 때만**) | 커널 코드 실행 중 타이머 IRQ |
| `preempt_enable()` | preempt count가 0이 되는 순간 비트를 확인 → `preempt_schedule()` (**PREEMPTION일 때만**) | `spin_unlock()` 직후 |
| `cond_resched()` | 긴 커널 루프 안의 명시적 양보 지점 (**PREEMPTION이 없을 때 의미**) | 파일시스템·메모리 회수 루프 |

```c
/* kernel/entry/common.c — 유저 공간으로 돌아가기 직전 */
	while (ti_work & EXIT_TO_USER_MODE_WORK) {
		local_irq_enable_exit_to_user(ti_work);
		if (ti_work & (_TIF_NEED_RESCHED | _TIF_NEED_RESCHED_LAZY))
			schedule();
		...
		if (ti_work & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL))
			arch_do_signal_or_restart(regs);
```

유저 코드는 **언제나** 선점된다(IRQ가 오면 유저로 돌아가기 전에 위 루프를 지나므로). preemption 모델이 바꾸는 것은 **커널 코드를 실행하던 중** 언제 끊을 수 있느냐다.

### 7.2 preemption 모델 다섯 가지 (6.18 `kernel/Kconfig.preempt`)

```text
choice
	prompt "Preemption Model"
	default PREEMPT_NONE
config PREEMPT_NONE        "No Forced Preemption (Server)"
config PREEMPT_VOLUNTARY   "Voluntary Kernel Preemption (Desktop)"
config PREEMPT             "Preemptible Kernel (Low-Latency Desktop)"
config PREEMPT_LAZY        "Scheduler controlled preemption model"   depends on ARCH_HAS_PREEMPT_LAZY
endchoice
config PREEMPT_RT          "Fully Preemptible Kernel (Real-Time)"     depends on EXPERT && ARCH_SUPPORTS_RT
config PREEMPT_DYNAMIC     "Preemption behaviour defined on boot"
```

(위 블록은 Kconfig 파일에서 prompt 줄만 모아 정리한 것이다.)

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">커널 안에서 긴 syscall 실행 중, RT 태스크가 깨어나면 언제 바뀌나</text>
  <line x1="250" y1="40" x2="250" y2="300" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 4"/>
  <text x="250" y="316" text-anchor="middle" font-size="12">RT 깨어남 (IRQ)</text>
  <line x1="330" y1="44" x2="330" y2="296" stroke="currentColor" stroke-opacity="0.35"/>
  <text x="330" y="334" text-anchor="middle" font-size="12">cond_resched()</text>
  <rect x="380" y="44" width="60" height="252" fill="#e08a3c" fill-opacity="0.10"/>
  <text x="410" y="316" text-anchor="middle" font-size="12">spinlock</text>
  <line x1="600" y1="44" x2="600" y2="296" stroke="currentColor" stroke-opacity="0.35"/>
  <text x="600" y="316" text-anchor="middle" font-size="12">유저로 복귀</text>
  <text x="20" y="74" font-size="13">NONE</text>
  <rect x="150" y="60" width="450" height="22" fill="#888" fill-opacity="0.35"/>
  <rect x="600" y="60" width="50" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="20" y="124" font-size="13">VOLUNTARY</text>
  <rect x="150" y="110" width="180" height="22" fill="#888" fill-opacity="0.35"/>
  <rect x="330" y="110" width="320" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="20" y="174" font-size="13">PREEMPT (full)</text>
  <rect x="150" y="160" width="102" height="22" fill="#888" fill-opacity="0.35"/>
  <rect x="252" y="160" width="398" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="20" y="224" font-size="13">LAZY</text>
  <rect x="150" y="210" width="102" height="22" fill="#888" fill-opacity="0.35"/>
  <rect x="252" y="210" width="398" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="20" y="274" font-size="13">PREEMPT_RT</text>
  <rect x="150" y="260" width="102" height="22" fill="#888" fill-opacity="0.35"/>
  <rect x="252" y="260" width="398" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <rect x="150" y="340" width="14" height="12" fill="#888" fill-opacity="0.35"/>
  <text x="170" y="351" font-size="12">낮은 우선순위 태스크 (커널 코드)</text>
  <rect x="420" y="340" width="14" height="12" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="440" y="351" font-size="12">RT 태스크</text>
</svg>
```

그림 7 — 같은 상황(낮은 우선순위 태스크가 긴 syscall을 커널 안에서 처리 중, 250 지점에서 RT 태스크 깨어남)에서 모델별 전환 시점. 깨어난 시점이 주황 spinlock 구간 안이었다면 PREEMPT(full)와 LAZY도 `spin_unlock()`까지 기다리고, PREEMPT_RT만 즉시 바꿀 수 있다(RT 커널에서 spinlock은 잠들 수 있는 rt_mutex 기반 락이 된다).

| 모델 | 커널 코드 선점 | 특징 |
|---|---|---|
| `PREEMPT_NONE` | 안 함. 유저 복귀·스스로 잠들 때만 | 처리량 최대. 지연 보장 없음. 서버 |
| `PREEMPT_VOLUNTARY` | `might_sleep()`/`cond_resched()` 지점에서만 | 데스크톱 배포판의 오래된 기본값 |
| `PREEMPT` (full) | preempt count가 0인 어디서든 (IRQ 리턴, `preempt_enable()`) | ms 단위 지연 요구. **랩 커널, arm64 defconfig** |
| `PREEMPT_LAZY` (6.13+) | RT·DL 깨움은 full처럼 즉시. fair 태스크끼리는 `TIF_NEED_RESCHED_LAZY`만 세우고 tick이나 유저 복귀까지 미룸 | 락 쥔 태스크 선점을 줄여 처리량 회복. arm64는 `ARCH_HAS_PREEMPT_LAZY` 선택 |
| `PREEMPT_RT` (6.12에 mainline 편입) | spinlock 안에서도 대부분 선점. IRQ 핸들러가 스레드로 돎 | hard RT에 가까움. N1에서 자세히 |

`PREEMPT_DYNAMIC`은 커널 커맨드라인 `preempt=none/voluntary/full/lazy`로 부팅 때 고르게 해 준다. arm64는 `HAVE_PREEMPT_DYNAMIC_KEY`를 갖지만 Kconfig 기본값이 `HAVE_PREEMPT_DYNAMIC_CALL`에만 y라서, 랩 커널에서는 꺼져 있다(그래서 `/sys/kernel/debug/sched/preempt` 파일도 없다 — 실제로 `cat`이 실패했다).

> preempt count: `preempt_disable()`, `spin_lock()`, `local_bh_disable()`, hardirq/softirq 진입이 모두 `current`의 preempt count를 올린다. 0이 아닐 때 선점하지 않는다. E4 §5의 "scheduling while atomic"이 바로 이 카운트가 0이 아닌데 `schedule()`에 들어간 경우다. F2에서 자세히.

---

## 8. 깨어남 지연 측정 — SCHED_OTHER vs SCHED_FIFO

### 8.1 예제 8 — `clock_nanosleep(TIMER_ABSTIME)` 주기 루프의 지연 히스토그램

무엇을 확인하나: 5 ms 주기로 절대 시각까지 자고 깨어나서 "목표 시각보다 얼마나 늦었나"를 1000번 잰다(cyclictest의 핵심과 같다, N3). 같은 CPU에 부하를 깔고 정책만 바꾼다. 음수 인자는 §4.5의 custom slice를 요청한다. `mlockall()`로 page fault도 막는다.

```c
/* f1_lat: periodic wakeups with clock_nanosleep(TIMER_ABSTIME), 5 ms period;
 * usage: f1_lat <prio> <loops>  -> latency histogram (us)
 *   prio > 0: SCHED_FIFO prio,  0: SCHED_OTHER,  -N: SCHED_OTHER with custom slice N us */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>

struct f1_attr { uint32_t size, policy; uint64_t flags; int32_t nice; uint32_t prio;
		 uint64_t runtime, deadline, period; };      /* = struct sched_attr (v1) */

#define PERIOD_NS 5000000L
static const int edge[] = { 100, 200, 500, 1000, 2000, 5000, 10000, 1000000000 };
#define NB (int)(sizeof(edge) / sizeof(edge[0]))

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s prio loops\n", argv[0]); exit(1); }
	int prio = atoi(argv[1]), loops = atoi(argv[2]);
	cpu_set_t s;
	CPU_ZERO(&s); CPU_SET(1, &s);
	if (sched_setaffinity(0, sizeof(s), &s)) { perror("affinity"); exit(1); }
	if (prio > 0) {
		struct sched_param sp = { .sched_priority = prio };
		if (sched_setscheduler(0, SCHED_FIFO, &sp)) { perror("FIFO"); exit(1); }
	} else if (prio < 0) {     /* EEVDF: request a shorter slice (6.12+) */
		struct f1_attr a = { .size = sizeof(a), .policy = SCHED_OTHER,
				     .runtime = (uint64_t)-prio * 1000 };
		if (syscall(SYS_sched_setattr, 0, &a, 0)) { perror("setattr"); exit(1); }
	}
	if (mlockall(MCL_CURRENT | MCL_FUTURE)) { perror("mlockall"); exit(1); }
	long hist[NB] = { 0 }, max = 0, sum = 0;
	struct timespec next, t;
	clock_gettime(CLOCK_MONOTONIC, &next);
	for (int i = 0; i < loops; i++) {
		next.tv_nsec += PERIOD_NS;
		if (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
		clock_gettime(CLOCK_MONOTONIC, &t);
		long us = ((t.tv_sec - next.tv_sec) * 1000000000L + t.tv_nsec - next.tv_nsec) / 1000;
		int b = 0;
		while (us >= edge[b]) b++;
		hist[b]++; sum += us;
		if (us > max) max = us;
		if (us * 1000 > PERIOD_NS) next = t;   /* overrun: re-sync, do not pile up */
	}
	printf("%s%.0d: n=%d avg=%ld us max=%ld us\n",
	       prio > 0 ? "FIFO" : prio ? "OTHER slice(us)=" : "OTHER", prio < 0 ? -prio : 0,
	       loops, sum / loops, max);
	for (int b = 0; b < NB; b++)
		printf("  <%-7d us : %ld\n", edge[b] < 1000000000 ? edge[b] : 99999, hist[b]);
	return 0;
}
```

시나리오 1 — 부하 없음 vs CPU1에 **nice -10 hog 4개**(무거운 fair 태스크):

```sh
echo "== idle system"; /f1_lat 0 1000; /f1_lat 80 1000
echo "== 4 hogs at nice -10 pinned to CPU1"
for i in 1 2 3 4; do taskset -c 1 nice -n -10 /f1_spin 60 & done
sleep 1
/f1_lat 0 1000
/f1_lat 80 1000
```

```text
lab initramfs: 6.18.55-dbg up 27.88s
== idle system
OTHER: n=1000 avg=658 us max=5961 us
  <100     us : 0
  <200     us : 0
  <500     us : 543
  <1000    us : 366
  <2000    us : 60
  <5000    us : 30
  <10000   us : 1
  <99999   us : 0
FIFO: n=1000 avg=691 us max=7585 us
  <100     us : 0
  <200     us : 0
  <500     us : 507
  <1000    us : 322
  <2000    us : 150
  <5000    us : 19
  <10000   us : 2
  <99999   us : 0
== 4 hogs at nice -10 pinned to CPU1
OTHER: n=1000 avg=6291 us max=120111 us
  <100     us : 0
  <200     us : 1
  <500     us : 215
  <1000    us : 1
  <2000    us : 23
  <5000    us : 179
  <10000   us : 320
  <99999   us : 261
FIFO: n=1000 avg=305 us max=835 us
  <100     us : 0
  <200     us : 0
  <500     us : 935
  <1000    us : 65
  <2000    us : 0
  <5000    us : 0
  <10000   us : 0
  <99999   us : 0
```

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
<text x="330.0" y="22" text-anchor="middle" font-size="14" font-weight="bold">5 ms 주기 깨어남 지연 분포 (nice -10 hog 4개와 같은 CPU, 1000회)</text>
<line x1="60" y1="260.0" x2="640" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="264.0" text-anchor="end" font-size="12">0</text>
<line x1="60" y1="206.2" x2="640" y2="206.2" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="210.2" text-anchor="end" font-size="12">250</text>
<line x1="60" y1="152.5" x2="640" y2="152.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="156.5" text-anchor="end" font-size="12">500</text>
<line x1="60" y1="98.8" x2="640" y2="98.8" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="102.8" text-anchor="end" font-size="12">750</text>
<line x1="60" y1="45.0" x2="640" y2="45.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="49.0" text-anchor="end" font-size="12">1000</text>
<line x1="60" y1="260" x2="640" y2="260" stroke="currentColor"/>
<line x1="60" y1="45" x2="60" y2="260" stroke="currentColor"/>
<rect x="68.2" y="260.0" width="26" height="0.0" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="97.2" y="260.0" width="26" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="96.2" y="278" text-anchor="middle" font-size="12">&lt;0.1</text>
<rect x="140.8" y="259.8" width="26" height="0.2" fill="#e08a3c" fill-opacity="0.75"/>
<text x="153.8" y="255.8" text-anchor="middle" font-size="12">1</text>
<rect x="169.8" y="260.0" width="26" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="168.8" y="278" text-anchor="middle" font-size="12">0.1-0.2</text>
<rect x="213.2" y="213.8" width="26" height="46.2" fill="#e08a3c" fill-opacity="0.75"/>
<text x="226.2" y="209.8" text-anchor="middle" font-size="12">215</text>
<rect x="242.2" y="59.0" width="26" height="201.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="255.2" y="55.0" text-anchor="middle" font-size="12">935</text>
<text x="241.2" y="278" text-anchor="middle" font-size="12">0.2-0.5</text>
<rect x="285.8" y="259.8" width="26" height="0.2" fill="#e08a3c" fill-opacity="0.75"/>
<text x="298.8" y="255.8" text-anchor="middle" font-size="12">1</text>
<rect x="314.8" y="246.0" width="26" height="14.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="327.8" y="242.0" text-anchor="middle" font-size="12">65</text>
<text x="313.8" y="278" text-anchor="middle" font-size="12">0.5-1</text>
<rect x="358.2" y="255.1" width="26" height="4.9" fill="#e08a3c" fill-opacity="0.75"/>
<text x="371.2" y="251.1" text-anchor="middle" font-size="12">23</text>
<rect x="387.2" y="260.0" width="26" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="386.2" y="278" text-anchor="middle" font-size="12">1-2</text>
<rect x="430.8" y="221.5" width="26" height="38.5" fill="#e08a3c" fill-opacity="0.75"/>
<text x="443.8" y="217.5" text-anchor="middle" font-size="12">179</text>
<rect x="459.8" y="260.0" width="26" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="458.8" y="278" text-anchor="middle" font-size="12">2-5</text>
<rect x="503.2" y="191.2" width="26" height="68.8" fill="#e08a3c" fill-opacity="0.75"/>
<text x="516.2" y="187.2" text-anchor="middle" font-size="12">320</text>
<rect x="532.2" y="260.0" width="26" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="531.2" y="278" text-anchor="middle" font-size="12">5-10</text>
<rect x="575.8" y="203.9" width="26" height="56.1" fill="#e08a3c" fill-opacity="0.75"/>
<text x="588.8" y="199.9" text-anchor="middle" font-size="12">261</text>
<rect x="604.8" y="260.0" width="26" height="0.0" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="603.8" y="278" text-anchor="middle" font-size="12">≥10</text>
<text x="350.0" y="296" text-anchor="middle" font-size="12">지연 구간 (ms)</text>
<rect x="70" y="314" width="14" height="14" fill="#e08a3c" fill-opacity="0.75"/><text x="90" y="326" font-size="13">SCHED_OTHER: 평균 6.3 ms, 최대 120 ms</text>
<rect x="390" y="314" width="14" height="14" fill="#4a7bd0" fill-opacity="0.75"/><text x="410" y="326" font-size="13">SCHED_FIFO 80: 최대 0.84 ms</text>
</svg>
```

그림 8 — 부하(nice -10 hog 4개) 아래 깨어남 지연 분포(`-icount shift=3`, 1000회). SCHED_OTHER는 26%가 10 ms 이상 늦었고, SCHED_FIFO는 전부 1 ms 안이다.

출력에서 볼 것:

- **부하가 없으면 정책은 상관없다**(둘 다 평균 약 0.66~0.69 ms). 이 바닥값 0.5 ms 전후는 icount 모드의 느린 가상 CPU와 디버그 커널(lockdep 등) 비용이다. 실제 Cortex-A53/A72 보드에서는 수십 µs가 정상이다. 오히려 idle일 때 꼬리(최대 6~7 ms)가 부하 FIFO보다 긴데, CPU가 idle(WFI)에서 깨어나는 경로와 에뮬레이터의 idle 처리 비용으로 보인다(이 노트에서 원인까지 추적하지는 않았다).
- **부하 아래 SCHED_OTHER는 최대 120 ms**. nice -10 hog(weight 9548)의 slice/weight는 nice 0 태스크(1024)의 약 1/9.3이라 hog들의 virtual deadline이 훨씬 가깝다. 깨어난 태스크가 eligible이어도 deadline 경쟁에서 지므로 hog들이 한 바퀴 도는 동안 기다린다.
- **SCHED_FIFO는 최대 0.84 ms, 평균 0.3 ms** — idle 때보다도 낫다(CPU가 hog로 바빠서 WFI 탈출 비용이 없다). RT는 깨어나는 즉시 `wakeup_preempt` → `resched_curr` → IRQ 리턴에서 전환이다. fair 태스크가 몇 개, 얼마나 무겁든 상관없다.

시나리오 2 — CPU1에 **nice 0 hog 4개**, 정책 세 가지(OTHER 기본 / OTHER slice 100 µs / FIFO):

```sh
for i in 1 2 3 4; do taskset -c 1 /f1_spin 60 & done
sleep 1
/f1_lat 0 600; /f1_lat -100 600; /f1_lat 80 600
```

```text
== 4 nice-0 hogs pinned to CPU1
OTHER: n=600 avg=1068 us max=4172 us
  <100     us : 0
  <200     us : 0
  <500     us : 431
  <1000    us : 35
  <2000    us : 2
  <5000    us : 132
  <10000   us : 0
  <99999   us : 0
OTHER slice(us)=100: n=600 avg=925 us max=4913 us
  <100     us : 0
  <200     us : 0
  <500     us : 500
  <1000    us : 1
  <2000    us : 0
  <5000    us : 99
  <10000   us : 0
  <99999   us : 0
FIFO: n=600 avg=319 us max=1996 us
  <100     us : 0
  <200     us : 0
  <500     us : 559
  <1000    us : 39
  <2000    us : 2
  <5000    us : 0
  <10000   us : 0
  <99999   us : 0
```

출력에서 볼 것: 같은 weight의 hog라면 SCHED_OTHER도 대부분 0.5 ms 안에 깨어난다 — 오래 잔 태스크는 lag가 0 이상이라 eligible이고, deadline도 hog들보다 가까운 경우가 많아 깨어나자마자 선점한다(EEVDF의 wakeup preemption). 그래도 **22%(132/600)는 2~5 ms 꼬리**가 있다. custom slice 100 µs는 그 꼬리를 99/600으로 조금 줄였을 뿐이다 — 이 실험에서는 극적인 효과가 없었다. **꼬리를 없앤 것은 SCHED_FIFO뿐**이다. 결론: "지연에 민감하면 RT 정책"이라는 실무 규칙은 EEVDF에서도 그대로다. custom slice는 RT 권한 없이 쓸 수 있는 약한 손잡이다.

---

## 9. ftrace로 실제 전환 보기

디버그 커널에는 ftrace가 켜져 있다. tracefs는 자동 마운트가 안 되므로 직접 마운트한다. 사용 가능한 tracer:

```sh
mount -t tracefs none /sys/kernel/tracing; cat /sys/kernel/tracing/available_tracers
```

```text
function_graph wakeup_dl wakeup_rt wakeup preemptirqsoff preemptoff irqsoff function nop
```

### 9.1 예제 9 — `sched_waking` → `sched_wakeup` → `sched_switch`

무엇을 확인하나: CPU1에서 SCHED_OTHER hog가 돌고, 같은 CPU의 FIFO 80 태스크가 5 ms마다 깨어난다. 스케줄러 tracepoint 세 개로 한 번의 깨어남을 따라간다.

```sh
T=/sys/kernel/tracing
taskset -c 1 /f1_spin 20 &
sleep 0.5
echo 2 > $T/tracing_cpumask                      # CPU1만
echo 1 > $T/events/sched/sched_switch/enable
echo 1 > $T/events/sched/sched_wakeup/enable
echo 1 > $T/events/sched/sched_waking/enable
echo 1 > $T/tracing_on
taskset -c 1 /f1_lat 80 3 > /dev/null
echo 0 > $T/tracing_on
grep -v "^#" $T/trace | grep -A2 "sched_waking: comm=f1_lat" | head -9
```

```text
         f1_spin-74      [001] d.h2.    29.901361: sched_waking: comm=f1_lat pid=76 prio=19 target_cpu=001
         f1_spin-74      [001] dNh3.    29.901391: sched_wakeup: comm=f1_lat pid=76 prio=19 target_cpu=001
         f1_spin-74      [001] d..2.    29.901477: sched_switch: prev_comm=f1_spin prev_pid=74 prev_prio=120 prev_state=R ==> next_comm=f1_lat next_pid=76 next_prio=19
--
         f1_spin-74      [001] d.h2.    29.906358: sched_waking: comm=f1_lat pid=76 prio=19 target_cpu=001
         f1_spin-74      [001] dNh3.    29.906388: sched_wakeup: comm=f1_lat pid=76 prio=19 target_cpu=001
         f1_spin-74      [001] d..2.    29.906474: sched_switch: prev_comm=f1_spin prev_pid=74 prev_prio=120 prev_state=R ==> next_comm=f1_lat next_pid=76 next_prio=19
```

출력에서 볼 것 (`-icount`, 시각 값은 실행마다 다르다):

- 이벤트가 찍힌 문맥이 **`f1_spin-74`** 다. 깨우는 일(waking, wakeup)은 hog가 돌던 CPU1에서 **타이머 hardirq 안**(`h` 플래그)에서 일어났다. 깨어날 태스크는 아직 돌지 않는다.
- `sched_wakeup` 줄의 **`N`** 플래그 = 이 시점에 need-resched가 섰다. RT 80이 fair hog를 밀어내야 하므로 `wakeup_preempt()` → `resched_curr()`.
- `sched_switch`는 86 µs 뒤, hardirq 플래그가 없다 — IRQ에서 빠져나가는 길(유저 복귀 직전 `exit_to_user_mode_loop()`)에서 `schedule()`이 불렸다. `prev_state=R`: hog는 잠든 게 아니라 **선점당했다**(여전히 runnable).
- `prio=19`: 커널 내부 prio = 99 − 80. `next_prio=120`은 nice 0.
- 5 ms 간격(29.901361 → 29.906358)이 `clock_nanosleep` 주기다.

### 9.2 예제 10 — `wakeup_rt` tracer: 최고 우선순위 RT 태스크의 최대 깨어남 지연

무엇을 확인하나: `wakeup_rt`는 "가장 높은 우선순위 RT 태스크가 깨어나서(`try_to_wake_up`) 실제로 CPU를 받기까지(`__schedule`)"의 **최대값**과 그때의 경로를 기록한다. 함수 단위 추적은 끄고(`function-trace` 0) 이벤트와 스택만 본다.

```sh
echo 0 > $T/options/function-trace
echo wakeup_rt > $T/current_tracer
echo 0 > $T/tracing_max_latency
echo 1 > $T/tracing_on
taskset -c 1 /f1_lat 80 300
echo 0 > $T/tracing_on
echo "max latency (us): $(cat $T/tracing_max_latency)"
cat $T/trace          # 헤더 일부와 스택 일부만 남기고 생략
```

```text
FIFO: n=300 avg=372 us max=924 us
  ...
max latency (us): 604
# tracer: wakeup_rt
#
# wakeup_rt latency trace v1.1.5 on 6.18.55-dbg
# --------------------------------------------------------------------
# latency: 604 us, #6/6, CPU#1 | (M:PREEMPT VP:0, KP:0, SP:0 HP:0 #P:2)
#    -----------------
#    | task: f1_lat-80 (uid:0 nice:0 policy:1 rt_prio:80)
#    -----------------
...
 f1_spin-74        1dNh4.    2us+:       74:120:R   + [001]      80: 19:R f1_lat
 f1_spin-74        1dNh4.   21us+: <stack trace>
 f1_spin-74        1dNh4.   33us!: ttwu_do_activate <-try_to_wake_up
 f1_spin-74        1d..3.  584us : __schedule <-schedule
 f1_spin-74        1d..3.  588us+:       74:120:R ==> [001]      80: 19:R f1_lat
 f1_spin-74        1d..3.  601us : <stack trace>
-- stack (top 6)
 => __trace_stack
 => probe_wakeup
 => trace_sched_wakeup
 => ttwu_do_activate
 => try_to_wake_up
 => wake_up_process
```

같은 tracer를 icount 없이 처음 돌렸을 때 찍힌 첫 스택 전체(깨우는 쪽 경로)는 이렇다 — 하드웨어 타이머 IRQ에서 hrtimer가 만료되어 잠든 태스크를 깨우는 경로가 한 줄씩 보인다:

```text
 => __trace_stack
 => probe_wakeup
 => trace_sched_wakeup
 => ttwu_do_activate
 => try_to_wake_up
 => wake_up_process
 => hrtimer_wakeup
 => __hrtimer_run_queues
 => hrtimer_interrupt
 => arch_timer_handler_virt
 => handle_percpu_devid_irq
 => handle_irq_desc
 => generic_handle_domain_irq
 => gic_handle_irq
 => call_on_irq_stack
 => do_interrupt_handler
 => el0_interrupt
 => __el0_irq_handler_common
 => el0t_64_irq_handler
 => el0t_64_irq
```

출력에서 볼 것:

- 헤더의 **`M:PREEMPT`** = 이 커널의 preemption 모델(full). `task: f1_lat-80 ... policy:1 rt_prio:80`.
- `74:120:R + [001] 80: 19:R f1_lat` = "pid 74(prio 120, R)가 돌고 있을 때 pid 80(prio 19)이 깨어났다". `==>` 줄이 실제 전환.
- 300번 중 최악이 604 µs. 그 대부분(33 µs → 584 µs)이 깨어남 이후 `__schedule`까지였다. icount 모드의 느린 가상 CPU에서 IRQ 처리 후반부(softirq의 hrtimer·RCU 처리 등)를 지나는 시간이다 — 함수 추적을 켜면(`function-trace` 1) 그 사이에 무엇이 돌았는지 함수 단위로 보인다. 실제 보드에서 이 tracer는 "어떤 드라이버의 IRQ off 구간이 RT 지연을 만들었나"를 찾는 1차 도구다(M·N 모듈).
- 스택의 맨 아래 **`el0t_64_irq`** = IRQ가 **EL0(유저 모드)** 에서 왔다(hog가 유저 공간 busy loop였으므로). `gic_handle_irq` → `arch_timer_handler_virt` → `hrtimer_interrupt` → `hrtimer_wakeup` → `try_to_wake_up`. `clock_nanosleep`의 정체는 hrtimer 하나다(F8).
- 처음 icount 없이 돌린 실행에서는 최대 지연이 15~21 ms로 찍혔다. 호스트 과부하로 vCPU 스레드 자체가 밀려난 결과라 숫자는 버리고 스택만 썼다(§1.3).

---

## 10. context switch 안에서 일어나는 일

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">RT 태스크가 깨어나 CPU를 받기까지 (arm64, 예제 9의 한 사이클)</text>
  <text x="580" y="44" text-anchor="middle" font-size="12" font-weight="bold">측정 (icount)</text>
  <rect x="30" y="52" width="500" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="42" y="71" font-size="12">① 타이머 IRQ (EL0에서): gic_handle_irq → hrtimer_interrupt</text>
  <text x="42" y="89" font-size="12">   → hrtimer_wakeup → try_to_wake_up   [sched_waking]</text>
  <text x="580" y="80" text-anchor="middle" font-size="12">+0 µs</text>
  <rect x="30" y="110" width="500" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="42" y="129" font-size="12">② ttwu_do_activate: enqueue_task(rt) + wakeup_preempt</text>
  <text x="42" y="147" font-size="12">   → resched_curr: TIF_NEED_RESCHED 세움   [sched_wakeup, N]</text>
  <text x="580" y="138" text-anchor="middle" font-size="12">+30 µs</text>
  <rect x="30" y="168" width="500" height="46" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="42" y="187" font-size="12">③ IRQ 종료 → exit_to_user_mode_loop: NEED_RESCHED 확인</text>
  <text x="42" y="205" font-size="12">   → schedule() → __schedule()</text>
  <rect x="30" y="226" width="500" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="42" y="245" font-size="12">④ pick_next_task: stop → dl → rt (여기서 f1_lat)</text>
  <text x="42" y="263" font-size="12">   put_prev_task(fair, f1_spin) · set_next_task(rt, f1_lat)</text>
  <rect x="30" y="284" width="500" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="42" y="303" font-size="12">⑤ context_switch: next->mm 있음 → switch_mm_irqs_off</text>
  <text x="42" y="321" font-size="12">   (TTBR0_EL1 = next의 페이지 테이블 + ASID)   [sched_switch]</text>
  <text x="580" y="312" text-anchor="middle" font-size="12">+116 µs</text>
  <rect x="30" y="342" width="500" height="46" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="42" y="361" font-size="12">⑥ switch_to → __switch_to → cpu_switch_to (entry.S)</text>
  <text x="42" y="379" font-size="12">   x19~x29 · sp · lr 저장/복원, sp_el0 = next (current 교체)</text>
  <rect x="30" y="400" width="500" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="42" y="419" font-size="12">⑦ finish_task_switch → next는 "자기가 예전에 schedule()을</text>
  <text x="42" y="437" font-size="12">   부른 지점"에서 이어서 실행 → clock_nanosleep에서 유저로 복귀</text>
  <line x1="280" y1="98" x2="280" y2="108" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="280" y1="156" x2="280" y2="166" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="280" y1="214" x2="280" y2="224" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="280" y1="272" x2="280" y2="282" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="280" y1="330" x2="280" y2="340" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="280" y1="388" x2="280" y2="398" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="340" y="464" text-anchor="middle" font-size="12">①~⑥은 전부 prev(f1_spin)의 커널 스택 위에서 실행된다. ⑥의 ret부터 next의 스택이다</text>
</svg>
```

그림 9 — 한 번의 깨어남과 전환. 오른쪽 숫자는 예제 9 첫 사이클의 tracepoint 시각 차이(29.901361 기준).

### 10.1 `context_switch()` (kernel/sched/core.c, 6.18.55)

```c
static __always_inline struct rq *
context_switch(struct rq *rq, struct task_struct *prev,
	       struct task_struct *next, struct rq_flags *rf)
{
	prepare_task_switch(rq, prev, next);
	...
	/*
	 * kernel -> kernel   lazy + transfer active
	 *   user -> kernel   lazy + mmgrab_lazy_tlb() active
	 *
	 * kernel ->   user   switch + mmdrop_lazy_tlb() active
	 *   user ->   user   switch
	 */
	if (!next->mm) {                                // to kernel
		enter_lazy_tlb(prev->active_mm, next);
		next->active_mm = prev->active_mm;
		...
	} else {                                        // to user
		...
		switch_mm_irqs_off(prev->active_mm, next->mm, next);
		...
	}
	...
	/* Here we just switch the register state and the stack. */
	switch_to(prev, next, prev);
	barrier();

	return finish_task_switch(prev);
}
```

읽는 법:

- **주소 공간 전환은 next가 유저 태스크일 때만**. kthread(`mm == NULL`)로 바꿀 때는 페이지 테이블을 바꾸지 않고 이전 것을 빌려 쓴다(lazy TLB). 커널 주소 공간은 모든 프로세스에 똑같이 매핑되어 있으므로(TTBR1) 문제가 없다. 같은 프로세스의 스레드끼리(`prev->mm == next->mm`) 바꿀 때도 `switch_mm`이 사실상 할 일이 없다 — **스레드 전환이 프로세스 전환보다 싼 이유**다.
- arm64는 ASID(Address Space ID)가 있어서 `switch_mm`이 TLB 전체를 비우지 않는다. 그래도 캐시·TLB·분기 예측기의 "따뜻함"을 잃는 간접 비용이 직접 비용(수 µs)보다 크다.
- **`switch_to` 이후의 코드는 "나중에 prev가 다시 선택됐을 때" 실행된다.** `switch_to(prev, next, prev)`의 세 번째 인자가 그 이유다: 다시 돌아왔을 때 "나 직전에 돌던 태스크"를 알려 준다(`finish_task_switch(prev)`가 그것을 정리한다).

### 10.2 `cpu_switch_to` — RTOS의 PendSV 핸들러와 같은 자리

```c
/* arch/arm64/kernel/entry.S (6.18.55) */
SYM_FUNC_START(cpu_switch_to)
	save_and_disable_daif x11
	mov	x10, #THREAD_CPU_CONTEXT
	add	x8, x0, x10
	mov	x9, sp
	stp	x19, x20, [x8], #16		// store callee-saved registers
	stp	x21, x22, [x8], #16
	stp	x23, x24, [x8], #16
	stp	x25, x26, [x8], #16
	stp	x27, x28, [x8], #16
	stp	x29, x9, [x8], #16
	str	lr, [x8]
	add	x8, x1, x10
	ldp	x19, x20, [x8], #16		// restore callee-saved registers
	...
	ldr	lr, [x8]
	mov	sp, x9
	msr	sp_el0, x1
	...
	ret
SYM_FUNC_END(cpu_switch_to)
```

Cortex-M의 PendSV 핸들러는 `r4-r11`을 PSP 스택에 push하고 새 TCB의 스택 포인터에서 pop한다. arm64 Linux도 **callee-saved 레지스터만**(x19~x29, sp, lr) `task_struct.thread.cpu_context`에 저장한다. 나머지(caller-saved x0~x18)는 C 호출 규약상 `cpu_switch_to`를 부른 쪽이 이미 버려도 되는 레지스터라 저장할 필요가 없다. 유저 레지스터 전체는 이미 예외 진입 때 커널 스택의 `pt_regs`에 저장되어 있다. FP/SIMD 레지스터는 `__switch_to()`의 `fpsimd_thread_switch()`가 따로(필요할 때 lazy하게) 처리한다. **`msr sp_el0, x1`** 이 §2.4의 `current` 교체다. `ret`은 새 `lr` — next가 예전에 `cpu_switch_to`를 불렀던 바로 다음 줄로 돌아간다.

---

## 11. /proc과 debugfs로 스케줄러 들여다보기

### 11.1 예제 11 — `/proc/<pid>/sched`, `/proc/<pid>/schedstat`, `/proc/schedstat`, `sched/debug`

무엇을 확인하나: CPU1의 hog 하나에 대해 스케줄러가 기록한 통계를 본다(`CONFIG_SCHEDSTATS=y`).

```sh
taskset -c 1 /f1_spin 40 & HOG=$!
sleep 2
cat /proc/$HOG/sched
cat /proc/$HOG/schedstat
grep ctxt /proc/$HOG/status
cat /proc/schedstat
sed -n '/^cpu#1/,/^cpu#2/p' /sys/kernel/debug/sched/debug | head -60
```

```text
== /proc/73/sched
f1_spin (73, #threads: 1)
-------------------------------------------------------------------
se.exec_start                                :         28832.508800
se.vruntime                                  :         27005.322250
se.sum_exec_runtime                          :          2473.623968
se.nr_migrations                             :                    1
nr_switches                                  :                   45
nr_voluntary_switches                        :                    0
nr_involuntary_switches                      :                   45
se.load.weight                               :              1048576
se.avg.load_sum                              :                47204
se.avg.runnable_sum                          :             48346266
se.avg.util_sum                              :             46056469
se.avg.load_avg                              :                 1023
se.avg.runnable_avg                          :                 1024
se.avg.util_avg                              :                  975
se.avg.last_update_time                      :          28836346880
se.avg.util_est                              :                    0
policy                                       :                    0
prio                                         :                  120
se.slice                                     :              1400000
clock-delta                                  :                  432
...
== /proc/73/schedstat
2661174464 63028176 64
== ctxt switches
voluntary_ctxt_switches:	0
nonvoluntary_ctxt_switches:	79
== /proc/schedstat
version 17
timestamp 4294900182
cpu0 0 0 0 0 0 0 7254602368 944426320 1378
domain0 MC 3 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
cpu1 0 0 0 0 0 0 30170506560 828431280 1326
domain0 MC 3 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
== debugfs sched/debug (cpu#1 part)
cpu#1
  .nr_running                    : 1
  .nr_switches                   : 1672
  ...
cfs_rq[1]:/
  .left_deadline                 : 0.000001
  .left_vruntime                 : 0.000001
  .zero_vruntime                 : 27722.340202
  .avg_vruntime                  : 27722.340202
  .right_vruntime                : 0.000001
  .spread                        : 0.000000
  .nr_queued                     : 1
  ...
  .load                          : 1048576
  ...
rt_rq[1]:
  .rt_nr_running                 : 0

dl_rq[1]:
  .dl_nr_running                 : 0
  .dl_bw->bw                     : 996147
  .dl_bw->total_bw               : 104856

runnable tasks:
 S            task   PID       vruntime   eligible    deadline             slice          sum-exec      switches  prio         wait-time        sum-sleep       sum-block  node   group-id  group-path
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 S            init     1     24438.797450   E       24440.197450           1.400000     15012.954144       445     120         0.000000         0.000000         0.000000   0      0        /
 S     migration/1    21        89.966563   E          90.468963           0.700000        20.704320        14       0         0.000000         0.000000         0.000000   0      0        /
 S       watchdogd    47      2964.024953   E           0.450608           1.400000         0.428288         2      49         0.000000         0.000000         0.000000   0      0        /
...
```

(`/proc/<pid>/sched`의 NUMA 줄, `sched/debug`의 중간 줄과 runnable tasks 목록 대부분은 `...`로 생략했다.)

출력에서 볼 것:

- **`nr_voluntary_switches 0`, `nr_involuntary_switches 45`**: busy loop는 한 번도 스스로 잠들지 않았고 45번 전부 선점당했다. `/proc/<pid>/status`의 `voluntary_ctxt_switches`/`nonvoluntary_ctxt_switches`(79, 조금 뒤에 읽어서 더 큼)가 같은 값의 다른 창이다(`task_struct`의 `nvcsw`/`nivcsw`). **실무 진단 1순위 숫자**다: 센서 스레드의 nonvoluntary가 빠르게 늘면 "누군가에게 밀리고 있다".
- **`se.load.weight 1048576`** = 1024 × 1024. 64-bit 커널은 weight를 1024배 해상도로 들고 있다(예제 2에서 `>> SCHED_FIXEDPOINT_SHIFT`한 이유).
- **`/proc/<pid>/schedstat`** = `CPU에서 돈 시간(ns) · run queue에서 기다린 시간(ns) · 실행된 횟수`. 이 hog는 2.66초 돌고 63 ms 기다렸다. **기다린 시간(두 번째 값)이 지연 문제의 직접 증거**다.
- **`/proc/schedstat`** cpu 줄의 7·8·9번째 값도 같은 의미(CPU별 합계). version 17 형식(`Documentation/scheduler/sched-stats.rst`).
- **`sched/debug`**: 6.18에서 `/proc/sched_debug`가 아니라 debugfs `/sys/kernel/debug/sched/debug`다. `cfs_rq`에 EEVDF 상태(`zero_vruntime`, `avg_vruntime`, `left_deadline`)가 보인다. runnable tasks 표에는 각 태스크의 **eligible(E) 여부, deadline, slice**가 나온다 — `migration/1`의 slice가 0.7 ms인 것은 stop 클래스라 fair 필드를 쓰지 않아서 부팅 초기(slice 배율이 CPU 1개 기준이던 때) 값이 남은 것으로 보이고, `watchdogd`의 deadline 0.45는 RT라서 의미 없는 값이다. 커널 내부를 이만큼 그대로 보여 주는 창은 드물다.
- `dl_rq`의 `total_bw 104856` = fair_server 두 개(§6.3).

---

## 12. 커널 안에서는 무슨 일이 일어나나

유저 API 하나하나가 커널의 어떤 함수·필드로 이어지는지 정리한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="110" y="22" text-anchor="middle" font-size="13" font-weight="bold">유저 API</text>
  <text x="340" y="22" text-anchor="middle" font-size="13" font-weight="bold">커널 함수</text>
  <text x="580" y="22" text-anchor="middle" font-size="13" font-weight="bold">바뀌는 것</text>
  <rect x="20" y="36" width="180" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="61" text-anchor="middle" font-size="12">nice() · setpriority()</text>
  <rect x="240" y="36" width="200" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="61" text-anchor="middle" font-size="12">set_user_nice()</text>
  <rect x="480" y="36" width="185" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="572" y="61" text-anchor="middle" font-size="12">static_prio · se.load</text>
  <rect x="20" y="96" width="180" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="121" text-anchor="middle" font-size="12">sched_setscheduler/attr</text>
  <rect x="240" y="96" width="200" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="121" text-anchor="middle" font-size="12">__sched_setscheduler()</text>
  <rect x="480" y="96" width="185" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="572" y="121" text-anchor="middle" font-size="12">policy · sched_class · dl</text>
  <rect x="20" y="156" width="180" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="181" text-anchor="middle" font-size="12">sched_setaffinity()</text>
  <rect x="240" y="156" width="200" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="181" text-anchor="middle" font-size="12">__set_cpus_allowed_ptr()</text>
  <rect x="480" y="156" width="185" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="572" y="181" text-anchor="middle" font-size="12">cpus_mask (+ 이주)</text>
  <rect x="20" y="216" width="180" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="241" text-anchor="middle" font-size="12">clock_nanosleep()</text>
  <rect x="240" y="216" width="200" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="241" text-anchor="middle" font-size="12">hrtimer_nanosleep()</text>
  <rect x="480" y="216" width="185" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="572" y="241" text-anchor="middle" font-size="12">__state → schedule()</text>
  <rect x="20" y="276" width="180" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="301" text-anchor="middle" font-size="12">PI mutex lock</text>
  <rect x="240" y="276" width="200" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="301" text-anchor="middle" font-size="12">futex_lock_pi → rt_mutex</text>
  <rect x="480" y="276" width="185" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="572" y="301" text-anchor="middle" font-size="12">소유자 prio 일시 상승</text>
  <line x1="200" y1="56" x2="238" y2="56" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="440" y1="56" x2="478" y2="56" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="200" y1="116" x2="238" y2="116" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="440" y1="116" x2="478" y2="116" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="200" y1="176" x2="238" y2="176" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="440" y1="176" x2="478" y2="176" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="200" y1="236" x2="238" y2="236" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="440" y1="236" x2="478" y2="236" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="200" y1="296" x2="238" y2="296" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="440" y1="296" x2="478" y2="296" stroke="currentColor" marker-end="url(#ah7)"/>
</svg>
```

그림 10 — 스케줄링 관련 syscall이 바꾸는 `task_struct` 필드. 함수 이름은 6.18.55 `kernel/sched/syscalls.c`, `kernel/sched/core.c`, `kernel/time/hrtimer.c`, `kernel/futex/pi.c`에서 확인했다.

- **잠들기**(F3에서 자세히): `clock_nanosleep()` → `hrtimer_nanosleep()` → `do_nanosleep()`이 `__state = TASK_INTERRUPTIBLE`로 바꾸고 hrtimer를 건 뒤 `schedule()`. `__schedule()`은 prev가 RUNNING이 아니면 `dequeue_task()`로 rq에서 뺀다. 드라이버의 `wait_event_interruptible()`도 정확히 이 패턴이다(`prepare_to_wait` → 상태 바꾸기 → 조건 확인 → `schedule()`).
- **깨우기**: hrtimer 만료(IRQ) → `hrtimer_wakeup()` → `wake_up_process()` → `try_to_wake_up()` → `select_task_rq()`(어느 CPU?) → `ttwu_do_activate()` → `enqueue_task()` + `wakeup_preempt()`. 드라이버가 IRQ 핸들러에서 `wake_up_interruptible(&wq)`나 `complete()`를 부르면 이 경로를 탄다. **"ISR에서 데이터를 받고 스레드를 깨운다"는 RTOS 패턴의 Linux 버전**이 이것이고, 깨어난 스레드가 얼마나 빨리 도는지는 §8에서 본 대로 그 스레드의 정책에 달렸다.
- **커널 스레드의 우선순위**: 드라이버가 만드는 kthread는 기본이 SCHED_NORMAL nice 0이다. 실시간 처리가 필요하면 `sched_set_fifo(task)`(FIFO 50) 또는 `sched_set_fifo_low(task)`(FIFO 1). 커널은 임의 숫자를 주는 `sched_setscheduler()`를 모듈에 export하지 않는다 — "커널 안 RT 우선순위 남발"을 막으려는 정책이다. threaded IRQ(`request_threaded_irq`)의 핸들러 스레드도 FIFO 50으로 만들어진다(예제 2의 `watchdogd`와 같은 값). F6·F7에서 다시 나온다.
- **긴 커널 루프**: 드라이버에서 수천 개 디스크립터를 초기화하는 루프처럼 오래 도는 코드는, `PREEMPT_NONE`/`VOLUNTARY` 커널에서 다른 태스크를 굶긴다. 관례상 `cond_resched()`를 넣는다(잠들 수 있는 문맥에서만, E4 §12의 soft lockup과 연결). full/lazy 커널에서는 `cond_resched()`가 사실상 아무 일도 안 한다.
- **우선순위 상속(PI)**: 유저 `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)` 뮤텍스는 `FUTEX_LOCK_PI` → 커널 `futex_lock_pi()` → `rt_mutex`. 높은 우선순위 태스크가 기다리면 `rt_mutex_setprio()`가 락 소유자의 `prio`를 기다리는 쪽 수준으로 올린다. **`prio`와 `normal_prio`가 다를 수 있는 유일한 이유**가 이것이다(§2.2). 커널 내부의 `rt_mutex`도 같은 메커니즘이고, PREEMPT_RT에서는 spinlock까지 rt_mutex 기반이 된다.

---

## 13. Embedded 실무에서 다시 보기

### 13.1 장면 1 — 1 kHz IMU 제어 루프의 지터

예를 들어 드론 센서 키트에서 IMU를 SPI로 1 kHz 읽어 자세 추정 루프를 돌린다. 이 스레드를 그냥 두면 SCHED_OTHER라서, 카메라 인코더나 로그 압축 같은 무거운 작업이 같은 CPU에 오면 §8처럼 수~수십 ms 밀린다. 실무 처방(N2에서 자세히):

```c
/* 제어 스레드 시작부의 전형적인 설정 (개념 예시 — 이 노트의 f1_lat가 같은 일을 한다) */
struct sched_param sp = { .sched_priority = 80 };
pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);   /* RT 정책 */
mlockall(MCL_CURRENT | MCL_FUTURE);                        /* page fault 금지 */
/* + CPU 고정(isolcpus/cpuset), SPI IRQ 스레드 우선순위를 제어 스레드보다 높게 */
```

함정: 제어 스레드(FIFO 80)가 SPI 드라이버의 **threaded IRQ 핸들러(FIFO 50)** 를 기다린다면, 데이터가 오는 경로가 제어 스레드보다 낮은 우선순위로 돈다. `ps -eLo pid,cls,rtprio,comm`(procps)이나 `chrt -p`로 `irq/NN-spi...` 스레드의 우선순위를 확인하고 필요하면 올린다.

### 13.2 장면 2 — "RT 스레드가 1초에 한 번 50 ms 멈춰요"

RT 루프가 CPU를 100% 쓰도록 짠 시스템(폴링 루프)에서, 6.12+ 커널로 올린 뒤 정확히 1초 주기로 50 ms 멈춤이 보인다면 §5의 fair_server다. 그 CPU에 굶는 fair 태스크(kworker, 로그 데몬)가 있다는 뜻이다. 처방은 (1) RT 루프를 폴링 대신 이벤트 대기로 바꾸거나 (2) 그 CPU를 격리해 fair 태스크가 못 오게 하는 것이다. fair_server를 0으로 만드는 것(예제 5의 C)은 최후의 수단이다.

### 13.3 장면 3 — 카메라 파이프라인과 백그라운드 작업 나누기

V4L2 캡처 스레드, 인코더, 네트워크 송신, 펌웨어 업데이트 다운로더가 같은 SoC에 있다. 업데이트·로그 업로드는 `SCHED_BATCH`나 `nice 10~19`로 낮춘다. nice 10 vs 0이면 측정대로 약 10% : 90%다(그림 4). cgroup v2의 `cpu.weight`로 서비스 단위 비율을 주는 것이 systemd 환경의 표준이다(L 모듈).

### 13.4 장면 4 — 우선순위 역전으로 센서 루프가 가끔 멈춤

낮은 우선순위 로거 스레드가 공용 버퍼 뮤텍스를 잡고 있는 사이, 중간 우선순위 스레드가 CPU를 차지하면, 그 뮤텍스를 기다리는 높은 우선순위 센서 스레드가 무기한 밀린다(Mars Pathfinder 1997년 사례와 같은 구조). Linux 유저 공간 처방: `PTHREAD_PRIO_INHERIT` 뮤텍스. 커널 드라이버의 `struct mutex`는 PI를 하지 않는다(PREEMPT_RT가 아니면). 그래서 드라이버에서 RT 스레드와 일반 스레드가 같은 mutex를 오래 공유하는 설계 자체를 피한다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 지연 민감 스레드를 SCHED_OTHER로 둠 | 평소엔 괜찮다가 부하 때 수~수십 ms 지터 | fair 클래스는 지연 보장이 없다 (§8, 최대 120 ms) | SCHED_FIFO + mlockall + CPU 고정 |
| RT 스레드에서 busy-poll | 1초마다 50 ms 멈춤, 다른 프로세스가 극도로 느림 | 6.12+ fair_server가 굶은 fair 태스크에게 시간을 줌 | 이벤트 대기로 바꾸기, CPU 격리 |
| `echo -1 > sched_rt_runtime_us`로 throttling 끄기 | 아무 변화 없음 | RT_GROUP_SCHED 없는 6.12+에선 DL admission에만 쓰임 (§5 예제 B) | debugfs `fair_server/cpuN/runtime` (위험 이해 후) |
| RT 우선순위를 전부 99로 | 커널 스레드(`migration` 아닌 IRQ 스레드 등)보다 높아져 시스템 응답 상실 | 99는 커널 watchdog류와 경쟁 | 앱은 보통 1~89 범위에서 상대 순서로 설계 |
| nice로 지연을 해결하려 함 | CPU 몫은 늘었는데 지터는 그대로 | nice는 weight(비율)만 바꾼다 | 지연은 정책(RT/DEADLINE)이나 custom slice로 |
| `prio` 숫자를 섞어 말함 | 리뷰·면접에서 의사소통 실패 | 커널 prio(0이 최고) vs rt_priority(99가 최고) vs top의 PR | 어느 축인지 밝힌다 (§2.2 표) |
| SCHED_DEADLINE 태스크에서 fork/affinity | `EAGAIN` / `EBUSY` | DL은 대역폭 회계 때문에 제약 | RESET_ON_FORK, cpuset으로 root domain 분리 |
| 커널 긴 루프에 양보 지점 없음 (NONE/VOLUNTARY 커널) | soft lockup 경고, 다른 태스크 굶음 | 커널 코드는 그 모델에서 선점되지 않는다 | `cond_resched()` (잠들 수 있는 문맥에서만) |
| 드라이버 kthread를 기본 정책으로 둠 | 데이터 경로가 앱 RT 스레드보다 늦게 돎 | kthread 기본은 SCHED_NORMAL | `sched_set_fifo()` / `sched_set_fifo_low()` |
| RT·일반 스레드가 일반 mutex 공유 | 높은 우선순위 스레드가 가끔 크게 지연 | 우선순위 역전 | PI mutex, 공유 줄이기, 락 쥔 구간 짧게 |

---

## 15. 면접에서 이렇게 말한다

**Q.** How does the Linux scheduler work?

**A.** CPU마다 run queue가 있고, 그 안에 클래스별 큐가 있다. 순서는 stop, deadline, RT, fair, idle이고, 스케줄러는 위 클래스부터 물어서 처음 나온 태스크를 돌린다. 보통 프로세스가 속한 fair 클래스는 6.6부터 EEVDF다: 각 태스크의 vruntime을 weight로 정규화해서 추적하고, 받을 몫보다 덜 받은(lag ≥ 0) 태스크 중 virtual deadline이 가장 이른 것을 고른다. RT 클래스는 RTOS처럼 고정 우선순위 선점형이다. 실제 전환은 need-resched 플래그가 선 뒤 IRQ 리턴·유저 복귀·preempt_enable 같은 선점 지점에서 일어난다.

> Each CPU has its own run queue, split by scheduling class: stop, deadline, real-time, fair, idle. The core asks each class in that order and runs the first task it gets. Normal tasks are in the fair class, which since 6.6 is EEVDF: every task has a weight from its nice value, vruntime advances inversely to that weight, and the scheduler picks, among tasks that are owed service — lag at least zero — the one with the earliest virtual deadline, where the deadline is vruntime plus slice over weight. Real-time tasks, SCHED_FIFO and RR, behave like an RTOS: fixed priority, preemptive. The actual switch happens when TIF_NEED_RESCHED is set and the CPU reaches a preemption point — IRQ return, return to user, preempt_enable, or a blocking call.

**꼬리 질문**: "CFS와 EEVDF의 차이는?" → CFS는 vruntime 최소만 봤고 지연은 휴리스틱이었다. EEVDF는 공평(lag)과 지연(slice → deadline)을 분리해서, 짧은 slice를 요청한 태스크가 원칙적으로 빨리 뽑힌다.

**Q.** What is priority inversion, and how does Linux deal with it?

**A.** 낮은 우선순위 태스크가 락을 잡은 채 중간 우선순위 태스크에게 밀리면, 그 락을 기다리는 높은 우선순위 태스크가 사실상 중간 우선순위보다 늦게 도는 현상이다. 해결책은 우선순위 상속: 높은 태스크가 기다리는 동안 락 소유자의 우선순위를 그 수준으로 올린다. 유저 공간은 `PTHREAD_PRIO_INHERIT` 뮤텍스(PI futex → 커널 rt_mutex), 커널은 rt_mutex가 이를 하고, PREEMPT_RT에서는 spinlock과 mutex까지 PI를 한다. 일반 커널의 `struct mutex`는 PI를 하지 않는다.

> Priority inversion is when a high-priority task waits on a lock held by a low-priority task, and a medium-priority task preempts the lock holder, so the high-priority task is effectively blocked by the medium one — the Mars Pathfinder bug. The fix is priority inheritance: while the high-priority task waits, the lock owner temporarily runs at the waiter's priority. In Linux userspace you get it with a PTHREAD_PRIO_INHERIT mutex, which uses PI futexes backed by the kernel's rt_mutex; the boost shows up as task->prio differing from normal_prio. On PREEMPT_RT, spinlocks and mutexes become rt_mutex-based, so the kernel gets PI too. A regular kernel struct mutex does not do PI, so I avoid sharing such locks between RT and non-RT paths.

**꼬리 질문**: "PI 말고 다른 방법은?" → priority ceiling(`PTHREAD_PRIO_PROTECT`), 락 공유 자체를 없애기(lock-free 링버퍼, 메시지 큐).

**Q.** What happens if a SCHED_FIFO thread goes into an infinite loop?

**A.** 그 CPU의 fair 태스크는 굶는다. 다만 완전히 멈추지는 않는다. 6.12 이전에는 RT throttling(1초 중 0.95초까지만 RT)이, 6.12+ 기본 설정에서는 CPU마다 있는 deadline 클래스의 fair_server가 1초에 50 ms를 fair 태스크에게 준다. 제가 QEMU에서 재 보니 정확히 4.9%를 받았고, 그 50 ms가 굶기 시작한 지 약 0.95초 뒤에 몰아서 왔다. 그리고 RT_GROUP_SCHED가 없는 커널에서는 `sched_rt_runtime_us=-1`로 이 동작이 꺼지지 않았다.

> The fair tasks on that CPU starve, but not completely. Historically RT throttling capped RT at 950 ms per second. Since 6.12, on kernels without RT group scheduling, that job is done by a per-CPU fair server — a SCHED_DEADLINE entity that reserves 50 ms per second for fair tasks and runs deferred, at zero laxity. I measured it on a 6.18 kernel: the starved task got 4.9 percent, delivered in one 50 ms chunk near the end of each second, and writing -1 to sched_rt_runtime_us did not change it. Other CPUs are unaffected, so the fix is usually to not busy-poll in RT, and to isolate the CPU.

**Q.** What's the difference between nice and real-time priority?

**A.** nice는 fair 클래스 안에서 CPU **비율**을 바꾼다(weight, nice 1 차이 ≈ 10%). 아무리 낮은 nice라도 결국 CPU를 받는다. RT 우선순위는 **절대 순서**다. 높은 RT가 runnable이면 낮은 쪽은 0%다. 그리고 모든 RT는 모든 fair보다 위다. 지연 문제는 nice로 못 고친다.

> Nice only changes the weight inside the fair class, so it changes proportions — each nice step is about ten percent of CPU, about a 1.25x weight ratio; I measured nice 0 versus 5 at 75 to 25 percent, matching the weight table. RT priority is strict: a runnable higher-priority RT task gets the CPU, period, and every RT task beats every fair task. So nice is for throughput sharing; for latency you need an RT or deadline policy.

**Q.** Explain kernel preemption models. What does CONFIG_PREEMPT change?

**A.** 유저 코드는 언제나 선점된다. 모델이 바꾸는 건 커널 코드다. NONE은 유저 복귀나 스스로 잠들 때만, VOLUNTARY는 cond_resched 같은 명시적 지점에서도, PREEMPT(full)는 preempt count가 0인 어디서나(IRQ 리턴, preempt_enable). LAZY는 RT 깨움엔 full처럼, fair끼리는 tick까지 미뤄 처리량을 회복한다. PREEMPT_RT는 spinlock 구간까지 선점 가능하게 만들고 IRQ를 스레드로 돌린다(6.12에 mainline).

> User code is always preemptible; the model decides when kernel code can be preempted. NONE: only at return to user or when the task blocks. VOLUNTARY adds explicit points like cond_resched. Full PREEMPT allows preemption anywhere the preempt count is zero — on IRQ exit or at preempt_enable. LAZY, since 6.13, preempts immediately for RT and deadline wakeups but defers fair-versus-fair preemption to the next tick or return to user, to avoid preempting lock holders. PREEMPT_RT, mainline since 6.12, makes most spinlock sections preemptible by turning them into sleeping rt_mutex-based locks and forces threaded interrupts.

**Q.** What happens during a context switch on ARM64?

**A.** `__schedule()`이 다음 태스크를 고르고 `context_switch()`를 부른다. next가 유저 태스크면 `switch_mm`으로 TTBR0와 ASID를 바꾸고(kthread면 lazy TLB로 생략), `cpu_switch_to`에서 callee-saved 레지스터 x19–x29, sp, lr을 `thread.cpu_context`에 저장·복원하고 `sp_el0`에 next의 task_struct를 넣어 `current`를 바꾼다. FP/SIMD는 별도로 처리. 직접 비용은 수 µs지만 캐시·TLB가 식는 간접 비용이 더 크다.

> The scheduler picks the next task and calls context_switch. If next has its own mm, switch_mm loads its page table into TTBR0 with its ASID, so no full TLB flush; for kernel threads it lazily borrows the previous mm. Then cpu_switch_to saves the callee-saved registers x19 to x29, sp and lr into the task's cpu_context, loads the next task's, and writes sp_el0, which is how arm64 tracks current. User registers were already saved in pt_regs on exception entry, and FP/SIMD state is switched separately. The direct cost is microseconds; the bigger cost is cold caches and TLB.

**Q.** How would you guarantee a sensor thread gets 2 ms every 10 ms?

**A.** SCHED_DEADLINE으로 runtime 2 ms, period 10 ms를 예약한다. 커널이 admission control로 전체 대역폭을 검사해서 넘치면 EBUSY로 거부하고, CBS로 예약을 넘어 쓰지 못하게 막는다. 측정해 보니 hog 4개 아래에서도 정확히 20.1%를 받았다. 단 fork·affinity 제약이 있고, 주기적 패턴이 분명할 때 맞는 도구다. 단순한 경우엔 SCHED_FIFO + 주기 sleep이 더 흔하다.

> I'd use SCHED_DEADLINE with runtime 2 ms and period 10 ms via sched_setattr. The kernel does admission control — it rejects the request with EBUSY if total reserved bandwidth would exceed the limit — and enforces the budget with a constant bandwidth server, throttling the task when it overruns. In my test under heavy load it got 20.1 percent of a CPU with a 20 percent reservation. In practice many teams still use SCHED_FIFO with clock_nanosleep for simple periodic loops; DEADLINE is nicer when you have multiple periodic tasks and want the kernel to check schedulability.

**Q.** How do you debug "my real-time thread is sometimes late"?

**A.** 먼저 숫자로: cyclictest나 직접 만든 루프로 지연 분포를 잰다. 그다음 `/proc/<pid>/status`의 nonvoluntary_ctxt_switches, `/proc/<pid>/schedstat`의 대기 시간으로 "밀리고 있나"를 본다. 원인 위치는 ftrace: `sched_switch`/`sched_wakeup` 이벤트로 누가 CPU를 잡고 있었는지, `wakeup_rt` tracer로 최악의 깨어남 지연과 그 경로를 본다. IRQ off·preempt off 구간이 의심되면 `irqsoff`/`preemptoff` tracer.

> First quantify it: a cyclictest-style loop gives the latency histogram and the max. Then check whether the thread is being preempted — nonvoluntary context switches in /proc/pid/status and wait time in /proc/pid/schedstat. Then find who: ftrace sched_switch and sched_wakeup events show what was running; the wakeup_rt tracer records the worst wakeup-to-run latency with stack traces — in my lab it showed the wakeup coming from the arch timer IRQ through hrtimer_wakeup. If the gap is inside the kernel, the irqsoff and preemptoff tracers point to the code holding interrupts or preemption off.

---

## 16. 직접 해보기

1. **개념**: nice -5와 nice 0인 태스크 두 개가 한 CPU를 나눌 때 각자의 몫을 weight 표로 계산하라. 그다음 nice 10과 15로 바꾸면?
   정답/힌트: 3121 : 1024 → 75.3% : 24.7%. 110 : 36 → 75.3% : 24.7% — weight가 비율 기반이라 같다.
2. **개념**: 예제 2의 nice 5 출력에서 `deadline - vruntime`이 4,279,403인 이유를 식으로 설명하라. custom slice 300 µs를 주면 얼마가 되나?
   정답/힌트: `slice × 1024 / weight` = 1,400,000 × 1024 / 335. 300,000 × 1024 / 335 ≈ 916,955.
3. **코드**: `f1_share`를 고쳐 SCHED_BATCH, SCHED_IDLE 정책도 받게 하고, nice 0 SCHED_OTHER vs SCHED_IDLE의 비율을 재 보라. SCHED_IDLE의 weight는 얼마로 추정되나?
   정답/힌트: `sched_setscheduler(0, SCHED_IDLE, &sp)` (priority 0). 커널 `WEIGHT_IDLEPRIO`(3)를 소스에서 확인하고 측정과 비교.
4. **코드**: `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)` 뮤텍스로 우선순위 역전을 재현하라: 한 CPU에 low(FIFO 10, 락 잡고 바쁜 일) / mid(FIFO 50, busy loop) / high(FIFO 90, 같은 락 대기). PI 있음/없음에서 high가 락을 얻기까지 걸린 시간을 비교하라. 락을 잡은 동안 low의 `/proc/<pid>/sched`의 prio를 찍어 보라.
   정답/힌트: PI 없음이면 mid가 끝날 때까지 high가 못 돈다. PI 있음이면 low의 prio가 high 수준(9)으로 올라간 것이 보인다.
5. **/proc 관찰**: 예제 5를 돌리는 동안 다른 셸에서 `cat /sys/kernel/debug/sched/debug`의 `dl_rq[1]`과 `runnable tasks`를 1초 간격으로 찍어 fair_server가 언제 활성화되는지 보라.
   정답/힌트: `.dl_nr_running`이 FIFO 시작 약 0.95초 뒤 잠깐 1이 된다(직접 확인해 볼 것 — 이 노트에서는 실행하지 않았다).
6. **ftrace**: 예제 9를 `-icount` 없이, 또 `nice -n -10` hog 4개 아래 SCHED_OTHER로 돌려 `sched_wakeup`에서 `sched_switch`까지의 간격을 비교하라. `sched_wakeup` 줄의 `N` 플래그가 OTHER일 때도 서는가?
   정답/힌트: 무거운 hog 아래 OTHER는 wakeup_preempt에서 선점이 거절되어 `N`이 안 서는 경우가 많다.
7. **커널 코드**: 예제 2 모듈에 `pid` 모듈 파라미터를 추가해 `find_get_pid()` + `get_pid_task()`로 임의 태스크의 필드를 찍게 하라. 참조 카운트(`put_task_struct`, `put_pid`)를 잊지 말 것.
   정답/힌트: `get_pid_task(find_get_pid(pid), PIDTYPE_PID)` → 사용 → `put_task_struct()`; `find_get_pid`의 참조는 `put_pid()`.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| `task_struct` | 태스크 디스크립터 | 스레드 하나당 하나. RTOS의 TCB |
| tgid | thread group id | 유저가 보는 PID. 같은 프로세스의 스레드가 공유 |
| run queue (`struct rq`) | 실행 대기 큐 | CPU마다 하나, 클래스별 하위 큐를 가짐 |
| scheduling class | 스케줄링 클래스 | stop > dl > rt > fair > idle. 함수 포인터 테이블 |
| policy | 스케줄링 정책 | 유저가 고르는 것: NORMAL, BATCH, IDLE, FIFO, RR, DEADLINE |
| `static_prio` / `normal_prio` / `prio` | 우선순위 3종 | nice 기반 / 정책 기반 / PI까지 반영한 실효값 |
| weight | 가중치 | nice에서 온 값. fair 클래스의 CPU 몫 비율 |
| vruntime | 가상 실행 시간 | 실제 실행 시간 × 1024 / weight |
| CFS | Completely Fair Scheduler | 2.6.23~6.5의 fair 클래스. vruntime 최소 선택 |
| EEVDF | Earliest Eligible Virtual Deadline First | 6.6+ fair 클래스. eligible 중 가상 deadline 최소 |
| lag | 지연된 몫 | `w × (V − v)`. 양수면 덜 받음 |
| eligible | 선택 자격 | lag ≥ 0인 태스크 |
| virtual deadline | 가상 마감 | `v + slice / w` |
| slice | 요청량 | 한 번에 받을 실행 시간. 기본 0.7 ms × (1 + log2 CPU 수) |
| SCHED_FIFO / RR | 실시간 정책 | 고정 우선순위 1~99. RR은 같은 우선순위끼리 100 ms 교대 |
| RT throttling | RT 제한 | RT 폭주 시 비RT 몫 보장. 6.12+는 fair_server가 담당 |
| fair_server | fair 서버 | CPU마다 50 ms/1 s 예약된 deadline 엔티티. 굶는 fair 태스크를 돌림 |
| SCHED_DEADLINE | 마감 정책 | runtime/deadline/period 예약. EDF + CBS |
| admission control | 승인 제어 | 예약 합이 한계를 넘으면 `EBUSY` |
| `TIF_NEED_RESCHED` | 재스케줄 필요 플래그 | "바꿔야 한다" 표시. 선점 지점에서 확인 |
| preemption point | 선점 지점 | IRQ 리턴, 유저 복귀, `preempt_enable`, `cond_resched` |
| preempt count | 선점 금지 카운터 | 0이 아니면 선점 안 함 (spinlock, IRQ 문맥) |
| PREEMPT_LAZY | 지연 선점 모델 | fair끼리 선점을 tick까지 미룸 |
| context switch | 문맥 전환 | `context_switch()` → `switch_mm` + `cpu_switch_to` |
| lazy TLB | 주소 공간 빌려 쓰기 | kthread로 전환 시 페이지 테이블을 안 바꿈 |
| priority inheritance | 우선순위 상속 | 락 소유자를 기다리는 쪽 우선순위로 올림 (rt_mutex) |
| schedstat | 스케줄 통계 | `/proc/<pid>/schedstat`: 실행 시간·대기 시간·횟수 |

---

## 18. 요약 & 체크리스트

Linux 스케줄러는 CPU마다 run queue를 두고, 클래스 순서(stop > deadline > RT > fair > idle)대로 물어서 다음 태스크를 고른다. RT 클래스는 Don이 아는 RTOS 스케줄러와 같다. 대부분의 태스크가 사는 fair 클래스는 6.6부터 EEVDF로, weight(= nice)로 정규화한 vruntime의 평균 V를 기준으로 덜 받은(eligible) 태스크 중 가상 deadline(`v + slice/w`)이 가장 이른 것을 고른다 — 측정한 CPU 몫은 weight 표와 1%p 안에서 맞았다. RT가 폭주하면 6.12+ 커널에서는 fair_server가 1초에 50 ms를 fair에게 돌려주고(측정 4.9%), `sched_rt_runtime_us=-1`로는 꺼지지 않는다. SCHED_DEADLINE은 예약과 admission control을 준다(20% 예약 → 20.1%, 초과 요청 → EBUSY). 실제 전환은 `TIF_NEED_RESCHED`가 선 뒤 선점 지점에서 일어나고, 커널 코드가 언제 끊길 수 있는지는 preemption 모델이 정한다. 부하 아래 깨어남 지연은 SCHED_OTHER가 최대 120 ms, SCHED_FIFO가 최대 0.84 ms였다(QEMU icount 기준) — 지연이 중요하면 정책을 바꿔야 한다.

- [ ] `task_struct`의 `__state`, `pid/tgid`, `prio/static_prio/normal_prio/rt_priority`, `se/rt/dl`, `mm/active_mm`, `stack`을 설명할 수 있다
- [ ] 커널 prio(0~139)와 rt_priority(1~99), nice(-20~19)의 변환을 말할 수 있다
- [ ] 클래스 순서와 그것이 링커 섹션 순서로 정해진다는 것을 설명할 수 있다
- [ ] nice → weight 표로 두 태스크의 CPU 몫을 계산할 수 있다
- [ ] EEVDF의 V, lag, eligible, virtual deadline, slice를 그림으로 설명할 수 있다
- [ ] SCHED_FIFO와 RR의 차이, RR timeslice 기본값을 말할 수 있다
- [ ] RT throttling의 고전 설명과 6.12+ fair_server(dl_defer)의 차이를 설명할 수 있다
- [ ] SCHED_DEADLINE을 `sched_setattr()`로 설정하고 admission control 결과를 해석할 수 있다
- [ ] `schedule()`이 불리는 경로(blocking, tick, wakeup, 유저 복귀, IRQ 리턴, preempt_enable, cond_resched)를 나열할 수 있다
- [ ] preemption 모델 다섯 가지의 차이를 말할 수 있다
- [ ] ftrace `sched_switch`/`sched_wakeup`과 `wakeup_rt`로 깨어남 경로와 지연을 보여 줄 수 있다
- [ ] arm64 context switch(`switch_mm_irqs_off`, `cpu_switch_to`, `sp_el0`)를 설명할 수 있다

---

## 참고 자료

- docs.kernel.org — [EEVDF Scheduler](https://docs.kernel.org/scheduler/sched-eevdf.html), [CFS Scheduler](https://docs.kernel.org/scheduler/sched-design-CFS.html), [Deadline Task Scheduling](https://docs.kernel.org/scheduler/sched-deadline.html), [Real-Time group scheduling](https://docs.kernel.org/scheduler/sched-rt-group.html), [Scheduler Statistics](https://docs.kernel.org/scheduler/sched-stats.html), [Scheduler Nice Design](https://docs.kernel.org/scheduler/sched-nice-design.html) — 랩 소스 `Documentation/scheduler/`의 같은 파일을 읽었다
- man7.org — [sched(7)](https://man7.org/linux/man-pages/man7/sched.7.html), [sched_setattr(2)](https://man7.org/linux/man-pages/man2/sched_setattr.2.html), [sched_setaffinity(2)](https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html), [clock_nanosleep(2)](https://man7.org/linux/man-pages/man2/clock_nanosleep.2.html), [proc_pid_status(5)](https://man7.org/linux/man-pages/man5/proc_pid_status.5.html)
- LWN.net — [An EEVDF CPU scheduler for Linux](https://lwn.net/Articles/925371/), [Completing the EEVDF scheduler](https://lwn.net/Articles/969062/) (sched-eevdf.rst가 인용하는 두 기사)
- Michael Kerrisk, "The Linux Programming Interface", 35장 (Process Priorities and Scheduling)
- OSTEP 7~9장 (Scheduling: Introduction, MLFQ, Proportional Share) — EEVDF의 "비례 배분" 배경
- 소스 (6.18.55, elixir.bootlin.com에서도 같은 경로): `include/linux/sched.h`(task_struct, sched_entity), `kernel/sched/core.c`(`__schedule`, `context_switch`, `sched_prio_to_weight`, `sched_tick`), `kernel/sched/fair.c`(`pick_eevdf`, `update_deadline`, `avg_vruntime`), `kernel/sched/rt.c`, `kernel/sched/deadline.c`(`sched_init_dl_servers`), `kernel/sched/syscalls.c`, `kernel/Kconfig.preempt`, `kernel/entry/common.c`, `arch/arm64/kernel/entry.S`(`cpu_switch_to`), `include/asm-generic/vmlinux.lds.h`(`SCHED_DATA`)
