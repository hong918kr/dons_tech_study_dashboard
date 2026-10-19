# S7. SocketCAN 실습 프로젝트 — CAN 센서 노드 + 게이트웨이 + 로거를 만들고 고장 주입·CI까지 돌린다

> **이 노트를 다 읽으면**: DBC를 직접 설계하고 CRC8 + alive counter(E2E)로 프레임을 보호할 수 있다 · bare-metal Cortex-M 센서 ECU를 slcan으로 Linux에 붙여 진짜 `can_dev` 인터페이스(에러 상태·bus-off 포함)를 만들 수 있다 · 필터·`SO_TIMESTAMPING`·데드라인 감시·bus-off 재시작을 하는 C 게이트웨이 데몬과 회전 로거, python-can/cantools 분석 스크립트를 짤 수 있다 · 고장 주입 25개 테스트를 QEMU에서 자동으로 돌려 `RESULT: PASS`를 내는 CI 스크립트를 GitHub에 올릴 수 있다
> **왜 필요한가**: 로봇·차량·드론 회사 면접에서 "CAN으로 센서 데이터를 받는데, 프레임이 늦거나 깨지거나 버스가 죽으면 소프트웨어가 어떻게 반응하나?"는 단골 질문이고, 말로 하는 답보다 **돌아가는 레포 + 측정 숫자 + 고장 주입 테스트**가 훨씬 강하다
> **Don 기준 난이도**: CRC·카운터·타임아웃·버스 에러 카운터·레지스터 직접 접근 펌웨어는 이미 아는 것 / 새로 해 볼 것은 SocketCAN 소켓 옵션(필터, 에러 마스크, 타임스탬프, overflow 카운터), 유저 공간 데몬의 이벤트 루프 설계, DBC → 생성 C 코드, 로그 포맷 호환, QEMU 두 개를 엮는 자동 테스트
> **선행 노트**: Q4(CAN 실무 — can-utils, DBC, slcan 에러 주입, **이 프로젝트의 직접 선행**), I10(CAN 서브시스템·can_dev·비트 타이밍), A4(epoll·timerfd), A6(signal), A9(소켓), H8(tty·line discipline), O3(Linux ↔ MCU 링크 설계), P6(테스트·CI), Q7(센서 데이터 품질 툴). 같이 보면 좋은 것: D6(DT overlay), D7(binding 검증), N4(Linux vs MCU 분담), S1(QEMU 랩)

---

## 0. 큰 그림 — 무엇을 만들고 왜

Q4는 can-utils와 소켓 API를 **도구 단위**로 익혔고, I10은 `can0`이 커널 안에서 어떻게 생기는지를 **드라이버 쪽**에서 팠다. 이 노트는 그 둘을 합쳐 **포트폴리오 프로젝트 하나**를 끝까지 만든다. 개념은 다시 설명하지 않고, 필요한 곳에서 "Q4 8.4절" 식으로 ID만 건다.

만드는 것은 로봇이나 차량의 센서 키트에서 흔한 구조다: **센서 MCU가 CAN으로 IMU·온도 프레임을 뿌리고, Linux SoC의 게이트웨이가 받아서 검증하고, 상위 버스로 다시 내보내고, 모든 것을 로그로 남긴다.**

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">VM 호스트 (Lima elinux) — ci/run_ci.sh 가 전부 띄우고 판정한다</text><rect x="10" y="30" width="200" height="150" rx="4" fill="#3f9a6b" fill-opacity="0.04" stroke="#3f9a6b" stroke-dasharray="5 4"/><text x="20.0" y="50.0" font-size="13" text-anchor="start" font-weight="bold">QEMU 1: mps2-an385</text><rect x="17" y="62" width="186" height="52" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="110.0" y="84.8" font-size="12" text-anchor="middle">sensor_ecu.elf (M3)</text><text x="110.0" y="99.8" font-size="12" text-anchor="middle">IMU 100 Hz · TEMP 10 Hz</text><rect x="17" y="122" width="186" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="110.0" y="142.8" font-size="12" text-anchor="middle">CRC8 + 4비트 카운터</text><text x="110.0" y="157.8" font-size="12" text-anchor="middle">0x7E0 = 고장 주입</text><rect x="240" y="30" width="430" height="330" rx="4" fill="#4a7bd0" fill-opacity="0.04" stroke="#4a7bd0" stroke-dasharray="5 4"/><text x="250.0" y="50.0" font-size="13" text-anchor="start" font-weight="bold">QEMU 2: Image-dom + rootfs-dom (aarch64 Linux)</text><rect x="254" y="62" width="146" height="52" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="327.0" y="84.8" font-size="12" text-anchor="middle">pci-serial</text><text x="327.0" y="99.8" font-size="12" text-anchor="middle">/dev/ttyS0</text><rect x="254" y="132" width="146" height="52" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="327.0" y="154.8" font-size="12" text-anchor="middle">s7sl0 (slcan)</text><text x="327.0" y="169.8" font-size="12" text-anchor="middle">센서 버스 · can_dev</text><rect x="410" y="62" width="246" height="70" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="533.0" y="86.3" font-size="12" text-anchor="middle" font-weight="bold">canmond (C 데몬)</text><text x="533.0" y="101.3" font-size="12" text-anchor="middle">필터·E2E·데드라인·bus-off</text><text x="533.0" y="116.3" font-size="12" text-anchor="middle">E2E 통과 프레임만 전달</text><rect x="410" y="150" width="246" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="533.0" y="168.8" font-size="12" text-anchor="middle">s7vc1 (vcan) — 백본 버스</text><text x="533.0" y="183.8" font-size="12" text-anchor="middle">센서 프레임 + GW_STATUS 0x500</text><rect x="254" y="214" width="196" height="56" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="352.0" y="238.8" font-size="12" text-anchor="middle">canlogd (모든 CAN 버스)</text><text x="352.0" y="253.8" font-size="12" text-anchor="middle">candump -l 형식 · 회전</text><rect x="470" y="214" width="186" height="56" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="563.0" y="238.8" font-size="12" text-anchor="middle">bus.log, bus.log.1 ...</text><text x="563.0" y="253.8" font-size="12" text-anchor="middle">(회전된 로그 파일)</text><rect x="254" y="290" width="402" height="56" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="455.0" y="314.8" font-size="12" text-anchor="middle">tests/guest_test.sh — 고장 주입 + TAP 판정</text><text x="455.0" y="329.8" font-size="12" text-anchor="middle">analyze.py (python-can + cantools) · canplayer 재생 회귀</text><line x1="203" y1="88" x2="252" y2="88" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="252" y1="98" x2="203" y2="98" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><text x="224.0" y="80.0" font-size="11" text-anchor="middle">UART</text><line x1="327" y1="114" x2="327" y2="130" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="400" y1="150" x2="408" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="533" y1="132" x2="533" y2="148" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="327" y1="184" x2="327" y2="212" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="450" y1="242" x2="468" y2="242" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="560" y1="194" x2="560" y2="212" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4" marker-end="url(#ah1)"/><rect x="10" y="214" width="200" height="132" rx="4" fill="#888" fill-opacity="0.04" stroke="#888" stroke-dasharray="5 4"/><text x="20.0" y="234.0" font-size="13" text-anchor="start" font-weight="bold">VM 호스트 측 CI</text><rect x="17" y="246" width="186" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="110.0" y="270.3" font-size="12" text-anchor="middle">-Werror 빌드 + 단위</text><rect x="17" y="294" width="186" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="110.0" y="318.3" font-size="12" text-anchor="middle">ecu_smoke.py (ECU 단독)</text><line x1="203" y1="318" x2="252" y2="318" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4" marker-end="url(#ah1)"/><text x="10.0" y="386.0" font-size="12" text-anchor="start">링크: unix 소켓 link.sock (Linux = 서버, ECU = reconnect 클라이언트), 콘솔: console.sock</text><text x="10.0" y="406.0" font-size="12" text-anchor="start">빨강 = 진짜 can_dev(에러 상태·bus-off 있음), 파랑/주황 = 유저 공간 프로세스·가상 버스</text><text x="10.0" y="426.0" font-size="12" text-anchor="start">판정: 단위 6 + ECU 6 + 시스템 25 테스트 → 마지막 줄 RESULT: PASS / FAIL, 종료 코드 0 / 1</text><text x="10.0" y="446.0" font-size="12" text-anchor="start">실보드로 옮기면: s7sl0 → can0 (MCP2515/MCP2518FD, SPI), ECU → 실제 MCU 노드 (14절)</text></svg>
```

그림 1 — 이 프로젝트의 전체 구성. 왼쪽 QEMU는 Cortex-M3 센서 ECU, 오른쪽 QEMU는 Linux 게이트웨이다. 둘은 UART(slcan)로 묶여 Linux 쪽에 진짜 `can_dev` 인터페이스 `s7sl0`이 생긴다. VM 호스트의 `ci/run_ci.sh`가 빌드부터 판정까지 전부 자동으로 한다.

bare-metal로 비유하면 이렇다. 센서 ECU는 Don이 SSD에서 짜던 **주기 태스크 + 패킷 CRC를 붙이는 펌웨어**다. 게이트웨이 데몬은 **watchdog 겸 라우터 태스크** — 메시지마다 "마지막으로 언제 왔나"를 타이머로 감시하고(AUTOSAR에서는 deadline monitoring), CRC·시퀀스 번호를 보고(E2E protection), 버스 컨트롤러가 bus-off에 빠지면 재초기화한다. 로거는 **트레이스 버퍼를 플래시에 흘려 쓰는 로깅 태스크**고, 분석 스크립트는 그 로그로 지터·드롭·버스 부하를 계산하는 오프라인 툴이다.

### 0.1 최종 산출물 — 이게 끝나면 GitHub에 올라가는 것

`~/.elinux_scratch/S7/can-sensor-kit/` (git 레포, 커밋 2개, 23개 파일):

| 경로 | 내용 | 줄 수 |
|---|---|---|
| `dbc/s7_sensor_kit.dbc` | 메시지 5개 (IMU 가속·자이로 100 Hz, 온도 10 Hz, 게이트웨이 상태, 고장 명령) | 60 |
| `common/e2e.h` | CRC8 SAE-J1850 + 4비트 카운터 검사 — 펌웨어와 데몬이 **같은 헤더**를 쓴다 | 49 |
| `firmware/sensor_ecu.c` | Cortex-M3 bare-metal ECU, slcan 프로토콜, 고장 주입 5종 | 125 |
| `gen/s7_sensor_kit.[ch]` | `cantools generate_c_source`로 만든 pack/unpack 코드 | 2692 |
| `daemon/canmond.c` | 게이트웨이 데몬 (필터, 타임스탬프, E2E, 데드라인, bus-off 재시작, 라우팅) | 334 |
| `logger/canlogd.c` | candump `-l` 호환 로거 + 크기 회전 | 103 |
| `tools/analyze.py` | python-can + cantools: 주기·지터·드롭·에러 프레임·버스 부하·전달 지연 | 116 |
| `tests/` | 단위 테스트(C), ECU 단독 테스트(Python), 시스템 테스트 25개(bash, TAP) | — |
| `ci/run_ci.sh`, `ci/qemu_ctl.py` | QEMU 두 개를 띄워 전부 돌리고 PASS/FAIL 판정 | 117 |
| `hw/*.dts` | MCP2515 / MCP2518FD DT overlay (실보드용) | — |

그리고 **면접에서 보여 줄 숫자** (전부 이 노트에서 실제로 잰 값, TCG 에뮬레이션이라 "방법"을 보여 주는 숫자다):

| 항목 | 값 |
|---|---|
| IMU_ACCEL 수신 주기 (idle 게스트, 20 s) | 평균 10.12 ms, std 1.19 ms, p99 13.41 ms, 최대 29.96 ms |
| 송신 정지 → MISSING 판정 | 정지 후 102~106 ms (타임아웃 = 10 × 주기) |
| bus-off → 데이터 복구 | 약 150 ms (backoff 100 ms + 재오픈 + 3프레임 디바운스) |
| 나쁜 CRC 5개 주입 시 백본으로 새어 나간 프레임 | own routing 0개 / can-gw 5개 |
| 전달 지연 p50 (own routing / can-gw) | 809 us / 122 us |
| 전체 CI | 95~99 s, 단위 6 + ECU 6 + 시스템 25 테스트 PASS (연속 2회) |

### 0.2 실행 환경

- VM 호스트: Lima `elinux`, `uname -r` = `7.0.0-38-generic` (aarch64), gcc 15.2.0, arm-none-eabi-gcc 14.2.1, QEMU 10.2.1, can-utils 2023.03, dtc 1.7.2.
- Linux 게스트: 공용 랩 `Image-dom` (`6.18.55-dom`, CAN raw/bcm/gw 내장, slcan·vcan 모듈) + `rootfs-dom` (Ubuntu 24.04 + systemd, can-utils, python3-can 4.3.1, cantools 45.0.0). rootfs는 qcow2 오버레이로만 띄웠다. 게스트 1 GB RAM, `-smp 2`, 커널 인자 `apparmor=0`.
- 센서 ECU: `qemu-system-arm -M mps2-an385` (Cortex-M3, 25 MHz SysTick 기준).
- C 바이너리는 VM 호스트에서 `gcc -static`으로 빌드해서 9p 공유 폴더(`/mnt/s7`)로 게스트에 넘겼다 (둘 다 aarch64라 그대로 돈다). 경고 0개, CI에서는 `-Werror`.

주의: 모든 시간 숫자는 **KVM 없는 TCG 에뮬레이션**에서, 다른 노트 작성자 6명이 같은 VM을 쓰는 동안(load average 3~7) 잰 값이다. 실보드 지연이 아니다. 이 노트에서는 오히려 이 "나쁜 환경"이 데몬 설계의 약점을 두 번이나 드러내 줬다 (7.6절, 8.3절).

---

## 1. 준비물과 레포 구조

### 1.1 준비물

| 준비물 | 어디서 | 확인 명령 |
|---|---|---|
| Lima VM `elinux` + `/var/lab` 랩 | S1 노트 | `limactl shell elinux -- cat /var/lab/README.txt` |
| `Image-dom`, `rootfs-dom.ext4` | 공용 랩 (읽기 전용) | `ls /var/lab/out/Image-dom` |
| `arm-none-eabi-gcc`, `qemu-system-arm` | VM 호스트 | `arm-none-eabi-gcc --version` |
| python-can, cantools | **게스트** rootfs-dom (VM 호스트에는 없다) | 게스트에서 `python3 -c "import can,cantools"` |
| Q4 8절 (slcan 에러 주입 원리) | 노트 | — |

VM 호스트에 python-can이 없다는 점이 설계에 영향을 줬다: **cantools 코드 생성과 분석 스크립트는 게스트에서** 돌리고, VM 호스트 쪽 CI 스크립트는 Python 표준 라이브러리만 쓴다.

### 1.2 레포 구조

```
can-sensor-kit/
  README.md            .gitignore
  dbc/s7_sensor_kit.dbc
  common/e2e.h                      <- firmware + daemon share this
  firmware/sensor_ecu.c sensor_ecu.ld Makefile
  gen/s7_sensor_kit.c s7_sensor_kit.h   <- generated by cantools, committed
  daemon/canmond.c Makefile
  logger/canlogd.c Makefile
  tools/analyze.py kit_up.sh
  tests/test_e2e.c ecu_smoke.py guest_test.sh
  ci/run_ci.sh qemu_ctl.py
  hw/s7-mcp2515-overlay.dts s7-mcp2518fd-overlay.dts fake-base.dts
```

생성 코드(`gen/`)를 커밋하는 이유: 빌드 머신(VM 호스트, 실보드의 Yocto 빌드)에 cantools가 없어도 빌드가 되고, DBC가 바뀌면 diff로 리뷰할 수 있다. 대신 "DBC만 고치고 재생성을 잊는" 실수가 생기므로, 확장 과제로 CI에 "재생성 후 diff가 비었는가" 검사를 넣는다(14절).

빌드 산출물은 레포 밖(`/var/tmp/S7/…`)에 만든다. Mac 쪽 레포 디렉터리는 VM에서 읽기 전용이기 때문이기도 하지만, 원래 그래야 한다 — 각 `Makefile`이 `OUT=` 변수를 받는다.

---

## 2. 단계 1 — DBC 설계: 무엇을 어떤 비트에 싣나

### 2.1 메시지 설계 원칙

센서 프레임 하나를 설계할 때 정할 것은 다섯 가지다: ID(우선순위), 주기, 페이로드 레이아웃(엔디언·스케일·부호), **무결성 필드(CRC·카운터)**, 그리고 수신 측의 **타임아웃 정책**. 이 프로젝트의 선택:

- ID가 작을수록 중재에서 이긴다(I10 1.2절). 센서 데이터 0x110~0x120, 게이트웨이 상태 0x500, 진단·고장 명령은 맨 뒤 0x7E0.
- IMU 100 Hz(10 ms), 온도 10 Hz(100 ms). 주기는 DBC 속성 `GenMsgCycleTime`에 적는다 — cantools가 이 값을 C 매크로 `..._CYCLE_TIME_MS`로 뽑아 주고, 데몬은 그 매크로로 타임아웃을 계산한다. **주기의 진실은 DBC 한 곳**에만 있다.
- byte0 = CRC8, byte1 하위 4비트 = alive counter, 상위 4비트 = 상태. AUTOSAR E2E Profile 1에서 영감을 받은 단순화 버전이다(표준을 그대로 구현한 것은 아니다).
- CRC 계산에 **CAN ID를 섞는다** (AUTOSAR의 "Data ID" 개념). 같은 페이로드가 엉뚱한 ID로 라우팅되거나 게이트웨이가 ID를 잘못 바꾸면 CRC가 틀린다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">프레임 레이아웃 (Intel little-endian, 비트 번호 = 바이트×8 + 비트)</text><text x="168.5" y="40.0" font-size="12" text-anchor="middle">byte 0</text><text x="233.5" y="40.0" font-size="12" text-anchor="middle">byte 1</text><text x="298.5" y="40.0" font-size="12" text-anchor="middle">byte 2</text><text x="363.5" y="40.0" font-size="12" text-anchor="middle">byte 3</text><text x="428.5" y="40.0" font-size="12" text-anchor="middle">byte 4</text><text x="493.5" y="40.0" font-size="12" text-anchor="middle">byte 5</text><text x="558.5" y="40.0" font-size="12" text-anchor="middle">byte 6</text><text x="623.5" y="40.0" font-size="12" text-anchor="middle">byte 7</text><text x="10.0" y="78.0" font-size="12" text-anchor="start">0x110 IMU_ACCEL</text><rect x="137.0" y="52" width="63.0" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="168.5" y="76.0" font-size="11" text-anchor="middle">CRC8</text><rect x="202.0" y="52" width="63.0" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="233.5" y="69.0" font-size="11" text-anchor="middle">CNT</text><text x="233.5" y="83.0" font-size="11" text-anchor="middle">STAT</text><rect x="267.0" y="52" width="128.0" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="331.0" y="69.0" font-size="11" text-anchor="middle">ACCEL_X</text><text x="331.0" y="83.0" font-size="11" text-anchor="middle">0.01 m/s2</text><rect x="397.0" y="52" width="128.0" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="461.0" y="69.0" font-size="11" text-anchor="middle">ACCEL_Y</text><text x="461.0" y="83.0" font-size="11" text-anchor="middle">int16</text><rect x="527.0" y="52" width="128.0" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="591.0" y="69.0" font-size="11" text-anchor="middle">ACCEL_Z</text><text x="591.0" y="83.0" font-size="11" text-anchor="middle">int16</text><text x="10.0" y="128.0" font-size="12" text-anchor="start">0x111 IMU_GYRO</text><rect x="137.0" y="102" width="63.0" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="168.5" y="126.0" font-size="11" text-anchor="middle">CRC8</text><rect x="202.0" y="102" width="63.0" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="233.5" y="119.0" font-size="11" text-anchor="middle">CNT</text><text x="233.5" y="133.0" font-size="11" text-anchor="middle">STAT</text><rect x="267.0" y="102" width="128.0" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="331.0" y="119.0" font-size="11" text-anchor="middle">GYRO_X</text><text x="331.0" y="133.0" font-size="11" text-anchor="middle">0.01 deg/s</text><rect x="397.0" y="102" width="128.0" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="461.0" y="126.0" font-size="11" text-anchor="middle">GYRO_Y</text><rect x="527.0" y="102" width="128.0" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="591.0" y="126.0" font-size="11" text-anchor="middle">GYRO_Z</text><text x="10.0" y="178.0" font-size="12" text-anchor="start">0x120 TEMP_STATUS</text><rect x="137.0" y="152" width="63.0" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="168.5" y="176.0" font-size="11" text-anchor="middle">CRC8</text><rect x="202.0" y="152" width="63.0" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="233.5" y="169.0" font-size="11" text-anchor="middle">CNT</text><text x="233.5" y="183.0" font-size="11" text-anchor="middle">STAT</text><rect x="267.0" y="152" width="128.0" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="331.0" y="169.0" font-size="11" text-anchor="middle">TEMP_IMU</text><text x="331.0" y="183.0" font-size="11" text-anchor="middle">0.01 degC</text><rect x="397.0" y="152" width="128.0" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="461.0" y="169.0" font-size="11" text-anchor="middle">SUPPLY_MV</text><text x="461.0" y="183.0" font-size="11" text-anchor="middle">mV</text><rect x="527.0" y="152" width="128.0" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="591.0" y="169.0" font-size="11" text-anchor="middle">UPTIME_S</text><text x="591.0" y="183.0" font-size="11" text-anchor="middle">s</text><text x="10.0" y="228.0" font-size="12" text-anchor="start">0x500 GW_STATUS</text><rect x="137.0" y="202" width="63.0" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="168.5" y="226.0" font-size="11" text-anchor="middle">ALIVE</text><rect x="202.0" y="202" width="95.5" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="249.8" y="219.0" font-size="11" text-anchor="middle">A G T BUS</text><text x="249.8" y="233.0" font-size="11" text-anchor="middle">3비트 × 4</text><rect x="299.5" y="202" width="30.5" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="314.8" y="226.0" font-size="11" text-anchor="middle">-</text><rect x="332.0" y="202" width="128.0" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="396.0" y="226.0" font-size="11" text-anchor="middle">INTEGRITY_ERRS</text><rect x="462.0" y="202" width="128.0" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="526.0" y="226.0" font-size="11" text-anchor="middle">TIMEOUT_TOTAL</text><rect x="592.0" y="202" width="63.0" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="623.5" y="226.0" font-size="11" text-anchor="middle">RST</text><text x="10.0" y="278.0" font-size="12" text-anchor="start">0x7E0 FAULT_CMD</text><rect x="137.0" y="252" width="63.0" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="168.5" y="276.0" font-size="11" text-anchor="middle">CODE</text><rect x="202.0" y="252" width="63.0" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="233.5" y="276.0" font-size="11" text-anchor="middle">ARG</text><text x="10.0" y="318.0" font-size="12" text-anchor="start">CRC8 SAE-J1850 (poly 0x1D, init 0xFF, xorout 0xFF) = f(id_lo, id_hi, byte1..7) → byte0</text><text x="10.0" y="338.0" font-size="12" text-anchor="start">ID를 CRC에 섞는 이유: 같은 페이로드가 다른 ID로 잘못 라우팅돼도 검출 (AUTOSAR의 Data ID 개념)</text><text x="10.0" y="358.0" font-size="12" text-anchor="start">byte1: CNT = 하위 4비트, STAT = 상위 4비트 · A/G/T = 메시지별 상태, RST = RESTARTS</text><text x="10.0" y="378.0" font-size="12" text-anchor="start">상태 값 0 NO_DATA … 4 BAD_DATA · BUS 0 ACTIVE … 4 RESTARTING · FAULT 1 MUTE … 5 DELAY</text></svg>
```

그림 2 — 프레임 레이아웃. 신호는 모두 Intel(little-endian, DBC의 `@1`) 순서다. GW_STATUS는 3비트 상태 4개를 비트 8~19에 빽빽하게 넣었다.

### 2.2 DBC 파일

무엇을 확인하는 파일인가: 메시지 5개, 신호의 비트 위치·스케일·단위, 주기 속성, 상태 값 테이블(`VAL_`).

```text
VERSION "s7-can-sensor-kit 1.0"

NS_ :

BS_:

BU_: SENSOR_ECU GATEWAY TESTER

BO_ 272 IMU_ACCEL: 8 SENSOR_ECU
 SG_ ACCEL_CRC : 0|8@1+ (1,0) [0|255] "" GATEWAY
 SG_ ACCEL_CNT : 8|4@1+ (1,0) [0|15] "" GATEWAY
 SG_ ACCEL_STAT : 12|4@1+ (1,0) [0|15] "" GATEWAY
 SG_ ACCEL_X : 16|16@1- (0.01,0) [-327.68|327.67] "m/s2" GATEWAY
 SG_ ACCEL_Y : 32|16@1- (0.01,0) [-327.68|327.67] "m/s2" GATEWAY
 SG_ ACCEL_Z : 48|16@1- (0.01,0) [-327.68|327.67] "m/s2" GATEWAY

BO_ 273 IMU_GYRO: 8 SENSOR_ECU
 SG_ GYRO_CRC : 0|8@1+ (1,0) [0|255] "" GATEWAY
 SG_ GYRO_CNT : 8|4@1+ (1,0) [0|15] "" GATEWAY
 SG_ GYRO_STAT : 12|4@1+ (1,0) [0|15] "" GATEWAY
 SG_ GYRO_X : 16|16@1- (0.01,0) [-327.68|327.67] "deg/s" GATEWAY
 SG_ GYRO_Y : 32|16@1- (0.01,0) [-327.68|327.67] "deg/s" GATEWAY
 SG_ GYRO_Z : 48|16@1- (0.01,0) [-327.68|327.67] "deg/s" GATEWAY

BO_ 288 TEMP_STATUS: 8 SENSOR_ECU
 SG_ TEMP_CRC : 0|8@1+ (1,0) [0|255] "" GATEWAY
 SG_ TEMP_CNT : 8|4@1+ (1,0) [0|15] "" GATEWAY
 SG_ TEMP_STAT : 12|4@1+ (1,0) [0|15] "" GATEWAY
 SG_ TEMP_IMU : 16|16@1- (0.01,0) [-327.68|327.67] "degC" GATEWAY
 SG_ SUPPLY_MV : 32|16@1+ (1,0) [0|65535] "mV" GATEWAY
 SG_ UPTIME_S : 48|16@1+ (1,0) [0|65535] "s" GATEWAY

BO_ 1280 GW_STATUS: 8 GATEWAY
 SG_ GW_ALIVE : 0|8@1+ (1,0) [0|255] "" TESTER
 SG_ ACCEL_STATE : 8|3@1+ (1,0) [0|7] "" TESTER
 SG_ GYRO_STATE : 11|3@1+ (1,0) [0|7] "" TESTER
 SG_ TEMP_STATE : 14|3@1+ (1,0) [0|7] "" TESTER
 SG_ BUS_STATE : 17|3@1+ (1,0) [0|7] "" TESTER
 SG_ INTEGRITY_ERRS : 24|16@1+ (1,0) [0|65535] "" TESTER
 SG_ TIMEOUT_TOTAL : 40|16@1+ (1,0) [0|65535] "" TESTER
 SG_ RESTARTS : 56|8@1+ (1,0) [0|255] "" TESTER

BO_ 2016 ECU_FAULT_CMD: 2 TESTER
 SG_ FAULT_CODE : 0|8@1+ (1,0) [0|255] "" SENSOR_ECU
 SG_ FAULT_ARG : 8|8@1+ (1,0) [0|255] "" SENSOR_ECU

CM_ BO_ 272 "E2E protected: CRC8 SAE-J1850 over (id_lo, id_hi, byte1..7), 4-bit alive counter";
CM_ BO_ 1280 "Gateway health summary, republished on the backbone bus";
CM_ BO_ 2016 "Lab-only fault injection command to the simulated sensor ECU";
BA_DEF_ BO_ "GenMsgCycleTime" INT 0 65535;
BA_DEF_DEF_ "GenMsgCycleTime" 0;
BA_ "GenMsgCycleTime" BO_ 272 10;
BA_ "GenMsgCycleTime" BO_ 273 10;
BA_ "GenMsgCycleTime" BO_ 288 100;
BA_ "GenMsgCycleTime" BO_ 1280 100;
VAL_ 1280 ACCEL_STATE 0 "NO_DATA" 1 "OK" 2 "LATE" 3 "MISSING" 4 "BAD_DATA" ;
VAL_ 1280 GYRO_STATE 0 "NO_DATA" 1 "OK" 2 "LATE" 3 "MISSING" 4 "BAD_DATA" ;
VAL_ 1280 TEMP_STATE 0 "NO_DATA" 1 "OK" 2 "LATE" 3 "MISSING" 4 "BAD_DATA" ;
VAL_ 1280 BUS_STATE 0 "ACTIVE" 1 "WARNING" 2 "PASSIVE" 3 "BUS_OFF" 4 "RESTARTING" ;
VAL_ 2016 FAULT_CODE 0 "NONE" 1 "MUTE" 2 "BAD_CRC" 3 "CNT_JUMP" 4 "BUS_OFF" 5 "DELAY" ;
```

### 2.3 cantools로 레이아웃 검증

DBC는 손으로 쓰면 비트가 겹치거나 비는 실수가 잦다. `cantools dump`가 그림을 그려 준다 (게스트에서 실행, 앞부분만):

```sh
cantools dump s7_sensor_kit.dbc | head -60
```

```text
================================= Messages =================================

  ------------------------------------------------------------------------

  Name:           IMU_ACCEL
  Id:             0x110
  Length:         8 bytes
  Cycle time:     10 ms
  Senders:        SENSOR_ECU
  Layout:

                          Bit

             7   6   5   4   3   2   1   0
           +---+---+---+---+---+---+---+---+
         0 |<-----------------------------x|
           +---+---+---+---+---+---+---+---+
             +-- ACCEL_CRC
           +---+---+---+---+---+---+---+---+
         1 |<-------------x|<-------------x|
           +---+---+---+---+---+---+---+---+
             |               +-- ACCEL_CNT
             +-- ACCEL_STAT
           +---+---+---+---+---+---+---+---+
         2 |------------------------------x|
     B     +---+---+---+---+---+---+---+---+
     y   3 |<------------------------------|
     t     +---+---+---+---+---+---+---+---+
     e       +-- ACCEL_X
           +---+---+---+---+---+---+---+---+
         4 |------------------------------x|
           +---+---+---+---+---+---+---+---+
         5 |<------------------------------|
           +---+---+---+---+---+---+---+---+
             +-- ACCEL_Y
           +---+---+---+---+---+---+---+---+
         6 |------------------------------x|
           +---+---+---+---+---+---+---+---+
         7 |<------------------------------|
           +---+---+---+---+---+---+---+---+
             +-- ACCEL_Z

  Signal tree:
```

출력에서 볼 것: `Cycle time: 10 ms`가 DBC 속성에서 읽혔다. byte 1에 `ACCEL_CNT`(하위 니블)와 `ACCEL_STAT`(상위 니블)이 나란히 있고, 16비트 신호가 byte 2~3에 걸쳐 있다(`x`가 LSB 위치).

**체크포인트**: 이 단계가 끝나면 `cantools dump`가 에러 없이 5개 메시지를 보여 주고, 모든 8바이트 메시지의 64비트가 빈틈없이 채워져 있어야 한다(GW_STATUS의 비트 20~23만 예약).

**흔한 실패**:

- `cantools`가 신호 이름을 snake_case로 바꾸면서 `E2E_ERR_TOTAL`이 `e2_e_err_total`이 됐다 — 실제로 처음 생성했을 때 그랬다. 생성 C 코드의 필드 이름이 이상하면 DBC 이름을 고친다(이 레포는 `INTEGRITY_ERRS`, 상태 값 `BAD_DATA`로 바꿨다).
- 부호 있는 신호에 `@1+`(unsigned)를 쓰면 음수 가속도가 655.35 근처 값으로 디코드된다. 부호는 `-`.

---

## 3. 단계 2 — 공용 E2E 헤더와 단위 테스트

### 3.1 코드

무엇을 확인하는 코드인가: CRC8 SAE-J1850(poly 0x1D, init 0xFF, xorout 0xFF)과 카운터 판정을 **헤더 하나**로 만들어, Cortex-M3 펌웨어와 Linux 데몬이 똑같이 쓰게 한다. 결과는 4가지: OK, BAD_CRC, REPEATED(같은 카운터 다시 옴), GAP(중간이 빠짐 — 몇 개인지 `lost`로).

```c
/* e2e.h - shared by firmware (Cortex-M3) and gateway daemon (Linux).
 * CRC8 SAE-J1850 (poly 0x1D, init 0xFF, xorout 0xFF; check("123456789") = 0x4B)
 * over [id_lo, id_hi, data[1..7]]; result stored in data[0].
 * data[1] low nibble = 4-bit alive counter (wraps 15 -> 0). */
