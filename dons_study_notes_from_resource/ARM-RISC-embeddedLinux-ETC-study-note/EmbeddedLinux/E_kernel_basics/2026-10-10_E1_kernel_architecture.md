# E1. 커널 아키텍처 개요 — 서브시스템 지도와 소스 트리 길찾기

> **이 노트를 다 읽으면**: Linux가 왜 "모듈을 가진 monolithic 커널"인지 RTOS·마이크로커널과 비교해 말할 수 있다 · 프로세스/메모리/VFS/네트워크/드라이버/arch 서브시스템이 어떻게 맞물리는지 한 장 지도로 그릴 수 있다 · 3,600만 줄짜리 소스 트리에서 함수 정의·호출자·담당자·크래시 주소의 소스 줄을 몇 분 안에 찾을 수 있다 · `/dev/ttyAMA0`의 `read()`가 `svc`에서 PL011 레지스터까지 어떤 파일을 거치는지 file:line으로 짚을 수 있다
> **왜 필요한가**: 면접에서는 "monolithic vs microkernel"과 "X 드라이버 코드는 어디 있나"가 거의 반드시 나오고, 입사 첫 주에는 벤더 BSP 커널 트리를 받아 "이 보드의 UART/I2C/카메라 코드가 어디 있지?"부터 시작한다
> **Don 기준 난이도**: 레이어 구조·인터럽트·레지스터·링버퍼는 이미 안다 / 새로 배울 부분은 "커널 전체가 한 주소 공간의 거대한 C 프로그램이고, 서브시스템끼리는 함수 포인터 표(ops)로 연결된다"는 감각과 그 트리를 다루는 도구들
> **선행 노트**: A1(Linux 전체 구조), A3(파일 I/O와 디바이스 접근), C6(커널 시작 과정) — E2(설정과 빌드)·E3(커널 모듈)·H8(UART·TTY)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don의 SSD 펌웨어를 떠올려 보자. 소스 트리는 아마 이랬을 것이다: `hal/`(레지스터 접근), `drivers/`(NAND, PCIe, DMA), `ftl/`(매핑 테이블), `host/`(NVMe 명령 처리), `rtos/`(태스크·큐). 빌드하면 **ELF 하나**가 나오고, 그 안에서 모든 코드는 서로의 함수를 그냥 부른다. 새 사람이 오면 "NVMe Read 명령이 들어와서 NAND에서 데이터가 나가기까지 어떤 파일을 거치나"를 따라가 보게 하는 게 첫 숙제다.

Linux 커널도 정확히 같은 성격의 물건이다. 다만 크기가 다르다. 이 노트의 랩 트리(Linux 6.18.55)는 **파일 91,154개, C·헤더·어셈블리·Rust 소스 3,637만 줄**이다(§3에서 직접 센다). 그리고 구조가 정해진 레이어로 나뉘어 있다.

```svg
<svg viewBox="0 0 680 446" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="10" width="640" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="32" y="36" font-size="13" font-weight="bold">유저 공간</text>
  <text x="32" y="54" font-size="12">EL0</text>
  <rect x="130" y="20" width="150" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="205" y="44" text-anchor="middle" font-size="12">센서 데몬 · 셸</text>
  <rect x="295" y="20" width="160" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="375" y="44" text-anchor="middle" font-size="12">cat /dev/ttyAMA0</text>
  <rect x="470" y="20" width="180" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="560" y="44" text-anchor="middle" font-size="12">libc: read() → svc #0</text>
  <rect x="20" y="82" width="640" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="104" text-anchor="middle" font-size="12">syscall 경계 — el0t_64_sync → el0_svc → sys_call_table[x8]  (arch/arm64/kernel)</text>
  <rect x="20" y="128" width="640" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="145" font-size="12" font-weight="bold">커널 코어 서브시스템 — 한 주소 공간에서 서로 함수를 직접 부른다</text>
  <rect x="30" y="152" width="96" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="78" y="171" text-anchor="middle" font-size="12">스케줄러</text>
  <text x="78" y="190" text-anchor="middle" font-size="12">kernel/sched</text>
  <rect x="134" y="152" width="96" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="182" y="171" text-anchor="middle" font-size="12">메모리 관리</text>
  <text x="182" y="190" text-anchor="middle" font-size="12">mm/</text>
  <rect x="238" y="152" width="96" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="286" y="171" text-anchor="middle" font-size="12">VFS</text>
  <text x="286" y="190" text-anchor="middle" font-size="12">fs/</text>
  <rect x="342" y="152" width="96" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="390" y="171" text-anchor="middle" font-size="12">네트워크</text>
  <text x="390" y="190" text-anchor="middle" font-size="12">net/</text>
  <rect x="446" y="152" width="96" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="494" y="171" text-anchor="middle" font-size="12">IPC</text>
  <text x="494" y="190" text-anchor="middle" font-size="12">ipc/</text>
  <rect x="550" y="152" width="96" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="598" y="171" text-anchor="middle" font-size="12">보안 LSM</text>
  <text x="598" y="190" text-anchor="middle" font-size="12">security/</text>
  <rect x="20" y="220" width="640" height="76" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="237" font-size="12" font-weight="bold">드라이버 프레임워크 — 드라이버가 등록하는 자리</text>
  <rect x="30" y="244" width="117" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="88" y="262" text-anchor="middle" font-size="12">TTY · serial</text>
  <text x="88" y="280" text-anchor="middle" font-size="12">drivers/tty</text>
  <rect x="155" y="244" width="117" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="213" y="262" text-anchor="middle" font-size="12">block</text>
  <text x="213" y="280" text-anchor="middle" font-size="12">block/ blk-mq</text>
  <rect x="280" y="244" width="117" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="338" y="262" text-anchor="middle" font-size="12">netdev</text>
  <text x="338" y="280" text-anchor="middle" font-size="12">net/core</text>
  <rect x="405" y="244" width="117" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="463" y="262" text-anchor="middle" font-size="12">input · IIO</text>
  <text x="463" y="280" text-anchor="middle" font-size="12">drivers/iio</text>
  <rect x="530" y="244" width="117" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="588" y="262" text-anchor="middle" font-size="12">driver core</text>
  <text x="588" y="280" text-anchor="middle" font-size="12">drivers/base</text>
  <rect x="20" y="308" width="640" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="330" text-anchor="middle" font-size="12">디바이스 드라이버 (drivers/*) — 예: amba-pl011.c · virtio_blk.c · gpio-pl061.c</text>
  <rect x="20" y="354" width="640" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="376" text-anchor="middle" font-size="12">아키텍처 층 (arch/arm64) — 예외 벡터 · 페이지 테이블 · 컨텍스트 스위치 · SMP</text>
  <rect x="20" y="400" width="640" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="422" text-anchor="middle" font-size="12">하드웨어 — Cortex-A57 · GIC · PL011 UART · RAM · virtio-mmio (QEMU virt)</text>
  <line x1="340" y1="70" x2="340" y2="81" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="116" x2="340" y2="127" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="208" x2="340" y2="219" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="296" x2="340" y2="307" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="342" x2="340" y2="353" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="388" x2="340" y2="399" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 커널을 위에서 아래로 자른 단면. 유저 공간에서 `svc`로 들어온 요청은 arch 층의 예외 진입을 지나 코어 서브시스템(VFS 등)으로 가고, 서브시스템 프레임워크를 거쳐 개별 드라이버, 다시 arch 층(MMIO·IRQ)을 지나 하드웨어에 닿는다. §6에서 이 단면을 `read()` 하나로 실제로 관통한다.

펌웨어 비유로 정리하면 이렇다.

| 펌웨어 (Don의 SSD) | Linux 커널 | 이 노트에서 |
|---|---|---|
| `hal/`, 스타트업 어셈블리, 벡터 테이블 | `arch/arm64/` (entry.S, head.S, mm/) | §3.2 |
| `drivers/nand/`, `drivers/pcie/` | `drivers/` (143개 하위 디렉터리) | §3.3 |
| `rtos/` (태스크, 큐, 세마포어) | `kernel/` (sched, locking, irq, time) | §2 |
| `ftl/` (매핑, GC) | `mm/` + `fs/` + `block/` | §2 |
| `host/` NVMe 명령 디코더 | syscall 테이블 + `fs/read_write.c` 같은 진입 함수 | §6 |
| 공용 헤더 `inc/` | `include/linux/`, `include/uapi/` | §3.4 |
| `.map` 파일, `objdump` | `System.map`, `vmlinux`, `/proc/kallsyms` | §5 |
| "이 모듈 담당자 누구?" (사내 위키) | `MAINTAINERS` + `scripts/get_maintainer.pl` | §4.5 |

이 노트의 순서는 다음과 같다. §1 커널 구조의 종류 → §2 서브시스템 지도 → §3 소스 트리 투어(실측) → §4 코드 길찾기 도구 → §5 빌드된 이미지 해부 → §6 `read()` 한 번을 끝까지 따라가기 → §7 서브시스템을 잇는 ops 표.

> 실행 환경: 모든 명령은 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안에서 돌렸다. 소스 측정은 공용 랩의 `/var/lab/src/linux-6.18.55`, 빌드 산출물 측정은 `/var/lab/build/linux`(같은 커널의 O= 빌드 트리, `vmlinux`·`System.map`·`.config`·`.ko` 포함)에서 했다. 게스트 실험은 QEMU `virt`(cortex-a57, 512 MB, TCG)로 부팅한 **게스트 커널 `6.18.55`**(BusyBox initramfs)에서 돌렸다. 랩은 읽기 전용으로만 썼고, 산출물은 VM의 `/var/tmp/E1/`에 만들었다. 주소(KASLR)·타이밍은 실행마다 다르다.

---

## 1. Monolithic 커널 + 모듈 — RTOS·마이크로커널과 무엇이 다른가

### 1.1 직관 — "누가 같은 주소 공간에 사나"

A1에서 한 번 봤던 비교를 커널 소스 관점에서 다시 본다. OS 구조를 가르는 질문은 하나다: **드라이버와 파일시스템이 커널과 같은 주소 공간·같은 특권으로 도는가?**

```svg
<svg viewBox="0 0 680 376" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="120" y="24" text-anchor="middle" font-size="13" font-weight="bold">Linux (monolithic + LKM)</text>
  <text x="340" y="24" text-anchor="middle" font-size="13" font-weight="bold">마이크로커널 (QNX, seL4)</text>
  <text x="560" y="24" text-anchor="middle" font-size="13" font-weight="bold">RTOS (FreeRTOS, Zephyr)</text>
  <line x1="20" y1="150" x2="445" y2="150" stroke="currentColor" stroke-dasharray="5,4"/>
  <rect x="35" y="56" width="80" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="75" y="79" text-anchor="middle" font-size="12">앱</text>
  <rect x="125" y="56" width="80" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="79" text-anchor="middle" font-size="12">앱</text>
  <line x1="120" y1="96" x2="120" y2="163" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="128" y="130" font-size="12">syscall</text>
  <rect x="30" y="165" width="180" height="165" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="183" text-anchor="middle" font-size="12" font-weight="bold">커널 주소 공간 (EL1)</text>
  <rect x="42" y="192" width="156" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="212" text-anchor="middle" font-size="12">sched·mm·VFS·net</text>
  <rect x="42" y="228" width="156" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="248" text-anchor="middle" font-size="12">빌트인 드라이버</text>
  <rect x="42" y="264" width="156" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4,3"/>
  <text x="120" y="284" text-anchor="middle" font-size="12">모듈 .ko (insmod)</text>
  <text x="120" y="318" text-anchor="middle" font-size="12">모두 같은 권한·주소 공간</text>
  <rect x="250" y="40" width="88" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="294" y="62" text-anchor="middle" font-size="12">앱</text>
  <rect x="352" y="40" width="88" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="396" y="62" text-anchor="middle" font-size="12">FS 서버</text>
  <rect x="250" y="84" width="88" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="294" y="106" text-anchor="middle" font-size="12">UART 드라이버</text>
  <rect x="352" y="84" width="88" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="396" y="106" text-anchor="middle" font-size="12">net 서버</text>
  <line x1="300" y1="122" x2="300" y2="168" stroke="currentColor" marker-end="url(#ah2)" marker-start="url(#ah2)"/>
  <line x1="380" y1="122" x2="380" y2="168" stroke="currentColor" marker-end="url(#ah2)" marker-start="url(#ah2)"/>
  <rect x="265" y="170" width="150" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="193" text-anchor="middle" font-size="12" font-weight="bold">마이크로커널</text>
  <text x="340" y="213" text-anchor="middle" font-size="12">IPC · 스케줄 · MMU</text>
  <text x="340" y="252" text-anchor="middle" font-size="12">서버끼리 메시지(IPC)로 통신,</text>
  <text x="340" y="270" text-anchor="middle" font-size="12">커널은 전달만 한다</text>
  <rect x="470" y="40" width="180" height="290" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560" y="60" text-anchor="middle" font-size="12" font-weight="bold">한 이미지 · 한 주소 공간</text>
  <rect x="485" y="74" width="150" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="560" y="95" text-anchor="middle" font-size="12">태스크 A</text>
  <rect x="485" y="114" width="150" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="560" y="135" text-anchor="middle" font-size="12">태스크 B</text>
  <rect x="485" y="154" width="150" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="175" text-anchor="middle" font-size="12">커널: 스케줄러·큐</text>
  <rect x="485" y="194" width="150" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560" y="215" text-anchor="middle" font-size="12">드라이버 = 함수</text>
  <rect x="485" y="234" width="150" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="560" y="255" text-anchor="middle" font-size="12">HAL·레지스터 직접</text>
  <text x="560" y="300" text-anchor="middle" font-size="12">MPU로 태스크만 선택 격리</text>
  <text x="120" y="360" text-anchor="middle" font-size="12">드라이버 버그 = 커널 패닉</text>
  <text x="340" y="360" text-anchor="middle" font-size="12">드라이버 죽어도 재시작 가능</text>
  <text x="560" y="360" text-anchor="middle" font-size="12">특권 분리가 거의 없음</text>
</svg>
```
그림 2 — 세 가지 OS 구조. 점선은 특권 경계(EL0/EL1). Linux는 드라이버까지 커널 주소 공간에 두고 모듈로 "나중에 링크"할 수 있게 했다. 마이크로커널은 드라이버를 유저 프로세스로 내보낸다. RTOS는 보통 경계 자체가 없다.

### 1.2 정의

- **Monolithic kernel**: 스케줄러, 메모리 관리, 파일시스템, 네트워크 스택, 디바이스 드라이버가 **하나의 커널 주소 공간에서 같은 특권 레벨(EL1)로** 돌며 서로를 **직접 함수 호출**한다. Linux, 전통 BSD.
- **Loadable kernel module (LKM)**: 커널이 돌고 있는 중에 `.ko` 파일을 읽어 **커널 주소 공간에 링크해 넣는** 메커니즘. 들어간 뒤에는 빌트인 코드와 구별되지 않는다. 격리가 아니라 **지연 링크**다(§5.5에서 모듈 심볼이 `/proc/kallsyms`에 빌트인 심볼과 나란히 나타나는 것을 확인한다).
- **Microkernel**: 커널에는 주소 공간·스레드·IPC·스케줄링만 남기고, 드라이버·파일시스템·네트워크는 **유저 공간 서버 프로세스**로 돌린다. QNX Neutrino, seL4, (macOS XNU는 Mach 기반이지만 BSD·드라이버를 커널 안에 둔 hybrid다).
- **RTOS (FreeRTOS, Zephyr 등)**: 애플리케이션과 커널이 **하나의 이미지로 링크**되고 대개 한 주소 공간에서 돈다. MMU 없이 MPU로 일부 영역만 보호하는 경우가 많다. Don이 쓰던 펌웨어가 이쪽이다.

### 1.3 비교 표

| 항목 | Linux (monolithic + LKM) | 마이크로커널 (QNX, seL4) | RTOS (FreeRTOS, Zephyr) |
|---|---|---|---|
| 드라이버가 사는 곳 | 커널 주소 공간 (EL1) | 유저 프로세스 (EL0) | 앱과 같은 이미지 |
| 서브시스템 간 호출 | 직접 함수 호출 (수 ns) | 메시지 IPC + 컨텍스트 스위치 | 직접 함수 호출 |
| 드라이버 버그의 영향 | Oops/panic, 커널 전체 위험 | 그 서버만 죽고 재시작 가능 | 시스템 전체 리셋 |
| 런타임 확장 | `insmod`/`modprobe` | 서버 프로세스 실행 | 보통 불가 (재빌드·재플래시) |
| 인증·검증 | 어렵다 (코드 수천만 줄) | 쉽다 (seL4는 형식 검증) | 작아서 상대적으로 쉽다 |
| 드라이버 생태계 | 압도적 (`drivers/` 2,518만 줄) | 벤더 제공 위주 | 벤더 HAL 위주 |
| 실시간성 | PREEMPT_RT로 개선 (N 모듈) | 설계상 강함 | 설계 목표 |

### 1.4 Don 경험과 연결 — "RTOS에서 Linux로 오면 무엇이 바뀌나"

- RTOS에서 드라이버는 "태스크가 부르는 함수 묶음"이었다. Linux에서도 드라이버는 결국 커널 안의 함수 묶음이다. **바뀌는 것은 누가 그 함수를 부르는가**다: 유저의 `read()`가 VFS를 거쳐 드라이버의 `.read_iter`를 부르고(§6), 인터럽트는 GIC 드라이버를 거쳐 드라이버의 handler를 부르고, 장치 발견은 driver core가 드라이버의 `probe()`를 부른다(D3).
- RTOS에서 메모리 보호가 MPU 영역 몇 개였다면, Linux에서는 **유저↔커널 경계만 강하게** 지키고 커널 내부는 서로를 완전히 믿는다. 그래서 드라이버의 포인터 버그 하나가 파일시스템 메모리를 덮어쓸 수 있다. 이것이 면접에서 "커널 코드는 왜 더 조심해야 하나"(E4)의 근거다.
- 모듈은 펌웨어로 치면 "부트로더가 나중에 RAM에 올려 주는 오버레이 코드"에 가깝다. 다만 커널이 직접 재배치(relocation)와 심볼 해석을 해 준다(E3).

### 1.5 흔한 함정

- "모듈로 넣으면 격리된다"는 오해. `.ko`는 들어가는 순간 커널과 같은 권한이다. 격리를 원하면 UIO/VFIO 같은 유저 공간 드라이버(D3 §12.3)나 별도 코어(O 모듈)를 쓴다.
- "Linux는 monolithic이라 모듈식 설계가 아니다"도 틀린 말이다. **바이너리 구조**가 monolithic일 뿐, **소스 설계**는 ops 표와 프레임워크로 강하게 나뉘어 있다(§7).

---

## 2. 서브시스템 지도 — 큰 덩어리와 그 관계

### 2.1 지도

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="180" y="12" width="320" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="34" text-anchor="middle" font-size="12" font-weight="bold">시스템 콜 인터페이스 (번호 공간 470개)</text>
  <rect x="20" y="80" width="118" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="79" y="99" text-anchor="middle" font-size="12">프로세스·스케줄러</text>
  <text x="79" y="118" text-anchor="middle" font-size="12">kernel/ sched/</text>
  <rect x="152" y="80" width="118" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="211" y="99" text-anchor="middle" font-size="12">메모리 관리</text>
  <text x="211" y="118" text-anchor="middle" font-size="12">mm/</text>
  <rect x="284" y="80" width="118" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="343" y="99" text-anchor="middle" font-size="12">VFS</text>
  <text x="343" y="118" text-anchor="middle" font-size="12">fs/*.c</text>
  <rect x="416" y="80" width="118" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="475" y="99" text-anchor="middle" font-size="12">네트워크 스택</text>
  <text x="475" y="118" text-anchor="middle" font-size="12">net/</text>
  <rect x="548" y="80" width="118" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="607" y="99" text-anchor="middle" font-size="12">IPC</text>
  <text x="607" y="118" text-anchor="middle" font-size="12">ipc/</text>
  <rect x="20" y="168" width="118" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="79" y="186" text-anchor="middle" font-size="12">IRQ·시간·워크큐</text>
  <text x="79" y="204" text-anchor="middle" font-size="12">kernel/irq 등</text>
  <rect x="152" y="168" width="118" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="211" y="186" text-anchor="middle" font-size="12">slab·페이지 할당</text>
  <text x="211" y="204" text-anchor="middle" font-size="12">mm/slub.c 등</text>
  <rect x="284" y="168" width="118" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="343" y="186" text-anchor="middle" font-size="12">파일시스템</text>
  <text x="343" y="204" text-anchor="middle" font-size="12">ext4·proc·sysfs</text>
  <rect x="416" y="168" width="118" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="475" y="186" text-anchor="middle" font-size="12">프로토콜</text>
  <text x="475" y="204" text-anchor="middle" font-size="12">ipv4·ipv6·can</text>
  <rect x="548" y="168" width="118" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="607" y="186" text-anchor="middle" font-size="12">보안 LSM 훅</text>
  <text x="607" y="204" text-anchor="middle" font-size="12">security/</text>
  <rect x="20" y="250" width="250" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="145" y="268" text-anchor="middle" font-size="12">디바이스 모델</text>
  <text x="145" y="286" text-anchor="middle" font-size="12">bus·device·driver — drivers/base</text>
  <rect x="284" y="250" width="118" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="343" y="268" text-anchor="middle" font-size="12">block 계층</text>
  <text x="343" y="286" text-anchor="middle" font-size="12">block/ (blk-mq)</text>
  <rect x="416" y="250" width="118" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="475" y="268" text-anchor="middle" font-size="12">netdev</text>
  <text x="475" y="286" text-anchor="middle" font-size="12">net/core/dev.c</text>
  <rect x="548" y="250" width="118" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="607" y="268" text-anchor="middle" font-size="12">TTY·입력·IIO</text>
  <text x="607" y="286" text-anchor="middle" font-size="12">drivers/tty 등</text>
  <rect x="20" y="330" width="646" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="343" y="352" text-anchor="middle" font-size="12">디바이스 드라이버 drivers/* — amba-pl011 · sdhci · stmmac · bmi160 …</text>
  <rect x="20" y="378" width="646" height="32" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="343" y="399" text-anchor="middle" font-size="12">arch/arm64 — 예외 진입 · MMU · 컨텍스트 스위치 · 캐시 (C6, F11)</text>
  <line x1="340" y1="46" x2="79" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="46" x2="211" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="46" x2="343" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="46" x2="475" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="46" x2="607" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="79" y1="168" x2="79" y2="130" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="211" y1="128" x2="211" y2="166" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="343" y1="128" x2="343" y2="166" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="343" y1="212" x2="343" y2="248" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="343" y1="294" x2="343" y2="328" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="475" y1="128" x2="475" y2="166" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="475" y1="212" x2="475" y2="248" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="475" y1="294" x2="475" y2="328" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="607" y1="294" x2="607" y2="328" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="145" y1="294" x2="145" y2="328" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="343" y1="364" x2="343" y2="376" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```
그림 3 — 서브시스템 지도. 위에서 아래로 "요청이 내려가는 방향", IRQ 상자에서 스케줄러로 올라가는 화살표는 "인터럽트가 잠든 태스크를 깨운다"를 뜻한다. 보안(LSM)은 VFS·네트워크·IPC 곳곳에 훅으로 끼어 있어 선으로 그리지 않았다. 문자 디바이스(`/dev/ttyAMA0`)는 VFS에서 곧장 오른쪽 아래 TTY 프레임워크로 간다(§6).

