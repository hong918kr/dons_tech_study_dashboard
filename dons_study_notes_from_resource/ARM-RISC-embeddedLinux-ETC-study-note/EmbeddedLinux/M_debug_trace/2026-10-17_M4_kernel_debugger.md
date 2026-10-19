# M4. 커널 디버거 — kgdb, JTAG + OpenOCD/Lauterbach, Magic SysRq, vmlinux 심볼

> **이 노트를 다 읽으면**: MCU JTAG 경험을 Linux 커널로 옮길 때 무엇이 달라지는지(MMU·KASLR·멀티코어·preemption·전원 도메인·시간) 말할 수 있다 · QEMU gdbstub을 JTAG처럼 써서 `start_kernel`, MMU 켜기 전 물리 주소 코드, KASLR 커널, 모듈 함수·전역 변수에 브레이크포인트/워치포인트를 걸고 `lx-*` 명령으로 태스크·모듈·per-CPU 변수를 볼 수 있다 · kgdb/kdb를 두 번째 UART와 콘솔 UART에서 띄우고 SysRq-g·serial BREAK로 진입할 수 있다 · "인터럽트 핸들러에서 멈춘 보드"를 디버거로 붙어 찾고, 멈춰 있던 동안 생기는 watchdog·시계 부작용을 설명할 수 있다
> **왜 필요한가**: bring-up과 "보드가 그냥 얼었다" 장애에서 로그가 아무것도 안 남을 때 마지막 수단이 디버거다. 면접에서는 "JTAG만 있는 보드에서 인터럽트 핸들러 hang을 어떻게 잡나요?", "kgdb와 JTAG의 차이는?", "Magic SysRq로 뭘 볼 수 있나요?"로 나온다
> **Don 기준 난이도**: JTAG halt·브레이크포인트·워치포인트·레지스터/메모리 보기·리셋 직후 디버깅은 SSD 컨트롤러와 RF 칩에서 매일 했다 / 새로 배울 부분은 가상 주소와 KASLR 위의 심볼, 실행 중에 올라오는 모듈의 심볼, 커널 안에 사는 디버거(kgdb), CPU를 세웠을 때 OS가 받는 부작용, Cortex-A CoreSight 쪽 설정
> **선행 노트**: C6 §2(gdb로 커널 첫 명령 보기), E3(모듈), M1(printk·콘솔), M3(Oops 해독 — §6 `add-symbol-file`과 섹션 주소, KASLR) — L5(secure boot의 JTAG 잠금), L7(kdump·pstore), M11(scope·LA 연계)와 함께 읽으면 좋다

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어에서 Don이 제일 믿던 도구는 JTAG였다. 코어를 세우고, PC와 스택을 보고, SRAM의 큐 상태를 덤프하고, 워치포인트로 "누가 이 디스크립터를 덮어썼나"를 잡았다. Embedded Linux에서도 그 도구는 그대로 있다. 다만 **세우는 대상이 운영체제**라는 점이 다르다. 코어를 세우면 그 위의 스케줄러, 타이머, RCU, watchdog, 다른 코어가 모두 영향을 받는다. 그리고 커널은 가상 주소에서 돌고, 부팅마다 위치가 바뀌고(KASLR), 드라이버는 부팅 뒤에 모듈로 올라온다.

그래서 Linux 커널 디버깅 도구는 사다리처럼 생각하면 좋다. 위쪽은 커널이 살아 있어야 쓸 수 있고 싸다. 아래로 갈수록 커널의 협조가 덜 필요하지만 장비와 설정이 더 든다.

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="400" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="32" y="40" font-size="13">1. printk · dynamic debug · ftrace (M1, M5)</text>
  <text x="32" y="60" font-size="12">커널이 살아 있어야 한다 · 고치고 다시 빌드·부팅</text>
  <rect x="65" y="82" width="400" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="77" y="102" font-size="13">2. Magic SysRq (t, w, l, m, p)</text>
  <text x="77" y="122" font-size="12">IRQ·콘솔이 살아 있으면 상태 덤프 · 멈추지는 않는다</text>
  <rect x="110" y="144" width="400" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="122" y="164" font-size="13">3. kgdb / kdb (kgdboc)</text>
  <text x="122" y="184" font-size="12">커널 안의 gdb stub · 폴링 UART 필요 · 모든 CPU 정지</text>
  <rect x="155" y="206" width="400" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="167" y="226" font-size="13">4. QEMU gdbstub (-s -S) — 이 랩의 JTAG</text>
  <text x="167" y="246" font-size="12">에뮬레이터가 CPU를 세운다 · 커널 협조 불필요</text>
  <rect x="200" y="268" width="400" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="212" y="288" font-size="13">5. JTAG/SWD + OpenOCD · TRACE32 · J-Link · ETM</text>
  <text x="212" y="308" font-size="12">실리콘 debug logic이 코어를 세운다 · 리셋 첫 명령부터</text>
  <line x1="630" y1="25" x2="630" y2="312" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
  <text x="20" y="348" font-size="12">아래로 갈수록: 커널 협조가 덜 필요하고, 장비·설정은 더 필요하다</text>
</svg>
```

그림 1 — 커널 디버깅 도구의 사다리. 위에서부터 시도하고, 위 단계가 안 통할 때(로그 없음, 콘솔 먹통, IRQ 마스크 상태로 hang) 아래로 내려간다.

이 노트의 흐름은 다음과 같다.

- §1: MCU JTAG과 Linux 커널 디버깅이 무엇이 다른가 (표 하나로 정리)
- §2~§3: QEMU gdbstub을 JTAG 대용으로 쓰기 — `start_kernel`, MMU 켜기 전 물리 주소, KASLR, VA/PA 메모리, `lx-*` 명령
- §4: 모듈 디버깅 — 심볼 로딩, 워치포인트로 메모리 오염 범인 잡기, 이 랩에서 만난 함정
- §5: kgdb와 kdb — 구조, 두 번째 UART, SysRq-g, serial BREAK
- §6: Magic SysRq
- §7: 실전 — 인터럽트 핸들러에서 hang 난 CPU를 나중에 붙어서 찾기
- §8: 멈추면 생기는 일 — 시계, watchdog, 스텝
- §9: 실제 보드의 JTAG — CoreSight, OpenOCD, TRACE32 (가이드)

실행 환경: Mac의 Lima VM `elinux`(호스트 커널 7.0, gdb 17.1, QEMU 10.2.1) 안에서 QEMU `virt` 게스트를 TCG로 돌린다. 게스트 커널은 랩의 `Image-trace`(6.18.55-trace, KGDB+KDB+kgdboc, `CONFIG_GDB_SCRIPTS=y`, `vmlinux-trace`는 DWARF5 full debug info). VM을 17명이 같이 쓰는 중이라 부하가 높다. **이 노트의 모든 시간 값은 TCG 에뮬레이션 + 공유 부하에서 잰 값이라 실제 하드웨어 숫자가 아니다.**

---

## 1. MCU JTAG에서 Linux 커널로 — 무엇이 달라지나

### 1.1 직관

Cortex-R/M 펌웨어는 "주소 = 물리 주소, 이미지 = 한 덩어리, 코어 = 내 코드만 돈다"였다. `.map` 파일의 주소에 그대로 브레이크포인트를 걸면 됐다. Linux 커널은 이 세 가지가 다 깨진다.

### 1.2 차이 정리

| 주제 | MCU 펌웨어 (Don이 하던 것) | Linux 커널 (Cortex-A) | 디버거에서 하는 일 |
|---|---|---|---|
| 주소 | 물리 주소 하나. `.map` 그대로 | 커널은 `0xffff8000...` 가상 주소. MMU 켜기 전 `head.S`는 물리 주소에서 돈다 | 심볼 파일을 오프셋 주고 다시 로드(§3.2). 물리 메모리는 `monitor xp`, `lx-virt_to_phys` |
| 이미지 위치 | 링크 주소 고정 | KASLR(`CONFIG_RANDOMIZE_BASE`)로 부팅마다 이동 | `nokaslr` 부팅, 또는 런타임 레지스터로 오프셋 계산(§3.3) |
| 코드 묶음 | 이미지 하나 | `vmlinux` + 부팅 뒤 올라오는 `.ko` 모듈 | 모듈마다 섹션 주소를 알려 줘야 한다(§4) |
| 코어 | 대개 1~2개, 각자 펌웨어 | SMP. 한 코어를 세워도 다른 코어는 돈다(디버거 설정에 따라) | gdb에서 CPU = thread. `info threads`, `thread N` |
| 무엇이 돌고 있나 | 내 태스크들 | 수백 개 task, kthread, IRQ, softirq | `$lx_current()`, `lx-ps`, `bt`로 "지금 누구 문맥인가"부터 |
| 시간 | 타이머 내 맘대로 | 스케줄러 틱, hrtimer, RCU, softlockup/hardlockup/workqueue watchdog | 멈춘 시간만큼 경고가 쏟아질 수 있다(§8) |
| 캐시 | 대개 단순 | D-cache, I-cache, 여러 코어 coherency | 디버거가 쓴 소프트웨어 브레이크포인트가 I-cache에 반영되는지 — 디버거가 처리하지만 메모리 직접 수정 때는 유의 |
| 전원 | 코어 항상 켜짐 | cpuidle이 코어 전원을 끈다(PSCI) | 꺼진 코어는 debug 레지스터도 꺼진다. `cpuidle.off=1`로 테스트(§9) |
| 보안 | 대개 열려 있음 | TF-A가 EL3, secure world. 양산품은 JTAG fuse 잠금 | secure debug 권한(SPIDEN), L5 |

### 1.3 Don 경험과 연결

SSD 컨트롤러에서 "코어 하나만 세우고 다른 코어(호스트 인터페이스 쪽)는 계속 돌게" 했다가 호스트 타임아웃이 난 경험이 있다면, Linux에서 생기는 문제도 같은 종류다. 커널에서는 이게 RCU stall, watchdog lockup 경고, 네트워크 타임아웃으로 나타난다. 반대로 "모든 코어를 세운다"는 것은 시계까지 세우지 않으면 깨어났을 때 시간이 점프한다. §8에서 실제로 본다.

---

## 2. 랩 준비 — `vmlinux`, `scripts/gdb`, `nokaslr`

### 2.1 세 가지 재료

커널을 소스 수준에서 디버깅하려면 셋이 필요하다.

1. **`vmlinux`**: 압축 안 된 ELF. `Image`는 이것을 `objcopy -O binary`한 것이라 심볼이 없다. 디버거에는 `vmlinux`를 준다(M3).
2. **DWARF 디버그 정보**: `CONFIG_DEBUG_INFO_DWARF5=y` 같은 설정. `vmlinux-trace`는 full DWARF5로 약 600 MB다.
3. **`scripts/gdb` 헬퍼** (`CONFIG_GDB_SCRIPTS=y`): `lx-dmesg`, `lx-ps`, `lx-lsmod`, `lx-symbols`, `$lx_current()` 같은 Python 확장. 빌드 트리에 `vmlinux-gdb.py`와 `scripts/gdb/linux/constants.py`(커널 헤더에서 뽑은 상수)가 생겨야 쓸 수 있다.

### 2.2 예제 1 — `scripts/gdb` 헬퍼를 내 디렉터리에 빌드하기

무엇을 확인하나: 랩 빌드 트리(`linux-build-trace`)에는 `CONFIG_GDB_SCRIPTS=y`인데도 `vmlinux-gdb.py`가 없었다(`Image modules` 타깃만 빌드해서 `scripts_gdb` 타깃이 안 돌았다). 랩은 읽기 전용이니 `.config`만 복사해서 내 출력 디렉터리에 `scripts_gdb`만 만든다.

```sh
ls /var/lab/out/linux-build-trace/vmlinux-gdb.py
mkdir -p /var/tmp/M4/kb && cp /var/lab/build/linux-trace/.config /var/tmp/M4/kb/
cd /var/lab/src/linux-6.18.55 && make O=/var/tmp/M4/kb -j2 olddefconfig scripts_gdb 2>&1 | tail -2
ln -sf /var/lab/out/vmlinux-trace /var/tmp/M4/kb/vmlinux
ls -la /var/tmp/M4/kb/vmlinux-gdb.py
```

```text
ls: cannot access '/var/lab/out/linux-build-trace/vmlinux-gdb.py': No such file or directory
  GEN     scripts/gdb/linux/constants.py
make[1]: Leaving directory '/var/tmp/M4/kb'
lrwxrwxrwx 1 donh donh 53 Oct 17 23:43 /var/tmp/M4/kb/vmlinux-gdb.py -> /var/lab/src/linux-6.18.55/scripts/gdb/vmlinux-gdb.py
```

출력에서 볼 것: 약 47초(부하 상태 TCG가 아닌 VM 호스트 빌드)로 끝났다. `vmlinux-gdb.py`는 소스 쪽 파일로의 심볼릭 링크이고, 자기 위치 기준 `scripts/gdb/linux/`에서 모듈을 import하므로 **링크가 있는 디렉터리**(`/var/tmp/M4/kb`)에 `constants.py`가 있어야 한다. 이후 gdb에서는 `source /var/tmp/M4/kb/vmlinux-gdb.py`로 읽는다(자동 로드는 gdb의 `auto-load safe-path` 설정이 필요해서 명시적으로 `source` 했다).

### 2.3 이 랩의 실행 패턴

모든 gdbstub 예제는 같은 틀을 쓴다. QEMU를 `-S`(시작하자마자 정지) + `-gdb tcp::12404`(gdb 서버)로 띄우고, gdb를 batch 모드로 붙인다. `-s`는 `-gdb tcp::1234`의 줄임말이다. 다른 작성자와 포트가 겹치지 않게 12404를 썼다.

```sh
# /Users/donh/.elinux_scratch/M4/run_e.sh <gdb스크립트> <태그> "<커널 인자>"
cd /var/tmp/M4; L=/var/lab/out; G=$1; T=$2; A=$3; shift 3
(timeout 400 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -nographic \
   -kernel $L/Image-trace -initrd i.gz -append "console=ttyAMA0 $A" \
   -S -gdb tcp::12404 "$@" > q_$T.log 2>&1 &)
sleep 1; cd kb; gdb -q -batch -x $G ./vmlinux > ../$T.out 2>&1; cat ../$T.out
```

`i.gz`는 랩 BusyBox initramfs를 복사해 이 노트의 모듈(`m4_bug.ko`, `m4_hang.ko`)과 게스트 스크립트를 넣은 것이다. `LAB_CMD=`로 게스트 스크립트 하나를 실행한다.

batch 모드 gdb에서 "달리는 커널을 몇 초 뒤에 세우기"(JTAG의 Halt 버튼)는 `shell (sleep 40; kill -INT $PPID) &` 다음 `continue`로 했다. gdb가 SIGINT를 받으면 타깃에 interrupt를 보낸다. (`continue &` + `interrupt`는 batch 모드에서 비동기 이벤트가 처리되지 않아 실패했다.)

---

## 3. QEMU gdbstub = 이 랩의 JTAG

### 3.1 gdbstub, kgdb, JTAG — 누가 CPU를 세우나

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
  <rect x="15" y="20" width="200" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="115" y="43" font-size="14" text-anchor="middle">QEMU gdbstub</text>
  <rect x="230" y="20" width="200" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="330" y="43" font-size="14" text-anchor="middle">kgdb / kdb</text>
  <rect x="445" y="20" width="200" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="545" y="43" font-size="14" text-anchor="middle">JTAG / SWD</text>
  <rect x="15" y="70" width="200" height="34" fill="none" stroke="#e08a3c"/>
  <text x="25" y="92" font-size="12">멈추는 주체: QEMU 프로세스</text>
  <rect x="15" y="114" width="200" height="34" fill="none" stroke="#e08a3c"/>
  <text x="25" y="136" font-size="12">커널 설정: 필요 없음</text>
  <rect x="15" y="158" width="200" height="34" fill="none" stroke="#e08a3c"/>
  <text x="25" y="180" font-size="12">시간: 가상 시계도 멈춤</text>
  <rect x="15" y="202" width="200" height="34" fill="none" stroke="#e08a3c"/>
  <text x="25" y="224" font-size="12">실보드: 없음 (랩 전용)</text>
  <rect x="230" y="70" width="200" height="34" fill="none" stroke="#4a7bd0"/>
  <text x="240" y="92" font-size="12">멈추는 주체: 커널 자신</text>
  <rect x="230" y="114" width="200" height="34" fill="none" stroke="#4a7bd0"/>
  <text x="240" y="136" font-size="12">커널 설정: KGDB + 폴링 UART</text>
  <rect x="230" y="158" width="200" height="34" fill="none" stroke="#4a7bd0"/>
  <text x="240" y="180" font-size="12">시간: 카운터는 계속 간다</text>
  <rect x="230" y="202" width="200" height="34" fill="none" stroke="#4a7bd0"/>
  <text x="240" y="224" font-size="12">실보드: UART만 있으면 됨</text>
  <rect x="445" y="70" width="200" height="34" fill="none" stroke="#d0564a"/>
  <text x="455" y="92" font-size="12">멈추는 주체: 코어 자체</text>
  <rect x="445" y="114" width="200" height="34" fill="none" stroke="#d0564a"/>
  <text x="455" y="136" font-size="12">커널 설정: 심볼만 있으면</text>
  <rect x="445" y="158" width="200" height="34" fill="none" stroke="#d0564a"/>
  <text x="455" y="180" font-size="12">시간: SoC 설정에 따름</text>
  <rect x="445" y="202" width="200" height="34" fill="none" stroke="#d0564a"/>
  <text x="455" y="224" font-size="12">실보드: 프로브와 핀 필요</text>
  <text x="15" y="272" font-size="13">공통: 호스트의 gdb + vmlinux 심볼 + scripts/gdb(lx-*)는 그대로 쓴다</text>
</svg>
```