#ifndef S7_E2E_H
#define S7_E2E_H
#include <stdint.h>

static inline uint8_t e2e_crc8_update(uint8_t crc, const uint8_t *p, unsigned n)
{
	while (n--) {
		crc ^= *p++;
		for (int b = 0; b < 8; b++)
			crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x1D : crc << 1);
	}
	return crc;
}

static inline uint8_t e2e_crc8(const uint8_t *p, unsigned n)
{
	return (uint8_t)(e2e_crc8_update(0xFF, p, n) ^ 0xFF);
}

static inline uint8_t e2e_frame_crc(uint32_t id, const uint8_t d[8])
{
	uint8_t hdr[2] = { (uint8_t)(id & 0xFF), (uint8_t)((id >> 8) & 0xFF) };
	uint8_t crc = e2e_crc8_update(0xFF, hdr, 2);
	return (uint8_t)(e2e_crc8_update(crc, d + 1, 7) ^ 0xFF);
}

/* E2E check result */
enum e2e_res { E2E_OK, E2E_BAD_CRC, E2E_REPEATED, E2E_GAP };

static inline enum e2e_res e2e_check(uint32_t id, const uint8_t d[8],
				     int last_cnt, unsigned *lost)
{
	*lost = 0;
	if (e2e_frame_crc(id, d) != d[0])
		return E2E_BAD_CRC;
	int cnt = d[1] & 0x0F;
	if (last_cnt < 0)
		return E2E_OK;                    /* first frame: accept */
	unsigned delta = (unsigned)(cnt - last_cnt) & 0x0F;
	if (delta == 0)
		return E2E_REPEATED;
	*lost = delta - 1;
	return delta == 1 ? E2E_OK : E2E_GAP;
}
#endif
```

### 3.2 단위 테스트 실행

무엇을 확인하나: CRC 표준 check 값(`"123456789"` → 0x4B), ID가 다르면 CRC가 틀리는지, 카운터 wrap(15 → 3은 3개 손실), 1비트 오류 검출. 출력은 TAP 형식이라 CI가 그대로 읽는다.

```sh
gcc -std=gnu11 -Wall -Wextra -O2 tests/test_e2e.c -o /var/tmp/S7/build/test_e2e && /var/tmp/S7/build/test_e2e
```

```text
ok 1 - crc8 j1850 check value 0x4B
ok 2 - counter 2 -> 3 ok
ok 3 - same payload, other id -> bad crc (data id)
ok 4 - counter repeated
ok 5 - 15 -> 3 = 3 lost (wrap)
ok 6 - single bit flip detected
1..6
```

**체크포인트**: `ok 1`의 0x4B가 맞아야 한다. 이 값이 틀리면 다항식·초기값·반전 중 하나가 표준과 다르다. CRC 구현은 언제나 **표준 check 값으로 먼저 고정**하고 시작한다 — SSD 펌웨어에서 ECC·CRC 엔진을 검증하던 방식 그대로다.

---

## 4. 단계 3 — 센서 ECU 펌웨어 (Cortex-M3, slcan)

### 4.1 왜 slcan + 외부 MCU인가

vcan에는 can_dev 층이 없어서 에러 상태·bus-off가 원리적으로 없다(Q4 0절 그림 1). Q4 8절은 MCU가 slcan의 `e`(에러)·`s`(상태) 메시지를 보내면 Linux slcan 드라이버가 **진짜 컨트롤러 드라이버와 같은 `can_change_state()` / `can_bus_off()` 경로**를 탄다는 것을 보였다. 이 프로젝트는 그 기법을 "센서 ECU"로 키운다: MCU 하나가 **센서 노드 + 그 노드가 붙은 CAN 어댑터** 역할을 동시에 한다.

### 4.2 펌웨어 코드

무엇을 확인하는 코드인가: SysTick 1 ms로 메시지 3개를 주기 송신(가속 10 ms, 자이로 10 ms를 5 ms 어긋나게, 온도 100 ms)하고, 매 프레임에 `e2e_frame_crc()`와 카운터를 넣는다. 가짜 센서 값은 삼각파(libm 없이 정수만). (1/3)

```c
/* sensor_ecu.c - simulated CAN sensor ECU (IMU + temperature) on QEMU mps2-an385.
 * Cortex-M3 bare metal. UART0 speaks slcan (Lawicel ASCII) = "ECU + CAN adapter".
 * UART1 = debug log. SysTick = 1 ms. Frames per dbc/s7_sensor_kit.dbc. */
#include <stdint.h>
#include "../common/e2e.h"
#define U0   ((volatile uint32_t *)0x40004000)  /* DATA STATE CTRL INTST BAUDDIV */
#define U1   ((volatile uint32_t *)0x40005000)
#define SYST ((volatile uint32_t *)0xE000E010)  /* CSR RVR CVR */
#define ID_ACCEL 0x110u
#define ID_GYRO  0x111u
#define ID_TEMP  0x120u
#define ID_FAULT 0x7E0u

static volatile uint32_t ticks;                 /* ms since reset */
static int is_open, busoff;
static uint32_t mute_until, badcrc_left, delay_left, jump_pending;
static const char hx[] = "0123456789ABCDEF";

struct msg { uint32_t id, period, next; uint8_t cnt; };
static struct msg tab[3] = {
	{ ID_ACCEL, 10, 10, 0 }, { ID_GYRO, 10, 15, 0 }, { ID_TEMP, 100, 52, 0 },
};