### 2.2 서브시스템별 한 줄 설명과 드라이버 접점

| 서브시스템 | 디렉터리 | 핵심 구조체·함수 | 드라이버를 쓸 때 만나는 지점 | 깊게 보는 노트 |
|---|---|---|---|---|
| 프로세스·스케줄러 | `kernel/`, `kernel/sched/` | `task_struct`, `schedule()`, `wake_up_process()` | 드라이버가 잠들고(wait queue) 깨울 때 | F1, F3 |
| IRQ·시간·지연 작업 | `kernel/irq/`, `kernel/time/`, `kernel/workqueue.c` | `request_irq()`, `hrtimer`, `queue_work()` | 인터럽트 핸들러 등록, 하반부 처리 | F6, F7, F8 |
| 메모리 관리 | `mm/` | `kmalloc()`, `vmalloc()`, `struct page`, `vm_area_struct` | 버퍼 할당, `mmap` 구현, DMA 버퍼 | F9, F10, F11 |
| VFS | `fs/*.c` (`read_write.c`, `open.c`, `char_dev.c`) | `struct file`, `struct inode`, `file_operations` | char device의 `.read`/`.write`/`.ioctl` | F12, G4 |
| 파일시스템 | `fs/ext4/`, `fs/proc/`, `fs/sysfs/` 등 77종 | `super_block`, `dentry` | sysfs 속성(`DEVICE_ATTR`), procfs | G8, J2 |
| block 계층 | `block/` | `bio`, `request`, blk-mq | eMMC/NVMe 드라이버 | I11, J1 |
| 네트워크 | `net/` (71개 하위) | `sk_buff`, `net_device`, NAPI | 이더넷·CAN 드라이버 | I6, I10 |
| IPC | `ipc/` | SysV msg·sem·shm, POSIX mqueue | 드라이버와는 거의 무관 | A7 |
| 보안 | `security/` | LSM 훅 (SELinux, AppArmor, Landlock …) | 드라이버와는 거의 무관, 제품 하드닝 | L 모듈 |
| 디바이스 모델 | `drivers/base/` | `struct device`, `bus_type`, `device_driver` | `probe()`/`remove()`, sysfs 트리 | G2, G3, D3 |
| 드라이버 프레임워크 | `drivers/tty/`, `drivers/iio/`, `drivers/input/`, `drivers/i2c/` … | `uart_ops`, `iio_info`, `i2c_driver` | 드라이버가 등록하는 "자리" | H, I 모듈 |
| arch | `arch/arm64/` | 예외 벡터, `cpu_switch_to`, 페이지 테이블 | 직접 건드릴 일은 적다, Oops 해석 때 본다 | C6, M3 |

### 2.3 서브시스템끼리는 어떻게 대화하나

monolithic 커널이니 그냥 함수를 부른다. 다만 무작정 부르지 않고 두 가지 규칙이 있다.

- **ops 표 (함수 포인터 구조체)**: 위 층은 아래 층의 구체 구현을 모른다. VFS는 `file->f_op->read_iter`만 부르고, 그게 TTY인지 ext4인지 모른다. TTY는 `ld->ops->read`만 부르고, 그게 n_tty인지 모른다. C로 짠 인터페이스·가상 함수다. §7에서 실제 체인을 본다.
- **export된 심볼**: 모듈이 쓸 수 있는 함수는 `EXPORT_SYMBOL`/`EXPORT_SYMBOL_GPL`로 표시된 것뿐이다. 랩 빌드의 `Module.symvers`에는 **20,778개**(EXPORT_SYMBOL 8,329 + EXPORT_SYMBOL_GPL 12,449)가 있고, 그중 13,815개가 `vmlinux` 본체에서 나온다(§7 예제에서 측정). 이것이 사실상 "드라이버가 쓸 수 있는 커널 API의 전체 목록"이다. 단, 이 목록은 **안정 ABI가 아니다** — 버전마다 바뀐다(E7).

---

## 3. 소스 트리 투어 — 실측 숫자로

### 3.1 최상위 디렉터리

먼저 전체 모양을 한 장으로 본다(숫자는 아래 예제 1의 실측값).

```svg
<svg viewBox="0 0 680 526" xmlns="http://www.w3.org/2000/svg">
  <text x="97.5" y="24" text-anchor="middle" font-size="13" font-weight="bold">커널 코어</text>
  <rect x="20" y="36" width="155" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="28" y="52" font-size="12" font-weight="bold">kernel/</text>
  <text x="167" y="52" text-anchor="end" font-size="12">0.53M</text>
  <text x="28" y="69" font-size="12">스케줄러·시그널·IRQ</text>
  <rect x="20" y="82" width="155" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="28" y="98" font-size="12" font-weight="bold">mm/</text>
  <text x="167" y="98" text-anchor="end" font-size="12">0.21M</text>
  <text x="28" y="115" font-size="12">가상 메모리·할당자</text>
  <rect x="20" y="128" width="155" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="28" y="144" font-size="12" font-weight="bold">fs/</text>
  <text x="167" y="144" text-anchor="end" font-size="12">1.57M</text>
  <text x="28" y="161" font-size="12">VFS + FS 77종</text>
  <rect x="20" y="174" width="155" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="28" y="190" font-size="12" font-weight="bold">net/</text>
  <text x="167" y="190" text-anchor="end" font-size="12">1.32M</text>
  <text x="28" y="207" font-size="12">소켓·TCP/IP</text>
  <rect x="20" y="220" width="155" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="28" y="236" font-size="12" font-weight="bold">block/</text>
  <text x="167" y="236" text-anchor="end" font-size="12">71K</text>
  <text x="28" y="253" font-size="12">블록 I/O 계층</text>
  <rect x="20" y="266" width="155" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="28" y="282" font-size="12" font-weight="bold">ipc/</text>
  <text x="167" y="282" text-anchor="end" font-size="12">9.9K</text>
  <text x="28" y="299" font-size="12">SysV IPC</text>
  <rect x="20" y="312" width="155" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="28" y="328" font-size="12" font-weight="bold">security/</text>
  <text x="167" y="328" text-anchor="end" font-size="12">123K</text>
  <text x="28" y="345" font-size="12">LSM (SELinux 등)</text>
  <rect x="20" y="358" width="155" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="28" y="374" font-size="12" font-weight="bold">crypto/</text>
  <text x="167" y="374" text-anchor="end" font-size="12">110K</text>
  <text x="28" y="391" font-size="12">암호 API</text>
  <rect x="20" y="404" width="155" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="28" y="420" font-size="12" font-weight="bold">io_uring/</text>
  <text x="167" y="420" text-anchor="end" font-size="12">25K</text>
  <text x="28" y="437" font-size="12">비동기 I/O</text>
  <rect x="20" y="450" width="155" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="28" y="466" font-size="12" font-weight="bold">init/</text>
  <text x="167" y="466" text-anchor="end" font-size="12">4.6K</text>
  <text x="28" y="483" font-size="12">start_kernel</text>
  <text x="262.5" y="24" text-anchor="middle" font-size="13" font-weight="bold">하드웨어 쪽</text>
  <rect x="185" y="36" width="155" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="193" y="52" font-size="12" font-weight="bold">drivers/</text>
  <text x="332" y="52" text-anchor="end" font-size="12">25.18M</text>
  <text x="193" y="69" font-size="12">장치 드라이버 143종</text>
  <rect x="185" y="82" width="155" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="193" y="98" font-size="12" font-weight="bold">arch/</text>
  <text x="332" y="98" text-anchor="end" font-size="12">2.52M</text>
  <text x="193" y="115" font-size="12">CPU 아키텍처 21종</text>
  <rect x="185" y="128" width="155" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="193" y="144" font-size="12" font-weight="bold">sound/</text>
  <text x="332" y="144" text-anchor="end" font-size="12">1.60M</text>
  <text x="193" y="161" font-size="12">ALSA·ASoC</text>
  <rect x="185" y="174" width="155" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="193" y="190" font-size="12" font-weight="bold">virt/</text>
  <text x="332" y="190" text-anchor="end" font-size="12">11K</text>
  <text x="193" y="207" font-size="12">KVM 공통 코드</text>
  <text x="427.5" y="24" text-anchor="middle" font-size="13" font-weight="bold">공용 코드·헤더</text>
  <rect x="350" y="36" width="155" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="358" y="52" font-size="12" font-weight="bold">include/</text>
  <text x="497" y="52" text-anchor="end" font-size="12">1.39M</text>
  <text x="358" y="69" font-size="12">공용 헤더</text>
  <rect x="350" y="82" width="155" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="358" y="98" font-size="12" font-weight="bold">lib/</text>
  <text x="497" y="98" text-anchor="end" font-size="12">0.29M</text>
  <text x="358" y="115" font-size="12">자료구조·CRC</text>
  <rect x="350" y="128" width="155" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="358" y="144" font-size="12" font-weight="bold">rust/</text>
  <text x="497" y="144" text-anchor="end" font-size="12">51K</text>
  <text x="358" y="161" font-size="12">Rust 바인딩</text>
  <rect x="350" y="174" width="155" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="358" y="190" font-size="12" font-weight="bold">certs/</text>
  <text x="497" y="190" text-anchor="end" font-size="12">1K</text>
  <text x="358" y="207" font-size="12">커널 키링 인증서</text>
  <text x="592.5" y="24" text-anchor="middle" font-size="13" font-weight="bold">빌드·도구·문서</text>
  <rect x="515" y="36" width="155" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="523" y="52" font-size="12" font-weight="bold">scripts/</text>
  <text x="662" y="52" text-anchor="end" font-size="12">51K</text>
  <text x="523" y="69" font-size="12">Kconfig·kbuild·검사</text>
  <rect x="515" y="82" width="155" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="523" y="98" font-size="12" font-weight="bold">tools/</text>
  <text x="662" y="98" text-anchor="end" font-size="12">1.25M</text>
  <text x="523" y="115" font-size="12">perf·selftests (유저)</text>
  <rect x="515" y="128" width="155" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="523" y="144" font-size="12" font-weight="bold">Documentation/</text>
  <text x="662" y="144" text-anchor="end" font-size="12"></text>
  <text x="523" y="161" font-size="12">rst 3,821개 문서</text>
  <rect x="515" y="174" width="155" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="523" y="190" font-size="12" font-weight="bold">samples/</text>
  <text x="662" y="190" text-anchor="end" font-size="12">38K</text>
  <text x="523" y="207" font-size="12">예제 코드</text>
  <rect x="515" y="220" width="155" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="523" y="236" font-size="12" font-weight="bold">usr/</text>
  <text x="662" y="236" text-anchor="end" font-size="12">0.8K</text>
  <text x="523" y="253" font-size="12">initramfs 생성</text>
  <text x="340" y="516" text-anchor="middle" font-size="12">숫자 = .c .h .S .rs 줄 수 (랩 linux-6.18.55에서 측정, Documentation은 파일 10,819개)</text>
</svg>
```
그림 4 — 랩 트리(linux-6.18.55)의 최상위 디렉터리를 역할별로 묶은 지도. 초록은 하드웨어와 무관한 커널 코어, 파랑은 하드웨어 쪽, 주황은 공용 코드, 회색은 빌드·도구·문서다. `tools/`는 커널이 아니라 perf·selftests 같은 **유저 공간 프로그램**이라 커널 이미지에 들어가지 않는다.

#### 예제 1 — 최상위 디렉터리별 파일 수와 소스 줄 수

무엇을 확인하나: 디렉터리마다 파일 수, 소스 파일(`.c .h .S .rs`) 수, 그 줄 수를 세서 "커널 코드의 대부분이 어디 있는가"를 숫자로 본다.

```sh
cat > ~/.elinux_scratch/E1/e1_topdirs.sh <<'EOF'
#!/bin/bash
# per top-level dir: number of files, number of .c/.h/.S/.rs files, lines in those
cd /var/lab/src/linux-6.18.55
printf "%-14s %7s %7s %10s\n" DIR FILES SRC SRC_LINES
for d in */; do
  d=${d%/}
  f=$(find "$d" -type f | wc -l)
  s=$(find "$d" -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.rs' \) | wc -l)
  l=$(find "$d" -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.rs' \) -print0 | xargs -0 cat | wc -l)
  printf "%-14s %7d %7d %10d\n" "$d" "$f" "$s" "$l"
done | sort -k4 -n -r
echo "TOTAL files: $(find . -type f | wc -l)"
echo "TOTAL src lines: $(find . -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.rs' \) -print0 | xargs -0 cat | wc -l)"
EOF
limactl shell elinux -- bash /Users/donh/.elinux_scratch/E1/e1_topdirs.sh
```

```text
DIR              FILES     SRC  SRC_LINES
drivers          36542   33038   25181571
arch             18091   10411    2522918
sound             2911    2641    1600011
fs                2259    2084    1574261
include           6506    6501    1394605
net               1983    1811    1317607
tools             8552    4600    1254898
kernel             675     599     526525
lib                765     690     289847
mm                 197     187     213058
security           297     253     123075
crypto             193     175     110053
block              100      95      70934
scripts            558     119      51250
rust               227     220      51043
samples            287     189      37551
io_uring            78      76      24947
virt                20      15      10850
ipc                 13      12       9913
init                17      13       4606
certs               12       7       1067
usr                 12       4        831
Documentation    10819       4        708
LICENSES            23       0          0
TOTAL files: 91154
TOTAL src lines: 36372129
```

출력에서 볼 것:

- **`drivers/`가 2,518만 줄로 전체 소스의 69%**다. 커널 코어(`kernel/` 53만 + `mm/` 21만)는 합쳐도 2%가 안 된다. "Linux 커널 개발자"의 대부분은 드라이버 개발자라는 말이 숫자로 보인다.
- `wc -l`은 주석·빈 줄을 포함한 물리 줄 수다. "SLOC"(주석 제외)로 세면 더 작다. 그래도 비율은 거의 같다.
- `Documentation/`은 파일 10,819개지만 소스 줄은 거의 없다 — 대부분 `.rst`와 DT 바인딩 `.yaml`이다(§3.5).

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">소스 줄 수 — 최상위 디렉터리별 (.c .h .S .rs, 총 36.4M줄)</text>
  <text x="142" y="61" text-anchor="end" font-size="12">drivers/</text>
  <rect x="150" y="49" width="100.8" height="16" fill="#e08a3c" fill-opacity="0.75"/>
  <rect x="250.8" y="49" width="329.2" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="586.0" y="61" font-size="12">25.18M</text>
  <text x="142" y="85" text-anchor="end" font-size="12">arch/</text>
  <rect x="150" y="73" width="43.1" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="199.1" y="85" font-size="12">2.52M</text>
  <text x="142" y="109" text-anchor="end" font-size="12">sound/</text>
  <rect x="150" y="97" width="27.3" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="183.3" y="109" font-size="12">1.60M</text>
  <text x="142" y="133" text-anchor="end" font-size="12">fs/</text>
  <rect x="150" y="121" width="26.9" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="182.9" y="133" font-size="12">1.57M</text>
  <text x="142" y="157" text-anchor="end" font-size="12">include/</text>
  <rect x="150" y="145" width="23.8" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="179.8" y="157" font-size="12">1.39M</text>
  <text x="142" y="181" text-anchor="end" font-size="12">net/</text>
  <rect x="150" y="169" width="22.5" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="178.5" y="181" font-size="12">1.32M</text>
  <text x="142" y="205" text-anchor="end" font-size="12">tools/</text>
  <rect x="150" y="193" width="21.4" height="16" fill="#888" fill-opacity="0.75"/>
  <text x="177.4" y="205" font-size="12">1.25M</text>
  <text x="142" y="229" text-anchor="end" font-size="12">kernel/</text>
  <rect x="150" y="217" width="9.0" height="16" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="165.0" y="229" font-size="12">0.53M</text>
  <text x="142" y="253" text-anchor="end" font-size="12">lib/</text>
  <rect x="150" y="241" width="4.9" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="160.9" y="253" font-size="12">0.29M</text>
  <text x="142" y="277" text-anchor="end" font-size="12">mm/</text>
  <rect x="150" y="265" width="3.6" height="16" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="159.6" y="277" font-size="12">0.21M</text>
  <text x="142" y="301" text-anchor="end" font-size="12">security/</text>
  <rect x="150" y="289" width="2.1" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="158.1" y="301" font-size="12">0.12M</text>
  <text x="142" y="325" text-anchor="end" font-size="12">crypto/</text>
  <rect x="150" y="313" width="1.9" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="157.9" y="325" font-size="12">0.11M</text>
  <text x="142" y="349" text-anchor="end" font-size="12">block/</text>
  <rect x="150" y="337" width="1.5" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="157.5" y="349" font-size="12">70.9K</text>
  <text x="142" y="373" text-anchor="end" font-size="12">rust/</text>
  <rect x="150" y="361" width="1.5" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="157.5" y="373" font-size="12">51.0K</text>
  <text x="142" y="397" text-anchor="end" font-size="12">ipc/</text>
  <rect x="150" y="385" width="1.5" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="157.5" y="397" font-size="12">9.9K</text>
  <text x="142" y="421" text-anchor="end" font-size="12">init/</text>
  <rect x="150" y="409" width="1.5" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="157.5" y="421" font-size="12">4.6K</text>
  <line x1="150" y1="42" x2="150" y2="430" stroke="currentColor"/>
  <text x="340" y="458" text-anchor="middle" font-size="12">주황 = drivers/gpu/drm/amd 하나 (5.90M줄, 그중 레지스터 헤더 4.66M) · 초록 = 코어(kernel, mm)</text>