그림 2 — 세 가지 "커널 세우기"의 비교. gdbstub과 JTAG는 커널 바깥에서 세우고, kgdb는 커널이 자기 자신을 세운다. 그래서 kgdb는 커널이 망가지면(IRQ 마스크 hang, 콘솔 드라이버 고장) 못 쓴다.

QEMU의 gdbstub은 gdb의 Remote Serial Protocol(RSP — gdb가 원격 타깃과 주고받는 텍스트 패킷 규약) 서버가 QEMU 안에 들어 있는 것이다. OpenOCD가 JTAG 프로브 앞에서 하던 역할을 QEMU가 직접 한다. 각 vCPU는 gdb에서 thread 하나로 보인다. 그래서 JTAG 연습 대용으로 아주 좋다. 다만 부작용(시간)은 실보드와 다르다(§8).

### 3.2 예제 2 — `start_kernel`에서 세우고, 부팅 뒤 Halt해서 `lx-*`로 둘러보기

무엇을 확인하나: 리셋 상태에서 붙어 `start_kernel`에 하드웨어 브레이크포인트를 걸고, 그다음 부팅이 끝난 커널을 40초 뒤 세워서 CPU별 상태, 현재 task, per-CPU 변수, 모듈, dmesg, 디바이스 목록을 gdb 안에서 읽는다. 게스트는 `m4_bug.ko`를 올리고 잔다.

```text
# e1.gdb
set pagination off
set confirm off
target remote :12404
source /var/tmp/M4/kb/vmlinux-gdb.py
hbreak start_kernel
continue
bt
info registers SCTLR_EL1 TTBR1_EL1
delete
shell (sleep 40; kill -INT $PPID) &
continue
info threads
lx-version
p $lx_current().comm
p $lx_current().pid
lx-cpus
p $lx_per_cpu(runqueues, 0).nr_running
p $lx_per_cpu(runqueues, 1).nr_running
p $lx_per_cpu(runqueues, 1).curr->comm
info registers TTBR0_EL1 TTBR1_EL1
lx-lsmod
pipe lx-ps | head -6
pipe lx-dmesg | tail -4
pipe lx-device-list-bus platform | head -6
monitor quit
```

```sh
run_e.sh e1.gdb e1 "nokaslr LAB_CMD=insmod~/m4_bug.ko;sleep~600"
```

```text
0x0000000040000000 in ?? ()
Hardware assisted breakpoint 1 at 0xffff8000833709b0: file /var/lab/src/linux-6.18.55/init/main.c, line 871.

Thread 1 hit Breakpoint 1, start_kernel () at /var/lab/src/linux-6.18.55/init/main.c:871
871	{
#0  start_kernel () at /var/lab/src/linux-6.18.55/init/main.c:871
#1  0xffff800083379e30 in __primary_switched () at /var/lab/src/linux-6.18.55/arch/arm64/kernel/head.S:246
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
SCTLR_EL1      0x200000034f4d91d   144115188964317469
TTBR1_EL1      0x43567000          1129738240

Thread 2 received signal SIGINT, Interrupt.
[Switching to Thread 1.2]
cpu_do_idle () at /var/lab/src/linux-6.18.55/arch/arm64/kernel/idle.c:32
32		arm_cpuidle_restore_irq_context(&context);
  Id   Target Id                    Frame 
  1    Thread 1.1 (CPU#0 [halted ]) cpu_do_idle () at /var/lab/src/linux-6.18.55/arch/arm64/kernel/idle.c:32
* 2    Thread 1.2 (CPU#1 [halted ]) cpu_do_idle () at /var/lab/src/linux-6.18.55/arch/arm64/kernel/idle.c:32
Linux version 6.18.55-trace (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP PREEMPT Sat Oct 17 23:37:47 PDT 2026
$1 = "swapper/1\000\000\000\000\000\000"
$2 = 0
Possible CPUs : [0, 1]
Present CPUs  : [0, 1]
Online CPUs   : [0, 1]
Active CPUs   : [0, 1]
$3 = 0
$4 = 0
$5 = "swapper/1\000\000\000\000\000\000"
TTBR0_EL1      0x4531a000          1160880128
TTBR1_EL1      0x34000043567000    14636699918692352
Address            Module                  Size  Used by
0xffff80007bfa0000 m4_bug                 12288  0
      TASK          PID    COMM
0xffff8000837a8b40   0   swapper/0
0xffff0000046a8000   1   init
0xffff0000046aa300   2   kthreadd
0xffff0000046ac600   3   pool_workqueue_
0xffff000004730000   4   kworker/R-rcu_g
[    8.289780]     LAB_CMD=insmod~/m4_bug.ko;sleep~600
[    8.948702] m4_bug: loading out-of-tree module taints kernel.
[    8.950250] m4_bug: module verification failed: signature and/or required key missing - tainting kernel
[    9.007748] m4_bug: loaded, m4_fill=ffff80007bfa0068 guard@ffff80007bfa2030
dev psci:	0xffff000004417410
dev platform-bus@c000000:	0xffff000004417810
dev 9020000.fw-cfg:	0xffff000004417c10
dev a000000.virtio_mmio:	0xffff000006068010
dev a000200.virtio_mmio:	0xffff000006068410
dev a000400.virtio_mmio:	0xffff000006068810
```

(마지막 `monitor quit`이 QEMU를 끝내면서 나오는 "Remote communication error" 한 줄은 생략했다. 포인터 값은 실행마다 다르다.)

출력에서 볼 것:

- **첫 줄 `0x40000000`**: 리셋 직후 PC. QEMU가 RAM 앞에 써 둔 작은 부트 stub이다(C6 §2.3). JTAG로 리셋 상태에서 붙은 것과 같다.
- **`hbreak`를 쓴 이유**: 붙은 시점(PC `0x40000000`)에는 MMU가 꺼져 있어서 `start_kernel`의 가상 주소에 BRK를 써 넣을 메모리 매핑이 아직 없다. 하드웨어 브레이크포인트(디버그 레지스터의 주소 비교기)는 메모리를 안 건드리고, 나중에 CPU가 그 가상 주소를 실행하는 순간 맞는다.
- **`start_kernel` 시점 TTBR1_EL1 = 0x43567000**(물리 주소의 페이지 테이블), 부팅 후 TTBR1 = 0x34000043567000. 위쪽 비트(bit 48 이상)는 ASID다. 커널 공간 페이지 테이블 base 자체는 같다(같은 `swapper_pg_dir`).
- **CPU = thread**: 두 vCPU 모두 `cpu_do_idle`(WFI)에 있다. 게스트가 `sleep 600` 중이니 할 일이 없다. `$lx_current()`는 **선택된 thread(CPU)의** `current`다 — CPU1의 idle task `swapper/1`, PID 0. 런큐 길이 0과 `curr->comm`이 같은 이야기를 한다.
- **`lx-dmesg`**: 콘솔이 죽은 보드에서도 커널 로그 버퍼를 메모리에서 직접 읽는다. M3 §끝에서 "로그가 아예 없다면 JTAG로 `__log_buf`를 읽는다"고 한 것이 바로 이것이다.
- **`lx-device-list-bus platform`**: 드라이버 모델(G 모듈)의 `struct device` 목록을 메모리에서 걷는다. 장치가 probe 됐는지 sysfs 없이 확인할 수 있다.

### 3.3 예제 3 — MMU 켜기 전 코드: 물리 주소로 심볼 다시 로드하기

무엇을 확인하나: `head.S`의 앞부분은 MMU가 꺼진 상태에서 **물리 주소**로 돈다. 그런데 `vmlinux`의 심볼은 전부 가상 주소다. JTAG에서 하던 대로 "같은 ELF를 물리 로드 주소만큼 옮겨서" 다시 로드하면 MMU 전 코드에도 심볼 이름으로 브레이크포인트를 걸 수 있다. 오프셋 = 물리 로드 주소 − 링크 주소 = `0x40200000 − 0xffff800080000000` = (64비트로 감아서) `0x7fffc0200000`.

```text
# e1b.gdb
set pagination off
set confirm off
target remote :12404
# 1) virtual-address breakpoint before the MMU is on: would never hit early
hbreak primary_entry
info breakpoints
delete
# 2) reload the same vmlinux shifted to its *physical* load address
symbol-file
symbol-file ./vmlinux -o 0x7fffc0200000
hbreak primary_entry
hbreak __primary_switch
continue
printf "pc=%#lx  SCTLR_EL1.M=%d\n", $pc, $SCTLR_EL1 & 1
continue
printf "pc=%#lx  SCTLR_EL1.M=%d\n", $pc, $SCTLR_EL1 & 1
x/3i $pc
# 3) step over __enable_mmu
nexti 3
printf "after bl __enable_mmu: pc=%#lx SCTLR_EL1.M=%d TTBR1=%#lx\n", $pc, $SCTLR_EL1 & 1, $TTBR1_EL1
# 4) back to virtual symbols for the rest of the boot
delete
symbol-file ./vmlinux
hbreak __primary_switched
continue
printf "pc=%#lx\n", $pc
monitor xp /2gx 0x40200000
x/2gx 0xffff800080000000
monitor quit
```

```text
0x0000000040000000 in ?? ()
Hardware assisted breakpoint 1 at 0xffff8000833630a0
Num     Type           Disp Enb Address            What
1       hw breakpoint  keep y   0xffff8000833630a0 <primary_entry>
Hardware assisted breakpoint 2 at 0x435630a0
Hardware assisted breakpoint 3 at 0x435635e4

Thread 1 hit Breakpoint 2, 0x00000000435630a0 in primary_entry ()
pc=0x435630a0  SCTLR_EL1.M=0

Thread 1 hit Breakpoint 3, 0x00000000435635e4 in __primary_switch ()
pc=0x435635e4  SCTLR_EL1.M=0
=> 0x435635e4 <__primary_switch+4>:	adrp	x1, 0x43566000
   0x435635e8 <__primary_switch+8>:	adrp	x2, 0x43690000
   0x435635ec <__primary_switch+12>:	bl	0x43563570 <__enable_mmu>
0x00000000435635f0 in __primary_switch ()
after bl __enable_mmu: pc=0x435635f0 SCTLR_EL1.M=1 TTBR1=0x43566000
Hardware assisted breakpoint 4 at 0xffff800083379da8: file /var/lab/src/linux-6.18.55/arch/arm64/kernel/head.S, line 220.

Thread 1 hit Breakpoint 4, __primary_switched () at /var/lab/src/linux-6.18.55/arch/arm64/kernel/head.S:220
220	SYM_FUNC_START_LOCAL(__primary_switched)
pc=0xffff800083379da8
0000000040200000: 0x14cd8c27fa405a4d 0x0000000000000000
0xffff800080000000 <_text>:	0x14cd8c27fa405a4d	0x0000000000000000
```

출력에서 볼 것:

- 1번 브레이크포인트는 가상 주소 `0xffff8000833630a0`에 걸렸다. MMU가 꺼진 동안 PC는 `0x435630a0`이므로 **절대 안 맞는다**(지웠으니 출력에는 안 나왔지만, 그대로 두면 그냥 지나간다). Cortex-M에서 하던 대로 `.map` 주소에 걸면 안 되는 첫 번째 장면이다.
- 오프셋을 준 `symbol-file`로 같은 심볼이 `0x435630a0`(물리)로 바뀌었고, 거기서 멈췄다. SCTLR_EL1.M = 0 → MMU off 확인.
- `__primary_switch`는 +4에서 멈췄다. 함수 첫 명령은 BTI 랜딩 패드(`bti c`)라서 gdb가 프롤로그 다음에 걸었다.
- `bl __enable_mmu`를 `nexti`로 넘기자 SCTLR_EL1.M = 1. 그런데 PC는 여전히 `0x435635f0`(물리)다. idmap(가상 = 물리인 임시 매핑) 위에서 돌기 때문이다. 그 뒤 `__pi_early_map_kernel`이 커널을 가상 주소에 매핑하고 `br x8`로 `__primary_switched`(가상)로 뛴다. 원래 심볼로 돌아와서 거기서 멈췄다.
- 마지막 두 줄: 같은 8바이트(`MZ` 헤더 `0x...5a4d`)를 물리 주소 `0x40200000`(QEMU 모니터 `xp`)와 가상 주소 `_text`(gdb `x`)로 읽었다. JTAG에서 AXI-AP로 물리 메모리를 읽는 것과 코어를 통해 가상 주소로 읽는 것의 차이다(§9).

### 3.4 예제 4 — KASLR 커널에 붙기: VBAR_EL1로 오프셋 구하기

무엇을 확인하나: `nokaslr` 없이 부팅하면 `vmlinux`의 주소가 다 틀린다. 6.18의 `lx-symbols`는 KASLR 오프셋을 s390에서만 자동으로 구한다(`scripts/gdb/linux/symbols.py`의 `get_kerneloffset()`이 s390 분기뿐). arm64에서는 직접 구해야 한다. 런타임 예외 벡터 주소 `VBAR_EL1`에서 `vmlinux`의 `vectors` 주소를 빼면 오프셋이다.

```text
# e1c.gdb
set pagination off
set confirm off
target remote :12404
hbreak start_kernel
shell (sleep 45; kill -INT $PPID) &
continue
info symbol $pc
printf "VBAR_EL1=%#lx  &vectors(vmlinux)=%#lx\n", $VBAR_EL1, &vectors
python off = int(gdb.parse_and_eval("$VBAR_EL1")) - int(gdb.parse_and_eval("(unsigned long)&vectors")); gdb.execute("set $kaslr = %d" % off)
printf "KASLR offset = %#lx\n", $kaslr
delete
symbol-file
eval "symbol-file ./vmlinux -o %#lx", $kaslr
info symbol $pc
bt 3
source /var/tmp/M4/kb/vmlinux-gdb.py
lx-lsmod
monitor quit
```

```text
0x0000000040000000 in ?? ()
Hardware assisted breakpoint 1 at 0xffff8000833709b0: file /var/lab/src/linux-6.18.55/init/main.c, line 871.

Thread 2 received signal SIGINT, Interrupt.
[Switching to Thread 1.2]
0xffffd3a6fa101d48 in ?? ()
No symbol matches $pc.
VBAR_EL1=0xffffd3a6f8a11000  &vectors(vmlinux)=0xffff800080011000
KASLR offset = 0x53a678a00000
cpu_do_idle + 8 in section .text
#0  cpu_do_idle () at /var/lab/src/linux-6.18.55/arch/arm64/kernel/idle.c:32
#1  0xffffd3a6fa101d60 in arch_cpu_idle () at /var/lab/src/linux-6.18.55/arch/arm64/kernel/idle.c:44
#2  0xffffd3a6fa101f10 in default_idle_call () at /var/lab/src/linux-6.18.55/kernel/sched/idle.c:122
Address            Module                  Size  Used by
0xffffd3a6e6c4d000 m4_bug                 12288  0
```

게스트 콘솔 로그에서:

```text
[    0.000000][    T0] KASLR enabled
[    0.000000][    T0] CPU features: kernel page table isolation forced ON by KASLR
```

출력에서 볼 것:

- `hbreak start_kernel`은 **한 번도 안 걸렸다**. arm64는 `start_kernel` 전에(`__pi_early_map_kernel`에서) 커널을 무작위 주소로 옮기기 때문에 링크 주소의 `start_kernel`은 실행되지 않는다. 45초 뒤 강제로 세웠을 때 PC `0xffffd3a6fa101d48`은 "모르는 주소"였다.
- 오프셋 `0x53a678a00000`(2 MB 정렬)을 주고 `symbol-file`을 다시 읽자 같은 PC가 `cpu_do_idle + 8`이 됐고 소스 줄까지 나온다. 이후 `lx-lsmod`도 동작한다. 모듈 영역도 KASLR과 함께 옮겨졌다(`0xffffd3a6e6c4d000`).
- 실무 팁: 커널 쪽 협조가 가능하면 `/proc/kallsyms`의 `_text`와 `System.map`의 `_text` 차이로도 같은 값이 나온다. 디버깅용 부팅이면 그냥 `nokaslr`를 준다(M3 §7).

QEMU virt는 `-kernel` 부팅에서 DT의 `/chosen/kaslr-seed`를 채워 주므로 이 랩에서도 KASLR이 실제로 켜진다. 실보드는 부트로더가 seed를 넣어야 켜진다.

### 3.5 가상 주소와 물리 주소로 같은 메모리 읽기

예제 2의 같은 세션에서 `linux_banner`를 두 방식으로 읽었다.

```text
p &linux_banner
lx-virt_to_phys 0xffff80008229c810
x/s linux_banner
maint packet Qqemu.PhysMemMode:1
x/s 0x4249c810
```

```text
$6 = (const char (*)[]) 0xffff80008229c810 <linux_banner>
virt_to_phys(0xffff80008229c810) = 0x4249c810
0xffff80008229c810 <linux_banner>:	"Linux version 6.18.55-trace (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP PREEMPT Sat Oct 17 23:37:47 PDT 2026\n"
sending: Qqemu.PhysMemMode:1
received: ""
0x4249c810:	<error: Cannot access memory at address 0x4249c810>
```

출력에서 볼 것: `lx-virt_to_phys`가 커널의 선형 매핑 규칙으로 물리 주소를 계산했다(0x4249c810 = 0x40200000 + (0x...8229c810 − 0x...80000000)). 그런데 QEMU의 "물리 메모리 모드" 패킷은 이 빌드(QEMU 10.2.1, arm)에서 빈 응답(= 지원 안 함)이 왔다. 그래서 물리 주소는 예제 3처럼 **`monitor xp`**(QEMU 모니터 명령을 gdb로 보내기)로 읽는 것이 확실하다. JTAG에서도 같은 구분이 있다: 코어를 통해 읽으면 MMU가 번역한 가상 주소, MEM-AP(AXI-AP)로 읽으면 물리 주소(§9).

