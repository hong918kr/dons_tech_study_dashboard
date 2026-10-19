# Q4. CAN 실무 — SocketCAN 도구·프로그래밍·DBC·ISO-TP·J1939·에러 진단을 손으로 익히기

> **이 노트를 다 읽으면**: candump/cansend/cangen/canbusload/canplayer로 버스를 기록·재생·분석할 수 있다 · 필터·CAN FD·`SO_TIMESTAMPING`·에러 프레임을 쓰는 C 수신기를 짤 수 있다 · DBC 파일을 만들어 cantools로 로그를 신호값으로 풀고 python-can으로 주기 명령을 보낼 수 있다 · vxcan+netns+can-gw, ISO-TP, J1939, slcan(외부 MCU) 구성을 직접 세우고 bus-off 복구 경로를 커널 소스로 설명할 수 있다
> **왜 필요한가**: 로봇·차량·드론 회사에서 "모터가 가끔 멈춘다, CAN 로그 떠서 봐 달라", "DBC 받았으니 텔레메트리 디코더 만들어라", "bus-off 나면 어떻게 복구하나"는 입사 첫 달에 받는 일이고, 면접에서도 그대로 나온다
> **Don 기준 난이도**: 비트 타이밍·에러 카운터·스코프 디코드는 이미 아는 HW 지식 / 새로 배울 것은 can-utils 사용법, 소켓 필터의 함정, DBC 신호 레이아웃, ISO-TP·J1939 같은 상위 프로토콜이 Linux에서 어디 사는지, 랩에서 에러 상태를 재현하는 법
> **선행 노트**: I10(CAN 서브시스템 — 프로토콜·비트 타이밍 알고리즘·can_dev 드라이버, **이 노트의 이론 짝**), A9(소켓), A10(셸 도구), H8(UART·tty), N4(Linux ↔ MCU 역할 분담). 같이 보면 좋은 노트: O3(Linux ↔ 외부 MCU 통신 설계), Q3(타임스탬프·동기화), Q7(센서 데이터 품질 툴)

---

## 0. 큰 그림 — 이게 왜 필요한가

I10은 "CAN 컨트롤러가 어떻게 `can0`이 되는가"를 드라이버 쪽에서 팠다. 이 노트는 반대편이다. **보드가 이미 떠 있고 `can0`이 있을 때, 현장 엔지니어가 하루 종일 하는 일**을 순서대로 손에 익힌다.

예를 들어 센서 키트라면: Jetson이나 i.MX8 위의 Linux가 CAN으로 바퀴 모터 컨트롤러 4개, BMS, IMU 보드와 이야기한다. 일어나는 일은 대개 이렇다.

1. 버스를 켜고(`ip link ... bitrate`), 뭐가 흐르는지 본다(`candump`).
2. 이상한 현상을 **로그로 잡고**(`candump -l`), 사무실에서 **재생**한다(`canplayer`).
3. 원시 바이트를 **DBC로 신호값**(rpm, A, °C)으로 푼다(cantools).
4. 앱을 짠다: 필터, 주기 송신, 타임스탬프, 에러 감시(C 또는 python-can).
5. 진단·업데이트는 ISO-TP(UDS), 트럭·농기계는 J1939를 쓴다.
6. 가끔 버스가 죽는다(bus-off). 원인은 대부분 물리 계층이다 — 종단 저항, bitrate 불일치, 커넥터.

```svg
<svg viewBox="0 0 680 490" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">유저 공간 — 도구와 앱</text><rect x="10" y="28" width="124" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="72.0" y="46.7" font-size="12" text-anchor="middle">can-utils</text><text x="72.0" y="61.7" font-size="12" text-anchor="middle">candump·cansend</text><rect x="144" y="28" width="124" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="206.0" y="46.7" font-size="12" text-anchor="middle">python-can</text><text x="206.0" y="61.7" font-size="12" text-anchor="middle">cantools (DBC)</text><rect x="278" y="28" width="124" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="340.0" y="46.7" font-size="12" text-anchor="middle">isotpsend/recv</text><text x="340.0" y="61.7" font-size="12" text-anchor="middle">isotpdump</text><rect x="412" y="28" width="124" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="474.0" y="46.7" font-size="12" text-anchor="middle">j1939acd</text><text x="474.0" y="61.7" font-size="12" text-anchor="middle">j1939cat</text><rect x="546" y="28" width="124" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="608.0" y="46.7" font-size="12" text-anchor="middle">내 C 앱</text><text x="608.0" y="61.7" font-size="12" text-anchor="middle">(RAW + 필터)</text><line x1="0" y1="88" x2="680" y2="88" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4"/><text x="672.0" y="102.0" font-size="12" text-anchor="end">socket(PF_CAN, ...) — 유저/커널 경계</text><rect x="10" y="112" width="156" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="88.0" y="130.7" font-size="12" text-anchor="middle">CAN_RAW</text><text x="88.0" y="145.7" font-size="12" text-anchor="middle">프레임 그대로</text><rect x="178" y="112" width="156" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="256.0" y="130.7" font-size="12" text-anchor="middle">CAN_BCM</text><text x="256.0" y="145.7" font-size="12" text-anchor="middle">주기 송신·변화 감지</text><rect x="346" y="112" width="156" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="424.0" y="130.7" font-size="12" text-anchor="middle">CAN_ISOTP</text><text x="424.0" y="145.7" font-size="12" text-anchor="middle">4095 B+ 분할·FC</text><rect x="514" y="112" width="156" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="592.0" y="130.7" font-size="12" text-anchor="middle">CAN_J1939</text><text x="592.0" y="145.7" font-size="12" text-anchor="middle">주소 클레임·TP</text><line x1="72" y1="72" x2="88" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="206" y1="72" x2="256" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="340" y1="72" x2="424" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="474" y1="72" x2="592" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="608" y1="72" x2="88" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><rect x="10" y="180" width="470" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="245.0" y="201.7" font-size="12" text-anchor="middle">af_can 코어: rcvlist(필터 해시) · 로컬 루프백(echo)</text><text x="245.0" y="216.7" font-size="12" text-anchor="middle">/proc/net/can/{stats,rcvlist_*}</text><rect x="492" y="180" width="178" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="581.0" y="201.7" font-size="12" text-anchor="middle">can-gw (커널 라우팅)</text><text x="581.0" y="216.7" font-size="12" text-anchor="middle">cangw -A -s A -d B</text><line x1="88" y1="156" x2="88" y2="178" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="256" y1="156" x2="208" y2="178" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="424" y1="156" x2="328" y2="178" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="592" y1="156" x2="448" y2="178" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><rect x="10" y="262" width="124" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="72.0" y="280.7" font-size="12" text-anchor="middle">vcan</text><text x="72.0" y="295.7" font-size="12" text-anchor="middle">SW 루프백</text><rect x="144" y="262" width="124" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="206.0" y="280.7" font-size="12" text-anchor="middle">vxcan</text><text x="206.0" y="295.7" font-size="12" text-anchor="middle">netns 터널 쌍</text><rect x="278" y="262" width="124" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="340.0" y="280.7" font-size="12" text-anchor="middle">slcan</text><text x="340.0" y="295.7" font-size="12" text-anchor="middle">tty line disc.</text><rect x="412" y="262" width="124" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="474.0" y="280.7" font-size="12" text-anchor="middle">sja1000 등</text><text x="474.0" y="295.7" font-size="12" text-anchor="middle">PCI/플랫폼 HW</text><rect x="546" y="262" width="124" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="608.0" y="280.7" font-size="12" text-anchor="middle">m_can · flexcan</text><text x="608.0" y="295.7" font-size="12" text-anchor="middle">mcp251xfd(SPI)</text><line x1="245" y1="230" x2="72" y2="260" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="245" y1="230" x2="206" y2="260" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="245" y1="230" x2="340" y2="260" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="245" y1="230" x2="474" y2="260" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="245" y1="230" x2="608" y2="260" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><rect x="278" y="322" width="392" height="48" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="474.0" y="342.7" font-size="12" text-anchor="middle">can_dev (drivers/net/can/dev)</text><text x="474.0" y="357.7" font-size="12" text-anchor="middle">bittiming · TEC/REC 상태 · restart-ms · echo</text><line x1="340" y1="306" x2="340" y2="324" stroke="currentColor" stroke-width="1.3"/><line x1="474" y1="306" x2="474" y2="324" stroke="currentColor" stroke-width="1.3"/><line x1="608" y1="306" x2="608" y2="324" stroke="currentColor" stroke-width="1.3"/><rect x="10" y="400" width="124" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="72.0" y="418.7" font-size="12" text-anchor="middle">버스 없음</text><text x="72.0" y="433.7" font-size="12" text-anchor="middle">(echo만)</text><line x1="72" y1="306" x2="72" y2="398" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4" marker-end="url(#ah1)"/><rect x="144" y="400" width="124" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="206.0" y="418.7" font-size="12" text-anchor="middle">다른 netns</text><text x="206.0" y="433.7" font-size="12" text-anchor="middle">의 vxcan 짝</text><line x1="206" y1="306" x2="206" y2="398" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4" marker-end="url(#ah1)"/><rect x="278" y="400" width="124" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340.0" y="418.7" font-size="12" text-anchor="middle">UART → MCU</text><text x="340.0" y="433.7" font-size="12" text-anchor="middle">(Lawicel ASCII)</text><line x1="340" y1="370" x2="340" y2="398" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><rect x="412" y="400" width="124" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="474.0" y="418.7" font-size="12" text-anchor="middle">트랜시버</text><text x="474.0" y="433.7" font-size="12" text-anchor="middle">CAN_H / CAN_L</text><line x1="474" y1="370" x2="474" y2="398" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><rect x="546" y="400" width="124" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="608.0" y="418.7" font-size="12" text-anchor="middle">트랜시버</text><text x="608.0" y="433.7" font-size="12" text-anchor="middle">+ 120 Ω × 2</text><line x1="608" y1="370" x2="608" y2="398" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><text x="10.0" y="464.0" font-size="12" text-anchor="start">가운데 줄 = net_device (ip link로 설정, 데이터는 skb). vcan·vxcan은 can_dev가 없어서</text><text x="10.0" y="482.0" font-size="12" text-anchor="start">bitrate·TEC/REC·bus-off가 없다. 점선 = 물리 계층 없음 → vcan 랩은 프로토콜 에러를 못 만든다</text></svg>
```

그림 1 — SocketCAN 스택을 "실무 도구" 기준으로 다시 그렸다. 위에서부터 도구 → 소켓 프로토콜 → af_can 코어(필터·루프백)와 can-gw → `net_device` → 물리. vcan·vxcan은 can_dev 층이 없다. 그래서 bitrate, TEC/REC, bus-off가 원리적으로 존재하지 않는다 — 이 노트가 slcan과 외부 MCU를 랩에 넣은 이유다.

bare-metal 비유로 정리하면: CAN 컨트롤러 = 메일박스가 달린 UART 같은 주변장치, SocketCAN = 그 주변장치를 여러 태스크가 동시에 쓰게 해 주는 RTOS 메시지 큐 + 필터 테이블, can-utils = JTAG 대신 쓰는 버스 모니터다.

### 0.1 이 노트의 실습 환경

