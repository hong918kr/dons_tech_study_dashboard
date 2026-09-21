# J12. Familiarity with wireless protocols (BLE, Wi-Fi, or Thread)

> **분류**: Requirement 5/7 · **관련 개념 노트**: C06, S03 (§3~§4), C03, C05
> **Don 현재 상태**: 🟡 부분 — Apple **RF(Wireless) 칩셋 통합** 그룹에서 무선 칩을 제품에 올리는 일(HW/인터페이스 수준)을 하고 있지만, 레쥬메에 BLE/Wi-Fi **프로토콜 스택** 개발 경험은 없다.
> **이 노트를 다 읽으면**: ① "familiarity"가 요구하는 실제 깊이가 어디까지인지 선을 그을 수 있다 · ② BLE/Wi-Fi/Thread를 난이도별(기초·중급·심화)로 대답할 수 있다 · ③ host–controller 경계(HCI, 펌웨어 다운로드, coexistence)라는 **Don이 이미 강한 축**으로 대화를 끌고 올 수 있다.

---

## 0. 문장 뜯어보기

| 구(句) | 표면적 의미 | 채용담당자가 이 단어를 고른 이유 |
|---|---|---|
| Familiarity with | "익숙함". expert/deep/strong이 아니다 | 다른 Requirement에는 "Strong proficiency"(C/C++), "Hands-on experience"(RTOS)를 썼다. 여기만 단어를 낮췄다 = **스택 개발자를 뽑는 자리가 아니다** |
| wireless protocols | 프로토콜(계층·상태 머신·타이밍) | RF 설계가 아니다. 안테나·매칭·OTA(over-the-air) 성능은 별도 RF 공고가 있다 [11] |
| BLE | Bluetooth Low Energy | 컨슈머 기기의 **폰 페어링·provisioning·컨트롤 채널**. 사실상 필수 |
| Wi-Fi | 802.11 STA | 대역폭이 필요한 경로(음성 업로드, OTA 이미지 다운로드, 모델 갱신) |
| Thread | 802.15.4 기반 IP mesh | 홈 기기·Matter 쪽 포석. "a family of AI devices, **for yourself and for the home**" [10]와 연결 |
| **or** | 셋 중 하나면 된다 | 셋 다 아는 사람은 드물다는 걸 안다. **하나를 깊게 + 나머지를 정확히 비교**하면 통과 |

> 결론: 이 문장은 "BLE 스택을 짜 봤나?"가 아니라 **"무선 링크가 붙어 있는 기기의 펌웨어를 맡겼을 때, 무선 때문에 생기는 문제를 알아볼 수 있나?"**를 묻는다. 전력·타이밍·연결 유지·coexistence·bring-up이 실제 관심사다.

### 0.1 Responsibility 노트와의 역할 분담

이 노트는 **검증 관점**이다. "면접관이 무엇으로 판별하는지, 내 경력에서 무엇을 증거로 내놓을지, 갭을 어떻게 메울지"만 다룬다.
프로토콜 자체의 교과서적 설명(PHY, 패킷 구조, GATT 구현, 전류 계산)은 `C06`에 있고, 문항 드릴은 `S03 §3~§4`에 있다. 여기서는 중복하지 않고 **필요한 곳에서 절 번호로 가리킨다.**

---

## 1. Hark에서 실제로 하게 될 일 (추정)

[추정] context 2.2·2.7절 기준. Hark 1세대 기기는 RF Antenna 공고에 **Cellular, Wi-Fi, BT, GNSS, NFC, UWB**가 모두 명시된 웨어러블급 기기이고 [확인됨][11], 구조는 **Qualcomm SoC(Cortex-A, Android) + always-on 저전력 MCU(Ambiq 계열, RTOS)** 조합으로 추정된다.

이 조합에서 이 포지션(Embedded Software Engineer, MCU/BSP 쪽)이 무선과 닿는 지점은 다음과 같다.

| 닿는 지점 | 하는 일 | 깊이 |
|---|---|---|
| MCU의 BLE 링크 | 폰과의 페어링, 기기 설정/상태 GATT, 버튼·센서 이벤트 알림. 벤더 SDK(Nordic NCS, Ambiq + 외부 BLE SoC 등) 위에서 GATT 서비스 정의와 연결 파라미터 튜닝 | **중간~깊음** — 이 역할이 직접 짠다 |
| Wi-Fi | SoC(Android) 쪽이 주로 담당. MCU는 "Wi-Fi 켜라/꺼라"를 IPC로 요청하고, 전력 상태를 조정 | 얕음 — 단, OTA 경로·전류 예산은 알아야 함 |
| Cellular / GNSS / NFC / UWB | 대부분 SoC 또는 전용 모듈. 펌웨어는 전원 시퀀스, 인터페이스 활성화, 상태 보고 | 얕음 — 단, **bring-up 때는 이 역할이 앞장선다** |
| 콤보 칩 bring-up | 라디오 칩이 호스트에 붙는 경로(UART/SDIO/PCIe/SPI), 펌웨어 패치 다운로드, 전원·클럭 시퀀스, 리셋 핀, 32.768 kHz 슬립 클럭 | **깊음 — 여기가 Don의 자리** |
| Coexistence | 2.4 GHz(Wi-Fi/BT/Thread)와 셀룰러·UWB가 동시에 살아 있을 때의 중재. PTA 신호 배선, 우선순위 정책, 증상 판별 | **깊음 — 여기도 Don의 자리** |
| 전력 | BLE advertising/connection interval, Wi-Fi DTIM/TWT가 평균 전류에 미치는 영향. always-on 기기의 배터리 예산 방어 | 중간 |

**하루/한 주가 어떤 모습인가** [추정]
- 월: EVT 보드에서 BLE 연결이 특정 유닛만 3분 뒤 끊긴다 → 로그 + 스니퍼 캡처 → supervision timeout인지, 슬립 클럭 정확도(window widening)인지 좁힘 (C06 §5.5)
- 화: 라디오 칩 벤더가 새 컨트롤러 펌웨어 패치를 줬다 → 부팅 시퀀스에 반영, 다운로드 실패 시 복구 경로 추가, 버전 로깅 (C06 §10.3)
- 수: HW 팀이 "coex 3-wire를 이 핀으로 뺐다"고 알림 → 핀 설정·극성·타이밍 확인, LA로 BT_ACTIVE/WLAN_ACTIVE 파형 캡처
- 목: AI 팀이 "음성 업로드 중에 오디오 드롭이 난다" → Wi-Fi 버스트와 I2S DMA의 시간 충돌 분석 (S06 D06)
- 금: 평균 전류 회귀. advertising interval을 100 ms에서 300 ms로 바꿨을 때 페어링 체감 지연 vs 전류를 제품팀과 협상

> 핵심: **Hark가 이 자리에 기대하는 무선 능력은 "스택을 새로 짜는 것"이 아니라 "라디오가 붙은 시스템을 살려 놓고 예산 안에 유지하는 것"**이다. 이건 Don이 Apple에서 매일 하는 일과 같은 범주다.

---

## 2. 핵심 개념

### 2.1 "familiarity"의 실제 채점 기준

면접관(특히 하드웨어·펌웨어 리드)이 이 항목에서 보는 건 세 층이다.

| 층 | 질문의 형태 | 통과 기준 | 실패 신호 |
|---|---|---|---|
| L1 — 어휘 | "BLE에서 advertising과 connection의 차이는?" | 용어를 정확히, 수치 범위와 함께 | 용어를 섞어 씀(예: "BLE 패킷을 broadcast로 보낸다"를 connection 상태에서 설명) |
| L2 — 트레이드오프 | "connection interval을 늘리면 뭐가 좋아지고 뭐가 나빠지나?" | 전류·지연·throughput·재연결을 한 문장에 엮음 | "전력이 좋아진다"만 말하고 끝 |
| L3 — 시스템 판별 | "BLE가 Wi-Fi 켤 때마다 끊긴다. 어디부터 보나?" | 계층을 나눠 좁히는 절차 + 측정 도구 | 바로 "coex 문제"라고 단정 |

**펌웨어 엔지니어에게 기대되지 않는 것** (선을 긋고 가야 답이 흔들리지 않는다)
- Link Layer 상태 머신을 RTL/펌웨어로 구현하기
- 802.11 MAC의 프레임 포맷 암기, EAPOL 4-way handshake 메시지의 비트 단위 내용
- Thread의 MLE 메시지 포맷, mesh routing 알고리즘 내부
- 암호 스위트 수학(AES-CCM 내부, ECDH 연산)

**기대되는 것**
- 상태와 전이(광고 → 연결 → 끊김 → 재연결)를 그릴 수 있음
- 파라미터와 전류·지연의 관계를 수치로 말할 수 있음
- 스택이 호스트/컨트롤러로 어디서 갈라지고, 그 경계에서 무엇이 깨지는지
- 무선 문제인지 아닌지 판별하는 도구(스니퍼, LA, 스택 로그, RSSI/PER 카운터)

### 2.2 세 프로토콜 한 장 비교 (면접에서 이 표를 말로 재생)