</svg>
```
그림 5 — 최상위 디렉터리별 소스 줄 수(예제 1 실측). `drivers/` 막대의 주황 부분은 **AMD GPU 드라이버 디렉터리 하나**(`drivers/gpu/drm/amd`, 590만 줄)이고, 그중 466만 줄은 자동 생성된 레지스터 정의 헤더(`include/asic_reg`)다. 줄 수가 곧 "읽어야 할 코드 양"은 아니라는 뜻이다.

### 3.2 `arch/` — CPU마다 다른 부분

`arch/`에는 21개 아키텍처가 있다. 각 아키텍처 디렉터리는 비슷한 모양을 갖는다: `boot/`(이미지 생성, DTS), `kernel/`(예외 진입, 컨텍스트 스위치, SMP 부팅), `mm/`(페이지 테이블, 폴트 처리, 캐시), `include/asm/`(아키텍처별 헤더), `configs/`(defconfig).

#### 예제 2 — `arch/`와 `arch/arm64` 해부

무엇을 확인하나: 아키텍처별 코드 양, arm64 하위 디렉터리별 크기, DTS 파일 수, 그리고 `arch/arm64/kernel`과 `mm`에 어떤 파일이 있는지.

```sh
cat > ~/.elinux_scratch/E1/e1_arch.sh <<'EOF'
#!/bin/bash
cd /var/lab/src/linux-6.18.55
echo "== arch/ dirs: $(ls -d arch/*/ | wc -l)"; ls arch | tr '\n' ' '; echo
echo "== arch lines top 8 (src)"
for d in arch/*/; do d=${d%/}; printf "%-16s %8d\n" ${d#arch/} $(find $d -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' \) -print0 | xargs -0 cat | wc -l); done | sort -k2 -nr | head -8
echo "== arch/arm64 subdirs"
for d in arch/arm64/*/; do d=${d%/}; printf "%-28s files=%5d src_lines=%8d\n" $d $(find $d -type f | wc -l) $(find $d -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' \) -print0 | xargs -0 cat 2>/dev/null | wc -l); done
echo "== arm64 dts vendors: $(ls -d arch/arm64/boot/dts/*/ | wc -l), dts files: $(find arch/arm64/boot/dts -name '*.dts' | wc -l), dtsi: $(find arch/arm64/boot/dts -name '*.dtsi' | wc -l)"
for v in freescale ti nvidia qcom rockchip; do printf "  %-10s dts=%4d\n" $v $(find arch/arm64/boot/dts/$v -name '*.dts' | wc -l); done
echo "== arch/arm64/kernel sample"; ls arch/arm64/kernel | head -80 | tr '\n' ' '; echo
echo "== arch/arm64/mm"; ls arch/arm64/mm | tr '\n' ' '; echo
EOF
limactl shell elinux -- bash /Users/donh/.elinux_scratch/E1/e1_arch.sh
```

```text
== arch/ dirs: 21
Kconfig alpha arc arm arm64 csky hexagon loongarch m68k microblaze mips nios2 openrisc parisc powerpc riscv s390 sh sparc um x86 xtensa 
== arch lines top 8 (src)
x86                474904
powerpc            438781
mips               289730
arm                286226
arm64              173978
m68k               152180
sparc              127279
s390               119640
== arch/arm64 subdirs
arch/arm64/boot              files= 2833 src_lines=   15286
arch/arm64/configs           files=    3 src_lines=       0
arch/arm64/crypto            files=   42 src_lines=   12311
arch/arm64/hyperv            files=    3 src_lines=     333
arch/arm64/include           files=  230 src_lines=   32819
arch/arm64/kernel            files=  120 src_lines=   37948
arch/arm64/kvm               files=  107 src_lines=   57839
arch/arm64/lib               files=   27 src_lines=    4747
arch/arm64/mm                files=   27 src_lines=     9045
arch/arm64/net               files=    4 src_lines=    3520
arch/arm64/tools             files=    6 src_lines=       0
arch/arm64/xen               files=    2 src_lines=     130
== arm64 dts vendors: 39, dts files: 1399, dtsi: 1185
  freescale  dts= 210
  ti         dts=  57
  nvidia     dts=  19
  qcom       dts= 298
  rockchip   dts= 193
== arch/arm64/kernel sample
Makefile Makefile.syscalls acpi.c acpi_numa.c acpi_parking_protocol.c alternative.c armv8_deprecated.c asm-offsets.c cacheinfo.c compat_alignment.c cpu-reset.S cpu_errata.c cpu_ops.c cpufeature.c cpuinfo.c crash_dump.c debug-monitors.c efi-header.S efi-rt-wrapper.S efi.c elfcore.c entry-common.c entry-fpsimd.S entry-ftrace.S entry.S fpsimd.c ftrace.c head.S hibernate-asm.S hibernate.c hw_breakpoint.c hyp-stub.S idle.c image-vars.h image.h io.c irq.c jump_label.c kaslr.c kexec_image.c kgdb.c kuser32.S machine_kexec.c machine_kexec_file.c module-plts.c module.c mte.c paravirt.c patching.c pci.c perf_callchain.c perf_regs.c pi pointer_auth.c probes process.c proton-pack.c psci.c ptrace.c reloc_test_core.c reloc_test_syms.S relocate_kernel.S return_address.c rsi.c sdei.c setup.c signal.c signal32.c sigreturn32.S sleep.S smccc-call.S smp.c smp_spin_table.c stacktrace.c suspend.c sys.c sys32.c sys_compat.c syscall.c time.c 
== arch/arm64/mm
Makefile cache.S context.c contpte.c copypage.c dma-mapping.c extable.c fault.c fixmap.c flush.c gcs.c hugetlbpage.c init.c ioremap.c kasan_init.c mem_encrypt.c mmap.c mmu.c mteswap.c pageattr.c pgd.c physaddr.c proc.S ptdump.c ptdump_debugfs.c trans_pgd-asm.S trans_pgd.c
```

출력에서 볼 것:

- **arm64 전체 코드가 17만 줄**이다. x86(47만)보다 훨씬 작다 — arm64가 레거시가 적고, 보드별 차이를 C 코드 대신 DTS로 표현하기 때문이다(D1). `arch/arm64/boot`의 파일 2,833개 대부분이 DTS/DTSI다: 벤더 39곳, 보드 `.dts` 1,399개. Don이 만질 i.MX8은 `freescale/`(210개), TI AM62는 `ti/`, Jetson은 `nvidia/`에 있다.
- `arch/arm64/kernel`에서 꼭 알아 둘 파일: `head.S`(커널 첫 명령, C6), `entry.S`(예외 벡터 — 펌웨어의 vector table), `entry-common.c`(예외를 C로 분류), `syscall.c`(syscall 디스패치), `process.c`(스레드 생성·컨텍스트 스위치 보조), `smp.c`, `psci.c`(CPU 켜기·끄기, C5), `irq.c`.
- `arch/arm64/mm`: `fault.c`(data abort → page fault 처리, 펌웨어의 MMU abort 핸들러에 해당), `mmu.c`(커널 페이지 테이블 구성), `ioremap.c`(H1에서 쓰는 MMIO 매핑), `dma-mapping.c`(H9), `cache.S`(캐시 유지 명령).
- `arch/arm64/kvm`이 `kernel`보다 크다(5.8만 줄) — 가상화 지원이 생각보다 큰 덩어리다.

> 덤으로 알게 된 것: 랩 빌드의 빌트인 객체 목록에 `arch/arm/xen/*.o`가 4개 들어 있다. `arch/arm64/xen/Makefile` 2행이 `$(addprefix ../../arm/xen/, enlighten.o grant-table.o p2m.o mm.o)`로 32비트 ARM 쪽 Xen 코드를 그대로 재사용하기 때문이다. arch 디렉터리끼리도 코드를 공유한다.

### 3.3 `drivers/` — 커널의 3분의 2

#### 예제 3 — `drivers/` 하위 디렉터리별 크기

무엇을 확인하나: `drivers/` 안 143개 하위 디렉터리 중 어디가 큰지, 그리고 GPU 드라이버 안에서도 어디가 큰지.

```sh
cat > ~/.elinux_scratch/E1/e1_drivers.sh <<'EOF'
#!/bin/bash
# drivers/ breakdown by subdirectory: src files and lines (.c/.h/.S/.rs)
cd /var/lab/src/linux-6.18.55/drivers
for d in */; do
  d=${d%/}
  s=$(find "$d" -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.rs' \) | wc -l)
  l=$(find "$d" -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.rs' \) -print0 | xargs -0 cat 2>/dev/null | wc -l)
  printf "%-16s %6d %9d\n" "$d" "$s" "$l"
done | sort -k3 -n -r | head -25
echo "subdirs: $(ls -d */ | wc -l)"
echo "== gpu split"
for d in gpu/drm/amd gpu/drm/i915 gpu/drm/xe gpu/drm/nouveau; do
  printf "%-18s %9d\n" $d $(find $d -type f \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 cat | wc -l); done
echo "amd asic_reg headers: $(find gpu/drm/amd/include/asic_reg -name '*.h' -print0 | xargs -0 cat | wc -l)"
EOF
limactl shell elinux -- bash /Users/donh/.elinux_scratch/E1/e1_drivers.sh
```

```text
gpu                7164   7761119
net                6052   5199386
media              2595   1491136
scsi                769   1016838
clk                1270    768843
usb                 784    582777
pinctrl             546    556945
infiniband          568    431252
iio                 727    387035
accel               518    374175
staging            1134    360503
video               480    295139
crypto              519    266964
input               462    219235
md                  286    212351
platform            368    204656
hwmon               312    201805
tty                 216    191004
mtd                 291    188392
acpi                339    172958
dma                 206    162059
pci                 222    161539
hid                 277    142954
s390                224    138386
phy                 281    138090
subdirs: 143
== gpu split
gpu/drm/amd          5902232
gpu/drm/i915          408366
gpu/drm/xe            119321
gpu/drm/nouveau       224972
amd asic_reg headers: 4661051
```

출력에서 볼 것:

- `drivers/` 디렉터리 이름은 **버스나 장치 종류**로 나뉜다: 버스(`i2c/`, `spi/`, `usb/`, `pci/`), 장치 클래스(`net/`, `gpu/`, `media/`, `iio/`, `input/`, `tty/`, `hwmon/`), SoC 공통 인프라(`clk/`, `pinctrl/`, `regulator/`, `irqchip/`, `dma/`, `phy/`, `soc/`). **"이 칩의 드라이버는 어디 있나"의 답은 "그 칩이 무슨 일을 하나"로 정해진다** — I2C에 붙은 IMU라도 `drivers/i2c/`가 아니라 `drivers/iio/imu/`에 있다. `drivers/i2c/`에는 I2C **컨트롤러**(adapter) 드라이버가 있다.
- 임베디드에서 중요한 디렉터리가 의외로 크다: `clk/` 77만 줄, `pinctrl/` 56만 줄. SoC마다 클럭 트리·핀 먹스 표가 길기 때문이다(H3, H4).
- `staging/`(36만 줄)은 품질이 아직 mainline 기준에 못 미친 드라이버가 머무는 곳이다. 벤더 코드가 여기서 졸업하거나 삭제된다(E7).

```svg
<svg viewBox="0 0 680 446" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">drivers/ 하위 디렉터리별 소스 줄 수 (143개 중 상위 + tty)</text>
  <text x="142" y="61" text-anchor="end" font-size="12">gpu</text>
  <rect x="150" y="49" width="430.0" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="586.0" y="61" font-size="12">7.76M</text>
  <text x="142" y="85" text-anchor="end" font-size="12">net</text>
  <rect x="150" y="73" width="288.1" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="444.1" y="85" font-size="12">5.20M</text>
  <text x="142" y="109" text-anchor="end" font-size="12">media</text>
  <rect x="150" y="97" width="82.6" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="238.6" y="109" font-size="12">1.49M</text>
  <text x="142" y="133" text-anchor="end" font-size="12">scsi</text>
  <rect x="150" y="121" width="56.3" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="212.3" y="133" font-size="12">1.02M</text>
  <text x="142" y="157" text-anchor="end" font-size="12">clk</text>
  <rect x="150" y="145" width="42.6" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="198.6" y="157" font-size="12">0.77M</text>
  <text x="142" y="181" text-anchor="end" font-size="12">usb</text>
  <rect x="150" y="169" width="32.3" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="188.3" y="181" font-size="12">0.58M</text>
  <text x="142" y="205" text-anchor="end" font-size="12">pinctrl</text>
  <rect x="150" y="193" width="30.9" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="186.9" y="205" font-size="12">0.56M</text>
  <text x="142" y="229" text-anchor="end" font-size="12">infiniband</text>
  <rect x="150" y="217" width="23.9" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="179.9" y="229" font-size="12">0.43M</text>
  <text x="142" y="253" text-anchor="end" font-size="12">iio</text>
  <rect x="150" y="241" width="21.4" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="177.4" y="253" font-size="12">0.39M</text>
  <text x="142" y="277" text-anchor="end" font-size="12">accel</text>
  <rect x="150" y="265" width="20.7" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="176.7" y="277" font-size="12">0.37M</text>
  <text x="142" y="301" text-anchor="end" font-size="12">staging</text>
  <rect x="150" y="289" width="20.0" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="176.0" y="301" font-size="12">0.36M</text>
  <text x="142" y="325" text-anchor="end" font-size="12">video</text>
  <rect x="150" y="313" width="16.4" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="172.4" y="325" font-size="12">0.30M</text>
  <text x="142" y="349" text-anchor="end" font-size="12">input</text>
  <rect x="150" y="337" width="12.1" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="168.1" y="349" font-size="12">0.22M</text>
  <text x="142" y="373" text-anchor="end" font-size="12">tty</text>
  <rect x="150" y="361" width="10.6" height="16" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="166.6" y="373" font-size="12">0.19M</text>
  <text x="142" y="397" text-anchor="end" font-size="12">pci</text>
  <rect x="150" y="385" width="8.9" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="164.9" y="397" font-size="12">0.16M</text>
  <line x1="150" y1="42" x2="150" y2="406" stroke="currentColor"/>
  <text x="340" y="434" text-anchor="middle" font-size="12">주황 = 이 노트의 길찾기 대상 drivers/tty (serial 포함, 0.19M줄)</text>
</svg>
```
그림 6 — `drivers/` 하위 디렉터리별 소스 줄 수(예제 3 실측). 이 노트에서 따라갈 `drivers/tty`는 19만 줄로 18위다.

### 3.4 `include/` — 헤더는 세 군데에 있다

| 위치 | 누가 쓰나 | 예 | 규칙 |
|---|---|---|---|
| `include/linux/` | 커널 내부 전용 (드라이버 포함) | `linux/fs.h`, `linux/platform_device.h` | 버전마다 마음대로 바뀐다 |
| `include/uapi/linux/` | 커널 + 유저 공간 (ABI) | `uapi/linux/fs.h`, `uapi/linux/i2c-dev.h` | **절대 깨면 안 된다** (유저 ABI) |
| `arch/arm64/include/asm/` | 커널 내부, arm64 전용 | `asm/io.h`, `asm/barrier.h` | `#include <asm/...>`로 쓰면 현재 아키텍처 것이 잡힌다 |
| `arch/arm64/include/uapi/asm/` | 유저 + 커널, arm64 전용 ABI | `asm/ptrace.h`, `asm/sigcontext.h` | 유저 ABI |
| `include/asm-generic/` | 아키텍처가 따로 구현 안 하면 쓰는 기본값 | `asm-generic/unistd.h` 계열 | arm64는 syscall 번호를 여기 것(generic)으로 쓴다 |
| `include/dt-bindings/` | DTS와 드라이버가 공유하는 상수 | `dt-bindings/gpio/gpio.h` | DTS에서 `#include` (D2) |

`make headers_install`이 `uapi/` 쪽만 걸러 유저 공간용 헤더(`/usr/include/linux/...`)를 만든다. 즉 A 모듈에서 쓴 `#include <linux/i2c-dev.h>`는 커널 트리의 `include/uapi/linux/i2c-dev.h`에서 온 것이다.

#### 예제 4 — 헤더·문서·MAINTAINERS 규모

무엇을 확인하나: `include/` 하위의 크기, `linux/fs.h`가 `uapi/linux/fs.h`를 끌어오는 방식, `Documentation/`의 구성.

```sh
cat > ~/.elinux_scratch/E1/e1_include.sh <<'EOF'
#!/bin/bash
cd /var/lab/src/linux-6.18.55
echo "== include/ subdirs (files)"
for d in include/*/; do printf "%-26s %5d\n" $d $(find $d -type f | wc -l); done | sort -k2 -nr | head -12
echo "== arch/arm64/include: asm=$(ls arch/arm64/include/asm | wc -l) uapi/asm=$(ls arch/arm64/include/uapi/asm | wc -l)"
echo "== include/linux/fs.h lines: $(wc -l < include/linux/fs.h)  include/uapi/linux/fs.h: $(wc -l < include/uapi/linux/fs.h)"
grep -n "#include <uapi/linux/fs.h>" include/linux/fs.h
echo "== Documentation top dirs (files)"
for d in Documentation/*/; do printf "%-34s %5d\n" $d $(find $d -type f | wc -l); done | sort -k2 -nr | head -10
echo "rst files: $(find Documentation -name '*.rst' | wc -l), yaml bindings: $(find Documentation/devicetree/bindings -name '*.yaml' | wc -l)"
echo "== MAINTAINERS lines: $(wc -l < MAINTAINERS), entries(F: lines): $(grep -c '^F:' MAINTAINERS), sections: $(grep -c '^[A-Z0-9].*[A-Z]' MAINTAINERS)"
EOF
limactl shell elinux -- bash /Users/donh/.elinux_scratch/E1/e1_include.sh
```

```text
== include/ subdirs (files)
include/linux/              2759
include/dt-bindings/        1102
include/uapi/                963
include/net/                 383
include/sound/               201
include/trace/               187
include/asm-generic/         154
include/drm/                 143
include/media/               111
include/crypto/               81
include/soc/                  74
include/xen/                  61
== arch/arm64/include: asm=196 uapi/asm=20
== include/linux/fs.h lines: 4096  include/uapi/linux/fs.h: 658
53:#include <uapi/linux/fs.h>
== Documentation top dirs (files)
Documentation/devicetree/           6016
Documentation/ABI/                   660
Documentation/translations/          523
Documentation/userspace-api/         479
Documentation/admin-guide/           418
Documentation/driver-api/            350
Documentation/networking/            276
Documentation/hwmon/                 264
Documentation/arch/                  243
Documentation/filesystems/           143
rst files: 3821, yaml bindings: 5182
...
```

(마지막 MAINTAINERS 줄은 `sections` 세는 정규식이 엉성해서 생략했다. MAINTAINERS는 §4.5에서 제대로 본다: 28,440줄, `F:` 패턴 9,188개.)

출력에서 볼 것:

- `include/linux/fs.h`(4,096줄)가 53행에서 `uapi/linux/fs.h`(658줄)를 포함한다. **커널 내부 헤더 = uapi 헤더 + 커널 전용 추가분** 구조다. 드라이버에서 유저와 공유할 구조체(ioctl 인자 등)는 uapi 쪽에 둬야 한다(G5).
- `Documentation/`의 절반 이상(6,016개)이 `devicetree/`이고 그중 `.yaml` 바인딩이 5,182개다. DT 노드를 쓸 때 "이 compatible에 어떤 속성이 필요한가"는 여기서 찾는다(D7).
- 드라이버 개발자가 가장 자주 여는 문서 디렉터리는 `driver-api/`(커널 내부 API 설명), `devicetree/bindings/`, `admin-guide/kernel-parameters.txt`(부트 인자), `ABI/`(sysfs 속성 계약)다. 웹으로는 docs.kernel.org에 같은 내용이 렌더링돼 있다.

### 3.5 나머지 디렉터리 한 줄씩

| 디렉터리 | 무엇 | 드라이버 개발자가 여기 오는 때 |
|---|---|---|
| `init/` | `start_kernel()`(`init/main.c`), initramfs 풀기 | 부팅 순서·initcall 문제 (C6) |
| `kernel/` | 스케줄러, 시그널, fork, IRQ 코어, 타이머, 워크큐, printk, 모듈 로더 | `request_irq` 내부, `printk` 동작 확인 |
| `mm/` | 페이지 할당자, slab, vmalloc, page fault, mmap | 메모리 할당 실패 디버깅 (F9) |
| `fs/` | VFS(최상위 `.c` 파일들) + 개별 FS(하위 디렉터리 77개) | char device 경로, sysfs/debugfs |
| `net/` | 소켓, TCP/IP, netfilter, CAN(`net/can`) | SocketCAN (I10) |
| `block/` | 블록 I/O 계층, I/O 스케줄러 | eMMC/NVMe 성능 |
| `lib/` | 문자열, `rbtree`, `kfifo`, CRC, `vsprintf` | 링버퍼가 필요하면 `kfifo`부터 |
| `scripts/` | Kconfig 파서, kbuild, `checkpatch.pl`, `get_maintainer.pl`, `faddr2line`, `dtc` | 매일 (§4, E2, E7) |
| `tools/` | perf, bpftool, selftests, IIO 유틸 — **유저 공간 프로그램** | 성능 측정 (M6), 테스트 |
| `rust/` | Rust 커널 crate와 C 바인딩 | E8 |
| `samples/` | 커널 API 예제 모듈 (kobject, kfifo, trace_events …) | 새 API를 처음 쓸 때 복사해 오는 출발점 |
| `usr/` | 빌트인 initramfs 생성 (`gen_init_cpio`) | initramfs를 커널에 내장할 때 (K 모듈) |
| `certs/` | 모듈 서명 검증용 키링 | Secure boot 제품 (L 모듈) |
| `virt/` | KVM 공통 코드 | 거의 없음 |

---

## 4. 코드 길찾기 — 프로처럼

3,600만 줄은 읽는 대상이 아니라 **검색하는 대상**이다. 커널 개발자의 하루는 "이 함수 정의 어디?", "이걸 누가 부르지?", "이 구조체 필드는 누가 채우지?", "이 크래시 주소는 몇 번째 줄?"의 반복이다. 도구를 상황별로 정리한다.

### 4.1 `grep -rn`과 `git grep`

랩 트리는 tarball을 푼 것이라 **git 저장소가 아니다**. 그래도 `git grep --no-index`로 git의 빠른 검색기를 쓸 수 있다. 회사에서 받는 벤더 커널은 보통 git 저장소이므로 그냥 `git grep`을 쓰면 된다.

#### 예제 5 — 정의 찾기, 호출자 찾기, 문자열로 드라이버 찾기

무엇을 확인하나: 같은 검색을 `grep -rn`과 `git grep`으로 해 보고, 호출자 검색에서 "텍스트 검색의 한계"를 본다.

```sh
cat > ~/.elinux_scratch/E1/e1_nav.sh <<'EOF'
#!/bin/bash
cd /var/lab/src/linux-6.18.55
echo "== 1. grep -rn definition of vfs_read"
time (grep -rn --include='*.c' '^ssize_t vfs_read(' . )
echo "== 2. git grep --no-index (not a git repo)"
time (git grep --no-index -n -E '^ssize_t vfs_read\(' -- '*.c')
echo "== 3. callers of vfs_read (count, first 8)"
git grep --no-index -n -w 'vfs_read' -- '*.c' | grep -v '^fs/read_write.c:552' | wc -l
git grep --no-index -n -w 'vfs_read' -- '*.c' | head -8
echo "== 4. who registers ttyAMA"
git grep --no-index -n '"ttyAMA"' -- drivers
echo "== 5. compatible arm,pl011 in drivers"
git grep --no-index -n 'arm,pl011\|0x00041011' -- drivers/tty | head
echo "== tools"
for t in ctags cscope gtags; do command -v $t >/dev/null && echo "$t: yes" || echo "$t: not installed"; done
grep -n "^tags TAGS cscope gtags\|^PHONY += tags TAGS cscope gtags" Makefile
ls scripts/tags.sh scripts/get_maintainer.pl scripts/faddr2line scripts/decode_stacktrace.sh scripts/clang-tools/gen_compile_commands.py scripts/checkpatch.pl
EOF
limactl shell elinux -- bash /Users/donh/.elinux_scratch/E1/e1_nav.sh 2>&1
```

```text
== 1. grep -rn definition of vfs_read
./fs/read_write.c:552:ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)

real	0m15.616s
user	0m1.420s
sys	0m2.927s
== 2. git grep --no-index (not a git repo)
fs/read_write.c:552:ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)

real	0m3.197s
user	0m1.529s
sys	0m1.918s
== 3. callers of vfs_read (count, first 8)
5
fs/exec.c:833:	ssize_t res = vfs_read(file, (void __user *)addr, len, &pos);
fs/nfs/localio.c:716:	dprintk("%s: vfs_read count=%u pos=%llu\n",
fs/read_write.c:552:ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
fs/read_write.c:715:		ret = vfs_read(fd_file(f), buf, count, ppos);
fs/read_write.c:763:		return vfs_read(fd_file(f), buf, count, &pos);
net/core/net-sysfs.c:52: *    rtnl_lock                                   vfs_read
== 4. who registers ttyAMA
drivers/tty/serial/amba-pl011.c:2629:	.name		= "ttyAMA",
drivers/tty/serial/amba-pl011.c:2768:	.driver_name		= "ttyAMA",
drivers/tty/serial/amba-pl011.c:2769:	.dev_name		= "ttyAMA",
== 5. compatible arm,pl011 in drivers
drivers/tty/serial/amba-pl011.c:2735:OF_EARLYCON_DECLARE(pl011, "arm,pl011", pl011_early_console_setup);
drivers/tty/serial/amba-pl011.c:3123:		.id	= 0x00041011,
== tools
ctags: not installed
cscope: not installed
gtags: not installed
2083:tags TAGS cscope gtags: FORCE
scripts/checkpatch.pl
scripts/clang-tools/gen_compile_commands.py
scripts/decode_stacktrace.sh
scripts/faddr2line
scripts/get_maintainer.pl
```

출력에서 볼 것:

- **정의 찾기의 요령은 "줄 맨 앞 + 반환형"**이다. 커널 코딩 스타일은 함수 정의의 반환형과 이름을 한 줄에 쓰므로 `'^ssize_t vfs_read('`처럼 `^`로 고정하면 호출부가 걸러지고 정의만 남는다. 구조체 정의는 `'^struct tty_driver {'`, 매크로는 `'#define NAME'`.
- 3번(호출자): `-w 'vfs_read'`로 5줄이 남았지만 **진짜 호출은 3개**(`fs/exec.c:833`, `fs/read_write.c:715`, `:763`)다. `fs/nfs/localio.c:716`은 문자열 안, `net/core/net-sysfs.c:52`는 주석이다. 텍스트 검색은 의미를 모른다. 함수 포인터로 불리는 경우(`f_op->read_iter`)는 아예 못 찾는다 — 그럴 땐 **구조체 초기화**(`.read_iter = tty_read`)를 검색한다.
- 4·5번: "장치 이름 `ttyAMA`는 누가 만드나", "이 compatible/ID를 누가 처리하나"를 문자열로 바로 찾았다. **드라이버 찾기의 1번 무기는 dmesg에 나온 문자열이나 DT의 compatible을 그대로 grep하는 것**이다. PL011은 DT `compatible = "arm,pl011", "arm,primecell"`이지만 실제 매칭은 AMBA 버스가 하드웨어 ID 레지스터(`0x00041011`, 3123행)로 한다는 것도 덤으로 보인다(D3에서 본 OF 매칭과 다른 경로).
- 시간: 첫 `grep -rn`은 15.6초, 바로 이어 돈 `git grep`은 3.2초였다. 첫 실행은 디스크 캐시가 차가워서 불리했으므로, 캐시가 데워진 뒤 다시 쟀다:

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55; for i in 1 2; do
 /usr/bin/time -f "grep -rn     %es" grep -rn --include="*.c" "^ssize_t vfs_read(" . >/dev/null
 /usr/bin/time -f "git grep     %es" git grep --no-index -n -E "^ssize_t vfs_read\(" -- "*.c" >/dev/null
done'
```

```text
grep -rn     9.11s
git grep     3.93s
grep -rn     3.32s
git grep     2.12s
```

VM을 다른 작업과 같이 쓰는 중이라 값이 흔들리지만, `git grep`이 매번 더 빨랐다(여러 스레드로 검색한다). git 저장소 안에서는 인덱스된 파일만 보므로 빌드 산출물도 안 걸려서 더 빠르고 깨끗하다.

### 4.2 ctags · cscope · compile_commands.json

IDE식 "정의로 점프", "호출자 목록"은 인덱스가 필요하다. 커널 Makefile에 타깃이 준비돼 있다.

| 도구 | 만드는 명령 | 장점 | 한계 |
|---|---|---|---|
| ctags | `make tags` (vim), `make TAGS` (emacs) | 빠름, 정의 점프 | 호출자 검색 약함, `#ifdef` 무시 |
| cscope | `make cscope` → `cscope -d` | 호출자·호출 대상·문자열 검색 | 매크로·함수 포인터 약함 |
| GNU global | `make gtags` | 큰 트리에서 빠름 | 설치 필요 |
| clangd + `compile_commands.json` | `make compile_commands.json` 또는 `scripts/clang-tools/gen_compile_commands.py` | **실제 빌드 설정 기준** 의미 분석, `#ifdef`·매크로 정확 | 빌드가 한 번 끝나 있어야 함 |

규칙 두 가지:

- `make tags`/`make cscope`는 `scripts/tags.sh`를 부르고, 기본으로 **현재 `ARCH`의 `arch/` 디렉터리만** 인덱싱한다(`ALLSOURCE_ARCHS`가 비면 `SRCARCH`만, tags.sh 42–44행). 그래서 x86 PC에서 `ARCH`를 안 주고 만들면 arm64 코드가 빠진다. `make ARCH=arm64 tags`로 만든다. `COMPILED_SOURCE=1`을 주면 실제로 컴파일된 파일만 인덱싱한다(tags.sh 111행) — `drivers/gpu`의 590만 줄을 건너뛸 수 있다.
- 랩 VM에는 ctags·cscope·gtags가 **설치돼 있지 않고**(예제 5의 `tools` 출력), 랩 트리는 읽기 전용이라 `make tags`는 **실행하지 않았다**. 자기 PC에서는 `sudo apt install universal-ctags cscope` 후 소스 복사본에서 만든다.

#### 예제 6 — 빌드 트리에서 `compile_commands.json` 만들기

무엇을 확인하나: 이미 빌드된 트리의 `.cmd` 파일들로부터 clangd용 컴파일 DB를 만들고(쓰기는 `/var/tmp/E1`에만), 이 설정에서 실제로 컴파일된 `.c`가 전체의 몇 %인지 센다.

```sh
limactl shell elinux -- bash -lc 'S=/var/lab/src/linux-6.18.55; cd /var/tmp/E1; time python3 $S/scripts/clang-tools/gen_compile_commands.py -d /var/lab/build/linux -o /var/tmp/E1/e1_compile_commands.json 2>&1 | tail -3; python3 -c "
import json;d=json.load(open(\"/var/tmp/E1/e1_compile_commands.json\"));print(\"entries:\",len(d))
e=[x for x in d if x[\"file\"].endswith(\"amba-pl011.c\")][0];print(e[\"file\"]);print(e[\"command\"][:160])"; ls -l e1_compile_commands.json | awk "{print \$5}"'
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55; echo "all .c: $(find . -name "*.c" | wc -l)"; echo "non-tools/samples/scripts/Documentation .c: $(find . -name "*.c" -not -path "./tools/*" -not -path "./samples/*" -not -path "./scripts/*" -not -path "./Documentation/*" | wc -l)"; python3 -c "
import json,collections;d=json.load(open(\"/var/tmp/E1/e1_compile_commands.json\"))
c=collections.Counter(x[\"file\"].split(\"linux-6.18.55/\")[1].split(\"/\")[0] for x in d if \"linux-6.18.55/\" in x[\"file\"])
print(sorted(c.items(),key=lambda t:-t[1])[:10])
c2=collections.Counter(x[\"file\"].split(\"linux-6.18.55/\")[1].split(\"/\")[1] for x in d if \"linux-6.18.55/arch/\" in x[\"file\"]); print(c2)"'
```

```text
real	0m18.452s
user	0m1.504s
sys	0m1.862s
entries: 10117
/var/lab/src/linux-6.18.55/drivers/tty/serial/amba-pl011.c
gcc -Wp,-MMD,drivers/tty/serial/.amba-pl011.o.d -nostdinc -I/var/lab/src/linux-6.18.55/arch/arm64/include -I./arch/arm64/include/generated -I/var/lab/src/linux-
29196682
all .c: 36001
non-tools/samples/scripts/Documentation .c: 32389
[('drivers', 6306), ('net', 503), ('fs', 441), ('sound', 356), ('lib', 246), ('kernel', 245), ('arch', 232), ('mm', 87), ('crypto', 80), ('block', 47)]
Counter({'arm64': 228, 'arm': 4})
```

출력에서 볼 것:

- 이 설정(arm64 defconfig 기반, 빌트인 + 모듈 전부)에서 컴파일된 `.c`는 **10,117개 — 커널 쪽 `.c` 32,389개의 31%**다. 나머지 69%는 다른 아키텍처이거나 꺼진 드라이버다. **"이 함수가 내 보드 커널에 들어 있나"를 텍스트 검색으로 판단하면 틀린다**. compile DB(또는 빌드 트리의 `.o` 존재)로 확인한다.
- `arch/`에서는 `arm64` 228개(+ Xen 공유 코드 `arm` 4개)만 컴파일됐다. clangd는 이 DB를 보고 `#ifdef CONFIG_...` 분기와 매크로를 실제 설정대로 해석하므로, VS Code·Neovim에서 "정의로 이동/참조 찾기"가 ctags보다 정확하다.

### 4.3 elixir.bootlin.com — 브라우저 버전의 cscope

Bootlin이 운영하는 Elixir Cross Referencer는 모든 커널 릴리스를 인덱싱해 두었다. 설치가 필요 없어서 면접 중 화면 공유나 회사 노트북이 없을 때 가장 빠르다.

- 파일 보기: `https://elixir.bootlin.com/linux/v6.18/source/fs/read_write.c` — 줄 번호를 누르면 `#L552` 같은 앵커가 붙는다.
- 식별자 검색: 화면 위 검색창에 `vfs_read`를 넣으면 "Defined in"(정의)과 "Referenced in"(참조 위치) 목록이 나온다. 함수·구조체·매크로·Kconfig 심볼 모두 된다.
- 버전 비교: URL의 `v6.18`만 바꾸면 같은 파일의 다른 버전을 본다. 벤더 커널이 5.15이면 5.15로 놓고 읽는다.

(이 절은 웹 서비스 사용법이라 VM에서 실행한 출력이 없다. 이 노트의 줄 번호는 6.18.55 기준이라 elixir의 v6.18 태그와 몇 줄 다를 수 있다.)

### 4.4 이 파일 담당자는 누구인가 — MAINTAINERS와 `get_maintainer.pl`

#### 예제 7 — PL011 드라이버의 담당자 찾기

무엇을 확인하나: 패치를 보낼 사람·메일링 리스트를 스크립트로 뽑고, 그 근거가 되는 MAINTAINERS 항목을 직접 읽는다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55; ./scripts/get_maintainer.pl --nogit --nogit-fallback -f drivers/tty/serial/amba-pl011.c'
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55; n=$(grep -n "^ARM PRIMECELL UART PL010 AND PL011 DRIVERS" MAINTAINERS | cut -d: -f1); sed -n "${n},$((n+8))p" MAINTAINERS; echo "($n)"; n=$(grep -n "^TTY LAYER AND SERIAL DRIVERS" MAINTAINERS | cut -d: -f1); sed -n "${n},$((n+14))p" MAINTAINERS; echo "($n)"'
```

```text
Russell King <linux@armlinux.org.uk> (maintainer:ARM PRIMECELL UART PL010 AND PL011 DRIVERS)
Greg Kroah-Hartman <gregkh@linuxfoundation.org> (maintainer:TTY LAYER AND SERIAL DRIVERS)
Jiri Slaby <jirislaby@kernel.org> (maintainer:TTY LAYER AND SERIAL DRIVERS)
linux-kernel@vger.kernel.org (open list:TTY LAYER AND SERIAL DRIVERS)
linux-serial@vger.kernel.org (open list:TTY LAYER AND SERIAL DRIVERS)
```

```text
ARM PRIMECELL UART PL010 AND PL011 DRIVERS
M:	Russell King <linux@armlinux.org.uk>
S:	Odd Fixes
F:	drivers/tty/serial/amba-pl01*.c
F:	include/linux/amba/serial.h

ARM PRIMECELL VIC PL190/PL192 DRIVER
M:	Linus Walleij <linus.walleij@linaro.org>
L:	linux-arm-kernel@lists.infradead.org (moderated for non-subscribers)
(2217)
TTY LAYER AND SERIAL DRIVERS
M:	Greg Kroah-Hartman <gregkh@linuxfoundation.org>
M:	Jiri Slaby <jirislaby@kernel.org>
L:	linux-kernel@vger.kernel.org
L:	linux-serial@vger.kernel.org
S:	Supported
T:	git git://git.kernel.org/pub/scm/linux/kernel/git/gregkh/tty.git
F:	Documentation/devicetree/bindings/serial/
F:	Documentation/driver-api/serial/
F:	drivers/tty/
F:	include/linux/selection.h
F:	include/linux/serial.h
F:	include/linux/serial_core.h
F:	include/linux/sysrq.h
F:	include/linux/tty*.h
(26167)
```

출력에서 볼 것:

- 한 파일에 **두 겹의 담당**이 걸린다: 좁은 항목(PL011 드라이버, `F: drivers/tty/serial/amba-pl01*.c`)과 넓은 항목(TTY 전체, `F: drivers/tty/`). `get_maintainer.pl`은 둘을 합쳐서 보여 준다. 패치는 이 사람들 + 리스트로 보낸다(E7).
- 태그 의미: `M:` 메인테이너, `L:` 메일링 리스트, `S:` 상태(`Supported` = 돈 받고 관리, `Odd Fixes` = 가끔 고침), `T:` 그 서브시스템의 git 트리, `F:` 파일 패턴. MAINTAINERS 파일 첫 40줄에 이 규칙이 적혀 있다.
- `--nogit`은 git 기록(최근 커밋한 사람) 분석을 끈 것이다. 이 트리는 git 저장소가 아니라 그 기능을 쓸 수 없다. 실제 저장소에서는 옵션 없이 돌리면 최근 기여자도 함께 나온다.
- 실무에서 MAINTAINERS의 진짜 쓸모: **이 코드의 "현재" git 트리(`T:`)와 리스트를 알면, 아직 릴리스 안 된 수정이 이미 있는지 lore.kernel.org 리스트 아카이브에서 찾을 수 있다**. 벤더 커널에서 버그를 만나면 먼저 거기를 뒤진다.

### 4.5 크래시 주소 → 소스 줄 — `scripts/faddr2line`

Oops 로그에는 `pl011_int+0x60/0x5e0` 같은 "심볼+오프셋/크기"가 찍힌다. 펌웨어에서 PC 값을 `.map` 파일과 `addr2line`으로 풀던 것과 같다. 커널은 KASLR 때문에 절대 주소가 매번 바뀌므로 **심볼 기준 오프셋**으로 푸는 `faddr2line`을 쓴다.

#### 예제 8 — `faddr2line`으로 오프셋을 file:line으로

무엇을 확인하나: 디버그 정보가 있는 `vmlinux`에서 함수+오프셋을 소스 줄로 바꾸고, 크기가 틀리면 어떻게 거절하는지 본다.

```sh
limactl shell elinux -- bash -lc 'S=/var/lab/src/linux-6.18.55; cd /var/lab/build/linux
$S/scripts/faddr2line vmlinux vfs_read+0x40/0x2c8 2>&1 | head
nm -S vmlinux | grep -w -E "vfs_read|pl011_int|n_tty_read|tty_read"
$S/scripts/faddr2line vmlinux pl011_int+0x60 2>&1 | head
$S/scripts/faddr2line vmlinux vfs_read+0x40/0x33c'
```

```text
skipping vfs_read address at 0xffff800080371e2c due to size mismatch (0x2c8 != 0x33c)
no match for vfs_read+0x40/0x2c8
ffff80008097a60c 0000000000000578 t n_tty_read
ffff8000809bdca0 00000000000005e0 t pl011_int
ffff800080973a70 0000000000000218 t tty_read
ffff800080371dec 000000000000033c T vfs_read
pl011_int+0x60/0x5e0:
pl011_dma_rx_irq at drivers/tty/serial/amba-pl011.c:936
(inlined by) pl011_int at drivers/tty/serial/amba-pl011.c:1619
vfs_read+0x40/0x33c:
get_current at arch/arm64/include/asm/current.h:19
(inlined by) access_ok at arch/arm64/include/asm/uaccess.h:46
(inlined by) vfs_read at fs/read_write.c:560
```

출력에서 볼 것:

- 첫 줄: `/0x2c8`(함수 크기)를 일부러 틀리게 줬더니 "size mismatch"로 거절했다. **Oops에 찍힌 `/크기`는 "이 vmlinux가 그 커널과 같은 빌드인가"를 검사하는 장치**다. 다른 빌드의 vmlinux로 풀면 엉뚱한 줄이 나오는 사고를 막아 준다. 현장에서 가장 흔한 실수가 "Oops가 난 이미지와 다른 vmlinux로 디코딩"이다.
- (이 블록의 명령은 실제로는 예제 12 스크립트의 앞부분과 별도 명령 한 번으로 나눠 실행한 것을 모아 보인 것이다. 출력은 각 실행 결과 그대로다.)
- `(inlined by)` 체인: `vfs_read+0x40`은 `vfs_read` 본문 560행의 `access_ok()` 안, 다시 그 안의 `get_current()`였다. 커널은 `static inline`을 많이 쓰므로 한 주소가 여러 겹의 소스 위치를 가진다.
- `pl011_int+0x60`은 1619행(`pl011_dma_rx_irq` 호출 지점)이다. §6에서 이 함수를 다시 만난다.
- 이게 되려면 `CONFIG_DEBUG_INFO`가 켜진 vmlinux가 필요하다. 랩 설정은 `CONFIG_DEBUG_INFO=y`, `CONFIG_DEBUG_INFO_REDUCED=y`다(예제 9). 제품 빌드라도 **배포한 이미지와 짝이 되는 vmlinux를 반드시 보관**해야 한다. 로그 전체를 한 번에 풀 때는 `scripts/decode_stacktrace.sh vmlinux < oops.txt`를 쓴다(M3).

### 4.6 "X는 어디 있나" — 질문별 첫 수

| 찾는 것 | 첫 수 | 예 |
|---|---|---|
| dmesg에 찍힌 메시지를 낸 코드 | 메시지 문자열 일부를 grep (형식 문자열 `%d` 앞뒤 고정 부분) | `"Serial: AMBA PL011"` |
| DT 노드의 드라이버 | compatible 문자열을 `drivers/`에서 grep | `"arm,pl011"` |
| `/dev/xxx`를 만드는 드라이버 | 장치 이름 grep, `/proc/devices`의 major | `"ttyAMA"`, major 204 |
| 함수 정의 | `^반환형 이름(` grep, elixir, clangd | `'^ssize_t vfs_read('` |
| 함수 포인터로 불리는 구현 | ops 구조체 초기화 grep | `'\.read_iter\s*=\s*tty_read'` |
| syscall 구현 | `SYSCALL_DEFINE<N>(이름,` grep | `'SYSCALL_DEFINE3(read,'` |
| Kconfig 옵션이 켜는 파일 | Makefile에서 `CONFIG_이름` grep | `obj-$(CONFIG_SERIAL_AMBA_PL011)` |
| Kconfig 옵션 설명·의존성 | `Kconfig` 파일에서 `config 이름` grep, `make menuconfig`의 `/` 검색 | `config SERIAL_AMBA_PL011` |
| sysfs 속성의 구현 | 속성 이름 + `_show`/`_store` grep | `driver_override_store` |
| 크래시 주소의 소스 줄 | `scripts/faddr2line vmlinux 심볼+off/size` | 예제 8 |
| 이 파일의 담당자 | `scripts/get_maintainer.pl -f 파일` | 예제 7 |
| 내 보드 커널에 들어 있나 | `.config`의 `=y`/`=m`, 빌드 트리의 `.o`, `/proc/kallsyms` | §5 |

---

## 5. 빌드된 이미지 해부

소스 트리가 "설계도 창고"라면, 빌드 결과는 그중 **이 보드용으로 고른 부분**이다. 랩 빌드 트리(`/var/lab/build/linux`)에서 무엇이 실제로 들어갔는지 잰다.

### 5.1 `.config` — 빌트인(`=y`) vs 모듈(`=m`)

#### 예제 9 — 설정 상태 수, 모듈 수, 이미지 크기

무엇을 확인하나: `.config`에서 `=y`/`=m`/꺼짐 개수, 만들어진 `.ko` 수, `vmlinux`·`Image` 크기.

```sh
cat > ~/.elinux_scratch/E1/e1_image.sh <<'EOF'
#!/bin/bash
B=/var/lab/build/linux
cd $B
echo "== .config"
echo "=y: $(grep -c '=y$' .config)  =m: $(grep -c '=m$' .config)  not set: $(grep -c 'is not set' .config)"
grep -E '^CONFIG_(LOCALVERSION|MODULES|IKCONFIG_PROC|KALLSYMS_ALL|DEBUG_INFO[A-Z_]*|SERIAL_AMBA_PL011|SERIAL_AMBA_PL011_CONSOLE)=' .config
echo "== modules"
echo "modules.order: $(wc -l < modules.order)  modules.builtin: $(wc -l < modules.builtin)  .ko files: $(find . -name '*.ko' | wc -l)"
echo "ko total bytes: $(find . -name '*.ko' -print0 | du -cb --files0-from=- | tail -1)"
echo "== sizes"
ls -l vmlinux vmlinux.unstripped vmlinux.a vmlinux.o arch/arm64/boot/Image 2>&1 | awk '{print $5, $9}'
size vmlinux
file vmlinux | cut -c1-150
echo "System.map lines: $(wc -l < System.map)"
EOF
limactl shell elinux -- bash /Users/donh/.elinux_scratch/E1/e1_image.sh
```

```text
== .config
=y: 3296  =m: 1272  not set: 4947
CONFIG_LOCALVERSION=""
CONFIG_IKCONFIG_PROC=y
CONFIG_KALLSYMS_ALL=y
CONFIG_MODULES=y
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y
CONFIG_DEBUG_INFO=y
CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT=y
CONFIG_DEBUG_INFO_REDUCED=y
CONFIG_DEBUG_INFO_COMPRESSED_NONE=y
== modules
modules.order: 1423  modules.builtin: 1347  .ko files: 1423
ko total bytes: 343747088	total
== sizes
42740224 arch/arm64/boot/Image
162255104 vmlinux
389618 vmlinux.a
324684024 vmlinux.o
163692424 vmlinux.unstripped
   text	   data	    bss	    dec	    hex	filename
21641971	20961815	 708144	43311930	294e33a	vmlinux
vmlinux: ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), statically linked, BuildID[sha1]=965134744436f6c5a193234c4400d0a30e3d76a9, with
System.map lines: 224024
```

출력에서 볼 것:

- `=y` 3,296개, `=m` 1,272개. `=m` 옵션 1,272개가 `.ko` 1,423개를 만든다 — 한 옵션이 여러 모듈을 만들 수 있다(예: 드라이버 + 공용 헬퍼). `modules.builtin` 1,347줄은 "모듈로도 만들 수 있는(tristate) 코드인데 이번엔 `=y`로 들어간 것" 목록이다. `amba-pl011.ko`가 여기 있다(예제 12).
- 모듈 1,423개 합계가 **344 MB**다. 디버그 정보가 들어 있어서 그렇다. 제품 rootfs에는 `INSTALL_MOD_STRIP=1`로 strip해서 넣는다(E2, K 모듈).
- `vmlinux.a`가 389,618바이트밖에 안 되는 것은 **thin archive**이기 때문이다. 실제 `.o`는 복사하지 않고 경로만 들고 있다(예제 10에서 `ar t`로 목록을 읽는다).
- `size vmlinux`의 `text`/`data` 열은 섹션 플래그로 거칠게 나눈 것이라 `.rodata`가 `data` 쪽에 섞여 들어간다. 정확히 보려면 `size -A`(섹션별)를 쓴다 — 다음 절.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">랩 커널 .config — 빌트인(=y) vs 모듈(=m)</text>
  <rect x="40.0" y="40" width="207.8" height="30" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="143.9" y="60" text-anchor="middle" font-size="12">=y 3,296</text>
  <rect x="247.8" y="40" width="80.2" height="30" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="287.9" y="60" text-anchor="middle" font-size="12">=m 1,272</text>
  <rect x="328.1" y="40" width="311.9" height="30" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="484.0" y="60" text-anchor="middle" font-size="12">is not set 4,947</text>
  <text x="40" y="92" font-size="12">세 상태 합 9,515개 옵션 — =m 옵션 1,272개가 .ko 1,423개를 만든다 (한 옵션 → 여러 .ko 가능)</text>
  <text x="340" y="126" text-anchor="middle" font-size="13" font-weight="bold">.ko 1,423개는 어디서 오나 (modules.order 최상위 디렉터리)</text>
  <text x="132" y="154" text-anchor="end" font-size="12">drivers/</text>
  <rect x="140" y="142" width="440.0" height="15" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="586.0" y="154" font-size="12">1094</text>
  <text x="132" y="175" text-anchor="end" font-size="12">sound/</text>
  <rect x="140" y="163" width="88.9" height="15" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="234.9" y="175" font-size="12">221</text>
  <text x="132" y="196" text-anchor="end" font-size="12">net/</text>
  <rect x="140" y="184" width="22.9" height="15" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="168.9" y="196" font-size="12">57</text>
  <text x="132" y="217" text-anchor="end" font-size="12">crypto/</text>
  <rect x="140" y="205" width="12.5" height="15" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="158.5" y="217" font-size="12">31</text>
  <text x="132" y="238" text-anchor="end" font-size="12">lib/</text>
  <rect x="140" y="226" width="3.6" height="15" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="149.6" y="238" font-size="12">9</text>
  <text x="132" y="259" text-anchor="end" font-size="12">fs/</text>
  <rect x="140" y="247" width="2.4" height="15" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="148.4" y="259" font-size="12">6</text>
  <text x="132" y="280" text-anchor="end" font-size="12">arch/</text>
  <rect x="140" y="268" width="2.0" height="15" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="148.0" y="280" font-size="12">5</text>
  <line x1="140" y1="138" x2="140" y2="289" stroke="currentColor"/>
</svg>
```
그림 7 — 랩 커널 `.config`의 옵션 상태와 모듈 출처(예제 9·12 실측). 모듈의 77%가 `drivers/`에서, 16%가 `sound/`에서 나온다.

### 5.2 `vmlinux`의 섹션 — 무엇이 메모리에 올라가나

#### 예제 10 — `size -A`와 `readelf -l`

무엇을 확인하나: `vmlinux`의 큰 섹션들, 그리고 어떤 섹션이 어느 LOAD 세그먼트로 메모리에 올라가는지.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/build/linux; size -A vmlinux | awk "\$2>100000 || NR<3"; readelf -lW vmlinux | sed -n 1,30p'
limactl shell elinux -- bash -lc 'cd /var/lab/build/linux; readelf -SW vmlinux | grep -E "symtab|strtab|shstrtab"; size -A vmlinux | awk "\$1 !~ /^.debug/ && NR>2 && \$1!=\"Total\" {s+=\$2} END{print \"nondebug\", s}"'
```

```text
vmlinux  :
section                             size                   addr
.text                           19193856   18446603338368745472
.rodata                         13657936   18446603338387947520
__ksymtab_gpl                     100800   18446603338401682708
__ksymtab_strings                 274927   18446603338401783508
.init.text                        585512   18446603338402168832
.altinstructions                  391992   18446603338402802156
.init.data                       1294375   18446603338403250176
.rela.dyn                         662832   18446603338404626216
.relr.dyn                         184200   18446603338405289048
.data                            5770472   18446603338405511168
__bug_table                       133740   18446603338411281640
.bss                              708144   18446603338411421696
.debug_aranges                    293376                      0
.debug_info                     61928134                      0
.debug_abbrev                    4850069                      0
.debug_line                     26395534                      0
.debug_frame                     4146248                      0
.debug_str                       5900771                      0
.debug_line_str                   297868                      0
.debug_rnglists                  5340643                      0
Total                          152464611

Elf file type is DYN (Shared object file)
Entry point 0xffff800080000000
There are 4 program headers, starting at offset 64

Program Headers:
  Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
  LOAD           0x010000 0xffff800080000000 0xffff800080000000 0x125e000 0x125e000 R E 0x10000
  LOAD           0x1270000 0xffff800081260000 0xffff800081260000 0x1662a00 0x170fe30 RWE 0x10000
  NOTE           0x1fed324 0xffff800081fdd324 0xffff800081fdd324 0x000054 0x000054 R   0x4
  GNU_STACK      0x000000 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x8

 Section to Segment mapping:
  Segment Sections...
   00     .head.text .text 
   01     .rodata .pci_fixup __ksymtab __ksymtab_gpl __ksymtab_strings __param __modver __ex_table .notes .hyp.rodata .got .got.plt .rodata.text .init.text .exit.text .altinstructions .init.data runtime_shift_d_hash_shift runtime_ptr_dentry_hashtable .data..percpu .hyp.data..percpu .hyp.reloc .rela.dyn .relr.dyn .data __bug_table .hyp.data .mmuoff.data.write .mmuoff.data.read .pecoff_edata_padding .bss 
   02     .notes 
   03
```

```text
  [ 5] __ksymtab         PROGBITS        ffff800081f69740 1f79740 00fdd4 00   A  0   0  4
  [ 6] __ksymtab_gpl     PROGBITS        ffff800081f79514 1f89514 0189c0 00   A  0   0  4
  [ 7] __ksymtab_strings PROGBITS        ffff800081f91ed4 1fa1ed4 0431ef 01 AMS  0   0  1
  [43] .symtab           SYMTAB          0000000000000000 90eb3b0 660330 18     44 246757  8
  [44] .strtab           STRTAB          0000000000000000 974b6e0 370c92 00      0   0  1
  [45] .shstrtab         STRTAB          0000000000000000 9abc372 00020c 00      0   0  1
nondebug 43311968
```

출력에서 볼 것:

- **메모리에 올라가는 섹션 합계 = 43,311,968바이트(43.3 MB)**, `size`의 `dec` 값과 같다. `.text` 19.2 MB, `.rodata` 13.7 MB, `.data` 5.8 MB, `.init.*` 1.9 MB(부팅이 끝나면 "Freeing unused kernel memory"로 해제), `.bss` 0.7 MB.
- 나머지는 **`.debug_*` 109 MB**와 `.symtab`+`.strtab` 10.3 MB(0x660330 + 0x370c92)다. 이것들은 주소가 0이고 LOAD 세그먼트에 안 들어간다 — 디버거·`faddr2line`만 쓴다.
- LOAD 세그먼트는 둘: 코드(`R E`)와 나머지 전부(`RWE`). `RWE`는 링커 스크립트가 여러 섹션을 한 세그먼트로 묶어서 생긴 표시일 뿐이고, 실제 권한은 커널이 부팅 중 자기 페이지 테이블에서 섹션별로 다시 건다(`CONFIG_STRICT_KERNEL_RWX=y` — `.rodata`는 읽기 전용, `.text`는 읽기+실행, 데이터는 실행 금지).
- `Entry point 0xffff800080000000`은 링크 시점 가상 주소다. 실제로는 KASLR이 이 위치를 매번 옮긴다(§5.4).
- 부트로더가 싣는 `Image`(42.7 MB)는 `vmlinux`를 `objcopy -O binary`로 벗긴 것이라 적재 부분(`.bss` 제외 42.6 MB)과 크기가 거의 같다(C6).

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">vmlinux 해부 — 파일 162.3 MB 중 메모리에 올라가는 것은 43.3 MB</text>
  <text x="40" y="48" font-size="12">파일 (MB) — 디버그 정보가 3분의 2</text>
  <rect x="40.0" y="56" width="157.5" height="30" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="118.8" y="76" text-anchor="middle" font-size="12">적재 42.6</text>
  <rect x="197.5" y="56" width="403.6" height="30" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="399.3" y="76" text-anchor="middle" font-size="12">DWARF .debug_* 109.2</text>
  <rect x="601.1" y="56" width="38.1" height="30" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="40" y="128" font-size="12">메모리에 올라가는 섹션 (MB, 확대)</text>
  <rect x="40.0" y="136" width="265.9" height="30" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="172.9" y="156" text-anchor="middle" font-size="12">.text 19.2</text>
  <rect x="305.9" y="136" width="189.2" height="30" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="400.5" y="156" text-anchor="middle" font-size="12">.rodata 13.7</text>
  <rect x="495.1" y="136" width="79.9" height="30" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="535.1" y="156" text-anchor="middle" font-size="12">.data 5.8</text>
  <rect x="575.0" y="136" width="26.0" height="30" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <rect x="601.1" y="136" width="38.9" height="30" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="40" y="104" font-size="12">주황 = .symtab + .strtab 10.3 (심볼 이름표, 적재 안 됨)</text>
  <text x="40" y="196" font-size="12">빨강 = .init.* 1.9 (부팅 후 해제) · 회색 = .bss 0.7 + __ksymtab·rela 등 2.1</text>
  <text x="40" y="222" font-size="12">부트로더가 싣는 Image(objcopy -O binary) = 42.7 MB → 적재 부분과 거의 같은 크기</text>
</svg>
```
그림 8 — `vmlinux` 파일 162 MB의 구성(예제 9·10 실측). 파일의 3분의 2가 디버그 정보이고, 실제 RAM에 올라가는 것은 43 MB다. 아래 막대는 적재 부분만 확대한 것이다.

### 5.3 서브시스템별 크기 — 빌트인 객체를 디렉터리로 묶기

#### 예제 11 — `vmlinux.a`의 객체 4,338개를 디렉터리별로 합산

무엇을 확인하나: 커널 이미지에 들어간 코드가 어느 서브시스템에서 왔는지. `vmlinux.a`(thin archive)의 멤버 목록을 `ar t`로 읽고, 각 `.o`에 `size`를 돌려 최상위 디렉터리별로 더한다.

```sh
cat > ~/.elinux_scratch/E1/e1_group.sh <<'EOF'
#!/bin/bash
# group size(1) of every object listed in vmlinux.a by top-level dir (and drivers/<sub>)
cd /var/lab/build/linux
ar t vmlinux.a > /var/tmp/E1/e1_objs.txt
echo "objects in vmlinux.a: $(wc -l < /var/tmp/E1/e1_objs.txt)"
xargs size < /var/tmp/E1/e1_objs.txt | awk 'NR>1{print $1,$2,$3,$6}' > /var/tmp/E1/e1_sizes.txt
echo "== by top dir (text data bss nobj)"
awk '{split($4,p,"/"); k=p[1]; if(k=="arch") k=p[1]"/"p[2]; t[k]+=$1; d[k]+=$2; b[k]+=$3; n[k]++; T+=$1}
     END{for(k in t) printf "%-14s %9d %9d %8d %5d %5.1f%%\n",k,t[k],d[k],b[k],n[k],100*t[k]/T; printf "TOTAL text %d\n",T}' /var/tmp/E1/e1_sizes.txt | sort -k2 -nr
echo "== drivers/<sub> top 15 by text"
awk '$4 ~ /^drivers\//{split($4,p,"/"); k=p[2]; t[k]+=$1; n[k]++} END{for(k in t) printf "%-14s %9d %5d\n",k,t[k],n[k]}' /var/tmp/E1/e1_sizes.txt | sort -k2 -nr | head -15
EOF
limactl shell elinux -- bash /Users/donh/.elinux_scratch/E1/e1_group.sh
```

```text
objects in vmlinux.a: 4338
== by top dir (text data bss nobj)
drivers         21307463   5716685   290331  2839  66.2%
fs               2435996     65274    25543   328   7.6%
kernel           2313535    631550   171669   245   7.2%
net              2286591    109438    25045   251   7.1%
mm               1154338     83541    79579    87   3.6%
arch/arm64       1148223    146841    86905   184   3.6%
lib               628887     23753     2628   211   2.0%
block             310306     15348     3160    47   1.0%
io_uring          166442      1880       16    37   0.5%
crypto            155421     14132     9446    52   0.5%
security           90815     14297     5128    23   0.3%
ipc                69860      1889       20    11   0.2%
virt               69491      2782      216     9   0.2%
init               21005     15307      129     7   0.1%
arch/arm            6413      2637     2329     4   0.0%
certs               1263        24        8     2   0.0%
usr                  520         0        0     1   0.0%
TOTAL text 32166569
== drivers/<sub> top 15 by text
pinctrl          3346627   175
clk              3255940   524
net              2469044   206
usb              1294771   141
tty               688096    73
acpi              681433   225
mmc               583841    61
regulator         555014    40
pci               551676    89
scsi              442980    38
soc               416643    70
mtd               381991    64
phy               357279    53
firmware          356373    76
pmdomain          340732    49
```

출력에서 볼 것:

- 이미지 안에서도 **`drivers/`가 66%**다. 코어(`kernel/` + `mm/`)는 약 11%, arm64 아키텍처 코드는 3.6%(1.1 MB)뿐이다.
- 객체 파일의 `text` 열은 `.text`뿐 아니라 `.rodata`, `.init.text` 같은 읽기 전용 섹션을 다 더한 값이라 합계(32.2 MB)가 `vmlinux`의 `.text`(19.2 MB)보다 크다. **비율을 보는 용도**로 쓴다.
- `drivers/` 안 1·2위가 **pinctrl(3.3 MB)과 clk(3.3 MB)**다. arm64 defconfig는 "이미지 하나로 수십 개 SoC를 부팅"하는 게 목표라 모든 SoC의 핀·클럭 드라이버를 빌트인으로 넣는다(부팅에 필요하므로 모듈로 뺄 수 없다). **제품 커널은 자기 SoC 것만 남겨서 이 부분을 크게 줄인다** — §8의 크기 다이어트 장면.

```svg
<svg viewBox="0 0 680 398" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">vmlinux에 빌트인된 코드 — 디렉터리별 text 크기 (객체 4,338개 합)</text>
  <text x="142" y="61" text-anchor="end" font-size="12">drivers/</text>
  <rect x="150" y="49" width="360.0" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="516.0" y="61" font-size="12">21.31 MB (66.2%)</text>
  <text x="142" y="85" text-anchor="end" font-size="12">fs/</text>
  <rect x="150" y="73" width="41.2" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="197.2" y="85" font-size="12">2.44 MB (7.6%)</text>
  <text x="142" y="109" text-anchor="end" font-size="12">kernel/</text>
  <rect x="150" y="97" width="39.1" height="16" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="195.1" y="109" font-size="12">2.31 MB (7.2%)</text>
  <text x="142" y="133" text-anchor="end" font-size="12">net/</text>
  <rect x="150" y="121" width="38.6" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="194.6" y="133" font-size="12">2.29 MB (7.1%)</text>
  <text x="142" y="157" text-anchor="end" font-size="12">mm/</text>
  <rect x="150" y="145" width="19.5" height="16" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="175.5" y="157" font-size="12">1.15 MB (3.6%)</text>
  <text x="142" y="181" text-anchor="end" font-size="12">arch/arm64/</text>
  <rect x="150" y="169" width="19.4" height="16" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="175.4" y="181" font-size="12">1.15 MB (3.6%)</text>
  <text x="142" y="205" text-anchor="end" font-size="12">lib/</text>
  <rect x="150" y="193" width="10.6" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="166.6" y="205" font-size="12">629 KB (2.0%)</text>
  <text x="142" y="229" text-anchor="end" font-size="12">block/</text>
  <rect x="150" y="217" width="5.2" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="161.2" y="229" font-size="12">310 KB (1.0%)</text>
  <text x="142" y="253" text-anchor="end" font-size="12">io_uring/</text>
  <rect x="150" y="241" width="2.8" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="158.8" y="253" font-size="12">166 KB (0.5%)</text>
  <text x="142" y="277" text-anchor="end" font-size="12">crypto/</text>
  <rect x="150" y="265" width="2.6" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="158.6" y="277" font-size="12">155 KB (0.5%)</text>
  <text x="142" y="301" text-anchor="end" font-size="12">security/</text>
  <rect x="150" y="289" width="1.5" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="157.5" y="301" font-size="12">91 KB (0.3%)</text>
  <text x="142" y="325" text-anchor="end" font-size="12">ipc/</text>
  <rect x="150" y="313" width="1.5" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="157.5" y="325" font-size="12">70 KB (0.2%)</text>
  <text x="142" y="349" text-anchor="end" font-size="12">init/</text>
  <rect x="150" y="337" width="1.5" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="157.5" y="349" font-size="12">21 KB (0.1%)</text>
  <line x1="150" y1="42" x2="150" y2="358" stroke="currentColor"/>
  <text x="340" y="386" text-anchor="middle" font-size="12">size(1)의 text 열 합계 = 32.2 MB · drivers가 3분의 2, 코어(kernel+mm)는 11%</text>
</svg>
```
그림 9 — `vmlinux`에 빌트인된 코드의 출처(예제 11 실측). 막대 길이는 각 디렉터리 객체들의 `size` text 열 합계.

```svg
<svg viewBox="0 0 680 374" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">drivers/ 안에서 빌트인 text가 큰 하위 디렉터리</text>
  <text x="142" y="61" text-anchor="end" font-size="12">pinctrl</text>
  <rect x="150" y="49" width="430.0" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="586.0" y="61" font-size="12">3.35 MB</text>
  <text x="142" y="85" text-anchor="end" font-size="12">clk</text>
  <rect x="150" y="73" width="418.3" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="574.3" y="85" font-size="12">3.26 MB</text>
  <text x="142" y="109" text-anchor="end" font-size="12">net</text>
  <rect x="150" y="97" width="317.2" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="473.2" y="109" font-size="12">2.47 MB</text>
  <text x="142" y="133" text-anchor="end" font-size="12">usb</text>
  <rect x="150" y="121" width="166.4" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="322.4" y="133" font-size="12">1.29 MB</text>
  <text x="142" y="157" text-anchor="end" font-size="12">tty</text>
  <rect x="150" y="145" width="88.4" height="16" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="244.4" y="157" font-size="12">0.69 MB</text>
  <text x="142" y="181" text-anchor="end" font-size="12">acpi</text>
  <rect x="150" y="169" width="87.6" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="243.6" y="181" font-size="12">0.68 MB</text>
  <text x="142" y="205" text-anchor="end" font-size="12">mmc</text>
  <rect x="150" y="193" width="75.0" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="231.0" y="205" font-size="12">0.58 MB</text>
  <text x="142" y="229" text-anchor="end" font-size="12">regulator</text>
  <rect x="150" y="217" width="71.3" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="227.3" y="229" font-size="12">0.56 MB</text>
  <text x="142" y="253" text-anchor="end" font-size="12">pci</text>
  <rect x="150" y="241" width="70.9" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="226.9" y="253" font-size="12">0.55 MB</text>
  <text x="142" y="277" text-anchor="end" font-size="12">scsi</text>
  <rect x="150" y="265" width="56.9" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="212.9" y="277" font-size="12">0.44 MB</text>
  <text x="142" y="301" text-anchor="end" font-size="12">soc</text>
  <rect x="150" y="289" width="53.5" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="209.5" y="301" font-size="12">0.42 MB</text>
  <text x="142" y="325" text-anchor="end" font-size="12">mtd</text>
  <rect x="150" y="313" width="49.1" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="205.1" y="325" font-size="12">0.38 MB</text>
  <line x1="150" y1="42" x2="150" y2="334" stroke="currentColor"/>
  <text x="340" y="362" text-anchor="middle" font-size="12">defconfig는 여러 SoC를 한 이미지로 부팅하려고 pinctrl·clk 드라이버를 다 빌트인한다</text>
</svg>
```
그림 10 — `drivers/` 안에서 빌트인 코드가 큰 하위 디렉터리(예제 11 실측). 주황은 §6에서 따라갈 `drivers/tty`(serial 포함 0.69 MB).

### 5.4 가장 큰 심볼, `System.map`, 모듈 출처

#### 예제 12 — `nm --size-sort`, 심볼 타입, 모듈이 어디서 오나

무엇을 확인하나: 이미지에서 가장 큰 심볼들, `System.map`의 심볼 타입 분포, `.ko`가 어느 디렉터리에서 오는지, PL011이 빌트인인지. (실제 실행한 스크립트에는 예제 8의 `faddr2line`·`nm -S` 줄과 `grep -c '' modules.builtin` 한 줄이 더 있었다. 그 출력은 예제 8·9에 이미 있으므로 여기서는 스크립트와 출력 양쪽에서 뺐다.)

```sh
cat > ~/.elinux_scratch/E1/e1_img2.sh <<'EOF'
#!/bin/bash
S=/var/lab/src/linux-6.18.55; B=/var/lab/build/linux
cd $B
echo "== top 10 biggest symbols in vmlinux (nm --size-sort)"
nm --size-sort -S -r vmlinux | head -10
echo "== top 8 biggest text (t/T) symbols"
nm --size-sort -S -r vmlinux | awk '$3 ~ /^[tT]$/' | head -8
echo "== System.map symbol type counts"
awk '{print $2}' System.map | sort | uniq -c | sort -nr | head -8
echo "== where is pl011 / tty / n_tty: built-in or module"
grep -E 'amba-pl011|n_tty|tty_io' modules.builtin modules.order | head
echo "== modules by top dir (modules.order)"
cut -d/ -f1 modules.order | sort | uniq -c | sort -nr
echo "== drivers modules by subdir top 8"
awk -F/ '$1=="drivers"{print $2}' modules.order | sort | uniq -c | sort -nr | head -8
EOF
limactl shell elinux -- bash /Users/donh/.elinux_scratch/E1/e1_img2.sh
```

```text
== top 10 biggest symbols in vmlinux (nm --size-sort)
ffff800082980000 fffffffffffefe30 B _end
ffff800081c3de20 00000000002f8160 D kallsyms_seqs_of_names
ffff800081919628 00000000002487d8 D kallsyms_names
ffff800081b631d8 00000000000dac40 D kallsyms_offsets
ffff800082247f28 00000000000a1d30 r __pi_rela_start
ffff80008208a9ec 000000000005fb38 R __alt_instructions
ffff800082346cc8 0000000000058000 d _printk_rb_static_infos
ffff8000822e9c58 000000000002cf88 r __pi_relr_start
ffff800082940fe8 0000000000022000 B cpu_topology
ffff800081f44da0 00000000000219b0 d __kvm_nvhe_$d
== top 8 biggest text (t/T) symbols
ffff800080000000 0000000000010000 T _text
ffff800080871380 0000000000004238 t ma35d1_clocks_probe
ffff800080854f80 000000000000390c t imx8mp_clocks_probe
ffff800080fafb0c 00000000000033e8 t hidinput_configure_usage
ffff800080858dc0 00000000000033ac t imx8mq_clocks_probe
ffff800080d07e48 0000000000003240 t __sh_eth_get_regs
ffff80008084f020 0000000000003200 t imx8mm_clocks_probe
ffff800080852220 0000000000002d60 t imx8mn_clocks_probe
== System.map symbol type counts
 116344 d
  42879 t
  29516 r
  28202 T
   3480 b
   2308 D
   1018 B
    213 W
== where is pl011 / tty / n_tty: built-in or module
modules.builtin:kernel/drivers/tty/serial/amba-pl011.ko
== modules by top dir (modules.order)
   1094 drivers
    221 sound
     57 net
     31 crypto
      9 lib
      6 fs
      5 arch
== drivers modules by subdir top 8
    219 media
    125 net
    111 gpu
     62 clk
     54 usb
     44 phy
     33 iio
     25 soc
```

출력에서 볼 것:

- 가장 큰 데이터는 **kallsyms 표 자체**(`kallsyms_seqs_of_names` 3.1 MB + `kallsyms_names` 2.4 MB + `kallsyms_offsets` 0.9 MB ≈ 6.4 MB)다. `CONFIG_KALLSYMS_ALL=y`로 모든 심볼 이름을 커널 안에 넣어 둔 비용이다. 덕분에 Oops가 `pl011_int+0x60`처럼 이름으로 찍히고 `/proc/kallsyms`가 동작한다. 크기가 아주 빡빡한 제품에서는 줄이는 대상이 된다. (`_end`의 크기 `fffffffffffefe30`은 nm이 다음 심볼과의 거리로 계산하다 음수가 된 것으로, 의미 없는 값이다.)
- 가장 큰 함수들은 **i.MX8 계열 클럭 probe 함수**(`imx8mp_clocks_probe` 14.6 KB 등)다. 클럭 트리 수백 개를 하나하나 등록하는 직선 코드라 그렇다. Don이 i.MX8 보드를 만지면 이 파일(`drivers/clk/imx/clk-imx8mp.c`)을 반드시 열게 된다.
- 심볼 타입: 대문자 = 전역(`T` 함수, `D` 데이터, `R` 읽기 전용, `B` bss), 소문자 = 파일 static. `t`(static 함수)가 `T`(전역 함수)보다 많다 — 커널 코드 대부분은 파일 안에 숨겨져 있고, 밖으로 보이는 것은 적다.
- `amba-pl011.ko`가 `modules.builtin`에 있다 = tristate 드라이버를 `=y`로 넣었다. `n_tty`·`tty_io`는 목록에 없다 — 애초에 모듈로 만들 수 없는(bool) 코어 코드라서다.
- 모듈은 `drivers/`(1,094개) 중에서도 `media/`(219개, 카메라·튜너)가 가장 많다. 부팅에 필요 없는 장치들이 모듈로 빠진다는 원칙이 보인다.

### 5.5 살아 있는 커널에서 보기 — `/proc/kallsyms`, `/sys/module`

이제 랩 커널을 QEMU로 부팅해서, 빌드 산출물(`System.map`)과 실행 중인 커널을 비교한다. 게스트 안에서 돌릴 스크립트와 C 프로그램을 initramfs 복사본에 넣는다.

#### 예제 13 — 게스트에서 심볼·모듈·장치 관찰

무엇을 확인하나: 실행 중 커널의 심볼 수와 주소, `/sys/module`에 무엇이 있는지, 모듈 하나를 `insmod`하면 무엇이 생기는지, `/dev/ttyAMA0`이 누구 것인지, 그리고 `read()` 중인 프로세스의 커널 스택.

```sh
cat > ~/.elinux_scratch/E1/e1_guest.sh <<'EOF'
#!/bin/sh
# runs inside the QEMU guest (lab kernel 6.18.55, BusyBox)
echo "== uname"; uname -r -m
echo "== kallsyms"
echo "lines: $(wc -l < /proc/kallsyms)"
grep -w -E 'vfs_read|ksys_read|tty_read|n_tty_read|pl011_int|__arm64_sys_read' /proc/kallsyms
echo "== /sys/module"
echo "entries: $(ls /sys/module | wc -l)  with initstate(loadable,loaded): $(ls /sys/module/*/initstate 2>/dev/null | wc -l)"
ls /sys/module | head -12 | tr '\n' ' '; echo
ls /sys/module/printk/parameters | tr '\n' ' '; echo
echo "== insmod a loadable module"
insmod /e1_crc-ccitt.ko && cat /proc/modules
ls /sys/module/crc_ccitt | tr '\n' ' '; echo
cat /sys/module/crc_ccitt/initstate /sys/module/crc_ccitt/taint; echo "[taint end]"
grep '\[crc_ccitt\]' /proc/kallsyms
echo "== ttyAMA0: device number, driver, irq"
ls -l /dev/ttyAMA0
grep -E 'ttyAMA|^  5 ' /proc/devices
readlink /sys/class/tty/ttyAMA0/device/driver
readlink /sys/class/tty/ttyAMA0/device
grep pl011 /proc/interrupts
echo "== a reader blocked in read() on /dev/ttyAMA0: kernel stack"
dd if=/dev/ttyAMA0 of=/dev/null bs=1 count=1 2>/dev/null &
P=$!; sleep 2
echo "wchan: $(cat /proc/$P/wchan)"; echo
cat /proc/$P/stack
cat /proc/$P/syscall
kill $P
echo "== config.gz"
zcat /proc/config.gz | grep -c '=m$'
EOF
cat > ~/.elinux_scratch/E1/e1_boot.sh <<'EOF'
#!/bin/bash
set -e
W=/var/tmp/E1; mkdir -p $W; cd $W
rm -rf e1_root; mkdir e1_root; cp -a /var/lab/build/rootfs/. e1_root/
cp /var/lab/build/linux/lib/crc/crc-ccitt.ko e1_root/e1_crc-ccitt.ko
cp /Users/donh/.elinux_scratch/E1/e1_guest.sh e1_root/e1_guest.sh; chmod +x e1_root/e1_guest.sh
(cd e1_root && find . | cpio -o -H newc --quiet | gzip) > e1_initrd.gz
timeout 180 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd e1_initrd.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/e1_guest.sh" < /dev/null 2>&1 | tr -d '\r'
EOF
limactl shell elinux -- bash /Users/donh/.elinux_scratch/E1/e1_boot.sh 2>&1 | tail -80
```

```text
lab initramfs: 6.18.55 up 5.81s
== uname
6.18.55 aarch64
== kallsyms
lines: 224016
ffffc4ca26771dec T vfs_read
ffffc4ca267728e0 T ksys_read
ffffc4ca267729e0 T __arm64_sys_read
ffffc4ca26d73a70 t tty_read
ffffc4ca26d7a60c t n_tty_read
ffffc4ca26dbdca0 t pl011_int
== /sys/module
entries: 194  with initstate(loadable,loaded): 0
8250 8250_core 8250_exar 8250_pci 8250_pericom acpi acpiphp ahci ahci_ceva ahci_xgene amd_xgbe arm_smc_wdt 
always_kmsg_dump console_no_auto_verbose console_suspend ignore_loglevel time 
== insmod a loadable module
crc_ccitt 12288 0 - Live 0xffffc4c9b2ac8000
coresize holders initsize initstate notes refcnt sections taint uevent 
live

[taint end]
ffffc4c9b2acc25c r __kstrtab_crc_ccitt_table	[crc_ccitt]
ffffc4c9b2acc26c r __kstrtabns_crc_ccitt_table	[crc_ccitt]
ffffc4c9b2acc00c r $d	[crc_ccitt]
ffffc4c9b2acc00c r __ksymtab_crc_ccitt_table	[crc_ccitt]
ffffc4c9b2acc26d r __kstrtab_crc_ccitt	[crc_ccitt]
ffffc4c9b2acc277 r __kstrtabns_crc_ccitt	[crc_ccitt]
ffffc4c9b2acc000 r $d	[crc_ccitt]
ffffc4c9b2acc000 r __ksymtab_crc_ccitt	[crc_ccitt]
ffffc4c9b2aca000 d $d	[crc_ccitt]
ffffc4c9b2ac8020 t $x	[crc_ccitt]
ffffc4c9b2acc018 r $d	[crc_ccitt]
ffffc4c9b2acc218 r $d	[crc_ccitt]
ffffc4c9b2acc278 r $d	[crc_ccitt]
ffffc4c9b2acc278 r _note_19	[crc_ccitt]
ffffc4c9b2acc290 r _note_18	[crc_ccitt]
ffffc4c9b2aca000 d __this_module	[crc_ccitt]
ffffc4c9b2ac8020 T crc_ccitt	[crc_ccitt]
ffffc4c9b2acc018 R crc_ccitt_table	[crc_ccitt]
== ttyAMA0: device number, driver, irq
crw-------    1 0        0         204,  64 Jan  1  1970 /dev/ttyAMA0
  5 /dev/tty
  5 /dev/console
  5 /dev/ptmx
204 ttyAMA
../../../../../bus/serial-base/drivers/port
../../../9000000.pl011:0.0
 13:          0 GIC-0  33 Level     uart-pl011
== a reader blocked in read() on /dev/ttyAMA0: kernel stack
wchan: wait_woken

[<0>] wait_woken+0x84/0x94
[<0>] n_tty_read+0x434/0x578
[<0>] tty_read+0xe8/0x218
[<0>] vfs_read+0x244/0x33c
[<0>] ksys_read+0x64/0x100
[<0>] __arm64_sys_read+0x18/0x24
[<0>] invoke_syscall.constprop.0+0x40/0xe0
[<0>] el0_svc_common.constprop.0+0x38/0xe0
[<0>] do_el0_svc+0x1c/0x28
[<0>] el0_svc+0x38/0x118
[<0>] el0t_64_sync_handler+0xa0/0xe4
[<0>] el0t_64_sync+0x198/0x19c
63 0x0 0x3b324460 0x1 0x0 0x0 0x0 0xfffff0e87a00 0x423ca8
== config.gz
1272
[   35.329137] reboot: Power down
```

(`ls -l`의 파일 이름 앞뒤에 BusyBox가 붙인 색상 이스케이프 코드는 지웠다. 주소는 KASLR 때문에 부팅마다 다르다.)

출력에서 볼 것:

- **`/proc/kallsyms` 224,016줄 ≈ `System.map` 224,024줄**. 같은 빌드이므로 거의 같은 목록이다. 하지만 주소가 다르다: `System.map`의 `vfs_read`는 `ffff800080371dec`, 실행 중에는 `ffffc4ca26771dec`. 아래 12비트 이상(`...371dec`)은 같고 상위만 밀렸다 — **KASLR**(커널 주소 무작위화, `CONFIG_RANDOMIZE_BASE=y`). QEMU가 DT의 `/chosen/kaslr-seed`에 부팅마다 새 난수를 넣어 준다(`virt.dts` 391행). 그래서 Oops 분석에는 절대 주소가 아니라 `심볼+오프셋`을 쓴다(예제 8).
- **`/sys/module`에 194개가 있는데 그중 로드 가능한 모듈(`initstate` 파일 보유)은 0개**다. 빌트인 코드도 모듈 파라미터가 있으면 `/sys/module/<이름>/parameters`가 생긴다. 예: `printk`의 `ignore_loglevel`, `time` — 부트 인자 `printk.time=1`이 바로 이 파일이다. "`/sys/module`에 있다 ≠ 모듈로 로드됐다"는 점이 함정이다.
- `insmod` 후 `crc_ccitt`가 `/proc/modules`에 `Live`로 나오고, `/sys/module/crc_ccitt`에 `initstate`(`live`), `refcnt`, `holders`, `sections`가 생겼다. **모듈 심볼은 `/proc/kallsyms`에 `[crc_ccitt]` 꼬리표를 달고 빌트인 심볼과 같은 표에 섞여 들어간다** — §1에서 말한 "모듈 = 커널 주소 공간에 지연 링크"의 증거다. 모듈 주소(`ffffc4c9b2ac8000`)는 커널 이미지(`ffffc4ca2...`) 바로 아래의 모듈 영역이다. `__ksymtab_crc_ccitt`는 이 모듈이 `EXPORT_SYMBOL`한 함수를 다른 모듈이 쓸 수 있게 해 주는 표 항목이다(E3).
- `taint`가 빈 값 = 커널을 "오염"시키지 않았다(in-tree, GPL, 서명 문제 없음). out-of-tree 모듈을 넣으면 여기에 `O`가 찍힌다(E3).
- 장치: `/dev/ttyAMA0`은 **문자 장치, major 204, minor 64**. `/proc/devices`에서 204 = `ttyAMA`. 이 번호는 `amba-pl011.c` 47행 `#define SERIAL_AMBA_MAJOR 204`에서 온다. IRQ는 Linux 번호 13, GIC 하드웨어 번호 33(SPI 1 + 32 — DT의 `interrupts = <0x00 0x01 0x04>`), 핸들러 이름 `uart-pl011`. 카운트가 0인 것은 이 부팅 동안 입력이 한 글자도 없었기 때문이다. 출력도 많았는데 TX 인터럽트가 0인 것은, QEMU의 PL011이 바이트를 즉시 내보내 TX FIFO가 차지 않으니 드라이버가 `start_tx`에서 FIFO에 직접 써 넣고 끝났기 때문으로 보인다(소스와 카운트로 한 추정이며 따로 검증하지 않았다).
- `/sys/class/tty/ttyAMA0/device`가 `9000000.pl011:0.0`이고 그 드라이버가 `serial-base` 버스의 `port`다. 최근(6.x) serial core는 AMBA 장치(`9000000.pl011`) 밑에 "포트" 장치를 하나 더 둔다. 실제 PL011 드라이버(`uart-pl011`)는 그 부모(`9000000.pl011`)에 붙어 있다.
- 마지막 블록이 이 노트의 하이라이트다: **`read()` 하나가 커널 안에서 지나간 함수 12개가 실제 스택으로 찍혔다**. §6에서 이 스택을 한 줄씩 소스와 맞춘다. `/proc/<pid>/syscall`의 첫 숫자 `63`은 arm64(generic) syscall 번호의 `read`다.

#### 예제 14 — C로 KASLR 슬라이드 계산하기

무엇을 확인하나: `/proc/kallsyms`를 C로 파싱해 원하는 심볼을 찾고, `System.map`의 링크 주소(`_text` = `ffff800080000000`)와 비교해 이번 부팅의 KASLR 슬라이드를 계산한다. 모듈 심볼(`[...]` 꼬리표) 수도 센다.

```c
/* e1_kaslr.c: find symbols in /proc/kallsyms and compute the KASLR slide
 * against the link-time address from System.map (passed as argv[1]). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <link_addr_of__text>\n", argv[0]); return 1; }
    unsigned long link = strtoul(argv[1], NULL, 16);
    const char *want[] = { "_text", "vfs_read", "tty_read", "pl011_int", NULL };
    FILE *f = fopen("/proc/kallsyms", "r");
    if (!f) { perror("/proc/kallsyms"); return 1; }
    char line[512], name[256], type;
    unsigned long addr, text = 0, total = 0, modsyms = 0;
    while (fgets(line, sizeof line, f)) {
        total++;
        if (strchr(line, '['))          /* "[modname]" suffix = module symbol */
            modsyms++;
        if (sscanf(line, "%lx %c %255s", &addr, &type, name) != 3)
            continue;
        for (int i = 0; want[i]; i++)
            if (strcmp(name, want[i]) == 0) {
                printf("%-10s %c %016lx\n", name, type, addr);
                if (i == 0) text = addr;
            }
    }
    fclose(f);
    printf("symbols=%lu module_symbols=%lu\n", total, modsyms);
    printf("KASLR slide = %#lx\n", text - link);
    return 0;
}
```

예제 15의 C 프로그램과 함께 VM에서 정적 링크로 빌드하고(게스트 BusyBox에는 glibc가 없다), 새 initramfs에 넣어 부팅했다.

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/E1 && cd /var/tmp/E1 && for p in e1_ttyread e1_kaslr; do gcc -std=gnu11 -Wall -Wextra -O2 -static /Users/donh/.elinux_scratch/E1/$p.c -o $p || exit 1; done; file e1_ttyread | cut -c1-90; grep -w _text /var/lab/build/linux/System.map'
cat > ~/.elinux_scratch/E1/e1_guest2.sh <<'EOF'
#!/bin/sh
echo "== e1_kaslr"; /e1_kaslr ffff800080000000
echo "== e1_ttyread"; /e1_ttyread /dev/ttyAMA0
EOF
cat > ~/.elinux_scratch/E1/e1_boot2.sh <<'EOF'
#!/bin/bash
set -e
W=/var/tmp/E1; cd $W
rm -rf e1_root2; mkdir e1_root2; cp -a /var/lab/build/rootfs/. e1_root2/
cp e1_ttyread e1_kaslr e1_root2/; cp /Users/donh/.elinux_scratch/E1/e1_guest2.sh e1_root2/; chmod +x e1_root2/e1_guest2.sh
(cd e1_root2 && find . | cpio -o -H newc --quiet | gzip) > e1_initrd2.gz
timeout 180 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd e1_initrd2.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/e1_guest2.sh" < /dev/null 2>&1 | tr -d '\r'
EOF
limactl shell elinux -- bash /Users/donh/.elinux_scratch/E1/e1_boot2.sh 2>&1 | tail -40
```

```text
e1_ttyread: ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux), statically link
ffff800080000000 T _text
```

```text
lab initramfs: 6.18.55 up 2.49s
== e1_kaslr
_text      T ffffbbc2a8000000
vfs_read   T ffffbbc2a8371dec
tty_read   t ffffbbc2a8973a70
pl011_int  t ffffbbc2a89bdca0
symbols=224016 module_symbols=0
KASLR slide = 0x3bc228000000
== e1_ttyread
...  (이 부분은 §6.1 예제 15에서 본다)
[   12.273868] reboot: Power down
```

출력에서 볼 것:

- `-Wall -Wextra`로 경고 0개. 이번 부팅의 슬라이드는 `0x3bc228000000`이다. 세 번의 부팅(예제 13 두 번, 이번 한 번)에서 `vfs_read`가 `ffffa930...`, `ffffc4ca...`, `ffffbbc2...`로 매번 달랐다. 하지만 `vfs_read - _text` = `0x371dec`는 `System.map`과 똑같다. **심볼 사이의 상대 거리는 변하지 않는다.**
- 모듈을 안 넣은 부팅이라 `module_symbols=0`이다(예제 13에서는 `[crc_ccitt]` 18줄이 있었다).
- root가 아니면 `/proc/kallsyms`의 주소가 전부 0으로 보인다(`kptr_restrict`). 게스트 셸은 root라 실제 주소가 보였다.

---

## 6. 가이드 워크 — `/dev/ttyAMA0`의 `read()`를 끝까지 따라가기

이제 지도와 도구를 들고 실제 경로 하나를 처음부터 끝까지 걷는다. 대상은 QEMU `virt`의 콘솔 UART(ARM PL011)다. Don의 센서 키트로 치면 **GNSS 수신기가 붙은 UART에서 NMEA 문장을 읽는 것**과 똑같은 경로다(장치 이름만 `ttyAMA0` 대신 `ttymxc1`(i.MX), `ttyS2`(TI) 등).

경로는 두 갈래다.

- **소비자(위→아래)**: 앱의 `read()`가 커널로 들어가 데이터가 없으면 잠든다.
- **생산자(아래→위)**: 바이트가 도착하면 인터럽트가 드라이버를 깨우고, 데이터가 TTY 계층을 거쳐 올라가 잠든 소비자를 깨운다.

### 6.1 예제 15 — 잠든 `read()`의 커널 스택을 C로 잡기

무엇을 확인하나: 장치를 열어 `fstat`으로 major/minor를 확인하고, 자식 프로세스가 `read()`에서 잠든 사이 부모가 자식의 `/proc/<pid>/syscall`과 `/proc/<pid>/stack`을 읽는다. 이 스택이 소비자 경로의 "실측 지도"다.

```c
/* e1_ttyread.c: follow read() on a tty into the kernel.
 * child blocks in read(); parent prints the child's kernel stack. */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

static void dump(const char *path)
{
    char line[256];
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f))
        fputs(line, stdout);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/ttyAMA0";
    struct stat st;
    int fd = open(dev, O_RDONLY | O_NOCTTY);
    if (fd < 0) { perror("open"); exit(1); }
    if (fstat(fd, &st) < 0) { perror("fstat"); exit(1); }
    printf("%s: char=%d major=%u minor=%u\n", dev, S_ISCHR(st.st_mode),
           major(st.st_rdev), minor(st.st_rdev));

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid == 0) {                 /* child: block in read() */
        char c;
        ssize_t n = read(fd, &c, 1);
        printf("child read returned %zd\n", n);
        _exit(0);
    }
    sleep(1);                       /* let the child reach the wait */
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/syscall", (int)pid);
    printf("-- %s (nr fd buf count ...)\n", path);
    dump(path);
    snprintf(path, sizeof path, "/proc/%d/stack", (int)pid);
    printf("-- %s\n", path);
    dump(path);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    close(fd);
    return 0;
}
```

빌드와 실행은 예제 14와 같은 명령(같은 부팅)으로 했다. 아래는 그 부팅 출력의 `== e1_ttyread` 부분이다.

```text
/dev/ttyAMA0: char=1 major=204 minor=64
-- /proc/65/syscall (nr fd buf count ...)
63 0x3 0xfffffe0c36c7 0x1 0x0 0x0 0x0 0xfffffe0c36b0 0x440148
-- /proc/65/stack
[<0>] wait_woken+0x84/0x94
[<0>] n_tty_read+0x434/0x578
[<0>] tty_read+0xe8/0x218
[<0>] vfs_read+0x244/0x33c
[<0>] ksys_read+0x64/0x100
[<0>] __arm64_sys_read+0x18/0x24
[<0>] invoke_syscall.constprop.0+0x40/0xe0
[<0>] el0_svc_common.constprop.0+0x38/0xe0
[<0>] do_el0_svc+0x1c/0x28
[<0>] el0_svc+0x38/0x118
[<0>] el0t_64_sync_handler+0xa0/0xe4
[<0>] el0t_64_sync+0x198/0x19c
```

출력에서 볼 것:

- `/proc/65/syscall`: syscall 번호 63(`read`), 인자 `fd=3`, `buf`, `count=1`. 자식이 정확히 우리가 부른 `read(fd, &c, 1)`에서 멈춰 있다.
- 스택은 **아래에서 위로** 읽는다: `el0t_64_sync`(예외 벡터) → … → `vfs_read` → `tty_read` → `n_tty_read` → `wait_woken`(잠). 그림 1의 층이 그대로 나온다: arch(`el0*`) → syscall 디스패치(`invoke_syscall`) → VFS(`vfs_read`) → TTY 프레임워크(`tty_read`) → line discipline(`n_tty_read`) → 스케줄러(`wait_woken`).
- **드라이버(`amba-pl011.c`)는 이 스택에 없다**. 읽는 쪽은 드라이버를 직접 부르지 않고 TTY 버퍼에서 꺼내 갈 뿐이다. 드라이버는 생산자 쪽(인터럽트)에서 등장한다. UART·TTY 구조의 핵심이다(H8).
- `.constprop.0`은 컴파일러가 상수 인자를 전파해 만든 함수 복제본이라는 뜻이다. 소스 이름은 `invoke_syscall`.

### 6.2 스택을 소스 줄로 — `faddr2line` 한 번에

```sh
limactl shell elinux -- bash -lc 'S=/var/lab/src/linux-6.18.55; cd /var/lab/build/linux; $S/scripts/faddr2line vmlinux n_tty_read+0x434/0x578 tty_read+0xe8/0x218 vfs_read+0x244/0x33c ksys_read+0x64/0x100 el0_svc+0x38/0x118 2>&1; grep -w vfs_read System.map'
```

```text
n_tty_read+0x434/0x578:
n_tty_wait_for_input at drivers/tty/n_tty.c:2169
(inlined by) n_tty_read at drivers/tty/n_tty.c:2264

