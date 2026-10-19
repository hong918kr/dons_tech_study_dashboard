# L7. 크래시 데이터 수집 — pstore/ramoops, kdump, coredump 수집, 원격 로그

> **이 노트를 다 읽으면**: 필드 크래시를 종류별(커널 panic·lockup·watchdog 리셋·유저 segfault·OOM·무응답)로 나누고 각각 무엇이 증거로 남는지 말할 수 있다 · DT `reserved-memory`에 ramoops 노드를 넣어 panic 로그·console·pmsg를 warm reset 너머로 넘기고 systemd-pstore로 디스크에 옮길 수 있다 · `crashkernel=` + `kexec -p`로 kdump를 걸어 vmcore를 뜨고 makedumpfile·crash로 분석할 수 있다 · systemd-coredump·core_pattern·netconsole·journal-upload로 유저 크래시와 로그를 장비 밖으로 꺼내는 파이프라인을 설계할 수 있다
> **왜 필요한가**: "A device in the field rebooted overnight — how do you find out why?"는 장비 회사 Embedded Linux 면접의 단골 설계 질문이다. 실무에서는 재현 안 되는 필드 리셋 하나가 몇 주를 잡아먹고, 그때 손에 든 증거의 질이 해결 속도를 정한다
> **Don 기준 난이도**: SSD 펌웨어의 "예약 DRAM에 마지막 로그 남기기", "NAND 로그 영역에 assert 덤프 쓰기", "다음 부팅 때 vendor log page로 꺼내기"는 이미 몸으로 안다 / 그것이 Linux에서는 pstore·kdump·coredump·journald라는 서로 다른 계층으로 쪼개져 있다는 것, 각 계층의 설정 지점(DT, 커널 인자, sysctl, systemd)이 새로 배울 부분
> **선행 노트**: A6 §8(시그널과 core dump), F10(reserved-memory·carveout), I9(watchdog·pretimeout), K1 §1(panic 직전 로그가 사라지는 이유), J3(dm-verity `panic_on_corruption`). 같은 모듈의 L1(journald·systemd), L4(크래시 루프 → bootcount 롤백)와 맞물린다. 로그 해독 자체는 M3(Oops 해독)·M9(gdb·core)에서 깊게 다룬다

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어 시절을 떠올려 보자. 고객사 서버에서 드라이브가 한 번 멈췄다가 살아났다. 그 드라이브를 RMA로 받기 전까지 손에 쥘 수 있는 건 **드라이브가 스스로 남긴 기록**뿐이었다. 그래서 펌웨어는 assert가 터지면 예약 SRAM/DRAM 영역에 레지스터와 최근 이벤트 링버퍼를 쓰고, 다음 부팅 때 그걸 NAND의 로그 블록으로 옮기고, 호스트는 vendor-specific log page로 읽어 갔다. 핵심은 세 단계였다: **죽는 순간 살아남는 곳에 쓴다 → 다음 부팅 때 영구 저장소로 옮긴다 → 밖으로 꺼낸다.**

Linux 장비도 똑같은 세 단계를 밟는다. 다만 "죽는 것"의 종류가 훨씬 다양하고, 단계마다 담당 메커니즘이 따로 있다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="100" y="20" font-size="13" text-anchor="middle">무엇이 죽었나</text>
<text x="560" y="20" font-size="13" text-anchor="middle">무엇이 증거를 잡나</text>
<rect x="10" y="32" width="190" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="105" y="57" font-size="13" text-anchor="middle">커널 oops / panic</text>
<rect x="10" y="84" width="190" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="105" y="109" font-size="13" text-anchor="middle">lockup · hung task</text>
<rect x="10" y="136" width="190" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="105" y="161" font-size="13" text-anchor="middle">HW watchdog 리셋</text>
<rect x="10" y="188" width="190" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="105" y="213" font-size="13" text-anchor="middle">유저 segfault / abort</text>
<rect x="10" y="240" width="190" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="105" y="265" font-size="13" text-anchor="middle">OOM kill</text>
<rect x="10" y="292" width="190" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="105" y="317" font-size="13" text-anchor="middle">조용한 정지 · 전원 차단</text>
<rect x="470" y="32" width="200" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="570" y="57" font-size="13" text-anchor="middle">pstore / ramoops</text>
<rect x="470" y="96" width="200" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="570" y="121" font-size="13" text-anchor="middle">kdump → vmcore</text>
<rect x="470" y="160" width="200" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="570" y="185" font-size="13" text-anchor="middle">reset reason 레지스터</text>
<rect x="470" y="224" width="200" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="570" y="249" font-size="13" text-anchor="middle">systemd-coredump</text>
<rect x="470" y="288" width="200" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="570" y="313" font-size="13" text-anchor="middle">journald + 원격 로그</text>
<line x1="200" y1="52" x2="468" y2="52" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="200" y1="56" x2="468" y2="112" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<line x1="200" y1="104" x2="468" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="200" y1="156" x2="468" y2="180" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="200" y1="152" x2="468" y2="64" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<line x1="200" y1="208" x2="468" y2="242" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="200" y1="260" x2="468" y2="304" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="200" y1="312" x2="468" y2="310" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="200" y1="318" x2="468" y2="186" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<text x="20" y="360" font-size="13">실선: 기본 경로. 점선: 설정을 해야 열리는 경로 (lockup을 panic으로, pretimeout을 panic으로).</text>
<text x="20" y="382" font-size="13">조용한 정지는 커널이 아무것도 못 쓴다 → 리셋 원인 + 마지막 원격 로그 + console-ramoops가 전부다.</text>
</svg>
```
그림 1 — 필드 크래시 종류와 증거를 잡는 메커니즘

세 단계를 Linux 용어로 다시 쓰면 이렇다.

| 단계 | SSD 펌웨어에서 | Linux 장비에서 | 이 노트 |
|---|---|---|---|
| 죽는 순간 기록 | assert 핸들러가 예약 DRAM에 덤프 | panic → kmsg_dump → **pstore/ramoops**, 또는 **kdump**로 두 번째 커널이 메모리 전체 보존, 유저는 커널이 **core**를 씀 | §3~§7 |
| 다음 부팅 때 영구화 | 부팅 시 DRAM 덤프를 NAND 로그 블록으로 | **systemd-pstore**가 `/sys/fs/pstore` → `/var/lib/systemd/pstore`, kdump 캡처 커널이 vmcore를 디스크로, **systemd-coredump**가 `/var/lib/systemd/coredump` | §5, §6, §7 |
| 밖으로 꺼냄 | vendor log page, telemetry log | **netconsole**(실시간 UDP), **systemd-journal-upload**, 크래시 파일 업로더(store-and-forward) | §8, §10 |

이 노트의 실습은 전부 VM 안의 QEMU `virt` 게스트에서 했다. 커널은 랩의 `Image-sec`(6.18.55-sec — PSTORE_RAM/CONSOLE/PMSG/FTRACE·KEXEC·CRASH_DUMP·NETCONSOLE 내장), 유저랜드는 BusyBox initramfs와 Ubuntu 24.04 + systemd 255 rootfs(`rootfs-sysd`)다. VM 호스트는 Ubuntu 26.04, `uname -r` = `7.0.0-38-generic`. QEMU는 TCG(에뮬레이션)라 **모든 시간 수치는 실제 하드웨어 수치가 아니다.**

---

## 1. 필드 크래시 분류 — 무엇이 죽었고, 무엇이 남나

### 1.1 직관 — "누가 마지막 말을 할 수 있나"

크래시 수집 설계는 결국 **"죽는 순간에 아직 살아서 기록을 남길 수 있는 코드가 누구냐"** 의 문제다.

- 유저 프로세스가 죽으면 커널은 멀쩡하다 → 커널이 그 프로세스 메모리를 core 파일로 써 줄 수 있다. 가장 쉬운 경우.
- 커널이 oops를 내면 커널은 다쳤지만 아직 돈다 → 자기 로그를 어딘가(ramoops) 쓰고 계속 가거나 panic한다.
- 커널이 panic하면 남은 일은 "최소한의 코드로 기록하고 리셋"뿐이다 → kmsg_dump(pstore)나, 미리 올려 둔 두 번째 커널(kdump)에 넘긴다.
- 커널이 인터럽트까지 막힌 채 멈추면(hard lockup) 커널은 아무것도 못 한다 → NMI 성격의 감시(arm64는 pseudo-NMI/버디 CPU)나 HW watchdog만 남는다.
- 전원이 나가거나 SoC가 통째로 멈추면 소프트웨어는 한 글자도 못 쓴다 → **리셋 원인 레지스터**와 **직전까지 밖으로 보낸 로그**만 남는다.

### 1.2 분류표

| 종류 | 겉으로 보이는 증상 | 누가 감지하나 | 남길 수 있는 증거 | 수집 장치 |
|---|---|---|---|---|
| 커널 oops | 프로세스 하나 죽고 커널 경고, 이후 이상 동작 | 커널 예외 핸들러(`die()`) | 레지스터·콜스택·모듈 목록 | dmesg, pstore(oops도 기본 기록), journald |
| 커널 panic | 리부트(`panic=N`) 또는 정지 | `panic()` | 마지막 kmsg, 필요하면 메모리 전체 | pstore dmesg 레코드, kdump vmcore |
| soft lockup | 한 CPU가 커널에서 20초+ 스케줄 안 됨 | watchdog hrtimer + `watchdog/N` | 해당 CPU 스택 | `softlockup_panic=1` → panic 경로 |
| hard lockup | 인터럽트 꺼진 채 멈춤 | perf NMI 또는 버디 CPU 감시 | 운 좋으면 스택 | `hardlockup_panic=1`, 없으면 HW WDT |
| hung task | D 상태 태스크 120초+ | khungtaskd | 막힌 태스크 스택 | `hung_task_panic=1` |
| HW watchdog 리셋 | 갑작스런 리부트, 로그 없음 | SoC watchdog | 리셋 원인 비트, pretimeout 시 panic 로그 | 부트로더가 읽는 reset cause, I9 pretimeout + pstore |
| 유저 segfault/abort | 서비스 재시작, 기능 일시 정지 | 커널(시그널 전달) | core 파일(메모리+레지스터) | systemd-coredump, core_pattern |
| OOM kill | 큰 프로세스가 사라짐 | OOM killer | "Out of memory: Killed process" 커널 로그 + 메모리 상태표 | journald, pstore console |
| 조용한 정지·전원 | 응답 없음, 결국 전원 재투입 | 사람, 외부 감시 | 거의 없음 | console-ramoops(warm reset일 때만), 원격 로그의 마지막 줄 |

표에서 중요한 점 두 가지:

1. **lockup·hung task는 기본이 "경고만 하고 계속"이다.** 제품에서 이걸 panic으로 바꾸지 않으면 장비는 반쯤 죽은 채로 HW watchdog을 기다리고, 리셋 후엔 아무 증거도 없다. 그래서 필드 장비는 보통 `softlockup_panic`, `hung_task_panic`, `panic_on_oops`를 켜고 `panic=N`으로 자동 리부트시킨다(§2).
2. **증거의 품질 차이가 크다.** 리셋 원인 비트 → 마지막 kmsg 수십 KiB(pstore) → 메모리 전체(kdump) 순으로 정보량이 늘고 비용도 는다.

### 1.3 예산 — 임베디드에서 무엇을 얼마나 남기나

SSD 펌웨어에서 덤프 영역 크기를 놓고 하드웨어 팀과 싸웠던 것처럼, 여기서도 예산이 설계를 정한다.

| 증거 | 대략 크기 | 드는 비용 | 판단 |
|---|---|---|---|
| reset cause + bootcount | 수 바이트 | 부트로더 코드 약간 | 무조건 |
| pstore dmesg (ramoops) | 레코드당 수십~수백 KiB, 전체 1 MiB 안팎 | RAM 1 MiB를 커널이 못 씀 | 거의 무조건 |
| console/pmsg ramoops | 수십~수백 KiB | 같은 예약 영역 안 | 권장 |
| 유저 core (압축) | 수백 KiB ~ 수십 MiB | 플래시 공간·쓰기 수명 | 크기 제한 걸고 켬 |
| kdump vmcore (필터 후) | 수십 MiB ~ 수백 MiB | `crashkernel=` 예약 RAM(수백 MiB) + 저장 공간 | 큰 장비(Jetson급)만 |
| 원격 로그 | 하루 수 KiB~MiB | 셀룰러 요금·배터리 | 레벨·속도 제한 걸고 |

---
## 2. 커널 panic 정책 — 증거를 남기고 반드시 리부트하게

### 2.1 왜 정책이 먼저인가

크래시 수집 장치를 아무리 잘 붙여도, 커널이 **panic까지 가지 않거나 panic 후 멈춰 있으면** 증거는 디스크에 도착하지 않는다. SSD 펌웨어에서 "assert → 덤프 → 자동 리셋"을 한 묶음으로 설계했던 것처럼, Linux에서도 "이상 감지 → panic → 기록 → 리부트"를 커널 인자와 sysctl로 묶는다.

### 2.2 설정 지점

| 커널 인자 (sysctl) | 기본값 | 의미 | 필드 장비 권장 |
|---|---|---|---|
| `panic=N` (`kernel.panic`) | `CONFIG_PANIC_TIMEOUT`(보통 0) | panic 후 N초 뒤 리부트. 0이면 영원히 멈춤, 음수면 즉시 | 3~10 |
| `oops=panic` (`kernel.panic_on_oops`) | 0 | oops를 곧바로 panic으로 | 1 (반쯤 죽은 커널로 계속 돌지 않게) |
| `kernel.oops_limit` | 10000 | oops가 이 횟수를 넘으면 panic | 기본 유지 |
| `softlockup_panic=1` (`kernel.softlockup_panic`) | 0 | soft lockup 감지 시 panic | 1 |
| `nmi_watchdog=panic` (`kernel.hardlockup_panic`) | 0 | hard lockup 감지 시 panic | 1 (감지기가 있을 때) |
| `sysctl.kernel.hung_task_panic=1` (`kernel.hung_task_panic`) | 0 | D 상태 120초(기본) 초과 시 panic | 제품 성격에 따라 |
| `panic_on_warn=1` | 0 | WARN도 panic으로 | 개발·퍼징용, 필드에서는 보통 끔 |
| `panic_sys_info=` (`kernel.panic_sys_info`), `panic_console_replay` | 끔 | panic 때 태스크·메모리·락 정보 등을 추가 출력 | 필요한 항목만 |
| `crash_kexec_post_notifiers` | 끔 | kdump 전에 panic notifier·kmsg_dump(pstore)를 먼저 돌림 | kdump와 pstore를 같이 쓸 때 고려(§6.5) |

커널 인자 전용 이름이 없는 sysctl도 `sysctl.<이름>=<값>` 형식으로 커널 명령줄에 줄 수 있다(5.8부터). 6.18에서는 예전의 `panic_print` 비트마스크가 deprecated 되고 `panic_sys_info`(문자열 목록)와 `panic_console_replay`로 나뉘었다. 커널 소스 `kernel/panic.c`에 "The 'panic_print' parameter is now deprecated" 메시지가 들어 있다. 버전마다 이름이 달라지는 영역이라 제품 커널 버전의 `Documentation/admin-guide/sysctl/kernel.rst`를 확인해야 한다.

또 하나: lockup·hung task 감지기는 **커널 설정에 들어 있어야** 동작한다. 랩의 `Image-sec`는 `CONFIG_SOFTLOCKUP_DETECTOR`·`CONFIG_DETECT_HUNG_TASK`가 꺼져 있다(디버그 커널 `Image-dbg`에는 켜져 있다). 제품 커널 config 리뷰 때 체크리스트에 넣어야 하는 항목이다.

### 2.3 panic() 안에서 일어나는 순서

`kernel/panic.c`의 `vpanic()`이 하는 일을 순서대로 보면, 크래시 수집 장치들이 어디에 끼어드는지 보인다(6.18 소스 기준, 단순화).

```text
vpanic(fmt, ...)
  pr_emerg("Kernel panic - not syncing: ...")   + 스택 덤프
  kgdb_panic()                                   (kgdb가 있으면 여기서 디버거 진입, M4)
  if (!crash_kexec_post_notifiers)
      __crash_kexec(NULL)                        <-- kdump가 로드돼 있으면 여기서 캡처 커널로 점프, 이후는 실행 안 됨
  panic_other_cpus_shutdown()                    (다른 CPU 정지)
  atomic_notifier_call_chain(&panic_notifier_list)
  sys_info(panic_print)
  kmsg_dump_desc(KMSG_DUMP_PANIC, buf)           <-- pstore/ramoops가 여기서 dmesg 레코드를 씀
  if (crash_kexec_post_notifiers)
      __crash_kexec(NULL)
  console_unblank(), console_flush_on_panic()
  if (panic_timeout > 0) -> N초 대기 후 emergency_restart()   (arm64 virt: PSCI SYSTEM_RESET)
  else 영원히 루프 (panic_blink)
```

여기서 면접에 나오는 함정이 하나 나온다. **kdump가 로드돼 있으면 기본 설정에서는 pstore dmesg 레코드가 써지지 않는다.** `__crash_kexec()`가 `kmsg_dump()`보다 먼저 불리고 돌아오지 않기 때문이다. 둘 다 원하면 `crash_kexec_post_notifiers`를 켜야 하는데, 이미 망가진 커널에서 notifier를 더 돌리는 만큼 kdump 실패 위험이 조금 늘어난다(소스 주석이 직접 그렇게 경고한다). §6.5에서 실제로 확인한다.

---

## 3. pstore — "죽어도 남는 저장소"를 파일시스템으로 보여 주는 층

### 3.1 직관

pstore(persistent store)는 SSD 펌웨어의 "덤프 영역 + 다음 부팅 때 읽는 API"를 일반화한 커널 프레임워크다. 두 반쪽으로 나뉜다.

- **frontend** — 무엇을 남길지: panic/oops 때의 커널 로그(`dmesg`), 콘솔 출력 전체(`console`), 유저가 직접 쓰는 메시지(`pmsg`, `/dev/pmsg0`), 함수 트레이스(`ftrace`).
- **backend** — 어디에 남길지: 예약 RAM(`ramoops`), UEFI 변수(`efi-pstore`), 블록 장치(`pstore/blk`), MTD 플래시(`mtdpstore`), ACPI ERST(서버).

다음 부팅 때 backend가 남아 있는 레코드를 읽어 **`/sys/fs/pstore/` 아래 파일**로 보여 준다. 파일을 지우면(`rm`) backend에서도 지워진다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="90" y="20" font-size="13" text-anchor="middle">frontend (무엇을)</text>
<rect x="10" y="32" width="160" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="90" y="52" font-size="13" text-anchor="middle">kmsg_dump</text>
<text x="90" y="68" font-size="12" text-anchor="middle">panic·oops 로그</text>
<rect x="10" y="88" width="160" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="90" y="108" font-size="13" text-anchor="middle">console</text>
<text x="90" y="124" font-size="12" text-anchor="middle">printk 전체 스트림</text>
<rect x="10" y="144" width="160" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="90" y="164" font-size="13" text-anchor="middle">pmsg</text>
<text x="90" y="180" font-size="12" text-anchor="middle">유저 /dev/pmsg0</text>
<rect x="10" y="200" width="160" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="90" y="220" font-size="13" text-anchor="middle">ftrace</text>
<text x="90" y="236" font-size="12" text-anchor="middle">함수 호출 기록</text>
<rect x="250" y="100" width="170" height="80" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="335" y="128" font-size="14" text-anchor="middle">pstore core</text>
<text x="335" y="148" font-size="12" text-anchor="middle">fs/pstore/platform.c</text>
<text x="335" y="166" font-size="12" text-anchor="middle">backend는 한 번에 하나</text>
<text x="590" y="20" font-size="13" text-anchor="middle">backend (어디에)</text>
<rect x="500" y="32" width="170" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="585" y="52" font-size="13" text-anchor="middle">ramoops</text>
<text x="585" y="68" font-size="12" text-anchor="middle">예약 RAM (임베디드 주력)</text>
<rect x="500" y="88" width="170" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="585" y="108" font-size="13" text-anchor="middle">efi-pstore</text>
<text x="585" y="124" font-size="12" text-anchor="middle">UEFI 변수 (NVRAM)</text>
<rect x="500" y="144" width="170" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="585" y="164" font-size="13" text-anchor="middle">pstore/blk</text>
<text x="585" y="180" font-size="12" text-anchor="middle">블록 장치 panic write</text>
<rect x="500" y="200" width="170" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="585" y="220" font-size="13" text-anchor="middle">mtdpstore</text>
<text x="585" y="236" font-size="12" text-anchor="middle">NOR/NAND 파티션</text>
<line x1="170" y1="54" x2="248" y2="120" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="170" y1="110" x2="248" y2="132" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="170" y1="166" x2="248" y2="148" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="170" y1="222" x2="248" y2="162" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="420" y1="120" x2="498" y2="56" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="420" y1="132" x2="498" y2="110" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
<line x1="420" y1="148" x2="498" y2="164" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
<line x1="420" y1="160" x2="498" y2="220" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
<line x1="335" y1="180" x2="335" y2="268" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="140" y="270" width="390" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="335" y="290" font-size="13" text-anchor="middle">다음 부팅: /sys/fs/pstore/</text>
<text x="335" y="308" font-size="12" text-anchor="middle">dmesg-ramoops-0, console-ramoops-0, pmsg-ramoops-0</text>
</svg>
```
그림 2 — pstore의 frontend · core · backend 구조