- VM 호스트: Lima `elinux`, `uname -r` = `7.0.0-38-generic` (aarch64). can-utils 2023.03, `arm-none-eabi-gcc`, QEMU 10.2.1.
- QEMU 게스트: 공용 랩 `Image-dom`(`6.18.55-dom`) + `rootfs-dom`(Ubuntu 24.04 + systemd, can-utils 2023.03, python3-can 4.3.1, cantools 45.0.0). rootfs는 qcow2 오버레이로 띄웠다. 게스트 커널 설정: `CAN_RAW`·`CAN_BCM`·`CAN_GW`·`CAN_DEV`는 내장(`=y`), vcan·vxcan·slcan·can-isotp·can-j1939는 모듈.
- 진짜 컨트롤러 흉내: QEMU의 `kvaser_pci`(SJA1000) 2장을 같은 `-object can-bus`에 꽂았다. 랩 커널에 SJA1000 드라이버가 꺼져 있어(`# CONFIG_CAN_SJA1000 is not set`) I10과 같은 방법으로 `sja1000.ko`·`kvaser_pci.ko`를 소스 그대로 out-of-tree 빌드했다(`make -C /var/lab/out/linux-build-dom M=$PWD`).
- 외부 MCU: 두 번째 QEMU(`qemu-system-arm -M mps2-an385`, Cortex-M3)에서 직접 쓴 bare-metal 펌웨어 `q4_mcu.elf`가 돈다. Linux 게스트의 `pci-serial`(16550A → `/dev/ttyS0`)과 unix 소켓으로 연결되고, **slcan(Lawicel ASCII) 프로토콜**로 말한다. "USB-CAN 어댑터 + 모터 노드" 역할이다.
- 게스트는 시리얼 콘솔 소켓으로 조종했고, 스크립트와 바이너리는 9p 공유 폴더(`/mnt/q4`)로 넘겼다. C 예제는 VM 호스트에서 `gcc -static -O2 -Wall -Wextra`로 빌드했고 경고 0개다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="10" y="10" width="440" height="360" rx="6" fill="#4a7bd0" fill-opacity="0.04" stroke="#4a7bd0" stroke-dasharray="5 4"/><text x="20.0" y="30.0" font-size="13" text-anchor="start" font-weight="bold">QEMU 게스트 1: Image-dom + rootfs-dom (aarch64)</text><rect x="24" y="44" width="196" height="92" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="122.0" y="71.7" font-size="12" text-anchor="middle">root netns</text><text x="122.0" y="86.7" font-size="12" text-anchor="middle">q4vc0 / q4vcf (vcan)</text><text x="122.0" y="101.7" font-size="12" text-anchor="middle">q4vx0 (vxcan 한쪽)</text><text x="122.0" y="116.7" font-size="12" text-anchor="middle">cangw 규칙 2개</text><rect x="240" y="44" width="196" height="92" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="338.0" y="71.7" font-size="12" text-anchor="middle">netns q4ecu</text><text x="338.0" y="86.7" font-size="12" text-anchor="middle">q4vx1 (vxcan 짝)</text><text x="338.0" y="101.7" font-size="12" text-anchor="middle">isotprecv (ECU 역할)</text><text x="338.0" y="116.7" font-size="12" text-anchor="middle">cangw 규칙 = 0개</text><line x1="220" y1="100" x2="238" y2="100" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><line x1="240" y1="110" x2="222" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="24" y="160" width="196" height="60" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="122.0" y="186.7" font-size="12" text-anchor="middle">can0 · can1</text><text x="122.0" y="201.7" font-size="12" text-anchor="middle">sja1000 + kvaser_pci (.ko)</text><rect x="240" y="160" width="196" height="60" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="338.0" y="186.7" font-size="12" text-anchor="middle">q4sl0 (slcan)</text><text x="338.0" y="201.7" font-size="12" text-anchor="middle">slcand → /dev/ttyS0</text><rect x="24" y="250" width="196" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="122.0" y="271.7" font-size="12" text-anchor="middle">QEMU -object can-bus</text><text x="122.0" y="286.7" font-size="12" text-anchor="middle">(프레임 단위 전달)</text><line x1="122" y1="220" x2="122" y2="248" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><line x1="150" y1="248" x2="150" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="240" y="250" width="196" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="338.0" y="271.7" font-size="12" text-anchor="middle">pci-serial (16550A)</text><text x="338.0" y="286.7" font-size="12" text-anchor="middle">chardev 소켓 서버</text><line x1="338" y1="220" x2="338" y2="248" stroke="currentColor" stroke-width="1.3"/><text x="20.0" y="330.0" font-size="12" text-anchor="start">vcan·vxcan: SW만 / can0·can1: 비트 타이밍 계산은 진짜,</text><text x="20.0" y="348.0" font-size="12" text-anchor="start">하지만 버스 모델에 ACK·비트가 없어 에러 상태는 안 생김</text><rect x="470" y="200" width="200" height="170" rx="6" fill="#3f9a6b" fill-opacity="0.04" stroke="#3f9a6b" stroke-dasharray="5 4"/><text x="480.0" y="220.0" font-size="13" text-anchor="start" font-weight="bold">QEMU 2: mps2-an385</text><rect x="484" y="232" width="172" height="60" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="570.0" y="258.7" font-size="12" text-anchor="middle">q4_mcu.elf (Cortex-M3)</text><text x="570.0" y="273.7" font-size="12" text-anchor="middle">UART0 = slcan 링크</text><rect x="484" y="302" width="172" height="54" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="570.0" y="325.7" font-size="12" text-anchor="middle">0x200 → 0x201 응답</text><text x="570.0" y="340.7" font-size="12" text-anchor="middle">0x7EE → e/s 에러 주입</text><line x1="436" y1="275" x2="482" y2="262" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><line x1="482" y1="270" x2="436" y2="283" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><text x="560.0" y="120.0" font-size="12" text-anchor="middle">unix 소켓</text><text x="560.0" y="138.0" font-size="12" text-anchor="middle">q4_link.sock</text><text x="560.0" y="156.0" font-size="12" text-anchor="middle">(MCU가 재접속)</text><line x1="560" y1="164" x2="460" y2="266" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 4"/></svg>
```

그림 2 — 이 노트의 랩 구성. 한 게스트 안에 vcan(SW 버스), vxcan(네임스페이스 사이 터널), SJA1000 2장(비트 타이밍 계산이 진짜인 컨트롤러), slcan(시리얼 너머 MCU)을 모두 띄웠다.

주의: 모든 타이밍 숫자는 **KVM 없는 TCG 에뮬레이션**에서, 그것도 19명이 같은 VM을 쓰는 동안(load average 17~24) 잰 값이다. 실보드 지연이 아니라 "측정하는 방법"을 보여 주는 숫자다.

---

## 1. 프레임 — 실무에서 읽어야 하는 만큼만

프로토콜의 비트 단위 설명(중재, 에러 5종, TEC/REC 규칙)은 I10 1절에 있다. 여기서는 **candump 한 줄을 보고 머릿속에서 바로 풀어야 하는 것**만 모았다.

```svg
<svg viewBox="0 0 680 410" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="22.0" font-size="13" text-anchor="start" font-weight="bold">Classic CAN, 11-bit ID (CAN 2.0A)</text><rect x="10" y="30" width="34" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="27.0" y="46.7" font-size="12" text-anchor="middle">SOF</text><text x="27.0" y="61.7" font-size="12" text-anchor="middle">1</text><rect x="46" y="30" width="62" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="77.0" y="46.7" font-size="12" text-anchor="middle">ID</text><text x="77.0" y="61.7" font-size="12" text-anchor="middle">11</text><rect x="110" y="30" width="36" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="128.0" y="46.7" font-size="12" text-anchor="middle">RTR</text><text x="128.0" y="61.7" font-size="12" text-anchor="middle">1</text><rect x="148" y="30" width="34" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="165.0" y="46.7" font-size="12" text-anchor="middle">IDE</text><text x="165.0" y="61.7" font-size="12" text-anchor="middle">1</text><rect x="184" y="30" width="28" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="198.0" y="46.7" font-size="12" text-anchor="middle">r0</text><text x="198.0" y="61.7" font-size="12" text-anchor="middle">1</text><rect x="214" y="30" width="40" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="234.0" y="46.7" font-size="12" text-anchor="middle">DLC</text><text x="234.0" y="61.7" font-size="12" text-anchor="middle">4</text><rect x="256" y="30" width="120" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="316.0" y="46.7" font-size="12" text-anchor="middle">DATA</text><text x="316.0" y="61.7" font-size="12" text-anchor="middle">0~8 B</text><rect x="378" y="30" width="48" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="402.0" y="46.7" font-size="12" text-anchor="middle">CRC</text><text x="402.0" y="61.7" font-size="12" text-anchor="middle">15</text><rect x="428" y="30" width="30" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="443.0" y="46.7" font-size="12" text-anchor="middle">del</text><text x="443.0" y="61.7" font-size="12" text-anchor="middle">1</text><rect x="460" y="30" width="40" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="480.0" y="46.7" font-size="12" text-anchor="middle">ACK</text><text x="480.0" y="61.7" font-size="12" text-anchor="middle">1+1</text><rect x="502" y="30" width="40" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="522.0" y="46.7" font-size="12" text-anchor="middle">EOF</text><text x="522.0" y="61.7" font-size="12" text-anchor="middle">7</text><rect x="544" y="30" width="36" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="562.0" y="46.7" font-size="12" text-anchor="middle">IFS</text><text x="562.0" y="61.7" font-size="12" text-anchor="middle">3</text><text x="10.0" y="102.0" font-size="13" text-anchor="start" font-weight="bold">Extended, 29-bit ID (CAN 2.0B) — SocketCAN: CAN_EFF_FLAG</text><rect x="10" y="110" width="34" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="27.0" y="126.7" font-size="12" text-anchor="middle">SOF</text><text x="27.0" y="141.7" font-size="12" text-anchor="middle">1</text><rect x="46" y="110" width="50" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="71.0" y="126.7" font-size="12" text-anchor="middle">ID-A</text><text x="71.0" y="141.7" font-size="12" text-anchor="middle">11</text><rect x="98" y="110" width="36" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="116.0" y="126.7" font-size="12" text-anchor="middle">SRR</text><text x="116.0" y="141.7" font-size="12" text-anchor="middle">1</text><rect x="136" y="110" width="34" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="153.0" y="126.7" font-size="12" text-anchor="middle">IDE</text><text x="153.0" y="141.7" font-size="12" text-anchor="middle">=1</text><rect x="172" y="110" width="84" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="214.0" y="126.7" font-size="12" text-anchor="middle">ID-B</text><text x="214.0" y="141.7" font-size="12" text-anchor="middle">18</text><rect x="258" y="110" width="36" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="276.0" y="126.7" font-size="12" text-anchor="middle">RTR</text><text x="276.0" y="141.7" font-size="12" text-anchor="middle">1</text><rect x="296" y="110" width="44" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="318.0" y="126.7" font-size="12" text-anchor="middle">r1 r0</text><text x="318.0" y="141.7" font-size="12" text-anchor="middle">2</text><rect x="342" y="110" width="40" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="362.0" y="126.7" font-size="12" text-anchor="middle">DLC</text><text x="362.0" y="141.7" font-size="12" text-anchor="middle">4</text><rect x="384" y="110" width="90" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="429.0" y="126.7" font-size="12" text-anchor="middle">DATA</text><text x="429.0" y="141.7" font-size="12" text-anchor="middle">0~8 B</text><rect x="476" y="110" width="48" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="500.0" y="126.7" font-size="12" text-anchor="middle">CRC</text><text x="500.0" y="141.7" font-size="12" text-anchor="middle">15</text><rect x="526" y="110" width="40" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="546.0" y="126.7" font-size="12" text-anchor="middle">ACK</text><text x="546.0" y="141.7" font-size="12" text-anchor="middle">~</text><rect x="568" y="110" width="62" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="599.0" y="126.7" font-size="12" text-anchor="middle">EOF IFS</text><text x="599.0" y="141.7" font-size="12" text-anchor="middle">7+3</text><rect x="262" y="186" width="268" height="52" rx="4" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-dasharray="4 3"/><text x="10.0" y="182.0" font-size="13" text-anchor="start" font-weight="bold">CAN FD — BRS=1이면 점선 구간이 data bitrate로 빨라진다</text><rect x="10" y="190" width="34" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="27.0" y="206.7" font-size="12" text-anchor="middle">SOF</text><text x="27.0" y="221.7" font-size="12" text-anchor="middle">1</text><rect x="46" y="190" width="50" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="71.0" y="206.7" font-size="12" text-anchor="middle">ID</text><text x="71.0" y="221.7" font-size="12" text-anchor="middle">11</text><rect x="98" y="190" width="36" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="116.0" y="206.7" font-size="12" text-anchor="middle">RRS</text><text x="116.0" y="221.7" font-size="12" text-anchor="middle">0</text><rect x="136" y="190" width="34" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="153.0" y="206.7" font-size="12" text-anchor="middle">IDE</text><text x="153.0" y="221.7" font-size="12" text-anchor="middle">1</text><rect x="172" y="190" width="36" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="190.0" y="206.7" font-size="12" text-anchor="middle">FDF</text><text x="190.0" y="221.7" font-size="12" text-anchor="middle">=1</text><rect x="210" y="190" width="32" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="226.0" y="206.7" font-size="12" text-anchor="middle">res</text><text x="226.0" y="221.7" font-size="12" text-anchor="middle">1</text><rect x="244" y="190" width="36" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="262.0" y="206.7" font-size="12" text-anchor="middle">BRS</text><text x="262.0" y="221.7" font-size="12" text-anchor="middle">1</text><rect x="282" y="190" width="36" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="300.0" y="206.7" font-size="12" text-anchor="middle">ESI</text><text x="300.0" y="221.7" font-size="12" text-anchor="middle">1</text><rect x="320" y="190" width="40" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="340.0" y="206.7" font-size="12" text-anchor="middle">DLC</text><text x="340.0" y="221.7" font-size="12" text-anchor="middle">4</text><rect x="362" y="190" width="82" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="403.0" y="206.7" font-size="12" text-anchor="middle">DATA</text><text x="403.0" y="221.7" font-size="12" text-anchor="middle">0~64 B</text><rect x="446" y="190" width="30" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="461.0" y="206.7" font-size="12" text-anchor="middle">SC</text><text x="461.0" y="221.7" font-size="12" text-anchor="middle">4</text><rect x="478" y="190" width="50" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="503.0" y="206.7" font-size="12" text-anchor="middle">CRC</text><text x="503.0" y="221.7" font-size="12" text-anchor="middle">17/21</text><rect x="530" y="190" width="38" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="549.0" y="206.7" font-size="12" text-anchor="middle">ACK…</text><text x="549.0" y="221.7" font-size="12" text-anchor="middle"></text><rect x="570" y="190" width="36" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="588.0" y="206.7" font-size="12" text-anchor="middle">EOF</text><text x="588.0" y="221.7" font-size="12" text-anchor="middle">7</text><text x="10.0" y="262.0" font-size="13" text-anchor="start" font-weight="bold">struct canfd_frame (72 B, CANFD_MTU) — classic은 struct can_frame (16 B, CAN_MTU)</text><rect x="10" y="272" width="150" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="85.0" y="290.7" font-size="12" text-anchor="middle">can_id</text><text x="85.0" y="305.7" font-size="12" text-anchor="middle">u32: ID + 3 플래그</text><rect x="163" y="272" width="70" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="198.0" y="290.7" font-size="12" text-anchor="middle">len</text><text x="198.0" y="305.7" font-size="12" text-anchor="middle">0..64</text><rect x="236" y="272" width="110" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="291.0" y="290.7" font-size="12" text-anchor="middle">flags</text><text x="291.0" y="305.7" font-size="12" text-anchor="middle">BRS ESI FDF</text><rect x="349" y="272" width="70" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="384.0" y="298.2" font-size="12" text-anchor="middle">res0/1</text><rect x="422" y="272" width="228" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="536.0" y="290.7" font-size="12" text-anchor="middle">data[64]</text><text x="536.0" y="305.7" font-size="12" text-anchor="middle">__aligned(8)</text><text x="10.0" y="340.0" font-size="12" text-anchor="start">can_id 비트: 31 = CAN_EFF_FLAG (29-bit) · 30 = CAN_RTR_FLAG · 29 = CAN_ERR_FLAG (에러 프레임)</text><text x="10.0" y="358.0" font-size="12" text-anchor="start">classic DLC 0..15 중 9..15는 8바이트로 취급 → 원래 값은 can_frame.len8_dlc (cc-len8-dlc 모드)</text><text x="10.0" y="376.0" font-size="12" text-anchor="start">FD 길이는 0..8, 12, 16, 20, 24, 32, 48, 64 → len 9는 can_fd_len2dlc()가 12로 올림(패딩)</text><text x="10.0" y="394.0" font-size="12" text-anchor="start">비트 수: 11-bit 8 B classic = 111비트(+스터핑 최대 24) → 500 kbit/s에서 222~270 µs</text></svg>
```

그림 3 — Classic(11/29-bit)과 CAN FD 프레임, 그리고 SocketCAN 구조체의 대응. FD는 BRS 비트의 샘플 포인트부터 CRC delimiter까지(점선)만 data bitrate로 빨라진다. 중재 구간은 classic과 같은 속도라서 FD 노드와 classic 노드가 같은 중재 규칙을 공유한다.

| candump에서 보이는 것 | 의미 | SocketCAN에서 |
|---|---|---|
| `123` (3자리) | 11-bit 표준 ID | `can_id = 0x123` |
| `18FF50E5` (8자리) | 29-bit 확장 ID | `0x18FF50E5`에 `CAN_EFF_FLAG`(비트 31)를 OR → 코드에서 `0x98FF50E5` |
| `[4]` | classic DLC = 데이터 4바이트 | `can_frame.len` |
| `[16]` (두 자리) | CAN FD 길이 | `canfd_frame.len`, 소켓에 `CAN_RAW_FD_FRAMES` 필요 |
| `remote request` | RTR 프레임 (데이터 없음) | `CAN_RTR_FLAG` |
| `B` / `E` 플래그 | FD의 BRS / ESI | `canfd_frame.flags` |
| `ERRORFRAME` | 컨트롤러가 만든 가짜 프레임(버스에 없음) | `CAN_ERR_FLAG`, 클래스는 `can_id` 하위 비트 |

꼭 기억할 세 가지:

- **ID = 우선순위.** 낮은 ID가 중재에서 이긴다. 모터 토크 명령은 낮은 ID, 로그·진단은 높은 ID.
- **ACK = "누군가 받았다"**이지 "목적지가 받았다"가 아니다. 애플리케이션 수신 확인은 상위 프로토콜 몫이다(ISO-TP의 Flow Control, J1939 TP의 CTS/EndOfMsgAck — 5·6절에서 실제로 본다).
- **에러 프레임은 버스 위 프레임이 아니다.** Linux가 컨트롤러 상태(에러 카운터, 상태 변화)를 앱에 알려 주려고 만든 "메타 프레임"이다. 그래서 candump `-e`나 `CAN_RAW_ERR_FILTER`로 따로 구독해야 보인다.

에러 카운터 요약(자세한 규칙은 I10 1.3절): 송신 에러 TEC += 8, 수신 에러 REC += 1, 성공하면 1씩 감소. 96 이상 warning, 128 이상 **error-passive**(에러 플래그를 recessive로만 냄), TEC 256 이상 **bus-off**(버스에서 손을 뗌). 복구는 recessive 11비트 연속을 128번 본 뒤에만 가능하다.

---

## 2. 비트 타이밍 실무 — `ip link ... bitrate 500000 sample-point 0.875`

### 2.1 그림으로 한 번에

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">한 비트 = 16 tq (SJA1000, 8 MHz, 500 kbit/s, sample-point 0.875)</text><rect x="30" y="34" width="36" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="48.0" y="58.2" font-size="12" text-anchor="middle">Sync</text><rect x="68" y="34" width="226" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="181.0" y="58.2" font-size="12" text-anchor="middle">Prop_Seg = 6</text><rect x="296" y="34" width="264" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="428.0" y="58.2" font-size="12" text-anchor="middle">Phase_Seg1 = 7</text><rect x="562" y="34" width="74" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="599.0" y="58.2" font-size="12" text-anchor="middle">PS2 = 2</text><line x1="30" y1="80" x2="30" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="68" y1="80" x2="68" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="106" y1="80" x2="106" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="144" y1="80" x2="144" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="182" y1="80" x2="182" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="220" y1="80" x2="220" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="258" y1="80" x2="258" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="296" y1="80" x2="296" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="334" y1="80" x2="334" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="372" y1="80" x2="372" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="410" y1="80" x2="410" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="448" y1="80" x2="448" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="486" y1="80" x2="486" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="524" y1="80" x2="524" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="562" y1="80" x2="562" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="600" y1="80" x2="600" y2="88" stroke="currentColor" stroke-width="1.3"/><line x1="638" y1="80" x2="638" y2="88" stroke="currentColor" stroke-width="1.3"/><text x="49.0" y="102.0" font-size="12" text-anchor="start">1 tq = 125 ns</text><line x1="562" y1="26" x2="562" y2="120" stroke="#d0564a" stroke-width="2"/><text x="562.0" y="136.0" font-size="12" text-anchor="middle">샘플 포인트 = (1+6+7)/16 = 87.5 %</text><path d="M30 150 L638 150" stroke="currentColor" stroke-width="1.2" marker-start="url(#ah3)" marker-end="url(#ah3)"/><text x="334.0" y="168.0" font-size="12" text-anchor="middle">비트 시간 2 µs = 16 tq × 125 ns  →  500 kbit/s</text><text x="10.0" y="200.0" font-size="12" text-anchor="start">SJW = 1 tq: 엣지가 늦게/일찍 오면 Phase_Seg1을 늘리거나 PS2를 줄여 재동기화(최대 SJW만큼)</text><text x="10.0" y="220.0" font-size="12" text-anchor="start">Prop_Seg: 트랜시버 루프 + 케이블 왕복 지연을 흡수 · Phase_Seg: 클럭 오차 흡수</text><text x="10.0" y="250.0" font-size="13" text-anchor="start" font-weight="bold">ip -details 한 줄과 대응:</text><rect x="10" y="260" width="660" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340.0" y="279.2" font-size="12" text-anchor="middle">tq 125 prop-seg 6 phase-seg1 7 phase-seg2 2 sjw 1 brp 1   (clock 8000000)</text><text x="10.0" y="314.0" font-size="12" text-anchor="start">brp = 8 MHz × 125 ns = 1 · SJA1000 레지스터: BTR0=0x00, BTR1=0x1c (TSEG1=13, TSEG2=2)</text></svg>
```

그림 4 — SJA1000(8 MHz 클럭)에서 500 kbit/s, 샘플 포인트 87.5%가 되는 방법. 한 비트를 tq(time quantum) 16개로 쪼개고 Sync(1) + Prop(6) + Phase1(7) 뒤에서 샘플한다. 커널 계산 알고리즘(`can_calc_bittiming()`)의 단계별 설명은 I10 4절.

용어를 한 줄씩:

- **tq**: CAN 클럭을 BRP(prescaler)로 나눈 최소 시간 단위. 8 MHz, BRP 1이면 125 ns.
- **Sync_Seg**: 항상 1 tq. 엣지가 여기 오길 기대한다.
- **Prop_Seg**: 트랜시버 루프 지연 + 케이블 왕복 지연을 흡수한다. 버스가 길수록 커야 한다.
- **Phase_Seg1 / Phase_Seg2**: 노드 사이 클럭 오차를 흡수하는 완충. 재동기화 때 Phase_Seg1을 늘리거나 Phase_Seg2를 줄인다.
- **SJW**: 한 번의 재동기화로 조정할 수 있는 최대 tq.
- **샘플 포인트** = (1 + Prop + Phase1) / 전체 tq. CiA 권장값은 bitrate에 따라 87.5%(≤500 k), 80%(800 k), 75%(1 M). **버스의 모든 노드가 비슷한 샘플 포인트를 써야 한다** — MCU 펌웨어 팀과 맞출 숫자다.

### 2.2 예제 1 — 컨트롤러별 계산을 유저 공간에서 미리 보기 (`can-calc-bit-timing`)

무엇을 확인하나: 같은 500 kbit/s라도 CAN 클럭과 컨트롤러 레지스터 한계에 따라 tq·세그먼트가 어떻게 달라지는지. can-utils에 들어 있는 이 도구는 커널과 같은 알고리즘(`v5.19`)을 유저 공간에서 돌린다 — 보드가 없어도 DT의 클럭 값만 알면 결과를 미리 볼 수 있다.

```sh
can-calc-bit-timing -c 8000000 -b 500000 -s 875 sja1000
can-calc-bit-timing -c 40000000 -b 500000 -s 875 -d 2000000 mcp251xfd
can-calc-bit-timing -c 80000000 -b 500000 -s 800 -d 2000000 mcan-v3.1+
can-calc-bit-timing -q -c 24000000 -b 500000 -s 875 flexcan
```

```text
Bit timing parameters for sja1000 with 8.000000 MHz ref clock (cmd-line) using algo 'v5.19'
 nominal                                  real  Bitrt    nom   real  SampP
 Bitrate TQ[ns] PrS PhS1 PhS2 SJW BRP  Bitrate  Error  SampP  SampP  Error   BTR0 BTR1
  500000    125   6    7    2   1   1   500000   0.0%  87.5%  87.5%   0.0%   0x00 0x1c

Bit timing parameters for mcp251xfd with 40.000000 MHz ref clock (cmd-line) using algo 'v5.19'
 nominal                                  real  Bitrt    nom   real  SampP
 Bitrate TQ[ns] PrS PhS1 PhS2 SJW BRP  Bitrate  Error  SampP  SampP  Error       NBTCFG
  500000     25  34   35   10   1   1   500000   0.0%  87.5%  87.5%   0.0%   0x00440900

Data Bit timing parameters for mcp251xfd with 40.000000 MHz ref clock (cmd-line) using algo 'v5.19'
 nominal                                  real  Bitrt    nom   real  SampP
 Bitrate TQ[ns] PrS PhS1 PhS2 SJW BRP  Bitrate  Error  SampP  SampP  Error       NBTCFG
 2000000     25   8    8    3   1   1  2000000   0.0%  87.5%  85.0%   2.9%   0x000f0200

Bit timing parameters for mcan-v3.1+ with 80.000000 MHz ref clock (cmd-line) using algo 'v5.19'
 nominal                                  real  Bitrt    nom   real  SampP
 Bitrate TQ[ns] PrS PhS1 PhS2 SJW BRP  Bitrate  Error  SampP  SampP  Error         NBTP
  500000     12  63   64   32   1   1   500000   0.0%  80.0%  80.0%   0.0%   0x00007e1f

Data Bit timing parameters for mcan-v3.1+ with 80.000000 MHz ref clock (cmd-line) using algo 'v5.19'
 nominal                                  real  Bitrt    nom   real  SampP
 Bitrate TQ[ns] PrS PhS1 PhS2 SJW BRP  Bitrate  Error  SampP  SampP  Error         NBTP
 2000000     12  15   16    8   1   1  2000000   0.0%  80.0%  80.0%   0.0%   0x00001e07

  500000    125   6    7    2   1   3   500000   0.0%  87.5%  87.5%   0.0%   0x02310005
```

출력에서 볼 것:

- MCP2518FD(40 MHz)의 **data phase 2 Mbit/s**는 비트당 20 tq뿐이라 87.5%를 정확히 못 만들고 85.0%(오차 2.9%)가 됐다. FD data phase는 원래 75~80%를 많이 쓴다. 요청값과 실제값이 다를 수 있으니 **설정 후 반드시 `ip -details`로 실제값을 읽는다.**
- 마지막 열은 그 컨트롤러의 **레지스터 값**(SJA1000 `BTR0/BTR1`, MCP251xFD `NBTCFG`, M_CAN `NBTP`, FlexCAN `CTRL1`)이다. bring-up 때 레지스터 덤프(devmem, regmap debugfs — H2 참조)와 직접 대조할 수 있다.
- M_CAN 80 MHz, `-s 800`(80%): tq가 12.5 ns라서 출력에는 반올림한 `12`가 찍힌다.

### 2.3 예제 2 — 진짜 컨트롤러에 설정하고 `ip -details`로 확인

무엇을 확인하나: QEMU가 흉내 내는 SJA1000에 bitrate·샘플 포인트·restart-ms·berr-reporting을 주고, 커널이 고른 값과 드라이버가 쓴 레지스터를 본다. 그리고 실무에서 자주 맞는 세 가지 에러를 일부러 낸다.

```sh
insmod /mnt/q4/sja1000.ko; insmod /mnt/q4/kvaser_pci.ko
ip link set can0 type can bitrate 500000 sample-point 0.875 restart-ms 100 berr-reporting on
ip link set can1 type can bitrate 500000 sample-point 0.875 restart-ms 100
ip link set can0 up; ip link set can1 up
ip -details -statistics link show can0
dmesg | grep BTR | tail -2
ip link set can0 type can bitrate 250000; echo "change while up rc=$?"
ip link set can0 type can termination 120; echo "termination rc=$?"
ip link set can0 down; ip link set can0 type can bitrate 1000000 sample-point 0.875
ip -details link show can0 | grep -E "bitrate|tq"
```

```text
7: can0: <NOARP,UP,LOWER_UP,ECHO> mtu 16 qdisc pfifo_fast state UP mode DEFAULT group default qlen 10
    link/can  promiscuity 0  allmulti 0 minmtu 16 maxmtu 16 
    can <BERR-REPORTING> state ERROR-ACTIVE (berr-counter tx 0 rx 0) restart-ms 100 
	  bitrate 500000 sample-point 0.875
	  tq 125 prop-seg 6 phase-seg1 7 phase-seg2 2 sjw 1 brp 1
	  sja1000: tseg1 1..16 tseg2 1..8 sjw 1..4 brp 1..64 brp_inc 1
	  clock 8000000 
	  re-started bus-errors arbit-lost error-warn error-pass bus-off
	  0          0          0          0          0          0         numtxqueues 1 ...
    RX:  bytes packets errors dropped  missed   mcast           
             0       0      0       0       0       0 
    TX:  bytes packets errors dropped carrier collsns           
             0       0      0       0       0       0 
[  625.316873] kvaser_pci 0000:00:03.0 can0: setting BTR0=0x00 BTR1=0x1c
[  625.540354] kvaser_pci 0000:00:04.0 can1: setting BTR0=0x00 BTR1=0x1c
RTNETLINK answers: Device or resource busy
change while up rc=2
Error: Termination is not configurable on this device.
termination rc=2
	  bitrate 1000000 sample-point 0.875
	  tq 125 prop-seg 3 phase-seg1 3 phase-seg2 1 sjw 1 brp 1
```

(`numtxqueues` 이후 긴 줄은 줄였다.) 출력에서 볼 것:

- `ip -details` 한 화면이 **bring-up 체크리스트 전부**다: 상태(`ERROR-ACTIVE`), 에러 카운터(`berr-counter tx 0 rx 0`), 복구 정책(`restart-ms 100`), 실제 비트 타이밍, 컨트롤러 한계(`tseg1 1..16` …), 입력 클럭(`clock 8000000`), 에러 통계 6칸.
- dmesg의 `BTR1=0x1c` = (TSEG2-1 = 1)을 4비트 왼쪽으로 민 값과 (TSEG1-1 = 12)의 OR — 그림 4와 같다. 커널 계산 → 드라이버 `do_set_bittiming` → 레지스터.
- **올라가 있는 인터페이스의 bitrate는 못 바꾼다**(`EBUSY`). 항상 `down` → 설정 → `up`.
- `termination 120`은 DT에 `termination-gpios`가 있는 보드에서만 된다(I10 8.5절). 이 카드는 안 된다.
- 1 Mbit/s에 87.5%를 요청하면 8 MHz 클럭으로는 **비트당 8 tq**밖에 안 된다: 1+3+3+1 = 8, 샘플 포인트 7/8. 8 tq는 재동기화 여유(SJW 1, PS2 1)가 거의 없는 설정이다 — 1 Mbit/s 버스에서 CAN 클럭이 낮은 컨트롤러가 말썽인 이유다. 가능하면 CAN 클럭을 높이는 쪽(예: 40·80 MHz)으로 DT의 `assigned-clock-rates`를 고른다.

### 2.4 bitrate 불일치 — 실제 버스에서 보이는 증상 (가이드, 실행 안 함)

QEMU의 can-bus 모델은 프레임 단위로 복사할 뿐 비트를 흉내 내지 않는다(I10 6.3절에서 확인). 그래서 bitrate가 다른 두 노드도 랩에서는 그냥 통한다. 실제 버스에서는 이렇게 보인다.

| 상황 | Linux 쪽 증상 | 스코프에서 |
|---|---|---|
| 내 노드만 bitrate가 다름 (예: 500 k vs 250 k) | 수신 0, `bus-errors` 급증, 송신하면 TEC가 8씩 올라 error-passive → 곧 bus-off, `restart-ms`면 bus-off 반복 | 비트 폭이 2 µs가 아닌 4 µs, 에러 플래그(dominant 6비트)가 계속 찍힘 |
| 같은 bitrate, 샘플 포인트만 크게 다름 (예: 87.5% vs 60%) | 짧은 버스에서는 멀쩡, 긴 케이블·노드 많을 때만 간헐 CRC/stuff error | 엣지 근처에서 샘플 → 링잉 구간에 걸림 |
| 클럭 소스 오차 큼 (RC 오실레이터 MCU) | 긴 프레임·데이터 0x00/0xFF(스터핑 많음)에서만 에러 | 프레임 뒷부분으로 갈수록 비트 경계가 밀림 |
| CAN FD data phase만 불일치 | classic 프레임은 정상, BRS 프레임만 에러 | BRS 이후 구간만 깨짐 |

디버그 순서: `ip -details -statistics`의 bus-errors가 오르는가 → `candump -e`로 에러 클래스(stuff/form/bit/ACK) → 스코프로 비트 폭 측정(500 k = 2.000 µs) → 다른 노드의 설정표와 비교. 10절에서 다시 정리한다.

---

## 3. can-utils — 현장 도구 한 바퀴

### 3.1 예제 3 — vcan 만들기, 그리고 6.18의 함정 하나

무엇을 확인하나: vcan(가상 버스)을 만들고 classic/FD/확장 ID/RTR 프레임을 보내 candump 표기를 읽는다.

```sh
modprobe vcan; ip link add q4vc0 type vcan; ip link add q4vcf type vcan
ip link set q4vcf mtu 72; ip link set q4vc0 up; ip link set q4vcf up
ip -details link show q4vc0
candump -t a -x q4vc0 q4vcf > /tmp/d1.txt & P=$!; sleep 0.5
cansend q4vc0 123#DEADBEEF
cansend q4vc0 18FF50E5#0102
cansend q4vc0 7DF#R
cansend q4vc0 123#R3
cansend q4vcf 456##1.00112233445566778899AABBCCDDEEFF
cansend q4vcf 457##0.0011
cansend q4vc0 456##1.0011 ; echo "rc=$?"
sleep 0.3; kill $P; cat /tmp/d1.txt
```

```text
3: q4vc0: <NOARP,UP,LOWER_UP> mtu 2060 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000
    link/can  promiscuity 0  allmulti 0 minmtu 0 maxmtu 0 
    vcan addrgenmode eui64 numtxqueues 1 numrxqueues 1 ...
CAN interface is not CAN FD capable - sorry.
rc=1
 (1792341952.003820)  q4vc0  TX - -  123   [4]  DE AD BE EF
 (1792341952.576568)  q4vc0  TX - -  18FF50E5   [2]  01 02
 (1792341952.824442)  q4vc0  TX - -       7DF   [0]  remote request
 (1792341953.085093)  q4vc0  TX - -       123   [3]  remote request
 (1792341953.275251)  q4vcf  TX B -       456  [16]  00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
 (1792341953.698131)  q4vcf  TX - -       457  [02]  00 11
```

출력에서 볼 것:

- `cansend` 문법: `ID#데이터`(classic), `ID#R`/`ID#R3`(RTR, DLC 3), `ID##<flags>.<데이터>`(FD — flags 1 = BRS). ID가 8자리면 자동으로 29-bit.
- `-x`는 방향(`TX`/`RX`)과 FD 플래그(`B` = BRS, `E` = ESI)를 보여 준다. `[16]`처럼 길이가 두 자리면 FD 프레임이다.
- **함정**: 6.18 커널의 vcan은 기본 MTU가 **2060(`CANXL_MTU`)**이다(`drivers/net/can/vcan.c`의 `dev->mtu = CANXL_MTU`). 그런데 can-utils 2023.03의 cansend는 "MTU가 정확히 72인가"로 FD 지원을 판단해서 q4vc0에 FD를 거부했다. 커널은 문제없이 받는다 — 4.2절에서 내 C 프로그램은 같은 q4vc0에 FD 프레임을 보냈다. **툴과 커널 버전이 어긋나면 툴이 거짓말을 할 수 있다.** FD 실습용 vcan은 `mtu 72`로 명시하자.

### 3.2 예제 4 — 명령줄 필터 문법

무엇을 확인하나: candump의 `<if>,<id>:<mask>`(일치), `<id>~<mask>`(불일치), 29-bit 마스크.

```sh
candump -n 3 -t z "q4vc0,100:700" > /tmp/d2.txt 2>&1 & P=$!
candump -n 2 "q4vc0,123~7FF" > /tmp/d5.txt 2>&1 & Q=$!; sleep 0.5
cansend q4vc0 050#01; cansend q4vc0 123#02; cansend q4vc0 1FF#03; cansend q4vc0 200#04; cansend q4vc0 180#05
sleep 0.5; kill $P $Q 2>/dev/null; echo "-- 100:700"; cat /tmp/d2.txt; echo "-- 123~7FF"; cat /tmp/d5.txt
```

```text
-- 100:700
 (000.000000)  q4vc0  123   [1]  02
 (000.078496)  q4vc0  1FF   [1]  03
 (000.383730)  q4vc0  180   [1]  05
-- 123~7FF
  q4vc0  050   [1]  01
  q4vc0  1FF   [1]  03
```

29-bit 마스크는 따로 돌렸다: 필터를 건 candump를 띄우고 18FF50E5, 18FEF100, 18FF0001을 차례로 보냈다(실제 실행 때는 위의 11-bit 프레임 5개도 함께 보냈다 — 29-bit 필터에는 걸리지 않는다).

```sh
candump -n 2 "q4vc0,18FF0000:1FFF0000" > /tmp/d3.txt 2>&1 & Q=$!; sleep 0.5
cansend q4vc0 18FF50E5#06; cansend q4vc0 18FEF100#07; cansend q4vc0 18FF0001#08
sleep 0.5; echo "-- EFF PGN FFxx:"; cat /tmp/d3.txt
```

```text
-- EFF PGN FFxx:
  q4vc0  18FF50E5   [1]  06
  q4vc0  18FF0001   [1]  08
```

출력에서 볼 것:

- `100:700` = "(ID & 0x700) == 0x100" → 0x100~0x1FF 범위만 통과. 마스크는 "비교할 비트"다.
- `123~7FF` = 0x123만 **빼고** 전부. `-n 2`라 처음 두 개만 찍혔다.
- `18FF0000:1FFF0000` = J1939의 PDU2 그룹 0xFFxx만. 0x18FEF100(PGN 0xFEF1)은 걸러졌다. J1939 ID는 우선순위(3비트) · PGN(18비트) · 소스 주소(8비트)라서 이런 마스크를 자주 쓴다(6절).
- 함정: 어떤 필터를 주든 **에러 프레임은 별도**다. `#<err_mask>`를 붙여야 받는다(8절).

### 3.3 예제 5 — 기록(`-l`), ASC 변환, 재생(`canplayer`)

무엇을 확인하나: 현장에서 잡은 로그를 다른 인터페이스로 재생하는 기본기. 버그 재현의 80%가 이것이다.

```sh
cd /tmp; candump -l q4vc0 & P=$!; sleep 0.3
cangen q4vc0 -g 5 -I 0x3A0 -L 4 -D i -n 6; sleep 0.2; kill -INT $P; sleep 0.2
L=$(ls -t candump-*.log | head -1); cat $L
log2asc -I $L q4vc0 | head -12
ip link add q4vc1 type vcan; ip link set q4vc1 up
candump -t d -n 6 q4vc1 > /tmp/d4.txt & P=$!; sleep 0.3
canplayer -I $L q4vc1=q4vc0; sleep 0.3; cat /tmp/d4.txt
```

```text
Disabled standard output while logging.
Enabling Logfile 'candump-2026-10-18_164600.log'
(1792341960.883949) q4vc0 3A0#00000000
(1792341960.891841) q4vc0 3A0#01000000
(1792341960.898576) q4vc0 3A0#02000000
(1792341960.907343) q4vc0 3A0#03000000
(1792341960.913286) q4vc0 3A0#04000000
(1792341960.919853) q4vc0 3A0#05000000
date Sun Oct 18 16:46:00 2026
base hex  timestamps absolute
no internal events logged
   0.000000 1  3A0             Rx   d 4 00 00 00 00
   0.007892 1  3A0             Rx   d 4 01 00 00 00
   0.014627 1  3A0             Rx   d 4 02 00 00 00
   0.023394 1  3A0             Rx   d 4 03 00 00 00
   0.029337 1  3A0             Rx   d 4 04 00 00 00
   0.035904 1  3A0             Rx   d 4 05 00 00 00
 (000.000000)  q4vc1  3A0   [4]  00 00 00 00
 (000.021446)  q4vc1  3A0   [4]  01 00 00 00
 (000.000246)  q4vc1  3A0   [4]  02 00 00 00
 (000.000182)  q4vc1  3A0   [4]  03 00 00 00
 (000.003919)  q4vc1  3A0   [4]  04 00 00 00
 (000.006699)  q4vc1  3A0   [4]  05 00 00 00
```

출력에서 볼 것:

- `cangen -g 5 -I 0x3A0 -L 4 -D i`: 5 ms 간격, 고정 ID, 4바이트, 데이터는 증가(`i`)하는 카운터. `-D r`이면 랜덤, 16진 문자열이면 고정.
- candump 로그 형식 `(epoch) if ID#DATA`는 **사실상 표준 교환 형식**이다. cantools·python-can·Wireshark가 읽는다. `log2asc`는 Vector CANalyzer의 ASC로 바꾼다 — 차량 쪽 동료와 로그를 주고받을 때 쓴다.
- `canplayer q4vc1=q4vc0`은 "로그의 q4vc0을 q4vc1로 내보내라"는 매핑이다. 재생 간격(0.021, 0.0002, …)이 원래 간격(약 7 ms)과 다르다 — canplayer는 로그 타임스탬프를 따라 `nanosleep`하지만 TCG 게스트에서는 스케줄링이 뭉쳤다. 정밀한 재생 타이밍이 필요하면 실보드 + RT 우선순위(N2)로 돌린다.

### 3.4 그 밖의 도구 한 줄 요약

| 도구 | 쓰임 | 자주 쓰는 옵션 |
|---|---|---|
| `candump` | 수신·기록 | `-t a/d/z/A` 타임스탬프, `-x` 방향, `-e` 에러, `-l` 파일, `-L` 로그 형식으로 stdout, `-n N` |
| `cansend` | 한 프레임 송신 | `ID#..`, `ID##F..`(FD), `ID#R` |
| `cangen` | 부하·패턴 생성 | `-g ms`, `-I`, `-L`, `-D i/r/HEX`, `-e`(29-bit), `-f`(FD), `-i`(ENOBUFS 무시) |
| `canplayer` | 로그 재생 | `-I file`, `dst=src`, `-l i`(무한 반복), `-t`(타이밍 무시) |
| `cansniffer` | ID별로 바뀌는 바이트를 한 화면에 (top 같은 화면) | `-c` 색, `-b` 이진 — 터미널 전용이라 이 노트에서는 실행 출력을 싣지 않았다 |
| `canbusload` | 버스 부하 % | `if@bitrate`, `-e` 정확 스터핑, `-i` 스터핑 무시, `-b` 막대 (9절) |
| `cangw` | 커널 게이트웨이 규칙 | `-A/-D/-L`, `-f` 필터, `-m` 수정, `-e` echo (5절) |
| `isotpsend/recv/dump` | ISO-TP | `-s/-d` ID, `-b` BS, `-m` STmin, `-L` FD (5절) |
| `j1939acd`, `j1939cat`, `j1939spy` | J1939 | 6절 |
| `slcand`, `slcan_attach` | 시리얼 CAN 어댑터 붙이기 | `-o -c -s6` (8절) |
| `can-calc-bit-timing` | 비트 타이밍 계산 | `-c clock -b rate -s ‰ -d dbitrate` (2절) |

---

## 4. C로 짜기 — 필터, CAN FD, 타임스탬프, 에러 프레임

I10 5절은 RAW 필터·FD·BCM의 기본 API를 보였다. 여기서는 **실제 앱에 넣을 수신기 하나**를 만든다: 필터 여러 개 + FD 허용 + 모든 에러 클래스 구독 + `SO_TIMESTAMPING`(SW·HW 타임스탬프 동시 요청). 송신기는 주기 송신을 `clock_nanosleep(TIMER_ABSTIME)`으로 하고, 데이터 앞 8바이트에 "보낸 시각"을 넣어 지연을 잰다.

### 4.1 송신기 `q4_tx.c`

무엇을 확인하는 코드인가: 절대 시각 기반 주기 송신, CAN FD 송신(`CAN_RAW_FD_FRAMES` + `CANFD_MTU` 크기 write), 그리고 `ENOBUFS`(큐가 가득 참)를 에러가 아니라 "버린 프레임"으로 세는 패턴.

```c
/* q4_tx IF ID COUNT PERIOD_US [fd] : periodic sender, data[0..7] = CLOCK_REALTIME ns at send */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#define DIE(m) do { perror(m); exit(1); } while (0)
int main(int argc, char **argv)
{
	if (argc < 5) { fprintf(stderr, "usage: %s IF ID COUNT PERIOD_US [fd]\n", argv[0]); return 2; }
	int fdm = argc > 5, one = 1, s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) DIE("socket");
	if (fdm && setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &one, sizeof one)) DIE("FD_FRAMES");
	struct sockaddr_can a = { .can_family = AF_CAN, .can_ifindex = if_nametoindex(argv[1]) };
	if (!a.can_ifindex) DIE("if_nametoindex");
	if (bind(s, (struct sockaddr *)&a, sizeof a)) DIE("bind");
	struct canfd_frame f = { .can_id = strtoul(argv[2], NULL, 16), .len = fdm ? 64 : 8,
				 .flags = fdm ? CANFD_BRS : 0 };
	long n = atol(argv[3]), per = atol(argv[4]), nobuf = 0;
	struct timespec next, now;
	clock_gettime(CLOCK_MONOTONIC, &next);
	for (long i = 0; i < n; i++) {
		next.tv_nsec += per * 1000;
		while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
		clock_gettime(CLOCK_REALTIME, &now);
		unsigned long long ns = now.tv_sec * 1000000000ULL + now.tv_nsec;
		memcpy(f.data, &ns, 8);
		if (fdm) f.data[8] = (unsigned char)i;           /* FD: sequence after the stamp */
		if (write(s, &f, fdm ? CANFD_MTU : CAN_MTU) < 0) {
			if (errno == ENOBUFS) { nobuf++; continue; }  /* qdisc/driver queue full */
			DIE("write");
		}
	}
	printf("sent %ld frames, ENOBUFS %ld\n", n - nobuf, nobuf);
	return 0;
}
```

출력에서 볼 것은 4.3절에서 수신기와 함께 본다. 포인트: write 크기(`CAN_MTU` 16 / `CANFD_MTU` 72)가 곧 "이 프레임은 classic/FD"라는 선언이다. FD 소켓 옵션 없이 72바이트를 쓰면 `EINVAL`이다.

### 4.2 수신기 `q4_rxts.c`

무엇을 확인하는 코드인가: 한 소켓에 필터 배열, FD, 에러 마스크, 타임스탬프를 모두 걸고 `recvmsg()`의 cmsg에서 `SCM_TIMESTAMPING`(타임스탬프 3개짜리 배열)을 꺼낸다.

```c
/* q4_rxts IF COUNT [id:mask ...] : RAW RX with filters, CAN FD, error frames, SO_TIMESTAMPING */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <net/if.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>
#include <linux/net_tstamp.h>
#include <linux/errqueue.h>
#define DIE(m) do { perror(m); exit(1); } while (0)
static long long tsns(const struct timespec *t) { return t->tv_sec * 1000000000LL + t->tv_nsec; }
int main(int argc, char **argv)
{
	int s = socket(PF_CAN, SOCK_RAW, CAN_RAW), one = 1, nf = 0;
	struct can_filter flt[8];
	if (s < 0 || argc < 3) DIE("socket/usage");
	for (int i = 3; i < argc && nf < 8; i++, nf++)
		sscanf(argv[i], "%x:%x", &flt[nf].can_id, &flt[nf].can_mask);
	if (nf && setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, flt, nf * sizeof flt[0])) DIE("FILTER");
	can_err_mask_t em = CAN_ERR_MASK;                     /* all error classes */
	int tsf = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE |
		  SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &one, sizeof one) ||
	    setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &em, sizeof em) ||
	    setsockopt(s, SOL_SOCKET, SO_TIMESTAMPING, &tsf, sizeof tsf)) DIE("setsockopt");
	struct sockaddr_can a = { .can_family = AF_CAN, .can_ifindex = if_nametoindex(argv[1]) };
	if (bind(s, (struct sockaddr *)&a, sizeof a)) DIE("bind");
	long long sum1 = 0, sum2 = 0, max1 = 0, max2 = 0; int n = atoi(argv[2]), lat = 0;
	for (int k = 0; k < n; k++) {
		struct canfd_frame f; char cb[256]; struct timespec now, *ts = NULL;
		struct iovec iov = { &f, sizeof f };
		struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = cb, .msg_controllen = sizeof cb };
		ssize_t r = recvmsg(s, &m, 0);
		clock_gettime(CLOCK_REALTIME, &now);
		if (r != CAN_MTU && r != CANFD_MTU) DIE("recvmsg");
		for (struct cmsghdr *c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c))
			if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SO_TIMESTAMPING)
				ts = ((struct scm_timestamping *)CMSG_DATA(c))->ts; /* [0]=SW [2]=HW */
		if (f.can_id & CAN_ERR_FLAG) {
			printf("ERR class=0x%03x ctrl=0x%02x prot=0x%02x tec=%u rec=%u\n", f.can_id & CAN_ERR_MASK,
			       f.data[1], f.data[2], f.data[6], f.data[7]);
			continue;
		}
		long long sent; memcpy(&sent, f.data, 8);
		long long d1 = ts ? tsns(&ts[0]) - sent : -1, d2 = ts ? tsns(&now) - tsns(&ts[0]) : -1;
		if (k < 3) printf("%s id=%03x len=%d sw_ts=%lld.%09ld hw_ts=%lld send->kts=%lldns kts->app=%lldns\n",
				  r == CANFD_MTU ? "FD" : "CC", f.can_id, f.len, ts ? (long long)ts[0].tv_sec : 0,
				  ts ? ts[0].tv_nsec : 0, ts ? tsns(&ts[2]) : 0, d1, d2);
		lat++; sum1 += d1; sum2 += d2; if (d1 > max1) max1 = d1; if (d2 > max2) max2 = d2;
	}
	if (lat) printf("n=%d send->kts avg %lld max %lld us; kts->app avg %lld max %lld us\n", lat,
			sum1 / lat / 1000, max1 / 1000, sum2 / lat / 1000, max2 / 1000);
	return 0;
}
```

- `CAN_RAW_ERR_FILTER`에 `CAN_ERR_MASK`(모든 클래스)를 주면 일반 필터와 무관하게 에러 프레임이 들어온다. 실무 앱은 최소한 `CAN_ERR_BUSOFF | CAN_ERR_CRTL | CAN_ERR_RESTARTED`는 받아야 한다.
- `scm_timestamping.ts[0]`은 SW 타임스탬프(skb가 스택에 들어온 시각), `ts[2]`는 HW 타임스탬프(컨트롤러가 찍은 시각, 지원하는 드라이버만)다. `ts[1]`은 과거 호환용으로 비어 있다.

### 4.3 예제 6 — 필터 동작과 CAN FD

