# H10. 메일박스 · 하드웨어 스핀락 — 이종 코어 통신의 하드웨어 기본기

> **이 노트를 다 읽으면**: Cortex-A(Linux)와 Cortex-M/R(RTOS)·DSP가 한 칩에 있을 때 왜 "알림용 메일박스"와 "코어 간 락용 hwspinlock"이 따로 필요한지 그림으로 설명할 수 있다 · Linux mailbox framework의 controller(`mbox_controller`, `mbox_chan_ops`, txdone irq/poll/ack, `of_xlate`)와 client(`mbox_client`, `mbox_request_channel_byname`, `mbox_send_message`, `rx_callback`) 양쪽을 직접 짜서 QEMU에서 돌려 볼 수 있다 · 타임아웃 뒤 프레임워크 상태가 원격 코어와 어긋나는 장면을 실제 로그로 읽고 대책(sequence 번호, 채널 리셋)을 말할 수 있다 · hwspinlock API·provider ops·regmap `use_hwlock`을 쓰고, "공유 메모리 + hwspinlock + doorbell" IPC 3종 세트와 그 위의 rpmsg/remoteproc·SCMI를 연결해 설명할 수 있다
> **왜 필요한가**: i.MX8·AM62·STM32MP1처럼 "Linux + 실시간 MCU 코어"가 한 칩에 있는 SoC가 로봇·드론·센서 장비의 표준이 됐다. 모터 제어·센서 타이밍은 M코어가, 인식·네트워크는 Linux가 맡고, 둘 사이 통신이 깨지면 장비 전체가 멈춘다. 또 최신 SoC는 클럭·전원·센서를 SCMI 펌웨어에 맡기는데, 그 통로가 메일박스다. "Linux와 co-processor는 어떻게 통신하나?"는 이종 SoC 회사 면접의 단골 질문이다
> **Don 기준 난이도**: 멀티코어 SSD 컨트롤러에서 Cortex-R 코어끼리 mailbox/doorbell 레지스터와 공유 SRAM으로 명령을 주고받은 경험이 이 노트의 거의 전부를 이미 덮는다 / 새로 배울 부분은 Linux가 그것을 감싸는 방식 — controller/client 분리, txdone 판정 3방식, 콜백이 도는 컨텍스트, DT `mboxes`/`hwlocks` 바인딩, 타임아웃 시 프레임워크 동작, hwspinlock이 로컬 spinlock과 2단으로 겹치는 구조
> **선행 노트**: F4(락 — spinlock·atomic), F5(메모리 배리어), F6(인터럽트 처리), F8(hrtimer), G1~G3(platform driver·probe), D1~D3(Device Tree), H1(MMIO), H2(regmap), C5(SMC를 EL3로 가는 "메일박스"처럼 쓰는 PSCI) — O1~O3(remoteproc·rpmsg·OpenAMP)로 이어진다

---

## 0. 큰 그림 — 한 칩 안의 여러 "컴퓨터"

요즘 임베디드 SoC는 "CPU 하나"가 아니다. 예를 들어 센서 키트를 i.MX8M Plus로 만들면 칩 안에 이런 것들이 같이 있다.

- Cortex-A53 4개: Linux. 카메라·네트워크·인식 파이프라인.
- Cortex-M7 1개: FreeRTOS나 Zephyr. 모터 PWM, IMU를 정확히 1 kHz로 읽기, 안전 감시.
- HiFi4 DSP: 오디오나 센서 신호 처리 펌웨어.

이 셋은 **서로 다른 OS, 서로 다른 캐시, 서로 다른 메모리 지도**를 가진다. 같은 DDR을 볼 수는 있지만, Cortex-A 클러스터 안에서 통하던 "캐시 일관성(coherency)"과 "원자적 명령(LDXR/STXR, LSE atomics)"의 약속이 Cortex-M까지 이어지지 않는다. 그래서 두 가지 하드웨어 블록이 따로 붙는다.

1. **메일박스(mailbox)**: "너한테 할 말 있어"를 **상대 코어의 인터럽트**로 전달하는 블록. 짧은 데이터 레지스터(보통 32비트 몇 개)를 같이 들고 있기도 하다.
2. **하드웨어 스핀락(hwspinlock, hardware semaphore)**: "이 공유 자원은 지금 내 것"을 **버스 트랜잭션 한 번으로 원자적으로** 판정해 주는 레지스터 묶음.

그리고 이 둘 옆에 **공유 메모리**(on-chip SRAM이나 DDR 예약 영역)가 있어서 실제 메시지 본문이 거기 놓인다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="200" height="92" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="54" text-anchor="middle" font-size="13" font-weight="bold">Cortex-A 클러스터</text>
  <text x="120" y="76" text-anchor="middle" font-size="12">Linux · 드라이버</text>
  <text x="120" y="96" text-anchor="middle" font-size="12">L1/L2 캐시 (서로 coherent)</text>
  <rect x="240" y="30" width="200" height="92" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="54" text-anchor="middle" font-size="13" font-weight="bold">Cortex-M/R 코어</text>
  <text x="340" y="76" text-anchor="middle" font-size="12">FreeRTOS · Zephyr</text>
  <text x="340" y="96" text-anchor="middle" font-size="12">자기 TCM · 자기 캐시</text>
  <rect x="460" y="30" width="200" height="92" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="54" text-anchor="middle" font-size="13" font-weight="bold">DSP</text>
  <text x="560" y="76" text-anchor="middle" font-size="12">오디오·신호처리 펌웨어</text>
  <text x="560" y="96" text-anchor="middle" font-size="12">자기 로컬 메모리</text>
  <rect x="20" y="160" width="640" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="182" text-anchor="middle" font-size="12">SoC 인터커넥트 (AXI/NoC) — 클러스터 사이에는 캐시 일관성이 없다</text>
  <line x1="120" y1="122" x2="120" y2="158" stroke="currentColor" marker-end="url(#ah1)" marker-start="url(#ah1)"/>
  <line x1="340" y1="122" x2="340" y2="158" stroke="currentColor" marker-end="url(#ah1)" marker-start="url(#ah1)"/>
  <line x1="560" y1="122" x2="560" y2="158" stroke="currentColor" marker-end="url(#ah1)" marker-start="url(#ah1)"/>
  <rect x="20" y="236" width="200" height="104" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="120" y="260" text-anchor="middle" font-size="13" font-weight="bold">메일박스</text>
  <text x="120" y="282" text-anchor="middle" font-size="12">MU · IPCC · MHU</text>
  <text x="120" y="302" text-anchor="middle" font-size="12">데이터 레지스터 + 상태</text>
  <text x="120" y="322" text-anchor="middle" font-size="12">쓰면 상대 코어에 IRQ</text>
  <rect x="240" y="236" width="200" height="104" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="260" text-anchor="middle" font-size="13" font-weight="bold">하드웨어 스핀락</text>
  <text x="340" y="282" text-anchor="middle" font-size="12">락 레지스터 N개</text>
  <text x="340" y="302" text-anchor="middle" font-size="12">읽기/쓰기 한 번이</text>
  <text x="340" y="322" text-anchor="middle" font-size="12">원자적 획득 판정</text>
  <rect x="460" y="236" width="200" height="104" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="560" y="260" text-anchor="middle" font-size="13" font-weight="bold">공유 메모리</text>
  <text x="560" y="282" text-anchor="middle" font-size="12">on-chip SRAM · DDR 예약</text>
  <text x="560" y="302" text-anchor="middle" font-size="12">메시지 본문 · 링버퍼</text>
  <text x="560" y="322" text-anchor="middle" font-size="12">(vring, SCMI shmem)</text>
  <line x1="120" y1="196" x2="120" y2="234" stroke="currentColor" marker-end="url(#ah1)" marker-start="url(#ah1)"/>
  <line x1="340" y1="196" x2="340" y2="234" stroke="currentColor" marker-end="url(#ah1)" marker-start="url(#ah1)"/>
  <line x1="560" y1="196" x2="560" y2="234" stroke="currentColor" marker-end="url(#ah1)" marker-start="url(#ah1)"/>
  <text x="340" y="378" text-anchor="middle" font-size="12">Don의 SSD 컨트롤러 = Cortex-R 여러 개 + mailbox/doorbell + 공유 SRAM — 같은 구조다</text>
</svg>
```

그림 1 — 이종(heterogeneous) SoC. 위쪽 세 "컴퓨터"는 서로 캐시 일관성도, 같은 OS도 없다. 아래 세 블록이 그 사이를 잇는다: 메일박스는 **알림**, 하드웨어 스핀락은 **상호 배제**, 공유 메모리는 **본문**.

Don에게 이건 새로운 이야기가 아니다. SSD 컨트롤러의 Cortex-R 코어들(호스트 인터페이스 코어, FTL 코어, NAND 백엔드 코어)은 **명령 디스크립터를 공유 SRAM에 쓰고, mailbox/doorbell 레지스터를 써서 상대 코어에 IRQ를 걸고**, 공유 자원(예: 프리 블록 풀)은 하드웨어 세마포어 레지스터로 잡았다. Linux가 하는 일은 그 하드웨어를 **두 개의 프레임워크**로 감싸 표준 API를 주는 것이다.

| 하드웨어 | Linux 프레임워크 | 소스 위치 | 펌웨어 세계의 이름 |
|---|---|---|---|
| 메일박스 / doorbell | mailbox framework (`CONFIG_MAILBOX`) | `drivers/mailbox/`, `include/linux/mailbox_{controller,client}.h` | MU, IPC 레지스터, doorbell |
| 하드웨어 스핀락 | hwspinlock framework (`CONFIG_HWSPINLOCK`) | `drivers/hwspinlock/`, `include/linux/hwspinlock.h` | HW semaphore, HSEM, mutex 레지스터 |
| 공유 메모리 | 프레임워크 없음 — `reserved-memory` + `memremap`/`ioremap` (F10, H1) | DT `memory-region` | 공유 SRAM 영역 |

### 0.1 이 노트의 지도

| 절 | 내용 | 직접 돌리는 것 |
|---|---|---|
| §1 | 왜 공유 메모리만으로는 안 되나, doorbell vs data mailbox, 실물 SoC | — |
| §2 | 랩 준비: 가짜 하드웨어 DT 노드, 빌드, 게스트 실행기 | 예제 1~3 |
| §3 | mailbox **controller** 쪽 (`h10_fakembox`) | 코드 |
| §4 | mailbox **client** 쪽 (`h10_client`), 콜백 컨텍스트 | 예제 4 |
| §5 | TX 완료 판정 3방식 irq/poll/ack + `knows_txdone` | 예제 5 |
| §6 | 큐 넘침, 늦은 원격, 죽은 원격 — 실패할 때 프레임워크 | 예제 6~8 |
| §7 | 왕복 지연 측정 (payload × txdone 모드) | 예제 9 + 차트 |
| §8 | hwspinlock: 왜 필요한가, API, provider, regmap | 예제 10 |
| §9 | IPC 3종 세트, rpmsg/remoteproc, SCMI | — |
| §10 | 실물 SoC DT(i.MX8MP, STM32MP15, AM62, Juno SCMI) 해독 | 예제 11~12 |

### 0.2 이 노트의 실험에 대한 정직한 경고

QEMU `virt` 머신에는 **메일박스도 hwspinlock 하드웨어도 없다**. 그래서 이 노트의 실험은 전부 **가짜(fake) provider** 위에서 돈다.

- `h10_fakembox`: 진짜 mailbox controller 드라이버처럼 framework에 등록되지만, "원격 코어"는 **hrtimer 콜백이 흉내 낸다**. hrtimer는 hardirq 컨텍스트에서 돌기 때문에, 진짜 메일박스 RX 인터럽트 핸들러에서 콜백이 불리는 상황과 컨텍스트가 같다.
- `h10_hwlock`: 진짜 hwspinlock provider처럼 등록되지만, "락 레지스터"는 **보통 메모리의 32비트 워드에 `cmpxchg`** 를 하는 것이다. "원격 코어"는 CPU0에 고정한 **kthread가 흉내 낸다**. 진짜 하드웨어는 버스 트랜잭션 하나로 원자성을 보장하고, 우리 가짜는 Cortex-A57의 원자 명령으로 보장한다는 점이 다르다.
- 측정값은 **TCG 에뮬레이션(KVM 없음)** 이고, 같은 VM에서 다른 작업 11개가 동시에 돌았다(측정 직후 `uptime`의 load average 9.63). 절대값은 실제 실리콘과 다르다. **모드 사이의 상대 차이**와 **동작 순서**만 믿는다.

framework 쪽 코드(`drivers/mailbox/mailbox.c`, `drivers/hwspinlock/hwspinlock_core.c`)는 진짜 커널 코드 그대로 돈다. 그래서 "framework가 무엇을 언제 부르는가"는 실물과 같다.

---

## 1. 왜 이종 SoC에 메일박스와 hwspinlock이 필요한가

### 1.1 공유 메모리만으로 부족한 두 가지 이유

"둘 다 DDR을 볼 수 있으니 거기에 메시지를 쓰면 되지 않나?" 반은 맞다. 본문은 실제로 공유 메모리에 쓴다. 하지만 두 가지가 빠진다.

**(1) 알림이 없다.** 상대가 메시지를 썼는지 알려면 계속 폴링해야 한다. Linux가 1 ms마다 공유 메모리를 들여다보면 전력도 낭비고 지연도 최대 1 ms다. Cortex-M이 WFI로 잠들어 있으면 폴링 자체를 못 한다. 필요한 것은 "상대 코어에 인터럽트를 거는 레지스터"다. 이게 **메일박스(doorbell)** 다. SSD 펌웨어에서 "다른 코어의 IPC 레지스터 비트를 세트하면 그 코어에 IRQ가 뜬다"와 정확히 같다.

**(2) 원자성이 이어지지 않는다.** Linux의 `spin_lock()`은 Cortex-A의 exclusive monitor(LDAXR/STXR) 또는 LSE 원자 명령(CAS, SWP)으로 구현된다(F4). 이 명령들이 다른 코어와 원자적으로 동작하려면 두 코어가 **같은 coherency 도메인**에 있어서 캐시 라인 소유권을 하드웨어가 중재해 줘야 한다. 그런데:

- Cortex-M4/M7은 대개 Cortex-A 클러스터의 coherent 인터커넥트 밖에 있다. M코어가 공유 SRAM에 LDREX/STREX를 해도 그 exclusive monitor는 **M코어 쪽 로컬 모니터**일 뿐, A코어의 캐시 라인 상태와 아무 관계가 없다.
- 캐시도 문제다. A코어가 캐시에 들고 있는 락 워드를 M코어는 DDR에서 직접 읽는다. 한쪽이 "잠갔다"고 쓴 값이 아직 캐시에만 있으면 다른 쪽은 "풀려 있다"로 본다.
- 결정적으로, M코어의 RTOS는 Linux의 `spinlock_t` 구조체가 무엇인지 모른다. Linux 쪽 qspinlock의 내부 형식(대기 큐 꼬리 인코딩 등)을 RTOS가 따라 할 수도 없다.

그래서 **버스 트랜잭션 한 번에 "획득 성공/실패"가 결정되는 전용 레지스터**가 필요하다. 하드웨어 스핀락은 보통 이렇게 생겼다(§8.3에서 실제 드라이버 코드로 본다).

- **읽어서 잡기(read-to-acquire)**: 락 레지스터를 읽었을 때 0이 나오면 내가 잡은 것이다. 읽는 순간 하드웨어가 1로 바꾼다. TI OMAP/AM6x 계열.
- **써서 잡고 다시 읽어 확인(write-then-readback)**: 내 코어 ID를 쓰고 다시 읽어 내 ID가 보이면 성공. STM32 HSEM, Qualcomm TCSR mutex.

어느 쪽이든 원자성은 **주변장치 레지스터(Device 메모리, 캐시 안 됨)의 단일 접근**이 보장한다. 코어 종류·캐시·OS와 무관하다.

### 1.2 doorbell 메일박스 vs data 메일박스

| 종류 | 무엇을 전달하나 | 예 | Linux에서 `void *mssg`의 의미 |
|---|---|---|---|
| doorbell (신호만) | "뭔가 있다"는 사실 = IRQ 1개 (채널 = 비트 하나) | Qualcomm APCS IPC, ARM MHU doorbell 모드, STM32 IPCC | 대개 무시하거나 NULL. 본문은 공유 메모리 |
| data 메일박스 | 32비트 워드 몇 개 + IRQ | i.MX MU(TX/RX 레지스터 4개), TI mailbox(FIFO), ARM MHU(32비트 status) | 컨트롤러 드라이버가 정한 형식으로 해석 (예: `u32 *`) |

실무에서는 data 메일박스도 **"명령 코드 하나 + 공유 메모리 포인터/인덱스"** 정도만 담고, 진짜 본문은 공유 메모리에 둔다. 레지스터 몇 개로는 센서 프레임 하나도 못 담기 때문이다. 이 노트의 `h10_msg`도 같은 생각으로 만들었다: `cmd` 워드는 "메일박스 레지스터"에 해당하고, `shm`/`len`은 "공유 SRAM의 본문"을 가리킨다.

### 1.3 실물 SoC에서의 모습

아래 표의 Linux 드라이버 이름과 DT 속성은 랩 커널 소스(`/var/lab/src/linux-6.18.55`)에서 확인했다. 하드웨어 설명(레지스터 개수 등)은 **기억에 의존한 것**이라 정확한 값은 각 SoC의 Reference Manual로 확인해야 한다.

| SoC | 메일박스 | Linux 드라이버 (확인) | 하드웨어 스핀락 | Linux 드라이버 (확인) | 비고 |
|---|---|---|---|---|---|
| NXP i.MX8M/i.MX9 | MU (Messaging Unit) | `imx-mailbox.c`, `#mbox-cells = <2>` (타입, 인덱스) | 별도 블록 없음 (기억) | — | MU는 TX/RX 데이터 레지스터 + 플래그 레지스터 (기억). DSP·Cortex-M remoteproc가 사용 |
| TI AM62/AM64 | Mailbox (FIFO 기반) | `omap-mailbox.c`, 서브노드 `ti,mbox-tx`/`ti,mbox-rx` | Spinlock 블록 | `omap_hwspinlock.c` (`ti,am64-hwspinlock`) | 메시지 FIFO + 사용자별 IRQ (기억) |
| ST STM32MP1 | IPCC | `stm32-ipcc.c`, `#mbox-cells = <1>` | HSEM | `stm32_hwspinlock.c` (`st,stm32-hwspinlock`) | IPCC는 채널당 "occupied/free" 플래그만 있는 doorbell형 |
| Qualcomm | APCS IPC, IPCC | `qcom-apcs-ipc-mailbox.c`, `qcom-ipcc.c` | TCSR mutex | `qcom_hwspinlock.c` (`qcom,tcsr-mutex`) | APCS는 레지스터에 `BIT(idx)` 한 번 쓰는 순수 doorbell |
| ARM Juno 등 | MHU | `arm_mhu.c`, `arm_mhu_db.c` | — | — | SCMI 펌웨어(SCP)와의 통로 |

### 1.4 Don의 경험과 연결

| SSD 펌웨어에서 하던 것 | 이 노트의 Linux 용어 |
|---|---|
| 다른 코어 IPC 레지스터에 비트 쓰기 → 그 코어 IRQ | `mbox_send_message()` → controller의 `send_data` |
| IPC IRQ 핸들러에서 명령 큐 읽기 | controller ISR → `mbox_chan_received_data()` → client의 `rx_callback` |
| "상대가 내 명령을 가져갔나" 상태 비트 확인 | txdone: `last_tx_done` 폴링 또는 TX-empty IRQ |
| 응답 메시지를 받고 나서야 다음 명령 전송 | ACK 모드 (`knows_txdone` + `mbox_client_txdone`) |
| HW 세마포어 레지스터로 공유 풀 잠금 | `hwspin_lock_timeout()` / `hwspin_unlock()` |
| 공유 SRAM의 명령 디스크립터 링 | `reserved-memory` + vring(rpmsg) 또는 SCMI shmem |

---

## 2. 랩 준비 — 가짜 하드웨어를 Device Tree에 꽂기

VM 호스트 커널과 랩 게스트 커널을 먼저 구분해 둔다.

```sh
limactl shell elinux -- uname -r
```

```text
7.0.0-38-generic
```

이건 VM 호스트 커널이다. 이 노트의 모든 커널 모듈은 **랩 커널 6.18.55**(`/var/lab/out/Image`)용으로 빌드해서 **QEMU 게스트 안에서만** `insmod`한다. 랩 커널 설정에는 과제에 필요한 것이 이미 켜져 있다(예제 3에서 게스트 `/proc/config.gz`로 확인).

### 2.1 예제 1 — 가짜 하드웨어를 DT에 선언하기

무엇을 확인하나: 실제 SoC dtsi에 있을 법한 메일박스·hwspinlock 노드와 그 사용자(client) 노드를 QEMU virt DT 뒤에 덧붙인다. `#mbox-cells`/`mboxes`/`mbox-names`, `#hwlock-cells`/`hwlocks`/`hwlock-names`가 핵심이다.

```dts
/* h10_nodes.dtsi: appended to a dump of the QEMU virt DT (-smp 2 -m 512M -cpu cortex-a57) */
/ {
	h10_mbox: h10-mailbox {
		compatible = "h10,fake-mbox";
		#mbox-cells = <1>;              /* 1 cell = channel index */
	};

	h10-mbox-client {
		compatible = "h10,mbox-client";
		mboxes = <&h10_mbox 0>, <&h10_mbox 1>;
		mbox-names = "ctrl", "event";
	};

	h10_hwlock: h10-hwspinlock {
		compatible = "h10,fake-hwspinlock";
		#hwlock-cells = <1>;
	};

	h10-hwlock-user {
		compatible = "h10,hwlock-user";
		hwlocks = <&h10_hwlock 0>, <&h10_hwlock 1>;
		hwlock-names = "data", "reg";
	};
};
```

읽는 법은 인터럽트(`interrupts` + `#interrupt-cells`)나 GPIO(`gpios` + `#gpio-cells`)와 똑같은 **phandle + specifier** 패턴이다(D3).