### 3.2 backend 비교

| backend | 저장 매체 | 언제 써지나 | 콜드 전원 차단 후 | 임베디드에서 |
|---|---|---|---|---|
| ramoops | DT/커널 인자로 지정한 예약 DRAM(또는 SRAM) | 즉시(메모리 쓰기) | **사라진다** (DRAM은 전원 없으면 지워짐) | 주력. 가장 단순하고 panic 경로에서 가장 안전 |
| efi-pstore | UEFI 변수 저장소(SPI NOR 등) | panic 때 EFI runtime service 호출 | 남는다 | UEFI 부팅 장비(x86, 일부 arm64 서버·Jetson UEFI) |
| pstore/blk | 지정한 블록 장치 파티션 | panic 때 드라이버의 panic write 경로 | 남는다 | 드라이버가 panic-safe write를 지원해야 함(제한적) |
| mtdpstore | MTD 파티션(NOR/NAND) | panic 때 MTD panic_write | 남는다 | NOR가 있으면 좋은 선택. NAND는 erase 블록 관리가 필요 |
| ERST | ACPI 펌웨어 영역 | 펌웨어 호출 | 남는다 | 서버급 |

Don에게 익숙한 비유로 정리하면: ramoops = "예약 DRAM 덤프", mtdpstore·pstore/blk = "NAND 로그 블록에 직접 쓰는 panic writer", efi-pstore = "펌웨어 NVRAM에 맡기기". 차이는 **panic 순간에 그 쓰기가 얼마나 안전하냐**다. 메모리 쓰기는 락도 인터럽트도 필요 없지만, 블록·MTD 쓰기는 컨트롤러가 살아 있어야 하고 드라이버가 폴링 기반 panic 경로를 따로 가져야 한다. SSD에서도 "assert 핸들러에서 NAND program을 직접 하느냐, 일단 DRAM에 두고 다음 부팅에서 옮기느냐"를 고민했던 것과 같은 축이다.

pstore core는 **한 번에 backend 하나만** 등록받는다. ramoops와 efi-pstore가 둘 다 있으면 먼저 등록한 쪽만 쓰인다(`pstore.backend=ramoops`로 고를 수 있다).

---
## 4. ramoops — 예약 DRAM에 남기는 마지막 기록

### 4.1 정의와 설정 방법

ramoops(`fs/pstore/ram.c`, `ram_core.c`)는 **물리 주소가 고정된 RAM 영역**을 몇 개의 링버퍼(PRZ, persistent RAM zone)로 쪼개 pstore backend로 쓴다. 영역을 알려 주는 방법은 셋이다.

| 방법 | 예 | 언제 |
|---|---|---|
| DT `reserved-memory` 노드 (`compatible = "ramoops"`) | 아래 §4.2 | ARM 임베디드 표준. 메모리 예약과 설정이 한 곳에 |
| 커널 인자 / 모듈 파라미터 | `ramoops.mem_address=0x7fe00000 ramoops.mem_size=0x100000 ramoops.record_size=0x20000` | DT를 못 고칠 때. 이때는 `memmap=`/`mem=` 등으로 그 영역을 따로 비워야 한다 |
| 플랫폼 데이터 (보드 코드) | `struct ramoops_platform_data` | 옛 보드 파일, x86 Chromebook 등 |

DT 바인딩(`Documentation/devicetree/bindings/reserved-memory/ramoops.yaml`)의 주요 속성:

| 속성 | 뜻 | 이 노트 값 |
|---|---|---|
| `reg` | 영역의 물리 주소·크기 | `0x7fe00000`, 1 MiB |
| `record-size` | panic/oops dmesg 레코드 하나 크기. 남은 공간 ÷ 이 값 = 레코드 개수 | 128 KiB |
| `console-size` | 콘솔 스트림 링버퍼 | 256 KiB |
| `pmsg-size` | 유저 `/dev/pmsg0` 링버퍼 | 128 KiB |
| `ftrace-size` | 함수 트레이스 (CONFIG_PSTORE_FTRACE 필요) | 128 KiB |
| `ecc-size` | 블록마다 붙일 Reed-Solomon ECC 바이트 수. 0이면 ECC 없음 | 16 |
| `max-reason` | 어느 kmsg_dump 이유까지 기록하나 (1=PANIC, 2=OOPS(기본), 3=EMERG, 4=SHUTDOWN) | 기본 |
| `mem-type` | 매핑 속성 0=write-combine(기본), 1=uncached, 2=cached | 기본 |
| `unbuffered` (bool) | `mem-type = 1`의 옛 표기 | — |

`mem-type`이 왜 있나? panic 순간에 캐시에 남은 데이터는 리셋과 함께 사라질 수 있다. 기본 write-combine 매핑은 캐시를 거치지 않으므로(쓰기가 버퍼에 모였다가 곧 DRAM으로 나감) 리셋 너머로 살아남을 확률이 높다. SSD 펌웨어에서 덤프 영역을 non-cacheable로 잡거나 덤프 후 D-cache clean을 했던 것과 같은 이유다.

### 4.2 메모리 레이아웃

`ramoops_probe()`는 영역을 **dmesg 레코드들 → console → pmsg → ftrace** 순서로 앞에서부터 잘라 쓴다(`fs/pstore/ram.c`). dmesg 몫은 "전체 − console − ftrace − pmsg"이고 그걸 `record-size`로 나눈 만큼 레코드가 생긴다. 이 노트 설정이면 1 MiB − 256 − 128 − 128 KiB = 512 KiB → 128 KiB 레코드 **4개**(`dmesg-ramoops-0` … `-3`). 각 zone 앞에는 12바이트 헤더(`struct persistent_ram_buffer`: `sig`, `start`, `size`)가 있고, ECC를 켜면 zone 끝에 패리티가 붙는다.

```
phys addr     zone                     size     header sig
0x7fe00000    dmesg record 0           128 KiB  0x43474244 ("DBGC")
0x7fe20000    dmesg record 1           128 KiB
0x7fe40000    dmesg record 2           128 KiB
0x7fe60000    dmesg record 3           128 KiB
0x7fe80000    console ring             256 KiB  0x43474244
0x7fec0000    pmsg ring                128 KiB  0x43474244
0x7fee0000    ftrace                   128 KiB  0x43474244 ^ LINUX_VERSION_CODE
0x7ff00000    (end of ramoops, 1 MiB)

each zone: [sig u32][start u32][size u32][data ... ][ECC parity if ecc-size>0]
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">1 GiB 게스트 RAM의 끝 1 MiB (0x7fe00000 ~ 0x7fefffff)</text>
<rect x="20" y="40" width="80" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="60" y="66" font-size="12" text-anchor="middle">dmesg 0</text>
<text x="60" y="84" font-size="12" text-anchor="middle">128K</text>
<rect x="100" y="40" width="80" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="140" y="66" font-size="12" text-anchor="middle">dmesg 1</text>
<text x="140" y="84" font-size="12" text-anchor="middle">128K</text>
<rect x="180" y="40" width="80" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="220" y="66" font-size="12" text-anchor="middle">dmesg 2</text>
<text x="220" y="84" font-size="12" text-anchor="middle">128K</text>
<rect x="260" y="40" width="80" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="300" y="66" font-size="12" text-anchor="middle">dmesg 3</text>
<text x="300" y="84" font-size="12" text-anchor="middle">128K</text>
<rect x="340" y="40" width="160" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="420" y="66" font-size="12" text-anchor="middle">console 링버퍼</text>
<text x="420" y="84" font-size="12" text-anchor="middle">256K</text>
<rect x="500" y="40" width="80" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="540" y="66" font-size="12" text-anchor="middle">pmsg</text>
<text x="540" y="84" font-size="12" text-anchor="middle">128K</text>
<rect x="580" y="40" width="80" height="60" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="620" y="66" font-size="12" text-anchor="middle">ftrace</text>
<text x="620" y="84" font-size="12" text-anchor="middle">128K</text>
<text x="20" y="118" font-size="12">0x7fe00000</text>
<text x="340" y="118" font-size="12">0x7fe80000</text>
<text x="500" y="118" font-size="12">0x7fec0000</text>
<text x="598" y="118" font-size="12">0x7fee0000</text>
<text x="20" y="160" font-size="13">zone 하나의 내부 (struct persistent_ram_buffer)</text>
<rect x="20" y="176" width="80" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="60" y="203" font-size="12" text-anchor="middle">sig</text>
<rect x="100" y="176" width="80" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="140" y="203" font-size="12" text-anchor="middle">start</text>
<rect x="180" y="176" width="80" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="220" y="203" font-size="12" text-anchor="middle">size</text>
<rect x="260" y="176" width="300" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="410" y="203" font-size="12" text-anchor="middle">data[] — 링버퍼 (start에서 이어 씀)</text>
<rect x="560" y="176" width="100" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="610" y="203" font-size="12" text-anchor="middle">ECC 패리티</text>
<text x="20" y="250" font-size="13">sig가 맞으면 "지난 부팅의 기록"으로 읽고, 틀리면 새 영역으로 보고 지운다.</text>
<text x="20" y="272" font-size="13">ECC를 켜면 블록마다 RS 패리티로 비트 오류를 고친다 (DRAM 리텐션 중 비트 플립 대비).</text>
<text x="20" y="292" font-size="13">ftrace zone은 sig에 커널 버전을 XOR해 둔다 — 다른 커널의 트레이스를 잘못 읽지 않게.</text>
</svg>
```
그림 3 — 이 노트의 ramoops 1 MiB 레이아웃과 zone 내부 구조

SSD 펌웨어의 덤프 헤더와 똑같다: 매직(`DBGC`) + 쓰기 위치 + 길이. 매직이 없으면 "쓰레기 RAM"으로 보고 초기화한다. 다른 점은 dmesg 레코드는 panic 때 **한 번에 통째로** 쓰이는 고정 슬롯(여러 번 panic하면 0→1→2→3 순환)이고, console·pmsg는 **평소에 계속** 이어 쓰는 링버퍼라는 것이다.

### 4.3 RAM은 언제 살아남나 — warm reset만

ramoops의 전제는 **"리셋해도 DRAM 내용이 그대로 있다"** 이다. 이게 성립하는 조건이 생각보다 좁다.

| 리셋 종류 | DRAM 내용 | ramoops |
|---|---|---|
| 소프트웨어 리부트 (`reboot`, panic → `emergency_restart`) | 대개 유지 (DRAM은 refresh만 계속되면 된다) | 동작 |
| SoC 내부 watchdog 리셋 | 대개 유지 (SoC 리셋 도메인에 따라 다름) | 보통 동작. DDR 컨트롤러까지 리셋되면 self-refresh 처리가 관건 |
| 외부 PMIC 리셋 / 전원 차단 후 재투입 | **사라진다** (수십 ms~수 초면 대부분 비트가 날아감, 온도에 따라 다름) | 동작 안 함 |
| 부트로더가 그 영역을 건드림 | 덮어써짐 | 동작 안 함 (가장 흔한 실패 원인) |

실무에서 확인할 것:

1. **DDR 초기화가 내용을 지우지 않는가.** 일부 SoC의 DDR training은 메모리 패턴 테스트로 DRAM 일부를 덮어쓴다. warm reset 경로에서 training을 건너뛰거나(저장된 training 결과 재사용) DRAM을 self-refresh에 둔 채 컨트롤러만 리셋하는 기능이 있는지 SoC 문서를 본다.
2. **부트로더·TF-A·OP-TEE가 그 주소를 쓰지 않는가.** U-Boot의 relocation 주소, `fdt_addr`, `kernel_addr_r`, 압축 해제 버퍼가 DRAM 끝에 몰려 있는 경우가 많다. ramoops를 DRAM 끝에 두었다면 U-Boot가 자기 자신을 DRAM 끝으로 relocate하는지 확인해야 한다(U-Boot는 DT의 reserved-memory를 보고 피할 수도 있지만 보장되지 않는다).
3. **커널 자신이 그 영역을 할당자에 주지 않는가.** `reserved-memory` 노드가 있으면 memblock에서 빠진다(F10). 커널 인자 방식이면 `mem=`/`memmap=`으로 직접 빼야 한다.
4. **콜드 부팅 직후의 쓰레기 값.** 전원 투입 직후 DRAM은 무작위 값이라 sig가 우연히 맞을 확률은 무시할 만하지만, ECC가 없으면 "반쯤 망가진 로그"를 읽을 수 있다. 그래서 `ecc-size`를 켠다.

QEMU `virt`는 어떨까? `system_reset`(PSCI SYSTEM_RESET 포함)은 **게스트 RAM을 지우지 않고** CPU·장치만 리셋한 뒤 `-kernel`/`-initrd`/DTB를 다시 적재한다. 새 QEMU 프로세스를 띄우는 것은 콜드 부팅(RAM 0으로 시작)이다. 아래 예제들로 둘 다 확인한다.

### 예제 1 — QEMU virt DTB를 덤프해서 ramoops 노드를 넣는다

무엇을 확인하나: 게스트와 **같은 `-m`/`-smp`/`-cpu`** 로 QEMU 자신의 DTB를 뽑고, `reserved-memory` 안에 ramoops 노드를 넣어 다시 컴파일한다(D6·F10에서 한 방식).

```sh
cd /var/tmp/L7
qemu-system-aarch64 -machine virt,dumpdtb=l7_virt.dtb -cpu cortex-a57 -smp 2 -m 1G -nographic
dtc -I dtb -O dts -o l7_virt.dts l7_virt.dtb 2>/dev/null
grep -n -A1 "memory@" l7_virt.dts
# ramoops_node.dtsi 를 "aliases {" 앞에 끼워 넣는다
awk -v f=ramoops_node.dtsi '/^\taliases \{/{while((getline l < f)>0) print l} {print}' l7_virt.dts > l7_ramoops.dts
dtc -q -I dts -O dtb -o l7_ramoops.dtb l7_ramoops.dts
dtc -I dtb -O dts l7_ramoops.dtb 2>/dev/null | grep -A10 "ramoops@"
```

끼워 넣은 노드:

```dts
	reserved-memory {
		#address-cells = <2>;
		#size-cells = <2>;
		ranges;

		l7_ramoops: ramoops@7fe00000 {
			compatible = "ramoops";
			reg = <0x0 0x7fe00000 0x0 0x100000>;	/* 1 MiB at top of 1 GiB */
			record-size = <0x20000>;		/* 128 KiB per dmesg record */
			console-size = <0x40000>;		/* 256 KiB console log */
			pmsg-size = <0x20000>;			/* 128 KiB userspace /dev/pmsg0 */
			ftrace-size = <0x20000>;		/* 128 KiB function trace */
			ecc-size = <16>;			/* Reed-Solomon ECC bytes per block */
		};
	};
```

```text
20:	memory@40000000 {
21-		reg = <0x00 0x40000000 0x00 0x40000000>;
		ramoops@7fe00000 {
			compatible = "ramoops";
			reg = <0x00 0x7fe00000 0x00 0x100000>;
			record-size = <0x20000>;
			console-size = <0x40000>;
			pmsg-size = <0x20000>;
			ftrace-size = <0x20000>;
			ecc-size = <0x10>;
		};
	};
```

출력에서 볼 것: RAM은 `0x40000000`부터 1 GiB라서 끝은 `0x80000000`이다. ramoops를 그 직전 1 MiB에 두었다. `-q`를 빼면 dtc가 `clocks_property`·`gpios_property` 경고를 낸다. 이건 QEMU가 만든 DTB를 역컴파일하면서 phandle이 숫자로 풀려서 생기는 것으로 원래 DTB에서도 똑같이 나오며 무시해도 된다. **`-smp`나 `-m`을 바꾸면 DTB를 다시 덤프해야 한다**(cpu 노드·memory 노드가 바뀐다 — 랩 README의 주의 사항).

### 예제 2 — panic → warm reset → 다음 부팅에서 dmesg·pmsg·console 읽기

무엇을 확인하나: BusyBox initramfs의 `LAB_CMD`로 2단계 스크립트를 돌린다. 1차 부팅은 pstore가 비어 있으니 pmsg에 앱 상태를 쓰고 `/dev/kmsg`에 표시를 남긴 뒤 `echo c > /proc/sysrq-trigger`로 panic, `panic=3`이 3초 뒤 리셋한다. 2차 부팅(같은 QEMU 프로세스, warm reset)은 pstore에 레코드가 있으니 내용을 출력하고 끝낸다.

```sh
#!/bin/sh
# /l7.sh — boot 1 writes + crashes, boot 2 (after warm reset) reads
mount -t pstore pstore /sys/fs/pstore
if [ -z "$(ls /sys/fs/pstore)" ]; then
  echo "=== L7 boot 1: pstore empty"
  dmesg | grep -i -E "ramoops|pstore"
  grep -i -B1 -A1 "7fe00000" /proc/iomem
  echo "L7 pmsg: sensor-daemon state=ARMED seq=42 (written before crash)" > /dev/pmsg0
  echo "L7 marker: about to crash at uptime $(cut -d' ' -f1 /proc/uptime)" > /dev/kmsg
  echo c > /proc/sysrq-trigger
else
  echo "=== L7 boot 2: pstore has records"
  ls -l /sys/fs/pstore
  echo "--- dmesg-ramoops-0 (head)"; head -5 /sys/fs/pstore/dmesg-ramoops-0
  echo "--- dmesg-ramoops-0 (crash lines)"
  grep -E "L7 marker|sysrq|Kernel panic" /sys/fs/pstore/dmesg-ramoops-0
  echo "--- pmsg-ramoops-0"; cat /sys/fs/pstore/pmsg-ramoops-0
  echo "--- console-ramoops-0 (tail)"; tail -4 /sys/fs/pstore/console-ramoops-0
fi
```

```sh
cp -a /var/lab/build/rootfs l7_rootfs && cp l7.sh l7_rootfs/ && chmod +x l7_rootfs/l7.sh
(cd l7_rootfs && find . | cpio -o -H newc --quiet | gzip) > l7_initrd.gz
time timeout 240 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -nographic \
  -kernel /var/lab/out/Image-sec -initrd l7_initrd.gz -dtb l7_ramoops.dtb \
  -append "console=ttyAMA0 panic=3 LAB_CMD=sh~/l7.sh" > l7_ex_ramoops.log 2>&1
```

1차 부팅 (핵심 줄만, 콜스택 일부 생략):

```text
[    0.000000] OF: reserved mem: 0x000000007fe00000..0x000000007fefffff (1024 KiB) map non-reusable ramoops@7fe00000
[    0.168993] pstore: Using crash dump compression: deflate
[    0.169298] printk: legacy console [ramoops-1] enabled
[    0.177353] pstore: Registered ramoops as persistent store backend
[    0.177440] ramoops: using 0x100000@0x7fe00000, ecc: 16
...
=== L7 boot 1: pstore empty
  7fd12000-7fdfffff : reserved
  7fe00000-7fefffff : reserved
  7ff00000-7fffffff : reserved
[    1.425341] L7 marker: about to crash at uptime 1.35
[    1.426239] sysrq: Trigger a crash
[    1.426456] Kernel panic - not syncing: sysrq triggered crash
[    1.427266] CPU: 0 UID: 0 PID: 75 Comm: sh Not tainted 6.18.55-sec #1 PREEMPT
...
[    1.428381]  sysrq_handle_crash+0x1c/0x20
[    1.428442]  __handle_sysrq+0x94/0x1a0
[    1.428497]  write_sysrq_trigger+0xb0/0xe0
...
[    1.429262] SMP: stopping secondary CPUs
[    1.430384] Kernel Offset: 0x46b569000000 from 0xffff800080000000
[    1.437592] Rebooting in 3 seconds..
[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
```

2차 부팅 (같은 QEMU 프로세스):

```text
=== L7 boot 2: pstore has records
total 0
-r--r--r--    1 0        0            19685 Oct 18 04:27 console-ramoops-0
-r--r--r--    1 0        0            21100 Oct 18 04:27 dmesg-ramoops-0
-r--r--r--    1 0        0               90 Oct 18 04:27 pmsg-ramoops-0
--- dmesg-ramoops-0 (head)
Panic#1 Part1
<6>[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
<5>[    0.000000] Linux version 6.18.55-sec (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, ...
<6>[    0.000000] KASLR enabled
<5>[    0.000000] random: crng init done
--- dmesg-ramoops-0 (crash lines)
<12>[    1.425341] L7 marker: about to crash at uptime 1.35
<6>[    1.426239] sysrq: Trigger a crash
<0>[    1.426456] Kernel panic - not syncing: sysrq triggered crash
<4>[    1.428381]  sysrq_handle_crash+0x1c/0x20
<4>[    1.428442]  __handle_sysrq+0x94/0x1a0
<4>[    1.428497]  write_sysrq_trigger+0xb0/0xe0
--- pmsg-ramoops-0
L7 pmsg: sensor-daemon state=ARMED seq=42 (written before crash)

ECC: No errors detected
--- console-ramoops-0 (tail)
[    1.430753] Memory Limit: none
[    1.437592] Rebooting in 3 seconds..

ECC: No errors detected
```