```sh
$B/q4_rxts q4vc0 4 123:7FF 500:700 & P=$!; sleep 0.5
cansend q4vc0 124#00; cansend q4vc0 7FF#00; $B/q4_tx q4vc0 123 2 1000; $B/q4_tx q4vc0 5AB 2 1000
wait $P
$B/q4_rxts q4vcf 3 & P=$!; sleep 0.5; $B/q4_tx q4vcf 1A0 3 1000 fd; wait $P
$B/q4_tx q4vc0 1A0 1 1000 fd
```

```text
sent 2 frames, ENOBUFS 0
CC id=123 len=8 sw_ts=1792341987.996279152 hw_ts=0 send->kts=238544ns kts->app=6775600ns
CC id=123 len=8 sw_ts=1792341988.000283632 hw_ts=0 send->kts=273792ns kts->app=296825312ns
CC id=5ab len=8 sw_ts=1792341989.331642544 hw_ts=0 send->kts=239712ns kts->app=5968640ns
n=4 send->kts avg 216 max 273 us; kts->app avg 78524 max 296825 us
sent 2 frames, ENOBUFS 0
sent 3 frames, ENOBUFS 0
FD id=1a0 len=64 sw_ts=1792341991.359659008 hw_ts=0 send->kts=237536ns kts->app=9966240ns
FD id=1a0 len=64 sw_ts=1792341991.365971248 hw_ts=0 send->kts=146544ns kts->app=164341824ns
FD id=1a0 len=64 sw_ts=1792341991.366491152 hw_ts=0 send->kts=98368ns kts->app=173504736ns
n=3 send->kts avg 160 max 237 us; kts->app avg 115937 max 173504 us
sent 1 frames, ENOBUFS 0
```

(두 프로세스가 같은 stdout에 쓰므로 줄 순서가 섞여 있다. 수신기는 처음 3개만 자세히 찍는다.) 출력에서 볼 것:

- 0x124, 0x7FF은 필터에 걸려 안 들어왔다. 0x123(정확 일치)과 0x5AB(0x500~0x5FF 범위) 4개만 받았다.
- `hw_ts=0`: vcan은 HW 타임스탬프가 없다. `ethtool -T q4vc0`도 `software-transmit / software-receive`만 보고했다. HW 타임스탬프가 필요하면 mcp251xfd·m_can·flexcan처럼 `get_ts_info`가 `hardware-receive`를 보고하는 컨트롤러를 고른다(I10 8.4절, Q3).
- 마지막 줄: cansend는 거부했던 q4vc0(MTU 2060)에 **내 프로그램은 FD 프레임을 보냈다**(3.1절의 함정 확인).
- `kts->app`가 수 ms~수백 ms로 들쭉날쭉하다 — 다음 절에서 측정으로 본다.

### 4.4 예제 7 — 필터 마스크의 함정: 29-bit 프레임이 몰래 들어온다

무엇을 확인하나: `123:7FF`라는 "흔한" 필터가 29-bit ID 0x00000123도 통과시키는지, 그리고 커널이 필터를 어느 수신 리스트에 넣는지(`/proc/net/can/rcvlist_*`).

```sh
for m in 123:7FF 123:C00007FF; do
  echo "== filter $m"
  $B/q4_rxts q4vc0 2 $m > /tmp/f.txt & P=$!; sleep 0.5
  grep -h "q4vc0  *123" /proc/net/can/rcvlist_sff /proc/net/can/rcvlist_fil | sed 's/ffff[0-9a-f]*/<ptr>/g'
  cansend q4vc0 00000123#01; cansend q4vc0 123#02; cansend q4vc0 123#03; sleep 0.5
  kill $P 2>/dev/null; wait $P 2>/dev/null; cat /tmp/f.txt
done
```

```text
== filter 123:7FF
   q4vc0     123    000007ff  <ptr>  <ptr>         0  raw
CC id=80000123 len=1 sw_ts=1792342626.552457104 hw_ts=0 send->kts=1792342626552457103ns kts->app=2587520ns
CC id=123 len=1 sw_ts=1792342626.636069056 hw_ts=0 send->kts=1792342626636069054ns kts->app=10328944ns
n=2 send->kts avg 1792342626594263 max 1792342626636069 us; kts->app avg 6458 max 10328 us
== filter 123:C00007FF
   q4vc0     123    c00007ff  <ptr>  <ptr>         0  raw
CC id=123 len=1 sw_ts=1792342628.471110736 hw_ts=0 send->kts=1792342628471110734ns kts->app=739056ns
CC id=123 len=1 sw_ts=1792342628.568639184 hw_ts=0 send->kts=1792342628568639181ns kts->app=4474800ns
n=2 send->kts avg 1792342628519874 max 1792342628568639 us; kts->app avg 2606 max 4474 us
```

(이 실험의 데이터는 송신 시각이 아니라서 `send->kts` 값은 의미 없다. 포인터 주소는 `<ptr>`로 가렸다.) 출력에서 볼 것:

- 마스크 `0x7FF`는 하위 11비트만 비교한다. **29-bit 프레임 0x00000123(`id=80000123`, 비트 31 = `CAN_EFF_FLAG`)도 통과했다.** 차량 버스처럼 11/29-bit가 섞인 버스에서 실제로 터지는 버그다.
- 마스크에 `CAN_EFF_FLAG | CAN_RTR_FLAG`(= `0xC0000000`)를 넣은 `0xC00007FF`는 "11-bit 데이터 프레임 0x123만"을 뜻한다. 29-bit 프레임은 걸러졌다.
- 커널은 필터를 어느 리스트에 넣나? 위 명령은 `grep -h`로 두 파일을 같이 봐서 출력만으로는 구분이 안 된다. 따로 확인한 첫 번째 경우(`123:7FF`)는 `/proc/net/can/rcvlist_fil`에 있었다(아래 출력). 커널 `net/can/af_can.c`의 `can_rcv_list_find()`를 보면 **단일 ID 해시 리스트(`rx_sff[id]`, `rx_eff[hash]`)는 마스크가 정확히 `CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG`(29-bit면 `CAN_EFF_MASK | …`)이고 can_id에 RTR이 없을 때만** 쓴다. 그러니 두 번째 필터 `0xC00007FF`는 소스상 `rx_sff[0x123]`으로 간다 — 수신 때 마스크 비교 루프를 돌지 않는 빠른 경로다. 필터가 수백 개인 게이트웨이 앱에서는 이 차이가 CPU 시간으로 드러난다.

```sh
$B/q4_rxts q4vc0 1 123:7FF 500:700 > /dev/null & P=$!; sleep 0.5
grep -B1 -A3 "q4vc0" /proc/net/can/rcvlist_fil | head -6
grep -A3 "q4vc0" /proc/net/can/rcvlist_err | head -4
```

```text
  device   can_id   can_mask      function          userdata       matches  ident
   q4vc0     500    00000700  ffffb8aea92fc5d8  ffff000004e16800         0  raw
   q4vc0     123    000007ff  ffffb8aea92fc5d8  ffff000004e16800         0  raw

   q4vc0     000    1fffffff  ffffb8aea92fc5d8  ffff000004e16800         0  raw
```

(첫 세 줄이 `rcvlist_fil`, 마지막 줄이 `rcvlist_err`다. 같은 실행에서 `rcvlist_sff`의 q4vc0 항목은 `(q4vc0: no entry)`였다.) 에러 마스크도 `rx_err` 리스트의 한 항목(마스크 = 구독한 에러 클래스)으로 들어간다는 것도 보인다. 실무 결론: **필터 마스크에는 EFF/RTR 비트를 넣어 의도를 명시한다** — 버그도 막고 빠른 경로도 탄다.

### 4.5 예제 8 — 수신 지연 측정: 커널 타임스탬프 vs 앱이 읽은 시각

무엇을 확인하나: 1 ms 주기 2000프레임을 보내며 ① 송신 write → 커널 RX 타임스탬프, ② 커널 RX 타임스탬프 → 앱이 `recvmsg`에서 깨어난 시각을 잰다. CPU 부하(`stress-ng --cpu 2`)가 있을 때와 비교한다.

```sh
for mode in idle load; do
  [ $mode = load ] && { stress-ng --cpu 2 --timeout 8s >/dev/null 2>&1 & S=$!; sleep 1; }
  $B/q4_rxts q4vc0 2000 123:7FF > /tmp/lat_$mode.txt & P=$!; sleep 0.5
  $B/q4_tx q4vc0 123 2000 1000; wait $P; tail -1 /tmp/lat_$mode.txt
  [ $mode = load ] && wait $S
done
chrt -f 80 $B/q4_rxts q4vc0 2000 123:7FF
```

```text
sent 2000 frames, ENOBUFS 0
n=2000 send->kts avg 303 max 53312 us; kts->app avg 3527 max 70977 us
sent 2000 frames, ENOBUFS 0
n=2000 send->kts avg 328 max 45359 us; kts->app avg 4975 max 126999 us
chrt: failed to set pid 0's policy: Operation not permitted
```

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">vcan 지연 평균 (µs, 2000 프레임, 1 ms 주기) — TCG 에뮬레이션 값</text><text x="172.0" y="70.0" font-size="12" text-anchor="end">send → 커널 RX ts</text><rect x="180" y="40" width="21.8" height="24" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="207.8" y="57.0" font-size="12" text-anchor="start">303 (idle)</text><rect x="180" y="70" width="23.6" height="24" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="209.6" y="87.0" font-size="12" text-anchor="start">328 (CPU 부하)</text><text x="172.0" y="160.0" font-size="12" text-anchor="end">커널 RX ts → 앱 read</text><rect x="180" y="130" width="253.9" height="24" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="439.9" y="147.0" font-size="12" text-anchor="start">3527 (idle)</text><rect x="180" y="160" width="358.2" height="24" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="544.2" y="177.0" font-size="12" text-anchor="start">4975 (CPU 부하)</text><line x1="180" y1="34" x2="180" y2="222" stroke="currentColor" stroke-width="1.3"/><line x1="180.0" y1="222" x2="180.0" y2="227" stroke="currentColor" stroke-width="1.3"/><text x="180.0" y="242.0" font-size="12" text-anchor="middle">0</text><line x1="252.0" y1="222" x2="252.0" y2="227" stroke="currentColor" stroke-width="1.3"/><text x="252.0" y="242.0" font-size="12" text-anchor="middle">1000</text><line x1="324.0" y1="222" x2="324.0" y2="227" stroke="currentColor" stroke-width="1.3"/><text x="324.0" y="242.0" font-size="12" text-anchor="middle">2000</text><line x1="396.0" y1="222" x2="396.0" y2="227" stroke="currentColor" stroke-width="1.3"/><text x="396.0" y="242.0" font-size="12" text-anchor="middle">3000</text><line x1="468.0" y1="222" x2="468.0" y2="227" stroke="currentColor" stroke-width="1.3"/><text x="468.0" y="242.0" font-size="12" text-anchor="middle">4000</text><line x1="540.0" y1="222" x2="540.0" y2="227" stroke="currentColor" stroke-width="1.3"/><text x="540.0" y="242.0" font-size="12" text-anchor="middle">5000</text><text x="10.0" y="262.0" font-size="12" text-anchor="start">최댓값: idle 53 / 71 ms, 부하 45 / 127 ms — 공용 VM에서 QEMU 여러 개가 도는 TCG 값 (실보드 아님)</text></svg>
```

그림 5 — 위 측정의 평균값. 커널이 프레임을 받는 순간까지(파랑·주황 위)는 수백 µs로 비슷하지만, **커널 타임스탬프 → 앱** 구간은 ms 단위이고 부하에 따라 늘어난다. 최댓값은 수십~백 ms다.

출력에서 볼 것:

- 숫자 자체는 TCG + 과부하 VM이라 실보드보다 수십~수백 배 나쁘다. 하지만 **구조**는 실보드와 같다: 프레임이 버스에 있던 시각에 가장 가까운 것은 커널(또는 HW) 타임스탬프이고, 앱이 읽은 시각은 스케줄링 지연만큼 늦다. **센서 퓨전에는 `clock_gettime()`이 아니라 `SO_TIMESTAMPING` 값을 써야 한다**(Q3).
- `chrt -f 80`이 root인데도 `EPERM`이었다. systemd 게스트에서 셸이 속한 cgroup에 RT 런타임이 할당되지 않은 경우의 전형적 증상이다(`CONFIG_RT_GROUP_SCHED`). 실시간 수신 스레드는 systemd 유닛에 `CPUSchedulingPolicy=fifo`를 주거나 cgroup 설정을 확인한다(N2). 이 노트에서는 더 파지 않았다.

---

## 5. vxcan + 네트워크 네임스페이스 + can-gw + ISO-TP

### 5.1 왜 vxcan인가

vcan은 "자기 자신에게 돌아오는 버스"라서 두 프로그램이 같은 인터페이스를 공유한다. **vxcan은 veth의 CAN 버전**이다: 한 쌍의 인터페이스이고, 한쪽에서 보낸 프레임이 다른 쪽에서 수신된다. 한쪽을 다른 네트워크 네임스페이스(컨테이너)로 옮기면 "ECU 시뮬레이터는 격리된 컨테이너, 메인 앱은 호스트" 같은 구성이 된다. HIL 테스트 벤치, 도커로 돌리는 차량 시뮬레이터가 이 방식이다.

### 5.2 예제 9 — 네임스페이스 너머로 라우팅하기

무엇을 확인하나: vxcan 쌍을 만들고 한쪽을 `q4ecu` 네임스페이스로 보낸 뒤, can-gw로 "센서 버스(q4vc0)의 0x100~0x1FF를 ID에 0x400을 XOR해서 ECU 쪽으로" 보내는 규칙을 건다.

```sh
modprobe vxcan; modprobe can-gw; modprobe can-isotp
ip netns add q4ecu
ip link add q4vx0 type vxcan peer name q4vx1 netns q4ecu
ip link set q4vx0 up; ip -n q4ecu link set q4vx1 up; ip -n q4ecu link set lo up
ip -details link show q4vx0; ip netns exec q4ecu ip -br link
ip netns exec q4ecu candump -t d -n 3 q4vx1 > /tmp/ns1.txt & P=$!; sleep 0.5
cansend q4vx0 321#C0FFEE; cansend q4vc0 321#00
cangw -A -s q4vc0 -d q4vx0 -f 100:700 -m XOR:I:400.0.0000000000000000
cangw -A -s q4vx0 -d q4vc0 -f 7E8:7FF -e
cansend q4vc0 1A0#11223344; cansend q4vc0 2A0#55; cansend q4vc0 101#66
sleep 0.5; cat /tmp/ns1.txt; cangw -L; ip netns exec q4ecu cangw -L
```

```text
6: q4vx0@if2: <NOARP,UP,LOWER_UP> mtu 2060 qdisc noqueue state UP mode DEFAULT group default qlen 1000
    link/can  link-netns q4ecu promiscuity 0  allmulti 0 minmtu 0 maxmtu 0 
    vxcan addrgenmode eui64 numtxqueues 1 numrxqueues 1 ...
lo               UNKNOWN        00:00:00:00:00:00 <LOOPBACK,UP,LOWER_UP> 
q4vx1@if6        UP             <NOARP,UP,LOWER_UP> 
 (000.000000)  q4vx1  321   [3]  C0 FF EE
 (000.299740)  q4vx1  5A0   [4]  11 22 33 44
 (000.146780)  q4vx1  501   [1]  66
cangw -A -s q4vx0 -d q4vc0 -e -f 7E8:7FF # 0 handled 0 dropped 0 deleted
cangw -A -s q4vc0 -d q4vx0 -m XOR:I:400.0.0000000000000000 -f 100:700 # 2 handled 0 dropped 0 deleted
```

출력에서 볼 것:

- `link-netns q4ecu`: 짝이 다른 네임스페이스에 있다. 네임스페이스 안에서는 `lo`와 `q4vx1`만 보인다 — 호스트의 vcan·can0은 존재하지 않는다.
- q4vx0으로 직접 보낸 0x321은 짝으로 건너갔고, q4vc0의 0x321은 규칙 범위 밖이라 안 건너갔다. 0x1A0 → **0x5A0**, 0x101 → **0x501**(XOR 0x400), 0x2A0은 필터에서 빠졌다. `# 2 handled`가 규칙의 카운터다.
- 네임스페이스 안의 `cangw -L`은 아무것도 출력하지 않았다: **can-gw 규칙은 네임스페이스마다 따로**다. 그리고 can-gw는 같은 네임스페이스의 두 인터페이스 사이에서만 라우팅한다 — 네임스페이스를 넘는 건 vxcan의 몫이다. vxcan은 넘길 때 can-gw hop 카운터(`csum_start`)를 0으로 되돌린다(`drivers/net/can/vxcan.c`의 "reset CAN GW hop counter" 주석) — 네임스페이스마다 독립된 라우팅 도메인이라는 뜻이다.
- `-m XOR:I:400.0.<data>`의 형식은 `연산:대상필드:id.len.data`이다. ID 외에 데이터 바이트를 AND/OR/XOR/SET 할 수 있고, `-x`/`-c`로 XOR 체크섬·CRC8을 다시 계산하게 할 수도 있다(`cangw -h`).

### 5.3 예제 10 — ISO-TP로 300바이트 보내기 (BS=4, STmin=5 ms)

무엇을 확인하나: 8바이트 프레임 버스에 300바이트를 보낼 때 ISO-TP(ISO 15765-2)가 First Frame, Flow Control, Consecutive Frame으로 쪼개는 모습. 수신 측(ECU)이 **block size 4, STmin 5 ms**를 요구하면 송신 측 커널이 그것을 지키는지. I10 5.4절은 40바이트·BS=0이었다. 이번엔 흐름 제어가 실제로 일하는 경우다.

```sh
ip netns exec q4ecu isotprecv -s 7E8 -d 7E0 -b 4 -m 5 q4vx1 > /tmp/iso_rx.txt & R=$!
candump -t d -x q4vx0 > /tmp/iso_dump.txt & D=$!
isotpdump -t d -s 7E0 -d 7E8 q4vx0 > /tmp/iso_dec.txt & I=$!; sleep 0.5
python3 -c "print(' '.join('%02X' % (i & 255) for i in range(300)))" > /tmp/p300.txt
time isotpsend -s 7E0 -d 7E8 q4vx0 < /tmp/p300.txt
sleep 0.5; kill $D $I; wait $R
echo "frames on wire: $(wc -l < /tmp/iso_dump.txt)"; head -8 /tmp/iso_dump.txt; tail -2 /tmp/iso_dump.txt
head -4 /tmp/iso_dec.txt
wc -w < /tmp/iso_rx.txt    # + 원본과 cmp
```

```text
real	0m0.389s
user	0m0.029s
sys	0m0.111s
frames on wire: 54
 (000.000000)  q4vx0  RX - -  7E8   [3]  30 04 05
 (000.000718)  q4vx0  TX - -  7E0   [8]  21 06 07 08 09 0A 0B 0C
 (000.009790)  q4vx0  TX - -  7E0   [8]  22 0D 0E 0F 10 11 12 13
 (000.000516)  q4vx0  TX - -  7E0   [8]  11 2C 00 01 02 03 04 05
 (000.010661)  q4vx0  TX - -  7E0   [8]  23 14 15 16 17 18 19 1A
 (000.005834)  q4vx0  TX - -  7E0   [8]  24 1B 1C 1D 1E 1F 20 21
 (000.000194)  q4vx0  RX - -  7E8   [3]  30 04 05
 (000.000338)  q4vx0  TX - -  7E0   [8]  25 22 23 24 25 26 27 28
...
 (000.000297)  q4vx0  TX - -  7E0   [8]  29 1E 1F 20 21 22 23 24
 (000.006982)  q4vx0  TX - -  7E0   [8]  2A 25 26 27 28 29 2A 2B
(0.000000)  q4vx0  7E8  [3]  [FC] FC: 0 = CTS # BS: 4 # STmin: 0x05 = 5 ms
(0.000000)  q4vx0  7E0  [8]  [CF] sn: 1    data: 06 07 08 09 0A 0B 0C 
(0.009790)  q4vx0  7E0  [8]  [CF] sn: 2    data: 0D 0E 0F 10 11 12 13 
(0.000516)  q4vx0  7E0  [8]  [FF] ln: 300  data: 00 01 02 03 04 05 
300
payload identical
```

(`...`은 스크립트가 찍은 구분선이고, 그 뒤 중간 6줄은 생략했다.) 출력에서 볼 것:

- **First Frame** `11 2C 00 01 …`: 상위 니블 1 = FF, 길이 12비트 = 0x12C = 300. 데이터 6바이트가 같이 간다.
- **Flow Control** `30 04 05`: 상위 니블 3 = FC, 상태 0 = CTS(보내도 됨), BS = 4, STmin = 0x05 = 5 ms. 송신자는 CF 4개마다 새 FC를 기다린다.
- **Consecutive Frame** `21`, `22`, … `2F`, `20`, `21` …: 상위 니블 2 = CF, 하위 니블 = 4비트 순번(SN, 1부터 시작해 15 다음 0으로 돈다). 그래서 끝 부분에 `29`, `2A`가 다시 나온다.
- 프레임 수 54 = FF 1 + CF 42(6 + 42 × 7 = 300바이트) + FC 11(첫 FC + 4개마다 1개). CF 사이 간격이 5~10 ms인 것이 STmin이 지켜진 증거다. 유저 공간은 `write` 한 번(isotpsend), `read` 한 번(isotprecv)만 했다 — **분할·FC 대기·STmin 타이머는 전부 커널 `net/can/isotp.c` 안**(hrtimer)이다.
- 순서 주의: candump에서 FF의 TX(echo)가 FC보다 **뒤에** 찍혔다. vxcan은 `IFF_ECHO`가 없어서 송신 프레임의 로컬 복사본을 af_can의 `can_send()`가 `dev_queue_xmit()` 뒤에 따로 `netif_rx()`로 넣는다. 상대가 그 사이에 FC를 돌려보내면 candump는 "FC → FF" 순서로 본다. **candump의 TX/RX 순서를 버스 순서로 단정하지 말 것** — 순서가 중요하면 타임스탬프를 비교하거나 실제 버스에서 별도 수신 노드로 본다.
- `isotpdump`는 FF·FC·CF를 사람이 읽게 풀어 준다. `-u`를 주면 UDS 서비스(예: `0x22 ReadDataByIdentifier`)까지 해석한다.

같은 300바이트를 CAN FD ISO-TP(링크 MTU 72, TX_DL 64, BRS)로 보내면:

```sh
ip link set q4vx0 down; ip link set q4vx0 mtu 72; ip link set q4vx0 up      # q4vx1도 같은 방법
ip netns exec q4ecu isotprecv -s 7E8 -d 7E0 -L 72:64:1 q4vx1 > /tmp/iso_rx2.txt &
isotpsend -s 7E0 -d 7E8 -L 72:64:1 q4vx0 < /tmp/p300.txt
```

```text
FD frames on wire: 6
 (000.000000)  q4vx0  RX B -  7E8  [03]  30 00 00
 (000.000209)  q4vx0  TX B -  7E0  [64]  21 3E 3F 40 41 42 43 44 45 46 47 48 49 4A 4B 4C 4
 (000.000109)  q4vx0  TX B -  7E0  [64]  11 2C 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0
 (000.000157)  q4vx0  TX B -  7E0  [64]  22 7D 7E 7F 80 81 82 83 84 85 86 87 88 89 8A 8B 8
 (000.000271)  q4vx0  TX B -  7E0  [64]  23 BC BD BE BF C0 C1 C2 C3 C4 C5 C6 C7 C8 C9 CA C
 (000.000347)  q4vx0  TX B -  7E0  [64]  24 FB FC FD FE FF 00 01 02 03 04 05 06 07 08 09 0
(줄은 90자에서 잘랐다)
```

54프레임이 6프레임이 됐다. FF가 62바이트, CF가 63바이트씩 나른다. ECU 펌웨어 업데이트(UDS `0x36 TransferData`)에서 CAN FD가 classic보다 수 배 빠른 이유가 이것이다.

---

## 6. J1939 — 트럭·농기계·발전기의 공용어

J1939(SAE)는 29-bit ID를 **우선순위(3) · PGN(Parameter Group Number, 18) · 소스 주소(SA, 8)**로 쪼개 쓰는 상위 프로토콜이다. 노드는 부팅 때 **주소 클레임**(Address Claim, PGN 0xEE00)으로 8비트 주소를 협상하고, 8바이트보다 긴 데이터는 **Transport Protocol**(TP.CM = PGN 0xEC00, TP.DT = PGN 0xEB00)로 나눈다. Linux 5.4부터 `CAN_J1939` 소켓이 이것을 커널에서 처리한다.

### 6.1 예제 11 — 주소 클레임과 100바이트 TP 전송

무엇을 확인하나: 두 노드가 같은 주소 범위를 원할 때의 클레임, 그리고 `j1939cat`(J1939 소켓을 쓰는 netcat)로 100바이트를 보낼 때 커널이 만드는 RTS/CTS/DT/EndOfMsgAck.

```sh
modprobe can-j1939
candump -t d -x q4vc0 > /tmp/j.txt & D=$!; sleep 0.5
j1939acd -r 0x80-0x82 -c /tmp/a.jacd 11223344556677A0 q4vc0 & A1=$!; sleep 1.5
j1939acd -r 0x80-0x82 -c /tmp/b.jacd 11223344556677B0 q4vc0 & A2=$!; sleep 1.5
j1939cat -r q4vc0:0x91 > /tmp/j_rx.bin & R=$!; sleep 0.5
head -c 100 /dev/urandom > /tmp/j_tx.bin
j1939cat -i /tmp/j_tx.bin q4vc0:0x90 :0x91,0x12300; sleep 1
kill $R; cmp /tmp/j_tx.bin /tmp/j_rx.bin && echo "j1939 100 B transfer OK"
cat /tmp/j.txt
```

```text
  RX RTS: 1792342324 s 862593 us (seq=000, send=0000000)
  total size: 100, pgn=0x12300, sa=0x90, da=0x91 src_name=0x00000000, dst_name=0x00000000)
  TX ACK: 1792342324 s 883512 us (seq=000, send=0000100)
j1939 100 B transfer OK
 (000.000000)  q4vc0  TX - -  18EAFFFE   [3]  00 EE 00
 (001.272060)  q4vc0  TX - -  18EEFF80   [8]  A0 77 66 55 44 33 22 11
 (000.526794)  q4vc0  TX - -  18EAFFFE   [3]  00 EE 00
 (000.011640)  q4vc0  TX - -  18EEFF80   [8]  A0 77 66 55 44 33 22 11
 (001.249907)  q4vc0  TX - -  18EEFF81   [8]  B0 77 66 55 44 33 22 11
 (001.650331)  q4vc0  RX - -  18EC9190   [8]  10 64 00 0F 0F 00 23 01
 (000.007584)  q4vc0  RX - -  18EC9091   [8]  11 0F 01 FF FF 00 23 01
 (000.007210)  q4vc0  RX - -  18EB9190   [8]  01 17 EA 52 F7 E6 B7 DE
 (000.000116)  q4vc0  RX - -  18EB9190   [8]  02 DB 30 D8 BF CB C7 0C
 ...
 (000.000050)  q4vc0  RX - -  18EB9190   [8]  0F 1C A4 FF FF FF FF FF
 (000.008412)  q4vc0  RX - -  18EC9091   [8]  13 64 00 0F FF 00 23 01
```

(TP.DT 03~0E 12줄은 생략했다.) 출력에서 볼 것 — ID를 손으로 풀어 보자: `18EEFF80` = 우선순위 6(0x18 >> 2), PGN 0xEE00(Address Claimed), 목적지 0xFF(전체), SA 0x80.

- `18EAFFFE … 00 EE 00`: SA 0xFE(아직 주소 없음)가 "모두 Address Claimed(0x00EE00)를 보고하라"고 **요청**(PGN 0xEA00)한다.
- `18EEFF80 A0 77 66 … 11`: 데이터 8바이트가 64-bit NAME(리틀 엔디언 `11223344556677A0`)이다. 첫 노드가 0x80을 차지했다.
- 두 번째 노드(NAME `…B0`)는 0x80이 이미 더 작은 NAME(우선)에게 있다는 것을 보고 **0x81**을 클레임했다. 충돌하면 NAME이 작은 쪽이 이기는 규칙 — CAN 중재와 같은 "작은 값 우선" 철학이다.
- TP: `18EC9190 10 64 00 0F 0F …` = TP.CM **RTS**(0x10), 크기 0x0064 = 100, 패킷 15개, CTS당 최대 15개, PGN 0x012300(마지막 3바이트 `00 23 01`). 수신자 `11 0F 01` = **CTS**(15개, 1번부터). 그다음 `18EB9190` TP.DT 01~0F(7바이트씩, 마지막은 FF 패딩), 마지막 `13 …` = **EndOfMsgAck**.
- candump에 `RX`로 찍힌 이유: j1939 스택이 같은 vcan에서 송·수신을 모두 하고, J1939 소켓이 보낸 프레임은 candump 소켓 입장에서 "다른 소켓이 보낸 것"이기 때문이다.
- 실무: J1939 차량 데이터(엔진 rpm = PGN 61444 EEC1 등)는 DBC(7절)로 풀고, 주소 클레임·TP는 커널 소켓에 맡긴다. 유저 공간 라이브러리로 TP를 직접 구현하던 시절의 타이밍 버그(TP.DT 간격 50~200 ms 규칙)를 커널이 대신 지켜 준다.

---

## 7. DBC + cantools + python-can — 바이트를 물리량으로

### 7.1 DBC 파일이란

DBC(Vector의 CAN database 형식)는 "어떤 ID의 몇 번째 비트부터 몇 비트가 무슨 신호이고, 스케일·오프셋·단위가 무엇인지"를 적은 텍스트 파일이다. 차량·로봇 회사에서 **버스의 계약서** 역할을 한다. 펌웨어 팀(MCU)과 Linux 팀이 같은 DBC에서 코드를 생성하거나 디코드한다. bare-metal 비유: 레지스터 맵 헤더(`#define FOO_SHIFT`, `FOO_MASK`)를 버스 메시지에 대해 만든 것.

이 노트용으로 만든 `q4_robot.dbc` (모터 명령·상태 + J1939 스타일 BMS):

```text
VERSION ""

NS_ :

BS_:

BU_: MAIN MOTOR1 BMS

BO_ 512 MotorCmd: 8 MAIN
 SG_ TargetSpeed : 0|16@1- (0.1,0) [-3000|3000] "rpm" MOTOR1
 SG_ TorqueLimit : 16|8@1+ (0.5,0) [0|127.5] "Nm" MOTOR1
 SG_ Mode : 24|2@1+ (1,0) [0|3] "" MOTOR1
 SG_ Counter : 56|4@1+ (1,0) [0|15] "" MOTOR1

BO_ 513 MotorStatus: 8 MOTOR1
 SG_ ActualSpeed : 0|16@1- (0.1,0) [-3000|3000] "rpm" MAIN
 SG_ Current : 16|12@1+ (0.01,0) [0|40.95] "A" MAIN
 SG_ Temp : 32|8@1+ (1,-40) [-40|215] "degC" MAIN
 SG_ Fault : 40|1@1+ (1,0) [0|1] "" MAIN
 SG_ Counter : 56|4@1+ (1,0) [0|15] "" MAIN

BO_ 2566869221 BMS_Pack: 8 BMS
 SG_ PackVoltage : 7|16@0+ (0.01,0) [0|655.35] "V" MAIN
 SG_ PackCurrent : 23|16@0- (0.1,0) [-3276.8|3276.7] "A" MAIN
 SG_ SoC : 39|8@0+ (0.5,0) [0|100] "%" MAIN

CM_ SG_ 512 TargetSpeed "Signed speed setpoint, little-endian (Intel)";
CM_ BO_ 2566869221 "J1939-style 29-bit ID 0x18FF50E5, big-endian (Motorola) signals";
VAL_ 512 Mode 0 "Off" 1 "Speed" 2 "Torque" 3 "Fault" ;
```

신호 한 줄 읽는 법: `SG_ TargetSpeed : 0|16@1- (0.1,0) [-3000|3000] "rpm" MOTOR1`

- `0|16`: 시작 비트 0, 길이 16.
- `@1`: 바이트 순서 **Intel(little-endian)**, `@0`이면 **Motorola(big-endian)**. `-`는 signed, `+`는 unsigned.
- `(0.1,0)`: 물리값 = raw × 0.1 + 0. `[-3000|3000]`: 허용 범위. `"rpm"`: 단위. 마지막은 수신 노드.
- 확장 ID 메시지는 ID에 0x80000000을 더해 적는다: `2566869221` = `0x98FF50E5` = 0x18FF50E5 + EFF 표시.
- Motorola 신호의 시작 비트는 **MSB의 위치**(`7|16@0+`는 바이트 0의 비트 7에서 시작해 바이트 1로 내려간다). DBC에서 가장 많이 틀리는 부분이다.

### 7.2 예제 12 — python-can으로 주기 명령 + 응답 시뮬레이터, candump로 기록

무엇을 확인하는 코드인가: MAIN이 `MotorCmd`를 100 Hz로 보내고(`send_periodic`), 시뮬레이터 MOTOR1이 받은 명령마다 `MotorStatus`로 답하며, MAIN이 응답을 DBC로 풀고 주기를 잰다. 둘 다 python-can의 `Notifier`(백그라운드 수신 스레드)를 쓴다.

```python
"""q4_pycan.py IF : python-can + cantools - MAIN sends MotorCmd @100 Hz, a sim MOTOR1 answers."""
import sys, time, statistics, can, cantools

db = cantools.database.load_file("/mnt/q4/q4_robot.dbc")
cmd_m, st_m = db.get_message_by_name("MotorCmd"), db.get_message_by_name("MotorStatus")
main = can.Bus(interface="socketcan", channel=sys.argv[1])
motor = can.Bus(interface="socketcan", channel=sys.argv[1],
                can_filters=[{"can_id": cmd_m.frame_id, "can_mask": 0x7FF}])

class MotorSim(can.Listener):                       # runs in the Notifier thread
    def on_message_received(self, msg):
        c = cmd_m.decode(msg.data)
        data = st_m.encode({"ActualSpeed": c["TargetSpeed"] * 0.98, "Current": 3.2,
                            "Temp": 41, "Fault": 0, "Counter": c["Counter"]})
        motor.send(can.Message(arbitration_id=st_m.frame_id, data=data, is_extended_id=False))

stamps = []
class Logger(can.Listener):
    def on_message_received(self, msg):
        if msg.arbitration_id == st_m.frame_id:
            stamps.append(msg.timestamp)            # kernel RX timestamp (SO_TIMESTAMP)
            if len(stamps) <= 2: print("rx", db.decode_message(msg.arbitration_id, msg.data))

n1 = can.Notifier(motor, [MotorSim()]); n2 = can.Notifier(main, [Logger()])
def payload(i): return cmd_m.encode({"TargetSpeed": 1500.0, "TorqueLimit": 20, "Mode": "Speed", "Counter": i % 16})
task = main.send_periodic(can.Message(arbitration_id=cmd_m.frame_id, data=payload(0), is_extended_id=False), 0.01)
for i in range(1, 100):                             # update the rolling counter while the task runs
    time.sleep(0.01); task.modify_data(can.Message(arbitration_id=cmd_m.frame_id, data=payload(i), is_extended_id=False))
task.stop(); time.sleep(0.1); n1.stop(); n2.stop()
gaps = [(b - a) * 1e3 for a, b in zip(stamps, stamps[1:])]
print(f"status frames={len(stamps)} period ms: mean={statistics.mean(gaps):.2f} "
      f"stdev={statistics.pstdev(gaps):.2f} min={min(gaps):.2f} max={max(gaps):.2f}")
main.shutdown(); motor.shutdown()
```

```sh
candump -l q4vc0 & P=$!; sleep 0.5
python3 /mnt/q4/q4_pycan.py q4vc0
cansend q4vc0 18FF50E5#4E20FF9CB4000000
kill -INT $P
```

```text
rx {'ActualSpeed': 1470.0, 'Current': 3.2, 'Temp': 41, 'Fault': 0, 'Counter': 0}
rx {'ActualSpeed': 1470.0, 'Current': 3.2, 'Temp': 41, 'Fault': 0, 'Counter': 0}
status frames=165 period ms: mean=21.84 stdev=21.22 min=1.81 max=130.79
```

출력에서 볼 것:

- 시뮬레이터가 TargetSpeed 1500 × 0.98 = 1470 rpm으로 답했고, MAIN이 DBC로 다시 풀었다.
- 명령은 10 ms 주기인데 응답 간격은 평균 21.8 ms, 최대 131 ms다. 명령 쪽은 `send_periodic`이 python-can의 socketcan 백엔드에서 **커널 BCM 소켓의 `TX_SETUP`**으로 구현되어 커널 hrtimer가 보낸다(I10 5.6절). 반면 응답은 Python Notifier 스레드가 TCG 위에서 GIL과 싸우며 만든다. 같은 프레임 수(1초 명령 약 100개)에서 상태 프레임이 165개인 것은 로그 기록 시간 전체(약 3.6초)를 센 결과다. **주기 정확도가 필요한 송신은 BCM(커널)에, 판단 로직은 유저 공간에** — 실무 규칙 그대로다.
- `task.modify_data()`는 BCM의 `TX_SETUP` 갱신으로, 주기를 깨지 않고 롤링 카운터만 바꾼다. 모터 컨트롤러의 "명령 카운터가 안 바뀌면 통신 끊김으로 판단" 같은 안전 기능과 맞물리는 패턴이다.

### 7.3 예제 13 — 로그를 DBC로 풀기, 인코드 검증

```sh
L=$(ls -t candump-*.log | head -1); echo "log lines: $(wc -l < $L)"; head -4 $L
python3 -m cantools decode /mnt/q4/q4_robot.dbc < $L | head -12
tail -1 $L | python3 -m cantools decode --single-line /mnt/q4/q4_robot.dbc
python3 - <<'PY'
import cantools
db = cantools.database.load_file("/mnt/q4/q4_robot.dbc")
m = db.get_message_by_name("BMS_Pack")
print(hex(m.frame_id), m.is_extended_frame, m.encode({"PackVoltage": 200.0, "PackCurrent": -10.0, "SoC": 90}).hex())
c = db.get_message_by_name("MotorCmd")
print(c.encode({"TargetSpeed": -12.5, "TorqueLimit": 20, "Mode": "Torque", "Counter": 7}).hex())
try: c.encode({"TargetSpeed": 5000, "TorqueLimit": 20, "Mode": 1, "Counter": 0})
except Exception as e: print(type(e).__name__, e)
PY
```

```text
log lines: 496
(1792342135.211618) q4vc0 200#983A280100000000
(1792342135.228007) q4vc0 201#6C39400151000000
(1792342135.229941) q4vc0 200#983A280100000000
(1792342135.244071) q4vc0 200#983A280100000001
(1792342135.211618) q4vc0 200#983A280100000000 ::
MotorCmd(
    TargetSpeed: 1500.0 rpm,
    TorqueLimit: 20.0 Nm,
    Mode: Speed,
    Counter: 0
)
(1792342135.228007) q4vc0 201#6C39400151000000 ::
MotorStatus(
    ActualSpeed: 1470.0 rpm,
    Current: 3.2 A,
    Temp: 41 degC,
error: [Errno 32] Broken pipe
(1792342139.704486) q4vc0 18FF50E5#4E20FF9CB4000000 :: BMS_Pack(PackVoltage: 200.0 V, PackCurrent: -10.0 A, SoC: 90.0 %)
0x18ff50e5 True 4e20ff9cb4000000
83ff280200000007
EncodeError Expected signal "TargetSpeed" value smaller than or equal to 3000 in message "MotorCmd", but got 5000.
```

(`Broken pipe`는 `head -12`가 파이프를 먼저 닫아서 난 것이다.) 손으로 검산하자 — DBC 디버깅은 결국 이 작업이다:

- `200#983A2801…`: TargetSpeed raw = 0x3A98(리틀 엔디언: 바이트 0 = 0x98, 바이트 1 = 0x3A) = 15000 → × 0.1 = 1500 rpm. TorqueLimit 0x28 = 40 × 0.5 = 20 Nm. Mode = 1 = "Speed". 바이트 7의 하위 4비트가 롤링 카운터(0 → 1)다.
- `201#6C3940015100…`: ActualSpeed 0x396C = 14700 → 1470 rpm. Current는 비트 16부터 12비트: 바이트 2 = 0x40, 바이트 3 하위 니블 = 0x1 → 0x140 = 320 × 0.01 = 3.2 A. Temp 0x51 = 81 − 40 = 41 °C.
- BMS(Motorola): PackVoltage 200.0 / 0.01 = 20000 = 0x4E20 → **바이트 0 = 0x4E, 바이트 1 = 0x20**(big-endian). PackCurrent −10.0 / 0.1 = −100 = 0xFF9C. SoC 90 / 0.5 = 180 = 0xB4. 같은 숫자라도 Intel이었다면 `20 4E`였다.
- 범위를 넘는 값은 `EncodeError`. 송신 쪽 코드에 DBC 인코더를 쓰면 이런 실수를 런타임에 잡는다(디코드는 기본적으로 범위를 검사하지 않으니 수신 쪽은 따로 검증해야 한다).
- 그 밖의 cantools 기능(실행 안 함): `cantools monitor`(curses 실시간 화면), `cantools plot`(matplotlib 그래프), `cantools generate_c_source`(MCU용 C 인코더/디코더 생성 — 펌웨어 팀과 같은 DBC를 공유하는 방법).

---

## 8. slcan + 외부 MCU — 그리고 랩에서 에러 상태 만들기

### 8.1 slcan이란

slcan은 시리얼(UART/USB-CDC)로 CAN 프레임을 **ASCII 텍스트**로 주고받는 Lawicel 프로토콜이다: `t1232AABB\r` = "11-bit ID 0x123, 길이 2, 데이터 AA BB". 값싼 USB-CAN 동글(CANable 계열 등)과 많은 MCU 브리지가 이 프로토콜을 쓴다. Linux 쪽은 **tty line discipline**(`N_SLCAN`)으로 시리얼 포트 위에 `slcan0` 네트워크 인터페이스를 만든다 — H8에서 본 "tty 위에 프로토콜을 얹는" 구조다.