tty_read+0xe8/0x218:
iterate_tty_read at drivers/tty/tty_io.c:847
(inlined by) tty_read at drivers/tty/tty_io.c:922

vfs_read+0x244/0x33c:
new_sync_read at fs/read_write.c:491
(inlined by) vfs_read at fs/read_write.c:572

ksys_read+0x64/0x100:
ksys_read at fs/read_write.c:715

el0_svc+0x38/0x118:
__daif_local_irq_disable at arch/arm64/include/asm/irqflags.h:55
(inlined by) arm64_exit_to_user_mode at arch/arm64/kernel/entry-common.c:104
(inlined by) el0_svc at arch/arm64/kernel/entry-common.c:750
```

출력에서 볼 것:

- 스택의 주소는 **복귀 주소**(호출 다음 명령)다. 그래서 `el0_svc+0x38`이 `do_el0_svc(regs)`를 부르는 749행이 아니라 그 다음 줄 750행(복귀 후 유저로 돌아가는 처리)으로 풀렸다. Oops 디코딩 때 "한 줄 아래가 나오는" 이유다.
- `vfs_read`는 `new_sync_read`(491행)를 거쳤다 = TTY는 옛 `.read`가 아니라 `.read_iter`를 구현한다. `tty_read`는 `iterate_tty_read`(847행)에서 line discipline의 `read`를 불렀다. 이것들이 인라인돼서 스택에는 안 보였던 함수들이다.

### 6.3 소비자 경로를 소스로 읽기

스택의 각 프레임을 랩 소스에서 열어 본다(모든 줄 번호는 `/var/lab/src/linux-6.18.55`에서 grep으로 확인했다).

**1) arch 층 — 예외 진입과 syscall 디스패치.** `svc #0`은 동기 예외(Synchronous exception from EL0)를 일으킨다. 펌웨어로 치면 SVC 핸들러다.