출력에서 볼 것:

- 두 부팅 전체가 **6.7초**(TCG)에 끝났다. QEMU `virt`의 PSCI 리셋은 RAM을 보존한다는 것이 이것으로 확인된다 — 같은 프로세스 안의 2차 부팅이 1차 부팅의 기록을 읽었다.
- `dmesg-ramoops-0` 첫 줄 `Panic#1 Part1`: pstore가 붙이는 헤더. 이유(Panic/Oops), 몇 번째 덤프인지, 레코드가 커서 쪼개졌다면 몇 번째 조각인지. `<6>`·`<0>`은 printk 레벨(`<0>`=KERN_EMERG)이고 `<12>`는 `/dev/kmsg`로 쓴 유저 메시지(facility user=1 → 1×8+4)다.
- 레코드 파일은 21100바이트인데 128 KiB 슬롯에 들어갔다. `pstore: Using crash dump compression: deflate` — dmesg 레코드는 **압축해서** 쓰고 읽을 때 풀어서 보여 준다. 부팅부터 panic까지 전체 로그가 한 레코드에 들어간 이유다.
- `pmsg`·`console` 파일 끝의 `ECC: No errors detected` — ECC를 켰기 때문에 붙는 줄. 리텐션 중 비트가 뒤집혔다면 고친 개수가 찍힌다.
- `printk: legacy console [ramoops-1] enabled` — console frontend는 **진짜 콘솔 드라이버로 등록된다.** 그래서 `quiet`나 `loglevel=`로 콘솔 레벨을 낮추면 console-ramoops에도 그만큼 덜 남는다(예제 3에서 실제로 걸렸다).

### 예제 3 — panic 없는 "조용한 정지": console·pmsg만 남는다

무엇을 확인하나: 1차 부팅이 로그를 남긴 뒤 `sleep`으로 멈춘다(커널은 멀쩡, 앱만 멈춘 상황). VM 쪽에서 QEMU monitor로 ramoops 영역의 헤더를 직접 읽고(`xp`), `system_reset`을 보내 HW watchdog bite를 흉내 낸다. 2차 부팅에서 무엇이 남았나 본다.

```sh
#!/bin/bash
# run_hang.sh — boot 1 logs then hangs; host reads ramoops headers, then warm-resets
cd /var/tmp/L7; rm -f l7_mon.sock l7_hang.log
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -nographic \
  -monitor unix:l7_mon.sock,server,nowait -serial file:l7_hang.log \
  -kernel /var/lab/out/Image-sec -initrd l7_initrd.gz -dtb l7_ramoops.dtb \
  -append "console=ttyAMA0 LAB_CMD=sh~/l7h.sh" </dev/null >/dev/null 2>&1 &
QP=$!
until grep -q "hang boot 1" l7_hang.log 2>/dev/null; do sleep 1; done
sleep 3
for a in 0x7fe00000 0x7fe80000 0x7fec0000 0x7fee0000; do
  echo "xp /4xw $a" | socat - UNIX-CONNECT:l7_mon.sock | tr -d "\r" | grep "^0000"
done
echo "host: sending system_reset (simulated watchdog bite)"
echo system_reset | socat - UNIX-CONNECT:l7_mon.sock >/dev/null
wait $QP; echo "qemu rc=$?"
tr -d '\r' < l7_hang.log | grep -E "=== L7|L7 |pmsg|console|ramoops|reboot"
```

게스트 쪽 `l7h.sh`는 1차 부팅에서 `echo "L7 pmsg: control-loop heartbeat 1017, entering calibration" > /dev/pmsg0`, `echo "L7 marker: last words before silent hang" > /dev/kmsg` 후 `sleep 9999`, 2차 부팅에서 `/sys/fs/pstore`를 나열하고 pmsg와 console에서 표시 줄을 찾는다.

```text
000000007fe00000: 0x43474244 0x00000000 0x00000000 0x00000000
000000007fe80000: 0x43474244 0x0000479d 0x0000479d 0x2020205b
000000007fec0000: 0x43474244 0x0000003b 0x0000003b 0x7020374c
000000007fee0000: 0x43415073 0x00000000 0x00000000 0x00000000
host: sending system_reset (simulated watchdog bite)
qemu rc=0
[    0.000000] OF: reserved mem: 0x000000007fe00000..0x000000007fefffff (1024 KiB) map non-reusable ramoops@7fe00000
[    0.000000] Kernel command line: console=ttyAMA0 LAB_CMD=sh~/l7h.sh
[    0.347208] pstore: Registered ramoops as persistent store backend
[    0.347375] ramoops: using 0x100000@0x7fe00000, ecc: 16
=== L7 hang boot 1
[    3.112821] L7 marker: last words before silent hang
[    0.000000] OF: reserved mem: 0x000000007fe00000..0x000000007fefffff (1024 KiB) map non-reusable ramoops@7fe00000
[    0.000000] Kernel command line: console=ttyAMA0 LAB_CMD=sh~/l7h.sh
[    0.365942] pstore: Registered ramoops as persistent store backend
[    0.366125] ramoops: using 0x100000@0x7fe00000, ecc: 16
=== L7 hang boot 2
console-ramoops-0
pmsg-ramoops-0
--- pmsg
L7 pmsg: control-loop heartbeat 1017, entering calibration
--- console tail
[    3.112821] L7 marker: last words before silent hang
[    3.035958] reboot: Power down
```

출력에서 볼 것:

- monitor `xp`로 본 헤더가 그림 3과 그대로 맞는다. 네 zone 모두 sig `0x43474244`("DBGC")로 시작한다. dmesg 슬롯(`0x7fe00000`)은 panic이 없었으니 `start=size=0`. console은 `0x479d`(18333)바이트가 쌓였고 데이터가 `0x2020205b` = `"[   "`(타임스탬프 시작)로 시작한다. pmsg는 `0x3b`=59바이트 = 쓴 메시지 길이와 같다.
- ftrace zone의 sig `0x43415073`은 `0x43474244 ^ 0x061237`이다. `0x061237`은 `LINUX_VERSION_CODE`(6.18.55)라서, 그림 3에서 말한 "커널 버전을 XOR한 sig"가 실제 메모리에서 보인다. 이 커널은 `CONFIG_PSTORE_FTRACE`가 꺼져 있어(FUNCTION_TRACER 없음) zone은 잡혀 있어도 기록은 없다.
- 2차 부팅에는 **dmesg 레코드가 없다**(panic·oops가 없었으니 kmsg_dump가 안 불렸다). 대신 console-ramoops에 마지막 콘솔 줄, pmsg에 앱이 남긴 마지막 상태가 있다. **"watchdog으로 리셋됐는데 로그가 없다"는 필드 사례에서 console·pmsg ramoops가 거의 유일한 단서**가 되는 이유다.
- 함정 하나: 처음엔 `quiet`를 붙여 돌렸더니 console-ramoops 파일이 아예 안 생겼다. `quiet`는 콘솔 레벨을 4로 낮추는데, `/dev/kmsg`로 쓴 줄은 레벨 4(warning)라 콘솔에 안 나가고, 따라서 console-ramoops에도 안 들어간다. 제품에서 부팅 로그를 줄이려고 `quiet`를 쓰면 console-ramoops의 가치도 같이 떨어진다 — `pmsg`나 dmesg 레코드는 이 영향을 받지 않는다.

### 예제 4 — 콜드 재시작(새 QEMU 프로세스)에서는 아무것도 없다

무엇을 확인하나: 예제 2 직후 새 QEMU 프로세스로 읽기 전용 스크립트를 돌린다. 새 프로세스 = 새로 0으로 채운 RAM = 전원 재투입. 덤으로 ramoops가 DT에서 읽은 설정을 `/sys/module/ramoops/parameters`로 확인한다.

```sh
#!/bin/sh
# /l7r.sh — read-only: what does pstore hold right now?
mount -t pstore pstore /sys/fs/pstore
echo "=== L7 read-only boot"
echo "pstore files: $(ls /sys/fs/pstore | wc -l)"; ls /sys/fs/pstore | cat
cd /sys/module/ramoops/parameters && for p in mem_address mem_size record_size console_size pmsg_size ftrace_size max_reason ecc mem_type; do echo "$p=$(cat $p)"; done
```

```text
=== L7 read-only boot
pstore files: 0
mem_address=2145386496
mem_size=1048576
record_size=131072
console_size=262144
pmsg_size=131072
ftrace_size=131072
max_reason=2
ecc=0
mem_type=0
[    2.868441] reboot: Power down
```