void systick_isr(void) { ticks++; }
static void putc_(volatile uint32_t *u, char c) { while (u[1] & 1) ; u[0] = (uint8_t)c; }
static void puts_(volatile uint32_t *u, const char *s) { while (*s) putc_(u, *s++); }
static int hv(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static void put16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static int tri(uint32_t t, int amp, uint32_t per)  /* triangle wave, -amp..+amp */
{
	int ph = (int)(t % per), h = (int)per / 2;
	return (ph < h ? ph : (int)per - ph) * 4 * amp / (int)per - amp;
}

static void send_frame(uint32_t id, const uint8_t d[8])   /* -> "tIII8DD..\r" */
{
	char b[24]; int n = 0;
	b[n++] = 't'; b[n++] = hx[(id >> 8) & 7]; b[n++] = hx[(id >> 4) & 15]; b[n++] = hx[id & 15];
	b[n++] = '8';
	for (int i = 0; i < 8; i++) { b[n++] = hx[d[i] >> 4]; b[n++] = hx[d[i] & 15]; }
	b[n++] = '\r'; b[n] = 0; puts_(U0, b);
}
```

(2/3) 프레임 내용 만들기와 고장 주입 명령 처리. 0x7E0 프레임의 byte0 = 고장 코드, byte1 = 인자. 1 MUTE(인자 × 10 ms 송신 정지), 2 BAD_CRC(다음 N개 CRC 오염), 3 CNT_JUMP(카운터 +3), 4 BUS_OFF(ACK 에러 → passive → bus-off 보고 후 송신 중단), 5 DELAY(다음 N개를 15 ms씩 늦춤).

```c
static void publish(struct msg *m, uint32_t t)
{
	uint8_t d[8];
	if (m->id == ID_ACCEL) {
		put16(d + 2, tri(t, 150, 2000)); put16(d + 4, tri(t + 500, 150, 2000)); put16(d + 6, 981);
	} else if (m->id == ID_GYRO) {
		put16(d + 2, tri(t, 3000, 4000)); put16(d + 4, 0); put16(d + 6, -tri(t, 1500, 1000));
	} else {
		put16(d + 2, 2500 + (int)(t / 1000 % 600)); put16(d + 4, 12000 + tri(t, 50, 3000));
		put16(d + 6, (int)(t / 1000));
	}
	if (m->id == ID_ACCEL && jump_pending) { m->cnt = (uint8_t)(m->cnt + 3); jump_pending = 0; }
	d[1] = m->cnt & 0x0F;                       /* STAT nibble = 0 (sensor OK) */
	m->cnt = (uint8_t)((m->cnt + 1) & 0x0F);
	d[0] = e2e_frame_crc(m->id, d);
	if (m->id == ID_ACCEL && badcrc_left) { d[0] ^= 0x5A; badcrc_left--; }
	send_frame(m->id, d);
}

/* (2/2) fault injection, slcan command parser, main loop */
static void fault(uint8_t code, uint8_t arg)
{
	puts_(U1, "fault "); putc_(U1, hx[code & 15]); putc_(U1, '\n');
	switch (code) {
	case 1: mute_until = ticks + 10u * arg; break;          /* stop sender arg x 10 ms */
	case 2: badcrc_left = arg; break;                       /* next arg ACCEL frames bad CRC */
	case 3: jump_pending = 1; break;                        /* ACCEL counter +3 (3 lost) */
	case 4: puts_(U0, "e1a\r"); puts_(U0, "sp000128\r");   /* ACK err -> passive -> bus-off */
		puts_(U0, "sb000256\r"); busoff = 1; break;
	case 5: delay_left = arg; break;                        /* next arg ACCEL frames +15 ms */
	}
}

static void rx_frame(const char *l)                       /* l = "tIIILDD.." */
{
	unsigned id = (unsigned)(hv(l[1]) << 8 | hv(l[2]) << 4 | hv(l[3]));
	int len = l[4] - '0';
	puts_(U0, "z\r");                                   /* Lawicel: TX accepted */
	if (id == ID_FAULT && len >= 2)
		fault((uint8_t)(hv(l[5]) << 4 | hv(l[6])), (uint8_t)(hv(l[7]) << 4 | hv(l[8])));
}

static void cmd(const char *l)
{
	switch (l[0]) {
	case 'C': is_open = 0; putc_(U0, '\r'); puts_(U1, "close\n"); break;
	case 'S': putc_(U0, '\r'); break;
	case 'O': is_open = 1; busoff = 0; putc_(U0, '\r'); puts_(U1, "open\n"); break;
	case 'V': puts_(U0, "V1013\r"); break;
	case 't': if (is_open && !busoff) rx_frame(l); else putc_(U0, '\a'); break;
	default: putc_(U0, '\a');                           /* BEL = error */
	}
}
```

(3/3) 리셋 핸들러: `.data` 복사와 `.bss` 초기화를 직접 한다(Q4의 MCU는 QEMU가 RAM을 0으로 준다는 데 기댔다 — 실보드에서는 안 된다). 메인 루프는 UART 폴링 + 스케줄 확인.

```c
extern uint32_t _sdata, _edata, _sidata, _sbss, _ebss;
void reset_isr(void)
{
	char line[40]; int n = 0;
	for (uint32_t *s = &_sidata, *d = &_sdata; d < &_edata; ) *d++ = *s++;  /* .data */
	for (uint32_t *d = &_sbss; d < &_ebss; ) *d++ = 0;                      /* .bss  */
	U0[4] = 16; U0[2] = 3; U1[4] = 16; U1[2] = 1;      /* baud div, RX/TX enable */
	SYST[1] = 25000 - 1; SYST[2] = 0; SYST[0] = 7;     /* 1 ms tick @ 25 MHz */
	puts_(U1, "s7 sensor ecu up\n");
	for (;;) {
		if (U0[1] & 2) {                                /* RX buffer full */
			char c = (char)U0[0];
			if (c == '\r') { line[n] = 0; if (n) cmd(line); n = 0; }
			else if (n < 39) line[n++] = c;
		}
		uint32_t t = ticks;
		for (int i = 0; i < 3; i++) {
			struct msg *m = &tab[i];
			if ((int32_t)(t - m->next) < 0) continue;
			m->next += m->period;
			if (m->id == ID_ACCEL && delay_left) { m->next += 15; delay_left--; }
			if (is_open && !busoff && (int32_t)(t - mute_until) >= 0) publish(m, t);
		}
	}
}

__attribute__((section(".vectors"), used)) static void (*const vec[16])(void) = {
	(void (*)(void))0x20010000, reset_isr, [15] = systick_isr };
```

링커 스크립트 — `.data`의 LMA는 FLASH, VMA는 RAM이다(`AT > FLASH`). 리셋 핸들러가 `_sidata`에서 `_sdata`로 복사한다:

```text
MEMORY { FLASH (rx) : ORIGIN = 0x0, LENGTH = 4M  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K }
SECTIONS {
  .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > FLASH
  .data : { _sdata = .; *(.data*) _edata = .; } > RAM AT > FLASH
  _sidata = LOADADDR(.data);
  .bss (NOLOAD) : { _sbss = .; *(.bss*) *(COMMON) _ebss = .; } > RAM
}
```

### 4.3 빌드

```sh
make -C firmware OUT=/var/tmp/S7/build
arm-none-eabi-objdump -h /var/tmp/S7/build/sensor_ecu.elf | grep -E "text|data|bss"
```

```text
   text	   data	    bss	    dec	    hex	filename
   1314	     48	     28	   1390	    56e	/var/tmp/S7/build/sensor_ecu.elf
  0 .text         00000522  00000000  00000000  00010000  2**2
  1 .data         00000030  20000000  00000522  00020000  2**2
  2 .bss          0000001c  20000030  00000552  00020030  2**2
```

출력에서 볼 것: `.data`는 VMA 0x20000000(RAM), LMA 0x522(FLASH, `.text` 바로 뒤). 48바이트는 메시지 테이블 `tab[3]`이다. 펌웨어 전체가 1.4 KB.

처음 빌드에서는 Q4의 4줄짜리 링커 스크립트를 그대로 썼고, `data 48`이 나오는데 `.data` 섹션 배치가 없었다 — 고아 섹션이 FLASH 쪽에 붙었고, mps2-an385의 0x0 영역이 QEMU에서 RAM이라 우연히 돌았을 것이다. 실보드 플래시에서는 쓰기가 안 되므로 위처럼 고쳤다.

### 4.4 ECU 단독 테스트 — Linux 없이 펌웨어만 검증

무엇을 확인하나: QEMU를 소켓 서버로 띄우고 Python이 slcan "호스트" 역할을 해서 `C`/`S6`/`O`를 보낸 뒤 2초간 프레임을 세고, CRC를 Python으로 다시 계산하고, 고장 명령 3종에 대한 반응을 본다. 시스템 테스트보다 **훨씬 빠른(4초) 1차 방어선**이다. 파일: `tests/ecu_smoke.py` (62줄, 레포 참고).

```sh
python3 tests/ecu_smoke.py /var/tmp/S7/build/sensor_ecu.elf /var/tmp/S7/build; echo rc=$?
cat /var/tmp/S7/build/ecu_smoke.log      # UART1 = 디버그 로그
```

```text
# 2.0 s window: 0x110=195, 0x111=195, 0x120=19
ok 1 - rates ~100 Hz / ~10 Hz
ok 2 - all CRCs valid (409 frames)
ok 3 - fault 2: exactly 5 bad-CRC frames (got 5)
ok 4 - fault 1: mute 200 ms visible
ok 5 - fault 4: bus-off report, no frames
ok 6 - re-open resumes traffic
1..6
rc=0
s7 sensor ecu up
close
open
fault 2
fault 1
fault 4
open
```

출력에서 볼 것: 2초에 195개(100 Hz에서 첫 프레임까지의 지연만큼 모자람). 디버그 UART에 Linux 쪽 명령 순서(`close`, `open`, `fault …`)가 그대로 남는다 — 실보드에서도 MCU 디버그 UART를 따로 두는 이유다.

**체크포인트**: `rc=0`, 그리고 디버그 로그에 `s7 sensor ecu up`이 찍혀야 한다.

**흔한 실패**: QEMU의 `-serial` 순서가 UART0, UART1 순이다. 디버그 로그가 소켓으로 섞여 들어오면 `-serial` 두 개의 순서가 바뀐 것이다.

---

## 5. 단계 4 — DBC에서 C 코드 생성 (cantools)

```sh
cantools generate_c_source s7_sensor_kit.dbc     # 게스트에서
ls -l s7_sensor_kit.[ch]
grep -n "CYCLE_TIME_MS\|FRAME_ID" gen/s7_sensor_kit.h | head
```

```text
Successfully generated ./s7_sensor_kit.h and ./s7_sensor_kit.c.
-rw-r--r-- 1 root root 24512 Oct 18 23:35 s7_sensor_kit.c
-rw-r--r-- 1 root root 44902 Oct 18 23:35 s7_sensor_kit.h
48:#define S7_SENSOR_KIT_IMU_ACCEL_FRAME_ID (0x110u)
49:#define S7_SENSOR_KIT_IMU_GYRO_FRAME_ID (0x111u)
50:#define S7_SENSOR_KIT_TEMP_STATUS_FRAME_ID (0x120u)
51:#define S7_SENSOR_KIT_GW_STATUS_FRAME_ID (0x500u)
52:#define S7_SENSOR_KIT_ECU_FAULT_CMD_FRAME_ID (0x7e0u)
69:#define S7_SENSOR_KIT_IMU_ACCEL_CYCLE_TIME_MS (10u)
70:#define S7_SENSOR_KIT_IMU_GYRO_CYCLE_TIME_MS (10u)
71:#define S7_SENSOR_KIT_TEMP_STATUS_CYCLE_TIME_MS (100u)
72:#define S7_SENSOR_KIT_GW_STATUS_CYCLE_TIME_MS (100u)
```

(`grep` 결과는 첫 생성본 기준이다. 신호 이름을 바꾼 뒤 재생성한 파일도 이 줄들은 같다.) 생성물이 주는 것: 메시지마다 구조체(`struct s7_sensor_kit_imu_accel_t`), `_pack()`/`_unpack()`(비트 시프트 코드), 신호마다 `_encode()`/`_decode()`(스케일 적용, `double`)와 `_is_in_range()`. 데몬은 GW_STATUS를 `_pack()`으로 만들고, 디버그 출력에서 `_decode()`로 m/s²를 찍는다.

손으로 비트 시프트를 짜는 것과 비교: 생성 코드는 DBC와 항상 일치한다는 것이 장점이고, `double` 연산과 큰 헤더(45 KB)가 단점이다. MCU 쪽은 `double`이 비싸므로 이 레포의 펌웨어는 생성 코드를 쓰지 않고 정수로 직접 채웠다 — **"DBC 하나, 생성 코드는 Linux 쪽만"**이 흔한 실무 타협이다.

---

## 6. 단계 5 — QEMU 두 개를 엮고 버스 올리기

### 6.1 QEMU 실행 (ci/qemu_ctl.py 핵심부)

무엇을 확인하는 코드인가: Linux 게스트는 `pci-serial`(16550A → `/dev/ttyS0`)을 unix 소켓 **서버**로, ECU는 같은 소켓에 `reconnect-ms=1000`으로 붙는 **클라이언트**로 띄운다. Q4 8.2절의 교훈("재부팅이 잦은 쪽을 재접속 클라이언트로")을 그대로 따른다. 콘솔도 unix 소켓이라 스크립트가 셸 명령을 보낼 수 있다.

```python
def guest(work):
    ov = f"{work}/guest.qcow2"
    if not os.path.exists(ov):
        subprocess.check_call(["qemu-img", "create", "-q", "-f", "qcow2", "-F", "raw",
                               "-b", f"{LAB}/rootfs-dom.ext4", ov])
    spawn(work, "guest", ["qemu-system-aarch64", "-machine", "virt", "-cpu", "cortex-a57", "-smp", "2",
        "-m", "1G", "-nographic", "-kernel", f"{LAB}/Image-dom",
        "-drive", f"file={ov},if=virtio,format=qcow2",
        "-virtfs", f"local,path={work}/share,mount_tag=s7,security_model=none,id=s7fs",
        "-chardev", f"socket,id=ecu,path={work}/link.sock,server=on,wait=off",
        "-device", "pci-serial,chardev=ecu",
        "-serial", f"unix:{work}/console.sock,server=on,wait=off", "-monitor", "none",
        "-append", "console=ttyAMA0 root=/dev/vda rw rootwait apparmor=0 "
                   "systemd.mask=apparmor.service systemd.mask=snapd.service quiet"])

def ecu(work, elf):
    spawn(work, "ecu", ["qemu-system-arm", "-M", "mps2-an385", "-nographic", "-monitor", "none",
        "-kernel", elf, "-chardev", f"socket,id=s0,path={work}/link.sock,reconnect-ms=1000",
        "-serial", "chardev:s0", "-serial", f"file:{work}/ecu_debug.log"])
```

### 6.2 게스트 쪽 버스 올리기 (tools/kit_up.sh)

```sh
#!/bin/bash
# kit_up.sh - (guest) bring up sensor bus s7sl0 (slcan on ttyS0) + backbone vcans. idempotent.
modprobe slcan; modprobe vcan
for i in s7vc1 s7vc2 s7vc3; do ip link add $i type vcan 2>/dev/null; ip link set $i up; done
if ! ip link show s7sl0 > /dev/null 2>&1; then
	stty -F /dev/ttyS0 raw -echo 115200
	slcand /dev/ttyS0 s7sl0; sleep 1                 # attach line discipline only
	ip link set s7sl0 type can bitrate 500000         # kernel sends C / S6 / O on up
fi
ip link set s7sl0 up
```

`slcand`에 `-s`·`-o`를 주지 않고 line discipline만 붙인 뒤, bitrate는 `ip link`로 준다(Q4 8.5절 방식). 이러면 `ip link set up/down` 때마다 **커널 slcan이 직접 `C`, `S6`, `O`를 MCU에 보낸다** — 진짜 컨트롤러 드라이버의 `ndo_open`/`ndo_stop`이 레지스터를 초기화하는 것과 같은 모양이다. 7절의 bus-off 재시작이 이 성질에 기대고 있다.

### 6.3 확인 — 링크 상태, 원시 프레임, DBC 디코드

```sh
ip -details link show s7sl0 | head -4
candump -t d -n 8 s7sl0
candump -L -n 3 s7sl0,110:7FF > /tmp/raw.txt; cantools decode $DBC < /tmp/raw.txt
```

```text
4: s7sl0: <NOARP,UP,LOWER_UP> mtu 16 qdisc pfifo_fast state UP mode DEFAULT group default qlen 10
    link/can  promiscuity 0  allmulti 0 minmtu 16 maxmtu 16 
    can state ERROR-ACTIVE restart-ms 0 
	  bitrate 500000
 (000.000000)  s7sl0  110   [8]  D3 03 DC FF 72 00 D5 03
 (000.004208)  s7sl0  111   [8]  84 04 35 07 00 00 D6 FC
 (000.004588)  s7sl0  110   [8]  63 04 DF FF 75 00 D5 03
 (000.005841)  s7sl0  111   [8]  E0 05 17 07 00 00 9A FC
 (000.003457)  s7sl0  110   [8]  04 05 E2 FF 78 00 D5 03
 (000.005659)  s7sl0  111   [8]  38 06 F9 06 00 00 5E FC
 (000.006048)  s7sl0  110   [8]  96 06 E5 FF 7B 00 D5 03
 (000.004784)  s7sl0  111   [8]  9D 07 DB 06 00 00 22 FC
(1792367557.449565) s7sl0 110#09007F001600D503
(1792367557.459148) s7sl0 110#020182001300D503
(1792367557.469123) s7sl0 110#900285001000D503
(1792367557.449565) s7sl0 110#09007F001600D503 ::
IMU_ACCEL(
    ACCEL_CRC: 9,
    ACCEL_CNT: 0,
    ACCEL_STAT: 0,
    ACCEL_X: 1.27 m/s2,
    ACCEL_Y: 0.22 m/s2,
    ACCEL_Z: 9.81 m/s2
)
...
```

(첫 덩어리는 첫 통합 실행, 둘째는 `cantools decode` 단계 실행에서 가져왔다. 디코드 출력은 첫 프레임만 남기고 생략했다.) 출력에서 볼 것:

- `can state ERROR-ACTIVE`, `bitrate 500000` — 커널이 bitrate를 안다(slcand `-s6` 방식이면 `bitrate 0`이었다, Q4 8.3절).
- 0x110과 0x111이 약 5 ms 간격으로 번갈아 온다 — 펌웨어에서 자이로 오프셋을 5 ms로 준 결과. 실제 ECU도 같은 주기의 메시지를 **같은 틱에 몰지 않고 흩는다**(버스 부하 버스트를 피하려고).
- 첫 프레임 `D3 03 DC FF 72 00 D5 03`을 손으로 풀면: CRC 0xD3, byte1 0x03 → 카운터 3, X = 0xFFDC = -36 → -0.36 m/s², Y = 0x0072 = 114 → 1.14, Z = 0x03D5 = 981 → 9.81(중력). cantools 디코드와 같은 방법이다.

**체크포인트**: `candump`에 0x110·0x111·0x120이 보이고 `cantools decode`의 ACCEL_Z가 9.81이어야 한다.

**흔한 실패**:

- `candump`에 아무것도 없음 → ECU QEMU가 소켓에 아직 안 붙었다(`reconnect-ms`라 최대 1초). 또는 `stty raw`를 안 해서 tty가 `\r`을 `\n`으로 바꿨다 — slcan은 `\r`로 프레임을 끝낸다.
- `ip link set s7sl0 type can bitrate 333333` → `Error: bitrate 0 bps not supported.` slcan은 고정 목록(`bitrate_const`)만 받는다(Q4 8.5절).

---

## 7. 단계 6 — 게이트웨이 데몬 `canmond`

### 7.1 설계 — 한 스레드, 이벤트 루프 하나

데몬은 스레드 없이 **epoll 하나**로 세 가지 이벤트를 기다린다(A4): CAN 소켓 수신, 5 ms `timerfd`(데드라인 판정·상태 프레임 송신), `signalfd`(SIGTERM·SIGUSR1). RTOS로 치면 "이벤트 플래그 3개를 기다리는 태스크 하나"다. 스레드를 나누지 않으니 상태 변수에 락이 필요 없다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">메시지별 상태 (IMU_ACCEL, IMU_GYRO, TEMP_STATUS 각각)</text><rect x="10" y="40" width="110" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="65.0" y="58.8" font-size="12" text-anchor="middle">NO_DATA</text><text x="65.0" y="73.8" font-size="12" text-anchor="middle">시작</text><rect x="190" y="40" width="110" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="245.0" y="58.8" font-size="12" text-anchor="middle">OK</text><text x="245.0" y="73.8" font-size="12" text-anchor="middle">전달함</text><rect x="400" y="40" width="110" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="455.0" y="58.8" font-size="12" text-anchor="middle">LATE</text><text x="455.0" y="73.8" font-size="12" text-anchor="middle">age &gt; 3×주기</text><rect x="400" y="150" width="110" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="455.0" y="168.8" font-size="12" text-anchor="middle">MISSING</text><text x="455.0" y="183.8" font-size="12" text-anchor="middle">age &gt; 10×주기</text><rect x="185" y="150" width="120" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="245.0" y="168.8" font-size="12" text-anchor="middle">BAD_DATA</text><text x="245.0" y="183.8" font-size="12" text-anchor="middle">CRC·반복 카운터</text><line x1="120" y1="62" x2="188" y2="62" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="154.0" y="56.0" font-size="11" text-anchor="middle">첫 프레임</text><line x1="300" y1="55" x2="398" y2="55" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="349.0" y="49.0" font-size="11" text-anchor="middle">tick: 늦음</text><line x1="398" y1="72" x2="302" y2="72" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="349.0" y="88.0" font-size="11" text-anchor="middle">프레임 도착</text><line x1="455" y1="84" x2="455" y2="148" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="462.0" y="120.0" font-size="11" text-anchor="start">tick: 타임아웃</text><path d="M400,180 C360,230 300,230 270,196" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="352.0" y="236.0" font-size="11" text-anchor="middle">E2E 실패</text><line x1="245" y1="84" x2="245" y2="148" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="240.0" y="116.0" font-size="11" text-anchor="end">E2E 실패</text><path d="M190,172 C140,140 150,100 205,86" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="150.0" y="132.0" font-size="11" text-anchor="end">좋은 3개</text><path d="M510,172 C600,150 560,40 512,48" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="578.0" y="120.0" font-size="11" text-anchor="start">좋은 3개</text><text x="10.0" y="228.0" font-size="12" text-anchor="start">OK에서만 백본으로 전달 · 판정 전 RX 큐를 먼저 비운다</text><line x1="0" y1="250" x2="680" y2="250" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4"/><text x="10.0" y="274.0" font-size="13" text-anchor="start" font-weight="bold">버스 상태 (에러 프레임 → on_error)</text><rect x="10" y="292" width="120" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="70.0" y="316.3" font-size="12" text-anchor="middle">ACTIVE</text><rect x="145" y="292" width="120" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="205.0" y="316.3" font-size="12" text-anchor="middle">WARNING</text><rect x="280" y="292" width="120" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="340.0" y="316.3" font-size="12" text-anchor="middle">PASSIVE</text><rect x="415" y="292" width="120" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="475.0" y="316.3" font-size="12" text-anchor="middle">BUS_OFF</text><rect x="550" y="292" width="120" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="610.0" y="316.3" font-size="12" text-anchor="middle">RESTARTING</text><line x1="130" y1="312" x2="143" y2="312" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><line x1="265" y1="312" x2="278" y2="312" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><line x1="400" y1="312" x2="413" y2="312" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><line x1="535" y1="312" x2="548" y2="312" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="476.0" y="350.0" font-size="11" text-anchor="middle">backoff 100 ms ×2, 최대 2 s</text><path d="M610,332 C610,400 70,400 70,334" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="340.0" y="394.0" font-size="11" text-anchor="middle">ifdown + ifup 성공 → ACTIVE (slcan은 restart-ms 미지원)</text><text x="10.0" y="424.0" font-size="12" text-anchor="start">CAN_ERR_CRTL data[1]: WARNING/PASSIVE/ACTIVE 비트 · CAN_ERR_BUSOFF · CAN_ERR_RESTARTED</text><text x="10.0" y="444.0" font-size="12" text-anchor="start">restart-ms를 지원하는 드라이버(mcp251x 등)라면 커널이 재시작 → 데몬은 RESTARTED만 기다린다</text></svg>
```

그림 3 — 데몬의 두 상태 머신. 위: 메시지마다 하나(NO_DATA → OK → LATE → MISSING, CRC·반복 카운터면 BAD_DATA, 복구는 좋은 프레임 3개 디바운스). 아래: 버스 하나(에러 프레임이 움직이고, BUS_OFF면 backoff 뒤 링크 재오픈).

설계 결정과 이유:

| 결정 | 이유 |
|---|---|
| 커널 필터로 0x110/0x111/0x120만 받음 | 버스가 범람해도 데몬 소켓 큐는 안 넘친다 (10.4절에서 측정) |
| `CAN_RAW_ERR_FILTER = CAN_ERR_MASK` | 에러 프레임(상태 변화, bus-off)을 데이터와 같은 소켓으로 받음 |
| 주기 통계는 커널 RX 타임스탬프로 | 데몬이 늦게 깨어나도 도착 간격은 정확 |
| 데드라인 판정은 CLOCK_MONOTONIC으로 | 소프트웨어 RX 타임스탬프는 CLOCK_REALTIME이라 NTP 조정에 흔들림 |
| 판정 전에 RX 큐를 먼저 비움 | 7.6절의 가짜 MISSING 버그 수정 |
| OK 상태 프레임만 백본으로 전달 | 나쁜 데이터가 하류로 새지 않게 (can-gw와 비교: 10.5절) |
| bus-off 재시작에 지수 backoff | 물리 고장이 계속되면 재시작 → 즉시 bus-off가 반복되며 버스를 괴롭힘 |

### 7.2 코드 (1/7) — 상태와 메시지 테이블

1~24행은 `#include` 목록이라 생략했다(`linux/can.h`, `linux/can/raw.h`, `linux/can/error.h`, `linux/net_tstamp.h`, `linux/errqueue.h`, `sys/epoll.h`, `sys/timerfd.h`, `sys/signalfd.h` 등 + 생성 헤더 `s7_sensor_kit.h`, `e2e.h`). 메시지 테이블의 ID와 주기는 **생성 헤더의 매크로**에서 온다.

```c
#include "s7_sensor_kit.h"
#include "e2e.h"

enum mstate { NO_DATA, OK, LATE, MISSING, BAD_DATA };
enum bstate { B_ACTIVE, B_WARNING, B_PASSIVE, B_BUSOFF, B_RESTARTING };
static const char *const mstr[] = { "NO_DATA", "OK", "LATE", "MISSING", "BAD_DATA" };
static const char *const bstr[] = { "ACTIVE", "WARNING", "PASSIVE", "BUS_OFF", "RESTARTING" };

struct mon {
	const char *name; uint32_t id, period_ms;
	enum mstate st; int last_cnt, good_run, rejected;  /* rejected since last good frame */
	uint64_t last_mono_ns, last_kts_ns;           /* deadline clock / kernel RX timestamp */
	unsigned rx, bad_crc, repeated, lost, late, timeouts;
	uint64_t gap_min_us, gap_max_us, gap_sum_us, gaps;
};
#define MON(n, N) { #N, S7_SENSOR_KIT_##N##_FRAME_ID, S7_SENSOR_KIT_##N##_CYCLE_TIME_MS, \
		    NO_DATA, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, UINT64_MAX, 0, 0, 0 }
static struct mon mons[] = { MON(0, IMU_ACCEL), MON(1, IMU_GYRO), MON(2, TEMP_STATUS) };
#define NMON (sizeof(mons) / sizeof(mons[0]))

static enum bstate bus = B_ACTIVE;
static unsigned restarts, bus_errors, rx_ovfl, fwd, late_x = 2, timeout_x = 5;
static uint64_t restart_at_ns, backoff_ms = 100;
static int rx_s, tx_s, own_routing = 1, dump;
static char rx_if[IFNAMSIZ], tx_if[IFNAMSIZ];

static uint64_t now_ns(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void ev(const char *fmt, ...)                  /* one event line, monotonic stamp */
{
	va_list ap;
	uint64_t t = now_ns();
	printf("[%6llu.%03llu] ", (unsigned long long)(t / 1000000000ull),
	       (unsigned long long)(t / 1000000ull % 1000));
	va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
	putchar('\n');
}

static void set_state(struct mon *m, enum mstate s, const char *why)
{
	if (m->st == s) return;
	ev("STATE %s %s -> %s (%s)", m->name, mstr[m->st], mstr[s], why);
	m->st = s;
}
```

### 7.3 코드 (2/7) — 소켓: 필터, 에러 마스크, 타임스탬프, overflow 카운터

무엇을 확인하는 코드인가: 수신 소켓에 소켓 옵션 4개를 거는 부분과, `ip link set down/up`과 같은 일을 `ioctl(SIOCSIFFLAGS)`로 하는 함수. 필터 마스크에 `CAN_EFF_FLAG | CAN_RTR_FLAG`를 넣는 이유는 Q4 4.4절의 함정(29비트 프레임이 몰래 들어옴) 때문이다.

```c
/* (2/5) sockets: kernel filters, error frames, SO_TIMESTAMPING, SO_RXQ_OVFL */
static int open_can(const char *ifname, int rx)
{
	struct sockaddr_can a = { .can_family = AF_CAN };
	int s = socket(PF_CAN, SOCK_RAW, CAN_RAW), on = 1;
	if (s < 0 || !(a.can_ifindex = (int)if_nametoindex(ifname))) {
		perror(ifname); exit(1);
	}
	if (rx) {
		struct can_filter f[NMON];
		for (unsigned i = 0; i < NMON; i++) {        /* exact 11-bit match, data frames only */
			f[i].can_id = mons[i].id;
			f[i].can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;
		}
		can_err_mask_t em = CAN_ERR_MASK;            /* all error classes */
		int ts = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
		if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, f, sizeof(f)) ||
		    setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &em, sizeof(em)) ||
		    setsockopt(s, SOL_SOCKET, SO_TIMESTAMPING, &ts, sizeof(ts)) ||
		    setsockopt(s, SOL_SOCKET, SO_RXQ_OVFL, &on, sizeof(on))) {
			perror("setsockopt"); exit(1);
		}
	} else {
		setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);   /* TX only: receive nothing */
	}
	if (bind(s, (struct sockaddr *)&a, sizeof(a))) { perror("bind"); exit(1); }
	return s;
}

static int if_updown(const char *ifname, int up)     /* = ip link set IF down/up */
{
	struct ifreq r = { 0 };
	int s = socket(AF_INET, SOCK_DGRAM, 0), rc;
	snprintf(r.ifr_name, IFNAMSIZ, "%s", ifname);
	rc = ioctl(s, SIOCGIFFLAGS, &r);
	if (!rc) {
		r.ifr_flags = (short)(up ? (r.ifr_flags | IFF_UP) : (r.ifr_flags & ~IFF_UP));
		rc = ioctl(s, SIOCSIFFLAGS, &r);
	}
	if (rc) ev("ERROR ioctl %s %s: %s", ifname, up ? "up" : "down", strerror(errno));
	close(s);
	return rc;
}

static void tx(uint32_t id, const uint8_t *d, uint8_t len)
{
	struct can_frame f = { .can_id = id, .len = len };
	memcpy(f.data, d, len);
	if (write(tx_s, &f, sizeof(f)) != sizeof(f) && errno != ENOBUFS)
		ev("ERROR tx %s: %s", tx_if, strerror(errno));
}
```

### 7.4 코드 (3/7) — 데이터 프레임 경로

무엇을 확인하는 코드인가: E2E 판정 → 나쁜 프레임은 버림 → 간격 통계(커널 타임스탬프) → 상태 갱신(디바운스) → OK면 백본으로 전달 → (옵션) 생성 코드로 물리값 디코드. "거부된 프레임은 손실이 아니다"를 `rejected` 카운터로 구분한다 — 처음 버전은 CRC 오염 3개 다음 프레임에서 `GAP lost=3`을 같이 찍어서 **같은 사건을 두 번 셌다**.

```c
/* (3/5) data frame path: E2E check -> stats -> state -> own routing */
static void on_data(struct mon *m, const struct can_frame *f, uint64_t kts_ns)
{
	unsigned lost;
	uint64_t t = now_ns();
	enum e2e_res r = f->len == 8 ? e2e_check(m->id, f->data, m->last_cnt, &lost) : E2E_BAD_CRC;

	m->rx++;
	if (r == E2E_BAD_CRC || r == E2E_REPEATED) {    /* reject: do not forward, do not feed */
		if (r == E2E_BAD_CRC) m->bad_crc++; else m->repeated++;
		m->good_run = 0; m->rejected += (r == E2E_BAD_CRC);
		ev("E2E %s %s cnt=%d", m->name, r == E2E_BAD_CRC ? "BAD_CRC" : "REPEATED", f->data[1] & 15);
		set_state(m, BAD_DATA, r == E2E_BAD_CRC ? "crc" : "repeated counter");
		return;
	}
	if (r == E2E_GAP) {                             /* rejected frames are not "lost" */
		lost = lost > (unsigned)m->rejected ? lost - (unsigned)m->rejected : 0;
		m->lost += lost;
		if (lost) ev("E2E %s GAP lost=%u", m->name, lost);
	}
	m->rejected = 0;
	if (m->last_kts_ns) {                           /* period stats from kernel timestamps */
		uint64_t g = (kts_ns - m->last_kts_ns) / 1000;
		if (g < m->gap_min_us) m->gap_min_us = g;
		if (g > m->gap_max_us) m->gap_max_us = g;
		m->gap_sum_us += g; m->gaps++;
		if (g > (uint64_t)late_x * m->period_ms * 1000) {
			m->late++;
			ev("LATE %s gap=%.1fms", m->name, (double)g / 1000);
		}
	}
	m->last_cnt = f->data[1] & 15; m->last_kts_ns = kts_ns; m->last_mono_ns = t;
	if (m->st == MISSING || m->st == BAD_DATA) {    /* debounce: 3 good frames to recover */
		if (++m->good_run >= 3) set_state(m, OK, "3 good frames");
	} else {
		set_state(m, OK, m->st == NO_DATA ? "first frame" : "frame arrived");
	}
	if (own_routing && m->st == OK) { tx(f->can_id, f->data, f->len); fwd++; }
	if (dump && m->id == S7_SENSOR_KIT_IMU_ACCEL_FRAME_ID && m->rx % 100 == 0) {
		struct s7_sensor_kit_imu_accel_t a;
		s7_sensor_kit_imu_accel_unpack(&a, f->data, f->len);
		ev("DECODE IMU_ACCEL x=%.2f y=%.2f z=%.2f m/s2",
		   s7_sensor_kit_imu_accel_accel_x_decode(a.accel_x),
		   s7_sensor_kit_imu_accel_accel_y_decode(a.accel_y),
		   s7_sensor_kit_imu_accel_accel_z_decode(a.accel_z));
	}
}
```

### 7.5 코드 (4/7, 5/7) — 에러 프레임, 데드라인 틱, 상태 프레임

```c
/* (4/5) error frames -> bus state machine; restart; deadline tick; status frame */
static void set_bus(enum bstate s, const char *why)
{
	if (bus == s) return;
	ev("BUS %s -> %s (%s)", bstr[bus], bstr[s], why);
	bus = s;
}

static void on_error(const struct can_frame *f)
{
	if (f->can_id & (CAN_ERR_PROT | CAN_ERR_ACK | CAN_ERR_BUSERROR)) bus_errors++;
	if (f->can_id & CAN_ERR_CRTL) {
		uint8_t c = f->data[1];
		if (c & (CAN_ERR_CRTL_TX_PASSIVE | CAN_ERR_CRTL_RX_PASSIVE)) set_bus(B_PASSIVE, "ctrl");
		else if (c & (CAN_ERR_CRTL_TX_WARNING | CAN_ERR_CRTL_RX_WARNING)) set_bus(B_WARNING, "ctrl");
		else if (c & CAN_ERR_CRTL_ACTIVE) set_bus(B_ACTIVE, "ctrl");
	}
	if (f->can_id & CAN_ERR_RESTARTED) set_bus(B_ACTIVE, "driver restarted (restart-ms)");
	if (f->can_id & CAN_ERR_BUSOFF) {
		set_bus(B_BUSOFF, "error frame");
		restart_at_ns = now_ns() + backoff_ms * 1000000ull;
		ev("RESTART scheduled in %llu ms", (unsigned long long)backoff_ms);
		if (backoff_ms < 2000) backoff_ms *= 2;      /* exponential backoff, cap 2 s */
	}
}
```

`tick()`은 5 ms마다: 메시지별 age 판정, bus-off 재시작 시각 확인(재시작 = ifdown + ifup), 100 ms마다 GW_STATUS를 생성 코드 `_pack()`으로 만들어 송신.

```c
static void tick(void)                               /* every 5 ms */
{
	static uint64_t next_status;
	uint64_t t = now_ns();

	for (unsigned i = 0; i < NMON; i++) {
		struct mon *m = &mons[i];
		if (m->st == NO_DATA || m->st == MISSING) continue;
		uint64_t age_ms = (t - m->last_mono_ns) / 1000000;
		if (age_ms > (uint64_t)timeout_x * m->period_ms) {
			char why[32];
			snprintf(why, sizeof(why), "timeout, age %llu ms", (unsigned long long)age_ms);
			m->timeouts++; m->good_run = 0;
			set_state(m, MISSING, why);
		} else if (m->st == OK && age_ms > (uint64_t)late_x * m->period_ms) {
			set_state(m, LATE, "deadline");
		}
	}
	if (bus == B_BUSOFF && restart_at_ns && t >= restart_at_ns) {
		set_bus(B_RESTARTING, "backoff expired");
		restart_at_ns = 0; restarts++;
		if (!if_updown(rx_if, 0) && !if_updown(rx_if, 1)) set_bus(B_ACTIVE, "link re-opened");
	}
	if (bus == B_ACTIVE && backoff_ms > 100 && mons[0].st == OK) backoff_ms = 100;
	if (t >= next_status) {                          /* GW_STATUS every 100 ms (DBC cycle) */
		static uint8_t alive;
		struct s7_sensor_kit_gw_status_t g = { .gw_alive = alive++,
			.accel_state = mons[0].st, .gyro_state = mons[1].st, .temp_state = mons[2].st,
			.bus_state = bus, .restarts = (uint8_t)restarts };
		uint8_t d[8];
		for (unsigned i = 0; i < NMON; i++) {
			g.integrity_errs = (uint16_t)(g.integrity_errs + mons[i].bad_crc + mons[i].repeated);
			g.timeout_total = (uint16_t)(g.timeout_total + mons[i].timeouts);
		}
		s7_sensor_kit_gw_status_pack(d, &g, sizeof(d));
		tx(S7_SENSOR_KIT_GW_STATUS_FRAME_ID, d, 8);
		next_status = t + S7_SENSOR_KIT_GW_STATUS_CYCLE_TIME_MS * 1000000ull;
	}
}
```

### 7.6 코드 (6/7, 7/7) — recvmsg와 cmsg, 메인 루프

`SO_TIMESTAMPING`의 소프트웨어 RX 시각은 `struct scm_timestamping`의 `ts[0]`, `SO_RXQ_OVFL`은 이 소켓이 지금까지 버린 프레임 누적 수(`uint32_t`)다. 링크가 내려가면 커널이 소켓에 `ENETDOWN`을 한 번 걸어 둔다 — 무시하고 계속 읽으면 된다(12절).

```c
/* (5/5) recvmsg + cmsg, epoll main loop, summary on SIGTERM/SIGUSR1 */
static int rx_one(void)                              /* 1 = handled a frame, 0 = queue empty */
{
	struct can_frame f;
	char ctl[256];
	struct iovec io = { &f, sizeof(f) };
	struct msghdr mh = { .msg_iov = &io, .msg_iovlen = 1, .msg_control = ctl,
			     .msg_controllen = sizeof(ctl) };
	uint64_t kts = 0;
	ssize_t n = recvmsg(rx_s, &mh, MSG_DONTWAIT);
	if (n < 0) {
		if (errno == ENETDOWN) { ev("NOTE recvmsg ENETDOWN (link went down)"); return 1; }
		if (errno != EAGAIN) ev("ERROR recvmsg: %s", strerror(errno));
		return 0;
	}
	for (struct cmsghdr *c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c)) {
		if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SO_TIMESTAMPING) {
			struct scm_timestamping *ts = (struct scm_timestamping *)CMSG_DATA(c);
			kts = (uint64_t)ts->ts[0].tv_sec * 1000000000ull + (uint64_t)ts->ts[0].tv_nsec;
		} else if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SO_RXQ_OVFL) {
			uint32_t d; memcpy(&d, CMSG_DATA(c), sizeof(d));
			if (d != rx_ovfl) { ev("OVERFLOW socket dropped %u frames total", d); rx_ovfl = d; }
		}
	}
	if (f.can_id & CAN_ERR_FLAG) { on_error(&f); return 1; }
	for (unsigned i = 0; i < NMON; i++)
		if ((f.can_id & CAN_SFF_MASK) == mons[i].id) on_data(&mons[i], &f, kts);
	return 1;
}

static void summary(void)
{
	for (unsigned i = 0; i < NMON; i++) {
		struct mon *m = &mons[i];
		ev("SUMMARY %s rx=%u bad_crc=%u repeated=%u lost=%u late=%u timeouts=%u "
		   "gap_us min=%llu avg=%llu max=%llu state=%s", m->name, m->rx, m->bad_crc,
		   m->repeated, m->lost, m->late, m->timeouts, (unsigned long long)(m->gaps ? m->gap_min_us : 0),
		   (unsigned long long)(m->gaps ? m->gap_sum_us / m->gaps : 0),
		   (unsigned long long)m->gap_max_us, mstr[m->st]);
	}
	ev("SUMMARY bus=%s restarts=%u bus_errors=%u rx_ovfl=%u forwarded=%u",
	   bstr[bus], restarts, bus_errors, rx_ovfl, fwd);
}
```

```c
int main(int argc, char **argv)
{
	int o, ep = epoll_create1(0);
	while ((o = getopt(argc, argv, "i:o:ndL:T:")) != -1) {
		if (o == 'L') late_x = (unsigned)atoi(optarg);
		else if (o == 'T') timeout_x = (unsigned)atoi(optarg);
		else if (o == 'i') snprintf(rx_if, IFNAMSIZ, "%s", optarg);
		else if (o == 'o') snprintf(tx_if, IFNAMSIZ, "%s", optarg);
		else if (o == 'n') own_routing = 0;
		else if (o == 'd') dump = 1;
		else { fprintf(stderr, "usage: canmond -i RXIF -o TXIF [-n] [-d] [-L late_x] [-T timeout_x]\n"); return 2; }
	}
	if (!rx_if[0] || !tx_if[0]) { fprintf(stderr, "need -i and -o\n"); return 2; }
	setvbuf(stdout, NULL, _IOLBF, 0);
	rx_s = open_can(rx_if, 1); tx_s = open_can(tx_if, 0);
	int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
	struct itimerspec its = { { 0, 5000000 }, { 0, 5000000 } };
	sigset_t ss; sigemptyset(&ss);
	sigaddset(&ss, SIGTERM); sigaddset(&ss, SIGINT); sigaddset(&ss, SIGUSR1);
	sigprocmask(SIG_BLOCK, &ss, NULL);
	int sfd = signalfd(-1, &ss, 0);
	if (ep < 0 || tfd < 0 || sfd < 0 || timerfd_settime(tfd, 0, &its, NULL)) { perror("init"); return 1; }
	int fds[3] = { rx_s, tfd, sfd };
	for (int i = 0; i < 3; i++) {
		struct epoll_event e = { .events = EPOLLIN, .data.fd = fds[i] };
		epoll_ctl(ep, EPOLL_CTL_ADD, fds[i], &e);
	}
	ev("START rx=%s tx=%s routing=%s late=%ux timeout=%ux", rx_if, tx_if,
	   own_routing ? "own(E2E-gated)" : "off", late_x, timeout_x);
	for (;;) {
		struct epoll_event e[4];
		int n = epoll_wait(ep, e, 4, -1);
		for (int i = 0; i < n; i++) {
			if (e[i].data.fd == rx_s) rx_one();
			else if (e[i].data.fd == tfd) {
				uint64_t x;
				if (read(tfd, &x, 8) != 8) continue;
				if (x > 4) ev("STALL daemon not scheduled for ~%llu ms", (unsigned long long)x * 5);
				while (rx_one()) ;              /* drain queued frames BEFORE judging deadlines */
				tick();
			}
			else {
				struct signalfd_siginfo si;
				if (read(sfd, &si, sizeof(si)) != sizeof(si)) continue;
				summary();
				if (si.ssi_signo != SIGUSR1) return 0;
			}
		}
	}
}
```

빌드 (VM 호스트, 경고 0개):

```sh
make -C daemon OUT=/var/tmp/S7/build
```

```text
cc -std=gnu11 -O2 -Wall -Wextra -I../gen -I../common  -static canmond.c ../gen/s7_sensor_kit.c -o /var/tmp/S7/build/canmond
```

### 7.7 첫 실행 — 임계값은 측정해서 정한다

처음에는 AUTOSAR 예제에서 흔한 값처럼 LATE = 2 × 주기, 타임아웃 = 5 × 주기로 시작했다. 고장 주입 **전** 3초의 실제 출력 (초기 버전 바이너리, 앞부분):

```text
[   336.373] START rx=s7sl0 tx=s7vc1 routing=own(E2E-gated) late=2x timeout=5x
[   336.392] STATE IMU_GYRO NO_DATA -> OK (first frame)
[   336.399] STATE IMU_ACCEL NO_DATA -> OK (first frame)
[   336.464] STATE TEMP_STATUS NO_DATA -> OK (first frame)
[   337.393] DECODE IMU_ACCEL x=-1.08 y=-0.42 z=9.81 m/s2
[   337.569] STATE IMU_ACCEL OK -> LATE (deadline)
[   337.570] LATE IMU_ACCEL gap=24.8ms
[   337.577] STATE IMU_ACCEL LATE -> OK (frame arrived)
[   337.579] STATE IMU_GYRO OK -> LATE (deadline)
[   337.579] LATE IMU_GYRO gap=28.8ms
[   337.580] STATE IMU_GYRO LATE -> OK (frame arrived)
[   338.430] DECODE IMU_ACCEL x=1.08 y=0.42 z=9.81 m/s2
[   338.958] STATE IMU_GYRO OK -> LATE (deadline)
[   338.973] STATE IMU_ACCEL OK -> LATE (deadline)
[   338.975] LATE IMU_GYRO gap=22.5ms
[   338.975] STATE IMU_GYRO LATE -> OK (frame arrived)
[   338.977] STATE IMU_ACCEL LATE -> OK (frame arrived)
[   339.273] STATE IMU_GYRO OK -> LATE (deadline)
[   339.278] STATE IMU_ACCEL OK -> LATE (deadline)
[   339.303] STATE IMU_GYRO LATE -> MISSING (timeout)
[   339.308] STATE IMU_ACCEL LATE -> MISSING (timeout)
[   339.313] LATE IMU_GYRO gap=62.0ms
[   339.317] LATE IMU_ACCEL gap=62.1ms
[   339.333] STATE IMU_GYRO MISSING -> OK (3 good frames)
[   339.338] STATE IMU_ACCEL MISSING -> OK (3 good frames)
```

고장을 하나도 넣지 않았는데 IMU가 LATE(간격 22~29 ms)를 여러 번 찍고, 339.30에서는 **MISSING까지** 갔다(간격 62 ms). 원인은 데몬이 아니라 환경이다: TCG에서 게스트 vCPU나 ECU QEMU가 수십 ms씩 멈춘다. 9.3절의 히스토그램이 이 꼬리를 보여 준다(idle에서도 최대 30 ms, 게스트 부하 시 96 ms).

교훈 — 실무에서도 똑같다: 타임아웃은 "주기의 몇 배"를 외워서 정하는 게 아니라 **실제 지터 분포의 꼬리(p99.9, max)보다 충분히 크게**, 그러면서도 시스템이 요구하는 검출 시간 안에서 정한다. 이 랩에서는 LATE = 3 × 주기(30 ms), 타임아웃 = 10 × 주기(100 ms)로 했다(`-L 3 -T 10`). 실보드라면 측정해서 훨씬 줄일 수 있다.

### 7.8 버그 하나 — 데몬이 자기 RX 큐와 경주했다

임계값을 올린 뒤에도 시스템 테스트 2회차에서 "baseline 4초 동안 타임아웃 0개"가 FAIL 났다. 데몬 로그의 그 순간:

```text
[   982.152] LATE IMU_ACCEL gap=30.3ms
[   982.217] STATE IMU_ACCEL OK -> LATE (deadline)
[   982.218] STATE IMU_GYRO OK -> MISSING (timeout, age 102 ms)
[   982.218] LATE IMU_GYRO gap=35.3ms
[   982.220] STATE IMU_ACCEL LATE -> OK (frame arrived)
[   982.222] STATE IMU_GYRO MISSING -> OK (3 good frames)
```

모순이 보인다: 데몬은 "마지막 처리 후 102 ms"라며 MISSING을 냈는데, 바로 다음 줄의 **커널 타임스탬프 기준 간격은 35.3 ms**다. 즉 프레임은 제때 커널에 도착해 소켓 큐에 들어 있었고, 데몬 프로세스가 약 70 ms 동안 스케줄되지 못했다. 깨어난 순간 epoll이 timerfd와 소켓을 **동시에** 알려 줬고, 루프가 timerfd를 먼저 처리해서 큐 안의 프레임을 보기도 전에 타임아웃을 선언했다.

수정 두 가지(7.6절 코드에 반영):

1. timerfd 이벤트에서 `tick()` 전에 `while (rx_one()) ;`로 **큐를 먼저 비운다**. "판정은 이미 도착한 증거를 다 본 뒤에" — SSD 펌웨어에서 타임아웃 핸들러가 완료 큐를 먼저 폴링하던 것과 같은 원칙이다.
2. timerfd의 `read()` 값(놓친 만료 횟수)이 4를 넘으면 `STALL` 이벤트를 찍는다. 데몬이 **자기 자신이 굶었다는 것**을 로그에 남겨, "센서가 늦었다"와 "내가 늦게 봤다"를 구분한다.

수정 후 실제 STALL 수: 부하가 높던 3회차 실행에서 98번(최대 `~205 ms`), 조용한 20초 측정에서 0번, 게스트에 `stress-ng --cpu 2`를 건 20초에서 6번. 이후 baseline 타임아웃 FAIL은 다시 나오지 않았다(CI 연속 2회 PASS, 11절). 실보드라면 이 데몬을 `SCHED_FIFO`로 올리고 CPU를 고정하는 것이 정석이다(N2).

**체크포인트**: `canmond -i s7sl0 -o s7vc1 -L 3 -T 10 -d`를 띄우면 `NO_DATA -> OK (first frame)`이 3줄, 1초마다 `DECODE IMU_ACCEL … z=9.81`이 찍히고, 조용한 환경에서 MISSING이 없어야 한다. 조용한 20초 실행의 요약:

```text
[  1482.620] SUMMARY IMU_ACCEL rx=1966 bad_crc=0 repeated=0 lost=0 late=0 timeouts=0 gap_us min=6752 avg=10118 max=29963 state=OK
[  1482.621] SUMMARY IMU_GYRO rx=1966 bad_crc=0 repeated=0 lost=0 late=3 timeouts=0 gap_us min=3406 avg=10118 max=31811 state=OK
[  1482.621] SUMMARY TEMP_STATUS rx=196 bad_crc=0 repeated=0 lost=0 late=0 timeouts=0 gap_us min=97887 avg=101204 max=136269 state=OK
[  1482.621] SUMMARY bus=ACTIVE restarts=0 bus_errors=0 rx_ovfl=0 forwarded=4128
```

---

## 8. 단계 7 — 로거 `canlogd`: candump 호환 + 회전

### 8.1 왜 직접 짜나

`candump -l`은 회전이 없다. 장비에서 며칠씩 돌리면 파일 하나가 디스크를 채운다. 직접 짠 로거는 (1) **모든 CAN 인터페이스**를 소켓 하나로(`can_ifindex = 0`) 받고, (2) candump `-l` 형식 그대로 써서 `canplayer`·python-can·cantools가 바로 읽게 하고, (3) 크기로 회전하고 `SIGHUP`으로도 회전한다(logrotate 연동).

```c
static FILE *rotate(FILE *fp, const char *base, int keep)
{
	char a[512], b[512];
	if (fp) fclose(fp);
	for (int i = keep - 1; i >= 1; i--) {            /* base.(i) -> base.(i+1) */
		snprintf(a, sizeof(a), "%s.%d", base, i);
		snprintf(b, sizeof(b), "%s.%d", base, i + 1);
		rename(a, b);
	}
	snprintf(b, sizeof(b), "%s.1", base);
	rename(base, b);
	if (!(fp = fopen(base, "w"))) { perror(base); exit(1); }
	return fp;
}

static int fmt_frame(char *o, size_t sz, const struct can_frame *f)   /* candump style */
{
	int n;
	if (f->can_id & CAN_ERR_FLAG)
		n = snprintf(o, sz, "%08X#", f->can_id & (CAN_ERR_MASK | CAN_ERR_FLAG));
	else if (f->can_id & CAN_EFF_FLAG)
		n = snprintf(o, sz, "%08X#", f->can_id & CAN_EFF_MASK);
	else
		n = snprintf(o, sz, "%03X#", f->can_id & CAN_SFF_MASK);
	if (f->can_id & CAN_RTR_FLAG)
		return n + snprintf(o + n, sz - (size_t)n, "R");
	for (int i = 0; i < f->len && i < 8; i++)
		n += snprintf(o + n, sz - (size_t)n, "%02X", f->data[i]);
	return n;
}
```

```c
int main(int argc, char **argv)
{
	const char *base = NULL; long max = 1 << 20, size = 0; int keep = 5, o, on = 1, rb = 4 << 20;
	while ((o = getopt(argc, argv, "f:s:k:")) != -1) {
		if (o == 'f') base = optarg;
		else if (o == 's') max = atol(optarg);
		else if (o == 'k') keep = atoi(optarg);
	}
	if (!base) { fprintf(stderr, "usage: canlogd -f FILE [-s bytes] [-k keep]\n"); return 2; }
	struct sigaction sa = { .sa_handler = on_sig };        /* no SA_RESTART: recvmsg -> EINTR */
	sigaction(SIGTERM, &sa, NULL); sigaction(SIGINT, &sa, NULL); sigaction(SIGHUP, &sa, NULL);

	int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	can_err_mask_t em = CAN_ERR_MASK;
	struct sockaddr_can a = { .can_family = AF_CAN, .can_ifindex = 0 };   /* 0 = any CAN if */
	if (s < 0 || setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &em, sizeof(em)) ||
	    setsockopt(s, SOL_SOCKET, SO_TIMESTAMP, &on, sizeof(on)) ||
	    setsockopt(s, SOL_SOCKET, SO_RXQ_OVFL, &on, sizeof(on)) ||
	    bind(s, (struct sockaddr *)&a, sizeof(a))) { perror("can socket"); return 1; }
	if (setsockopt(s, SOL_SOCKET, SO_RCVBUFFORCE, &rb, sizeof(rb)))   /* root: beyond rmem_max */
		setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rb, sizeof(rb));

	FILE *fp = fopen(base, "a");
	if (!fp) { perror(base); return 1; }
	size = ftell(fp);
	unsigned long frames = 0, rot = 0, drained = 0; uint32_t drops = 0;
	for (;;) {                       /* after SIGTERM: drain what is still queued, then exit */
		struct can_frame f; struct sockaddr_can src; char ctl[128], line[96], ifn[IFNAMSIZ];
		struct iovec io = { &f, sizeof(f) };
		struct msghdr mh = { &src, sizeof(src), &io, 1, ctl, sizeof(ctl), 0 };
		struct timeval tv = { 0, 0 };
		if (recvmsg(s, &mh, stop ? MSG_DONTWAIT : 0) < 0) {
			if (stop && (errno == EAGAIN || errno == EINTR)) break;
			if (errno != EINTR && errno != ENETDOWN) { perror("recvmsg"); break; }
			if (rotate_req) { rotate_req = 0; fp = rotate(fp, base, keep); rot++; size = 0; }
			continue;
		}
		for (struct cmsghdr *c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c)) {
			if (c->cmsg_type == SO_TIMESTAMP) memcpy(&tv, CMSG_DATA(c), sizeof(tv));
			else if (c->cmsg_type == SO_RXQ_OVFL) memcpy(&drops, CMSG_DATA(c), sizeof(drops));
		}
		if (!if_indextoname((unsigned)src.can_ifindex, ifn)) snprintf(ifn, sizeof(ifn), "if%d", src.can_ifindex);
		fmt_frame(line, sizeof(line), &f);
		size += fprintf(fp, "(%lld.%06ld) %s %s\n", (long long)tv.tv_sec, (long)tv.tv_usec, ifn, line);
		drained += stop != 0;
		if (++frames % 64 == 0) fflush(fp);          /* no ftell(): it costs an lseek per frame */
		if (size >= max || rotate_req) { rotate_req = 0; fp = rotate(fp, base, keep); rot++; size = 0; }
	}
	fclose(fp);
	fprintf(stderr, "canlogd: frames=%lu rotations=%lu socket_drops=%u drained_at_exit=%lu\n",
		frames, rot, drops, drained);
	return 0;
}
```

### 8.2 실패에서 배우기 1 — 로거가 몰래 프레임을 버렸다

첫 시스템 테스트(25개 중 2개 FAIL)의 관련 줄:

```text
# canlogd: frames=9322 rotations=4 socket_drops=131
# s7sl0/IMU_ACCEL            2026   10   10.82   8.75   22.84  332.02      5    10
not ok 19 - log: exactly 3 frames lost (counter jump only)
# daemon rx=2063 logger count=2026
not ok 23 - logger and daemon agree on IMU_ACCEL count (+-5)
```

해석: 로거가 **131 프레임을 소켓 큐 overflow로 잃었다**(`socket_drops=131`, `SO_RXQ_OVFL` 값). 그래서 로그 기준 IMU_ACCEL이 데몬보다 37개 적고, 로그 분석의 `lost`가 10(실제 카운터 점프 3 + 로거 손실 7)으로 틀렸다. 같은 순간 데몬은 `rx_ovfl=0`이었다. 차이는 **필터**다: 플러드(0x050, 10.4절) 동안 데몬 소켓에는 커널 필터가 0x050을 넣지 않지만, 필터 없는 로거 소켓에는 전부 들어온다. 게다가 첫 버전은 프레임마다 `ftell()`(내부적으로 `lseek` 시스템 콜)을 불러 느렸다.

고친 것: `SO_RCVBUFFORCE`로 수신 버퍼를 4 MB로(root만 가능, 안 되면 `SO_RCVBUF` — 이쪽은 `net.core.rmem_max`로 잘린다), 쓴 바이트 수를 `fprintf` 반환값으로 직접 세서 `ftell` 제거. 다음 실행에서 `socket_drops=0`, 로그와 데몬 카운트 일치(1853 = 1853).

### 8.3 실패에서 배우기 2 — 종료할 때 큐에 남은 것

그 뒤 한 번 더 "로거 2267 < 데몬 2429"가 나왔다. 이번에는 `socket_drops=0`이고 로그 안의 카운터는 빈틈없이 이어졌다 — 즉 빠진 프레임은 **로그의 앞이나 끝**에 있다. 가설: 로거가 TCG에서 느려 큐에 밀린 프레임이 있었는데, SIGTERM을 받자마자 종료해서 버렸다. `SIGTERM` 뒤에는 `MSG_DONTWAIT`로 큐가 빌 때까지 마저 읽고 끝나도록 고치고, 몇 개를 건졌는지 `drained_at_exit`로 찍게 했다. 다음 CI 실행의 실제 출력:

```text
# canlogd: frames=16973 rotations=7 socket_drops=0 drained_at_exit=414
# daemon rx=3776 logger count=3818
```

종료 순간 **414 프레임**이 큐에 밀려 있었다 — 가설이 맞았다. 이번엔 반대로 로거가 42개 더 많다(로거가 데몬보다 먼저 시작해서 나중에 끝나니 당연하다). 그래서 테스트 조건도 "±5개 일치"에서 **"로거 ≥ 데몬, 그리고 socket_drops = 0"**으로 바꿨다. 테스트가 틀린 가정을 하고 있었던 것도 이 과정에서 드러났다.

**체크포인트**: `kill -TERM`으로 멈춘 로거가 `socket_drops=0`을 찍고, `bus.log`, `bus.log.1` … 이 `-s` 크기 근처에서 잘려 있어야 한다.

---

## 9. 단계 8 — 분석 스크립트 `analyze.py` (python-can + cantools)

### 9.1 무엇을 계산하나

로그 파일 여러 개(회전된 것 포함)를 시간순으로 합쳐서:

- 채널·메시지별 수신 간격의 평균, std, p50, p99, 최대 — DBC의 주기와 비교
- E2E 메시지면 CRC 실패 수, 카운터 손실 수(거부된 프레임은 빼고), 신호별 물리값 범위(cantools 디코드)
- 에러 프레임 목록(클래스와 시각)
- 초 단위 **버스 부하** — 비트 스터핑과 CRC15까지 넣은 정확한 비트 수(Q4 9.2절의 `frame_bits`를 그대로 가져왔다)를 500 kbit/s로 나눔
- `--latency A:B`: 같은 ID·데이터가 A에서 보이고 B에서 다시 보일 때까지의 시간 = 게이트웨이 전달 지연

핵심부(앞의 CRC·비트 계산 함수와 뒤의 출력부는 생략, 전체 116줄):

```python
ap = argparse.ArgumentParser()
ap.add_argument("dbc"); ap.add_argument("logs", nargs="+")
ap.add_argument("--bitrate", type=int, default=500000)
ap.add_argument("--json"); ap.add_argument("--hist")
ap.add_argument("--latency", help="SRC:DST channels: forward delay of identical frames")
a = ap.parse_args()
db = cantools.database.load_file(a.dbc)
msgs, errs = [], []
for f in a.logs:            # python-can 4.3 only flags error frames that carry CAN_ERR_BUSERROR
    keep = []               # and drops their class/channel -> parse error frames ourselves
    for line in open(f):
        ts, ch, fr = line.split()[:3]
        cid = fr.split("#")[0]
        if len(cid) == 8 and int(cid, 16) & 0x20000000:
            errs.append((ch, float(ts[1:-1]), int(cid, 16) & 0x1FFFFFFF))
        else:
            keep.append(line)
    msgs += list(can.CanutilsLogReader(io.StringIO("".join(keep))))
msgs.sort(key=lambda m: m.timestamp)              # rotated files -> one timeline
errs.sort(key=lambda e: e[1])
by = collections.defaultdict(list); bits = collections.Counter()
for m in msgs:
    by[(m.channel, m.arbitration_id)].append(m)
    bits[(m.channel, int(m.timestamp))] += frame_bits(m.arbitration_id, m.data, m.is_extended_id)

report = {"messages": {}, "error_frames": [], "busload": {}}
for (ch, cid), ms in sorted(by.items()):
    try: dm = db.get_message_by_frame_id(cid)
    except KeyError: dm = None
    name = dm.name if dm else f"0x{cid:03X}"
    gaps = [(b.timestamp - a_.timestamp) * 1e3 for a_, b in zip(ms, ms[1:])]
    r = {"count": len(ms), "cycle_ms": dm.cycle_time if dm else None}
    if gaps:
        r.update(mean_ms=statistics.fmean(gaps), std_ms=statistics.pstdev(gaps),
                 p50_ms=pct(gaps, 50), p99_ms=pct(gaps, 99), max_ms=max(gaps), min_ms=min(gaps))
    if dm and any(s.name.endswith("_CRC") for s in dm.signals):   # E2E-protected message
        bad = lost = rep = rej = 0; last = None; rng = {}
        for m in ms:
            d = bytes(m.data)
            if crc8(bytes([cid & 0xFF, cid >> 8]) + d[1:]) != d[0]: bad += 1; rej += 1; continue
            c = d[1] & 15
            if last is not None:
                delta = (c - last) & 15
                rep += delta == 0; lost += max(0, delta - 1 - rej)   # rejected != lost
            last = c; rej = 0
            for k, v in dm.decode(d).items():
                if not k.endswith(("_CRC", "_CNT", "_STAT")):
                    lo, hi = rng.get(k, (v, v)); rng[k] = (min(lo, v), max(hi, v))
        r.update(bad_crc=bad, counter_lost=lost, repeated=rep,
                 ranges={k: [round(lo, 2), round(hi, 2)] for k, (lo, hi) in rng.items()})
    report["messages"][f"{ch}/{name}"] = r
    if a.hist and a.hist.split(":")[0] == ch and int(a.hist.split(":")[1], 16) == cid:
        open(a.hist.split(":")[2], "w").write("\n".join(f"{g:.3f}" for g in gaps) + "\n")
```

### 9.2 툴 함정 — python-can 4.3이 에러 프레임을 데이터 프레임으로 읽는다

처음 버전은 에러 프레임도 python-can에 맡겼다. 실제 출력:

```text
s7sl0/0x040                   1    -    0.00   0.00    0.00    0.00      -     -
s7sl0/IMU_ACCEL             883   10   10.52   5.17   23.58  143.20      3     6
...
s7sl0/0x204                   1    -    0.00   0.00    0.00    0.00      -     -
...
error frame None t=+5.052s class=0x00000000
```

bus-off(0x040)와 상태 변화(0x204)가 **ID 0x040, 0x204인 데이터 프레임**으로 둔갑했고, ACK 에러(0x0A8) 하나만 에러 프레임으로 잡혔는데 채널(`None`)과 클래스(0)를 잃었다. python-can 4.3.1의 `CanutilsLogReader.__iter__` 소스(게스트에서 `inspect.getsource`로 확인)에 이유가 있다:

```text
            if can_id & CAN_ERR_FLAG and can_id & CAN_ERR_BUSERROR:
                msg = Message(timestamp=timestamp, is_error_frame=True)
            else:
                msg = Message(
                    timestamp=timestamp,
                    arbitration_id=can_id & 0x1FFFFFFF,
```

`CAN_ERR_BUSERROR`(0x80) 비트가 있는 것만 에러 프레임으로 보고, 그때도 ID·채널을 버린다. 그래서 위 코드처럼 **에러 프레임 줄(8자리 ID에 0x20000000 비트)은 직접 파싱**하고 나머지만 python-can에 넘긴다. Q4 8.4절에서 본 "오래된 candump가 0x204를 모른다"와 같은 종류의 함정이다 — 도구의 디코더를 믿기 전에 원시 값을 한 번 보자.

### 9.3 실행 — 조용한 20초, 그리고 게스트 CPU 부하 20초

```sh
canlogd -f /tmp/g7/base.log -s 10000000 &  canmond -i s7sl0 -o s7vc1 -L 3 -T 10 -d &
sleep 20; kill ...                                  # 두 번째는 stress-ng --cpu 2 를 같이 돌림
python3 tools/analyze.py dbc/s7_sensor_kit.dbc /tmp/g7/base.log --hist s7sl0:110:gaps110.csv --latency s7sl0:s7vc1
```

```text
canlogd: frames=8462 rotations=0 socket_drops=0 drained_at_exit=1
8462 frames, 1 file(s), span 19.9 s
channel/message               n  cyc    mean    std     p99     max badcrc  lost
s7sl0/IMU_ACCEL            1972   10   10.12   1.19   13.41   29.96      0     0
s7sl0/IMU_GYRO             1972   10   10.12   1.49   14.01   31.81      0     0
s7sl0/TEMP_STATUS           197  100  101.20   4.27  119.31  136.27      0     0
s7vc1/IMU_ACCEL            1966   10   10.12   1.41   14.00   31.89      0     0
s7vc1/IMU_GYRO             1966   10   10.12   1.60   14.04   32.20      0     0
s7vc1/TEMP_STATUS           196  100  101.20   4.62  120.47  137.19      0     0
s7vc1/GW_STATUS             193  100  103.05   2.67  106.47  122.76      -     -
busload s7sl0: avg 4.90 %  peak 4.96 %  (@500000 bit/s, 19 s)
busload s7vc1: avg 5.14 %  peak 5.21 %  (@500000 bit/s, 19 s)
forward latency s7sl0->s7vc1: n=4128 p50=484 us p99=2399 us max=11795 us
```

같은 측정을 게스트에서 `stress-ng --cpu 2 --timeout 20`과 함께 돌린 결과(요약 줄만):

```text
s7sl0/IMU_ACCEL            1946   10   10.33   2.78   18.29   95.84      0     0
s7vc1/IMU_ACCEL            1939   10   10.31   4.46   18.90  158.03      0     0
forward latency s7sl0->s7vc1: n=4071 p50=812 us p99=11287 us max=148645 us
```

출력에서 볼 것:

- idle: IMU_ACCEL 평균 10.12 ms, std 1.19 ms, p99 13.41 ms. 부하: std 2.78 ms, p99 18.29 ms, 최대 95.84 ms. 센서 버스(`s7sl0`)의 간격까지 넓어진 것은 slcan 수신 경로(tty flip buffer → line discipline 작업, 12절)가 일반 커널 작업이라 stress-ng와 CPU를 다투기 때문이다.
- 버스 부하 4.90 % — 손으로 검산: 8바이트 표준 프레임은 스터핑 전 111비트, 스터핑 포함 평균 약 115~117비트. 초당 210프레임 × 약 117비트 ≈ 24.5 kbit ≈ 500 kbit/s의 4.9 %. 백본(`s7vc1`)은 GW_STATUS 10 Hz가 더해져 5.14 %.
- 전달 지연 p50 484 us → 부하 시 812 us, p99 2.4 ms → 11.3 ms. **유저 공간 게이트웨이의 꼬리 지연은 CPU 경쟁에 그대로 노출된다** — 10.5절 can-gw 비교와 N2(RT 튜닝)로 이어지는 숫자다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">IMU_ACCEL 수신 간격 히스토그램 (커널 SO_TIMESTAMP, 20 s, TCG)</text><line x1="70" y1="290.0" x2="660" y2="290.0" stroke="#888" stroke-width="0.5"/><text x="62.0" y="294.0" font-size="11" text-anchor="end">1</text><line x1="70" y1="230.0" x2="660" y2="230.0" stroke="#888" stroke-width="0.5"/><text x="62.0" y="234.0" font-size="11" text-anchor="end">10</text><line x1="70" y1="170.0" x2="660" y2="170.0" stroke="#888" stroke-width="0.5"/><text x="62.0" y="174.0" font-size="11" text-anchor="end">100</text><line x1="70" y1="110.0" x2="660" y2="110.0" stroke="#888" stroke-width="0.5"/><text x="62.0" y="114.0" font-size="11" text-anchor="end">1000</text><line x1="70" y1="50.0" x2="660" y2="50.0" stroke="#888" stroke-width="0.5"/><text x="62.0" y="54.0" font-size="11" text-anchor="end">10000</text><line x1="70" y1="290" x2="660" y2="290" stroke="currentColor" stroke-width="1.3"/><text x="92.7" y="306.0" font-size="11" text-anchor="middle">0-2</text><text x="138.1" y="306.0" font-size="11" text-anchor="middle">2-4</text><rect x="139.1" y="271.9" width="14.7" height="18.1" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="146.4" y="267.9" font-size="10" text-anchor="middle">2</text><text x="183.5" y="306.0" font-size="11" text-anchor="middle">4-6</text><rect x="184.5" y="261.4" width="14.7" height="28.6" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="191.8" y="257.4" font-size="10" text-anchor="middle">3</text><text x="228.8" y="306.0" font-size="11" text-anchor="middle">6-8</text><rect x="212.2" y="243.3" width="14.7" height="46.7" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><rect x="229.8" y="203.2" width="14.7" height="86.8" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="274.2" y="306.0" font-size="11" text-anchor="middle">8-10</text><rect x="257.5" y="110.9" width="14.7" height="179.1" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="264.9" y="106.9" font-size="10" text-anchor="middle">965</text><rect x="275.2" y="113.3" width="14.7" height="176.7" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="282.6" y="109.3" font-size="10" text-anchor="middle">881</text><text x="319.6" y="306.0" font-size="11" text-anchor="middle">10-12</text><rect x="302.9" y="110.8" width="14.7" height="179.2" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="310.3" y="106.8" font-size="10" text-anchor="middle">968</text><rect x="320.6" y="112.2" width="14.7" height="177.8" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="328.0" y="108.2" font-size="10" text-anchor="middle">919</text><text x="365.0" y="306.0" font-size="11" text-anchor="middle">12-14</text><rect x="348.3" y="217.8" width="14.7" height="72.2" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><rect x="366.0" y="177.8" width="14.7" height="112.2" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="410.4" y="306.0" font-size="11" text-anchor="middle">14-16</text><rect x="393.7" y="261.4" width="14.7" height="28.6" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="401.0" y="257.4" font-size="10" text-anchor="middle">3</text><rect x="411.4" y="225.2" width="14.7" height="64.8" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="455.8" y="306.0" font-size="11" text-anchor="middle">16-18</text><rect x="439.1" y="271.9" width="14.7" height="18.1" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="446.4" y="267.9" font-size="10" text-anchor="middle">2</text><rect x="456.8" y="243.3" width="14.7" height="46.7" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="501.2" y="306.0" font-size="11" text-anchor="middle">18-20</text><rect x="484.5" y="271.9" width="14.7" height="18.1" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="491.8" y="267.9" font-size="10" text-anchor="middle">2</text><rect x="502.2" y="239.3" width="14.7" height="50.7" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="546.5" y="306.0" font-size="11" text-anchor="middle">20-22</text><rect x="529.8" y="271.9" width="14.7" height="18.1" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="537.2" y="267.9" font-size="10" text-anchor="middle">2</text><rect x="547.5" y="290.0" width="14.7" height="0.0" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="554.9" y="286.0" font-size="10" text-anchor="middle">1</text><text x="591.9" y="306.0" font-size="11" text-anchor="middle">22-24</text><rect x="575.2" y="290.0" width="14.7" height="0.0" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="582.6" y="286.0" font-size="10" text-anchor="middle">1</text><rect x="592.9" y="271.9" width="14.7" height="18.1" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="600.3" y="267.9" font-size="10" text-anchor="middle">2</text><text x="637.3" y="306.0" font-size="11" text-anchor="middle">24+</text><rect x="620.6" y="243.3" width="14.7" height="46.7" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="628.0" y="239.3" font-size="10" text-anchor="middle">6</text><rect x="638.3" y="230.0" width="14.7" height="60.0" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="645.7" y="226.0" font-size="10" text-anchor="middle">10</text><text x="365.0" y="324.0" font-size="12" text-anchor="middle">간격 구간 (ms) — 기대값 10 ms</text><text x="16.0" y="170.0" font-size="12" text-anchor="middle">개수</text><rect x="300.0" y="58.0" width="12.0" height="12.0" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="318.0" y="68.0" font-size="11" text-anchor="start">idle: std 1.19 · p99 13.4 · max 30.0 ms</text><rect x="300.0" y="78.0" width="12.0" height="12.0" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="318.0" y="88.0" font-size="11" text-anchor="start">stress-ng --cpu 2: std 2.78 · p99 18.3 · max 95.8</text><text x="10.0" y="350.0" font-size="12" text-anchor="start">세로축 로그 스케일. 꼬리(24 ms 이상)는 TCG vCPU 정지 + 같은 VM의 다른 작업 부하 — 실보드 수치가 아니다.</text><text x="10.0" y="370.0" font-size="12" text-anchor="start">데몬 기준 timeout 10×주기(100 ms)는 idle 최대값의 3배 이상 여유. 3×주기 LATE는 부하 때 가끔 뜬다.</text></svg>
```

그림 4 — 두 실행의 IMU_ACCEL 수신 간격 히스토그램(2 ms 구간, 세로 로그 스케일). 기대값 10 ms 주변 8~12 ms에 거의 다 모였고, 부하를 걸면 12 ms 이상 꼬리가 10배쯤 두꺼워진다. 24 ms 이상(idle 6개, 부하 10개)이 7.7절의 가짜 LATE를 만든 범인이다.

**체크포인트**: `analyze.py`의 `s7sl0/IMU_ACCEL` 개수가 데몬 `SUMMARY`의 `rx`와 같거나 약간 크고, idle에서 `badcrc`·`lost`가 0이어야 한다.

---

## 10. 단계 9 — 고장 주입을 하나씩: 데몬은 어떻게 반응하나

고장은 전부 0x7E0 명령 하나로 ECU에 넣는다(`cansend s7sl0 7E0#<code><arg>`). 아래 출력은 11절 CI 실행 2회차의 실제 출력에서 해당 부분을 잘라 왔다.

### 10.1 송신 정지 (MUTE 300 ms) — 데드라인 감시

```sh
cansend s7sl0 7E0#011E        # 0x1E = 30 → 300 ms 송신 정지
```

```text
# T2 stop the sender (mute 300 ms)
ok 4 - IMU_ACCEL timeout detected
ok 5 - IMU_ACCEL recovered after 3 good frames
# [    58.668] STATE IMU_ACCEL LATE -> MISSING (timeout, age 103 ms)
# [    58.894] STATE IMU_ACCEL MISSING -> OK (3 good frames)
```

정지 후 103 ms에 MISSING(타임아웃 10 × 10 ms + 틱 5 ms 해상도), 송신 재개 후 좋은 프레임 3개(약 30 ms)를 받고 OK. 그 사이 백본에는 IMU 프레임이 안 나가고 GW_STATUS의 `ACCEL_STATE`가 3(MISSING)으로 나간다 — 하류 노드는 "데이터가 없다"와 "데이터가 없다는 것을 게이트웨이가 안다"를 구분할 수 있다.

### 10.2 CRC 오염 5개 — E2E와 재생 회귀

```text
# T3 corrupt CRC x5
ok 6 - exactly 5 BAD_CRC frames rejected
ok 7 - BAD_DATA -> OK after debounce
ok 8 - rejected frames not counted as lost
```

5개 모두 `BAD_CRC`로 거부, BAD_DATA → OK 디바운스, 그리고 "거부된 프레임은 손실로 세지 않음"(7.4절 수정)까지 확인한다. 그리고 이 순간을 **기록에서 다시 재생**해 같은 판정이 나오는지 본다 — 로그 포맷이 candump 호환이라 `canplayer`가 바로 먹는다:

```text
# T9 replay regression: recorded fault window -> canplayer -> second daemon
ok 24 - replay reproduces 5 BAD_CRC
ok 25 - replay reproduces GAP lost=3
# replay: [    87.471] SUMMARY IMU_ACCEL rx=277 bad_crc=5 repeated=0 lost=3 late=1 timeouts=1 gap_us min=854 avg=10500 max=90408 state=MISSING
```

`canplayer -I replay.log s7vc2=s7sl0`로 기록 구간만 vcan에 다시 뿌리고 두 번째 데몬을 붙였다. 현장에서 잡은 로그 하나가 **영구 회귀 테스트**가 된다 — "그 버그 고쳤나?"를 장비 없이 확인하는 방법이다. (재생은 원래 시간 간격을 따른다. 재생이 끝나면 프레임이 끊기니 마지막 상태가 MISSING인 것은 정상이다.)

### 10.3 카운터 점프 — 진짜 손실

```text
# T4 alive-counter jump
ok 9 - counter gap of 3 detected
```

카운터가 +3 뛰면 `GAP lost=3`. 이 고장만이 진짜 "프레임 손실"이고, 9절 분석의 `lost`도 정확히 3이어야 한다(CI 테스트 19번).

### 10.4 버스 범람 — 높은 우선순위 0x050을 2초간

```sh
timeout 2 cangen s7sl0 -I 050 -L 8 -D i -g 0 -i
```

```text
# T5 flood the sensor bus with high-priority 0x050 for 2 s

Counted 9350 ENOBUFS return values on write().

ok 10 - daemon alive after flood
ok 11 - kernel filter shielded daemon (no socket overflow)
ok 12 - sensor stream OK after flood
# during flood: [    63.781] LATE IMU_GYRO gap=30.5ms
# during flood: [    63.842] STATE IMU_ACCEL OK -> LATE (deadline)
# during flood: [    63.843] STATE IMU_GYRO OK -> LATE (deadline)
# during flood: [    63.844] LATE IMU_ACCEL gap=36.5ms
```

출력에서 볼 것:

- `ENOBUFS` 9350번: slcan의 TX 큐(qlen 10)와 시리얼 속도가 송신을 막았다. 2초 동안 실제로 나간 0x050은 854개(분석 결과, 11절), 센서 버스 부하는 최대 16 %까지 올랐다.
- 데몬은 살아 있고 **소켓 overflow 0** — 커널 필터가 0x050을 데몬 큐에 넣지 않는다. 같은 플러드에 필터 없는 로거는 첫 버전에서 131개를 잃었다(8.2절).
- 플러드 중 IMU 간격이 30~36 ms로 벌어졌다. 하지만 **원인을 정직하게 적자**: 이 랩의 "버스"는 UART 양방향 스트림이라 중재(arbitration)가 없다. 지연은 MCU가 들어오는 플러드 명령을 폴링·응답(`z\r`)하느라 자기 송신을 늦춘 것과 게스트 CPU 부하다. 진짜 CAN 버스라면 0x050이 중재에서 0x110을 **매번 이기므로** 버스 부하가 100 %에 가까워지면 0x110이 아예 못 나간다 — 그것이 실버스 플러드의 본질이고, 데몬의 MISSING 판정이 그때 보호막이 된다.

### 10.5 전달 방식 비교 — 내 라우팅 vs 커널 can-gw

데몬을 `-n`(라우팅 끔)으로 띄우고 커널 게이트웨이 규칙 3개를 건다. 각 실행에서 3초 뒤 CRC 오염 5개를 넣었다.

```sh
for id in 110 111 120; do cangw -A -s s7sl0 -d s7vc1 -e -f $id:7FF; done
cangw -L
```

```text
## own
s7sl0/IMU_ACCEL             594   10   10.26   1.61   15.21   31.40      5     0
s7vc1/IMU_ACCEL             584   10   10.36   3.66   16.67   76.96      0     7
forward latency s7sl0->s7vc1: n=1233 p50=809 us p99=5240 us max=17380 us
cangw -A -s s7sl0 -d s7vc1 -e -f 120:7FF # 1 handled 0 dropped 0 deleted
cangw -A -s s7sl0 -d s7vc1 -e -f 111:7FF # 6 handled 0 dropped 0 deleted
cangw -A -s s7sl0 -d s7vc1 -e -f 110:7FF # 9 handled 0 dropped 0 deleted
## cangw
s7sl0/IMU_ACCEL             593   10   10.20   1.21   13.90   31.71      5     0
s7vc1/IMU_ACCEL             593   10   10.20   1.24   13.29   31.89      5     0
forward latency s7sl0->s7vc1: n=1245 p50=122 us p99=630 us max=3722 us
cangw -A -s s7sl0 -d s7vc1 -e -f 120:7FF # 136 handled 0 dropped 0 deleted
cangw -A -s s7sl0 -d s7vc1 -e -f 111:7FF # 1354 handled 0 dropped 0 deleted
cangw -A -s s7sl0 -d s7vc1 -e -f 110:7FF # 1357 handled 0 dropped 0 deleted
```

(첫 `cangw -L`은 규칙을 막 추가한 시점이라 카운터가 작다.) 결과를 그림으로:

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">전달 방식 비교 — 같은 고장(나쁜 CRC 5개) 주입, 6 s 측정 (g6 실행)</text><text x="190.0" y="65.0" font-size="12" text-anchor="end">own: p50 지연</text><rect x="200.0" y="52.0" width="234.6" height="20.0" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="440.6" y="66.0" font-size="11" text-anchor="start">809 us</text><text x="190.0" y="99.0" font-size="12" text-anchor="end">can-gw: p50 지연</text><rect x="200.0" y="86.0" width="168.3" height="20.0" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="374.3" y="100.0" font-size="11" text-anchor="start">122 us</text><text x="190.0" y="133.0" font-size="12" text-anchor="end">own: p99 지연</text><rect x="200.0" y="120.0" width="300.0" height="20.0" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="506.0" y="134.0" font-size="11" text-anchor="start">5240 us</text><text x="190.0" y="167.0" font-size="12" text-anchor="end">can-gw: p99 지연</text><rect x="200.0" y="154.0" width="225.8" height="20.0" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="431.8" y="168.0" font-size="11" text-anchor="start">630 us</text><text x="200.0" y="194.0" font-size="11" text-anchor="start">막대 = log10 스케일</text><rect x="20" y="210" width="310" height="74" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="175.0" y="236.3" font-size="12" text-anchor="middle" font-weight="bold">own routing (canmond)</text><text x="175.0" y="251.3" font-size="12" text-anchor="middle">나쁜 CRC 전달 0개 · 디바운스로 7개 보류</text><text x="175.0" y="266.3" font-size="12" text-anchor="middle">유저 공간 왕복 → 지연 큼</text><rect x="350" y="210" width="310" height="74" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="505.0" y="236.3" font-size="12" text-anchor="middle" font-weight="bold">can-gw (커널, -e)</text><text x="505.0" y="251.3" font-size="12" text-anchor="middle">나쁜 CRC 5개 그대로 전달</text><text x="505.0" y="266.3" font-size="12" text-anchor="middle">softirq 안에서 복사 → 지연 작음</text></svg>
```

그림 6 — 같은 고장에서 두 방식. can-gw는 softirq 안에서 skb를 복제해 바로 보내서 p50 122 us(7배 빠름)지만, **CRC가 틀린 5개를 그대로 하류로 흘렸다**. 내 라우팅은 0개를 흘렸고, 대신 디바운스 동안 좋은 프레임 7개도 보류했다(백본 `lost 7`). 정답은 하나가 아니다: 원시 데이터를 빠르게 넘기는 경로는 can-gw, 검증된 데이터만 넘기는 경로는 데몬 — 실제 차량 게이트웨이도 둘을 섞는다.

함정 하나 — 처음에는 `-e` 없이 규칙을 걸었는데, 규칙 카운터(`handled`)는 1262까지 올라가는데 **`s7vc1`의 로그에는 한 개도 안 찍혔다**. 커널 소스로 확인한 이유: can-gw는 `can_send(nskb, gwj->flags & CGW_FLAGS_CAN_ECHO)`로 보내고(`net/can/gw.c`), `-e`가 없으면 loopback = 0이다. 그리고 vcan의 `vcan_tx()`는 echo 모듈 인자가 꺼져 있으면 loopback이 아닌 프레임을 그냥 `consume_skb()`한다 — 진짜 컨트롤러라면 선로로 나가 다른 노드가 받았겠지만, vcan에는 "다른 노드"가 없다. **vcan으로 게이트웨이를 테스트할 때는 `-e`**.

### 10.6 bus-off — 데몬 없이 먼저, 그다음 데몬과

먼저 **대조 실험**: 데몬 없이 bus-off를 넣으면 어떻게 되나.

```sh
cansend s7sl0 7E0#0400; sleep 0.5
ip -details -statistics link show s7sl0 | grep -E "s7sl0|can state|bus-off|..."
echo "frames in 1 s while bus-off: $(timeout 1 candump s7sl0,110:7FF | wc -l)"
ip link set s7sl0 type can restart-ms 100; ip link set s7sl0 type can restart
ip link set s7sl0 down; ip link set s7sl0 up
echo "frames in 1 s after down/up: $(timeout 1 candump s7sl0,110:7FF | wc -l)"
```

```text
10: s7sl0: <NO-CARRIER,NOARP,UP> mtu 16 qdisc pfifo_fast state DOWN mode DEFAULT group default qlen 10
    can state BUS-OFF restart-ms 0 
	  re-started bus-errors arbit-lost error-warn error-pass bus-off
        159888   19986      0       0       0       0 
          8634    1083      2       0       0       0 
frames in 1 s while bus-off: 0
Error: Device doesn't support restart from Bus Off.
restart-ms rc=2
Error: Device doesn't support restart from Bus Off.
restart rc=2
frames in 1 s after down/up: 96
[ 1226.402091] s7sl0: renamed from can0
[ 1242.229717] s7sl0: bus-off
[ 1326.000680] s7sl0: bus-off
```

출력에서 볼 것: `NO-CARRIER … state DOWN`, `can state BUS-OFF`, 1초 동안 프레임 0개. 버스는 **스스로 돌아오지 않는다**. slcan에는 `do_set_mode`가 없어서 `restart-ms`도 수동 `restart`도 `Device doesn't support restart from Bus Off`로 거부된다(Q4 8.4절). `down/up` 하자 1초에 96개가 다시 들어왔다. (중간의 통계 두 줄은 RX/TX 패킷 통계 — grep이 6칸 카운터 값 줄을 놓쳤다. 6칸 값은 아래 데몬 실행에서 보인다.)

이제 데몬이 하는 일 — CI 실행의 실제 출력:

```text
# T6 bus-off reported by the adapter
ok 13 - PASSIVE -> BUS_OFF seen
ok 14 - daemon restarted the link
ok 15 - IMU_ACCEL back to OK after restart
# 	  re-started bus-errors arbit-lost error-warn error-pass bus-off
# 	  0          0          0          0          1          1         numtxqueues 1 numrxqueues 1 gso_max_size 65536 gso_max_segs 65535 tso_max_size 65536 tso_max_segs 65535 gro_max_size 65536 
# [    66.186] BUS ACTIVE -> PASSIVE (ctrl)
# [    66.187] BUS PASSIVE -> BUS_OFF (error frame)
# [    66.187] RESTART scheduled in 100 ms
# [    66.289] BUS BUS_OFF -> RESTARTING (backoff expired)
# [    66.311] BUS RESTARTING -> ACTIVE (link re-opened)
# [    66.311] NOTE recvmsg ENETDOWN (link went down)
# T7 shutdown, summary, log rotation
# [    67.254] SUMMARY IMU_ACCEL rx=2202 bad_crc=5 repeated=0 lost=3 late=15 timeouts=2 gap_us min=119 avg=10615 max=308855 state=OK
```

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">bus-off 복구 타임라인 — CI 실행 3의 실제 이벤트 (ms, PASSIVE = 0)</text><line x1="70" y1="125" x2="620" y2="125" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><line x1="70.0" y1="121" x2="70.0" y2="129" stroke="currentColor" stroke-width="1.3"/><text x="70.0" y="141.0" font-size="10" text-anchor="middle">0</text><line x1="137.5" y1="121" x2="137.5" y2="129" stroke="currentColor" stroke-width="1.3"/><text x="137.5" y="141.0" font-size="10" text-anchor="middle">20</text><line x1="205.0" y1="121" x2="205.0" y2="129" stroke="currentColor" stroke-width="1.3"/><text x="205.0" y="141.0" font-size="10" text-anchor="middle">40</text><line x1="272.5" y1="121" x2="272.5" y2="129" stroke="currentColor" stroke-width="1.3"/><text x="272.5" y="141.0" font-size="10" text-anchor="middle">60</text><line x1="340.0" y1="121" x2="340.0" y2="129" stroke="currentColor" stroke-width="1.3"/><text x="340.0" y="141.0" font-size="10" text-anchor="middle">80</text><line x1="407.5" y1="121" x2="407.5" y2="129" stroke="currentColor" stroke-width="1.3"/><text x="407.5" y="141.0" font-size="10" text-anchor="middle">100</text><line x1="475.0" y1="121" x2="475.0" y2="129" stroke="currentColor" stroke-width="1.3"/><text x="475.0" y="141.0" font-size="10" text-anchor="middle">120</text><line x1="542.5" y1="121" x2="542.5" y2="129" stroke="currentColor" stroke-width="1.3"/><text x="542.5" y="141.0" font-size="10" text-anchor="middle">140</text><line x1="610.0" y1="121" x2="610.0" y2="129" stroke="currentColor" stroke-width="1.3"/><text x="610.0" y="141.0" font-size="10" text-anchor="middle">160</text><line x1="70.0" y1="125" x2="70.0" y2="58" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4"/><rect x="17.0" y="32" width="106" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="70.0" y="49.0" font-size="11" text-anchor="middle">PASSIVE</text><line x1="73.375" y1="125" x2="73.375" y2="152" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4"/><rect x="20.375" y="152" width="106" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="73.4" y="169.0" font-size="11" text-anchor="middle">BUS_OFF</text><line x1="157.75" y1="125" x2="157.75" y2="98" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4"/><rect x="104.75" y="72" width="106" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="157.8" y="89.0" font-size="11" text-anchor="middle">ACCEL LATE</text><line x1="404.125" y1="125" x2="404.125" y2="192" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4"/><rect x="351.125" y="192" width="106" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="404.1" y="209.0" font-size="11" text-anchor="middle">ACCEL MISSING</text><line x1="410.875" y1="125" x2="410.875" y2="58" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4"/><rect x="357.875" y="32" width="106" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="410.9" y="49.0" font-size="11" text-anchor="middle">RESTARTING</text><line x1="481.75" y1="125" x2="481.75" y2="152" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4"/><rect x="428.75" y="152" width="106" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="481.8" y="169.0" font-size="11" text-anchor="middle">ACTIVE</text><line x1="508.75" y1="125" x2="508.75" y2="98" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4"/><rect x="455.75" y="72" width="106" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="508.8" y="89.0" font-size="11" text-anchor="middle">첫 프레임</text><line x1="576.25" y1="125" x2="576.25" y2="192" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4"/><rect x="523.25" y="192" width="106" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="576.2" y="209.0" font-size="11" text-anchor="middle">ACCEL OK</text><rect x="73.4" y="122.0" width="408.4" height="6.0" rx="0" fill="#d0564a" fill-opacity="0.35"/><text x="10.0" y="256.0" font-size="12" text-anchor="start">0~1 ms: MCU가 sp000128, sb000256 보고 → slcan이 can_change_state() / can_bus_off() 호출</text><text x="10.0" y="276.0" font-size="12" text-anchor="start">+1 ms: 데몬이 CAN_ERR_BUSOFF 수신, 100 ms backoff 예약 (즉시 재시작 = bus-off 폭풍 위험)</text><text x="10.0" y="296.0" font-size="12" text-anchor="start">+101~122 ms: ioctl ifdown → C, ifup → C / S6 / O 를 MCU에 보냄 → MCU busoff 해제</text><text x="10.0" y="316.0" font-size="12" text-anchor="start">+150 ms: 좋은 프레임 3개 디바운스 후 OK. 데이터가 끊긴 구간 ≈ 140 ms (gap=140.6ms)</text><text x="10.0" y="336.0" font-size="12" text-anchor="start">TCG 시간이다. 실버스 bus-off 복구는 128 × 11 recessive 비트(500k에서 약 2.8 ms)가 더 든다.</text></svg>
```

그림 5 — 다른 CI 실행(3회차)의 데몬 로그 전체 타임스탬프로 그린 bus-off 복구 타임라인. PASSIVE 보고부터 IMU 데이터가 OK로 돌아오기까지 150 ms, 데이터 공백 약 140 ms. backoff 100 ms가 대부분이고, 재오픈(ifdown/ifup → MCU에 `C`/`S6`/`O`)은 21 ms 걸렸다.

출력에서 볼 것:

- `error-pass 1, bus-off 1` 카운터는 `can_change_state()`/`can_bus_off()`가 올린 것 — 진짜 컨트롤러 드라이버와 같은 경로다.
- `NOTE recvmsg ENETDOWN`: ifdown 순간 커널(`raw_notify()`, `NETDEV_DOWN`)이 소켓에 `sk_err = ENETDOWN`을 걸었다. 다음 `recvmsg()`가 한 번 실패한다. **이걸 치명적 에러로 처리해서 종료하는 데몬이 흔한 버그다.**
- 재시작 직후 같은 고장이 또 나면 backoff가 200 ms, 400 ms … 2 s까지 늘어나고, IMU가 OK로 돌아오면 100 ms로 리셋된다(`tick()` 코드).

**체크포인트**: 데몬 로그에 `BUS PASSIVE -> BUS_OFF` → `RESTART scheduled in 100 ms` → `RESTARTING -> ACTIVE` → `IMU_ACCEL MISSING -> OK` 순서가 1초 안에 나와야 한다.

---

## 11. 단계 10 — 전부 자동으로: 시스템 테스트와 CI 스크립트

### 11.1 게스트 안 시스템 테스트 `tests/guest_test.sh`

설계: 테스트 함수는 TAP 한 줄(`ok N - …` / `not ok N - …`)만 낸다. 데몬 로그를 "마크 이후 줄"로 잘라(`mark`/`since`) 고장마다 새로 생긴 이벤트만 본다. `waitev`는 정해진 시간 안에 이벤트가 나타나길 기다린다 — `sleep` 고정값보다 덜 깨진다. 앞부분(헬퍼 + 셋업 + T1~T2):

```sh
#!/bin/bash
# guest_test.sh - runs INSIDE the Linux guest (Image-dom + rootfs-dom). TAP output.
# Needs: ECU QEMU on /dev/ttyS0, repo at $K, binaries at $K/bin. Last line: RESULT: PASS/FAIL
K=${K:-/mnt/s7}; B=$K/bin; W=/tmp/s7ci; DBC=$K/dbc/s7_sensor_kit.dbc
n=0; fails=0
ok()    { n=$((n+1)); if eval "$2"; then echo "ok $n - $1"; else echo "not ok $n - $1"; fails=$((fails+1)); fi; }
mark()  { M=$(wc -l < $W/canmond.out); }
since() { tail -n +$((M+1)) $W/canmond.out; }
waitev() { for _ in $(seq $((${2:-2}*10))); do since | grep -qE "$1" && return 0; sleep 0.1; done; return 1; }
cnt()   { since | grep -cE "$1"; }

# --- setup: sensor bus = slcan over pci-serial, backbone = vcan ---
rm -rf $W; mkdir -p $W/log
modprobe slcan; modprobe vcan
for i in s7vc1 s7vc2 s7vc3; do ip link add $i type vcan 2>/dev/null; ip link set $i up; done
stty -F /dev/ttyS0 raw -echo 115200
pkill slcand; slcand /dev/ttyS0 s7sl0; sleep 1
ip link set s7sl0 type can bitrate 500000 && ip link set s7sl0 up
$B/canlogd -f $W/log/bus.log -s 100000 -k 8 2> $W/canlogd.err & LOG=$!
$B/canmond -i s7sl0 -o s7vc1 -L 3 -T 10 > $W/canmond.out 2>&1 & MON=$!
M=0; sleep 4
echo "# T1 baseline"
ok "all 3 sensor messages reached OK" '[ $(cnt "NO_DATA -> OK") -eq 3 ]'
ok "no timeout during 4 s baseline" '[ $(cnt "MISSING") -eq 0 ]'
timeout 3 candump -L -n 1 s7vc1,500:7FF > $W/gw.txt
python3 -c "import cantools,sys;db=cantools.database.load_file('$DBC');f=open('$W/gw.txt').read().split()[2].split('#')
print('#', db.decode_message(int(f[0],16), bytes.fromhex(f[1])))" > $W/gw_dec.txt; cat $W/gw_dec.txt
ok "GW_STATUS on backbone says ACCEL_STATE OK" 'grep -q "ACCEL_STATE.: .OK." $W/gw_dec.txt'

echo "# T2 stop the sender (mute 300 ms)"; mark
cansend s7sl0 7E0#011E
ok "IMU_ACCEL timeout detected" 'waitev "IMU_ACCEL .*-> MISSING" 2'
ok "IMU_ACCEL recovered after 3 good frames" 'waitev "IMU_ACCEL MISSING -> OK" 3'
since | grep -E "IMU_ACCEL .*MISSING" | sed 's/^/# /'
```

나머지 T3~T9(CRC, 카운터, 플러드, bus-off, 종료·회전, 분석, 재생)는 같은 패턴이다(레포 참고, 전체 93줄). 마지막 줄은 `RESULT: PASS (25 tests)` 또는 `RESULT: FAIL (k of 25 failed)`이고 종료 코드도 같이 맞춘다.

### 11.2 VM 호스트 쪽 CI 스크립트 `ci/run_ci.sh`

무엇을 확인하는 코드인가: 단계마다 실패하면 즉시 `RESULT: FAIL (stage: …)`로 끝내는 파이프라인. 빌드는 `-Werror`, 가장 싼 테스트(단위)부터 비싼 테스트(QEMU 두 개 부팅)로 간다 — P6 1.1절 "버그는 싼 층에서 잡는다".

```sh
#!/bin/bash
# run_ci.sh - build + test the whole kit in QEMU (VM host side). exit 0 = PASS.
# stages: build (-Werror) -> unit (E2E) -> ecu-smoke (firmware alone) -> boot 2 QEMUs -> guest tests
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WORK:-/var/tmp/S7/ci}
CTL="python3 $REPO/ci/qemu_ctl.py"
T0=$(date +%s)
stage() { echo "=== [$(( $(date +%s) - T0 ))s] $1"; }
fail()  { echo "RESULT: FAIL (stage: $1)"; $CTL stop $WORK > /dev/null 2>&1; exit 1; }