6.x 커널의 slcan(`drivers/net/can/slcan/slcan-core.c`)은 프레임 외에 두 가지 메시지를 더 이해한다:

- `e<길이><종류…>`: 버스 에러. `a` ACK, `b`/`B` bit0/bit1, `c` CRC, `f` form, `s` stuff, `o`/`O` RX/TX overrun → `slcan_bump_err()`가 `CAN_ERR_PROT | CAN_ERR_BUSERROR` 에러 프레임을 만든다.
- `s<상태><REC 3자리><TEC 3자리>`: 상태 변화. `a` active, `w` warning, `p` passive, `b` bus-off → `slcan_bump_state()`가 **`can_change_state()`와 `can_bus_off()`를 호출**한다.

즉 MCU 쪽이 에러를 보고하면, Linux는 진짜 컨트롤러 드라이버와 **같은 can_dev 경로**로 에러 상태를 처리한다. vcan·QEMU can-bus로는 못 만드는 에러 상태를 이 경로로 재현할 수 있다.

### 8.2 MCU 펌웨어 `q4_mcu.c` (Cortex-M3, QEMU mps2-an385)

무엇을 확인하는 코드인가: UART를 폴링으로 읽어 slcan 명령(`S6`, `O`, `C`, `V`, `t…`)을 처리하고, SysTick 10 ms로 100 ms마다 하트비트 0x100을 보내고, 0x200 명령에 0x201로 답하고, 0x7EE를 받으면 에러·상태 메시지를 "주입"하는 bare-metal 펌웨어. Don에게 익숙한 레지스터 직접 접근 코드다. (62줄이라 두 블록으로 나눴다.)

```c
/* q4_mcu.c - bare-metal "motor node + slcan adapter" on QEMU mps2-an385 (Cortex-M3) */
#include <stdint.h>
#define U0 ((volatile uint32_t *)0x40004000)   /* slcan link: DATA STATE CTRL INT BAUDDIV */
#define U1 ((volatile uint32_t *)0x40005000)   /* debug log */
#define SYST ((volatile uint32_t *)0xE000E010) /* SysTick CSR RVR CVR */
static volatile uint32_t ticks;                /* .bss: QEMU RAM starts zeroed (real HW: clear it) */
static int is_open; static uint8_t hb; static uint32_t recover_at;
static const char hx[] = "0123456789ABCDEF";
void systick_isr(void) { ticks++; }
static void putc_(volatile uint32_t *u, char c) { while (u[1] & 1) ; u[0] = (uint8_t)c; }
static void puts_(volatile uint32_t *u, const char *s) { while (*s) putc_(u, *s++); }
static int hv(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static void frame(unsigned id, int len, const uint8_t *d)          /* -> "tIIILDD..\r" */
{
	char b[24]; int n = 0;
	b[n++] = 't'; b[n++] = hx[(id >> 8) & 7]; b[n++] = hx[(id >> 4) & 15]; b[n++] = hx[id & 15];
	b[n++] = (char)('0' + len);
	for (int i = 0; i < len; i++) { b[n++] = hx[d[i] >> 4]; b[n++] = hx[d[i] & 15]; }
	b[n++] = '\r'; b[n] = 0; puts_(U0, b);
}
static void rx_frame(const char *l)                                /* l = "tIIILDD.." */
{
	unsigned id = (unsigned)(hv(l[1]) << 8 | hv(l[2]) << 4 | hv(l[3])); int len = l[4] - '0';
	uint8_t d[8];
	for (int i = 0; i < len && i < 8; i++) d[i] = (uint8_t)(hv(l[5 + 2 * i]) << 4 | hv(l[6 + 2 * i]));
	puts_(U0, "z\r");                                              /* Lawicel: TX ok */
	if (id == 0x200) { d[0] ^= 0xFF; frame(0x201, len, d); }       /* "motor" answers */
	if (id == 0x7EE && len > 0) {                                  /* lab fault injection */
		static const char *const inj[] = { "sa000000\r", "e1a\r", "sp000128\r", "sb000256\r" };
		puts_(U0, inj[d[0] & 3]); puts_(U1, "inject\n");
		if ((d[0] & 3) == 3) recover_at = ticks + 100;             /* auto "recovery" 1 s later */
	}
}
```

```c
/* q4_mcu.c (2/2) */
static void cmd(const char *l)
{
	puts_(U1, "cmd: "); puts_(U1, l); puts_(U1, "\n");
	switch (l[0]) {
	case 'S': case 'C': is_open = 0; putc_(U0, '\r'); break;
	case 'O': is_open = 1; putc_(U0, '\r'); break;
	case 'V': puts_(U0, "V1013\r"); break;
	case 't': if (is_open) rx_frame(l); else putc_(U0, '\a'); break;
	default: putc_(U0, '\a');                                      /* BEL = error */
	}
}
void reset_isr(void)
{
	char line[40]; int n = 0; uint32_t next = 10;
	U0[4] = 16; U0[2] = 3; U1[4] = 16; U1[2] = 1;                 /* baud div, TX/RX enable */
	SYST[1] = 250000 - 1; SYST[2] = 0; SYST[0] = 7;               /* 10 ms tick @ 25 MHz */
	for (;;) {
		if (U0[1] & 2) {                                           /* RX buffer full */
			char c = (char)U0[0];
			if (c == '\r') { line[n] = 0; if (n) cmd(line); n = 0; } else if (n < 39) line[n++] = c;
		}
		if (recover_at && (int32_t)(ticks - recover_at) >= 0) { puts_(U0, "sa000000\r"); recover_at = 0; }
		if (is_open && (int32_t)(ticks - next) >= 0) {             /* 100 ms heartbeat 0x100 */
			uint8_t d[2] = { hb++, 0xA5 }; next = ticks + 10; frame(0x100, 2, d);
		}
	}
}
__attribute__((section(".vectors"), used)) static void (*const vec[16])(void) = {
	(void (*)(void))0x20010000, reset_isr, [15] = systick_isr };
```

빌드(VM 호스트, 경고 0개)와 크기:

```sh
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -Os -Wall -Wextra -ffreestanding -nostdlib -T q4_mcu.ld q4_mcu.c -o q4_mcu.elf
arm-none-eabi-size q4_mcu.elf
```

```text
   text	   data	    bss	    dec	    hex	filename
    852	      0	     13	    865	    361	q4_mcu.elf
```

링커 스크립트는 FLASH(0x0)에 `.vectors`·`.text`·`.rodata`, RAM(0x20000000)에 `.bss`만 둔 4줄짜리다. `.data`를 일부러 안 만들었고(초기값 있는 전역 변수 없음), `.bss`는 QEMU가 RAM을 0으로 시작해서 지우지 않았다 — 실보드라면 리셋 핸들러에서 지워야 한다.

두 QEMU는 unix 소켓으로 묶었다: Linux 게스트는 `-chardev socket,id=mcu,path=…/q4_link.sock,server=on,wait=off -device pci-serial,chardev=mcu`(서버), MCU는 `-chardev socket,id=s0,path=…/q4_link.sock,reconnect-ms=1000 -serial chardev:s0`(재접속하는 클라이언트). 처음에는 반대로(MCU가 서버) 했는데, 작업 중 MCU QEMU 프로세스가 외부 요인으로 죽자 Linux 쪽 chardev가 끊긴 채 복구되지 않았다. **"재부팅이 잦은 쪽을 재접속 클라이언트로"** — 실제 SoC ↔ MCU 링크 설계(O3)에서도 같은 원칙이다.

### 8.3 예제 14 — slcand로 붙이고, MCU와 말하기

```sh
modprobe slcan; T=/dev/ttyS0; stty -F $T raw -echo 115200
slcand -o -c -s6 $T q4sl0; sleep 1; ip link set q4sl0 up
ip -details link show q4sl0
candump -t d -n 4 q4sl0
candump -t d -n 2 "q4sl0,201:7FF" & sleep 0.3
cansend q4sl0 200#0F1E; sleep 0.5; cansend q4sl0 200#AA55
cat /var/tmp/Q4/q4_mcu.log          # (VM 호스트) MCU 디버그 UART
```

```text
3: q4sl0: <NOARP,UP,LOWER_UP> mtu 16 qdisc pfifo_fast state UP mode DEFAULT group default qlen 10
    link/can  promiscuity 0  allmulti 0 minmtu 16 maxmtu 16 
    can state ERROR-ACTIVE restart-ms 0 
	  bitrate 0
	     [   10000,    20000,    50000,   100000,   125000,   250000, 
	        500000,   800000,  1000000 ]
	  clock 0 numtxqueues 1 numrxqueues 1 ...
 (000.000000)  q4sl0  100   [2]  16 A5
 (000.096421)  q4sl0  100   [2]  17 A5
 (000.102925)  q4sl0  100   [2]  18 A5
 (000.100346)  q4sl0  100   [2]  19 A5
 (000.000000)  q4sl0  201   [2]  F0 1E
 (000.756553)  q4sl0  201   [2]  55 55
q4_mcu up
cmd: C
cmd: S6
cmd: O
cmd: t20020F1E
cmd: t2002AA55
```

(MCU 로그는 첫 번째 펌웨어 버전의 실행에서 가져왔다 — 그 버전은 부팅 문구를 찍었다.) 출력에서 볼 것:

- `slcand -o -c -s6`: slcand가 직접 `C\r`(닫기), `S6\r`(500 kbit/s — S0..S8이 10 k..1 M에 대응), `O\r`(열기)를 보내고 line discipline을 붙인 뒤 인터페이스 이름을 `q4sl0`으로 바꿨다. MCU 로그가 정확히 그 순서다.
- `ip -details`의 `bitrate 0`과 `[10000, … 1000000]`: slcand가 bitrate를 시리얼로 설정했으니 커널은 모른다. 대신 slcan은 **`bitrate_const`**(고정 bitrate 목록)를 등록해서, 커널이 bitrate를 설정하는 방식도 지원한다(8.5절).
- 하트비트 간격 약 100 ms(SysTick 10 ms × 10), 0x200 명령에 바이트 0이 반전된 0x201 응답. 이게 "Linux SoC + 외부 MCU"의 최소 형태다.

### 8.4 예제 15 — 에러 주입: ACK 에러 → passive → bus-off → "복구"

무엇을 확인하나: MCU가 `e1a`(ACK 에러), `sp000128`(passive, TEC 128), `sb000256`(bus-off)을 차례로 보고하고, 1초 뒤 스스로 `sa000000`(active)을 보낼 때 Linux의 상태·통계·에러 프레임·송신 가능 여부.

```sh
$B/q4_rxts q4sl0 4 7FF:7FF > /tmp/s3.txt & R=$!
candump -t d -e -n 3 "q4sl0,0~0,#FFFFFFFF" > /tmp/s5.txt &     # 에러 프레임만 (별도 실행)
for v in 01 02 03; do cansend q4sl0 7EE#$v; sleep 0.5; done
ip -details -statistics link show q4sl0 | grep -E "q4sl0|state|bus-off|^ +[0-9]"
sleep 1.5; wait $R; cat /tmp/s3.txt
ip -details -statistics link show q4sl0 | grep -E "q4sl0|state"
cansend q4sl0 200#01; echo "send rc=$?"; ip -s link show q4sl0 | tail -2
ip link set q4sl0 down; ip link set q4sl0 up; ip -details link show q4sl0 | grep -E "q4sl0|state"
cansend q4sl0 200#02      # candump -n 1 "q4sl0,201:7FF" 로 응답 확인
dmesg | grep -i q4sl0 | tail -2
```

```text
5: q4sl0: <NO-CARRIER,NOARP,UP> mtu 16 qdisc pfifo_fast state DOWN mode DEFAULT group default qlen 10
    can state BUS-OFF restart-ms 0 
	  re-started bus-errors arbit-lost error-warn error-pass bus-off
            72      36      0       0       0       0 
             3       3      1       0       0       0 
ERR class=0x0a8 ctrl=0x00 prot=0x00 tec=0 rec=0
ERR class=0x204 ctrl=0x20 prot=0x00 tec=128 rec=0
ERR class=0x040 ctrl=0x00 prot=0x00 tec=0 rec=0
ERR class=0x204 ctrl=0x40 prot=0x00 tec=0 rec=0
5: q4sl0: <NO-CARRIER,NOARP,UP> mtu 16 qdisc pfifo_fast state DOWN mode DEFAULT group default qlen 10
    can state ERROR-ACTIVE restart-ms 0 
send rc=0
    TX:  bytes packets errors dropped carrier collsns           
             3       3      1       1       0       0 
5: q4sl0: <NOARP,UP,LOWER_UP> mtu 16 qdisc pfifo_fast state UP mode DEFAULT group default qlen 10
    can state ERROR-ACTIVE restart-ms 0 
  q4sl0  201   [1]  FD
[  139.477181] q4sl0: renamed from can0
[  143.670767] q4sl0: bus-off
```

(첫 `ip` 출력의 grep이 통계 6칸의 값 줄을 놓쳤다 — 위 두 숫자 줄은 RX/TX 패킷 통계다. 6칸 값은 앞선 실행에서 `0 0 0 0 1 1` = error-pass 1, bus-off 1이었다.) 에러 프레임 4개를 `linux/can/error.h`로 풀면:

| class (`can_id`) | 데이터 | 뜻 | 커널에서 만든 곳 |
|---|---|---|---|
| `0x0a8` = PROT 0x08 + ACK 0x20 + BUSERROR 0x80 | data[3] = 0x19 (ACK slot) | 송신했는데 아무도 ACK 안 함 | `slcan_bump_err()` |
| `0x204` = CRTL 0x04 + CNT 0x200 | data[1] = 0x20 TX_PASSIVE, data[6] = TEC 128 | error-passive 진입 | `can_change_state()` |
| `0x040` = BUSOFF | — | bus-off | `can_change_state()` + `can_bus_off()` |
| `0x204` | data[1] = 0x40 ACTIVE | 다시 error-active | `can_change_state()` |

출력에서 볼 것 — 이 실험의 핵심:

- bus-off 순간 커널이 `netif_carrier_off()`를 불러 `NO-CARRIER … state DOWN`이 됐다(`dmesg: q4sl0: bus-off`). `restart-ms 0`이라 자동 재시작 타이머는 없다.
- MCU가 1초 뒤 `sa000000`을 보내자 can 상태는 **ERROR-ACTIVE로 돌아왔는데 carrier는 여전히 꺼져 있다.** `slcan_bump_state()`는 상태만 바꾸고 `netif_carrier_on()`을 부르지 않기 때문이다. 이때 `cansend`는 **rc=0(성공처럼 보임)인데 프레임은 버려졌다**(TX `dropped 1`, MCU 로그에 `t200101`이 없음). 앱이 write 성공만 믿으면 "보냈는데 모터가 안 움직인다"가 된다.
- `ip link set down/up`(= `slcan_netdev_open()`이 상태를 ACTIVE로 두고 큐를 다시 시작)한 뒤에야 0x200 → 0x201 응답이 돌아왔다.
- 자동 복구를 켜려고 하면:

```text
+ ip link set q4sl0 type can restart-ms 100
Error: Device doesn't support restart from Bus Off.
+ ip link set q4sl0 type can restart
Error: Device doesn't support restart from Bus Off.
```

slcan에는 `priv->do_set_mode`가 없어서 `drivers/net/can/dev/netlink.c`가 `restart-ms ≠ 0`을 `-EOPNOTSUPP`로 막는다. **"bus-off 복구가 되는지"는 드라이버마다 다르다** — 새 어댑터·컨트롤러를 고를 때 확인할 항목이다.

- 툴 버전 함정 하나 더: 같은 에러 프레임을 can-utils 2023.03의 `candump -e`로 보면 `0x204`에서 `Error class 0x204 is invalid`라는 경고를 냈다. 상태 프레임에 카운터(`CAN_ERR_CNT`, 0x200)를 같이 싣는 것은 비교적 최근 커널 동작이라 오래된 candump 디코더가 모른다. 원시 값은 그대로 찍히니 표를 보고 직접 풀면 된다.

```text
 (000.000000)  q4sl0  200000A8   [8]  00 00 00 19 00 00 00 00   ERRORFRAME
	protocol-violation{{}{acknowledge-slot}}
	no-acknowledgement-on-tx
	bus-error
 (000.726228)  q4sl0  20000204   [8]  00 20 00 00 00 00 80 00   ERRORFRAME
	20000204   [8]  00 20 00 00 00 00 80 00   ERRORFRAME
 (000.715408)  q4sl0  20000040   [8]  00 00 00 00 00 00 00 00   ERRORFRAME
	bus-off
```

(`candump -e "q4sl0,0~0,#FFFFFFFF"` — `0~0`은 "모든 ID의 반대" = 데이터 프레임은 하나도 안 받는다는 뜻이고, `#FFFFFFFF`가 모든 에러 클래스다. 이 조합이 "에러만 보기"다. `,#FFFFFFFF`만 주면 데이터 프레임도 다 들어온다 — 처음에 그렇게 해서 하트비트만 잔뜩 찍혔다.)

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">에러 상태와 커널 경로 (drivers/net/can/dev/dev.c)</text><rect x="10" y="40" width="150" height="50" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="85.0" y="61.7" font-size="12" text-anchor="middle">ERROR-ACTIVE</text><text x="85.0" y="76.7" font-size="12" text-anchor="middle">TEC,REC &lt; 96</text><rect x="180" y="40" width="150" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="255.0" y="61.7" font-size="12" text-anchor="middle">ERROR-WARNING</text><text x="255.0" y="76.7" font-size="12" text-anchor="middle">≥ 96</text><rect x="350" y="40" width="150" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="425.0" y="61.7" font-size="12" text-anchor="middle">ERROR-PASSIVE</text><text x="425.0" y="76.7" font-size="12" text-anchor="middle">≥ 128</text><rect x="520" y="40" width="150" height="50" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="595.0" y="61.7" font-size="12" text-anchor="middle">BUS-OFF</text><text x="595.0" y="76.7" font-size="12" text-anchor="middle">TEC ≥ 256</text><line x1="160" y1="65" x2="178" y2="65" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/><line x1="330" y1="65" x2="348" y2="65" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/><line x1="500" y1="65" x2="518" y2="65" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/><rect x="370" y="112" width="300" height="70" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="520.0" y="136.2" font-size="12" text-anchor="middle">can_bus_off(dev)</text><text x="520.0" y="151.2" font-size="12" text-anchor="middle">netif_carrier_off → qdisc가 TX 버림</text><text x="520.0" y="166.2" font-size="12" text-anchor="middle">restart_ms &gt; 0 이면 delayed_work 예약</text><line x1="595" y1="90" x2="595" y2="110" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/><rect x="10" y="112" width="300" height="70" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160.0" y="136.2" font-size="12" text-anchor="middle">can_restart(dev)  (restart_ms 후)</text><text x="160.0" y="151.2" font-size="12" text-anchor="middle">echo skb 비움 · CAN_ERR_RESTARTED 프레임</text><text x="160.0" y="166.2" font-size="12" text-anchor="middle">carrier_on · do_set_mode(START)</text><line x1="368" y1="147" x2="312" y2="147" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/><line x1="85" y1="112" x2="85" y2="92" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/><text x="10.0" y="206.0" font-size="12" text-anchor="start">상태가 바뀔 때마다 드라이버 IRQ → can_change_state(dev, cf, tx_state, rx_state)</text><text x="10.0" y="224.0" font-size="12" text-anchor="start">→ 에러 프레임(CAN_ERR_CRTL + data[1], CAN_ERR_CNT + data[6..7]) + 통계 error-warn/pass++</text><text x="10.0" y="262.0" font-size="12" text-anchor="start">수동 복구: ip link set can0 type can restart → can_restart_now()</text><text x="10.0" y="280.0" font-size="12" text-anchor="start">  restart_ms가 0이고 상태가 BUS-OFF일 때만 허용 (아니면 -EINVAL / -EBUSY)</text><text x="10.0" y="306.0" font-size="12" text-anchor="start" font-weight="bold">slcan처럼 do_set_mode가 없는 드라이버:</text><text x="10.0" y="324.0" font-size="12" text-anchor="start">  restart-ms 설정 자체가 -EOPNOTSUPP → 어댑터가 "sa"를 보내 상태는 ACTIVE가 돼도</text><text x="10.0" y="342.0" font-size="12" text-anchor="start">  carrier는 꺼진 채 → 앱이 ip link down/up으로 다시 열어야 한다 (8절 실험)</text><text x="10.0" y="368.0" font-size="12" text-anchor="start">HW 복구 규칙: bus-off 노드는 recessive 11비트 × 128번을 본 뒤에야 버스로 돌아온다</text></svg>
```

그림 6 — 에러 상태 전이와 커널 경로. 컨트롤러 드라이버(또는 slcan의 `s` 메시지)가 상태 변화를 보면 `can_change_state()`가 에러 프레임과 통계를 만들고, bus-off면 `can_bus_off()`가 carrier를 끄고 `restart_ms` 타이머를 건다. 타이머가 터지면 `can_restart()`가 `CAN_ERR_RESTARTED` 프레임을 올리고 `do_set_mode(CAN_MODE_START)`로 컨트롤러를 다시 켠다.

### 8.5 예제 16 — 커널이 bitrate를 설정하게 하기

slcand 없이 line discipline만 붙이고(`slcand <tty>`에 `-s`·`-o`를 안 줌), bitrate는 `ip`로 준다.

```sh
slcand /dev/ttyS0 q4sl1; sleep 1
ip link set q4sl1 type can bitrate 500000; ip link set q4sl1 up
ip -details link show q4sl1 | grep -E "bitrate|can "
ip link set q4sl1 down; ip link set q4sl1 type can bitrate 333333; echo "odd bitrate rc=$?"
```

```text
    can state ERROR-ACTIVE restart-ms 0 
	  bitrate 500000