출력에서 볼 것: 콜드 부팅에서는 레코드 0개. `mem_address=2145386496`은 `0x7fe00000`, `max_reason=2`는 KMSG_DUMP_OOPS(=oops와 panic 둘 다 기록)이다. `ecc=0`으로 보이지만 커널 로그는 `ecc: 16`이다. DT로 설정하면 ECC 값은 platform data로만 가고 모듈 파라미터 변수는 갱신되지 않기 때문이다. **sysfs 파라미터만 보고 "ECC 꺼짐"이라고 판단하면 틀린다** — 부팅 로그의 `ramoops: using ... ecc:` 줄이 기준이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="20" width="200" height="120" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="42" font-size="13" text-anchor="middle">부팅 N (평소)</text>
<text x="110" y="64" font-size="12" text-anchor="middle">console·pmsg 계속 이어 씀</text>
<text x="110" y="84" font-size="12" text-anchor="middle">panic → kmsg_dump</text>
<text x="110" y="104" font-size="12" text-anchor="middle">→ dmesg 슬롯에 압축 기록</text>
<text x="110" y="124" font-size="12" text-anchor="middle">panic=N → 리셋</text>
<rect x="240" y="20" width="200" height="120" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="42" font-size="13" text-anchor="middle">리셋 구간</text>
<text x="340" y="64" font-size="12" text-anchor="middle">DRAM refresh 유지?</text>
<text x="340" y="84" font-size="12" text-anchor="middle">DDR init이 안 지우나?</text>
<text x="340" y="104" font-size="12" text-anchor="middle">부트로더가 안 덮나?</text>
<text x="340" y="124" font-size="12" text-anchor="middle">→ 셋 다 yes여야 생존</text>
<rect x="470" y="20" width="200" height="120" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="570" y="42" font-size="13" text-anchor="middle">부팅 N+1</text>
<text x="570" y="64" font-size="12" text-anchor="middle">ramoops probe: sig 확인</text>
<text x="570" y="84" font-size="12" text-anchor="middle">ECC 검사·교정</text>
<text x="570" y="104" font-size="12" text-anchor="middle">/sys/fs/pstore/* 로 노출</text>
<text x="570" y="124" font-size="12" text-anchor="middle">systemd-pstore가 디스크로</text>
<line x1="210" y1="80" x2="238" y2="80" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="440" y1="80" x2="468" y2="80" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="10" y="180" width="320" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="170" y="204" font-size="13" text-anchor="middle">warm reset (QEMU system_reset)</text>
<text x="170" y="226" font-size="12" text-anchor="middle">예제 2·3: 레코드가 다음 부팅에 보인다</text>
<rect x="350" y="180" width="320" height="64" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="510" y="204" font-size="13" text-anchor="middle">cold (새 QEMU 프로세스, 전원 차단)</text>
<text x="510" y="226" font-size="12" text-anchor="middle">예제 4: 레코드 0개</text>
<text x="20" y="282" font-size="13">그래서 ramoops는 "panic·watchdog 리셋" 진단용이고, 전원 문제(브라운아웃) 진단에는 못 쓴다.</text>
<text x="20" y="304" font-size="13">전원 차단까지 견뎌야 하면 mtdpstore·pstore/blk·efi-pstore 같은 비휘발 backend가 필요하다.</text>
</svg>
```
그림 4 — pstore 기록이 리셋을 건너가는 흐름과 warm/cold 차이

---

## 5. systemd-pstore — 다음 부팅에서 디스크로 옮기기

### 5.1 왜 옮겨야 하나

`/sys/fs/pstore`의 파일은 backend 공간 그 자체다. ramoops 슬롯은 4개뿐이라 그대로 두면 다음 panic이 덮어쓰고, 콜드 리셋 한 번이면 사라진다. 그래서 부팅 초기에 **영구 저장소로 복사하고 backend에서 지우는** 단계가 필요하다. SSD 펌웨어의 "부팅 시 DRAM 덤프를 NAND 로그 블록으로 flush"와 같다.

systemd에는 이걸 하는 `systemd-pstore.service`가 있다. `/sys/fs/pstore`가 비어 있지 않을 때만 돈다(`ConditionDirectoryNotEmpty=/sys/fs/pstore`). 설정은 `/etc/systemd/pstore.conf`:

```text
[PStore]
Storage=external   # external(기본): /var/lib/systemd/pstore 로 복사, journal: 저널에만, none: 안 함
Unlink=yes         # 복사 후 /sys/fs/pstore 에서 지움 (기본 yes) → backend 공간이 비워진다
```

### 예제 5 — systemd 255 rootfs에서 panic → 재부팅 → 자동 보관

무엇을 확인하나: Ubuntu 24.04 + systemd 255 rootfs(`rootfs-sysd`의 내 복사본)를 같은 ramoops DTB로 부팅한다. 테스트용 oneshot 서비스(`l7-test.service`, 커널 인자 `l7.mode=pstore`일 때만 실행)가 1단계에서 정책 sysctl을 출력하고 pmsg를 쓴 뒤 panic, 2단계에서 systemd-pstore의 결과를 본다.

```sh
#!/bin/bash
# /usr/local/sbin/l7-test.sh (pstore part). Stage kept in /var/lib/l7
S=/var/lib/l7; mkdir -p $S
say() { echo "L7> $*"; }
if [ ! -e $S/pstore1 ]; then
  say "pstore stage 1 (before crash)"
  sysctl kernel.panic kernel.panic_on_oops kernel.oops_limit kernel.panic_on_warn kernel.core_pattern
  say "pstore dir: $(ls /sys/fs/pstore | wc -l) files"
  systemctl show systemd-pstore -p ActiveState -p ConditionResult
  echo "L7 pmsg: app=sensor-hub ver=1.4.2 state=STREAMING" > /dev/pmsg0
  touch $S/pstore1; sync
  say "triggering panic"; echo c > /proc/sysrq-trigger
else
  say "pstore stage 2 (after warm reset)"
  say "pstore dir now: $(ls /sys/fs/pstore | wc -l) files"
  journalctl -b -u systemd-pstore --no-pager -o short-monotonic
  find /var/lib/systemd/pstore -type f -exec ls -l {} \;
  f=$(find /var/lib/systemd/pstore -name 'pmsg-ramoops-0'); say "pmsg: $(head -1 $f)"
  d=$(find /var/lib/systemd/pstore -name 'dmesg-ramoops-0'); grep -E "Kernel panic|Comm:" $d
  rm -f $S/pstore1; systemctl poweroff
fi
```

```sh
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -nographic \
  -kernel /var/lab/out/Image-sec -drive file=l7_root.ext4,if=virtio,format=raw \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 -dtb l7_ramoops.dtb \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait panic=3 l7.mode=pstore"
```

```text
[   30.830171] l7-test.sh[313]: L7> pstore stage 1 (before crash)
[   30.946676] l7-test.sh[321]: kernel.panic = 3
[   30.958351] l7-test.sh[321]: kernel.panic_on_oops = 0
[   30.965629] l7-test.sh[321]: kernel.oops_limit = 10000
[   30.982075] l7-test.sh[321]: kernel.panic_on_warn = 0
[   30.989198] l7-test.sh[321]: kernel.core_pattern = |/usr/lib/systemd/systemd-coredump %P %u %g %s %t 9223372036854775808 %h %d
[   31.241128] l7-test.sh[313]: L7> pstore dir: 0 files
[   31.979844] l7-test.sh[325]: ActiveState=inactive
[   31.989494] l7-test.sh[325]: ConditionResult=no
[   32.344461] CPU: 1 UID: 0 PID: 313 Comm: l7-test.sh Not tainted 6.18.55-sec #1 PREEMPT
[   32.367505] Rebooting in 3 seconds..
...
[   22.630159] l7-test.sh[311]: L7> pstore stage 2 (after warm reset)
[   22.892535] l7-test.sh[311]: L7> pstore dir now: 0 files
[   23.187466] l7-test.sh[322]: [    8.237412] labsysd systemd-pstore[112]: PStore console-ramoops-0 moved to /var/lib/systemd/pstore/console-ramoops-0
[   23.198852] l7-test.sh[322]: [    8.330139] labsysd systemd-pstore[112]: PStore dmesg-ramoops-0 moved to /var/lib/systemd/pstore/dmesg-ramoops-0
[   23.214308] l7-test.sh[322]: [    8.343729] labsysd systemd-pstore[112]: PStore pmsg-ramoops-0 moved to /var/lib/systemd/pstore/pmsg-ramoops-0
[   23.223700] l7-test.sh[322]: [    8.344888] labsysd systemd[1]: Finished systemd-pstore.service - Platform Persistent Storage Archival.
[   23.326479] l7-test.sh[328]: -rw------- 1 root root 75 Oct 18 04:30 /var/lib/systemd/pstore/pmsg-ramoops-0
[   23.347933] l7-test.sh[329]: -rw------- 1 root root 33887 Oct 18 04:30 /var/lib/systemd/pstore/dmesg-ramoops-0
[   23.393644] l7-test.sh[330]: -rw------- 1 root root 27698 Oct 18 04:30 /var/lib/systemd/pstore/console-ramoops-0
[   23.569938] l7-test.sh[311]: L7> pmsg: L7 pmsg: app=sensor-hub ver=1.4.2 state=STREAMING
[   23.773219] l7-test.sh[336]: [   32.342759] Kernel panic - not syncing: sysrq triggered crash
[   23.783695] l7-test.sh[336]: [   32.344461] CPU: 1 UID: 0 PID: 313 Comm: l7-test.sh Not tainted 6.18.55-sec #1 PREEMPT
[   26.506626] reboot: Power down
```

출력에서 볼 것:

- 1단계: pstore가 비어 있어 `systemd-pstore`는 `ConditionResult=no`로 건너뛰었다. 이 이미지는 `panic_on_oops=0`이 기본이다 — 제품이라면 `oops=panic`을 넣을 자리.
- 2단계: 부팅 8초(monotonic)에 systemd-pstore가 세 레코드를 `/var/lib/systemd/pstore/`로 **옮겼고**(moved), `/sys/fs/pstore`는 0개가 됐다(`Unlink=yes`). 다음 panic을 받을 슬롯이 비었다는 뜻이다.
- 파일 권한이 `-rw-------`(root 전용). 크래시 로그에는 메모리 주소·명령줄·때로는 고객 데이터가 들어 있어서다(§8.2 PII).
- 1차 부팅의 `Comm: l7-test.sh`가 보관본 dmesg에 그대로 있다 → "어느 프로세스 문맥에서 panic했나"까지 다음 부팅에서 안다.
- systemd-pstore는 **옮기기까지만** 한다. 서버로 올리는 것은 별도 업로더의 몫이다(§10).

---
## 6. kdump — 두 번째 커널로 메모리 전체를 건진다

### 6.1 직관

pstore는 "마지막 로그 수십 KiB"다. 로그만으로는 답이 안 나오는 버그가 있다: 어떤 구조체가 이미 망가져 있었나, 다른 CPU는 무엇을 하고 있었나, 락은 누가 들고 있었나. 이때는 **panic 순간의 메모리 전체**가 필요하다. JTAG로 멈춘 SoC의 메모리를 통째로 떠서 오프라인으로 보는 것과 같은데, 현장 장비에는 JTAG가 없다.

kdump의 아이디어는 간단하다. 부팅 때 RAM 일부를 **아무도 쓰지 않게 비워 두고**(crashkernel=), 거기에 **두 번째 커널(캡처 커널)을 미리 올려 둔다**(`kexec -p`). panic이 나면 망가진 첫 번째 커널은 아무것도 정리하지 않고 곧장 캡처 커널로 점프한다(`__crash_kexec()`). 캡처 커널은 예약 영역 안에서만 살면서 나머지 RAM(죽은 커널의 메모리)을 `/proc/vmcore`라는 ELF core 파일로 보여 준다. 유저스페이스가 그걸 디스크에 쓰고 리부트한다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="110" y="20" font-size="13" text-anchor="middle">1차 커널 (제품 커널)</text>
<text x="340" y="20" font-size="13" text-anchor="middle">RAM</text>
<text x="570" y="20" font-size="13" text-anchor="middle">캡처 커널</text>
<rect x="10" y="34" width="200" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="59" font-size="12" text-anchor="middle">① crashkernel=256M 예약</text>
<rect x="10" y="90" width="200" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="115" font-size="12" text-anchor="middle">② kexec -p Image + cmdline</text>
<rect x="10" y="146" width="200" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="110" y="171" font-size="12" text-anchor="middle">③ panic → __crash_kexec()</text>
<rect x="10" y="202" width="200" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="110" y="227" font-size="12" text-anchor="middle">CPU 레지스터 저장 후 점프</text>
<rect x="260" y="34" width="160" height="150" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="100" font-size="12" text-anchor="middle">1차 커널 메모리</text>
<text x="340" y="118" font-size="12" text-anchor="middle">(죽은 상태 그대로)</text>
<rect x="260" y="184" width="160" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="210" font-size="12" text-anchor="middle">crashkernel 영역</text>
<text x="340" y="228" font-size="12" text-anchor="middle">캡처 커널 + elfcorehdr</text>
<rect x="470" y="146" width="200" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="570" y="171" font-size="12" text-anchor="middle">④ 예약 영역에서만 부팅</text>
<rect x="470" y="202" width="200" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="570" y="227" font-size="12" text-anchor="middle">⑤ /proc/vmcore 노출</text>
<rect x="470" y="258" width="200" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="570" y="283" font-size="12" text-anchor="middle">⑥ makedumpfile → 디스크</text>
<rect x="470" y="314" width="200" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="570" y="339" font-size="12" text-anchor="middle">⑦ 리부트 → 1차 커널</text>
<line x1="210" y1="110" x2="258" y2="210" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
<line x1="210" y1="222" x2="468" y2="170" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="468" y1="222" x2="422" y2="120" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
<line x1="570" y1="186" x2="570" y2="200" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="570" y1="242" x2="570" y2="256" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="570" y1="298" x2="570" y2="312" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="20" y="284" font-size="12">점선 ②: 캡처 커널 이미지를</text>
<text x="20" y="302" font-size="12">미리 예약 영역에 복사해 둔다.</text>
<text x="20" y="330" font-size="12">점선 ⑤: 캡처 커널은 죽은 메모리를</text>
<text x="20" y="348" font-size="12">읽기만 한다 (ELF core로 보여 줌).</text>
<text x="20" y="384" font-size="13">핵심: 망가진 커널은 디스크·네트워크를 만지지 않는다. 깨끗한 새 커널이 그 일을 한다.</text>
</svg>
```
그림 5 — kdump의 두 커널 시퀀스

SSD 비유로는 "assert가 나면 메인 펌웨어는 손 떼고, ROM에 있는 작은 덤프 전용 펌웨어가 DRAM 전체를 NAND로 쓰게 한다"에 가깝다. 죽은 코드가 복잡한 일(파일시스템, 블록 드라이버)을 하지 않는다는 점이 kdump가 신뢰받는 이유다.

### 6.2 구성 요소

| 요소 | 어디서 | 이 노트에서 |
|---|---|---|
| 커널 설정 | `CONFIG_KEXEC`/`KEXEC_FILE`, `CONFIG_CRASH_DUMP`, `CONFIG_PROC_VMCORE` (1차·캡처 둘 다. 같은 Image를 쓰는 게 보통) | Image-sec에 모두 내장 |
| 메모리 예약 | 커널 인자 `crashkernel=` | `crashkernel=256M` |
| 캡처 커널 적재 | `kexec -p <Image> --initrd=... --append=...` (kexec_load), `kexec -s -p ...` (kexec_file_load, 커널이 이미지를 검증·파싱) | `kexec -p /boot/Image-sec` |
| 캡처 쪽 유저랜드 | 작은 initramfs 또는 루트 FS에 덤프 스크립트 | `init=/usr/local/sbin/l7-capture.sh` |
| 덤프 필터·압축 | `makedumpfile -d 31 -c/-l/-z` | §6.4 |
| 분석 | `crash vmlinux vmcore`, `vmcore-dmesg`, `makedumpfile --dump-dmesg` | §6.5 |

`crashkernel=` 문법(`Documentation/admin-guide/kdump/kdump.rst`):

| 형식 | 뜻 |
|---|---|
| `crashkernel=256M` | 커널이 알아서 256 MiB 자리를 찾아 예약 |
| `crashkernel=256M@0x60000000` | 주소 고정 |
| `crashkernel=512M-2G:128M,2G-:256M` | RAM 크기 범위별로 다른 크기 (배포판 기본값 스타일) |
| `crashkernel=1G,high` + `crashkernel=128M,low` | 큰 영역은 4 GiB 위, DMA용 작은 영역은 아래 (arm64는 최근 6.x 커널에서 지원 — 제품 커널 버전 문서 확인) |

캡처 커널 명령줄에 넣는 관례: `nr_cpus=1`(CPU 하나만 깨운다 — 메모리·시간 절약, 다른 CPU는 죽은 커널 문맥에 멈춰 있다), `reset_devices`(드라이버가 장치를 강제 리셋하라는 힌트 — 죽은 커널이 DMA를 걸어 둔 채 멈췄을 수 있다), 필요 없는 서브시스템 끄기(`cma=0`, `cgroup_disable=memory` 등).

### 예제 6 — crashkernel 예약 → kexec -p → panic → 캡처 커널이 /proc/vmcore 저장

무엇을 확인하나: sysd rootfs를 `crashkernel=256M`으로 부팅해서 테스트 서비스(`l7.mode=kdump`)가 캡처 커널을 적재하고 panic한다. 캡처 커널은 같은 디스크를 루트로 쓰고 `init=`으로 덤프 스크립트를 PID 1로 실행한다. 스크립트는 vmcore를 저장하고 `echo b > /proc/sysrq-trigger`로 리부트, 다시 1차 커널이 떠서 결과를 보여 준다.

```sh
# l7-test.sh (kdump stage 1, 핵심 부분)
cat /proc/cmdline
grep -i "crash kernel" /proc/iomem; free -m
kexec -p /boot/Image-sec \
  --append="console=ttyAMA0 root=/dev/vda rw init=/usr/local/sbin/l7-capture.sh nr_cpus=1 reset_devices"
echo "kexec_crash_loaded=$(cat /sys/kernel/kexec_crash_loaded) crash_size=$(cat /sys/kernel/kexec_crash_size)"
sync; echo c > /proc/sysrq-trigger
```

```sh
#!/bin/sh
# /usr/local/sbin/l7-capture.sh — PID 1 of the capture kernel: save /proc/vmcore, then reboot
mount -t proc proc /proc; mount -t sysfs sysfs /sys; mount -o remount,rw /
mkdir -p /var/crash/l7; L=/var/crash/l7/capture.log
{
echo "capture kernel $(uname -r) cmdline: $(cat /proc/cmdline)"
grep MemTotal /proc/meminfo
ls -l /proc/vmcore
s=$(cut -d' ' -f1 /proc/uptime)
if ! makedumpfile -c -d 31 /proc/vmcore /var/crash/l7/vmcore 2>&1; then
  echo "fallback: raw sparse copy of /proc/vmcore"
  cp --sparse=always /proc/vmcore /var/crash/l7/vmcore.raw && echo "raw copy done"
fi
e=$(cut -d' ' -f1 /proc/uptime); echo "makedumpfile uptime $s -> $e"
makedumpfile --dump-dmesg /proc/vmcore /var/crash/l7/dmesg.txt 2>&1
ls -l /var/crash/l7
} > $L 2>&1
cat $L
sync; mount -o remount,ro /
echo b > /proc/sysrq-trigger
```

1차 커널 (부팅 → 적재 → panic):

```text
[    0.000000] crashkernel reserved: 0x000000006cc00000 - 0x000000007cc00000 (256 MB)
[    0.000000] Kernel command line: console=ttyAMA0 root=/dev/vda rw rootwait panic=3 crashkernel=256M l7.mode=kdump
[    0.166307] Memory: 674800K/1048576K available (22016K kernel code, 7058K rwdata, 20100K rodata, 3520K init, 800K bss, 338272K reserved, 32768K cma-reserved)
...
[   29.234804] l7-test.sh[328]:   6cc00000-7cbfffff : Crash kernel
[   29.346312] l7-test.sh[335]:                total        used        free      shared  buff/cache   available
[   29.356595] l7-test.sh[335]: Mem:             697         145         472           0         144         551
[   35.233178] l7-test.sh[317]: L7> kexec -p took 5.69 s
[   35.345197] l7-test.sh[317]: L7> kexec_crash_loaded=1 crash_size=268435456
[   35.582395] CPU: 0 UID: 0 PID: 317 Comm: l7-test.sh Kdump: loaded Not tainted 6.18.55-sec #1 PREEMPT
...
[   35.588580] Starting crashdump kernel...
[   35.589009] Bye!
```

캡처 커널:

```text
[    0.000000] OF: fdt: Reserving 4 KiB of memory at 0x7cbf0000 for elfcorehdr
[    0.000000] Kernel command line: console=ttyAMA0 root=/dev/vda rw init=/usr/local/sbin/l7-capture.sh nr_cpus=1 reset_devices
[    0.119112] Memory: 165844K/262144K available (22016K kernel code, 7058K rwdata, 20100K rodata, 3520K init, 800K bss, 61276K reserved, 32768K cma-reserved)
[    2.364394] Run /usr/local/sbin/l7-capture.sh as init process
capture kernel 6.18.55-sec cmdline: console=ttyAMA0 root=/dev/vda rw init=/usr/local/sbin/l7-capture.sh nr_cpus=1 reset_devices
MemTotal:         204388 kB
-r-------- 1 root root 860368896 Oct 18 04:35 /proc/vmcore
calculate_plat_config: PAGE SIZE 0x1000 and VA Bits 52 not supported
get_machdep_info_arm64: Can't determine platform config values

makedumpfile Failed.
fallback: raw sparse copy of /proc/vmcore
raw copy done
makedumpfile uptime 2.75 -> 13.42
calculate_plat_config: PAGE SIZE 0x1000 and VA Bits 52 not supported
get_machdep_info_arm64: Can't determine platform config values

makedumpfile Failed.
total 510708
-rw-r--r-- 1 root root       613 Oct 18 04:35 capture.log
-r-------- 1 root root 860368896 Oct 18 04:35 vmcore.raw
```

출력에서 볼 것:

- **메모리 비용이 그대로 보인다.** crashkernel 없이 부팅하면 `Memory: 935304K/1048576K available`인데(예제 5 부팅), 256M을 예약하면 `674800K`로 약 254 MiB 줄었다. `free`의 total도 697 MiB. 1 GiB 장비에서 RAM 4분의 1을 "평소엔 아무 일도 안 하는 커널"에 바친 셈이다.
- 1차 커널 panic 헤더에 `Kdump: loaded`가 찍힌다. 크래시 로그만 보고도 "이 장비는 kdump가 걸려 있었다"를 알 수 있다.
- 캡처 커널은 예약된 256 MiB 안에서만 돈다(`262144K` 중 `165844K` available). 그 안에서도 `32768K cma-reserved`가 보인다 — 1차 커널과 같은 CMA 설정이 캡처 커널에도 먹어서 32 MiB를 낭비한다. 캡처 커널 명령줄에 `cma=0`을 주는 이유다.
- `elfcorehdr` 4 KiB: kexec-tools가 미리 만들어 둔 "죽은 커널 메모리 지도"(ELF 프로그램 헤더 + CPU 레지스터 note). 캡처 커널은 이걸 보고 `/proc/vmcore`를 구성한다. 크기 `860368896`(≈820.5 MiB)는 `readelf -l`로 보면 LOAD 세그먼트 셋의 합이다: `0x40000000`부터 crashkernel 시작(`0x6cc00000`)까지 716 MiB, crashkernel 끝(`0x7cc00000`)부터 RAM 끝까지 52 MiB, 그리고 커널 이미지 영역 52.5 MiB를 커널 텍스트 가상주소(`0xffffcb96...`)로 한 번 더. 예약 영역은 정확히 빠져 있다.
- **makedumpfile이 실패했다.** 게스트(Ubuntu 24.04)의 makedumpfile 1.7.5도, VM 호스트의 1.7.7도 같은 메시지다. Image-sec는 `CONFIG_ARM64_VA_BITS_52=y`(4 KiB 페이지)로 빌드됐는데, makedumpfile의 arm64 코드가 "4K 페이지 + 52비트 VA" 조합을 지원하지 않는다. 실제 CPU(cortex-a57)는 LPA2가 없어 런타임엔 48비트로 돌고 있지만(VMCOREINFO의 `TCR_EL1_T1SZ=0x10`), makedumpfile은 VMCOREINFO의 `NUMBER(VA_BITS)=52`를 보고 거부한다. 그래서 스크립트가 원본 vmcore를 sparse 복사로 저장했다(820 MiB 중 디스크 실사용 약 500 MiB, TCG에서 약 10.7초).
- `kexec -p` 자체는 이번에 5.7초였지만 다른 실행에서 9.6초, 25.8초도 나왔다. TCG + VM 부하에 따라 크게 흔들리는 값이라 실제 하드웨어 수치로 읽으면 안 된다(실 HW에서는 보통 1초 미만).

실무 교훈: **kdump 경로는 "켜 놓기"가 아니라 "주기적으로 실제로 떨어뜨려 보기"까지가 설정이다.** 커널 config 하나(VA_BITS) 바꾼 것이 덤프 도구를 깨뜨렸고, panic이 나기 전까지 아무도 모른다. CI에 "kdump 한 번 떨어뜨리고 vmcore를 crash로 열어 보는" 테스트를 넣는 이유다.

### 6.3 makedumpfile — 덤프 레벨과 압축

원본 vmcore는 RAM 크기만큼 크다. makedumpfile은 커널 자료구조(`struct page` 플래그)를 읽어서 **분석에 필요 없는 페이지를 빼고** 압축한다. 이 때문에 VMCOREINFO(1차 커널이 남긴 "구조체 오프셋·심볼 주소 표")가 꼭 필요하고, 커널 메모리 레이아웃(VA_BITS, 페이지 크기)을 정확히 알아야 한다 — 위 실패가 그래서 났다.

| `-d` 비트 | 빼는 페이지 | 비고 |
|---|---|---|
| 1 | 0으로 채워진 페이지 | |
| 2 | non-private page cache | 파일 내용 캐시 |
| 4 | private page cache | |
| 8 | 유저 프로세스 페이지 | 유저 메모리가 필요하면 이 비트를 빼라 |
| 16 | free 페이지 | |
| 31 | 위 전부 | 커널 분석용 표준. 보통 수 GiB → 수십~수백 MiB |

압축: `-c`(zlib), `-l`(lzo), `-p`(snappy), `-z`(zstd, 빌드 옵션에 따라). `--dump-dmesg`는 vmcore에서 커널 로그만 텍스트로 뽑는다.

### 예제 7 — VM 호스트에서 덤프 꺼내 로그 뽑기

무엇을 확인하나: 내 이미지 복사본을 loop 마운트해서 vmcore를 꺼내고, makedumpfile이 안 되니 kexec-tools에 들어 있는 `vmcore-dmesg`로 로그를 뽑는다. `readelf -n`으로 vmcore의 note도 본다.

```sh
cd /var/tmp/L7 && mkdir -p dump && sudo mount -o loop l7_root.ext4 mnt
sudo cp --sparse=always mnt/var/crash/l7/vmcore.raw dump/ && sudo umount mnt
cd dump && ls -ls vmcore.raw
readelf -n vmcore.raw | head -6
makedumpfile -c -d 31 vmcore.raw vmcore.d31
/usr/sbin/vmcore-dmesg vmcore.raw > vmcore-dmesg.txt; wc -l vmcore-dmesg.txt
grep -n "Kernel panic\|Comm:\|sysrq" vmcore-dmesg.txt
```

```text
510680 -r-------- 1 donh root 860368896 Oct 17 21:37 vmcore.raw

Displaying notes found at file offset 0x00001000 with length 0x0000109c:
  Owner                Data size 	Description
  CORE                 0x00000188	NT_PRSTATUS (prstatus structure)
  CORE                 0x00000188	NT_PRSTATUS (prstatus structure)
  VMCOREINFO           0x00000d4c	Unknown note type: (0x00000000)
calculate_plat_config: PAGE SIZE 0x1000 and VA Bits 52 not supported
get_machdep_info_arm64: Can't determine platform config values

makedumpfile Failed.
429 vmcore-dmesg.txt
404:[   35.580663] sysrq: Trigger a crash
405:[   35.581054] Kernel panic - not syncing: sysrq triggered crash
406:[   35.582395] CPU: 0 UID: 0 PID: 317 Comm: l7-test.sh Kdump: loaded Not tainted 6.18.55-sec #1 PREEMPT
```

출력에서 볼 것:

- note가 셋: CPU마다 하나씩 `NT_PRSTATUS`(죽는 순간의 레지스터 — `-smp 2`라 2개)와 `VMCOREINFO`. VMCOREINFO 본문은 사람이 읽을 수 있는 텍스트로, 위에서 생략한 hex를 풀면 `OSRELEASE=6.18.55-sec`, `BUILD-ID=94bc14e3...`, `PAGESIZE=4096`, `NUMBER(VA_BITS)=52`, `KERNELOFFSET=...`(KASLR 오프셋), `CRASHTIME=...` 같은 줄이다. **분석 도구는 여기의 BUILD-ID로 맞는 vmlinux를 찾아야 한다.**
- VM 호스트의 makedumpfile 1.7.7도 같은 이유로 실패한다(도구 문제이지 캡처 문제가 아니다).
- `vmcore-dmesg`는 VMCOREINFO의 printk 링버퍼 심볼·오프셋만으로 로그를 복원하므로 VA_BITS 문제와 상관없이 동작했다. 429줄 전체 로그 끝에 panic이 있다 — pstore가 없어도 kdump만으로 마지막 로그는 얻는다.

### 6.4 crash로 분석하기

`crash`는 gdb를 품은 커널 덤프 분석기다. `crash vmlinux vmcore`로 열고 `bt`(패닉 CPU 콜스택), `log`(dmesg), `ps`(태스크 목록), `kmem -i`(메모리 요약), `struct`/`p`(자료구조), `foreach bt`(모든 태스크 스택), `dis`(역어셈블)를 쓴다. 필요한 것은 **같은 빌드의 디버그 정보가 든 vmlinux**다.

랩의 `vmlinux-sec`로 열어 보면:

```sh
cd /var/tmp/L7/dump && printf "sys\nbt\nlog -T | tail -25\nps | head -12\nquit\n" > cmds
time timeout 300 crash -s /var/lab/out/vmlinux-sec vmcore.raw -i cmds
```

```text
WARNING: CONFIG_DEBUG_INFO_REDUCED=y
crash: /var/lab/out/vmlinux-sec: no debugging data available

crash: zero-size memory allocation! (called from b5fde40b8e94)

real	0m14.496s
```

출력에서 볼 것: `CONFIG_DEBUG_INFO_REDUCED=y`로 빌드한 vmlinux는 구조체 타입 정보가 빠져 있어 crash가 거부한다. crash에는 `task_struct`·`mm_struct` 등의 **완전한 타입 정보**가 필요하다. 제품 빌드에서 "디버그 정보는 용량 때문에 reduced로" 같은 결정이 kdump 분석을 막는 전형적인 경우다. 디버그 정보는 장비에 넣을 필요가 없다 — **빌드 서버에 build-id로 보관**하면 된다(§9).

### 예제 7b — 커널을 고쳐 다시: VA_BITS=48 + full debug info

무엇을 확인하나: 실패 원인 둘을 실제로 확인하려고 Image-sec 설정에서 **두 항목만** 바꿔 내 디렉터리에 커널을 다시 빌드했다(`-j2`, VM 부하 속에서 약 32분). 그 커널을 1차·캡처 커널 둘 다로 써서 예제 6을 반복한다.

```sh
cp /var/lab/build/linux-sec/.config /var/tmp/L7/kbuild/.config
cd /var/lab/src/linux-6.18.55
./scripts/config --file /var/tmp/L7/kbuild/.config -d ARM64_VA_BITS_52 -e ARM64_VA_BITS_48 \
  -d DEBUG_INFO_REDUCED -d DEBUG_INFO_COMPRESSED_NONE -e DEBUG_INFO_COMPRESSED_ZLIB \
  --set-str LOCALVERSION "-l7kd"
make O=/var/tmp/L7/kbuild olddefconfig && make O=/var/tmp/L7/kbuild -j2 Image
# Image -> my rootfs copy as /boot/Image-l7kd, then boot it with crashkernel=256M l7.mode=kdump l7.kimg=Image-l7kd
```

캡처 커널 쪽 출력(진행 표시줄은 생략):

```text
capture kernel 6.18.55-l7kd cmdline: console=ttyAMA0 root=/dev/vda rw init=/usr/local/sbin/l7-capture.sh nr_cpus=1 reset_devices
MemTotal:         204388 kB
-r-------- 1 root root 860368896 Oct 18 05:15 /proc/vmcore
...
The kernel version is not supported.
The makedumpfile operation may be incomplete.

The dumpfile is saved to /var/crash/l7/vmcore.

makedumpfile Completed.
makedumpfile uptime 2.91 -> 28.92
...
-rw------- 1 root root     41880 Oct 18 05:15 dmesg.txt
-rw------- 1 root root 113261900 Oct 18 05:15 vmcore
```

VM 호스트에서 crash로 연다(`sys`, `log`, `bt -a`, `ps`, `kmem -i`; 출력 일부):

```sh
crash -s /var/tmp/L7/kbuild/vmlinux vmcore.d31 -i cmds3
```

```text
      KERNEL: /var/tmp/L7/kbuild/vmlinux
    DUMPFILE: vmcore.d31  [PARTIAL DUMP]
        CPUS: 2 [OFFLINE: 1]
        DATE: Wed Dec 31 16:00:00 PST 1969
      UPTIME: 00:01:17
LOAD AVERAGE: 8.01, 2.41, 0.84
       TASKS: 97
    NODENAME: labsysd
     RELEASE: 6.18.55-l7kd
     VERSION: #1 SMP PREEMPT Sat Oct 17 22:10:25 PDT 2026
     MACHINE: aarch64  (unknown Mhz)
      MEMORY: 1 GB
       PANIC: "Kernel panic - not syncing: sysrq triggered crash"
...
PID: 309      TASK: ffff00000597db40  CPU: 1    COMMAND: "l7-test.sh"
 #0 [ffff800080b3b9f0] machine_kexec at ffffb478e2a5194c
 #1 [ffff800080b3bb60] __crash_kexec at ffffb478e2bd1ff8
 #2 [ffff800080b3bb90] vpanic at ffffb478e2a159f0
 #3 [ffff800080b3bc40] panic at ffffb478e2a15c1c
 #4 [ffff800080b3bc90] sysrq_handle_crash at ffffb478e35ac398
 #5 [ffff800080b3bca0] __handle_sysrq at ffffb478e35acb50
 #6 [ffff800080b3bce0] write_sysrq_trigger at ffffb478e35ad2ac
 #7 [ffff800080b3bd20] proc_reg_write at ffffb478e2ec2a10
 #8 [ffff800080b3bdb0] vfs_write at ffffb478e2e13d6c
...
#16 [ffff800080b3bfe8] el0t_64_sync at ffffb478e2a11598
     PC: 0000ffffb1591d08   LR: 0000ffffb152d764   SP: 0000fffff7b78700
...
PID: 264      TASK: ffff000004cadb40  CPU: 0    COMMAND: "(udev-worker)"
 #0 [ffff800080003f30] crash_save_cpu at ffffb478e2bd26dc
...
      PID    PPID  CPU       TASK        ST  %MEM      VSZ      RSS  COMM
        0       0   0  ffffb478e56d7e00  RU   0.0        0        0  [swapper/0]
        0       0   1  ffff000003a04900  RU   0.0        0        0  [swapper/1]
        1       0   1  ffff000003968000  IN   0.6    21104    11900  systemd
        2       0   0  ffff000003969240  IN   0.0        0        0  [kthreadd]
...
                 PAGES        TOTAL      PERCENTAGE
    TOTAL MEM   178456     697.1 MB         ----
         FREE   104956       410 MB   58% of TOTAL MEM
         USED    73500     287.1 MB   41% of TOTAL MEM
       CACHED    47282     184.7 MB   26% of TOTAL MEM
         SLAB     7778      30.4 MB    4% of TOTAL MEM

real	0m24.853s
```

출력에서 볼 것:

- 두 가지만 바꾸자 makedumpfile(게스트 1.7.5)이 동작했다. `-d 31 -c`로 **820 MiB → 108 MiB**(113261900바이트), TCG에서 26초. "kernel version is not supported" 경고는 1.7.5가 6.18보다 오래돼서 나오는 것으로, 결과는 crash가 정상적으로 열었다. 제품에서는 커널 버전을 지원하는 makedumpfile을 캡처 환경에 넣는 것이 원칙이다.
- crash가 full debug info vmlinux(342 MB)로 열렸다. `PANIC:` 문자열, 패닉 CPU 1의 `bt`(panic → `__crash_kexec` → `machine_kexec`), 맨 아래 유저 레지스터(PC는 유저 주소 — `write()`를 부른 셸)까지 나온다.
- `bt -a`의 CPU 0은 `(udev-worker)`가 돌던 중 **`crash_save_cpu`에서 멈춰 있다**: 패닉 CPU가 IPI로 다른 CPU를 세우면서 각자 레지스터를 저장하게 한 흔적이다. "다른 CPU는 그 순간 무엇을 하고 있었나"는 pstore 로그로는 알 수 없는 정보다(데드락 분석의 핵심).
- `CPUS: 2 [OFFLINE: 1]`은 crash가 정지된 CPU를 offline으로 표시하는 것이고, `DATE`가 1969년(epoch 0)으로 나온 원인은 확인하지 않았다(분석에는 영향 없다).
- `kmem -i`의 TOTAL MEM 697 MB는 1차 커널이 본 메모리(crashkernel 256M 제외)와 일치한다. 메모리 누수·slab 폭증 크래시라면 여기서 바로 보인다.

### 6.5 kdump와 pstore를 같이 쓰면 — `crash_kexec_post_notifiers`

§2.3에서 소스로 본 순서를 실제로 확인한다. 예제 6과 같은 kdump 설정에 ramoops DTB를 함께 주고(그래서 캡처 커널도 같은 ramoops 영역을 본다), 캡처 커널이 `/sys/fs/pstore`를 나열하게 했다. 덤프 복사는 생략(`l7.nocopy`). 첫 번째는 기본값, 두 번째는 1차 커널 명령줄에 `crash_kexec_post_notifiers`를 추가했다.

```sh
# run_kp.sh: same kdump flow as Example 6, plus ramoops DT; "$@" = extra kernel args
bash run_sysd.sh l7_kp0.log 900 -dtb l7_ramoops.dtb -- panic=3 crashkernel=256M l7.mode=kdump l7.nocopy
bash run_sysd.sh l7_kp1.log 900 -dtb l7_ramoops.dtb -- panic=3 crashkernel=256M l7.mode=kdump l7.nocopy crash_kexec_post_notifiers
```

기본값(kp0) — 캡처 커널과 그다음 부팅:

```text
pstore in capture kernel:
total 0
-r--r--r-- 1 root root 26332 Oct 18 04:55 console-ramoops-0
...
[  108.725572] l7-test.sh[318]: archived pstore:
[  109.339515] l7-test.sh[329]: console-ramoops-0
```

`crash_kexec_post_notifiers`(kp1):

```text
pstore in capture kernel:
total 0
-r--r--r-- 1 root root 27702 Oct 18 05:00 console-ramoops-0
-r--r--r-- 1 root root 42070 Oct 18 05:00 dmesg-ramoops-0
...
[  106.150856] l7-test.sh[322]: archived pstore:
[  106.773198] l7-test.sh[333]: console-ramoops-0
[  106.846585] l7-test.sh[333]: dmesg-ramoops-0
```

kp0 실행 후 보관된 console 레코드의 명령줄:

```text
[    0.000000] Kernel command line: console=ttyAMA0 root=/dev/vda rw init=/usr/local/sbin/l7-capture.sh nr_cpus=1 reset_devices l7.nocopy
```

출력에서 볼 것:

- 기본값에서는 **dmesg 레코드가 없다.** `__crash_kexec()`가 `kmsg_dump()`보다 먼저 불려 돌아오지 않았기 때문이다. `crash_kexec_post_notifiers`를 주면 kmsg_dump가 먼저 돌아 dmesg 레코드가 생기고, 그다음 캡처 커널로 간다.
- 함정 하나 더: 다음 부팅에서 보관된 `console-ramoops-0`의 명령줄이 **캡처 커널의 것**이다. 캡처 커널도 같은 DT를 받아 같은 ramoops 영역의 console 링버퍼에 자기 부팅 로그를 이어 썼기 때문에, 1차 커널의 마지막 콘솔 줄은 밀려났다. kdump + ramoops 조합이라면 캡처 커널 명령줄에서 pstore를 끄거나(예: 캡처 커널용 DTB에서 ramoops 노드 제거, 또는 `ramoops.mem_size=0` 같은 방법 — 제품 커널에서 동작을 확인하고 쓸 것) console 레코드를 캡처 단계에서 먼저 저장해야 한다.

### 6.6 임베디드에서 kdump를 쓰나?

| 항목 | 256 MiB급 소형 보드 (i.MX6ULL, AM335x 등) | Jetson Orin / i.MX8 고사양 (8~64 GiB) |
|---|---|---|
| crashkernel 예약 | 128 MiB만 잡아도 RAM의 절반 | 512 MiB ~ 1 GiB도 부담 적음 |
| 덤프 저장 공간 | eMMC/NAND 몇 백 MiB가 귀함, 쓰기 수명 부담 | NVMe·대용량 eMMC |
| 덤프 업로드 | 셀룰러로 수백 MiB는 비현실적 | 필터·압축 후 가능, 또는 현장 회수 |
| 대안 | pstore/ramoops + 좋은 panic 로그 + 재현 환경 | kdump + pstore 둘 다 |
| 결론 | **보통 안 쓴다.** 개발 단계나 특정 버그 추적용 빌드에서만 | 서버급 운영 방식 그대로 쓸 만하다 (L4로 OTA 배포한 커널마다 vmlinux 보관) |

---
## 7. 유저스페이스 coredump — 가장 흔한 필드 크래시

### 7.1 커널이 core를 쓰는 경로

A6 §8에서 본 것처럼, Core 동작 시그널(SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGQUIT …)로 프로세스가 죽으면 커널의 `do_coredump()`(`fs/coredump.c`)가 프로세스 메모리와 레지스터를 ELF core 형식으로 내보낸다. **어디로** 보낼지는 `kernel.core_pattern`이 정한다.

| core_pattern 첫 글자 | 종류 (6.18 `enum coredump_type_t`) | 동작 |
|---|---|---|
| 일반 경로 (`/var/crash/core.%e.%p`) | `COREDUMP_FILE` | 커널이 직접 파일을 만든다. `RLIMIT_CORE`(ulimit -c)가 크기 한도 |
| 파이프 기호 + 경로 (`/path/helper args` 앞에 파이프) | `COREDUMP_PIPE` | 커널이 helper를 **root, 초기 네임스페이스**에서 실행하고 core를 stdin으로 흘려 보낸다. `RLIMIT_CORE`는 무시(단, 값이 1이면 재귀 방지로 중단) |
| `@/path/socket` | `COREDUMP_SOCK` (최근 커널에 추가) | 유닉스 소켓으로 coredump 서버에 넘긴다 |

pattern 지정자: `%p`(PID), `%P`(초기 PID 네임스페이스 기준 PID), `%u`/`%g`(uid/gid), `%s`(시그널), `%t`(epoch 시각), `%h`(호스트명), `%e`(실행 파일 이름), `%c`(RLIMIT_CORE soft 값), `%d`(dump mode). 전체 목록은 `man 5 core`.

`kernel.core_pipe_limit`(이 이미지는 16)은 "동시에 몇 개까지 helper가 끝날 때까지 죽은 프로세스를 수거하지 않고 기다릴지"다. helper가 `/proc/<pid>/`에서 명령줄·cgroup·환경 같은 메타데이터를 읽을 시간을 준다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="30" width="150" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="85" y="54" font-size="13" text-anchor="middle">센서 데몬</text>
<text x="85" y="74" font-size="12" text-anchor="middle">NULL 역참조</text>
<rect x="190" y="30" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="265" y="54" font-size="13" text-anchor="middle">커널</text>
<text x="265" y="74" font-size="12" text-anchor="middle">SIGSEGV → coredump</text>
<rect x="370" y="30" width="150" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="445" y="54" font-size="13" text-anchor="middle">systemd-coredump</text>
<text x="445" y="74" font-size="12" text-anchor="middle">core_pattern 파이프</text>
<rect x="530" y="30" width="140" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="600" y="54" font-size="13" text-anchor="middle">@.service</text>
<text x="600" y="74" font-size="12" text-anchor="middle">소켓으로 넘겨 처리</text>
<line x1="160" y1="58" x2="188" y2="58" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="340" y1="58" x2="368" y2="58" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="520" y1="58" x2="528" y2="58" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="600" y1="86" x2="600" y2="126" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="370" y="128" width="300" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="520" y="152" font-size="12" text-anchor="middle">/var/lib/systemd/coredump/core.*.zst</text>
<text x="520" y="172" font-size="12" text-anchor="middle">ProcessSizeMax · ExternalSizeMax · MaxUse</text>
<rect x="10" y="128" width="330" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="175" y="152" font-size="12" text-anchor="middle">journal 항목: PID, 시그널, 명령줄, unit</text>
<text x="175" y="172" font-size="12" text-anchor="middle">+ elfutils로 만든 스택 트레이스 (심볼 있으면)</text>
<line x1="530" y1="100" x2="342" y2="150" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
<rect x="10" y="230" width="330" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="175" y="254" font-size="13" text-anchor="middle">coredumpctl list / info / dump</text>
<text x="175" y="274" font-size="12" text-anchor="middle">장비 위에서 1차 분류</text>
<rect x="370" y="230" width="300" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="520" y="254" font-size="13" text-anchor="middle">업로드 → 서버의 gdb</text>
<text x="520" y="274" font-size="12" text-anchor="middle">build-id로 심볼 찾아 bt</text>
<line x1="175" y1="188" x2="175" y2="228" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="520" y1="188" x2="520" y2="228" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="20" y="316" font-size="13">core 파일과 메타데이터(journal)는 따로 산다 — 파일이 지워져도 "언제 무엇이 죽었나"는 남는다.</text>
</svg>
```
그림 6 — systemd-coredump 파이프라인

### 예제 8 — 일부러 죽는 "센서 데몬"

무엇을 확인하나: 잘린 프레임에서 `payload`가 NULL이 되어 `strlen()`이 폴트하는 프로그램. 인자 1이면 `abort()`, 2면 64 MiB 힙을 압축 안 되는 값으로 채운 뒤 폴트한다(크기 한도 실험용). VM 호스트(gcc 15, glibc 2.42)에서 빌드한 동적 바이너리는 게스트(Ubuntu 24.04, glibc 2.39)에서 심볼 버전 때문에 안 돌 수 있어서 `-static`으로 빌드했다.

```c
/* l7_segv.c — a "sensor daemon" that dereferences a bad pointer on a parse error */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct frame { int id; char *payload; };