| 단계 | 위치 | 하는 일 |
|---|---|---|
| 벡터 | `arch/arm64/kernel/entry.S:530` `kernel_ventry 0, t, 64, sync` | VBAR_EL1 벡터 표의 "EL0 64비트 동기 예외" 칸, 레지스터를 `pt_regs`로 저장 |
| C 분류 | `arch/arm64/kernel/entry-common.c:762` `el0t_64_sync_handler` | ESR_EL1의 예외 클래스를 보고 SVC면 `el0_svc`(743행) |
| 디스패치 | `arch/arm64/kernel/syscall.c:149` `do_el0_svc` → 151행 `el0_svc_common(regs, regs->regs[8], __NR_syscalls, sys_call_table)` | **x8 레지스터 = syscall 번호** |
| 호출 | `syscall.c:38` `invoke_syscall` → 48행 `syscall_fn = syscall_table[array_index_nospec(scno, sc_nr)]` | 표에서 함수 포인터를 꺼내 호출 (Spectre 대비 인덱스 마스킹) |
| 표 | `arch/arm64/kernel/sys.c:60` `sys_call_table[__NR_syscalls]`, 번호는 `scripts/syscall.tbl:82` `63 common read sys_read` | 번호 63 → `__arm64_sys_read` |

**2) VFS — `fs/read_write.c`.**

