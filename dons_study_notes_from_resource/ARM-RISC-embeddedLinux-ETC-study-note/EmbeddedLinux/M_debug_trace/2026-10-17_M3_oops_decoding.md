# M3. Oops·panic 해독 — 콜스택, PC/LR, addr2line, decode_stacktrace.sh, System.map, BUG/WARN

> **이 노트를 다 읽으면**: arm64 Oops를 첫 줄부터 `---[ end trace ]---`까지 한 줄씩 읽고 ESR을 비트 단위로 풀 수 있다 · `faddr2line`·`decode_stacktrace.sh`·`addr2line`·`gdb`·`decodecode`·`pahole`로 `pc`를 소스 줄·구조체 멤버까지 바꾸고, 각 도구가 **언제 틀린 답을 내는지** 안다 · KASLR 주소를 `System.map` 주소로 되돌리고, 0x10·`6b6b…`·`dead…100`·`4141…` 같은 주소 패턴만 보고 버그 종류를 추정할 수 있다 · Oops / panic / BUG / WARN / lockup을 구별하고 `panic_on_oops`·`panic_on_warn`의 의미를 설명할 수 있다
> **왜 필요한가**: 면접 단골 질문이 "Here's an oops — walk me through it", "oops 로그 보면 뭐부터 봐요?"다. 실무에서는 고객 장비의 콘솔 로그 한 덩어리(또는 pstore 파일 하나)만 받고 원인을 찾아야 하는 일이 매주 생긴다
> **Don 기준 난이도**: Cortex-M HardFault 분석(CFSR/BFAR/stacked PC/LR → map 파일 → 소스 줄)은 이미 몸으로 안다 — Oops는 **같은 일을 MMU·KASLR·모듈·인라인이 있는 세계에서 하는 것**이다 / 새로 배울 부분은 ESR 필드, frame record 기반 unwinder의 맹점, 모듈 섹션 오프셋, KASLR 보정, 크래시 지점 ≠ 버그 지점 사고법
> **선행 노트**: E3(커널 모듈), E4(커널 제약 — §4 스택 overflow, §8.3 Oops 첫 읽기·그림 4), E6(유저↔커널 경계, `el0_svc`), F4(락), F9(slab 할당자), M1(printk·콘솔·earlycon), M2(`/proc/modules`·taint 비트 표) — M4(kgdb·JTAG), M8(KASAN·lockdep 같은 탐지기), L7(pstore·kdump로 크래시 회수)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어에서 HardFault를 잡던 순서를 떠올려 보자. ① HardFault_Handler가 예외 스택 프레임(r0–r3, r12, LR, PC, xPSR)과 CFSR·HFSR·BFAR·MMFAR를 RAM/UART에 덤프한다. ② CFSR 비트를 보고 "precise bus fault, BFAR 유효" 같은 **원인**을 정한다. ③ stacked PC를 `.map` 파일이나 `arm-none-eabi-addr2line`에 넣어 **소스 줄**을 찾는다. ④ stacked LR로 **누가 불렀는지** 본다. ⑤ BFAR 값 자체(0x00000010? 0xDEADBEEF?)에서 **버그 종류**를 추정한다.

Linux 커널의 Oops도 정확히 이 다섯 단계다. 차이는 세 가지뿐이다.

- 커널이 덤프를 **알아서, 사람이 읽을 수 있게** 찍어 준다 (ESR 필드 해석, 심볼 이름, 콜스택까지).
- 주소가 **KASLR로 매 부팅 무작위**라서 `System.map`과 바로 비교할 수 없고, 모듈은 로드될 때마다 다른 곳에 놓인다.
- 대부분의 경우 **시스템이 죽지 않는다**. Oops는 "현재 태스크를 죽이고 계속 간다"가 기본이다. 그래서 "죽었다"가 아니라 "이상해졌다"로 보고되는 버그가 많다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" font-size="13">Cortex-M (Don이 아는 세계)</text>
  <rect x="10" y="34" width="150" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="58" text-anchor="middle" font-size="12">Fault 발생</text>
  <text x="85" y="76" text-anchor="middle" font-size="12">MemManage/BusFault</text>
  <rect x="178" y="34" width="150" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="253" y="58" text-anchor="middle" font-size="12">HardFault_Handler</text>
  <text x="253" y="76" text-anchor="middle" font-size="12">(내가 짠 핸들러)</text>
  <rect x="346" y="34" width="150" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="421" y="58" text-anchor="middle" font-size="12">CFSR · BFAR</text>
  <text x="421" y="76" text-anchor="middle" font-size="12">stacked PC · LR 덤프</text>
  <rect x="514" y="34" width="156" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="592" y="58" text-anchor="middle" font-size="12">리셋 또는 정지</text>
  <text x="592" y="76" text-anchor="middle" font-size="12">(시스템 전체)</text>
  <line x1="160" y1="62" x2="176" y2="62" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="328" y1="62" x2="344" y2="62" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="496" y1="62" x2="512" y2="62" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="10" y="134" font-size="13">Linux arm64 (이 노트)</text>
  <rect x="10" y="146" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="170" text-anchor="middle" font-size="12">Data Abort / BRK</text>
  <text x="85" y="188" text-anchor="middle" font-size="12">(EL1에서 동기 예외)</text>
  <rect x="178" y="146" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="253" y="170" text-anchor="middle" font-size="12">do_mem_abort</text>
  <text x="253" y="188" text-anchor="middle" font-size="12">bug_brk_handler</text>
  <rect x="346" y="146" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="421" y="170" text-anchor="middle" font-size="12">die(): Oops 출력</text>
  <text x="421" y="188" text-anchor="middle" font-size="12">ESR · FAR · pc · lr</text>
  <rect x="514" y="146" width="156" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="592" y="170" text-anchor="middle" font-size="12">태스크만 kill</text>
  <text x="592" y="188" text-anchor="middle" font-size="12">또는 panic</text>
  <line x1="160" y1="174" x2="176" y2="174" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="328" y1="174" x2="344" y2="174" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="496" y1="174" x2="512" y2="174" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="10" y="226" width="660" height="62" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="250" text-anchor="middle" font-size="12">사람이 하는 일은 같다: 원인(ESR/CFSR) → 위치(pc → 소스 줄) → 호출자(lr, Call trace)</text>
  <text x="340" y="272" text-anchor="middle" font-size="12">→ 주소 값의 의미(FAR/BFAR) → 크래시 지점이 진짜 버그 지점인가?</text>
</svg>
```

그림 1 — HardFault 분석과 Oops 분석은 같은 다섯 단계다. Linux는 덤프를 커널이 해석까지 해서 찍어 주고, 기본 동작이 "시스템 정지"가 아니라 "현재 태스크만 죽이고 계속"이라는 점이 다르다.

### 0.1 이 노트의 실험 — 진짜 Oops 14개

이 노트의 모든 로그는 랩 커널 `6.18.55-trace`(`/var/lab/out/Image-trace`, `vmlinux-trace`에 DWARF5 전체 디버그 정보)를 QEMU `virt`(cortex-a57, 2 CPU, TCG)로 부팅해서 실제로 받은 콘솔 출력이다. VM 호스트 커널은 `uname -r` → `7.0.0-38-generic`이고, 버그 모듈은 **호스트에 절대 넣지 않고** 일회용 QEMU 게스트에만 넣었다.

버그는 실험용 모듈 하나(`m3_bugs.ko`)에 모아 두고, `/sys/module/m3_bugs/parameters/trigger`에 이름을 쓰면 해당 버그 하나가 실행되게 했다. 이 노트는 버그 코드가 아니라 **출력을 읽는 법**이 주제라서 모듈 소스는 싣지 않는다. 각 트리거가 하는 일은 한 문장으로만 적는다.

| 트리거 | 모듈이 하는 일 (한 문장) | 결과 (커널이 찍은 첫 줄) | 절 |
|---|---|---|---|
| `null` | 조회 함수가 NULL을 돌려줬는데 확인 없이 그 구조체의 `irq` 멤버를 읽는다 | `Unable to handle kernel NULL pointer dereference at virtual address 0000000000000010` | §2–§6 |
| `uaf` | 객체를 `kfree`한 뒤 그 객체의 함수 포인터 테이블을 따라 호출한다 (`slab_debug=P`) | `Unable to handle kernel paging request at virtual address 006b6b6b6b6b6b6b` | §9.2 |
| `bugon` | magic 값이 틀린 구조체로 `BUG_ON` 검사를 한다 | `kernel BUG at m3_bugs.c:47!` | §8.2 |
| `warn` | 0인 모듈 파라미터를 `WARN_ON_ONCE`로 검사한다 (두 번 호출) | `WARNING: CPU: 0 PID: 79 at m3_bugs.c:96 ...` | §8.3 |
| `warnf` | 메시지가 붙은 `WARN(1, ...)`을 한 번 친다 | `m3: sensor imu0 timed out (-110)` | §8.3 |
| `div0` | 정수 100을 0으로 나눈다 | `m3_bugs: 100 / 0 = 0` (트랩 없음) | §9.7 |
| `stack` | 지역 배열을 가진 함수를 10만 번 재귀 호출한다 | `Insufficient stack space to handle exception!` | §9.6 |
| `sleep` | 스핀락을 잡은 채 `msleep(10)` 한다 | `BUG: scheduling while atomic: sh/79/0x00000002` | §8.4 |
| `list` | 연결 리스트 노드를 가진 객체에 문자 `'A'`를 24바이트 덮어쓴 뒤 리스트를 순회하며 출력한다 | `Unable to handle kernel paging request at virtual address 0041414141414139` | §9.4 |
| `dbldel` | 같은 리스트 노드에 `list_del`을 두 번 한다 | `... at virtual address dead000000000108` | §9.3 |
| `misalign` | 홀수 주소에 있는 `atomic_t`에 `atomic_inc_return`을 한다 | `FSC = 0x21: alignment fault` | §9.5 |
| `warn` + `panic_on_warn=1` | 위 `warn`과 같다 | `Kernel panic - not syncing: kernel: panic_on_warn set ...` | §8.5 |
| `null` + `oops=panic` | 위 `null`과 같다 | `Kernel panic - not syncing: Oops: Fatal exception` | §8.5 |
| `null` + `nokaslr` | 위 `null`과 같다 | 모듈 주소가 `0xffff80007bfa0000`로 고정 | §7 |
| (별도 모듈) | 모듈 init에서 선점을 끈 채 30초 동안 `mdelay`로 바쁘게 기다린다 | `watchdog: BUG: soft lockup - CPU#1 stuck for 13s!` | §8.6 |

실행 하네스는 이렇게 생겼다. 랩 BusyBox initramfs를 복사해 모듈과 스크립트 `t.sh`를 넣고, `LAB_CMD=sh~/t.sh`로 그 스크립트 하나만 돌린 뒤 꺼지게 한다(E4 §1.2와 같은 방식).

```sh
#!/bin/bash
# run.sh <tag> <bug> [extra kernel args]  — VM 호스트에서 실행
set -e
tag=$1; bug=$2; extra=$3
W=/var/tmp/M3/run_$tag; rm -rf $W; mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
cp /var/tmp/M3/m3_bugs/trace/m3_bugs.ko $W/r/
cat > $W/r/t.sh <<T
insmod /m3_bugs.ko
grep m3_bugs /proc/modules
cat /sys/module/m3_bugs/sections/.text
sh -c 'echo $bug > /sys/module/m3_bugs/parameters/trigger'
echo "rc=\$?"
T
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M \
  -nographic -no-reboot -kernel /var/lab/out/Image-trace -initrd $W/i.gz \
  -append "console=ttyAMA0 $extra LAB_CMD=sh~/t.sh" 2>&1 | tr -d '\r' > $W/console.txt || true
echo "exit; lines=$(wc -l < $W/console.txt)"
```

출력에서 볼 것: `echo`를 `sh -c '...'`로 한 번 감싼 이유는, Oops가 나면 **echo를 실행한 프로세스가 커널에 의해 죽기** 때문이다. 감싸 두면 바깥 셸은 살아서 `rc=139`(128 + SIGSEGV 11)를 찍고 다음 줄로 간다. `/proc/modules`의 로드 주소와 `/sys/module/m3_bugs/sections/.text`는 §6–§7에서 주소 계산에 쓴다. TCG 에뮬레이션이고 VM을 17명이 같이 쓰는 중이라 타임스탬프·도구 실행 시간은 실제 보드 수치가 아니다.

---

## 1. Oops란 무엇인가 — 정의 세 줄

- **Oops**: 커널 모드에서 처리할 수 없는 예외(잘못된 주소 접근, `BUG()`의 `brk` 명령, 정의되지 않은 명령 등)가 났을 때 커널이 진단 정보를 찍고 **현재 태스크를 죽이는** 것. 커널 자체는 계속 돈다. 이름 그대로 "앗, 실수".
- **panic**: 커널이 "더 진행하면 안 된다"고 판단하고 **모든 CPU를 멈추는** 것. `Kernel panic - not syncing: <이유>`로 시작한다. 인터럽트 문맥에서 Oops가 나거나, `panic_on_oops`가 켜져 있거나, init이 죽거나, 스택이 넘치면 panic으로 간다.
- **WARN**: "이 조건은 버그지만 계속 갈 수는 있다"는 경고. 콜스택을 찍고 taint `W`를 남기고 **그대로 다음 명령으로 진행한다**. 아무도 죽지 않는다.

`BUG`는 이름이 헷갈린다. `BUG()`/`BUG_ON()` 매크로는 **Oops를 일부러 일으키는 것**이고(`kernel BUG at file:line!`), `BUG: scheduling while atomic`, `BUG: soft lockup` 같은 `BUG:` 접두어 메시지는 **커널의 검사기가 문제를 보고하는 것**(대개 태스크를 죽이지 않는다)이다. §8에서 하나씩 실제 로그로 비교한다.

---

## 2. arm64 Oops 해부 — 한 줄씩

`null` 트리거의 Oops 전체다(부팅 로그 앞부분 생략, 줄 앞의 `[    59.0xxxxx][   T80]` 접두어는 시각과 `PRINTK_CALLER`가 붙인 "태스크 80" 표시다).