Error: bitrate 0 bps not supported.
odd bitrate rc=2
```

MCU 로그에는 `cmd: C`, `cmd: S6`, `cmd: O`가 다시 찍혔다 — 이번에는 커널 `slcan_netdev_open()`이 `"C\rS%d\r"`과 `"O\r"`을 보낸 것이다(`S6` = `slcan_bitrate_const[]`의 인덱스 6 = 500000). 목록에 없는 333333은 거부됐다. 에러 문구의 "0 bps"는 iproute2/커널 메시지 조합이 내놓은 값으로, 요청값이 아니다 — 메시지를 액면 그대로 믿지 말고 `bitrate_const` 목록을 보자.

---

## 9. 버스 부하 — 계산과 측정

### 9.1 왜 중요한가

CAN은 우선순위 중재라서 부하가 높아질수록 **낮은 우선순위(큰 ID) 메시지의 지연이 먼저 폭증**한다. 흔한 설계 기준은 평균 부하 30~50% 이하다(회사·표준마다 다르다 — 기억 기반, 프로젝트 규칙으로 확인). 부하 = (초당 비트 수) / bitrate이고, 프레임 하나의 비트 수는 **스터핑** 때문에 데이터 내용에 따라 달라진다.

- 11-bit, 8바이트, 스터핑 없음: SOF 1 + ID 11 + RTR/IDE/r0 3 + DLC 4 + 데이터 64 + CRC 15 + CRC del 1 + ACK 2 + EOF 7 + IFS 3 = **111비트**.
- 스터핑 최악: SOF~CRC 98비트 구간에서 최대 (98 − 1) / 4 = 24비트 추가 → 135비트.
- 500 kbit/s에서 111비트 = 222 µs → 최대 약 4500 프레임/s(100%).

### 9.2 예제 17 — 직접 계산(`q4_busload.py`) vs `canbusload`

무엇을 확인하는 코드인가: candump 로그의 각 프레임에 대해 실제 비트열(SOF~CRC)을 만들고 CRC-15를 계산한 뒤 5비트 규칙으로 스터핑 비트를 센다. canbusload의 세 모드(`-e` 정확, `-i` 무시, 기본 = 최악 가정)와 비교한다.

```python
"""q4_busload.py LOG BITRATE : exact classic-CAN bit count (stuffing + CRC15) from a candump -l log."""
import sys, collections

def crc15(bits):
    crc = 0
    for b in bits:
        nxt = b ^ ((crc >> 14) & 1)
        crc = (crc << 1) & 0x7FFF
        if nxt: crc ^= 0x4599
    return [(crc >> i) & 1 for i in range(14, -1, -1)]

def frame_bits(can_id, data, eff):
    b = lambda v, n: [(v >> i) & 1 for i in range(n - 1, -1, -1)]
    if eff:   # SOF, ID-A(11), SRR=1, IDE=1, ID-B(18), RTR, r1, r0, DLC
        s = [0] + b(can_id >> 18, 11) + [1, 1] + b(can_id & 0x3FFFF, 18) + [0, 0, 0] + b(len(data), 4)
    else:     # SOF, ID(11), RTR, IDE=0, r0, DLC
        s = [0] + b(can_id, 11) + [0, 0, 0] + b(len(data), 4)
    for byte in data: s += b(byte, 8)
    s += crc15(s)
    stuffed, run, last = 0, 0, None          # stuff bits are counted over SOF..CRC
    for bit in s:
        run = run + 1 if bit == last else 1
        last = bit
        if run == 5:
            stuffed += 1; last = 1 - bit; run = 1   # the inserted bit starts a new run
    return len(s) + stuffed + 3 + 7 + 3, stuffed    # + CRC del/ACK/ACK del, EOF, IFS

log, rate = sys.argv[1], int(sys.argv[2])
per_sec, frames, stuff_tot = collections.Counter(), 0, 0
for line in open(log):
    ts, _, msg = line.split()
    cid, payload = msg.split("#")
    data = bytes.fromhex(payload)
    nbits, st = frame_bits(int(cid, 16), data, len(cid) == 8)
    per_sec[int(float(ts.strip("()")))] += nbits; frames += 1; stuff_tot += st
for sec in sorted(per_sec)[1:-1]:            # drop partial first/last second
    print(f"t={sec}  bits={per_sec[sec]:7d}  load={100 * per_sec[sec] / rate:5.1f}%")
print(f"frames={frames}  avg stuff bits/frame={stuff_tot / frames:.2f}")
```

데이터에 따라 스터핑이 다르게 나오도록 네 종류를 1500개씩 흘리고, canbusload 세 개와 candump를 동시에 돌렸다.

```sh
for o in -e -i ""; do canbusload $o q4vc0@500000 > /tmp/bl2_${o:-w}.txt & done
candump -l q4vc0 & sleep 1.5
cangen q4vc0 -g 0 -I 0x0C0 -L 8 -D r -n 1500
cangen q4vc0 -g 0 -I 0x000 -L 8 -D 0000000000000000 -n 1500
cangen q4vc0 -g 0 -I 0x7FF -L 8 -D FFFFFFFFFFFFFFFF -n 1500
cangen q4vc0 -g 0 -e -I 18FF50E5 -L 8 -D r -n 1500
# ... 종료 후 awk로 canbusload 출력의 프레임 수·비트 수를 합산, q4_busload.py로 로그 분석
```

```text
canbusload -e: frames=5515 bits=694141
canbusload -i: frames=5811 bits=674541
canbusload -w: frames=5768 bits=813505
log frames: 5386
frames=5386  avg stuff bits/frame=9.82
id 0C0: frames=1312 bits/frame=114.8 stuff/frame=3.77
id 000: frames=1199 bits/frame=127.0 stuff/frame=16.00
id 7FF: frames=1474 bits/frame=126.0 stuff/frame=15.00
id 18FF50E5: frames=1401 bits/frame=135.8 stuff/frame=4.75
total bits: 678760
```

(위 명령은 실제 스크립트를 줄여 옮겼다. `-w`는 옵션 없는 기본 모드에 내가 붙인 이름이다.)

```svg
<svg viewBox="0 0 640 250" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="13" text-anchor="start" font-weight="bold">같은 로그(5천여 프레임)의 프레임당 비트 수 — 계산 방식별</text><text x="222.0" y="54.0" font-size="12" text-anchor="end">canbusload -i (스터핑 무시)</text><rect x="230" y="38" width="301.9" height="26" fill="#888" fill-opacity="0.55" stroke="#888"/><text x="537.9" y="56.0" font-size="12" text-anchor="start">116.1</text><text x="222.0" y="98.0" font-size="12" text-anchor="end">canbusload -e (정확 계산)</text><rect x="230" y="82" width="327.3" height="26" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="563.3" y="100.0" font-size="12" text-anchor="start">125.9</text><text x="222.0" y="142.0" font-size="12" text-anchor="end">q4_busload.py (직접 계산)</text><rect x="230" y="126" width="327.6" height="26" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="563.6" y="144.0" font-size="12" text-anchor="start">126.0</text><text x="222.0" y="186.0" font-size="12" text-anchor="end">canbusload 기본 (최악 가정)</text><rect x="230" y="170" width="366.6" height="26" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="602.6" y="188.0" font-size="12" text-anchor="start">141.0</text><line x1="230" y1="30" x2="230" y2="214" stroke="currentColor" stroke-width="1.3"/><line x1="230.0" y1="214" x2="230.0" y2="219" stroke="currentColor" stroke-width="1.3"/><text x="230.0" y="234.0" font-size="12" text-anchor="middle">0</text><line x1="360.0" y1="214" x2="360.0" y2="219" stroke="currentColor" stroke-width="1.3"/><text x="360.0" y="234.0" font-size="12" text-anchor="middle">50</text><line x1="490.0" y1="214" x2="490.0" y2="219" stroke="currentColor" stroke-width="1.3"/><text x="490.0" y="234.0" font-size="12" text-anchor="middle">100</text><line x1="620.0" y1="214" x2="620.0" y2="219" stroke="currentColor" stroke-width="1.3"/><text x="620.0" y="234.0" font-size="12" text-anchor="middle">150</text><text x="425.0" y="248.0" font-size="12" text-anchor="middle">비트/프레임 (8바이트 데이터, 11/29-bit 혼합)</text></svg>
```

그림 7 — 프레임당 평균 비트 수. 정확 계산(`-e`, 125.9)과 내 계산(126.0)이 0.1비트 차이로 맞고, 스터핑 무시(`-i`, 116.1)는 약 8% 과소, 기본 모드(141.0)는 약 12% 과대 평가다.

출력에서 볼 것:

- **같은 8바이트라도 데이터가 0x00이나 0xFF로 채워지면 스터핑 16·15비트**가 붙어 126~127비트가 된다. 랜덤 데이터는 3.8비트. 패딩을 0x00 대신 0xAA/0x55 같은 교번 패턴으로 하는 프로토콜이 있는 이유다(ISO-TP 패딩 기본값 0xCC도 같은 맥락 — 기억 기반, 표준 확인 필요).
- **측정 도구마다 받은 프레임 수가 다르다**(5386~5811, 보낸 것은 6000). `-g 0` 폭주에 수신 소켓 버퍼가 넘쳐 각 소켓이 서로 다른 프레임을 잃었다. 그래서 총합 대신 **프레임당 비트**로 비교했다. 실무 교훈: 부하 측정 도구 자체가 프레임을 잃을 수 있다. `candump -d`(드롭 감시, `SO_RXQ_OVFL`)로 확인하고, 소켓 수신 버퍼(`SO_RCVBUF`)를 키운다.
- canbusload 기본 모드는 "최악 스터핑"이라 보수적이다. 설계 단계 여유 계산에는 이게 맞고, 현장 실측 비교에는 `-e`가 맞다.

### 9.3 예제 18 — 진짜 컨트롤러 TX 큐: `ENOBUFS`

같은 폭주를 SJA1000(can0 → can1)에 걸면:

```sh
cangen can0 -g 0 -L 8 -n 20000 2>&1 | tail -1
ip link set can0 txqueuelen 1000
timeout 4 cangen can0 -g 0 -i -L 8 -D r          # 동시에 canbusload -e can1@500000
ip -s link show can0 | tail -4; ip -s link show can1 | tail -4   # can1 통계는 다음 스크립트 시작 때 읽었다
```

```text
write: No buffer space available
 can1@500000   652   74478  41728      0  14%
 can1@500000   968  110528  61952      0  22%
 can1@500000   854   97570  54656      0  19%
 can1@500000   478   54593  30592      0  10%
    RX:  bytes packets errors dropped  missed   mcast           
             0       0      0       0       0       0 
    TX:  bytes packets errors dropped carrier collsns           
         74835    9357      0       0       0       0 
    RX:  bytes packets errors dropped  missed   mcast           
         74835    9357      0       0       0       0 
    TX:  bytes packets errors dropped carrier collsns           
             0       0      0       0       0       0 
```

출력에서 볼 것:

- 진짜 CAN 장치는 `qdisc pfifo_fast qlen 10`이다(2.3절 출력). 컨트롤러가 한 번에 한 프레임만 보내므로(SJA1000은 TX 버퍼 1개) 큐 10개는 금방 차고, **`write()`가 `ENOBUFS`로 실패**한다. cangen은 기본으로 거기서 멈췄다(첫 줄). 앱은 `ENOBUFS`를 "잠시 후 재시도" 또는 "버린 프레임 카운트"로 처리해야 한다(4.1절 `q4_tx`). `txqueuelen`을 키우면 실패는 줄지만 큐에서 기다리는 시간(지연)이 늘어난다 — 제어 루프에는 오래된 명령이 늦게 나가는 것이 더 나쁠 수 있다.
- 4초 동안 9357프레임이 can0 → can1로 갔고 can1 드라이버는 다 받았지만(RX 9357), canbusload 소켓은 약 2950개만 봤다 — 9.2절과 같은 "측정 소켓의 드롭"이다. 그리고 TCG 에뮬레이터는 비트 시간을 흉내 내지 않으므로 이 % 값은 500 kbit/s 버스의 물리적 한계와 무관하다.

---

## 10. 실제 버스 진단 — 측정 장비와 함께 (가이드, 실행 안 함)

이 절은 랩에서 재현할 수 없는 물리 계층 이야기다. Don의 scope·로직 분석기 경험이 그대로 쓰이는 곳이다.

### 10.1 전원 끄고 저항부터

- CAN_H–CAN_L 사이 저항을 전원 끈 상태에서 잰다. **약 60 Ω**이면 양 끝 120 Ω 두 개가 정상. 120 Ω이면 종단 하나 빠짐, 40 Ω이면 세 개(중간 노드에 종단이 하나 더), 무한대면 둘 다 빠짐 또는 단선.
- 종단이 틀리면 반사로 엣지에 링잉이 생기고, 샘플 포인트 근처에 걸리면 간헐적 bit/form/CRC 에러 → 심하면 bus-off. 짧은 벤치 케이블에서는 멀쩡하다가 차량·로봇에 넣으면 터지는 전형적 문제다.
- 보드에 GPIO 종단 스위치가 있으면 DT `termination-gpios` + `ip link set can0 type can termination 120`(I10 8.5절).

### 10.2 전압 레벨 (기억 기반 — 트랜시버 데이터시트로 확인)

- recessive: CAN_H ≈ CAN_L ≈ 2.5 V (차동 약 0 V).
- dominant: CAN_H ≈ 3.5 V, CAN_L ≈ 1.5 V (차동 약 2 V). 수신기는 차동 0.9 V 이상을 dominant로 본다.
- 트랜시버의 STB/EN 핀이 standby면 수신만 되거나 아무것도 안 된다 — Linux에서는 "TX가 ACK 에러만 낸다"로 보인다. DT의 트랜시버 노드(`phys = <&can_transceiver>`, `drivers/phy/phy-can-transceiver.c`)가 이 핀을 제어하는지 확인한다.

### 10.3 스코프·로직 분석기로 디코드

- 차동 프로브로 CAN_H–CAN_L, 또는 트랜시버의 RXD 핀(로직 레벨)을 로직 분석기로. 프로토콜 디코더에 bitrate와 샘플 포인트를 맞춰 준다.
- 볼 것: 비트 폭(500 k = 2.000 µs ± 클럭 오차), ACK 슬롯이 dominant인가(아무도 ACK 안 하면 recessive), 에러 플래그(dominant 6비트 연속 = 일부러 스터핑 규칙 위반), 프레임 사이 IFS 3비트.
- 내 노드만 bitrate가 다르면 디코더에 내 프레임이 "깨진 프레임 + 에러 플래그"로 보인다.
- Linux 쪽과 시간 맞추기: 앱이 특정 이벤트 때 GPIO를 토글(M11)하고 그 핀을 같은 로직 분석기 채널로 잡으면, `SO_TIMESTAMPING` 값과 버스 파형을 같은 시간축에 놓을 수 있다.

### 10.4 증상 → 원인 표

| 증상 (`ip -details -s`, candump -e) | 가장 흔한 원인 | 확인 |
|---|---|---|
| TX가 ACK 에러만, error-passive에서 멈춤 | 상대 노드 없음/꺼짐, 트랜시버 standby, 케이블 빠짐 | 다른 노드 전원, STB 핀, 저항 측정 |
| bus-off 반복(`restart-ms`로 계속 재시작) | bitrate 불일치, CAN_H/L 단락, 트랜시버 고장 | 스코프 비트 폭, 저항, 트랜시버 교체 |
| 간헐 CRC/stuff/form 에러, 긴 버스에서만 | 종단 불량, 샘플 포인트 불일치, 스텁 길이 | 60 Ω 확인, 노드별 샘플 포인트 비교 |
| `arbit-lost` 증가 | 정상(부하 높을 때) 또는 같은 ID를 두 노드가 송신(설계 버그) | candump로 ID 중복 확인 |
| RX overflow 에러(`CAN_ERR_CRTL_RX_OVERFLOW`) | 컨트롤러 FIFO 넘침 — IRQ/NAPI 지연, SPI 컨트롤러(mcp251xfd)의 SPI 속도 | IRQ 스레드 우선순위(N2), SPI 클럭, rx-offload |
| 앱이 프레임을 잃음, 인터페이스 통계는 정상 | 소켓 수신 버퍼 넘침 | `candump -d`, `SO_RCVBUF`, `SO_RXQ_OVFL` |
| `write()`가 `ENOBUFS` | TX 큐(qlen 10) 가득 — 버스 부하 높음 또는 bus-off | `ip -s` TX dropped, 상태 확인 |

---

## 11. 커널 안에서는 무슨 일이 일어나나

이 노트에서 쓴 명령·API가 커널의 어디로 이어지는지 한 번에 정리한다(드라이버 내부는 I10 7·9절).

| 유저가 한 일 | 커널 경로 | 다시 만나는 곳 |
|---|---|---|
| `ip link set can0 type can bitrate …` | rtnetlink → `drivers/net/can/dev/netlink.c` `can_changelink()` → `can_calc_bittiming()` → 드라이버 `do_set_bittiming` | I10 4절, P3(데이터시트 → 레지스터) |
| `ip link set can0 up` | `ndo_open` → `open_candev()`(bitrate 설정 확인) → 컨트롤러 시작 | G 모듈 probe/open 구분 |
| `socket(PF_CAN, SOCK_RAW, CAN_RAW)` + `CAN_RAW_FILTER` | `net/can/raw.c` → `can_rx_register()` → `can_rcv_list_find()`가 `rx_sff`/`rx_eff`/`rx_fil`/`rx_inv`/`rx_err` 중 하나에 등록 | 4.4절, `/proc/net/can/rcvlist_*` |
| `write()` | `raw_sendmsg()` → `can_send()` → `dev_queue_xmit()`(qdisc) → 드라이버 `ndo_start_xmit`; `IFF_ECHO` 없으면 `can_send()`가 로컬 복사본을 `netif_rx()` | 5.3절 순서 함정, I6(네트워크 드라이버) |
| 수신 | 드라이버 IRQ → NAPI/`netif_rx()` → `can_rcv()` → `can_receive()` → 필터 리스트 순회 → 각 소켓 `raw_rcv()` → `sock_queue_rcv_skb()` (여기서 버퍼가 차면 drop) | F7(softirq), 9.2절 드롭 |
| `SO_TIMESTAMPING` | SW: `netif_rx`/수신 경로의 `net_timestamp_check()`가 `skb->tstamp`; HW: 드라이버가 `skb_hwtstamps(skb)`; vcan·vxcan TX는 `skb_tx_timestamp()` | Q3 |
| `send_periodic`(python-can), BCM | `net/can/bcm.c` `TX_SETUP` → hrtimer가 프레임 생성 | I10 5.6절 |
| ISO-TP 소켓 | `net/can/isotp.c` — FF/CF/FC 상태 머신 + hrtimer(STmin, N_Bs 타임아웃) | 5.3절 |
| J1939 소켓 | `net/can/j1939/` — `address-claim.c`, `transport.c`(TP/ETP 세션) | 6절 |
| `cangw -A` | `net/can/gw.c` — netlink 규칙 → `can_rx_register()`로 소스 인터페이스에 수신 훅, 수정 후 `can_send()` | 5.2절 |
| `slcand` | `TIOCSETD N_SLCAN` → `slcan-core.c` `slcan_open()`이 `alloc_candev()` + `register_candev()`; 수신 바이트는 `slcan_receive_buf()` → `slcan_unesc()` → `slcan_bump()` | H8(tty ldisc) |
| bus-off / restart | `can_change_state()`, `can_bus_off()`, `restart_work` → `can_restart()` → `do_set_mode()` | 8.4절, 그림 6 |

드라이버를 쓸 때 이 지식이 다시 나오는 곳: ① 에러 IRQ 핸들러에서 TEC/REC를 읽어 `can_change_state()`를 부르는 것은 드라이버 몫이다. 부르지 않으면 앱은 bus-off를 영영 모른다(I10 6.3절의 CTU CAN FD `STOPPED` 사례). ② `do_set_mode`를 구현해야 `restart-ms`가 켜진다(8.4절의 slcan 사례). ③ HW 타임스탬프를 `skb_hwtstamps()`에 넣고 `ethtool get_ts_info`로 광고해야 `SO_TIMESTAMPING`의 `ts[2]`가 채워진다.

---

## 12. Embedded 실무에서 다시 보기

### 12.1 로봇 메인 보드의 CAN 서비스 구성 예

예를 들어 AMR(창고 로봇)이라면:

- 부팅: systemd-networkd `[CAN]` 섹션으로 bitrate·`RestartSec`(= restart-ms) 설정(I10 10.2절). 컨트롤러가 `do_set_mode`를 지원하는지 먼저 확인(8.4절).
- 명령 송신: 모터 4개에 100 Hz 속도 명령 → BCM `TX_SETUP` 주기 송신 + `modify`로 값 갱신(7.2절). 앱이 죽으면 BCM 타이머도 소켓과 함께 사라진다 — 모터 쪽 펌웨어는 "명령 타임아웃 → 정지"를 반드시 구현한다(N4의 역할 분담).
- 상태 수신: `0xC00007FF` 마스크의 정확 필터들(4.4절) + 에러 마스크 `CAN_ERR_BUSOFF | CAN_ERR_CRTL | CAN_ERR_RESTARTED` + `SO_TIMESTAMPING`(4.2절).
- 로깅: `candump -l`을 상시 링버퍼로(파일 회전은 systemd 또는 logrotate), 사고 시점 ±30초를 잘라 보관. 분석은 사무실에서 `canplayer` + cantools(3.3, 7.3절).
- 진단·업데이트: 모터 펌웨어 업데이트는 UDS over ISO-TP(5.3절) — CAN FD면 수 배 빠르다. 업데이트 파일은 rootfs OTA 번들에 같이(L4).
- 테스트: CI에서 vxcan + 네임스페이스로 "ECU 시뮬레이터 컨테이너 ↔ 메인 앱"을 돌리고, 현장 로그를 canplayer로 재생해 회귀 테스트(P6).

### 12.2 bus-off 감시 코드 조각

8.4절 실험이 알려 준 규칙을 앱 코드로 옮기면(실행하지 않은 발췌 — 4.2절 `q4_rxts`의 에러 분기에 들어갈 내용):

```c
if (f.can_id & CAN_ERR_BUSOFF)       { bus_ok = 0; robot_safe_stop(); }      /* 모터에 명령이 안 간다 */
if (f.can_id & CAN_ERR_RESTARTED)    { bus_ok = 1; }                          /* restart-ms 경로 */
if ((f.can_id & CAN_ERR_CRTL) && (f.data[1] & (CAN_ERR_CRTL_TX_PASSIVE | CAN_ERR_CRTL_RX_PASSIVE)))
	log_warn("CAN error-passive: TEC=%u REC=%u", f.data[6], f.data[7]);