```c
/* fs/read_write.c:722 (linux-6.18.55) */
SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
{
	return ksys_read(fd, buf, count);
}
```

`ksys_read`(704행)는 fd 번호로 `struct file`을 찾고(`CLASS(fd_pos, f)(fd)` — fd 테이블 조회, A3) 파일 위치를 챙겨 `vfs_read`(552행)를 부른다. `vfs_read`의 핵심은 이 부분이다.

```c
/* fs/read_write.c:552-573 (발췌) */
ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	ssize_t ret;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;
	if (unlikely(!access_ok(buf, count)))
		return -EFAULT;

	ret = rw_verify_area(READ, file, pos, count);
	if (ret)
		return ret;
	if (count > MAX_RW_COUNT)
		count =  MAX_RW_COUNT;

	if (file->f_op->read)
		ret = file->f_op->read(file, buf, count, pos);
	else if (file->f_op->read_iter)
		ret = new_sync_read(file, buf, count, pos);
	else
		ret = -EINVAL;
```

VFS가 하는 일은 **검사와 위임**뿐이다: 읽기 모드로 열렸나, 유저 버퍼 주소가 유저 영역인가(`access_ok`), 범위가 맞나. 그 다음 `file->f_op`(이 파일을 연 드라이버가 준 `file_operations`)로 넘긴다. VFS는 상대가 TTY인지 모른다. 드라이버를 쓸 때 **`.read`를 구현하면 바로 여기서 불린다**(G4).