- provider 노드의 `#mbox-cells = <1>`: "나를 가리킬 때 phandle 뒤에 숫자 1개를 붙여라."
- client의 `mboxes = <&h10_mbox 0>, <&h10_mbox 1>`: 0번 항목 = 채널 0, 1번 항목 = 채널 1.
- `mbox-names = "ctrl", "event"`: 0번 항목의 이름은 `ctrl`, 1번은 `event`. 드라이버는 숫자 대신 이름으로 찾는다(`mbox_request_channel_byname`).
- hwspinlock도 같다. 차이는 셀 이름이 `#hwlock-cells`, 속성 이름이 `hwlocks`/`hwlock-names`라는 것뿐이다.

이 블록을 virt DT에 붙여 DTB를 만든다. README의 경고대로, `-dtb`로 넘길 DT는 **부팅할 때와 같은 `-smp`/`-m`/`-cpu`로 덤프**해야 한다(CPU 노드 수가 달라지면 부팅이 꼬인다). hwspinlock 실험은 "두 코어가 동시에 경쟁"해야 하므로 `-smp 2`로 덤프한다.

```sh
#!/bin/bash
# mk_dtb.sh — dump QEMU virt DT with the SAME -smp/-m/-cpu we boot with, append H10 nodes
set -e
cd /var/tmp/H10
qemu-system-aarch64 -machine virt,dumpdtb=virt2.dtb -cpu cortex-a57 -smp 2 -m 512M -nographic
dtc -q -I dtb -O dts virt2.dtb -o virt2.dts
cat virt2.dts /Users/donh/.elinux_scratch/H10/h10_nodes.dtsi > h10.dts
dtc -q -I dts -O dtb h10.dts -o h10.dtb
grep -c "cpu@" virt2.dts
dtc -q -I dtb -O dts h10.dtb | grep -B1 -A3 "h10-mailbox {"
```

```text
2

	h10-mailbox {
		compatible = "h10,fake-mbox";
		#mbox-cells = <0x01>;
		phandle = <0x01>;
```

출력에서 볼 것: CPU 노드가 2개(`-smp 2`)이고, `dtc`가 라벨 `h10_mbox`를 위해 `phandle = <0x01>`을 자동으로 만들어 넣었다. QEMU가 만든 노드의 phandle은 `0x8000`대라 겹치지 않는다. 덤프 단계에서 `dtc` 경고는 `-q`로 숨겼다.

### 2.2 예제 2 — 모듈 빌드

무엇을 확인하나: 세 모듈(`h10_fakembox`, `h10_client`, `h10_hwlock`)을 랩 커널 빌드 트리로 빌드한다. hwspinlock provider는 `drivers/hwspinlock/hwspinlock_internal.h`(구조체 `hwspinlock_device`, `hwspinlock_ops`)가 필요한데, 이 헤더는 `include/linux/`가 아니라 **드라이버 폴더 안에 있다**. 즉 hwspinlock provider는 원래 "트리 안에서" 쓰라는 뜻이다. 트리 밖 실험이므로 헤더를 복사해 쓴다.

```sh
#!/bin/bash
# build.sh — build the three H10 modules against the lab kernel (6.18.55)
set -e
D=/var/tmp/H10/mod
mkdir -p $D && cd $D
cp /Users/donh/.elinux_scratch/H10/{h10_fakembox.c,h10_client.c,h10_hwlock.c,h10_msg.h} .
cp /var/lab/src/linux-6.18.55/drivers/hwspinlock/hwspinlock_internal.h .
printf 'obj-m += h10_fakembox.o h10_client.o h10_hwlock.o\nccflags-y += -I$(src)\n' > Kbuild
make -C /var/lab/out/linux-build M=$D -j2 modules W=0 2>&1 | grep -E "warning|error|\.ko$" || true
ls -1 $D/*.ko
```

```text
  LD [M]  h10_fakembox.ko
  LD [M]  h10_hwlock.ko
  LD [M]  h10_client.ko
/var/tmp/H10/mod/h10_client.ko
/var/tmp/H10/mod/h10_fakembox.ko
/var/tmp/H10/mod/h10_hwlock.ko
```

출력에서 볼 것: `warning`/`error` 줄이 하나도 없다(경고 0개). 처음 빌드 때는 `in_hardirq()`가 `unsigned long`을 돌려주는데 `%d`로 찍어서 `-Wformat` 경고가 났고, `!!in_hardirq()`로 고쳤다. 커널 매크로의 반환형을 가정하지 말 것.

게스트 실행기는 F4·E4와 같은 구조다. 차이는 `-dtb /var/tmp/H10/h10.dtb`와 기본 `-smp 2`뿐이다.

```sh
#!/bin/bash
# run_guest.sh <tag> <test.sh>   (env: FILES, SMP, TMO)
set -e
tag=$1; t=$2
W=/var/tmp/H10/run_$tag
rm -rf "/var/tmp/H10/run_${tag:?}" && mkdir -p "$W/r"
cp -a /var/lab/build/rootfs/. $W/r/
for f in $FILES; do cp $f $W/r/; done
cp $t $W/r/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m 512M \
  -nographic -no-reboot -dtb /var/tmp/H10/h10.dtb -kernel /var/lab/out/Image -initrd $W/i.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/t.sh" 2>&1 | tr -d '\r'
```

---

## 3. mailbox framework — controller(provider) 쪽

### 3.1 두 층으로 나눈 이유

mailbox framework는 G모듈에서 본 다른 서브시스템(I2C의 adapter/client, GPIO의 gpio_chip/consumer, clk의 provider/consumer)과 같은 모양이다.

- **controller 드라이버**: 하드웨어 블록 하나(MU, IPCC …)를 안다. "이 채널에 한 워드 보내기", "TX가 비었나 확인", "RX 인터럽트가 왔다"만 구현한다. 메시지 형식이나 프로토콜은 모른다.
- **client 드라이버**: 프로토콜을 안다(remoteproc의 "kick", SCMI 메시지, rpmsg vring 알림 …). 어느 하드웨어 위에서 도는지는 모른다. DT의 `mboxes`로 채널을 받기만 한다.
- **mailbox core**(`drivers/mailbox/mailbox.c`): 둘 사이에서 **채널 소유권**, **TX 큐(채널당 20칸)**, **TX 완료 판정**, **DT 해석**을 맡는다.

덕분에 같은 SCMI 드라이버가 Juno의 MHU 위에서도, i.MX95의 MU 위에서도 돈다. Don의 SSD 펌웨어로 치면 "IPC HAL(레지스터 접근)"과 "명령 프로토콜 계층"을 분리하고, 그 사이에 공용 큐 관리 코드를 둔 것과 같다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="15" y="15" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="90" y="38" text-anchor="middle" font-size="13">remoteproc/rpmsg</text>
  <text x="90" y="58" text-anchor="middle" font-size="12">kick · vring 알림</text>
  <rect x="180" y="15" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="38" text-anchor="middle" font-size="13">arm_scmi</text>
  <text x="255" y="58" text-anchor="middle" font-size="12">전원·클럭·센서 요청</text>
  <rect x="345" y="15" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="420" y="38" text-anchor="middle" font-size="13">qcom smd/glink</text>
  <text x="420" y="58" text-anchor="middle" font-size="12">모뎀·DSP 통신</text>
  <rect x="510" y="15" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="585" y="38" text-anchor="middle" font-size="13">h10_client</text>
  <text x="585" y="58" text-anchor="middle" font-size="12">이 노트의 client</text>
  <rect x="15" y="98" width="645" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="337" y="117" text-anchor="middle" font-size="12">client API: mbox_request_channel(_byname) · mbox_send_message</text>
  <text x="337" y="134" text-anchor="middle" font-size="12">mbox_client_txdone · mbox_client_peek_data · mbox_free_channel</text>
  <rect x="15" y="164" width="645" height="96" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="337" y="188" text-anchor="middle" font-size="13" font-weight="bold">mailbox core — drivers/mailbox/mailbox.c</text>
  <text x="337" y="210" text-anchor="middle" font-size="12">채널 소유권(한 채널 = client 하나) · TX 링버퍼 20칸 · active_req 1개</text>
  <text x="337" y="230" text-anchor="middle" font-size="12">TX 완료 판정: IRQ / POLL(hrtimer) / ACK(client)</text>
  <text x="337" y="250" text-anchor="middle" font-size="12">DT mboxes 해석(of_xlate) · RX는 버퍼링 없이 rx_callback 직행</text>
  <rect x="15" y="282" width="645" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="337" y="301" text-anchor="middle" font-size="12">controller API: mbox_controller_register · mbox_chan_received_data</text>
  <text x="337" y="318" text-anchor="middle" font-size="12">mbox_chan_txdone · ops: send_data/startup/shutdown/last_tx_done/peek_data</text>
  <rect x="15" y="348" width="150" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="90" y="371" text-anchor="middle" font-size="13">imx-mailbox</text>
  <text x="90" y="391" text-anchor="middle" font-size="12">i.MX MU</text>
  <rect x="180" y="348" width="150" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="255" y="371" text-anchor="middle" font-size="13">stm32-ipcc</text>
  <text x="255" y="391" text-anchor="middle" font-size="12">STM32MP1 IPCC</text>
  <rect x="345" y="348" width="150" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="420" y="371" text-anchor="middle" font-size="13">arm_mhu(_db)</text>
  <text x="420" y="391" text-anchor="middle" font-size="12">SCP 통로</text>
  <rect x="510" y="348" width="150" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="585" y="371" text-anchor="middle" font-size="13">h10_fakembox</text>
  <text x="585" y="391" text-anchor="middle" font-size="12">가짜 (hrtimer)</text>
  <line x1="337" y1="71" x2="337" y2="96" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="337" y1="142" x2="337" y2="162" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="337" y1="260" x2="337" y2="280" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="337" y1="326" x2="337" y2="346" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="337" y="424" text-anchor="middle" font-size="12">아래로: send_data 호출 — 위로: rx_callback · tx_done 콜백 (IRQ 컨텍스트)</text>
</svg>
```

그림 2 — mailbox framework의 층. client는 위, controller는 아래에만 의존하고, 가운데 core가 큐와 TX 완료 판정을 맡는다. 우리의 `h10_fakembox`는 진짜 controller 자리에 끼워 넣은 가짜다.

### 3.2 controller가 채우는 구조체

`include/linux/mailbox_controller.h`(랩 커널 6.18.55)에서 그대로 옮겼다.

```c
struct mbox_chan_ops {
	int (*send_data)(struct mbox_chan *chan, void *data);
	int (*flush)(struct mbox_chan *chan, unsigned long timeout);
	int (*startup)(struct mbox_chan *chan);
	void (*shutdown)(struct mbox_chan *chan);
	bool (*last_tx_done)(struct mbox_chan *chan);
	bool (*peek_data)(struct mbox_chan *chan);
};

struct mbox_controller {
	struct device *dev;
	const struct mbox_chan_ops *ops;
	struct mbox_chan *chans;
	int num_chans;
	bool txdone_irq;
	bool txdone_poll;
	unsigned txpoll_period;
	struct mbox_chan *(*fw_xlate)(struct mbox_controller *mbox,
				      const struct fwnode_reference_args *sp);
	struct mbox_chan *(*of_xlate)(struct mbox_controller *mbox,
				      const struct of_phandle_args *sp);
	/* Internal to API */
	struct hrtimer poll_hrt;
	spinlock_t poll_hrt_lock;
	struct list_head node;
};
```

| 필드 / op | 언제 불리나 | 컨텍스트 | 할 일 |
|---|---|---|---|
| `send_data` | core가 큐에서 메시지 하나를 꺼낼 때 | 채널 spinlock(irqsave) 안 — **잠들면 안 됨** | 데이터 레지스터 쓰기 + doorbell. 하드웨어가 아직 바쁘면 `-EBUSY` (core가 나중에 재시도) |
| `startup` | client가 채널을 요청할 때 (`mbox_request_channel`) | process, 잠들어도 됨 | 채널 IRQ 켜기, 클럭 켜기 |
| `shutdown` | `mbox_free_channel` | process | IRQ 끄기, 진행 중인 것 정리 |
| `last_tx_done` | POLL 모드에서 core의 hrtimer가 주기적으로 | hrtimer(hardirq) | "상대가 마지막 메시지를 가져갔나" 상태 비트 읽기 |
| `peek_data` | client가 `mbox_client_peek_data`로 | atomic 가능 | RX 레지스터에 와 있는 것이 있으면 지금 밀어 올리기 (선택) |
| `flush` | client가 `mbox_flush`로 | atomic 가능 | TX 완료까지 busy-wait (선택, atomic 컨텍스트 송신용) |
| `txdone_irq` | 등록 시 | — | "TX-empty 인터럽트가 있다" → 드라이버가 ISR에서 `mbox_chan_txdone()` |
| `txdone_poll` + `txpoll_period` | 등록 시 | — | "상태 비트를 폴링해 달라" → core가 hrtimer로 `last_tx_done` 호출 (ms 단위) |
| 둘 다 false | 등록 시 | — | ACK 모드: client가 `mbox_client_txdone()`으로 알려 준다 |
| `of_xlate` | `mbox_request_channel` 안에서 | process | DT specifier(`<&mbox 3 1>`의 숫자들) → `struct mbox_chan *` |

controller가 core에 "올려 보내는" 함수는 둘뿐이다.

- `mbox_chan_received_data(chan, mssg)`: RX 인터럽트에서 부른다. core는 **버퍼링하지 않고** 바로 client의 `rx_callback`을 부른다. 소스 주석 그대로 "The controller should ACK the RX only after this call returns." 즉 `mssg`가 가리키는 데이터는 이 호출이 끝나면 덮어써질 수 있다.
- `mbox_chan_txdone(chan, r)`: TX-empty 인터럽트에서 부른다. `txdone_irq`가 아닌 컨트롤러가 부르면 core가 `"Controller can't run the TX ticker"`를 찍고 무시한다.

### 3.3 `#mbox-cells`와 `of_xlate`

DT specifier의 숫자 개수와 의미는 **컨트롤러마다 다르다**. 그래서 해석을 컨트롤러에 맡긴다.

| 컨트롤러 | `#mbox-cells` | 셀의 의미 (랩 소스에서 확인) |
|---|---|---|
| `h10_fakembox` | 1 | 채널 번호 |
| STM32 IPCC | 1 | 채널 번호 |
| i.MX MU | 2 | (타입, 인덱스) — 타입: 0 TX, 1 RX, 2 TXDB, 3 RXDB, 4 RST, 5 TXDB_V2 (`enum imx_mu_chan_type`) |
| ARM MHU doorbell (Juno SCMI) | 2 | (채널, doorbell 비트) |
| TI OMAP/AM6x | 1 | 셀 대신 **서브노드 phandle**: 서브노드 안에 `ti,mbox-tx`/`ti,mbox-rx` = (FIFO 번호, IRQ 줄, 사용자 번호) |

`of_xlate`도 `fw_xlate`도 안 채우면 core가 `fw_mbox_index_xlate`(첫 셀을 채널 인덱스로 씀)를 기본으로 넣는다. 우리는 로그를 찍으려고 `of_xlate`를 직접 채웠다.

### 3.4 `h10_fakembox` — 가짜 컨트롤러 코드

먼저 두 모듈이 같이 쓰는 메시지 형식이다. `cmd`는 "메일박스 데이터 레지스터에 실리는 워드", `shm`/`len`은 "공유 SRAM에 놓인 본문"을 흉내 낸다.

```c
/* h10_msg.h — message format shared by h10_fakembox (provider) and h10_client */
#ifndef H10_MSG_H
#define H10_MSG_H
#include <linux/types.h>
#define H10_REPLY 0x80000000u   /* remote sets this bit in reply cmd */
#define H10_EVT   0x0000E000u   /* unsolicited event from remote */
struct h10_msg {
	u32 seq;        /* sequence number (to match replies) */
	u32 cmd;        /* command word = what a real MU "data register" carries */
	u32 len;        /* payload length in shared SRAM */
	const u8 *shm;  /* payload lives in "shared SRAM", not in the mailbox */
	u32 result;     /* reply: remote's checksum of the payload */
};
#endif
```

controller 본체 (1/3) — 모듈 파라미터와 "원격 코어" 에뮬레이션. `remote_fn`이 핵심이다: 원격 코어가 (1) 공유 SRAM의 본문을 읽고 (2) TX 레지스터를 비우고(TX-empty IRQ) (3) 응답을 RX 레지스터에 넣는(RX IRQ) 순서를 hrtimer 콜백 하나로 흉내 낸다.

```c
// h10_fakembox.c — FAKE mailbox controller. QEMU virt has no mailbox HW:
// the "remote core" is EMULATED by an hrtimer callback (hardirq context).
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mailbox_controller.h>
#include <linux/hrtimer.h>
#include <linux/of.h>
#include "h10_msg.h"

static char *txdone = "irq";     /* irq | poll | ack */
module_param(txdone, charp, 0444);
static uint delay_us = 50;       /* remote "processing" time per message */
module_param(delay_us, uint, 0444);
static uint dead_mask;           /* bit i set: remote never answers chan i */
module_param(dead_mask, uint, 0444);
static uint event_ms;            /* chan 1: unsolicited event period (0=off) */
module_param(event_ms, uint, 0444);

#define NCH 2
struct fch {
	struct mbox_chan *chan;
	struct hrtimer remote, event;   /* emulated remote CPU activity */
	struct h10_msg *req;            /* "TX data register" */
	struct h10_msg reply, ev;       /* "RX data register" */
	bool busy;                      /* TX reg full: remote hasn't read it */
	u32 evseq;
};
struct fmb {
	struct mbox_controller mbox;
	struct mbox_chan chans[NCH];
	struct fch fc[NCH];
};

static enum hrtimer_restart remote_fn(struct hrtimer *t)
{
	struct fch *c = container_of(t, struct fch, remote);
	struct h10_msg *m = c->req;
	u32 i, sum = 0;

	for (i = 0; i < m->len; i++)    /* remote reads payload from shared SRAM */
		sum += m->shm[i];
	c->reply = (struct h10_msg){ .seq = m->seq, .cmd = m->cmd | H10_REPLY,
				     .len = m->len, .result = sum };
	WRITE_ONCE(c->busy, false);          /* remote drained the TX register */
	if (c->chan->mbox->txdone_irq)
		mbox_chan_txdone(c->chan, 0);    /* = "TX empty" interrupt */
	mbox_chan_received_data(c->chan, &c->reply);  /* = "RX full" interrupt */
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart event_fn(struct hrtimer *t)
{
	struct fch *c = container_of(t, struct fch, event);

	c->ev = (struct h10_msg){ .seq = ++c->evseq, .cmd = H10_EVT };
	mbox_chan_received_data(c->chan, &c->ev);    /* doorbell from remote */
	hrtimer_forward_now(t, ms_to_ktime(event_ms));
	return HRTIMER_RESTART;
}
```

(2/3) — `mbox_chan_ops`. `send_data`는 "TX 레지스터에 쓰고 doorbell을 울리는" 자리다. 하드웨어가 아직 앞 메시지를 안 가져갔으면 `-EBUSY`를 돌려주는데, 이것은 진짜 드라이버(예: TX FIFO가 찬 TI mailbox)에서도 같은 약속이다.

```c
static int f_send(struct mbox_chan *chan, void *data)
{
	struct fch *c = chan->con_priv;
	int idx = chan - chan->mbox->chans;

	if (READ_ONCE(c->busy))
		return -EBUSY;              /* previous word not consumed yet */
	c->req = data;
	WRITE_ONCE(c->busy, true);      /* "write TX register + ring doorbell" */
	if (!(dead_mask & BIT(idx)))
		hrtimer_start(&c->remote, us_to_ktime(delay_us), HRTIMER_MODE_REL);
	return 0;
}

static bool f_last_tx_done(struct mbox_chan *chan)   /* used in poll mode */
{
	struct fch *c = chan->con_priv;

	return !READ_ONCE(c->busy);
}

static int f_startup(struct mbox_chan *chan)
{
	struct fch *c = chan->con_priv;
	int idx = chan - chan->mbox->chans;

	dev_info(chan->mbox->dev, "startup chan %d\n", idx);
	if (idx == 1 && event_ms)
		hrtimer_start(&c->event, ms_to_ktime(event_ms), HRTIMER_MODE_REL);
	return 0;
}

static void f_shutdown(struct mbox_chan *chan)
{
	struct fch *c = chan->con_priv;

	hrtimer_cancel(&c->event);
	hrtimer_cancel(&c->remote);
	WRITE_ONCE(c->busy, false);
	dev_info(chan->mbox->dev, "shutdown chan %d\n", (int)(chan - chan->mbox->chans));
}

static const struct mbox_chan_ops f_ops = {
	.send_data = f_send, .last_tx_done = f_last_tx_done,
	.startup = f_startup, .shutdown = f_shutdown,
};
```

(3/3) — `of_xlate`와 probe. `txdone` 파라미터로 세 가지 TX 완료 방식을 고른다.

```c
static struct mbox_chan *f_xlate(struct mbox_controller *mb,
				 const struct of_phandle_args *sp)
{
	if (sp->args_count != 1 || sp->args[0] >= mb->num_chans)
		return ERR_PTR(-EINVAL);
	dev_info(mb->dev, "of_xlate: %pOFn cell=%u -> chan %u\n",
		 sp->np, sp->args[0], sp->args[0]);
	return &mb->chans[sp->args[0]];
}

static int f_probe(struct platform_device *pdev)
{
	struct fmb *f = devm_kzalloc(&pdev->dev, sizeof(*f), GFP_KERNEL);
	int i;

	if (!f)
		return -ENOMEM;
	for (i = 0; i < NCH; i++) {
		f->fc[i].chan = &f->chans[i];
		f->chans[i].con_priv = &f->fc[i];
		hrtimer_setup(&f->fc[i].remote, remote_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		hrtimer_setup(&f->fc[i].event, event_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	}
	f->mbox.dev = &pdev->dev;
	f->mbox.ops = &f_ops;
	f->mbox.chans = f->chans;
	f->mbox.num_chans = NCH;
	f->mbox.of_xlate = f_xlate;
	if (!strcmp(txdone, "irq")) {
		f->mbox.txdone_irq = true;
	} else if (!strcmp(txdone, "poll")) {
		f->mbox.txdone_poll = true;
		f->mbox.txpoll_period = 1;          /* ms */
	}                                       /* else: ACK by client */
	dev_info(&pdev->dev, "%d chans, txdone=%s, remote=hrtimer EMULATION (%u us)\n",
		 NCH, txdone, delay_us);
	return devm_mbox_controller_register(&pdev->dev, &f->mbox);
}

static const struct of_device_id f_of[] = { { .compatible = "h10,fake-mbox" }, { } };
MODULE_DEVICE_TABLE(of, f_of);
static struct platform_driver f_drv = {
	.probe = f_probe,
	.driver = { .name = "h10_fakembox", .of_match_table = f_of },
};
module_platform_driver(f_drv);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H10 fake mailbox controller (remote = hrtimer emulation)");
```

