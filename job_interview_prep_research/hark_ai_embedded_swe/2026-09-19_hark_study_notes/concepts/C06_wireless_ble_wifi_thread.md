# C06. 무선 프로토콜 — BLE · Wi-Fi · Thread를 펌웨어 엔지니어 눈으로

> **이 노트를 다 읽으면**: BLE 스택 계층과 advertising/connection 타임라인을 그리고 전력·처리량을 계산할 수 있다 · Zephyr로 GATT peripheral을 만들고 한 줄씩 설명할 수 있다 · Wi-Fi 연결 과정(association, WPA2/3 4-way handshake)과 power save(DTIM, TWT)를 설명할 수 있다 · Thread/Matter 역할과 host–controller 분리, 2.4 GHz 공존(PTA)을 Apple 칩셋 통합 경험과 연결해 말할 수 있다
> **JD 연결**: "Familiarity with wireless protocols (BLE, Wi-Fi, Thread)"
> **Don 기준 난이도**: 라디오 칩을 호스트에 붙이는 HW/인터페이스(PCIe, UART, SPMI, RFFE)와 bring-up은 이미 안다 / 그 위를 흐르는 프로토콜 스택(Link Layer, GATT, SMP, 802.11 MLME, 6LoWPAN)과 파라미터가 전력에 미치는 영향은 새로 배운다

---

## 0. 큰 그림

Apple에서 Don이 본 세계는 "라디오 칩과 호스트 사이의 선"이었다: WLAN 데이터 경로의 PCIe, 전원을 주는 PMIC의 SPMI, 프런트엔드(PA/LNA/스위치)를 제어하는 RFFE. 이 노트는 그 선 **위로 흐르는 내용물**, 즉 프로토콜 스택을 다룬다.

예를 들어 Hark 같은 기기(추정: SoC + always-on MCU, Cellular·Wi-Fi·BT·GNSS·NFC·UWB)라면 무선은 이렇게 배치될 수 있다.

```
                         ┌───────────────── SoC (Cortex-A, Android) ─────────────────┐
                         │  Bluetooth host (Android BT 스택)             Wi-Fi driver  │
                         │  wpa_supplicant / 네트워크 스택                   Modem     │
                         └──────┬───────────────────────┬──────────────────┬────────┘
                         UART(H4)/PCIe               PCIe/SDIO          (내장/PCIe)
                                │                        │                  │
                     ┌──────────▼────────────────────────▼──┐        ┌──────▼──────┐
                     │  Wi-Fi + BT combo chip (controller/FW) │◀─coex─▶│ Cellular    │
                     │  2.4/5/6 GHz, 공유 안테나, 내부 PTA     │ WCI-2 │ (LTE/NR)    │
                     └──────────┬────────────────────────────┘        └─────────────┘
                                │ RFFE (PA/LNA/스위치/튜너 제어)
                             안테나

 always-on 쪽 (가능한 구성):
  ┌──────────────── MCU (예: nRF52/53급 또는 BLE 내장 MCU) ────────────────┐
  │ BLE host + controller (Zephyr), 폰과 저전력 연결 유지, provisioning     │
  └────────────────────────────────────────────────────────────────────────┘
```

펌웨어 엔지니어가 무선에서 실제로 하는 일은 대부분 **벤더 스택을 통합하고 설정하는 일**이다. 스택을 처음부터 짜지 않는다. 그래서 면접에서 보는 것은:

1. 스택의 계층과 각 계층의 책임을 아는가 (어디서 문제가 생겼는지 좁힐 수 있나)
2. 파라미터(interval, latency, MTU, DTIM)가 **전력·지연·처리량**에 어떻게 작용하는지 계산할 수 있는가
3. 보안(pairing, bonding, WPA3)과 provisioning 흐름을 설명할 수 있는가
4. 여러 라디오가 한 기기에서 같이 살 때(coexistence) 무슨 일이 생기는지 아는가

4번은 Don의 강점과 직접 닿는다.

---

## 1. 2.4 GHz 대역과 세 프로토콜 비교

### 1.1 한눈에 비교

| 항목 | BLE | Wi-Fi (802.11 b/g/n/ax) | Thread (802.15.4) |
|---|---|---|---|
| 주 용도 | 폰-액세서리, 센서, 오디오(LE Audio) | 고속 IP 연결, 클라우드 | 저전력 IP mesh (스마트홈) |
| 대역 | 2.4 GHz, 40채널 × 2 MHz | 2.4 / 5 / 6 GHz, 20~160 MHz 폭 | 2.4 GHz, 16채널(11~26) × 5 MHz 간격 |
| PHY 속도 | 1 Mbps, 2 Mbps, Coded 500/125 kbps | 수 Mbps ~ Gbps | 250 kbps |
| 토폴로지 | star (central-peripheral), broadcast | star (AP-STA) | mesh (router들이 다중 홉) |
| 네트워크 계층 | 없음(GATT 직접), IP는 선택 | IP (Ethernet 유사) | IPv6 over 6LoWPAN |
| 평균 전류 (연결 유지) | µA ~ 수십 µA | 수백 µA ~ mA (PS 모드) | Sleepy End Device는 µA |
| 펌웨어 복잡도 | 중간 | 높음(supplicant, TCP/IP, TLS) | 중간~높음(mesh, commissioning) |

### 1.2 2.4 GHz 채널 배치

```
 MHz  2400    2412      2437      2462      2480   2483.5
       │       │ Wi-Fi 1 │ Wi-Fi 6 │ Wi-Fi 11│        │
       │  ┌────┴────┐┌───┴─────┐┌──┴──────┐  │        │   (20 MHz 폭)
 BLE   │37▲         ▲38         │           ▲39      │   광고 채널 37(2402) 38(2426) 39(2480)
       │ ||||||||||||| data ch 0~36 (2 MHz 간격) |||||
 15.4  │   11  12  13 ... (2405 + 5×(k−11) MHz) ... 26 │
```

BLE 광고 채널 37/38/39는 Wi-Fi의 흔한 채널 1, 6, 11 **사이 틈**에 놓이도록 설계됐다. 연결 후 데이터 채널은 adaptive frequency hopping(AFH)으로 바쁜 채널을 피한다.

---

## 2. BLE 스택 계층

### 2.1 계층도

```
 ┌──────────────────────────────── Application ────────────────────────────────┐
 │                                                                               │
 │   GAP  (역할, 광고/스캔, 연결 수립, 보안 정책)     GATT (service / characteristic) │
 │                                                    ATT  (handle, read/write/notify)│
 │   SMP  (pairing, key 분배)                          │                          │  Host
 │     └──────────────── L2CAP (CID 0x0004=ATT, 0x0005=LE signaling, 0x0006=SMP)  │
 ├───────────────────── HCI (Command / Event / ACL data / ISO data) ─────────────┤
 │   Link Layer  (상태 머신: Standby/Advertising/Scanning/Initiating/Connection,  │
 │                채널 hopping, ACK/재전송, AES-CCM 암호화, 타이밍)               │  Controller
 │   PHY         (LE 1M, LE 2M, LE Coded S=2 / S=8, GFSK)                          │
 └───────────────────────────────────────────────────────────────────────────────┘
```

각 계층의 책임을 SSD 스택과 대응시키면 이해가 빠르다.

| BLE 계층 | 하는 일 | SSD/NVMe 비유 |
|---|---|---|
| PHY | 변조, 비트 전송 | PCIe PHY (SerDes) |
| Link Layer | 패킷 타이밍, ACK, 재전송, 채널 hopping, 암호화 | PCIe Data Link Layer (ACK/NAK, replay) |
| HCI | host와 controller 사이 표준 명령/이벤트 인터페이스 | NVMe 명령/완료 큐 인터페이스 |
| L2CAP | 채널 다중화, 분할/재조립 | Transaction Layer의 라우팅 |
| ATT/GATT | 속성 데이터베이스 read/write/notify | NVMe admin/IO 명령 세트 |
| GAP/SMP | 발견·연결·보안 정책 | 디바이스 enumeration, 보안(TCG Opal) |

### 2.2 Host–Controller 분리

- **Controller**(PHY + LL)는 µs 단위 타이밍이 중요해서 라디오 옆에서 돈다. 보통 벤더가 바이너리로 준다(예: Nordic SoftDevice Controller, Zephyr의 오픈소스 LL도 있음).
- **Host**(L2CAP 이상)는 타이밍이 덜 빡빡해서 앱 프로세서에 둘 수 있다.
- 둘 사이가 **HCI**다. Bluetooth Core Spec이 명령/이벤트 형식을 표준화했기 때문에 host와 controller를 다른 회사 것으로 섞을 수 있다(Linux BlueZ host + 아무 USB 동글).

9절에서 HCI 전송(UART H4 등)을 자세히 본다.

### 2.3 BLE 버전별 주요 기능

| 버전 | 연도 | 펌웨어 관점 주요 추가 |
|---|---|---|
| 4.0 | 2010 | Bluetooth Low Energy 도입 |
| 4.1 | 2013 | LE L2CAP CoC(connection-oriented channel), 역할 동시 수행 개선 |
| 4.2 | 2014 | **LE Secure Connections**(ECDH P-256), **Data Length Extension**(LL payload 27→251 bytes), LE privacy 1.2 |
| 5.0 | 2016 | **LE 2M PHY**, **LE Coded PHY**(장거리), **Extended Advertising** |
| 5.1 | 2019 | Direction finding (AoA/AoD) |
| 5.2 | 2019 | **LE Isochronous Channels**(LE Audio 기반), EATT, LE Power Control |
| 5.3 | 2021 | Connection subrating, channel classification 개선 |
| 5.4 | 2023 | PAwR(Periodic Advertising with Responses), 암호화된 광고 데이터 |
| 6.0 | 2024 | Channel Sounding(거리 측정) |

---

## 3. BLE PHY와 Link Layer

### 3.1 PHY