`f_op`이 TTY 것이 된 시점은 `open()` 때다: `fs/char_dev.c:373` `chrdev_open`이 major 204로 등록된 cdev를 찾아 412행 `replace_fops(filp, fops)`로 `file->f_op`을 TTY의 `tty_fops`로 바꾸고, 414행에서 그 `open`(`tty_open`)을 부른다.

**3) TTY 프레임워크 — `drivers/tty/tty_io.c`.** 459–460행에서 `tty_fops`의 `.read_iter = tty_read`. `tty_read`(901행)는 이 tty에 걸린 line discipline을 잡고(`tty_ldisc_ref_wait`) `iterate_tty_read`를 부르며, 그 안 847행이 `size = ld->ops->read(tty, file, kernel_buf, size, &cookie, offset);`다. TTY 코어는 "줄 단위 편집·에코·Ctrl-C"를 모른다. 그건 line discipline 몫이다.

**4) line discipline — `drivers/tty/n_tty.c`.** 기본 ldisc `n_tty`의 ops 표(2512행~)에 `.read = n_tty_read`(2520행), `.receive_buf2 = n_tty_receive_buf2`가 있다. `n_tty_read`(2195행)는 읽을 데이터가 없으면 `n_tty_wait_for_input` → 2169행 `wait_woken(wait, TASK_INTERRUPTIBLE, *timeout)`에서 **잠든다**. 이것이 스택 맨 위의 `wait_woken`이다. 펌웨어로 치면 "RX 세마포어를 pend하고 태스크가 블록된 상태"다(F3).

### 6.4 생산자 경로 — 바이트가 도착하면

이 부분은 런타임에 관찰하지 않고 **소스를 읽어 추적**했다(랩 커널은 `# CONFIG_FTRACE is not set`이라 함수 경로를 실행 중에 찍지 못했다. 위치는 모두 grep으로 확인했다).

| 순서 | 위치 | 하는 일 |
|---|---|---|
| 1 | HW: PL011 RX FIFO에 바이트 → GIC SPI 1 (hwirq 33) | `/proc/interrupts`의 `13: GIC-0 33 Level uart-pl011` |
| 2 | `arch/arm64/kernel/entry-common.c:154` `do_interrupt_handler` | IRQ 예외 → `handle_arch_irq` |
| 3 | `drivers/irqchip/irq-gic.c:334` `gic_handle_irq` → 370행 `generic_handle_domain_irq` | GIC에서 hwirq를 읽어 Linux IRQ로 변환, 등록된 handler 호출 (H6) |
| 4 | `drivers/tty/serial/amba-pl011.c:1602` `pl011_int` | `REG_RIS`(raw interrupt status)를 읽고, RX면 `pl011_rx_chars` (handler 등록은 1844행 `request_irq(..., pl011_int, IRQF_SHARED, "uart-pl011", uap)`) |
| 5 | `amba-pl011.c:1468` `pl011_rx_chars` → 321행 `pl011_fifo_to_tty` | `REG_FR`의 RXFE가 설 때까지 `REG_DR`을 최대 256번 읽음, 에러 비트(BE/PE/FE/OE) 처리 |
| 6 | `drivers/tty/serial/serial_core.c:3479` `uart_insert_char` → `tty_insert_flip_char` | 바이트를 tty_port의 flip buffer에 넣음 |
| 7 | `drivers/tty/tty_buffer.c:528` `tty_flip_buffer_push` → 533행 `queue_work(system_unbound_wq, ...)` | 인터럽트 컨텍스트에서는 여기까지, 나머지는 워크큐로 미룸 (F7) |
| 8 | `tty_buffer.c:462` `flush_to_ldisc` (kworker 스레드) → 445행 `port->client_ops->receive_buf` | 프로세스 컨텍스트에서 ldisc로 넘김 |
| 9 | `drivers/tty/tty_port.c:23` `tty_port_default_receive_buf` → `tty_buffer.c:383` `tty_ldisc_receive_buf` → 387행 `ld->ops->receive_buf2` | ldisc ops 호출 |
| 10 | `drivers/tty/n_tty.c:1766` `n_tty_receive_buf2` → `n_tty_receive_buf_common` | 줄 편집·에코·특수 문자 처리 |
| 11 | `n_tty.c:1242` `n_tty_receive_handle_newline` → 1250행 `wake_up_interruptible_poll(&tty->read_wait, ...)` | canonical 모드에서 줄바꿈이 오면 **잠든 reader를 깨움** |

PL011 인터럽트 핸들러의 몸통은 Don이 펌웨어에서 짜던 UART ISR과 거의 같다.

```c
/* drivers/tty/serial/amba-pl011.c:1602-1625 (발췌) */
static irqreturn_t pl011_int(int irq, void *dev_id)
{
	struct uart_amba_port *uap = dev_id;
	unsigned int status, pass_counter = AMBA_ISR_PASS_LIMIT;
	int handled = 0;

	uart_port_lock(&uap->port);
	status = pl011_read(uap, REG_RIS) & uap->im;
	if (status) {
		do {
			check_apply_cts_event_workaround(uap);

			pl011_write(status & ~(UART011_TXIS | UART011_RTIS | UART011_RXIS),
				    uap, REG_ICR);

			if (status & (UART011_RTIS | UART011_RXIS)) {
				if (pl011_dma_rx_running(uap))
					pl011_dma_rx_irq(uap);
				else
					pl011_rx_chars(uap);
			}
```

상태 레지스터 읽기 → 클리어 → RX/TX 분기 → 최대 `AMBA_ISR_PASS_LIMIT`(256, 51행)번 반복. **다른 점은 핸들러가 데이터를 앱에 직접 주지 않고 공용 버퍼(flip buffer)에 넣고 워크큐에 넘긴다는 것**, 그리고 레지스터 접근이 `pl011_read()`(내부적으로 `readl` 계열, H1)로 감싸져 있다는 것뿐이다. 예제 8에서 `pl011_int+0x60`이 1619행(`pl011_dma_rx_irq` 호출)으로 풀린 것도 이 블록이다.

```svg
<svg viewBox="0 0 680 550" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="180" y="24" text-anchor="middle" font-size="13" font-weight="bold">① read() — 프로세스 컨텍스트</text>
  <text x="510" y="24" text-anchor="middle" font-size="13" font-weight="bold">② RX 인터럽트 — IRQ → 워크큐</text>
  <rect x="20" y="40" width="320" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="55" font-size="12">유저: dd 또는 e1_ttyread</text>
  <text x="30" y="70" font-size="12">read(fd, buf, 1) → svc #0</text>
  <rect x="20" y="86" width="320" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="30" y="101" font-size="12">arch/arm64/kernel/entry.S:530</text>
  <text x="30" y="116" font-size="12">벡터 → el0t_64_sync</text>
  <rect x="20" y="132" width="320" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="30" y="147" font-size="12">arch/arm64/kernel/entry-common.c:762</text>
  <text x="30" y="162" font-size="12">el0t_64_sync_handler → el0_svc (:743)</text>
  <rect x="20" y="178" width="320" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="30" y="193" font-size="12">arch/arm64/kernel/syscall.c:149, :38</text>
  <text x="30" y="208" font-size="12">do_el0_svc → invoke_syscall (x8 = 63)</text>
  <rect x="20" y="224" width="320" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="239" font-size="12">fs/read_write.c:722 → :704</text>
  <text x="30" y="254" font-size="12">SYSCALL_DEFINE3(read) → ksys_read</text>
  <rect x="20" y="270" width="320" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="285" font-size="12">fs/read_write.c:552</text>
  <text x="30" y="300" font-size="12">vfs_read — 모드·access_ok·범위 검사</text>
  <rect x="20" y="316" width="320" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="331" font-size="12">fs/read_write.c:572 → :481</text>
  <text x="30" y="346" font-size="12">new_sync_read → f_op->read_iter</text>
  <rect x="20" y="362" width="320" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="377" font-size="12">drivers/tty/tty_io.c:901 (tty_fops :460)</text>
  <text x="30" y="392" font-size="12">tty_read → iterate_tty_read</text>
  <rect x="20" y="408" width="320" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="423" font-size="12">drivers/tty/tty_io.c:847</text>
  <text x="30" y="438" font-size="12">ld->ops->read → n_tty_read</text>
  <rect x="20" y="454" width="320" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="469" font-size="12">drivers/tty/n_tty.c:2195 → :2169</text>
  <text x="30" y="484" font-size="12">n_tty_read → wait_woken()에서 잠듦</text>
  <rect x="360" y="40" width="300" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="370" y="55" font-size="12">HW: PL011 RX FIFO에 바이트 도착</text>
  <text x="370" y="70" font-size="12">GIC hwirq 33 → Linux IRQ 13</text>
  <rect x="360" y="86" width="300" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="370" y="101" font-size="12">arch/arm64/kernel/entry-common.c:154</text>
  <text x="370" y="116" font-size="12">IRQ 예외 → do_interrupt_handler</text>
  <rect x="360" y="132" width="300" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="370" y="147" font-size="12">drivers/irqchip/irq-gic.c:334</text>
  <text x="370" y="162" font-size="12">gic_handle_irq → 도메인 핸들러</text>
  <rect x="360" y="178" width="300" height="36" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="370" y="193" font-size="12">drivers/tty/serial/amba-pl011.c:1602</text>
  <text x="370" y="208" font-size="12">pl011_int: RIS 읽고 RX 처리</text>
  <rect x="360" y="224" width="300" height="36" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="370" y="239" font-size="12">amba-pl011.c:1468 → :321</text>
  <text x="370" y="254" font-size="12">pl011_rx_chars → pl011_fifo_to_tty</text>
  <rect x="360" y="270" width="300" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="370" y="285" font-size="12">drivers/tty/serial/serial_core.c:3479</text>
  <text x="370" y="300" font-size="12">uart_insert_char → tty_insert_flip_char</text>
  <rect x="360" y="316" width="300" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="370" y="331" font-size="12">drivers/tty/tty_buffer.c:528</text>
  <text x="370" y="346" font-size="12">tty_flip_buffer_push → queue_work</text>
  <rect x="360" y="362" width="300" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="370" y="377" font-size="12">drivers/tty/tty_buffer.c:462</text>
  <text x="370" y="392" font-size="12">flush_to_ldisc (kworker에서 실행)</text>
  <rect x="360" y="408" width="300" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="370" y="423" font-size="12">tty_port.c:23 → tty_buffer.c:383</text>
  <text x="370" y="438" font-size="12">tty_ldisc_receive_buf → receive_buf2</text>
  <rect x="360" y="454" width="300" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="370" y="469" font-size="12">drivers/tty/n_tty.c:1766 → :1242</text>
  <text x="370" y="484" font-size="12">n_tty_receive_buf2 … wake_up (:1250)</text>
  <line x1="180" y1="76" x2="180" y2="85" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="180" y1="122" x2="180" y2="131" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="180" y1="168" x2="180" y2="177" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="180" y1="214" x2="180" y2="223" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="180" y1="260" x2="180" y2="269" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="180" y1="306" x2="180" y2="315" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="180" y1="352" x2="180" y2="361" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="180" y1="398" x2="180" y2="407" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="180" y1="444" x2="180" y2="453" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="76" x2="510" y2="85" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="122" x2="510" y2="131" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="168" x2="510" y2="177" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="214" x2="510" y2="223" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="260" x2="510" y2="269" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="306" x2="510" y2="315" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="352" x2="510" y2="361" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="398" x2="510" y2="407" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="444" x2="510" y2="453" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="360" y1="472" x2="342" y2="472" stroke="currentColor" stroke-dasharray="3,2" marker-end="url(#ah4)"/>
  <rect x="20" y="506" width="640" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="528" text-anchor="middle" font-size="12">깨어난 n_tty_read가 read_buf에서 유저 버퍼로 복사 → 같은 길을 거꾸로 되돌아 EL0 복귀</text>
</svg>
```
그림 11 — `/dev/ttyAMA0` 한 바이트의 두 갈래 경로와 각 단계의 file:line(linux-6.18.55). 왼쪽은 예제 15의 실측 스택과 `faddr2line`으로 확인했고, 오른쪽은 소스를 읽어 추적했다. 두 경로는 n_tty의 `read_wait` 대기열에서 만난다(점선). 회색 = arch/irqchip, 초록 = VFS, 파랑 = TTY 프레임워크, 주황 = PL011 드라이버, 빨강 = 잠드는 지점.

### 6.5 이 워크에서 얻은 길찾기 요령

- **위에서 내려갈 때는 ops 표를 따라간다**: `file->f_op` → `tty_fops`(초기화 grep), `ld->ops` → `n_tty_ops`. 함수 포인터 호출을 만나면 `'\.read_iter\s*=\s*'` 같은 초기화 패턴으로 구현을 찾는다.
- **아래에서 올라갈 때는 `request_irq`부터**: 드라이버의 인터럽트 핸들러 이름은 `request_irq(..., handler, ...)` 한 줄에서 나온다. `/proc/interrupts`의 이름(`uart-pl011`)을 grep하면 그 줄이 바로 잡힌다.
- **실행 중 커널이 있으면 스택부터 본다**: `/proc/<pid>/stack`, `echo t > /proc/sysrq-trigger`(모든 태스크 스택 덤프, `CONFIG_MAGIC_SYSRQ=y`), 나중에 배울 ftrace(M5)가 "어떤 파일을 거치나"를 소스 읽기보다 빨리 알려 준다.

---

## 7. 커널 안에서는 무슨 일이 일어나나 — ops 표로 이어진 서브시스템

§6의 경로를 구조체 관점에서 다시 그리면, 서브시스템 경계마다 **함수 포인터 표**가 하나씩 있다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="190" height="66" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="115" y="50" text-anchor="middle" font-size="12" font-weight="bold">struct file</text>
  <text x="115" y="68" text-anchor="middle" font-size="12">f_op = &amp;tty_fops</text>
  <text x="115" y="86" text-anchor="middle" font-size="12">VFS (fs/)</text>
  <rect x="245" y="30" width="190" height="66" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="50" text-anchor="middle" font-size="12" font-weight="bold">struct tty_struct</text>
  <text x="340" y="68" text-anchor="middle" font-size="12">ldisc → ops, port</text>
  <text x="340" y="86" text-anchor="middle" font-size="12">TTY 코어 (tty_io.c)</text>
  <rect x="470" y="30" width="190" height="66" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="565" y="50" text-anchor="middle" font-size="12" font-weight="bold">struct tty_ldisc_ops</text>
  <text x="565" y="68" text-anchor="middle" font-size="12">.read = n_tty_read</text>
  <text x="565" y="86" text-anchor="middle" font-size="12">n_tty.c:2512</text>
  <rect x="20" y="150" width="190" height="66" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="170" text-anchor="middle" font-size="12" font-weight="bold">struct tty_port</text>
  <text x="115" y="188" text-anchor="middle" font-size="12">buf (flip buffer)</text>
  <text x="115" y="206" text-anchor="middle" font-size="12">tty_buffer.c</text>
  <rect x="245" y="150" width="190" height="66" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="170" text-anchor="middle" font-size="12" font-weight="bold">struct uart_port</text>
  <text x="340" y="188" text-anchor="middle" font-size="12">ops = &amp;amba_pl011_pops</text>
  <text x="340" y="206" text-anchor="middle" font-size="12">amba-pl011.c:2324</text>
  <rect x="470" y="150" width="190" height="66" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="565" y="170" text-anchor="middle" font-size="12" font-weight="bold">PL011 레지스터</text>
  <text x="565" y="188" text-anchor="middle" font-size="12">DR · FR · RIS · ICR</text>
  <text x="565" y="206" text-anchor="middle" font-size="12">MMIO 0x09000000</text>
  <line x1="210" y1="63" x2="243" y2="63" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="435" y1="63" x2="468" y2="63" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="340" y1="96" x2="115" y2="148" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="243" y1="183" x2="212" y2="183" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="435" y1="183" x2="468" y2="183" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="340" y="244" text-anchor="middle" font-size="12">각 화살표 = 포인터 하나. 위 층은 아래 층의 구현을 모르고 표만 부른다</text>
</svg>
```
그림 12 — `/dev/ttyAMA0` 경로를 잇는 구조체와 ops 표. VFS는 `tty_fops`만, TTY 코어는 `n_tty_ops`만, serial core는 `amba_pl011_pops`(`uap->port.ops = &amba_pl011_pops`, 2924행)만 안다. 드라이버(주황)는 아래로는 레지스터, 위로는 `tty_port`의 flip buffer에 데이터를 밀어 넣는다(`uart_insert_char`).

이 그림에서 드라이버 작성자에게 중요한 점:

- **드라이버는 "자기 칸"만 채운다**. PL011 드라이버가 구현하는 것은 `struct uart_ops amba_pl011_pops`(startup, shutdown, set_termios, start_tx …)와 인터럽트 핸들러뿐이다. `/dev` 노드 생성, `read()` 처리, 줄 편집, 에코, `termios`, `poll`은 전부 TTY·serial core가 해 준다. 이것이 I1 "직접 char device를 만들지 말고 서브시스템에 등록하라"의 실례다.
- 드라이버는 `uart_register_driver()`(`serial_core.c:2758`)로 자기를 등록하고, 그 안에서 `tty_register_driver()`(2805행)가 major 204의 문자 장치 영역을 잡는다. 장치 발견은 AMBA 버스가 ID 레지스터(`0x00041011`)로 매칭해 `pl011_probe()`(2899행)를 부른다(D3의 OF 매칭과 같은 driver core 경로, 버스만 다르다).
- 서브시스템이 드라이버에게 빌려주는 함수(`uart_insert_char`, `tty_flip_buffer_push` 등)는 `EXPORT_SYMBOL_GPL`로 export돼 있어서 PL011을 모듈로 빌드해도 링크된다. 랩 빌드의 export 심볼은 20,778개다(아래 측정).

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/build/linux; echo "Module.symvers: $(wc -l < Module.symvers)"; awk "{print \$4}" Module.symvers | sort | uniq -c; awk "\$3==\"vmlinux\"" Module.symvers | wc -l'
```

```text
Module.symvers: 20778
   8329 EXPORT_SYMBOL
  12449 EXPORT_SYMBOL_GPL
  13815
```

GPL 전용 export가 60%다. 클로즈드 소스 모듈은 이 60%를 쓸 수 없다 — 라이선스 판단(E7)이 기술 설계에 직접 영향을 주는 지점이다.

다시 만나는 곳:

- **F12 VFS 내부**: `struct file`·`inode`·`file_operations`와 `chrdev_open`의 `replace_fops`.
- **G4 문자 디바이스**: 직접 `cdev`를 만들고 `.read`를 구현하면 §6.3의 `vfs_read` 570행에서 불린다.
- **F3 잠자기와 깨우기**: `wait_woken`/`wake_up_interruptible_poll` 쌍 — 드라이버에서는 `wait_event_interruptible`/`wake_up_interruptible`로 쓴다.
- **F6·F7 인터럽트와 지연 작업**: `pl011_int` → `queue_work` → `flush_to_ldisc`의 top half / bottom half 분리.
- **H6 GIC, H8 UART·TTY**: 오른쪽 갈래의 각 상자.
- **E6 유저↔커널 경계**: `access_ok`, `char __user *buf`, 그리고 n_tty가 데이터를 유저 버퍼로 복사하는 부분.

---

## 8. Embedded 실무에서 다시 보기

### 8.1 장면 1 — 벤더 BSP 커널을 처음 받은 날

i.MX8M Plus 보드의 벤더 커널(예: NXP `linux-imx`)을 받았다. 첫 30분에 할 일은 이 노트의 순서 그대로다.

```sh
# 0) 무슨 버전인가 (mainline 어디서 갈라졌나)
head -6 Makefile
# 1) 내 보드 DTS는 어디 있나
ls arch/arm64/boot/dts/freescale/ | grep imx8mp
# 2) 이 보드 defconfig는 무엇인가
ls arch/arm64/configs/
# 3) 센서 키트의 각 장치 드라이버는 어디 있나 (DT compatible로 역추적)
grep -rn 'compatible' arch/arm64/boot/dts/freescale/imx8mp-evk.dts | head
git grep -n '"bosch,bmi088' -- drivers      # IMU
git grep -n '"fsl,imx8mp-uart\|fsl,imx6q-uart' -- drivers/tty   # GNSS UART
git grep -n '"fsl,imx8mp-flexcan' -- drivers/net/can              # CAN
# 4) 벤더가 mainline 대비 무엇을 고쳤나 (git 저장소일 때)
git log --oneline v6.6..HEAD -- drivers/media/platform/nxp | head
```

(위는 순서를 보여 주는 명령 모음이다. 파일 이름·compatible·태그는 예시이고 이 랩에서 실행하지 않았다.)

벤더 커널에서 가장 시간이 많이 드는 곳은 `drivers/`의 벤더 전용 디렉터리(예: `drivers/mxc/`)와 `arch/arm64/boot/dts/<vendor>/`다. mainline에 없는 디렉터리가 보이면 "이건 업그레이드 때 따로 포팅해야 하는 짐"이라는 표시다(E7, P 모듈).

### 8.2 장면 2 — 부팅 로그에 GNSS UART가 안 보인다

`/dev/ttymxc1`이 없다. 그림 3·4의 지도로 원인 후보를 층별로 나눈다.

| 층 | 확인 | 이 노트의 도구 |
|---|---|---|
| DT | 노드 `status = "okay"`인가, pinctrl이 맞나 | D 모듈, `/proc/device-tree` |
| 설정 | 드라이버가 `=y`나 `=m`인가 | `zcat /proc/config.gz`, 예제 9 |
| 드라이버 존재 | 심볼이 실행 중 커널에 있나 | `grep imx_uart /proc/kallsyms` (예제 13) |
| 매칭·probe | sysfs `driver` 링크, dmesg | D3 |
| 장치 노드 | `/proc/devices`에 major가 있나 | 예제 13 |

### 8.3 장면 3 — 커널 이미지 크기 다이어트

부트 파티션이 16 MB인 제품에서 defconfig 커널(Image 42.7 MB)은 들어가지 않는다. 예제 11의 숫자가 우선순위를 알려 준다: 빌트인 코드의 66%가 `drivers/`, 그중 1·2위가 **다른 SoC의 pinctrl·clk**다. 자기 SoC 것만 남기고, 디버그 정보·`KALLSYMS_ALL`(예제 12의 6.4 MB 표)·안 쓰는 파일시스템·네트워크 프로토콜을 끄는 순서로 줄인다. 줄인 뒤에는 예제 11 스크립트를 다시 돌려 전후를 비교한다. 설정 방법은 E2, 부팅 시간과의 관계는 C7·M10에서 다룬다.

### 8.4 장면 4 — 현장에서 온 Oops 한 장

필드 장비에서 `Unable to handle kernel NULL pointer dereference ... pc : xxx_imu_irq+0x5c/0x1a0 [imu_drv]` 로그가 왔다. 할 일은 정해져 있다.

```sh
# 같은 빌드의 모듈(.ko, 디버그 정보 포함)로 오프셋을 줄 번호로
./scripts/faddr2line drivers/iio/imu/imu_drv.ko xxx_imu_irq+0x5c/0x1a0
# 로그 전체를 한 번에
./scripts/decode_stacktrace.sh vmlinux /path/to/modules < oops.txt
```