static int parse_frame(const struct frame *f)
{
    /* bug: payload is NULL for a truncated frame, strlen() faults */
    return (int)strlen(f->payload);
}

static void handle_rx(int seq)
{
    struct frame f = { .id = seq, .payload = NULL };
    if (seq < 3) f.payload = "OK";
    printf("seq %d len %d\n", seq, parse_frame(&f));
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int mode = argc > 1 ? atoi(argv[1]) : 0;
    if (mode == 1) abort();                 /* SIGABRT path */
    if (mode == 2) {                        /* big heap, to test size limits */
        size_t n = 64u << 20;
        char *p = malloc(n);
        if (!p) { perror("malloc"); exit(1); }
        for (size_t i = 0; i < n; i++)      /* fill with hard-to-compress bytes */
            p[i] = (char)((i * 2654435761u) >> 13);
        __asm__ volatile("" : : "r"(p) : "memory"); /* keep malloc alive */
        printf("touched %zu MiB\n", n >> 20);
    }
    for (int seq = 0; seq < 5; seq++)
        handle_rx(seq);
    return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O1 -g -static l7_segv.c -o /var/tmp/L7/bin/l7_segv
file /var/tmp/L7/bin/l7_segv
```

```text
/var/tmp/L7/bin/l7_segv: ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=c62e5b58b09b15548e07f93133b3a48e937b8632, for GNU/Linux 3.7.0, with debug_info, not stripped
```

출력에서 볼 것: 경고 0개. `BuildID[sha1]=...` — 링커가 넣는 이 해시가 나중에 "이 core는 어느 빌드의 바이너리였나"를 정하는 열쇠다(§9). 이 값은 첫 빌드의 것이고, 64 MiB 채우기 부분을 고친 뒤 다시 빌드한 바이너리는 build-id가 달라진다(코드가 바뀌면 바뀐다 — 그게 요점이다).

처음 버전은 힙을 `memset(p, 0x5a, n)`으로 채웠는데 core가 전혀 커지지 않았다. `-O1`에서 GCC가 "쓰고 한 번도 안 읽는 malloc"을 통째로 지웠기 때문이다(malloc elision). 빈 `asm volatile`에 포인터를 넘겨 컴파일러가 지우지 못하게 했다.

### 예제 9 — systemd-coredump + coredumpctl

무엇을 확인하나: sysd 게스트에서 테스트 서비스(`l7.mode=core`)가 위 프로그램을 SIGSEGV·SIGABRT로 죽이고 `coredumpctl`로 본다. 그다음 core를 꺼내 VM 호스트의 gdb로 분석한다(게스트 이미지에는 gdb가 없다).

```sh
sysctl kernel.core_pattern kernel.core_pipe_limit
ulimit -c unlimited
/usr/local/bin/l7_segv;   echo "exit status $?"
/usr/local/bin/l7_segv 1; echo "exit status $?"
sleep 3
coredumpctl list --no-pager
coredumpctl info --no-pager COREDUMP_SIGNAL_NAME=SIGSEGV | sed -n '1,40p'
ls -l /var/lib/systemd/coredump/
coredumpctl dump --no-pager -o /var/crash/l7/core.segv COREDUMP_SIGNAL_NAME=SIGSEGV
```

```text
kernel.core_pattern = |/usr/lib/systemd/systemd-coredump %P %u %g %s %t 9223372036854775808 %h %d
kernel.core_pipe_limit = 16
seq 0 len 2
seq 1 len 2
seq 2 len 2
/usr/local/sbin/l7-core.sh: line 7:   329 Segmentation fault      (core dumped) /usr/local/bin/l7_segv
L7> exit status 139
/usr/local/sbin/l7-core.sh: line 8:   379 Aborted                 (core dumped) /usr/local/bin/l7_segv 1
L7> exit status 134
TIME                        PID UID GID SIG     COREFILE EXE                    SIZE
Sun 2026-10-18 04:43:01 UTC 329   0   0 SIGSEGV missing  /usr/local/bin/l7_segv    -
Sun 2026-10-18 04:43:15 UTC 352   0   0 SIGABRT missing  /usr/local/bin/l7_segv    -
Sun 2026-10-18 04:43:43 UTC 400   0   0 SIGSEGV missing  /usr/local/bin/l7_segv    -
Sun 2026-10-18 04:52:20 UTC 329   0   0 SIGSEGV present  /usr/local/bin/l7_segv 8.1K
Sun 2026-10-18 04:52:27 UTC 379   0   0 SIGABRT present  /usr/local/bin/l7_segv 7.9K
           PID: 329 (l7_segv)
           UID: 0 (root)
           GID: 0 (root)
        Signal: 11 (SEGV)
     Timestamp: Sun 2026-10-18 04:42:44 UTC (9min ago)
  Command Line: /usr/local/bin/l7_segv
    Executable: /usr/local/bin/l7_segv
 Control Group: /system.slice/l7-test.service
          Unit: l7-test.service
         Slice: system.slice
       Boot ID: 2e7f7cb05165415dbc84c85c4c957305
    Machine ID: 1db677694b984127a01b305411b4cd2d
      Hostname: labsysd
       Storage: /var/lib/systemd/coredump/core.l7_segv.0.2e7f7cb05165415dbc84c85c4c957305.329.1792298564000000.zst (missing)
       Message: Process 329 (l7_segv) of user 0 dumped core.

                Stack trace of thread 329:
                #0  0x0000000000411b10 n/a (n/a + 0x0)
                #1  0x00000000004007e0 n/a (n/a + 0x0)
                #2  0x00000000004008d0 n/a (n/a + 0x0)
                #3  0x0000000000400f4c n/a (n/a + 0x0)
                #4  0x00000000004005f0 n/a (n/a + 0x0)
                ELF object binary architecture: AARCH64
...
total 28
-rw-r----- 1 root root 8367 Oct 18 04:52 core.l7_segv.0.102fc2e65196459c9ff2bf3d4ef7e0bf.329.1792299129000000.zst
-rw-r----- 1 root root 8153 Oct 18 04:52 core.l7_segv.0.102fc2e65196459c9ff2bf3d4ef7e0bf.379.1792299140000000.zst
-rw-r--r-- 1 root root 1998848 Oct 18 04:52 /var/crash/l7/core.segv
```

출력에서 볼 것:

- 이 출력은 같은 이미지를 **두 번째로** 부팅한 실행의 것이다. 첫 실행의 core 파일은 실험 사이에 내가 지웠는데, 목록에는 `missing`으로 남아 있다. **메타데이터는 journal에, core 본체는 파일로 따로** 저장되기 때문이다. 필드에서 디스크 한도(`MaxUse=`)로 core 파일이 정리돼도 "언제, 어느 unit의, 어떤 시그널"이었는지는 journal에 남는다. PID 329가 두 번 보이는 것도 실제 상황 — 부팅마다 같은 순서로 서비스가 뜨면 PID가 겹친다. 그래서 식별자는 PID가 아니라 **Boot ID + PID + 시각**이다(파일 이름에 셋 다 들어 있다).
- 셸이 `(core dumped)`, 종료 코드 139 = 128 + 11(SIGSEGV), 134 = 128 + 6(SIGABRT).
- `Unit: l7-test.service`, `Control Group` — 어느 서비스가 죽었는지 systemd가 붙여 준다. 크래시 대시보드에서 "서비스별 크래시 수"를 집계하는 키.
- core는 `.zst`(zstd)로 압축돼 8 KiB. `coredumpctl dump`로 풀어 놓으니 약 2 MB(정적 바이너리라 libc가 다 들어 있다).
- **스택 트레이스가 `n/a`다.** systemd-coredump는 elfutils로 스택을 풀지만, 이 정적 바이너리는 "어느 ELF 모듈인지"를 core 안의 link map으로 찾지 못해 심볼을 못 붙였다. 장비 위 즉석 트레이스는 "있으면 좋은 것"이고, 확실한 분석은 서버에서 한다.

VM 호스트에서 gdb로:

```sh
gdb -q -batch -ex "bt" -ex "frame 2" -ex "info locals" /var/tmp/L7/bin/l7_segv core.segv
```

```text
Core was generated by `/usr/local/bin/l7_segv'.
Program terminated with signal SIGSEGV, Segmentation fault.
#0  0x00000000004129d0 in __strlen_asimd ()
#0  0x00000000004129d0 in __strlen_asimd ()
#1  0x00000000004009b4 in parse_frame (f=<synthetic pointer>) at /Users/donh/.elinux_scratch/L7/guest/l7_segv.c:12
#2  handle_rx (seq=3) at /Users/donh/.elinux_scratch/L7/guest/l7_segv.c:19
#3  main (argc=<optimized out>, argv=<optimized out>) at /Users/donh/.elinux_scratch/L7/guest/l7_segv.c:37
#2  handle_rx (seq=3) at /Users/donh/.elinux_scratch/L7/guest/l7_segv.c:19
19	    printf("seq %d len %d\n", seq, parse_frame(&f));
f = {id = <optimized out>, payload = 0x0}
```

출력에서 볼 것: 디버그 정보가 있는 같은 바이너리를 주니 `strlen` ← `parse_frame` ← `handle_rx(seq=3)` ← `main`, 그리고 `payload = 0x0`까지 바로 나온다. 장비에는 strip된 바이너리, 서버에는 같은 build-id의 디버그 정보 — 이 분리가 §9의 symbol server 구조다. gdb는 시작할 때 `https://debuginfod.ubuntu.com`을 쓸지 물었다(배치 모드라 N). 배포판 패키지는 이 방식으로 심볼을 받는다.

### 예제 10 — 크기 한도: coredump.conf, core_pattern 파일, RLIMIT_CORE

무엇을 확인하나: (B) systemd-coredump의 `ProcessSizeMax=16M`이 64 MiB 힙 프로세스에 어떻게 작동하나, (C) core_pattern을 일반 파일로 바꾸고 `ulimit -c`를 0 / 무제한 / 4096으로 바꿔 본다.

```sh
mkdir -p /etc/systemd/coredump.conf.d
printf '[Coredump]\nProcessSizeMax=16M\nExternalSizeMax=16M\n' > /etc/systemd/coredump.conf.d/l7.conf
/usr/local/bin/l7_segv 2; sleep 3
coredumpctl info --no-pager -1 | grep -E "PID|Signal|Storage|Size|Message" | head -8
rm /etc/systemd/coredump.conf.d/l7.conf
mkdir -p /var/crash; sysctl -w kernel.core_pattern=/var/crash/core.%e.%p.%s
( ulimit -c 0;         /usr/local/bin/l7_segv >/dev/null ); echo "ulimit 0 -> $(ls /var/crash | grep -c core) core files"
( ulimit -c unlimited; /usr/local/bin/l7_segv 2 >/dev/null )
( ulimit -c 4096;      /usr/local/bin/l7_segv 2 >/dev/null )
ls -ls /var/crash/ | grep core
```

```text
touched 64 MiB
/usr/local/sbin/l7-core.sh: line 18:   455 Segmentation fault      (core dumped) /usr/local/bin/l7_segv 2
           PID: 455 (l7_segv)
        Signal: 11 (SEGV)
       Storage: /var/lib/systemd/coredump/core.l7_segv.0.102fc2e65196459c9ff2bf3d4ef7e0bf.455.1792299158000000.zst (truncated)
  Size on Disk: 183.6K
       Message: Process 455 (l7_segv) of user 0 dumped core.
kernel.core_pattern = /var/crash/core.%e.%p.%s
/usr/local/sbin/l7-core.sh: line 24:   510 Segmentation fault      /usr/local/bin/l7_segv > /dev/null
L7> ulimit 0 -> 0 core files
/usr/local/sbin/l7-core.sh: line 25:   515 Segmentation fault      (core dumped) /usr/local/bin/l7_segv 2 > /dev/null
/usr/local/sbin/l7-core.sh: line 26:   526 Segmentation fault      (core dumped) /usr/local/bin/l7_segv 2 > /dev/null
65612 -rw------- 1 root root 68370432 Oct 18 04:52 core.l7_segv.515.11
 4096 -rw------- 1 root root  5500928 Oct 18 04:52 core.l7_segv.526.11
```

출력에서 볼 것:

- (B) 64 MiB 힙 프로세스는 `ProcessSizeMax=16M`을 넘어서 `(truncated)` — 앞부분 16 MiB까지만 압축 저장(183.6 KiB). 잘린 core도 레지스터·스택이 앞쪽 note·세그먼트에 있으면 bt는 대개 나온다. 힙 내용을 봐야 하는 버그라면 못 본다.
- (C) 파일 모드에서는 `RLIMIT_CORE`가 그대로 적용된다. `ulimit -c 0`이면 셸 메시지에서 `(core dumped)`가 빠지고 파일이 없다. 무제한이면 68 MB(64 MiB 힙 + 나머지). `ulimit -c 4096`(bash 단위 1 KiB 블록 = 4 MiB)이면 디스크 실사용이 정확히 4096 KiB에서 멈췄다. 겉보기 크기 5.5 MB가 더 큰 것은 0인 페이지를 커널이 건너뛰어(hole) sparse 파일로 썼기 때문이다.
- 파이프 모드(systemd-coredump)에서는 커널이 `RLIMIT_CORE`를 무시하고(소스 `fs/coredump.c`: `cprm->limit = RLIM_INFINITY`), 대신 helper에 한도 값을 인자로 넘긴다. Ubuntu의 `50-coredump.conf`는 그 자리에 `%c` 대신 상수 `9223372036854775808`(2^63)을 넣어 **서비스의 `LimitCORE=`와 상관없이 항상 수집**하게 해 두었다. 한도 관리는 `coredump.conf`(ProcessSizeMax·ExternalSizeMax·MaxUse·KeepFree)로 한다.

### 7.2 임베디드 core 정책

| 결정 | 선택지 | 권장 |
|---|---|---|
| 수집기 | 커널 파일 직접 / systemd-coredump / 자체 helper(파이프 기호 + `/usr/bin/crash-catcher`) | systemd 장비면 systemd-coredump. BusyBox 장비면 작은 helper가 압축·회전·메타데이터 기록 |
| 크기 | 전체 core / 잘린 core / minidump | 플래시가 작으면 `ProcessSizeMax` 수 MiB + 압축. 큰 앱(카메라·AI)은 minidump |
| 무엇을 뺄까 | `/proc/<pid>/coredump_filter` 비트(익명 private, shared, ELF 헤더, hugetlb …) | 큰 DMA 버퍼·카메라 공유 메모리는 빼고 힙·스택만 |
| 보관 | `MaxUse=`·`KeepFree=` | 쓰기 수명 고려해 개수·용량 상한 |
| 재시작 | systemd `Restart=on-failure` + `StartLimitBurst` | 크래시 루프는 L4의 bootcount·롤백으로 연결 |

**minidump**(Google Breakpad/Crashpad의 형식)는 전체 메모리 대신 스레드별 스택 일부·레지스터·모듈 목록(build-id 포함)만 담은 수십~수백 KiB짜리 덤프다. 앱 안에 시그널 핸들러(또는 별도 handler 프로세스)를 넣어 크래시 순간 직접 쓴다. 서버 쪽에서 build-id로 심볼을 찾아 스택을 복원한다. 크롬·안드로이드 앱·여러 임베디드 제품이 이 방식을 쓴다. 이 노트에서는 개념만 다룬다(실행 안 함).

---

## 8. 원격 로그 — 장비 밖으로 꺼내기

### 8.1 왜 따로 설계해야 하나

장비 안에 아무리 잘 남겨도, 장비가 회수되지 않으면 소용없다. 그리고 몇몇 크래시(전원·조용한 정지)는 **장비 안에 아무것도 안 남는다** — 그 직전까지 밖으로 보낸 로그가 유일한 증거다. 경로는 둘로 나뉜다.

| 경로 | 무엇 | 장점 | 한계 |
|---|---|---|---|
| **netconsole** | 커널 printk를 UDP로 즉시 송신 (netpoll — 인터럽트·스케줄러 없이 NIC를 폴링) | panic 메시지까지 나간다. 유저스페이스 불필요 | UDP라 유실·순서 보장 없음, 같은 L2/라우팅 필요, 암호화 없음. 랩·공장·사내망용 |
| **journal 업로드** (systemd-journal-upload → journal-remote) | journald 항목을 HTTP(S)로 전송, 커서로 재개 | 구조화 필드, TLS 가능, 끊겼다가 이어 보냄 | 유저스페이스가 살아 있어야. panic 메시지는 못 보냄 |
| rsyslog / syslog-ng / fluent-bit / vector | 로그 수집·필터·버퍼링·여러 목적지 | 디스크 버퍼, 재시도, 변환 | 메모리·CPU 비용, 설정 복잡 |
| 크래시 파일 업로더 (자체 데몬) | pstore·core·vmcore 파일을 묶어 HTTPS로 | 큰 파일, 재시도, 대역폭 제어 | 직접 만들어야 함 (§10) |

### 예제 11 — netconsole: 게스트 커널 로그와 panic을 UDP로 VM 호스트에 보낸다

무엇을 확인하나: Image-sec는 `CONFIG_NETCONSOLE=y` + `NETCONSOLE_DYNAMIC=y`라 부팅 후 configfs로 대상을 만들 수 있다. QEMU user-net에서 게스트가 보는 `10.0.2.2`는 VM 호스트의 루프백으로 NAT되므로, VM 호스트에서 `socat`으로 UDP 16677을 받는다. 게이트웨이 MAC은 QEMU slirp의 고정값 `52:55:0a:00:02:02`.

```sh
# VM host side (before booting the guest)
socat -u UDP-RECV:16677 OPEN:l7_netcon.txt,creat,append &
```

```sh
# guest side (l7-core.sh, part E)
ip -br link; ip -br addr
mountpoint -q /sys/kernel/config || mount -t configfs none /sys/kernel/config
N=/sys/kernel/config/netconsole/l7; mkdir $N
DEV=$(ip -br link | awk '$1 ~ /^en/ {print $1; exit}')
echo $DEV > $N/dev_name; echo 10.0.2.15 > $N/local_ip; echo 10.0.2.2 > $N/remote_ip
echo 16677 > $N/remote_port; echo 52:55:0a:00:02:02 > $N/remote_mac; echo 1 > $N/enabled
for f in enabled dev_name local_ip remote_ip remote_port remote_mac; do echo "$f=$(cat $N/$f)"; done
echo "<3>L7 netconsole: hello from guest $(hostname)" > /dev/kmsg
sleep 1; sync; echo c > /proc/sysrq-trigger
```

게스트 출력:

```text
lo               UNKNOWN        00:00:00:00:00:00 <LOOPBACK,UP,LOWER_UP>
enp0s1           UP             52:54:00:12:34:56 <BROADCAST,MULTICAST,UP,LOWER_UP>
lo               UNKNOWN        127.0.0.1/8 ::1/128
enp0s1           UP             10.0.2.15/24 metric 1024 fec0::5054:ff:fe12:3456/64 fe80::5054:ff:fe12:3456/64
enabled=1
dev_name=enp0s1
local_ip=10.0.2.15
remote_ip=10.0.2.2
remote_port=16677
remote_mac=52:55:0a:00:02:02
```

VM 호스트가 받은 것 (`l7_netcon.txt` 처음 부분, 콜스택 중간 생략):

```text
[  306.861828] L7 netconsole: hello from guest labsysd
[  308.234051] sysrq: Trigger a crash
[  308.241264] Kernel panic - not syncing: sysrq triggered crash
[  308.242316] CPU: 1 UID: 0 PID: 321 Comm: l7-core.sh Not tainted 6.18.55-sec #1 PREEMPT
[  308.242561] Hardware name: linux,dummy-virt (DT)
[  308.242821] Call trace:
[  308.243035]  show_stack+0x28/0x7c (C)
...
[  308.249442]  write_sysrq_trigger+0xb0/0xe0
...
[  308.250721] SMP: stopping secondary CPUs
[  308.253822] Kernel Offset: 0x2ef8d9600000 from 0xffff800080000000
[  308.254033] PHYS_OFFSET: 0xfff1000040000000
[  308.254185] CPU features: 0x104000,04027800,40004000,0400421b
[  308.254383] Memory Limit: none
[  308.254812] ---[ end Kernel panic - not syncing: sysrq triggered crash ]---
```

출력에서 볼 것: **panic의 마지막 줄 `---[ end Kernel panic ...]---`까지 UDP로 나갔다.** 파일시스템·네트워크 스택 상위 계층은 이미 못 쓰는 상태지만 netpoll은 NIC 드라이버의 poll 함수로 직접 패킷을 내보낸다. 장비 랙이나 HIL 테스트 벤치에서 시리얼 콘솔 수십 개를 깔지 않고 로그를 모으는 실전 방법이다. 같은 설정을 부팅 인자로 주려면 `netconsole=6665@10.0.2.15/eth0,16677@10.0.2.2/52:55:0a:00:02:02` 형식(인자 시점에는 udev 이름 변경 전이라 `eth0`)을 쓴다 — 이 형식은 이 노트에서 실행하지 않았다. `CONFIG_NETCONSOLE_EXTENDED_LOG`를 켜고 대상에 `extended=1`을 주면 시퀀스 번호·레벨이 포함된 형식으로 보내 유실·순서 문제를 수신 쪽에서 판별할 수 있다(Image-sec에서는 꺼져 있다).

### 예제 12 — systemd-journal-upload → systemd-journal-remote (게스트 안 루프백)

무엇을 확인하나: VM 호스트에는 `systemd-journal-remote`가 설치돼 있지 않아서 게스트 안에서 수신기(`journal-remote`)와 송신기(`journal-upload`)를 둘 다 돌렸다. 핵심은 **커서 기반 재개**다 — `--save-state` 파일에 마지막으로 보낸 위치를 저장하고, 다음 업로드는 거기서부터 보낸다. 이것이 "오프라인이 잦은 장비"의 store-and-forward 기본기다.

```sh
rm -rf /var/tmp/l7r; mkdir -p /var/tmp/l7r
/usr/lib/systemd/systemd-journal-remote --listen-http=19532 --output=/var/tmp/l7r/ & JR=$!
sleep 2
logger -t l7app "field event #1: imu timeout on /dev/spidev1.0"
/usr/lib/systemd/systemd-journal-upload --url=http://127.0.0.1:19532 --save-state=/var/tmp/l7r/state --follow=no 2>&1 | tail -2
echo "state file: $(head -3 /var/tmp/l7r/state)"
logger -t l7app "field event #2: imu recovered after reset"
/usr/lib/systemd/systemd-journal-upload --url=http://127.0.0.1:19532 --save-state=/var/tmp/l7r/state --follow=no 2>&1 | tail -2
kill $JR; wait $JR 2>/dev/null
ls -l /var/tmp/l7r/
journalctl --directory=/var/tmp/l7r --no-pager -t l7app -o short-monotonic
journalctl --directory=/var/tmp/l7r --no-pager -o export -t l7app -n 1 | grep -E "^(MESSAGE|_HOSTNAME|_BOOT_ID|_MACHINE_ID|__CURSOR)=" | cut -c1-90
```

```text
No more entries, closing journal.
L7> state file: # This is private data. Do not parse.
LAST_CURSOR=s=98b87f11ab9a42ec821ba5cccc0a8b39;i=1c15;b=102fc2e65196459c9ff2bf3d4ef7e0bf;m=7538fdc;t=65e162cd8922c;x=f245c8a4b2f69a8f
No more entries, closing journal.
Finishing after writing 6798 entries
total 8200
-rw-r----- 1 root root 8388608 Oct 18 04:53 remote-127.0.0.1.journal
-rw------- 1 root root     172 Oct 18 04:53 state
[  237.130711] labsysd l7app[486]: field event #1: imu timeout on /dev/spidev1.0
[  298.790488] labsysd l7app[604]: field event #2: imu recovered after reset
-- Boot 102fc2e65196459c9ff2bf3d4ef7e0bf --
[  106.002351] labsysd l7app[548]: field event #1: imu timeout on /dev/spidev1.0
[  132.839860] labsysd l7app[635]: field event #2: imu recovered after reset
__CURSOR=s=24b914dc80104db990b6f67fb566dcae;i=1a8e;b=102fc2e65196459c9ff2bf3d4ef7e0bf;m=7e
_BOOT_ID=102fc2e65196459c9ff2bf3d4ef7e0bf
_MACHINE_ID=1db677694b984127a01b305411b4cd2d
_HOSTNAME=labsysd
MESSAGE=field event #2: imu recovered after reset
```

출력에서 볼 것:

- 첫 업로드는 state 파일이 없으니 **디스크에 있는 저널 전체**(이전 부팅 것 포함)를 보냈다. 수신기가 끝날 때 `Finishing after writing 6798 entries`. 이 이미지를 두 번 부팅했기 때문에 수신 저널에 부팅 두 개(`-- Boot ... --`)의 `l7app` 항목이 다 들어 있다. 실제 장비에서 처음 연결할 때 몇 주치 로그가 한꺼번에 올라가는 장면이 이것이다 — 대역폭 상한이 필요한 이유.
- 두 번째 업로드는 `LAST_CURSOR` 다음부터, 즉 새 항목만 보냈다. 커서에는 `b=`(boot ID), `m=`(monotonic), `t=`(realtime), `i=`(seqnum)이 들어 있어 "어디까지 보냈나"가 정확하다.
- 수신 쪽 항목에 `_MACHINE_ID`, `_BOOT_ID`, `_HOSTNAME`이 붙어 있다. 서버는 이 필드로 장비·부팅을 구분한다. 첫 부팅 때 생성되는 machine-id가 **장비 ID로 쓰기에는 부적절할 수 있다**(이미지 복제·공장 초기화 시 바뀌거나 겹친다) — 제품에서는 보안 칩·eFuse의 시리얼을 따로 붙인다.
- 첫 업로드(약 6000~6800 항목)는 두 번의 실행에서 TCG 기준 26초와 61초 걸렸다. VM 부하에 따라 흔들리는 값이고 실 HW 수치가 아니다.
- `journal-upload`는 `--url=https://...`와 `--key=`/`--cert=`/`--trust=`로 상호 TLS를 지원한다. 실제 배포에서는 HTTP 평문을 쓰지 않는다.

### 8.2 store-and-forward, 속도 제한, PII

**store-and-forward**: 드론·차량·원격 센서는 대부분의 시간 오프라인이다. 원칙:

1. **먼저 로컬에 영구 저장**(journald `Storage=persistent`, pstore·core 보관 디렉터리). 업로드 성공 전에는 지우지 않는다.
2. **커서·확인응답으로 재개**. 업로드 단위는 멱등(같은 것을 두 번 보내도 서버가 중복 제거 — crash ID·journal cursor로).
3. **용량 상한과 우선순위**. journald `SystemMaxUse=`, coredump `MaxUse=`. 공간이 모자라면 오래된 info 로그부터 버리고 크래시 증거는 최대한 지킨다.
4. **연결되면 작은 것부터**: 크래시 요약(시그널·스택·build-id, 수 KiB) → pstore 로그 → core/vmcore(큰 것, Wi-Fi·도킹 때만).
5. **쓰기 수명**: 크래시 루프가 eMMC를 갉아먹지 않게 같은 크래시 시그니처는 횟수만 세고 덤프는 처음 몇 개만.

**속도 제한(rate limiting)**: 커널은 `printk_ratelimit()`·`dev_*_ratelimited()`로, journald는 `RateLimitIntervalSec=`/`RateLimitBurst=`로, 업로더는 대역폭 상한으로. 고장 난 센서가 초당 수천 줄 에러를 뿜으면 로그가 진짜 원인을 밀어내고 셀룰러 요금을 태운다. SSD 펌웨어에서 같은 에러 이벤트를 카운터로 합쳤던 것과 같은 처리.

**PII·보안**: 크래시 데이터에는 의도치 않은 것이 들어간다 — core에는 메모리 전체(카메라 프레임, 위치, 토큰), 로그에는 SSID·IP·사용자 이름, vmcore에는 키링까지. 국방·카메라 장비라면 특히:

- 수집 범위 축소(minidump, `coredump_filter`, `-d 31`로 유저 페이지 제외).
- 저장 시 암호화(dm-crypt 파티션 — L6), 전송은 상호 TLS, 서버 접근 제어·보존 기간.
- 로그 메시지에 비밀을 쓰지 않는 코딩 규칙, 업로드 전 필터(정규식 마스킹).
- 고객 동의·계약(무엇을 언제 올리는지)과 오프라인 모드(망 분리 장비는 USB 회수 절차).

---

## 9. 크래시 ID · build-id · symbol server

### 9.1 build-id가 열쇠다

필드 크래시를 해석하려면 "이 주소는 어느 빌드의 어느 함수인가"를 알아야 한다. 버전 문자열은 틀릴 수 있다(로컬 빌드, 같은 버전 재빌드). **build-id**(링커가 `.note.gnu.build-id`에 넣는 해시)는 바이너리 내용이 같으면 같고 다르면 다르다.

| 대상 | build-id가 있는 곳 | 확인 방법 |
|---|---|---|
| 유저 실행 파일·라이브러리 | ELF note | `file`, `readelf -n`, core 안의 매핑 첫 페이지 |
| 커널 | vmlinux note + VMCOREINFO `BUILD-ID=` | 예제 7의 vmcore note, `/sys/kernel/notes` |
| 커널 모듈 | `.ko` note | `readelf -n x.ko` |

### 9.2 symbol server 구조

1. CI가 빌드할 때 **strip 전 디버그 정보**(`objcopy --only-keep-debug`)를 build-id 키로 저장(`.build-id/ab/cdef....debug` 디렉터리 구조 또는 debuginfod 서버).
2. 장비에는 strip된 바이너리만 배포(용량·역공학 방지).
3. 크래시 수신 서버가 core/minidump/vmcore에서 build-id를 읽고 디버그 정보를 가져와 스택을 심볼화.
4. **debuginfod**(elfutils 프로젝트)는 이 저장소를 HTTP로 제공하는 표준 서버다. gdb·elfutils·systemd-coredump(빌드 옵션에 따라)가 `DEBUGINFOD_URLS` 환경 변수로 자동 조회한다. 예제 9에서 gdb가 `debuginfod.ubuntu.com`을 물어본 것이 이것.

커널은 같은 원리로 `vmlinux`(전체 DWARF)와 `System.map`, 모듈 `.ko.debug`를 릴리스별로 보관한다. §6.4에서 본 것처럼 `DEBUG_INFO_REDUCED`로 만든 vmlinux로는 crash가 열리지 않으니, **보관용 디버그 정보는 완전하게** 만든다(장비 이미지 크기와 무관하다).

### 9.3 크래시 ID와 중복 제거

서버에는 같은 버그의 크래시가 장비 수천 대에서 수백 번씩 올라온다. 대시보드가 쓸모 있으려면 **크래시 시그니처**로 묶어야 한다:

- 유저 크래시: (실행 파일 build-id, 시그널, 심볼화된 상위 N 프레임의 함수명) 해시.
- 커널 크래시: (커널 build-id, panic 문자열 정규화, 콜스택 상위 함수명 — 오프셋·주소·PID·타임스탬프 제거) 해시.
- 개별 발생 ID: 장비 시리얼 + boot ID + 시각.

그다음 "시그니처별 발생 수, 영향 장비 수, 첫 발생 버전, 마지막 발생 버전"으로 정렬하면 OTA 수정의 우선순위가 나온다. SSD 펌웨어 시절 RMA 분석에서 "같은 assert ID가 몇 개 드라이브에서 났나"로 분류했던 것과 같다.

---

## 10. 필드 크래시 파이프라인 설계

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="20" font-size="13">장비 위</text>
<rect x="10" y="30" width="150" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="85" y="54" font-size="13" text-anchor="middle">① 캡처</text>
<text x="85" y="74" font-size="12" text-anchor="middle">pstore · core</text>
<text x="85" y="92" font-size="12" text-anchor="middle">kdump · reset cause</text>
<rect x="180" y="30" width="150" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="255" y="54" font-size="13" text-anchor="middle">② 영구화</text>
<text x="255" y="74" font-size="12" text-anchor="middle">systemd-pstore</text>
<text x="255" y="92" font-size="12" text-anchor="middle">coredump 디렉터리</text>
<rect x="350" y="30" width="150" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="425" y="54" font-size="13" text-anchor="middle">③ 묶기</text>
<text x="425" y="74" font-size="12" text-anchor="middle">메타데이터 + 요약</text>
<text x="425" y="92" font-size="12" text-anchor="middle">build-id · 버전</text>
<rect x="520" y="30" width="150" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="595" y="54" font-size="13" text-anchor="middle">④ 업로드</text>
<text x="595" y="74" font-size="12" text-anchor="middle">연결될 때 · 재시도</text>
<text x="595" y="92" font-size="12" text-anchor="middle">작은 것 먼저 · TLS</text>
<line x1="160" y1="65" x2="178" y2="65" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="330" y1="65" x2="348" y2="65" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="500" y1="65" x2="518" y2="65" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="595" y1="100" x2="595" y2="158" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="20" y="150" font-size="13">서버</text>
<rect x="520" y="160" width="150" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="595" y="184" font-size="13" text-anchor="middle">⑤ 심볼화</text>
<text x="595" y="204" font-size="12" text-anchor="middle">debuginfod</text>
<text x="595" y="222" font-size="12" text-anchor="middle">vmlinux 보관소</text>
<rect x="350" y="160" width="150" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="425" y="184" font-size="13" text-anchor="middle">⑥ 분류</text>
<text x="425" y="204" font-size="12" text-anchor="middle">시그니처로 묶기</text>
<text x="425" y="222" font-size="12" text-anchor="middle">대시보드 · 알림</text>
<rect x="180" y="160" width="150" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="255" y="184" font-size="13" text-anchor="middle">⑦ 수정</text>
<text x="255" y="204" font-size="12" text-anchor="middle">재현 · 패치</text>
<text x="255" y="222" font-size="12" text-anchor="middle">회귀 테스트</text>
<rect x="10" y="160" width="150" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="85" y="184" font-size="13" text-anchor="middle">⑧ OTA 배포</text>
<text x="85" y="204" font-size="12" text-anchor="middle">단계적 롤아웃</text>
<text x="85" y="222" font-size="12" text-anchor="middle">시그니처 감소 확인</text>
<line x1="520" y1="195" x2="502" y2="195" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="350" y1="195" x2="332" y2="195" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="180" y1="195" x2="162" y2="195" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="85" y1="160" x2="85" y2="102" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah6)"/>
<text x="20" y="270" font-size="13">크래시 루프 방어: ①이 반복되면 bootcount가 늘어 L4의 A/B 롤백이 먼저 장비를 살린다.</text>
<text x="20" y="292" font-size="13">롤백된 장비도 ④에서 "어느 버전에서 몇 번 죽었나"를 올려야 ⑥이 회귀를 잡는다.</text>
<text x="20" y="314" font-size="13">카나리 배포(⑧)는 같은 시그니처가 줄었는지, 새 시그니처가 생겼는지를 보고 확대한다.</text>
<text x="20" y="336" font-size="13">③에는 리셋 원인 레지스터·uptime·온도·전원 전압처럼 "크래시 밖" 정보도 같이 넣는다.</text>
</svg>
```
그림 7 — 캡처부터 OTA 수정까지의 필드 크래시 파이프라인

### 10.1 부팅 때 "왜 리셋됐나" 판정 순서

장비가 다음 부팅에서 자기 리셋 이유를 분류하는 로직(예: 작은 oneshot 서비스 `crash-triage.service`, `After=systemd-pstore.service`):

1. 부트로더가 읽은 **리셋 원인**(SoC reset status 레지스터 → U-Boot 환경 변수나 DT `/chosen` 속성으로 전달, i.MX의 SRSR, TI의 PRM_RSTST 같은 것), `bootcount`.
2. `/var/lib/systemd/pstore/`에 새 dmesg 레코드가 있나 → **커널 panic/oops**. 첫 줄 `Panic#N`/`Oops#N`, panic 문자열.
3. dmesg 레코드는 없고 console/pmsg만 있나 + 리셋 원인이 watchdog → **hang 후 watchdog**. pmsg의 마지막 앱 상태가 단서.
4. `/var/crash/` 또는 kdump 디렉터리에 vmcore가 있나 → 커널 크래시 상세.
5. 아무것도 없고 리셋 원인이 POR(전원) → **전원 문제 또는 콜드 리셋**. 원격 로그의 마지막 줄·전원 텔레메트리 확인.
6. 정상 shutdown 기록(journal의 마지막 부팅이 `systemd-shutdown`으로 끝났나 — `journalctl -b -1 -n 20`) → 의도된 리부트.