| | BLE | Wi-Fi (STA) | Thread |
|---|---|---|---|
| 표준 주체 | Bluetooth SIG (Core Spec) | IEEE 802.11 + Wi-Fi Alliance 인증 | Thread Group (802.15.4 PHY/MAC 위) |
| 대역 | 2.4 GHz ISM, 40 ch × 2 MHz | 2.4/5/6 GHz | 2.4 GHz, 16 ch (11~26) |
| 원시 속도 | LE 1M / 2M / Coded(500k, 125k) | 수십~수백 Mbps | 250 kbps |
| 토폴로지 | star (중앙-주변), mesh는 별도 프로파일 | infrastructure (AP 경유) | **IPv6 mesh, self-healing** |
| 주소/네트워크 | 48-bit BD_ADDR, 연결 핸들 | MAC + IP (DHCP) | **native IPv6 / 6LoWPAN** |
| 전형 전류 | 수 µA(슬립) ~ 수 mA(연결 이벤트) | 수십~수백 mA 버스트 | BLE와 같은 급, SED는 매우 낮음 |
| 기기에서의 쓸모 | 폰 페어링, 설정, 알림, provisioning | 대용량(OTA, 음성 업로드, 모델 갱신) | 홈 기기·Matter 연동 |
| 펌웨어가 만지는 것 | GATT 서비스, 연결 파라미터, 보안 | 연결 관리, power save, 재연결 정책 | 역할(Router/SED) 선택, child timeout |

세 줄 요약으로 말할 것:
- **BLE는 "가깝고 짧고 싸다"** — 제어 채널.
- **Wi-Fi는 "빠르지만 비싸다"** — 필요할 때만 켜는 파이프.
- **Thread는 "싸고 항상 IP로 닿는다"** — 집 안 기기들이 서로 연결되는 망.

### 2.3 BLE — 기억해야 할 수치 (Core Spec 기준)

이 값들은 Bluetooth Core Specification에 정의된 것이라 벤더와 무관하다. 면접에서 숫자를 말하면 "familiarity" 이상으로 들린다.

| 항목 | 값 | 단위 해석 |
|---|---|---|
| Primary advertising 채널 | 37(2402 MHz), 38(2426), 39(2480) | Wi-Fi 1/6/11 사이 틈에 배치 |
| Advertising interval | 20 ms ~ 10.24 s | 0.625 ms 단위. 여기에 0~10 ms 랜덤 delay가 더해짐 |
| Connection interval | 7.5 ms ~ 4.0 s | 1.25 ms 단위 |
| Peripheral latency | 0 ~ 499 | 건너뛸 수 있는 연결 이벤트 수 |
| Supervision timeout | 100 ms ~ 32 s | 10 ms 단위. 반드시 `timeout > (1 + latency) × interval × 2` |
| 기본 ATT MTU | 23 octet | payload 20 octet (opcode 1 + handle 2) |
| LL 데이터 길이 | 기본 27, DLE로 최대 251 octet | Data Length Extension |
| PHY | LE 1M / LE 2M / LE Coded (S=2 → 500 kbps, S=8 → 125 kbps) | 5.0부터 2M·Coded |

버전별 기능(면접에서 "5.x에서 뭐가 생겼나" 질문이 나옴):

| 버전 | 대표 기능 |
|---|---|
| 4.0 | LE 도입 |
| 4.2 | LE Secure Connections, DLE, Privacy 1.2 |
| 5.0 | LE 2M PHY, LE Coded PHY, Extended Advertising |
| 5.1 | Direction Finding (AoA/AoD), GATT caching |
| 5.2 | LE Audio 기반(LE Isochronous Channels, EATT, LE Power Control) |
| 5.3 | 연결 subrating, 채널 분류 개선 |
| 5.4 | PAwR (Periodic Advertising with Responses) |
| 6.0 | Channel Sounding (거리 측정) |