코드에서 볼 것:

- `send_data`는 core가 채널 spinlock을 `irqsave`로 잡은 채 부른다(`msg_submit`). 그래서 `hrtimer_start`처럼 잠들지 않는 것만 쓴다. 진짜 드라이버라면 `writel()` 몇 번이다.
- `remote_fn`은 hrtimer 콜백이라 **hardirq 컨텍스트**다. 진짜 MU 드라이버도 `request_irq()`로 등록한 ISR(`imx_mu_isr`)에서 `mbox_chan_received_data()`와 `mbox_chan_txdone()`을 부른다(랩 소스 `imx-mailbox.c`에서 확인). 그래서 client 콜백이 도는 컨텍스트가 실물과 같다.
- `devm_mbox_controller_register()`를 썼기 때문에 `remove`가 없다. 모듈을 내리면 devres가 `mbox_controller_unregister()` → 모든 채널 `mbox_free_channel()` → 우리 `shutdown`(타이머 취소)을 부른다.

---

## 4. mailbox framework — client 쪽

### 4.1 `struct mbox_client` — client가 "나는 이렇게 쓰겠다"고 밝히는 구조체

```c
struct mbox_client {
	struct device *dev;
	bool tx_block;
	unsigned long tx_tout;
	bool knows_txdone;

	void (*rx_callback)(struct mbox_client *cl, void *mssg);
	void (*tx_prepare)(struct mbox_client *cl, void *mssg);
	void (*tx_done)(struct mbox_client *cl, void *mssg, int r);
};
```

| 필드 | 뜻 | 주의 |
|---|---|---|
| `dev` | DT에서 `mboxes`를 찾을 장치 | 이 장치의 `of_node`(fwnode)를 읽는다 |
| `tx_block` | true면 `mbox_send_message()`가 TX 완료(또는 타임아웃)까지 **잠든다** | `wait_for_completion_timeout` 사용 → atomic 컨텍스트에서 금지 |
| `tx_tout` | 블로킹 대기 한도(ms). 0이면 "사실상 무한"(소스상 3600000 ms) | 넘으면 `-ETIME`(-62) |
| `knows_txdone` | "TX 완료를 내가(프로토콜이) 안다" | POLL 컨트롤러일 때만 의미가 있다 → core가 그 채널을 ACK 모드로 바꾼다(§5) |
| `rx_callback` | 원격이 보낸 데이터 도착 | 컨트롤러 ISR 컨텍스트. **잠들기 금지** |
| `tx_prepare` | `send_data` 직전 | 공유 메모리에 본문을 쓰는 자리 (SCMI가 사용) |
| `tx_done` | TX 완료 또는 실패 통지 (`r`=0 또는 에러) | 대개 atomic. 타임아웃이면 `r=-ETIME`으로 process 컨텍스트에서 불린다 |

### 4.2 client API

| 함수 | 컨텍스트 | 하는 일 |
|---|---|---|
| `mbox_request_channel(cl, index)` | process (잠들 수 있음) | `mboxes`의 index번째 specifier → `of_xlate` → 채널을 이 client에 **독점** 할당 → `startup` |
| `mbox_request_channel_byname(cl, name)` | process | `mbox-names`에서 이름 → index → 위와 같음 |
| `mbox_send_message(chan, mssg)` | 블로킹이면 process, 아니면 atomic도 가능 | 큐에 넣고 `send_data` 시도. 반환값: **0 이상의 토큰**(큐 슬롯 번호) 또는 음수 에러 |
| `mbox_client_txdone(chan, r)` | atomic 가능 | ACK 모드에서 "방금 그 메시지 완료" 통지 |
| `mbox_client_peek_data(chan)` | atomic 가능 | 컨트롤러에 "RX 와 있으면 지금 올려" 요청 |
| `mbox_flush(chan, timeout)` | atomic 가능 | 컨트롤러가 `flush` op를 구현했을 때만 |
| `mbox_free_channel(chan)` | process | `shutdown` 후 소유권 반납. **큐에 남은 메시지는 콜백 없이 버린다**(소스 주석) |

중요한 약속 하나: `mbox_send_message()`에 넘긴 `mssg` 포인터는 **`tx_done`이 불릴 때까지 살아 있어야 한다**. core는 포인터만 큐에 넣고 나중에 `send_data`로 넘기기 때문이다. 스택 변수를 넘기고 함수를 빠져나가면 use-after-free다. 우리 client가 `static struct h10_msg msgs[32]`를 쓰는 이유다.

### 4.3 `h10_client` 코드

(1/5) — 파라미터와 상태. 채널이 둘이라 `mbox_client`도 둘이다(`cl`은 `ctrl` 채널, `cl_ev`는 `event` 채널). `rx_callback`이 "어느 채널에서 왔는지"를 인자로 받지 않으므로, 채널마다 client 구조체를 따로 두고 `container_of`로 구분하는 것이 관례다.

```c
// h10_client.c — mailbox CLIENT: request channels by name, send, rx_callback
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mailbox_client.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/sort.h>
#include <linux/slab.h>
#include "h10_msg.h"

static char *test = "basic";     /* basic | nonblock | late | bench */
module_param(test, charp, 0444);
static bool knows;               /* client says "I know when TX is done" */
module_param(knows, bool, 0444);
static uint tout = 100;          /* tx_tout in ms (blocking mode) */
static uint nmsg = 3;            /* messages in basic/late test */
module_param(nmsg, uint, 0444);
module_param(tout, uint, 0444);

struct h10c {
	struct device *dev;
	struct mbox_client cl, cl_ev;
	struct mbox_chan *ctrl, *evt;
	struct completion rx;
	ktime_t t_rx;
	u32 rx_seq, rx_res, n_ev, n_txdone;
	bool verbose;
};
static struct h10_msg msgs[32];          /* must live until tx_done */
static u8 shm[16384];                    /* "shared SRAM" payload area */

#define CTX() !!in_hardirq(), !!in_serving_softirq(), !!in_task()
```

(2/5) — 콜백 세 개. `CTX()`로 지금 hardirq인지, softirq인지, task인지 찍는다(F2).

```c
static void rx_cb(struct mbox_client *cl, void *m)
{
	struct h10c *h = container_of(cl, struct h10c, cl);
	struct h10_msg *r = m;

	h->t_rx = ktime_get();
	if (knows)
		mbox_client_txdone(h->ctrl, 0);  /* the reply IS our TX ack */
	if (h->verbose)
		dev_info(h->dev, "rx_callback seq=%u cmd=%#x res=%u [hardirq=%d softirq=%d task=%d]\n",
			 r->seq, r->cmd, r->result, CTX());
	WRITE_ONCE(h->rx_seq, r->seq);
	h->rx_res = r->result;
	complete(&h->rx);
}

static void ev_cb(struct mbox_client *cl, void *m)
{
	struct h10c *h = container_of(cl, struct h10c, cl_ev);
	struct h10_msg *r = m;

	h->n_ev++;
	if (h->n_ev <= 2)
		dev_info(h->dev, "event seq=%u cmd=%#x [hardirq=%d]\n", r->seq, r->cmd, !!in_hardirq());
}

static void txdone_cb(struct mbox_client *cl, void *m, int r)
{
	struct h10c *h = container_of(cl, struct h10c, cl);

	h->n_txdone++;
	if (h->verbose)
		dev_info(h->dev, "tx_done seq=%u r=%d [hardirq=%d softirq=%d task=%d]\n",
			 ((struct h10_msg *)m)->seq, r, CTX());
}
```

(3/5) — 블로킹 송신 테스트(`basic`)와 "타임아웃 직후 또 보내기" 테스트(`late`).

```c
static void t_basic(struct h10c *h, int n)
{
	int i, ret;
	ktime_t t0;

	for (i = 1; i <= n; i++) {
		msgs[i] = (struct h10_msg){ .seq = i, .cmd = 0x10, .len = 4, .shm = shm };
		reinit_completion(&h->rx);
		t0 = ktime_get();
		ret = mbox_send_message(h->ctrl, &msgs[i]);
		dev_info(h->dev, "send seq=%d ret=%d after %lld us\n", i, ret,
			 ktime_us_delta(ktime_get(), t0));
		if (!wait_for_completion_timeout(&h->rx, msecs_to_jiffies(100)))
			dev_info(h->dev, "seq=%d: no reply within 100 ms\n", i);
		else
			dev_info(h->dev, "reply seq=%u rtt=%lld us\n", h->rx_seq,
				 ktime_us_delta(h->t_rx, t0));
	}
}

static void t_late(struct h10c *h, int n)      /* send again right after -ETIME */
{
	int i, ret;
	ktime_t t0 = ktime_get();

	for (i = 1; i <= n; i++) {
		msgs[i] = (struct h10_msg){ .seq = i, .cmd = 0x10, .shm = shm };
		ret = mbox_send_message(h->ctrl, &msgs[i]);
		dev_info(h->dev, "t=%lld ms: send seq=%d ret=%d\n", ktime_ms_delta(ktime_get(), t0), i, ret);
	}
	msleep(200);
	dev_info(h->dev, "t=%lld ms: last rx seq=%u\n", ktime_ms_delta(ktime_get(), t0), h->rx_seq);
}
```

(4/5) — 논블로킹 연속 송신(`nonblock`)과 지연 측정(`bench`).

```c
static void t_nonblock(struct h10c *h)
{
	int i, ret, ok = 0;

	for (i = 0; i < 25; i++) {
		msgs[i] = (struct h10_msg){ .seq = 100 + i, .cmd = 0x20, .shm = shm };
		ret = mbox_send_message(h->ctrl, &msgs[i]);
		if (ret >= 0)
			ok++;
		if (ret < 0 || i < 3 || i == 19)
			dev_info(h->dev, "nonblock send #%d -> token %d\n", i, ret);
	}
	msleep(300);
	dev_info(h->dev, "queued=%d tx_done calls=%u last rx seq=%u\n",
		 ok, h->n_txdone, h->rx_seq);
}

static int cmp64(const void *a, const void *b)
{
	u64 x = *(const u64 *)a, y = *(const u64 *)b;

	return x < y ? -1 : x > y;
}

static void t_bench(struct h10c *h)
{
	static const u32 lens[] = { 0, 256, 1024, 4096, 16384 };
	u64 *tx = kcalloc(200, sizeof(u64), GFP_KERNEL), *rt = kcalloc(200, sizeof(u64), GFP_KERNEL);
	int i, k;
	ktime_t t0;

	if (!tx || !rt)
		goto out;
	for (k = 0; k < ARRAY_SIZE(lens); k++) {
		for (i = 0; i < 200; i++) {
			memset(shm, i, lens[k]);              /* writer fills shared SRAM */
			msgs[0] = (struct h10_msg){ .seq = i, .cmd = 0x30, .len = lens[k], .shm = shm };
			reinit_completion(&h->rx);
			t0 = ktime_get();
			mbox_send_message(h->ctrl, &msgs[0]);  /* blocking: returns at txdone */
			tx[i] = ktime_to_ns(ktime_sub(ktime_get(), t0));
			wait_for_completion(&h->rx);
			rt[i] = ktime_to_ns(ktime_sub(h->t_rx, t0));
		}
		sort(tx, 200, sizeof(u64), cmp64, NULL);
		sort(rt, 200, sizeof(u64), cmp64, NULL);
		dev_info(h->dev, "bench len=%5u n=200 txdone med=%llu us p90=%llu | rtt med=%llu us p90=%llu\n",
			 lens[k], tx[100] / 1000, tx[180] / 1000, rt[100] / 1000, rt[180] / 1000);
	}
out:
	kfree(tx);
	kfree(rt);
}
```

(5/5) — probe에서 이름으로 채널 두 개를 받고 테스트를 돌린다. `remove`에서 반드시 `mbox_free_channel()`.

```c
static int c_probe(struct platform_device *pdev)
{
	struct h10c *h = devm_kzalloc(&pdev->dev, sizeof(*h), GFP_KERNEL);

	if (!h)
		return -ENOMEM;
	h->dev = &pdev->dev;
	init_completion(&h->rx);
	h->verbose = strcmp(test, "bench") && strcmp(test, "nonblock");
	h->cl = (struct mbox_client){ .dev = &pdev->dev, .rx_callback = rx_cb,
		.tx_done = txdone_cb, .tx_block = strcmp(test, "nonblock") != 0,
		.tx_tout = tout, .knows_txdone = knows };
	h->cl_ev = (struct mbox_client){ .dev = &pdev->dev, .rx_callback = ev_cb };
	h->ctrl = mbox_request_channel_byname(&h->cl, "ctrl");
	if (IS_ERR(h->ctrl))
		return dev_err_probe(&pdev->dev, PTR_ERR(h->ctrl), "ctrl chan\n");
	h->evt = mbox_request_channel_byname(&h->cl_ev, "event");
	if (IS_ERR(h->evt)) {
		mbox_free_channel(h->ctrl);
		return dev_err_probe(&pdev->dev, PTR_ERR(h->evt), "event chan\n");
	}
	platform_set_drvdata(pdev, h);
	dev_info(h->dev, "test=%s tx_block=%d tx_tout=%u knows_txdone=%d\n",
		 test, h->cl.tx_block, tout, knows);
	if (!strcmp(test, "basic"))
		t_basic(h, nmsg);
	else if (!strcmp(test, "late"))
		t_late(h, nmsg);
	else if (!strcmp(test, "nonblock"))
		t_nonblock(h);
	else if (!strcmp(test, "bench"))
		t_bench(h);
	msleep(50);
	dev_info(h->dev, "events received on 'event' chan: %u\n", h->n_ev);
	return 0;
}

static void c_remove(struct platform_device *pdev)
{
	struct h10c *h = platform_get_drvdata(pdev);

	mbox_free_channel(h->evt);
	mbox_free_channel(h->ctrl);
}

static const struct of_device_id c_of[] = { { .compatible = "h10,mbox-client" }, { } };
MODULE_DEVICE_TABLE(of, c_of);
static struct platform_driver c_drv = {
	.probe = c_probe, .remove = c_remove,
	.driver = { .name = "h10_client", .of_match_table = c_of },
};
module_platform_driver(c_drv);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H10 mailbox client test");
```

### 4.4 예제 3 — 게스트에서 설정·DT·바인딩 확인

무엇을 확인하나: 랩 커널에 mailbox/hwspinlock/SCMI/remoteproc가 켜져 있는지, 어떤 실물 드라이버가 built-in으로 등록돼 있는지, 우리가 붙인 DT 속성이 게스트에서 어떻게 보이는지.

```sh
zcat /proc/config.gz | grep -E "^CONFIG_(MAILBOX|HWSPINLOCK|ARM_MHU|IMX_MBOX|PCC|ARM_SCMI_PROTOCOL|ARM_SCMI_TRANSPORT_MAILBOX|ARM_SCMI_TRANSPORT_SMC|REMOTEPROC|RPMSG_VIRTIO|HWSPINLOCK_QCOM)="
echo "--- built-in mailbox/hwspinlock/scmi/rproc drivers:"
ls /sys/bus/platform/drivers | grep -iE "mbox|mailbox|mhu|hwspin|scmi|rproc|ipcc" | tr '\n' ' '; echo
echo "--- scmi in boot log:"; dmesg | grep -ic scmi
insmod /h10_fakembox.ko
insmod /h10_client.ko test=basic nmsg=0
cd /sys/firmware/devicetree/base
echo "--- DT: h10-mailbox"; hexdump -C h10-mailbox/#mbox-cells; hexdump -C h10-mailbox/phandle
echo "--- DT: h10-mbox-client"; tr '\0' ' ' < h10-mbox-client/mbox-names; echo; hexdump -C h10-mbox-client/mboxes
echo "--- bound drivers:"; ls -l /sys/bus/platform/devices/h10-mailbox/driver /sys/bus/platform/devices/h10-mbox-client/driver | sed 's/.* h10/h10/'
```

```text
lab initramfs: 6.18.55 up 1.63s
CONFIG_ARM_SCMI_PROTOCOL=y
CONFIG_ARM_SCMI_TRANSPORT_MAILBOX=y
CONFIG_ARM_SCMI_TRANSPORT_SMC=y
CONFIG_HWSPINLOCK=y
CONFIG_HWSPINLOCK_QCOM=y
CONFIG_MAILBOX=y
CONFIG_ARM_MHU=y
CONFIG_IMX_MBOX=y
CONFIG_PCC=y
CONFIG_REMOTEPROC=y
CONFIG_RPMSG_VIRTIO=y
--- built-in mailbox/hwspinlock/scmi/rproc drivers:
arm-scmi bcm2835-mbox bcm74110-mbox cix_mbox hi3660-mbox hi6220-mbox imx-rproc platform-mhu qcom-ipcc qcom_hwspinlock rproc-virtio scmi_mailbox_transport scmi_smc_transport 
--- scmi in boot log:
1
--- DT: h10-mailbox
00000000  00 00 00 01                                       |....|
00000004
00000000  00 00 00 01                                       |....|
00000004
--- DT: h10-mbox-client
ctrl event 
00000000  00 00 00 01 00 00 00 00  00 00 00 01 00 00 00 01  |................|
00000010
--- bound drivers:
lrwxrwxrwx    1 0        0                0 Oct 11 03:53 /sys/bus/platform/devices/h10-mailbox/driver -> ../../../bus/platform/drivers/h10_fakembox
lrwxrwxrwx    1 0        0                0 Oct 11 03:53 /sys/bus/platform/devices/h10-mbox-client/driver -> ../../../bus/platform/drivers/h10_client
[    2.442238] reboot: Power down
```

출력에서 볼 것:

- `CONFIG_MAILBOX=y`, `CONFIG_HWSPINLOCK=y`와 함께 실물 컨트롤러(`IMX_MBOX`, `ARM_MHU`, `PCC`), SCMI의 mailbox 전송(`ARM_SCMI_TRANSPORT_MAILBOX`), remoteproc·rpmsg가 다 built-in이다. arm64 defconfig 기반이라 여러 SoC 드라이버가 한꺼번에 들어 있다.
- platform 드라이버 목록에 `qcom-ipcc`, `platform-mhu`, `qcom_hwspinlock`, `arm-scmi`, `scmi_mailbox_transport`, `imx-rproc`가 보인다. 하지만 QEMU virt DT에 맞는 노드가 없으니 **probe된 장치는 없다**. 부팅 로그의 SCMI 관련 줄은 1개뿐인데(예제 12에서 내용 확인), "SCMI 버스 등록"일 뿐 실제 SCMI 펌웨어와 통신한 것이 아니다.
- `mboxes`의 원시 바이트 `00 00 00 01 00 00 00 00 00 00 00 01 00 00 00 01` = `<phandle 1, 셀 0>, <phandle 1, 셀 1>`. 그리고 `h10-mailbox/phandle`도 1이다. 커널은 이 phandle로 컨트롤러 노드를 찾고, 우리 `of_xlate`에 셀 값을 넘긴다.
- `/sys/bus/platform/drivers`에 `imx_mu`가 안 보이는 것은 grep 패턴(`mbox|mailbox|…`)에 안 걸려서다. 다른 부팅에서 `imx|stm32`로 찾으면 `imx_mu`, `imx-rproc`가 있다(예제 12 출력).

### 4.5 예제 4 — 블로킹 송신, 응답, 원격의 비동기 이벤트

무엇을 확인하나: `txdone=irq` 컨트롤러 위에서 블로킹 송신 3번과, `event` 채널로 원격이 먼저 보내는(unsolicited) 메시지를 받는다. 콜백이 도는 컨텍스트도 찍는다.

```sh
uname -r
insmod /h10_fakembox.ko txdone=irq delay_us=50 event_ms=20
insmod /h10_client.ko test=basic
dmesg | grep h10 | sed 's/^\[ *[0-9.]*\] //'
```

```text
lab initramfs: 6.18.55 up 0.67s
6.18.55
h10_fakembox: loading out-of-tree module taints kernel.
h10_fakembox h10-mailbox: 2 chans, txdone=irq, remote=hrtimer EMULATION (50 us)
h10_fakembox h10-mailbox: of_xlate: h10-mailbox cell=0 -> chan 0
h10_fakembox h10-mailbox: startup chan 0
h10_fakembox h10-mailbox: of_xlate: h10-mailbox cell=1 -> chan 1
h10_fakembox h10-mailbox: startup chan 1
h10_client h10-mbox-client: test=basic tx_block=1 tx_tout=100 knows_txdone=0
h10_client h10-mbox-client: tx_done seq=1 r=0 [hardirq=1 softirq=0 task=0]
h10_client h10-mbox-client: rx_callback seq=1 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
h10_client h10-mbox-client: send seq=1 ret=0 after 664 us
h10_client h10-mbox-client: reply seq=1 rtt=532 us
h10_client h10-mbox-client: tx_done seq=2 r=0 [hardirq=1 softirq=0 task=0]
h10_client h10-mbox-client: rx_callback seq=2 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
h10_client h10-mbox-client: send seq=2 ret=1 after 166 us
h10_client h10-mbox-client: reply seq=2 rtt=131 us
h10_client h10-mbox-client: tx_done seq=3 r=0 [hardirq=1 softirq=0 task=0]
h10_client h10-mbox-client: rx_callback seq=3 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
h10_client h10-mbox-client: send seq=3 ret=2 after 154 us
h10_client h10-mbox-client: reply seq=3 rtt=125 us
h10_client h10-mbox-client: event seq=1 cmd=0xe000 [hardirq=1]
h10_client h10-mbox-client: event seq=2 cmd=0xe000 [hardirq=1]
h10_client h10-mbox-client: events received on 'event' chan: 2
[    0.891559] reboot: Power down
```

출력에서 볼 것:

- **채널 요청 순서**: `mbox_request_channel_byname("ctrl")` → `mbox-names`에서 index 0 → `mboxes`의 0번 specifier → 우리 `of_xlate`(cell=0) → `startup`. 이름이 DT에서 숫자로, 숫자가 채널로 바뀌는 과정이 로그에 그대로 찍혔다.
- **블로킹 송신의 반환값은 0이 아니라 토큰**이다: `ret=0, 1, 2`. 큐 슬롯 번호가 돌아가며 나온다. "성공이면 0"이라고 가정하고 `if (ret)`로 검사하면 두 번째 메시지부터 실패로 오인한다. 올바른 검사는 `if (ret < 0)`. SCMI mailbox 전송 소스에도 "mbox_send_message returns non-negative value on success"라는 주석이 붙어 있다.
- **콜백 컨텍스트**: `tx_done`, `rx_callback`, `event` 전부 `hardirq=1`. 컨트롤러가 IRQ(여기서는 hrtimer)에서 core를 부르기 때문이다. 그래서 `rx_callback` 안에서 `mutex_lock`, `kmalloc(GFP_KERNEL)`, `msleep`, I2C 전송 같은 잠드는 일은 금지다. 무거운 처리는 workqueue나 threaded IRQ로 넘긴다(F7).
- **순서**: `tx_done`(원격이 TX 레지스터를 비움) → `rx_callback`(원격의 응답) → 그 뒤에야 잠들어 있던 송신자가 깨어나 `send ... after N us`를 찍는다. 그래서 `send`의 소요 시간(첫 메시지 664 us)이 `rtt`(532 us)보다 길다: 깨어나는 데 걸린 시간과 콜백 안의 `dev_info` 출력 시간이 더해진다. TCG에서 콘솔 출력은 특히 비싸다.
- `res=0`: 본문 길이 4바이트가 전부 0이라 체크섬도 0이다. 원격이 `shm`을 실제로 읽는지는 §7 bench에서 길이를 바꿔 본다.
- `event_ms=20`이라 원격이 20 ms마다 `event` 채널로 먼저 말을 건다. Linux가 아무것도 보내지 않았는데 `rx_callback`이 불린다. 실물에서는 "M코어가 IMU 샘플 버퍼 반쯤 찼다", "모터 과전류" 같은 통지다. 메일박스는 **양방향**이고, 채널을 열어 두는 것만으로 수신 준비가 끝난다.

---

## 5. TX 완료 판정 3가지 — irq · poll · ack

### 5.1 왜 "TX 완료"가 중요한가

메일박스 데이터 레지스터는 보통 깊이 1(또는 작은 FIFO)이다. 앞 메시지를 원격이 안 가져갔는데 또 쓰면 덮어쓴다. 그래서 core는 **채널마다 "지금 하드웨어에 올라가 있는 메시지"(`active_req`)를 딱 하나만** 허용하고, 그것이 끝났다는 신호가 와야 큐의 다음 것을 `send_data`로 넘긴다. 그 신호를 받는 방법이 세 가지다.

| 모드 | 컨트롤러 설정 | 누가 "완료"를 알리나 | 실물 예 (랩 소스 확인) |
|---|---|---|---|
| IRQ | `txdone_irq = true` | 컨트롤러 ISR이 `mbox_chan_txdone()` (TX-empty 인터럽트) | `imx-mailbox.c`, `stm32-ipcc.c`, `omap-mailbox.c` |
| POLL | `txdone_poll = true`, `txpoll_period` ms | core의 hrtimer가 `last_tx_done()`을 주기적으로 호출 | `arm_mhu.c`, `arm_mhu_db.c` |
| ACK | 둘 다 false | client가 `mbox_client_txdone()` (예: 응답을 받은 시점) | `qcom-ipcc.c`, `qcom-apcs-ipc-mailbox.c`(플래그 안 켬) → qcom smd/glink/smp2p가 보낸 직후 `mbox_client_txdone(chan, 0)` |

그리고 client의 `knows_txdone`이 끼어든다. core의 `__mbox_bind_client()`에 이 한 줄이 있다.

```c
		if (chan->txdone_method	== TXDONE_BY_POLL && cl->knows_txdone)
			chan->txdone_method = TXDONE_BY_ACK;
```

즉 `knows_txdone`은 **POLL 컨트롤러를 ACK로 바꾸는 스위치**일 뿐이다. IRQ 컨트롤러에서는 아무 효과가 없고, 그 상태에서 client가 `mbox_client_txdone()`을 부르면 core가 거부한다. SCMI가 이 스위치를 쓴다: SCMI 응답은 공유 메모리 채널 상태로 완료를 알 수 있으므로, MHU(POLL) 위에서 폴링을 끄고 응답 수신 시점에 직접 tick한다(`cl->knows_txdone = tx`).

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="15" y="26" font-size="13" font-weight="bold">IRQ 모드</text>
  <line x1="140" y1="70" x2="660" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="170" y1="58" x2="170" y2="82" stroke="#4a7bd0" stroke-width="3"/>
  <text x="170" y="50" text-anchor="middle" font-size="12">send_data</text>
  <line x1="320" y1="58" x2="320" y2="82" stroke="#3f9a6b" stroke-width="3"/>
  <text x="320" y="50" text-anchor="middle" font-size="12">TX-empty IRQ</text>
  <text x="320" y="98" text-anchor="middle" font-size="12">mbox_chan_txdone → tx_done</text>
  <line x1="460" y1="58" x2="460" y2="82" stroke="#e08a3c" stroke-width="3"/>
  <text x="460" y="50" text-anchor="middle" font-size="12">RX IRQ</text>
  <text x="460" y="98" text-anchor="middle" font-size="12">rx_callback</text>
  <text x="15" y="146" font-size="13" font-weight="bold">POLL 모드</text>
  <line x1="140" y1="190" x2="660" y2="190" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="170" y1="178" x2="170" y2="202" stroke="#4a7bd0" stroke-width="3"/>
  <text x="170" y="170" text-anchor="middle" font-size="12">send_data</text>
  <circle cx="190" cy="190" r="5" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <text x="200" y="218" text-anchor="middle" font-size="12">poll: 아직</text>
  <line x1="460" y1="178" x2="460" y2="202" stroke="#e08a3c" stroke-width="3"/>
  <text x="460" y="170" text-anchor="middle" font-size="12">RX IRQ</text>
  <text x="460" y="218" text-anchor="middle" font-size="12">rx_callback</text>
  <line x1="590" y1="178" x2="590" y2="202" stroke="#3f9a6b" stroke-width="3"/>
  <text x="590" y="170" text-anchor="middle" font-size="12">poll: 완료</text>
  <text x="590" y="218" text-anchor="middle" font-size="12">tx_done</text>
  <line x1="200" y1="234" x2="580" y2="234" stroke="#888" stroke-dasharray="4 3" marker-end="url(#ah3)" marker-start="url(#ah3)"/>
  <text x="390" y="250" text-anchor="middle" font-size="12">txpoll_period (우리 설정 1 ms)만큼 늦게 안다</text>
  <text x="15" y="290" font-size="13" font-weight="bold">ACK 모드</text>
  <line x1="140" y1="334" x2="660" y2="334" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="170" y1="322" x2="170" y2="346" stroke="#4a7bd0" stroke-width="3"/>
  <text x="170" y="314" text-anchor="middle" font-size="12">send_data</text>
  <line x1="460" y1="322" x2="460" y2="346" stroke="#e08a3c" stroke-width="3"/>
  <text x="460" y="314" text-anchor="middle" font-size="12">RX IRQ → rx_callback</text>
  <text x="460" y="362" text-anchor="middle" font-size="12">안에서 mbox_client_txdone</text>
  <text x="460" y="380" text-anchor="middle" font-size="12">→ tx_done (응답 = 완료)</text>
</svg>
```

그림 3 — 같은 메시지 하나의 타임라인을 세 모드로 본 것. IRQ 모드는 원격이 레지스터를 비우는 순간, POLL 모드는 그 다음 폴링 시점, ACK 모드는 client가 "응답을 받았으니 끝"이라고 말하는 순간에 TX가 끝난다. 이 시점이 블로킹 `mbox_send_message()`가 돌아오는 시점이다.

### 5.2 예제 5 — 3 모드 × `knows_txdone` 0/1 매트릭스

무엇을 확인하나: 같은 client로 컨트롤러 모드만 바꿔 가며, `tx_done`과 `rx_callback`의 순서, 블로킹 송신의 반환값과 소요 시간을 비교한다.

```sh
for m in irq poll ack; do for k in 0 1; do
  echo "=== txdone=$m knows_txdone=$k"
  dmesg -c > /dev/null
  insmod /h10_fakembox.ko txdone=$m delay_us=50
  insmod /h10_client.ko test=basic nmsg=1 knows=$k tout=100
  dmesg | grep -E "h10_client|mailbox" | grep -v -E "test=|of_xlate|startup|shutdown|chans,|events received" | sed 's/^\[ *[0-9.]*\] //; s/h10_client h10-mbox-client: //'
  rmmod h10_client; rmmod h10_fakembox
done; done
```

```text
lab initramfs: 6.18.55 up 1.27s
=== txdone=irq knows_txdone=0
tx_done seq=1 r=0 [hardirq=1 softirq=0 task=0]
rx_callback seq=1 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
send seq=1 ret=0 after 4962 us
reply seq=1 rtt=815 us
=== txdone=irq knows_txdone=1
[    1.801373] h10_fakembox h10-mailbox: Client can't run the TX ticker
tx_done seq=1 r=0 [hardirq=1 softirq=0 task=0]
h10_fakembox h10-mailbox: Client can't run the TX ticker
rx_callback seq=1 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
send seq=1 ret=0 after 760 us
reply seq=1 rtt=553 us
=== txdone=poll knows_txdone=0
rx_callback seq=1 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
tx_done seq=1 r=0 [hardirq=1 softirq=0 task=0]
send seq=1 ret=0 after 2251 us
reply seq=1 rtt=269 us
=== txdone=poll knows_txdone=1
tx_done seq=1 r=0 [hardirq=1 softirq=0 task=0]
rx_callback seq=1 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
send seq=1 ret=0 after 609 us
reply seq=1 rtt=287 us
=== txdone=ack knows_txdone=0
rx_callback seq=1 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
tx_done seq=1 r=-62 [hardirq=0 softirq=0 task=1]
send seq=1 ret=-62 after 103658 us
reply seq=1 rtt=228 us
=== txdone=ack knows_txdone=1
tx_done seq=1 r=0 [hardirq=1 softirq=0 task=0]
rx_callback seq=1 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
send seq=1 ret=0 after 742 us
reply seq=1 rtt=437 us
[    3.248175] reboot: Power down
```

출력에서 볼 것 (줄마다 해석):

| 경우 | 관찰 | 이유 |
|---|---|---|
| irq, knows=0 | `tx_done` → `rx_callback` → 반환 `ret=0` | 정상. TX-empty IRQ가 먼저 오고 응답이 뒤따른다 |
| irq, knows=1 | `Client can't run the TX ticker` 에러가 찍힘. 그래도 정상 동작 | `knows_txdone`은 IRQ 채널을 ACK로 바꾸지 않는다. client의 `mbox_client_txdone()`은 거부되고, TX 완료는 여전히 IRQ가 알린다. 첫 에러 줄은 `dev_err`라 `quiet` 부팅에서도 콘솔에 바로 찍혔다(타임스탬프 붙은 줄) |
| poll, knows=0 | `rx_callback`이 **먼저**, `tx_done`이 나중 (hardirq) | 원격은 50 us 만에 응답했지만, core의 폴링 hrtimer가 다음 주기에 `last_tx_done()`을 보고서야 완료를 안다. 송신 반환이 2251 us |
| poll, knows=1 | `tx_done` → `rx_callback`, 반환 609 us | core가 채널을 ACK로 바꿨다. 우리 `rx_cb`가 첫 줄에서 `mbox_client_txdone()`을 부르므로 `tx_done`이 `rx_callback`의 로그보다 먼저 찍힌다 |
| ack, knows=0 | `tx_done ... r=-62 [task=1]`, `ret=-62 after 103658 us` | 아무도 tick하지 않는다. 응답(`rx_callback`)은 왔지만 core는 그것이 완료라는 것을 모른다. `tx_tout`=100 ms 후 `-ETIME`. 이때 `tx_done`은 process 컨텍스트(`task=1`)에서 불린다 |
| ack, knows=1 | 정상, `tx_done`이 hardirq | client가 응답 수신 시점에 tick |

마지막에서 두 번째 줄이 실무에서 가장 흔한 버그 패턴이다. "ACK 모드 컨트롤러(Qualcomm 계열)인데 client가 `mbox_client_txdone()`을 안 부름" → 첫 메시지는 나가지만(큐가 비어 있었으니까) **두 번째부터는 큐에서 영원히 대기**하고, 블로킹이면 매번 타임아웃이다.

---

## 6. 실패할 때 — 큐 넘침, 늦은 원격, 죽은 원격

정상 경로는 쉽다. 면접과 실무에서 차이가 나는 것은 **원격 코어가 느리거나 죽었을 때 Linux 쪽에서 무슨 일이 일어나는가**다. M코어 펌웨어가 HardFault로 멈추는 일은 실제로 흔하다.

이 절의 세 실험은 한 번의 게스트 부팅에서 차례로 돌렸다.

```sh
F='s/^\[ *[0-9.]*\] //; s/h10_client h10-mbox-client: //'
echo "=== A) non-blocking, 25 messages back-to-back (txdone=irq, remote 2 ms)"
dmesg -c > /dev/null
insmod /h10_fakembox.ko txdone=irq delay_us=2000
insmod /h10_client.ko test=nonblock
dmesg | grep -E "h10_client|Try|h10-mailbox: [^ost]" | grep -v -E "test=|chans,|of_xlate" | sed "$F"
rmmod h10_client; rmmod h10_fakembox
echo "=== B) remote slower (30 ms) than tx_tout (10 ms)"
dmesg -c > /dev/null
insmod /h10_fakembox.ko txdone=irq delay_us=30000
insmod /h10_client.ko test=late tout=10 nmsg=4
dmesg | grep h10_client | grep -v -E "test=|events received" | sed "$F"
rmmod h10_client; rmmod h10_fakembox
echo "=== C) remote dead on chan 0"
dmesg -c > /dev/null
insmod /h10_fakembox.ko txdone=irq dead_mask=1
insmod /h10_client.ko test=late tout=10 nmsg=2
dmesg | grep h10_client | grep -v -E "test=|events received" | sed "$F"
```

### 6.1 예제 6 — 논블로킹 연속 송신: 큐는 20칸

```text
lab initramfs: 6.18.55 up 1.94s
=== A) non-blocking, 25 messages back-to-back (txdone=irq, remote 2 ms)
[    2.462894] h10_fakembox h10-mailbox: Try increasing MBOX_TX_QUEUE_LEN
[    2.462930] h10_fakembox h10-mailbox: Try increasing MBOX_TX_QUEUE_LEN
[    2.462943] h10_fakembox h10-mailbox: Try increasing MBOX_TX_QUEUE_LEN
[    2.462956] h10_fakembox h10-mailbox: Try increasing MBOX_TX_QUEUE_LEN
nonblock send #0 -> token 0
nonblock send #1 -> token 1
nonblock send #2 -> token 2
nonblock send #19 -> token 19
h10_fakembox h10-mailbox: Try increasing MBOX_TX_QUEUE_LEN
nonblock send #21 -> token -105
h10_fakembox h10-mailbox: Try increasing MBOX_TX_QUEUE_LEN
nonblock send #22 -> token -105
h10_fakembox h10-mailbox: Try increasing MBOX_TX_QUEUE_LEN
nonblock send #23 -> token -105
h10_fakembox h10-mailbox: Try increasing MBOX_TX_QUEUE_LEN
nonblock send #24 -> token -105
queued=21 tx_done calls=21 last rx seq=120
events received on 'event' chan: 0
```

출력에서 볼 것:

- 원격이 메시지 하나에 2 ms 걸리는 동안 25개를 몰아 보냈다. 처음 것은 바로 하드웨어로 나가고(`active_req`), 나머지는 core의 채널 링버퍼(`MBOX_TX_QUEUE_LEN` = 20)에 쌓인다. 그래서 1 + 20 = **21개까지 받아들이고**(`queued=21`), 22번째부터(`#21`) `-105`(`-ENOBUFS`)와 `Try increasing MBOX_TX_QUEUE_LEN` 에러가 난다. `#20`은 성공이라 출력 조건에 안 걸려 안 찍혔다.
- `Try increasing ...` 줄이 위쪽에 타임스탬프를 달고 4번, 아래 dmesg에 다시 4번 나온다. 위쪽은 `dev_err`가 콘솔로 바로 나간 것이고 아래는 같은 메시지를 dmesg로 다시 읽은 것이다.
- 이 상수는 **헤더의 `#define`이라 바꾸려면 커널을 다시 빌드해야 한다**. 에러 메시지가 권하는 대로 늘리기보다, client가 흐름 제어(`tx_done` 콜백에서 다음 것 보내기, 또는 공유 메모리 링으로 묶어 doorbell 한 번)를 하는 것이 정답이다. rpmsg가 메시지마다 메일박스를 쓰지 않고 vring에 쌓은 뒤 "kick" 한 번만 보내는 이유다(§9).
- 논블로킹 모드의 반환값도 토큰(0, 1, 2 … 19)이다.

### 6.2 예제 7 — 원격이 `tx_tout`보다 느릴 때: 프레임워크와 원격의 상태가 어긋난다

```text
=== B) remote slower (30 ms) than tx_tout (10 ms)
tx_done seq=1 r=-62 [hardirq=0 softirq=0 task=1]
t=14 ms: send seq=1 ret=-62
rx_callback seq=1 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
tx_done seq=2 r=-62 [hardirq=0 softirq=0 task=1]
t=31 ms: send seq=2 ret=-62
t=47 ms: send seq=3 ret=-62
rx_callback seq=2 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
tx_done seq=3 r=-62 [hardirq=0 softirq=0 task=1]
t=62 ms: send seq=4 ret=-62
rx_callback seq=3 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
tx_done seq=4 r=0 [hardirq=1 softirq=0 task=0]
rx_callback seq=4 cmd=0x80000010 res=0 [hardirq=1 softirq=0 task=0]
t=266 ms: last rx seq=4
```

이 로그는 처음 보면 이상하다. 원격 처리 시간 30 ms(우리 파라미터)로 각 사건의 시각을 다시 맞춰 보면 이렇다. `t=` 값은 측정한 것이고, 원격이 끝난 시각(30, 60, 90, 120 ms 근처)은 `delay_us=30000`에서 **추정**한 것이다.

| 시각(ms) | 일어난 일 | 로그 |
|---|---|---|
| 0 | seq1 → `send_data` 성공, 원격 처리 시작 (30 ms 걸림) | — |
| 14 | 10 ms 대기 초과 → core가 `tx_tick(-ETIME)`: `active_req`를 비우고 seq1을 실패 처리 | `tx_done seq=1 r=-62`, `send seq=1 ret=-62` |
| 14 | seq2 송신 → `send_data`가 `-EBUSY`(원격이 아직 seq1 처리 중) → **큐에 남음** | — |
| 약 30 | 원격이 seq1을 끝냄 → TX-empty IRQ → core: `active_req`가 비어 있으니 tx_done은 없음, 대신 큐의 seq2를 하드웨어로 보냄. seq1의 응답 도착 | `rx_callback seq=1` |
| 31 | seq2의 10 ms 대기 초과 → `tx_tick(-ETIME)`: 하드웨어에 **실제로 올라가 있는** seq2를 실패로 보고 | `tx_done seq=2 r=-62`, `send seq=2 ret=-62` |
| 31~47 | seq3 송신 → `-EBUSY`로 큐에 남음 → 타임아웃. 이번엔 `active_req`가 비어 있어 **tx_done 없이** `-ETIME`만 반환 | `send seq=3 ret=-62` |
| 약 60 | 원격이 seq2(실패로 보고된 그 메시지!)를 처리하고 응답 → core는 큐의 seq3을 하드웨어로 | `rx_callback seq=2` |
| 62 | seq4의 타임아웃 → `tx_tick(-ETIME)`이 하드웨어에 있던 **seq3**을 실패 처리 | `tx_done seq=3 r=-62`, `send seq=4 ret=-62` |
| 약 90 | 원격이 seq3 응답, core가 seq4를 하드웨어로 | `rx_callback seq=3` |
| 약 120 | 원격이 seq4 완료 → `tx_done seq=4 r=0` — 송신자는 이미 `-ETIME`을 받고 떠났다 | `tx_done seq=4 r=0`, `rx_callback seq=4` |

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="15" y="62" font-size="13">원격 코어</text>
  <text x="15" y="80" font-size="12">(실제 처리)</text>
  <rect x="100" y="48" width="129" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="164" y="68" text-anchor="middle" font-size="12">seq1</text>
  <rect x="229" y="48" width="129" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="293" y="68" text-anchor="middle" font-size="12">seq2</text>
  <rect x="358" y="48" width="129" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="422" y="68" text-anchor="middle" font-size="12">seq3</text>
  <rect x="487" y="48" width="129" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="551" y="68" text-anchor="middle" font-size="12">seq4</text>
  <text x="15" y="152" font-size="13">client가</text>
  <text x="15" y="170" font-size="12">받은 반환값</text>
  <line x1="160" y1="140" x2="160" y2="170" stroke="#d0564a" stroke-width="3"/>
  <text x="160" y="190" text-anchor="middle" font-size="12">seq1 -ETIME</text>
  <line x1="233" y1="140" x2="233" y2="170" stroke="#d0564a" stroke-width="3"/>
  <text x="250" y="132" text-anchor="middle" font-size="12">seq2 -ETIME</text>
  <line x1="302" y1="140" x2="302" y2="170" stroke="#d0564a" stroke-width="3"/>
  <text x="318" y="190" text-anchor="middle" font-size="12">seq3 -ETIME</text>
  <line x1="367" y1="140" x2="367" y2="170" stroke="#d0564a" stroke-width="3"/>
  <text x="380" y="132" text-anchor="middle" font-size="12">seq4 -ETIME</text>
  <line x1="100" y1="210" x2="650" y2="210" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="100" y="228" text-anchor="middle" font-size="12">0</text>
  <text x="229" y="228" text-anchor="middle" font-size="12">30</text>
  <text x="358" y="228" text-anchor="middle" font-size="12">60</text>
  <text x="487" y="228" text-anchor="middle" font-size="12">90</text>
  <text x="616" y="228" text-anchor="middle" font-size="12">120 ms</text>
  <text x="340" y="262" text-anchor="middle" font-size="12">client는 4개 모두 실패로 알지만, 원격은 4개를 전부 처리했다</text>
  <text x="340" y="282" text-anchor="middle" font-size="12">(원격 막대 위치는 delay 30 ms에서 추정 · 반환 시각은 측정값)</text>