/* restart 지원이 없는 드라이버(slcan 등): ACTIVE로 돌아와도 carrier가 꺼져 있을 수 있다 →
   /sys/class/net/can0/carrier 확인 후 필요하면 down/up (rtnetlink) */
```

### 12.3 Linux ↔ MCU 링크를 slcan으로 할까?

8절처럼 MCU가 slcan을 말하면 Linux 쪽은 공짜로 SocketCAN 도구 전부를 쓴다. 대신 ASCII라 대역 효율이 낮고(프레임 하나에 최대 27바이트 + `\r`), 115200 baud UART면 초당 수백 프레임이 한계다. 고속이 필요하면 USB의 gs_usb(candleLight 펌웨어, 바이너리 프로토콜, 랩 커널에 `gs_usb` 모듈 있음)나 SPI CAN 컨트롤러(mcp251xfd)를 쓴다. 자세한 설계 비교는 O3.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 필터 `0x123:0x7FF`만 씀 | 29-bit 0x00000123도 들어와 오동작 | 마스크가 EFF 비트를 안 봄 | 마스크에 EFF·RTR 비트 포함(`0xC00007FF`) → 4.4절 |
| up 상태에서 bitrate 변경 | `RTNETLINK answers: Device or resource busy` | can_dev가 동작 중 변경 금지 | `down` → 설정 → `up` |
| vcan에 FD를 cansend | `CAN interface is not CAN FD capable` | 6.18 vcan MTU 2060, 구버전 cansend는 72만 FD로 인정 | `ip link set vcanX mtu 72` |
| `write()` 성공 = 전송 성공이라고 믿음 | 모터가 명령을 못 받음 | bus-off/no-carrier 중에도 write가 성공하고 qdisc가 버림 | 에러 프레임·carrier 감시, TX dropped 통계 |
| `ENOBUFS`에서 앱이 종료 | 부하 높을 때 앱 크래시 | TX 큐 10개 가득 | 재시도·드롭 카운트, 필요 시 `txqueuelen` (지연 트레이드오프) |
| `restart-ms`가 당연히 된다고 가정 | `Device doesn't support restart from Bus Off` | 드라이버에 `do_set_mode` 없음(slcan 등) | 드라이버 확인, 앱에서 down/up 복구 |
| 앱 수신 시각을 센서 시각으로 사용 | 퓨전 결과가 흔들림, 지터 ms 단위 | 스케줄링 지연 포함 | `SO_TIMESTAMPING`(SW 또는 HW) 사용 → 4.5절 |
| DBC Motorola 신호를 Intel처럼 해석 | 값이 터무니없음(200 V가 8270 V 등) | 바이트 순서·시작 비트 규칙 차이 | `cantools dump`로 레이아웃 확인 → 7.3절 |
| 종단 저항 미확인 | 벤치에선 되고 차량에선 간헐 에러/bus-off | 반사·링잉 | 전원 끄고 60 Ω 측정 → 10.1절 |
| candump TX/RX 순서를 버스 순서로 믿음 | 프로토콜 순서 위반으로 오진 | 로컬 echo 경로가 따로 들어옴 | 타임스탬프 비교, 별도 수신 노드로 확인 → 5.3절 |


---

## 14. 면접에서 이렇게 말한다

**Q.** "Walk me through how you'd bring up and validate a CAN interface on a new Linux board."

**A.** 장치 확인(`ip -details`, dmesg probe) → 클럭과 비트 타이밍(요청 bitrate·샘플 포인트와 실제 tq·세그먼트, 다른 노드와 일치) → 컨트롤러 내부 루프백 → 실제 버스(60 Ω, 트랜시버 모드) → candump로 상대 노드 확인 → 에러 카운터와 bus-off 복구 정책 확인 순서로 간다고 말한다.

> I start from the outside in. First `ip -details` and dmesg to make sure the controller probed with the right clock. Then I set bitrate and sample point and read back the actual tq and segments, and compare them with what the MCU nodes use — 87.5% at 500k is the usual target. I test the driver with the controller's internal loopback, then go to the real bus: power off, measure about 60 ohms between CAN_H and CAN_L, check the transceiver isn't in standby. Then candump to see the other nodes, and finally I watch the error counters and decide the bus-off recovery policy with restart-ms.

**꼬리 질문**: "restart-ms를 0으로 두는 게 나은 경우는?" → 안전상 사람이 원인을 확인한 뒤 재가동해야 하는 시스템(모터 폭주 위험). 자동 재시작이 bus-off 루프를 만들어 다른 노드 통신까지 방해할 수 있다.

**Q.** "What's the difference between candump's timestamp and the time your application reads the frame?"

**A.** candump와 내 앱 모두 커널이 skb에 찍은 타임스탬프를 받을 수 있다(`SO_TIMESTAMPING`). 앱의 read 시각은 거기에 스케줄링 지연이 더해진 것이고, 랩에서 평균 수 ms·최대 100 ms 넘게 차이 났다. HW 타임스탬프가 있는 컨트롤러면 IRQ·NAPI 지연까지 빠진다.

> The kernel timestamps the skb when it enters the stack — or the controller does it in hardware if the driver supports it — and I get that through SO_TIMESTAMPING. The time my read() returns includes scheduling latency on top. In my lab the gap averaged milliseconds and spiked past 100 ms under load. For sensor fusion I always use the socket timestamp, ideally the hardware one, never clock_gettime at read time.

**Q.** "Your CAN filter is `0x123` with mask `0x7FF`. Any problem?"

**A.** 마스크가 EFF/RTR 비트를 안 보니 29-bit ID 0x123과 RTR 프레임도 통과한다. 마스크에 `CAN_EFF_FLAG | CAN_RTR_FLAG`를 넣으면 정확히 11-bit 데이터 프레임만 받고, 커널의 단일 ID 해시 리스트(빠른 경로)도 쓴다.

> Yes — the mask only compares the low 11 bits, so an extended frame with ID 0x123 or an RTR frame also matches. I've reproduced that on vcan. I put CAN_EFF_FLAG and CAN_RTR_FLAG into the mask, which makes it an exact standard-frame match, and as a bonus the kernel puts it into the per-ID hash list instead of the linear mask list.

**Q.** "The bus goes bus-off every few minutes in the field. How do you debug it?"

**A.** 먼저 데이터: `ip -details -statistics`의 bus-errors·error-pass·bus-off 카운트와 `candump -e`의 에러 클래스(ACK/bit/stuff/form/CRC)를 로그로 남긴다. ACK면 상대 노드·트랜시버, bit 에러면 물리(단락·트랜시버), stuff/CRC/form이면 종단·샘플 포인트·노이즈. 그다음 스코프로 비트 폭·링잉·에러 플래그를 본다.

> I log the evidence first: the error counters and bus-off count from ip -statistics, and the error classes from candump -e, with timestamps so I can correlate with what the robot was doing. The class tells me where to look: ACK errors mean nobody is acknowledging — a node or transceiver is down; bit errors on transmit point to the physical layer, a short or a bad transceiver; stuff, CRC or form errors usually mean termination, sample-point mismatch or noise. Then I put a scope or logic analyzer on the bus and check bit width, ringing and where the error flags start.

**Q.** "How would you send a 300-byte firmware chunk over classic CAN?"

**A.** ISO-TP(ISO 15765-2). FF + CF + 수신자가 정하는 Flow Control(BS, STmin). Linux는 `CAN_ISOTP` 소켓이 커널에서 분할·타이머를 처리하므로 앱은 write 한 번. 랩에서 BS 4, STmin 5 ms로 54프레임, CAN FD에서는 6프레임이었다.

> With ISO-TP. The first frame carries the length, the receiver answers with flow control — block size and minimum separation time — and the sender streams consecutive frames with a 4-bit sequence number. On Linux the CAN_ISOTP socket does all of that in the kernel with hrtimers, so the application does a single write. In my lab 300 bytes took 54 frames with block size 4 and 5 ms STmin; with CAN FD it was 6 frames.

**Q.** "What's in a DBC file and how do you use it on Linux?"

**A.** 메시지 ID·길이·송신 노드, 신호의 시작 비트·길이·바이트 순서(Intel/Motorola)·부호·스케일·오프셋·범위·단위, 열거값. Linux에서는 cantools(Python)로 candump 로그를 디코드하고, 인코더로 범위 검사, `generate_c_source`로 MCU와 같은 정의 공유.

> It's the contract for the bus: for each message the ID, length and sender, and for each signal the start bit, length, byte order, sign, scale, offset, range and unit, plus value tables. On Linux I use cantools — decode candump logs into physical values, encode with range checks, and the same DBC can generate C code for the MCU side so both ends agree. The classic bug is a Motorola signal decoded as Intel.

**Q.** "How do you test CAN software without hardware?"

**A.** vcan(단일 가상 버스), vxcan(네임스페이스 사이 쌍 — 컨테이너 ECU 시뮬레이터), can-gw(라우팅·변형), canplayer(현장 로그 재생). 단 vcan에는 bitrate·에러 상태가 없어서, 에러 처리 테스트는 slcan 같은 경로로 에러를 주입하거나 HIL에서 한다.

> vcan for a single virtual bus, vxcan pairs to put an ECU simulator in its own network namespace or container, can-gw to route and rewrite between them, and canplayer to replay real field logs as regression tests. The limit is that vcan has no bit timing and no error states, so for error handling I inject errors — for example an MCU speaking slcan can report bus-off and the kernel runs the same can_dev path — or I test on a hardware-in-the-loop bench.

---

## 15. 직접 해보기

1. **개념**: 500 kbit/s 버스에 11-bit 8바이트 프레임을 1 kHz로 보내는 모터 4개와 같은 주기의 상태 4개가 있다. 스터핑 없는 경우와 최악의 경우 부하를 계산하라. 정답/힌트: 8000 프레임/s × 111비트 = 888 kbit/s → 177%, 최악 135비트면 216%. 한 버스로는 불가능 — 주기를 낮추거나 CAN FD·버스 분리.
2. **코드**: `q4_rxts`에 `SO_RXQ_OVFL`을 추가해 cmsg로 드롭 카운터를 출력하고, `cangen -g 0`으로 드롭을 만들어 보라. 정답/힌트: `setsockopt(SOL_SOCKET, SO_RXQ_OVFL, &one)`, cmsg 타입 `SO_RXQ_OVFL`, 값은 `__u32` 누적 드롭 수.
3. **코드**: DBC의 `MotorStatus.Fault`가 1이면 0x200 명령의 Mode를 "Off"로 바꿔 보내는 python-can 앱을 써라. 시뮬레이터가 10% 확률로 Fault를 내게 바꾸고 candump 로그를 cantools로 확인하라. 정답/힌트: `task.modify_data()`로 BCM 메시지만 바꾸면 주기가 유지된다.
4. **관찰**: `q4_rxts`를 필터 100개로 띄우고 `/proc/net/can/rcvlist_sff`와 `rcvlist_fil`에 무엇이 생기는지 비교하라(마스크 `7FF` vs `C00007FF`). 정답/힌트: 4.4절의 `can_rcv_list_find()` 조건. `matches` 열이 수신마다 증가하는 것도 보라.
5. **관찰**: ISO-TP 수신자의 STmin을 0, 5, 20 ms로 바꿔 300바이트 전송 시간(`time isotpsend`)을 재고 표로 만들어라. 정답/힌트: 대략 CF 수 × STmin. BS를 0으로 하면 FC가 한 번만 나간다.
6. **펌웨어**: `q4_mcu.c`에 "bus-off 뒤 recessive 128 × 11비트" 규칙을 흉내 내 `sb` 후 `sw`/`sp`를 거쳐 `sa`로 돌아오게 바꾸고, Linux의 에러 프레임 순서를 기록하라. 정답/힌트: `can_change_state()`는 같은 상태로 바뀌면 경고(`oops, state did not change`)를 남긴다 — dmesg 확인.
7. **설계**: 로봇 메인 보드에서 bus-off가 났을 때의 상태 머신(감지 → 안전 정지 → 원인 기록 → 복구 시도 → 재가동 조건)을 그리고, `restart-ms`를 쓸지 앱에서 down/up할지 결정 근거를 쓰라. 정답/힌트: 8.4절의 slcan 결과, 14절 꼬리 질문.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| SocketCAN | Linux CAN 프레임워크 | CAN 컨트롤러를 `net_device`로, 통신을 `PF_CAN` 소켓으로 다룬다 |
| vcan | virtual CAN | 자기에게 돌아오는 SW 버스, 비트 타이밍·에러 없음 |
| vxcan | virtual CAN tunnel | 한 쌍의 CAN 인터페이스, 네임스페이스 사이 연결 |
| can-gw | CAN gateway | 커널 안에서 인터페이스 사이 프레임 라우팅·수정 |
| slcan | serial line CAN | Lawicel ASCII 프로토콜, tty line discipline |
| tq | time quantum | 비트 타이밍의 최소 단위 = BRP / CAN 클럭 |
| 샘플 포인트 | sample point | 비트 안에서 값을 읽는 위치(%) |
| SJW | synchronization jump width | 재동기화 한 번에 조정 가능한 최대 tq |
| TEC / REC | transmit / receive error counter | 에러 상태를 결정하는 카운터 |
| error-passive | 에러 수동 상태 | TEC 또는 REC ≥ 128, recessive 에러 플래그만 냄 |
| bus-off | 버스 이탈 | TEC ≥ 256, 송수신 중단, 복구 절차 필요 |
| restart-ms | 자동 재시작 지연 | bus-off 후 can_dev가 컨트롤러를 다시 켜는 시간 |
| 에러 프레임 | `CAN_ERR_FLAG` 프레임 | 커널이 상태·에러를 앱에 알리는 메타 프레임 |
| BRS / ESI | bit rate switch / error state indicator | FD 데이터 구간 가속 / 송신자 passive 표시 |
| BCM | broadcast manager | 커널 주기 송신·변화 감지 소켓 |
| ISO-TP | ISO 15765-2 | 4095바이트(FD는 더 큼)까지 분할 전송, FF/CF/FC |
| BS / STmin | block size / separation time min | ISO-TP 흐름 제어 파라미터 |
| UDS | ISO 14229 진단 서비스 | ISO-TP 위의 진단·플래시 프로토콜 |
| J1939 | SAE J1939 | 29-bit ID에 PGN·SA를 싣는 상용차 프로토콜 |
| PGN | parameter group number | J1939 메시지 종류 번호 |
| Address Claim | 주소 클레임 | J1939 노드가 8비트 주소를 협상하는 절차 |
| TP.CM / TP.DT | transport protocol | J1939의 긴 메시지 분할(RTS/CTS/BAM) |
| DBC | CAN database | 신호 레이아웃·스케일·단위 정의 파일 |
| Intel / Motorola | little / big endian | DBC 신호 바이트 순서 |
| 비트 스터핑 | bit stuffing | 같은 값 5비트 뒤 반대 비트 삽입 |
| 종단 저항 | termination | 버스 양 끝 120 Ω, 합성 60 Ω |

---

## 17. 요약 & 체크리스트

CAN 실무는 "버스를 보고(candump), 기록하고(-l), 재생하고(canplayer), 의미로 풀고(DBC/cantools), 앱을 짜고(RAW 필터·BCM·타임스탬프·에러 마스크), 상위 프로토콜은 커널에 맡기는(ISO-TP·J1939·can-gw)" 일이다. 랩에서 확인한 함정들 — 11-bit 마스크로 29-bit가 새는 필터, 6.18 vcan의 MTU 2060과 구버전 cansend, 큐가 차면 `ENOBUFS`, bus-off 뒤 write가 성공한 척 버려지는 프레임, 드라이버마다 다른 `restart-ms` 지원, 측정 도구 자체의 드롭 — 는 모두 현장에서 그대로 나온다. 에러 상태는 vcan으로는 못 만들지만, can_dev 경로(`can_change_state` → `can_bus_off` → `can_restart`)를 이해하고 slcan 같은 경로로 주입하면 랩에서도 확인할 수 있다. 물리 계층(60 Ω, 샘플 포인트, 스코프 디코드)은 Don의 HW 디버그 경험이 그대로 통하는 영역이다.

- [ ] `ip -details -statistics link show can0` 출력의 모든 줄(상태, berr-counter, tq·세그먼트, 한계, clock, 통계 6칸)을 설명할 수 있다
- [ ] bitrate·샘플 포인트에서 tq·세그먼트를 손으로 계산하고 `can-calc-bit-timing`으로 검산할 수 있다
- [ ] candump 필터 문법(`id:mask`, `id~mask`, `#errmask`, `0~0`)으로 원하는 프레임·에러만 볼 수 있다
- [ ] `candump -l` → `canplayer` → `cantools decode`로 현장 로그를 재현·해석할 수 있다
- [ ] RAW 소켓에 필터(EFF/RTR 비트 포함)·FD·에러 마스크·`SO_TIMESTAMPING`을 거는 C 코드를 짤 수 있다
- [ ] 커널 타임스탬프와 앱 수신 시각의 차이를 측정하고 왜 생기는지 설명할 수 있다
- [ ] vxcan + 네트워크 네임스페이스 + can-gw로 ECU 시뮬레이터 구성을 만들 수 있다
- [ ] ISO-TP의 FF/CF/FC와 BS·STmin을 candump 출력에서 읽을 수 있다
- [ ] J1939 ID를 우선순위·PGN·SA로 풀고 주소 클레임·TP 흐름을 설명할 수 있다
- [ ] DBC 신호 정의(Intel/Motorola, 스케일·오프셋)를 읽고 바이트를 손으로 디코드할 수 있다
- [ ] 스터핑을 포함한 버스 부하를 계산하고 canbusload 모드 차이를 설명할 수 있다
- [ ] bus-off → 복구의 커널 경로와 `restart-ms`가 드라이버에 따라 안 될 수 있는 이유를 설명할 수 있다

## 참고 자료

- Linux kernel documentation, "SocketCAN - Controller Area Network": https://docs.kernel.org/networking/can.html
- Linux kernel documentation, "J1939 Documentation": https://docs.kernel.org/networking/j1939.html
- Linux kernel documentation, "ISO 15765-2 (ISO-TP)": https://docs.kernel.org/networking/iso15765-2.html
- 커널 소스(6.18.55, 랩 `/var/lab/src/linux-6.18.55`): `net/can/af_can.c`(`can_rcv_list_find`), `net/can/raw.c`, `net/can/gw.c`, `net/can/isotp.c`, `net/can/j1939/`, `drivers/net/can/dev/dev.c`(`can_change_state`, `can_bus_off`, `can_restart`), `drivers/net/can/dev/netlink.c`, `drivers/net/can/slcan/slcan-core.c`, `drivers/net/can/vcan.c`, `drivers/net/can/vxcan.c`, `include/uapi/linux/can/error.h` — elixir.bootlin.com에서 같은 경로로 볼 수 있다
- can-utils 저장소: https://github.com/linux-can/can-utils
- python-can 문서: https://python-can.readthedocs.io/
- cantools 문서: https://cantools.readthedocs.io/
- QEMU 문서, "CAN Bus Emulation Support": https://www.qemu.org/docs/master/system/devices/can.html
- 관련 노트: I10(CAN 서브시스템·드라이버), A9(소켓), H8(tty·UART), N2(실시간 튜닝), N4(Linux ↔ MCU 분담), O3(외부 MCU 통신 설계), Q3(타임스탬프·동기화), Q7(데이터 품질 툴), P6(테스트·CI)