결과를 작은 JSON(장비 ID, boot ID, 이전 부팅 버전, 분류, 시그니처, 파일 목록)으로 써서 업로드 큐에 넣는다.

### 10.2 설계 체크리스트 (면접에서 그대로 말할 수 있게)

| 층 | 결정 |
|---|---|
| 정책 | `panic=N`, `oops=panic`, lockup·hung task panic, HW watchdog + pretimeout(I9) |
| 캡처 | ramoops(DT 예약, ECC, 부트로더 비침범 검증), 큰 장비면 kdump, systemd-coredump 한도 |
| 영구화 | systemd-pstore, journald persistent + 용량 상한, 쓰기 수명 |
| 식별 | 장비 시리얼, boot ID, 펌웨어 버전, build-id, 크래시 시그니처 |
| 전송 | store-and-forward, 커서·멱등, 우선순위, 대역폭 상한, 상호 TLS |
| 서버 | debuginfod·vmlinux 보관, 심볼화, 시그니처 분류, 대시보드 |
| 피드백 | 재현 → 수정 → 회귀 테스트(kdump·pstore 경로 자체도 CI에서 떨어뜨려 검증) → 단계적 OTA(L4) |
| 개인정보 | 수집 최소화, 암호화, 보존 기간, 고객 동의 |