```text
[   59.025707][   T80] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000010
[   59.025829][   T80] Mem abort info:
[   59.025856][   T80]   ESR = 0x0000000096000006
[   59.025919][   T80]   EC = 0x25: DABT (current EL), IL = 32 bits
[   59.025960][   T80]   SET = 0, FnV = 0
[   59.025981][   T80]   EA = 0, S1PTW = 0
[   59.026007][   T80]   FSC = 0x06: level 2 translation fault
[   59.026034][   T80] Data abort info:
[   59.026051][   T80]   ISV = 0, ISS = 0x00000006, ISS2 = 0x00000000
[   59.026066][   T80]   CM = 0, WnR = 0, TnD = 0, TagAccess = 0
[   59.026081][   T80]   GCS = 0, Overlay = 0, DirtyBit = 0, Xs = 0
[   59.026131][   T80] user pgtable: 4k pages, 48-bit VAs, pgdp=00000000456a0000
[   59.026177][   T80] [0000000000000010] pgd=080000004500b403, p4d=080000004500b403, pud=080000004500c403, pmd=0000000000000000
[   59.030266][   T80] Internal error: Oops: 0000000096000006 [#1]  SMP
[   59.051074][   T80] Modules linked in: m3_bugs(OE)
[   59.055396][   T80] CPU: 1 UID: 0 PID: 80 Comm: sh Tainted: G           OE       6.18.55-trace #1 PREEMPT 
[   59.059561][   T80] Tainted: [O]=OOT_MODULE, [E]=UNSIGNED_MODULE
[   59.063021][   T80] Hardware name: linux,dummy-virt (DT)
[   59.064170][   T80] pstate: 60000005 (nZCv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[   59.066671][   T80] pc : m3_get_irq.constprop.0+0xc/0xa10 [m3_bugs]
[   59.071073][   T80] lr : m3_trigger+0x1fc/0x210 [m3_bugs]
[   59.071322][   T80] sp : ffff8000804fbc50
[   59.071459][   T80] x29: ffff8000804fbc50 x28: ffff000004fdc600 x27: 0000000000000000
...  (x26 ~ x1 생략)
[   59.085301][   T80] x2 : 000000000000000a x1 : ffffcd3f667be044 x0 : 0000000000000000
[   59.085301][   T80] Call trace:
[   59.086040][   T80]  m3_get_irq.constprop.0+0xc/0xa10 [m3_bugs] (P)
[   59.088443][   T80]  param_attr_store+0x80/0x110
[   59.088637][   T80]  module_attr_store+0x2c/0x50
[   59.088919][   T80]  sysfs_kf_write+0x84/0xa8
[   59.089174][   T80]  kernfs_fop_write_iter+0x134/0x210
[   59.089381][   T80]  vfs_write+0x248/0x390
[   59.089562][   T80]  ksys_write+0x6c/0x110
[   59.089728][   T80]  __arm64_sys_write+0x20/0x38
[   59.089941][   T80]  invoke_syscall.constprop.0+0x48/0xf0
[   59.092464][   T80]  el0_svc_common.constprop.0+0x40/0xf0
[   59.095035][   T80]  do_el0_svc+0x24/0x40
[   59.095249][   T80]  el0_svc+0x40/0x180
[   59.095426][   T80]  el0t_64_sync_handler+0xa0/0xf0
[   59.095584][   T80]  el0t_64_sync+0x198/0x1a0
[   59.096093][   T80] Code: ffffcd3f aa1e03e9 d503201f d2800000 (b9401000) 
[   59.096879][   T80] ---[ end trace 0000000000000000 ]---
Segmentation fault
rc=139
```

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <rect x="8" y="8" width="404" height="454" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="16" y="32" font-size="12">Unable to handle kernel NULL pointer deref ... 0x10</text>
  <text x="16" y="62" font-size="12">ESR = 0x96000006  EC = 0x25: DABT (current EL)</text>
  <text x="16" y="92" font-size="12">FSC = 0x06: level 2 translation fault   WnR = 0</text>
  <text x="16" y="122" font-size="12">[0000000000000010] pgd=... pud=... pmd=0000000000000000</text>
  <text x="16" y="152" font-size="12">Internal error: Oops: 0000000096000006 [#1]  SMP</text>
  <text x="16" y="182" font-size="12">Modules linked in: m3_bugs(OE)</text>
  <text x="16" y="212" font-size="12">CPU: 1 UID: 0 PID: 80 Comm: sh Tainted: G  OE</text>
  <text x="16" y="242" font-size="12">pstate: 60000005 (nZCv daif -PAN -UAO ...)</text>
  <text x="16" y="272" font-size="12">pc : m3_get_irq.constprop.0+0xc/0xa10 [m3_bugs]</text>
  <text x="16" y="302" font-size="12">lr : m3_trigger+0x1fc/0x210 [m3_bugs]</text>
  <text x="16" y="332" font-size="12">sp : ffff8000804fbc50   x29 ... x0 : 0000000000000000</text>
  <text x="16" y="362" font-size="12">Call trace: m3_get_irq... (P) / param_attr_store+0x80</text>
  <text x="16" y="392" font-size="12">Code: ffffcd3f aa1e03e9 d503201f d2800000 (b9401000)</text>
  <text x="16" y="422" font-size="12">---[ end trace 0000000000000000 ]---</text>
  <text x="16" y="452" font-size="12">Segmentation fault   (echo 프로세스가 SIGSEGV로 죽음)</text>
  <g stroke="currentColor" stroke-dasharray="3,3">
    <line x1="412" y1="28" x2="424" y2="28"/><line x1="412" y1="58" x2="424" y2="58"/>
    <line x1="412" y1="88" x2="424" y2="88"/><line x1="412" y1="118" x2="424" y2="118"/>
    <line x1="412" y1="148" x2="424" y2="148"/><line x1="412" y1="178" x2="424" y2="178"/>
    <line x1="412" y1="208" x2="424" y2="208"/><line x1="412" y1="238" x2="424" y2="238"/>
    <line x1="412" y1="268" x2="424" y2="268"/><line x1="412" y1="298" x2="424" y2="298"/>
    <line x1="412" y1="328" x2="424" y2="328"/><line x1="412" y1="358" x2="424" y2="358"/>
    <line x1="412" y1="388" x2="424" y2="388"/><line x1="412" y1="418" x2="424" y2="418"/>
    <line x1="412" y1="448" x2="424" y2="448"/>
  </g>
  <text x="430" y="32" font-size="12">① 무엇이: 접근 종류 + FAR</text>
  <text x="430" y="62" font-size="12">② 왜: 예외 신드롬 (§3)</text>
  <text x="430" y="92" font-size="12">③ L2 테이블에 매핑 없음, 읽기</text>
  <text x="430" y="122" font-size="12">④ show_pte: 어디서 끊겼나</text>
  <text x="430" y="152" font-size="12">⑤ 몇 번째 Oops — 항상 [#1]</text>
  <text x="430" y="182" font-size="12">⑥ 용의자 목록 + taint</text>
  <text x="430" y="212" font-size="12">⑦ 누가: CPU · PID · 명령</text>
  <text x="430" y="242" font-size="12">⑧ SPSR: 플래그 · IRQ 마스크 · EL</text>
  <text x="430" y="272" font-size="12" font-weight="bold">⑨ 죽은 곳 = ELR (stacked PC)</text>
  <text x="430" y="302" font-size="12" font-weight="bold">⑩ x30 = 돌아갈 곳 (stacked LR)</text>
  <text x="430" y="332" font-size="12">⑪ 레지스터: x0 = 0 → NULL</text>
  <text x="430" y="362" font-size="12">⑫ 경로: frame record 체인 (§4)</text>
  <text x="430" y="392" font-size="12">⑬ 명령어: ( ) 안이 죽은 명령</text>
  <text x="430" y="422" font-size="12">⑭ 끝 — Oops면 태스크만 죽음</text>
  <text x="430" y="452" font-size="12">⑮ 유저가 보는 결과</text>
</svg>
```

그림 2 — Oops 해부도(`null` 트리거의 실제 값). 굵게 표시한 ⑨·⑩이 HardFault의 stacked PC·LR에 해당하고, ①·②가 BFAR·CFSR에 해당한다. E4 그림 4의 9단계 해부를 arm64 필드까지 더 쪼갠 것이다.

### 2.1 줄별 의미표

| 줄 | 뜻 | 이번 값에서 읽을 것 | Cortex-M 대응 |
|---|---|---|---|
| `Unable to handle kernel NULL pointer dereference at virtual address 0000000000000010` | 커널 모드 data abort. 주소가 첫 페이지(< `PAGE_SIZE`) 안이면 "NULL pointer dereference", 아니면 "paging request"라고 쓴다 | FAR = 0x10. NULL + 0x10이다 → 구조체 포인터가 NULL이고 오프셋 0x10의 멤버를 읽었다 | BFAR/MMFAR |
| `Mem abort info:` ~ `Xs = 0` | ESR_EL1을 필드별로 풀어 준 것 | EC 0x25 = 현재 EL(커널)의 data abort, FSC 0x06 = L2 translation fault, WnR 0 = 읽기 | CFSR(MMFSR/BFSR) |
| `user pgtable: 4k pages, 48-bit VAs, pgdp=...` | 주소가 유저 영역(TTBR0)이라 유저 페이지 테이블을 걸었다는 뜻. 커널 주소면 `swapper pgtable` | 0x10은 유저 주소 공간 쪽이다. 커널 코드가 유저 주소를 만진 것 | 없음 (MPU는 단일 맵) |
| `[0000000000000010] pgd=... pmd=0000000000000000` | `show_pte()`가 4단계 테이블을 따라가며 각 항목 값 출력 | `pmd=0` — L2 단계에서 끊겼다. FSC "level 2"와 일치한다 | 없음 |
| `Internal error: Oops: 0000000096000006 [#1]  SMP` | `die()`가 찍는 머리줄. 콜론 뒤 숫자는 ESR, `[#1]`은 부팅 후 몇 번째 die인가 | **#1이 아닌 Oops는 앞선 Oops의 후유증일 수 있다** — 항상 #1부터 본다 | 없음 |
| `Modules linked in: m3_bugs(OE)` | 로드된 모듈 전부와 각 모듈의 taint | 용의자는 out-of-tree(O)·서명 없음(E) 모듈 하나 | 없음 |
| `CPU: 1 UID: 0 PID: 80 Comm: sh Tainted: G ... OE ... 6.18.55-trace #1 PREEMPT` | 죽은 CPU, 현재 태스크(echo를 실행한 `sh`), taint 문자열, 커널 버전·빌드 번호와 선점 모델 | 시스템콜 문맥(프로세스 문맥)에서 죽었다. 인터럽트 문맥이면 panic으로 갔을 것이다 | 현재 태스크 ID (RTOS) |
| `Tainted: [O]=OOT_MODULE, [E]=UNSIGNED_MODULE` | taint 문자의 풀이 (비트 표는 M2) | 커뮤니티에 보고하면 "out-of-tree 모듈 없이 재현되나?"부터 묻는다 | 없음 |
| `pstate: 60000005 (nZCv daif ...)` | 예외 직전의 PSTATE(SPSR_EL1). 대문자 = 1 | N=0 Z=1 C=1 V=0, `daif` 소문자 = IRQ·FIQ 모두 **열려 있음**, 하위 4비트 0x5 = EL1h | stacked xPSR |
| `pc : m3_get_irq.constprop.0+0xc/0xa10 [m3_bugs]` | ELR_EL1 — 예외를 낸 명령의 주소를 `심볼+오프셋/크기 [모듈]`로 | 죽은 곳. `.constprop.0`은 GCC가 상수 인자를 박아 복제한 함수라는 표시 | stacked PC |
| `lr : m3_trigger+0x1fc/0x210 [m3_bugs]` | x30 — 마지막 `bl`의 복귀 주소 | 누가 불렀나(대개). 단 §4.3처럼 **낡은 값**일 수 있다 | stacked LR |
| `sp`, `x29` ~ `x0` | 범용 레지스터 31개 | `x0 = 0` — NULL 포인터가 x0에 있었다 | r0–r12 |
| `Call trace:` | frame record(x29 체인)를 따라간 콜스택. `(P)`는 pc에서 온 첫 줄 | 위에서 아래로 = 안쪽에서 바깥쪽(유저 syscall 진입 `el0t_64_sync`까지) | 수동 스택 워크 |
| `Code: ... (b9401000)` | pc 앞 4개 + pc의 명령어 워드. 괄호 안이 죽은 명령 | `b9401000` = `ldr w0, [x0, #16]` (§5) | 없음 |
| `---[ end trace 0000000000000000 ]---` | 출력 끝. 숫자는 예전 trace id 자리(현재 0) | 이 뒤 `Segmentation fault`는 유저가 본 결과 | — |

### 2.2 pstate 읽는 법

`pstate`는 예외가 나기 직전 CPU 상태다. 이번 실험에서 세 가지 값이 나왔다.

| 값 | 표시 | 의미 | 어느 실험 |
|---|---|---|---|
| `60000005` | `nZCv daif` | 플래그 Z·C, 인터럽트 전부 허용, EL1h | null, uaf, bugon, warn… (평범한 프로세스 문맥) |
| `000000c5` | `nzcv daIF` | **IRQ·FIQ 마스크**(I, F 대문자) | list — `printk` 내부에서 IRQ를 끈 채로 죽었다 |
| `200003c5` | `nzCv DAIF` | D·A·I·F 전부 마스크 | bugon 뒤의 후속 WARN — idle 진입 중 |

IRQ가 꺼진 상태에서 죽었다면 "스핀락(`spin_lock_irqsave`) 구간 안이었나?"를 먼저 의심한다. 실제로 list 실험은 Oops 뒤에 `note: sh[79] exited with irqs disabled`가 따라 나왔다 — 이 태스크가 IRQ를 끈 채로 죽어서, 커널이 대신 그 사실을 기록한 것이다.

---

## 3. ESR — "왜 죽었나"를 비트로 읽기

ESR_EL1(Exception Syndrome Register)은 Cortex-M의 CFSR에 해당한다. 예외 원인을 32비트에 담는다(상위 32비트는 ISS2로 FEAT_LS64 같은 확장용).

- **EC**(Exception Class, 비트 31:26): 예외의 큰 분류. 0x25 = 현재 EL의 data abort, 0x24 = 하위 EL(유저)의 data abort, 0x21 = 현재 EL의 instruction abort, 0x3c = BRK 명령, 0x00 = 알 수 없음(UNDEF 명령 등).
- **IL**(비트 25): 명령어 길이. 1 = 32비트 명령(A64는 항상 1).
- **ISS**(비트 24:0): EC별 세부 정보. data abort라면 아래 필드가 있다.

| ISS 필드 | 비트 | 뜻 |
|---|---|---|
| ISV | 24 | 1이면 비트 23:14에 "어떤 레지스터로 몇 바이트 로드/스토어했나"가 유효 (커널 abort에서는 대개 0) |
| SET | 12:11 | 동기 에러 타입 (RAS 확장) |
| FnV | 10 | **FAR not Valid**. 1이면 FAR 값을 믿지 말 것 (Cortex-M의 BFARVALID=0과 같은 의미, 극성은 반대) |
| EA | 9 | External Abort — 버스(인터커넥트)가 에러 응답을 돌려줌. Don이 아는 "bus error"가 이것이다 |
| CM | 8 | 캐시 유지보수 명령(`dc`)이 낸 abort |
| S1PTW | 7 | 페이지 테이블을 걷다가(stage 2) 난 abort |
| WnR | 6 | **Write not Read** — 1 = 쓰기, 0 = 읽기 |
| DFSC | 5:0 | Data Fault Status Code — 0x04–0x07 translation fault L0–L3, 0x09–0x0b access flag, 0x0d–0x0f permission fault, 0x10 동기 external abort, 0x21 alignment fault |

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13">ESR = 0x96000006 (null 트리거)  = 1001 0110 0000 ... 0000 0110</text>
  <rect x="20" y="36" width="120" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="80" y="54" text-anchor="middle" font-size="12">EC [31:26]</text>
  <text x="80" y="72" text-anchor="middle" font-size="12">100101 = 0x25</text>
  <rect x="140" y="36" width="40" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="160" y="54" text-anchor="middle" font-size="12">IL</text>
  <text x="160" y="72" text-anchor="middle" font-size="12">1</text>
  <rect x="180" y="36" width="480" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="420" y="54" text-anchor="middle" font-size="12">ISS [24:0]</text>
  <text x="420" y="72" text-anchor="middle" font-size="12">0x0000006</text>
  <line x1="180" y1="80" x2="20" y2="120" stroke="currentColor" stroke-dasharray="3,3"/>
  <line x1="660" y1="80" x2="660" y2="120" stroke="currentColor" stroke-dasharray="3,3"/>
  <rect x="20" y="120" width="50" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="45" y="140" text-anchor="middle" font-size="12">ISV</text>
  <text x="45" y="160" text-anchor="middle" font-size="12">0</text>
  <rect x="70" y="120" width="130" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="135" y="140" text-anchor="middle" font-size="12">[23:13]</text>
  <text x="135" y="160" text-anchor="middle" font-size="12">ISV=1일 때만</text>
  <rect x="200" y="120" width="56" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="228" y="140" text-anchor="middle" font-size="12">SET</text>
  <text x="228" y="160" text-anchor="middle" font-size="12">00</text>
  <rect x="256" y="120" width="50" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="281" y="140" text-anchor="middle" font-size="12">FnV</text>
  <text x="281" y="160" text-anchor="middle" font-size="12">0</text>
  <rect x="306" y="120" width="44" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="328" y="140" text-anchor="middle" font-size="12">EA</text>
  <text x="328" y="160" text-anchor="middle" font-size="12">0</text>
  <rect x="350" y="120" width="44" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="372" y="140" text-anchor="middle" font-size="12">CM</text>
  <text x="372" y="160" text-anchor="middle" font-size="12">0</text>
  <rect x="394" y="120" width="60" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="424" y="140" text-anchor="middle" font-size="12">S1PTW</text>
  <text x="424" y="160" text-anchor="middle" font-size="12">0</text>
  <rect x="454" y="120" width="50" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="479" y="140" text-anchor="middle" font-size="12">WnR</text>
  <text x="479" y="160" text-anchor="middle" font-size="12">0 읽기</text>
  <rect x="504" y="120" width="156" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="582" y="140" text-anchor="middle" font-size="12">DFSC [5:0]</text>
  <text x="582" y="160" text-anchor="middle" font-size="12">000110 = L2 transl.</text>
  <text x="20" y="206" font-size="13">이 노트에서 실제로 나온 ESR 6개</text>
  <line x1="20" y1="214" x2="660" y2="214" stroke="currentColor"/>
  <text x="20" y="236" font-size="12">0x96000006  DABT  WnR=0  DFSC 0x06 L2 transl.  → null: 0x10 읽기</text>
  <text x="20" y="262" font-size="12">0x96000004  DABT  WnR=0  DFSC 0x04 L0 transl.  → uaf 6b6b…, list 4141…</text>
  <text x="20" y="288" font-size="12">0x96000044  DABT  WnR=1  DFSC 0x04 L0 transl.  → dbldel: dead…108 쓰기</text>
  <text x="20" y="314" font-size="12">0x96000021  DABT  WnR=0  DFSC 0x21 alignment   → misalign: ldxr 홀수 주소</text>
  <text x="20" y="340" font-size="12">0x96000047  DABT  WnR=1  DFSC 0x07 L3 transl.  → stack: guard page 쓰기</text>
  <text x="20" y="366" font-size="12">0xf2000800  EC 0x3c BRK  imm 0x800 = BUG_BRK_IMM  → bugon</text>
  <text x="20" y="400" font-size="12">0x96 = EC 0x25 + IL 1 → "커널이 데이터 접근하다 죽음"의 지문. 0x92로 시작하면 유저(EL0) 쪽 data abort</text>
</svg>
```

그림 3 — ESR 비트 필드와 이번 실험의 실제 값. 상위 바이트 `0x96`만 봐도 "커널 data abort"임을 안다. 그다음 WnR(읽기/쓰기)과 DFSC(무슨 종류의 fault)를 본다.

### 3.1 예제 — ESR 해독기 (`esr.py`)

커널이 이미 풀어서 찍어 주지만, 면접이나 다른 아키텍처 로그(예: 어떤 BSP는 ESR만 숫자로 찍는다)에서 직접 풀 수 있어야 한다. 이번 실험에 나온 EC·FSC만 담은 작은 해독기다.

```python
#!/usr/bin/env python3
"""esr.py — decode arm64 ESR_ELx values seen in oopses (subset of Arm ARM D24)."""
import sys
EC = {0x15: "SVC64", 0x18: "MSR/MRS trap", 0x20: "IABT (lower EL)",
      0x21: "IABT (current EL)", 0x22: "PC alignment", 0x24: "DABT (lower EL)",
      0x25: "DABT (current EL)", 0x26: "SP alignment", 0x2f: "SError",
      0x3c: "BRK (AArch64)", 0x00: "Unknown/UNDEF"}
FSC = {0x04: "L0 translation", 0x05: "L1 translation", 0x06: "L2 translation",
       0x07: "L3 translation", 0x09: "L1 access flag", 0x0b: "L3 access flag",
       0x0d: "L1 permission", 0x0f: "L3 permission", 0x10: "sync external abort",
       0x21: "alignment", 0x30: "TLB conflict"}
BRK = {0x800: "BUG()/WARN() (BUG_BRK_IMM)", 0x900: "KASAN", 0x400: "KGDB",
       0x004: "kprobes", 0x100: "FAULT_BRK"}

def decode(esr):
    ec, il, iss = (esr >> 26) & 0x3f, (esr >> 25) & 1, esr & 0x1ffffff
    out = [f"ESR=0x{esr:016x}  EC=0x{ec:02x} {EC.get(ec, '?')}  IL={32 if il else 16}"]
    if ec in (0x24, 0x25, 0x20, 0x21):
        f = lambda b, w=1: (iss >> b) & ((1 << w) - 1)
        out.append(f"  ISV={f(24)} SET={f(11,2)} FnV={f(10)} EA={f(9)} CM={f(8)}"
                   f" S1PTW={f(7)} WnR={f(6)}  FSC=0x{f(0,6):02x} {FSC.get(f(0,6), '?')}")
        out.append("  -> " + ("WRITE" if f(6) else "READ") + " access"
                   + ("" if f(10) == 0 else ", FAR NOT valid"))
    elif ec == 0x3c:
        out.append(f"  BRK imm=0x{iss & 0xffff:03x}: {BRK.get(iss & 0xffff, '?')}")
    return "\n".join(out)

for a in sys.argv[1:]:
    print(decode(int(a, 16)))
```

```sh
python3 esr.py 96000006 96000004 96000044 96000021 96000047 f2000800
```

```text
ESR=0x0000000096000006  EC=0x25 DABT (current EL)  IL=32
  ISV=0 SET=0 FnV=0 EA=0 CM=0 S1PTW=0 WnR=0  FSC=0x06 L2 translation
  -> READ access
ESR=0x0000000096000004  EC=0x25 DABT (current EL)  IL=32
  ISV=0 SET=0 FnV=0 EA=0 CM=0 S1PTW=0 WnR=0  FSC=0x04 L0 translation
  -> READ access
ESR=0x0000000096000044  EC=0x25 DABT (current EL)  IL=32
  ISV=0 SET=0 FnV=0 EA=0 CM=0 S1PTW=0 WnR=1  FSC=0x04 L0 translation
  -> WRITE access
ESR=0x0000000096000021  EC=0x25 DABT (current EL)  IL=32
  ISV=0 SET=0 FnV=0 EA=0 CM=0 S1PTW=0 WnR=0  FSC=0x21 alignment
  -> READ access
ESR=0x0000000096000047  EC=0x25 DABT (current EL)  IL=32
  ISV=0 SET=0 FnV=0 EA=0 CM=0 S1PTW=0 WnR=1  FSC=0x07 L3 translation
  -> WRITE access
ESR=0x00000000f2000800  EC=0x3c BRK (AArch64)  IL=32
  BRK imm=0x800: BUG()/WARN() (BUG_BRK_IMM)
```

출력에서 볼 것: translation fault의 **레벨**이 주소 종류를 알려 준다. 0x10은 유저 영역이라 PGD·PUD는 있고 L2(PMD)에서 비었다(`pmd=0`). `6b6b…`·`4141…`·`dead…` 같은 쓰레기 주소는 아예 **유효 주소 범위 밖**(non-canonical)이라 L0에서 바로 실패한다 — 커널이 `address between user and kernel address ranges`라고 덧붙인다. 스택 guard page는 매핑 구조는 다 있고 마지막 L3 PTE만 비어 있어서 L3 fault다. 그리고 `misalign`의 `ldxr`은 **로드**라서 WnR=0이다 — atomic 증가지만 첫 명령은 읽기(load-exclusive)다.

### 3.2 Cortex-M HardFault ↔ arm64 Oops 대응표

| Cortex-M (ARMv7-M/v8-M) | arm64 Oops | 메모 |
|---|---|---|
| CFSR (MMFSR·BFSR·UFSR) | ESR_EL1 — `Mem abort info:` 블록 | 둘 다 "원인 레지스터". arm64는 EC로 먼저 크게 나눈다 |
| MMFAR / BFAR | FAR_EL1 — `at virtual address ...` | 결함 주소 |
| MMARVALID / BFARVALID = 1 | FnV = 0 | 극성 반대. FnV=1이면 주소를 믿지 마라 |
| BFSR.PRECISERR | EA=1, 동기 external abort (DFSC 0x10) | 버스가 에러 응답 — 전원 꺼진 블록·클록 없는 IP의 레지스터 접근 |
| BFSR.IMPRECISERR | SError (EC 0x2f), `SError Interrupt on CPU` | 비동기 — pc가 원인 명령이 아니다 |
| MMFSR.DACCVIOL (MPU 위반) | permission fault (DFSC 0x0d–0x0f) | 읽기 전용 페이지 쓰기, PAN 위반 등 |
| UFSR.UNALIGNED | alignment fault (DFSC 0x21) | arm64는 일반 load/store는 정렬 안 돼도 되지만 exclusive/atomic·Device 메모리는 안 된다 (§9.5) |
| UFSR.DIVBYZERO (`CCR.DIV_0_TRP`=1일 때) | **없음** — `sdiv`/`udiv`는 0을 돌려준다 | §9.7 |
| UFSR.UNDEFINSTR | EC 0x00 `Internal error: Oops - Undefined instruction` | |
| BKPT 명령 | BRK 명령 (EC 0x3c) — `BUG()`가 쓰는 방법 | imm 0x800 = BUG |
| MSTKERR / STKERR, MSPLIM | VMAP_STACK guard page + `Insufficient stack space` | §9.6, E4 §4 |
| stacked PC | `pc :` (ELR_EL1) | |
| stacked LR | `lr :` (x30) | |
| stacked xPSR | `pstate:` (SPSR_EL1) | |
| EXC_RETURN 값으로 MSP/PSP 판단 | `CPU: … PID: … Comm:` + 마지막 `el0t_64_sync` 프레임 | 어느 문맥에서 왔나 |
| `.map` 파일 + `addr2line` | `System.map` + `faddr2line`/`decode_stacktrace.sh` | KASLR 보정이 추가 (§7) |
| HardFault = 시스템 정지 | Oops = 태스크만 죽음, panic = 시스템 정지 | 기본값이 다르다 (§8) |

---
## 4. Call trace — 콜스택은 어떻게 만들어지고, 어디서 거짓말하나

### 4.1 직관 — frame record 체인

arm64 커널은 `CONFIG_FRAME_POINTER=y`로 빌드된다(이 랩 커널도 그렇다). 함수가 다른 함수를 부르려면 자기 복귀 주소(x30)를 어딘가 보관해야 하므로, 프롤로그에서 **x29(frame pointer)와 x30(LR) 한 쌍**을 스택에 넣고 x29가 그 쌍을 가리키게 한다. 이 16바이트 쌍을 **frame record**라고 한다.

```text
stp  x29, x30, [sp, #-32]!     // [sp] = 이전 x29, [sp+8] = 내 복귀 주소
mov  x29, sp                   // x29 → 방금 만든 frame record
```

unwinder(`arch/arm64/kernel/stacktrace.c`)는 Oops 순간의 `pc`를 첫 줄로 찍고(`(P)` 표시), 그다음부터 x29가 가리키는 frame record에서 "복귀 주소"를 읽어 한 줄 찍고, "이전 x29"를 따라 다음 record로 간다. Don이 Cortex-M에서 스택 메모리를 덤프해 손으로 LR 비슷한 값을 찾던 일을, 커널은 x29 체인 덕분에 정확하게 자동으로 한다.

맨 위 `(P)` 말고 `(C)`가 붙는 줄도 있다. `show_stack+0x30/0x90 (C)`처럼 **현재 실행 중인 함수에서 직접 unwind를 시작했다**는 표시다 — panic이나 `dump_stack()`이 자기 자리에서 콜스택을 찍을 때 나온다.

### 4.2 맹점 1 — leaf 함수의 호출자가 사라진다

`null` Oops의 Call trace를 다시 보자.

```text
 pc : m3_get_irq.constprop.0+0xc/0xa10 [m3_bugs]
 lr : m3_trigger+0x1fc/0x210 [m3_bugs]
 sp : ffff8000804fbc50
 x29: ffff8000804fbc50 ...
 Call trace:
  m3_get_irq.constprop.0+0xc/0xa10 [m3_bugs] (P)
  param_attr_store+0x80/0x110
  module_attr_store+0x2c/0x50
```

`m3_trigger`가 **Call trace에 없다**. `lr`에만 있다. 이유는 `m3_get_irq`가 leaf 함수(다른 함수를 안 부름)라서 frame record를 만들지 않았기 때문이다. 실제 기계어가 이를 보여 준다(§6.4의 `objdump` 출력에서 발췌):

```text
0000000000000008 <m3_get_irq.constprop.0>:
   8:	d503201f 	nop
   c:	d503201f 	nop
  10:	d2800000 	mov	x0, #0x0                   	// #0
  14:	b9401000 	ldr	w0, [x0, #16]
  18:	d65f03c0 	ret
```

`stp x29, x30`이 없다. 그래서 Oops 순간 x29는 여전히 **호출자 `m3_trigger`의 frame record**를 가리키고 있었다(`x29 == sp == ffff8000804fbc50`인 것도 그 증거다 — leaf가 sp를 안 건드렸다). unwinder는 첫 줄을 `pc`로 찍은 뒤 x29의 record를 읽는데, 그 record에 든 것은 **`m3_trigger`의 복귀 주소, 즉 `param_attr_store+0x80`**이다. `m3_trigger` 자신의 위치(`+0x1fc`)는 x30 레지스터에만 있었고, unwinder는 x30을 콜스택 줄로 쓰지 않는다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13">CPU 레지스터 (Oops 순간)</text>
  <rect x="20" y="34" width="250" height="34" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="56" font-size="12">pc = m3_get_irq+0xc (leaf)</text>
  <rect x="20" y="74" width="250" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="96" font-size="12">x30 (lr) = m3_trigger+0x1fc</text>
  <rect x="20" y="114" width="250" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="136" font-size="12">x29 = ffff8000804fbc50</text>
  <text x="330" y="22" font-size="13">커널 스택 (높은 주소가 아래)</text>
  <rect x="330" y="34" width="330" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="56" font-size="12">m3_trigger의 frame record @ ...bc50</text>
  <text x="340" y="80" font-size="12">[+0] 이전 x29   [+8] param_attr_store+0x80</text>
  <rect x="330" y="104" width="330" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="126" font-size="12">param_attr_store의 frame record</text>
  <text x="340" y="150" font-size="12">[+0] 이전 x29   [+8] module_attr_store+0x2c</text>
  <rect x="330" y="174" width="330" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="199" font-size="12">... sysfs_kf_write ... el0t_64_sync</text>
  <line x1="270" y1="131" x2="328" y2="64" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="495" y1="94" x2="495" y2="102" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="495" y1="164" x2="495" y2="172" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="20" y="252" font-size="13">unwinder가 찍는 Call trace</text>
  <rect x="20" y="262" width="300" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="282" font-size="12">1. m3_get_irq+0xc (P) ← pc에서</text>
  <rect x="20" y="296" width="300" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="316" font-size="12">2. param_attr_store+0x80 ← x29 record</text>
  <rect x="20" y="330" width="300" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="350" font-size="12">3. module_attr_store+0x2c ← 다음 record</text>
  <rect x="350" y="262" width="310" height="98" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="360" y="286" font-size="12">빠진 줄: m3_trigger+0x1fc</text>
  <text x="360" y="310" font-size="12">leaf가 x30을 스택에 안 저장했으므로</text>
  <text x="360" y="334" font-size="12">이 값은 lr 줄에만 있다</text>
  <text x="360" y="354" font-size="12">→ 항상 pc · lr · Call trace를 같이 읽는다</text>
  <text x="20" y="390" font-size="12">비교: uaf의 m3_uaf는 kfree를 불러 frame을 만든다 → m3_trigger가 Call trace에 나온다</text>
</svg>
```

그림 4 — frame record 체인과 leaf 함수 맹점. 첫 줄은 pc, 둘째 줄부터는 x29 체인에서 온다. leaf 함수에서 죽으면 바로 위 호출자는 `lr`에만 남는다.

`bugon`도 같다. `m3_check.constprop.0.isra.0`이 leaf라서 Call trace는 `m3_check...` 다음 바로 `param_attr_store`로 넘어가고, `m3_trigger+0x8c`는 `lr`에만 있다. 반대로 `uaf`의 `m3_uaf`는 `kmalloc`·`kfree`를 부르는 non-leaf라서 Call trace에 `m3_trigger+0x174/0x210`가 제대로 나온다.

### 4.3 맹점 2 — lr은 "마지막 bl의 복귀 주소"일 뿐이다

`uaf` Oops의 머리 부분이다.

```text
 pc : m3_uaf+0x48/0x80 [m3_bugs]
 lr : m3_uaf+0x44/0x80 [m3_bugs]
```

`lr`이 같은 함수 안, pc 바로 앞을 가리킨다. "자기가 자기를 불렀나?"가 아니다. `objdump`로 보면(§6.4) `m3_uaf+0x40`(파일 오프셋 0x168)이 `bl kfree`이고, 0x44가 그 복귀 지점이다. 즉 **lr = "이 함수가 마지막으로 부른 함수(kfree)에서 돌아온 자리"**다. 이것만으로 "죽기 직전에 `kfree`를 불렀다"는 강력한 단서가 된다 — use-after-free다.

`warnf`에서는 `pc`와 `lr`이 똑같이 `m3_trigger+0xb8`였다. `WARN(1, fmt, ...)`이 메시지를 먼저 `__warn_printk()`로 찍고(`bl`), 그 복귀 지점에 바로 `brk` 명령이 있기 때문이다. lr은 호출자 정보가 아니라 **x30 레지스터의 현재 값**이라는 것을 기억한다.

### 4.4 맹점 3 — 인라인과 `.constprop`/`.isra`

- `m3_get_irq.constprop.0`: GCC가 "이 함수는 항상 같은 상수 인자로 불린다"는 걸 알고 그 상수를 박은 복제본을 만들었다. 원래 함수 이름 뒤에 `.constprop.N`이 붙는다.
- `m3_check.constprop.0.isra.0`: 거기에 더해 IPA-SRA(인자를 구조체 포인터 대신 필요한 필드 값만 넘기도록 바꿈)까지 했다.
- 조회 함수 `m3_lookup`은 아예 **인라인돼서 사라졌다**. 기계어에 `mov x0, #0x0`(조회 결과 NULL을 상수로 박음)만 남았다. Call trace에는 인라인된 함수가 나오지 않는다 — `faddr2line`의 `(inlined by)` 체인으로만 보인다(§6.2).
- `bugon`의 `m3_check`는 컴파일러가 "magic은 항상 틀린 값"임을 증명해서 비교를 없애고 **무조건 `brk #0x800`** 한 줄로 만들어 버렸다(§6.4 objdump). 그래서 `pc`가 함수 시작 +0x8이다.

이 접미사들은 "다른 함수"가 아니라 같은 소스 함수의 최적화 버전이다. `faddr2line`·`gdb`에는 접미사까지 포함한 이름 그대로 넣는다.

---

## 5. `Code:` 줄 — 죽은 명령어를 직접 보기

`Code:`에는 pc 앞 4개 명령과 pc의 명령, 총 5워드(리틀엔디언 32비트 값)가 찍히고 **괄호 안이 예외를 낸 명령**이다. 커널 트리의 `scripts/decodecode`가 이를 역어셈블한다(크로스 환경이면 `ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-`, 이 VM은 네이티브 arm64라 `ARCH`만).

### 5.1 예제 — null·dbldel·misalign·list·bugon의 Code: 해독

```sh
S=/var/lab/src/linux-6.18.55/scripts
grep "Code:" oops/null.txt | ARCH=arm64 $S/decodecode
for t in dbldel misalign list bugon; do
  grep "Code:" oops/$t.txt | ARCH=arm64 $S/decodecode | sed -n "/All code/,/trapping/p"
done
```

```text
[ 59.096093][ T80] Code: ffffcd3f aa1e03e9 d503201f d2800000 (b9401000)
All code
========
   0:	ffffcd3f 	.inst	0xffffcd3f ; undefined
   4:	aa1e03e9 	mov	x9, x30
   8:	d503201f 	nop
   c:	d2800000 	mov	x0, #0x0                   	// #0
  10:*	b9401000 	ldr	w0, [x0, #16]		<-- trapping instruction

Code starting with the faulting instruction
===========================================
   0:	b9401000 	ldr	w0, [x0, #16]
All code
========
   0:	d2802001 	mov	x1, #0x100                 	// #256
   4:	f2fbd5a1 	movk	x1, #0xdead, lsl #48
   8:	f9000443 	str	x3, [x2, #8]
   c:	91008822 	add	x2, x1, #0x22
  10:*	f9000422 	str	x2, [x1, #8]		<-- trapping instruction
All code
========
   0:	d0000001 	adrp	x1, 0x2000
   4:	91022021 	add	x1, x1, #0x88
   8:	91000421 	add	x1, x1, #0x1
   c:	f9800031 	prfm	pstl1strm, [x1]
  10:*	885f7c20 	ldxr	w0, [x1]		<-- trapping instruction
All code
========
   0:	91000400 	add	x0, x0, #0x1
   4:	eb0600bf 	cmp	x5, x6
   8:	540005a0 	b.eq	0xbc  // b.none
   c:	aa0503e4 	mov	x4, x5
  10:*	38646845 	ldrb	w5, [x2, x4]		<-- trapping instruction
All code
========
   0:	541d0b18 	bc.hi	0x3a160  // bc.pmore
   4:	ffffc8db 	.inst	0xffffc8db ; undefined
   8:	aa1e03e9 	mov	x9, x30
   c:	d503201f 	nop
  10:*	d4210000 	brk	#0x800		<-- trapping instruction
```

출력에서 볼 것:

- **null**: `mov x0, #0` 다음 `ldr w0, [x0, #16]` — 0 + 16에서 32비트를 읽는다. 소스가 없어도 "NULL 포인터의 오프셋 0x10, 4바이트 멤버"까지 읽힌다. 이 오프셋을 구조체 멤버 이름으로 바꾸는 것은 §6.5의 `pahole`이다.
- **dbldel**: `x1 = 0xdead000000000100`을 만들고 `[x1 + 8]`에 **쓴다**(`str`). `0xdead000000000100`은 `LIST_POISON1`, x2 = x1 + 0x22 = `LIST_POISON2`(`dead000000000122`)다. `list_del()`이 끝난 노드의 `next`/`prev`에 박아 두는 독 값이다 — "이미 지운 노드를 또 지웠다"(§9.3).
- **misalign**: 주소에 `+1`을 더한 뒤 `ldxr`(load-exclusive) — exclusive 접근은 자연 정렬이 필수다(§9.5).
- **list**: `ldrb w5, [x2, x4]` — 바이트 단위 문자열 읽기. `printk`의 `%s` 처리기다(§9.4).
- **bugon**: `brk #0x800` — `BUG()`는 일부러 넣은 브레이크포인트 명령이다. ESR의 `f2000800`과 같은 숫자다.

null과 bugon에 보이는 `.inst 0xffffc8db`, `mov x9, x30`, `nop`은 **ftrace의 흔적**이다. 커널은 모든 함수 앞에 `nop` 두 개(patchable function entry)를 넣어 두고, 부팅 때 첫 nop을 `mov x9, x30`으로 바꿔 ftrace가 언제든 훅을 걸 수 있게 한다. 함수 바로 앞 8바이트에는 ftrace ops 포인터(커널 주소의 상위 32비트 `ffffcd3f`가 보인다)가 들어간다. 그래서 `Code:` 바이트는 **디스크의 `.ko`와 다를 수 있다** — 실행 중 패치된 내용이 찍히기 때문이다. `.ko`의 objdump에는 같은 자리가 `nop nop`이다(M5 ftrace).

---

## 6. 해독 도구 상자 — 어떤 도구를 언제 쓰고, 언제 틀리나

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13">입력</text>
  <rect x="20" y="34" width="170" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="58" text-anchor="middle" font-size="12">Oops 텍스트</text>
  <rect x="20" y="84" width="170" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="108" text-anchor="middle" font-size="12">vmlinux (같은 빌드)</text>
  <rect x="20" y="134" width="170" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="158" text-anchor="middle" font-size="12">모듈 .ko (디버그 정보)</text>
  <rect x="20" y="184" width="170" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="208" text-anchor="middle" font-size="12">소스 트리 · System.map</text>
  <text x="250" y="22" font-size="13">도구</text>
  <rect x="250" y="34" width="190" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="56" text-anchor="middle" font-size="12">decode_stacktrace.sh</text>
  <rect x="250" y="74" width="190" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="96" text-anchor="middle" font-size="12">faddr2line</text>
  <rect x="250" y="114" width="190" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="136" text-anchor="middle" font-size="12">addr2line -j 섹션</text>
  <rect x="250" y="154" width="190" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="176" text-anchor="middle" font-size="12">gdb list / info line</text>
  <rect x="250" y="194" width="190" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="216" text-anchor="middle" font-size="12">decodecode · objdump -dr</text>
  <rect x="250" y="234" width="190" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="256" text-anchor="middle" font-size="12">pahole -C 구조체</text>
  <text x="500" y="22" font-size="13">출력</text>
  <rect x="500" y="34" width="160" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="56" text-anchor="middle" font-size="12">로그 전체에 파일:줄</text>
  <rect x="500" y="74" width="160" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="96" text-anchor="middle" font-size="12">줄 + inlined 체인</text>
  <rect x="500" y="114" width="160" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="136" text-anchor="middle" font-size="12">섹션 오프셋 → 줄</text>
  <rect x="500" y="154" width="160" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="176" text-anchor="middle" font-size="12">줄 + 주변 소스</text>
  <rect x="500" y="194" width="160" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="216" text-anchor="middle" font-size="12">죽은 명령어</text>
  <rect x="500" y="234" width="160" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="256" text-anchor="middle" font-size="12">오프셋 → 멤버 이름</text>
  <line x1="190" y1="54" x2="248" y2="51" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="190" y1="104" x2="248" y2="91" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="190" y1="154" x2="248" y2="131" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="190" y1="154" x2="248" y2="251" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="190" y1="204" x2="248" y2="171" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="190" y1="54" x2="248" y2="211" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="440" y1="51" x2="498" y2="51" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="440" y1="91" x2="498" y2="91" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="440" y1="131" x2="498" y2="131" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="440" y1="171" x2="498" y2="171" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="440" y1="211" x2="498" y2="211" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="440" y1="251" x2="498" y2="251" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="20" y="296" width="640" height="94" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="320" font-size="12">전제: vmlinux·.ko가 Oops를 낸 바로 그 빌드 (build-id 일치). 다르면 줄이 조용히 틀린다</text>
  <text x="30" y="344" font-size="12">함정: 함수가 .text 밖(.text.unlikely 등)에 있으면 decode_stacktrace.sh가 틀린 줄</text>
  <text x="30" y="368" font-size="12">교차 확인: 같은 주소를 도구 두 개로 풀어서 답이 같을 때만 믿는다 (§6.3)</text>
</svg>
```

그림 5 — 해독 파이프라인. 입력 네 가지(로그·vmlinux·.ko·소스)가 다 있어야 하고, 도구마다 받는 주소 형식이 다르다.

| 도구 | 입력 | 장점 | 실패하는 경우 |
|---|---|---|---|
| `scripts/decode_stacktrace.sh vmlinux 소스 모듈디렉터리 < oops.txt` | Oops 로그 전체 | 한 번에 모든 줄을 파일:줄로, `Code:`도 자동 해독 | 함수가 `.text` 밖 섹션에 있으면 **틀린 줄**(§6.3). 큰 vmlinux에서 느림(이번 실측 2분 24초, 공유 VM 부하 상태) |
| `scripts/faddr2line 파일 심볼+off/size` | `pc`·Call trace 한 줄 | 섹션을 제대로 찾음, `(inlined by)` 체인 | `/size`가 실제 함수 크기와 다르면 거부(§6.2) — 그땐 `/size`를 떼고 다시 |
| `addr2line -fi -e 파일 [-j 섹션] 주소` | 섹션 기준 오프셋 또는 nokaslr 절대 주소 | 가장 기본, 어디서나 있음 | 섹션을 잘못 주면 다른 함수가 나옴 |
| `gdb 파일 -ex 'list *(심볼+off)'` | 심볼+오프셋 | 주변 소스까지 보여 줌, `info line` | 모듈을 커널 주소로 보려면 `add-symbol-file`에 섹션 주소를 다 줘야 함 |
| `scripts/decodecode` | `Code:` 줄 | 소스·심볼 없이도 동작 | ftrace 패치 등으로 바이트가 디스크와 다를 수 있음 |
| `objdump -dr`(+`-S`, `-l`) | `.ko`/vmlinux | 함수 전체 기계어, 재배치 정보 | `.ko`는 재배치 전이라 `bl` 대상이 엉뚱하게 표시됨(§6.4) — 반드시 `-r` |
| `pahole -C 구조체 파일` | 구조체 이름 | 오프셋 → 멤버 이름 | BTF/DWARF 없는 빌드에선 불가 |

### 6.1 예제 — `decode_stacktrace.sh`로 로그 전체 풀기 (그리고 틀린 답)

```sh
cd /var/tmp/M3
S=/var/lab/src/linux-6.18.55/scripts; V=/var/lab/out/vmlinux-trace
time $S/decode_stacktrace.sh $V /var/lab/src/linux-6.18.55 /var/tmp/M3/m3_bugs/trace \
     < oops/null.txt 2>&1 | sed -n "/pc :/,\$p" | grep -v " x[0-9]"
```

```text
[   59.066671][   T80] pc : m3_get_irq.constprop.0 (/var/tmp/M3/m3_bugs/m3_bugs.c:20) m3_bugs
[   59.071073][   T80] lr : m3_trigger (/var/tmp/M3/m3_bugs/m3_bugs.c:90 (discriminator 1)) m3_bugs
[   59.071322][   T80] sp : ffff8000804fbc50
[   59.085301][   T80] Call trace:
[   59.086040][   T80] m3_get_irq.constprop.0 (/var/tmp/M3/m3_bugs/m3_bugs.c:20) m3_bugs (P)
[   59.088443][   T80]  param_attr_store (kernel/params.c:591)
[   59.088637][   T80]  module_attr_store (kernel/params.c:909)
[   59.088919][   T80]  sysfs_kf_write (fs/sysfs/file.c:147)
[   59.089174][   T80]  kernfs_fop_write_iter (fs/kernfs/file.c:352)
[   59.089381][   T80]  vfs_write (fs/read_write.c:593 fs/read_write.c:686)
[   59.089562][   T80]  ksys_write (fs/read_write.c:738)
[   59.089728][   T80]  __arm64_sys_write (fs/read_write.c:746)
...  (invoke_syscall ~ el0t_64_sync 생략)
[   59.096093][   T80] Code: ffffcd3f aa1e03e9 d503201f d2800000 (b9401000)
All code
========
   0:	ffffcd3f 	.inst	0xffffcd3f ; undefined
   4:	aa1e03e9 	mov	x9, x30
   8:	d503201f 	nop
   c:	d2800000 	mov	x0, #0x0                   	// #0
  10:*	b9401000 	ldr	w0, [x0, #16]		<-- trapping instruction
...
real	2m24.213s
```

출력에서 볼 것: 커널 쪽 줄은 전부 맞다(`vfs_write (fs/read_write.c:593 fs/read_write.c:686)`처럼 인라인 체인도 공백으로 이어 붙여 준다). 그런데 **pc 줄의 `m3_bugs.c:20`은 틀렸다**. 20번 줄은 `m3_get_irq`가 아니라 다른 작은 함수 `m3_start`의 줄이다. 함수 이름은 맞게 찍고 줄 번호만 틀리니 알아채기 어렵다.

### 6.2 예제 — `faddr2line`: 크기 불일치 경고와 인라인 체인

```sh
K=m3_bugs/trace/m3_bugs.ko
$S/faddr2line $K m3_get_irq.constprop.0+0xc/0xa10 m3_get_irq.constprop.0+0xc \
  m3_uaf+0x48/0x80 m3_double_del+0x54/0x80 m3_misalign+0x38/0x60 \
  m3_list_bug+0xcc/0xe8 m3_trigger+0x1fc/0x210
```

```text
skipping m3_get_irq.constprop.0 address at 0x14 due to size mismatch (0xa10 != 0x14)
no match for m3_get_irq.constprop.0+0xc/0xa10
m3_get_irq.constprop.0+0xc/0x14:
m3_get_irq at /var/tmp/M3/m3_bugs/m3_bugs.c:34 (discriminator 1)

m3_uaf+0x48/0x80:
m3_uaf at /var/tmp/M3/m3_bugs/m3_bugs.c:43

m3_double_del+0x54/0x80:
__list_del at /var/lab/src/linux-6.18.55/include/linux/list.h:210
(inlined by) __list_del_entry at /var/lab/src/linux-6.18.55/include/linux/list.h:233
(inlined by) list_del at /var/lab/src/linux-6.18.55/include/linux/list.h:244
(inlined by) m3_double_del at /var/tmp/M3/m3_bugs/m3_bugs.c:77

m3_misalign+0x38/0x60:
__ll_sc_atomic_add_return at /var/lab/src/linux-6.18.55/arch/arm64/include/asm/atomic_ll_sc.h:95 (discriminator 2)
(inlined by) arch_atomic_add_return at /var/lab/src/linux-6.18.55/arch/arm64/include/asm/atomic.h:52 (discriminator 2)
(inlined by) raw_atomic_add_return at /var/lab/src/linux-6.18.55/include/linux/atomic/atomic-arch-fallback.h:564 (discriminator 2)
(inlined by) raw_atomic_inc_return at /var/lab/src/linux-6.18.55/include/linux/atomic/atomic-arch-fallback.h:1020 (discriminator 2)
(inlined by) atomic_inc_return at /var/lab/src/linux-6.18.55/include/linux/atomic/atomic-instrumented.h:454 (discriminator 2)
(inlined by) m3_misalign at /var/tmp/M3/m3_bugs/m3_bugs.c:83 (discriminator 2)

m3_list_bug+0xcc/0xe8:
m3_list_bug at /var/tmp/M3/m3_bugs/m3_bugs.c:67 (discriminator 4)

m3_trigger+0x1fc/0x210:
m3_trigger at /var/tmp/M3/m3_bugs/m3_bugs.c:90 (discriminator 1)
```

출력에서 볼 것:

- **크기 불일치**: Oops는 `m3_get_irq.constprop.0`의 크기를 `0xa10`이라고 찍었지만 ELF 심볼표의 실제 크기는 `0x14`(20바이트, 명령 5개)다. 커널의 kallsyms는 모듈 심볼의 크기를 따로 저장하지 않고 "다음 심볼까지의 거리"로 계산하는데, 이 함수는 모듈 텍스트 영역의 **마지막 심볼**이라 "다음 심볼"이 없고, 기본값인 텍스트 영역 끝까지가 크기로 잡혔다(`0x...55f0 + 0xa10 = 0x...6000`, 4 KiB 페이지 경계와 맞아떨어진다). `faddr2line`은 크기가 다르면 "다른 빌드일 수 있다"며 거부한다. `/size`를 떼면 바로 풀린다. 별도 모듈의 lockup 실험에서도 `m3_lock_init+0x30/0xfb0`처럼 같은 현상이 보였다.
- **정답은 34번 줄**(`return d->irq`에 해당하는 줄, `discriminator 1`은 같은 줄 안의 두 번째 기본 블록이라는 DWARF 표시). §6.1의 20번 줄은 틀렸다.
- **inlined by 체인**: `m3_double_del+0x54`의 실제 명령은 `include/linux/list.h:210`의 `__list_del()` 안에 있다. 모듈 77번 줄의 `list_del()` 호출이 세 단계 인라인으로 펼쳐진 것이다. Call trace에는 `m3_double_del` 하나만 보이지만 faddr2line은 "커널 헤더의 이 줄에서, 내 코드의 이 줄을 거쳐" 죽었다고 알려 준다. `misalign`은 여섯 단계 — atomic API가 LL/SC 구현까지 인라인된 끝에 `ldxr`이 있다.
- `m3_trigger+0x1fc`(lr) → 90번 줄: `m3_get_irq`를 부른 줄이다. §4.2에서 Call trace에 빠졌던 호출자를 여기서 찾는다.

### 6.3 왜 `decode_stacktrace.sh`가 틀렸나 — `.text.unlikely`

```sh
readelf -sW m3_bugs.ko | grep -E "m3_(get_irq|uaf|trigger|start|check|double|misalign|list)"
readelf -SW m3_bugs.ko | grep -E " \.text( |\.unlikely)"
```

```text
    40: 0000000000000008    16 FUNC    LOCAL  DEFAULT   12 m3_start
    44: 00000000000000c8    80 FUNC    LOCAL  DEFAULT   12 m3_misalign
    52: 0000000000000128   108 FUNC    LOCAL  DEFAULT   12 m3_uaf
    55: 0000000000000008    20 FUNC    LOCAL  DEFAULT   21 m3_get_irq.constprop.0
    57: 00000000000001a8    12 FUNC    LOCAL  DEFAULT   12 m3_check.constprop.0.isra.0
    60: 00000000000001c8   108 FUNC    LOCAL  DEFAULT   12 m3_double_del
    62: 0000000000000248   224 FUNC    LOCAL  DEFAULT   12 m3_list_bug
    64: 0000000000000330   528 FUNC    LOCAL  DEFAULT   12 m3_trigger
    71: 0000000000000000    16 OBJECT  LOCAL  DEFAULT   14 m3_list
  [12] .text             PROGBITS        0000000000000000 0000d0 000540 00  AX  0   0 32
  [21] .text.unlikely    PROGBITS        0000000000000000 0007c0 00001c 00  AX  0   0  8
```

출력에서 볼 것: `m3_get_irq.constprop.0`은 섹션 **21번 `.text.unlikely`**의 오프셋 8에 있고, `m3_start`는 섹션 **12번 `.text`**의 같은 오프셋 8에 있다. GCC는 "항상 NULL 역참조로 끝나는 함수 = 거의 실행되지 않을 경로"라고 판단해 `.text.unlikely`(cold 섹션)로 옮겼다. `decode_stacktrace.sh`는 모듈 심볼 값(8) + 오프셋(0xc) = 0x14를 계산한 뒤 **섹션을 지정하지 않고** addr2line에 넘겼고, addr2line은 `.text`의 0x14 = `m3_start` 안의 줄(20)을 돌려줬다. `addr2line`으로 두 섹션을 직접 비교하면 분명하다.

```sh
cd /var/tmp/M3/m3_bugs/trace
addr2line -fi -e m3_bugs.ko -j .text 0x14
addr2line -fi -e m3_bugs.ko -j .text.unlikely 0x14
```

```text
m3_start
/var/tmp/M3/m3_bugs/m3_bugs.c:20
m3_get_irq
/var/tmp/M3/m3_bugs/m3_bugs.c:34 (discriminator 1)
```

교훈: **모듈의 줄 번호는 섹션을 알아야 정확하다**. 이상한 줄이 나오면 `readelf -s`로 그 함수가 어느 섹션(`Ndx` 열)에 있는지부터 본다. 커널 쪽에서도 `__init` 함수(`.init.text`), cold 경로(`.text.unlikely`), `noinstr` 함수(`.noinstr.text`)가 같은 함정을 만든다. `faddr2line`은 심볼표에서 섹션까지 찾아 쓰기 때문에 맞았다.

### 6.4 예제 — `objdump -dr`과 gdb로 죽은 명령과 줄을 확인

`uaf`의 pc `m3_uaf+0x48`을 기계어로 확인한다. `m3_uaf`는 `.text` 0x128에서 시작하므로 pc는 0x128 + 0x48 = 0x170이다.

```sh
objdump -d --no-show-raw-insn m3_bugs.ko | sed -n "/<m3_uaf>:/,/^\$/p"
```

```text
0000000000000128 <m3_uaf>:
 128:	nop
 12c:	nop
 130:	paciasp
 134:	adrp	x0, 0 <kmalloc_caches>
 138:	stp	x29, x30, [sp, #-32]!
 13c:	mov	x2, #0x30                  	// #48
 140:	mov	x29, sp
 144:	mov	w1, #0xcc0                 	// #3264
 148:	ldr	x0, [x0]
 14c:	bl	0 <__kmalloc_cache_noprof>
 150:	cbz	x0, 18c <m3_uaf+0x64>
 154:	adrp	x1, 0 <m3_start-0x8>
 158:	add	x1, x1, #0x0
 15c:	str	x19, [sp, #16]
 160:	mov	x19, x0
 164:	str	x1, [x0, #24]
 168:	bl	0 <kfree>
 16c:	ldr	x0, [x19, #24]
 170:	ldr	x1, [x0]
 174:	mov	x0, x19
 178:	blr	x1
 17c:	ldr	x19, [sp, #16]
 180:	ldp	x29, x30, [sp], #32
 184:	autiasp
 188:	ret
...
```

출력에서 볼 것: 0x168 `bl kfree` → 0x16c `ldr x0, [x19, #24]`(해제된 객체의 오프셋 24 = 함수 포인터 테이블 포인터를 읽음, 이 읽기는 성공 — 메모리는 아직 매핑돼 있고 내용이 `6b`로 칠해져 있다) → **0x170 `ldr x1, [x0]`**(그 쓰레기 포인터 `6b6b…`를 따라감 → fault). `lr = m3_uaf+0x44` = 0x16c = kfree의 복귀 지점, `x0 = 6b6b6b6b6b6b6b6b`가 레지스터 덤프와 정확히 맞는다. `paciasp`/`autiasp`는 포인터 인증(PAC, `CONFIG_ARM64_PTR_AUTH_KERNEL=y`)으로 복귀 주소에 서명하는 명령이다. `kmalloc` 크기 `0x30`(48)과 GFP 값 `0xcc0`(`GFP_KERNEL`)도 기계어에 그대로 보인다.

`.ko`를 objdump할 때 **반드시 `-r`을 붙여야 하는 이유**는 `null`의 호출 지점에서 보인다.

```sh
objdump -dr --no-show-raw-insn m3_bugs.ko | sed -n "/<m3_trigger>:/,/^\$/p" | grep -A4 " 528:"
```

```text
 528:	bl	8 <m3_start>
			528: R_AARCH64_CALL26	m3_get_irq.constprop.0
 52c:	mov	w1, w0
 530:	adrp	x0, 0 <m3_start-0x8>
			530: R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.8+0x38
```

`.ko`는 링크 전 오브젝트라서 `bl`의 목적지가 아직 채워지지 않았고, objdump는 같은 섹션의 오프셋 8 = `m3_start`라고 **잘못 표시**한다. 재배치 항목(`R_AARCH64_CALL26 m3_get_irq.constprop.0`)이 진짜 목적지다. §6.3과 똑같은 섹션 혼동이다. 그리고 `m3_trigger` 시작 0x330 + 0x1fc = **0x52c** — Oops의 `lr`이 바로 이 `bl` 다음 명령이다.

`.text.unlikely`의 `m3_get_irq`를 줄 정보와 함께 보면 34번 줄이 다시 확인된다.

```sh
objdump -dl m3_bugs.ko -j .text.unlikely      # 앞부분(파일 머리, -0x8 슬롯) 생략
```

```text
0000000000000008 <m3_get_irq.constprop.0>:
m3_get_irq():
/var/tmp/M3/m3_bugs/m3_bugs.c:31
   8:	d503201f 	nop
   c:	d503201f 	nop
/var/tmp/M3/m3_bugs/m3_bugs.c:34
  10:	d2800000 	mov	x0, #0x0                   	// #0
/var/tmp/M3/m3_bugs/m3_bugs.c:34 (discriminator 1)
  14:	b9401000 	ldr	w0, [x0, #16]
/var/tmp/M3/m3_bugs/m3_bugs.c:35
  18:	d65f03c0 	ret
```

gdb는 `.ko`를 직접 열어 `list *(심볼+오프셋)`으로 줄과 주변 소스를 보여 준다(소스 10줄 출력은 이 노트에서 생략).

```sh
gdb -q -batch -ex "list *(m3_uaf+0x48)" -ex "info line *(m3_uaf+0x48)" m3_bugs.ko
```

```text
0x170 is in m3_uaf (m3_bugs.c:43).
...  (소스 38~47번 줄 생략)
Line 43 of "m3_bugs.c" starts at address 0x170 <m3_uaf+72> and ends at 0x180 <m3_uaf+88>.
```

vmlinux 쪽 주소도 같은 방식이다. Call trace의 `vfs_write+0x248`을 넣으면 인라인된 `new_sync_write`의 줄이 나온다.

```sh
gdb -q -batch -ex "list *(vfs_write+0x248)" /var/lab/out/vmlinux-trace | head -2
```

```text
0xffff8000804ad4c0 is in vfs_write (/var/lab/src/linux-6.18.55/fs/read_write.c:593).
588	
```

실행 중인 커널 주소 그대로 gdb에 넣고 싶을 때(예: JTAG·kgdb 세션, M4)는 모듈 섹션의 **런타임 주소**를 `add-symbol-file`에 다 알려 줘야 한다. 섹션 주소는 `/sys/module/<모듈>/sections/`에 있다(root 필요).

```sh
# 게스트에서 미리 읽어 둔 값: .text 0xffffd7be87ff50a0, .text.unlikely 0xffffd7be87ff55e8
gdb -q -batch -ex "add-symbol-file m3_bugs.ko 0xffffd7be87ff50a0 -s .text.unlikely 0xffffd7be87ff55e8" \
    -ex "info symbol 0xffffd7be87ff55fc" /var/lab/out/vmlinux-trace
```

```text
add symbol table from file "m3_bugs.ko" at
	.text_addr = 0xffffd7be87ff50a0
	.text.unlikely_addr = 0xffffd7be87ff55e8
m3_get_irq.constprop + 12 in section .text.unlikely of /var/tmp/M3/m3_bugs/trace/m3_bugs.ko
```

출력에서 볼 것: 런타임 pc `0xffffd7be87ff55fc`(= `.text.unlikely` 시작 + 0x14)를 넣으니 `m3_get_irq.constprop + 12`(0xc)가 나왔다 — Oops의 `+0xc`와 같다. `.text.unlikely` 주소를 빼먹고 `.text`만 주면 gdb도 §6.3과 똑같이 틀린다.

### 6.5 예제 — `pahole`: 오프셋을 멤버 이름으로

NULL + 0x10에서 4바이트를 읽었다(`ldr w0, [x0, #16]`). 무슨 멤버인가?

```sh
pahole -C m3_dev m3_bugs.ko; pahole -C m3_ent m3_bugs.ko
```

```text
struct m3_dev {
	char                       name[16];             /*     0    16 */
	u32                        irq;                  /*    16     4 */
	u32                        magic;                /*    20     4 */
	const struct m3_ops  *     ops;                  /*    24     8 */
	struct list_head           node;                 /*    32    16 */

	/* size: 48, cachelines: 1, members: 5 */
	/* last cacheline: 48 bytes */
};

struct m3_ent {
	char                       tag[8];               /*     0     8 */
	struct list_head           node;                 /*     8    16 */

	/* size: 24, cachelines: 1, members: 2 */
	/* last cacheline: 24 bytes */
};
```

출력에서 볼 것: 오프셋 16, 크기 4 = `irq`. `uaf`의 `[x0, #24]` = `ops`(함수 포인터 테이블), `size: 48` = objdump의 `mov x2, #0x30`. `m3_ent`의 `node`가 오프셋 8이라는 것은 §9.4에서 `0x...39`(= `0x...41` − 8)를 설명하는 열쇠다. 커널 구조체라면 `pahole -C task_struct vmlinux`처럼 vmlinux에 대고 같은 일을 한다(BTF가 있으면 디버그 정보 없이도 된다).

---

## 7. KASLR과 System.map — 주소를 빌드 결과물과 맞추기

### 7.1 직관

Cortex-M의 `.map` 파일 주소는 플래시에 구운 주소 그대로다. Linux arm64는 `CONFIG_RANDOMIZE_BASE=y`(KASLR)라서 부팅마다 커널 전체가 무작위 오프셋만큼 옮겨진다. `System.map`(빌드 때 `nm vmlinux`로 만든 심볼표)과 `vmlinux`의 주소는 **오프셋 0 기준**이다. Oops에 심볼 이름이 찍히면(`CONFIG_KALLSYMS=y`) 신경 쓸 필요가 없지만, 날 주소만 있는 경우(레지스터 값, 메모리 덤프, kallsyms 없는 빌드, JTAG로 읽은 PC)에는 보정이 필요하다.

### 7.2 예제 — 런타임 주소 → System.map 주소

게스트에서 root로 kallsyms를 읽고(`kptr_restrict`가 0이라 주소가 보인다 — M1·M2), 호스트의 System.map과 비교했다.

```sh
# 게스트 (runall의 ks 실행)
cat /proc/sys/kernel/kptr_restrict
grep -E " (_text|vfs_write)$" /proc/kallsyms
grep -E "m3_(get_irq|uaf|trigger)" /proc/kallsyms
for s in .text .text.unlikely .data .bss; do echo "$s $(cat /sys/module/m3_bugs/sections/$s)"; done
```

```text
0
ffffd7bea5a00000 T _text
ffffd7bea5ead278 T vfs_write
ffffd7be87ff51c8 t m3_uaf	[m3_bugs]
ffffd7be87ff55f0 t m3_get_irq.constprop.0	[m3_bugs]
ffffd7be87ff53d0 t m3_trigger	[m3_bugs]
.text 0xffffd7be87ff50a0
.text.unlikely 0xffffd7be87ff55e8
.data 0xffffd7be87ff7078
.bss 0xffffd7be87ff7088
```

```sh
# VM 호스트
grep -E " (vfs_write|_text|_stext|string)$" /var/lab/out/linux-build-trace/System.map
python3 -c "print(hex(0xffffd7bea5a00000-0xffff800080000000))"
python3 -c "print(hex(0xffffd7bea5ead278-0x57be25a00000))"
addr2line -fi -e /var/lab/out/vmlinux-trace 0xffff8000804ad4c0
```

```text
ffff800080000000 T _text
ffff800080010000 T _stext
ffff8000804ad278 T vfs_write
ffff8000816f3d80 t string
0x57be25a00000
0xffff8000804ad278
new_sync_write
/var/lab/src/linux-6.18.55/fs/read_write.c:593
vfs_write
/var/lab/src/linux-6.18.55/fs/read_write.c:686
```

출력에서 볼 것:

- **KASLR 오프셋 = 런타임 `_text` − System.map `_text`** = `0x57be25a00000`. 이걸 런타임 `vfs_write`에서 빼면 System.map 값 `ffff8000804ad278`과 정확히 같다.
- `0xffff8000804ad4c0` = `vfs_write`(…d278) + 0x248, 즉 Call trace의 `vfs_write+0x248`을 System.map 기준 절대 주소로 만든 것이다. addr2line은 인라인 체인(`new_sync_write` → `vfs_write`)까지 준다. 위 decode_stacktrace의 `vfs_write (fs/read_write.c:593 fs/read_write.c:686)`와 일치한다.
- 모듈은 커널 이미지와 **별도로** 무작위 배치된다(`2G module region forced by RANDOMIZE_MODULE_REGION_FULL`이 부팅 로그에 있다). 모듈 주소는 KASLR 오프셋으로 못 되돌리고, **섹션 시작 주소를 빼서 섹션 오프셋**으로 만든다: `0x...55f0 − 0x...55e8 = 0x8`(심볼표 값과 같다), pc는 `+0xc` → 섹션 오프셋 0x14.
- `m3_uaf`의 런타임 주소 `...51c8` − `.text` `...50a0` = `0x128` — 심볼표의 `m3_uaf` 값과 같다.

panic 메시지에는 커널이 이 오프셋을 직접 찍어 준다. `oops=panic` 실험의 끝부분이다.

```text
[   T79] Kernel panic - not syncing: Oops: Fatal exception
[   T79] SMP: stopping secondary CPUs
[   T79] Kernel Offset: 0x3354cd000000 from 0xffff800080000000
[   T79] PHYS_OFFSET: 0x40000000
[   T79] CPU features: 0x104000,04027800,40004000,0400421b
[   T79] Memory Limit: none
[   T79] ---[ end Kernel panic - not syncing: Oops: Fatal exception ]---
```

`Kernel Offset: X from 0xffff800080000000`은 "이 부팅에서 `_text` = 0xffff800080000000 + X"라는 뜻이다. 그 부팅의 런타임 `vfs_write` = `0xffff8000804ad278 + 0x3354cd000000` = `0xffffb3554d4ad278`(직접 계산한 값). 필드에서 받은 panic 로그에 날 주소가 있다면 이 줄로 System.map 기준 주소를 만든다.

### 7.3 `nokaslr` — 디버깅할 때는 끄는 것도 방법

`nokaslr` 부팅 인자로 같은 `null` 버그를 냈더니:

```text
[    0.000000][    T0] KASLR disabled on command line
...
m3_bugs 12288 0 - Live 0xffff80007bfa0000 (OE)
0xffff80007bfa00a0
...
[   T79] pc : m3_get_irq.constprop.0+0xc/0xa10 [m3_bugs]
[   T79] Code: ffff8000 aa1e03e9 d503201f d2800000 (b9401000) 
```

출력에서 볼 것: 모듈 주소가 `0xffff80007bfa0000`처럼 커널 기본 주소(`0xffff800080000000`) 바로 아래 고정 영역으로 왔다. 커널 이미지도 System.map 주소 그대로 돈다 — 그래서 JTAG·kgdb 세션(M4)이나 반복 재현 실험에서는 `nokaslr`이 편하다. `Code:` 첫 워드가 `ffff8000`으로 바뀐 것도 보인다(§5의 ftrace ops 포인터 상위 32비트가 주소에 따라 달라짐). 양산 이미지에서는 KASLR을 끄지 않는다. 보안 기능이기 때문이다.

### 7.4 `%p`·`kptr_restrict`와 주소 숨김

Oops의 레지스터 덤프(`x0`~`x29`, `sp`)는 날 주소를 그대로 찍는다(디버깅용 예외 — `pc`/`lr`은 `%pS`로 심볼+오프셋). 하지만 일반 `printk("%p")`는 해시값을, `/proc/kallsyms`는 `kptr_restrict`가 1 이상이고 CAP_SYSLOG가 없으면 0을 보여 준다(M1 §`%p` 해싱, M2 `/proc/modules`). 필드 로그 분석 때 "주소가 다 0이에요"라는 말을 들으면 이것이다. 그래서 **Oops를 심볼 이름과 함께 찍는 `CONFIG_KALLSYMS`**가 양산에서도 중요하다 — 끄면 이미지는 조금 작아지지만 필드 Oops가 날 주소뿐이 된다.

---
## 8. Oops · panic · BUG · WARN · lockup — 무엇이 다른가

### 8.1 한 장 비교

| 종류 | 첫 줄 | 일으키는 것 | 그 뒤 | taint |
|---|---|---|---|---|
| Oops (fault) | `Unable to handle kernel ...` → `Internal error: Oops: <ESR> [#N]` | MMU abort, alignment, undefined instruction | 현재 태스크 SIGSEGV로 죽음, 커널은 계속 | `D` |
| Oops (BUG) | `kernel BUG at file:line!` → `Internal error: Oops - BUG: <ESR>` | `BUG()`/`BUG_ON()` = `brk #0x800` | 위와 같다 | `D` |
| WARN | `WARNING: CPU: N PID: P at file:line func+off` | `WARN_ON()`/`WARN()` = 같은 `brk`, 다른 분기 | **다음 명령으로 진행**, 아무도 안 죽음 | `W` |
| 검사기 BUG | `BUG: scheduling while atomic`, `BUG: sleeping function called from invalid context` | 스케줄러·`might_sleep()` 검사 | 대개 계속 (콜스택만) | `W` |
| lockup | `watchdog: BUG: soft lockup - CPU#N stuck for Ns!` | watchdog 타이머 | 계속 (반복 보고) | `L` |
| panic | `Kernel panic - not syncing: <이유>` | `panic()` 직접 호출 또는 정책 | 모든 CPU 정지, `panic=N`이면 N초 뒤 리부트 | — |

panic이 "not syncing"이라고 말하는 것은 "디스크 버퍼를 sync하지 않고 멈춘다"는 오래된 표현이다. 이 랩 커널은 `CONFIG_PANIC_TIMEOUT=0`이라 panic 뒤 영원히 멈춰 있고, QEMU는 `timeout`에 의해 종료됐다(`terminating on signal 15 ... (timeout)`).

### 8.2 BUG_ON — 그리고 첫 Oops 뒤의 커널은 믿을 수 없다

```text
[   T79] ------------[ cut here ]------------
[   T79] kernel BUG at m3_bugs.c:47!
[   T79] Internal error: Oops - BUG: 00000000f2000800 [#1]  SMP
[   T79] Modules linked in: m3_bugs(OE)
[   T79] CPU: 0 UID: 0 PID: 79 Comm: sh Tainted: G           OE       6.18.55-trace #1 PREEMPT 
[   T79] pc : m3_check.constprop.0.isra.0+0x8/0x20 [m3_bugs]
[   T79] lr : m3_trigger+0x8c/0x210 [m3_bugs]
[   T79] Call trace:
[   T79]  m3_check.constprop.0.isra.0+0x8/0x20 [m3_bugs] (P)
[   T79]  param_attr_store+0x80/0x110
...
[   T79] Code: 541d0b18 ffffc8db aa1e03e9 d503201f (d4210000) 
[   T79] ---[ end trace 0000000000000000 ]---
[   T79] note: sh[79] exited with irqs disabled
[   T79] note: sh[79] exited with preempt_count 1
[    T0] ------------[ cut here ]------------
[    T0] WARNING: CPU: 0 PID: 0 at kernel/context_tracking.c:127 ct_kernel_exit.constprop.0+0x15c/0x180
[    T0] CPU: 0 UID: 0 PID: 0 Comm: swapper/0 Tainted: G      D    OE       6.18.55-trace #1 PREEMPT 
[    T0] Tainted: [D]=DIE, [O]=OOT_MODULE, [E]=UNSIGNED_MODULE
[    T0] pstate: 200003c5 (nzCv DAIF -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[    T0] Call trace:
[    T0]  ct_kernel_exit.constprop.0+0x15c/0x180 (P)
[    T0]  ct_idle_enter+0x10/0x20
[    T0]  default_idle_call+0x2c/0x148
[    T0]  do_idle+0x2b0/0x2f0
...
```

출력에서 볼 것:

- `kernel BUG at m3_bugs.c:47!` — 파일과 줄이 **로그에 바로 있다**. `BUG()`는 `brk` 명령과 함께 `__bug_table` 섹션에 "이 brk 주소 = 이 파일:줄"을 기록해 두고, 핸들러(`report_bug()`)가 pc로 표를 찾아 찍는다. 그래서 BUG/WARN은 해독 도구 없이도 줄이 나온다.
- 숫자 `f2000800`은 ESR이다. EC 0x3c(BRK), imm 0x800.
- 그 뒤가 중요하다. 태스크가 **IRQ를 끄고 preempt_count 1인 채로** 죽었다(`note: ... exited with ...`). 그 상태가 idle 태스크(`swapper/0`, PID 0)까지 번져 context tracking 검사기가 WARN을 쳤다. 이 WARN은 `m3_bugs`와 아무 관계 없는 `kernel/context_tracking.c`를 가리킨다. 그리고 taint에 **`D`(DIE)**가 붙어 있다 — "이미 한 번 죽은 커널"이라는 표시다.
- 교훈: **로그에 크래시가 여러 개면 항상 첫 번째(`[#1]`, 또는 taint에 `D`/`W`가 아직 없는 것)부터 본다**. 나머지는 후유증일 가능성이 크다. 커뮤니티도 taint `D`가 있는 보고는 "첫 Oops를 보여 달라"고 돌려보낸다.

### 8.3 WARN — 계속 달리는 경고, 그리고 `_ONCE`

`warn` 실험은 트리거를 **두 번** 썼고, 그 뒤 taint 값을 읽었다.

```text
[   T79] ------------[ cut here ]------------
[   T79] WARNING: CPU: 0 PID: 79 at m3_bugs.c:96 m3_trigger+0x134/0x210 [m3_bugs]
[   T79] Modules linked in: m3_bugs(OE)
[   T79] CPU: 0 UID: 0 PID: 79 Comm: sh Tainted: G           OE       6.18.55-trace #1 PREEMPT 
[   T79] pc : m3_trigger+0x134/0x210 [m3_bugs]
[   T79] lr : m3_trigger+0x64/0x210 [m3_bugs]
[   T79] Call trace:
[   T79]  m3_trigger+0x134/0x210 [m3_bugs] (P)
[   T79]  param_attr_store+0x80/0x110
...
[   T79] ---[ end trace 0000000000000000 ]---
rc=0
rc2=0
12800
```

출력에서 볼 것:

- `rc=0` — echo가 **성공**했다. WARN은 아무도 죽이지 않는다. `Internal error`, `Code:` 줄도 없다.
- 두 번째 트리거(`rc2=0`)는 **아무것도 찍지 않았다**. `WARN_ON_ONCE`는 호출 지점마다 한 번만 찍는다(지점별 static 플래그). 필드 로그에서 WARN이 한 번만 보인다고 한 번만 일어난 것이 아니다.
- taint `12800` = `0x3200` = 비트 9(`W`, WARN) + 12(`O`) + 13(`E`). 경고 이후 이 커널의 모든 Oops 헤더에 `W`가 찍힌다(비트 표는 M2).
- `warnf`의 `WARN(1, "m3: sensor %s timed out (%d)\n", ...)`은 메시지 한 줄이 `cut here` 바로 아래에 먼저 나온다: `m3: sensor imu0 timed out (-110)`. 드라이버에서 WARN을 쓸 거면 **메시지 있는 형태**가 필드 분석에 훨씬 낫다. (§4.3에서 본 대로 이 경우 pc와 lr이 같다.)

### 8.4 BUG: scheduling while atomic — 검사기의 보고

```text
[   T79] BUG: scheduling while atomic: sh/79/0x00000002
[   T79] Modules linked in: m3_bugs(OE)
[   T79] Preemption disabled at:
[   T79] [<ffffc54834294568>] m3_trigger+0x198/0x210 [m3_bugs]
[   T79] CPU: 1 UID: 0 PID: 79 Comm: sh Tainted: G           OE       6.18.55-trace #1 PREEMPT 
[   T79] Call trace:
[   T79]  show_stack+0x30/0x90 (C)
[   T79]  dump_stack_lvl+0x70/0x98
[   T79]  dump_stack+0x18/0x24
[   T79]  __schedule_bug+0x88/0xa8
[   T79]  __schedule+0xa00/0xc28
[   T79]  schedule+0x34/0xe0
[   T79]  schedule_timeout+0x88/0x128
[   T79]  msleep+0x3c/0x70
[   T79]  m3_trigger+0x1a0/0x210 [m3_bugs]
...
[   T79] DEBUG_LOCKS_WARN_ON(val > preempt_count())
[   T79] WARNING: CPU: 1 PID: 79 at kernel/sched/core.c:5846 preempt_count_sub+0x90/0x158
[   T79] CPU: 1 UID: 0 PID: 79 Comm: sh Tainted: G        W  OE       6.18.55-trace #1 PREEMPT 
[   T79] Call trace:
[   T79]  preempt_count_sub+0x90/0x158 (P)
[   T79]  _raw_spin_unlock+0x20/0x58
[   T79]  m3_trigger+0x1a8/0x210 [m3_bugs]
...
[   T79] m3_bugs: slept under spinlock
rc=0
```

출력에서 볼 것:

- `sh/79/0x00000002` = 명령/PID/preempt_count. 스핀락이 1, `__schedule` 진입이 1을 더했다.
- `Preemption disabled at: m3_trigger+0x198` — **선점을 끈 위치**(= `spin_lock`)를 따로 알려 준다(`CONFIG_DEBUG_PREEMPT`). 콜스택은 잠든 곳(`msleep`)이고 이 줄은 락을 잡은 곳이다. 둘 다 있어야 원인이 보인다.
- 스케줄러는 보고 뒤 preempt_count를 강제로 맞춰 계속 가므로, 나중의 `spin_unlock`이 카운트를 0 아래로 내리려 해서 **두 번째 WARN**이 나왔다. 역시 첫 보고가 원인, 두 번째는 후유증이다. 줄 사이에 `** replaying previous printk message **`가 끼어든 것은 콘솔 출력이 겹쳤을 때 printk가 하는 일이다(M1).
- 이 커널은 `DEBUG_ATOMIC_SLEEP`이 꺼져 있어 `msleep` 진입 시점이 아니라 **실제로 잠들 때** 잡혔다. 켜면 `BUG: sleeping function called from invalid context`로 더 일찍 잡힌다(E4 §5.4, M8).

### 8.5 정책 — `panic_on_oops`, `panic_on_warn`

`oops=panic`(= sysctl `kernel.panic_on_oops=1`)으로 같은 `null` 버그를 내면 Oops 끝에 이 줄이 붙고 시스템이 멈춘다(§7.2의 출력).

```text
[   T79] ---[ end trace 0000000000000000 ]---
[   T79] Kernel panic - not syncing: Oops: Fatal exception
```

`panic_on_warn=1`(= `kernel.panic_on_warn`)이면 **WARN 하나로** panic이 된다.

```text
[   T79] WARNING: CPU: 1 PID: 79 at m3_bugs.c:96 m3_trigger+0x134/0x210 [m3_bugs]
...
[   T79] Kernel panic - not syncing: kernel: panic_on_warn set ...
[   T79] Call trace:
[   T79]  show_stack+0x30/0x90 (C)
...
[   T79] Kernel Offset: 0x50c869600000 from 0xffff800080000000
[   T79] ---[ end Kernel panic - not syncing: kernel: panic_on_warn set ... ]---
```

| 설정 | 부팅 인자 / sysctl | 언제 쓰나 |
|---|---|---|
| Oops → panic | `oops=panic` / `kernel.panic_on_oops=1` | **양산 임베디드 기본값으로 권장**. 반쯤 죽은 커널(락 잡은 채 죽은 태스크, 꺼진 IRQ)로 계속 도는 것보다 증거(pstore, L7)를 남기고 리부트하는 편이 안전하다 |
| panic 후 리부트 | `panic=5` / `kernel.panic=5` | 위와 짝. 0이면 영원히 멈춤(이 랩) |
| WARN → panic | `panic_on_warn=1` / `kernel.panic_on_warn=1` | 퍼징(syzkaller), CI, 안전 인증 제품. 보통 제품은 너무 공격적 |
| soft lockup → panic | `softlockup_panic=1` / `kernel.softlockup_panic` | 실시간 제어 장비에서 "멈춤 = 고장"일 때 |
| hung task → panic | `kernel.hung_task_panic=1` | D 상태로 오래 멈춘 태스크 |
| 누적 한도 | `kernel.oops_limit`(기본 10000), `kernel.warn_limit` | 그 횟수를 넘으면 panic (반복 Oops로 공격하는 것 방지) |

Oops가 **인터럽트 문맥**에서 나면 설정과 상관없이 `Fatal exception in interrupt`로 panic이다. 죽일 "현재 태스크"가 의미가 없기 때문이다. 스택 overflow도 설정과 상관없이 panic이다(§9.6).

### 8.6 lockup — 별도 모듈로 soft lockup 내 보기

선점을 끈 채 30초 바쁘게 기다리는 별도 모듈(`m3_lock.ko`)을 `watchdog_thresh=5`로 부팅한 게스트에서 insmod했다. soft lockup 기준은 `2 × watchdog_thresh` = 10초다.

```text
5
[   17.550883][   T77] m3_lock: loading out-of-tree module taints kernel.
[   30.374426][    C1] watchdog: BUG: soft lockup - CPU#1 stuck for 13s! [insmod:77]
[   30.374640][    C1] Modules linked in: m3_lock(OE+)
[   30.382309][    C1] CPU: 1 UID: 0 PID: 77 Comm: insmod Tainted: G           OE       6.18.55-trace #1 PREEMPT 
[   30.386824][    C1] pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[   30.386910][    C1] pc : arch_timer_read_cntvct_el0+0x8/0x10
[   30.387443][    C1] lr : __delay_cycles+0x5c/0xa8
[   30.388225][    C1] Call trace:
[   30.388591][    C1]  arch_timer_read_cntvct_el0+0x8/0x10 (P)
[   30.394876][    C1]  __delay+0x5c/0xd8
[   30.394936][    C1]  __const_udelay+0x2c/0x40
[   30.394954][    C1]  m3_lock_init+0x30/0xfb0 [m3_lock]
[   30.395570][    C1]  do_one_initcall+0x5c/0x280
[   30.395591][    C1]  do_init_module+0x5c/0x270
[   30.395617][    C1]  load_module+0x1ba0/0x2258
...
[   42.374933][    C1] watchdog: BUG: soft lockup - CPU#1 stuck for 24s! [insmod:77]
[   42.375309][    C1] CPU: 1 UID: 0 PID: 77 Comm: insmod Tainted: G           OEL      6.18.55-trace #1 PREEMPT 
[   42.375397][    C1] Tainted: [O]=OOT_MODULE, [E]=UNSIGNED_MODULE, [L]=SOFTLOCKUP
...
[   64.764135][    C0] BUG: workqueue lockup - pool cpus=1 node=0 flags=0x0 nice=0 stuck for 46s!
[   64.765903][    C0]     pending: psi_avgs_work, deferred_probe_timeout_work_func
...
rc=0
28672
```

출력에서 볼 것:

- `[C1]` — 이 보고는 태스크(`T77`)가 아니라 **CPU 1의 hrtimer 인터럽트**가 찍었다(`PRINTK_CALLER`). watchdog은 각 CPU의 타이머 인터럽트에서 "그 CPU의 watchdog 스레드가 최근에 돌았나"를 확인한다. 찍힌 pc·Call trace는 **인터럽트 순간 그 CPU가 하던 일**이다 — 루프 안 아무 지점이다. 그래서 lockup 보고를 여러 번 비교하면(`__delay+0x5c` → `+0x90`) 루프의 범위가 보인다.
- `stuck for 13s` — 기준 10초인데 13초, 이후 24s·35s로 반복 보고. TCG에서 타이머가 늦게 돌기 때문에 숫자가 실제 하드웨어보다 거칠다.
- `Modules linked in: m3_lock(OE+)` — `+`는 "로드 중"(init 함수가 아직 안 끝남) 표시다.
- CPU 1이 묶이자 그 CPU의 workqueue도 멈춰 46초 뒤 `BUG: workqueue lockup`이 **다른 CPU(C0)**에서 보고됐다. 하나의 원인이 여러 검사기를 깨운다.
- 30초 뒤 init이 끝나 `rc=0`. 시스템은 살았고, taint `28672` = `0x7000` = `O`·`E`·`L`(비트 14, SOFTLOCKUP).
- 이 밖의 "멈춤" 보고는 이번에 실행하지 않았다(가이드): hard lockup(`watchdog: CPU1: Watchdog detected hard LOCKUP on cpu 1` — IRQ까지 꺼진 루프, buddy 검출기는 이웃 CPU가 보고), hung task(`INFO: task <이름>:<pid> blocked for more than 120 seconds.` — D 상태로 오래 잠든 태스크), RCU stall(`rcu: INFO: rcu_preempt detected stalls on CPUs/tasks:`).

```svg
<svg viewBox="0 0 680 500" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13">1단계: 첫 줄 패턴으로 종류를 정한다</text>
  <rect x="20" y="32" width="280" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="52" font-size="12">Unable to handle kernel ...</text>
  <rect x="20" y="68" width="280" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="88" font-size="12">kernel BUG at file:line!</text>
  <rect x="20" y="104" width="280" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="124" font-size="12">WARNING: CPU: N PID: P at ...</text>
  <rect x="20" y="140" width="280" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="160" font-size="12">BUG: scheduling while atomic</text>
  <rect x="20" y="176" width="280" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="196" font-size="12">watchdog: BUG: soft lockup</text>
  <rect x="20" y="212" width="280" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="232" font-size="12">Insufficient stack space ...</text>
  <rect x="20" y="248" width="280" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="268" font-size="12">Kernel panic - not syncing: X</text>
  <g stroke="currentColor"><line x1="300" y1="47" x2="358" y2="47" marker-end="url(#ah6)"/><line x1="300" y1="83" x2="358" y2="83" marker-end="url(#ah6)"/><line x1="300" y1="119" x2="358" y2="119" marker-end="url(#ah6)"/><line x1="300" y1="155" x2="358" y2="155" marker-end="url(#ah6)"/><line x1="300" y1="191" x2="358" y2="191" marker-end="url(#ah6)"/><line x1="300" y1="227" x2="358" y2="227" marker-end="url(#ah6)"/><line x1="300" y1="263" x2="358" y2="263" marker-end="url(#ah6)"/></g>
  <text x="365" y="52" font-size="12">MMU fault → ESR + FAR 패턴 (아래 2단계)</text>
  <text x="365" y="88" font-size="12">BUG_ON → 줄이 로그에 있음, 조건 확인</text>
  <text x="365" y="124" font-size="12">WARN → 계속 실행, 첫 번째만 본다</text>
  <text x="365" y="160" font-size="12">문맥 위반 → 락 잡은 곳 + 잠든 곳</text>
  <text x="365" y="196" font-size="12">CPU 독점 → 보고 여러 개의 pc 비교</text>
  <text x="365" y="232" font-size="12">스택 overflow → 재귀 · 큰 지역 변수</text>
  <text x="365" y="268" font-size="12">X 문장이 원인 → 그 앞의 첫 보고</text>
  <text x="20" y="312" font-size="13">2단계: FAR(주소) 값으로 버그 종류를 추정한다</text>
  <rect x="20" y="322" width="280" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="340" font-size="12">0x0 ~ 0xfff (첫 페이지)</text>
  <rect x="20" y="352" width="280" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="370" font-size="12">6b6b6b6b... (또는 5a5a..., a5)</text>
  <rect x="20" y="382" width="280" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="400" font-size="12">dead000000000100 / 122 (+8)</text>
  <rect x="20" y="412" width="280" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="430" font-size="12">41414141..., 20202020... (ASCII)</text>
  <rect x="20" y="442" width="280" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="460" font-size="12">FSC 0x21, 홀수 · 비정렬 주소</text>
  <rect x="20" y="472" width="280" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="490" font-size="12">태스크 스택 하한 바로 아래</text>
  <g stroke="currentColor"><line x1="300" y1="335" x2="358" y2="335" marker-end="url(#ah6)"/><line x1="300" y1="365" x2="358" y2="365" marker-end="url(#ah6)"/><line x1="300" y1="395" x2="358" y2="395" marker-end="url(#ah6)"/><line x1="300" y1="425" x2="358" y2="425" marker-end="url(#ah6)"/><line x1="300" y1="455" x2="358" y2="455" marker-end="url(#ah6)"/><line x1="300" y1="485" x2="358" y2="485" marker-end="url(#ah6)"/></g>
  <text x="365" y="340" font-size="12">NULL + 멤버 오프셋 → pahole</text>
  <text x="365" y="370" font-size="12">해제 후 사용 (UAF) → KASAN (M8)</text>
  <text x="365" y="400" font-size="12">list_del 두 번 / 지운 노드 사용</text>
  <text x="365" y="430" font-size="12">문자열이 포인터를 덮음 → overflow</text>
  <text x="365" y="460" font-size="12">atomic · Device 메모리 정렬 위반</text>
  <text x="365" y="490" font-size="12">guard page → 스택 overflow</text>
</svg>
```

그림 6 — "이건 무슨 크래시인가" 결정 트리. 첫 줄 패턴이 종류를, FAR 값이 버그 유형을 알려 준다. 빨강은 태스크나 시스템이 죽는 것, 주황은 계속 달리는 보고다.

---

## 9. 주소 패턴과 사례 — 숫자만 보고 버그를 추정하기

### 9.1 패턴 표

| FAR 값 | 의미 | 이번 실험 | 다음 행동 |
|---|---|---|---|
| `0x0`~`0xfff` | NULL + 멤버 오프셋. 커널이 "NULL pointer dereference"라고 쓴다 | `0x10` (null) | `pahole -C <구조체>`로 오프셋 → 멤버. 그 포인터를 돌려준 함수의 에러 경로 확인 |
| `6b6b6b6b6b6b6b6b` | `POISON_FREE` — 해제된 slab 객체를 칠한 값. slab poisoning(`slab_debug=P` 또는 `SLUB_DEBUG_ON`)이 켜져 있어야 보인다 | `006b6b6b6b6b6b6b` (uaf) | 해제 지점 찾기 — KASAN이면 alloc/free 콜스택을 다 준다(M8) |
| `5a5a5a5a…` | `POISON_INUSE` — 할당은 됐지만 초기화 안 한 slab 객체 | — | 초기화 누락, `kzalloc` 고려 |
| `dead000000000100` / `…122` | `LIST_POISON1/2` — `list_del()`이 끝난 노드에 박는 값(`0xdead…` = `CONFIG_ILLEGAL_POINTER_VALUE`) | `dead000000000108` 쓰기 (dbldel) | 같은 노드 두 번 삭제, 삭제 후 순회. 락 없이 두 경로가 지우는 레이스가 흔한 원인 |
| `4141…`, `2020…` 같은 ASCII | 문자열·패킷 데이터가 포인터 자리를 덮어썼다 | `0041414141414139` (list) | 바이트를 문자로 바꿔 보고("AAAA") 그 데이터를 쓰는 코드의 길이 검사를 본다 |
| 정렬 안 된 주소 + FSC 0x21 | exclusive/atomic 또는 Device 메모리의 비정렬 접근 | `...4089` + `ldxr` (misalign) | 구조체 `__packed`, 버퍼 + 1 같은 포인터 연산 |
| 태스크 스택 하한 바로 아래 | VMAP_STACK guard page | `0xffff800080507fc8` (stack) | 재귀, 큰 지역 배열 (E4 §4) |
| `ffff0000…` (48비트 VA) | 선형 맵 — `kmalloc`·페이지 메모리 | 레지스터 `x19 = ffff0000052c8ae0` | slab 객체 포인터 |
| `ffff8000…` | vmalloc 영역 — 커널 스택, `vmalloc`, ioremap | `sp = ffff8000804fbc50` | 스택 포인터·ioremap 주소 |

상위 바이트가 `00`으로 보이는 것(`006b6b…`, `0041…`)은 실제로는 `6b6b…`/`4141…`이다. arm64는 주소의 최상위 바이트를 태그로 쓸 수 있는 TBI를 지원하므로, 커널의 fault 처리기(`do_mem_abort()`)는 FAR에서 태그를 뗀 값(`untagged_addr(far)`)을 찍는다. 레지스터 덤프의 `x0 : 6b6b6b6b6b6b6b6b`가 원래 값이다.

### 9.2 사례 — use-after-free (`slab_debug=P`)

```text
[   T79] Unable to handle kernel paging request at virtual address 006b6b6b6b6b6b6b
[   T79]   ESR = 0x0000000096000004
[   T79]   FSC = 0x04: level 0 translation fault
[   T79] [006b6b6b6b6b6b6b] address between user and kernel address ranges
[   T79] Internal error: Oops: 0000000096000004 [#1]  SMP
[   T79] pc : m3_uaf+0x48/0x80 [m3_bugs]
[   T79] lr : m3_uaf+0x44/0x80 [m3_bugs]
[   T79] x20: ffff000006193f38 x19: ffff000006194500 x18: 0000000000000000
[   T79] x2 : 0000000000000000 x1 : 0000000000000000 x0 : 6b6b6b6b6b6b6b6b
[   T79] Call trace:
[   T79]  m3_uaf+0x48/0x80 [m3_bugs] (P)
[   T79]  m3_trigger+0x174/0x210 [m3_bugs]
[   T79] Code: aa0003f3 f9000c01 97ffff81 f9400e60 (f9400001) 
```

읽는 순서: FAR이 `6b` 반복 → 해제된 객체에서 읽은 포인터를 따라갔다. `x19 = ffff000006194500`은 선형 맵의 slab 객체(= 해제된 그 객체), `x0 = 6b6b…`는 거기서 읽은 오프셋 24의 값. lr이 `kfree` 복귀 지점(§4.3). `Code:`의 `97ffff81`은 `bl`(= `kfree`), `f9400e60` = `ldr x0, [x19, #24]`, 괄호 안 `ldr x1, [x0]`. 소스 없이도 "kfree 직후 그 객체의 +24 포인터를 역참조" 까지 나온다.

**slab poisoning이 없었다면?** 해제된 메모리에는 예전 값(`&m3_ops`)이 그대로 남아 있어서 호출이 **성공**했을 것이다. UAF는 대개 조용히 지나가다 나중에 다른 객체가 그 메모리를 재사용할 때 엉뚱한 곳에서 터진다. 개발 빌드에서 `slab_debug=P`(또는 `slab_debug=FZPU`) 부팅 인자로 poisoning을 켜는 이유다. KASAN(M8)은 첫 잘못된 접근에서 alloc·free 콜스택까지 붙여서 바로 잡는다.

### 9.3 사례 — list_del 두 번 (`dead…108`)

```text
[   T79] Unable to handle kernel paging request at virtual address dead000000000108
[   T79]   ESR = 0x0000000096000044
[   T79]   CM = 0, WnR = 1, TnD = 0, TagAccess = 0
[   T79] pc : m3_double_del+0x54/0x80 [m3_bugs]
[   T79] lr : m3_double_del+0x28/0x80 [m3_bugs]
[   T79] x2 : dead000000000122 x1 : dead000000000100 x0 : ffff000005088e60
[   T79] Call trace:
[   T79]  m3_double_del+0x54/0x80 [m3_bugs] (P)
[   T79]  m3_trigger+0x128/0x210 [m3_bugs]
[   T79] Code: d2802001 f2fbd5a1 f9000443 91008822 (f9000422) 
```

`list_del(entry)`은 `entry->next->prev = entry->prev` 를 하고 나서 `entry->next = LIST_POISON1`, `entry->prev = LIST_POISON2`를 박는다. 두 번째 `list_del`에서 `entry->next`가 `dead…100`이므로 `next->prev`(= `+8`)에 **쓰기** → `dead…108`, WnR=1. 레지스터에 `x1 = LIST_POISON1`, `x2 = LIST_POISON2`가 그대로 있다. faddr2line은 `include/linux/list.h:210`의 `__list_del` 안이라고 알려 줬다(§6.2). `CONFIG_DEBUG_LIST`(또는 `LIST_HARDENED`)가 켜져 있으면 fault 전에 `list_del corruption. next->prev should be ..., but was dead000000000122` 같은 보고로 잡힌다 — 이 랩 커널은 둘 다 꺼져 있다.

### 9.4 사례 — 크래시 지점 ≠ 버그 지점 (`list`)

이 실험은 이 노트에서 가장 중요한 교훈이다. **pc가 커널 코어(`lib/vsprintf.c`)에 있다.**

```text
[   T79] m3_bugs: tag 
[   T79] m3_bugs: tag AAAAAAAA
[   T79] Unable to handle kernel paging request at virtual address 0041414141414139
[   T79]   ESR = 0x0000000096000004
[   T79] [0041414141414139] address between user and kernel address ranges
[   T79] Internal error: Oops: 0000000096000004 [#1]  SMP
[   T79] pstate: 000000c5 (nzcv daIF -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[   T79] pc : string+0x50/0x140
[   T79] lr : vsnprintf+0x1f8/0x638
[   T79] x5 : 0000000000000008 x4 : 0000000000000000 x3 : ffffffff00080a00
[   T79] x2 : 4141414141414139 x1 : ffff80008057ba38 x0 : ffff80008057ba3f
[   T79] Call trace:
[   T79]  string+0x50/0x140 (P)
[   T79]  vsnprintf+0x1f8/0x638
[   T79]  vprintk_store+0x110/0x4a0
[   T79]  vprintk_emit+0xb4/0x340
[   T79]  vprintk_default+0x3c/0x50
[   T79]  vprintk+0x38/0x80
[   T79]  _printk+0x68/0xa0
[   T79]  m3_list_bug+0xcc/0xe8 [m3_bugs]
[   T79]  m3_trigger+0x130/0x210 [m3_bugs]
[   T79] Code: 91000400 eb0600bf 540005a0 aa0503e4 (38646845) 
[   T79] note: sh[79] exited with irqs disabled
```

`decode_stacktrace.sh`는 `pc : string (lib/vsprintf.c:653 lib/vsprintf.c:735)`, `m3_list_bug (m3_bugs.c:67 (discriminator 4))`라고 풀었다. 추리:

1. pc는 `string()` — printk의 `%s` 처리기. `ldrb w5, [x2, x4]`로 문자열의 바이트를 읽다가 죽었다. 문자열 주소 x2 = `4141414141414139`.
2. 'A' = 0x41. 그 주소는 `"AAAAAAAA"`에서 8을 뺀 값이다. §6.5의 `pahole`: `struct m3_ent`는 `tag[8]` 다음 오프셋 8에 `node`. `list_for_each_entry`는 `node.next`에서 8을 빼 엔트리 주소를 만든다(`container_of`). 즉 **어떤 엔트리의 `node.next`가 'A'로 덮여 있었고**, 그 가짜 엔트리의 `tag`를 출력하려다 죽었다.
3. 바로 앞에 `tag AAAAAAAA`가 찍혔다 — 덮어쓰인 엔트리를 정상적으로 출력한 뒤, 그 엔트리의 `next`를 따라간 것이다.
4. 그러니 버그는 printk가 아니라 **이 엔트리를 덮어쓴 코드**다(8바이트 `tag`에 24바이트를 써서 `node`의 `next`·`prev`까지 침범). 그 코드는 Call trace에 **없다** — 이미 끝났기 때문이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <line x1="20" y1="150" x2="660" y2="150" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="600" y="172" font-size="12">시간</text>
  <rect x="20" y="40" width="140" height="80" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="90" y="62" text-anchor="middle" font-size="12">① 버그 지점</text>
  <text x="90" y="84" text-anchor="middle" font-size="12">tag[8]에 24바이트</text>
  <text x="90" y="106" text-anchor="middle" font-size="12">node.next = AAAA</text>
  <rect x="180" y="40" width="140" height="80" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="62" text-anchor="middle" font-size="12">② 조용함</text>
  <text x="250" y="84" text-anchor="middle" font-size="12">다른 노드 추가</text>
  <text x="250" y="106" text-anchor="middle" font-size="12">아무 보고 없음</text>
  <rect x="340" y="40" width="140" height="80" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="410" y="62" text-anchor="middle" font-size="12">③ 순회</text>
  <text x="410" y="84" text-anchor="middle" font-size="12">tag 출력 2번 성공</text>
  <text x="410" y="106" text-anchor="middle" font-size="12">next = 0x4141...</text>
  <rect x="500" y="40" width="160" height="80" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="580" y="62" text-anchor="middle" font-size="12">④ 크래시 지점</text>
  <text x="580" y="84" text-anchor="middle" font-size="12">printk → string()</text>
  <text x="580" y="106" text-anchor="middle" font-size="12">pc = lib/vsprintf.c</text>
  <line x1="90" y1="120" x2="90" y2="146" stroke="#d0564a"/>
  <line x1="250" y1="120" x2="250" y2="146" stroke="#888"/>
  <line x1="410" y1="120" x2="410" y2="146" stroke="#888"/>
  <line x1="580" y1="120" x2="580" y2="146" stroke="#e08a3c"/>
  <rect x="20" y="190" width="640" height="96" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="214" font-size="12">Call trace에는 ④만 있다. ①은 이미 끝난 코드라 스택에 흔적이 없다</text>
  <text x="30" y="238" font-size="12">단서: FAR = ASCII 'A' − 8 → 문자열이 list 포인터를 덮었고, container_of로 8을 뺐다</text>
  <text x="30" y="262" font-size="12">더 일찍 잡으려면: CONFIG_DEBUG_LIST(②의 list_add에서), KASAN(①의 쓰기에서) — M8</text>
</svg>
```

그림 7 — 크래시 지점 ≠ 버그 지점. 메모리 오염 버그는 오염을 **사용하는** 코드에서 터진다. pc가 커널 코어 함수(`string`, `memcpy`, `list_*`, `kfree`)에 있으면 먼저 "누가 나쁜 데이터를 넘겼나"를 Call trace 아래쪽에서 찾는다.

### 9.5 사례 — 정렬 fault (`misalign`)

```text
[   T79] Unable to handle kernel paging request at virtual address ffffcadb174c4089
[   T79]   ESR = 0x0000000096000021
[   T79]   FSC = 0x21: alignment fault
[   T79] swapper pgtable: 4k pages, 48-bit VAs, pgdp=0000000043567000
[   T79] [ffffcadb174c4089] pgd=100000004418a003, p4d=100000004418a003, pud=100000004510d403, pmd=100000004510e403, pte=00e800004510ff03
[   T79] pc : m3_misalign+0x38/0x60 [m3_bugs]
[   T79] lr : m3_trigger+0x15c/0x210 [m3_bugs]
[   T79] x2 : 000000000000000a x1 : ffffcadb174c4089 x0 : 00000000ffffffea
[   T79] Code: d0000001 91022021 91000421 f9800031 (885f7c20) 
```

이번엔 `swapper pgtable`(커널 주소)이고 **PTE까지 전부 유효하다**(`pte=00e8…ff03`). 페이지는 멀쩡히 있다 — 문제는 주소 끝자리 `…89`(홀수)다. arm64는 일반 메모리에 대한 보통의 `ldr`/`str`은 비정렬이어도 하드웨어가 처리하지만, `ldxr`/`stxr`(exclusive) 같은 atomic 명령과 Device 메모리(MMIO) 접근은 자연 정렬이 필수다. Cortex-M에서 `UNALIGN_TRP`나 `LDREX` 정렬 fault를 본 적이 있다면 같은 이야기다. 실무 원인은 `__packed` 구조체 안의 `atomic_t`/`spinlock_t`, 또는 패킷 버퍼 + 1 같은 포인터 연산이다. 드라이버의 `readl()`을 비정렬 MMIO 주소로 해도 같은 FSC가 나온다.

### 9.6 사례 — 스택 overflow (`stack`)

```text
[    C1] Insufficient stack space to handle exception!
[    C1] ESR: 0x0000000096000047 -- DABT (current EL)
[    C1] FAR: 0xffff800080507fc8
[    C1] Task stack:     [0xffff800080508000..0xffff80008050c000]
[    C1] IRQ stack:      [0xffff800080008000..0xffff80008000c000]
[    C1] Overflow stack: [0xffff00001fed1140..0xffff00001fed2140]
[    C1] pc : __pi_memset_generic+0x50/0x188
[    C1] lr : m3_recurse+0x3c/0xa0 [m3_bugs]
[    C1] sp : ffff800080507fc0
[    C1] Kernel panic - not syncing: kernel stack overflow
[    C1] Call trace:
...
[    C1]  __bad_stack+0x80/0x88
[    C1]  __pi_memset_generic+0x50/0x188 (P)
[    C1]  m3_recurse+0x80/0xa0 [m3_bugs]
...  (m3_recurse+0x80 줄이 수십 번 반복)
[    C1]  m3_trigger+0x1c4/0x210 [m3_bugs]
```

FAR `…7fc8`은 태스크 스택 하한 `…8000`의 **0x38바이트 아래**, guard page다. 형식이 일반 Oops와 다르다: `Mem abort info` 해석 대신 `ESR:`/`FAR:` 한 줄씩, 그리고 세 스택의 범위. 예외 진입 코드가 sp를 검사해 CPU별 Overflow stack으로 갈아타고 바로 panic한다(태스크를 정상 종료시킬 스택이 없으니까). pc가 `memset`인 이유(지역 배열 0 초기화, `CONFIG_INIT_STACK_ALL_ZERO`)와 16 KiB 스택 이야기는 E4 §4.4에서 같은 형식의 로그로 자세히 다뤘다. 여기서 볼 것은 **반복되는 Call trace 줄 = 재귀**라는 패턴뿐이다.

### 9.7 사례 — 0으로 나누기는 트랩이 아니다 (`div0`)

```text
[   T79] m3_bugs: 100 / 0 = 0
rc=0
```

Oops가 없다. A64의 `sdiv`/`udiv`는 제수가 0이면 **결과 0을 돌려줄 뿐 예외를 내지 않는다**(아키텍처 정의). x86은 `#DE` 예외 → `divide error: 0000` Oops를 내고, Cortex-M은 `CCR.DIV_0_TRP`를 켜면 UsageFault를 낸다. 그래서 arm64에서는 0 나누기가 **조용히 틀린 값**이 된다. "x86에서는 죽던 버그가 ARM 보드에선 안 죽는다"는 이식 함정이고, UBSAN(M8)의 `-fsanitize=integer-divide-by-zero`로만 잡힌다.

---

## 10. 현장 워크플로 — 붙여 넣은 Oops 하나만 받았을 때

고객이 슬랙에 콘솔 로그를 붙여 넣었다. 할 일을 순서대로 적는다.

1. **첫 보고를 찾는다.** `[#1]`, taint에 `D`/`W`가 아직 없는 것. 그 앞 수십 줄(드라이버 에러, 타임아웃, `-EIO`)도 같이 확보한다 — 원인은 종종 Oops 전에 찍혀 있다.
2. **종류를 정한다**(그림 6). Oops / BUG / WARN / 검사기 / lockup / panic.
3. **빌드를 고정한다.** `Linux version`·`CPU:` 줄의 버전 문자열(`6.18.55-trace #1`)과 Tainted의 모듈 목록으로 **정확히 같은 빌드의 vmlinux와 .ko**를 구한다. 빌드 서버에 이미지마다 디버그 심볼(vmlinux, `.ko` 디버그 판, `System.map`)을 build-id와 함께 보관해 두는 것이 이 단계의 전제다. 다른 빌드로 풀면 줄 번호가 **오류 없이 조용히** 틀린다.
4. **원인 필드**: ESR → EC·WnR·DFSC, FAR 패턴(§9.1).
5. **위치**: `pc`를 `faddr2line`(크기가 안 맞으면 `/size` 제거), 의심스러우면 `readelf -s`로 섹션 확인 후 `addr2line -j`. 인라인 체인까지 본다.
6. **호출자**: `lr`과 Call trace를 같이 본다. leaf면 lr이 유일한 호출자 정보다(§4.2). lr이 같은 함수 안이면 "마지막으로 부른 함수"다(§4.3).
7. **데이터**: 레지스터 중 FAR과 같은 값, 0, poison 값을 찾고, `Code:`를 decodecode로 풀어 어느 레지스터가 베이스였는지 본다. 오프셋은 `pahole`로 멤버 이름이 된다.
8. **크래시 지점이 버그 지점인가?** pc가 커널 코어 함수면 데이터가 어디서 왔는지 거슬러 올라간다(§9.4).
9. **재현과 탐지기**: 같은 조건을 KASAN·`DEBUG_LIST`·lockdep 켠 빌드에서 재현한다(M8). 날 주소만 있으면 §7의 KASLR 보정.

로그가 아예 없다면? L7(pstore/ramoops로 다음 부팅에 회수, kdump로 메모리 전체), M1(earlycon), M4(JTAG로 멈춘 CPU의 PC·`__log_buf` 직접 읽기)로 간다.

---

## 11. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah8" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="200" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="45" text-anchor="middle" font-size="12">ldr w0, [x0, #16] → abort</text>
  <rect x="20" y="80" width="200" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="105" text-anchor="middle" font-size="12">el1h_64_sync → el1_abort</text>
  <rect x="20" y="140" width="200" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="165" text-anchor="middle" font-size="12">do_mem_abort(far, esr)</text>
  <rect x="20" y="200" width="200" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="225" text-anchor="middle" font-size="12">do_page_fault</text>
  <rect x="20" y="260" width="200" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="285" text-anchor="middle" font-size="12">__do_kernel_fault</text>
  <g stroke="currentColor"><line x1="120" y1="60" x2="120" y2="78" marker-end="url(#ah8)"/><line x1="120" y1="120" x2="120" y2="138" marker-end="url(#ah8)"/><line x1="120" y1="180" x2="120" y2="198" marker-end="url(#ah8)"/><line x1="120" y1="240" x2="120" y2="258" marker-end="url(#ah8)"/></g>
  <rect x="250" y="20" width="200" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="350" y="45" text-anchor="middle" font-size="12">brk #0x800 (BUG/WARN)</text>
  <rect x="250" y="80" width="200" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="350" y="105" text-anchor="middle" font-size="12">bug_brk_handler</text>
  <rect x="250" y="140" width="200" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="350" y="165" text-anchor="middle" font-size="12">report_bug(pc) → __bug_table</text>
  <rect x="250" y="200" width="200" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="350" y="225" text-anchor="middle" font-size="12">WARN: __warn, pc += 4</text>
  <g stroke="currentColor"><line x1="350" y1="60" x2="350" y2="78" marker-end="url(#ah8)"/><line x1="350" y1="120" x2="350" y2="138" marker-end="url(#ah8)"/><line x1="350" y1="180" x2="350" y2="198" marker-end="url(#ah8)"/></g>
  <text x="350" y="262" text-anchor="middle" font-size="12">→ 계속 실행</text>
  <rect x="480" y="140" width="180" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570" y="165" text-anchor="middle" font-size="12">die("Oops", regs, esr)</text>
  <rect x="480" y="200" width="180" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570" y="225" text-anchor="middle" font-size="12">__die: 헤더·레지스터·Code</text>
  <rect x="480" y="260" width="180" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570" y="282" text-anchor="middle" font-size="12">panic 정책 또는</text>
  <text x="570" y="302" text-anchor="middle" font-size="12">make_task_dead(SIGSEGV)</text>
  <g stroke="currentColor"><line x1="220" y1="280" x2="478" y2="165" marker-end="url(#ah8)"/><line x1="450" y1="160" x2="478" y2="160" marker-end="url(#ah8)"/><line x1="570" y1="180" x2="570" y2="198" marker-end="url(#ah8)"/><line x1="570" y1="240" x2="570" y2="258" marker-end="url(#ah8)"/></g>
  <text x="470" y="118" font-size="12">BUG: die("Oops - BUG")</text>
</svg>
```

그림 8 — 커널 안의 경로. MMU abort와 BUG의 `brk`는 다른 입구로 들어와 같은 `die()`에서 만난다. WARN은 `die()`에 가지 않고 pc를 다음 명령으로 넘겨 계속 실행한다.

- **fault 경로** (`arch/arm64/mm/fault.c`): `do_mem_abort()`가 ESR의 DFSC로 처리 함수 표를 찾는다(translation fault → `do_translation_fault` → `do_page_fault`). 커널 모드이고 유저 주소 처리로 해결이 안 되면 `__do_kernel_fault()`가 먼저 **exception table**을 본다. `copy_from_user()`처럼 "fault가 나도 되는" 명령이면 fixup 주소로 점프해서 `-EFAULT`를 돌려주고 끝이다(E6, `get_user`). 표에 없으면 `die_kernel_fault()`가 `Unable to handle kernel ...`, `mem_abort_decode()`(Mem abort info 블록), `show_pte()`를 찍고 `die()`를 부른다.
- **BUG/WARN 경로**: `BUG()`/`WARN()`은 `brk #0x800`과 `__bug_table` 항목(주소·파일·줄·플래그)을 만든다. debug 예외 → `bug_brk_handler()` → `report_bug()`가 pc로 표를 찾아 `kernel BUG at …` 또는 `WARNING: …`을 찍는다. BUG면 `die("Oops - BUG", …)`, WARN이면 `arm64_skip_faulting_instruction()`으로 pc += 4 하고 돌아간다.
- **`die()`** (`arch/arm64/kernel/traps.c`): `die_lock`을 잡고 `console_verbose()`(콘솔 레벨을 최대로 — `quiet`여도 Oops가 보이는 이유, M1), `__die()`가 `Internal error: %s: %016lx [#%d] SMP`, 모듈 목록, `show_regs()`, Call trace, `Code:`를 찍는다. taint `D`를 더하고, `in_interrupt()`면 `panic("Fatal exception in interrupt")`, `panic_on_oops`면 `panic("Fatal exception")`, 아니면 `make_task_dead(SIGSEGV)` — 유저가 본 `Segmentation fault`/`rc=139`다.
- 드라이버를 쓸 때 다시 만나는 지점: `BUG_ON`은 리뷰에서 거의 거부된다(시스템을 죽이니까). 복구 가능한 상황은 `WARN_ON_ONCE` + 에러 반환(`if (WARN_ON_ONCE(!dev)) return -EINVAL;`)이 커널 관례다(E5). 유저 포인터는 반드시 exception table을 쓰는 `copy_*_user` 계열로 만져야 Oops가 아니라 `-EFAULT`가 된다(E6, G4).

---

## 12. Embedded 실무에서 다시 보기

**센서 키트 IMU 드라이버의 probe NULL.** i.MX8 보드에서 IMU 드라이버가 `devm_regulator_get_optional()`의 반환값을 NULL 검사만 하고 쓰면, 실패 시 돌아온 `ERR_PTR(-ENODEV)`(= `0xffffffffffffffed`)에 멤버 오프셋을 더한 주소가 FAR로 찍힌다(오프셋이 크면 0 근처로 넘어가기도 한다). **FAR이 `0xffff…fff`에 가까운 작은 음수면 `ERR_PTR` 역참조**다(`IS_ERR` 대신 `!ptr` 검사). 작은 양수(0x10)는 NULL, 큰 음수 근처는 ERR_PTR — 이 두 패턴만 알아도 bring-up Oops의 절반은 첫 줄에서 끝난다. (이 FAR 패턴은 이번 랩에서 실행하지 않았다 — 가이드.)

**전원 꺼진 블록의 레지스터 접근.** 런타임 PM으로 클록이 꺼진 IP의 레지스터를 `readl()`하면 SoC에 따라 동기 external abort(`EA = 1`, `FSC = 0x10`)나 비동기 `SError Interrupt`가 난다. SError는 pc가 원인 명령이 아닐 수 있다(BFSR.IMPRECISERR와 같다). Don의 RF 칩 bring-up 경험 — "버스 에러가 나면 먼저 클록·전원·리셋" — 이 그대로 통한다(N5 런타임 PM, H 모듈). (가이드 — QEMU virt에서는 재현하지 않았다.)

**필드 장비의 로그 회수.** 양산 드론·카메라에는 UART가 없다. `panic_on_oops=1` + `panic=5` + ramoops(L7)로 Oops 텍스트를 다음 부팅에 `/sys/fs/pstore/dmesg-ramoops-0`로 회수하고, 빌드 서버의 build-id 매칭 vmlinux·`.ko`로 §10 순서를 돌린다. 모듈의 디버그 정보는 이미지에서 strip되므로(`INSTALL_MOD_STRIP=1`) **strip 전 `.ko`를 따로 보관**해야 한다 — 없으면 Call trace의 `[모듈]` 줄은 함수+오프셋까지만 읽힌다.

**벤더 BSP 모듈.** 소스 없는 벤더 `.ko`(GPU, Wi-Fi)에서 Oops가 나면 `Tainted: P`(proprietary)가 붙고 커뮤니티 지원은 끝이다. 그래도 `Code:`와 `objdump -dr`로 죽은 명령과 오프셋까지는 직접 볼 수 있어 벤더에 보낼 버그 리포트의 질이 달라진다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 마지막 Oops부터 읽는다 | 원인과 무관한 코어 함수를 쫓는다 | 첫 Oops의 후유증(§8.2) | `[#1]`, taint `D`/`W` 없는 첫 보고부터 |
| Call trace만 보고 lr을 안 본다 | 호출자를 못 찾는다 | leaf 함수는 frame record를 안 만든다(§4.2) | pc·lr·Call trace를 함께 읽는다 |
| 다른 빌드의 vmlinux/.ko로 해독 | 그럴듯하지만 틀린 줄 번호 | 오프셋이 빌드마다 다르다 | build-id 일치하는 디버그 심볼 보관 |
| `decode_stacktrace.sh` 결과를 그대로 믿는다 | 함수명은 맞고 줄이 다른 함수 | `.text.unlikely` 등 섹션 미지정(§6.3) | `faddr2line`, `readelf -s` 섹션 확인 후 `addr2line -j` |
| `faddr2line`이 "size mismatch"로 거부하자 포기 | `no match` | kallsyms 크기가 다음 심볼 거리 | `/size`를 떼고 다시 |
| `.ko`를 `objdump -d`만으로 읽음 | `bl`이 엉뚱한 함수를 가리킴 | 재배치 전 오브젝트 | `objdump -dr` |
| 날 주소를 System.map과 바로 비교 | 심볼이 안 맞는다 | KASLR | `Kernel Offset` 또는 `_text` 차이를 뺀다, 모듈은 섹션 시작을 뺀다 |
| pc가 `string`/`memcpy`라 커널 버그라고 결론 | 엉뚱한 곳에 패치 | 크래시 지점 ≠ 버그 지점(§9.4) | 나쁜 데이터를 넘긴 호출자·오염원 추적, KASAN |
| 0 나누기가 안 죽으니 괜찮다고 판단 | 조용히 틀린 값 | arm64 `sdiv`는 트랩 없음 | UBSAN, 제수 검사 |
| 양산에서 `panic_on_oops=0` | 반쯤 죽은 커널로 계속 동작, 락·IRQ 꺼진 채 | Oops는 태스크만 죽인다 | `panic_on_oops=1` + `panic=N` + pstore |

---

## 14. 면접에서 이렇게 말한다

**Q.** Here's a kernel oops. Walk me through how you read it.

**A.** 첫 Oops인지(`[#1]`, taint) 확인 → 첫 줄과 ESR로 종류(커널 data abort, 읽기, L2 translation fault)와 FAR(0x10) → `Modules linked in`·taint로 용의자 → `pc`를 `faddr2line`으로 소스 줄과 인라인 체인 → `lr`과 Call trace로 호출 경로(leaf면 lr만이 호출자) → 레지스터와 `Code:`로 어느 포인터가 NULL이었는지, `pahole`로 오프셋을 멤버 이름으로 → 마지막으로 크래시 지점이 진짜 버그 지점인지 판단한다.

> First I make sure it's the first oops — the [#1] and no D taint — because later ones are often fallout. The first line plus the ESR tell me what happened: here a kernel-mode data abort, a read, translation fault, at address 0x10. Then Modules linked in and the taint flags give me suspects. I take the pc, symbol plus offset, and run faddr2line against the exact same build's module to get the file, line and inline chain. Then lr and the call trace for the call path — if the faulting function is a leaf, the caller only shows up in lr. The registers and the Code line tell me which register held the bad pointer, and pahole turns the offset into a struct member. Last question: is the crash site the bug site, or did someone corrupt the data earlier?

**꼬리 질문**: "decode_stacktrace.sh가 틀린 줄을 줄 수 있나?" → 함수가 `.text.unlikely` 같은 다른 섹션에 있으면 그렇다. 실제로 겪었고 faddr2line/`addr2line -j`로 교차 확인했다.

**Q.** The faulting address is 0x10. What does that tell you?

**A.** 첫 페이지 안의 작은 주소는 NULL 포인터 + 멤버 오프셋이다. 0x10이면 오프셋 16의 멤버다. `Code:`의 `ldr w0, [x0, #16]`이 4바이트 읽기임을 확인하고, `pahole`로 그 구조체의 16번 오프셋 멤버를 찾는다. 그다음은 "왜 그 포인터가 NULL이었나" — 대개 조회·할당 함수의 실패 반환값을 확인하지 않은 것이다.

> A small address in the first page means a NULL struct pointer plus a member offset — here offset 16. The Code line shows ldr w0, [x0, #16], so a 32-bit load with x0 equal to zero. pahole on the struct tells me which member sits at offset 16; in my lab it was the irq field. Then the real question is why the pointer was NULL — usually a lookup or allocation whose failure return wasn't checked. If the address were something like 6b6b6b6b, I'd think use-after-free instead, and dead000000000100 or 122 means a list entry that was already deleted.

**Q.** What's the difference between an oops, a panic, a BUG and a WARN?

**A.** Oops는 커널 모드 예외로 현재 태스크만 죽이고 계속 간다(taint D). panic은 모든 CPU를 멈춘다. `BUG_ON`은 일부러 Oops를 내는 것(`brk`)이고, WARN은 콜스택만 찍고 계속 실행한다(taint W). Oops가 인터럽트 문맥이면 panic이고, `panic_on_oops`/`panic_on_warn`으로 정책을 바꾼다.

> An oops is an unhandled exception in kernel mode: the kernel prints the report and kills the current task, but keeps running, tainted D. A panic stops all CPUs. BUG_ON deliberately triggers an oops through a brk instruction; WARN prints the same kind of report and then just continues, tainted W. An oops in interrupt context always becomes a panic, and panic_on_oops or panic_on_warn turn the others into panics. On production embedded devices I set panic_on_oops plus a reboot timeout and capture the log with pstore, because a half-dead kernel holding locks is worse than a clean reboot.

**Q.** Why might the function that called the crashing function be missing from the call trace?

**A.** arm64 unwinder는 x29 frame record 체인을 따라간다. leaf 함수는 frame record를 안 만들어서 x29가 여전히 호출자의 record를 가리키고, 그 record에는 호출자의 **복귀 주소**(한 단계 위)만 있다. 호출자 자신의 위치는 x30(lr)에만 있다. 인라인된 함수도 Call trace에 안 나오며 faddr2line의 inlined-by 체인으로만 보인다.

> The arm64 unwinder walks the chain of frame records — x29 and x30 pairs pushed in each prologue. A leaf function doesn't push one, so x29 still points at the caller's record, which holds the caller's return address — one level up. The caller's own position is only in lr. I saw exactly that: the pc was in a small getter, and its caller appeared only on the lr line. Inlined functions never appear either; faddr2line's inlined-by chain shows them.

**Q.** You only have a raw address from a field log. How do you map it to a symbol with KASLR on?

**A.** 커널 이미지는 panic 로그의 `Kernel Offset: X`나 런타임 `_text`와 System.map `_text`의 차이로 오프셋을 구해 빼면 System.map 주소가 된다. 모듈은 별도 무작위 배치라 `/sys/module/<m>/sections/` 시작 주소를 빼서 섹션 오프셋으로 만들고 `addr2line -j <섹션>` 또는 gdb `add-symbol-file`에 섹션 주소를 다 준다.

> For the kernel image I subtract the KASLR offset — either from the Kernel Offset line in the panic, or runtime _text minus System.map _text — and then addr2line against vmlinux. Modules are randomized separately, so I subtract the section base from /sys/module/name/sections and use addr2line with -j for that section, or give gdb add-symbol-file every section address. For repeatable debugging sessions I'll just boot with nokaslr.

**Q.** The pc is inside printk's string(). Is this a printk bug?

**A.** 거의 아니다. 코어 함수에서 죽으면 나쁜 데이터를 넘긴 쪽을 의심한다. 실험에서 FAR이 ASCII 'A' − 8이었다 — 문자열이 리스트 포인터를 덮었고 `container_of`가 8을 뺐다. 버그는 덮어쓴 코드였고 Call trace에는 없었다. `DEBUG_LIST`나 KASAN으로 더 일찍 잡는다.

> Almost never. When the pc is in a core helper like string, memcpy or list code, I look for who passed bad data. In my lab the address was 0x4141414141414139 — ASCII A's minus 8 — so string data had overwritten a list pointer and container_of subtracted the member offset. The real bug was an earlier buffer overrun that had already returned, so it's not on the stack at all. I'd reproduce with CONFIG_DEBUG_LIST or KASAN to catch it at the write.

**Q.** How does this map to Cortex-M HardFault debugging you've done?

**A.** 1:1이다. CFSR ↔ ESR, BFAR/MMFAR ↔ FAR(BFARVALID ↔ FnV 반대 극성), stacked PC/LR/xPSR ↔ pc/lr/pstate, imprecise bus fault ↔ SError, map 파일 ↔ System.map + KASLR 보정. 차이는 Linux는 해석까지 찍어 주고, 기본이 시스템 정지가 아닌 태스크 종료라는 것.

> It's almost one to one. CFSR maps to ESR, BFAR and MMFAR to FAR — with FnV as the inverted BFARVALID — and the stacked PC, LR and xPSR to the pc, lr and pstate lines. An imprecise bus fault is an SError on arm64. The map file becomes System.map plus a KASLR correction. The differences are that Linux decodes everything for you, and that by default it kills only the current task instead of halting the system — which is why I'd turn on panic_on_oops on a product.

---

## 15. 직접 해보기

1. **개념** — ESR `0x92000047`을 손으로 풀어라. EC·WnR·DFSC는? 커널 모드인가?
   정답/힌트: EC = 0x24(하위 EL = 유저 모드 data abort), WnR = 1, DFSC 0x07 L3 translation. 커널 Oops가 아니라 유저 프로세스의 page fault(대개 정상 처리되거나 SIGSEGV)다.
2. **개념** — FAR이 `fffffffffffffff4`였다. 무엇을 의심하나?
   정답/힌트: `ERR_PTR(-12)`(`-ENOMEM`) 역참조. `IS_ERR()` 검사 누락.
3. **해독 과제** — §8.6 lockup 로그의 `m3_lock_init+0x30/0xfb0`을 `faddr2line`으로 풀어 보라. 처음에 무슨 메시지가 나오나?
   정답/힌트: size mismatch로 거부된다. `/0xfb0`를 떼면 된다. `0xfb0`이 왜 그렇게 큰지 §6.2로 설명해 보라.
4. **해독 과제** — §9.4 `list` Oops의 `string+0x50/0x140`을 vmlinux-trace로 `faddr2line`하고, 출력의 두 줄(653, 735)이 각각 무슨 함수인지 말하라.
   정답/힌트: `string_nocheck`(vsprintf.c:653)가 `string`(735)에 인라인됐다. 실제 출력은 §9.4 위 문단.
5. **관찰 과제** — QEMU 게스트에서 `echo 1 > /proc/sys/kernel/panic_on_oops` 후 같은 Oops를 내고, panic 줄의 `Kernel Offset`으로 Call trace의 `vfs_write` 런타임 주소를 계산해 System.map과 대조하라.
   정답/힌트: §7.2. 오프셋을 더하면 런타임, 빼면 System.map.
6. **코드 과제** — `__packed` 구조체 안에 `atomic_t`를 둔 모듈을 만들어 `atomic_inc`를 해 보고, FSC가 0x21인지, 그리고 `__packed`를 빼면 사라지는지 확인하라.
   정답/힌트: 필드 오프셋이 홀수가 되게 `u8` 하나를 앞에 둔다. `ldxr`/`stxr` 또는 LSE의 `ldadd` 명령이 정렬을 요구한다(CPU에 따라 명령이 다름 — cortex-a57은 LSE 없음).
7. **관찰 과제** — `uaf`를 `slab_debug=P` 없이 돌려 보라. 무슨 일이 일어나나? 왜 그런가?
   정답/힌트: 아마 아무 일도 안 일어난다(해제된 메모리에 옛 값이 남음). §9.2. Image-kasan에서 돌리면 KASAN 리포트가 나온다(M8).

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Oops | 커널 모드 예외 보고 | 현재 태스크를 죽이고 커널은 계속 돈다 |
| panic | 커널 정지 | `Kernel panic - not syncing:`. 모든 CPU 정지 |
| ESR_EL1 | Exception Syndrome Register | 예외 원인. EC·IL·ISS. CFSR에 해당 |
| FAR_EL1 | Fault Address Register | 결함 주소. BFAR/MMFAR에 해당 |
| ELR_EL1 | Exception Link Register | 예외를 낸 명령 주소 = Oops의 `pc` |
| EC | Exception Class | ESR 31:26. 0x25 커널 data abort, 0x3c BRK |
| DFSC | Data Fault Status Code | translation/permission/alignment 등 종류 |
| WnR | Write not Read | 1이면 쓰기 접근 |
| frame record | x29·x30 쌍 | 함수 프롤로그가 스택에 넣는 연결 리스트 노드, unwinder가 따라감 |
| leaf 함수 | 다른 함수를 안 부르는 함수 | frame record를 안 만들 수 있음 → 호출자가 Call trace에서 빠짐 |
| taint | 오염 플래그 | 커널이 "정상이 아님"을 기록한 비트. `D` DIE, `W` WARN, `O` OOT, `E` 서명 없음, `L` softlockup |
| KASLR | 커널 주소 무작위화 | 부팅마다 이미지 위치가 바뀜, `Kernel Offset`으로 보정 |
| System.map | 빌드 시점 심볼표 | KASLR 오프셋 0 기준 주소 |
| kallsyms | 런타임 심볼표 | Oops에 심볼 이름을 찍는 근거, `/proc/kallsyms` |
| faddr2line | 커널 스크립트 | `심볼+off/size` → 파일:줄 + 인라인 체인 |
| decode_stacktrace.sh | 커널 스크립트 | Oops 로그 전체를 파일:줄로 변환 |
| decodecode | 커널 스크립트 | `Code:` 줄 역어셈블 |
| pahole | DWARF/BTF 도구 | 구조체 레이아웃·멤버 오프셋 |
| `.text.unlikely` | cold 코드 섹션 | 거의 실행 안 되는 함수를 모아 둔 섹션, 줄 번호 도구의 함정 |
| POISON_FREE (0x6b) | slab 독 값 | 해제된 객체를 칠한 값 → UAF 신호 |
| LIST_POISON1/2 | 리스트 독 값 | `dead…100`/`dead…122`, 삭제된 노드 |
| VMAP_STACK | vmalloc 커널 스택 | guard page로 overflow를 즉시 잡음 |
| soft lockup | CPU 독점 | 한 CPU에서 `2 × watchdog_thresh`초 이상 스케줄 안 됨 |

---

## 17. 요약 & 체크리스트

Oops는 HardFault 덤프를 커널이 해석까지 해서 찍어 주는 것이다. 첫 Oops를 고르고, 첫 줄·ESR(EC·WnR·DFSC)·FAR로 "무엇이 왜"를, `pc`→`faddr2line`으로 "어디서"를, `lr`·Call trace로 "누가"를, 레지스터·`Code:`·`pahole`로 "어떤 데이터"를 읽는다. 도구는 같은 빌드의 심볼이 있어야 맞고, 섹션(`.text.unlikely`)·kallsyms 크기·재배치 전 objdump 같은 함정 때문에 두 도구로 교차 확인한다. KASLR은 `Kernel Offset`/`_text` 차이로, 모듈은 섹션 시작 주소로 보정한다. 주소 패턴(0x10, `6b6b`, `dead…100/122`, ASCII, 홀수 주소, 스택 하한)은 버그 유형을 바로 알려 주고, 메모리 오염 버그는 크래시 지점과 버그 지점이 다르다는 것을 항상 기억한다. Oops·panic·BUG·WARN·lockup은 보고 형식과 "그 뒤"가 다르며, 양산 장비는 `panic_on_oops` + 리부트 + pstore가 기본 정책이다.

- [ ] arm64 Oops의 모든 줄(첫 줄 ~ end trace)이 무엇인지 설명할 수 있다
- [ ] ESR 값을 EC·IL·WnR·DFSC로 손으로 풀 수 있다
- [ ] Cortex-M CFSR/BFAR/stacked PC·LR과 ESR/FAR/pc·lr을 대응시켜 말할 수 있다
- [ ] leaf 함수의 호출자가 Call trace에서 빠지는 이유를 frame record로 설명할 수 있다
- [ ] `faddr2line`, `decode_stacktrace.sh`, `addr2line -j`, `gdb list`, `decodecode`, `pahole`을 써서 pc를 소스 줄·멤버 이름으로 바꿀 수 있다
- [ ] `decode_stacktrace.sh`가 틀리는 경우(.text.unlikely)와 `faddr2line` size mismatch를 설명하고 우회할 수 있다
- [ ] KASLR 런타임 주소를 System.map 주소로, 모듈 주소를 섹션 오프셋으로 바꿀 수 있다
- [ ] 0x10, `6b6b…`, `dead…100/122`, ASCII, 비정렬, 스택 하한 패턴을 보고 버그 유형을 추정할 수 있다
- [ ] Oops·panic·BUG·WARN·soft lockup의 첫 줄과 "그 뒤"를 구별할 수 있다
- [ ] `panic_on_oops`·`panic_on_warn`·`panic=`의 의미와 양산 권장값을 말할 수 있다
- [ ] 크래시 지점 ≠ 버그 지점 사례를 하나 들어 추리 과정을 설명할 수 있다
- [ ] 붙여 넣은 Oops 하나로 시작하는 현장 워크플로 9단계를 말할 수 있다

## 참고 자료

- docs.kernel.org — "Bug hunting" (`admin-guide/bug-hunting`): Oops 읽기, `faddr2line`, `decode_stacktrace.sh`, `gdb` 사용 — https://docs.kernel.org/admin-guide/bug-hunting.html
- docs.kernel.org — "Tainted kernels" (`admin-guide/tainted-kernels`): taint 문자·비트 표 — https://docs.kernel.org/admin-guide/tainted-kernels.html
- docs.kernel.org — "Reporting issues" (`admin-guide/reporting-issues`): 첫 Oops, taint, 디코드된 로그를 보고하는 법 — https://docs.kernel.org/admin-guide/reporting-issues.html
- docs.kernel.org — "Softlockup detector and hardlockup detector" (`admin-guide/lockup-watchdogs`) — https://docs.kernel.org/admin-guide/lockup-watchdogs.html
- 커널 소스 (6.18.55, elixir.bootlin.com에서 열람): `arch/arm64/mm/fault.c`(`do_mem_abort`, `die_kernel_fault`, `mem_abort_decode`, `show_pte`), `arch/arm64/kernel/traps.c`(`die`, `__die`, `bug_brk_handler`), `arch/arm64/kernel/stacktrace.c`(unwinder), `lib/bug.c`(`report_bug`), `include/linux/poison.h`, `scripts/faddr2line`, `scripts/decode_stacktrace.sh`, `scripts/decodecode`
- Arm Architecture Reference Manual for A-profile (DDI 0487) — ESR_EL1, FAR_EL1, ISS encoding for Data Abort
- Arm, "ARMv7-M Architecture Reference Manual" — CFSR/HFSR/BFAR (대응표의 Cortex-M 쪽)
- 관련 노트: E4 §4·§8(스택 overflow·Oops 첫 읽기), M1(printk·콘솔), M2(taint 표·`/proc/modules`), M4(kgdb/JTAG), M8(KASAN·lockdep·UBSAN), L7(pstore·kdump)