</svg>
```

그림 4 — 늦은 원격과 짧은 `tx_tout`. client 쪽 결과(모두 `-ETIME`)와 원격 쪽 사실(모두 처리됨)이 완전히 어긋난다. 게다가 `tx_done`이 엉뚱한 메시지 번호로 불리고(seq4 타임아웃 때 seq3의 `tx_done`), 마지막 `tx_done seq=4 r=0`은 아무도 기다리지 않는 completion을 채워 둔다.

이 실험의 교훈은 프레임워크 사용법이 아니라 **프로토콜 설계**다.

- `-ETIME`은 "전달 실패"가 아니라 **"모름"** 이다. 원격은 그 명령을 이미 실행했을 수 있다. 모터 "정지" 명령이면 괜찮지만, "+10도 회전"처럼 **멱등(idempotent)이 아닌 명령을 재전송하면 두 번 실행된다**.
- 그래서 실무 프로토콜은 메시지에 **sequence 번호**를 넣고, 응답을 seq로 맞춰 보며, 늦게 온 응답은 버린다. 우리 `rx_callback`은 seq를 찍기만 했지만, 실제 드라이버는 "지금 기다리는 seq가 아니면 무시"해야 한다.
- 커널의 SCMI mailbox 전송도 같은 문제를 겪었다. `rx_callback`에서 공유 메모리 채널이 아직 "플랫폼 소유"이면 그 IRQ를 무시하는 코드가 있고, 주석이 "a spurious IRQ from a previous timed-out reply which arrived late could be wrongly associated with the next pending transaction"이라고 설명한다(`drivers/firmware/arm_scmi/transports/mailbox.c`, 랩 소스에서 확인).
- `tx_tout`은 원격의 **최악** 처리 시간보다 넉넉하게 잡는다. 타임아웃이 났다면 채널을 "오염됨"으로 보고 재동기화(채널 free 후 재요청, 원격 리셋 — remoteproc의 recovery) 절차를 밟는다.

또 하나 미묘한 점: 마지막 `tx_done seq=4 r=0`에서 core의 `tx_tick()`은 `tx_block`이 켜져 있으니 `complete(&chan->tx_complete)`를 부른다. 기다리는 사람이 없으니 completion의 카운트만 1 올라간다. 다음 블로킹 송신은 `wait_for_completion_timeout()`이 이 **묵은 완료를 즉시 소비하고** 자기 메시지가 끝나기 전에 돌아올 수 있다(소스 흐름으로 추론한 것이고, 이 노트에서 따로 재현하지는 않았다). 타임아웃 뒤 채널을 다시 쓰기 전에 free/request로 새로 여는 것이 안전한 이유다.

### 6.3 예제 8 — 원격이 죽었을 때

```text
=== C) remote dead on chan 0
tx_done seq=1 r=-62 [hardirq=0 softirq=0 task=1]
t=15 ms: send seq=1 ret=-62
t=28 ms: send seq=2 ret=-62
t=242 ms: last rx seq=0
[    4.225616] reboot: Power down
```

출력에서 볼 것: seq1은 `tx_done ... r=-62`와 함께 실패한다. seq2는 `send_data`가 `-EBUSY`(원격이 TX 레지스터를 영영 안 비움)라 큐에 남은 채로 타임아웃이 나고, **`tx_done` 콜백조차 없다**. 이후 이 채널의 모든 메시지가 큐 안에서 썩는다. `mbox_free_channel()`은 큐를 콜백 없이 버린다. 원격 코어가 죽은 것을 감지하는 것은 메일박스의 일이 아니다. remoteproc의 watchdog/crash 통지(O1)나 상위 프로토콜의 heartbeat가 할 일이다.

---

## 7. 측정 — 왕복 지연 vs payload, txdone 모드별

### 7.1 예제 9 — bench

무엇을 확인하나: 블로킹 송신이 돌아오기까지(txdone)와 응답 콜백까지(rtt)를 payload 크기 5가지 × 모드 3가지로 200번씩 재고 중앙값과 90퍼센타일을 본다. 원격 처리 시간은 20 us로 두고, 원격은 payload 전체의 체크섬을 계산한다("공유 SRAM 읽기" 흉내).

```sh
for m in irq poll ack; do
  k=0; [ $m = ack ] && k=1
  dmesg -c > /dev/null
  insmod /h10_fakembox.ko txdone=$m delay_us=20
  insmod /h10_client.ko test=bench knows=$k
  echo "--- txdone=$m knows_txdone=$k"
  dmesg | grep "bench len" | sed 's/^\[ *[0-9.]*\] //; s/h10_client h10-mbox-client: //'
  rmmod h10_client; rmmod h10_fakembox
done
```

```text
lab initramfs: 6.18.55 up 2.19s
--- txdone=irq knows_txdone=0
bench len=    0 n=200 txdone med=75 us p90=125 | rtt med=53 us p90=86
bench len=  256 n=200 txdone med=61 us p90=117 | rtt med=47 us p90=73
bench len= 1024 n=200 txdone med=59 us p90=73 | rtt med=47 us p90=58
bench len= 4096 n=200 txdone med=98 us p90=163 | rtt med=68 us p90=109
bench len=16384 n=200 txdone med=156 us p90=272 | rtt med=108 us p90=166
--- txdone=poll knows_txdone=0
bench len=    0 n=200 txdone med=1086 us p90=1323 | rtt med=56 us p90=189
bench len=  256 n=200 txdone med=1113 us p90=1367 | rtt med=61 us p90=171
bench len= 1024 n=200 txdone med=1087 us p90=1461 | rtt med=54 us p90=155
bench len= 4096 n=200 txdone med=1085 us p90=1308 | rtt med=63 us p90=166
bench len=16384 n=200 txdone med=1082 us p90=1261 | rtt med=81 us p90=220
--- txdone=ack knows_txdone=1
bench len=    0 n=200 txdone med=86 us p90=170 | rtt med=53 us p90=94
bench len=  256 n=200 txdone med=58 us p90=102 | rtt med=41 us p90=58
bench len= 1024 n=200 txdone med=86 us p90=156 | rtt med=53 us p90=89
bench len= 4096 n=200 txdone med=62 us p90=71 | rtt med=47 us p90=51
bench len=16384 n=200 txdone med=86 us p90=95 | rtt med=71 us p90=78
[    4.853991] reboot: Power down
```

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
  <line x1="70" y1="290.0" x2="640" y2="290.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="294.0" text-anchor="end" font-size="12">0</text>
  <line x1="70" y1="255.7" x2="640" y2="255.7" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="259.7" text-anchor="end" font-size="12">200</text>
  <line x1="70" y1="221.4" x2="640" y2="221.4" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="225.4" text-anchor="end" font-size="12">400</text>
  <line x1="70" y1="187.1" x2="640" y2="187.1" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="191.1" text-anchor="end" font-size="12">600</text>
  <line x1="70" y1="152.9" x2="640" y2="152.9" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="156.9" text-anchor="end" font-size="12">800</text>
  <line x1="70" y1="118.6" x2="640" y2="118.6" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="122.6" text-anchor="end" font-size="12">1000</text>
  <line x1="70" y1="84.3" x2="640" y2="84.3" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="88.3" text-anchor="end" font-size="12">1200</text>
  <line x1="70" y1="50.0" x2="640" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="54.0" text-anchor="end" font-size="12">1400</text>
  <line x1="70" y1="290" x2="640" y2="290" stroke="currentColor"/>
  <line x1="70" y1="50" x2="70" y2="290" stroke="currentColor"/>
  <rect x="84.0" y="277.1" width="26" height="12.9" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="97.0" y="273.1" text-anchor="middle" font-size="11">75</text>
  <rect x="114.0" y="103.8" width="26" height="186.2" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="127.0" y="99.8" text-anchor="middle" font-size="11">1086</text>
  <rect x="144.0" y="275.3" width="26" height="14.7" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="157.0" y="271.3" text-anchor="middle" font-size="11">86</text>
  <text x="127.0" y="308" text-anchor="middle" font-size="12">0 B</text>
  <rect x="198.0" y="279.5" width="26" height="10.5" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="211.0" y="275.5" text-anchor="middle" font-size="11">61</text>
  <rect x="228.0" y="99.2" width="26" height="190.8" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="241.0" y="95.2" text-anchor="middle" font-size="11">1113</text>
  <rect x="258.0" y="280.1" width="26" height="9.9" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="271.0" y="276.1" text-anchor="middle" font-size="11">58</text>
  <text x="241.0" y="308" text-anchor="middle" font-size="12">256 B</text>
  <rect x="312.0" y="279.9" width="26" height="10.1" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="325.0" y="275.9" text-anchor="middle" font-size="11">59</text>
  <rect x="342.0" y="103.7" width="26" height="186.3" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="355.0" y="99.7" text-anchor="middle" font-size="11">1087</text>
  <rect x="372.0" y="275.3" width="26" height="14.7" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="385.0" y="271.3" text-anchor="middle" font-size="11">86</text>
  <text x="355.0" y="308" text-anchor="middle" font-size="12">1K B</text>
  <rect x="426.0" y="273.2" width="26" height="16.8" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="439.0" y="269.2" text-anchor="middle" font-size="11">98</text>
  <rect x="456.0" y="104.0" width="26" height="186.0" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="469.0" y="100.0" text-anchor="middle" font-size="11">1085</text>
  <rect x="486.0" y="279.4" width="26" height="10.6" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="499.0" y="275.4" text-anchor="middle" font-size="11">62</text>
  <text x="469.0" y="308" text-anchor="middle" font-size="12">4K B</text>
  <rect x="540.0" y="263.3" width="26" height="26.7" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="553.0" y="259.3" text-anchor="middle" font-size="11">156</text>
  <rect x="570.0" y="104.5" width="26" height="185.5" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="583.0" y="100.5" text-anchor="middle" font-size="11">1082</text>
  <rect x="600.0" y="275.3" width="26" height="14.7" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="613.0" y="271.3" text-anchor="middle" font-size="11">86</text>
  <text x="583.0" y="308" text-anchor="middle" font-size="12">16K B</text>
  <text x="355" y="330" text-anchor="middle" font-size="12">payload (shared SRAM)</text>
  <text x="18" y="170" text-anchor="middle" font-size="12" transform="rotate(-90 18 170)">send → return (us, median)</text>
  <rect x="90" y="12" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="108" y="22" font-size="12">txdone_irq</text>
  <rect x="260" y="12" width="12" height="12" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="278" y="22" font-size="12">txdone_poll (1 ms)</text>
  <rect x="460" y="12" width="12" height="12" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="478" y="22" font-size="12">ACK (knows_txdone)</text>
</svg>
```

그림 5 — 블로킹 `mbox_send_message()`가 돌아오기까지 걸린 시간(중앙값, us). 실측(QEMU TCG, 원격은 hrtimer 에뮬레이션, 각 막대 n=200). POLL 모드만 1 ms 근처에 붙어 있다.

출력에서 볼 것:

- **POLL 모드는 `txpoll_period`가 지배한다**. 원격은 20 us면 끝나는데 송신자는 약 1.08 ms 뒤에 돌아온다. 그 사이 응답(rtt 54~81 us)은 이미 와 있다. core가 송신 직후 hrtimer를 0으로 한 번 걷어차지만(`msg_submit`의 "kick start the timer immediately") 그때는 원격이 아직 안 끝났고, 다음 확인이 1 ms 뒤다. ARM MHU 드라이버들이 POLL을 쓰는 이유는 MHU에 TX 완료 인터럽트가 없기 때문인데(기억, 하드웨어 매뉴얼로 확인할 것), 그래서 SCMI가 `knows_txdone`으로 폴링을 우회한다.
- **IRQ와 ACK는 비슷하다**(중앙값 60~90 us). 이 숫자의 대부분은 hrtimer 프로그래밍·인터럽트·송신자 깨우기(잠든 task를 깨워 다시 스케줄)의 TCG 비용이다. txdone이 rtt보다 20~50 us 긴 것이 그 "깨우기" 비용이다.
- **payload가 커지면 rtt가 는다**(IRQ: 47 → 108 us, 0 B → 16 KB). 원격이 16 KB를 바이트 단위로 읽는 비용이 TCG에서 약 60 us다. 실물이라면 M코어가 비캐시 SRAM이나 DDR을 읽는 속도에 달렸다. 요점은 **메일박스 자체는 크기와 무관하고(워드 하나), 비용은 공유 메모리 쪽에 있다**는 것이다.
- 절대값은 믿지 말 것. 실리콘의 메일박스 왕복은 보통 마이크로초 단위다(기억; 칩마다 다르다). 여기서 믿을 것은 "POLL은 주기만큼 손해", "블로킹 송신에는 깨우기 비용이 붙는다", "본문 크기 비용은 메일박스가 아니라 메모리 쪽"이라는 세 가지 경향이다.

---

## 8. hwspinlock — 서로 다른 OS 사이의 락

### 8.1 직관: 화장실 열쇠가 벽에 걸린 사무실 두 곳

Linux의 `spinlock_t`는 "우리 사무실(Cortex-A 클러스터) 안의 규칙"이다. 같은 캐시 일관성 도메인 안의 CPU끼리는 이 규칙이 완벽하게 통한다. 그런데 옆 사무실(Cortex-M)은 그 규칙을 모른다. 같은 화장실(공유 SRAM의 데이터)을 쓰려면 **두 사무실 모두가 볼 수 있는 복도 벽에 열쇠 하나(하드웨어 락 레지스터)** 를 걸어 두고 "열쇠를 가진 쪽만 들어간다"고 약속해야 한다.

정확히 말하면 일반 spinlock이 코어 사이에서 실패하는 이유는 세 가지다(§1.1 복습).

| 이유 | 무엇이 깨지나 |
|---|---|
| exclusive monitor / 원자 명령이 coherency 도메인 안에서만 의미가 있음 | M코어의 STREX "성공"과 A코어의 STLXR "성공"이 동시에 날 수 있다 |
| 캐시 도메인이 다름 | A코어가 캐시에 쓴 "잠김"이 M코어에 안 보인다 |
| 락 자료구조의 형식 | RTOS가 qspinlock 형식을 모른다. 애초에 각 OS는 자기 락만 본다 |

hwspinlock 레지스터는 Device 메모리(비캐시, 버스가 순서 보장)에 있고, **한 번의 읽기 또는 쓰기+읽기**로 소유권이 결정되므로 이 셋을 모두 피한다.

### 8.2 Linux hwspinlock API

hwspinlock core는 하드웨어 락 위에 **로컬 spinlock을 하나 더** 씌운다. 하드웨어 락은 "다른 코어(다른 OS)"를 막고, 로컬 spinlock은 "같은 Linux 안의 다른 CPU, 선점, 인터럽트"를 막는다. `__hwspin_trylock()`(랩 소스 `drivers/hwspinlock/hwspinlock_core.c`)의 뼈대는 이렇다.

```c
	switch (mode) {
	case HWLOCK_IRQSTATE:
		ret = spin_trylock_irqsave(&hwlock->lock, *flags);
		break;
	case HWLOCK_IRQ:
		ret = spin_trylock_irq(&hwlock->lock);
		break;
	case HWLOCK_RAW:
	case HWLOCK_IN_ATOMIC:
		ret = 1;
		break;
	default:
		ret = spin_trylock(&hwlock->lock);
		break;
	}

	/* is lock already taken by another context on the local cpu ? */
	if (!ret)
		return -EBUSY;

	/* try to take the hwspinlock device */
	ret = hwlock->bank->ops->trylock(hwlock);
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="300" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="45" text-anchor="middle" font-size="13">Linux CPU0 · CPU1 · IRQ · 선점</text>
  <text x="170" y="66" text-anchor="middle" font-size="12">hwspin_lock_timeout(hl, ms)</text>
  <rect x="360" y="20" width="300" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="45" text-anchor="middle" font-size="13">Cortex-M (RTOS)</text>
  <text x="510" y="66" text-anchor="middle" font-size="12">벤더 HAL의 HSEM/spinlock 함수</text>
  <rect x="20" y="120" width="300" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="145" text-anchor="middle" font-size="13" font-weight="bold">1단: 로컬 spinlock_t</text>
  <text x="170" y="166" text-anchor="middle" font-size="12">Linux 안의 경쟁자만 막는다</text>
  <text x="170" y="182" text-anchor="middle" font-size="12">mode: 기본 · _irq · _irqsave · (_raw 없음)</text>
  <rect x="190" y="230" width="300" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="255" text-anchor="middle" font-size="13" font-weight="bold">2단: 하드웨어 락 레지스터</text>
  <text x="340" y="276" text-anchor="middle" font-size="12">ops-&gt;trylock: 버스 접근 한 번으로 판정</text>
  <text x="340" y="292" text-anchor="middle" font-size="12">모든 코어·모든 OS가 공유</text>
  <line x1="170" y1="80" x2="170" y2="118" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="220" y1="190" x2="280" y2="228" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="80" x2="410" y2="228" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="560" y="160" text-anchor="middle" font-size="12">RTOS는 1단 없이</text>
  <text x="560" y="178" text-anchor="middle" font-size="12">바로 레지스터로</text>
</svg>
```

그림 6 — hwspinlock의 2단 구조. Linux 쪽은 로컬 spinlock으로 "Linux 안"을 먼저 정리한 뒤 하드웨어 레지스터로 "Linux 밖"과 경쟁한다. `_raw`/`_in_atomic` 모드는 1단을 건너뛰므로 Linux 안의 직렬화를 호출자가 책임진다.

| API | 1단(로컬) | 기다리는 방식 | 쓰는 곳 |
|---|---|---|---|
| `hwspin_lock_timeout(hl, ms)` | `spin_trylock` (선점 꺼짐) | jiffies 기준 timeout까지 **busy-wait** (잠들지 않음) | process 컨텍스트 공유 데이터 |
| `hwspin_lock_timeout_irq` | `spin_trylock_irq` | 같음 | IRQ와도 공유할 때 (IRQ가 켜져 있던 게 확실할 때) |
| `hwspin_lock_timeout_irqsave(hl, ms, &flags)` | `spin_trylock_irqsave` | 같음 | 가장 안전한 기본값. qcom smem이 사용 |
| `hwspin_lock_timeout_raw` | 없음 | 같음 | 호출자가 이미 다른 락으로 직렬화 (예: `sc27xx_adc.c`) — 안에서 잠드는 일도 가능해진다 |
| `hwspin_lock_timeout_in_atomic` | 없음 | jiffies 대신 `udelay(100)` 누적으로 timeout 계산 | jiffies가 안 흐를 수 있는 atomic 컨텍스트 (예: `irq-stm32mp-exti.c`) |
| `hwspin_trylock*` | 위와 같음 | 한 번만 시도 | 실패 시 다른 일 하기 |
| `hwspin_unlock*` | 짝 맞춰 해제 | — | lock과 같은 접미사 |

획득·해제 쪽 API:

| API | 뜻 |
|---|---|
| `of_hwspin_lock_get_id(np, index)` | DT `hwlocks`의 index번째 → 전역 lock id (provider가 아직 없으면 `-EPROBE_DEFER`) |
| `of_hwspin_lock_get_id_byname(np, name)` | `hwlock-names`로 찾기 |
| `hwspin_lock_request_specific(id)` / `devm_` 판 | 그 id의 락을 독점 요청. provider 모듈 참조(`try_module_get`)와 runtime PM을 잡는다 |
| `hwspin_lock_free(hl)` | 반납 |
| `hwspin_lock_bust(hl, id)` | 다른 코어(id)가 잡은 채 죽은 락을 강제로 푼다 — provider가 `bust` op를 구현했을 때만 (remoteproc 복구용) |

알아 둘 것 두 가지.

- 예전 커널에는 "아무 빈 락이나 달라"는 `hwspin_lock_request()`가 있었는데, **랩 커널 6.18의 헤더에는 `_specific`만 있다**. 락 번호는 원격 펌웨어와 미리 합의한 것이어야 하므로 "아무거나"는 의미가 없었다. 오래된 BSP 코드를 볼 때 주의.
- 모든 timeout 변형이 **잠들지 않는다**. 기본 모드도 로컬 spinlock을 잡은 채(선점 꺼짐) 하드웨어 락을 돌며 기다린다. 원격 코어가 락을 수 ms 잡으면 그동안 Linux CPU 하나가 선점 불가로 묶인다. 그래서 공유 임계 구역은 **짧게**(레지스터 몇 개 갱신, 링 인덱스 갱신) 설계한다.

### 8.3 provider 쪽 — `hwspinlock_ops`와 실물 드라이버

```c
struct hwspinlock_ops {
	int (*trylock)(struct hwspinlock *lock);
	void (*unlock)(struct hwspinlock *lock);
	int (*bust)(struct hwspinlock *lock, unsigned int id);
	void (*relax)(struct hwspinlock *lock);
};
```

`trylock`은 성공이면 1, 실패면 0, `relax`는 재시도 사이 쉬는 방법이다. 실물 세 가지를 랩 소스에서 그대로 옮긴다. 하드웨어 원자성의 세 가지 구현이 다 보인다.