---

## 4. 모듈 디버깅 — 심볼 로딩과 워치포인트

### 4.1 직관 — 링크는 insmod 때 끝난다

`.ko`는 링크가 덜 된 ELF(relocatable object)다. 모든 섹션 주소가 0이고, `insmod` 때 커널의 모듈 로더가 메모리를 잡아 섹션을 배치하고 재배치를 푼다. 그래서 디버거는 "어느 섹션이 어디에 갔는지"를 알아야 모듈 심볼을 쓸 수 있다. 방법은 둘이다.

- 손으로: `/sys/module/<이름>/sections/.text` 등을 읽어 `add-symbol-file m.ko <.text> -s .data <주소> -s .bss <주소> ...` (M3 §6.4).
- 자동: `lx-symbols <모듈 경로>`. 이미 올라온 모듈은 `modules` 리스트를 걸어 다 로드하고, `do_init_module`에 내부 브레이크포인트를 걸어 **새 모듈이 올라올 때마다** 자동으로 로드한다.

```svg
<svg viewBox="0 0 660 320" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="230" height="120" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="32" y="52" font-size="13">m4_bug.ko (섹션 주소 = 0)</text>
  <text x="32" y="78" font-size="12">.text        @ 0x0</text>
  <text x="32" y="98" font-size="12">.data        @ 0x0</text>
  <text x="32" y="118" font-size="12">.bss         @ 0x0</text>
  <text x="32" y="138" font-size="12">.init.text   @ 0x0</text>
  <line x1="255" y1="90" x2="375" y2="90" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah5)"/>
  <text x="262" y="80" font-size="12">insmod: 로더가 배치</text>
  <rect x="380" y="30" width="265" height="120" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="392" y="52" font-size="13">커널 메모리 (모듈 영역)</text>
  <text x="392" y="78" font-size="12">.text   0xffff80007bfa0060</text>
  <text x="392" y="98" font-size="12">.data   0xffff80007bfa2020</text>
  <text x="392" y="118" font-size="12">.bss    0xffff80007bfa2038</text>
  <text x="392" y="138" font-size="12">.rodata 0xffff80007bfa4068</text>
  <rect x="20" y="200" width="250" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="225" font-size="12">/sys/module/m4_bug/sections/*</text>
  <rect x="300" y="200" width="230" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="310" y="225" font-size="12">add-symbol-file 또는 lx-symbols</text>
  <rect x="550" y="200" width="95" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="562" y="225" font-size="12">gdb 심볼</text>
  <line x1="510" y1="152" x2="150" y2="197" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
  <line x1="272" y1="220" x2="296" y2="220" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
  <line x1="532" y1="220" x2="546" y2="220" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
  <text x="20" y="285" font-size="12">lx-symbols는 do_init_module에 내부 브레이크포인트를 걸어 insmod 때마다 자동으로 한다</text>
  <text x="20" y="305" font-size="12">gdb의 ELF 심볼(함수·변수 주소)은 정확했지만, 이 랩에서 DWARF 주소는 섹션별로 맞지 않았다 (§4.4)</text>
</svg>
```

그림 3 — 모듈 심볼 로딩. 실행 중 섹션 주소(sysfs 또는 `struct module`의 `sect_attrs`)를 gdb에 알려 줘야 `.ko`의 심볼이 실제 주소를 가리킨다.

### 4.2 예제 5 — 테스트 모듈: 언젠가 guard를 덮어쓰는 kthread

무엇을 확인하나: SSD 펌웨어에서 흔했던 "배열 overrun이 옆의 구조체 필드를 덮는다" 버그를 모듈로 만든다. `m4_len` 파라미터가 5가 되는 순간 `buf[4]` = `guard`가 덮인다. 범인을 워치포인트로 잡는 것이 목표다.

```c
// m4_bug.c - a kthread that eventually corrupts a "guard" word by overrunning buf[]
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>

struct m4_state {
	int buf[4];
	int guard;              /* must stay 0x1234 */
};
static struct m4_state m4_s = { .guard = 0x1234 };
static int m4_len = 4;      /* how many slots m4_fill() writes */
module_param(m4_len, int, 0644);
static struct task_struct *m4_task;

static noinline void m4_fill(int n)
{
	int i;

	for (i = 0; i < n; i++)
		((int *)m4_s.buf)[i] = 0xa0 + i;   /* n == 5 -> writes guard */
}

static noinline int m4_tick(int iter)
{
	m4_fill(READ_ONCE(m4_len));
	return m4_s.guard;
}

static int m4_thread(void *arg)
{
	int iter = 0, g;

	while (!kthread_should_stop()) {
		g = m4_tick(iter++);
		if (g != 0x1234)
			pr_err("m4_bug: guard corrupted: %#x (iter %d)\n", g, iter);
		msleep(500);
	}
	return 0;
}

static int __init m4_init(void)
{
	pr_info("m4_bug: loaded, m4_fill=%px guard@%px\n", m4_fill, &m4_s.guard);
	m4_task = kthread_run(m4_thread, NULL, "m4_bug");
	return PTR_ERR_OR_ZERO(m4_task);
}

static void __exit m4_exit(void)
{
	kthread_stop(m4_task);
}
module_init(m4_init);
module_exit(m4_exit);
MODULE_DESCRIPTION("M4 debugger demo");
MODULE_LICENSE("GPL");
```

```sh
cd /var/tmp/M4/mod && printf 'obj-m := m4_bug.o m4_hang.o\n' > Kbuild
make -C /var/lab/out/linux-build-trace M=$PWD modules 2>&1 | grep -iE "warn|error"; modinfo -F vermagic m4_bug.ko
```

```text
6.18.55-trace SMP preempt mod_unload aarch64
```

출력에서 볼 것: 경고 0개(처음에는 `MODULE_DESCRIPTION()` 누락 modpost 경고가 나와서 추가했다). vermagic이 `6.18.55-trace`라서 `Image-trace`에만 올린다. `((int *)m4_s.buf)[i]`로 쓴 이유는 `m4_s.buf[4]`라고 쓰면 컴파일러가 `-Warray-bounds`로 잡아 주기 때문이다 — 실무 버그는 대개 이렇게 포인터로 우회돼 있어서 컴파일러가 못 본다.

게스트 스크립트(`/m4_e2.sh`)는 모듈을 올리고, 섹션 주소와 kallsyms를 찍고, 60초 뒤 `m4_len`을 5로 바꾼다.

```sh
#!/bin/sh
insmod /m4_bug.ko
cd /sys/module/m4_bug/sections
echo "sections: .text=$(cat .text) .data=$(cat .data) .bss=$(cat .bss)"
grep -w m4_s /proc/kallsyms
sleep 60
echo "guest: setting m4_len=5"
echo 5 > /sys/module/m4_bug/parameters/m4_len
sleep 600
```

### 4.3 예제 6 — `lx-symbols` + 하드웨어 워치포인트로 덮어쓴 놈 잡기

무엇을 확인하나: 부팅 초기에 `lx-symbols`를 걸어 두면 insmod 순간 심볼이 자동으로 들어온다. 그다음 `m4_fill`에 브레이크포인트, `guard` 주소에 하드웨어 워치포인트(쓰기 감시)를 건다.

```text
# e2.gdb
set pagination off
set confirm off
target remote :12404
source /var/tmp/M4/kb/vmlinux-gdb.py
hbreak start_kernel
continue
delete
lx-symbols /var/tmp/M4/mod
shell (sleep 45; kill -INT $PPID) &
continue
info line m4_fill
p &m4_s
info symbol 0xffff80007bfa2020
p/x *(struct m4_state *)0xffff80007bfa2020
break m4_fill
continue
bt 4
info symbol $pc
delete
watch -l *(int *)0xffff80007bfa2030
continue
bt 3
info symbol $pc
x/2i $pc-4
p/x *(struct m4_state *)0xffff80007bfa2020
p $lx_current().comm
p $lx_current().pid
info threads
monitor quit
```

```text
loading vmlinux
scanning for modules in /var/tmp/M4/mod
scanning for modules in /var/tmp/M4/kb
loading @0xffff80007bfa0000: /var/tmp/M4/mod/m4_bug.ko

Thread 2 received signal SIGINT, Interrupt.
[Switching to Thread 1.2]
cpu_do_idle () at /var/lab/src/linux-6.18.55/arch/arm64/kernel/idle.c:32
32		arm_cpuidle_restore_irq_context(&context);
Line 43 of "m4_bug.c" starts at address 0xffff80007bfa0068 <m4_fill> and ends at 0xffff80007bfa0070 <m4_exit+16>.
$1 = (struct m4_state *) 0x0
m4_s in section .data of /var/tmp/M4/mod/m4_bug.ko
$2 = {buf = {0xa0, 0xa1, 0xa2, 0xa3}, guard = 0x1234}
Breakpoint 2 at 0xffff80007bfa0068: file m4_bug.c, line 43.

Thread 2 hit Breakpoint 2, m4_exit () at m4_bug.c:43
43	{
#0  m4_exit () at m4_bug.c:43
#1  0xffff80007bfa00d0 in m4_init () at m4_bug.c:45
#2  0xffff80007bfa011c in m4_thread (arg=<optimized out>) at m4_bug.c:34
#3  0xffff800080115c9c in kthread (_create=<optimized out>) at /var/lab/src/linux-6.18.55/kernel/kthread.c:432
m4_fill in section .text of /var/tmp/M4/mod/m4_bug.ko
Hardware watchpoint 3: -location *(int *)0xffff80007bfa2030

Thread 2 hit Hardware watchpoint 3: -location *(int *)0xffff80007bfa2030

Old value = 4660
New value = 164
m4_init () at m4_bug.c:19
19		for (i = 0; i < n; i++)
#0  m4_init () at m4_bug.c:19
#1  0xffff80007bfa00d0 in m4_init () at m4_bug.c:45
#2  0xffff80007bfa011c in m4_thread (arg=<optimized out>) at m4_bug.c:34
m4_fill + 40 in section .text of /var/tmp/M4/mod/m4_bug.ko
   0xffff80007bfa008c <m4_init+36>:	str	w2, [x3, x1, lsl #2]
=> 0xffff80007bfa0090 <m4_init+40>:	add	x1, x1, #0x1
$3 = {buf = {0xa0, 0xa1, 0xa2, 0xa3}, guard = 0xa4}
$4 = "m4_bug\000\000\000\000\000\000\000\000\000"
$5 = 78
  Id   Target Id                    Frame 
  1    Thread 1.1 (CPU#0 [halted ]) cpu_do_idle () at /var/lab/src/linux-6.18.55/arch/arm64/kernel/idle.c:32
* 2    Thread 1.2 (CPU#1 [running]) m4_init () at m4_bug.c:19
```

게스트 콘솔:

```text
[    6.842808][   T78] m4_bug: loaded, m4_fill=ffff80007bfa0068 guard@ffff80007bfa2030
sections: .text=0xffff80007bfa0060 .data=0xffff80007bfa2020 .bss=0xffff80007bfa2038
ffff80007bfa2020 d m4_s	[m4_bug]
guest: setting m4_len=5
[   71.553452][   T79] m4_bug: guard corrupted: 0xa4 (iter 127)
```

(시작 부분 `start_kernel` 정지 줄과 마지막 연결 종료 줄은 생략했다. 이 출력은 같은 스크립트의 두 번 실행에서 나온 것이라 gdb 쪽과 콘솔 쪽 PID가 1 다를 수 있다.)

출력에서 볼 것:

- **워치포인트가 범인을 잡았다**: Old 4660(0x1234) → New 164(0xa4). 멈춘 곳은 `m4_fill + 40`, 직전 명령 `str w2, [x3, x1, lsl #2]`(= `buf[i] = ...`, i = 4). 콜스택은 `m4_thread`(kthread) → `m4_tick` → `m4_fill`, 문맥은 `m4_bug` kthread(PID 78) on CPU1.
- **워치포인트는 쓰기 "후"에 보고된다**: PC가 `str` 다음 명령에 있다. ARM의 워치포인트 예외는 명령 완료 전에 나지만 gdb가 그 명령을 한 스텝 실행한 뒤 Old/New 값을 보여 준다. 그래서 "범인은 PC 바로 앞 명령"으로 읽는다.
- **`watch -l`(location)**: 표현식이 아니라 그 주소를 감시한다. 하드웨어 워치포인트라서 커널이 전속력으로 도는 동안 비용이 0이다. Cortex-A57은 브레이크포인트 6개, 워치포인트 4개다(`ID_AA64DFR0_EL1 = 0x10305106`: BRPs 필드 5 → 6개, WRPs 필드 3 → 4개, §8.4의 세션에서 읽음). 소프트웨어 워치포인트(매 명령 스텝)는 커널에서 사실상 못 쓴다.
- **그런데 이상한 것들이 있다**: `p &m4_s`가 `0x0`, `info line m4_fill`이 "line 43", 콜스택의 함수 이름이 `m4_exit`/`m4_init`. 실제로는 `m4_fill`(`info symbol`이 맞게 말한다)이다. 다음 절에서 본다.

### 4.4 이 랩에서 만난 함정 — `.ko`의 DWARF 주소가 섹션별로 안 맞는다

무엇을 확인하나: 타깃 없이 `.ko`만 gdb에 넣어도 같은 현상이 나는지, 섹션 주소를 다 줘도 그런지 본다.

```sh
cd /var/tmp/M4/mod
gdb -q -batch -ex "add-symbol-file m4_bug.ko -s .text 0x10000 -s .data 0x20000 -s .bss 0x30000" \
  -ex "info address m4_s" -ex "info address m4_len" -ex "info address m4_task" \
  -ex "info line m4_fill" -ex "info address m4_tick" 2>&1 | grep -v "^add symbol\|_addr"
nm m4_bug.ko | grep -E " m4_(s|len|fill|init|exit)$"
```

```text
Symbol "m4_s" is static storage at address 0x0.
Symbol "m4_len" is static storage at address 0x10014.
Symbol "m4_task" is static storage at address 0x0.
Line 43 of "m4_bug.c" starts at address 0x10008 <m4_fill> and ends at 0x10010 <m4_exit+16>.
Symbol "m4_tick" is a function at address 0x10048.
0000000000000000 t m4_exit
0000000000000008 t m4_fill
0000000000000008 t m4_init
0000000000000014 d m4_len
0000000000000000 d m4_s
```

출력에서 볼 것: `.data`를 0x20000에 두라고 했는데 `m4_len`(`.data`+0x14)이 **`.text` 쪽 0x10014**로 나왔고, `m4_s`(`.data`+0)는 0이다. `m4_fill`(`.text`+8)과 `m4_init`(`.init.text`+8)은 섹션 안 오프셋이 같아서 줄 번호·함수 이름이 뒤섞였다(43행은 `m4_init`의 `{`). 즉 이 환경(gdb 17.1, binutils 2.46, aarch64 `.ko`, DWARF5와 DWARF4 둘 다)에서는 **DWARF 쪽 주소가 섹션별 오프셋을 못 받았다**. 반면 ELF 심볼표(`info symbol`, `break m4_fill`의 주소, `nm`)는 정확했다. `lx-symbols`가 넘기는 `-s` 목록을 21개 섹션 전부로 늘려도(직접 만든 `m4-addsym` 명령) 결과는 같았다. 원인은 확인하지 못했다(gdb의 relocatable ELF 디버그 정보 재배치 쪽으로 추정). 다른 gdb 버전에서는 정상일 수 있으니 **결론은 "검증하라"**다.

실무 대처:

- 주소는 **ELF 심볼**에서 얻는다: 게스트의 `/proc/kallsyms`(`ffff80007bfa2020 d m4_s [m4_bug]`), gdb의 `info symbol <주소>`, `maint print msymbols`. kdb의 `md m4_s`도 kallsyms를 쓰므로 정확했다(예제 9).
- 타입은 DWARF에서 그대로 쓴다: `p/x *(struct m4_state *)0xffff80007bfa2020`처럼 주소에 캐스트하면 구조체 출력은 정확하다.
- 함수 이름·줄 번호가 이상하면 `info symbol $pc`와 `x/i $pc`로 교차 확인한다. 디버깅 세션 동안만이라면 드라이버를 **built-in(=y)으로 빌드**해 `vmlinux`에 넣으면 이 문제가 아예 없다.

### 4.5 모듈 `init` 함수 디버깅

`module_init` 함수는 `do_init_module()` → `do_one_initcall(mod->init)`에서 한 번 돌고, 끝나면 `.init.text`가 해제된다. 그래서 순서가 중요하다.

1. insmod **전에** `lx-symbols <경로>`를 실행해 둔다(예제 6처럼). insmod 순간 `do_init_module`의 내부 브레이크포인트가 심볼을 로드하고 계속 진행한다 — 예제 6 출력의 `loading @0xffff80007bfa0000` 줄이 그 순간이다.
2. 그다음에야 `break m4_init`이 의미가 있다. 손으로 할 때는 `break do_one_initcall`에서 멈춘 뒤 `p fn`으로 init 함수 주소를 얻어 `hbreak *<주소>`를 건다.
3. init이 끝난 뒤의 `.init.text` 주소는 다른 용도로 재사용될 수 있으니 그 주소의 브레이크포인트는 지운다.

(이 절의 2~3번은 설명이고 따로 실행하지 않았다.)

### 4.6 하드웨어 vs 소프트웨어 브레이크포인트