> **버전 의존**: Core Spec은 계속 갱신된다. 최신 버전과 기능 목록은 [bluetooth.com/specifications/specs](https://www.bluetooth.com/specifications/specs/)에서 확인할 것. 면접에서는 "5.0에서 2M과 Coded가 들어왔고, LE Audio는 5.2 계열이다" 정도면 충분하다.

### 2.4 Host–Controller 경계 — Don이 강한 축

BLE 스택은 규격상 **Host**와 **Controller**로 나뉘고, 그 사이를 **HCI**가 잇는다. 이 경계가 곧 "칩과 칩 사이"이고, Don이 Apple에서 매일 보는 경계다.

```
   [ Application ]        ← GATT 서비스, 제품 로직
   ───────────────
   [   Host      ]        GAP / GATT / ATT / SMP / L2CAP
   ═══ HCI ═══════        ← 여기가 칩 경계가 되기도 한다
   [ Controller ]        Link Layer / PHY
   ───────────────
        RF
```

배치는 세 가지다.

| 배치 | 모습 | 예 | 펌웨어가 하는 일 |
|---|---|---|---|
| SoC 단일칩 | Host와 Controller가 같은 MCU 안 | Nordic nRF52/nRF53 + Zephyr | HCI가 내부 함수 호출. 전송 디버깅 없음 |
| 컨트롤러 분리 | 호스트 MCU + 외부 BLE 컨트롤러 | 호스트 + HCI UART 라디오 | **전송 계층(H4/H5), 흐름 제어, 펌웨어 패치 다운로드** |
| 콤보 칩 | Wi-Fi + BT가 한 칩, 버스도 공유 | SDIO(Wi-Fi) + UART(BT), 또는 PCIe | 전원 시퀀스, 펌웨어/패치 로드, coex 배선, 두 서브시스템의 리셋 순서 |

**HCI UART (H4) 패킷 — 외워 두면 값이 큰 3줄** (Core Spec Vol 4, Part A)

```
[ 1 octet type ][ ...packet... ]
 0x01 Command   : opcode(2, OGF 6b + OCF 10b) | plen(1) | params
 0x02 ACL data  : handle+flags(2)             | len(2)  | data
 0x03 SCO       : handle+flags(2)             | len(1)  | data
 0x04 Event     : event code(1)               | plen(1) | params
 0x05 ISO       : handle+flags(2)             | len(2)  | data     (5.2~)
```

H4는 흐름 제어가 없어서 하드웨어 RTS/CTS가 필요하다. **H5(Three-wire UART, Vol 4 Part D)**는 슬라이딩 윈도우·재전송·CRC를 얹어 RTS/CTS 없이도 견디게 만든 것이다. 면접에서 "H4와 H5의 차이"는 컨트롤러 분리 경험을 검증하는 좋은 질문이다.

**부팅 시퀀스에서 실제로 깨지는 것들** (여기가 Don의 언어다)

```
 전원 인가 ──> 라디오 전원 레일 안정 ──> 32.768 kHz 슬립 클럭 공급
        ──> RESET_N 해제 (Tmin 대기) ──> 호스트가 전송 열기(UART 115200 등)
        ──> HCI Reset 전송 ──> Command Complete 수신
        ──> 벤더 펌웨어/패치 다운로드 (벤더 전용 HCI opcode)
        ──> 보레이트 변경 ──> 다시 HCI Reset
        ──> Read Local Version Information ──> 버전 로깅
        ──> LE Set Advertising Parameters / Enable
```

| 실패 지점 | 증상 | 첫 확인 |
|---|---|---|
| 슬립 클럭 없음/부정확 | 연결은 되는데 수십 초~수 분 뒤 끊김, sleep 전류 높음 | 32.768 kHz 파형, 크리스털 부하 커패시터, ppm 사양 |
| RESET_N 타이밍 | HCI Reset에 응답 없음 | 리셋 해제 후 대기 시간, 레일 순서 |
| 보레이트 전환 구간 | 패치 다운로드 중간에 멈춤 | 양쪽 전환 타이밍, RTS/CTS 배선 |
| 패치 버전 불일치 | 특정 유닛만 기능 이상 | Read Local Version 로그를 부팅마다 남기기 |
| coex 핀 미설정 | Wi-Fi 트래픽 때만 BLE PER 상승 | BT_ACTIVE/WLAN_ACTIVE 파형, 극성 |

> **벤더 의존**: 패치 파일 형식(`.hcd`, `.bin`), 벤더 opcode, 다운로드 프로토콜은 벤더마다 다르다. Infineon/Cypress 계열은 HCI 벤더 커맨드로 미니드라이버를 올린 뒤 패치를 밀어 넣고, Linux에서는 `btattach`/`hciattach` 계열 도구가 같은 일을 한다. 면접에서는 **"벤더마다 다르지만 흐름은 같다"**고 말하는 게 정확하다.

### 2.5 Wi-Fi — 펌웨어가 실제로 만나는 부분

```
  "이 SSID에 붙어" ──> ① Scan (passive: 비콘 수신 / active: probe request)
                    ──> ② Authentication (Open, 또는 WPA3는 SAE)
                    ──> ③ Association (AID 할당)
                    ──> ④ 4-way handshake (EAPOL, PTK/GTK 생성)
                    ──> ⑤ DHCP → IP 확보
                    ──> ⑥ DNS / TLS / 애플리케이션
```

면접에서 가치 있는 건 **"어디서 깨지고 어떻게 구분하나"**다.

| 단계 | 실패 증상 | 구분법 |
|---|---|---|
| ① Scan | AP가 안 보임 | 채널/대역(5 GHz만 방송하는 SSID), 숨김 SSID, 지역 규제(regdomain) |
| ② Auth | 즉시 거부 | 보안 모드 불일치(WPA2 vs WPA3-only), PMF 요구 |
| ④ EAPOL | 붙었다 끊김 반복 | 비밀번호 오류 vs 타임아웃. 스택 로그에 실패 사유 코드 |
| ⑤ DHCP | "연결됨"인데 통신 안 됨 | 링크는 살아 있고 IP만 없음 → L2/L3 구분의 교과서 사례 |
| ⑥ 이후 | 특정 서버만 실패 | 방화벽/캡티브 포털. 펌웨어 잘못이 아님을 증명 |

**power save — 평균 전류를 지배하는 것**
- 비콘 주기: 보통 100 TU (1 TU = 1024 µs → 약 102.4 ms)
- DTIM period: 몇 번째 비콘마다 broadcast/multicast를 내보내는지. STA는 DTIM 비콘은 반드시 깨서 들어야 한다. DTIM=3이면 약 307 ms마다 깨어남
- TWT (Target Wake Time, 802.11ax): AP와 깨어날 시각을 협상 → IoT/웨어러블에 유리
- 펌웨어의 역할: **power save 모드를 켜고, 지연에 민감한 구간에서만 끄는 정책**을 만드는 것

계산 연습(배터리 수명, DTIM별 평균 전류)은 `S03 §5 E4`에 있다.

### 2.6 Thread — 셋 중 가장 적게 알아도 되지만, 비교는 정확해야

- PHY/MAC: IEEE 802.15.4, 2.4 GHz, 250 kbps
- 위에 **6LoWPAN**(IPv6 헤더 압축), **MLE**(이웃/라우팅 관리), UDP. 즉 **모든 노드가 IPv6 주소를 가진다** — BLE와 가장 큰 차이
- 역할: Leader / Router / REED / FED / MED / SED. 배터리 기기는 보통 **SED(Sleepy End Device)** — 부모 라우터에 붙어 자다가 poll 주기마다 깨서 데이터를 가져옴
- Border Router가 Thread 망과 집안 Wi-Fi/이더넷을 잇는다
- **Matter**는 애플리케이션 계층 표준이고, 전송으로 Thread 또는 Wi-Fi를 쓴다. **커미셔닝(최초 등록)은 BLE로 한다** — 이 사실 하나만 알아도 세 프로토콜을 엮어 말할 수 있다

Hark 맥락에서의 한 문장 [추정]: "집 안 기기 라인업까지 가면 Thread/Matter가 자연스러운 선택지고, 그때도 최초 설정은 BLE로 하게 된다."

### 2.7 Coexistence — 2.4 GHz에서 셋이 싸울 때

```
 2.4 GHz 대역
 |---- Wi-Fi ch1 ----|---- Wi-Fi ch6 ----|---- Wi-Fi ch11 ----|
  BLE adv 37 ^            BLE adv 38 ^          BLE adv 39 ^
  (Wi-Fi 채널 사이 틈에 일부러 배치)
  802.15.4 ch11~26 은 20/25/26 이 Wi-Fi 1/6/11 과 겹침이 적음
```

충돌은 두 종류다.
- **In-band 간섭**: 다른 기기의 전파. 해결은 주파수 회피(BLE adaptive frequency hopping, Wi-Fi 채널 선택, 802.15.4 채널 고정)
- **같은 기기 안의 충돌**: 안테나·프런트엔드·전원을 공유. 이건 회피가 아니라 **중재**가 필요 → PTA (Packet Traffic Arbitration)

PTA는 라디오 서브시스템 사이의 실선 신호다. 배선 수에 따라 2/3/4-wire라 부르고 이름은 벤더마다 다르다.

| 신호(대표 이름) | 방향 | 뜻 |
|---|---|---|
| BT_ACTIVE / REQUEST | BT → 중재기 | "지금 송수신하겠다" |
| BT_PRIORITY | BT → 중재기 | "이건 중요한 패킷이다"(연결 유지 등) |
| WLAN_ACTIVE / GRANT | WLAN → BT | "내가 쓰는 중" 또는 "허가" |

펌웨어가 하는 일: 핀 매핑·극성·활성 타이밍 설정, 우선순위 정책(예: 통화 오디오 > 벌크 업로드), 그리고 **문제 생겼을 때 이게 원인인지 판별**하는 것.

증상 판별 요령(면접 답으로 좋다):
- Wi-Fi를 완전히 끈 상태에서 BLE만 돌려 재현되면 coex 문제가 아니다
- coex 신호를 LA로 캡처해서 BT_ACTIVE가 실제로 토글되는지 본다. 토글이 없으면 설정/배선 문제
- 거리·전파 문제인지 보려면 RSSI와 PER(또는 CRC 에러 카운터)을 같이 본다
- 스니퍼(nRF Sniffer + Wireshark)로 공중에서 실제로 재전송이 늘었는지 확인

더 깊은 내용은 `C06 §11`.

---

## 3. 실무 패턴과 함정

### 3.1 무선 문제를 층으로 쪼개는 절차

이 절차를 그대로 말할 수 있으면 L3(시스템 판별)를 통과한다.

```
 증상: "가끔 끊긴다"
   │
   ├─ [1] 범위 확정 — 몇 % 유닛? 특정 보드/특정 환경/특정 폰?
   │       유닛 특정 → HW/부품 편차. 전 유닛 → FW/설정
   │
   ├─ [2] 링크 계층 로그 — 끊김 사유 코드
   │       supervision timeout / remote user terminated / MIC failure ...
   │       사유가 다르면 원인이 다르다
   │
   ├─ [3] 공중 확인 — 스니퍼 캡처
   │       재전송 증가? 아예 이벤트가 안 나감? 상대가 응답 안 함?
   │
   ├─ [4] 호스트 경계 확인 — HCI 로그 / 전송 계층
   │       호스트가 커맨드를 늦게 보냈나, 컨트롤러가 이벤트를 흘렸나
   │
   ├─ [5] 물리 — 슬립 클럭 정확도, 전원 레일 sag, coex 신호, 안테나 접촉
   │
   └─ [6] 시스템 — CPU 기아(고우선순위 태스크가 스택 태스크를 굶김),
           인터럽트 지연, 플래시 쓰기 중 라디오 이벤트 miss
```

> [5]와 [6]이 **Don이 이미 남보다 잘하는 구간**이다. 면접에서 [1]~[4]를 정확히 말하고 [5]~[6]에서 실제 사례를 꺼내면, "familiarity" 요건은 넘어서 "이 사람 있으면 bring-up이 편하겠다"로 바뀐다.

### 3.2 흔한 함정 표

| 함정 | 증상 | 진짜 원인 | 고치는 법 |
|---|---|---|---|
| 32.768 kHz 정확도 미달 | 연결이 수십 초~수 분 뒤 끊김, 슬립 전류도 높음 | window widening이 커져 수신 창을 놓침 (C06 §5.5) | 크리스털 ppm 사양 확인, 부하 커패시터, RC 오실레이터 캘리브레이션 |
| supervision timeout 계산 실수 | latency를 올렸더니 연결이 툭툭 끊김 | `timeout > (1+latency) × interval × 2` 위반 | 세 파라미터를 함께 계산해서 설정 |
| ATT MTU 협상 안 함 | throughput이 기대의 1/5 | 기본 MTU 23 → payload 20바이트 | MTU exchange + DLE + 2M PHY를 같이 켠다 (C06 §5.7) |
| 플래시 쓰기 중 라디오 이벤트 누락 | OTA 중에만 끊김 | 플래시 erase가 수십~수백 ms 동안 코어/버스를 막음 | erase를 잘게 쪼개고 연결 이벤트 사이에 배치, 또는 인터벌을 늘림 |
| 스택 태스크 우선순위 낮게 설정 | 부하가 걸릴 때만 끊김 | RTOS에서 무선 스택은 시간 제약이 강함 | 스택/라디오 태스크를 높은 우선순위로, 긴 임계구역 제거 |
| coex 핀 극성 반대 | Wi-Fi 켜면 BLE PER 급상승 | active-high/low 설정 오류 | LA로 파형 확인, 데이터시트 대조 |
| 페어링 정보(bond) 저장 실패 | 재부팅하면 다시 페어링 요구 | NVS 영역 미초기화/용량 부족 | bond 저장소 용량과 초기화 확인 |
| 재연결 폭주 | 연결 실패 후 전류 급증 | 즉시 무한 재시도 | 지수 백오프 + 상한, 실패 사유별 정책 |
| regdomain 미설정 | 특정 국가/채널에서만 스캔 실패 | Wi-Fi 규제 도메인 | 공장에서 국가 코드 프로비저닝 (C09와 연결) |
| 한 번의 재현으로 결론 | 잘못된 수정 후 재발 | 무선은 확률적 | PER/재전송 카운터로 **통계**를 본다 |

### 3.3 무선을 붙인 기기의 펌웨어 코드 — 두 가지 패턴

**패턴 1: 연결 상태를 제품 상태 머신으로 다루기**

```c
/* 무선 링크는 "끊길 수 있는 자원"이다. 콜백이 아니라 상태로 다룬다. */
typedef enum {
    LINK_IDLE,        /* 라디오 꺼짐 */
    LINK_ADVERTISING, /* 광고 중 (전류 낮음) */
    LINK_CONNECTED,   /* 연결됨 */
    LINK_BACKOFF,     /* 실패 후 대기 */
} link_state_t;

typedef struct {
    link_state_t state;
    uint32_t     backoff_ms;      /* 지수 백오프 */
    uint8_t      last_disc_reason;/* 링크 계층이 준 사유 코드 */
    uint32_t     disc_count;      /* 텔레메트리로 올릴 카운터 */
} link_ctx_t;

/* 끊김 콜백은 "기록하고 상태만 바꾼다". 여기서 재연결을 직접 하지 않는다.
   콜백은 스택 컨텍스트라 오래 붙잡으면 다음 이벤트를 놓친다. */
void on_disconnected(link_ctx_t *ctx, uint8_t reason)
{
    ctx->last_disc_reason = reason;
    ctx->disc_count++;
    ctx->state = LINK_BACKOFF;
    ctx->backoff_ms = (ctx->backoff_ms == 0) ? 100
                    : (ctx->backoff_ms < 30000 ? ctx->backoff_ms * 2 : 30000);
    /* 실제 재시도는 애플리케이션 태스크에서 타이머로 */
}
```

**패턴 2: 연결 파라미터를 "모드"로 묶기** — 제품 팀과 협상할 때 이 표가 대화의 언어가 된다.

```c
typedef struct {
    uint16_t interval_min_units; /* 1.25 ms 단위 */
    uint16_t interval_max_units;
    uint16_t latency;            /* 건너뛸 이벤트 수 */
    uint16_t timeout_units;      /* 10 ms 단위 */
} conn_param_t;

/* 값은 예시다. 제품 요구(체감 지연)와 전류 예산으로 결정한다. */
static const conn_param_t MODE_INTERACTIVE = { 12, 24,  0, 400 }; /* 15~30 ms, 4 s */
static const conn_param_t MODE_IDLE        = { 80, 160, 4, 600 }; /* 100~200 ms, 6 s */
static const conn_param_t MODE_BULK        = { 6,  12,  0, 400 }; /* 7.5~15 ms, 업로드 */

/* 검증: timeout_ms > (1 + latency) * interval_max_ms * 2 */
static int conn_param_is_valid(const conn_param_t *p)
{
    uint32_t interval_max_ms = (uint32_t)p->interval_max_units * 125u / 100u;
    uint32_t timeout_ms      = (uint32_t)p->timeout_units * 10u;
    return timeout_ms > (uint32_t)(1u + p->latency) * interval_max_ms * 2u;
}
```

> 면접에서 "연결 파라미터를 어떻게 정하나?"가 나오면 **"하나의 값이 아니라 모드로 관리하고, 상태에 따라 전환하며, 항상 supervision timeout 부등식을 검증한다"**고 답한다. 이건 실무를 해 본 사람의 답이다.

### 3.4 관측 가능성 — 무선은 "로그 설계"가 실력이다

필드에서 재현되지 않는 게 무선 문제의 본질이다. 그래서 **무엇을 세어 두었는가**가 실력을 가른다.

| 지표 | 왜 필요한가 |
|---|---|
| 끊김 사유 코드별 카운트 | timeout인지 상대가 끊은 건지 보안 실패인지 구분 |
| 연결 지속 시간 히스토그램 | "가끔"이 얼마나 가끔인지 정량화 |
| 재연결 시도 횟수·성공률 | 백오프 정책 검증 |
| 평균/최소 RSSI | 전파 문제와 로직 문제 구분 |
| CRC 오류 / 재전송 수 | 간섭·coex 증거 |
| 라디오 on-time 누적 | 전류 예산 회귀 감시 |
| 컨트롤러 펌웨어 버전 | 유닛별 편차 추적 |

> Don의 SSD telemetry 경험(NVMe telemetry 디버그 기능, error reporting/handling 설계)이 **그대로 옮겨지는 지점**이다. 무선 스택을 안 짜 봤어도, "필드에서 안 잡히는 문제를 잡을 수 있게 펌웨어를 설계해 본 사람"이라는 증거가 된다.

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| Bluetooth Core Specification | BLE 전 계층의 유일한 규범. 타이밍 값·HCI 정의 | Vol 6 Part B (Link Layer), Vol 3 Part F/G (ATT/GATT), Vol 4 Part A/D (HCI UART, 3-wire) | https://www.bluetooth.com/specifications/specs/ |
| Bluetooth Assigned Numbers | UUID, 회사 ID, appearance 값 | GATT 서비스/특성 UUID 표 | https://www.bluetooth.com/specifications/assigned-numbers/ |
| Zephyr Bluetooth 문서 | 실제 구현으로 본 GAP/GATT API, HCI 드라이버 | Bluetooth API + samples/bluetooth | https://docs.zephyrproject.org/latest/connectivity/bluetooth/index.html |
| Zephyr Wi-Fi 문서 | Wi-Fi 관리 API, power save 설정 | Wi-Fi Management | https://docs.zephyrproject.org/latest/connectivity/networking/api/wifi.html |
| Nordic 개발자 문서 | nRF Connect SDK, 연결 파라미터·전력 가이드, coex | Bluetooth LE 섹션, Power optimization | https://docs.nordicsemi.com/ |
| nRF Sniffer for Bluetooth LE | 공중 패킷 캡처 도구 (Wireshark 플러그인) | 설치 + 캡처 필터 | https://www.nordicsemi.com/Products/Development-tools/nRF-Sniffer-for-Bluetooth-LE |
| Wireshark | BLE/802.15.4/802.11 디섹터 | 캡처 후 프로토콜 트리 읽기 | https://www.wireshark.org/ |
| BlueZ | Linux BT 스택. `btmon`으로 **HCI 트래픽을 눈으로 본다** | btmon 출력 형식 | https://github.com/bluez/bluez |
| IEEE 802.11 | Wi-Fi MAC/PHY 규범 | 비콘/TIM/DTIM, power management 절 | https://standards.ieee.org/ieee/802.11/7028/ |
| IEEE 802.15.4 | Thread/Zigbee의 PHY·MAC | 채널 배치, 프레임 구조 | https://standards.ieee.org/ieee/802.15.4/7029/ |
| Linux 802.11 driver API | cfg80211/mac80211 구조, FullMAC vs SoftMAC | 드라이버 아키텍처 절 | https://www.kernel.org/doc/html/latest/driver-api/80211/index.html |
| wpa_supplicant | STA 연결·인증(WPA2/WPA3/SAE) 참조 구현 | 상태 머신, 로그 메시지 | https://w1.fi/wpa_supplicant/ |
| OpenThread | Thread의 오픈소스 구현 + 개념 문서 | Thread Primer (역할, mesh, border router) | https://openthread.io/guides |
| OpenThread 소스 | 실제 스택 구조 | `src/core/thread` | https://github.com/openthread/openthread |
| Thread Group 스펙 | Thread 1.x 규범 (가입 필요할 수 있음) | 역할 정의, child timeout | https://www.threadgroup.org/ThreadSpec |
| CSA Matter | Matter 개요·인증 | 커미셔닝 흐름(BLE 경유) | https://csa-iot.org/all-solutions/matter/ |
| Matter 구현 | 실제 코드 | `src/platform` (플랫폼별 BLE/Wi-Fi 연동) | https://github.com/project-chip/connectedhomeip |
| Wi-Fi Alliance | WPA3, Wi-Fi 6/7 인증 프로그램 정의 | 보안 프로그램 페이지 | https://www.wi-fi.org/ |

**버전·벤더 의존 표시**
- Bluetooth Core Spec 버전마다 기능이 추가된다(5.0 2M/Coded, 5.2 LE Audio, 5.4 PAwR, 6.0 Channel Sounding). **어느 기능이 몇 버전인지는 위 SIG 페이지로 확인**할 것.
- 콤보 칩의 펌웨어/패치 다운로드 절차, 벤더 HCI opcode, coex 핀 이름은 **전적으로 벤더·칩 리비전 의존**이다. "벤더 문서를 봐야 한다"고 말하는 게 정답이다.
- Zephyr/NCS API 이름은 릴리스마다 바뀔 수 있다. `docs.zephyrproject.org/latest`는 개발 브랜치 문서이므로, 특정 릴리스를 쓸 때는 해당 버전 문서를 봐야 한다.

---

## 5. 예상 면접 질문

난이도별로 배치했다. **기초는 반드시 막힘없이**, **중급은 트레이드오프까지**, **심화는 "모르는 부분을 정확히 인정하고 아는 축으로 끌어오는 것"**이 목표다.

| # | 난이도 | 주제 |
|---|---|---|
| Q01~Q03 | 기초 | BLE 상태·파라미터·GATT |
| Q04~Q05 | 기초 | Wi-Fi 연결 / Thread 비교 |
| Q06~Q08 | 중급 | 전력 트레이드오프, throughput, 보안 |
| Q09~Q11 | 중급 | host–controller 경계, 콤보 칩 bring-up, coexistence |
| Q12~Q14 | 심화 | 디버깅 시나리오, 아키텍처 선택 |
| Q15 | 정직성 | 경험 범위 확인 질문 |

### Q01. Walk me through what happens from powering on a BLE peripheral to exchanging data with a phone.

**왜 묻나**: 용어를 정확히 쓰는지, 상태 전이를 그릴 수 있는지. 이걸 못 하면 나머지 질문을 하지 않는다.
**30초 답변**: 전원 인가 → 스택 초기화 → advertising 시작(37/38/39 채널에서 interval마다 패킷 송출) → 중앙 기기가 스캔해서 연결 요청 → 연결 수립되면 connection interval마다 연결 이벤트에서 데이터 교환 → 그 위에 ATT/GATT로 서비스·특성 discovery → notify/write로 데이터. 필요하면 MTU 교환과 연결 파라미터 갱신을 한다.
**English answer**: After the stack initializes, the peripheral starts advertising on the three primary channels, 37, 38 and 39, at whatever advertising interval we configured. A central scans, sees the advertisement, and sends a connect request. Once connected, the two sides wake up together every connection interval and exchange link layer packets. Above that, the central discovers our GATT services and characteristics, and we typically negotiate a larger ATT MTU and updated connection parameters. From then on the peripheral sends notifications and the central writes to characteristics.
**꼬리질문**
- "Advertising interval의 허용 범위는?" → 20 ms에서 10.24 s, 0.625 ms 단위, 여기에 0~10 ms 랜덤 지연이 더해져 충돌을 줄인다.
- "왜 하필 37/38/39인가?" → 2402/2426/2480 MHz로 Wi-Fi 1/6/11 채널 사이 틈에 배치돼 간섭을 피하려는 설계.
- "연결 후에도 광고를 계속할 수 있나?" → 가능하다. 다중 연결이나 다른 역할을 위해 병행 광고를 쓴다.

### Q02. What do connection interval, peripheral latency and supervision timeout do, and how are they related?

**왜 묻나**: L2(트레이드오프)의 대표 질문. 세 값을 함께 다룰 줄 아는지.
**30초 답변**: connection interval은 깨어나는 주기(7.5 ms~4 s, 1.25 ms 단위), peripheral latency는 보낼 게 없을 때 건너뛸 수 있는 이벤트 수(0~499), supervision timeout은 이 시간 동안 아무것도 못 받으면 끊는 값(100 ms~32 s, 10 ms 단위). 세 값은 `timeout > (1 + latency) × interval × 2`를 반드시 만족해야 하고, 안 그러면 정상 동작 중에도 끊긴다.
**English answer**: The connection interval sets how often both sides wake up — it ranges from 7.5 milliseconds to 4 seconds in 1.25 millisecond units. Peripheral latency lets the peripheral skip up to 499 connection events when it has nothing to send, so you keep low latency in the central-to-peripheral direction while cutting average current. Supervision timeout is how long we tolerate hearing nothing before declaring the link lost. The constraint that trips people up is that the timeout must be greater than one plus the latency, times the interval, times two — otherwise you drop connections during perfectly normal operation.
**꼬리질문**
- "latency를 올리는 게 interval을 늘리는 것보다 나은 경우는?" → 중앙 → 주변 방향 명령의 반응은 빠르게 유지하면서 평균 전류만 줄이고 싶을 때.
- "주변 기기가 파라미터를 직접 바꿀 수 있나?" → 갱신을 요청하고 중앙이 승인한다. 폰 OS는 허용 범위가 정해져 있어 요청이 거절될 수 있다.

### Q03. Design the GATT interface for a wearable that streams sensor events and receives commands.

**왜 묻나**: GATT를 "데이터 모델"로 이해하는지. 코딩이 아니라 설계 감각.
**30초 답변**: 서비스 하나에 특성 세 개 — 센서 이벤트는 notify(수신 확인이 필요하면 indicate), 명령은 write(응답 필요하면 write-with-response), 상태·버전은 read. 이벤트는 개별 전송이 아니라 배치로 묶어 보내고, MTU를 협상해 한 번에 많이 보낸다. 페이로드는 고정 헤더 + 버전 필드로 설계해 나중에 확장할 수 있게 한다.
**English answer**: I'd define one custom service with a small number of characteristics: a notify characteristic for sensor events, a write characteristic for commands, and a read characteristic for device state and firmware version. I'd batch sensor samples into a single notification rather than sending one per sample, because each notification costs a connection event. I'd negotiate the ATT MTU up front and size the payload to fit one packet after data length extension. And I'd put a version byte in the payload header so we can evolve the format without breaking older phone apps.
**꼬리질문**
- "notify와 indicate 차이?" → indicate는 애플리케이션 레벨 확인 응답을 받으므로 신뢰성은 높지만 처리량이 낮다.
- "표준 서비스를 쓸 수는 없나?" → Battery Service, Device Information Service 같은 표준은 그대로 쓰고, 제품 고유 데이터만 커스텀 UUID로.

### Q04. What happens between "connect to this SSID" and having a working IP address?

**왜 묻나**: Wi-Fi를 "붙는다/안 붙는다"가 아니라 단계로 보는지. 디버깅 능력의 지표.
**30초 답변**: 스캔(수동: 비콘 수신 / 능동: probe) → authentication(개방, WPA3면 SAE) → association(AID 할당) → 4-way handshake로 키 생성 → DHCP로 IP. 각 단계에서 실패 양상이 다르다. 예를 들어 "연결됨인데 통신이 안 된다"는 보통 L2는 됐고 DHCP가 안 된 것이다.
**English answer**: The station scans, either passively by listening for beacons or actively with probe requests. Then it authenticates — open system for WPA2, or SAE for WPA3 — and associates, which is when the AP assigns an association ID. After that comes the four-way EAPOL handshake that derives the session keys. Only then does DHCP run and give us an IP. Knowing these stages matters for debugging: if the device says connected but nothing works, the link layer succeeded and the problem is almost always DHCP or something above it.
**꼬리질문**
- "WPA2와 WPA3의 실무적 차이는?" → WPA3는 SAE를 쓰고 PMF(management frame protection)가 필수라, 구형 기기와 혼재된 망에서 호환성 문제가 생길 수 있다.
- "5 GHz 전용 SSID에서 안 보인다면?" → 칩의 지원 대역, 그리고 지역 규제 도메인 설정을 먼저 본다.

### Q05. What is Thread, and when would you pick it over BLE or Wi-Fi?

**왜 묻나**: 셋을 비교할 수 있는지. Thread 경험을 기대하는 게 아니라 판단력을 본다.
**30초 답변**: Thread는 802.15.4 PHY/MAC(2.4 GHz, 250 kbps) 위에 6LoWPAN으로 IPv6를 올린 저전력 mesh다. 모든 노드가 IP 주소를 갖고, mesh라 노드 하나가 빠져도 경로가 복구되며, 배터리 기기는 sleepy end device로 붙는다. 집 안에 기기가 여러 개 깔리고 라우터 커버리지를 넘겨야 할 때 BLE보다 낫고, 전력 예산이 Wi-Fi를 감당 못 할 때 Wi-Fi보다 낫다. Matter의 전송 계층으로 쓰이고, 커미셔닝은 BLE로 한다.
**English answer**: Thread is a low-power IPv6 mesh built on 802.15.4 — the same radio class as BLE in terms of power, but with native IPv6 through 6LoWPAN and self-healing mesh routing. Every node is IP-addressable, and battery devices join as sleepy end devices that poll their parent. I'd choose it over BLE when I need many devices in a home covering more area than a star topology reaches, and over Wi-Fi when the power budget can't afford an 802.11 radio. In practice it shows up as the transport under Matter, and even then the initial commissioning happens over BLE.
**꼬리질문**
- "Thread와 Zigbee의 관계?" → 같은 802.15.4 기반이지만 Thread는 IPv6 네이티브이고 Zigbee는 자체 네트워크 계층을 쓴다.
- "Border Router가 하는 일?" → Thread mesh와 집안 Wi-Fi/이더넷 사이에서 IPv6를 라우팅하고 서비스 디스커버리를 중계한다.

### Q06. Our device must run for days on a small battery but still be reachable from the phone. How do you budget the radio?

**왜 묻나**: 전력과 무선을 엮는 능력. Hark JD의 "always-on, battery-powered"와 직결.
**30초 답변**: 라디오 전류는 "이벤트당 에너지 × 이벤트 빈도"로 본다. 연결 상태라면 interval과 latency가 빈도를 정하고, 이벤트당 에너지는 송수신 시간과 TX power가 정한다. 그래서 상태별 모드를 만든다 — 상호작용 중엔 짧은 interval, 유휴엔 긴 interval + latency. 연결이 없을 땐 광고 간격을 늘리고, 대용량 전송은 Wi-Fi를 짧게 켜서 끝내는 race-to-idle이 유리하다. 마지막으로 실제 계측(예: Nordic PPK2)으로 검증한다.
**English answer**: I model the radio as energy per event times event rate. In a connection, the interval and peripheral latency set the rate, and the transmit time and TX power set the energy per event. So I define modes: a short interval while the user is interacting, and a long interval with latency when idle. When disconnected, I stretch the advertising interval. For bulk transfers I prefer race-to-idle — bring Wi-Fi up, move the data fast, and shut it down — rather than keeping a slow link alive. Then I verify with a real current measurement rather than trusting the datasheet numbers.
**꼬리질문**
- "peripheral latency를 쓰면 왜 전류가 줄면서 반응성은 유지되나?" → 주변 기기가 보낼 게 없을 때만 건너뛰고, 중앙이 보낼 게 있으면 그 이벤트에서 받는다. 단, 건너뛰는 중엔 중앙의 데이터도 다음 이벤트까지 기다릴 수 있다는 점은 명확히 해야 한다.
- 계산 연습은 `S03 §5 E2`, `S03 §5 E4`.

### Q07. How do you maximize BLE throughput?

**왜 묻나**: 처리량을 제한하는 요소를 아는지. 스펙 수치 기억력도 함께 본다.
**30초 답변**: 네 가지를 동시에 올려야 한다 — PHY를 LE 2M으로, DLE로 LL payload를 251 octet까지, ATT MTU를 그에 맞게 키우고(기본 23은 payload 20뿐), connection interval을 짧게 해서 이벤트당 여러 패킷을 넣는다. 그리고 write-without-response나 notify를 쓴다. 상대(폰 OS)가 지원하지 않으면 협상 결과가 내려앉으므로, 협상된 실제 값을 로그로 남겨야 한다.
**English answer**: Four things together. Move to the 2 megabit PHY, enable data length extension so a link layer packet carries up to 251 octets, raise the ATT MTU to match — the default of 23 only gives you 20 bytes of payload — and shorten the connection interval so more packets fit per event. Use notifications or write-without-response so you're not paying an acknowledgement round trip per operation. And always log what was actually negotiated, because the phone side often caps what you asked for.
**꼬리질문**
- "짧은 interval의 대가는?" → 평균 전류 증가와 coex 압박. 그래서 벌크 모드에서만 쓴다.
- "그래도 처리량이 안 나오면?" → 재전송률과 RSSI를 본다. 간섭이면 파라미터로 해결되지 않는다.

### Q08. How does BLE pairing and bonding work, and how would you secure a consumer device?

**왜 묻나**: 보안을 "켜는 것"이 아니라 설계로 보는지.
**30초 답변**: pairing은 연결 중에 키를 만들고 링크를 암호화하는 과정, bonding은 그 키를 저장해서 다음에 다시 페어링하지 않게 하는 것. LE Secure Connections(4.2~)는 ECDH를 써서 수동 도청을 막는다. 연결 방식(association model)은 기기의 입출력 능력에 따라 Just Works / Passkey / Numeric Comparison / OOB로 갈리고, Just Works는 MITM 방어가 없다. 화면 없는 웨어러블이면 NFC 같은 OOB나 폰 앱을 통한 확인을 붙이고, 민감한 특성은 암호화·인증된 링크에서만 접근 가능하게 권한을 설정한다. Privacy(RPA)로 주소 추적도 막는다.
**English answer**: Pairing generates keys and encrypts the link; bonding stores those keys so the devices trust each other on the next connection. LE Secure Connections uses ECDH, which protects against passive eavesdropping. The association model depends on the device's input and output capability — Just Works has no man-in-the-middle protection, so for a headless wearable I'd either use an out-of-band channel like NFC, or drive confirmation through the companion app. Separately, I'd set per-characteristic permissions so anything sensitive requires an encrypted, authenticated link, and enable privacy with resolvable private addresses so the device isn't trackable by its MAC.
**꼬리질문**
- "bond가 깨지는 흔한 원인은?" → 한쪽만 bond를 지웠을 때(폰에서 기기를 "잊음"), 또는 기기 쪽 NVS 저장 실패. 증상은 재연결 실패나 MIC failure로 나타난다.
- "공장에서 무엇을 프로비저닝해야 하나?" → 기기 고유 주소/식별자와 필요하면 기기 인증서. `C09` 참조.

### Q09. The BLE controller is a separate chip on a UART. What does the host firmware actually have to do?

**왜 묻나**: **이 질문이 Don의 홈그라운드다.** host–controller 경계를 아는 사람은 드물다.
**30초 답변**: 전원·클럭·리셋 시퀀스를 맞추고, HCI 전송(H4면 하드웨어 흐름 제어 필요, H5면 재전송·CRC 포함)을 열고, HCI Reset부터 시작해 벤더 펌웨어/패치를 다운로드하고, 보레이트를 올린 뒤 다시 초기화하고, 로컬 버전을 읽어 로깅한다. 그리고 컨트롤러가 죽었을 때 복구하는 경로(재리셋 + 재다운로드)를 반드시 만든다. bring-up에서 깨지는 건 대부분 32.768 kHz 슬립 클럭, 리셋 타이밍, 보레이트 전환 구간, 흐름 제어 배선이다.
**English answer**: The host owns everything around the link, not the link itself. It sequences power rails, provides the sleep clock, releases reset with the right timing, opens the HCI transport — H4 needs hardware flow control, H5 adds retransmission and CRC so it survives without it — and then runs the bring-up sequence: HCI Reset, vendor firmware or patch download, baud rate change, reset again, read local version and log it. I also make sure there's a recovery path, because a controller that wedges has to be power-cycled and reloaded rather than leaving the product dead. In my experience the failures cluster in four places: the 32.768 kilohertz sleep clock, reset release timing, the baud rate switch window, and flow control wiring.
**꼬리질문**
- "H4와 H5의 차이?" → H4는 순수 프레이밍만, 흐름 제어를 하드웨어에 맡긴다. H5(Three-wire UART)는 시퀀스 번호·ACK·CRC를 얹어 RTS/CTS 없이도 손실을 복구한다.
- "슬립 클럭이 왜 그렇게 중요한가?" → 컨트롤러가 다음 연결 이벤트 시각을 예측하려면 저속 클럭 정확도가 필요하다. 오차가 크면 수신 창(window widening)이 넓어져 전류가 오르고, 심하면 이벤트를 놓쳐 끊긴다. (C06 §5.5)
- "HCI를 어떻게 눈으로 보나?" → 스택이 제공하는 HCI 로깅을 켜거나, Linux에서는 `btmon`으로 본다. 전송 계층 자체가 의심되면 LA로 UART를 캡처해 패킷 타입 바이트부터 디코드한다.

### Q10. Wi-Fi and Bluetooth share the 2.4 GHz front end. How does coexistence work and what does firmware own?

**왜 묻나**: 시스템 관점. 그리고 Hark 기기처럼 라디오가 6종 들어가는 제품에서 실제로 터지는 문제다.
**30초 답변**: 같은 대역의 다른 기기와의 간섭은 주파수 회피(BLE의 adaptive frequency hopping, Wi-Fi 채널 선택)로 줄이고, 같은 기기 안의 충돌은 회피가 아니라 중재가 필요하다. 그게 PTA — BT_ACTIVE / BT_PRIORITY / WLAN_ACTIVE 같은 실선 신호로 누가 언제 송수신할지 조정한다. 펌웨어는 핀 매핑·극성·타이밍을 설정하고 우선순위 정책을 정하며, 문제가 생겼을 때 이게 원인인지 판별한다.
**English answer**: Two different problems. Interference from other devices is handled in the frequency domain — BLE hops adaptively away from bad channels, Wi-Fi picks a channel. But two radios inside the same product sharing an antenna and front end can't avoid each other in frequency; they have to be arbitrated in time. That's packet traffic arbitration: real wires between the subsystems, typically a request line, a priority line and a grant or active line. Firmware owns the pin mapping, polarity and timing configuration, and the priority policy — for example, voice traffic outranks a bulk log upload. And when something breaks, firmware has to prove whether coex is the cause: turn one radio fully off and see if it reproduces, then capture the coex lines on a logic analyzer to confirm they're actually toggling.
**꼬리질문**
- "coex가 원인인지 어떻게 증명하나?" → 한쪽 라디오를 완전히 끈 상태에서 재현 여부, coex 신호 파형 캡처, 그리고 PER/재전송 카운터의 상관관계.
- "안테나가 하나면?" → 안테나 스위치 제어까지 시퀀싱에 들어가고, 전환 타이밍이 어긋나면 두 라디오 모두 성능이 떨어진다.

### Q11. Which radio architecture would you pick for a battery wearable: integrated SoC radio or an external combo chip?

**왜 묻나**: 설계 판단. 정답이 없고, 트레이드오프를 말하면 통과.
**30초 답변**: 통합 SoC는 부품 수·BOM·전력이 유리하고 HCI 전송 디버깅이 없어 bring-up이 빠르다. 대신 그 벤더의 스택과 로드맵에 묶인다. 외부 콤보 칩은 Wi-Fi·BT를 한 번에 얻고 라디오 성능이 검증된 걸 쓸 수 있지만, 전원 시퀀스·펌웨어 다운로드·coex 배선·전송 계층이라는 실패 지점이 추가된다. 제품이 셀룰러·GNSS·NFC·UWB까지 있는 멀티 라디오면 이미 중재 문제가 있으니, coex 지원이 검증된 조합을 고르는 게 실질적인 기준이다.
**English answer**: An integrated radio on the application SoC or MCU gives you fewer parts, lower idle power, and no HCI transport to debug, which makes bring-up much faster — but you're tied to that vendor's stack. An external combo chip gets you Wi-Fi and Bluetooth in one place with proven RF performance, at the cost of a whole class of failure modes: power sequencing, firmware download, coexistence wiring, and the transport itself. For a device that already carries several radios, I'd weight coexistence support and vendor validation of that specific combination more heavily than BOM cost, because arbitration problems are the ones that show up late and are hardest to fix.
**꼬리질문**
- "일정 관점에서는?" → 외부 콤보 칩은 벤더 패치 릴리스에 일정이 묶인다. 이건 기술 문제가 아니라 프로그램 리스크로 관리해야 한다.

### Q12. Users report the device randomly disconnects from their phone. How do you debug it?

**왜 묻나**: 무선 문제를 층으로 쪼개는 절차(§3.1)를 가지고 있는지. **이 질문 하나에 가장 많은 점수가 걸린다.**
**30초 답변**: 먼저 범위를 좁힌다 — 전 유닛인지 일부인지, 특정 폰/OS인지, 특정 환경(사람 많은 곳)인지. 그다음 링크 계층이 준 끊김 사유 코드를 본다. supervision timeout이면 타이밍·클럭·CPU 기아 쪽, remote user terminated면 폰 쪽 정책, MIC failure면 bond/보안 쪽이다. 스니퍼로 공중을 캡처해 재전송이 느는지, 아예 이벤트가 안 나가는지 본다. 동시에 RSSI·재전송 카운터를 통계로 본다. 마지막으로 시스템 요인 — 플래시 쓰기나 고우선순위 태스크가 스택을 굶기는지, 32.768 kHz 정확도가 맞는지.
**English answer**: I start by scoping it: all units or some, a particular phone or OS, a particular environment. Then I look at the disconnect reason code, because they point at completely different causes — supervision timeout points at timing, clock accuracy or CPU starvation; remote user terminated points at the phone's policy; a MIC failure points at bonding or security state. Next I capture on air with a sniffer to see whether we're retransmitting heavily or missing events entirely, and I correlate with RSSI and retransmission counters so I'm looking at statistics, not one anecdote. If the air looks fine, I look at the system: a long flash erase or a high priority task can starve the stack and make it miss connection events, and an out-of-spec 32 kilohertz sleep clock widens the receive window until events are missed.
**꼬리질문**
- "재현이 안 되면?" → 필드 텔레메트리 설계로 간다. 사유 코드별 카운트, 연결 지속 시간, RSSI 히스토그램을 누적해 올린다(§3.4).
- "폰 OS 때문이라고 어떻게 증명하나?" → 같은 펌웨어로 여러 폰 모델을 비교하고, 폰 쪽 HCI 로그(Android의 btsnoop 등)를 받아 대조한다.

### Q13. During bring-up of a new board, BLE works but the sleep current is 10x the budget. Where do you look?

**왜 묻나**: 무선 + 저전력 + bring-up을 엮는 통합 질문. Hark JD 세 항목이 한 번에 걸린다.
**30초 답변**: 먼저 라디오 탓인지부터 가른다. 라디오를 완전히 끈 상태의 전류를 재서 베이스라인을 잡는다. 라디오가 원인이면 광고/연결 파라미터, 슬립 클럭 소스(정확도가 나쁘면 수신 창이 넓어져 on-time이 늘어난다), 컨트롤러가 실제로 저전력 모드에 드는지를 본다. 라디오가 아니면 GPIO 미설정 핀, 풀업으로 흐르는 전류, 디버그 인터페이스 활성 상태, 켜진 채인 레귤레이터·센서를 본다. 도구는 고다이내믹레인지 전류 측정기와 전류 파형을 시간축에서 보는 것이다.
**English answer**: First I separate radio from everything else by measuring with the radio fully off, which gives me a baseline. If the radio is the cause, I check the advertising and connection parameters, the sleep clock source — a poor crystal widens the receive window and increases on-time — and whether the controller is actually entering its low power state, which often fails because the host keeps the transport awake or a flow control line is held asserted. If the radio isn't the cause, it's the usual bring-up list: unconfigured GPIOs floating or fighting pull-ups, the debug interface left enabled, regulators or sensors never put to sleep. I measure with a current profiler that can span microamps to tens of milliamps and look at the current waveform in time, because the shape tells you which subsystem it is.
**꼬리질문**
- "HCI 전송이 슬립을 막는 사례?" → 흐름 제어 라인이 계속 어서트돼 있거나 호스트가 UART를 열어 둔 채면 컨트롤러가 깊은 슬립에 못 든다. 벤더가 제공하는 슬립 프로토콜(전용 핀 또는 벤더 HCI 커맨드)을 맞춰야 한다 — **벤더 의존**.
- 관련: `C05`, `S03 §1`.

### Q14. How do you decide the split between the always-on MCU and the application SoC for connectivity?

**왜 묻나**: Hark 기기 구조 [추정]에 대한 설계 대화. 시니어리티 판별.
**30초 답변**: 기준은 "SoC를 깨우지 않고 끝낼 수 있는가"다. 항상 살아 있어야 하는 제어 링크(BLE)는 MCU에, 대역폭이 필요한 것(Wi-Fi 업로드, OTA)은 SoC에 둔다. MCU가 이벤트를 모아 두었다가 임계치에 도달할 때만 SoC를 깨우면 평균 전류가 크게 준다. 대신 두 쪽이 같은 상태(페어링 정보, 연결 상태)를 봐야 하므로 IPC 프로토콜과 소유권을 명확히 정해야 하고, 부팅·업데이트 순서도 설계해야 한다.
**English answer**: The rule I use is: whatever can be handled without waking the application processor belongs on the MCU. The always-on control link — typically BLE — lives on the MCU so the device stays reachable at microamp-level average current, and the MCU batches events and only wakes the SoC when there's real work. Bandwidth-heavy paths like Wi-Fi uploads and OTA downloads belong to the SoC, which is expensive but only runs in bursts. The hard part isn't the split, it's the shared state: pairing information, connection status, and who owns the user-facing behavior. That needs an explicit IPC protocol with clear ownership, plus a defined boot order and a defined update order for both images.
**꼬리질문**
- "두 이미지 버전이 어긋나면?" → IPC 프로토콜에 버전 필드를 두고 호환성 규칙을 정한다. OTA는 `J04`/`C07`.
- "MCU가 BLE를 가지면 폰 앱이 두 개를 보게 되나?" → 아니다. 하나의 기기로 보이게 GATT를 MCU 쪽에 통합하고 SoC 데이터는 MCU를 통해 중계하거나, 전송이 큰 건 별도 경로로 넘긴다.

### Q15. How much of the wireless stack have you actually written?

**왜 묻나**: 정직성 확인. **여기서 과장하면 그 뒤 모든 답의 신뢰가 무너진다.** 반대로 정직하게 선을 긋고 강한 축을 제시하면 신뢰가 올라간다.
**30초 답변**: 스택 자체(Link Layer, GATT, 802.11 MAC)를 구현한 적은 없다. 내 경험은 그 아래 경계 — 라디오 칩이 호스트 시스템에 붙는 지점이다. 전원·클럭·리셋 시퀀스, 호스트 인터페이스(PCIe, I2C, SPMI, RFFE), 통합 후에 나타나는 장애의 root cause. 스택 API 레벨은 지금 Zephyr로 직접 실습하며 메우고 있다.
**English answer**: I want to be precise about this. I have not implemented a link layer or a GATT stack — that's not what my work has been. What I have done, at Apple, is integrate new wireless silicon into shipping platforms: owning the host-side interfaces, PCIe, I2C, SPMI and RFFE, and root-causing the failures that only appear when a new radio meets the full hardware and software system. So I'm strong exactly where radios meet firmware — bring-up sequencing, transport, coexistence wiring, and proving whether a failure is hardware or software. The stack API layer, GATT services and connection parameter tuning, is the part I'm deliberately closing right now with hands-on work on Zephyr.
**꼬리질문**
- "그 실습에서 뭘 했나?" → **<확인 필요: nRF52840 DK + Zephyr로 BLE peripheral·전류 측정·MCUboot OTA 미니 프로젝트를 실제로 진행했는지. context 4.8 체크리스트 항목이며, 완료하지 않았다면 이 답을 하지 말 것.>**
- "그럼 첫 3개월에 뭘 할 수 있나?" → bring-up과 통합 이슈는 즉시 기여 가능하고, 스택 위쪽은 기존 코드베이스를 읽으며 따라잡는다.

---

## 6. Don 매핑

### 6.1 레쥬메에서 쓸 수 있는 근거 (context 3.1절)

| 근거 문장 | 이 요건에서의 쓸모 |
|---|---|
| Apple **RF-Hardware (Wireless) Chipset Integration** — 새 무선 칩을 출하 플랫폼에 통합 | "무선 도메인 안에서 일한다"는 직접 증거. 단, HW/인터페이스 수준임을 명시해야 함 |
| "root-cause analysis of fundamental and interface level failures … when a new chip meets the full HW/SW system" | §2.4의 host–controller 경계 문제와 1:1 |
| PCIe, I2C, SPMI, RFFE 장애 root cause | 콤보 칩 호스트 인터페이스(SDIO/PCIe/UART)와 같은 범주. **RFFE/SPMI는 RF 프런트엔드 제어 버스라 무선 맥락에서 특히 설득력 있음** |
| "Silicon/system bring up -> NPI -> MP" | 라디오 bring-up 경험으로 바로 연결 |
| DSO, protocol analyzer, Logic Analyzer, Power Analyzer | coex 신호 캡처, HCI UART 디코드, 전류 프로파일링 |
| SSD FW의 telemetry / error reporting·handling 설계 | §3.4 무선 관측 가능성 설계로 이식 |
| 신뢰성 vs 성능/전력 margin sign-off | 연결 파라미터 ↔ 전류 협상의 언어 |

### 6.2 정직한 포지셔닝 스크립트 (그대로 외울 것)

**한국어 뼈대**: "나는 스택 개발자가 아니라 **통합 엔지니어**다. 무선 칩이 시스템에 붙는 경계에서 일했고, 그 경계가 실제로 깨지는 곳을 안다. 스택 API 레벨은 지금 메우는 중이다."

**영어 60초 버전** (Q15보다 긴, 오프닝용)

> I work in Apple's wireless chipset integration group. To be clear about the boundary: I'm not writing link layer or GATT code. My job is what happens when a new radio silicon meets a real product — power and clock sequencing, the host interfaces it hangs off, PCIe, I2C, SPMI and RFFE, and root-causing failures that only show up in the full hardware and software system. That's the layer where bring-up actually fails: the sleep clock is out of spec, the reset timing is wrong, the firmware patch didn't load, the coexistence lines aren't wired the way the datasheet assumes. I've also carried that from first power-on through NPI into mass production, and built factory test flows around it. On the protocol side I know the model and the numbers — advertising and connection intervals, peripheral latency, supervision timeout and how they trade against current, the Wi-Fi association and four-way handshake sequence, and how Thread differs. What I'm adding deliberately right now is hands-on stack-level work, building a Zephyr BLE peripheral so I've written the GATT services and tuned the parameters myself rather than only debugging them.

**이 스크립트가 잘 작동하는 이유**
1. 첫 문장에서 선을 긋는다 → 이후 어떤 질문에도 방어적이지 않다
2. "그 경계가 실제로 깨지는 곳"을 구체적으로 나열 → familiarity를 훨씬 넘는 신호
3. 갭을 스스로 먼저 말하고 메우는 계획을 붙임 → 면접관이 파고들 동기를 없앰

### 6.3 하면 안 되는 말

| 하지 말 것 | 이유 | 대신 이렇게 |
|---|---|---|
| "BLE 스택 경험 있습니다" | 꼬리질문 두 번이면 드러난다 | "무선 칩 통합 경험이 있고, 스택 위쪽은 지금 실습으로 메우고 있다" |
| "Wi-Fi 드라이버를 다뤘습니다" | 레쥬메 근거 없음 | "호스트 인터페이스와 bring-up을 다뤘다" |
| "프로토콜은 비슷비슷하다" | 깊이 없음으로 들림 | 세 프로토콜 비교표(§2.2)를 수치와 함께 |
| Apple 업무를 과장 | 재직 중이라 검증 리스크 | 역할 경계를 정확히. 이게 오히려 시니어답게 들린다 |

### 6.4 확인이 필요한 항목

- **<확인 필요: Apple에서 BLE/Wi-Fi 칩의 HCI/SDIO/PCIe 전송 계층 문제를 직접 디버깅한 구체 사례가 있는지. 있다면 §2.4의 실패 표에 자기 사례를 하나 매핑해서 STAR로 준비할 것.>**
- **<확인 필요: coexistence(PTA) 신호나 안테나 스위칭 관련 이슈를 본 적이 있는지. 있다면 Q10의 강력한 뒷받침이 된다.>**
- **<확인 필요: 32.768 kHz 슬립 클럭이나 라디오 전원 시퀀스 관련 bring-up 이슈 경험. 이건 무선 펌웨어 면접에서 가장 값비싼 스토리다.>**
- **<확인 필요: Solidigm/SK hynix 시절 프로토콜 분석기로 PCIe/NVMe 링크 트레이닝 문제를 디버깅한 사례. 있다면 "링크 계층 문제를 계측으로 좁히는 절차"의 증거로 쓸 수 있다.>**
- **<확인 필요: nRF52840 DK + Zephyr 미니 프로젝트 진행 여부 (context 4.8). 미완료면 Q15·§6.2의 마지막 문장을 빼야 한다.>**

### 6.5 갭을 메우는 가장 효율적인 1주

레쥬메에 BLE를 쓰려면 **직접 만든 것**이 있어야 한다. 최소 투자로 최대 효과:

| 일차 | 할 일 | 얻는 답변 |
|---|---|---|
| 1~2 | Zephyr로 nRF52840 DK에 BLE peripheral 올리기. 커스텀 GATT 서비스 1개 + notify | Q01, Q03을 "직접 짜 봤다"로 |
| 3 | 연결 파라미터를 모드별로 바꿔 가며 전류 측정 (PPK2) | Q02, Q06을 **자기 측정값**으로 |
| 4 | MTU/DLE/2M PHY를 켜고 throughput 측정 | Q07을 숫자로 |
| 5 | nRF Sniffer + Wireshark로 자기 기기 캡처, 연결 이벤트 관찰 | Q12의 "스니퍼로 본다"를 실감 있게 |
| 6 | Linux에서 `btmon`으로 HCI 트래픽 보기 | Q09를 "실제로 본 적 있다"로 |
| 7 | OpenThread CLI로 노드 2개 mesh 구성 | Q05를 비교 이상으로 |

실습 절차는 `C06 §14`에 있다.

---

## 7. 준비 체크리스트

- [ ] §2.2 세 프로토콜 비교표를 **아무것도 안 보고 말로** 재생할 수 있다
- [ ] §2.3의 BLE 수치 8줄(광고/연결 인터벌, latency, timeout, MTU, DLE, PHY)을 암기했다
- [ ] supervision timeout 부등식을 예시 값으로 즉석에서 계산할 수 있다
- [ ] HCI 패킷 타입 5개(0x01~0x05)와 H4 vs H5 차이를 설명할 수 있다
- [ ] 콤보 칩 부팅 시퀀스(§2.4)를 화이트보드에 그릴 수 있고, 실패 지점 5개를 댄다
- [ ] Wi-Fi 연결 6단계와 각 단계의 실패 증상을 말할 수 있다
- [ ] §3.1 "끊김 디버깅 6단계"를 순서대로 말할 수 있다
- [ ] §6.2 포지셔닝 스크립트를 영어로 60초 안에, 막힘없이 말한다
- [ ] §6.4의 <확인 필요> 항목을 자기 기억과 대조해 채우거나 지운다
- [ ] (선택·고효율) §6.5의 1주 실습 중 최소 1~3일차를 실제로 수행해 자기 측정값을 갖는다

---

## 8. 더 읽기

| 가고 싶은 곳 | 노트 |
|---|---|
| BLE 전 계층 원리, 패킷 구조, GATT 구현 코드 | `C06 §2~§7` |
| BLE 평균 전류·처리량 계산 | `C06 §5.6`, `C06 §5.7` |
| window widening과 32 kHz 크리스털 | `C06 §5.5` |
| Wi-Fi power save, FullMAC vs SoftMAC | `C06 §8.2`, `C06 §8.3` |
| Thread·Matter 구조 | `C06 §9` |
| HCI 전송과 콤보 칩 펌웨어 다운로드 | `C06 §10` |
| 2.4 GHz coexistence 상세 | `C06 §11` |
| 무선 면접 문항 드릴 (Q11~Q23) | `S03 §3`, `S03 §4` |
| 배터리 수명·DTIM 계산 연습 | `S03 §5` |
| "BLE가 Wi-Fi 켤 때 끊긴다" 시나리오 답안 | `S06 D04` |
| 저전력 설계 원리 | `C05`, `S03 §1` |
| 라디오 칩 bring-up의 회로도·전원 측면 | `C10 §8`, `C10 §9`, `J13` |
| RTOS 태스크 우선순위가 스택을 굶기는 문제 | `C04`, `J11` |
| OTA를 무선으로 내릴 때의 제약 | `C07`, `J04` |