```c
/* TI OMAP/AM6x: read-to-acquire — 읽었을 때 0(NOTTAKEN)이면 내 것 */
static int omap_hwspinlock_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	/* attempt to acquire the lock by reading its value */
	return (SPINLOCK_NOTTAKEN == readl(lock_addr));
}

/* STM32 HSEM: write my core ID with LOCK bit, read back and compare */
static int stm32_hwspinlock_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;
	u32 status;

	writel(STM32_MUTEX_LOCK_BIT | STM32_MUTEX_COREID, lock_addr);
	status = readl(lock_addr);

	return status == (STM32_MUTEX_LOCK_BIT | STM32_MUTEX_COREID);
}

/* Qualcomm TCSR mutex: same idea through a regmap field */
static int qcom_hwspinlock_trylock(struct hwspinlock *lock)
{
	struct regmap_field *field = lock->priv;
	u32 lock_owner;
	int ret;

	ret = regmap_field_write(field, QCOM_MUTEX_APPS_PROC_ID);
	if (ret)
		return ret;

	ret = regmap_field_read(field, &lock_owner);
	if (ret)
		return ret;

	return lock_owner == QCOM_MUTEX_APPS_PROC_ID;
}
```

STM32의 `relax`는 `ndelay(50)`이다. "write my ID then read back" 방식은 레지스터가 **다른 코어의 ID가 들어 있으면 내 쓰기를 무시**하도록 하드웨어가 만들어져 있어서 성립한다(이 동작은 STM32MP1 RM의 HSEM 설명 기억 — 소스는 비교만 한다). Don이 SSD 컨트롤러에서 쓴 HW 세마포어도 이 둘 중 하나였을 것이다.

### 8.4 `h10_hwlock` — 공유 워드 위의 가짜 provider와 경쟁 실험

실험 설계:

- **가짜 레지스터**: `struct shared`의 `lockw[4]`가 락 4개. `trylock` = `cmpxchg(word, 0, OWN_LINUX) == 0`. 진짜 하드웨어의 "버스 접근 한 번" 대신 Cortex-A57의 원자 명령을 쓴다(그래서 **에뮬레이션**이다).
- **원격 코어**: CPU0에 고정한 kthread `h10_remote`. RTOS 코어처럼 같은 워드에 직접 `cmpxchg(word, 0, OWN_REMOTE)`를 한다(Linux API를 거치지 않음). 잡으면 `a == b` 불변식을 검사하고 `hold_us` 동안 쥔다. 그다음 공유 "레지스터" `reg`의 상위 16비트를 read-modify-write로 1 올린다.
- **Linux 쪽**: probe를 돌리는 insmod 프로세스를 CPU1로 옮기고 300 ms 동안 반복: 락 → `a++`, 3 us, `b++` → 해제. 그리고 regmap으로 `reg`의 bit0을 4번 토글(`regmap_update_bits`).
- `mode=hw`: 양쪽이 같은 하드웨어 락 워드를 쓴다. `mode=spin`: Linux는 자기 `spinlock_t`만, 원격은 자기 RTOS 락(`rlock`)만 쓴다 — "**각자 자기 OS의 락만 잡는**" 잘못된 설계를 일부러 재현한다.

(1/4) — 파라미터, 공유 메모리, provider ops와 등록.

```c
// h10_hwlock.c — FAKE hwspinlock provider over a shared memory word (cmpxchg)
// + a test client. The "remote core" is EMULATED by a kthread pinned to CPU0.
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hwspinlock.h>
#include <linux/regmap.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include "hwspinlock_internal.h"   /* copied from drivers/hwspinlock/ */

static char *mode = "hw";        /* hw: both sides use the hw lock word
				    spin: Linux uses spinlock_t, remote uses its own lock */
module_param(mode, charp, 0444);
static uint hold_us = 20;        /* remote critical-section length */
module_param(hold_us, uint, 0444);
static uint to_ms = 10;          /* Linux hwspin_lock_timeout() timeout */
module_param(to_ms, uint, 0444);

#define NLOCK 4
#define OWN_LINUX 0x1u
#define OWN_REMOTE 0x2u
struct shared {                  /* emulated shared SRAM (both "cores" see it) */
	u32 lockw[NLOCK];            /* hw lock words */
	u32 rlock;                   /* remote's private RTOS lock (spin mode) */
	u32 a, b;                    /* invariant: a == b outside critical sections */
	u32 reg;                     /* a "register" shared with the co-processor */
};
static struct shared sh;
static bool use_hw;

static int f_trylock(struct hwspinlock *l)
{
	return cmpxchg((u32 *)l->priv, 0, OWN_LINUX) == 0;   /* full barrier */
}
static void f_unlock(struct hwspinlock *l)
{
	smp_store_release((u32 *)l->priv, 0);
}
static void f_relax(struct hwspinlock *l)
{
	ndelay(50);
}
static const struct hwspinlock_ops f_ops = {
	.trylock = f_trylock, .unlock = f_unlock, .relax = f_relax,
};

static int p_probe(struct platform_device *pdev)
{
	struct hwspinlock_device *bank;
	int i;

	bank = devm_kzalloc(&pdev->dev, struct_size(bank, lock, NLOCK), GFP_KERNEL);
	if (!bank)
		return -ENOMEM;
	for (i = 0; i < NLOCK; i++)
		bank->lock[i].priv = &sh.lockw[i];
	dev_info(&pdev->dev, "registering %d locks (shared-word EMULATION)\n", NLOCK);
	return devm_hwspin_lock_register(&pdev->dev, bank, &f_ops, 0, NLOCK);
}
```

(2/4) — 원격 코어 에뮬레이션과 regmap 접근 함수. regmap 쪽 "버스"는 접근마다 2 us 걸리게 했다(I2C/SPI나 느린 APB 레지스터 흉내). RMW 창을 넓혀 경쟁이 잘 보이게 하려는 것이다.

```c
/* ---- emulated remote core: kthread on CPU0 ---- */
static u32 r_acq, r_busy, r_torn, r_inc;
static bool r_lock(u32 *w)
{
	return cmpxchg(w, 0, OWN_REMOTE) == 0;
}
static int remote_fn(void *arg)
{
	u32 *w = use_hw ? &sh.lockw[0] : &sh.rlock;   /* which lock remote obeys */
	u32 *wr = use_hw ? &sh.lockw[1] : &sh.rlock;
	u32 v;

	while (!kthread_should_stop()) {
		if (r_lock(w)) {
			if (READ_ONCE(sh.a) != READ_ONCE(sh.b))
				r_torn++;                 /* saw Linux mid-update */
			udelay(hold_us);
			smp_store_release(w, 0);
			r_acq++;
		} else {
			r_busy++;
		}
		if (r_lock(wr)) {                     /* RMW on shared register */
			v = READ_ONCE(sh.reg);
			udelay(1);
			WRITE_ONCE(sh.reg, v + 0x10000);
			r_inc++;
			smp_store_release(wr, 0);
		}
		udelay(5);
		cond_resched();
	}
	return 0;
}

/* register accessors for regmap: a "slow bus" (2 us per access) */
static int rr(void *ctx, unsigned int reg, unsigned int *val)
{
	udelay(2);
	*val = READ_ONCE(sh.reg);
	return 0;
}
static int rw(void *ctx, unsigned int reg, unsigned int val)
{
	udelay(2);
	WRITE_ONCE(sh.reg, val);
	return 0;
}
```

(3/4) — 사용자(probe): DT에서 이름으로 lock id를 얻고, regmap을 `use_hwlock`으로 만든다.

```c
static DEFINE_SPINLOCK(local_lock);
static int u_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap_config rc = { .reg_bits = 32, .val_bits = 32, .reg_stride = 4,
				    .reg_read = rr, .reg_write = rw, .max_register = 0 };
	struct hwspinlock *hl;
	struct regmap *map;
	struct task_struct *rt;
	u32 n = 0, tmo = 0, i, bad = 0;
	u64 wmax = 0, wsum = 0, d;
	ktime_t t0, end;
	int id, ret;

	use_hw = !strcmp(mode, "hw");
	memset(&sh, 0, sizeof(sh));
	r_acq = r_busy = r_torn = r_inc = 0;
	id = of_hwspin_lock_get_id_byname(dev->of_node, "data");
	if (id < 0)
		return dev_err_probe(dev, id, "hwlock id\n");
	hl = devm_hwspin_lock_request_specific(dev, id);
	rc.use_hwlock = use_hw;
	rc.hwlock_id = of_hwspin_lock_get_id_byname(dev->of_node, "reg");
	map = devm_regmap_init(dev, NULL, NULL, &rc);
	if (!hl || IS_ERR(map))
		return -ENODEV;
	dev_info(dev, "mode=%s data-lock id=%d reg-lock id=%u hold_us=%u to_ms=%u\n",
		 mode, id, rc.hwlock_id, hold_us, to_ms);
```

(4/4) — 경쟁 루프와 결과 출력. Linux 쪽은 `hwspin_lock_timeout()`(또는 `spin` 모드면 로컬 `spin_lock()`)으로 `a`/`b`를 고치고, 이어서 regmap으로 `reg` bit0을 4번 토글한다.

```c
	rt = kthread_create(remote_fn, NULL, "h10_remote");
	if (IS_ERR(rt))
		return PTR_ERR(rt);
	kthread_bind(rt, 0);
	wake_up_process(rt);
	set_cpus_allowed_ptr(current, cpumask_of(1));    /* Linux side on CPU1 */
	end = ktime_add_ms(ktime_get(), 300);
	while (ktime_before(ktime_get(), end)) {
		t0 = ktime_get();
		if (use_hw) {
			ret = hwspin_lock_timeout(hl, to_ms);
			if (ret) {
				tmo++;
				continue;
			}
		} else {
			spin_lock(&local_lock);              /* invisible to remote */
		}
		d = ktime_to_ns(ktime_sub(ktime_get(), t0));
		wsum += d;
		wmax = max(wmax, d);
		WRITE_ONCE(sh.a, sh.a + 1);
		udelay(3);
		WRITE_ONCE(sh.b, sh.b + 1);
		if (use_hw)
			hwspin_unlock(hl);
		else
			spin_unlock(&local_lock);
		n++;
		for (i = 0; i < 4; i++)                      /* regmap RMW on bit 0 */
			if (regmap_update_bits(map, 0, BIT(0), (n + i) & 1))
				bad++;
		udelay(5);
	}
	kthread_stop(rt);
	dev_info(dev, "linux: CS=%u timeouts=%u wait avg=%llu ns max=%llu us\n",
		 n, tmo, n ? wsum / n : 0, wmax / 1000);
	dev_info(dev, "remote: CS=%u busy=%u torn(a!=b seen)=%u\n", r_acq, r_busy, r_torn);
	dev_info(dev, "reg: remote incs=%u reg[31:16]=%u lost=%u\n",
		 r_inc, sh.reg >> 16, r_inc - (sh.reg >> 16));
	return 0;
}
```

드라이버 등록 — provider와 사용자 platform 드라이버 두 개를 한 모듈에 담았다.

```c
static const struct of_device_id p_of[] = { { .compatible = "h10,fake-hwspinlock" }, { } };
static const struct of_device_id u_of[] = { { .compatible = "h10,hwlock-user" }, { } };
MODULE_DEVICE_TABLE(of, p_of);
static struct platform_driver p_drv = {
	.probe = p_probe, .driver = { .name = "h10_hwlock", .of_match_table = p_of },
};
static struct platform_driver u_drv = {
	.probe = u_probe, .driver = { .name = "h10_hwlock_user", .of_match_table = u_of },
};
static int __init h_init(void)
{
	int ret = platform_driver_register(&p_drv);

	if (ret)
		return ret;
	ret = platform_driver_register(&u_drv);
	if (ret)
		platform_driver_unregister(&p_drv);
	return ret;
}
static void __exit h_exit(void)
{
	platform_driver_unregister(&u_drv);
	platform_driver_unregister(&p_drv);
}
module_init(h_init);
module_exit(h_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H10 fake hwspinlock (shared-word emulation) + test user");
```

hwspinlock 요청은 provider 모듈의 참조를 잡기 때문에(`try_module_get`), provider와 사용자가 같은 모듈인 이 구조에서는 `rmmod`가 `Resource temporarily unavailable`로 거절된다(처음 실행에서 실제로 겪었다). 그래서 시나리오마다 게스트를 새로 부팅했다.

```sh
# t_hwlock.sh — /args holds the module parameters for this boot
F='s/^\[ *[0-9.]*\] //; s/h10_hwlock_user h10-hwlock-user: //'
insmod /h10_hwlock.ko $(cat /args)
dmesg | grep h10 | grep -v taints | sed "$F"
```

### 8.5 예제 10 — 세 시나리오

```text
=== insmod h10_hwlock.ko mode=hw hold_us=20 to_ms=10
h10_hwlock h10-hwspinlock: registering 4 locks (shared-word EMULATION)
mode=hw data-lock id=0 reg-lock id=1 hold_us=20 to_ms=10
linux: CS=1131 timeouts=8 wait avg=76136 ns max=13877 us
remote: CS=3249 busy=631 torn(a!=b seen)=0
reg: remote incs=3414 reg[31:16]=3414 lost=0
=== insmod h10_hwlock.ko mode=spin hold_us=20
h10_hwlock h10-hwspinlock: registering 4 locks (shared-word EMULATION)
mode=spin data-lock id=0 reg-lock id=1 hold_us=20 to_ms=10
linux: CS=3459 timeouts=0 wait avg=643 ns max=23 us
remote: CS=7105 busy=0 torn(a!=b seen)=799
reg: remote incs=7105 reg[31:16]=5013 lost=2092
=== insmod h10_hwlock.ko mode=hw hold_us=3000 to_ms=1
h10_hwlock h10-hwspinlock: registering 4 locks (shared-word EMULATION)
mode=hw data-lock id=0 reg-lock id=1 hold_us=3000 to_ms=1
linux: CS=6 timeouts=67 wait avg=2057341 ns max=2973 us
remote: CS=68 busy=39 torn(a!=b seen)=0
reg: remote incs=107 reg[31:16]=107 lost=0
```

출력에서 볼 것:

| 시나리오 | 관찰 | 해석 |
|---|---|---|
| `hw`, hold 20 us | `torn=0`, `lost=0` | 원격이 `a`와 `b`를 볼 때 한 번도 "Linux가 반쯤 고친 상태"를 보지 않았고, 공유 레지스터의 원격 증가분 3414개가 하나도 사라지지 않았다. 하드웨어 락(흉내)이 두 "OS"를 직렬화했다 |
| `spin` (각자 자기 락) | `torn=799`, `lost=2092` (7105개 중 29%) | Linux의 `spinlock_t`는 원격에 안 보인다. 원격이 Linux 임계 구역 한가운데를 799번 엿봤고, regmap의 read-modify-write가 원격의 증가분을 2092번 덮어썼다. regmap의 기본 락(로컬 mutex/spinlock)은 **Linux 안에서만** RMW를 원자적으로 만든다 |
| `hw`, hold 3000 us, timeout 1 ms | `timeouts=67`, `CS=6`, 대기 평균 2.06 ms | 원격이 락을 3 ms씩 쥐면 1 ms timeout은 거의 매번 실패한다. 실패해도 `-ETIMEDOUT`이 돌아올 뿐 데이터는 안전하다(`torn=0`) |

수치에 대한 정직한 메모:

- `spin` 모드에서 Linux의 대기 시간이 평균 643 ns로 아주 짧다. 아무하고도 경쟁하지 않으니까 당연하다. **빠르고 틀린 것**이다.
- 첫 시나리오에서도 `timeouts=8`, 최대 대기 13.9 ms가 나왔다. 원격의 임계 구역은 20 us뿐인데 10 ms를 넘긴 것은, TCG가 vCPU를 호스트 스레드로 돌리고 그 VM에서 다른 QEMU 작업 여럿이 동시에 돌아(load average 9.63) **vCPU 스레드 자체가 수 ms씩 멈췄기** 때문으로 본다(추정 — 실리콘에서는 일어나지 않는 종류의 지연이다). 그래도 이 숫자는 교훈을 준다: 원격 코어가 인터럽트나 디버거 정지로 락을 오래 쥐면 Linux 쪽에서 timeout이 실제로 난다. timeout 경로를 반드시 처리하라.
- 세 번째 시나리오에서 timeout 1 ms인데 평균 대기가 2.06 ms다. `__hwspin_lock_timeout()`은 `msecs_to_jiffies(to)`로 만료를 계산하고 이 커널은 `CONFIG_HZ=250`(1 jiffy = 4 ms)이라, "1 ms"가 실제로는 다음 tick까지(0~4 ms)다. 정밀한 짧은 timeout이 필요하면 `_in_atomic` 변형(udelay 누적)을 쓰거나 `trylock` 루프를 직접 짠다.

### 8.6 regmap + hwlock — 코프로세서와 공유하는 레지스터

위 실험의 `reg`는 실물의 이런 장면이다: STM32MP1의 EXTI(외부 인터럽트 컨트롤러) 레지스터 일부를 Cortex-A7(Linux)과 Cortex-M4가 같이 설정한다. Linux 쪽 드라이버 `irq-stm32mp-exti.c`가 `hwspin_lock_timeout_in_atomic()`으로 이 레지스터 접근을 감싼다(랩 소스에서 확인).

regmap을 쓰면 드라이버가 매번 lock/unlock을 쓸 필요 없이 설정 한 번으로 끝난다. regmap core(`drivers/base/regmap/regmap.c`)는 `use_hwlock`이 켜져 있으면 `hwspin_lock_request_specific(config->hwlock_id)`로 락을 받고, 모든 `regmap_read/write/update_bits`를 `hwspin_lock_timeout(map->hwlock, UINT_MAX)`(사실상 무한 대기)로 감싼다. `hwlock_mode`는 `HWLOCK_IRQSTATE`, `HWLOCK_IRQ`, 0(기본) 중 하나다.

```c
	struct regmap_config rc = {
		.reg_bits = 32, .val_bits = 32, .reg_stride = 4,
		.use_hwlock = true,
		.hwlock_id = of_hwspin_lock_get_id_byname(np, "reg"),
		.hwlock_mode = HWLOCK_IRQSTATE,   /* 우리 실험은 0(기본)을 썼다 */
	};
```

주의할 점: regmap의 hwlock은 **regmap 호출 하나 단위**로만 잡힌다. `regmap_update_bits()` 하나는 원자적이지만, `regmap_read()` 후 C 코드에서 계산하고 `regmap_write()`하는 두 호출 사이는 보호되지 않는다. 여러 레지스터를 묶어 바꿔야 하면 `hwspin_lock_timeout()`을 직접 잡고 그 안에서 MMIO를 하거나, regmap의 `disable_locking`/외부 lock 콜백 설계를 따로 해야 한다. 그리고 regmap이 이미 그 lock id를 독점 요청하므로, 같은 id를 드라이버가 또 `request_specific`하면 실패한다(우리 코드가 `data`와 `reg`에 락을 따로 쓴 이유).

---

## 9. IPC 3종 세트 — 공유 메모리 + hwspinlock + doorbell

### 9.1 패턴

메일박스·hwspinlock·공유 메모리를 합치면 이종 코어 IPC의 고전 패턴이 된다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="30" y="15" width="150" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="105" y="40" text-anchor="middle" font-size="13">Linux (Cortex-A)</text>
  <rect x="265" y="15" width="150" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="40" text-anchor="middle" font-size="13">공유 SRAM + 락</text>
  <rect x="500" y="15" width="150" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="575" y="40" text-anchor="middle" font-size="13">RTOS (Cortex-M)</text>
  <line x1="105" y1="55" x2="105" y2="405" stroke="currentColor" stroke-dasharray="4 4"/>
  <line x1="340" y1="55" x2="340" y2="405" stroke="currentColor" stroke-dasharray="4 4"/>
  <line x1="575" y1="55" x2="575" y2="405" stroke="currentColor" stroke-dasharray="4 4"/>
  <line x1="105" y1="85" x2="336" y2="85" stroke="#3f9a6b" marker-end="url(#ah8)"/>
  <text x="220" y="78" text-anchor="middle" font-size="12">① hwspin_lock_timeout</text>
  <line x1="105" y1="125" x2="336" y2="125" stroke="currentColor" marker-end="url(#ah8)"/>
  <text x="220" y="118" text-anchor="middle" font-size="12">② 본문을 링에 쓰고 head++</text>
  <line x1="105" y1="165" x2="336" y2="165" stroke="#3f9a6b" marker-end="url(#ah8)"/>
  <text x="220" y="158" text-anchor="middle" font-size="12">③ hwspin_unlock</text>
  <line x1="105" y1="205" x2="571" y2="205" stroke="#d0564a" stroke-width="2" marker-end="url(#ah8)"/>
  <text x="340" y="198" text-anchor="middle" font-size="12">④ mbox_send_message — doorbell IRQ</text>
  <line x1="575" y1="245" x2="344" y2="245" stroke="#3f9a6b" marker-end="url(#ah8)"/>
  <text x="458" y="238" text-anchor="middle" font-size="12">⑤ 락 → 읽고 tail++ → 해제</text>
  <line x1="575" y1="285" x2="344" y2="285" stroke="currentColor" marker-end="url(#ah8)"/>
  <text x="458" y="278" text-anchor="middle" font-size="12">⑥ 응답을 반대 방향 링에</text>
  <line x1="575" y1="325" x2="109" y2="325" stroke="#d0564a" stroke-width="2" marker-end="url(#ah8)"/>
  <text x="340" y="318" text-anchor="middle" font-size="12">⑦ 응답 doorbell → ISR → rx_callback</text>
  <rect x="120" y="345" width="440" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="366" text-anchor="middle" font-size="12">⑧ rx_callback(hardirq)은 work만 예약하고</text>
  <text x="340" y="384" text-anchor="middle" font-size="12">본문 처리는 workqueue/threaded IRQ에서</text>