| PHY | 심볼 속도 | 데이터 속도 | 특징 |
|---|---|---|---|
| LE 1M | 1 Msym/s | 1 Mbps | 필수. 모든 기기 지원, 광고 기본 |
| LE 2M | 2 Msym/s | 2 Mbps | 선택. 같은 데이터를 절반 시간에 → 라디오 on 시간 감소, 거리 약간 감소 |
| LE Coded S=2 | 1 Msym/s | 500 kbps | FEC. 거리 증가 |
| LE Coded S=8 | 1 Msym/s | 125 kbps | FEC. 거리 약 4배(이론), on 시간 크게 증가 |

전력 관점: **같은 데이터라면 2M PHY가 라디오 on 시간을 거의 절반으로 줄인다.** 링크 품질이 괜찮으면 2M로 올리는 것이 전력과 처리량 모두에 이득이다.

### 3.2 Link Layer 패킷 구조 (LE 1M 기준)

```
 ┌──────────┬────────────────┬────────┬───────────────────┬───────┬───────┐
 │ Preamble │ Access Address │ Header │ Payload           │ (MIC) │ CRC   │
 │ 1 byte   │ 4 bytes        │ 2 bytes│ 0 ~ 251 bytes     │ 4     │ 3     │
 └──────────┴────────────────┴────────┴───────────────────┴───────┴───────┘
   (2M PHY는 preamble 2 bytes, MIC는 암호화된 연결에서만)

 LE 1M에서 1 byte = 8 µs
 빈 패킷(payload 0) : 1 + 4 + 2 + 3 = 10 bytes = 80 µs
 최대 데이터 패킷   : 1 + 4 + 2 + 251 + 3 = 261 bytes = 2088 µs  (MIC 제외)
```

### 3.3 Link Layer 상태

```
                     ┌─────────────┐
          ┌─────────▶│  Standby    │◀──────────┐
          │          └─────┬───────┘           │
          │     ┌──────────┼────────────┐      │
          │     ▼          ▼            ▼      │
     ┌──────────┐   ┌───────────┐  ┌───────────┐
     │Advertising│   │ Scanning  │  │ Initiating│
     └────┬─────┘   └───────────┘  └────┬──────┘
          │ CONNECT_IND 수신              │ CONNECT_IND 송신
          ▼                               ▼
     ┌──────────────── Connection ───────────────┐
     │  peripheral 역할              central 역할  │
     └───────────────────────────────────────────┘
```

---

## 4. Advertising과 Scanning

### 4.1 Advertising 동작

peripheral은 광고 이벤트마다 채널 37, 38, 39에 차례로 같은 패킷을 보낸다. 광고 이벤트 사이 간격이 **advInterval + advDelay**다.

```
 advInterval (20 ms ~ 10.24 s, 0.625 ms 단위)   + advDelay (0~10 ms 랜덤)
 |◀──────────────────────────────────────────▶|◀─▶|
 ┌──┐ ┌──┐ ┌──┐                                    ┌──┐ ┌──┐ ┌──┐
 │37│ │38│ │39│         (sleep)                    │37│ │38│ │39│
 └──┘ └──┘ └──┘                                    └──┘ └──┘ └──┘
  ↑ 연결 가능한 광고면 각 채널 TX 직후 잠깐 RX (SCAN_REQ / CONNECT_IND 대기)
```

- **advDelay**는 두 기기가 같은 간격으로 광고할 때 계속 충돌하지 않도록 넣는 랜덤 지연이다.
- 간격 범위 20 ms~10.24 s는 legacy advertising 기준이다. Core 5.0 이전에는 non-connectable/scannable 광고의 최소 간격이 100 ms였고, 5.0에서 20 ms로 통일됐다.
- Extended advertising(5.0+)은 primary 채널(37/38/39)에 짧은 `ADV_EXT_IND`만 보내고, 실제 데이터는 data 채널의 `AUX_ADV_IND`로 보낸다. 페이로드를 최대 254 bytes/PDU, 체인으로 최대 1650 bytes까지 늘릴 수 있다.

### 4.2 Legacy advertising PDU 종류

| PDU | 연결 가능 | 스캔 응답 | 용도 |
|---|---|---|---|
| `ADV_IND` | O | O | 일반적인 "나 연결해도 돼" 광고 |
| `ADV_DIRECT_IND` | O (특정 기기만) | X | 알고 있는 central에게 빠른 재연결 |
| `ADV_SCAN_IND` | X | O | 연결은 안 받지만 추가 정보 제공 |
| `ADV_NONCONN_IND` | X | X | 비콘(iBeacon, Eddystone 등) |

광고 데이터(AD) 최대 31 bytes, 스캔 응답(`SCAN_RSP`)도 31 bytes. AD는 `[length][type][data]` 구조의 반복이다: 예를 들어 Flags(0x01), Complete Local Name(0x09), 128-bit Service UUID 목록(0x07).

### 4.3 Scanning

- **scan interval**: 스캐너가 한 채널을 듣는 주기, **scan window**: 그 중 실제로 듣는 시간. window = interval이면 연속 스캔(전력 최대).
- **passive scan**: 듣기만 함. **active scan**: `SCAN_REQ`를 보내 `SCAN_RSP`를 받음.
- 발견 지연 = 광고 간격과 스캔 window가 겹칠 확률의 문제다. 광고를 1 s 간격으로 하면 폰이 기기를 찾는 데 수 초가 걸릴 수 있다. 그래서 흔한 패턴은 **"처음 30초는 빠르게(예: 100 ms 안팎), 그 다음은 느리게(1 s 이상)"**다. Apple의 액세서리 가이드라인도 이런 식의 권장 간격을 제시한다(버전별 문서 확인).

---

## 5. Connection — 타이밍, 파라미터, 전력

### 5.1 연결 수립

central이 `CONNECT_IND`를 보내면 그 안에 연결 파라미터(access address, CRC init, 전송 window, connection interval, peripheral latency, supervision timeout, 채널 맵, hop 값, sleep clock accuracy)가 들어 있다. 이후 양쪽은 합의된 **anchor point**마다 만나서 패킷을 주고받는다.

### 5.2 Connection event 타임라인

```
          anchor point                                             다음 anchor point
              │◀─────────────── connInterval (7.5 ms ~ 4 s, 1.25 ms 단위) ───────────────▶│
              │                                                                          │
 Central  ────┤▓▓C→P▓▓├──────┤▓▓C→P▓▓├──────┤▓C→P▓├─────────────────── (sleep) ─────────┤▓▓
 Periph.  ────────────┤▓P→C▓├───────┤▓P→C▓▓├───────┤▓P→C▓├──────────── (sleep) ──────────────
              │      │◀T_IFS▶│                                                          
              │       = 150 µs                                                          
              │◀────────────── connection event ──────────────▶│
                (MD=1이면 다음 패킷 쌍 계속, 양쪽 MD=0이면 이벤트 종료)
```