$CTL stop $WORK > /dev/null 2>&1; rm -rf $WORK; mkdir -p $WORK/share/bin
stage build
make -s -C $REPO/firmware OUT=$WORK/share/bin EXTRA=-Werror || fail build-firmware
make -s -C $REPO/daemon   OUT=$WORK/share/bin EXTRA=-Werror || fail build-daemon
make -s -C $REPO/logger   OUT=$WORK/share/bin EXTRA=-Werror || fail build-logger
gcc -std=gnu11 -O2 -Wall -Wextra -Werror $REPO/tests/test_e2e.c -o $WORK/test_e2e || fail build-unit
stage unit
$WORK/test_e2e || fail unit
stage ecu-smoke
timeout 60 python3 $REPO/tests/ecu_smoke.py $WORK/share/bin/sensor_ecu.elf $WORK || fail ecu-smoke
stage boot
rsync -a --exclude .git --exclude bin $REPO/ $WORK/share/
$CTL guest $WORK && $CTL ecu $WORK $WORK/share/bin/sensor_ecu.elf || fail qemu-start
$CTL wait $WORK 600 || fail boot
stage guest-tests
$CTL run $WORK tests/guest_test.sh 400; rc=$?
cp -r $WORK/share/artifacts $WORK/artifacts 2> /dev/null
$CTL stop $WORK > /dev/null; rm -f $WORK/guest.qcow2
stage done
[ $rc -eq 0 ] && echo "RESULT: PASS" || echo "RESULT: FAIL (stage: guest-tests rc=$rc)"
exit $rc
```

`qemu_ctl.py run`은 콘솔 소켓으로 `bash /mnt/s7/tests/guest_test.sh > …out; echo S7END… rc=$?`를 보내고 마커가 나올 때까지 기다려 rc를 파싱한다. 여기서도 버그가 하나 있었다: 마커 `rc=`까지만 보고 읽기를 멈춰서, 숫자가 다음 소켓 청크로 오면 `IndexError`가 났고, 그다음엔 숫자 뒤에 붙은 셸 프롬프트 이스케이프(`1\x1b[?2004h…`)를 `int()`가 못 먹었다. 지금은 정규식 `\d+`로 읽는다. **CI 하네스 자체도 테스트 대상**이다.

### 11.3 실행

```sh
ci/run_ci.sh; echo rc=$?
```

```text
=== [0s] build
   text	   data	    bss	    dec	    hex	filename
   1314	     48	     28	   1390	    56e	/var/tmp/S7/ci/share/bin/sensor_ecu.elf
=== [1s] unit
ok 1 - crc8 j1850 check value 0x4B
ok 2 - counter 2 -> 3 ok
ok 3 - same payload, other id -> bad crc (data id)
ok 4 - counter repeated
ok 5 - 15 -> 3 = 3 lost (wrap)
ok 6 - single bit flip detected
1..6
=== [1s] ecu-smoke
# 2.0 s window: 0x110=198, 0x111=197, 0x120=20
ok 1 - rates ~100 Hz / ~10 Hz
ok 2 - all CRCs valid (415 frames)
ok 3 - fault 2: exactly 5 bad-CRC frames (got 5)
ok 4 - fault 1: mute 200 ms visible
ok 5 - fault 4: bus-off report, no frames
ok 6 - re-open resumes traffic
1..6
=== [5s] boot
guest pid 2803443
ecu pid 2803449
guest shell ready after 41 s
=== [46s] guest-tests
# T1 baseline
ok 1 - all 3 sensor messages reached OK
ok 2 - no timeout during 4 s baseline
# {'GW_ALIVE': 49, 'ACCEL_STATE': 'OK', 'GYRO_STATE': 'OK', 'TEMP_STATE': 'OK', 'BUS_STATE': 'ACTIVE', 'INTEGRITY_ERRS': 0, 'TIMEOUT_TOTAL': 0, 'RESTARTS': 0}
ok 3 - GW_STATUS on backbone says ACCEL_STATE OK
# T2 stop the sender (mute 300 ms)
ok 4 - IMU_ACCEL timeout detected
ok 5 - IMU_ACCEL recovered after 3 good frames
# [    58.668] STATE IMU_ACCEL LATE -> MISSING (timeout, age 103 ms)
# [    58.894] STATE IMU_ACCEL MISSING -> OK (3 good frames)
...
ok 18 - log: 5 bad CRC on sensor bus
ok 19 - log: exactly 3 frames lost (counter jump only)
ok 20 - log: gateway forwarded 0 bad-CRC frames
ok 21 - log: bus-off error frame recorded
ok 22 - log: flood visible (>100 frames of 0x050)
# daemon rx=2202 logger count=2210
ok 23 - logger saw every frame the daemon saw (logger >= daemon, 0 socket drops)
# T9 replay regression: recorded fault window -> canplayer -> second daemon
ok 24 - replay reproduces 5 BAD_CRC
ok 25 - replay reproduces GAP lost=3
# replay: [    87.471] SUMMARY IMU_ACCEL rx=277 bad_crc=5 repeated=0 lost=3 late=1 timeouts=1 gap_us min=854 avg=10500 max=90408 state=MISSING
1..25
RESULT: PASS (25 tests)
=== [99s] done
RESULT: PASS
```

(가운데 T3~T8의 상세 줄은 10절에서 잘라 보여 줬다.) 세 번 돌린 기록:

| 실행 | 결과 | 걸린 시간 | 비고 |
|---|---|---|---|
| CI 1 | FAIL (23번) | 146 s | 로거 > 데몬 3818 vs 3776, `drained_at_exit=414` → 테스트 조건이 틀렸음 (8.3절) |
| CI 2 | PASS 25/25 | 99 s | VM load ≈ 5.7 |
| CI 3 | PASS 25/25 | 95 s | VM load ≈ 5.0 |

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">ci/run_ci.sh 단계와 소요 시간 (CI 실행 2, 총 99 s, VM load ≈ 5)</text><rect x="20.0" y="40.0" width="6.4" height="18.0" rx="2" fill="#888" fill-opacity="0.85"/><text x="32.4" y="53.0" font-size="11" text-anchor="start">build</text><rect x="26.4" y="66.0" width="4.0" height="18.0" rx="2" fill="#888" fill-opacity="0.85"/><text x="34.3" y="79.0" font-size="11" text-anchor="start">unit</text><rect x="28.3" y="92.0" width="23.7" height="18.0" rx="2" fill="#3f9a6b" fill-opacity="0.85"/><text x="58.0" y="105.0" font-size="11" text-anchor="start">ecu-smoke</text><rect x="52.0" y="118.0" width="262.4" height="18.0" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="183.2" y="131.0" font-size="11" text-anchor="middle">boot (2 QEMU)</text><rect x="314.4" y="144.0" width="339.2" height="18.0" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="484.0" y="157.0" font-size="11" text-anchor="middle">guest tests 25개</text><text x="20.0" y="186.0" font-size="11" text-anchor="middle">0s</text><text x="148.0" y="186.0" font-size="11" text-anchor="middle">20s</text><text x="276.0" y="186.0" font-size="11" text-anchor="middle">40s</text><text x="404.0" y="186.0" font-size="11" text-anchor="middle">60s</text><text x="532.0" y="186.0" font-size="11" text-anchor="middle">80s</text><text x="660.0" y="186.0" font-size="11" text-anchor="middle">100s</text><line x1="20" y1="174" x2="660" y2="174" stroke="currentColor" stroke-width="1.3"/><text x="10.0" y="210.0" font-size="12" text-anchor="start">부팅이 시간의 41 %. 실제 CI라면 부팅 1번에 테스트 여러 개를 묶고, 로그·JSON은 아티팩트로 올린다 (P6).</text></svg>
```

그림 7 — CI 2회차의 단계별 시간. 부팅이 41 %, 시스템 테스트가 54 %다. 빌드·단위·ECU 테스트는 합쳐 5초 — 이 셋만 따로 "pre-commit 훅"으로 돌리면 개발 루프가 빨라진다.

**체크포인트**: `ci/run_ci.sh`의 마지막 줄이 `RESULT: PASS`이고 `$?`가 0이어야 한다. 실패하면 `/var/tmp/S7/ci/artifacts/`(데몬 로그, 회전 로그, 분석 JSON, 재생 로그)를 본다.

**흔한 실패**:

- 게스트 부팅 timeout → AppArmor 서비스가 부하에서 수 분 걸린다. 커널 인자 `apparmor=0`과 `systemd.mask=apparmor.service`를 넣었다(게스트 한정, VM 호스트는 건드리지 않음).
- 앞 실행의 `candump`가 게스트 콘솔을 붙잡고 있어서 다음 명령이 줄 서서 기다림 → 게스트 스크립트의 모든 수신 명령에 `timeout`이나 `-n`을 단다. 이 노트 작업 중 실제로 한 번 콘솔이 막혀서 Ctrl-C(`\x03`)를 소켓으로 보내 풀었다.
- 테스트가 가끔만 실패 → 시간 임계값과 TCG 지터를 의심한다(7.7절). "가끔 실패하는 테스트"는 지우지 말고 **원인을 로그로 증명**한 뒤 고친다 — 이 노트의 두 버그(7.8절, 8.3절)가 다 그렇게 나왔다.

---

## 12. 커널 안에서는 무슨 일이 일어나나

프레임 하나가 ECU UART에서 데몬의 `recvmsg()`까지, 그리고 bus-off 하나가 데몬의 재시작까지 가는 경로다. 각 함수는 6.18.55 소스에서 확인했다.

**수신 경로**

1. ECU가 UART로 `t1108D303…\r`을 보낸다 → QEMU `pci-serial`(16550A) 인터럽트 → 8250 드라이버가 문자를 tty flip buffer에 넣는다(H8).
2. tty 계층이 line discipline `N_SLCAN`의 receive 콜백을 부른다 → `slcan_unesc()`가 `\r`까지 모아 `slcan_bump()` → `slcan_bump_frame()`이 `struct can_frame` skb를 만들어 `netif_rx()`.
3. NET_RX softirq → `can_receive()`(`net/can/af_can.c`) → `can_rcv_filter()`가 인터페이스별·전체(`can_ifindex = 0`) 수신 목록을 훑는다. 필터는 ID 해시·마스크 목록이다(I10 5.2절, `/proc/net/can/rcvlist_*`). **데몬 소켓의 필터에 0x050이 없으므로 플러드 프레임은 여기서 데몬 소켓으로 가지 않는다** — 10.4절에서 overflow 0의 이유.
4. 일치하면 `raw_rcv()`(`net/can/raw.c`)가 skb를 복제하고, 소프트웨어 RX 타임스탬프를 붙인 채 `sock_queue_rcv_skb_reason()`으로 소켓 큐에 넣는다. 큐가 `sk_rcvbuf`를 넘으면 실패 → `sk_drops` 증가 → 다음 `recvmsg()`의 `SO_RXQ_OVFL` cmsg 값이 된다(8.2절의 131).
5. `recvmsg()` → `sock_recv_cmsgs()`가 `SCM_TIMESTAMPING`과 `SO_RXQ_OVFL` cmsg를 채운다.

**에러·bus-off 경로**

1. ECU가 `sp000128\r`, `sb000256\r`을 보낸다 → `slcan_bump_state()` → `can_change_state()`가 에러 프레임(`CAN_ERR_CRTL`, data[1], 카운터 data[6..7])을 만들고 통계 `error-pass`를 올린다 → `sb`면 `can_bus_off()`가 `netif_carrier_off()`를 부르고 bus-off 통계를 올린다. 진짜 컨트롤러 드라이버도 같은 일을 한다 — 예를 들어 mcp251x는 threaded IRQ 핸들러 `mcp251x_can_ist()` 안에서 에러 플래그 레지스터로 상태를 계산해 에러 프레임을 `netif_rx()`로 올리고, bus-off면 `can_bus_off()`를 부른다(I10 6절).
2. 에러 프레임도 `netif_rx()` → 같은 af_can 경로 → 데몬 소켓의 `CAN_RAW_ERR_FILTER`에 걸려 전달된다.
3. 데몬이 `SIOCSIFFLAGS`로 IFF_UP을 내리면 → `dev_close()` → slcan `ndo_stop`이 MCU에 `C\r`을 보냄 → netdevice notifier `NETDEV_DOWN` → `raw_notify()`가 그 인터페이스에 바인드된 소켓마다 `sk->sk_err = ENETDOWN` → 데몬의 다음 `recvmsg()`가 `ENETDOWN`으로 한 번 실패(10.6절 NOTE 줄).
4. IFF_UP을 올리면 → slcan `slcan_netdev_open()` → `open_candev()` + MCU에 `C\rS6\r`, `O\r` → 상태 ERROR-ACTIVE, 큐 재시작.
5. restart-ms를 지원하는 드라이버라면 이 과정 대신 `can_bus_off()`가 `restart_ms` 뒤 `can_restart()`를 예약하고, 커널이 `CAN_ERR_RESTARTED` 에러 프레임을 올린다 — 데몬의 `on_error()`가 이 경우도 처리한다.

**can-gw 경로** (10.5절): `can_can_gw_rcv()`가 수신 목록 콜백으로 softirq 안에서 불려 skb를 복제·수정하고 `can_send(nskb, CGW_FLAGS_CAN_ECHO 여부)`로 바로 목적지에 넣는다. 유저 공간 왕복(웨이크업, 스케줄, 두 번의 시스템 콜)이 없으니 지연이 짧다.

드라이버를 쓸 때 다시 만나는 지점: 실보드의 `mcp251x`는 SPI 트랜잭션이 잠들 수 있으므로 인터럽트를 **threaded IRQ**(F6, `request_threaded_irq(…, NULL, mcp251x_can_ist, … IRQF_ONESHOT …)`)로 처리하고, 그 안에서 데이터·에러 프레임을 `netif_rx()`로 올린다. `mcp251xfd`는 `spi->rt = true`(SPI 메시지 펌프를 RT 스레드로)를 켠다(13절에서 소스 확인). 즉 이 노트의 데몬이 보는 이벤트 순서는 실보드에서도 같다.

---

## 13. 실보드로 옮기기 — MCP2515 / MCP2518FD (가이드, 실행 안 함 — HW 필요)

이 절의 DT overlay 컴파일·병합, 모듈 정보, binding·드라이버 소스 확인은 실제로 실행했다. 배선·`config.txt`·실제 버스 측정은 **HW가 필요해서 실행하지 않았다**.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">실보드 경로 (가이드 — HW 필요, 실행 안 함)</text><rect x="10" y="40" width="170" height="120" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="95.0" y="74.3" font-size="12" text-anchor="middle" font-weight="bold">Raspberry Pi 5</text><text x="95.0" y="89.3" font-size="12" text-anchor="middle">또는 BeaglePlay</text><text x="95.0" y="104.3" font-size="12" text-anchor="middle">SPI0: SCLK MOSI MISO</text><text x="95.0" y="119.3" font-size="12" text-anchor="middle">CE0 · GPIO25 = INT</text><text x="95.0" y="134.3" font-size="12" text-anchor="middle">3V3 · GND</text><rect x="250" y="40" width="170" height="120" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="335.0" y="74.3" font-size="12" text-anchor="middle" font-weight="bold">MCP2515 모듈</text><text x="335.0" y="89.3" font-size="12" text-anchor="middle">+ TJA1050 / 트랜시버</text><text x="335.0" y="104.3" font-size="12" text-anchor="middle">크리스털 8 / 16 MHz</text><text x="335.0" y="119.3" font-size="12" text-anchor="middle">INT 액티브 로우</text><text x="335.0" y="134.3" font-size="12" text-anchor="middle">J1 = 120 Ω 점퍼</text><line x1="180" y1="70" x2="248" y2="70" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/><line x1="248" y1="90" x2="180" y2="90" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/><text x="214.0" y="64.0" font-size="11" text-anchor="middle">SPI</text><line x1="248" y1="130" x2="180" y2="130" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/><text x="214.0" y="124.0" font-size="11" text-anchor="middle">INT</text><rect x="490" y="40" width="180" height="120" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="580.0" y="74.3" font-size="12" text-anchor="middle" font-weight="bold">MCU 센서 노드</text><text x="580.0" y="89.3" font-size="12" text-anchor="middle">(STM32 bxCAN / FDCAN)</text><text x="580.0" y="104.3" font-size="12" text-anchor="middle">이 레포의 ECU 펌웨어를</text><text x="580.0" y="119.3" font-size="12" text-anchor="middle">실제 CAN 드라이버로 이식</text><text x="580.0" y="134.3" font-size="12" text-anchor="middle">+ 트랜시버</text><line x1="20" y1="230" x2="660" y2="230" stroke="#d0564a" stroke-width="2"/><line x1="20" y1="250" x2="660" y2="250" stroke="#d0564a" stroke-width="2"/><text x="24.0" y="224.0" font-size="11" text-anchor="start">CAN_H</text><text x="24.0" y="266.0" font-size="11" text-anchor="start">CAN_L</text><rect x="30" y="282" width="70" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="65.0" y="301.3" font-size="12" text-anchor="middle">120 Ω</text><rect x="580" y="282" width="70" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="615.0" y="301.3" font-size="12" text-anchor="middle">120 Ω</text><line x1="335" y1="160" x2="335" y2="228" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/><line x1="580" y1="160" x2="580" y2="228" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/><rect x="300" y="280" width="200" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="400.0" y="298.8" font-size="12" text-anchor="middle">USB-CAN 어댑터 (gs_usb)</text><text x="400.0" y="313.8" font-size="12" text-anchor="middle">노트북에서 candump</text><line x1="400" y1="250" x2="400" y2="278" stroke="currentColor" stroke-width="1.3"/><text x="10.0" y="346.0" font-size="12" text-anchor="start">점검 순서: 전원 끄고 CAN_H-CAN_L 저항 60 Ω (양 끝 120 Ω 병렬) → 크리스털 확인 → DT overlay</text><text x="10.0" y="366.0" font-size="12" text-anchor="start">→ dmesg에서 mcp251x probe 확인 → ip link ... bitrate 500000 → candump → 스코프/LA 디코드</text><text x="10.0" y="386.0" font-size="12" text-anchor="start">3.3 V SoC + 5 V 모듈이면 MISO 레벨 확인 (모듈마다 다름 — 회로도로 확인)</text></svg>
```

그림 8 — 실보드 구성. QEMU 랩의 `s7sl0`(slcan)은 SPI CAN 컨트롤러 `can0`으로, Cortex-M3 QEMU는 실제 MCU 노드로 바뀐다. 데몬·로거·분석 스크립트·테스트는 인터페이스 이름만 바꿔서 그대로 쓴다.

### 13.1 binding이 요구하는 것 (6.18.55 트리에서 확인)

`Documentation/devicetree/bindings/net/can/microchip,mcp2510.yaml`과 `microchip,mcp251xfd.yaml`, 드라이버 `drivers/net/can/spi/mcp251x.c`, `mcp251xfd/mcp251xfd-core.c`를 읽고 정리했다:

| 항목 | MCP2515 (`mcp251x`) | MCP2518FD (`mcp251xfd`) |
|---|---|---|
| compatible | `microchip,mcp2510` / `mcp2515` / `mcp25625` | `microchip,mcp2517fd` / `mcp2518fd` / `mcp251xfd`, `mcp251863` + `mcp2518fd` |
| 필수 | `compatible`, `reg`, `clocks`, `interrupts` | `compatible`, `reg`, `interrupts`, `clocks` |
| 선택 | `vdd-supply`, `xceiver-supply`, `gpio-controller` + `#gpio-cells` | `vdd-supply`, `xceiver-supply`, `microchip,rx-int-gpios`, `spi-max-frequency` (최대 20 MHz, "clocks의 절반 이하") |
| 클럭 범위 (드라이버) | 1~25 MHz 아니면 `-ERANGE` | 1~40 MHz, 4 MHz 이하면 PLL ×10 켬 |
| SPI 속도 기본값 | DT에 없으면 10 MHz (mcp2510은 5 MHz) | `min(DT 값, clock/2 × 0.85)`, PLL 켠 뒤 더 빠르게 |
| bus-off 자동 재시작 | `do_set_mode` 있음 → `restart-ms` 지원 | 지원 |
| 공통 (`can-controller.yaml`) | 노드 이름 `can@…`, `termination-gpios`, `termination-ohms` | 같음 (mcp251xfd binding이 참조) |

`mcp251x`는 `clocks`가 없으면 레거시 `clock-frequency` 프로퍼티를 읽는 코드가 남아 있지만, binding상 `clocks`가 필수다 — 새 DT에는 `fixed-clock` 노드를 만들어 연결한다.

### 13.2 DT overlay — 컴파일과 병합 확인 (실행함)

`hw/s7-mcp2515-overlay.dts` (Raspberry Pi 커널 DT 라벨 `spi0`, `spidev0`, `gpio` 기준):

```dts
// s7-mcp2515-overlay.dts - MCP2515 on SPI0 CE0, INT on GPIO25 (Raspberry Pi style labels)
// properties per Documentation/devicetree/bindings/net/can/microchip,mcp2510.yaml (6.18.55)
/dts-v1/;
/plugin/;

&{/} {
	s7_can_osc: s7-can-osc {
		compatible = "fixed-clock";
		#clock-cells = <0>;
		clock-frequency = <16000000>;   /* READ THE CRYSTAL: cheap modules use 8 MHz */
	};
};

&spidev0 {
	status = "disabled";                    /* spidev@0 would claim CE0 */
};

&spi0 {
	status = "okay";
	#address-cells = <1>;
	#size-cells = <0>;

	can@0 {
		compatible = "microchip,mcp2515";
		reg = <0>;                          /* chip select CE0 */
		spi-max-frequency = <10000000>;     /* driver default if omitted: 10 MHz */
		clocks = <&s7_can_osc>;             /* required: driver reads clk rate */
		interrupt-parent = <&gpio>;
		interrupts = <25 8>;                /* GPIO25, IRQ_TYPE_LEVEL_LOW (active-low INT) */
	};
};
```

확인 방법: 그 세 라벨만 가진 가짜 base DT(`hw/fake-base.dts`)를 만들어 `fdtoverlay`로 실제로 얹어 본다. 라벨 이름 오타, `/plugin/` 누락, phandle 해석 실패가 여기서 다 잡힌다.

```sh
dtc -@ -q -I dts -O dtb -o s7-mcp2515-overlay.dtbo hw/s7-mcp2515-overlay.dts
dtc -@ -q -I dts -O dtb -o fake-base.dtb hw/fake-base.dts
fdtoverlay -i fake-base.dtb -o merged.dtb s7-mcp2515-overlay.dtbo
dtc -q -I dtb -O dts merged.dtb | sed -n "/s7-can-osc/,\$p"
```

```text
s7-mcp2515-overlay.dtbo: 1051 bytes
s7-mcp2518fd-overlay.dtbo: 1063 bytes
	s7-can-osc {
		phandle = <0x04>;
		clock-frequency = <0xf42400>;
		#clock-cells = <0x00>;
		compatible = "fixed-clock";
	};
...
	spi@7e204000 {
		reg = <0x7e204000 0x200>;
		status = "okay";
		...
		can@0 {
			interrupts = <0x19 0x08>;
			interrupt-parent = <0x01>;
			clocks = <0x04>;
			spi-max-frequency = <0x989680>;
			reg = <0x00>;
			compatible = "microchip,mcp2515";
		};

		spidev@0 {
			status = "disabled";
			compatible = "spidev-placeholder";
			...
```

(일부 줄 생략.) 출력에서 볼 것: `0xf42400` = 16 000 000, `0x989680` = 10 000 000, `interrupts = <0x19 0x08>` = GPIO25, `IRQ_TYPE_LEVEL_LOW`(8). `clocks = <0x04>`가 새 `s7-can-osc` 노드의 phandle을 가리킨다 — overlay의 `&s7_can_osc` 참조가 병합 때 해석됐다. 같은 CE0을 쓰는 `spidev@0`은 꺼졌다. 이걸 안 끄면 실보드에서 spidev가 CE0을 먼저 잡아 mcp251x가 probe되지 않는다.

모듈 쪽 확인(랩 커널 `Image-dom`에 `CONFIG_CAN_MCP251X=m`):

```sh
modinfo /var/lab/out/modules-dom/lib/modules/6.18.55-dom/kernel/drivers/net/can/spi/mcp251x.ko | grep -E "alias|vermagic"
```

```text
alias:          of:N*T*Cmicrochip,mcp25625C*
alias:          of:N*T*Cmicrochip,mcp25625
alias:          of:N*T*Cmicrochip,mcp2515C*
alias:          of:N*T*Cmicrochip,mcp2515
alias:          of:N*T*Cmicrochip,mcp2510C*
alias:          of:N*T*Cmicrochip,mcp2510
alias:          spi:mcp25625
alias:          spi:mcp2515
alias:          spi:mcp2510
vermagic:       6.18.55-dom SMP preempt mod_unload aarch64
```

DT의 `compatible = "microchip,mcp2515"`가 `of:N*T*Cmicrochip,mcp2515` alias에 걸려 udev가 모듈을 자동 로드한다(D3, L2).

### 13.3 실보드 절차 (가이드, 실행 안 함)

1. **크리스털부터 읽는다.** 싼 MCP2515 모듈은 8 MHz가 많고 16 MHz도 있다. DT 값이 틀리면 probe는 되지만 bitrate가 두 배·절반으로 틀려서 **버스 전체가 에러 프레임**으로 덮인다(Q4 2.4절 증상).
2. **전원 끄고 저항 측정.** CAN_H–CAN_L 사이 60 Ω(양 끝 120 Ω 병렬)이어야 한다. 120 Ω이면 한쪽 종단이 없고, 40 Ω이면 종단이 셋이다(Q4 10.1절). 모듈의 J1 점퍼가 120 Ω 종단이다.
3. 레벨 확인: SoC는 3.3 V. 5 V 트랜시버 모듈이면 MISO가 5 V로 올라올 수 있다 — 모듈 회로도로 확인한다(모듈마다 다르다).
4. Raspberry Pi라면 직접 만든 overlay 대신 RPi 커널에 들어 있는 overlay를 쓰는 방법도 있다: `config.txt`에 `dtoverlay=mcp2515-can0,oscillator=16000000,interrupt=25` (매개변수 이름은 RPi 펌웨어의 overlay README에서 확인). 직접 만든 overlay는 `dtc`로 컴파일해 `/boot/firmware/overlays/`에 넣고 `dtoverlay=s7-mcp2515`.
5. 부팅 후 확인:

```sh
dmesg | grep -i mcp251               # probe 성공 메시지 / -ERANGE면 클럭
ip -details link show can0           # clock 16000000 이 보이는지
ip link set can0 type can bitrate 500000 restart-ms 100
ip link set can0 up
candump -t d -e can0                 # 상대 노드(USB-CAN 어댑터)에서 cansend
```

6. 이 레포를 그대로: `canmond -i can0 -o vcan0`(또는 두 번째 컨트롤러), `canlogd`, `analyze.py`. 실버스에서는 `restart-ms 100`이 있으므로 데몬의 ifdown/ifup 재시작은 필요 없고 `CAN_ERR_RESTARTED`만 기다리면 된다 — 옵션으로 끄게 만드는 것이 14절 확장 과제다.
7. 측정: 스코프로 CAN_H/CAN_L 차동(recessive ≈ 0 V 차, dominant ≈ 2 V 차 — 트랜시버 데이터시트로 확인), 로직 분석기(Saleae 등)의 CAN 디코더로 ID·CRC·ACK 확인, MCP2515의 INT 핀을 LA 한 채널에 같이 걸어 **프레임 끝 → INT 하강 → 커널 RX 타임스탬프**를 한 시간축에 놓는다(M11). 그러면 SPI 컨트롤러 지연(인터럽트 → 스레드 → SPI 읽기)이 직접 보인다 — 이 노트의 TCG 숫자가 대신 못 하는 일이다.
8. 실보드 고장 주입: 커넥터 하나 뽑기(ACK 에러 → error-passive), CAN_H–CAN_L 단락(bus-off), 종단 하나 제거(반사 → 고속에서 에러), bitrate 틀린 노드 추가. 각각에서 데몬 로그와 `ip -s -d link` 카운터를 기록해 README에 붙인다.

---

## 14. 확장 아이디어 — 다음 커밋들

| 확장 | 무엇을 배우나 | 연결 노트 |
|---|---|---|
| `canmond -R` 옵션: restart-ms 있는 드라이버면 ifdown/ifup 대신 `CAN_ERR_RESTARTED` 대기 | 드라이버 능력에 따른 정책 분기 | I10 6절 |
| CAN FD: DBC에 64바이트 메시지, `CAN_RAW_FD_FRAMES`, 데몬·로거의 `canfd_frame` 처리 (`##` 로그 형식) | FD 프레임 크기 구분, BRS | Q4 4.3절, I10 5.3절 |
| 데몬을 systemd 서비스로: `Restart=on-failure`, `WatchdogSec=` + `sd_notify("WATCHDOG=1")`를 `tick()`에서 | 데몬이 멈추면 systemd가 재시작 | L1 |
| `SCHED_FIFO` + `mlockall()` + CPU 고정 후 9.3절 측정 다시 (Image-rt 커널로도) | 꼬리 지연이 얼마나 줄어드나 | N2, N3, S5 |
| CI에 "DBC 재생성 후 `git diff --exit-code gen/`" 단계 | 생성 코드와 DBC 불일치 방지 | P6 |
| GitHub Actions: 빌드·단위·ECU 테스트만 클라우드에서(QEMU-arm은 x86 러너에서도 됨), 시스템 테스트는 self-hosted | 무엇을 어디서 돌리나 | P6 4.4절 |
| ECU 펌웨어를 STM32(bxCAN/FDCAN) 실보드로 이식, `e2e.h` 그대로 | 같은 E2E 코드가 양쪽에서 돈다 | O3, N4 |
| 게이트웨이 상태를 ROS 2 토픽으로 (`diagnostic_msgs`) | 로보틱스 회사의 공통어 | Q6 |
| ISO-TP로 ECU 설정 읽기·쓰기(UDS 비슷하게) | 주기 프레임 외의 진단 채널 | Q4 5.3절 |
| 분석 결과를 HTML 리포트(그래프)로 | Q7의 데이터 품질 툴 | Q7 |

---

## 15. GitHub와 면접에서 보여 주기

### 15.1 README 목차 (레포에 이미 초안이 있다)

1. 한 문단 요약 + 아키텍처 그림(그림 1을 영어로)
2. Quick start: `ci/run_ci.sh` 한 줄, 예상 출력 마지막 5줄
3. **Results 표**: 0.1절의 숫자 표 그대로 + "QEMU TCG — methodology, not real-HW numbers" 문구
4. Design decisions: 7.1절 표(왜 필터, 왜 커널 타임스탬프, 왜 디바운스, 왜 backoff)
5. **Bugs found by the tests**: 7.8절(RX 큐와 경주), 8.2절(로거 overflow), 8.3절(종료 시 큐 버림), 9.2절(python-can 에러 프레임) — 이게 레포에서 가장 강한 부분이다
6. Fault injection matrix: 고장 5종 × (데몬 반응, 검출 시간, 복구 시간)
7. Real hardware: 13절 요약 + overlay 파일
8. Limitations: 중재 없는 에뮬레이션 버스, TCG 시간, slcan은 restart-ms 미지원

### 15.2 2분 데모 스크립트

1. (20초) "CAN 센서 노드, 게이트웨이, 로거를 만들고 고장 주입 25개로 CI를 돌립니다." 그림 1을 띄운다.
2. (30초) 터미널에서 `ci/run_ci.sh` — 실행해 둔 결과를 보여 주며 `RESULT: PASS`.
3. (30초) bus-off 타임라인(그림 5): "PASSIVE에서 데이터 복구까지 150 ms, 대부분 의도한 backoff입니다. 왜 즉시 재시작하지 않는지 설명하면…"
4. (20초) can-gw 비교(그림 6): "커널 게이트웨이가 7배 빠르지만 CRC가 틀린 프레임을 그대로 흘립니다. 그래서 검증 경로는 유저 공간에 둡니다."
5. (20초) 버그 하나(7.8절): "테스트가 가끔 실패해서 로그를 봤더니 커널 타임스탬프 간격은 35 ms인데 제 데몬은 102 ms라고 판정했습니다…"

### 15.3 면접에서 이렇게 말한다

**Q.** Walk me through what happens in your gateway when a sensor stops sending.

**A.** 메시지마다 마지막 수신 시각을 들고 있고, 5 ms 타이머에서 age를 본다. 3 × 주기면 LATE, 10 × 주기면 MISSING으로 바꾸고 백본으로 그 메시지 전달을 멈추고, 상태 프레임으로 MISSING을 알린다. 다시 오면 좋은 프레임 3개를 받은 뒤 OK. 랩에서 300 ms 정지를 주입했을 때 103 ms 만에 MISSING이 났다. 임계값은 측정한 지터 분포를 보고 정했다.

> Each monitored message has a last-seen timestamp, and a 5 ms timerfd tick compares its age against the cycle time from the DBC: three cycles means LATE, ten means MISSING. On MISSING the gateway stops forwarding that signal and publishes the state in a health frame, so downstream nodes know the data is stale rather than just absent. Recovery needs three consecutive valid frames. In my QEMU lab a 300 ms dropout was flagged after 103 ms. I picked the thresholds from the measured inter-arrival histogram, not from a rule of thumb.

**꼬리 질문**: "Why not just use the timestamp of the last frame in the socket?" → 7.8절: 판정 전에 큐를 비워야 한다, 그리고 SW 타임스탬프는 CLOCK_REALTIME.

**Q.** How do you detect corrupted or stale data that still has a valid CAN CRC?

**A.** CAN CRC는 선로 비트 오류만 막는다. 게이트웨이·ECU 소프트웨어 버그, 버퍼 재사용, 잘못된 라우팅은 못 막는다. 그래서 페이로드 안에 E2E CRC8과 4비트 alive counter를 넣었고, CRC 계산에 CAN ID를 섞어서 잘못된 ID로 간 프레임도 걸린다. 카운터가 같으면 stale(반복), 건너뛰면 손실 수를 센다.

> The CAN CRC only protects the wire. It doesn't catch a bug in a gateway that forwards a stale buffer or rewrites an ID. So, like AUTOSAR E2E, each frame carries its own CRC8 over the payload plus the CAN ID as a data ID, and a 4-bit alive counter. A repeated counter means stale data, a jump means lost frames, and a CRC mismatch means the payload was corrupted somewhere above the bus.

**Q.** Your CAN interface went bus-off. What should the software do?

**A.** 먼저 드라이버가 restart-ms를 지원하는지 본다. 지원하면 커널이 재시작하고 `CAN_ERR_RESTARTED`가 온다. 이 랩의 slcan처럼 지원 안 하면 앱이 링크를 내렸다 올려야 한다. 어느 쪽이든 즉시 재시작은 안 된다 — 단락 같은 물리 고장이면 재시작 → bus-off가 반복되며 버스 전체를 방해한다. 100 ms부터 2 s까지 지수 backoff를 했고, 랩에서 데이터 복구까지 150 ms였다. 그리고 `ENETDOWN`을 치명적 에러로 처리하지 않는다.

> First check whether the driver supports restart-ms. If it does, the kernel restarts the controller and I just wait for the CAN_ERR_RESTARTED error frame. slcan doesn't, so my daemon takes the link down and up itself. Either way I never restart immediately: if the cause is physical, like a shorted bus, immediate restarts just keep hammering the bus. I use exponential backoff from 100 ms to 2 s and reset it once data is healthy again. In the lab, recovery to valid data took about 150 ms, mostly the deliberate backoff.

**Q.** Why did you write your own routing instead of using can-gw?

**A.** 둘 다 재 봤다. can-gw는 softirq 안에서 복사해서 p50 122 us, 내 데몬은 809 us다. 하지만 can-gw는 CRC가 틀린 프레임 5개를 그대로 백본으로 넘겼고 내 데몬은 0개였다. 원시 데이터 고속 경로는 can-gw, 검증된 데이터 경로는 데몬 — 요구사항에 따라 섞는다.

> I measured both. can-gw forwards inside the softirq, so its median latency was 122 microseconds against 809 for my user-space path. But can-gw can't look inside the payload, and it forwarded all five corrupted frames, while my gateway forwarded none. So for raw, latency-sensitive traffic I'd use can-gw, and for anything that should be validated, the daemon.

**Q.** How do you know your logger isn't losing frames?

**A.** `SO_RXQ_OVFL`로 소켓 드롭 카운터를 받아서 기록한다. 실제로 첫 버전은 플러드 때 131개를 잃었다 — 필터 없는 로거라 범람 프레임을 다 받았고, 프레임마다 `ftell()`로 시스템 콜을 불렀다. 수신 버퍼를 키우고 `ftell`을 없앴다. 종료할 때 큐에 남은 걸 버리는 버그도 있어서, SIGTERM 뒤 큐를 비우고 끝나게 했다 — 한 번은 414개가 남아 있었다. 그리고 테스트로 "로거 ≥ 데몬, 드롭 0"을 매번 확인한다.

> I enable SO_RXQ_OVFL, so every received frame carries the socket's drop counter, and the logger reports it. That's how I found that the first version dropped 131 frames during a bus flood: it had no filters and called ftell, which is an lseek, on every frame. I raised the receive buffer and dropped the ftell. I also found it discarded its backlog on SIGTERM, 414 frames once, so now it drains the queue before exiting. The CI checks logger count ≥ daemon count and zero drops on every run.

**Q.** How would you test this without hardware, and what can't you test that way?

**A.** 두 QEMU를 소켓으로 묶고 MCU가 slcan으로 에러·상태를 보고하게 해서 진짜 can_dev 경로를 탔다. 고장 5종을 주입하고 25개 체크를 CI로 돌린다. 못 하는 것: 중재(에뮬레이션 버스엔 없음), 실제 bitrate 타이밍, 컨트롤러 IRQ·SPI 지연, 선로 반사. 그건 HIL 리그에서 LA로 핀과 커널 타임스탬프를 같이 찍어야 한다.

> Two QEMU instances: a Cortex-M3 ECU and the Linux gateway, joined by a socket, with the ECU speaking slcan so error states go through the real can_dev path. Five fault types, 25 checks, one CI script. What it can't show is arbitration, real bit timing, controller IRQ and SPI latency, or signal-integrity problems. For those you need a HIL rig with a logic analyzer correlating pins with kernel timestamps.

**Q.** (Don 스토리) How does your SSD firmware background help here?

**A.** 이 데몬의 구조는 SSD 펌웨어의 커맨드 타임아웃 처리와 같다: 완료 큐를 먼저 폴링하고 나서 타임아웃을 선언해야 하고(7.8절 버그가 정확히 그것이었다), 에러가 나면 즉시 재시도하지 않고 backoff하며, 모든 카운터를 남겨 사후 분석한다. 다른 것은 Linux에서 그 루프를 epoll·timerfd·소켓 옵션으로 짠다는 점이다.

> The pattern is the same as command timeout handling in SSD firmware: poll the completion queue before declaring a timeout, which is exactly the bug I hit, back off instead of retrying in a tight loop, and keep counters for every failure so you can explain an incident afterwards. What was new for me was expressing that loop with epoll, timerfd and socket options instead of a hardware queue.

---

## 16. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 타임아웃을 "2 × 주기"로 외워서 설정 | 고장 없는데 LATE·MISSING이 가끔 뜸 | 지터 꼬리(TCG에서 30~96 ms)를 무시 | 히스토그램을 재고 p99.9·max보다 크게 (7.7절) |
| 타이머 이벤트를 RX보다 먼저 처리 | 데몬 age 102 ms인데 커널 간격 35 ms | 큐에 이미 온 프레임을 안 보고 판정 | 판정 전에 RX 큐 비우기, STALL 기록 (7.8절) |
| 필터 없는 로거, 기본 rcvbuf | 플러드 때 로그가 빔, 분석의 lost가 부풀음 | 소켓 큐 overflow | `SO_RCVBUFFORCE`, `SO_RXQ_OVFL` 기록 (8.2절) |
| SIGTERM 받자마자 종료 | 로그 끝부분 프레임이 없음 | 큐에 밀린 backlog를 버림 | `MSG_DONTWAIT`로 큐 비우고 종료 (8.3절) |
| `ENETDOWN`을 치명적 에러로 처리 | bus-off 재시작 후 데몬 종료 | ifdown 때 `raw_notify()`가 `sk_err` 설정 | 기록만 하고 계속 읽기 (12절) |
| vcan에 `cangw` 규칙을 `-e` 없이 | 규칙 카운터는 오르는데 목적지에 프레임 없음 | loopback 0 + vcan echo 꺼짐 → `consume_skb` | `cangw -A … -e` (10.5절) |
| python-can으로 에러 프레임 읽기 | bus-off가 ID 0x040 데이터 프레임으로 보임 | 4.3.1은 BUSERROR 비트 있는 것만 에러로 인식 | 에러 줄은 직접 파싱 (9.2절) |
| CRC 오염 뒤 카운터 차이를 손실로 셈 | 같은 사건이 BAD_CRC + GAP로 두 번 | 거부된 프레임도 카운터를 소비 | `rejected`를 빼고 손실 계산 (7.4절) |
| 링커 스크립트에 `.data` 배치 없음 | QEMU에서는 돌고 실보드에선 초기값이 쓰레기 | 초기값 있는 전역이 플래시에 남음 | `> RAM AT > FLASH` + 리셋 때 복사 (4.2절) |
| MCP2515 크리스털 값을 틀리게 | probe 성공, 버스 전체 에러 프레임 | bitrate가 2배·절반 | 모듈 크리스털 확인 후 DT 수정 (13.3절) |

---

## 17. 직접 해보기

1. **개념** — 데몬이 데드라인 판정에 커널 RX 타임스탬프가 아니라 CLOCK_MONOTONIC을 쓰는 이유 두 가지를 쓰라.
   정답/힌트: 소프트웨어 RX 타임스탬프는 CLOCK_REALTIME이라 NTP·수동 시각 변경에 점프한다 / "지금"과 비교하려면 같은 시계여야 하는데 프레임이 안 올 때는 비교할 타임스탬프 자체가 없다.
2. **코드** — `canmond`에 `-R` 옵션을 추가해 restart-ms 드라이버 모드(재시작은 커널, 데몬은 `CAN_ERR_RESTARTED`만 대기)를 구현하고, slcan에서는 이 모드가 왜 영원히 BUS_OFF에 머무는지 실행으로 보여라.
   정답/힌트: `on_error()`에서 BUSOFF 때 `restart_at_ns`를 잡지 않으면 된다. slcan은 `do_set_mode`가 없어 커널 재시작이 안 일어난다(10.6절 대조 실험).
3. **측정** — 데몬을 `chrt -f 50`으로 띄우고 게스트에 `stress-ng --cpu 2`를 걸어 9.3절 측정을 다시 하라. 전달 지연 p99와 STALL 수가 어떻게 바뀌나?
   정답/힌트: 데몬 쪽 꼬리는 줄어야 한다. 센서 버스 간격(`s7sl0`)의 꼬리는 slcan 수신이 kworker·softirq에서 돌므로 그대로일 수 있다 — 무엇이 RT 우선순위로 올라갔는지 구분하라(N2).
4. **고장 주입** — ECU 펌웨어에 "같은 카운터를 두 번 보내기"(REPEATED) 고장 코드 6을 추가하고, `guest_test.sh`에 테스트를 하나 더 써서 CI를 PASS로 만들어라.
   정답/힌트: `publish()`에서 카운터를 증가시키지 않는 분기 하나. 데몬은 이미 `E2E_REPEATED`를 처리한다.
5. **/proc로 관찰** — 데몬과 로거가 떠 있을 때 게스트에서 `/proc/net/can/` 아래 `rcvlist_sff`, `rcvlist_fil`, `rcvlist_all`, `rcvlist_err`를 보라. 데몬의 필터 3개, 두 프로세스의 에러 마스크, 로거의 "모든 ID"가 각각 어디에 보이나?
   정답/힌트: `net/can/af_can.c`의 `can_rcv_list_find()`를 읽으면 답이 나온다 — 마스크가 정확히 `CAN_SFF_MASK` + EFF + RTR 플래그인 단일 ID 필터는 `rcvlist_sff`(ID로 바로 인덱스하는 배열), 마스크 0인 로거는 `rcvlist_all`, 에러 마스크는 둘 다 `rcvlist_err`. 데몬 필터에서 RTR 플래그를 빼면 `rcvlist_fil`로 떨어져 수신마다 마스크 비교를 한다. I10 5.2절.
6. **로그 회귀** — CI 아티팩트의 `replay.log`를 Mac으로 가져와 `analyze.py`만 돌려(게스트에서) 같은 BAD_CRC 5, lost 3이 나오는지 확인하라. 그다음 일부러 한 줄을 지워 `lost`가 바뀌는 것을 보라.
   정답/힌트: 로그가 진실의 원천이 되면, 분석기 자체의 회귀 테스트도 로그로 짤 수 있다.
7. **실보드 준비** — `hw/s7-mcp2518fd-overlay.dts`를 `fake-base.dts`에 `fdtoverlay`로 얹어 병합 결과에서 `interrupts-extended`와 `spi-max-frequency`가 어떻게 들어갔는지 확인하라.
   정답/힌트: 13.2절과 같은 명령. `interrupts-extended`는 gpio phandle + 25 + 8이 된다.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| DBC | CAN database 파일 | 메시지 ID·신호 비트 위치·스케일·주기를 적는 사실상 표준 텍스트 형식 |
| E2E protection | End-to-End 보호 | 페이로드 안의 CRC + 카운터로 버스 위 계층의 오류까지 잡는 기법 (AUTOSAR) |
| alive counter | 생존 카운터 | 프레임마다 1씩 늘어나는 작은 카운터. 반복 = stale, 점프 = 손실 |
| Data ID | 데이터 식별자 | CRC 계산에 섞는 메시지 고유값. 여기서는 CAN ID |
| deadline monitoring | 데드라인 감시 | 주기 메시지가 정해진 시간 안에 오는지 보는 것 (타임아웃 감시) |
| debounce | 디바운스 | 상태를 바꾸기 전 연속 N번 확인. 여기서는 좋은 프레임 3개 |
| slcan | serial line CAN | 시리얼로 CAN 프레임을 ASCII로 주고받는 Lawicel 프로토콜과 그 Linux 드라이버 |
| can_dev | CAN 장치 공통 계층 | bitrate·에러 상태·bus-off·restart를 처리하는 드라이버 공통 코드 |
| bus-off | 버스 이탈 | TEC ≥ 256인 노드가 버스에서 스스로 빠진 상태 |
| restart-ms | 자동 재시작 간격 | bus-off 뒤 커널이 컨트롤러를 다시 켜기까지의 시간 (드라이버가 지원할 때만) |
| backoff | 물러서기 | 재시도 간격을 실패마다 늘리는 정책 |
| can-gw | CAN gateway | 커널 안에서 CAN 인터페이스 사이 프레임을 복사·수정하는 모듈 (`cangw`) |
| SO_TIMESTAMPING | 타임스탬프 소켓 옵션 | 커널이 수신 시각을 cmsg로 붙여 주게 하는 옵션 |
| SO_RXQ_OVFL | 수신 큐 overflow 카운터 | 소켓이 버린 프레임 누적 수를 cmsg로 받는 옵션 |
| candump log | candump `-l` 형식 | `(초.마이크로초) 인터페이스 ID#DATA` 한 줄 형식. canplayer·python-can이 읽음 |
| TAP | Test Anything Protocol | `ok N - 이름` / `not ok N` 줄로 결과를 내는 테스트 출력 형식 |
| HIL | Hardware-in-the-loop | 실제 보드를 자동 장비로 조작하며 하는 시험 |
| TCG | Tiny Code Generator | KVM 없이 QEMU가 명령어를 번역해 실행하는 방식. 시간이 실제와 다르다 |

---

## 19. 요약 & 셀프 체크리스트

Q4·I10에서 배운 SocketCAN 지식을 한 시스템으로 묶었다: DBC로 메시지를 설계하고, 같은 E2E 헤더를 MCU와 Linux가 공유하고, MCU가 slcan으로 붙어 진짜 can_dev 에러 경로를 쓰게 했다. 데몬은 epoll 하나로 필터·타임스탬프·E2E·데드라인·bus-off를 처리하고 검증된 프레임만 넘기며, 로거와 분석기는 로그 하나로 지터·드롭·부하·지연을 숫자로 만든다. 고장 5종을 자동으로 넣는 25개 테스트가 QEMU 두 개에서 100초 안에 돈다. 그리고 그 테스트가 실제 버그 네 개(RX 큐와 경주, 로거 overflow, 종료 시 큐 버림, 이중 카운트)와 도구 함정 두 개(python-can 에러 프레임, vcan + can-gw의 `-e`)를 찾아냈다 — 면접에서 가장 말할 거리가 많은 부분이다. 실보드로는 overlay 하나와 인터페이스 이름만 바꾸면 된다.

- [ ] DBC에 주기 속성과 E2E 필드를 넣어 메시지를 설계하고 `cantools dump`로 검증할 수 있다
- [ ] CRC8 SAE-J1850을 표준 check 값으로 검증하고, 카운터로 반복·손실을 구분할 수 있다
- [ ] slcan의 `e`/`s` 메시지로 Linux에 에러 상태와 bus-off를 만드는 원리를 설명할 수 있다
- [ ] `CAN_RAW_FILTER`, `CAN_RAW_ERR_FILTER`, `SO_TIMESTAMPING`, `SO_RXQ_OVFL`을 걸고 cmsg를 파싱하는 코드를 짤 수 있다
- [ ] 데드라인 감시의 임계값을 측정한 지터 분포로 정하는 이유를 숫자로 말할 수 있다
- [ ] 타이머 판정 전에 RX 큐를 비워야 하는 이유를 7.8절 로그로 설명할 수 있다
- [ ] bus-off에서 restart-ms 지원 여부에 따른 두 가지 복구 방법과 backoff 이유를 설명할 수 있다
- [ ] `ENETDOWN`이 왜 오는지 `raw_notify()`까지 따라가 설명할 수 있다
- [ ] can-gw와 유저 공간 라우팅의 지연·검증 트레이드오프를 측정값으로 말할 수 있다
- [ ] candump 호환 로거의 회전과 드롭 감지, 종료 시 큐 비우기를 구현할 수 있다
- [ ] `ci/run_ci.sh`를 혼자 다시 돌려 `RESULT: PASS`를 만들고, 실패 시 아티팩트로 원인을 찾을 수 있다
- [ ] MCP2515/MCP2518FD overlay의 필수 프로퍼티와 크리스털·종단 점검 순서를 말할 수 있다

## 참고 자료

- Linux 커널 문서, SocketCAN: https://docs.kernel.org/networking/can.html
- 커널 소스 (6.18.55, 이 노트에서 읽은 파일): `net/can/af_can.c`, `net/can/raw.c`, `net/can/gw.c`, `drivers/net/can/slcan/slcan-core.c`, `drivers/net/can/vcan.c`, `drivers/net/can/dev/dev.c`, `drivers/net/can/spi/mcp251x.c`, `drivers/net/can/spi/mcp251xfd/mcp251xfd-core.c` — https://elixir.bootlin.com/linux/latest/source
- DT binding: `Documentation/devicetree/bindings/net/can/microchip,mcp2510.yaml`, `microchip,mcp251xfd.yaml`, `can-controller.yaml`
- can-utils: https://github.com/linux-can/can-utils
- cantools 문서: https://cantools.readthedocs.io/
- python-can 문서: https://python-can.readthedocs.io/
- man7.org: `socket(7)`(SO_RCVBUF, SO_RCVBUFFORCE, SO_RXQ_OVFL), `epoll(7)`, `timerfd_create(2)`, `signalfd(2)`, `netdevice(7)`(SIOCGIFFLAGS/SIOCSIFFLAGS)
- 커널 문서, timestamping: https://docs.kernel.org/networking/timestamping.html
- 관련 노트: Q4(CAN 실무), I10(CAN 서브시스템), A4, A9, H8, O3, P6, Q7, N2, D6, D7, M11