</svg>
```

그림 7 — IPC 3종 세트의 한 왕복. 락은 공유 메모리의 **본문과 인덱스**를 지키고, 메일박스는 **알림만** 한다. 단일 생산자·단일 소비자 링이라면 ①③의 락 없이 head/tail을 각자 한쪽만 쓰고 배리어로 순서를 맞추는 설계도 흔하다(rpmsg의 vring이 그렇다).

세 가지를 섞을 때의 규칙:

- **배리어**: ②에서 본문을 쓴 뒤 ④ doorbell을 울리기 전에 쓰기가 상대에게 보여야 한다. MMIO 쓰기(`writel`)는 앞선 일반 메모리 쓰기와의 순서를 보장하지만(H1, F5), 공유 메모리가 **캐시되는 DDR**이면 캐시 flush(또는 비캐시 매핑)가 따로 필요하다. 공유 영역은 보통 DT `reserved-memory`에 `no-map`으로 잡고 비캐시로 매핑한다(F10).
- **락은 짧게**: 락 안에서는 인덱스 갱신과 짧은 복사만. 원격의 ISR이 그 락을 기다리며 돌 수도 있다.
- **doorbell은 락 밖에서**: ④를 락 안에서 하면 상대가 깨어나자마자 락에서 막힌다.
- **응답 처리는 bottom half로**: `rx_callback`은 hardirq다(예제 4, 5).

### 9.2 위에 올라가는 것 1 — remoteproc · rpmsg (O1·O2 예고)

- **remoteproc**: M코어 펌웨어를 로드하고(`firmware-name`), 리셋을 풀고, 죽으면 복구한다. 메일박스 채널을 몇 개 들고 "kick"(새 메시지 있음), "shutdown", "detach" 같은 신호를 보낸다. STM32MP15 DK 보드의 DT에서 `mbox-names = "vq0", "vq1", "shutdown", "detach"`를 예제 11에서 본다. STM32 remoteproc 소스는 `vq` 채널은 `tx_block = false`, `shutdown`은 `tx_block = true`와 `tx_tout = 500`, `detach`는 `tx_tout = 200`으로 잡는다(랩 소스 `stm32_rproc.c`에서 확인).
- **rpmsg(virtio)**: 공유 메모리에 virtio **vring** 두 개(송신/수신)를 두고, 메시지를 vring에 쌓은 뒤 메일박스로 "vq0/vq1에 뭔가 있다"만 알린다. §6.1의 큐 20칸 한계를 피하는 방식이다. 유저 공간에서는 `/dev/rpmsgN`(rpmsg_char)으로 보인다. 자세한 것은 O2.

### 9.3 위에 올라가는 것 2 — SCMI: 전원·클럭·센서를 펌웨어에 맡기기

최신 SoC(Arm 레퍼런스 플랫폼, i.MX95, STM32MP2, 여러 서버 SoC)는 클럭·전원 도메인·DVFS·온도 센서를 Linux가 직접 레지스터로 만지지 않는다. 별도 시스템 제어 프로세서(SCP)나 TF-A(EL3, C5)가 맡고, Linux는 **SCMI(System Control and Management Interface)** 메시지로 "이 클럭 400 MHz로", "이 전원 도메인 켜 줘"라고 요청만 한다. 보안(Linux가 해킹돼도 전압을 망가뜨리지 못함)과 여러 OS 사이 자원 중재가 이유다.

SCMI의 전송(transport)은 여러 가지인데, 가장 고전적인 것이 **메일박스 + 공유 메모리(shmem)** 다.

- 요청 본문은 `arm,scmi-shmem` 영역(작은 SRAM 조각)에 쓴다.
- 메일박스로 doorbell을 울린다(`mboxes`, `mbox-names = "tx", "rx"`).
- 응답은 같은 shmem에 돌아오고, 펌웨어가 반대 방향 doorbell(또는 완료 IRQ)을 울린다.
- 메일박스 대신 **SMC/HVC 명령 자체를 doorbell로** 쓰는 전송도 있다(`arm,scmi-smc`). C5에서 본 PSCI가 SMC로 EL3를 부르는 것과 같은 길이다. 랩 커널에는 `ARM_SCMI_TRANSPORT_MAILBOX`와 `ARM_SCMI_TRANSPORT_SMC`가 둘 다 켜져 있다(예제 3).

Linux 안에서는 SCMI가 clk provider(`#clock-cells`), power-domain provider(`#power-domain-cells`), cpufreq 드라이버로 보인다. 그래서 일반 드라이버는 평소처럼 `clk_prepare_enable()`(H3)을 부르고, 그 아래가 SCMI 메시지 → 메일박스로 바뀐 것을 모른다. 이것이 "클럭이 안 켜진다"를 디버그할 때 레지스터 덤프만 봐서는 안 되는 이유다.

---

## 10. 실물 SoC Device Tree 해독

### 10.1 예제 11 — 랩 커널 트리의 dtsi에서 바인딩 읽기

무엇을 확인하나: 앞에서 배운 `#mbox-cells`/`mboxes`/`mbox-names`/`#hwlock-cells`가 실제 SoC DT에서 어떻게 쓰이는지. VM에서 랩 소스 트리를 grep한다(게스트가 아니라 VM 호스트 셸).

```sh
cd /var/lab/src/linux-6.18.55/arch/arm64/boot/dts
grep -n -A6 "mu2: mailbox@30e60000" freescale/imx8mp.dtsi
grep -n "mbox-names = \"tx\", \"rx\", \"rxdb\"" -A1 freescale/imx8mp.dtsi
grep -n -A3 "&mailbox {" arm/juno-scmi.dtsi
grep -n -A5 "compatible = \"arm,scmi\"" arm/juno-scmi.dtsi
grep -n -A3 "cpu_scp_lpri0: scp-sram@0" arm/juno-scmi.dtsi
grep -n -A3 "hwspinlock: spinlock@2a000000" ti/k3-am62-main.dtsi
grep -n -A6 "&mailbox0_cluster0 {" ti/k3-am62-ti-ipc-firmware.dtsi
grep -n "mboxes = <&mailbox0_cluster0" ti/k3-am62-ti-ipc-firmware.dtsi
cd ../../../arm/boot/dts/st
grep -n -A9 "ipcc: mailbox@4c001000" stm32mp151.dtsi
grep -n -A4 "&m4_rproc {" stm32mp15xx-dkx.dtsi
```

```text
1246:			mu2: mailbox@30e60000 {
1247-				compatible = "fsl,imx8mp-mu", "fsl,imx6sx-mu";
1248-				reg = <0x30e60000 0x10000>;
1249-				interrupts = <GIC_SPI 136 IRQ_TYPE_LEVEL_HIGH>;
1250-				#mbox-cells = <2>;
1251-				clocks = <&audio_blk_ctrl IMX8MP_CLK_AUDIOMIX_MU2_ROOT>;
1252-				status = "disabled";
2443:			mbox-names = "tx", "rx", "rxdb";
2444-			mboxes = <&mu2 0 0>, <&mu2 1 0>, <&mu2 3 0>;
187:&mailbox {
188-	compatible = "arm,mhu-doorbell", "arm,primecell";
189-	#mbox-cells = <2>;
190-};
46:			compatible = "arm,scmi";
47-			mbox-names = "tx", "rx";
48-			mboxes = <&mailbox 0 0 &mailbox 0 1>;
49-			shmem = <&cpu_scp_lpri0 &cpu_scp_lpri1>;
50-			#address-cells = <1>;
51-			#size-cells = <0>;
204:	cpu_scp_lpri0: scp-sram@0 {
205-		compatible = "arm,scmi-shmem";
206-		reg = <0x0 0x80>;
207-	};
797:	hwspinlock: spinlock@2a000000 {
798-		compatible = "ti,am64-hwspinlock";
799-		reg = <0x00 0x2a000000 0x00 0x1000>;
800-		#hwlock-cells = <1>;
22:&mailbox0_cluster0 {
23-	status = "okay";
24-
25-	mbox_m4_0: mbox-m4-0 {
26-		ti,mbox-rx = <0 0 0>;
27-		ti,mbox-tx = <1 0 0>;
28-	};
37:	mboxes = <&mailbox0_cluster0 &mbox_m4_0>;
48:	mboxes = <&mailbox0_cluster0 &mbox_r5_0>;
126:		ipcc: mailbox@4c001000 {
127-			compatible = "st,stm32mp1-ipcc";
128-			#mbox-cells = <1>;
129-			reg = <0x4c001000 0x400>;
130-			st,proc-id = <0>;
131-			interrupts-extended =
132-				<&exti 61 IRQ_TYPE_LEVEL_HIGH>,
133-				<&intc GIC_SPI 101 IRQ_TYPE_LEVEL_HIGH>;
134-			interrupt-names = "rx", "tx";
135-			clocks = <&rcc IPCC>;
477:&m4_rproc {
478-	memory-region = <&retram>, <&mcuram>, <&mcuram2>, <&vdev0vring0>,
479-			<&vdev0vring1>, <&vdev0buffer>;
480-	mboxes = <&ipcc 0>, <&ipcc 1>, <&ipcc 2>, <&ipcc 3>;
481-	mbox-names = "vq0", "vq1", "shutdown", "detach";
```

해독:

| 노드 | 읽는 법 |
|---|---|
| i.MX8MP `mu2` + DSP | `#mbox-cells = <2>` → `<&mu2 타입 인덱스>`. DSP는 `<&mu2 0 0>`(TX 0번), `<&mu2 1 0>`(RX 0번), `<&mu2 3 0>`(RX doorbell 0번)을 `"tx", "rx", "rxdb"`로 받는다. 데이터 채널과 doorbell 채널이 한 MU 안에 같이 있는 것이 보인다 |
| Juno SCMI | `&mailbox`(MHU)를 doorbell 모드로 바꾸고 `#mbox-cells = <2>`. SCMI 노드는 `"tx"`, `"rx"` 두 채널과 `shmem` 두 개(`arm,scmi-shmem` SRAM 조각, 0x80 바이트)를 받는다. 메일박스는 알림, shmem은 본문 — 그림 7 그대로다 |
| TI AM62 | hwspinlock 블록은 `#hwlock-cells = <1>`(락 번호). 메일박스는 클러스터 노드 아래 **서브노드**마다 `ti,mbox-rx`/`ti,mbox-tx` = (FIFO, IRQ 줄, 사용자). M4F 코어는 `mboxes = <&mailbox0_cluster0 &mbox_m4_0>`로 서브노드 phandle을 specifier로 쓴다 |
| STM32MP15 | IPCC는 `#mbox-cells = <1>`(채널 번호), IRQ가 `"rx"`, `"tx"` 두 줄. DK 보드의 M4 remoteproc은 채널 0~3을 `vq0`, `vq1`, `shutdown`, `detach`로 쓴다. `memory-region`의 `vdev0vring0/1`, `vdev0buffer`가 rpmsg의 공유 메모리다 |

STM32MP15의 HSEM(hwspinlock)은 드라이버(`stm32_hwspinlock.c`, `st,stm32-hwspinlock`)는 있지만 이 트리의 `stm32mp151.dtsi`에서는 노드를 찾지 못했다(grep 결과 없음). 보드·버전마다 다를 수 있다.

### 10.2 예제 12 — 게스트에서 SCMI·rpmsg 흔적 확인

무엇을 확인하나: QEMU virt에는 SCMI 펌웨어가 없으니 SCMI가 "준비만 된" 상태인지, 그리고 rpmsg/virtio 버스가 등록돼 있는지.

```sh
dmesg | grep -i scmi
ls /sys/bus | grep -iE "scmi|rpmsg|virtio"
ls /sys/bus/platform/drivers | grep -iE "imx|stm32" | tr "\n" " "
```

```text
lab initramfs: 6.18.55 up 2.34s
[    0.863647] scmi_core: SCMI protocol bus registered
rpmsg
scmi_protocol
virtio
clk_imx8ulp fsl,imx8dxl-iomuxc gpio-imx-scu imx-bus-devfreq imx-gpcv2 imx-i2c imx-intmux imx-irqsteer imx-lpi2c imx-pgc imx-rproc imx-scu imx-scu-pd imx-uart imx6q-pcie imx8-acm imx8-pcie-phy imx8m-blk-ctrl imx8mm-ccm imx8mm-pinctrl imx8mn-ccm imx8mn-pinctrl imx8mp-audio-blk-ctrl imx8mp-blk-ctrl imx8mp-ccm imx8mp-dwc3 imx8mp-interconnect imx8mp-pinctrl imx8mq-ccm imx8mq-pinctrl imx8qm-pinctrl imx8qxp-clk imx8qxp-lpcg-clk imx8qxp-pinctrl imx8ulp-pinctrl imx91-pinctrl imx93-blk-ctrl imx93-ccm imx93-pinctrl imx93_power_domain imx93_src imx_mu imx_ocotp imx_scu_ocotp imx_usb reset_imx7 sdhci-esdhc-imx stm32-etzpc stm32-lptimer-timer stm32-rifsc stm32-rng stm32-usart stm32_hdp stm32mp21_rcc stm32mp257-pinctrl stm32mp25_rcc stm32mp_exti usbmisc_imx [    3.126814] reboot: Power down
```

출력에서 볼 것: `scmi_core: SCMI protocol bus registered` 한 줄뿐이다. SCMI **버스**(프로토콜 드라이버들이 붙을 자리)는 생겼지만, DT에 `arm,scmi` 노드가 없어서 실제 에이전트는 probe되지 않았다. `rpmsg`, `scmi_protocol`, `virtio` 버스가 있고, platform 드라이버 목록에 `imx_mu`(i.MX MU 메일박스), `imx-rproc`가 built-in으로 들어 있다. 실물 보드에서는 여기에 `/sys/class/remoteproc/remoteproc0`와 `/sys/bus/scmi_protocol/devices/`가 나타난다(실물에서 확인할 것).

---

## 11. 커널 안에서는 무슨 일이 일어나나

`mbox_send_message()` 한 번이 지나가는 길(랩 소스 `drivers/mailbox/mailbox.c` 기준, 함수 이름만 영어로).

```
client: mbox_send_message(chan, mssg)
  add_to_rbuf()            chan->lock (irqsave); ring of MBOX_TX_QUEUE_LEN=20; -ENOBUFS if full
  msg_submit()             if active_req == MBOX_NO_MSG: tx_prepare(); ops->send_data()
                           ok -> active_req = mssg ; -EBUSY -> stays queued
                           TXDONE_BY_POLL -> hrtimer_start(poll_hrt, 0)
  tx_block ? wait_for_completion_timeout(tx_complete, tx_tout)
             timeout -> tx_tick(chan, -ETIME)

controller ISR (TX empty): mbox_chan_txdone(chan, 0)  --(only if TXDONE_BY_IRQ)--> tx_tick()
core hrtimer:              txdone_hrtimer() -> ops->last_tx_done() -> tx_tick()
client (ACK):              mbox_client_txdone(chan, 0) --(only if TXDONE_BY_ACK)--> tx_tick()

tx_tick(chan, r)
  active_req -> mssg ; active_req = MBOX_NO_MSG
  msg_submit()             next queued message goes to HW
  mssg == MBOX_NO_MSG ? return (nothing to report)
  cl->tx_done(cl, mssg, r)
  r != -ETIME && tx_block ? complete(tx_complete)

controller ISR (RX): mbox_chan_received_data(chan, data) -> cl->rx_callback(cl, data)  (no buffering)
```

이 그림 하나로 §5·§6의 모든 관찰이 설명된다.

- POLL 모드에서 `tx_done`이 늦는 이유 = `txdone_hrtimer`의 주기.
- 타임아웃 뒤 `tx_done`이 엉뚱한 메시지에 붙는 이유 = `tx_tick`은 "그 순간의 `active_req`"를 보고하지, "타임아웃 난 그 메시지"를 보고하지 않는다.
- 죽은 원격에서 `tx_done`조차 없는 이유 = `send_data`가 `-EBUSY`면 `active_req`가 비어 있고, `tx_tick`은 `MBOX_NO_MSG`를 보고 그냥 돌아간다.

채널 요청 쪽: `mbox_request_channel()` → `fwnode_property_get_reference_args(fwnode, "mboxes", "#mbox-cells", …)` → 등록된 컨트롤러 목록(`mbox_cons`)에서 그 fwnode를 가진 컨트롤러 검색 → 없으면 **`-EPROBE_DEFER`**(G3의 deferred probe) → 있으면 `fw_xlate`/`of_xlate` → `__mbox_bind_client()`(이미 주인이 있으면 `-EBUSY`, 컨트롤러 모듈 참조 획득, `knows_txdone`이면 POLL→ACK) → `ops->startup()`.

hwspinlock 쪽: `of_hwspin_lock_get_id()` → `of_parse_phandle_with_args(np, "hwlocks", "#hwlock-cells", …)` → provider 목록(radix tree)에서 bank를 찾아 xlate → 전역 id. `hwspin_lock_request_specific()` → provider 모듈 `try_module_get` + `pm_runtime_get_sync` → 사용 중 표시. `__hwspin_trylock()` → 로컬 spinlock 변형 → `ops->trylock()` → `mb()`. `__hwspin_unlock()` → `mb()` → `ops->unlock()` → 로컬 spinlock 해제.

드라이버를 쓸 때 다시 만나는 곳:

| 이 노트의 개념 | 다시 나오는 곳 |
|---|---|
| provider/consumer + DT specifier + `-EPROBE_DEFER` | G3(deferred probe), H3(clk), H4(GPIO), H6(irq_domain xlate) |
| 콜백이 hardirq에서 불림 → bottom half로 넘기기 | F6, F7 |
| 공유 메모리 비캐시 매핑, `reserved-memory` | F10, H9(DMA·캐시 관리) |
| regmap `use_hwlock` | H2 |
| remoteproc·rpmsg·OpenAMP | O1, O2, O3 |
| SCMI가 clk·power-domain provider로 보임 | H3, C5(TF-A가 SCMI 서버가 되는 경우) |

---

## 12. Embedded 실무에서 다시 보기

### 12.1 센서 키트: IMU를 M7이 읽고 Linux가 받는다

i.MX8M Plus 센서 키트에서 IMU를 1 kHz로 정확히 읽는 일은 Linux보다 M7이 잘한다(지터 없음). 구조는 이렇다.

- M7: SPI로 IMU를 읽어 공유 메모리 링(또는 rpmsg vring)에 샘플을 쌓는다. 10 ms마다 MU doorbell로 "샘플 10개 있음".
- Linux: `imx_rproc`가 M7 펌웨어를 올리고, rpmsg 채널이 `/dev/rpmsg0`로 보인다. 유저 공간 프로세스가 epoll로 기다렸다가 읽는다(A6).
- 실패 시나리오: M7 펌웨어가 HardFault로 멈춤 → doorbell이 끊긴다. 유저 공간 타임아웃으로 감지 → remoteproc의 `recovery`로 재부팅. 메일박스 timeout 로그(`-62`)가 첫 증상인 경우가 많다.

### 12.2 TI AM62: R5F(장치 관리)와 M4F

AM62에는 Linux 외에 DM(Device Manager) R5F와 M4F 코어가 있다(기억 — TI 문서 확인). 예제 11에서 본 것처럼 메일박스 클러스터의 서브노드가 코어마다 따로 있다. 하드웨어 스핀락 블록도 있어서, 두 코어가 같은 공유 메모리 구조를 다룰 때 사용한다.

### 12.3 "클럭이 안 켜져요" — SCMI 시대의 디버그

SCMI 기반 SoC에서 어떤 IP의 클럭이 안 켜지면, Linux 쪽 `clk_summary`(debugfs)에는 켜졌다고 나오는데 실제로는 펌웨어가 거절했을 수 있다. 확인 순서: (1) `dmesg | grep -i scmi`로 SCMI 에이전트 probe와 프로토콜 버전, (2) 해당 clk가 SCMI clk인지(DT `clocks = <&scmi_clk N>`), (3) 펌웨어 쪽 로그(TF-A/SCP 콘솔), (4) 메일박스 timeout 메시지. Apple에서 새 실리콘 bring-up 때 "펌웨어가 응답 안 함"을 버스 수준까지 따라가던 경험이 그대로 쓰인다 — 메일박스 레지스터의 상태 비트를 JTAG로 읽어 "doorbell이 울렸는데 상대가 안 가져갔다"와 "상대가 응답했는데 Linux가 IRQ를 못 받았다"를 가른다.

### 12.4 원격과 공유하는 레지스터: regmap + hwlock

STM32MP1의 EXTI처럼, 인터럽트 라우팅이나 GPIO 설정 레지스터를 두 코어가 같이 쓰는 경우가 있다. 드라이버는 regmap에 `use_hwlock`을 켜거나(§8.6), 직접 `hwspin_lock_timeout_in_atomic()`으로 감싼다. 원격 펌웨어 팀과 **락 번호와 레지스터 소유권을 문서로 합의**하는 것이 코드보다 먼저다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `mbox_send_message()` 반환값을 `if (ret)`로 검사 | 두 번째 메시지부터 "실패" 처리 | 성공 시 0 이상의 **토큰**을 돌려준다 (예제 4: 0, 1, 2) | `if (ret < 0)` |
| ACK 모드 컨트롤러인데 `mbox_client_txdone()` 안 부름 | 첫 메시지만 나가고 이후 전부 timeout, 블로킹이면 매번 `-ETIME` | 아무도 TX를 끝내지 않는다 (예제 5) | 응답 수신 시나 송신 직후 `mbox_client_txdone(chan, 0)` |
| IRQ 컨트롤러에 `knows_txdone` + `mbox_client_txdone()` | `Client can't run the TX ticker` 에러 | `knows_txdone`은 POLL만 ACK로 바꾼다 | 컨트롤러의 txdone 방식 확인 후 맞춰 쓴다 |
| `rx_callback`에서 잠드는 함수 호출 | `BUG: sleeping function called from invalid context`, 간헐 멈춤 | 콜백은 컨트롤러 ISR(hardirq) 컨텍스트 (예제 4) | 데이터 복사 후 `schedule_work()`/threaded IRQ로 넘긴다 |
| 스택 변수를 메시지로 넘기고 반환 | 원격이 쓰레기를 받음, 간헐적 오동작 | core는 포인터만 큐에 저장 → `tx_done` 전까지 살아 있어야 함 | static/할당 메모리, `tx_done`에서 해제 |
| 논블로킹으로 몰아 보내기 | `Try increasing MBOX_TX_QUEUE_LEN`, `-ENOBUFS` | 채널 큐 20칸 고정 (예제 6) | `tx_done`에서 흐름 제어, 본문은 공유 메모리 링 + doorbell 한 번 |
| `-ETIME` 뒤 같은 명령 재전송 | 원격에서 명령이 두 번 실행, 응답 seq 어긋남 | 타임아웃 ≠ 미전달. 늦은 응답이 다음 요청에 붙는다 (예제 7) | seq 번호, 멱등 명령 설계, 타임아웃 후 채널 재오픈·원격 리셋 |
| 원격과 공유하는 데이터를 Linux `spinlock_t`로만 보호 | 가끔 깨진 값, 카운터 손실 | 원격 OS는 Linux 락을 모른다 (예제 10: torn 799, lost 2092) | hwspinlock(또는 단일 생산자·소비자 링 + 배리어) |
| hwspinlock 안에서 긴 작업·잠들기 | Linux CPU가 선점 불가로 ms 단위 정지, timeout 남발 | timeout API도 busy-wait, 기본 모드는 선점 꺼짐 | 임계 구역은 인덱스·짧은 복사만 |
| 공유 메모리를 캐시되는 일반 매핑으로 사용 | 상대가 옛 값을 봄, 재현 어려운 데이터 깨짐 | 서로 다른 캐시 도메인 | `reserved-memory` + `no-map` + 비캐시 매핑, 또는 명시적 캐시 유지보수 |