- 한 connection event는 **central이 먼저 보내고 peripheral이 T_IFS(150 µs) 뒤에 답하는** 패킷 쌍의 반복이다. 보낼 데이터가 없어도 빈 패킷을 주고받아 링크를 유지한다.
- 헤더의 **MD(More Data)** 비트가 1이면 같은 이벤트 안에서 쌍을 더 주고받는다. 처리량이 여기서 나온다.
- 매 이벤트마다 채널이 바뀐다(channel selection algorithm #1 또는 #2).

### 5.3 연결 파라미터 (Core Spec 값)

| 파라미터 | 범위 | 단위 | 의미 |
|---|---|---|---|
| Connection interval | 7.5 ms ~ 4 s | 1.25 ms (값 6~3200) | anchor 사이 간격 |
| Peripheral latency | 0 ~ 499 | 이벤트 수 | peripheral이 보낼 게 없으면 건너뛸 수 있는 이벤트 수 |
| Supervision timeout | 100 ms ~ 32 s | 10 ms (값 10~3200) | 이 시간 동안 패킷 없으면 연결 끊김으로 판단 |

제약: **supervision timeout > (1 + peripheral latency) × connection interval × 2** (ms 단위로 비교).

누가 정하나? **central이 최종 결정**한다. peripheral은 원하는 값을 요청할 뿐이다(L2CAP Connection Parameter Update Request 또는 LL Connection Parameter Request 절차). 폰 OS는 자기 정책(여러 기기 동시 연결, Wi-Fi 공존)에 따라 요청을 거절하거나 조정할 수 있다. iOS는 액세서리가 지켜야 할 파라미터 규칙을 Apple 액세서리 디자인 가이드라인에 공개한다.

### 5.4 Peripheral latency의 효과

```
 connInterval = 100 ms, peripheral latency = 4

 Central :  ▓   ▓   ▓   ▓   ▓   ▓   ▓   ▓   ▓   ▓   ▓       (매 이벤트 송신)
 Periph. :  ▓                   ▓                   ▓       (5번에 1번만 깸 → 500 ms)
            │◀──── 보낼 데이터 없으면 4번 건너뜀 ────▶│
 peripheral에 급한 데이터가 생기면 다음 이벤트에 바로 깨어나 보낼 수 있음 (송신 지연은 짧게 유지)
 central → peripheral 방향 데이터는 최대 500 ms 지연
```

latency는 "**peripheral 송신은 빠르게, 평소 전력은 낮게**"를 동시에 얻는 도구다. 단점은 central→peripheral 방향 지연이다.

### 5.5 Window widening — 왜 32 kHz 크리스털 정확도가 전력에 영향을 주나

peripheral은 sleep 동안 32 kHz 저속 클럭으로 시간을 잰다. 클럭이 부정확하면 anchor point가 언제 올지 확신할 수 없어서 **일찍 깨어 더 오래 듣는다(window widening)**. 대략:

```
 windowWidening ≈ ((centralSCA + peripheralSCA) / 1,000,000) × 마지막 anchor 이후 경과 시간

 예: 양쪽 합 100 ppm, 경과 1 s  → 약 100 µs 앞뒤로 더 들어야 함
     내부 RC(수백 ppm)면 훨씬 커짐
```

connInterval × (1 + latency)가 길수록 경과 시간이 커지므로 widening도 커진다. 그래서 초저전력 BLE 제품은 **정확한 32.768 kHz 크리스털**(예: ±20 ppm)을 쓴다. BOM 결정이 전력에 직결되는 예다.

### 5.6 BLE 평균 전류 계산 (예시)

아래 전류·시간은 **설명용 가정값**이다(nRF52급 칩, DC/DC 사용, 0 dBm 근처 가정). 실제 값은 칩 데이터시트, Nordic Online Power Profiler, PPK2 측정으로 확인한다.

| 구간 | 시간 | 전류 | 전하 |
|---|---|---|---|
| CPU wake + HFXO 기동 | 300 µs | 1.0 mA | 0.30 µC |
| 라디오 RX ramp-up | 140 µs | 4.0 mA | 0.56 µC |
| RX (central의 빈 패킷, 80 µs) | 80 µs | 5.0 mA | 0.40 µC |
| T_IFS (RX→TX 전환) | 150 µs | 5.0 mA | 0.75 µC |
| TX (빈 패킷 응답) | 80 µs | 5.0 mA | 0.40 µC |
| 후처리 (스택, 앱) | 200 µs | 3.0 mA | 0.60 µC |
| **이벤트당 합계** | 약 0.95 ms | | **약 3.0 µC** |

sleep 전류 2 µA로 가정하면:

```
 I_avg = Q_event / connInterval_eff + I_sleep

 interval 7.5 ms          : 3.0 µC / 7.5 ms  = 400 µA   + 2 → 약 402 µA
 interval 100 ms          : 3.0 µC / 0.1 s   = 30 µA    + 2 → 약 32 µA
 interval 100 ms, lat = 4 : 3.0 µC / 0.5 s   = 6 µA     + 2 → 약 8 µA   (central 쪽 전력은 그대로)
 interval 1 s             : 3.0 µC / 1 s     = 3 µA     + 2 → 약 5 µA
```

광고도 같은 방식으로 계산한다. 연결 가능 legacy 광고 1 이벤트 = 3채널 × (ramp + 최대 376 µs TX + 짧은 RX) + 오버헤드. 채널당 약 2.5 µC로 잡으면 이벤트당 약 8 µC:

```
 advInterval 100 ms : 8 µC / 0.1 s = 80 µA  + 2 → 약 82 µA
 advInterval 1 s    : 8 µC / 1 s   = 8 µA   + 2 → 약 10 µA
```

교훈: **"빨리 광고하는 상태"가 오래 지속되면 연결 유지보다 비싸다.** 폰이 연결을 끊은 뒤 무한히 100 ms로 광고하는 버그가 배터리를 조용히 갉아먹는 흔한 필드 이슈다.

### 5.7 처리량 계산 (MTU, DLE, PHY)

용어 정리:

- **LL payload**: 기본 27 bytes. DLE(4.2+)를 협상하면 최대 251 bytes.
- **ATT_MTU**: 기본 23 bytes(LE). MTU Exchange로 최대 517 bytes까지 협상.
- 알림 1개의 앱 데이터 = ATT_MTU − 3 (ATT 헤더: opcode 1 + handle 2).
- LL payload 안에는 L2CAP 헤더 4 bytes가 들어간다.

```
 기본값:   LL 27 bytes = L2CAP 4 + ATT 23  →  notify 앱 데이터 = 23 − 3 = 20 bytes
 DLE 최대: LL 251 bytes = L2CAP 4 + ATT 247 →  앱 데이터 244 bytes (ATT_MTU 247로 맞추면 분할 없음)
```

이론 최대 처리량 (한 방향, 매 쌍마다 데이터 패킷 + 빈 ACK 패킷, 이벤트가 끊기지 않는다고 가정):

```
 LE 1M: 데이터 2088 µs + T_IFS 150 + 빈 패킷 80 + T_IFS 150 = 2468 µs 당 244 bytes
        → 244 × 8 / 2468 µs ≈ 0.79 Mbps
 LE 2M: 데이터 (2+4+2+251+3)×4 µs = 1048 µs + 150 + 빈 패킷 44 + 150 = 1392 µs 당 244 bytes
        → 244 × 8 / 1392 µs ≈ 1.40 Mbps
```

실제로는 connection event 길이 제한, 폰 쪽 controller 정책, 재전송 때문에 이보다 낮다. 기본값(27 bytes, 1M)이면 수십~백 kbps 수준으로 떨어진다. **OTA 이미지 전송 속도가 느리다는 문제의 대부분은 MTU/DLE/PHY 협상 실패**다.

---

## 6. GAP과 GATT

### 6.1 역할

| 계층 | 역할 | 설명 |
|---|---|---|
| GAP | Broadcaster / Observer | 연결 없이 광고만 / 스캔만 |
| GAP | Peripheral / Central | 광고 후 연결받는 쪽 / 스캔 후 연결하는 쪽 |
| GATT | Server / Client | 속성 DB를 가진 쪽 / 읽고 쓰는 쪽 |

GAP 역할과 GATT 역할은 독립이다. 보통 웨어러블은 peripheral + server지만, 폰의 알림 서비스(ANCS 등)를 받을 때는 웨어러블이 GATT client가 된다.

### 6.2 속성 데이터베이스

GATT의 모든 것은 **attribute**다: 16-bit handle + type(UUID) + 권한 + 값.

```
 Handle  Type (UUID)                    Value
 0x0010  0x2800 Primary Service         <서비스 UUID>
 0x0011  0x2803 Characteristic Decl.    properties=READ|NOTIFY, value handle=0x0012, <char UUID>
 0x0012  <char UUID> Characteristic Val <센서 값 bytes>
 0x0013  0x2902 CCCD                    0x0001 = notify 켬 (client가 씀)
```

- **CCCD(Client Characteristic Configuration Descriptor)**: client가 0x0001을 쓰면 notify, 0x0002면 indicate를 켠다. bonding된 기기라면 CCCD 값은 연결 사이에 유지돼야 한다.
- **Notify vs Indicate**: notify는 ATT 수준 확인 응답이 없다(LL ACK는 있음). indicate는 client가 confirmation을 보내야 다음 indicate를 보낼 수 있다 → 신뢰성은 높고 처리량은 낮다.
- **Write vs Write Without Response**: 후자는 응답을 기다리지 않아 OTA 같은 대량 전송에 쓴다.
- 16-bit UUID는 SIG가 할당한 표준 서비스(예: Battery Service 0x180F, Heart Rate 0x180D)용이고, 자체 서비스는 128-bit UUID를 쓴다.

### 6.3 Zephyr GATT peripheral 전체 예제

```c
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

/* (1) 자체 128-bit UUID (임의 예시 값) */
#define SVC_UUID_VAL BT_UUID_128_ENCODE(0x8a3b0001, 0x1f2e, 0x4c5d, 0x9e0f, 0x0123456789ab)
#define CHR_UUID_VAL BT_UUID_128_ENCODE(0x8a3b0002, 0x1f2e, 0x4c5d, 0x9e0f, 0x0123456789ab)
static const struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(SVC_UUID_VAL);
static const struct bt_uuid_128 chr_uuid = BT_UUID_INIT_128(CHR_UUID_VAL);

static int16_t sensor_val;          /* 예: 온도 × 100 */
static bool notify_on;
static struct bt_conn *cur_conn;

/* (2) client가 CCCD를 쓰면 호출 */
static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    notify_on = (value == BT_GATT_CCC_NOTIFY);
}

/* (3) client가 값을 읽으면 호출 */
static ssize_t read_val(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             &sensor_val, sizeof(sensor_val));
}

/* (4) 서비스 정의: attrs[0]=service, [1]=chrc decl, [2]=chrc value, [3]=CCC */
BT_GATT_SERVICE_DEFINE(sensor_svc,
    BT_GATT_PRIMARY_SERVICE(&svc_uuid),
    BT_GATT_CHARACTERISTIC(&chr_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, read_val, NULL, NULL),
    BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* (5) 광고 데이터와 스캔 응답 */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, SVC_UUID_VAL),
};
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void adv_start(void)
{
    /* (6) 연결 가능 광고, 1 s ~ 1.2 s 간격 */
    int err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN,
                                              BT_GAP_ADV_SLOW_INT_MIN,
                                              BT_GAP_ADV_SLOW_INT_MAX, NULL),
                              ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        printk("adv start failed (%d)\n", err);
    }
}

static void adv_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    adv_start();
}
static K_WORK_DEFINE(adv_work, adv_work_handler);

/* (7) 연결 콜백 */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        return;
    }
    cur_conn = bt_conn_ref(conn);
    /* 500 ms interval, latency 4, timeout 6 s 요청: (1+4)×500×2 = 5000 < 6000 */
    bt_conn_le_param_update(conn, BT_LE_CONN_PARAM(400, 400, 4, 600));
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("disconnected (reason 0x%02x)\n", reason);
    if (cur_conn) {
        bt_conn_unref(cur_conn);
        cur_conn = NULL;
    }
    notify_on = false;
}

static void recycled(void)
{
    k_work_submit(&adv_work);        /* (8) 연결 객체가 반환된 뒤 광고 재시작 */
}

static void param_updated(struct bt_conn *conn, uint16_t interval,
                          uint16_t latency, uint16_t timeout)
{
    ARG_UNUSED(conn);
    printk("conn params: %u x1.25ms, lat %u, to %u x10ms\n", interval, latency, timeout);
}

BT_CONN_CB_DEFINE(conn_cb) = {
    .connected = connected,
    .disconnected = disconnected,
    .recycled = recycled,
    .le_param_updated = param_updated,
};

int main(void)
{
    int err = bt_enable(NULL);       /* (9) 동기 초기화 (콜백 NULL) */
    if (err) {
        printk("bt_enable failed (%d)\n", err);
        return 0;
    }
    adv_start();

    for (;;) {
        k_sleep(K_SECONDS(5));       /* (10) 5초마다 값 갱신 → batching 효과 */
        sensor_val++;
        if (cur_conn && notify_on) {
            err = bt_gatt_notify(cur_conn, &sensor_svc.attrs[2],
                                 &sensor_val, sizeof(sensor_val));
            if (err) {
                printk("notify failed (%d)\n", err);
            }
        }
    }
    return 0;
}
```

```
# prj.conf
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_DEVICE_NAME="hark-demo"
```

한 줄씩 핵심:

- (1) `BT_UUID_128_ENCODE`는 사람이 읽는 UUID 순서로 쓰면 BLE의 little-endian 바이트 배열로 바꿔 준다. 광고 데이터에도 같은 매크로를 재사용했다.
- (2) CCC 콜백의 `value`는 client가 쓴 CCCD 값이다. `BT_GATT_CCC_NOTIFY`(0x0001)와 비교한다.
- (3) `bt_gatt_attr_read()`가 offset/len 처리(긴 읽기 포함)를 대신해 준다. 직접 memcpy하면 offset 처리를 빠뜨리기 쉽다.
- (4) `BT_GATT_SERVICE_DEFINE`은 속성 테이블을 **컴파일 타임에 정적으로** 만든다(링커 섹션에 배치). `BT_GATT_CHARACTERISTIC` 하나가 declaration과 value 두 속성을 만들기 때문에 value는 `attrs[2]`다.
- (5) Flags(3 bytes) + 128-bit UUID(18 bytes) = 21 bytes로 31 bytes 한도 안이다. 이름은 스캔 응답으로 옮겨 광고 패킷을 짧게 했다(짧을수록 TX 시간이 줄어든다).
- (6) `BT_GAP_ADV_SLOW_INT_MIN/MAX`는 0x0640/0x0780(단위 0.625 ms) = 1 s/1.2 s다. `BT_LE_ADV_OPT_CONN`은 Zephyr 4.0에서 도입된 이름이고, 이전 버전은 `BT_LE_ADV_OPT_CONNECTABLE`을 썼다. 쓰는 버전의 헤더를 확인한다.
- (7) `bt_conn_ref()`로 참조를 잡아 두지 않으면 콜백이 끝난 뒤 conn 포인터가 무효가 될 수 있다. `BT_LE_CONN_PARAM(min, max, latency, timeout)`의 interval은 1.25 ms 단위, timeout은 10 ms 단위다. 최종 값은 central이 정한다.
- (8) 연결이 끊긴 직후에는 controller 자원이 아직 반환되지 않아 광고 시작이 실패할 수 있다. `recycled` 콜백 이후 work queue에서 재시작하는 것이 최근 Zephyr 샘플의 패턴이다.
- (9) `bt_enable(NULL)`은 초기화가 끝날 때까지 블록한다. bonding 정보를 flash에 저장하려면 `CONFIG_BT_SETTINGS=y`(+ settings 백엔드)를 켜고 `bt_enable()` 뒤에 `settings_load()`를 호출한다.
- (10) 값이 바뀔 때마다 보내지 말고 모아서 보내면 라디오 이벤트 수가 줄어든다. 단, connection event는 알림이 없어도 매 interval(latency 고려) 발생한다는 것을 기억한다.
- 단순화한 부분: `cur_conn`/`notify_on`은 BT 스레드와 main 스레드가 같이 접근한다. 제품 코드에서는 atomic이나 mutex, 또는 이벤트를 메시지 큐로 main에 넘기는 구조로 만든다.

### 6.4 연결 후 성능 협상 API (Zephyr)

| 목적 | API | 비고 |
|---|---|---|
| 연결 파라미터 변경 요청 | `bt_conn_le_param_update()` | central이 최종 결정 |
| PHY 변경 요청 | `bt_conn_le_phy_update()` + `BT_CONN_LE_PHY_PARAM_2M` | `CONFIG_BT_USER_PHY_UPDATE` 필요 |
| Data length 변경 | `bt_conn_le_data_len_update()` + `BT_LE_DATA_LEN_PARAM_MAX` | `CONFIG_BT_USER_DATA_LEN_UPDATE`, controller 버퍼 설정 필요 |
| MTU 교환 (client 쪽) | `bt_gatt_exchange_mtu()` | server는 `CONFIG_BT_L2CAP_TX_MTU` 등으로 수용 크기 설정 |
| 현재 MTU 확인 | `bt_gatt_get_mtu()` | 알림 최대 크기 = MTU − 3 |
| 보안 수준 요청 | `bt_conn_set_security()` | `BT_SECURITY_L2`~`L4` |

---

## 7. BLE 보안 — pairing과 bonding

### 7.1 용어

- **Pairing**: 두 기기가 암호화 키를 만드는 절차(SMP).
- **Bonding**: pairing 결과 키를 **저장**해서 다음 연결 때 바로 암호화하는 것.
- **LTK**(Long Term Key): 링크 암호화 키. **IRK**(Identity Resolving Key): 랜덤 주소를 해석하는 키. **CSRK**: 데이터 서명 키(거의 안 씀).

### 7.2 Pairing 3단계

```
 Phase 1: Pairing Feature Exchange
          IO capability(DisplayOnly, DisplayYesNo, KeyboardOnly, NoInputNoOutput, KeyboardDisplay),
          OOB 여부, MITM 요구, Secure Connections 지원 여부 교환 → 연결 방식 결정

 Phase 2: 키 생성
          Legacy : TK(Just Works면 0) → STK            ← 수동 도청에 약함
          LE Secure Connections(4.2+) : ECDH P-256 공개키 교환 → LTK 직접 생성

 Phase 3: 키 분배 (bonding 시)
          LTK / IRK / (CSRK) 교환 → flash에 저장
```

### 7.3 연결 방식(association model)

| 방식 | 조건 | MITM 보호 | 예 |
|---|---|---|---|
| Just Works | 한쪽이 입출력 없음 | 없음 | 헤드폰, 간단한 센서 |
| Numeric Comparison | 양쪽 화면 + Yes/No (SC 전용) | 있음 | 폰 ↔ 스마트워치 |
| Passkey Entry | 한쪽 표시, 한쪽 입력 | 있음 | 키보드 |
| Out of Band | NFC 등 다른 채널로 키 교환 | 채널 보안에 따름 | NFC 탭 페어링 |

Zephyr 보안 수준: `BT_SECURITY_L1`(암호화 없음), `L2`(암호화, MITM 없음), `L3`(암호화 + MITM 인증), `L4`(LE Secure Connections + MITM, 128-bit 키). 특성 권한에 `BT_GATT_PERM_READ_ENCRYPT`나 `BT_GATT_PERM_READ_AUTHEN`을 주면 해당 수준이 아니면 접근이 거부된다.

### 7.4 Privacy

기기가 고정 MAC 주소로 광고하면 추적당한다. 그래서 **Resolvable Private Address(RPA)**를 주기적으로 바꾼다(Zephyr 기본 `CONFIG_BT_RPA_TIMEOUT` = 900초). bonding된 폰은 IRK로 RPA를 해석해 "같은 기기"임을 안다. 컨슈머 웨어러블에서는 privacy가 사실상 필수다.

### 7.5 흔한 보안/bonding 버그

- 폰에서 기기 삭제 후 재페어링 실패: 기기 쪽에 옛 bond가 남아 키가 불일치. **양쪽 bond 정리 흐름**(기기에서 "페어링 초기화" 버튼, `bt_unpair()`)이 필요하다.
- bond 저장 flash가 꽉 참: 최대 bond 수(`CONFIG_BT_MAX_PAIRED`) 초과 정책을 정해야 한다.
- factory reset 때 bond를 지우지 않아 중고 기기가 이전 주인 폰에 붙음(C09 출하 전 초기화와 연결).

---

## 8. Wi-Fi — 펌웨어 엔지니어가 알아야 할 부분

### 8.1 연결 과정 (STA)

```
 STA                                              AP
  │ ── Probe Request (active scan) ─────────────▶ │   또는 Beacon 수신(passive scan)
  │ ◀──────────────────────── Probe Response ──── │
  │ ── Authentication (Open System 또는 SAE) ───▶ │   WPA3-Personal은 여기서 SAE 교환
  │ ◀─────────────────────── Authentication ───── │
  │ ── Association Request ─────────────────────▶ │   능력(속도, HT/VHT/HE), RSN IE
  │ ◀─────────────────── Association Response ─── │   AID 할당
  │ ◀── EAPOL msg1 (ANonce) ───────────────────── │ ┐
  │ ── EAPOL msg2 (SNonce, MIC) ────────────────▶ │ │ 4-way handshake
  │ ◀── EAPOL msg3 (GTK 암호화 전달, MIC) ──────── │ │ → PTK 설치
  │ ── EAPOL msg4 (ACK) ────────────────────────▶ │ ┘
  │ ── DHCP Discover/Request ... ───────────────▶ │   IP 획득 → 이제 TCP/IP
```

- **WPA2-Personal(PSK)**: PMK = PBKDF2-HMAC-SHA1(passphrase, SSID, 4096회, 256 bit). 4-way handshake에서 PMK + ANonce + SNonce + 두 MAC 주소로 PTK를 만든다. handshake를 캡처하면 **오프라인 사전 공격**이 가능한 것이 약점이다.
- **WPA3-Personal(SAE)**: Dragonfly 기반 키 교환으로 오프라인 사전 공격을 막고 forward secrecy를 준다. **PMF(802.11w, Protected Management Frames)가 필수**다.
- **PMF**: deauth/disassoc 같은 관리 프레임을 보호해 강제 연결 끊기 공격을 막는다.
- 이 절차는 보통 **supplicant**(wpa_supplicant 등)가 처리한다. Zephyr도 hostap 기반 supplicant를 통합했다(`CONFIG_WIFI_NM_WPA_SUPPLICANT`).

### 8.2 Wi-Fi 칩 아키텍처: FullMAC vs SoftMAC

| 구분 | FullMAC | SoftMAC |
|---|---|---|
| MAC/MLME(연결 관리) 위치 | 칩 펌웨어 | 호스트 드라이버(Linux mac80211) |
| 호스트 부담 | 적음, 이더넷처럼 보임 | 큼 |
| 예 (Linux) | brcmfmac(Broadcom/Cypress) | ath9k, mt76 등 |
| MCU에 적합 | 예 (nRF7002, ESP32 계열 호스티드, 각종 SPI/SDIO 모듈) | 아님 |

호스트 인터페이스는 SDIO, PCIe, USB, SPI/QSPI다. 부팅 시 **호스트가 칩에 펌웨어와 보드별 설정(NVRAM, 캘리브레이션, 규제 도메인 파일)을 다운로드**하는 구조가 흔하다(Linux `request_firmware()`). Don이 Apple에서 본 PCIe 링크 bring-up 뒤에 바로 이 FW 다운로드 단계가 온다.

### 8.3 Power save — DTIM, PS-Poll, TWT

```
 Beacon 간격 = 보통 100 TU = 102.4 ms (1 TU = 1024 µs)
 DTIM period = 3 이면 3번째 beacon마다 브로드캐스트/멀티캐스트 전달

 AP beacon :  B    B    D    B    B    D    B    B    D      (D = DTIM beacon)
 STA (PS)  :            ▓              ▓              ▓      ← DTIM마다만 깨어 TIM 확인
                        │
                        └ TIM에 내 AID 비트가 켜져 있으면 → PS-Poll / null frame(PM=0)로 버퍼 데이터 수신
```

- STA는 프레임 헤더의 **PM 비트**로 "나 잘게"를 AP에 알린다. AP는 그 STA 앞 unicast 프레임을 버퍼링하고 beacon의 **TIM**에 표시한다.
- **Listen interval**: STA가 association 때 "최대 몇 beacon마다 깨겠다"고 알리는 값. DTIM을 건너뛰면 브로드캐스트(ARP 등)를 놓칠 수 있다.
- **U-APSD**(WMM power save): 트리거 프레임으로 데이터 수신, VoIP 같은 주기 트래픽용.
- **TWT(Target Wake Time, 802.11ax/Wi-Fi 6)**: STA와 AP가 "언제 깨어날지" 일정을 협상한다(individual TWT, broadcast TWT). beacon마다 깨지 않아도 되므로 IoT 기기 전력을 크게 줄인다. AP가 지원해야 한다.

**Wi-Fi 연결 유지 전력 예시 (가정값)**: DTIM 깨어남 1회에 RX 2 ms × 50 mA, 그 사이 sleep 50 µA라면

```
 DTIM 1 (102.4 ms마다): 50 mA × 2 ms / 102.4 ms ≈ 0.98 mA + 0.05 ≈ 1.0 mA
 DTIM 3 (307 ms마다)  : 100 µC / 307 ms          ≈ 0.33 mA + 0.05 ≈ 0.38 mA
 DTIM 10 (1.024 s마다): 100 µC / 1.024 s         ≈ 0.10 mA + 0.05 ≈ 0.15 mA
```

같은 "연결 유지"라도 BLE(수~수십 µA)와 Wi-Fi(수백 µA~mA)는 한 자릿수 이상 차이 난다. 그래서 웨어러블은 **평소엔 BLE로 폰과 연결을 유지하고, 큰 데이터나 폰 없는 상황에서만 Wi-Fi를 켜는** 구조가 흔하다. 실제 칩 값은 데이터시트와 측정으로 확인한다.

### 8.4 Zephyr Wi-Fi 연결 코드 (발췌)

```c
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

int wifi_connect(const char *ssid, const char *psk)
{
    struct net_if *iface = net_if_get_default();     /* (1) Wi-Fi 인터페이스 */
    struct wifi_connect_req_params p = {0};

    p.ssid = (const uint8_t *)ssid;
    p.ssid_length = strlen(ssid);
    p.psk = (const uint8_t *)psk;
    p.psk_length = strlen(psk);
    p.security = WIFI_SECURITY_TYPE_PSK;             /* (2) WPA2-PSK. WPA3는 WIFI_SECURITY_TYPE_SAE */
    p.channel = WIFI_CHANNEL_ANY;
    p.band = WIFI_FREQ_BAND_2_4_GHZ;
    p.mfp = WIFI_MFP_OPTIONAL;                       /* (3) PMF 협상 */

    /* (4) 비동기: 결과는 NET_EVENT_WIFI_CONNECT_RESULT 이벤트로 온다 */
    return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p));
}
```

- (1) 여러 인터페이스가 있으면 기본 인터페이스가 Wi-Fi가 아닐 수 있다. 최근 버전은 Wi-Fi 인터페이스를 찾는 헬퍼(`net_if_get_first_wifi()`)를 제공한다.
- (4) `net_mgmt()`는 요청만 보내고 반환한다. 연결 결과, 끊김(`NET_EVENT_WIFI_DISCONNECT_RESULT`), IP 획득(`NET_EVENT_IPV4_ADDR_ADD`)은 `net_mgmt_init_event_callback()` + `net_mgmt_add_event_callback()`으로 등록한 콜백에서 받는다(콜백의 이벤트 인자 타입은 Zephyr 버전에 따라 32/64-bit로 바뀌었으니 헤더 확인).
- power save는 `NET_REQUEST_WIFI_PS`로 설정하고, 셸에서는 `wifi ps`, `wifi twt` 명령으로 실험할 수 있다(드라이버 지원 필요).

### 8.5 BLE로 Wi-Fi provisioning

화면 없는 기기에 Wi-Fi 비밀번호를 넣는 표준적인 방법이다.

```
 폰 앱                               기기 (BLE peripheral)
  │ ── 스캔, provisioning 서비스 UUID 발견 ─▶│
  │ ── 연결 + pairing (LE SC, 가능하면 MITM)─▶│   또는 앱 수준 보안 세션(ECDH + PoP 코드)
  │ ── "주변 AP 스캔해 줘" (write) ─────────▶ │ ── Wi-Fi 스캔
  │ ◀── AP 목록 (notify) ───────────────────── │
  │ ── SSID + 비밀번호 (write, 암호화된 링크)─▶ │ ── Wi-Fi 연결 시도 (8.1 흐름)
  │ ◀── 결과: 성공/실패 이유 (notify) ────────  │
  │ ── (클라우드 계정 연결 토큰 전달) ───────▶ │
```

- 구현 예: Espressif의 provisioning 컴포넌트(BLE transport + 보안 세션), Nordic nRF Connect SDK의 Wi-Fi provisioning over BLE 샘플, Matter의 BLE 기반 commissioning.
- 보안 포인트: BLE 링크가 Just Works면 근처 공격자가 비밀번호를 볼 수 있다. **LE Secure Connections + 앱 수준 암호화(Proof-of-Possession 코드)**를 같이 쓰는 게 일반적이다.
- 실패 이유(비밀번호 틀림, AP 못 찾음, DHCP 실패)를 구분해서 앱에 알려 주는 것이 UX의 핵심이다.

---

## 9. Thread와 Matter

### 9.1 IEEE 802.15.4 기초

- 2.4 GHz O-QPSK, **250 kbps**, 채널 11~26(2405~2480 MHz, 5 MHz 간격).
- PHY 최대 프레임 **127 bytes**. MAC 헤더와 FCS를 빼면 상위 계층이 쓸 수 있는 건 100 bytes 안팎이다.
- CSMA-CA로 채널 접근, ACK 프레임으로 재전송.

### 9.2 Thread 스택

```
 ┌───────────────────────────────┐
 │ Application (Matter, CoAP 등)  │
 ├───────────────────────────────┤
 │ UDP                            │
 ├───────────────────────────────┤
 │ IPv6  (라우팅, mesh-local 주소)│   ← Thread는 IP 네트워크다
 ├───────────────────────────────┤
 │ 6LoWPAN (헤더 압축, 분할)      │   ← IPv6 최소 MTU 1280 bytes를 127 bytes 프레임에 싣기
 ├───────────────────────────────┤
 │ IEEE 802.15.4 MAC              │
 ├───────────────────────────────┤
 │ IEEE 802.15.4 PHY (2.4 GHz)    │
 └───────────────────────────────┘
 제어: MLE(Mesh Link Establishment) — 이웃 발견, 링크 설정, 라우터 선출
```

### 9.3 Thread 역할

```
                    (Wi-Fi / Ethernet IPv6 네트워크)
                               │
                       ┌───────┴────────┐
                       │ Border Router  │   Thread ↔ 외부 IP 네트워크
                       └───────┬────────┘
                  ┌────────────┼─────────────┐
             ┌────┴───┐   ┌────┴───┐    ┌────┴───┐
             │ Router │───│ Leader │────│ Router │     Router: 항상 RX, 패킷 중계
             └───┬────┘   └───┬────┘    └───┬────┘     Leader: 라우터 ID 할당 (1개, 자동 선출)
                 │            │             │
              ┌──┴──┐      ┌──┴──┐       ┌──┴──┐
              │ SED │      │ MED │       │REED │       REED: 필요하면 Router로 승격 가능
              └─────┘      └─────┘       └─────┘
   SED(Sleepy End Device): 라디오 끄고 자다가 주기적으로 parent에 data poll
   MED(Minimal End Device): 항상 RX, 라우팅 안 함
```

- 라우터는 최대 32개(ID 할당 기준), 나머지는 end device로 붙는다.
- **SED의 전력**은 data poll 주기로 결정된다. BLE peripheral latency와 비슷한 trade-off다(poll 주기 ↑ = 전력 ↓, 하향 지연 ↑).
- Thread 1.2+의 **CSL(Coordinated Sampled Listening)**을 쓰는 SSED(Synchronized SED)는 parent와 합의된 시각에만 짧게 들어서 하향 지연을 줄인다.
- 네트워크 자격 증명(network key, PAN ID, extended PAN ID, channel, mesh-local prefix 등)을 묶은 것이 **Operational Dataset**이다. 기기를 네트워크에 넣는 과정이 commissioning이다.
- 구현: **OpenThread**(Google 주도 오픈소스)가 사실상 표준이며 Zephyr/nRF Connect SDK에 통합돼 있다.

### 9.4 Matter

- Connectivity Standards Alliance(CSA)가 만든 **스마트홈 애플리케이션 계층 표준**(1.0은 2022년). 데이터 모델(cluster, attribute, command)과 보안(기기 인증서 기반 attestation, 운영 인증서)을 정의한다.
- 전송은 IPv6 위: **Thread, Wi-Fi, Ethernet**. **BLE는 commissioning(최초 설정) 용도**로 쓴다.
- 펌웨어 관점: 기기마다 **DAC(Device Attestation Certificate)와 개인키를 공장에서 주입**해야 한다 → C09 provisioning과 직결된다.
- Hark의 "home용 기기" 언급을 보면 향후 Matter/Thread가 등장할 수 있다(추정).

### 9.5 멀티프로토콜 라디오

nRF52840이나 nRF5340 같은 칩은 한 라디오로 BLE와 802.15.4를 **시분할**로 같이 돌린다(Nordic은 MPSL이 timeslot을 중재). 동시에 두 프로토콜을 쓰면 각자의 스케줄 충돌(예: BLE connection event와 Thread poll이 겹침)을 스택이 우선순위로 해결한다. 이것도 일종의 coexistence다.

---

## 10. Host–Controller 분리와 HCI 전송

### 10.1 배치 방식

| 구성 | Host 위치 | Controller 위치 | 연결 | 예 |
|---|---|---|---|---|
| 단일 칩 | 같은 MCU | 같은 MCU | 함수 호출 | nRF52840 + Zephyr |
| 듀얼 코어 | app core | network core | 코어 간 IPC | nRF5340 (Zephyr `hci_ipc` 샘플) |
| 외부 controller | 앱 프로세서/SoC | BT 칩 | UART(H4/H5), USB, SDIO, PCIe | 스마트폰, Linux 보드 |
| 외부 controller (MCU) | MCU | 별도 BLE 칩 | UART H4 | Zephyr `hci_uart` 샘플을 controller로 사용 |

### 10.2 HCI UART (H4) 패킷 형식

```
 H4: 첫 바이트가 패킷 종류
   0x01 Command   : [0x01][opcode lo][opcode hi][param len][params...]
   0x02 ACL data  : [0x02][handle+flags 2B][data len 2B][data...]
   0x03 SCO data
   0x04 Event     : [0x04][event code][param len][params...]
   0x05 ISO data  (5.2+)

 opcode = (OGF << 10) | OCF

 예) HCI_Reset (OGF 0x03, OCF 0x0003 → opcode 0x0C03)
     host → ctrl : 01 03 0C 00
     ctrl → host : 04 0E 04 01 03 0C 00
                   │  │  │  │  └──┴── 완료된 opcode 0x0C03, status 0x00(성공)
                   │  │  │  └ Num_HCI_Command_Packets = 1
                   │  │  └ 파라미터 길이 4
                   │  └ Command Complete 이벤트(0x0E)
                   └ Event 패킷
```

- H4는 오류 복구가 없어서 **하드웨어 흐름 제어(RTS/CTS)가 사실상 필수**다. 흐름 제어 없이 높은 baud에서 바이트를 잃으면 패킷 경계가 어긋나 스택이 이상해진다. H5(Three-wire UART)는 슬립, 재전송, 체크섬이 있다.
- Linux에서는 `btmon`으로 HCI 트래픽을 전부 볼 수 있다. Don이 Apple에서 프로토콜 분석기로 버스를 보던 것과 같은 도구다.

### 10.3 콤보 칩 펌웨어 다운로드

스마트폰급 Wi-Fi/BT 콤보 칩은 보통 이렇게 올라온다(세부는 벤더마다 다름).

```
 1. PMIC가 칩 전원 레일 인가 (SPMI/I2C로 제어), 32 kHz sleep clock 공급, reset 해제
 2. 호스트 인터페이스 링크업 (PCIe enumeration 또는 SDIO 카드 초기화)
 3. Wi-Fi: 호스트 드라이버가 FW 이미지 + NVRAM(보드 설정/캘리브레이션) + 규제 DB를 칩 RAM에 다운로드 → 실행
 4. BT: ROM 코드가 부팅된 뒤 호스트가 vendor HCI 명령으로 patch/설정 파일 다운로드 → HCI_Reset
 5. 공존(coex) 설정, 안테나/프런트엔드 설정 (RFFE로 FEM 제어)
 6. 정상 동작. 이후 칩 FW crash 시 호스트가 core dump 수집 → 재다운로드(recovery)
```

- 3~4의 파일 버전이 호스트 드라이버와 맞지 않으면 "링크는 올라왔는데 스캔이 안 된다" 같은 증상이 난다.
- 칩 FW crash 복구 경로(watchdog, dump, 재초기화)를 설계하는 것은 호스트 펌웨어/드라이버 엔지니어의 일이다.

> **Don 경험과 연결**: 이 1~6 순서가 Don이 Apple에서 매일 본 경계다. 면접에서 "나는 BLE 스택 API를 오래 쓰진 않았지만, 새 콤보 칩이 호스트와 만날 때 전원 시퀀스 → 링크업 → FW 다운로드 → HCI/드라이버 초기화까지 어디서 깨지는지 root cause를 잡아 왔다"고 말하면 갭을 강점으로 바꿀 수 있다.

---

## 11. 2.4 GHz 공존 (coexistence)

### 11.1 무엇이 충돌하나

| 충돌 | 이유 |
|---|---|
| Wi-Fi 2.4 GHz ↔ BLE | 같은 대역, 공유 안테나 또는 가까운 안테나 |
| Wi-Fi ↔ Thread/802.15.4 | 같은 대역, 15.4 채널 일부가 Wi-Fi 채널과 겹침 |
| LTE/NR ↔ 2.4 GHz | Band 40(2300~2400 MHz), Band 41(2496~2690 MHz), Band 7 UL(2500~2570 MHz)이 ISM 대역 바로 옆 → 송신 쪽이 수신 쪽을 막음(desense) |
| 5 GHz Wi-Fi ↔ 기타 | 하모닉/상호변조 (보드 설계 영역) |

### 11.2 해결 수단

```
 (1) 콤보 칩 내부 중재        (2) 칩 사이 PTA                       (3) 모뎀과 WCI-2
 ┌──────────────┐            ┌──────────┐  REQUEST  ┌──────────┐     ┌────────┐ UART  ┌──────────┐
 │ Wi-Fi │ BT   │            │ BLE/15.4 │ ────────▶ │ Wi-Fi    │     │ Cellular│◀────▶│ Wi-Fi/BT │
 │  └─ arbiter ─┘│            │  칩      │ PRIORITY  │ (arbiter)│     │ modem  │ 실시간 │ 콤보     │
 │ 공유 안테나    │            │          │ ────────▶ │          │     └────────┘ 상태  └──────────┘
 └──────────────┘            │          │ ◀──────── │          │      (TX 중, 프레임 타이밍 등)
                             └──────────┘   GRANT   └──────────┘
```

- **PTA(Packet Traffic Arbitration)**: IEEE 802.15.2가 권고한 방식. 저전력 라디오가 송수신 전에 REQUEST(+PRIORITY)를 올리고, arbiter(보통 Wi-Fi 칩)가 GRANT를 주면 그때 쓴다. 신호 이름과 선 개수(2-wire, 3-wire, 4-wire)는 벤더마다 다르다. 펌웨어는 GPIO 할당, 극성, 우선순위 규칙을 설정한다.
- **WCI-2**: 모뎀과 BT/Wi-Fi 칩이 UART로 실시간 상태를 교환하는 인터페이스. Bluetooth Core Spec의 MWS(Mobile Wireless Standards) coexistence 부분에 정의돼 있다.
- **AFH / 채널 맵**: BLE는 Wi-Fi가 쓰는 채널을 데이터 채널 맵에서 뺀다. host가 `HCI_LE_Set_Host_Channel_Classification`으로 나쁜 채널을 알려 줄 수도 있다.
- **시간 분할 정책**: 오디오 스트리밍(BT) 중 Wi-Fi 스캔을 미루기, BLE connection interval을 Wi-Fi beacon 타이밍과 겹치지 않게 조정하기 등.

공존 문제의 전형적 증상: "Wi-Fi 다운로드 중에 BLE 연결이 끊긴다(supervision timeout)", "LTE 특정 밴드에서만 BT 오디오가 끊긴다". 디버깅은 **각 라디오의 활동 타이밍을 같은 시간축에 놓고 보는 것**(PTA GPIO를 LA로 캡처 + 스니퍼 로그 + 모뎀 로그)이 핵심이다.

> **Don 경험과 연결**: RFFE로 프런트엔드를 제어하고 여러 라디오가 한 제품에서 부딪히는 문제를 본 경험은 이 영역과 직접 겹친다. "PTA GPIO와 RFFE 트랜잭션을 LA로 같이 캡처해 타이밍 충돌을 증명했다" 같은 이야기가 있다면 면접에서 강력한 스토리다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 연결 끊긴 뒤 빠른 광고를 계속 유지 | 폰과 멀어지면 배터리가 급감 | fast adv 타임아웃 없음 | 30초 후 slow adv(1 s+)로 전환 |
| MTU/DLE/PHY 협상 안 함 | OTA가 수십 kbps로 느림 | 기본 27/23 bytes, 1M | DLE 251, MTU 247, 2M PHY 요청, 결과 확인 |
| supervision timeout 제약 위반 | 파라미터 업데이트 거절 또는 잦은 끊김 | timeout ≤ (1+lat)×interval×2 | 제약 계산 후 요청 |
| conn 포인터 참조 관리 누락 | 끊김 후 크래시 | `bt_conn_ref/unref` 불균형 | 콜백에서 ref, disconnected에서 unref |
| bond 불일치 | 재페어링 실패, 암호화 실패 | 한쪽만 bond 삭제 | 기기 쪽 bond 삭제 UX, `bt_unpair()` |
| CCCD 상태 미확인 notify | 에러 반환, 로그 폭주 | client가 notify를 안 켬 | CCC 콜백 상태 확인 후 전송 |
| HCI UART 흐름 제어 없음 | 간헐적 HCI 파싱 오류, 스택 리셋 | 고속 baud에서 바이트 손실 | RTS/CTS 사용, baud 확인 |
| Wi-Fi DTIM 1 고정 | 대기 전류 mA 단위 | beacon마다 깸 | DTIM/listen interval 조정, TWT |
| Wi-Fi 비밀번호를 Just Works BLE로 전달 | 보안 감사 실패 | MITM 보호 없음 | LE SC + 앱 수준 PoP 암호화 |
| 공존 설정 누락 | Wi-Fi 트래픽 중 BLE 끊김 | PTA GPIO/우선순위 미설정 | PTA 설정, 활동 타이밍 캡처로 검증 |

---

## 13. 면접에서 이렇게 말한다

**Q.** Explain the difference between advertising and a connection in BLE, and how each affects power.

**A.** 광고는 연결 없이 채널 37/38/39에 주기적으로 패킷을 뿌리는 것(20 ms~10.24 s 간격), 연결은 central과 합의한 interval(7.5 ms~4 s)마다 anchor point에서 만나 데이터 채널을 hopping하며 주고받는 것. 전력은 둘 다 "이벤트당 전하 ÷ 간격"이고, 연결은 peripheral latency로 이벤트를 건너뛸 수 있다.

"Advertising is connectionless: the peripheral transmits the same PDU on the three primary channels, 37, 38 and 39, every advertising interval — 20 milliseconds to 10.24 seconds plus a random delay. A connection is scheduled: after CONNECT_IND, both sides meet at an anchor point every connection interval, from 7.5 milliseconds to 4 seconds, hopping across the data channels. For power, both reduce to charge per event divided by interval. In a connection the peripheral can also use peripheral latency to skip events when it has nothing to send, which cuts its average current several times while keeping its own transmit latency low."

**Q.** How would you choose connection parameters for a wearable that streams sensor data sometimes but is idle most of the time?

**A.** 두 모드: 평소에는 긴 interval(예: 500 ms~1 s) + latency, 스트리밍/OTA 때만 짧은 interval(예: 15~30 ms) + 2M PHY + DLE. timeout 제약 확인, 폰 OS 가이드라인 준수, central이 최종 결정하므로 `le_param_updated` 콜백으로 실제 값을 확인.

"I'd use two profiles. Idle: a long interval, say 500 ms to 1 s, with some peripheral latency, so the peripheral wakes rarely but can still send an urgent notification quickly. Active — streaming or OTA — a short interval around 15 to 30 ms, plus 2M PHY and data length extension to cut radio-on time per byte. I'd switch with a connection parameter update, check the supervision-timeout constraint, follow the phone vendor's accessory guidelines, and verify what the central actually granted in the parameter-updated callback, because the central has the final say."

**Q.** Why is BLE throughput often much lower than 1 or 2 Mbps?

**A.** 기본 LL payload 27 bytes/ATT MTU 23이면 패킷당 20 bytes만 데이터, T_IFS 150 µs와 빈 ACK 패킷 오버헤드, connection event 길이 제한, 폰이 한 이벤트에 보내는 패킷 수 제한, 재전송. DLE 251 + MTU 247 + 2M PHY로 이론상 약 1.4 Mbps.

"Protocol overhead. With default settings each link-layer packet carries 27 bytes, and after L2CAP and ATT headers a notification carries only 20 bytes of payload. Every packet is followed by a 150-microsecond inter-frame space and an acknowledgment packet, and the phone's controller limits how many packets it allows per connection event. Negotiating data length extension to 251 bytes, an ATT MTU of 247 and the 2M PHY brings the theoretical ceiling to about 1.4 megabits; in practice you measure and see what the phone allows."

**Q.** Walk me through BLE pairing and bonding. What would you choose for a screenless wearable?

**A.** feature exchange → 키 생성(LE Secure Connections: ECDH P-256) → 키 분배(LTK, IRK). 화면 없으면 Just Works가 되는데 MITM 보호가 없음 → OOB(NFC) 또는 앱 수준 인증(QR/PoP 코드)로 보완, privacy(RPA), bond 삭제 UX.

"Pairing has three phases: feature exchange, where IO capabilities decide the association model; key generation, which with LE Secure Connections is an ECDH P-256 exchange; and key distribution, where bonding stores the LTK and the IRK so we can reconnect encrypted and resolve private addresses. A screenless wearable ends up with Just Works, which has no MITM protection, so I'd add an out-of-band channel like NFC or a proof-of-possession code printed on the device and checked at the application layer. I'd also enable privacy with rotating resolvable addresses and give the user a way to clear bonds, because one-sided bond deletion is the most common re-pairing failure."

**Q.** How does Wi-Fi power save work, and how would you minimize idle current?

**A.** STA가 PM 비트로 잠을 알리고 AP가 버퍼링, beacon의 TIM으로 알려 줌. DTIM마다 브로드캐스트. DTIM/listen interval을 늘리고, 불필요한 브로드캐스트(ARP, mDNS) 필터, Wi-Fi 6 TWT, 가능하면 BLE로 keep-alive하고 Wi-Fi는 필요할 때만.

"The station sets the power-management bit, the AP buffers its frames and advertises them in the TIM of each beacon, and broadcast traffic is delivered after DTIM beacons. The station only needs to wake for the beacons it has to hear, then retrieves buffered data with PS-Poll or a null frame. To cut idle current I'd skip to a larger DTIM or listen interval, filter broadcast traffic the device doesn't need, use target wake time on Wi-Fi 6 access points, and architecturally keep the always-on link on BLE and bring Wi-Fi up only for bulk transfers — idle Wi-Fi is typically an order of magnitude more current than a BLE connection."

**Q.** What's the host/controller split and how does HCI work?

**A.** Controller = PHY + Link Layer(타이밍 중요, 라디오 옆), Host = L2CAP/ATT/GATT/SMP/GAP. 둘 사이 표준 HCI: command/event/ACL/ISO 패킷. UART H4는 첫 바이트로 종류 구분, 흐름 제어 필수. Apple에서 본 콤보 칩 bring-up과 연결.

"The controller is the PHY and link layer, which need microsecond timing and live next to the radio; the host is L2CAP, ATT, GATT, SMP and GAP, which can run on the application processor. HCI is the standard interface between them: commands and events for control, ACL and ISO packets for data. Over UART the H4 transport prefixes each packet with a type byte — 1 for command, 2 for ACL, 4 for event — and needs hardware flow control. In my Apple role I worked right at this boundary: power sequencing, link-up, firmware and patch download to the combo chip, then HCI and driver initialization — so I'm used to figuring out which layer a failure lives in."

**Q.** What is Thread and how does it differ from BLE and Wi-Fi?

**A.** 802.15.4(250 kbps) 위 6LoWPAN + IPv6 mesh. 라우터가 중계하고 SED는 parent에 poll하며 µA 수준. BLE는 IP 없는 star, Wi-Fi는 고속 star. Matter가 Thread/Wi-Fi 위 앱 계층, BLE는 commissioning.

"Thread is a low-power IPv6 mesh on IEEE 802.15.4 at 250 kilobits. 6LoWPAN compresses IPv6 headers to fit 127-byte frames, routers forward traffic and a leader assigns router IDs, and sleepy end devices keep the radio off and poll their parent, so they run at microamps. Compared with BLE, it's a real IP network and multi-hop; compared with Wi-Fi, it's far lower power and bandwidth. Matter sits on top as the application layer over Thread or Wi-Fi, and uses BLE only for commissioning."

**Q.** A user reports that BLE disconnects while the device is downloading over Wi-Fi. How do you debug it?

**A.** disconnect reason 코드 확인(0x08 supervision timeout이면 링크 손실), 같은 시간축에 BLE 스니퍼 + Wi-Fi 활동 + PTA GPIO(LA) 캡처, 공존 설정(PTA 우선순위, BLE 이벤트 보호), 채널 맵, 안테나 격리 확인. 파라미터(timeout 여유, latency) 조정.

"First the disconnect reason: 0x08 is a supervision timeout, meaning the link was lost rather than closed. Then I'd put everything on one timeline: a BLE sniffer trace, Wi-Fi activity from driver logs, and the PTA request and grant lines on a logic analyzer. If BLE requests are being denied during Wi-Fi bursts, it's a coexistence priority problem — raise the priority of connection events, or have Wi-Fi yield periodically. I'd also check the channel map avoids the active Wi-Fi channel and make sure the supervision timeout has enough margin. If grants look fine but packets are still lost, I'd look at antenna isolation with the hardware team."

---

## 14. 직접 해보기

### 실습 1 — Zephyr BLE peripheral + nRF Connect 앱 + PPK2

```sh
cd ~/zephyrproject
west build -p -b nrf52840dk/nrf52840 zephyr/samples/bluetooth/peripheral_hr
west flash
```

1. 폰에 nRF Connect for Mobile을 설치하고 기기를 스캔·연결한 뒤 Heart Rate 특성의 notify를 켠다.
2. 앱에서 connection parameter 요청(또는 6.3 예제처럼 코드에서 `bt_conn_le_param_update()`)으로 interval을 바꿔 가며 PPK2로 평균 전류를 기록한다.
3. 5.6절의 계산값과 측정값을 표로 비교한다. 차이가 나는 이유(폰이 준 실제 interval, 로그 UART, 스택 처리 시간)를 찾는다.
4. 6.3의 GATT 예제를 `samples/bluetooth/peripheral` 구조를 복사한 새 앱으로 빌드해 본다.

### 실습 2 — nRF Sniffer로 공중 패킷 보기

- nRF52840 DK 또는 nRF52840 Dongle에 nRF Sniffer for Bluetooth LE 펌웨어를 올리고 Wireshark extcap 플러그인을 설치한다(Nordic 문서 절차).
- 확인할 것: `ADV_IND` 3채널 반복과 간격, `SCAN_REQ/SCAN_RSP`, `CONNECT_IND` 안의 interval/latency/timeout 값, `LL_LENGTH_REQ`(DLE), `LL_PHY_REQ`, ATT MTU Exchange, pairing 패킷.
- 암호화된 연결은 LTK 없이는 내용이 안 보인다는 것도 확인한다(Legacy Just Works는 스니퍼가 해독 가능 → 왜 LE SC가 필요한지 체감).

### 실습 3 — HCI를 눈으로 보기 (Linux 필요)

```sh
# DK를 HCI UART controller로 만든다
west build -p -b nrf52840dk/nrf52840 zephyr/samples/bluetooth/hci_uart
west flash

# Linux 호스트(BlueZ)에서
sudo btattach -B /dev/ttyACM0 -S 1000000 -P h4 &
sudo btmon &                       # 모든 HCI 명령/이벤트를 디코드해 출력
bluetoothctl                       # power on, scan on
```

- `btmon` 출력에서 `HCI_Reset`, `LE Set Scan Parameters`, `LE Advertising Report` 이벤트를 찾고 10.2절의 바이트 형식과 대조한다.
- 보드의 UART 흐름 제어 설정과 baud는 샘플 문서와 보드 overlay에 따라 다를 수 있으니 확인한다. macOS는 외부 HCI controller 연결을 지원하지 않으므로 Linux PC나 VM(USB passthrough)을 쓴다.

### 실습 4 — OpenThread CLI로 mesh 만들기 (DK 2개)

```sh
# nRF Connect SDK 사용 시
west build -p -b nrf52840dk/nrf52840 nrf/samples/openthread/cli
west flash
```

두 보드의 시리얼 셸에서(프롬프트 형식은 샘플에 따라 `ot` 접두어가 붙는다):

```sh
ot dataset init new
ot dataset commit active
ot ifconfig up
ot thread start
ot state            # 한 대는 leader
ot dataset active -x  # 이 hex dataset을 두 번째 보드에 넣는다
```

두 번째 보드에서 `ot dataset set active <hex>`, `ot ifconfig up`, `ot thread start` 후 `ot state`가 child 또는 router가 되는지, `ot ping <leader 주소>`가 되는지 본다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| GAP | Generic Access Profile | 광고, 연결, 역할, 보안 정책 |
| GATT | Generic Attribute Profile | service/characteristic 데이터 모델 |
| ATT | Attribute Protocol | handle 기반 read/write/notify |
| SMP | Security Manager Protocol | pairing, 키 분배 |
| L2CAP | Logical Link Control and Adaptation | 채널 다중화, 분할/재조립 |
| HCI | Host Controller Interface | host–controller 표준 명령/이벤트 |
| Link Layer | BLE 하위 계층 | 타이밍, ACK, hopping, 암호화 |
| Anchor point | 연결 이벤트 시작 시각 | central 첫 패킷 시점 |
| Connection interval | 연결 이벤트 간격 | 7.5 ms ~ 4 s |
| Peripheral latency | 건너뛸 수 있는 이벤트 수 | 0 ~ 499 |
| Supervision timeout | 링크 손실 판단 시간 | 100 ms ~ 32 s |
| T_IFS | inter frame space | 150 µs |
| DLE | Data Length Extension | LL payload 최대 251 bytes |
| ATT_MTU | ATT 최대 패킷 | 기본 23, 최대 517 |
| CCCD | Client Characteristic Config. Descriptor | notify/indicate 켜기 (0x2902) |
| LTK / IRK | Long Term Key / Identity Resolving Key | 암호화 키 / 랜덤 주소 해석 키 |
| LE Secure Connections | 4.2+ pairing | ECDH P-256 기반 |
| RPA | Resolvable Private Address | 주기적으로 바뀌는 주소 |
| AFH | Adaptive Frequency Hopping | 나쁜 채널 회피 |
| DTIM | Delivery Traffic Indication Message | 브로드캐스트 전달 beacon 주기 |
| TIM | Traffic Indication Map | 버퍼된 unicast 표시 |
| TWT | Target Wake Time | Wi-Fi 6 깨어남 일정 협상 |
| SAE | Simultaneous Authentication of Equals | WPA3-Personal 키 교환 |
| PMF | Protected Management Frames | 802.11w, WPA3 필수 |
| FullMAC / SoftMAC | Wi-Fi MAC 위치 | 칩 FW / 호스트 드라이버 |
| 6LoWPAN | IPv6 over 802.15.4 | 헤더 압축, 분할 |
| MLE | Mesh Link Establishment | Thread 링크/라우터 관리 |
| SED | Sleepy End Device | poll로 데이터 받는 저전력 노드 |
| Border Router | Thread ↔ IP 네트워크 | 외부 연결 게이트웨이 |
| Matter | CSA 스마트홈 표준 | IP 위 앱 계층, BLE commissioning |
| PTA | Packet Traffic Arbitration | 라디오 간 REQUEST/GRANT 중재 |
| WCI-2 | Wireless Coexistence Interface 2 | 모뎀 ↔ BT/Wi-Fi UART 공존 신호 |
| H4 / H5 | HCI UART 전송 | 단순 / 신뢰성(3-wire) |

---

## 16. 요약 & 체크리스트

BLE는 PHY·Link Layer(controller)와 L2CAP·ATT/GATT·SMP·GAP(host)로 나뉘고 HCI로 연결된다. 광고는 채널 37/38/39에 20 ms~10.24 s 간격, 연결은 7.5 ms~4 s interval의 anchor point마다 패킷 쌍을 주고받으며 peripheral latency와 supervision timeout이 전력과 안정성을 정한다. 평균 전류는 "이벤트당 전하 ÷ 실효 간격 + sleep 전류"로 계산하고, 처리량은 DLE·MTU·2M PHY 협상에 달려 있다. 보안은 LE Secure Connections + bonding + privacy가 기본이다. Wi-Fi는 scan→auth→assoc→4-way handshake(WPA3는 SAE+PMF) 뒤 DTIM/TWT로 전력을 관리하며, BLE로 provisioning한다. Thread는 802.15.4 + 6LoWPAN + IPv6 mesh이고 Matter가 그 위 앱 계층이다. 여러 라디오가 공존할 때는 콤보 칩 내부 중재, PTA, WCI-2, AFH로 조정하며, Don의 칩셋 통합 경험이 이 host–controller·coex 경계와 직접 이어진다.

- [ ] BLE 스택 계층도(host/controller/HCI)를 그리고 각 계층 책임을 말할 수 있다
- [ ] advertising 이벤트와 connection event 타임라인(anchor, T_IFS, MD)을 그릴 수 있다
- [ ] 연결 파라미터 범위와 supervision timeout 제약식을 외워서 쓸 수 있다
- [ ] 이벤트당 전하로 BLE 평균 전류와 배터리 영향을 계산할 수 있다
- [ ] 27/23/20 bytes와 251/247/244 bytes의 관계, 2M PHY 처리량 계산을 설명할 수 있다
- [ ] Zephyr `BT_GATT_SERVICE_DEFINE`, `bt_le_adv_start`, `bt_gatt_notify`, `BT_CONN_CB_DEFINE` 코드를 설명할 수 있다
- [ ] pairing 3단계, association model, bonding·privacy를 설명하고 화면 없는 기기 전략을 말할 수 있다
- [ ] Wi-Fi 연결 과정(WPA2 4-way, WPA3 SAE/PMF)과 DTIM/TWT power save를 설명할 수 있다
- [ ] Thread 역할(Leader/Router/REED/SED/Border Router)과 Matter의 위치를 설명할 수 있다
- [ ] HCI H4 패킷을 바이트 단위로 해석하고 콤보 칩 bring-up 순서와 PTA/WCI-2 공존을 Apple 경험과 엮어 말할 수 있다

---

## 참고 자료

- [Bluetooth Core Specification (Bluetooth SIG)](https://www.bluetooth.com/specifications/specs/core-specification/)
- [Bluetooth SIG — Assigned Numbers (UUID, AD types)](https://www.bluetooth.com/specifications/assigned-numbers/)
- [Zephyr — Bluetooth LE Host](https://docs.zephyrproject.org/latest/connectivity/bluetooth/index.html)
- [Zephyr — Bluetooth API (GATT, Connection Management)](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/index.html)
- [Zephyr — Bluetooth samples](https://docs.zephyrproject.org/latest/samples/bluetooth/bluetooth.html)
- [Zephyr — Wi-Fi Management](https://docs.zephyrproject.org/latest/connectivity/networking/api/wifi.html)
- [Nordic — nRF Sniffer for Bluetooth LE](https://www.nordicsemi.com/Products/Development-tools/nRF-Sniffer-for-Bluetooth-LE)
- [Nordic — Online Power Profiler for Bluetooth LE](https://devzone.nordicsemi.com/power/w/opp/2/online-power-profiler-for-bluetooth-le)
- [Nordic DevAcademy — Bluetooth Low Energy Fundamentals](https://academy.nordicsemi.com/courses/bluetooth-low-energy-fundamentals/)
- [OpenThread](https://openthread.io/)
- [Thread Group](https://www.threadgroup.org/)
- [Connectivity Standards Alliance — Matter](https://csa-iot.org/all-solutions/matter/)
- [Apple — Accessory Design Guidelines for Apple Devices](https://developer.apple.com/accessories/Accessory-Design-Guidelines.pdf)
- [Linux BlueZ](https://github.com/bluez/bluez)
- [hostap / wpa_supplicant](https://w1.fi/wpa_supplicant/)
- [Wi-Fi Alliance — Wi-Fi CERTIFIED WPA3](https://www.wi-fi.org/discover-wi-fi/security)