| 종류 | 원리 | 개수 | 커널에서 쓸 때 |
|---|---|---|---|
| 소프트웨어 (`break`) | 명령을 BRK로 바꿔 쓴다 | 무제한 | 텍스트가 읽기 전용 매핑이면 디버거가 물리로 쓰거나(gdbstub, JTAG) 커널이 text patching으로 쓴다(kgdb). 아직 메모리에 없는 코드(MMU 전, 모듈 로드 전)에는 못 건다 |
| 하드웨어 (`hbreak`) | 디버그 레지스터의 주소 비교기 | A57은 6개 | 부팅 초기, ROM/플래시, 재배치 중 코드에 쓴다. 가상/물리 중 "지금 CPU가 내는 주소"와 비교한다 |
| 워치포인트 (`watch -l`) | 데이터 주소 비교기 | A57은 4개 | 메모리 오염 범인 잡기. 크기·정렬 제한(최대 8바이트 단위로 묶임) |

---

## 5. kgdb와 kdb — 커널 안에 사는 디버거

### 5.1 구조

kgdb는 "커널 안에 들어 있는 gdb stub"이다. 커널이 예외(BRK)를 받으면 모든 CPU를 세우고, UART로 gdb와 RSP 패킷을 주고받는다. kdb는 같은 코어 위에서 사람이 직접 치는 셸이다.

```svg
<svg viewBox="0 0 660 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="40" width="230" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="32" y="62" font-size="13">호스트: gdb vmlinux</text>
  <text x="32" y="84" font-size="12">target remote /dev/ttyUSB0</text>
  <line x1="252" y1="68" x2="283" y2="68" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
  <text x="20" y="122" font-size="12">UART 케이블 위로 RSP 패킷</text>
  <rect x="270" y="20" width="375" height="345" rx="8" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="560" y="38" font-size="12">타깃 커널</text>
  <rect x="285" y="45" width="340" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="297" y="65" font-size="13">kgdboc — I/O 드라이버</text>
  <text x="297" y="85" font-size="12">tty 드라이버의 poll_get_char / poll_put_char</text>
  <rect x="285" y="112" width="340" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="297" y="132" font-size="13">debug_core (kernel/debug)</text>
  <text x="297" y="152" font-size="12">kgdb_handle_exception → kgdb_cpu_enter</text>
  <rect x="285" y="180" width="185" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="297" y="207" font-size="13">gdbstub.c (gdb용)</text>
  <rect x="480" y="180" width="145" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="492" y="207" font-size="13">kdb (사람용 셸)</text>
  <rect x="285" y="242" width="340" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="297" y="262" font-size="13">arch/arm64/kernel/kgdb.c</text>
  <text x="297" y="282" font-size="12">BRK 명령 · 단일 스텝 · 레지스터 저장</text>
  <text x="285" y="322" font-size="12">진입: kgdbwait · SysRq-g · BRK · oops/panic</text>
  <text x="285" y="345" font-size="12">나갈 때: dbg_touch_watchdogs() (softlockup·RCU만)</text>
  <rect x="20" y="250" width="235" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="32" y="272" font-size="13">다른 CPU들</text>
  <text x="32" y="294" font-size="12">roundup IPI → 대기 루프에서 스핀</text>
  <line x1="455" y1="98" x2="455" y2="109" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
  <line x1="380" y1="164" x2="380" y2="177" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
  <line x1="550" y1="164" x2="550" y2="177" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
  <line x1="455" y1="240" x2="455" y2="165" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
  <line x1="257" y1="278" x2="283" y2="150" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
</svg>
```

그림 4 — kgdb 구조. 예외가 나면 arch 코드가 debug_core로 넘기고, debug_core가 다른 CPU를 IPI로 불러 세운 뒤, gdbstub 또는 kdb가 kgdboc를 통해 UART 한 글자씩 폴링으로 주고받는다.

핵심 포인트:

- **kgdboc**("kgdb over console")는 기존 tty 드라이버의 **폴링 콜백**(`CONFIG_CONSOLE_POLL`의 `poll_get_char`/`poll_put_char`)을 빌려 쓴다. 디버거 안에서는 인터럽트가 꺼져 있으니 IRQ 기반 수신을 못 쓰기 때문이다. 그래서 그 UART 드라이버가 폴링 콜백을 구현해야 한다. PL011(`ttyAMA`)과 8250(`ttyS`)은 구현돼 있다.
- **진입 경로**: `kgdbwait`(부팅 중 kgdboc 등록 순간 대기), SysRq-g, 걸어 둔 브레이크포인트(BRK), oops/panic(`kgdb_panic`, L7 그림의 그 지점).
- **다른 CPU 세우기**: `kgdb_roundup_cpus()`가 IPI를 보내 다른 CPU를 대기 루프에 묶는다. arm64에 pseudo-NMI가 없으면 **IRQ가 마스크된 CPU는 이 IPI를 못 받는다** — §7의 hang 시나리오에서 kgdb가 약한 이유다.
- **나갈 때**: `dbg_touch_watchdogs()`가 `touch_softlockup_watchdog_sync()`, `clocksource_touch_watchdog()`, `rcu_cpu_stall_reset()`을 부른다(`kernel/debug/debug_core.c`). 멈춘 시간 때문에 softlockup·RCU stall 경고가 나지 않게 하는 것이다. **workqueue watchdog은 여기 없다** — 예제 7에서 실제로 걸린다.
- **kgdbts**(`CONFIG_KGDB_TESTS`)는 kgdb 자체 테스트 모듈인데 `Image-trace` 설정에서 꺼져 있어(`# CONFIG_KGDB_TESTS is not set`) 돌리지 않았다.
- `CONFIG_KGDB_HONOUR_BLOCKLIST=y`: kprobe 금지 목록(`NOKPROBE_SYMBOL`)에 있는 함수에는 kgdb 브레이크포인트를 못 건다. 디버거 자기 경로에 BRK를 넣으면 재귀로 죽기 때문이다.

### 5.2 예제 7 — kgdb를 두 번째 UART(PCI 16550)로: `kgdbwait`, SysRq-g, 브레이크포인트

무엇을 확인하나: 콘솔(ttyAMA0)은 그대로 두고, QEMU에 PCI 16550 UART(`-device pci-serial`)를 하나 더 붙여 TCP 소켓으로 내보낸다. 게스트에서는 `ttyS0`이 되고 `kgdboc=ttyS0,115200 kgdbwait`로 부팅 중 gdb를 기다리게 한다. 두 번째 포트는 `-device pci-serial`(PCI 16550, `CONFIG_SERIAL_8250_PCI=y`)로 만들었다. virt 머신에 PL011을 하나 더 다는 방법은 QEMU 버전마다 달라서 어느 버전에서나 되는 PCI 방식을 골랐다.

```sh
# run_k.sh: console on ttyAMA0 (log file), kgdb on PCI 16550 ttyS0 exported as TCP 12414
(timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -nographic \
  -kernel $L/Image-trace -initrd i.gz \
  -append "console=ttyAMA0 nokaslr kgdboc=ttyS0,115200 kgdbwait LAB_CMD=/m4_e3.sh" \
  -chardev socket,id=k0,host=127.0.0.1,port=12414,server=on,wait=off \
  -device pci-serial,chardev=k0 > q_e3.log 2>&1 &)
for n in $(seq 120); do grep -q "Waiting for connection" q_e3.log && break; sleep 1; done
cd kb; timeout 250 gdb -q -batch -x e3.gdb ./vmlinux
```

게스트 스크립트 `/m4_e3.sh`는 `m4_bug.ko`를 올리고 15초 뒤 `echo g > /proc/sysrq-trigger`로 kgdb에 들어간다.

```text
# e3.gdb
set pagination off
set confirm off
target remote tcp:127.0.0.1:12414
bt 4
pipe info threads | head -4
pipe info threads | wc -l
continue
bt 8
source /var/tmp/M4/kb/vmlinux-gdb.py
pipe info threads | grep "^\*"
lx-lsmod
monitor lsmod
lx-symbols /var/tmp/M4/mod
break m4_fill
continue
bt 4
info registers pc sp
delete
detach
quit
```

```text
arch_kgdb_breakpoint () at /var/lab/src/linux-6.18.55/arch/arm64/include/asm/kgdb.h:21
21		asm ("brk %0" : : "I" (KGDB_COMPILED_DBG_BRK_IMM));
#0  arch_kgdb_breakpoint () at /var/lab/src/linux-6.18.55/arch/arm64/include/asm/kgdb.h:21
#1  kgdb_breakpoint () at /var/lab/src/linux-6.18.55/kernel/debug/debug_core.c:1213
#2  0xffff8000802355a8 in kgdb_initial_breakpoint () at /var/lab/src/linux-6.18.55/kernel/debug/debug_core.c:1015
#3  kgdb_register_io_module (new_dbg_io_ops=new_dbg_io_ops@entry=0xffff800083c3ac78 <kgdboc_io_ops>) at /var/lab/src/linux-6.18.55/kernel/debug/debug_core.c:1152
  Id   Target Id                      Frame 
  1    Thread 4294967294 (shadowCPU0) default_idle_call () at /var/lab/src/linux-6.18.55/kernel/sched/idle.c:130
  2    Thread 4294967293 (shadowCPU1) arch_kgdb_breakpoint () at /var/lab/src/linux-6.18.55/arch/arm64/include/asm/kgdb.h:21
* 3    Thread 1 (swapper/0)           arch_kgdb_breakpoint () at /var/lab/src/linux-6.18.55/arch/arm64/include/asm/kgdb.h:21
61
[New Thread 76]
...
Thread 61 received signal SIGTRAP, Trace/breakpoint trap.
[Switching to Thread 76]
kgdb_breakpoint () at /var/lab/src/linux-6.18.55/kernel/debug/debug_core.c:1213
1213		arch_kgdb_breakpoint();
#0  kgdb_breakpoint () at /var/lab/src/linux-6.18.55/kernel/debug/debug_core.c:1213
#1  0xffff800080234054 in sysrq_handle_dbg (key=<optimized out>) at /var/lab/src/linux-6.18.55/kernel/debug/debug_core.c:978
#2  0xffff800080cbbef4 in __handle_sysrq (key=103 'g', check_mask=check_mask@entry=false) at /var/lab/src/linux-6.18.55/drivers/tty/sysrq.c:611
#3  0xffff800080cbc680 in write_sysrq_trigger (file=<optimized out>, buf=<optimized out>, count=2, ppos=<optimized out>) at /var/lab/src/linux-6.18.55/drivers/tty/sysrq.c:1221
...
* 60   Thread 76 (m4_e3.sh)           kgdb_breakpoint () at /var/lab/src/linux-6.18.55/kernel/debug/debug_core.c:1213
Address            Module                  Size  Used by
0xffff80007bfa0000 m4_bug                 12288  0
Module                  Size  modstruct     Used by
m4_bug                  4096/    4096/       0/    4096  0xffff80007bfa2080    0  (Live) 0xffff80007bfa0000/0xffff80007bfa4000/0x0000000000000000/0xffff80007bfa2000 [ ]
loading vmlinux
scanning for modules in /var/tmp/M4/mod
scanning for modules in /var/tmp/M4/kb
loading @0xffff80007bfa0000: /var/tmp/M4/mod/m4_bug.ko
Breakpoint 1 at 0xffff80007bfa0068: file m4_bug.c, line 43.
[Switching to Thread 78]

Thread 65 hit Breakpoint 1, m4_exit () at m4_bug.c:43
43	{
#0  m4_exit () at m4_bug.c:43
#1  0xffff80007bfa00d0 in m4_init () at m4_bug.c:45
#2  0xffff80007bfa011c in m4_thread (arg=<optimized out>) at m4_bug.c:34
#3  0xffff800080115c9c in kthread (_create=<optimized out>) at /var/lab/src/linux-6.18.55/kernel/kthread.c:432
pc             0xffff80007bfa0068  0xffff80007bfa0068 <m4_fill>
sp             0xffff80008447be30  0xffff80008447be30
[Inferior 1 (Remote target) detached]
```

(`[New Thread ...]` 줄 여러 개와 `bt 8`의 뒤 4프레임은 `...`로 생략했다. 두 번의 실행 출력을 합쳤다 — 첫 실행은 `$lx_current()`에서 멈춰서 둘째 실행에서 바꿨다.)

게스트 콘솔:

```text
[    9.274810][    T1] KGDB: Registered I/O driver kgdboc
[    9.280183][    T1] KGDB: Waiting for connection from remote gdb...
[  186.197523][    T1] loop: module loaded
...
[  204.570844][   T76] sysrq: DEBUG
guest: sysrq-g now
guest: back from kgdb
```

출력에서 볼 것:

- **`kgdbwait`의 첫 정지**: `kgdb_register_io_module()` → `kgdb_initial_breakpoint()` → `brk`. kgdboc가 등록되는 순간(PCI UART probe 직후)이다. 이보다 앞선 부팅 코드는 kgdb로 못 본다 — 더 이르게는 `kgdboc_earlycon=`(earlycon 드라이버가 폴링을 지원할 때)이나 JTAG/gdbstub이 필요하다.
- **thread = task**: gdbstub에서는 thread가 CPU였는데 kgdb에서는 **모든 task가 thread**다(61개). CPU별 문맥은 `shadowCPU0/1`이라는 가짜 thread로 보인다. 그래서 `$lx_current()`(선택된 thread 번호를 CPU 번호로 쓰는 헬퍼)는 kgdb에서 "Invalid cast"로 실패했다. kgdb에서는 `info threads`의 `*` 줄이 현재 task다(`m4_e3.sh`, PID 76).
- **SysRq-g 경로**: `write_sysrq_trigger` → `__handle_sysrq('g')` → `sysrq_handle_dbg` → `kgdb_breakpoint`. 셸이 `/proc/sysrq-trigger`에 쓴 그 task 문맥에서 들어왔다.
- **`monitor lsmod`**: kgdb의 gdb 모드에서도 `monitor <kdb 명령>`으로 kdb 명령을 쓸 수 있다(KDB가 켜져 있을 때).
- **소프트웨어 브레이크포인트**: `break m4_fill`은 kgdb가 커널 텍스트에 BRK를 써 넣는다(커널의 text patching 경로). gdbstub과 달리 하드웨어 레지스터를 쓰지 않았다.
- **부팅 시간 10초 → 186초**: gdb가 `info threads`로 60개 task 정보를 패킷 수백 개로 받는 동안 kgdb는 멈춰 있었고, 그 사이에도 `arch_sys_counter`는 계속 갔다. 커널 시계 기준으로 약 3분이 지나갔다. 폴링 UART + TCG라서 느리다(실보드 115200 bps에서도 `info threads`는 느리다). §8에서 이 "시간이 계속 간다"의 결과를 본다.

### 5.3 예제 8 — 같은 세션의 대가: workqueue lockup

예제 7의 콘솔 로그를 더 보면, kgdb에서 나온 뒤 47초 만에 경고가 떴다.

```text
[  204.570844][   T76] sysrq: DEBUG
[  251.588241][    C0] BUG: workqueue lockup - pool cpus=0 node=0 flags=0x0 nice=0 stuck for 47s!
[  251.589911][    C0] Showing busy workqueues and worker pools:
[  251.590065][    C0] workqueue events: flags=0x100
[  251.590233][    C0]   pwq 2: cpus=0 node=0 flags=0x0 nice=0 active=2 refcnt=3
[  251.590479][    C0]     pending: kgdboc_restore_input_helper, vmstat_shepherd
[  251.591542][    C0] workqueue events_unbound: flags=0x2
[  251.591576][    C0]   pwq 8: cpus=0-1 flags=0x4 nice=0 active=2 refcnt=3
[  251.591686][    C0]     in-flight: 48:crng_reseed
[  251.594925][    C0]     pending: flush_memcg_stats_dwork
```

출력에서 볼 것: 두 번째 kgdb 정지(SysRq-g 후 gdb가 lx-symbols·브레이크포인트로 시간을 쓴 동안)에 CPU0의 workqueue가 47초 동안 진행하지 못했다. `dbg_touch_watchdogs()`는 softlockup과 RCU만 리셋하고 workqueue watchdog(`CONFIG_WQ_WATCHDOG`)은 리셋하지 않기 때문이다. pending 목록에 kgdb 자신이 예약한 `kgdboc_restore_input_helper`가 보이는 것이 재미있다. 이것은 버그가 아니라 **디버거가 만든 가짜 경고**다. 디버거로 오래 멈춘 뒤의 lockup 경고는 먼저 "내가 멈춰서 생긴 것인가"를 의심한다.

### 5.4 예제 9 — kdb: 콘솔 UART 하나로 모두 하기 + serial BREAK

무엇을 확인하나: 보드에 UART가 하나뿐이면 `kgdboc=ttyAMA0`처럼 콘솔과 같은 포트를 쓰고, gdb 대신 kdb 셸을 쓴다. `expect`로 게스트 셸에서 SysRq-g를 보내 kdb 명령 몇 개를 치고, 두 번째에는 **serial BREAK + g**(UART로 보내는 Magic SysRq)로 들어간다. QEMU `-nographic`의 `mon:stdio`에서는 `Ctrl-a b`가 BREAK를 보낸다.

```text
#!/usr/bin/expect -f
# kdb on the console UART (kgdboc=ttyAMA0) + sysrq via serial BREAK (QEMU mon:stdio, Ctrl-a b)
set timeout 300
log_file -noappend /var/tmp/M4/e6.log
spawn qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -nographic -kernel /var/lab/out/Image-trace -initrd /var/tmp/M4/i.gz -append "console=ttyAMA0 nokaslr kgdboc=ttyAMA0"
expect "lab initramfs"
expect "# "
send "insmod /m4_bug.ko; cat /proc/sys/kernel/sysrq\r"
expect "loaded"
expect "# "
send "echo g > /proc/sysrq-trigger\r"
expect "kdb>"
foreach c {"bt" "lsmod" "md4c5 m4_s" "rd" "dmesg 3" "ps" "go"} {
    send "$c\r"
    expect {
        "kdb>" {}
        "# " {}
        "more>" { send "q" ; exp_continue }
    }
}
sleep 1
send "\r"
expect "# "
send "\x01b"
sleep 0.3
send "g"
expect "kdb>"
send "go\r"
expect "# "
send "poweroff -f\r"
expect eof
```