---

## 14. 면접에서 이렇게 말한다

**Q.** How does Linux running on the Cortex-A cores communicate with a Cortex-M co-processor on the same SoC?

**A.** 세 가지 하드웨어를 쓴다. 본문은 공유 메모리(SRAM이나 `reserved-memory`로 잡은 DDR)에 두고, 알림은 메일박스 doorbell 인터럽트로, 공유 구조의 상호 배제는 하드웨어 스핀락이나 단일 생산자·소비자 링으로 한다. Linux에서는 mailbox framework와 hwspinlock framework가 이 하드웨어를 감싸고, 그 위에 remoteproc(펌웨어 로드·리셋·복구)과 rpmsg(virtio vring 기반 메시지 채널)가 올라간다. 유저 공간은 `/dev/rpmsgN`으로 본다.

> We split it into payload, notification and mutual exclusion. The payload lives in shared memory — usually a reserved-memory carve-out mapped non-cacheable. Notification is a mailbox doorbell that raises an interrupt on the other core, and if both sides touch the same structure we protect it with a hardware spinlock or design it as single-producer single-consumer rings. In Linux that's the mailbox and hwspinlock frameworks, with remoteproc handling firmware load and recovery, and rpmsg over virtio vrings carrying the messages, exposed to userspace as /dev/rpmsg devices.

**꼬리 질문**: "왜 메시지를 메일박스 레지스터에 다 담지 않나?" → 레지스터는 워드 몇 개뿐이고 채널 큐도 20칸이라, 본문은 vring에 모으고 doorbell은 "새 것 있음" 한 번만 보낸다.

**Q.** Explain the Linux mailbox framework: what does a controller driver implement and what does a client do?

**A.** controller는 하드웨어 하나를 맡아 `mbox_chan_ops`의 `send_data`, `startup`, `shutdown`, 필요하면 `last_tx_done`/`peek_data`를 구현하고, TX 완료를 IRQ로 알 수 있는지(`txdone_irq`), 폴링해야 하는지(`txdone_poll`), 아니면 client가 알려 줘야 하는지 밝혀 `mbox_controller_register`한다. RX 인터럽트에서는 `mbox_chan_received_data`, TX-empty 인터럽트에서는 `mbox_chan_txdone`을 부른다. client는 `mbox_client`에 `rx_callback`, `tx_done`, 블로킹 여부와 타임아웃을 채우고 DT `mboxes`/`mbox-names`로 채널을 요청해 `mbox_send_message`로 보낸다. 가운데 core가 채널 소유권, 채널당 20칸 큐, TX 완료 상태 기계를 맡는다.

> The controller side knows the hardware: it fills mbox_chan_ops — send_data, startup, shutdown, and last_tx_done if TX completion has to be polled — declares whether it gets a TX-done interrupt, needs polling, or relies on the client to ack, and registers the controller. From its IRQ handlers it calls mbox_chan_received_data and mbox_chan_txdone. The client knows the protocol: it sets rx_callback, tx_done, tx_block and tx_tout, requests a channel through the mboxes and mbox-names properties, and sends with mbox_send_message. The core in between owns channel allocation, a 20-entry TX queue per channel, and the TX-done state machine.

**꼬리 질문**: "`rx_callback`은 어떤 컨텍스트에서 불리나?" → 보통 컨트롤러의 hardirq 핸들러 안. 잠들면 안 되고, 무거운 처리는 workqueue로.

**Q.** What are the three TX-done methods and when would you use `knows_txdone`?

**A.** IRQ는 하드웨어에 TX-empty 인터럽트가 있을 때(i.MX MU, STM32 IPCC), POLL은 상태 비트만 있을 때(ARM MHU) core가 hrtimer로 `last_tx_done`을 부르는 방식, ACK는 하드웨어가 아무것도 모를 때 client가 `mbox_client_txdone`으로 알려 주는 방식(Qualcomm IPCC/APCS 위의 glink·smd)이다. `knows_txdone`은 POLL 컨트롤러 위에서 프로토콜 자체가 완료를 아는 경우(SCMI처럼 응답이 곧 완료) 폴링을 끄고 ACK로 바꾸는 스위치다. 내 실험에서 POLL(1 ms 주기)은 원격이 20 us 만에 끝나도 송신이 약 1.08 ms 뒤에 돌아왔고, `knows_txdone`을 켜면 IRQ 모드와 비슷해졌다.

> IRQ mode is for hardware with a TX-empty interrupt, like the i.MX MU or STM32 IPCC. Poll mode is for hardware that only exposes a status bit, like ARM MHU — the core runs an hrtimer that calls last_tx_done. Ack mode is when the hardware can't tell at all, so the client calls mbox_client_txdone, which is what the Qualcomm glink and smd drivers do. knows_txdone converts a polled channel into ack mode when the protocol itself knows completion — SCMI does that because the reply means the transfer is done. In my measurements polling at 1 ms added about a millisecond per blocking send even though the remote answered in 20 microseconds.

**Q.** What happens if the remote processor doesn't respond within `tx_tout`?

**A.** 블로킹 송신은 `-ETIME`을 받고 core는 그 채널의 `active_req`를 비운다. 하지만 원격은 그 명령을 이미 실행했을 수 있고, 늦은 응답이 나중에 `rx_callback`으로 오며, 다음 메시지가 하드웨어에서 `-EBUSY`로 큐에 밀리면 이후 `tx_done`이 다른 메시지 번호로 불리기도 한다. 그래서 타임아웃은 "실패"가 아니라 "모름"으로 다루고, 프로토콜에 sequence 번호를 넣어 늦은 응답을 버리고, 멱등이 아닌 명령은 재전송 대신 상태 조회 후 결정하며, 채널을 재오픈하거나 remoteproc으로 원격을 복구한다. SCMI mailbox 전송에도 늦은 응답 IRQ를 걸러내는 코드가 있다.

> The blocking send returns -ETIME and the core clears the active request, but the remote may already have executed the command. A late reply still arrives through rx_callback, and if the next message gets queued behind a busy channel, later tx_done callbacks can be attributed to a different message — I reproduced exactly that with an emulated slow remote. So I treat a timeout as "unknown", not "failed": sequence numbers to drop stale replies, idempotent commands or a status query before retrying, and resync by reopening the channel or recovering the remote through remoteproc. The SCMI mailbox transport has explicit code to ignore such late replies.

**Q.** Why can't you just use a normal spinlock in shared memory between Linux and an RTOS core?

**A.** 일반 spinlock의 원자성은 exclusive monitor나 LSE 원자 명령이 **같은 coherency 도메인** 안에서 캐시 라인 소유권을 중재해서 나온다. Cortex-M은 대개 그 도메인 밖이고, 캐시도 다르며, RTOS는 Linux의 qspinlock 형식도 모른다. 하드웨어 스핀락은 Device 메모리 레지스터에 대한 읽기 한 번(TI)이나 쓰기 후 읽기(STM32 HSEM, Qualcomm TCSR)로 소유권을 결정해서 이 문제를 피한다. 내가 공유 워드 위의 가짜 provider로 실험했을 때, 양쪽이 각자 자기 락만 잡게 하면 원격이 깨진 중간 상태를 799번 봤고 공유 레지스터 증가분 7105개 중 2092개가 사라졌다. 하드웨어 락을 공유하자 둘 다 0이 됐다.

> A normal spinlock relies on exclusive monitors or LSE atomics arbitrated by cache coherency, and the Cortex-M is usually outside that coherency domain, with different caches, and its RTOS doesn't know Linux's lock format anyway. A hardware spinlock decides ownership with a single access to a device register — read-to-acquire on TI, write-then-readback on STM32 HSEM and Qualcomm TCSR — so it works across cores and operating systems. In an emulation where each side only took its own lock, the remote saw a torn update 799 times and about 30 percent of its register increments were lost; with a shared lock both went to zero.

**꼬리 질문**: "hwspinlock을 잡고 오래 일해도 되나?" → 안 된다. timeout 변형도 busy-wait이고 기본 모드는 로컬 spinlock을 잡아 선점이 꺼진다. 임계 구역은 인덱스 갱신 수준으로.

**Q.** How do you protect a register block that is shared with a co-processor in a Linux driver?

**A.** DT에 `hwlocks`/`hwlock-names`로 원격과 합의한 락 번호를 적고, regmap을 쓰면 `regmap_config`에 `use_hwlock`, `hwlock_id`(`of_hwspin_lock_get_id_byname`), `hwlock_mode`를 넣는다. 그러면 regmap의 모든 접근이 `hwspin_lock_timeout`으로 감싸진다. 다만 보호 단위가 regmap 호출 하나라서, 여러 호출에 걸친 read-modify-write는 직접 락을 잡아야 한다. 어떤 비트를 누가 소유하는지 원격 펌웨어 팀과 문서로 정하는 것이 먼저다.

> I put the agreed lock number in the device tree with hwlocks and hwlock-names, and if the driver uses regmap I set use_hwlock, hwlock_id from of_hwspin_lock_get_id_byname, and hwlock_mode in the regmap_config, so every regmap access is wrapped in hwspin_lock_timeout. The protection is per regmap call, so a sequence of reads and writes needs the lock taken explicitly. And before any code, ownership of each bit is agreed with the firmware team.

**Q.** What is SCMI and how does it relate to mailboxes?

**A.** SCMI는 Arm이 정한 시스템 제어 프로토콜로, 클럭·전원 도메인·DVFS·센서·리셋을 Linux가 직접 레지스터로 만지지 않고 SCP나 EL3 펌웨어에 메시지로 요청하게 한다. 전송은 보통 공유 메모리(`arm,scmi-shmem`)에 메시지를 쓰고 메일박스 doorbell을 울리는 방식이고, SMC를 doorbell로 쓰는 전송도 있다. Linux 안에서 SCMI는 clk provider, power-domain provider, cpufreq로 보이므로 일반 드라이버는 `clk_prepare_enable`을 그대로 부른다. 디버그할 때는 그 아래가 메시지 기반이라는 것을 기억하고 SCMI 로그와 메일박스 timeout을 같이 본다.

> SCMI is Arm's System Control and Management Interface: instead of Linux poking clock and power registers, it sends requests to a system control processor or to EL3 firmware. The classic transport is a shared-memory buffer plus a mailbox doorbell; there's also an SMC-based transport. Inside Linux SCMI shows up as a clock provider, a power-domain provider and a cpufreq driver, so ordinary drivers still just call clk_prepare_enable — but when a clock doesn't come up you have to look at the SCMI and mailbox layer, not just register dumps.

---

## 15. 직접 해보기

1. (개념) i.MX MU처럼 데이터 레지스터가 4개인 메일박스에서, 센서 프레임 4 KB를 Cortex-M에서 Linux로 보내는 방법을 설계하라. 무엇을 메일박스에, 무엇을 공유 메모리에 두나? 정답/힌트: 공유 메모리 링에 프레임, 메일박스에는 "링 인덱스" 또는 doorbell만. 메시지마다 doorbell을 울리지 말고 묶어서(배칭) 울린다.
2. (코드) `h10_client`의 `rx_cb`를 "기다리는 seq와 다르면 버리고 카운트"하도록 고치고, 예제 7을 다시 돌려 늦은 응답이 몇 개 버려지는지 보라. 정답/힌트: `h->want_seq`를 송신 전에 기록, `rx_cb`에서 비교. seq1~3의 응답이 버려져야 한다.
3. (코드) `h10_fakembox`에 `peek_data` op를 구현하라: 원격이 응답을 만들었지만 아직 "RX IRQ"를 안 올린 상태를 흉내 내고, client가 `mbox_client_peek_data()`로 당겨 오게 하라. 정답/힌트: `remote_fn`에서 `mbox_chan_received_data` 대신 `pending = true`만 하고, `peek_data`에서 pending이면 `mbox_chan_received_data` 후 true 반환.
4. (관찰) 예제 5에서 POLL 모드의 `txpoll_period`를 1에서 10으로 바꾸면 bench의 txdone 중앙값이 어떻게 될지 예측하고 확인하라. 정답/힌트: 약 10 ms 근처로 늘어난다(첫 즉시 폴링에서 놓치면 다음 주기까지).
5. (코드) `h10_hwlock`의 Linux 쪽 루프를 `hwspin_lock_timeout_in_atomic()`으로 바꾸고, 세 번째 시나리오(hold 3000 us, timeout 1 ms)의 평균 대기가 어떻게 달라지는지 보라. 정답/힌트: jiffies(4 ms 단위) 대신 udelay 누적으로 재므로 timeout이 1 ms에 가까워진다. 단 `_in_atomic`은 로컬 spinlock을 안 잡으니 Linux 안의 직렬화는 호출자 책임이다.
6. (소스 읽기) 랩 트리에서 `drivers/remoteproc/imx_rproc.c`의 `imx_rproc_xtr_mbox_init()`을 읽고 `tx_block`, `tx_tout`, `knows_txdone`이 각각 무엇으로 설정되는지, 그리고 MU 컨트롤러의 txdone 방식과 어떻게 맞물리는지 설명하라. 정답/힌트: `tx_tout = 100`, `knows_txdone = false`, `tx_block`은 인자. MU는 `txdone_irq = true`라 client가 tick할 필요가 없다.
7. (DT) i.MX93 EVK의 `cm33` 노드(`imx93-11x11-evk.dts`)에서 `mboxes = <&mu1 0 1>, <&mu1 1 1>, <&mu1 3 1>`을 해독하라. 정답/힌트: MU1의 (TX, 1번), (RX, 1번), (RX doorbell, 1번) — `mbox-names = "tx", "rx", "rxdb"`.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| heterogeneous SoC | 이종 SoC | 종류가 다른 코어(Cortex-A, Cortex-M/R, DSP)가 한 칩에 있는 구조 |
| mailbox | 메일박스 | 상대 코어에 인터럽트를 거는 레지스터 블록. 짧은 데이터를 함께 실을 수 있다 |
| doorbell | 초인종 | 데이터 없이 "확인해 봐" 신호만 보내는 메일박스 채널 |
| MU / IPCC / MHU | 벤더별 메일박스 이름 | NXP Messaging Unit, ST Inter-Processor Communication Controller, Arm Message Handling Unit |
| `mbox_controller` | 컨트롤러 구조체 | 하드웨어 메일박스 블록 하나를 framework에 등록하는 단위 |
| `mbox_chan` | 채널 | 한 방향(또는 양방향) 통로. 한 번에 client 하나만 소유 |
| `mbox_client` | 클라이언트 | 채널을 쓰는 쪽이 콜백과 정책을 밝히는 구조체 |
| txdone | TX 완료 | 원격이 마지막 메시지를 가져갔다는 판정. IRQ, POLL, ACK 세 방식 |
| `knows_txdone` | — | POLL 채널을 ACK로 바꾸는 client 플래그 |
| `#mbox-cells` | — | DT에서 컨트롤러 phandle 뒤에 붙는 숫자 개수 |
| `of_xlate` | — | DT specifier를 채널 포인터로 바꾸는 컨트롤러 함수 |
| `MBOX_TX_QUEUE_LEN` | — | 채널당 TX 대기열 길이, 20 (헤더 상수) |
| hwspinlock | 하드웨어 스핀락 | 버스 접근 한 번으로 소유권이 정해지는 락 레지스터 |
| HSEM / TCSR mutex | 벤더별 hwspinlock 이름 | STM32 Hardware Semaphore, Qualcomm TCSR mutex |
| `#hwlock-cells` | — | DT에서 hwspinlock phandle 뒤 숫자 개수(보통 락 번호 1개) |
| coherency domain | 캐시 일관성 도메인 | 하드웨어가 캐시 라인 소유권을 자동으로 맞춰 주는 코어들의 범위 |
| exclusive monitor | 배타 모니터 | LDXR/STXR가 "그 사이 아무도 안 썼는지" 추적하는 하드웨어 |
| shared memory | 공유 메모리 | 두 코어가 모두 접근하는 SRAM 또는 DDR 영역. 메시지 본문을 둔다 |
| remoteproc | — | 원격 코어 펌웨어 로드·시작·정지·복구 framework (O1) |
| rpmsg | — | 공유 메모리 virtio vring 위의 메시지 채널 (O2) |
| vring | — | virtio의 링 버퍼. 디스크립터 테이블 + avail/used 링 |
| SCMI | System Control and Management Interface | 클럭·전원·DVFS·센서를 펌웨어에 메시지로 요청하는 Arm 표준 |
| SCP | System Control Processor | SCMI 요청을 처리하는 전용 저전력 코어 |
| `-ETIME` | 에러 62 | mailbox 블로킹 송신 타임아웃 |
| `-ENOBUFS` | 에러 105 | mailbox 채널 큐(20칸) 가득 참 |

---

## 17. 요약 & 체크리스트

이종 SoC에서 Linux와 RTOS·DSP 코어는 캐시 일관성도, 같은 OS의 락도 공유하지 않는다. 그래서 본문은 공유 메모리에, 알림은 메일박스 doorbell 인터럽트로, 상호 배제는 하드웨어 스핀락(또는 단일 생산자·소비자 링)으로 나눈다. Linux의 mailbox framework는 하드웨어를 아는 controller(`send_data`, `startup`/`shutdown`, `last_tx_done`, txdone irq/poll, `of_xlate`)와 프로토콜을 아는 client(`rx_callback`, `tx_done`, `tx_block`/`tx_tout`, `knows_txdone`)를 분리하고, core가 채널 소유권·20칸 큐·TX 완료 상태 기계를 맡는다. 콜백은 hardirq에서 불리고, 블로킹 송신 반환값은 토큰이며, POLL 모드는 주기만큼 손해를 본다. 타임아웃은 "모름"이라 sequence 번호와 재동기화 설계가 필요하다는 것을 가짜 컨트롤러로 직접 확인했다. hwspinlock framework는 로컬 spinlock 위에 하드웨어 락을 겹친 2단 구조이며, 각자 자기 OS의 락만 잡으면 데이터가 깨진다는 것을 측정했다(torn 799, lost 2092 → 0). 그 위에 remoteproc·rpmsg(O1~O2)와 SCMI가 올라간다.

- [ ] 공유 메모리만으로 코어 간 통신을 하면 무엇이 빠지는지(알림, 원자성) 두 가지를 말할 수 있다
- [ ] doorbell 메일박스와 data 메일박스의 차이와, 본문을 공유 메모리에 두는 이유를 설명할 수 있다
- [ ] `mbox_controller`/`mbox_chan_ops`를 채워 컨트롤러 드라이버 뼈대를 짤 수 있다
- [ ] `mbox_client`를 채우고 `mbox_request_channel_byname`/`mbox_send_message`/`mbox_free_channel`로 client를 짤 수 있다
- [ ] txdone IRQ·POLL·ACK의 차이와 `knows_txdone`의 정확한 효과를 소스 근거로 설명할 수 있다
- [ ] `rx_callback`·`tx_done`이 도는 컨텍스트와 그 안에서 하면 안 되는 일을 말할 수 있다
- [ ] 블로킹 송신 반환값이 토큰이라는 것과 큐 20칸 한계를 알고 있다
- [ ] 원격이 느리거나 죽었을 때 framework가 하는 일과 프로토콜 쪽 대책을 설명할 수 있다
- [ ] DT의 `#mbox-cells`/`mboxes`/`mbox-names`, `#hwlock-cells`/`hwlocks`/`hwlock-names`를 실물 dtsi에서 해독할 수 있다
- [ ] 일반 spinlock이 코어(OS) 사이에서 안 되는 이유와 hwspinlock의 2단 구조를 설명할 수 있다
- [ ] hwspinlock timeout 변형들의 차이(로컬 락, IRQ, jiffies vs udelay)를 말할 수 있다
- [ ] regmap `use_hwlock`의 동작과 그 보호 단위의 한계를 설명할 수 있다

## 참고 자료

- docs.kernel.org — The Common Mailbox Framework: https://docs.kernel.org/driver-api/mailbox.html
- docs.kernel.org — Hardware Spinlock Framework: https://docs.kernel.org/locking/hwspinlock.html
- docs.kernel.org — Remote Processor Framework: https://docs.kernel.org/staging/remoteproc.html
- docs.kernel.org — Remote Processor Messaging (rpmsg) Framework: https://docs.kernel.org/staging/rpmsg.html
- 커널 소스 (랩 트리 `/var/lab/src/linux-6.18.55`, 또는 elixir.bootlin.com): `drivers/mailbox/mailbox.c`, `include/linux/mailbox_controller.h`, `include/linux/mailbox_client.h`, `drivers/mailbox/imx-mailbox.c`, `drivers/mailbox/stm32-ipcc.c`, `drivers/mailbox/arm_mhu.c`, `drivers/hwspinlock/hwspinlock_core.c`, `drivers/hwspinlock/stm32_hwspinlock.c`, `drivers/hwspinlock/omap_hwspinlock.c`, `drivers/base/regmap/regmap.c`, `drivers/firmware/arm_scmi/transports/mailbox.c`, `drivers/remoteproc/stm32_rproc.c`
- DT 바인딩: `Documentation/devicetree/bindings/mailbox/`, `Documentation/devicetree/bindings/hwlock/`, `Documentation/devicetree/bindings/firmware/arm,scmi.yaml`
- Arm SCMI 명세: Arm "System Control and Management Interface" (developer.arm.com, 문서 번호 DEN0056)
- 각 SoC Reference Manual: NXP i.MX 8M Plus RM (MU 장), ST RM0436 STM32MP15 (IPCC·HSEM 장), TI AM62x TRM (Mailbox·Spinlock 장) — 이 노트에서 "기억"으로 표시한 하드웨어 세부는 여기서 확인