---
## 11. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="20" width="200" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="110" y="47" font-size="13" text-anchor="middle">die() / panic() / BUG()</text>
<rect x="240" y="20" width="200" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="47" font-size="13" text-anchor="middle">__crash_kexec()</text>
<rect x="470" y="20" width="200" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="570" y="40" font-size="12" text-anchor="middle">crash_save_cpu()</text>
<text x="570" y="56" font-size="12" text-anchor="middle">machine_kexec()</text>
<rect x="240" y="90" width="200" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="117" font-size="13" text-anchor="middle">kmsg_dump(reason)</text>
<rect x="470" y="90" width="200" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="570" y="110" font-size="12" text-anchor="middle">pstore_dump()</text>
<text x="570" y="126" font-size="12" text-anchor="middle">압축 → psi->write()</text>
<rect x="470" y="160" width="200" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="570" y="180" font-size="12" text-anchor="middle">ramoops_pstore_write()</text>
<text x="570" y="196" font-size="12" text-anchor="middle">persistent_ram_write()</text>
<rect x="10" y="160" width="200" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="110" y="180" font-size="12" text-anchor="middle">printk → console 드라이버</text>
<text x="110" y="196" font-size="12" text-anchor="middle">ttyAMA0 · ramoops · netcon</text>
<rect x="240" y="160" width="200" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="180" font-size="12" text-anchor="middle">netpoll_send_udp()</text>
<text x="340" y="196" font-size="12" text-anchor="middle">NIC poll (IRQ 없이)</text>
<rect x="10" y="240" width="200" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="110" y="260" font-size="12" text-anchor="middle">유저 폴트 → get_signal()</text>
<text x="110" y="276" font-size="12" text-anchor="middle">→ vfs_coredump()</text>
<rect x="240" y="240" width="200" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="260" font-size="12" text-anchor="middle">format_corename()</text>
<text x="340" y="276" font-size="12" text-anchor="middle">FILE / PIPE / SOCK</text>
<rect x="470" y="240" width="200" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="570" y="260" font-size="12" text-anchor="middle">binfmt->core_dump()</text>
<text x="570" y="276" font-size="12" text-anchor="middle">elf_core_dump()</text>
<line x1="210" y1="42" x2="238" y2="42" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="440" y1="42" x2="468" y2="42" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="110" y1="64" x2="238" y2="110" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah7)"/>
<line x1="440" y1="112" x2="468" y2="112" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="570" y1="134" x2="570" y2="158" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="110" y1="64" x2="110" y2="158" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="210" y1="182" x2="238" y2="182" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="210" y1="262" x2="238" y2="262" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="440" y1="262" x2="468" y2="262" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="20" y="318" font-size="12">점선: kdump가 로드돼 있으면 기본으로는 실행 안 됨 (post_notifiers로 순서 변경).</text>
<text x="20" y="338" font-size="12">panic 경로의 모든 쓰기는 락·인터럽트·스케줄러 없이 동작해야 한다 — 메모리 쓰기와 폴링 I/O뿐.</text>
</svg>
```
그림 8 — 크래시 순간 커널 안의 기록 경로

경로별 핵심 함수와 구조체(6.18 소스 기준):

| 경로 | 위치 | 핵심 |
|---|---|---|
| panic 순서 | `kernel/panic.c` `vpanic()` | §2.3. `panic_timeout`, `crash_kexec_post_notifiers`, `panic_notifier_list` |
| kmsg_dump | `kernel/printk/printk.c` `kmsg_dump_desc()` | 등록된 `struct kmsg_dumper`마다 `max_reason` 검사 후 `->dump()` 호출. dumper는 `kmsg_dump_get_buffer()`로 printk 링버퍼를 읽는다 |
| pstore core | `fs/pstore/platform.c` `pstore_dump()` | 압축(`pstore: Using crash dump compression: deflate`), `struct pstore_info`의 `->write()`로 backend 호출. 레코드는 `struct pstore_record` |
| ramoops | `fs/pstore/ram.c`, `ram_core.c` | `struct ramoops_context`가 zone(`struct persistent_ram_zone`)들을 들고 있다. `persistent_ram_write()`는 링버퍼에 memcpy + ECC 갱신 |
| pstore fs | `fs/pstore/inode.c` | 부팅 때 backend `->read()`로 레코드를 읽어 `/sys/fs/pstore` 파일로. `unlink` → backend `->erase()` |
| pmsg | `fs/pstore/pmsg.c` | `/dev/pmsg0` 문자 장치의 `write_pmsg()` → `psinfo->write_user()`. G4의 char device `file_operations.write` 그대로 |
| kexec 적재 | `kernel/kexec.c`(`kexec_load`), `kernel/kexec_file.c` | 세그먼트를 crashkernel 영역에 복사, `kexec_crash_image` |
| crash 진입 | `kernel/crash_core.c` `__crash_kexec()`, `arch/arm64/kernel/machine_kexec.c` | `crash_save_cpu()`가 레지스터를 `crash_notes`(per-CPU)에 저장 → 다른 CPU 정지 → 캡처 커널로 점프 |
| vmcore | `fs/proc/vmcore.c` | 캡처 커널이 `elfcorehdr=`(DT `linux,elfcorehdr`)를 읽어 `/proc/vmcore` 구성. VMCOREINFO는 `kernel/vmcore_info.c` |
| netconsole | `drivers/net/netconsole.c`, `net/core/netpoll.c` | `struct console`로 등록, `netpoll_send_udp()`가 NIC 드라이버의 `ndo_poll_controller`/NAPI poll로 직접 송신 |
| coredump | `fs/coredump.c` `vfs_coredump()`, `fs/binfmt_elf.c` `elf_core_dump()` | `format_corename()`이 pattern을 해석하고 FILE/PIPE/SOCK 분기. PIPE면 `call_usermodehelper` 계열로 helper 실행, helper 프로세스는 `RLIMIT_CORE=1`로 재귀 방지 |

드라이버를 쓸 때 다시 만나는 지점:

- **panic 경로에서 안전한 드라이버 코드.** netconsole이 쓰는 NIC 드라이버는 netpoll을 지원해야 하고(I6), mtdpstore·pstore/blk를 쓰려면 MTD/블록 드라이버에 **panic write**(인터럽트·DMA 완료 대기 없이 폴링으로 쓰는 경로)가 있어야 한다. SSD 펌웨어의 "assert 핸들러용 폴링 NAND program"과 똑같은 요구다.
- **panic notifier.** 드라이버가 `atomic_notifier_chain_register(&panic_notifier_list, ...)`로 panic 때 하드웨어를 안전 상태로 둘 수 있다(모터 정지, 레이저 끄기). 로보틱스 장비에서 중요하다. 단 kdump가 로드돼 있으면 기본으로는 안 불린다(§2.3).
- **캡처 커널에서의 장치 초기화.** 죽은 커널이 DMA를 걸어 둔 장치를 캡처 커널 드라이버가 probe하면, 진행 중이던 DMA가 캡처 커널 메모리를 덮을 수 있다. 드라이버 probe에서 장치를 먼저 리셋하는 것(`reset_devices` 힌트 존중)이 kdump 신뢰성에 직결된다(H9 DMA, G3 probe).
- **WARN/BUG 사용.** 드라이버의 `WARN_ON`은 필드에서 `panic_on_warn=1`이면 panic이 된다. 복구 가능한 조건에 `BUG_ON`을 쓰면 장비 전체가 죽는다(E5).

---

## 12. Embedded 실무에서 다시 보기

### 12.1 센서 키트 — "밤사이 한 번 리부트됐다"

i.MX8 기반 센서 키트(IMU SPI, 카메라 V4L2, LiDAR UDP)가 야외에서 하룻밤 사이 한 번 리부트됐다. 다음 날 연결되자 업로더가 보낸 묶음:

- 리셋 원인 = watchdog, bootcount = 1, 이전 부팅 uptime 14시간.
- pstore: dmesg 레코드 없음, console-ramoops 마지막 줄 `imx-sdma ... timeout`, pmsg 마지막 앱 상태 `lidar: rx stall 3 s`.
- coredump 없음.

→ panic이 아니라 커널이 반쯤 멈춘 상태로 HW watchdog을 맞았다. console-ramoops 직전 로그에 DMA 타임아웃. 다음 조치: `softlockup_panic`·`hung_task_panic` 켜기(다음엔 스택이 남는다), watchdog pretimeout을 panic governor로(I9), 원격 로그에서 같은 시간대 다른 장비의 DMA 경고 빈도 조회.

### 12.2 Jetson Orin — kdump까지 가는 장비

32 GiB Orin 기반 자율주행 컴퓨트 박스. GPU 드라이버 관련 커널 panic이 드물게 발생. RAM이 넉넉하니 `crashkernel=512M` + 캡처 initramfs(makedumpfile 포함, NVMe의 전용 파티션에 저장) + 다음 부팅에서 업로더가 Wi-Fi 도킹 시에만 vmcore(수백 MiB)를 올린다. 서버는 OTA 릴리스마다 보관한 `vmlinux`·모듈 디버그 정보로 crash를 돌려 `bt -a`·`kmem -i`·`dev -d` 결과를 티켓에 자동 첨부한다. 이때 이 노트의 교훈 두 개를 체크리스트에 넣는다: 덤프 도구가 커널 config(VA_BITS, 페이지 크기)를 지원하는지, 보관 vmlinux가 REDUCED가 아닌지.

### 12.3 256 MiB 소형 게이트웨이 — pstore만으로

AM335x급, RAM 256 MiB, NAND 256 MiB. kdump는 불가능에 가깝다. 대신:

```dts
	reserved-memory {
		#address-cells = <1>;
		#size-cells = <1>;
		ranges;

		ramoops@8ff00000 {
			compatible = "ramoops";
			reg = <0x8ff00000 0x100000>;	/* last 1 MiB of 256 MiB DRAM at 0x80000000 */
			record-size = <0x10000>;	/* 64 KiB x 8 records */
			console-size = <0x40000>;
			pmsg-size = <0x40000>;
			ecc-size = <16>;
		};
	};