```text
~ # echo g > /proc/sysrq-trigger

Entering kdb (current=0xffff0000046a8000, pid 1) on processor 0 due to Keyboard Entry
[0]kdb> bt
Stack traceback for pid 1
0xffff0000046a8000        1        0  1    0   R  0xffff0000046a9c20 *sh
CPU: 0 UID: 0 PID: 1 Comm: sh Tainted: G           OE       6.18.55-trace #1 PREEMPT 
Call trace:
 show_stack+0x30/0x90 (C)
 ...
 kgdb_cpu_enter+0x180/0x718
 kgdb_handle_exception+0xb8/0xf8
 kgdb_compiled_brk_handler+0x30/0x48
 call_el1_break_hook+0xb0/0xd8
 do_el1_brk64+0x2c/0x60
 el1_brk64+0x38/0x60
 el1h_64_sync_handler+0x68/0xb8
 el1h_64_sync+0x6c/0x70
 kgdb_breakpoint+0x20/0x88 (P)
 __handle_sysrq+0x9c/0x1b0
 write_sysrq_trigger+0xb8/0xe0
 ...
[0]kdb> lsmod
Module                  Size  modstruct     Used by
m4_bug                  4096/    4096/       0/    4096  0xffff80007bfa2080    0  (Live) 0xffff80007bfa0000/0xffff80007bfa4000/0x0000000000000000/0xffff80007bfa2000 [ ]
[0]kdb> md4c5 m4_s
0xffff80007bfa2020 000000a0 000000a1 000000a2 000000a3   ................
0xffff80007bfa2030 00001234                              4...
[0]kdb> rd
x0: ffff800083ee2000  x1: 0000000000000001  x2: 0000000000000000
...
x30: ffff800080234054  sp: ffff800083fbbc90  pc: ffff800080233c68
pstate: 60000005  v0: ??  v1: ??  v2: ??  v3: ??  v4: ??  v5: ??  v6: ??  v7: ??
[0]kdb> dmesg 3
<5>[   12.041660][   T73] m4_bug: module verification failed: signature and/or required key missing - tainting kernel
<6>[   12.119699][   T73] m4_bug: loaded, m4_fill=ffff80007bfa0068 guard@ffff80007bfa2030
<6>[   12.204318][    T1] sysrq: DEBUG
[0]kdb> ps
1 idle process (state -) and 
60 sleeping system daemon (state [ims]) processes suppressed,
use 'ps A' to see all.
Task Addr               Pid   Parent [*] cpu State Thread             Command
0xffff0000046a8000        1        0  1    0   R  0xffff0000046a9c20 *sh
0xffff00000441c600       74        2  0    0   D  0xffff00000441e220  m4_bug
[0]kdb> go
~ # 
Entering kdb (current=0xffff8000837a8b40, pid 0) on processor 0 due to Keyboard Entry
[0]kdb> go
[   13.619012][    C0] sysrq: DEBUG
```

(`bt`의 가운데 kdb 내부 프레임, `rd`의 가운데 레지스터 줄, 반복된 `ps` 헤더 줄은 생략했다.)

출력에서 볼 것:

- **kdb는 kallsyms를 쓴다**: `md4c5 m4_s`(4바이트 단위 5개)가 정확히 `0xffff80007bfa2020`에서 `a0 a1 a2 a3 1234`를 보여 줬다. gdb의 DWARF 문제(§4.4)와 무관하다. 보드에 심볼 파일도 호스트도 없을 때 kdb가 빛나는 이유다.
- `bt`의 아래쪽은 kgdb 진입 경로 그대로다: `el1h_64_sync` → `el1_brk64` → `call_el1_break_hook` → `kgdb_compiled_brk_handler`. BRK 예외가 디버그 훅 테이블을 거쳐 kgdb로 간다(M3의 BRK 즉치값 표에서 0x400이 KGDB).
- `ps`는 기본으로 잠든 데몬을 숨긴다. `m4_bug` kthread는 `msleep()` 중이라 `D`(TASK_UNINTERRUPTIBLE).
- **serial BREAK로 들어간 두 번째 진입**: `current=... pid 0`(swapper, idle). 셸 명령이 아니라 UART 드라이버의 수신 경로(BREAK 감지 → `uart_handle_break` → 다음 글자 'g'를 SysRq로 처리)에서 들어왔기 때문이다. 셸이 먹통이어도 IRQ가 살아 있으면 이 경로는 동작한다.
- 같은 포트를 콘솔과 kgdb gdb 모드로 같이 쓰려면 콘솔 글자와 RSP 패킷이 섞이므로 `agent-proxy`(호스트에서 한 포트를 콘솔용·gdb용 TCP 두 개로 나눠 주는 도구) 같은 분배기가 필요하다. 이 랩은 포트를 하나 더 만들어 그 문제를 피했다(agent-proxy는 실행하지 않았다).

### 5.5 kgdb 명령줄 정리

| 인자 / 설정 | 뜻 |
|---|---|
| `kgdboc=ttyS0,115200` | kgdb I/O로 쓸 tty와 속도. 런타임에는 `echo ttyS0 > /sys/module/kgdboc/parameters/kgdboc` |
| `kgdbwait` | kgdboc가 등록되자마자 정지하고 gdb를 기다린다 |
| `kgdboc_earlycon=` | earlycon 드라이버로 더 일찍 kgdb를 쓴다(드라이버가 폴링을 지원해야) |
| `kgdbcon` | gdb에 커널 printk를 같이 보낸다(gdb가 붙어 있을 때) |
| `nokaslr` | 심볼 주소를 vmlinux와 맞춘다 |
| `rodata=off` (필요할 때) | 텍스트 쓰기 보호 때문에 소프트웨어 브레이크포인트가 안 될 때의 옛 해법. arm64 6.x는 text patching으로 처리해서 이 랩에서는 필요 없었다 |
| `CONFIG_KGDB_KDB`, `CONFIG_KDB_KEYBOARD` | kdb 셸, 그리고 로컬 키보드로 kdb 쓰기 |

---

## 6. Magic SysRq — 멈추지 않고 상태를 뽑는 키

### 6.1 직관

Magic SysRq는 "커널에 직접 꽂힌 비상 버튼"이다. 유저스페이스가 다 죽어도 IRQ와 콘솔이 살아 있으면 키 하나로 커널이 상태를 덤프하거나 행동한다. 입력 경로는 셋이다.

- `/proc/sysrq-trigger`에 글자 쓰기 (root, 셸이 살아 있을 때)
- 시리얼 콘솔에서 BREAK 신호 후 5초 안에 글자 (`CONFIG_MAGIC_SYSRQ_SERIAL`)
- PC 키보드의 Alt+SysRq+글자

### 6.2 자주 쓰는 키 (`drivers/tty/sysrq.c`의 `sysrq_key_table`, 6.18)

| 키 | 동작 | 언제 쓰나 |
|---|---|---|
| `t` | 모든 task의 상태와 스택 (show state) | "무엇이 어디서 자고 있나" 전체 사진 |
| `w` | `D` 상태(uninterruptible) task만 (show blocked) | I/O나 락에서 멈춘 hang |
| `l` | 활성 CPU 전부의 백트레이스 | 어느 CPU가 무엇을 돌고 있나. IRQ가 막힌 CPU는 pseudo-NMI 없으면 못 받음(§7) |
| `p` | 현재 CPU의 레지스터 | 짧은 확인 |
| `m` | 메모리 상태 (show memory) | OOM 직전 |
| `q` | hrtimer·clockevent 목록 | 타이머가 안 터진다 |
| `d` | 잡고 있는 락 목록 (lockdep 켜져 있을 때) | 데드락 |
| `c` | 일부러 panic (crash) | kdump/pstore 테스트(L7) |
| `s` / `u` / `b` | sync / 읽기 전용 재마운트 / 즉시 재부팅 | 필드 장비를 그나마 안전하게 재부팅(s → u → b 순서) |
| `g` | kgdb/kdb 진입 | §5 |
| `0`~`9` | 콘솔 로그 레벨 | 로그 홍수 줄이기 |

### 6.3 예제 10 — `/proc/sysrq-trigger`로 투어하기

무엇을 확인하나: 키 몇 개를 차례로 눌러 출력 형식을 본다. 마지막에 `kernel.sysrq`를 0으로 꺼도 `/proc/sysrq-trigger`는 동작하는지 본다.

```sh
#!/bin/sh
# /m4_e7.sh (LAB_CMD)
insmod /m4_bug.ko
echo "sysrq mask: $(cat /proc/sys/kernel/sysrq)"
for k in h w l m q; do
  echo "=== sysrq $k"
  echo $k > /proc/sysrq-trigger
done
echo 0 > /proc/sys/kernel/sysrq
echo t > /proc/sysrq-trigger
dmesg | tail -3
```

```text
sysrq mask: 1
[    6.845431][   T76] sysrq: HELP : loglevel(0-9) reboot(b) crash(c) terminate-all-tasks(e) memory-full-oom-kill(f) kill-all-tasks(i) thaw-filesystems(j) sak(k
[    6.847574][   T76] sysrq: Show Blocked State
[    6.847911][   T76] task:m4_bug          state:D stack:0     pid:78    tgid:78    ppid:2      task_flags:0x208040 flags:0x00000010
[    6.848194][   T76] Call trace:
[    6.848328][   T76]  __switch_to+0x100/0x1c8 (T)
[    6.848657][   T76]  __schedule+0x3b8/0xc28
[    6.848675][   T76]  schedule+0x34/0xe0
[    6.848690][   T76]  schedule_timeout+0x88/0x128
[    6.848707][   T76]  msleep+0x3c/0x70
[    6.848726][   T76]  m4_thread+0x3c/0x70 [m4_bug]
[    6.849061][   T76]  kthread+0x134/0x220
[    6.849083][   T76]  ret_from_fork+0x10/0x20
[    6.852215][   T76] sysrq: Show backtrace of all active CPUs
[    6.852405][   T76] NMI backtrace for cpu 0
[    6.852871][   T76] CPU: 0 UID: 0 PID: 76 Comm: m4_e7.sh Tainted: G           OE       6.18.55-trace #1 PREEMPT 
...
[    6.859819][    C1] NMI backtrace for cpu 1
[    6.859991][    C1] CPU: 1 UID: 0 PID: 32 Comm: pr/ttyAMA-1 Tainted: G           OE       6.18.55-trace #1 PREEMPT 
...
[    6.862764][   T76] sysrq: Show Memory
[    6.862857][   T76] Mem-Info:
[    6.863077][   T76] active_anon:550 inactive_anon:155 isolated_anon:0
...
[    6.867564][   T76] now at 6545973008 nsecs
...
[    6.876879][   T76] sysrq: Show State
[    6.876918][   T76] task:init            state:S stack:0     pid:1     tgid:1     ppid:0      task_flags:0x400100 flags:0x00000008
[    6.876958][   T76] Call trace:
[    6.876968][   T76]  __switch_to+0x100/0x1c8 (T)
[    6.876998][   T76]  __schedule+0x3b8/0xc28
[    6.877013][   T76]  schedule+0x34/0xe0
[    6.877028][   T76]  do_wait+0x68/0x148
```

(전체 1055줄 중 일부. `t`는 모든 task를 덤프해서 수백 줄이 나온다.)

출력에서 볼 것:

- `w`가 `D` 상태의 `m4_bug` kthread를 콜스택과 함께 보여 줬다: `m4_thread` → `msleep` → `schedule_timeout`. 장비가 "멈췄다"고 할 때 첫 명령으로 쓰기 좋다. 내 드라이버 함수가 `wait_event` 근처에서 보이면 깨우는 쪽(IRQ 핸들러)이 안 돌고 있다는 뜻이다(F3).
- `l`의 "NMI backtrace"라는 이름과 달리 arm64는 pseudo-NMI가 꺼져 있으면 **보통 IPI**로 다른 CPU에 요청한다. CPU1은 콘솔 출력 kthread(`pr/ttyAMA-1`)를 돌고 있었다.
- `kernel.sysrq` = 1(모두 허용, `CONFIG_MAGIC_SYSRQ_DEFAULT_ENABLE=0x1`). 0으로 껐는데도 마지막 `t`가 실행됐다(`Show State`). 이 마스크는 **키보드·시리얼 입력에만 적용**되고, root가 쓰는 `/proc/sysrq-trigger`는 항상 허용된다(`__handle_sysrq(key, check_mask=false)` — 예제 7 백트레이스의 `check_mask=false`가 바로 이것).
- 양산 장비에서는 시리얼 BREAK 한 번으로 재부팅(`b`)되는 것이 위험하니 `kernel.sysrq` 값을 비트마스크로 줄이거나 `CONFIG_MAGIC_SYSRQ_SERIAL_SEQUENCE`로 BREAK 뒤 특정 문자열을 요구하게 만든다.

---

## 7. 실전 — 인터럽트 핸들러에서 멈춘 CPU를 "프로브를 꽂아서" 찾기

### 7.1 시나리오

센서 키트 보드가 가끔 "반쯤" 얼어붙는다. 콘솔에 lockup 경고 한 줄이 나오고 끝이다. 유력 용의자는 IMU 드라이버의 IRQ 핸들러가 상태 비트를 무한히 기다리는 것. 이것을 모듈 `m4_hang`으로 재현한다: CPU1에 고정된 hrtimer 콜백(hardirq 문맥)이 절대 안 오는 "HW ready"를 스핀으로 기다린다.

```c
// m4_hang.c - an hrtimer callback (hardirq context) that spins on CPU1
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/smp.h>

static int m4_release;          /* set to 1 from the debugger to unstick */
static struct hrtimer m4_timer;

static noinline void m4_wait_hw_ready(void)
{
	/* pretend: wait for a status bit that never comes */
	while (!READ_ONCE(m4_release))
		cpu_relax();
}

static enum hrtimer_restart m4_timer_fn(struct hrtimer *t)
{
	pr_info("m4_hang: timer fired on cpu %d, spinning\n", smp_processor_id());
	m4_wait_hw_ready();
	pr_info("m4_hang: released\n");
	return HRTIMER_NORESTART;
}

static void m4_arm(void *unused)
{
	hrtimer_start(&m4_timer, ms_to_ktime(100), HRTIMER_MODE_REL_PINNED);
}

static int __init m4_init(void)
{
	hrtimer_setup(&m4_timer, m4_timer_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED);
	return smp_call_function_single(1, m4_arm, NULL, 1);
}

static void __exit m4_exit(void)
{
	hrtimer_cancel(&m4_timer);
}
module_init(m4_init);
module_exit(m4_exit);
MODULE_DESCRIPTION("M4 debugger demo");
MODULE_LICENSE("GPL");
```

(`hrtimer_setup()`은 최근 커널에서 `hrtimer_init()` + `.function` 대입을 대체한 API다. 6.18 트리에서 경고 없이 빌드됐다.)

### 7.2 예제 11 — 나중에 붙기 (no `-S`): JTAG을 hung 보드에 꽂는 것과 같다

무엇을 확인하나: QEMU를 `-S` 없이 `-gdb tcp::12404`만 주고 띄운다(gdb 서버는 열려 있지만 아무도 안 붙음). 게스트가 lockup 경고를 낸 뒤에 gdb를 붙여 어느 CPU가 어디서 도는지, IRQ 마스크 상태, 그리고 디버거로 변수를 바꿔 풀어 주기까지 한다.

```text
# e5.gdb (attach after "hard LOCKUP" appears on the console)
set pagination off
set confirm off
target remote :12404
source /var/tmp/M4/kb/vmlinux-gdb.py
info threads
thread 2
bt 6
p/x $cpsr
printf "DAIF.I (IRQ masked) = %d\n", ($cpsr >> 7) & 1
lx-symbols /var/tmp/M4/mod
info symbol $pc
bt 8
python import re; o=gdb.execute("maint print msymbols -objfile /var/tmp/M4/mod/m4_hang.ko", to_string=True); a=re.search(r"([0-9a-f]{16}) m4_release", o).group(1); gdb.execute("set $rel = 0x" + a)
printf "m4_release @ %#lx = %d\n", $rel, *(int *)$rel
set var *(int *)$rel = 1
detach
quit
```