(명령 형태만 보인 것으로, 이 랩에서 실행하지 않았다. 예제 8의 `faddr2line`과 같은 원리이고 모듈이면 `vmlinux` 대신 `.ko`를 준다.) `/크기`가 안 맞으면 빌드가 다른 것이다 — 릴리스마다 vmlinux와 `.ko`의 디버그 버전을 보관하는 체계가 없으면 이 작업이 불가능하다.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| I2C IMU 드라이버를 `drivers/i2c/`에서 찾음 | 아무리 찾아도 없음 | `drivers/i2c/`는 컨트롤러(adapter), 장치 드라이버는 기능별(`iio/imu/`) | compatible 문자열로 `drivers/` 전체 grep |
| 텍스트 검색 결과를 "이 커널에 있음"으로 판단 | 고친 코드가 아무 효과 없음 | 그 파일이 이 설정에서 컴파일 안 됨 (컴파일되는 `.c`는 31%) | `.config`, 빌드 트리의 `.o`, `/proc/kallsyms` 확인 |
| x86 PC에서 `make tags`를 `ARCH` 없이 생성 | arm64 함수로 점프가 안 됨 | tags.sh는 현재 `SRCARCH`만 인덱싱 | `make ARCH=arm64 tags` |
| 다른 빌드의 vmlinux로 Oops 디코딩 | 엉뚱한 줄 번호 | 같은 심볼이라도 오프셋 위치가 다름 | `/크기` 포함해서 `faddr2line`, 빌드 ID 확인 |
| 절대 주소로 `System.map` 대조 | 주소가 전혀 안 맞음 | KASLR이 매 부팅 이미지를 옮김 | `심볼+오프셋`으로 비교, 슬라이드 계산(예제 14) |
| `/sys/module/X`가 있으니 모듈이라 생각 | `rmmod X` 실패 | 빌트인도 파라미터가 있으면 `/sys/module`에 나타남 | `initstate` 파일 유무, `/proc/modules` 확인 |
| `include/linux/` 헤더를 유저 프로그램에서 include | 컴파일 에러 또는 커널 버전마다 깨짐 | 커널 내부 헤더는 ABI가 아님 | `include/uapi/`(설치된 `/usr/include/linux`)만 사용 |
| 패치를 메인테이너 한 명에게만 보냄 | 무응답 | 리스트(`L:`)와 상위 서브시스템 담당이 빠짐 | `get_maintainer.pl` 결과 전체를 To/Cc |
| 모듈로 넣으면 커널이 안전하다고 생각 | 모듈 버그로 커널 패닉 | 모듈은 커널과 같은 권한·주소 공간 | 격리가 필요하면 유저 공간 드라이버(UIO/VFIO) |
| 함수 포인터 호출을 grep으로 추적 실패 | "이 함수 아무도 안 부르네?" | `f_op->read_iter(...)` 같은 간접 호출 | ops 구조체 초기화(`.read_iter = `)를 grep |

---

## 10. 면접에서 이렇게 말한다

**Q.** Linux is described as a monolithic kernel. What does that mean, and how does it compare to a microkernel?

**A.** 스케줄러·메모리 관리·파일시스템·네트워크·드라이버가 전부 한 커널 주소 공간에서 같은 권한(EL1)으로 돌고 서로를 직접 함수 호출한다는 뜻이다. 빠르고 드라이버 생태계가 거대하지만, 드라이버 버그 하나가 커널 전체를 죽일 수 있다. 마이크로커널(QNX, seL4)은 IPC·스케줄링·주소 공간만 커널에 두고 드라이버·파일시스템을 유저 프로세스로 돌린다. 장애 격리와 검증은 쉽지만 메시지 전달 비용이 있다. Linux도 소스 설계는 ops 표와 프레임워크로 잘 나뉘어 있어서, "바이너리는 monolithic, 설계는 모듈식"이라고 말한다.

> Monolithic means the scheduler, memory management, filesystems, networking and device drivers all run in one kernel address space at the same privilege level and call each other directly. That gives performance and a huge driver ecosystem, but one buggy driver can take the whole kernel down. A microkernel like QNX or seL4 keeps only IPC, scheduling and address spaces in the kernel and runs drivers and filesystems as user processes — better fault isolation and easier verification, at the cost of message-passing overhead. Linux is monolithic as a binary, but the source is very modular: subsystems talk through ops tables and frameworks.

**꼬리 질문**: "그럼 loadable module은 microkernel 쪽 아닌가요?" → 아니다. `.ko`는 커널 주소 공간에 링크되고 빌트인과 같은 권한으로 돈다. 런타임 확장성일 뿐 격리가 아니다. `/proc/kallsyms`에 모듈 심볼이 `[모듈명]` 꼬리표로 커널 심볼과 같은 표에 들어가는 것을 직접 확인했다.

**Q.** Coming from RTOS firmware, what is the biggest structural difference you see in Linux?

**A.** RTOS에서는 앱·커널·드라이버가 한 이미지로 링크되고 보통 특권 분리가 없다. Linux는 유저↔커널 경계를 MMU와 예외 레벨로 강하게 나누고, 드라이버는 내가 부르는 함수가 아니라 **커널이 정해진 시점에 불러 주는 콜백**(probe, read_iter, IRQ handler)이 된다. 그래서 Linux 드라이버를 쓴다는 것은 "어느 프레임워크의 어느 ops 칸을 채우느냐"를 고르는 일이다.

> In an RTOS, the application, kernel and drivers are linked into one image and there's usually no privilege separation. In Linux the user/kernel boundary is enforced by the MMU and exception levels, and a driver stops being code I call — it becomes a set of callbacks the kernel invokes at defined points: probe, read_iter, the interrupt handler. So writing a Linux driver is mostly choosing the right framework and filling in its ops structure.

**Q.** Walk me through what happens when a user process calls `read()` on a serial port.

**A.** arm64에서 libc가 x8에 63을 넣고 `svc`를 실행하면, `entry.S`의 벡터에서 `el0t_64_sync_handler` → `el0_svc` → `invoke_syscall`이 syscall 표로 `sys_read`를 부른다. `ksys_read`가 fd로 `struct file`을 찾고 `vfs_read`가 권한·유저 버퍼를 검사한 뒤 `file->f_op->read_iter`, 즉 TTY 코어의 `tty_read`를 부른다. `tty_read`는 line discipline `n_tty_read`를 부르고, 데이터가 없으면 `read_wait` 대기열에서 잠든다. 반대편에서는 PL011 RX 인터럽트가 GIC를 거쳐 `pl011_int`로 들어오고, FIFO를 비워 flip buffer에 넣은 뒤 워크큐로 `flush_to_ldisc`를 돌려 n_tty에 넘기고, 줄바꿈이 오면 reader를 깨운다. QEMU에서 잠든 reader의 `/proc/pid/stack`으로 이 경로를 실제로 확인했다.

> On arm64, libc puts 63 in x8 and executes svc. The exception vector in entry.S goes to el0t_64_sync_handler, el0_svc, and invoke_syscall, which indexes the syscall table to sys_read. ksys_read turns the fd into a struct file, vfs_read checks the mode and the user buffer and calls file->f_op->read_iter, which for a tty is tty_read. That calls the line discipline, n_tty_read, which sleeps on the tty's read wait queue if there's no data. On the other side, the PL011 RX interrupt comes through the GIC into pl011_int, which drains the FIFO into the tty flip buffer and queues work; flush_to_ldisc hands the bytes to n_tty, and on a newline it wakes the reader. I verified the read side by dumping /proc/pid/stack of a blocked reader in QEMU.

**꼬리 질문**: "드라이버는 그 스택 어디에 있나요?" → 읽는 쪽 스택에는 없다. UART 드라이버는 인터럽트 쪽에서 버퍼를 채울 뿐이고 `read()`는 TTY 계층이 처리한다.

**Q.** You've just been handed a vendor kernel tree. Where would you look for the driver of a specific sensor on the board?

**A.** 보드 DTS(`arch/arm64/boot/dts/<vendor>/`)에서 그 센서 노드의 compatible을 읽고, 그 문자열을 `drivers/`에서 `git grep`한다. 드라이버는 버스가 아니라 기능별로 놓이므로 I2C IMU라도 `drivers/iio/imu/`에 있다. 그 다음 Makefile에서 그 파일을 켜는 `CONFIG_` 이름을 찾아 `.config`에서 `=y`/`=m`인지 확인하고, `get_maintainer.pl`로 담당자와 메일링 리스트를 확인해서 이미 알려진 버그 수정이 있는지 본다.

> I'd start from the board DTS under arch/arm64/boot/dts, take the sensor node's compatible string, and git grep it under drivers. Drivers live by function, not by bus, so an I2C IMU is in drivers/iio/imu, not drivers/i2c. Then I'd find the CONFIG symbol in the Makefile that builds that file and check whether it's =y or =m in the config, and run get_maintainer.pl to find the maintainers and mailing list, so I can check lore for fixes that haven't reached the vendor tree yet.

**Q.** How do you turn an address from a kernel oops into a source line?

**A.** Oops에는 `function+offset/size`가 찍힌다. KASLR 때문에 절대 주소는 매번 바뀌므로 심볼 기준 오프셋을 쓴다. 같은 빌드의 디버그 정보가 있는 vmlinux(모듈이면 그 `.ko`)에 `scripts/faddr2line`을 돌리면 inline 체인까지 file:line이 나온다. `/size`가 다르면 빌드가 다르다는 뜻이라 스크립트가 거절해 준다. 로그 전체는 `decode_stacktrace.sh`로 한 번에 푼다. 스택 주소는 복귀 주소라 호출 다음 줄이 나올 수 있다는 점도 기억해 둔다.

> The oops prints function plus offset over size. Because of KASLR the absolute address changes every boot, so I work from the symbol offset. I run scripts/faddr2line against the vmlinux — or the .ko for a module — from the exact same build with debug info, and it gives file and line including the inline chain. If the size doesn't match, the build is different and faddr2line refuses, which is a useful safety check. For a whole log I use decode_stacktrace.sh. And since stack entries are return addresses, the line can be the one right after the call.

**Q.** What's the difference between `include/linux` and `include/uapi`?

**A.** `include/uapi`는 유저 공간과 공유하는 ABI 헤더라서 한번 내보낸 구조체·상수는 절대 깨면 안 되고, `make headers_install`로 `/usr/include/linux`가 된다. `include/linux`는 커널 내부 전용이고 버전마다 자유롭게 바뀐다. 커널 헤더는 보통 uapi 헤더를 include하고 커널 전용 부분을 덧붙인다(예: `linux/fs.h`가 `uapi/linux/fs.h`를 포함). 드라이버의 ioctl 구조체는 uapi 쪽에 둔다.

> include/uapi holds the user-space ABI: structs and constants shared with user space that must never break, and headers_install turns it into /usr/include/linux. include/linux is kernel-internal and changes freely between versions. A kernel header usually includes its uapi counterpart and adds internal parts — linux/fs.h includes uapi/linux/fs.h, for example. A driver's ioctl structures belong in uapi.

**Q.** How big is the kernel, and where does the code actually go?

**A.** 6.18 기준 소스가 약 3,600만 줄이고 그중 69%가 `drivers/`다. 코어(`kernel/`+`mm/`)는 2%도 안 된다. 실제 arm64 defconfig 이미지도 빌트인 코드의 66%가 드라이버이고, 그중 가장 큰 건 여러 SoC를 한 이미지로 부팅하려고 넣은 pinctrl·clock 드라이버다. 이 설정에서 컴파일되는 `.c`는 전체의 약 31%뿐이다. 그래서 제품 커널은 자기 SoC 드라이버만 남겨 크게 줄일 수 있다.

> Around 36 million lines in 6.18, and about 69 percent of that is drivers; the core — kernel and mm — is under 2 percent. In an arm64 defconfig image, drivers are still about two-thirds of the built-in code, led by pinctrl and clock drivers for all the SoCs the generic image supports. Only about 31 percent of the C files are even compiled for that config, so a product kernel can be trimmed a lot by keeping just its own SoC's drivers.

---

## 11. 직접 해보기

1. **개념**: 그림 3에서 `mmap()`으로 카메라 버퍼를 매핑하는 요청은 어떤 상자들을 거치나? — 정답/힌트: syscall → mm(`vm_area_struct` 생성) → VFS(`file->f_op->mmap`) → V4L2 프레임워크 → 드라이버의 버퍼 매핑(`remap_pfn_range` 또는 videobuf2), G5·I5.
2. **측정**: 예제 1 스크립트를 고쳐 `drivers/iio/`의 하위 디렉터리별 줄 수를 세고, `imu/` 안에 어떤 칩 드라이버가 있는지 나열하라. — 정답/힌트: `drivers/iio/imu/`에 bmi160, bmi270, inv_mpu6050, st_lsm6dsx 등. Don의 센서 키트 IMU가 여기 있다.
3. **길찾기**: `write()`를 `/dev/ttyAMA0`에 했을 때 PL011의 TX FIFO에 바이트가 들어가는 함수까지 file:line으로 찾아라. — 정답/힌트: `tty_fops`의 `.write_iter` → `n_tty_write` → serial core의 `uart_write` → `start_tx` → `amba_pl011_pops`의 `pl011_start_tx` → `pl011_tx_chars`. `.start_tx\s*=` grep.
4. **관찰**: 예제 13의 게스트에서 `echo t > /proc/sysrq-trigger` 후 `dmesg`로 모든 태스크의 스택을 덤프하고, `kworker`가 어디서 자는지 보라. — 정답/힌트: 대부분 `worker_thread` → `schedule`. `flush_to_ldisc`가 이 kworker 위에서 돈다.
5. **도구**: 자기 PC(또는 VM의 `/var/tmp/E1` 복사본)에서 `make ARCH=arm64 COMPILED_SOURCE=1 O=<빌드디렉터리> cscope`를 실행하고(ctags/cscope 설치 필요), `vfs_read`의 호출자를 cscope로 찾아 예제 5의 grep 결과와 비교하라. — 정답/힌트: cscope는 문자열·주석을 빼고 실제 호출 3곳만 보여 준다. 함수 포인터 호출은 둘 다 못 찾는다.
6. **이미지**: 예제 11 스크립트에 `data` 열 기준 정렬을 추가해 데이터가 가장 큰 디렉터리를 찾아라. 왜 `kernel/`의 data가 text 대비 큰가? — 정답/힌트: `kernel/` data 631,550바이트 — kallsyms·printk 링버퍼·per-CPU 영역 같은 큰 정적 표가 `kernel/` 쪽에 있다. 예제 12의 큰 심볼 목록과 맞춰 보라.
7. **C 과제**: 예제 14를 확장해 `/proc/modules`를 읽고 모듈마다 `/proc/kallsyms`에서 `[모듈명]` 심볼 수와 `T`(export 가능 전역 함수) 수를 출력하라. `insmod`한 crc-ccitt로 확인한다. — 정답/힌트: crc_ccitt는 18줄, `T`는 `crc_ccitt` 하나.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| monolithic kernel | 모놀리식 커널 | 모든 서브시스템·드라이버가 한 커널 주소 공간에서 같은 권한으로 도는 구조 |
| microkernel | 마이크로커널 | IPC·스케줄링·주소 공간만 커널에 두고 나머지를 유저 서버로 돌리는 구조 |
| LKM (loadable kernel module) | 적재 가능 모듈 | 실행 중에 커널 주소 공간에 링크해 넣는 `.ko` 파일 |
| subsystem | 서브시스템 | 커널의 기능 덩어리 (sched, mm, VFS, net, TTY, IIO …) |
| framework | 프레임워크 | 드라이버가 ops를 채워 등록하는 서브시스템 쪽 공용 코드 |
| ops 표 | 함수 포인터 구조체 | `file_operations`, `uart_ops`처럼 구현을 숨기는 인터페이스 |
| `arch/` | 아키텍처 디렉터리 | CPU별 예외 진입·MMU·부팅 코드, arm64는 `arch/arm64` |
| uapi | user API 헤더 | 유저와 공유하는 ABI 헤더, 깨면 안 된다 |
| `vmlinux` | 커널 ELF | 심볼·디버그 정보가 있는 링크 결과, 디버깅용 |
| `Image` | 부팅 이미지 | `vmlinux`를 raw binary로 만든 것, 부트로더가 싣는다 |
| `System.map` | 심볼 맵 | 링크 시점 심볼 주소 목록 |
| `/proc/kallsyms` | 실행 중 심볼 표 | KASLR 적용 후 실제 주소, 모듈 심볼 포함 |
| KASLR | 커널 주소 무작위화 | 부팅마다 커널 위치를 무작위로 옮기는 보안 기능 |
| thin archive | 얇은 아카이브 | 객체를 복사하지 않고 경로만 담는 `.a` (`vmlinux.a`, `built-in.a`) |
| `EXPORT_SYMBOL(_GPL)` | 심볼 내보내기 | 모듈이 쓸 수 있게 공개한 커널 함수·변수 |
| MAINTAINERS | 담당자 파일 | 파일 패턴별 메인테이너·리스트·git 트리 목록 |
| `faddr2line` | 주소→줄 스크립트 | `심볼+오프셋/크기`를 file:line으로 바꿈 |
| compile_commands.json | 컴파일 DB | 파일별 실제 컴파일 명령, clangd가 사용 |
| line discipline | 회선 규칙 | TTY와 드라이버 사이에서 줄 편집·에코·특수 문자를 처리하는 층 (n_tty) |
| flip buffer | 플립 버퍼 | UART 드라이버가 받은 바이트를 TTY 계층에 넘기는 버퍼 |

---

## 13. 요약 & 체크리스트

Linux는 스케줄러·메모리·VFS·네트워크·드라이버가 한 주소 공간에서 직접 함수 호출로 돌아가는 monolithic 커널이고, 모듈은 격리가 아니라 지연 링크다. 하지만 소스는 서브시스템과 ops 표로 깔끔하게 나뉘어 있어, 위 층은 아래 층의 구현을 모른 채 `f_op->read_iter`, `ld->ops->read`, `port->ops` 같은 표만 부른다. 랩 트리 3,637만 줄 중 69%가 `drivers/`이고 코어는 2% 미만이며, arm64 defconfig 이미지(적재 43 MB)도 빌트인 코드의 66%가 드라이버다. 코드 찾기는 compatible·메시지·장치 이름 문자열의 `git grep`에서 시작해, 정의는 `^반환형 이름(`, 간접 호출은 ops 초기화로 찾고, 의미 기반 검색은 compile DB + clangd 또는 elixir를, 담당자는 `get_maintainer.pl`을, 크래시 주소는 같은 빌드의 vmlinux와 `faddr2line`을 쓴다. `/dev/ttyAMA0`의 `read()`는 `el0t_64_sync` → `invoke_syscall` → `vfs_read` → `tty_read` → `n_tty_read`에서 잠들고, PL011 인터럽트가 `pl011_int` → flip buffer → 워크큐 → n_tty를 거쳐 깨운다 — 이 경로는 잠든 reader의 커널 스택으로 실제 확인했다.

- [ ] monolithic / microkernel / RTOS의 차이를 "드라이버가 어디 사는가"로 30초 안에 설명할 수 있다
- [ ] 모듈이 격리가 아님을 `/proc/kallsyms`의 `[모듈명]` 심볼로 보여 줄 수 있다
- [ ] 그림 3의 서브시스템 지도를 빈 종이에 그리고 각 상자의 디렉터리를 말할 수 있다
- [ ] 최상위 디렉터리 10개 이상의 역할을 말하고 `drivers/`가 대부분이라는 것을 숫자로 말할 수 있다
- [ ] `arch/arm64`의 `kernel/`, `mm/`, `boot/dts/`에 무엇이 있는지 말할 수 있다
- [ ] `include/linux`, `include/uapi`, `arch/arm64/include/asm`의 차이를 설명할 수 있다
- [ ] 함수 정의·호출자·ops 구현을 `git grep`으로 찾고, 텍스트 검색의 한계를 설명할 수 있다
- [ ] `get_maintainer.pl`과 MAINTAINERS의 `M:`/`L:`/`F:`/`T:`를 읽을 수 있다
- [ ] `faddr2line`으로 `심볼+오프셋/크기`를 file:line으로 바꾸고, 크기 불일치의 의미를 설명할 수 있다
- [ ] `size -A`, `nm --size-sort`, `vmlinux.a`로 이미지의 구성을 디렉터리별로 잴 수 있다
- [ ] `.config`의 `=y`/`=m`과 `/sys/module`, `/proc/modules`의 관계를 설명할 수 있다
- [ ] `/dev/ttyAMA0`의 `read()` 경로를 소비자·생산자 양쪽으로 file:line과 함께 설명할 수 있다

---

## 참고 자료

- docs.kernel.org — The Linux kernel user's and administrator's guide / Driver implementer's API guide: https://docs.kernel.org/
- docs.kernel.org — Linux kernel coding style (함수 정의 형식, 위 grep 요령의 근거): https://docs.kernel.org/process/coding-style.html
- docs.kernel.org — How to get your change into the Linux kernel (MAINTAINERS, get_maintainer.pl): https://docs.kernel.org/process/submitting-patches.html
- docs.kernel.org — Bug hunting (Oops 해석, faddr2line·decode_stacktrace): https://docs.kernel.org/admin-guide/bug-hunting.html
- docs.kernel.org — The TTY layer / Low Level Serial API: https://docs.kernel.org/driver-api/tty/index.html , https://docs.kernel.org/driver-api/serial/driver.html
- Elixir Cross Referencer (Bootlin): https://elixir.bootlin.com/linux/v6.18/source
- Bootlin, "Linux kernel and driver development training" 슬라이드 (커널 소스 구조, 모듈, 문자 디바이스 장): https://bootlin.com/training/kernel/
- 커널 소스 (이 노트에서 인용한 위치, linux-6.18.55): `fs/read_write.c`(`SYSCALL_DEFINE3(read)` 722, `ksys_read` 704, `vfs_read` 552, `new_sync_read` 481), `fs/char_dev.c`(`chrdev_open` 373), `arch/arm64/kernel/entry.S`(530), `arch/arm64/kernel/entry-common.c`(`el0_svc` 743, `el0t_64_sync_handler` 762, `do_interrupt_handler` 154), `arch/arm64/kernel/syscall.c`(`invoke_syscall` 38, `do_el0_svc` 149), `arch/arm64/kernel/sys.c`(60), `scripts/syscall.tbl`(82), `drivers/tty/tty_io.c`(`tty_fops` 459, `iterate_tty_read` 835, `tty_read` 901), `drivers/tty/n_tty.c`(`n_tty_read` 2195, `n_tty_ops` 2512, `n_tty_receive_buf2` 1766, `n_tty_receive_handle_newline` 1242), `drivers/tty/tty_buffer.c`(`flush_to_ldisc` 462, `tty_flip_buffer_push` 528), `drivers/tty/tty_port.c`(23), `drivers/tty/serial/serial_core.c`(`uart_insert_char` 3479, `uart_register_driver` 2758), `drivers/tty/serial/amba-pl011.c`(`pl011_fifo_to_tty` 321, `pl011_rx_chars` 1468, `pl011_int` 1602, `amba_pl011_pops` 2324, `pl011_probe` 2899, `pl011_ids` 3121), `drivers/irqchip/irq-gic.c`(`gic_handle_irq` 334), `scripts/tags.sh`, `scripts/get_maintainer.pl`, `scripts/faddr2line`, `scripts/clang-tools/gen_compile_commands.py`
- Robert Love, "Linux Kernel Development" 3판 — 1장(커널 소개, monolithic vs microkernel), 2장(커널 소스 트리)
- OSTEP (Arpaci-Dusseau), 2장 "Introduction to Operating Systems"와 6장 "Limited Direct Execution" — 유저/커널 모드와 trap의 원리