```

(가이드 — 이 보드 DT는 실행하지 않았다. 주소는 예시이며 실제 보드의 DRAM 맵·U-Boot relocation 위치를 확인해야 한다.) 여기에 앱이 중요한 상태 전이를 `/dev/pmsg0`에 한 줄씩 남기고, systemd-pstore(또는 BusyBox 장비라면 init 스크립트의 `cp /sys/fs/pstore/* /data/crash/ && rm /sys/fs/pstore/*`)가 보관, 업로더가 셀룰러로 수 KiB 요약만 보낸다. 전원 차단 원인까지 잡으려면 NOR가 있는 보드에서 mtdpstore를 고려한다.

### 12.4 HIL 벤치 — netconsole로 시리얼 대신

하드웨어-인-더-루프 테스트 랙에 장비 20대. 시리얼 케이블 대신 부팅 인자 `netconsole=...@<bench-server>/...`로 모든 커널 로그를 벤치 서버의 UDP 수집기로 보낸다. 테스트 중 panic이 나도 마지막 줄까지 서버에 있다(예제 11). 테스트 프레임워크는 장비별 로그를 시험 케이스 시간 구간과 맞춰 저장한다 — Don이 Apple에서 버스 트레이스와 커널 로그를 맞추던 작업(M11)의 네트워크 버전이다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| ramoops 영역을 부트로더가 덮음 | 개발 보드에선 되는데 제품 보드에선 pstore가 늘 비어 있음 | U-Boot relocation·압축 해제 버퍼가 DRAM 끝에 | U-Boot 메모리 맵 확인, ramoops 위치 이동, warm reset 테스트를 제품 부트 체인으로 |
| 콜드 리셋 경로로 테스트 | "pstore가 동작 안 한다" | 외부 PMIC 리셋은 DRAM을 지운다 | SoC 내부 리셋(soft reset·WDT)으로 시험. 전원 차단 대비는 비휘발 backend |
| `quiet`·낮은 loglevel | console-ramoops가 비거나 빈약 | console frontend는 콘솔 레벨을 따른다 | 중요한 메시지는 높은 레벨로, 또는 pmsg/dmesg 레코드 사용 (예제 3) |
| kdump만 켜고 pstore dmesg 기대 | panic 후 `dmesg-ramoops-*` 없음 | `__crash_kexec()`가 kmsg_dump보다 먼저 | `crash_kexec_post_notifiers` 또는 vmcore-dmesg로 로그 확보 (§6.5) |
| 캡처 커널이 같은 ramoops 사용 | 보관된 console 로그가 캡처 커널 부팅 로그 | 같은 DT를 캡처 커널도 받음 | 캡처 커널에서 pstore 끄기, 캡처 단계에서 먼저 저장 |
| 덤프 도구·커널 config 불일치 | `makedumpfile Failed`, crash `no debugging data` | VA_BITS_52+4K 미지원, `DEBUG_INFO_REDUCED` | CI에서 kdump 실제 테스트, 보관용 vmlinux는 full debug info (§6.2, §6.4) |
| crashkernel 너무 작음 | 캡처 커널이 OOM으로 부팅 중 죽고 그냥 멈춤 | 드라이버·initramfs·CMA 몫 미계산 | 캡처 cmdline에 `cma=0`·`nr_cpus=1`, 크기를 실측으로 결정 |
| lockup/hung task를 panic으로 안 바꿈 | watchdog 리셋 후 증거 없음 | 기본은 경고만 | `softlockup_panic`, `hung_task_panic`, `nmi_watchdog=panic` + config 확인 |
| systemd-pstore 없이 운영 | 다음 panic이 이전 기록을 덮음, 콜드 리셋에 소실 | `/sys/fs/pstore`는 backend 그 자체 | 부팅 초기에 보관·삭제 (예제 5) |
| core 한도 없음 | 크래시 루프가 eMMC를 채우고 수명을 깎음 | coredump `MaxUse` 기본은 디스크 비율 | `ProcessSizeMax`, `MaxUse`, 동일 시그니처 덤프 수 제한 |
| 장비에 debug 심볼 없는데 서버에도 없음 | 스택이 주소뿐 | strip 전 디버그 정보 미보관 | build-id 키로 debuginfod 보관 (§9) |

---

## 14. 면접에서 이렇게 말한다

**Q.** "A device in the field rebooted overnight — how do you find out why?"

**A.** 먼저 리셋 원인을 분류한다. 부트로더가 읽은 SoC 리셋 원인(watchdog/POR/소프트웨어)과 bootcount, 그다음 pstore — dmesg 레코드가 있으면 커널 panic/oops, 없고 console·pmsg만 있으면 hang 후 watchdog, 아무것도 없고 POR면 전원 문제다. 유저 크래시는 coredump와 journal에서 unit·시그널을 본다. 원격 로그의 마지막 줄과 같은 시간대 다른 장비의 패턴도 본다. 증거가 부족했다면 다음을 위해 lockup·hung task를 panic으로 바꾸고 pretimeout을 켠다.

> First I'd classify the reset. The bootloader records the SoC reset cause and a boot counter, so I know if it was a watchdog, a power-on reset or a software reboot. Then pstore: if there's a dmesg record, it was a kernel panic or oops and I have the last log with the call trace. If there's only the console and pmsg rings and the reset cause is watchdog, the system hung, and the last console lines and the app's last pmsg state are my clues. If there's nothing and it was a power-on reset, I look at power. For userspace I check coredumpctl and the journal of the previous boot. I'd also look at the remote logs right before the reset and at other units for the same pattern. And if the evidence was thin, I'd turn soft lockups and hung tasks into panics and enable the watchdog pretimeout so the next occurrence leaves a stack.

**꼬리 질문**: "pstore가 늘 비어 있다면?" → 부트로더가 영역을 덮는지, 리셋이 DRAM을 지우는 경로인지, 콘솔 레벨, 그리고 warm reset으로 직접 재현해 확인(예제 2·4).

**Q.** "pstore vs kdump on a 256 MB board?"

**A.** 256 MiB면 pstore/ramoops다. ramoops는 1 MiB 안팎만 예약하면 panic 로그·콘솔·앱 메시지가 warm reset을 넘어 남고, panic 경로가 메모리 쓰기뿐이라 가장 안전하다. kdump는 crashkernel로 보통 수백 MiB를 평소에 놀려야 하고(실측: 1 GiB에서 256M 예약 시 가용 메모리 935 → 675 MiB), 덤프를 저장·업로드할 공간도 없다. kdump는 RAM이 넉넉한 Jetson급이나 개발 빌드에서 특정 버그를 쫓을 때 쓴다.

> On 256 MB I'd use pstore with ramoops. It costs about a megabyte of reserved RAM, survives a warm reset, and the panic path is just memory writes, which is about as safe as it gets. kdump needs a crashkernel reservation that sits idle — in my QEMU test, reserving 256 MB on a 1 GB guest dropped available memory from about 935 to 675 MB — plus storage and bandwidth for the dump. That doesn't fit a small board. I'd keep kdump for bigger platforms like a Jetson, or for a debug build when I'm chasing one specific bug that the log alone can't explain.

**Q.** "How do you get crash data off devices that are offline most of the time?"

**A.** store-and-forward다. 크래시 순간에는 ramoops·coredump로 로컬에 남기고, 다음 부팅에서 systemd-pstore 등으로 영구 저장소에 옮긴다. 업로더는 연결이 생기면 작은 요약(시그니처·build-id·버전)부터 보내고 큰 덤프는 Wi-Fi·도킹 때만 보낸다. 커서나 crash ID로 재개·중복 제거하고, 확인응답 전에는 지우지 않는다. 용량·쓰기 수명 상한, 대역폭 제한, TLS, PII 최소화가 같이 간다.

> Store and forward. At crash time, the evidence goes somewhere that survives — ramoops for the kernel, systemd-coredump for userspace — and on the next boot it's moved to persistent storage. An uploader runs whenever there's connectivity: it sends small summaries first — crash signature, build ID, versions — and the large dumps only on Wi-Fi or at the dock. Uploads are resumable and idempotent, with a cursor or crash ID, and nothing is deleted locally until the server acknowledges it. Around that you need storage caps so a crash loop doesn't wear out the eMMC, bandwidth limits, mutual TLS, and you strip what you don't need, because cores contain customer data.

**Q.** "What's the difference between netconsole and shipping journald logs?"

**A.** netconsole은 커널 콘솔 드라이버라 printk를 netpoll로 UDP 송신한다. 인터럽트·유저스페이스 없이 동작하니 panic 메시지까지 나간다. 대신 UDP라 유실되고 암호화도 없어서 랩·벤치용이다. journal 업로드는 유저스페이스 데몬이 구조화된 journald 항목을 HTTPS로 보내고 커서로 재개한다. 필드용이지만 panic 순간의 메시지는 못 보낸다 — 그건 pstore로 다음 부팅에 보낸다.

> netconsole is a kernel console that sends printk output as UDP through netpoll, so it works with interrupts off and even prints the whole panic. But it's UDP — no delivery guarantee, no encryption — so I use it on test benches. journal-upload is a userspace service that ships structured journald entries over HTTPS and resumes from a saved cursor. That's what goes to the field, but it can't send anything once the kernel has panicked; for that I rely on pstore and upload the record on the next boot.

**Q.** "Your kdump setup produced a vmcore but makedumpfile and crash both fail. What do you check?"

**A.** 도구와 커널의 조합이다. makedumpfile은 VMCOREINFO로 메모리 레이아웃을 해석하니 VA_BITS·페이지 크기 지원 여부(내 실험에서는 arm64 4K + 52비트 VA를 1.7.7도 거부), crash는 같은 build-id의 full debug info vmlinux가 필요하다(`DEBUG_INFO_REDUCED`면 거부). 그 사이에도 `vmcore-dmesg`로 로그는 뽑을 수 있다. 근본적으로는 kdump 경로를 CI에서 정기적으로 떨어뜨려 본다.

> I'd check the tool against the kernel config. makedumpfile parses the memory layout from VMCOREINFO, so page size and VA bits have to be supported — in my lab, makedumpfile 1.7.7 refused an arm64 kernel built with 52-bit VA and 4K pages. crash needs the matching vmlinux with full debug info; with DEBUG_INFO_REDUCED it says there's no debugging data. Meanwhile vmcore-dmesg can still pull the kernel log out of the raw dump. And the real fix is process: crash the kernel on purpose in CI and open the dump, so a config change can't silently break the path.

**Q.** "Where does a userspace core dump go on a systemd system, and how do you keep it from filling the disk?"

**A.** `kernel.core_pattern`이 `|systemd-coredump`라 커널이 core를 파이프로 넘긴다. 파이프 모드는 RLIMIT_CORE를 무시하고, systemd-coredump가 메타데이터는 journal에, 압축된 core는 `/var/lib/systemd/coredump`에 둔다. `coredumpctl list/info/dump`로 본다. 한도는 `coredump.conf`의 `ProcessSizeMax`·`ExternalSizeMax`·`MaxUse`·`KeepFree`로 건다 — 넘으면 truncated로 저장된다.

> The kernel's core_pattern starts with a pipe to systemd-coredump, so the kernel streams the core to it and ignores RLIMIT_CORE in pipe mode. systemd-coredump puts the metadata — PID, signal, unit, command line, a stack trace if it can — into the journal, and the compressed core into /var/lib/systemd/coredump. You inspect it with coredumpctl list, info and dump. To bound it, coredump.conf has ProcessSizeMax and ExternalSizeMax per dump and MaxUse and KeepFree for the directory; a dump over the limit gets stored truncated. On flash I'd also cap how many dumps of the same signature we keep.

**Q.** "How do you make a kernel panic leave useful evidence on a product?"

**A.** 정책과 수집을 같이 설계한다. `panic=N`으로 자동 리부트, `oops=panic`으로 반쯤 죽은 커널로 돌지 않게, lockup·hung task를 panic으로. 수집은 ramoops(DT 예약, ECC)와 큰 장비면 kdump, 그리고 다음 부팅에 systemd-pstore로 보관하고 업로드. 크래시 루프는 bootcount로 A/B 롤백(L4)에 연결한다.

> Policy and capture together. Policy: panic=N so it reboots by itself, oops=panic so it doesn't keep running half-broken, and soft lockups and hung tasks promoted to panics. Capture: a ramoops region in the device tree with ECC, kdump if the board has the RAM. On the next boot, systemd-pstore archives the records and an uploader ships them. And if it panics in a loop, the boot counter triggers the A/B rollback, so the device comes back on the previous image and still reports what happened.

---

## 15. 직접 해보기

1. **개념**: 1 MiB ramoops에서 `record-size=0x10000`, `console-size=0x40000`, `pmsg-size=0x20000`, `ftrace-size=0`이면 dmesg 레코드는 몇 개인가?
   정답/힌트: (1024 − 256 − 128) KiB ÷ 64 KiB = 10개.
2. **실험**: 예제 2를 `panic=3` 대신 `panic=0`으로 돌리고 QEMU monitor의 `system_reset`으로 리셋해도 dmesg 레코드가 남는지 확인하라. 남는다면 왜인가?
   정답/힌트: kmsg_dump는 panic 직후(리부트 대기 전)에 이미 썼다. 리셋을 누가 일으키든 RAM이 보존되면 남는다.
3. **실험**: 예제 2를 세 번 연속 panic하게(스크립트에서 레코드 개수를 세어 3개가 될 때까지 crash) 바꾸고 `dmesg-ramoops-0..2`의 첫 줄 `Panic#N`을 확인하라.
   정답/힌트: 레코드가 순환하므로 4개를 넘기면 가장 오래된 것이 덮인다. systemd-pstore 없이는 이력이 사라진다.
4. **코드**: pmsg에 "마지막 상태"를 남기는 C 함수 `void pmsg_note(const char *fmt, ...)`를 작성하라(`/dev/pmsg0`을 `O_WRONLY`로 열어 `vdprintf`). 센서 루프의 상태 전이마다 호출하는 데몬을 만들고 예제 3처럼 정지시켜 보라.
   정답/힌트: pmsg는 링버퍼라 오래된 줄이 밀린다. 한 줄에 타임스탬프·시퀀스를 넣고, 매 루프가 아니라 상태가 바뀔 때만 쓴다.
5. **관찰**: 게스트에서 `cat /proc/sys/kernel/core_pattern`, `cat /proc/self/coredump_filter`, `systemd-analyze cat-config systemd/coredump.conf`를 보고, `coredump_filter`를 바꿔 예제 10의 64 MiB 힙이 core에서 빠지게 해 보라.
   정답/힌트: 비트 0(anonymous private)을 끄면 힙이 빠진다. `echo 0x32 > /proc/self/coredump_filter` 후 실행(자식이 상속).
6. **kdump**: 커널을 `ARM64_VA_BITS_48`로 다시 빌드해 예제 6을 반복하고 `makedumpfile -d 31` 결과 크기를 원본 vmcore와 비교하라. `-d 1`, `-d 17`도 비교하라.
   정답/힌트: 0 페이지와 free 페이지만 빼도 대부분 줄어든다. 이 노트 §6.4의 재빌드 결과를 참고.
7. **설계**: 셀룰러로 하루 1 MiB만 쓸 수 있는 드론 500대의 크래시 업로드 정책을 표로 써라(무엇을, 어떤 순서로, 얼마나).
   정답/힌트: 요약(수 KiB, 즉시) → pstore dmesg(압축 수십 KiB, 즉시) → core(도킹 시) → 같은 시그니처 반복은 카운트만.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| pstore | persistent store | 크래시 기록을 리셋 너머로 남기는 커널 프레임워크, `/sys/fs/pstore`로 노출 |
| ramoops | RAM backend for pstore | 예약 DRAM을 zone으로 나눠 dmesg·console·pmsg·ftrace 기록 |
| PRZ | persistent RAM zone | ramoops의 링버퍼 하나 (sig·start·size 헤더 + 데이터 + ECC) |
| pmsg | persistent message | 유저가 `/dev/pmsg0`에 쓰는 메시지를 ramoops에 남기는 frontend |
| kmsg_dump | kernel message dump | panic/oops 때 printk 링버퍼를 dumper(pstore 등)에 넘기는 훅 |
| systemd-pstore | — | 부팅 시 pstore 레코드를 `/var/lib/systemd/pstore`로 옮기는 서비스 |
| kdump | kernel crash dump | panic 시 미리 올린 캡처 커널로 점프해 메모리 전체를 덤프 |
| kexec | kernel execute | 실행 중인 커널에서 펌웨어 없이 다른 커널로 바로 부팅 |
| crashkernel= | — | 캡처 커널용 메모리를 부팅 때 예약하는 커널 인자 |
| vmcore | — | 캡처 커널이 `/proc/vmcore`로 보여 주는 죽은 커널 메모리 (ELF core) |
| VMCOREINFO | — | 1차 커널이 남긴 심볼 주소·구조체 오프셋 표, 덤프 도구가 해석에 사용 |
| elfcorehdr | — | 죽은 커널 메모리 지도(ELF 헤더 + CPU note), 캡처 커널에 전달 |
| makedumpfile | — | vmcore에서 불필요한 페이지를 빼고 압축하는 도구 (`-d 31`) |
| crash | — | gdb 기반 커널 덤프 분석기 (`bt`, `log`, `ps`, `kmem`) |
| core_pattern | — | 유저 core를 파일/파이프/소켓 중 어디로 보낼지 정하는 sysctl |
| systemd-coredump | — | core_pattern 파이프 helper, 메타데이터는 journal, core는 압축 파일 |
| minidump | — | 스택·레지스터·모듈 목록만 담은 작은 덤프 (Breakpad/Crashpad) |
| netconsole | — | printk를 netpoll로 UDP 송신하는 콘솔 드라이버 |
| netpoll | — | 인터럽트 없이 NIC를 폴링해 패킷을 보내는 커널 기능 |
| journal-upload / journal-remote | — | journald 항목을 HTTP(S)로 보내고 받는 systemd 도구 쌍 |
| build-id | — | ELF note의 바이너리 해시, 디버그 정보 매칭 키 |
| debuginfod | — | build-id로 디버그 정보를 HTTP 제공하는 서버 (elfutils) |
| store-and-forward | — | 로컬에 먼저 저장하고 연결될 때 전송하는 방식 |
| warm / cold reset | — | DRAM 내용이 유지되는 리셋 / 전원 재투입으로 지워지는 리셋 |

---

## 17. 요약 & 체크리스트

필드 크래시 수집은 SSD 펌웨어의 "죽는 순간 살아남는 곳에 쓰고, 다음 부팅에 영구화하고, 밖으로 꺼낸다"를 Linux 계층별로 나눈 것이다. 커널은 먼저 정책(`panic=`, `oops=panic`, lockup panic)으로 반드시 panic·리부트하게 만들고, panic 순간 kmsg_dump → pstore/ramoops가 예약 DRAM에 압축 로그를 남긴다. console·pmsg 링버퍼는 panic 없는 정지에서도 마지막 줄을 남긴다. 이 기록은 warm reset에서만 살아남고(QEMU에서 직접 확인), systemd-pstore가 다음 부팅에 디스크로 옮긴다. 메모리 전체가 필요하면 kdump — crashkernel 예약 + 캡처 커널 + vmcore — 인데, RAM·저장 공간 비용 때문에 큰 장비용이고 덤프 도구와 커널 config 궁합까지 검증해야 한다(이 랩에서 VA_BITS·DEBUG_INFO_REDUCED로 실제로 깨졌다). 유저 크래시는 core_pattern 파이프 → systemd-coredump → coredumpctl, 한도는 coredump.conf로. 로그는 netconsole(벤치, panic까지)과 journal-upload(필드, 커서 재개)로 꺼내고, 전체를 store-and-forward·build-id 심볼화·시그니처 분류·OTA 수정의 파이프라인으로 묶는다.

- [ ] 필드 크래시 종류별로 무엇이 증거로 남는지 표로 말할 수 있다
- [ ] panic 경로의 순서(kdump → notifier → kmsg_dump → 리부트)와 `crash_kexec_post_notifiers`의 의미를 설명할 수 있다
- [ ] DT reserved-memory에 ramoops 노드를 넣고 크기 배분으로 레코드 개수를 계산할 수 있다
- [ ] ramoops가 warm reset에서만 살아남는 이유와 부트로더·DDR 초기화 함정을 설명할 수 있다
- [ ] `/sys/fs/pstore`의 dmesg·console·pmsg 레코드를 읽고 systemd-pstore 보관 흐름을 설정할 수 있다
- [ ] crashkernel 예약·`kexec -p`·캡처 스크립트로 vmcore를 저장하는 kdump 경로를 구성할 수 있다
- [ ] makedumpfile 덤프 레벨과, 도구·커널 config 불일치로 분석이 실패하는 경우를 설명할 수 있다
- [ ] core_pattern의 파일/파이프 차이와 RLIMIT_CORE·coredump.conf 한도를 설명할 수 있다
- [ ] coredumpctl로 크래시를 찾고 core를 꺼내 gdb로 bt를 볼 수 있다
- [ ] netconsole을 configfs로 설정해 panic 메시지를 원격으로 받을 수 있다
- [ ] journal-upload의 커서 기반 재개로 store-and-forward를 설명할 수 있다
- [ ] build-id·debuginfod·크래시 시그니처로 서버 쪽 분류 파이프라인을 설계할 수 있다

---

## 참고 자료

- docs.kernel.org — Ramoops oops/panic logger: https://docs.kernel.org/admin-guide/ramoops.html
- docs.kernel.org — Documentation for Kdump: https://docs.kernel.org/admin-guide/kdump/kdump.html
- docs.kernel.org — Persistent storage for a kernel's dying breath (pstore-blk): https://docs.kernel.org/admin-guide/pstore-blk.html
- docs.kernel.org — Netconsole: https://docs.kernel.org/networking/netconsole.html
- docs.kernel.org — Documentation for /proc/sys/kernel/ (panic, panic_on_oops, core_pattern …): https://docs.kernel.org/admin-guide/sysctl/kernel.html
- DT 바인딩 `Documentation/devicetree/bindings/reserved-memory/ramoops.yaml` (커널 소스)
- 커널 소스 (6.18): `kernel/panic.c`, `fs/pstore/ram.c`, `fs/pstore/ram_core.c`, `fs/pstore/platform.c`, `fs/coredump.c`, `kernel/crash_core.c`, `fs/proc/vmcore.c`, `drivers/net/netconsole.c` — https://elixir.bootlin.com/linux/v6.18/source
- man7.org — core(5): https://man7.org/linux/man-pages/man5/core.5.html
- systemd man pages — systemd-pstore(8), systemd-coredump(8), coredump.conf(5), coredumpctl(1), systemd-journal-upload(8), systemd-journal-remote(8): https://www.freedesktop.org/software/systemd/man/
- crash utility: https://crash-utility.github.io/
- makedumpfile: https://github.com/makedumpfile/makedumpfile
- elfutils debuginfod: https://sourceware.org/elfutils/Debuginfod.html
- Michael Kerrisk, "The Linux Programming Interface" — 22장(시그널 고급, core dump)