```text
cpu_do_idle () at /var/lab/src/linux-6.18.55/arch/arm64/kernel/idle.c:32
32		arm_cpuidle_restore_irq_context(&context);
  Id   Target Id                    Frame 
* 1    Thread 1.1 (CPU#0 [halted ]) cpu_do_idle () at /var/lab/src/linux-6.18.55/arch/arm64/kernel/idle.c:32
  2    Thread 1.2 (CPU#1 [running]) 0xffff80007bfa0080 in ?? ()
[Switching to thread 2 (Thread 1.2)]
#0  0xffff80007bfa0080 in ?? ()
#0  0xffff80007bfa0080 in ?? ()
#1  0xffff00003fdd7360 in ?? ()
#2  0x000000022832e9b0 in ?? ()
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
$1 = 0x600000c5
DAIF.I (IRQ masked) = 1
loading vmlinux
scanning for modules in /var/tmp/M4/mod
scanning for modules in /var/tmp/M4/kb
loading @0xffff80007bfa0000: /var/tmp/M4/mod/m4_hang.ko
m4_wait_hw_ready + 24 in section .text of /var/tmp/M4/mod/m4_hang.ko
#0  0xffff80007bfa0080 in m4_exit () at m4_hang.c:30
#1  0xffff80007bfa0114 in m4_timer_fn (t=<optimized out>) at m4_hang.c:19
#2  0xffff8000801d2b88 in __run_hrtimer (cpu_base=0xffff00003fdd7300, base=0xffff00003fdd7340, timer=0xffff80007bfa2020 <m4_timer>, now=0xffff800083fb3e80, flags=192) at /var/lab/src/linux-6.18.55/kernel/time/hrtimer.c:1782
#3  __hrtimer_run_queues (cpu_base=cpu_base@entry=0xffff00003fdd7300, now=now@entry=9264359856, flags=flags@entry=192, active_mask=active_mask@entry=15) at /var/lab/src/linux-6.18.55/kernel/time/hrtimer.c:1846
#4  0xffff8000801d34b8 in hrtimer_interrupt (dev=<optimized out>) at /var/lab/src/linux-6.18.55/kernel/time/hrtimer.c:1908
#5  0xffff8000813b68e4 in timer_handler (access=1, evt=<optimized out>) at /var/lab/src/linux-6.18.55/drivers/clocksource/arm_arch_timer.c:573
#6  arch_timer_handler_virt (irq=<optimized out>, dev_id=<optimized out>) at /var/lab/src/linux-6.18.55/drivers/clocksource/arm_arch_timer.c:584
#7  0xffff800080190aa8 in handle_percpu_devid_irq (desc=0xffff00000463ac00) at /var/lab/src/linux-6.18.55/kernel/irq/chip.c:915
m4_release @ 0xffff80007bfa4048 = 0
[Inferior 1 (process 1) detached]
```

게스트 콘솔:

```text
[   10.199451][    C1] m4_hang: timer fired on cpu 1, spinning
[   37.117225][    C0] watchdog: CPU0: Watchdog detected hard LOCKUP on cpu 1
[   37.117675][    C0] Modules linked in: m4_hang(OE)
[   37.118795][    C0] Sending NMI from CPU 0 to CPUs 1:
[  201.622694][    C1] m4_hang: released
[  201.633805][    C1] NMI backtrace for cpu 1
[  201.637914][    C1] CPU: 1 UID: 0 PID: 78 Comm: grep Tainted: G           OE       6.18.55-trace #1 PREEMPT 
[  201.638332][    C1] pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[  201.638393][    C1] pc : kallsyms_expand_symbol.constprop.0+0x48/0xc8
[  201.638533][    C1] lr : update_iter+0x9c/0x238
...
ffff80007bfa4048 r m4_release	[m4_hang]
guest: CPU0 still alive
```

(레지스터 줄과 콜스택 대부분은 생략했다. 같은 실험을 두 번 돌린 것 중 두 번째 출력이다. 첫 번째 실행도 메시지 순서는 같았고, 그때 늦게 찍힌 CPU1 백트레이스는 `pc : _raw_spin_unlock_irq+0x20/0x60`, 콜스택 `run_timer_base` ← `run_timer_softirq` ← ... ← `seq_read`였다.)

출력에서 볼 것 — 이것이 면접 답의 뼈대다:

1. **`info threads` 한 줄로 범인 CPU가 나온다**: CPU0은 idle(`halted`), CPU1은 `running` 상태로 모르는 주소 `0xffff80007bfa0080`(모듈 영역)에서 돈다.
2. **PSTATE `0x600000c5`**: DAIF의 I 비트(bit 7) = 1 → IRQ 마스크 상태. 하드 IRQ 문맥이라는 강력한 힌트. 모드 비트 `0x5` = EL1h(커널).
3. **심볼 로드 후**: `m4_wait_hw_ready + 24`, 콜스택은 `handle_percpu_devid_irq` → `arch_timer_handler_virt` → `hrtimer_interrupt` → `__run_hrtimer(timer=<m4_timer>)` → `m4_timer_fn`. "타이머 IRQ 안에서 우리 콜백이 상태 비트를 기다린다"가 확정된다. (frame #0의 `m4_exit` 이름은 §4.4의 DWARF 함정이고 `info symbol`이 맞다.)
4. **소프트웨어 감시자는 어디까지 했나**: buddy hard lockup 감지기(CPU0이 CPU1의 hrtimer 카운트가 안 늘어남을 봄)가 27초 만에 "hard LOCKUP on cpu 1"을 냈다. 그런데 백트레이스를 받으려고 보낸 "NMI"는 arm64에서 `CONFIG_ARM64_PSEUDO_NMI`가 꺼져 있어 **일반 IPI**다 — IRQ를 막은 CPU1은 못 받았다. CPU1의 백트레이스는 디버거가 풀어 준 뒤(201초)에야 찍혔고, 그때는 이미 다른 일을 하던 중이라 **엉뚱한 콜스택**이다. 두 실행에서 각각 `kallsyms_expand_symbol`(게스트의 `grep`이 `/proc/kallsyms`를 읽던 중)과 `run_timer_base`(타이머 softirq)가 찍혔다. 로그만 믿으면 엉뚱한 곳을 의심하게 된다.
5. **RCU stall은 안 떴다**: CPU0이 idle이라 진행 중인 grace period가 없었던 것으로 보인다(stall은 GP가 진행 중일 때만 보고된다). "경고가 없다 ≠ 문제가 없다".
6. **디버거로 풀어 주기**: `m4_release`의 주소를 ELF 심볼에서 찾아 1을 썼고, CPU1이 바로 `released`를 찍고 정상으로 돌아왔다. kallsyms에서 `m4_release`가 `r`(읽기 전용 데이터)로 나온 것에 주목하자. C 코드가 한 번도 안 쓰는 static 변수라 컴파일러가 `.rodata`에 넣었다. 커널 매핑상 쓰기 금지 페이지인데도 gdbstub은 물리 메모리에 바로 써서 성공했다. 실보드 JTAG에서 코어를 통한 쓰기(MMU 권한 검사 적용)라면 실패할 수 있고, MEM-AP(AXI-AP) 물리 쓰기는 성공한다.

kgdb로는 이 장면을 못 잡는다. SysRq-g는 CPU0에서 들어갈 수는 있지만 kgdb의 roundup IPI도 CPU1에 안 들어가서 CPU1의 레지스터를 못 얻는다(pseudo-NMI가 켜진 커널이면 상황이 낫다: `CONFIG_ARM64_PSEUDO_NMI=y` + `irqchip.gicv3_pseudo_nmi=1`, GICv3 필요). **JTAG(여기서는 gdbstub)는 코어 자체를 세우니 문맥과 무관하게 PC를 본다.** 이것이 "JTAG만 있는 보드에서 IRQ 핸들러 hang"의 정답 방향이다.

---

## 8. 멈추면 생기는 일 — 시계, watchdog, 스텝

### 8.1 그림으로 보기

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
  <rect x="300" y="38" width="160" height="232" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
  <text x="330" y="30" font-size="13">디버거 정지 60 s</text>
  <text x="10" y="64" font-size="13">벽시계 (host)</text>
  <line x1="120" y1="60" x2="640" y2="60" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="124" font-size="13">QEMU 가상 시계</text>
  <line x1="120" y1="120" x2="300" y2="120" stroke="#e08a3c" stroke-width="2"/>
  <line x1="300" y1="120" x2="460" y2="120" stroke="#e08a3c" stroke-width="1" stroke-dasharray="2 4"/>
  <line x1="460" y1="120" x2="640" y2="120" stroke="#e08a3c" stroke-width="2"/>
  <text x="330" y="140" font-size="12">시간이 안 흐름</text>
  <text x="465" y="140" font-size="12">uptime 그대로, rtc만 점프</text>
  <text x="10" y="184" font-size="13">kgdb: 카운터</text>
  <line x1="120" y1="180" x2="640" y2="180" stroke="#4a7bd0" stroke-width="2"/>
  <circle cx="460" cy="180" r="5" fill="#4a7bd0"/>
  <text x="465" y="200" font-size="12">깨어나면 wq 경고 가능</text>
  <text x="10" y="244" font-size="13">실보드 WDT</text>
  <line x1="120" y1="240" x2="640" y2="240" stroke="#3f9a6b" stroke-width="2"/>
  <line x1="150" y1="232" x2="150" y2="248" stroke="#3f9a6b" stroke-width="2"/>
  <line x1="190" y1="232" x2="190" y2="248" stroke="#3f9a6b" stroke-width="2"/>
  <line x1="230" y1="232" x2="230" y2="248" stroke="#3f9a6b" stroke-width="2"/>
  <line x1="270" y1="232" x2="270" y2="248" stroke="#3f9a6b" stroke-width="2"/>
  <line x1="390" y1="228" x2="410" y2="252" stroke="#d0564a" stroke-width="3"/>
  <line x1="410" y1="228" x2="390" y2="252" stroke="#d0564a" stroke-width="3"/>
  <text x="465" y="262" font-size="12">kick 없음 → 보드 리셋</text>
  <text x="125" y="226" font-size="12">kick</text>
</svg>
```

그림 5 — 같은 60초 정지가 도구마다 다르게 보인다. QEMU gdbstub은 가상 시계까지 세운다. kgdb는 CPU만 세우고 시스템 카운터는 계속 간다. 실보드의 외부 watchdog은 아무도 kick하지 않으니 리셋을 건다.

### 8.2 예제 12 — gdbstub으로 60초 세우면 게스트 시간은?

무엇을 확인하나: 게스트가 5초마다 uptime, 시스템 시간(`date`), PL031 RTC 시간을 찍는다. 35초 뒤 gdb로 세우고 호스트에서 60초 기다린 뒤 재개한다.

```text
# e4.gdb
set pagination off
set confirm off
target remote :12404
shell (sleep 35; kill -INT $PPID) &
continue
shell date +%T; echo "host: halted for 60 s"; sleep 60; date +%T
shell (sleep 60; kill -INT $PPID) &
continue
```

```text
tick 0 uptime=4.22 date=07:40:31 rtc=07:40:31
tick 1 uptime=9.28 date=07:40:36 rtc=07:40:36
tick 2 uptime=14.33 date=07:40:41 rtc=07:40:41
tick 3 uptime=19.41 date=07:40:46 rtc=07:40:46
tick 4 uptime=24.50 date=07:40:51 rtc=07:40:51
tick 5 uptime=29.57 date=07:40:56 rtc=07:40:56
tick 6 uptime=34.66 date=07:41:02 rtc=07:42:01
tick 7 uptime=39.75 date=07:41:07 rtc=07:42:07
tick 8 uptime=44.80 date=07:41:12 rtc=07:42:12
tick 9 uptime=49.91 date=07:41:17 rtc=07:42:17
tick 10 uptime=55.01 date=07:41:22 rtc=07:42:22
tick 11 uptime=60.08 date=07:41:27 rtc=07:42:27
tick 12 uptime=65.21 date=07:41:32 rtc=07:42:32
tick 13 uptime=70.29 date=07:41:37 rtc=07:42:37
```

출력에서 볼 것: tick 5와 tick 6 사이에 60초를 세웠는데 **uptime과 `date`는 5초만 늘었다**. QEMU는 VM을 정지하면 가상 시계(`QEMU_CLOCK_VIRTUAL`, ARM generic timer의 기준)도 세우기 때문이다. 게스트 입장에서는 정지가 "없었던 일"이라 lockup 경고도 없다(이 실행의 dmesg에 lockup·stall 줄이 없었다). 그런데 RTC(PL031)는 호스트 시간을 따라가서 **07:40:56 → 07:42:01로 점프**했다. 그 뒤로 시스템 시간과 RTC가 1분 어긋난 채로 간다. 디버깅 뒤 NTP·로그 타임스탬프가 이상하면 이것이다.

### 8.3 실보드와 kgdb에서는

- **kgdb**: CPU는 멈춰도 ARM 시스템 카운터(`CNTVCT`)는 계속 간다. 깨어나면 커널이 "60초 동안 아무 일도 안 했다"를 본다. 그래서 kgdb는 나갈 때 softlockup·clocksource·RCU 감시를 리셋하지만, 예제 8처럼 workqueue 감시는 못 막았다. 네트워크 상대(TCP keepalive, CAN 버스의 heartbeat 상대 MCU)는 당연히 타임아웃된다.
- **JTAG 실보드**: 코어를 세워도 SoC의 system counter는 기본적으로 계속 간다. ARM 일반 타이머 사양에는 디버그 정지 신호가 오면 카운터를 멈추는 설정(CNTCR의 HDBG 비트, CoreSight CTI로 halt 신호를 연결해야 함)이 있지만 SoC 통합에 따라 쓸 수 있는지가 다르다. 확인하고 쓴다.
- **외부 watchdog**: SoC WDT나 PMIC watchdog은 코어가 멈추면 kick을 못 받아 보드를 리셋한다. 디버깅 전에 끄거나(`nowayout` 아닌 경우 `/dev/watchdog` 닫기, DT에서 disable, PMIC 설정) 디버그 정지 시 WDT도 정지시키는 SoC 레지스터(많은 SoC에 "debug halt 시 WDT freeze" 비트가 있다)를 쓴다. SSD 컨트롤러 시절의 "JTAG 붙이면 WDT 끄고 시작"과 똑같다.

### 8.4 단일 스텝(stepi)이 이상할 때

커널을 `stepi`로 한 명령씩 가면 그 사이에 타이머 IRQ가 떠서 "다음 명령"이 IRQ 벡터가 되는 일이 생긴다. QEMU gdbstub은 기본값으로 이것을 막는다.

```text
maint packet qqemu.sstepbits
maint packet qqemu.sstep
info registers ID_AA64DFR0_EL1
```

```text
sending: qqemu.sstepbits
received: "ENABLE=1,NOIRQ=2,NOTIMER=4"
sending: qqemu.sstep
received: "0x7"
ID_AA64DFR0_EL1 0x10305106         271601926
```

출력에서 볼 것: 현재 단일 스텝 모드 = 0x7 = ENABLE + NOIRQ(스텝 중 IRQ 안 받음) + NOTIMER(스텝 중 타이머 안 감). 실보드 JTAG에는 이런 마법이 없다 — OpenOCD는 Cortex-A에서 스텝 중 인터럽트 마스킹 옵션(`<target> aarch64 maskisr on`)을 제공하고, 그것도 안 되면 IRQ 진입 뒤 `finish`로 빠져나온다. 같은 출력의 `ID_AA64DFR0_EL1`이 §4.6의 브레이크포인트 6개/워치포인트 4개 근거다.

preemption도 같은 종류의 함정이다. 한 함수 안에서 `next`를 하다가 그 task가 다른 CPU로 옮겨 가면 gdbstub(thread = CPU)에서는 "다른 thread가 브레이크포인트를 쳤다"처럼 보인다. 브레이크포인트에 조건을 거는 습관(`break m4_fill if $lx_current().pid == 78`)이 도움이 된다.

---

## 9. 실제 보드의 JTAG — CoreSight, OpenOCD, TRACE32 (가이드)

### 9.1 Cortex-A의 디버그 구조

Cortex-M의 SWD는 "DAP → AHB-AP → 코어 디버그 레지스터와 메모리"가 거의 전부였다. Cortex-A SoC는 CoreSight라는 디버그·트레이스 부품 묶음이다.

```svg
<svg viewBox="0 0 660 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="15" width="220" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="32" y="40" font-size="13">PC: gdb / TRACE32 GUI</text>
  <rect x="20" y="80" width="340" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="32" y="105" font-size="13">프로브: J-Link · TRACE32 · FTDI+OpenOCD</text>
  <line x1="130" y1="57" x2="130" y2="77" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <line x1="130" y1="122" x2="130" y2="175" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <text x="140" y="150" font-size="12">JTAG (TCK/TMS/TDI/TDO) 또는 SWD</text>
  <rect x="10" y="160" width="640" height="232" rx="8" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="560" y="178" font-size="12">SoC</text>
  <rect x="30" y="180" width="200" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="42" y="205" font-size="13">DAP (Debug Port)</text>
  <rect x="30" y="250" width="130" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="42" y="275" font-size="13">APB-AP</text>
  <rect x="30" y="320" width="150" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="42" y="345" font-size="13">AXI-AP (MEM-AP)</text>
  <line x1="95" y1="222" x2="95" y2="247" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <line x1="60" y1="222" x2="60" y2="317" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <rect x="200" y="240" width="140" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="212" y="262" font-size="13">A53 core 0</text>
  <text x="212" y="284" font-size="12">debug · CTI · ETM</text>
  <rect x="350" y="240" width="140" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="362" y="262" font-size="13">A53 core 1</text>
  <text x="362" y="284" font-size="12">debug · CTI · ETM</text>
  <line x1="162" y1="270" x2="197" y2="270" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <text x="165" y="236" font-size="12">Debug APB</text>
  <rect x="505" y="240" width="140" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="262" font-size="13">Funnel → ETF</text>
  <text x="515" y="284" font-size="12">ETR · TPIU 핀</text>
  <line x1="492" y1="270" x2="502" y2="270" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <rect x="200" y="320" width="290" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="212" y="345" font-size="12">시스템 버스 → DRAM·주변장치 (물리 주소)</text>
  <line x1="182" y1="340" x2="197" y2="340" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <rect x="505" y="320" width="140" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="515" y="342" font-size="12">debug 전원 도메인</text>
  <text x="515" y="364" font-size="12">DBGEN/SPIDEN 퓨즈</text>
</svg>
```

그림 6 — Cortex-A SoC의 CoreSight 디버그 경로. APB-AP로 코어별 debug 레지스터(정지·재개·레지스터 읽기)와 CTI(코어 간 halt 신호 전달)·ETM(명령 트레이스)에 가고, AXI-AP로 시스템 버스에 직접 붙어 물리 메모리를 읽고 쓴다.

부품 하나씩:

- **DAP / DP**: JTAG 또는 SWD를 받는 입구. Cortex-M과 같다.
- **APB-AP → Debug APB**: 코어마다 있는 외부 디버그 레지스터(EDSCR 등)에 접근한다. 디버거는 여기서 코어를 halt하고, **코어에 명령을 주입해서**(ITR) 레지스터와 메모리를 읽는다. 그래서 코어를 통한 메모리 읽기는 MMU 번역(가상 주소)과 캐시를 거친다.
- **AXI-AP(MEM-AP)**: 시스템 버스에 직접 붙는다. 물리 주소, 캐시를 안 거침(캐시에만 있는 dirty 데이터는 못 본다 — 정지한 코어의 D-cache를 먼저 clean해야 한다). 코어가 꺼져 있거나 hang이어도 DRAM을 읽을 수 있다.
- **CTI/CTM**: 한 코어가 halt되면 다른 코어도 같이 halt하게 신호를 연결한다(SMP halt). OpenOCD의 `target smp`가 이것을 쓴다.
- **ETM + Funnel + ETF/ETR + TPIU**: 명령 실행 흐름을 압축해서 기록한다. "hang 직전 1만 개 명령"을 볼 수 있다. ETR은 DRAM 버퍼, TPIU는 트레이스 핀으로 보낸다. Linux에도 CoreSight 드라이버(`perf record -e cs_etm//`)가 있다(M6와 연결).
- **debug 전원 도메인**: cpuidle이 코어를 끄면(PSCI CPU_SUSPEND의 power-down 상태) 그 코어의 debug 레지스터도 꺼져서 디버거가 "코어를 잃는다". 디버깅 중에는 `cpuidle.off=1`(또는 `nohlt`) 커널 인자로 깊은 idle을 막는다. Cortex-A의 OS Lock·OS Double Lock도 전원 관리 중 디버그 접근을 막는 장치다.
- **보안**: DBGEN(비보안 invasive debug), SPIDEN(secure invasive debug) 같은 인증 신호가 fuse나 TF-A 설정으로 꺼지면 JTAG로 코어를 못 세우거나 secure world(EL3, BL31)를 못 본다. 양산품에서 JTAG를 잠그는 이야기는 L5 §9.4.

### 9.2 OpenOCD 설정 — Cortex-A53 4코어 SoC 예시 (가이드, 실행 안 함)

OpenOCD는 VM에 설치돼 있지 않고 실보드도 없어 실행하지 않았다. 구조는 OpenOCD의 `tcl/target/*.cfg`(예: i.MX8M, RK3399 계열 설정)를 따른 것이다. **디버그 베이스 주소는 SoC 매뉴얼의 CoreSight ROM table에서 확인해야 한다**(아래 값은 형식 예시).

```text
# board.cfg - Cortex-A53 x4 over JTAG (guide only, values are examples)
adapter driver jlink
transport select jtag
adapter speed 4000
reset_config trst_and_srst
set _CHIPNAME soc
jtag newtap $_CHIPNAME cpu -irlen 4 -expected-id 0x5ba00477
dap create $_CHIPNAME.dap -chain-position $_CHIPNAME.cpu
set _DBGBASE {0x80410000 0x80510000 0x80610000 0x80710000}
set _CTIBASE {0x80420000 0x80520000 0x80620000 0x80720000}
set _smp ""
for { set i 0 } { $i < 4 } { incr i } {
    cti create $_CHIPNAME.cti$i -dap $_CHIPNAME.dap -ap-num 1 \
        -baseaddr [lindex $_CTIBASE $i]
    target create $_CHIPNAME.a53.$i aarch64 -dap $_CHIPNAME.dap -ap-num 1 \
        -dbgbase [lindex $_DBGBASE $i] -cti $_CHIPNAME.cti$i
    lappend _smp $_CHIPNAME.a53.$i
}
target smp {*}$_smp
# physical memory through the AXI-AP, independent of the cores
target create $_CHIPNAME.axi mem_ap -dap $_CHIPNAME.dap -ap-num 0
```

쓰는 법(가이드):

```sh
openocd -f board.cfg                       # gdb 서버 :3333 (코어마다 포트 하나씩 또는 smp면 하나)
gdb-multiarch vmlinux -ex "target extended-remote :3333" \
  -ex "source vmlinux-gdb.py" -ex "monitor halt" -ex "info threads" -ex "lx-dmesg"
```

- `target smp`로 묶으면 gdb에서 코어가 thread로 보이고 하나가 멈추면 CTI로 전부 멈춘다 — 이 노트의 gdbstub과 같은 모델이다.
- 호스트가 x86이면 `gdb-multiarch`(또는 `aarch64-linux-gnu-gdb`)가 필요하다. 이 VM은 arm64라 그냥 `gdb`를 썼다.
- `monitor halt`/`resume`, `monitor aarch64 maskisr on`(스텝 중 IRQ 마스크), `monitor <target> mdw phys 0x...`(물리 메모리)처럼 OpenOCD 명령은 `monitor`로 보낸다.
- KASLR, 모듈 심볼, `lx-*`는 §3~§4와 완전히 같다. 달라지는 것은 시간(§8.3)과 전원(§9.1)이다.

### 9.3 Lauterbach TRACE32 — Linux awareness (가이드, 실행 안 함)

TRACE32는 상용 디버거로, 실리콘 벤더 bring-up 팀이 많이 쓴다. 강점은 **OS awareness**(Linux task 목록, 프로세스별 MMU 공간, 모듈 자동 심볼)와 ETM 트레이스 분석이다. 스크립트(PRACTICE, `.cmm`)의 대략적인 모양은 다음과 같다. 명령 이름은 TRACE32 문서 기준이지만 awareness 파일 경로와 세부 옵션은 TRACE32 버전과 설치 폴더(`~~/demo/arm64/kernel/linux/`)마다 달라 확인이 필요하다.

```text
; linux_attach.cmm (guide only)
SYStem.RESet
SYStem.CPU CortexA53
SYStem.CONFIG.CoreNumber 4.
CORE.ASSIGN 1. 2. 3. 4.
SYStem.Option.MMUSPACES ON          ; separate address spaces per process
SYStem.Mode Attach                  ; attach to a running board, no reset
Break
Data.LOAD.Elf vmlinux /NoCODE       ; symbols only, the code is already in RAM
TASK.CONFIG ~~/demo/arm64/kernel/linux/awareness/linux.t32
MENU.ReProgram ~~/demo/arm64/kernel/linux/awareness/linux.men
TASK.List.tasks                     ; like lx-ps
sYmbol.AutoLOAD.CHECKLINUX "do ~~/demo/arm64/kernel/linux/awareness/autoload.cmm "
```

- `SYStem.Mode Attach`가 "hung 보드에 리셋 없이 붙기"(§7)다. `SYStem.Up`은 리셋하고 붙는다.
- `/NoCODE`: 코드는 이미 RAM에 있으니 심볼만 읽는다.
- `sYmbol.AutoLOAD`가 `lx-symbols`처럼 모듈 심볼을 주소 맞춰 자동으로 읽는다.
- Segger J-Link는 프로브로서 OpenOCD나 J-Link GDB Server와 같이 쓴다. Cortex-A 지원은 있지만 Linux awareness는 TRACE32만큼 깊지 않다.

---

## 10. 커널 안에서는 무슨 일이 일어나나

디버거들이 커널의 어디와 만나는지 경로로 정리한다.

| 사건 | 커널 경로 | 관련 노트 |
|---|---|---|
| gdbstub/JTAG halt | 커널은 모른다. CPU가 debug state로 들어가고 나온다 | C6 §2 |
| `hbreak`/`watch` (JTAG, gdbstub) | 외부에서 debug 레지스터(DBGBVR/DBGWVR)를 직접 프로그램 | — |
| 커널 자체의 hw breakpoint | `arch/arm64/kernel/hw_breakpoint.c`, perf의 `PERF_TYPE_BREAKPOINT` | M6 |
| kgdb 브레이크포인트 | `kgdb_arch_set_breakpoint()` → text patching으로 BRK 쓰기 → 실행 시 `el1h_64_sync` → `do_el1_brk64` → `call_el1_break_hook` → `kgdb_brk_handler` | E6(예외 진입), M3(BRK 즉치값) |
| kgdb 진입 | `kgdb_handle_exception()` → `kgdb_cpu_enter()` → `kgdb_roundup_cpus()`(IPI) → `gdb_serial_stub()` 또는 `kdb_stub()` | F7(IRQ·IPI) |
| kgdb I/O | `kgdboc` → `tty_driver->ops->poll_get_char/poll_put_char` | I-모듈(tty/serial) |
| SysRq | `/proc/sysrq-trigger` → `write_sysrq_trigger()` → `__handle_sysrq(key, false)` / UART BREAK → `uart_handle_break()` → `uart_handle_sysrq_char()` → `handle_sysrq()` | M1(콘솔), L7(`c`) |
| `t`, `w` | `show_state_filter()` → task마다 `sched_show_task()` | F5(스케줄러) |
| `l` | `trigger_all_cpu_backtrace()` → `nmi_trigger_cpumask_backtrace()` → arm64는 IPI(또는 pseudo-NMI) | F7 |
| lockup 감시 | `kernel/watchdog.c`(softlockup + buddy hardlockup), `kernel/workqueue.c`(`wq_watchdog_timer_fn`), RCU stall(`kernel/rcu/tree_stall.h`) | N1, F6 |
| 모듈 섹션 | `load_module()` → `mod_sysfs_setup()` → `/sys/module/<m>/sections/` (`module_sect_attrs`) → `do_init_module()` | E3, M3 |

드라이버를 쓸 때 다시 만나는 지점:

- **UART 드라이버를 쓴다면**(H/I 모듈) `poll_get_char`/`poll_put_char`를 구현해야 그 보드에서 kgdb를 쓸 수 있다. BREAK 처리(`uart_handle_break`)를 빼먹으면 serial SysRq가 안 된다.
- **IRQ 핸들러**(G·H 모듈)에서 폴링 루프를 돌 때는 반드시 타임아웃을 둔다(`readl_poll_timeout_atomic()`). §7의 hang이 정확히 타임아웃 없는 폴링이다.
- **watchdog 드라이버**(I 모듈 watchdog)는 디버그 정지 중 freeze 비트를 지원하면 DT 속성이나 모듈 파라미터로 노출해 두면 bring-up 팀이 고마워한다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트 bring-up: 커널이 earlycon 이후 침묵

U-Boot `Starting kernel ...` 뒤 아무 출력이 없다(C6 §5, M1). JTAG로 붙어 `monitor halt` → PC를 본다. PC가 물리 주소면 MMU 전에 죽은 것(§3.3 방식으로 심볼을 옮겨 읽는다). 가상 주소면 `lx-dmesg`로 로그 버퍼를 읽는다 — 콘솔 드라이버만 안 떴을 뿐 커널은 많이 진행했을 수 있다. KASLR이면 §3.4로 오프셋부터.

### 11.2 IMU 드라이버의 간헐 hang (§7의 실물판)

현장 로그에 "hard LOCKUP on cpu 2"만 남는다. pseudo-NMI 없는 arm64라면 그 CPU의 백트레이스는 신뢰할 수 없다(§7의 4번). 재현 장비에 JTAG를 꽂고 `SYStem.Mode Attach`/OpenOCD halt → CPU2의 PC → IRQ 핸들러의 폴링 루프. 수정은 `readl_poll_timeout_atomic()` + 에러 경로. 재발 감시로는 `CONFIG_ARM64_PSEUDO_NMI` + `irqchip.gicv3_pseudo_nmi=1`을 켜서 다음에는 로그에 진짜 백트레이스가 남게 한다(성능 영향 측정 후).

### 11.3 필드 장비: JTAG 없음, UART 하나

양산 장비는 JTAG fuse가 잠겨 있다(L5). 남는 것은 콘솔 UART와 Magic SysRq다. BREAK + `w`(D 상태 task), `l`, `t`를 받아 두고, 필요하면 `c`로 kdump/pstore를 남긴다(L7). 보안 정책상 kgdb·kdb는 양산 커널에서 끄는 것이 보통이다(`CONFIG_KGDB=n`) — 디버그 커널을 따로 빌드해 RMA 장비에서 쓴다.

### 11.4 메모리 오염 — "누가 DMA 디스크립터를 덮었나"

SSD 펌웨어의 단골 버그가 Linux에서도 그대로 온다. 하드웨어 워치포인트는 CPU가 쓴 경우만 잡는다(예제 6). **DMA 엔진이 덮은 경우는 워치포인트에 안 걸린다** — 그때는 IOMMU fault 로그, KASAN(M8), 또는 디스크립터 앞뒤에 guard 패턴을 두고 주기적으로 검사하는 방법을 쓴다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| KASLR 커널에 `vmlinux` 그대로 | 브레이크포인트 안 걸림, `?? ()` 투성이 | 부팅마다 커널이 옮겨짐 | `nokaslr`, 또는 `VBAR_EL1 − &vectors`로 오프셋 구해 `symbol-file -o` (§3.4) |
| MMU 전 코드에 가상 주소 브레이크포인트 | 영원히 안 멈춤 | PC가 물리 주소 | `symbol-file vmlinux -o <phys−virt>` + `hbreak` (§3.3) |
| `Image`를 gdb에 줌 | 심볼 없음 | `Image`는 raw binary | 같은 빌드의 `vmlinux` |
| 모듈 `.text`만 주고 `add-symbol-file` | 변수·줄 번호 엉뚱 | 섹션 주소 누락 | sysfs `sections/` 전부 또는 `lx-symbols`. 그래도 이상하면 `info symbol`로 확인(§4.4) |
| `lx-*`가 없음 | `Undefined command: "lx-dmesg"` | `scripts_gdb` 미빌드, `source` 안 함, auto-load 거부 | `make scripts_gdb`, `source vmlinux-gdb.py`(§2.2) |
| kgdb 접속을 너무 일찍 시도 | `Bogus trace status reply`, 쓰레기 패킷 | kgdboc 등록 전 부팅 로그를 gdb가 읽음 | "Waiting for connection" 확인 후 접속 (이 랩에서 실제로 겪음) |
| 콘솔 UART로 kgdb gdb 모드 | gdb 패킷과 printk가 섞여 끊김 | 한 포트 공유 | 별도 UART, kdb 사용, 또는 agent-proxy |
| UART 드라이버에 poll 콜백 없음 | `kgdboc: ... not supported` 류로 등록 실패 | `CONFIG_CONSOLE_POLL` 콜백 미구현 | 드라이버에 `poll_get_char/poll_put_char` 구현 |
| 디버거로 오래 멈춘 뒤 경고를 버그로 오인 | workqueue lockup, RCU stall, NTP 점프 | 멈춘 동안 시간이 흐름(또는 RTC만 흐름) | 정지 시간과 경고 시각 대조(§5.3, §8.2) |
| JTAG 중 보드 리셋 | 멈춘 지 몇 초 뒤 리셋 | 외부 WDT/PMIC watchdog | WDT 끄기, debug-halt freeze 비트 |
| cpuidle 켠 채 JTAG | 코어가 "사라짐", 접근 에러 | 코어 전원 도메인 off | `cpuidle.off=1` |

---

## 13. 면접에서 이렇게 말한다

**Q.** How would you debug a hang in an interrupt handler on a board where you only have JTAG?

**A.** 프로브를 리셋 없이 attach하고 모든 코어를 halt한다. 코어별 PC와 PSTATE를 본다. IRQ 마스크(DAIF.I=1)인 채로 한 코어가 같은 루프를 돌고 있으면 그게 범인이다. `vmlinux`를 KASLR 오프셋에 맞춰 로드하고, 모듈이면 섹션 주소로 심볼을 올려 PC를 함수와 줄로 바꾼 뒤, 콜스택으로 어떤 IRQ에서 들어왔는지 확인한다. 이때 커널 로그는 믿기 어렵다. pseudo-NMI가 없는 arm64에서는 IRQ가 막힌 코어에 백트레이스 IPI가 안 들어가서 엉뚱한 스택이 찍힐 수 있기 때문이다. 고칠 때는 폴링에 타임아웃을 넣는다.

> I'd attach the probe without resetting the board and halt all cores. Then I look at each core's PC and PSTATE. If one core has IRQs masked and keeps looping in the same few instructions, that's the culprit. I load vmlinux at the KASLR offset, plus the module symbols at their section addresses, and walk the stack to see which interrupt brought us there. I wouldn't trust the kernel's own lockup backtrace much. On arm64 without pseudo-NMI, the backtrace IPI can't reach a core that has interrupts masked, so you can get a stack from much later. The fix is usually a bounded poll, like readl_poll_timeout_atomic, with a real error path.

**꼬리 질문**: "JTAG도 없으면?" → buddy hardlockup 감지 + pseudo-NMI를 켠 커널로 재현하거나, SysRq `l`로 다른 CPU 상태, 그리고 의심 루프에 GPIO 토글을 넣어 scope로 본다(M11).

**Q.** kgdb vs JTAG — when would you use which?

**A.** kgdb는 커널 안에 있는 gdb stub이라 UART 하나면 되고 프로브도 필요 없다. 그 대신 커널이 예외를 처리할 수 있을 만큼 살아 있어야 하고, 폴링 UART 드라이버가 필요하며, kgdboc가 등록되기 전의 초기 부팅이나 IRQ가 막힌 코어는 못 본다. JTAG는 실리콘이 코어를 세우니 리셋 첫 명령부터, 그리고 어떤 문맥이든 볼 수 있다. ETM 트레이스와 물리 메모리 직접 접근도 된다. 하지만 핀·프로브·SoC 설정(디버그 전원, 보안 fuse)이 필요하다. 필드 장비나 접근이 어려운 보드에는 kgdb/kdb, bring-up과 hang 분석에는 JTAG를 쓴다.

> kgdb is a gdb stub inside the kernel, so all you need is a serial port. The catch is that the kernel has to be healthy enough to take the debug exception. You also need a UART driver with polling hooks, and you can't see anything before kgdboc registers, or any core with interrupts masked. JTAG stops the core in silicon. It works from the very first instruction after reset and in any context, and you get physical memory access and ETM trace on top. The price is the probe, the debug header, and the SoC setup: debug power domains, and security fuses that are often blown on production parts. So I'd use JTAG for bring-up and hard hangs, and kgdb or kdb on boards where I only have a console.

**Q.** What is Magic SysRq and what would you use it for on an embedded device?

**A.** 커널에 직접 들어가는 비상 키다. `/proc/sysrq-trigger`나 시리얼 BREAK 뒤 한 글자로 쓴다. 장비가 멈췄을 때 `w`로 D 상태 task 콜스택, `l`로 CPU별 백트레이스, `t`로 전체 task, `m`으로 메모리를 뽑는다. `c`는 kdump·pstore 테스트용 강제 panic, `s`·`u`·`b`는 안전한 재부팅 순서, `g`는 kgdb 진입이다. `kernel.sysrq` 마스크는 키보드·시리얼에만 적용되고 `/proc/sysrq-trigger`는 root면 항상 된다. 양산품에서는 BREAK 한 번으로 재부팅되지 않게 마스크나 sequence를 설정한다.

> It's an emergency interface straight into the kernel. You trigger it by writing a letter to /proc/sysrq-trigger, or by sending a serial BREAK followed by a key. When a device hangs, I'd grab w first for blocked tasks with their stacks, then l for per-CPU backtraces, t for every task, and m for memory. c forces a panic, which is handy for testing kdump or pstore. s, u, b is the safe reboot order, and g drops into kgdb. One detail: the kernel.sysrq mask only applies to keyboard and serial input. Root can always use /proc/sysrq-trigger. On production units I'd restrict the mask so that a stray BREAK on the console can't reboot the box.

**Q.** How do you get symbols for a loadable module in gdb?

**A.** `.ko`는 섹션 주소가 0인 relocatable ELF이고, 실제 주소는 insmod 때 정해진다. `/sys/module/<이름>/sections/`의 `.text`, `.data`, `.bss` 등을 `add-symbol-file`에 넘기거나 `lx-symbols`를 쓰면 `struct module`의 섹션 정보를 읽어 자동으로 하고, `do_init_module`에 브레이크포인트를 걸어 새 모듈도 따라간다. init 함수를 디버깅하려면 insmod 전에 이것을 걸어 둔다. 실제로 해 보니 gdb 버전에 따라 DWARF 주소가 틀리게 나올 수 있어서 `info symbol`이나 kallsyms로 교차 확인한다.

> A .ko is a relocatable ELF with every section at address zero. The real addresses only get decided when insmod loads it. You can pass .text, .data, .bss and so on from /sys/module/name/sections to add-symbol-file. Or you use lx-symbols, which reads the section addresses out of struct module and hooks do_init_module, so modules loaded later get picked up too. To debug a module's init function, set that up before insmod. One lesson from practice: I've seen gdb get DWARF addresses wrong for modules, so I always cross-check with info symbol or /proc/kallsyms.

**Q.** What changes when you halt a Linux system in a debugger, compared to halting an MCU?

**A.** OS가 시간을 본다. 멈춘 동안 시스템 카운터가 계속 가면 깨어날 때 softlockup·RCU stall·workqueue lockup 경고, 타임아웃, 네트워크 끊김이 생긴다. kgdb는 일부 감시만 리셋한다. 외부 watchdog은 보드를 리셋한다. QEMU는 가상 시계까지 세워서 경고는 없지만 RTC만 점프한다. 그 밖에 cpuidle로 꺼진 코어는 디버거에서 사라지고, 스텝 중 타이머 IRQ가 끼어들며, preemption 때문에 task가 다른 CPU로 옮겨 간다.

> The OS keeps track of time. If the system counter keeps running while you're halted, you resume into softlockup, RCU stall and workqueue lockup warnings, plus timeouts and dropped connections. kgdb resets some of those detectors, but not the workqueue one. An external watchdog will just reset the board. QEMU stops its virtual clock, so you get no warnings, but the RTC jumps. Beyond time, cores that cpuidle has powered down disappear from the debugger, timer interrupts get in the way when you single-step, and with preemption your task can move to another CPU between two steps.

**Q.** How do you debug the kernel before the MMU is turned on?

**A.** 그 구간은 물리 주소에서 돈다. 하드웨어 브레이크포인트를 쓰고, `vmlinux`를 "물리 로드 주소 − 링크 주소" 오프셋으로 다시 로드하면 심볼 이름으로 걸 수 있다. MMU를 켜는 `__enable_mmu` 뒤에도 잠시 idmap 위에서 물리 주소로 돌다가 가상 주소로 점프하니, 그 지점에서 원래 심볼로 바꾼다. 메모리는 물리로 직접 읽는다(MEM-AP, QEMU `xp`).

> Until the MMU is on, the code runs at physical addresses. I use hardware breakpoints, and I reload vmlinux with an offset of physical load address minus link address, so I can still break by symbol name. Right after __enable_mmu the code keeps running on the identity map for a moment, still at physical addresses. Then it branches to the virtual __primary_switched, and that's where I switch back to the normal symbols. For memory I read physical addresses directly, through the MEM-AP on real hardware or with xp on QEMU.

**Q.** Your kernel prints "hard LOCKUP on cpu 1" but the backtrace looks unrelated. Why?

**A.** arm64에서 pseudo-NMI가 꺼져 있으면 "NMI backtrace"는 사실 일반 IPI다. IRQ가 막힌 CPU는 그 IPI를 마스크가 풀린 뒤에야 처리해서, 백트레이스는 hang이 풀린 뒤의 엉뚱한 문맥이 찍힌다. 이 노트의 실험에서도 hang은 hrtimer 콜백에 있었는데 로그 백트레이스는 나중에 돈 `run_timer_base`나 kallsyms 읽기를 가리켰다. pseudo-NMI를 켜거나 JTAG로 본다.

> On arm64 without pseudo-NMI, the "NMI backtrace" is really an ordinary IPI. A CPU with interrupts masked only takes it once the mask is lifted, so the backtrace you get is from whatever it was doing after the hang cleared. I reproduced exactly that: the hang was in an hrtimer callback, but the logged backtrace pointed at code that ran later, like run_timer_base in one run and a kallsyms read in another. You fix the visibility by enabling pseudo-NMI, with CONFIG_ARM64_PSEUDO_NMI and irqchip.gicv3_pseudo_nmi=1, or you look at the core with JTAG.

---

## 14. 직접 해보기

1. **개념**: 예제 2에서 `$lx_current()`가 `swapper/1`이었다. kgdb(예제 7)에서는 같은 헬퍼가 실패했다. 두 환경에서 gdb "thread"가 무엇을 뜻하는지 설명하라.
   정답/힌트: gdbstub/JTAG는 thread = CPU, kgdb는 thread = task(+ shadowCPU 가짜 thread). 헬퍼는 thread 번호를 CPU 번호로 쓴다.
2. **코드 과제**: 예제 6을 응용해 `m4_s.buf[0]`을 **읽는** 코드를 잡는 read watchpoint(`rwatch -l`)를 걸어 보라. 몇 번째 명령에서 멈추나?
   정답/힌트: `m4_tick`의 `return m4_s.guard`는 `guard`를 읽으니 `rwatch -l *(int *)0xffff80007bfa2030`이 500 ms마다 멈춘다. 워치포인트 4개 한도 안에서 쓴다.
3. **관찰**: 예제 12를 `-icount shift=auto`나 `-rtc clock=vm` 옵션으로 다시 돌려 RTC 점프가 사라지는지 보라.
   정답/힌트: `-rtc clock=vm`이면 RTC도 가상 시계를 따라가 점프가 없어야 한다(실행해서 확인).
4. **코드 과제**: `m4_hang.c`의 루프를 `readl_poll_timeout_atomic()` 스타일(`ktime` 기반 타임아웃 10 ms + `pr_err`)로 고치고, hard lockup 경고가 사라지는지 확인하라.
   정답/힌트: `#include <linux/iopoll.h>`의 `read_poll_timeout_atomic(READ_ONCE, v, v, 10, 10000, false, m4_release)` 형태를 쓸 수 있다.
5. **관찰**: kdb에서 `ps A`, `bta`(모든 task 백트레이스), `summary`, `md` 대신 `mdp`(물리 주소)를 써 보라. 예제 3의 `0x40200000`을 `mdp`로 읽으면 무엇이 나오나?
   정답/힌트: `MZ` 헤더 `4d 5a 40 fa ...` — `monitor xp` 결과와 같아야 한다.
6. **개념**: 실보드 JTAG로 커널을 60초 멈췄다가 재개했더니 "rcu: INFO: rcu_preempt detected stalls"가 떴다. 버그인가? 확인 방법은?
   정답/힌트: 정지 시각과 경고 시각을 비교한다. 정지 중 system counter가 흘렀으면 디버거 부작용. CNTCR.HDBG/CTI로 카운터 정지를 설정하거나 `rcupdate.rcu_cpu_stall_suppress=1`로 디버깅 동안 끈다.
7. **설계**: 양산 장비에서 디버그 가능성과 보안을 둘 다 잡으려면? (JTAG, kgdb, SysRq 각각)
   정답/힌트: JTAG는 fuse 잠금 + 인증 디버그(L5), kgdb는 양산 커널에서 `=n`·RMA용 디버그 커널, SysRq는 마스크 축소 + serial sequence.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| gdbstub | gdb 원격 서버 | gdb의 RSP 패킷을 받아 타깃을 조종하는 코드. QEMU 안(`-s -S`), 커널 안(kgdb), OpenOCD에 있다 |
| RSP | Remote Serial Protocol | `$packet#checksum` 형태의 gdb 원격 규약 |
| kgdb | kernel gdb | 커널 안의 gdb stub. 예외 시 모든 CPU를 세우고 UART로 gdb와 통신 |
| kgdboc | kgdb over console | 기존 tty 드라이버의 폴링 콜백을 빌려 쓰는 kgdb I/O 드라이버 |
| kdb | kernel debugger shell | kgdb 코어 위의 사람용 명령 셸(`bt`, `ps`, `md`, `go`) |
| `kgdbwait` | 부팅 인자 | kgdboc 등록 즉시 정지하고 gdb를 기다림 |
| Magic SysRq | 비상 키 | 키 하나로 커널 상태 덤프·동작. `/proc/sysrq-trigger`, serial BREAK |
| serial BREAK | UART 신호 | 프레임보다 긴 low 구간. 다음 글자를 SysRq로 해석 |
| KASLR | 커널 주소 무작위화 | 부팅마다 커널 이미지를 무작위 오프셋에 둔다 |
| `scripts/gdb` | gdb Python 헬퍼 | `lx-*` 명령과 `$lx_*` 함수. `CONFIG_GDB_SCRIPTS` |
| `lx-symbols` | 모듈 심볼 로더 | `struct module`에서 섹션 주소를 읽어 `add-symbol-file` |
| hw breakpoint | 하드웨어 브레이크포인트 | 디버그 레지스터 주소 비교. 메모리를 안 바꿈. A57은 6개 |
| watchpoint | 데이터 브레이크포인트 | 주소 접근(쓰기/읽기) 시 정지. A57은 4개 |
| CoreSight | ARM 디버그·트레이스 구조 | DAP, AP, CTI, ETM, Funnel, ETF/ETR, TPIU |
| DAP / AP | Debug Access Port / Access Port | 프로브 입구 / 버스별 접근 창구(APB-AP, AXI-AP) |
| CTI | Cross Trigger Interface | 코어 간 halt·restart 신호 전달 |
| ETM | Embedded Trace Macrocell | 명령 실행 흐름 트레이스 |
| DBGEN / SPIDEN | 디버그 인증 신호 | 비보안/보안 invasive debug 허용. fuse·TF-A가 제어 |
| pseudo-NMI | arm64 가짜 NMI | GICv3 우선순위로 IRQ 마스크 중에도 받는 인터럽트. lockup 백트레이스에 필요 |
| buddy hardlockup | 하드 lockup 감지 방식 | CPU가 이웃 CPU의 hrtimer 카운트 정체를 감시 |
| idmap | identity map | 가상 = 물리인 임시 매핑. MMU 켜는 순간 쓰임 |

---

## 16. 요약 & 체크리스트

Linux 커널 디버거는 JTAG 경험 위에 세 가지를 더한 것이다. 첫째, **주소**: 커널은 가상 주소에서 돌고 KASLR로 옮겨지며, 모듈은 insmod 때 배치되니 `vmlinux`·`.ko` 심볼을 실제 주소에 맞춰 로드해야 한다(`nokaslr`, `symbol-file -o`, `lx-symbols`, 그리고 `info symbol`로 검증). 둘째, **도구의 위치**: QEMU gdbstub·JTAG는 커널 밖에서 코어를 세워 어떤 상태든 보지만, kgdb/kdb는 커널 안에 있어 UART 하나로 되는 대신 커널이 살아 있어야 하고 IRQ가 막힌 코어는 못 본다. Magic SysRq는 멈추지 않고 상태를 덤프하는 첫 번째 수단이다. 셋째, **멈춤의 부작용**: OS는 시간을 보므로 정지 후 watchdog·RCU·workqueue 경고, 외부 WDT 리셋, RTC 점프가 생긴다. 이 랩에서는 IRQ 핸들러 hang을 나중에 붙어서 PC·DAIF·콜스택으로 확정하고 변수를 바꿔 풀었다. 그리고 pseudo-NMI 없는 arm64의 lockup 백트레이스가 엉뚱한 문맥을 찍는 것을 직접 봤다.

- [ ] MCU JTAG과 Linux 커널 디버깅의 차이(MMU·KASLR·SMP·시간·전원·보안)를 표로 설명할 수 있다
- [ ] QEMU를 `-s -S`로 띄우고 `hbreak start_kernel`, `lx-dmesg`, `lx-ps`, `lx-lsmod`, `$lx_current()`, `$lx_per_cpu()`를 쓸 수 있다
- [ ] MMU 켜기 전 코드에 `symbol-file vmlinux -o <phys−virt>`로 심볼 이름 브레이크포인트를 걸 수 있다
- [ ] KASLR 오프셋을 `VBAR_EL1`(또는 kallsyms)로 구해 심볼을 맞출 수 있다
- [ ] 모듈 심볼을 `/sys/module/*/sections`나 `lx-symbols`로 올리고, 결과를 `info symbol`로 검증할 수 있다
- [ ] 하드웨어 워치포인트로 메모리를 덮어쓴 명령과 문맥을 잡을 수 있다
- [ ] kgdb 구조(kgdboc 폴링, debug_core, roundup IPI, BRK)와 진입 경로 4가지를 말할 수 있다
- [ ] `kgdboc=ttyS0 kgdbwait`로 gdb를 붙이고, SysRq-g와 serial BREAK로 kdb에 들어가 `bt`, `ps`, `lsmod`, `md`, `dmesg`, `go`를 쓸 수 있다
- [ ] Magic SysRq의 `t w l p m q c s u b g`를 설명하고 `kernel.sysrq` 마스크와 `/proc/sysrq-trigger`의 차이를 안다
- [ ] IRQ 핸들러 hang을 디버거로 찾는 순서와, pseudo-NMI 없는 lockup 백트레이스를 왜 믿으면 안 되는지 말할 수 있다
- [ ] 디버거 정지가 만드는 부작용(가상 시계·카운터·workqueue 경고·외부 WDT·cpuidle)을 구분할 수 있다
- [ ] Cortex-A SoC의 CoreSight 경로(DAP→APB-AP/AXI-AP→코어 debug·CTI·ETM)와 OpenOCD `target create ... aarch64` 설정의 뼈대를 설명할 수 있다

## 참고 자료

- docs.kernel.org — "Debugging kernel and modules via gdb" (`Documentation/process/debugging/gdb-kernel-debugging.rst`): https://docs.kernel.org/process/debugging/gdb-kernel-debugging.html
- docs.kernel.org — "Using kgdb, kdb and the kernel debugger internals" (`Documentation/process/debugging/kgdb.rst`): https://docs.kernel.org/process/debugging/kgdb.html
- docs.kernel.org — "Linux Magic System Request Key Hacks" (`Documentation/admin-guide/sysrq.rst`): https://docs.kernel.org/admin-guide/sysrq.html
- docs.kernel.org — "Softlockup detector and hardlockup detector" (`Documentation/admin-guide/lockup-watchdogs.rst`)
- 커널 소스(6.18.55): `kernel/debug/debug_core.c`(`dbg_touch_watchdogs`, `kgdb_cpu_enter`), `kernel/debug/gdbstub.c`, `kernel/debug/kdb/`, `drivers/tty/serial/kgdboc.c`, `arch/arm64/kernel/kgdb.c`, `drivers/tty/sysrq.c`(`sysrq_key_table`), `scripts/gdb/linux/symbols.py`, `arch/arm64/kernel/head.S` — https://elixir.bootlin.com/linux/v6.18/source
- QEMU 문서 — "GDB usage": https://www.qemu.org/docs/master/system/gdb.html
- OpenOCD User's Guide — "CPU Configuration", "Architecture and Core Commands"(ARMv8-A `aarch64`): https://openocd.org/doc/html/index.html
- Arm — "CoreSight Architecture Specification", "Arm Architecture Reference Manual for A-profile"(External debug, Generic Timer 장) — developer.arm.com
- Lauterbach — "Training Linux Debugging"/"RTOS Debugger for Linux" 문서 (TRACE32 설치 폴더 `~~/pdf/`, lauterbach.com)
- 관련 노트: C6 §2, E3, E6, F3, F7, L5 §9.4, L7, M1, M3 §6~7, M6, M8, M11, N1
